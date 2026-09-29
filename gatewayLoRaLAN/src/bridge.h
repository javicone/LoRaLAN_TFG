#pragma once
// Traduccion entre los mensajes LoRaLAN de aplicacion (AppProtocol.h) y el
// JSON que intercambia el gateway con el backend FastAPI.
//
// Sin E/S ni hilos: todo lo que tiene logica esta aqui para poder probarlo en
// PC (test_bridge.cpp). main.cpp solo mueve bytes entre radio, colas y HTTP.
//
// Contrato con Server/routers/gateway.py:
//
//   POST /auth/device       {"device_id":"gw","api_key":"..."}
//                           -> 200 {"access_token":"...","token_type":"bearer"}
//                           (el token caduca: el gateway lo renueva ante un 401)
//   POST /gateway/fichaje   {"gateway":"gw","node":16,"uid":"04A30FB2"}
//                           -> 200 {"code":0..4}  (AppProtocol.h, app::Code)
//   POST /gateway/uplink    {"gateway":"gw","node":16,"tipo":"sensores",
//                            "temperatura":21.5,"humedad":40.1,"presion":1013.2,
//                            "luz":320,"presencia":true,"rssi":-60.0,"snr":9.5}
//                           {"gateway":"gw","node":16,"tipo":"reles",
//                            "rele1":true,"rele2":false,"rssi":..,"snr":..}
//                           {"gateway":"gw","node":16,"tipo":"estado","online":true}
//   GET  /gateway/commands?gateway=gw
//                           -> {"commands":[{"node":16,"tipo":"rele","rele":1,"on":true},
//                                           {"node":16,"tipo":"reset"}]}
//   POST /gateway/status    (sin cambios, ver main.cpp)

#include "AppProtocol.h"
#include "net/http_client.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bridge {

// Escapa comillas y barras; descarta caracteres de control.
std::string jsonEscape(const std::string& s);

std::string authJson(const std::string& deviceId, const std::string& apiKey);

// Extrae access_token de la respuesta de /auth/device. false si no es un 200 con token.
bool tokenDeRespuesta(const net::Response& r, std::string& token);

std::string fichajeJson(const std::string& gateway, uint8_t node, const std::string& uidHex);

// Codigo para el ACK a partir de la respuesta del servidor. Cualquier cosa que
// no sea un 200 con un codigo conocido es REINTENTAR: ante la duda, que el
// nodo lo vuelva a intentar en vez de dar un NACK definitivo falso.
uint8_t codigoFichaje(const net::Response& r);

// JSON de /gateway/uplink para una POLL_RESPONSE. false si el payload no es
// un mensaje conocido (vacio, tipo desconocido o longitud incorrecta).
bool uplinkJson(const std::string& gateway, uint8_t node, const uint8_t* payload, uint8_t len,
                float rssi, float snr, std::string& out);

std::string estadoJson(const std::string& gateway, uint8_t node, bool online);

// Comando listo para gateway.sendCommand().
struct Command {
    uint8_t node = 0;
    uint8_t len = 0;
    uint8_t data[app::kLenRele] = {0};   // el comando mas largo
};

// Lee la respuesta de GET /gateway/commands. Los comandos mal formados o de
// tipo desconocido se descartan (y se cuentan en `descartados`).
std::vector<Command> parseCommands(const std::string& body, unsigned* descartados = nullptr);

} // namespace bridge
