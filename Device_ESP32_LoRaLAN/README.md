# Device_ESP32_LoRaLAN

Firmware del nodo que se instala en cada aula: un ESP32 con lector NFC PN532, sensores
(BME280, BH1750 y radar LD2410), dos relés, LEDs, buzzer, pantalla OLED y una radio SX1276
con la que habla LoRaLAN con el gateway. No usa WiFi.

El nodo solo envía el UID de la tarjeta; el gateway le devuelve el resultado:

| Indicación | Significado |
|---|---|
| Verde, «registrado» | Asistencia guardada |
| Amarillo, «Sin clase ahora» | No hay clase en esta aula a esta hora |
| Rojo, «Clase de otro profesor» | La clase es de otro profesor |
| Rojo, «Tarjeta no registrada» | Tarjeta desconocida o nodo sin aula asignada |
| Amarillo parpadeando, «se reenviará» | El servidor no respondió; se reintenta |

## Ficheros

| Fichero | Qué contiene |
|---|---|
| `Device_ESP32_LoRaLAN.ino` | `setup()` y `loop()`: NFC, resultado del fichaje, sensores y pantalla |
| `lora_node.cpp` | La radio: tarea en el core 0 y cola de fichajes pendientes |
| `AppProtocol.h` | Mensajes nodo ↔ gateway (compartido con el gateway) |
| `config.h` | Dirección del nodo, radio, clave, tiempos y pines |
| `globals.*` | Datos y objetos compartidos |
| `*_module.ino`, `setupSensores.ino` | Sensores, NFC y pantalla |
| `indicators.ino` | LEDs, buzzer y relés |
| `PINES_ESP32_DIAGRAMA.md` | Mapa de pines |

## Cómo funciona

- **Dos núcleos**: la radio corre en el core 0 y el resto (NFC, sensores, LEDs, pantalla)
  en `loop()`, en el core 1. Se comunican por colas, así un `delay()` de un LED nunca
  retrasa un ACK.
- **Fichajes**: se guardan hasta 8 y se envían de uno en uno. Si la respuesta es
  `REINTENTAR` o no llega, se reenvían cada 30 s. La misma tarjeta no se reenvía antes de
  60 s.
- **Sensores y relés**: van en la respuesta al sondeo del gateway; el estado de los relés,
  cuando cambia y cada 6 sondeos.
- **Órdenes**: el gateway puede encender o apagar los relés y reiniciar el nodo.

Mensajes de `AppProtocol.h` (byte 0 = tipo):

| Sentido | Tipo | Contenido |
|---|---|---|
| nodo → gateway | `01 FICHAJE` | UID de la tarjeta (4–10 bytes) |
| nodo → gateway | `02 SENSORES` | Temperatura, humedad, presión, luz y presencia |
| nodo → gateway | `03 ESTADO_RELES` | Estado de los dos relés |
| gateway → nodo | `10 RELE` | Relé (1–2) y encendido o apagado |
| gateway → nodo | `11 RESET` | — |

## Configuración (`config.h`)

- `LORALAN_NODE_ID`: `0` pide dirección al gateway; de 1 a 127 fija una manual, distinta
  en cada placa. La dirección se guarda en NVS. En el servidor, el nodo 2 es `node-002`.
- La radio y `LORALAN_APP_KEY` deben coincidir con las del gateway.
- Pines del SX1276: CS 15, RST 13, DIO0 4. Comparte el bus SPI con el PN532 (CS 5).

## Compilar

Con la biblioteca LoRaLAN del repo:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 \
  --library ../LORALAN_LIB/LoRaLAN \
  --libraries ~/Documents/Arduino/libraries .
```

Bibliotecas: RadioLib, Adafruit PN532, Adafruit BME280, BH1750, ld2410 y Adafruit
SSD1306/GFX.
