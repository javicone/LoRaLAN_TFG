// Familia TIME_SYNC. Lo esperado sale de RF8 (capitulo 3).

#include "common.h"

CASE("T1", "TIME_SYNC", "Llega un TIME_SYNC", "2026-11-01", "Un nodo asociado", "TIME_SYNC con 1000 s") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.gw().sendTimeSync(1000);
    w.runFor(200);
    const Event* rx = t.first(Ev::RX_OK, MsgType::TIME_SYNC);
    PROP("PRO-7", "El desfase del nodo pasa a ser la hora del gateway menos su reloj");
    EXPECT_TRUE(rx != nullptr, "TIME_SYNC recibido");
    if (rx != nullptr) EXPECT_EQ(w.node(0).timeOffsetMs(), 1000000L - static_cast<long>(rx->t), "desfase en ms");
}

CASE("T2", "TIME_SYNC", "Solo llegan TIME_SYNC", "2026-11-01", "Silencio de 10 s; TIME_SYNC cada 2 s; sin sondeo", "Ninguno") {
    WorldConfig cfg;
    cfg.timeSyncIntervalMs = 2000;
    cfg.silenceTimeoutMs = 10000;
    World w(ctx, 1, cfg);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.runFor(15000);
    PROP("PRO-7", "El nodo recibe los TIME_SYNC");
    EXPECT_TRUE(t.count(Ev::RX_OK, MsgType::TIME_SYNC, World::dev(0)) >= 5, "TIME_SYNC recibidos");
    PROP("PRO-9", "Los TIME_SYNC no cuentan como senal de vida: se detecta el silencio");
    EXPECT_TRUE(t.count(Ev::TX, MsgType::JOIN_REQUEST) >= 1, "repite el JOIN");
}

CASE("T-PER", "TIME_SYNC", "Difusion periodica", "2026-11-01", "TIME_SYNC cada 1 s; nodo sin JOIN automatico", "Ninguno") {
    WorldConfig cfg;
    cfg.timeSyncIntervalMs = 1000;
    cfg.tweakNode = [](int, LoRaLANNode::Config& c) { c.joinRetryMs = 0; };
    World w(ctx, 1, cfg);
    Trace& t = w.trace();
    w.runFor(10500);   // emisiones de 1000 a 10000 ms
    const auto tx = t.select(Ev::TX, MsgType::TIME_SYNC, kGatewayRadio);
    PROP("PRO-7", "Se emite sola, con el periodo configurado");
    EXPECT_EQ(tx.size(), 10, "emisiones en 10 s");
    bool regular = true;
    for (size_t i = 1; i < tx.size(); ++i) {
        const uint32_t gap = tx[i]->t - tx[i - 1]->t;
        if (gap < 990 || gap > 1010) regular = false;
    }
    EXPECT_TRUE(regular, "periodo de 1000 ms");
}
