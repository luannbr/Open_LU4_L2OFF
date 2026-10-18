#include "Party.h"
#include "Protocol/src/L2Constants.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/Log.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TextRuntime.h"

#include "CConfig.h"
#include "CWorld.h"
#include "CWorldObject.h"
#include "CursedWeapon.h"
#include "Item.h"
#include "MultiPartyCommandChannel.h"
#include "MultiPartyCommandChannelManager.h"
#include "MultiPartyCommandChannelRegistry.h"
#include "ObjectDB.h"
#include "PVPMatch.h"
#include "PartyMatchingRequest.h"
#include "PartyMatchingWaitingRoom.h"
#include "Pet.h"
#include "ReplyChecker.h"
#include "SharedContainer.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/Utils.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr const char* kPartySourceFileNarrow = "Party.cpp";

constexpr std::int16_t kTacticalSignClientIndex[4] = {
    0x0a68,
    0x0a69,
    0x0a6a,
    0x0a6b,
};

std::uint32_t ReadObjectSid(const CObject* object) noexcept { return object ? object->m_nId : 0; }

std::uint32_t ReadItemClassId(const CItem* item) noexcept {
    return item ? static_cast<std::uint32_t>(item->GetClassId()) : 0;
}

std::int64_t ReadItemAmount(const CItem* item) noexcept {
    // Current RouteItem/RouteAdena/CheckRouteAdenaCond/SweepItem and
    // CheckRouteItemCond all read CItem::m_data->m_nCount directly.
    return item->m_data->m_nCount;
}

std::uint32_t ReadItemInventoryId(const CItem* item) noexcept {
    // CheckRouteAdenaCond reloads this field after CheckAddable. Retain a
    // separate direct read at each source call site.
    return static_cast<std::uint32_t>(item->m_data->m_nClassID);
}

std::int32_t ReadItemEtcItemType(const CItem* item) noexcept { return item->m_const->m_EtcitemType; }

bool ItemTracksAdena(const CItem* item) noexcept {
    // RouteAdena gates the User adena accumulator on the current CItem
    // trace-NPC-class-ID value at +0x74.
    return item->m_nTraceNPCClassId != 0;
}

CWorldObject* ItemWorldObject(CItem* item) noexcept {
    // CheckRouteAdenaCond applies the recovered integer IsInRange body to
    // that base view directly.
    return static_cast<CWorldObject*>(item);
}

CObject* ItemObject(CItem* item) noexcept { return static_cast<CObject*>(item); }

bool ItemForcesIndividualRouting(CItem* item) noexcept {
    // Current SweepItem, CheckRouteItem and CheckRouteItemCond all execute
    // the same short-circuited pair: virtual CItem::IsEtcItem followed by
    // CSharedItemConstant::m_EtcitemType (+0x08) == 8.
    return ItemObject(item)->IsEtcItem() && ReadItemEtcItemType(item) == 8;
}


User* ResolveUser(std::uint32_t sid) {
    CObject* const object = CObject::GetObjectW(sid);
    if (!object || !object->IsPC()) {
        return nullptr;
    }
    return object->CastUser();
}

User* ResolveRawUser(std::uint32_t sid) {
    // CheckRouteAdenaCond's current body uses the generic smart-ID lookup
    // result directly. Unlike the ordinary party-member helpers, it does
    // not execute the IsPC/CastUser virtual gate before storing the pointer.
    return static_cast<User*>(CObject::GetObjectW(sid));
}

CCreature* ResolveTacticalCreature(std::uint32_t sid) {
    // The current body returns CSmartID::GetObjectW directly as CCreature*.
    // it does not add an IsCreature/CastCreature gate here.
    return static_cast<CCreature*>(CObject::GetObjectW(sid));
}

void SendPartySummonStatus(User* recipient, CSummon* summon) {
    if (!summon) {
        return;
    }
    GuardHelper guard(L"in_code", CThreadManager::GetThreadID());
    CUserSocket* const socket = recipient->GetSocket();
    const CSharedCreatureData* const data = summon->m_data;
    const std::int32_t maxMp = runtime::Win64CvttDoubleToInt32(data->m_dBaseMP);
    const std::int32_t mp = runtime::Win64CvttDoubleToInt32(data->m_dMP);
    const std::int32_t maxHp = runtime::Win64CvttDoubleToInt32(data->m_dBaseHP);
    const std::int32_t hp = runtime::Win64CvttDoubleToInt32(data->m_dHP);
    const std::uint32_t summonId = summon->m_nId;
    socket->Send("cddcccdcdcdcd", 0x18, summonId, 0, 0, 4, 9, hp, 10, maxHp, 0x0b, mp, 0x0c, maxMp);
}

} // namespace

CPartySmartIdManager g_PartySmartIdManager;
CPartyObjectMap g_PartyMap;

CPartySmartIdManager* PartySmartIdManager() noexcept { return &g_PartySmartIdManager; }

CParty::CParty(ItemRoutingType routingType) :
    m_Dismissed(0),
    m_nTimerCount(0),
    m_LevelGap(0),
    m_nLevSum(0),
    m_nMaxLev(0),
    m_nMinLev(0x78),
    m_aOrderedMember{},
    m_nNextItemWinnerIndex(0),
    m_nMemberCount(0),
    m_ItemRoutingType(routingType),
    m_IsLootingTypeModifyInProgress(false),
    padding_cd{},
    m_LootingChangeTimeout(0),
    m_ReservedItemLootingType(static_cast<ItemRoutingType>(0)) {
    L2_GUARD_FUNCTION();

    m_data = CPartyFactory::Inst().Add(this);
    const std::int32_t sharedIndex = m_data->index;

    m_nId = g_PartySmartIdManager.RegisterObject(
        // 13,
        SCLS_PARTY, static_cast<std::uint32_t>(sharedIndex), this
    );
    CPartyFactory::Inst().PublishIndexId(sharedIndex, m_nId);

    m_data->m_nId = static_cast<std::int32_t>(m_nId);
    m_data->m_nOnlineMasterDBID = 0;
    m_data->m_nOnlineMemberCount = 0;
    m_data->m_OnlineMaster.id = 0;
    m_data->m_OnlineMaster.index = UINT32_MAX;
    std::memset(m_data->m_OnlineMember, 0, sizeof(m_data->m_OnlineMember));
    std::memset(m_data->m_TaticalSign, 0, sizeof(m_data->m_TaticalSign));
    m_data->m_nMPCCId = 0;
    m_data->m_bPVPMatchStart = false;
    m_data->m_nPVPMatchTeamIndex = 0;
    m_data->m_bIsActive = true;

    AddTimer(2500U, 1);
    runtime::raw_atomic::ExchangeI32(&m_Divide, 0);
    Stat::inst().IncrementTotalPartyCount();
}

CParty::~CParty() {
    L2_GUARD_FUNCTION();
    CPartyFactory::Inst().Remove(this);
}

L2_DEFINE_OBJECT_CLASS_ID(CParty)

L2_DEFINE_OBJECT_CLASS_NAME(CParty, m_pClassName)

L2_DEFINE_OBJECT_SINGLE_ALLOCATOR(CParty, __GetClassName())

L2_DEFINE_OBJECT_TYPED_RELEASE(CParty)

int CParty::GetIndex() const { return m_data->index; }

FVector CParty::GetPos() const { return {}; }

CObject* CParty::Copy() { return nullptr; }

ItemRoutingType CParty::ReservedItemLootingType() {
    L2_GUARD_FUNCTION();
    return m_ReservedItemLootingType;
}

bool CParty::IsLootingTypeModifyInProgress() {
    L2_GUARD_FUNCTION();
    return m_IsLootingTypeModifyInProgress;
}

bool CParty::RouteItem(CItem* item, User* user) {
    L2_GUARD_FUNCTION();
    if (!item) {
        return false;
    }
    if (!user) {
        return false;
    }

    CWorld::Inst().DoAsyncAddItemToInventory(item, user);
    item->LogRelateItem(*user, ReadItemAmount(item));
    return true;
}

void CParty::SetMaster(User* user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        m_data->m_nOnlineMasterDBID = 0;
        m_data->m_OnlineMaster.id = 0;
        m_data->m_OnlineMaster.index = UINT32_MAX;
        return;
    }

    m_data->m_nOnlineMasterDBID = user->GetDBID();
    m_data->m_OnlineMaster.id = ReadObjectSid(user);
    m_data->m_OnlineMaster.index = static_cast<std::uint32_t>(user->GetIndex());
}

void CParty::SetRoutingType(ItemRoutingType routingType) {
    L2_GUARD_FUNCTION();
    m_ItemRoutingType = routingType;
}

ItemRoutingType CParty::GetRoutingType() const {
    L2_GUARD_FUNCTION();
    return m_ItemRoutingType;
}

bool CParty::InsertMemberSid(std::uint32_t sid, bool rejectDuplicate) {
    L2_GUARD_FUNCTION();
    if (rejectDuplicate && m_nMemberCount > 0) {
        bool duplicate = false;
        for (int index = 0; index < m_nMemberCount; ++index) {
            if (m_aOrderedMember[index] == sid) {
                duplicate = true;
            }
        }
        if (duplicate) {
            return false;
        }
    }

    // The current body has no explicit seven-member bounds gate.
    m_aOrderedMember[m_nMemberCount] = sid;
    ++m_nMemberCount;
    return true;
}

void CParty::DeleteMemberSid(std::uint32_t sid) {
    L2_GUARD_FUNCTION();
    std::uint32_t retained[7]{};
    int retainedCount = 0;
    int removedIndex = -1;
    for (int index = 0; index < m_nMemberCount; ++index) {
        if (m_aOrderedMember[index] == sid) {
            removedIndex = index;
        } else {
            retained[retainedCount++] = m_aOrderedMember[index];
        }
    }
    if (removedIndex == -1) {
        return;
    }

    std::memcpy(m_aOrderedMember, retained, sizeof(retained));
    m_nMemberCount = retainedCount;
    if (removedIndex < m_nNextItemWinnerIndex) {
        --m_nNextItemWinnerIndex;
    }
}

int CParty::GetTacticalSignId(CCreature& creature) {
    L2_GUARD_FUNCTION();
    const std::uint32_t sid = ReadObjectSid(&creature);
    for (int index = 0; index < 4; ++index) {
        if (static_cast<std::uint32_t>(m_data->m_TaticalSign[index]) == sid) {
            return index + 1;
        }
    }
    // return 0.
    return TacticalSign::NOT_SIGNED;
}

bool CParty::RouteAdena(CItem* item, User* user, std::vector<User*>& members) {
    L2_GUARD_FUNCTION();
    static_cast<void>(user);

    const int memberCount = static_cast<int>(members.size());
    if (memberCount < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Error. MemberCount is zero.",
        //     L"Party.cpp",
        //     0x725)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Error. MemberCount is zero.",
        //     L"Party.cpp",
        //     0x725)
        L2_LOG_ERROR(
            Log,
            "Cannot distribute party adena: the converted member count is %d (list size %llu); no distribution is "
            "attempted.",
            memberCount, static_cast<unsigned long long>(members.size())
        );
        return false;
    }

    std::int64_t remainderByMember[7]{};
    const std::int64_t totalAmount = ReadItemAmount(item);
    const std::int64_t each = totalAmount / memberCount;
    std::int64_t remainder = totalAmount - memberCount * each;
    if (each < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Error. nEach is zero.",
        //     L"Party.cpp",
        //     0x732)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Error. nEach is zero.",
        //     L"Party.cpp",
        //     0x732)
        L2_LOG_ERROR(
            Log, "Cannot distribute party adena: total %lld divided among %d members gives negative share %lld.",
            static_cast<long long>(totalAmount), memberCount, static_cast<long long>(each)
        );
        return false;
    }

    // Party membership is capped at seven by the surrounding current
    // invariant. The target uses a fixed seven-element stack array too.
    while (remainder != 0) {
        const int randomValue = ::LegacyCrtRand();
        ++remainderByMember[randomValue % memberCount];
        --remainder;
    }

    for (int index = 0; index < memberCount; ++index) {
        User* const member = runtime::CheckedVectorAt(members, static_cast<std::size_t>(index));
        if (!member) {
            continue;
        }
        const std::int64_t routedAmount = each + remainderByMember[index];
        if (routedAmount <= 0) {
            continue;
        }
        CWorld::Inst().DoAsyncAddItemToInventory(0x39, routedAmount, member);
        item->LogRelateItem(*member, routedAmount);
        if (ItemTracksAdena(item)) {
            member->m_UserStatData.AddUserStat2(UserStat::US_ADENA, routedAmount);
        }
    }
    return true;
}

bool CParty::CheckRouteAdenaCond(CItem* item, User* user, std::vector<User*>& members, bool rangeMode) {
    L2_GUARD_FUNCTION();
    static_cast<void>(user);

    if (!item || ReadItemClassId(item) != 0x39U) {
        return false;
    }

    CancelTradeAllMember();
    members.reserve(7);
    members.clear();

    std::vector<User*> allMembers;
    {
        L2_GUARD(*m_lock);
        for (int index = 0; index < m_nMemberCount; ++index) {
            if (User* const member = ResolveRawUser(m_aOrderedMember[index])) {
                allMembers.push_back(member);
            }
        }
    }

    const std::int64_t totalAmount = ReadItemAmount(item);
    for (User* const member : allMembers) {
        if (!member) {
            continue;
        }

        const std::uint32_t addabilityItemId = ReadItemInventoryId(item);
        if (!member->m_inv.CheckAddable(static_cast<int>(addabilityItemId), totalAmount, false)) {
            continue;
        }

        // The target reloads the shared-data item ID after CheckAddable.
        // keep that ordering because the call may observe or mutate the
        // item owner independently.
        if (ReadItemInventoryId(item) != 0x39U || !ItemWorldObject(item)->IsInRange(member, 1500, rangeMode) ||
            !member->CanDoAction_china()) {
            continue;
        }
        members.push_back(member);
    }

    if (members.empty()) {
        return false;
    }
    return totalAmount / static_cast<std::int64_t>(members.size()) >= 0;
}

User* CParty::GetMaster() {
    L2_GUARD_FUNCTION();
    const std::uint32_t sid = m_data->m_OnlineMaster.id;
    if (sid == 0) {
        return nullptr;
    }

    User* const user = ResolveUser(sid);
    if (!user) {
        m_data->m_OnlineMaster.id = 0;
        m_data->m_OnlineMaster.index = UINT32_MAX;
    }
    return user;
}

void CParty::CancelTradeAllMember() {
    L2_GUARD_FUNCTION();
    for (int index = 0; index < m_nMemberCount; ++index) {
        User* const user = ResolveUser(m_aOrderedMember[index]);
        if (user && user->IsNowTrade()) {
            user->TradeCancel(false);
        }
    }
}

User* CParty::GetItemWinnerOrderly(User* referenceUser, std::uint32_t itemId, int amount) {
    L2_GUARD_FUNCTION();
    for (int attempt = 0; attempt < m_nMemberCount; ++attempt) {
        int candidateIndex;
        User* const candidate = [&] {
            L2_GUARD(*m_lock);
            candidateIndex = (m_nNextItemWinnerIndex + attempt) % m_nMemberCount;
            return ResolveUser(m_aOrderedMember[candidateIndex]);
        }();
        if (!candidate) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] [User *pMember] is nullptr",
            //     kPartySourceFile,
            //     0xc14)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] [User *pMember] is nullptr",
            //     kPartySourceFile,
            //     0xc14)
            L2_LOG_WARN(
                Log, "Party item-routing candidate at index %d is unavailable for item %u; scanning continues.",
                candidateIndex, static_cast<unsigned>(itemId)
            );
            continue;
        }
        if (!candidate->CanDoAction_china() || !candidate->IsAlive() || !candidate->IsInRange(referenceUser, 1500.0)) {
            continue;
        }
        if (candidate->IsNowTrade()) {
            candidate->TradeCancel(false);
        }
        if (!candidate->m_inv.CheckAddable(static_cast<int>(itemId), static_cast<std::int64_t>(amount), false)) {
            continue;
        }
        {
            L2_GUARD(*m_lock);
            m_nNextItemWinnerIndex = (candidateIndex + 1) % m_nMemberCount;
        }
        return candidate;
    }
    return nullptr;
}

User* CParty::GetItemWinnerRandomly(User* referenceUser, std::uint32_t itemId, int amount) {
    L2_GUARD_FUNCTION();
    std::vector<User*> users;
    GetAllMember(users);
    std::vector<User*> candidates;
    for (User* const candidate : users) {
        if (!candidate || !candidate->IsAlive() || !candidate->IsInRange(referenceUser, 1500.0)) {
            continue;
        }
        if (candidate->IsNowTrade()) {
            candidate->TradeCancel(false);
        }
        if (candidate->CanDoAction_china() &&
            candidate->m_inv.CheckAddable(static_cast<int>(itemId), static_cast<std::int64_t>(amount), false)) {
            candidates.push_back(candidate);
        }
    }
    if (candidates.empty()) {
        return nullptr;
    }
    const std::uint32_t randomValue = static_cast<std::uint32_t>(::LegacyCrtRand());
    return runtime::CheckedVectorAt(candidates, randomValue % static_cast<std::uint32_t>(candidates.size()));
}

CMultiPartyCommandChannel* CParty::GetMPCC() {
    L2_GUARD_FUNCTION();
    CSPointer<CMultiPartyCommandChannel> channel = CMultiPartyCommandChannelRegistry::FindByMemberParty(this);
    return channel.get();
}

User* CParty::GetMember(std::uint32_t sid) {
    L2_GUARD_FUNCTION();
    bool present = false;
    {
        L2_GUARD(*m_lock);
        for (int index = 0; index < m_nMemberCount; ++index) {
            if (m_aOrderedMember[index] == sid) {
                present = true;
                break;
            }
        }
    }
    return present ? ResolveUser(sid) : nullptr;
}

CCreature* CParty::GetTacticalSignedCreature(int signId) {
    L2_GUARD_FUNCTION();
    // if (static_cast<unsigned int>(signId - 1) >= 4U) {
    if (static_cast<unsigned int>(signId - 1) >= static_cast<unsigned>(TacticalSign::MAX_COUNT)) {
        return nullptr;
    }
    const std::uint32_t sid = static_cast<std::uint32_t>(m_data->m_TaticalSign[signId - 1]);
    // An unused sign is 0, which CObject::GetObjectW answers with nullptr after
    // logging a call stack; every user info of a party member asks for all four.
    if (sid == 0) {
        return nullptr;
    }
    return ResolveTacticalCreature(sid);
}

void CParty::SendTacticalSignOnJoinParty(User& user) {
    // Current L2Server.exe 0x0092e810. The matching-build MAP keeps the
    // original owner/name and current instructions keep the 1..4 order.
    // for (int signId = 1. SignId <= 4. ++signId) {
    for (int signId = 1; signId <= TacticalSign::MAX_COUNT; ++signId) {
        CCreature* const creature = GetTacticalSignedCreature(signId);
        if (creature != nullptr) {
            creature->SendTacticalSignInfo(user, signId);
        }
    }
}

void CParty::SendTacticalSignOnLeaveParty(User& user) {
    // Current L2Server.exe 0x0092e86c. This remains a distinct physical
    // owner and sends sign ID zero for every currently signed creature.
    // for (int signId = 1. SignId <= 4. ++signId) {
    for (int signId = 1; signId <= TacticalSign::MAX_COUNT; ++signId) {
        CCreature* const creature = GetTacticalSignedCreature(signId);
        if (creature != nullptr) {
            // creature->SendTacticalSignInfo(user, 0)
            creature->SendTacticalSignInfo(user, TacticalSign::NOT_SIGNED);
        }
    }
}

bool CParty::IsMember(std::uint32_t sid) {
    L2_GUARD_FUNCTION();
    return GetMember(sid) != nullptr;
}

void CParty::GetAllMemberWhoCanGetExpSp(std::vector<User*>& users, CCreature* creature) {
    L2_GUARD_FUNCTION();
    users.reserve(7);
    users.clear();
    {
        L2_GUARD(*m_lock);
        for (int index = 0; index < m_nMemberCount; ++index) {
            User* const user = ResolveUser(m_aOrderedMember[index]);
            if (user && user->IsInRange(creature, 1500.0)) {
                users.push_back(user);
            }
        }
    }
}

void CParty::GetAllMember(std::vector<User*>& users) {
    L2_GUARD_FUNCTION();
    users.reserve(7);
    users.clear();
    L2_GUARD(*m_lock);
    for (int index = 0; index < m_nMemberCount; ++index) {
        if (User* const user = ResolveUser(m_aOrderedMember[index])) {
            users.push_back(user);
        }
    }
}

std::u16string CParty::GetAllMemberName() {
    L2_GUARD_FUNCTION();
    std::vector<User*> users;
    GetAllMember(users);

    std::u16string joinedNames;
    for (User* const user : users) {
        joinedNames += std::u16string(user->GetName() != nullptr ? user->GetName() : u"");
        joinedNames += u'/';
    }
    if (!joinedNames.empty()) {
        // Current helper appends the delimiter after every entry and erases
        // exactly its final UTF-16 code unit. DAT_00f0fab8 is u"/".
        joinedNames.erase(joinedNames.size() - 1, 1);
    }
    return std::u16string(std::u16string_view(joinedNames));
}

int CParty::GetMinLevelOfAllMember(bool checkDistance, int radius, FVector position, bool checkFilter, int filterArgument) {
    L2_GUARD_FUNCTION();
    int minimumLevel = 120;
    std::vector<User*> users;
    GetAllMember(users);
    for (User* const user : users) {
        const int candidateLevel = user->Level();
        if (candidateLevel >= minimumLevel) {
            continue;
        }
        if (checkFilter && !user->IsInInZone(static_cast<unsigned int>(filterArgument))) {
            continue;
        }
        if (checkDistance) {
            const FVector userPosition = user->GetPos();
            const double distanceSquared = (userPosition.x - position.x) * (userPosition.x - position.x) +
                (userPosition.y - position.y) * (userPosition.y - position.y) +
                (userPosition.z - position.z) * (userPosition.z - position.z);
            const double radiusSquared = static_cast<double>(runtime::MultiplyInt32Wrapping(radius, radius));
            if (radiusSquared <= distanceSquared) {
                continue;
            }
        }
        // The current body performs the virtual level query again here.
        minimumLevel = user->Level();
    }
    return minimumLevel;
}

void CParty::GetAllMemberId(std::vector<std::uint32_t>& sids) {
    L2_GUARD_FUNCTION();
    sids.clear();
    {
        L2_GUARD(*m_lock);
        for (int index = 0; index < m_nMemberCount; ++index) {
            sids.push_back(m_aOrderedMember[index]);
        }
    }
}

void CParty::OnSetTacticalSign(CCreature& creature, int signId) {
    L2_GUARD_FUNCTION();
    std::vector<User*> users;
    GetAllMember(users);
    for (User* const user : users) {
        creature.SendTacticalSignInfo(*user, signId);
    }
}

void CParty::OnJoinParty(User& user) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x009306cc.
    std::vector<User*> users;
    GetAllMember(users);

    CContainerIndex neighbors;
    CContainer* const container = CContainerDB::Inst().GetContainer(user.GetContainerIndex());
    if (container != nullptr) {
        neighbors = CContainerDB::Inst().GatherNeighborContainerIndex(container);
    }

    CContainerFilterNull filter;
    struct ContainerFunc final : CContainerFunc {
        void Do(CObject* object) override {
            // The deleting/deleted gate applies only to PC objects.
            if (object->IsPC() && (object->IsDeleting() || object->m_Deleted != 0)) {
                return;
            }
            CCreature* const creature = object->IsCreature() ? object->CastCreature() : nullptr;
            if (creature == nullptr || creature->m_nId == user->m_nId || creature->GetParty() != party) {
                return;
            }
            if (!creature->IsVisible()) {
                user->InSight(creature, true);
            }
            if (!user->IsVisible()) {
                creature->InSight(user, true);
            }
        }

        User* user;
        CParty* party;
    } function;
    function.user = &user;
    function.party = this;
    FOR_ALL_OBJECT_IN_CONTAINER_INDEX(neighbors, filter, function);
    SendTacticalSignOnJoinParty(user);
}

void CParty::OnLeaveParty(User& user) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00930880.
    std::vector<User*> users;
    GetAllMember(users);

    CContainerIndex neighbors;
    CContainer* const container = CContainerDB::Inst().GetContainer(user.GetContainerIndex());
    if (container != nullptr) {
        neighbors = CContainerDB::Inst().GatherNeighborContainerIndex(container);
    }

    CContainerFilterNull filter;
    struct ContainerFunc final : CContainerFunc {
        void Do(CObject* object) override {
            // The deleting/deleted gate applies only to PC objects.
            if (object->IsPC() && (object->IsDeleting() || object->m_Deleted != 0)) {
                return;
            }
            CCreature* const creature = object->IsCreature() ? object->CastCreature() : nullptr;
            if (creature == nullptr || creature->m_nId == user->m_nId || creature->GetParty() != party) {
                return;
            }
            if (!creature->IsVisible()) {
                user->SendOutOfSight(creature, false);
            }
            if (!user->IsVisible()) {
                creature->OutOfSight(user, false);
            }
        }

        User* user;
        CParty* party;
    } function;
    function.user = &user;
    function.party = this;
    FOR_ALL_OBJECT_IN_CONTAINER_INDEX(neighbors, filter, function);
    SendTacticalSignOnLeaveParty(user);
}

void CParty::BroadcastOnSetTacticalSign(CCreature& setter, CCreature& creature, int signId) {
    L2_GUARD_FUNCTION();
    std::vector<User*> users;
    GetAllMember(users);
    CObject* const creatureObject = &creature;
    for (User* const user : users) {
        if (creatureObject->IsPC()) {
            user->GetSocket()->Send(
                "chccScSch", 0x62, SystemMessageId::CHARACTER_USED_NAMED_ACTION_ON_CHARACTER, 3, 0x0c, setter.GetName(),
                0x0c, creature.GetName(), 0x0d, static_cast<int>(kTacticalSignClientIndex[signId - 1])
            );
        } else if (creatureObject->IsNPC()) {
            user->GetSocket()->Send(
                "chccScdch", 0x62, SystemMessageId::CHARACTER_USED_NAMED_ACTION_ON_CHARACTER, 3, 0x0c, setter.GetName(), 2,
                creature.GetClassId(), 0x0d, static_cast<int>(kTacticalSignClientIndex[signId - 1])
            );
        }
    }
}

void CParty::OnResetTacticalSign(CCreature& creature) {
    L2_GUARD_FUNCTION();
    std::vector<User*> users;
    GetAllMember(users);
    for (User* const user : users) {
        // creature.SendTacticalSignInfo(*user, 0)
        creature.SendTacticalSignInfo(*user, TacticalSign::NOT_SIGNED);
    }
}

bool CParty::SetTacticalSign(CCreature& setter, CCreature& creature, int signId) {
    L2_GUARD_FUNCTION();
    // if (static_cast<unsigned int>(signId - 1) >= 4U) {
    if (static_cast<unsigned int>(signId - 1) >= static_cast<unsigned>(TacticalSign::MAX_COUNT)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"TACTICAL_SIGN_SERVER_INDEX index is invalid %d",
        //     signId - 1)
        // L2_LOG_ERROR(Log,
        //     L"TACTICAL_SIGN_SERVER_INDEX index is invalid %d",
        //     signId - 1)
        L2_LOG_WARN(Log, "Cannot assign tactical sign %d: valid IDs are 1 through 4.", signId);
        return false;
    }

    const int selectedIndex = signId - 1;
    const std::uint32_t creatureSid = ReadObjectSid(&creature);
    const std::uint32_t displacedSid = static_cast<std::uint32_t>(m_data->m_TaticalSign[selectedIndex]);
    if (displacedSid == creatureSid) {
        OnResetTacticalSign(static_cast<int>(displacedSid));
        m_data->m_TaticalSign[selectedIndex] = 0;
        return true;
    }

    OnResetTacticalSign(static_cast<int>(displacedSid));
    for (int index = 0; index < 4; ++index) {
        if (static_cast<std::uint32_t>(m_data->m_TaticalSign[index]) == creatureSid) {
            m_data->m_TaticalSign[index] = 0;
        }
    }
    m_data->m_TaticalSign[selectedIndex] = runtime::Int32FromBits(creatureSid);
    creature.SetTacticalSignToMe(static_cast<int>(m_nId));
    OnSetTacticalSign(creature, signId);
    BroadcastOnSetTacticalSign(setter, creature, signId);
    return true;
}

bool CParty::CheckAcceptStatus() {
    L2_GUARD_FUNCTION();
    const bool accepted = [&] {
        L2_WRITE_GUARD(m_PartyLootingLock);
        bool accepted = true;
        for (const auto& status : m_LootingTypeModifyStatus) {
            if (!status.second) {
                accepted = false;
                break;
            }
        }
        return accepted;
    }();
    return accepted;
}

void CParty::CompleteModifyLootingType() {
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(m_PartyLootingLock);
        m_IsLootingTypeModifyInProgress = false;
        const std::uint64_t clearedTail = 0;
        std::memcpy(&m_LootingChangeTimeout, &clearedTail, sizeof(clearedTail));
        m_LootingTypeModifyStatus.clear();
    }
}

void CParty::AcceptModifyLootingType(User* user) {
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(m_PartyLootingLock);
        if (user) {
            m_LootingTypeModifyStatus[user->GetDBID()] = true;
        }
    }
}

bool CParty::StartModifyLootingType(ItemRoutingType routingType) {
    L2_GUARD_FUNCTION();
    m_IsLootingTypeModifyInProgress = true;
    m_ReservedItemLootingType = routingType;

    std::vector<User*> users;
    GetAllMember(users);
    for (User* const user : users) {
        if (GetMaster() != user) {
            m_LootingTypeModifyStatus[user->GetDBID()] = false;
        }
    }
    AddTimer(1000U, 2);
    return true;
}

void CParty::MemberPositionPacket() {
    L2_GUARD_FUNCTION();
    std::vector<User*> members;
    GetAllMember(members);

    std::array<char, 20480> packet;
    int packetSize = 0;
    int memberCount = 0;
    for (User* const member : members) {
        const FVector position = member->GetPos();
        packetSize = runtime::AddInt32Wrapping(
            packetSize,
            ::Assemble(
                packet.data() + packetSize, 0x5000 - packetSize, "dddd", member->GetServerId(),
                runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
                runtime::Win64CvttDoubleToInt32(position.z)
            )
        );
        memberCount = runtime::AddInt32Wrapping(memberCount, 1);
    }
    if (memberCount <= 0) {
        return;
    }
    for (User* const member : members) {
        member->GetSocket()->Send("cdb", 0xba, memberCount, packetSize, packet.data());
    }
}

void CParty::DivideAdenaStart() {
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::ExchangeI32(&m_Divide, 1);

    std::vector<User*> users;
    GetAllMember(users);
    for (User* const user : users) {
        if (user->m_nId == m_data->m_OnlineMaster.id) {
            user->GetSocket()->Send("ch", 0xfe, 0x15b);
        }
        user->GetSocket()->SendSystemMessage(SystemMessageId::ADENA_DISTRIBUTION_STARTED);
    }
}

void CParty::SendCancelPacket(int messageId, DivideType::Enum divideType) {
    L2_GUARD_FUNCTION();
    const std::int32_t previous = runtime::raw_atomic::ExchangeI32(&m_Divide, 0);
    if (previous == 0) {
        return;
    }

    User* const master = GetMaster();
    if (!master) {
        return;
    }
    std::vector<User*> users;
    GetAllMember(users);
    for (User* const user : users) {
        if (user->m_nId == master->m_nId) {
            master->GetSocket()->Send("chc", 0xfe, 0x15c, static_cast<int>(divideType));
        }
        user->SendSystemMessage(messageId);
    }
}

void CParty::DivideAdenaCancel(DivideType::Enum divideType) {
    L2_GUARD_FUNCTION();
    SendCancelPacket(0x1037, divideType);
}

void CParty::DivideAdena(std::int64_t totalAmount) {
    // Current L2Server 0x00934098.
    L2_GUARD_FUNCTION();

    const std::int32_t memberCount = m_nMemberCount;
    User* const master = GetMaster();
    if (!master) {
        return;
    }

    CancelTradeAllMember();
    const std::u16string masterName = std::u16string(master->GetName());
    const std::int64_t amountPerMember = totalAmount / memberCount;
    if (amountPerMember < 1) {
        SendCancelPacket(0x103d, static_cast<DivideType::Enum>(3));
        return;
    }

    std::vector<User*> members;
    GetAllMember(members);
    constexpr std::int64_t kMaximumAdena = 999999999999LL;
    for (User* const member : members) {
        if (member->m_nId != master->m_nId &&
            runtime::AddInt64Wrapping(member->m_inv.GetAdenaAmount(), amountPerMember) > kMaximumAdena) {
            SendCancelPacket(0x104b, static_cast<DivideType::Enum>(3));
            master->GetSocket()->SendSystemMessage_S(
                SystemMessageId::CANNOT_DISTRIBUTE_ADENA_RECIPIENT_LIMIT_EXCEEDED, member->GetName()
            );
            return;
        }
    }

    runtime::raw_atomic::ExchangeI32(&m_Divide, 0);
    const std::int32_t previousMasterDivide = runtime::raw_atomic::ExchangeI32(&master->m_nWaitGetItemOnce, 1);
    if (previousMasterDivide == 0) {
        for (User* const member : members) {
            if (member->m_nId == master->m_nId) {
                continue;
            }
            if (!master->TradeDivideAdena(member, memberCount, amountPerMember, false)) {
                member->SendSystemMessage(SystemMessageId::ADENA_DISTRIBUTION_NOT_RECEIVED);
                master->GetSocket()->SendSystemMessage_S(
                    SystemMessageId::ADENA_NOT_DISTRIBUTED_TO_CHARACTER, member->GetName()
                );
            }
        }
    } else {
        master->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
    }

    master->GetSocket()->Send("chccdQQS", 0xfe, 0x15d, 1, 0, memberCount, amountPerMember, totalAmount, masterName.c_str());
    CLogFormatter formatter;
    formatter.WriteLog1(
        0x233a, master, nullptr, nullptr, nullptr, nullptr, 1, totalAmount, memberCount, amountPerMember, 0, 0, 0
    );
}

void CParty::AnnounceSweepItem(User* user, std::uint32_t itemId, int amount) {
    L2_GUARD_FUNCTION();
    if (itemId == 0x39U) {
        return;
    }

    std::vector<User*> users;
    GetAllMember(users);
    for (User* const recipient : users) {
        if (recipient != user && recipient && recipient->IsInRange(user, 1500, false)) {
            if (amount < 2) {
                recipient->GetSocket()->Send(
                    "chccScd", 0x62, SystemMessageId::CHARACTER_SWEEP_OBTAINED_ITEM, 2, 0x0c, user->GetName(), 3, itemId
                );
            } else {
                recipient->GetSocket()->Send(
                    "chccScdcd", 0x62, SystemMessageId::CHARACTER_SWEEP_OBTAINED_MULTIPLE_ITEMS, 3, 0x0c, user->GetName(), 3,
                    itemId, 1, amount
                );
            }
        }
    }
}

bool CParty::SweepItem(int itemId, int amount, User* user) {
    L2_GUARD_FUNCTION();

    int routingType = static_cast<int>(m_ItemRoutingType);
    CItem* const item = reinterpret_cast<CItem*>(g_ObjectDB.GetObjectW(itemId, false));
    if (!item || !ItemObject(item)->IsItem()) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] Item[%d] is not exist.",
        //     "Party.cpp",
        //     0x882,
        //     itemId)
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] Item[%d] is not exist.",
        //     "Party.cpp",
        //     0x882,
        //     itemId)
        L2_LOG_ERROR(
            Log, "Cannot distribute swept item %d (count %d): its prototype is missing or is not an item.", itemId, amount
        );
        return false;
    }

    if (ItemForcesIndividualRouting(item)) {
        routingType = 0;
    }

    User* winner = nullptr;
    if (routingType > 0) {
        if (routingType < 3) {
            winner = GetItemWinnerRandomly(user, static_cast<std::uint32_t>(itemId), amount);
        } else if (routingType <= 4) {
            winner = GetItemWinnerOrderly(user, static_cast<std::uint32_t>(itemId), amount);
        }
        if (winner) {
            const ItemOptionKey optionKey{};
            const BaseAttribute attribute;
            if (!winner->AddItemToInventory(
                    itemId, static_cast<std::int64_t>(amount), ItemGetReason::Etc, 0, 0, 0, optionKey, 0, attribute
                )) {
                return false;
            }
            item->LogRelateItem(*winner, ReadItemAmount(item));
            user = winner;
            AnnounceSweepItem(user, static_cast<std::uint32_t>(itemId), amount);
            return true;
        }
    }

    if (!user->CanDoAction_china()) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_ACT_DUE_TO_FATIGUE);
        return false;
    }
    if (!user->m_inv.CheckAddable(itemId, static_cast<std::int64_t>(amount), false)) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::INVENTORY_SLOTS_FULL);
        return false;
    }
    const ItemOptionKey optionKey{};
    const BaseAttribute attribute;
    if (!user->AddItemToInventory(
            itemId, static_cast<std::int64_t>(amount), ItemGetReason::Etc, 0, 0, 0, optionKey, 0, attribute
        )) {
        return false;
    }
    item->LogRelateItem(*user, ReadItemAmount(item));
    AnnounceSweepItem(user, static_cast<std::uint32_t>(itemId), amount);
    return true;
}

bool CParty::AddMember(User* user) {
    // Current L2Server 0x0093475c.
    L2_GUARD_FUNCTION();
    if (!user) {
        return false;
    }

    const std::u16string targetUserName = std::u16string(user->GetName() != nullptr ? user->GetName() : u"");
    BroadcastSystemMsgToAllParty(SystemMessageId::CHARACTER_JOINED_PARTY, EST_CHAR_NAME, targetUserName.c_str());

    bool insertedOnlineMember = false;
    bool alreadyOnlineMember = false;
    {
        L2_GUARD(*m_lock);

        if (!g_bSendSummonHPMPInfo) {
            std::vector<User*> members;
            GetAllMember(members);
            std::vector<CSummon*> joiningSummons;
            user->GetAllSummon(joiningSummons);

            const std::uint32_t joiningSid = ReadObjectSid(user);
            for (User* const member : members) {
                if (ReadObjectSid(member) == joiningSid) {
                    continue;
                }

                std::vector<CSummon*> memberSummons;
                member->GetAllSummon(memberSummons);
                for (CSummon* const summon : joiningSummons) {
                    SendPartySummonStatus(member, summon);
                }
                for (CSummon* const summon : memberSummons) {
                    SendPartySummonStatus(user, summon);
                }
            }
        }

        const std::uint32_t sid = ReadObjectSid(user);
        const std::int32_t sharedIndex = user->m_data->index;
        if (!InsertMemberSid(sid, user->m_data->m_nBuilder != 1)) {
            return false;
        }

        if (!user->m_bPartyBuilder) {
            for (int index = 0; index < 7; ++index) {
                if (m_data->m_OnlineMember[index].id == sid) {
                    alreadyOnlineMember = true;
                }
            }
        }

        if (!alreadyOnlineMember) {
            for (int index = 0; index < 7; ++index) {
                IdIndexPair& member = m_data->m_OnlineMember[index];
                if (member.id == 0U) {
                    member.id = sid;
                    member.index = static_cast<std::uint32_t>(sharedIndex);
                    ++m_data->m_nOnlineMemberCount;
                    insertedOnlineMember = true;
                    break;
                }
            }
        }

        m_nLevSum += user->Level();
        const std::int32_t maximumCandidate = user->Level();
        if (m_nMaxLev < maximumCandidate) {
            m_nMaxLev = maximumCandidate;
        }
        m_LevelGap = m_nMaxLev - m_nMinLev;
        const std::int32_t minimumCandidate = user->Level();
        if (minimumCandidate < m_nMinLev) {
            m_nMinLev = minimumCandidate;
        }
        m_LevelGap = m_nMaxLev - m_nMinLev;

        if (CMultiPartyCommandChannel* const mpcc = GetMPCC()) {
            mpcc->SendCancelPacket(0x1037, static_cast<DivideType::Enum>(2));
        } else {
            SendCancelPacket(0x1037, static_cast<DivideType::Enum>(2));
        }
    }
    MemberPositionPacket();

    if (insertedOnlineMember) {
        Stat::inst().IncrementTotalPartyMemberCount();
        return true;
    }
    if (!alreadyOnlineMember) {
        const std::u16string targetOverflowName = std::u16string(user->GetName() != nullptr ? user->GetName() : u"");
        // Log.Add(
        //     LOG_ERROR,
        //     L"CParty[%d]::AddMember[%s]-> Overflow",
        //     m_nId,
        //     overflowName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"CParty[%d]::AddMember[%s]-> Overflow",
        //     m_nId,
        //     overflowName.c_str())
        L2_LOG_ERROR(
            Log,
            "Party %d has no free online-member slot for '%s'; earlier join notifications and member updates are retained.",
            m_nId, Utf16ToUtf8(targetOverflowName).c_str()
        );
    }
    return false;
}

bool CParty::DeleteMember(std::uint32_t sid) {
    L2_GUARD_FUNCTION();

    const bool deleted = [&] {
        L2_GUARD(*m_lock);
        DeleteMemberSid(sid);
        bool deleted = false;
        for (int index = 0; index < 7; ++index) {
            IdIndexPair& member = m_data->m_OnlineMember[index];
            if (member.id == sid) {
                member.id = 0;
                member.index = UINT32_MAX;
                --m_data->m_nOnlineMemberCount;
                deleted = true;
                break;
            }
        }
        return deleted;
    }();

    User* const member = GetMember(sid);
    {
        L2_GUARD(*m_lock);
        if (member) {
            m_nLevSum -= member->Level();
        }
    }
    ValidateLevel();

    if (!deleted) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CParty[%d]::DeleteMember[%d]-> Cannot find",
        //     m_nId,
        //     sid)
        // L2_LOG_ERROR(Log,
        //     L"CParty[%d]::DeleteMember[%d]-> Cannot find",
        //     m_nId,
        //     sid)
        L2_LOG_ERROR(
            Log,
            "Party %d cannot find online member object %u during removal; member-ID deletion and level recalculation "
            "already ran.",
            m_nId, static_cast<unsigned>(sid)
        );
        return false;
    }

    if (CMultiPartyCommandChannel* const mpcc = GetMPCC()) {
        mpcc->SendCancelPacket(0x1037, static_cast<DivideType::Enum>(2));
    } else {
        SendCancelPacket(0x1037, static_cast<DivideType::Enum>(2));
    }
    Stat::inst().DecrementTotalPartyMemberCount();
    return true;
}

void CParty::DismissImpl(bool retainNonMasterContext, bool writeDismissLogs) {
    L2_GUARD_FUNCTION();
    CMultiPartyCommandChannelManager commandChannelManager;
    commandChannelManager.OnDismissParty(this);
    CPVPMatchSystem::inst().OnDismissParty(this);
    m_data->m_bIsActive = false;

    User* const master = GetMaster();
    if (master) {
        const std::u16string memberNames = GetAllMemberName();
        CLogFormatter formatter;
        formatter.WriteLog1_1(
            0x333, master, nullptr, memberNames, nullptr, nullptr, 0, 0, 0, 0, 0,
            static_cast<std::int32_t>(GetRoutingType()), 0
        );
    }

    bool lootingChangeWasActive = false;
    if (m_IsLootingTypeModifyInProgress) {
        lootingChangeWasActive = true;
        CompleteModifyLootingType();
    }

    std::vector<User*> members;
    GetAllMember(members);
    if (!g_bSendSummonHPMPInfo) {
        int ownerIndex = 0;
        for (User* const owner : members) {
            if (!owner) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] [User *pMemberUser = lstMember] "
                //     L"is nullptr",
                //     L"Party.cpp",
                //     0x318,
                //     ownerIndex)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] [User *pMemberUser = lstMember] "
                //     L"is nullptr",
                //     L"Party.cpp",
                //     0x318,
                //     ownerIndex)
                L2_LOG_WARN(
                    Log,
                    "Party dismissal cannot process summons for member index %d: the user is unavailable; other members are "
                    "still processed.",
                    ownerIndex
                );
                ++ownerIndex;
                continue;
            }

            std::vector<CSummon*> summons;
            owner->GetAllSummon(summons);
            for (User* const recipient : members) {
                if (recipient->m_nId == owner->m_nId) {
                    continue;
                }
                for (CSummon* const summon : summons) {
                    if (!summon) {
                        continue;
                    }
                    GuardHelper summonEntry(L"in_code", CThreadManager::GetThreadID());
                    recipient->GetSocket()->Send("cddcccdcd", 0x18, summon->m_nId, 0, 0, 2, 10, 0, 0x0c, 0);
                }
            }
            ++ownerIndex;
        }
    }

    int memberIndex = 0;
    for (User* const member : members) {
        if (!member) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] [User *pMemberUser = lstMember] is nullptr",
            //     L"Party.cpp",
            //     0x33f,
            //     memberIndex)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] [User *pMemberUser = lstMember] is nullptr",
            //     L"Party.cpp",
            //     0x33f,
            //     memberIndex)
            L2_LOG_WARN(
                Log, "Party dismissal skips unavailable member index %d; cleanup continues for other members.", memberIndex
            );
            ++memberIndex;
            continue;
        }
        if (lootingChangeWasActive) {
            member->GetSocket()->Send("chdd", 0xfe, 0xc1, 0, 0);
            member->GetSocket()->SendSystemMessage(SystemMessageId::PARTY_LOOT_CHANGE_CANCELLED);
        }

        User* const nonMasterContext = retainNonMasterContext && member != master ? member : nullptr;
        member->SetParty(nullptr, nonMasterContext);
        member->m_data->m_nPartyId = 0;
        member->GetSocket()->Send("cd", 0x3f, 1);
        member->GetSocket()->Send("c", 0x50);
        g_PartyMatchingWaitingRoom.SetInvitable(member);

        if (writeDismissLogs) {
            const std::u16string memberNames = GetAllMemberName();
            CLogFormatter formatter;
            formatter.WriteLog1_1(
                0x334, member, master, memberNames, nullptr, nullptr, 0, 0, 0, 0,
                static_cast<std::int64_t>(writeDismissLogs), static_cast<std::int32_t>(GetRoutingType()), 0
            );
        }
        Stat::inst().DecrementTotalPartyMemberCount();
        ++memberIndex;
    }
    Stat::inst().DecrementTotalPartyCount();

    const std::uint32_t sid = m_nId;
    g_PartyMap.DeleteObject(static_cast<int>(sid), const_cast<char*>(kPartySourceFileNarrow), 0x366);
    g_PartySmartIdManager.DeleteObject(sid);
}

bool CParty::DismissByWithdraw(User* user, bool retainNonMasterContext) {
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::CompareExchangeI32(&m_Dismissed, 1, 0) != 0) {
        return false;
    }

    if (User* const master = GetMaster()) {
        if (runtime::raw_atomic::ExchangeI32(&m_Divide, 0) != 0) {
            master->GetSocket()->Send("chc", 0xfe, 0x15c, 5);
        }
    }
    CPartyMatchingRequest::inst().OnDismissParty(this);
    CSPointer<User> userKeepAlive(user, ObjectRefType::FunctionChain);
    CPartyMatchingRequest::inst().OnWithdrawParty(userKeepAlive);
    DismissImpl(retainNonMasterContext, false);
    return true;
}

bool CParty::Dismiss(bool retainNonMasterContext, bool writeDismissLogs) {
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::CompareExchangeI32(&m_Dismissed, 1, 0) != 0) {
        return false;
    }

    User* const master = GetMaster();
    CSPointer<User> masterKeepAlive(master, ObjectRefType::FunctionChain);
    CPartyMatchingRequest::inst().DismissRoom(masterKeepAlive);
    DismissImpl(retainNonMasterContext, writeDismissLogs);
    return true;
}

void CParty::AddPetWindowPacket(CSummon* summon) {
    // Current L2Server.exe 0x009319f8.
    L2_GUARD_FUNCTION();
    if (!summon) {
        return;
    }

    const std::uint32_t summonSid = summon->m_nId;
    const std::int32_t classId = summon->GetClassId();
    const std::int8_t summonType = static_cast<std::int8_t>(summon->m_data->m_SummonType);
    User* const master = summon->GetMaster();
    const std::uint32_t masterSid = master ? master->m_nId : 0;
    const std::int32_t hp = runtime::Win64CvttDoubleToInt32(summon->m_data->m_dHP);
    const std::int32_t maximumHp = runtime::Win64CvttDoubleToInt32(summon->m_data->m_dBaseHP);
    const std::int32_t mp = runtime::Win64CvttDoubleToInt32(summon->m_data->m_dMP);
    const std::int32_t maximumMp = runtime::Win64CvttDoubleToInt32(summon->m_data->m_dBaseMP);

    std::vector<User*> members;
    GetAllMember(members);
    for (User* const member : members) {
        if (member && member->m_nId != masterSid) {
            member->GetSocket()->Send(
                "chddcddddd", 0xfe, 0x18, summonSid, classId, summonType, masterSid, hp, maximumHp, mp, maximumMp
            );
        }
    }
}

void CParty::UpdatePetWindowPacket(CSummon* summon) {
    // Current L2Server.exe 0x00931c00.
    L2_GUARD_FUNCTION();
    if (!summon) {
        return;
    }

    const std::uint32_t summonSid = summon->m_nId;
    const std::int32_t classId = summon->GetClassId();
    const std::int8_t summonType = static_cast<std::int8_t>(summon->m_data->m_SummonType);
    User* const master = summon->GetMaster();
    const std::uint32_t masterSid = master ? master->m_nId : 0;
    const std::int32_t hp = runtime::Win64CvttDoubleToInt32(summon->m_data->m_dHP);
    const std::int32_t maximumHp = runtime::Win64CvttDoubleToInt32(summon->m_data->m_dBaseHP);
    const std::int32_t mp = runtime::Win64CvttDoubleToInt32(summon->m_data->m_dMP);
    const std::int32_t maximumMp = runtime::Win64CvttDoubleToInt32(summon->m_data->m_dBaseMP);

    std::vector<User*> members;
    GetAllMember(members);
    for (User* const member : members) {
        if (member && member->m_nId != masterSid) {
            member->GetSocket()->Send(
                "chddcddddd", 0xfe, 0x19, summonSid, classId, summonType, masterSid, hp, maximumHp, mp, maximumMp
            );
        }
    }
}

void CParty::ClosePetWindowPacket(CSummon* summon) {
    // Current L2Server.exe 0x00931e08 reads summon type for every send.
    L2_GUARD_FUNCTION();
    if (!summon) {
        return;
    }

    const std::uint32_t summonSid = summon->m_nId;
    User* const master = summon->GetMaster();
    const std::uint32_t masterSid = master ? master->m_nId : 0;

    std::vector<User*> members;
    GetAllMember(members);
    for (User* const member : members) {
        if (member && member->m_nId != masterSid) {
            member->GetSocket()->Send(
                "chdcd", 0xfe, 0x6b, summonSid, static_cast<std::int8_t>(summon->m_data->m_SummonType), masterSid
            );
        }
    }
}

void CParty::OpenAllWindowPacket(User* recipient) {
    L2_GUARD_FUNCTION();

    std::vector<User*> members;
    GetAllMember(members);

    std::array<char, 0x5000> memberPacket;
    int memberPacketLength = 0;
    std::int8_t memberCount = 0;
    for (User* const member : members) {
        if (!recipient->m_bPartyBuilder && member->m_nId == recipient->m_nId) {
            continue;
        }

        CSharedCreatureData* const data = member->m_data;
        const int memberLength = ::Assemble(
            memberPacket.data() + memberPacketLength, 0x5000 - memberPacketLength, "dSdddddddchch", member->m_nId,
            member->GetName(), runtime::Win64CvttDoubleToInt32(data->m_dCP),
            runtime::Win64CvttDoubleToInt32(data->m_dBaseCP), runtime::Win64CvttDoubleToInt32(data->m_dHP),
            runtime::Win64CvttDoubleToInt32(data->m_dBaseHP), runtime::Win64CvttDoubleToInt32(data->m_dMP),
            runtime::Win64CvttDoubleToInt32(data->m_dBaseMP), member->GetVitalPoint(),
            static_cast<std::int8_t>(data->m_nLevel), static_cast<std::int16_t>(data->m_nClass),
            static_cast<std::int8_t>(data->m_Sex), static_cast<std::int16_t>(data->m_Race)
        );
        if (memberLength > 0) {
            memberPacketLength = runtime::AddInt32Wrapping(memberPacketLength, memberLength);
            memberCount = static_cast<std::int8_t>(memberCount + 1);
        }

        int summonCount = 0;
        int summonPacketLength = 0;
        std::array<char, 0x5000> summonPacket;
        if (member->HavePet() || member->HaveSummon()) {
            FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
                member, summon, ++summonCount; CSharedCreatureData* const summonData = summon->m_data;
                const int summonLength = ::Assemble(
                    summonPacket.data() + summonPacketLength, 0x5000 - summonPacketLength, "ddcSddddc", summon->m_nId,
                    summon->GetClassId(), static_cast<std::int8_t>(summonData->m_SummonType), summon->m_sNickName,
                    runtime::Win64CvttDoubleToInt32(summonData->m_dHP),
                    runtime::Win64CvttDoubleToInt32(summonData->m_dBaseHP),
                    runtime::Win64CvttDoubleToInt32(summonData->m_dMP),
                    runtime::Win64CvttDoubleToInt32(summonData->m_dBaseMP), static_cast<std::int8_t>(summonData->m_nLevel)
                );
                if (summonLength > 0) {
                    summonPacketLength = runtime::AddInt32Wrapping(summonPacketLength, summonLength);
                }
            );
        }

        const int summonSectionLength = summonCount == 0
            ? ::Assemble(memberPacket.data() + memberPacketLength, 0x5000 - memberPacketLength, "d", 0)
            : ::Assemble(
                  memberPacket.data() + memberPacketLength, 0x5000 - memberPacketLength, "db", summonCount,
                  summonPacketLength, summonPacket.data()
              );
        if (summonSectionLength > 0) {
            memberPacketLength = runtime::AddInt32Wrapping(memberPacketLength, summonSectionLength);
        }
    }

    recipient->GetSocket()->Send(
        "cdccb", 0x4e, m_data->m_OnlineMaster.id, static_cast<std::int8_t>(GetRoutingType()), memberCount,
        memberPacketLength, memberPacket.data()
    );
}

void CParty::OpenAllWindowPacket() {
    L2_GUARD_FUNCTION();
    std::vector<User*> members;
    GetAllMember(members);
    for (User* const member : members) {
        OpenAllWindowPacket(member);
    }
}

void CParty::AddWindowPacket(User* member) {
    L2_GUARD_FUNCTION();

    const std::uint32_t memberSid = member->m_nId;
    const char16_t* const memberName = member->GetName();
    CSharedCreatureData* const data = member->m_data;
    const std::int32_t cp = runtime::Win64CvttDoubleToInt32(data->m_dCP);
    const std::int32_t maximumCp = runtime::Win64CvttDoubleToInt32(data->m_dBaseCP);
    const std::int32_t hp = runtime::Win64CvttDoubleToInt32(data->m_dHP);
    const std::int32_t maximumHp = runtime::Win64CvttDoubleToInt32(data->m_dBaseHP);
    const std::int32_t mp = runtime::Win64CvttDoubleToInt32(data->m_dMP);
    const std::int32_t maximumMp = runtime::Win64CvttDoubleToInt32(data->m_dBaseMP);
    const std::int8_t level = static_cast<std::int8_t>(data->m_nLevel);
    const std::int16_t classId = static_cast<std::int16_t>(data->m_nClass);
    const std::int8_t sex = static_cast<std::int8_t>(data->m_Sex);
    const std::int16_t race = static_cast<std::int16_t>(data->m_Race);

    std::vector<CSummon*> summons;
    member->GetAllSummonAndPet(summons);

    std::vector<User*> members;
    GetAllMember(members);
    for (User* const recipient : members) {
        if (recipient->m_nId == memberSid) {
            continue;
        }

        // Current 00933c88 sends the recipient's vitality in the new
        // member record. This is not normalized to the added member's
        // vitality even though every surrounding field belongs to member.
        recipient->GetSocket()->Send(
            "cdddSdddddddchch", 0x4f, m_data->m_OnlineMaster.id, static_cast<std::int32_t>(GetRoutingType()), memberSid,
            memberName, cp, maximumCp, hp, maximumHp, mp, maximumMp, recipient->GetVitalPoint(), level, classId, sex, race
        );

        // This apparently extra broadcast nesting is present in current
        // 00933c88: every non-self recipient pass calls AddPetWindowPacket
        // once for each non-null summon in the added member's snapshot.
        for (CSummon* const summon : summons) {
            if (summon) {
                GuardHelper summonCall(L"in_code", CThreadManager::GetThreadID());
                AddPetWindowPacket(summon);
            }
        }
    }
}

void CParty::UpdateWindowPacket(User* member, int unusedFirst, int relationType, int relationValue, int unusedLast) {
    L2_GUARD_FUNCTION();
    static_cast<void>(unusedFirst);
    static_cast<void>(unusedLast);

    const std::uint32_t memberSid = member->m_nId;
    // Current 00930d7c makes this otherwise-unused virtual call before
    // reading the shared status fields.
    static_cast<void>(member->GetName());
    CSharedCreatureData* const data = member->m_data;
    const std::int32_t cp = runtime::Win64CvttDoubleToInt32(data->m_dCP);
    const std::int32_t maximumCp = runtime::Win64CvttDoubleToInt32(data->m_dBaseCP);
    const std::int32_t hp = runtime::Win64CvttDoubleToInt32(data->m_dHP);
    const std::int32_t maximumHp = runtime::Win64CvttDoubleToInt32(data->m_dBaseHP);
    const std::int32_t mp = runtime::Win64CvttDoubleToInt32(data->m_dMP);
    const std::int32_t maximumMp = runtime::Win64CvttDoubleToInt32(data->m_dBaseMP);
    const std::int8_t level = static_cast<std::int8_t>(data->m_nLevel);
    const std::int16_t classId = static_cast<std::int16_t>(data->m_nClass);
    const std::int32_t vitalPoint = member->GetVitalPoint();

    CPacketHelperEx<20480> changedValues;
    CPacketHelperEx<20480> changedHeader;
    std::uint16_t changeMask = 0;
    if (member->m_nPreCP != cp) {
        changeMask |= 0x001;
        changedValues.Assemble("d", cp);
    }
    if (member->m_nPreBaseCP != maximumCp) {
        changeMask |= 0x002;
        changedValues.Assemble("d", maximumCp);
    }
    if (member->m_nPreHP != hp) {
        changeMask |= 0x004;
        changedValues.Assemble("d", hp);
    }
    if (member->m_nPreBaseHP != maximumHp) {
        changeMask |= 0x008;
        changedValues.Assemble("d", maximumHp);
    }
    if (member->m_nPreMP != mp) {
        changeMask |= 0x010;
        changedValues.Assemble("d", mp);
    }
    if (member->m_nPreBaseMP != maximumMp) {
        changeMask |= 0x020;
        changedValues.Assemble("d", maximumMp);
    }
    if (member->m_nPreLevel != level) {
        changeMask |= 0x040;
        changedValues.Assemble("c", level);
    }
    if (member->m_nPreClass != classId) {
        changeMask |= 0x080;
        changedValues.Assemble("h", classId);
    }
    if (member->m_nPreVitalPoint != vitalPoint) {
        changeMask |= 0x200;
        changedValues.Assemble("d", vitalPoint);
    }
    if (changeMask == 0) {
        return;
    }
    changedHeader.Assemble("dh", memberSid, changeMask);

    std::vector<User*> members;
    GetAllMember(members);
    for (User* const recipient : members) {
        if (recipient->m_nId == memberSid) {
            continue;
        }

        recipient->GetSocket()->Send(
            "cbb", 0x52, changedHeader.GetLength(), changedHeader.GetPacket(), changedValues.GetLength(),
            changedValues.GetPacket()
        );
        recipient->GetSocket()->Send(
            "cddcccdcd", 0x18, memberSid, relationValue, static_cast<std::int8_t>(relationType), 2, 9, hp, 10, maximumHp
        );
        recipient->GetSocket()->Send(
            "cddcccdcd", 0x18, memberSid, relationValue, static_cast<std::int8_t>(relationType), 2, 0x0b, mp, 0x0c, maximumMp
        );
    }

    member->m_nPreCP = cp;
    member->m_nPreBaseCP = maximumCp;
    member->m_nPreHP = hp;
    member->m_nPreBaseHP = maximumHp;
    member->m_nPreMP = mp;
    member->m_nPreBaseMP = maximumMp;
    member->m_nPreLevel = level;
    member->m_nPreClass = classId;
    member->m_nPreVitalPoint = vitalPoint;
}

void CParty::CloseAllWindowPacket() {
    L2_GUARD_FUNCTION();
    const bool lootingChangeWasInProgress = m_IsLootingTypeModifyInProgress;
    if (lootingChangeWasInProgress) {
        CompleteModifyLootingType();
    }

    std::vector<User*> users;
    GetAllMember(users);
    for (User* const user : users) {
        if (lootingChangeWasInProgress) {
            user->GetSocket()->Send("chdd", 0xfe, 0xc1, 0, 0);
            user->GetSocket()->SendSystemMessage(SystemMessageId::PARTY_LOOT_CHANGE_CANCELLED);
        }
        user->GetSocket()->Send("c", 0x50);
    }
}

bool CParty::CheckRouteItem(CItem* item, User* user) {
    L2_GUARD_FUNCTION();
    if (!item || !user) {
        return false;
    }

    int routingType = static_cast<int>(m_ItemRoutingType);
    if (ItemForcesIndividualRouting(item)) {
        routingType = 0;
    }
    if (routingType == 0) {
        return user->m_inv.CheckAddable(item->m_data->m_nClassID, item->m_data->m_nCount, false);
    }
    if (routingType > 0 && routingType < 5) {
        std::vector<User*> users;
        GetAllMember(users);
        bool found = false;
        for (User* const candidate : users) {
            if (candidate->IsAlive() && candidate->IsInRange(user, 1500.0) &&
                candidate->m_inv.CheckAddable(item->m_data->m_nClassID, item->m_data->m_nCount, false)) {
                found = true;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

User* CParty::CheckRouteItemCond(CItem* item, User* user) {
    L2_GUARD_FUNCTION();
    if (!item) {
        return nullptr;
    }
    const std::uint32_t itemId = ReadItemClassId(item);
    if (itemId == 0x39 || !user) {
        return nullptr;
    }

    int routingType = static_cast<int>(m_ItemRoutingType);
    if (!ItemForcesIndividualRouting(item)) {
        if (CCursedWeaponMgr::inst().FindConst(item->m_data->m_nClassID)) {
            routingType = 0;
        }
        User* winner = nullptr;
        if (routingType >= 1 && routingType < 3) {
            winner = GetItemWinnerRandomly(user, itemId, static_cast<int>(ReadItemAmount(item)));
        } else if (routingType >= 3 && routingType <= 4) {
            winner = GetItemWinnerOrderly(user, itemId, static_cast<int>(ReadItemAmount(item)));
        }
        if (winner) {
            return winner;
        }
    }

    if (!user->CanDoAction_china()) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_ACT_DUE_TO_FATIGUE);
        return nullptr;
    }
    if (!user->m_inv.CheckAddable(item->m_data->m_nClassID, item->m_data->m_nCount, false)) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::INVENTORY_SLOTS_FULL);
        return nullptr;
    }
    return user;
}

bool CParty::Route(CItem* item, User* user, bool keepAdenaItem) {
    L2_GUARD_FUNCTION();
    if (!item || !CWorld::Inst().PickItemCond(item, user)) {
        return false;
    }

    std::vector<User*> members;
    if (ReadItemClassId(item) == 0x39U) {
        const bool canRoute = CheckRouteAdenaCond(item, user, members, keepAdenaItem);
        if (canRoute) {
            if (!keepAdenaItem) {
                CWorld::Inst().DoAsyncPickItem(item);
            }
            RouteAdena(item, user, members);
        }
        return canRoute;
    }

    User* const winner = CheckRouteItemCond(item, user);
    if (!winner) {
        return false;
    }
    CWorld::Inst().DoAsyncPickItem(item);
    RouteItem(item, winner);
    return true;
}

void CParty::ValidateLevel() {
    L2_GUARD_FUNCTION();
    if (m_nMaxLev < 0) {
        m_nMaxLev = 0;
    }
    m_LevelGap = m_nMaxLev - m_nMinLev;
    if (m_nMinLev > 120) {
        m_nMinLev = 120;
    }
    m_LevelGap = m_nMaxLev - m_nMinLev;

    std::vector<User*> users;
    GetAllMember(users);
    for (User* const user : users) {
        const int maximumCandidate = user->Level();
        if (maximumCandidate > m_nMaxLev) {
            m_nMaxLev = maximumCandidate;
        }
        m_LevelGap = m_nMaxLev - m_nMinLev;

        // The target queries the virtual level slot for the
        // minimum branch instead of reusing maximumCandidate.
        const int minimumCandidate = user->Level();
        if (minimumCandidate < m_nMinLev) {
            m_nMinLev = minimumCandidate;
        }
        m_LevelGap = m_nMaxLev - m_nMinLev;
    }
}

User* CParty::GetMasterCandidate() {
    L2_GUARD_FUNCTION();
    std::vector<User*> users;
    GetAllMember(users);
    for (User* const user : users) {
        if (user != GetMaster()) {
            return user;
        }
    }
    return nullptr;
}

void CParty::OnReplaceMaster(User* oldMaster) {
    // Current L2Server 0x00934df8.
    L2_GUARD_FUNCTION();

    CloseAllWindowPacket();
    OpenAllWindowPacket();
    SendAllSpelledInfoChanged();

    User* const newMaster = GetMaster();
    if (!oldMaster || !newMaster) {
        return;
    }

    CSPointer<User> oldMasterPointer(oldMaster, ObjectRefType::FunctionChain);
    CSPointer<User> newMasterPointer(newMaster, ObjectRefType::FunctionChain);
    CPartyMatchingRequest::inst().OnReplacePartyMaster(std::move(oldMasterPointer), std::move(newMasterPointer));
}

bool CParty::HandOverMaster(User* newMaster) {
    // Current L2Server 0x009356b0.
    L2_GUARD_FUNCTION();
    if (!newMaster) {
        return false;
    }

    if (CMultiPartyCommandChannel* const channel = GetMPCC()) {
        channel->SendCancelPacket(0x1039, DivideType::DIT_MASTER_CHANGE);
    } else {
        SendCancelPacket(0x1039, DivideType::DIT_MASTER_CHANGE);
    }

    User* const oldMaster = GetMaster();
    SetMaster(newMaster);
    newMaster->SetParty(this, nullptr);
    BroadcastSystemMsgToAllParty(SystemMessageId::CHARACTER_BECAME_PARTY_LEADER, EST_CHAR_NAME, newMaster->GetName());
    OnReplaceMaster(oldMaster);

    CMultiPartyCommandChannelManager manager;
    CSPointer<User> masterPointer(newMaster, ObjectRefType::FunctionChain);
    manager.OnHandOverPartyMaster(std::move(masterPointer));

    CPVPMatchSystem::inst().OnHandOverPartyMaster(this, newMaster);

    if (oldMaster) {
        oldMaster->GetSocket()->Send("chd", 0xfe, 0xc5, 0);
    }
    newMaster->GetSocket()->Send("chd", 0xfe, 0xc5, 1);
    return true;
}

std::uint32_t CParty::Oust(User& user, wchar_t* name) {
    // Current L2Server 0x00936574.
    L2_GUARD_FUNCTION();

#if !defined(_WIN32)
    const auto toTargetUtf16 = [](const wchar_t* source) {
        std::u16string result;
        while (*source != L'\0') {
            const std::uint32_t codePoint = static_cast<std::uint32_t>(*source++);
            if (codePoint <= 0xffffU) {
                result.push_back(static_cast<char16_t>(codePoint));
            } else {
                const std::uint32_t surrogate = codePoint - 0x10000U;
                result.push_back(static_cast<char16_t>(0xd800U + (surrogate >> 10U)));
                result.push_back(static_cast<char16_t>(0xdc00U + (surrogate & 0x3ffU)));
            }
        }
        return result;
    };
#endif

    SharedUtilsWideChar lowerName[25]{};
#if defined(_WIN32)
    static_assert(sizeof(wchar_t) == sizeof(SharedUtilsWideChar));
    ToLowerCase(reinterpret_cast<const SharedUtilsWideChar*>(name), lowerName);
    wchar_t* const lookupName = reinterpret_cast<wchar_t*>(lowerName);
#else
    const std::u16string requestedName = toTargetUtf16(name);
    ToLowerCase(requestedName.c_str(), lowerName);
    std::wstring lookupNameStorage = target_utf16::ToHostCodeUnits(reinterpret_cast<const char16_t*>(lowerName));
    wchar_t* const lookupName = lookupNameStorage.data();
#endif

    CSPointer<User> targetReference = GetUserFromNameMap(user.m_data->GetWorldId(), WideToUtf16(lookupName));
    if (!targetReference) {
        return 0;
    }

    User* const target = targetReference->CastUser();
    if (target->m_data->m_nPartyId != static_cast<std::uint32_t>(m_data->m_nId)) {
        user.GetSocket()->SendSystemMessage_S(
            SystemMessageId::CANNOT_EXPEL_CHARACTER_NOT_IN_PARTY, WideToUtf16(name).c_str()
        );
        return 0;
    }

    const std::uint32_t targetSid = target->m_nId;
    const char16_t* const targetName = target->GetName();
    if (m_nMemberCount < 3) {
        DismissByWithdraw(target, false);
        return targetSid;
    }

    const std::u16string targetPacketNameStorage(targetName);
    const char16_t* const targetPacketName = targetPacketNameStorage.c_str();

    CMultiPartyCommandChannelManager manager;
    manager.OnWithdrawParty(targetReference);
    CPartyMatchingRequest::inst().OnWithdrawParty(targetReference);
    g_PartyMatchingWaitingRoom.SetInvitable(target);

    std::vector<CSummon*> targetSummons;
    target->GetAllSummonAndPet(targetSummons);
    std::vector<User*> members;
    GetAllMember(members);

    bool lootingChangeWasActive = false;
    if (m_IsLootingTypeModifyInProgress) {
        lootingChangeWasActive = true;
        CompleteModifyLootingType();
    }

    if (!g_bSendSummonHPMPInfo) {
        for (User* const member : members) {
            if (member->m_nId == targetSid) {
                continue;
            }

            std::vector<CSummon*> memberSummons;
            std::vector<CSummon*> targetOrdinarySummons;
            member->GetAllSummon(memberSummons);
            target->GetAllSummon(targetOrdinarySummons);

            for (CSummon* const summon : memberSummons) {
                if (!summon) {
                    continue;
                }
                GuardHelper inCode(L"in_code", CThreadManager::GetThreadID());
                target->GetSocket()->Send("cddcccdcd", 0x18, summon->m_nId, 0, 0, 2, 10, 0, 12, 0);
            }
            for (CSummon* const summon : targetOrdinarySummons) {
                if (!summon) {
                    continue;
                }
                GuardHelper inCode(L"in_code", CThreadManager::GetThreadID());
                member->GetSocket()->Send("cddcccdcd", 0x18, summon->m_nId, 0, 0, 2, 10, 0, 12, 0);
            }
        }
    }

    for (User* const member : members) {
        if (lootingChangeWasActive) {
            member->GetSocket()->Send("chdd", 0xfe, 0xc1, 0, 0);
            member->GetSocket()->SendSystemMessage(SystemMessageId::PARTY_LOOT_CHANGE_CANCELLED);
        }

        if (member->m_nId == targetSid) {
            member->GetSocket()->Send("c", 0x50);
            member->GetSocket()->Send("c", 0x3d);
        } else {
            member->GetSocket()->Send("cdS", 0x51, targetSid, targetPacketName);
            for (CSummon* const summon : targetSummons) {
                if (!summon) {
                    continue;
                }
                GuardHelper inCode(L"in_code", CThreadManager::GetThreadID());
                const std::uint8_t summonTypeBits = static_cast<std::uint8_t>(summon->m_data->m_SummonType);
                std::int8_t summonType = 0;
                std::memcpy(&summonType, &summonTypeBits, sizeof(summonType));
                member->GetSocket()->Send("chdcd", 0xfe, 0x6b, summon->m_nId, static_cast<int>(summonType), targetSid);
            }
        }
    }

    DeleteMember(targetSid);
    target->SetParty(nullptr, nullptr);
    target->m_data->m_nMPCCId = 0;

    User* const master = GetMaster();
    if (master) {
        const std::u16string memberNames = GetAllMemberName();
        CLogFormatter formatter;
        formatter.WriteLog1_1(
            0x335, target, master, memberNames, nullptr, nullptr, 0, 0, 0, 0, 0, static_cast<std::int32_t>(GetRoutingType()),
            0
        );
    }

    BroadcastSystemMsgToAllParty(SystemMessageId::CHARACTER_EXPELLED_FROM_PARTY, EST_CHAR_NAME, targetName);
    return targetSid;
}

void CParty::Withdraw(User* user, const bool sendPacket) {
    // Current L2Server 0x00936e9c.
    L2_GUARD_FUNCTION();
    if (!user) {
        return;
    }
    if (m_nMemberCount < 3) {
        DismissByWithdraw(user, sendPacket);
        return;
    }

    CMultiPartyCommandChannelManager manager;
    CSPointer<User> channelUser(user, ObjectRefType::FunctionChain);
    manager.OnWithdrawParty(std::move(channelUser));

    CSPointer<User> matchingUser(user, ObjectRefType::FunctionChain);
    CPartyMatchingRequest::inst().OnWithdrawParty(std::move(matchingUser));
    g_PartyMatchingWaitingRoom.SetInvitable(user);

    const std::uint32_t withdrawingSid = user->m_nId;
    const std::u16string withdrawingName = std::u16string(user->GetName() != nullptr ? user->GetName() : u"");

    std::vector<CSummon*> withdrawingSummons;
    user->GetAllSummonAndPet(withdrawingSummons);
    std::vector<User*> members;
    GetAllMember(members);

    bool lootingChangeWasActive = false;
    if (m_IsLootingTypeModifyInProgress) {
        lootingChangeWasActive = true;
        CompleteModifyLootingType();
    }

    if (!g_bSendSummonHPMPInfo) {
        for (User* const member : members) {
            if (!member || member->m_nId == withdrawingSid) {
                continue;
            }

            std::vector<CSummon*> memberSummons;
            member->GetAllSummonAndPet(memberSummons);
            for (CSummon* const summon : memberSummons) {
                if (!summon) {
                    continue;
                }
                user->GetSocket()->Send("cddcccdcd", 0x18, summon->m_nId, 0, 0, 2, 10, 0, 12, 0);
            }
            for (CSummon* const summon : withdrawingSummons) {
                if (!summon) {
                    continue;
                }
                member->GetSocket()->Send("cddcccdcd", 0x18, summon->m_nId, 0, 0, 2, 10, 0, 12, 0);
            }
        }
    }

    for (User* const member : members) {
        if (!member) {
            continue;
        }
        if (lootingChangeWasActive) {
            member->GetSocket()->Send("chdd", 0xfe, 0xc1, 0, 0);
            member->GetSocket()->SendSystemMessage(SystemMessageId::PARTY_LOOT_CHANGE_CANCELLED);
        }

        if (member->m_nId == withdrawingSid) {
            member->GetSocket()->Send("c", 0x50);
            continue;
        }

        member->GetSocket()->Send("cdS", 0x51, withdrawingSid, withdrawingName.c_str());
        for (CSummon* const summon : withdrawingSummons) {
            if (!summon) {
                continue;
            }
            member->GetSocket()->Send(
                "chdcd", 0xfe, 0x6b, summon->m_nId, static_cast<std::int32_t>(summon->m_data->m_SummonType), withdrawingSid
            );
        }
    }

    DeleteMember(withdrawingSid);
    user->SetParty(nullptr, sendPacket ? GetMaster() : nullptr);
    user->m_data->m_nMPCCId = 0;
    BroadcastSystemMsgToAllParty(SystemMessageId::CHARACTER_LEFT_PARTY, EST_CHAR_NAME, user->GetName());
}

void CParty::OnPacketOust(User& user, wchar_t* name) {
    // Current L2Server 0x009376dc.
    L2_GUARD_FUNCTION();

    CUserSocket* const socket = user.GetSocket();
    if (m_data->m_OnlineMaster.id != user.m_nId) {
        socket->Send("cd", 0x3c, 0);
        return;
    }
    if (m_data->m_bPVPMatchStart) {
        socket->Send("cd", 0x3c, 0);
        return;
    }

    const std::wstring userNameStorage = Utf16ToWide(user.GetName());
    const wchar_t* const userName = userNameStorage.c_str();
    int comparison = 0;
#if defined(_WIN32)
    comparison = ::_wcsicmp(name, userName);
#else
    const wchar_t* left = name;
    const wchar_t* right = userName;
    while (*left != L'\0' && *right != L'\0') {
        const wint_t foldedLeft = std::towlower(static_cast<wint_t>(*left));
        const wint_t foldedRight = std::towlower(static_cast<wint_t>(*right));
        if (foldedLeft != foldedRight) {
            comparison = foldedLeft < foldedRight ? -1 : 1;
            break;
        }
        ++left;
        ++right;
    }
    if (comparison == 0 && *left != *right) {
        comparison = *left == L'\0' ? -1 : 1;
    }
#endif

    if (comparison == 0) {
        socket->Send("cd", 0x3c, 0);
        return;
    }

    socket->Send("cd", 0x3c, Oust(user, name) != 0 ? 1 : 0);
}

void CParty::SendAllSpelledInfoChanged() {
    L2_GUARD_FUNCTION();
    std::vector<User*> users;
    GetAllMember(users);
    for (User* const user : users) {
        user->m_bSpelledInfoChanged = true;
    }
}

void CParty::Accept(CPartyMemberVisitor& visitor) {
    L2_GUARD_FUNCTION();
    std::vector<User*> users;
    GetAllMember(users);
    for (User* const user : users) {
        visitor.Work(user);
    }
}

void CParty::BroadcastToAllParty(const char* format, ...) {
    L2_GUARD_FUNCTION();
    va_list arguments;
    va_start(arguments, format);
    std::vector<User*> users;
    GetAllMember(users);
    for (User* const user : users) {
        user->GetSocket()->SendV(format, arguments);
    }
    va_end(arguments);
}

void CParty::BroadcastToAllPartyV(const char* format, va_list arguments) {
    L2_GUARD_FUNCTION();
    std::vector<User*> users;
    GetAllMember(users);
    for (User* const user : users) {
        user->GetSocket()->SendV(format, arguments);
    }
}

void CParty::BroadcastSystemMsgToAllParty(std::uint32_t messageId, eSMsgType messageType, const char16_t* text) {
    L2_GUARD_FUNCTION();
    std::vector<User*> users;
    GetAllMember(users);
    for (User* const user : users) {
        user->GetSocket()->Send("chccS", 0x62, messageId, 1, static_cast<std::int32_t>(messageType), text);
    }
}

void CParty::StartScenePlay(int sceneId, int param1, int param2) {
    L2_GUARD_FUNCTION();
    std::vector<User*> users;
    GetAllMember(users);
    for (User* const user : users) {
        if (user) {
            user->StartScenePlay(sceneId, param1, param2);
        }
    }
}

void CParty::TeleportToLocation(
    int x, int y, int z, int randomRange, std::uint32_t parameter, TeleportSourceType::Enum source, CCreature* creature
) {
    L2_GUARD_FUNCTION();

    std::vector<User*> users;
    GetAllMember(users);
    for (User* const user : users) {
        if (!user) {
            continue;
        }

        const int lower = randomRange < 0 ? randomRange : 0;
        const int upper = randomRange < 0 ? 0 : randomRange;
        const int width = upper - lower + 1;
        const int offsetX = static_cast<int>(::genrand64_real2() * static_cast<double>(width) + static_cast<double>(lower));
        const int offsetY = static_cast<int>(::genrand64_real2() * static_cast<double>(width) + static_cast<double>(lower));

        user->StopMove();
        user->TeleportToLocation(x + offsetX, y + offsetY, z, parameter, 0x10000, source, creature, 0);
    }
}

bool CParty::IsAllPartyMemberSamePledge() {
    L2_GUARD_FUNCTION();
    std::vector<User*> users;
    GetAllMember(users);
    CPledge* pledge = nullptr;
    for (User* const user : users) {
        CPledge* candidate = pledge;
        if (user) {
            candidate = user->GetPledge();
            if (pledge && pledge != candidate) {
                return false;
            }
        }
        pledge = candidate;
    }
    return true;
}

bool CParty::IsAnyOneIsInAirShip() {
    L2_GUARD_FUNCTION();
    std::vector<User*> users;
    GetAllMember(users);
    for (User* const user : users) {
        if (user && user->IsInAirShip()) {
            return true;
        }
    }
    return false;
}

bool CParty::ResetTacticalSign(CCreature& creature) {
    L2_GUARD_FUNCTION();
    const std::uint32_t sid = ReadObjectSid(&creature);
    for (int index = 0; index < 4; ++index) {
        if (static_cast<std::uint32_t>(m_data->m_TaticalSign[index]) == sid) {
            OnResetTacticalSign(creature);
            m_data->m_TaticalSign[index] = 0;
            return true;
        }
    }
    return false;
}

void CParty::OnResetTacticalSign(int creatureSid) {
    L2_GUARD_FUNCTION();
    if (creatureSid == 0) {
        return;
    }
    CObject* const object = CObject::GetObjectW(static_cast<std::uint32_t>(creatureSid));
    if (object) {
        OnResetTacticalSign(*reinterpret_cast<CCreature*>(object));
    }
}

CParty* CParty::TestCreateParty(User* const requester, User* const replier, const ItemRoutingType routingType) {
    // Current L2Server.exe 0x009350c4, original owner Party.cpp.
    L2_GUARD_FUNCTION();
    if (CReplyChecker::AttachReplyChecker(2, requester, replier, 60)) {
        requester->m_ItemRoutingType = routingType;
    }

    CSPointer<User> actualRequester = CReplyChecker::GetRequesterFromReplier(replier);
    CParty* const party = new CParty(actualRequester->m_ItemRoutingType);
    party->SetMaster(actualRequester.get());
    static_cast<void>(party->AddMember(actualRequester.get()));
    static_cast<void>(party->AddMember(replier));
    g_PartyMap.Insert(runtime::Int32FromBits(party->m_nId), party, "Party.cpp", 0x0cfa);

    actualRequester->SetParty(party, nullptr);
    requester->SetParty(party, nullptr);
    replier->SetParty(party, nullptr);
    party->OpenAllWindowPacket(requester);
    party->OpenAllWindowPacket(replier);

    CSPointer<User> requesterReference(requester, ObjectRefType::FunctionChain);
    static_cast<void>(CPartyMatchingRequest::inst().OnJoinParty(actualRequester, requesterReference));
    static_cast<void>(g_PartyMatchingWaitingRoom.SetNonInvitable(actualRequester.get()));
    static_cast<void>(g_PartyMatchingWaitingRoom.SetNonInvitable(requester));
    L2_RELEASE(party, ObjectRefType::CreateDelete);
    CReplyChecker::FinishWaitingReplyByReplier(replier);
    return party;
}

void CParty::TimerExpired(int id) {
    L2_GUARD_FUNCTION();
    if (id == 1) {
        if (m_nTimerCount == 4) {
            m_nTimerCount = 0;
            MemberPositionPacket();
        } else {
            ++m_nTimerCount;
        }
        if (m_data->m_bIsActive) {
            AddTimer(2500U, 1);
        }
        return;
    }

    if (id != 2 || !m_IsLootingTypeModifyInProgress) {
        return;
    }
    if (m_LootingChangeTimeout > 10000) {
        CompleteModifyLootingType();
        return;
    }
    m_LootingChangeTimeout += 1000;
    AddTimer(1000U, 2);
}
