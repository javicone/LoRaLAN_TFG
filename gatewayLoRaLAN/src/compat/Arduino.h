#pragma once
// Capa de compatibilidad Arduino para Linux (Raspberry Pi).
//
// La biblioteca LoRaLAN incluye <Arduino.h>. En la Pi no hay Arduino, asi que
// este fichero proporciona lo minimo que la biblioteca usa, sobre primitivas
// POSIX. Es el mismo enfoque que el simulador de PC: la biblioteca del
// protocolo se compila SIN MODIFICAR y solo cambia lo que hay detras de sus
// includes. Aqui, a diferencia del simulador, el reloj y el aleatorio son
// reales.

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

// --- Reloj monotono en milisegundos ---
// Monotono a proposito: no salta si se ajusta la hora del sistema (NTP), lo
// que rompería los temporizadores de reintento del protocolo.
inline uint32_t millis() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

inline uint32_t micros() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000ULL);
}

inline void delay(unsigned long ms) {
    struct timespec req;
    req.tv_sec  = (time_t)(ms / 1000);
    req.tv_nsec = (long)((ms % 1000) * 1000000L);
    nanosleep(&req, nullptr);
}

inline void delayMicroseconds(unsigned int us) {
    struct timespec req;
    req.tv_sec  = us / 1000000;
    req.tv_nsec = (long)(us % 1000000) * 1000L;
    nanosleep(&req, nullptr);
}

// --- Aleatorio ---
// El protocolo lo usa para el DevNonce del JOIN y para el jitter del backoff
// de CAD. Se emplea random_r con estado propio para no interferir con otras
// partes del proceso.
long random(long maxExclusive);
long random(long minInclusive, long maxExclusive);
void randomSeed(unsigned long seed);

// --- F() / __FlashStringHelper ---
// En AVR mueve literales a flash. En Linux no hay tal distincion: es un cast
// de ida y vuelta.
class __FlashStringHelper;
#define F(str) reinterpret_cast<const __FlashStringHelper*>(str)

// --- Serial ---
// Las trazas de la biblioteca van a stdout con marca de tiempo. El nivel se
// controla con setSerialEnabled() para poder silenciarlas en produccion.
class LinuxSerial {
public:
    void print(const __FlashStringHelper* s) { print(reinterpret_cast<const char*>(s)); }
    void println(const __FlashStringHelper* s) { println(reinterpret_cast<const char*>(s)); }

    void print(const char* s);
    void print(int v);
    void print(unsigned int v);
    void print(long v);
    void print(unsigned long v);
    void print(unsigned char v, int base);

    void println(const char* s) { print(s); newline(); }
    void println(int v) { print(v); newline(); }
    void println(unsigned int v) { print(v); newline(); }
    void println(long v) { print(v); newline(); }
    void println(unsigned long v) { print(v); newline(); }
    void println() { newline(); }

private:
    bool atLineStart_ = true;
    void prefix();
    void newline();
};

extern LinuxSerial Serial;

// Silencia o habilita las trazas de la biblioteca.
void setSerialEnabled(bool enabled);
