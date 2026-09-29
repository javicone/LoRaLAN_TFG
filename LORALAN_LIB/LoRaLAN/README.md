# LoRaLAN: Protocolo LPLAN Optimizado para Redes Locales

LoRaLAN es un protocolo de comunicaciones de red de area local inalambrica basado en la capa fisica (PHY) de LoRa.

Ha sido disenado como parte de un Trabajo de Fin de Grado (TFG) en Ingenieria de Telecomunicaciones / Telematica para ofrecer una alternativa determinista, de baja latencia y alta seguridad frente a implementaciones estandar como LoRaWAN, ideal para entornos de campus, edificios inteligentes y redes privadas.

## Tabla de Contenidos

- [Justificacion y Objetivos](#justificacion-y-objetivos)
- [Arquitectura del Sistema](#arquitectura-del-sistema)
- [Capa de Enlace y Acceso al Medio (MAC)](#capa-de-enlace-y-acceso-al-medio-mac)
- [Estructura de la Trama](#estructura-de-la-trama)
- [Ciberseguridad y Criptografia](#ciberseguridad-y-criptografia)
- [Implementacion y Hardware](#implementacion-y-hardware)

## Justificacion y Objetivos

Mientras que LoRaWAN sobresale en despliegues metropolitanos (esquema ALOHA, alta latencia, escalabilidad masiva), presenta deficiencias en entornos locales donde se requiere determinismo temporal y baja latencia (por ejemplo, sistemas de control de acceso o fichaje en tiempo real).

LoRaLAN resuelve esto mediante:

- Una topologia en estrella centralizada (Single-Gateway / Multi-Node).
- Un esquema MAC hibrido que combina multiplexacion por division de tiempo (TDM) para gestion rutinaria y deteccion de actividad de canal (CAD) para eventos asincronos criticos.
- Una carga util fragmentada de hasta 64 bytes para mantener el Time on Air (ToA) por debajo de los ~100 ms, minimizando la probabilidad de colision.

## Arquitectura del Sistema

El ecosistema LoRaLAN adopta un modelo de responsabilidades separadas para garantizar el rendimiento en tiempo real estricto (hard real-time):

- **Nodos (End-Devices):** Basados en microcontroladores de bajo consumo unidos a transceptores LoRa (ej. SX1276). Gestionan sensores, cifrado local y acceso al medio.

- **Gateway (Smart Bridge):** Implementado sobre una arquitectura Dual-Core (ESP32). Actua exclusivamente como puente traductor de Capa 2 (LoRa) a Capa 4/7 (TCP/WebSockets).
Aquí la idea es que el gateway sea un esp32 y que actúe de gateway en uno de sus cores y de puente conectado mediante ethernet o WiFi a internet para tener acceso al servidor o a donde sea que se envíen los datos. El puente deberá estar preparado para enviar paquetes MQTT, IP, WEBSOCKET. Según esté configurado.

- **Servidor Central:** Desplegado en hardware externo (Raspberry Pi, servidor local o cloud). Se comporta coomo un servidor normal. En nuestra implementación se comunicaba con los nodos por HTTP (información asíncrona como los fichajes y actuadores) y por WEBSOCKET (para la información en tiempo real)

## Capa de Enlace y Acceso al Medio (MAC)

El protocolo abandona el esquema ALOHA puro en favor de un sistema de prioridades y control de acceso hibrido.

### Gestion de Colisiones y Modos de Transmision

- **Modo POLL (TDM estricto):** El Gateway asigna ventanas de tiempo. Un nodo solo puede transmitir metricas rutinarias o heartbeats cuando recibe una peticion directa (Poll). Se soporta piggybacking para incluir datos pendientes en el acuse de recibo. El modo poll se podrá configurar para cambiar la frecuencia del poll, lo cual permite gestionar el TOA para no exceder el duty cycle permitido del 10% en la banda que estamos usando, la 869.5 MHz. Se debe adelantar unos ms el reloj en las tramas beacons para (DRIFT).

	Si en su ventan de poll un nodo no contesta se detectará que está inactivo. Sirve como Keep-alive.

- **Modo PUSH (asincrono con CAD):** Para eventos criticos (ej. fichaje de un usuario). El nodo verifica si el canal esta libre mediante Channel Activity Detection (CAD). Esta logica esta implementada y requiere validacion de campo con SX1276.

	- Si el canal esta ocupado: aplica un algoritmo de backoff exponencial aleatorio.
	- Si hay conflicto de transmision simultanea entre Gateway y Nodo, el Gateway tiene prioridad absoluta. y  los nodos se ponen a escuchar.
	- En caso de que el nodo o el gateway no respondan con ACK, se realiza el backoff exponencial con reintento N veces. Si no hay exito, el paquete se encola para poder enviarse en un poll. 

	- Los dispositivos deberían de estar disponibles en todo momento para recepción y emision de PUSH, salvo cuando trasmiten. 

### Sincronizacion

El Gateway emite tramas de tipo BEACON cada 60 (o mas) segundos dirigidas a la direccion de broadcast (0xFF). Estas tramas incluyen un timestamp y corrigen la desviacion de reloj (drift) de los osciladores de cristal de los nodos. En las ventanas de transmisión los nodos deben empezar a escuchar un poco antes de que se les otorge su slot de tiempo para escuchar ya que LoRa cuando emite paquete al principio envía un preambulo. Si no se detecta el preambulo entero, los trasnmisores no escuchan ese paquete como tal.

- -	1 beacon por minuto son 60 beacons por hora a 35 ms por beacon son 2,1s/h o 0.58% de duty cycle.

### Prioridades de Trafico

- **Prioridad Alta (2):** PUSH_EVENT, ACK y CMD.
- **Prioridad Media (1):** TIME_SYNC (beacon), JOIN_REQUEST, JOIN_ACCEPT y LEAVE.
- **Prioridad Baja (0):** POLL y POLL_RESPONSE.

El valor 3 queda reservado y se rechaza. Actualmente la prioridad viaja en la trama pero no altera el orden de transmision.

## Estructura de la Trama

La cabecera ocupa **7 bytes** en el aire. Version, tipo y prioridad van empaquetados en el byte 0.

| Byte | Bits | Campo | Descripcion / Funcion |
| --- | --- | --- | --- |
| 0 | 7-6 | Version | Version del protocolo (2 bits: actual 1, maximo 4 versiones). |
| 0 | 5-2 | Tipo | Tipo de mensaje (4 bits: maximo 16 tipos, usados 9: 0x01-0x0A salvo 0x05, reservado). |
| 0 | 1-0 | Prioridad | 2 bits: 0 baja, 1 media, 2 alta; 3 reservado. |
| 1 | 8 | Flags | ACK_REQUIRED 0x01, IS_RETRY 0x02; el resto de bits reservados (se rechazan). |
| 2 | 8 | Network ID | Identificador de la subred (0-255). Aisla colisiones logicas entre redes cercanas. |
| 3 | 8 | ID Origen | Direccion del emisor. El Gateway siempre es 0x00. |
| 4 | 8 | ID Destino | Direccion del receptor. Broadcast es 0xFF. |
| 5 | 8 | SEQ | Numero de secuencia (0-255). Control de duplicados y anti-replay. |
| 6 | 8 | Payload Len | Longitud del payload (0-64). |
| 7.. | | Payload | 0-64 bytes, cifrado con AES-128-CTR. |
| final | | MIC | 4 bytes: AES-CMAC truncado sobre cabecera + payload cifrado. |

Trama total: de 11 a 75 bytes.

En memoria, `struct LoRaLANHeader` guarda cada campo en su propio `uint8_t` (9 bytes) por comodidad, pero `serializeHeader` empaqueta version, tipo y prioridad con desplazamientos de bits al transmitir: `byte0 = (version << 6) | (tipo << 2) | prioridad`. Ejemplo: PUSH_EVENT (0x01), version 1, prioridad alta (2) → `0x46`.


## Ciberseguridad y Criptografia

El protocolo asume que el canal inalambrico es inherentemente hostil, garantizando confidencialidad, integridad y autenticidad.

- **Cifrado de flujo (AES-128-CTR):** Se cifra unicamente el payload. El modo CTR (Counter) no anade padding, manteniendo el tamano de la trama intacto y reduciendo la latencia de procesamiento.

- **Integridad (Encrypt-then-MAC):** Antes de descifrar, el receptor verifica el MIC. Si la firma criptografica no coincide, la trama se descarta inmediatamente, protegiendo al sistema de ataques de denegacion de servicio (DoS) por agotamiento de CPU.

**Autenticacion (Join Process) - IMPLEMENTADO**
Basado en una clave precompartida maestra (APPKEY 128 bits) entre gateways y nodos a partir de la cual se crea una clave de sesion por nodo.
	- El nodo envia un JOIN_REQUEST con un numero aleatorio (DevNonce) además de un ID genérico y su dirección MAC.
	- El Gateway verifica el MIC y ejecuta una Funcion de Derivacion de Claves (KDF).
	- Se genera una SessionKey unica y temporal por nodo con un coste de acceso en memoria de $O(1)$. Esto supone al gateway un coste de 4KB en memoria, lo cual no es mucho y nos permite tener un array que sea: 
     **arrayNodos[id_nodo] = sessionKey_nodo_id**
    Además, se le asigna a los nodos un id. Por lo que es como si estuvieramos haciendo un mini DHCP. Lo malo es que si se compromete la appkey se compromete la seguridad de toda la red.

	- Podríamos hacer un mecanismo hibrido que posea direccionamiento dinámico y direccionamiento estático pero aumentaría la complejidad y quizá no merezca la pena.

- **Anti-Replay:** Validado estrictamente mediante el emparejamiento criptografico del campo SEQ.

**Futuro (no implementado):** Se puede estudiar un JOIN con intercambio de claves (p. ej. Diffie-Hellman) para eliminar la dependencia total de la APPKEY. Esto no forma parte de la libreria actual.

## Implementacion y Hardware

La libreria incluye una capa de protocolo lista para usar en modo gateway y nodo mediante la clase `LoRaLANProtocol`, con JOIN, POLL, PUSH con CAD, y TIME_SYNC.

### Configuracion disponible en la libreria

- Rol y direccionamiento: `beginGateway`, `beginNode`, `begin(Role, ...)`, `netId`, `gatewayId`, `nodeId`, `nodeType`.
- JOIN: `setJoinRetryMs`, `requestJoin`, `resetJoin`, `setAppKey`.
- POLL y TIME_SYNC: `setPollIntervalMs`, `setTimeSyncIntervalMs`.
- Retransmision: `setTxRetryPolicy`, `setAppRetryPolicy`.
- CAD: `setCadPolicy` (habilitar/deshabilitar, backoff base, max exp, jitter, backoff por error, CAD en JOIN).

### Ejemplos rapidos de uso

**Gateway (ESP32 + SX1276)**

```cpp
#include <RadioLib.h>
#include <LoRaLAN.h>

SX1276 radio = new Module(15, 4, 13, RADIOLIB_NC);
LoRaLANProtocol protocol(radio);

const uint8_t APPKEY[16] = {0x42,0x19,0xA7,0x5C,0xD3,0x88,0x11,0xE4,0x2F,0x90,0x6A,0xC1,0x7B,0x54,0x0D,0xBE};

void setup() {
	// ... inicializa radio ...
	LoRaLANProtocol::GatewayConfig cfg;
	cfg.netId = 0x42;
	cfg.gatewayId = 0x00;
	cfg.appKey = APPKEY;
	protocol.begin(LoRaLANProtocol::Role::GATEWAY, &cfg, nullptr);
	protocol.setPollIntervalMs(2000);
	protocol.setTimeSyncIntervalMs(30000);
}

void loop() {
	// ... leer buffer de radio ...
	// protocol.handleReceived(buffer, len);
	protocol.process();
}
```

**Nodo (ESP32 + SX1276)**

```cpp
#include <RadioLib.h>
#include <LoRaLAN.h>

SX1276 radio = new Module(15, 4, 13, RADIOLIB_NC);
LoRaLANProtocol protocol(radio);

const uint8_t APPKEY[16] = {0x42,0x19,0xA7,0x5C,0xD3,0x88,0x11,0xE4,0x2F,0x90,0x6A,0xC1,0x7B,0x54,0x0D,0xBE};

void setup() {
	// ... inicializa radio ...
	LoRaLANProtocol::NodeConfig cfg;
	cfg.netId = 0x42;
	cfg.nodeId = 0x10;
	cfg.gatewayId = 0x00;
	cfg.nodeType = 0x01;
	cfg.appKey = APPKEY;
	protocol.begin(LoRaLANProtocol::Role::NODE, nullptr, &cfg);
	protocol.setJoinRetryMs(5000);
	protocol.setCadPolicy(true, 40, 5, 50, 60, true);
}

void loop() {
	// ... leer buffer de radio ...
	// protocol.handleReceived(buffer, len);
	protocol.process();
	if (protocol.isJoined()) {
		uint8_t payload[] = {0x01, 0x02};
		protocol.queuePushEvent(payload, sizeof(payload), true);
	}
}
```

### Gateway Dual-Core (FreeRTOS)

- **Core 1 (LoRaLAN Engine):** Dedicacion exclusiva a la interrupcion de radio LoRa, temporizadores TDM por hardware, calculo criptografico AES por aceleracion hardware y enrutamiento L2.
- **Core 0 (CommBridge Engine):** Gestion del stack TCP/IP, WiFi/Ethernet LwIP y cliente de WebSockets.

