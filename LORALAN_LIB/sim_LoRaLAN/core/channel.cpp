#include "channel.h"

#include <cmath>

#include "airtime.h"

namespace {
const char* actionName(FaultAction a) {
    switch (a) {
        case FaultAction::DROP: return "DROP";
        case FaultAction::DELAY: return "DELAY";
        case FaultAction::DUPLICATE: return "DUPLICATE";
        case FaultAction::REORDER: return "REORDER";
        case FaultAction::CORRUPT: return "CORRUPT";
    }
    return "?";
}
}  // namespace

Channel::Channel(Trace& trace, const ChannelConfig& config, uint32_t seed)
    : trace_(trace), config_(config), rng_(seed ^ 0x5A5A5A5Au) {}

std::vector<std::string> Channel::unfiredFaults() const {
    std::vector<std::string> out;
    for (const Fault& f : faults_) {
        if (f.fired == 0) {
            out.push_back(std::string(actionName(f.action)) + " " + typeName(static_cast<uint8_t>(f.type)) +
                          " aparicion " + std::to_string(f.nth));
        }
    }
    return out;
}

uint32_t Channel::airtimeMs(size_t frameLen) const {
    if (config_.fixedAirtimeMs != 0) return config_.fixedAirtimeMs;
    radio::LoRaParams params;
    params.spreadingFactor = config_.spreadingFactor;
    return static_cast<uint32_t>(std::ceil(radio::timeOnAirMs(static_cast<uint16_t>(frameLen), params)));
}

void Channel::applyFaults(AirFrame& frame, Dir dir, uint32_t now) {
    for (Fault& f : faults_) {
        if (static_cast<uint8_t>(f.type) != frame.type || f.dir != dir) continue;
        f.seen++;
        if (f.nth != 0 && f.seen != f.nth) continue;
        f.fired++;
        switch (f.action) {
            case FaultAction::DROP: frame.drop = true; break;
            case FaultAction::DELAY: frame.delay = true; frame.delayMs = f.param; break;
            case FaultAction::DUPLICATE: frame.duplicate = true; frame.duplicateGapMs = f.param; break;
            case FaultAction::REORDER: frame.hold = true; break;
            case FaultAction::CORRUPT: frame.corrupt = true; frame.corruptIndex = f.param; break;
        }
        Event e;
        e.t = now;
        e.kind = Ev::FAULT;
        e.dev = frame.radio;
        e.type = frame.type;
        e.seq = frame.seq;
        e.detail = actionName(f.action);
        trace_.add(std::move(e));
    }
}

uint32_t Channel::transmit(int radio, const uint8_t* data, size_t len, uint32_t now) {
    AirFrame frame;
    frame.bytes.assign(data, data + len);
    frame.radio = radio;
    frame.startMs = now;
    frame.endMs = now + airtimeMs(len);
    const uint32_t endMs = frame.endMs;

    Event e;
    e.t = now;
    e.kind = Ev::TX;
    e.dev = radio;
    e.len = static_cast<uint8_t>(len);
    LoRaLANFrame parsed;
    if (LoRaLANPacket::parse(data, static_cast<uint16_t>(len), parsed)) {
        frame.type = parsed.header.type;
        frame.seq = parsed.header.seq;
        e.type = parsed.header.type;
        e.seq = parsed.header.seq;
        e.flags = parsed.header.flags;
    }
    trace_.add(std::move(e));

    const Dir dir = radio == kAttackerRadio ? Dir::ATTACKER : (radio == kGatewayRadio ? Dir::DOWN : Dir::UP);
    applyFaults(frame, dir, now);

    // Una trama que acaba en este mismo milisegundo ya no solapa.
    for (AirFrame& other : air_) {
        if (other.radio != radio && other.endMs > frame.startMs && other.startMs < frame.endMs) {
            other.collided = true;
            frame.collided = true;
        }
    }
    air_.push_back(std::move(frame));
    return endMs;
}

int Channel::scan(int radio, uint32_t now) {
    bool busy = false;
    for (const AirFrame& f : air_) {
        if (f.startMs > now) continue;   // emision encolada que aun no ha empezado
        if (f.endMs > now && (f.noise || now - f.startMs >= config_.cadBlindMs)) busy = true;
    }
    Event e;
    e.t = now;
    e.kind = busy ? Ev::CAD_BUSY : Ev::CAD_FREE;
    e.dev = radio;
    trace_.add(std::move(e));
    return busy ? RADIOLIB_PREAMBLE_DETECTED : RADIOLIB_CHANNEL_FREE;
}

void Channel::occupy(uint32_t now, uint32_t durationMs) {
    AirFrame noise;
    noise.radio = kAttackerRadio;
    noise.startMs = now;
    noise.endMs = now + durationMs;
    noise.noise = true;
    for (AirFrame& other : air_) {
        if (other.endMs > now) {
            other.collided = true;
            noise.collided = true;
        }
    }
    air_.push_back(std::move(noise));
}

std::vector<Delivery> Channel::popDue(uint32_t now) {
    std::vector<Delivery> out;
    std::vector<Delivery> newlyHeld;

    for (size_t i = 0; i < air_.size();) {
        if (air_[i].endMs > now) {
            ++i;
            continue;
        }
        AirFrame f = std::move(air_[i]);
        air_.erase(air_.begin() + static_cast<long>(i));

        if (f.noise) continue;
        if (f.collided) {
            Event e;
            e.t = now;
            e.kind = Ev::COLLISION;
            e.dev = f.radio;
            e.type = f.type;
            e.seq = f.seq;
            trace_.add(std::move(e));
            continue;
        }
        if (f.drop) continue;

        Delivery d{f.bytes, f.radio};
        if (f.corrupt && f.corruptIndex < d.bytes.size()) d.bytes[f.corruptIndex] ^= 0x01;
        if (f.duplicate) delayed_.push_back({now + f.duplicateGapMs, d});
        if (f.hold) newlyHeld.push_back(std::move(d));
        else if (f.delay) delayed_.push_back({now + f.delayMs, std::move(d)});
        else out.push_back(std::move(d));
    }

    for (size_t i = 0; i < delayed_.size();) {
        if (delayed_[i].atMs > now) {
            ++i;
            continue;
        }
        out.push_back(std::move(delayed_[i].delivery));
        delayed_.erase(delayed_.begin() + static_cast<long>(i));
    }

    if (!out.empty() && !held_.empty()) {
        out.insert(out.end(), held_.begin(), held_.end());
        held_.clear();
    }
    held_.insert(held_.end(), newlyHeld.begin(), newlyHeld.end());
    return out;
}

bool Channel::linkLost(int fromRadio, int toRadio) {
    const auto it = linkLoss_.find({fromRadio, toRadio});
    const int pct = it != linkLoss_.end() ? it->second : config_.lossPct;
    return pct > 0 && static_cast<int>(rng_() % 100u) < pct;
}
