#pragma once

#include "Creature.h"
#include "Shared/src/IOC.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/Territory.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CItem;
class CPledge;
class CUserSocket;
class User;

enum AirShipTypeEnum : std::int32_t {
    ASTE_REGULAR = 0,
    ASTE_PLEDGE = 1,
    ASTE_INDIVIDUAL = 2,
    ASTE_GROUP = 3,
    ASTE_MAX = 4,
};

enum AirShipDespawnReason : std::int32_t {
    ASDR_NOCOST = 0,
    ASDR_LANDING = 1,
    ASDR_EMPTY = 2,
    ASDR_BUILDER = 3,
    ASDR_MAX = 4,
};

namespace DriverHelper {

enum AirShipDriverType : std::int32_t {
    ASDT_FAILED = 0,
    ASDT_SET = 1,
    ASDT_RESET = 2,
};

AirShipDriverType CanDrive(class CAirShip* airShip, class User* user, bool forced);

} // namespace DriverHelper

namespace BuilderCmdHelper {

bool CanUseBuilderCmd(User* user, int level);
void SummonAirShip(User* user, char16_t* command);
void BuyFreeAirShip(User* user, char16_t* command);
void DestroyRegularShip(User* user, char16_t* command);
void SailRegularShip(User* user, char16_t* command);
void GetOnAirShip(User* user, char16_t* command);
void GetOffAirShip(User* user, char16_t* command);
void SetDriver(User* user, char16_t* command);
void Destroy(User* user, char16_t* command);
void DestroyAll(User* user, char16_t* command);
void Ascent(User* user, char16_t* command);
void Descent(User* user, char16_t* command);
void Teleport(User* user, char16_t* command);
void TakeOffAirShip(User* user, char16_t* command);
void AirShipInfo(User* user, char16_t* command);
void LandingInAirport(User* user, char16_t* command);

} // namespace BuilderCmdHelper

// The spelling is donor-selected source vocabulary. The current handler
// closes the four actions and the exclusive maximum.
enum UserAirShipActionType : std::int32_t {
    UASAT_CONTROL_WHEEL = 0,
    UASAT_LEAVE_WHEEL = 1,
    UASAT_START_SHIP = 2,
    UASAT_LEAVE_SHIP = 3,
    UASAT_MAX = 4,
};


enum AirShipPathNodeType : std::int32_t {
    ASPNT_NONE = 0,
    ASPNT_START = 1,
    ASPNT_MOVE = 2,
    ASPNT_TELEPORT = 3,
    ASPNT_INTO_AIRPORT = 4,
};

struct AirShipPathNode {
    AirShipPathNode() : nodeType(ASPNT_NONE), dir(0) {}

    bool IsMovingNode() const { return nodeType == ASPNT_MOVE || nodeType == ASPNT_INTO_AIRPORT; }

    AirShipPathNodeType nodeType;
    ::FVector pos;
    std::int32_t dir;
};


class CAirport;
class CAirportForFree;
class CAirportForRegular;
class CAirportPlatform;
class CAirShipConstFree;
class CAirShipConstRegular;
class CParserForAirShip;
class CAirShipSteering;
class CContainer;
class CDBSocket;
class CShipController;
class CSharedAirShipData;

struct CollisionData {
    FVector position;
    double radius;
    double height;
};

class CAirShipConst {
public:
    explicit CAirShipConst(std::int32_t typeId);
    virtual ~CAirShipConst();

    virtual void OnLoad();
    virtual bool IsForRegular() const;
    virtual bool IsForFree() const;
    virtual CAirShipConstRegular* CastForRegular();
    virtual CAirShipConstFree* CastForFree();
    virtual int GetMaxFuel() const;
    virtual FVector GetSteeringPos() const;
    virtual FVector GetDriverPos() const;
    virtual int GetSteeringClassId() const;

    void SetType(AirShipTypeEnum type) { m_Type = type; }
    void SetSpeed(std::size_t index, double speed) { m_Speed[index] = speed; }
    void AddCollision(const FVector& position, double radius, double height);
    void SetMaxPassenger(std::int32_t maximum) { m_MaxPassenger = maximum; }

    std::int32_t m_TypeId;
    AirShipTypeEnum m_Type;
    double m_Speed[3];
    std::int32_t m_MaxPassenger;
    std::byte padding_2c[4];
    std::vector<CollisionData> m_RawCollisionList;
    std::vector<CollisionData> m_CollisionList;
};


class CAirShipConstRegular final : public CAirShipConst {
public:
    explicit CAirShipConstRegular(std::int32_t typeId);
    ~CAirShipConstRegular() override;

    void OnLoad() override;
    bool IsForRegular() const override;
    CAirShipConstRegular* CastForRegular() override;

    void AddPathNode(const AirShipPathNode& node);
    void SetReturnInfo(AirShipPathNodeType type);
    const AirShipPathNode& GetNode(std::int32_t index) const;
    std::int32_t GetNextIndex(std::int32_t index) const;
    const AirShipPathNode& GetNextNode(std::int32_t& index) const;
    bool IsEmptyPath() const;

private:
    friend class CParserForAirShip;

    std::vector<AirShipPathNode> m_Path;
};

struct AirShipSteeringInfo {
    FVector m_SteeringPos;
    FVector m_DriverPos;
    std::int32_t m_SteeringClassId;
    std::int32_t padding_34;
};

class CAirShipConstFree final : public CAirShipConst {
public:
    struct AirShipCostType {
        std::int32_t m_ItemClassId = 0;
        std::int32_t m_ItemCount = 0;
        std::int32_t m_ErrorMessageId = 0;
    };

    explicit CAirShipConstFree(std::int32_t typeId);
    ~CAirShipConstFree() override;

    bool IsForFree() const override;
    CAirShipConstFree* CastForFree() override;
    int GetMaxFuel() const override;
    FVector GetSteeringPos() const override;
    FVector GetDriverPos() const override;
    int GetSteeringClassId() const override;

    void SetFuel(std::int32_t maximum, std::int32_t consume);
    void SetSteeringPos(const FVector& position);
    void SetDriverPos(const FVector& position);
    void SetSteeringItem(const char16_t* name);
    void SetBuyCost(const char16_t* name, std::int32_t count);
    void SetSummonCost(const char16_t* name, std::int32_t count);
    bool CanPayBuyConst(User* user);
    bool PayBuyCost(User* user);
    bool CanPaySummonConst(User* user);
    bool PaySummonCost(User* user);

private:
    friend class CParserForAirShip;
    friend class CAirShipForFree;

    bool CanPayCost(User* user, const AirShipCostType& cost, std::int32_t messageId);
    bool PayCost(User* user, const AirShipCostType& cost, std::int32_t messageId);

    std::int32_t m_MaxFuel;
    std::int32_t m_FuelConsume;
    AirShipSteeringInfo m_SteeringInfo;
    enum : std::int32_t {
        ERROR_MSG_ON_SUMMON = 2327,
        ERROR_MSG_ON_BUY = 2724,
    };
    static_assert(ERROR_MSG_ON_SUMMON == 2327);
    static_assert(ERROR_MSG_ON_BUY == 2724);
    AirShipCostType m_BuyCost;
    AirShipCostType m_SummonCost;
};

class CAirport {
public:
    struct GetOffInfo {
        FVector m_Pos;
        std::int32_t m_SceneId = 0;
        std::int32_t padding_1c = 0;
    };

    explicit CAirport(std::int32_t id);
    virtual ~CAirport();

    virtual bool IsForRegular() const;
    virtual bool IsForFree() const;
    virtual CAirportForRegular* CastForRegular();
    virtual CAirportForFree* CastForFree();

    void AddGetOffInfo(CAirportPlatform* platform, const FVector& position, std::int32_t sceneId, std::int32_t line);
    const GetOffInfo& GetGetOffInfo(std::int32_t platformId);

protected:
    friend class CAirShip;
    friend class CAirShipForFree;
    friend class CAirShipForRegular;
    friend class CParserForAirShip;

    std::int32_t m_Id;
    std::int32_t padding_0c = 0;
    std::map<std::int32_t, GetOffInfo> m_GetOffInfoMap;
    std::int32_t m_Stopover = 0;
    std::int32_t padding_34 = 0;
};

class CAirportForRegular final : public CAirport {
public:
    explicit CAirportForRegular(std::int32_t id);
    ~CAirportForRegular() override;

    bool IsForRegular() const override;
    CAirportForRegular* CastForRegular() override;
    const FVector& GetAirShipPos() const;

private:
    friend class CParserForAirShip;

    FVector m_AirShipPos;
};

struct CAirportTeleport {
    explicit CAirportTeleport(std::int32_t id) : m_Id(id) {}

    std::int32_t m_Id;
    std::int32_t m_FuelConsume = 0;
    FVector m_Pos;
};

class CAirportPlatform {
public:
    explicit CAirportPlatform(std::int32_t id) :
        m_Id(id), m_SharedIndex(0) {}

    CAirportPlatform(const CAirportPlatform&) = default;
    ~CAirportPlatform() = default;

    std::int32_t Id() const { return m_Id; }

    void SetLandingPath(std::vector<FVector>& path) {
        m_LandingPath = path;
        path.clear();
    }

    void SetTakeOffPath(std::vector<FVector>& path) {
        m_TakeOffPath = path;
        path.clear();
    }

    void SetShipPos(FVector position) { m_AirShipPos = position; }

    void SetOccupied(bool occupied);
    bool IsOccupied() const;
    const FVector& GetLandingPath(std::int32_t index) const;
    const FVector& GetTakeOffPath(std::int32_t index) const;

    const FVector& GetShipPos() const { return m_AirShipPos; }

    void SetSharedIndex(std::int32_t index) { m_SharedIndex = index; }

    CAirportPlatform& operator=(const CAirportPlatform&) = default;

private:
    std::int32_t m_Id;
    std::vector<FVector> m_LandingPath;
    std::vector<FVector> m_TakeOffPath;
    FVector m_AirShipPos;
    std::int32_t m_SharedIndex;
};

class CAirportForFree final : public CAirport {
public:
    explicit CAirportForFree(std::int32_t id);
    ~CAirportForFree() override;

    bool IsForFree() const override;
    CAirportForFree* CastForFree() override;
    void AddPlatform(CAirportPlatform* platform);
    void SendTeleportList(User* user);
    CAirportTeleport* GetTeleport(std::int32_t id);
    CAirportPlatform* GetPlatform(std::int32_t id);

private:
    friend class CParserForAirShip;

    std::map<std::int32_t, CAirportTeleport*> m_TeleportMap;
    std::map<std::int32_t, CAirportPlatform*> m_PlatformMap;
};

enum AirShipAreaType : std::int32_t {
    ASAT_AIRPORT = 0,
    ASAT_SPEED = 1,
};

class CAirShipArea {
public:
    explicit CAirShipArea(std::int32_t id) : m_Id(id) {}

    bool Process(class CAirShip* airShip);

private:
    friend class CAirShipAreaDB;
    friend class CParserForAirShip;

    std::int32_t m_Id;
    std::int32_t m_MapX = 0;
    std::int32_t m_MapY = 0;
    AirShipAreaType m_Type = ASAT_AIRPORT;
    std::int32_t m_Option = 0;
    std::int32_t padding_14 = 0;
    CTerritory m_Territory;
};

class CAirShipAreaDB {
public:
    static CAirShipAreaDB& Inst();
    void AddArea(CAirShipArea* area);
    void CheckAirShip(class CAirShip* airShip);

private:
    std::array<std::vector<CAirShipArea*>, 20 * 17> m_AreaList;
};

class CAirShipManager {
public:
    struct FreeAirShipInfo {
        std::uint32_t m_DBID;
        bool m_Summoned;
        std::byte padding_05[3];
        std::int32_t m_Fuel;
        std::int32_t m_TypeId;
    };

    CAirShipManager();
    ~CAirShipManager();

    static CAirShipManager& Inst();
    void Load();
    void LoadAirShipFromDB();
    void OnLoadPledge(CPledge* pledge);
    bool AddConst(CAirShipConst* airShipConst);
    CAirport* GetAirport(std::int32_t id);
    bool BuyFreeAirShip(User* user, AirShipTypeEnum type, std::int32_t typeId, std::uint32_t requestId);
    bool SummonFreeAirShip(User* user, AirShipTypeEnum type, std::int32_t airportId, std::int32_t platformId);
    void OnBuyFreeAirShip(
        std::uint32_t userId,
        std::uint32_t requestId,
        std::uint32_t airShipId,
        std::int32_t airShipType,
        std::int32_t airShipTypeId,
        std::uint32_t ownerId,
        std::int32_t fuel
    );
    void OnLoadFreeAirShipAll(std::int32_t count, const unsigned char* packet);
    void DebugAirShipInfo(User* user, class CAirShip* airShip);
    bool CanGetOnAirShip(User* user, std::uint32_t airShipId);
    bool GetOnAirShip(User* user, std::uint32_t airShipId);
    void GetOnAirShip(User* user, class CAirShip* airShip);
    void DestroyAllAirShip();
    bool DestroyAllRegularShip();
    bool DestoryAirShip(std::uint32_t airShipId, AirShipDespawnReason reason);
    bool SaveFreeAirShipInfo(AirShipTypeEnum type, std::uint32_t ownerDbId, std::int32_t fuel);
    bool SailRegularAirShip();

private:
    friend class CParserForAirShip;

    struct SummonParams {
        SummonParams() : ownerDBID(0), airshipDBID(0), fuel(0) {}

        std::uint32_t ownerDBID;
        std::uint32_t airshipDBID;
        std::int32_t fuel;
    };


    bool _OnBuyAirShip(
        User* user,
        std::uint32_t airShipId,
        std::int32_t airShipType,
        std::int32_t airShipTypeId,
        std::uint32_t ownerId,
        std::int32_t fuel
    );
    bool _OnLoadAirShip(
        std::uint32_t airShipId,
        std::int32_t airShipType,
        std::int32_t airShipTypeId,
        std::uint32_t ownerId,
        std::int32_t fuel
    );
    class CAirShip* _CreateAirShip(
        CAirShipConst* airShipConst, std::uint32_t dbId, std::uint32_t ownerDbId, std::int32_t fuel
    );
    void OnAirShipSummoned(std::uint32_t ownerDbId);
    CAirShipConst* _CanSummonFreeAirShip(
        User* user, AirShipTypeEnum type, std::int32_t airportId, std::int32_t platformId, SummonParams& parameters
    );
    CAirShipConst* _CanBuyFreeAirShip(User* user, AirShipTypeEnum type, std::int32_t typeId);
    bool _SavePledgeAirShipInfo(std::uint32_t ownerDbId, std::int32_t fuel);
    bool _OnDespawnFreeAirShip(std::uint32_t ownerDbId);

    CYieldLock m_AirShipLock;
    std::map<std::int32_t, CAirShipConst*> m_AirShipConstMap;
    std::map<std::int32_t, CAirport*> m_AirportMap;
    std::set<std::uint32_t> m_AirShipInstList;
    std::map<std::uint32_t, FreeAirShipInfo> m_FreeAirShipDBMap;
};


class CAirShipHandler {
public:
    static bool BuilderCmdHandler(CUserSocket* socket, User* user, char16_t* command);
    static bool UserCmdHandler(User* user, UserAirShipActionType action);
    static bool DBPacketHandler(CDBSocket* socket, const unsigned char* packet);
};


class CAirShip : public CAbstractCarrier {
public:
    enum AirShipStatusType : std::int32_t {
        ACTION_FREEZE = 0,
        ACTION_OK = 1,
    };

    struct CAirShipKey {
        std::int32_t m_x;
        std::int32_t m_y;

        bool operator<(const CAirShipKey& other) const noexcept {
            const auto makeKey = [](std::int32_t x, std::int32_t y) {
                const std::uint32_t bias = static_cast<std::uint32_t>(y > 0 ? 10000 : -10000);
                return static_cast<std::int32_t>(static_cast<std::uint32_t>(x) + bias);
            };
            return makeKey(m_x, m_y) < makeKey(other.m_x, other.m_y);
        }
    };

    enum AutoSailingInfoType : std::int32_t {
        ASIT_NONE = 0,
        ASIT_LANDING = 1,
        ASIT_TAKEOFF = 2,
        ASIT_REGULAR = 3,
    };

    struct AutoSailingInfo {
        AutoSailingInfoType type;
        std::int32_t nodeIndex;
        std::int32_t stopoverTick;
        std::byte padding_0c[4];
        CAirportPlatform* pPlatform;
        CAirportForFree* pAirport;
        std::int32_t teleportIndex;
        std::byte padding_24[4];
    };

    explicit CAirShip(CAirShipConst* airShipConst);
    CAirShip(const CAirShip& other);
    ~CAirShip() override;

    L2_DECLARE_OBJECT_REGISTRATION(CAirShip)

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    void TimerExpired(int id) override;
    FAngle GetDir() const override;
    int GetIndex() const override;
    bool IsAirShip() override;
    CAirShip* CastAirShip() override;
    bool IsEnemyTo(CCreature* creature) override;
    void InSight(CObject* object, bool flag) override;
    void OutOfSight(CObject* object, bool flag) override;
    unsigned int GetDBID() const override;
    void SetDBID(unsigned int dbId) override;
    FVector GetPos() const override;
    void SetDir(const FAngle& direction) override;
    bool SetMovingIntoCarrier(CMoveRelativeAction* action) override;

    virtual void OnCreate(unsigned int dbId, unsigned int ownerDbId, int typeId);
    virtual bool EnterWorld(int x, int y) = 0;
    virtual bool CanAction();
    virtual void SendDebugInfo(User* user);
    virtual const char16_t* GetOwnerName();
    virtual bool CanRefuel(int fuel);
    virtual bool Refuel(int fuel, User* user);
    virtual void TakeOff(int airportId, User* user);
    virtual void SendTeleportList(User* user);
    virtual bool IsLanded();
    virtual bool CanBeDestroyed();

    void SeeDropItem(CItem* item, CCreature* dropper);
    void Delete(AirShipDespawnReason reason);
    void LeaveWorld();
    bool CanGetOn(User* user, bool forced);
    bool GetOn(User* user, bool forced);
    bool HavePassengers();
    void KickoutAllPassengers(FVector position);
    bool IsDriver(User* user) const;
    User* GetDriver();
    bool SetDriver(User* user, bool forced);
    bool GetOff(User* user, bool forced);
    void StopMove(bool force);
    void Ascent();
    void Descent();
    void MoveToLocation(int x, int y, bool broadcast);
    void MoveToLocation(FVector position);
    void TeleportToLocation(int x, int y, int z, unsigned int inZoneId, int direction);
    void OnMoveToLocationPacket(User* user, const unsigned char* packet);
    FVector GetDetailLocation() const;
    FVector GetTargetPos(CCreature* creature, CSkillInfo* skillInfo, double time);
    void FillOutputVectorWithParts(
        std::vector<CSafePointer2<CStaticObject>>& targets, std::vector<double>& distances, FVector& position
    );
    void SendAirShipInfo(CUserSocket* socket);
    void BroadCastAirShipInfo();
    void OnCannotMoveAnymorePacket(int x, int y, int z);
    void OnValidateLocation(FVector position);
    void OnStartRotating(int angle, int side);
    void OnFinishRotating(int angle);
    void OnAppearingPacket(User* user);
    void LoadCannon(CNPC* npc);


protected:
    virtual void OnDelete(AirShipDespawnReason reason);
    virtual void OnEnterWorld();
    virtual void OnLeaveWorld();
    virtual void OnTimerExpired();
    virtual void AutoSail() = 0;
    virtual void _GetOff(User* user) = 0;
    virtual int GetFuel();

    void _EnterWorld(const FVector& position);
    void OnLanding(int airportId, int platformId);
    void OnTakeOff(CAirport* airport, CAirportPlatform* platform);
    void OnSummon(CAirport* airport, CAirportPlatform* platform);
    void Landing(int airportId);
    void ProcessLanding(int airportId);
    void ProcessLanding(FVector position);
    void UnloadAllParts();
    void UnloadAllCannons();

private:
    void GetClosestIntersectionPoint(
        FVector& closestPoint,
        CAirShip* otherAirShip,
        CSkillInfo* skillInfo,
        CTerritory& territory,
        const FVector& source,
        const FVector& destination
    );

    friend class CAirShipReleaseVerifier;
    friend class CAirShipManager;
    friend class CShipAction;
    friend class CShipController;
    friend class CShipMoveAction;
    friend class CAirShipArea;
    friend class CContainer;
    friend class CWorld;
    friend class User;
    friend void BuilderCmdHelper::LandingInAirport(User* user, char16_t* command);
    friend DriverHelper::AirShipDriverType DriverHelper::CanDrive(CAirShip* airShip, User* user, bool forced);

public:
    CYieldLock* m_lock;
    FBox3 m_tlmBox3;
    CSharedAirShipData* m_data;

protected:
    ::std::set<CSmartID> m_AirShipParts;
    AirShipStatusType m_statusType;
    std::byte padding_a4[4];
    ::std::map<CAirShipKey, CSmartID> m_AirShipCannons;
    FVector m_vMovingDirection;
    FVector m_vMovingDest;
    std::uint32_t m_nLastTick;
    std::uint32_t m_nMoveTick;
    bool m_bMoving;
    std::byte padding_101[7];
    CShipController* m_pController;
    CContainer* m_pContainer;
    CAirShipConst* m_const;
    CAirShipSteering* m_Steering;
    AutoSailingInfo m_AutoInfo;
    std::int32_t m_Speed;
    std::int32_t m_SpeedAreaFactor;
    FVector m_SavingPos;
    std::uint32_t m_TickCount;
    std::int32_t m_PassengerCount;

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CAirShip)
};


class CAirShipForFree final : public CAirShip {
public:
    explicit CAirShipForFree(CAirShipConst* airShipConst);
    CAirShipForFree(const CAirShipForFree& other);
    ~CAirShipForFree() override = default;

    L2_DECLARE_OBJECT_REGISTRATION(CAirShipForFree)

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()
    CObject* Copy() override;

    void OnCreate(unsigned int dbId, unsigned int ownerDbId, int typeId) override;
    bool EnterWorld(int airportId, int platformId) override;
    bool CanAction() override;
    const char16_t* GetOwnerName() override;
    bool CanRefuel(int fuel) override;
    bool Refuel(int fuel, User* user) override;
    void TakeOff(int airportId, User* user) override;
    void SendTeleportList(User* user) override;
    bool IsLanded() override;
    bool CanBeDestroyed() override;

protected:
    void OnDelete(AirShipDespawnReason reason) override;
    void OnEnterWorld() override;
    void OnLeaveWorld() override;
    void OnTimerExpired() override;
    void AutoSail() override;
    void _GetOff(User* user) override;
    int GetFuel() override;

private:
    void AutoSail_Landing();
    void AutoSail_Takeoff();
    void ConsumeFuel(int fuel);
    void SaveInfo();

    std::int32_t m_Fuel;
    std::uint32_t m_FuelTick;
    std::uint32_t m_EmptyTick;
    std::int32_t m_SomethingChanged;

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CAirShipForFree)
};


class CAirShipForRegular final : public CAirShip {
public:
    struct StopDebugInfo {
        std::int32_t tick;
        std::byte padding_04[4];
        FVector position;
    };

    explicit CAirShipForRegular(CAirShipConst* airShipConst);
    CAirShipForRegular(const CAirShipForRegular& other);
    ~CAirShipForRegular() override = default;

    L2_DECLARE_OBJECT_REGISTRATION(CAirShipForRegular)

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()
    CObject* Copy() override;

    bool EnterWorld(int x, int y) override;
    void SendDebugInfo(User* user) override;
    bool IsLanded() override;
    bool CanBeDestroyed() override;

protected:
    void OnEnterWorld() override;
    void AutoSail() override;
    void _GetOff(User* user) override;

private:
    void BroadcastMovePacket();
    void OnNode(const AirShipPathNode& node);
    void OnNodeChanged();

    StopDebugInfo m_StopDebugInfo;

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CAirShipForRegular)
};


// Exact current delayed-release verifier from AirShip.cpp.
class CAirShipReleaseVerifier : public ::CIOObject {
public:
    explicit CAirShipReleaseVerifier(CAirShip* airShip);
    ~CAirShipReleaseVerifier() override = default;

    void TimerExpired(int id) override;


private:
    CAirShip* m_pAirShip;
};
