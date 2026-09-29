#include "airtime.h"

#include <cmath>
#include <algorithm>

namespace radio {

double timeOnAirMs(uint16_t payloadLen, const LoRaParams& p) {
    const int SF = p.spreadingFactor;
    const double BW = (double)p.bandwidthHz;

    // Duracion de simbolo
    const double tSym = (double)(1u << SF) / BW;   // segundos

    // Low data rate optimize: obligatorio cuando el simbolo supera 16 ms
    const int DE = (tSym > 0.016) ? 1 : 0;

    const int IH  = p.explicitHeader ? 0 : 1;
    const int CRC = p.crcEnabled ? 1 : 0;
    const int CR  = p.codingRate - 4;              // 5..8 -> 1..4

    const double tPreamble = (p.preambleSymbols + 4.25) * tSym;

    // Numero de simbolos de payload
    const double num = 8.0 * payloadLen - 4.0 * SF + 28.0 + 16.0 * CRC - 20.0 * IH;
    const double den = 4.0 * (SF - 2.0 * DE);
    double nPayload = 8.0;
    if (den > 0.0) {
        nPayload += std::max(std::ceil(num / den) * (CR + 4), 0.0);
    }

    const double tPayload = nPayload * tSym;
    return (tPreamble + tPayload) * 1000.0;        // a milisegundos
}

// ----------------------------------------------------------------------

DutyCycleBudget::DutyCycleBudget(double maxDutyCycle, uint32_t windowMs)
    : maxDuty_(maxDutyCycle), windowMs_(windowMs) {}

void DutyCycleBudget::prune(uint32_t nowMs) {
    // Resta con aritmetica de 32 bits sin signo: correcta aun con desbordamiento
    // del reloj monotono (ocurre cada ~49,7 dias).
    while (!history_.empty() && (uint32_t)(nowMs - history_.front().atMs) > windowMs_) {
        history_.pop_front();
    }
}

double DutyCycleBudget::usedMs(uint32_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    prune(nowMs);
    double total = 0.0;
    for (const auto& tx : history_) total += tx.airtimeMs;
    return total;
}

double DutyCycleBudget::usedFraction(uint32_t nowMs) {
    return usedMs(nowMs) / (double)windowMs_;
}

bool DutyCycleBudget::canTransmit(double airtimeMs, uint32_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    prune(nowMs);
    double total = 0.0;
    for (const auto& tx : history_) total += tx.airtimeMs;
    return (total + airtimeMs) <= (maxDuty_ * (double)windowMs_);
}

void DutyCycleBudget::record(double airtimeMs, uint32_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    prune(nowMs);
    history_.push_back({nowMs, airtimeMs});
}

uint32_t DutyCycleBudget::waitMs(double airtimeMs, uint32_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    prune(nowMs);

    const double budget = maxDuty_ * (double)windowMs_;
    double total = 0.0;
    for (const auto& tx : history_) total += tx.airtimeMs;

    if (total + airtimeMs <= budget) return 0;

    // Hay que esperar a que salgan de la ventana transmisiones suficientes
    // para hacer sitio.
    double needed = (total + airtimeMs) - budget;
    for (const auto& tx : history_) {
        needed -= tx.airtimeMs;
        if (needed <= 0.0) {
            const uint32_t age = (uint32_t)(nowMs - tx.atMs);
            return (age >= windowMs_) ? 0 : (windowMs_ - age);
        }
    }
    return windowMs_;   // ni vaciando la ventana cabe: la trama es demasiado larga
}

} // namespace radio
