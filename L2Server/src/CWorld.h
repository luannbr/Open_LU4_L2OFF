#pragma once

#include "Shared/src/BaseStructure.h"
#include "Shared/src/Obj.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <cstddef>
#include <cstdint>

class CGeoData;
namespace l2geo {
class CWorld;
} // namespace l2geo

class CContainer;
class CCreature;
class CCastle;
class CDoor;
class CItem;
class CAirShip;
class CShuttle;
class CStaticObject;
class CWorldObject;
class CVehicle;
class User;
enum ReportType : std::int32_t;
enum SlotType : std::int32_t;
struct AttackTargetDataType;
struct ChargeData;

// Current L2Server world owner. This is a global product type:
// l2geo::CWorld is the distinct main 0x28 Shared/L2NPC object.
class CWorld : public MemoryObject {
public:
    static constexpr std::size_t kSerializerQueueSize = 4194304;
    using SerializerType = ::CExecutionSerializer<CWorld, kSerializerQueueSize>;

    explicit CWorld(bool isTestWorld);
    ~CWorld() override;

    CWorld(const CWorld&) = delete;
    CWorld& operator=(const CWorld&) = delete;

    static CWorld& Inst();
    static void ReportDistribution(ReportType type);
    static void ReportUser();
    bool GodsVoice(wchar_t* message);
    void InitGeo();
    bool IsValidPosition(int x, int y);
    bool StaticObjectAppear(CStaticObject* object, ::FVector position);
    bool StaticObjectDisappear(CStaticObject* object);
    void LoadStaticObject(CStaticObject* object, CAirShip* airShip);
    void UnLoadStaticObject(CStaticObject* object, CAirShip* airShip);
    static void IncreaseUserMapDensity(const ::FVector& position, User& user);
    static void DecreaseUserMapDensity(const ::FVector& position, User& user);
    void ContainerMove(CCreature& creature, CContainer* destination, bool force);
    bool MoveTo(CCreature* creature, ::FVector position, bool force);
    bool MoveToZone(CCreature* creature, const ::FVector& position, unsigned int inZoneId);
    bool EnterWorld(CCreature* creature, ::FVector position, unsigned int inZoneId, bool activate, bool suppressQueue);
    bool MoveToRelatively(CCreature* creature, CWorldObject* relativeObject, ::FVector position);
    bool GetOnAirShip(CCreature* creature, CAirShip* airShip);
    bool OnAppearingIntoAirShip(CAirShip* airShip, User* user);
    bool GetOnVehicle(CCreature* creature, CVehicle* vehicle, ::FVector position);
    bool GetOffVehicle(CCreature* creature, CVehicle* vehicle, ::FVector position, bool force);
    bool GetOffAirShip(CCreature* creature, CAirShip* airShip, ::FVector position);
    bool GetOnShuttle(CCreature* creature, CShuttle* shuttle, ::FVector position);
    bool GetOffShuttle(CCreature* creature, CShuttle* shuttle, ::FVector position, bool force);
    bool EnterWorld(CShuttle* shuttle, ::FVector position);
    bool LeaveWorld(CShuttle* shuttle);
    bool EnterWorld(CAirShip* airShip, ::FVector position);
    bool LeaveWorld(CAirShip* airShip);
    bool LeaveWorld(CCreature* creature, ::FVector position, bool force);
    void DoorStatusUpdate(CDoor* door);
    void DoorOpenClose(CDoor* door, bool opened, bool forced);
    bool ShuttleKickFromDoor(CDoor* door, ::FVector banPosition);
    bool ShuttleMove(CShuttle* shuttle, ::FVector position);
    bool AirShipTeleport(CAirShip* airShip, ::FVector position, unsigned int inZoneId, int direction);
    bool AirShipMove(CAirShip* airShip, ::FVector position);
    void VehicleEnterWorld(CVehicle* vehicle, ::FVector position);
    void VehicleLeaveWorld(CVehicle* vehicle);
    void VehicleMove(CVehicle* vehicle, ::FVector position);
    void VehicleKickIllegal(CVehicle* vehicle, int ticketClassId, ::FVector banPosition);
    void VehicleInformVehicleStart(CVehicle* vehicle, bool start);
    void VehicleKickTransformer(CVehicle* vehicle);
    bool CanGo(
        const ::FVector& source,
        ::FVector& destination,
        ::FVector& outputPosition,
        bool& distanceExhausted,
        double& travelledDistance,
        int inZoneId,
        bool floating
    );
    bool CanGo(const ::FVector& source, ::FVector& destination, int inZoneId, bool floating);
    bool FindAcceptableDestZPos(const ::FVector& source, ::FVector& destination, bool floating);
    bool GetValidJumpPos(
        const ::FVector& source, ::FVector& destination, ChargeData& chargeData, double distance, int inZoneId, bool floating
    );
    bool Attack(
        int actorId,
        int weaponClassId,
        AttackTargetDataType& targetData,
        AttackType attackType,
        SlotType slotType,
        bool notifyNeighbors
    );
    void Spell(CCreature& creature, CWorldObject& target, int skillId, int skillLevel, int skillSubLevel);
    bool GetValidPositionAroundPos(
        const CCreature& creature,
        const int radius,
        const double collisionRadius,
        const ::FVector& source,
        ::FVector& destination
    );
    bool CanSummonNpc(const ::FVector& position, User* user);
    bool PutItem(CItem* item, ::FVector position, std::uint32_t inZoneId, CCreature* dropper);
    bool PutItem(CItem* item, ::FVector position, CCreature* dropper);
    bool TryPickItem(CItem* item, CCreature* creature, bool force);
    bool PickItem(CItem* item, CCreature* creature, bool force);
    bool PickItemCond(CItem* item, CCreature* creature);
    void PickAndDeleteItem(CItem* item);
    void DeleteInZoneItem(CItem* item);

    // The fifth and only CWorld-owned virtual slot in the current vtable.
    virtual CGeoData* GetGeo();


private:
    friend class CCastle;

    L2_ASYNC_DECLARE_FIELDS_2(__async_arg_AsyncStaticObjectAppear, CStaticObject*, ::FVector);

    L2_ASYNC_DECLARE_FIELDS_1(__async_arg_AsyncStaticObjectDisappear, CStaticObject*);

    L2_ASYNC_DECLARE_FIELDS_2(__async_arg_AsyncLoadStaticObject, CStaticObject*, CAirShip*);

    L2_ASYNC_DECLARE_FIELDS_2(__async_arg_AsyncUnLoadStaticObject, CStaticObject*, CAirShip*);

    L2_ASYNC_DECLARE_FIELDS_3(__async_arg_AsyncMove, CCreature*, CContainer*, bool);

    L2_ASYNC_DECLARE_FIELDS_3(__async_arg_AsyncLeaveWorld, CCreature*, ::FVector, ::FVector);

    L2_ASYNC_DECLARE_FIELDS_4(__async_arg_AsyncEnterWorld, CCreature*, ::FVector, unsigned int, bool);

    L2_ASYNC_DECLARE_FIELDS_3(__async_arg_AsyncMoveToZone, CCreature*, ::FVector, unsigned int);

    L2_ASYNC_DECLARE_FIELDS_3(__async_arg_AsyncGetOnVehicle, CCreature*, CVehicle*, ::FVector);

    L2_ASYNC_DECLARE_FIELDS_3(__async_arg_AsyncGetOffVehicle, CCreature*, CVehicle*, ::FVector);

    L2_ASYNC_DECLARE_FIELDS_3(__async_arg_AsyncGetOffAirShip, CCreature*, CAirShip*, ::FVector);

    L2_ASYNC_DECLARE_FIELDS_2(__async_arg_AsyncGetOnAirShip, CCreature*, CAirShip*);

    L2_ASYNC_DECLARE_FIELDS_2(__async_arg_AsyncAirShipEnterWorld, CAirShip*, ::FVector);

    L2_ASYNC_DECLARE_FIELDS_1(__async_arg_AsyncAirShipLeaveWorld, CAirShip*);

    L2_ASYNC_DECLARE_FIELDS_4(__async_arg_AsyncAirShipTeleport, CAirShip*, ::FVector, unsigned int, int);

    L2_ASYNC_DECLARE_FIELDS_2(__async_arg_AsyncAppearIntoAirShip, User*, CAirShip*);

    L2_ASYNC_DECLARE_FIELDS_3(__async_arg_AsyncGetOnShuttle, CCreature*, CShuttle*, ::FVector);

    L2_ASYNC_DECLARE_FIELDS_3(__async_arg_AsyncGetOffShuttle, CCreature*, CShuttle*, ::FVector);

    L2_ASYNC_DECLARE_FIELDS_4(__async_arg_AsyncPutItem, CCreature*, CItem*, ::FVector, CContainer*);

    L2_ASYNC_DECLARE_FIELDS_2(__async_arg_AsyncShuttleEnterWorld, CShuttle*, ::FVector);

    L2_ASYNC_DECLARE_FIELDS_1(__async_arg_AsyncShuttleLeaveWorld, CShuttle*);

    L2_ASYNC_DECLARE_FIELDS_2(__async_arg_AsyncShuttleMove, CShuttle*, ::FVector);

    L2_ASYNC_DECLARE_FIELDS_2(__async_arg_AsyncVehicleEnterWorld, CVehicle*, ::FVector);

    L2_ASYNC_DECLARE_FIELDS_1(__async_arg_AsyncVehicleLeaveWorld, CVehicle*);

    L2_ASYNC_DECLARE_FIELDS_2(__async_arg_AsyncVehicleMove, CVehicle*, ::FVector);

    L2_ASYNC_DECLARE_FIELDS_3(__async_arg_AsyncVehicleKickIllegal, CVehicle*, int, ::FVector);

    L2_ASYNC_DECLARE_FIELDS_2(__async_arg_AsyncVehicleInformVehicleStart, CVehicle*, bool);

    L2_ASYNC_DECLARE_FIELDS_1(__async_arg_AsyncVehicleKickTransformer, CVehicle*);

    L2_ASYNC_DECLARE_FIELDS_2(__async_arg_AsyncAirShipMove, CAirShip*, CContainer*);

    L2_ASYNC_DECLARE_FIELDS_1(__async_arg_AsyncDoorStatusUpdate, CDoor*);

    L2_ASYNC_DECLARE_FIELDS_3(__async_arg_AsyncDoorOpenClose, CDoor*, bool, bool);

    L2_ASYNC_DECLARE_FIELDS_2(__async_arg_AsyncShuttleKickFromDoor, CDoor*, ::FVector);

    L2_ASYNC_DECLARE_FIELDS_1(__async_arg_AsyncPickAndDeleteItem, CItem*);

    L2_ASYNC_DECLARE_FIELDS_1(__async_arg_AsyncPickItem, CItem*);

    L2_ASYNC_DECLARE_FIELDS_2(__async_arg_AsyncAddItemToInventory, CItem*, CCreature*);

    L2_ASYNC_DECLARE_FIELDS_3(__async_arg_AsyncAddItemToInventoryWithparams, CCreature*, int, std::int64_t);

    L2_ASYNC_DECLARE_FIELDS_1(__async_arg_AsyncDeleteInZoneItem, CItem*);


    void AsyncStaticObjectAppear(CStaticObject* object, ::FVector position);
    void __async_AsyncStaticObjectAppear(CExecutionArgumentPad* argument);
    void __async_AsyncStaticObjectDisappear(CExecutionArgumentPad* argument);
    void __async_AsyncLoadStaticObject(CExecutionArgumentPad* argument);
    void __async_AsyncUnLoadStaticObject(CExecutionArgumentPad* argument);
    void __async_AsyncMove(CExecutionArgumentPad* argument);
    void AsyncEnterWorld(CCreature* creature, ::FVector position, unsigned int inZoneId, bool activate);
    void __async_AsyncEnterWorld(CExecutionArgumentPad* argument);
    void AsyncEnterWorldInternal(CCreature* creature, const ::FVector& position, unsigned int inZoneId, bool activate);
    void AsyncMoveToZone(CCreature* creature, ::FVector position, unsigned int inZoneId);
    void __async_AsyncMoveToZone(CExecutionArgumentPad* argument);
    void AsyncLeaveWorld(CCreature* creature, ::FVector source, ::FVector destination);
    void __async_AsyncLeaveWorld(CExecutionArgumentPad* argument);
    void AsyncLeaveWorldInternal(CCreature* creature, const ::FVector& source, const ::FVector& destination);
    void AsyncGetOnVehicle(CCreature* creature, CVehicle* vehicle, ::FVector position);
    void AsyncGetOffVehicle(CCreature* creature, CVehicle* vehicle, ::FVector position);
    void AsyncGetOffAirShip(CCreature* creature, CAirShip* airShip, ::FVector position);
    void AsyncGetOnAirShip(CCreature* creature, CAirShip* airShip);
    void AsyncAirShipEnterWorld(CAirShip* airShip, ::FVector position);
    void __async_AsyncAirShipEnterWorld(CExecutionArgumentPad* argument);
    void __async_AsyncAirShipLeaveWorld(CExecutionArgumentPad* argument);
    void AsyncAirShipTeleport(CAirShip* airShip, ::FVector position, unsigned int inZoneId, int direction);
    void __async_AsyncAirShipTeleport(CExecutionArgumentPad* argument);
    void AsyncAppearIntoAirShip(User* user, CAirShip* airShip);
    void AsyncGetOnShuttle(CCreature* creature, CShuttle* shuttle, ::FVector position);
    void AsyncGetOffShuttle(CCreature* creature, CShuttle* shuttle, ::FVector position);
    void __async_AsyncGetOnVehicle(CExecutionArgumentPad* argument);
    void __async_AsyncGetOffVehicle(CExecutionArgumentPad* argument);
    void __async_AsyncGetOffAirShip(CExecutionArgumentPad* argument);
    void __async_AsyncGetOnAirShip(CExecutionArgumentPad* argument);
    void __async_AsyncAppearIntoAirShip(CExecutionArgumentPad* argument);
    void __async_AsyncGetOnShuttle(CExecutionArgumentPad* argument);
    void __async_AsyncGetOffShuttle(CExecutionArgumentPad* argument);
    void AsyncPutItem(CCreature* dropper, CItem* item, ::FVector position, CContainer* container);
    void __async_AsyncPutItem(CExecutionArgumentPad* argument);
    void __async_AsyncPickAndDeleteItem(CExecutionArgumentPad* argument);
    bool PickAndEquipItemFailed(CItem* item);
    bool PickAndEquipItem(CItem* item, CCreature* creature);
    void PickAndUseItem(CItem* item, CCreature* creature);

public:
    void DoAsyncPickItem(CItem* item);

private:
    void __async_AsyncPickItem(CExecutionArgumentPad* argument);

public:
    void DoAsyncAddItemToInventory(CItem* item, CCreature* creature);
    void DoAsyncAddItemToInventory(int itemClassId, std::int64_t count, CCreature* creature);

private:
    void __async_AsyncAddItemToInventory(CExecutionArgumentPad* argument);
    void __async_AsyncAddItemToInventoryWithparams(CExecutionArgumentPad* argument);
    void __async_AsyncDeleteInZoneItem(CExecutionArgumentPad* argument);
    void __async_AsyncShuttleEnterWorld(CExecutionArgumentPad* argument);
    void __async_AsyncShuttleLeaveWorld(CExecutionArgumentPad* argument);
    void __async_AsyncShuttleMove(CExecutionArgumentPad* argument);
    void __async_AsyncVehicleEnterWorld(CExecutionArgumentPad* argument);
    void __async_AsyncVehicleLeaveWorld(CExecutionArgumentPad* argument);
    void __async_AsyncVehicleMove(CExecutionArgumentPad* argument);
    void __async_AsyncVehicleKickIllegal(CExecutionArgumentPad* argument);
    void __async_AsyncVehicleInformVehicleStart(CExecutionArgumentPad* argument);
    void __async_AsyncVehicleKickTransformer(CExecutionArgumentPad* argument);
    void __async_AsyncAirShipMove(CExecutionArgumentPad* argument);
    void __async_AsyncDoorStatusUpdate(CExecutionArgumentPad* argument);
    void __async_AsyncDoorOpenClose(CExecutionArgumentPad* argument);
    void __async_AsyncShuttleKickFromDoor(CExecutionArgumentPad* argument);
    void OnGetOnOffAirShip(CCreature* creature, ::FVector position);

    static void IncreaseUserMapDensity1(int x, int y, const ::FVector& position, User& user);
    static void DecreaseUserMapDensity1(int x, int y, const ::FVector& position, User& user);

    SerializerType Serializer;
    std::int64_t m_AdenaDropByNpcs;
    std::int64_t m_AdenaDropByNpcsOld;
    bool m_IsTestWorld;
    std::byte padding_16000069[7];
    CGeoData* m_geo;
};
