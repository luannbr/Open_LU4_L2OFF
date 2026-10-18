#include "MiniGameAgit.h"
#include "AccountJobFinishSignal.h"
#include "CacheContextManager.h"
#include "Creature.h"
#include "DB.h"
#include "NpcServer.h"
#include "Party.h"
#include "Pledge.h"
#include "PledgeFunction.h"
#include "PledgeInfo.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/TextRuntime.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserSocket.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <queue>
#include <string>
#include <utility>

namespace {

std::uint32_t Milliseconds(int seconds) { return static_cast<std::uint32_t>(seconds) * 1000U; }

int ResidenceId(const CResidenceData* const data) noexcept { return data != nullptr ? data->m_nResidenceID : 0; }

const char16_t* ResidenceName(const CResidenceData* const data) noexcept {
    return data != nullptr ? data->m_sResidenceName.data() : u"";
}

char16_t* TakeMiniGameBuilderToken(char16_t*& command) noexcept {
    // UTF-16 implementation of current Shared::GetLeftString. The current
    // helper has no null-input gate.
    while (*command == u' ') {
        ++command;
    }
    if (*command == u'\0') {
        return nullptr;
    }

    char16_t* const token = command;
    while (*command != u'\0' && *command != u' ') {
        ++command;
    }
    if (*command != u'\0') {
        *command = u'\0';
        ++command;
    }
    return token;
}

void SendNpcState(const CResidenceData* const data, const int active, const int resumed) {
    g_npcserver.Send("cdSddd", 0x31, ResidenceId(data), ResidenceName(data), 0, active, resumed);
}

void SendToPledgeMaster(const unsigned int pledgeDbId, const char* const format, std::va_list arguments) {
    CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, static_cast<int>(pledgeDbId));
    if (!pledge) {
        return;
    }
    User* const master = pledge->GetMaster();
    if (master == nullptr) {
        return;
    }
    CUserSocket* const socket = master->GetSocket();
    if (socket != nullptr) {
        socket->SendV(format, arguments);
    }
}

} // namespace

bool CMiniGameAgitNpcFuncHandler::PreCheck() {
    // Current L2Server 0x0083bc08.
    L2_GUARD_FUNCTION();
    if (m_byFunc >= 6U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid func id(%d)",
        //     L"MiniGameAgit.cpp",
        //     0x6fc,
        //     static_cast<int>(m_byFunc))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid func id(%d)",
        //     L"MiniGameAgit.cpp",
        //     0x6fc,
        //     static_cast<int>(m_byFunc))
        L2_LOG_WARN(
            Log, "Mini-game clan hall %d: NPC request function %u is outside 0..5; the request is rejected.", m_nAgitId,
            static_cast<unsigned>(m_byFunc)
        );
        return false;
    }

    m_pUser = CCreatureFactory::Inst().FindObjectSP(m_nUserIndex);
    if (!m_pUser || !m_pUser->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid user index(%d)",
        //     L"MiniGameAgit.cpp",
        //     0x703,
        //     m_nUserIndex)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid user index(%d)",
        //     L"MiniGameAgit.cpp",
        //     0x703,
        //     m_nUserIndex)
        L2_LOG_WARN(
            Log,
            "Mini-game clan hall %d: NPC request function %u is rejected because character index %d does not resolve to a "
            "player.",
            m_nAgitId, static_cast<unsigned>(m_byFunc), m_nUserIndex
        );
        return false;
    }

    m_pNpc = CCreatureFactory::Inst().FindObjectSP(m_nNpcIndex);
    if (!m_pNpc || !m_pNpc->IsNPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid npc index(%d)",
        //     L"MiniGameAgit.cpp",
        //     0x70a,
        //     m_nNpcIndex)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid npc index(%d)",
        //     L"MiniGameAgit.cpp",
        //     0x70a,
        //     m_nNpcIndex)
        L2_LOG_WARN(
            Log,
            "Mini-game clan hall %d: NPC request function %u is rejected because NPC index %d does not resolve to an NPC.",
            m_nAgitId, static_cast<unsigned>(m_byFunc), m_nNpcIndex
        );
        return false;
    }

    m_pAgit = CMiniGameAgit::GetMiniGameAgit(m_nAgitId);
    if (m_pAgit == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid agit id(%d)",
        //     L"MiniGameAgit.cpp",
        //     0x711,
        //     m_nAgitId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid agit id(%d)",
        //     L"MiniGameAgit.cpp",
        //     0x711,
        //     m_nAgitId)
        L2_LOG_ERROR(
            Log, "Mini-game clan hall %d was not found; NPC request function %u is rejected.", m_nAgitId,
            static_cast<unsigned>(m_byFunc)
        );
        return false;
    }

    const FVector npcPosition = m_pNpc->GetPos();
    const FVector userPosition = m_pUser->GetPos();
    const double deltaX = userPosition.x - npcPosition.x;
    const double deltaY = userPosition.y - npcPosition.y;
    return deltaX * deltaX + deltaY * deltaY <= 1000000.0;
}

void CMiniGameAgitNpcFuncHandler::RegisterPledge() {
    // Current L2Server 0x0083d398.
    L2_GUARD_FUNCTION();
    int result = -7;
    User* const firstUser = m_pUser->CastUser();
    CPledge* const pledge = firstUser->GetPledge();
    if (pledge != nullptr) {
        User* const secondUser = m_pUser->CastUser();
        if (pledge->GetMaster() == secondUser) {
            result = m_pAgit->AddEntryPledge(pledge, m_nPoint);
        } else {
            result = -2;
        }
    }

    g_npcserver.Send("ccdddd", 0x56, 0, ResidenceId(m_pAgit->m_data), result, m_nUserIndex, m_nNpcIndex);
}

void CMiniGameAgitNpcFuncHandler::UnregisterPledge() {
    // Current L2Server 0x0083d540.
    L2_GUARD_FUNCTION();
    int result = -7;
    User* const firstUser = m_pUser->CastUser();
    CPledge* const pledge = firstUser->GetPledge();
    if (pledge != nullptr) {
        User* const secondUser = m_pUser->CastUser();
        if (pledge->GetMaster() == secondUser) {
            result = m_pAgit->RemoveEntryPledge(pledge);
        } else {
            result = -2;
        }
    }

    g_npcserver.Send("ccdddd", 0x56, 1, ResidenceId(m_pAgit->m_data), result, m_nUserIndex, m_nNpcIndex);
}

void CMiniGameAgitNpcFuncHandler::GetStatus() {
    // Current L2Server 0x0083a388.
    L2_GUARD_FUNCTION();
    g_npcserver.Send(
        "ccdddd", 0x56, 2, ResidenceId(m_pAgit->m_data), static_cast<int>(m_pAgit->GetState()), m_nUserIndex, m_nNpcIndex
    );
}

void CMiniGameAgitNpcFuncHandler::JoinGame() {
    // Current L2Server 0x0083efcc.
    L2_GUARD_FUNCTION();
    User* const user = m_pUser->CastUser();
    const int result = m_pAgit->JoinGame(user);
    g_npcserver.Send("ccdddd", 0x56, 3, ResidenceId(m_pAgit->m_data), result, m_nUserIndex, m_nNpcIndex);
}

void CMiniGameAgitNpcFuncHandler::SetWinner() {
    // Current L2Server 0x0083a7ac.
    L2_GUARD_FUNCTION();
    User* const user = m_pUser->CastUser();
    CPledge* const pledge = user->GetPledge();
    if (pledge != nullptr) {
        m_pAgit->OnSetWinner(pledge);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]MiniGameAgit Winner Set. pledge_id[%d]",
        //     L"MiniGameAgit.cpp",
        //     0x77f,
        //     pledge->GetDbId())
        // Noise: This unconditional notification trace cannot confirm winner acceptance by OnSetWinner. CloseGame sends the
        // final outcome to players. L2_LOG_ERROR(Log,
        //     L"[%s][%d]MiniGameAgit Winner Set. pledge_id[%d]",
        //     L"MiniGameAgit.cpp",
        //     0x77f,
        //     pledge->GetDbId())
    }
}

void CMiniGameAgitNpcFuncHandler::GetUnreturnedPoint() {
    // Current L2Server 0x0083d6e8.
    L2_GUARD_FUNCTION();
    User* const firstUser = m_pUser->CastUser();
    CPledge* const pledge = firstUser->GetPledge();
    if (pledge == nullptr) {
        return;
    }

    User* const secondUser = m_pUser->CastUser();
    if (pledge->GetMaster() != secondUser) {
        return;
    }

    const int point = m_pAgit->GetUnreturnedPoint(pledge->GetDbId());
    if (point > 0) {
        g_npcserver.Send("ccdddd", 0x56, 4, ResidenceId(m_pAgit->m_data), point, m_nUserIndex, m_nNpcIndex);
    }
}

void CMiniGameAgitNpcFuncHandler::Execute() {
    // Current L2Server 0x0083f61c.
    L2_GUARD_FUNCTION();
    switch (m_byFunc) {
        case 0:
            RegisterPledge();
            break;
        case 1:
            UnregisterPledge();
            break;
        case 2:
            GetStatus();
            break;
        case 3:
            JoinGame();
            break;
        case 4:
            SetWinner();
            break;
        case 5:
            GetUnreturnedPoint();
            break;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] invalid func id(%d)",
            //     L"MiniGameAgit.cpp",
            //     0x738,
            //     static_cast<int>(m_byFunc))
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] invalid func id(%d)",
            //     L"MiniGameAgit.cpp",
            //     0x738,
            //     static_cast<int>(m_byFunc))
            L2_LOG_ERROR(
                Log, "Mini-game clan hall %d: cannot dispatch NPC request function %u; supported functions are 0..5.",
                m_nAgitId, static_cast<unsigned>(m_byFunc)
            );
            break;
    }
}

CMiniGameAgitSelectedPledgeEntry::CMiniGameAgitSelectedPledgeEntry() :
    m_nSelectedPledgeIdList{}, m_nCount(0), m_alignment14To18{} {}

bool CMiniGameAgitSelectedPledgeEntry::IsSelected(const unsigned int pledgeDbId) const {
    for (int index = 0; index < m_nCount; ++index) {
        if (m_nSelectedPledgeIdList[index] == pledgeDbId) {
            return true;
        }
    }
    return false;
}

void CMiniGameAgitSelectedPledgeEntry::Clear() {
    m_nCount = 0;
    std::fill(std::begin(m_nSelectedPledgeIdList), std::end(m_nSelectedPledgeIdList), 0U);
}

void CMiniGameAgitSelectedPledgeEntry::Add(const unsigned int pledgeDbId) {
    if (m_nCount < 4) {
        m_nSelectedPledgeIdList[m_nCount] = pledgeDbId;
    }
    ++m_nCount;
}

int CMiniGameAgitSelectedPledgeEntry::Size() const { return m_nCount; }

void CMiniGameAgitSelectedPledgeEntry::GetList(std::vector<unsigned int>& pledges) const {
    L2_GUARD_FUNCTION();
    pledges.clear();
    for (int index = 0; index < m_nCount; ++index) {
        pledges.push_back(m_nSelectedPledgeIdList[index]);
    }
}

int CMiniGameAgitSelectedPledgeEntry::GetIndex(const unsigned int pledgeDbId) const {
    L2_GUARD_FUNCTION();
    for (int index = 0; index < m_nCount; ++index) {
        if (m_nSelectedPledgeIdList[index] == pledgeDbId) {
            return index;
        }
    }
    return -1;
}

bool CMiniGameAgitPlayerEntry::AddPlayer(const unsigned int userDbId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EntrySect);
    return m_Entry.insert(userDbId).second;
}

bool CMiniGameAgitPlayerEntry::DelPlayer(const unsigned int userDbId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EntrySect);
    return m_Entry.erase(userDbId) != 0;
}

void CMiniGameAgitPlayerEntry::Clear() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EntrySect);
    m_Entry.clear();
}

void CMiniGameAgitPlayerEntry::DeleteGameItem(const std::vector<int>& itemClassIds) {
    // Current L2Server 0x0083dd10.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EntrySect);
    for (const unsigned int userDbId : m_Entry) {
        CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(userDbId)));
        if (!user) {
            continue;
        }
        for (const int itemClassId : itemClassIds) {
            user->DeleteItemInInventory(itemClassId);
        }
    }
}

void CMiniGameAgitPlayerEntry::Banish(
    const unsigned int winnerPledgeDbId,
    const std::vector<FVector>& challengerRestartPoints,
    const std::vector<FVector>& otherRestartPoints
) {
    // Current L2Server 0x0084095c. The target performs the
    // modulo even when the selected point list is empty.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EntrySect);
    for (const unsigned int userDbId : m_Entry) {
        CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(userDbId)));
        if (!user) {
            continue;
        }

        CPledge* const pledge = user->GetPledge();
        const std::vector<FVector>& points =
            pledge != nullptr && pledge->GetDbId() == winnerPledgeDbId ? challengerRestartPoints : otherRestartPoints;
        const FVector position = points[static_cast<std::size_t>(std::rand()) % points.size()];

        L2_ADD_REF(user, ObjectRefType::EventRetain);
        g_ExpelTimer.ReserveExpelWithLock(user.get(), position);

        CLogFormatter formatter;
        formatter.WriteLog1(
            0x11d, user.get(), nullptr, nullptr, pledge != nullptr ? pledge->GetPledgeName() : nullptr,
            nullptr, pledge != nullptr ? static_cast<std::int64_t>(pledge->GetDbId()) : -1, 1, 0, 0, 0, 0, 0
        );
    }
}

bool CMiniGameAgitRegistPledgeEntry::Regist(const unsigned int pledgeDbId, const int point, const bool fromDatabase) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EntrySect);
    if (!fromDatabase && CheckValidPledge(pledgeDbId, true) != 0) {
        return false;
    }
    const RegistInfo info(point, runtime::crt::Time32Now());
    return m_Entry.emplace(pledgeDbId, info).second;
}

int CMiniGameAgitRegistPledgeEntry::Unregist(const unsigned int pledgeDbId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EntrySect);
    const auto found = m_Entry.find(pledgeDbId);
    if (found == m_Entry.end()) {
        return 0;
    }
    const int point = found->second.nPoint;
    m_Entry.erase(found);
    return point;
}

bool CMiniGameAgitRegistPledgeEntry::IsRegistered(const unsigned int pledgeDbId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EntrySect);
    return m_Entry.find(pledgeDbId) != m_Entry.end();
}

int CMiniGameAgitRegistPledgeEntry::GetPoint(const unsigned int pledgeDbId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EntrySect);
    const auto found = m_Entry.find(pledgeDbId);
    return found != m_Entry.end() ? found->second.nPoint : 0;
}

int CMiniGameAgitRegistPledgeEntry::CheckValidPledge(const unsigned int pledgeDbId, const bool checkRegistered) {
    L2_GUARD_FUNCTION();
    CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, static_cast<int>(pledgeDbId));
    if (!pledge) {
        return -7;
    }
    if (pledge->IsWaitingToDismiss()) {
        return -1;
    }
    if (!m_bCheckLoosely) {
        PledgeFunction* const function = pledge->GetFunction();
        if (function == nullptr || !function->IsEnable(PledgeFunctionType::MINIGAME)) {
            return -9;
        }
        if (pledge->GetDbMemberCount() < 5) {
            return -10;
        }
        if (pledge->GetWorldAgit() != nullptr) {
            return -8;
        }
    }
    if (checkRegistered && IsRegistered(pledgeDbId)) {
        return -11;
    }
    return 0;
}

void CMiniGameAgitRegistPledgeEntry::Clear() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EntrySect);
    m_Entry.clear();
}

void CMiniGameAgitRegistPledgeEntry::Broadcast(const char* const format, std::va_list arguments) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EntrySect);
    for (const auto& entry : m_Entry) {
        std::va_list copy;
        va_copy(copy, arguments);
        SendToPledgeMaster(entry.first, format, copy);
        va_end(copy);
    }
}

void CMiniGameAgitRegistPledgeEntry::BroadcastExcept(
    const std::vector<unsigned int>& except, const char* const format, std::va_list arguments
) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EntrySect);
    for (const auto& entry : m_Entry) {
        if (std::find(except.begin(), except.end(), entry.first) != except.end()) {
            continue;
        }
        std::va_list copy;
        va_copy(copy, arguments);
        SendToPledgeMaster(entry.first, format, copy);
        va_end(copy);
    }
}

void CMiniGameAgitSelectedPledgeEntry::Broadcast(const char* const format, std::va_list arguments) {
    L2_GUARD_FUNCTION();
    for (int index = 0; index < m_nCount; ++index) {
        std::va_list copy;
        va_copy(copy, arguments);
        SendToPledgeMaster(m_nSelectedPledgeIdList[index], format, copy);
        va_end(copy);
    }
}

void CUnreturnedPointKeeper::Add(const unsigned int pledgeDbId, const unsigned int point) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_PledgePointMapLock);
    const auto found = m_PledgePointMap.find(pledgeDbId);
    if (found == m_PledgePointMap.end()) {
        m_PledgePointMap.emplace(pledgeDbId, point);
    } else {
        found->second = point;
    }
}

void CUnreturnedPointKeeper::Remove(const unsigned int pledgeDbId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_PledgePointMapLock);
    m_PledgePointMap.erase(pledgeDbId);
}

void CUnreturnedPointKeeper::Clear() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_PledgePointMapLock);
    m_PledgePointMap.clear();
}

int CUnreturnedPointKeeper::Size() const { return static_cast<int>(m_PledgePointMap.size()); }

bool CUnreturnedPointKeeper::HasUnreturnedPoint(const unsigned int pledgeDbId) const {
    return m_PledgePointMap.find(pledgeDbId) != m_PledgePointMap.end();
}

int CUnreturnedPointKeeper::GetPoint(const unsigned int pledgeDbId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_PledgePointMapLock);
    const auto found = m_PledgePointMap.find(pledgeDbId);
    return found != m_PledgePointMap.end() ? static_cast<int>(found->second) : 0;
}

void CMiniGameAgitRegistPledgeEntry::GetTopN(
    const int count, std::vector<unsigned int>& pledges, const bool includeInvalid, const bool logResult
) {
    L2_GUARD_FUNCTION();
    struct PledgePair {
        unsigned int pledgeDbId;
        int point;
        std::int32_t time;

        bool operator<(const PledgePair& other) const noexcept {
            if (point != other.point) {
                return point < other.point;
            }
            return time > other.time;
        }
    };

    L2_GUARD(m_EntrySect);
    std::priority_queue<PledgePair> sorted;
    for (const auto& entry : m_Entry) {
        sorted.push(PledgePair{entry.first, entry.second.nPoint, entry.second.nTime});
    }

    pledges.clear();
    int acceptedCount = 0;
    while (!sorted.empty()) {
        const PledgePair current = sorted.top();
        sorted.pop();
        const int validationCode = CheckValidPledge(current.pledgeDbId, false);
        const bool accepted = (validationCode == 0 || includeInvalid) && acceptedCount < count;
        if (accepted) {
            pledges.push_back(current.pledgeDbId);
            ++acceptedCount;
        }
        if (logResult) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d]MiniGameAgit GetTopN Confirm Result : "
            //     L"pledge_id[%d], point[%d], code[%d], confirmed[%d]",
            //     L"MiniGameAgit.cpp",
            //     0x195,
            //     current.pledgeDbId,
            //     current.point,
            //     validationCode,
            //     accepted ? 1 : 0)
            // Noise: Per-clan eligibility/ranking results are routine selection details. Selected clans keep their
            // accounting records and builder inspection. L2_LOG_ERROR(Log,
            //     L"[%s][%d]MiniGameAgit GetTopN Confirm Result : "
            //     L"pledge_id[%d], point[%d], code[%d], confirmed[%d]",
            //     L"MiniGameAgit.cpp",
            //     0x195,
            //     current.pledgeDbId,
            //     current.point,
            //     validationCode,
            //     accepted ? 1 : 0)
        }
    }
}

void CMiniGameAgitRegistPledgeEntry::BuilderCmd_ShowInfo(CUserSocket* const socket) {
    // Current L2Server 0x0083b360.
    L2_GUARD_FUNCTION();
    User* const builder = socket != nullptr ? socket->GetUser() : nullptr;
    if (builder == nullptr || builder->m_data == nullptr || builder->m_data->m_nWorldId != g_nWorldId) {
        return;
    }

    L2_GUARD(m_EntrySect);
    char16_t message[1024];
    for (const auto& entry : m_Entry) {
        CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, static_cast<int>(entry.first));
        if (!pledge) {
            continue;
        }
        FormatUtf16To(
            message, std::size(message), u"  pledge[%s] point[%d] time[%d]", pledge->GetPledgeName(), entry.second.nPoint,
            entry.second.nTime
        );
        socket->SendSystemMessage(u"SYS", message);
    }
}

void CMiniGameAgitRegistPledgeEntry::BuilderCmd_PledgeCheckLoosely(CUserSocket*, const bool checkLoosely) {
    // Current L2Server 0x00839d90.
    L2_GUARD_FUNCTION();
    m_bCheckLoosely = checkLoosely;
}

void CMiniGameAgitSelectedPledgeEntry::BuilderCmd_ShowInfo(CUserSocket* const socket) {
    // Current L2Server 0x0083ab8c.
    L2_GUARD_FUNCTION();
    char16_t message[2048]{};
    FormatUtf16To(message, std::size(message), u"  selected pledge[");
    for (int index = 0; index < m_nCount; ++index) {
        CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, static_cast<int>(m_nSelectedPledgeIdList[index]));
        if (!pledge) {
            continue;
        }
        const std::size_t length = std::char_traits<char16_t>::length(message);
        FormatUtf16To(message + length, std::size(message) - length, u" %s ", pledge->GetPledgeName());
    }
    const std::size_t length = std::char_traits<char16_t>::length(message);
    FormatUtf16To(message + length, std::size(message) - length, u"]");
    socket->SendSystemMessage(u"SYS", message);
}

void CMiniGameAgitPlayerEntry::BuilderCmd_ShowInfo(CUserSocket* const socket) {
    // Current L2Server 0x0083def4.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EntrySect);
    char16_t message[1024]{};
    for (const unsigned int userDbId : m_Entry) {
        CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(userDbId)));
        if (!user) {
            continue;
        }
        const char16_t* pledgeName = u"(null_pledge)";
        if (user->GetPledge() != nullptr) {
            pledgeName = user->GetPledge()->GetPledgeName();
        }
        const char16_t* const userName = user->GetName();
        FormatUtf16To(message, std::size(message), u"  joiner[%ls, %ls]", userName, pledgeName);
        socket->SendSystemMessage(u"SYS", message);
    }
}

CMiniGameAgit::CMiniGameAgit() :
    m_alignment4a4To4a8{},
    m_StateHelper(m_TimeSetting),
    m_PledgeEntry(),
    m_PlayerEntry(),
    m_UnreturnedPointKeeper(),
    m_nWinnerPledgeDbId(0),
    m_bCheckLoosely(false),
    m_alignment575To578{},
    m_nLastAlarmSec(0),
    m_alignment57cTo580{} {
    // Current L2Server 0x00840cd0.
    L2_GUARD_FUNCTION();
}

CMiniGameAgit::~CMiniGameAgit() {
    // Current L2Server 0x0083f868. Member and base teardown follows the
    // guarded body through ordinary generated destruction.
    L2_GUARD_FUNCTION();
}

bool CMiniGameAgit::IsMiniGameAgit() const { return true; }

bool CMiniGameAgit::IsBattleTypeAgit() const { return true; }

CMiniGameAgit* CMiniGameAgit::CastMiniGameAgit() const { return const_cast<CMiniGameAgit*>(this); }

void CMiniGameAgit::StartTimer() {
    // Current L2Server 0x00839ee0.
    L2_GUARD_FUNCTION();
    if (!IsUnderSiege()) {
        const ResidenceState state = m_data != nullptr ? m_data->m_ResidenceState : RS_MAX;
        if (state != RS_STANDBY) {
            ChangeResidenceState(RS_STANDBY);
        }
    } else {
        m_bSiegeContinued = true;
        if (m_data != nullptr) {
            m_data->m_ResidenceState = RS_STANDBY;
        }
    }

    // The target unconditionally consumes the loaded residence-data owner
    // after the state branch.
    SetTime(m_data->m_nMiniGameAgitPrepareTerm, m_data->m_nMiniGameAgitProgressTerm, m_data->m_nMiniGameAgitCloseTerm, true);
    CWorldAgit::StartTimer();
}

void CMiniGameAgit::PauseGame() {
    // Current L2Server 0x0083a028.
    L2_GUARD_FUNCTION();
    m_StateHelper.Pause();
}

void CMiniGameAgit::ResumeGame() {
    // Current L2Server 0x0083a0b8.
    L2_GUARD_FUNCTION();
    m_StateHelper.Resume();
}

void CMiniGameAgit::StartSiege() {
    // Current L2Server 0x00841730.
    L2_GUARD_FUNCTION();
    if (!m_bSiegeContinued) {
        StartGame();
    } else {
        RestartGame();
        m_bSiegeContinued = false;
    }
}

void CMiniGameAgit::EndSiege() { EndGame(); }

void CMiniGameAgit::PassSiege() { PassGame(); }

void CMiniGameAgit::PauseSiege() { PauseGame(); }

void CMiniGameAgit::ResumeSiege() { ResumeGame(); }

void CMiniGameAgit::TimerExpired(const int id) {
    // Current L2Server 0x00841628.
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    const ResidenceState state = m_data != nullptr ? m_data->m_ResidenceState : RS_MAX;
    const std::int32_t nextSiege = m_data != nullptr ? m_data->m_NextSiege : 0;

    const bool handleState = state == RS_SIEGE || ((state == RS_STANDBY || state == RS_REGISTRATION) && nextSiege <= now);
    if (handleState) {
        if (g_PledgeTimer.IsNpcServerAlive(CPledgeTimer::NSAT_20)) {
            HandleState();
        } else {
            PauseSiege();
        }
    }
    CWorldAgit::TimerExpired(id);
}

void CMiniGameAgit::SetTime(
    const int prepareSeconds, const int processSeconds, const int closeSeconds, const bool checkTotalTime
) {
    L2_GUARD_FUNCTION();
    m_TimeSetting.SetTime(prepareSeconds, processSeconds, closeSeconds, checkTotalTime);
}

CMiniGameAgitStatus::state CMiniGameAgit::GetState() { return m_StateHelper.GetState(); }

void CMiniGameAgit::LoadFromDB(const int elapsedMilliseconds) {
    // Current L2Server 0x0083a214.
    L2_GUARD_FUNCTION();
    m_StateHelper.LoadFromDB(static_cast<std::uint32_t>(elapsedMilliseconds));
}

CMiniGameAgit* CMiniGameAgit::GetMiniGameAgit(const int residenceId) {
    // Current L2Server 0x0083a2b4.
    L2_GUARD_FUNCTION();
    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence == nullptr || !residence->IsWorldAgit()) {
        return nullptr;
    }
    CWorldAgit* const worldAgit = residence->CastWorldAgit();
    return worldAgit != nullptr ? worldAgit->CastMiniGameAgit() : nullptr;
}

void CMiniGameAgit::OnSetWinner(CPledge* const pledge) {
    // Current L2Server 0x0083a154.
    L2_GUARD_FUNCTION();
    if (pledge != nullptr && m_nWinnerPledgeDbId == 0 && m_StateHelper.GetState() == CMiniGameAgitStatus::PROCESS) {
        m_nWinnerPledgeDbId = pledge->GetDbId();
    }
}

int CMiniGameAgit::AddEntryPledge(CPledge* const pledge, const int point) {
    // Current L2Server 0x0083ca38.
    L2_GUARD_FUNCTION();
    if (m_StateHelper.GetState() != CMiniGameAgitStatus::REGISTER) {
        return -12;
    }
    const unsigned int pledgeDbId = pledge != nullptr ? pledge->GetDbId() : 0U;
    const int validationCode = m_PledgeEntry.CheckValidPledge(pledgeDbId, true);
    if (validationCode != 0) {
        return validationCode;
    }
    if (point < 1) {
        return -13;
    }
    db.RequestAddMiniGameAgitPledge(static_cast<unsigned int>(ResidenceId(m_data)), pledge, point);
    return 0;
}

int CMiniGameAgit::RemoveEntryPledge(CPledge* const pledge) {
    // Current L2Server 0x0083cb50.
    L2_GUARD_FUNCTION();
    if (m_StateHelper.GetState() != CMiniGameAgitStatus::REGISTER) {
        return -12;
    }
    const int point = m_PledgeEntry.Unregist(pledge->GetDbId());
    if (point < 1) {
        return -11;
    }
    db.RequestDelMiniGameAgitPledge(static_cast<unsigned int>(ResidenceId(m_data)), pledge);

    CLogFormatter formatter;
    formatter.WriteLog4(
        0x11c, nullptr, nullptr, nullptr, pledge->GetPledgeName(), nullptr, pledge->GetDbId(), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    );
    return point;
}

bool CMiniGameAgit::AddEntryPledgeFromDB(const unsigned int pledgeDbId, const int point) {
    // Current L2Server 0x0083e78c.
    L2_GUARD_FUNCTION();
    return m_PledgeEntry.Regist(pledgeDbId, point, true);
}

void CMiniGameAgit::AddMember(const unsigned int pledgeDbId, const unsigned int userDbId) {
    // Current L2Server 0x0083cef4 ignores pledgeDbId.
    L2_GUARD_FUNCTION();
    static_cast<void>(pledgeDbId);
    m_PlayerEntry.AddPlayer(userDbId);
}

int CMiniGameAgit::GetUnreturnedPoint(const unsigned int pledgeDbId) {
    // Current L2Server 0x0083d0cc.
    L2_GUARD_FUNCTION();
    const int point = m_UnreturnedPointKeeper.GetPoint(pledgeDbId);
    m_UnreturnedPointKeeper.Remove(pledgeDbId);
    return point;
}

FVector CMiniGameAgit::GetChallengerRestartPoint(const unsigned int pledgeDbId) {
    const int index = m_SelectedPledgeEntry.GetIndex(pledgeDbId);
    if (index < 0 || static_cast<std::size_t>(index) >= m_AgitEntrancePointList.size()) {
        return FVector{};
    }
    return m_AgitEntrancePointList[static_cast<std::size_t>(index)];
}

void CMiniGameAgit::DeleteGameItem(User* const user) {
    // Current L2Server 0x0083af94.
    L2_GUARD_FUNCTION();
    for (const int itemClassId : m_AgitDeleteItemList) {
        user->DeleteItemInInventory(itemClassId);
    }
}

int CMiniGameAgit::GetJoinErrorCode(User* const user) {
    // Current L2Server 0x0083ccd8.
    L2_GUARD_FUNCTION();
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        return -7;
    }
    if (pledge->IsWaitingToDismiss()) {
        return -1;
    }
    if (pledge->GetMaster() != user) {
        return -2;
    }

    const unsigned int pledgeDbId = pledge->GetDbId();
    if (!m_PledgeEntry.IsRegistered(pledgeDbId) && GetResidenceOwnerPledge() != pledge) {
        return -14;
    }
    if (!m_SelectedPledgeEntry.IsSelected(pledgeDbId)) {
        return -15;
    }

    if (!m_bCheckLoosely) {
        PledgeFunction* const function = pledge->GetFunction();
        if (function == nullptr || !function->IsEnable(PledgeFunctionType::MINIGAME)) {
            return -9;
        }
        if (pledge->GetDbMemberCount() < 5) {
            return -10;
        }
        CWorldAgit* const worldAgit = pledge->GetWorldAgit();
        if (worldAgit != nullptr && worldAgit != this) {
            return -8;
        }
    }

    CParty* const party = user->GetParty();
    if (party == nullptr) {
        return -3;
    }
    if (party->GetMaster() != user) {
        return -4;
    }
    if (!m_bCheckLoosely && party->GetMemberCount() < 5) {
        return -6;
    }
    if (!party->IsAllPartyMemberSamePledge()) {
        return -5;
    }
    return 0;
}

bool CMiniGameAgit::TeleportToGameArea(User* const user) {
    // Current L2Server 0x0083b8c8.
    L2_GUARD_FUNCTION();
    CPledge* const pledge = user->GetPledge();
    const int index = m_SelectedPledgeEntry.GetIndex(pledge->GetDbId());
    if (index == -1) {
        return false;
    }
    if (static_cast<std::size_t>(index) >= m_AgitEntrancePointList.size()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] MiniGameAgit(%d) "
        //     L"agit_entrance_point not set in '%s'",
        //     L"MiniGameAgit.cpp",
        //     0x485,
        //     ResidenceId(m_data),
        //     L"..\\Script\\castledata.txt")
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] MiniGameAgit(%d) "
        //     L"agit_entrance_point not set in '%s'",
        //     L"MiniGameAgit.cpp",
        //     0x485,
        //     ResidenceId(m_data),
        //     L"..\\Script\\castledata.txt")
        L2_LOG_ERROR(
            Log,
            "Mini-game clan hall %d: player teleport refused because entrance index %d has no position (configured "
            "positions %llu); check agit_entrance_point in castledata.txt.",
            ResidenceId(m_data), index, static_cast<unsigned long long>(m_AgitEntrancePointList.size())
        );
        return false;
    }

    DeleteGameItem(user);
    user->DispelByBuffProtectionFlag(0x1f);
    user->TryDismounting(true, false);
    user->DespawnPet(true, false, DESPAWN_PET_NONE, false);
    user->DespawnAllSummon(true, false);
    user->DespawnAllCubic(false);

    const FVector& position = m_AgitEntrancePointList[static_cast<std::size_t>(index)];
    user->TeleportToLocation(
        static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z), 0, 0x10000,
        TeleportSourceType::TeleportToGameArea, nullptr, 0
    );
    return true;
}

bool CMiniGameAgit::PartyTeleportToGameArea(CParty* const party) {
    // Current L2Server 0x0083a610.
    L2_GUARD_FUNCTION();
    User* const master = party->GetMaster();
    if (master == nullptr) {
        return false;
    }
    const int index = m_SelectedPledgeEntry.GetIndex(master->GetPledge()->GetDbId());
    if (index == -1) {
        return false;
    }
    if (static_cast<std::size_t>(index) >= m_AgitEntrancePointList.size()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] MiniGameAgit(%d) "
        //     L"agit_entrance_point not set in '%s'",
        //     L"MiniGameAgit.cpp",
        //     0x49b,
        //     ResidenceId(m_data),
        //     L"..\\Script\\castledata.txt")
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] MiniGameAgit(%d) "
        //     L"agit_entrance_point not set in '%s'",
        //     L"MiniGameAgit.cpp",
        //     0x49b,
        //     ResidenceId(m_data),
        //     L"..\\Script\\castledata.txt")
        L2_LOG_ERROR(
            Log,
            "Mini-game clan hall %d: party teleport refused because entrance index %d has no position (configured positions "
            "%llu); check agit_entrance_point in castledata.txt.",
            ResidenceId(m_data), index, static_cast<unsigned long long>(m_AgitEntrancePointList.size())
        );
        return false;
    }

    const FVector& position = m_AgitEntrancePointList[static_cast<std::size_t>(index)];
    party->TeleportToLocation(
        static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z), 10, 0,
        TeleportSourceType::PartyTeleportToGameArea, nullptr
    );
    return true;
}

int CMiniGameAgit::JoinGame(User* const user) {
    // Current L2Server 0x0083e488.
    L2_GUARD_FUNCTION();
    if (m_PlayerEntry.IsPlayer(user->GetDBID()) && user->GetPledge() != nullptr) {
        return TeleportToGameArea(user) ? 0 : -1;
    }
    if (m_StateHelper.GetState() != CMiniGameAgitStatus::PREPARE) {
        return -12;
    }

    const int error = GetJoinErrorCode(user);
    if (error != 0) {
        return error;
    }
    CParty* const party = user->GetParty();
    if (party == nullptr) {
        return -3;
    }

    std::vector<User*> members;
    party->GetAllMember(members);
    for (User* const member : members) {
        if (member == nullptr) {
            continue;
        }
        DeleteGameItem(member);
        member->DispelByBuffProtectionFlag(0x1f);
        member->TryDismounting(true, false);
        member->DespawnPet(true, false, DESPAWN_PET_NONE, false);
        member->DespawnAllSummon(true, false);
        member->DespawnAllCubic(false);
        m_PlayerEntry.AddPlayer(member->GetDBID());
        db.RequestAddMiniGameAgitMemeber(static_cast<unsigned int>(ResidenceId(m_data)), user->GetPledge(), member);

        CPledge* const memberPledge = member->GetPledge();
        if (memberPledge != nullptr) {
            CLogFormatter formatter;
            formatter.WriteLog1(
                0x11d, member, nullptr, nullptr, memberPledge->GetPledgeName(), nullptr,
                memberPledge->GetDbId(), 0, 0, 0, 0, 0, 0
            );
        }
    }
    PartyTeleportToGameArea(party);
    return 0;
}

void CMiniGameAgit::BroadcastToAllPledgeMaster(const char* const format, ...) {
    // Current L2Server 0x0083bab8.
    L2_GUARD_FUNCTION();
    std::va_list arguments;
    va_start(arguments, format);
    m_PledgeEntry.Broadcast(format, arguments);
    va_end(arguments);
}

void CMiniGameAgit::BroadcastToSelectedPledgeMaster(const char* const format, ...) {
    // Current L2Server 0x0083aeb0.
    L2_GUARD_FUNCTION();
    std::va_list arguments;
    va_start(arguments, format);
    m_SelectedPledgeEntry.Broadcast(format, arguments);
    va_end(arguments);
}

void CMiniGameAgit::BroadcastToUnselectedPledgeMaster(const char* const format, ...) {
    // Current L2Server 0x00840618.
    L2_GUARD_FUNCTION();
    std::vector<unsigned int> selected;
    m_SelectedPledgeEntry.GetList(selected);
    std::va_list arguments;
    va_start(arguments, format);
    m_PledgeEntry.BroadcastExcept(selected, format, arguments);
    va_end(arguments);
}

void CMiniGameAgit::PrepareGame() {
    // Current L2Server 0x00840dfc.
    L2_GUARD_FUNCTION();
    BroadcastToAllPledgeMaster("chc", 0x62, 0x71f, 0);
    if (m_SelectedPledgeEntry.Size() < 2) {
        PassGame();
        return;
    }
    BroadcastToSelectedPledgeMaster("chc", 0x62, 0x720, 0);
    BroadcastToUnselectedPledgeMaster("chc", 0x62, 0x721, 0);
    m_nLastAlarmSec = static_cast<int>(m_StateHelper.GetGameStartRemainSec()) + 1;
}

void CMiniGameAgit::StartGame() {
    // Current L2Server 0x00840f18.
    L2_GUARD_FUNCTION();
    if (m_SelectedPledgeEntry.Size() < 2) {
        PassGame();
        return;
    }
    SendNpcState(m_data, 1, 0);
    MakeNPCDisable(true);
    BroadcastSystemMessageW(SystemMessageId::CLAN_HALL_SIEGE_STARTED);
}

void CMiniGameAgit::RestartGame() {
    // Current L2Server 0x00841030.
    L2_GUARD_FUNCTION();
    MakeSelectedEntry(false);
    if (m_SelectedPledgeEntry.Size() < 2) {
        PassGame();
        return;
    }
    SendNpcState(m_data, 1, 1);
    MakeNPCDisable(true);
    BroadcastSystemMessageW(SystemMessageId::CLAN_HALL_SIEGE_STARTED);
}

void CMiniGameAgit::HandleState() {
    // Current L2Server 0x00841150.
    L2_GUARD_FUNCTION();
    if (m_StateHelper.IsPaused()) {
        ResumeGame();
        m_StateHelper.UpdateState();
        if (m_StateHelper.GetState() == CMiniGameAgitStatus::PROCESS) {
            if (!m_bSiegeContinued) {
                SendNpcState(m_data, 1, 1);
                MakeNPCDisable(true);
            } else {
                RestartGame();
                ChangeResidenceState(RS_SIEGE);
                m_bSiegeContinued = false;
            }
        }
        if (m_StateHelper.GetState() == CMiniGameAgitStatus::PREPARE) {
            m_bSiegeContinued = true;
        }
    }

    if (m_StateHelper.GetState() == CMiniGameAgitStatus::REGISTER) {
        m_StateHelper.TurnOn();
        m_nWinnerPledgeDbId = 0;
        MakeSelectedEntry(true);
    }

    if (m_StateHelper.GetState() == CMiniGameAgitStatus::CLOSE || m_nWinnerPledgeDbId == 0) {
        if (m_StateHelper.UpdateState()) {
            switch (m_StateHelper.GetState()) {
                case CMiniGameAgitStatus::PREPARE:
                    PrepareGame();
                    break;
                case CMiniGameAgitStatus::PROCESS:
                    StartSiege();
                    ChangeResidenceState(RS_SIEGE);
                    break;
                case CMiniGameAgitStatus::CLOSE:
                    CloseGame();
                    break;
                case CMiniGameAgitStatus::CLEAN_UP:
                    EndSiege();
                    break;
                default:
                    break;
            }
        }
    } else {
        m_StateHelper.CloseForcely();
        CloseGame();
    }

    db.RequestSetMiniGameAgitStatus(*this, m_StateHelper.GetElapsedMillisec());
    SendTimeElapsedMessage();
}

void CMiniGameAgit::ClearGame() {
    // Current L2Server 0x0084139c.
    L2_GUARD_FUNCTION();
    m_PlayerEntry.DeleteGameItem(m_AgitDeleteItemList);
    m_PlayerEntry.Banish(m_nWinnerPledgeDbId, m_OwnerRestartPointList, m_BanishPointList);
    m_StateHelper.Clear();
    m_PledgeEntry.Clear();
    m_SelectedPledgeEntry.Clear();
    m_PlayerEntry.Clear();
    m_nWinnerPledgeDbId = 0;
    ChangeResidenceState(RS_STANDBY);
    if (m_data != nullptr) {
        m_data->m_NextSiege = static_cast<std::int32_t>(static_cast<std::uint32_t>(m_data->m_NextSiege) + 0x127500U);
    }
    db.RequestSetNextAgitBattleTime(this);
}

void CMiniGameAgit::EndGame() {
    // Current L2Server 0x0083c984.
    L2_GUARD_FUNCTION();
    db.RequestResetMiniGameAgit(*this);
    m_UnreturnedPointKeeper.Clear();
}

void CMiniGameAgit::PassGame() {
    // Current L2Server 0x0083fffc.
    L2_GUARD_FUNCTION();
    db.RequestResetMiniGameAgit(*this);
    BroadcastToSelectedPledgeMaster("chc", 0x62, 0x731, 0);
    BroadcastSystemMessageW(SystemMessageId::CLAN_HALL_SIEGE_ENDED_IN_DRAW);
    m_UnreturnedPointKeeper.Clear();
    if (m_SelectedPledgeEntry.Size() > 0) {
        std::vector<unsigned int> selected;
        m_SelectedPledgeEntry.GetList(selected);
        for (const unsigned int pledgeDbId : selected) {
            m_UnreturnedPointKeeper.Add(pledgeDbId, static_cast<unsigned int>(m_PledgeEntry.GetPoint(pledgeDbId)));
        }
    }
    CPledge* const owner = GetResidenceOwnerPledge();
    if (owner != nullptr && owner->GetFunction()->IsEnable(PledgeFunctionType::MINIGAME)) {
        owner->UpdateNameValue(NameValueForAgit::SUCCESS_DEFENCE);
    }
}

void CMiniGameAgit::CloseGame() {
    // Current L2Server 0x0083e2b4.
    L2_GUARD_FUNCTION();
    SendNpcState(m_data, 0, 0);
    MakeNPCDisable(false);
    BroadcastSystemMessageW(SystemMessageId::CLAN_HALL_SIEGE_ENDED);

    CSPointer<CPledge> winner = CPledge::FindPledge(g_nWorldId, static_cast<int>(m_nWinnerPledgeDbId));
    const int previousOwnerPledgeId = m_data != nullptr ? static_cast<int>(m_data->m_nResidenceOwnerID) : 0;
    if (!winner) {
        BroadcastSystemMessageW(SystemMessageId::CLAN_HALL_SIEGE_ENDED_IN_DRAW);
        ChangeOwner(nullptr, 0, true);
    } else {
        BroadcastSystemMessageW(winner->GetPledgeName(), SystemMessageId::CLAN_WON_NAMED_CLAN_HALL_SIEGE);
        ChangeOwner(winner.get(), 0, true);
    }
    UpdatePledgeNameValue(previousOwnerPledgeId, winner.get());
    m_PlayerEntry.DeleteGameItem(m_AgitDeleteItemList);
}

void CMiniGameAgit::MakeSelectedEntry(const bool logResult) {
    // Current L2Server 0x008401a0.
    L2_GUARD_FUNCTION();
    CPledge* const owner = GetResidenceOwnerPledge();
    if (owner != nullptr) {
        m_SelectedPledgeEntry.Add(owner->GetDbId());
        if (logResult) {
            CLogFormatter formatter;
            formatter.WriteLog4(
                0x11b, nullptr, nullptr, nullptr, owner->GetPledgeName(), nullptr, owner->GetDbId(), 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0
            );
        }
    }

    std::vector<unsigned int> pledges;
    m_PledgeEntry.GetTopN(4 - (owner != nullptr ? 1 : 0), pledges, false, logResult);
    for (const unsigned int pledgeDbId : pledges) {
        m_SelectedPledgeEntry.Add(pledgeDbId);
        if (!logResult) {
            continue;
        }
        CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, static_cast<int>(pledgeDbId));
        if (pledge) {
            CLogFormatter formatter;
            formatter.WriteLog4(
                0x11b, nullptr, nullptr, nullptr, pledge->GetPledgeName(), nullptr, pledge->GetDbId(), 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0
            );
        }
    }
}

void CMiniGameAgit::LoadTopNPledgeFromDB() {
    // Current L2Server 0x00840464.
    L2_GUARD_FUNCTION();
    std::vector<unsigned int> pledges;
    m_PledgeEntry.GetTopN(4, pledges, true, false);
    for (const unsigned int pledgeDbId : pledges) {
        if (pledgeDbId == 0) {
            continue;
        }
        CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, static_cast<int>(pledgeDbId));
        if (!pledge) {
            db.RequestLoadPledge(g_nWorldId, pledgeDbId, false);
            // Current code supplies no value for the embedded %d.
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR] ::LoadTopNPledgeFromDB "
            //     L"LoadPledge PledgeDBID[%d]")
            // Noise: Loading a referenced clan is routine startup work. This per-clan trace also has a missing format
            // argument. The database request is retained. L2_LOG_ERROR(Log,
            //     L"[NO_ERROR] ::LoadTopNPledgeFromDB "
            //     L"LoadPledge PledgeDBID[%d]")
        }
    }
}

bool CMiniGameAgitDBHandler::ReplyLoadMiniGameAgit(CDBSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00840708, RP_EX 0x28.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    std::int32_t residenceId;
    std::int32_t elapsedMilliseconds;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &residenceId, &elapsedMilliseconds);

    CMiniGameAgit* const agit = CMiniGameAgit::GetMiniGameAgit(residenceId);
    if (agit == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] MiniGameAgit(%d) is null.",
        //     L"MiniGameAgit.cpp",
        //     0x638,
        //     residenceId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] MiniGameAgit(%d) is null.",
        //     L"MiniGameAgit.cpp",
        //     0x638,
        //     residenceId)
        L2_LOG_ERROR(Log, "Mini-game clan hall %d was not found; its database load reply is not applied.", residenceId);
        return false;
    }

    agit->LoadFromDB(elapsedMilliseconds);

    std::int32_t pledgeCount;
    cursor = ::Disassemble(cursor, "d", &pledgeCount);
    if (pledgeCount < 201) {
        for (std::int32_t index = 0; index < pledgeCount; ++index) {
            std::int32_t pledgeDatabaseId;
            std::int32_t point;
            cursor = ::Disassemble(cursor, "dd", &pledgeDatabaseId, &point);
            agit->AddEntryPledgeFromDB(static_cast<unsigned int>(pledgeDatabaseId), point);
        }
        agit->LoadTopNPledgeFromDB();
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] MiniGameAgit(%d) nPledgeCount > %d",
        //     L"MiniGameAgit.cpp",
        //     0x640,
        //     residenceId,
        //     200)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] MiniGameAgit(%d) nPledgeCount > %d",
        //     L"MiniGameAgit.cpp",
        //     0x640,
        //     residenceId,
        //     200)
        L2_LOG_ERROR(
            Log,
            "Mini-game clan hall %d: database load reply declares %d clans, exceeding the limit of 200; clan entries are "
            "not loaded, but reply processing continues.",
            residenceId, pledgeCount
        );
    }

    std::int32_t memberCount;
    cursor = ::Disassemble(cursor, "d", &memberCount);
    if (memberCount < 37) {
        for (std::int32_t index = 0; index < memberCount; ++index) {
            std::int32_t pledgeDatabaseId;
            std::int32_t userDatabaseId;
            cursor = ::Disassemble(cursor, "dd", &pledgeDatabaseId, &userDatabaseId);
            agit->AddMember(static_cast<unsigned int>(pledgeDatabaseId), static_cast<unsigned int>(userDatabaseId));
        }
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] MiniGameAgit(%d) nUserCount > %d",
        //     L"MiniGameAgit.cpp",
        //     0x651,
        //     residenceId,
        //     36)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] MiniGameAgit(%d) nUserCount > %d",
        //     L"MiniGameAgit.cpp",
        //     0x651,
        //     residenceId,
        //     36)
        L2_LOG_ERROR(
            Log,
            "Mini-game clan hall %d: database load reply declares %d members, exceeding the limit of 36; member entries are "
            "not loaded, but timer startup continues.",
            residenceId, memberCount
        );
    }

    agit->StartTimer();
    if (g_JobFinishSignal.SetAgitLoaded()) {
        CPledge::SetFlagReservePledge(PRLT_AGIT);
    }
    return false;
}

bool CMiniGameAgitDBHandler::ReplyAddMiniGameAgitPledge(CDBSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x0083ed18, RP_EX 0x29.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    std::int32_t result;
    std::int32_t requestContext;
    std::int32_t residenceId;
    std::int32_t pledgeDatabaseId;
    std::int32_t point;
    ::Disassemble(packet, "ddddd", &result, &requestContext, &residenceId, &pledgeDatabaseId, &point);

    // Current code extracts the raw pointer from a temporary CSPointer and
    // releases that temporary before GetContext overwrites the same lane.
    CPledge* pledge = CPledge::FindPledge(g_nWorldId, pledgeDatabaseId).get();
    if (!Singleton<CCacheContextManager>::inst().GetContext(requestContext, "p", &pledge)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid context id[%x]",
        //     L"MiniGameAgit.cpp",
        //     0x681,
        //     requestContext)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid context id[%x]",
        //     L"MiniGameAgit.cpp",
        //     0x681,
        //     requestContext)
        L2_LOG_ERROR(
            Log,
            "Mini-game clan hall %d: cannot restore request context 0x%08x for the clan-registration reply (clan %d, result "
            "%d); the reply is not applied.",
            residenceId, static_cast<unsigned>(requestContext), pledgeDatabaseId, result
        );
        return false;
    }

    CMiniGameAgit* const agit = CMiniGameAgit::GetMiniGameAgit(residenceId);
    if (agit == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] MiniGameAgit(%d) is null.",
        //     L"MiniGameAgit.cpp",
        //     0x688,
        //     residenceId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] MiniGameAgit(%d) is null.",
        //     L"MiniGameAgit.cpp",
        //     0x688,
        //     residenceId)
        L2_LOG_ERROR(
            Log, "Mini-game clan hall %d was not found; the clan-registration reply for clan %d (result %d) is not applied.",
            residenceId, pledgeDatabaseId, result
        );
    } else if (result == 0) {
        agit->AddEntryPledgeFromDB(static_cast<unsigned int>(pledgeDatabaseId), point);
        if (pledge == nullptr) {
            return false;
        }

        CLogFormatter formatter;
        formatter.WriteLog4(
            0x11a, nullptr, nullptr, nullptr, pledge->GetPledgeName(), nullptr, pledge->GetDbId(), point, 0, 0, 0, 0, 0, 0,
            0, 0, 0
        );
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] MiniGameAgit DB process fail"
        //     L"(RP_ADD_MINIGAME_AGIT_PLEDGE_EX) error_code(%d) "
        //     L"agit_id(%d) pledge_id(%d) point(%d).",
        //     L"MiniGameAgit.cpp",
        //     0x68d,
        //     result,
        //     residenceId,
        //     pledgeDatabaseId,
        //     point)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] MiniGameAgit DB process fail"
        //     L"(RP_ADD_MINIGAME_AGIT_PLEDGE_EX) error_code(%d) "
        //     L"agit_id(%d) pledge_id(%d) point(%d).",
        //     L"MiniGameAgit.cpp",
        //     0x68d,
        //     result,
        //     residenceId,
        //     pledgeDatabaseId,
        //     point)
        L2_LOG_ERROR(
            Log,
            "Mini-game clan hall %d: database clan registration failed with result %d (clan %d, points %d); no entry is "
            "added from this reply.",
            residenceId, result, pledgeDatabaseId, point
        );
    }

    if (pledge != nullptr) {
        L2_RELEASE(pledge, ObjectRefType::Cached);
    }
    return false;
}

bool CMiniGameAgitDBHandler::ReplyAddMiniGameAgitMember(CDBSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x0083d188, RP_EX 0x2a.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    std::int32_t result;
    std::int32_t requestContext;
    std::int32_t residenceId;
    std::int32_t pledgeDatabaseId;
    std::int32_t userDatabaseId;
    ::Disassemble(packet, "ddddd", &result, &requestContext, &residenceId, &pledgeDatabaseId, &userDatabaseId);

    CPledge* pledge = nullptr;
    User* user = nullptr;
    if (!Singleton<CCacheContextManager>::inst().GetContext(requestContext, "pp", &pledge, &user)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid context id[%x]",
        //     L"MiniGameAgit.cpp",
        //     0x6ba,
        //     requestContext)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid context id[%x]",
        //     L"MiniGameAgit.cpp",
        //     0x6ba,
        //     requestContext)
        L2_LOG_ERROR(
            Log,
            "Mini-game clan hall %d: cannot restore request context 0x%08x for the member-registration reply (clan %d, "
            "character DB ID %d, result %d); the reply is not applied.",
            residenceId, static_cast<unsigned>(requestContext), pledgeDatabaseId, userDatabaseId, result
        );
        return false;
    }

    CMiniGameAgit* const agit = CMiniGameAgit::GetMiniGameAgit(residenceId);
    if (agit == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] MiniGameAgit(%d) is null.",
        //     L"MiniGameAgit.cpp",
        //     0x6c0,
        //     residenceId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] MiniGameAgit(%d) is null.",
        //     L"MiniGameAgit.cpp",
        //     0x6c0,
        //     residenceId)
        L2_LOG_ERROR(
            Log,
            "Mini-game clan hall %d was not found; the member-registration reply for clan %d, character DB ID %d (result "
            "%d) is not applied.",
            residenceId, pledgeDatabaseId, userDatabaseId, result
        );
    } else if (result == 0) {
        agit->AddMember(static_cast<unsigned int>(pledgeDatabaseId), static_cast<unsigned int>(userDatabaseId));
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] MiniGameAgit DB process fail"
        //     L"(RP_ADD_MINIGAME_AGIT_MEMBER_EX) error_code(%d) "
        //     L"agit_id(%d) pledge_id(%d) user_id(%d).",
        //     L"MiniGameAgit.cpp",
        //     0x6c3,
        //     result,
        //     residenceId,
        //     pledgeDatabaseId,
        //     userDatabaseId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] MiniGameAgit DB process fail"
        //     L"(RP_ADD_MINIGAME_AGIT_MEMBER_EX) error_code(%d) "
        //     L"agit_id(%d) pledge_id(%d) user_id(%d).",
        //     L"MiniGameAgit.cpp",
        //     0x6c3,
        //     result,
        //     residenceId,
        //     pledgeDatabaseId,
        //     userDatabaseId)
        L2_LOG_ERROR(
            Log,
            "Mini-game clan hall %d: database member registration failed with result %d (clan %d, character DB ID %d); no "
            "member is added from this reply.",
            residenceId, result, pledgeDatabaseId, userDatabaseId
        );
    }

    // Successful context extraction in the target unconditionally releases
    // both registered objects without null checks.
    L2_RELEASE(pledge, ObjectRefType::Cached);
    L2_RELEASE(user, ObjectRefType::Cached);
    return false;
}

bool CMiniGameAgitDBHandler::ReplyResetMiniGameAgit(CDBSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x008414ec, RP_EX 0x2b.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    std::int32_t result;
    std::int32_t residenceId;
    ::Disassemble(packet, "dd", &result, &residenceId);

    CMiniGameAgit* const agit = CMiniGameAgit::GetMiniGameAgit(residenceId);
    if (agit == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] MiniGameAgit(%d) is null.",
        //     L"MiniGameAgit.cpp",
        //     0x6e1,
        //     residenceId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] MiniGameAgit(%d) is null.",
        //     L"MiniGameAgit.cpp",
        //     0x6e1,
        //     residenceId)
        L2_LOG_ERROR(
            Log, "Mini-game clan hall %d was not found; its database reset reply (result %d) is not applied.", residenceId,
            result
        );
        return false;
    }

    if (result != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] MiniGameAgit(%d) DB process fail"
        //     L"(RP_RESET_MINIGAME_AGIT_EX) error_code(%d).",
        //     L"MiniGameAgit.cpp",
        //     0x6e8,
        //     residenceId,
        //     result)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] MiniGameAgit(%d) DB process fail"
        //     L"(RP_RESET_MINIGAME_AGIT_EX) error_code(%d).",
        //     L"MiniGameAgit.cpp",
        //     0x6e8,
        //     residenceId,
        //     result)
        L2_LOG_ERROR(
            Log, "Mini-game clan hall %d: database reset failed with result %d; local game cleanup still proceeds.",
            residenceId, result
        );
    }
    agit->ClearGame();
    return false;
}

void CMiniGameAgit::BuilderCmd_ShowInfo(CUserSocket* const socket) {
    // Current L2Server 0x0083e84c.
    L2_GUARD_FUNCTION();
    char16_t message[1024];
    const std::wstring residenceName = Utf16ToWide(ResidenceName(m_data));
    FormatUtf16To(message, std::size(message), u"-- mini game agit[%ls, %d]", residenceName.c_str(), ResidenceId(m_data));
    socket->SendSystemMessage(u"SYS", message);

    FormatUtf16To(message, std::size(message), u"  status [%ls]", m_StateHelper.GetStateString());
    socket->SendSystemMessage(u"SYS", message);

    switch (m_StateHelper.GetState()) {
        case CMiniGameAgitStatus::REGISTER:
            m_PledgeEntry.BuilderCmd_ShowInfo(socket);
            break;
        case CMiniGameAgitStatus::PREPARE:
        case CMiniGameAgitStatus::PROCESS:
        case CMiniGameAgitStatus::CLOSE:
        case CMiniGameAgitStatus::CLEAN_UP:
            m_PledgeEntry.BuilderCmd_ShowInfo(socket);
            m_SelectedPledgeEntry.BuilderCmd_ShowInfo(socket);
            m_PlayerEntry.BuilderCmd_ShowInfo(socket);
            break;
        default:
            break;
    }
}

namespace CMiniGameAgitBuilderCmd {

bool SetTime(CUserSocket* const socket, User*, char16_t* command) {
    // Current L2Server 0x0083a470.
    L2_GUARD_FUNCTION();
    char16_t* const residenceToken = TakeMiniGameBuilderToken(command);
    if (residenceToken != nullptr) {
        CMiniGameAgit* const agit = CMiniGameAgit::GetMiniGameAgit(WindowsWtol(residenceToken));
        if (agit != nullptr) {
            char16_t* const prepareToken = TakeMiniGameBuilderToken(command);
            if (prepareToken != nullptr) {
                const int prepareSeconds = WindowsWtol(prepareToken);
                if (prepareSeconds > 0) {
                    char16_t* const processToken = TakeMiniGameBuilderToken(command);
                    if (processToken != nullptr) {
                        const int processSeconds = WindowsWtol(processToken);
                        if (processSeconds > 0) {
                            char16_t* const closeToken = TakeMiniGameBuilderToken(command);
                            if (closeToken != nullptr) {
                                const int closeSeconds = WindowsWtol(closeToken);
                                if (closeSeconds > 0) {
                                    agit->m_TimeSetting.SetTime(prepareSeconds, processSeconds, closeSeconds, false);
                                    socket->SendSystemMessage(u"SYS", u"Builder Command 'set_mg_time' accepted...");
                                    return false;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    socket->SendSystemMessage(u"SYS", u"//set_mg_time [castle_id] [waiting_time_sec] [game_time_sec] [close_time_sec]");
    return false;
}

bool PledgeCheckLoosely(CUserSocket* const socket, User*, char16_t* command) {
    // Current L2Server 0x0083a8dc.
    L2_GUARD_FUNCTION();
    char16_t* const residenceToken = TakeMiniGameBuilderToken(command);
    if (residenceToken != nullptr) {
        CMiniGameAgit* const agit = CMiniGameAgit::GetMiniGameAgit(WindowsWtol(residenceToken));
        if (agit != nullptr) {
            char16_t* const modeToken = TakeMiniGameBuilderToken(command);
            if (modeToken != nullptr) {
                const bool loosely = std::u16string_view(modeToken).compare(u"on") == 0;
                agit->BuilderCmd_PledgeCheckLoosely(socket, loosely);
                socket->SendSystemMessage(u"SYS", u"Builder Command 'mg_pledge_check_loosely' accepted...");
                return false;
            }
        }
    }
    socket->SendSystemMessage(u"SYS", u"//mg_pledge_check_loosely [agit_id] [on/off]");
    return false;
}

bool SetWinner(CUserSocket* const socket, User* const user, char16_t* command) {
    // Current L2Server 0x0083b054.
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->m_data->m_nWorldId != g_nWorldId) {
        return false;
    }

    char16_t* const residenceToken = TakeMiniGameBuilderToken(command);
    if (residenceToken != nullptr) {
        CMiniGameAgit* const agit = CMiniGameAgit::GetMiniGameAgit(WindowsWtol(residenceToken));
        if (agit != nullptr) {
            char16_t* const pledgeToken = TakeMiniGameBuilderToken(command);
            if (pledgeToken != nullptr) {
                std::u16string pledgeName = std::u16string(pledgeToken);
                CSPointer<CPledge> pledge =
                    CPledge::FindPledge(g_nWorldId, reinterpret_cast<SharedUtilsWideChar*>(pledgeName.data()));
                if (pledge) {
                    agit->OnSetWinner(pledge.get());
                    socket->SendSystemMessage(u"SYS", u"Builder Command 'set_mg_winner' accepted...");
                    return false;
                }
            }
        }
    }
    socket->SendSystemMessage(u"SYS", u"//set_mg_winner [castle_id] [pledge_name]");
    return false;
}

bool RegisterPledge(CUserSocket* const socket, User* const user, char16_t* command) {
    // Current L2Server 0x0083d894.
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->m_data->m_nWorldId != g_nWorldId) {
        return false;
    }

    char16_t* const residenceToken = TakeMiniGameBuilderToken(command);
    if (residenceToken != nullptr) {
        CMiniGameAgit* const agit = CMiniGameAgit::GetMiniGameAgit(WindowsWtol(residenceToken));
        if (agit != nullptr) {
            char16_t* const pledgeToken = TakeMiniGameBuilderToken(command);
            if (pledgeToken != nullptr) {
                std::u16string pledgeName = std::u16string(pledgeToken);
                CSPointer<CPledge> pledge =
                    CPledge::FindPledge(g_nWorldId, reinterpret_cast<SharedUtilsWideChar*>(pledgeName.data()));
                if (pledge) {
                    char16_t* const certificateToken = TakeMiniGameBuilderToken(command);
                    if (certificateToken != nullptr) {
                        const int certificateCount = WindowsWtol(certificateToken);
                        if (certificateCount > 0) {
                            const int result = agit->AddEntryPledge(pledge.get(), certificateCount);
                            if (result == 0) {
                                socket->SendSystemMessage(u"SYS", u"Builder Command 'register_mg_pledge' accepted...");
                            } else {
                                std::array<char16_t, 1024> message{};
                                FormatUtf16To(
                                    message.data(), message.size(), u"register_mg_pledge failure... errorcode[%d]", result
                                );
                                socket->SendSystemMessage(u"SYS", message.data());
                            }
                            return false;
                        }
                    }
                }
            }
        }
    }
    socket->SendSystemMessage(u"SYS", u"//register_mg_pledge [castle_id] [pledge_name] [certificate_num]");
    return false;
}

bool JoinGame(CUserSocket* const socket, User*, char16_t* command) {
    // Current L2Server 0x0083f100.
    L2_GUARD_FUNCTION();
    char16_t* const residenceToken = TakeMiniGameBuilderToken(command);
    if (residenceToken != nullptr) {
        CMiniGameAgit* const agit = CMiniGameAgit::GetMiniGameAgit(WindowsWtol(residenceToken));
        if (agit != nullptr) {
            char16_t* const characterToken = TakeMiniGameBuilderToken(command);
            if (characterToken != nullptr) {
                CSPointer<User> character = GetUserFromNameMap(g_nWorldId, std::u16string(characterToken));
                if (character) {
                    const int result = agit->JoinGame(character.get());
                    if (result == 0) {
                        socket->SendSystemMessage(u"SYS", u"Builder Command 'join_mg' accepted...");
                    } else {
                        std::array<char16_t, 1024> message{};
                        FormatUtf16To(message.data(), message.size(), u"join_mg failure... errorcode[%d]", result);
                        socket->SendSystemMessage(u"SYS", message.data());
                    }
                    return false;
                }
            }
        }
    }
    socket->SendSystemMessage(u"SYS", u"//join_mg [castle_id] [char_name]");
    return false;
}

bool View(CUserSocket* const socket, User*, char16_t* command) {
    // Current L2Server 0x0083f338.
    L2_GUARD_FUNCTION();
    char16_t* const residenceToken = TakeMiniGameBuilderToken(command);
    if (residenceToken != nullptr) {
        CMiniGameAgit* const agit = CMiniGameAgit::GetMiniGameAgit(WindowsWtol(residenceToken));
        if (agit != nullptr) {
            agit->BuilderCmd_ShowInfo(socket);
            return false;
        }
    }
    socket->SendSystemMessage(u"SYS", u"//view_mg [castle_id]");
    return false;
}

} // namespace CMiniGameAgitBuilderCmd

void CMiniGameAgit::SendTimeElapsedMessage() {
    // Current L2Server 0x0083e9c8. The target uses unsigned
    // subtraction for both one-second windows.
    L2_GUARD_FUNCTION();
    const std::uint32_t startRemaining = m_StateHelper.GetGameStartRemainSec();
    constexpr std::array<int, 6> kMasterMinuteMarks{60, 50, 40, 30, 20, 10};
    for (const int minutes : kMasterMinuteMarks) {
        const int seconds = minutes * 60;
        if (startRemaining <= static_cast<std::uint32_t>(seconds) && seconds < m_nLastAlarmSec) {
            BroadcastToSelectedPledgeMaster("chccc", 0x62, 0x722, 1, 0x14, static_cast<int>(static_cast<char>(minutes)));
            m_nLastAlarmSec = static_cast<std::int32_t>(startRemaining);
            return;
        }
    }

    constexpr std::array<int, 4> kTerritoryMinuteMarks{5, 3, 2, 1};
    for (const int minutes : kTerritoryMinuteMarks) {
        const int seconds = minutes * 60;
        if (startRemaining <= static_cast<std::uint32_t>(seconds) && seconds < m_nLastAlarmSec) {
            g_ptlUserMap->BroadcastToNeighborRect(
                nullptr, 0x80000, *m_OwnerRestartPointList.begin(), 0x4000, "chccc", 0x62, 0x723, 1, 0x14,
                static_cast<int>(static_cast<char>(minutes))
            );
            m_nLastAlarmSec = static_cast<std::int32_t>(startRemaining);
            return;
        }
    }

    if (startRemaining - 1U < 10U) {
        g_ptlUserMap->BroadcastToNeighborRect(
            nullptr, 0x80000, *m_OwnerRestartPointList.begin(), 0x4000, "chccc", 0x62, 0x724, 1, 0x14,
            static_cast<int>(static_cast<char>(startRemaining))
        );
        m_nLastAlarmSec = static_cast<std::int32_t>(startRemaining);
        return;
    }

    const std::uint32_t endRemaining = m_StateHelper.GetGameEndRemainSec();
    if (endRemaining == 60U) {
        g_ptlUserMap->BroadcastToNeighborRect(
            nullptr, 0x80000, *m_OwnerRestartPointList.begin(), 0x4000, "chccc", 0x62, 0x8c4, 1, 0x14, 1
        );
        m_nLastAlarmSec = 60;
    } else if (endRemaining == 30U || endRemaining == 20U || endRemaining - 1U < 10U) {
        g_ptlUserMap->BroadcastToNeighborRect(
            nullptr, 0x80000, *m_OwnerRestartPointList.begin(), 0x4000, "chccc", 0x62, 0x8c5, 1, 0x14,
            static_cast<int>(static_cast<char>(endRemaining))
        );
        m_nLastAlarmSec = static_cast<std::int32_t>(endRemaining);
    }
}

CMiniGameAgitStatus::state CMiniGameAgitStatus::CalcState() {
    L2_GUARD_FUNCTION();

    if (m_msecElapsed == 0) {
        return REGISTER;
    }

    const std::uint32_t prepareSeconds = static_cast<std::uint32_t>(m_TimeSetting.GetPrepareSec());
    if (m_msecElapsed < prepareSeconds * 1000U) {
        return PREPARE;
    }

    const std::uint32_t processEndSeconds = prepareSeconds + static_cast<std::uint32_t>(m_TimeSetting.GetProcessSec());
    if (m_msecElapsed < processEndSeconds * 1000U) {
        return PROCESS;
    }

    const std::uint32_t closeEndSeconds = processEndSeconds + static_cast<std::uint32_t>(m_TimeSetting.GetCloseSec());
    if (m_msecElapsed < closeEndSeconds * 1000U) {
        return CLOSE;
    }

    return CLEAN_UP;
}

bool CMiniGameAgitStatus::UpdateState() {
    L2_GUARD_FUNCTION();

    UpdateElapsedTime();
    const state nextState = CalcState();
    if (nextState == m_State) {
        return false;
    }

    m_State = nextState;
    return true;
}

const wchar_t* CMiniGameAgitStatus::GetStateString() {
    L2_GUARD_FUNCTION();

    switch (m_State) {
        case REGISTER:
            return L"REGISTER";
        case PREPARE:
            return L"PREPARE";
        case PROCESS:
            return L"PROCESS";
        case CLOSE:
            return L"CLOSE";
        case CLEAN_UP:
            return L"CLEAN_UP";
        default:
            return L"NONE";
    }
}

std::uint32_t CMiniGameAgitStatus::GetGameStartRemainSec() {
    L2_GUARD_FUNCTION();

    if (m_msecElapsed == 0) {
        return static_cast<std::uint32_t>(m_TimeSetting.GetPrepareSec());
    }

    const std::uint32_t prepareMilliseconds = Milliseconds(m_TimeSetting.GetPrepareSec());
    if (m_msecElapsed < prepareMilliseconds) {
        return (prepareMilliseconds - m_msecElapsed) / 1000U;
    }
    return 0;
}

std::uint32_t CMiniGameAgitStatus::GetGameEndRemainSec() {
    L2_GUARD_FUNCTION();

    const std::uint32_t prepareMilliseconds = Milliseconds(m_TimeSetting.GetPrepareSec());
    if (m_msecElapsed <= prepareMilliseconds) {
        return 0;
    }

    const std::uint32_t elapsedAfterPrepare = m_msecElapsed - prepareMilliseconds;
    const std::uint32_t processMilliseconds = Milliseconds(m_TimeSetting.GetProcessSec());
    if (elapsedAfterPrepare >= processMilliseconds) {
        return 0;
    }

    return (elapsedAfterPrepare - processMilliseconds) / 1000U;
}

bool CMiniGameAgitPlayerEntry::IsPlayer(unsigned int userDbId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EntrySect);
    return m_Entry.find(userDbId) != m_Entry.end();
}

bool CMiniGameAgit::IsPlayer(unsigned int userDbId) {
    L2_GUARD_FUNCTION();
    return m_PlayerEntry.IsPlayer(userDbId);
}

bool CMiniGameAgit::IsPlayer(User* user) {
    L2_GUARD_FUNCTION();

    const FVector position = user->GetPos();
    CResidence* const residence = g_ResidenceDB.GetWhoseBattleFieldWithBattleCampZone(position);
    if (residence != nullptr && residence->CastWorldAgit() != nullptr &&
        residence->CastWorldAgit()->CastMiniGameAgit() != nullptr) {
        return residence->CastWorldAgit()->CastMiniGameAgit()->IsPlayer(user->GetDBID());
    }
    return false;
}
