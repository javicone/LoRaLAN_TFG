#pragma once
// Mensajes de aplicacion de Control de Asistencia sobre LoRaLAN.
//
// Lo comparten el firmware del nodo (este sketch) y el gateway de la Raspberry
// (gatewayLoRaLAN lo incluye desde aqui): una sola definicion para los dos
// extremos. Sin dependencias de Arduino para poder compilarlo y probarlo en PC.
//
// Byte 0 = tipo de mensaje; el resto depende del tipo. Multibyte en big-endian.
// Todo cabe de sobra en LORALAN_MAX_PAYLOAD (64 B).
//
//   nodo -> gw   FICHAJE       01 | UID NFC (4..10 B)                PUSH, ACK diferido
//   nodo -> gw   SENSORES      02 | temp i16 (C*100) | hum u16 (%*100)
//                                 | presion u16 (hPa*10) | lux u16 | presencia u8
//                                                                     respuesta a POLL
//   nodo -> gw   ESTADO_RELES  03 | bit0 rele1, bit1 rele2           respuesta a POLL
//   gw -> nodo   RELE          10 | numero (1..2) | on (0/1)         CMD
//   gw -> nodo   RESET         11                                    CMD
//
// Sensor sin lectura valida: NAN al codificar -> valor centinela en el aire ->
// NAN al decodificar.

#include <math.h>
#include <stdint.h>
#include <string.h>

namespace app {

enum class Msg : uint8_t {
    FICHAJE = 0x01,
    SENSORES = 0x02,
    ESTADO_RELES = 0x03,
    RELE = 0x10,
    RESET = 0x11,
};

// Codigo de aplicacion que viaja en el ACK del FICHAJE (1 byte).
// 0 = ACK; cualquier otro = NACK. Mismos valores en Server/routers/gateway.py.
enum class Code : uint8_t {
    OK = 0,           // 200: asistencia registrada
    SIN_CLASE = 1,    // 400: NACK definitivo, no hay clase ahora en ese aula
    DENEGADO = 2,     // 403: NACK definitivo, no puede fichar en esta clase
    DESCONOCIDO = 3,  // 404: NACK definitivo, tarjeta o aula no registrada
    REINTENTAR = 4,   // 5xx o sin respuesta: NACK temporal, el nodo reenvia
};

// Un NACK definitivo no se reintenta; REINTENTAR (o un codigo desconocido) si.
inline bool esDefinitivo(uint8_t code) {
    return code <= static_cast<uint8_t>(Code::DESCONOCIDO);
}

constexpr uint8_t kUidMin = 4;
constexpr uint8_t kUidMax = 10;
constexpr uint8_t kLenFichajeMax = 1 + kUidMax;
constexpr uint8_t kLenSensores = 10;
constexpr uint8_t kLenEstadoReles = 2;
constexpr uint8_t kLenRele = 3;
constexpr uint8_t kLenReset = 1;

struct Sensores {
    float temperatura = NAN;  // C
    float humedad = NAN;      // %
    float presion = NAN;      // hPa
    float lux = NAN;
    bool presencia = false;
};

namespace detail {

constexpr int16_t kInvalidI16 = INT16_MIN;
constexpr uint16_t kInvalidU16 = 0xFFFF;

inline void putU16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); }
inline uint16_t getU16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }

// Redondea y satura; NAN o fuera de rango -> centinela.
inline int16_t toI16(float v, float scale) {
    if (isnan(v)) return kInvalidI16;
    const float s = roundf(v * scale);
    return (s <= float(kInvalidI16) || s > float(INT16_MAX)) ? kInvalidI16 : int16_t(s);
}
inline uint16_t toU16(float v, float scale) {
    if (isnan(v)) return kInvalidU16;
    const float s = roundf(v * scale);
    return (s < 0.0f || s >= float(kInvalidU16)) ? kInvalidU16 : uint16_t(s);
}
inline float fromI16(int16_t v, float scale) { return v == kInvalidI16 ? NAN : float(v) / scale; }
inline float fromU16(uint16_t v, float scale) { return v == kInvalidU16 ? NAN : float(v) / scale; }

} // namespace detail

// Tipo de un payload recibido, o 0 si esta vacio.
inline uint8_t tipo(const uint8_t* in, uint8_t len) { return len > 0 ? in[0] : 0; }

// Los encode* devuelven la longitud escrita en out, o 0 si los datos no valen.

inline uint8_t encodeFichaje(uint8_t* out, const uint8_t* uid, uint8_t uidLen) {
    if (uidLen < kUidMin || uidLen > kUidMax) return 0;
    out[0] = uint8_t(Msg::FICHAJE);
    memcpy(out + 1, uid, uidLen);
    return uint8_t(1 + uidLen);
}

// uid apunta dentro de in (no se copia).
inline bool decodeFichaje(const uint8_t* in, uint8_t len, const uint8_t*& uid, uint8_t& uidLen) {
    if (tipo(in, len) != uint8_t(Msg::FICHAJE) || len < 1 + kUidMin || len > kLenFichajeMax) return false;
    uid = in + 1;
    uidLen = uint8_t(len - 1);
    return true;
}

// UID en hexadecimal en mayusculas, sin separadores: el formato de
// usuarios.uid_tarjeta (el mismo que generaba uidToString() en el firmware WiFi).
// out necesita 2*uidLen+1 bytes.
inline void uidToHex(const uint8_t* uid, uint8_t uidLen, char* out) {
    static const char kHex[] = "0123456789ABCDEF";
    for (uint8_t i = 0; i < uidLen; ++i) {
        out[2 * i] = kHex[uid[i] >> 4];
        out[2 * i + 1] = kHex[uid[i] & 0x0F];
    }
    out[2 * uidLen] = '\0';
}

inline uint8_t encodeSensores(uint8_t* out, const Sensores& s) {
    out[0] = uint8_t(Msg::SENSORES);
    detail::putU16(out + 1, uint16_t(detail::toI16(s.temperatura, 100.0f)));
    detail::putU16(out + 3, detail::toU16(s.humedad, 100.0f));
    detail::putU16(out + 5, detail::toU16(s.presion, 10.0f));
    detail::putU16(out + 7, detail::toU16(s.lux, 1.0f));
    out[9] = s.presencia ? 1 : 0;
    return kLenSensores;
}

inline bool decodeSensores(const uint8_t* in, uint8_t len, Sensores& s) {
    if (tipo(in, len) != uint8_t(Msg::SENSORES) || len != kLenSensores) return false;
    s.temperatura = detail::fromI16(int16_t(detail::getU16(in + 1)), 100.0f);
    s.humedad = detail::fromU16(detail::getU16(in + 3), 100.0f);
    s.presion = detail::fromU16(detail::getU16(in + 5), 10.0f);
    s.lux = detail::fromU16(detail::getU16(in + 7), 1.0f);
    s.presencia = in[9] != 0;
    return true;
}

inline uint8_t encodeEstadoReles(uint8_t* out, bool rele1, bool rele2) {
    out[0] = uint8_t(Msg::ESTADO_RELES);
    out[1] = uint8_t((rele1 ? 0x01 : 0) | (rele2 ? 0x02 : 0));
    return kLenEstadoReles;
}

inline bool decodeEstadoReles(const uint8_t* in, uint8_t len, bool& rele1, bool& rele2) {
    if (tipo(in, len) != uint8_t(Msg::ESTADO_RELES) || len != kLenEstadoReles) return false;
    rele1 = (in[1] & 0x01) != 0;
    rele2 = (in[1] & 0x02) != 0;
    return true;
}

inline uint8_t encodeRele(uint8_t* out, uint8_t numero, bool on) {
    if (numero < 1 || numero > 2) return 0;
    out[0] = uint8_t(Msg::RELE);
    out[1] = numero;
    out[2] = on ? 1 : 0;
    return kLenRele;
}

inline bool decodeRele(const uint8_t* in, uint8_t len, uint8_t& numero, bool& on) {
    if (tipo(in, len) != uint8_t(Msg::RELE) || len != kLenRele || in[1] < 1 || in[1] > 2) return false;
    numero = in[1];
    on = in[2] != 0;
    return true;
}

inline uint8_t encodeReset(uint8_t* out) {
    out[0] = uint8_t(Msg::RESET);
    return kLenReset;
}

} // namespace app
