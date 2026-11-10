# gatewayLoRaLAN

Gateway LoRaLAN para la Raspberry Pi, con un SX1276 por SPI. Hace de puente entre la
radio y el servidor:

```
nodos ESP32 --LoRaLAN--> [gatewayLoRaLAN] --HTTP/JSON--> FastAPI (Server/)
```

- Recibe de los nodos los fichajes, los sensores y el estado de los relés, y los sube al
  servidor.
- Recoge del servidor las órdenes pendientes (relés, reinicio) y las envía por radio.
- La respuesta del servidor a cada fichaje vuelve al nodo dentro del ACK de radio.
- Calcula el tiempo en el aire y no arranca si el sondeo configurado supera el ciclo de
  trabajo permitido.

## Ficheros

| Fichero | Qué contiene |
|---|---|
| `src/main.cpp` | Programa principal: radio, hilos y colas |
| `src/config.h` | Pines, radio, tiempos y servidor (valores por defecto) |
| `src/bridge.*` | Traducción entre mensajes de radio y JSON del servidor |
| `src/net/http_client.*` | Cliente HTTP mínimo (sin TLS) |
| `src/radio/airtime.*` | Tiempo en el aire (fórmula de Semtech) y ciclo de trabajo |
| `src/compat/Arduino.*` | `Arduino.h` para Linux, para compilar la biblioteca sin tocarla |
| `gateway.env` | Variables de este despliegue |
| `test_*.cpp` | Pruebas sin hardware; `test_radio` prueba solo la radio |

Los mensajes nodo ↔ gateway se definen en `../Device_ESP32_LoRaLAN/AppProtocol.h`,
compartido con el nodo.

## Cómo funciona

El programa usa tres hilos para que una petición HTTP lenta nunca retrase un ACK de radio
(el nodo lo espera 1200 ms):

- **Radio**: atiende la radio y la biblioteca. Nunca hace E/S de red; sus avisos solo
  encolan.
- **Fichajes**: envía cada fichaje al servidor (`POST /gateway/fichaje`, timeout 800 ms).
- **Red**: sube la telemetría, pide órdenes cada 5 s y envía un latido cada 30 s.

**Fichaje**: al llegar un evento del nodo, el gateway no lo confirma enseguida; espera la
respuesta del servidor y la envía en el ACK. Si no llega en 1000 ms, responde
`REINTENTAR` y el nodo lo reenvía más tarde. Códigos: `0 OK`, `1 SIN_CLASE`,
`2 DENEGADO`, `3 DESCONOCIDO` y `4 REINTENTAR`.

Con el servidor se autentica con su api_key (`POST /auth/device`) y renueva el token
cuando caduca. El formato de cada JSON está en la cabecera de `src/bridge.h`.

## Configuración

Variables de entorno (las carga `gateway.env`):

| Variable | Por defecto | Uso |
|---|---|---|
| `LORALAN_SERVER_HOST` | 127.0.0.1 | Servidor FastAPI |
| `LORALAN_SERVER_PORT` | 8000 | |
| `LORALAN_API_KEY` | vacío | api_key del gateway en la tabla `nodos` |
| `LORALAN_GATEWAY_NAME` | gw-rpi-01 | `device_id` del gateway en `nodos` |
| `LORALAN_POLL_MS` | 5000 | Intervalo de sondeo |
| `LORALAN_TIMESYNC_MS` | 30000 | Intervalo de sincronización horaria |

Radio: 869,525 MHz, SF7, 125 kHz, CR 4/5, sync word 0x12, 14 dBm. La `APP_KEY` está en
`main.cpp` y debe coincidir con la de los nodos.

Conexionado (numeración BCM): DIO0 → 16, RST → 13, CS → CE0, MOSI/MISO/SCK → SPI0.
Alimentación a **3,3 V**.

## Compilar y ejecutar

Necesita `cmake`, `liblgpio-dev`, `libmbedtls-dev` (sin ella no hay cifrado y CMake se
niega a configurar) y RadioLib en `~/RadioLib`.

```bash
mkdir build && cd build
cmake -DRADIOLIB_DIR=$HOME/RadioLib -DLORALAN_DIR=$PWD/../../LORALAN_LIB/LoRaLAN/src ..
make -j4 gatewayLoRaLAN
make test_airtime test_app_protocol test_bridge   # pruebas sin hardware
cd .. && set -a; . ./gateway.env; set +a
sudo -E ./build/gatewayLoRaLAN
```

Puesta en marcha completa en [`../arranque.md`](../arranque.md).
