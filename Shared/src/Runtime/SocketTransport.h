#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>

namespace portable {

using SocketCloseSink = std::function<void()>;
using SocketGracefulCloseSink = std::function<void()>;
using SocketInitializeSink = std::function<void()>;
using SocketReadArmSink = std::function<bool(std::size_t)>;
using SocketWriteCompletion = std::function<void(bool, std::uint32_t, int)>;

enum class SocketWriteDisposition {
    Failed,
    Completed,
    Pending,
};

struct SocketWriteSubmission {
    SocketWriteDisposition disposition = SocketWriteDisposition::Failed;
    std::uint32_t transferred = 0;

    static SocketWriteSubmission Failed() noexcept { return {}; }

    static SocketWriteSubmission Completed(std::uint32_t transferred) noexcept {
        return {SocketWriteDisposition::Completed, transferred};
    }

    static SocketWriteSubmission Pending() noexcept { return {SocketWriteDisposition::Pending, 0}; }

    bool IsFailed() const noexcept { return disposition == SocketWriteDisposition::Failed; }

    bool IsCompleted() const noexcept { return disposition == SocketWriteDisposition::Completed; }
};

// Returns the native WSAStartup error code on Windows and zero on platforms
// that do not require process-wide socket initialization.
int InitializeSocketSubsystem();
void CleanupSocketSubsystem();

// A WinSock overlapped WSASend is either rejected immediately, completed
// synchronously, or accepted for a later IOCP completion. Two-argument
// callbacks remain useful for native sockets that complete synchronously.
// the production client transports use the three-argument pending form.
class SocketWriteSink {
public:
    SocketWriteSink() = default;
    SocketWriteSink(std::nullptr_t) noexcept {}
    SocketWriteSink(const SocketWriteSink&) = default;
    SocketWriteSink(SocketWriteSink&&) noexcept = default;
    SocketWriteSink& operator=(const SocketWriteSink&) = default;
    SocketWriteSink& operator=(SocketWriteSink&&) noexcept = default;

    template <typename Function>
        requires(!std::same_as<std::decay_t<Function>, SocketWriteSink>)
    SocketWriteSink(Function&& function) {
        Assign(std::forward<Function>(function));
    }

    explicit operator bool() const noexcept { return static_cast<bool>(function_); }

    SocketWriteSubmission operator()(const std::uint8_t* data, int length, SocketWriteCompletion completion) const {
        return function_ ? function_(data, length, std::move(completion)) : SocketWriteSubmission::Failed();
    }

private:
    template <typename Function>
    void Assign(Function&& function) {
        using Stored = std::decay_t<Function>;
        if constexpr (std::is_invocable_v<Stored&, const std::uint8_t*, int, SocketWriteCompletion>) {
            using Result = std::invoke_result_t<Stored&, const std::uint8_t*, int, SocketWriteCompletion>;
            function_ = [sink = Stored(std::forward<Function>(function))](
                            const std::uint8_t* data, int length, SocketWriteCompletion completion
                        ) mutable {
                if constexpr (std::is_void_v<Result>) {
                    sink(data, length, std::move(completion));
                    return SocketWriteSubmission::Pending();
                } else {
                    return sink(data, length, std::move(completion));
                }
            };
        } else {
            using Result = std::invoke_result_t<Stored&, const std::uint8_t*, int>;
            function_ = [sink = Stored(std::forward<Function>(function))](
                            const std::uint8_t* data, int length, SocketWriteCompletion
                        ) mutable {
                if constexpr (std::is_void_v<Result>) {
                    sink(data, length);
                    return SocketWriteSubmission::Completed(static_cast<std::uint32_t>(length));
                } else {
                    const int transferred = static_cast<int>(sink(data, length));
                    return transferred <= 0 ? SocketWriteSubmission::Failed()
                                            : SocketWriteSubmission::Completed(static_cast<std::uint32_t>(transferred));
                }
            };
        }
    }

    std::function<SocketWriteSubmission(const std::uint8_t* data, int length, SocketWriteCompletion completion)> function_;
};

// CIOSocketEx keeps the target object layout. The native
// socket/IOCP operation is therefore bound outside that object, keyed by its
// stable address. Packet assembly, admission, coalescing and buffer ownership
// remain inside the recovered CIOSocketEx implementation.
class SocketTransportBinding {
public:
    SocketTransportBinding() = default;
    SocketTransportBinding(
        const void* socket,
        SocketWriteSink writeSink,
        SocketCloseSink closeSink = {},
        SocketInitializeSink initializeSink = {},
        SocketGracefulCloseSink gracefulCloseSink = {},
        SocketReadArmSink readArmSink = {}
    );
    ~SocketTransportBinding();

    SocketTransportBinding(const SocketTransportBinding&) = delete;
    SocketTransportBinding& operator=(const SocketTransportBinding&) = delete;

    SocketTransportBinding(SocketTransportBinding&& other) noexcept;
    SocketTransportBinding& operator=(SocketTransportBinding&& other) noexcept;

    void Reset(
        const void* socket = nullptr,
        SocketWriteSink writeSink = {},
        SocketCloseSink closeSink = {},
        SocketInitializeSink initializeSink = {},
        SocketGracefulCloseSink gracefulCloseSink = {},
        SocketReadArmSink readArmSink = {}
    );

private:
    const void* socket_ = nullptr;
};

bool BindSocketTransport(
    const void* socket,
    SocketWriteSink writeSink,
    SocketCloseSink closeSink = {},
    SocketInitializeSink initializeSink = {},
    SocketGracefulCloseSink gracefulCloseSink = {},
    SocketReadArmSink readArmSink = {}
);
void UnbindSocketTransport(const void* socket);
// Native half of CIOSocketEx::Initialize: the target applies its socket
// options only after constructing and binding the final target object.
bool InitializeSocketTransport(const void* socket);
// Mirrors one outstanding WSARecv owned by CIOSocketEx::Read. The pending
// marker survives native transport close until the matching completion
// releases the target's IoRetain.
bool ArmSocketReadTransport(const void* socket, std::size_t capacity);
bool ConsumeSocketReadTransport(const void* socket);
std::size_t CancelSocketReadTransport(const void* socket);
// Portable replacement for the target's SO_LINGER/closesocket boundary.
// The registry entry is removed before the owner callback runs so a receive
// completion caused by that close can safely re-enter CIOSocketEx::Close.
void CloseSocketTransport(const void* socket);
void GracefulCloseSocketTransport(const void* socket);
SocketWriteSubmission WriteSocketTransport(
    const void* socket, const std::uint8_t* data, int length, SocketWriteCompletion completion
);

} // namespace portable
