#pragma once

#include "CWorldObject.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/IOC.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

class CDoor;
class CContainer;
class CItem;
class CParserForShuttle;
class CUserSocket;
class User;

namespace BuilderCmd {
bool set_shuttle(CUserSocket*, User*, char16_t*);
bool shuttle_info(CUserSocket*, User*, char16_t*);
bool shuttle_add_outer_door(CUserSocket*, User*, char16_t*);
} // namespace BuilderCmd


struct InnerDoor {
    int id; // +0x00
    ::FVector open_pos; // +0x08
    ::FVector close_pos; // +0x20
    ::FVector safe_pos; // +0x38
};


class CShuttleConst {
public:
    double GetRadius() const { return m_dRadius; }

    void AddInnerDoor(InnerDoor innerDoor);
    void AddInnerDoor(int id, ::FVector openPos, ::FVector closePos, ::FVector safePos);

    int m_nId; // +0x00
    double m_dRadius; // +0x08
    std::vector<InnerDoor,
        std::allocator<InnerDoor>> m_vInnerDoorList; // +0x10
};


class CShuttlePathNode {
public:
    enum NodeType : std::int32_t {
        MOVE = 0,
        TELEPORT = 1,
    };

    CShuttlePathNode();
    ~CShuttlePathNode();

    void AddOuterDoor(CDoor* pDoor);

    NodeType m_Type; // +0x00
    ::FVector m_vPos; // +0x08
    int m_nWaitTime; // +0x20
    CDoor* m_pOuterDoor; // +0x28
    ::FVector m_vBanPos; // +0x30
    int m_nSpeed; // +0x48
    std::vector<int,
        std::allocator<int>> m_vInnerDoorList; // +0x50
};


class CShuttle : public CAbstractCarrier {
public:
    enum ShuttleState : std::int32_t {
        STS_MOVE = 0,
        STS_WAIT = 1,
    };

    CShuttle(int objectId, CShuttleConst* constant);
    CShuttle(const CShuttle& other);
    ~CShuttle() override;

    L2_DECLARE_OBJECT_REGISTRATION(CShuttle)

    static CShuttleSmartIdManager s_ShuttleSmartIdManager;

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    void TimerExpired(int id) override;
    FAngle GetDir() const override;
    int GetIndex() const override;
    CObject* Copy() override;
    bool IsShuttle() override;
    CShuttle* CastShuttle() override;
    void InSight(CObject* object, bool flag) override;
    void OutOfSight(CObject* object, bool flag) override;
    FVector ConvRelToAbs(FVector position) const override;
    FVector ConvAbsToRel(FVector position) const override;
    bool IsActive() override;
    FVector GetPos() const override;
    void SetDir(const FAngle& direction) override;
    bool SetMovingIntoCarrier(CMoveRelativeAction* action) override;

    void SeeDropItem(CItem* item, CCreature* dropper);
    void Delete();
    virtual void OnCreate();
    virtual FAngle GetMovingDir() const;

    void LeaveWorld();
    FVector GetAdjustPos(double distance, FVector position);
    void StartWait();
    bool HasArrived() const;
    void SetNextNode();
    bool AddOuterDoor(int nodeIndex, const char16_t* doorName);
    const CShuttlePathNode& GetCurrentNode() const;
    const CShuttlePathNode& GetNode(int nodeIndex) const;
    void SendShuttleInfo(CUserSocket* socket, bool includeCurrentState, int nodeIndex);
    void StartMove();
    void OpenStationDoor();
    void CloseStationDoor();
    void EnterWorld();
    void AddPathNode(const CShuttlePathNode& node);

    int ObjectID() const noexcept { return m_nObjectID; }


private:
    friend class CShuttleReleaseVerifier;
    friend class CShuttleManager;
    friend class CContainer;
    friend class CWorld;
    friend class User;
    friend bool BuilderCmd::set_shuttle(CUserSocket*, User*, char16_t*);
    friend bool BuilderCmd::shuttle_info(CUserSocket*, User*, char16_t*);
    friend bool BuilderCmd::shuttle_add_outer_door(CUserSocket*, User*, char16_t*);

public:
    CSharedShuttleData* m_data;
    CYieldLock* m_lock;
    CShuttleConst* m_const;

private:
    std::int32_t m_nObjectID;
    std::byte padding_74[4];
    CContainer* m_pContainer;
    FVector m_vOrigin;
    FVector m_vDestination;
    std::int32_t m_nMoveSpeed;
    std::int32_t m_nDirection;
    std::int32_t m_nRotationSpeed;
    bool m_bIsInWorld;
    std::byte padding_bd[3];
    std::vector<CShuttlePathNode> m_vNodeList;
    std::int32_t m_index;
    std::uint32_t m_nLastWaitTick;
    ShuttleState m_State;
    std::byte padding_ec[4];

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CShuttle)
};


class CShuttleManager : public ::CIOObject {
public:
    CShuttleManager();
    ~CShuttleManager() override;

    static CShuttleManager& Inst();

    bool DeleteShuttle(int objectId);
    void DeleteAllShuttle();
    CShuttle* GetShuttle(int objectId);
    CShuttleConst* GetShuttleConst(int constId);
    void AllShuttleEnterWorld();
    CShuttleConst* CreateShuttleConst(int constId, double radius);
    void Load();
    CShuttle* CreateShuttle(int objectId, int constId);
    void AddPathNode(int objectId, int x, int y, int z);


private:
    ::std::map<int, CShuttleConst*> m_ShuttleConstMap; // +0x18
    ::std::map<int, CShuttle*> m_ShuttleMap; // +0x38
    CYieldLock m_lock; // +0x58
};


// Exact current delayed-release verifier from Shuttle.cpp.
class CShuttleReleaseVerifier : public ::CIOObject {
public:
    explicit CShuttleReleaseVerifier(CShuttle* shuttle);
    ~CShuttleReleaseVerifier() override = default;

    void TimerExpired(int id) override;


private:
    CShuttle* m_pShuttle;
};
