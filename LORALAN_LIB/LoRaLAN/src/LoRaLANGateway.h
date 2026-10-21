#pragma once

#include "LoRaLANDevice.h"

// Gateway LoRaLAN: asocia nodos (JOIN), los sondea (POLL), difunde la hora
// (TIME_SYNC), recibe sus eventos (PUSH) y les envia comandos cifrados (CMD).
//
//   LoRaLANGateway gateway(radio);
//   LoRaLANGateway::Config cfg;
//   cfg.netId = 0x42; cfg.gatewayId = 0x00; cfg.appKey = APPKEY;
//   gateway.begin(cfg);
//   // loop(): gateway.handleReceived(buf, len); gateway.process();
class LoRaLANGateway final : public LoRaLANDevice {
public:
    static constexpr uint8_t kMaxNodes = 32;

    struct Config : BaseConfig {
        uint8_t gatewayId = 0;
        uint32_t pollIntervalMs = 2000UL;      // 0 = sin POLL automatico
        uint32_t timeSyncIntervalMs = 30000UL; // 0 = sin TIME_SYNC automatico
        // true: el ACK de cada PUSH_EVENT no sale solo; la app lo envia con
        // ackPush() cuando tiene la respuesta (p. ej. del servidor).
        bool deferredPushAck = false;
        // POLL seguidos sin ninguna trama del nodo para declararlo inactivo
        // (0 = no detectar). Cualquier trama posterior lo reactiva.
        uint8_t maxMissedPolls = 3;
    };

    // Callback de altas/bajas de nodos.
    using NodeStatusCallback = void (*)(uint8_t nodeId, uint8_t nodeType);

    explicit LoRaLANGateway(PhysicalLayer& radio);

    void begin(const Config& config);
    Config getConfig() const;

    void setPollIntervalMs(uint32_t intervalMs) { pollIntervalMs_ = intervalMs; }
    void setTimeSyncIntervalMs(uint32_t intervalMs) { timeSyncIntervalMs_ = intervalMs; }
    void setDeferredPushAck(bool deferred) { deferredPushAck_ = deferred; }
    void setMaxMissedPolls(uint8_t maxMissed) { maxMissedPolls_ = maxMissed; }

    // ACK diferido de un PUSH_EVENT (seq = frame.header.seq del callback), con
    // un payload corto de aplicacion. false si ese PUSH ya no esta pendiente:
    // ya respondido, sustituido por un PUSH posterior o el nodo se re-asocio.
    // Si el ACK se pierde, las retransmisiones del nodo reciben la misma respuesta.
    bool ackPush(uint8_t nodeId, uint16_t seq, const uint8_t* payload, uint8_t payloadLen);

    // Transmision manual (process() ya los envia segun los intervalos).
    bool sendTimeSync(uint32_t timestampSeconds);
    bool sendPoll(uint8_t nodeId);
    // Comando cifrado gateway -> nodo, con ACK y reintentos (requiere nodo asociado).
    bool sendCommand(uint8_t nodeId, const uint8_t* payload, uint8_t payloadLen);

    // Estado de la tabla de nodos.
    uint8_t nodeCount() const;
    bool isAlive(uint8_t nodeId, uint32_t maxAgeMs) const;

    void setOnPushEvent(EventCallback cb) { onPushEvent_ = cb; }
    void setOnPollResponse(EventCallback cb) { onPollResponse_ = cb; }
    void setOnNodeJoined(NodeStatusCallback cb) { onNodeJoined_ = cb; }
    void setOnNodeLeft(NodeStatusCallback cb) { onNodeLeft_ = cb; }
    // Nodo asociado que ha dejado de contestar a maxMissedPolls sondeos.
    void setOnNodeInactive(NodeStatusCallback cb) { onNodeInactive_ = cb; }

private:
    enum class AckState : uint8_t { NONE, PENDING, READY };

    struct NodeEntry {
        uint8_t nodeId = 0;
        uint8_t nodeType = 0;
        uint32_t lastSeen = 0;
        uint16_t lastDevNonce = 0;
        uint8_t sessionKey[16] = {0};
        // Anti-repeticion: ultima secuencia aceptada de este nodo. haveSeq se
        // pone a false en cada JOIN para resincronizar (un nodo que reinicia
        // arranca su contador de nuevo).
        uint16_t lastSeq = 0;
        bool haveSeq = false;
        // POLL enviados desde la ultima trama recibida del nodo.
        uint8_t missedPolls = 0;
        bool active = false;
        bool joined = false;
        // ACK diferido del ultimo PUSH: PENDING hasta ackPush(), luego READY
        // con la respuesta guardada para reenviarla a las retransmisiones.
        AckState ack = AckState::NONE;
        uint16_t ackSeq = 0;
        uint8_t ackLen = 0;
        uint8_t ackPayload[LORALAN_MAX_PAYLOAD] = {0};
    };

    const uint8_t* sessionKeyFor(uint8_t peerId) const override;
    bool onFrame(const LoRaLANFrame& frame) override;
    void onProcess() override;

    bool onJoinRequest(const LoRaLANFrame& frame);
    void onLeave(const LoRaLANFrame& frame);
    // PUSH_EVENT y POLL_RESPONSE: anti-repeticion, descifrado y entrega a la app.
    void onNodeData(const LoRaLANFrame& frame, EventCallback cb);
    // PUSH_EVENT con ACK diferido.
    void onDeferredPush(const LoRaLANFrame& frame);
    void deliverNodeData(const LoRaLANFrame& frame, EventCallback cb);

    bool sendJoinAccept(uint8_t dstId, uint8_t assignedId, uint16_t devNonce, uint8_t status);

    void resetNodeTable();
    int findNodeIndex(uint8_t nodeId) const;
    int findOrCreateNode(uint8_t nodeId, uint8_t nodeType);

    uint32_t pollIntervalMs_ = 2000UL;
    uint32_t timeSyncIntervalMs_ = 30000UL;
    bool deferredPushAck_ = false;
    uint8_t maxMissedPolls_ = 3;

    NodeEntry nodes_[kMaxNodes] = {};
    uint8_t numNodes_ = 0;
    uint8_t pollIndex_ = 0;
    uint32_t lastPollMs_ = 0;
    uint32_t lastTimeSyncMs_ = 0;

    EventCallback onPushEvent_ = nullptr;
    EventCallback onPollResponse_ = nullptr;
    NodeStatusCallback onNodeJoined_ = nullptr;
    NodeStatusCallback onNodeLeft_ = nullptr;
    NodeStatusCallback onNodeInactive_ = nullptr;
};
