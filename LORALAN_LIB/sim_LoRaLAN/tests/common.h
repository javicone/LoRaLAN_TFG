#pragma once
// Utilidades compartidas por los casos.

#include <cstring>
#include <vector>

#include "world.h"

namespace {

const uint8_t kEvent[] = {'E', 'V', 'E', 'N', 'T', 'O'};
const uint8_t kEventLen = sizeof(kEvent);
const uint8_t kOther[] = {'O', 'T', 'R', 'O'};
const uint8_t kOtherLen = sizeof(kOther);

// Plazo de ACK y emisiones por defecto: 1200 ms y 3 emisiones, es decir, 3600 ms
// hasta agotar los intentos.
constexpr uint32_t kExhaustMs = 6000;

inline bool sameData(const Event* e, const uint8_t* data, size_t len) {
    return e != nullptr && e->data.size() == len && (len == 0 || std::memcmp(e->data.data(), data, len) == 0);
}

// Numero de eventos de ese tipo que llevan exactamente esos datos de aplicacion.
inline size_t countWithData(const Trace& t, Ev kind, const uint8_t* data, size_t len) {
    size_t n = 0;
    for (const Event* e : t.select(kind)) {
        if (sameData(e, data, len)) ++n;
    }
    return n;
}

// Desenlaces de PUSH notificados a la aplicacion del nodo con ese valor.
inline size_t countOutcome(const Trace& t, int delivered) {
    size_t n = 0;
    for (const Event* e : t.select(Ev::APP_PUSH_ACK)) {
        if (e->value == delivered) ++n;
    }
    return n;
}

inline bool containsBytes(const std::vector<uint8_t>& haystack, const uint8_t* needle, size_t len) {
    if (haystack.size() < len) return false;
    for (size_t i = 0; i + len <= haystack.size(); ++i) {
        if (std::memcmp(haystack.data() + i, needle, len) == 0) return true;
    }
    return false;
}

// Un evento posterior llega bien: la sesion sigue en pie.
inline bool sessionWorks(World& w, int node = 0) {
    const size_t before = countWithData(w.trace(), Ev::APP_PUSH, kOther, kOtherLen);
    if (!w.node(node).queuePushEvent(kOther, kOtherLen, true)) return false;
    w.runFor(1000);
    return countWithData(w.trace(), Ev::APP_PUSH, kOther, kOtherLen) == before + 1;
}

}  // namespace
