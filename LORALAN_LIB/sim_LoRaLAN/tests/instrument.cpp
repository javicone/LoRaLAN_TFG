// Comprobaciones del propio instrumento (RNF8). No cuentan entre los 75 casos del
// protocolo: demuestran que se cumplen las condiciones del plan de pruebas.

#include "world.h"

namespace {

const uint8_t kData[] = {'D', 'A', 'T', 'O'};

// Escenario con reintentos y jitter de CAD, sensible a cualquier no determinismo.
std::string scenarioTrace(uint32_t seed) {
    Ctx local(seed);
    {
        WorldConfig cfg;
        cfg.channel.lossPct = 20;
        World w(local, 4, cfg);
        w.runFor(20000);
        for (int i = 0; i < 4; ++i) w.node(i).queuePushEvent(kData, sizeof(kData), true);
        w.runFor(10000);
    }
    return local.trace();
}

// Un nodo asociado que emite un evento bajo la regla dada.
template <typename Check>
void withFault(Ctx& ctx, const Fault& fault, Check check) {
    World w(ctx);
    w.joinAll();
    w.trace().mark();
    w.inject(fault);
    w.node(0).queuePushEvent(kData, sizeof(kData), true);
    w.runFor(5000);
    check(w, w.trace());
}

}  // namespace

CASE("SIM1", "SIMULADOR", "Determinismo", "2026-11-01", "Escenario de 4 nodos con 20 % de perdida", "Ninguno") {
    const std::string first = scenarioTrace(7);
    PROP("RNF-5", "La misma semilla reproduce exactamente la misma traza");
    EXPECT_TRUE(!first.empty(), "la traza no debe estar vacia");
    EXPECT_TRUE(first == scenarioTrace(7), "dos ejecuciones deben dar trazas identicas");
    PROP("RNF-5", "Otra semilla produce otra traza");
    EXPECT_TRUE(first != scenarioTrace(8), "la semilla debe influir en la ejecucion");
}

CASE("SIM2", "SIMULADOR", "Aislamiento entre mundos", "2026-11-01", "Un mundo previo con actividad", "Ninguno") {
    {
        Ctx previous;
        World old(previous);
        old.joinAll();
        old.node(0).queuePushEvent(kData, sizeof(kData), true);
        old.runFor(1000);
    }
    World w(ctx);
    PROP("RNF-5", "Un mundo nuevo no hereda reloj, traza ni sesiones del anterior");
    EXPECT_EQ(w.now(), 0, "reloj");
    EXPECT_EQ(w.trace().events().size(), 0, "eventos");
    EXPECT_EQ(w.gw().nodeCount(), 0, "nodos del gateway");
    EXPECT_TRUE(!w.node(0).isJoined(), "el nodo no debe estar asociado");
}

CASE("SIM3", "SIMULADOR", "Un estimulo que no ocurre da No concluyente", "2026-11-01",
     "Un nodo asociado", "Regla sobre el 9.o ACK, que nunca aparece") {
    Ctx inner;
    {
        World w(inner);
        w.joinAll();
        w.inject(Drop(MsgType::ACK, Dir::DOWN, 9));
        w.runFor(100);
    }
    inner.prop("RNF-5", "propiedad de prueba");
    inner.expectTrue(true, "asercion cumplida");
    inner.finish();
    PROP("RNF-5", "Aunque se cumplan las aserciones, la propiedad no se da por superada");
    EXPECT_TRUE(inner.properties().front().verdict == Verdict::INCONCLUSIVE, "veredicto No concluyente");
}

CASE("SIM4", "SIMULADOR", "Acciones de inyeccion de fallos", "2026-11-01",
     "Un nodo asociado que emite un evento", "Una regla distinta en cada sub-caso") {
    withFault(ctx, Drop(MsgType::PUSH_EVENT, Dir::UP, 1), [&](World&, Trace& t) {
        PROP("RNF-5", "DROP: la trama elegida no llega y el nodo la repite");
        EXPECT_EQ(t.count(Ev::TX, MsgType::PUSH_EVENT), 2, "emisiones");
        EXPECT_EQ(t.count(Ev::FAULT), 1, "actuaciones de la regla");
        EXPECT_EQ(t.count(Ev::APP_PUSH), 1, "entregas");
    });
    withFault(ctx, Delay(MsgType::PUSH_EVENT, Dir::UP, 1, 500), [&](World& w, Trace& t) {
        PROP("RNF-5", "DELAY: la trama llega con el retraso pedido");
        const Event* tx = t.first(Ev::TX, MsgType::PUSH_EVENT);
        const Event* app = t.first(Ev::APP_PUSH);
        EXPECT_TRUE(tx != nullptr && app != nullptr, "emision y entrega");
        if (tx != nullptr && app != nullptr) {
            EXPECT_EQ(app->t - tx->t, w.channel().airtimeMs(tx->len) + 500, "retardo");
        }
    });
    withFault(ctx, Duplicate(MsgType::PUSH_EVENT, Dir::UP, 1, 200), [&](World&, Trace& t) {
        PROP("RNF-5", "DUPLICATE: el receptor recibe dos copias de una sola emision");
        EXPECT_EQ(t.count(Ev::TX, MsgType::PUSH_EVENT), 1, "emisiones");
        EXPECT_EQ(t.count(Ev::RX_OK, MsgType::PUSH_EVENT, kGatewayRadio), 2, "recepciones en el gateway");
    });
    withFault(ctx, Corrupt(MsgType::PUSH_EVENT, Dir::UP, 1, 8), [&](World&, Trace& t) {
        PROP("RNF-5", "CORRUPT: la trama alterada se traza como descartada por MIC");
        EXPECT_EQ(t.drops(DropReason::BAD_MIC), 1, "descartes por MIC");
    });
    {
        World w(ctx, 2);
        w.joinAll();
        Trace& t = w.trace();
        t.mark();
        w.inject(Reorder(MsgType::PUSH_EVENT, Dir::UP, 1));
        w.node(0).queuePushEvent(kData, sizeof(kData), false);
        w.runFor(200);
        w.node(1).queuePushEvent(kData, sizeof(kData), false);
        w.runFor(200);
        PROP("RNF-5", "REORDER: la primera trama se entrega despues de la segunda");
        const Event* firstRx = t.first(Ev::APP_PUSH);
        const Event* lastRx = t.last(Ev::APP_PUSH);
        EXPECT_EQ(t.count(Ev::APP_PUSH), 2, "entregas");
        EXPECT_TRUE(firstRx != nullptr && firstRx->value == w.nodeId(1), "primero llega el evento del nodo 1");
        EXPECT_TRUE(lastRx != nullptr && lastRx->value == w.nodeId(0), "despues llega el del nodo 0");
    }
}

CASE("SIM5", "SIMULADOR", "Modelo de canal", "2026-11-01", "Canal declarado en spec/modelo_canal.md", "Ninguno") {
    {
        World w(ctx);
        PROP("RNF-5", "El airtime coincide con el calculo a mano: 11 B en SF7/125 kHz son 41,2 ms");
        EXPECT_EQ(w.channel().airtimeMs(11), 42, "airtime redondeado al ms superior");
    }
    const auto burst = [&](uint32_t blindMs) {
        WorldConfig cfg;
        cfg.channel.cadBlindMs = blindMs;
        World w(ctx, 2, cfg);
        w.joinAll();
        w.trace().mark();
        w.node(0).queuePushEvent(kData, sizeof(kData), false);
        w.node(1).queuePushEvent(kData, sizeof(kData), false);
        w.runFor(500);
        return w.trace().count(Ev::COLLISION);
    };
    const size_t ideal = burst(0);
    const size_t blind = burst(2);
    PROP("RNF-5", "Sin ventana ciega, dos nodos simultaneos no colisionan; con ella, si");
    EXPECT_EQ(ideal, 0, "colisiones con W = 0");
    EXPECT_EQ(blind, 2, "tramas destruidas con W = 2 ms");
    {
        World w(ctx, 2);
        w.joinAll();
        Trace& t = w.trace();
        t.mark();
        w.channel().setLoss(World::dev(0), kGatewayRadio, 100);
        w.node(0).queuePushEvent(kData, sizeof(kData), false);
        w.runFor(5);
        w.node(1).queuePushEvent(kData, sizeof(kData), false);
        w.runFor(5);
        PROP("RNF-5", "Una trama que el enlace va a perder sigue ocupando el canal");
        EXPECT_EQ(t.count(Ev::CAD_BUSY, World::dev(1)), 1, "el otro nodo la detecta");
        w.runFor(500);
        PROP("RNF-5", "La perdida es por enlace: solo afecta al receptor indicado");
        EXPECT_EQ(t.count(Ev::LOST, MsgType::PUSH_EVENT, kGatewayRadio), 1, "perdida en el gateway");
        EXPECT_EQ(t.count(Ev::APP_PUSH), 1, "solo se entrega el evento del nodo 1");
    }
    {
        World w(ctx);
        w.joinAll();
        Trace& t = w.trace();
        t.mark();
        w.channel().occupy(w.now(), 30);
        w.gw().sendTimeSync(1000);
        w.runFor(100);
        PROP("RNF-5", "Un emisor ajeno destruye una trama solapada y queda trazado");
        EXPECT_EQ(t.count(Ev::COLLISION, MsgType::TIME_SYNC), 1, "colisiones");
    }
}
