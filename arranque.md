# Arranque del prototipo

```
ESP32 (nodo) --LoRa--> Raspberry Pi 192.168.1.43 [gateway LoRaLAN + FastAPI :8000 + MariaDB :3306]
```

Todo corre en la Pi y se habla en local. Orden: **1. BD → 2. servidor → 3. gateway → 4. nodo**.
Las órdenes se lanzan desde `~/Desktop/TFGTelem/controlAsistenciaMigrado` (antes, `git pull`).

## 1. Base de datos (solo la primera vez)

MariaDB arranca sola con la Pi. Sustituir `CONTRASEÑA_BD` y `API_KEY_GATEWAY` (16 caracteres o más):

```bash
sudo apt install -y mariadb-server
sudo mysql -e "CREATE DATABASE \`control-asistencia\` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;"
sudo mysql -e "CREATE USER 'asistencia'@'localhost' IDENTIFIED BY 'CONTRASEÑA_BD';"
sudo mysql -e "GRANT ALL PRIVILEGES ON \`control-asistencia\`.* TO 'asistencia'@'localhost';"
sudo mysql control-asistencia < Database/esquema.sql
sudo mysql control-asistencia < Database/migracion_loralan.sql
sudo mysql control-asistencia -e "UPDATE nodos SET api_key='API_KEY_GATEWAY' WHERE device_id='gw-rpi-01';"
```

La misma api_key va en `gatewayLoRaLAN/gateway.env` (`LORALAN_API_KEY=...`).

## 2. Servidor FastAPI

**Primera vez:**

```bash
cd Server
sudo apt install -y python3-venv
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt
python scripts/create_user.py "Administrador" admin@uex.es 'CONTRASEÑA_ADMIN' --rol administrador
```

`Server/.env`: `DB_HOST=localhost`, `DB_USER=asistencia`, `DB_PASSWORD=CONTRASEÑA_BD`,
`DB_NAME=control-asistencia`. Usuarios, aulas y clases se crean luego desde `/gestion-datos`.

**Arrancar** (terminal propia, abierta):

```bash
cd Server && . .venv/bin/activate && python appFastApi.py
```

Debe mostrar `SSL/TLS DESHABILITADO`, `http://0.0.0.0:8000` y `Pool de base de datos creado`.
Va **sin TLS** porque el gateway no habla HTTPS: no restaurar los `keys/*.pem.bak`.

## 3. Gateway LoRaLAN

**Primera vez:**

```bash
sudo apt install -y build-essential cmake liblgpio-dev libmbedtls-dev
ls ~/RadioLib/src/RadioLib.h || git clone https://github.com/jgromes/RadioLib ~/RadioLib
sudo raspi-config               # Interface Options -> SPI -> Yes; debe existir /dev/spidev0.0
```

Sin `libmbedtls-dev` (cifrado) CMake no configura.

**Compilar**, pegando línea a línea (al pegar un bloque entero la terminal mete `^[[200~`):

```bash
cd gatewayLoRaLAN
rm -rf build && mkdir build && cd build
cmake -DRADIOLIB_DIR=$HOME/RadioLib -DLORALAN_DIR=$PWD/../../LORALAN_LIB/LoRaLAN/src ..
make -j4 gatewayLoRaLAN
```

Termina con `[100%] Built target gatewayLoRaLAN`. `No rule to make target` = se está en
`build/build`; repetir el bloque, que borra `build`.

**Arrancar** (otra terminal; `gateway.env` con `LORALAN_SERVER_HOST=127.0.0.1`):

```bash
cd gatewayLoRaLAN
set -a; . ./gateway.env; set +a
sudo -E ./build/gatewayLoRaLAN
```

Debe mostrar `Inicializando SX1276... OK`, el ciclo de trabajo del sondeo (0.82 % a 5 s) y
`Gateway en marcha`; en el servidor, `POST /auth/device 200` y `POST /gateway/status` periódicos.
Si falla el SX1276: SPI sin habilitar, radio sin 3,3 V o cableado
(ver `Device_ESP32_LoRaLAN/PINES_ESP32_DIAGRAMA.md`).

## 4. Nodo ESP32

- Compilar con la biblioteca del repo (`README.md`, "Arranque rápido"). Desde el IDE de
  Arduino, copiar antes `LORALAN_LIB/LoRaLAN` sobre `Documents/Arduino/libraries/LoRaLAN`.
- Al arrancar hace el JOIN y el servidor lo da de alta con su primer paquete (`node-254` si la
  dirección es dinámica, `node-NNN` si es manual). Asignarle el aula desde el panel de
  administrador; sin aula, los fichajes devuelven `DESCONOCIDO` (LED rojo).
- **No arranca con el PN532 conectado**: casi siempre es un cortocircuito en el módulo, no falta
  de corriente. Medir VCC–GND (~0 Ω = corto), probar solo con alimentación y añadir señales de
  una en una. En el monitor serie (115200), `Brownout` = caída de tensión;
  `invalid header` = pin de arranque (GPIO 5).

## Comprobación final

1. Tarjeta conocida: LED verde y fila nueva en `asistencias`. Desconocida: LED rojo.
2. Con el servidor parado, el nodo recibe `REINTENTAR` y reenvía cada 30 s; al volver el
   servidor se registra una sola vez.
3. Relés desde el panel: llegan al nodo en el siguiente sondeo.
