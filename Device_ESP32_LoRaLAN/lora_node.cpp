/*********
 * lora_node.cpp: comunicación del nodo por LoRaLAN (sustituye a WiFi + HTTPS + WebSocket + JWT).
 *
 * Todo lo de radio vive en tareaRadio(), fijada al core 0. loop() (core 1)
 * lee la tarjeta y los sensores y solo intercambia datos con la radio por dos
 * colas FreeRTOS y una lectura de sensores protegida:
 *
 *   loop() --encolarFichaje(uid)------> [colaFichajes]  --> radio: PUSH (FICHAJE)
 *   loop() <--recogerResultadoFichaje-- [colaResultados] <-- radio: ACK con código
 *   loop() --publicarLecturaSensores--> ultimaLectura    --> radio: respuesta al POLL
 *
 * Así la radio nunca espera a un sensor I2C, y los LEDs, el buzzer y la
 * pantalla (con sus delay) nunca retrasan un ACK.
 *
 * Fichaje: el gateway contesta dentro del ACK del PUSH con un código de
 * AppProtocol.h. 0 = ACK (registrado); 1..3 = NACK definitivo (no se
 * reintenta); REINTENTAR o sin ACK = NACK temporal: el fichaje se guarda en RAM
 * y se reenvía cada FICHAJE_REINTENTO_MS. El servidor no lo duplica.
 *********/

#include <Arduino.h>
#include <RadioLib.h>
#include <LoRaLAN.h>
#include "AppProtocol.h"
#include "globals.h"
#include "config.h"

// El SX1276 comparte el bus SPI con el PN532 (cada uno con su CS). El core
// de ESP32 serializa las transacciones SPI entre tareas (SPIClass::beginTransaction
// toma un mutex), así que la radio (core 0) y el lector (core 1) no se pisan.
static SX1276 radioLoRa = new Module(LORA_CS, LORA_DIO0, LORA_RST, RADIOLIB_NC);
static LoRaLANNode nodo(radioLoRa);

struct FichajePendiente {
  uint8_t uid[app::kUidMax];
  uint8_t uidLen;
  uint32_t proximoIntentoMs;   // 0 = en cuanto se pueda
  bool avisado;                // ya se mostró REINTENTAR para este fichaje
};

struct ResultadoMsg {
  ResultadoFichaje resultado;
  char uidHex[2 * app::kUidMax + 1];
};

static QueueHandle_t colaFichajes = nullptr;     // loop -> radio
static QueueHandle_t colaResultados = nullptr;   // radio -> loop

// Pendientes de enviar o reenviar. Solo los toca la tarea de radio.
static FichajePendiente pendientes[FICHAJES_EN_COLA_MAX];
static uint8_t numPendientes = 0;
static bool fichajeEnVuelo = false;
static uint32_t enVueloDesdeMs = 0;

// Última lectura de sensores (la escribe loop, la lee la radio en el POLL).
static portMUX_TYPE muxLectura = portMUX_INITIALIZER_UNLOCKED;
static app::Sensores ultimaLectura;
static volatile bool relesCambiados = true;      // informar del estado inicial
static uint8_t pollsDesdeReles = 0;
static uint32_t reiniciarEnMs = 0;               // != 0: reinicio pedido por CMD

// ============================================
// API para loop()
// ============================================

bool encolarFichaje(const uint8_t* uid, uint8_t uidLen) {
  if (colaFichajes == nullptr || uidLen < app::kUidMin || uidLen > app::kUidMax) return false;
  FichajePendiente f = {};
  memcpy(f.uid, uid, uidLen);
  f.uidLen = uidLen;
  return xQueueSend(colaFichajes, &f, 0) == pdTRUE;
}

bool recogerResultadoFichaje(ResultadoFichaje& resultado, String& uid) {
  ResultadoMsg msg;
  if (colaResultados == nullptr || xQueueReceive(colaResultados, &msg, 0) != pdTRUE) return false;
  resultado = msg.resultado;
  uid = msg.uidHex;
  return true;
}

void publicarLecturaSensores(const BME280Data& bme, const LuzData& luz, const RadarData& radar) {
  app::Sensores s;   // NAN = sin lectura
  if (bme.valido) {
    s.temperatura = bme.temperatura;
    s.humedad = bme.humedad;
    s.presion = bme.presion;
  }
  if (luz.valido) s.lux = luz.lux;
  s.presencia = radar.valido && radar.presenciaDetectada;

  portENTER_CRITICAL(&muxLectura);
  ultimaLectura = s;
  portEXIT_CRITICAL(&muxLectura);
}

bool loraAsociado() {
  return nodo.isJoined();
}

// ============================================
// Relés (los ordena el gateway con un CMD)
// ============================================

static void aplicarRele(uint8_t numero, bool on) {
  if (numero == 1) activarRele1(on);
  else if (numero == 2) activarRele2(on);
  relesCambiados = true;   // la siguiente respuesta al POLL confirma el estado
}

// ============================================
// Callbacks de LoRaLAN (se ejecutan en la tarea de radio)
// ============================================

static void enviarResultado(ResultadoFichaje r, const FichajePendiente& f) {
  ResultadoMsg msg;
  msg.resultado = r;
  app::uidToHex(f.uid, f.uidLen, msg.uidHex);
  xQueueSend(colaResultados, &msg, 0);   // si loop va atrasado, se pierde solo el aviso visual
}

static void onPushAck(bool entregado, const uint8_t* ack, uint8_t ackLen) {
  fichajeEnVuelo = false;
  if (numPendientes == 0) return;
  FichajePendiente& f = pendientes[0];

  const uint8_t code = (entregado && ackLen >= 1) ? ack[0] : static_cast<uint8_t>(app::Code::REINTENTAR);
  Serial.printf("[LoRa] Fichaje: %s, codigo %u\n", entregado ? "ACK recibido" : "sin ACK", code);

  if (!app::esDefinitivo(code)) {
    // NACK temporal: se reintenta más tarde, detrás de los demás pendientes.
    if (!f.avisado) enviarResultado(ResultadoFichaje::REINTENTAR, f);
    FichajePendiente copia = f;
    copia.avisado = true;
    copia.proximoIntentoMs = millis() + FICHAJE_REINTENTO_MS;
    memmove(&pendientes[0], &pendientes[1], (numPendientes - 1) * sizeof(FichajePendiente));
    pendientes[numPendientes - 1] = copia;
    return;
  }

  static const ResultadoFichaje porCodigo[] = {
    ResultadoFichaje::OK, ResultadoFichaje::SIN_CLASE, ResultadoFichaje::DENEGADO, ResultadoFichaje::DESCONOCIDO
  };
  enviarResultado(porCodigo[code], f);
  numPendientes--;
  memmove(&pendientes[0], &pendientes[1], numPendientes * sizeof(FichajePendiente));
}

static void onComando(const LoRaLANFrame&, const uint8_t* payload, uint8_t len) {
  uint8_t numero = 0;
  bool on = false;
  if (app::decodeRele(payload, len, numero, on)) {
    Serial.printf("[LoRa] CMD rele%u = %s\n", numero, on ? "ON" : "OFF");
    aplicarRele(numero, on);
  } else if (app::tipo(payload, len) == static_cast<uint8_t>(app::Msg::RESET)) {
    // No se reinicia aqui: LoRaLAN envia el ACK del CMD despues de este
    // callback. tareaRadio() reinicia pasado un momento.
    Serial.println("[LoRa] CMD reset: reiniciando en 1 s");
    reiniciarEnMs = millis() + 1000UL;
  } else {
    Serial.println("[LoRa] CMD desconocido, ignorado");
  }
}

// Respuesta a cada POLL del gateway: el estado de los relés si cambió (o cada
// 6 sondeos, para que el servidor lo recupere tras reiniciarse) y si no, sensores.
static bool onPollRequest(uint8_t* payload, uint8_t& len) {
  if (relesCambiados || ++pollsDesdeReles >= 6) {
    relesCambiados = false;
    pollsDesdeReles = 0;
    len = app::encodeEstadoReles(payload, relay1Estado, relay2Estado);
    return true;
  }
  app::Sensores s;
  portENTER_CRITICAL(&muxLectura);
  s = ultimaLectura;
  portEXIT_CRITICAL(&muxLectura);
  len = app::encodeSensores(payload, s);
  return true;
}

static void onAsociado(const LoRaLANFrame&, const uint8_t*, uint8_t) {
  Serial.println("[LoRa] Asociado al gateway");
  relesCambiados = true;
}

// ============================================
// Inicialización y bucle
// ============================================

bool setupLoRa() {
  colaFichajes = xQueueCreate(4, sizeof(FichajePendiente));
  colaResultados = xQueueCreate(4, sizeof(ResultadoMsg));

  // El PN532 ya inició el bus (SPI.begin en setupNFC); la radio usa el mismo.
  Serial.print("[LoRa] Inicializando SX1276... ");
  int state = radioLoRa.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR,
                              LORA_SYNC_WORD, LORA_TX_POWER_DBM, LORA_PREAMBLE_LEN);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("ERROR %d (revisa CS=%d, RST=%d, DIO0=%d)\n", state, LORA_CS, LORA_RST, LORA_DIO0);
    return false;
  }
  Serial.println("OK");

  LoRaLANNode::Config cfg;
  cfg.netId = LORALAN_NET_ID;
  cfg.nodeId = LORALAN_NODE_ID;
  cfg.gatewayId = LORALAN_GATEWAY_ID;
  cfg.nodeType = LORALAN_NODE_TYPE;
  cfg.appKey = LORALAN_APP_KEY;
  nodo.begin(cfg);
  nodo.setOnJoinAccepted(onAsociado);
  nodo.setOnCommand(onComando);
  nodo.setOnPollRequest(onPollRequest);
  nodo.setOnPushAck(onPushAck);

  radioLoRa.startReceive();
  return true;
}

static void atenderFichajes() {
  // Nuevos desde loop()
  FichajePendiente f;
  while (xQueueReceive(colaFichajes, &f, 0) == pdTRUE) {
    if (numPendientes < FICHAJES_EN_COLA_MAX) {
      pendientes[numPendientes++] = f;
    } else {
      Serial.println("[LoRa] Cola de fichajes llena: se descarta el nuevo");
      enviarResultado(ResultadoFichaje::REINTENTAR, f);
    }
  }

  // Red de seguridad: si el nodo pierde la asociación con un PUSH en vuelo,
  // LoRaLAN descarta ese PUSH sin avisar; el fichaje sigue en pendientes[0].
  if (fichajeEnVuelo && (!nodo.isJoined() || millis() - enVueloDesdeMs > 20000UL)) {
    fichajeEnVuelo = false;
  }

  // Uno cada vez: el ACK del PUSH en vuelo corresponde siempre a pendientes[0].
  if (fichajeEnVuelo || numPendientes == 0 || !nodo.isJoined()) return;
  if (pendientes[0].proximoIntentoMs != 0 && millis() < pendientes[0].proximoIntentoMs) return;

  uint8_t payload[app::kLenFichajeMax];
  const uint8_t len = app::encodeFichaje(payload, pendientes[0].uid, pendientes[0].uidLen);
  if (nodo.queuePushEvent(payload, len, true)) {
    fichajeEnVuelo = true;
    enVueloDesdeMs = millis();
  }
}

void tareaRadio(void* pvParameters) {
  uint8_t buffer[LORALAN_HEADER_SIZE + LORALAN_MAX_PAYLOAD + LORALAN_MIC_SIZE];

  for (;;) {
    // Recepción por sondeo del flag RX_DONE (por SPI), sin ISR en DIO0: el CAD
    // también usa DIO0 y una ISR de "paquete recibido" saltaría en falso.
    if (radioLoRa.checkIrq(RADIOLIB_IRQ_RX_DONE) == 1) {
      const size_t len = radioLoRa.getPacketLength();
      if (len > 0 && len <= sizeof(buffer) && radioLoRa.readData(buffer, len) == RADIOLIB_ERR_NONE) {
        nodo.handleReceived(buffer, len);
      }
      radioLoRa.startReceive();
    }

    nodo.process();
    atenderFichajes();

    if (reiniciarEnMs != 0 && millis() > reiniciarEnMs) {
      ESP.restart();
    }

    vTaskDelay(2 / portTICK_PERIOD_MS);
  }
}
