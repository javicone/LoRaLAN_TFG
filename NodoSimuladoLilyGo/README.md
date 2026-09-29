# NodoSimuladoLilyGo — nodo de aula simulado

## Qué hace

Un nodo de aula **sin sensores ni lector NFC**, para probar el sistema completo con
solo una placa LilyGo T3 V1.6 (ESP32 + SX1276). Para el gateway y el servidor es
indistinguible de un nodo real: usa los mismos mensajes (`AppProtocol.h`), la misma
radio y la misma APP_KEY. Es el nodo `3` (`node-003` en el servidor).

- **Sensores inventados**: temperatura, humedad, luz… con una onda senoidal lenta más
  ruido, y presencia aleatoria (70 %).
- **Fichajes** de tres formas:
  - escribiendo un UID en hexadecimal en el monitor serie (p. ej. `04A1B2C3` + Enter);
  - pulsando el botón BOOT (GPIO0): ficha el siguiente UID de `UIDS_SIMULADOS`;
  - automáticamente cada `AUTO_FICHAJE_MS` (2 min; `0` lo desactiva).
- **Relés simulados**: el relé 1 enciende el LED de la placa (GPIO25).
- Responde a `RESET` desde el panel reiniciándose.

Todo lo que pasa se imprime por el monitor serie a 115200.

## Ficheros

| Fichero | Qué contiene |
|---|---|
| `NodoSimuladoLilyGo.ino` | Todo el programa |
| `AppProtocol.h` | **Copia** de `../Device_ESP32_LoRaLAN/AppProtocol.h`. Si cambia el protocolo, hay que volver a copiarla |

## Cómo está programado

Es una versión reducida del nodo real, en un solo fichero y en un solo núcleo
(no hay sensores lentos que puedan bloquear la radio):

- `setup()`: arranca el SPI con los pines de la LilyGo (SCK 5, MISO 19, MOSI 27,
  CS 18, RST 14, DIO0 26), inicializa el SX1276 y configura `LoRaLANNode` con los
  mismos callbacks que el nodo real.
- `loop()` cada 2 ms:
  1. Sondea el flag `RX_DONE` por SPI y entrega la trama a `nodo.handleReceived()`.
  2. `nodo.process()`.
  3. `atenderEntradas()`: serie, botón (detecta el flanco de bajada) y fichaje automático.
  4. `enviarFichaje()`: si hay un fichaje activo y el nodo está asociado, lo manda como
     PUSH con `queuePushEvent(..., true)`.
- `onPushAck`: códigos 0–3 cierran el fichaje e imprimen el resultado; `REINTENTAR` o
  sin ACK lo reprograma a 30 s.
- `onPollRequest`: igual que el nodo real, relés si cambiaron (o cada 6 sondeos) y si no,
  sensores simulados.
- `onComando`: `RELE` actualiza las variables y el LED; `RESET` reinicia a 1 s.

Diferencia deliberada con el nodo real: **un solo fichaje pendiente** (el real guarda
8). Mientras hay uno en curso, los nuevos se rechazan con un aviso.

## Uso

1. Poner en `UIDS_SIMULADOS` UIDs que existan en `usuarios.uid_tarjeta` si se quieren
   ver fichajes OK.
2. Compilar con la biblioteca LoRaLAN del repo y RadioLib (placa: ESP32 Dev Module o
   TTGO LoRa32).
3. Con el gateway en marcha, el nodo hace el JOIN y el servidor lo da de alta como
   `node-003` con su primera trama. Asignarle un aula desde el panel de administrador.
