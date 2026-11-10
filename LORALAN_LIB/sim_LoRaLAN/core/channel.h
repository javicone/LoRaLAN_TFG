#pragma once
// Canal virtual e inyeccion de fallos. Su modelo se declara en spec/modelo_canal.md.

#include <map>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "trace.h"

constexpr int kGatewayRadio = 0;
constexpr int kAttackerRadio = 99;

enum class Dir : uint8_t { UP, DOWN, ATTACKER };

enum class FaultAction : uint8_t { DROP, DELAY, DUPLICATE, REORDER, CORRUPT };

// Regla declarada por el caso: actua sobre la aparicion nth de las tramas de ese
// tipo y sentido, contadas desde que se anade la regla (nth = 0: sobre todas).
struct Fault {
    FaultAction action;
    MsgType type;
    Dir dir;
    unsigned nth;
    uint32_t param;     // DELAY/DUPLICATE: ms; CORRUPT: indice del byte alterado
    unsigned seen;
    unsigned fired;
};

inline Fault Drop(MsgType t, Dir d, unsigned nth) { return {FaultAction::DROP, t, d, nth, 0, 0, 0}; }
inline Fault Delay(MsgType t, Dir d, unsigned nth, uint32_t ms) { return {FaultAction::DELAY, t, d, nth, ms, 0, 0}; }
inline Fault Duplicate(MsgType t, Dir d, unsigned nth, uint32_t gapMs) { return {FaultAction::DUPLICATE, t, d, nth, gapMs, 0, 0}; }
// Retiene la trama y la entrega justo despues de la siguiente trama entregada.
inline Fault Reorder(MsgType t, Dir d, unsigned nth) { return {FaultAction::REORDER, t, d, nth, 0, 0, 0}; }
inline Fault Corrupt(MsgType t, Dir d, unsigned nth, uint32_t byteIndex) { return {FaultAction::CORRUPT, t, d, nth, byteIndex, 0, 0}; }

struct ChannelConfig {
    uint8_t spreadingFactor = 7;
    uint32_t fixedAirtimeMs = 0;  // != 0: sustituye a la formula (p. ej. airtime medido)
    uint32_t cadBlindMs = 0;      // una trama no es detectable hasta W ms despues de empezar
    int lossPct = 0;              // perdida por defecto de cada enlace
};

struct Delivery {
    std::vector<uint8_t> bytes;
    int from = 0;
};

class Channel {
public:
    Channel(Trace& trace, const ChannelConfig& config, uint32_t seed);

    void addFault(const Fault& fault) { faults_.push_back(fault); }
    std::vector<std::string> unfiredFaults() const;

    void setLoss(int fromRadio, int toRadio, int pct) { linkLoss_[{fromRadio, toRadio}] = pct; }
    void setDefaultLoss(int pct) { config_.lossPct = pct; }

    uint32_t airtimeMs(size_t frameLen) const;

    // Pone la trama en el aire y devuelve cuando deja de ocuparlo.
    uint32_t transmit(int radio, const uint8_t* data, size_t len, uint32_t now);
    int scan(int radio, uint32_t now);
    // Emisor ajeno a la red: ocupa el canal y nadie recibe nada.
    void occupy(uint32_t now, uint32_t durationMs);

    std::vector<Delivery> popDue(uint32_t now);
    bool linkLost(int fromRadio, int toRadio);
    bool airEmpty() const { return air_.empty() && delayed_.empty(); }

private:
    struct AirFrame {
        std::vector<uint8_t> bytes;
        int radio = 0;
        uint32_t startMs = 0;
        uint32_t endMs = 0;
        uint8_t type = 0;
        uint16_t seq = 0;
        bool noise = false;
        bool collided = false;
        bool drop = false;
        bool hold = false;
        bool corrupt = false;
        uint32_t corruptIndex = 0;
        bool delay = false;
        uint32_t delayMs = 0;
        bool duplicate = false;
        uint32_t duplicateGapMs = 0;
    };
    struct Delayed {
        uint32_t atMs;
        Delivery delivery;
    };

    void applyFaults(AirFrame& frame, Dir dir, uint32_t now);

    Trace& trace_;
    ChannelConfig config_;
    std::mt19937 rng_;   // separado del de la biblioteca: la perdida no altera sus sorteos
    std::vector<Fault> faults_;
    std::map<std::pair<int, int>, int> linkLoss_;
    std::vector<AirFrame> air_;
    std::vector<Delayed> delayed_;
    std::vector<Delivery> held_;
};
