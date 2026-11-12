# MonitorCardputer

Analizador de bolsillo de la red LoRaLAN en un M5Stack Cardputer con el módulo Cap
LoRa-1262 (SX1262). **Solo escucha**: nunca transmite, así que no altera la red.

- **Sin claves** ve todas las tramas (tipo, origen, destino, secuencia, RSSI y SNR) y el
  resultado de cada fichaje, porque el ACK va en claro.
- **Con la APP_KEY**, si oye el JOIN de un nodo calcula su clave de sesión y a partir de ahí
  descifra sensores, UIDs y órdenes, y comprueba el MIC. Si se enciende después del JOIN,
  basta con reiniciar el nodo desde el panel.
- **Avisa** de nodos que no responden, MIC incorrecto, secuencias repetidas, tramas de
  otra red y ciclo de trabajo por encima del 10 % (sub-banda de 869,525 MHz).

Tres pantallas (teclas `1`, `2`, `3`; `;` y `.` para desplazar): resumen de la red, una
tarjeta por nodo y registro de eventos (que también sale por el puerto serie).

## Ficheros

| Fichero | Qué contiene |
|---|---|
| `MonitorCardputer.ino` | Todo el programa |
| `AppProtocol.h` | Copia de `../Device_ESP32_LoRaLAN/AppProtocol.h` |

## Compilar

Placa M5Cardputer (ESP32-S3) con las bibliotecas M5Cardputer, RadioLib y LoRaLAN (la del
repo). La radio usa los mismos parámetros que la red: 869,525 MHz, SF7, 125 kHz, CR 4/5.
