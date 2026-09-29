#include "LoRaLANPacket.h"
#include "LoRaLANPacketInternal.h"

namespace {

constexpr uint8_t LORALAN_FLAGS_MASK =
    static_cast<uint8_t>(Flags::ACK_REQUIRED) |
    static_cast<uint8_t>(Flags::IS_RETRY);

bool isValidMsgType(uint8_t type) {
    switch (static_cast<MsgType>(type)) {
        case MsgType::PUSH_EVENT:
        case MsgType::ACK:
        case MsgType::POLL:
        case MsgType::POLL_RESPONSE:
        case MsgType::TIME_SYNC:
        case MsgType::JOIN_REQUEST:
        case MsgType::JOIN_ACCEPT:
        case MsgType::LEAVE:
        case MsgType::CMD:
            return true;
        default:
            return false;
    }
}

bool isValidPriority(uint8_t priority) {
    return priority <= static_cast<uint8_t>(Priority::PRIO_HIGH);
}

bool hasOnlyKnownFlags(uint8_t flags) {
    return (flags & static_cast<uint8_t>(~LORALAN_FLAGS_MASK)) == 0;
}

} // namespace

// Construye un paquete en wire format y anade el MIC.
bool LoRaLANPacket::build(
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
) {
    if (buffer == nullptr) {
        return false;
    }

    if (payloadLen > LORALAN_MAX_PAYLOAD) {
        return false;
    }

    if (payloadLen > 0 && payload == nullptr) {
        return false;
    }

    const uint8_t typeValue = static_cast<uint8_t>(type);
    const uint8_t prioValue = static_cast<uint8_t>(priority);

    if (!isValidMsgType(typeValue) || !isValidPriority(prioValue) || !hasOnlyKnownFlags(flags)) {
        return false;
    }

    LoRaLANHeader header = {};
    header.version = LORALAN_VERSION;
    header.netId = netId;
    header.srcId = srcId;
    header.dstId = dstId;
    header.type = typeValue;
    header.priority = prioValue;
    header.flags = flags;
    header.seq = seq;
    header.payloadLen = payloadLen;

    // Serializar cabecera
    serializeHeader(header, buffer);

    // Copiar payload si existe
    if (payloadLen > 0 && payload != nullptr) {
        memcpy(buffer + LORALAN_HEADER_SIZE, payload, payloadLen);
    }

    // Hueco del MIC a cero: lo rellena applyMic() con la clave del interlocutor.
    memset(buffer + LORALAN_HEADER_SIZE + payloadLen, 0, LORALAN_MIC_SIZE);

    // Calcular longitud total del paquete
    totalLen = LORALAN_HEADER_SIZE + payloadLen + LORALAN_MIC_SIZE;

    return true;
}

// Parsea y valida un buffer de paquete recibido.
bool LoRaLANPacket::parse(
    const uint8_t* buffer,
    uint16_t bufferLen,
    LoRaLANFrame& frame
) {
    if (buffer == nullptr) {
        return false;
    }

    // Verificar tamano minimo
    if (bufferLen < LORALAN_HEADER_SIZE + LORALAN_MIC_SIZE) {
        return false;
    }

    // Copiar la cabecera
    deserializeHeader(buffer, frame.header);

    // Verificar longitud del payload
    if (frame.header.payloadLen > LORALAN_MAX_PAYLOAD) {
        return false;
    }

    // Verificar que el buffer tiene suficiente espacio
    const uint16_t expectedLen = LORALAN_HEADER_SIZE + frame.header.payloadLen + LORALAN_MIC_SIZE;
    if (bufferLen != expectedLen) {
        return false;
    }

    if (frame.header.version != LORALAN_VERSION) {
        return false;
    }

    if (!isValidMsgType(frame.header.type)) {
        return false;
    }

    if (!isValidPriority(frame.header.priority)) {
        return false;
    }

    if (!hasOnlyKnownFlags(frame.header.flags)) {
        return false;
    }

    // Copiar payload
    if (frame.header.payloadLen > 0) {
        memcpy(
            frame.payload,
            buffer + LORALAN_HEADER_SIZE,
            frame.header.payloadLen
        );
    }

    // Copiar MIC (little-endian)
    frame.mic =
        static_cast<uint32_t>(buffer[LORALAN_HEADER_SIZE + frame.header.payloadLen]) |
        (static_cast<uint32_t>(buffer[LORALAN_HEADER_SIZE + frame.header.payloadLen + 1]) << 8) |
        (static_cast<uint32_t>(buffer[LORALAN_HEADER_SIZE + frame.header.payloadLen + 2]) << 16) |
        (static_cast<uint32_t>(buffer[LORALAN_HEADER_SIZE + frame.header.payloadLen + 3]) << 24);

    // El MIC NO se valida aqui: es un CMAC y la clave depende del interlocutor,
    // que solo se conoce tras leer la cabecera. La comprobacion la hace la capa
    // de protocolo con LoRaLANPacket::checkMic() antes de procesar la trama.
    // parse() se limita a la validacion estructural.
    return true;
}
