# sim_LoRaLAN

Simulador para la evaluación de LoRaLAN (capítulo 6 del TFG). Ejecuta la biblioteca real,
sin modificarla (`../LoRaLAN/src`), en un proceso con reloj virtual y canal simulado. La
fórmula del tiempo en el aire es la del gateway (`../../gatewayLoRaLAN/src/radio`).

Lo que verifica es **la lógica del protocolo**. El cifrado, el tiempo en el aire, el CAD y el
alcance reales se validan en hardware; las hipótesis del canal están en `spec/modelo_canal.md`.

Qué hace el código y por qué: `MANUAL.md`.

## Compilar y ejecutar

Desde bash en Windows, con el g++ de MSYS2:

```
export PATH=/c/msys64/mingw64/bin:$PATH
cmake -G "MinGW Makefiles" -S . -B build
cmake --build build
build/sim_tests --list
build/sim_tests --all --out results      veredictos y trazas
build/sim_tests --case U6 --seed 7       un caso, reproducible
build/sim_tests --family PUSH --verbose  con las trazas de la biblioteca
python experiments/sweep.py --seeds 30    experimentos, en results/perf (unos 2 min)
python experiments/plot.py                figuras, en results/perf/figuras
```

Código de salida 0: nada inesperado. Un fallo conocido sigue constando como Fallida.

## Estructura

| Ruta | Contenido |
|---|---|
| `shim/` | `Arduino.h`, `RadioLib.h` y `mbedtls/` falsos, detrás de los que se compila la biblioteca |
| `core/trace` | Eventos y consultas: sobre esto se hacen las aserciones |
| `core/channel` | Canal e inyección de fallos |
| `core/world` | Mundo limpio por caso y motor de 1 ms |
| `core/harness` | `CASE`, `PROP`, veredictos |
| `tests/` | Casos por familia; `instrument.cpp` comprueba el propio simulador |
| `experiments/` | Experimentos de rendimiento: programa, barridos y figuras |
| `spec/` | Hipótesis y documentos del plan de pruebas |

## Añadir un caso

```cpp
CASE("U6", "PUSH", "Evento con el primer ACK perdido", "2026-11-01",
     "Un nodo asociado, canal limpio", "Se descarta el 1.er ACK de bajada") {
    World w(ctx);                                 // 1. mundo limpio
    w.joinAll();
    w.trace().mark();                             //    la asociación no cuenta
    w.inject(Drop(MsgType::ACK, Dir::DOWN, 1));   // 2. estímulo
    w.node(0).queuePushEvent(DATA, LEN, true);
    w.runFor(5000);                               // 3. ejecución
    PROP("PRO-5", "El nodo retransmite el evento una vez");   // 4. lo esperado
    EXPECT_EQ(w.trace().count(Ev::TX, MsgType::PUSH_EVENT), 2, "PUSH_EVENT emitidos");
}
```

- Lo esperado se escribe y se fecha **antes** de ejecutar, a partir del requisito.
- Una regla de inyección que no actúa da **No concluyente** de forma automática.
- `KNOWN_FAIL("motivo")` tras un `PROP` marca un fallo conocido: sigue contando como Fallida.

## Limitación de la observabilidad

`LoRaLANNode` y `LoRaLANGateway` son `final` y `handleReceived()` devuelve `bool`, de modo que
el motivo de descarte se clasifica desde fuera en `World::receiveAt`: `parse` y `netId` con
funciones públicas, y `BAD_MIC` por el aviso que imprime la biblioteca, captado en el shim de
`Serial`. Si ese texto cambia, los descartes por MIC pasarían a otro motivo y fallaría SIM4.
