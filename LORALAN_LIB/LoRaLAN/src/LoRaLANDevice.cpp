#include "LoRaLANDevice.h"

namespace {

// Ventana de aceptacion del numero de secuencia.
//
// El campo seq es de 2 bytes, asi que la comparacion es modular (2^16). Se
// acepta una trama cuyo seq este por delante del ultimo aceptado dentro de
// esta ventana; se rechaza el duplicado exacto (delta 0) y cualquier valor muy
// alejado, que corresponde a una repeticion antigua.
//
// 64 tolera perder hasta 63 tramas consecutivas de un nodo sin
// desincronizarse, y deja fuera de la ventana las tramas capturadas hace
// tiempo: una trama vieja solo volveria a entrar tras 65536 tramas nuevas. Si un nodo reinicia, su contador vuelve a
// empezar, pero entonces re-JOIN y el JOIN resincroniza (haveSeq = false).
constexpr uint8_t kSeqWindow = 64;

// Tope de espera del CAD. Un CAD dura ~2 simbolos: 2 ms en SF7/125 kHz y
// ~66 ms en SF12/125 kHz. Si se supera, la radio no ha respondido.
constexpr uint32_t kCadTimeoutMs = 500;

} // namespace

LoRaLANDevice::LoRaLANDevice(PhysicalLayer& radio)
    : radio_(radio), manager_(radio) {
    base_.appKey = appKey_;
}

void LoRaLANDevice::applyBaseConfig(const BaseConfig& config, uint8_t localId) {
    base_ = config;
    base_.appKey = appKey_;
    if (config.appKey != nullptr) {
        memcpy(appKey_, config.appKey, sizeof(appKey_));
    }
    localId_ = localId;
}

void LoRaLANDevice::fillBaseConfig(BaseConfig& config) const {
    config = base_;
}

void LoRaLANDevice::setAppKey(const uint8_t key[16]) {
    if (key != nullptr) {
        memcpy(appKey_, key, sizeof(appKey_));
    }
}

void LoRaLANDevice::setTxRetryPolicy(uint8_t maxRetries, uint32_t timeoutMs) {
    base_.txMaxRetries = maxRetries;
    base_.txTimeoutMs = timeoutMs;
}

void LoRaLANDevice::setAppRetryPolicy(uint8_t maxRetries, uint32_t ackTimeoutMs) {
    base_.appMaxRetries = maxRetries;
    base_.appAckTimeoutMs = ackTimeoutMs;
}

void LoRaLANDevice::process() {
    manager_.process();
    onProcess();
}

// Parsea, autentica y despacha un buffer recibido.
bool LoRaLANDevice::handleReceived(const uint8_t* buffer, size_t len) {
    if (buffer == nullptr || len == 0) {
        return false;
    }

    LoRaLANFrame frame;
    if (!LoRaLANPacket::parse(buffer, static_cast<uint16_t>(len), frame)) {
        return false;
    }

    if (frame.header.netId != base_.netId) {
        return false;
    }

    // Autenticacion ANTES de descifrar y de tocar cualquier estado: una trama
    // no autentica no consume ciclos de descifrado ni ranura en la tabla.
    const uint8_t* micKey = micKeyFor(frame.header.type, frame.header.srcId, frame.header.dstId);
    if (micKey == nullptr) {
        return false;   // sin clave para este interlocutor: no se acepta
    }
    if (!LoRaLANPacket::checkMic(buffer, static_cast<uint16_t>(len), micKey)) {
        Serial.println(F("[ERROR] MIC invalido, trama descartada"));
        return false;
    }

    if (frame.header.type == static_cast<uint8_t>(MsgType::ACK)) {
        manager_.notifyAck(frame.header.seq, frame.payload, frame.header.payloadLen);
        return true;
    }

    ackHandled_ = false;
    if (!onFrame(frame)) {
        return false;
    }

    if (!ackHandled_ && LoRaLANPacket::isAckRequired(frame) && frame.header.dstId == localId_) {
        sendAck(frame.header.srcId, frame.header.seq);
    }
    return true;
}

const uint8_t* LoRaLANDevice::micKeyFor(uint8_t msgType, uint8_t peerId, uint8_t dstId) const {
    const MsgType type = static_cast<MsgType>(msgType);
    if (type == MsgType::JOIN_REQUEST || type == MsgType::JOIN_ACCEPT || dstId == 0xFF) {
        return appKey_;
    }
    return sessionKeyFor(peerId);
}

bool LoRaLANDevice::sendAck(uint8_t dstId, uint16_t seq, const uint8_t* payload, uint8_t payloadLen) {
    uint8_t buffer[LORALAN_HEADER_SIZE + LORALAN_MAX_PAYLOAD + LORALAN_MIC_SIZE];
    uint16_t len = 0;

    if (!LoRaLANPacket::buildAck(buffer, base_.netId, localId_, dstId, seq, len, payload, payloadLen)) {
        return false;
    }

    return sendRaw(buffer, len, F("ACK"));
}

// Punto unico por el que salen al aire las tramas sin espera de ACK
// (POLL, TIME_SYNC, ACK, LEAVE, JOIN_*). build() solo reserva el hueco del MIC.
bool LoRaLANDevice::sendRaw(const uint8_t* buffer, uint16_t len, const __FlashStringHelper* label) {
    uint8_t sealed[LORALAN_HEADER_SIZE + LORALAN_MAX_PAYLOAD + LORALAN_MIC_SIZE];
    if (buffer == nullptr || len == 0 || len > sizeof(sealed)) {
        return false;
    }

    LoRaLANFrame hdr;
    if (!LoRaLANPacket::parse(buffer, len, hdr)) {
        return false;
    }

    const uint8_t* key = micKeyFor(hdr.header.type, hdr.header.dstId, hdr.header.dstId);
    if (key == nullptr) {
        Serial.print(F("[ERROR] Sin clave para sellar "));
        Serial.println(label);
        return false;
    }

    memcpy(sealed, buffer, len);
    if (!LoRaLANPacket::applyMic(sealed, len, key)) {
        Serial.print(F("[ERROR] No se pudo calcular el MIC de "));
        Serial.println(label);
        return false;
    }

    return manager_.transmitWithRetry(sealed, len, label, base_.txMaxRetries, base_.txTimeoutMs);
}

// CAD asincrono. scanChannel() de RadioLib lee el resultado por el pin DIO1 en
// el SX127x, que puede no estar cableado; startChannelScan() + checkIrq() +
// getChannelScanResult() leen los flags de la radio por SPI y valen para
// cualquier PhysicalLayer.
//
// El SX127x indica canal ocupado con RADIOLIB_PREAMBLE_DETECTED y el SX126x /
// LR11x0 con RADIOLIB_LORA_DETECTED: ambos son BUSY.
LoRaLANDevice::ChannelState LoRaLANDevice::checkChannel() {
    if (radio_.startChannelScan() != RADIOLIB_ERR_NONE) {
        radio_.startReceive();
        return ChannelState::ERROR;
    }

    const uint32_t startMs = millis();
    int16_t done = 0;
    while ((done = radio_.checkIrq(RADIOLIB_IRQ_CAD_DONE)) == 0 && millis() - startMs < kCadTimeoutMs) {
        delay(1);
    }

    const int16_t result = (done == 1) ? radio_.getChannelScanResult() : done;
    radio_.startReceive();   // al acabar el CAD la radio queda en reposo

    if (result == RADIOLIB_PREAMBLE_DETECTED || result == RADIOLIB_LORA_DETECTED) {
        return ChannelState::BUSY;
    }
    return result == RADIOLIB_CHANNEL_FREE ? ChannelState::FREE : ChannelState::ERROR;
}

bool LoRaLANDevice::acceptSeq(uint16_t seq, uint16_t& lastSeq, bool& haveSeq) {
    if (!haveSeq) {          // primera trama de la sesion: fija el origen
        haveSeq = true;
        lastSeq = seq;
        return true;
    }

    const uint16_t delta = static_cast<uint16_t>(seq - lastSeq);
    if (delta == 0 || delta > kSeqWindow) {
        return false;
    }

    lastSeq = seq;
    return true;
}
