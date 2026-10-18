#include "RelayClientSocket.h"

#include "Alliance.h"
#include "DB.h"
#include "L2MobileNoti.h"
#include "Pledge.h"
#include "Relay.h"

#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/TextRuntime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace {

std::u16string SanitizeRelayChatForLog(const std::u16string& message) {
    // Current 0x00a2c328/0x00a2c5a8 iterates target UTF-16 code units. A
    // comma becomes a space. A backspace-delimited segment is copied only
    // while ESC toggles its inner lane on. The opening ESC is itself copied,
    // while the closing ESC is not.
    std::array<char16_t, 1024> sanitized{};
    std::size_t input = 0;
    std::size_t output = 0;
    while (input < message.size()) {
        char16_t value = message[input];
        if (value == u',') {
            sanitized[output++] = u' ';
        } else if (value != u'\b') {
            sanitized[output++] = value;
        } else {
            bool copy = false;
            for (;;) {
                ++input;
                value = input < message.size() ? message[input] : u'\0';
                if (value == u'\b' || value == u'\0') {
                    break;
                }
                if (value == u'\x1b') {
                    copy = !copy;
                }
                if (copy) {
                    sanitized[output++] = value;
                }
            }
        }
        ++input;
    }
    return std::u16string(sanitized.data());
}

void WriteRelayChatLog(
    const int logId, const int characterId, const std::u16string& characterName, const std::u16string& message
) {
    const std::u16string sanitized = SanitizeRelayChatForLog(message);
    CLogFormatter formatter;
    // formatter.WriteChatLog(
    //     logId,
    //     static_cast<std::int64_t>(characterId),
    //     0,
    //     0,
    //     0,
    //     0,
    //     sanitized.data(),
    //     actorName.data(),
    //     const_cast<wchar_t*>(L""))
    formatter.WriteChatLog(logId, static_cast<std::int64_t>(characterId), 0, 0, 0, 0, sanitized, characterName.c_str(), u"");
}

} // namespace

bool DummyRelayServerPacket(RelayClientSocket*, unsigned char*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     "DummyPacket called in RelayClient")
    // L2_LOG_ERROR(Log,
    //     "DummyPacket called in RelayClient")
    L2_LOG_WARN(Log, "Relay packet reached the fallback handler; it returns without processing the packet.");
    return false;
}

bool PacketTestFunc(RelayClientSocket*, unsigned char*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     "Receive RELAYSC_TEST packet")
    // L2_LOG_ERROR(Log,
    //     "Receive RELAYSC_TEST packet")
    L2_LOG_INFO(Log, "Relay test packet received.");
    return false;
}

bool PacketGiveItemFunc(RelayClientSocket*, unsigned char* const packetBytes) {
    // Current L2Server 0x00a2c158.
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     "PacketGiveItemFunc called in RelayClient")
    // Noise: Entering the relay gift handler is not an error or proof that the gift was delivered. The existing
    // request/result paths follow. L2_LOG_ERROR(Log,
    //     "PacketGiveItemFunc called in RelayClient")

    ReadBufferL2MobileNoti::ML2_GIVE_POST packet{};
    static_cast<void>(ReadBufferL2MobileNoti::Read_ML2_GIVE_POST(packet, packetBytes, 3, 0x400));
    if (packet.mailTitle.size() < 0x41 && packet.mailBody.size() < 0x81 && packet.charDBId > 0 && packet.itemAmount > 0 &&
        packet.itemTemplateId > 0) {
        db.RequestWritePostBirth(
            packet.charDBId, packet.mailTitle.c_str(), packet.mailBody.c_str(), packet.itemTemplateId, 0x0e, static_cast<int>(packet.itemAmount),
            0, packet.presentId
        );
    } else {
        RelayClient::Inst()->SendGivePostResult(
            packet.presentId, packet.charDBId, packet.itemTemplateId, static_cast<int>(packet.itemAmount), -1
        );
    }
    return false;
}

bool PacketSayPledgeFunc(RelayClientSocket*, unsigned char* const packetBytes) {
    // Current L2Server 0x00a2c328.
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     "[NO_ERROR]PacketSayPledgeFunc called in RelayClient")
    // Noise: Routine relay clan-chat receipt is already recorded by WriteRelayChatLog. This extra error trace adds no
    // outcome. L2_LOG_ERROR(Log,
    //     "[NO_ERROR]PacketSayPledgeFunc called in RelayClient")

    ReadBufferL2MobileNoti::ML2_SAY_PLEDGE packet{};
    static_cast<void>(ReadBufferL2MobileNoti::Read_ML2_SAY_PLEDGE(packet, packetBytes, 3, 0x400));
    WriteRelayChatLog(SPT_PLEDGE, packet.charId, packet.charName, packet.chatMsg);

    CSPointer<CPledge> pledge = CPledge::FindPledge(packet.worldId, packet.pledgeId);
    if (pledge) {
        pledge->SayByMobile(packet.worldId, packet.charId, packet.charName.c_str(), packet.chatMsg.c_str(), SPT_PLEDGE);
    }
    return false;
}

bool PacketSayAllianceFunc(RelayClientSocket*, unsigned char* const packetBytes) {
    // Current L2Server 0x00a2c5a8.
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     "[NO_ERROR]PacketSayAllianceFunc called in RelayClient")
    // Noise: Routine relay alliance-chat receipt is already recorded by WriteRelayChatLog. This extra error trace adds no
    // outcome. L2_LOG_ERROR(Log,
    //     "[NO_ERROR]PacketSayAllianceFunc called in RelayClient")

    ReadBufferL2MobileNoti::ML2_SAY_ALLIANCE packet{};
    static_cast<void>(ReadBufferL2MobileNoti::Read_ML2_SAY_ALLIANCE(packet, packetBytes, 3, 0x400));
    WriteRelayChatLog(SPT_ALLIANCE, packet.charId, packet.charName, packet.chatMsg);

    CSPointer<CPledge> pledge = CPledge::FindPledge(packet.worldId, packet.pledgeId);
    CAlliance* const alliance = pledge ? pledge->GetAlliance() : nullptr;
    if (alliance != nullptr) {
        alliance->SayByMobile(packet.worldId, packet.charId, packet.charName.c_str(), packet.chatMsg.c_str(), SPT_ALLIANCE);
    }
    return false;
}

RelayServerPacketMap g_RelayServerPacketTable{DummyRelayServerPacket, u""};
L2_BIND_PACKET(IsRegistered_RELAYSC_TEST, g_RelayServerPacketTable, 0, PacketTestFunc);
L2_BIND_PACKET(IsRegistered_RELAYSC_GIVE_ITEM, g_RelayServerPacketTable, 1, PacketGiveItemFunc);
L2_BIND_PACKET(IsRegistered_RELAYSC_SAY_PLEDGE, g_RelayServerPacketTable, 2, PacketSayPledgeFunc);
L2_BIND_PACKET(IsRegistered_RELAYSC_SAY_ALLIANCE, g_RelayServerPacketTable, 3, PacketSayAllianceFunc);
