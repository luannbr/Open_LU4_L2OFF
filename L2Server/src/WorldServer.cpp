#include "AirShip.h"
#include "CConfig.h"
#include "CContributeData.h"
#include "CGeneralObject.h"
#include "CWorld.h"
#include "CWorldObject.h"
#include "Castle.h"
#include "Creature.h"
#include "DB.h"
#include "Door.h"
#include "Fortress.h"
#include "HolyThing.h"
#include "IOCPMessageServer.h"
#include "InstantZone.h"
#include "Item.h"
#include "MakerEvent.h"
#include "NPC.h"
#include "NewAction.h"
#include "NewActionHelper.h"
#include "NpcServer.h"
#include "ObjectDB.h"
#include "Party.h"
#include "Pet.h"
#include "Pledge.h"
#include "Protocol/src/L2Constants.h"
#include "Protocol/src/SystemMessageIds.h"
#include "SerializedMessage.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "SharedContainer.h"
#include "Shuttle.h"
#include "Skill.h"
#include "StaticObject.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserBroadcaster.h"
#include "UserSocket.h"
#include "Vehicle.h"
#include "WorldCollision.h"

#include "Portable/ClientProtocolRuntime.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/Exception.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/NativeTcpSocket.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/ShareDefine.h"
#include "Shared/src/ShareInfo.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/Territory.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/TimeAnalysis.h"
#include "Shared/src/World.h"
#include "Shared/src/platform/Runtime.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <new>
#include <string>
#include <utility>

thread_local char g_tlClanAttackPacket[0x5000]{};
thread_local char g_tlClanSpelledPacket[0x5000]{};


namespace {

int DensityCoordinate(const double coordinate, const std::uint32_t offset) {
    return runtime::AddInt32Wrapping(runtime::Win64CvttDoubleToInt32(coordinate), runtime::Int32FromBits(offset)) /
        0x800;
}

int WorldCoordinate(double coordinate) noexcept { return runtime::Win64CvttDoubleToInt32(coordinate); }

std::int32_t NpcAttackEventDamage(double damage, double builderBonus) noexcept {
    if (damage > 0.0 && builderBonus > 1.0) {
        // GM behavior change: the original attack event ignored dpsbonus even
        // though GotDamageBy applied it to HP damage. The bonus therefore did
        // not contribute to damage-driven AI such as Istina's ballista.
        // L2NPC converts this integer through float before storing it as int.
        // INT32_MAX would round to 2^31 there. Saturate at the largest safe float.
        constexpr double maximumNpcDamage = 2147483520.0;
        damage = std::min(damage * builderBonus, maximumNpcDamage);
    }
    return runtime::Win64CvttDoubleToInt32(damage);
}

constexpr std::int32_t ArithmeticShiftRight15(const std::int32_t value) noexcept {
    const std::uint32_t bits = static_cast<std::uint32_t>(value);
    const std::uint32_t signFill = (bits & UINT32_C(0x80000000)) != 0 ? UINT32_C(0xfffe0000) : UINT32_C(0);
    return runtime::Int32FromBits((bits >> 15U) | signFill);
}

std::int32_t WorldMapCoordinate(const double coordinate, const std::int32_t offset) noexcept {
    return ArithmeticShiftRight15(runtime::AddInt32Wrapping(runtime::Win64CvttDoubleToInt32(coordinate), offset)) + 10;
}

void CopyTargetAccountName(std::array<wchar_t, 15>& destination, const std::array<char16_t, 15>& source) noexcept {
    for (std::size_t index = 0; index < source.size(); ++index) {
        destination[index] = static_cast<wchar_t>(source[index]);
        if (source[index] == u'\0') {
            break;
        }
    }
}

// Kept out of FOR_ALL_OBJECT_IN_OBJECT_MAP: MSVC rejects directives in macro arguments.
void CopyReportName(std::array<wchar_t, 256>& destination, const std::wstring& source) noexcept {
#if defined(_WIN32)
    ::wcscpy_s(destination.data(), destination.size(), source.c_str());
#else
    std::wcscpy(destination.data(), source.c_str());
#endif
}

void OnAttackMissHappened(CCreature& attacker, CObject& target) {
    // Current L2Server 0x00c58c34.
    L2_GUARD_FUNCTION();

    if (target.IsCreature()) {
        CCreature* const targetCreature = target.CastCreature();
        targetCreature->TriggerSkillByAvoid(attacker);
        targetCreature->TriggerAlterSkill(AlterSkill::Avoid, ABNS_INVALID);
    }

    if (attacker.IsPC()) {
        User* const user = attacker.CastUser();
        user->GetSocket()->Send("chccS", 0x62, SystemMessageId::CHARACTER_ATTACK_MISSED, 1, 0xc, user->GetName());
    } else if (User* const owner = attacker.GetUserOrMaster()) {
        owner->GetSocket()->Send("chccd", 0x62, SystemMessageId::CHARACTER_ATTACK_MISSED, 1, 2, attacker.GetClassId());
    }

    User* const targetOwner = target.GetUserOrMaster();
    if (targetOwner == nullptr) {
        return;
    }

    if (!target.IsPC()) {
        if (attacker.IsPC()) {
            User* const attackerUser = attacker.CastUser();
            targetOwner->GetSocket()->Send(
                "chccdcS", 0x62, SystemMessageId::CHARACTER_EVADED_CHARACTER_ATTACK, 2, 2, target.GetClassId(), 0xc,
                attackerUser->GetName2(targetOwner)
            );
        } else {
            targetOwner->GetSocket()->Send(
                "chccdcd", 0x62, SystemMessageId::CHARACTER_EVADED_CHARACTER_ATTACK, 2, 2, target.GetClassId(), 2,
                attacker.GetClassId()
            );
        }
        return;
    }

    User* const targetUser = target.CastUser();
    if (!attacker.IsPC()) {
        targetUser->GetSocket()->Send(
            "chccScd", 0x62, SystemMessageId::CHARACTER_EVADED_CHARACTER_ATTACK, 2, 0xc, targetUser->GetName(), 2,
            attacker.GetClassId()
        );
    } else {
        User* const attackerUser = attacker.CastUser();
        targetUser->GetSocket()->Send(
            "chccScS", 0x62, SystemMessageId::CHARACTER_EVADED_CHARACTER_ATTACK, 2, 0xc, targetUser->GetName(), 0xc,
            attackerUser->GetName2(targetUser)
        );
    }
}

} // namespace

CWorld::__async_arg_AsyncStaticObjectAppear::__async_arg_AsyncStaticObjectAppear(
    __async_arg_AsyncStaticObjectAppear* other
) :
    __a1(other->__a1), __a2(other->__a2) {}

CWorld::__async_arg_AsyncStaticObjectAppear::__async_arg_AsyncStaticObjectAppear() {}

CWorld::__async_arg_AsyncStaticObjectAppear::__async_arg_AsyncStaticObjectAppear(CStaticObject* a1, FVector a2) :
    __a1(a1), __a2(a2) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_2(CWorld, __async_arg_AsyncStaticObjectAppear)

CWorld::__async_arg_AsyncStaticObjectDisappear::__async_arg_AsyncStaticObjectDisappear(
    __async_arg_AsyncStaticObjectDisappear* other
) :
    __a1(other->__a1) {}

CWorld::__async_arg_AsyncStaticObjectDisappear::__async_arg_AsyncStaticObjectDisappear() {}

CWorld::__async_arg_AsyncStaticObjectDisappear::__async_arg_AsyncStaticObjectDisappear(CStaticObject* a1) : __a1(a1) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_1(CWorld, __async_arg_AsyncStaticObjectDisappear)

CWorld::__async_arg_AsyncLoadStaticObject::__async_arg_AsyncLoadStaticObject(__async_arg_AsyncLoadStaticObject* other) :
    __a1(other->__a1), __a2(other->__a2) {}

CWorld::__async_arg_AsyncLoadStaticObject::__async_arg_AsyncLoadStaticObject() {}

CWorld::__async_arg_AsyncLoadStaticObject::__async_arg_AsyncLoadStaticObject(CStaticObject* a1, CAirShip* a2) :
    __a1(a1), __a2(a2) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_2(CWorld, __async_arg_AsyncLoadStaticObject)

CWorld::__async_arg_AsyncUnLoadStaticObject::__async_arg_AsyncUnLoadStaticObject(
    __async_arg_AsyncUnLoadStaticObject* other
) :
    __a1(other->__a1), __a2(other->__a2) {}

CWorld::__async_arg_AsyncUnLoadStaticObject::__async_arg_AsyncUnLoadStaticObject() {}

CWorld::__async_arg_AsyncUnLoadStaticObject::__async_arg_AsyncUnLoadStaticObject(CStaticObject* a1, CAirShip* a2) :
    __a1(a1), __a2(a2) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_2(CWorld, __async_arg_AsyncUnLoadStaticObject)

CWorld::__async_arg_AsyncMove::__async_arg_AsyncMove(__async_arg_AsyncMove* other) :
    __a1(other->__a1), __a2(other->__a2), __a3(other->__a3) {}

CWorld::__async_arg_AsyncMove::__async_arg_AsyncMove() {}

CWorld::__async_arg_AsyncMove::__async_arg_AsyncMove(CCreature* a1, CContainer* a2, bool a3) :
    __a1(a1), __a2(a2), __a3(a3) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_3(CWorld, __async_arg_AsyncMove)

CWorld::__async_arg_AsyncLeaveWorld::__async_arg_AsyncLeaveWorld(__async_arg_AsyncLeaveWorld* other) :
    __a1(other->__a1), __a2(other->__a2), __a3(other->__a3) {}

CWorld::__async_arg_AsyncLeaveWorld::__async_arg_AsyncLeaveWorld() {}

CWorld::__async_arg_AsyncLeaveWorld::__async_arg_AsyncLeaveWorld(CCreature* a1, FVector a2, FVector a3) :
    __a1(a1), __a2(a2), __a3(a3) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_3(CWorld, __async_arg_AsyncLeaveWorld)

CWorld::__async_arg_AsyncEnterWorld::__async_arg_AsyncEnterWorld(__async_arg_AsyncEnterWorld* other) :
    __a1(other->__a1), __a2(other->__a2), __a3(other->__a3), __a4(other->__a4) {}

CWorld::__async_arg_AsyncEnterWorld::__async_arg_AsyncEnterWorld() {}

CWorld::__async_arg_AsyncEnterWorld::__async_arg_AsyncEnterWorld(CCreature* a1, FVector a2, unsigned int a3, bool a4) :
    __a1(a1), __a2(a2), __a3(a3), __a4(a4) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_4(CWorld, __async_arg_AsyncEnterWorld)

CWorld::__async_arg_AsyncMoveToZone::__async_arg_AsyncMoveToZone(__async_arg_AsyncMoveToZone* other) :
    __a1(other->__a1), __a2(other->__a2), __a3(other->__a3) {}

CWorld::__async_arg_AsyncMoveToZone::__async_arg_AsyncMoveToZone() {}

CWorld::__async_arg_AsyncMoveToZone::__async_arg_AsyncMoveToZone(CCreature* a1, FVector a2, unsigned int a3) :
    __a1(a1), __a2(a2), __a3(a3) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_3(CWorld, __async_arg_AsyncMoveToZone)

CWorld::__async_arg_AsyncGetOnVehicle::__async_arg_AsyncGetOnVehicle(__async_arg_AsyncGetOnVehicle* other) :
    __a1(other->__a1), __a2(other->__a2), __a3(other->__a3) {}

CWorld::__async_arg_AsyncGetOnVehicle::__async_arg_AsyncGetOnVehicle() {}

CWorld::__async_arg_AsyncGetOnVehicle::__async_arg_AsyncGetOnVehicle(CCreature* a1, CVehicle* a2, FVector a3) :
    __a1(a1), __a2(a2), __a3(a3) {}

// Current L2Server 0x00c580f0.
L2_ASYNC_DEFINE_FIELDWISE_COPY_3(CWorld, __async_arg_AsyncGetOnVehicle)

CWorld::__async_arg_AsyncGetOffVehicle::__async_arg_AsyncGetOffVehicle(__async_arg_AsyncGetOffVehicle* other) :
    __a1(other->__a1), __a2(other->__a2), __a3(other->__a3) {}

CWorld::__async_arg_AsyncGetOffVehicle::__async_arg_AsyncGetOffVehicle() {}

CWorld::__async_arg_AsyncGetOffVehicle::__async_arg_AsyncGetOffVehicle(CCreature* a1, CVehicle* a2, FVector a3) :
    __a1(a1), __a2(a2), __a3(a3) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_3(CWorld, __async_arg_AsyncGetOffVehicle)

CWorld::__async_arg_AsyncGetOffAirShip::__async_arg_AsyncGetOffAirShip(__async_arg_AsyncGetOffAirShip* other) :
    __a1(other->__a1), __a2(other->__a2), __a3(other->__a3) {}

CWorld::__async_arg_AsyncGetOffAirShip::__async_arg_AsyncGetOffAirShip() {}

CWorld::__async_arg_AsyncGetOffAirShip::__async_arg_AsyncGetOffAirShip(CCreature* a1, CAirShip* a2, FVector a3) :
    __a1(a1), __a2(a2), __a3(a3) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_3(CWorld, __async_arg_AsyncGetOffAirShip)

CWorld::__async_arg_AsyncGetOnAirShip::__async_arg_AsyncGetOnAirShip(__async_arg_AsyncGetOnAirShip* other) :
    __a1(other->__a1), __a2(other->__a2) {}

CWorld::__async_arg_AsyncGetOnAirShip::__async_arg_AsyncGetOnAirShip() {}

CWorld::__async_arg_AsyncGetOnAirShip::__async_arg_AsyncGetOnAirShip(CCreature* a1, CAirShip* a2) : __a1(a1), __a2(a2) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_2(CWorld, __async_arg_AsyncGetOnAirShip)

CWorld::__async_arg_AsyncAirShipEnterWorld::__async_arg_AsyncAirShipEnterWorld(__async_arg_AsyncAirShipEnterWorld* other) :
    __a1(other->__a1), __a2(other->__a2) {}

CWorld::__async_arg_AsyncAirShipEnterWorld::__async_arg_AsyncAirShipEnterWorld() {}

CWorld::__async_arg_AsyncAirShipEnterWorld::__async_arg_AsyncAirShipEnterWorld(CAirShip* a1, FVector a2) :
    __a1(a1), __a2(a2) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_2(CWorld, __async_arg_AsyncAirShipEnterWorld)

CWorld::__async_arg_AsyncAirShipLeaveWorld::__async_arg_AsyncAirShipLeaveWorld(__async_arg_AsyncAirShipLeaveWorld* other) :
    __a1(other->__a1) {}

CWorld::__async_arg_AsyncAirShipLeaveWorld::__async_arg_AsyncAirShipLeaveWorld() {}

CWorld::__async_arg_AsyncAirShipLeaveWorld::__async_arg_AsyncAirShipLeaveWorld(CAirShip* a1) : __a1(a1) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_1(CWorld, __async_arg_AsyncAirShipLeaveWorld)

CWorld::__async_arg_AsyncAirShipTeleport::__async_arg_AsyncAirShipTeleport(__async_arg_AsyncAirShipTeleport* other) :
    __a1(other->__a1), __a2(other->__a2), __a3(other->__a3), __a4(other->__a4) {}

CWorld::__async_arg_AsyncAirShipTeleport::__async_arg_AsyncAirShipTeleport() {}

CWorld::__async_arg_AsyncAirShipTeleport::__async_arg_AsyncAirShipTeleport(
    CAirShip* a1, FVector a2, unsigned int a3, int a4
) :
    __a1(a1), __a2(a2), __a3(a3), __a4(a4) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_4(CWorld, __async_arg_AsyncAirShipTeleport)

CWorld::__async_arg_AsyncAppearIntoAirShip::__async_arg_AsyncAppearIntoAirShip(__async_arg_AsyncAppearIntoAirShip* other) :
    __a1(other->__a1), __a2(other->__a2) {}

CWorld::__async_arg_AsyncAppearIntoAirShip::__async_arg_AsyncAppearIntoAirShip() {}

CWorld::__async_arg_AsyncAppearIntoAirShip::__async_arg_AsyncAppearIntoAirShip(User* a1, CAirShip* a2) :
    __a1(a1), __a2(a2) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_2(CWorld, __async_arg_AsyncAppearIntoAirShip)

CWorld::__async_arg_AsyncGetOnShuttle::__async_arg_AsyncGetOnShuttle(__async_arg_AsyncGetOnShuttle* other) :
    __a1(other->__a1), __a2(other->__a2), __a3(other->__a3) {}

CWorld::__async_arg_AsyncGetOnShuttle::__async_arg_AsyncGetOnShuttle() {}

CWorld::__async_arg_AsyncGetOnShuttle::__async_arg_AsyncGetOnShuttle(CCreature* a1, CShuttle* a2, FVector a3) :
    __a1(a1), __a2(a2), __a3(a3) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_3(CWorld, __async_arg_AsyncGetOnShuttle)

CWorld::__async_arg_AsyncGetOffShuttle::__async_arg_AsyncGetOffShuttle(__async_arg_AsyncGetOffShuttle* other) :
    __a1(other->__a1), __a2(other->__a2), __a3(other->__a3) {}

CWorld::__async_arg_AsyncGetOffShuttle::__async_arg_AsyncGetOffShuttle() {}

CWorld::__async_arg_AsyncGetOffShuttle::__async_arg_AsyncGetOffShuttle(CCreature* a1, CShuttle* a2, FVector a3) :
    __a1(a1), __a2(a2), __a3(a3) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_3(CWorld, __async_arg_AsyncGetOffShuttle)

CWorld::__async_arg_AsyncPutItem::__async_arg_AsyncPutItem(__async_arg_AsyncPutItem* other) :
    __a1(other->__a1), __a2(other->__a2), __a3(other->__a3), __a4(other->__a4) {}

CWorld::__async_arg_AsyncPutItem::__async_arg_AsyncPutItem() {}

CWorld::__async_arg_AsyncPutItem::__async_arg_AsyncPutItem(CCreature* a1, CItem* a2, FVector a3, CContainer* a4) :
    __a1(a1), __a2(a2), __a3(a3), __a4(a4) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_4(CWorld, __async_arg_AsyncPutItem)

CWorld::__async_arg_AsyncShuttleEnterWorld::__async_arg_AsyncShuttleEnterWorld(__async_arg_AsyncShuttleEnterWorld* other) :
    __a1(other->__a1), __a2(other->__a2) {}

CWorld::__async_arg_AsyncShuttleEnterWorld::__async_arg_AsyncShuttleEnterWorld() {}

CWorld::__async_arg_AsyncShuttleEnterWorld::__async_arg_AsyncShuttleEnterWorld(CShuttle* a1, FVector a2) :
    __a1(a1), __a2(a2) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_2(CWorld, __async_arg_AsyncShuttleEnterWorld)

CWorld::__async_arg_AsyncShuttleLeaveWorld::__async_arg_AsyncShuttleLeaveWorld(__async_arg_AsyncShuttleLeaveWorld* other) :
    __a1(other->__a1) {}

CWorld::__async_arg_AsyncShuttleLeaveWorld::__async_arg_AsyncShuttleLeaveWorld() {}

CWorld::__async_arg_AsyncShuttleLeaveWorld::__async_arg_AsyncShuttleLeaveWorld(CShuttle* a1) : __a1(a1) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_1(CWorld, __async_arg_AsyncShuttleLeaveWorld)

CWorld::__async_arg_AsyncShuttleMove::__async_arg_AsyncShuttleMove(__async_arg_AsyncShuttleMove* other) :
    __a1(other->__a1), __a2(other->__a2) {}

CWorld::__async_arg_AsyncShuttleMove::__async_arg_AsyncShuttleMove() {}

CWorld::__async_arg_AsyncShuttleMove::__async_arg_AsyncShuttleMove(CShuttle* a1, FVector a2) : __a1(a1), __a2(a2) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_2(CWorld, __async_arg_AsyncShuttleMove)

CWorld::__async_arg_AsyncVehicleEnterWorld::__async_arg_AsyncVehicleEnterWorld(__async_arg_AsyncVehicleEnterWorld* other) :
    __a1(other->__a1), __a2(other->__a2) {}

CWorld::__async_arg_AsyncVehicleEnterWorld::__async_arg_AsyncVehicleEnterWorld() {}

CWorld::__async_arg_AsyncVehicleEnterWorld::__async_arg_AsyncVehicleEnterWorld(CVehicle* a1, FVector a2) :
    __a1(a1), __a2(a2) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_2(CWorld, __async_arg_AsyncVehicleEnterWorld)

CWorld::__async_arg_AsyncVehicleLeaveWorld::__async_arg_AsyncVehicleLeaveWorld(__async_arg_AsyncVehicleLeaveWorld* other) :
    __a1(other->__a1) {}

CWorld::__async_arg_AsyncVehicleLeaveWorld::__async_arg_AsyncVehicleLeaveWorld() {}

CWorld::__async_arg_AsyncVehicleLeaveWorld::__async_arg_AsyncVehicleLeaveWorld(CVehicle* a1) : __a1(a1) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_1(CWorld, __async_arg_AsyncVehicleLeaveWorld)

CWorld::__async_arg_AsyncVehicleMove::__async_arg_AsyncVehicleMove(__async_arg_AsyncVehicleMove* other) :
    __a1(other->__a1), __a2(other->__a2) {}

CWorld::__async_arg_AsyncVehicleMove::__async_arg_AsyncVehicleMove() {}

CWorld::__async_arg_AsyncVehicleMove::__async_arg_AsyncVehicleMove(CVehicle* a1, FVector a2) : __a1(a1), __a2(a2) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_2(CWorld, __async_arg_AsyncVehicleMove)

CWorld::__async_arg_AsyncVehicleKickIllegal::__async_arg_AsyncVehicleKickIllegal(
    __async_arg_AsyncVehicleKickIllegal* other
) :
    __a1(other->__a1), __a2(other->__a2), __a3(other->__a3) {}

CWorld::__async_arg_AsyncVehicleKickIllegal::__async_arg_AsyncVehicleKickIllegal() {}

CWorld::__async_arg_AsyncVehicleKickIllegal::__async_arg_AsyncVehicleKickIllegal(CVehicle* a1, int a2, FVector a3) :
    __a1(a1), __a2(a2), __a3(a3) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_3(CWorld, __async_arg_AsyncVehicleKickIllegal)

CWorld::__async_arg_AsyncVehicleInformVehicleStart::__async_arg_AsyncVehicleInformVehicleStart(
    __async_arg_AsyncVehicleInformVehicleStart* other
) :
    __a1(other->__a1), __a2(other->__a2) {}

CWorld::__async_arg_AsyncVehicleInformVehicleStart::__async_arg_AsyncVehicleInformVehicleStart() {}

CWorld::__async_arg_AsyncVehicleInformVehicleStart::__async_arg_AsyncVehicleInformVehicleStart(CVehicle* a1, bool a2) :
    __a1(a1), __a2(a2) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_2(CWorld, __async_arg_AsyncVehicleInformVehicleStart)

CWorld::__async_arg_AsyncVehicleKickTransformer::__async_arg_AsyncVehicleKickTransformer(
    __async_arg_AsyncVehicleKickTransformer* other
) :
    __a1(other->__a1) {}

CWorld::__async_arg_AsyncVehicleKickTransformer::__async_arg_AsyncVehicleKickTransformer() {}

CWorld::__async_arg_AsyncVehicleKickTransformer::__async_arg_AsyncVehicleKickTransformer(CVehicle* a1) : __a1(a1) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_1(CWorld, __async_arg_AsyncVehicleKickTransformer)

CWorld::__async_arg_AsyncAirShipMove::__async_arg_AsyncAirShipMove(__async_arg_AsyncAirShipMove* other) :
    __a1(other->__a1), __a2(other->__a2) {}

CWorld::__async_arg_AsyncAirShipMove::__async_arg_AsyncAirShipMove() {}

CWorld::__async_arg_AsyncAirShipMove::__async_arg_AsyncAirShipMove(CAirShip* a1, CContainer* a2) : __a1(a1), __a2(a2) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_2(CWorld, __async_arg_AsyncAirShipMove)

CWorld::__async_arg_AsyncDoorStatusUpdate::__async_arg_AsyncDoorStatusUpdate(__async_arg_AsyncDoorStatusUpdate* other) :
    __a1(other->__a1) {}

CWorld::__async_arg_AsyncDoorStatusUpdate::__async_arg_AsyncDoorStatusUpdate() {}

CWorld::__async_arg_AsyncDoorStatusUpdate::__async_arg_AsyncDoorStatusUpdate(CDoor* a1) : __a1(a1) {}

// Current L2Server 0x00c58378.
L2_ASYNC_DEFINE_FIELDWISE_COPY_1(CWorld, __async_arg_AsyncDoorStatusUpdate)

CWorld::__async_arg_AsyncDoorOpenClose::__async_arg_AsyncDoorOpenClose(__async_arg_AsyncDoorOpenClose* other) :
    __a1(other->__a1), __a2(other->__a2), __a3(other->__a3) {}

CWorld::__async_arg_AsyncDoorOpenClose::__async_arg_AsyncDoorOpenClose() {}

CWorld::__async_arg_AsyncDoorOpenClose::__async_arg_AsyncDoorOpenClose(CDoor* a1, bool a2, bool a3) :
    __a1(a1), __a2(a2), __a3(a3) {}

// Current L2Server 0x00c58390.
L2_ASYNC_DEFINE_FIELDWISE_COPY_3(CWorld, __async_arg_AsyncDoorOpenClose)

CWorld::__async_arg_AsyncShuttleKickFromDoor::__async_arg_AsyncShuttleKickFromDoor(
    __async_arg_AsyncShuttleKickFromDoor* other
) :
    __a1(other->__a1), __a2(other->__a2) {}

CWorld::__async_arg_AsyncShuttleKickFromDoor::__async_arg_AsyncShuttleKickFromDoor() {}

CWorld::__async_arg_AsyncShuttleKickFromDoor::__async_arg_AsyncShuttleKickFromDoor(CDoor* a1, FVector a2) :
    __a1(a1), __a2(a2) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_2(CWorld, __async_arg_AsyncShuttleKickFromDoor)

CWorld::__async_arg_AsyncPickAndDeleteItem::__async_arg_AsyncPickAndDeleteItem(__async_arg_AsyncPickAndDeleteItem* other) :
    __a1(other->__a1) {}

CWorld::__async_arg_AsyncPickAndDeleteItem::__async_arg_AsyncPickAndDeleteItem() {}

CWorld::__async_arg_AsyncPickAndDeleteItem::__async_arg_AsyncPickAndDeleteItem(CItem* a1) : __a1(a1) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_1(CWorld, __async_arg_AsyncPickAndDeleteItem)

CWorld::__async_arg_AsyncPickItem::__async_arg_AsyncPickItem(__async_arg_AsyncPickItem* other) : __a1(other->__a1) {}

CWorld::__async_arg_AsyncPickItem::__async_arg_AsyncPickItem() {}

CWorld::__async_arg_AsyncPickItem::__async_arg_AsyncPickItem(CItem* a1) : __a1(a1) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_1(CWorld, __async_arg_AsyncPickItem)

CWorld::__async_arg_AsyncAddItemToInventory::__async_arg_AsyncAddItemToInventory(
    __async_arg_AsyncAddItemToInventory* other
) :
    __a1(other->__a1), __a2(other->__a2) {}

CWorld::__async_arg_AsyncAddItemToInventory::__async_arg_AsyncAddItemToInventory() {}

CWorld::__async_arg_AsyncAddItemToInventory::__async_arg_AsyncAddItemToInventory(CItem* a1, CCreature* a2) :
    __a1(a1), __a2(a2) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_2(CWorld, __async_arg_AsyncAddItemToInventory)

CWorld::__async_arg_AsyncAddItemToInventoryWithparams::__async_arg_AsyncAddItemToInventoryWithparams(
    __async_arg_AsyncAddItemToInventoryWithparams* other
) :
    __a1(other->__a1), __a2(other->__a2), __a3(other->__a3) {}

CWorld::__async_arg_AsyncAddItemToInventoryWithparams::__async_arg_AsyncAddItemToInventoryWithparams() {}

CWorld::__async_arg_AsyncAddItemToInventoryWithparams::__async_arg_AsyncAddItemToInventoryWithparams(
    CCreature* a1, int a2, std::int64_t a3
) :
    __a1(a1), __a2(a2), __a3(a3) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_3(CWorld, __async_arg_AsyncAddItemToInventoryWithparams)

CWorld::__async_arg_AsyncDeleteInZoneItem::__async_arg_AsyncDeleteInZoneItem(__async_arg_AsyncDeleteInZoneItem* other) :
    __a1(other->__a1) {}

CWorld::__async_arg_AsyncDeleteInZoneItem::__async_arg_AsyncDeleteInZoneItem() {}

CWorld::__async_arg_AsyncDeleteInZoneItem::__async_arg_AsyncDeleteInZoneItem(CItem* a1) : __a1(a1) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_1(CWorld, __async_arg_AsyncDeleteInZoneItem)

CWorld::CWorld(bool isTestWorld) :
    m_AdenaDropByNpcs(0), m_AdenaDropByNpcsOld(0), m_IsTestWorld(isTestWorld), padding_16000069{}, m_geo(nullptr) {
    L2_GUARD_FUNCTION();
    // Noise: Constructing the world object does not mean that geodata or
    // the world is ready. The startup coordinator reports loading progress.
    // runtime::log::WriteLogFormatterLine(
    //     Log,
    //     LOG_NORMAL,
    //     u"CWorld::Inst() Created")
    Serializer.SetOwnerName(u"World");
}

CWorld::~CWorld() {
    if (m_geo && !m_IsTestWorld) {
        delete m_geo;
    }
}

CWorld& CWorld::Inst() {
    // The current image uses an unguarded bit local-static. C++ local-static
    // synchronization is the selected cross-platform safety difference.
    static CWorld world(false);
    return world;
}

void CWorld::ContainerMove(CCreature& creature, CContainer* destination, bool force) {
    L2_GUARD_FUNCTION();
    L2_ADD_REF(&(creature), ObjectRefType::SerializedWorldThread);

    CWorld& world = Inst();
    __async_arg_AsyncMove argument(&creature, destination, force);
    world.Serializer.AddExecutable(
        &world, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncMove(pad); }, &argument, L"AsyncMove"
    );
}

bool CWorld::MoveTo(CCreature* creature, FVector position, bool force) {
    L2_GUARD_FUNCTION();
    L2_DECLARE_AUTO_TIMER(timer, JOB_WORLD_MOVETO);

    if (creature == nullptr || !creature->m_data->m_bIsActive) {
        return false;
    }

    const FVector currentPosition = creature->GetPos();
    const FVector delta{
        position.x - currentPosition.x,
        position.y - currentPosition.y,
        position.z - currentPosition.z,
    };
    if (delta.IsZero()) {
        return true;
    }

    if (!Inst().IsValidPosition(WorldCoordinate(position.x), WorldCoordinate(position.y))) {
        return false;
    }

    CContainer* const source = CContainerDB::Inst().GetContainer(creature->m_data->container_index);
    if (source == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"pContFrom is null index[%d] while "
        //     L"(%d,%d,%d)->(%d,%d,%d) creature[%s] "
        //     L"at file[%s], line[%d]",
        //     creature->m_data->container_index,
        //     WorldCoordinate(currentPosition.x),
        //     WorldCoordinate(currentPosition.y),
        //     WorldCoordinate(currentPosition.z),
        //     WorldCoordinate(position.x),
        //     WorldCoordinate(position.y),
        //     WorldCoordinate(position.z),
        //     creature->GetName(),
        //     L"world_server.cpp",
        //     0x84f)
        // L2_LOG_ERROR(Log,
        //     L"[world movement] Character '%s' has missing source container %d while moving from (%d, %d, %d) to (%d, %d,
        //     %d). Move rejected.", creature->GetName(), creature->m_data->container_index,
        //     WorldCoordinate(currentPosition.x),
        //     WorldCoordinate(currentPosition.y),
        //     WorldCoordinate(currentPosition.z),
        //     WorldCoordinate(position.x),
        //     WorldCoordinate(position.y),
        //     WorldCoordinate(position.z))
        L2_LOG_ERROR(
            Log,
            u"[world movement] Character '%s' has missing source container %d while moving from (%d, %d, %d) to (%d, %d, "
            u"%d); move rejected.",
            creature->GetName(), creature->m_data->container_index, WorldCoordinate(currentPosition.x),
            WorldCoordinate(currentPosition.y), WorldCoordinate(currentPosition.z), WorldCoordinate(position.x),
            WorldCoordinate(position.y), WorldCoordinate(position.z)
        );
        runtime::process::ReportCallStack(2, false);
        return false;
    }

    if (creature->m_data->IsFlying() && !creature->IsRidingWyvern()) {
        const int height = WorldCoordinate(position.z);
        if (height <= -1000) {
            position.z = -1000.0;
        } else if (height >= 6000) {
            position.z = 6000.0;
        }
    }

    if (!source->MoveTo(*creature, position, force)) {
        return false;
    }

    std::int32_t expected = 0;
    if (creature->PrepareLookNeighbor.compare_exchange_strong(expected, std::int32_t{1})) {
        if (creature->m_vPrevLookPos.DistCube(position) > 22500.0) {
            auto* const message = new CSerializedLookNeighborMessage(creature, false, creature->m_vPrevLookPos);
            creature->PushSerializedMessage(message);
            creature->m_vPrevLookPos = position;
        }
        creature->PrepareLookNeighbor.store(0);
    }
    return true;
}

void CWorld::AsyncMoveToZone(CCreature* creature, FVector position, unsigned int inZoneId) {
    __async_arg_AsyncMoveToZone argument(creature, position, inZoneId);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncMoveToZone(pad); }, &argument,
        L"AsyncMoveToZone"
    );
}

bool CWorld::MoveToZone(CCreature* creature, const FVector& position, unsigned int inZoneId) {
    L2_GUARD_FUNCTION();
    L2_DECLARE_AUTO_TIMER(timer, JOB_WORLD_MOVETOZONE);

    if (creature == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "creature null at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0x888)
        L2_LOG_ERROR(Log, "[world movement] Move-to-zone request has no creature; request rejected.");
        return false;
    }
    if (creature->m_data == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "creature data null at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0x88e)
        L2_LOG_ERROR(Log, "[world movement] Creature %u has no shared data; move-to-zone request rejected.", creature->Id());
        return false;
    }
    if (!Inst().IsValidPosition(WorldCoordinate(position.x), WorldCoordinate(position.y))) {
        // Log.Add(
        //     LOG_ERROR,
        //     "invalid position (%d, %d) at file[%s], line[%d]",
        //     WorldCoordinate(position.x),
        //     WorldCoordinate(position.y),
        //     "world_server.cpp",
        //     0x894)
        L2_LOG_ERROR(
            Log,
            "[world movement] Destination (%d, %d) is outside the world; move-to-zone request for creature %u rejected.",
            WorldCoordinate(position.x), WorldCoordinate(position.y), creature->Id()
        );
        return false;
    }
    if (creature->GetInZoneID() == inZoneId) {
        // Log.Add(
        //     LOG_ERROR,
        //     "same container id, can't move file[%s], line[%d]",
        //     "world_server.cpp",
        //     0x89a)
        L2_LOG_ERROR(
            Log, "[world movement] Creature %u is already in instance zone %u; redundant zone move rejected.",
            creature->Id(), inZoneId
        );
        return false;
    }

    if (inZoneId != 0) {
        CGeneralObject* const object = CGeneralObject::GetObjectW(inZoneId);
        CInstantZone* const instantZone = object != nullptr && object->IsInZone() ? object->CastInZone() : nullptr;
        if (instantZone == nullptr || instantZone->GetContainer() == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     "invalid appearing zone id %d at file[%s], line[%d]",
            //     inZoneId,
            //     "world_server.cpp",
            //     0x8a0)
            L2_LOG_ERROR(
                Log, "[world movement] Instance zone %u has no usable world container; move for creature %u rejected.",
                inZoneId, creature->Id()
            );
            return false;
        }
    }

    if (!creature->m_data->m_bIsActive) {
        // Log.Add(
        //     LOG_ERROR,
        //     "m_bIsActive is fail[%s], line[%d]",
        //     "world_server.cpp",
        //     0x8a7)
        L2_LOG_ERROR(Log, "[world movement] Creature %u is inactive; move-to-zone request rejected.", creature->Id());
        return false;
    }

    L2_ADD_REF(creature, ObjectRefType::SerializedWorldThread);
    AsyncMoveToZone(creature, position, inZoneId);
    return true;
}

bool CWorld::MoveToRelatively(CCreature* creature, CWorldObject* relativeObject, FVector position) {
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "creature null at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0xa4d)
        L2_LOG_ERROR(Log, "[world movement] Relative-move request has no creature; request rejected.");
        return false;
    }
    if (relativeObject == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "pRelativeObject null at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0xa53)
        L2_LOG_ERROR(
            Log, "[world movement] Relative-move request for creature %u has no reference object; request rejected.",
            creature->Id()
        );
        return false;
    }
    if (creature->m_data == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "creature data null at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0xa59)
        L2_LOG_ERROR(
            Log, "[world movement] Creature %u has no shared data; relative-move request rejected.", creature->Id()
        );
        return false;
    }
    if (!creature->m_data->m_bIsActive) {
        return false;
    }

    const FVector absolute = relativeObject->ConvRelToAbs(position);
    const bool moved = MoveTo(creature, absolute, false);
    creature->SetRelPos(position);
    return moved;
}

bool CWorld::CanGo(
    const FVector& source,
    FVector& destination,
    FVector& outputPosition,
    bool& distanceExhausted,
    double& travelledDistance,
    int inZoneId,
    bool floating
) {
    L2_GUARD_FUNCTION();

    bool result = false;
    if (!floating) {
        destination.z = static_cast<double>(Inst().GetGeo()->FindGround(
            WorldCoordinate(destination.x), WorldCoordinate(destination.y), WorldCoordinate(destination.z + 8.0)
        ));
    }

    if (IsValidPosition(WorldCoordinate(destination.x), WorldCoordinate(destination.y))) {
        result = Inst().GetGeo()->MoveStraightTest(
            source, destination, outputPosition, travelledDistance, distanceExhausted, 4096.0, floating, 0
        );
        if (!result) {
            destination = outputPosition;
        }
        if (!Inst().GetGeo()->CanSee(source, destination, inZoneId, true)) {
            travelledDistance = 0.0;
            result = false;
        }
    }
    return result;
}

bool CWorld::CanGo(const FVector& source, FVector& destination, int inZoneId, bool floating) {
    L2_GUARD_FUNCTION();
    FVector outputPosition{};
    bool distanceExhausted = false;
    double travelledDistance = 0.0;
    return CanGo(source, destination, outputPosition, distanceExhausted, travelledDistance, inZoneId, floating);
}

bool CWorld::FindAcceptableDestZPos(const FVector& source, FVector& destination, const bool floating) {
    // Current L2Server 0x00c5bd74.
    L2_GUARD_FUNCTION();

    FVector probe = destination;
    const int verticalAllowance = floating ? 0x20 : 0x70;
    probe.z = static_cast<double>(WorldCoordinate(source.z) + verticalAllowance);
    const int ground =
        Inst().GetGeo()->FindGround(WorldCoordinate(probe.x), WorldCoordinate(probe.y), WorldCoordinate(probe.z));
    const int destinationZ = WorldCoordinate(destination.z);
    if (destinationZ + verticalAllowance < ground) {
        return false;
    }
    if (destinationZ < ground || (ground < destinationZ && !floating)) {
        destination.z = static_cast<double>(ground);
    }
    return true;
}

bool CWorld::GetValidJumpPos(
    const FVector& source,
    FVector& destination,
    ChargeData& chargeData,
    const double distance,
    const int inZoneId,
    const bool floating
) {
    // Current L2Server 0x00c5f970.
    L2_GUARD_FUNCTION();

    const FVector direction{destination.x - source.x, destination.y - source.y, destination.z - source.z};
    const FVector normalized = direction.SafeNormal();
    const double fullDistance = std::sqrt(
        (source.x - destination.x) * (source.x - destination.x) + (source.y - destination.y) * (source.y - destination.y) +
        (source.z - destination.z) * (source.z - destination.z)
    );
    const double jumpDistance = fullDistance - distance;
    FVector jumpPosition{source.x + normalized.x * jumpDistance, source.y + normalized.y * jumpDistance,
        source.z + normalized.z * jumpDistance};

    FVector collisionPosition{};
    if (g_WorldPlaneCollision.CheckCollision(source, jumpPosition, 0.0, collisionPosition, inZoneId, false)) {
        jumpPosition = FVector{collisionPosition.x - normalized.x * distance, collisionPosition.y - normalized.y * distance,
            collisionPosition.z - normalized.z * distance};
        destination = jumpPosition;
    }

    if (!CanGo(source, jumpPosition, inZoneId, floating)) {
        return false;
    }
    destination = jumpPosition;
    chargeData.SetData(source, jumpPosition, normalized, jumpDistance);
    return true;
}

bool CWorld::Attack(
    int actorId,
    int weaponClassId,
    AttackTargetDataType& targetData,
    AttackType attackType,
    SlotType slotType,
    bool notifyNeighbors
) {
    // Current L2Server 0x00c606f8.
    L2_GUARD_FUNCTION();

    CCreature* const actor = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(actorId));
    if (actor == nullptr) {
        return false;
    }
    CObject* const target = CObject::GetObjectW(static_cast<std::uint32_t>(targetData.nTargetId));
    if (target == nullptr) {
        return false;
    }

    if (!targetData.bIsHit) {
        OnAttackMissHappened(*actor, *target);
        return true;
    }

    {
        GuardHelper criticalGuard(L"world_attack_guard2", CThreadManager::GetThreadID());
        if (targetData.bIsCritical) {
            if (actor->IsPC()) {
                actor->CastUser()->TriggerAlterSkill(AlterSkill::Critical, ABNS_INVALID);
            }
            actor->SendSystemMessage(SystemMessageId::CHARACTER_CRITICAL_HIT);
        }
    }

    GuardHelper targetGuard(L"world_attack_guard3", CThreadManager::GetThreadID());

    if (target->IsHolyThing()) {
        CHolyThing* const holyThing = target->CastHolyThing();
        const FVector actorPosition = actor->m_data->m_Pos;
        const FVector targetPosition = holyThing->GetPos();
        const double deltaX = actorPosition.x - targetPosition.x;
        const double deltaY = actorPosition.y - targetPosition.y;
        if (deltaX * deltaX + deltaY * deltaY >= 10000.0) {
            // actor->ActionFailed(0)
            actor->ActionFailed(AF_DEFAULT);
        } else {
            holyThing->AttackedBy(actor, 1.0);
        }
        return true;
    }

    const auto isAttackable = [actor, attackType](const FVector& targetPosition) {
        const FVector actorPosition = actor->GetPos();
        const double deltaX = actorPosition.x - targetPosition.x;
        const double deltaY = actorPosition.y - targetPosition.y;
        return CAttackActionHelper::CheckAttackable(
            static_cast<double>(actor->m_data->m_nAttackRange), actor->m_data->m_dCollisionRadius, 100.0,
            deltaX * deltaX + deltaY * deltaY, actor->m_data->m_dCollisionHeight, 100.0, WorldCoordinate(actorPosition.z),
            WorldCoordinate(targetPosition.z), attackType
        );
    };

    constexpr int kAppendCapacity = 0x4f9c;
    constexpr int kFinalCapacity = 0x4fa0;

    if (target->IsDoor()) {
        CDoor* const door = target->CastDoor();
        if (!isAttackable(door->GetPos())) {
            // actor->ActionFailed(0)
            actor->ActionFailed(AF_DEFAULT);
            return true;
        }
        door->AttackedBy(actor, targetData.dDamage);

        static std::uint32_t lastNotificationTick = runtime::GetTickCount() - 2000U;
        const std::uint32_t now = runtime::GetTickCount();
        if (now <= lastNotificationTick + 1000U) {
            return true;
        }
        lastNotificationTick = runtime::GetTickCount();

        int packetLength = ::Assemble(
            g_tlClanAttackPacket, 0x4ff8, "ddddd", actorId, actor->GetIndex(), static_cast<int>(target->m_nId),
            weaponClassId, runtime::Win64CvttDoubleToInt32(targetData.dDamage)
        );
        int targetCount = 0;
        {
            GuardHelper doorNeighborGuard(L"world_attack_door_guard_3_2", CThreadManager::GetThreadID());

            class TLMFilter final : public CTLMFilterDist {
            public:
                TLMFilter(int inZoneId, const FVector& position) : CTLMFilterDist(inZoneId, position, 2048.0) {}

                bool CanDo(CObject* object) override {
                    if (!CTLMFilterDist::CanDo(object)) {
                        return false;
                    }
                    CCreature* const candidate = object->CastCreature();
                    return candidate->m_data->m_ClanId[0] == 0x38;
                }
            };

            class TLMFunc final : public CTLMFunc {
            public:
                TLMFunc(int& packetLength, int& targetCount) : m_packetLength(packetLength), m_targetCount(targetCount) {}

                bool CanDo(CObject* object, double) override {
                    if (object->IsPC() && (object->IsDeleting() || object->m_Deleted != 0)) {
                        return true;
                    }
                    if (kAppendCapacity - m_packetLength <= 99) {
                        return false;
                    }

                    CCreature* const candidate = object->CastCreature();
                    const int assembled = ::Assemble(
                        g_tlClanAttackPacket + m_packetLength, kAppendCapacity - m_packetLength, "d",
                        candidate->m_data->index
                    );
                    if (assembled == 0) {
                        return false;
                    }
                    ++m_targetCount;
                    m_packetLength += assembled;
                    return true;
                }

            private:
                int& m_packetLength;
                int& m_targetCount;
            };


            TLMFilter filter(static_cast<int>(target->GetInZoneID()), door->GetPos());
            TLMFunc append(packetLength, targetCount);
            FOR_ALL_NEIGHBOR_CREATURE_DIST(&filter, &append);
        }
        {
            GuardHelper doorSendGuard(L"world_attack_door_guard_3_3", CThreadManager::GetThreadID());
            if (targetCount > 0 && notifyNeighbors) {
                packetLength += ::Assemble(g_tlClanAttackPacket + packetLength, kFinalCapacity - packetLength, "d", -1);
                g_npcserver.Send("cb", 0x1b, packetLength, g_tlClanAttackPacket);
            }
        }
        return true;
    }

    if (target->IsBattleCamp() || target->IsControlTower() || target->IsAirShipPart() || target->IsOwnThing()) {
        CStaticObject* const staticObject = target->CastStaticObject();
        if (!isAttackable(staticObject->GetPos())) {
            // actor->ActionFailed(0)
            actor->ActionFailed(AF_DEFAULT);
        } else {
            staticObject->AttackedBy(actor, targetData.dDamage);
        }
        return true;
    }

    if (!target->IsCreature()) {
        return true;
    }

    CCreature* const creatureTarget = target->CastCreature();
    if (creatureTarget->IsNPC()) {
        switch (creatureTarget->m_nClassId) {
            case 0xf761c:
            case 0xf761d:
            case 0xf761e:
            case 0xf742a:
            case 0xf742b:
            case 0xf7519:
            case 0xf769c:
            case 0xf769d:
            case 0xf769e:
                switch (weaponClassId) {
                    case 0x106a:
                    case 0x140d:
                    case 0x16b9:
                    case 0x1b92:
                    case 0x209e:
                    case 0xb4a9:
                    case 0xc286:
                        targetData.dDamage = 1.0;
                        break;
                    default:
                        targetData.dDamage = 0.0;
                        break;
                }
                break;
            default:
                break;
        }
    }

    creatureTarget->AttackedBy(actor, targetData.dDamage, targetData.bIsCritical, weaponClassId, attackType, slotType);
    if (targetData.eShieldDefense != AttackTargetDataType::SDT_NONE) {
        creatureTarget->TriggerSkillByBlock(*actor, static_cast<int>(targetData.eShieldDefense));
        creatureTarget->TriggerAlterSkill(AlterSkill::Block, ABNS_INVALID);
    }

    bool sendNpcAttackEvent = false;
    if (creatureTarget->IsNPC()) {
        sendNpcAttackEvent = creatureTarget->CastNPC()->CanGetNpcEvent();
    }
    if (!sendNpcAttackEvent && actor->IsPC()) {
        sendNpcAttackEvent = actor->CastUser()->HaveSummon() || actor->CastUser()->HaveSummonedNpc();
    }
    if (!sendNpcAttackEvent && creatureTarget->IsPC()) {
        sendNpcAttackEvent = creatureTarget->CastUser()->HaveSummon() || creatureTarget->CastUser()->HaveSummonedNpc();
    }
    const std::int32_t npcAttackDamage = NpcAttackEventDamage(targetData.dDamage, actor->dDPSBonusForBuilder);
    if (sendNpcAttackEvent) {
        // Original GM-bonus omission (kept for reference):
        // g_npcserver.Send(
        //     "cddddd", 4, actorId, static_cast<int>(target->m_nId),
        //     runtime::Win64CvttDoubleToInt32(targetData.dDamage), weaponClassId, targetData.bIsCritical)
        g_npcserver.Send(
            "cddddd", 4, actorId, static_cast<int>(target->m_nId), npcAttackDamage, weaponClassId, targetData.bIsCritical
        );
    }

    if (!notifyNeighbors) {
        return true;
    }

    // Keep the same damage in the neighboring NPC clan-attack notification.
    // Original: runtime::Win64CvttDoubleToInt32(targetData.dDamage).
    int packetLength = ::Assemble(
        g_tlClanAttackPacket, 0x4ff8, "ddddd", actorId, actor->GetIndex(), static_cast<int>(target->m_nId), weaponClassId,
        npcAttackDamage
    );
    int neighborCount = 0;

    const int targetIndex = creatureTarget->GetIndex();
    {
        GuardHelper clanHelpGuard(L"world_attack_guard_3_2", CThreadManager::GetThreadID());

        class TLMFilter final : public CTLMFilterDist {
        public:
            TLMFilter(int inZoneId, const FVector& position, double distance, int targetIndex, int targetClassIndex) :
                CTLMFilterDist(inZoneId, position, distance),
                m_targetIndex(targetIndex),
                m_targetClassIndex(targetClassIndex) {}

            bool CanDo(CObject* object) override {
                if (!CTLMFilterDist::CanDo(object)) {
                    return false;
                }
                CNPC* const candidate = __SafeCastNpc(object);
                if (candidate == nullptr) {
                    return false;
                }

                const int targetZ = WorldCoordinate(GetPosition().z);
                const int candidateZ = WorldCoordinate(candidate->GetPos().z);
                if (candidateZ > runtime::AddInt32Wrapping(targetZ, 300)) {
                    return false;
                }
                const int candidateTop =
                    runtime::AddInt32Wrapping(candidateZ, WorldCoordinate(candidate->m_data->m_dCollisionHeight));
                if (runtime::AddInt32Wrapping(targetZ, runtime::Int32FromBits(UINT32_C(0xfffffed4))) > candidateTop) {
                    return false;
                }
                return candidate->m_data->index != m_targetIndex &&
                    Singleton<CClanHelpTable>::inst().GetFlag(m_targetClassIndex, candidate->m_data->m_nClassIndex) &&
                    candidate->CanGetNpcEvent();
            }

        private:
            int m_targetIndex;
            int m_targetClassIndex;
        };

        class TLMFunc final : public CTLMFunc {
        public:
            TLMFunc(int& packetLength, int& targetCount) : m_packetLength(packetLength), m_targetCount(targetCount) {}

            bool CanDo(CObject* object, double) override {
                if (object->IsPC() && (object->IsDeleting() || object->m_Deleted != 0)) {
                    return true;
                }

                CCreature* const candidate = object->CastCreature();
                const int assembled = ::Assemble(
                    g_tlClanAttackPacket + m_packetLength, kAppendCapacity - m_packetLength, "d", candidate->m_data->index
                );
                if (assembled == 0) {
                    return false;
                }
                ++m_targetCount;
                m_packetLength += assembled;
                return true;
            }

        private:
            int& m_packetLength;
            int& m_targetCount;
        };


        const int clanHelpRange = creatureTarget->m_data->m_nClanHelpRange;
        if (creatureTarget->IsNPC() && clanHelpRange > 0) {
            const FVector position = creatureTarget->GetPos();
            TLMFilter filter(
                static_cast<int>(creatureTarget->GetInZoneID()), position, static_cast<double>(clanHelpRange), targetIndex,
                creatureTarget->m_data->m_nClassIndex
            );
            TLMFunc append(packetLength, neighborCount);
            FOR_ALL_NEIGHBOR_CREATURE_DIST(&filter, &append);
        }
    }

    {
        GuardHelper siegeNpcGuard(L"world_attack_guard_3_3", CThreadManager::GetThreadID());

        class TLMFilter final : public CTLMFilterDist {
        public:
            TLMFilter(int inZoneId, const FVector& position, int targetIndex, int residenceId) :
                CTLMFilterDist(inZoneId, position, 500.0), m_targetIndex(targetIndex), m_residenceId(residenceId) {}

            bool CanDo(CObject* object) override {
                if (!CTLMFilterDist::CanDo(object)) {
                    return false;
                }
                CCreature* const candidate = object->CastCreature();
                return candidate != nullptr && candidate->m_data->index != m_targetIndex && candidate->IsNPC() &&
                    static_cast<int>(candidate->m_data->m_nClass) == 0x19 &&
                    candidate->m_data->m_nResidenceID == m_residenceId;
            }

        private:
            int m_targetIndex;
            int m_residenceId;
        };

        class TLMFunc final : public CTLMFunc {
        public:
            TLMFunc(int& packetLength, int& targetCount) : m_packetLength(packetLength), m_targetCount(targetCount) {}

            bool CanDo(CObject* object, double) override {
                if (object->IsPC() && (object->IsDeleting() || object->m_Deleted != 0)) {
                    return true;
                }

                CCreature* const candidate = object->CastCreature();
                const int assembled = ::Assemble(
                    g_tlClanAttackPacket + m_packetLength, kAppendCapacity - m_packetLength, "d", candidate->m_data->index
                );
                if (assembled == 0) {
                    return false;
                }
                ++m_targetCount;
                m_packetLength += assembled;
                return true;
            }

        private:
            int& m_packetLength;
            int& m_targetCount;
        };


        if (creatureTarget->IsPC()) {
            User* const lastSendUser = creatureTarget->CastUser();
            const std::uint32_t now = runtime::GetTickCount();
            if (lastSendUser->dwLastSendTime + 10000U < now) {
                creatureTarget->CastUser()->dwLastSendTime = runtime::GetTickCount();
                User* const user = creatureTarget->CastUser();
                if (creatureTarget->m_AreaMod.m_bIsInBattleField && !creatureTarget->m_AreaMod.m_bIsInPvPZone) {
                    CPledge* const pledge = user->GetPledge();
                    const int castleId = user->m_nCurCastleId;
                    if (pledge != nullptr && pledge->GetCurrentCastleSiegeStatus(castleId) == 2) {
                        const int residenceId = pledge->GetCurrentCastleSiegeDbId(castleId);
                        const FVector position = creatureTarget->GetPos();
                        TLMFilter filter(
                            static_cast<int>(creatureTarget->GetInZoneID()), position, targetIndex, residenceId
                        );
                        TLMFunc append(packetLength, neighborCount);
                        FOR_ALL_NEIGHBOR_CREATURE_DIST(&filter, &append);
                    }
                }
            }
        }
    }

    {
        GuardHelper neighborSendGuard(L"world_attack_guard_3_4", CThreadManager::GetThreadID());
        if (neighborCount > 0) {
            packetLength += ::Assemble(g_tlClanAttackPacket + packetLength, kFinalCapacity - packetLength, "d", -1);
            g_npcserver.Send("cb", 0x1b, packetLength, g_tlClanAttackPacket);
        }
    }
    return true;
}

void CWorld::Spell(CCreature& creature, CWorldObject& target, int skillId, int skillLevel, int skillSubLevel) {
    L2_GUARD_FUNCTION();

    constexpr int kPayloadSize = 0x4f88;
    constexpr int kElementCapacity = 0x4f84;
    int payloadLength = 0;
    int targetCount = 0;

    if (!target.IsCreature()) {
        if (target.IsDoor()) {
            CDoor* const door = target.CastDoor();
            if (runtime::GetTickCount() - door->m_dwLastSendTime > 1000U) {
                target.CastDoor()->m_dwLastSendTime = runtime::GetTickCount();

                class TLMFilter final : public CTLMFilterDist {
                public:
                    TLMFilter(int inZoneId, const FVector& position) : CTLMFilterDist(inZoneId, position, 4096.0) {}

                    bool CanDo(CObject* object) override {
                        if (!CTLMFilterDist::CanDo(object)) {
                            return false;
                        }
                        CCreature* const candidate = object->CastCreature();
                        return candidate->m_data->m_ClanId[0] == 0x38;
                    }
                };

                class TLMFunc final : public CTLMFunc {
                public:
                    TLMFunc(int& payloadLength, int& targetCount) :
                        m_payloadLength(payloadLength), m_targetCount(targetCount) {}

                    bool CanDo(CObject* object, double) override {
                        if (object->IsPC() && (object->IsDeleting() || object->m_Deleted != 0)) {
                            return true;
                        }
                        if (0x4f9c - m_payloadLength <= 99) {
                            return false;
                        }

                        CCreature* const candidate = object->CastCreature();
                        const int assembled = ::Assemble(
                            g_tlClanSpelledPacket + m_payloadLength, kElementCapacity - m_payloadLength, "d",
                            candidate->m_data->index
                        );
                        if (assembled == 0) {
                            return false;
                        }
                        ++m_targetCount;
                        m_payloadLength += assembled;
                        return true;
                    }

                private:
                    int& m_payloadLength;
                    int& m_targetCount;
                };


                const FVector position = target.GetPos();
                TLMFilter filter(static_cast<int>(target.GetInZoneID()), position);
                TLMFunc func(payloadLength, targetCount);
                FOR_ALL_NEIGHBOR_CREATURE_DIST(&filter, &func);
            }
        }
    } else {
        CCreature* const spellTarget = target.CastCreature();
        const int targetIndex = spellTarget->GetIndex();
        const int clanIdCount = spellTarget->m_data->m_nClanIdCount;
        const int clanHelpRange = spellTarget->m_data->m_nClanHelpRange;

        if (spellTarget->IsNPC() && clanIdCount != 0 && clanHelpRange > 0) {
            class TLMFilter final : public CTLMFilterDist {
            public:
                TLMFilter(int inZoneId, const FVector& position, double distance, int targetIndex, int targetClassIndex) :
                    CTLMFilterDist(inZoneId, position, distance),
                    m_targetIndex(targetIndex),
                    m_targetClassIndex(targetClassIndex) {}

                bool CanDo(CObject* object) override {
                    if (!CTLMFilterDist::CanDo(object)) {
                        return false;
                    }
                    CNPC* const candidate = __SafeCastNpc(object);
                    return candidate != nullptr && candidate->m_data->index != m_targetIndex &&
                        Singleton<CClanHelpTable>::inst().GetFlag(m_targetClassIndex, candidate->m_data->m_nClassIndex) &&
                        candidate->CanGetNpcEvent();
                }

            private:
                int m_targetIndex;
                int m_targetClassIndex;
            };

            class TLMFunc final : public CTLMFunc {
            public:
                TLMFunc(int& payloadLength, int& targetCount) : m_payloadLength(payloadLength), m_targetCount(targetCount) {}

                bool CanDo(CObject* object, double) override {
                    if (object->IsPC() && (object->IsDeleting() || object->m_Deleted != 0)) {
                        return true;
                    }

                    CCreature* const candidate = object->CastCreature();
                    const int assembled = ::Assemble(
                        g_tlClanSpelledPacket + m_payloadLength, kElementCapacity - m_payloadLength, "d",
                        candidate->m_data->index
                    );
                    if (assembled == 0) {
                        return false;
                    }
                    ++m_targetCount;
                    m_payloadLength += assembled;
                    return true;
                }

            private:
                int& m_payloadLength;
                int& m_targetCount;
            };


            const FVector position = spellTarget->GetPos();
            TLMFilter filter(
                static_cast<int>(spellTarget->GetInZoneID()), position, static_cast<double>(clanHelpRange), targetIndex,
                spellTarget->m_data->m_nClassIndex
            );
            TLMFunc func(payloadLength, targetCount);
            FOR_ALL_NEIGHBOR_CREATURE_DIST(&filter, &func);
        }

        if (spellTarget->IsPC()) {
            User* const lastSendUser = spellTarget->CastUser();
            if (runtime::GetTickCount() - lastSendUser->dwLastSendTime > 10000U) {
                User* const user = spellTarget->CastUser();
                user->dwLastSendTime = runtime::GetTickCount();
                if (spellTarget->m_AreaMod.m_bIsInBattleField && !spellTarget->m_AreaMod.m_bIsInPvPZone) {
                    const int castleId = user->m_nCurCastleId;
                    CPledge* const pledge = user->GetPledge();
                    if (pledge != nullptr && pledge->GetCurrentCastleSiegeStatus(castleId) == 2) {
                        class TLMFilter final : public CTLMFilterDist {
                        public:
                            TLMFilter(int inZoneId, const FVector& position, int targetIndex, int residenceId) :
                                CTLMFilterDist(inZoneId, position, 500.0),
                                m_targetIndex(targetIndex),
                                m_residenceId(residenceId) {}

                            bool CanDo(CObject* object) override {
                                if (!CTLMFilterDist::CanDo(object)) {
                                    return false;
                                }
                                CCreature* const candidate = object->CastCreature();
                                return candidate != nullptr && candidate->m_data->index != m_targetIndex &&
                                    static_cast<int>(candidate->m_data->m_nClass) == 0x19 &&
                                    candidate->m_data->m_nResidenceID == m_residenceId;
                            }

                        private:
                            int m_targetIndex;
                            int m_residenceId;
                        };

                        class TLMFunc final : public CTLMFunc {
                        public:
                            TLMFunc(int& payloadLength, int& targetCount) :
                                m_payloadLength(payloadLength), m_targetCount(targetCount) {}

                            bool CanDo(CObject* object, double) override {
                                if (object->IsPC() && (object->IsDeleting() || object->m_Deleted != 0)) {
                                    return true;
                                }

                                CCreature* const candidate = object->CastCreature();
                                const int assembled = ::Assemble(
                                    g_tlClanSpelledPacket + m_payloadLength, kElementCapacity - m_payloadLength, "d",
                                    candidate->m_data->index
                                );
                                if (assembled == 0) {
                                    return false;
                                }
                                ++m_targetCount;
                                m_payloadLength += assembled;
                                return true;
                            }

                        private:
                            int& m_payloadLength;
                            int& m_targetCount;
                        };


                        const int residenceId = pledge->GetCurrentCastleSiegeDbId(castleId);
                        const FVector position = spellTarget->GetPos();
                        TLMFilter filter(static_cast<int>(spellTarget->GetInZoneID()), position, targetIndex, residenceId);
                        TLMFunc func(payloadLength, targetCount);
                        FOR_ALL_NEIGHBOR_CREATURE_DIST(&filter, &func);
                    }
                }
            }
        }
    }

    if (targetCount > 0) {
        payloadLength += ::Assemble(g_tlClanSpelledPacket + payloadLength, kPayloadSize - payloadLength, "d", -1);
        g_npcserver.Send(
            "cddddddb", 0x1c, static_cast<int>(creature.Id()), creature.GetIndex(), static_cast<int>(target.Id()), skillId,
            skillLevel, skillSubLevel, payloadLength, g_tlClanSpelledPacket
        );
    }
}

bool CWorld::GetValidPositionAroundPos(
    const CCreature& creature, const int radius, const double collisionRadius, const FVector& source, FVector& destination
) {
    L2_GUARD_FUNCTION();

    const double positive = static_cast<double>(radius);
    const double negative = -positive;
    std::vector<FVector> candidates;
    candidates.reserve(8);
    candidates.push_back(FVector{source.x + positive, source.y + positive, source.z});
    candidates.push_back(FVector{source.x + positive, source.y, source.z});
    candidates.push_back(FVector{source.x + negative, source.y + positive, source.z});
    candidates.push_back(FVector{source.x + negative, source.y, source.z});
    candidates.push_back(FVector{source.x + positive, source.y + negative, source.z});
    candidates.push_back(FVector{source.x, source.y + negative, source.z});
    candidates.push_back(FVector{source.x + negative, source.y + negative, source.z});
    candidates.push_back(FVector{source.x, source.y + positive, source.z});

    for (std::size_t index = 1; index < candidates.size(); ++index) {
        const std::size_t selected = static_cast<std::size_t>(std::rand() & 0x7fff) % (index + 1);
        std::swap(runtime::CheckedVectorAt(candidates, index), runtime::CheckedVectorAt(candidates, selected));
    }

    destination = source;
    FVector outputPosition{};
    bool distanceExhausted = false;
    double travelledDistance = 0.0;
    for (FVector& candidate : candidates) {
        if (CanGo(source, candidate, outputPosition, distanceExhausted, travelledDistance, creature.GetInZoneID(), false) &&
            !g_WorldPlaneCollision.CheckCollision(
                source, candidate, collisionRadius, outputPosition, creature.GetInZoneID(), true
            )) {
            destination = outputPosition;
            return true;
        }
    }
    return false;
}

bool CWorld::CanSummonNpc(const FVector& position, User* const user) {
    // Current L2Server 0x00c59934.
    L2_GUARD_FUNCTION();

    if (!IsValidPosition(static_cast<int>(position.x), static_cast<int>(position.y))) {
        user->SendSystemMessage(SystemMessageId::CANNOT_SUMMON_AT_CURRENT_LOCATION);
        return false;
    }

    const FVector source = user->GetPos();
    if (!GetGeo()->MoveStraightTest(source, position, 250.0, false, 0)) {
        user->SendSystemMessage(SystemMessageId::CANNOT_SUMMON_AT_CURRENT_LOCATION);
        return false;
    }

    if (!NpcServer::Inst()->Connected()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_SUMMON_PET_NPC_SERVER_UNAVAILABLE);
        return false;
    }
    return true;
}

bool CWorld::PutItem(CItem* item, FVector position, std::uint32_t inZoneId, CCreature* dropper) {
    L2_GUARD_FUNCTION();
    if (item == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "item null at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0xabc)
        L2_LOG_ERROR(Log, "[world items] Put-item request has no item; request rejected.");
        return false;
    }
    if (item->m_data == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "item data null at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0xac2)
        L2_LOG_ERROR(Log, "[world items] Item object %u has no shared data; put-item request rejected.", item->Id());
        return false;
    }
    if (!Inst().IsValidPosition(WorldCoordinate(position.x), WorldCoordinate(position.y))) {
        // Log.Add(
        //     LOG_ERROR,
        //     "invalid position (%d, %d, %d) ItemDBID(%d) "
        //     "at file[%s], line[%d]",
        //     WorldCoordinate(position.x),
        //     WorldCoordinate(position.y),
        //     WorldCoordinate(position.z),
        //     item->GetDBID(),
        //     "world_server.cpp",
        //     0xac8)
        L2_LOG_ERROR(
            Log, "[world items] Position (%d, %d, %d) is outside the world; put request for database item %u rejected.",
            WorldCoordinate(position.x), WorldCoordinate(position.y), WorldCoordinate(position.z), item->GetDBID()
        );
        return false;
    }

    CContainer* container = nullptr;
    if (inZoneId == 0) {
        container = CContainerDB::Inst().GetWorldContainer(
            WorldCoordinate(position.x), WorldCoordinate(position.y), WorldCoordinate(position.z)
        );
    } else {
        CGeneralObject* const object = CGeneralObject::GetObjectW(inZoneId);
        CInstantZone* const instantZone = object != nullptr && object->IsInZone() ? object->CastInZone() : nullptr;
        if (instantZone != nullptr) {
            if (!instantZone->EnterZone(item)) {
                // Log.Add(
                //     LOG_ERROR,
                //     "[%s][%d][DropItem] Cannot Enter Inzone "
                //     "SID(%d) ItemDBID(%d)",
                //     "world_server.cpp",
                //     0xad5,
                //     inZoneId,
                //     item->GetDBID())
                L2_LOG_ERROR(
                    Log, "[world items] Database item %u could not enter instance zone %u; drop rejected.", item->GetDBID(),
                    inZoneId
                );
                return false;
            }
            container = instantZone->GetContainer();
        }
    }

    if (container == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Putitem failed : Pos(%d, %d, %d) (%d) ItemDBID(%d), "
        //     "file[%s], line[%d]",
        //     WorldCoordinate(position.x),
        //     WorldCoordinate(position.y),
        //     WorldCoordinate(position.z),
        //     inZoneId,
        //     item->GetDBID(),
        //     "world_server.cpp",
        //     0xae4)
        L2_LOG_ERROR(
            Log,
            "[world items] No container covers position (%d, %d, %d) in instance zone %u; put request for database item %u "
            "rejected.",
            WorldCoordinate(position.x), WorldCoordinate(position.y), WorldCoordinate(position.z), inZoneId, item->GetDBID()
        );
        return false;
    }

    if (dropper != nullptr) {
        L2_ADD_REF(dropper, ObjectRefType::SerializedWorldThread);
        FVector outputPosition{};
        bool distanceExhausted = false;
        double travelledDistance = 0.0;
        const FVector source = dropper->GetPos();
        if (!Inst().CanGo(
                source, position, outputPosition, distanceExhausted, travelledDistance, dropper->GetInZoneID(), false
            )) {
            Inst().GetValidPositionAroundPos(
                *dropper, 0x28, dropper->m_data->m_dCollisionRadius, dropper->GetPos(), position
            );
        }
    }

    for (int thread = 0; thread < CThreadManager::GetThreadCount(); ++thread) {
        L2_ADD_REF(item, ObjectRefType::ThreadLocalMap);
    }
    item->IncreaseTLRef();
    g_ptlItemMap->Push(item->GetIndex(), position, item);
    L2_ADD_REF(item, ObjectRefType::SerializedWorldThread);
    AsyncPutItem(dropper, item, position, container);
    return true;
}

bool CWorld::PickItemCond(CItem* item, CCreature* creature) {
    L2_GUARD_FUNCTION();
    if (item == nullptr || creature == nullptr) {
        return false;
    }
    if (!item->IsItem()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Item[] is not exist.",
        //     L"world_server.cpp",
        //     0xbfa)
        L2_LOG_ERROR(Log, u"[world items] Object %u is not an item; pickup eligibility check rejected it.", item->Id());
        return false;
    }
    if (!creature->IsPC()) {
        return true;
    }
    User* const user = creature->CastUser();
    return user != nullptr &&
        user->m_inv.CheckAddable(
            // Current CWorld::PickItemCond@0x00c59744 uses the live item
            // index (+0x28), not its class ID (+0x58).
            item->m_data->index, false
        );
}

bool CWorld::TryPickItem(CItem* item, CCreature* creature, bool force) {
    L2_GUARD_FUNCTION();
    if (item == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "item null at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0xb25)
        L2_LOG_ERROR(Log, "[world items] Pickup request has no item; request rejected.");
        return false;
    }
    if (item->m_data == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "item data null at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0xb2b)
        L2_LOG_ERROR(Log, "[world items] Item object %u has no shared data; pickup request rejected.", item->Id());
        return false;
    }
    if (creature == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "creature null at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0xb31)
        L2_LOG_ERROR(
            Log, "[world items] Pickup request for database item %u has no creature; request rejected.", item->GetDBID()
        );
        return false;
    }
    if (creature->m_data == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "creature data null at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0xb37)
        L2_LOG_ERROR(
            Log, "[world items] Creature %u has no shared data; pickup request for database item %u rejected.",
            creature->Id(), item->GetDBID()
        );
        return false;
    }
    if (!item->IsItem()) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] item is not a item.",
        //     "world_server.cpp",
        //     0xb3d)
        L2_LOG_ERROR(Log, "[world items] Object %u is not an item; pickup request rejected.", item->Id());
        return false;
    }

    if (creature->IsPC()) {
        User* const user = creature->CastUser();
        if (user->m_PledgeGameUser.IsWaiting()) {
            return false;
        }
    }

    const int itemClassId = item->m_data->m_nClassID;
    if (itemClassId >= 0x5122 && itemClassId <= 0x5124) {
        if (creature->IsPC()) {
            return false;
        }
        if (creature->IsPet() && creature->CastNPC()->m_nClassId != 0xf4842) {
            return false;
        }
    }
    if (!item->m_data->m_bIsActive) {
        return false;
    }

    if (!force) {
        const FVector creaturePosition = creature->GetPos();
        const FVector itemPosition = item->GetPos();
        const double deltaX = itemPosition.x - creaturePosition.x;
        const double deltaY = itemPosition.y - creaturePosition.y;
        if (deltaX * deltaX + deltaY * deltaY > 900.0) {
            return false;
        }

        if (!GetGeo()->CanSee(item, creature, creature->IsPC()) &&
            !GetGeo()->CanSeeOldStyle(item, creature, creature->IsPC())) {
            return false;
        }
    }

    if (item->m_const->m_AttackType == AT_FLAG) {
        if (!creature->IsPC()) {
            return false;
        }
        User* const user = creature->CastUser();
        if (user == nullptr || user->m_data->m_StopMode == STP_STORE || user->GetPrivateStoreType() != 0) {
            return false;
        }
        CPledge* const pledge = user->GetPledge();
        if (pledge == nullptr || pledge->m_pFortressUnderOffenceSiege == nullptr ||
            !pledge->m_pFortressUnderOffenceSiege->CanFlagBePicked(user, pledge, item, false)) {
            return false;
        }
    }

    CObject* const contributionObject = item->m_ContributeDataID.GetObjectW();
    if (contributionObject == nullptr) {
        return PickItem(item, creature, force);
    }

    CContributeData* const contribution = static_cast<CContributeData*>(contributionObject);
    const std::int64_t elapsed = runtime::Int64FromBits(
        static_cast<std::uint64_t>(runtime::GetTickCount()) - static_cast<std::uint64_t>(item->m_nDroppedTick)
    );
    if (elapsed > 5000) {
        if (elapsed > 15000) {
            if (elapsed > 0) {
                return PickItem(item, creature, force);
            }
            return false;
        }
        if (contribution->PickableIn15Sec(creature)) {
            return PickItem(item, creature, force);
        }

        if (creature->IsPC()) {
            if (item->m_data->GetEnchanted() < 1) {
                creature->CastUser()->GetSocket()->Send(
                    "chccd", 0x62, SystemMessageId::FAILED_TO_PICK_UP_ITEM, 1, 3, item->m_nClassId
                );
            } else {
                creature->CastUser()->GetSocket()->Send(
                    "chccccd", 0x62, SystemMessageId::FAILED_TO_PICK_UP_ENCHANTED_ITEM, 2, 0x13,
                    static_cast<int>(static_cast<std::int8_t>(item->m_data->GetEnchanted())), 3, item->m_nClassId
                );
            }
        } else if (creature->IsPet()) {
            User* const master = creature->CastPet()->GetMaster();
            if (master == nullptr) {
                return false;
            }
            if (item->m_data->GetEnchanted() < 1) {
                master->GetSocket()->Send("chccd", 0x62, SystemMessageId::FAILED_TO_PICK_UP_ITEM, 1, 3, item->m_nClassId);
            } else {
                master->GetSocket()->Send(
                    "chccccd", 0x62, SystemMessageId::FAILED_TO_PICK_UP_ENCHANTED_ITEM, 2, 0x13,
                    static_cast<int>(static_cast<std::int8_t>(item->m_data->GetEnchanted())), 3, item->m_nClassId
                );
            }
        }
        return false;
    }

    if (contribution->PickableIn5Sec(creature)) {
        return PickItem(item, creature, force);
    }
    if (creature->IsPC()) {
        if (item->m_data->GetEnchanted() < 1) {
            creature->CastUser()->GetSocket()->Send(
                "chccd", 0x62, SystemMessageId::FAILED_TO_PICK_UP_ITEM, 1, 3, item->m_nClassId
            );
        } else {
            creature->CastUser()->GetSocket()->Send(
                "chccccd", 0x62, SystemMessageId::FAILED_TO_PICK_UP_ENCHANTED_ITEM, 2, 0x13,
                static_cast<int>(static_cast<std::int8_t>(item->m_data->GetEnchanted())), 3, item->m_nClassId
            );
        }
    } else if (creature->IsPet()) {
        User* const master = creature->CastPet()->GetMaster();
        if (master == nullptr) {
            return false;
        }
        if (item->m_data->GetEnchanted() < 1) {
            master->GetSocket()->Send("chccd", 0x62, SystemMessageId::FAILED_TO_PICK_UP_ITEM, 1, 3, item->m_nClassId);
        } else {
            master->GetSocket()->Send(
                "chccccd", 0x62, SystemMessageId::FAILED_TO_PICK_UP_ENCHANTED_ITEM, 2, 0x13,
                static_cast<int>(static_cast<std::int8_t>(item->m_data->GetEnchanted())), 3, item->m_nClassId
            );
        }
    }
    return false;
}

bool CWorld::PutItem(CItem* item, FVector position, CCreature* dropper) {
    L2_GUARD_FUNCTION();
    const std::uint32_t inZoneId = dropper != nullptr ? dropper->GetInZoneID() : 0;
    return PutItem(item, position, inZoneId, dropper);
}

void CWorld::AsyncPutItem(CCreature* dropper, CItem* item, FVector position, CContainer* container) {
    __async_arg_AsyncPutItem argument(dropper, item, position, container);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncPutItem(pad); }, &argument, L"AsyncPutItem"
    );
}

void CWorld::__async_AsyncPutItem(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CCreature* dropper;
    CItem* item;
    FVector position;
    CContainer* container;
    std::memcpy(&dropper, argument->Buffer + 0x08, sizeof(dropper));
    std::memcpy(&item, argument->Buffer + 0x10, sizeof(item));
    std::memcpy(&position, argument->Buffer + 0x18, sizeof(position));
    std::memcpy(&container, argument->Buffer + 0x30, sizeof(container));

    if (container == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[AsyncPutItem] Container is Invalid! "
        //     L"CretureID[%d] ItemID[%d]",
        //     dropper != nullptr ? dropper->Id() : 0,
        //     item->GetDBID())
        L2_LOG_ERROR(
            Log, u"[world items] Async put for database item %u has no destination container; dropper runtime ID is %u.",
            item->GetDBID(), dropper != nullptr ? dropper->Id() : 0
        );
        return;
    }

    ReleaseHelper<CItem, ObjectRefType::SerializedWorldThread> releaseItem(item, "world_server.cpp", 0x60e);
    ReleaseHelper<CCreature, ObjectRefType::SerializedWorldThread> releaseDropper(dropper, "world_server.cpp", 0x60f);

    item->m_data->SetPos(position);
    container->PushItem(item);
    item->m_data->m_bIsActive = true;

    CContainerIndex neighbor = CContainerDB::Inst().GatherNeighborContainerIndex(container);
    CContainerFilterNull filter;
    if (dropper == nullptr) {
        class ContainerFunc final : public CContainerFunc {
        public:
            explicit ContainerFunc(CItem* item) : m_item(item) {}

            void Do(CObject* object) override {
                if (object == m_item) {
                    return;
                }
                if (object->IsPC() && (object->IsDeleting() || object->m_Deleted != 0)) {
                    return;
                }
                object->InSight(m_item, false);
            }

        private:
            CItem* m_item;
        } function(item);


        FOR_ALL_OBJECT_IN_CONTAINER_INDEX(neighbor, filter, function);
        return;
    }

    class ContainerFunc final : public CContainerFunc {
    public:
        ContainerFunc(CItem* item, CCreature* dropper) : m_item(item), m_dropper(dropper) {}

        void Do(CObject* object) override {
            if (object->IsPC() && (object->IsDeleting() || object->m_Deleted != 0)) {
                return;
            }

            if (object->IsPC()) {
                User* const user = object->CastUser();
                m_item->SendItemDropped(user->GetSocket(), m_dropper);
            } else if (object->IsVehicle()) {
                object->CastVehicle()->SeeDropItem(m_item, m_dropper);
            } else if (object->IsAirShip()) {
                object->CastAirShip()->SeeDropItem(m_item, m_dropper);
            } else if (object->IsShuttle()) {
                object->CastShuttle()->SeeDropItem(m_item, m_dropper);
            }
        }

    private:
        CItem* m_item;
        CCreature* m_dropper;
    } function(item, dropper);


    FOR_ALL_OBJECT_IN_CONTAINER_INDEX(neighbor, filter, function);
}

void CWorld::PickAndDeleteItem(CItem* item) {
    L2_GUARD_FUNCTION();
    L2_ADD_REF(item, ObjectRefType::SerializedWorldThread);

    __async_arg_AsyncPickAndDeleteItem argument(item);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncPickAndDeleteItem(pad); }, &argument,
        L"AsyncPickAndDeleteItem"
    );
}

bool CWorld::PickAndEquipItemFailed(CItem* item) {
    L2_GUARD_FUNCTION();
    if (item == nullptr) {
        return false;
    }
    L2_GUARD(*item->m_lock);
    item->m_data->m_bIsActive = true;
    return false;
}

bool CWorld::PickAndEquipItem(CItem* item, CCreature* creature) {
    L2_GUARD_FUNCTION();
    if (item == nullptr || creature == nullptr) {
        return PickAndEquipItemFailed(item);
    }

    if (!creature->IsPC() && !creature->IsNPC()) {
        return PickAndEquipItemFailed(item);
    }
    if (creature->IsPC()) {
        User* const user = creature->CastUser();
        if (user == nullptr || !user->CanEquip(item)) {
            return PickAndEquipItemFailed(item);
        }
    }
    if (!creature->m_inv.CheckAddable(
            // Current CWorld::PickAndEquipItem@0x00c6719c uses the live
            // item index for the two-argument CheckAddable overload.
            item->m_data->index, false
        )) {
        return PickAndEquipItemFailed(item);
    }

    int slot = 0;
    while (slot < ST_MAX && (ST2SBT[slot] & item->m_data->m_Slot) == 0) {
        ++slot;
    }
    if (slot >= ST_MAX || creature->IsEquippedWithForcedItem(static_cast<SlotType>(slot))) {
        return PickAndEquipItemFailed(item);
    }

    if (item->m_data->m_Type == IT_WEAPON && creature->m_SkillDurationMod.m_nBlockEquipWeaponCount > 0) {
        return PickAndEquipItemFailed(item);
    }
    if (item->m_data->m_Type == IT_ARMOR && slot < 0x12 && ((0x28c00U >> (slot & 0x1f)) & 1U) != 0 &&
        creature->m_SkillMod.m_nBlockEquipChestArmorCount > 0) {
        return PickAndEquipItemFailed(item);
    }

    if (User* const user = creature->GetUserOrMaster()) {
        item->LogRelateItem(*user, item->m_data->m_nCount);
    }
    if (PickItemCond(item, creature)) {
        DoAsyncPickItem(item);
        DoAsyncAddItemToInventory(item, creature);
    }
    return true;
}

void CWorld::PickAndUseItem(CItem* item, CCreature* creature) {
    // Current L2Server 0x00c667b4.
    L2_GUARD_FUNCTION();
    // Recovered source used the following defensive boundary, but current
    // 0x00c667b4 only tests the creature argument before reading item fields:
    // if (item == nullptr || creature == nullptr) { return. }
    if (creature == nullptr) {
        return;
    }

    const CSkillInfo* lastSkill = nullptr;
    for (const CSkillInfo* const skill : item->m_ItemSkillList) {
        if (skill == nullptr) {
            continue;
        }
        lastSkill = skill;
        if (skill->CheckOperateCond(creature, creature, CSkillInfo::OCT_BOTH) != -2) {
            continue;
        }

        skill->ActivateSkill(creature, creature, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);

        if (skill->m_OperateType != SKILL_INSTANT && skill->m_OperateType != SKILL_PARTY_INSTANT) {
            continue;
        }

        User* user = nullptr;
        if (creature->IsPC()) {
            user = creature->CastUser();
            FOR_ALL_SUMMON_IN_SUMMON_LIST(
                user, summon, skill->ActivateSkill(summon, summon, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
            );
        }

        if (skill->m_OperateType != SKILL_PARTY_INSTANT) {
            continue;
        }
        user = creature->CastUser();
        if (user == nullptr) {
            continue;
        }
        CParty* const party = user->GetParty();
        if (party == nullptr) {
            continue;
        }

        std::vector<User*> members;
        party->GetAllMember(members);
        const FVector userPosition = user->GetPos();
        const double affectRange = static_cast<double>(skill->m_nAffectRange);
        const double affectRangeSquared = affectRange * affectRange;
        for (User* const member : members) {
            if (member == nullptr || member == user) {
                continue;
            }
            const FVector memberPosition = member->GetPos();
            const double dx = memberPosition.x - userPosition.x;
            const double dy = memberPosition.y - userPosition.y;
            const double dz = memberPosition.z - userPosition.z;
            if (dx * dx + dy * dy + dz * dz >= affectRangeSquared) {
                continue;
            }

            skill->ActivateSkill(member, member, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
            FOR_ALL_SUMMON_IN_SUMMON_LIST(
                member, summon, skill->ActivateSkill(summon, summon, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
            );
        }
    }

    const int itemIndex = static_cast<int>(item->m_nId);
    const FVector creaturePosition = creature->GetPos();
    if (!creature->IsGMInvisible()) {
        const int distance = creature->GetBroadcastDist(0x1000);
        if (creature->IsPC()) {
            User* const accountIdUser = creature->CastUser();
            User* const databaseIdUser = creature->CastUser();
            User* const accountNameUser = creature->CastUser();
            const std::wstring accountName = target_utf16::ToHostCodeUnits(accountNameUser->m_sAccountName.data());
            // Log.Add(
            //     LOG_BOTH,
            //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
            //     L"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,,",
            //     static_cast<std::int64_t>(0x3cd),
            //     static_cast<std::int64_t>(databaseIdUser->GetDBID()),
            //     static_cast<std::int64_t>(accountIdUser->GetAccountID()),
            //     static_cast<std::int64_t>(creaturePosition.x),
            //     static_cast<std::int64_t>(creaturePosition.y),
            //     static_cast<std::int64_t>(creaturePosition.z),
            //     static_cast<std::int64_t>(creature->m_data->m_Race),
            //     static_cast<std::int64_t>(creature->m_data->m_Sex),
            //     static_cast<std::int64_t>(creature->m_data->m_nClass),
            //     static_cast<std::int64_t>(creature->m_data->m_nLevel),
            //     static_cast<std::int64_t>(item->m_data->m_nClassID),
            //     creature->GetName(),
            //     accountName.c_str())
            Log.Add(
                LOG_BOTH,
                u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                u"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,,",
                static_cast<std::int64_t>(0x3cd), static_cast<std::int64_t>(databaseIdUser->GetDBID()),
                static_cast<std::int64_t>(accountIdUser->GetAccountID()), static_cast<std::int64_t>(creaturePosition.x),
                static_cast<std::int64_t>(creaturePosition.y), static_cast<std::int64_t>(creaturePosition.z),
                static_cast<std::int64_t>(creature->m_data->m_Race), static_cast<std::int64_t>(creature->m_data->m_Sex),
                static_cast<std::int64_t>(creature->m_data->m_nClass), static_cast<std::int64_t>(creature->m_data->m_nLevel),
                static_cast<std::int64_t>(item->m_data->m_nClassID), creature->GetName(), accountName.c_str()
            );
        }

        const FVector itemPosition = item->GetPos();
        g_ptlUserMap->BroadcastToNeighborDist(
            creature, 0xa000, itemPosition, distance, "cddddd", 0x17, static_cast<int>(creature->m_nId), itemIndex,
            runtime::Win64CvttDoubleToInt32(creaturePosition.x), runtime::Win64CvttDoubleToInt32(creaturePosition.y),
            runtime::Win64CvttDoubleToInt32(creaturePosition.z)
        );

        if (lastSkill != nullptr) {
            creature->m_IsSheathingWeapon = false;
            SkillUseInfo use{};
            use.m_skillID = lastSkill->m_nSkillId;
            use.m_skillLevel = lastSkill->m_nLevel;
            use.m_skillSubLevel = lastSkill->m_nSubLevel;
            int zero1 = 0;
            int zero2 = 0;
            int zero3 = 0;
            int zero4 = 0;
            int zero5 = 0;
            int zero6 = 0;
            DefaultIOBuffer* const packet =
                creature->MakePacketForMagicSkillUse(zero1, creature, use, zero2, zero3, zero4, zero5, zero6, nullptr);
            g_ptlUserMap->BroadcastToNeighborDist(creature, 0x5000, creaturePosition, distance, packet);
            packet->Release();
        }
    } else if (creature->IsPC()) {
        creature->CastUser()->GetSocket()->Send(
            "cdddddd", 0x17, static_cast<int>(creature->m_nId), itemIndex,
            runtime::Win64CvttDoubleToInt32(creaturePosition.x), runtime::Win64CvttDoubleToInt32(creaturePosition.y),
            runtime::Win64CvttDoubleToInt32(creaturePosition.z)
        );
        if (lastSkill != nullptr) {
            creature->m_IsSheathingWeapon = false;
            SkillUseInfo use{};
            use.m_skillID = lastSkill->m_nSkillId;
            use.m_skillLevel = lastSkill->m_nLevel;
            use.m_skillSubLevel = lastSkill->m_nSubLevel;
            int zero1 = 0;
            int zero2 = 0;
            int zero3 = 0;
            int zero4 = 0;
            int zero5 = 0;
            int zero6 = 0;
            creature->CastUser()->SendMagicSkill(zero1, creature, use, zero2, zero3, zero4, zero5, zero6, nullptr);
        }
    }

    PickAndDeleteItem(item);
}

bool CWorld::PickItem(CItem* item, CCreature* creature, bool force) {
    L2_GUARD_FUNCTION();
    if (item == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "item null at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0xcf5)
        L2_LOG_ERROR(Log, "[world items] Pickup execution has no item; operation rejected.");
        return false;
    }
    if (creature == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "creature null at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0xcfb)
        L2_LOG_ERROR(
            Log, "[world items] Pickup execution for database item %u has no creature; operation rejected.", item->GetDBID()
        );
        return false;
    }

    if (!force) {
        const FVector creaturePosition = creature->GetPos();
        const FVector itemPosition = item->GetPos();
        const double dx = itemPosition.x - creaturePosition.x;
        const double dy = itemPosition.y - creaturePosition.y;
        const double dz = itemPosition.z - creaturePosition.z;
        if (dx * dx + dy * dy + dz * dz > 262144.0) {
            return false;
        }
    }

    {
        L2_GUARD(*item->m_lock);
        if (!item->m_data->m_bIsActive) {
            return false;
        }
        item->m_data->m_bIsActive = false;
    }

    const std::int64_t originalAmount = item->m_data->m_nCount;
    if (((originalAmount < 1) && item->m_data->m_ItemConsumeType != ICT_CHARGE) || originalAmount < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Try to get invalid amount[%d].",
        //     L"world_server.cpp",
        //     0xd1f,
        //     runtime::Int32FromBits(
        //         static_cast<std::uint32_t>(originalAmount)))
        L2_LOG_ERROR(
            Log, u"[world items] Database item %u has invalid pickup amount %I64d; pickup rejected.", item->GetDBID(),
            originalAmount
        );
        DoAsyncPickItem(item);
        return false;
    }

    if (item->m_const->m_bExImmediateEffect) {
        PickAndUseItem(item, creature);
        return true;
    }
    if (item->m_const->m_bForceEquip) {
        return PickAndEquipItem(item, creature);
    }

    CItem* const originalItem = item;
    CItem* pickedItem = item;
    User* const picker = creature->GetUserOrMaster();
    const bool fakeItemPickup = picker != nullptr &&
        ((static_cast<std::uint32_t>(item->m_data->index) &
             // 0xf8000000U) == 0x30000000U ||
             CLASS_MASK) == (SCLS_FAKE_ITEM << CLASS_SHIFT) ||
            picker->m_bFakeItemState);

    if (fakeItemPickup) {
        const FVector itemPosition = item->GetPos();
        {
            L2_GUARD(picker->m_HadFakeItemLock);
            picker->m_bHadFakeItem = true;
        }

        const std::u16string ipAddress = picker->GetSocket()->GetIPAddressString();
        const std::u16string ipAddress16 = std::u16string(ipAddress.c_str());
        const std::u16string pickerName = std::u16string(picker->GetName());
        CLogFormatter audit;
        audit.WriteAuditLog(
            0x585, picker->GetDBID(), picker->GetAccountID(), 0, 0, WorldCoordinate(itemPosition.x),
            WorldCoordinate(itemPosition.y), WorldCoordinate(itemPosition.z), ipAddress16.c_str(), u"", u"", 9, 0, 0, 0, 0,
            0, 0, 0, 0, picker->Level(), pickerName.c_str(), picker->m_sAccountName.data(), u"", u"", 0
        );

        FOR_ALL_OBJECT_IN_OBJECT_MAP(g_GMCharMap, user, {
            if (user == nullptr || !user->IsBuilder()) {
                continue;
            }
            const FVector observer = user->GetPos();
            const double dx = itemPosition.x - observer.x;
            const double dy = itemPosition.y - observer.y;
            if (dx * dx + dy * dy >= 16777216.0) {
                continue;
            }
            const int objectId = static_cast<int>(picker->m_nId);
            const int x = WorldCoordinate(itemPosition.x);
            const int y = WorldCoordinate(itemPosition.y);
            const int z = WorldCoordinate(itemPosition.z);
            user->GetSocket()->Send(
                "cdddddddddddhhddddd", 0x48, 0, objectId, objectId, 0x7e8, 1, 500, 0, 0x5dc, x, y, z, 0, 2, x, y, z, 0, 0
            );
            user->GetSocket()->Send("cdddddd", 0x54, 0, objectId, 0x7e8, 1, 1, objectId);
        });

        const int classId = picker->m_bFakeItemState ? item->m_data->m_nClassID : g_nFakeItemClassID;
        if (classId < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] ItemClassID[%d] is less equal than 0",
            //     L"world_server.cpp",
            //     0xd8c,
            //     classId)
            L2_LOG_ERROR(Log, u"[fake item] Resolved item class %d is invalid; pickup rejected.", classId);
            return false;
        }
        CObject* const prototype = g_ObjectDB.GetObjectW(classId, false);
        if (prototype == nullptr || !prototype->IsItem()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Item[%d] is not exist.",
            //     L"world_server.cpp",
            //     0xd72,
            //     classId)
            L2_LOG_ERROR(Log, u"[fake item] Item class %d is missing or is not an item; pickup rejected.", classId);
            return false;
        }
        pickedItem = g_ObjectDB.CreateItem(classId);
        if (pickedItem == nullptr || !pickedItem->IsItem()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Item[%d] is not exist.",
            //     L"world_server.cpp",
            //     0xd7b,
            //     classId)
            L2_LOG_ERROR(Log, u"[fake item] Failed to create item class %d; pickup rejected.", classId);
            return false;
        }
        pickedItem->m_data->SetPos(itemPosition);
        pickedItem->m_nTraceCharId = 0;
        pickedItem->SetDBID(0);
        if (!pickedItem->SetAmount(originalAmount)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] item amount is invalid[%lld][%lld]",
            //     L"world_server.cpp",
            //     0xd85,
            //     static_cast<long long>(
            //         pickedItem->m_data->m_nCount),
            //     static_cast<long long>(originalAmount))
            L2_LOG_ERROR(
                Log, u"[fake item] Created item class %d rejected pickup amount %lld; current amount is %lld.", classId,
                static_cast<long long>(originalAmount), static_cast<long long>(pickedItem->m_data->m_nCount)
            );
            return false;
        }
    }

    if (pickedItem->m_nClassId == 0x39 && pickedItem->GetDBID() == 0) {
        m_AdenaDropByNpcs += pickedItem->m_data->m_nCount;
        const std::int64_t delta = m_AdenaDropByNpcs - m_AdenaDropByNpcsOld;
        if (delta > 10000000) {
            User* const adenaPicker = creature->GetUserOrMaster();
            if (adenaPicker != nullptr) {
                CLogFormatter formatter;
                formatter.WriteLog2(0x2329, adenaPicker, nullptr, delta, m_AdenaDropByNpcs, 0, 0, 0, 0, 0);
                m_AdenaDropByNpcsOld = m_AdenaDropByNpcs;
            }
        }
    }

    CParty* const party = creature->GetParty();
    User* const routingPicker = creature->GetUserOrMaster();
    bool routed = false;
    if (party == nullptr || routingPicker == nullptr) {
        if (!creature->CanDoAction_china()) {
            creature->SendSystemMessage(SystemMessageId::CANNOT_ACT_DUE_TO_FATIGUE);
        } else if (PickItemCond(originalItem, creature)) {
            if (!force) {
                DoAsyncPickItem(originalItem);
            }
            DoAsyncAddItemToInventory(pickedItem, creature);
            routed = true;
            if (routingPicker != nullptr) {
                pickedItem->LogRelateItem(*routingPicker, pickedItem->m_data->m_nCount);
                if (pickedItem->m_nClassId == 0x39 && pickedItem->m_nTraceNPCClassId != 0) {
                    routingPicker->m_UserStatData.AddUserStat2(UserStat::US_ADENA, pickedItem->m_data->m_nCount);
                }
            }
        }
    } else {
        routed = party->Route(pickedItem, routingPicker, force);
    }

    if (!routed) {
        L2_GUARD(*pickedItem->m_lock);
        pickedItem->m_data->m_bIsActive = true;
        return false;
    }

    if (pickedItem->m_nClassId == 0x39 && creature->IsPC()) {
        creature->CastUser()->CheckTutorialEvent(TCE_PICK_ADENA);
    }
    if (pickedItem->m_nClassId == 0x18d1 && creature->IsPC()) {
        creature->CastUser()->CheckTutorialEvent(TCE_PICK_BLUE_GEM);
    }

    if (fakeItemPickup) {
        originalItem->Delete();
    } else if (party != nullptr && pickedItem->m_nClassId == 0x39) {
        pickedItem->Delete();
    }

    if (!force) {
        const FVector itemPosition = pickedItem->GetPos();
        const FVector creaturePosition = creature->GetPos();
        if (!creature->IsGMInvisible()) {
            g_ptlUserMap->BroadcastToNeighborDist(
                creature, 0xa000, itemPosition, 0x800, "cddddd", 0x17, static_cast<int>(creature->m_nId),
                static_cast<int>(pickedItem->m_nId), WorldCoordinate(creaturePosition.x),
                WorldCoordinate(creaturePosition.y), WorldCoordinate(creaturePosition.z)
            );
        } else if (creature->IsPC()) {
            creature->CastUser()->GetSocket()->Send(
                "cdddddd", 0x17, static_cast<int>(creature->m_nId), static_cast<int>(pickedItem->m_nId),
                WorldCoordinate(creaturePosition.x), WorldCoordinate(creaturePosition.y), WorldCoordinate(creaturePosition.z)
            );
        }
    }
    return true;
}

void CWorld::DoAsyncPickItem(CItem* item) {
    L2_GUARD_FUNCTION();
    if (item == nullptr) {
        return;
    }
    item->SetNextInZoneID(0);
    L2_ADD_REF(item, ObjectRefType::SerializedWorldThread);

    __async_arg_AsyncPickItem argument(item);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncPickItem(pad); }, &argument,
        L"AsyncPickItem"
    );
}

void CWorld::__async_AsyncPickItem(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CItem* item = nullptr;
    std::memcpy(&item, argument->Buffer + 0x08, sizeof(item));
    ReleaseHelper<CItem, ObjectRefType::SerializedWorldThread> releaseItem(item, "world_server.cpp", 0x5cc);

    CContainer* const container = CContainerDB::Inst().GetContainer(item->m_data->container_index);
    if (container == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Getitem failed : item position container index (%d), "
        //     "file[%s], line[%d]",
        //     item->m_data->container_index,
        //     "world_server.cpp",
        //     0x5d2)
        L2_LOG_ERROR(
            Log, "[world items] Database item %u references missing container %d; removal from the world aborted.",
            item->GetDBID(), item->m_data->container_index
        );
        return;
    }

    container->PopItem(item);
    CContainerIndex neighbor = CContainerDB::Inst().GatherNeighborContainerIndex(container);
    CContainerFilterNull filter;
    class ContainerFunc final : public CContainerFunc {
    public:
        explicit ContainerFunc(CItem* item) : m_item(item) {}

        void Do(CObject* object) override {
            if (object == m_item) {
                return;
            }
            if (object->IsPC() && (object->IsDeleting() || object->m_Deleted != 0)) {
                return;
            }
            object->OutOfSight(m_item, true);
        }

    private:
        CItem* m_item;
    } function(item);
    FOR_ALL_OBJECT_IN_CONTAINER_INDEX(neighbor, filter, function);

    item->m_data->m_bIsActive = false;
    item->m_ContributeDataID = CSmartID();
    item->m_bDroppedByAI = false;
    if (CInstantZone* const zone = item->GetInstantZone()) {
        zone->LeaveZone(item);
    }
    item->SetInZoneID(item->GetNextInZoneID());
    g_ptlItemMap->Remove(item->m_data->index, item);
}

void CWorld::DoAsyncAddItemToInventory(CItem* item, CCreature* creature) {
    L2_GUARD_FUNCTION();
    if (item == nullptr || creature == nullptr) {
        return;
    }
    L2_ADD_REF(item, ObjectRefType::SerializedWorldThread);
    L2_ADD_REF(creature, ObjectRefType::SerializedWorldThread);

    __async_arg_AsyncAddItemToInventory argument(item, creature);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncAddItemToInventory(pad); }, &argument,
        L"AsyncAddItemToInventory"
    );
}

void CWorld::DoAsyncAddItemToInventory(int itemClassId, std::int64_t count, CCreature* creature) {
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return;
    }

    L2_ADD_REF(creature, ObjectRefType::SerializedWorldThread);
    __async_arg_AsyncAddItemToInventoryWithparams argument(creature, itemClassId, count);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncAddItemToInventoryWithparams(pad); },
        &argument, L"AsyncAddItemToInventoryWithparams"
    );
}

void CWorld::__async_AsyncAddItemToInventory(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CItem* item = nullptr;
    CCreature* creature = nullptr;
    std::memcpy(&item, argument->Buffer + 0x08, sizeof(item));
    std::memcpy(&creature, argument->Buffer + 0x10, sizeof(creature));

    if (item == nullptr || creature == nullptr) {
        if (item != nullptr) {
            L2_RELEASE(item, ObjectRefType::SerializedWorldThread);
        }
        if (creature != nullptr) {
            L2_RELEASE(creature, ObjectRefType::SerializedWorldThread);
        }
        return;
    }

    if (creature->IsPC()) {
        User* const user = creature->CastUser();
        if (user != nullptr) {
            db.RequestPickItem(user, item);
            creature->AnnouncePickItem(item);
        }
    } else if (creature->IsPet()) {
        CPet* const pet = creature->CastPet();
        if (pet != nullptr) {
            User* const master = pet->GetMaster();
            const unsigned int masterSid = master != nullptr ? master->Id() : 0;
            db.PetGetItem(masterSid, *pet, item);
            creature->AnnouncePickItem(item);
        }
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"pCreature is not User at file[%s], line[%d]",
        //     L"world_server.cpp",
        //     0x1628)
        L2_LOG_ERROR(
            Log, u"[inventory] Creature %u is neither a player nor a pet; database item %u cannot be added.", creature->Id(),
            item->GetDBID()
        );
    }

    L2_RELEASE(item, ObjectRefType::SerializedWorldThread);
    L2_RELEASE(creature, ObjectRefType::SerializedWorldThread);
}

void CWorld::__async_AsyncAddItemToInventoryWithparams(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CCreature* creature = nullptr;
    int itemClassId = 0;
    std::int64_t requestedCount = 0;
    std::memcpy(&creature, argument->Buffer + 0x08, sizeof(creature));
    std::memcpy(&itemClassId, argument->Buffer + 0x10, sizeof(itemClassId));
    std::memcpy(&requestedCount, argument->Buffer + 0x18, sizeof(requestedCount));

    if (creature == nullptr) {
        return;
    }

    InventoryParams parameters;
    parameters.nItemClassID = itemClassId;
    parameters.nCount = requestedCount;
    CItem* const prototype = reinterpret_cast<CItem*>(g_ObjectDB.GetObjectW(itemClassId, false));
    prototype->SetDBID(0);

    if (!creature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"pCreature is not User at file[%s], line[%d]",
        //     L"world_server.cpp",
        //     0x1666)
        L2_LOG_ERROR(
            Log, u"[inventory] Creature %u is not a player; item class %d amount %I64d cannot be added.", creature->Id(),
            itemClassId, requestedCount
        );
    } else if (User* const user = creature->CastUser()) {
        std::array<char, 20480> packet;
        int packetSize = 0;
        int itemCount = 0;
        if (CSharedItemData::IsStackable(prototype->m_data->m_ItemConsumeType)) {
            if (MsgHelper_ManipulateItem(
                    packet.data(), static_cast<int>(packet.size()), packetSize, prototype, parameters
                ) != 0) {
                itemCount = 1;
            }
        } else {
            parameters.nCount = 1;
            int index = 0;
            while (static_cast<std::int64_t>(index) < requestedCount) {
                if (MsgHelper_ManipulateItem(
                        packet.data(), static_cast<int>(packet.size()), packetSize, prototype, parameters
                    ) != 0) {
                    itemCount = runtime::AddInt32Wrapping(itemCount, 1);
                }
                index = runtime::AddInt32Wrapping(index, 1);
            }
        }

        db.CreateUpdateItems(user, itemCount, packetSize, packet.data(), nullptr, 0, nullptr, false);
        user->AnnouncePickItem(prototype);
    }

    L2_RELEASE(creature, ObjectRefType::SerializedWorldThread);
}

void CWorld::__async_AsyncPickAndDeleteItem(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CItem* item;
    std::memcpy(&item, argument->Buffer + 0x08, sizeof(item));

    CContainer* const container = CContainerDB::Inst().GetContainer(item->m_data->container_index);
    if (container == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Getitem failed : item position container index (%d), "
        //     "file[%s], line[%d]",
        //     item->m_data->container_index,
        //     "world_server.cpp",
        //     0x58b)
        L2_LOG_ERROR(
            Log, "[world items] Database item %u references missing container %d; pickup-and-delete aborted.",
            item->GetDBID(), item->m_data->container_index
        );
        L2_RELEASE(item, ObjectRefType::SerializedWorldThread);
        return;
    }

    container->PopItem(item);
    CContainerIndex neighbor = CContainerDB::Inst().GatherNeighborContainerIndex(container);
    CContainerFilterNull filter;
    class ContainerFunc final : public CContainerFunc {
    public:
        explicit ContainerFunc(CItem* item) : m_item(item) {}

        void Do(CObject* object) override {
            if (object == m_item) {
                return;
            }
            if (object->IsPC() && (object->IsDeleting() || object->m_Deleted != 0)) {
                return;
            }
            object->OutOfSight(m_item, true);
        }

    private:
        CItem* m_item;
    } function(item);
    FOR_ALL_OBJECT_IN_CONTAINER_INDEX(neighbor, filter, function);

    item->m_data->m_bIsActive = false;
    g_ptlItemMap->Remove(item->m_data->index, item);
    L2_RELEASE(item, ObjectRefType::SerializedWorldThread);
    item->Delete();
}

void CWorld::DeleteInZoneItem(CItem* item) {
    L2_GUARD_FUNCTION();
    L2_ADD_REF(item, ObjectRefType::SerializedWorldThread);

    __async_arg_AsyncDeleteInZoneItem argument(item);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncDeleteInZoneItem(pad); }, &argument,
        L"AsyncDeleteInZoneItem"
    );
}

void CWorld::__async_AsyncDeleteInZoneItem(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CItem* item;
    std::memcpy(&item, argument->Buffer + 0x08, sizeof(item));

    CContainer* const container = CContainerDB::Inst().GetContainer(item->m_data->container_index);
    if (container == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Getitem failed : item[%d] position container index (%d), "
        //     "file[%s], line[%d]",
        //     item->m_data->m_nClassID,
        //     item->m_data->container_index,
        //     "world_server.cpp",
        //     0x5bb)
        L2_LOG_ERROR(
            Log, "[world items] Item class %d references missing container %d; instance-item deletion aborted.",
            item->m_data->m_nClassID, item->m_data->container_index
        );
        L2_RELEASE(item, ObjectRefType::SerializedWorldThread);
        return;
    }

    container->PopItem(item);
    g_ptlItemMap->Remove(item->m_data->index, item);
    item->SetInZoneID(0);
    L2_RELEASE(item, ObjectRefType::SerializedWorldThread);
    item->Delete();
}

bool CWorld::GetOnVehicle(CCreature* creature, CVehicle* vehicle, FVector position) {
    // Current L2Server 0x00c5f0e0.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || vehicle == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"error in CWorld::GetOnVehicle()")
        L2_LOG_ERROR(Log, u"[vehicle] Board request is missing the creature or vehicle; request rejected.");
        return false;
    }
    if (!creature->m_data->m_bIsActive) {
        return false;
    }

    L2_ADD_REF(creature, ObjectRefType::SerializedWorldThread);
    L2_ADD_REF(vehicle, ObjectRefType::SerializedWorldThread);
    AsyncGetOnVehicle(creature, vehicle, position);
    return true;
}

bool CWorld::GetOffVehicle(CCreature* creature, CVehicle* vehicle, FVector position, bool) {
    L2_GUARD_FUNCTION();
    if (creature == nullptr || vehicle == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"error in CWorld::GetOffVehicle()")
        L2_LOG_ERROR(Log, u"[vehicle] Disembark request is missing the creature or vehicle; request rejected.");
        return false;
    }
    if (!creature->m_data->m_bIsActive) {
        return false;
    }

    L2_ADD_REF(creature, ObjectRefType::SerializedWorldThread);
    L2_ADD_REF(vehicle, ObjectRefType::SerializedWorldThread);
    AsyncGetOffVehicle(creature, vehicle, position);
    return true;
}

bool CWorld::GetOnAirShip(CCreature* creature, CAirShip* airShip) {
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] pCreature is NULL!!!",
        //     L"world_server.cpp",
        //     0x10cd)
        L2_LOG_ERROR(Log, u"[airship] Board request has no creature; request rejected.");
        return false;
    }
    if (airShip == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] pAirShip is NULL!!!",
        //     L"world_server.cpp",
        //     0x10d3)
        L2_LOG_ERROR(Log, u"[airship] Board request for creature %u has no airship; request rejected.", creature->Id());
        return false;
    }

    CContainer* const source = CContainerDB::Inst().GetContainer(creature->m_data->container_index);
    if (source == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"pContFrom null index[%d] creature[%s] "
        //     L"at file[%s], line[%d]",
        //     creature->m_data->container_index,
        //     creature->GetName(),
        //     L"world_server.cpp",
        //     0x10dc)
        // L2_LOG_ERROR(Log,
        //     L"[airship] Character '%s' has missing source container %d. Board request rejected.",
        //     creature->GetName(),
        //     creature->m_data->container_index)
        L2_LOG_ERROR(
            Log, u"[airship] Character '%s' has missing source container %d; board request rejected.", creature->GetName(),
            creature->m_data->container_index
        );
        runtime::process::ReportCallStack(2, false);
        return false;
    }
    if (airShip->m_pContainer == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] AsyncGetOnAirShip : "
        //     L"pAirShipContainer is NULL!!!",
        //     L"world_server.cpp",
        //     0x10e5)
        // L2_LOG_ERROR(Log,
        //     L"[airship] Airship %u has no container. Board request for character '%s' rejected.",
        //     airShip->Id(),
        //     creature->GetName())
        L2_LOG_ERROR(
            Log, u"[airship] Airship %u has no container; board request for character '%s' rejected.", airShip->Id(),
            creature->GetName()
        );
        runtime::process::ReportCallStack(2, false);
        return false;
    }

    L2_ADD_REF(creature, ObjectRefType::SerializedWorldThread);
    L2_ADD_REF(airShip, ObjectRefType::SerializedWorldThread);
    AsyncGetOnAirShip(creature, airShip);
    return true;
}

bool CWorld::OnAppearingIntoAirShip(CAirShip* airShip, User* user) {
    // Current L2Server 0x00c6631c.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] pUser is NULL!!!",
        //     L"world_server.cpp",
        //     0x112b)
        L2_LOG_ERROR(Log, u"[airship] Appearance request has no user; request rejected.");
        return false;
    }
    if (airShip == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] pAirShip is NULL!!!",
        //     L"world_server.cpp",
        //     0x1131)
        L2_LOG_ERROR(Log, u"[airship] Appearance request for user %u has no airship; request rejected.", user->Id());
        return false;
    }

    L2_ADD_REF(user, ObjectRefType::SerializedWorldThread);
    L2_ADD_REF(airShip, ObjectRefType::SerializedWorldThread);
    AsyncAppearIntoAirShip(user, airShip);
    return true;
}

bool CWorld::GetOffAirShip(CCreature* creature, CAirShip* airShip, FVector position) {
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] pCreature is NULL!!!",
        //     L"world_server.cpp",
        //     0x10fa)
        L2_LOG_ERROR(Log, u"[airship] Disembark request has no creature; request rejected.");
        return false;
    }
    if (airShip == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] pAirShip is NULL!!!",
        //     L"world_server.cpp",
        //     0x1100)
        L2_LOG_ERROR(Log, u"[airship] Disembark request for creature %u has no airship; request rejected.", creature->Id());
        return false;
    }
    if (!IsValidPosition(WorldCoordinate(position.x), WorldCoordinate(position.y))) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid pos(%d, %d, %d)!!!",
        //     L"world_server.cpp",
        //     0x1106,
        //     WorldCoordinate(position.x),
        //     WorldCoordinate(position.y),
        //     WorldCoordinate(position.z))
        L2_LOG_ERROR(
            Log, u"[airship] Disembark destination (%d, %d, %d) is outside the world; request for creature %u rejected.",
            WorldCoordinate(position.x), WorldCoordinate(position.y), WorldCoordinate(position.z), creature->Id()
        );
        return false;
    }

    CContainer* const source = CContainerDB::Inst().GetContainer(creature->m_data->container_index);
    if (source == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] pContFrom null index[%d] creature[%s]",
        //     L"world_server.cpp",
        //     0x110e,
        //     creature->m_data->container_index,
        //     creature->GetName())
        // L2_LOG_ERROR(Log,
        //     L"[airship] Character '%s' has missing source container %d. Disembark request rejected.",
        //     creature->GetName(),
        //     creature->m_data->container_index)
        L2_LOG_ERROR(
            Log, u"[airship] Character '%s' has missing source container %d; disembark request rejected.",
            creature->GetName(), creature->m_data->container_index
        );
        runtime::process::ReportCallStack(2, false);
        return false;
    }

    CContainer* const destination = CContainerDB::Inst().GetWorldContainer(
        WorldCoordinate(position.x), WorldCoordinate(position.y), WorldCoordinate(position.z)
    );
    if (destination == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] pContTo null (%d, %d, %d)!!!",
        //     L"world_server.cpp",
        //     0x1117,
        //     WorldCoordinate(position.x),
        //     WorldCoordinate(position.y),
        //     WorldCoordinate(position.z))
        L2_LOG_ERROR(
            Log, u"[airship] No destination container covers (%d, %d, %d); disembark request for creature %u rejected.",
            WorldCoordinate(position.x), WorldCoordinate(position.y), WorldCoordinate(position.z), creature->Id()
        );
        runtime::process::ReportCallStack(2, false);
        return false;
    }

    L2_ADD_REF(creature, ObjectRefType::SerializedWorldThread);
    L2_ADD_REF(airShip, ObjectRefType::SerializedWorldThread);
    AsyncGetOffAirShip(creature, airShip, position);
    return true;
}

bool CWorld::GetOnShuttle(CCreature* creature, CShuttle* shuttle, FVector position) {
    // Current L2Server 0x00c600fc.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || shuttle == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"error in CWorld::GetOnShuttle()")
        L2_LOG_ERROR(Log, u"[shuttle] Board request is missing the creature or shuttle; request rejected.");
        return false;
    }
    if (!creature->m_data->m_bIsActive) {
        return false;
    }

    L2_ADD_REF(creature, ObjectRefType::SerializedWorldThread);
    L2_ADD_REF(shuttle, ObjectRefType::SerializedWorldThread);
    AsyncGetOnShuttle(creature, shuttle, position);
    return true;
}

bool CWorld::GetOffShuttle(CCreature* creature, CShuttle* shuttle, FVector position, bool) {
    L2_GUARD_FUNCTION();
    if (creature == nullptr || shuttle == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"error in CWorld::GetOffShuttle()")
        L2_LOG_ERROR(Log, u"[shuttle] Disembark request is missing the creature or shuttle; request rejected.");
        return false;
    }
    if (!creature->m_data->m_bIsActive) {
        return false;
    }

    L2_ADD_REF(creature, ObjectRefType::SerializedWorldThread);
    L2_ADD_REF(shuttle, ObjectRefType::SerializedWorldThread);
    AsyncGetOffShuttle(creature, shuttle, position);
    return true;
}

bool CWorld::LeaveWorld(CCreature* creature, FVector position, bool force) {
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "creature null at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0x81b)
        L2_LOG_ERROR(Log, "[world lifecycle] Leave-world request has no creature; request rejected.");
        return false;
    }
    if (creature->m_data == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "creature data null at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0x821)
        L2_LOG_ERROR(Log, "[world lifecycle] Creature %u has no shared data; leave-world request rejected.", creature->Id());
        return false;
    }
    if (!force) {
        L2_ADD_REF(creature, ObjectRefType::SerializedWorldThread);
        AsyncLeaveWorld(creature, creature->GetPos(), position);
    }
    return true;
}

void CWorld::AsyncEnterWorld(CCreature* creature, FVector position, unsigned int inZoneId, bool activate) {
    __async_arg_AsyncEnterWorld argument(creature, position, inZoneId, activate);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncEnterWorld(pad); }, &argument,
        L"AsyncEnterWorld"
    );
}

bool CWorld::EnterWorld(CCreature* creature, FVector position, unsigned int inZoneId, bool activate, bool suppressQueue) {
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "creature null at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0x7aa)
        L2_LOG_ERROR(Log, "[world lifecycle] Enter-world request has no creature; request rejected.");
        return false;
    }
    if (creature->m_data == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "creature data null at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0x7b0)
        L2_LOG_ERROR(Log, "[world lifecycle] Creature %u has no shared data; enter-world request rejected.", creature->Id());
        return false;
    }

    creature->m_bIsRestarting = false;
    if (creature->IsPC()) {
        creature->m_data->m_bIsActive = true;
        User* const user = creature->CastUser();
        user->SendUserInfo(nullptr, nullptr);
        user->SendVitalityEffectInfo(nullptr);
        if (g_nCheckCastleId == 2) {
            user->m_nCurCastleId = user->m_AreaMod.m_bIsInBattleField ? CCastle::GetBattleFieldCastleId(user->GetPos()) : 0;
        }
        if (activate) {
            user->OnActivate();
            position.z += 5.0;
            // Current 0x00c65232 compares shared-data +0x3bec
            // (m_moveType), not adjacent movement presentation state.
            if (static_cast<std::int32_t>(user->m_data->m_moveType) != CMT_FLY && !user->m_AreaMod.m_bIsUnderWater) {
                position.z = static_cast<double>(Inst().GetGeo()->FindGround(
                    WorldCoordinate(position.x), WorldCoordinate(position.y), WorldCoordinate(position.z)
                ));
                user->m_data->SetPos(position);
            }
        }
        user->SendQuestList(nullptr);
        if (g_bFreeOpenServer) {
            user->GetSocket()->Send(
                "chdc", 0xfe, 0xda, static_cast<int>(user->Id()),
                static_cast<int>(!user->CheckPremiumType(PremiumUserType::PUT_FREE_USER))
            );
        }
    }

    if (!Inst().IsValidPosition(WorldCoordinate(position.x), WorldCoordinate(position.y))) {
        // Log.Add(
        //     LOG_ERROR,
        //     "invalid position (%d, %d) at file[%s], line[%d]",
        //     WorldCoordinate(position.x),
        //     WorldCoordinate(position.y),
        //     "world_server.cpp",
        //     0x7ed)
        L2_LOG_ERROR(
            Log, "[world lifecycle] Enter-world destination (%d, %d) is invalid for creature %u; request rejected.",
            WorldCoordinate(position.x), WorldCoordinate(position.y), creature->Id()
        );
        return false;
    }

    creature->m_nBeforeEnterWorldTick = runtime::GetTickCount();
    if (!suppressQueue) {
        L2_ADD_REF(creature, ObjectRefType::SerializedWorldThread);
        AsyncEnterWorld(creature, position, inZoneId, activate);
    }
    return true;
}

bool CWorld::GodsVoice(wchar_t* const message) {
    // Current L2Server 0x00c5f358, original world_server.cpp owner.
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_NORMAL,
    //     L"Gods Voice : [%s]",
    //     message)
    L2_LOG_INFO(Log, u"[announcement] Broadcasting administrator message: '%s'.", message);
    CUserBroadcaster::inst().BroadcastToAllUserAnnounce(WideToUtf16(message).c_str());
    return true;
}

void CWorld::VehicleEnterWorld(CVehicle* vehicle, FVector position) {
    L2_GUARD_FUNCTION();
    L2_ADD_REF(vehicle, ObjectRefType::SerializedWorldThread);

    __async_arg_AsyncVehicleEnterWorld argument(vehicle, position);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncVehicleEnterWorld(pad); }, &argument,
        L"AsyncVehicleEnterWorld"
    );
}

void CWorld::VehicleLeaveWorld(CVehicle* vehicle) {
    L2_GUARD_FUNCTION();
    L2_ADD_REF(vehicle, ObjectRefType::SerializedWorldThread);

    __async_arg_AsyncVehicleLeaveWorld argument(vehicle);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncVehicleLeaveWorld(pad); }, &argument,
        L"AsyncVehicleLeaveWorld"
    );
}

void CWorld::VehicleMove(CVehicle* vehicle, FVector position) {
    L2_GUARD_FUNCTION();
    L2_ADD_REF(vehicle, ObjectRefType::SerializedWorldThread);

    __async_arg_AsyncVehicleMove argument(vehicle, position);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncVehicleMove(pad); }, &argument,
        L"AsyncVehicleMove"
    );
}

void CWorld::VehicleKickIllegal(CVehicle* vehicle, int ticketClassId, FVector banPosition) {
    L2_GUARD_FUNCTION();
    L2_ADD_REF(vehicle, ObjectRefType::SerializedWorldThread);

    __async_arg_AsyncVehicleKickIllegal argument(vehicle, ticketClassId, banPosition);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncVehicleKickIllegal(pad); }, &argument,
        L"AsyncVehicleKickIllegal"
    );
}

void CWorld::VehicleInformVehicleStart(CVehicle* vehicle, bool start) {
    L2_GUARD_FUNCTION();
    L2_ADD_REF(vehicle, ObjectRefType::SerializedWorldThread);

    __async_arg_AsyncVehicleInformVehicleStart argument(vehicle, start);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncVehicleInformVehicleStart(pad); },
        &argument, L"AsyncVehicleInformVehicleStart"
    );
}

void CWorld::VehicleKickTransformer(CVehicle* vehicle) {
    L2_GUARD_FUNCTION();
    L2_ADD_REF(vehicle, ObjectRefType::SerializedWorldThread);

    __async_arg_AsyncVehicleKickTransformer argument(vehicle);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncVehicleKickTransformer(pad); }, &argument,
        L"AsyncVehicleKickTransformer"
    );
}

bool CWorld::EnterWorld(CShuttle* shuttle, FVector position) {
    L2_GUARD_FUNCTION();
    L2_ADD_REF(shuttle, ObjectRefType::SerializedWorldThread);

    __async_arg_AsyncShuttleEnterWorld argument(shuttle, position);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncShuttleEnterWorld(pad); }, &argument,
        L"AsyncShuttleEnterWorld"
    );
    return true;
}

bool CWorld::LeaveWorld(CShuttle* shuttle) {
    L2_GUARD_FUNCTION();
    L2_ADD_REF(shuttle, ObjectRefType::SerializedWorldThread);

    __async_arg_AsyncShuttleLeaveWorld argument(shuttle);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncShuttleLeaveWorld(pad); }, &argument,
        L"AsyncShuttleLeaveWorld"
    );
    return true;
}

void CWorld::DoorStatusUpdate(CDoor* door) {
    // Current L2Server 0x00c6b460.
    L2_GUARD_FUNCTION();
    L2_ADD_REF(door, ObjectRefType::SerializedWorldThread);

    __async_arg_AsyncDoorStatusUpdate argument(door);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncDoorStatusUpdate(pad); }, &argument,
        L"AsyncDoorStatusUpdate"
    );
}

void CWorld::DoorOpenClose(CDoor* door, bool opened, bool forced) {
    // Current L2Server 0x00c6bc88.
    L2_GUARD_FUNCTION();
    L2_ADD_REF(door, ObjectRefType::SerializedWorldThread);

    __async_arg_AsyncDoorOpenClose argument(door, opened, forced);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncDoorOpenClose(pad); }, &argument,
        L"AsyncDoorOpenClose"
    );
}

bool CWorld::ShuttleKickFromDoor(CDoor* door, FVector banPosition) {
    L2_GUARD_FUNCTION();
    if (door == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"error in CWorld::ShuttleKickFromDoor()")
        L2_LOG_ERROR(Log, u"[shuttle] Door-based ejection request has no door; request rejected.");
        return false;
    }

    L2_ADD_REF(door, ObjectRefType::SerializedWorldThread);
    __async_arg_AsyncShuttleKickFromDoor argument(door, banPosition);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncShuttleKickFromDoor(pad); }, &argument,
        L"AsyncShuttleKickFromDoor"
    );
    return true;
}

bool CWorld::ShuttleMove(CShuttle* shuttle, FVector position) {
    L2_GUARD_FUNCTION();
    L2_ADD_REF(shuttle, ObjectRefType::SerializedWorldThread);

    __async_arg_AsyncShuttleMove argument(shuttle, position);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncShuttleMove(pad); }, &argument,
        L"AsyncShuttleMove"
    );
    return true;
}

bool CWorld::EnterWorld(CAirShip* airShip, FVector position) {
    // Current L2Server 0x00c636b8.
    L2_GUARD_FUNCTION();
    if (airShip == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "AirShip null at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0x1044)
        L2_LOG_ERROR(Log, "[airship] Enter-world request has no airship; request rejected.");
        return false;
    }
    if (airShip->m_data == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "AirShip shared data null at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0x104a)
        L2_LOG_ERROR(Log, "[airship] Airship %u has no shared data; enter-world request rejected.", airShip->Id());
        return false;
    }
    if (!IsValidPosition(WorldCoordinate(position.x), WorldCoordinate(position.y))) {
        // Log.Add(
        //     LOG_ERROR,
        //     "invalid position (%d, %d) at file[%s], line[%d]",
        //     WorldCoordinate(position.x),
        //     WorldCoordinate(position.y),
        //     "world_server.cpp",
        //     0x1051)
        L2_LOG_ERROR(
            Log, "[airship] Enter-world position (%d, %d) is invalid for airship %u; request rejected.",
            WorldCoordinate(position.x), WorldCoordinate(position.y), airShip->Id()
        );
        return false;
    }

    L2_ADD_REF(airShip, ObjectRefType::SerializedWorldThread);
    AsyncAirShipEnterWorld(airShip, position);
    g_ptlAirShipMap->SetTlmBox3(
        airShip->m_tlmBox3, WorldCoordinate(position.x), WorldCoordinate(position.y), WorldCoordinate(position.z)
    );
    g_ptlAirShipMap->Push(airShip->m_data->index, position, airShip);
    return true;
}

bool CWorld::LeaveWorld(CAirShip* airShip) {
    // Current L2Server 0x00c5fe30.
    L2_GUARD_FUNCTION();
    if (airShip != nullptr) {
        L2_ADD_REF(airShip, ObjectRefType::SerializedWorldThread);
        __async_arg_AsyncAirShipLeaveWorld argument(airShip);
        Serializer.AddExecutable(
            this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncAirShipLeaveWorld(pad); }, &argument,
            L"AsyncAirShipLeaveWorld"
        );
    }
    return airShip != nullptr;
}

bool CWorld::AirShipTeleport(CAirShip* airShip, FVector position, unsigned int inZoneId, int direction) {
    // Current L2Server 0x00c6b65c.
    L2_GUARD_FUNCTION();
    if (airShip != nullptr) {
        L2_ADD_REF(airShip, ObjectRefType::SerializedWorldThread);
        AsyncAirShipTeleport(airShip, position, inZoneId, direction);
    }
    return airShip != nullptr;
}

bool CWorld::AirShipMove(CAirShip* airShip, FVector position) {
    L2_GUARD_FUNCTION();
    if (airShip == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[AirShip] airship null at file[%s], line[%d]",
        //     L"world_server.cpp",
        //     0x1088)
        L2_LOG_ERROR(Log, u"[airship] Move request has no airship; request rejected.");
        return false;
    }
    if (!IsValidPosition(WorldCoordinate(position.x), WorldCoordinate(position.y))) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[AirShip] trying to move airship to invalid "
        //     L"position (%d, %d) at file[%s], line[%d]",
        //     WorldCoordinate(position.x),
        //     WorldCoordinate(position.y),
        //     L"world_server.cpp",
        //     0x108f)
        L2_LOG_ERROR(
            Log, u"[airship] Destination (%d, %d) is outside the world; move request for airship %u rejected.",
            WorldCoordinate(position.x), WorldCoordinate(position.y), airShip->Id()
        );
        return false;
    }

    CContainer* const source = CContainerDB::Inst().GetContainer(airShip->m_pContainer->m_Data.m_nParentContainerIndex);
    if (source == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[AirShip] pContFrom null index at file[%s], line[%d]",
        //     L"world_server.cpp",
        //     0x1096)
        L2_LOG_ERROR(Log, u"[airship] Airship %u has no source world container; move rejected.", airShip->Id());
        return false;
    }
    CContainer* const destination = CContainerDB::Inst().GetWorldContainer(
        WorldCoordinate(position.x), WorldCoordinate(position.y), WorldCoordinate(position.z)
    );
    if (destination == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[AirShip] pContTo null pos(%d, %d) at file[%s], line[%d]",
        //     WorldCoordinate(position.x),
        //     WorldCoordinate(position.y),
        //     L"world_server.cpp",
        //     0x109e)
        L2_LOG_ERROR(
            Log, u"[airship] No destination container covers (%d, %d); move request for airship %u rejected.",
            WorldCoordinate(position.x), WorldCoordinate(position.y), airShip->Id()
        );
        return false;
    }

    const int x = WorldCoordinate(position.x);
    const int y = WorldCoordinate(position.y);
    const int z = WorldCoordinate(position.z);
    if (!airShip->m_tlmBox3.Include(x, y, z)) {
        g_ptlAirShipMap->SetTlmBox3(airShip->m_tlmBox3, x, y, z);
        g_ptlAirShipMap->Push(airShip->m_data->index, position, airShip);
    }
    airShip->m_data->SetPos(position);
    L2_ADD_REF(airShip, ObjectRefType::SerializedWorldThread);

    __async_arg_AsyncAirShipMove argument(airShip, destination);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncAirShipMove(pad); }, &argument,
        L"AsyncAirShipMove"
    );
    return true;
}

void CWorld::AsyncGetOnVehicle(CCreature* creature, CVehicle* vehicle, FVector position) {
    // Current L2Server 0x00c5e470.
    __async_arg_AsyncGetOnVehicle argument(creature, vehicle, position);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncGetOnVehicle(pad); }, &argument,
        L"AsyncGetOnVehicle"
    );
}

void CWorld::AsyncGetOffVehicle(CCreature* creature, CVehicle* vehicle, FVector position) {
    __async_arg_AsyncGetOffVehicle argument(creature, vehicle, position);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncGetOffVehicle(pad); }, &argument,
        L"AsyncGetOffVehicle"
    );
}

void CWorld::AsyncLeaveWorld(CCreature* creature, FVector source, FVector destination) {
    __async_arg_AsyncLeaveWorld argument(creature, source, destination);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncLeaveWorld(pad); }, &argument,
        L"AsyncLeaveWorld"
    );
}

void CWorld::AsyncGetOffAirShip(CCreature* creature, CAirShip* airShip, FVector position) {
    __async_arg_AsyncGetOffAirShip argument(creature, airShip, position);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncGetOffAirShip(pad); }, &argument,
        L"AsyncGetOffAirShip"
    );
}

void CWorld::AsyncGetOnAirShip(CCreature* creature, CAirShip* airShip) {
    __async_arg_AsyncGetOnAirShip argument(creature, airShip);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncGetOnAirShip(pad); }, &argument,
        L"AsyncGetOnAirShip"
    );
}

void CWorld::AsyncAirShipEnterWorld(CAirShip* airShip, FVector position) {
    // Current L2Server 0x00c5e67c.
    __async_arg_AsyncAirShipEnterWorld argument(airShip, position);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncAirShipEnterWorld(pad); }, &argument,
        L"AsyncAirShipEnterWorld"
    );
}

void CWorld::AsyncAirShipTeleport(CAirShip* airShip, FVector position, unsigned int inZoneId, int direction) {
    // Current L2Server 0x00c6b0f8.
    __async_arg_AsyncAirShipTeleport argument(airShip, position, inZoneId, direction);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncAirShipTeleport(pad); }, &argument,
        L"AsyncAirShipTeleport"
    );
}

void CWorld::AsyncAppearIntoAirShip(User* user, CAirShip* airShip) {
    __async_arg_AsyncAppearIntoAirShip argument(user, airShip);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncAppearIntoAirShip(pad); }, &argument,
        L"AsyncAppearIntoAirShip"
    );
}

void CWorld::AsyncGetOnShuttle(CCreature* creature, CShuttle* shuttle, FVector position) {
    // Current L2Server 0x00c5e6ec.
    __async_arg_AsyncGetOnShuttle argument(creature, shuttle, position);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncGetOnShuttle(pad); }, &argument,
        L"AsyncGetOnShuttle"
    );
}

void CWorld::AsyncGetOffShuttle(CCreature* creature, CShuttle* shuttle, FVector position) {
    __async_arg_AsyncGetOffShuttle argument(creature, shuttle, position);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncGetOffShuttle(pad); }, &argument,
        L"AsyncGetOffShuttle"
    );
}

void CWorld::__async_AsyncEnterWorld(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CCreature* creature = nullptr;
    FVector position{};
    unsigned int inZoneId = 0;
    bool activate = false;
    std::memcpy(&creature, argument->Buffer + 0x08, sizeof(creature));
    std::memcpy(&position, argument->Buffer + 0x10, sizeof(position));
    std::memcpy(&inZoneId, argument->Buffer + 0x28, sizeof(inZoneId));
    std::memcpy(&activate, argument->Buffer + 0x2c, sizeof(activate));
    AsyncEnterWorldInternal(creature, position, inZoneId, activate);
}

void CWorld::AsyncEnterWorldInternal(CCreature* creature, const FVector& position, unsigned int inZoneId, bool activate) {
    L2_GUARD_FUNCTION();

    ReleaseHelper<CCreature, ObjectRefType::SerializedWorldThread> releaseCreature(creature, "world_server.cpp", 0x3c6);

    const unsigned int previousInZoneId = creature->GetInZoneID();
    creature->SetInZoneID(inZoneId);
    const bool containerChanged = previousInZoneId != inZoneId;
    if (activate || containerChanged) {
        creature->PushSerializedMessage(new CSerializedTakeOffItems(creature, activate, containerChanged));
    }

    CContainer* container = nullptr;
    if (!creature->IsInRealWorld()) {
        CInstantZone* const instantZone = creature->GetInstantZone();
        if (instantZone != nullptr) {
            container = instantZone->GetContainer();
        }
    } else {
        container = CContainerDB::Inst().GetWorldContainer(
            WorldCoordinate(position.x), WorldCoordinate(position.y), WorldCoordinate(position.z)
        );
    }

    if (container == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "maplock NULL pos(%d, %d), at file[%s], line[%d]",
        //     WorldCoordinate(position.x),
        //     WorldCoordinate(position.y),
        //     "world_server.cpp",
        //     0x3e1)
        L2_LOG_ERROR(
            Log, "[world lifecycle] No container covers enter-world position (%d, %d) for creature %u in instance zone %u.",
            WorldCoordinate(position.x), WorldCoordinate(position.y), creature->Id(), inZoneId
        );
        return;
    }

    if (creature->IsPC() && creature->IsInRealWorld()) {
        User* const user = creature->CastUser();
        user->m_vUserMapDensityPosition = position;
        IncreaseUserMapDensity(position, *user);
    }

    creature->m_data->SetPos(position);
    container->PushCreature(creature);
    creature->m_data->m_bIsActive = true;
    if (!creature->IsPC() && activate) {
        creature->OnActivate();
    }
    if (creature->IsPC()) {
        creature->ResetPassingPos(position);
    }

    class ContainerFunc final : public CContainerFunc {
    public:
        explicit ContainerFunc(CCreature* actor) : actor_(actor) {}

        void Do(CObject* candidate) override {
            if (candidate == actor_) {
                return;
            }
            if (candidate->IsPC() && (candidate->IsDeletingObject() || candidate->m_Deleted != 0)) {
                return;
            }
            if (actor_->IsPC() && (actor_->IsDeletingObject() || actor_->m_Deleted != 0)) {
                return;
            }
            candidate->InSight(actor_, true);
            actor_->InSight(candidate, false);
        }

    private:
        CCreature* actor_;
    } function(creature);


    CContainerFilterNull filter;
    CContainerIndex neighbors = CContainerDB::Inst().GatherNeighborContainerIndex(container);
    creature->MakeEnterWorldPacket();
    FOR_ALL_OBJECT_IN_CONTAINER_INDEX(neighbors, filter, function);
    creature->ClearEnterWorldPacket();

    if (creature->IsInRealWorld()) {
        g_ptlCreatureMap->SetTlmBox3(
            creature->m_tlmBox3, WorldCoordinate(position.x), WorldCoordinate(position.y), WorldCoordinate(position.z)
        );
    }
    g_ptlCreatureMap->Push(creature->m_data->index, position, creature);

    FVector enteredPosition = position;
    creature->OnEnterWorld(enteredPosition);
    creature->m_vPrevLookPos = position;
    creature->PushSerializedMessage(new CSerializedLookNeighborMessage(creature, true, position));

    if (creature->IsNPC()) {
        runtime::raw_atomic::IncrementI32(&NpcSpawnInfo::Inst()->Server_EnterWorld);
        g_npcserver.Send("cddS", 2, static_cast<int>(creature->Id()), creature->GetIndex(), creature->GetName());
    }

    const std::uint32_t elapsed = runtime::GetTickCount() - creature->m_nBeforeEnterWorldTick;
    std::int32_t previous = g_ServerContext.MaxSerializerTime.load(std::memory_order_relaxed);
    while (elapsed > static_cast<std::uint32_t>(previous) &&
        !g_ServerContext.MaxSerializerTime.compare_exchange_weak(
            previous, static_cast<std::int32_t>(elapsed), std::memory_order_relaxed, std::memory_order_relaxed
        )) {
    }
    if (elapsed > static_cast<std::uint32_t>(previous)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR] [AsyncEnterWorld] MaxSerializerTime "
        //     L"Updated ! : now[%d], previous[%d]",
        //     elapsed,
        //     previous)
        // this records a new observed maximum, not a timeout.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR] [AsyncEnterWorld] MaxSerializerTime "
        //     L"Updated ! : now[%d], previous[%d]",
        //     elapsed,
        //     previous)
        L2_LOG_INFO(
            Log,
            "[AsyncEnterWorld] Maximum elapsed time from queue submission through world entry increased to %u ms (previous "
            "%u ms); object ID %u, class ID %d. This is a timing statistic, not a timeout.",
            static_cast<unsigned int>(elapsed), static_cast<unsigned int>(previous), creature->Id(), creature->m_nClassId
        );
    }
}

void CWorld::__async_AsyncMoveToZone(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CCreature* creature = nullptr;
    FVector destination{};
    unsigned int inZoneId = 0;
    std::memcpy(&creature, argument->Buffer + 0x08, sizeof(creature));
    std::memcpy(&destination, argument->Buffer + 0x10, sizeof(destination));
    std::memcpy(&inZoneId, argument->Buffer + 0x28, sizeof(inZoneId));

    ReleaseHelper<CCreature, ObjectRefType::SerializedWorldThread> releaseCreature(creature, "world_server.cpp", 0x547);

    if (!LeaveWorld(creature, destination, true)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "leave world failed at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0x54b)
        L2_LOG_ERROR(
            Log, "[world movement] Creature %u could not leave its current container while moving to instance zone %u.",
            creature->Id(), inZoneId
        );
        return;
    }

    L2_ADD_REF(creature, ObjectRefType::SerializedWorldThread);
    const FVector source = creature->GetPos();
    AsyncLeaveWorldInternal(creature, source, destination);

    if (!EnterWorld(creature, destination, inZoneId, false, true)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "enter world failed at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0x554)
        L2_LOG_ERROR(
            Log, "[world movement] Creature %u could not enter instance zone %u after leaving its previous container.",
            creature->Id(), inZoneId
        );
        return;
    }

    L2_ADD_REF(creature, ObjectRefType::SerializedWorldThread);
    AsyncEnterWorldInternal(creature, destination, inZoneId, false);
}

void CWorld::__async_AsyncLeaveWorld(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CCreature* creature;
    FVector source;
    FVector destination;
    std::memcpy(&creature, argument->Buffer + 0x08, sizeof(creature));
    std::memcpy(&source, argument->Buffer + 0x10, sizeof(source));
    std::memcpy(&destination, argument->Buffer + 0x28, sizeof(destination));
    AsyncLeaveWorldInternal(creature, source, destination);
}

void CWorld::AsyncLeaveWorldInternal(CCreature* creature, const FVector& source, const FVector& destination) {
    L2_GUARD_FUNCTION();

    ReleaseHelper<CCreature, ObjectRefType::SerializedWorldThread> releaseCreature(creature, "world_server.cpp", 0x4b4);

    bool wasInCarrier = false;
    const int sourceContainerIndex = creature->m_data->container_index;
    if (sourceContainerIndex > 0) {
        if (creature->IsInVehicle()) {
            wasInCarrier = true;
            CContainer* const carrierContainer = CContainerDB::Inst().GetContainer(sourceContainerIndex);
            CVehicle* const vehicle =
                carrierContainer == nullptr ? nullptr : g_VehicleDB.FindVehicle(carrierContainer->m_Data.m_nOwnerId);
            if (vehicle == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     "vehicle null at file[%s], line[%d]",
                //     "world_server.cpp",
                //     0x4c7)
                L2_LOG_ERROR(
                    Log, "[world lifecycle] Creature %u references vehicle container %d, but its vehicle is missing.",
                    creature->Id(), sourceContainerIndex
                );
            } else {
                GetOffVehicle(creature, vehicle, source, false);
                L2_RELEASE(vehicle, ObjectRefType::FindObject);
            }
        } else if (creature->IsInShuttle()) {
            wasInCarrier = true;
            CContainer* const carrierContainer = CContainerDB::Inst().GetContainer(sourceContainerIndex);
            CShuttle* const shuttle = carrierContainer == nullptr
                ? nullptr
                : CShuttle::s_ShuttleSmartIdManager.GetObjectW(
                      static_cast<std::uint32_t>(carrierContainer->m_Data.m_nOwnerId)
                  );
            if (shuttle == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     "shuttle null at file[%s], line[%d]",
                //     "world_server.cpp",
                //     0x4d4)
                L2_LOG_ERROR(
                    Log, "[world lifecycle] Creature %u references shuttle container %d, but its shuttle is missing.",
                    creature->Id(), sourceContainerIndex
                );
            } else {
                GetOffShuttle(creature, shuttle, source, false);
            }
        }
    }

    CContainer* container = CContainerDB::Inst().GetContainer(creature->m_data->container_index);
    if (container == nullptr) {
        creature->m_bIsRestarting = false;
        const FVector position = creature->GetPos();
        // Log.Add(
        //     LOG_ERROR,
        //     "container NULL index[%d] pos[%d, %d] "
        //     "at file [%s], line [%d]",
        //     creature->m_data->container_index,
        //     WorldCoordinate(position.x),
        //     WorldCoordinate(position.y),
        //     "world_server.cpp",
        //     0x4eb)
        L2_LOG_ERROR(
            Log, "[world lifecycle] Creature %u references missing container %d at (%d, %d); leave-world aborted.",
            creature->Id(), creature->m_data->container_index, WorldCoordinate(position.x), WorldCoordinate(position.y)
        );
        return;
    }

    if (!container->PopCreature(creature)) {
        // Log.Add(LOG_ERROR, L"Already poped creature")
        L2_LOG_ERROR(
            Log, u"[world lifecycle] Creature %u was absent from container %d during leave-world; operation aborted.",
            creature->Id(), creature->m_data->container_index
        );
        creature->m_bIsRestarting = false;
        return;
    }

    if (container->IsDynamic()) {
        container = CContainerDB::Inst().GetContainer(container->m_Data.m_nParentContainerIndex);
    }
    if (container == nullptr) {
        const FVector position = creature->GetPos();
        const wchar_t* const wasInCarrierText = wasInCarrier ? L"true" : L"false";
        // Log.Add(
        //     LOG_LOCAL,
        //     L"[%s][%d] container NULL index[%d] pos[%d, %d] "
        //     L"WasInVehicle[%s] WasInAirShip[%s]",
        //     L"world_server.cpp",
        //     0x502,
        //     creature->m_data->container_index,
        //     WorldCoordinate(position.x),
        //     WorldCoordinate(position.y),
        //     wasInCarrierText,
        //     L"false")
        L2_LOG(
            Log, LOG_LOCAL,
            u"[world lifecycle] Parent container is missing after removing creature %u from container %d at (%d, %d); "
            u"carrier state was %s.",
            creature->Id(), creature->m_data->container_index, WorldCoordinate(position.x), WorldCoordinate(position.y),
            wasInCarrierText
        );
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] container NULL index[%d] pos[%d, %d] "
        //     L"WasInVehicle[%s] WasInAirShip[%s]",
        //     L"world_server.cpp",
        //     0x506,
        //     creature->m_data->container_index,
        //     WorldCoordinate(position.x),
        //     WorldCoordinate(position.y),
        //     wasInCarrierText,
        //     L"false")
        L2_LOG_ERROR(
            Log,
            u"[world lifecycle] Parent container is missing after removing creature %u from container %d at (%d, %d); "
            u"carrier state was %s.",
            creature->Id(), creature->m_data->container_index, WorldCoordinate(position.x), WorldCoordinate(position.y),
            wasInCarrierText
        );
        return;
    }

    class ContainerFunc final : public CContainerFunc {
    public:
        explicit ContainerFunc(CCreature* actor) : actor_(actor) {}

        void Do(CObject* candidate) override {
            if (candidate == actor_) {
                return;
            }
            if (candidate->IsPC() && (candidate->IsDeletingObject() || candidate->m_Deleted != 0)) {
                return;
            }
            candidate->OutOfSight(actor_, true);
            actor_->OutOfSight(candidate, true);
        }

    private:
        CCreature* actor_;
    } function(creature);
    CContainerFilterNull filter;
    CContainerIndex neighbors = CContainerDB::Inst().GatherNeighborContainerIndex(container);
    FOR_ALL_OBJECT_IN_CONTAINER_INDEX(neighbors, filter, function);

    creature->m_data->m_bIsActive = false;
    // Current L2Server 0x00c61a56 calls CObject vslot +0xc8 here,
    // CObject::IsInRealWorld(). The recovered IsObject() call decremented
    // real-world density while leaving an instant zone even though instant-
    // zone entry never increments that density.
    if (creature->IsPC() && creature->IsInRealWorld()) {
        User* const user = creature->CastUser();
        DecreaseUserMapDensity(user->m_vUserMapDensityPosition, *user);
    }
    creature->m_data->SetPos(destination);
    if (creature->IsNPC()) {
        CInstantZone* const instantZone = creature->GetInstantZone();
        if (instantZone != nullptr) {
            instantZone->LeaveZone(creature->CastNPC());
        }
    }
}

void CWorld::__async_AsyncMove(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CCreature* actor;
    CContainer* destination;
    bool force;
    std::memcpy(&actor, argument->Buffer + 0x08, sizeof(actor));
    std::memcpy(&destination, argument->Buffer + 0x10, sizeof(destination));
    std::memcpy(&force, argument->Buffer + 0x18, sizeof(force));

    ReleaseHelper<CCreature, ObjectRefType::SerializedWorldThread> releaseActor(actor, "world_server.cpp", 0x563);

    CContainer* const source = CContainerDB::Inst().GetContainer(actor->m_data->container_index);
    if (source == destination) {
        return;
    }

    if (source == nullptr) {
        const FVector yPosition = actor->GetPos();
        const int y = runtime::Win64CvttDoubleToInt32(yPosition.y);
        const FVector xPosition = actor->GetPos();
        const int x = runtime::Win64CvttDoubleToInt32(xPosition.x);
        // Log.Add(
        //     LOG_ERROR,
        //     "container NULL index[%d] pos[%d, %d] "
        //     "at file [%s], line [%d]",
        //     actor->m_data->container_index,
        //     x,
        //     y,
        //     "world_server.cpp",
        //     0x56f)
        L2_LOG_ERROR(
            Log, "[world movement] Creature %u references missing source container %d at (%d, %d); async move aborted.",
            actor->Id(), actor->m_data->container_index, x, y
        );
        return;
    }

    if (!source->PopCreature(actor)) {
        const FVector position = actor->m_data->m_Pos;
        // Log.Add(
        //     LOG_ERROR,
        //     L"actor(%d,%d,%d) in AsyncMove, "
        //     L"failed to pop from GetContainer!!!",
        //     runtime::Win64CvttDoubleToInt32(position.x),
        //     runtime::Win64CvttDoubleToInt32(position.y),
        //     runtime::Win64CvttDoubleToInt32(position.z))
        L2_LOG_ERROR(
            Log,
            u"[world movement] Creature %u was absent from its source container during an async move at (%d, %d, %d); move "
            u"aborted.",
            actor->Id(), runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
            runtime::Win64CvttDoubleToInt32(position.z)
        );
        return;
    }

    CContainerDB::Inst().ContainerChanged(actor, source, destination, force);
    destination->PushCreature(actor);
}

void CWorld::__async_AsyncGetOnVehicle(CExecutionArgumentPad* argument) {
    // Current L2Server 0x00c5a730.
    L2_GUARD_FUNCTION();

    CCreature* creature;
    CVehicle* vehicle;
    FVector position;
    std::memcpy(&creature, argument->Buffer + 0x08, sizeof(creature));
    std::memcpy(&vehicle, argument->Buffer + 0x10, sizeof(vehicle));
    std::memcpy(&position, argument->Buffer + 0x18, sizeof(position));

    ReleaseHelper<CCreature, ObjectRefType::SerializedWorldThread> releaseCreature(creature, "world_server.cpp", 0x660);
    ReleaseHelper<CVehicle, ObjectRefType::SerializedWorldThread> releaseVehicle(vehicle, "world_server.cpp", 0x661);

    CContainer* source;
    CContainer* parent;
    CContainer* carrierContainer;
    {
        GuardHelper guard(L"GetOnVehicle_1", CThreadManager::GetThreadID());
        source = CContainerDB::Inst().GetContainer(creature->m_data->container_index);
        if (source == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] GetOnVehicle Message : "
            //     L"creature container is NULL (%d)!!!",
            //     L"world_server.cpp",
            //     0x66e,
            //     creature->m_data->container_index)
            L2_LOG_ERROR(
                Log, u"[vehicle] Creature %u references missing source container %d; board operation aborted.",
                creature->Id(), creature->m_data->container_index
            );
            return;
        }

        parent = CContainerDB::Inst().GetContainer(vehicle->m_pContainer->m_Data.m_nParentContainerIndex);
        carrierContainer = vehicle->m_pContainer;
        if (parent == nullptr || carrierContainer == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] GetOnVehicle Message : "
            //     L"vehicle container is NULL!!!",
            //     L"world_server.cpp",
            //     0x678)
            L2_LOG_ERROR(
                Log,
                u"[vehicle] Vehicle %u has no usable carrier or parent container; board operation for creature %u aborted.",
                vehicle->Id(), creature->Id()
            );
            return;
        }
    }

    if (carrierContainer == source) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] GetOnVehicle Message : "
        //     L"already contained in vehicle!!!",
        //     L"world_server.cpp",
        //     0x67f)
        L2_LOG_ERROR(
            Log, u"[vehicle] Creature %u is already in vehicle %u; duplicate board operation ignored.", creature->Id(),
            vehicle->Id()
        );
        return;
    }

    bool popped;
    {
        GuardHelper guard(L"GetOnVehicle_3", CThreadManager::GetThreadID());
        popped = source->PopCreature(creature);
        if (!popped) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] PopCreature Failed!!!",
            //     L"world_server.cpp",
            //     0x68e)
            L2_LOG_ERROR(
                Log, u"[vehicle] Creature %u was absent from source container %d during board operation for vehicle %u.",
                creature->Id(), creature->m_data->container_index, vehicle->Id()
            );
        } else {
            carrierContainer->PushCreature(creature);
            creature->SetRelPos(position);
        }
    }

    {
        GuardHelper guard(L"GetOnVehicle_4", CThreadManager::GetThreadID());
        if (source != parent && popped) {
            CContainerDB::Inst().ContainerChanged(creature, source, parent, false);
        }
    }

    (new CGetOnVehicleIOCPMessage(creature, static_cast<int>(vehicle->m_nId), position))->Post(0);
}

void CWorld::__async_AsyncGetOffVehicle(CExecutionArgumentPad* argument) {
    // Current L2Server 0x00c5ac28.
    L2_GUARD_FUNCTION();

    CCreature* creature;
    CVehicle* vehicle;
    FVector position;
    std::memcpy(&creature, argument->Buffer + 0x08, sizeof(creature));
    std::memcpy(&vehicle, argument->Buffer + 0x10, sizeof(vehicle));
    std::memcpy(&position, argument->Buffer + 0x18, sizeof(position));

    ReleaseHelper<CCreature, ObjectRefType::SerializedWorldThread> releaseCreature(creature, "world_server.cpp", 0x6a3);
    ReleaseHelper<CVehicle, ObjectRefType::SerializedWorldThread> releaseVehicle(vehicle, "world_server.cpp", 0x6a4);

    if (!IsValidPosition(WorldCoordinate(position.x), WorldCoordinate(position.y))) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"error in CWorld::GetOffVehicle()")
        L2_LOG_ERROR(
            Log, u"[vehicle] Disembark destination (%d, %d) is invalid for creature %u; operation aborted.",
            WorldCoordinate(position.x), WorldCoordinate(position.y), creature->Id()
        );
        return;
    }

    CContainer* destination;
    CContainer* carrierContainer;
    CContainer* parent;
    {
        GuardHelper guard(L"getoffvehicle_1", CThreadManager::GetThreadID());
        destination = CContainerDB::Inst().GetWorldContainer(
            WorldCoordinate(position.x), WorldCoordinate(position.y), WorldCoordinate(position.z)
        );
        if (destination == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] GetOffVehicle Message : "
            //     L"pCreatureWorldContainer = NULL",
            //     L"world_server.cpp",
            //     0x6b6)
            L2_LOG_ERROR(
                Log, u"[vehicle] No world container covers disembark destination (%d, %d, %d) for creature %u.",
                WorldCoordinate(position.x), WorldCoordinate(position.y), WorldCoordinate(position.z), creature->Id()
            );
            return;
        }

        parent = CContainerDB::Inst().GetContainer(vehicle->m_pContainer->m_Data.m_nParentContainerIndex);
        carrierContainer = vehicle->m_pContainer;
        if (parent == nullptr || carrierContainer == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] GetOffVehicle Message : "
            //     L"Vehicle Container is invalid",
            //     L"world_server.cpp",
            //     0x6bf)
            L2_LOG_ERROR(
                Log,
                u"[vehicle] Vehicle %u has no usable carrier or parent container; disembark operation for creature %u "
                u"aborted.",
                vehicle->Id(), creature->Id()
            );
            return;
        }
    }

    {
        GuardHelper guard(L"getoffvehicle_3", CThreadManager::GetThreadID());
        if (!carrierContainer->PopCreature(creature)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] PopCreature Failed",
            //     L"world_server.cpp",
            //     0x6cf)
            L2_LOG_ERROR(
                Log, u"[vehicle] Creature %u was absent from vehicle %u during disembark; operation aborted.",
                creature->Id(), vehicle->Id()
            );
            return;
        }

        creature->m_data->SetPos(position);
        destination->PushCreature(creature);
    }

    {
        GuardHelper guard(L"getoffvehicle_4", CThreadManager::GetThreadID());
        if (destination != parent) {
            CContainerDB::Inst().ContainerChanged(creature, parent, destination, false);
        }
    }

    auto* const message = new CGetOffVehicleIOCPMessage(creature, vehicle->m_nId, position);
    if (message != nullptr) {
        message->Post(0);
    }
}

void CWorld::__async_AsyncGetOnShuttle(CExecutionArgumentPad* argument) {
    // Current L2Server 0x00c5ca20.
    L2_GUARD_FUNCTION();

    CCreature* creature;
    CShuttle* shuttle;
    FVector position;
    std::memcpy(&creature, argument->Buffer + 0x08, sizeof(creature));
    std::memcpy(&shuttle, argument->Buffer + 0x10, sizeof(shuttle));
    std::memcpy(&position, argument->Buffer + 0x18, sizeof(position));

    ReleaseHelper<CCreature, ObjectRefType::SerializedWorldThread> releaseCreature(creature, "world_server.cpp", 0x1441);
    ReleaseHelper<CShuttle, ObjectRefType::SerializedWorldThread> releaseShuttle(shuttle, "world_server.cpp", 0x1442);

    CContainer* source;
    CContainer* parent;
    CContainer* carrierContainer;
    {
        GuardHelper guard(L"GetOnShuttle_1", CThreadManager::GetThreadID());
        source = CContainerDB::Inst().GetContainer(creature->m_data->container_index);
        if (source == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] GetOnShuttle Message : "
            //     L"creature container is NULL (%d)!!!",
            //     L"world_server.cpp",
            //     0x144f,
            //     creature->m_data->container_index)
            L2_LOG_ERROR(
                Log, u"[shuttle] Creature %u references missing source container %d; board operation aborted.",
                creature->Id(), creature->m_data->container_index
            );
            return;
        }

        parent = CContainerDB::Inst().GetContainer(shuttle->m_pContainer->m_Data.m_nParentContainerIndex);
        carrierContainer = shuttle->m_pContainer;
        if (parent == nullptr || carrierContainer == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] GetOnShuttle Message : "
            //     L"Shuttle container is NULL!!!",
            //     L"world_server.cpp",
            //     0x1459)
            L2_LOG_ERROR(
                Log,
                u"[shuttle] Shuttle %u has no usable carrier or parent container; board operation for creature %u aborted.",
                shuttle->Id(), creature->Id()
            );
            return;
        }
    }

    if (carrierContainer == source) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] GetOnShuttle Message : "
        //     L"already contained in Shuttle!!!",
        //     L"world_server.cpp",
        //     0x1460)
        L2_LOG_ERROR(
            Log, u"[shuttle] Creature %u is already in shuttle %u; duplicate board operation ignored.", creature->Id(),
            shuttle->Id()
        );
        return;
    }

    bool popped;
    {
        GuardHelper guard(L"GetOnShuttle_3", CThreadManager::GetThreadID());
        popped = source->PopCreature(creature);
        if (!popped) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] PopCreature Failed!!!",
            //     L"world_server.cpp",
            //     0x146f)
            L2_LOG_ERROR(
                Log, u"[shuttle] Creature %u was absent from source container %d during board operation for shuttle %u.",
                creature->Id(), creature->m_data->container_index, shuttle->Id()
            );
        } else {
            carrierContainer->PushCreature(creature);
            creature->SetRelPos(position);
        }
    }

    {
        GuardHelper guard(L"GetOnShuttle_4", CThreadManager::GetThreadID());
        if (source != parent && popped) {
            CContainerDB::Inst().ContainerChanged(creature, source, parent, false);
        }
    }

    (new CGetOnShuttleIOCPMessage(creature, shuttle->m_nObjectID, position))->Post(0);
}

void CWorld::__async_AsyncGetOffShuttle(CExecutionArgumentPad* argument) {
    // Current L2Server 0x00c5cf1c.
    L2_GUARD_FUNCTION();

    CCreature* creature;
    CShuttle* shuttle;
    FVector position;
    std::memcpy(&creature, argument->Buffer + 0x08, sizeof(creature));
    std::memcpy(&shuttle, argument->Buffer + 0x10, sizeof(shuttle));
    std::memcpy(&position, argument->Buffer + 0x18, sizeof(position));

    ReleaseHelper<CCreature, ObjectRefType::SerializedWorldThread> releaseCreature(creature, "world_server.cpp", 0x1484);
    ReleaseHelper<CShuttle, ObjectRefType::SerializedWorldThread> releaseShuttle(shuttle, "world_server.cpp", 0x1485);

    if (!IsValidPosition(WorldCoordinate(position.x), WorldCoordinate(position.y))) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"error in CWorld::GetOffShuttle()")
        L2_LOG_ERROR(
            Log, u"[shuttle] Disembark destination (%d, %d) is invalid for creature %u; operation aborted.",
            WorldCoordinate(position.x), WorldCoordinate(position.y), creature->Id()
        );
        return;
    }

    CContainer* destination;
    CContainer* carrierContainer;
    CContainer* parent;
    {
        GuardHelper guard(L"getoffshuttle_1", CThreadManager::GetThreadID());
        destination = CContainerDB::Inst().GetWorldContainer(
            WorldCoordinate(position.x), WorldCoordinate(position.y), WorldCoordinate(position.z)
        );
        if (destination == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] GetOffShuttle Message : "
            //     L"pCreatureWorldContainer = NULL",
            //     L"world_server.cpp",
            //     0x1497)
            L2_LOG_ERROR(
                Log, u"[shuttle] No world container covers disembark destination (%d, %d, %d) for creature %u.",
                WorldCoordinate(position.x), WorldCoordinate(position.y), WorldCoordinate(position.z), creature->Id()
            );
            return;
        }

        parent = CContainerDB::Inst().GetContainer(shuttle->m_pContainer->m_Data.m_nParentContainerIndex);
        carrierContainer = shuttle->m_pContainer;
        if (parent == nullptr || carrierContainer == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] GetOffShuttle Message : "
            //     L"Shuttle Container is invalid",
            //     L"world_server.cpp",
            //     0x14a0)
            L2_LOG_ERROR(
                Log,
                u"[shuttle] Shuttle %u has no usable carrier or parent container; disembark operation for creature %u "
                u"aborted.",
                shuttle->Id(), creature->Id()
            );
            return;
        }
    }

    {
        GuardHelper guard(L"getoffshuttle_3", CThreadManager::GetThreadID());
        if (!carrierContainer->PopCreature(creature)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] AsyncGetOffShuttle:: PopCreature Failed",
            //     L"world_server.cpp",
            //     0x14b1)
            L2_LOG_ERROR(
                Log, u"[shuttle] Creature %u was absent from shuttle %u during disembark; operation aborted.",
                creature->Id(), shuttle->Id()
            );
            return;
        }

        creature->m_data->SetPos(position);
        destination->PushCreature(creature);
    }

    {
        GuardHelper guard(L"getoffshuttle_4", CThreadManager::GetThreadID());
        if (destination != parent) {
            CContainerDB::Inst().ContainerChanged(creature, parent, destination, false);
        }
    }

    auto* const message =
        new CGetOffShuttleIOCPMessage(creature, static_cast<std::uint32_t>(shuttle->m_nObjectID), position);
    if (message != nullptr) {
        message->Post(0);
    }
}

void CWorld::__async_AsyncVehicleEnterWorld(CExecutionArgumentPad* argument) {
    // Current L2Server 0x00c5b56c.
    L2_GUARD_FUNCTION();

    CVehicle* vehicle;
    FVector position;
    std::memcpy(&vehicle, argument->Buffer + 0x08, sizeof(vehicle));
    std::memcpy(&position, argument->Buffer + 0x10, sizeof(position));

    ReleaseHelper<CVehicle, ObjectRefType::SerializedWorldThread> releaseVehicle(vehicle, "world_server.cpp", 0x957);
    if (vehicle == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "pVehicle null at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0x95b)
        L2_LOG_ERROR(Log, "[vehicle] Async enter-world operation has no vehicle; operation aborted.");
        return;
    }
    if (!IsValidPosition(WorldCoordinate(position.x), WorldCoordinate(position.y))) {
        // Log.Add(
        //     LOG_ERROR,
        //     "invalid position (%d, %d) at file[%s], line[%d]",
        //     WorldCoordinate(position.x),
        //     WorldCoordinate(position.y),
        //     "world_server.cpp",
        //     0x962)
        L2_LOG_ERROR(
            Log, "[vehicle] Enter-world position (%d, %d) is invalid for vehicle %u; operation aborted.",
            WorldCoordinate(position.x), WorldCoordinate(position.y), vehicle->Id()
        );
        return;
    }

    CContainer* const container = CContainerDB::Inst().GetWorldContainer(
        WorldCoordinate(position.x), WorldCoordinate(position.y), WorldCoordinate(position.z)
    );
    if (container == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "maplock NULL pos(%d, %d), at file[%s], line[%d]",
        //     WorldCoordinate(position.x),
        //     WorldCoordinate(position.y),
        //     "world_server.cpp",
        //     0x96a)
        L2_LOG_ERROR(
            Log, "[vehicle] No world container covers enter-world position (%d, %d) for vehicle %u.",
            WorldCoordinate(position.x), WorldCoordinate(position.y), vehicle->Id()
        );
        return;
    }

    vehicle->SetPos(position);
    container->PushVehicle(vehicle);

    class ContainerFunc final : public CContainerFunc {
    public:
        explicit ContainerFunc(CVehicle* vehicle) : vehicle_(vehicle) {}

        void Do(CObject* candidate) override {
            if (candidate == vehicle_ || (candidate->IsPC() && (candidate->IsDeleting() || candidate->m_Deleted != 0))) {
                return;
            }
            candidate->InSight(vehicle_, false);
            vehicle_->InSight(candidate, false);
        }

    private:
        CVehicle* vehicle_;
    } function(vehicle);


    CContainerIndex neighbors = CContainerDB::Inst().GatherNeighborContainerIndex(container);
    CContainerFilterNull filter;
    FOR_ALL_OBJECT_IN_CONTAINER_INDEX(neighbors, filter, function);
}

void CWorld::__async_AsyncVehicleLeaveWorld(CExecutionArgumentPad* argument) {
    // Current L2Server 0x00c5b814.
    L2_GUARD_FUNCTION();

    CVehicle* vehicle;
    std::memcpy(&vehicle, argument->Buffer + 0x08, sizeof(vehicle));
    ReleaseHelper<CVehicle, ObjectRefType::SerializedWorldThread> releaseVehicle(vehicle, "world_server.cpp", 0x993);
    if (vehicle == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "pVehicle null at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0x998)
        L2_LOG_ERROR(Log, "[vehicle] Async leave-world operation has no vehicle; operation aborted.");
        return;
    }

    CContainer* const container = CContainerDB::Inst().GetContainer(vehicle->m_pContainer->m_Data.m_nParentContainerIndex);
    if (container == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "container NULL at file [%s], line [%d]",
        //     "world_server.cpp",
        //     0x99f)
        L2_LOG_ERROR(
            Log, "[vehicle] Vehicle %u references a missing parent world container; leave-world operation aborted.",
            vehicle->Id()
        );
        return;
    }

    class ContainerFilter final : public CContainerFilter {
    public:
        explicit ContainerFilter(CVehicle* vehicle) : vehicle_(vehicle) {}

        bool CanDo(CObject* candidate) override { return candidate != vehicle_; }

    private:
        CVehicle* vehicle_;
    } filter(vehicle);


    class ContainerFunc final : public CContainerFunc {
    public:
        explicit ContainerFunc(CVehicle* vehicle) : vehicle_(vehicle) {}

        void Do(CObject* candidate) override {
            if (candidate->IsPC() && (candidate->IsDeleting() || candidate->m_Deleted != 0)) {
                return;
            }
            candidate->OutOfSight(vehicle_, true);
            vehicle_->OutOfSight(candidate, true);
        }

    private:
        CVehicle* vehicle_;
    } function(vehicle);


    CContainerIndex neighbors = CContainerDB::Inst().GatherNeighborContainerIndex(container);
    FOR_ALL_OBJECT_IN_CONTAINER_INDEX__INSTANT(neighbors, filter, function);
    container->PopVehicle(vehicle);
}

void CWorld::__async_AsyncVehicleMove(CExecutionArgumentPad* argument) {
    // Current L2Server 0x00c630e4.
    L2_GUARD_FUNCTION();

    CVehicle* vehicle;
    FVector position;
    std::memcpy(&vehicle, argument->Buffer + 0x08, sizeof(vehicle));
    std::memcpy(&position, argument->Buffer + 0x10, sizeof(position));
    if (vehicle == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] vehicle null",
        //     L"world_server.cpp",
        //     0x9d1)
        L2_LOG_ERROR(Log, u"[vehicle] Async move operation has no vehicle; operation aborted.");
        return;
    }

    ReleaseHelper<CVehicle, ObjectRefType::SerializedWorldThread> releaseVehicle(vehicle, "world_server.cpp", 0x9d6);
    const FVector current = vehicle->GetPos();
    const FVector delta{position.x - current.x, position.y - current.y, position.z - current.z};
    if (!IsValidPosition(WorldCoordinate(position.x), WorldCoordinate(position.y))) {
        const std::wstring vehicleName = target_utf16::ToHostCodeUnits(vehicle->m_Name);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] trying to move vehicle[%s] "
        //     L"to invalid position (%d, %d)",
        //     L"world_server.cpp",
        //     0x9e8,
        //     vehicleName.c_str(),
        //     WorldCoordinate(position.x),
        //     WorldCoordinate(position.y))
        L2_LOG_ERROR(
            Log, u"[vehicle] Destination (%d, %d) is invalid for vehicle '%s'; move rejected.", WorldCoordinate(position.x),
            WorldCoordinate(position.y), vehicleName.c_str()
        );
        return;
    }

    CContainer* const source = CContainerDB::Inst().GetContainer(vehicle->m_pContainer->m_Data.m_nParentContainerIndex);
    CContainer* const destination = CContainerDB::Inst().GetWorldContainer(
        WorldCoordinate(position.x), WorldCoordinate(position.y), WorldCoordinate(position.z)
    );
    if (source == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"pContFrom null index at file[%s], line[%d]",
        //     L"world_server.cpp",
        //     0x9f4)
        L2_LOG_ERROR(Log, u"[vehicle] Vehicle %u has no source world container; move rejected.", vehicle->Id());
        return;
    }
    if (destination == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"pContTo null pos(%d, %d) at file[%s], line[%d]",
        //     WorldCoordinate(position.x),
        //     WorldCoordinate(position.y),
        //     L"world_server.cpp",
        //     0x9fb)
        L2_LOG_ERROR(
            Log, u"[vehicle] No destination container covers (%d, %d); move request for vehicle %u rejected.",
            WorldCoordinate(position.x), WorldCoordinate(position.y), vehicle->Id()
        );
        return;
    }

    vehicle->SetPos(position);
    if (source != destination && source->PopVehicle(vehicle)) {
        destination->PushVehicle(vehicle);
        CContainerDB::Inst().ContainerChanged(vehicle, source, destination, false);
    }

    CCreatureFactory& factory = CCreatureFactory::Inst();
    int index = vehicle->m_pContainer->m_Data.m_nCreatureFirst;
    while (index != -1) {
        const std::uint32_t smartId = static_cast<std::uint32_t>(factory.GetIdByIndex(index));
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(smartId);
        if (creature == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"invalid creature at CVehicle::TimerExpired()")
            L2_LOG_ERROR(
                Log,
                u"[vehicle] Passenger runtime ID %u cannot be resolved while moving vehicle %u; passenger scan stopped.",
                smartId, vehicle->Id()
            );
            break;
        }

        L2_ADD_REF(creature, ObjectRefType::FindObject);
        if (!creature->m_data->m_bIsActive) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"inactive creature in vehicle!")
            L2_LOG_ERROR(
                Log, u"[vehicle] Passenger %u is inactive while attached to vehicle %u.", creature->Id(), vehicle->Id()
            );
        } else {
            const FVector creaturePosition = creature->GetPos();
            const FVector adjusted{creaturePosition.x + delta.x, creaturePosition.y + delta.y, creaturePosition.z + delta.z};
            if (!IsValidPosition(WorldCoordinate(adjusted.x), WorldCoordinate(adjusted.y))) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"AdjustPos Error NowPos(%d,%d,%d), "
                //     L"AdjustPos(%d, %d, %d), Delta(%d, %d, %d)",
                //     WorldCoordinate(creaturePosition.x),
                //     WorldCoordinate(creaturePosition.y),
                //     WorldCoordinate(creaturePosition.z),
                //     WorldCoordinate(adjusted.x),
                //     WorldCoordinate(adjusted.y),
                //     WorldCoordinate(adjusted.z),
                //     WorldCoordinate(delta.x),
                //     WorldCoordinate(delta.y),
                //     WorldCoordinate(delta.z))
                L2_LOG_ERROR(
                    Log,
                    u"[vehicle] Moving passenger %u with vehicle %u would produce invalid position (%d, %d, %d) from (%d, "
                    u"%d, %d), delta (%d, %d, %d).",
                    creature->Id(), vehicle->Id(), WorldCoordinate(adjusted.x), WorldCoordinate(adjusted.y),
                    WorldCoordinate(adjusted.z), WorldCoordinate(creaturePosition.x), WorldCoordinate(creaturePosition.y),
                    WorldCoordinate(creaturePosition.z), WorldCoordinate(delta.x), WorldCoordinate(delta.y),
                    WorldCoordinate(delta.z)
                );
            } else if (vehicle->m_pContainer->m_Data.m_nIndex == creature->m_data->container_index) {
                MoveTo(creature, adjusted, false);
            }
        }

        const int nextIndex = factory.GetNextIndex(index);
        L2_RELEASE(creature, ObjectRefType::FindObject);
        index = nextIndex;
    }
}

void CWorld::__async_AsyncVehicleKickIllegal(CExecutionArgumentPad* argument) {
    // Current L2Server 0x00c69ad0.
    L2_GUARD_FUNCTION();

    CVehicle* vehicle;
    int ticketClassId;
    FVector banPosition;
    std::memcpy(&vehicle, argument->Buffer + 0x08, sizeof(vehicle));
    std::memcpy(&ticketClassId, argument->Buffer + 0x10, sizeof(ticketClassId));
    std::memcpy(&banPosition, argument->Buffer + 0x18, sizeof(banPosition));

    ReleaseHelper<CVehicle, ObjectRefType::SerializedWorldThread> releaseVehicle(vehicle, "world_server.cpp", 0x8b4);
    std::vector<CCreature*> usersToKick;
    CCreatureFactory& factory = CCreatureFactory::Inst();
    int index = vehicle->m_pContainer->m_Data.m_nCreatureFirst;
    while (index != -1) {
        const std::uint32_t smartId = static_cast<std::uint32_t>(factory.GetIdByIndex(index));
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(smartId);
        if (creature == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"invalid creature at CVehicle::TimerExpired()")
            L2_LOG_ERROR(
                Log,
                u"[vehicle] Passenger runtime ID %u cannot be resolved while checking tickets on vehicle %u; passenger scan "
                u"stopped.",
                smartId, vehicle->Id()
            );
            break;
        }

        L2_ADD_REF(creature, ObjectRefType::FindObject);
        if (!creature->m_data->m_bIsActive) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"inactive creature in vehicle!")
            L2_LOG_ERROR(
                Log, u"[vehicle] Passenger %u is inactive while attached to vehicle %u during ticket validation.",
                creature->Id(), vehicle->Id()
            );
        } else if (creature->IsPC()) {
            User* const user = static_cast<User*>(creature);
            const FVector userPosition = user->GetPos();
            const std::wstring accountName = target_utf16::ToHostCodeUnits(user->m_sAccountName.data());
            if (!user->DeleteItemInInventoryBeforeCommit(ticketClassId, 1)) {
                usersToKick.push_back(creature);
                // Log.Add(
                //     LOG_BOTH,
                //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                //     L"%I64d,%I64d,%I64d,%I64d,,,,%I64d,,,%s,%s,,,,",
                //     static_cast<std::int64_t>(0x3a9),
                //     static_cast<std::int64_t>(user->GetDBID()),
                //     static_cast<std::int64_t>(user->GetAccountID()),
                //     static_cast<std::int64_t>(
                //         WorldCoordinate(userPosition.x)),
                //     static_cast<std::int64_t>(
                //         WorldCoordinate(userPosition.y)),
                //     static_cast<std::int64_t>(
                //         WorldCoordinate(userPosition.z)),
                //     static_cast<std::int64_t>(user->m_data->m_Race),
                //     static_cast<std::int64_t>(user->m_data->m_Sex),
                //     static_cast<std::int64_t>(user->m_data->m_nClass),
                //     static_cast<std::int64_t>(user->m_data->m_nLevel),
                //     static_cast<std::int64_t>(ticketClassId),
                //     user->GetName(),
                //     accountName.c_str())
                Log.Add(
                    LOG_BOTH,
                    u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                    u"%I64d,%I64d,%I64d,%I64d,,,,%I64d,,,%s,%s,,,,",
                    static_cast<std::int64_t>(0x3a9), static_cast<std::int64_t>(user->GetDBID()),
                    static_cast<std::int64_t>(user->GetAccountID()),
                    static_cast<std::int64_t>(WorldCoordinate(userPosition.x)),
                    static_cast<std::int64_t>(WorldCoordinate(userPosition.y)),
                    static_cast<std::int64_t>(WorldCoordinate(userPosition.z)),
                    static_cast<std::int64_t>(user->m_data->m_Race), static_cast<std::int64_t>(user->m_data->m_Sex),
                    static_cast<std::int64_t>(user->m_data->m_nClass), static_cast<std::int64_t>(user->m_data->m_nLevel),
                    static_cast<std::int64_t>(ticketClassId), user->GetName(), accountName.c_str()
                );
            } else {
                // Log.Add(
                //     LOG_BOTH,
                //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                //     L"%I64d,%I64d,%I64d,%I64d,,,,%I64d,%I64d,,"
                //     L"%s,%s,,,,",
                //     static_cast<std::int64_t>(0x3a8),
                //     static_cast<std::int64_t>(user->GetDBID()),
                //     static_cast<std::int64_t>(user->GetAccountID()),
                //     static_cast<std::int64_t>(
                //         WorldCoordinate(userPosition.x)),
                //     static_cast<std::int64_t>(
                //         WorldCoordinate(userPosition.y)),
                //     static_cast<std::int64_t>(
                //         WorldCoordinate(userPosition.z)),
                //     static_cast<std::int64_t>(user->m_data->m_Race),
                //     static_cast<std::int64_t>(user->m_data->m_Sex),
                //     static_cast<std::int64_t>(user->m_data->m_nClass),
                //     static_cast<std::int64_t>(user->m_data->m_nLevel),
                //     static_cast<std::int64_t>(ticketClassId),
                //     static_cast<std::int64_t>(1),
                //     user->GetName(),
                //     accountName.c_str())
                Log.Add(
                    LOG_BOTH,
                    u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                    u"%I64d,%I64d,%I64d,%I64d,,,,%I64d,%I64d,,"
                    u"%s,%s,,,,",
                    static_cast<std::int64_t>(0x3a8), static_cast<std::int64_t>(user->GetDBID()),
                    static_cast<std::int64_t>(user->GetAccountID()),
                    static_cast<std::int64_t>(WorldCoordinate(userPosition.x)),
                    static_cast<std::int64_t>(WorldCoordinate(userPosition.y)),
                    static_cast<std::int64_t>(WorldCoordinate(userPosition.z)),
                    static_cast<std::int64_t>(user->m_data->m_Race), static_cast<std::int64_t>(user->m_data->m_Sex),
                    static_cast<std::int64_t>(user->m_data->m_nClass), static_cast<std::int64_t>(user->m_data->m_nLevel),
                    static_cast<std::int64_t>(ticketClassId), static_cast<std::int64_t>(1), user->GetName(),
                    accountName.c_str()
                );
            }
        }

        const int nextIndex = factory.GetNextIndex(index);
        L2_RELEASE(creature, ObjectRefType::FindObject);
        index = nextIndex;
    }

    for (CCreature* const creature : usersToKick) {
        if (creature->IsPC()) {
            creature->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_BOARD_WITHOUT_BOAT_TICKET);
        }
        creature->TeleportToLocation(
            WorldCoordinate(banPosition.x), WorldCoordinate(banPosition.y), WorldCoordinate(banPosition.z), 0, 0x10000,
            TeleportSourceType::AsyncVehicleKickIllegal, nullptr, 0
        );
    }
}

void CWorld::__async_AsyncVehicleInformVehicleStart(CExecutionArgumentPad* argument) {
    // Current L2Server 0x00c69ffc.
    L2_GUARD_FUNCTION();

    CVehicle* vehicle;
    bool start;
    std::memcpy(&vehicle, argument->Buffer + 0x08, sizeof(vehicle));
    std::memcpy(&start, argument->Buffer + 0x10, sizeof(start));

    ReleaseHelper<CVehicle, ObjectRefType::SerializedWorldThread> releaseVehicle(vehicle, "world_server.cpp", 0x8fb);
    std::vector<CCreature*> users;
    CCreatureFactory& factory = CCreatureFactory::Inst();
    int index = vehicle->m_pContainer->m_Data.m_nCreatureFirst;
    while (index != -1) {
        const std::uint32_t smartId = static_cast<std::uint32_t>(factory.GetIdByIndex(index));
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(smartId);
        if (creature == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"invalid creature at CVehicle::InformVehicleStart()")
            L2_LOG_ERROR(
                Log,
                u"[vehicle] Passenger runtime ID %u cannot be resolved while publishing start state for vehicle %u; "
                u"passenger scan stopped.",
                smartId, vehicle->Id()
            );
            break;
        }

        L2_ADD_REF(creature, ObjectRefType::FindObject);
        if (!creature->m_data->m_bIsActive) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"inactive creature in vehicle!")
            L2_LOG_ERROR(
                Log, u"[vehicle] Passenger %u is inactive while attached to vehicle %u during start-state publication.",
                creature->Id(), vehicle->Id()
            );
        } else if (creature->IsPC()) {
            users.push_back(creature);
        }

        const int nextIndex = factory.GetNextIndex(index);
        L2_RELEASE(creature, ObjectRefType::FindObject);
        index = nextIndex;
    }

    for (CCreature* const creature : users) {
        if (creature->IsPC()) {
            creature->CastUser()->GetSocket()->Send("cdd", 0xc0, static_cast<int>(vehicle->Id()), start ? 1 : 0);
        }
    }
}

void CWorld::__async_AsyncVehicleKickTransformer(CExecutionArgumentPad* argument) {
    // Current L2Server 0x00c6a308.
    L2_GUARD_FUNCTION();

    CVehicle* vehicle;
    std::memcpy(&vehicle, argument->Buffer + 0x08, sizeof(vehicle));

    ReleaseHelper<CVehicle, ObjectRefType::SerializedWorldThread> releaseVehicle(vehicle, "world_server.cpp", 0x928);
    std::vector<User*> usersToKick;
    CCreatureFactory& factory = CCreatureFactory::Inst();
    int index = vehicle->m_pContainer->m_Data.m_nCreatureFirst;
    while (index != -1) {
        const std::uint32_t smartId = static_cast<std::uint32_t>(factory.GetIdByIndex(index));
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(smartId);
        if (creature == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"invalid creature at CVehicle::TimerExpired()")
            L2_LOG_ERROR(
                Log,
                u"[vehicle] Passenger runtime ID %u cannot be resolved while checking transformations on vehicle %u; "
                u"passenger scan stopped.",
                smartId, vehicle->Id()
            );
            break;
        }

        L2_ADD_REF(creature, ObjectRefType::FindObject);
        if (!creature->m_data->m_bIsActive) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"inactive creature in vehicle!")
            L2_LOG_ERROR(
                Log, u"[vehicle] Passenger %u is inactive while attached to vehicle %u during transformation validation.",
                creature->Id(), vehicle->Id()
            );
        } else if (creature->IsPC()) {
            if (!creature->CastUser()->CanGetOnVehicle()) {
                usersToKick.push_back(creature->CastUser());
            }
        }

        const int nextIndex = factory.GetNextIndex(index);
        L2_RELEASE(creature, ObjectRefType::FindObject);
        index = nextIndex;
    }

    for (User* const user : usersToKick) {
        user->TeleportToLocation(
            WorldCoordinate(vehicle->m_vKickPos.x), WorldCoordinate(vehicle->m_vKickPos.y),
            WorldCoordinate(vehicle->m_vKickPos.z), 0, 0x10000, TeleportSourceType::AsyncVehicleKickTransformer, nullptr, 0
        );
    }
}

void CWorld::__async_AsyncShuttleEnterWorld(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CShuttle* shuttle;
    FVector position;
    std::memcpy(&shuttle, argument->Buffer + 0x08, sizeof(shuttle));
    std::memcpy(&position, argument->Buffer + 0x10, sizeof(position));

    ReleaseHelper<CShuttle, ObjectRefType::SerializedWorldThread> releaseShuttle(shuttle, "world_server.cpp", 0x14ca);
    if (shuttle == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "pShuttle null at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0x14ce)
        L2_LOG_ERROR(Log, "[shuttle] Async enter-world operation has no shuttle; operation aborted.");
        return;
    }
    if (!IsValidPosition(WorldCoordinate(position.x), WorldCoordinate(position.y))) {
        // Log.Add(
        //     LOG_ERROR,
        //     "invalid position (%d, %d) at file[%s], line[%d]",
        //     WorldCoordinate(position.x),
        //     WorldCoordinate(position.y),
        //     "world_server.cpp",
        //     0x14d5)
        L2_LOG_ERROR(
            Log, "[shuttle] Enter-world position (%d, %d) is invalid for shuttle %u; operation aborted.",
            WorldCoordinate(position.x), WorldCoordinate(position.y), shuttle->Id()
        );
        return;
    }

    CContainer* const container = CContainerDB::Inst().GetWorldContainer(
        WorldCoordinate(position.x), WorldCoordinate(position.y), WorldCoordinate(position.z)
    );
    if (container == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "maplock NULL pos(%d, %d), at file[%s], line[%d]",
        //     WorldCoordinate(position.x),
        //     WorldCoordinate(position.y),
        //     "world_server.cpp",
        //     0x14dd)
        L2_LOG_ERROR(
            Log, "[shuttle] No world container covers enter-world position (%d, %d) for shuttle %u.",
            WorldCoordinate(position.x), WorldCoordinate(position.y), shuttle->Id()
        );
        return;
    }

    shuttle->m_data->SetPos(position);
    container->PushShuttle(shuttle);
    shuttle->m_data->m_bIsActive = true;

    class ContainerFunc final : public CContainerFunc {
    public:
        explicit ContainerFunc(CShuttle* shuttle) : shuttle_(shuttle) {}

        void Do(CObject* candidate) override {
            if (candidate == shuttle_ || (candidate->IsPC() && (candidate->IsDeleting() || candidate->m_Deleted != 0))) {
                return;
            }
            candidate->InSight(shuttle_, false);
            shuttle_->InSight(candidate, false);
        }

    private:
        CShuttle* shuttle_;
    } function(shuttle);


    CContainerIndex neighbors = CContainerDB::Inst().GatherNeighborContainerIndex(container);
    CContainerFilterNull filter;
    FOR_ALL_OBJECT_IN_CONTAINER_INDEX(neighbors, filter, function);
    shuttle->AddTimer(666, 0);
}

void CWorld::__async_AsyncShuttleLeaveWorld(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CShuttle* shuttle;
    std::memcpy(&shuttle, argument->Buffer + 0x08, sizeof(shuttle));
    ReleaseHelper<CShuttle, ObjectRefType::SerializedWorldThread> releaseShuttle(shuttle, "world_server.cpp", 0x150a);
    if (shuttle == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "pShuttle null at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0x150f)
        L2_LOG_ERROR(Log, "[shuttle] Async leave-world operation has no shuttle; operation aborted.");
        return;
    }

    CContainer* const container = CContainerDB::Inst().GetContainer(shuttle->m_pContainer->m_Data.m_nParentContainerIndex);
    if (container == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "container NULL at file [%s], line [%d]",
        //     "world_server.cpp",
        //     0x1516)
        L2_LOG_ERROR(
            Log, "[shuttle] Shuttle %u references a missing parent world container; leave-world operation aborted.",
            shuttle->Id()
        );
        return;
    }

    class ContainerFilter final : public CContainerFilter {
    public:
        explicit ContainerFilter(CShuttle* shuttle) : shuttle_(shuttle) {}

        bool CanDo(CObject* candidate) override { return candidate != shuttle_; }

    private:
        CShuttle* shuttle_;
    } filter(shuttle);


    class ContainerFunc final : public CContainerFunc {
    public:
        explicit ContainerFunc(CShuttle* shuttle) : shuttle_(shuttle) {}

        void Do(CObject* candidate) override {
            if (candidate->IsPC() && (candidate->IsDeleting() || candidate->m_Deleted != 0)) {
                return;
            }
            candidate->OutOfSight(shuttle_, true);
            shuttle_->OutOfSight(candidate, true);
        }

    private:
        CShuttle* shuttle_;
    } function(shuttle);


    CContainerIndex neighbors = CContainerDB::Inst().GatherNeighborContainerIndex(container);
    FOR_ALL_OBJECT_IN_CONTAINER_INDEX__INSTANT(neighbors, filter, function);
    container->PopShuttle(shuttle);
    shuttle->m_data->m_bIsActive = false;
}

void CWorld::__async_AsyncDoorStatusUpdate(CExecutionArgumentPad* argument) {
    // Current L2Server 0x00c698b0. The current collector at 0x00c694a8
    // snapshots direct creatures plus creatures riding vehicles and
    // shuttles before invoking the per-user callback at 0x00c59f5c.
    L2_GUARD_FUNCTION();

    CDoor* door;
    std::memcpy(&door, argument->Buffer + 0x08, sizeof(door));
    ReleaseHelper<CDoor, ObjectRefType::SerializedWorldThread> releaseDoor(door, "world_server.cpp", 0x6fc);

    CContainer* const container = CContainerDB::Inst().GetContainer(door->m_data->container_index);
    if (container == nullptr) {
        const FVector yPosition = door->GetPos();
        const int y = runtime::Win64CvttDoubleToInt32(yPosition.y);
        const FVector xPosition = door->GetPos();
        const int x = runtime::Win64CvttDoubleToInt32(xPosition.x);
        // Log.Add(
        //     LOG_ERROR,
        //     "Door[%S] container NULL pos[%d, %d] "
        //     "at file [%s], line [%d]",
        //     door->m_sDoorName,
        //     x,
        //     y,
        //     "world_server.cpp",
        //     0x704)
        // L2_LOG_ERROR(Log,
        //     "[doors] Door '%S' has no world container at (%d, %d). Status update aborted.",
        //     door->m_sDoorName,
        //     x,
        //     y)
        L2_LOG_ERROR(
            Log, u"[doors] Door '%S' has no world container at (%d, %d); status update aborted.", door->m_sDoorName, x, y
        );
        return;
    }

    CContainerIndex neighbors = CContainerDB::Inst().GatherNeighborContainerIndex(container);
    std::vector<CCreature*> creatures;
    const auto appendCreatures = [&creatures](CContainer* current) {
        int creatureIndex = current->m_Data.m_nCreatureFirst;
        while (creatureIndex != -1) {
            CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(
                static_cast<std::uint32_t>(CCreatureFactory::Inst().GetIdByIndex(creatureIndex))
            );
            if (creature != nullptr) {
                creatures.push_back(creature);
            }
            creatureIndex = CCreatureFactory::Inst().GetNextIndex(creatureIndex);
        }
    };

    for (int neighborIndex = 0; neighborIndex < neighbors.n; ++neighborIndex) {
        CContainer* const neighbor = neighbors.container[static_cast<std::size_t>(neighborIndex)];
        appendCreatures(neighbor);

        int vehicleContainerIndex = neighbor->m_Data.m_nVehicleFirst;
        while (vehicleContainerIndex != -1) {
            CContainer* const vehicleContainer = CContainerDB::Inst().GetContainer(vehicleContainerIndex);
            appendCreatures(vehicleContainer);
            vehicleContainerIndex = vehicleContainer->m_Data.m_nNext;
        }

        int shuttleIndex = neighbor->m_Data.m_nShuttleFirst;
        while (shuttleIndex != -1) {
            CShuttle* const shuttle = CShuttle::s_ShuttleSmartIdManager.GetObjectW(
                static_cast<std::uint32_t>(CShuttleFactory::Inst().GetIdByIndex(shuttleIndex))
            );
            if (shuttle != nullptr && shuttle->m_pContainer != nullptr) {
                appendCreatures(shuttle->m_pContainer);
            }
            shuttleIndex = CShuttleFactory::Inst().GetNextIndex(shuttleIndex);
        }
    }

    for (CCreature* const creature : creatures) {
        if (creature->IsPC()) {
            if (creature->CastUser()->GetSocket() != nullptr) {
                door->SendDoorStatus(creature->CastUser()->GetSocket());
            }
        }
    }
}

void CWorld::__async_AsyncDoorOpenClose(CExecutionArgumentPad* argument) {
    // Current L2Server 0x00c6b758. The current body uses the same
    // 0x00c694a8 neighborhood collector and 0x00c59f5c per-user callback as
    // AsyncDoorStatusUpdate after it propagates the state to door relations.
    L2_GUARD_FUNCTION();

    CDoor* door;
    bool opened;
    bool forced;
    std::memcpy(&door, argument->Buffer + 0x08, sizeof(door));
    std::memcpy(&opened, argument->Buffer + 0x10, sizeof(opened));
    std::memcpy(&forced, argument->Buffer + 0x11, sizeof(forced));
    ReleaseHelper<CDoor, ObjectRefType::SerializedWorldThread> releaseDoor(door, "world_server.cpp", 0x727);

    if (!opened) {
        if (!door->m_data->m_bIsOpen) {
            return;
        }
        door->m_data->m_bIsOpen = false;
        door->ValidatePlaneCollision();
        if (door->m_eType != DOORT_CHILD) {
            DoorEventMap::inst().SetEvent(door, 0);
            DoorStatusMap::inst().SetEvent(door->m_sDoorName, door->GetInZoneID());
        }

        for (std::int32_t slaveIndex = 0; static_cast<std::size_t>(slaveIndex) < door->m_vSlaves.size(); ++slaveIndex) {
            CDoor* const slave = runtime::CheckedVectorAt(door->m_vSlaves, static_cast<std::size_t>(slaveIndex));
            slave->DoDoorAction(slave->m_eMasterCloseEvent);
        }

        if (door->m_eType == DOORT_PARENT) {
            for (std::int32_t childIndex = 0; static_cast<std::size_t>(childIndex) < door->m_vChilds.size(); ++childIndex) {
                runtime::CheckedVectorAt(door->m_vChilds, static_cast<std::size_t>(childIndex))->Close(true, forced);
            }
            return;
        }
    } else {
        if (door->m_data->m_bIsOpen) {
            return;
        }
        door->m_data->m_bIsOpen = true;
        door->ValidatePlaneCollision();
        if (door->m_eType != DOORT_CHILD) {
            DoorEventMap::inst().SetEvent(door, 1);
            DoorStatusMap::inst().SetEvent(door->m_sDoorName, door->GetInZoneID());
        }

        for (std::int32_t slaveIndex = 0; static_cast<std::size_t>(slaveIndex) < door->m_vSlaves.size(); ++slaveIndex) {
            CDoor* const slave = runtime::CheckedVectorAt(door->m_vSlaves, static_cast<std::size_t>(slaveIndex));
            slave->DoDoorAction(slave->m_eMasterOpenEvent);
        }

        if (door->m_eType == DOORT_PARENT) {
            for (std::int32_t childIndex = 0; static_cast<std::size_t>(childIndex) < door->m_vChilds.size(); ++childIndex) {
                runtime::CheckedVectorAt(door->m_vChilds, static_cast<std::size_t>(childIndex))->Open(true, forced);
            }
            return;
        }
    }

    CContainer* const container = CContainerDB::Inst().GetContainer(door->m_data->container_index);
    if (container == nullptr) {
        const int inZoneType = door->m_nInstantZoneTypeID;
        const FVector yPosition = door->GetPos();
        const int y = runtime::Win64CvttDoubleToInt32(yPosition.y);
        const FVector xPosition = door->GetPos();
        const int x = runtime::Win64CvttDoubleToInt32(xPosition.x);
        // Log.Add(
        //     LOG_ERROR,
        //     "Door[%S] container NULL pos[%d, %d] inzone[%d] "
        //     "at file [%s], line [%d]",
        //     door->m_sDoorName,
        //     x,
        //     y,
        //     inZoneType,
        //     "world_server.cpp",
        //     0x776)
        // L2_LOG_ERROR(Log,
        //     "[doors] Door '%S' has no world container at (%d, %d) in instance type %d. State propagation aborted.",
        //     door->m_sDoorName,
        //     x,
        //     y,
        //     inZoneType)
        L2_LOG_ERROR(
            Log, u"[doors] Door '%S' has no world container at (%d, %d) in instance type %d; state propagation aborted.",
            door->m_sDoorName, x, y, inZoneType
        );
        return;
    }

    CContainerIndex neighbors = CContainerDB::Inst().GatherNeighborContainerIndex(container);
    std::vector<CCreature*> creatures;
    const auto appendCreatures = [&creatures](CContainer* current) {
        int creatureIndex = current->m_Data.m_nCreatureFirst;
        while (creatureIndex != -1) {
            CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(
                static_cast<std::uint32_t>(CCreatureFactory::Inst().GetIdByIndex(creatureIndex))
            );
            if (creature != nullptr) {
                creatures.push_back(creature);
            }
            creatureIndex = CCreatureFactory::Inst().GetNextIndex(creatureIndex);
        }
    };

    for (int neighborIndex = 0; neighborIndex < neighbors.n; ++neighborIndex) {
        CContainer* const neighbor = neighbors.container[static_cast<std::size_t>(neighborIndex)];
        appendCreatures(neighbor);

        int vehicleContainerIndex = neighbor->m_Data.m_nVehicleFirst;
        while (vehicleContainerIndex != -1) {
            CContainer* const vehicleContainer = CContainerDB::Inst().GetContainer(vehicleContainerIndex);
            appendCreatures(vehicleContainer);
            vehicleContainerIndex = vehicleContainer->m_Data.m_nNext;
        }

        int shuttleIndex = neighbor->m_Data.m_nShuttleFirst;
        while (shuttleIndex != -1) {
            CShuttle* const shuttle = CShuttle::s_ShuttleSmartIdManager.GetObjectW(
                static_cast<std::uint32_t>(CShuttleFactory::Inst().GetIdByIndex(shuttleIndex))
            );
            if (shuttle != nullptr && shuttle->m_pContainer != nullptr) {
                appendCreatures(shuttle->m_pContainer);
            }
            shuttleIndex = CShuttleFactory::Inst().GetNextIndex(shuttleIndex);
        }
    }

    for (CCreature* const creature : creatures) {
        if (creature->IsPC()) {
            if (creature->CastUser()->GetSocket() != nullptr) {
                door->SendDoorStatus(creature->CastUser()->GetSocket());
            }
        }
    }
}

void CWorld::__async_AsyncShuttleKickFromDoor(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CDoor* door;
    FVector banPosition;
    std::memcpy(&door, argument->Buffer + 0x08, sizeof(door));
    std::memcpy(&banPosition, argument->Buffer + 0x10, sizeof(banPosition));

    ReleaseHelper<CDoor, ObjectRefType::SerializedWorldThread> releaseDoor(door, "world_server.cpp", 0x15ba);

    class TLMFilter final : public CTLMFilterDist {
    public:
        explicit TLMFilter(CDoor* door) : CTLMFilterDist(static_cast<CWorldObject*>(door), 1024.0), door_(door) {}

        bool CanDo(CObject* object) override {
            CWorldObject* const worldObject = object->CastWorldObject();
            if (worldObject == nullptr || !CTLMFilterDist::CanDo(object)) {
                return false;
            }
            CTerritory territory;
            door_->GetTerritory(territory);
            return territory.IsInside(worldObject->GetPos(), false);
        }

    private:
        CDoor* door_;
    } filter(door);


    class TLMFunc final : public CTLMFunc {
    public:
        explicit TLMFunc(FVector banPosition) : banPosition_(banPosition) {}

        bool CanDo(CObject* object, double) override {
            if (object->IsPC() && (object->IsDeleting() || object->m_Deleted != 0)) {
                return true;
            }
            CCreature* const creature = object->CastCreature();
            return creature->TeleportToLocation(
                WorldCoordinate(banPosition_.x), WorldCoordinate(banPosition_.y), WorldCoordinate(banPosition_.z), 0,
                0x10000, TeleportSourceType::Unknown, nullptr, 0
            );
        }

    private:
        FVector banPosition_;
    } function(banPosition);


    FOR_ALL_NEIGHBOR_CREATURE_DIST(&filter, &function);
}

void CWorld::__async_AsyncShuttleMove(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CShuttle* shuttle;
    FVector position;
    std::memcpy(&shuttle, argument->Buffer + 0x08, sizeof(shuttle));
    std::memcpy(&position, argument->Buffer + 0x10, sizeof(position));
    if (shuttle == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Shuttle null",
        //     L"world_server.cpp",
        //     0x1549)
        L2_LOG_ERROR(Log, u"[shuttle] Async move operation has no shuttle; operation aborted.");
        return;
    }

    ReleaseHelper<CShuttle, ObjectRefType::SerializedWorldThread> releaseShuttle(shuttle, "world_server.cpp", 0x154d);
    const FVector current = shuttle->GetPos();
    const FVector delta{position.x - current.x, position.y - current.y, position.z - current.z};
    if (!IsValidPosition(WorldCoordinate(position.x), WorldCoordinate(position.y))) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] trying to move Shuttle[%d] to "
        //     L"invalid position (%d, %d, %d)",
        //     L"world_server.cpp",
        //     0x155f,
        //     static_cast<int>(shuttle->Id()),
        //     WorldCoordinate(position.x),
        //     WorldCoordinate(position.y),
        //     WorldCoordinate(position.z))
        L2_LOG_ERROR(
            Log, u"[shuttle] Destination (%d, %d, %d) is invalid for shuttle %u; move rejected.",
            WorldCoordinate(position.x), WorldCoordinate(position.y), WorldCoordinate(position.z), shuttle->Id()
        );
        return;
    }

    CContainer* const source = CContainerDB::Inst().GetContainer(shuttle->m_pContainer->m_Data.m_nParentContainerIndex);
    CContainer* const destination = CContainerDB::Inst().GetWorldContainer(
        WorldCoordinate(position.x), WorldCoordinate(position.y), WorldCoordinate(position.z)
    );
    if (source == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"pContFrom null index at file[%s], line[%d]",
        //     L"world_server.cpp",
        //     0x156b)
        L2_LOG_ERROR(Log, u"[shuttle] Shuttle %u has no source world container; move rejected.", shuttle->Id());
        return;
    }
    if (destination == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"pContTo null pos(%d, %d) at file[%s], line[%d]",
        //     WorldCoordinate(position.x),
        //     WorldCoordinate(position.y),
        //     L"world_server.cpp",
        //     0x1572)
        L2_LOG_ERROR(
            Log, u"[shuttle] No destination container covers (%d, %d); move request for shuttle %u rejected.",
            WorldCoordinate(position.x), WorldCoordinate(position.y), shuttle->Id()
        );
        return;
    }

    shuttle->m_data->SetPos(position);
    if (source != destination && source->PopShuttle(shuttle)) {
        destination->PushShuttle(shuttle);
        CContainerDB::Inst().ContainerChanged(shuttle, source, destination, false);
    }

    CCreatureFactory& factory = CCreatureFactory::Inst();
    int index = shuttle->m_pContainer->m_Data.m_nCreatureFirst;
    while (index != -1) {
        const std::uint32_t smartId = static_cast<std::uint32_t>(factory.GetIdByIndex(index));
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(smartId);
        if (creature == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"invalid creature at CShuttle::TimerExpired()")
            L2_LOG_ERROR(
                Log,
                u"[shuttle] Passenger runtime ID %u cannot be resolved while moving shuttle %u; passenger scan stopped.",
                smartId, shuttle->Id()
            );
            break;
        }

        L2_ADD_REF(creature, ObjectRefType::FindObject);
        if (!creature->m_data->m_bIsActive) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"inactive creature in Shuttle!")
            L2_LOG_ERROR(
                Log, u"[shuttle] Passenger %u is inactive while attached to shuttle %u.", creature->Id(), shuttle->Id()
            );
        } else {
            const FVector creaturePosition = creature->GetPos();
            const FVector adjusted{creaturePosition.x + delta.x, creaturePosition.y + delta.y, creaturePosition.z + delta.z};
            if (!IsValidPosition(WorldCoordinate(adjusted.x), WorldCoordinate(adjusted.y))) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"AdjustPos Error NowPos(%d,%d,%d), "
                //     L"AdjustPos(%d, %d, %d), Delta(%d, %d, %d)",
                //     WorldCoordinate(creaturePosition.x),
                //     WorldCoordinate(creaturePosition.y),
                //     WorldCoordinate(creaturePosition.z),
                //     WorldCoordinate(adjusted.x),
                //     WorldCoordinate(adjusted.y),
                //     WorldCoordinate(adjusted.z),
                //     WorldCoordinate(delta.x),
                //     WorldCoordinate(delta.y),
                //     WorldCoordinate(delta.z))
                L2_LOG_ERROR(
                    Log,
                    u"[shuttle] Moving passenger %u with shuttle %u would produce invalid position (%d, %d, %d) from (%d, "
                    u"%d, %d), delta (%d, %d, %d).",
                    creature->Id(), shuttle->Id(), WorldCoordinate(adjusted.x), WorldCoordinate(adjusted.y),
                    WorldCoordinate(adjusted.z), WorldCoordinate(creaturePosition.x), WorldCoordinate(creaturePosition.y),
                    WorldCoordinate(creaturePosition.z), WorldCoordinate(delta.x), WorldCoordinate(delta.y),
                    WorldCoordinate(delta.z)
                );
            } else if (shuttle->m_pContainer->m_Data.m_nIndex == creature->m_data->container_index) {
                MoveTo(creature, adjusted, false);
            }
        }

        const int nextIndex = factory.GetNextIndex(index);
        L2_RELEASE(creature, ObjectRefType::FindObject);
        index = nextIndex;
    }
}

void CWorld::__async_AsyncAirShipEnterWorld(CExecutionArgumentPad* argument) {
    // Current L2Server 0x00c5bea8.
    L2_GUARD_FUNCTION();

    CAirShip* airShip;
    FVector position;
    std::memcpy(&airShip, argument->Buffer + 0x08, sizeof(airShip));
    std::memcpy(&position, argument->Buffer + 0x10, sizeof(position));

    ReleaseHelper<CAirShip, ObjectRefType::SerializedWorldThread> releaseAirShip(airShip, "world_server.cpp", 0x129e);
    if (airShip == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "pAirShip null at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0x12a2)
        L2_LOG_ERROR(Log, "[airship] Async enter-world operation has no airship; operation aborted.");
        return;
    }
    if (!IsValidPosition(WorldCoordinate(position.x), WorldCoordinate(position.y))) {
        // Log.Add(
        //     LOG_ERROR,
        //     "invalid position (%d, %d) at file[%s], line[%d]",
        //     WorldCoordinate(position.x),
        //     WorldCoordinate(position.y),
        //     "world_server.cpp",
        //     0x12a9)
        L2_LOG_ERROR(
            Log, "[airship] Enter-world position (%d, %d) is outside the world; operation aborted.",
            WorldCoordinate(position.x), WorldCoordinate(position.y)
        );
        return;
    }

    CContainer* const container = CContainerDB::Inst().GetWorldContainer(
        WorldCoordinate(position.x), WorldCoordinate(position.y), WorldCoordinate(position.z)
    );
    if (container == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "maplock NULL pos(%d, %d), at file[%s], line[%d]",
        //     WorldCoordinate(position.x),
        //     WorldCoordinate(position.y),
        //     "world_server.cpp",
        //     0x12b1)
        L2_LOG_ERROR(
            Log, "[airship] No world container covers enter-world position (%d, %d); operation aborted.",
            WorldCoordinate(position.x), WorldCoordinate(position.y)
        );
        return;
    }

    airShip->m_data->m_bIsActive = true;
    airShip->m_data->SetPos(position);
    container->PushAirShip(airShip);
    airShip->m_data->m_bIsInWorld = true;

    class ContainerFunc final : public CContainerFunc {
    public:
        explicit ContainerFunc(CAirShip* airShip) : airShip_(airShip) {}

        void Do(CObject* candidate) override {
            if (candidate == airShip_ || (candidate->IsPC() && (candidate->IsDeleting() || candidate->m_Deleted != 0))) {
                return;
            }
            candidate->InSight(airShip_, false);
            airShip_->InSight(candidate, false);
        }

    private:
        CAirShip* airShip_;
    } function(airShip);


    CContainerIndex neighbors = CContainerDB::Inst().GatherNeighborContainerIndex(container);
    CContainerFilterNull filter;
    FOR_ALL_OBJECT_IN_CONTAINER_INDEX(neighbors, filter, function);
    airShip->AddTimer(500, -2);
}

void CWorld::__async_AsyncAirShipLeaveWorld(CExecutionArgumentPad* argument) {
    // Current L2Server 0x00c5c17c.
    L2_GUARD_FUNCTION();

    CAirShip* airShip;
    std::memcpy(&airShip, argument->Buffer + 0x08, sizeof(airShip));
    ReleaseHelper<CAirShip, ObjectRefType::SerializedWorldThread> releaseAirShip(airShip, "world_server.cpp", 0x12dd);
    if (airShip == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "airship null at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0x12e1)
        L2_LOG_ERROR(Log, "[airship] Async leave-world operation has no airship; operation aborted.");
        return;
    }

    CContainer* const container = CContainerDB::Inst().GetContainer(airShip->m_pContainer->m_Data.m_nParentContainerIndex);
    if (container == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "container NULL at file [%s], line [%d]",
        //     "world_server.cpp",
        //     0x12e8)
        L2_LOG_ERROR(
            Log, "[airship] Airship %u references a missing parent world container; leave-world operation aborted.",
            airShip->Id()
        );
        return;
    }

    class ContainerFilter final : public CContainerFilter {
    public:
        explicit ContainerFilter(CAirShip* airShip) : airShip_(airShip) {}

        bool CanDo(CObject* candidate) override { return candidate != airShip_; }

    private:
        CAirShip* airShip_;
    } filter(airShip);


    class ContainerFunc final : public CContainerFunc {
    public:
        explicit ContainerFunc(CAirShip* airShip) : airShip_(airShip) {}

        void Do(CObject* candidate) override {
            if ((candidate->IsPC() && (candidate->IsDeleting() || candidate->m_Deleted != 0)) || airShip_->IsDeleting() ||
                airShip_->m_Deleted != 0) {
                return;
            }
            candidate->OutOfSight(airShip_, true);
            airShip_->OutOfSight(candidate, true);
        }

    private:
        CAirShip* airShip_;
    } function(airShip);


    CContainerIndex neighbors = CContainerDB::Inst().GatherNeighborContainerIndex(container);
    FOR_ALL_OBJECT_IN_CONTAINER_INDEX__INSTANT(neighbors, filter, function);
    container->PopAirShip(airShip);
    airShip->m_data->m_bIsInWorld = false;
}

void CWorld::__async_AsyncAirShipTeleport(CExecutionArgumentPad* argument) {
    // Current L2Server 0x00c6a70c.
    L2_GUARD_FUNCTION();

    CAirShip* airShip;
    FVector position;
    unsigned int inZoneId;
    int direction;
    std::memcpy(&airShip, argument->Buffer + 0x08, sizeof(airShip));
    std::memcpy(&position, argument->Buffer + 0x10, sizeof(position));
    std::memcpy(&inZoneId, argument->Buffer + 0x28, sizeof(inZoneId));
    std::memcpy(&direction, argument->Buffer + 0x2c, sizeof(direction));

    ReleaseHelper<CAirShip, ObjectRefType::SerializedWorldThread> releaseAirShip(airShip, "world_server.cpp", 0x11e2);
    const int sourceIndex = airShip->m_pContainer->m_Data.m_nParentContainerIndex;
    CContainer* const source = CContainerDB::Inst().GetContainer(sourceIndex);
    if (source == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] container[%d] NULL",
        //     "world_server.cpp",
        //     0x11e8,
        //     sourceIndex)
        L2_LOG_ERROR(
            Log, "[airship] Source world container %d is missing for airship %u; teleport aborted.", sourceIndex,
            airShip->Id()
        );
        return;
    }

    std::vector<CCreature*> passengers;
    CCreatureFactory& creatureFactory = CCreatureFactory::Inst();
    int index = airShip->m_pContainer->m_Data.m_nCreatureFirst;
    while (index != -1) {
        const std::uint32_t smartId = static_cast<std::uint32_t>(creatureFactory.GetIdByIndex(index));
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(smartId);
        if (creature == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[AirShip] invalid creature at "
            //     L"CWorld::AsyncAirShipTeleport()")
            L2_LOG_ERROR(
                Log,
                u"[airship] Passenger runtime ID %u cannot be resolved while teleporting airship %u; passenger scan "
                u"stopped.",
                smartId, airShip->Id()
            );
            break;
        }

        L2_ADD_REF(creature, ObjectRefType::FindObject);
        if (!creature->IsSummonOrPet()) {
            if (!creature->IsActive()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[AirShip] inactive creature in airship!")
                L2_LOG_ERROR(
                    Log, u"[airship] Passenger %u is inactive while attached to airship %u during teleport.", creature->Id(),
                    airShip->Id()
                );
            } else {
                passengers.push_back(creature);
            }
        }
        const int nextIndex = creatureFactory.GetNextIndex(index);
        L2_RELEASE(creature, ObjectRefType::FindObject);
        index = nextIndex;
    }

    for (CCreature* const creature : passengers) {
        creature->TeleportWithAirShip(airShip, position, inZoneId);
    }

    class ContainerFilter final : public CContainerFilter {
    public:
        explicit ContainerFilter(CAirShip* airShip) : airShip_(airShip) {}

        bool CanDo(CObject* candidate) override { return candidate != airShip_; }

    private:
        CAirShip* airShip_;
    } filter(airShip);


    class ContainerFunc final : public CContainerFunc {
    public:
        explicit ContainerFunc(CAirShip* airShip) : airShip_(airShip) {}

        void Do(CObject* candidate) override {
            if (candidate->IsPC() && (candidate->IsDeleting() || candidate->m_Deleted != 0)) {
                return;
            }
            candidate->OutOfSight(airShip_, true);
        }

    private:
        CAirShip* airShip_;
    } function(airShip);


    CContainerIndex neighbors = CContainerDB::Inst().GatherNeighborContainerIndex(source);
    FOR_ALL_OBJECT_IN_CONTAINER_INDEX__INSTANT(neighbors, filter, function);
    source->PopAirShip(airShip);

    CContainer* const destination = CContainerDB::Inst().GetWorldContainer(
        WorldCoordinate(position.x), WorldCoordinate(position.y), WorldCoordinate(position.z)
    );
    if (destination == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] container NULL at (%d, %d, %d)",
        //     "world_server.cpp",
        //     0x1241,
        //     WorldCoordinate(position.x),
        //     WorldCoordinate(position.y),
        //     WorldCoordinate(position.z))
        L2_LOG_ERROR(
            Log, "[airship] No destination container covers teleport position (%d, %d, %d) for airship %u.",
            WorldCoordinate(position.x), WorldCoordinate(position.y), WorldCoordinate(position.z), airShip->Id()
        );
        return;
    }

    airShip->m_data->SetPos(position);
    airShip->SetDir(FAngle{direction});
    destination->PushAirShip(airShip);

    CStaticObjectFactory& staticFactory = CStaticObjectFactory::Inst();
    index = airShip->m_pContainer->m_Data.m_nStaticObjectFirst;
    while (index != -1) {
        const std::uint32_t smartId = static_cast<std::uint32_t>(staticFactory.GetIdByIndex(index));
        CStaticObject* const object = CStaticObject::GetObjectW(smartId);
        if (object == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[AirShip] invalid StaticObject at "
            //     L"CWorld::AsyncAirShipTeleport()")
            L2_LOG_ERROR(
                Log,
                u"[airship] Static-object runtime ID %u cannot be resolved while teleporting airship %u; object scan "
                u"stopped.",
                smartId, airShip->Id()
            );
            break;
        }

        L2_ADD_REF(object, ObjectRefType::FindObject);
        object->SetPos(airShip->ConvRelToAbs(object->GetRelPos()));
        const int nextIndex = staticFactory.GetNextIndex(index);
        L2_RELEASE(object, ObjectRefType::FindObject);
        index = nextIndex;
    }
}

void CWorld::__async_AsyncAirShipMove(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CAirShip* airShip;
    CContainer* destination;
    std::memcpy(&airShip, argument->Buffer + 0x08, sizeof(airShip));
    std::memcpy(&destination, argument->Buffer + 0x10, sizeof(destination));

    ReleaseHelper<CAirShip, ObjectRefType::SerializedWorldThread> releaseAirShip(airShip, "world_server.cpp", 0x131b);
    CContainer* const source = CContainerDB::Inst().GetContainer(airShip->m_pContainer->m_Data.m_nParentContainerIndex);
    if (source == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[AirShip] pContFrom null index at file[%s], line[%d]",
        //     L"world_server.cpp",
        //     0x1323)
        L2_LOG_ERROR(Log, u"[airship] Airship %u has no source world container; async move aborted.", airShip->Id());
        return;
    }
    if (source != destination && source->PopAirShip(airShip)) {
        destination->PushAirShip(airShip);
        CContainerDB::Inst().ContainerChanged(airShip, source, destination, false);
    }

    CCreatureFactory& creatureFactory = CCreatureFactory::Inst();
    int index = airShip->m_pContainer->m_Data.m_nCreatureFirst;
    while (index != -1) {
        const std::uint32_t smartId = static_cast<std::uint32_t>(creatureFactory.GetIdByIndex(index));
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(smartId);
        if (creature == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[AirShip] invalid creature at CWorld::AirShipMove()")
            L2_LOG_ERROR(
                Log,
                u"[airship] Passenger runtime ID %u cannot be resolved while moving airship %u; passenger scan stopped.",
                smartId, airShip->Id()
            );
            break;
        }

        L2_ADD_REF(creature, ObjectRefType::FindObject);
        if (!creature->m_data->m_bIsActive) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[AirShip] inactive creature in airship!")
            L2_LOG_ERROR(
                Log, u"[airship] Passenger %u is inactive while attached to airship %u during movement.", creature->Id(),
                airShip->Id()
            );
        } else {
            MoveToRelatively(creature, airShip, creature->GetRelPos());
        }
        const int nextIndex = creatureFactory.GetNextIndex(index);
        L2_RELEASE(creature, ObjectRefType::FindObject);
        index = nextIndex;
    }

    CStaticObjectFactory& staticFactory = CStaticObjectFactory::Inst();
    index = airShip->m_pContainer->m_Data.m_nStaticObjectFirst;
    while (index != -1) {
        const std::uint32_t smartId = static_cast<std::uint32_t>(staticFactory.GetIdByIndex(index));
        CStaticObject* const object = CStaticObject::GetObjectW(smartId);
        if (object == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[AirShip] invalid StaticObject at "
            //     L"CWorld::AirShipMove()")
            L2_LOG_ERROR(
                Log,
                u"[airship] Static-object runtime ID %u cannot be resolved while moving airship %u; object scan stopped.",
                smartId, airShip->Id()
            );
            break;
        }

        L2_ADD_REF(object, ObjectRefType::FindObject);
        object->SetPos(airShip->ConvRelToAbs(object->GetRelPos()));
        const int nextIndex = staticFactory.GetNextIndex(index);
        L2_RELEASE(object, ObjectRefType::FindObject);
        index = nextIndex;
    }
}

void CWorld::__async_AsyncGetOffAirShip(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CCreature* creature;
    CAirShip* airShip;
    FVector position;
    std::memcpy(&creature, argument->Buffer + 0x08, sizeof(creature));
    std::memcpy(&airShip, argument->Buffer + 0x10, sizeof(airShip));
    std::memcpy(&position, argument->Buffer + 0x18, sizeof(position));

    ReleaseHelper<CCreature, ObjectRefType::SerializedWorldThread> releaseCreature(creature, "world_server.cpp", 0x1184);
    ReleaseHelper<CAirShip, ObjectRefType::SerializedWorldThread> releaseAirShip(airShip, "world_server.cpp", 0x1185);

    CContainer* const destination = CContainerDB::Inst().GetWorldContainer(
        WorldCoordinate(position.x), WorldCoordinate(position.y), WorldCoordinate(position.z)
    );
    if (destination == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] AsyncGetOffAirShip  : "
        //     L"pCreatureContainer = NULL",
        //     L"world_server.cpp",
        //     0x118d)
        L2_LOG_ERROR(
            Log, u"[airship] No destination container covers disembark position (%d, %d, %d) for creature %u.",
            WorldCoordinate(position.x), WorldCoordinate(position.y), WorldCoordinate(position.z), creature->Id()
        );
        return;
    }

    CContainer* const carrierContainer = airShip->m_pContainer;
    CContainer* const parent = CContainerDB::Inst().GetContainer(carrierContainer->m_Data.m_nParentContainerIndex);
    if (parent == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] AsyncGetOffAirShip  : "
        //     L"pVehicleParentContainer is invalid",
        //     L"world_server.cpp",
        //     0x1195)
        L2_LOG_ERROR(
            Log, u"[airship] Airship %u has no parent world container; disembark of creature %u aborted.", airShip->Id(),
            creature->Id()
        );
        return;
    }
    if (carrierContainer == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] AsyncGetOffAirShip  : "
        //     L"pVehicleContainer is invalid",
        //     L"world_server.cpp",
        //     0x119d)
        L2_LOG_ERROR(
            Log, u"[airship] Airship %u has no carrier container; disembark of creature %u aborted.", airShip->Id(),
            creature->Id()
        );
        return;
    }
    if (!carrierContainer->PopCreature(creature)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Already poped creature")
        L2_LOG_ERROR(
            Log, u"[airship] Creature %u was already removed from airship %u; disembark aborted.", creature->Id(),
            airShip->Id()
        );
        return;
    }

    class ContainerFunc final : public CContainerFunc {
    public:
        explicit ContainerFunc(CCreature* actor) : actor_(actor) {}

        void Do(CObject* candidate) override {
            if (candidate == actor_ || !CanUpdate(candidate) || !CanUpdate(actor_)) {
                return;
            }
            candidate->InSight(actor_, false);
            actor_->InSight(candidate, false);
        }

    private:
        static bool CanUpdate(CObject* object) {
            return !object->IsPC() || (!object->IsDeleting() && object->m_Deleted == 0);
        }

        CCreature* actor_;
    } function(creature);
    CContainerFilterNull filter;

    CContainerIndex carrierNeighbors = CContainerDB::Inst().GatherNeighborContainerIndex(carrierContainer);
    FOR_ALL_OBJECT_IN_CONTAINER_INDEX(carrierNeighbors, filter, function);
    CContainerIndex parentNeighbors = CContainerDB::Inst().GatherNeighborContainerIndex(parent);
    FOR_ALL_OBJECT_IN_CONTAINER_INDEX(parentNeighbors, filter, function);

    creature->m_data->m_bIsActive = false;
    creature->m_data->SetPos(position);
    creature->SetMyAirShipID(0);
    OnGetOnOffAirShip(creature, position);
    if (creature->IsPC()) {
        creature->CastUser()->ChangeUserMapDensity(position);
    }
}

void CWorld::__async_AsyncGetOnAirShip(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CCreature* creature;
    CAirShip* airShip;
    std::memcpy(&creature, argument->Buffer + 0x08, sizeof(creature));
    std::memcpy(&airShip, argument->Buffer + 0x10, sizeof(airShip));

    ReleaseHelper<CCreature, ObjectRefType::SerializedWorldThread> releaseCreature(creature, "world_server.cpp", 0x1142);
    ReleaseHelper<CAirShip, ObjectRefType::SerializedWorldThread> releaseAirShip(airShip, "world_server.cpp", 0x1143);

    CContainer* const carrierContainer = airShip->m_pContainer;
    if (carrierContainer == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] AsyncGetOnAirShip : "
        //     L"airship container is NULL",
        //     L"world_server.cpp",
        //     0x1149)
        L2_LOG_ERROR(
            Log, u"[airship] Airship %u has no carrier container; boarding of creature %u aborted.", airShip->Id(),
            creature->Id()
        );
        return;
    }

    CContainer* const parent = CContainerDB::Inst().GetContainer(carrierContainer->m_Data.m_nParentContainerIndex);
    if (parent == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] AsyncGetOnAirShip : "
        //     L"airship parent container is NULL",
        //     L"world_server.cpp",
        //     0x1151)
        L2_LOG_ERROR(
            Log, u"[airship] Airship %u has no parent world container while creature %u is boarding.", airShip->Id(),
            creature->Id()
        );
    }
    CContainer* const source = CContainerDB::Inst().GetContainer(creature->m_data->container_index);
    if (source == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] AsyncGetOnAirShip : "
        //     L"creature container is NULL",
        //     L"world_server.cpp",
        //     0x1158)
        L2_LOG_ERROR(
            Log, u"[airship] Creature %u references missing source container %d while boarding airship %u.", creature->Id(),
            creature->m_data->container_index, airShip->Id()
        );
    }

    if (!CContainer::ChangeContainerOf(creature, source, carrierContainer)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] AsyncGetOnAirShip "
        //     L"CContainer::ChangeContainerOf Failed!!!",
        //     L"world_server.cpp",
        //     0x1171)
        L2_LOG_ERROR(
            Log, u"[airship] Failed to move creature %u from container %d into airship %u.", creature->Id(),
            creature->m_data->container_index, airShip->Id()
        );
        return;
    }

    const FVector position = airShip->GetPos();
    creature->m_data->SetPos(position);
    creature->SetRelPos(FVector{});
    creature->ResetPassingPos(position);
    OnGetOnOffAirShip(creature, position);
    if (creature->IsPC()) {
        creature->CastUser()->ChangeUserMapDensity(position);
    }
    if (parent != source) {
        CContainerDB::Inst().ContainerChanged(creature, source, parent, false);
    }
}

void CWorld::__async_AsyncAppearIntoAirShip(CExecutionArgumentPad* argument) {
    // Current L2Server 0x00c64194.
    L2_GUARD_FUNCTION();

    User* user;
    CAirShip* airShip;
    std::memcpy(&user, argument->Buffer + 0x08, sizeof(user));
    std::memcpy(&airShip, argument->Buffer + 0x10, sizeof(airShip));

    ReleaseHelper<User, ObjectRefType::SerializedWorldThread> releaseUser(user, "world_server.cpp", 0x126e);
    ReleaseHelper<CAirShip, ObjectRefType::SerializedWorldThread> releaseAirShip(airShip, "world_server.cpp", 0x126f);

    CContainer* const carrierContainer = airShip->m_pContainer;
    if (carrierContainer == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] AsyncAppearIntoAirShip : "
        //     L"airship container is NULL",
        //     L"world_server.cpp",
        //     0x1275)
        L2_LOG_ERROR(
            Log, u"[airship] Airship %u has no carrier container; appearance of user %u aborted.", airShip->Id(), user->Id()
        );
        return;
    }

    user->m_data->m_bIsActive = true;
    const FVector position = airShip->GetPos();
    user->SetInZoneID(airShip->GetInZoneID());
    user->m_data->SetPos(position);
    user->ResetPassingPos(position);
    OnGetOnOffAirShip(user, position);
    user->ChangeUserMapDensity(position);
    carrierContainer->PushCreature(user);
    user->InSight(airShip, true);
}

void CWorld::OnGetOnOffAirShip(CCreature* creature, FVector position) {
    L2_GUARD_FUNCTION();

    const int x = WorldCoordinate(position.x);
    const int y = WorldCoordinate(position.y);
    const int z = WorldCoordinate(position.z);
    if (!creature->m_tlmBox3.Include(x, y, z)) {
        g_ptlCreatureMap->SetTlmBox3(creature->m_tlmBox3, x, y, z);
        g_ptlCreatureMap->Push(creature->m_data->index, position, creature);
        if (creature->IsPC()) {
            g_ptlUserMap->Push(creature->m_data->index, position, creature);
        }
    }
}

void CWorld::InitGeo() {
    L2_GUARD_FUNCTION();
    // Exact one-shot success behavior: no read/delete of an earlier member.
    // a repeated successful call overwrites it just like the current image.
    m_geo = new (std::nothrow) CGeoData(false);
}

bool CWorld::IsValidPosition(int x, int y) {
    CGeoData* const geo = GetGeo();
    return geo != nullptr && geo->IsValidPosition(x, y);
}

bool CWorld::StaticObjectAppear(CStaticObject* object, ::FVector position) {
    L2_GUARD_FUNCTION();
    L2_ADD_REF(object, ObjectRefType::SerializedWorldThread);
    AsyncStaticObjectAppear(object, position);
    return true;
}

bool CWorld::StaticObjectDisappear(CStaticObject* object) {
    L2_GUARD_FUNCTION();
    L2_ADD_REF(object, ObjectRefType::SerializedWorldThread);

    __async_arg_AsyncStaticObjectDisappear argument(object);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncStaticObjectDisappear(pad); }, &argument,
        L"AsyncStaticObjectDisappear"
    );
    return true;
}

void CWorld::LoadStaticObject(CStaticObject* object, CAirShip* airShip) {
    L2_GUARD_FUNCTION();
    L2_ADD_REF(object, ObjectRefType::SerializedWorldThread);
    L2_ADD_REF(airShip, ObjectRefType::SerializedWorldThread);

    __async_arg_AsyncLoadStaticObject argument(object, airShip);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncLoadStaticObject(pad); }, &argument,
        L"AsyncLoadStaticObject"
    );
}

void CWorld::UnLoadStaticObject(CStaticObject* object, CAirShip* airShip) {
    L2_GUARD_FUNCTION();
    L2_ADD_REF(object, ObjectRefType::SerializedWorldThread);
    L2_ADD_REF(airShip, ObjectRefType::SerializedWorldThread);

    __async_arg_AsyncUnLoadStaticObject argument(object, airShip);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncUnLoadStaticObject(pad); }, &argument,
        L"AsyncUnLoadStaticObject"
    );
}

void CWorld::AsyncStaticObjectAppear(CStaticObject* object, FVector position) {
    __async_arg_AsyncStaticObjectAppear argument(object, position);
    Serializer.AddExecutable(
        this, [](CWorld* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncStaticObjectAppear(pad); }, &argument,
        L"AsyncStaticObjectAppear"
    );
}

void CWorld::__async_AsyncStaticObjectAppear(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CStaticObject* actor;
    FVector position;
    std::memcpy(&actor, argument->Buffer + 0x08, sizeof(actor));
    std::memcpy(&position, argument->Buffer + 0x10, sizeof(position));

    ReleaseHelper<CStaticObject, ObjectRefType::SerializedWorldThread> releaseActor(actor, "world_server.cpp", 0x355);

    CContainer* container = nullptr;
    if (actor->IsInRealWorld()) {
        container = CContainerDB::Inst().GetWorldContainer(
            runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
            runtime::Win64CvttDoubleToInt32(position.z)
        );
    } else {
        CInstantZone* const instantZone = actor->GetInstantZone();
        if (instantZone != nullptr) {
            container = instantZone->GetContainer();
        }
    }

    if (container == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "maplock NULL pos(%d, %d), at file[%s], line[%d]",
        //     runtime::Win64CvttDoubleToInt32(position.x),
        //     runtime::Win64CvttDoubleToInt32(position.y),
        //     "world_server.cpp",
        //     0x368)
        L2_LOG_ERROR(
            Log, "[static objects] No container covers appearance position (%d, %d) for static object %u.",
            runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y), actor->Id()
        );
        return;
    }

    actor->m_data->SetPos(position);
    if (container->PushStaticObject(actor)) {
        actor->PushToThreadLocalMap();
    }
    actor->m_data->m_bIsActive = true;

    class ContainerFunc final : public CContainerFunc {
    public:
        explicit ContainerFunc(CStaticObject* actor) : actor_(actor) {}

        void Do(CObject* candidate) override {
            if (!candidate->IsPC() || (!candidate->IsDeleting() && candidate->m_Deleted == 0)) {
                candidate->InSight(actor_, true);
                actor_->InSight(candidate, false);
            }
        }

    private:
        CStaticObject* actor_;
    } function(actor);


    CContainerIndex neighbors = CContainerDB::Inst().GatherNeighborContainerIndex(container);
    CContainerFilterNull filter;
    FOR_ALL_OBJECT_IN_CONTAINER_INDEX(neighbors, filter, function);
}

void CWorld::__async_AsyncStaticObjectDisappear(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CStaticObject* actor;
    std::memcpy(&actor, argument->Buffer + 0x08, sizeof(actor));

    ReleaseHelper<CStaticObject, ObjectRefType::SerializedWorldThread> releaseActor(actor, "world_server.cpp", 0x392);

    CContainer* const container = CContainerDB::Inst().GetContainer(actor->m_data->container_index);
    if (container == nullptr) {
        const FVector yPosition = actor->GetPos();
        const int y = runtime::Win64CvttDoubleToInt32(yPosition.y);
        const FVector xPosition = actor->GetPos();
        const int x = runtime::Win64CvttDoubleToInt32(xPosition.x);
        // Log.Add(
        //     LOG_ERROR,
        //     "container NULL pos[%d, %d] at file [%s], line [%d]",
        //     x,
        //     y,
        //     "world_server.cpp",
        //     0x397)
        L2_LOG_ERROR(
            Log, "[static objects] Static object %u references a missing container at (%d, %d); disappearance aborted.",
            actor->Id(), x, y
        );
        return;
    }

    if (!container->PopStaticObject(actor)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Already poped pStaticObject")
        L2_LOG_ERROR(
            Log, u"[static objects] Static object %u was already removed from its container; disappearance aborted.",
            actor->Id()
        );
        return;
    }

    class ContainerFunc final : public CContainerFunc {
    public:
        explicit ContainerFunc(CStaticObject* actor) : actor_(actor) {}

        void Do(CObject* candidate) override {
            if (!candidate->IsPC() || (!candidate->IsDeleting() && candidate->m_Deleted == 0)) {
                candidate->OutOfSight(actor_, true);
                actor_->OutOfSight(candidate, true);
            }
        }

    private:
        CStaticObject* actor_;
    } function(actor);


    CContainerIndex neighbors = CContainerDB::Inst().GatherNeighborContainerIndex(container);
    CContainerFilterNull filter;
    FOR_ALL_OBJECT_IN_CONTAINER_INDEX(neighbors, filter, function);
    actor->m_data->m_bIsActive = false;
    actor->PopFromThreadLocalMap();
}

void CWorld::__async_AsyncLoadStaticObject(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CStaticObject* actor;
    CAirShip* airShip;
    std::memcpy(&actor, argument->Buffer + 0x08, sizeof(actor));
    std::memcpy(&airShip, argument->Buffer + 0x10, sizeof(airShip));

    ReleaseHelper<CStaticObject, ObjectRefType::SerializedWorldThread> releaseActor(actor, "world_server.cpp", 0x1367);
    ReleaseHelper<CAirShip, ObjectRefType::SerializedWorldThread> releaseAirShip(airShip, "world_server.cpp", 0x1368);

    if (airShip == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "pAirShip NULL at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0x136c)
        L2_LOG_ERROR(
            Log, "[static objects] Load request for static object %u has no airship; operation aborted.",
            actor != nullptr ? actor->Id() : 0
        );
        return;
    }
    if (actor == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "pStaticObject NULL at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0x1372)
        L2_LOG_ERROR(
            Log, "[static objects] Airship %u received a load request with no static object; operation aborted.",
            airShip->Id()
        );
        return;
    }

    CContainer* const airShipContainer = airShip->m_pContainer;
    if (airShipContainer == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "pAirShipContainer NULL  at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0x1379)
        L2_LOG_ERROR(
            Log, "[static objects] Airship %u has no carrier container; loading static object %u aborted.", airShip->Id(),
            actor->Id()
        );
        return;
    }

    CContainer* const worldContainer = CContainerDB::Inst().GetContainer(airShipContainer->m_Data.m_nParentContainerIndex);
    if (worldContainer == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "pAirShipWorldContainer NULL  at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0x1380)
        L2_LOG_ERROR(
            Log,
            "[static objects] Airship %u references a missing parent world container; loading static object %u aborted.",
            airShip->Id(), actor->Id()
        );
        return;
    }

    airShipContainer->PushStaticObject(actor);
    actor->m_data->m_bIsActive = true;

    class ContainerFunc final : public CContainerFunc {
    public:
        explicit ContainerFunc(CStaticObject* actor) : actor_(actor) {}

        void Do(CObject* candidate) override {
            if (!candidate->IsPC() || (!candidate->IsDeleting() && candidate->m_Deleted == 0)) {
                candidate->InSight(actor_, true);
                actor_->InSight(candidate, false);
            }
        }

    private:
        CStaticObject* actor_;
    } function(actor);


    CContainerIndex neighbors = CContainerDB::Inst().GatherNeighborContainerIndex(worldContainer);
    CContainerFilterNull filter;
    FOR_ALL_OBJECT_IN_CONTAINER_INDEX(neighbors, filter, function);
}

void CWorld::__async_AsyncUnLoadStaticObject(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CStaticObject* actor;
    CAirShip* airShip;
    std::memcpy(&actor, argument->Buffer + 0x08, sizeof(actor));
    std::memcpy(&airShip, argument->Buffer + 0x10, sizeof(airShip));

    ReleaseHelper<CStaticObject, ObjectRefType::SerializedWorldThread> releaseActor(actor, "world_server.cpp", 0x13a6);
    ReleaseHelper<CAirShip, ObjectRefType::SerializedWorldThread> releaseAirShip(airShip, "world_server.cpp", 0x13a7);

    CContainer* const airShipContainer = CContainerDB::Inst().GetContainer(actor->m_data->container_index);
    if (airShipContainer == nullptr) {
        const FVector yPosition = actor->GetPos();
        const int y = runtime::Win64CvttDoubleToInt32(yPosition.y);
        const FVector xPosition = actor->GetPos();
        const int x = runtime::Win64CvttDoubleToInt32(xPosition.x);
        // Log.Add(
        //     LOG_ERROR,
        //     "pAirShipContainer NULL pos[%d, %d] at file [%s], line [%d]",
        //     x,
        //     y,
        //     "world_server.cpp",
        //     0x13ad)
        L2_LOG_ERROR(
            Log, "[static objects] Static object %u references a missing airship container at (%d, %d); unload aborted.",
            actor->Id(), x, y
        );
        return;
    }

    CContainer* const worldContainer = CContainerDB::Inst().GetContainer(airShipContainer->m_Data.m_nParentContainerIndex);
    if (worldContainer == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "pAirShipWorldContainer NULL  at file[%s], line[%d]",
        //     "world_server.cpp",
        //     0x13b4)
        L2_LOG_ERROR(
            Log,
            "[static objects] Airship %u references a missing parent world container; unloading static object %u aborted.",
            airShip->Id(), actor->Id()
        );
        return;
    }

    if (!airShipContainer->PopStaticObject(actor)) {
        // Log.Add(LOG_ERROR, L"Already poped pStaticObject")
        L2_LOG_ERROR(
            Log, u"[static objects] Static object %u was already removed from airship %u; unload aborted.", actor->Id(),
            airShip->Id()
        );
        return;
    }

    class ContainerFunc final : public CContainerFunc {
    public:
        explicit ContainerFunc(CStaticObject* actor) : actor_(actor) {}

        void Do(CObject* candidate) override {
            if (!candidate->IsPC() || (!candidate->IsDeleting() && candidate->m_Deleted == 0)) {
                candidate->OutOfSight(actor_, true);
                actor_->OutOfSight(candidate, true);
            }
        }

    private:
        CStaticObject* actor_;
    } function(actor);


    CContainerIndex neighbors = CContainerDB::Inst().GatherNeighborContainerIndex(worldContainer);
    CContainerFilterNull filter;
    FOR_ALL_OBJECT_IN_CONTAINER_INDEX(neighbors, filter, function);
    actor->m_data->m_bIsActive = false;
}

void CWorld::IncreaseUserMapDensity1(int x, int y, const FVector& position, User& user) {
    L2_GUARD_FUNCTION();
    for (std::uint32_t deltaY = 0; deltaY != 3U; ++deltaY) {
        const std::uint32_t candidateY = static_cast<std::uint32_t>(y) - 1U + deltaY;
        if (candidateY >= 0x110U) {
            continue;
        }
        for (std::uint32_t deltaX = 0; deltaX != 3U; ++deltaX) {
            const std::uint32_t candidateX = static_cast<std::uint32_t>(x) - 1U + deltaX;
            if (candidateX >= 0x140U) {
                continue;
            }
            const std::uint32_t index = candidateY * 0x140U + candidateX;
            if (index < kUserMapDensityRecordCount) {
                SharedUserMapDensity::Inst().Increase(static_cast<int>(index), position, user);
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] index(%d, %d) -> index[%d] is invalid",
                //     L"world_server.cpp",
                //     0x40,
                //     x,
                //     y,
                //     index)
                L2_LOG_ERROR(
                    Log,
                    u"[world density] Cell (%d, %d) produced invalid density record %u while adding a user; update skipped.",
                    x, y, index
                );
            }
        }
    }
}

void CWorld::IncreaseUserMapDensity(const FVector& position, User& user) {
    L2_GUARD_FUNCTION();
    IncreaseUserMapDensity1(
        DensityCoordinate(position.x, 0x50000U), DensityCoordinate(position.y, 0x40000U), position, user
    );
}

void CWorld::DecreaseUserMapDensity1(int x, int y, const FVector& position, User& user) {
    L2_GUARD_FUNCTION();
    for (std::uint32_t deltaY = 0; deltaY != 3U; ++deltaY) {
        const std::uint32_t candidateY = static_cast<std::uint32_t>(y) - 1U + deltaY;
        if (candidateY >= 0x110U) {
            continue;
        }
        for (std::uint32_t deltaX = 0; deltaX != 3U; ++deltaX) {
            const std::uint32_t candidateX = static_cast<std::uint32_t>(x) - 1U + deltaX;
            if (candidateX >= 0x140U) {
                continue;
            }
            const std::uint32_t index = candidateY * 0x140U + candidateX;
            if (index < kUserMapDensityRecordCount) {
                SharedUserMapDensity::Inst().Decrease(static_cast<int>(index), position, user);
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] index(%d, %d) -> index[%d] is invalid",
                //     L"world_server.cpp",
                //     0x65,
                //     x,
                //     y,
                //     index)
                L2_LOG_ERROR(
                    Log,
                    u"[world density] Cell (%d, %d) produced invalid density record %u while removing a user; update "
                    u"skipped.",
                    x, y, index
                );
            }
        }
    }
}

void CWorld::DecreaseUserMapDensity(const FVector& position, User& user) {
    L2_GUARD_FUNCTION();
    DecreaseUserMapDensity1(
        DensityCoordinate(position.x, 0x50000U), DensityCoordinate(position.y, 0x40000U), position, user
    );
}

CGeoData* CWorld::GetGeo() {
    if (!m_geo) {
        // Log.Add(LOG_ERROR, "NULL m_geo at file[%s], line[%d]", __WFILE__, __LINE__)
        // Log.Add(LOG_ERROR,
        //     "[geodata] Geodata was requested but no geodata object is available. Returning null. Check geodata
        //     initialization.")
        L2_LOG_ERROR(
            Log,
            "[geodata] Geodata was requested but no geodata object is available; returning null. Check geodata "
            "initialization."
        );
    }
    return m_geo;
}

void CWorld::ReportDistribution(ReportType type) {
    // Current L2Server.exe 0x00c5f5f0, 333 bytes.
    L2_GUARD_FUNCTION();

    std::int32_t inactive = 0;
    switch (type) {
        case RPT_CREATURE: {
            // Log.Add(
            //     LOG_NORMAL,
            //     "==================== Creature Distribution Report ======================")
            L2_LOG_INFO(Log, "[world report] Creature distribution report started.");
            const std::int32_t npcCount = g_NPCMap.GetSize();
            const std::int32_t userCount = g_UserMap.GetSize();
            inactive = runtime::AddInt32Wrapping(npcCount, userCount);
            break;
        }
        case RPT_USER:
            // Log.Add(
            //     LOG_NORMAL,
            //     "==================== User Distribution Report ======================")
            L2_LOG_INFO(Log, "[world report] User distribution report started.");
            inactive = g_UserMap.GetSize();
            break;
        case RPT_NPC:
            // Log.Add(
            //     LOG_NORMAL,
            //     "==================== NPC Distribution Report ======================")
            L2_LOG_INFO(Log, "[world report] NPC distribution report started.");
            inactive = g_NPCMap.GetSize();
            break;
        case RPT_ITEM:
            // Log.Add(
            //     LOG_NORMAL,
            //     "==================== Item Distribution Report ======================")
            L2_LOG_INFO(Log, "[world report] Item distribution report started.");
            break;
    }

    // Log.Add(
    //     LOG_NORMAL,
    //     "Total : %d, (include inactivated = %d)",
    //     0,
    //     inactive)
    L2_LOG_INFO(Log, "[world report] Distribution totals: active=%d, including inactive=%d.", 0, inactive);
}

void CWorld::ReportUser() {
    // Current L2Server.exe 0x00c6569c, 926 bytes.
    L2_GUARD_FUNCTION();

    // Log.Add(LOG_NORMAL, "============  User Report ============")
    L2_LOG_INFO(Log, "[world report] Connected-user report started.");

    FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
        if (user == nullptr) {
            continue;
        }

        std::array<wchar_t, 256> characterName;
        if (user->m_data == nullptr) {
            characterName[0] = L'\0';
        } else {
            CopyReportName(characterName, Utf16ToWide(user->GetName()));
        }

        CUserSocket* const socket = user->GetSocket();
        if (socket == nullptr) {
            // Log.Add(
            //     LOG_NORMAL,
            //     L"User [%s] at null socket",
            //     characterName.data())
            L2_LOG_INFO(Log, u"[world report] Character '%s' has no client socket.", characterName.data());
            continue;
        }

        const std::int32_t mapY = WorldMapCoordinate(user->GetPos().y, 0x40000);
        const std::int32_t mapX = WorldMapCoordinate(user->GetPos().x, 0x50000);
        const std::int32_t z = runtime::Win64CvttDoubleToInt32(user->GetPos().z);
        const std::int32_t y = runtime::Win64CvttDoubleToInt32(user->GetPos().y);
        const std::int32_t x = runtime::Win64CvttDoubleToInt32(user->GetPos().x);
        const char* const address = ::inet_ntoa(socket->GetAddress());

        std::array<wchar_t, 15> accountNameStorage;
        CopyTargetAccountName(accountNameStorage, user->m_sAccountName);
        const wchar_t* const accountName = accountNameStorage.data();

        // Log.Add(
        //     LOG_NORMAL,
        //     L"User [%s] of account[%s] at socket[%d] from [%S] : "
        //     L"(%d, %d, %d) map[%d,%d]",
        //     characterName.data(),
        //     accountName,
        //     socket->m_Socket,
        //     address,
        //     x,
        //     y,
        //     z,
        //     mapX,
        //     mapY)
        L2_LOG_INFO(
            Log, u"[world report] Character '%s', account '%s': socket=%d, address=%S, position=(%d, %d, %d), map=(%d, %d).",
            characterName.data(), accountName, socket->m_Socket, address, x, y, z, mapX, mapY
        );
    });
}
