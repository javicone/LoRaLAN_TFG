#pragma once

#include "LoRaLANDevice.h"

// Nodo LoRaLAN: se asocia al gateway (JOIN), envia eventos asincronos con CAD
// (PUSH), responde a los sondeos (POLL), recibe comandos cifrados (CMD) y
// ajusta su reloj con TIME_SYNC.
//
//   LoRaLANNode node(radio);
//   LoRaLANNode::Config cfg;
//   cfg.netId = 0x42; cfg.nodeId = 0x10; cfg.appKey = APPKEY;
//   node.begin(cfg);
//   // loop(): node.handleReceived(buf, len); node.process();
//   //         if (node.isJoined()) node.queuePushEvent(data, len);
class LoRaLANNode final : public LoRaLANDevice {
public:
    // Backoff CAD para PUSH y JOIN.
    struct CadConfig {
        bool enabled = true;
        uint32_t baseBackoffMs = 40UL;
        uint8_t maxBackoffExp = 5;
        uint32_t jitterMs = 50UL;
        uint32_t errorBackoffMs = 60UL;
        bool joinCadEnabled = true;
    };

    struct Config : BaseConfig {
        uint8_t nodeId = 0;
        uint8_t gatewayId = 0;
        uint8_t nodeType = 0x01;
        uint32_t joinRetryMs = 5000UL;   // 0 = sin JOIN automatico
        CadConfig cad{};
    };

    // Poll bajo demanda: rellena payload y devuelve true si hay datos.
    using PollRequestCallback = bool (*)(uint8_t* payload, uint8_t& payloadLen);
    // Desenlace de un PUSH con ACK: delivered = llego el ACK; ackPayload es la
    // respuesta de aplicacion que traia (ackLen 0 si ninguna o si no llego).
    using PushAckCallback = void (*)(bool delivered, const uint8_t* ackPayload, uint8_t ackLen);

    explicit LoRaLANNode(PhysicalLayer& radio);

    void begin(const Config& config);
    Config getConfig() const;

    void setJoinRetryMs(uint32_t intervalMs) { joinRetryMs_ = intervalMs; }
    void setCadPolicy(const CadConfig& cad) { cad_ = cad; }

    // Evento asincrono con CAD. fallbackToPoll: si agota reintentos sin ACK,
    // el payload se rescata y se entrega en la siguiente ventana POLL.
    bool queuePushEvent(const uint8_t* payload, uint8_t payloadLen, bool ackRequired = true,
                        bool fallbackToPoll = false);
    // Payload para el proximo POLL del gateway.
    bool queuePollPayload(const uint8_t* payload, uint8_t payloadLen);
    // True mientras el payload encolado no ha sido consumido por un POLL.
    bool isPollPayloadPending() const { return pollPayloadPending_; }

    // Asociacion.
    bool requestJoin();
    void resetJoin();
    // Baja limpia: notifica LEAVE y desactiva el auto-join.
    bool leave();
    bool isJoined() const { return isJoined_; }

    // Diferencia entre el reloj del gateway (ultimo TIME_SYNC) y millis().
    int32_t timeOffsetMs() const { return timeOffsetMs_; }

    void setOnJoinAccepted(EventCallback cb) { onJoinAccepted_ = cb; }
    // Comando recibido del gateway (ya descifrado).
    void setOnCommand(EventCallback cb) { onCommand_ = cb; }
    // Genera el payload de POLL_RESPONSE si no hay uno encolado.
    void setOnPollRequest(PollRequestCallback cb) { onPollRequest_ = cb; }
    // Resultado de cada PUSH con ACK (tambien si se agotan los reintentos).
    void setOnPushAck(PushAckCallback cb) { onPushAck_ = cb; }

private:
    struct PendingPush {
        bool active = false;
        uint8_t flags = 0;
        uint8_t cadAttempts = 0;
        uint32_t cadBackoffUntil = 0;
        uint8_t payload[LORALAN_MAX_PAYLOAD] = {0};
        uint8_t payloadLen = 0;
        bool fallbackToPoll = false;
    };

    const uint8_t* sessionKeyFor(uint8_t peerId) const override;
    bool onFrame(const LoRaLANFrame& frame) override;
    void onProcess() override;

    bool onJoinAccept(const LoRaLANFrame& frame);
    void onTimeSync(const LoRaLANFrame& frame);
    void onCommand(const LoRaLANFrame& frame);
    void onPoll();

    void sendJoinRequest();
    void processPendingPush();

    uint8_t gatewayId_ = 0;
    uint8_t nodeType_ = 0x01;
    uint32_t joinRetryMs_ = 5000UL;
    CadConfig cad_{};

    bool isJoined_ = false;
    bool autoJoin_ = true;
    uint16_t lastDevNonce_ = 0;
    uint32_t lastJoinAttemptMs_ = 0;
    int32_t timeOffsetMs_ = 0;

    PendingPush pendingPush_{};
    // PUSH con ACK en vuelo, para casar su resultado en onProcess().
    bool pushInFlight_ = false;
    uint8_t pushSeq_ = 0;

    uint8_t pollPayload_[LORALAN_MAX_PAYLOAD] = {0};
    uint8_t pollPayloadLen_ = 0;
    bool pollPayloadPending_ = false;

    // Copia del PUSH en vuelo para rescatarlo hacia la ventana POLL si no llega ACK.
    bool pushFallbackArmed_ = false;
    uint8_t pushFallbackSeq_ = 0;
    uint8_t pushFallbackPayload_[LORALAN_MAX_PAYLOAD] = {0};
    uint8_t pushFallbackLen_ = 0;

    EventCallback onJoinAccepted_ = nullptr;
    EventCallback onCommand_ = nullptr;
    PollRequestCallback onPollRequest_ = nullptr;
    PushAckCallback onPushAck_ = nullptr;
};
