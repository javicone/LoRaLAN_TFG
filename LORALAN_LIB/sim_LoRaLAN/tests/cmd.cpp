// Familia CMD. Lo esperado sale de RF5, RF7, RF10 y RF12 (capitulo 3).
// RF7 se lee con la ampliacion «como mucho una vez», pendiente de pasar al capitulo 3.

#include "common.h"

namespace {
const uint8_t kOrder[] = {'A', 'B', 'R', 'I', 'R', '-', 'R', 'E', 'L', 'E'};
}

CASE("C1", "CMD", "Orden con canal limpio", "2026-11-01", "Un nodo asociado", "Ninguno") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    const bool ok = w.gw().sendCommand(w.nodeId(0), kOrder, sizeof(kOrder));
    w.runFor(2000);
    PROP("PRO-4", "Se descifra y se ejecuta una vez, y se confirma");
    EXPECT_TRUE(ok, "sendCommand aceptado");
    EXPECT_EQ(t.count(Ev::TX, MsgType::CMD), 1, "emisiones");
    EXPECT_EQ(countWithData(t, Ev::APP_CMD, kOrder, sizeof(kOrder)), 1, "ejecuciones con los datos correctos");
    EXPECT_EQ(t.count(Ev::TX, MsgType::ACK, World::dev(0)), 1, "ACK del nodo");
    PROP("PRO-8", "Los bytes en el aire no contienen el texto claro");
    EXPECT_TRUE(!containsBytes(w.captured(MsgType::CMD), kOrder, sizeof(kOrder)), "texto claro ausente");
}

CASE("C2", "CMD", "Orden perdida", "2026-11-01", "Un nodo asociado", "Se descarta el 1.er CMD") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.inject(Drop(MsgType::CMD, Dir::DOWN, 1));
    w.gw().sendCommand(w.nodeId(0), kOrder, sizeof(kOrder));
    w.runFor(kExhaustMs);
    PROP("PRO-5", "El gateway reintenta y la orden se ejecuta una vez");
    EXPECT_EQ(t.count(Ev::TX, MsgType::CMD), 2, "emisiones");
    EXPECT_EQ(t.retries(MsgType::CMD), 1, "reintentos");
    EXPECT_EQ(t.count(Ev::APP_CMD), 1, "ejecuciones");

    World w2(ctx);
    w2.joinAll();
    Trace& t2 = w2.trace();
    t2.mark();
    w2.inject(Drop(MsgType::CMD, Dir::DOWN, 1));
    w2.gw().sendCommand(w2.nodeId(0), kOrder, sizeof(kOrder));
    w2.runFor(300);
    w2.gw().sendPoll(w2.nodeId(0));
    w2.runFor(kExhaustMs);
    PROP("PRO-5", "Aunque llegue un POLL antes del reintento, la orden se ejecuta una vez");
    EXPECT_EQ(t2.count(Ev::APP_CMD), 1, "ejecuciones");
}

CASE("C3", "CMD", "ACK del nodo perdido", "2026-11-01", "Un nodo asociado", "Se descarta el 1.er ACK de subida") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    w.inject(Drop(MsgType::ACK, Dir::UP, 1));
    w.gw().sendCommand(w.nodeId(0), kOrder, sizeof(kOrder));
    w.runFor(kExhaustMs);
    PROP("PRO-5", "El gateway retransmite la orden una vez");
    EXPECT_EQ(t.count(Ev::TX, MsgType::CMD), 2, "emisiones");
    PROP("PRO-4", "El nodo ejecuta la orden como mucho una vez");
    EXPECT_EQ(t.count(Ev::APP_CMD), 1, "ejecuciones");
}

CASE("C4", "CMD", "Orden a un nodo no asociado", "2026-11-01", "Un nodo sin asociar", "sendCommand") {
    World w(ctx);
    Trace& t = w.trace();
    PROP("PRO-4", "sendCommand devuelve false y no emite nada");
    EXPECT_TRUE(!w.gw().sendCommand(0x55, kOrder, sizeof(kOrder)), "direccion desconocida");
    EXPECT_TRUE(!w.gw().sendCommand(w.nodeId(0), kOrder, sizeof(kOrder)), "nodo sin asociar");
    w.runFor(100);
    EXPECT_EQ(t.count(Ev::TX, MsgType::CMD), 0, "emisiones");
}

CASE("C5", "CMD", "Orden con otra en espera", "2026-11-01", "Orden A sin confirmar", "Orden B") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    t.mark();
    const bool first = w.gw().sendCommand(w.nodeId(0), kOrder, sizeof(kOrder));
    const bool second = w.gw().sendCommand(w.nodeId(0), kOther, kOtherLen);
    w.runFor(1000);
    const bool third = w.gw().sendCommand(w.nodeId(0), kOther, kOtherLen);
    w.runFor(1000);
    PROP("PRO-4", "La segunda se rechaza y la aplicacion puede reintentar al terminar la primera");
    EXPECT_TRUE(first, "primera aceptada");
    EXPECT_TRUE(!second, "segunda rechazada");
    EXPECT_TRUE(third, "reintento aceptado");
    EXPECT_EQ(t.count(Ev::APP_CMD), 2, "ejecuciones");
}

CASE("C7", "CMD", "Repeticion de una orden capturada", "2026-11-01", "Orden ya ejecutada; atacante con la trama", "Reinyeccion") {
    World w(ctx);
    w.joinAll();
    Trace& t = w.trace();
    w.gw().sendCommand(w.nodeId(0), kOrder, sizeof(kOrder));
    w.runFor(1000);
    const std::vector<uint8_t> first = w.captured(MsgType::CMD);
    t.mark();
    w.attackerSend(first);
    w.runFor(500);
    PROP("PRO-8", "La repeticion se rechaza: la orden no se ejecuta otra vez");
    EXPECT_EQ(t.count(Ev::APP_CMD), 0, "ejecuciones");

    w.gw().sendCommand(w.nodeId(0), kOther, kOtherLen);
    w.runFor(1000);
    t.mark();
    w.attackerSend(first);
    w.runFor(500);
    PROP("PRO-8", "Tambien se rechaza la repeticion de una orden que no es la ultima");
    EXPECT_EQ(t.count(Ev::APP_CMD), 0, "ejecuciones");

    // Sin ordenes durante mas de media escala del contador del gateway (unas 39 h con
    // sondeo cada 5 s y sincronizacion cada 30 s). La traza se vacia para no exportarla.
    for (int i = 0; i < 33000; ++i) {
        if (i % 100 == 0) {
            w.gw().sendPoll(w.nodeId(0));
            w.runFor(200);
        } else {
            w.gw().sendTimeSync(1000);
            w.runFor(60);
        }
    }
    t.clear();
    t.mark();
    w.gw().sendCommand(w.nodeId(0), kOrder, sizeof(kOrder));
    w.runFor(1000);
    PROP("PRO-8", "Una orden nueva se ejecuta tras mas de media escala del contador sin ordenes");
    EXPECT_EQ(countWithData(t, Ev::APP_CMD, kOrder, sizeof(kOrder)), 1, "ejecuciones de la orden nueva");
}
