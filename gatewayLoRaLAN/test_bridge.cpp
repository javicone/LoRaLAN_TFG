// Autocomprobacion de src/bridge.* (no necesita hardware ni red).
//
//   g++ -std=c++17 -I src -I ../Device_ESP32_LoRaLAN test_bridge.cpp src/bridge.cpp -o test_bridge
//   ./test_bridge      codigo de salida 0 = todo correcto

#undef NDEBUG   // los assert son el test: no deben desaparecer en Release

#include "bridge.h"

#include <cassert>
#include <cmath>
#include <cstdio>

static net::Response resp(bool ok, int status, const char* body) {
    net::Response r;
    r.ok = ok;
    r.status = status;
    r.body = body;
    return r;
}

int main() {
    using app::Code;
    const auto code = [](Code c) { return static_cast<uint8_t>(c); };

    // --- Autenticacion del gateway ---
    assert(bridge::authJson("gw-rpi-01", "k\"1") == "{\"device_id\":\"gw-rpi-01\",\"api_key\":\"k\\\"1\"}");
    std::string token = "previo";
    assert(bridge::tokenDeRespuesta(resp(true, 200, "{\"access_token\":\"eyJ.abc\",\"token_type\":\"bearer\"}"), token));
    assert(token == "eyJ.abc");
    token = "previo";
    assert(!bridge::tokenDeRespuesta(resp(true, 401, "{\"access_token\":\"x\"}"), token) && token == "previo");
    assert(!bridge::tokenDeRespuesta(resp(true, 200, "{\"detail\":\"x\"}"), token));
    assert(!bridge::tokenDeRespuesta(resp(true, 200, "{\"access_token\":\"\"}"), token));
    assert(!bridge::tokenDeRespuesta(resp(false, 0, ""), token));

    // --- Fichaje: peticion ---
    assert(bridge::fichajeJson("gw-\"1", 16, "04A3") ==
           "{\"gateway\":\"gw-\\\"1\",\"node\":16,\"uid\":\"04A3\"}");

    // --- Fichaje: respuesta -> codigo del ACK ---
    assert(bridge::codigoFichaje(resp(true, 200, "{\"code\":0}")) == code(Code::OK));
    assert(bridge::codigoFichaje(resp(true, 200, "{\"code\": 2, \"detalle\":\"x\"}")) == code(Code::DENEGADO));
    assert(bridge::codigoFichaje(resp(true, 200, "{\"code\":3}")) == code(Code::DESCONOCIDO));
    // Todo lo raro es REINTENTAR, nunca un NACK definitivo inventado
    assert(bridge::codigoFichaje(resp(false, 0, "")) == code(Code::REINTENTAR));           // red caida / timeout
    assert(bridge::codigoFichaje(resp(true, 500, "{\"code\":0}")) == code(Code::REINTENTAR));
    assert(bridge::codigoFichaje(resp(true, 200, "{\"otro\":1}")) == code(Code::REINTENTAR));
    assert(bridge::codigoFichaje(resp(true, 200, "{\"code\":9}")) == code(Code::REINTENTAR));
    assert(bridge::codigoFichaje(resp(true, 200, "{\"code\":-1}")) == code(Code::REINTENTAR));

    // --- Uplink: sensores ---
    uint8_t buf[64];
    app::Sensores s;
    s.temperatura = 21.5f;
    s.humedad = 40.1f;
    s.lux = 320.0f;
    s.presencia = true;             // presion sin lectura -> null
    uint8_t n = app::encodeSensores(buf, s);
    std::string json;
    assert(bridge::uplinkJson("gw", 16, buf, n, -60.0f, 9.5f, json));
    assert(json == "{\"gateway\":\"gw\",\"node\":16,\"tipo\":\"sensores\",\"temperatura\":21.50,"
                   "\"humedad\":40.10,\"presion\":null,\"luz\":320,\"presencia\":true,"
                   "\"rssi\":-60.0,\"snr\":9.5}");

    // --- Uplink: reles ---
    n = app::encodeEstadoReles(buf, true, false);
    assert(bridge::uplinkJson("gw", 7, buf, n, -70.0f, 1.0f, json));
    assert(json == "{\"gateway\":\"gw\",\"node\":7,\"tipo\":\"reles\",\"rele1\":true,\"rele2\":false,"
                   "\"rssi\":-70.0,\"snr\":1.0}");

    // --- Uplink: lo desconocido no se sube ---
    assert(!bridge::uplinkJson("gw", 7, buf, 0, 0, 0, json));      // POLL vacio
    n = app::encodeReset(buf);                                      // tipo de bajada
    assert(!bridge::uplinkJson("gw", 7, buf, n, 0, 0, json));
    n = app::encodeEstadoReles(buf, true, true);
    assert(!bridge::uplinkJson("gw", 7, buf, n - 1, 0, 0, json));  // truncado

    assert(bridge::estadoJson("gw", 3, false) ==
           "{\"gateway\":\"gw\",\"node\":3,\"tipo\":\"estado\",\"online\":false}");

    // --- Comandos ---
    unsigned malos = 99;
    auto cmds = bridge::parseCommands(
        "{\"commands\":[{\"node\":16,\"tipo\":\"rele\",\"rele\":2,\"on\":true},"
        "{\"node\": 17, \"tipo\": \"reset\"},"
        "{\"node\":16,\"tipo\":\"rele\",\"rele\":5,\"on\":true},"   // rele inexistente
        "{\"node\":0,\"tipo\":\"reset\"},"                          // nodo invalido
        "{\"node\":16,\"tipo\":\"bailar\"},"                        // tipo desconocido
        "{\"node\":16,\"tipo\":\"rele\",\"rele\":1}"                // falta "on"
        "]}", &malos);
    assert(cmds.size() == 2 && malos == 4);
    uint8_t num = 0;
    bool on = false;
    assert(cmds[0].node == 16 && app::decodeRele(cmds[0].data, cmds[0].len, num, on) && num == 2 && on);
    assert(cmds[1].node == 17 && cmds[1].len == 1 && app::tipo(cmds[1].data, 1) == uint8_t(app::Msg::RESET));

    // Un campo de un objeto no se lee del siguiente
    cmds = bridge::parseCommands("{\"commands\":[{\"node\":16,\"tipo\":\"rele\",\"rele\":1},{\"on\":true}]}", &malos);
    assert(cmds.empty() && malos == 2);

    assert(bridge::parseCommands("{\"commands\":[]}", &malos).empty() && malos == 0);
    assert(bridge::parseCommands("basura", &malos).empty());

    printf("test_bridge: OK\n");
    return 0;
}
