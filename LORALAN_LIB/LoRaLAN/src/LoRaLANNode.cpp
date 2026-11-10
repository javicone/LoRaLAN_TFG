#include "LoRaLANNode.h"

LoRaLANNode::LoRaLANNode(PhysicalLayer& radio)
    : LoRaLANDevice(radio) {}

void LoRaLANNode::begin(const Config& config) {
    applyBaseConfig(config, config.nodeId);
    gatewayId_ = config.gatewayId;
    memcpy(hwId_, config.hwId, LORALAN_HWID_SIZE);
    joinRetryMs_ = config.joinRetryMs;
    silenceTimeoutMs_ = config.silenceTimeoutMs;
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
    memcpy(cfg.hwId, hwId_, LORALAN_HWID_SIZE);
    cfg.joinRetryMs = joinRetryMs_;
    cfg.silenceTimeoutMs = silenceTimeoutMs_;
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

bool LoRaLANNode::queuePollPayload(const uint8_t* payload, uint8_t payloadLen, bool ackRequired) {
    if (!isJoined_ || payloadLen > LORALAN_MAX_PAYLOAD) {
        return false;
    }

    pollPayloadLen_ = payloadLen;
    if (payloadLen > 0 && payload != nullptr) {
        memcpy(pollPayload_, payload, payloadLen);
    }
    pollPayloadPending_ = true;
    pollPayloadAck_ = ackRequired;
    // Un ACK que llegue ahora es del payload anterior: no debe borrar este.
    pollInFlight_ = false;
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
    pollInFlight_ = false;   // el payload encolado se conserva para la nueva sesion
    manager_.clearSessionKey();
}

bool LoRaLANNode::leave() {
    if (!isJoined_) {
        return false;
    }

    autoJoin_ = false;
    // LEAVE con ACK: la clave de sesion se conserva hasta que llega el ACK (o
    // se agotan los reintentos), porque hace falta para verificarlo; el borrado
    // lo hace onProcess() al recoger el resultado del envio.
    if (!manager_.sendPacket(gatewayId_, MsgType::LEAVE, Priority::PRIO_MEDIUM,
                             static_cast<uint8_t>(Flags::ACK_REQUIRED), nullptr, 0, F("LEAVE"),
                             base_.appMaxRetries, base_.appAckTimeoutMs, &leaveSeq_)) {
        resetJoin();   // TX ocupado: baja local sin aviso al gateway
        return false;
    }
    isJoined_ = false;   // ya no acepta PUSH ni POLL mientras espera el ACK
    leaving_ = true;
    return true;
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
            if (!isJoined_) {          // de baja: ni se ejecuta ni se confirma
                ackHandled_ = true;
                return true;
            }
            lastHeardMs_ = millis();
            onCommand(frame);
            return true;
        case MsgType::POLL:
            // El POLL mantiene al dia la secuencia del gateway aunque no haya ordenes;
            // uno repetido no se responde ni cuenta como senal de vida.
            if (isJoined_ && !acceptSeq(frame.header.seq, gwLastSeq_, gwSeqSeen_, gwHaveSeq_)) {
                return true;
            }
            lastHeardMs_ = millis();
            onPoll();
            return true;
        default:
            return true;
    }
}

bool LoRaLANNode::onJoinAccept(const LoRaLANFrame& frame) {
    if (frame.header.payloadLen < 4 + LORALAN_HWID_SIZE) {
        return false;
    }

    const uint8_t assignedId = frame.payload[0];
    const uint16_t devNonce = static_cast<uint16_t>(frame.payload[1] << 8) | frame.payload[2];
    const uint8_t status = frame.payload[3];
    // Con varios nodos asociandose a la vez, el identificador de fabrica
    // distingue cual es el JOIN_ACCEPT propio.
    if (isJoined_ || status != 0x00 || devNonce != lastDevNonce_ ||
        memcmp(frame.payload + 4, hwId_, LORALAN_HWID_SIZE) != 0 ||
        assignedId == 0 || assignedId == 0xFF) {
        return false;
    }

    // Se adopta la direccion asignada antes de derivar la clave: el gateway la
    // deriva con ella.
    if (assignedId != localId_) {
        localId_ = assignedId;
        manager_.setIdentity(base_.netId, localId_);
    }

    uint8_t sessionKey[16];
    if (!loralan_crypto::deriveSessionKey(appKey_, base_.netId, localId_, devNonce, sessionKey)) {
        return false;
    }
    manager_.setSessionKey(sessionKey);
    isJoined_ = true;
    gwHaveSeq_ = false;   // sesion nueva: la secuencia del gateway no se compara con la anterior
    lastHeardMs_ = millis();

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
    // Retransmision o repeticion: no se ejecuta otra vez, pero handleReceived()
    // vuelve a confirmar para que el gateway deje de reintentar.
    if (!acceptSeq(frame.header.seq, gwLastSeq_, gwSeqSeen_, gwHaveSeq_)) {
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
    const bool ackRequired = pollPayloadPending_ && pollPayloadAck_;

    // Poll bajo demanda: si no hay payload encolado, se lo pedimos a la app.
    uint8_t onDemand[LORALAN_MAX_PAYLOAD];
    if (payload == nullptr && onPollRequest_ != nullptr) {
        uint8_t n = 0;
        if (onPollRequest_(onDemand, n) && n > 0 && n <= LORALAN_MAX_PAYLOAD) {
            payload = onDemand;
            payloadLen = n;
        }
    }

    // Solo pide ACK el payload encolado que lo requiere: la telemetria y el
    // "sigo vivo" se repiten en el siguiente POLL si se pierden.
    uint16_t seq = 0;
    const bool ok = manager_.sendPacket(gatewayId_, MsgType::POLL_RESPONSE, Priority::PRIO_LOW,
                                        ackRequired ? static_cast<uint8_t>(Flags::ACK_REQUIRED) : 0,
                                        payload, payloadLen, F("POLL_RESPONSE"),
                                        base_.appMaxRetries, base_.appAckTimeoutMs, &seq);
    if (!ok || !pollPayloadPending_) {
        return;   // si fallo, el payload encolado sigue pendiente para otro POLL
    }
    if (ackRequired) {
        pollInFlight_ = true;   // se borra al llegar el ACK, en onProcess()
        pollSeq_ = seq;
    } else {
        pollPayloadPending_ = false;
        pollPayloadLen_ = 0;
    }
}

// Tareas periodicas: silencio del gateway, reintento de JOIN, desenlace del
// PUSH y TX pendiente.
void LoRaLANNode::onProcess() {
    const uint32_t now = millis();

    // Sin noticias del gateway con la clave de sesion: si se reinicio, ya no
    // conoce al nodo y descarta sus tramas. No se emite LEAVE (un gateway
    // reiniciado no podria verificarlo); el JOIN con DevNonce nuevo rehace la
    // sesion en cualquier caso. TIME_SYNC no cuenta: va con la clave maestra.
    if (isJoined_ && silenceTimeoutMs_ > 0 && now - lastHeardMs_ >= silenceTimeoutMs_) {
        Serial.println(F("Gateway en silencio: sesion perdida, se repite el JOIN"));
        resetJoin();
    }

    if (!isJoined_ && autoJoin_ && joinRetryMs_ > 0 && now - lastJoinAttemptMs_ >= joinRetryMs_ + joinJitterMs_) {
        sendJoinRequest();
        lastJoinAttemptMs_ = now;
        // Parte aleatoria: los nodos que arrancan a la vez dejan de coincidir.
        joinJitterMs_ = static_cast<uint32_t>(random(0, joinRetryMs_ / 4 + 1));
    }

    uint16_t doneSeq = 0;
    bool doneAcked = false;
    if (manager_.takeTxResult(doneSeq, doneAcked)) {
        if (doneAcked) {
            lastHeardMs_ = now;
        }
        // Desenlace del LEAVE, con ACK o sin el: ahora si se borra la clave.
        if (leaving_ && doneSeq == leaveSeq_) {
            leaving_ = false;
            resetJoin();
        }
        // POLL_RESPONSE con ACK: entregada, se borra; si no, sale en el siguiente POLL.
        if (pollInFlight_ && doneSeq == pollSeq_) {
            pollInFlight_ = false;
            if (doneAcked) {
                pollPayloadPending_ = false;
                pollPayloadLen_ = 0;
            } else {
                Serial.println(F("POLL_RESPONSE sin ACK: el payload se repite en el siguiente POLL"));
            }
        }
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
    const uint16_t seq = manager_.nextSequence();

    // localId_ es la direccion preferida: la configurada o la ultima asignada.
    if (!LoRaLANPacket::buildJoinRequest(buffer, base_.netId, localId_, lastDevNonce_, hwId_, seq, len)) {
        return;
    }

    if (cad_.joinCadEnabled && checkChannel() == ChannelState::BUSY) {
        return;   // canal ocupado: se reintenta en el siguiente joinRetryMs
    }
    sendRaw(buffer, len, F("JOIN_REQUEST"));
}

// Gestiona el backoff CAD y lanza el PUSH pendiente.
// El evento no llego a salir: mismo desenlace que al agotar los reintentos. El aviso
// va el ultimo porque la aplicacion puede encolar otro evento desde el callback.
void LoRaLANNode::abandonPendingPush() {
    pendingPush_.active = false;
    Serial.println(F("PUSH sin emitir: canal no disponible tras los intentos de escucha"));
    if (pendingPush_.fallbackToPoll) {
        queuePollPayload(pendingPush_.payload, pendingPush_.payloadLen);
    }
    if (onPushAck_ != nullptr) {
        onPushAck_(false, nullptr, 0);
    }
}

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
            if (pendingPush_.cadAttempts >= cad_.maxAttempts) {
                abandonPendingPush();
            }
            return;
        }
        if (channel == ChannelState::ERROR) {
            pendingPush_.cadAttempts++;
            pendingPush_.cadBackoffUntil = millis() + cad_.errorBackoffMs;
            if (pendingPush_.cadAttempts >= cad_.maxAttempts) {
                abandonPendingPush();
            }
            return;
        }
    }

    uint16_t seq = 0;
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
