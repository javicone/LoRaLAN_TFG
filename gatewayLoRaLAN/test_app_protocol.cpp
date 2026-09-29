// Autocomprobacion de AppProtocol.h (no necesita hardware).
//
//   g++ -std=c++17 -I ../Device_ESP32_LoRaLAN test_app_protocol.cpp -o test_app_protocol
//   ./test_app_protocol      codigo de salida 0 = todo correcto
//
// Vive aqui y no en el sketch: el IDE de Arduino compila todos los .cpp de la
// carpeta del sketch y un main() lo romperia.

#undef NDEBUG   // los assert son el test: no deben desaparecer en Release

#include "AppProtocol.h"

#include <cassert>
#include <cmath>
#include <cstdio>

static bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

int main() {
    uint8_t buf[64];

    // --- FICHAJE ---
    const uint8_t uid[] = {0x04, 0xA3, 0x0F, 0xB2, 0x5C, 0x80, 0x01};
    uint8_t n = app::encodeFichaje(buf, uid, sizeof(uid));
    assert(n == 8 && buf[0] == 0x01);
    const uint8_t* uidOut = nullptr;
    uint8_t uidLen = 0;
    assert(app::decodeFichaje(buf, n, uidOut, uidLen));
    assert(uidLen == sizeof(uid) && memcmp(uidOut, uid, uidLen) == 0);
    char hex[2 * app::kUidMax + 1];
    app::uidToHex(uidOut, uidLen, hex);
    assert(strcmp(hex, "04A30FB25C8001") == 0);
    assert(app::encodeFichaje(buf, uid, 3) == 0);    // UID demasiado corto
    assert(app::encodeFichaje(buf, uid, 11) == 0);   // demasiado largo
    assert(!app::decodeFichaje(buf, 4, uidOut, uidLen));

    // --- SENSORES ---
    app::Sensores s;
    s.temperatura = -12.34f;
    s.humedad = 55.5f;
    s.presion = 1013.25f;
    s.lux = 320.4f;
    s.presencia = true;
    n = app::encodeSensores(buf, s);
    assert(n == app::kLenSensores);
    app::Sensores r;
    assert(app::decodeSensores(buf, n, r));
    assert(near(r.temperatura, -12.34f, 0.006f));   // resolucion 0,01 C
    assert(near(r.humedad, 55.5f, 0.006f));
    assert(near(r.presion, 1013.3f, 0.06f));        // resolucion 0,1 hPa
    assert(near(r.lux, 320.0f, 0.5f));
    assert(r.presencia);

    // Sensor caido (NAN) y fuera de rango -> NAN al otro lado
    app::Sensores roto;
    roto.lux = 100000.0f;                           // no cabe en u16
    n = app::encodeSensores(buf, roto);
    assert(app::decodeSensores(buf, n, r));
    assert(std::isnan(r.temperatura) && std::isnan(r.humedad) && std::isnan(r.presion) && std::isnan(r.lux));
    assert(!r.presencia);
    assert(!app::decodeSensores(buf, n - 1, r));    // longitud incorrecta

    // --- ESTADO_RELES ---
    bool r1 = true, r2 = true;
    n = app::encodeEstadoReles(buf, false, true);
    assert(app::decodeEstadoReles(buf, n, r1, r2) && !r1 && r2);

    // --- RELE (CMD) ---
    uint8_t num = 0;
    bool on = false;
    n = app::encodeRele(buf, 2, true);
    assert(n == app::kLenRele && app::decodeRele(buf, n, num, on) && num == 2 && on);
    assert(app::encodeRele(buf, 3, true) == 0);

    // --- RESET y tipos cruzados ---
    n = app::encodeReset(buf);
    assert(n == 1 && app::tipo(buf, n) == uint8_t(app::Msg::RESET));
    assert(!app::decodeRele(buf, n, num, on));
    assert(app::tipo(buf, 0) == 0);

    // --- Codigos ACK/NACK ---
    assert(app::esDefinitivo(uint8_t(app::Code::OK)));
    assert(app::esDefinitivo(uint8_t(app::Code::DESCONOCIDO)));
    assert(!app::esDefinitivo(uint8_t(app::Code::REINTENTAR)));
    assert(!app::esDefinitivo(0x7F));   // codigo desconocido: mejor reintentar

    printf("test_app_protocol: OK\n");
    return 0;
}
