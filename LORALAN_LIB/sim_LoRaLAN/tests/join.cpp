// Familia JOIN. Lo esperado sale de RF1, RF2, RF4, RF12 y RNF5 (capitulo 3).

#include <set>

#include "common.h"

namespace {
const uint8_t kWrongKey[16] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                               0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x01};
}  // namespace

CASE("J1", "JOIN", "Asociacion con canal limpio", "2026-11-01",
     "Gateway y un nodo recien arrancados, canal limpio", "Ninguno") {
    World w(ctx);
    const bool joined = w.joinAll();
    Trace& t = w.trace();

    PROP("PRO-6", "El nodo queda asociado tras un unico intercambio JOIN_REQUEST y JOIN_ACCEPT");
    EXPECT_TRUE(joined, "el nodo debe quedar asociado");
    EXPECT_EQ(t.count(Ev::TX, MsgType::JOIN_REQUEST), 1, "JOIN_REQUEST emitidos");
    EXPECT_EQ(t.count(Ev::TX, MsgType::JOIN_ACCEPT), 1, "JOIN_ACCEPT emitidos");

    PROP("PRO-6", "La aplicacion de cada extremo recibe un unico aviso de la asociacion");
    EXPECT_EQ(t.count(Ev::APP_JOIN_ACCEPTED, World::dev(0)), 1, "avisos en el nodo");
    EXPECT_EQ(t.count(Ev::APP_NODE_JOINED, kGatewayRadio), 1, "avisos en el gateway");
}

CASE("J2", "JOIN", "JOIN_REQUEST perdido", "2026-11-01", "Nodo recien arrancado", "Se descarta el 1.er JOIN_REQUEST") {
    World w(ctx);
    Trace& t = w.trace();
    w.inject(Drop(MsgType::JOIN_REQUEST, Dir::UP, 1));
    w.runUntil([&] { return t.count(Ev::TX, MsgType::JOIN_REQUEST) == 1; }, 5000);
    const std::vector<uint8_t> first = w.captured(MsgType::JOIN_REQUEST);
    const bool joined = w.joinAll();
    const std::vector<uint8_t> second = w.captured(MsgType::JOIN_REQUEST);
    const auto reqs = t.select(Ev::TX, MsgType::JOIN_REQUEST);

    PROP("PRO-6", "Repite el JOIN_REQUEST pasado joinRetryMs y queda asociado");
    EXPECT_TRUE(joined, "asociado");
    EXPECT_EQ(reqs.size(), 2, "JOIN_REQUEST emitidos");
    if (reqs.size() == 2) {
        const uint32_t gap = reqs[1]->t - reqs[0]->t;
        EXPECT_TRUE(gap >= 1000 && gap <= 1251, "separacion de joinRetryMs mas la parte aleatoria");
    }

    PROP("PRO-6", "El reintento lleva un DevNonce nuevo");
    EXPECT_TRUE(first.size() > 8 && second.size() > 8 && (first[7] != second[7] || first[8] != second[8]), "DevNonce distinto");
}

CASE("J3", "JOIN", "JOIN_ACCEPT perdido", "2026-11-01", "Nodo recien arrancado", "Se descarta el 1.er JOIN_ACCEPT") {
    World w(ctx);
    Trace& t = w.trace();
    w.inject(Drop(MsgType::JOIN_ACCEPT, Dir::DOWN, 1));
    const bool joined = w.joinAll();

    PROP("PRO-6", "El nodo repite y el gateway reutiliza la entrada y la direccion");
    EXPECT_TRUE(joined, "asociado");
    EXPECT_EQ(t.count(Ev::TX, MsgType::JOIN_REQUEST), 2, "JOIN_REQUEST emitidos");
    EXPECT_EQ(w.gw().nodeCount(), 1, "entradas en la tabla");
    EXPECT_EQ(w.nodeId(0), 0x20, "direccion");

    PROP("PRO-6", "Ambos extremos acaban con la misma clave de sesion");
    EXPECT_TRUE(sessionWorks(w), "un evento llega descifrado correctamente");
}

CASE("J4", "JOIN", "Canal ocupado al asociarse", "2026-11-01", "Emisor ajeno de 1500 ms desde el arranque", "Canal ocupado") {
    World w(ctx);
    Trace& t = w.trace();
    w.channel().occupy(0, 1500);
    const bool joined = w.joinAll();
    const auto reqs = t.select(Ev::TX, MsgType::JOIN_REQUEST);

    PROP("PRO-3", "No transmite con el canal ocupado");
    EXPECT_TRUE(t.count(Ev::CAD_BUSY, World::dev(0)) >= 1, "debe detectar el canal ocupado");
    EXPECT_TRUE(!reqs.empty() && reqs[0]->t >= 1500, "el primer JOIN_REQUEST sale con el canal libre");
    EXPECT_EQ(t.count(Ev::COLLISION), 0, "colisiones");
    PROP("PRO-3", "Reintenta despues y queda asociado");
    EXPECT_TRUE(joined, "asociado");
}

CASE("J5", "JOIN", "Varios nodos se asocian a la vez", "2026-11-01", "Cuatro nodos con el mismo joinRetryMs", "Arranque simultaneo") {
    const auto scenario = [&](uint32_t blindMs, const char* text) {
        WorldConfig cfg;
        cfg.channel.cadBlindMs = blindMs;
        cfg.tweakNode = [](int, LoRaLANNode::Config& c) { c.joinRetryMs = 1000; };
        World w(ctx, 4, cfg);
        const bool joined = w.runUntil(
            [&] {
                for (int i = 0; i < 4; ++i) {
                    if (!w.node(i).isJoined()) return false;
                }
                return true;
            },
            60000);
        w.runFor(200);
        PROP("PRO-1", text);
        EXPECT_TRUE(joined, "los cuatro asociados");
        EXPECT_EQ(w.gw().nodeCount(), 4, "entradas en la tabla");
        std::set<uint8_t> ids;
        for (int i = 0; i < 4; ++i) ids.insert(w.nodeId(i));
        EXPECT_EQ(ids.size(), 4, "direcciones distintas");
    };
    scenario(0, "Con escucha ideal, todos quedan asociados con su direccion");
    scenario(2, "Con ventana ciega de 2 ms, todos quedan asociados con su direccion");
}

CASE("J6", "JOIN", "Nodo reiniciado con sesion viva", "2026-11-01", "Nodo asociado que ya emitio 3 eventos", "Reinicio del nodo") {
    World w(ctx);
    w.joinAll();
    for (int i = 0; i < 3; ++i) {
        w.node(0).queuePushEvent(kEvent, kEventLen, true);
        w.runFor(300);
    }
    Trace& t = w.trace();
    t.mark();
    w.rebootNode(0);
    const bool joined = w.joinAll();

    PROP("PRO-6", "El nodo reiniciado establece una sesion nueva");
    EXPECT_TRUE(joined, "asociado");
    EXPECT_EQ(t.count(Ev::TX, MsgType::JOIN_REQUEST), 1, "JOIN_REQUEST emitidos");
    EXPECT_EQ(w.gw().nodeCount(), 1, "entradas en la tabla");
    PROP("PRO-6", "La secuencia se reinicia: un evento con numero bajo se acepta");
    EXPECT_TRUE(sessionWorks(w), "el evento llega a la aplicacion");
}

CASE("J7", "JOIN", "Clave maestra incorrecta", "2026-11-01", "Nodo con otra APPKEY", "Ninguno") {
    WorldConfig cfg;
    cfg.tweakNode = [](int, LoRaLANNode::Config& c) { c.appKey = kWrongKey; };
    World w(ctx, 1, cfg);
    w.runFor(8000);
    Trace& t = w.trace();
    uint8_t hw[LORALAN_HWID_SIZE];

    PROP("PRO-6", "El nodo nunca queda asociado");
    EXPECT_TRUE(!w.node(0).isJoined(), "no asociado");
    EXPECT_EQ(t.count(Ev::TX, MsgType::JOIN_ACCEPT), 0, "JOIN_ACCEPT emitidos");
    EXPECT_TRUE(t.drops(DropReason::BAD_MIC) >= 1, "el gateway lo rechaza por el MIC");
    PROP("PRO-6", "El gateway no guarda ningun estado");
    EXPECT_EQ(w.gw().nodeCount(), 0, "nodos en la tabla");
    EXPECT_EQ(t.count(Ev::APP_NODE_JOINED), 0, "avisos");
    EXPECT_TRUE(!w.gw().nodeHwId(w.nodeId(0), hw), "sin entrada para esa direccion");
}

CASE("J8", "JOIN", "Otro identificador de red", "2026-11-01", "Nodo con otro netId", "Ninguno") {
    WorldConfig cfg;
    cfg.tweakNode = [](int, LoRaLANNode::Config& c) { c.netId = 0x11; };
    World w(ctx, 1, cfg);
    w.runFor(8000);
    Trace& t = w.trace();
    PROP("PRO-6", "El gateway ignora las tramas de otra red");
    EXPECT_TRUE(!w.node(0).isJoined(), "no asociado");
    EXPECT_EQ(w.gw().nodeCount(), 0, "nodos en la tabla");
    EXPECT_EQ(t.count(Ev::TX, MsgType::JOIN_ACCEPT), 0, "JOIN_ACCEPT emitidos");
    EXPECT_TRUE(t.drops(DropReason::WRONG_NET) >= 1, "descartes por red");
}

CASE("J9", "JOIN", "Tabla llena", "2026-11-01", "33 nodos para 32 entradas", "Ninguno") {
    World w(ctx, 33);
    w.runFor(40000);
    int joined = 0, firstJoined = -1;
    for (int i = 0; i < 33; ++i) {
        if (w.node(i).isJoined()) {
            ++joined;
            if (firstJoined < 0) firstJoined = i;
        }
    }
    PROP("RNF-3", "Solo se admiten 32 nodos y el 33.o se rechaza");
    EXPECT_EQ(w.gw().nodeCount(), 32, "nodos en la tabla");
    EXPECT_EQ(joined, 32, "nodos asociados");
    PROP("RNF-3", "Los nodos admitidos no se ven afectados");
    EXPECT_TRUE(firstJoined >= 0 && sessionWorks(w, firstJoined), "un evento llega");
}

CASE("J10", "JOIN", "Direccion ocupada o sin preferencia", "2026-11-01",
     "Un nodo pide 0x20, otro pide la misma y otro no pide ninguna", "Ninguno") {
    WorldConfig cfg;
    cfg.tweakNode = [](int i, LoRaLANNode::Config& c) { c.nodeId = i == 2 ? 0 : 0x20; };
    World w(ctx, 3, cfg);
    const bool joined = w.joinAll();
    const uint8_t a = w.nodeId(0), b = w.nodeId(1), c = w.nodeId(2);

    PROP("PRO-6", "El primero conserva la pedida y los otros reciben una libre del rango dinamico");
    EXPECT_TRUE(joined, "asociados");
    EXPECT_EQ(a, 0x20, "direccion del primero");
    EXPECT_TRUE(b >= LoRaLANGateway::kFirstDynamicId && c >= LoRaLANGateway::kFirstDynamicId, "rango dinamico");
    EXPECT_TRUE(a != b && a != c && b != c, "direcciones distintas");
    PROP("PRO-6", "El nodo que cambio de direccion funciona con la asignada");
    EXPECT_TRUE(sessionWorks(w, 1), "un evento llega");
    const Event* app = w.trace().last(Ev::APP_PUSH);
    EXPECT_TRUE(app != nullptr && app->value == b, "origen del evento");
}

CASE("J12", "JOIN", "Repeticion del ultimo JOIN", "2026-11-01", "Nodo asociado; atacante con su JOIN_REQUEST", "Reinyeccion") {
    World w(ctx);
    w.joinAll();
    const std::vector<uint8_t> captured = w.captured(MsgType::JOIN_REQUEST);
    Trace& t = w.trace();
    t.mark();
    w.attackerSend(captured);
    w.runFor(500);
    PROP("PRO-8", "La repeticion se rechaza");
    EXPECT_EQ(t.count(Ev::TX, MsgType::JOIN_ACCEPT, kGatewayRadio), 0, "JOIN_ACCEPT emitidos");
    EXPECT_EQ(t.count(Ev::APP_NODE_JOINED), 0, "avisos de asociacion");
    PROP("PRO-8", "La sesion sigue intacta");
    EXPECT_TRUE(sessionWorks(w), "un evento llega");
}

CASE("J13", "JOIN", "Repeticion de un JOIN antiguo", "2026-11-01",
     "Nodo reasociado; atacante con el JOIN_REQUEST de la sesion anterior", "Reinyeccion") {
    World w(ctx);
    w.joinAll();
    const std::vector<uint8_t> old = w.captured(MsgType::JOIN_REQUEST);
    w.node(0).resetJoin();
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.attackerSend(old);
    w.runFor(500);
    PROP("PRO-8", "La repeticion se rechaza");
    KNOWN_FAIL("J13: el gateway solo recuerda el ultimo DevNonce");
    EXPECT_EQ(t.count(Ev::TX, MsgType::JOIN_ACCEPT, kGatewayRadio), 0, "JOIN_ACCEPT emitidos");
    EXPECT_EQ(t.count(Ev::APP_NODE_JOINED), 0, "avisos de asociacion");
    PROP("PRO-8", "La sesion sigue intacta");
    KNOWN_FAIL("J13: el gateway solo recuerda el ultimo DevNonce");
    EXPECT_TRUE(sessionWorks(w), "un evento llega");
}

CASE("J15", "JOIN", "Envios antes de asociarse", "2026-11-01", "Nodo sin asociar", "Encolar evento y carga") {
    World w(ctx);
    PROP("PRO-6", "Sin asociacion no se acepta ningun envio");
    EXPECT_TRUE(!w.node(0).queuePushEvent(kEvent, kEventLen, true), "queuePushEvent");
    EXPECT_TRUE(!w.node(0).queuePollPayload(kEvent, kEventLen, true), "queuePollPayload");
}
