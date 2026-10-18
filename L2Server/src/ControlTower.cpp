#include "ControlTower.h"
#include "Area.h"
#include "CWorld.h"
#include "Castle.h"
#include "DB.h"
#include "Dominion.h"
#include "Fortress.h"
#include "NpcServer.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Relay.h"
#include "Residence.h"
#include "Shared/src/MemorySub.h"
#include "SharedContainer.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace {


constexpr char16_t kEmptyUtf16[] = u"";

void CopyUtf16Name(char16_t (&destination)[64], const char16_t* source) noexcept {
    std::size_t length = 0;
    while (length < 64 && source[length] != u'\0') {
        ++length;
    }
    if (length == 64) {
        destination[0] = u'\0';
        return;
    }
    std::memcpy(destination, source, (length + 1) * sizeof(char16_t));
}

} // namespace

CControlTowerDB::CControlTowerDB() {
    L2_GUARD_FUNCTION();
    m_NameMap.clear();
    m_IdMap.clear();
}

CControlTowerDB& CControlTowerDB::Inst() {
    static CControlTowerDB instance;
    return instance;
}

CControlTower* CControlTowerDB::GetControlTower(const char16_t* const name) {
    L2_GUARD_FUNCTION();
    const auto found = [&]() {
        L2_GUARD(m_Lock);
        return m_NameMap.find(name);
    }();

    if (found == m_NameMap.end()) {
        const std::wstring hostName = name != nullptr ? Utf16ToWide(name) : std::wstring{};
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] undefined (or unregistered) ControlTower[%s]",
        //     __WFILE__,
        //     0x196,
        //     name != nullptr
        //         ? hostName.c_str()
        //         : nullptr)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] undefined (or unregistered) ControlTower[%s]",
        //     __WFILE__,
        //     0x196,
        //     name != nullptr
        //         ? hostName.c_str()
        //         : nullptr)
        L2_LOG_ERROR(
            Log, u"No control tower is registered under name '%ls'; returning no tower.",
            name != nullptr ? hostName.c_str() : L"<null>"
        );
        return nullptr;
    }
    return found->second;
}

void CControlTowerDB::AddControlTower(const char16_t* const name, CControlTower* const tower) {
    L2_GUARD_FUNCTION();
    const FVector position = tower->GetPos();
    const int x = runtime::Win64CvttDoubleToInt32(position.x);
    const int y = runtime::Win64CvttDoubleToInt32(position.y);
    const int z = runtime::Win64CvttDoubleToInt32(position.z);
    if (CContainerDB::Inst().GetWorldContainer(x, y, z) == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Putitem failed : pos(%d, %d, %d), file[%s], line[%d]",
        //     x,
        //     y,
        //     z,
        //     "ControlTower.cpp",
        //     0x175)
        // L2_LOG_ERROR(Log,
        //     "Putitem failed : pos(%d, %d, %d), file[%s], line[%d]",
        //     x,
        //     y,
        //     z,
        //     "ControlTower.cpp",
        //     0x175)
        L2_LOG_ERROR(
            Log, u"Cannot register control tower '%ls': no world container at (%d, %d, %d); this registration is skipped.",
            name != nullptr ? name : u"<null>", x, y, z
        );
        return;
    }

    if (tower->m_eControlTowerType == ControlTowerTypeEnum::Support && tower->GetHP() <= 0.0) {
        tower->SetStatus(ControlTowerStatusEnum::Destroyed);
    }
    g_PendingStaticObjects.RegisterAsPending(tower);

    {
        L2_GUARD(m_Lock);
        m_NameMap[std::u16string(name != nullptr ? name : u"")] = tower;
        m_IdMap[static_cast<int>(tower->Id())] = tower;
    }
}

CControlTower::CControlTower() :
    m_nDisplayNPCID_Working(0),
    m_nDisplayNPCID_NotWorking(0),
    m_nControlLevel(0),
    m_bTogglable(false),
    padding_4f5{},
    m_eStatus(ControlTowerStatusEnum::Idle),
    m_eReservedStatus(ControlTowerStatusEnum::Idle),
    m_pOwningResidence(nullptr),
    m_bCanChangeStatus(1),
    padding_50c{},
    m_eControlTowerType(ControlTowerTypeEnum::Undefined),
    m_sControlTowerName{},
    padding_5b4{} {
    m_data->m_dMaxHP = 1000.0;
    m_data->m_dHP = 1000.0;
    m_data->m_dPDefend = 476.0;
    m_data->m_dMDefend = 383.0;
    CStaticObject::OnCreate();
}

CControlTower::CControlTower(const CControlTower& other) :
    CStaticObject(other),
    m_nDisplayNPCID_Working(0),
    m_nDisplayNPCID_NotWorking(0),
    m_nControlLevel(0),
    m_bTogglable(other.m_bTogglable),
    padding_4f5{},
    m_eStatus(ControlTowerStatusEnum::Idle),
    m_eReservedStatus(ControlTowerStatusEnum::Idle),
    m_pOwningResidence(nullptr),
    m_bCanChangeStatus(1),
    padding_50c{},
    m_eControlTowerType(other.m_eControlTowerType),
    m_sControlTowerName{},
    padding_5b4{} {
    m_data->m_dMaxHP = 1000.0;
    m_data->m_dHP = 1000.0;
    m_data->m_dPDefend = 476.0;
    m_data->m_dMDefend = 383.0;
    CopyUtf16Name(m_sControlTowerName, other.m_sControlTowerName);
    CStaticObject::OnCreate();
}

CControlTower::~CControlTower() {
    m_pOwningResidence = nullptr;
    g_ServerContext.ControlTower.fetch_sub(1, std::memory_order_relaxed);
}

L2_DEFINE_OBJECT_CLASS_ID(CControlTower)

L2_DEFINE_OBJECT_CLASS_NAME(CControlTower, m_pClassName_CControlTower)

L2_DEFINE_OBJECT_ALLOCATOR(CControlTower, __GetClassName())

L2_DEFINE_OBJECT_NEW(CControlTower)

L2_DEFINE_OBJECT_TYPED_RELEASE(CControlTower)

CObject* CControlTower::Copy() { return new CControlTower(*this); }

bool CControlTower::IsControlTower() { return true; }

CControlTower* CControlTower::CastControlTower() { return this; }

bool CControlTower::IsEnemyTo(CCreature* creature) {
    L2_GUARD_FUNCTION();
    if (m_eControlTowerType != ControlTowerTypeEnum::Support) {
        return true;
    }

    User* const user = creature->CastUser();
    if (user == nullptr) {
        return true;
    }
    if (CDominionManager::Inst()->GetState() != DSST_DOMINION_SIEGE) {
        return false;
    }
    return user->m_data->m_nDominionSiegeId != GetDominionSiegeID();
}

bool CControlTower::IsAlive() { return m_eStatus != ControlTowerStatusEnum::Idle; }

void CControlTower::DecideDistance(CMoveToTargetShareData* data, CSmartID creatureId) {
    DecideDistanceAttackable(data, creatureId, 100);
}

bool CControlTower::IsActive() { return m_data->m_bIsActive; }

double CControlTower::AttackedBy(CCreature* attacker, double damage) {
    L2_GUARD_FUNCTION();
    return GotDamageBy(attacker, damage);
}

double CControlTower::GotDamageBy(CCreature* attacker, double damage) {
    L2_GUARD_FUNCTION();
    if (!m_data->m_bBreakable) {
        return 0.0;
    }

    User* const userOrMaster = attacker->GetUserOrMaster();
    if (m_eControlTowerType == ControlTowerTypeEnum::Support) {
        if (userOrMaster == nullptr) {
            return 0.0;
        }
        if (userOrMaster->m_data->m_nDominionSiegeId == GetDominionSiegeID()) {
            return 0.0;
        }
    }

    attacker->SendSystemMessage_d(SystemMessageId::DEALT_DAMAGE, runtime::Win64CvttDoubleToInt32(damage));

    const double hp = GetHP();
    if (hp - damage > 0.0) {
        SetHP(hp - damage);
    } else {
        SetHP(0.0);
        const bool ownsTransition = runtime::raw_atomic::CompareExchangeI32(&m_bCanChangeStatus, 0, 1) == 1;
        if (ownsTransition) {
            if (!m_bTogglable) {
                if (m_eControlTowerType == ControlTowerTypeEnum::Support && m_eStatus == ControlTowerStatusEnum::Working) {
                    User* actor = userOrMaster;
                    if (actor == nullptr) {
                        actor = attacker->CastUser();
                    }
                    CLogFormatter formatter;
                    formatter.WriteLog2(0x7ed, actor, nullptr, GetDominionSiegeID(), 0, 0, 0, 0, 0, 0);
                    const std::uint32_t killerId = userOrMaster != nullptr ? userOrMaster->Id() : attacker->Id();
                    g_npcserver.Send("cdddd", 0x26, GetDominionSiegeID(), 7, 0, killerId);
                    if (m_pOwningResidence && m_pOwningResidence->IsFortress()) {
                        CDominion* const dominion = g_ResidenceDB.GetDominion(GetDominionSiegeID());
                        if (dominion != nullptr) {
                            CCastle* const castle = dominion->GetCastle();
                            if (castle != nullptr) {
                                castle->NotifySupportBroken();
                            }
                        }
                        db.RequestSaveControlTowerStatus(this);
                    }
                }

                if (m_eStatus == ControlTowerStatusEnum::Working) {
                    SetStatus(ControlTowerStatusEnum::Destroyed);
                    const std::int32_t displayNpcId =
                        m_eStatus == ControlTowerStatusEnum::Working ? m_nDisplayNPCID_Working : m_nDisplayNPCID_NotWorking;
                    const std::int32_t residenceId = m_pOwningResidence->GetResidenceID();
                    const std::int64_t x = runtime::Win64CvttDoubleToInt64(GetPos().x);
                    const std::int64_t z = runtime::Win64CvttDoubleToInt64(GetPos().z);
                    const std::int64_t y = runtime::Win64CvttDoubleToInt64(GetPos().y);
                    CLogFormatter formatter;
                    formatter.WriteLog2(0x7ee, userOrMaster, nullptr, x, y, z, residenceId, displayNpcId, 0, 0);
                }
            } else {
                if (m_eStatus == ControlTowerStatusEnum::Working) {
                    SetStatus(ControlTowerStatusEnum::Destroyed);
                    const std::int32_t displayNpcId =
                        m_eStatus == ControlTowerStatusEnum::Working ? m_nDisplayNPCID_Working : m_nDisplayNPCID_NotWorking;
                    const std::int32_t residenceId = m_pOwningResidence->GetResidenceID();
                    const std::int64_t x = runtime::Win64CvttDoubleToInt64(GetPos().x);
                    const std::int64_t z = runtime::Win64CvttDoubleToInt64(GetPos().z);
                    const std::int64_t y = runtime::Win64CvttDoubleToInt64(GetPos().y);
                    CLogFormatter formatter;
                    formatter.WriteLog2(0x7ee, userOrMaster, nullptr, x, y, z, residenceId, displayNpcId, 0, 0);

                    const std::int32_t relayDisplayNpcId =
                        m_eStatus == ControlTowerStatusEnum::Working ? m_nDisplayNPCID_Working : m_nDisplayNPCID_NotWorking;
                    const std::int32_t relayResidenceId = m_pOwningResidence->GetResidenceID();
                    const std::int64_t relayX = runtime::Win64CvttDoubleToInt64(GetPos().x);
                    const std::int64_t relayZ = runtime::Win64CvttDoubleToInt64(GetPos().z);
                    const std::int64_t relayY = runtime::Win64CvttDoubleToInt64(GetPos().y);
                    RelayClient::Inst()->SendTowerDestructPacket(
                        relayX, relayY, relayZ, relayResidenceId, relayDisplayNpcId
                    );
                } else {
                    SetStatus(ControlTowerStatusEnum::Working);
                }
                SetHP(m_data->m_dMaxHP);
            }

            if (m_eControlTowerType == ControlTowerTypeEnum::Life && m_pOwningResidence && m_pOwningResidence->IsCastle()) {
                m_pOwningResidence->CastCastle()->ValidateLifeControlLevel(true);
            }
            runtime::raw_atomic::ExchangeI32(&m_bCanChangeStatus, 1);
        }
    }

    SendControlTowerInfo(nullptr);
    return damage;
}

bool CControlTower::IsEnabled() { return m_data->m_bIsActive; }

void CControlTower::SendControlTowerInfoImpl(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    if (user->GetSocket() == nullptr) {
        return;
    }

    const double x = GetPos().x;
    const double y = GetPos().y;
    const double z = GetPos().z;
    CUserSocket* const socket = user->GetSocket();
    const std::int32_t displayNpcId =
        m_eStatus == ControlTowerStatusEnum::Working ? m_nDisplayNPCID_Working : m_nDisplayNPCID_NotWorking;
    const double hp = m_data->m_dHP;
    const double maximumHp = m_data->m_dMaxHP;
    const bool enemy = IsEnemyTo(user);
    socket->Send(
        "cddSdddddffffdddd", 0x92, static_cast<std::int32_t>(m_nId), displayNpcId, kEmptyUtf16, enemy ? 1 : 0,
        runtime::Win64CvttDoubleToInt32(x), runtime::Win64CvttDoubleToInt32(y), runtime::Win64CvttDoubleToInt32(z),
        0, 1.0, 1.0, 7.0, 35.0, runtime::Win64CvttDoubleToInt32(maximumHp), runtime::Win64CvttDoubleToInt32(hp), 2, 0
    );
}

int CControlTower::GetDominionSiegeID() {
    L2_GUARD_FUNCTION();
    if (m_pOwningResidence == nullptr) {
        return 0;
    }
    CFortress* const fortress = m_pOwningResidence->CastFortress();
    return fortress != nullptr ? fortress->GetDominionSiegeID() : 0;
}

void CControlTower::SendControlTowerInfo(CUserSocket* socket) {
    L2_GUARD_FUNCTION();
    class TLMFunc final : public CTLMFunc {
    public:
        explicit TLMFunc(CControlTower* const controlTower) noexcept : m_controlTower(controlTower) {}

        bool CanDo(CObject* const object, double) override {
            User* user = nullptr;
            if (object->IsPC()) {
                user = object->CastUser();
            }
            if (user != nullptr && !object->IsDeleting() && object->m_Deleted == 0) {
                m_controlTower->SendControlTowerInfoImpl(user);
            }
            return true;
        }

    private:
        CControlTower* m_controlTower;
    };

    if (socket == nullptr) {
        const FVector position = GetPos();
        const int inZoneId = static_cast<int>(GetInZoneID());
        CTLMFilterRect filter(inZoneId, position, 2048.0);
        TLMFunc sender(this);
        FOR_ALL_NEIGHBOR_USER_RECT(&filter, &sender);
        return;
    }

    SendControlTowerInfoImpl(socket->GetUser());
}

void CControlTower::SetStatus(ControlTowerStatusEnum status) {
    L2_GUARD_FUNCTION();
    if (status == m_eStatus) {
        return;
    }

    CWorld::Inst().StaticObjectDisappear(this);
    for (std::size_t index = 0; index < m_lstControlArea.size() && static_cast<std::int32_t>(index) < m_nControlLevel;
        ++index) {
        runtime::CheckedVectorAt(m_lstControlArea, index)->SetOnOff(status == ControlTowerStatusEnum::Working);
    }
    m_eStatus = status;
    CWorld::Inst().StaticObjectAppear(this, GetPos());

    if (status != ControlTowerStatusEnum::Idle && m_eControlTowerType == ControlTowerTypeEnum::Trap && m_pOwningResidence &&
        m_pOwningResidence->IsCastle()) {
        CResidence* const residence = m_pOwningResidence;
        FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
            if (user->IsInRealWorld() && residence->IsInBattleField(user->GetPos(), true)) {
                if (status == ControlTowerStatusEnum::Working) {
                    user->GetSocket()->SendSystemMessage(SystemMessageId::TRAP_DEVICE_ACTIVATED);
                } else if (status == ControlTowerStatusEnum::Destroyed) {
                    user->GetSocket()->SendSystemMessage(SystemMessageId::TRAP_DEVICE_STOPPED);
                }
            }
        });
    }
}

void CControlTower::ChangeToReservedStatus() {
    L2_GUARD_FUNCTION();
    SetStatus(m_eReservedStatus);
}

void CControlTower::OnSiegeStarted(bool resumed) {
    L2_GUARD_FUNCTION();
    if (!resumed) {
        SetHP(m_data->m_dMaxHP);
    } else {
        ChangeToReservedStatus();
    }

    if (m_eControlTowerType == ControlTowerTypeEnum::Support) {
        if (GetHP() <= 0.0) {
            if (m_pOwningResidence && m_pOwningResidence->IsFortress()) {
                static_cast<void>(g_ResidenceDB.GetDominion(GetDominionSiegeID()));
                db.RequestSaveControlTowerStatus(this);
            }
            SetStatus(ControlTowerStatusEnum::Destroyed);
        } else {
            SetStatus(ControlTowerStatusEnum::Working);
            CWorld::Inst().StaticObjectAppear(this, GetPos());
        }
        SetBreakable(true);
    }

    if (m_eControlTowerType == ControlTowerTypeEnum::Life) {
        if (GetHP() > 0.0) {
            SetStatus(ControlTowerStatusEnum::Working);
        }
        SetBreakable(true);
    } else if (m_eControlTowerType == ControlTowerTypeEnum::Trap && m_nControlLevel > 0) {
        SetStatus(ControlTowerStatusEnum::Working);
        SetBreakable(true);
    }
    db.RequestSaveControlTowerStatus(this);
}

void CControlTower::OnSiegeEnded() {
    L2_GUARD_FUNCTION();
    SetHP(m_data->m_dMaxHP);
    SetStatus(ControlTowerStatusEnum::Idle);
    SetBreakable(false);
    m_nControlLevel = 0;
    db.RequestSaveControlTowerStatus(this);
    if (m_eControlTowerType == ControlTowerTypeEnum::Support) {
        CWorld::Inst().StaticObjectDisappear(this);
    }
}

void CControlTower::OnMidVictory() {
    L2_GUARD_FUNCTION();
    SetHP(m_data->m_dMaxHP);
    if (m_eControlTowerType == ControlTowerTypeEnum::Life) {
        SetStatus(ControlTowerStatusEnum::Working);
        SetBreakable(true);
    } else if (m_eControlTowerType == ControlTowerTypeEnum::Trap) {
        SetStatus(ControlTowerStatusEnum::Idle);
        SetBreakable(false);
    }
    m_nControlLevel = 0;
    db.RequestSaveControlTowerStatus(this);
}

void CControlTower::OnSiegePaused() {
    L2_GUARD_FUNCTION();
    m_eReservedStatus = m_eStatus;
    SetStatus(ControlTowerStatusEnum::Idle);
    SetBreakable(false);
}

void CControlTower::OnSiegeResumed() {
    L2_GUARD_FUNCTION();
    ChangeToReservedStatus();
    SetBreakable(
        m_eControlTowerType == ControlTowerTypeEnum::Life ||
        (m_eControlTowerType == ControlTowerTypeEnum::Trap && m_nControlLevel > 0) ||
        m_eControlTowerType == ControlTowerTypeEnum::Support
    );
}
