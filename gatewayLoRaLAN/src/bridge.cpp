#include "bridge.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace bridge {

namespace {

// Numero JSON; NAN (sensor sin lectura) -> null.
std::string num(float v, const char* fmt = "%.2f") {
    if (std::isnan(v)) return "null";
    char buf[32];
    snprintf(buf, sizeof(buf), fmt, v);
    return buf;
}

const char* boolStr(bool b) { return b ? "true" : "false"; }

std::string cabecera(const std::string& gateway, uint8_t node, const char* tipo) {
    return "{\"gateway\":\"" + jsonEscape(gateway) + "\",\"node\":" + std::to_string(node) +
           ",\"tipo\":\"" + tipo + "\"";
}

std::string radio(float rssi, float snr) {
    return ",\"rssi\":" + num(rssi, "%.1f") + ",\"snr\":" + num(snr, "%.1f");
}

// --- Lectura minima de JSON ---
// El servidor devuelve estructuras planas y fijas que controlamos nosotros; no
// se justifica un parser completo. Se busca "clave": dentro de [from, to).
// ponytail: no soporta objetos anidados ni comillas escapadas dentro de valores;
// cambiar a un parser real si el contrato crece.

size_t valuePos(const std::string& s, const char* key, size_t from, size_t to) {
    const std::string pat = std::string("\"") + key + "\"";
    size_t k = s.find(pat, from);
    if (k == std::string::npos || k >= to) return std::string::npos;
    size_t c = s.find(':', k + pat.size());
    if (c == std::string::npos || c >= to) return std::string::npos;
    size_t v = s.find_first_not_of(" \t\r\n", c + 1);
    return (v == std::string::npos || v >= to) ? std::string::npos : v;
}

bool findInt(const std::string& s, const char* key, size_t from, size_t to, long& out) {
    size_t v = valuePos(s, key, from, to);
    if (v == std::string::npos) return false;
    char* end = nullptr;
    out = std::strtol(s.c_str() + v, &end, 10);
    return end != s.c_str() + v;
}

bool findStr(const std::string& s, const char* key, size_t from, size_t to, std::string& out) {
    size_t v = valuePos(s, key, from, to);
    if (v == std::string::npos || s[v] != '"') return false;
    size_t q = s.find('"', v + 1);
    if (q == std::string::npos || q >= to) return false;
    out = s.substr(v + 1, q - v - 1);
    return true;
}

bool findBool(const std::string& s, const char* key, size_t from, size_t to, bool& out) {
    size_t v = valuePos(s, key, from, to);
    if (v == std::string::npos) return false;
    if (s.compare(v, 4, "true") == 0) { out = true; return true; }
    if (s.compare(v, 5, "false") == 0) { out = false; return true; }
    return false;
}

} // namespace

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back(c); }
        else if (static_cast<unsigned char>(c) >= 0x20) { out.push_back(c); }
    }
    return out;
}

std::string authJson(const std::string& deviceId, const std::string& apiKey) {
    return "{\"device_id\":\"" + jsonEscape(deviceId) + "\",\"api_key\":\"" + jsonEscape(apiKey) + "\"}";
}

bool tokenDeRespuesta(const net::Response& r, std::string& token) {
    std::string t;
    if (!r.ok || r.status != 200 || !findStr(r.body, "access_token", 0, r.body.size(), t) || t.empty()) {
        return false;
    }
    token = t;
    return true;
}

std::string fichajeJson(const std::string& gateway, uint8_t node, const std::string& uidHex) {
    return "{\"gateway\":\"" + jsonEscape(gateway) + "\",\"node\":" + std::to_string(node) +
           ",\"uid\":\"" + jsonEscape(uidHex) + "\"}";
}

uint8_t codigoFichaje(const net::Response& r) {
    const uint8_t reintentar = static_cast<uint8_t>(app::Code::REINTENTAR);
    if (!r.ok || r.status != 200) return reintentar;
    long code = -1;
    if (!findInt(r.body, "code", 0, r.body.size(), code)) return reintentar;
    return (code >= 0 && code <= reintentar) ? static_cast<uint8_t>(code) : reintentar;
}

bool uplinkJson(const std::string& gateway, uint8_t node, const uint8_t* payload, uint8_t len,
                float rssi, float snr, std::string& out) {
    switch (static_cast<app::Msg>(app::tipo(payload, len))) {
        case app::Msg::SENSORES: {
            app::Sensores s;
            if (!app::decodeSensores(payload, len, s)) return false;
            out = cabecera(gateway, node, "sensores") +
                  ",\"temperatura\":" + num(s.temperatura) +
                  ",\"humedad\":" + num(s.humedad) +
                  ",\"presion\":" + num(s.presion, "%.1f") +
                  ",\"luz\":" + num(s.lux, "%.0f") +
                  ",\"presencia\":" + boolStr(s.presencia) +
                  radio(rssi, snr) + "}";
            return true;
        }
        case app::Msg::ESTADO_RELES: {
            bool r1 = false, r2 = false;
            if (!app::decodeEstadoReles(payload, len, r1, r2)) return false;
            out = cabecera(gateway, node, "reles") +
                  ",\"rele1\":" + boolStr(r1) + ",\"rele2\":" + boolStr(r2) +
                  radio(rssi, snr) + "}";
            return true;
        }
        default:
            return false;
    }
}

std::string estadoJson(const std::string& gateway, uint8_t node, bool online) {
    return cabecera(gateway, node, "estado") + ",\"online\":" + boolStr(online) + "}";
}

std::vector<Command> parseCommands(const std::string& body, unsigned* descartados) {
    std::vector<Command> cmds;
    unsigned malos = 0;

    size_t lista = valuePos(body, "commands", 0, body.size());
    if (lista != std::string::npos && body[lista] == '[') {
        const size_t fin = body.find(']', lista);
        size_t cursor = lista + 1;
        while (fin != std::string::npos) {
            const size_t a = body.find('{', cursor);
            if (a == std::string::npos || a > fin) break;
            const size_t b = body.find('}', a);
            if (b == std::string::npos || b > fin) break;
            cursor = b + 1;

            Command c;
            long node = 0;
            std::string tipo;
            if (!findInt(body, "node", a, b, node) || node < 1 || node > 254 ||
                !findStr(body, "tipo", a, b, tipo)) {
                malos++;
                continue;
            }
            c.node = static_cast<uint8_t>(node);

            if (tipo == "rele") {
                long rele = 0;
                bool on = false;
                if (findInt(body, "rele", a, b, rele) && findBool(body, "on", a, b, on) &&
                    rele >= 1 && rele <= 2) {
                    c.len = app::encodeRele(c.data, static_cast<uint8_t>(rele), on);
                }
            } else if (tipo == "reset") {
                c.len = app::encodeReset(c.data);
            }

            if (c.len == 0) {
                malos++;
                continue;
            }
            cmds.push_back(c);
        }
    }

    if (descartados != nullptr) *descartados = malos;
    return cmds;
}

} // namespace bridge
