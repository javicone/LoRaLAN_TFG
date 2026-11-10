# gatewayLoRaLAN — gateway LoRaLAN en la Raspberry Pi

## Qué hace

Es el puente entre la radio y el servidor. Corre como un proceso normal de Linux
en la Raspberry Pi, con un módulo SX1276 conectado por SPI:

```
nodos ESP32 --LoRaLAN (868 MHz)--> [gatewayLoRaLAN] --HTTP/JSON--> FastAPI (Server/)
```

- **Hacia arriba**: recibe por radio los fichajes (UID de la tarjeta NFC), los
  sensores y el estado de los relés de cada nodo, y los sube al servidor en JSON.
- **Hacia abajo**: pregunta al servidor si hay órdenes pendientes (encender un relé,
  reiniciar un nodo) y las transmite por radio.
- **Fichaje**: la respuesta del servidor (OK, sin clase, denegado…) viaja de vuelta
  al nodo **dentro del ACK** de la trama del fichaje.
- **Regulación**: calcula el tiempo en el aire real de cada trama y no deja
  transmitir si se supera el ciclo de trabajo legal de la banda. Si la configuración
  de sondeo no es legal, se niega a arrancar.

## Ficheros

| Fichero | Qué contiene |
|---|---|
| `src/main.cpp` | Programa principal: arranque, radio, los tres hilos y las colas |
| `src/config.h` | Pines, parámetros de radio, tiempos, rutas del servidor (valores por defecto) |
| `src/bridge.h/.cpp` | Traducción mensajes LoRaLAN ↔ JSON del backend. Sin E/S: se prueba en PC |
| `src/net/http_client.*` | Cliente HTTP/1.1 mínimo sobre sockets POSIX (POST y GET) |
| `src/radio/airtime.*` | Ecuación de Time-on-Air de Semtech y contador de ciclo de trabajo |
| `src/compat/Arduino.*` | Capa que imita `Arduino.h` sobre Linux, para compilar LoRaLAN sin tocarla |
| `CMakeLists.txt` | Compilación: gateway + tests |
| `gateway.env` | Variables de entorno de este despliegue (servidor, api_key, nombre) |
| `test_airtime.cpp` | Autocomprobación del cálculo de ToA y del ciclo de trabajo |
| `test_app_protocol.cpp` | Autocomprobación del formato de mensajes (`AppProtocol.h`) |
| `test_bridge.cpp` | Autocomprobación del JSON de ida y vuelta con el servidor |
| `test_radio.cpp` | Prueba de la radio sola (sin LoRaLAN): transmite o escucha texto |

`AppProtocol.h` **no está aquí**: se incluye desde `../Device_ESP32_LoRaLAN/` para que
nodo y gateway compartan una sola definición de los mensajes.

## Cómo está programado

### 1. La biblioteca LoRaLAN se compila sin modificar

La biblioteca del protocolo (en este repo, `LORALAN_LIB/LoRaLAN/src`) está escrita
para Arduino y hace `#include <Arduino.h>`. En vez de modificarla, `src/compat/`
proporciona un `Arduino.h` propio con lo que la biblioteca usa:

- `millis()` / `micros()` con `CLOCK_MONOTONIC` (no salta si NTP cambia la hora).
- `delay()` con `nanosleep`.
- `random()` con `random_r` y semilla del reloj real (para el DevNonce del JOIN).
- `F()` como un simple cast, y un `Serial` que escribe en stdout con marca de tiempo.

El `CMakeLists.txt` pone `src/compat` **el primero** en la ruta de includes para que
`<Arduino.h>` se resuelva ahí. La ruta de la biblioteca es `LORALAN_DIR`: por defecto
`../../IMPLEMENTACIONMIA/libraries/LoRaLAN/src` (la carpeta de trabajo del autor); con
solo este repo se pasa `-DLORALAN_DIR=<repo>/LORALAN_LIB/LoRaLAN/src`. La radio la maneja RadioLib con su HAL oficial para
Raspberry Pi (`PiHal`, sobre `lgpio`). También exige `libmbedtls-dev`: sin ella la
biblioteca compilaría pero **sin cifrar** el payload, y por eso CMake aborta.

### 2. Tres hilos

La separación es por corrección, no por rendimiento: el nodo espera el ACK 1200 ms
y una petición HTTP lenta dentro del callback de radio haría que lo diera por perdido.

```
            ┌──────────────── hilo de RADIO (main) ────────────────┐
  antena -> │ gateway.process(), handleReceived(), ackPush(),     │
            │ sendCommand(). Nunca toca la red.                   │
            └──┬──────────────┬───────────────▲──────────────▲───┘
    g_fichajes │   g_uplinks  │   g_resultados│   g_comandos │
               ▼              ▼               │              │
  ┌── hilo FICHAJES ──┐  ┌──────── hilo RED ─────────────────┴──┐
  │ POST /fichaje     │  │ POST /uplink (telemetría)            │
  │ timeout 800 ms    │  │ GET /commands cada 5 s               │
  └───────────────────┘  │ POST /status cada 30 s               │
                         └──────────────────────────────────────┘
```

- **Radio** (`main()`): bucle cerrado con `delay(1)`. Mira el flag `RX_DONE` del chip
  por SPI (no usa interrupción), lee la trama y se la pasa a `gateway.handleReceived()`.
  Los callbacks de LoRaLAN (`onPushEvent`, `onPollResponse`, `onNodeJoined`,
  `onNodeLeft`) **solo encolan**. Después atiende los resultados de fichajes y los
  comandos pendientes.
- **Fichajes** (`fichajeThread`): saca un fichaje de la cola, hace un POST con timeout
  de 800 ms y deja el código de respuesta en `g_resultados`. Va en un hilo aparte para
  que un envío de telemetría lento no le quite el plazo.
- **Red** (`networkThread`): vacía la cola de telemetría, pide comandos cada
  `COMMAND_POLL_MS` y manda el latido de estado cada `STATUS_REPORT_MS`.

Las colas son una plantilla `Cola<T>` (deque + mutex + condition_variable). `push()`
nunca bloquea: si la cola está llena descarta el elemento **más antiguo** y el número
de descartes se informa en el campo `dropped` del latido.

### 3. Fichaje con ACK diferido

El gateway arranca con `cfg.deferredPushAck = true`: al llegar un PUSH, LoRaLAN no
contesta solo; espera a que el programa llame a `ackPush(node, seq, código)`.

1. `onPushEvent` decodifica el UID (`app::decodeFichaje`), lo apunta en
   `g_pendientes` con la hora de llegada y lo encola para el hilo de fichajes.
2. El hilo de fichajes hace `POST /gateway/fichaje` y obtiene `{"code": n}`.
   `bridge::codigoFichaje()` convierte cualquier cosa rara (error de red, no-200,
   JSON sin código) en `REINTENTAR`.
3. `atenderFichajes()` (hilo de radio) envía el ACK con ese código.
4. Si pasan `FICHAJE_DEADLINE_MS` (1000 ms) sin respuesta, la radio contesta sola
   `REINTENTAR` y el nodo lo reenviará más tarde. Una respuesta que llegue tarde se
   descarta.

Códigos (`AppProtocol.h` → `app::Code`): `0 OK`, `1 SIN_CLASE`, `2 DENEGADO`,
`3 DESCONOCIDO` (definitivos) y `4 REINTENTAR` (temporal).

### 4. Comandos y ciclo de trabajo

`atenderComandos()` coge un comando, calcula su ToA con `radio::timeOnAirMs()` y
pregunta a `DutyCycleBudget::canTransmit()` si cabe en el presupuesto de la última
hora (ventana deslizante con un `deque` de transmisiones). Si no cabe, espera; si el
nodo no está asociado, lo descarta.

Al arrancar, `main()` calcula cuánto ciclo de trabajo consume el POLL periódico
(`LORALAN_POLL_MS`) y **sale con error** si supera `DUTY_CYCLE_MAX`. Con SF7 y un
POLL cada 5 s el consumo es 0,82 %; la frecuencia 869,525 MHz permite el 10 %.

> Nota: los ACK no se descuentan del presupuesto (marcado con `ponytail:` en el
> código). Solo importa si el tráfico de fichajes crece mucho.

### 5. Autenticación con el servidor

`ClienteServidor` envuelve `net::HttpClient`. En la primera petición pide un JWT a
`POST /auth/device` con `LORALAN_GATEWAY_NAME` + `LORALAN_API_KEY`, y lo renueva cuando
el servidor contesta 401 (el token caduca a las 24 h). Como mucho un intento cada 10 s,
para no inundar el servidor si la api_key está mal. Cada hilo tiene su propio cliente.

### 6. bridge: el contrato con el servidor

`bridge.h` documenta en su cabecera el formato exacto de cada JSON. El JSON se
**genera** concatenando strings (con `jsonEscape`) y se **lee** con un buscador de
`"clave":` muy simple, suficiente porque las respuestas del servidor son planas y las
controlamos nosotros. Un sensor sin lectura (NAN) se envía como `null`.

`parseCommands()` convierte `{"commands":[{"node":16,"tipo":"rele","rele":1,"on":true}]}`
en tramas binarias listas para `sendCommand()`; los mal formados se cuentan y descartan.

### 7. Cliente HTTP

Un socket TCP por petición, HTTP/1.1 escrito a mano con `Connection: close`: envía la
petición, lee hasta que el servidor cierra y separa estado y cuerpo por la línea en
blanco. Timeouts con `SO_RCVTIMEO`/`SO_SNDTIMEO`. **Sin TLS**: por eso el servidor debe ir
por HTTP en la misma red (en el prototipo, en la propia Pi).

## Configuración

Los valores de `src/config.h` son los por defecto; en ejecución se cambian con
variables de entorno (las carga `gateway.env`):

| Variable | Defecto | Uso |
|---|---|---|
| `LORALAN_SERVER_HOST` | 127.0.0.1 | Servidor FastAPI |
| `LORALAN_SERVER_PORT` | 8000 | |
| `LORALAN_API_KEY` | vacío | api_key del gateway en la tabla `nodos` |
| `LORALAN_GATEWAY_NAME` | gw-rpi-01 | `device_id` del gateway en `nodos` |
| `LORALAN_POLL_MS` | 5000 | Intervalo de sondeo a los nodos |
| `LORALAN_TIMESYNC_MS` | 30000 | Intervalo de sincronización horaria |

La `APP_KEY` de 16 bytes está compilada en `main.cpp` y debe ser idéntica a la de los
nodos. Radio: 869,525 MHz, SF7, BW 125 kHz, CR 4/5, sync word 0x12, 14 dBm.

Conexionado (BCM): DIO0 → 16, RST → 13, CS → CE0 (lo mueve el driver spidev),
MOSI/MISO/SCK → SPI0. Alimentación a **3,3 V**.

## Compilar, probar y ejecutar

```bash
mkdir build && cd build
cmake -DRADIOLIB_DIR=$HOME/RadioLib ..
make -j4 gatewayLoRaLAN
make test_airtime test_app_protocol test_bridge   # tests sin hardware
cd .. && set -a; . ./gateway.env; set +a
sudo -E ./build/gatewayLoRaLAN
```

`sudo ./build/test_radio` (o `test_radio rx`) sirve para comprobar solo la capa
física antes de meter LoRaLAN por medio. Guía completa de puesta en marcha en
`../arranque.md`.
