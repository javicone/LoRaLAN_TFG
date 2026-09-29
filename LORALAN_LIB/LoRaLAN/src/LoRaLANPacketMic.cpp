#include "LoRaLANPacket.h"
#include "LoRaLANCrypto.h"

// Escribe el CMAC de la trama en sus ultimos 4 bytes.
bool LoRaLANPacket::applyMic(uint8_t* buffer, uint16_t totalLen, const uint8_t key[16]) {
    if (buffer == nullptr || key == nullptr || totalLen < LORALAN_HEADER_SIZE + LORALAN_MIC_SIZE) {
        return false;
    }

    const uint16_t covered = static_cast<uint16_t>(totalLen - LORALAN_MIC_SIZE);
    uint8_t mic[4];
    if (!loralan_crypto::cmacAes128(key, buffer, covered, mic)) {
        return false;   // falla en cerrado: sin CMAC no se emite la trama
    }

    memcpy(buffer + covered, mic, LORALAN_MIC_SIZE);
    return true;
}

// Verifica el CMAC de una trama recibida, en tiempo constante.
bool LoRaLANPacket::checkMic(const uint8_t* buffer, uint16_t totalLen, const uint8_t key[16]) {
    if (buffer == nullptr || key == nullptr || totalLen < LORALAN_HEADER_SIZE + LORALAN_MIC_SIZE) {
        return false;
    }

    const uint16_t covered = static_cast<uint16_t>(totalLen - LORALAN_MIC_SIZE);
    uint8_t expected[4];
    if (!loralan_crypto::cmacAes128(key, buffer, covered, expected)) {
        return false;
    }

    return loralan_crypto::micEquals(expected, buffer + covered);
}
