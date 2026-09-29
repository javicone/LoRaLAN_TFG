/*********
 Monitor pasivo de la red LoRaLAN para M5Stack Cardputer + Cap LoRa-1262 (SX1262).

 Solo escucha: nunca transmite, asi que no altera la red ni gasta ciclo de trabajo.

 Sin claves ve la cabecera de todas las tramas (tipo, origen, destino, seq,
 reintentos), RSSI/SNR y el codigo de resultado de cada fichaje (el ACK va en
 claro). Con la APP_KEY, si oye el JOIN de un nodo deriva su clave de sesion
 (CMAC(APP_KEY, "LLK"|netId|nodo|DevNonce), LoRaLANCrypto.h) y ademas descifra
 sensores, UIDs y ordenes de reles, y verifica el MIC de cada trama.
 Si el monitor arranca despues del JOIN, reinicia el nodo (CMD reset desde la web).

 Teclas: 1 resumen, 2 nodos, 3 log, ; y . para desplazar.

 AppProtocol.h es una copia de Device_ESP32_LoRaLAN/AppProtocol.h.
*********/

#include <M5Cardputer.h>
#include <RadioLib.h>
#include <LoRaLAN.h>
#include <LoRaLANCrypto.h>
#include "AppProtocol.h"

// ---------- Red: igual que el gateway ----------
#define LORALAN_NET_ID      0x42
#define LORALAN_GATEWAY_ID  0x00
#define LORA_FREQ_MHZ       869.525
#define LORA_BW_KHZ         125.0
#define LORA_SF             7
#define LORA_CR             5
#define LORA_SYNC_WORD      0x12   // RadioLib lo pasa al 0x1424 del SX126x
#define LORA_PREAMBLE_LEN   8

// ponytail: APP_KEY compilada; sin ella (todo a 0) solo se ven cabeceras y ACK.
static const uint8_t LORALAN_APP_KEY[16] = {
  0x42, 0x19, 0xA7, 0x5C, 0xD3, 0x88, 0x11, 0xE4,
  0x2F, 0x90, 0x6A, 0xC1, 0x7B, 0x54, 0x0D, 0xBE
};

// ---------- Cap LoRa-1262 (variante Meshtastic m5stack_cardputer_adv) ----------
#define LORA_SCK    40
#define LORA_MISO   39
#define LORA_MOSI   14
#define LORA_CS     5
#define LORA_RST    3
#define LORA_DIO1   4
#define LORA_BUSY   6
#define LORA_TCXO_V 1.8

// ---------- Alertas ----------
#define POLLS_SIN_RESPUESTA_MAX  3
#define DUTY_LIMITE_PCT          1.0f

SPIClass spiLoRa(HSPI);
SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY, spiLoRa);
M5Canvas lienzo(&M5Cardputer.Display);

// ============================================
// Estado
// ============================================

// Tiempo en el aire por minuto de la ultima hora (ventana movil para el ciclo de trabajo).
struct Ventana {
  uint32_t minuto = 0;
  uint16_t ms[60] = {0};

  void avanzar() {
    const uint32_t ahora = millis() / 60000UL;
    for (uint32_t m = minuto + 1; m <= ahora && m <= minuto + 60; m++) ms[m % 60] = 0;
    minuto = ahora;
  }
  void sumar(uint32_t aireMs) {
    avanzar();
    ms[minuto % 60] = min<uint32_t>(ms[minuto % 60] + aireMs, 60000UL);
  }
  float dutyPct() {
    avanzar();
    uint32_t total = 0;
    for (uint16_t v : ms) total += v;
    return total * 100.0f / 3600000.0f;
  }
};

struct Nodo {
  uint32_t vistoMs = 0;          // 0 = nunca
  float rssi = 0, snr = 0, rssiMedia = 0;
  uint32_t tramas = 0, reintentos = 0, perdidas = 0, micMal = 0;
  bool haySeq = false;
  uint8_t ultSeq = 0;
  uint8_t pollsSinResp = 0;
  bool asociado = false;
  bool tieneClave = false;
  uint8_t clave[16];
  bool haySensores = false;
  app::Sensores sensores;
  int8_t rele1 = -1, rele2 = -1;  // -1 = desconocido
  uint8_t pushSeq = 0;
  char pushUid[2 * app::kUidMax + 1] = "?";
  bool alertaCallado = false, alertaDuty = false;
  Ventana aire;
};

static Nodo nodos[256];          // indice = id LoRaLAN; 0 = gateway
static uint32_t porTipo[16] = {0};
static uint32_t total = 0, crcMal = 0, invalidas = 0, otraRed = 0;
static uint32_t tramasEsteMin = 0, tramasMinAnterior = 0, minutoTramas = 0;
static uint32_t ultimoTimeSyncMs = 0;
static uint32_t ultimaRxMs = 0;          // para el piloto de recepcion

static const char* NOMBRE_TIPO[] = { "?", "PUSH", "ACK", "POLL", "RESP", "?", "TSYNC", "JREQ", "JACC", "LEAVE", "CMD" };
static const char* nombreTipo(uint8_t t) { return t <= 0x0A ? NOMBRE_TIPO[t] : "?"; }

// Log circular
#define LOG_LINEAS 60
static struct { char txt[41]; uint16_t color; } logLineas[LOG_LINEAS];
static uint16_t logN = 0;   // lineas escritas en total

static void anotar(uint16_t color, const char* fmt, ...) {
  auto& l = logLineas[logN++ % LOG_LINEAS];
  char tmp[64];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  const uint32_t s = millis() / 1000;
  snprintf(l.txt, sizeof(l.txt), "%02lu:%02lu %s", (s / 60) % 60, s % 60, tmp);
  l.color = color;
  Serial.println(l.txt);
}
// Paleta (RGB565), tema oscuro
#define RGB(r, g, b) uint16_t(((r) & 0xF8) << 8 | ((g) & 0xFC) << 3 | (b) >> 3)
static const uint16_t C_FONDO   = RGB(10, 12, 18);
static const uint16_t C_BARRA   = RGB(22, 26, 38);
static const uint16_t C_TARJETA = RGB(30, 35, 50);
static const uint16_t C_ACENTO  = RGB(0, 170, 255);
static const uint16_t C_OK      = RGB(60, 210, 120);
static const uint16_t C_AVISO   = RGB(255, 170, 40);
static const uint16_t C_MAL     = RGB(255, 75, 75);
static const uint16_t C_TENUE   = RGB(125, 135, 155);
static const uint16_t C_TEXTO   = RGB(235, 240, 250);

#define INFO(...)   anotar(C_TEXTO, __VA_ARGS__)
#define OK(...)     anotar(C_OK, __VA_ARGS__)
#define ALERTA(...) anotar(C_MAL, __VA_ARGS__)

// ============================================
// Analisis de cada trama
// ============================================

static const char* textoCodigo(uint8_t c) {
  static const char* t[] = { "OK", "SIN_CLASE", "DENEGADO", "DESCONOCIDO", "REINTENTAR" };
  return c <= 4 ? t[c] : "?";
}

static void procesarTrama(const uint8_t* buf, size_t len, float rssi, float snr, uint32_t aireMs) {
  total++;
  ultimaRxMs = millis();
  if (millis() / 60000UL != minutoTramas) {
    tramasMinAnterior = tramasEsteMin;
    tramasEsteMin = 0;
    minutoTramas = millis() / 60000UL;
  }
  tramasEsteMin++;

  LoRaLANFrame f;
  if (!LoRaLANPacket::parse(buf, len, f)) { invalidas++; return; }
  const LoRaLANHeader& h = f.header;
  if (h.netId != LORALAN_NET_ID) {
    if (otraRed++ == 0) ALERTA("Trama de otra red 0x%02X", h.netId);
    return;
  }
  porTipo[h.type & 0x0F]++;

  const uint8_t tipo = h.type;
  const bool retry = h.flags & uint8_t(Flags::IS_RETRY);
  const uint8_t idNodo = h.srcId == LORALAN_GATEWAY_ID ? h.dstId : h.srcId;
  Nodo& emisor = nodos[h.srcId];
  Nodo& nodo = nodos[idNodo];   // si dst es broadcast (0xFF) no se usa

  // Estadisticas del emisor
  emisor.vistoMs = millis();
  emisor.rssi = rssi;
  emisor.snr = snr;
  emisor.rssiMedia = emisor.tramas ? emisor.rssiMedia * 0.9f + rssi * 0.1f : rssi;
  emisor.tramas++;
  emisor.aire.sumar(aireMs);
  if (retry) emisor.reintentos++;

  // Huecos de seq (solo nodos; el ACK lleva la seq de la trama que confirma)
  if (h.srcId != LORALAN_GATEWAY_ID && tipo != uint8_t(MsgType::ACK)) {
    if (emisor.haySeq) {
      const uint8_t d = h.seq - emisor.ultSeq;
      if (d == 0 && !retry && tipo != uint8_t(MsgType::JOIN_REQUEST)) {
        ALERTA("Seq repetida nodo %u (replay?)", h.srcId);
      } else if (d > 1 && d < 128) {
        emisor.perdidas += d - 1;
      }
    }
    emisor.haySeq = true;
    emisor.ultSeq = h.seq;
  }

  // MIC: JOIN y broadcast con la APP_KEY, el resto con la clave de sesion del nodo
  const bool conAppKey = tipo == uint8_t(MsgType::JOIN_REQUEST) || tipo == uint8_t(MsgType::JOIN_ACCEPT) || h.dstId == 0xFF;
  const uint8_t* clave = conAppKey ? LORALAN_APP_KEY : (nodo.tieneClave ? nodo.clave : nullptr);
  bool micOk = false;
  if (clave) {
    micOk = LoRaLANPacket::checkMic(buf, len, clave);
    if (!micOk) {
      emisor.micMal++;
      ALERTA("MIC incorrecto %s de %u", nombreTipo(tipo), h.srcId);
    }
  }

  // Payload descifrado (solo si el MIC confirma que la clave es la buena)
  uint8_t claro[LORALAN_MAX_PAYLOAD];
  const bool cifrado = tipo == uint8_t(MsgType::PUSH_EVENT) || tipo == uint8_t(MsgType::POLL_RESPONSE) ||
                       tipo == uint8_t(MsgType::CMD);
  bool hayClaro = false;
  if (cifrado && micOk && h.payloadLen > 0) {
    uint8_t nonce[16];
    loralan_crypto::buildNonce(nonce, h.netId, h.srcId, h.seq);
    hayClaro = loralan_crypto::aes128CtrCrypt(clave, nonce, f.payload, claro, h.payloadLen);
  }

  switch (static_cast<MsgType>(tipo)) {
    case MsgType::JOIN_REQUEST:
      INFO("JOIN pedido por nodo %u", h.srcId);
      break;

    case MsgType::JOIN_ACCEPT: {
      if (h.payloadLen < 4) break;
      const uint8_t asignado = f.payload[0];
      const uint16_t devNonce = uint16_t(f.payload[1] << 8) | f.payload[2];
      const uint8_t estado = f.payload[3];
      Nodo& n = nodos[asignado];
      if (estado != 0) { ALERTA("JOIN rechazado nodo %u (%u)", asignado, estado); break; }
      n.asociado = true;
      n.haySeq = false;
      n.pollsSinResp = 0;
      n.alertaCallado = false;
      n.tieneClave = micOk && loralan_crypto::deriveSessionKey(LORALAN_APP_KEY, h.netId, asignado, devNonce, n.clave);
      OK("JOIN nodo %u %s", asignado, n.tieneClave ? "(clave obtenida)" : "(sin clave)");
      break;
    }

    case MsgType::LEAVE:
      nodos[h.srcId].asociado = false;
      INFO("LEAVE nodo %u", h.srcId);
      break;

    case MsgType::TIME_SYNC:
      ultimoTimeSyncMs = millis();
      break;

    case MsgType::POLL:
      if (++nodo.pollsSinResp > POLLS_SIN_RESPUESTA_MAX && !nodo.alertaCallado) {
        nodo.alertaCallado = true;
        ALERTA("Nodo %u no responde (%u polls)", idNodo, nodo.pollsSinResp);
      }
      break;

    case MsgType::POLL_RESPONSE:
      if (nodo.alertaCallado) OK("Nodo %u vuelve a responder", idNodo);
      nodo.pollsSinResp = 0;
      nodo.alertaCallado = false;
      nodo.asociado = true;
      if (hayClaro) {
        bool r1, r2;
        if (app::decodeSensores(claro, h.payloadLen, nodo.sensores)) {
          nodo.haySensores = true;
        } else if (app::decodeEstadoReles(claro, h.payloadLen, r1, r2)) {
          nodo.rele1 = r1;
          nodo.rele2 = r2;
        }
      }
      break;

    case MsgType::PUSH_EVENT: {
      nodo.asociado = true;
      nodo.pushSeq = h.seq;
      const uint8_t* uid;
      uint8_t uidLen;
      if (hayClaro && app::decodeFichaje(claro, h.payloadLen, uid, uidLen)) {
        app::uidToHex(uid, uidLen, nodo.pushUid);
        INFO("Fichaje nodo %u %s%s", idNodo, nodo.pushUid, retry ? " (reint.)" : "");
      } else {
        strcpy(nodo.pushUid, "?");
        INFO("PUSH nodo %u (cifrado)%s", idNodo, retry ? " (reint.)" : "");
      }
      break;
    }

    case MsgType::ACK:
      // ACK del gateway a un PUSH: el primer byte es el codigo del fichaje (en claro).
      if (h.srcId == LORALAN_GATEWAY_ID && h.seq == nodo.pushSeq && h.payloadLen >= 1) {
        const uint8_t c = f.payload[0];
        anotar(c == 0 ? C_OK : C_AVISO,"Nodo %u %s: %s", idNodo, nodo.pushUid, textoCodigo(c));
      }
      break;

    case MsgType::CMD: {
      uint8_t numero;
      bool on;
      if (!hayClaro) INFO("CMD a nodo %u (cifrado)", idNodo);
      else if (app::decodeRele(claro, h.payloadLen, numero, on)) INFO("CMD nodo %u rele%u %s", idNodo, numero, on ? "ON" : "OFF");
      else if (app::tipo(claro, h.payloadLen) == uint8_t(app::Msg::RESET)) INFO("CMD nodo %u RESET", idNodo);
      break;
    }
  }
}

static void revisarDuty() {
  for (int i = 0; i < 256; i++) {
    Nodo& n = nodos[i];
    if (!n.vistoMs) continue;
    const bool excede = n.aire.dutyPct() > DUTY_LIMITE_PCT;
    if (excede && !n.alertaDuty) ALERTA("%s %u supera %.0f%% de ciclo", i == LORALAN_GATEWAY_ID ? "GW" : "Nodo", i, DUTY_LIMITE_PCT);
    n.alertaDuty = excede;
  }
}

// ============================================
// Pantalla 240x135: barra superior con pestanas + zona de contenido (y 20..135)
// ============================================
static uint8_t pantalla = 1;
static int desplazamiento = 0;
static bool hayMasAbajo = false;

static const int Y0 = 20;   // inicio del contenido

static void hace(char* out, size_t n, uint32_t ms) {
  const uint32_t s = (millis() - ms) / 1000;
  if (s < 60) snprintf(out, n, "%lus", s);
  else if (s < 3600) snprintf(out, n, "%lum", s / 60);
  else snprintf(out, n, "%luh", s / 3600);
}

static void texto(const lgfx::IFont* fuente, uint16_t color, int x, int y, const char* s, textdatum_t datum = top_left) {
  lienzo.setFont(fuente);
  lienzo.setTextColor(color);
  lienzo.setTextDatum(datum);
  lienzo.drawString(s, x, y);
}

static void pastilla(int x, int y, const char* s, uint16_t fondo, uint16_t color) {
  lienzo.setFont(&fonts::Font0);
  const int w = lienzo.textWidth(s) + 8;
  lienzo.fillRoundRect(x, y, w, 11, 5, fondo);
  texto(&fonts::Font0, color, x + 4, y + 2, s);
}

static uint16_t colorRssi(float rssi) {
  return rssi > -90 ? C_OK : rssi > -105 ? C_AVISO : C_MAL;
}

// Cuatro barras de cobertura, como en un movil
static void iconoSenal(int x, int y, float rssi) {
  const int llenas = rssi > -80 ? 4 : rssi > -95 ? 3 : rssi > -105 ? 2 : rssi > -115 ? 1 : 0;
  for (int i = 0; i < 4; i++) {
    const int h = 3 + 3 * i;
    lienzo.fillRect(x + i * 4, y + 12 - h, 3, h, i < llenas ? colorRssi(rssi) : C_BARRA);
  }
}

static void barraSuperior() {
  lienzo.fillRect(0, 0, 240, 18, C_BARRA);
  texto(&fonts::DejaVu12, C_ACENTO, 5, 3, "LoRaLAN");

  // Piloto de recepcion: se enciende un momento con cada trama
  const bool rx = ultimaRxMs && millis() - ultimaRxMs < 400;
  lienzo.fillCircle(66, 9, 3, rx ? C_OK : C_TARJETA);

  static const char* pestanas[] = { "Resumen", "Nodos", "Log" };
  int x = 76;
  for (int i = 0; i < 3; i++) {
    lienzo.setFont(&fonts::Font0);
    const int w = lienzo.textWidth(pestanas[i]) + 10;
    const bool activa = pantalla == i + 1;
    if (activa) lienzo.fillRoundRect(x, 3, w, 13, 6, C_ACENTO);
    texto(&fonts::Font0, activa ? C_FONDO : C_TENUE, x + 5, 6, pestanas[i]);
    x += w + 3;
  }

  // Bateria
  const int nivel = M5Cardputer.Power.getBatteryLevel();
  if (nivel >= 0) {
    const uint16_t c = nivel > 50 ? C_OK : nivel > 20 ? C_AVISO : C_MAL;
    lienzo.drawRoundRect(214, 5, 20, 9, 2, C_TENUE);
    lienzo.fillRect(234, 7, 2, 5, C_TENUE);
    lienzo.fillRect(216, 7, 16 * nivel / 100, 5, c);
  }
}

static void tarjetaDato(int x, int y, const char* titulo, const char* valor, uint16_t color) {
  lienzo.fillRoundRect(x, y, 116, 34, 6, C_TARJETA);
  texto(&fonts::Font0, C_TENUE, x + 7, y + 5, titulo);
  texto(&fonts::DejaVu18, color, x + 7, y + 14, valor);
}

static void dibujarResumen() {
  char v[16];
  int activos = 0;
  for (int i = 1; i < 256; i++) activos += nodos[i].vistoMs && millis() - nodos[i].vistoMs < 300000UL;

  snprintf(v, sizeof(v), "%lu", tramasMinAnterior);
  tarjetaDato(3, Y0 + 1, "TRAMAS / MIN", v, C_ACENTO);
  snprintf(v, sizeof(v), "%d", activos);
  tarjetaDato(121, Y0 + 1, "NODOS ACTIVOS", v, activos ? C_OK : C_TENUE);
  snprintf(v, sizeof(v), "%lu / %lu", crcMal, invalidas + otraRed);
  tarjetaDato(3, Y0 + 38, "CRC MAL / AJENAS", v, crcMal + invalidas + otraRed ? C_AVISO : C_OK);
  strcpy(v, "--");
  if (ultimoTimeSyncMs) hace(v, sizeof(v), ultimoTimeSyncMs);
  tarjetaDato(121, Y0 + 38, "ULTIMO TIME_SYNC", v, C_TEXTO);

  // Ciclo de trabajo de la ultima hora: escala 0..2 %, marca en el limite del 1 %
  int y = Y0 + 77;
  texto(&fonts::Font0, C_TENUE, 5, y, "CICLO DE TRABAJO (1 h)   limite 1%");
  y += 11;
  int filas = 0;
  for (int i = 0; i < 256 && filas < 2; i++) {
    Nodo& n = nodos[i];
    if (!n.vistoMs) continue;
    filas++;
    const float d = n.aire.dutyPct();
    char etiqueta[8];
    snprintf(etiqueta, sizeof(etiqueta), i == LORALAN_GATEWAY_ID ? "GW" : "N%d", i);
    texto(&fonts::Font0, C_TEXTO, 5, y + 2, etiqueta);
    const int bx = 32, bw = 150;
    lienzo.fillRoundRect(bx, y + 1, bw, 9, 4, C_TARJETA);
    const int lleno = constrain(int(d / 2.0f * bw), d > 0 ? 4 : 0, bw);
    lienzo.fillRoundRect(bx, y + 1, lleno, 9, 4, d > DUTY_LIMITE_PCT ? C_MAL : d > 0.7f ? C_AVISO : C_OK);
    lienzo.drawFastVLine(bx + bw / 2, y - 1, 13, C_TEXTO);
    snprintf(v, sizeof(v), "%.2f%%", d);
    texto(&fonts::Font0, C_TEXTO, 236, y + 2, v, top_right);
    y += 13;
  }
  if (!filas) texto(&fonts::Font0, C_TENUE, 5, y + 2, "Esperando tramas...");
}

static void tarjetaNodo(int y, int id, Nodo& n) {
  lienzo.fillRoundRect(3, y, 234, 54, 7, C_TARJETA);
  const uint16_t estado = (n.alertaCallado || n.micMal) ? C_MAL : (n.asociado || id == 0) ? C_OK : C_TENUE;
  lienzo.fillRoundRect(3, y, 4, 54, 2, estado);

  char s[48];
  snprintf(s, sizeof(s), id == LORALAN_GATEWAY_ID ? "Gateway" : "Nodo %d", id);
  texto(&fonts::DejaVu12, C_TEXTO, 12, y + 4, s);

  // Etiquetas de estado a la derecha
  int x = 232;
  auto etiqueta = [&](const char* t, uint16_t fondo, uint16_t color) {
    lienzo.setFont(&fonts::Font0);
    x -= lienzo.textWidth(t) + 8;
    pastilla(x, y + 5, t, fondo, color);
    x -= 3;
  };
  if (n.alertaCallado) etiqueta("CALLADO", C_MAL, C_TEXTO);
  else if (n.asociado || id == 0) etiqueta("ACTIVO", C_OK, C_FONDO);
  if (n.tieneClave) etiqueta("CLAVE", C_ACENTO, C_FONDO);
  if (n.micMal) etiqueta("MIC!", C_MAL, C_TEXTO);

  // Radio
  iconoSenal(12, y + 19, n.rssi);
  snprintf(s, sizeof(s), "%.0f dBm  SNR %.1f", n.rssi, n.snr);
  texto(&fonts::Font0, colorRssi(n.rssi), 32, y + 22, s);
  char t[8];
  hace(t, sizeof(t), n.vistoMs);
  snprintf(s, sizeof(s), "hace %s", t);
  texto(&fonts::Font0, C_TENUE, 232, y + 22, s, top_right);

  // Datos de la aplicacion
  if (n.haySensores) {
    const app::Sensores& se = n.sensores;
    snprintf(s, sizeof(s), "%.1fC  %.0f%%  %.0f lx", se.temperatura, se.humedad, se.lux);
    texto(&fonts::DejaVu12, C_TEXTO, 12, y + 36, s);
    lienzo.fillCircle(150, y + 42, 3, se.presencia ? C_OK : C_TENUE);
    texto(&fonts::Font0, C_TENUE, 156, y + 39, se.presencia ? "gente" : "vacio");
  } else {
    snprintf(s, sizeof(s), "%lu tramas  %lu reint  %lu perd", n.tramas, n.reintentos, n.perdidas);
    texto(&fonts::Font0, C_TENUE, 12, y + 39, s);
  }
  if (n.rele1 >= 0) {
    pastilla(192, y + 38, "R1", n.rele1 ? C_AVISO : C_BARRA, n.rele1 ? C_FONDO : C_TENUE);
    pastilla(214, y + 38, "R2", n.rele2 ? C_AVISO : C_BARRA, n.rele2 ? C_FONDO : C_TENUE);
  }
}

static void dibujarNodos() {
  int visibles[256], total = 0;
  for (int i = 0; i < 256; i++) if (nodos[i].vistoMs) visibles[total++] = i;
  if (!total) {
    texto(&fonts::DejaVu12, C_TENUE, 120, 70, "Aun no se ha oido nada", middle_center);
    return;
  }
  desplazamiento = constrain(desplazamiento, 0, max(0, total - 2));
  for (int k = 0; k < 2 && desplazamiento + k < total; k++) {
    const int id = visibles[desplazamiento + k];
    tarjetaNodo(Y0 + 1 + k * 57, id, nodos[id]);
  }
  hayMasAbajo = desplazamiento + 2 < total;
}

static void dibujarLog() {
  const int visibles = 11, alto = 10;
  const int disponibles = min<int>(logN, LOG_LINEAS);
  desplazamiento = constrain(desplazamiento, 0, max(0, disponibles - visibles));
  const int primera = max(0, disponibles - visibles - desplazamiento);
  int y = Y0 + 2;
  for (int i = primera; i < min(disponibles, primera + visibles); i++) {
    auto& l = logLineas[(logN - disponibles + i) % LOG_LINEAS];
    if ((i & 1) == 0) lienzo.fillRect(0, y - 1, 240, alto, C_BARRA);
    lienzo.fillCircle(5, y + 3, 2, l.color);
    // hora en tenue, mensaje en su color
    char hora[6];
    memcpy(hora, l.txt, 5);
    hora[5] = '\0';
    texto(&fonts::Font0, C_TENUE, 11, y, hora);
    texto(&fonts::Font0, l.color, 44, y, l.txt + 6);
    y += alto;
  }
  if (!disponibles) texto(&fonts::DejaVu12, C_TENUE, 120, 70, "Sin eventos", middle_center);
  hayMasAbajo = false;
  if (desplazamiento > 0) lienzo.fillTriangle(232, 128, 238, 128, 235, 132, C_ACENTO);   // hay mas recientes
}

static void dibujar() {
  hayMasAbajo = false;
  lienzo.fillSprite(C_FONDO);
  barraSuperior();
  if (pantalla == 1) dibujarResumen();
  else if (pantalla == 2) dibujarNodos();
  else dibujarLog();
  if (hayMasAbajo) lienzo.fillTriangle(232, 128, 238, 128, 235, 132, C_ACENTO);
  lienzo.pushSprite(0, 0);
}

static void atenderTeclado() {
  if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
  for (char c : M5Cardputer.Keyboard.keysState().word) {
    if (c >= '1' && c <= '3') { pantalla = c - '0'; desplazamiento = 0; }
    else if (c == ';') desplazamiento += pantalla == 3 ? 1 : -1;   // flecha arriba
    else if (c == '.') desplazamiento += pantalla == 3 ? -1 : 1;   // flecha abajo
  }
  desplazamiento = max(0, desplazamiento);
}

// ============================================
static volatile bool recibido = false;
static void IRAM_ATTR alRecibir() { recibido = true; }

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Display.setRotation(1);
  lienzo.createSprite(240, 135);
  lienzo.setTextSize(1);
  Serial.begin(115200);

  spiLoRa.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  int estado = radio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR, LORA_SYNC_WORD,
                           10, LORA_PREAMBLE_LEN, LORA_TCXO_V);
  if (estado == RADIOLIB_ERR_NONE) estado = radio.setDio2AsRfSwitch(true);
  if (estado == RADIOLIB_ERR_NONE) radio.setRxBoostedGainMode(true);   // mas sensibilidad
  if (estado != RADIOLIB_ERR_NONE) {
    lienzo.fillSprite(C_FONDO);
    lienzo.fillRoundRect(20, 35, 200, 65, 8, C_TARJETA);
    lienzo.fillRoundRect(20, 35, 5, 65, 2, C_MAL);
    texto(&fonts::DejaVu18, C_MAL, 122, 55, "Error SX1262", middle_center);
    char s[40];
    snprintf(s, sizeof(s), "codigo %d - revisa el Cap LoRa", estado);
    texto(&fonts::DejaVu12, C_TENUE, 122, 80, s, middle_center);
    lienzo.pushSprite(0, 0);
    for (;;) delay(1000);
  }
  radio.setPacketReceivedAction(alRecibir);
  radio.startReceive();

  // Pantalla de bienvenida
  lienzo.fillSprite(C_FONDO);
  texto(&fonts::DejaVu24, C_ACENTO, 120, 50, "LoRaLAN", middle_center);
  texto(&fonts::DejaVu12, C_TEXTO, 120, 76, "Monitor de red", middle_center);
  texto(&fonts::Font0, C_TENUE, 120, 100, "869.525 MHz SF7  BW125  - solo escucha", middle_center);
  lienzo.pushSprite(0, 0);
  delay(1500);

  OK("Escuchando %.3f MHz SF%d (solo RX)", LORA_FREQ_MHZ, LORA_SF);
}

void loop() {
  M5Cardputer.update();
  atenderTeclado();

  if (recibido) {
    recibido = false;
    uint8_t buf[256];
    const size_t len = radio.getPacketLength();
    const int estado = radio.readData(buf, min(len, sizeof(buf)));
    if (estado == RADIOLIB_ERR_NONE) {
      procesarTrama(buf, len, radio.getRSSI(), radio.getSNR(), radio.getTimeOnAir(len) / 1000);
    } else if (estado == RADIOLIB_ERR_CRC_MISMATCH) {
      crcMal++;
    }
    radio.startReceive();
  }

  static uint32_t ultimoDibujo = 0;
  if (millis() - ultimoDibujo > 200) {
    ultimoDibujo = millis();
    revisarDuty();
    dibujar();
  }
}
