#include "PledgeBBS.h"

#include "AllianceDb.h"
#include "CacheSocket.h"
#include "Pledge.h"
#include "PledgeDb.h"

#include "L2Core/src/inc/Guard.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/IOCPacket.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace {

constexpr int kPledgeBbsReplyOpcode = 0xb0;

} // namespace

CPledgeBBS::CPledgeBBS() { L2_GUARD_FUNCTION(); }

CPledgeBBS::~CPledgeBBS() { L2_GUARD_FUNCTION(); }

bool CPledgeBBS::GetClanList(int, int, int, const std::uint8_t*, CSocket*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool CPledgeBBS::GetClanIntro(int, int, int, const std::uint8_t*, CSocket*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool CPledgeBBS::GetClanInfo(int characterId, int userServerId, int option, const std::uint8_t* packet, CSocket* socket) {
    L2_GUARD_FUNCTION();

    int requestToken = 0;
    int requestedPledgeId = 0;
    ::Disassemble(packet, "dd", &requestToken, &requestedPledgeId);

    std::uint32_t pledgeId = 0;
    std::uint32_t rulerId = 0;
    int allianceId = 0;
    int skillLevel = 0;
    std::uint32_t memberCount = 0;
    std::array<char16_t, 64> rulerName{};
    std::array<char16_t, 64> pledgeName{};
    std::array<char16_t, 64> allianceName{};

    CPledgeSP pledge = g_pledgeDb.GetPledge(requestedPledgeId, false, runtime::process::CurrentDatabaseId());
    if (pledge) {
        pledgeId = pledge->GetId();
        rulerId = pledge->rullerId();
        std::copy_n(pledge->rullerName(), 24, rulerName.begin());
        std::copy_n(pledge->GetName(), 24, pledgeName.begin());
        allianceId = static_cast<int>(pledge->allianceId());
        skillLevel = pledge->skillLevel();
        memberCount = pledge->GetMemberCount();

        if (allianceId > 0) {
            CAllianceSP alliance = g_allianceDb.GetAlliance(
                static_cast<std::uint32_t>(allianceId), static_cast<std::uint32_t>(runtime::process::CurrentDatabaseId())
            );
            if (alliance) {
                std::copy_n(alliance->name(), 25, allianceName.begin());
            }
        }
    }

    socket->Send(
        "cdddddSdSdSdd", kPledgeBbsReplyOpcode, characterId, userServerId, option, requestToken, static_cast<int>(pledgeId),
        rulerName.data(), static_cast<int>(rulerId), pledgeName.data(), allianceId, allianceName.data(), skillLevel,
        static_cast<int>(memberCount)
    );
    return true;
}

bool CPledgeBBS::GetClanAdmin(int characterId, int userServerId, int option, const std::uint8_t* packet, CSocket* socket) {
    L2_GUARD_FUNCTION();

    int requestToken = 0;
    int requestedPledgeId = 0;
    ::Disassemble(packet, "dd", &requestToken, &requestedPledgeId);

    std::uint32_t pledgeId = 0;
    std::uint32_t rulerId = 0;
    int allianceId = 0;
    int skillLevel = 0;
    std::uint32_t memberCount = 0;
    std::array<char16_t, 64> rulerName{};
    std::array<char16_t, 64> pledgeName{};
    std::array<char16_t, 64> allianceName{};

    CPledgeSP pledge = g_pledgeDb.GetPledge(requestedPledgeId, false, runtime::process::CurrentDatabaseId());
    if (pledge) {
        pledgeId = pledge->GetId();
        rulerId = pledge->rullerId();
        std::copy_n(pledge->rullerName(), 24, rulerName.begin());
        std::copy_n(pledge->GetName(), 24, pledgeName.begin());
        allianceId = static_cast<int>(pledge->allianceId());
        skillLevel = pledge->skillLevel();
        memberCount = pledge->GetMemberCount();

        if (allianceId > 0) {
            CAllianceSP alliance = g_allianceDb.GetAlliance(
                static_cast<std::uint32_t>(allianceId), static_cast<std::uint32_t>(runtime::process::CurrentDatabaseId())
            );
            if (alliance) {
                std::copy_n(alliance->name(), 25, allianceName.begin());
            }
        }
    }

    socket->Send(
        "cdddddSdSdSdd", kPledgeBbsReplyOpcode, characterId, userServerId, option, requestToken, static_cast<int>(pledgeId),
        rulerName.data(), static_cast<int>(rulerId), pledgeName.data(), allianceId, allianceName.data(), skillLevel,
        static_cast<int>(memberCount)
    );
    return true;
}

bool CPledgeBBS::DoPledgeBBS(int characterId, int userServerId, int option, const std::uint8_t* packet, CSocket* socket) {
    L2_GUARD_FUNCTION();

    if (option == 1) {
        GetClanList(characterId, userServerId, 1, packet, socket);
    } else if (option == 2) {
        GetClanInfo(characterId, userServerId, 2, packet, socket);
    } else if (option == 3) {
        GetClanIntro(characterId, userServerId, 3, packet, socket);
    } else if (option == 4) {
        GetClanAdmin(characterId, userServerId, 4, packet, socket);
    }
    return true;
}
