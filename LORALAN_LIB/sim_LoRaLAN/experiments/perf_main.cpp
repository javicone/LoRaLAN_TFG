// Experimentos de rendimiento. Cada ejecucion es una replica: una configuracion y
// una semilla, con la biblioteca real, y una linea JSON con sus metricas.
//   sim_perf --exp EXP5 --nodes 8 --rate 60 --poll 5000 --blind 0 --seed 1
// Los barridos y la estadistica entre replicas estan en sweep.py.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "airtime.h"
#include "world.h"

namespace {

using Args = std::map<std::string, std::string>;

long argL(const Args& a, const char* key, long fallback) {
    const auto it = a.find(key);
    return it == a.end() ? fallback : std::strtol(it->second.c_str(), nullptr, 10);
}

std::string jsonOut;
void metric(const char* key, double value) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), ",\"%s\":%.6g", key, value);
    jsonOut += buf;
}

double percentile(std::vector<double> v, double q) {
    if (v.empty()) return -1;
    std::sort(v.begin(), v.end());
    const size_t rank = static_cast<size_t>(std::ceil(q * static_cast<double>(v.size())));
    return v[rank == 0 ? 0 : rank - 1];
}

void latencyMetrics(const std::vector<double>& lat) {
    metric("lat_p50", percentile(lat, 0.50));
    metric("lat_p95", percentile(lat, 0.95));
    metric("lat_p99", percentile(lat, 0.99));
    metric("lat_max", percentile(lat, 1.00));
}

// Ciclo de trabajo sobre lo realmente emitido desde la marca, con el tiempo en el aire
// exacto: el del gateway y el del nodo que mas emite.
void dutyMetrics(const Trace& t, int nodes, double windowMs) {
    radio::LoRaParams params;
    double gateway = 0, worstNode = 0;
    for (const Event* e : t.select(Ev::TX, kGatewayRadio)) gateway += radio::timeOnAirMs(e->len, params);
    for (int i = 0; i < nodes; ++i) {
        double air = 0;
        for (const Event* e : t.select(Ev::TX, World::dev(i))) air += radio::timeOnAirMs(e->len, params);
        worstNode = std::max(worstNode, air);
    }
    metric("duty_gateway_pct", 100.0 * gateway / windowMs);
    metric("duty_worst_node_pct", 100.0 * worstNode / windowMs);
}

// Mas de 32 nodos: el gateway en la direccion 0 deja libres las 254 direcciones.
void configureLargeNetwork(WorldConfig& cfg, int nodes) {
    if (nodes <= 32) return;
    cfg.gatewayId = 0;
    cfg.traceDrops = false;
    cfg.tweakNode = [](int i, LoRaLANNode::Config& c) { c.nodeId = static_cast<uint8_t>(i + 1); };
}

// Lectura de sensores de 10 B, como en el prototipo: la POLL_RESPONSE ocupa 21 B.
bool sensorReading(uint8_t* payload, uint8_t& len) {
    std::memset(payload, 0x33, 10);
    len = 10;
    return true;
}

// Fichaje de 6 B (trama de 17 B): los 4 primeros identifican el evento.
void encodeId(uint32_t id, uint8_t* out) {
    std::memcpy(out, &id, 4);
    out[4] = 0;
    out[5] = 0;
}

uint32_t decodeId(const Event* e) {
    uint32_t id = 0;
    if (e->data.size() >= 4) std::memcpy(&id, e->data.data(), 4);
    return id;
}

// Llegadas de Poisson, con un generador propio: el trafico no altera los sorteos
// de la biblioteca ni los del canal.
class Arrivals {
public:
    Arrivals(uint32_t seed, double perMinute) : rng_(seed ^ 0xC0FFEEu), lambdaPerMs_(perMinute / 60000.0) {}
    uint32_t nextGap() {
        const double u = (static_cast<double>(rng_()) + 1.0) / 4294967297.0;
        return static_cast<uint32_t>(std::ceil(-std::log(u) / lambdaPerMs_));
    }
    int node(int nodes) { return static_cast<int>(rng_() % static_cast<uint32_t>(nodes)); }

private:
    std::mt19937 rng_;
    double lambdaPerMs_;
};

struct Pending {
    uint32_t id;
};

// Trafico de fichajes con cola de aplicacion en cada nodo. La latencia va desde que
// se produce el fichaje hasta que llega a la aplicacion del gateway.
class Traffic {
public:
    Traffic(World& w, int nodes, uint32_t seed, double perMinute)
        : w_(w), nodes_(nodes), arrivals_(seed, perMinute), queues_(static_cast<size_t>(nodes)),
          sent_(static_cast<size_t>(nodes)) {
        next_ = w_.now() + arrivals_.nextGap();
    }

    void step(bool generate) {
        const uint32_t now = w_.now();
        if (generate && now >= next_) {
            const int n = arrivals_.node(nodes_);
            const uint32_t id = static_cast<uint32_t>(arrival_.size()) + 1;
            arrival_.push_back(now);
            queues_[static_cast<size_t>(n)].push_back({id});
            next_ = now + arrivals_.nextGap();
        }
        for (int n = 0; n < nodes_; ++n) {
            std::deque<Pending>& q = queues_[static_cast<size_t>(n)];
            if (q.empty()) continue;
            uint8_t data[6];
            encodeId(q.front().id, data);
            if (w_.node(n).queuePushEvent(data, sizeof(data), true)) {
                sent_[static_cast<size_t>(n)].push_back(q.front().id);
                q.pop_front();
            }
        }
        w_.runFor(1);
    }

    // Eventos de cada nodo en orden de envio: sus desenlaces llegan en ese mismo orden.
    const std::vector<uint32_t>& sentBy(int node) const { return sent_[static_cast<size_t>(node)]; }
    uint32_t offered() const { return static_cast<uint32_t>(arrival_.size()); }
    uint32_t arrivalOf(uint32_t id) const { return arrival_[id - 1]; }
    size_t backlog() const {
        size_t n = 0;
        for (const auto& q : queues_) n += q.size();
        return n;
    }

private:
    World& w_;
    int nodes_;
    Arrivals arrivals_;
    std::vector<std::deque<Pending>> queues_;
    std::vector<std::vector<uint32_t>> sent_;
    std::vector<uint32_t> arrival_;
    uint32_t next_ = 0;
};

// VAL: un nodo, un evento cada 6 s y perdida solo en la subida. Comparable con 1 - p^3.
void runVal(const Args& a, Ctx& ctx) {
    WorldConfig cfg;
    World w(ctx, 1, cfg);
    if (!w.joinAll()) { metric("join_fail", 1); return; }
    w.channel().setLoss(World::dev(0), kGatewayRadio, static_cast<int>(argL(a, "loss", 0)));
    const long trials = argL(a, "trials", 200);
    Trace& t = w.trace();
    const uint8_t data[6] = {1, 2, 3, 4, 5, 6};
    long delivered = 0, txDelivered = 0, txTotal = 0;
    for (long i = 0; i < trials; ++i) {
        t.mark();
        w.node(0).queuePushEvent(data, sizeof(data), true);
        w.runFor(6000);
        const long tx = static_cast<long>(t.count(Ev::TX, MsgType::PUSH_EVENT));
        txTotal += tx;
        if (t.count(Ev::APP_PUSH) > 0) {
            ++delivered;
            txDelivered += tx;
        }
    }
    metric("offered", static_cast<double>(trials));
    metric("delivered", static_cast<double>(delivered));
    metric("tx_delivered", static_cast<double>(txDelivered));
    metric("tx_total", static_cast<double>(txTotal));
}

// EXP1, eventos: un nodo, sin sondeo, ensayos separados 6 s, perdida en ambos sentidos.
void runExp1Events(const Args& a, Ctx& ctx) {
    World w(ctx, 1);
    if (!w.joinAll()) { metric("join_fail", 1); return; }
    w.channel().setDefaultLoss(static_cast<int>(argL(a, "loss", 0)));
    const long trials = argL(a, "trials", 200);
    Trace& t = w.trace();
    const uint8_t data[6] = {1, 2, 3, 4, 5, 6};
    long delivered = 0, confirmed = 0, unconfirmedButDelivered = 0, txTotal = 0, duplicates = 0;
    std::vector<double> lat;
    for (long i = 0; i < trials; ++i) {
        t.mark();
        const uint32_t start = w.now();
        w.node(0).queuePushEvent(data, sizeof(data), true);
        w.runFor(6000);
        const auto apps = t.select(Ev::APP_PUSH);
        txTotal += static_cast<long>(t.count(Ev::TX, MsgType::PUSH_EVENT));
        const Event* outcome = t.first(Ev::APP_PUSH_ACK);
        const bool acked = outcome != nullptr && outcome->value == 1;
        if (acked) ++confirmed;
        if (!apps.empty()) {
            ++delivered;
            lat.push_back(static_cast<double>(apps[0]->t - start));
            if (!acked) ++unconfirmedButDelivered;
            if (apps.size() > 1) ++duplicates;
        }
    }
    metric("offered", static_cast<double>(trials));
    metric("delivered", static_cast<double>(delivered));
    metric("confirmed", static_cast<double>(confirmed));
    metric("delivered_not_confirmed", static_cast<double>(unconfirmedButDelivered));
    metric("duplicates", static_cast<double>(duplicates));
    metric("tx_total", static_cast<double>(txTotal));
    latencyMetrics(lat);
}

// EXP1, sondeo: cuatro nodos respondiendo con sensores bajo perdida en ambos sentidos.
void runExp1Poll(const Args& a, Ctx& ctx) {
    WorldConfig cfg;
    cfg.pollIntervalMs = static_cast<uint32_t>(argL(a, "poll", 5000));
    const int nodes = static_cast<int>(argL(a, "nodes", 4));
    World w(ctx, nodes, cfg);
    if (!w.joinAll()) { metric("join_fail", 1); return; }
    for (int i = 0; i < nodes; ++i) w.node(i).setOnPollRequest(sensorReading);
    w.runFor(static_cast<uint32_t>(argL(a, "warmup", 30000)));
    w.channel().setDefaultLoss(static_cast<int>(argL(a, "loss", 0)));
    Trace& t = w.trace();
    t.mark();
    const long duration = argL(a, "duration", 1800000);
    w.runFor(static_cast<uint32_t>(duration));
    metric("polls", static_cast<double>(t.count(Ev::TX, MsgType::POLL)));
    metric("responses", static_cast<double>(t.count(Ev::APP_POLL_RESP)));
    metric("false_inactive", static_cast<double>(t.count(Ev::APP_NODE_INACTIVE)));
    metric("node_hours", static_cast<double>(nodes) * static_cast<double>(duration) / 3600000.0);
    dutyMetrics(t, nodes, static_cast<double>(duration));
}

// EXP4 (sin sondeo) y EXP5 (con sondeo): fichajes de Poisson en toda la red.
void runTraffic(const Args& a, Ctx& ctx) {
    WorldConfig cfg;
    cfg.pollIntervalMs = static_cast<uint32_t>(argL(a, "poll", 0));
    cfg.timeSyncIntervalMs = cfg.pollIntervalMs != 0 ? 30000 : 0;
    cfg.channel.cadBlindMs = static_cast<uint32_t>(argL(a, "blind", 0));
    cfg.silenceTimeoutMs = static_cast<uint32_t>(argL(a, "silence", 0));
    const int nodes = static_cast<int>(argL(a, "nodes", 8));
    const uint32_t seed = static_cast<uint32_t>(argL(a, "seed", 1));
    configureLargeNetwork(cfg, nodes);
    World w(ctx, nodes, cfg);
    if (!w.joinAll(600000)) { metric("join_fail", 1); return; }
    metric("join_all_ms", w.now());
    for (int i = 0; i < nodes; ++i) w.node(i).setOnPollRequest(sensorReading);
    w.runFor(static_cast<uint32_t>(argL(a, "warmup", 30000)));
    Trace& t = w.trace();
    t.mark();

    w.channel().setDefaultLoss(static_cast<int>(argL(a, "loss", 0)));
    Traffic traffic(w, nodes, seed, static_cast<double>(argL(a, "rate", 6)));
    const long duration = argL(a, "duration", 1800000);
    for (long i = 0; i < duration; ++i) traffic.step(true);
    for (long i = 0; i < 20000; ++i) traffic.step(false);   // se dejan terminar los reintentos
    dutyMetrics(t, nodes, static_cast<double>(duration + 20000));

    std::vector<int> times(traffic.offered() + 1, 0);
    std::vector<double> lat;
    for (const Event* e : t.select(Ev::APP_PUSH)) {
        const uint32_t id = decodeId(e);
        if (id == 0 || id > traffic.offered()) continue;
        if (times[id]++ == 0) lat.push_back(static_cast<double>(e->t - traffic.arrivalOf(id)));
    }
    long delivered = 0, duplicates = 0, delayed = 0;
    for (uint32_t id = 1; id <= traffic.offered(); ++id) {
        if (times[id] > 0) ++delivered;
        if (times[id] > 1) ++duplicates;
    }
    for (double l : lat) {
        if (l > 60) ++delayed;   // mas que su tiempo en el aire (52 ms) y un margen
    }
    const double tx = static_cast<double>(t.count(Ev::TX, MsgType::PUSH_EVENT));
    metric("offered", traffic.offered());
    metric("delivered", static_cast<double>(delivered));
    long confirmed = 0;
    for (const Event* e : t.select(Ev::APP_PUSH_ACK)) {
        if (e->value == 1) ++confirmed;
    }
    metric("confirmed", static_cast<double>(confirmed));
    // Confirmado al nodo sin haber llegado a la aplicacion: una perdida silenciosa (U22).
    long falseConfirmed = 0, notConfirmedDelivered = 0;
    for (int n = 0; n < nodes; ++n) {
        const std::vector<uint32_t>& sent = traffic.sentBy(n);
        const auto outcomes = t.select(Ev::APP_PUSH_ACK, World::dev(n));
        for (size_t k = 0; k < outcomes.size() && k < sent.size(); ++k) {
            if (outcomes[k]->value == 1 && times[sent[k]] == 0) ++falseConfirmed;
            if (outcomes[k]->value == 0 && times[sent[k]] > 0) ++notConfirmedDelivered;
        }
    }
    metric("false_confirmed", static_cast<double>(falseConfirmed));
    metric("delivered_not_confirmed", static_cast<double>(notConfirmedDelivered));
    metric("duplicates", static_cast<double>(duplicates));
    metric("delayed", static_cast<double>(delayed));
    metric("backlog_end", static_cast<double>(traffic.backlog()));
    metric("tx_push", tx);
    metric("collided_push", static_cast<double>(t.count(Ev::COLLISION, MsgType::PUSH_EVENT)));
    metric("collided_poll", static_cast<double>(t.count(Ev::COLLISION, MsgType::POLL)));
    metric("collided_response", static_cast<double>(t.count(Ev::COLLISION, MsgType::POLL_RESPONSE)));
    metric("collided_ack", static_cast<double>(t.count(Ev::COLLISION, MsgType::ACK)));
    metric("cad_busy", static_cast<double>(t.count(Ev::CAD_BUSY)));
    metric("join_requests", static_cast<double>(t.count(Ev::TX, MsgType::JOIN_REQUEST)));
    latencyMetrics(lat);
}

}  // namespace

int main(int argc, char** argv) {
    Args a;
    for (int i = 1; i + 1 < argc; i += 2) a[std::string(argv[i]).substr(2)] = argv[i + 1];
    const std::string exp = a.count("exp") ? a["exp"] : "";
    const uint32_t seed = static_cast<uint32_t>(argL(a, "seed", 1));
    sim_serial_quiet = true;
    Ctx ctx(seed);

    if (exp == "VAL") runVal(a, ctx);
    else if (exp == "EXP1E") runExp1Events(a, ctx);
    else if (exp == "EXP1P") runExp1Poll(a, ctx);
    else if (exp == "EXP4" || exp == "EXP5") runTraffic(a, ctx);
    else {
        std::fprintf(stderr, "Experimento desconocido: %s\n", exp.c_str());
        return 2;
    }
    std::printf("{\"exp\":\"%s\",\"seed\":%u%s}\n", exp.c_str(), seed, jsonOut.c_str());
    return 0;
}
