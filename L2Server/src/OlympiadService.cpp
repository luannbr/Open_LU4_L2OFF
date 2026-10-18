#include "OlympiadService.h"
#include "Area.h"
#include "CConfig.h"
#include "CursedWeapon.h"
#include "DB.h"
#include "IOCPMessageServer.h"
#include "InstantZone.h"
#include "MatchingPointDB.h"
#include "NpcServer.h"
#include "Object.h"
#include "OlympiadMatch.h"
#include "OlympiadStuff.h"
#include "Pet.h"
#include "Pledge.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/Config.h"
#include "Shared/src/DXPacket.h"
#include "Shared/src/DateTime.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/TargetFormatting.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/ScopedLockGuards.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/UserConst.h"
#include "Shared/src/proto/DBPacket.h"
#include "User.h"
#include "UserBroadcaster.h"
#include "UserSocket.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t kOlympiadServiceSource[] = L"OlympiadService.cpp";

class OlympiadTimerRearmer final {
public:
    OlympiadTimerRearmer(CIOObject& owner, const std::int64_t delay, const int id) : owner_(owner), delay_(delay), id_(id) {}

    ~OlympiadTimerRearmer() { owner_.AddTimer(delay_, id_); }

    OlympiadTimerRearmer(const OlympiadTimerRearmer&) = delete;
    OlympiadTimerRearmer& operator=(const OlympiadTimerRearmer&) = delete;

private:
    CIOObject& owner_;
    std::int64_t delay_;
    int id_;
};

void AppendUtf8(std::u16string& output, const std::string& value) {
    const std::u16string converted = Utf8ToUtf16(value);
    output.append(std::u16string_view(converted));
}

std::u16string GetOlympiadUserNameBySID(const std::uint32_t sid) {
    std::u16string result;
    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(static_cast<int>(sid));
    if (!creature || !creature->IsPC()) {
        return result;
    }

    const char16_t* const name = creature->GetName();
    if (name != nullptr) {
        const std::u16string converted = std::u16string(name);
        result.assign(std::u16string_view(converted));
    }
    return result;
}

void SetUserNoblessData(CSPointer<User> user, const int noblessIndex) {
    // Current L2Server 0x008fbd9c.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0xcbf,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0xcbf,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_ERROR(Log, "Cannot update Olympiad nobless data: user is missing.");
        return;
    }

    user->m_data->SetNoblessIndex(noblessIndex);
    if (noblessIndex == -1) {
        user->m_data->SetNoblessType(0);
        user->m_data->SetHeroType(0);
    } else {
        const SharedNoblessInfo* const info = SharedNoblessInfo::GetInfo(noblessIndex);
        user->m_data->SetNoblessType(info->nNoblessType);
        user->m_data->SetHeroType(info->nHeroType);
    }
    user->ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_NOBLESS});
}

GameValidCondition CheckMatchingValidConditionAtWait(WOlympiadTeam& team) {
    CSPointer<WOlympiadMemberInfo> member = team.GetMember();
    if (!member) {
        return GVC_VALID_TEAM;
    }

    const GameValidCondition prepared = member->GetPrepareValidCondition();
    if (prepared != GVC_NONE && prepared != GVC_VALID_TEAM) {
        return prepared;
    }
    if (team.GetUserSID() == 0U || !g_UserMap.FindObject(static_cast<int>(team.GetUserSID()))) {
        return GVC_VALID_TEAM;
    }

    constexpr std::array<GameValidCondition, 3> checks{
        GVC_ALIVE,
        GVC_NOT_CURSED,
        GVC_SUBJOB,
    };
    for (const GameValidCondition condition : checks) {
        if (!team.IsValidCondition(condition)) {
            return condition;
        }
    }
    return GVC_NONE;
}

bool CheckAndClosePrevSocket(const std::uint32_t accountId, const int worldId, const int userDbId) {
    // Current L2Server 0x00903f58. The account map owns raw socket pointers.
    // Close is performed after leaving its process-wide lock.
    CUserSocket* previous = nullptr;
    {
        L2_GUARD(accountMapSect);
        const auto position = accountMap.find(accountId);
        if (position == accountMap.end()) {
            return true;
        }

        previous = position->second;
        if (previous == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]Previous account erase: %d",
            //     kOlympiadServiceSource,
            //     0xeda,
            //     accountId)
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]Previous account erase: %d",
            //     kOlympiadServiceSource,
            //     0xeda,
            //     accountId)
            L2_LOG_WARN(Log, "Removing empty account-map entry for account %u before Olympiad registration.", accountId);
            accountMap.erase(position);
            return true;
        }

        if (previous->GetAccountID() != accountId ||
            previous->m_hSocket.load(std::memory_order_acquire) == UINT64_C(0xffffffffffffffff)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]Previous account erase and release: %d, %d",
            //     kOlympiadServiceSource,
            //     0xee2,
            //     accountId,
            //     previous->GetAccountID())
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]Previous account erase and release: %d, %d",
            //     kOlympiadServiceSource,
            //     0xee2,
            //     accountId,
            //     previous->GetAccountID())
            L2_LOG_WARN(
                Log,
                "Removing stale account-map entry before Olympiad registration: requested account %u, socket account %u "
                "(account mismatch or invalid socket handle).",
                accountId, static_cast<unsigned int>(previous->GetAccountID())
            );
            accountMap.erase(position);
        }
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d]close socket by account map error: %d, %d, %d, %d, %d, %s, %d",
    //     kOlympiadServiceSource,
    //     0xeef,
    //     accountId,
    //     worldId,
    //     userDbId,
    //     previous->GetAccountID(),
    //     previous->m_nId,
    //     Utf16ToWide(
    //         reinterpret_cast<const char16_t*>(
    //             previous->GetAccountName()))
    //         .c_str(),
    //     previous->m_nCloseReason)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d]close socket by account map error: %d, %d, %d, %d, %d, %s, %d",
    //     kOlympiadServiceSource,
    //     0xeef,
    //     accountId,
    //     worldId,
    //     userDbId,
    //     previous->GetAccountID(),
    //     previous->m_nId,
    //     Utf16ToWide(
    //         reinterpret_cast<const char16_t*>(
    //             previous->GetAccountName()))
    //         .c_str(),
    //     previous->m_nCloseReason)
    L2_LOG_WARN(
        Log,
        "Closing previous connection before Olympiad registration: account %u, world %d, character DB ID %d; previous "
        "account %u, socket object ID %u, close reason %d.",
        accountId, worldId, userDbId, static_cast<unsigned int>(previous->GetAccountID()),
        static_cast<unsigned int>(previous->m_nId), static_cast<int>(previous->m_nCloseReason)
    );
    previous->Close();
    return false;
}

bool CheckAndClosePrevSocket(CSPointer<WOlympiadMemberInfo> member) {
    // Current L2Server 0x009041d4. The by-value copy and its release are part
    // of the current FunctionChain reference lifecycle.
    L2_GUARD_FUNCTION();
    const int userDbId = member->GetDBID();
    const int worldId = member->GetWorldID();
    const std::uint32_t accountId = static_cast<std::uint32_t>(member->GetAccountID());
    return CheckAndClosePrevSocket(accountId, worldId, userDbId);
}

void Write_LOG_WOLYMPIAD_WIN_MATCH_InHost(
    const CSPointer<WOlympiadMemberInfo> member,
    const int olympiadPoint,
    const CSPointer<WOlympiadMemberInfo> opponent,
    const int changedPoint,
    const unsigned int rewardItemClassId,
    const std::int64_t rewardItemCount,
    const WOlympiadGameResultInfo& result
) {
    // Current L2Server 0x008f8dec.
    L2_GUARD_FUNCTION();
    if (!member) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x65b,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x65b,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_ERROR(Log, "Cannot write Olympiad win record: member is missing.");
        return;
    }
    if (!opponent) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x65c,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x65c,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_ERROR(Log, "Cannot write Olympiad win record: opponent is missing.");
        return;
    }

    const std::u16string inzoneType = runtime::DecimalUtf16(result.nInzoneType);
    std::u16string rewardItem = runtime::DecimalUtf16(static_cast<std::int64_t>(rewardItemClassId));
    rewardItem.push_back(u'/');
    rewardItem += runtime::DecimalUtf16(rewardItemCount);

    CLogFormatter formatter{};
    formatter.WriteLog(
        0x1fd3, member->GetDBID(), member->GetAccountID(), opponent->GetDBID(), opponent->GetAccountID(), 0, 0, 0,
        inzoneType.c_str(), rewardItem.c_str(), nullptr, static_cast<int>(member->GetClassType()), member->GetLevel(),
        changedPoint, runtime::AddInt32Wrapping(olympiadPoint, changedPoint), result.fieldId, result.elapsedTime, 0, 0, 0,
        member->GetWorldID(), member->GetCharName().c_str(), member->GetAccountName().c_str(),
        opponent->GetCharName().c_str(), opponent->GetAccountName().c_str(), 0
    );
}

void Write_LOG_WOLYMPIAD_LOSE_MATCH_InHost(
    const CSPointer<WOlympiadMemberInfo> member,
    const int olympiadPoint,
    const CSPointer<WOlympiadMemberInfo> opponent,
    const int changedPoint,
    const unsigned int rewardItemClassId,
    const std::int64_t rewardItemCount,
    const WOlympiadGameResultInfo& result
) {
    // Current L2Server 0x008f93d4.
    L2_GUARD_FUNCTION();
    if (!opponent) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x68e,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x68e,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_ERROR(Log, "Cannot write Olympiad loss record: opponent is missing.");
        return;
    }
    if (!member) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x68f,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x68f,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_ERROR(Log, "Cannot write Olympiad loss record: member is missing.");
        return;
    }

    const std::u16string inzoneType = runtime::DecimalUtf16(result.nInzoneType);
    std::u16string rewardItem = runtime::DecimalUtf16(static_cast<std::int64_t>(rewardItemClassId));
    rewardItem.push_back(u'/');
    rewardItem += runtime::DecimalUtf16(rewardItemCount);

    CLogFormatter formatter{};
    formatter.WriteLog(
        0x1fd4, member->GetDBID(), member->GetAccountID(), opponent->GetDBID(), opponent->GetAccountID(), 0, 0, 0,
        inzoneType.c_str(), rewardItem.c_str(), nullptr, static_cast<int>(member->GetClassType()), member->GetLevel(),
        changedPoint, runtime::AddInt32Wrapping(olympiadPoint, changedPoint), result.fieldId, result.elapsedTime, 0, 0, 0,
        member->GetWorldID(), member->GetCharName().c_str(), member->GetAccountName().c_str(),
        opponent->GetCharName().c_str(), opponent->GetAccountName().c_str(), 0
    );
}

void Write_LOG_WOLYMPIAD_DRAW_MATCH_InHost(
    const CSPointer<WOlympiadMemberInfo> member,
    const int olympiadPoint,
    const int changedPoint,
    const CSPointer<WOlympiadMemberInfo> opponent,
    const WOlympiadGameResultInfo& result
) {
    // Current L2Server 0x008f99bc.
    L2_GUARD_FUNCTION();
    if (!member) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x6be,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x6be,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_ERROR(Log, "Cannot write Olympiad draw record: member is missing.");
        return;
    }
    if (!opponent) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x6bf,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x6bf,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_ERROR(Log, "Cannot write Olympiad draw record: opponent is missing.");
        return;
    }

    const std::u16string inzoneType = runtime::DecimalUtf16(result.nInzoneType);
    CLogFormatter formatter{};
    formatter.WriteLog(
        0x1fd5, member->GetDBID(), member->GetAccountID(), opponent->GetDBID(), opponent->GetAccountID(), 0, 0, 0,
        inzoneType.c_str(), nullptr, nullptr, static_cast<int>(member->GetClassType()), member->GetLevel(), changedPoint,
        runtime::AddInt32Wrapping(olympiadPoint, changedPoint), result.fieldId, result.elapsedTime, 0, 0, 0,
        member->GetWorldID(), member->GetCharName().c_str(), member->GetAccountName().c_str(),
        opponent->GetCharName().c_str(), opponent->GetAccountName().c_str(), 0
    );
}

void Write_LOG_WOLYMPIAD_WIN_MATCH_POINT_InHost(
    const CSPointer<WOlympiadMemberInfo> member,
    const int olympiadPoint,
    const CSPointer<WOlympiadMemberInfo> opponent,
    const int opponentOlympiadPoint,
    const WOlympiadGameResultInfo& result
) {
    // Current L2Server 0x008f9fb8.
    L2_GUARD_FUNCTION();
    if (!member) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x6ed,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x6ed,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_ERROR(Log, "Cannot write Olympiad winning-point record: member is missing.");
        return;
    }
    if (!opponent) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x6ee,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x6ee,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_ERROR(Log, "Cannot write Olympiad winning-point record: opponent is missing.");
        return;
    }

    const std::u16string inzoneType = runtime::DecimalUtf16(result.nInzoneType);
    CLogFormatter formatter{};
    formatter.WriteLog(
        0x1fe5, member->GetDBID(), member->GetAccountID(), opponent->GetDBID(), opponent->GetAccountID(), 0, 0, 0,
        inzoneType.c_str(), nullptr, nullptr, static_cast<int>(member->GetClassType()), member->GetLevel(),
        member->GetMatchingPoint().nWinningPoint, olympiadPoint, result.fieldId, opponent->GetMatchingPoint().nWinningPoint,
        opponentOlympiadPoint, 0, 0, member->GetWorldID(), member->GetCharName().c_str(), member->GetAccountName().c_str(),
        opponent->GetCharName().c_str(), opponent->GetAccountName().c_str(), 0
    );
}

void Write_LOG_WOLYMPIAD_LOSE_MATCH_POINT_InHost(
    const CSPointer<WOlympiadMemberInfo> member,
    const int olympiadPoint,
    const CSPointer<WOlympiadMemberInfo> opponent,
    const int opponentOlympiadPoint,
    const WOlympiadGameResultInfo& result
) {
    // Current L2Server 0x008fa618.
    L2_GUARD_FUNCTION();
    if (!opponent) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x71b,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x71b,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_ERROR(Log, "Cannot write Olympiad losing-point record: opponent is missing.");
        return;
    }
    if (!member) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x71c,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x71c,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_ERROR(Log, "Cannot write Olympiad losing-point record: member is missing.");
        return;
    }

    const std::u16string inzoneType = runtime::DecimalUtf16(result.nInzoneType);
    CLogFormatter formatter{};
    formatter.WriteLog(
        0x1fe6, member->GetDBID(), member->GetAccountID(), opponent->GetDBID(), opponent->GetAccountID(), 0, 0, 0,
        inzoneType.c_str(), nullptr, nullptr, static_cast<int>(member->GetClassType()), member->GetLevel(),
        member->GetMatchingPoint().nWinningPoint, olympiadPoint, result.fieldId, opponent->GetMatchingPoint().nWinningPoint,
        opponentOlympiadPoint, 0, 0, member->GetWorldID(), member->GetCharName().c_str(), member->GetAccountName().c_str(),
        opponent->GetCharName().c_str(), opponent->GetAccountName().c_str(), 0
    );
}

void Write_LOG_WOLYMPIAD_DRAW_MATCH_POINT_InHost(
    const CSPointer<WOlympiadMemberInfo> member,
    const int olympiadPoint,
    const CSPointer<WOlympiadMemberInfo> opponent,
    const int opponentOlympiadPoint,
    const WOlympiadGameResultInfo& result
) {
    // Current L2Server 0x008fac80.
    L2_GUARD_FUNCTION();
    if (!member) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x749,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x749,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_ERROR(Log, "Cannot write Olympiad draw-point record: member is missing.");
        return;
    }
    if (!opponent) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x74a,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x74a,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_ERROR(Log, "Cannot write Olympiad draw-point record: opponent is missing.");
        return;
    }

    const std::u16string inzoneType = runtime::DecimalUtf16(result.nInzoneType);
    CLogFormatter formatter{};
    formatter.WriteLog(
        0x1fe7, member->GetDBID(), member->GetAccountID(), opponent->GetDBID(), opponent->GetAccountID(), 0, 0, 0,
        inzoneType.c_str(), nullptr, nullptr, static_cast<int>(member->GetClassType()), member->GetLevel(),
        member->GetMatchingPoint().nWinningPoint, olympiadPoint, result.fieldId, opponent->GetMatchingPoint().nWinningPoint,
        opponentOlympiadPoint, 0, 0, member->GetWorldID(), member->GetCharName().c_str(), member->GetAccountName().c_str(),
        opponent->GetCharName().c_str(), opponent->GetAccountName().c_str(), 0
    );
}

std::vector<int> CreateRandomInzonTypeIDs() {
    // Current L2Server 0x00905d90. The final extra entry is deliberately
    // type 147, giving the target distribution 284/283/283.
    L2_GUARD_FUNCTION();
    std::vector<int> typeIds;
    typeIds.reserve(SharedOlympiadInfo::FIELD_COUNT);
    for (int typeId = 147; typeId < 150; ++typeId) {
        for (int count = 0; count < 283; ++count) {
            typeIds.push_back(typeId);
        }
    }
    typeIds.push_back(147);

    // Current MSVC std::random_shuffle implementation at 0x008fd6d0 uses
    // rand() and swaps element n with rand()%n for n=2..size.
    for (std::size_t count = 2; count <= typeIds.size(); ++count) {
        const auto random = static_cast<unsigned int>(std::rand()) & 0x7fffU;
        std::swap(runtime::CheckedVectorAt(typeIds, count - 1), runtime::CheckedVectorAt(typeIds, random % count));
    }
    return typeIds;
}

} // namespace

namespace WOlympiadServiceConst {

// Current L2Server data 0x015da39a. The packet-handler configuration path
// mutates this byte after startup. The image default is enabled.
bool _MATCHING_POINT_MODE = true;

} // namespace WOlympiadServiceConst

class MatchingPointUpdater {
public:
    struct Member {
        Member() = default;
        Member(const Member&) = default;

        std::CDualKey<int, int> key;
        float fLose;
        float fWin;
        float fDraw;
        float fPanalty;
        std::int64_t nConstK;
        std::int64_t nOlympiadPoint;
        bool CheckValidConditionAtWait;
        float fWinningRate;
        bool bWinner;
        std::int64_t nResultPoint;
    };

    MatchingPointUpdater(int gameResult, Member member1, Member member2);
    ~MatchingPointUpdater() = default;

    std::int64_t GetResultPoint(std::CDualKey<int, int> key) const {
        return m_member1.key == key ? m_member1.nResultPoint : m_member2.nResultPoint;
    }

    bool IsSuccess() const { return m_bSuccess; }

private:
    float GetSa(Member& member);
    void SetResultPoint(Member& member, float sa);
    std::int64_t CulcateResultPoint(Member& member, std::int64_t constK, float sa);
    int RoundUpAndDown(bool winner, double value);

    int m_nGameResult;
    bool m_bSuccess;
    Member m_member1;
    Member m_member2;
    std::vector<float, std::allocator<float>> constKs;
};


MatchingPointUpdater::MatchingPointUpdater(const int gameResult, Member member1, Member member2) :
    m_nGameResult(gameResult), m_bSuccess(false) {
    if (static_cast<unsigned int>(m_nGameResult) > 4U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]failed, nGameResult: %d",
        //     kOlympiadServiceSource,
        //     0x1a2f,
        //     m_nGameResult)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]failed, nGameResult: %d",
        //     kOlympiadServiceSource,
        //     0x1a2f,
        //     m_nGameResult)
        L2_LOG_ERROR(
            Log, "Cannot calculate Olympiad matching points: game result %d is outside the supported range 0..4.",
            m_nGameResult
        );
        return;
    }

    member1.bWinner = m_nGameResult == 1;
    member2.bWinner = m_nGameResult == 0;
    SetResultPoint(member1, GetSa(member1));
    SetResultPoint(member2, GetSa(member2));
    std::memcpy(&m_member1, &member1, sizeof(m_member1));
    std::memcpy(&m_member2, &member2, sizeof(m_member2));
    m_bSuccess = true;
}

float MatchingPointUpdater::GetSa(Member& member) {
    if (m_nGameResult >= 0 && m_nGameResult < 2) {
        if (member.bWinner) {
            return member.fWin;
        }
        return member.CheckValidConditionAtWait ? member.fPanalty : member.fLose;
    }
    if (m_nGameResult == 2) {
        return member.CheckValidConditionAtWait ? member.fPanalty : member.fDraw;
    }
    if (m_nGameResult == 4) {
        return member.fPanalty;
    }
    return 0.0F;
}

void MatchingPointUpdater::SetResultPoint(Member& member, const float sa) {
    member.nResultPoint = CulcateResultPoint(member, member.nConstK, sa);
}

std::int64_t MatchingPointUpdater::CulcateResultPoint(Member& member, const std::int64_t constK, const float sa) {
    const float raw = (sa - member.fWinningRate) * static_cast<float>(constK);
    return static_cast<std::int64_t>(RoundUpAndDown(member.bWinner, static_cast<double>(raw)));
}

int MatchingPointUpdater::RoundUpAndDown(const bool winner, const double value) {
    const std::int64_t truncated64 = runtime::Win64CvttDoubleToInt64(value);
    int result = runtime::Int32FromBits(static_cast<std::uint32_t>(truncated64));
    const double truncated = static_cast<double>(result);
    if (value > truncated) {
        result = runtime::AddInt32Wrapping(result, 1);
    } else if (truncated > value) {
        result = runtime::SubtractInt32Wrapping(result, 1);
    } else if (result == 0) {
        result = winner ? 1 : -1;
    }
    return result;
}

MatchingPointUpdater::Member ConvertFrom(WOlympiadTeam& team) {
    MatchingPointUpdater::Member member;
    member.key = team.GetMemberKey();
    member.nOlympiadPoint = team.GetOlympiadPoint();
    member.CheckValidConditionAtWait = CheckMatchingValidConditionAtWait(team) != GVC_NONE;
    {
        CSPointer<WOlympiadMemberInfo> info = team.GetMember();
        member.fWinningRate = info->GetOlympiadPoint().fWinningRate;
    }
    g_MatchingPointDB.GetOlympiadPoints(
        member.nOlympiadPoint, member.nConstK, member.fLose, member.fWin, member.fDraw, member.fPanalty
    );
    member.nResultPoint = 0;
    return member;
}

WOlympiadMessage::WOlympiadMessage() : nSeason(0), nStep(0), tSeasonStart(0), idLegendary(-1) {}

L2_DEFINE_OBJECT_CLASS_ID(WOlympiadMemberInfo)

L2_DEFINE_OBJECT_CLASS_NAME(WOlympiadMemberInfo, m_pClassName_WOlympiadMemberInfo)

// Current L2Server 0x008ecbd0. Current memory-class number 203.
L2_DEFINE_OBJECT_ALLOCATOR(WOlympiadMemberInfo, __GetClassName())

L2_DEFINE_OBJECT_NEW(WOlympiadMemberInfo)

// Current vtable slot 0, folded at 0x007e8928.
L2_DEFINE_OBJECT_ADD_REF(WOlympiadMemberInfo)

// Current L2Server 0x008fdcdc.
L2_DEFINE_OBJECT_RELEASE(WOlympiadMemberInfo)

WOlympiadMemberInfo::WOlympiadMemberInfo(const std::CDualKey<int, int>& key)
    // Current L2Server 0x008fdc30.
    :
    m_Key(key),
    m_Application{},
    m_bIsPlayer(false),
    m_AuthInfo{},
    m_nFieldID(-1),
    m_eGameValidCondition(GVC_NONE),
    m_bPeerDisconnected(false),
    m_bMatchingPointLoaded(false),
    m_MatchingPoint{} {}

WOlympiadMemberInfo::WOlympiadMemberInfo(const std::CDualKey<int, int>& key, const olympiad::PkApplication& application)
    // Current L2Server 0x00901a74.
    :
    m_Key(key),
    m_Application(application),
    m_bIsPlayer(true),
    m_AuthInfo{},
    m_nFieldID(-1),
    m_eGameValidCondition(GVC_NONE),
    m_bPeerDisconnected(false),
    m_bMatchingPointLoaded(false),
    m_MatchingPoint{} {}

WOlympiadMemberInfo::~WOlympiadMemberInfo() = default;

std::CDualKey<int, int> WOlympiadMemberInfo::GetKey() const { return m_Key; }

int WOlympiadMemberInfo::GetWorldID() const { return m_Key.m_PrimaryKey; }

int WOlympiadMemberInfo::GetDBID() const { return m_Key.m_SecondaryKey; }

GameRuleType WOlympiadMemberInfo::GetGameRule() const { return static_cast<GameRuleType>(m_Application.cGameRule); }

ClassType WOlympiadMemberInfo::GetClassType() const { return static_cast<ClassType>(m_Application.nClassType); }

void WOlympiadMemberInfo::SetFieldID(const int fieldId) { m_nFieldID = fieldId; }

int WOlympiadMemberInfo::GetFieldID() const { return m_nFieldID; }

bool WOlympiadMemberInfo::IsInMatch() const { return m_nFieldID != -1; }

void WOlympiadMemberInfo::SetAuthInfo(const olympiad::PkOlympiadAuthInfo& authInfo) { m_AuthInfo = authInfo; }

const olympiad::PkOlympiadAuthInfo& WOlympiadMemberInfo::GetAuthInfo() const { return m_AuthInfo; }

bool WOlympiadMemberInfo::IsPlayer() const { return m_bIsPlayer; }

GameValidCondition WOlympiadMemberInfo::GetPrepareValidCondition() const { return m_eGameValidCondition; }

void WOlympiadMemberInfo::SetPrepareValidCondition(const GameValidCondition condition) { m_eGameValidCondition = condition; }

void WOlympiadMemberInfo::SetPeerDisconnect() { m_bPeerDisconnected = true; }

bool WOlympiadMemberInfo::IsPeerDisconnected() const { return m_bPeerDisconnected; }

const std::u16string& WOlympiadMemberInfo::GetCharName() const { return m_Application.sCharName; }

const std::u16string& WOlympiadMemberInfo::GetWorldCharName() const {
    // Current L2Server 0x00901b20: lazy "%s_%d" publication.
    L2_GUARD_FUNCTION();
    if (m_sWorldCharName.empty()) {
        std::u16string worldName(m_Application.sCharName.data(), m_Application.sCharName.size());
        worldName.push_back(u'_');
        worldName.append(Utf8ToUtf16(std::to_string(m_Key.m_PrimaryKey)));
        m_sWorldCharName.assign(worldName);
    }
    return m_sWorldCharName;
}

int WOlympiadMemberInfo::GetAccountID() const { return m_Application.nAccountID; }

RaceType WOlympiadMemberInfo::GetRace() const { return static_cast<RaceType>(m_Application.nRace); }

SexType WOlympiadMemberInfo::GetSex() const { return static_cast<SexType>(m_Application.nSex); }

int WOlympiadMemberInfo::GetLevel() const { return m_Application.nLevel; }

const std::u16string& WOlympiadMemberInfo::GetAccountName() const { return m_Application.sAccountName; }

const olympiad::PkApplication& WOlympiadMemberInfo::GetAllication() const { return m_Application; }

void WOlympiadMemberInfo::ResetMatchingInfo() {
    m_bMatchingPointLoaded = false;
    m_MatchingPoint = {};
}

std::int64_t WOlympiadServiceUtil::ValidateMatchingPointDiff(
    const std::int64_t basePoint, const std::int64_t matchingPointDiff
) {
    // Current L2Server 0x008f8428.
    L2_GUARD_FUNCTION();
    const auto type = MatchingPoint::MPT_OLYMPIAD;
    const std::int64_t maximumPoint = g_MatchingPointDB.m_nMatchingPointMax[type];
    const std::int64_t minimum = runtime::SubtractInt64Wrapping(g_MatchingPointDB.m_nMatchingPointMin[type], basePoint);
    if (matchingPointDiff < minimum) {
        return minimum;
    }
    const std::int64_t maximum = runtime::SubtractInt64Wrapping(maximumPoint, basePoint);
    if (matchingPointDiff > maximum) {
        return maximum;
    }
    return matchingPointDiff;
}

void WOlympiadMemberInfo::SetMatchingPoint(const std::int64_t point) {
    // Current L2Server 0x008f8630.
    L2_GUARD_FUNCTION();
    std::int64_t base = 0;
    if (!g_MatchingPointDB.GetMatchingPointBase(MatchingPoint::MPT_OLYMPIAD, m_Application.nLevel, base)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]fail to get base point user: %d, %d, %d",
        //     L"OlympiadService.cpp",
        //     0x203,
        //     m_Key.m_PrimaryKey,
        //     m_Key.m_SecondaryKey,
        //     m_Application.nLevel)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]fail to get base point user: %d, %d, %d",
        //     L"OlympiadService.cpp",
        //     0x203,
        //     m_Key.m_PrimaryKey,
        //     m_Key.m_SecondaryKey,
        //     m_Application.nLevel)
        L2_LOG_ERROR(
            Log,
            "Cannot load Olympiad matching points: base points are unavailable for level %d (world %d, character DB ID %d).",
            m_Application.nLevel, m_Key.m_PrimaryKey, m_Key.m_SecondaryKey
        );
        return;
    }

    m_bMatchingPointLoaded = true;
    m_MatchingPoint.nMatchingPointBase = base;
    m_MatchingPoint.nMatchingPointDiff = WOlympiadServiceUtil::ValidateMatchingPointDiff(base, point);
    m_MatchingPoint.nMatchingPointTotal = runtime::AddInt64Wrapping(base, m_MatchingPoint.nMatchingPointDiff);
}

std::int64_t WOlympiadMemberInfo::CalculateMatchingPoint(const std::int64_t point) const {
    return runtime::AddInt64Wrapping(m_MatchingPoint.nMatchingPointDiff, point);
}

std::int64_t WOlympiadMemberInfo::GetWinningPoint() const { return m_MatchingPoint.nWinningPoint; }

bool WOlympiadMemberInfo::IsLoadedMatchingPoint() const { return m_bMatchingPointLoaded; }

const WOlympiadMatchingPoint& WOlympiadMemberInfo::GetMatchingPoint() const { return m_MatchingPoint; }

WOlympiadMatchingPoint& WOlympiadMemberInfo::GetOlympiadPoint() { return m_MatchingPoint; }

void WOlympiadMemberInfo::SetWinningRate(const std::int64_t opponentPoint, const std::int64_t ownPoint) {
    // Current L2Server 0x009093e0: Elo expected-score formula.
    m_MatchingPoint.fWinningRate =
        static_cast<float>(1.0 / (std::pow(10.0, static_cast<double>(opponentPoint - ownPoint) / 400.0) + 1.0));
}

float WOlympiadMemberInfo::GetWinningRate() const { return m_MatchingPoint.fWinningRate; }

void WOlympiadMemberInfo::SetWinningPoint(const std::int64_t point, const float rate) {
    m_MatchingPoint.nWinningPoint = point;
    m_MatchingPoint.fWinningRate = rate;
}

L2_DEFINE_OBJECT_CLASS_ID(WOlympiadTeam)

L2_DEFINE_OBJECT_CLASS_NAME(WOlympiadTeam, m_pClassName_WOlympiadTeam)

// Current L2Server 0x008f42a4. Current memory-class number 204.
L2_DEFINE_OBJECT_ALLOCATOR(WOlympiadTeam, __GetClassName())

L2_DEFINE_OBJECT_NEW(WOlympiadTeam)

WOlympiadTeam::WOlympiadTeam()
    // Current L2Server 0x008f8768. m_PrevOlympiadPoint is not
    // initialized by the target default constructor.
    :
    m_GameRule(GRT_MAX), m_PartyMasterIndex(0), m_ChangePoint(0), m_PlayerInfo{} {}

WOlympiadTeam::WOlympiadTeam(CSPointer<WOlympiadMemberInfo> member)
    // Current L2Server 0x00909fec. m_PrevOlympiadPoint is intentionally
    // left uninitialized by this target constructor.
    :
    m_GameRule(member ? member->GetGameRule() : GRT_MAX),
    m_PartyMasterIndex(0),
    m_ChangePoint(0),
    m_spMember(member),
    m_PlayerInfo{} {
    L2_GUARD_FUNCTION();
    if (!member) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x262,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x262,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_ERROR(Log, "Olympiad team initialized without a member; matching information was not reset.");
        return;
    }
    if (WOlympiadUsers::inst().GetNoblessInfo(member->GetKey()) == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]No nobless: %d, %d",
        //     kOlympiadServiceSource,
        //     0x26d,
        //     member->GetWorldID(),
        //     member->GetDBID())
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]No nobless: %d, %d",
        //     kOlympiadServiceSource,
        //     0x26d,
        //     member->GetWorldID(),
        //     member->GetDBID())
        L2_LOG_ERROR(
            Log,
            "Olympiad team has no nobless information for world %d, character DB ID %d; matching information was not reset.",
            member->GetWorldID(), member->GetDBID()
        );
        return;
    }
    member->ResetMatchingInfo();
}

WOlympiadTeam::WOlympiadTeam(const WOlympiadTeam& other) :
    MemoryObject(other),
    m_GameRule(other.m_GameRule),
    m_PartyMasterIndex(other.m_PartyMasterIndex),
    m_ChangePoint(other.m_ChangePoint),
    m_PrevOlympiadPoint(other.m_PrevOlympiadPoint),
    m_spMember(other.m_spMember),
    m_PlayerInfo(other.m_PlayerInfo) {
    L2_GUARD_FUNCTION();
}

WOlympiadTeam::~WOlympiadTeam() = default;

// Current L2Server 0x008dc530.
L2_DEFINE_OBJECT_RELEASE(WOlympiadTeam)

const WOlympiadPlayerInfo* WOlympiadTeam::GetPlayerInfo() const {
    // Current L2Server 0x008f4ad4.
    return m_PlayerInfo.playerSID == 0 ? nullptr : &m_PlayerInfo;
}

CSPointer<WOlympiadMemberInfo> WOlympiadTeam::GetMember() const {
    // Current L2Server 0x008fb524.
    L2_GUARD_FUNCTION();
    return m_spMember;
}

bool WOlympiadTeam::IsRegisteredUser(const std::uint32_t sid) const {
    // Current L2Server 0x008f53a8.
    L2_GUARD_FUNCTION();
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    return player != nullptr && player->playerSID == sid;
}

bool WOlympiadTeam::IsRegisteredUser(CSPointer<User> user) const {
    // Current L2Server 0x008f8828.
    L2_GUARD_FUNCTION();
    return user && IsRegisteredUser(user->GetServerId());
}

bool WOlympiadTeam::IsRegisteredUser(const std::CDualKey<int, int> key) const {
    // Current L2Server 0x008fdec8.
    L2_GUARD_FUNCTION();
    CSPointer<WOlympiadMemberInfo> member = GetMember();
    return member && member->GetKey() == key;
}

void WOlympiadTeam::SetConnectedUser(CSPointer<User> user) {
    // Current L2Server 0x008fb2e0.
    m_PlayerInfo.playerWorldId = user->m_data->m_nWorldId;
    m_PlayerInfo.playerSID = user->GetServerId();
    if (!user->m_pWOlympiadUser->RegisterOlympiadEntry()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]nStatus: %d",
        //     L"OlympiadService.cpp",
        //     0x97d,
        //     static_cast<int>(user->m_pWOlympiadUser->GetStatus()))
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]nStatus: %d",
        //     L"OlympiadService.cpp",
        //     0x97d,
        //     static_cast<int>(user->m_pWOlympiadUser->GetStatus()))
        L2_LOG_ERROR(
            Log, "Cannot register the connected player for Olympiad entry: status %d; continuing team setup.",
            static_cast<int>(user->m_pWOlympiadUser->GetStatus())
        );
    }
    m_PlayerInfo.pledgeId = static_cast<int>(user->m_data->m_nPledgeID);

    const CPledge* const pledge = user->GetPledge();
    const std::u16string pledgeName = std::u16string(pledge == nullptr ? u"" : pledge->GetPledgeName());
    const std::size_t copyCount = std::min(pledgeName.size(), std::size(m_PlayerInfo.pledgeName) - 1U);
    std::char_traits<char16_t>::copy(m_PlayerInfo.pledgeName, pledgeName.data(), copyCount);
    m_PlayerInfo.pledgeName[copyCount] = u'\0';
    m_PartyMasterIndex = user->m_data->m_nClass;
}

std::CDualKey<int, int> WOlympiadTeam::GetMemberKey() const {
    CSPointer<WOlympiadMemberInfo> member = GetMember();
    return member ? member->GetKey() : std::CDualKey<int, int>{};
}

std::uint32_t WOlympiadTeam::GetUserSID() const {
    L2_GUARD_FUNCTION();
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    return player == nullptr ? 0U : player->playerSID;
}

void WOlympiadTeam::UnregisterAll() {
    // Current L2Server 0x008fdfb0.
    L2_GUARD_FUNCTION();
    CSPointer<WOlympiadMemberInfo> member = GetMember();
    if (!member) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kOlympiadServiceSource,
        //     0x2a5)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kOlympiadServiceSource,
        //     0x2a5)
        L2_LOG_WARN(Log, "Cannot unregister Olympiad team: member is missing.");
        return;
    }

    WOlympiadStuff::Write_LOG_WOLYMPIAD_REMOVE_ENTRY_InHost(
        member->GetWorldID(), member->GetDBID(), member->GetAllication()
    );

    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return;
    }

    CSPointer<User> user(WOlympiadStuff::FindUserBySID(player->playerSID), ObjectRefType::FunctionChain);
    if (!user) {
        return;
    }

    if (user->m_pWOlympiadUser == nullptr || !user->m_pWOlympiadUser->OlympiadStatusReset()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]StatusReset failed: %d, %d, %d",
        //     kOlympiadServiceSource,
        //     0x2c1,
        //     user->m_data->m_nDBID,
        //     static_cast<int>(user->GetServerId()),
        //     user->m_data->m_nClass)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]StatusReset failed: %d, %d, %d",
        //     kOlympiadServiceSource,
        //     0x2c1,
        //     user->m_data->m_nDBID,
        //     static_cast<int>(user->GetServerId()),
        //     user->m_data->m_nClass)
        L2_LOG_ERROR(
            Log,
            "Cannot reset Olympiad status while unregistering character DB ID %d (object ID %u, class %d); clearing the "
            "applicant flag.",
            user->m_data->m_nDBID, user->GetServerId(), user->m_data->m_nClass
        );
    }
    user->m_data->SetOlympidApplicant(false);
}

void WOlympiadTeam::SendV(const char* const format, va_list arguments) const {
    // Current L2Server 0x008f552c.
    L2_GUARD_FUNCTION();
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return;
    }
    User* const pUser = WOlympiadStuff::FindUserBySID(player->playerSID);
    if (pUser == nullptr) {
        return;
    }
    pUser->GetSocket()->SendV(format, arguments);
}

void WOlympiadTeam::Send(const char* const format, ...) const {
    // Current L2Server 0x008f5614.
    L2_GUARD_FUNCTION();
    va_list arguments;
    va_start(arguments, format);
    SendV(format, arguments);
    va_end(arguments);
}

void WOlympiadTeam::Send(const S_EX_PROTOCOL protocol, const network::IPacket& packet) const {
    // Current L2Server 0x008f56b8.
    L2_GUARD_FUNCTION();
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player != nullptr) {
        WOlympiadStuff::SendPacketTo(player->playerSID, protocol, packet);
    }
}

void WOlympiadTeam::RestorePlayers(const bool bWithSkill) {
    // Current L2Server 0x008f6184.
    L2_GUARD_FUNCTION();
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return;
    }
    User* const pUser = WOlympiadStuff::FindUserBySID(player->playerSID);
    if (pUser == nullptr) {
        return;
    }

    {
        L2_GUARD(*pUser->m_lock);
        pUser->m_data->RestoreCP();
        pUser->m_data->RestoreHP();
        pUser->m_data->RestoreMP();
    }
    pUser->ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_OLYMPIAD});

    if (bWithSkill) {
        if (CPledge* const pPledge = pUser->GetPledge()) {
            pPledge->AddPledgeSkillsTo(pUser, false);
        }
    }
}

void WOlympiadTeam::SetPlayer(const OlympiadTeamEnum team, const int fieldId, CInstantZone* const inZone) {
    // Current L2Server 0x008f5fb0.
    L2_GUARD_FUNCTION();
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return;
    }
    User* const pUser = WOlympiadStuff::FindUserBySID(player->playerSID);
    if (pUser == nullptr) {
        return;
    }
    pUser->m_pWOlympiadUser->SetPlayer(
        static_cast<std::uint8_t>(team), fieldId, inZone, m_GameRule, static_cast<int>(team) * 3
    );
}

void WOlympiadTeam::UpdateStatus(WOlympiadMatch* const match, const int targetSid) {
    // Current L2Server 0x008f6320.
    L2_GUARD_FUNCTION();
    if (match == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x5a8,
        //     L"WO_ASSERT_NOT_NULL")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x5a8,
        //     L"WO_ASSERT_NOT_NULL")
        L2_LOG_ERROR(Log, "Cannot update Olympiad player status: match is missing (target object ID %d).", targetSid);
        return;
    }
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return;
    }
    match->AsyncUpdateUserStatus(player->playerSID, targetSid);
    match->AsyncUpdateSpelledInfo(player->playerSID, targetSid);
}

void WOlympiadTeam::Freeze() {
    // Current L2Server 0x009020f8.
    L2_GUARD_FUNCTION();
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return;
    }
    User* const pUser = WOlympiadStuff::FindUserBySID(player->playerSID);
    if (pUser == nullptr) {
        return;
    }

    pUser->StopMove();
    pUser->DoNothing();
    FOR_ALL_SUMMON_IN_SUMMON_LIST(pUser, summon, summon->StopMove(); summon->DoNothing(););
}

void WOlympiadTeam::IncreaseMatchCount() {
    // Current L2Server 0x0090f828.
    L2_GUARD_FUNCTION();
    CSPointer<WOlympiadMemberInfo> member = GetMember();
    if (!member) {
        return;
    }
    WOlympiadUsers::inst().IncMatchCount(member->GetKey(), m_GameRule, 1);
}

const wchar_t* WOlympiadTeam::GetPledageName() const {
    // Current L2Server 0x008f4b6c. The target returns the fixed UTF-16
    // buffer embedded in WOlympiadPlayerInfo. LP64 needs an equivalent
    // host-wide view instead of a widening reinterpret-cast.
    L2_GUARD_FUNCTION();
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return nullptr;
    }
    thread_local std::wstring pledgeName;
    pledgeName =
        Utf16ToWide(std::u16string_view(player->pledgeName, std::char_traits<char16_t>::length(player->pledgeName)));
    return pledgeName.c_str();
}

void WOlympiadTeam::SendSystemMessage(const int msgNo) const {
    // Current L2Server 0x008f545c.
    L2_GUARD_FUNCTION();
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return;
    }
    User* const pUser = WOlympiadStuff::FindUserBySID(player->playerSID);
    if (pUser != nullptr) {
        pUser->GetSocket()->SendSystemMessage(msgNo);
    }
}

bool WOlympiadTeam::IsValidTeam() const {
    // Current L2Server 0x008f5780.
    L2_GUARD_FUNCTION();
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return false;
    }
    User* const pUser = WOlympiadStuff::FindUserBySID(player->playerSID);
    return pUser != nullptr;
}

bool WOlympiadTeam::IsFishingZone() const {
    // Current L2Server 0x008f5840.
    L2_GUARD_FUNCTION();
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return true;
    }
    User* const pUser = WOlympiadStuff::FindUserBySID(player->playerSID);
    if (pUser == nullptr) {
        return true;
    }
    if (CAreaDB::inst().IsFishingZone(pUser->GetPos(), 0)) {
        pUser->SendSystemMessage(SystemMessageId::CANNOT_JOIN_OLYMPIAD_WHILE_IN_FISHING_AREA);
        return false;
    }
    return true;
}

void WOlympiadTeam::SendInvalidMessage(const int messageId, User* const user) const {
    L2_GUARD_FUNCTION();
    Send("chccS", 0x62, messageId, 1, 0x0c, user->GetName());
}

void WOlympiadTeam::ResetStatus() {
    // Current L2Server 0x008f5a2c.
    L2_GUARD_FUNCTION();
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return;
    }
    User* const pUser = WOlympiadStuff::FindUserBySID(player->playerSID);
    if (pUser != nullptr) {
        pUser->m_pWOlympiadUser->OlympiadStatusReset();
    }
}

bool WOlympiadTeam::WaitingGame() {
    // Current L2Server 0x008f5af0.
    L2_GUARD_FUNCTION();
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return true;
    }
    User* const pUser = WOlympiadStuff::FindUserBySID(player->playerSID);
    return pUser != nullptr && pUser->m_pWOlympiadUser->WaitingOlympiadPlaying();
}

void WOlympiadTeam::StartGame() {
    // Current L2Server 0x008f5bc0.
    L2_GUARD_FUNCTION();
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return;
    }
    User* const user = WOlympiadStuff::FindUserBySID(player->playerSID);
    if (user == nullptr) {
        return;
    }
    user->m_pWOlympiadUser->ResetDamage();
    user->PlayMusic(u"ns17_f");
    WOlympiadDefaultSetting::inst().GiveSkillToPlayer(user, m_GameRule);
}

void WOlympiadTeam::FinishGame(const bool needRevive) {
    // Current L2Server 0x008f5cac.
    L2_GUARD_FUNCTION();
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return;
    }
    User* const user = WOlympiadStuff::FindUserBySID(player->playerSID);
    if (user == nullptr) {
        return;
    }
    if (needRevive) {
        user->m_pWOlympiadUser->SetAlive(true);
    }
    user->DispelByBuffProtectionFlag(2);
}

void WOlympiadTeam::RecvSummonInfo(CSummon* const pSummon) const {
    // Current L2Server 0x008f5d88.
    L2_GUARD_FUNCTION();
    if (pSummon == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x4cb,
        //     L"WO_ASSERT_NOT_NULL")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x4cb,
        //     L"WO_ASSERT_NOT_NULL")
        L2_LOG_WARN(Log, "Cannot send summon information to the Olympiad player: summon is missing.");
        return;
    }
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return;
    }
    User* const pUser = WOlympiadStuff::FindUserBySID(player->playerSID);
    if (pUser == nullptr) {
        return;
    }
    pSummon->SendNPCInfo(pUser, false, false);
    if (!g_bSendSummonHPMPInfo) {
        SendSummonHPMPInfo(pUser, pSummon);
    }
}

void WOlympiadTeam::RecvHideSummonInfo(CSummon* const pSummon) const {
    // Current L2Server 0x008f5ea8.
    L2_GUARD_FUNCTION();
    if (pSummon == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x4e5,
        //     L"WO_ASSERT_NOT_NULL")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x4e5,
        //     L"WO_ASSERT_NOT_NULL")
        L2_LOG_WARN(Log, "Cannot hide summon information from the Olympiad player: summon is missing.");
        return;
    }
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return;
    }
    User* const pUser = WOlympiadStuff::FindUserBySID(player->playerSID);
    if (pUser != nullptr && !g_bSendSummonHPMPInfo) {
        HideSummonHPMPInfo(pUser, pSummon);
    }
}

void WOlympiadTeam::SendSummonHPMPInfo(User* const user, CSummon* const summon) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || summon == nullptr || summon->m_data == nullptr || user->GetSocket() == nullptr) {
        return;
    }
    user->GetSocket()->Send(
        "cddcccdcdcdcd", 0x18, static_cast<int>(summon->m_nId), 0, 0, 4, 9, static_cast<int>(summon->m_data->m_dHP), 10,
        static_cast<int>(summon->m_data->m_dBaseHP), 0x0b, static_cast<int>(summon->m_data->m_dMP), 0x0c,
        static_cast<int>(summon->m_data->m_dBaseMP)
    );
}

void WOlympiadTeam::HideSummonHPMPInfo(User* const user, CSummon* const summon) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || summon == nullptr || user->GetSocket() == nullptr || user->IsMyParty(summon)) {
        return;
    }
    user->GetSocket()->Send("cddcccdcd", 0x18, static_cast<int>(summon->m_nId), 0, 0, 2, 10, 0, 0x0c, 0);
}

void WOlympiadTeam::SendSummonInfo(WOlympiadTeam& oppTeam) {
    // Current L2Server 0x00901c24.
    L2_GUARD_FUNCTION();
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return;
    }
    User* const pUser = WOlympiadStuff::FindUserBySID(player->playerSID);
    if (pUser == nullptr) {
        return;
    }
    FOR_ALL_SUMMON_IN_SUMMON_LIST(pUser, summon, oppTeam.RecvSummonInfo(summon););
}

void WOlympiadTeam::HideSummonInfo(WOlympiadTeam& oppTeam) {
    // Current L2Server 0x00901e14.
    L2_GUARD_FUNCTION();
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return;
    }
    User* const pUser = WOlympiadStuff::FindUserBySID(player->playerSID);
    if (pUser == nullptr) {
        return;
    }
    FOR_ALL_SUMMON_IN_SUMMON_LIST(pUser, summon, oppTeam.RecvHideSummonInfo(summon););
}

void WOlympiadTeam::ObserverEnd() {
    // Current L2Server 0x008f60ac.
    L2_GUARD_FUNCTION();
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return;
    }
    User* const pUser = WOlympiadStuff::FindUserBySID(player->playerSID);
    if (pUser != nullptr && pUser->m_data->m_bGMInvisible && pUser->m_bIsObserving) {
        pUser->ObserverEnd();
    }
}

void WOlympiadTeam::SavePlayers() {
    // Current L2Server 0x008f6434.
    L2_GUARD_FUNCTION();
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return;
    }
    User* const pUser = WOlympiadStuff::FindUserBySID(player->playerSID);
    if (pUser != nullptr && !pUser->IsSaveCharacter()) {
        db.SendSaveCharacterInfo(pUser, false, true);
    }
}

int WOlympiadTeam::GetDamage() const {
    // Current L2Server 0x008f650c.
    L2_GUARD_FUNCTION();
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return 0;
    }
    User* const pUser = WOlympiadStuff::FindUserBySID(player->playerSID);
    return pUser == nullptr ? 0 : pUser->m_pWOlympiadUser->GetDamage();
}

bool WOlympiadTeam::IsValidInventory() const {
    // Current L2Server 0x008f8970.
    L2_GUARD_FUNCTION();
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return true;
    }
    User* const pUser = WOlympiadStuff::FindUserBySID(player->playerSID);
    if (pUser != nullptr && !pUser->m_pWOlympiadUser->ValidInventory()) {
        SendInvalidMessage(0x69b, pUser);
        return false;
    }
    return true;
}

bool WOlympiadTeam::IsValidSubjob() const {
    // Current L2Server 0x008f8a54.
    L2_GUARD_FUNCTION();
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return true;
    }
    User* const pUser = WOlympiadStuff::FindUserBySID(player->playerSID);
    if (pUser != nullptr && (pUser->m_data->IsSubJobClass() || pUser->m_data->IsDualClass())) {
        SendInvalidMessage(0x5dc, pUser);
        return false;
    }
    return true;
}

bool WOlympiadTeam::IsAlive() const {
    // Current L2Server 0x008f8b44.
    L2_GUARD_FUNCTION();
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return true;
    }
    User* const pUser = WOlympiadStuff::FindUserBySID(player->playerSID);
    if (pUser != nullptr && pUser->m_data->m_bIsAlive == 0) {
        SendInvalidMessage(0x742, pUser);
        return false;
    }
    return true;
}

bool WOlympiadTeam::IsActive() const {
    // Current L2Server 0x008f8c24.
    L2_GUARD_FUNCTION();
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return true;
    }
    User* const pUser = WOlympiadStuff::FindUserBySID(player->playerSID);
    if (pUser != nullptr && !pUser->IsActive()) {
        SendInvalidMessage(0x7ed, pUser);
        return false;
    }
    return true;
}

bool WOlympiadTeam::IsNotTraining() const {
    // Current L2Server 0x008f8d04.
    L2_GUARD_FUNCTION();
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return true;
    }
    User* const pUser = WOlympiadStuff::FindUserBySID(player->playerSID);
    if (pUser != nullptr && pUser->IsTraining()) {
        SendInvalidMessage(0x1812, pUser);
        return false;
    }
    return true;
}

bool WOlympiadTeam::IsNotCursedUser(int& param) const {
    // Current L2Server 0x00906f48.
    L2_GUARD_FUNCTION();
    param = 0;
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return true;
    }
    User* const pUser = WOlympiadStuff::FindUserBySID(player->playerSID);
    if (pUser != nullptr && CCursedWeaponMgr::inst().IsCursedUser(pUser)) {
        param = pUser->m_nCursedWeaponClassId;
        Send(
            "chccScd", 0x62, SystemMessageId::CHARACTER_CANNOT_JOIN_OLYMPIAD_OWNS_NAMED_OBJECT, 2, 0x0c, pUser->GetName(), 3,
            param
        );
        return false;
    }
    return true;
}

bool WOlympiadTeam::IsValidCondition(const GameValidCondition condition, int& itemId) const {
    L2_GUARD_FUNCTION();
    switch (condition) {
        case GVC_INVENTORY:
            return IsValidInventory();
        case GVC_SUBJOB:
            return IsValidSubjob();
        case GVC_VALID_TEAM:
            return IsValidTeam();
        case GVC_NOT_CURSED:
            return IsNotCursedUser(itemId);
        case GVC_ALIVE:
            return IsAlive();
        case GVC_ACTIVE:
            return IsActive();
        case GVC_FISHING:
            return IsFishingZone();
        case GVC_NOT_TRAINING:
            return IsNotTraining();
        default:
            return false;
    }
}

bool WOlympiadTeam::IsValidCondition(const GameValidCondition condition) const {
    L2_GUARD_FUNCTION();
    int itemId = 0;
    return IsValidCondition(condition, itemId);
}

bool WOlympiadTeam::IsDisconnected() const {
    L2_GUARD_FUNCTION();
    CSPointer<WOlympiadMemberInfo> member = GetMember();
    return member && member->GetPrepareValidCondition() == GVC_VALID_TEAM;
}

bool WOlympiadTeam::IsPeerDisconnected() const {
    L2_GUARD_FUNCTION();
    CSPointer<WOlympiadMemberInfo> member = GetMember();
    return member && member->IsPeerDisconnected();
}

void WOlympiadTeam::SendCancelMatch(const GameValidCondition condition, const int itemId) const {
    L2_GUARD_FUNCTION();
    WOlympiadServiceUtil::SendOSN_CANCEL_MATCH(GetMemberKey(), condition, itemId);
    switch (condition) {
        case GVC_INVENTORY:
        case GVC_SUBJOB:
        case GVC_ALIVE:
        case GVC_ACTIVE:
        case GVC_FISHING:
        case GVC_NOT_TRAINING:
            SendSystemMessage(SystemMessageId::MATCH_CANCELLED_OPPONENT_NOT_ELIGIBLE);
            break;
        case GVC_VALID_TEAM:
            SendSystemMessage(SystemMessageId::MATCH_CANCELLED_OPPONENT_LOGGED_OUT);
            break;
        case GVC_NOT_CURSED:
            Send("chccd", 0x62, SystemMessageId::OLYMPIAD_CANCELLED_OPPONENT_ACQUIRED_NAMED_OBJECT, 1, 3, itemId);
            break;
        default:
            break;
    }
}

const wchar_t* WOlympiadTeam::GetPartyMasterName() const {
    L2_GUARD_FUNCTION();
    CSPointer<WOlympiadMemberInfo> member = GetMember();
    if (!member) {
        return nullptr;
    }
    // The target returns the UTF-16 xwstring buffer directly. Host wchar_t
    // is 32-bit on the portable build, so keep the same pointer-returning
    // contract through a thread-local host-wide view.
    thread_local std::wstring partyMasterName;
    partyMasterName = Utf16ToWide(member->GetWorldCharName());
    return partyMasterName.c_str();
}

std::uint32_t WOlympiadTeam::GetPledgeSID() const {
    // Current L2Server 0x008fb730. The packet carrier stores the pledge
    // database id, so resolve the live pledge in its original world before
    // returning the CObject smart id.
    L2_GUARD_FUNCTION();
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    if (player == nullptr) {
        return 0;
    }
    CSPointer<CPledge> pledge = CPledge::FindPledge(player->playerWorldId, player->pledgeId);
    return pledge ? pledge->Id() : 0;
}

ClassType WOlympiadTeam::GetClassType() const {
    L2_GUARD_FUNCTION();
    CSPointer<WOlympiadMemberInfo> member = GetMember();
    return member ? member->GetClassType() : static_cast<ClassType>(-1);
}

int WOlympiadTeam::GetWorldID() const {
    L2_GUARD_FUNCTION();
    CSPointer<WOlympiadMemberInfo> member = GetMember();
    return member ? member->GetWorldID() : 0;
}

void WOlympiadTeam::Set(const WOlympiadTeam* const other) {
    L2_GUARD_FUNCTION();
    if (other == nullptr) {
        return;
    }
    m_GameRule = other->m_GameRule;
    m_PartyMasterIndex = other->m_PartyMasterIndex;
    m_ChangePoint = other->m_ChangePoint;
    m_PrevOlympiadPoint = other->m_PrevOlympiadPoint;
    m_spMember = other->m_spMember;
    m_PlayerInfo = other->m_PlayerInfo;
}

void WOlympiadTeam::SetFieldID(const int fieldId) {
    CSPointer<WOlympiadMemberInfo> member = GetMember();
    if (member) {
        member->SetFieldID(fieldId);
    }
}

int WOlympiadTeam::GetOlympiadPoint() const {
    L2_GUARD_FUNCTION();
    CSPointer<WOlympiadMemberInfo> member = GetMember();
    if (!member) {
        return 0;
    }
    const SharedNoblessInfo* const info = WOlympiadUsers::inst().GetNoblessInfo(member->GetKey());
    if (info == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]not nobless: %d, %d",
        //     kOlympiadServiceSource,
        //     0x5f4,
        //     member->GetWorldID(),
        //     member->GetDBID())
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]not nobless: %d, %d",
        //     kOlympiadServiceSource,
        //     0x5f4,
        //     member->GetWorldID(),
        //     member->GetDBID())
        L2_LOG_ERROR(
            Log,
            "Cannot read Olympiad points: nobless information is missing for world %d, character DB ID %d; returning zero.",
            member->GetWorldID(), member->GetDBID()
        );
        return 0;
    }
    return info->nOlympiadPoint;
}

bool WOlympiadTeam::IsMatchingPointLoaded() const {
    CSPointer<WOlympiadMemberInfo> member = GetMember();
    return member && member->IsLoadedMatchingPoint();
}

void WOlympiadTeam::SetWinningPoint(const std::int64_t point) {
    CSPointer<WOlympiadMemberInfo> member = GetMember();
    if (member) {
        member->SetWinningPoint(point, member->GetWinningRate());
    }
}

int WOlympiadTeam::GetPrevOlympiadPoint() const { return m_PrevOlympiadPoint; }

int WOlympiadTeam::EncodeMatchResultToBuffer(const int opponentDamage, char* const buffer, const int capacity) const {
    // Current L2Server 0x0090a364.
    L2_GUARD_FUNCTION();
    int point = GetOlympiadPoint();
    if (m_PrevOlympiadPoint == point) {
        point += m_ChangePoint;
    }
    point = std::clamp(point, 10, 10000);
    return ::Assemble(
        buffer, capacity, "SSdddddd", target_utf16::FromRecoveredWindowsWide(GetPartyMasterName()).c_str(),
        target_utf16::FromRecoveredWindowsWide(GetPledageName()).c_str(), static_cast<int>(GetClassType()), GetWorldID(),
        opponentDamage, point, m_ChangePoint, GetPledgeSID()
    );
}

void WOlympiadTeam::SetPrevOlympiadPoint(const int point) { m_PrevOlympiadPoint = point; }

void WOlympiadTeam::SetChangePoint(const int point) { m_ChangePoint = point; }

void WOlympiadTeam::EscapeFromOlympiad() {
    // Current L2Server 0x0090d4e0.
    L2_GUARD_FUNCTION();
    const WOlympiadPlayerInfo* const player = GetPlayerInfo();
    User* const pUser = player == nullptr ? nullptr : WOlympiadStuff::FindUserBySID(player->playerSID);
    if (pUser == nullptr) {
        return;
    }
    pUser->m_pWOlympiadUser->EscapeFromOlympiad();
    WOlympiadService::inst().ReturnToOrigin(CSPointer<User>(pUser, ObjectRefType::FunctionChain));
}

void WOlympiadTeam::Win(WOlympiadTeam& rival, const WOlympiadGameResultInfo& result) {
    // Current L2Server 0x0090d5d8. The transfer calculation is kept
    // even though its return value is observably discarded by the target.
    L2_GUARD_FUNCTION();
    (void)WOlympiadDefaultSetting::inst().GetTransferingPoint(result.gameRule, rival.GetOlympiadPoint(), GetOlympiadPoint());
    WOlympiadDefaultSetting::BonusItemData* const bonus = WOlympiadDefaultSetting::inst().GetBonusItemData(result.gameRule);

    CSPointer<WOlympiadMemberInfo> winner = GetMember();
    CSPointer<WOlympiadMemberInfo> loser = rival.GetMember();
    if (!winner || !loser) {
        return;
    }
    const int winnerPoint = static_cast<int>(winner->GetWinningPoint());
    const int loserPoint = static_cast<int>(loser->GetWinningPoint());
    m_PrevOlympiadPoint = GetOlympiadPoint();
    rival.m_PrevOlympiadPoint = rival.GetOlympiadPoint();
    m_ChangePoint = winnerPoint;
    rival.m_ChangePoint = loserPoint;

    const int rewardItem = WOlympiadDefaultSetting::inst().GetRewardItem();
    WOlympiadService::inst().SaveOlympiadRecord(
        winner->GetKey(), loser->GetKey(), winnerPoint, rewardItem, result.nWinnerItemCount, loserPoint, rewardItem,
        result.nLoserItemCount, result.elapsedTime, 0, result.gameRule
    );

    CSPointer<User> winnerUser = g_UserMap.FindObject(static_cast<int>(GetUserSID()));
    if (winnerUser && winnerUser->GetSocket() != nullptr) {
        winnerUser->GetSocket()->Send(
            "chccSch", 0x62, SystemMessageId::CHARACTER_GAINED_OLYMPIAD_POINTS, 2, 0x0c, winner->GetCharName().c_str(), 0x15,
            static_cast<int>(static_cast<short>(winnerPoint))
        );
        WOlympiadService::inst().SaveLastMatchResult(winner->GetKey(), GRE_WIN, result.nWinnerItemCount);
    }
    CSPointer<User> loserUser = g_UserMap.FindObject(static_cast<int>(rival.GetUserSID()));
    if (loserUser && loserUser->GetSocket() != nullptr) {
        loserUser->GetSocket()->Send(
            "chccSch", 0x62, SystemMessageId::CHARACTER_LOST_OLYMPIAD_POINTS, 2, 0x0c, loser->GetCharName().c_str(), 0x15,
            static_cast<int>(static_cast<short>(loserPoint))
        );
        WOlympiadService::inst().SaveLastMatchResult(loser->GetKey(), GRE_LOSE, result.nLoserItemCount);
    }

    const int winnerOlympiadPoint = GetOlympiadPoint();
    const int loserOlympiadPoint = rival.GetOlympiadPoint();
    Write_LOG_WOLYMPIAD_WIN_MATCH_InHost(
        winner, winnerOlympiadPoint, loser, winnerPoint, static_cast<unsigned int>(rewardItem), result.nWinnerItemCount,
        result
    );
    Write_LOG_WOLYMPIAD_LOSE_MATCH_InHost(
        loser, loserOlympiadPoint, winner, loserPoint, static_cast<unsigned int>(rewardItem), result.nLoserItemCount, result
    );
    if (WOlympiadServiceConst::_MATCHING_POINT_MODE) {
        const int winnerFinalPoint =
            runtime::AddInt32Wrapping(winnerOlympiadPoint, static_cast<int>(winner->GetMatchingPoint().nWinningPoint));
        const int loserFinalPoint =
            runtime::AddInt32Wrapping(loserOlympiadPoint, static_cast<int>(loser->GetMatchingPoint().nWinningPoint));
        Write_LOG_WOLYMPIAD_WIN_MATCH_POINT_InHost(winner, winnerFinalPoint, loser, loserFinalPoint, result);
        Write_LOG_WOLYMPIAD_LOSE_MATCH_POINT_InHost(loser, loserFinalPoint, winner, winnerFinalPoint, result);
    }
    if (bonus != nullptr && (bonus->itemClassId > 0 || bonus->itemClassIdForLoser != 0)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Bonus Item is not available: %d, %d",
        //     kOlympiadServiceSource,
        //     0x832,
        //     bonus->itemClassId,
        //     bonus->itemClassIdForLoser)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Bonus Item is not available: %d, %d",
        //     kOlympiadServiceSource,
        //     0x832,
        //     bonus->itemClassId,
        //     bonus->itemClassIdForLoser)
        L2_LOG_WARN(
            Log,
            "Olympiad bonus items are configured but unavailable in this result handler: winner item class %d, loser item "
            "class %d.",
            bonus->itemClassId, bonus->itemClassIdForLoser
        );
    }
}

void WOlympiadTeam::Draw(WOlympiadTeam& rival, const WOlympiadGameResultInfo& result) {
    // Current L2Server 0x0090dc3c.
    L2_GUARD_FUNCTION();
    CSPointer<WOlympiadMemberInfo> first = GetMember();
    CSPointer<WOlympiadMemberInfo> second = rival.GetMember();
    if (!first || !second) {
        return;
    }
    const int firstPoint = static_cast<int>(first->GetWinningPoint());
    const int secondPoint = static_cast<int>(second->GetWinningPoint());
    m_PrevOlympiadPoint = GetOlympiadPoint();
    rival.m_PrevOlympiadPoint = rival.GetOlympiadPoint();
    m_ChangePoint = firstPoint;
    rival.m_ChangePoint = secondPoint;
    WOlympiadService::inst().SaveOlympiadRecord(
        first->GetKey(), second->GetKey(), firstPoint, 0, 0, secondPoint, 0, 0, result.elapsedTime, 1, result.gameRule
    );

    const auto notify =
        [](WOlympiadTeam& team, const CSPointer<WOlympiadMemberInfo>& member, const int point, const int itemCount) {
            CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(team.GetUserSID()));
            if (!user || user->GetSocket() == nullptr) {
                return;
            }
            user->GetSocket()->Send(
                "chccSch", 0x62, SystemMessageId::CHARACTER_LOST_OLYMPIAD_POINTS, 2, 0x0c, member->GetCharName().c_str(),
                0x15, static_cast<int>(static_cast<short>(point))
            );
            WOlympiadService::inst().SaveLastMatchResult(member->GetKey(), GRE_DRAW, itemCount);
        };
    notify(*this, first, firstPoint, result.nWinnerItemCount);
    notify(rival, second, secondPoint, result.nLoserItemCount);

    const int firstOlympiadPoint = GetOlympiadPoint();
    const int secondOlympiadPoint = rival.GetOlympiadPoint();
    Write_LOG_WOLYMPIAD_DRAW_MATCH_InHost(first, firstOlympiadPoint, firstPoint, second, result);
    Write_LOG_WOLYMPIAD_DRAW_MATCH_InHost(second, secondOlympiadPoint, secondPoint, first, result);
    if (WOlympiadServiceConst::_MATCHING_POINT_MODE) {
        const int firstFinalPoint =
            runtime::AddInt32Wrapping(firstOlympiadPoint, static_cast<int>(first->GetMatchingPoint().nWinningPoint));
        const int secondFinalPoint =
            runtime::AddInt32Wrapping(secondOlympiadPoint, static_cast<int>(second->GetMatchingPoint().nWinningPoint));
        Write_LOG_WOLYMPIAD_DRAW_MATCH_POINT_InHost(first, firstFinalPoint, second, secondFinalPoint, result);
        Write_LOG_WOLYMPIAD_DRAW_MATCH_POINT_InHost(second, secondFinalPoint, first, firstFinalPoint, result);
    }
}

void WOlympiadTeam::PayPenalty(
    WOlympiadTeam& rival, const WOlympiadGameResultInfo& result, const GameValidCondition condition
) {
    // Current L2Server 0x0090e108.
    L2_GUARD_FUNCTION();
    const int currentPoint = GetOlympiadPoint();
    const int penalty = WOlympiadDefaultSetting::inst().GetPenaltyPoint(result.gameRule, currentPoint);
    const std::CDualKey<int, int> selfKey = GetMemberKey();
    const std::CDualKey<int, int> rivalKey = rival.GetMemberKey();
    WOlympiadService::inst().SaveOlympiadPenaltyLog(
        selfKey, penalty, currentPoint - penalty, result.fieldId, static_cast<int>(result.gameRule),
        static_cast<int>(condition)
    );
    WOlympiadService::inst().SaveOlympiadRecord(rivalKey, selfKey, 0, 0, 0, -penalty, 0, 0, 0, 2, result.gameRule);
    WOlympiadServiceUtil::SendOSN_OLYMPIAD_POINT(selfKey, -penalty);
}

WOlympiadMatchMaker::WOlympiadMatchMaker()
    // Current compiler-expanded constructor 0x009062a0.
    :
    m_TeamList{}, m_RuleSelected{}, m_alignment00c7To00c8{}, m_EntryCount{} {
    L2_GUARD_FUNCTION();
    EntrySelectInit();
}

WOlympiadMatchMaker::~WOlympiadMatchMaker() = default;

void WOlympiadMatchMaker::EntrySelectInit() {
    // Current L2Server 0x008f4c08 clears exactly 39 rule-selection bytes.
    L2_GUARD_FUNCTION();
    std::fill(std::begin(m_RuleSelected), std::end(m_RuleSelected), false);
}

void WOlympiadMatchMaker::AddEntry(WOlympiadTeam* const team, const GameRuleType gameRule) {
    // Current L2Server 0x009053f8.
    L2_GUARD_FUNCTION();
    if (team == nullptr || gameRule < GRT_TEAM || gameRule >= GRT_MAX) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0xaa3,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0xaa3,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_ERROR(
            Log, "Cannot add Olympiad matchmaking entry: team available %s, game rule %d (supported range %d..%d).",
            team != nullptr ? "yes" : "no", static_cast<int>(gameRule), static_cast<int>(GRT_TEAM),
            static_cast<int>(GRT_MAX) - 1
        );
        return;
    }

    L2_GUARD(m_EntryMapLock);
    auto& entries = m_TeamList[static_cast<int>(gameRule)];
    const auto offset = static_cast<std::ptrdiff_t>(genrand64_real2() * static_cast<double>(entries.size() + 1U));
    auto position = entries.begin();
    std::advance(position, offset);
    entries.insert(position, CSPointer<WOlympiadTeam>(team, ObjectRefType::FunctionChain));
    _AddEntryCount(gameRule, 1);
}

void WOlympiadMatchMaker::RemoveEntry(const std::CDualKey<int, int> key) {
    // Current L2Server 0x00904a18.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EntryMapLock);
    for (int rule = GRT_CLASSLESS; rule < GRT_MAX; ++rule) {
        auto& entries = m_TeamList[rule];
        for (auto position = entries.begin(); position != entries.end(); ++position) {
            if (*position && (*position)->IsRegisteredUser(key)) {
                _AddEntryCount(static_cast<GameRuleType>(rule), -1);
                (*position)->UnregisterAll();
                entries.erase(position);
                return;
            }
        }
    }
}

void WOlympiadMatchMaker::ClearAllEntry() {
    // Current L2Server 0x00904c20.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EntryMapLock);
    for (int rule = GRT_CLASSLESS; rule < GRT_MAX; ++rule) {
        for (const CSPointer<WOlympiadTeam>& team : m_TeamList[rule]) {
            if (team) {
                team->UnregisterAll();
            }
        }
        m_TeamList[rule].clear();
    }
    std::fill(std::begin(m_EntryCount), std::end(m_EntryCount), 0);
    EntrySelectInit();
    for (int rule = GRT_CLASSLESS; rule < GRT_MAX; ++rule) {
        WOlympiadServiceUtil::SetEntryCount(static_cast<GameRuleType>(rule), 0);
    }
}

int WOlympiadMatchMaker::GetClassGroupEntryCount(const int group) const {
    // Current L2Server 0x008f4cc4 supports only the classless group.
    L2_GUARD_FUNCTION();
    if (group == GRT_CLASSLESS) {
        return m_EntryCount[GRT_CLASSLESS];
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
    //     kOlympiadServiceSource,
    //     0xb01,
    //     L"WO_ASSERT_EQUAL_RETURN")
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
    //     kOlympiadServiceSource,
    //     0xb01,
    //     L"WO_ASSERT_EQUAL_RETURN")
    L2_LOG_ERROR(
        Log, "Cannot read Olympiad entry count for group %d: only classless group %d is supported; returning zero.", group,
        static_cast<int>(GRT_CLASSLESS)
    );
    return 0;
}

void WOlympiadMatchMaker::AnnounceToEntry(const std::uint32_t messageId, const GameRuleType gameRule) const {
    // Current L2Server 0x00902fa0.
    L2_GUARD_FUNCTION();
    auto* const self = const_cast<WOlympiadMatchMaker*>(this);
    L2_GUARD(self->m_EntryMapLock);
    for (int rule = GRT_CLASSLESS; rule < GRT_MAX; ++rule) {
        if (gameRule != GRT_MAX && rule != gameRule) {
            continue;
        }
        for (const CSPointer<WOlympiadTeam>& team : self->m_TeamList[rule]) {
            if (team) {
                WOlympiadServiceUtil::SendOSN_SYSTEM_MESSAGE_NO(team->GetMemberKey(), static_cast<int>(messageId));
            }
        }
    }
}

bool WOlympiadMatchMaker::IsRegisteredUser(const std::CDualKey<int, int> key) const {
    auto* const self = const_cast<WOlympiadMatchMaker*>(this);
    L2_GUARD(self->m_EntryMapLock);
    for (int rule = GRT_CLASSLESS; rule < GRT_MAX; ++rule) {
        for (const CSPointer<WOlympiadTeam>& team : self->m_TeamList[rule]) {
            if (team && team->IsRegisteredUser(key)) {
                return true;
            }
        }
    }
    return false;
}

bool WOlympiadMatchMaker::CheckEntryByGameRule(const GameRuleType gameRule) const {
    // Current L2Server 0x008f6678.
    L2_GUARD_FUNCTION();
    if (gameRule != GRT_CLASSLESS) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0xbab,
        //     L"WO_ASSERT_EQUAL_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0xbab,
        //     L"WO_ASSERT_EQUAL_RETURN")
        L2_LOG_ERROR(
            Log, "Cannot check Olympiad matchmaking entries for game rule %d: only classless rule %d is supported.",
            static_cast<int>(gameRule), static_cast<int>(GRT_CLASSLESS)
        );
        return false;
    }
    return WOlympiadDefaultSetting::inst().GetMatchMinCount(gameRule) <= GetClassGroupEntryCount(GRT_CLASSLESS);
}

void WOlympiadMatchMaker::AddEntryCount(const GameRuleType gameRule, const int count) {
    // Current L2Server 0x008fbbac.
    L2_GUARD_FUNCTION();
    if (gameRule != GRT_CLASSLESS) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0xb90,
        //     L"WO_ASSERT_EQUAL")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0xb90,
        //     L"WO_ASSERT_EQUAL")
        L2_LOG_ERROR(
            Log, "Cannot change Olympiad entry count by %d for game rule %d: only classless rule %d is supported.", count,
            static_cast<int>(gameRule), static_cast<int>(GRT_CLASSLESS)
        );
        return;
    }
    m_EntryCount[GRT_CLASSLESS] += count;
    WOlympiadServiceUtil::SetEntryCount(GRT_CLASSLESS, m_EntryCount[GRT_CLASSLESS]);
}

void WOlympiadMatchMaker::_AddEntryCount(const GameRuleType gameRule, const int count) {
    // Current L2Server 0x008fbca4 duplicates the public body.
    L2_GUARD_FUNCTION();
    if (gameRule != GRT_CLASSLESS) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0xb9e,
        //     L"WO_ASSERT_EQUAL")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0xb9e,
        //     L"WO_ASSERT_EQUAL")
        L2_LOG_ERROR(
            Log, "Cannot change Olympiad entry count by %d for game rule %d: only classless rule %d is supported.", count,
            static_cast<int>(gameRule), static_cast<int>(GRT_CLASSLESS)
        );
        return;
    }
    m_EntryCount[GRT_CLASSLESS] += count;
    WOlympiadServiceUtil::SetEntryCount(GRT_CLASSLESS, m_EntryCount[GRT_CLASSLESS]);
}

WOlympiadService::OlympiadMembers::OlympiadMembers() = default;
WOlympiadService::OlympiadMembers::~OlympiadMembers() = default;

void WOlympiadService::OlympiadMembers::AddMember(CSPointer<WOlympiadMemberInfo> member) {
    // Current L2Server 0x0090921c.
    L2_GUARD_FUNCTION();
    if (!member) {
        return;
    }
    L2_GUARD(m_Lock);
    const std::CDualKey<int, int> key = member->GetKey();
    m_Members[key.m_PrimaryKey][key.m_SecondaryKey] = member;
    m_MemberKeys.insert(key);
}

void WOlympiadService::OlympiadMembers::RemoveMember(const std::CDualKey<int, int> key) {
    // Current L2Server 0x00909434.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    auto world = m_Members.find(key.m_PrimaryKey);
    if (world != m_Members.end()) {
        world->second.erase(key.m_SecondaryKey);
    }
    m_MemberKeys.erase(key);
}

bool WOlympiadService::OlympiadMembers::IsMember(const std::CDualKey<int, int> key) const {
    L2_GUARD_FUNCTION();
    auto* const self = const_cast<OlympiadMembers*>(this);
    L2_GUARD(self->m_Lock);
    return self->m_MemberKeys.count(key) != 0;
}

CSPointer<WOlympiadMemberInfo> WOlympiadService::OlympiadMembers::FindMember(const std::CDualKey<int, int> key) const {
    // Current L2Server 0x008fedfc.
    L2_GUARD_FUNCTION();
    auto* const self = const_cast<OlympiadMembers*>(this);
    L2_GUARD(self->m_Lock);
    const auto world = self->m_Members.find(key.m_PrimaryKey);
    if (world == self->m_Members.end()) {
        return {};
    }
    const auto member = world->second.find(key.m_SecondaryKey);
    return member == world->second.end() ? CSPointer<WOlympiadMemberInfo>{} : member->second;
}

void WOlympiadService::OlympiadMembers::Clear() {
    // Current L2Server 0x00908db0.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_Members.clear();
    m_MemberKeys.clear();
}

std::vector<CSPointer<WOlympiadMemberInfo>> WOlympiadService::OlympiadMembers::GetMembers(const int worldId) const {
    // Current L2Server 0x00906374.
    L2_GUARD_FUNCTION();
    std::vector<CSPointer<WOlympiadMemberInfo>> result;
    auto* const self = const_cast<OlympiadMembers*>(this);
    L2_GUARD(self->m_Lock);
    const auto world = self->m_Members.find(worldId);
    if (world != self->m_Members.end()) {
        result.reserve(world->second.size());
        for (const auto& member : world->second) {
            result.push_back(member.second);
        }
    }
    return result;
}

WOlympiadService::MatchResults::MatchResults() = default;
WOlympiadService::MatchResults::~MatchResults() = default;

bool WOlympiadService::MatchResults::Pop(const std::CDualKey<int, int> key, GameResultEnum& result, int& itemCount) {
    // Current L2Server 0x009007d8.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    const auto position = m_mapMatchResults.find(key);
    if (position == m_mapMatchResults.end()) {
        return false;
    }
    result = position->second.eResult;
    itemCount = position->second.nItemCount;
    m_mapMatchResults.erase(position);
    return true;
}

void WOlympiadService::MatchResults::Push(
    const std::CDualKey<int, int> key, const GameResultEnum result, const int itemCount
) {
    // Current L2Server 0x00904f74.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_mapMatchResults[key] = Node{result, itemCount};
}

WOlympiadUsers::WOlympiadUsers() {}

WOlympiadUsers::~WOlympiadUsers() = default;

void WOlympiadUsers::Init() {
    // Current L2Server 0x008f4da0. Both outcome messages use
    // LOG_ERROR. The success string carries the target's NO_ERROR marker.
    L2_GUARD_FUNCTION();
    db.RequestLoadAllWOlympiadUser(true, false);
    if (g_JobFinishSignal.m_nMustLoadWOlympiadUser > 0) {
        if (!g_JobFinishSignal.WaitFinish(JFT_LOAD_WORLD_OLYMPIAD_USER, 0x5a)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]Problem occurred in Loading World Olympiad User info",
            //     kOlympiadServiceSource,
            //     0xc3d)
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]Problem occurred in Loading World Olympiad User info",
            //     kOlympiadServiceSource,
            //     0xc3d)
            L2_LOG_ERROR(Log, "Waiting for World Olympiad user data failed; initialization continues.");
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]Load Success[World Olympiad User]",
            //     kOlympiadServiceSource,
            //     0xc41)
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]Load Success[World Olympiad User]",
            //     kOlympiadServiceSource,
            //     0xc41)
            L2_LOG_INFO(Log, "World Olympiad user data loading completed.");
        }
    }
}

void WOlympiadUsers::LoadMatchContFromDB() {
    // Current L2Server 0x008f4e98. Both outcomes use
    // LOG_ERROR, including the NO_ERROR success record.
    L2_GUARD_FUNCTION();
    db.RequestLoadWOlympiadMatchCount();
    if (g_JobFinishSignal.m_nMustLoadMatchCount > 0) {
        if (!g_JobFinishSignal.WaitFinish(JFT_LOAD_NOBLESS_MATCH_COUNT, 0x5a)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]Problem occurred in Loading Nobless Match Count info",
            //     kOlympiadServiceSource,
            //     0xc56)
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]Problem occurred in Loading Nobless Match Count info",
            //     kOlympiadServiceSource,
            //     0xc56)
            L2_LOG_ERROR(Log, "Waiting for Olympiad match counts failed; initialization continues.");
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]Load Success[Nobless Match Count]",
            //     kOlympiadServiceSource,
            //     0xc5a)
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]Load Success[Nobless Match Count]",
            //     kOlympiadServiceSource,
            //     0xc5a)
            L2_LOG_INFO(Log, "Olympiad match count loading completed.");
        }
    }
}

int WOlympiadUsers::GetNoblessIndex(const std::CDualKey<int, int> key) const {
    // Current L2Server 0x00900380.
    L2_GUARD_FUNCTION();
    auto* const self = const_cast<WOlympiadUsers*>(this);
    L2_GUARD(self->m_LockNobless);
    const auto position = self->m_mapNoblessToIndex.find(key);
    return position == self->m_mapNoblessToIndex.end() ? -1 : position->second;
}

bool WOlympiadUsers::IsRegistered(const std::CDualKey<int, int> key) const {
    // Current L2Server 0x00900638.
    L2_GUARD_FUNCTION();
    return GetNoblessIndex(key) != -1;
}

const SharedNoblessInfo* WOlympiadUsers::GetNoblessInfo(const std::CDualKey<int, int> key) const {
    // Current L2Server 0x009022f0.
    L2_GUARD_FUNCTION();
    const int index = GetNoblessIndex(key);
    return index == -1 ? nullptr : SharedNoblessInfo::GetInfo(index);
}

void WOlympiadUsers::SetNoblessInfo(
    const std::CDualKey<int, int> key,
    const int heroType,
    const int winCount,
    const int previousPoint,
    const int olympiadPoint,
    const int matchCount,
    const int olympiadWinCount,
    const int olympiadLoseCount,
    const int olympiadTradePoint,
    const bool update
) {
    // Current L2Server 0x009087f8.
    L2_GUARD_FUNCTION();
    CSPointer<User> user;
    int noblessIndex = 0;
    // int previousHeroType = 0
    int previousHeroType = HERO_NONE;
    bool heroChanged = false;
    {
        L2_GUARD(m_LockNobless);
        auto position = m_mapNoblessToIndex.find(key);
        if (position == m_mapNoblessToIndex.end()) {
            noblessIndex = static_cast<int>(m_mapNoblessToIndex.size());
            position = m_mapNoblessToIndex.emplace(key, noblessIndex).first;
        } else {
            noblessIndex = position->second;
        }

        SharedNoblessInfo* const info = SharedNoblessInfo::GetInfo(noblessIndex);
        user = WOlympiadStuff::FindUserByKey(key);
        info->nCharSid = user ? static_cast<int>(user->GetServerId()) : 0;
        info->nCharDbId = key.m_SecondaryKey;
        previousHeroType = info->nHeroType;
        info->nHeroType = heroType;
        info->nWinCount = winCount;
        info->nPreviousPoint = previousPoint;
        info->nOlympiadPoint = olympiadPoint;
        info->nMatchCount = matchCount;
        info->nOlympiadWinCount = olympiadWinCount;
        info->nOlympiadLoseCount = olympiadLoseCount;
        info->nOlympiadTradePoint = olympiadTradePoint;
        heroChanged = previousHeroType != heroType;
        if (heroChanged) {
            auto& heroes = m_mapHeroCache[key.m_PrimaryKey];
            if (HeroType_IsHero(heroType)) {
                heroes.insert(key);
            } else {
                heroes.erase(key);
            }
        }
    }

    if (!user) {
        return;
    }
    SetUserNoblessData(user, noblessIndex);
    if (heroChanged) {
        if (!user->IsHero()) {
            user->ImpoundHeroWeapon();
        }
        user->ValidateHeroSkills();
    }
    if (update) {
        user->ImpoundHeroEntryItem(false);
        user->ImpoundHeroEntryItem(true);
        user->ValidateHeroSkills();
    }
    // if (heroChanged && heroType == 0) {
    if (heroChanged && heroType == HERO_NONE) {
        if (CPledge* const pledge = user->GetPledge()) {
            pledge->ApplyPledgeSkillsTo(user.get(), false, false);
            user->m_nParamChanged |= std::int64_t{PCB_USER};
        }
    }
}

int WOlympiadUsers::GetMatchCountByStep(
    const std::CDualKey<int, int> key, const int season, const int step, const GameRuleType rule
) const {
    // Current L2Server 0x009006d8.
    L2_GUARD_FUNCTION();
    if (rule < GRT_TEAM || rule >= GRT_MAX) {
        return 0;
    }
    auto* const self = const_cast<WOlympiadUsers*>(this);
    L2_GUARD(self->m_LockNobless);
    const auto position = self->m_mapWeeklyMatchCount.find(key);
    if (position == self->m_mapWeeklyMatchCount.end() || position->second.nSeason != season ||
        position->second.nStep != step) {
        return 0;
    }
    return position->second.nMatchCount[static_cast<int>(rule)];
}

int WOlympiadUsers::GetRemainMatchCount(const std::CDualKey<int, int> key) const {
    // Current L2Server 0x0090e5d4.
    L2_GUARD_FUNCTION();
    WOlympiadService::inst();
    const int step = SharedOlympiadInfo::GetInfo()->m_nStep;
    WOlympiadService::inst();
    const int season = SharedOlympiadInfo::GetInfo()->m_nSeason;
    const GameRuleType rule = WOlympiadService::inst().GetValidRule();
    return WOlympiadDefaultSetting::inst().GetMatchLimit(rule) - GetMatchCountByStep(key, season, step, rule);
}

void WOlympiadUsers::IncMatchCount(const std::CDualKey<int, int> key, const GameRuleType rule, const int step) {
    // Current L2Server 0x0090e280. The current image accepts the
    // rule as an array index without an extra donor-era PCH gate.
    L2_GUARD_FUNCTION();
    const SharedOlympiadInfo* const shared = SharedOlympiadInfo::GetInfo();
    int updatedCount = 0;
    {
        L2_GUARD(m_LockNobless);
        WeeklyOlympiadMatchCount& counts = m_mapWeeklyMatchCount[key];
        if (counts.nSeason != shared->m_nSeason || counts.nStep != shared->m_nStep) {
            counts.nSeason = shared->m_nSeason;
            counts.nStep = shared->m_nStep;
            std::fill(std::begin(counts.nMatchCount), std::end(counts.nMatchCount), 0);
        }
        counts.nMatchCount[static_cast<int>(rule)] += step;
        updatedCount = counts.nMatchCount[static_cast<int>(rule)];
    }

    CDX_WOLYMPIAD_SAVE_MATCH_COUNT request(
        key.m_PrimaryKey, key.m_SecondaryKey, shared->m_nSeason, shared->m_nStep, static_cast<int>(rule), updatedCount
    );
    db.RequestTPacketProcess(g_nWorldId, 0x4a, static_cast<int>(sizeof(request)), request);
}

bool WOlympiadUsers::CanRegistMatch(
    const std::CDualKey<int, int> key, const int season, const int step, MatchLimitType& limit, const GameRuleType rule
) {
    // Current L2Server 0x0090045c.
    L2_GUARD_FUNCTION();
    if (rule < GRT_TEAM || rule >= GRT_MAX) {
        return false;
    }
    L2_GUARD(m_LockNobless);
    auto position = m_mapWeeklyMatchCount.find(key);
    if (position == m_mapWeeklyMatchCount.end()) {
        return true;
    }

    WeeklyOlympiadMatchCount& counts = position->second;
    if (counts.nSeason != season || counts.nStep != step) {
        counts.nSeason = season;
        counts.nStep = step;
        std::fill(std::begin(counts.nMatchCount), std::end(counts.nMatchCount), 0);
    }

    if (counts.nMatchCount[static_cast<int>(rule)] >= WOlympiadDefaultSetting::inst().GetMatchLimit(rule)) {
        limit = MLT_GAMERULE;
        return false;
    }

    int total = 0;
    for (const int count : counts.nMatchCount) {
        total += count;
    }
    if (total >= WOlympiadDefaultSetting::inst().GetMatchLimit(GRT_MAX)) {
        limit = MLT_TOTAL;
        return false;
    }
    return true;
}

void WOlympiadUsers::SetMatchCount(
    const std::CDualKey<int, int> key, const int season, const int step, const GameRuleType rule, const int count
) {
    // Current L2Server 0x0090e460.
    L2_GUARD_FUNCTION();
    if (rule < GRT_TEAM || rule >= GRT_MAX) {
        return;
    }
    const SharedOlympiadInfo* const shared = SharedOlympiadInfo::GetInfo();
    if (season != shared->m_nSeason || step != shared->m_nStep) {
        return;
    }
    L2_GUARD(m_LockNobless);
    WeeklyOlympiadMatchCount& value = m_mapWeeklyMatchCount[key];
    value.nSeason = season;
    value.nStep = step;
    value.nMatchCount[static_cast<int>(rule)] = count;
}

std::vector<std::CDualKey<int, int>> WOlympiadUsers::GetHeroes(const int season) const {
    // Current L2Server 0x00904de4.
    L2_GUARD_FUNCTION();
    std::vector<std::CDualKey<int, int>> result;
    auto* const self = const_cast<WOlympiadUsers*>(this);
    L2_GUARD(self->m_LockNobless);
    const auto position = self->m_mapHeroCache.find(season);
    if (position == self->m_mapHeroCache.end()) {
        return result;
    }
    result.reserve(position->second.size());
    for (const std::CDualKey<int, int>& key : position->second) {
        result.push_back(key);
    }
    return result;
}

void WOlympiadUsers::SendMatchCount(const std::CDualKey<int, int> receiver, const std::CDualKey<int, int> subject) const {
    // Current L2Server 0x0090e6cc.
    L2_GUARD_FUNCTION();
    const int remain = GetRemainMatchCount(subject);
    const GameRuleType rule = WOlympiadService::inst().GetValidRule();
    if (!WOlympiadServiceUtil::SendOSA_MATCH_COUNT(receiver, rule, remain)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kOlympiadServiceSource,
        //     0xdcd)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kOlympiadServiceSource,
        //     0xdcd)
        L2_LOG_ERROR(
            Log,
            "Cannot send remaining Olympiad match count to world %d, character DB ID %d (subject world %d, character DB ID "
            "%d, remaining %d, rule %d).",
            receiver.m_PrimaryKey, receiver.m_SecondaryKey, subject.m_PrimaryKey, subject.m_SecondaryKey, remain,
            static_cast<int>(rule)
        );
    }
}

void WOlympiadUsers::SendOlympiadRecordTo(
    const std::CDualKey<int, int> receiver, const CDX_WOLYMPIAD_RECORD* const record
) const {
    // Current L2Server 0x009052b8.
    L2_GUARD_FUNCTION();
    if (record == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0xdf1,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0xdf1,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_ERROR(
            Log, "Cannot send Olympiad record to world %d, character DB ID %d: record is missing.", receiver.m_PrimaryKey,
            receiver.m_SecondaryKey
        );
        return;
    }
    olympiad::OSA_OLYMPIAD_RECORD response{};
    response.nUserDBID = receiver.m_SecondaryKey;
    response.nPoint = record->nPoint;
    response.nWinCount = record->nWinCount;
    response.nLoseCount = record->nLoseCount;
    response.nMatchCount = GetRemainMatchCount(receiver);
    response.nPrevClassType = record->nPrevClassType;
    response.nPrevRank = record->nPrevRank;
    response.nPrevRankCount = record->nPrevRankCount;
    response.nPrevClassRank = record->nPrevClassRank;
    response.nPrevClassRankCount = record->nPrevClassRankCount;
    response.nPrevClassRankByServer = record->nPrevClassRankByServer;
    response.nPrevClassRankByServerCount = record->nPrevClassRankByServerCount;
    response.nPrevPoint = record->nPrevPoint;
    response.nPrevWinCount = record->nPrevWinCount;
    response.nPrevLoseCount = record->nPrevLoseCount;
    response.nPrevGrade = WOlympiadDefaultSetting::inst().GetOlympiadGrade(record->nPrevRank, record->nPrevRankCount);
    WOlympiadNetworkHost::inst().SendPacket(receiver.m_PrimaryKey, OSA_OLYMPIAD_RECORD, response);
}

void WOlympiadService::SendClassRankByServerTop10(
    const int worldId, const CDX_WOLYMPIAD_GET_CLASS_RANK_BY_SERVER_TOP10* const rank
) const {
    // Current L2Server 0x009075d8.
    L2_GUARD_FUNCTION();
    if (rank == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x1720,
        //     L"WO_ASSERT_NOT_NULL")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x1720,
        //     L"WO_ASSERT_NOT_NULL")
        L2_LOG_ERROR(Log, "Cannot send Olympiad top 10 class ranking to world %d: ranking data is missing.", worldId);
        return;
    }

    olympiad::OSA_CLASS_RANK_BY_SERVER response{};
    response.nHeroClass = rank->m_nHeroClassType;
    for (int index = 0; index < rank->m_nCount; ++index) {
        const auto& source = rank->m_Ranks[index];
        std::size_t length = 0;
        while (length < std::size(source.sCharName) && source.sCharName[length] != u'\0') {
            ++length;
        }
        olympiad::PkClassRankByServerInfo entry{};
        entry.nRanking = index + 1;
        entry.sName.assign(std::u16string_view(source.sCharName, length));
        entry.nPoint = source.nPoint;
        response.vRanks.push_back(entry);
    }
    WOlympiadNetworkHost::inst().SendPacket(worldId, OSA_CLASS_RANK_BY_SERVER, response);
}

WOlympiadParam::WOlympiadParam()
    // Current L2Server 0x008f4374.
    :
    m_bTermSet(false), m_alignment0011To0014{}, m_aTerm{}, m_PeriodType(MONTHLY) {}

void WOlympiadParam::SetTerm(
    const int start, const int bonus1, const int bonus2, const int bonus3, const int bonus4, const int nominate
) {
    // Current L2Server 0x008f43a8.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    m_aTerm[2] = start;
    m_aTerm[3] = bonus1;
    m_aTerm[4] = bonus2;
    m_aTerm[5] = bonus3;
    m_aTerm[6] = bonus4;
    m_aTerm[7] = nominate;
    m_bTermSet = true;
}

WOlympiadCondition::Value WOlympiadParam::GetStep(const int elapsed) const {
    // Current L2Server 0x008f44b0.
    L2_GUARD_FUNCTION();
    if (elapsed >= m_aTerm[7]) {
        return WOlympiadCondition::NOMINATE_HEROES;
    }
    if (elapsed >= m_aTerm[6]) {
        return WOlympiadCondition::GIVE_4TH_BONUS;
    }
    if (elapsed >= m_aTerm[5]) {
        return WOlympiadCondition::GIVE_3RD_BONUS;
    }
    if (elapsed >= m_aTerm[4]) {
        return WOlympiadCondition::GIVE_2ND_BONUS;
    }
    if (elapsed >= m_aTerm[3]) {
        return WOlympiadCondition::GIVE_1ST_BONUS;
    }
    return elapsed >= m_aTerm[2] ? WOlympiadCondition::RESET_POINT : WOlympiadCondition::START_SEASON;
}

bool WOlympiadParam::AbleToAddEntry(const std::int32_t seasonStart, const int start, const int end) const {
    // Current L2Server 0x008f4598.
    L2_GUARD_FUNCTION();
    if (!m_bTermSet) {
        return false;
    }

    const L2DateTime now = L2DateTime::GetCurrent();
    if (now.m_tSecond == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kOlympiadServiceSource,
        //     0xba)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kOlympiadServiceSource,
        //     0xba)
        L2_LOG_ERROR(Log, "Cannot check Olympiad registration hours: current timestamp is zero; registration denied.");
        return false;
    }

    const WOlympiadCondition::Value step = GetStep(now.m_tSecond - seasonStart);
    const bool liveOrTest = g_L2ServerType == L2ServerType::LIVE || g_L2ServerType == L2ServerType::TEST;
    if (step != WOlympiadCondition::NOMINATE_HEROES && (liveOrTest || m_PeriodType == MONTHLY)) {
        return L2DateTimeUtil::IsBetweenInMinute(now.m_nHour * 60 + now.m_nMin + 20, start, end);
    }
    return true;
}

bool WOlympiadParam::IsMonthly() const { return m_PeriodType == MONTHLY; }

void WOlympiadParam::SetMonthly(const bool monthly) { m_PeriodType = monthly ? MONTHLY : DAILY; }

std::int32_t WOlympiadParam::GetNextSeasonStartTime(const std::int32_t start) const {
    // Current L2Server 0x008f46ec.
    const std::int32_t now = runtime::crt::Time32Now();
    const std::int32_t next = start + m_aTerm[7];
    return now - next < m_aTerm[7] / 2 ? next : now;
}

WOlympiadMatchTime::Value WOlympiadParam::GetMatchTime(const WOlympiadCondition::Value step, const L2DateTime& time) {
    // Current L2Server 0x008f5284.
    const WOlympiadDefaultSetting& setting = WOlympiadDefaultSetting::inst();
    if (!setting.IsOlympiadStarted(time.m_eDayOfWeek, time.m_nHour, time.m_nMin)) {
        return WOlympiadMatchTime::BEFORE_MATCHTIME;
    }
    if (step != WOlympiadCondition::NOMINATE_HEROES &&
        !L2DateTimeUtil::IsBetweenInMinute(
            time.m_nHour * 60 + time.m_nMin + 20, setting.GetStartTime(), setting.GetEndTime()
        )) {
        return WOlympiadMatchTime::BEFORE_FINISH;
    }
    return WOlympiadMatchTime::NOW_MATCHTIME;
}

WOlympiadMatchTime::Value WOlympiadParam::GetMatchTime(
    const WOlympiadCondition::Value step, const PeriodType period, const L2DateTime& time
) {
    // Current L2Server 0x008f8548.
    L2_GUARD_FUNCTION();
    if (step == WOlympiadCondition::START_SEASON) {
        return WOlympiadMatchTime::BEFORE_MATCHTIME;
    }
    if (g_L2ServerType == L2ServerType::LIVE || g_L2ServerType == L2ServerType::TEST || period == MONTHLY) {
        return GetMatchTime(step, time);
    }
    return WOlympiadMatchTime::NOW_MATCHTIME;
}

bool WOlympiadParam::GetWantedStatus(const std::int32_t seasonStart, WOlympiadStatus& status) const {
    // Current L2Server 0x008fdb4c.
    L2_GUARD_FUNCTION();
    if (!m_bTermSet) {
        return false;
    }
    const L2DateTime now = L2DateTime::GetCurrent();
    const WOlympiadCondition::Value step = GetStep(now.m_tSecond - seasonStart);
    status.nStep = static_cast<int>(step);
    status.nMatchTime = static_cast<int>(GetMatchTime(step, m_PeriodType, now));
    return true;
}

void WOlympiadParam::GetDebugString(std::u16string& output) const {
    // Current L2Server 0x00901920.
    L2_GUARD_FUNCTION();
    if (m_PeriodType == USER_DEFINED) {
        AppendUtf8(output, "period type [USER_DEFINED], ");
    } else if (m_PeriodType == DAILY) {
        AppendUtf8(output, "period type [DAILY], ");
    } else {
        AppendUtf8(output, "period type [MONTHLY], ");
    }

    char buffer[1024]{};
    std::snprintf(
        buffer, sizeof(buffer),
        "RESET_POINT(%d) GIVE_1ST_BONUS(%d) GIVE_2ND_BONUS(%d) "
        "GIVE_3RD_BONUS(%d) GIVE_4TH_BONUS(%d) NOMINATE_HEROES(%d)",
        m_aTerm[2], m_aTerm[3], m_aTerm[4], m_aTerm[5], m_aTerm[6], m_aTerm[7]
    );
    AppendUtf8(output, buffer);
}

WOlympiadHtml::WOlympiadHtml() = default;
WOlympiadHtml::~WOlympiadHtml() = default;

void WOlympiadHtml::Init() {
    // Current L2Server 0x00900b98.
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_Lock);
    m_mapMatchList.clear();
}

void WOlympiadHtml::OnGameChanged(const int fieldId) {
    // Current L2Server 0x00902988.
    L2_GUARD_FUNCTION();
    if (fieldId < 0 || fieldId >= SharedOlympiadInfo::FIELD_COUNT) {
        return;
    }
    const sOlympiadFieldInfo* const field = &SharedOlympiadInfo::GetInfo()->arrFieldInfo[fieldId];

    L2_WRITE_GUARD(m_Lock);
    const auto position = m_mapMatchList.find(fieldId);
    if (position == m_mapMatchList.end()) {
        if (field->fieldStatus != FIELD_NONE) {
            m_mapMatchList[fieldId] = field;
        }
    } else if (field->fieldStatus == FIELD_NONE) {
        m_mapMatchList.erase(position);
    } else {
        position->second = field;
    }
}

void WOlympiadHtml::SendGameList(const std::CDualKey<int, int> key) const {
    // Current L2Server 0x0090ec9c.
    L2_GUARD_FUNCTION();
    if (WOlympiadService::inst().m_nMatchTime != WOlympiadMatchTime::NOW_MATCHTIME) {
        WOlympiadServiceUtil::SendOSN_SYSTEM_MESSAGE_NO(key, 0x673);
        return;
    }

    auto* const self = const_cast<WOlympiadHtml*>(this);
    L2_READ_GUARD(self->m_Lock);
    olympiad::OSA_MATCH_LIST packet{};
    packet.nUserDBID = key.m_SecondaryKey;
    int count = 0;
    for (const auto& entry : self->m_mapMatchList) {
        const sOlympiadFieldInfo* const field = entry.second;
        if (field == nullptr) {
            continue;
        }

        olympiad::PkMatchInfo info{};
        info.nFieldID = entry.first;
        info.nGameRule = static_cast<int>(field->gameRule);
        info.nStatus = static_cast<int>(field->fieldStatus);
        info.sTeam1Name = GetOlympiadUserNameBySID(static_cast<std::uint32_t>(field->nPlayer1));
        info.sTeam2Name = GetOlympiadUserNameBySID(static_cast<std::uint32_t>(field->nPlayer2));
        packet.vList.push_back(info);

        if (++count == 50) {
            packet.bRemain = true;
            WOlympiadNetworkHost::inst().SendPacket(key.m_PrimaryKey, OSA_MATCH_LIST, packet);
            packet.vList.clear();
            count = 0;
        }
    }

    packet.bRemain = false;
    WOlympiadNetworkHost::inst().SendPacket(key.m_PrimaryKey, OSA_MATCH_LIST, packet);
}

void WOlympiadHtml::SendGameList(CSPointer<User> user) const {
    // Current L2Server 0x0090ef70.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x17e9,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x17e9,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_WARN(Log, "Cannot send Olympiad game list: user is missing.");
        return;
    }

    CUserSocket* const socket = user->GetSocket();
    if (socket == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x17ea,
        //     L"WO_ASSERT_NOT_NULL")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x17ea,
        //     L"WO_ASSERT_NOT_NULL")
        L2_LOG_WARN(Log, "Cannot send Olympiad game list to character DB ID %d: socket is missing.", user->m_data->m_nDBID);
        return;
    }
    if (WOlympiadService::inst().m_nMatchTime != WOlympiadMatchTime::NOW_MATCHTIME) {
        socket->SendSystemMessage(SystemMessageId::OUTSIDE_OLYMPIAD_MATCH_PERIOD);
        return;
    }

    std::array<char, 0x5000> buffer{};
    int used = 0;
    int count = 0;
    auto* const self = const_cast<WOlympiadHtml*>(this);
    L2_READ_GUARD(self->m_Lock);
    for (const auto& entry : self->m_mapMatchList) {
        const sOlympiadFieldInfo* const field = entry.second;
        if (field == nullptr) {
            continue;
        }

        const std::u16string team1 = GetOlympiadUserNameBySID(static_cast<std::uint32_t>(field->nPlayer1));
        const std::u16string team2 = GetOlympiadUserNameBySID(static_cast<std::uint32_t>(field->nPlayer2));
        if (team1.empty() || team2.empty()) {
            continue;
        }

        used += ::Assemble(
            buffer.data() + used, static_cast<int>(buffer.size()) - used, "dddSS", entry.first,
            static_cast<int>(field->gameRule), static_cast<int>(field->fieldStatus), team1.c_str(), team2.c_str()
        );
        if (++count == 50) {
            socket->Send("chdddb", 0xfe, 0xd5, 0, count, 1, used, buffer.data());
            count = 0;
            used = 0;
        }
    }

    socket->Send("chdddb", 0xfe, 0xd5, 0, count, 0, used, buffer.data());
}

void WOlympiadHtml::SendGameListDump(const std::CDualKey<int, int> key) const {
    // Current L2Server 0x00907078.
    L2_GUARD_FUNCTION();
    olympiad::OSA_MATCH_LIST packet{};
    packet.nUserDBID = key.m_SecondaryKey;
    int count = 0;
    for (int fieldId = 0; fieldId < 80; ++fieldId) {
        olympiad::PkMatchInfo info{};
        info.nFieldID = fieldId;
        info.nGameRule = GRT_CLASSLESS;
        info.nStatus = FIELD_MATCH;
        // Original: "올림유저1_"
        const std::u16string team1 = Utf8ToUtf16(std::string("OlyUser1_") + std::to_string(fieldId + 1));
        // Original: "올림유저2_"
        const std::u16string team2 = Utf8ToUtf16(std::string("OlyUser2_") + std::to_string(fieldId + 1));
        info.sTeam1Name.assign(std::u16string_view(team1));
        info.sTeam2Name.assign(std::u16string_view(team2));
        packet.vList.push_back(info);

        if (++count == 50) {
            packet.bRemain = true;
            WOlympiadNetworkHost::inst().SendPacket(key.m_PrimaryKey, OSA_MATCH_LIST, packet);
            packet.vList.clear();
            count = 0;
        }
    }

    packet.bRemain = false;
    WOlympiadNetworkHost::inst().SendPacket(key.m_PrimaryKey, OSA_MATCH_LIST, packet);
}

WOlympiadService::WOlympiadService()
    // Current L2Server 0x0090ca00.
    :
    IWOlympiadNetworkHostCallback(),
    m_pMembers(new OlympiadMembers()),
    m_tLastAccouncing(0),
    m_tSeasonStart(0),
    m_nDbProcessing(0),
    m_nMessageRegistered(0),
    m_nMatchTime(0),
    m_nStartSec(0),
    m_nBonus1Sec(0),
    m_nBonus2Sec(0),
    m_nBonus3Sec(0),
    m_nBonus4Sec(0),
    m_nNominateSec(0),
    m_TickCount(0),
    m_sidPlayerMap{},
    m_MatchMakingJob{},
    m_nForceInstantZoneTypeID(0),
    m_pLastMatchResults(new MatchResults()) {
    m_vOlympiadFieldList.resize(SharedOlympiadInfo::FIELD_COUNT);
}

WOlympiadService::~WOlympiadService() {
    // Current L2Server 0x0090cb98 destroys the 850 match objects before the
    // two process-owned helper allocations.
    m_vOlympiadFieldList.clear();
    delete m_pLastMatchResults;
    m_pLastMatchResults = nullptr;
    delete m_pMembers;
    m_pMembers = nullptr;
}

bool WOlympiadService::Init() {
    // Current L2Server 0x0090a5a8.
    L2_GUARD_FUNCTION();
    if (WOlympiadServiceConfig::inst().IsHost()) {
        constexpr int kAllowedPackets[] = {0xcd, 0xce, 0xb3};
        constexpr int kAllowedExPackets[] = {0x29, 0x85};
        for (const int packet : kAllowedPackets) {
            aPacketAllowTable_ForRaidUser[packet] = true;
        }
        for (const int packet : kAllowedExPackets) {
            aPacketExAllowTable_ForRaidUser[packet] = true;
        }
        aPacketAllowTable_ForRaidUser[0x7d] = false;
    }

    CDX_WOLYMPIAD_LOAD load{};
    db.RequestTPacketProcess(g_nWorldId, 0x43, static_cast<int>(sizeof(load)), load);
    if (!g_JobFinishSignal.WaitFinish(JFT_LOAD_OLYMPIAD, 10)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Loading JFT_LOAD_OLYMPIAD timeout",
        //     kOlympiadServiceSource,
        //     0xe93)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Loading JFT_LOAD_OLYMPIAD timeout",
        //     kOlympiadServiceSource,
        //     0xe93)
        L2_LOG_ERROR(Log, "Waiting for Olympiad data timed out; service initialization continues.");
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]Load Success.",
        //     kOlympiadServiceSource,
        //     0xe97)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]Load Success.",
        //     kOlympiadServiceSource,
        //     0xe97)
        L2_LOG_INFO(Log, "Olympiad data loading completed.");
    }

    InitOlympiadField();
    WOlympiadUsers::inst().LoadMatchContFromDB();
    (new COlympiadLoadingMessage())->Post(0);

    CDX_WOLYMPIAD_GET_PLAYER_COUNT playerCount(SharedOlympiadInfo::GetInfo()->m_nSeason);
    db.RequestTPacketProcess(g_nWorldId, 0x45, static_cast<int>(sizeof(playerCount)), playerCount);
    AddTimer(1000, 0);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]WOlympiadService is initialized.",
    //     kOlympiadServiceSource,
    //     0xea8)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]WOlympiadService is initialized.",
    //     kOlympiadServiceSource,
    //     0xea8)
    L2_LOG_INFO(Log, "Olympiad service setup completed; periodic processing scheduled.");
    return true;
}

void WOlympiadService::InitOlympiadField() {
    // Current L2Server 0x00906648: publish every field id first, bind all
    // instant zones, then initialize the complete 850-field surface.
    L2_GUARD_FUNCTION();
    for (int fieldId = 0; fieldId < SharedOlympiadInfo::FIELD_COUNT; ++fieldId) {
        m_vOlympiadFieldList[fieldId].SetFieldId(fieldId);
    }
    InitOlympiadInZone();
    for (int fieldId = 0; fieldId < SharedOlympiadInfo::FIELD_COUNT; ++fieldId) {
        m_vOlympiadFieldList[fieldId].Init(fieldId);
    }
}

void WOlympiadService::ProcessMessage() {
    // Current L2Server 0x008fc2d4 folds the single registered message with a
    // compare/exchange before publishing it into SharedOlympiadInfo.
    if (runtime::raw_atomic::CompareExchangeI32(reinterpret_cast<volatile std::int32_t*>(&m_nMessageRegistered), 0, 1) !=
        1) {
        return;
    }

    SharedOlympiadInfo::GetInfo()->m_nSeason = m_msgReceived.nSeason;
    SharedOlympiadInfo::GetInfo()->m_nStep = m_msgReceived.nStep;
    if (m_msgReceived.idLegendary >= 0) {
        SharedOlympiadInfo::GetInfo()->m_idLegendary = m_msgReceived.idLegendary;
    }
    if (m_msgReceived.tSeasonStart != 0) {
        m_tSeasonStart = m_msgReceived.tSeasonStart;
    }
    WOlympiadServiceUtil::BroadcastOSN_NOTIFY_SEASON(
        m_msgReceived.nSeason, m_msgReceived.nStep, m_tSeasonStart, m_nNominateSec,
        SharedOlympiadInfo::GetInfo()->m_idLegendary
    );
}

void WOlympiadService::SetOlympiadTerm(
    const bool monthly,
    const bool saveTerm,
    const int season,
    const int startSec,
    const int bonus1Sec,
    const int bonus2Sec,
    const int bonus3Sec,
    const int bonus4Sec,
    const int nominateSec,
    const bool nominateHeroes,
    const bool notifyNewSeason
) {
    // Current L2Server 0x008f6b30.
    L2_GUARD_FUNCTION();
    m_Param.m_PeriodType = monthly ? WOlympiadParam::MONTHLY : WOlympiadParam::USER_DEFINED;
    m_Param.m_bTermSet = false;
    m_nStartSec = startSec;
    m_nBonus1Sec = bonus1Sec;
    m_nBonus2Sec = bonus2Sec;
    m_nBonus3Sec = bonus3Sec;
    m_nBonus4Sec = bonus4Sec;
    m_nNominateSec = nominateSec;
    m_Param.SetTerm(startSec, bonus1Sec, bonus2Sec, bonus3Sec, bonus4Sec, nominateSec);

    if (saveTerm) {
        CDX_WOLYMPIAD_SAVE_TERM request(season, startSec, bonus1Sec, bonus2Sec, bonus3Sec, bonus4Sec, nominateSec);
        db.RequestTPacketProcess(g_nWorldId, 0x47, static_cast<int>(sizeof(request)), request);
    }
    if (nominateHeroes) {
        if (!monthly) {
            m_tSeasonStart = runtime::crt::Time32Now();
        }
        CDX_WOLYMPIAD_NOMINATE_HEROES request(SharedOlympiadInfo::GetInfo()->m_nSeason, 1, m_tSeasonStart, notifyNewSeason);
        db.RequestTPacketProcess(g_nWorldId, 0x48, static_cast<int>(sizeof(request)), request);
    }
}

void WOlympiadService::SetOlympiadMonthly(int season, const bool nominateHeroes, const bool notifyNewSeason) {
    // Current L2Server 0x008f6d24.
    L2_GUARD_FUNCTION();
    if (season == 0) {
        season = SharedOlympiadInfo::GetInfo()->m_nSeason;
    }
    const L2DateTime now = L2DateTime::GetCurrent();
    if (now.m_tSecond == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kOlympiadServiceSource,
        //     0x12c4)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kOlympiadServiceSource,
        //     0x12c4)
        L2_LOG_ERROR(Log, "Cannot configure monthly Olympiad season %d: current timestamp is zero.", season);
        return;
    }

    m_tSeasonStart = now.GetStartOfTheMonth();
    CDX_WOLYMPIAD_SAVE_SEASON_START_TIME saveStart(season, m_tSeasonStart);
    db.RequestTPacketProcess(g_nWorldId, 0x50, static_cast<int>(sizeof(saveStart)), saveStart);
    SetOlympiadTerm(
        true, true, season, 0, 7 * 86400, 14 * 86400, 21 * 86400, 28 * 86400, now.GetDaysInThisMonth() * 86400,
        nominateHeroes, notifyNewSeason
    );
}

void WOlympiadService::ClearAllEntry() {
    // Current L2Server 0x008f91b0.
    m_Entry.ClearAllEntry();
    if (m_pMembers != nullptr) {
        m_pMembers->Clear();
    }
}

void WOlympiadService::StartOlympiadSeason() {
    L2_GUARD_FUNCTION();
    // Current L2Server 0x008f713c.
    CDX_WOLYMPIAD_START_SEASON request(GetSeason(), WOlympiadCondition::START_SEASON, m_tSeasonStart);
    db.RequestTPacketProcess(g_nWorldId, 0x53, static_cast<int>(sizeof(request)), request);
}

void WOlympiadService::SetFromDb(const int season, const int step, const std::int32_t seasonStart, const int legendaryId) {
    // Current L2Server 0x008fdbb4.
    m_msgReceived.nSeason = season;
    m_msgReceived.nStep = step;
    m_msgReceived.tSeasonStart = seasonStart;
    m_msgReceived.idLegendary = legendaryId;
    runtime::raw_atomic::ExchangeI32(reinterpret_cast<volatile std::int32_t*>(&m_nMessageRegistered), 1);
}

void WOlympiadService::LoadFromDb(const int season, const int step, const std::int32_t seasonStart, const int legendaryId) {
    // Current L2Server 0x008fc25c.
    SharedOlympiadInfo::GetInfo()->m_nSeason = season;
    SharedOlympiadInfo::GetInfo()->m_nStep = step;
    m_tSeasonStart = seasonStart;
    SharedOlympiadInfo::GetInfo()->m_idLegendary = legendaryId;
    WOlympiadServiceUtil::BroadcastOSN_NOTIFY_SEASON(season, step, seasonStart, m_nNominateSec, legendaryId);
}

void WOlympiadService::SetDefaultOlympiadTerm(const int season) {
    // Current L2Server 0x008fc150.
    L2_GUARD_FUNCTION();
    if (m_Param.m_PeriodType == WOlympiadParam::MONTHLY) {
        SetOlympiadMonthly(season, false, true);
        return;
    }
    SetOlympiadTerm(
        false, true, season, m_nStartSec, m_nBonus1Sec, m_nBonus2Sec, m_nBonus3Sec, m_nBonus4Sec, m_nNominateSec, false, true
    );
}

void WOlympiadService::OnAddUser(const bool success, const int worldId, const int userDbId) {
    L2_GUARD_FUNCTION();
    // Current L2Server 0x008f7274.
    olympiad::OSA_REGISTER response{};
    response.nUserDBID = userDbId;
    response.bSuccess = success;
    WOlympiadNetworkHost::inst().SendPacket(worldId, OSA_REGISTER, response);
}

void WOlympiadService::NotifyNoblessInfo(const std::CDualKey<int, int> key, const bool newSeason) {
    // Current L2Server 0x008ff930.
    L2_GUARD_FUNCTION();
    const SharedNoblessInfo* const info = WOlympiadUsers::inst().GetNoblessInfo(key);
    if (info == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kOlympiadServiceSource,
        //     0x1638)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kOlympiadServiceSource,
        //     0x1638)
        L2_LOG_ERROR(
            Log, "Cannot notify world %d about character DB ID %d: Olympiad nobless information is missing.",
            key.m_PrimaryKey, key.m_SecondaryKey
        );
        return;
    }
    olympiad::OSN_NOBLESS_INFO packet{};
    WOlympiadStuff::ConvertNoblessInfo(info, &packet);
    packet.bNewSeason = newSeason;
    WOlympiadNetworkHost::inst().SendPacket(key.m_PrimaryKey, OSN_NOBLESS_INFO, packet);
}

bool WOlympiadService::NotifyOlympiadMatchingInfo(const int worldId) {
    L2_GUARD_FUNCTION();
    // Current L2Server 0x008f734c.
    olympiad::OSN_OLYMPIAD_MATCHING_INFO packet{};
    packet.bMatchOpen = m_nMatchTime == WOlympiadMatchTime::NOW_MATCHTIME;
    return WOlympiadNetworkHost::inst().SendPacket(worldId, OSN_OLYMPIAD_MATCHING_INFO, packet);
}

void WOlympiadService::DoNextStep(const int fieldId, std::u16string parameter) {
    // Current L2Server 0x008ffb40.
    L2_GUARD_FUNCTION();
    WOlympiadMatch* const field = GetField(fieldId);
    if (field == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kOlympiadServiceSource,
        //     0x1651)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kOlympiadServiceSource,
        //     0x1651)
        L2_LOG_ERROR(Log, "Cannot advance Olympiad match: field %d is unavailable.", fieldId);
        return;
    }
    field->DoNextStep(std::move(parameter));
}

void WOlympiadService::CheckAutoStart() {
    // Current L2Server 0x008fc08c.
    L2_GUARD_FUNCTION();
    const L2DateTime now = L2DateTime::GetCurrent();
    if (now.m_nDay == 1 && m_Param.m_PeriodType == WOlympiadParam::MONTHLY) {
        SetOlympiadMonthly(0, false, true);
    }
}

void WOlympiadService::ConfirmStep() {
    // Current L2Server 0x008f69d8.
    L2_GUARD_FUNCTION();
    SharedOlympiadInfo* const info = SharedOlympiadInfo::GetInfo();
    const int nextStep = info->m_nStep + 1;
    if (nextStep == 2) {
        CDX_WOLYMPIAD_ADD_ALL_POINT request(info->m_nSeason, 2, 1000);
        db.RequestTPacketProcess(g_nWorldId, 0x4d, static_cast<int>(sizeof(request)), request);
        return;
    }
    if (nextStep >= 3 && nextStep <= 6) {
        CDX_WOLYMPIAD_ADD_ALL_POINT request(info->m_nSeason, nextStep, 0);
        db.RequestTPacketProcess(g_nWorldId, 0x4e, static_cast<int>(sizeof(request)), request);
        return;
    }
    if (nextStep == 7) {
        CDX_WOLYMPIAD_NOMINATE_HEROES request(info->m_nSeason, 1, m_Param.GetNextSeasonStartTime(m_tSeasonStart), true);
        db.RequestTPacketProcess(g_nWorldId, 0x48, static_cast<int>(sizeof(request)), request);
    }
}

void WOlympiadService::BroadcastOlympiadMatchingInfo() {
    // Current L2Server 0x008f7418.
    L2_GUARD_FUNCTION();
    olympiad::OSN_OLYMPIAD_MATCHING_INFO packet{};
    packet.bMatchOpen = m_nMatchTime == WOlympiadMatchTime::NOW_MATCHTIME;
    WOlympiadNetworkHost::inst().BroadcastPacket(OSN_OLYMPIAD_MATCHING_INFO, packet);
}

void WOlympiadService::InitOlympiadInZone() {
    // Current L2Server 0x00905e9c.
    L2_GUARD_FUNCTION();
    const std::vector<int> typeIds = CreateRandomInzonTypeIDs();
    for (int fieldId = 0; fieldId < SharedOlympiadInfo::FIELD_COUNT; ++fieldId) {
        WOlympiadMatch* const field = GetField(fieldId);
        if (field == nullptr || field->GetInZone() != nullptr) {
            continue;
        }

        const int typeId = runtime::CheckedVectorAt(typeIds, static_cast<std::size_t>(fieldId));
        if (g_InstantZoneDB.GetTypicalInstantZone(typeId) == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]No inzone: %d",
            //     kOlympiadServiceSource,
            //     0x11d4,
            //     typeId)
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]No inzone: %d",
            //     kOlympiadServiceSource,
            //     0x11d4,
            //     typeId)
            L2_LOG_ERROR(
                Log, "Cannot initialize Olympiad field %d: instant zone template %d is missing; skipping this field.",
                fieldId, typeId
            );
            continue;
        }

        CreateFailReason reason = CFR_INTERNAL_ERROR;
        CInstantZone* const zone = g_InstantZoneMgr.CreateInstantZone(typeId, reason);
        if (zone == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]Failed to create inzone: %d, %d",
            //     kOlympiadServiceSource,
            //     0x11dd,
            //     typeId,
            //     static_cast<int>(reason))
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]Failed to create inzone: %d, %d",
            //     kOlympiadServiceSource,
            //     0x11dd,
            //     typeId,
            //     static_cast<int>(reason))
            L2_LOG_ERROR(
                Log, "Cannot create instant zone type %d for Olympiad field %d: reason %d; skipping this field.", typeId,
                fieldId, static_cast<int>(reason)
            );
            continue;
        }
        field->SetInZone(zone);
    }
}

void WOlympiadService::StopAllMatchMaking() {
    // Current L2Server 0x00909d78.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Entry.m_EntryMapLock);
        for (const CSPointer<WOlympiadTeam>& team : m_Entry.m_TeamList[GRT_CLASSLESS]) {
            if (!team) {
                continue;
            }
            const std::CDualKey<int, int> key = team->GetMemberKey();
            WOlympiadServiceUtil::SendOSN_STOP(key);
            m_pMembers->RemoveMember(key);
        }
    }
    m_Entry.ClearAllEntry();
}

void WOlympiadService::OnPeerDisconnected(const int worldId) {
    // Current L2Server 0x00909c08.
    L2_GUARD_FUNCTION();
    const std::vector<CSPointer<WOlympiadMemberInfo>> members = m_pMembers->GetMembers(worldId);
    for (const CSPointer<WOlympiadMemberInfo>& member : members) {
        if (!member) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
            //     kOlympiadServiceSource,
            //     0x1696)
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
            //     kOlympiadServiceSource,
            //     0x1696)
            L2_LOG_ERROR(
                Log,
                "Cannot finish Olympiad peer-disconnect cleanup for world %d: member list contains an empty entry; "
                "remaining entries were not processed.",
                worldId
            );
            return;
        }
        member->SetPeerDisconnect();
        member->SetPrepareValidCondition(GVC_VALID_TEAM);
        DisconnectMember(member->GetKey());
    }
}

void WOlympiadService::TimerExpired(int) {
    // Current L2Server 0x0090b110.
    L2_GUARD_FUNCTION();
    ProcessMessage();

    const std::uint32_t previousTick = m_TickCount++;
    if (previousTick % 60U == 0U) {
        CDX_WOLYMPIAD_GET_PLAYER_COUNT request(GetSeason());
        db.RequestTPacketProcess(g_nWorldId, 0x45, static_cast<int>(sizeof(request)), request);
    }

    OlympiadTimerRearmer rearm(*this, 1000, 0);
    if (m_tLastAccouncing != 0) {
        return;
    }
    if (m_tSeasonStart == 0) {
        CheckAutoStart();
        return;
    }

    WOlympiadStatus wanted;
    if (!m_Param.GetWantedStatus(m_tSeasonStart, wanted)) {
        return;
    }

    if (wanted.nMatchTime != m_nMatchTime) {
        switch (wanted.nMatchTime) {
            case WOlympiadMatchTime::BEFORE_MATCHTIME:
                WOlympiadServiceUtil::BroadcastMessage(0x66a);
                break;
            case WOlympiadMatchTime::NOW_MATCHTIME:
                InitOlympiadInZone();
                WOlympiadServiceUtil::BroadcastMessage(0x669);
                break;
            case WOlympiadMatchTime::BEFORE_FINISH:
                WOlympiadServiceUtil::BroadcastMessage(0x77f);
                StopAllMatchMaking();
                break;
            default:
                break;
        }
        m_nMatchTime = wanted.nMatchTime;
        BroadcastOlympiadMatchingInfo();
    }

    if (SharedOlympiadInfo::GetInfo()->m_nStep < wanted.nStep) {
        ConfirmStep();
    }

    if (m_nMatchTime == WOlympiadMatchTime::NOW_MATCHTIME) {
        const GameRuleType rule = GetValidRule();
        if (m_Entry.CheckEntryByGameRule(rule)) {
            if (!WOlympiadServiceConst::_MATCHING_POINT_MODE) {
                DoMatch(static_cast<int>(rule));
            } else {
                LoadMatchingPoint(static_cast<int>(rule));
            }
        }
    }
}

int WOlympiadService::GetSeason() const {
    // Current L2Server 0x008f50c0.
    return SharedOlympiadInfo::GetInfo()->m_nSeason;
}

WOlympiadCondition::Value WOlympiadService::GetStep() const {
    // Current L2Server 0x008f50d8.
    return static_cast<WOlympiadCondition::Value>(SharedOlympiadInfo::GetInfo()->m_nStep);
}

std::int32_t WOlympiadService::GetSeasonStart() const {
    // Current L2Server 0x008f50d4.
    return m_tSeasonStart;
}

int WOlympiadService::GetNominateSec() const {
    // Current L2Server 0x008f5158.
    return m_nNominateSec;
}

int WOlympiadService::GetLegendaryWorld() const {
    // Current L2Server 0x008f50ec.
    return SharedOlympiadInfo::GetInfo()->m_idLegendary;
}

GameRuleType WOlympiadService::GetValidRule() const {
    // Current L2Server 0x008f5040 always returns classless.
    L2_GUARD_FUNCTION();
    return GRT_CLASSLESS;
}

bool WOlympiadService::IsPossibleToSetHero() const {
    // Current L2Server 0x008f4f8c.
    L2_GUARD_FUNCTION();
    const std::int32_t elapsed = runtime::crt::Time32Now() - m_tSeasonStart;
    return elapsed > 300 && elapsed + 300 < m_nNominateSec;
}

void WOlympiadService::RegisterDbProcessing() {
    // Current L2Server 0x008f5100.
    runtime::raw_atomic::AddI32(&m_nDbProcessing, 1);
}

void WOlympiadService::UnregisterDbProcessing() {
    // Current L2Server 0x008f5108.
    runtime::raw_atomic::AddI32(&m_nDbProcessing, -1);
}

void WOlympiadService::SetSeasonStartOnTheFly(const std::int32_t seasonStart) {
    // Current L2Server 0x008f5110.
    m_tSeasonStart = seasonStart;
}

void WOlympiadService::SetDebugUser(const std::CDualKey<int, int> key) {
    // Current L2Server 0x008f5114.
    m_keyDebugUser = key;
}

void WOlympiadService::SetForceInstantZoneTypeID(const int typeId) {
    // Current L2Server 0x008f5160.
    m_nForceInstantZoneTypeID = typeId;
}

void WOlympiadService::InitHtml() {
    // Current L2Server 0x0090297c.
    m_olympiadHtml.Init();
}

void WOlympiadService::GetDebugString(std::u16string& output) const {
    // Current L2Server 0x0090239c.
    L2_GUARD_FUNCTION();
    const L2DateTime start = L2DateTime::FromTime_t(m_tSeasonStart);
    const SharedOlympiadInfo* const shared = SharedOlympiadInfo::GetInfo();

    char buffer[1024]{};
    std::snprintf(
        buffer, sizeof(buffer), "season[%d] start [%04d-%02d-%02d %02d:%02d:%02d], ", shared->m_nSeason, start.m_nYear,
        start.m_nMonth, start.m_nDay, start.m_nHour, start.m_nMin, start.m_nSec
    );
    AppendUtf8(output, buffer);

    const int step = SharedOlympiadInfo::GetInfo()->m_nStep;
    const char* stepName = nullptr;
    switch (step) {
        case 0:
            stepName = "NONE";
            break;
        case 1:
            stepName = "START_SEASON";
            break;
        case 2:
            stepName = "RESET_POINT";
            break;
        case 3:
            stepName = "GIVE_1ST_BONUS";
            break;
        case 4:
            stepName = "GIVE_2ND_BONUS";
            break;
        case 5:
            stepName = "GIVE_3RD_BONUS";
            break;
        case 6:
            stepName = "GIVE_4TH_BONUS";
            break;
        case 7:
            stepName = "NOMINATE_HEROES";
            break;
        case 8:
            stepName = "CONDITION_MAX";
            break;
        default:
            break;
    }
    if (stepName != nullptr) {
        AppendUtf8(output, "step [");
        AppendUtf8(output, stepName);
        AppendUtf8(output, "], ");
    }

    std::u16string parameters;
    m_Param.GetDebugString(parameters);
    output.append(parameters);

    std::snprintf(buffer, sizeof(buffer), " force_inzone_type[%d]", m_nForceInstantZoneTypeID);
    AppendUtf8(output, buffer);
}

bool WOlympiadService::ObserveMatch(const std::CDualKey<int, int> key, const int fieldId, const std::uint32_t accountId) {
    // Current L2Server 0x0090e964.
    L2_GUARD_FUNCTION();
    if (static_cast<unsigned int>(fieldId) >= static_cast<unsigned int>(SharedOlympiadInfo::FIELD_COUNT)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]invalid field id: %d, %d, %d",
        //     kOlympiadServiceSource,
        //     0x15ae,
        //     key.m_PrimaryKey,
        //     key.m_SecondaryKey,
        //     fieldId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]invalid field id: %d, %d, %d",
        //     kOlympiadServiceSource,
        //     0x15ae,
        //     key.m_PrimaryKey,
        //     key.m_SecondaryKey,
        //     fieldId)
        L2_LOG_WARN(
            Log, "Cannot observe Olympiad field %d: field ID is outside 0..%d (world %d, character DB ID %d).", fieldId,
            static_cast<int>(SharedOlympiadInfo::FIELD_COUNT) - 1, key.m_PrimaryKey, key.m_SecondaryKey
        );
        return false;
    }
    if (GetField(fieldId) == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]No field id: %d, %d, %d",
        //     kOlympiadServiceSource,
        //     0x15b6,
        //     key.m_PrimaryKey,
        //     key.m_SecondaryKey,
        //     fieldId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]No field id: %d, %d, %d",
        //     kOlympiadServiceSource,
        //     0x15b6,
        //     key.m_PrimaryKey,
        //     key.m_SecondaryKey,
        //     fieldId)
        L2_LOG_ERROR(
            Log, "Cannot observe Olympiad field %d: field is unavailable (world %d, character DB ID %d).", fieldId,
            key.m_PrimaryKey, key.m_SecondaryKey
        );
        return false;
    }

    CSPointer<WOlympiadMemberInfo> previous = FindMember(key);
    if (previous) {
        if (previous->IsPlayer()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]Already player: %d, %d",
            //     kOlympiadServiceSource,
            //     0x15c0,
            //     key.m_PrimaryKey,
            //     key.m_SecondaryKey)
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]Already player: %d, %d",
            //     kOlympiadServiceSource,
            //     0x15c0,
            //     key.m_PrimaryKey,
            //     key.m_SecondaryKey)
            L2_LOG_WARN(
                Log, "Cannot observe Olympiad match: character DB ID %d in world %d is already registered as a player.",
                key.m_SecondaryKey, key.m_PrimaryKey
            );
            return false;
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]Already observer: %d, %d",
        //     kOlympiadServiceSource,
        //     0x15c5,
        //     key.m_PrimaryKey,
        //     key.m_SecondaryKey)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]Already observer: %d, %d",
        //     kOlympiadServiceSource,
        //     0x15c5,
        //     key.m_PrimaryKey,
        //     key.m_SecondaryKey)
        L2_LOG_INFO(
            Log,
            "Removing previous Olympiad observer registration before checking the new observation request (world %d, "
            "character DB ID %d).",
            key.m_PrimaryKey, key.m_SecondaryKey
        );
        m_pMembers->RemoveMember(key);
    }

    if (!CheckAndClosePrevSocket(accountId, key.m_PrimaryKey, key.m_SecondaryKey)) {
        return false;
    }

    CSPointer<WOlympiadMemberInfo> member(new WOlympiadMemberInfo(key), ObjectRefType::FunctionChain);
    member->SetFieldID(fieldId);
    m_pMembers->AddMember(member);
    return true;
}

CSPointer<WOlympiadMemberInfo> WOlympiadService::FindMember(const std::CDualKey<int, int> key) const {
    return m_pMembers ? m_pMembers->FindMember(key) : CSPointer<WOlympiadMemberInfo>{};
}

bool WOlympiadService::AssignConnectedUser(CSPointer<User> user) {
    // Current L2Server 0x008fefd0.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadService.cpp",
        //     0x14c6,
        //     L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadService.cpp",
        //     0x14c6,
        //     L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_WARN(Log, "Cannot assign connected Olympiad participant: user is missing.");
        return false;
    }

    const std::CDualKey<int, int> key(user->m_data->m_nWorldId, static_cast<int>(user->GetDBID()));
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]C_ENTER_WORLD recv: %d, %d",
    //     L"OlympiadService.cpp",
    //     0x14ca,
    //     key.m_PrimaryKey,
    //     key.m_SecondaryKey)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]C_ENTER_WORLD recv: %d, %d",
    //     L"OlympiadService.cpp",
    //     0x14ca,
    //     key.m_PrimaryKey,
    //     key.m_SecondaryKey)
    L2_LOG_INFO(
        Log, "Assigning connected Olympiad participant from world %d, character DB ID %d.", key.m_PrimaryKey,
        key.m_SecondaryKey
    );

    CSPointer<WOlympiadMemberInfo> member = FindMember(key);
    if (!member) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]No member: %d, %d",
        //     L"OlympiadService.cpp",
        //     0x14d0,
        //     key.m_PrimaryKey,
        //     key.m_SecondaryKey)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]No member: %d, %d",
        //     L"OlympiadService.cpp",
        //     0x14d0,
        //     key.m_PrimaryKey,
        //     key.m_SecondaryKey)
        L2_LOG_WARN(
            Log, "Cannot assign connected Olympiad participant from world %d, character DB ID %d: member is not registered.",
            key.m_PrimaryKey, key.m_SecondaryKey
        );
        return false;
    }

    const int fieldId = member->GetFieldID();
    WOlympiadMatch* const field = GetField(fieldId);
    if (field == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]No field: %d, %d, %d",
        //     L"OlympiadService.cpp",
        //     0x14d8,
        //     key.m_PrimaryKey,
        //     key.m_SecondaryKey,
        //     fieldId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]No field: %d, %d, %d",
        //     L"OlympiadService.cpp",
        //     0x14d8,
        //     key.m_PrimaryKey,
        //     key.m_SecondaryKey,
        //     fieldId)
        L2_LOG_ERROR(
            Log, "Cannot assign connected Olympiad participant from world %d, character DB ID %d: field %d is unavailable.",
            key.m_PrimaryKey, key.m_SecondaryKey, fieldId
        );
        return false;
    }

    const bool assigned = member->IsPlayer() ? field->SetConnectedUser(user) : field->AddObserver(user->GetServerId());
    if (!assigned) {
        return false;
    }

    user->m_data->SetOlympidApplicant(true);
    return true;
}

bool WOlympiadService::RegisterAuthInfo(const std::CDualKey<int, int> key, const olympiad::PkOlympiadAuthInfo& authInfo) {
    // Current L2Server 0x008ff404.
    L2_GUARD_FUNCTION();
    CSPointer<WOlympiadMemberInfo> member = FindMember(key);
    if (!member) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]no member: %d, %d",
        //     kOlympiadServiceSource,
        //     0x150f,
        //     key.m_PrimaryKey,
        //     key.m_SecondaryKey)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]no member: %d, %d",
        //     kOlympiadServiceSource,
        //     0x150f,
        //     key.m_PrimaryKey,
        //     key.m_SecondaryKey)
        L2_LOG_WARN(
            Log,
            "Cannot store Olympiad authentication information for world %d, character DB ID %d: member is not registered.",
            key.m_PrimaryKey, key.m_SecondaryKey
        );
        return false;
    }
    member->SetAuthInfo(authInfo);
    return true;
}

const olympiad::PkOlympiadAuthInfo* WOlympiadService::FindAuthInfo(const std::CDualKey<int, int> key) const {
    // Current L2Server 0x008ff56c.
    CSPointer<WOlympiadMemberInfo> member = FindMember(key);
    return member ? &member->GetAuthInfo() : nullptr;
}

WOlympiadService::StartMatchMakingResult WOlympiadService::StartMatchMaking(CSPointer<WOlympiadMemberInfo> member) {
    // Current L2Server 0x0090a794.
    L2_GUARD_FUNCTION();
    if (!member) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0xf08,
        //     L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0xf08,
        //     L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_WARN(Log, "Cannot start Olympiad matchmaking: member is missing.");
        return SMMR_Failed;
    }
    const std::CDualKey<int, int> key = member->GetKey();
    const GameRuleType gameRule = member->GetGameRule();
    if (m_nMatchTime != WOlympiadMatchTime::NOW_MATCHTIME) {
        WOlympiadServiceUtil::SendOSA_START(key, gameRule, StartResult::Failed, 0x673, 0);
        return SMMR_Failed;
    }

    CSPointer<WOlympiadMemberInfo> previous = FindMember(key);
    if (previous) {
        if (previous->IsPlayer()) {
            WOlympiadServiceUtil::SendOSA_START(key, gameRule, StartResult::Already, 0x5de, 0);
            return SMMR_Failed;
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]Already observer: %d, %d",
        //     kOlympiadServiceSource,
        //     0xf1f,
        //     key.m_PrimaryKey,
        //     key.m_SecondaryKey)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]Already observer: %d, %d",
        //     kOlympiadServiceSource,
        //     0xf1f,
        //     key.m_PrimaryKey,
        //     key.m_SecondaryKey)
        L2_LOG_INFO(
            Log,
            "Removing Olympiad observer registration before checking matchmaking eligibility (world %d, character DB ID "
            "%d).",
            key.m_PrimaryKey, key.m_SecondaryKey
        );
        m_pMembers->RemoveMember(key);
    }

    if (!CheckAndClosePrevSocket(member)) {
        WOlympiadServiceUtil::SendOSA_START(key, gameRule, StartResult::Failed, 0x5de, 0);
        return SMMR_Failed;
    }

    WOlympiadUsers::MatchLimitType limit = WOlympiadUsers::MLT_MAX;
    const SharedOlympiadInfo* const shared = SharedOlympiadInfo::GetInfo();
    if (!WOlympiadUsers::inst().CanRegistMatch(key, shared->m_nSeason, shared->m_nStep, limit, gameRule)) {
        int messageId = 0;
        if (limit == WOlympiadUsers::MLT_GAMERULE) {
            messageId = 0xc99;
        }
        if (limit == WOlympiadUsers::MLT_TOTAL) {
            messageId = 0xc98;
        }
        if (messageId != 0) {
            WOlympiadServiceUtil::SendOSA_START(key, gameRule, StartResult::Failed, messageId, 0);
        }
        return SMMR_Failed;
    }

    const WOlympiadDefaultSetting& setting = WOlympiadDefaultSetting::inst();
    if (!m_Param.AbleToAddEntry(m_tSeasonStart, setting.GetStartTime(), setting.GetEndTime())) {
        WOlympiadServiceUtil::SendOSA_START(key, gameRule, StartResult::Failed, 0x70b, 0);
        return SMMR_TimeLimit;
    }

    m_pMembers->AddMember(member);
    WOlympiadTeam* const team = new WOlympiadTeam(member);
    m_Entry.AddEntry(team, gameRule);
    const int entryCount = m_Entry.GetClassGroupEntryCount(gameRule);
    if (!WOlympiadServiceUtil::SendOSA_START(key, gameRule, StartResult::Success, 0, entryCount)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Send OSA_START failed: %d %d",
        //     kOlympiadServiceSource,
        //     0xf5d,
        //     key.m_PrimaryKey,
        //     key.m_SecondaryKey)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Send OSA_START failed: %d %d",
        //     kOlympiadServiceSource,
        //     0xf5d,
        //     key.m_PrimaryKey,
        //     key.m_SecondaryKey)
        L2_LOG_ERROR(
            Log,
            "Cannot send Olympiad matchmaking acceptance to world %d, character DB ID %d; removing the entry and "
            "membership.",
            key.m_PrimaryKey, key.m_SecondaryKey
        );
        m_Entry.RemoveEntry(key);
        m_pMembers->RemoveMember(key);
        return SMMR_Failed;
    }
    return SMMR_Success;
}

void WOlympiadService::SetPrepareValidCondition(const std::CDualKey<int, int> key, const GameValidCondition condition) {
    // Current L2Server 0x008ff684.
    L2_GUARD_FUNCTION();
    CSPointer<WOlympiadMemberInfo> member = FindMember(key);
    if (!member) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]No member: %d, %d",
        //     L"OlympiadService.cpp",
        //     0x1672,
        //     key.m_PrimaryKey,
        //     key.m_SecondaryKey)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]No member: %d, %d",
        //     L"OlympiadService.cpp",
        //     0x1672,
        //     key.m_PrimaryKey,
        //     key.m_SecondaryKey)
        L2_LOG_WARN(
            Log, "Cannot set Olympiad preparation condition %d for world %d, character DB ID %d: member is not registered.",
            static_cast<int>(condition), key.m_PrimaryKey, key.m_SecondaryKey
        );
        return;
    }
    if (!member->IsPlayer()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]No need to prepare for non player: %d, %d",
        //     L"OlympiadService.cpp",
        //     0x1678,
        //     key.m_PrimaryKey,
        //     key.m_SecondaryKey)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]No need to prepare for non player: %d, %d",
        //     L"OlympiadService.cpp",
        //     0x1678,
        //     key.m_PrimaryKey,
        //     key.m_SecondaryKey)
        L2_LOG_WARN(
            Log, "Ignoring Olympiad preparation condition %d for world %d, character DB ID %d: member is an observer.",
            static_cast<int>(condition), key.m_PrimaryKey, key.m_SecondaryKey
        );
        return;
    }
    if (member->GetFieldID() == -1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadService.cpp",
        //     0x167e)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadService.cpp",
        //     0x167e)
        L2_LOG_WARN(
            Log,
            "Cannot set Olympiad preparation condition %d for world %d, character DB ID %d: player has no assigned field.",
            static_cast<int>(condition), key.m_PrimaryKey, key.m_SecondaryKey
        );
        return;
    }
    member->SetPrepareValidCondition(condition);
}

void WOlympiadService::NotifyLastMatchResult(const std::CDualKey<int, int> key) {
    // Current L2Server 0x00900a78.
    L2_GUARD_FUNCTION();
    if (!m_pLastMatchResults) {
        return;
    }
    GameResultEnum result = GRE_DRAW;
    int itemCount = 0;
    if (!m_pLastMatchResults->Pop(key, result, itemCount)) {
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]NotifyLastMatchResult: %d, %d, %d, %d",
    //     L"OlympiadService.cpp",
    //     0x16da,
    //     key.m_PrimaryKey,
    //     key.m_SecondaryKey,
    //     static_cast<int>(result),
    //     itemCount)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]NotifyLastMatchResult: %d, %d, %d, %d",
    //     L"OlympiadService.cpp",
    //     0x16da,
    //     key.m_PrimaryKey,
    //     key.m_SecondaryKey,
    //     static_cast<int>(result),
    //     itemCount)
    L2_LOG_INFO(
        Log, "Sending previous Olympiad match result to world %d, character DB ID %d: result %d, item count %d.",
        key.m_PrimaryKey, key.m_SecondaryKey, static_cast<int>(result), itemCount
    );
    olympiad::OSN_LAST_MATCH_RESULT packet{};
    packet.nUserDBID = key.m_SecondaryKey;
    packet.nMatchResult = static_cast<int>(result);
    packet.nItemCount = itemCount;
    WOlympiadNetworkHost::inst().SendPacket(key.m_PrimaryKey, OSN_LAST_MATCH_RESULT, packet);
}

void WOlympiadService::SendGameList(const std::CDualKey<int, int> key) const { m_olympiadHtml.SendGameList(key); }

void WOlympiadService::SendGameList(CSPointer<User> user) const { m_olympiadHtml.SendGameList(user); }

void WOlympiadService::SendGameListDump(const std::CDualKey<int, int> key) const { m_olympiadHtml.SendGameListDump(key); }

void WOlympiadService::StopMatchMaking(const std::CDualKey<int, int> key) {
    // Current L2Server 0x0090951c.
    L2_GUARD_FUNCTION();
    CSPointer<WOlympiadMemberInfo> member = FindMember(key);
    if (!member) {
        WOlympiadServiceUtil::SendOSA_STOP(key, StopResult::NotMember);
        return;
    }
    if (member->GetFieldID() != -1) {
        WOlympiadServiceUtil::SendOSA_STOP(key, StopResult::Playing);
        return;
    }
    m_pMembers->RemoveMember(member->GetKey());
    m_Entry.RemoveEntry(key);
    WOlympiadServiceUtil::SendOSA_STOP(key, StopResult::Success);
}

void WOlympiadService::DisconnectMember(const std::CDualKey<int, int> key) {
    // Current L2Server 0x0090969c.
    L2_GUARD_FUNCTION();
    CSPointer<WOlympiadMemberInfo> member = FindMember(key);
    if (!member) {
        return;
    }
    if (member->GetFieldID() == -1) {
        m_pMembers->RemoveMember(member->GetKey());
        m_Entry.RemoveEntry(key);
    } else {
        member->SetPrepareValidCondition(GVC_VALID_TEAM);
    }
}

bool WOlympiadServiceUtil::LinkNobless(CSPointer<User> user) {
    // Current L2Server 0x0090b484.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x18a3,
        //     L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x18a3,
        //     L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_WARN(Log, "Cannot link Olympiad nobless information: user is missing.");
        return false;
    }

    const int worldId = user->m_data->m_nWorldId;
    const int userDatabaseId = static_cast<int>(user->GetDBID());
    const std::CDualKey<int, int> key(worldId, userDatabaseId);
    const int noblessIndex = WOlympiadUsers::inst().GetNoblessIndex(key);
    SetUserNoblessData(user, noblessIndex);
    return noblessIndex != -1;
}

void WOlympiadServiceUtil::BroadcastOSN_SYSTEM_MESSAGE_NO(const int messageId) {
    // Current L2Server 0x008f7df0.
    L2_GUARD_FUNCTION();
    olympiad::OSN_SYSTEM_MESSAGE_NO packet{};
    packet.bBroadcast = true;
    packet.nUserDBID = 0;
    packet.nMessageNo = messageId;
    WOlympiadNetworkHost::inst().BroadcastPacket(OSN_SYSTEM_MESSAGE_NO, packet);
}

void WOlympiadService::ConfirmOlympiadUser(CSPointer<User> user) {
    // Current L2Server 0x008fc364.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x152a,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x152a,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_WARN(Log, "Cannot confirm Olympiad participant: user is missing.");
        return;
    }

    olympiad::OSN_CONFIRMED packet{};
    packet.nUserDBID = static_cast<int>(user->GetDBID());
    const int worldId = user->m_data->m_nWorldId;
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OSN_CONFIRMED sent: %d, %d",
    //     kOlympiadServiceSource,
    //     0x152f,
    //     worldId,
    //     packet.nUserDBID)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OSN_CONFIRMED sent: %d, %d",
    //     kOlympiadServiceSource,
    //     0x152f,
    //     worldId,
    //     packet.nUserDBID)
    L2_LOG_INFO(
        Log, "Sending Olympiad participant confirmation to world %d, character DB ID %d.", worldId, packet.nUserDBID
    );
    if (!WOlympiadNetworkHost::inst().SendPacket(worldId, OSN_CONFIRMED, packet)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kOlympiadServiceSource,
        //     0x1533)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kOlympiadServiceSource,
        //     0x1533)
        L2_LOG_ERROR(
            Log,
            "Cannot send Olympiad participant confirmation to world %d, character DB ID %d; user count was not incremented.",
            worldId, packet.nUserDBID
        );
        return;
    }
    user->GetSocket()->IncUserCount(false);
}

void WOlympiadService::OnUserDisconnected(CSPointer<User> user) {
    // Current L2Server 0x008fc5e0.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x1540,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x1540,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_WARN(Log, "Cannot notify the origin world about an Olympiad disconnect: user is missing.");
        return;
    }
    olympiad::OSN_DISCONNECTED packet{};
    packet.nUserDBID = static_cast<int>(user->GetDBID());
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OSN_DISCONNECTED sent: %d, %d",
    //     kOlympiadServiceSource,
    //     0x1545,
    //     user->m_data->m_nWorldId,
    //     packet.nUserDBID)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OSN_DISCONNECTED sent: %d, %d",
    //     kOlympiadServiceSource,
    //     0x1545,
    //     user->m_data->m_nWorldId,
    //     packet.nUserDBID)
    L2_LOG_INFO(
        Log, "Sending Olympiad disconnect notification to world %d, character DB ID %d.", user->m_data->m_nWorldId,
        packet.nUserDBID
    );
    WOlympiadNetworkHost::inst().SendPacket(user->m_data->m_nWorldId, OSN_DISCONNECTED, packet);
}

void WOlympiadService::Restart(CSPointer<User> user) {
    // Current L2Server 0x008fcd9c.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x157f,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x157f,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_WARN(Log, "Cannot restart Olympiad participant: user is missing.");
        return;
    }

    const int worldId = user->m_data->m_nWorldId;
    if (worldId == g_nWorldId) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]user is not in raid: %d, %d",
        //     kOlympiadServiceSource,
        //     0x1583,
        //     worldId,
        //     static_cast<int>(user->GetDBID()))
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]user is not in raid: %d, %d",
        //     kOlympiadServiceSource,
        //     0x1583,
        //     worldId,
        //     static_cast<int>(user->GetDBID()))
        L2_LOG_WARN(
            Log, "Cannot restart Olympiad participant: origin world %d is the local world (character DB ID %d).", worldId,
            static_cast<int>(user->GetDBID())
        );
        return;
    }

    CUserSocket* const socket = user->GetSocket();
    if (socket == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Socket is null: %d, %d",
        //     kOlympiadServiceSource,
        //     0x158b,
        //     worldId,
        //     static_cast<int>(user->GetDBID()))
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Socket is null: %d, %d",
        //     kOlympiadServiceSource,
        //     0x158b,
        //     worldId,
        //     static_cast<int>(user->GetDBID()))
        L2_LOG_WARN(
            Log, "Cannot restart Olympiad participant from world %d, character DB ID %d: socket is missing.", worldId,
            static_cast<int>(user->GetDBID())
        );
        return;
    }

    olympiad::OSQ_RESTART packet{};
    packet.nUserDBID = static_cast<int>(user->GetDBID());
    packet.nUserSID = static_cast<int>(user->GetServerId());
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OSQ_RESTART sent: %d",
    //     kOlympiadServiceSource,
    //     0x1593,
    //     packet.nUserDBID)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OSQ_RESTART sent: %d",
    //     kOlympiadServiceSource,
    //     0x1593,
    //     packet.nUserDBID)
    L2_LOG_INFO(Log, "Sending Olympiad restart request to world %d for character DB ID %d.", worldId, packet.nUserDBID);
    if (WOlympiadNetworkHost::inst().SendPacket(worldId, OSQ_RESTART, packet)) {
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d]Restart failed to sent: %d, %d",
    //     kOlympiadServiceSource,
    //     0x1598,
    //     worldId,
    //     static_cast<int>(user->GetDBID()))
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d]Restart failed to sent: %d, %d",
    //     kOlympiadServiceSource,
    //     0x1598,
    //     worldId,
    //     static_cast<int>(user->GetDBID()))
    L2_LOG_ERROR(
        Log,
        "Cannot send Olympiad restart request to world %d for character DB ID %d; sending the client a raid-socket close "
        "instruction and unbinding the user.",
        worldId, static_cast<int>(user->GetDBID())
    );
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]S_EX_CLOSE_RAID_SOCKET sent: %d, %d,
    //     RSCR_RESTART_ON_RAID_SERVER", kOlympiadServiceSource, 0x159c, worldId, static_cast<int>(user->GetDBID()))
    // Noise: the preceding failure message already describes the close instruction and unbind that follow.
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]S_EX_CLOSE_RAID_SOCKET sent: %d, %d,
    //     RSCR_RESTART_ON_RAID_SERVER", kOlympiadServiceSource, 0x159c, worldId, static_cast<int>(user->GetDBID()))
    socket->Send("chd", 0xfe, 0xb9, 1);
    socket->m_nCloseReason = SCR_RETURN_TO_ORIGIN;
    socket->UnBindUser();
}

bool WOlympiadService::ReturnToOrigin(CSPointer<User> user) {
    // Current L2Server 0x008fc7cc.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x1550,
        //     L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x1550,
        //     L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_WARN(Log, "Cannot return Olympiad participant to the origin world: user is missing.");
        return false;
    }
    const int worldId = user->m_data->m_nWorldId;
    if (worldId == g_nWorldId) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]user is not in raid: %d, %d",
        //     kOlympiadServiceSource,
        //     0x1554,
        //     worldId,
        //     static_cast<int>(user->GetDBID()))
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]user is not in raid: %d, %d",
        //     kOlympiadServiceSource,
        //     0x1554,
        //     worldId,
        //     static_cast<int>(user->GetDBID()))
        L2_LOG_WARN(
            Log, "Cannot return Olympiad participant: origin world %d is the local world (character DB ID %d).", worldId,
            static_cast<int>(user->GetDBID())
        );
        return false;
    }
    CUserSocket* const socket = user->GetSocket();
    if (socket == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Socket is null: %d, %d",
        //     kOlympiadServiceSource,
        //     0x155c,
        //     worldId,
        //     static_cast<int>(user->GetDBID()))
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Socket is null: %d, %d",
        //     kOlympiadServiceSource,
        //     0x155c,
        //     worldId,
        //     static_cast<int>(user->GetDBID()))
        L2_LOG_WARN(
            Log, "Cannot return Olympiad participant to world %d, character DB ID %d: socket is missing.", worldId,
            static_cast<int>(user->GetDBID())
        );
        return false;
    }

    olympiad::OSQ_RETURN packet{};
    packet.nUserDBID = static_cast<int>(user->GetDBID());
    packet.nUserSID = static_cast<int>(user->GetServerId());
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OSQ_RETURN sent: %d, %d",
    //     kOlympiadServiceSource,
    //     0x1564,
    //     worldId,
    //     static_cast<int>(user->GetDBID()))
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OSQ_RETURN sent: %d, %d",
    //     kOlympiadServiceSource,
    //     0x1564,
    //     worldId,
    //     static_cast<int>(user->GetDBID()))
    L2_LOG_INFO(
        Log, "Sending Olympiad return request to world %d for character DB ID %d.", worldId,
        static_cast<int>(user->GetDBID())
    );
    if (WOlympiadNetworkHost::inst().SendPacket(worldId, OSQ_RETURN, packet)) {
        return true;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d]ReturnToOrigin failed to sent: %d, %d",
    //     kOlympiadServiceSource,
    //     0x1568,
    //     worldId,
    //     static_cast<int>(user->GetDBID()))
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d]ReturnToOrigin failed to sent: %d, %d",
    //     kOlympiadServiceSource,
    //     0x1568,
    //     worldId,
    //     static_cast<int>(user->GetDBID()))
    L2_LOG_ERROR(
        Log,
        "Cannot send Olympiad return request to world %d for character DB ID %d; sending the client a raid-socket close "
        "instruction and unbinding the user.",
        worldId, static_cast<int>(user->GetDBID())
    );
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]S_EX_CLOSE_RAID_SOCKET sent: %d, %d,
    //     RSCR_RETURN_TO_ORIGIN", kOlympiadServiceSource, 0x156c, worldId, static_cast<int>(user->GetDBID()))
    // Noise: the preceding failure message already describes the close instruction and unbind that follow.
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]S_EX_CLOSE_RAID_SOCKET sent: %d, %d,
    //     RSCR_RETURN_TO_ORIGIN", kOlympiadServiceSource, 0x156c, worldId, static_cast<int>(user->GetDBID()))
    socket->Send("chc", 0xfe, 0xb9, 0);
    socket->m_nCloseReason = SCR_RETURN_TO_ORIGIN;
    socket->UnBindUser();
    return false;
}

void WOlympiadServiceUtil::BroadcastMessage(const int messageId) {
    // Current L2Server 0x008fd50c.
    BroadcastOSN_SYSTEM_MESSAGE_NO(messageId);
    CUserBroadcaster::inst().BroadcastToAllUser("chc", 0x62, messageId, 0);
}

void WOlympiadServiceUtil::BroadcastOSN_NOTIFY_SEASON(
    const int season, const int step, const std::int32_t seasonStart, const int nominateSec, const int legendaryId
) {
    // Current L2Server 0x008f7ebc.
    L2_GUARD_FUNCTION();
    olympiad::OSN_NOTIFY_SEASON packet{};
    packet.nSeason = season;
    packet.nStep = step;
    packet.nStartTime = seasonStart;
    packet.nNominateSecond = nominateSec;
    packet.nLegendaryWorldID = legendaryId;
    WOlympiadNetworkHost::inst().BroadcastPacket(OSN_NOTIFY_SEASON, packet);
}

void WOlympiadServiceUtil::BroadcastStartSeason(const int season) {
    // Current L2Server 0x008fd544.
    olympiad::OSN_START_SEASON packet{};
    packet.nSeason = season;
    WOlympiadNetworkHost::inst().BroadcastPacket(OSN_START_SEASON, packet);
    CUserBroadcaster::inst().BroadcastToAllUser("chccd", 0x62, 0x667, 1, 1, season);
}

void WOlympiadServiceUtil::BroadcastStopSeason(const int season) {
    // Current L2Server 0x008fd5b4.
    olympiad::OSN_STOP_SEASON packet{};
    packet.nSeason = season;
    WOlympiadNetworkHost::inst().BroadcastPacket(OSN_STOP_SEASON, packet);
    CUserBroadcaster::inst().BroadcastToAllUser("chccd", 0x62, 0x668, 1, 1, season);
}

void WOlympiadServiceUtil::BroadcastOSN_OLYMPIAD_POINT(const int point) {
    // Current L2Server 0x008f7fa8.
    L2_GUARD_FUNCTION();
    olympiad::OSN_OLYMPIAD_POINT packet{};
    packet.bBroadcast = true;
    packet.nUserDBID = 0;
    packet.nPoint = point;
    WOlympiadNetworkHost::inst().BroadcastPacket(OSN_OLYMPIAD_POINT, packet);
}

void WOlympiadServiceUtil::ResetRankInfo(const ClassType classType) {
    // Current L2Server 0x008f7584.
    L2_GUARD_FUNCTION();
    SharedOlympiadInfo* const info = SharedOlympiadInfo::GetInfo();
    const int classIndex = OlympiadDefine::GetHeroClassIndex(classType);
    if (classIndex != -1) {
        info->arrRankingCount[classIndex] = 0;
    }
}

void WOlympiadServiceUtil::AddRankInfo(
    const ClassType classType, const int ranking, const wchar_t* const name, const int point
) {
    // Current L2Server 0x008f7664.
    L2_GUARD_FUNCTION();
    const std::u16string targetName = WideToUtf16(name);
    SharedOlympiadInfo::GetInfo()->AddRankInfo(
        classType, ranking, reinterpret_cast<const SharedUtilsWideChar*>(targetName.c_str()), point
    );
}

void WOlympiadServiceUtil::SetSeasonPlayerCount(const int count) {
    // Current L2Server 0x008f7538.
    SharedOlympiadInfo::GetInfo()->m_nSeasonPlayerCount = count;
    olympiad::OSN_PLAYER_COUNT packet{};
    packet.nCount = count;
    WOlympiadNetworkHost::inst().BroadcastPacket(OSN_PLAYER_COUNT, packet);
}

void WOlympiadServiceUtil::SendOSA_VERSION(const int worldId, const bool success) {
    L2_GUARD_FUNCTION();
    olympiad::OSA_VERSION packet{};
    packet.nWorldId = worldId;
    packet.bSuccess = success;
    WOlympiadNetworkHost::inst().SendPacket(worldId, OSA_VERSION, packet);
}

bool WOlympiadServiceUtil::SendOSA_START(
    const std::CDualKey<int, int> key,
    const GameRuleType gameRule,
    const StartResult::Value result,
    const int param1,
    const int param2
) {
    L2_GUARD_FUNCTION();
    olympiad::OSA_START packet{};
    packet.cGameRule = static_cast<char>(gameRule);
    packet.nUserDBID = key.m_SecondaryKey;
    packet.cResult = static_cast<char>(result);
    packet.nMessageNo = param1;
    packet.nEntryCount = param2;
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OSA_START sent: %d, %d",
    //     L"OlympiadService.cpp",
    //     0x18c7,
    //     key.m_PrimaryKey,
    //     key.m_SecondaryKey)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OSA_START sent: %d, %d",
    //     L"OlympiadService.cpp",
    //     0x18c7,
    //     key.m_PrimaryKey,
    //     key.m_SecondaryKey)
    L2_LOG_INFO(
        Log,
        "Sending Olympiad matchmaking response to world %d, character DB ID %d: result %d, rule %d, message ID %d, entry "
        "count %d.",
        key.m_PrimaryKey, key.m_SecondaryKey, static_cast<int>(result), static_cast<int>(gameRule), param1, param2
    );
    return WOlympiadNetworkHost::inst().SendPacket(key.m_PrimaryKey, OSA_START, packet);
}

bool WOlympiadServiceUtil::SendOSA_STOP(const std::CDualKey<int, int> key, const StopResult::Value result) {
    L2_GUARD_FUNCTION();
    olympiad::OSA_STOP packet{};
    packet.nUserDBID = key.m_SecondaryKey;
    packet.cResult = static_cast<char>(result);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OSA_STOP sent: %d, %d, %d",
    //     L"OlympiadService.cpp",
    //     0x18dd,
    //     key.m_PrimaryKey,
    //     key.m_SecondaryKey,
    //     static_cast<int>(result))
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OSA_STOP sent: %d, %d, %d",
    //     L"OlympiadService.cpp",
    //     0x18dd,
    //     key.m_PrimaryKey,
    //     key.m_SecondaryKey,
    //     static_cast<int>(result))
    L2_LOG_INFO(
        Log, "Sending Olympiad matchmaking cancellation response to world %d, character DB ID %d: result %d.",
        key.m_PrimaryKey, key.m_SecondaryKey, static_cast<int>(result)
    );
    return WOlympiadNetworkHost::inst().SendPacket(key.m_PrimaryKey, OSA_STOP, packet);
}

bool WOlympiadServiceUtil::SendOSA_OBSERVE(const std::CDualKey<int, int> key, const ObserveResult::Value result) {
    // Current L2Server 0x008f7a2c.
    L2_GUARD_FUNCTION();
    olympiad::OSA_OBSERVE packet{};
    packet.nUserDBID = key.m_SecondaryKey;
    packet.cResult = static_cast<char>(result);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OSA_OBSERVE sent: %d, %d, %d",
    //     L"OlympiadService.cpp",
    //     0x18f1,
    //     key.m_PrimaryKey,
    //     key.m_SecondaryKey,
    //     static_cast<int>(result))
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OSA_OBSERVE sent: %d, %d, %d",
    //     L"OlympiadService.cpp",
    //     0x18f1,
    //     key.m_PrimaryKey,
    //     key.m_SecondaryKey,
    //     static_cast<int>(result))
    L2_LOG_INFO(
        Log, "Sending Olympiad observation response to world %d, character DB ID %d: result %d.", key.m_PrimaryKey,
        key.m_SecondaryKey, static_cast<int>(result)
    );
    return WOlympiadNetworkHost::inst().SendPacket(key.m_PrimaryKey, OSA_OBSERVE, packet);
}

bool WOlympiadServiceUtil::SendOSQ_PREPARE(const std::CDualKey<int, int> key) {
    L2_GUARD_FUNCTION();
    olympiad::OSQ_PREPARE packet{};
    packet.nUserDBID = key.m_SecondaryKey;
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OSQ_PREPARE sent: %d, %d",
    //     L"OlympiadService.cpp",
    //     0x1900,
    //     key.m_PrimaryKey,
    //     key.m_SecondaryKey)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OSQ_PREPARE sent: %d, %d",
    //     L"OlympiadService.cpp",
    //     0x1900,
    //     key.m_PrimaryKey,
    //     key.m_SecondaryKey)
    L2_LOG_INFO(
        Log, "Sending Olympiad preparation request to world %d, character DB ID %d.", key.m_PrimaryKey, key.m_SecondaryKey
    );
    return WOlympiadNetworkHost::inst().SendPacket(key.m_PrimaryKey, OSQ_PREPARE, packet);
}

bool WOlympiadServiceUtil::SendOSN_CONNECT(const std::CDualKey<int, int> key) {
    // Current L2Server 0x008f7c30.
    L2_GUARD_FUNCTION();
    olympiad::OSN_CONNECT packet{};
    packet.nUserDBID = key.m_SecondaryKey;
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OSN_CONNECT sent: %d, %d",
    //     L"OlympiadService.cpp",
    //     0x190f,
    //     key.m_PrimaryKey,
    //     key.m_SecondaryKey)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OSN_CONNECT sent: %d, %d",
    //     L"OlympiadService.cpp",
    //     0x190f,
    //     key.m_PrimaryKey,
    //     key.m_SecondaryKey)
    L2_LOG_INFO(
        Log, "Sending Olympiad connection notification to world %d, character DB ID %d.", key.m_PrimaryKey,
        key.m_SecondaryKey
    );
    return WOlympiadNetworkHost::inst().SendPacket(key.m_PrimaryKey, OSN_CONNECT, packet);
}

void WOlympiadServiceUtil::SendOSN_STOP(const std::CDualKey<int, int> key) {
    // Current L2Server 0x008f8210.
    L2_GUARD_FUNCTION();
    olympiad::OSN_STOP packet{};
    packet.bBroadcast = false;
    packet.nUserDBID = key.m_SecondaryKey;
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OSN_STOP sent: %d, %d",
    //     kOlympiadServiceSource,
    //     0x198a,
    //     key.m_PrimaryKey,
    //     key.m_SecondaryKey)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]OSN_STOP sent: %d, %d",
    //     kOlympiadServiceSource,
    //     0x198a,
    //     key.m_PrimaryKey,
    //     key.m_SecondaryKey)
    L2_LOG_INFO(
        Log, "Sending Olympiad matchmaking stop notification to world %d, character DB ID %d.", key.m_PrimaryKey,
        key.m_SecondaryKey
    );
    WOlympiadNetworkHost::inst().SendPacket(key.m_PrimaryKey, OSN_STOP, packet);
}

bool WOlympiadServiceUtil::SendOSA_MATCH_COUNT(
    const std::CDualKey<int, int> key, const GameRuleType gameRule, const int matchCount
) {
    // Current L2Server 0x008f813c.
    L2_GUARD_FUNCTION();
    olympiad::OSA_MATCH_COUNT packet{};
    packet.nUserDBID = key.m_SecondaryKey;
    packet.nGameRule = static_cast<int>(gameRule);
    packet.nMatchCount = matchCount;
    return WOlympiadNetworkHost::inst().SendPacket(key.m_PrimaryKey, OSA_MATCH_COUNT, packet);
}

void WOlympiadServiceUtil::SetEntryCount(const GameRuleType gameRule, const int count) {
    // Current L2Server 0x008f74e0.
    SharedOlympiadInfo::GetInfo()->m_EntryCount[static_cast<int>(gameRule)] = count;
    olympiad::OSN_ENTRY_COUNT packet{};
    packet.nGameRule = static_cast<int>(gameRule);
    packet.nCount = count;
    WOlympiadNetworkHost::inst().BroadcastPacket(OSN_ENTRY_COUNT, packet);
}

bool WOlympiadServiceUtil::SendOSN_SYSTEM_MESSAGE_NO(const std::CDualKey<int, int> key, const int messageId) {
    // Current L2Server 0x008f7d28.
    L2_GUARD_FUNCTION();
    olympiad::OSN_SYSTEM_MESSAGE_NO packet{};
    packet.bBroadcast = false;
    packet.nUserDBID = key.m_SecondaryKey;
    packet.nMessageNo = messageId;
    return WOlympiadNetworkHost::inst().SendPacket(key.m_PrimaryKey, OSN_SYSTEM_MESSAGE_NO, packet);
}

bool WOlympiadServiceUtil::SendOSN_SYSTEM_MESSAGE(const std::CDualKey<int, int> key, const std::u16string& message) {
    // Current L2Server 0x00900c54.
    L2_GUARD_FUNCTION();
    olympiad::OSN_SYSTEM_MESSAGE packet{};
    packet.nUserDBID = key.m_SecondaryKey;
    packet.sMessage = message;
    return WOlympiadNetworkHost::inst().SendPacket(key.m_PrimaryKey, OSN_SYSTEM_MESSAGE, packet);
}

WOlympiadMatch* WOlympiadService::GetField(const int fieldId) {
    // Current L2Server 0x008f6778.
    if (static_cast<unsigned int>(fieldId) >= 0x352U) {
        return nullptr;
    }
    return &m_vOlympiadFieldList[static_cast<std::size_t>(fieldId)];
}

void WOlympiadService::StopObserving(CSPointer<User> user) {
    // Current L2Server 0x00909854, original owner OlympiadService.cpp.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x1606,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x1606,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_WARN(Log, "Cannot stop Olympiad observation: user is missing.");
        return;
    }

    if (!user->IsOlympiadObserver()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Is not observer: %d, %d",
        //     kOlympiadServiceSource,
        //     0x160a,
        //     user->m_data->m_nWorldId,
        //     static_cast<int>(user->GetDBID()))
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Is not observer: %d, %d",
        //     kOlympiadServiceSource,
        //     0x160a,
        //     user->m_data->m_nWorldId,
        //     static_cast<int>(user->GetDBID()))
        L2_LOG_WARN(
            Log, "Cannot stop Olympiad observation for world %d, character DB ID %d: user is not an observer.",
            user->m_data->m_nWorldId, static_cast<int>(user->GetDBID())
        );
        return;
    }

    const int fieldId = user->m_pWOlympiadUser->GetFieldID();
    WOlympiadMatch* const field = user->m_pWOlympiadUser->GetField();
    if (field == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]No field: %d, %d, %d",
        //     kOlympiadServiceSource,
        //     0x1616,
        //     user->m_data->m_nWorldId,
        //     static_cast<int>(user->GetDBID()),
        //     fieldId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]No field: %d, %d, %d",
        //     kOlympiadServiceSource,
        //     0x1616,
        //     user->m_data->m_nWorldId,
        //     static_cast<int>(user->GetDBID()),
        //     fieldId)
        L2_LOG_ERROR(
            Log, "Cannot remove Olympiad observer from world %d, character DB ID %d: field %d is unavailable.",
            user->m_data->m_nWorldId, static_cast<int>(user->GetDBID()), fieldId
        );
        return;
    }

    field->RemoveObserver(user->GetServerId());
    m_pMembers->RemoveMember(std::CDualKey<int, int>(user->m_data->m_nWorldId, static_cast<int>(user->GetDBID())));
}

void WOlympiadService::ChangeMatchInObserving(CSPointer<User> user, const int fieldId) {
    // Current L2Server 0x008fd2c0.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadService.cpp",
        //     0x15e0,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadService.cpp",
        //     0x15e0,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_WARN(Log, "Cannot change observed Olympiad match to field %d: user is missing.", fieldId);
        return;
    }
    if (static_cast<unsigned int>(fieldId) >= static_cast<unsigned int>(SharedOlympiadInfo::FIELD_COUNT)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Hack?: %d, %d",
        //     L"OlympiadService.cpp",
        //     0x15e4,
        //     user->GetDBID(),
        //     fieldId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Hack?: %d, %d",
        //     L"OlympiadService.cpp",
        //     0x15e4,
        //     user->GetDBID(),
        //     fieldId)
        L2_LOG_WARN(
            Log, "Cannot change observed Olympiad match for character DB ID %u: field %d is outside 0..%d.",
            static_cast<unsigned int>(user->GetDBID()), fieldId, static_cast<int>(SharedOlympiadInfo::FIELD_COUNT) - 1
        );
        return;
    }
    if (!user->IsOlympiadObserver() || user->m_pWOlympiadUser == nullptr) {
        return;
    }
    WOlympiadMatch* const field = user->m_pWOlympiadUser->GetField();
    if (field != nullptr) {
        field->ChangeObserverField(user->GetServerId(), fieldId);
    }
}

void WOlympiadService::UnregisterEntry(const std::CDualKey<int, int> key) {
    // Current L2Server 0x009097a8.
    L2_GUARD_FUNCTION();
    m_pMembers->RemoveMember(key);
    m_Entry.RemoveEntry(key);
}

bool WOlympiadService::IsAnnouncingTurn(const int intervalSeconds) {
    // Current L2Server 0x008f511c.
    const std::int32_t now = runtime::crt::Time32Now();
    if (m_tLastAccouncing == 0 || intervalSeconds < now - m_tLastAccouncing) {
        m_tLastAccouncing = now;
        return true;
    }
    return false;
}

bool WOlympiadService::IsDebugging() const { return m_keyDebugUser.m_PrimaryKey != 0 || m_keyDebugUser.m_SecondaryKey != 0; }

WOlympiadMatch* WOlympiadService::FindVaildField() {
    // Current L2Server 0x008f6794. Exhaustion returns the last
    // fetched field even when that field failed the eligibility checks.
    L2_GUARD_FUNCTION();
    WOlympiadMatch* field = nullptr;
    for (int fieldId = 0; fieldId < SharedOlympiadInfo::FIELD_COUNT; ++fieldId) {
        field = GetField(fieldId);
        if (field == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]No match : %d",
            //     kOlympiadServiceSource,
            //     0x1008,
            //     fieldId)
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]No match : %d",
            //     kOlympiadServiceSource,
            //     0x1008,
            //     fieldId)
            L2_LOG_ERROR(Log, "Olympiad field %d is unavailable during the search for a match arena.", fieldId);
            continue;
        }
        if (!field->CanRegister()) {
            continue;
        }
        if (field->GetInZone() == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]pField->GetInZone() failed: %d",
            //     kOlympiadServiceSource,
            //     0x1015,
            //     fieldId)
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]pField->GetInZone() failed: %d",
            //     kOlympiadServiceSource,
            //     0x1015,
            //     fieldId)
            L2_LOG_ERROR(Log, "Olympiad field %d has no instant zone during the search for a match arena.", fieldId);
            continue;
        }
        if (m_nForceInstantZoneTypeID == 0 || field->GetInZone()->m_nTypeId == m_nForceInstantZoneTypeID) {
            break;
        }
    }
    return field;
}

void WOlympiadService::RegisterOlympiadMaker(const int instantZoneTypeId) {
    // Current L2Server 0x008f68dc.
    L2_GUARD_FUNCTION();
    for (int fieldId = 0; fieldId < SharedOlympiadInfo::FIELD_COUNT; ++fieldId) {
        WOlympiadMatch* const field = GetField(fieldId);
        if (field == nullptr) {
            continue;
        }
        CInstantZone* const zone = field->GetInZone();
        if (zone != nullptr && zone->m_nTypeId == instantZoneTypeId) {
            g_npcserver.Send("cdd", 0x9f, instantZoneTypeId, zone->m_nId);
        }
    }
}

void WOlympiadService::SendDebugText(std::u16string text) {
    // Current L2Server 0x009027c0.
    L2_GUARD_FUNCTION();
    if (!IsDebugging()) {
        return;
    }
    // The original buffer holds at most 0x3ff text units.
    if (text.size() >= 0x3ff) {
        text.resize(0x3ff);
    }

    WOlympiadServiceUtil::SendOSN_SYSTEM_MESSAGE(m_keyDebugUser, text);
    CSPointer<User> user = WOlympiadStuff::FindUserByKey(m_keyDebugUser);
    if (user) {
        user->SendSystemMessage(u"SYS", text.c_str());
    }
}

void WOlympiadService::OnGameChanged(const int fieldId) { m_olympiadHtml.OnGameChanged(fieldId); }

void WOlympiadService::SaveLastMatchResult(
    const std::CDualKey<int, int> key, const GameResultEnum result, const int itemCount
) {
    L2_GUARD_FUNCTION();
    if (m_pLastMatchResults != nullptr) {
        m_pLastMatchResults->Push(key, result, itemCount);
    }
}

void WOlympiadService::SaveOlympiadRecord(
    const std::CDualKey<int, int> winner,
    const std::CDualKey<int, int> loser,
    const int winnerPoint,
    const int winnerItemType,
    const int winnerItemAmount,
    const int loserPoint,
    const int loserItemType,
    const int loserItemAmount,
    const int elapsedTime,
    const int resultType,
    const GameRuleType gameRule
) {
    L2_GUARD_FUNCTION();
    const SharedOlympiadInfo* const info = SharedOlympiadInfo::GetInfo();
    CDX_WOLYMPIAD_SAVE_RECORD packet(
        info->m_nSeason, info->m_nStep, winner.m_PrimaryKey, winner.m_SecondaryKey, loser.m_PrimaryKey, loser.m_SecondaryKey,
        winnerPoint, winnerItemType, winnerItemAmount, loserPoint, loserItemType, loserItemAmount,
        runtime::crt::Time32Now(), resultType, elapsedTime, static_cast<int>(gameRule)
    );
    db.RequestTPacketProcess(g_nWorldId, 0x4f, static_cast<int>(sizeof(packet)), packet);
}

void WOlympiadService::SaveOlympiadPenaltyLog(
    const std::CDualKey<int, int> key,
    const int point,
    const int totalPoint,
    const int fieldId,
    const int gameRule,
    const int penaltyType
) {
    L2_GUARD_FUNCTION();
    CDX_WOLYMPIAD_LOG_PENALTY packet(
        key.m_PrimaryKey, key.m_SecondaryKey, point, totalPoint, fieldId, gameRule, penaltyType
    );
    db.RequestTPacketProcess(g_nWorldId, 0x4b, static_cast<int>(sizeof(packet)), packet);
}

void WOlympiadService::DoMatch(const int gameRule) {
    // Current L2Server 0x00905654.
    L2_GUARD_FUNCTION();
    if (static_cast<unsigned int>(gameRule) >= 3U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]invalid olympiad rule type: %d",
        //     kOlympiadServiceSource,
        //     0x1030,
        //     gameRule)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]invalid olympiad rule type: %d",
        //     kOlympiadServiceSource,
        //     0x1030,
        //     gameRule)
        L2_LOG_ERROR(Log, "Cannot prepare Olympiad matches: game rule %d is outside 0..2.", gameRule);
        return;
    }

    const GameRuleType rule = static_cast<GameRuleType>(gameRule);
    const bool announce = IsAnnouncingTurn(300);
    std::deque<CSPointer<WOlympiadTeam>> ready;
    {
        L2_GUARD(m_Entry.m_EntryMapLock);
        auto& source = m_Entry.m_TeamList[gameRule];
        for (auto position = source.begin(); position != source.end();) {
            if (!*position || !(*position)->GetMember()) {
                ++position;
                continue;
            }
            ready.push_back(*position);
            position = source.erase(position);
            m_Entry.AddEntryCount(rule, -1);
        }
    }

    for (std::size_t index = 2; index < ready.size(); index += 4) {
        auto first = ready.begin();
        auto second = ready.begin();
        std::advance(first, static_cast<std::ptrdiff_t>(index));
        std::advance(second, static_cast<std::ptrdiff_t>(index - 2));
        std::iter_swap(first, second);
    }

    while (ready.size() > 1) {
        WOlympiadMatch* const field = FindVaildField();
        if (field == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]Field not found",
            //     kOlympiadServiceSource,
            //     0x1067)
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]Field not found",
            //     kOlympiadServiceSource,
            //     0x1067)
            L2_LOG_ERROR(
                Log, "Cannot prepare Olympiad match for rule %d: no field was returned; retrying arena lookup.", gameRule
            );
            continue;
        }
        CSPointer<WOlympiadTeam> first = ready.front();
        ready.pop_front();
        CSPointer<WOlympiadTeam> second = ready.front();
        ready.pop_front();
        if (!field->PrepareMatch(rule, first.get(), second.get(), false)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]SetTeam Failed",
            //     kOlympiadServiceSource,
            //     0x1072)
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]SetTeam Failed",
            //     kOlympiadServiceSource,
            //     0x1072)
            L2_LOG_ERROR(
                Log, "Cannot prepare Olympiad match for rule %d; returning both teams to the entry queue.", gameRule
            );
            m_Entry.AddEntry(first.get(), rule);
            m_Entry.AddEntry(second.get(), rule);
        }
    }

    if (!ready.empty()) {
        if (announce) {
            m_Entry.AnnounceToEntry(0x6b1, rule);
        }
        while (!ready.empty()) {
            CSPointer<WOlympiadTeam> team = ready.front();
            ready.pop_front();
            m_Entry.AddEntry(team.get(), rule);
        }
    }
}

bool WOlympiadService::LoadMatchingPoint(const int gameRule) {
    // Current L2Server 0x00906734.
    L2_GUARD_FUNCTION();
    if (static_cast<unsigned int>(gameRule) >= 3U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]invalid olympiad game rule: %d",
        //     kOlympiadServiceSource,
        //     0x1363,
        //     gameRule)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]invalid olympiad game rule: %d",
        //     kOlympiadServiceSource,
        //     0x1363,
        //     gameRule)
        L2_LOG_ERROR(Log, "Cannot load Olympiad matching points: game rule %d is outside 0..2.", gameRule);
        return false;
    }
    JobTransactionLong jobType = 1;
    GuardBeginJob guardBegin(m_MatchMakingJob[gameRule], jobType, kOlympiadServiceSource, 0x1369);
    if (!guardBegin.IsLocked()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]fail to load point, already running: %d",
        //     kOlympiadServiceSource,
        //     0x136d,
        //     gameRule)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]fail to load point, already running: %d",
        //     kOlympiadServiceSource,
        //     0x136d,
        //     gameRule)
        L2_LOG_WARN(
            Log, "Cannot start another Olympiad matching-point load for rule %d: matchmaking job is already active.",
            gameRule
        );
        return false;
    }

    auto& memberMap = m_sidPlayerMap[gameRule];
    memberMap.clear();
    std::vector<int> worldIds;
    std::vector<int> databaseIds;
    {
        L2_GUARD(m_Entry.m_EntryMapLock);
        for (const CSPointer<WOlympiadTeam>& team : m_Entry.m_TeamList[gameRule]) {
            CSPointer<WOlympiadMemberInfo> member = team->GetMember();
            if (!member) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
                //     kOlympiadServiceSource,
                //     0x1385)
                // L2_LOG_ERROR(Log,
                //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
                //     kOlympiadServiceSource,
                //     0x1385)
                L2_LOG_ERROR(
                    Log, "Skipping Olympiad matching-point request entry for rule %d: team member is missing.", gameRule
                );
                continue;
            }
            worldIds.push_back(member->GetWorldID());
            databaseIds.push_back(member->GetDBID());
            memberMap[member->GetKey()] = member;
        }
    }

    if (IsDebugging()) {
        DebugString(u"Request LoadPoint");
    }
    if (db.RequestLoadWOlympiadMatchingPoint(worldIds, databaseIds, static_cast<GameRuleType>(gameRule))) {
        guardBegin.WaitForEndJob();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]Load match point success: %d",
        //     kOlympiadServiceSource,
        //     0x13a0,
        //     gameRule)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]Load match point success: %d",
        //     kOlympiadServiceSource,
        //     0x13a0,
        //     gameRule)
        L2_LOG_INFO(
            Log, "Olympiad matching-point load request submitted for rule %d; waiting for job completion.", gameRule
        );
        return true;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d]fail to request load matching point: %d",
    //     kOlympiadServiceSource,
    //     0x1398,
    //     gameRule)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d]fail to request load matching point: %d",
    //     kOlympiadServiceSource,
    //     0x1398,
    //     gameRule)
    L2_LOG_ERROR(Log, "Cannot submit Olympiad matching-point load request for rule %d.", gameRule);
    return false;
}

bool WOlympiadService::OnLoadMatchPointComplete(const protocol::RP_EX_WOLYMPIAD_MATCHING_POINT_LOAD& reply) {
    // Current L2Server 0x0090fdcc.
    L2_GUARD_FUNCTION();

    const int gameRule = static_cast<int>(static_cast<std::int8_t>(reply.cGameRule));
    if (static_cast<unsigned int>(gameRule) >= 3U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kOlympiadServiceSource,
        //     0x13af)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     kOlympiadServiceSource,
        //     0x13af)
        L2_LOG_ERROR(Log, "Cannot process Olympiad matching-point response: game rule %d is outside 0..2.", gameRule);
        return false;
    }

    JobTransactionLock* const job = &m_MatchMakingJob[gameRule];
    if (reply.vPoints.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Loaded point is none: %d",
        //     kOlympiadServiceSource,
        //     0x13b7,
        //     gameRule)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Loaded point is none: %d",
        //     kOlympiadServiceSource,
        //     0x13b7,
        //     gameRule)
        L2_LOG_WARN(
            Log, "Olympiad matching-point response contains no points for rule %d; ending the job without matchmaking.",
            gameRule
        );
        if (job != nullptr && !job->EndJob()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Failed to end job at %s(%d)",
            //     kOlympiadServiceSource,
            //     0x13b3)
            // L2_LOG_ERROR(Log,
            //     L"Failed to end job at %s(%d)",
            //     kOlympiadServiceSource,
            //     0x13b3)
            L2_LOG_ERROR(Log, "Cannot end Olympiad matching-point job for rule %d.", gameRule);
        }
        return false;
    }

    bool result = false;
    {
        L2_GUARD(m_Entry.m_EntryMapLock);
        auto& memberMap = m_sidPlayerMap[gameRule];
        for (const protocol::PkWOlympiadMatchingPoint& point : reply.vPoints) {
            const std::CDualKey<int, int> key(point.nWorldID, point.nUserDBID);
            const auto position = memberMap.find(key);
            if (position == memberMap.end()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[WORLD_OLYMPIAD][%s][%d]"
                //     L"fail to find user: %d, %d",
                //     kOlympiadServiceSource,
                //     0x13c7,
                //     point.nWorldID,
                //     point.nUserDBID)
                // L2_LOG_ERROR(Log,
                //     L"[WORLD_OLYMPIAD][%s][%d]"
                //     L"fail to find user: %d, %d",
                //     kOlympiadServiceSource,
                //     0x13c7,
                //     point.nWorldID,
                //     point.nUserDBID)
                L2_LOG_WARN(
                    Log,
                    "Ignoring loaded Olympiad matching points for world %d, character DB ID %d: member is absent from the "
                    "pending load map.",
                    point.nWorldID, point.nUserDBID
                );
                continue;
            }

            CSPointer<WOlympiadMemberInfo> member = position->second;
            member->SetMatchingPoint(point.nPoint);
        }

        auto& teams = m_Entry.m_TeamList[gameRule];
        if (teams.size() < 2U) {
            result = true;
        } else if (!WOlympiadServiceUtil::SortMembersWithMatchingPoint(MatchingPoint::MPT_OLYMPIAD, teams)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]SortMember failed: %d",
            //     kOlympiadServiceSource,
            //     0x13dc,
            //     gameRule)
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]SortMember failed: %d",
            //     kOlympiadServiceSource,
            //     0x13dc,
            //     gameRule)
            L2_LOG_ERROR(
                Log, "Cannot sort Olympiad teams by matching points for rule %d; matches were not prepared.", gameRule
            );
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]"
            //     L"success, sidMapCount: %d, %d",
            //     kOlympiadServiceSource,
            //     0x13e1,
            //     gameRule,
            //     memberMap.size())
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]"
            //     L"success, sidMapCount: %d, %d",
            //     kOlympiadServiceSource,
            //     0x13e1,
            //     gameRule,
            //     memberMap.size())
            L2_LOG_INFO(
                Log, "Olympiad teams sorted for rule %d (%zu members in the load map); preparing matches.", gameRule,
                memberMap.size()
            );
            DoMatch(gameRule);
            result = true;
        }
    }

    if (job != nullptr && !job->EndJob()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Failed to end job at %s(%d)",
        //     kOlympiadServiceSource,
        //     0x13b3)
        // L2_LOG_ERROR(Log,
        //     L"Failed to end job at %s(%d)",
        //     kOlympiadServiceSource,
        //     0x13b3)
        L2_LOG_ERROR(Log, "Cannot end Olympiad matching-point job for rule %d.", gameRule);
    }
    return result;
}

void WOlympiadServiceUtil::SendOSN_CANCEL_MATCH(
    const std::CDualKey<int, int> key, const GameValidCondition condition, const int itemId
) {
    L2_GUARD_FUNCTION();
    olympiad::OSN_CANCEL_MATCH packet{};
    packet.nUserDBID = key.m_SecondaryKey;
    packet.nGameValidCondition = static_cast<int>(condition);
    packet.nItemID = itemId;
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSN_CANCEL_MATCH sent: %d, %d, %d",
    //     kOlympiadServiceSource,
    //     0x199e,
    //     key.m_PrimaryKey,
    //     key.m_SecondaryKey,
    //     static_cast<int>(condition))
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSN_CANCEL_MATCH sent: %d, %d, %d",
    //     kOlympiadServiceSource,
    //     0x199e,
    //     key.m_PrimaryKey,
    //     key.m_SecondaryKey,
    //     static_cast<int>(condition))
    L2_LOG_INFO(
        Log, "Sending Olympiad match cancellation to world %d, character DB ID %d: condition %d, item ID %d.",
        key.m_PrimaryKey, key.m_SecondaryKey, static_cast<int>(condition), itemId
    );
    WOlympiadNetworkHost::inst().SendPacket(key.m_PrimaryKey, OSN_CANCEL_MATCH, packet);
}

void WOlympiadServiceUtil::SendOSN_OLYMPIAD_POINT(const std::CDualKey<int, int> key, const int point) {
    L2_GUARD_FUNCTION();
    olympiad::OSN_OLYMPIAD_POINT packet{};
    packet.bBroadcast = false;
    packet.nUserDBID = key.m_SecondaryKey;
    packet.nPoint = point;
    WOlympiadNetworkHost::inst().SendPacket(key.m_PrimaryKey, OSN_OLYMPIAD_POINT, packet);
}

void WOlympiadServiceUtil::SetFieldInfo(
    const int fieldId,
    const OlympiadFieldStatus status,
    const int player1,
    const int player2,
    const GameRuleType gameRule,
    const int instantZoneTypeId,
    const int instantZoneId
) {
    L2_GUARD_FUNCTION();
    if (static_cast<unsigned int>(fieldId) >= static_cast<unsigned int>(SharedOlympiadInfo::FIELD_COUNT)) {
        return;
    }
    sOlympiadFieldInfo& field = SharedOlympiadInfo::GetInfo()->arrFieldInfo[fieldId];
    field.fieldStatus = status;
    field.nPlayer1 = player1;
    field.nPlayer2 = player2;
    field.gameRule = gameRule;
    field.nInzoneTypeId = instantZoneTypeId;
    field.nInzoneSId = instantZoneId;
}

bool WOlympiadServiceUtil::SortMembersWithMatchingPoint(
    const MatchingPoint::MATCHINGPOINT_TYPE type, std::deque<CSPointer<WOlympiadTeam>>& members
) {
    // Current L2Server 0x0090fc98.
    L2_GUARD_FUNCTION();
    if (type != MatchingPoint::MPT_OLYMPIAD) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x1acf,
        //     L"WO_ASSERT_EQUAL_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x1acf,
        //     L"WO_ASSERT_EQUAL_RETURN")
        L2_LOG_ERROR(
            Log, "Cannot sort Olympiad teams: matching-point type %d is unsupported (expected %d).", static_cast<int>(type),
            static_cast<int>(MatchingPoint::MPT_OLYMPIAD)
        );
        return false;
    }

    std::sort(
        members.begin(), members.end(), [](const CSPointer<WOlympiadTeam>& left, const CSPointer<WOlympiadTeam>& right) {
            return left->GetOlympiadPoint() < right->GetOlympiadPoint();
        }
    );
    return true;
}

bool WOlympiadServiceUtil::GenerateWinningRate(WOlympiadTeam& team1, WOlympiadTeam& team2) {
    // Current L2Server 0x0090b604.
    L2_GUARD_FUNCTION();
    if (!team1.GetMember()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x19c9,
        //     L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x19c9,
        //     L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_ERROR(Log, "Cannot calculate Olympiad winning rates: first team member is missing.");
        return false;
    }
    if (!team2.GetMember()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x19ca,
        //     L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     kOlympiadServiceSource,
        //     0x19ca,
        //     L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_ERROR(Log, "Cannot calculate Olympiad winning rates: second team member is missing.");
        return false;
    }

    CSPointer<WOlympiadMemberInfo> first = team1.GetMember();
    CSPointer<WOlympiadMemberInfo> second = team2.GetMember();
    const int firstPoint = team1.GetOlympiadPoint();
    const int secondPoint = team2.GetOlympiadPoint();
    if (firstPoint < 1 || secondPoint < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]invalid olympiad point total : "
        //     L"%I64d %I64d",
        //     kOlympiadServiceSource,
        //     0x19d7,
        //     static_cast<std::int64_t>(firstPoint),
        //     static_cast<std::int64_t>(secondPoint))
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]invalid olympiad point total : "
        //     L"%I64d %I64d",
        //     kOlympiadServiceSource,
        //     0x19d7,
        //     static_cast<std::int64_t>(firstPoint),
        //     static_cast<std::int64_t>(secondPoint))
        L2_LOG_ERROR(
            Log, "Cannot calculate Olympiad winning rates: both point totals must be positive (first %d, second %d).",
            firstPoint, secondPoint
        );
        return false;
    }

    const int totalPoint = runtime::AddInt32Wrapping(firstPoint, secondPoint);
    if (totalPoint < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]invalid total point: %I64d",
        //     kOlympiadServiceSource,
        //     0x19e1,
        //     static_cast<std::int64_t>(totalPoint))
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]invalid total point: %I64d",
        //     kOlympiadServiceSource,
        //     0x19e1,
        //     static_cast<std::int64_t>(totalPoint))
        L2_LOG_ERROR(
            Log,
            "Cannot calculate Olympiad winning rates: combined 32-bit point total %d is not positive (first %d, second %d).",
            totalPoint, firstPoint, secondPoint
        );
        return false;
    }

    first->SetWinningRate(secondPoint, firstPoint);
    second->SetWinningRate(firstPoint, secondPoint);

    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]success: "
    //     L"%d, %d, %.2f, %d, %d, %.2f",
    //     kOlympiadServiceSource,
    //     0x1a07,
    //     first->GetWorldID(),
    //     first->GetDBID(),
    //     static_cast<double>(first->GetWinningRate()),
    //     second->GetWorldID(),
    //     second->GetDBID(),
    //     static_cast<double>(second->GetWinningRate()))
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]success: "
    //     L"%d, %d, %.2f, %d, %d, %.2f",
    //     kOlympiadServiceSource,
    //     0x1a07,
    //     first->GetWorldID(),
    //     first->GetDBID(),
    //     static_cast<double>(first->GetWinningRate()),
    //     second->GetWorldID(),
    //     second->GetDBID(),
    //     static_cast<double>(second->GetWinningRate()))
    L2_LOG_INFO(
        Log, "Olympiad winning rates calculated: world %d, character DB ID %d = %.2f; world %d, character DB ID %d = %.2f.",
        first->GetWorldID(), first->GetDBID(), static_cast<double>(first->GetWinningRate()), second->GetWorldID(),
        second->GetDBID(), static_cast<double>(second->GetWinningRate())
    );
    return true;
}

bool WOlympiadServiceUtil::UpdateMatchingPoint(const int gameResult, WOlympiadTeam& team1, WOlympiadTeam& team2) {
    L2_GUARD_FUNCTION();

    const MatchingPointUpdater::Member second = ConvertFrom(team2);
    const MatchingPointUpdater::Member first = ConvertFrom(team1);
    MatchingPointUpdater updater(gameResult, first, second);
    if (!updater.IsSuccess()) {
        return false;
    }

    {
        CSPointer<WOlympiadMemberInfo> member = team1.GetMember();
        const std::CDualKey<int, int> key = team1.GetMemberKey();
        const std::int64_t result = updater.GetResultPoint(key);
        member->GetOlympiadPoint().nWinningPoint = result;
    }
    {
        CSPointer<WOlympiadMemberInfo> member = team2.GetMember();
        const std::CDualKey<int, int> key = team2.GetMemberKey();
        const std::int64_t result = updater.GetResultPoint(key);
        member->GetOlympiadPoint().nWinningPoint = result;
    }

    CSPointer<WOlympiadMemberInfo> secondMember = team2.GetMember();
    CSPointer<WOlympiadMemberInfo> firstMember = team1.GetMember();
    WOlympiadMemberInfo* const secondInfo = secondMember.operator->();
    WOlympiadMemberInfo* const firstInfo = firstMember.operator->();
    const std::CDualKey<int, int> secondWorldKey = team2.GetMemberKey();
    const std::CDualKey<int, int> firstWorldKey = team1.GetMemberKey();
    const std::CDualKey<int, int> secondDbKey = team2.GetMemberKey();
    const std::CDualKey<int, int> firstDbKey = team1.GetMemberKey();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]UpdateMatchingPoint Success: "
    //     L"%d, %d, %I64d, %d, %d, %I64d",
    //     kOlympiadServiceSource,
    //     0x1ac2,
    //     firstWorldKey.m_PrimaryKey,
    //     firstDbKey.m_SecondaryKey,
    //     firstInfo->GetOlympiadPoint().nWinningPoint,
    //     secondWorldKey.m_PrimaryKey,
    //     secondDbKey.m_SecondaryKey,
    //     secondInfo->GetOlympiadPoint().nWinningPoint)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]UpdateMatchingPoint Success: "
    //     L"%d, %d, %I64d, %d, %d, %I64d",
    //     kOlympiadServiceSource,
    //     0x1ac2,
    //     firstWorldKey.m_PrimaryKey,
    //     firstDbKey.m_SecondaryKey,
    //     firstInfo->GetOlympiadPoint().nWinningPoint,
    //     secondWorldKey.m_PrimaryKey,
    //     secondDbKey.m_SecondaryKey,
    //     secondInfo->GetOlympiadPoint().nWinningPoint)
    L2_LOG_INFO(
        Log,
        "Olympiad match point changes calculated: world %d, character DB ID %d = %lld; world %d, character DB ID %d = %lld.",
        firstWorldKey.m_PrimaryKey, firstDbKey.m_SecondaryKey,
        static_cast<long long>(firstInfo->GetOlympiadPoint().nWinningPoint), secondWorldKey.m_PrimaryKey,
        secondDbKey.m_SecondaryKey, static_cast<long long>(secondInfo->GetOlympiadPoint().nWinningPoint)
    );
    return true;
}

void WOlympiadServiceUtil::SaveMatchPoints(const int nMatchID, const std::vector<CSPointer<WOlympiadMemberInfo>>& members) {
    // Current L2Server 0x0090f578.
    L2_GUARD_FUNCTION();
    if (WOlympiadService::inst().IsDebugging()) {
        WOlympiadService::inst().DebugString(u"Request SavePoint");
    }
    CDX_WOLYMPIAD_MATCHING_POINT_SAVE dxSaveMatchPoint;
    for (const CSPointer<WOlympiadMemberInfo>& member : members) {
        const WOlympiadMatchingPoint& matching = member->GetMatchingPoint();
        const std::int64_t newPoint = member->CalculateMatchingPoint(member->GetWinningPoint());
        dxSaveMatchPoint.AddRecord(member->GetWorldID(), member->GetDBID(), nMatchID, newPoint, matching.nMatchingPointDiff);
        if (WOlympiadService::inst().IsDebugging()) {
            WOlympiadService::inst().DebugString(
                u"%s: base:%d, diff:%d, total:%d, point:%d", member->GetCharName(), matching.nMatchingPointBase,
                matching.nMatchingPointDiff, matching.nMatchingPointTotal, member->GetWinningPoint()
            );
        }
    }
    if (dxSaveMatchPoint.HasRecord()) {
        db.RequestTPacketProcess(g_nWorldId, 0x59, 100, dxSaveMatchPoint);
    }
}
