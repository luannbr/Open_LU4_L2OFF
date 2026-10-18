#include "CreatureCommon.h"
#include "Protocol/src/L2Constants.h"
#include "Shared/src/MemorySub.h"

#include "AirShip.h"
#include "CContributeData.h"
#include "CreatureController.h"
#include "Setting.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/TimeAnalysis.h"

#include <algorithm>
#include <cstring>

// Current Creature_common.obj owns one empty default class-constant object.
// The current image only binds and compares its address. No field access or
// lifetime action exists anywhere in the image.
class CClassConstant {};
CClassConstant DEFAULT_CLASS_CONSTANT;

// Current Creature_common.cpp publishes this otherwise-unused specialization
// eagerly. Its 1023 RWLocks are constructed by CRT root 00d7eff4 before the
// process enters main and destroyed through the paired 00dd609c callback.
// MSVC and GCC reject an explicit specialization of an inline static member.
// This explicit instantiation also constructs the locks before main, with
// every compiler.
template std::array<RWLock, 1023> CObjectStorage<CCreature>::s_locks;

void CreatureCommonFunc::SaveUnEquipedItemAndSetNull(
    CCreature* creature, std::map<int, CItem*>& unequippedItems, SlotType slotType
) {
    // Current L2Server 0x005dd4b0.
    L2_GUARD_FUNCTION();
    if (static_cast<unsigned int>(slotType) >= static_cast<unsigned int>(ST_MAX)) {
        return;
    }

    int& itemIndex = creature->m_data->m_nSlotIndex[slotType];
    if (itemIndex == -1) {
        return;
    }
    unequippedItems[slotType] = creature->m_inv.GetByIndex(itemIndex);
    itemIndex = -1;
}

CCreature::CCreature() {
    L2_GUARD_FUNCTION();

    m_bFloatingMoveMode = false;
    m_bIsGMKill = false;
    m_nDead = 0;
    m_dPenaltyTargetCanResist = 1.0;

    m_data = CCreatureFactory::Inst().Add(this);
    m_reserved = nullptr;

    // The target constructs the embedded duration modifier before touching
    // the mapped record. Its C++ member constructor has already done so here.
    m_data->m_nMasterExpRatio = 100;
    m_data->m_bIsInCombatMode = false;
    m_data->m_nSSQRoundNumber = -1;
    m_data->m_nSSQPart = 0;
    m_data->m_nSSQPosition = 0;
    m_data->m_nSealSelectionNo = 0;
    m_data->m_nSSQPoint = 0;
    std::fill(std::begin(m_data->m_nTwilightItemCount), std::end(m_data->m_nTwilightItemCount), 0);
    std::fill(std::begin(m_data->m_nDawnItemCount), std::end(m_data->m_nDawnItemCount), 0);
    m_data->m_nTicketBuyCount = 0;
    m_data->m_nSSQDawnRound = 0;
    m_data->m_nInPeaceZone = 0;
    m_data->m_nInBattleField = 0;
    m_data->m_nPCCafePoint = 0;
    m_data->m_bCanUsePCCafePoint = false;
    m_data->m_nDailyPCCafePoint = 0;
    m_data->m_nBonusCheckLevel = 0;
    m_data->m_nPremiumLevel = 0;
    m_data->m_bPremiumPCCafeUser = 0;
    m_data->m_BaseAttribute.Init();
    m_data->m_TAttackType = static_cast<AttributeTypeEnum>(-2);
    m_data->m_OrgDefenceType = static_cast<AttributeTypeEnum>(-2);
    m_data->m_TransDefenceType = static_cast<AttributeTypeEnum>(-2);
    m_data->m_bCanMove = true;
    m_data->m_nInstantZoneID = 0;
    m_data->m_nInstantZoneTypeID = 0;
    m_data->m_bTeleporting = false;
    m_data->m_nPrivateRespawnNoLog = 0;
    m_nDownExp = 0;
    m_bIsPetAlive = 1;
    for (bool& flag : m_data->m_StatusFlag) {
        flag = true;
    }

    CSetting& setting = Singleton<CSetting>::inst();
    const CSetting::StatValueType& stat = setting.m_recommended_stat[0];
    m_data->m_Stat.m_N[0] = stat.Int;
    m_data->m_Stat.m_N[1] = stat.Str;
    m_data->m_Stat.m_N[2] = stat.Con;
    m_data->m_Stat.m_N[3] = stat.Men;
    m_data->m_Stat.m_N[4] = stat.Dex;
    m_data->m_Stat.m_N[5] = stat.Wit;
    m_data->m_Stat.m_N[6] = stat.Luc;
    m_data->m_Stat.m_N[7] = stat.Cha;
    m_data->m_OriginalStat = m_data->m_Stat;
    m_data->m_AbnormalResist.m_N[0] = setting.m_ResistType[0].m_PAbnormalResist;
    m_data->m_AbnormalResist.m_N[1] = setting.m_ResistType[0].m_MAbnormalResist;
    m_data->m_OriginAbnormalResist = m_data->m_AbnormalResist;

    Init();
    m_ShowPosTarget = 0;
    m_ShowActionChangeTarget = 0;
    m_nLastSkillId = 0;
    m_nChainAttackCount = 0;
    m_DefenceFatalBlowSuccessBonus.m_Per = 1.0;
    m_DefenceFatalBlowSuccessBonus.m_Diff = 0.0;
}

CCreature::CCreature(const CCreature& other) : CWorldObject(other) {
    L2_GUARD_FUNCTION();

    m_bFloatingMoveMode = false;
    m_bIsGMKill = false;
    m_nDead = 0;
    m_dPenaltyTargetCanResist = 1.0;

    {
        LagAlert lagAlert(5000, "CCreature::CCreature", 0x84);
        m_data = CCreatureFactory::Inst().Add(this);
    }
    m_reserved = nullptr;

    m_data->m_nMasterExpRatio = 100;
    m_data->m_bIsInCombatMode = false;
    m_data->m_nSSQRoundNumber = -1;
    m_data->m_nSSQPart = 0;
    m_data->m_nSSQPosition = 0;
    m_data->m_nSealSelectionNo = 0;
    m_data->m_nSSQPoint = 0;
    std::fill(std::begin(m_data->m_nTwilightItemCount), std::end(m_data->m_nTwilightItemCount), 0);
    std::fill(std::begin(m_data->m_nDawnItemCount), std::end(m_data->m_nDawnItemCount), 0);
    m_data->m_nTicketBuyCount = 0;
    m_data->m_nSSQDawnRound = 0;
    m_data->m_nInPeaceZone = 0;
    m_data->m_nInBattleField = 0;
    m_data->m_nPCCafePoint = 0;
    m_data->m_bCanUsePCCafePoint = false;
    m_data->m_nDailyPCCafePoint = 0;
    m_data->m_nBonusCheckLevel = 0;
    m_data->m_nPremiumLevel = 0;
    m_data->m_bPremiumPCCafeUser = 0;
    m_data->m_BaseAttribute.Init();
    m_data->m_TAttackType = static_cast<AttributeTypeEnum>(-2);
    m_data->m_OrgDefenceType = static_cast<AttributeTypeEnum>(-2);
    m_data->m_TransDefenceType = static_cast<AttributeTypeEnum>(-2);
    m_data->m_bCanMove = true;
    m_data->m_nInstantZoneID = 0;
    m_data->m_nInstantZoneTypeID = 0;
    m_data->m_bTeleporting = false;
    m_data->m_nPrivateRespawnNoLog = 0;

    m_nDownExp = other.m_nDownExp;
    m_bIsPetAlive = other.m_bIsPetAlive;
    for (bool& flag : m_data->m_StatusFlag) {
        flag = true;
    }

    CSetting& setting = Singleton<CSetting>::inst();
    const CSetting::StatValueType& stat = setting.m_recommended_stat[0];
    m_data->m_Stat.m_N[0] = stat.Int;
    m_data->m_Stat.m_N[1] = stat.Str;
    m_data->m_Stat.m_N[2] = stat.Con;
    m_data->m_Stat.m_N[3] = stat.Men;
    m_data->m_Stat.m_N[4] = stat.Dex;
    m_data->m_Stat.m_N[5] = stat.Wit;
    m_data->m_Stat.m_N[6] = stat.Luc;
    m_data->m_Stat.m_N[7] = stat.Cha;
    m_data->m_OriginalStat = m_data->m_Stat;
    m_data->m_AbnormalResist.m_N[0] = setting.m_ResistType[0].m_PAbnormalResist;
    m_data->m_AbnormalResist.m_N[1] = setting.m_ResistType[0].m_MAbnormalResist;
    m_data->m_OriginAbnormalResist = m_data->m_AbnormalResist;

    m_nLastSkillId = 0;
    m_nChainAttackCount = other.m_nChainAttackCount;
    m_DefenceFatalBlowSuccessBonus = other.m_DefenceFatalBlowSuccessBonus;
    InitializeSkillMod();
    InitializeItemMod();
    InitializeBuilderCmdMod();
    m_SkillMod.InitializeHennaBonus();
    InitializeAreaMod();

    m_data->m_nInstantZoneID = other.m_data->m_nInstantZoneID;
    m_data->m_nInstantZoneTypeID = other.m_data->m_nInstantZoneTypeID;

    Init();
    m_ShowPosTarget = 0;
    m_ShowActionChangeTarget = 0;
}

MemoryObject* CCreature::AddRef(const char* const callerFile, const int callerLine, const ObjectRefType type) {
    // Current L2Server 0x00ae4588. The subtype lane is intentionally
    // unchecked, matching the target's direct ObjectRefType indexing.
    runtime::raw_atomic::AddI32(&m_nRefCount, 1);

    RefHelperStruct& helper = m_RefHelpers.m_RefData[static_cast<std::size_t>(type)];
    helper.m_nRefCount.fetch_add(1, std::memory_order_seq_cst);
    helper.m_pAddCaller = callerFile;
    helper.m_nAddLine = callerLine;
    return this;
}

// Current L2Server 0x005e2058, Creature.h:0x59f.
L2_DEFINE_OBJECT_RELEASE(CCreature)

CCreature::~CCreature() {
    L2_GUARD_FUNCTION();

    {
        GuardHelper creatureFactoryGuard(L"creature factory", CThreadManager::GetThreadID());
        CCreatureFactory::Inst().Remove(this);
    }
    {
        GuardHelper controllerGuard(L"controller 1", CThreadManager::GetThreadID());
        if (m_pController != nullptr) {
            L2_RELEASE(m_pController, ObjectRefType::CreateDelete);
        }
    }
}

void CCreature::Init() {
    L2_GUARD_FUNCTION();

    pClassConstant = &DEFAULT_CLASS_CONSTANT;
    for (std::atomic<std::uint32_t>& id : m_attackLeaderId) {
        id.store(0, std::memory_order_relaxed);
    }
    m_data->m_iLastArrowIndex = -1;
    std::fill(std::begin(m_data->m_nSlotIndex), std::end(m_data->m_nSlotIndex), -1);

    m_pContribute = new CContributeData();
    m_bDietMode = false;
    m_dGMSpeed = 1.0;
    m_dSpeedModByWeight = 1.0;
    m_dHPRegenModByWeight = 1.0;
    m_dMPRegenModByWeight = 1.0;
    m_dCPRegenModByWeight = 1.0;
    m_nSetItemClassId = 0;
    m_mapNewSetItem.clear();
    m_mapShapeShiftedSetItem.clear();
    m_data->m_nCanHarvest = 0;
    m_bLeft = false;
    m_AlwaysSkillSuccess = false;
    m_dMeleeMultiTargetBias = 1.0;
    m_bAttackedByRange = false;
    m_bIgnoreWeaponRandomDamage = false;
    m_bIgnoreRegeneration = false;
    PrepareLookNeighbor.store(0, std::memory_order_relaxed);
    m_bMoving = false;
    m_vMovingDirection = FVector{};
    m_nLastTick = UINT32_MAX;
    m_data->m_bIsActive = false;
    m_data->m_nClanIdCount = 0;
    std::fill(std::begin(m_data->m_ClanId), std::end(m_data->m_ClanId), 0);
    m_data->m_nClanHelpRange = 500;
    m_data->m_nPrivateID = 0;
    m_dwDamagedTick = 0;
    m_dwBossNPCHurtTick = 0;
    m_data->DeleteAllAbnormalStatus();
    m_data->m_bMoveTrouble = false;
    m_data->m_dwPathFindFailCount = 0;
    m_vPosOnTick = FVector{};
    m_data->m_BossID = 0;
    SetInCombatMode(false);
    m_AvoidAgroWhenEnterWorld = false;
    m_bShowMovePosToGM = false;
    std::fill(std::begin(m_sNickName), std::end(m_sNickName), u'\0');
    m_nLaunchedSkills.store(0, std::memory_order_relaxed);
    m_bDoubleCastingMod = false;
    m_OnSummonStatBonus = false;
    m_bCanEscape = true;
    nCountAttacked = 0;
    dDPSBonusForBuilder = 1.0;
    m_InitializingSkillMod.store(0, std::memory_order_relaxed);
    m_nWeaponPAttackBonusByLevel = 0;
    m_nWeaponMAttackBonusByLevel = 0;
    m_hMinNewSetItemEnchantedEffect = 0;
    m_hMinNewSetItemEnchanted = 0;
    m_data->m_NeedUpdatePacketBitSet.Init(0x67, 0x10, reinterpret_cast<unsigned char*>(m_data->m_bNeedUpdatePacketBits));
    m_data->m_NeedUpdatePacketBitSet.SetAllBits();
    m_nDamageExceptAttackerId = 0;
    m_dExtraParam[0] = 0.0;
    m_dExtraParam[1] = 0.0;
    m_nChainAttackCount = 0;
    m_DefenceFatalBlowSuccessBonus.m_Diff = 0.0;
    m_DefenceFatalBlowSuccessBonus.m_Per = 1.0;
    m_lSaveSkillReuseDelay = 0;
}

void CCreature::OnCreateFakeNPC() {
    L2_GUARD_FUNCTION();

    m_data->m_nInventoryIndex = -1;
    m_inv.SetIndex(m_data->index, IsPC());

    m_nId = g_CreatureSmartIdManager.RegisterObject(
        // 5,
        SCLS_FAKE_NPC, static_cast<std::uint32_t>(m_data->index), this
    );
    CCreatureFactory::Inst().PublishIndexId(m_data->index, m_nId);
    m_data->m_nId = m_nId;

    for (int thread = 0; thread < CThreadManager::GetThreadCount(); ++thread) {
        L2_ADD_REF(this, ObjectRefType::ThreadLocalMap);
    }
    IncreaseTLRef();

    if (!CThreadManager::IsMainThread() && !CThreadManager::IsListenThread()) {
        m_pController = new CCreatureController();
        m_pController->SetActor(this);
    } else {
        m_pController = nullptr;
    }
#if defined(_WIN32)
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    m_LookNeighborSerializer.SetOwnerName(reinterpret_cast<const char16_t*>(GetName()));
#else
    GetName();
    m_LookNeighborSerializer.SetOwnerName(m_data->Name());
#endif
}

void CCreature::OnCreate() {
    L2_GUARD_FUNCTION();

    m_data->m_nInventoryIndex = -1;
    m_inv.SetIndex(m_data->index, IsPC());

    m_nId = g_CreatureSmartIdManager.RegisterObject(
        // 9,
        SCLS_CREATURE, static_cast<std::uint32_t>(m_data->index), this
    );
    CCreatureFactory::Inst().PublishIndexId(m_data->index, m_nId);
    m_data->m_nId = m_nId;

    for (int thread = 0; thread < CThreadManager::GetThreadCount(); ++thread) {
        L2_ADD_REF(this, ObjectRefType::ThreadLocalMap);
    }
    IncreaseTLRef();

    if (!CThreadManager::IsMainThread() && !CThreadManager::IsListenThread()) {
        m_pController = new CCreatureController();
        m_pController->SetActor(this);
    } else {
        m_pController = nullptr;
    }
#if defined(_WIN32)
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    m_LookNeighborSerializer.SetOwnerName(reinterpret_cast<const char16_t*>(GetName()));
#else
    GetName();
    m_LookNeighborSerializer.SetOwnerName(m_data->Name());
#endif
}

void CCreature::Delete() {
    L2_GUARD_FUNCTION();

    if (runtime::raw_atomic::IncrementI32(&m_nDeleteCount) != 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid CCreautre::Delete",
        //     L"Creature_common.cpp",
        //     0x1b5)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid CCreautre::Delete",
        //     L"Creature_common.cpp",
        //     0x1b5)
        L2_LOG_ERROR(
            Log,
            "Creature object %u deletion ignored: incremented delete counter is not 1; reporting the stack without "
            "repeating cleanup.",
            static_cast<unsigned>(m_nId)
        );
        runtime::process::ReportCallStack(LOG_ERROR, false);
        return;
    }

    DeleteAllItemsInInventory();
    SetDelete();
    HandleSerializedMessages();
    g_ptlCreatureMap->Remove(m_data->index, this);
    g_CreatureSmartIdManager.DeleteObject(m_nId);
    m_pContribute->AddTimer(IsBoss() ? 300000 : 20000, 0);
}

template <>
void CThreadLocalWorldMap2<120000, 600000, 16>::Write2(short x, short y, char z, int sharedIndex, CObject* object) {
    L2_GUARD_FUNCTION();
    const int threadCount = CThreadManager::GetThreadCount();
    for (int thread = 0; thread < threadCount; ++thread) {
        CTL_WorldMapNode2 node;
        const int instantMapIndex = object->GetInstantZoneIndex();
        node.idx = x;
        node.idy = y;
        node.idz = static_cast<std::int8_t>(z);
        node.index = sharedIndex;
        node.object = object;
        node.instantMapIndex = instantMapIndex;
        PipeBase::Write(thread, node);
    }
}

namespace CreatureCommonFunc {

double GetDispelProbability(
    const double baseProbability,
    const CSkillInfo* const dispelSkill,
    const CSkillInfo* const infectedSkill,
    const CCreatureSkillMod& skillMod,
    const int targetLevel
) {
    // Current L2Server 0x005d76bc.
    L2_GUARD_FUNCTION();

    const double dispelBonus =
        infectedSkill->m_eDebuff == DB_GOOD ? skillMod.m_dGoddBuffDispelBonusPer : skillMod.m_dBadBuffDispelBonusPer;
    const double levelTerm = static_cast<double>(dispelSkill->m_nMagicLevel * 2) -
        (static_cast<double>(infectedSkill->m_nMagicLevel) * 1.5 + static_cast<double>(targetLevel));
    return (static_cast<double>(infectedSkill->m_nAbnormalTime) / 120.0) * dispelBonus +
        static_cast<double>(runtime::Win64CvttDoubleToInt32(levelTerm)) + baseProbability;
}

void DoNothingSkillActors(int& creatureId) {
    L2_GUARD_FUNCTION();

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(creatureId);
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user != nullptr) {
        user->DoNothing();
    }
}

void BroadcastValidateLocation(CCreature& creature, CAbstractCarrier& carrier, int packetCode) {
    L2_GUARD_FUNCTION();

    const FVector carrierPosition = carrier.GetPos();
    const int heading = creature.m_data->GetDirByInt() & 0xffff;
    const FVector relativePosition = creature.GetRelPos();
    const int x = runtime::Win64CvttDoubleToInt32(relativePosition.x);
    const int y = runtime::Win64CvttDoubleToInt32(relativePosition.y);
    const int z = runtime::Win64CvttDoubleToInt32(relativePosition.z);

    if (creature.IsPC() && !creature.IsGMInvisible()) {
        g_ptlUserMap->BroadcastToNeighborExceptSelf(
            &creature, 0x5000, carrierPosition, "chdddddd", 0xfe, packetCode, static_cast<int>(creature.m_nId),
            static_cast<int>(carrier.m_nId), x, y, z, heading
        );
        return;
    }

    g_ptlUserMap->BroadcastToNeighbor(
        creature, 0x5000, carrierPosition, "chdddddd", 0xfe, packetCode, static_cast<int>(creature.m_nId),
        static_cast<int>(carrier.m_nId), x, y, z, heading
    );
}

bool IsNotDuplicated(CCreature* creature, std::vector<CSafePointer2<CCreature>>& creatures) {
    L2_GUARD_FUNCTION();
    const CSafePointer2<CCreature> candidate(creature);
    for (const CSafePointer2<CCreature>& existing : creatures) {
        if (existing.id == candidate.id && existing.index == candidate.index) {
            return false;
        }
    }
    return true;
}

bool IsNotDuplicated(CCreature* creature, CCreature::SkillTargetData& targetData) {
    return IsNotDuplicated(creature, targetData.c);
}

bool PushEffectsIfInTime(
    InfectedSkillType& infectedSkill,
    const int tick,
    std::vector<CSkillEffect*>& effects,
    std::vector<unsigned int>& sourceIds,
    std::vector<const CSkillInfo*>& skills,
    const int sourceId,
    const int level,
    int abnormalTime
) {
    // Current L2Server 0x005ddd40. The level argument is part of the
    // original ABI but the current body does not consume it.
    L2_GUARD_FUNCTION();
    static_cast<void>(level);

    CSkillInfo* const skillInfo = infectedSkill.pSkillInfo;
    if (abnormalTime == 0) {
        abnormalTime = skillInfo->m_nAbnormalTime;
    }
    if (skillInfo->m_nAbnormalTime != -1 && !CIOObject::IsInTime(infectedSkill.nStartTime, abnormalTime)) {
        return false;
    }

    for (CSkillEffect* const effect : skillInfo->m_TickEffects) {
        CSkillTickEffect* const tickEffect = effect->CastTickEffect();
        if (tickEffect == nullptr || tickEffect->nUnitTick == 0 || tick % tickEffect->nUnitTick != 0) {
            continue;
        }
        effects.push_back(tickEffect);
        sourceIds.push_back(static_cast<unsigned int>(sourceId));
        skills.push_back(skillInfo);
    }
    return true;
}

} // namespace CreatureCommonFunc

CAirShip* CCreature::GetMyAirShip() {
    L2_GUARD_FUNCTION();
    const unsigned int airShipId = GetMyAirShipID();
    if (airShipId == 0) {
        return nullptr;
    }

    CAirShip* const airShip = g_AirShipSmartIdManager.GetObjectW(airShipId);
    if (airShip == nullptr || !airShip->IsAirShip()) {
        return nullptr;
    }
    return airShip;
}
