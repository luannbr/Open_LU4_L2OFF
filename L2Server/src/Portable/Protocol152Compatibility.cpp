#include "Portable/ClientProtocolRuntime.h"

#include "L2Server/src/User.h"
#include "L2Server/src/UserPacketEx.h"
#include "L2Server/src/UserSocket.h"


#include <array>
#include <cstdint>

namespace runtime::client_protocol {
namespace {

using Callback = IncomingPacketCallback;

constexpr std::size_t kBasicSlotCount = 209;
constexpr std::size_t kP152ExtendedSlotCount = 0x163;

User* GetUser(CSocket* const baseSocket) noexcept {
    if (baseSocket == nullptr) {
        return nullptr;
    }
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    return socket->GetUser();
}

template <typename... TArgs>
bool DispatchConvertedPacket(const Callback callback, CSocket* const socket, const char* const format, TArgs... args) {
    std::array<std::uint8_t, 0x4000> converted{};
    const int length =
        ::Assemble(reinterpret_cast<char*>(converted.data()), static_cast<int>(converted.size()), format, args...);
    if (length < 0) {
        return false;
    }
    return callback(socket, converted.data());
}

bool JoinMpccRoomP152(CSocket* const socket, const std::uint8_t* const packet) {
    // Exact NA P152 Engine.dll RequestJoinMpccRoom@0x202d9530 writes
    // C_EX 0x05c as "chdd": room id and location. The current owner also
    // requires level and page, which did not exist in this P152 request.
    std::int32_t roomId = 0;
    std::int32_t location = 0;
    ::Disassemble(packet, "dd", &roomId, &location);
    return DispatchConvertedPacket(&UserPacketEx::ReplyJoinMPCCRoom, socket, "dddd", roomId, location, 0, 1);
}

bool RequestRegistBeautyP152(CSocket* const socket, const std::uint8_t* const packet) {
    // Exact NA P152 Engine.dll RequestRegistBeauty@0x202cb5b0 serializes
    // hair, face and color. The current owner expects the desired semantic
    // triplet followed by the per-state Hair/Face/Color item identifiers.
    std::int32_t hairId = 0;
    std::int32_t faceId = 0;
    std::int32_t colorId = 0;
    ::Disassemble(packet, "ddd", &hairId, &faceId, &colorId);
    return DispatchConvertedPacket(
        &UserPacketEx::RequestRegistBeauty, socket, "dddddd", faceId, hairId, colorId, hairId, faceId, colorId
    );
}

bool RequestResetBeautyP152(CSocket* const socket, const std::uint8_t* const packet) {
    // Exact NA P152 Engine.dll RequestResetBeauty@0x202cb460 has the same
    // three-field order as RequestRegistBeauty. The empty packet sharing
    // opcode 0x0cd is selected separately by its frame length.
    std::int32_t hairId = 0;
    std::int32_t faceId = 0;
    std::int32_t colorId = 0;
    ::Disassemble(packet, "ddd", &hairId, &faceId, &colorId);
    return DispatchConvertedPacket(
        &UserPacketEx::RequestResetBeauty, socket, "dddddd", faceId, hairId, colorId, hairId, faceId, colorId
    );
}

bool IgnorePledgeExtendedInfoP152(CSocket* const baseSocket, const std::uint8_t*) {
    // Exact NA P152 Engine.dll RequestPledgeExtendedInfo@0x202e9c50 sends
    // C 0x066 with a pledge name. The current product has only a rejecting
    // dummy in this slot and no matching owner, so compatibility accepts the
    // retired request without inventing a replacement response.
    return GetUser(baseSocket) == nullptr;
}

bool IgnoreChangeAbilityPointP152(CSocket* const baseSocket, const std::uint8_t*) {
    // Exact NA P152 Engine.dll RequestChangeAbilityPoint@0x202c9420 sends an
    // empty C_EX 0x0ec. The current product has no matching feature
    // owner in this slot.
    return GetUser(baseSocket) == nullptr;
}

bool IgnoreUnsupportedP152Packet(CSocket* const, const std::uint8_t*) {
    // Exact client code proves the P152 0x15e, 0x15f and 0x162 requests,
    // while the current product has no matching semantic owner. They must
    // not fall through to the different P162 handlers occupying these ids.
    return false;
}

const std::array<Callback, kBasicSlotCount>& BasicCallbacksP152() {
    static const std::array<Callback, kBasicSlotCount> callbacks = [] {
        std::array<Callback, kBasicSlotCount> result{};
        for (std::size_t index = 0; index < result.size(); ++index) {
            result[index] = gPacketTableFromClient.GetCallback(static_cast<int>(index));
        }

        // All other formerly overridden basic packets are byte-compatible
        // with the current callbacks in the exact NA client, or do not exist
        // in that client. Only this retired, current-dummy request differs.
        result[0x066] = &IgnorePledgeExtendedInfoP152;
        return result;
    }();
    return callbacks;
}

const std::array<Callback, kP152ExtendedSlotCount>& ExtendedCallbacksP152() {
    static const std::array<Callback, kP152ExtendedSlotCount> callbacks = [] {
        std::array<Callback, kP152ExtendedSlotCount> result{};
        result.fill(&IgnoreUnsupportedP152Packet);
        for (std::size_t index = 0; index < 352; ++index) {
            result[index] = g_UserPacketEx.GetCallback(static_cast<int>(index));
        }

        result[0x05c] = &JoinMpccRoomP152;
        result[0x0cb] = &RequestRegistBeautyP152;
        result[0x0cd] = &RequestResetBeautyP152;
        result[0x0ec] = &IgnoreChangeAbilityPointP152;
        result[0x15e] = &IgnoreUnsupportedP152Packet;
        result[0x15f] = &IgnoreUnsupportedP152Packet;
        result[0x160] = &UserPacketEx::RequestInteractModify;
        result[0x161] = &UserPacketEx::RequestTryEnchantArtifact;
        result[0x162] = &IgnoreUnsupportedP152Packet;
        return result;
    }();
    return callbacks;
}

} // namespace

IncomingPacketCallback ResolveBasicIncomingCallback(
    const CUserSocket* const socket, const int opcode, const IncomingPacketCallback currentCallback
) noexcept {
    if (NegotiatedRevision(socket) != kNorthAmericanRevision || static_cast<unsigned int>(opcode) >= kBasicSlotCount) {
        return currentCallback;
    }
    return BasicCallbacksP152()[static_cast<std::size_t>(opcode)];
}

IncomingPacketCallback ResolveExtendedIncomingCallback(
    const CUserSocket* const socket, const int opcode, const int payloadSize, const IncomingPacketCallback currentCallback
) noexcept {
    if (NegotiatedRevision(socket) != kNorthAmericanRevision ||
        static_cast<unsigned int>(opcode) >= kP152ExtendedSlotCount) {
        return currentCallback;
    }
    if (opcode == 0x0cd && payloadSize == 0) {
        // Exact NA P152 RequestShowResetShopList@0x202cb700 and
        // RequestResetBeauty@0x202cb460 share opcode 0x0cd. Only the latter
        // has its three-DWORD payload.
        return &UserPacketEx::RequestShowResetBeauty;
    }
    return ExtendedCallbacksP152()[static_cast<std::size_t>(opcode)];
}

int ExtendedPolicyOpcode(const CUserSocket* const socket, const int opcode) noexcept {
    if (NegotiatedRevision(socket) != kNorthAmericanRevision) {
        return opcode;
    }
    if (opcode == 0x160) {
        return 0x15e;
    }
    if (opcode == 0x161) {
        return 0x15f;
    }
    if (opcode == 0x162) {
        return 0;
    }
    return opcode;
}

} // namespace runtime::client_protocol
