// Familia FORMATO. Sin red: construccion y lectura de tramas (RF11, RF12, RNF2).

#include "common.h"

namespace {

const uint8_t kKey[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
const uint8_t kKey2[16] = {2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
constexpr size_t kBuf = LORALAN_HEADER_SIZE + LORALAN_MAX_PAYLOAD + LORALAN_MIC_SIZE + 8;

// Da acceso a la comprobacion de secuencia, protegida en la biblioteca.
struct SeqProbe : LoRaLANDevice {
    using LoRaLANDevice::acceptSeq;
};

uint16_t buildFrame(uint8_t* buf, uint8_t payloadLen) {
    uint8_t payload[LORALAN_MAX_PAYLOAD];
    std::memset(payload, 0x5A, sizeof(payload));
    uint16_t len = 0;
    LoRaLANPacket::build(buf, 0x10, 0x20, 0x01, MsgType::PUSH_EVENT, Priority::PRIO_HIGH,
                         static_cast<uint8_t>(Flags::ACK_REQUIRED), 0x1234, payload, payloadLen, len);
    return len;
}

}  // namespace

CASE("F1", "FORMATO", "Ida y vuelta de cada tipo de trama", "2026-11-01", "Ninguna", "Construir y volver a leer") {
    const MsgType types[] = {MsgType::PUSH_EVENT, MsgType::ACK,         MsgType::POLL,
                             MsgType::POLL_RESPONSE, MsgType::TIME_SYNC, MsgType::JOIN_REQUEST,
                             MsgType::JOIN_ACCEPT, MsgType::LEAVE,      MsgType::CMD};
    PROP("PRO-8", "Cada campo leido coincide con el construido y el MIC se verifica");
    const uint8_t payload[] = {9, 8, 7};
    for (MsgType type : types) {
        uint8_t buf[kBuf];
        uint16_t len = 0;
        const bool built = LoRaLANPacket::build(buf, 0x10, 0x20, 0x30, type, Priority::PRIO_MEDIUM,
                                                static_cast<uint8_t>(Flags::IS_RETRY), 0xABCD, payload, 3, len);
        EXPECT_TRUE(built, "la trama debe construirse");
        LoRaLANPacket::applyMic(buf, len, kKey);
        LoRaLANFrame f;
        EXPECT_TRUE(LoRaLANPacket::parse(buf, len, f), "la trama debe leerse");
        EXPECT_EQ(f.header.version, LORALAN_VERSION, "version");
        EXPECT_EQ(f.header.netId, 0x10, "netId");
        EXPECT_EQ(f.header.srcId, 0x20, "srcId");
        EXPECT_EQ(f.header.dstId, 0x30, "dstId");
        EXPECT_EQ(f.header.type, static_cast<uint8_t>(type), "tipo");
        EXPECT_EQ(f.header.priority, static_cast<uint8_t>(Priority::PRIO_MEDIUM), "prioridad");
        EXPECT_EQ(f.header.flags, static_cast<uint8_t>(Flags::IS_RETRY), "indicadores");
        EXPECT_EQ(f.header.seq, 0xABCD, "secuencia");
        EXPECT_EQ(f.header.payloadLen, 3, "longitud de carga");
        EXPECT_TRUE(std::memcmp(f.payload, payload, 3) == 0, "carga");
        EXPECT_TRUE(LoRaLANPacket::checkMic(buf, len, kKey), "MIC");
    }
}

CASE("F2", "FORMATO", "Limites de longitud de trama", "2026-11-01", "Ninguna", "Longitudes 10, 11, 75 y 76 B") {
    uint8_t buf[kBuf];
    std::memset(buf, 0, sizeof(buf));
    buildFrame(buf, LORALAN_MAX_PAYLOAD);
    LoRaLANFrame f;
    PROP("RNF-3", "Solo se aceptan tramas de 11 a 75 bytes");
    EXPECT_TRUE(!LoRaLANPacket::parse(buf, 10, f), "10 B debe rechazarse");
    EXPECT_TRUE(LoRaLANPacket::parse(buf, 11, f), "11 B debe aceptarse");
    EXPECT_TRUE(LoRaLANPacket::parse(buf, 75, f), "75 B debe aceptarse");
    EXPECT_TRUE(!LoRaLANPacket::parse(buf, 76, f), "76 B debe rechazarse");
}

CASE("F3", "FORMATO", "Secuencia de 16 bits y vuelta en 0xFFFF", "2026-11-01", "Ninguna", "Avanzar de 0xFFFE a 0x0000") {
    uint8_t buf[kBuf];
    uint16_t len = 0;
    LoRaLANPacket::build(buf, 0x10, 0x20, 0x01, MsgType::ACK, Priority::PRIO_LOW, 0, 0xFFFF, nullptr, 0, len);
    LoRaLANFrame f;
    LoRaLANPacket::parse(buf, len, f);
    PROP("PRO-8", "El campo conserva los 16 bits");
    EXPECT_EQ(f.header.seq, 0xFFFF, "secuencia leida");

    uint16_t last = 0xFFFE;
    uint64_t seen = 1;
    bool have = true;
    PROP("PRO-8", "La deteccion de repeticiones sigue funcionando al dar la vuelta");
    EXPECT_TRUE(SeqProbe::acceptSeq(0xFFFF, last, seen, have), "0xFFFF es nueva");
    EXPECT_TRUE(SeqProbe::acceptSeq(0x0000, last, seen, have), "0x0000 es nueva tras 0xFFFF");
    EXPECT_TRUE(!SeqProbe::acceptSeq(0x0000, last, seen, have), "repetir 0x0000 se rechaza");
    EXPECT_TRUE(!SeqProbe::acceptSeq(0xFFFF, last, seen, have), "volver a 0xFFFF se rechaza");
    EXPECT_TRUE(SeqProbe::acceptSeq(0x0001, last, seen, have), "0x0001 es nueva");

    last = 100;
    seen = 1;
    have = true;
    PROP("PRO-8", "Una secuencia anterior no vista se acepta una sola vez; una muy antigua, ninguna");
    EXPECT_TRUE(SeqProbe::acceptSeq(102, last, seen, have), "102 es nueva");
    EXPECT_TRUE(SeqProbe::acceptSeq(101, last, seen, have), "101, anterior sin ver, se acepta");
    EXPECT_TRUE(!SeqProbe::acceptSeq(101, last, seen, have), "101 repetida se rechaza");
    EXPECT_TRUE(!SeqProbe::acceptSeq(100, last, seen, have), "100, ya vista, se rechaza");
    EXPECT_TRUE(!SeqProbe::acceptSeq(38, last, seen, have), "64 por detras se rechaza");

    PROP("PRO-8", "Un salto hacia delante de hasta media escala se acepta");
    EXPECT_TRUE(SeqProbe::acceptSeq(102 + 0x7FFF, last, seen, have), "salto maximo aceptado");
}

CASE("F4", "FORMATO", "MIC sensible a cada byte", "2026-11-01", "Trama de 8 B de carga sellada", "Alterar cada byte") {
    uint8_t buf[kBuf];
    const uint16_t len = buildFrame(buf, 8);
    LoRaLANPacket::applyMic(buf, len, kKey);
    PROP("PRO-8", "La trama intacta se verifica");
    EXPECT_TRUE(LoRaLANPacket::checkMic(buf, len, kKey), "MIC con la clave correcta");
    PROP("PRO-8", "Alterar cualquier byte invalida el MIC");
    int accepted = 0;
    for (uint16_t i = 0; i < len; ++i) {
        buf[i] ^= 0x01;
        if (LoRaLANPacket::checkMic(buf, len, kKey)) ++accepted;
        buf[i] ^= 0x01;
    }
    EXPECT_EQ(accepted, 0, "bytes alterados que siguen pasando el MIC");
    PROP("PRO-8", "Otra clave invalida el MIC");
    EXPECT_TRUE(!LoRaLANPacket::checkMic(buf, len, kKey2), "MIC con otra clave");
}

CASE("F5", "FORMATO", "Campos de cabecera invalidos", "2026-11-01", "Trama valida", "Version, tipo, prioridad o indicadores invalidos") {
    uint8_t buf[kBuf];
    const uint16_t len = buildFrame(buf, 0);
    LoRaLANFrame f;
    PROP("PRO-8", "La trama de control se acepta");
    EXPECT_TRUE(LoRaLANPacket::parse(buf, len, f), "trama valida");

    const auto rejected = [&](uint8_t index, uint8_t value) {
        const uint8_t saved = buf[index];
        buf[index] = value;
        const bool ok = LoRaLANPacket::parse(buf, len, f);
        buf[index] = saved;
        return !ok;
    };
    const uint8_t control = buf[0];
    PROP("PRO-8", "Lectura: cada campo invalido se rechaza");
    EXPECT_TRUE(rejected(0, static_cast<uint8_t>((control & 0x3F) | 0x80)), "version 2");
    EXPECT_TRUE(rejected(0, static_cast<uint8_t>(control & 0xC3)), "tipo 0");
    EXPECT_TRUE(rejected(0, static_cast<uint8_t>((control & 0xC3) | (0x05 << 2))), "tipo reservado 5");
    EXPECT_TRUE(rejected(0, static_cast<uint8_t>((control & 0xC3) | (0x0F << 2))), "tipo 15");
    EXPECT_TRUE(rejected(0, static_cast<uint8_t>(control | 0x03)), "prioridad 3");
    EXPECT_TRUE(rejected(1, 0x04), "indicador desconocido");

    PROP("PRO-8", "Construccion: no se fabrica una trama invalida");
    uint16_t out = 0;
    EXPECT_TRUE(!LoRaLANPacket::build(buf, 0x10, 0x20, 0x01, MsgType::ACK, static_cast<Priority>(3), 0, 1, nullptr, 0, out), "prioridad 3");
    EXPECT_TRUE(!LoRaLANPacket::build(buf, 0x10, 0x20, 0x01, MsgType::ACK, Priority::PRIO_LOW, 0x04, 1, nullptr, 0, out), "indicador desconocido");
    EXPECT_TRUE(!LoRaLANPacket::build(buf, 0x10, 0x20, 0x01, static_cast<MsgType>(0x05), Priority::PRIO_LOW, 0, 1, nullptr, 0, out), "tipo reservado");
}

CASE("F6", "FORMATO", "Carga de 0, 1, 64 y 65 B", "2026-11-01", "Ninguna", "Construir con esas cargas") {
    uint8_t buf[kBuf + 8];
    uint8_t payload[65] = {0};
    uint16_t len = 0;
    PROP("RNF-3", "Se acepta hasta 64 bytes de carga, con su longitud total correcta");
    EXPECT_TRUE(LoRaLANPacket::build(buf, 1, 2, 3, MsgType::ACK, Priority::PRIO_LOW, 0, 1, payload, 0, len), "0 B");
    EXPECT_EQ(len, 11, "total con 0 B");
    EXPECT_TRUE(LoRaLANPacket::build(buf, 1, 2, 3, MsgType::ACK, Priority::PRIO_LOW, 0, 1, payload, 1, len), "1 B");
    EXPECT_EQ(len, 12, "total con 1 B");
    EXPECT_TRUE(LoRaLANPacket::build(buf, 1, 2, 3, MsgType::ACK, Priority::PRIO_LOW, 0, 1, payload, 64, len), "64 B");
    EXPECT_EQ(len, 75, "total con 64 B");
    PROP("RNF-3", "Se rechaza una carga de 65 bytes");
    EXPECT_TRUE(!LoRaLANPacket::build(buf, 1, 2, 3, MsgType::ACK, Priority::PRIO_LOW, 0, 1, payload, 65, len), "65 B");
}
