#include "LoRaLANNode.h"

LoRaLANNode::LoRaLANNode(PhysicalLayer& radio)
    : LoRaLANDevice(radio) {}

void LoRaLANNode::begin(const Config& config) {
    applyBaseConfig(config, config.nodeId);
    gatewayId_ = config.gatewayId;
    nodeType_ = config.nodeType;
    joinRetryMs_ = config.joinRetryMs;
    cad_ = config.cad;
    manager_.setIdentity(config.netId, config.nodeId);
    isJoined_ = false;
    lastDevNonce_ = 0;
    lastJoinAttemptMs_ = 0;
    pendingPush_ = {};
}

LoRaLANNode::Config LoRaLANNode::getConfig() const {
    Config cfg;
    fillBaseConfig(cfg);
    cfg.nodeId = localId_;
    cfg.gatewayId = gatewayId_;
    cfg.nodeType = nodeType_;
    cfg.joinRetryMs = joinRetryMs_;
    cfg.cad = cad_;
    return cfg;
}

bool LoRaLANNode::queuePushEvent(const uint8_t* payload, uint8_t payloadLen, bool ackRequired,
                                 bool fallbackToPoll) {
    if (!isJoined_ || payloadLen > LORALAN_MAX_PAYLOAD || pendingPush_.active) {
        return false;
    }

    pendingPush_ = {};
    pendingPush_.active = true;
    pendingPush_.flags = ackRequired ? static_cast<uint8_t>(Flags::ACK_REQUIRED) : 0;
    pendingPush_.payloadLen = payloadLen;
    if (payloadLen > 0 && payload != nullptr) {
        memcpy(pendingPush_.payload, payload, payloadLen);
    }
    pendingPush_.fallbackToPoll = fallbackToPoll && ackRequired;
    return true;
}

bool LoRaLANNode::queuePollPayload(const uint8_t* payload, uint8_t payloadLen) {
    if (!isJoined_ || payloadLen > LORALAN_MAX_PAYLOAD) {
        return false;
    }

    pollPayloadLen_ = payloadLen;
    if (payloadLen > 0 && payload != nullptr) {
        memcpy(pollPayload_, payload, payloadLen);
    }
    pollPayloadPending_ = true;
    return true;
}

bool LoRaLANNode::requestJoin() {
    autoJoin_ = true;
    lastJoinAttemptMs_ = 0;
    return true;
}

void LoRaLANNode::resetJoin() {
    isJoined_ = false;
    lastDevNonce_ = 0;
    lastJoinAttemptMs_ = 0;
    pendingPush_ = {};
    pushInFlight_ = false;
    manager_.clearSessionKey();
}

bool LoRaLANNode::leave() {
    if (!isJoined_) {
        return false;
    }

    uint8_t buffer[LORALAN_HEADER_SIZE + LORALAN_MAX_PAYLOAD + LORALAN_MIC_SIZE];
    uint16_t len = 0;
    const uint8_t seq = manager_.nextSequence();

    // ponytail: LEAVE sin ACK; si se pierde, el gateway lo detecta igual via isAlive()
    const bool ok = LoRaLANPacket::build(buffer, base_.netId, localId_, gatewayId_,
                                         MsgType::LEAVE, Priority::PRIO_MEDIUM, 0, seq, nullptr, 0, len)
                    && sendRaw(buffer, len, F("LEAVE"));

    resetJoin();
    autoJoin_ = false;
    return ok;
}

const uint8_t* LoRaLANNode::sessionKeyFor(uint8_t) const {
    return manager_.sessionKey();   // el interlocutor es siempre el gateway
}

bool LoRaLANNode::onFrame(const LoRaLANFrame& frame) {
    switch (static_cast<MsgType>(frame.header.type)) {
        case MsgType::JOIN_ACCEPT:
            return onJoinAccept(frame);
        case MsgType::TIME_SYNC:
            onTimeSync(frame);
            return true;
        case MsgType::CMD:
            onCommand(frame);
            return true;
        case MsgType::POLL:
            onPoll();
            return true;
        default:
            return true;
    }
}

bool LoRaLANNode::onJoinAccept(const LoRaLANFrame& frame) {
    if (frame.header.payloadLen < 4) {
        return false;
    }

    const uint8_t assignedId = frame.payload[0];
    const uint16_t devNonce = static_cast<uint16_t>(frame.payload[1] << 8) | frame.payload[2];
    const uint8_t status = frame.payload[3];
    if (status != 0x00 || devNonce != lastDevNonce_) {
        return false;
    }

    uint8_t sessionKey[16];
    if (!loralan_crypto::deriveSessionKey(appKey_, base_.netId, localId_, devNonce, sessionKey)) {
        return false;
    }
    manager_.setSessionKey(sessionKey);
    isJoined_ = true;

    if (assignedId != localId_) {
        localId_ = assignedId;
        manager_.setIdentity(base_.netId, localId_);
    }

    if (onJoinAccepted_ != nullptr) {
        onJoinAccepted_(frame, frame.payload, frame.header.payloadLen);
    }
    return true;
}

void LoRaLANNode::onTimeSync(const LoRaLANFrame& frame) {
    if (frame.header.payloadLen < 4) {
        return;
    }
    const uint32_t ts =
        (static_cast<uint32_t>(frame.payload[0]) << 24) |
        (static_cast<uint32_t>(frame.payload[1]) << 16) |
        (static_cast<uint32_t>(frame.payload[2]) << 8) |
        (static_cast<uint32_t>(frame.payload[3]));
    const int32_t gatewayMs = static_cast<int32_t>(ts * 1000UL);
    timeOffsetMs_ = gatewayMs - static_cast<int32_t>(millis());
}

void LoRaLANNode::onCommand(const LoRaLANFrame& frame) {
    if (!isJoined_ || frame.header.dstId != localId_ || onCommand_ == nullptr) {
        return;
    }
    uint8_t decrypted[LORALAN_MAX_PAYLOAD];
    uint8_t decryptedLen = 0;
    if (manager_.decryptPayload(frame, decrypted, decryptedLen)) {
        onCommand_(frame, decrypted, decryptedLen);
    }
}

void LoRaLANNode::onPoll() {
    if (!isJoined_) {
        return;
    }

    const uint8_t* payload = pollPayloadPending_ ? pollPayload_ : nullptr;
    uint8_t payloadLen = pollPayloadPending_ ? pollPayloadLen_ : 0;

    // Poll bajo demanda: si no hay payload encolado, se lo pedimos a la app.
    uint8_t onDemand[LORALAN_MAX_PAYLOAD];
    if (payload == nullptr && onPollRequest_ != nullptr) {
        uint8_t n = 0;
        if (onPollRequest_(onDemand, n) && n > 0 && n <= LORALAN_MAX_PAYLOAD) {
            payload = onDemand;
            payloadLen = n;
        }
    }

    manager_.sendPacket(gatewayId_, MsgType::POLL_RESPONSE, Priority::PRIO_LOW,
                        static_cast<uint8_t>(Flags::ACK_REQUIRED), payload, payloadLen,
                        F("POLL_RESPONSE"), base_.appMaxRetries, base_.appAckTimeoutMs);
    pollPayloadPending_ = false;
    pollPayloadLen_ = 0;
}

// Tareas periodicas: reintento de JOIN, desenlace del PUSH y TX pendiente.
void LoRaLANNode::onProcess() {
    const uint32_t now = millis();

    if (!isJoined_ && autoJoin_ && joinRetryMs_ > 0 && now - lastJoinAttemptMs_ >= joinRetryMs_) {
        sendJoinRequest();
        lastJoinAttemptMs_ = now;
    }

    uint8_t doneSeq = 0;
    bool doneAcked = false;
    if (manager_.takeTxResult(doneSeq, doneAcked)) {
        // Si el PUSH con fallback agoto reintentos sin ACK, el payload se
        // rescata para la siguiente ventana POLL.
        if (pushFallbackArmed_ && doneSeq == pushFallbackSeq_) {
            pushFallbackArmed_ = false;
            if (!doneAcked) {
                queuePollPayload(pushFallbackPayload_, pushFallbackLen_);
                Serial.println(F("PUSH sin ACK: payload rescatado para la ventana POLL"));
            }
        }
        if (pushInFlight_ && doneSeq == pushSeq_) {
            pushInFlight_ = false;
            if (onPushAck_ != nullptr) {
                uint8_t ackLen = 0;
                const uint8_t* ack = manager_.lastAckPayload(ackLen);
                onPushAck_(doneAcked, ack, ackLen);
            }
        }
    }

    processPendingPush();
}

void LoRaLANNode::sendJoinRequest() {
    uint8_t buffer[LORALAN_HEADER_SIZE + LORALAN_MAX_PAYLOAD + LORALAN_MIC_SIZE];
    uint16_t len = 0;
    lastDevNonce_ = static_cast<uint16_t>(random(0, 0xFFFF));
    const uint8_t seq = manager_.nextSequence();

    if (!LoRaLANPacket::buildJoinRequest(buffer, base_.netId, localId_, nodeType_, lastDevNonce_, seq, len)) {
        return;
    }

    if (cad_.joinCadEnabled && checkChannel() == ChannelState::BUSY) {
        return;   // canal ocupado: se reintenta en el siguiente joinRetryMs
    }
    sendRaw(buffer, len, F("JOIN_REQUEST"));
}

// Gestiona el backoff CAD y lanza el PUSH pendiente.
void LoRaLANNode::processPendingPush() {
    if (!pendingPush_.active) {
        return;
    }

    // Con un TX fiable en vuelo, esperar: reintentar aqui cada vuelta del loop
    // hace CAD continuo (saca la radio de RX y pierde el ACK en vuelo).
    if (manager_.isTxBusy()) {
        return;
    }

    if (cad_.enabled) {
        if (pendingPush_.cadBackoffUntil > 0 && millis() < pendingPush_.cadBackoffUntil) {
            return;
        }

        const ChannelState channel = checkChannel();
        if (channel == ChannelState::BUSY) {
            pendingPush_.cadAttempts++;
            const uint8_t cappedExp = pendingPush_.cadAttempts > cad_.maxBackoffExp ? cad_.maxBackoffExp : pendingPush_.cadAttempts;
            const uint32_t backoffMs = cad_.baseBackoffMs * (1UL << cappedExp) +
                                       static_cast<uint32_t>(random(0, cad_.jitterMs + 1));
            pendingPush_.cadBackoffUntil = millis() + backoffMs;
            return;
        }
        if (channel == ChannelState::ERROR) {
            pendingPush_.cadAttempts++;
            pendingPush_.cadBackoffUntil = millis() + cad_.errorBackoffMs;
            return;
        }
    }

    uint8_t seq = 0;
    const bool ok = manager_.sendPacket(gatewayId_, MsgType::PUSH_EVENT, Priority::PRIO_HIGH,
                                        pendingPush_.flags, pendingPush_.payload, pendingPush_.payloadLen,
                                        F("PUSH_EVENT"), base_.appMaxRetries, base_.appAckTimeoutMs, &seq);
    if (!ok) {
        return;
    }

    pushInFlight_ = (pendingPush_.flags & static_cast<uint8_t>(Flags::ACK_REQUIRED)) != 0;
    pushSeq_ = seq;
    if (pendingPush_.fallbackToPoll) {
        pushFallbackArmed_ = true;
        pushFallbackSeq_ = seq;
        pushFallbackLen_ = pendingPush_.payloadLen;
        memcpy(pushFallbackPayload_, pendingPush_.payload, pendingPush_.payloadLen);
    }
    pendingPush_.active = false;
}
