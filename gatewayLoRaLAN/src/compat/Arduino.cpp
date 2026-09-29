#include "Arduino.h"

#include <mutex>

// --- Aleatorio ---------------------------------------------------------
// El protocolo llama a random() desde el hilo de radio unicamente, pero se
// protege igualmente: si en el futuro alguien lo llama desde el hilo de red,
// random_r sobre estado compartido seria una condicion de carrera.

namespace {

std::mutex g_randMutex;
char       g_randState[64];
random_data g_randData;
bool       g_randReady = false;

void ensureRandom() {
    if (g_randReady) return;
    memset(&g_randData, 0, sizeof(g_randData));
    // Semilla desde el reloj real: a diferencia del simulador, aqui interesa
    // que dos gateways que arranquen a la vez no generen el mismo DevNonce.
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    initstate_r((unsigned)(ts.tv_nsec ^ ts.tv_sec), g_randState, sizeof(g_randState), &g_randData);
    g_randReady = true;
}

} // namespace

long random(long maxExclusive) {
    if (maxExclusive <= 0) return 0;
    std::lock_guard<std::mutex> lock(g_randMutex);
    ensureRandom();
    int32_t r = 0;
    random_r(&g_randData, &r);
    return (long)((uint32_t)r % (unsigned long)maxExclusive);
}

long random(long minInclusive, long maxExclusive) {
    if (maxExclusive <= minInclusive) return minInclusive;
    return minInclusive + random(maxExclusive - minInclusive);
}

void randomSeed(unsigned long seed) {
    std::lock_guard<std::mutex> lock(g_randMutex);
    memset(&g_randData, 0, sizeof(g_randData));
    initstate_r((unsigned)seed, g_randState, sizeof(g_randState), &g_randData);
    g_randReady = true;
}

// --- Serial ------------------------------------------------------------

namespace {
bool g_serialEnabled = true;
std::mutex g_serialMutex;   // las trazas pueden salir de dos hilos
}

void setSerialEnabled(bool enabled) { g_serialEnabled = enabled; }

LinuxSerial Serial;

void LinuxSerial::prefix() {
    if (!atLineStart_) return;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tmv;
    localtime_r(&ts.tv_sec, &tmv);
    printf("[%02d:%02d:%02d.%03ld][LORA] ",
           tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ts.tv_nsec / 1000000L);
    atLineStart_ = false;
}

void LinuxSerial::newline() {
    if (!g_serialEnabled) return;
    fputs("\n", stdout);
    fflush(stdout);
    atLineStart_ = true;
}

void LinuxSerial::print(const char* s) {
    if (!g_serialEnabled) return;
    std::lock_guard<std::mutex> lock(g_serialMutex);
    prefix();
    fputs(s ? s : "(null)", stdout);
}

void LinuxSerial::print(int v)           { if (!g_serialEnabled) return; std::lock_guard<std::mutex> l(g_serialMutex); prefix(); printf("%d", v); }
void LinuxSerial::print(unsigned int v)  { if (!g_serialEnabled) return; std::lock_guard<std::mutex> l(g_serialMutex); prefix(); printf("%u", v); }
void LinuxSerial::print(long v)          { if (!g_serialEnabled) return; std::lock_guard<std::mutex> l(g_serialMutex); prefix(); printf("%ld", v); }
void LinuxSerial::print(unsigned long v) { if (!g_serialEnabled) return; std::lock_guard<std::mutex> l(g_serialMutex); prefix(); printf("%lu", v); }

void LinuxSerial::print(unsigned char v, int base) {
    if (!g_serialEnabled) return;
    std::lock_guard<std::mutex> lock(g_serialMutex);
    prefix();
    if (base == 16) printf("%02X", v);
    else            printf("%u", v);
}
