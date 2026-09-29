// Autocomprobacion del calculo de ToA y del control de ciclo de trabajo.
// No necesita ni radio ni Raspberry Pi: es logica pura.
//
//   g++ -std=c++17 -I src test_airtime.cpp src/radio/airtime.cpp -o test_airtime
//   ./test_airtime
//
// Los valores esperados NO se toman de ninguna fuente externa: se derivan a
// mano de la ecuacion de Semtech en los comentarios, para que cualquiera pueda
// rehacer la cuenta y comprobar que el codigo y el test coinciden por el
// motivo correcto.

#include "radio/airtime.h"

#include <cstdio>
#include <cmath>

static int g_fallos = 0;

static void check(bool ok, const char* que) {
    printf("  [%s] %s\n", ok ? "OK  " : "FALLO", que);
    if (!ok) g_fallos++;
}

static void checkNear(double got, double expected, double tolPct, const char* que) {
    const double err = std::fabs(got - expected) / expected * 100.0;
    printf("  [%s] %s  (obtenido %.3f ms, esperado %.3f ms)\n",
           err <= tolPct ? "OK  " : "FALLO", que, got, expected);
    if (err > tolPct) g_fallos++;
}

int main() {
    printf("== Tiempo en el aire (ecuacion de Semtech) ==\n");

    radio::LoRaParams p;   // SF7, 125 kHz, CR 4/5, preambulo 8, cabecera explicita, CRC
    p.spreadingFactor = 7;
    p.bandwidthHz = 125000;
    p.codingRate = 5;

    // Derivacion para SF7 / 125 kHz / CR 4-5 / preambulo 8 / PL = 11 B:
    //   T_sym      = 2^7 / 125000                     = 1,024 ms
    //   T_preamble = (8 + 4,25) * 1,024               = 12,544 ms
    //   n_payload  = 8 + ceil((8*11 - 28 + 28 + 16)/28) * 5
    //              = 8 + ceil(104/28) * 5 = 8 + 4*5   = 28 simbolos
    //   T_payload  = 28 * 1,024                       = 28,672 ms
    //   ToA        = 12,544 + 28,672                  = 41,216 ms
    checkNear(radio::timeOnAirMs(11, p), 41.216, 0.1, "SF7 125kHz 11 B (trama LoRaLAN minima)");

    //   n_payload  = 8 + ceil((8*75 - 28 + 28 + 16)/28) * 5
    //              = 8 + ceil(616/28) * 5 = 8 + 22*5  = 118 simbolos
    //   T_payload  = 118 * 1,024                      = 120,832 ms
    //   ToA        = 12,544 + 120,832                 = 133,376 ms
    checkNear(radio::timeOnAirMs(75, p), 133.376, 0.1, "SF7 125kHz 75 B (trama LoRaLAN maxima)");

    // El ToA debe crecer con el factor de ensanchado
    double prev = 0;
    bool creciente = true;
    for (int sf = 7; sf <= 12; ++sf) {
        p.spreadingFactor = sf;
        const double toa = radio::timeOnAirMs(75, p);
        printf("      SF%-2d 75 B -> %8.1f ms\n", sf, toa);
        if (toa <= prev) creciente = false;
        prev = toa;
    }
    check(creciente, "el ToA crece monotonamente con el SF");

    // A SF12 una trama larga se va muy por encima de los ~100 ms que asumia la
    // memoria: es justamente la limitacion documentada en el capitulo 5.
    p.spreadingFactor = 12;
    check(radio::timeOnAirMs(75, p) > 1000.0,
          "SF12 con 75 B supera 1 s (invalida la cota de ~100 ms)");

    // El simulador de PC usa un airtime fijo de 15 ms. Comprobamos de cuanto
    // es la discrepancia con el valor real, que es la limitacion declarada en
    // el capitulo 4.
    p.spreadingFactor = 7;
    const double real11 = radio::timeOnAirMs(11, p);
    printf("      simulador usa 15 ms; el real en SF7 es %.1f ms (factor %.1fx)\n",
           real11, real11 / 15.0);
    check(real11 > 15.0, "el airtime real supera al del simulador (sim optimista)");

    // Doblar el ancho de banda debe reducir el ToA a la mitad
    p.bandwidthHz = 125000;
    const double bw125 = radio::timeOnAirMs(75, p);
    p.bandwidthHz = 250000;
    const double bw250 = radio::timeOnAirMs(75, p);
    checkNear(bw250, bw125 / 2.0, 1.0, "doblar el BW reduce el ToA a la mitad");

    printf("\n== Ciclo de trabajo ==\n");
    {
        // 1 % en una ventana de 1 hora = 36 000 ms de presupuesto
        radio::DutyCycleBudget duty(0.01, 3600000);
        const uint32_t t0 = 1000;

        check(duty.canTransmit(100.0, t0), "cabe una trama de 100 ms al inicio");
        check(duty.usedMs(t0) == 0.0, "sin consumo antes de transmitir");

        for (int i = 0; i < 350; ++i) duty.record(100.0, t0);   // 35 000 ms

        checkNear(duty.usedMs(t0), 35000.0, 0.1, "consumo acumulado correcto");
        check(duty.canTransmit(900.0, t0),   "cabe justo lo que resta (900 ms)");
        check(!duty.canTransmit(1100.0, t0), "se rechaza lo que excede el presupuesto");
        check(duty.waitMs(1100.0, t0) > 0,   "waitMs indica espera cuando no cabe");

        const uint32_t t1 = t0 + 3600001;
        check(duty.usedMs(t1) == 0.0, "la ventana deslizante libera el presupuesto");
        check(duty.canTransmit(1100.0, t1), "vuelve a caber tras vencer la ventana");
    }

    printf("\n== Intervalo de sondeo frente al limite regulatorio ==\n");
    {
        // Cuanto puede sondear el gateway sin pasarse del 1 %.
        radio::LoRaParams sf7;
        const double toaPoll = radio::timeOnAirMs(11, sf7);
        const double presupuestoMs = 0.01 * 3600000.0;
        const double pollsMax = presupuestoMs / toaPoll;
        const double intervaloMin = 3600.0 / pollsMax;

        printf("      ToA de un POLL en SF7   : %.2f ms\n", toaPoll);
        printf("      POLLs maximos por hora  : %.0f\n", pollsMax);
        printf("      intervalo minimo legal  : %.2f s\n", intervaloMin);

        // HALLAZGO: el valor por defecto de la biblioteca, pollIntervalMs=2000,
        // consume el 2,06 % y por tanto NO es legal en una sub-banda del 1 %.
        const double consumo2s = (toaPoll * (3600.0 / 2.0)) / 3600000.0 * 100.0;
        printf("      con POLL cada 2 s       : %.2f %% de ciclo de trabajo\n", consumo2s);
        check(consumo2s > 1.0,
              "queda demostrado que el defecto de 2 s excede el 1 % (hallazgo)");

        // El valor que adopta este gateway si debe ser legal.
        const double consumoDefecto = (toaPoll * (3600.0 / 5.0)) / 3600000.0 * 100.0;
        printf("      con POLL cada 5 s       : %.2f %% de ciclo de trabajo\n", consumoDefecto);
        check(consumoDefecto < 1.0,
              "el defecto de 5 s de este gateway si cabe en el 1 %");
        check(5.0 > intervaloMin,
              "5 s queda por encima del minimo legal calculado");

        // En SF12 ni siquiera 5 s bastan: hay que espaciar mucho mas.
        radio::LoRaParams sf12; sf12.spreadingFactor = 12;
        const double toa12 = radio::timeOnAirMs(11, sf12);
        const double min12 = toa12 / (0.01 * 1000.0);
        printf("      en SF12 el minimo legal : %.1f s\n", min12);
        check(min12 > 5.0, "en SF12 hay que espaciar el sondeo mucho mas que en SF7");
    }

    printf("\n%s (%d fallos)\n", g_fallos == 0 ? "TODO OK" : "HAY FALLOS", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
