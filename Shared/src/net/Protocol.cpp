// protocol.cpp  -  framing + dispatch implementation.
#include "Protocol.h"
#include "Shared/src/Log.h"

#include <utility>

namespace network {

// ---- framing ----------------------------------------------------------------
void BuildFrame(std::vector<uint8_t>& out, uint8_t opcode, const uint8_t* data, int dataLen) {
    int total = dataLen + 3; // 2-byte length + opcode + data
    out.clear();
    out.reserve(total);
    out.push_back((uint8_t)(total & 0xff));
    out.push_back((uint8_t)((total >> 8) & 0xff));
    out.push_back(opcode);
    for (int i = 0; i < dataLen; ++i) {
        out.push_back(data[i]);
    }
}

void SendPacket(const SendFn& send, uint8_t opcode, const uint8_t* data, int dataLen) {
    std::vector<uint8_t> frame;
    BuildFrame(frame, opcode, data, dataLen);
    Log.Add(LOG_NORMAL, "[proto] -> response opcode=0x%02x total=%d bytes", opcode, (int)frame.size());
    if (send) {
        send(frame.data(), (int)frame.size());
    }
}

// ---- per-connection framer --------------------------------------------------
void PacketReader::Feed(const uint8_t* d, int n) { buf_.insert(buf_.end(), d, d + n); }

bool PacketReader::Next(Frame& out) {
    if (bad_) {
        return false;
    }
    // Both target OnRead implementations validate the uint16 frame length as
    // soon as its two bytes are present. The opcode is needed only after a
    // valid body length guarantees a complete three-byte header.
    if (buf_.size() < 2) {
        return false;
    }
    int total = buf_[0] + buf_[1] * 256;
    int bodyLen = total - 2;

    if (bodyLen < 1 || bodyLen > max_body_length_) {
        Log.Add(LOG_WARN, "[proto] bad packet size %d (total=%d) — dropping stream", bodyLen, total);
        bad_ = true;
        return false;
    }
    if ((int)buf_.size() < total) {
        return false; // whole frame not arrived yet
    }

    uint8_t opcode = buf_[2];
    if (opcode >= opcode_count_) {
        Log.Add(LOG_WARN, "[proto] opcode 0x%02x out of range (callback slots=%d) — dropping", opcode, opcode_count_);
        bad_ = true;
        return false;
    }

    int dataLen = bodyLen - 1; // strip opcode
    out.opcode = opcode;
    out.data.assign(buf_.begin() + 3, buf_.begin() + 3 + dataLen);
    buf_.erase(buf_.begin(), buf_.begin() + total); // consume the frame
    return true;
}

// ---- registry ---------------------------------------------------------------
void HandlerRegistry::Register(int opcode, const std::string& name, Handler h) { map_[opcode] = {name, std::move(h)}; }
bool HandlerRegistry::Dispatch(const Frame& fr, const SendFn& send) {
    auto it = map_.find(fr.opcode);
    if (it == map_.end()) {
        Log.Add(LOG_WARN, "[proto] no handler for opcode 0x%02x (data %d bytes)", fr.opcode, (int)fr.data.size());
        return true;
    }
    Log.Add(
        LOG_NORMAL, "[proto] dispatch opcode=0x%02x -> %s (payload %d bytes)", fr.opcode, it->second.name.c_str(),
        (int)fr.data.size()
    );
    return it->second.fn(fr, send);
}
std::vector<std::pair<int, std::string>> HandlerRegistry::List() const {
    std::vector<std::pair<int, std::string>> v;
    for (auto& kv : map_) {
        v.emplace_back(kv.first, kv.second.name);
    }
    return v;
}
HandlerRegistry& Handlers() {
    static HandlerRegistry g;
    return g;
}

// ---- per-fd dispatcher ------------------------------------------------------
void Protocol::SetFrameSink(FrameSink sink) { frame_sink_ = std::move(sink); }

bool Protocol::OnData(int fd, const uint8_t* data, int n, const SendFn& send) {
    auto it = readers_.try_emplace(fd, opcode_count_, max_body_length_).first;
    PacketReader& r = it->second;
    if (r.bad()) {
        return false;
    }
    r.Feed(data, n);
    Frame fr;
    while (r.Next(fr)) {
        const bool closeRequested = frame_sink_ ? frame_sink_(fr, send) : registry_.Dispatch(fr, send);
        if (closeRequested) {
            return false;
        }
    }
    return !r.bad();
}
void Protocol::OnClose(int fd) { readers_.erase(fd); }

} // namespace network
