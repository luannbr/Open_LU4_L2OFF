#include "Fishing.h"
#include "Area.h"
#include "CWorld.h"
#include "Creature.h"
#include "FishingParser.h"
#include "Inventory.h"
#include "NpcServer.h"
#include "PledgeMissionInfo.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Relay.h"
#include "Shared/src/SharedFactory.h"
#include "ThreadLocalWorldMap.h"
#include "ToDoList.h"
#include "TransformConst.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Exception.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>

CFishingDB g_FishingDB;

CFishingDB::CFishingDB() : m_dFishingProbablityProb(0.0), m_nWaitNextFishing(15000) {}

CFishingDB::~CFishingDB() = default;

void CFishingDB::Add(CFishInfo* const info) {
    L2_GUARD_FUNCTION();
    if (info != nullptr && info->m_nFishGroupId < 500) {
        // The current machine performs the same signed-only upper bound.
        m_vFishInfo[info->m_nFishGroupId].push_back(info);
    }
}

void CFishingDB::Add(CFishingPlaceInfo* const info) {
    L2_GUARD_FUNCTION();
    if (info != nullptr) {
        m_mapFishingPlaceInfo.emplace(info->m_nId, info);
    }
}

void CFishingDB::Add(CFishingDistributionInfo* const info) {
    L2_GUARD_FUNCTION();
    if (info != nullptr) {
        m_mapFishingDistributionInfo.emplace(info->m_nId, info);
    }
}

void CFishingDB::Add(CFishingLureInfo* const info) {
    L2_GUARD_FUNCTION();
    if (info != nullptr) {
        m_mapFishingLureInfo.emplace(info->m_nItemId, info);
    }
}

void CFishingDB::Add(CFishingRodInfo* const info) {
    L2_GUARD_FUNCTION();
    if (info != nullptr) {
        m_mapFishingRodInfo.emplace(info->m_nItemId, info);
    }
}

void CFishingDB::Add(CFishingMonsterInfo* const info) {
    L2_GUARD_FUNCTION();
    if (info != nullptr) {
        m_vFishingMonsterInfo.push_back(info);
    }
}

void CFishingDB::Add(CFishingSkillInfo* const info) {
    L2_GUARD_FUNCTION();
    if (info != nullptr) {
        m_mapFishingSkillInfo.emplace(info->m_nID, info);
    }
}

CFishInfo* CFishingDB::GetRandomFishByGroup(const int group) const {
    L2_GUARD_FUNCTION();
    if (static_cast<unsigned int>(group) >= 500U) {
        return nullptr;
    }
    const FishList& fish = m_vFishInfo[group];
    return fish[static_cast<std::size_t>(std::rand() % static_cast<int>(fish.size()))];
}

CFishingPlaceInfo* CFishingDB::FindPlace(const FVector& position, const int placeType) const {
    L2_GUARD_FUNCTION();
    if (static_cast<unsigned int>(placeType) >= 3U) {
        return nullptr;
    }
    for (const auto& [id, info] : m_mapFishingPlaceInfo) {
        (void)id;
        if (info != nullptr && info->m_nPlaceType == placeType && info->m_pTerritory != nullptr &&
            info->m_pTerritory->IsInside(position, false)) {
            return info;
        }
    }
    return nullptr;
}

CFishingRodInfo* CFishingDB::FindFishingRod(const int itemId) const {
    L2_GUARD_FUNCTION();
    const auto position = m_mapFishingRodInfo.find(itemId);
    return position == m_mapFishingRodInfo.end() ? nullptr : position->second;
}

CFishingLureInfo* CFishingDB::FindLure(const int itemId) const {
    L2_GUARD_FUNCTION();
    const auto position = m_mapFishingLureInfo.find(itemId);
    return position == m_mapFishingLureInfo.end() ? nullptr : position->second;
}

bool CFishingDB::PostLoad() {
    L2_GUARD_FUNCTION();
    // Noise: the original logs one error for every fish and rod pair. The
    // current fishingdata.txt sets combat_duration=0 for every fish, which
    // gives thousands of lines. Count the pairs and log one summary instead.
    std::size_t lowPairs = 0;
    std::size_t checkedPairs = 0;
    long long firstDuration = 0;
    int firstFish = 0;
    int firstRod = 0;
    for (const FishList& group : m_vFishInfo) {
        for (const CFishInfo* const fish : group) {
            if (fish == nullptr) {
                continue;
            }
            for (const auto& [id, rod] : m_mapFishingRodInfo) {
                (void)id;
                if (rod != nullptr) {
                    ++checkedPairs;
                }
                if (rod != nullptr && fish->m_nCombatDuration + rod->m_nFishingDurationBonus < 10) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d], fishing duration is too low. %d, fish %d, rod %d",
                    //     L"fishing.cpp",
                    //     0x168,
                    //     fish->m_nCombatDuration +
                    //         rod->m_nFishingDurationBonus,
                    //     fish->m_nId,
                    //     rod->m_nId)
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d], fishing duration is too low. %d, fish %d, rod %d",
                    //     L"fishing.cpp",
                    //     0x168,
                    //     fish->m_nCombatDuration +
                    //         rod->m_nFishingDurationBonus,
                    //     fish->m_nId,
                    //     rod->m_nId)
                    // L2_LOG_ERROR(
                    //     Log,
                    //     "Fishing data has combat duration %lld below 10 for fish %d and rod %d; post-load validation "
                    //     "continues and retains the data.",
                    //     static_cast<long long>(fish->m_nCombatDuration) + rod->m_nFishingDurationBonus, fish->m_nId,
                    //     rod->m_nId
                    // )
                    if (lowPairs == 0) {
                        firstDuration = static_cast<long long>(fish->m_nCombatDuration) + rod->m_nFishingDurationBonus;
                        firstFish = fish->m_nId;
                        firstRod = rod->m_nId;
                    }
                    ++lowPairs;
                }
            }
        }
    }
    if (lowPairs != 0) {
        L2_LOG_WARN(
            Log,
            "Fishing data has combat duration below 10 for %zu of %zu fish and rod pairs (first: fish %d, rod %d, "
            "duration %lld). The data is retained. The fishing wait timer uses start_combat_time and the rod bonus.",
            lowPairs, checkedPairs, firstFish, firstRod, firstDuration
        );
    }
    return true;
}

void CFishingDB::Load() {
    // Current L2Server 0x00736978.
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kFishingPath = L"..\\Script\\fishingdata.txt";
#else
    constexpr const wchar_t* kFishingPath = L"../script/fishingdata.txt";
#endif

    CSecureStream input(kFishingPath, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from fishing file failed",
        //     L"fishing.cpp",
        //     0x13f)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from fishing file failed",
        //     L"fishing.cpp",
        //     0x13f)
        L2_LOG_ERROR(Log, "Cannot read fishingdata.txt; fishing data loading stops.");
        return;
    }

    CParserForFishing parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fishing parser.create failed",
        //     L"fishing.cpp",
        //     0x148)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fishing parser.create failed",
        //     L"fishing.cpp",
        //     0x148)
        L2_LOG_ERROR(Log, "Cannot initialize fishingdata.txt parser; fishing data loading stops.");
        return;
    }

    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing %s file failed after line[%d]",
        //     L"fishing.cpp",
        //     0x154,
        //     kFishingPath,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing %s file failed after line[%d]",
        //     L"fishing.cpp",
        //     0x154,
        //     kFishingPath,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "Cannot parse fishingdata.txt at script line %lld; data registered before the error is retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
}

CFishingController::__async_arg_DoAutoFishing::__async_arg_DoAutoFishing(__async_arg_DoAutoFishing* other) :
    __a1(other->__a1) {}

CFishingController::__async_arg_DoAutoFishing::__async_arg_DoAutoFishing() : __a1(false) {}

CFishingController::__async_arg_DoAutoFishing::__async_arg_DoAutoFishing(const bool start) : __a1(start) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_1(CFishingController, __async_arg_DoAutoFishing)

CFishingController::__async_arg_AsyncTimerExpired::__async_arg_AsyncTimerExpired(__async_arg_AsyncTimerExpired* other) :
    __a1(other->__a1) {}

CFishingController::__async_arg_AsyncTimerExpired::__async_arg_AsyncTimerExpired() {}

CFishingController::__async_arg_AsyncTimerExpired::__async_arg_AsyncTimerExpired(int a1) : __a1(a1) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_1(CFishingController, __async_arg_AsyncTimerExpired)

CFishingController::CFishingController() :
    m_FishingState(FISHINGS_NONE),
    m_nExperienceLevel(0),
    m_nReelingLevel(0),
    m_nPumpingLevel(0),
    m_dFishingShotBonus(0.0),
    m_dExperienceBonus(0.0),
    m_dReelingBonus(0.0),
    m_dPumpingBonus(0.0),
    m_nFishingCount(0),
    m_bInstantKillFlg(false),
    m_nFishingCountSamePlace(0) {
    L2_GUARD_FUNCTION();
}

CFishingController::~CFishingController() {
    L2_GUARD_FUNCTION();

    m_spActor = static_cast<CObject*>(nullptr);
    m_pFishingPlaceInfo = nullptr;
    m_pFishingDistributionInfo = nullptr;
    m_pFishInfo = nullptr;
}

void CFishingController::SetActor(User* actor) {
    L2_GUARD_FUNCTION();

    m_spActor = actor;
#if defined(_WIN32)
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    Serializer.SetOwnerName(reinterpret_cast<const char16_t*>(actor->GetName()));
#else
    actor->GetName();
    Serializer.SetOwnerName(actor->m_data->Name());
#endif
}

bool CFishingController::InitFishingData() {
    // Current L2Server 0x00733a24.
    L2_GUARD_FUNCTION();
    m_pFishingPlaceInfo = nullptr;
    m_pFishingDistributionInfo = nullptr;
    m_pFishInfo = nullptr;
    m_pFishingLureInfo = nullptr;
    m_pFishingRodInfo = nullptr;
    m_nLureId = -1;
    m_dFishingRodDamage = 0.0;
    m_nFishGroupId = -1;
    m_nFishId = -1;
    m_bFishAlive = false;
    m_nFishHP = 0;
    m_nFullFishHP = 0;
    m_dFishHPRegen = 0.0;
    m_dGutsProb = 0.0;
    m_dCheatingProb = 0.0;
    m_nGutsCheckTime = 0;
    m_nGuts = 0;
    m_nCheating = 0;
    m_nCheatingCount = 0;
    m_nGutsCount = 0;
    m_nTimeCount = 0;
    m_nSuccessFishing = 0;
    m_nItemId = 0;
    m_nFishGrade = 0;
    m_nEffect = 0;
    m_nMonsterID = 0;
    m_dMonsterProb = 0.0;
    m_bEquipPenalty = false;
    m_nPenalty = 0;
    m_nFishLengthMax = 0;
    m_dFishLengthRate = 0.0;
    m_nLureLengthBonus = 0;
    m_dLureLengthRateBonus = 0.0;
    m_nLureType = 0;
    m_bIsWaitNextAutoFishing = false;
    std::fill(std::begin(m_szIsNight), std::end(m_szIsNight), u'\0');
    m_szIsNight[0] = u'0';
    return false;
}

bool CFishingController::IsWaterPlace(FVector& position, User* const actor) {
    // Current L2Server 0x00733f34.
    L2_GUARD_FUNCTION();
    if (actor == nullptr) {
        return false;
    }
    int waterSurface = 0;
    const bool water = CAreaDB::inst().IsWaterZone(position, waterSurface, static_cast<int>(actor->GetInZoneID()));
    if (water) {
        position.z = static_cast<double>(waterSurface);
    }
    return water;
}

namespace {

template <std::size_t Capacity>
void FailFishingWideCopy(std::array<char16_t, Capacity>& destination, const int error) {
    destination[0] = u'\0';
    errno = error;
    runtime::process::HandleInvalidParameter(L"wcscpy_s");
}

template <std::size_t Capacity>
void CopyFishingTargetWide(std::array<char16_t, Capacity>& destination, const char16_t* const source) {
    if (source == nullptr) {
        FailFishingWideCopy(destination, EINVAL);
        return;
    }
    for (std::size_t index = 0; index < Capacity; ++index) {
        const char16_t value = source[index];
        destination[index] = value;
        if (value == u'\0') {
            return;
        }
    }
    FailFishingWideCopy(destination, ERANGE);
}


} // namespace

bool CFishingController::CheckFishingCondition(int& messageId) {
    // Current L2Server 0x00734520.
    L2_GUARD_FUNCTION();
    User* const actor = m_spActor.GetUser();
    if (actor == nullptr || !actor->IsAlive()) {
        return false;
    }
    if (!actor->IsPremiumUser()) {
        messageId = SystemMessageId::FISHING_REQUIRES_PAID_SERVICE_PERIOD;
        return false;
    }
    if (m_FishingState != FISHINGS_NONE) {
        return false;
    }

    const int rodId = actor->GetEquipedFishingRodId();
    if (rodId == -1) {
        messageId = SystemMessageId::FISHING_ROD_NOT_EQUIPPED;
        return false;
    }
    m_pFishingRodInfo = g_FishingDB.FindFishingRod(rodId);
    if (m_pFishingRodInfo == nullptr) {
        messageId = SystemMessageId::ITEM_REQUIREMENTS_NOT_MET;
        return false;
    }

    const int lureId = actor->GetEquipedLureId();
    if (lureId == -1) {
        messageId = SystemMessageId::FISHING_BAIT_NOT_EQUIPPED;
        return false;
    }
    m_pFishingLureInfo = g_FishingDB.FindLure(lureId);
    if (m_pFishingLureInfo == nullptr) {
        messageId = SystemMessageId::ITEM_REQUIREMENTS_NOT_MET;
        return false;
    }
    if (actor->m_AreaMod.m_bIsUnderWater) {
        messageId = SystemMessageId::CANNOT_FISH_UNDERWATER;
        return false;
    }
    if (actor->IsInVehicle() || actor->IsInAirShip() || actor->IsInShuttle() || actor->IsRiding() ||
        actor->IsTransformed(TransformType::Max) != nullptr) {
        messageId = SystemMessageId::CANNOT_FISH_WHILE_MOUNTED_OR_TRANSFORMED;
        return false;
    }

    constexpr double kPi = 3.1415926535897932384626433832795;
    const std::uint16_t yaw = static_cast<std::uint16_t>(actor->GetDir().Yaw);
    const double angle = static_cast<double>(yaw) * kPi / 32767.0;
    const FVector origin = actor->GetPos();
    FVector destination(
        origin.x + static_cast<double>(runtime::Win64CvttDoubleToInt32(std::cos(angle) * 300.0)),
        origin.y + static_cast<double>(runtime::Win64CvttDoubleToInt32(std::sin(angle) * 300.0)), origin.z + 100.0
    );
    const FVector source(
        static_cast<double>(runtime::Win64CvttDoubleToInt32(actor->GetPos().x)),
        static_cast<double>(runtime::Win64CvttDoubleToInt32(actor->GetPos().y)),
        static_cast<double>(runtime::Win64CvttDoubleToInt32(actor->GetPos().z) + 100)
    );
    CGeoData* const geo = CWorld::Inst().GetGeo();
    if (geo != nullptr && geo->CanSee(source, destination, static_cast<int>(actor->GetInZoneID()), false)) {
        destination.z = static_cast<double>(geo->FindGround(
            runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y),
            runtime::Win64CvttDoubleToInt32(destination.z)
        ));
        CFishingPlaceInfo* const place = g_FishingDB.FindPlace(destination, 0);
        if (place != nullptr && IsWaterPlace(destination, actor)) {
            return true;
        }
    }
    messageId = SystemMessageId::CANNOT_FISH_NOT_FACING_WATER_OR_OUTSIDE_FISHING_AREA;
    return false;
}

bool CFishingController::SetFishingInfo() {
    // Current L2Server 0x00734934.
    L2_GUARD_FUNCTION();
    User* const actor = m_spActor.GetUser();
    if (actor == nullptr) {
        return false;
    }

    constexpr double kPi = 3.1415926535897932384626433832795;
    const std::uint16_t yaw = static_cast<std::uint16_t>(actor->GetDir().Yaw);
    const double angle = static_cast<double>(yaw) * kPi / 32767.0;
    const FVector origin = actor->GetPos();
    FVector destination(
        origin.x + static_cast<double>(runtime::Win64CvttDoubleToInt32(std::cos(angle) * 300.0)),
        origin.y + static_cast<double>(runtime::Win64CvttDoubleToInt32(std::sin(angle) * 300.0)), origin.z + 100.0
    );
    const FVector source(
        static_cast<double>(runtime::Win64CvttDoubleToInt32(actor->GetPos().x)),
        static_cast<double>(runtime::Win64CvttDoubleToInt32(actor->GetPos().y)),
        static_cast<double>(runtime::Win64CvttDoubleToInt32(actor->GetPos().z) + 100)
    );
    CGeoData* const geo = CWorld::Inst().GetGeo();
    if (geo == nullptr || !geo->CanSee(source, destination, static_cast<int>(actor->GetInZoneID()), false)) {
        actor->SendSystemMessage(SystemMessageId::CANNOT_FISH_NOT_FACING_WATER_OR_OUTSIDE_FISHING_AREA);
        return false;
    }

    destination.z = static_cast<double>(geo->FindGround(
        runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y),
        runtime::Win64CvttDoubleToInt32(destination.z)
    ));
    m_pFishingPlaceInfo = g_FishingDB.FindPlace(destination, 0);
    if (m_pFishingPlaceInfo != nullptr && !IsWaterPlace(destination, actor)) {
        destination.z = static_cast<double>(geo->FindGround(
            runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y),
            runtime::Win64CvttDoubleToInt32(destination.z)
        ));
    }
    actor->m_data->m_NeedUpdatePacketBitSet.SetFVectorValue_IfModifed(m_vFishingPos, destination, 0x4c);

    m_pFishingLureInfo = g_FishingDB.FindLure(actor->GetEquipedLureId());
    if (m_pFishingLureInfo == nullptr || m_pFishingPlaceInfo == nullptr) {
        return false;
    }
    const auto preference = m_pFishingLureInfo->m_mPreference.find(m_pFishingPlaceInfo->m_nTerritoryGroup);
    if (preference == m_pFishingLureInfo->m_mPreference.end()) {
        actor->SendSystemMessage(SystemMessageId::BAIT_CANNOT_BE_USED_IN_THIS_AREA);
        return false;
    }

    const double random = genrand64_real1();
    double accumulated = 0.0;
    DistributionStruct* selected = nullptr;
    for (DistributionStruct* const distribution : preference->second) {
        if (distribution != nullptr && distribution->GroupID < 500) {
            accumulated += static_cast<double>(distribution->Probability) / 100.0;
            if (random * 100.0 <= accumulated) {
                selected = distribution;
                break;
            }
        }
    }
    if (selected == nullptr || selected->GroupID == -1) {
        return false;
    }
    m_pFishInfo = g_FishingDB.GetRandomFishByGroup(selected->GroupID);
    if (m_pFishInfo == nullptr) {
        return false;
    }
    m_nTimeCount = m_pFishInfo->m_nCombatDuration;
    return true;
}

bool CFishingController::SendFishingStartPacket() {
    // Current L2Server 0x00735184.
    L2_GUARD_FUNCTION();
    User* const actor = m_spActor.GetUser();
    if (actor == nullptr) {
        return false;
    }
    GetFishingInfoType();
    g_ptlUserMap->BroadcastToNeighborDist(
        actor, 0x5000, actor->GetPos(), actor->GetBroadcastDist(0x1000), "chdcdddc", 0xfe, 0x1e,
        static_cast<int>(actor->Id()), static_cast<int>(static_cast<std::int8_t>(m_nFishGroupId)),
        runtime::Win64CvttDoubleToInt32(m_vFishingPos.x), runtime::Win64CvttDoubleToInt32(m_vFishingPos.y),
        runtime::Win64CvttDoubleToInt32(m_vFishingPos.z), static_cast<int>(static_cast<std::int8_t>(m_lFishingInfo))
    );
    return true;
}

bool CFishingController::SendFishingEndPacket() {
    // Current L2Server 0x00735304.
    L2_GUARD_FUNCTION();
    User* const actor = m_spActor.GetUser();
    if (actor == nullptr) {
        return false;
    }
    g_ptlUserMap->BroadcastToNeighborDist(
        actor, 0x5000, actor->GetPos(), actor->GetBroadcastDist(0x1000), "chdc", 0xfe, 0x1f, static_cast<int>(actor->Id()),
        static_cast<int>(static_cast<std::int8_t>(m_nSuccessFishing))
    );
    return true;
}

bool CFishingController::SendFishingStartCombatPacket() {
    // Current L2Server 0x00735428.
    L2_GUARD_FUNCTION();
    User* const actor = m_spActor.GetUser();
    if (actor == nullptr) {
        return false;
    }
    g_ptlUserMap->BroadcastToNeighborDist(
        actor, 0x5000, actor->GetPos(), actor->GetBroadcastDist(0xc00), "chdddccc", 0xfe, 0x27,
        static_cast<int>(actor->Id()), m_nTimeCount, m_nFishHP, static_cast<int>(static_cast<std::int8_t>(m_nGuts)),
        static_cast<int>(static_cast<std::int8_t>(m_nFishGrade)), static_cast<int>(static_cast<std::int8_t>(m_nCheating))
    );
    return true;
}

bool CFishingController::StartFishing() {
    // Current L2Server 0x00735590.
    L2_GUARD_FUNCTION();
    User* const actor = m_spActor.GetUser();
    if (actor == nullptr) {
        return false;
    }
    if (actor->IsInvisibleBySkill()) {
        actor->SetVisibleBySkill(true, true);
    }
    InitFishingData();
    int messageId = -1;
    if (!CheckFishingCondition(messageId)) {
        if (messageId > 0) {
            actor->SendSystemMessage(static_cast<unsigned int>(messageId));
        }
        return false;
    }
    if (!SetFishingInfo()) {
        return false;
    }
    if (!actor->m_inv.UseLure(m_pFishingLureInfo->m_nItemId, 1)) {
        actor->SendSystemMessage(SystemMessageId::NOT_ENOUGH_BAIT);
        return false;
    }

    if (++m_nFishingCount > 2000) {
        m_nFishingCount = 0;
    }
    if (actor->m_data->m_nFishingShot == 1) {
        actor->TurnOffFishingShot(false);
    } else {
        m_dFishingShotBonus = 0.0;
    }
    actor->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
        reinterpret_cast<std::int32_t&>(m_FishingState), FISHINGS_WAIT, 0x4c
    );
    SendFishingStartPacket();
    actor->PlayMusic(u"SF_P_01");
    const int duration = std::max(10, m_pFishInfo->m_nStartCombatTime + m_pFishingRodInfo->m_nFishingDurationBonus);
    AddTimer(static_cast<std::int64_t>(duration) * 1000, m_nFishingCount * 10 + 1);
    return true;
}

bool CFishingController::EndFishing() {
    // Current L2Server 0x007357ac.
    L2_GUARD_FUNCTION();
    User* const actor = m_spActor.GetUser();
    if (actor != nullptr) {
        actor->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
            reinterpret_cast<std::int32_t&>(m_FishingState), FISHINGS_NONE, 0x4c
        );
        SendFishingEndPacket();
        m_FishingEndTime = runtime::crt::Time32Now();
    }
    return false;
}

void CFishingController::SetNoEndingAction() {
    L2_GUARD_FUNCTION();
    m_nSuccessFishing = 2;
}

void CFishingController::GetFishingInfoType() {
    L2_GUARD_FUNCTION();
    m_lFishingInfo = 2;
}

void CFishingController::DoAutoFishing(const bool start) {
    __async_arg_DoAutoFishing argument(start);
    Serializer.AddExecutable(
        this, [](CFishingController* owner, CExecutionArgumentPad* pad) { owner->__async_DoAutoFishing(pad); }, &argument,
        L"DoAutoFishing"
    );
}

void CFishingController::__async_DoAutoFishing(CExecutionArgumentPad* argument) {
    // Current L2Server 0x00735890.
    L2_ASYNC_IMPL1_GUARD(CFishingController, DoAutoFishing, bool, bIsStart);
    User* const actor = m_spActor.GetUser();
    if (actor == nullptr) {
        return;
    }
    bool bIsStart = false;
    std::memcpy(&bIsStart, argument->Buffer + 0x08, sizeof(bIsStart));

    CLogFormatter formatter{};
    if (!bIsStart) {
        m_bIsWaitNextAutoFishing = false;
        m_bIsAutoFishing = false;
        if (m_FishingState != FISHINGS_NONE) {
            SetNoEndingAction();
            EndFishing();
        }
        actor->GetSocket()->Send("chc", 0xfe, 0x17b, 0);
        actor->SendSystemMessage(SystemMessageId::FISHING_ENDED);
        formatter.WriteLog1(0x1fca, actor, nullptr, nullptr, nullptr, nullptr, 0, 0, 0, 0, 0, 0, 0);
        m_dFishingShotBonus = 0.0;
        return;
    }

    if (m_bIsWaitNextAutoFishing || m_FishingState != FISHINGS_NONE) {
        return;
    }
    const bool wasAutoFishing = m_bIsAutoFishing;
    m_bIsAutoFishing = true;
    if (StartFishing()) {
        if (!wasAutoFishing) {
            actor->SendSystemMessage(SystemMessageId::FISHING_STARTED);
            formatter.WriteLog1(0x1fc7, actor, nullptr, nullptr, nullptr, nullptr, 0, 0, 0, 0, 0, 0, 0);
        }
        return;
    }

    actor->GetSocket()->Send("chc", 0xfe, 0x17b, 0);
    m_bIsAutoFishing = false;
    if (wasAutoFishing) {
        actor->SendSystemMessage(SystemMessageId::FISHING_ENDED);
    }
    formatter.WriteLog1(0x1fca, actor, nullptr, nullptr, nullptr, nullptr, 0, 0, 0, 0, 0, 0, 0);
    m_dFishingShotBonus = 0.0;
}

void CFishingController::__async_AsyncTimerExpired(CExecutionArgumentPad* argument) {
    // Current L2Server 0x00735b2c.
    L2_ASYNC_IMPL1_GUARD(CFishingController, AsyncTimerExpired, int, id);
    User* const actor = m_spActor.GetUser();
    if (actor == nullptr) {
        return;
    }

    // The current callback takes these snapshots before it even decodes the
    // timer id. Their values are unused, but the virtual observations and
    // secure-copy failure boundary are part of the shipping call schedule.
    (void)actor->GetDBID();
    (void)actor->GetAccountID();
    std::array<char16_t, 25> characterName{};
    std::array<char16_t, 15> accountName{};
    CopyFishingTargetWide(characterName, actor->GetName());
    CopyFishingTargetWide(accountName, actor->m_sAccountName.data());
    [[maybe_unused]] FVector observedPosition = actor->GetPos();
    observedPosition = actor->GetPos();
    observedPosition = actor->GetPos();

    int id = 0;
    std::memcpy(&id, argument->Buffer + 0x08, sizeof(id));
    const int phase = id % 10;
    if (!m_bIsAutoFishing || (m_FishingState == FISHINGS_NONE && phase != 6) || id / 10 != m_nFishingCount) {
        return;
    }

    if (phase == 1) {
        actor->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
            reinterpret_cast<std::int32_t&>(m_FishingState), FISHINGS_BATTLE, 0x4c
        );
        if (m_nTimeCount != 0) {
            SendFishingStartCombatPacket();
            actor->PlayMusic(u"SF_S_01");
        }
        int delay = runtime::MultiplyInt32Wrapping(m_nTimeCount, 1000);
        if (delay < 0x50) {
            delay = 0x50;
        }
        AddTimer(delay, runtime::AddInt32Wrapping(runtime::MultiplyInt32Wrapping(m_nFishingCount, 10), 4));
        return;
    }

    if (phase == 6) {
        m_bIsWaitNextAutoFishing = false;
        DoAutoFishing(true);
        return;
    }
    if (phase != 4) {
        return;
    }

    const double successProbability = g_FishingDB.m_dFishingProbablityProb + m_dFishingShotBonus;
    const int roll = runtime::Win64CvttDoubleToInt32((genrand64_real1() * 100.0 + 0.0) * 100.0);
    const int threshold = runtime::Win64CvttDoubleToInt32(successProbability * 100.0);
    if (roll <= threshold) {
        m_nSuccessFishing = 1;
    }

    CLogFormatter formatter{};
    if (m_nSuccessFishing == 0) {
        actor->SendSystemMessage(SystemMessageId::BAIT_LOST_AFTER_FAILED_BITE);
        formatter.WriteLog1(
            0x1fc9, actor, nullptr, u"", u"", u"", m_dFishingShotBonus > 0.0, m_pFishingLureInfo->m_nItemId, roll, threshold,
            m_nSuccessFishing, 0, 0
        );
        g_npcserver.Send("cdd", 0xbe, actor->m_data->index, 0);
    } else if (m_nSuccessFishing == 1) {
        const int itemId = m_pFishInfo->m_nItemId;
        const std::int64_t resultingAmount = runtime::AddInt64Wrapping(actor->m_inv.GetItemAmount(itemId, 0, true), 1);

        InventoryParams parameters;
        parameters.nItemClassID = itemId;
        parameters.nCount = 1;
        parameters.m_Reason = ItemGetReason::Etc;
        actor->AddItemToInventory(parameters);

        formatter.WriteLog1(
            0x1fc8, actor, nullptr, u"", u"", u"", m_dFishingShotBonus > 0.0, m_pFishingLureInfo->m_nItemId, itemId, 1,
            resultingAmount, 0, 0
        );

        const CFishInfo* const rewardFishInfo = m_pFishInfo;
        std::int64_t experience = 0;
        std::int64_t experienceBonus = 0;
        if (rewardFishInfo->m_nExp != 0) {
            const double level = static_cast<double>(actor->m_data->m_nLevel);
            const bool reduced = actor->m_data->m_nLevel < 85;
            double scaled = std::pow(level, 2.2) * static_cast<double>(rewardFishInfo->m_nExp);
            if (reduced) {
                scaled /= 7.0;
            }
            const std::int64_t base = runtime::Win64CvttDoubleToInt64(scaled / 100000.0);
            experienceBonus = runtime::Win64CvttDoubleToInt64(
                static_cast<double>(base) * actor->m_SkillDurationMod.m_dExpSpBonusFishingPercent / 100.0
            );
            experience =
                runtime::AddInt64Wrapping(runtime::Win64CvttDoubleToInt64(scaled / 100000.0), experienceBonus);
            if (experience < 1) {
                experience = 1;
                experienceBonus = 0;
            }
        }

        std::int64_t sp = 0;
        std::int64_t spBonus = 0;
        if (rewardFishInfo->m_nSp != 0) {
            const double level = static_cast<double>(actor->m_data->m_nLevel);
            const bool reduced = actor->m_data->m_nLevel < 85;
            const double power = std::pow(level, 2.2);
            double scaled = power * static_cast<double>(m_pFishInfo->m_nSp);
            if (reduced) {
                scaled /= 7.0;
            }
            const std::int64_t base = runtime::Win64CvttDoubleToInt64(scaled / 100000.0);
            spBonus = runtime::Win64CvttDoubleToInt64(
                static_cast<double>(base) * actor->m_SkillDurationMod.m_dExpSpBonusFishingPercent / 100.0
            );
            sp = runtime::AddInt64Wrapping(runtime::Win64CvttDoubleToInt64(scaled / 100000.0), spBonus);
            if (sp < 1) {
                sp = 1;
                spBonus = 0;
            }
        }
        actor->ExpInc(experience, false);
        actor->SPInc(sp);
        RelayClient::Inst()->SendFishingItemPakcet(actor, itemId, experience, sp);
        actor->GetSocket()->Send(
            "chccQcQcQcQ", 0x62, SystemMessageId::OBTAINED_XP_AND_SP_WITH_BONUSES, 4, 6, experience, 6, experienceBonus, 6,
            sp, 6, spBonus
        );
        formatter.WriteLog2_1(0x1fbb, actor, nullptr, experience, sp, 0, 0, 0, 0, 0);
        g_npcserver.Send("cdd", 0xbe, actor->m_data->index, m_pFishInfo->m_nId);
        actor->PledgeMissionCheckAndAddCount(PledgeMission::PMGT_FISHING, m_pFishInfo->m_nItemId, 1);
        actor->PledgeMissionCheckAndAddCount(PledgeMission::PMGT_FISHING_ANY, 0, 1);
        actor->PledgeMissionCheckAndAddCount(PledgeMission::PMGT_FISHING_BAIT, m_pFishingLureInfo->m_nItemId, 1);
    }

    g_ToDoListManager.OnDailyMissionCheckAndUpdate(actor, E_DAILY_MISSION_TYPE_FISHINGCOUNT);
    EndFishing();
    if (m_bIsAutoFishing) {
        m_bIsWaitNextAutoFishing = true;
        AddTimer(
            g_FishingDB.m_nWaitNextFishing,
            runtime::AddInt32Wrapping(runtime::MultiplyInt32Wrapping(m_nFishingCount, 10), 6)
        );
    }
}

void CFishingController::TimerExpired(const int id) {
    // Current L2Server 0x00736310.
    L2_GUARD_FUNCTION();
    if (m_spActor.GetUser() == nullptr) {
        return;
    }
    __async_arg_AsyncTimerExpired argument(id);
    Serializer.AddExecutable(
        this, [](CFishingController* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncTimerExpired(pad); },
        &argument, L"AsyncTimerExpired"
    );
}
