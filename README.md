# Control de asistencia sobre LoRaLAN

Sistema de control de asistencia por tarjeta NFC para aulas. Cada aula tiene un nodo
ESP32 que lee la tarjeta del profesor y la envía por radio LoRa, con el protocolo propio
**LoRaLAN**, a un gateway en una Raspberry Pi. El gateway la pasa por HTTP a un servidor
FastAPI, que comprueba si el profesor tiene clase en esa aula, guarda la asistencia en
MySQL y devuelve el resultado al nodo. Los paneles web muestran el estado de las aulas
y permiten encender los relés de cada una.

Forma parte del TFG de Telemática de Javier Conejero (Universidad de Extremadura).

```
ESP32 (nodo) --LoRaLAN 869,525 MHz--> Raspberry Pi (gateway) --HTTP--> FastAPI + MySQL <--HTTP-- navegador
```

---

## Qué contiene

| Carpeta | Qué es | Detalle |
|---|---|---|
| `Device_ESP32_LoRaLAN/` | Firmware del nodo de aula: lector NFC (PN532), sensores (BME280, BH1750, radar LD2410), relés, pantalla OLED y LEDs | [README](Device_ESP32_LoRaLAN/README.md) |
| `gatewayLoRaLAN/` | Programa C++ que corre en la Raspberry Pi con un SX1276 por SPI. Hace de puente entre la radio y el servidor | [README](gatewayLoRaLAN/README.md) |
| `Server/` | Backend FastAPI y paneles web para profesor, conserje y administrador | [README](Server/README.md) |
| `Database/` | Esquema MySQL y migración para la versión LoRaLAN | [README](Database/README.md) |
| `LORALAN_LIB/LoRaLAN/` | Biblioteca LoRaLAN (gateway, nodo, paquetes, MIC y cifrado) que usan el nodo y el gateway | [README](LORALAN_LIB/LoRaLAN/README.md) |
| `MonitorCardputer/` | Monitor pasivo de la red en un M5Stack Cardputer (SX1262): solo escucha, muestra tramas, nodos y fichajes | [README](MonitorCardputer/README.md) |
| `NodoSimuladoLilyGo/` | Nodo de aula simulado en una LilyGo T3: mismos mensajes que el nodo real, con sensores y fichajes inventados | [README](NodoSimuladoLilyGo/README.md) |
| `TestPCB/` | Sketch que prueba uno a uno los componentes de la PCB del nodo, por monitor serie | `TestPCB.ino` |
| `arranque.md` | Guía de puesta en marcha completa, paso a paso | [arranque.md](arranque.md) |

---

## Cómo funciona

- **Fichaje**: el profesor acerca la tarjeta al nodo, que envía el UID al gateway. El
  gateway lo sube con `POST /gateway/fichaje` y el servidor responde con un código que
  vuelve al nodo dentro del ACK de radio:

  | Código | Significado | Nodo |
  |---|---|---|
  | 0 OK | Asistencia registrada | LED verde |
  | 1 SIN_CLASE | No tiene clase ahora en esa aula | LED amarillo |
  | 2 DENEGADO, 3 DESCONOCIDO | Tarjeta no válida o nodo sin aula | LED rojo |
  | 4 REINTENTAR | Servidor caído o sin respuesta a tiempo | Guarda el fichaje y lo reenvía cada 30 s |

  Un fichaje reenviado no se registra dos veces.
- **Sensores y relés**: el gateway sondea a cada nodo cada 5 s; el nodo responde con
  sus sensores y el estado de los relés, y el gateway lo sube al servidor.
- **Órdenes desde el panel** (relés, reinicio): el servidor las deja en cola, el gateway
  las recoge periódicamente y las envía al nodo por radio.
- **Alta de nodos**: un nodo aparece solo en la tabla `nodos` como `node-NNN` con su
  primera trama; el administrador le asigna el aula desde el panel.
- **Ciclo de trabajo**: todo el sistema emite en 869,525 MHz (sub-banda del 10 %). El
  gateway calcula el tiempo en el aire de cada trama y no arranca si la configuración de
  sondeo supera el límite legal.

Detalle del gateway (hilos, ACK diferido, contrato JSON) en
[gatewayLoRaLAN/README.md](gatewayLoRaLAN/README.md); endpoints y lógica de fichaje en
[Server/README.md](Server/README.md).

---

## Requisitos

**Hardware**

- Nodo: ESP32 con SX1276, PN532, sensores, relés y pantalla (pines en
  [Device_ESP32_LoRaLAN/PINES_ESP32_DIAGRAMA.md](Device_ESP32_LoRaLAN/PINES_ESP32_DIAGRAMA.md)).
- Gateway: Raspberry Pi con SX1276 alimentado a **3,3 V** (conexionado en
  [gatewayLoRaLAN/README.md](gatewayLoRaLAN/README.md)).
- Antenas de 868 MHz, conectadas **antes** de alimentar las radios.

**Software**

- MySQL 8 (en Docker, por ejemplo).
- Python 3 con las dependencias de `Server/requirements.txt`.
- En la Pi: `build-essential cmake liblgpio-dev libmbedtls-dev` y
  [RadioLib](https://github.com/jgromes/RadioLib) clonado en `~/RadioLib`.
  `libmbedtls-dev` es obligatoria: sin ella no hay cifrado y CMake se niega a configurar.
- Arduino IDE o `arduino-cli` con el core ESP32 para el nodo.

---

## Arranque rápido

Orden: **base de datos → servidor → gateway → nodo**. La guía completa, con firewall,
`.env`, comprobaciones y errores frecuentes, está en [arranque.md](arranque.md).

**1. Base de datos** (PC, Docker):

```bash
docker start control-asistencia-mysql
```

Instalación desde cero: `Database/esquema.sql` y después `Database/migracion_loralan.sql`
(ver [Database/README.md](Database/README.md)). La migración da de alta el gateway
`gw-rpi-01` con una api_key de ejemplo que hay que cambiar.

**2. Servidor** (Raspberry Pi o PC), con `DB_HOST` apuntando a la BD en `Server/.env`:

```bash
cd Server
python3 -m venv .venv && . .venv/bin/activate     # solo la primera vez
pip install -r requirements.txt                   # solo la primera vez
python appFastApi.py                              # http://0.0.0.0:8000
```

El servidor debe ir sin TLS (el gateway no habla HTTPS). En Windows, lanzarlo con
`$env:PYTHONUTF8=1; python appFastApi.py`.

**3. Gateway** (Raspberry Pi, con SPI habilitado en `raspi-config`):

```bash
cd gatewayLoRaLAN
rm -rf build && mkdir build && cd build
cmake -DRADIOLIB_DIR=$HOME/RadioLib ..
make -j4 gatewayLoRaLAN
cd ..
set -a; . ./gateway.env; set +a
sudo -E ./build/gatewayLoRaLAN
```

Por defecto CMake busca la biblioteca en `../../IMPLEMENTACIONMIA/libraries/LoRaLAN/src`.
Si solo se ha clonado este repositorio, añadir
`-DLORALAN_DIR=$PWD/../../LORALAN_LIB/LoRaLAN/src` a la orden `cmake`.

Arranque correcto: `Inicializando SX1276... OK` y `Gateway en marcha`; en el servidor
aparecen `POST /auth/device 200` y `POST /gateway/status` periódicos.

**4. Nodo** (ESP32):

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 --library LORALAN_LIB/LoRaLAN Device_ESP32_LoRaLAN
```

Desde el IDE de Arduino, copiar antes `LORALAN_LIB/LoRaLAN` encima de
`Documents/Arduino/libraries/LoRaLAN`, que es la copia que usa el IDE. Al arrancar, el
nodo se une a la red; después se le asigna el aula desde el panel de administrador.

### Gateway como servicio (opcional)

```ini
# /etc/systemd/system/loralan-gateway.service
[Unit]
Description=Gateway LoRaLAN
After=network-online.target

[Service]
EnvironmentFile=/home/pi/controlAsistenciaMigrado/gatewayLoRaLAN/gateway.env
ExecStart=/home/pi/controlAsistenciaMigrado/gatewayLoRaLAN/build/gatewayLoRaLAN
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl enable --now loralan-gateway
journalctl -u loralan-gateway -f
```

---

## Configuración

**Gateway** (`gatewayLoRaLAN/gateway.env`):

| Variable | Por defecto | Uso |
|---|---|---|
| `LORALAN_SERVER_HOST` | 127.0.0.1 | Host del servidor FastAPI |
| `LORALAN_SERVER_PORT` | 8000 | Puerto del servidor |
| `LORALAN_API_KEY` | (vacío) | api_key del gateway en la tabla `nodos` |
| `LORALAN_GATEWAY_NAME` | gw-rpi-01 | `device_id` del gateway en `nodos` |
| `LORALAN_POLL_MS` | 5000 | Intervalo de sondeo a los nodos |

**Nodo** (`Device_ESP32_LoRaLAN/config.h`):

- `LORALAN_NODE_ID`: `0` pide dirección dinámica al gateway; de 1 a 127 fija una
  manual, distinta en cada placa.
- `LORALAN_APP_KEY`, red y parámetros de radio: deben coincidir con los del gateway.

**Usuarios**: el login solo acepta contraseñas en bcrypt. Si la BD tiene contraseñas en
claro, ejecutar `python scripts/migrate_passwords.py` (primero con `--preview`).

---

## Pruebas sin hardware

Desde `gatewayLoRaLAN/`:

```bash
g++ -std=c++17 -I src test_airtime.cpp src/radio/airtime.cpp -o test_airtime && ./test_airtime
g++ -std=c++17 -I ../Device_ESP32_LoRaLAN test_app_protocol.cpp -o test_app_protocol && ./test_app_protocol
g++ -std=c++17 -I src -I ../Device_ESP32_LoRaLAN test_bridge.cpp src/bridge.cpp -o test_bridge && ./test_bridge
```

Comprueban el tiempo en el aire y el ciclo de trabajo, el formato de los mensajes
nodo ↔ gateway y el JSON con el servidor. Código de salida 0 = todo correcto.

Servidor (sin MySQL; la BD se sustituye por funciones falsas):

```bash
cd Server
python -m pytest scripts/test_gateway_panel.py -q
```

---

## Estado

Probado en hardware: el gateway en la Raspberry Pi, el nodo ESP32, el servidor y la base
de datos funcionan juntos de extremo a extremo. Las pruebas de PC anteriores pasan.
