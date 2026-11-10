#include "world.h"

#include <algorithm>

// Globales que el shim de Arduino declara como extern.
uint32_t sim_now_ms = 0;
const char* sim_serial_tag = "SIM";
uint32_t sim_rand_state = 42;
bool sim_serial_quiet = true;
bool sim_serial_mic_error = false;
SimSerial Serial;

namespace {

World* g_world = nullptr;

const uint8_t kAppKey[16] = {0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
                             0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C};
constexpr uint8_t kNetId = 0x10;
constexpr uint8_t kFirstNodeId = 0x20;

void onGwPush(const LoRaLANFrame& f, const uint8_t* p, uint8_t n) {
    g_world->appEvent(Ev::APP_PUSH, f.header.type, f.header.seq, p, n, f.header.srcId);
}
void onGwPollResp(const LoRaLANFrame& f, const uint8_t* p, uint8_t n) {
    g_world->appEvent(Ev::APP_POLL_RESP, f.header.type, f.header.seq, p, n, f.header.srcId);
}
void onGwJoined(uint8_t id) { g_world->appEvent(Ev::APP_NODE_JOINED, 0, 0, nullptr, 0, id); }
void onGwLeft(uint8_t id) { g_world->appEvent(Ev::APP_NODE_LEFT, 0, 0, nullptr, 0, id); }
void onGwInactive(uint8_t id) { g_world->appEvent(Ev::APP_NODE_INACTIVE, 0, 0, nullptr, 0, id); }

void onNodeJoinAccepted(const LoRaLANFrame& f, const uint8_t*, uint8_t) {
    g_world->appEvent(Ev::APP_JOIN_ACCEPTED, f.header.type, f.header.seq, nullptr, 0, 0);
}
void onNodeCommand(const LoRaLANFrame& f, const uint8_t* p, uint8_t n) {
    g_world->appEvent(Ev::APP_CMD, f.header.type, f.header.seq, p, n, 0);
}
void onNodePushAck(bool delivered, const uint8_t* p, uint8_t n) {
    g_world->appEvent(Ev::APP_PUSH_ACK, 0, 0, p, n, delivered ? 1 : 0);
}

}  // namespace

// Las dos funciones que el shim de RadioLib espera del canal.
int sim_channel_transmit(int radioId, const uint8_t* data, size_t len) {
    return g_world->radioTransmit(radioId, data, len);
}
int sim_channel_scan(int radioId) { return g_world->radioScan(radioId); }

World::World(Ctx& ctx, int nodes, const WorldConfig& config)
    : ctx_(ctx),
      config_(config),
      worldIndex_(ctx.nextWorldIndex()),
      channel_(trace_, config.channel, ctx.seed()) {
    g_world = this;
    sim_now_ms = 0;
    sim_rand_state = ctx.seed();
    sim_serial_tag = "SIM";
    sim_serial_mic_error = false;

    const size_t radios = static_cast<size_t>(nodes) + 1;
    busyUntil_.assign(radios, 0);
    state_.assign(radios, ObservedState{});
    for (size_t r = 0; r < radios; ++r) radios_.push_back(std::make_unique<SX1276>(static_cast<int>(r)));

    createGateway();
    nodes_.resize(static_cast<size_t>(nodes));
    for (int i = 0; i < nodes; ++i) createNode(i);
}

World::~World() {
    for (const std::string& fault : channel_.unfiredFaults()) ctx_.addNotProduced(fault);
    ctx_.addTrace(trace_.toCsv(worldIndex_));
    g_world = nullptr;
}

uint32_t World::now() const { return sim_now_ms; }

void World::createGateway() {
    gateway_ = std::make_unique<LoRaLANGateway>(*radios_[kGatewayRadio]);
    LoRaLANGateway::Config cfg;
    cfg.netId = kNetId;
    cfg.gatewayId = config_.gatewayId;
    cfg.appKey = kAppKey;
    cfg.pollIntervalMs = config_.pollIntervalMs;
    cfg.timeSyncIntervalMs = config_.timeSyncIntervalMs;
    cfg.deferredPushAck = config_.deferredPushAck;
    gateway_->begin(cfg);
    gateway_->setOnPushEvent(onGwPush);
    gateway_->setOnPollResponse(onGwPollResp);
    gateway_->setOnNodeJoined(onGwJoined);
    gateway_->setOnNodeLeft(onGwLeft);
    gateway_->setOnNodeInactive(onGwInactive);
}

void World::createNode(int i) {
    const size_t idx = static_cast<size_t>(i);
    nodes_[idx] = std::make_unique<LoRaLANNode>(*radios_[idx + 1]);
    LoRaLANNode::Config cfg;
    cfg.netId = kNetId;
    cfg.nodeId = static_cast<uint8_t>(kFirstNodeId + i);
    cfg.gatewayId = config_.gatewayId;
    cfg.appKey = kAppKey;
    const uint8_t hwId[LORALAN_HWID_SIZE] = {0x02, 0, 0, 0, 0, static_cast<uint8_t>(kFirstNodeId + i)};
    memcpy(cfg.hwId, hwId, LORALAN_HWID_SIZE);
    cfg.joinRetryMs = 1000u + 137u * static_cast<uint32_t>(i);   // reparte los JOIN del arranque
    cfg.silenceTimeoutMs = config_.silenceTimeoutMs;
    if (config_.tweakNode) config_.tweakNode(i, cfg);
    nodes_[idx]->begin(cfg);
    nodes_[idx]->setOnJoinAccepted(onNodeJoinAccepted);
    nodes_[idx]->setOnCommand(onNodeCommand);
    nodes_[idx]->setOnPushAck(onNodePushAck);
}

void World::rebootGateway() {
    gateway_.reset();
    createGateway();
}

void World::rebootNode(int i) {
    nodes_[static_cast<size_t>(i)].reset();
    createNode(i);
}

int World::radioTransmit(int radio, const uint8_t* data, size_t len) {
    LoRaLANFrame parsed;
    if (LoRaLANPacket::parse(data, static_cast<uint16_t>(len), parsed)) {
        capturedByType_[parsed.header.type].assign(data, data + len);
    }
    // transmit() bloquea en la placa: dos emisiones seguidas de un mismo dispositivo
    // (TIME_SYNC y POLL en el mismo process()) salen una tras otra, nunca a la vez.
    const uint32_t start = std::max(sim_now_ms, busyUntil_[static_cast<size_t>(radio)]);
    busyUntil_[static_cast<size_t>(radio)] = channel_.transmit(radio, data, len, start);
    return RADIOLIB_ERR_NONE;
}

int World::radioScan(int radio) { return channel_.scan(radio, sim_now_ms); }

void World::appEvent(Ev kind, uint8_t type, uint16_t seq, const uint8_t* data, uint8_t len, int value) {
    Event e;
    e.t = sim_now_ms;
    e.kind = kind;
    e.dev = current_;
    e.type = type;
    e.seq = seq;
    e.len = len;
    e.value = value;
    if (data != nullptr && len > 0) e.data.assign(data, data + len);
    trace_.add(std::move(e));
}

void World::tick() {
    // Half-duplex: mientras emite, un dispositivo no ejecuta nada (en la placa,
    // transmit() bloquea durante el tiempo en el aire).
    if (sim_now_ms >= busyUntil_[kGatewayRadio]) {
        current_ = kGatewayRadio;
        sim_serial_tag = "GW";
        gateway_->process();
    }
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (sim_now_ms < busyUntil_[i + 1]) continue;
        current_ = static_cast<int>(i) + 1;
        sim_serial_tag = "NODO";
        nodes_[i]->process();
    }
    for (const Delivery& d : channel_.popDue(sim_now_ms)) deliver(d);
    observeState();
    sim_now_ms++;
}

void World::deliver(const Delivery& delivery) {
    for (size_t r = 0; r < radios_.size(); ++r) {
        const int radio = static_cast<int>(r);
        if (radio == delivery.from) continue;
        if (channel_.linkLost(delivery.from, radio)) {
            Event e;
            e.t = sim_now_ms;
            e.kind = Ev::LOST;
            e.dev = radio;
            LoRaLANFrame parsed;
            if (LoRaLANPacket::parse(delivery.bytes.data(), static_cast<uint16_t>(delivery.bytes.size()), parsed)) {
                e.type = parsed.header.type;
                e.seq = parsed.header.seq;
            }
            trace_.add(std::move(e));
            continue;
        }
        receiveAt(radio, delivery.bytes);
    }
}

void World::receiveAt(int radio, const std::vector<uint8_t>& bytes) {
    current_ = radio;
    sim_serial_tag = radio == kGatewayRadio ? "GW" : "NODO";

    Event e;
    e.t = sim_now_ms;
    e.dev = radio;
    e.len = static_cast<uint8_t>(bytes.size());

    LoRaLANFrame parsed;
    const bool wellFormed = LoRaLANPacket::parse(bytes.data(), static_cast<uint16_t>(bytes.size()), parsed);
    uint8_t myId = 0;
    bool hasSession = false;
    bool usesAppKey = false;
    if (wellFormed) {
        e.type = parsed.header.type;
        e.seq = parsed.header.seq;
        e.flags = parsed.header.flags;
        usesAppKey = parsed.header.type == static_cast<uint8_t>(MsgType::JOIN_REQUEST) ||
                     parsed.header.type == static_cast<uint8_t>(MsgType::JOIN_ACCEPT) ||
                     parsed.header.dstId == 0xFF;
        if (radio == kGatewayRadio) {
            uint8_t ignored[LORALAN_HWID_SIZE];
            myId = config_.gatewayId;
            hasSession = gateway_->nodeHwId(parsed.header.srcId, ignored);
        } else {
            const LoRaLANNode& n = *nodes_[static_cast<size_t>(radio) - 1];
            myId = n.getConfig().nodeId;
            hasSession = n.isJoined() || n.isLeaving();
        }
    }

    sim_serial_mic_error = false;
    const bool accepted = radio == kGatewayRadio
                              ? gateway_->handleReceived(bytes.data(), bytes.size())
                              : nodes_[static_cast<size_t>(radio) - 1]->handleReceived(bytes.data(), bytes.size());

    if (accepted) {
        e.kind = Ev::RX_OK;
    } else {
        e.kind = Ev::RX_DROP;
        if (!wellFormed) e.reason = DropReason::MALFORMED;
        else if (parsed.header.netId != kNetId) e.reason = DropReason::WRONG_NET;
        else if (parsed.header.dstId != myId && !usesAppKey) e.reason = DropReason::NOT_ADDRESSED;
        else if (sim_serial_mic_error) e.reason = DropReason::BAD_MIC;
        else if (!usesAppKey && !hasSession) e.reason = DropReason::NO_KEY;
        else e.reason = DropReason::REJECTED_BY_ROLE;
        if (!config_.traceDrops) return;
    }
    trace_.add(std::move(e));
}

void World::traceState(int dev, const char* name, int value) {
    Event e;
    e.t = sim_now_ms;
    e.kind = Ev::STATE;
    e.dev = dev;
    e.value = value;
    e.detail = name;
    trace_.add(std::move(e));
}

void World::observeState() {
    const int gwNodes = gateway_->nodeCount();
    if (gwNodes != state_[0].gwNodes) {
        traceState(kGatewayRadio, "nodos_activos", gwNodes);
        state_[0].gwNodes = gwNodes;
    }
    for (size_t i = 0; i < nodes_.size(); ++i) {
        const LoRaLANNode& n = *nodes_[i];
        ObservedState& s = state_[i + 1];
        const int dev = static_cast<int>(i) + 1;
        if (n.isJoined() != s.joined) {
            traceState(dev, "asociado", n.isJoined());
            s.joined = n.isJoined();
        }
        if (n.isLeaving() != s.leaving) {
            traceState(dev, "dando_de_baja", n.isLeaving());
            s.leaving = n.isLeaving();
        }
        if (n.isPollPayloadPending() != s.pollPending) {
            traceState(dev, "carga_pendiente", n.isPollPayloadPending());
            s.pollPending = n.isPollPayloadPending();
        }
    }
}

bool World::runUntil(const std::function<bool()>& pred, uint32_t maxMs) {
    const uint32_t deadline = sim_now_ms + maxMs;
    while (sim_now_ms < deadline) {
        if (pred()) return true;
        tick();
    }
    return pred();
}

void World::runFor(uint32_t ms) {
    const uint32_t deadline = sim_now_ms + ms;
    while (sim_now_ms < deadline) tick();
}

bool World::joinAll(uint32_t maxMs) {
    const bool joined = runUntil(
        [this] {
            for (const auto& n : nodes_) {
                if (!n->isJoined()) return false;
            }
            return channel_.airEmpty();
        },
        maxMs);
    if (!joined) ctx_.addNotProduced("precondicion: no se asociaron todos los nodos");
    return joined;
}

void World::attackerSend(const std::vector<uint8_t>& frame) {
    channel_.transmit(kAttackerRadio, frame.data(), frame.size(), sim_now_ms);
}

std::vector<uint8_t> World::captured(MsgType type) const {
    const auto it = capturedByType_.find(static_cast<uint8_t>(type));
    return it == capturedByType_.end() ? std::vector<uint8_t>{} : it->second;
}
