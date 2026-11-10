/*********
 Nodo de aula SIMULADO para LilyGo T3 V1.6 (SX1276), sobre LoRaLAN.

 Habla con el gateway exactamente igual que Device_ESP32_LoRaLAN (mismos
 mensajes de AppProtocol.h), pero sin hardware: los sensores dan valores
 inventados y los fichajes salen de:
   - el monitor serie: escribe un UID en hex (p. ej. 04A1B2C3) y Enter
   - el boton BOOT (GPIO0): ficha el siguiente UID de UIDS_SIMULADOS
   - automatico cada AUTO_FICHAJE_MS (0 = desactivado)
 Los reles se simulan: el rele 1 enciende el LED de la placa (GPIO25).

 AppProtocol.h es una copia de Device_ESP32_LoRaLAN/AppProtocol.h: si cambia
 el protocolo, copiarlo de nuevo.
*********/

#include <RadioLib.h>
#include <LoRaLAN.h>
#include "AppProtocol.h"

// ---------- Identidad: distinta de cualquier otro nodo de la red ----------
#define LORALAN_NODE_ID     3        // direccion pedida; el gateway la concede si esta libre
#define LORALAN_NET_ID      0x42
#define LORALAN_GATEWAY_ID  0x00

// ---------- Radio: igual que el gateway ----------
#define LORA_FREQ_MHZ       869.525
#define LORA_BW_KHZ         125.0
#define LORA_SF             7
#define LORA_CR             5
#define LORA_SYNC_WORD      0x12
#define LORA_TX_POWER_DBM   14
#define LORA_PREAMBLE_LEN   8

static const uint8_t LORALAN_APP_KEY[16] = {
  0x42, 0x19, 0xA7, 0x5C, 0xD3, 0x88, 0x11, 0xE4,
  0x2F, 0x90, 0x6A, 0xC1, 0x7B, 0x54, 0x0D, 0xBE
};

// ---------- Pines LilyGo T3 V1.6 ----------
#define LORA_SCK    5
#define LORA_MISO   19
#define LORA_MOSI   27
#define LORA_CS     18
#define LORA_RST    14
#define LORA_DIO0   26
#define LED_PIN     25
#define BOTON_PIN   0

// ---------- Simulacion ----------
// Pon aqui UIDs que existan en usuarios.uid_tarjeta para ver fichajes OK.
static const char* UIDS_SIMULADOS[] = { "04A1B2C3", "DEADBEEF", "11223344" };
#define AUTO_FICHAJE_MS       120000UL   // 0 = solo serie / boton
#define FICHAJE_REINTENTO_MS   30000UL

SPIClass spiLoRa(VSPI);
SX1276 radio = new Module(LORA_CS, LORA_DIO0, LORA_RST, RADIOLIB_NC, spiLoRa);
LoRaLANNode nodo(radio);

static bool rele1 = false, rele2 = false;
static bool relesCambiados = true;   // informar del estado inicial
static uint8_t pollsDesdeReles = 0;
static uint32_t reiniciarEnMs = 0;

// ponytail: un solo fichaje pendiente; el nodo real guarda una cola de 8.
static struct {
  bool activo = false;
  bool enVuelo = false;
  uint8_t uid[app::kUidMax];
  uint8_t len = 0;
  uint32_t proximoMs = 0;
  uint32_t enVueloDesde = 0;
} fichaje;

// ============================================
// Sensores simulados
// ============================================
static app::Sensores simularSensores() {
  const float t = millis() / 60000.0f;   // minutos
  app::Sensores s;
  s.temperatura = 22.0f + 1.5f * sinf(t / 10.0f) + random(-20, 21) / 100.0f;
  s.humedad = 45.0f + 5.0f * sinf(t / 15.0f) + random(-50, 51) / 100.0f;
  s.presion = 1013.0f + random(-20, 21) / 10.0f;
  s.lux = 300.0f + 200.0f * sinf(t / 5.0f) + random(0, 30);
  s.presencia = random(0, 100) < 70;
  return s;
}

// ============================================
// Fichajes simulados
// ============================================
static bool hexAUid(const String& hex, uint8_t* uid, uint8_t& len) {
  if (hex.length() % 2 || hex.length() / 2 < app::kUidMin || hex.length() / 2 > app::kUidMax) return false;
  len = hex.length() / 2;
  for (uint8_t i = 0; i < len; i++) {
    char* fin;
    const String byte = hex.substring(2 * i, 2 * i + 2);
    uid[i] = strtoul(byte.c_str(), &fin, 16);
    if (*fin != '\0') return false;
  }
  return true;
}

static void fichar(const String& hex) {
  if (fichaje.activo) {
    Serial.println("Hay un fichaje pendiente, espera a que termine");
    return;
  }
  if (!hexAUid(hex, fichaje.uid, fichaje.len)) {
    Serial.println("UID no valido: 8..20 caracteres hex, p. ej. 04A1B2C3");
    return;
  }
  fichaje.activo = true;
  fichaje.proximoMs = 0;
  Serial.println("Fichaje encolado: " + hex);
}

static void atenderEntradas() {
  static uint8_t siguiente = 0;
  const uint8_t numUids = sizeof(UIDS_SIMULADOS) / sizeof(UIDS_SIMULADOS[0]);

  if (Serial.available()) {
    String linea = Serial.readStringUntil('\n');
    linea.trim();
    linea.toUpperCase();
    if (linea.length()) fichar(linea);
  }

  static bool botonAntes = HIGH;
  const bool boton = digitalRead(BOTON_PIN);
  if (botonAntes == HIGH && boton == LOW) fichar(UIDS_SIMULADOS[siguiente++ % numUids]);
  botonAntes = boton;

  static uint32_t ultimoAuto = 0;
  if (AUTO_FICHAJE_MS && nodo.isJoined() && millis() - ultimoAuto > AUTO_FICHAJE_MS) {
    ultimoAuto = millis();
    fichar(UIDS_SIMULADOS[siguiente++ % numUids]);
  }
}

static void enviarFichaje() {
  // Si se pierde la asociacion con el PUSH en vuelo, LoRaLAN lo descarta sin avisar.
  if (fichaje.enVuelo && (!nodo.isJoined() || millis() - fichaje.enVueloDesde > 20000UL)) {
    fichaje.enVuelo = false;
  }
  if (!fichaje.activo || fichaje.enVuelo || !nodo.isJoined()) return;
  if (fichaje.proximoMs && (int32_t)(millis() - fichaje.proximoMs) < 0) return;

  uint8_t payload[app::kLenFichajeMax];
  const uint8_t len = app::encodeFichaje(payload, fichaje.uid, fichaje.len);
  if (nodo.queuePushEvent(payload, len, true)) {
    fichaje.enVuelo = true;
    fichaje.enVueloDesde = millis();
  }
}

// ============================================
// Callbacks LoRaLAN
// ============================================
static void onPushAck(bool entregado, const uint8_t* ack, uint8_t ackLen) {
  fichaje.enVuelo = false;
  const uint8_t code = (entregado && ackLen >= 1) ? ack[0] : uint8_t(app::Code::REINTENTAR);
  static const char* texto[] = { "OK: asistencia registrada", "SIN_CLASE: no hay clase ahora en el aula",
                                 "DENEGADO: la clase es de otro profesor", "DESCONOCIDO: tarjeta o aula no registrada" };
  if (app::esDefinitivo(code)) {
    Serial.printf("[Fichaje] %s\n", texto[code]);
    fichaje.activo = false;
  } else {
    Serial.printf("[Fichaje] %s (codigo %u): se reintenta en %lu s\n",
                  entregado ? "REINTENTAR" : "sin ACK", code, FICHAJE_REINTENTO_MS / 1000);
    fichaje.proximoMs = millis() + FICHAJE_REINTENTO_MS;
  }
}

static void onComando(const LoRaLANFrame&, const uint8_t* payload, uint8_t len) {
  uint8_t numero = 0;
  bool on = false;
  if (app::decodeRele(payload, len, numero, on)) {
    Serial.printf("[CMD] rele%u = %s\n", numero, on ? "ON" : "OFF");
    (numero == 1 ? rele1 : rele2) = on;
    digitalWrite(LED_PIN, rele1);
    relesCambiados = true;
  } else if (app::tipo(payload, len) == uint8_t(app::Msg::RESET)) {
    Serial.println("[CMD] reset: reiniciando en 1 s");
    reiniciarEnMs = millis() + 1000UL;   // despues del ACK del CMD
  } else {
    Serial.println("[CMD] desconocido, ignorado");
  }
}

// Igual que el nodo real: estado de reles si cambio (o cada 6 sondeos), si no sensores.
static bool onPollRequest(uint8_t* payload, uint8_t& len) {
  if (relesCambiados || ++pollsDesdeReles >= 6) {
    relesCambiados = false;
    pollsDesdeReles = 0;
    len = app::encodeEstadoReles(payload, rele1, rele2);
    Serial.printf("[POLL] reles: %d %d\n", rele1, rele2);
    return true;
  }
  const app::Sensores s = simularSensores();
  len = app::encodeSensores(payload, s);
  Serial.printf("[POLL] %.2f C  %.1f %%  %.1f hPa  %.0f lux  presencia %d\n",
                s.temperatura, s.humedad, s.presion, s.lux, s.presencia);
  return true;
}

static void onAsociado(const LoRaLANFrame&, const uint8_t*, uint8_t) {
  Serial.printf("[LoRa] Asociado al gateway como node-%03u\n", nodo.getConfig().nodeId);
  relesCambiados = true;
}

// ============================================
void setup() {
  Serial.begin(115200);
  Serial.setTimeout(50);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BOTON_PIN, INPUT_PULLUP);
  randomSeed(esp_random());
  delay(1000);
  Serial.printf("\nNodo SIMULADO LoRaLAN, pide node-%03d\n", LORALAN_NODE_ID);

  spiLoRa.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  int state = radio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR,
                          LORA_SYNC_WORD, LORA_TX_POWER_DBM, LORA_PREAMBLE_LEN);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("Error SX1276: %d\n", state);
    for (;;) delay(1000);
  }

  LoRaLANNode::Config cfg;
  cfg.netId = LORALAN_NET_ID;
  cfg.nodeId = LORALAN_NODE_ID;
  // ponytail: sin NVS; tras reiniciarse pide de nuevo LORALAN_NODE_ID, que el
  // gateway le devuelve mientras siga en marcha (la reconoce por la MAC).
  const uint64_t mac = ESP.getEfuseMac();
  for (int i = 0; i < LORALAN_HWID_SIZE; ++i) {
    cfg.hwId[i] = uint8_t(mac >> (8 * i));
  }
  cfg.gatewayId = LORALAN_GATEWAY_ID;
  cfg.appKey = LORALAN_APP_KEY;
  nodo.begin(cfg);
  nodo.setOnJoinAccepted(onAsociado);
  nodo.setOnCommand(onComando);
  nodo.setOnPollRequest(onPollRequest);
  nodo.setOnPushAck(onPushAck);

  radio.startReceive();
  Serial.println("Escribe un UID en hex y Enter para fichar, o pulsa BOOT.");
}

void loop() {
  static uint8_t buffer[LORALAN_HEADER_SIZE + LORALAN_MAX_PAYLOAD + LORALAN_MIC_SIZE];

  // Sondeo de RX_DONE por SPI, sin ISR en DIO0 (el CAD tambien usa DIO0).
  if (radio.checkIrq(RADIOLIB_IRQ_RX_DONE) == 1) {
    const size_t len = radio.getPacketLength();
    if (len > 0 && len <= sizeof(buffer) && radio.readData(buffer, len) == RADIOLIB_ERR_NONE) {
      nodo.handleReceived(buffer, len);
    }
    radio.startReceive();
  }

  nodo.process();
  atenderEntradas();
  enviarFichaje();

  if (reiniciarEnMs && millis() > reiniciarEnMs) ESP.restart();
  delay(2);
}
