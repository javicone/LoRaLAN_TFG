# LoRaLAN

Biblioteca Arduino/C++ del protocolo **LoRaLAN**: una red local en estrella sobre LoRa,
con un gateway y hasta 32 nodos, asociación, sondeo, eventos, comandos, acuses de recibo y
seguridad (AES-128-CTR y MIC AES-CMAC). Funciona sobre
[RadioLib](https://github.com/jgromes/RadioLib) con cualquier radio de su `PhysicalLayer`
(aquí, SX1276).

La misma biblioteca, sin modificar, se compila para el ESP32 (nodo) y para Linux en la
Raspberry Pi (gateway), que le aporta su propio `Arduino.h`.

Junto a ella están el simulador de PC [`../sim_LoRaLAN`](../sim_LoRaLAN/README.md) y una
guía para asistentes de IA en [`skills/loralan/SKILL.md`](skills/loralan/SKILL.md).

## Ficheros de `src/`

| Fichero | Qué contiene |
|---|---|
| `LoRaLAN.h` | Cabecera única |
| `LoRaLANPacket*` | Formato de trama: construcción, lectura y MIC |
| `LoRaLANCrypto.h` | AES, CMAC y derivación de claves (sobre mbedTLS) |
| `LoRaLANManager.h` | Cifrado, secuencia, envío con espera de ACK y reintentos |
| `LoRaLANDevice.*` | Base común: recepción, autenticación, ACK, CAD y anti-repetición |
| `LoRaLANGateway.*` | Rol gateway |
| `LoRaLANNode.*` | Rol nodo |

## Cómo funciona

- **Asociación (JOIN)**: el nodo envía su identificador de fábrica (la MAC) y un número
  aleatorio; el gateway le asigna una dirección y ambos derivan una clave de sesión a
  partir de la clave maestra común (`APPKEY`). Las direcciones 1–127 son manuales y las
  128–254 las reparte el gateway.
- **Sondeo (POLL)**: el gateway pregunta por turnos a cada nodo cada `pollIntervalMs`; el
  nodo responde con sus datos. Un nodo que no responde a varios sondeos seguidos se marca
  inactivo.
- **Eventos (PUSH)**: el nodo envía datos sin esperar turno, tras escuchar que el canal
  está libre (CAD).
- **Comandos (CMD)**: órdenes cifradas del gateway a un nodo.
- **Hora (TIME_SYNC)**: el gateway difunde su reloj periódicamente.
- **ACK y reintentos**: PUSH, CMD y LEAVE se confirman con un ACK; sin él, se repiten
  hasta un total de 3 emisiones, esperando 1200 ms cada vez. El ACK puede llevar una respuesta corta de la
  aplicación: con `deferredPushAck`, el gateway la envía con `ackPush()` cuando la tiene
  (en este proyecto, el resultado del fichaje).

## Trama

```
| cabecera (7 B) | payload (0–64 B) | MIC (4 B) |
```

La cabecera lleva versión, tipo, prioridad, flags, red, origen, destino y un número de
secuencia de 16 bits. Tipos: PUSH_EVENT, ACK, POLL, POLL_RESPONSE, TIME_SYNC,
JOIN_REQUEST, JOIN_ACCEPT, LEAVE y CMD.

## Seguridad

- Cada trama lleva un MIC (CMAC-AES-128 truncado a 4 B) que se comprueba antes de
  procesarla: con la `APPKEY` en JOIN y difusiones, y con la clave de sesión en el resto.
- El payload de PUSH_EVENT, POLL_RESPONSE y CMD va cifrado con AES-128-CTR.
- Las tramas repetidas se descartan por su número de secuencia.
- Sin mbedTLS no hay cifrado ni MIC y la biblioteca no forma sesión. En el ESP32 viene con
  el core; en Linux hace falta `libmbedtls-dev`.

**Limitaciones**: si se filtra la `APPKEY` cae toda la red; el gateway solo rechaza el
último JOIN de cada nodo, no los antiguos; la tabla de nodos vive en RAM y se pierde al
reiniciar el gateway.

## Uso

La aplicación inicializa la radio, entrega cada trama recibida con `handleReceived()` y
llama a `process()` en cada vuelta. Ejemplo de gateway (recortado de
`gatewayLoRaLAN/src/main.cpp`):

```cpp
#include <RadioLib.h>
#include <LoRaLAN.h>

LoRaLANGateway gateway(radio);              // radio: SX1276 ya inicializada

void onPushEvent(const LoRaLANFrame& f, const uint8_t* payload, uint8_t len) {
    uint8_t resultado = 0;
    gateway.ackPush(f.header.srcId, f.header.seq, &resultado, 1);   // respuesta en el ACK
}

void setup() {
    LoRaLANGateway::Config cfg;
    cfg.netId = 0x42;
    cfg.appKey = APP_KEY;                   // uint8_t[16]
    cfg.pollIntervalMs = 5000;
    cfg.deferredPushAck = true;
    gateway.begin(cfg);
    gateway.setOnPushEvent(onPushEvent);
    radio.startReceive();
}

void loop() {
    gateway.process();
    if (radio.checkIrq(RADIOLIB_IRQ_RX_DONE) == 1) {   // mirar la IRQ antes de leer
        uint8_t buf[LORALAN_HEADER_SIZE + LORALAN_MAX_PAYLOAD + LORALAN_MIC_SIZE];
        size_t len = radio.getPacketLength();
        if (len > 0 && len <= sizeof(buf) && radio.readData(buf, len) == RADIOLIB_ERR_NONE)
            gateway.handleReceived(buf, len);
        radio.startReceive();
    }
}
```

El nodo (`LoRaLANNode`) se usa igual: `begin()` con su `Config` (red, `APPKEY`, MAC), el
mismo bucle de recepción y `queuePushEvent()` para enviar eventos cuando `isJoined()`.
Ver `Device_ESP32_LoRaLAN/lora_node.cpp`.

**Ciclo de trabajo**: la biblioteca no limita el tiempo en el aire. El `pollIntervalMs` de
2000 ms por defecto excede el 1 % en SF7; este proyecto usa 869,525 MHz (sub-banda del
10 %) y sondea cada 5000 ms.
