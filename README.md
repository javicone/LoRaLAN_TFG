# controlAsistenciaMigrado

Control de Asistencia sobre LoRaLAN, sin WebSocket:

```
ESP32 (nodo) --LoRaLAN--> Raspberry Pi (gateway) --HTTP--> FastAPI <--HTTP-- navegador
```

Copia de `ControlAsistencia/`, que no se toca.

```
controlAsistenciaMigrado/
├── gatewayLoRaLAN/          proceso C++ que corre en la Raspberry Pi
│   ├── src/
│   │   ├── main.cpp             hilos de radio, fichajes y red
│   │   ├── bridge.*             LoRaLAN <-> JSON del backend (contrato en bridge.h)
│   │   ├── config.h             pines, radio, tiempos, servidor
│   │   ├── compat/Arduino.*     capa Arduino sobre POSIX
│   │   ├── net/http_client.*    HTTP sobre sockets POSIX
│   │   └── radio/airtime.*      ToA de Semtech + ciclo de trabajo
│   ├── test_*.cpp           autocomprobaciones (no necesitan hardware)
│   └── CMakeLists.txt
├── Device_ESP32_LoRaLAN/    firmware del nodo
│   └── AppProtocol.h        mensajes nodo <-> gateway y códigos ACK/NACK
├── Server/                  backend FastAPI (routers/gateway.py, routers/panel.py)
└── Database/migracion_loralan.sql
```

---

## Por qué sin WebSocket

Conviene precisar el motivo, porque el que suele darse es incorrecto.

Un WebSocket vive en el lado IP —Raspberry Pi ↔ servidor, por Ethernet o WiFi— y
**no consume Time-on-Air de LoRa: cero**. El tiempo de aire lo determina únicamente
lo que sale por la radio.

Lo que sí cambia al quitarlo es **quién decide cuándo se transmite**. Con un socket
permanente abierto, el servidor puede empujar un comando en cualquier instante, y ese
comando sí se emite por radio y sí gasta ciclo de trabajo. Sin él, la Pi consulta los
comandos pendientes al ritmo que ella fija (`COMMAND_POLL_MS`) y los emite solo si su
presupuesto se lo permite.

**El límite regulatorio se aplica donde está la radio**, que es el único sitio donde
puede aplicarse con garantías.

---

## Arquitectura: tres hilos

La separación no es por rendimiento, es por corrección.

En el puente equivalente para ESP32 (`IMPLEMENTACIONMIA/examples/HttpWsBridgeGateway`)
el envío HTTP se hace dentro del callback de recepción, con `http.setTimeout(3000)`, y
la biblioteca envía el ACK **después** de que el callback retorne. Como el nodo tiene
su timeout de ACK en 1200 ms, un POST lento hace que dé el ACK por perdido y retransmita.

- **Hilo de radio**: bucle cerrado sobre `gateway.process()`. Nunca hace E/S de red.
  Los callbacks solo encolan. Envía los ACK de fichaje y los CMD.
- **Hilo de fichajes**: un POST por fichaje con timeout corto (800 ms). Va aparte para
  que un envío de telemetría lento no le haga perder el plazo del ACK.
- **Hilo de red**: telemetría, comandos pendientes y latido de estado.

### Fichaje: el resultado viaja en el ACK

```
nodo --PUSH(UID)--> radio --cola--> fichajes --POST /gateway/fichaje--> servidor
nodo <--ACK(code)-- radio <--cola-- fichajes <--{"code": n}-------------
```

El gateway usa `deferredPushAck` de LoRaLAN: el ACK del PUSH no sale solo, sino con
`ackPush()` cuando llega la respuesta. Un byte de estado: `0` = ACK, cualquier otro = NACK.

| Código | Significado | El nodo |
|---|---|---|
| 0 OK | asistencia registrada | LED verde |
| 1 SIN_CLASE, 2 DENEGADO, 3 DESCONOCIDO | NACK definitivo | no reintenta |
| 4 REINTENTAR | servidor caído, error o sin respuesta a tiempo | reenvía más tarde |

Si el servidor no contesta en `FICHAJE_DEADLINE_MS` (1000 ms, por debajo de los
1200 ms del nodo), la radio responde sola `REINTENTAR`. Si el ACK se pierde, la
retransmisión del nodo recibe la misma respuesta guardada y el servidor no ve el
fichaje dos veces.

Si la cola de subida se llena (`UPLINK_QUEUE_MAX`), se descartan los eventos **más
antiguos** y se contabilizan en el campo `dropped` del informe de estado. Perder
telemetría vieja es preferible a bloquear la radio.

---

## La biblioteca LoRaLAN se compila sin modificar

Es el mismo procedimiento que usa el simulador de PC: la biblioteca incluye
`<Arduino.h>`, y en lugar de tocarla se le proporciona esa cabecera. `src/compat/`
implementa sobre POSIX lo que la biblioteca usa —`millis()`, `delay()`, `random()`,
`F()` y `Serial`—, con reloj monótono real en vez de virtual.

La radio la resuelve RadioLib, que trae HAL oficial para Raspberry Pi
(`src/hal/RPi/PiHal.h`, sobre `lgpio`). La biblioteca LoRaLAN depende de la abstracción
`PhysicalLayer`, no del SX1276 concreto, así que encaja sin adaptadores.

---

## Conexionado SX1276 ↔ Raspberry Pi

Numeración BCM (la que usa `lgpio`).

| SX1276 | BCM | Pin físico | Función |
|--------|-----|-----------|---------|
| NSS / CS | 8  | 24 | Chip select (CE0) |
| MOSI     | 10 | 19 | SPI0 MOSI |
| MISO     | 9  | 21 | SPI0 MISO |
| SCK      | 11 | 23 | SPI0 SCK |
| DIO0     | 16 | 36 | Interrupción RX/TX |
| RST      | 13 | 33 | Reset |
| VCC      | —  | 1 o 17 | 3,3 V (**no 5 V**) |
| GND      | —  | 6, 9, 20… | Masa |

Condensador de 100 nF entre VCC y GND, lo más cerca posible del módulo. Antena de
868 MHz conectada **antes** de alimentar: transmitir sin antena daña el amplificador.

---

## Instalación en la Raspberry Pi

```bash
# 1. Habilitar SPI
sudo raspi-config          # Interface Options -> SPI -> Yes
ls /dev/spidev0.*          # debe aparecer spidev0.0

# 2. Dependencias
sudo apt update
sudo apt install -y build-essential cmake git liblgpio-dev libmbedtls-dev

# 3. RadioLib
cd ~
git clone https://github.com/jgromes/RadioLib

# 4. Compilar el gateway
cd ~/controlAsistenciaMigrado/gatewayLoRaLAN
mkdir build && cd build
cmake -DRADIOLIB_DIR=$HOME/RadioLib \
      -DLORALAN_DIR=$HOME/IMPLEMENTACIONMIA/libraries/LoRaLAN/src ..
make -j4
```

`libmbedtls-dev` **no es opcional**: sin ella, `LoRaLANCrypto.h` detecta su ausencia y
`aes128CtrCrypt()` devuelve `false`, con lo que el protocolo funcionaría **sin cifrar**.
El CMakeLists aborta si no la encuentra, precisamente para que eso no pase inadvertido.

---

## Ejecución

```bash
export LORALAN_SERVER_HOST=192.168.1.100
export LORALAN_SERVER_PORT=8000
export LORALAN_API_KEY="<api_key del gateway en la tabla nodos>"
export LORALAN_GATEWAY_NAME=gw-rpi-01

./gatewayLoRaLAN
```

| Variable | Por defecto | Significado |
|----------|-------------|-------------|
| `LORALAN_SERVER_HOST` | 127.0.0.1 | Host del backend FastAPI |
| `LORALAN_SERVER_PORT` | 8000 | Puerto del backend |
| `LORALAN_API_KEY` | (vacío) | api_key del gateway en `nodos`; con ella pide y renueva el JWT (caduca a las 24 h) |
| `LORALAN_GATEWAY_NAME` | gw-rpi-01 | Identifica este gateway; es su `device_id` en `nodos` |
| `LORALAN_POLL_MS` | 5000 | Intervalo de sondeo (ver aviso más abajo) |
| `LORALAN_TIMESYNC_MS` | 30000 | Intervalo de sincronización horaria |

### Como servicio systemd

```ini
# /etc/systemd/system/loralan-gateway.service
[Unit]
Description=Gateway LoRaLAN
After=network-online.target

[Service]
Type=simple
User=pi
Environment=LORALAN_SERVER_HOST=192.168.1.100
Environment=LORALAN_GATEWAY_NAME=gw-rpi-01
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

## Ciclo de trabajo: un incumplimiento detectado

Al implementar la ecuación de Time-on-Air de Semtech salió a la luz que **el valor por
defecto de la biblioteca LoRaLAN incumple la regulación de EU868**:

```
ToA de un POLL (11 B, SF7, 125 kHz, CR 4/5)  =  41,22 ms
presupuesto del 1 % en una hora              =  36 000 ms
POLLs máximos por hora                       =  873
                                             -> intervalo mínimo 4,12 s

con pollIntervalMs = 2000 (defecto)          =  2,06 %   EXCEDE el 1 %
con pollIntervalMs = 5000 (este gateway)     =  0,82 %   correcto
```

Ni el simulador ni la memoria lo detectaron, porque el simulador usa un airtime fijo de
15 ms —**2,7 veces por debajo del real en SF7**— y no modela la regulación.

El gateway **comprueba esto al arrancar y se niega a funcionar** si la configuración
excede el límite, en vez de emitir de más en silencio.

La dependencia con el factor de ensanchado es fuerte:

| SF | ToA de un POLL | Intervalo mínimo legal (1 %) |
|----|---------------|------------------------------|
| 7  | 41,2 ms  | 4,1 s |
| 12 | 1155 ms  | **115,5 s** |

En SF12 un sondeo cada 2 minutos ya agota el presupuesto. Es una restricción de
dimensionado que conviene tener presente al elegir el alcance del despliegue.

**Decisión:** todo el sistema (gateway, nodo, Cardputer, LilyGo) trabaja en
**869,525 MHz**, dentro de la sub-banda 869,4–869,65 MHz, que admite un **10 %** de ciclo
de trabajo (`DUTY_CYCLE_MAX 0.10` en `gatewayLoRaLAN/src/config.h`). Los mínimos legales
bajan diez veces: 0,41 s en SF7 y 11,5 s en SF12. El POLL de 5 s consume el 0,82 %.

---

## Firmware del nodo (ESP32)

`Device_ESP32_LoRaLAN/` es el firmware de aula sin WiFi: NFC, sensores, relés y
pantalla como antes, pero hablando LoRaLAN con el gateway.

- **Identidad**: `LORALAN_NODE_ID` en `config.h`, distinto en cada placa. En el
  servidor el nodo 2 es `node-002`. Radio y `LORALAN_APP_KEY` deben coincidir con el gateway.
- **Pines del SX1276**: CS=15, RST=13, DIO0=4 (los LEDs siguen en 25/27/14).
- **Dos núcleos**: la radio corre en el core 0 (`lora_node.cpp`); NFC, sensores,
  LEDs y pantalla en `loop()`. Se pasan datos por colas, así un `delay` de un LED
  nunca retrasa un ACK.
- **Fichaje**: la tarjeta sale en un PUSH y el resultado vuelve en el ACK
  (verde = registrado, amarillo = sin clase, rojo = denegado o tarjeta desconocida).
  Con `REINTENTAR` el fichaje se guarda en RAM (hasta 8) y se reenvía cada 30 s.
- **Sensores y relés**: van en la respuesta al POLL del gateway; los relés, cuando
  cambian y cada 6 sondeos.

Compilar desde línea de órdenes con la biblioteca **del repo**:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32   --library ../IMPLEMENTACIONMIA/libraries/LoRaLAN   --libraries ~/Documents/Arduino/libraries   Device_ESP32_LoRaLAN
```

Desde el IDE de Arduino hay que copiar antes `IMPLEMENTACIONMIA/libraries/LoRaLAN`
encima de `Documents/Arduino/libraries/LoRaLAN`: el IDE usa esa copia, y la que hay
es de julio (sin ACK con código ni CAD corregido).

---

## Instalación del lado servidor

```bash
cd Server
pip install -r requirements.txt
python appFastApi.py
```

### Base de datos (MySQL 8+ o MariaDB 10.5+)

| Caso | Fichero |
|------|---------|
| BD que ya existe (la de `ControlAsistencia`) | `Database/migracion_loralan.sql` |
| Instalación desde cero | `Database/esquema.sql` y después `migracion_loralan.sql` |

`esquema.sql` es el esquema que usa el código de verdad. **No usar**
`ControlAsistencia/Database/Esquemas_bbdd.sql` (tiene `clases.id_aula`, rol `admin` y le
falta `profesor_id`) ni restaurar `backupControlAsistencia.sql` (inserta 3 columnas en una
tabla `aulas` de 2).

La migración es repetible y no usa sintaxis exclusiva de MariaDB. Añade
`nodos.id_aula` (con `ON DELETE SET NULL`), deja `api_key` y `uid_tarjeta` opcionales,
añade la FK e índice de `asistencias.clase_id` y da de alta el gateway `gw-rpi-01` con una
api_key de ejemplo: **cámbiala** y usa la misma en `LORALAN_API_KEY`.

Contenedor nuevo con solo el esquema (montar la carpeta entera ejecutaría también el
backup y `Esquemas_bbdd.sql`, que chocan entre sí):

```bash
docker run --name control-asistencia-mysql -e MYSQL_ROOT_PASSWORD=...   -e MYSQL_DATABASE=control-asistencia -p 3306:3306   -v "$PWD/Database/esquema.sql:/docker-entrypoint-initdb.d/01-esquema.sql" -d mysql:8.0
```

**Contraseñas en claro.** El login solo acepta hashes bcrypt: los usuarios guardados con la
contraseña en claro no pueden entrar hasta ejecutar `python scripts/migrate_passwords.py`
(primero con `--preview` para ver a quién afecta).

Sin WebSocket en ningún sitio: se han eliminado `routers/websockets.py`,
`utils/websocket_manager.py` y `routers/aulas.py`.

### Endpoints

Formato exacto de cada cuerpo JSON: comentario de cabecera de `gatewayLoRaLAN/src/bridge.h`.

| Método | Ruta | Quién llama | Para qué |
|--------|------|-------------|----------|
| POST | `/auth/device` | Gateway | Obtiene el JWT con su api_key |
| POST | `/gateway/fichaje` | Gateway | Registra un fichaje y devuelve el código del ACK |
| POST | `/gateway/uplink` | Gateway | Telemetría (sensores, relés) y altas/bajas de nodos |
| GET  | `/gateway/commands` | Gateway | Recoge los comandos pendientes de sus nodos |
| POST | `/gateway/status` | Gateway | Latido: nodos vivos, ciclo de trabajo |
| GET  | `/panel/estado` | Paneles (cada 5 s) | Estado de las aulas; el admin ve también radio y gateways |
| POST | `/panel/rele` | Conserje, admin | Deja una orden de relé en la cola del gateway |
| POST | `/panel/reset` | Admin | Orden de reinicio del nodo |
| POST | `/panel/aula` | Admin | Asigna el aula de un nodo (se guarda en BD) |

**Nodos y aulas.** Un nodo LoRaLAN con id `N` es `node-NNN` en la tabla `nodos`
(p. ej. 16 → `node-016`). Se da de alta solo con la primera trama que sube el gateway;
el administrador le asigna el aula desde el panel. Sin aula, sus fichajes reciben
`DESCONOCIDO`.

**Estado y comandos en memoria.** "Conectado" = el gateway subió algo del nodo en los
últimos 120 s. Las órdenes pendientes viven en memoria: un reinicio del servidor pierde
las que el gateway aún no había recogido.

**Fichajes repetidos.** Si el nodo reenvía un fichaje (tras `REINTENTAR`) que el
servidor sí llegó a guardar, no se duplica: un profesor ficha una vez por clase y aula.

---

## Verificación

La lógica que no depende del hardware tiene autocomprobación:

```bash
g++ -std=c++17 -I src test_airtime.cpp src/radio/airtime.cpp -o test_airtime
./test_airtime          # código de salida 0 = todo correcto

g++ -std=c++17 -I ../Device_ESP32_LoRaLAN test_app_protocol.cpp -o test_app_protocol
./test_app_protocol     # formato de mensajes de aplicación (AppProtocol.h)

g++ -std=c++17 -I src -I ../Device_ESP32_LoRaLAN test_bridge.cpp src/bridge.cpp -o test_bridge
./test_bridge           # JSON de ida y vuelta con el backend
```

Servidor (sin MySQL, la BD se sustituye por funciones falsas):

```bash
cd Server
python -m pytest scripts/test_gateway_panel.py -q
```

En la Pi también: `make test_airtime test_app_protocol test_bridge`.

Cubre el cálculo de ToA (con los valores derivados a mano en los comentarios, para que
la cuenta sea rehacible), el crecimiento con SF y BW, la ventana deslizante del ciclo
de trabajo y el cálculo del intervalo mínimo legal.

### Estado de la validación

| Componente | Estado |
|-----------|--------|
| `radio/airtime.*` | **Verificado.** Test ejecutado, 0 fallos. |
| `bridge.*`, `AppProtocol.h` | **Verificado.** Tests ejecutados en PC. |
| `main.cpp` (sintaxis) | Compila con `-fsyntax-only` contra RadioLib 7.6 real (lgpio simulado). Sin ejecutar. |
| `net/http_client.*` | **Sin verificar.** Usa cabeceras POSIX; no compila ni se ejecuta fuera de Linux. |
| `compat/Arduino.*` | **Sin verificar.** Usa `random_r`/`initstate_r` de glibc. |
| `main.cpp` (ejecución) | **Sin verificar.** Necesita la Pi y la radio conectada. |
| `Server/routers/gateway.py`, `panel.py` | **Verificado sin BD** (`test_gateway_panel.py`). Sin probar contra MySQL real. |
| Paneles web (JS) | Sintaxis comprobada (`node --check`). Sin probar en navegador. |

Todo lo marcado como sin verificar está escrito pero **nunca se ha compilado ni
ejecutado**, porque requiere la Raspberry Pi. Es de esperar que aparezcan errores de
compilación en el primer intento sobre la placa.

---

## Primer arranque: qué mirar

```
gatewayLoRaLAN
  servidor : 192.168.1.100:8000
  gateway  : gw-rpi-01
Inicializando SX1276... OK
  ToA trama minima (0 B) : 41.2 ms
  ToA trama maxima (64 B): 133.4 ms
  presupuesto duty cycle : 36000 ms/hora (1.0%)
  sondeo cada 5.0 s     : 0.82% de ciclo de trabajo  (minimo legal 4.1 s)
Gateway en marcha. Ctrl+C para parar.
```

Si `begin()` devuelve error, casi siempre es una de tres: SPI sin habilitar en
`raspi-config`, el cableado de CS/RST/DIO0 cruzado, o el módulo alimentado a 5 V en
lugar de 3,3 V.
