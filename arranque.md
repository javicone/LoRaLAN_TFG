# Arranque del prototipo

Puesta en marcha de extremo a extremo: base de datos en el PC, servidor y gateway en la
Raspberry Pi, y nodo ESP32.

```
ESP32 (nodo) --LoRa--> Raspberry Pi 192.168.1.43 [gateway LoRaLAN + FastAPI :8000]
                                  |
                                  +--MySQL :3306--> PC 192.168.1.138 [Docker control-asistencia-mysql]
```

La Pi es la única que habla con la BD. El gateway habla con FastAPI en local
(`127.0.0.1:8000`) y FastAPI habla con el MySQL del PC.

Orden de arranque: **1. BD (PC) → 2. servidor (Pi) → 3. gateway (Pi) → 4. nodo**.

---

## 1. PC: base de datos en Docker

1. Abrir Docker Desktop.
2. Arrancar el contenedor (ya existe, con la BD migrada):
   ```powershell
   docker start control-asistencia-mysql
   docker ps --format "{{.Names}} {{.Status}} {{.Ports}}"
   ```
   Debe aparecer `0.0.0.0:3306->3306/tcp`.
3. **Solo la primera vez:** abrir el puerto 3306 a la Raspberry en el firewall de Windows
   (PowerShell **como administrador**):
   ```powershell
   New-NetFirewallRule -DisplayName "MySQL 3306 Raspberry" -Direction Inbound -Protocol TCP -LocalPort 3306 -RemoteAddress 192.168.1.43 -Action Allow -Profile Any
   ```
   `-Profile Any` hace que funcione aunque Windows marque la red como Pública, y
   `-RemoteAddress` limita el acceso a la Pi.

MySQL ya acepta conexiones remotas: el usuario `root@'%'` existe en el contenedor.

---

## 2. Raspberry Pi: servidor FastAPI

```bash
cd ~/Desktop/TFGTelem/controlAsistenciaMigrado/Server
git pull
```

**Solo la primera vez:**

```bash
sudo apt install -y python3-venv
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
```

En `Server/.env`, apuntar la BD al PC:

```
DB_HOST=192.168.1.138
DB_PORT=3306
```

Comprobar que la Pi llega al MySQL del PC:

```bash
nc -zv 192.168.1.138 3306       # debe decir "succeeded" / "open"
```

Si da timeout, el problema es el firewall de Windows (paso 1.3).

**Arrancar** (en su propia terminal, y dejarla abierta):

```bash
cd ~/Desktop/TFGTelem/controlAsistenciaMigrado/Server
. .venv/bin/activate
python appFastApi.py
```

Arranque correcto: `SSL/TLS DESHABILITADO`, `http://0.0.0.0:8000` y
`Pool de base de datos creado`.

El servidor tiene que ir **sin TLS**: el cliente HTTP del gateway no habla HTTPS. Los
certificados están renombrados a `keys/*.pem.bak` para eso; no restaurarlos.

Comprobación (desde otra terminal de la Pi; la api_key está en `gatewayLoRaLAN/gateway.env`):

```bash
curl -s -X POST http://127.0.0.1:8000/auth/device -H 'Content-Type: application/json' \
  -d '{"device_id":"gw-rpi-01","api_key":"TU_API_KEY"}'
```

Si devuelve un `access_token`, el servidor está bien y lee la BD.

> En Windows (si se arranca el servidor en el PC) hay que lanzarlo con
> `$env:PYTHONUTF8=1; python appFastApi.py`: sin eso se cae al imprimir los emojis del
> log en la consola.

---

## 3. Raspberry Pi: gateway LoRaLAN

**Solo la primera vez:** dependencias y RadioLib.

```bash
sudo apt install -y build-essential cmake liblgpio-dev libmbedtls-dev
ls ~/RadioLib/src/RadioLib.h || git clone https://github.com/jgromes/RadioLib ~/RadioLib
sudo raspi-config               # Interface Options -> SPI -> Yes
ls /dev/spidev0.0               # debe existir
```

`libmbedtls-dev` es obligatoria: sin ella, la radio funcionaría pero sin cifrar, y
CMake se niega a configurar el proyecto.

**Compilar.** Hay que pegar los comandos **línea a línea** y sin las líneas de comentario
`#`: al pegar un bloque, la terminal mete basura (`^[[200~`).

```bash
cd ~/Desktop/TFGTelem/controlAsistenciaMigrado/gatewayLoRaLAN
rm -rf build
mkdir build
cd build
cmake -DRADIOLIB_DIR=$HOME/RadioLib ..
make -j4 gatewayLoRaLAN
```

- Por defecto CMake toma la biblioteca LoRaLAN de `../../IMPLEMENTACIONMIA/libraries/LoRaLAN/src`.
  Si solo se tiene este repo, añadir `-DLORALAN_DIR=$PWD/../../LORALAN_LIB/LoRaLAN/src`
  (desde `build/`). RadioLib no está en el repo y se pasa con `-DRADIOLIB_DIR`.
- `make: *** No rule to make target 'gatewayLoRaLAN'` = no hay Makefile en la carpeta
  actual. Suele pasar por ejecutar `mkdir build && cd build` estando ya dentro de `build/`
  (se acaba en `build/build`). Solución: el bloque de arriba tal cual, que borra `build`.
- Termina bien con `[100%] Built target gatewayLoRaLAN`.

`gateway.env` debe tener `LORALAN_SERVER_HOST=127.0.0.1` (FastAPI corre en la misma Pi).

**Arrancar** (en otra terminal):

```bash
cd ~/Desktop/TFGTelem/controlAsistenciaMigrado/gatewayLoRaLAN
set -a; . ./gateway.env; set +a
sudo -E ./build/gatewayLoRaLAN
```

`sudo -E` da acceso a los GPIO sin perder las variables de `gateway.env`.

Arranque correcto:

```
Inicializando SX1276... OK
  ...
  sondeo cada 5.0 s     : 0.82% de ciclo de trabajo  (minimo legal 4.1 s)
Gateway en marcha. Ctrl+C para parar.
```

En la terminal del servidor deben aparecer `POST /auth/device 200` y, periódicamente,
`POST /gateway/status`.

Si falla `Inicializando SX1276`:
- SPI sin habilitar (`ls /dev/spidev0.0`).
- Radio no alimentada a **3,3 V**.
- Cableado: DIO0 → BCM16 (pin 36), RST → BCM13 (pin 33); CS lo mueve el driver SPI
  (CE0, pin 24). Tabla completa en `README.md`.

---

## 4. Nodo ESP32

- Compilar con la biblioteca **del repo** (ver `README.md`, sección "Arranque rápido").
  Desde el IDE de Arduino hay que copiar antes `LORALAN_LIB/LoRaLAN` encima
  de `Documents/Arduino/libraries/LoRaLAN`, porque la copia que usa el IDE está desfasada.
- Pines del SX1276: CS=15, RST=13, DIO0=4. Bus SPI compartido con el PN532: SCK=18,
  MISO=19, MOSI=23; CS del PN532 = 5, IRQ = 34. PN532 a **3,3 V**.
- Al arrancar hace el JOIN y el servidor da de alta el nodo con la primera trama (`node-254` si la
  dirección es dinámica, la primera que reparte el gateway; `node-NNN` si es manual).
- Asignar el aula al nodo desde el panel de administrador; sin aula, los fichajes devuelven
  `DESCONOCIDO` (LED rojo).

### Si el ESP32 no arranca con el PN532 conectado

Si no arranca ni con el USB del PC ni con un adaptador, el problema no es falta de
corriente (el PN532 gasta unos 150 mA): lo más probable es un cortocircuito en el módulo.

1. Con todo desconectado, medir con el multímetro VCC–GND del PN532: ~0 Ω = cortocircuito
   (buscar puentes de estaño en el conector).
2. Conectar solo VCC y GND. Si así ya no arranca, el problema es de alimentación o del
   módulo; si arranca, añadir los cables de señal de uno en uno hasta encontrar el que falla.
3. Mirar el monitor serie a 115200: `Brownout detector was triggered` = caída de tensión;
   `invalid header` / `flash read err` = pin de arranque (GPIO 5 lo es).
4. Comprobar que VCC y GND no estén invertidos y que los interruptores de modo estén en SPI.

---

## Comprobación final

1. Pasar una tarjeta: LED verde y fila nueva en `asistencias`.
2. Tarjeta desconocida: LED rojo.
3. Parar el servidor y fichar: el nodo recibe `REINTENTAR`, guarda el fichaje y lo reenvía
   cada 30 s; al volver el servidor se registra una sola vez.
4. Relés desde el panel: el comando llega al nodo en el siguiente sondeo del gateway.
