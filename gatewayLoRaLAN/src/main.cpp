// gatewayLoRaLAN - gateway LoRaLAN nativo para Raspberry Pi.
//
// Ejecuta la biblioteca LoRaLAN SIN MODIFICAR sobre un SX1276 conectado por
// SPI, y la une al backend FastAPI mediante HTTP sobre sockets POSIX.
//
// Tres hilos
// ----------
// El motivo no es rendimiento, sino correccion: el hilo de radio nunca hace
// E/S de red. Un POST lento dentro de un callback retrasaria los ACK mas alla
// del timeout del nodo (1200 ms) y provocaria retransmisiones.
//
//   Radio    : bucle cerrado sobre gateway.process(). Los callbacks solo
//              encolan. Envia los ACK de fichaje y los CMD.
//   Fichajes : un POST por fichaje con timeout corto. Hilo propio para que un
//              uplink de telemetria lento no le haga perder el plazo del ACK.
//   Red      : telemetria (uplink), comandos pendientes y latido de estado.
//
// Fichaje = PUSH con ACK diferido (LoRaLANGateway::ackPush):
//
//   nodo --PUSH(UID)--> radio --cola--> fichajes --POST--> servidor
//   nodo <--ACK(code)-- radio <--cola-- fichajes <--code--
//
// Si el servidor no contesta antes de FICHAJE_DEADLINE_MS, la radio responde
// sola con REINTENTAR (NACK temporal) y el nodo reenvia el fichaje mas tarde.
//
// Sin WebSocket a proposito: al no existir un canal por el que el servidor
// pueda empujar comandos en cualquier momento, es el gateway quien decide
// cuando transmite, y por tanto quien controla su presupuesto de ciclo de
// trabajo.

#include <RadioLib.h>
#include "hal/RPi/PiHal.h"

#include "LoRaLANGateway.h"

#include "AppProtocol.h"
#include "bridge.h"
#include "config.h"
#include "net/http_client.h"
#include "radio/airtime.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ======================= Configuracion en tiempo de ejecucion ============

static std::string envStr(const char* name, const char* fallback) {
    const char* v = ::getenv(name);
    return v ? std::string(v) : std::string(fallback);
}

static long envLong(const char* name, long fallback) {
    const char* v = ::getenv(name);
    return v ? ::strtol(v, nullptr, 10) : fallback;
}

// ======================= Estado compartido ==============================

namespace {

// Cola sencilla protegida por mutex. push() nunca bloquea al hilo de radio:
// si esta llena se descarta lo mas antiguo.
template <typename T>
class Cola {
public:
    explicit Cola(size_t max) : max_(max) {}

    // Devuelve false si hubo que descartar un elemento para hacer sitio.
    bool push(T v) {
        std::lock_guard<std::mutex> lock(m_);
        bool sitio = true;
        if (q_.size() >= max_) {
            q_.pop_front();
            sitio = false;
        }
        q_.push_back(std::move(v));
        cv_.notify_one();
        return sitio;
    }

    bool tryPop(T& out) {
        std::lock_guard<std::mutex> lock(m_);
        if (q_.empty()) return false;
        out = std::move(q_.front());
        q_.pop_front();
        return true;
    }

    // Espera hasta timeoutMs o hasta que haya algo (o se pida parar).
    bool waitPop(T& out, int timeoutMs, const std::atomic<bool>& running) {
        std::unique_lock<std::mutex> lock(m_);
        cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                     [&] { return !q_.empty() || !running; });
        if (q_.empty()) return false;
        out = std::move(q_.front());
        q_.pop_front();
        return true;
    }

    void wakeAll() { cv_.notify_all(); }

private:
    std::deque<T> q_;
    std::mutex m_;
    std::condition_variable cv_;
    size_t max_;
};

struct FichajeReq {
    uint8_t     node;
    uint16_t    seq;       // seq del PUSH: identifica el ACK pendiente
    std::string uid;       // hexadecimal
    uint32_t    recibidoMs;
};

struct FichajeRes {
    uint8_t node;
    uint16_t seq;
    uint8_t code;
};

Cola<std::string>        g_uplinks(UPLINK_QUEUE_MAX);   // JSON listo para POST
Cola<FichajeReq>         g_fichajes(64);
Cola<FichajeRes>         g_resultados(64);
Cola<bridge::Command>    g_comandos(64);

std::atomic<bool>     g_running{true};
std::atomic<unsigned> g_droppedUplinks{0};
std::atomic<unsigned> g_pushCount{0};
std::atomic<unsigned> g_pollCount{0};
std::atomic<unsigned> g_nodeCount{0};

std::string g_gwName;

radio::LoRaParams       g_loraParams;
radio::DutyCycleBudget  g_duty(DUTY_CYCLE_MAX, DUTY_WINDOW_MS);

void onSignal(int) {
    g_running = false;
    g_uplinks.wakeAll();
    g_fichajes.wakeAll();
}

void encolarUplink(std::string json) {
    if (!g_uplinks.push(std::move(json))) {
        g_droppedUplinks++;
    }
}

} // namespace

// ======================= Radio y protocolo ==============================

static PiHal* g_hal = nullptr;
static SX1276* g_radio = nullptr;
static LoRaLANGateway* g_gateway = nullptr;

// Fichajes esperando respuesta. Solo lo toca el hilo de radio.
struct FichajePendiente {
    uint8_t  node;
    uint16_t seq;
    uint32_t recibidoMs;
};
static std::vector<FichajePendiente> g_pendientes;

// --- Callbacks del protocolo: solo encolan, nunca hacen red -------------

static void onPushEvent(const LoRaLANFrame& frame, const uint8_t* payload, uint8_t payloadLen) {
    g_pushCount++;
    const uint8_t node = frame.header.srcId;
    const uint16_t seq = frame.header.seq;

    const uint8_t* uid = nullptr;
    uint8_t uidLen = 0;
    if (!app::decodeFichaje(payload, payloadLen, uid, uidLen)) {
        // No es un fichaje: se confirma ya (sin codigo) para que el nodo no
        // se quede esperando un ACK diferido que nadie va a enviar.
        g_gateway->ackPush(node, seq, nullptr, 0);
        Serial.print(F("PUSH desconocido del nodo "));
        Serial.println((unsigned long)node);
        return;
    }

    char hex[2 * app::kUidMax + 1];
    app::uidToHex(uid, uidLen, hex);
    const uint32_t now = millis();
    g_pendientes.push_back({node, seq, now});
    g_fichajes.push({node, seq, hex, now});
}

static void onPollResponse(const LoRaLANFrame& frame, const uint8_t* payload, uint8_t payloadLen) {
    if (payloadLen == 0) return;            // ventana vacia
    g_pollCount++;
    std::string json;
    if (bridge::uplinkJson(g_gwName, frame.header.srcId, payload, payloadLen,
                           g_radio->getRSSI(), g_radio->getSNR(), json)) {
        encolarUplink(std::move(json));
    } else {
        Serial.print(F("POLL_RESPONSE no reconocida del nodo "));
        Serial.println((unsigned long)frame.header.srcId);
    }
}

static void onNodeJoined(uint8_t nodeId) {
    encolarUplink(bridge::estadoJson(g_gwName, nodeId, true));
    uint8_t mac[LORALAN_HWID_SIZE] = {0};
    g_gateway->nodeHwId(nodeId, mac);
    char linea[64];
    snprintf(linea, sizeof(linea), "Nodo asociado: %u (MAC %02X:%02X:%02X:%02X:%02X:%02X)",
             nodeId, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.println(linea);
}

static void onNodeLeft(uint8_t nodeId) {
    encolarUplink(bridge::estadoJson(g_gwName, nodeId, false));
    Serial.print(F("Nodo dado de baja: "));
    Serial.println((unsigned long)nodeId);
}

// Envia los ACK de fichaje: respuestas del servidor y plazos vencidos.
static void atenderFichajes() {
    FichajeRes res;
    while (g_resultados.tryPop(res)) {
        for (size_t i = 0; i < g_pendientes.size(); ++i) {
            if (g_pendientes[i].node == res.node && g_pendientes[i].seq == res.seq) {
                g_pendientes.erase(g_pendientes.begin() + i);
                break;
            }
        }
        // false = ya se respondio REINTENTAR por plazo, o el nodo mando otro PUSH.
        if (!g_gateway->ackPush(res.node, res.seq, &res.code, 1)) {
            Serial.print(F("Respuesta de fichaje tardia descartada, nodo "));
            Serial.println((unsigned long)res.node);
        }
    }

    const uint32_t now = millis();
    const uint8_t reintentar = static_cast<uint8_t>(app::Code::REINTENTAR);
    for (size_t i = 0; i < g_pendientes.size();) {
        if (now - g_pendientes[i].recibidoMs >= FICHAJE_DEADLINE_MS) {
            g_gateway->ackPush(g_pendientes[i].node, g_pendientes[i].seq, &reintentar, 1);
            Serial.print(F("Fichaje sin respuesta a tiempo: REINTENTAR al nodo "));
            Serial.println((unsigned long)g_pendientes[i].node);
            g_pendientes.erase(g_pendientes.begin() + i);
        } else {
            ++i;
        }
    }
}

// Un CMD pendiente, sujeto al presupuesto de ciclo de trabajo.
static void atenderComandos() {
    static bridge::Command cmd;
    static bool haveCmd = false;
    if (!haveCmd) {
        haveCmd = g_comandos.tryPop(cmd);
        if (!haveCmd) return;
    }

    const double toa = radio::timeOnAirMs(
        (uint16_t)(LORALAN_HEADER_SIZE + cmd.len + LORALAN_MIC_SIZE), g_loraParams);
    const uint32_t now = millis();

    if (!g_duty.canTransmit(toa, now)) {
        // Se queda en espera: el limite regulatorio manda sobre la app.
        static uint32_t lastWarn = 0;
        if (now - lastWarn > 10000) {
            lastWarn = now;
            Serial.print(F("ciclo de trabajo agotado, CMD en espera "));
            Serial.println((unsigned long)g_duty.waitMs(toa, now));
        }
        return;
    }

    if (g_gateway->sendCommand(cmd.node, cmd.data, cmd.len)) {
        g_duty.record(toa, now);
        haveCmd = false;
    } else if (!g_gateway->isAlive(cmd.node, 0xFFFFFFFFUL)) {
        // Nodo no asociado: reintentarlo en cada vuelta no lo arregla.
        Serial.print(F("CMD descartado: nodo no asociado "));
        Serial.println((unsigned long)cmd.node);
        haveCmd = false;
    }
    // Si no: hay otro CMD esperando ACK; se reintenta en la siguiente vuelta.
}

// ======================= Cliente del servidor ===========================

// HttpClient autenticado. El JWT de /auth/device caduca (24 h), asi que no se
// configura un token fijo: se pide con la api_key del gateway al primer uso y
// se renueva cuando el servidor responde 401. Cada hilo tiene el suyo.
class ClienteServidor {
public:
    ClienteServidor(const std::string& host, uint16_t port, int timeoutMs, std::string apiKey)
        : http_(host, port, timeoutMs), apiKey_(std::move(apiKey)) {}

    net::Response post(const std::string& path, const std::string& body) {
        return conToken([&] { return http_.post(path, body); });
    }
    net::Response get(const std::string& path) {
        return conToken([&] { return http_.get(path); });
    }

private:
    template <typename F>
    net::Response conToken(F peticion) {
        if (!conToken_) renovar();
        net::Response r = peticion();
        if (r.ok && r.status == 401 && renovar()) {
            r = peticion();
        }
        return r;
    }

    bool renovar() {
        // Como mucho un intento cada 10 s: con la api_key mal no se inunda el
        // servidor (que ademas limita /auth/device a 10 por minuto).
        const uint32_t now = millis();
        if (apiKey_.empty() || (ultimoIntento_ != 0 && now - ultimoIntento_ < 10000UL)) return false;
        ultimoIntento_ = now;

        std::string token;
        if (!bridge::tokenDeRespuesta(http_.post(PATH_AUTH, bridge::authJson(g_gwName, apiKey_)), token)) {
            Serial.println(F("auth: no se pudo obtener token (revisa LORALAN_API_KEY y la tabla nodos)"));
            return false;
        }
        http_.setBearer(token);
        conToken_ = true;
        return true;
    }

    net::HttpClient http_;
    std::string     apiKey_;
    bool            conToken_ = false;
    uint32_t        ultimoIntento_ = 0;
};

// ======================= Hilo de fichajes ===============================

static void fichajeThread(std::string host, uint16_t port, std::string apiKey) {
    ClienteServidor http(host, port, FICHAJE_HTTP_TIMEOUT_MS, apiKey);

    FichajeReq req;
    while (g_running) {
        if (!g_fichajes.waitPop(req, 250, g_running)) continue;

        // Si ya no cabe en el plazo, la radio habra respondido REINTENTAR:
        // enviarlo ahora solo crearia un registro que el nodo va a repetir.
        if (millis() - req.recibidoMs >= FICHAJE_DEADLINE_MS) continue;

        const net::Response r = http.post(PATH_FICHAJE, bridge::fichajeJson(g_gwName, req.node, req.uid));
        const uint8_t code = bridge::codigoFichaje(r);
        if (!r.ok) {
            Serial.print(F("fichaje: fallo de red - "));
            Serial.println(r.error.c_str());
        }
        g_resultados.push({req.node, req.seq, code});
    }
}

// ======================= Hilo de red ====================================

static void networkThread(std::string host, uint16_t port, std::string apiKey) {
    ClienteServidor http(host, port, HTTP_TIMEOUT_MS, apiKey);

    uint32_t lastCommandPoll = 0;
    uint32_t lastStatus = 0;

    while (g_running) {
        // --- 1. Vaciar la cola de subida ---
        std::string json;
        if (g_uplinks.waitPop(json, 250, g_running)) {
            const net::Response r = http.post(PATH_UPLINK, json);
            if (!r.ok) {
                Serial.print(F("uplink: fallo de red - "));
                Serial.println(r.error.c_str());
            } else if (r.status != 200) {
                Serial.print(F("uplink: HTTP "));
                Serial.println((long)r.status);
            }
        }

        const uint32_t now = millis();

        // --- 2. Consultar comandos pendientes ---
        if (now - lastCommandPoll >= COMMAND_POLL_MS) {
            lastCommandPoll = now;
            const net::Response r = http.get(std::string(PATH_COMMANDS) + "?gateway=" + g_gwName);
            if (r.ok && r.status == 200) {
                unsigned malos = 0;
                for (const auto& c : bridge::parseCommands(r.body, &malos)) {
                    g_comandos.push(c);
                }
                if (malos > 0) {
                    Serial.print(F("comandos mal formados descartados: "));
                    Serial.println((unsigned long)malos);
                }
            }
        }

        // --- 3. Reportar estado ---
        if (now - lastStatus >= STATUS_REPORT_MS) {
            lastStatus = now;
            char body[320];
            snprintf(body, sizeof(body),
                     "{\"gateway\":\"%s\",\"nodes\":%u,\"push\":%u,\"poll\":%u,"
                     "\"dropped\":%u,\"duty_pct\":%.4f}",
                     bridge::jsonEscape(g_gwName).c_str(),
                     g_nodeCount.load(), g_pushCount.load(), g_pollCount.load(),
                     g_droppedUplinks.load(),
                     g_duty.usedFraction(now) * 100.0);
            http.post(PATH_STATUS, body);
        }
    }
}

// ======================= Programa principal =============================

int main() {
    ::signal(SIGINT, onSignal);
    ::signal(SIGTERM, onSignal);

    const std::string host  = envStr("LORALAN_SERVER_HOST", SERVER_HOST);
    const uint16_t    port  = (uint16_t)envLong("LORALAN_SERVER_PORT", SERVER_PORT);
    const std::string apiKey = envStr("LORALAN_API_KEY", "");
    g_gwName = envStr("LORALAN_GATEWAY_NAME", "gw-rpi-01");

    printf("gatewayLoRaLAN\n");
    printf("  servidor : %s:%u\n", host.c_str(), (unsigned)port);
    printf("  gateway  : %s\n", g_gwName.c_str());
    if (apiKey.empty()) {
        printf("AVISO: sin LORALAN_API_KEY el servidor rechazara todas las peticiones (401).\n");
    }

    // --- Clave de aplicacion ---
    // Debe coincidir byte a byte con la de los nodos. En despliegue conviene
    // leerla de fichero con permisos restringidos en vez de compilarla.
    static const uint8_t APP_KEY[16] = {
        0x42, 0x19, 0xA7, 0x5C, 0xD3, 0x88, 0x11, 0xE4,
        0x2F, 0x90, 0x6A, 0xC1, 0x7B, 0x54, 0x0D, 0xBE
    };

    // --- Radio ---
    g_hal = new PiHal(LORA_SPI_CHANNEL, LORA_SPI_SPEED);
    g_radio = new SX1276(new Module(g_hal, LORA_PIN_CS, LORA_PIN_DIO0, LORA_PIN_RST, RADIOLIB_NC));

    printf("Inicializando SX1276... ");
    fflush(stdout);
    int state = g_radio->begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR,
                               LORA_SYNC_WORD, LORA_TX_POWER_DBM, LORA_PREAMBLE_LEN);
    if (state != RADIOLIB_ERR_NONE) {
        printf("ERROR %d\n", state);
        printf("Comprueba el cableado SPI y que /dev/spidev0.0 existe (raspi-config).\n");
        return 1;
    }
    printf("OK\n");

    g_loraParams.spreadingFactor = LORA_SF;
    g_loraParams.bandwidthHz     = (uint32_t)(LORA_BW_KHZ * 1000);
    g_loraParams.codingRate      = LORA_CR;
    g_loraParams.preambleSymbols = LORA_PREAMBLE_LEN;

    const double toaMin = radio::timeOnAirMs(11, g_loraParams);
    printf("  ToA trama minima (0 B) : %.1f ms\n", toaMin);
    printf("  ToA trama maxima (64 B): %.1f ms\n", radio::timeOnAirMs(75, g_loraParams));
    printf("  presupuesto duty cycle : %.0f ms/hora (%.1f%%)\n",
           DUTY_CYCLE_MAX * DUTY_WINDOW_MS, DUTY_CYCLE_MAX * 100.0);

    // --- Comprobacion regulatoria del intervalo de sondeo ---
    // El POLL periodico es, con diferencia, el mayor consumidor de tiempo de
    // aire del gateway. Se verifica ANTES de arrancar que el intervalo
    // configurado cabe en el presupuesto: un gateway que incumpla el ciclo de
    // trabajo debe fallar de forma visible, no emitir de mas en silencio.
    // ponytail: los ACK (tambien los de fichaje) no se descuentan del
    // presupuesto; contarlos en g_duty si el trafico de fichajes crece.
    const uint32_t pollMs = (uint32_t)envLong("LORALAN_POLL_MS", POLL_INTERVAL_MS);
    if (pollMs > 0) {
        const double pollsPorHora = 3600000.0 / (double)pollMs;
        const double consumo = (toaMin * pollsPorHora) / DUTY_WINDOW_MS;
        const double minLegalS = (toaMin / (DUTY_CYCLE_MAX * 1000.0));
        printf("  sondeo cada %.1f s     : %.2f%% de ciclo de trabajo",
               pollMs / 1000.0, consumo * 100.0);

        if (consumo > DUTY_CYCLE_MAX) {
            printf("  <-- EXCEDE EL LIMITE\n");
            printf("\nERROR: con SF%d el sondeo debe espaciarse al menos %.1f s.\n",
                   LORA_SF, minLegalS);
            printf("Ajusta LORALAN_POLL_MS a %.0f o mas, o baja el SF.\n",
                   minLegalS * 1000.0);
            return 1;
        }
        printf("  (minimo legal %.1f s)\n", minLegalS);
    }

    // --- Protocolo ---
    g_gateway = new LoRaLANGateway(*g_radio);

    LoRaLANGateway::Config cfg;
    cfg.netId              = LORALAN_NET_ID;
    cfg.gatewayId          = LORALAN_GATEWAY_ID;
    cfg.appKey             = APP_KEY;
    cfg.pollIntervalMs     = pollMs;
    cfg.timeSyncIntervalMs = (uint32_t)envLong("LORALAN_TIMESYNC_MS", TIME_SYNC_INTERVAL_MS);
    cfg.deferredPushAck    = true;   // el resultado del fichaje va en el ACK

    g_gateway->begin(cfg);
    g_gateway->setOnPushEvent(onPushEvent);
    g_gateway->setOnPollResponse(onPollResponse);
    g_gateway->setOnNodeJoined(onNodeJoined);
    g_gateway->setOnNodeLeft(onNodeLeft);

    g_radio->startReceive();

    // --- Hilos de red ---
    std::thread fichajes(fichajeThread, host, port, apiKey);
    std::thread net(networkThread, host, port, apiKey);

    printf("Gateway en marcha. Ctrl+C para parar.\n\n");

    // --- Bucle de radio: nunca hace E/S de red ---
    uint8_t buffer[LORALAN_HEADER_SIZE + LORALAN_MAX_PAYLOAD + LORALAN_MIC_SIZE];

    while (g_running) {
        g_gateway->process();

        // Recepcion. Primero la IRQ: en el SX127x el registro de longitud
        // conserva la del ultimo paquete, y leerlo sin mirar RX_DONE volveria
        // a entregar la misma trama en cada vuelta (con ACK diferido, el
        // gateway la tomaria por retransmision y reenviaria el ACK sin parar).
        if (g_radio->checkIrq(RADIOLIB_IRQ_RX_DONE) == 1) {
            const size_t len = g_radio->getPacketLength();
            if (len > 0 && len <= sizeof(buffer) &&
                g_radio->readData(buffer, len) == RADIOLIB_ERR_NONE) {   // readData limpia la IRQ
                g_gateway->handleReceived(buffer, len);
            }
            g_radio->startReceive();   // imprescindible tras cada operacion
        }

        atenderFichajes();
        atenderComandos();
        g_nodeCount = g_gateway->nodeCount();

        // 1 ms: suficiente para los temporizadores del protocolo (el mas corto
        // es el backoff de CAD, 40 ms) sin consumir un nucleo entero.
        delay(1);
    }

    printf("\nParando...\n");
    g_uplinks.wakeAll();
    g_fichajes.wakeAll();
    fichajes.join();
    net.join();

    delete g_gateway;
    delete g_radio;
    delete g_hal;
    printf("Gateway detenido.\n");
    return 0;
}
