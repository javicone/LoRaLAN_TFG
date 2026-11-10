// Familia LEAVE. Lo esperado sale de RF5, RF9 y RF12 (capitulo 3).

#include "common.h"

CASE("L1", "LEAVE", "Baja con canal limpio", "2026-11-01", "Un nodo asociado", "Ninguno") {
    World w(ctx);
    w.joinAll();
    const uint8_t id = w.nodeId(0);
    Trace& t = w.trace();
    t.mark();
    const bool ok = w.node(0).leave();
    w.runFor(12000);
    PROP("PRO-6", "El gateway confirma la baja una vez y avisa a su aplicacion una vez");
    EXPECT_TRUE(ok, "leave() debe aceptarse");
    EXPECT_EQ(t.count(Ev::TX, MsgType::LEAVE), 1, "LEAVE emitidos");
    EXPECT_EQ(t.count(Ev::TX, MsgType::ACK, kGatewayRadio), 1, "ACK del gateway");
    EXPECT_EQ(t.count(Ev::APP_NODE_LEFT), 1, "avisos");
    PROP("PRO-6", "Ambos extremos terminan la sesion");
    EXPECT_TRUE(!w.node(0).isJoined() && !w.node(0).isLeaving(), "nodo de baja");
    EXPECT_EQ(w.gw().nodeCount(), 0, "nodos activos");
    EXPECT_TRUE(!w.gw().sendCommand(id, kEvent, kEventLen), "el gateway ya no tiene clave para el");
    PROP("PRO-6", "El nodo no se reasocia solo");
    EXPECT_EQ(t.count(Ev::TX, MsgType::JOIN_REQUEST), 0, "JOIN_REQUEST emitidos");
}

CASE("L2", "LEAVE", "ACK de la baja perdido", "2026-11-01", "Un nodo asociado", "Se descarta el 1.er ACK de bajada") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.inject(Drop(MsgType::ACK, Dir::DOWN, 1));
    w.node(0).leave();
    w.runFor(kExhaustMs + 1000);
    PROP("PRO-6", "El nodo retransmite el LEAVE");
    EXPECT_TRUE(t.count(Ev::TX, MsgType::LEAVE) >= 2, "LEAVE emitidos");
    EXPECT_TRUE(t.count(Ev::TX, MsgType::LEAVE) <= 3, "emisiones acotadas");
    PROP("PRO-6", "El gateway avisa una sola vez y el nodo acaba de baja");
    EXPECT_EQ(t.count(Ev::APP_NODE_LEFT), 1, "avisos");
    EXPECT_TRUE(!w.node(0).isJoined() && !w.node(0).isLeaving(), "nodo de baja");
}

CASE("L3", "LEAVE", "LEAVE perdido siempre", "2026-11-01", "Un nodo asociado con sondeo cada 1 s", "Se descartan todos los LEAVE") {
    WorldConfig cfg;
    cfg.pollIntervalMs = 1000;
    World w(ctx, 1, cfg);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.inject(Drop(MsgType::LEAVE, Dir::UP, 0));
    w.node(0).leave();
    w.runFor(12000);
    PROP("PRO-6", "Baja local al agotar los intentos");
    EXPECT_EQ(t.count(Ev::TX, MsgType::LEAVE), 3, "LEAVE emitidos");
    EXPECT_TRUE(!w.node(0).isJoined() && !w.node(0).isLeaving(), "nodo de baja");
    PROP("PRO-6", "El gateway acaba viendolo inactivo");
    EXPECT_EQ(t.count(Ev::APP_NODE_INACTIVE), 1, "avisos de inactivo");
}

CASE("L4", "LEAVE", "Baja mientras espera otro ACK", "2026-11-01", "Evento enviado sin ACK aun", "leave() durante la espera") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.inject(Drop(MsgType::ACK, Dir::DOWN, 1));
    w.node(0).queuePushEvent(kEvent, kEventLen, true);
    w.runFor(100);
    const bool accepted = w.node(0).leave();
    const bool joinedRightAfter = w.node(0).isJoined();
    w.runFor(kExhaustMs + 3000);
    PROP("PRO-6", "leave() devuelve false y el nodo sigue asociado");
    EXPECT_TRUE(!accepted, "leave() rechazado");
    EXPECT_TRUE(joinedRightAfter && w.node(0).isJoined(), "el nodo sigue asociado");
    EXPECT_EQ(t.count(Ev::TX, MsgType::LEAVE), 0, "LEAVE emitidos");
}

CASE("L5", "LEAVE", "Baja con un evento en cola", "2026-11-01", "Evento encolado que aun no ha salido", "leave() inmediato") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.node(0).queuePushEvent(kEvent, kEventLen, true);
    const bool ok = w.node(0).leave();
    w.runFor(kExhaustMs + 1000);
    PROP("PRO-5", "La baja se completa");
    EXPECT_TRUE(ok, "leave() debe aceptarse");
    EXPECT_TRUE(!w.node(0).isJoined() && !w.node(0).isLeaving(), "nodo de baja");
    PROP("PRO-5", "El evento se descarta avisando a la aplicacion");
    EXPECT_EQ(t.count(Ev::TX, MsgType::PUSH_EVENT), 0, "eventos emitidos");
    EXPECT_EQ(countOutcome(t, 0), 1, "avisos de no entregado");
}

CASE("L6", "LEAVE", "POLL o CMD durante la baja", "2026-11-01", "Baja en curso; el gateway no recibe el LEAVE", "POLL y CMD") {
    World w(ctx);
    w.joinAll();
    const uint8_t id = w.nodeId(0);
    Trace& t = w.trace();
    t.mark();
    w.inject(Drop(MsgType::LEAVE, Dir::UP, 0));
    w.node(0).leave();
    w.runFor(200);
    w.gw().sendPoll(id);
    w.runFor(200);
    w.gw().sendCommand(id, kEvent, kEventLen);
    w.runFor(200);
    PROP("PRO-6", "No responde al POLL");
    EXPECT_EQ(t.count(Ev::TX, MsgType::POLL_RESPONSE), 0, "POLL_RESPONSE emitidas");
    PROP("PRO-6", "No ejecuta la orden ni la confirma");
    EXPECT_EQ(t.count(Ev::APP_CMD), 0, "ordenes ejecutadas");
    EXPECT_EQ(t.count(Ev::TX, MsgType::ACK, World::dev(0)), 0, "ACK emitidos por el nodo");
}

CASE("L7", "LEAVE", "Reasociacion tras la baja", "2026-11-01", "Nodo de baja", "requestJoin()") {
    World w(ctx);
    w.joinAll();
    const uint8_t id = w.nodeId(0);
    w.node(0).leave();
    w.runFor(1000);
    Trace& t = w.trace();
    t.mark();
    w.node(0).requestJoin();
    const bool joined = w.joinAll();
    PROP("PRO-6", "Sesion nueva con la misma direccion");
    EXPECT_TRUE(joined, "asociado");
    EXPECT_EQ(t.count(Ev::TX, MsgType::JOIN_REQUEST), 1, "JOIN_REQUEST emitidos");
    EXPECT_EQ(w.nodeId(0), id, "direccion");
    EXPECT_EQ(w.gw().nodeCount(), 1, "nodos activos");
    EXPECT_TRUE(sessionWorks(w), "un evento llega");
}

CASE("L9", "LEAVE", "Baja de un nodo entre varios", "2026-11-01", "Tres nodos con sondeo cada 1 s", "Baja del nodo central") {
    WorldConfig cfg;
    cfg.pollIntervalMs = 1000;
    World w(ctx, 3, cfg);
    w.joinAll();
    w.node(1).leave();
    w.runFor(500);
    Trace& t = w.trace();
    t.mark();
    w.runFor(12000);
    const size_t polled0 = t.count(Ev::RX_OK, MsgType::POLL, World::dev(0));
    const size_t polled2 = t.count(Ev::RX_OK, MsgType::POLL, World::dev(2));
    PROP("PRO-6", "El sondeo salta la entrada del nodo de baja");
    EXPECT_EQ(t.count(Ev::RX_OK, MsgType::POLL, World::dev(1)), 0, "POLL aceptados por el nodo de baja");
    EXPECT_EQ(t.count(Ev::TX, MsgType::POLL_RESPONSE, World::dev(1)), 0, "respuestas del nodo de baja");
    PROP("PRO-6", "Los demas nodos reparten los turnos y siguen asociados");
    EXPECT_TRUE(polled0 >= 5 && polled2 >= 5, "los dos reciben turnos");
    EXPECT_TRUE(polled0 + 1 >= polled2 && polled2 + 1 >= polled0, "reparto justo");
    EXPECT_TRUE(w.node(0).isJoined() && w.node(2).isJoined(), "siguen asociados");
}

CASE("L10", "LEAVE", "Repeticion de un LEAVE", "2026-11-01",
     "Nodo reasociado; atacante con el LEAVE de la sesion anterior", "Reinyeccion") {
    World w(ctx);
    w.joinAll();
    w.node(0).leave();
    w.runFor(1000);
    const std::vector<uint8_t> captured = w.captured(MsgType::LEAVE);
    w.node(0).requestJoin();
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.attackerSend(captured);
    w.runFor(500);
    PROP("PRO-8", "La repeticion se rechaza y el nodo sigue asociado");
    EXPECT_EQ(t.count(Ev::APP_NODE_LEFT), 0, "avisos de baja");
    EXPECT_EQ(w.gw().nodeCount(), 1, "nodos activos");
    EXPECT_TRUE(sessionWorks(w), "un evento llega");
}
