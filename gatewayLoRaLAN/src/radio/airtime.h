#pragma once
// Calculo de tiempo en el aire (ToA) y control de ciclo de trabajo.
//
// El simulador de PC usa un airtime fijo de 15 ms, lo que basta para validar
// la logica del protocolo pero no para respetar la regulacion. Aqui se
// implementa la ecuacion real de Semtech (SX1276 datasheet, seccion 4.1.1.7),
// que es lo que permite al gateway saber cuanto tiempo de canal ha gastado.

#include <stdint.h>
#include <deque>
#include <mutex>

namespace radio {

struct LoRaParams {
    uint8_t  spreadingFactor = 7;      // 6..12
    uint32_t bandwidthHz     = 125000; // 125000, 250000, 500000
    uint8_t  codingRate      = 5;      // 5..8  (4/5 .. 4/8)
    uint8_t  preambleSymbols = 8;
    bool     explicitHeader  = true;
    bool     crcEnabled      = true;
};

// Tiempo en el aire de una trama de payloadLen bytes, en milisegundos.
//
//   T_sym      = 2^SF / BW
//   T_preamble = (n_pre + 4.25) * T_sym
//   n_payload  = 8 + max(ceil((8*PL - 4*SF + 28 + 16*CRC - 20*IH)
//                             / (4*(SF - 2*DE))) * CR, 0)
//   ToA        = T_preamble + n_payload * T_sym
//
// DE (low data rate optimize) se activa cuando T_sym > 16 ms, que en la
// practica significa SF11 y SF12 a 125 kHz.
double timeOnAirMs(uint16_t payloadLen, const LoRaParams& p);

// Contabiliza el tiempo en el aire consumido en una ventana deslizante y
// decide si una nueva transmision cabe dentro del limite regulatorio.
//
// En EU868 el limite depende de la sub-banda: 1 % en la mayoria y 10 % en
// 869,4-869,65 MHz (ETSI EN 300 220). El limite se pasa como fraccion.
class DutyCycleBudget {
public:
    // maxDutyCycle: 0.01 para el 1 %, 0.10 para el 10 %.
    // windowMs: ventana de contabilidad (por defecto una hora).
    explicit DutyCycleBudget(double maxDutyCycle = 0.01, uint32_t windowMs = 3600000);

    // ¿Cabe una transmision de airtimeMs sin exceder el limite?
    bool canTransmit(double airtimeMs, uint32_t nowMs);

    // Registra una transmision ya realizada.
    void record(double airtimeMs, uint32_t nowMs);

    // Tiempo en el aire acumulado en la ventana, en ms.
    double usedMs(uint32_t nowMs);

    // Fraccion de ciclo de trabajo consumida (0..1).
    double usedFraction(uint32_t nowMs);

    // Milisegundos que faltan para que quepa airtimeMs. 0 si cabe ya.
    uint32_t waitMs(double airtimeMs, uint32_t nowMs);

private:
    struct Tx { uint32_t atMs; double airtimeMs; };

    void prune(uint32_t nowMs);

    double          maxDuty_;
    uint32_t        windowMs_;
    std::deque<Tx>  history_;
    std::mutex      mutex_;
};

} // namespace radio
