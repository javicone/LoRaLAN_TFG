# sim_LoRaLAN

Simulador de PC con las pruebas del capítulo 6 del TFG. Ejecuta la biblioteca LoRaLAN sin
modificarla (`../LoRaLAN/src`) en un solo proceso, con reloj virtual y un canal simulado
con pérdidas y colisiones.

Verifica **la lógica del protocolo**: el cifrado es falso y no hay CAD ni alcance reales,
así que eso se valida en hardware. El funcionamiento interno está en `MANUAL.md` y el plan
de pruebas en `spec/`.

## Qué prueba

- **87 casos funcionales** (`tests/`), cada uno con su veredicto: el propio simulador (SIM),
  formato de trama (F), JOIN (J), LEAVE (L), POLL (P), PUSH (U), ACK diferido (D), CMD (C),
  TIME_SYNC (T), silencio del gateway (S) y ataques (A).
- **Experimentos de rendimiento** (`experiments/`): VAL (contraste con la teoría), EXP1
  (pérdidas), EXP4 (colisiones sin sondeo) y EXP5 (colisiones con sondeo).

Los resultados de referencia están en `results/`: `casos.csv` con los veredictos y
`perf/` con los experimentos y sus figuras.

## Compilar y ejecutar

En Windows, con el g++ de MSYS2:

```bash
export PATH=/c/msys64/mingw64/bin:$PATH
cmake -G "MinGW Makefiles" -S . -B build
cmake --build build

build/sim_tests --all --out results       # todos los casos
build/sim_tests --case U6 --seed 7        # un caso, reproducible
python experiments/sweep.py --seeds 30    # experimentos, en results/perf
python experiments/plot.py                # figuras, en results/perf/figuras
```
