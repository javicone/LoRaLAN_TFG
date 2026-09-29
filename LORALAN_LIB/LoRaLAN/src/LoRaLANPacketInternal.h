#ifndef LORALANPACKET_INTERNAL_H
#define LORALANPACKET_INTERNAL_H

#include "LoRaLANPacket.h"

// Empaqueta version/tipo/prioridad en un solo byte de control.
inline uint8_t packControlByte(uint8_t version, uint8_t type, uint8_t priority) {
    return static_cast<uint8_t>(((version & 0x03) << 6) | ((type & 0x0F) << 2) | (priority & 0x03));
}

// Desempaqueta el byte de control en campos de cabecera.
inline void unpackControlByte(uint8_t control, LoRaLANHeader& header) {
    header.version = static_cast<uint8_t>((control >> 6) & 0x03);
    header.type = static_cast<uint8_t>((control >> 2) & 0x0F);
    header.priority = static_cast<uint8_t>(control & 0x03);
}

// Serializa la cabecera al wire format (7 bytes).
inline void serializeHeader(const LoRaLANHeader& header, uint8_t* out) {
    out[0] = packControlByte(header.version, header.type, header.priority);
    out[1] = header.flags;
    out[2] = header.netId;
    out[3] = header.srcId;
    out[4] = header.dstId;
    out[5] = header.seq;
    out[6] = header.payloadLen;
}

// Deserializa el wire header en campos LoRaLANHeader.
inline void deserializeHeader(const uint8_t* in, LoRaLANHeader& header) {
    unpackControlByte(in[0], header);
    header.flags = in[1];
    header.netId = in[2];
    header.srcId = in[3];
    header.dstId = in[4];
    header.seq = in[5];
    header.payloadLen = in[6];
}

#endif // LORALANPACKET_INTERNAL_H
