// Familia SILENCIO: recuperacion ante la perdida del gateway (PRO-9).

#include "common.h"

CASE("S1", "SILENCIO", "El gateway deja de emitir", "2026-11-01", "Silencio de 5 s y sin sondeo", "Ninguno") {
    WorldConfig cfg;
    cfg.silenceTimeoutMs = 5000;
    World w(ctx, 1, cfg);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.runFor(8000);
    PROP("PRO-9", "El nodo repite el JOIN sin reiniciarse");
    EXPECT_TRUE(t.count(Ev::TX, MsgType::JOIN_REQUEST) >= 1, "JOIN_REQUEST emitidos");
    EXPECT_TRUE(w.node(0).isJoined(), "asociado");
}

CASE("S2", "SILENCIO", "Funcionamiento normal prolongado", "2026-11-01", "Silencio de 8 s; sondeo cada 2 s", "Ninguno") {
    WorldConfig cfg;
    cfg.silenceTimeoutMs = 8000;
    cfg.pollIntervalMs = 2000;
    World w(ctx, 1, cfg);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.runFor(120000);
    PROP("PRO-9", "Ninguna reasociacion falsa");
    EXPECT_EQ(t.count(Ev::TX, MsgType::JOIN_REQUEST), 0, "JOIN_REQUEST emitidos");
    EXPECT_TRUE(w.node(0).isJoined(), "asociado");
}

CASE("S4", "SILENCIO", "Reinicio del gateway con tres nodos", "2026-11-01",
     "Tres nodos, dos con direccion dinamica; silencio de 8 s; sondeo cada 2 s", "Reinicio del gateway") {
    WorldConfig cfg;
    cfg.silenceTimeoutMs = 8000;
    cfg.pollIntervalMs = 2000;
    cfg.tweakNode = [](int i, LoRaLANNode::Config& c) { c.nodeId = i == 2 ? 0 : 0x20; };
    World w(ctx, 3, cfg);
    w.joinAll();
    const uint8_t ids[3] = {w.nodeId(0), w.nodeId(1), w.nodeId(2)};
    Trace& t = w.trace();
    t.mark();
    w.rebootGateway();
    w.runFor(40000);
    PROP("PRO-9", "Los tres se reasocian");
    EXPECT_EQ(t.count(Ev::APP_NODE_JOINED), 3, "avisos de asociacion");
    EXPECT_EQ(w.gw().nodeCount(), 3, "nodos en la tabla");
    PROP("PRO-9", "Cada uno recupera su direccion");
    for (int i = 0; i < 3; ++i) EXPECT_EQ(w.nodeId(i), ids[i], "direccion");
}
