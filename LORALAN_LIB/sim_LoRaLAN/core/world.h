#pragma once
// Un mundo limpio por caso: reloj, azar, canal, gateway y nodos nuevos.
// Solo puede existir uno a la vez, porque los callbacks de la biblioteca son
// punteros a funcion sin contexto.

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "channel.h"
#include "harness.h"
#include "trace.h"

struct WorldConfig {
    ChannelConfig channel{};
    uint32_t pollIntervalMs = 0;       // sin sondeo de fondo salvo que el caso lo pida
    uint32_t timeSyncIntervalMs = 0;
    bool deferredPushAck = false;
    uint32_t silenceTimeoutMs = 0;
    uint8_t gatewayId = 0x01;
    // Los descartes de tramas ajenas ocupan mucha memoria en redes muy grandes.
    bool traceDrops = true;
    // Ajusta un nodo antes de arrancarlo: otra clave, otra red, otra direccion...
    std::function<void(int, LoRaLANNode::Config&)> tweakNode;
};

class World {
public:
    explicit World(Ctx& ctx, int nodes = 1, const WorldConfig& config = WorldConfig{});
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    LoRaLANGateway& gw() { return *gateway_; }
    LoRaLANNode& node(int i) { return *nodes_[static_cast<size_t>(i)]; }
    uint8_t nodeId(int i) { return node(i).getConfig().nodeId; }
    static int dev(int nodeIndex) { return nodeIndex + 1; }   // radio del nodo i

    Trace& trace() { return trace_; }
    Channel& channel() { return channel_; }
    uint32_t now() const;

    void inject(const Fault& fault) { channel_.addFault(fault); }

    void runFor(uint32_t ms);
    bool runUntil(const std::function<bool()>& pred, uint32_t maxMs);
    // Asocia a todos y espera a que se vacie el aire. Si no se logra, el caso
    // queda No concluyente: su precondicion no se dio.
    bool joinAll(uint32_t maxMs = 30000);

    // Trama de un atacante, por el aire y sin clave.
    void attackerSend(const std::vector<uint8_t>& frame);
    // Ultima trama emitida de ese tipo: lo que capturaria un receptor cualquiera.
    std::vector<uint8_t> captured(MsgType type) const;

    void rebootGateway();
    void rebootNode(int i);

    // Puntos de entrada desde el shim de la radio y desde los callbacks.
    int radioTransmit(int radio, const uint8_t* data, size_t len);
    int radioScan(int radio);
    void appEvent(Ev kind, uint8_t type, uint16_t seq, const uint8_t* data, uint8_t len, int value);

private:
    struct ObservedState {
        bool joined = false;
        bool leaving = false;
        bool pollPending = false;
        int gwNodes = 0;
    };

    void createGateway();
    void createNode(int i);
    void tick();
    void deliver(const Delivery& delivery);
    void receiveAt(int radio, const std::vector<uint8_t>& bytes);
    void observeState();
    void traceState(int dev, const char* name, int value);

    Ctx& ctx_;
    WorldConfig config_;
    int worldIndex_;
    Trace trace_;
    Channel channel_;
    int current_ = kGatewayRadio;   // dispositivo en ejecucion, para los callbacks

    std::vector<std::unique_ptr<SX1276>> radios_;   // indice = radio
    std::unique_ptr<LoRaLANGateway> gateway_;
    std::vector<std::unique_ptr<LoRaLANNode>> nodes_;
    std::vector<uint32_t> busyUntil_;
    std::vector<ObservedState> state_;
    std::map<uint8_t, std::vector<uint8_t>> capturedByType_;
};
