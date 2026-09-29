# MonitorCardputer — monitor pasivo de la red LoRaLAN

## Qué hace

Convierte un M5Stack Cardputer con el módulo Cap LoRa-1262 (SX1262) en un analizador
de la red de bolsillo. **Solo escucha: nunca transmite**, así que no altera la red ni
gasta ciclo de trabajo.

- **Sin claves** ve todas las tramas: tipo, origen, destino, número de secuencia,
  reintentos, RSSI y SNR, y el **resultado de cada fichaje** (el código del ACK va en claro).
- **Con la APP_KEY** (compilada en el sketch), si escucha el JOIN de un nodo calcula su
  clave de sesión igual que el gateway, y a partir de ahí **descifra** los sensores, los
  UID de las tarjetas y las órdenes de relés, y **verifica el MIC** de cada trama.
  Si el monitor se enciende después del JOIN, basta con reiniciar el nodo desde la web.
- **Alertas**: nodo que no responde a 3 POLL seguidos, MIC incorrecto, número de
  secuencia repetido (posible replay), tramas de otra red, y ciclo de trabajo por
  encima del 1 % en la última hora.

### Pantallas (teclas `1`, `2`, `3`; `;` y `.` para desplazar)

1. **Resumen**: tramas por minuto, nodos activos, tramas con CRC mal o ajenas, último
   TIME_SYNC y barras de ciclo de trabajo por equipo.
2. **Nodos**: una tarjeta por nodo con cobertura, RSSI/SNR, hace cuánto se oyó,
   sensores, presencia, relés y etiquetas (ACTIVO, CALLADO, CLAVE, MIC!).
3. **Log**: los últimos 60 eventos con hora y color (también salen por el puerto serie).

## Ficheros

| Fichero | Qué contiene |
|---|---|
| `MonitorCardputer.ino` | Todo el programa |
| `AppProtocol.h` | **Copia** de `../Device_ESP32_LoRaLAN/AppProtocol.h` para decodificar los mensajes |

## Cómo está programado

### Recepción

- Radio SX1262 en el bus HSPI (SCK 40, MISO 39, MOSI 14, CS 5, RST 3, DIO1 4, BUSY 6,
  TCXO 1,8 V), con `setDio2AsRfSwitch` y ganancia de recepción aumentada.
- Mismos parámetros que la red: 869,525 MHz, SF7, BW 125, CR 4/5, sync 0x12.
- Aquí sí se usa interrupción: `setPacketReceivedAction(alRecibir)` pone un flag, y
  `loop()` lee la trama, llama a `procesarTrama()` y vuelve a `startReceive()`.
  Una trama con CRC mal solo incrementa un contador.

### Análisis de cada trama (`procesarTrama`)

1. `LoRaLANPacket::parse()` de la biblioteca LoRaLAN separa cabecera y payload.
   Tramas de otra `netId` se cuentan y se ignoran.
2. Estadísticas del emisor: RSSI (y media móvil), nº de tramas, reintentos y tiempo en
   el aire (`radio.getTimeOnAir`) acumulado en una ventana de 60 minutos.
3. Huecos en el número de secuencia → tramas perdidas; misma secuencia sin flag de
   reintento → alerta de replay.
4. Clave: JOIN y broadcast usan la APP_KEY; el resto, la clave de sesión del nodo si se
   conoce. Si el MIC cuadra, descifra el payload con AES-128-CTR
   (`loralan_crypto::buildNonce` + `aes128CtrCrypt`).
5. Según el tipo:
   - `JOIN_ACCEPT`: obtiene el DevNonce y deriva la clave de sesión con
     `deriveSessionKey()` (CMAC de la APP_KEY).
   - `POLL`: cuenta sondeos sin respuesta.
   - `POLL_RESPONSE`: decodifica sensores o estado de relés.
   - `PUSH_EVENT`: guarda el UID del fichaje.
   - `ACK` del gateway con la misma secuencia que el PUSH: muestra el código del fichaje.
   - `CMD`: muestra la orden de relé o reset.

El estado de cada equipo vive en `Nodo nodos[256]`, indexado por el id LoRaLAN
(0 = gateway).

### Pantalla

Se dibuja cada 200 ms en un sprite de 240×135 (`M5Canvas`) y se vuelca de una vez, sin
parpadeo. Tema oscuro con paleta RGB565 propia; las funciones `tarjetaDato`,
`tarjetaNodo`, `pastilla` e `iconoSenal` componen la interfaz.

## Compilar

Arduino IDE / arduino-cli con placa **M5Cardputer** (ESP32-S3) y las bibliotecas
M5Cardputer, RadioLib y LoRaLAN (la del repo, que trae `LoRaLANCrypto.h`).
