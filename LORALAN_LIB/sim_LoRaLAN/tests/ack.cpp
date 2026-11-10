// Familia ACK diferido. Lo esperado sale de RF6 (capitulo 3).

#include "common.h"

namespace {

const uint8_t kCode[] = {0x07};

WorldConfig deferred() {
    WorldConfig cfg;
    cfg.deferredPushAck = true;
    return cfg;
}

// Lleva el evento hasta la aplicacion del gateway; devuelve su secuencia.
uint16_t pushUntilApp(World& w) {
    Trace& t = w.trace();
    w.node(0).queuePushEvent(kEvent, kEventLen, true);
    w.runUntil([&] { return t.count(Ev::APP_PUSH) == 1; }, 500);
    const Event* app = t.first(Ev::APP_PUSH);
    return app != nullptr ? app->seq : 0;
}

}  // namespace

CASE("D1", "ACK", "La aplicacion responde rapido", "2026-11-01", "Gateway con ACK diferido", "Respuesta a los 100 ms") {
    World w(ctx, 1, deferred());
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    const uint16_t seq = pushUntilApp(w);
    w.runFor(100);
    const bool ok = w.gw().ackPush(w.nodeId(0), seq, kCode, sizeof(kCode));
    w.runFor(1500);
    PROP("PRO-3", "El codigo llega al nodo dentro del ACK, sin retransmisiones");
    EXPECT_TRUE(ok, "ackPush aceptado");
    EXPECT_EQ(t.count(Ev::TX, MsgType::PUSH_EVENT), 1, "emisiones");
    EXPECT_EQ(countOutcome(t, 1), 1, "confirmado");
    EXPECT_TRUE(sameData(t.first(Ev::APP_PUSH_ACK), kCode, sizeof(kCode)), "codigo recibido");
}

CASE("D2", "ACK", "La aplicacion responde tarde", "2026-11-01", "Gateway con ACK diferido", "Respuesta a los 2000 ms") {
    World w(ctx, 1, deferred());
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    const uint16_t seq = pushUntilApp(w);
    w.runFor(2000);
    const size_t ackBefore = t.count(Ev::TX, MsgType::ACK, kGatewayRadio);
    const bool ok = w.gw().ackPush(w.nodeId(0), seq, kCode, sizeof(kCode));
    w.runFor(2000);
    PROP("PRO-3", "El nodo repite durante la espera, pero la aplicacion no recibe duplicado");
    EXPECT_EQ(t.count(Ev::TX, MsgType::PUSH_EVENT), 2, "emisiones");
    EXPECT_EQ(t.count(Ev::APP_PUSH), 1, "entregas");
    EXPECT_EQ(ackBefore, 0, "ACK antes de responder la aplicacion");
    PROP("PRO-3", "La respuesta llega al nodo");
    EXPECT_TRUE(ok, "ackPush aceptado");
    EXPECT_EQ(countOutcome(t, 1), 1, "confirmado");
    EXPECT_TRUE(sameData(t.first(Ev::APP_PUSH_ACK), kCode, sizeof(kCode)), "codigo recibido");
}

CASE("D3", "ACK", "ACK con la respuesta perdido", "2026-11-01", "Gateway con ACK diferido", "Se descarta el 1.er ACK de bajada") {
    World w(ctx, 1, deferred());
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.inject(Drop(MsgType::ACK, Dir::DOWN, 1));
    const uint16_t seq = pushUntilApp(w);
    w.runFor(100);
    w.gw().ackPush(w.nodeId(0), seq, kCode, sizeof(kCode));
    w.runFor(3000);
    PROP("PRO-3", "La retransmision recibe la misma respuesta guardada, sin duplicado");
    EXPECT_EQ(t.count(Ev::TX, MsgType::PUSH_EVENT), 2, "emisiones");
    EXPECT_EQ(t.count(Ev::TX, MsgType::ACK, kGatewayRadio), 2, "ACK del gateway");
    EXPECT_EQ(t.count(Ev::APP_PUSH), 1, "entregas");
    EXPECT_EQ(countOutcome(t, 1), 1, "confirmado");
    EXPECT_TRUE(sameData(t.first(Ev::APP_PUSH_ACK), kCode, sizeof(kCode)), "codigo recibido");
}

CASE("D4", "ACK", "La aplicacion nunca responde", "2026-11-01", "Gateway con ACK diferido", "Sin ackPush") {
    World w(ctx, 1, deferred());
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    pushUntilApp(w);
    w.runFor(kExhaustMs + 500);
    PROP("PRO-3", "Agotados los intentos, el nodo recibe delivered=false");
    EXPECT_EQ(t.count(Ev::TX, MsgType::PUSH_EVENT), 3, "emisiones");
    EXPECT_EQ(t.count(Ev::TX, MsgType::ACK, kGatewayRadio), 0, "ACK");
    EXPECT_EQ(t.count(Ev::APP_PUSH), 1, "entregas");
    EXPECT_EQ(countOutcome(t, 0), 1, "avisos de no entregado");
}

CASE("D6", "ACK", "POLL durante la espera", "2026-11-01", "Gateway con ACK diferido",
     "La aplicacion responde mientras el nodo contesta a un POLL") {
    World w(ctx, 1, deferred());
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    const uint16_t seq = pushUntilApp(w);
    w.gw().sendPoll(w.nodeId(0));
    w.runUntil([&] { return t.count(Ev::TX, MsgType::POLL_RESPONSE) == 1; }, 500);
    w.gw().ackPush(w.nodeId(0), seq, kCode, sizeof(kCode));
    w.runFor(kExhaustMs);
    PROP("PRO-3", "El ACK no se pierde por la respuesta al POLL");
    EXPECT_EQ(t.count(Ev::COLLISION, MsgType::ACK), 0, "ACK destruidos");
    EXPECT_EQ(t.count(Ev::TX, MsgType::PUSH_EVENT), 1, "emisiones del evento");
    EXPECT_EQ(countOutcome(t, 1), 1, "confirmado");
    EXPECT_TRUE(sameData(t.first(Ev::APP_PUSH_ACK), kCode, sizeof(kCode)), "codigo recibido");
}
