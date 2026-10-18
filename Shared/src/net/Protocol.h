// L2NPC and L2Server frames use little-endian byte order.
// [len_lo][len_hi][opcode][handler data]
// Total length = len_lo + len_hi * 256, including the two length bytes.
// Body length = total length - 2, from 1 to 0x4ffe bytes.
// The first body byte is the opcode. No checksum or length follows the data.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace network {

struct Frame {
    uint8_t opcode = 0;
    std::vector<uint8_t> data; // handler payload (length + opcode stripped)
};

// Send a raw byte buffer through the connection's socket transport.
using SendFn = std::function<void(const uint8_t*, int)>;

// A target callback returns true when CPacketEx must close its peer socket.
using Handler = std::function<bool(const Frame& req, const SendFn& send)>;
using FrameSink = std::function<bool(const Frame& req, const SendFn& send)>;

// ---- framing helpers --------------------------------------------------------
// Build a wire frame: [lo,hi,opcode][data], total = dataLen + 3.
void BuildFrame(std::vector<uint8_t>& out, uint8_t opcode, const uint8_t* data, int dataLen);
// Frame `data` (already-Assembled bytes) under `opcode` and push it through `send`.
void SendPacket(const SendFn& send, uint8_t opcode, const uint8_t* data, int dataLen);

// ---- per-connection stream framer ------------------------------------------
class PacketReader {
public:
    explicit PacketReader(int opcode_count = 200, int max_body_length = 0x4ffe) :
        opcode_count_(opcode_count), max_body_length_(max_body_length) {}

    void Feed(const uint8_t* d, int n);
    bool Next(Frame& out); // pop one complete, valid frame. False if none/incomplete
    bool bad() const { return bad_; }

private:
    std::vector<uint8_t> buf_;
    int opcode_count_ = 200;
    int max_body_length_ = 0x4ffe;
    bool bad_ = false;
};

// ---- opcode -> handler registry --------------------------------------------
class HandlerRegistry {
public:
    void Register(int opcode, const std::string& name, Handler h);
    // True is the callback's close-request result. A missing registered slot
    // is also fatal because the target callback map is complete.
    bool Dispatch(const Frame& fr, const SendFn& send);
    std::vector<std::pair<int, std::string>> List() const;

private:
    struct Entry {
        std::string name;
        Handler fn;
    };
    std::map<int, Entry> map_;
};

HandlerRegistry& Handlers(); // process-wide registry

// Per-fd dispatcher: accumulate bytes, split frames and dispatch.
class Protocol {
public:
    explicit Protocol(HandlerRegistry& registry = Handlers(), int opcode_count = 200, int max_body_length = 0x4ffe) :
        registry_(registry), opcode_count_(opcode_count), max_body_length_(max_body_length) {}

    // False means the target parser would put this socket into state 2 and
    // close it. The bad state remains sticky until OnClose, just as the
    // original CIOSocketEx instance does.
    void SetFrameSink(FrameSink sink);
    bool OnData(int fd, const uint8_t* data, int n, const SendFn& send);
    void OnClose(int fd);

private:
    std::map<int, PacketReader> readers_;
    HandlerRegistry& registry_;
    int opcode_count_ = 200;
    int max_body_length_ = 0x4ffe;
    FrameSink frame_sink_;
};

} // namespace network
