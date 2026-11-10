#pragma once

#include <Arduino.h>
#include <RadioLib.h>
#include "LoRaLANPacket.h"
#include "LoRaLANCrypto.h"

// Maneja construccion segura, ACK y reintentos.
class LoRaLANManager {
public:
    explicit LoRaLANManager(PhysicalLayer& radio)
        : radio_(radio) {
        clearPendingTx();
        memset(sessionKey_, 0, sizeof(sessionKey_));
    }

    // Define el contexto de direccionamiento.
    void setIdentity(uint8_t netId, uint8_t localNodeId) {
        netId_ = netId;
        localNodeId_ = localNodeId;
    }

    // Instala la session key para AES-CTR.
    void setSessionKey(const uint8_t key[16]) {
        if (key != nullptr) {
            memcpy(sessionKey_, key, sizeof(sessionKey_));
            hasSessionKey_ = true;
        }
    }

    // Borra la session key activa.
    void clearSessionKey() {
        memset(sessionKey_, 0, sizeof(sessionKey_));
        hasSessionKey_ = false;
    }

    // Session key activa, o nullptr si aun no hay. La usa la capa de protocolo
    // para calcular y verificar el MIC.
    const uint8_t* sessionKey() const {
        return hasSessionKey_ ? sessionKey_ : nullptr;
    }

    // Contador monotono de secuencia.
    uint16_t nextSequence() {
        return nextSeq_++;
    }

    // Encola un buffer y espera ACK con reintentos.
    bool enqueueReliableTx(const uint8_t* buffer,
                           uint16_t len,
                           uint16_t seq,
                           const __FlashStringHelper* label,
                           uint8_t maxAttempts,
                           uint32_t ackTimeoutMs,
                           const uint8_t micKey[16]) {
        if (buffer == nullptr || len == 0 || len > sizeof(pendingTx_.buffer)) {
            return false;
        }

        if (micKey == nullptr) {
            Serial.println(F("[ERROR] Sin clave para el MIC, envio descartado"));
            return false;
        }

        if (txState_ != TxState::IDLE || pendingTx_.active) {
            Serial.println(F("[BUSY] Hay un envio pendiente de ACK, se pospone el nuevo"));
            return false;
        }

        memcpy(pendingTx_.buffer, buffer, len);
        // La clave se guarda con el envio: cada retransmision reconstruye la
        // trama (para marcar el flag de reintento) y hay que volver a sellarla.
        memcpy(pendingTx_.micKey, micKey, 16);

        if (!LoRaLANPacket::applyMic(pendingTx_.buffer, len, pendingTx_.micKey)) {
            Serial.println(F("[ERROR] No se pudo calcular el MIC"));
            return false;
        }

        pendingTx_.len = len;
        pendingTx_.seq = seq;
        pendingTx_.attempts = 0;
        pendingTx_.maxAttempts = maxAttempts;
        pendingTx_.ackTimeoutMs = ackTimeoutMs;
        pendingTx_.lastTxMs = 0;
        pendingTx_.label = label;
        pendingTx_.active = true;
        ackForPendingTx_ = false;
        txState_ = TxState::WAIT_ACK;

        if (!sendPendingAttempt()) {
            if (pendingTx_.attempts >= pendingTx_.maxAttempts) {
                Serial.print(F("[ERROR] Envio APP agotado en "));
                Serial.println(label);
                clearPendingTx();
                return false;
            }
        }

        return true;
    }

    // Construye y cifra una trama. key: clave de cifrado; nullptr = la session
    // key propia (nodo). El gateway pasa la del nodo destino.
    bool buildSecureFrame(uint8_t dstId,
                          MsgType type,
                          Priority priority,
                          uint8_t flags,
                          const uint8_t* payload,
                          uint8_t payloadLen,
                          uint16_t seq,
                          uint8_t* outBuffer,
                          uint16_t& outLen,
                          const uint8_t* key = nullptr) {
        if (outBuffer == nullptr) {
            return false;
        }

        if (key == nullptr) {
            key = sessionKey();
        }
        if (payloadLen > 0 && key == nullptr) {
            return false;
        }

        uint8_t encryptedPayload[LORALAN_MAX_PAYLOAD];
        const uint16_t frameSeq = seq;

        const uint8_t* payloadToSend = payload;
        if (payloadLen > 0) {
            uint8_t nonce[16];
            loralan_crypto::buildNonce(nonce, netId_, localNodeId_, frameSeq);
            if (!loralan_crypto::aes128CtrCrypt(key, nonce, payload, encryptedPayload, payloadLen)) {
                return false;
            }
            payloadToSend = encryptedPayload;
        }

        return LoRaLANPacket::build(
            outBuffer,
            netId_,
            localNodeId_,
            dstId,
            type,
            priority,
            flags,
            frameSeq,
            payloadToSend,
            payloadLen,
            outLen
        );
    }

    // Construye, cifra y encola un paquete para TX.
    bool sendPacket(uint8_t dstId,
                    MsgType type,
                    Priority priority,
                    uint8_t flags,
                    const uint8_t* payload,
                    uint8_t payloadLen,
                    const __FlashStringHelper* label,
                    uint8_t maxAttempts,
                    uint32_t ackTimeoutMs,
                    uint16_t* outSeq = nullptr) {
        uint8_t buffer[LORALAN_HEADER_SIZE + LORALAN_MAX_PAYLOAD + LORALAN_MIC_SIZE];
        uint16_t len = 0;
        const uint16_t seq = nextSequence();

        if (!buildSecureFrame(dstId, type, priority, flags, payload, payloadLen, seq, buffer, len)) {
            return false;
        }

        if (outSeq != nullptr) {
            *outSeq = seq;
        }

        // Sin ACK_REQUIRED nadie va a confirmar: se emite una sola vez, sin
        // ocupar el TX fiable (si no, se retransmitiria hasta agotar intentos).
        if ((flags & static_cast<uint8_t>(Flags::ACK_REQUIRED)) == 0) {
            if (!hasSessionKey_ || !LoRaLANPacket::applyMic(buffer, len, sessionKey_)) {
                return false;
            }
            return transmitWithRetry(buffer, len, label, 1, ackTimeoutMs);
        }

        // sendPacket lo usa el nodo, cuyo interlocutor es siempre el gateway:
        // la clave correcta es su propia session key.
        return enqueueReliableTx(buffer, len, seq, label, maxAttempts, ackTimeoutMs,
                                 hasSessionKey_ ? sessionKey_ : nullptr);
    }

    // Descifra un payload usando la session key propia.
    bool decryptPayload(const LoRaLANFrame& frame, uint8_t* output, uint8_t& outputLen) const {
        return decryptWithKey(sessionKey(), frame, output, outputLen);
    }

    // Descifra el payload de una trama con la clave dada (AES-128-CTR, nonce
    // netId/srcId/seq). false si no hay clave.
    static bool decryptWithKey(const uint8_t* key, const LoRaLANFrame& frame,
                               uint8_t* output, uint8_t& outputLen) {
        if (key == nullptr || output == nullptr) {
            return false;
        }

        if (frame.header.payloadLen > LORALAN_MAX_PAYLOAD) {
            return false;
        }

        if (frame.header.payloadLen == 0) {
            outputLen = 0;
            return true;
        }

        uint8_t nonce[16];
        loralan_crypto::buildNonce(nonce, frame.header.netId, frame.header.srcId, frame.header.seq);
        if (!loralan_crypto::aes128CtrCrypt(key, nonce, frame.payload, output, frame.header.payloadLen)) {
            return false;
        }

        outputLen = frame.header.payloadLen;
        return true;
    }

    // Hay un TX fiable en curso (esperando ACK o reintentos).
    bool isTxBusy() const {
        return txState_ != TxState::IDLE || pendingTx_.active;
    }

    // Consume el resultado del ultimo TX fiable terminado (ACK ok o agotado).
    bool takeTxResult(uint16_t& seq, bool& acked) {
        if (!hasNewResult_) {
            return false;
        }
        seq = lastResultSeq_;
        acked = lastResultAcked_;
        hasNewResult_ = false;
        return true;
    }

    // Payload del ACK que cerro el ultimo TX fiable (vacio si no lo llevaba o
    // si se agotaron los reintentos). Valido tras takeTxResult().
    const uint8_t* lastAckPayload(uint8_t& len) const {
        len = lastAckLen_;
        return lastAckPayload_;
    }

    // Marca ACK recibido para el TX pendiente y guarda su payload.
    void notifyAck(uint16_t seq, const uint8_t* payload = nullptr, uint8_t payloadLen = 0) {
        if (txState_ == TxState::WAIT_ACK && pendingTx_.active && pendingTx_.seq == seq) {
            ackForPendingTx_ = true;
            ackLen_ = (payload != nullptr && payloadLen <= LORALAN_MAX_PAYLOAD) ? payloadLen : 0;
            if (ackLen_ > 0) {
                memcpy(ackPayload_, payload, ackLen_);
            }
        }
    }

    // Ejecuta la maquina de reintentos.
    void process() {
        processTxState();
    }

    // Transmite un buffer ya sellado, con reintentos de radio. Vuelve a RX
    // tras cada intento.
    bool transmitWithRetry(const uint8_t* buffer,
                           uint16_t len,
                           const __FlashStringHelper* label,
                           uint8_t maxRetries,
                           uint32_t timeoutMs) {
        uint32_t startMs = millis();

        for (uint8_t attempt = 1; attempt <= maxRetries; attempt++) {
            uint32_t elapsed = millis() - startMs;
            if (elapsed >= timeoutMs) {
                Serial.print(F("[ERROR] Timeout "));
                Serial.print(label);
                Serial.print(F(" ("));
                Serial.print(timeoutMs);
                Serial.println(F(" ms)"));
                return false;
            }

            Serial.print(F("Intento TX #"));
            Serial.print(attempt);
            Serial.print(F(" -> "));
            Serial.println(label);

            int state = radio_.transmit(const_cast<uint8_t*>(buffer), len);
            // Volver a RX ya: si no, el ACK de este envio llega antes de que
            // el bucle principal reactive la recepcion y se pierde.
            radio_.startReceive();
            if (state == RADIOLIB_ERR_NONE) {
                return true;
            }

            Serial.print(F("[ERROR] Error transmitiendo "));
            Serial.print(label);
            Serial.print(F(": "));
            Serial.println(state);

            if (attempt < maxRetries) {
                delay(static_cast<unsigned long>(random(30, 181)));
            }
        }

        Serial.print(F("[ERROR] Fallo final en "));
        Serial.println(label);
        return false;
    }

private:
    enum class TxState : uint8_t {
        IDLE = 0,
        WAIT_ACK = 1
    };

    struct PendingTxContext {
        uint8_t buffer[LORALAN_HEADER_SIZE + LORALAN_MAX_PAYLOAD + LORALAN_MIC_SIZE];
        uint8_t micKey[16] = {0};   // clave con la que se sella este envio
        uint16_t len = 0;
        uint16_t seq = 0;
        uint8_t attempts = 0;
        uint8_t maxAttempts = 0;
        uint32_t ackTimeoutMs = 0;
        uint32_t lastTxMs = 0;
        // Plazo del ACK mas una parte aleatoria, sorteada en cada emision: dos nodos que
        // colisionan no repiten a la vez. El ACK se acepta durante toda la espera.
        uint32_t retryAfterMs = 0;
        const __FlashStringHelper* label = nullptr;
        bool active = false;
    };

    static constexpr uint32_t kRetryJitterMs = 600;   // unas 12 tramas de evento en SF7

    bool markPacketAsRetry(uint8_t* buffer, uint16_t& len) {
        LoRaLANFrame frame;
        if (!LoRaLANPacket::parse(buffer, len, frame)) {
            return false;
        }

        if (!LoRaLANPacket::build(
                buffer,
                frame.header.netId,
                frame.header.srcId,
                frame.header.dstId,
                static_cast<MsgType>(frame.header.type),
                static_cast<Priority>(frame.header.priority),
                static_cast<uint8_t>(frame.header.flags | static_cast<uint8_t>(Flags::IS_RETRY)),
                frame.header.seq,
                frame.payload,
                frame.header.payloadLen,
                len)) {
            return false;
        }

        // build() deja un MIC provisional: hay que volver a sellar la trama con
        // la clave de este envio, o el receptor la rechazara.
        return LoRaLANPacket::applyMic(buffer, len, pendingTx_.micKey);
    }

    bool sendPendingAttempt() {
        if (!pendingTx_.active) {
            return false;
        }

        if (pendingTx_.attempts > 0) {
            if (!markPacketAsRetry(pendingTx_.buffer, pendingTx_.len)) {
                Serial.println(F("[ERROR] Error marcando paquete como reintento"));
                return false;
            }
        }

        pendingTx_.attempts++;
        Serial.print(F("Intento APP #"));
        Serial.print(pendingTx_.attempts);
        Serial.print(F(" -> "));
        Serial.println(pendingTx_.label);

        bool ok = transmitWithRetry(
            pendingTx_.buffer,
            pendingTx_.len,
            pendingTx_.label,
            pendingTx_.maxAttempts,
            pendingTx_.ackTimeoutMs
        );
        if (!ok) {
            return false;
        }

        pendingTx_.lastTxMs = millis();
        pendingTx_.retryAfterMs = pendingTx_.ackTimeoutMs + static_cast<uint32_t>(random(0, kRetryJitterMs + 1));
        radio_.startReceive();
        return true;
    }

    void clearPendingTx() {
        pendingTx_.active = false;
        pendingTx_.len = 0;
        pendingTx_.seq = 0;
        pendingTx_.attempts = 0;
        pendingTx_.maxAttempts = 0;
        pendingTx_.ackTimeoutMs = 0;
        pendingTx_.lastTxMs = 0;
        pendingTx_.label = nullptr;
        txState_ = TxState::IDLE;
        ackForPendingTx_ = false;
        ackLen_ = 0;
    }

    void processTxState() {
        if (txState_ != TxState::WAIT_ACK || !pendingTx_.active) {
            return;
        }

        if (ackForPendingTx_) {
            Serial.print(F("[ACK] Confirmado seq="));
            Serial.print(pendingTx_.seq);
            Serial.print(F(" ("));
            Serial.print(pendingTx_.label);
            Serial.println(F(")"));
            recordResult(true);
            clearPendingTx();
            return;
        }

        if (millis() - pendingTx_.lastTxMs < pendingTx_.retryAfterMs) {
            return;
        }

        if (pendingTx_.attempts >= pendingTx_.maxAttempts) {
            Serial.print(F("[ERROR] Timeout ACK agotado para "));
            Serial.println(pendingTx_.label);
            recordResult(false);
            clearPendingTx();
            return;
        }

        Serial.print(F("[RETRY] Timeout ACK, reintentando "));
        Serial.println(pendingTx_.label);

        if (!sendPendingAttempt() && pendingTx_.attempts >= pendingTx_.maxAttempts) {
            Serial.print(F("[ERROR] Envio APP agotado en "));
            Serial.println(pendingTx_.label);
            recordResult(false);
            clearPendingTx();
        }
    }

    // Guarda el desenlace del TX pendiente para que lo consuma la capa superior.
    void recordResult(bool acked) {
        lastResultSeq_ = pendingTx_.seq;
        lastResultAcked_ = acked;
        lastAckLen_ = acked ? ackLen_ : 0;
        if (lastAckLen_ > 0) {
            memcpy(lastAckPayload_, ackPayload_, lastAckLen_);
        }
        hasNewResult_ = true;
    }

    PhysicalLayer& radio_;
    uint8_t netId_ = 0;
    uint8_t localNodeId_ = 0;
    uint16_t nextSeq_ = 0;
    uint8_t sessionKey_[16];
    bool hasSessionKey_ = false;
    TxState txState_ = TxState::IDLE;
    bool ackForPendingTx_ = false;
    uint8_t ackPayload_[LORALAN_MAX_PAYLOAD] = {0};   // ACK del TX en curso
    uint8_t ackLen_ = 0;
    PendingTxContext pendingTx_{};
    uint16_t lastResultSeq_ = 0;
    bool lastResultAcked_ = false;
    uint8_t lastAckPayload_[LORALAN_MAX_PAYLOAD] = {0};
    uint8_t lastAckLen_ = 0;
    bool hasNewResult_ = false;
};
