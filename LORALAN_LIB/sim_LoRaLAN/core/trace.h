#pragma once
// Traza del mundo simulado: lo unico sobre lo que se hacen aserciones.

#include <Arduino.h>
#include <RadioLib.h>

#include <string>
#include <vector>

#include "LoRaLAN.h"

enum class Ev : uint8_t {
    TX,               // trama emitida al aire
    RX_OK,            // el receptor la acepto
    RX_DROP,          // el receptor la descarto (ver reason)
    LOST,             // la perdida del enlace impidio que llegara a un receptor
    FAULT,            // una regla de inyeccion actuo sobre esta trama
    COLLISION,        // la trama se destruyo por solape
    CAD_FREE,
    CAD_BUSY,
    STATE,            // cambio de estado observado por los getters (detail, value)
    APP_JOIN_ACCEPTED,
    APP_CMD,
    APP_PUSH_ACK,     // value: 1 = delivered
    APP_PUSH,         // value: direccion del nodo origen
    APP_POLL_RESP,    // value: direccion del nodo origen
    APP_NODE_JOINED,  // value: direccion del nodo
    APP_NODE_LEFT,
    APP_NODE_INACTIVE
};

enum class DropReason : uint8_t {
    NONE,
    MALFORMED,        // no supera LoRaLANPacket::parse
    WRONG_NET,        // otro netId
    NOT_ADDRESSED,    // dirigida a otro dispositivo
    NO_KEY,           // el receptor no tiene sesion con el emisor
    BAD_MIC,
    REJECTED_BY_ROLE  // autentica, pero la logica del rol la rechaza
};

struct Event {
    uint32_t t = 0;
    Ev kind = Ev::TX;
    int dev = -1;           // radio: 0 = gateway, i + 1 = nodo i, 99 = atacante
    uint8_t type = 0;       // MsgType (0 si no aplica)
    uint16_t seq = 0;
    uint8_t flags = 0;
    uint8_t len = 0;        // longitud total de la trama o de los datos de aplicacion
    DropReason reason = DropReason::NONE;
    int value = 0;
    std::string detail;
    std::vector<uint8_t> data;   // datos de aplicacion ya descifrados
};

class Trace {
public:
    void add(Event e) { events_.push_back(std::move(e)); }
    const std::vector<Event>& events() const { return events_; }

    // Las consultas solo miran lo posterior a la ultima marca. Sirve para que
    // la asociacion previa de un caso no cuente en sus aserciones.
    void mark() { mark_ = events_.size(); }
    // Vacia la traza. Solo para casos muy largos, que no pueden guardarlo todo.
    void clear() { events_.clear(); mark_ = 0; }

    size_t count(Ev kind, int dev = -1) const;
    size_t count(Ev kind, MsgType type, int dev = -1) const;
    size_t retries(MsgType type) const;    // TX con el indicador IS_RETRY
    size_t drops(DropReason reason) const;
    const Event* first(Ev kind) const;
    const Event* first(Ev kind, MsgType type) const;
    const Event* last(Ev kind) const;
    std::vector<const Event*> select(Ev kind, int dev = -1) const;
    std::vector<const Event*> select(Ev kind, MsgType type, int dev = -1) const;

    std::string toCsv(int worldIndex) const;   // sin cabecera
    static const char* csvHeader();

private:
    std::vector<Event> events_;
    size_t mark_ = 0;
};

const char* evName(Ev kind);
const char* typeName(uint8_t type);
const char* reasonName(DropReason reason);
