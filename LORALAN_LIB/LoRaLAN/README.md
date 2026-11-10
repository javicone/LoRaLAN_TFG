# LoRaLAN

Biblioteca Arduino/C++ que implementa **LoRaLAN**, un protocolo de red local sobre la
capa física LoRa: una red en estrella con un gateway y varios nodos, con asociación,
sondeo periódico, eventos asíncronos, comandos, acuses de recibo y seguridad
(cifrado AES-128-CTR y MIC AES-CMAC). Está escrita sobre
[RadioLib](https://github.com/jgromes/RadioLib) y funciona con cualquier radio que
implemente su `PhysicalLayer` (en este proyecto, SX1276 en el nodo y en el gateway).

La misma biblioteca, sin modificar, se compila para ESP32 (nodo) y para Linux en la
Raspberry Pi (gateway, ver `gatewayLoRaLAN/`), que le proporciona su propio `Arduino.h`.

---

## Ficheros

| Fichero | Qué contiene |
|---|---|
| `LoRaLAN.h` | Cabecera única que incluye todo lo demás |
| `LoRaLANPacket.h` | Tipos de mensaje, prioridades, flags, cabecera y constructores de tramas |
| `LoRaLANPacketCore.cpp` | `build()` y `parse()`: serialización y validación estructural de la trama |
| `LoRaLANPacketBuilders.cpp` | Constructores de ACK, POLL, JOIN_REQUEST, JOIN_ACCEPT y TIME_SYNC |
| `LoRaLANPacketMic.cpp` | `applyMic()` y `checkMic()`: cálculo y verificación del MIC |
| `LoRaLANPacketInternal.h` | Empaquetado de la cabecera de 7 bytes |
| `LoRaLANCrypto.h` | AES-128-CTR, CMAC-AES-128, derivación de la clave de sesión y nonce (sobre mbedTLS) |
| `LoRaLANManager.h` | Cifrado de tramas, número de secuencia, envío fiable con espera de ACK y reintentos |
| `LoRaLANDevice.h/.cpp` | Base común de gateway y nodo: recepción, autenticación, ACK automático, CAD y anti-repetición |
| `LoRaLANGateway.h/.cpp` | Rol gateway: tabla de nodos, JOIN, POLL, TIME_SYNC, PUSH con ACK diferido y CMD |
| `LoRaLANNode.h/.cpp` | Rol nodo: JOIN, PUSH con CAD, respuesta al POLL, CMD, TIME_SYNC, LEAVE |

---

## Cómo funciona

### Direcciones

Cada red tiene un `netId` (1 byte). El gateway tiene su propia dirección (`gatewayId`,
`0x00` en este proyecto) y `0xFF` es difusión. Los nodos usan:

- **`0x01`–`0x7F`**: direcciones manuales, configuradas en el nodo.
- **`0x80`–`0xFE`**: direcciones dinámicas, que el gateway reparte de arriba abajo.

El gateway admite hasta **32 nodos** (`LoRaLANGateway::kMaxNodes`).

### Asociación (JOIN)

1. El nodo envía un `JOIN_REQUEST` a difusión con un `DevNonce` aleatorio de 16 bits,
   su identificador de fábrica (`hwId`, 6 bytes; en el ESP32 es la MAC) y como origen la
   dirección que prefiere (`0` = cualquiera). Antes escucha el canal (CAD).
2. El gateway busca el nodo por su `hwId`. Si es nuevo, le asigna la dirección pedida si
   está libre, o la dinámica libre más alta. Un nodo conocido conserva la suya mientras
   el gateway siga en marcha.
3. Ambos derivan la misma **clave de sesión**:
   `AES-CMAC(APPKEY, "LLK" | netId | dirección | DevNonce)`.
4. El gateway responde con `JOIN_ACCEPT` (dirección asignada, `DevNonce`, estado y
   `hwId`). El nodo comprueba que el `DevNonce` y el `hwId` son los suyos, adopta la
   dirección, deriva la clave y confirma con un ACK.

Si no lo consigue, el nodo repite el JOIN cada `joinRetryMs` (5 s por defecto) más una
parte aleatoria de hasta un cuarto de ese intervalo. Si deja de oír al gateway durante
`silenceTimeoutMs` (desactivado por defecto), da la sesión por perdida y vuelve a
asociarse. Con `leave()` el nodo envía un `LEAVE` y se da de baja.

### Sondeo (POLL)

El gateway envía un `POLL` a un nodo asociado cada `pollIntervalMs`, por turnos. El nodo
responde con un `POLL_RESPONSE` que lleva el payload encolado con `queuePollPayload()` o,
si no hay ninguno, el que genere el callback `setOnPollRequest()` (puede ir vacío).
Si un nodo no envía nada durante `maxMissedPolls` sondeos seguidos (3 por defecto), el
gateway lo marca inactivo y avisa con `setOnNodeInactive()`; cualquier trama posterior
lo reactiva.

### Eventos (PUSH)

El nodo envía un `PUSH_EVENT` con `queuePushEvent()` sin esperar a ser sondeado. Antes
de transmitir escucha el canal (CAD); si está ocupado espera
`baseBackoffMs · 2^n` (con `n` hasta `maxBackoffExp`) más un margen aleatorio de hasta
`jitterMs`, y desiste tras `maxAttempts` escuchas fallidas. Con `fallbackToPoll`, un
evento que no se entrega se reenvía en la siguiente respuesta a un POLL.

### Acuses de recibo y reintentos

Las tramas con el flag `ACK_REQUIRED` se confirman con un `ACK` que lleva la misma
secuencia. Lo llevan PUSH, CMD, LEAVE, JOIN_ACCEPT y las POLL_RESPONSE que lo piden.
En PUSH, POLL_RESPONSE, CMD y LEAVE, quien envía espera `appAckTimeoutMs` (1200 ms por
defecto) más un margen aleatorio de hasta 600 ms, y reintenta hasta `appMaxRetries`
veces (3), marcando la trama con el flag `IS_RETRY`. Solo hay un envío de este tipo en
curso a la vez.
El nodo conoce el desenlace de cada PUSH con `setOnPushAck()`.

El ACK puede llevar un **payload corto de aplicación**. Con `deferredPushAck = true`, el
gateway no responde el PUSH automáticamente: la aplicación lo hace con `ackPush()` cuando
tiene la respuesta (en este proyecto, el resultado del fichaje que devuelve el servidor).
Si el nodo retransmite porque perdió el ACK, el gateway le reenvía la misma respuesta
guardada sin entregar el evento dos veces a la aplicación.

### Comandos (CMD) y hora (TIME_SYNC)

- `sendCommand()` envía al nodo un `CMD` cifrado con su clave de sesión, con ACK y
  reintentos. El nodo lo recibe en `setOnCommand()`.
- Cada `timeSyncIntervalMs` (30 s por defecto) el gateway difunde un `TIME_SYNC` con su
  reloj (`millis()/1000`, en segundos); el nodo guarda la diferencia en `timeOffsetMs()`.

---

## Formato de la trama

```
| cabecera (7 B) | payload (0–64 B) | MIC (4 B) |
```

| Byte | Campo | Contenido |
|---|---|---|
| 0 | Control | versión (2 bits) · tipo (4 bits) · prioridad (2 bits) |
| 1 | Flags | `0x01` ACK_REQUIRED, `0x02` IS_RETRY |
| 2 | netId | Identificador de red |
| 3 | srcId | Dirección de origen |
| 4 | dstId | Dirección de destino (`0xFF` = difusión) |
| 5–6 | seq | Número de secuencia de 16 bits, little-endian |

La longitud del payload no viaja en la cabecera: la da la cabecera explícita de LoRa y
se deduce de la longitud total recibida. Una trama ocupa entre 11 y 75 bytes.

| Tipo | Código | Prioridad | Payload | Cifrado |
|---|---|---|---|---|
| PUSH_EVENT | `0x01` | alta | datos de aplicación | sí |
| ACK | `0x02` | alta | respuesta corta de aplicación (opcional) | no |
| POLL | `0x03` | baja | vacío | — |
| POLL_RESPONSE | `0x04` | baja | datos de aplicación (opcional) | sí |
| TIME_SYNC | `0x06` | media | hora del gateway (4 B, segundos) | no |
| JOIN_REQUEST | `0x07` | media | DevNonce (2 B) + hwId (6 B) | no |
| JOIN_ACCEPT | `0x08` | media | dirección + DevNonce + estado + hwId | no |
| LEAVE | `0x09` | media | vacío | — |
| CMD | `0x0A` | alta | comando de aplicación | sí |

`parse()` rechaza las tramas con otra versión, un tipo desconocido, una prioridad
reservada o flags desconocidos.

---

## Seguridad

- **Claves**: una clave maestra `APPKEY` de 128 bits compartida por el gateway y los
  nodos, y una clave de sesión por nodo derivada en cada JOIN.
- **MIC**: los 4 últimos bytes son un CMAC-AES-128 truncado sobre cabecera y payload.
  Las tramas JOIN y las de difusión se firman con la `APPKEY`; el resto, con la clave de
  sesión del nodo. El receptor verifica el MIC antes de descifrar o de tocar ningún
  estado, y compara en tiempo constante.
- **Cifrado**: el payload de PUSH_EVENT, POLL_RESPONSE y CMD va cifrado con AES-128-CTR,
  con la clave de sesión y un nonce formado por `netId`, `srcId` y `seq`.
- **Anti-repetición**: cada receptor guarda la última secuencia aceptada y un mapa de
  las 64 anteriores. Se acepta una secuencia nueva hasta media escala por delante, o una
  anterior aún no vista dentro de esas 64. Una trama repetida no se entrega a la
  aplicación, pero se vuelve a confirmar para que el emisor deje de reintentar. El gateway
  lo aplica a PUSH y POLL_RESPONSE de cada nodo, y el nodo a POLL y CMD del gateway.
  Cada JOIN reinicia la ventana.
- **Fallo en cerrado**: sin mbedTLS no hay CMAC ni AES; las funciones devuelven `false`
  y no se forma sesión ni se emiten tramas. En el ESP32 mbedTLS viene con el core; en
  Linux hace falta `libmbedtls-dev`.

### Limitaciones conocidas

- Si se compromete la `APPKEY`, se compromete toda la red: con ella se derivan todas las
  claves de sesión y se firman las tramas de difusión.
- Un `TIME_SYNC` lo puede falsificar cualquiera que tenga la `APPKEY`, por ejemplo un
  nodo comprometido.
- El gateway solo rechaza el **último** `DevNonce` de cada nodo. Un `JOIN_REQUEST`
  antiguo reinyectado vuelve a derivar aquella clave de sesión y reinicia la ventana de
  secuencias.
- La secuencia es de 16 bits. Pasada media escala, una trama capturada vuelve a parecer
  nueva, así que la clave de sesión debe renovarse (un nuevo JOIN) antes de llegar ahí.
- La tabla de nodos del gateway vive en RAM: si el gateway se reinicia, los nodos tienen
  que volver a asociarse (lo hacen solos con `silenceTimeoutMs`).

---

## Configuración

Valores por defecto de la biblioteca:

| Parámetro | Por defecto | Rol | Uso |
|---|---|---|---|
| `netId` | 0 | ambos | Identificador de red |
| `appKey` | — | ambos | Clave maestra de 16 bytes (obligatoria) |
| `appMaxRetries` | 3 | ambos | Reintentos por falta de ACK |
| `appAckTimeoutMs` | 1200 | ambos | Espera del ACK |
| `txMaxRetries` | 3 | ambos | Reintentos de radio de las tramas sin espera de ACK (POLL, ACK, JOIN, TIME_SYNC) |
| `txTimeoutMs` | 2500 | ambos | Plazo total de esos reintentos |
| `gatewayId` | 0 | ambos | Dirección del gateway |
| `pollIntervalMs` | 2000 | gateway | Intervalo de sondeo (0 = sin POLL) |
| `timeSyncIntervalMs` | 30000 | gateway | Intervalo de TIME_SYNC (0 = sin TIME_SYNC) |
| `deferredPushAck` | false | gateway | La aplicación responde los PUSH con `ackPush()` |
| `maxMissedPolls` | 3 | gateway | Sondeos sin respuesta para marcar un nodo inactivo (0 = no detectar) |
| `nodeId` | 0 | nodo | Dirección preferida (0 = cualquiera) |
| `hwId` | — | nodo | Identificador de fábrica único, 6 bytes |
| `joinRetryMs` | 5000 | nodo | Intervalo de reintento del JOIN (0 = sin JOIN automático) |
| `silenceTimeoutMs` | 0 | nodo | Silencio del gateway tras el que se repite el JOIN (0 = desactivado) |
| `cad` | ver `CadConfig` | nodo | Escucha previa y backoff de PUSH y JOIN |

**Ciclo de trabajo.** La biblioteca no limita el tiempo en el aire; lo hace la
aplicación. El `pollIntervalMs` de 2000 ms por defecto excede el 1 % en SF7, por lo que
el gateway de este proyecto trabaja en 869,525 MHz (sub-banda del 10 %), sondea cada
5000 ms y comprueba el ciclo de trabajo al arrancar (`gatewayLoRaLAN/src/main.cpp`).

---

## Uso

La aplicación crea la radio con RadioLib, entrega a la biblioteca cada trama recibida
con `handleReceived()` y llama a `process()` en cada vuelta del bucle. Recortado del
gateway (`gatewayLoRaLAN/src/main.cpp`) y del nodo (`Device_ESP32_LoRaLAN/lora_node.cpp`)
de este proyecto:

**Gateway**

```cpp
#include <RadioLib.h>
#include <LoRaLAN.h>

LoRaLANGateway gateway(radio);              // radio: SX1276 de RadioLib ya inicializada

void onPushEvent(const LoRaLANFrame& frame, const uint8_t* payload, uint8_t len) {
    // ... procesar el evento y responder dentro del ACK:
    uint8_t resultado = 0;
    gateway.ackPush(frame.header.srcId, frame.header.seq, &resultado, 1);
}

void setup() {
    LoRaLANGateway::Config cfg;
    cfg.netId = 0x42;
    cfg.gatewayId = 0x00;
    cfg.appKey = APP_KEY;                   // uint8_t[16]
    cfg.pollIntervalMs = 5000;
    cfg.deferredPushAck = true;
    gateway.begin(cfg);
    gateway.setOnPushEvent(onPushEvent);
    radio.startReceive();
}

void loop() {
    gateway.process();
    if (radio.checkIrq(RADIOLIB_IRQ_RX_DONE) == 1) {
        uint8_t buf[LORALAN_HEADER_SIZE + LORALAN_MAX_PAYLOAD + LORALAN_MIC_SIZE];
        size_t len = radio.getPacketLength();
        if (len > 0 && len <= sizeof(buf) && radio.readData(buf, len) == RADIOLIB_ERR_NONE) {
            gateway.handleReceived(buf, len);
        }
        radio.startReceive();
    }
}
```

**Nodo**

```cpp
#include <RadioLib.h>
#include <LoRaLAN.h>

LoRaLANNode nodo(radio);

void onPushAck(bool entregado, const uint8_t* ack, uint8_t ackLen) {
    // entregado = llegó el ACK; ack = respuesta de la aplicación del gateway
}

void setup() {
    LoRaLANNode::Config cfg;
    cfg.netId = 0x42;
    cfg.nodeId = 0;                         // dirección dinámica
    cfg.gatewayId = 0x00;
    cfg.appKey = APP_KEY;
    // cfg.hwId = MAC del ESP32 (ESP.getEfuseMac())
    nodo.begin(cfg);
    nodo.setOnPushAck(onPushAck);
    radio.startReceive();
}

void loop() {
    // recepción igual que en el gateway, con nodo.handleReceived(buf, len)
    nodo.process();
    if (nodo.isJoined()) {
        // nodo.queuePushEvent(datos, len);  // evento asíncrono con ACK
    }
}
```

En el SX1276 la recepción se comprueba con `checkIrq(RADIOLIB_IRQ_RX_DONE)` antes de
leer: el registro de longitud conserva la del último paquete y, sin esa comprobación, se
volvería a entregar la misma trama en cada vuelta.
