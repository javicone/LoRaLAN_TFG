// Familia PUSH. Lo esperado sale de RF4, RF5 y RNF2 (capitulo 3).

#include "common.h"

CASE("U1", "PUSH", "Evento en reposo con el canal libre", "2026-11-01",
     "Un nodo asociado, canal limpio, sin sondeo", "Ninguno") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();

    const uint32_t queuedAt = w.now();
    const bool queued = w.node(0).queuePushEvent(kEvent, kEventLen, true);
    w.runFor(3000);

    PROP("PRO-3", "Comprueba el canal una vez y emite el evento sin esperar turno");
    EXPECT_TRUE(queued, "el evento debe aceptarse en cola");
    EXPECT_EQ(t.count(Ev::CAD_FREE, World::dev(0)), 1, "escuchas con el canal libre");
    EXPECT_EQ(t.count(Ev::TX, MsgType::PUSH_EVENT), 1, "PUSH_EVENT emitidos");

    PROP("RNF-1", "El evento llega a la aplicacion del gateway en un tiempo en el aire");
    const Event* tx = t.first(Ev::TX, MsgType::PUSH_EVENT);
    const Event* app = t.first(Ev::APP_PUSH);
    EXPECT_TRUE(tx != nullptr && app != nullptr, "deben constar la emision y la entrega");
    if (tx != nullptr && app != nullptr) {
        EXPECT_EQ(app->t - queuedAt, w.channel().airtimeMs(tx->len), "latencia en ms");
    }

    PROP("PRO-3", "El gateway confirma una vez y la aplicacion del nodo conoce el desenlace");
    EXPECT_EQ(t.count(Ev::TX, MsgType::ACK, kGatewayRadio), 1, "ACK emitidos por el gateway");
    EXPECT_EQ(t.count(Ev::APP_PUSH_ACK), 1, "desenlaces notificados");
    const Event* result = t.first(Ev::APP_PUSH_ACK);
    EXPECT_TRUE(result != nullptr && result->value == 1, "el desenlace debe ser entregado");
}

CASE("U2", "PUSH", "Evento sin ACK", "2026-11-01", "Un nodo asociado", "Evento sin confirmacion") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.node(0).queuePushEvent(kEvent, kEventLen, false);
    w.runFor(kExhaustMs);
    PROP("PRO-3", "Una emision, una entrega y sin ACK");
    EXPECT_EQ(t.count(Ev::TX, MsgType::PUSH_EVENT), 1, "emisiones");
    EXPECT_EQ(t.count(Ev::APP_PUSH), 1, "entregas");
    EXPECT_EQ(t.count(Ev::TX, MsgType::ACK), 0, "ACK");
    EXPECT_EQ(t.count(Ev::APP_PUSH_ACK), 0, "desenlaces notificados");
}

CASE("U3", "PUSH", "Canal ocupado", "2026-11-01", "Emisor ajeno de 500 ms", "Evento con el canal ocupado") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    const uint32_t start = w.now();
    w.channel().occupy(start, 500);
    w.node(0).queuePushEvent(kEvent, kEventLen, true);
    w.runFor(2500);
    const auto busy = t.select(Ev::CAD_BUSY, World::dev(0));
    PROP("PRO-3", "Aplaza con espera creciente");
    EXPECT_TRUE(busy.size() >= 2, "escuchas con el canal ocupado");
    bool growing = true;
    for (size_t i = 2; i < busy.size(); ++i) {
        if (busy[i]->t - busy[i - 1]->t <= busy[i - 1]->t - busy[i - 2]->t) growing = false;
    }
    EXPECT_TRUE(growing, "cada espera debe ser mayor que la anterior");
    PROP("PRO-3", "Transmite cuando se libera, sin solape, y se entrega");
    const Event* tx = t.first(Ev::TX, MsgType::PUSH_EVENT);
    EXPECT_TRUE(tx != nullptr && tx->t >= start + 500, "emite con el canal libre");
    EXPECT_EQ(t.count(Ev::COLLISION), 0, "colisiones");
    EXPECT_EQ(t.count(Ev::APP_PUSH), 1, "entregas");
}

CASE("U4", "PUSH", "Canal ocupado de forma permanente", "2026-11-01", "Emisor ajeno de 120 s", "Evento con el canal ocupado") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.channel().occupy(w.now(), 120000);
    w.node(0).queuePushEvent(kEvent, kEventLen, true);
    w.runFor(10000);
    PROP("PRO-5", "Limite de intentos de escucha y aviso a la aplicacion");
    EXPECT_EQ(countOutcome(t, 0), 1, "avisos de no entregado en 10 s");
    const int listened = t.count(Ev::CAD_BUSY, World::dev(0));
    EXPECT_EQ(listened, 8, "escuchas con el canal ocupado");
    w.runFor(100000);
    EXPECT_EQ(t.count(Ev::CAD_BUSY, World::dev(0)), listened, "deja de escuchar al desistir");
    EXPECT_EQ(t.count(Ev::TX, MsgType::PUSH_EVENT), 0, "no transmite sobre el canal ocupado");
    EXPECT_EQ(countOutcome(t, 0), 1, "un solo aviso");

    // Pasado el canal ocupado, la aplicacion puede volver a intentarlo.
    w.runFor(10000 + 1000);
    PROP("PRO-5", "Tras desistir, un evento nuevo sale con el canal libre");
    EXPECT_TRUE(w.node(0).queuePushEvent(kOther, kOtherLen, true), "evento nuevo aceptado");
    w.runFor(500);
    EXPECT_EQ(countWithData(t, Ev::APP_PUSH, kOther, kOtherLen), 1, "entrega del evento nuevo");

    World w2(ctx);
    w2.joinAll();
    Trace& t2 = w2.trace();
    t2.mark();
    w2.channel().occupy(w2.now(), 8000);
    w2.node(0).queuePushEvent(kEvent, kEventLen, true, true);
    w2.runFor(9000);
    PROP("PRO-5", "Con rescate, el evento abandonado se entrega por la via del sondeo");
    EXPECT_EQ(countOutcome(t2, 0), 1, "avisos de no entregado");
    w2.gw().sendPoll(w2.nodeId(0));
    w2.runFor(1500);
    EXPECT_EQ(countWithData(t2, Ev::APP_POLL_RESP, kEvent, kEventLen), 1, "entregas por el sondeo");
}

CASE("U5", "PUSH", "Primera emision perdida", "2026-11-01", "Un nodo asociado", "Se descarta el 1.er PUSH_EVENT") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.inject(Drop(MsgType::PUSH_EVENT, Dir::UP, 1));
    w.node(0).queuePushEvent(kEvent, kEventLen, true);
    w.runFor(kExhaustMs);
    PROP("PRO-5", "Un reintento y una sola entrega");
    EXPECT_EQ(t.count(Ev::TX, MsgType::PUSH_EVENT), 2, "emisiones");
    EXPECT_EQ(t.retries(MsgType::PUSH_EVENT), 1, "reintentos");
    {
        const auto tx = t.select(Ev::TX, MsgType::PUSH_EVENT);
        if (tx.size() == 2) {
            const uint32_t gap = tx[1]->t - tx[0]->t;
            EXPECT_TRUE(gap >= 1200 && gap <= 1801, "reintento tras el plazo del ACK mas la parte aleatoria");
        }
    }
    EXPECT_EQ(t.count(Ev::APP_PUSH), 1, "entregas");
    EXPECT_EQ(countOutcome(t, 1), 1, "confirmado");
}

CASE("U6", "PUSH", "Evento con el primer ACK perdido", "2026-11-01",
     "Un nodo asociado, canal limpio, sin sondeo", "Se descarta el 1.er ACK de bajada") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();

    w.inject(Drop(MsgType::ACK, Dir::DOWN, 1));
    w.node(0).queuePushEvent(kEvent, kEventLen, true);
    w.runFor(5000);

    PROP("PRO-5", "Sin confirmacion, el nodo retransmite el evento una vez");
    EXPECT_EQ(t.count(Ev::TX, MsgType::PUSH_EVENT), 2, "PUSH_EVENT emitidos");
    EXPECT_EQ(t.retries(MsgType::PUSH_EVENT), 1, "emisiones marcadas como reintento");

    PROP("PRO-5", "La aplicacion del gateway recibe el evento una sola vez");
    EXPECT_EQ(t.count(Ev::APP_PUSH), 1, "entregas a la aplicacion");

    PROP("PRO-5", "El gateway reconfirma y el nodo da el evento por entregado");
    EXPECT_EQ(t.count(Ev::TX, MsgType::ACK, kGatewayRadio), 2, "ACK emitidos por el gateway");
    EXPECT_EQ(t.count(Ev::APP_PUSH_ACK), 1, "desenlaces notificados");
    const Event* result = t.first(Ev::APP_PUSH_ACK);
    EXPECT_TRUE(result != nullptr && result->value == 1, "el desenlace debe ser entregado");
}

CASE("U7", "PUSH", "Todo perdido, sin rescate", "2026-11-01", "Un nodo asociado", "Se descartan todos los PUSH_EVENT") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.inject(Drop(MsgType::PUSH_EVENT, Dir::UP, 0));
    w.node(0).queuePushEvent(kEvent, kEventLen, true);
    w.runFor(kExhaustMs + 1000);
    PROP("PRO-5", "Un numero acotado de emisiones y aviso de no entregado");
    EXPECT_EQ(t.count(Ev::TX, MsgType::PUSH_EVENT), 3, "emisiones");
    EXPECT_EQ(t.count(Ev::APP_PUSH), 0, "entregas");
    EXPECT_EQ(countOutcome(t, 0), 1, "avisos de no entregado");
}

CASE("U8", "PUSH", "Todo perdido, con rescate", "2026-11-01", "Evento con rescate", "Se descartan todos los PUSH_EVENT") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.inject(Drop(MsgType::PUSH_EVENT, Dir::UP, 0));
    w.node(0).queuePushEvent(kEvent, kEventLen, true, true);
    w.runFor(kExhaustMs + 200);
    PROP("PRO-5", "Agotados los intentos, la aplicacion lo sabe y el evento queda para el sondeo");
    EXPECT_EQ(countOutcome(t, 0), 1, "avisos de no entregado");
    EXPECT_TRUE(w.node(0).isPollPayloadPending(), "evento rescatado");
    w.gw().sendPoll(w.nodeId(0));
    w.runFor(1500);
    PROP("PRO-5", "El evento se entrega por la via del sondeo");
    EXPECT_EQ(countWithData(t, Ev::APP_POLL_RESP, kEvent, kEventLen), 1, "entregas por el sondeo");
    EXPECT_TRUE(!w.node(0).isPollPayloadPending(), "carga borrada");
}

CASE("U9", "PUSH", "Rescate con otra carga ya encolada", "2026-11-01", "Carga A pendiente; evento B con rescate",
     "Se descartan todos los PUSH_EVENT") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.node(0).queuePollPayload(kOther, kOtherLen, true);
    w.inject(Drop(MsgType::PUSH_EVENT, Dir::UP, 0));
    w.node(0).queuePushEvent(kEvent, kEventLen, true, true);
    w.runFor(kExhaustMs + 200);
    for (int i = 0; i < 2; ++i) {
        w.gw().sendPoll(w.nodeId(0));
        w.runFor(1500);
    }
    PROP("PRO-5", "No se pierde ninguno de los dos datos");
    EXPECT_EQ(countWithData(t, Ev::APP_POLL_RESP, kOther, kOtherLen), 1, "entregas de la carga");
    EXPECT_EQ(countWithData(t, Ev::APP_POLL_RESP, kEvent, kEventLen), 1, "entregas del evento rescatado");
}

CASE("U10", "PUSH", "Dos eventos seguidos", "2026-11-01", "Un nodo asociado", "Dos queuePushEvent seguidos") {
    World w(ctx);
    w.joinAll();
    const bool first = w.node(0).queuePushEvent(kEvent, kEventLen, true);
    const bool second = w.node(0).queuePushEvent(kOther, kOtherLen, true);
    PROP("PRO-3", "El segundo se rechaza mientras el primero no haya salido");
    EXPECT_TRUE(first, "primero aceptado");
    EXPECT_TRUE(!second, "segundo rechazado");
}

CASE("U11", "PUSH", "Evento mientras otro espera ACK", "2026-11-01", "Evento A con el 1.er ACK perdido", "Evento B durante la espera") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.inject(Drop(MsgType::ACK, Dir::DOWN, 1));
    w.node(0).queuePushEvent(kEvent, kEventLen, true);
    w.runFor(200);
    const bool accepted = w.node(0).queuePushEvent(kOther, kOtherLen, true);
    w.runFor(kExhaustMs);
    PROP("PRO-3", "El segundo espera a que termine el primero y despues sale");
    EXPECT_TRUE(accepted, "aceptado en cola");
    const auto tx = t.select(Ev::TX, MsgType::PUSH_EVENT);
    const auto outcomes = t.select(Ev::APP_PUSH_ACK);
    EXPECT_EQ(tx.size(), 3, "emisiones: dos de A y una de B");
    EXPECT_TRUE(tx.size() == 3 && !outcomes.empty() && tx[2]->t >= outcomes[0]->t, "B sale tras el desenlace de A");
    PROP("PRO-3", "Los dos se entregan y se confirman");
    EXPECT_EQ(countWithData(t, Ev::APP_PUSH, kEvent, kEventLen), 1, "entregas de A");
    EXPECT_EQ(countWithData(t, Ev::APP_PUSH, kOther, kOtherLen), 1, "entregas de B");
    EXPECT_EQ(countOutcome(t, 1), 2, "confirmados");
}

CASE("U12", "PUSH", "Evento justo cuando empieza un POLL", "2026-11-01", "Un nodo asociado", "POLL 1 ms despues del evento") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.node(0).queuePushEvent(kEvent, kEventLen, true);
    w.runFor(2);
    w.gw().sendPoll(w.nodeId(0));
    w.runFor(kExhaustMs);
    PROP("PRO-3", "Colisionan una vez, se reintenta y se entrega una vez");
    EXPECT_EQ(t.count(Ev::COLLISION, MsgType::PUSH_EVENT), 1, "colisiones del evento");
    EXPECT_EQ(t.count(Ev::TX, MsgType::PUSH_EVENT), 2, "emisiones");
    EXPECT_EQ(t.count(Ev::APP_PUSH), 1, "entregas");
    EXPECT_EQ(countOutcome(t, 1), 1, "confirmado");
}

CASE("U13", "PUSH", "Evento mientras se responde a un POLL", "2026-11-01", "Respuesta al POLL en el aire", "Evento durante la respuesta") {
    const auto scenario = [&](int pusher, const char* text) {
        World w(ctx, 2);
        w.joinAll();
        Trace& t = w.trace();
        t.mark();
        w.gw().sendPoll(w.nodeId(0));
        w.runUntil([&] { return t.count(Ev::TX, MsgType::POLL_RESPONSE) == 1; }, 500);
        w.node(pusher).queuePushEvent(kEvent, kEventLen, true);
        w.runFor(1500);
        const Event* resp = t.first(Ev::TX, MsgType::POLL_RESPONSE);
        const Event* push = t.first(Ev::TX, MsgType::PUSH_EVENT);
        PROP("PRO-3", text);
        EXPECT_TRUE(resp != nullptr && push != nullptr && push->t >= resp->t + w.channel().airtimeMs(resp->len), "sale tras la respuesta");
        EXPECT_EQ(t.count(Ev::COLLISION), 0, "colisiones");
        EXPECT_EQ(t.count(Ev::APP_PUSH), 1, "entregas");
    };
    scenario(0, "El propio nodo espera a terminar la respuesta, sin colision");
    scenario(1, "Otro nodo detecta la respuesta y espera, sin colision");
}

CASE("U14", "PUSH", "Evento con una carga con ACK en curso", "2026-11-01", "Respuesta con ACK, con el 1.er ACK perdido", "Evento durante la espera") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.inject(Drop(MsgType::ACK, Dir::DOWN, 1));
    w.node(0).queuePollPayload(kOther, kOtherLen, true);
    w.gw().sendPoll(w.nodeId(0));
    w.runFor(300);
    w.node(0).queuePushEvent(kEvent, kEventLen, true);
    w.runFor(kExhaustMs);
    const Event* ack = t.first(Ev::RX_OK, MsgType::ACK);
    const Event* push = t.first(Ev::TX, MsgType::PUSH_EVENT);
    PROP("PRO-3", "El evento espera a que termine el envio fiable en curso");
    EXPECT_TRUE(ack != nullptr && push != nullptr && push->t >= ack->t, "sale tras el ACK de la carga");
    EXPECT_EQ(t.count(Ev::COLLISION), 0, "colisiones");
    PROP("PRO-3", "Ambos se entregan");
    EXPECT_EQ(countWithData(t, Ev::APP_POLL_RESP, kOther, kOtherLen), 1, "entregas de la carga");
    EXPECT_EQ(t.count(Ev::APP_PUSH), 1, "entregas del evento");
}

CASE("U15", "PUSH", "Reintento con el canal ocupado", "2026-11-01", "Evento con el 1.er ACK perdido",
     "Emisor ajeno durante el instante del reintento") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.inject(Drop(MsgType::ACK, Dir::DOWN, 1));
    w.node(0).queuePushEvent(kEvent, kEventLen, true);
    w.runFor(1000);
    const uint32_t from = w.now();
    w.channel().occupy(from, 500);
    w.runFor(kExhaustMs);
    int overlapped = 0;
    for (const Event* e : t.select(Ev::TX, MsgType::PUSH_EVENT)) {
        if (e->t >= from && e->t < from + 500) ++overlapped;
    }
    PROP("PRO-3", "El reintento escucha antes de transmitir");
    EXPECT_EQ(overlapped, 0, "emisiones durante la ocupacion");
    EXPECT_EQ(t.count(Ev::COLLISION, MsgType::PUSH_EVENT), 0, "colisiones del evento");
    EXPECT_TRUE(t.count(Ev::CAD_BUSY, World::dev(0)) >= 1, "debe detectar el canal ocupado");
}

CASE("U16", "PUSH", "Varios nodos colisionan", "2026-11-01", "Cuatro nodos asociados y ventana ciega de 2 ms",
     "Eventos en el mismo instante") {
    WorldConfig cfg;
    cfg.channel.cadBlindMs = 2;
    World w(ctx, 4, cfg);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    for (int i = 0; i < 4; ++i) w.node(i).queuePushEvent(kEvent, kEventLen, true);
    w.runFor(15000);
    PROP("PRO-3", "Dejan de coincidir y todos entregan");
    EXPECT_TRUE(t.count(Ev::COLLISION, MsgType::PUSH_EVENT) >= 2, "se produce la colision inicial");
    EXPECT_EQ(t.count(Ev::APP_PUSH), 4, "entregas");
    EXPECT_EQ(countOutcome(t, 1), 4, "confirmados");
}

CASE("U17", "PUSH", "Reinicio del gateway con un evento en curso", "2026-11-01", "Nodo con deteccion de silencio de 10 s",
     "Reinicio del gateway con el evento en el aire") {
    WorldConfig cfg;
    cfg.silenceTimeoutMs = 10000;
    World w(ctx, 1, cfg);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.node(0).queuePushEvent(kEvent, kEventLen, true);
    w.runFor(10);
    w.rebootGateway();
    w.runFor(20000);
    PROP("PRO-5", "El evento se da por no entregado y la aplicacion lo sabe");
    EXPECT_EQ(t.count(Ev::APP_PUSH), 0, "entregas");
    EXPECT_EQ(countOutcome(t, 0), 1, "avisos de no entregado");
    PROP("PRO-9", "El nodo se reasocia por silencio y el enlace vuelve a funcionar");
    EXPECT_TRUE(t.count(Ev::TX, MsgType::JOIN_REQUEST) >= 1, "repite el JOIN");
    EXPECT_TRUE(w.node(0).isJoined(), "asociado");
    EXPECT_EQ(w.gw().nodeCount(), 1, "nodos en la tabla");
    EXPECT_TRUE(sessionWorks(w), "un evento llega");
}

CASE("U19", "PUSH", "Evento de 0, 1, 64 y 65 B", "2026-11-01", "Un nodo asociado", "Encolar esas cargas") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    uint8_t data[65];
    std::memset(data, 0x42, sizeof(data));
    PROP("RNF-3", "Se acepta y se entrega hasta 64 bytes");
    for (uint8_t len : {0, 1, 64}) {
        EXPECT_TRUE(w.node(0).queuePushEvent(data, len, true), "carga aceptada");
        w.runFor(500);
        const Event* app = t.last(Ev::APP_PUSH);
        EXPECT_TRUE(app != nullptr && app->len == len, "longitud entregada");
    }
    EXPECT_EQ(t.count(Ev::APP_PUSH), 3, "entregas");
    PROP("RNF-3", "Se rechazan 65 bytes");
    EXPECT_TRUE(!w.node(0).queuePushEvent(data, 65, true), "carga de 65 B");
}

// Hallado al analizar EXP5: el reintento lleva una secuencia menor que la respuesta al POLL.
CASE("U22", "PUSH", "Evento perdido y POLL antes del reintento", "2026-11-01", "Un nodo asociado",
     "Se descarta el 1.er PUSH_EVENT y llega un POLL antes del reintento") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.inject(Drop(MsgType::PUSH_EVENT, Dir::UP, 1));
    w.node(0).queuePushEvent(kEvent, kEventLen, true);
    w.runFor(300);
    w.gw().sendPoll(w.nodeId(0));
    w.runFor(kExhaustMs);
    PROP("PRO-5", "Si el nodo recibe la confirmacion, la aplicacion del gateway recibio el evento");
    EXPECT_EQ(countOutcome(t, 1), 1, "confirmaciones");
    EXPECT_EQ(countWithData(t, Ev::APP_PUSH, kEvent, kEventLen), 1, "entregas");
}

CASE("U20", "PUSH", "Tras 63 y 64 perdidas seguidas", "2026-11-01", "Un nodo asociado", "Subida cortada durante N eventos") {
    const auto scenario = [&](int lost, const char* text) {
        World w(ctx);
        w.joinAll();
        w.node(0).queuePushEvent(kEvent, kEventLen, false);
        w.runFor(200);
        w.channel().setLoss(World::dev(0), kGatewayRadio, 100);
        for (int i = 0; i < lost; ++i) {
            w.node(0).queuePushEvent(kEvent, kEventLen, false);
            w.runFor(200);
        }
        w.channel().setLoss(World::dev(0), kGatewayRadio, 0);
        Trace& t = w.trace();
        t.mark();
        w.node(0).queuePushEvent(kOther, kOtherLen, true);
        w.runFor(1000);
        w.node(0).queuePushEvent(kEvent, kEventLen, true);
        w.runFor(1000);
        PROP("PRO-5", text);
        EXPECT_EQ(countWithData(t, Ev::APP_PUSH, kOther, kOtherLen), 1, "1.er evento tras el corte");
        EXPECT_EQ(countWithData(t, Ev::APP_PUSH, kEvent, kEventLen), 1, "2.o evento tras el corte");
    };
    scenario(63, "Tras 63 perdidas recupera la comunicacion");
    scenario(64, "Tras 64 perdidas recupera la comunicacion");
}
