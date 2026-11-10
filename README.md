# Control de asistencia sobre LoRaLAN

Sistema de control de asistencia por tarjeta NFC para aulas, parte del TFG de Telemática
de Javier Conejero (Universidad de Extremadura). El nodo de cada aula lee la tarjeta del
profesor y la envía por radio, con el protocolo propio **LoRaLAN**, a un gateway en una
Raspberry Pi; el gateway la pasa a un servidor FastAPI, en la misma Pi, que registra la
asistencia en MariaDB y devuelve el resultado al nodo.

```
ESP32 (nodo) --LoRaLAN 869,525 MHz--> Raspberry Pi (gateway) --HTTP--> FastAPI + MariaDB <-- navegador
```

## Qué contiene

| Carpeta | Qué es |
|---|---|
| [`Device_ESP32_LoRaLAN/`](Device_ESP32_LoRaLAN/README.md) | Firmware del nodo de aula: NFC, sensores, relés, pantalla y LEDs |
| [`gatewayLoRaLAN/`](gatewayLoRaLAN/README.md) | Gateway en C++ para la Raspberry Pi: puente entre la radio y el servidor |
| [`Server/`](Server/README.md) | Servidor FastAPI y paneles web (profesor, conserje y administrador) |
| [`Database/`](Database/README.md) | Esquema y migración de la base de datos |
| [`LORALAN_LIB/LoRaLAN/`](LORALAN_LIB/LoRaLAN/README.md) | Biblioteca del protocolo LoRaLAN |
| [`LORALAN_LIB/sim_LoRaLAN/`](LORALAN_LIB/sim_LoRaLAN/README.md) | Simulador de PC con las pruebas del capítulo 6 |
| [`MonitorCardputer/`](MonitorCardputer/README.md) | Monitor pasivo de la red en un M5Stack Cardputer |
| [`NodoSimuladoLilyGo/`](NodoSimuladoLilyGo/README.md) | Nodo simulado en una LilyGo T3, para probar sin la PCB |
| `TestPCB/` | Prueba uno a uno los componentes de la PCB del nodo |
| [`arranque.md`](arranque.md) | Puesta en marcha paso a paso |

## Cómo funciona

- **Fichaje**: el nodo envía el UID de la tarjeta, el gateway lo sube al servidor y la
  respuesta (registrado, sin clase, denegado o reintentar) vuelve al nodo dentro del ACK
  de radio, que la muestra con un LED.
- **Sensores y relés**: el gateway sondea a cada nodo cada 5 s y sube sus lecturas.
- **Órdenes del panel** (relés, reinicio): el gateway las recoge del servidor y las envía
  al nodo.
- **Alta de nodos**: un nodo aparece solo en el servidor con su primera trama; el
  administrador le asigna el aula desde el panel.

## Requisitos

- **Hardware**: ESP32 con SX1276 y PN532 para el nodo; Raspberry Pi con SX1276 a 3,3 V
  para el gateway; antenas de 868 MHz.
- **Software**: en la Pi, MariaDB, Python 3, `cmake`, `liblgpio-dev`, `libmbedtls-dev` y
  [RadioLib](https://github.com/jgromes/RadioLib) en `~/RadioLib`; Arduino IDE o
  `arduino-cli` con el core ESP32.

## Arranque rápido

Orden: **base de datos → servidor → gateway → nodo**. Detalle y errores frecuentes en
[arranque.md](arranque.md).

```bash
# 1. Base de datos: MariaDB en la Pi, arranca sola (instalación en arranque.md)

# 2. Servidor (BD configurada en Server/.env)
cd Server && pip install -r requirements.txt && python appFastApi.py

# 3. Gateway (Raspberry Pi, SPI habilitado)
cd gatewayLoRaLAN && mkdir -p build && cd build
cmake -DRADIOLIB_DIR=$HOME/RadioLib -DLORALAN_DIR=$PWD/../../LORALAN_LIB/LoRaLAN/src ..
make -j4 gatewayLoRaLAN && cd ..
set -a; . ./gateway.env; set +a; sudo -E ./build/gatewayLoRaLAN

# 4. Nodo
arduino-cli compile --fqbn esp32:esp32:esp32 --library LORALAN_LIB/LoRaLAN Device_ESP32_LoRaLAN
```

## Pruebas sin hardware

```bash
# Gateway: tiempo en el aire, mensajes nodo-gateway y JSON con el servidor
cd gatewayLoRaLAN
g++ -std=c++17 -I src test_airtime.cpp src/radio/airtime.cpp -o test_airtime && ./test_airtime
g++ -std=c++17 -I ../Device_ESP32_LoRaLAN test_app_protocol.cpp -o test_app_protocol && ./test_app_protocol
g++ -std=c++17 -I src -I ../Device_ESP32_LoRaLAN test_bridge.cpp src/bridge.cpp -o test_bridge && ./test_bridge

# Servidor (sin base de datos)
cd Server && python -m pytest scripts/test_gateway_panel.py -q
```

El simulador del protocolo tiene sus propias órdenes en
[LORALAN_LIB/sim_LoRaLAN/README.md](LORALAN_LIB/sim_LoRaLAN/README.md).

## Estado

Probado en hardware de extremo a extremo: nodo ESP32, gateway en la Raspberry Pi,
servidor y base de datos.
