#pragma once

#include "CWorldObject.h"
#include "Shared/src/IOC.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

class CContainer;
class CCreature;
class CItem;
class CUserSocket;
class CVehicleController;
class CVehicleStationInfo;
template <typename TObject>
class CSPointer;

enum VehicleActionType : std::int32_t {
    VATE_MESSAGE = 0,
    VATE_DEPARTURE = 1,
    VATE_CHECK_TICKET = 2,
    VATE_WAIT = 3,
    VATE_SET_KICK_POS = 4,
    VATE_PLAY_SOUND = 5,
    VATE_RESERVE_STATION = 6,
    VATE_RELEASE_STATION = 7,
};

struct VehicleActionInfo {
    VehicleActionType m_eType;
    std::int32_t m_nSpeakerID;
    ::std::vector<int> m_vMessageID;
    std::int32_t m_nWaitingMessageID;
    std::int32_t m_alignment2c;
    char16_t* m_sFileName;
    std::int32_t m_nStationID;
    std::int32_t m_nTicketID;
    FVector m_vBanningPos;
    std::int32_t m_nMovingSpeed;
    std::int32_t m_nRotatingSpeed;
    std::int32_t m_nNextActionTime;
    std::int32_t m_alignment64;
};


class CVehicleStationInfo final : public MemoryObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CVehicleStationInfo)

    CVehicleStationInfo();
    ~CVehicleStationInfo() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    void AddActionInfo(VehicleActionInfo* action);

    FVector m_vPoint;
    ::std::vector<VehicleActionInfo*> m_vAction;
    std::int32_t m_nStationID;
    std::int32_t m_alignment54;
};


class CVehicleController final : public CIOObject {
public:
    enum VehicleActionType : std::int32_t {
        REPLAY_ACTION = 1,
    };

    L2_DECLARE_OBJECT_REGISTRATION(CVehicleController)

    CVehicleController();
    ~CVehicleController() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void TimerExpired(int id) override;

    void AddStation(CVehicleStationInfo* station);
    void RemoveStations();
    void SetVehicle(class CVehicle* vehicle);
    class CVehicle* GetVehicle() const noexcept;
    void SetCaptainId(int captainId, FVector relativePosition);
    void SetBroadcastPoint(FVector first, FVector second);
    int GetNextStation();
    void Start();
    bool ReadyToDepartureAction();

protected:
    FVector m_vCaptainRelativePos;
    class CVehicle* m_pVehicle;
    CCreature* m_pCaptain;
    std::int32_t m_nCaptainID;
    std::int32_t m_nCurrentStation;
    std::int32_t m_nCurrentAction;
    std::int32_t m_alignment4c;
    ::std::vector<CVehicleStationInfo*> m_vStation;
    ::std::vector<FVector> m_vBroadcastPoint;
    std::int32_t m_nLastErrorCode;
    CYieldLock m_ControllerLock;
};


class CVehicle : public CAbstractCarrier {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CVehicle)

    CVehicle();
    CVehicle(const CVehicle& other);
    ~CVehicle() override = default;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    static CVehicle* GetObjectW(std::uint32_t sid);
    static CVehicle* FindObject(std::uint32_t sid);
    static CSPointer<CVehicle> FindObjectSP(std::uint32_t sid);

    // Current vtable +0x138 targets the 0x006ed9c4 identity thunk.
    CVehicle* CastVehicle() override;
    // Current vtable +0x118 targets the 0x004c0008 true thunk.
    bool IsVehicle() override;
    bool IsEnemyTo(CCreature* creature) override;
    bool IsAlive() override;
    void InSight(CObject* object, bool flag) override;
    void OutOfSight(CObject* object, bool flag) override;
    CObject* Copy() override;
    FVector ConvAbsToRel(FVector position) const override;
    FVector ConvRelToAbs(FVector position) const override;
    FVector GetPos() const override;
    FAngle GetDir() const override;
    int GetIndex() const override;
    bool SetMovingIntoCarrier(CMoveRelativeAction* action) override;
    void TimerExpired(int id) override;

    virtual void SetPos(const FVector& position);
    virtual void StartDrive();
    virtual void StopDrive();

    void CalcDir();
    FVector GetAdjustPos(double distance, FVector position);
    int GetNewDirection(const FVector& position, int direction);
    bool HasArrived();
    void InformVehicleStart(bool start);
    void KickIllegal(int index, FVector position);
    void KickTransformer();
    void OnStart();
    void ProcessStopDrive();
    bool QuickDeparture();
    void SeeDropItem(CItem* item, CCreature* creature);
    void SendVehicleInfo(CUserSocket* socket);

    void SetController(CVehicleController* controller) noexcept { m_pController = controller; }
    CVehicleController* GetController() const noexcept { return m_pController; }
    CContainer* GetContainer() const noexcept { return m_pContainer; }
    void SetOrigin(FVector position) noexcept { m_Origin = position; }
    FVector GetOrigin() const noexcept { return m_Origin; }
    void SetDestination(FVector position) noexcept { m_Destination = position; }
    FVector GetDestination() const noexcept { return m_Destination; }
    void SetMoveSpeed(int speed) noexcept { m_MoveSpeed = speed; }
    int GetMoveSpeed() const noexcept { return m_MoveSpeed; }
    void SetRotationSpeed(int speed) noexcept { m_RotationSpeed = speed; }
    bool IsStop() const noexcept { return m_MoveSpeed == 0; }


protected:
    friend class CContainer;
    friend class CWorld;
    friend class CVehicleController;
    friend class CVehicleDB;

    CVehicleController* m_pController;
    CContainer* m_pContainer;
    FVector m_Pos;
    FVector m_Origin;
    FVector m_Destination;
    std::int32_t m_MoveSpeed;
    std::int32_t m_Direction;
    std::int32_t m_RotationSpeed;
    bool m_bIsDriving;
    bool m_bWaitingStart;
    bool m_bWaitingStop;
    std::byte padding_bf;

public:
    FVector m_vKickPos;
    char16_t m_Name[128];
};


class CVehicleDB {
public:
    CVehicleDB();

    void AddVehicle(const char16_t* name, CVehicle* vehicle);
    bool ReserveStation(int stationId);
    bool ReleaseStation(int stationId);
    void ClearStation();
    void Load();
    CVehicle* FindVehicle(int vehicleId);
    void StartDrive();
    void QuickDeparture();
    void EraseAllVehicle();
    void Reload();

protected:
    ::std::map<int, CVehicle*> m_VehicleIDMap;
    CYieldLock m_VehicleMapSect;
    std::int32_t m_nStationReserved[101];
};


extern CVehicleDB g_VehicleDB;
