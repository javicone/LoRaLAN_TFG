#include "trace.h"

const char* evName(Ev kind) {
    switch (kind) {
        case Ev::TX: return "TX";
        case Ev::RX_OK: return "RX_OK";
        case Ev::RX_DROP: return "RX_DROP";
        case Ev::LOST: return "LOST";
        case Ev::FAULT: return "FAULT";
        case Ev::COLLISION: return "COLLISION";
        case Ev::CAD_FREE: return "CAD_FREE";
        case Ev::CAD_BUSY: return "CAD_BUSY";
        case Ev::STATE: return "STATE";
        case Ev::APP_JOIN_ACCEPTED: return "APP_JOIN_ACCEPTED";
        case Ev::APP_CMD: return "APP_CMD";
        case Ev::APP_PUSH_ACK: return "APP_PUSH_ACK";
        case Ev::APP_PUSH: return "APP_PUSH";
        case Ev::APP_POLL_RESP: return "APP_POLL_RESP";
        case Ev::APP_NODE_JOINED: return "APP_NODE_JOINED";
        case Ev::APP_NODE_LEFT: return "APP_NODE_LEFT";
        case Ev::APP_NODE_INACTIVE: return "APP_NODE_INACTIVE";
    }
    return "?";
}

const char* typeName(uint8_t type) {
    switch (static_cast<MsgType>(type)) {
        case MsgType::PUSH_EVENT: return "PUSH_EVENT";
        case MsgType::ACK: return "ACK";
        case MsgType::POLL: return "POLL";
        case MsgType::POLL_RESPONSE: return "POLL_RESPONSE";
        case MsgType::TIME_SYNC: return "TIME_SYNC";
        case MsgType::JOIN_REQUEST: return "JOIN_REQUEST";
        case MsgType::JOIN_ACCEPT: return "JOIN_ACCEPT";
        case MsgType::LEAVE: return "LEAVE";
        case MsgType::CMD: return "CMD";
    }
    return type == 0 ? "" : "DESCONOCIDO";
}

const char* reasonName(DropReason reason) {
    switch (reason) {
        case DropReason::NONE: return "";
        case DropReason::MALFORMED: return "MALFORMED";
        case DropReason::WRONG_NET: return "WRONG_NET";
        case DropReason::NOT_ADDRESSED: return "NOT_ADDRESSED";
        case DropReason::NO_KEY: return "NO_KEY";
        case DropReason::BAD_MIC: return "BAD_MIC";
        case DropReason::REJECTED_BY_ROLE: return "REJECTED_BY_ROLE";
    }
    return "?";
}

size_t Trace::count(Ev kind, int dev) const {
    size_t n = 0;
    for (size_t i = mark_; i < events_.size(); ++i) {
        const Event& e = events_[i];
        if (e.kind == kind && (dev < 0 || e.dev == dev)) ++n;
    }
    return n;
}

size_t Trace::count(Ev kind, MsgType type, int dev) const {
    size_t n = 0;
    for (size_t i = mark_; i < events_.size(); ++i) {
        const Event& e = events_[i];
        if (e.kind == kind && e.type == static_cast<uint8_t>(type) && (dev < 0 || e.dev == dev)) ++n;
    }
    return n;
}

size_t Trace::retries(MsgType type) const {
    size_t n = 0;
    for (size_t i = mark_; i < events_.size(); ++i) {
        const Event& e = events_[i];
        if (e.kind == Ev::TX && e.type == static_cast<uint8_t>(type) &&
            (e.flags & static_cast<uint8_t>(Flags::IS_RETRY)) != 0) {
            ++n;
        }
    }
    return n;
}

size_t Trace::drops(DropReason reason) const {
    size_t n = 0;
    for (size_t i = mark_; i < events_.size(); ++i) {
        if (events_[i].kind == Ev::RX_DROP && events_[i].reason == reason) ++n;
    }
    return n;
}

const Event* Trace::first(Ev kind) const {
    for (size_t i = mark_; i < events_.size(); ++i) {
        if (events_[i].kind == kind) return &events_[i];
    }
    return nullptr;
}

const Event* Trace::first(Ev kind, MsgType type) const {
    for (size_t i = mark_; i < events_.size(); ++i) {
        if (events_[i].kind == kind && events_[i].type == static_cast<uint8_t>(type)) return &events_[i];
    }
    return nullptr;
}

const Event* Trace::last(Ev kind) const {
    for (size_t i = events_.size(); i > mark_; --i) {
        if (events_[i - 1].kind == kind) return &events_[i - 1];
    }
    return nullptr;
}

std::vector<const Event*> Trace::select(Ev kind, int dev) const {
    std::vector<const Event*> out;
    for (size_t i = mark_; i < events_.size(); ++i) {
        const Event& e = events_[i];
        if (e.kind == kind && (dev < 0 || e.dev == dev)) out.push_back(&e);
    }
    return out;
}

std::vector<const Event*> Trace::select(Ev kind, MsgType type, int dev) const {
    std::vector<const Event*> out;
    for (size_t i = mark_; i < events_.size(); ++i) {
        const Event& e = events_[i];
        if (e.kind == kind && e.type == static_cast<uint8_t>(type) && (dev < 0 || e.dev == dev)) out.push_back(&e);
    }
    return out;
}

const char* Trace::csvHeader() { return "mundo,t_ms,evento,disp,tipo,seq,flags,long,motivo,valor,detalle\n"; }

std::string Trace::toCsv(int worldIndex) const {
    std::string out;
    for (const Event& e : events_) {
        out += std::to_string(worldIndex) + "," + std::to_string(e.t) + "," + evName(e.kind) + "," +
               std::to_string(e.dev) + "," + typeName(e.type) + "," + std::to_string(e.seq) + "," +
               std::to_string(e.flags) + "," + std::to_string(e.len) + "," + reasonName(e.reason) + "," +
               std::to_string(e.value) + "," + e.detail + "\n";
    }
    return out;
}
