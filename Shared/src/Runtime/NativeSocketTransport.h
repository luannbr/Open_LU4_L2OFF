#pragma once

#include <cstddef>
#include <cstdint>

namespace portable {

// Product-neutral native half of an accepted overlapped socket. Recovered
// socket owners keep their original class layout and packet logic. These two
// callbacks only return host receive/terminal completions to that owner.
using NativeSocketReadSink = bool (*)(void* owner, const std::uint8_t* data, std::size_t length);
using NativeSocketTerminalSink = void (*)(void* owner, int error);

bool BindNativeSocketTransport(
    void* owner, std::uint64_t nativeSocket, NativeSocketReadSink readSink, NativeSocketTerminalSink terminalSink
);

void StopNativeSocketTransports();

// Cold-path snapshot, consumed at most once per connection. Ages use a
// monotonic clock. -1 means that operation has not occurred. Sent bytes are
// accepted by the local OS, not always acknowledged by the remote peer.
struct NativeSocketDiagnostics {
    std::int64_t lifetimeMs = 0;
    std::int64_t lastReadAgeMs = -1;
    std::int64_t lastWriteAgeMs = -1;
    std::int64_t failureDispatchAgeMs = -1;
    std::uint64_t receivedBytes = 0;
    std::uint64_t sentBytes = 0;
    int firstNativeError = 0;
    bool writeFailure = false;
    int unsentNativeBytesAtFailure = 0;
};
// Expected closes do not consume the snapshot budget.
bool TakeNativeSocketDiagnostics(std::uint64_t socket, int error, NativeSocketDiagnostics& result);

} // namespace portable
