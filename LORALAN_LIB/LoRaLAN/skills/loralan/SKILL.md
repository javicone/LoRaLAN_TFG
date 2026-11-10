---
name: loralan
description: Ayuda a integrar la biblioteca LoRaLAN (red local LoRa en estrella gateway/nodos sobre SX1276 + RadioLib, EU868) en un proyecto propio. Usar cuando se mencione LoRaLAN, LoRaLANGateway, LoRaLANNode, PUSH/POLL/CMD sobre LoRa, o cuando se quiera montar un gateway o un nodo LoRa con ESP32/Raspberry Pi y transportar por encima un protocolo de aplicación propio.
---

# LoRaLAN: integrar la biblioteca en tu sistema

LoRaLAN es una capa MAC propia sobre LoRa: estrella con un gateway (dirección `0x00`) y hasta 32 nodos (`LoRaLANGateway::kMaxNodes`). Cada trama lleva hasta 64 B de payload (`LORALAN_MAX_PAYLOAD`) y un MIC AES-CMAC truncado a 4 B. El payload de PUSH_EVENT, POLL_RESPONSE y CMD va cifrado con AES-128-CTR; el de ACK, JOIN y TIME_SYNC va en claro, autenticado solo por el MIC. El nodo se asocia con JOIN usando una APPKEY compartida, de la que el gateway deriva una clave de sesión por nodo; el nodo envía su identificador de fábrica (`hwId`, p. ej. la MAC) y el gateway le asigna dirección.

**El payload es opaco**: LoRaLAN no impone formato. Tu protocolo de aplicación (tipo de mensaje en el primer byte, TLV, CBOR…) va dentro.

**Regla de oro**: no inventes métodos. La API completa está en `src/LoRaLANGateway.h`, `src/LoRaLANNode.h` y `src/LoRaLANDevice.h`; léelos antes de proponer código. No existen mesh, salto de frecuencia, OTA, estadísticas ni la antigua clase `LoRaLANProtocol`.

## Radio y bucle de recepción (igual en gateway y nodo)

La biblioteca no lee la radio: el sketch sondea `RX_DONE` y entrega la trama. Patrón probado en hardware:

```cpp
#include <RadioLib.h>
#include <LoRaLAN.h>

SX1276 radio = new Module(15, 4, 13, RADIOLIB_NC);   // CS, DIO0, RST, DIO1 (no hace falta)

// setup():
radio.begin(869.525, 125.0, 7, 5, 0x12, 14, 8);      // MHz, kHz, SF, CR 4/5, sync, dBm, preámbulo
// ... begin() del gateway o del nodo ...
radio.startReceive();

// bucle de radio:
uint8_t buf[LORALAN_HEADER_SIZE + LORALAN_MAX_PAYLOAD + LORALAN_MIC_SIZE];
if (radio.checkIrq(RADIOLIB_IRQ_RX_DONE) == 1) {      // mirar la IRQ primero
  size_t len = radio.getPacketLength();
  if (len > 0 && len <= sizeof(buf) && radio.readData(buf, len) == RADIOLIB_ERR_NONE)
    dispositivo.handleReceived(buf, len);
  radio.startReceive();                               // siempre, tras cada operación
}
dispositivo.process();
```

- Sin `checkIrq` previo, el SX127x devuelve otra vez la longitud del último paquete y se reprocesa la misma trama.
- No pongas ISR en DIO0: el CAD también lo usa y saltaría en falso.
- Todos los equipos de la red deben compartir frecuencia, SF, BW, CR, sync word, `netId` y APPKEY.

## Gateway mínimo

```cpp
LoRaLANGateway gateway(radio);

void onPush(const LoRaLANFrame& f, const uint8_t* p, uint8_t n) { /* encolar (f.header.srcId, p, n) */ }
void onPollResp(const LoRaLANFrame& f, const uint8_t* p, uint8_t n) { /* encolar */ }
void onJoined(uint8_t nodeId) { }

// setup():
LoRaLANGateway::Config cfg;
cfg.netId = 0x42;
cfg.gatewayId = 0x00;
cfg.appKey = APPKEY;                 // uint8_t[16]
cfg.pollIntervalMs = 5000;           // ver "ciclo de trabajo"; 0 = sin POLL
cfg.timeSyncIntervalMs = 30000;      // 0 = sin TIME_SYNC
gateway.begin(cfg);
gateway.setOnPushEvent(onPush);
gateway.setOnPollResponse(onPollResp);
gateway.setOnNodeJoined(onJoined);   // también setOnNodeLeft, setOnNodeInactive
```

Otros: `sendCommand(nodeId, payload, len)`, `nodeCount()`, `isAlive(nodeId, maxAgeMs)`, `nodeHwId(nodeId, out)`, `setMaxMissedPolls(n)`.

## Nodo mínimo

```cpp
LoRaLANNode node(radio);

void onAck(bool delivered, const uint8_t* ack, uint8_t ackLen) { }
void onCmd(const LoRaLANFrame& f, const uint8_t* p, uint8_t n) { }
bool onPoll(uint8_t* payload, uint8_t& len) { /* rellenar */ return false; }

// setup():
LoRaLANNode::Config cfg;
cfg.netId = 0x42;
cfg.appKey = APPKEY;
cfg.nodeId = 0;                      // preferida; 0 = cualquiera. La asignada: node.getConfig().nodeId
uint64_t mac = ESP.getEfuseMac();    // hwId único por nodo
for (int i = 0; i < LORALAN_HWID_SIZE; ++i) cfg.hwId[i] = uint8_t(mac >> (8 * i));
cfg.silenceTimeoutMs = 3UL * 32 * 5000;   // re-JOIN si el gateway calla; > nodos x pollIntervalMs
node.begin(cfg);
node.setOnPushAck(onAck);
node.setOnCommand(onCmd);
node.setOnPollRequest(onPoll);

// bucle, ya asociado:
if (node.isJoined()) node.queuePushEvent(data, len /*, ackRequired=true, fallbackToPoll=false */);
```

Guarda en NVS la dirección asignada (en `setOnJoinAccepted`) y pídela como `nodeId` tras reiniciar para conservarla.

## Cómo montar tu protocolo encima

| Necesidad | Mecanismo |
|---|---|
| Evento urgente nodo → gateway con confirmación | `queuePushEvent(..., true)` + `setOnPushAck`. Un PUSH en vuelo cada vez: espera al callback antes del siguiente. |
| Respuesta de aplicación en el propio ACK (p. ej. «aceptado/denegado» del servidor) | Gateway con `cfg.deferredPushAck = true`; al tener la respuesta, `ackPush(nodeId, frame.header.seq, resp, n)`. El nodo la recibe en `onAck(delivered, ack, ackLen)`. |
| Telemetría periódica sin coste de canal para el nodo | `setOnPollRequest` o `queuePollPayload`; llega al gateway por `setOnPollResponse`. |
| Orden gateway → nodo | `sendCommand` (cifrado, con ACK y reintentos) → `setOnCommand` en el nodo. |
| Que un PUSH fallido no se pierda | `fallbackToPoll = true`: se reenvía en la siguiente ventana POLL. |

Pon un byte de tipo al inicio del payload y valida longitudes al decodificar: el payload llega descifrado pero su contenido es responsabilidad tuya.

## Reglas que muerden

- **Plazo del ACK**: el nodo espera 1200 ms (`appAckTimeoutMs`). Los callbacks se ejecutan en el hilo de radio y **solo deben encolar**; nunca HTTP, MQTT, base de datos, `delay()` ni pantalla dentro de ellos. Con `deferredPushAck`, responde con `ackPush` antes del plazo o el nodo reintentará.
- **Ciclo de trabajo (EU868)**: 869,525 MHz está en la sub-banda del 10 %. El POLL periódico es el mayor consumo del gateway: tiempo en el aire × sondeos/hora no puede superar el límite que te impongas. El `pollIntervalMs = 2000` por defecto es demasiado agresivo; el gateway de referencia usa 5000 ms y se niega a arrancar si el cálculo excede el límite. Haz tú la misma comprobación (fórmula de ToA de Semtech) en lugar de rebajarla.
- **El gateway transmite sin CAD**; los nodos usan CAD asíncrono con backoff (`CadConfig`). Si tocas el CAD: canal libre es `RADIOLIB_CHANNEL_FREE (-15)`, no `0`.
- **`silenceTimeoutMs`** debe superar con holgura una vuelta de sondeo completa; si es menor, los nodos se re-asocian sin motivo.
- **Seguridad**: toda la red comparte la APPKEY; si se filtra de un nodo, cae la red entera. No la subas a repositorios públicos. Las difusiones (TIME_SYNC) se autentican con la APPKEY, no con una clave del gateway.
- **No modifiques la biblioteca por plataforma**: para PC/Linux se aporta un `Arduino.h` propio, sin `#ifdef` dentro de `src/`.

## Probar sin hardware

Junto a la biblioteca está el simulador de PC `../sim_LoRaLAN`: compila `src/` sin modificarla, con gateway y nodos en un proceso, reloj virtual y canal con pérdidas y colisiones. El tiempo en el aire sale de la fórmula de Semtech, pero el AES y el CMAC son falsos y no hay CAD ni alcance reales, así que sirve para la lógica del protocolo; cifrado, alcance y tiempos medidos solo se validan en hardware.

```
cd ../sim_LoRaLAN                               # con el g++ de MSYS2 en el PATH (Windows)
cmake -G "MinGW Makefiles" -S . -B build
cmake --build build
build/sim_tests --all --out results             # batería de 87 casos
build/sim_tests --case U6 --seed 7              # un caso, reproducible
python experiments/sweep.py --seeds 30          # experimentos VAL, EXP1, EXP4 y EXP5
```

Si cambias la biblioteca, pasa la batería: no debe aparecer ningún fallo nuevo frente a `results/casos.csv`.

Las latencias son multimodales (múltiplos de 1200 ms por reintentos): reporta mediana y percentiles, no la media.
