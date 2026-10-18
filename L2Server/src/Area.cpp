#include "Area.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/Log.h"
#include "Shared/src/MemorySub.h"

#include "AreaDataParser.h"
#include "CConfig.h"
#include "CGeneralObject.h"
#include "CWorld.h"
#include "Creature.h"
#include "EventController.h"
#include "Fishing.h"
#include "MakerEvent.h"
#include "OlympiadStuff.h"
#include "PledgeGameSystem.h"
#include "Setting.h"
#include "SharedContainer.h"
#include "Skill.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/Exception.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"
#include "Shared/src/platform/Runtime.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

std::uint32_t totalAddAreaTime = 0;
std::uint32_t totalAddAreaTime1 = 0;
std::uint32_t totalAddAreaTime2 = 0;
std::uint32_t totalAddAreaTime3 = 0;
int totalADDAreaCallCount = 0;

// Area.cpp's source-local profiler. Current AddArea creates the exact
// pointer/tick pair at each of its four scopes. The matching PDB keeps the
// class name and unsigned-long counter type.
class MiniProfile final {
public:
    explicit MiniProfile(std::uint32_t* const total) noexcept : total_(total), start_(runtime::GetTickCount()) {}

    ~MiniProfile() { *total_ += runtime::GetTickCount() - start_; }

private:
    std::uint32_t* total_;
    std::uint32_t start_;
};


namespace {


bool UpdatesGeoField(AreaTypeEnum type) noexcept {
    switch (type) {
        case AREAT_BATTLE:
        case AREAT_FREEZE:
        case AREAT_PEACE:
        case AREAT_ONLY_PVE:
        case AREAT_SSQ:
        case AREAT_DRAGON_LAIR:
        case AREAT_NO_SKILL:
        case AREAT_SIEGE:
            return true;
        default:
            return false;
    }
}


} // namespace


CArea::CArea() :
    m_FieldType(GEO_FIELD_BATTLE_RESIDENCE),
    m_vAffectRace{},
    m_nMoveBonus(0),
    m_nDamageOnHP(0),
    m_nDamageOnMP(0),
    m_nHpRegenBonus(0),
    m_nMpRegenBonus(0),
    m_bIsOn(true),
    m_bSSQArea(false),
    m_bShowDangerZone(true),
    m_padding053{},
    m_nMessageNO(333),
    m_nEnteringMessageNO(114),
    m_nLeavingMessageNO(115),
    m_padding190{},
    m_pEventController(nullptr),
    m_nInitialDelay(0),
    m_nOnTime(30000),
    m_nOffTime(30000),
    m_nRandomTime(0),
    m_bMPCCAsParty(false),
    m_padding1b1{},
    m_nMapX(0),
    m_nMapY(0),
    m_padding1bc{},
    m_AreaType(AREAT_MAX),
    m_AreaTargetType(AREATT_ALL),
    mSkillActionType(AST_RANDOM1),
    m_nSkillProb(100),
    m_nUnitTick(9),
    m_nRestartTime(600),
    m_BlockedActionTable{},
    mExpPenaltyPer(1.0),
    mIsItemDrop(false),
    mHPMPRegenOnLevelUp(false),
    m_padding24a{},
    mRestartAllowedTime(0),
    m_InstantZoneKey{0, 0},
    m_EnterInzoneKey{0, 0},
    m_FlyMoveInfo{0, 0} {
    L2_GUARD_FUNCTION();
    m_vAffectRace[1] = true;
}

CArea::~CArea() {
    L2_GUARD_FUNCTION();
    if (m_pEventController != nullptr) {
        m_pEventController->Delete();
    }
}

FVector CArea::GetRandomRestartPoint(const RaceType race, const FVector position, const bool chao, const int pkCount) {
    // Current L2Server.exe 0x0047b834. The target uses the
    // inclusive floating-point range helper instead of rand()%size.
    if (m_vecRestartPoints.empty()) {
        return Singleton<CSetting>::inst().GetRestartPoint(race, position, chao, pkCount);
    }

    const int maximum = static_cast<int>(m_vecRestartPoints.size()) - 1;
    const int minimum = maximum < 0 ? maximum : 0;
    const int upper = maximum < 0 ? 0 : maximum;
    const int index =
        static_cast<int>(genrand64_real2() * static_cast<double>((upper - minimum) + 1) + static_cast<double>(minimum));
    return runtime::CheckedVectorAt(m_vecRestartPoints, static_cast<std::size_t>(index));
}

void CArea::SetAreaType(const AreaTypeEnum type) {
    // Current L2Server 0x0047b0b8.
    L2_GUARD_FUNCTION();
    m_AreaType = type;
    switch (type) {
        case AREAT_BATTLE:
            m_FieldType = GEO_FIELD_BATTLE_PVP;
            return;
        case AREAT_FREEZE:
            m_FieldType = GEO_FIELD_FREEZE;
            return;
        case AREAT_PEACE:
            m_FieldType = GEO_FIELD_PEACE;
            return;
        case AREAT_WATER:
            m_FieldType = GEO_FIELD_WATER;
            return;
        case AREAT_ONLY_PVE:
            m_FieldType = GEO_FIELD_ONLY_PVE;
            return;
        case AREAT_SWAMP:
        case AREAT_AIRPORT:
            return;
        case AREAT_MOTHER_TREE:
            m_FieldType = GEO_FIELD_STATUS_CHANGE;
            return;
        case AREAT_NO_RESTART:
            m_FieldType = GEO_FIELD_NO_RESTART;
            return;
        case AREAT_DAMAGE:
            m_FieldType = GEO_FIELD_INSTANT;
            m_nUnitTick = 9;
            return;
        case AREAT_POISON:
        case AREAT_INSTANT_SKILL:
            m_FieldType = GEO_FIELD_INSTANT;
            m_nUnitTick = 45;
            return;
        case AREAT_TELEPORT:
        case AREAT_ENTER_INZONE:
        case AREAT_LEAVE_INZONE:
            m_FieldType = GEO_FIELD_INSTANT;
            m_nUnitTick = 5;
            return;
        case AREAT_SSQ:
            m_FieldType = GEO_FIELD_SSQ;
            return;
        case AREAT_FLYMOVE:
            m_FieldType = GEO_FIELD_INSTANT;
            m_nUnitTick = 1;
            return;
        case AREAT_SHUTTLE:
            m_FieldType = GEO_FIELD_SHUTTLE;
            return;
        case AREAT_DRAGON_LAIR:
            m_FieldType = GEO_FIELD_DRAGON_LAIR;
            return;
        case AREAT_NO_SKILL:
            m_FieldType = GEO_FIELD_NO_SKILL;
            return;
        case AREAT_SIEGE:
            m_FieldType = GEO_FIELD_BATTLE_RESIDENCE;
            return;
        case AREAT_FISHING:
            m_FieldType = GEO_FIELD_INSTANT;
            return;
        case AREAT_TRAINING_ZONE:
            m_FieldType = GEO_FIELD_TRAINING_ZONE;
            return;
        default:
            return;
    }
}

void CArea::SetAreaTargetType(const AreaTargetTypeEnum type) { m_AreaTargetType = type; }

void CArea::SetSkillActionType(const AreaSkillActionType type) { mSkillActionType = type; }

void CArea::SetUnitTick(const int unitTick) { m_nUnitTick = unitTick; }

void CArea::SetSkillProb(const int probability) {
    // Current L2Server 0x0047b2d0 performs an unsigned range check.
    L2_GUARD_FUNCTION();
    if (static_cast<std::uint32_t>(probability) < 101U) {
        m_nSkillProb = probability;
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"invalid skill_prob [%d]",
    //     probability)
    // L2_LOG_ERROR(Log,
    //     L"invalid skill_prob [%d]",
    //     probability)
    L2_LOG_ERROR(
        Log, u"[areas] Invalid skill_prob %d for territory \"%ls\"; expected 0..100. The setting is unchanged.", probability,
        m_FieldTerritory.sName
    );
}

void CArea::AddSkillInfo(CSkillInfo* const skillInfo) {
    // Current L2Server 0x0047e064.
    L2_GUARD_FUNCTION();
    if (skillInfo != nullptr) {
        mSkillInfoList.push_back(skillInfo);
    }
}

void CArea::AddSkillInfo(std::vector<std::int64_t>& skillKeys) {
    // Current L2Server 0x0047e118.
    L2_GUARD_FUNCTION();
    for (const std::int64_t packed : skillKeys) {
        const std::uint64_t bits = static_cast<std::uint64_t>(packed);
        const CSkillKey key(
            static_cast<int>(static_cast<std::uint32_t>(bits >> 32U)),
            static_cast<int>(static_cast<std::uint16_t>(bits >> 16U)), static_cast<int>(static_cast<std::uint16_t>(bits))
        );
        const CSkillInfo* const skillInfo = g_SkillDB.GetSkillInfo(key, nullptr);
        if (skillInfo != nullptr) {
            mSkillInfoList.push_back(const_cast<CSkillInfo*>(skillInfo));
        }
    }
}

L2_DEFINE_OBJECT_CLASS_ID(CArea)

L2_DEFINE_OBJECT_CLASS_NAME(CArea, m_pClassName_CArea)

L2_DEFINE_OBJECT_ALLOCATOR(CArea, __GetClassName())

L2_DEFINE_OBJECT_NEW(CArea)

L2_DEFINE_OBJECT_TYPED_RELEASE(CArea)

bool CArea::IsOn(int worldId) {
    L2_GUARD_FUNCTION();
    if (m_InstantZoneKey.typeId == 0 || worldId == 0) {
        return m_bIsOn;
    }

    CGeneralObject* const object = CGeneralObject::GetObjectW(static_cast<std::uint32_t>(worldId));
    CInstantZone* zone = nullptr;
    if (object != nullptr && object->IsInZone()) {
        zone = object->CastInZone();
    }
    return zone != nullptr ? zone->GetAreaIsOn(this) : m_bIsOn;
}

void CArea::MakeEventController(int eventId) {
    L2_GUARD_FUNCTION();
    m_pEventController = new CEventController;
    m_pEventController->EstablishEventController(eventId, m_FieldTerritory.GetCenterPos());
}

bool CArea::SetBannedTerritoryOnOff(int index, bool on) {
    L2_GUARD_FUNCTION();
    if (m_BannedFieldTerritories.empty() || index < 0 ||
        static_cast<std::size_t>(index) >= m_BannedFieldTerritories.size()) {
        return false;
    }
    m_bIsBannedTerritoryOn[static_cast<std::size_t>(index)] = on;
    return true;
}

bool CArea::IsBannedTerritoryOn(int index) {
    L2_GUARD_FUNCTION();
    if (m_BannedFieldTerritories.empty() || index < 0 ||
        static_cast<std::size_t>(index) >= m_BannedFieldTerritories.size()) {
        return false;
    }
    return m_bIsBannedTerritoryOn[static_cast<std::size_t>(index)];
}

bool CArea::IsBannedPoint(const FVector& position, bool includeZ) {
    L2_GUARD_FUNCTION();
    for (std::size_t index = 0; index < m_BannedFieldTerritories.size(); ++index) {
        CTerritory* const territory = runtime::CheckedVectorAt(m_BannedFieldTerritories, index);
        if (territory != nullptr && territory->GetTerritoryPointSize() > 0 && territory->IsInside(position, includeZ) &&
            IsBannedTerritoryOn(static_cast<int>(index))) {
            return true;
        }
    }
    return false;
}

void CArea::SetShowDangerZone(const bool enabled) {
    // Current L2Server 0x0047b4dc.
    L2_GUARD_FUNCTION();
    m_bShowDangerZone = enabled;
}

void CArea::SetOnOff(bool on) {
    L2_GUARD_FUNCTION();
    class TLMFunc final : public CTLMFunc {
    public:
        bool CanDo(CObject* object, double) override {
            if (!object->IsPC() || (!object->IsDeleting() && object->m_Deleted == 0)) {
                User* const user = object->CastUser();
                if (user != nullptr) {
                    user->SendETCBuffStatusInfo();
                }
            }
            return true;
        }
    };

    m_bIsOn = on;
    if (UpdatesGeoField(m_AreaType)) {
        CWorld::Inst().GetGeo()->SetField(&m_FieldTerritory, m_FieldType, on);
    }
    if (m_pEventController != nullptr) {
        m_pEventController->SetOnOff(on);
    }

    CTLMFilterTerritory filter(0, &m_FieldTerritory);
    TLMFunc update;
    FOR_ALL_NEIGHBOR_USER_IN_TERRITORY(&filter, &update);
    AreaStatusMap::inst().SetEvent(m_FieldTerritory.sName.c_str(), 0);
}

void CArea::TimerExpired(int id) {
    static_cast<void>(id);
    L2_GUARD_FUNCTION();

    const bool on = IsOn(0);
    int randomDelay = 0;
    if (m_nRandomTime > 0) {
        randomDelay = static_cast<int>(genrand64_real2() * static_cast<double>(m_nRandomTime) + 1.0);
    }
    const int delay = on ? m_nOnTime : m_nOffTime;
    SetOnOff(!on);
    AddTimer(static_cast<std::int64_t>(delay + randomDelay), 0);
}

void CArea::Process(CCreature* const creature) {
    // Current L2Server 0x0047c7e8.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return;
    }

    switch (m_AreaTargetType) {
        case AREATT_ALL:
            break;
        case AREATT_PC:
            if (!creature->IsPCOrSummon()) {
                return;
            }
            break;
        case AREATT_NPC:
            if (creature->IsSummonOrPet() || !creature->IsNPC()) {
                return;
            }
            break;
        case AREATT_ONLY_PC:
            if (!creature->IsPC()) {
                return;
            }
            break;
        default:
            return;
    }

    const auto passesSkillProbability = [this]() {
        return static_cast<int>(genrand64_real2() * 100.0 + 1.0) <= m_nSkillProb;
    };

    switch (m_AreaType) {
        case AREAT_INSTANT_SKILL:
            if (mSkillActionType == AST_SERIAL) {
                for (CSkillInfo* const skill : mSkillInfoList) {
                    if (passesSkillProbability()) {
                        skill->ActivateSkillEffects(nullptr, creature, CSkillInfo::EAT_SUCCESS, nullptr, 0.0);
                    }
                }
            } else if (mSkillActionType == AST_RANDOM1 && !mSkillInfoList.empty() && passesSkillProbability()) {
                CSkillInfo* const skill =
                    runtime::CheckedVectorAt(mSkillInfoList, static_cast<std::size_t>(::rand()) % mSkillInfoList.size());
                skill->ActivateSkillEffects(nullptr, creature, CSkillInfo::EAT_SUCCESS, nullptr, 0.0);
            }
            return;

        case AREAT_SWAMP:
            creature->m_AreaMod.m_dSpeedModByArea = static_cast<double>(m_nMoveBonus + 100) * 0.01;
            if (creature->m_AreaMod.m_dSpeedModByArea < 0.0) {
                creature->m_AreaMod.m_dSpeedModByArea = 0.0;
            }
            creature->m_AreaMod.m_bIsInSwamp = true;
            return;

        case AREAT_MOTHER_TREE:
            creature->m_AreaMod.m_dHpRegenAreaBonus = static_cast<double>(m_nHpRegenBonus);
            creature->m_AreaMod.m_dMpRegenAreaBonus = static_cast<double>(m_nMpRegenBonus);
            creature->m_AreaMod.m_nLeavingMessageNO = m_nLeavingMessageNO;
            return;

        case AREAT_DAMAGE: {
            creature->m_data->m_LastAttackerServerID = 0;
            const double damage = creature->GotDamageByEnvironment(
                static_cast<double>(m_nDamageOnHP) * creature->m_SkillMod.m_Mods[0x19].m_Per +
                    creature->m_SkillMod.m_Mods[0x19].m_Diff,
                true, DAMAGE_AREA, true
            );
            creature->IncreaseMP_diff(static_cast<double>(-m_nDamageOnMP), 4, 0, false);
            if (creature->IsPC()) {
                static_cast<User*>(creature)->GetSocket()->SendSystemMessage_d(m_nMessageNO, static_cast<int>(damage));
            }
            return;
        }

        case AREAT_POISON:
            if (mSkillActionType == AST_SERIAL) {
                for (CSkillInfo* const skill : mSkillInfoList) {
                    if (passesSkillProbability()) {
                        creature->InfectedSkillBy(nullptr, skill, nullptr, 0);
                    }
                }
            } else if (mSkillActionType == AST_RANDOM1 && !mSkillInfoList.empty()) {
                CSkillInfo* const skill =
                    runtime::CheckedVectorAt(mSkillInfoList, static_cast<std::size_t>(::rand()) % mSkillInfoList.size());
                if (passesSkillProbability()) {
                    creature->InfectedSkillBy(nullptr, skill, nullptr, 0);
                }
            }
            creature->m_AreaMod.m_bIsInPoison = true;
            return;

        case AREAT_TELEPORT: {
            FVector destination{};
            if (m_vecTeleportPoints.empty()) {
                destination = m_TeleportTerritory.PickRandomPos(false);
            } else {
                const std::size_t index =
                    static_cast<std::size_t>(genrand64_real2() * static_cast<double>(m_vecTeleportPoints.size()));
                destination = runtime::CheckedVectorAt(m_vecTeleportPoints, index);
            }
            creature->StopMove();
            creature->TeleportToLocation(
                static_cast<int>(destination.x), static_cast<int>(destination.y), static_cast<int>(destination.z),
                creature->GetInZoneID(), 0x10000, TeleportSourceType::Area, nullptr, 0
            );
            return;
        }

        case AREAT_ENTER_INZONE: {
            if (!creature->IsPC()) {
                return;
            }
            User* const user = creature->CastUser();
            if (user == nullptr || m_EnterInzoneKey.typeId == 0) {
                return;
            }
            g_InstantZoneMgr.EnterInstantZone(
                user, IZET_INDIVIDUAL, m_EnterInzoneKey.typeId, m_EnterInzoneKey.clusterId, 0, false
            );
            return;
        }

        case AREAT_LEAVE_INZONE: {
            if (!creature->IsPC()) {
                return;
            }
            User* const user = creature->CastUser();
            if (user == nullptr || user->IsInRealWorld()) {
                return;
            }
            CInstantZone* const zone = user->GetInstantZone();
            if (zone != nullptr) {
                g_InstantZoneMgr.LeaveInstantZone(user, zone, 0, 0, 0);
            }
            return;
        }

        case AREAT_FLYMOVE: {
            if (!creature->IsPC()) {
                return;
            }
            User* const user = creature->CastUser();
            if (user->HaveSummon() || user->HavePet()) {
                ++user->m_nAreaMessageTick;
                if (user->m_nAreaMessageTick % 7 == 1) {
                    user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_USE_SAYUNE_WITH_PET_OR_SERVITOR_SUMMONED);
                }
                return;
            }
            if (user->GetPrivateStoreType() != 0 || user->IsRiding()) {
                return;
            }
            if (!User::IsInFifthClassGroup(static_cast<ClassType>(user->GetClass())) &&
                !User::IsInSixthClassGroup(static_cast<ClassType>(user->GetClass()))) {
                return;
            }
            if (!user->CanStartFlyMove() || user->m_bFlyMoving) {
                return;
            }

            CFlyMovePathNode* const node = CFlyMoveManager::Inst().GetPathNode(m_FlyMoveInfo.path_id, m_FlyMoveInfo.node_id);
            if (node == nullptr) {
                const int z = static_cast<int>(user->GetPos().z);
                const int y = static_cast<int>(user->GetPos().y);
                const int x = static_cast<int>(user->GetPos().x);
                // Log.Add(
                //     LOG_ERROR,
                //     L"no node for [%d/%d] at [%d/%d/%d]",
                //     m_FlyMoveInfo.path_id,
                //     m_FlyMoveInfo.node_id,
                //     x,
                //     y,
                //     z)
                // L2_LOG_ERROR(Log,
                //     L"no node for [%d/%d] at [%d/%d/%d]",
                //     m_FlyMoveInfo.path_id,
                //     m_FlyMoveInfo.node_id,
                //     x,
                //     y,
                //     z)
                L2_LOG_ERROR(
                    Log, u"[areas] Cannot start Sayune movement: path %d has no node %d; player position (%d, %d, %d).",
                    m_FlyMoveInfo.path_id, m_FlyMoveInfo.node_id, x, y, z
                );
                return;
            }

            user->m_nPrevNode = m_FlyMoveInfo.node_id;
            user->m_FlyMoveInfo = m_FlyMoveInfo;
            user->GetSocket()->Send("ch", 0xfe, 0x110);
            return;
        }

        case AREAT_SHUTTLE:
            static_cast<void>(creature->IsPC());
            return;

        case AREAT_FISHING: {
            if (!creature->IsPC()) {
                return;
            }
            User* const user = creature->CastUser();
            CFishingController* const fishing = user->m_pFishingController;
            if (fishing != nullptr && fishing->m_FishingState == FISHINGS_NONE && fishing->m_bIsAutoFishing != true) {
                user->GetSocket()->Send("chc", 0xfe, 0x17b, 1);
            }
            return;
        }

        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"process which area: %d",
            //     static_cast<int>(m_AreaType))
            // L2_LOG_ERROR(Log,
            //     L"process which area: %d",
            //     static_cast<int>(m_AreaType))
            L2_LOG_ERROR(
                Log, u"[areas] No processing is defined for area type %d in territory \"%ls\"; action skipped.",
                static_cast<int>(m_AreaType), m_FieldTerritory.sName
            );
            return;
    }
}

bool CArea::CheckAndProcess(CCreature& creature, const FVector& position) {
    // Current L2Server 0x0047d24c.
    L2_GUARD_FUNCTION();
    if (creature.m_nTickCount % m_nUnitTick == 0 && IsOn(creature.GetInZoneID()) &&
        m_FieldTerritory.IsInside(position, true) && !IsBannedPoint(position, true)) {
        Process(&creature);
        return true;
    }
    return false;
}

CAreaDB::CAreaDB() : m_vArea{}, m_vSortedInstantArea{}, m_bLoadForChecker(false), m_padding3fc29{} {}

CAreaDB::~CAreaDB() = default;

const CAreaDB::AreaList& CAreaDB::GetAreaListByType(int mapX, int mapY, AreaTypeEnum type) const {
    return m_vArea[mapX][mapY][static_cast<int>(type)];
}

const CAreaDB::AreaList& CAreaDB::GetSortedInstantAreaList(int mapX, int mapY) const {
    return m_vSortedInstantArea[mapX][mapY];
}

CArea* CAreaDB::FindAreaRough(int worldId, const FVector& position, AreaTypeEnum type) {
    const int mapX = (static_cast<int>(position.x) + 0x50000) >> 15;
    const int mapY = (static_cast<int>(position.y) + 0x40000) >> 15;
    if (CWorld::Inst().GetGeo()->GetZone(mapX + 10, mapY + 10) == nullptr) {
        return nullptr;
    }

    for (CArea* const area : GetAreaListByType(mapX, mapY, type)) {
        if (area->IsOn(worldId) && area->m_FieldTerritory.IsInsideRough(position, true)) {
            return area;
        }
    }
    return nullptr;
}

CArea* CAreaDB::FindAllArea(const FVector& position, AreaTypeEnum type) {
    // Current L2Server 0x0047bff4. This ignores the area's
    // per-world on/off state. Callers that need it perform IsOn themselves.
    const int mapX = (static_cast<int>(position.x) + 0x50000) >> 15;
    const int mapY = (static_cast<int>(position.y) + 0x40000) >> 15;
    if (CWorld::Inst().GetGeo()->GetZone(mapX + 10, mapY + 10) == nullptr) {
        return nullptr;
    }

    for (CArea* const area : GetAreaListByType(mapX, mapY, type)) {
        if (area->m_FieldTerritory.IsInside(position, true) && !area->IsBannedPoint(position, true)) {
            return area;
        }
    }
    return nullptr;
}

CAreaDB::AreaList CAreaDB::FindAllAreaList(const FVector& position) {
    // Current L2Server 0x0047d8a0. This ignores each area's
    // per-world on/off state.
    const int mapX = (static_cast<int>(position.x) + 0x50000) >> 15;
    const int mapY = (static_cast<int>(position.y) + 0x40000) >> 15;
    if (CWorld::Inst().GetGeo()->GetZone(mapX + 10, mapY + 10) == nullptr) {
        return {};
    }

    AreaList candidates;
    for (std::size_t type = 0; type < kAreaTypeCount; ++type) {
        const AreaList& source = m_vArea[mapX][mapY][type];
        candidates.insert(candidates.end(), source.begin(), source.end());
    }

    AreaList matches;
    for (CArea* const area : candidates) {
        if (area->m_FieldTerritory.IsInside(position, true) && !area->IsBannedPoint(position, true)) {
            matches.push_back(area);
        }
    }
    return matches;
}

CAreaDB::AreaList CAreaDB::FindAllAreaList(const FVector& position, const AreaTypeEnum type) {
    // Current L2Server 0x0047e230. This ignores each area's
    // per-world on/off state.
    const int mapX = (static_cast<int>(position.x) + 0x50000) >> 15;
    const int mapY = (static_cast<int>(position.y) + 0x40000) >> 15;
    if (CWorld::Inst().GetGeo()->GetZone(mapX + 10, mapY + 10) == nullptr) {
        return {};
    }

    AreaList candidates;
    GatherAllAreas(mapX, mapY, type, candidates);

    AreaList matches;
    for (CArea* const area : candidates) {
        if (area->m_FieldTerritory.IsInside(position, true) && !area->IsBannedPoint(position, true)) {
            matches.push_back(area);
        }
    }
    return matches;
}

bool CAreaDB::IsUnderWater(const FVector& position, int& waterSurface, int worldId) {
    CArea* const area = FindAreaRough(worldId, position, AREAT_WATER);
    if (area == nullptr) {
        return false;
    }
    waterSurface = area->m_FieldTerritory.GetTop();
    return static_cast<int>(position.z) <= waterSurface;
}

CArea* CAreaDB::FindArea(int worldId, const FVector& position, AreaTypeEnum type, bool includeZ) {
    const int mapX = (static_cast<int>(position.x) + 0x50000) >> 15;
    const int mapY = (static_cast<int>(position.y) + 0x40000) >> 15;
    if (CWorld::Inst().GetGeo()->GetZone(mapX + 10, mapY + 10) == nullptr) {
        return nullptr;
    }

    for (CArea* const area : GetAreaListByType(mapX, mapY, type)) {
        if (area->IsOn(worldId) && area->m_FieldTerritory.IsInside(position, includeZ) &&
            !area->IsBannedPoint(position, includeZ)) {
            return area;
        }
    }
    return nullptr;
}

CArea* CAreaDB::FindArea(const char16_t* name) {
    L2_GUARD_FUNCTION();
    const auto position = m_AreaMap.find(name);
    if (position != m_AreaMap.end()) {
        return position->second;
    }

    const std::wstring hostAreaName = name != nullptr ? Utf16ToWide(name) : std::wstring{};
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] undefined (or unregistered) Area[%s]",
    //     __WFILE__,
    //     0x4cb,
    //     name != nullptr
    //         ? hostAreaName.c_str()
    //         : nullptr)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] undefined (or unregistered) Area[%s]",
    //     __WFILE__,
    //     0x4cb,
    //     name != nullptr
    //         ? hostAreaName.c_str()
    //         : nullptr)
    L2_LOG_ERROR(
        Log, u"[areas] No registered area named \"%ls\"; lookup failed.", name != nullptr ? hostAreaName.c_str() : L"<null>"
    );
    runtime::process::ReportCallStack(2, false);
    return nullptr;
}

bool CAreaDB::IsFishingZone(const FVector& position, const int worldId) {
    return FindArea(worldId, position, AREAT_FISHING, true) != nullptr;
}

bool CAreaDB::IsFlyMoveZone(const FVector& position, const int worldId) {
    // Current L2Server 0x0047c22c.
    return FindArea(worldId, position, AREAT_FLYMOVE, true) != nullptr;
}

bool CAreaDB::IsWaterZone(const FVector& position, int& waterSurface, int worldId) {
    CArea* const area = FindArea(worldId, position, AREAT_WATER, true);
    if (area == nullptr) {
        return false;
    }

    const int ground = CWorld::Inst().GetGeo()->FindGround(
        static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z)
    );
    const int surface = area->m_FieldTerritory.GetTop();
    if (ground > surface) {
        return false;
    }
    waterSurface = surface;
    return true;
}

void CAreaDB::ProcessInstantAreas(CCreature* const creature, const FVector& position) {
    // Current L2Server 0x0047d360.
    L2_GUARD_FUNCTION();
    const int mapX = (static_cast<int>(position.x) + 0x50000) >> 15;
    const int mapY = (static_cast<int>(position.y) + 0x40000) >> 15;
    if (CWorld::Inst().GetGeo()->GetZone(mapX + 10, mapY + 10) == nullptr) {
        return;
    }

    const AreaList& areas = GetSortedInstantAreaList(mapX, mapY);
    for (CArea* const area : areas) {
        InstantZoneKey key{0, 0};
        CInstantZone* const zone = creature->GetInstantZone();
        if (zone != nullptr) {
            key = zone->GetKey();
        }

        const InstantZoneKey& required = area->m_InstantZoneKey;
        const bool matches = required.clusterId == 0 ? required.typeId == key.typeId
                                                     : required.typeId == key.typeId && required.clusterId == key.clusterId;
        if (matches) {
            area->CheckAndProcess(*creature, position);
        }
    }
}

void CAreaDB::OnSunrise(bool) {
    // Current L2Server 0x0047b684 is an empty guarded hook.
    L2_GUARD_FUNCTION();
}

void CAreaDB::StartAreaTimer() {
    // Current L2Server 0x0047c374. The target trusts every
    // published map value and excludes instant-zone-bound areas.
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_AreaMap) {
        CArea* const area = entry.second;
        if (area->m_nInitialDelay > 0 && area->m_InstantZoneKey.typeId == 0) {
            area->AddTimer(area->m_nInitialDelay, 0);
        }
    }
}

void CAreaDB::GatherAllAreas(const int mapX, const int mapY, const AreaTypeEnum type, AreaList& areas) {
    // Current L2Server 0x0047da40.
    if (type == AREAT_MAX) {
        constexpr std::array<AreaTypeEnum, 8> kInstantTypes{AREAT_DAMAGE, AREAT_POISON, AREAT_TELEPORT, AREAT_INSTANT_SKILL,
            AREAT_ENTER_INZONE, AREAT_LEAVE_INZONE, AREAT_FLYMOVE, AREAT_FISHING};
        std::size_t total = 0;
        for (const AreaTypeEnum instantType : kInstantTypes) {
            total += m_vArea[mapX][mapY][instantType].size();
        }
        if (total != 0) {
            areas.reserve(total);
            for (const AreaTypeEnum instantType : kInstantTypes) {
                const AreaList& source = m_vArea[mapX][mapY][instantType];
                areas.insert(areas.end(), source.begin(), source.end());
            }
        }
        return;
    }
    if (static_cast<std::uint32_t>(type) < kAreaTypeCount) {
        areas = m_vArea[mapX][mapY][type];
    }
}

void CAreaDB::SortInstantArea() {
    // Current L2Server 0x0047db70.
    L2_GUARD_FUNCTION();
    for (int mapX = 0; mapX < static_cast<int>(kMapXCount); ++mapX) {
        for (int mapY = 0; mapY < static_cast<int>(kMapYCount); ++mapY) {
            AreaList allAreas;
            GatherAllAreas(mapX, mapY, AREAT_MAX, allAreas);
            if (allAreas.empty()) {
                continue;
            }

            int currentTick = allAreas.front()->m_nUnitTick;
            for (CArea* const area : allAreas) {
                if (area->m_nUnitTick < currentTick) {
                    currentTick = area->m_nUnitTick;
                }
            }

            int appended = 0;
            const int areaCount = static_cast<int>(allAreas.size());
            do {
                int nextTick = 0;
                for (CArea* const area : allAreas) {
                    const int unitTick = area->m_nUnitTick;
                    if (unitTick == currentTick) {
                        ++appended;
                        m_vSortedInstantArea[mapX][mapY].push_back(area);
                    } else if (currentTick < unitTick && (nextTick == 0 || unitTick < nextTick)) {
                        nextTick = unitTick;
                    }
                }
                if (nextTick > 0 && nextTick % currentTick != 0) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"invalid unittick(current %d, next %d)! at map (%d, %d)",
                    //     currentTick,
                    //     nextTick,
                    //     mapX + 10,
                    //     mapY + 10)
                    // L2_LOG_ERROR(Log,
                    //     L"invalid unittick(current %d, next %d)! at map (%d, %d)",
                    //     currentTick,
                    //     nextTick,
                    //     mapX + 10,
                    //     mapY + 10)
                    L2_LOG_ERROR(
                        Log,
                        u"[areas] Unit tick %d does not divide the next tick %d on map (%d, %d); area sorting continues.",
                        currentTick, nextTick, mapX + 10, mapY + 10
                    );
                }
                currentTick = nextTick;
            } while (appended < areaCount);
        }
    }
}

void CAreaDB::PostLoad() {
    // Current L2Server 0x0047de04.
    L2_GUARD_FUNCTION();
    for (int mapX = 0; mapX < static_cast<int>(kMapXCount); ++mapX) {
        for (int mapY = 0; mapY < static_cast<int>(kMapYCount); ++mapY) {
            AreaList enterAreas;
            GatherAllAreas(mapX, mapY, AREAT_ENTER_INZONE, enterAreas);
            for (CArea* const area : enterAreas) {
                if (area != nullptr && !g_InstantZoneDB.CheckAreaEnterInzone(area->m_EnterInzoneKey.typeId)) {
                    // const std::wstring name = target_utf16::ToHostCodeUnits(
                    //     area->m_FieldTerritory.sName.c_str())
                    const std::wstring name = Utf16ToWide(area->m_FieldTerritory.sName.c_str());
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"invalid inzone[%d] End Pos Type for area[%s]. must be nomination",
                    //     area->m_EnterInzoneKey.typeId,
                    //     name.c_str())
                    // L2_LOG_ERROR(Log,
                    //     L"invalid inzone[%d] End Pos Type for area[%s]. must be nomination",
                    //     area->m_EnterInzoneKey.typeId,
                    //     name.c_str())
                    L2_LOG_ERROR(
                        Log,
                        u"[areas] Instance zone %d referenced by entrance area \"%ls\" is unavailable or has invalid exit "
                        u"settings; both end-position types must be nomination.",
                        area->m_EnterInzoneKey.typeId, name.c_str()
                    );
                }
            }

            AreaList leaveAreas;
            GatherAllAreas(mapX, mapY, AREAT_LEAVE_INZONE, leaveAreas);
            for (CArea* const area : leaveAreas) {
                if (area != nullptr && area->m_InstantZoneKey.typeId == 0) {
                    // const std::wstring name = target_utf16::ToHostCodeUnits(
                    //     area->m_FieldTerritory.sName.c_str())
                    const std::wstring name = Utf16ToWide(area->m_FieldTerritory.sName.c_str());
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[inzone_leve] type must have instantzone_id for area [%s]",
                    //     name.c_str())
                    // L2_LOG_ERROR(Log,
                    //     L"[inzone_leve] type must have instantzone_id for area [%s]",
                    //     name.c_str())
                    L2_LOG_ERROR(
                        Log, u"[areas] Exit area \"%ls\" has no instantzone_id; post-load validation continues.",
                        name.c_str()
                    );
                }
            }
        }
    }
}

void CAreaDB::LoadArea() {
    // Current L2Server 0x0047e3ac.
#if defined(_WIN32)
    constexpr const wchar_t* kAreaPath = L"..\\Script\\areadata.txt";
#else
    constexpr const wchar_t* kAreaPath = L"../script/areadata.txt";
#endif
    CSecureStream input(kAreaPath, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from %s file failed",
        //     L"Area.cpp",
        //     0x5aa,
        //     kAreaPath)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from %s file failed",
        //     L"Area.cpp",
        //     0x5aa,
        //     kAreaPath)
        L2_LOG_ERROR(Log, "[areas] Cannot read areadata.txt from the configured script directory; area loading stops.");
        return;
    }

    CParserForArea parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForArea::create failed",
        //     L"Area.cpp",
        //     0x5b3)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForArea::create failed",
        //     L"Area.cpp",
        //     0x5b3)
        L2_LOG_ERROR(Log, "[areas] Cannot initialize the areadata.txt parser; area loading stops.");
        return;
    }
    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing %s file failed after line[%d]",
        //     L"Area.cpp",
        //     0x5c0,
        //     kAreaPath,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing %s file failed after line[%d]",
        //     L"Area.cpp",
        //     0x5c0,
        //     kAreaPath,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "[areas] Cannot finish parsing areadata.txt at line %d; any areas already registered remain in memory.",
            parser.m_lexer.line
        );
    }
}

void CAreaDB::LoadAreaForChecker() {
    m_bLoadForChecker = true;
    LoadArea();
    m_bLoadForChecker = false;
}

void CAreaDB::AddArea(CArea* const area) {
    // Current L2Server 0x0047ebf4.
    L2_GUARD_FUNCTION();
    MiniProfile totalProfile(&totalAddAreaTime);
    totalADDAreaCallCount = runtime::AddInt32Wrapping(totalADDAreaCallCount, 1);
    if (area == nullptr || area->m_FieldTerritory.GetTerritoryPointSize() < 1) {
        return;
    }

    // const std::wstring areaName = target_utf16::ToHostCodeUnits(
    //     area->m_FieldTerritory.sName.c_str())
    const std::wstring areaName = Utf16ToWide(area->m_FieldTerritory.sName.c_str());
    for (std::size_t index = 0; index < area->m_BannedFieldTerritories.size(); ++index) {
        CTerritory* const banned = runtime::CheckedVectorAt(area->m_BannedFieldTerritories, index);
        if (banned != nullptr && !area->m_FieldTerritory.IsInside(*banned, false)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Banned field territory [%s][%d] is out of field territory!",
            //     areaName.c_str(),
            //     static_cast<int>(index))
            // L2_LOG_ERROR(Log,
            //     L"Banned field territory [%s][%d] is out of field territory!",
            //     areaName.c_str(),
            //     static_cast<int>(index))
            L2_LOG_ERROR(
                Log, u"[areas] Cannot register area \"%ls\": excluded territory at index %d extends beyond its boundary.",
                areaName.c_str(), static_cast<int>(index)
            );
            return;
        }
    }
    if (area->m_BannedFieldTerritories.size() != area->m_bIsBannedTerritoryOn.size()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Banned field territory count [%s] is different from banned field flag count!",
        //     areaName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"Banned field territory count [%s] is different from banned field flag count!",
        //     areaName.c_str())
        L2_LOG_ERROR(
            Log, u"[areas] Cannot register area \"%ls\": excluded territory count %zu differs from enabled-flag count %zu.",
            areaName.c_str(), area->m_BannedFieldTerritories.size(), area->m_bIsBannedTerritoryOn.size()
        );
        return;
    }

    const FVector center = area->m_FieldTerritory.GetCenterPos();
    const int zoneX = ((static_cast<int>(center.x) + 0x50000) >> 15) + 10;
    const int zoneY = ((static_cast<int>(center.y) + 0x40000) >> 15) + 10;
    if (zoneX != area->m_nMapX || zoneY != area->m_nMapY) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Territory [%s] is out of intended map! intended map[%d, %d], current map[%d, %d]",
        //     areaName.c_str(),
        //     area->m_nMapX,
        //     area->m_nMapY,
        //     zoneX,
        //     zoneY)
        // L2_LOG_ERROR(Log,
        //     L"Territory [%s] is out of intended map! intended map[%d, %d], current map[%d, %d]",
        //     areaName.c_str(),
        //     area->m_nMapX,
        //     area->m_nMapY,
        //     zoneX,
        //     zoneY)
        L2_LOG_ERROR(
            Log,
            u"[areas] Cannot register area \"%ls\": configured map (%d, %d) differs from map (%d, %d) computed from its "
            u"center.",
            areaName.c_str(), area->m_nMapX, area->m_nMapY, zoneX, zoneY
        );
        return;
    }

    {
        MiniProfile geoProfile(&totalAddAreaTime1);
        CGeoData* const geo = CWorld::Inst().GetGeo();
        if (geo->GetZone(zoneX, zoneY) == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Cannot add area[%s]. no geodata pos(%d, %d, %d).",
            //     areaName.c_str(),
            //     static_cast<int>(center.x),
            //     static_cast<int>(center.y),
            //     static_cast<int>(center.z))
            // L2_LOG_ERROR(Log,
            //     L"Cannot add area[%s]. no geodata pos(%d, %d, %d).",
            //     areaName.c_str(),
            //     static_cast<int>(center.x),
            //     static_cast<int>(center.y),
            //     static_cast<int>(center.z))
            L2_LOG_ERROR(
                Log, u"[areas] Cannot register area \"%ls\": no geodata zone for its center (%d, %d, %d).", areaName.c_str(),
                static_cast<int>(center.x), static_cast<int>(center.y), static_cast<int>(center.z)
            );
            return;
        }

        switch (area->m_AreaType) {
            case AREAT_BATTLE:
            case AREAT_FREEZE:
            case AREAT_PEACE:
            case AREAT_ONLY_PVE:
            case AREAT_SSQ:
            case AREAT_DRAGON_LAIR:
            case AREAT_NO_SKILL:
            case AREAT_SIEGE: {
                MiniProfile dynamicFieldProfile(&totalAddAreaTime2);
                if (area->IsOn(0)) {
                    geo->SetField(&area->m_FieldTerritory, area->m_FieldType, true);
                }
                break;
            }
            case AREAT_NO_RESTART:
                break;
            default: {
                MiniProfile staticFieldProfile(&totalAddAreaTime3);
                if (!g_bLoadEventDataFromDB || area->m_FieldType != GEO_FIELD_WATER) {
                    geo->SetStaticField(&area->m_FieldTerritory, area->m_FieldType);
                }
                break;
            }
        }
    }

    m_AreaMap[area->m_FieldTerritory.sName] = area;
    m_vArea[zoneX - 10][zoneY - 10][area->m_AreaType].push_back(area);

    if (area->IsOn(0) && area->m_pEventController != nullptr) {
        const FVector position = area->m_pEventController->GetPos();
        CWorldContainer* const container = CContainerDB::Inst().GetWorldContainer(
            static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z)
        );
        if (container == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     "Putitem failed : pos(%d, %d, %d), file[%s], line[%d]",
            //     static_cast<int>(position.x),
            //     static_cast<int>(position.y),
            //     static_cast<int>(position.z),
            //     "Area.cpp",
            //     0x3f7)
            // L2_LOG_ERROR(Log,
            //     "Putitem failed : pos(%d, %d, %d), file[%s], line[%d]",
            //     static_cast<int>(position.x),
            //     static_cast<int>(position.y),
            //     static_cast<int>(position.z),
            //     "Area.cpp",
            //     0x3f7)
            L2_LOG_ERROR(
                Log,
                u"[areas] Area \"%ls\" is registered, but its event controller cannot be queued: no world container at (%d, "
                u"%d, %d).",
                areaName.c_str(), static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z)
            );
            return;
        }
        area->m_pEventController->m_data->m_bIsOpen = true;
        g_PendingStaticObjects.RegisterAsPending(area->m_pEventController);
    }
}
