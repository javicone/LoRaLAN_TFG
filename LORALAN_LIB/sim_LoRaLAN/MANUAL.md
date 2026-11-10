# Manual de sim_LoRaLAN: qué hace el código y por qué

Estado: núcleo y batería funcional escritos y ejecutados. Lo que aquí se describe se ha ejecutado en el simulador, no en hardware. El `README.md` dice cómo usarlo; este manual explica cómo está hecho y por qué.

## 1. Qué problema resuelve

Para evaluar LoRaLAN hay que poner al protocolo en apuros: perder justo el segundo ACK, duplicar una trama, reinyectar una captura. Con placas eso no se puede hacer a voluntad, ni repetir después exactamente igual.

El simulador ejecuta **la biblioteca real, sin modificarla**, en un programa de PC donde controlamos todo lo que la rodea. Así cada prueba provoca una situación concreta y comprueba qué hizo el protocolo.

Lo que **no** demuestra: que la criptografía sea correcta (su AES y su CMAC son falsos), ni cómo se comportan el alcance, el tiempo en el aire real o el CAD real. Eso se valida en placas.

### Por qué no bastaba con `sim/`

| Problema de `sim/` | Consecuencia | Solución aquí |
|---|---|---|
| 24 fases encadenadas que heredan estado | Si falla la 7, la causa puede estar en la 3 | Un mundo nuevo por caso |
| Pérdida al azar (`g_lossPct = 30`) | La situación ocurre o no según la semilla | Fallos dirigidos: «el 1.er ACK de bajada» |
| Comprobaciones sobre contadores sueltos | Se pregunta «¿llegó?», no «¿cuántas veces?» | Aserciones sobre una traza completa |
| CAD instantáneo y perfecto | Las colisiones entre nodos eran imposibles | Ventana ciega configurable |

## 2. La idea central: cambiar lo que hay detrás de los `#include`

La biblioteca solo le pide tres cosas al mundo: la hora (`millis()`), números aleatorios (`random()`) y una radio. Si le damos versiones nuestras con los mismos nombres, se compila sin cambiar una línea y cree que está en una placa.

```text
 tests/*.cpp         los casos: provocan una situación y comprueban el resultado
      |
 core/world          crea el gateway y los nodos y hace avanzar el tiempo
      |          \
 LoRaLANGateway   core/channel   el aire: tiempos, colisiones y fallos inyectados
 LoRaLANNode          |
 (código REAL)    core/trace     registro de todo lo ocurrido
      |
 shim/               Arduino.h, RadioLib.h y mbedtls falsos
```

## 3. Los archivos, uno a uno

### `shim/`: las piezas falsas

- **`Arduino.h`**: `millis()` devuelve una variable (`sim_now_ms`), de modo que el tiempo solo avanza cuando el simulador quiere; por eso minutos simulados tardan milisegundos. `random()` usa una semilla de 32 bits fijos: misma semilla, mismos sorteos, también en otra plataforma. `Serial` imprime por consola solo con `--verbose`.
- **`RadioLib.h`**: una radio `SX1276` falsa que devuelve **los mismos códigos que la real** (`-15` libre, `-14` ocupado). Importa porque una radio falsa con códigos erróneos ya ocultó un defecto del CAD en el pasado.
- **`mbedtls/`**: sustitutos de AES y CMAC que dependen de la clave, sin valor criptográfico. Sirven para comprobar que se usa la clave correcta en cada trama, no que el cifrado sea seguro.

### `core/trace`: lo que ocurrió

Cada cosa que pasa se guarda como un `Event`: instante, tipo de evento, dispositivo, tipo de trama, secuencia y, si se descartó, el motivo.

**Por qué:** las pruebas preguntan a la traza en lugar de leer la consola. «¿Cuántos PUSH salieron?» es una consulta (`count(Ev::TX, MsgType::PUSH_EVENT)`), y la traza se guarda en CSV para ponerla en la memoria.

**`mark()`**: las consultas solo miran lo posterior a la marca. Así la asociación previa de un caso no cuenta en sus aserciones.

### `core/channel`: el aire

Mantiene la lista de tramas en el aire y decide qué pasa con ellas:

| Función | Qué hace | Por qué |
|---|---|---|
| `airtimeMs` | Calcula el tiempo en el aire con la fórmula de Semtech, la misma del gateway de la Pi | Los 15 ms fijos de `sim/` falseaban tiempos y solapes |
| `transmit` | Pone la trama en el aire y marca como colisionada cualquier otra que solape | Es lo que ocurre físicamente, sin efecto captura |
| `scan` | Es el CAD: el canal está ocupado si hay una trama detectable | La ventana ciega W permite estudiar nodos que no se detectan a tiempo |
| `popDue` | Entrega las tramas que han terminado | Aplica colisiones y fallos justo en la entrega |
| `linkLost` | Sortea la pérdida de cada enlace | Por enlace, porque un nodo puede oír al gateway sin que este lo oiga a él |

El sorteo de pérdidas tiene **su propio generador de números**. Si compartiera el de la biblioteca, cambiar la pérdida alteraría también sus esperas aleatorias, y dos configuraciones dejarían de ser comparables.

**Los fallos** se declaran en cada caso, no en el simulador, para que el caso documente qué situación provoca:

| Regla | Efecto |
|---|---|
| `Drop(tipo, sentido, n)` | La aparición n ocupa el aire, pero no llega a nadie |
| `Delay(tipo, sentido, n, ms)` | Llega con ese retraso |
| `Duplicate(tipo, sentido, n, ms)` | Llega dos veces con una sola emisión |
| `Reorder(tipo, sentido, n)` | Llega después de la siguiente trama entregada |
| `Corrupt(tipo, sentido, n, byte)` | Llega con ese byte alterado |

**Por qué `unfiredFaults`**: si una regla nunca actúa, el caso no ha probado lo que decía. Sin esto daría un falso «Superada»; con esto queda **No concluyente** automáticamente.

### `core/world`: el mundo de cada caso

Crea reloj, azar, canal, gateway y nodos nuevos en su constructor.

**Por qué objetos nuevos y no `begin()`**: `begin()` no reinicia todo (no toca `autoJoin_` ni `pollPayloadPending_`), y un caso podría pasar gracias a lo que dejó el anterior.

El motor es `tick()`, que representa un milisegundo:

```text
1. El gateway y cada nodo ejecutan process(), salvo los que están transmitiendo
2. El canal entrega las tramas que han terminado
3. Se anotan en la traza los cambios de estado (asociado, de baja...)
4. El reloj avanza 1 ms
```

- **Paso fijo de 1 ms**: la biblioteca consulta `millis()` en cada `process()`, igual que en el `loop()` de Arduino, así que no hay que adaptarla.
- **Saltarse a quien transmite**: en la placa `transmit()` bloquea durante el tiempo en el aire. Esto reproduce ese comportamiento half-duplex.
- **`g_world` global**: los callbacks de la biblioteca son punteros a función sin contexto, así que necesitan un puntero global para llegar al mundo. El precio es que solo puede existir un mundo a la vez, cosa que aquí no molesta.
- **`current_`**: algunos callbacks no dicen qué nodo los dispara (`onPushAck`), así que el mundo recuerda quién se está ejecutando.

**`receiveAt`** entrega la trama al receptor y anota el motivo si la rechaza. `handleReceived()` solo devuelve `bool` y las clases son `final`, por lo que el motivo se deduce desde fuera:

| Motivo | Cómo se sabe |
|---|---|
| `MALFORMED` | `LoRaLANPacket::parse` falla |
| `WRONG_NET` | El `netId` es otro |
| `NOT_ADDRESSED` | Va dirigida a otro dispositivo |
| `BAD_MIC` | La biblioteca imprime «MIC invalido» y el shim de `Serial` lo capta |
| `NO_KEY` | No hay sesión con el emisor |
| `REJECTED_BY_ROLE` | Ninguno de los anteriores: la lógica del rol la rechazó |

Es una deducción, no una señal de la biblioteca: si cambia el texto del aviso, SIM4 falla y lo delata.

**`joinAll`**: si no consigue asociar a los nodos, el caso queda No concluyente, porque no se dio su precondición.

### `core/harness`: fichas y veredictos

- `CASE(...)` declara la ficha: identificador, familia, título, fecha de lo esperado, precondiciones y estímulo. Se registra sola al arrancar, sin listas que mantener a mano.
- `PROP("PRO-5", "...")` abre una propiedad: lo que se espera, ligado a un requisito. Las siguientes `EXPECT_*` pertenecen a ella.
- El veredicto es **por propiedad**: en C3, PRO-5 y PRO-4 tienen cada una su veredicto, y juntarlas ocultaría una de las dos cosas.

| Veredicto | Cuándo |
|---|---|
| Superada | Se produjo el estímulo y se cumplen todas las aserciones |
| Fallida | Se produjo el estímulo y alguna aserción no se cumple |
| No concluyente | Una regla no actuó, no se dio una precondición o la propiedad no tiene aserciones |

### `tests/`

- **`main.cpp`**: ejecuta los casos y escribe `casos.csv` y las trazas con `--out`. `KNOWN_FAIL` mantiene el veredicto de Fallida, pero no rompe el código de salida. Así la batería sirve de regresión sin esconder fallos. Si un fallo conocido deja de fallar, también avisa.
- **`format.cpp`, `join.cpp`, `leave.cpp`, `poll.cpp`, `push.cpp`, `ack.cpp`, `cmd.cpp`, `timesync.cpp`, `recovery.cpp`, `security.cpp`**: F1–F6 y los 76 casos del protocolo. `common.h` reúne sus utilidades.
- **`instrument.cpp`** (SIM1 a SIM5): prueban el propio simulador. Si el instrumento mide mal, nada de lo demás vale.

## 4. Ejemplo real: U6, un evento con el ACK perdido

El caso descarta el primer ACK de bajada y espera que el nodo repita el evento, que la aplicación lo reciba una sola vez y que el nodo acabe sabiendo que llegó. Esta es su traza real, resumida:

| t (ms) | Evento | Qué significa |
|---|---|---|
| 1152 | `CAD_FREE`, `TX PUSH_EVENT` (flags 1) | El nodo escucha, ve el canal libre y emite 17 B |
| 1204 | `APP_PUSH` | 52 ms después (tiempo en el aire) llega a la aplicación del gateway |
| 1204 | `TX ACK`, `FAULT DROP` | El gateway confirma y la regla descarta ese ACK |
| 2352 | `TX PUSH_EVENT` (flags 3) | Pasados 1200 ms sin ACK, el nodo reintenta; flags 3 añade `IS_RETRY` |
| 2404 | `TX ACK` | El gateway reconfirma; no hay un segundo `APP_PUSH`, porque detecta el duplicado |
| 2446 | `RX_OK ACK` | El nodo recibe la confirmación |
| 2447 | `APP_PUSH_ACK` valor 1 | Su aplicación sabe que el evento se entregó |

La traza muestra además un comportamiento de la biblioteca: en 2352 el nodo reintenta **sin `CAD_FREE` previo**, porque los reintentos no escuchan el canal. Será lo que examine el caso U15.

## 5. Cómo se añade un caso

1. Elegir el requisito y la propiedad del capítulo 3.
2. Escribir lo esperado **antes de ejecutar**, y fecharlo. Si se escribe después, se acaba dando por bueno lo que hace el código.
3. Seguir el molde del `README.md`: mundo limpio, estímulo, ejecución y aserciones.
4. Si falla, no se cambia lo esperado para que pase: se corrige el código, se revisa el requisito con justificación o se documenta como limitación.

## 6. Decisiones y sus costes

| Decisión | A cambio de |
|---|---|
| Sin frameworks de pruebas | Unas macros propias; basta con lo que necesitamos |
| CMake | Una dependencia de compilación, a cambio de un proyecto con dos ejecutables: pruebas y experimentos |
| No tocar la biblioteca | El motivo de descarte se deduce desde fuera |
| Sin ráfagas de pérdida, deriva de reloj ni efecto captura | Límites del modelo, declarados en `spec/modelo_canal.md`; se añadirían solo si cambiaran una conclusión |

## 7. Lo que falta

Pendiente: repetir los experimentos sobre la versión congelada. Las mutaciones se descartaron.
