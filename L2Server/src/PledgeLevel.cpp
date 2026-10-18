#include "PledgeLevel.h"
#include "L2Packet.h"
#include "ObjectDB.h"
#include "Pledge.h"
#include "PledgeLevelInfo.h"
#include "PledgeMastery.h"
#include "PledgeMemberService.h"
#include "PledgeStuff.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/AtomicJob.h"
#include "Shared/src/Log.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TextRuntime.h"
#include "User.h"
#include "UserPacketEx.h"
#include "UserSocket.h"

#include <array>
#include <cwchar>
#include <string>

namespace {


template <std::size_t Size, typename... Args>
std::u16string FormatPledgeLevelBuilderMessage(const std::u16string_view format, const Args&... args) {
    std::array<char16_t, Size> message{};
    FormatUtf16To(message.data(), message.size(), format, args...);
    return std::u16string(message.data());
}

void Send_S_EX_PLEDGE_LEVEL_UP(const User* const user, const char result) {
    L2_GUARD_FUNCTION();
    protocol::S_EX_PLEDGE_LEVEL_UP packet{};
    packet.nResult = result;
    PledgeStuff::SendPacket(user, S_EX_PLEDGE_LEVEL_UP, packet);
}

} // namespace

// static_assert(L2ServerMemory::kPledgeLevelClassId == 229)
static_assert(static_cast<std::int32_t>(ShippingMemoryClassId::PledgeLevel) == 229);

L2_DEFINE_OBJECT_CLASS_ID(PledgeLevel)

L2_DEFINE_OBJECT_CLASS_NAME(PledgeLevel, m_pClassName_PledgeLevel)

L2_DEFINE_OBJECT_SINGLE_ALLOCATOR(PledgeLevel, m_pClassName_PledgeLevel)

L2_DEFINE_OBJECT_RELEASE(PledgeLevel)

PledgeLevel::PledgeLevel() : m_TransactionLock(0x1e) {}

namespace PledgeLevelStuff {

int GetCostNameValue(const int pledgeLevel) {
    const PledgeLevelInfo* const info = PledgeLevelDB::inst().FindInfo(pledgeLevel);
    return info != nullptr ? info->Cost.name_value : 0;
}

bool RequestPledgeLevelUp(User* const user) {
    // Current L2Server 0x009b7988 / callback 0x009b7cb4.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeLevel.cpp",
        //     0x42,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeLevel.cpp",
        //     0x42,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot request a clan level increase: the user is unavailable.");
        return false;
    }
    if (!user->HavePledgePower(PP_CONTROL_SYSTEM_FUNC)) {
        user->SendSystemMessage(SystemMessageId::NO_AUTHORITY_FOR_CLAN_DEVELOPMENT);
        return false;
    }

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeLevel.cpp",
        //     0x4f)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeLevel.cpp",
        //     0x4f)
        L2_LOG_WARN(
            Log, "Cannot increase clan level for character %u: the clan is unavailable.",
            static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }

    JobTransactionLong jobType = 1;
    JobTransactionLock& transaction = pledge->GetLevel()->m_TransactionLock;
    if (!transaction.BeginJob(&jobType)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]To set job is failed: %d, %d",
        //     L"PledgeLevel.cpp",
        //     0x59,
        //     pledge->GetDbId(),
        //     static_cast<int>(jobType))
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]To set job is failed: %d, %d",
        //     L"PledgeLevel.cpp",
        //     0x59,
        //     pledge->GetDbId(),
        //     static_cast<int>(jobType))
        L2_LOG_WARN(
            Log, "Clan %u cannot start a level increase: transaction %d is already active.",
            static_cast<unsigned>(pledge->GetDbId()), static_cast<int>(jobType)
        );
        return false;
    }

    const PledgeLevelInfo* const info = PledgeLevelDB::inst().FindInfo(static_cast<int>(pledge->GetPledgeLevel()) + 1);
    if (info == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeLevel.cpp",
        //     0x61)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeLevel.cpp",
        //     0x61)
        L2_LOG_WARN(
            Log, "Clan %u has no next-level definition after level %d; the level-increase request is rejected.",
            static_cast<unsigned>(pledge->GetDbId()), static_cast<int>(pledge->GetPledgeLevel())
        );
        if (!transaction.EndJob()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Failed to end job at %s(%d)",
            //     L"PledgeLevel.cpp",
            //     0x55)
            // L2_LOG_ERROR(Log,
            //     L"Failed to end job at %s(%d)",
            //     L"PledgeLevel.cpp",
            //     0x55)
            L2_LOG_ERROR(Log, "Clan level-increase rejection found no active transaction during cleanup.");
        }
        return false;
    }

    PledgeStuff::PledgeCostHelper helper(user, pledge);
    helper.SetCost(info->Cost);
    PledgeStuff::Write_LOG_PLEDGE_LEVEL_UP(
        user, pledge, static_cast<int>(pledge->GetPledgeLevel()) + 1, PledgeStuff::ActionStep::Try
    );

    const UserCapture userCapture{user->Id(), user->m_data->GetWorldId(), static_cast<int>(user->GetDBID())};
    const PledgeCapture pledgeCapture{pledge->Id(), pledge->GetWorldId(), static_cast<int>(pledge->GetDbId())};
    helper.Do(MakeLambdaJob([userCapture, pledgeCapture, info, jobType](const int result) {
        static_cast<void>(info);
        static_cast<void>(jobType);
        CPledge* const owner = pledgeCapture.GetPledge();
        if (owner == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]No pledge for level up: %d",
            //     L"PledgeLevel.cpp",
            //     0x79,
            //     pledgeCapture.m_dbid)
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]No pledge for level up: %d",
            //     L"PledgeLevel.cpp",
            //     0x79,
            //     pledgeCapture.m_dbid)
            L2_LOG_ERROR(
                Log,
                "Clan %u is unavailable in the level-increase callback (cost-operation result %d); level update, reply and "
                "transaction cleanup are not reached.",
                static_cast<unsigned>(pledgeCapture.m_dbid), result
            );
            return;
        }

        User* const requester = userCapture.GetUser();
        Send_S_EX_PLEDGE_LEVEL_UP(requester, static_cast<char>(result));

        if (result == 0) {
            owner->SetPledgeLevel(owner->GetPledgeLevel() + 1, true);
            PledgeStuff::AddPrivilegeByLevelAndMastery(owner, true);
            owner->UpdateInfoWindowPacket();
            SystemMessage_d<NoCheck, User> levelMessage(
                SystemMessageId::CLAN_LEVEL_INCREASED_TO_VALUE, static_cast<int>(owner->GetPledgeLevel())
            );
            owner->ServeForAllMember(levelMessage, false);
            if (PledgeMasteryStuff::GetMaxMasteryPoint(owner) > 0) {
                ::SystemMessage<NoCheck, User> masteryMessage(SystemMessageId::CLAN_DEVELOPMENT_POINTS_GAINED);
                owner->ServeForAllMember(masteryMessage, false);
            }
            PledgeStuff::Write_LOG_PLEDGE_LEVEL_UP(
                requester, owner, static_cast<int>(owner->GetPledgeLevel()) + 1, PledgeStuff::ActionStep::Success
            );
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]Failed to level up: %d, %d",
            //     L"PledgeLevel.cpp",
            //     0x88,
            //     pledgeCapture.m_dbid,
            //     result)
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]Failed to level up: %d, %d",
            //     L"PledgeLevel.cpp",
            //     0x88,
            //     pledgeCapture.m_dbid,
            //     result)
            L2_LOG_ERROR(
                Log,
                "Clan %u level-increase cost operation returned result %d; the level is not changed by this callback and "
                "transaction cleanup follows.",
                static_cast<unsigned>(pledgeCapture.m_dbid), result
            );
        }

        JobTransactionLock& lock = owner->GetLevel()->m_TransactionLock;
        if (!lock.EndJob()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Failed to end job at %s(%d)",
            //     L"PledgeLevel.cpp",
            //     0x80)
            // L2_LOG_ERROR(Log,
            //     L"Failed to end job at %s(%d)",
            //     L"PledgeLevel.cpp",
            //     0x80)
            L2_LOG_ERROR(Log, "Clan level-increase callback found no active transaction during cleanup.");
        }
    }));
    return true;
}

namespace Builder {

void SendPledgeLevelInfoTo(CUserSocket* const socket, const CPledge* const pledge) {
    // Current L2Server.exe 0x009b8030.
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeLevel.cpp",
        //     0xa8,
        //     L"(pPledge != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeLevel.cpp",
        //     0xa8,
        //     L"(pPledge != NULL)")
        L2_LOG_ERROR(Log, "Cannot display clan-level information through the builder command: the clan is unavailable.");
        return;
    }

    CPledge* const mutablePledge = const_cast<CPledge*>(pledge);
    const PledgeLevelInfo* const info =
        PledgeLevelDB::inst().FindInfo(static_cast<int>(mutablePledge->GetPledgeLevel()) + 1);
    if (info == nullptr) {
        PledgeStuff::SendSystemMessage(socket, std::u16string(u"PledgeLevel is reached to max"));
        return;
    }

    PledgeStuff::SendSystemMessage(
        socket,
        FormatPledgeLevelBuilderMessage<128>(
            u"Next Level: %d, NameValue: %d/%d", info->level, mutablePledge->GetNameValue(), info->Cost.name_value
        )
    );
    PledgeStuff::SendSystemMessage(
        socket,
        FormatPledgeLevelBuilderMessage<256>(
            u"Item %s/%d", g_ObjectDB.GetName(info->Cost.item_class), static_cast<int>(info->Cost.item_amount)
        )
    );
}

} // namespace Builder

} // namespace PledgeLevelStuff
