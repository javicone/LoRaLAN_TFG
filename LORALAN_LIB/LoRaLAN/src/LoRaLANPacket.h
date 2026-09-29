#ifndef LORALANPACKET_H
#define LORALANPACKET_H

#include <Arduino.h>

// Version del protocolo en el wire format.
#define LORALAN_VERSION 0x01

// Limites del wire format.
#define LORALAN_MAX_PAYLOAD 64
#define LORALAN_HEADER_SIZE 7
#define LORALAN_MIC_SIZE 4

// Tipos de mensaje usados por la MAC.
enum class MsgType : uint8_t {
    PUSH_EVENT = 0x01,    // Prioridad Alta
    ACK = 0x02,          // Prioridad Alta
    POLL = 0x03,         // Prioridad Baja
    POLL_RESPONSE = 0x04, // Prioridad Baja
    // 0x05 reservado: era HELLO (alta sin claves), sustituido por JOIN
    TIME_SYNC = 0x06,    // Prioridad Media
    JOIN_REQUEST = 0x07, // Prioridad Media
    JOIN_ACCEPT = 0x08,  // Prioridad Media
    LEAVE = 0x09,        // Prioridad Media (baja limpia del nodo)
    CMD = 0x0A           // Prioridad Alta (comando gateway -> nodo)
};

// Niveles de prioridad para QoS.
enum class Priority : uint8_t {
    PRIO_LOW    = 0,
    PRIO_MEDIUM = 1,
    PRIO_HIGH   = 2   // maximo; el valor 3 (2 bits) queda reservado y se rechaza
};

// Flags almacenadas en la cabecera.
enum class Flags : uint8_t {
    ACK_REQUIRED = 0x01,
    IS_RETRY = 0x02
};

// Campos de la cabecera (decodificados del wire header).
struct __attribute__((packed)) LoRaLANHeader {
    uint8_t version;        // 1 byte completo
    uint8_t netId;          // 1 byte completo
    uint8_t srcId;          // ID origen
    uint8_t dstId;          // ID destino
    uint8_t type;           // 1 byte completo
    uint8_t priority;       // 1 byte completo
    uint8_t flags;          // 1 byte completo
    uint8_t seq;            // Número de secuencia
    uint8_t payloadLen;     // Longitud del payload
};

// Trama parseada con payload y MIC.
struct __attribute__((packed)) LoRaLANFrame {
    LoRaLANHeader header;
    uint8_t payload[LORALAN_MAX_PAYLOAD];
    uint32_t mic;           // MIC de 4 bytes para integridad
};

class LoRaLANPacket {
public:
    // Construye un buffer de trama en wire format.
    static bool build(
        uint8_t* buffer,
        uint8_t netId,
        uint8_t srcId,
        uint8_t dstId,
        MsgType type,
        Priority priority,
        uint8_t flags,
        uint8_t seq,
        const uint8_t* payload,
        uint8_t payloadLen,
        uint16_t& totalLen
    );

    // Parsea y valida un buffer recibido.
    static bool parse(
        const uint8_t* buffer,
        uint16_t bufferLen,
        LoRaLANFrame& frame
    );

    // Utilidades de MIC.
    //
    // El campo de 4 bytes al final de la trama es un CMAC-AES-128 truncado,
    // calculado con la clave correspondiente al interlocutor. build() deja el
    // hueco a cero; quien transmite debe llamar a
    // applyMic() con la clave adecuada antes de sacar la trama al aire, y
    // quien recibe debe llamar a checkMic() antes de procesarla.
    //
    // El motivo de separarlo de build()/parse() es que la clave depende del
    // interlocutor, y para saber quien es hay que leer antes la cabecera.

    // Recalcula el MIC de un buffer ya construido y lo escribe en su sitio.
    static bool applyMic(uint8_t* buffer, uint16_t totalLen, const uint8_t key[16]);

    // Verifica el MIC de un buffer recibido. Comparacion en tiempo constante.
    static bool checkMic(const uint8_t* buffer, uint16_t totalLen, const uint8_t key[16]);

    // Constructores de mensajes sin cifrar. PUSH_EVENT, POLL_RESPONSE y CMD
    // llevan payload cifrado y se construyen con LoRaLANManager::buildSecureFrame.
    // El ACK puede llevar un payload corto de aplicacion (p. ej. el resultado
    // de procesar el PUSH). Va en claro: el MIC con la clave de sesion ya lo
    // autentica y un codigo de resultado no es confidencial.
    static bool buildAck(
        uint8_t* buffer,
        uint8_t netId,
        uint8_t srcId,
        uint8_t dstId,
        uint8_t seqToAck,
        uint16_t& totalLen,
        const uint8_t* payload = nullptr,
        uint8_t payloadLen = 0
    );

    static bool buildPoll(
        uint8_t* buffer,
        uint8_t netId,
        uint8_t srcId,
        uint8_t dstId,
        uint8_t seq,
        uint16_t& totalLen
    );

    // Constructor JOIN_REQUEST con DevNonce.
    static bool buildJoinRequest(
        uint8_t* buffer,
        uint8_t netId,
        uint8_t srcId,
        uint8_t nodeType,
        uint16_t devNonce,
        uint8_t seq,
        uint16_t& totalLen
    );

    // Constructor JOIN_ACCEPT con ID asignado.
    static bool buildJoinAccept(
        uint8_t* buffer,
        uint8_t netId,
        uint8_t srcId,
        uint8_t dstId,
        uint8_t assignedId,
        uint16_t devNonce,
        uint8_t status,
        uint8_t seq,
        uint16_t& totalLen
    );

    // Constructor TIME_SYNC (beacon de sincronizacion).
    static bool buildTimeSync(
        uint8_t* buffer,
        uint8_t netId,
        uint8_t srcId,
        uint32_t timestamp,
        uint8_t seq,
        uint16_t& totalLen
    );

    static bool isAckRequired(const LoRaLANFrame& frame) {
        return (frame.header.flags & static_cast<uint8_t>(Flags::ACK_REQUIRED)) != 0;
    }
};

#endif // LORALANPACKET_H