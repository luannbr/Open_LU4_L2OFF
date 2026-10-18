#include "BattleCamp.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/Log.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/SourceFile.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"

#include "CConfig.h"
#include "CWorld.h"
#include "Creature.h"
#include "DB.h"
#include "ObjectDB.h"
#include "Pledge.h"
#include "PledgeMemberService.h"
#include "Residence.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Skill.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserSocket.h"

#include "Portable/ProcessRuntime.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Config.h"
#include "Shared/src/Exception.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/Socket.h"
#include "Shared/src/SystemMail.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <source_location>
#include <vector>

namespace {


void CopyUtf16Name(char16_t (&destination)[24], const char16_t* source) noexcept {
    std::size_t length = 0;
    while (length < 24 && source[length] != u'\0') {
        ++length;
    }
    if (length == 24) {
        destination[0] = u'\0';
        return;
    }
    std::memcpy(destination, source, (length + 1) * sizeof(char16_t));
}

void ReportInvalidCastleIndex(const std::source_location& where, std::uint32_t index) {
    // Log.Add(
    //     LOG_ERROR,
    //     "[%s][%d] castle_index is wrong!! [%u]",
    //     kUserHeaderPath,
    //     sourceLine,
    //     index)
    // L2_LOG_ERROR(Log,
    //     "[%s][%d] castle_index is wrong!! [%u]",
    //     kUserHeaderPath,
    //     sourceLine,
    //     index)
    L2_LOG_ERROR(
        Log,
        "Battle camp castle-siege status lookup has index %u outside 0..9 at %s:%d; reporting the stack and returning "
        "status 0.",
        index, ::source_location::FileName(where.file_name()), static_cast<int>(where.line())
    );
    runtime::process::ReportCallStack(2, false);
}

int UserCastleSiegeStatus(
    User* const user, const std::uint32_t index, const std::source_location where = std::source_location::current()
) {
    if (index < 9U) {
        return user->m_nCastleSiegeStatus[index];
    }
    if (index == 9U) {
        return user->m_nFortressUnderOffenceSiegeId;
    }
    ReportInvalidCastleIndex(where, index);
    return 0;
}

} // namespace

double CBattleCamp::s_dMaxHP = 7000000.0;
double CBattleCamp::s_dPDefend = 6918.0;
double CBattleCamp::s_dMDefend = 8029.0;

std::array<std::int32_t, 21> g_aHadMidVictory{};

CBattleCamp::CBattleCamp(
    std::uint32_t databaseId, CResidence* residence, CPledge* pledge, FVector position, int maximumHp, int hp, int baseType
) {
    L2_GUARD_FUNCTION();

    SetPos(position);
    SetDBID(databaseId);
    m_data->m_dMaxHP = static_cast<double>(maximumHp);
    m_data->m_dHP = static_cast<double>(hp);
    m_data->m_dPDefend = s_dPDefend;
    m_data->m_dMDefend = s_dMDefend;
    m_data->m_bIsActive = true;

    m_pBaseResidence = residence;
    m_nBaseResidenceId = residence->GetResidenceID();

    m_pOwningPledge = pledge;
    m_nOwningPledgeId = pledge->GetDbId();
    m_nOwningPledgeWorld = static_cast<std::uint32_t>(pledge->GetWorldId());
    const std::u16string pledgeName = std::u16string(pledge->GetPledgeName());
    CopyUtf16Name(m_sBattleCampName, pledgeName.c_str());

    m_dPreviousHp = 0.0;
    m_dPreviousSavedHp = 0.0;
    m_tAttacked = 0;
    m_nBaseType = baseType;
    m_nDbSaveTick = 0;
    CStaticObject::OnCreate();
    m_nWorldId = g_nWorldId;
}

CBattleCamp::CBattleCamp(const CBattleCamp& other) : CStaticObject(other) {
    L2_GUARD_FUNCTION();
    m_pBaseResidence = other.m_pBaseResidence;
    m_nBaseResidenceId = other.m_nBaseResidenceId;
    m_nOwningPledgeId = other.m_nOwningPledgeId;
    m_nOwningPledgeWorld = other.m_nOwningPledgeWorld;
    m_dPreviousHp = other.m_dPreviousHp;
    m_tAttacked = other.m_tAttacked;
    CopyUtf16Name(m_sBattleCampName, other.m_sBattleCampName);
    CStaticObject::OnCreate();
}

CBattleCamp::~CBattleCamp() {
    m_pBaseResidence = nullptr;
    m_pOwningPledge = nullptr;
    g_ServerContext.BattleCamp.fetch_sub(1, std::memory_order_relaxed);
}

L2_DEFINE_OBJECT_CLASS_ID(CBattleCamp)

L2_DEFINE_OBJECT_CLASS_NAME(CBattleCamp, m_pClassName_CBattleCamp)

L2_DEFINE_OBJECT_ALLOCATOR(CBattleCamp, __GetClassName())

L2_DEFINE_OBJECT_NEW(CBattleCamp)

L2_DEFINE_OBJECT_TYPED_RELEASE(CBattleCamp)

CObject* CBattleCamp::Copy() { return new CBattleCamp(*this); }

User* CBattleCamp::GetUserOrMaster() {
    L2_GUARD_FUNCTION();
    if (!m_pOwningPledge) {
        return nullptr;
    }

    return m_pOwningPledge->GetMaster();
}

bool CBattleCamp::IsBattleCamp() { return true; }

bool CBattleCamp::IsAdvanceBase() { return m_nBaseType == 1; }

CBattleCamp* CBattleCamp::CastBattleCamp() { return this; }

bool CBattleCamp::IsEnemyTo(CCreature* creature) {
    L2_GUARD_FUNCTION();
    if (!creature || !m_data->m_bIsActive || !m_pBaseResidence) {
        return false;
    }

    CObject* const object = creature;
    int castleState = 0;
    if (g_aHadMidVictory[static_cast<std::size_t>(m_nBaseResidenceId)] == 0 && object->IsPC()) {
        User* const user = object->CastUser();
        const std::uint32_t castleIndex = static_cast<std::uint32_t>(m_nBaseResidenceId) - 1U;
        if (UserCastleSiegeStatus(user, castleIndex) == 1) {
            return false;
        }
    }

    const int residenceId = m_nBaseResidenceId;
    if (!object->IsSummonOrPet()) {
        if (object->IsPC()) {
            if (m_nOwningPledgeId == creature->m_data->m_nPledgeID &&
                m_nOwningPledgeWorld == static_cast<std::uint32_t>(creature->m_data->m_nWorldId)) {
                return false;
            }

            User* const user = object->CastUser();
            if (g_bIsNonPKServer && (user->m_AreaMod.m_bIsInPvPZone || !user->m_AreaMod.m_bIsInBattleField)) {
                const std::uint32_t castleIndex = static_cast<std::uint32_t>(residenceId) - 1U;
                castleState = UserCastleSiegeStatus(user, castleIndex);
                if (castleState == 0) {
                    return false;
                }
            }
        }
        return true;
    }

    CCreature* const ownerObject = g_CreatureSmartIdManager.GetObjectW(creature->m_data->m_SummonerID);
    User* owner = nullptr;
    if (ownerObject && ownerObject->IsPC()) {
        owner = ownerObject->CastUser();
    }
    if (!owner) {
        return false;
    }

    CPledge* const ownerPledge = owner->GetPledge();
    if (!ownerPledge || !m_pOwningPledge) {
        return false;
    }

    const int ownerRelation = ownerPledge->GetCurrentCastleSiegeStatus(residenceId);
    const int campRelation = m_pOwningPledge->GetCurrentCastleSiegeStatus(residenceId);
    if (!g_bIsNonPKServer) {
        return (ownerRelation == 0 && campRelation == 0) || ownerRelation != campRelation;
    }
    if (!owner->m_AreaMod.m_bIsInPvPZone && owner->m_AreaMod.m_bIsInBattleField) {
        return (ownerRelation == 0 && campRelation == 0) || ownerRelation != campRelation;
    }
    return ownerRelation != 0 && ownerRelation != campRelation;
}

bool CBattleCamp::IsAlive() { return m_data->m_bIsActive; }

void CBattleCamp::SendDB(const char* format, ...) const {
    L2_GUARD_FUNCTION();
    const int worldId = ::CThreadManager::GetThreadID() < 16 ? m_nWorldId : g_nWorldId;
    runtime::process::SetCurrentWorldId(worldId);
    CDB::CWorldSetter worldSetter;

    va_list arguments;
    va_start(arguments, format);
    if (worldId < 1) {
        SystemMail::sendNotSetThreadLocalWorldID(mailServer);
    } else {
        db.m_pDBSockets[::LegacyCrtRand() & 1]->SendV(format, arguments);
    }
    va_end(arguments);
}

void CBattleCamp::DecideDistance(CMoveToTargetShareData* data, CSmartID creatureId) {
    DecideDistanceAttackable(data, creatureId, 100);
}

bool CBattleCamp::IsActive() { return m_data->m_bIsActive; }

double CBattleCamp::AttackedBy(CCreature* attacker, double damage) {
    L2_GUARD_FUNCTION();
    return GotDamageBy(attacker, damage);
}

double CBattleCamp::GotDamageBy(CCreature* attacker, double damage) {
    L2_GUARD_FUNCTION();
    if (IsAdvanceBase()) {
        return 0.0;
    }

    attacker->SendSystemMessage_d(SystemMessageId::DEALT_DAMAGE, runtime::Win64CvttDoubleToInt32(damage));
    if (damage > 0.0) {
        const std::int32_t now = runtime::crt::Time32Now();
        if (runtime::SubtractInt32Wrapping(now, m_tAttacked) > 30) {
            m_tAttacked = now;
            SystemMessage<BattleFieldChecker, CResidence> message(
                SystemMessageId::SIEGE_HEADQUARTERS_UNDER_ATTACK, m_pBaseResidence, 0, 0
            );
            m_pOwningPledge->ServeForAllMember(message, false);
        }
    }

    const double hp = GetHP();
    if (hp - damage > 0.0) {
        SetHP(hp - damage);
    } else {
        SetHP(0.0);
        db.RequestSaveBattleCampHp(this);
        db.RequestUninstallBattleCampBy(m_pOwningPledge, m_nBaseType);
    }
    return damage;
}

bool CBattleCamp::IsEnabled() { return m_data->m_bIsActive; }

FVector CBattleCamp::GetTeleportPos() {
    L2_GUARD_FUNCTION();
    FVector position = GetPos();
    const int random = ::LegacyCrtRand();
    const double xOffset = static_cast<double>(((random % 10) * 5 + 25) * 2);
    position.x += (random & 1) == 0 ? -xOffset : xOffset;

    const double yOffset = static_cast<double>(((random % 7) * 5 + 25) * 2);
    position.y += random % 3 == 0 ? -yOffset : yOffset;

    CGeoData* const geo = CWorld::Inst().GetGeo();
    position.z = static_cast<double>(geo->FindGround(
        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        runtime::Win64CvttDoubleToInt32(position.z + 32.0)
    ));
    return position;
}

void CBattleCamp::Appear() {
    L2_GUARD_FUNCTION();
    const FVector position = GetPos();
    CWorld::Inst().StaticObjectAppear(this, position);
    AddTimer(1000, 0);
}

void CBattleCamp::Disappear() {
    L2_GUARD_FUNCTION();
    m_data->m_bIsActive = false;
    CWorld::Inst().StaticObjectDisappear(this);
}

std::uint32_t CBattleCamp::GetOwningPledgeID() { return m_nOwningPledgeId; }

std::uint32_t CBattleCamp::GetOwningPledgeWorldID() { return m_nOwningPledgeWorld; }

void CBattleCamp::SendBattleCampInfo(CUserSocket* socket) {
    L2_GUARD_FUNCTION();
    class TLMFunc final : public CTLMFunc {
    public:
        TLMFunc(const double firstValue, const double secondValue, CBattleCamp* const battleCamp) noexcept :
            m_firstValue(firstValue), m_secondValue(secondValue), m_battleCamp(battleCamp) {}

        bool CanDo(CObject* const object, double) override {
            User* user = nullptr;
            if (object->IsPC()) {
                user = object->CastUser();
            }
            if (user != nullptr && !object->IsDeleting() && object->m_Deleted == 0) {
                m_battleCamp->SendBattleCampInfoImpl(user, m_firstValue, m_secondValue);
            }
            return true;
        }

    private:
        double m_firstValue;
        double m_secondValue;
        CBattleCamp* m_battleCamp;
    };

    double firstValue = 10.0;
    double secondValue = 80.0;
    if (IsAdvanceBase()) {
        firstValue = 110.0;
        secondValue = 70.0;
    }

    if (!socket) {
        const FVector position = GetPos();
        CTLMFilterRect filter(0, position, 2048.0);
        TLMFunc function(firstValue, secondValue, this);
        FOR_ALL_NEIGHBOR_USER_RECT(&filter, &function);
        return;
    }

    User* const user = socket->GetUser();
    if (user) {
        SendBattleCampInfoImpl(user, firstValue, secondValue);
    }
}

void CBattleCamp::SendBattleCampInfoImpl(User* user, double firstValue, double secondValue) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return;
    }
    CUserSocket* socket = user->GetSocket();
    if (!socket) {
        return;
    }

    const double x = GetPos().x;
    const double y = GetPos().y;
    const double z = GetPos().z;
    socket = user->GetSocket();
    socket->Send(
        "cddSdddddffffdddd", 0x92, static_cast<std::int32_t>(m_nId), m_nClassId, m_sBattleCampName, IsEnemyTo(user) ? 1 : 0,
        runtime::Win64CvttDoubleToInt32(x), runtime::Win64CvttDoubleToInt32(y), runtime::Win64CvttDoubleToInt32(z),
        0, 1.0, 1.0, firstValue, secondValue, runtime::Win64CvttDoubleToInt32(m_data->m_dMaxHP),
        runtime::Win64CvttDoubleToInt32(GetHP()), 1, m_nBaseType
    );
}

double CBattleCamp::GetStaticMaxHP() { return s_dMaxHP; }

int CBattleCamp::GetBaseResidenceId() { return m_nBaseResidenceId; }

int CBattleCamp::GetBaseType() { return m_nBaseType; }

int CBattleCamp::GetWorldId() const { return m_nWorldId; }

void CBattleCamp::TimerExpired(int id) {
    static_cast<void>(id);
    L2_GUARD_FUNCTION();
    if (!m_data->m_bIsActive) {
        return;
    }

    double hp = GetHP();
    if (m_dPreviousHp != hp) {
        SendBattleCampInfo(nullptr);
        hp = GetHP();
        m_dPreviousHp = hp;
    }
    if ((m_nDbSaveTick % 10 == 0 && m_dPreviousSavedHp != hp) || hp == 0.0) {
        db.RequestSaveBattleCampHp(this);
        m_dPreviousSavedHp = GetHP();
    }
    m_nDbSaveTick = runtime::AddInt32Wrapping(m_nDbSaveTick, 1);
    AddTimer(1000, 0);
}

CBattleCampDb::CBattleCampDb() {
    // Current L2Server 0x004b1e10. Member constructors own the exact map,
    // lock and reservation-tree initialization.
    L2_GUARD_FUNCTION();
}

CBattleCampDb::~CBattleCampDb() = default;

CSPointer<CBattleCamp> CBattleCampDb::GetBattleCamp(std::uint32_t databaseId) {
    // Current L2Server 0x004b141c.
    L2_GUARD_FUNCTION();
    return m_BattleCampMap.FindObject(static_cast<int>(databaseId));
}

void CBattleCampDb::AddBattleCamp(const char16_t* className, CBattleCamp* battleCamp) {
    // Current L2Server 0x004b18e8.
    L2_GUARD_FUNCTION();

    battleCamp->m_nClassId = g_ObjectDB.GetClassIdFromName(className, false);
    m_BattleCampMap.Insert(battleCamp->GetDBID(), battleCamp, const_cast<char*>("BattleCamp.cpp"), 0x1d8);

    {
        L2_GUARD(m_BattleCampDbSect);
        auto& reservation = m_mapBattleCampPos[battleCamp->m_nOwningPledgeId];
        reservation.first = battleCamp->GetPos();
        reservation.second = 0;
    }

    if (battleCamp->IsEnabled()) {
        battleCamp->Appear();
    }
}

void CBattleCampDb::RemoveBattleCamp(std::uint32_t databaseId) {
    // Current L2Server 0x004b1a68.
    L2_GUARD_FUNCTION();

    CSPointer<CBattleCamp> battleCamp = m_BattleCampMap.FindObject(static_cast<int>(databaseId));
    if (battleCamp) {
        battleCamp->Disappear();
        m_BattleCampMap.DeleteObject(static_cast<int>(databaseId), const_cast<char*>("BattleCamp.cpp"), 0x1f9);
    }

    {
        L2_GUARD(m_BattleCampDbSect);
        if (battleCamp) {
            m_mapBattleCampPos.erase(battleCamp->m_nOwningPledgeId);
        } else {
            // operator-> in current CSPointer reports this condition before the
            // original null dereference. Preserve the diagnostic while keeping
            // the portable host process recoverable.
            runtime::process::ReportNullSmartPointerDereference();
        }
    }
}

bool CBattleCampDb::ReserveBattleCamp(std::uint32_t pledgeId, const FVector& position) {
    // Current L2Server 0x004b1fa0.
    L2_GUARD_FUNCTION();

    static const CSkillInfo* const battleCampSkill = g_SkillDB.GetSkillInfo(CSkillKey(0xf7, 1), nullptr);
    if (battleCampSkill == nullptr) {
        return false;
    }

    const std::int32_t reservationWindow = static_cast<std::int32_t>(battleCampSkill->m_dSkillHitTime);
    const std::int32_t now = runtime::crt::Time32Now();
    std::vector<std::uint32_t> expired;
    bool available = true;

    {
        L2_GUARD(m_BattleCampDbSect);
        for (const auto& entry : m_mapBattleCampPos) {
            const std::int32_t timestamp = entry.second.second;
            const std::uint32_t expirationBits =
                static_cast<std::uint32_t>(timestamp) + 10U + static_cast<std::uint32_t>(reservationWindow);
            std::int32_t expiration = 0;
            std::memcpy(&expiration, &expirationBits, sizeof(expiration));
            if (timestamp == 0 || now <= expiration) {
                const double dx = position.x - entry.second.first.x;
                const double dy = position.y - entry.second.first.y;
                if (dx * dx + dy * dy < 40000.0) {
                    available = false;
                    break;
                }
            } else {
                expired.push_back(entry.first);
            }
        }

        for (const std::uint32_t id : expired) {
            m_mapBattleCampPos.erase(id);
        }
        if (available) {
            auto& reservation = m_mapBattleCampPos[pledgeId];
            reservation.first = position;
            reservation.second = now;
        }
    }
    return available;
}
