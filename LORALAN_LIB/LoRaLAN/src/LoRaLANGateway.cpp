#include "LoRaLANGateway.h"

LoRaLANGateway::LoRaLANGateway(PhysicalLayer& radio)
    : LoRaLANDevice(radio) {
    resetNodeTable();
}

void LoRaLANGateway::begin(const Config& config) {
    applyBaseConfig(config, config.gatewayId);
    pollIntervalMs_ = config.pollIntervalMs;
    timeSyncIntervalMs_ = config.timeSyncIntervalMs;
    deferredPushAck_ = config.deferredPushAck;
    maxMissedPolls_ = config.maxMissedPolls;
    manager_.setIdentity(config.netId, config.gatewayId);
    resetNodeTable();
}

LoRaLANGateway::Config LoRaLANGateway::getConfig() const {
    Config cfg;
    fillBaseConfig(cfg);
    cfg.gatewayId = localId_;
    cfg.pollIntervalMs = pollIntervalMs_;
    cfg.timeSyncIntervalMs = timeSyncIntervalMs_;
    cfg.deferredPushAck = deferredPushAck_;
    cfg.maxMissedPolls = maxMissedPolls_;
    return cfg;
}

bool LoRaLANGateway::sendTimeSync(uint32_t timestampSeconds) {
    uint8_t buffer[LORALAN_HEADER_SIZE + LORALAN_MAX_PAYLOAD + LORALAN_MIC_SIZE];
    uint16_t len = 0;
    const uint16_t seq = manager_.nextSequence();

    if (!LoRaLANPacket::buildTimeSync(buffer, base_.netId, localId_, timestampSeconds, seq, len)) {
        return false;
    }

    return sendRaw(buffer, len, F("TIME_SYNC"));
}

bool LoRaLANGateway::sendPoll(uint8_t nodeId) {
    uint8_t buffer[LORALAN_HEADER_SIZE + LORALAN_MAX_PAYLOAD + LORALAN_MIC_SIZE];
    uint16_t len = 0;
    const uint16_t seq = manager_.nextSequence();

    if (!LoRaLANPacket::buildPoll(buffer, base_.netId, localId_, nodeId, seq, len)) {
        return false;
    }

    return sendRaw(buffer, len, F("POLL"));
}

bool LoRaLANGateway::sendCommand(uint8_t nodeId, const uint8_t* payload, uint8_t payloadLen) {
    if (payloadLen > LORALAN_MAX_PAYLOAD) {
        return false;
    }

    const uint8_t* key = sessionKeyFor(nodeId);
    if (key == nullptr) {
        return false;   // nodo desconocido o no asociado
    }

    uint8_t buffer[LORALAN_HEADER_SIZE + LORALAN_MAX_PAYLOAD + LORALAN_MIC_SIZE];
    uint16_t len = 0;
    const uint16_t seq = manager_.nextSequence();

    // Cifrado con la session key del nodo y nonce(netId, gatewayId, seq),
    // simetrico a lo que espera manager_.decryptPayload en el nodo.
    if (!manager_.buildSecureFrame(nodeId, MsgType::CMD, Priority::PRIO_HIGH,
                                   static_cast<uint8_t>(Flags::ACK_REQUIRED),
                                   payload, payloadLen, seq, buffer, len, key)) {
        return false;
    }

    return manager_.enqueueReliableTx(buffer, len, seq, F("CMD"),
                                      base_.appMaxRetries, base_.appAckTimeoutMs, key);
}

uint8_t LoRaLANGateway::nodeCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < numNodes_; ++i) {
        if (nodes_[i].active && nodes_[i].joined) {
            count++;
        }
    }
    return count;
}

bool LoRaLANGateway::isAlive(uint8_t nodeId, uint32_t maxAgeMs) const {
    const int idx = findNodeIndex(nodeId);
    return idx >= 0 && nodes_[idx].joined && (millis() - nodes_[idx].lastSeen) <= maxAgeMs;
}

const uint8_t* LoRaLANGateway::sessionKeyFor(uint8_t peerId) const {
    const int idx = findNodeIndex(peerId);
    if (idx < 0 || !nodes_[idx].joined) {
        return nullptr;
    }
    return nodes_[idx].sessionKey;
}

bool LoRaLANGateway::onFrame(const LoRaLANFrame& frame) {
    switch (static_cast<MsgType>(frame.header.type)) {
        case MsgType::JOIN_REQUEST:
            return onJoinRequest(frame);
        case MsgType::LEAVE:
            onLeave(frame);
            return true;
        case MsgType::PUSH_EVENT:
            if (deferredPushAck_ && LoRaLANPacket::isAckRequired(frame)) {
                onDeferredPush(frame);
                ackHandled_ = true;
            } else {
                onNodeData(frame, onPushEvent_);
            }
            return true;
        case MsgType::POLL_RESPONSE:
            onNodeData(frame, onPollResponse_);
            return true;
        default:
            return true;
    }
}

bool LoRaLANGateway::onJoinRequest(const LoRaLANFrame& frame) {
    if (frame.header.payloadLen < 2 + LORALAN_HWID_SIZE || appKey_[0] == 0) {
        return false;
    }

    const uint16_t devNonce = static_cast<uint16_t>(frame.payload[0] << 8) | frame.payload[1];
    const uint8_t* hwId = frame.payload + 2;

    // Direccionamiento dinamico: el nodo se reconoce por su identificador de
    // fabrica y conserva su direccion mientras el gateway siga en marcha. Si
    // es nuevo (o el gateway se reinicio), recibe la que pide (srcId) si esta
    // libre. La tabla vive en RAM; la persistencia la pone el nodo, que guarda
    // su direccion y la vuelve a pedir.
    int idx = findNodeByHwId(hwId);
    if (idx < 0) {
        const uint8_t assigned = chooseAddress(frame.header.srcId);
        if (assigned == 0 || numNodes_ >= kMaxNodes) {
            return false;   // tabla o espacio de direcciones lleno
        }
        idx = numNodes_++;
        nodes_[idx] = NodeEntry{};
        nodes_[idx].nodeId = assigned;
        memcpy(nodes_[idx].hwId, hwId, LORALAN_HWID_SIZE);
    }
    NodeEntry& node = nodes_[idx];

    // Anti-repeticion del JOIN: un JOIN_REQUEST capturado y reinyectado haria
    // que el gateway volviese a derivar la clave y reiniciase la sesion. No
    // estorba a los reintentos legitimos: el nodo sortea un DevNonce nuevo en
    // cada intento.
    if (node.joined && devNonce == node.lastDevNonce) {
        return false;
    }

    // La clave se deriva con la direccion asignada, que es la que el nodo
    // usara como origen a partir de ahora.
    if (!loralan_crypto::deriveSessionKey(appKey_, base_.netId, node.nodeId, devNonce, node.sessionKey)) {
        return false;   // sin KDF no hay sesion ni JOIN_ACCEPT
    }
    node.lastSeen = millis();
    node.active = true;
    node.missedPolls = 0;
    node.lastDevNonce = devNonce;
    node.joined = true;
    // Sesion nueva: se resincroniza el contador de secuencia, de modo que un
    // nodo que ha reiniciado no queda bloqueado por la ventana.
    node.haveSeq = false;
    node.ack = AckState::NONE;

    sendJoinAccept(node.nodeId, devNonce, 0x00, node.hwId);
    if (onNodeJoined_ != nullptr) {
        onNodeJoined_(node.nodeId);
    }
    return true;
}

void LoRaLANGateway::onLeave(const LoRaLANFrame& frame) {
    const int idx = findNodeIndex(frame.header.srcId);
    if (idx >= 0 && nodes_[idx].joined) {
        // El ACK se firma con la clave de sesion: va antes de darlo de baja.
        // Si se pierde, la repeticion del LEAVE ya no pasa el MIC y el nodo
        // termina la baja al agotar sus reintentos.
        if (LoRaLANPacket::isAckRequired(frame)) {
            sendAck(frame.header.srcId, frame.header.seq);
            ackHandled_ = true;
        }
        nodes_[idx].joined = false;
        nodes_[idx].active = false;
        if (onNodeLeft_ != nullptr) {
            onNodeLeft_(nodes_[idx].nodeId);
        }
    }
}

void LoRaLANGateway::onNodeData(const LoRaLANFrame& frame, EventCallback cb) {
    const int idx = findNodeIndex(frame.header.srcId);
    if (idx >= 0) {
        nodes_[idx].lastSeen = millis();
        nodes_[idx].active = true;
        nodes_[idx].missedPolls = 0;

        // Anti-repeticion. Una trama ya vista NO se entrega otra vez a la app,
        // pero onFrame devuelve true igualmente para que se reenvie el ACK: si
        // el emisor retransmitio es porque no le llego, y sin ACK seguiria.
        if (!acceptSeq(frame.header.seq, nodes_[idx].lastSeq, nodes_[idx].seqSeen, nodes_[idx].haveSeq)) {
            return;
        }
    }

    deliverNodeData(frame, cb);
}

void LoRaLANGateway::onDeferredPush(const LoRaLANFrame& frame) {
    const uint8_t src = frame.header.srcId;
    const uint16_t seq = frame.header.seq;
    const int idx = findNodeIndex(src);
    if (idx < 0) {
        return;
    }
    NodeEntry& node = nodes_[idx];
    node.lastSeen = millis();
    node.active = true;
    node.missedPolls = 0;

    if (!acceptSeq(seq, node.lastSeq, node.seqSeen, node.haveSeq)) {
        // Retransmision: el nodo no recibio el ACK. No se entrega otra vez a la app.
        if (node.ackSeq == seq && node.ack == AckState::READY) {
            sendAck(src, seq, node.ackPayload, node.ackLen);   // misma respuesta
        } else if (node.ackSeq != seq || node.ack != AckState::PENDING) {
            sendAck(src, seq);   // duplicado antiguo sin respuesta guardada
        }
        // PENDING: la app aun no ha respondido; el nodo volvera a reintentar.
        return;
    }

    // Se marca antes de avisar a la app: puede llamar a ackPush() desde el callback.
    node.ack = AckState::PENDING;
    node.ackSeq = seq;
    node.ackLen = 0;
    deliverNodeData(frame, onPushEvent_);
}

bool LoRaLANGateway::ackPush(uint8_t nodeId, uint16_t seq, const uint8_t* payload, uint8_t payloadLen) {
    const int idx = findNodeIndex(nodeId);
    if (idx < 0 || payloadLen > LORALAN_MAX_PAYLOAD || (payloadLen > 0 && payload == nullptr)) {
        return false;
    }
    NodeEntry& node = nodes_[idx];
    if (!node.joined || node.ack != AckState::PENDING || node.ackSeq != seq) {
        return false;
    }

    node.ackLen = payloadLen;
    if (payloadLen > 0) {
        memcpy(node.ackPayload, payload, payloadLen);
    }
    // READY aunque el envio falle: la retransmision del nodo recibira la copia.
    node.ack = AckState::READY;
    sendAck(nodeId, seq, node.ackPayload, node.ackLen);
    return true;
}

void LoRaLANGateway::deliverNodeData(const LoRaLANFrame& frame, EventCallback cb) {
    uint8_t decrypted[LORALAN_MAX_PAYLOAD];
    uint8_t decryptedLen = 0;
    const uint8_t* payloadPtr = frame.payload;
    uint8_t payloadLen = frame.header.payloadLen;
    if (LoRaLANManager::decryptWithKey(sessionKeyFor(frame.header.srcId), frame, decrypted, decryptedLen) &&
        decryptedLen > 0) {
        payloadPtr = decrypted;
        payloadLen = decryptedLen;
    }

    if (cb != nullptr) {
        cb(frame, payloadPtr, payloadLen);
    }
}

bool LoRaLANGateway::sendJoinAccept(uint8_t assignedId, uint16_t devNonce, uint8_t status,
                                    const uint8_t hwId[LORALAN_HWID_SIZE]) {
    uint8_t buffer[LORALAN_HEADER_SIZE + LORALAN_MAX_PAYLOAD + LORALAN_MIC_SIZE];
    uint16_t len = 0;
    const uint16_t seq = manager_.nextSequence();

    // Va dirigido a la direccion asignada: tras adoptarla, el nodo confirma con ACK.
    if (!LoRaLANPacket::buildJoinAccept(buffer, base_.netId, localId_, assignedId, assignedId, devNonce, status,
                                        hwId, seq, len)) {
        return false;
    }

    return sendRaw(buffer, len, F("JOIN_ACCEPT"));
}

// Tareas periodicas: TIME_SYNC y POLL round-robin. Se sondean todos los nodos
// asociados, tambien los inactivos, para que puedan reactivarse al contestar.
void LoRaLANGateway::onProcess() {
    const uint32_t now = millis();

    if (timeSyncIntervalMs_ > 0 && now - lastTimeSyncMs_ >= timeSyncIntervalMs_) {
        sendTimeSync(now / 1000UL);
        lastTimeSyncMs_ = now;
    }

    if (pollIntervalMs_ > 0 && now - lastPollMs_ >= pollIntervalMs_ && numNodes_ > 0) {
        for (uint8_t attempts = 0; attempts < numNodes_; ++attempts) {
            // El modulo se aplica al guardar: con ++ y uint8_t, el indice se desajustaba
            // al dar la vuelta si numNodes_ no divide a 256.
            const uint8_t idx = pollIndex_ % numNodes_;
            pollIndex_ = static_cast<uint8_t>((idx + 1) % numNodes_);
            NodeEntry& node = nodes_[idx];
            if (!node.joined) {
                continue;
            }
            // Ninguna trama del nodo en los ultimos maxMissedPolls sondeos.
            if (maxMissedPolls_ > 0 && node.active && node.missedPolls >= maxMissedPolls_) {
                node.active = false;
                if (onNodeInactive_ != nullptr) {
                    onNodeInactive_(node.nodeId);
                }
            }
            if (node.missedPolls < 255) {
                node.missedPolls++;
            }
            sendPoll(node.nodeId);
            break;
        }
        lastPollMs_ = now;
    }
}

void LoRaLANGateway::resetNodeTable() {
    for (auto& node : nodes_) {
        node = NodeEntry{};
    }
    numNodes_ = 0;
    pollIndex_ = 0;
}

int LoRaLANGateway::findNodeIndex(uint8_t nodeId) const {
    for (uint8_t i = 0; i < numNodes_; ++i) {
        if (nodes_[i].nodeId == nodeId) {
            return i;
        }
    }
    return -1;
}

int LoRaLANGateway::findNodeByHwId(const uint8_t hwId[LORALAN_HWID_SIZE]) const {
    for (uint8_t i = 0; i < numNodes_; ++i) {
        if (memcmp(nodes_[i].hwId, hwId, LORALAN_HWID_SIZE) == 0) {
            return i;
        }
    }
    return -1;
}

uint8_t LoRaLANGateway::chooseAddress(uint8_t preferred) const {
    auto usable = [this](uint8_t id) {
        return id != 0 && id != 0xFF && id != localId_ && findNodeIndex(id) < 0;
    };
    if (usable(preferred)) {
        return preferred;
    }
    // Sin preferencia o con la preferida ocupada se asigna del rango dinamico,
    // de arriba abajo; el rango manual nunca se reparte.
    for (uint8_t id = 0xFE; id >= kFirstDynamicId; --id) {
        if (usable(id)) {
            return id;
        }
    }
    return 0;
}

bool LoRaLANGateway::nodeHwId(uint8_t nodeId, uint8_t out[LORALAN_HWID_SIZE]) const {
    const int idx = findNodeIndex(nodeId);
    if (idx < 0 || out == nullptr) {
        return false;
    }
    memcpy(out, nodes_[idx].hwId, LORALAN_HWID_SIZE);
    return true;
}
