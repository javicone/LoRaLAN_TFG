# Device_ESP32_LoRaLAN — firmware del nodo de aula

## Qué hace

Es el aparato que se instala en cada aula. Un ESP32 con:

- **Lector NFC PN532**: el profesor pasa su tarjeta para fichar.
- **Sensores**: BME280 (temperatura, humedad, presión), BH1750 (luz) y radar LD2410
  (presencia de personas).
- **Dos relés** (luces, calefacción…) que se manejan desde el panel web.
- **Indicadores**: LEDs verde/amarillo/rojo, buzzer y pantalla OLED 128×64.
- **Radio SX1276** con la que habla LoRaLAN con el gateway. **No usa WiFi.**

El nodo no sabe nada del servidor ni de las clases: envía el UID de la tarjeta y
el gateway le devuelve el resultado.

| LED / pantalla | Significado |
|---|---|
| Verde + "registrado" | Asistencia guardada (código OK) |
| Amarillo "Sin clase ahora" | No hay clase en este aula a esta hora |
| Rojo "Clase de otro profesor" | La clase existe pero es de otro profesor |
| Rojo "Tarjeta no registrada" | Tarjeta desconocida, o el nodo no tiene aula asignada |
| Amarillo parpadeando "se reenviará" | El servidor no respondió; el fichaje se guarda y se reintenta |

## Ficheros

| Fichero | Qué contiene |
|---|---|
| `Device_ESP32_LoRaLAN.ino` | `setup()` y `loop()`: lectura NFC, resultado del fichaje, sensores, pantalla |
| `lora_node.cpp` | Todo lo de radio: tarea en el core 0, colas, cola de fichajes pendientes |
| `AppProtocol.h` | Formato binario de los mensajes nodo ↔ gateway y códigos de resultado (compartido con el gateway) |
| `config.h` | Id del nodo, radio, APP_KEY, tiempos y pines |
| `globals.h / globals.cpp` | Estructuras de datos, objetos de los sensores y declaraciones comunes |
| `setupSensores.ino` | `initSensores()`: inicializa relés, NFC, radar, BME280, BH1750 y pantalla |
| `NFC_module.ino` | PN532 por SPI: arranque con reintentos y lectura por sondeo (100 ms) |
| `BME_module.ino` | BME280: lectura y validación de rangos |
| `LightMeter_module.ino` | BH1750: lectura y validación |
| `radar_module.ino` | LD2410 por UART2: presencia y distancias |
| `indicators.ino` | LEDs, buzzer, relés y secuencias (inicio, éxito, error) |
| `display_module.ino` | Pantalla OLED: estado del enlace, instrucciones y sensores |
| `funciones_auxiliares.ino` | `uidToString()`: UID a hexadecimal en mayúsculas |
| `PINES_ESP32_DIAGRAMA.md` | Tabla y mapa completo de pines |

## Cómo está programado

### 1. Dos núcleos, separados por colas

El ESP32 tiene dos núcleos y el firmware los usa para que la radio nunca espere a
nada lento (un sensor I2C, un `delay()` de un LED, la pantalla):

```
core 1: loop()                               core 0: tareaRadio()
  lee NFC ── encolarFichaje(uid) ──► colaFichajes ──► PUSH (FICHAJE)
  LEDs   ◄── recogerResultadoFichaje ◄── colaResultados ◄── ACK con código
  sensores ─ publicarLecturaSensores ─► ultimaLectura ──► respuesta al POLL
```

- `colaFichajes` y `colaResultados` son colas de FreeRTOS (`xQueueSend/Receive`
  con timeout 0: nadie se bloquea).
- La última lectura de sensores se comparte protegida con `portENTER_CRITICAL`.
- `tareaRadio()` se crea con `xTaskCreatePinnedToCore(..., 0)` en `setup()`.

### 2. La tarea de radio (`lora_node.cpp`)

Bucle cada 2 ms:

1. Si el chip marca `RX_DONE` (se lee por SPI, sin interrupción en DIO0, porque el CAD
   también usa DIO0 y dispararía en falso), lee la trama y la pasa a
   `nodo.handleReceived()`. Luego `startReceive()`.
2. `nodo.process()`: la biblioteca LoRaLAN gestiona JOIN, reintentos, TIME_SYNC, etc.
3. `atenderFichajes()`: envía el siguiente fichaje pendiente.
4. Si llegó un CMD de reset, reinicia pasado 1 s (para que antes salga el ACK del CMD).

Callbacks que registra en LoRaLAN:

- `onPollRequest`: el gateway pregunta; el nodo responde con el **estado de relés** si
  cambió (o cada 6 sondeos, para que el servidor lo recupere si se reinicia) y si no,
  con los **sensores** (10 bytes).
- `onComando`: `RELE` → `activarRele1/2()`; `RESET` → reinicio programado.
- `onPushAck`: llega el resultado de un fichaje.
- `onAsociado`: JOIN aceptado; fuerza a informar de los relés.

### 3. Fichajes: cola en RAM y reintentos

- `pendientes[]` guarda hasta `FICHAJES_EN_COLA_MAX` (8) fichajes.
- Solo hay **uno en vuelo** a la vez, así el ACK que llega siempre corresponde a
  `pendientes[0]`.
- Código 0–3 (definitivo): se muestra y se quita de la cola.
- Código 4 `REINTENTAR` o sin ACK: se muestra una sola vez, se pasa al final de la
  cola y se reintenta cada `FICHAJE_REINTENTO_MS` (30 s). El servidor no duplica el
  fichaje si ya lo había guardado.
- Red de seguridad: si el nodo pierde la asociación con un PUSH en vuelo (LoRaLAN lo
  descarta sin avisar) o pasan 20 s, se libera para volver a intentarlo.

En `loop()` hay además un filtro: la **misma tarjeta** no se reenvía por radio antes
de 60 s (`MISMA_TARJETA_MS`), para no gastar tiempo de aire. Y el lector NFC se pausa
3 s tras cada lectura.

### 4. Mensajes (`AppProtocol.h`)

Cabecera sin dependencias de Arduino, compartida con el gateway. Byte 0 = tipo;
números multibyte en big-endian:

| Dirección | Tipo | Contenido |
|---|---|---|
| nodo → gw | `01 FICHAJE` | UID NFC (4–10 bytes). Va en un PUSH con ACK diferido |
| nodo → gw | `02 SENSORES` | temp ×100 (i16), humedad ×100, presión ×10, lux, presencia |
| nodo → gw | `03 ESTADO_RELES` | bit0 relé 1, bit1 relé 2 |
| gw → nodo | `10 RELE` | número (1–2), on (0/1) |
| gw → nodo | `11 RESET` | — |

Un sensor sin lectura se codifica como un valor centinela (`INT16_MIN` / `0xFFFF`) y
se decodifica otra vez como NAN. Hay funciones `encodeX` / `decodeX` para cada tipo;
las `decode` comprueban longitud y rango.

### 5. Sensores e indicadores

Los módulos de sensores siguen el mismo patrón: `setupX()` con 3 reintentos que marca
`sensores.X_ok`, y `leerX()` que, si el sensor falló, intenta reiniciarlo y valida los
rangos del datasheet (valor fuera de rango → lectura no válida). Los relés son
**activos en LOW** (LOW = encendido) y arrancan apagados.

## Configuración (`config.h`)

- `LORALAN_NODE_ID`: `0` (por defecto) = dirección dinámica, la elige el gateway entre
  `0x80` y `0xFE` (de arriba abajo); 1–127 = manual, **distinta en cada placa**. La asignada
  se guarda en NVS y se vuelve a pedir. En el servidor el nodo 2 es `node-002`.
- Radio y `LORALAN_APP_KEY` deben coincidir con el gateway.
- SX1276: CS=15, RST=13, DIO0=4. Comparte el bus SPI (18/19/23) con el PN532 (CS=5);
  el core de ESP32 serializa las transacciones SPI entre tareas.
- I2C (21/22) compartido por BME280, BH1750 y OLED. Radar en UART2 (16/17).

## Compilar

Con la biblioteca LoRaLAN **del repo** (la copia de `Documents/Arduino/libraries` está
desfasada):

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 \
  --library ../../IMPLEMENTACIONMIA/libraries/LoRaLAN \
  --libraries ~/Documents/Arduino/libraries .
```

Bibliotecas: RadioLib, Adafruit PN532, Adafruit BME280, BH1750, ld2410, Adafruit SSD1306/GFX.
