// Familia SEGURIDAD. Lo esperado sale de RF10, RF11 y RF12 (capitulo 3).
// El cifrado del simulador es ficticio: se comprueba el uso de claves y contadores,
// nunca la solidez de las primitivas.

#include "common.h"

CASE("A1", "SEGURIDAD", "Repetir un PUSH capturado", "2026-11-01", "Evento ya entregado; atacante con la trama", "Reinyeccion") {
    World w(ctx);
    w.joinAll();
    w.node(0).queuePushEvent(kEvent, kEventLen, true);
    w.runFor(500);
    const std::vector<uint8_t> captured = w.captured(MsgType::PUSH_EVENT);
    Trace& t = w.trace();
    t.mark();
    w.attackerSend(captured);
    w.runFor(500);
    PROP("PRO-8", "Sin doble entrega y con el ACK reenviado");
    EXPECT_EQ(t.count(Ev::APP_PUSH), 0, "entregas de la repeticion");
    EXPECT_EQ(t.count(Ev::TX, MsgType::ACK, kGatewayRadio), 1, "ACK reenviados");
    PROP("PRO-8", "Un evento nuevo posterior se acepta");
    EXPECT_TRUE(sessionWorks(w), "un evento llega");
}

CASE("A2", "SEGURIDAD", "Alterar la carga o la cabecera", "2026-11-01", "Dos nodos; atacante con una trama legitima",
     "Reinyeccion con un byte alterado") {
    World w(ctx, 2);
    w.joinAll();
    w.node(0).queuePushEvent(kEvent, kEventLen, false);
    w.runFor(300);
    std::vector<uint8_t> payloadChanged = w.captured(MsgType::PUSH_EVENT);
    std::vector<uint8_t> sourceChanged = payloadChanged;
    payloadChanged[LORALAN_HEADER_SIZE] ^= 0x01;
    sourceChanged[3] = w.nodeId(1);
    Trace& t = w.trace();
    t.mark();
    w.attackerSend(payloadChanged);
    w.runFor(300);
    w.attackerSend(sourceChanged);
    w.runFor(300);
    PROP("PRO-8", "Las dos tramas se rechazan por el MIC, sin efecto alguno");
    EXPECT_EQ(t.drops(DropReason::BAD_MIC), 2, "descartes por MIC");
    EXPECT_EQ(t.count(Ev::APP_PUSH), 0, "entregas");
    EXPECT_EQ(t.count(Ev::TX, MsgType::ACK, kGatewayRadio), 0, "ACK");
    PROP("PRO-8", "La sesion sigue intacta");
    EXPECT_TRUE(sessionWorks(w), "un evento llega");
}

CASE("A3", "SEGURIDAD", "Devolver al nodo su propio ACK", "2026-11-01",
     "Evento perdido cuya secuencia coincide con la de un ACK capturado del nodo", "Reinyeccion del ACK") {
    World w(ctx);
    w.joinAll();
    w.gw().sendCommand(w.nodeId(0), kEvent, kEventLen);
    w.runFor(500);
    const std::vector<uint8_t> ownAck = w.captured(MsgType::ACK);
    Trace& t = w.trace();
    t.mark();
    w.inject(Drop(MsgType::PUSH_EVENT, Dir::UP, 0));
    w.node(0).queuePushEvent(kEvent, kEventLen, true);
    w.runFor(150);
    const Event* tx = t.first(Ev::TX, MsgType::PUSH_EVENT);
    LoRaLANFrame ack;
    const bool parsed = LoRaLANPacket::parse(ownAck.data(), static_cast<uint16_t>(ownAck.size()), ack);
    if (!parsed || ack.header.srcId != w.nodeId(0) || tx == nullptr || tx->seq != ack.header.seq) {
        ctx.addNotProduced("precondicion: la secuencia del evento no coincide con la del ACK capturado");
    }
    w.attackerSend(ownAck);
    w.runFor(kExhaustMs + 500);
    PROP("PRO-8", "El nodo no da por entregado un evento que el gateway no recibio");
    EXPECT_EQ(t.count(Ev::APP_PUSH), 0, "entregas");
    EXPECT_EQ(countOutcome(t, 1), 0, "confirmaciones falsas");
    EXPECT_EQ(countOutcome(t, 0), 1, "avisos de no entregado");
}

CASE("A4", "SEGURIDAD", "Agotar el contador en una sesion", "2026-11-01", "Un nodo asociado, airtime fijo de 1 ms",
     "65 536 eventos en una sesion") {
    WorldConfig cfg;
    cfg.channel.fixedAirtimeMs = 1;
    World w(ctx, 1, cfg);
    w.joinAll();
    Trace& t = w.trace();
    std::vector<uint8_t> seen(65536, 0);
    std::vector<uint8_t> firstFrame;
    int repeated = 0, sent = 0;
    bool sameCipher = false;
    for (int i = 0; i <= 65536; ++i) {
        if (!w.node(0).queuePushEvent(kEvent, kEventLen, false)) continue;
        w.runFor(3);
        const std::vector<uint8_t> frame = w.captured(MsgType::PUSH_EVENT);
        if (frame.size() < LORALAN_HEADER_SIZE + kEventLen) continue;
        ++sent;
        const uint16_t seq = static_cast<uint16_t>(frame[5] | (frame[6] << 8));
        if (firstFrame.empty()) firstFrame = frame;
        else if (seen[seq] != 0) {
            ++repeated;
            if (seq == static_cast<uint16_t>(firstFrame[5] | (firstFrame[6] << 8)) &&
                std::memcmp(frame.data() + LORALAN_HEADER_SIZE, firstFrame.data() + LORALAN_HEADER_SIZE, kEventLen) == 0) {
                sameCipher = true;
            }
        }
        seen[seq] = 1;
        if ((i & 0xFFF) == 0) t.clear();   // la traza de 65 536 eventos no cabe entera
    }
    PROP("PRO-8", "No se reutiliza el flujo de cifrado dentro de una sesion");
    KNOWN_FAIL("A4: el contador da la vuelta sin renovar la clave");
    EXPECT_EQ(sent, 65537, "eventos emitidos");
    EXPECT_EQ(repeated, 0, "numeros de secuencia repetidos en la sesion");
    EXPECT_TRUE(!sameCipher, "el mismo texto no debe cifrarse igual dos veces");

    // Una captura vuelve a parecer nueva tras media escala del contador del gateway.
    World w2(ctx);
    w2.joinAll();
    Trace& t2 = w2.trace();
    w2.gw().sendCommand(w2.nodeId(0), kOther, kOtherLen);
    w2.runFor(1000);
    const std::vector<uint8_t> order = w2.captured(MsgType::CMD);
    for (int i = 0; i < 33000; ++i) {
        if (i % 100 == 0) {
            w2.gw().sendPoll(w2.nodeId(0));
            w2.runFor(200);
        } else {
            w2.gw().sendTimeSync(1000);
            w2.runFor(60);
        }
    }
    t2.clear();
    t2.mark();
    w2.attackerSend(order);
    w2.runFor(500);
    PROP("PRO-8", "Una orden capturada no se ejecuta otra vez tras media escala del contador");
    KNOWN_FAIL("A4: sin renovar la clave, la proteccion dura media escala del contador");
    EXPECT_EQ(t2.count(Ev::APP_CMD), 0, "ejecuciones de la captura");
}

CASE("A5", "SEGURIDAD", "Tramas truncadas, longitudes extremas y tipos desconocidos", "2026-11-01",
     "Un nodo asociado; atacante con una trama legitima", "Reinyeccion de tramas malformadas") {
    World w(ctx);
    w.joinAll();
    w.node(0).queuePushEvent(kEvent, kEventLen, false);
    w.runFor(300);
    const std::vector<uint8_t> valid = w.captured(MsgType::PUSH_EVENT);
    std::vector<std::vector<uint8_t>> frames;
    for (size_t n : {1u, 5u, 10u}) frames.emplace_back(valid.begin(), valid.begin() + static_cast<long>(n));
    frames.emplace_back(76, 0x41);
    frames.emplace_back(200, 0x41);
    std::vector<uint8_t> unknownType = valid;
    unknownType[0] = static_cast<uint8_t>((unknownType[0] & 0xC3) | (0x0F << 2));
    frames.push_back(unknownType);
    std::vector<uint8_t> otherVersion = valid;
    otherVersion[0] = static_cast<uint8_t>((otherVersion[0] & 0x3F) | 0x80);
    frames.push_back(otherVersion);

    Trace& t = w.trace();
    t.mark();
    for (const std::vector<uint8_t>& f : frames) {
        w.attackerSend(f);
        w.runFor(1500);
    }
    PROP("PRO-8", "Todas se descartan como malformadas, sin caida ni respuesta");
    EXPECT_EQ(t.drops(DropReason::MALFORMED), frames.size() * 2, "descartes (gateway y nodo)");
    EXPECT_EQ(t.count(Ev::TX, kGatewayRadio), 0, "emisiones del gateway");
    EXPECT_EQ(t.count(Ev::TX, World::dev(0)), 0, "emisiones del nodo");
    EXPECT_EQ(t.count(Ev::APP_PUSH), 0, "entregas");
    PROP("PRO-8", "El estado no cambia");
    EXPECT_TRUE(w.node(0).isJoined(), "asociado");
    EXPECT_EQ(w.gw().nodeCount(), 1, "nodos en la tabla");
    EXPECT_TRUE(sessionWorks(w), "un evento llega");
}
