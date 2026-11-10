#pragma once
// Shim de Arduino para compilar LoRaLAN en PC (simulador).
// Reloj virtual: el main avanza sim_now_ms; delay() tambien lo avanza.

#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>

extern uint32_t sim_now_ms;         // reloj virtual (ms)
extern const char* sim_serial_tag;  // dispositivo activo para las trazas ("GW", "NODO", ...)
extern uint32_t sim_rand_state;       // 32 bits fijos: misma traza en cualquier plataforma
extern bool sim_serial_quiet;       // true = descarta la salida de Serial
extern bool sim_serial_mic_error;   // la biblioteca acaba de avisar de un MIC invalido

inline uint32_t millis() { return sim_now_ms; }
inline void delay(unsigned long ms) { sim_now_ms += (uint32_t)ms; }

// ponytail: LCG deterministico, mismas trazas en cada ejecucion
inline long random(long maxExclusive) {
    sim_rand_state = sim_rand_state * 1103515245u + 12345u;
    if (maxExclusive <= 0) return 0;
    return (long)((sim_rand_state >> 16) % (uint32_t)maxExclusive);
}
inline long random(long minInclusive, long maxExclusive) {
    return minInclusive + random(maxExclusive - minInclusive);
}
inline void randomSeed(unsigned long seed) { sim_rand_state = (uint32_t)seed; }

// F() / __FlashStringHelper: en PC no hay flash, es un cast de ida y vuelta.
class __FlashStringHelper;
#define F(str) reinterpret_cast<const __FlashStringHelper*>(str)

// Serial con prefijo [tiempo][dispositivo] al inicio de cada linea.
class SimSerial {
public:
    void print(const __FlashStringHelper* s) { print(reinterpret_cast<const char*>(s)); }
    void println(const __FlashStringHelper* s) { println(reinterpret_cast<const char*>(s)); }
    // El motivo de descarte BAD_MIC solo es observable por este aviso: las clases
    // de la biblioteca son final y handleReceived() devuelve un simple bool.
    void print(const char* s) { if (s && strstr(s, "MIC invalido")) sim_serial_mic_error = true; if (sim_serial_quiet) return; prefix(); fputs(s ? s : "(null)", stdout); }
    void println(const char* s) { print(s); newline(); }
    void print(int v) { if (sim_serial_quiet) return; prefix(); printf("%d", v); }
    void print(unsigned int v) { if (sim_serial_quiet) return; prefix(); printf("%u", v); }
    void print(long v) { if (sim_serial_quiet) return; prefix(); printf("%ld", v); }
    void print(unsigned long v) { if (sim_serial_quiet) return; prefix(); printf("%lu", v); }
    void println(int v) { print(v); newline(); }
    void println(unsigned int v) { print(v); newline(); }
    void println(long v) { print(v); newline(); }
    void println(unsigned long v) { print(v); newline(); }
    void println() { newline(); }
private:
    bool atLineStart_ = true;
    void prefix() {
        if (atLineStart_) {
            printf("[%7lu ms][%-4s] ", (unsigned long)sim_now_ms, sim_serial_tag);
            atLineStart_ = false;
        }
    }
    void newline() { if (sim_serial_quiet) return; fputs("\n", stdout); atLineStart_ = true; }
};

extern SimSerial Serial;
