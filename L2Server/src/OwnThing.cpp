#include "OwnThing.h"
#include "CWorld.h"
#include "Castle.h"
#include "DB.h"
#include "Dominion.h"
#include "Item.h"
#include "ObjectDB.h"
#include "Packet.h"
#include "Pledge.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Residence.h"
#include "Shared/src/MemorySub.h"
#include "SharedContainer.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <climits>
#include <cstdint>
#include <string>


COwnThing::COwnThing() :
    m_nOriginalResidenceID(0),
    m_nOwningResidenceID(0),
    m_pOwningResidence(nullptr),
    m_nOwningPledgeID(0),
    m_nStatus(OTS_BROKEN),
    m_wName{},
    m_pUser(nullptr),
    m_nClassId(0),
    m_alignment5E4To5E8{} {
    L2_GUARD_FUNCTION();
    m_data->m_dPDefend = 645.0;
    m_data->m_dMDefend = 518.0;
    SetTargetable(true, false);
    SetBreakable(true);
    m_data->m_dMaxHP = 9650.0;
    m_data->m_dHP = m_data->m_dMaxHP;
    CStaticObject::OnCreate();
}

COwnThing::COwnThing(const COwnThing& other) : CStaticObject(other) {
    L2_GUARD_FUNCTION();
    CStaticObject::OnCreate();
}

COwnThing::~COwnThing() = default;

L2_DEFINE_OBJECT_CLASS_ID(COwnThing)

L2_DEFINE_OBJECT_CLASS_NAME(COwnThing, m_pClassName_COwnThing)

L2_DEFINE_OBJECT_ALLOCATOR(COwnThing, __GetClassName())

L2_DEFINE_OBJECT_NEW(COwnThing)

L2_DEFINE_OBJECT_TYPED_RELEASE(COwnThing)

void COwnThing::TimerExpired(int id) {
    L2_GUARD_FUNCTION();
    static_cast<void>(id);
}

CStaticObject* COwnThing::Copy() { return new COwnThing(*this); }

bool COwnThing::IsOwnThing() { return true; }

COwnThing* COwnThing::CastOwnThing() { return this; }

bool COwnThing::IsAlive() { return true; }

bool COwnThing::AddOwnThing(const wchar_t* const className, COwnThing* const ownThing) {
    L2_GUARD_FUNCTION();
    if (ownThing == nullptr) {
        return false;
    }
    const FVector position = ownThing->GetPos();
    CWorldContainer* const container = CContainerDB::Inst().GetWorldContainer(
        static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z)
    );
    if (container == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"container null for ownthing %s",
        //     className)
        // L2_LOG_ERROR(Log,
        //     L"container null for ownthing %s",
        //     className)
        L2_LOG_ERROR(
            Log, "Cannot register territory ward '%s': no world container at (%.17g, %.17g, %.17g).",
            className != nullptr ? WideToUtf8(className).c_str() : "<null>", position.x, position.y, position.z
        );
        return false;
    }
    g_PendingStaticObjects.RegisterAsPending(ownThing);
    const std::u16string targetName = target_utf16::FromRecoveredWindowsWide(className);
    ownThing->m_nClassId = g_ObjectDB.GetClassIdFromName(targetName.c_str(), false);
    return true;
}

bool COwnThing::GiveToUser(User* const user, const bool rejectOwningDominion) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    const int dominionId = user->m_data->m_nDominionSiegeId;
    if (dominionId == 0 || (rejectOwningDominion && dominionId == m_nOwningResidenceID) ||
        user->m_data->m_StopMode == STP_STORE || user->GetPrivateStoreType() != 0 || user->IsRiding() ||
        user->m_pOwnThing != nullptr) {
        return false;
    }

    CDominion* const dominion = g_ResidenceDB.GetDominion(m_nOriginalResidenceID);
    if (dominion == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"No dominion for ownthing %d!",
        //     m_nClassId)
        // L2_LOG_ERROR(Log,
        //     L"No dominion for ownthing %d!",
        //     m_nClassId)
        L2_LOG_ERROR(
            Log, "Cannot give ward class %d to a player: its original territory %d was not found.", m_nClassId,
            m_nOriginalResidenceID
        );
        return false;
    }

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr || pledge->GetCastle() == nullptr) {
        return false;
    }

    const bool result = [&] {
        L2_GUARD(m_BindLock);
        bool result = true;
        if (m_pUser == nullptr) {
            const std::u16string pickupName = target_utf16::FromRecoveredWindowsWide(dominion->GetPickupOwnThingName());
            const int classId = g_ObjectDB.GetClassIdFromName(pickupName.c_str(), false);
            CItem* const item = g_ObjectDB.CreateItem(classId);
            if (item == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"Pick Ownthing Item fail! [%d]",
                //     classId)
                // L2_LOG_ERROR(Log,
                //     L"Pick Ownthing Item fail! [%d]",
                //     classId)
                L2_LOG_ERROR(
                    Log,
                    "Cannot give the territory ward to a player: pickup item '%s' (class %d, original territory %d) could "
                    "not be created.",
                    Utf16ToUtf8(pickupName).c_str(), classId, m_nOriginalResidenceID
                );
                result = false;
            } else {
                item->m_data->m_bIsActive = true;
                item->m_data->SetPos(user->GetPos());
                if (!user->AddItemToInventory2(item)) {
                    result = false;
                } else {
                    CLogFormatter formatter;
                    formatter.WriteLog1(
                        0x7e9, user, nullptr, pledge->GetPledgeName(), nullptr, nullptr, pledge->GetDbId(),
                        m_nOriginalResidenceID, m_nOwningResidenceID, 1, 0, 0, 0
                    );
                    user->m_pOwnThing = this;
                    if (CUserSocket* const socket = user->GetSocket()) {
                        socket->SendSystemMessage(SystemMessageId::WARD_ACQUIRED_RETURN_TO_FRIENDLY_OUTPOST);
                    }
                    m_pUser = user;
                    SetPos(user->GetPos());
                }
            }
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR] Somebody has taken ownthing already.")
            // Noise: Another player already holds the ward. Rejecting a competing pickup is normal, and the successful
            // pickup keeps its accounting record. L2_LOG_ERROR(Log,
            //     L"[NO_ERROR] Somebody has taken ownthing already.")
            result = false;
        }
        return result;
    }();
    return result;
}

void COwnThing::Appear() {
    L2_GUARD_FUNCTION();
    SetTargetable(true, true);
    SetBreakable(true);
    m_data->m_dMaxHP = 9650.0;
    SetHP(m_data->m_dMaxHP);
    m_pUser = nullptr;
    m_nStatus = OTS_ENABLED;
    CWorld::Inst().StaticObjectAppear(this, GetPos());
}

void COwnThing::Disappear() {
    L2_GUARD_FUNCTION();
    m_nStatus = OTS_BROKEN;
    SetTargetable(false, true);
    SetBreakable(false);
    CWorld::Inst().StaticObjectDisappear(this);
}

void COwnThing::Move(const FVector position) {
    L2_GUARD_FUNCTION();
    if (m_nStatus == OTS_ENABLED) {
        Disappear();
    }
    SetPos(position);
    Appear();
}

void COwnThing::HandleChangeSiegeState(CDominion* const dominion, const bool appear) {
    L2_GUARD_FUNCTION();
    if (dominion == nullptr) {
        return;
    }
    if (m_pUser != nullptr) {
        m_pUser->DeleteOwnThing(true, false);
    }
    const FVector position = dominion->_GetPos(this);
    Move(position);
    if (!appear && m_nStatus == OTS_ENABLED) {
        Disappear();
    }
    db.RequestSaveOwnthing(
        m_nOriginalResidenceID, m_nOwningResidenceID, static_cast<int>(position.x), static_cast<int>(position.y),
        static_cast<int>(position.z)
    );
}

void COwnThing::SetOriginalResidenceID(const int residenceId) {
    L2_GUARD_FUNCTION();
    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence != nullptr && residence->IsDominion()) {
        CDominion* const dominion = residence->CastDominion();
        if (dominion != nullptr) {
            dominion->m_pMainOwnThing = this;
            m_nOriginalResidenceID = residenceId;
            return;
        }
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"original dominion doesn't exists!!! dominion[%d]",
    //     residenceId)
    // L2_LOG_ERROR(Log,
    //     L"original dominion doesn't exists!!! dominion[%d]",
    //     residenceId)
    L2_LOG_ERROR(
        Log,
        "Cannot bind the ward to original territory %d: no usable territory object was found; the original territory ID is "
        "retained.",
        residenceId
    );
}

void COwnThing::SetOwningResidenceID(const int residenceId) {
    L2_GUARD_FUNCTION();
    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence != nullptr && residence->IsDominion()) {
        m_nOwningResidenceID = residenceId;
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"owning dominion doesn't exists!!! dominion[%d]",
    //     residenceId)
    // L2_LOG_ERROR(Log,
    //     L"owning dominion doesn't exists!!! dominion[%d]",
    //     residenceId)
    L2_LOG_ERROR(
        Log,
        "Cannot assign the ward's owning territory ID %d: the residence is missing or is not a territory; the stored "
        "territory ID is retained.",
        residenceId
    );
}

void COwnThing::SetOwningResidence(CResidence* const residence) {
    L2_GUARD_FUNCTION();
    m_pOwningResidence = residence;
    SetOwningResidenceID(residence != nullptr && residence->m_data != nullptr ? residence->m_data->m_nResidenceID : 0);
}

void COwnThing::SendOwnThingInfoImpl(User* const user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->GetSocket() == nullptr) {
        return;
    }
    const FVector position = GetPos();
    user->GetSocket()->Send(
        "cddSdddddffffdddd", 0x92, static_cast<int>(m_nId), m_nClassId, u"", IsEnemyTo(user), static_cast<int>(position.x),
        static_cast<int>(position.y), static_cast<int>(position.z), 0, 1.0, 1.0, 7.0, 35.0,
        static_cast<int>(m_data->m_dMaxHP), static_cast<int>(m_data->m_dHP), 6, m_nStatus
    );
}

double COwnThing::AttackedBy(CCreature* const attacker, const double damage) {
    L2_GUARD_FUNCTION();
    return GotDamageBy(attacker, damage);
}

void COwnThing::DecideDistance(CMoveToTargetShareData* const data, const CSmartID creatureId) {
    L2_GUARD_FUNCTION();
    DecideDistanceAttackable(data, creatureId, 35);
}

void COwnThing::BroadcastMessage(
    const OwnThingBroadcastType type, const eSMsgType argumentType, const char16_t* const argument, const bool argumentFirst
) {
    L2_GUARD_FUNCTION();
    int messageId = 0;
    switch (type) {
        case OTBT_BROKEN:
            messageId = 0xabe;
            break;
        case OTBT_DEAD:
            messageId = 0xabf;
            break;
        case OTBT_TAKEN:
            messageId = 0xb61;
            break;
        default:
            // Log.Add(LOG_ERROR, L"Unknown message for ownthing.")
            // L2_LOG_ERROR(Log, L"Unknown message for ownthing.")
            L2_LOG_ERROR(
                Log, "Cannot announce territory-ward event type %d: the type is unsupported; no announcement is sent.",
                static_cast<int>(type)
            );
            return;
    }
    CDominionManager::Inst()->SendDominionMessage(messageId, m_nOriginalResidenceID, argumentType, argument, argumentFirst);
}

void COwnThing::OnDestructed(CCreature* const attacker) {
    L2_GUARD_FUNCTION();
    User* const user = attacker != nullptr ? attacker->CastUser() : nullptr;
    if (user != nullptr && GiveToUser(user, true)) {
        BroadcastMessage(OTBT_BROKEN, EST_CHAR_NAME, user->GetName(), false);
        Disappear();
    }
}

void COwnThing::InsertOwnThingToDominion(CDominion* const dominion) {
    L2_GUARD_FUNCTION();
    if (dominion == nullptr) {
        return;
    }
    SetOwningResidence(dominion);
    dominion->AddOwnThing(this);
    SetPos(dominion->GetPos(this));
    const FVector position = GetPos();
    db.RequestSaveOwnthing(
        m_nOriginalResidenceID, m_nOwningResidenceID, static_cast<int>(position.x), static_cast<int>(position.y),
        static_cast<int>(position.z)
    );
}

bool COwnThing::IsEnemyTo(CCreature* const creature) {
    L2_GUARD_FUNCTION();
    User* const user = creature != nullptr ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        return true;
    }
    if (CDominionManager::Inst()->m_nState != DSST_DOMINION_SIEGE) {
        return false;
    }
    return user->m_data->m_nDominionSiegeId != m_nOwningResidenceID;
}

void COwnThing::SendOwnThingInfo(CUserSocket* const socket) {
    L2_GUARD_FUNCTION();
    class TLMFunc final : public CTLMFunc {
    public:
        explicit TLMFunc(COwnThing* const ownThing) : m_OwnThing(ownThing) {}

        bool CanDo(CObject* const object, double) override {
            if (object->IsPC() && !object->IsDeletingObject() && object->m_Deleted == 0) {
                m_OwnThing->SendOwnThingInfoImpl(object->CastUser());
            }
            return true;
        }

    private:
        COwnThing* m_OwnThing;
    };

    if (socket == nullptr) {
        CTLMFilterRect filter(static_cast<int>(GetInZoneID()), GetPos(), 2048.0);
        TLMFunc sender(this);
        FOR_ALL_NEIGHBOR_USER_RECT(&filter, &sender);
        return;
    }
    SendOwnThingInfoImpl(socket->GetUser());
}

void COwnThing::Regenerate(CDominion* const dominion, const FVector position) {
    L2_GUARD_FUNCTION();
    static_cast<void>(position);
    CDominion* const oldDominion =
        m_pOwningResidence != nullptr && m_pOwningResidence->IsDominion() ? m_pOwningResidence->CastDominion() : nullptr;
    if (oldDominion == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid original dominion pointer in regenerating ownthing.")
        // L2_LOG_ERROR(Log,
        //     L"Invalid original dominion pointer in regenerating ownthing.")
        L2_LOG_ERROR(
            Log,
            "Cannot regenerate the territory ward: its current owning residence is missing or is not a territory; "
            "regeneration is skipped."
        );
        return;
    }
    if (dominion == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid new dominion pointer in regenerating ownthing.")
        // L2_LOG_ERROR(Log,
        //     L"Invalid new dominion pointer in regenerating ownthing.")
        L2_LOG_ERROR(
            Log,
            "Cannot regenerate the ward from territory %d: the destination territory is missing; regeneration is skipped.",
            oldDominion->m_data != nullptr ? oldDominion->m_data->m_nResidenceID : 0
        );
        return;
    }

    oldDominion->RemoveOwnthing(this);
    InsertOwnThingToDominion(dominion);
    CSPointer<CPledge> ownerPledge(dominion->GetResidenceOwnerPledge(), ObjectRefType::FunctionChain);
    if (m_pUser != nullptr) {
        ownerPledge.SetObject(m_pUser->GetPledge(), ObjectRefType::FunctionChain);
        m_pUser->DeleteOwnThing(false, false);
    }
    Move(dominion->GetPos(this));
    if (ownerPledge) {
        BroadcastMessage(OTBT_TAKEN, EST_STRING, std::u16string(ownerPledge->GetPledgeName()).c_str(), true);
    }
    if (oldDominion->m_pMainCastle != nullptr) {
        CPledge* const castlePledge = oldDominion->m_pMainCastle->GetResidenceOwnerPledge();
        if (castlePledge != nullptr) {
            oldDominion->RemoveResidenceSkill(*castlePledge);
        }
    }
}

double COwnThing::GotDamageBy(CCreature* const attacker, double damage) {
    L2_GUARD_FUNCTION();
    if (!m_data->m_bBreakable) {
        return 0.0;
    }
    if (attacker != nullptr) {
        attacker->SendSystemMessage_d(SystemMessageId::DEALT_DAMAGE, static_cast<int>(damage));
    }
    {
        L2_GUARD(*m_lock);
        SetHP(GetHP() - damage);
        if (GetHP() <= 0.0) {
            SetHP(0.0);
            OnDestructed(attacker);
        }
    }
    if (GetHP() > 0.0) {
        SendOwnThingInfo(nullptr);
    }
    return damage;
}
