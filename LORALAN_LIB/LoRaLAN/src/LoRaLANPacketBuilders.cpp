#include "LoRaLANPacket.h"

// Constructor ACK (payload de aplicacion opcional, en claro).
bool LoRaLANPacket::buildAck(
    uint8_t* buffer,
    uint8_t netId,
    uint8_t srcId,
    uint8_t dstId,
    uint16_t seqToAck,
    uint16_t& totalLen,
    const uint8_t* payload,
    uint8_t payloadLen
) {
    return build(
        buffer,
        netId,
        srcId,
        dstId,
        MsgType::ACK,
        Priority::PRIO_HIGH,
        0,  // Sin flags especiales
        seqToAck,
        payload,
        payloadLen,
        totalLen
    );
}

// Constructor POLL. No pide ACK: la POLL_RESPONSE ya acredita que llego. Si
// lo pidiera, el nodo enviaria ese ACK justo cuando el gateway confirma la
// respuesta y, al ser la radio half-duplex, perderia esa confirmacion y
// retransmitiria la respuesta en cada sondeo.
bool LoRaLANPacket::buildPoll(
    uint8_t* buffer,
    uint8_t netId,
    uint8_t srcId,
    uint8_t dstId,
    uint16_t seq,
    uint16_t& totalLen
) {
    return build(
        buffer,
        netId,
        srcId,
        dstId,
        MsgType::POLL,
        Priority::PRIO_LOW,
        0,  // Sin flags especiales
        seq,
        nullptr,
        0,
        totalLen
    );
}

// Constructor JOIN_REQUEST (broadcast, requiere ACK).
bool LoRaLANPacket::buildJoinRequest(
    uint8_t* buffer,
    uint8_t netId,
    uint8_t srcId,
    uint16_t devNonce,
    const uint8_t hwId[LORALAN_HWID_SIZE],
    uint16_t seq,
    uint16_t& totalLen
) {
    uint8_t payload[2 + LORALAN_HWID_SIZE] = {
        static_cast<uint8_t>((devNonce >> 8) & 0xFF),
        static_cast<uint8_t>(devNonce & 0xFF)
    };
    memcpy(payload + 2, hwId, LORALAN_HWID_SIZE);

    return build(
        buffer,
        netId,
        srcId,
        0xFF,
        MsgType::JOIN_REQUEST,
        Priority::PRIO_MEDIUM,
        static_cast<uint8_t>(Flags::ACK_REQUIRED),
        seq,
        payload,
        sizeof(payload),
        totalLen
    );
}

// Constructor JOIN_ACCEPT (requiere ACK).
bool LoRaLANPacket::buildJoinAccept(
    uint8_t* buffer,
    uint8_t netId,
    uint8_t srcId,
    uint8_t dstId,
    uint8_t assignedId,
    uint16_t devNonce,
    uint8_t status,
    const uint8_t hwId[LORALAN_HWID_SIZE],
    uint16_t seq,
    uint16_t& totalLen
) {
    uint8_t payload[4 + LORALAN_HWID_SIZE] = {
        assignedId,
        static_cast<uint8_t>((devNonce >> 8) & 0xFF),
        static_cast<uint8_t>(devNonce & 0xFF),
        status
    };
    memcpy(payload + 4, hwId, LORALAN_HWID_SIZE);

    return build(
        buffer,
        netId,
        srcId,
        dstId,
        MsgType::JOIN_ACCEPT,
        Priority::PRIO_MEDIUM,
        static_cast<uint8_t>(Flags::ACK_REQUIRED),
        seq,
        payload,
        sizeof(payload),
        totalLen
    );
}

// Constructor TIME_SYNC (broadcast).
bool LoRaLANPacket::buildTimeSync(
    uint8_t* buffer,
    uint8_t netId,
    uint8_t srcId,
    uint32_t timestamp,
    uint16_t seq,
    uint16_t& totalLen
) {
    // Convertimos el timestamp a bytes
    uint8_t payload[4];
    payload[0] = (timestamp >> 24) & 0xFF;
    payload[1] = (timestamp >> 16) & 0xFF;
    payload[2] = (timestamp >> 8) & 0xFF;
    payload[3] = timestamp & 0xFF;

    return build(
        buffer,
        netId,
        srcId,
        0xFF,  // Broadcast para TIME_SYNC
        MsgType::TIME_SYNC,
        Priority::PRIO_MEDIUM,
        0,  // Sin flags especiales
        seq,
        payload,
        sizeof(payload),
        totalLen
    );
}
