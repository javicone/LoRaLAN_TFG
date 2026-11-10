// Familia POLL. Lo esperado sale de RF1, RF3 y RF5 (capitulo 3).

#include "common.h"

namespace {
bool ackFlag(const Event* e) {
    return e != nullptr && (e->flags & static_cast<uint8_t>(Flags::ACK_REQUIRED)) != 0;
}
bool onDemand(uint8_t* payload, uint8_t& len) {
    std::memcpy(payload, kEvent, kEventLen);
    len = kEventLen;
    return true;
}
}  // namespace

CASE("P1", "POLL", "POLL sin datos", "2026-11-01", "Un nodo asociado sin nada que enviar", "POLL") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.gw().sendPoll(w.nodeId(0));
    w.runFor(3000);
    PROP("PRO-2", "Respuesta vacia, una vez y sin ACK");
    EXPECT_EQ(t.count(Ev::TX, MsgType::POLL_RESPONSE), 1, "POLL_RESPONSE emitidas");
    EXPECT_TRUE(!ackFlag(t.first(Ev::TX, MsgType::POLL_RESPONSE)), "no pide ACK");
    EXPECT_EQ(t.count(Ev::TX, MsgType::ACK), 0, "ACK emitidos por cualquiera");
    const Event* tx = t.first(Ev::TX, MsgType::POLL_RESPONSE);
    EXPECT_TRUE(tx != nullptr && tx->len == 11, "trama sin carga");
}

CASE("P2", "POLL", "Respuesta bajo demanda", "2026-11-01", "Nodo con onPollRequest", "POLL") {
    World w(ctx);
    w.joinAll();
    w.node(0).setOnPollRequest(onDemand);
    Trace& t = w.trace();
    t.mark();
    w.gw().sendPoll(w.nodeId(0));
    w.runFor(3000);
    PROP("PRO-2", "Una emision con los datos, sin ACK");
    EXPECT_EQ(t.count(Ev::TX, MsgType::POLL_RESPONSE), 1, "POLL_RESPONSE emitidas");
    EXPECT_TRUE(!ackFlag(t.first(Ev::TX, MsgType::POLL_RESPONSE)), "no pide ACK");
    EXPECT_EQ(t.count(Ev::TX, MsgType::ACK), 0, "ACK emitidos");
    EXPECT_EQ(countWithData(t, Ev::APP_POLL_RESP, kEvent, kEventLen), 1, "entregas con los datos");
}

CASE("P3", "POLL", "Carga con ACK", "2026-11-01", "Carga encolada con ACK", "POLL") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.node(0).queuePollPayload(kEvent, kEventLen, true);
    w.gw().sendPoll(w.nodeId(0));
    w.runFor(3000);
    PROP("PRO-2", "Se entrega, se confirma y se borra del nodo");
    EXPECT_EQ(t.count(Ev::TX, MsgType::POLL_RESPONSE), 1, "POLL_RESPONSE emitidas");
    EXPECT_EQ(t.count(Ev::TX, MsgType::ACK, kGatewayRadio), 1, "ACK del gateway");
    EXPECT_EQ(countWithData(t, Ev::APP_POLL_RESP, kEvent, kEventLen), 1, "entregas");
    EXPECT_TRUE(!w.node(0).isPollPayloadPending(), "carga borrada");
}

CASE("P4", "POLL", "Carga con ACK perdido", "2026-11-01", "Carga encolada con ACK", "Se descartan los 3 primeros ACK") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.inject(Drop(MsgType::ACK, Dir::DOWN, 1));
    w.inject(Drop(MsgType::ACK, Dir::DOWN, 2));
    w.inject(Drop(MsgType::ACK, Dir::DOWN, 3));
    w.node(0).queuePollPayload(kEvent, kEventLen, true);
    w.gw().sendPoll(w.nodeId(0));
    w.runFor(kExhaustMs + 200);
    PROP("PRO-5", "Sin ACK, la carga se conserva y los reintentos no se entregan dos veces");
    EXPECT_EQ(t.count(Ev::TX, MsgType::POLL_RESPONSE), 3, "emisiones");
    EXPECT_TRUE(w.node(0).isPollPayloadPending(), "carga conservada");
    EXPECT_EQ(t.count(Ev::APP_POLL_RESP), 1, "entregas");
    w.gw().sendPoll(w.nodeId(0));
    w.runFor(1500);
    PROP("PRO-5", "Se repite en el siguiente sondeo y entonces se borra");
    EXPECT_EQ(t.count(Ev::TX, MsgType::POLL_RESPONSE), 4, "emisiones");
    EXPECT_TRUE(!w.node(0).isPollPayloadPending(), "carga borrada");
}

CASE("P5", "POLL", "Carga sin ACK", "2026-11-01", "Carga encolada sin ACK", "POLL") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.node(0).queuePollPayload(kEvent, kEventLen, false);
    w.gw().sendPoll(w.nodeId(0));
    w.runFor(3000);
    PROP("PRO-2", "Una emision y se borra");
    EXPECT_EQ(t.count(Ev::TX, MsgType::POLL_RESPONSE), 1, "emisiones");
    EXPECT_EQ(t.count(Ev::TX, MsgType::ACK), 0, "ACK");
    EXPECT_EQ(countWithData(t, Ev::APP_POLL_RESP, kEvent, kEventLen), 1, "entregas");
    EXPECT_TRUE(!w.node(0).isPollPayloadPending(), "carga borrada");
}

CASE("P6", "POLL", "Carga sobre carga pendiente", "2026-11-01", "Carga A pendiente", "Encolar carga B") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.node(0).queuePollPayload(kEvent, kEventLen, true);
    const bool second = w.node(0).queuePollPayload(kOther, kOtherLen, true);
    w.gw().sendPoll(w.nodeId(0));
    w.runFor(1500);
    PROP("PRO-5", "queuePollPayload devuelve false y no sobrescribe");
    EXPECT_TRUE(!second, "la segunda carga debe rechazarse");
    EXPECT_EQ(countWithData(t, Ev::APP_POLL_RESP, kEvent, kEventLen), 1, "llega la primera carga");
}

CASE("P7", "POLL", "POLL dirigido a otro nodo", "2026-11-01", "Dos nodos asociados", "POLL al nodo 1") {
    World w(ctx, 2);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.gw().sendPoll(w.nodeId(1));
    w.runFor(1000);
    PROP("PRO-2", "Solo responde el destinatario");
    EXPECT_EQ(t.count(Ev::TX, MsgType::POLL_RESPONSE, World::dev(0)), 0, "respuestas del otro nodo");
    EXPECT_EQ(t.count(Ev::TX, MsgType::POLL_RESPONSE, World::dev(1)), 1, "respuestas del destinatario");
}

CASE("P8", "POLL", "POLL perdido", "2026-11-01", "Un nodo con sondeo cada 1 s", "Se descarta el 1.er POLL") {
    WorldConfig cfg;
    cfg.pollIntervalMs = 1000;
    World w(ctx, 1, cfg);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.inject(Drop(MsgType::POLL, Dir::DOWN, 1));
    w.runFor(3500);
    const size_t polls = t.count(Ev::TX, MsgType::POLL);
    PROP("PRO-2", "Llega el siguiente turno y un solo POLL perdido no lo marca inactivo");
    EXPECT_TRUE(polls >= 3, "sigue sondeando");
    EXPECT_EQ(t.count(Ev::TX, MsgType::POLL_RESPONSE), polls - 1, "respuestas");
    EXPECT_EQ(t.count(Ev::APP_NODE_INACTIVE), 0, "avisos de inactivo");
}

CASE("P9", "POLL", "POLL_RESPONSE perdida", "2026-11-01", "Un nodo con carga", "Se descarta la 1.a respuesta") {
    {
        World w(ctx);
        w.joinAll();
        Trace& t = w.trace();
        t.mark();
        w.inject(Drop(MsgType::POLL_RESPONSE, Dir::UP, 1));
        w.node(0).queuePollPayload(kEvent, kEventLen, false);
        w.gw().sendPoll(w.nodeId(0));
        w.runFor(3000);
        PROP("PRO-5", "Sin ACK, el dato se pierde");
        EXPECT_EQ(t.count(Ev::TX, MsgType::POLL_RESPONSE), 1, "emisiones");
        EXPECT_EQ(t.count(Ev::APP_POLL_RESP), 0, "entregas");
        EXPECT_TRUE(!w.node(0).isPollPayloadPending(), "carga borrada");
    }
    {
        World w(ctx);
        w.joinAll();
        Trace& t = w.trace();
        t.mark();
        w.inject(Drop(MsgType::POLL_RESPONSE, Dir::UP, 1));
        w.node(0).queuePollPayload(kEvent, kEventLen, true);
        w.gw().sendPoll(w.nodeId(0));
        w.runFor(3000);
        PROP("PRO-5", "Con ACK, se repite y llega una vez");
        EXPECT_EQ(t.count(Ev::TX, MsgType::POLL_RESPONSE), 2, "emisiones");
        EXPECT_EQ(t.retries(MsgType::POLL_RESPONSE), 1, "reintentos");
        EXPECT_EQ(countWithData(t, Ev::APP_POLL_RESP, kEvent, kEventLen), 1, "entregas");
        EXPECT_TRUE(!w.node(0).isPollPayloadPending(), "carga borrada");
    }
}

CASE("P10", "POLL", "Reparto con varios nodos", "2026-11-01", "Cuatro nodos con sondeo cada 1 s", "Ninguno") {
    WorldConfig cfg;
    cfg.pollIntervalMs = 1000;
    World w(ctx, 4, cfg);
    w.joinAll();
    w.runFor(2000);
    Trace& t = w.trace();
    t.mark();
    w.runFor(24000);
    size_t minPolls = 1000, maxPolls = 0;
    bool regular = true;
    for (int i = 0; i < 4; ++i) {
        const auto polls = t.select(Ev::RX_OK, MsgType::POLL, World::dev(i));
        minPolls = polls.size() < minPolls ? polls.size() : minPolls;
        maxPolls = polls.size() > maxPolls ? polls.size() : maxPolls;
        for (size_t k = 1; k < polls.size(); ++k) {
            const uint32_t gap = polls[k]->t - polls[k - 1]->t;
            if (gap < 3990 || gap > 4010) regular = false;
        }
    }
    PROP("PRO-1", "Round-robin justo: el turno de cada nodo llega cada N intervalos");
    EXPECT_TRUE(minPolls >= 5, "todos reciben turnos");
    EXPECT_TRUE(maxPolls - minPolls <= 1, "reparto justo");
    EXPECT_TRUE(regular, "separacion de 4000 ms entre turnos");

    // N que no divide a 256: el turno debe seguir siendo regular al pasar de 256 sondeos.
    WorldConfig cfg3;
    cfg3.pollIntervalMs = 200;
    World w3(ctx, 3, cfg3);
    w3.joinAll();
    Trace& t3 = w3.trace();
    t3.mark();
    w3.runFor(70000);
    bool regular3 = true;
    size_t min3 = 100000, max3 = 0;
    for (int i = 0; i < 3; ++i) {
        const auto polls = t3.select(Ev::RX_OK, MsgType::POLL, World::dev(i));
        min3 = polls.size() < min3 ? polls.size() : min3;
        max3 = polls.size() > max3 ? polls.size() : max3;
        for (size_t k = 1; k < polls.size(); ++k) {
            const uint32_t gap = polls[k]->t - polls[k - 1]->t;
            if (gap < 590 || gap > 610) regular3 = false;
        }
    }
    PROP("PRO-1", "Con 3 nodos, el reparto sigue siendo justo tras mas de 256 sondeos");
    EXPECT_TRUE(min3 >= 100, "mas de 256 sondeos en total");
    EXPECT_TRUE(max3 - min3 <= 1, "reparto justo");
    EXPECT_TRUE(regular3, "separacion de 600 ms entre turnos");
}

CASE("P11", "POLL", "Nodo que deja de contestar y vuelve", "2026-11-01", "Un nodo con sondeo cada 1 s",
     "Enlace de subida cortado 8 s") {
    WorldConfig cfg;
    cfg.pollIntervalMs = 1000;
    World w(ctx, 1, cfg);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.channel().setLoss(World::dev(0), kGatewayRadio, 100);
    w.runFor(8000);
    const size_t pollsCut = t.count(Ev::TX, MsgType::POLL);
    PROP("PRO-2", "El gateway lo declara inactivo una vez y sigue sondeandolo");
    EXPECT_EQ(t.count(Ev::APP_NODE_INACTIVE), 1, "avisos de inactivo");
    EXPECT_TRUE(pollsCut >= 7, "sigue sondeando");
    w.channel().setLoss(World::dev(0), kGatewayRadio, 0);
    t.mark();
    w.runFor(6000);
    PROP("PRO-2", "Al volver a contestar se reactiva, sin nuevos avisos");
    EXPECT_TRUE(t.count(Ev::APP_POLL_RESP) >= 3, "respuestas recibidas");
    EXPECT_EQ(t.count(Ev::APP_NODE_INACTIVE), 0, "nuevos avisos de inactivo");
    EXPECT_TRUE(w.gw().isAlive(w.nodeId(0), 2000), "vivo para el gateway");
}

CASE("P14", "POLL", "POLL mientras el nodo espera un ACK", "2026-11-01", "Evento con el 1.er ACK perdido",
     "POLL justo antes del reintento") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.inject(Drop(MsgType::ACK, Dir::DOWN, 1));
    w.node(0).queuePushEvent(kEvent, kEventLen, true);
    w.runFor(1150);
    w.gw().sendPoll(w.nodeId(0));
    w.runFor(3000);
    PROP("PRO-5", "La respuesta al POLL no impide recibir el ACK esperado");
    EXPECT_EQ(countOutcome(t, 1), 1, "evento confirmado");
    EXPECT_EQ(countOutcome(t, 0), 0, "evento dado por perdido");
    EXPECT_EQ(t.count(Ev::TX, MsgType::PUSH_EVENT), 2, "emisiones del evento");
    EXPECT_EQ(t.count(Ev::COLLISION), 0, "colisiones");
}
