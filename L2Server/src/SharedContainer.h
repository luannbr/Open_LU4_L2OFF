#pragma once

#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CAirShip;
class CCreature;
class CItem;
class CObject;
class CShuttle;
class CStaticObject;
class CTerritory;
class CTLMFilter;
class CUserSocket;
class CVehicle;
struct FVector;

namespace BuilderCmd {
bool CSCharacterInfoPacket(CUserSocket*, char16_t*);
}

enum SkillOperationTargetType : std::int32_t {
    SOTT_UNDEFINDED = -1,
    SOTT_SELF = 0,
    SOTT_TARGET = 1,
    SOTT_ALL = 2,
};

// SharedContainer.cpp owns this small record even though its producer is the
// generated predefined-list parser. The current image stores a borrowed
// territory pointer followed by the four-byte operation-target enum.
class TerritoryElement {
public:
    TerritoryElement() noexcept : m_pTerritory(nullptr), m_eSkillOPTargetType(SOTT_UNDEFINDED) {}

    void SetTerritory(CTerritory* territory) noexcept { m_pTerritory = territory; }
    CTerritory* GetTerritory() const noexcept { return m_pTerritory; }
    void SetSkillOPTargetType(SkillOperationTargetType type) noexcept { m_eSkillOPTargetType = type; }
    SkillOperationTargetType GetSkillOPTargetType() const noexcept;

private:
    CTerritory* m_pTerritory;
    SkillOperationTargetType m_eSkillOPTargetType;
    [[maybe_unused]] std::int32_t m_targetPadding0c{};
};


struct CSharedContainerData {
    std::int32_t m_nCreatureFirst;
    std::int32_t m_nItemFirst;
    std::int32_t m_nVehicleFirst;
    std::int32_t m_nAirShipFirst;
    std::int32_t m_nShuttleFirst;
    std::int32_t m_nStaticObjectFirst;
    std::int32_t m_nIndex;
    std::int32_t m_nParentContainerIndex;
    std::int32_t m_nOwnerId;
    std::int32_t m_nNext;
    std::int32_t m_nBefore;
};


class CContainer;
class CContainerFilter;
class CContainerFunc;

class CContainerFunc {
public:
    virtual ~CContainerFunc() = default;
    virtual void Do(CObject* object) = 0;
};

class CContainerFilter {
public:
    virtual ~CContainerFilter() = default;
    virtual bool CanDo(CObject* object) = 0;
};

class CContainerFilterNull final : public CContainerFilter {
public:
    bool CanDo(CObject*) override { return true; }
};


// Current 0x808 value type used by every container-neighbor producer.
class CContainerIndex {
public:
    static constexpr std::size_t kCapacity = 0x100;

    CContainerIndex() noexcept;
    CContainerIndex(CContainerIndex& other) noexcept;
    CContainerIndex& operator=(const CContainerIndex& other) noexcept = default;

    void Add(CContainer* container);
    bool Have(CContainer* container);
    void Substract(CContainerIndex& other);

    std::int32_t n;
    [[maybe_unused]] std::array<std::byte, 4> m_alignmentPadding;
    std::array<CContainer*, kCapacity> container;
};


void FOR_ALL_OBJECT_IN_CONTAINER_READLOCK_ASSUMED(CContainer* container, CContainerFilter& filter, CContainerFunc& function);
void FOR_ALL_OBJECT_IN_CONTAINER__INSTANT(CContainer* container, CContainerFilter& filter, CContainerFunc& function);
void FOR_ALL_OBJECT_IN_CONTAINER_INDEX(CContainerIndex& index, CContainerFilter& filter, CContainerFunc& function);
void FOR_ALL_OBJECT_IN_CONTAINER_INDEX__INSTANT(CContainerIndex& index, CContainerFilter& filter, CContainerFunc& function);

class CContainerDB;
class CWorld;

class CContainer {
public:
    virtual void GatherNeighborContainerIndex(CContainerIndex& index) = 0;
    virtual bool CanMoveTo(CCreature& creature, const FVector& position, bool force) = 0;
    virtual void OnMoveTo(CCreature& creature, const FVector& position, bool force) = 0;
    virtual void CheckContainerChanged(CCreature& creature, const FVector& position, bool force) = 0;
    virtual bool IsDynamic();

    ~CContainer();

    bool MoveTo(CCreature& creature, const FVector& position, bool force);
    bool PushCreature(CCreature* creature);
    bool PopCreature(CCreature* creature);
    bool PushItem(CItem* item);
    bool PopItem(CItem* item);
    bool PushStaticObject(CStaticObject* object);
    bool PopStaticObject(CStaticObject* object);
    bool PushVehicle(CVehicle* vehicle);
    bool PopVehicle(CVehicle* vehicle);
    bool PushShuttle(CShuttle* shuttle);
    bool PopShuttle(CShuttle* shuttle);
    bool PushAirShip(CAirShip* airShip);
    bool PopAirShip(CAirShip* airShip);
    static bool ChangeContainerOf(CCreature* creature, CContainer* source, CContainer* destination);

protected:
    friend class CContainerDB;
    friend class CWorld;
    friend class CVehicle;
    friend bool BuilderCmd::CSCharacterInfoPacket(CUserSocket* socket, char16_t* command);
    friend void FOR_ALL_OBJECT_IN_CONTAINER_READLOCK_ASSUMED(
        CContainer* container, CContainerFilter& filter, CContainerFunc& function
    );

    CContainer();

    void SetCreaturePos(CCreature& creature, const FVector& position, bool force);

    CSharedContainerData m_Data;
    std::int32_t m_nZDividedIndex;

public:
    std::int32_t m_nGridX;
    std::int32_t m_nGridY;

protected:
    std::int32_t m_nGridZ;
    std::int32_t m_nUnknown44;

public:
    std::list<CTerritory*> m_lBlockTerritoryList;
    CYieldLock m_BlockTerritoryLock;
};


class CPrivateContainer final : public CContainer {
public:
    CPrivateContainer();
    ~CPrivateContainer();

    void GatherNeighborContainerIndex(CContainerIndex& index) override;
    bool CanMoveTo(CCreature& creature, const FVector& position, bool force) override;
    void OnMoveTo(CCreature& creature, const FVector& position, bool force) override;
    void CheckContainerChanged(CCreature& creature, const FVector& position, bool force) override;

private:
    friend class CObject;
    friend class CContainerDB;
    friend class CTLMFilter;

    std::int32_t m_nInstantMapIndex;
    std::int32_t m_nNextPrivateContainerIndex;
};


class CStaticContainer : public CContainer {
public:
    void GatherNeighborContainerIndex(CContainerIndex& index) override;
    bool CanMoveTo(CCreature& creature, const FVector& position, bool force) override;
    void OnMoveTo(CCreature& creature, const FVector& position, bool force) override;
    void CheckContainerChanged(CCreature& creature, const FVector& position, bool force) override;

    virtual bool IsEscaped(const FVector& position);
    virtual void ContainerMove(CCreature& creature, const FVector& position, CContainer* destination, bool force) = 0;

protected:
    CStaticContainer() = default;
};


class CWorldContainer final : public CStaticContainer {
public:
    CWorldContainer();

    void ContainerMove(CCreature& creature, const FVector& position, CContainer* destination, bool force) override;
};

class CDynamicContainer : public CStaticContainer {
public:
    void GatherNeighborContainerIndex(CContainerIndex& index) override;
    bool IsDynamic() override;

protected:
    CDynamicContainer() = default;
};

class CVehicleContainer final : public CDynamicContainer {
public:
    CVehicleContainer();

    void ContainerMove(CCreature& creature, const FVector& position, CContainer* destination, bool force) override;
};

class CAirShipContainer final : public CDynamicContainer {
public:
    CAirShipContainer();

    void ContainerMove(CCreature& creature, const FVector& position, CContainer* destination, bool force) override;
};

class CShuttleContainer final : public CDynamicContainer {
public:
    CShuttleContainer();

    void ContainerMove(CCreature& creature, const FVector& position, CContainer* destination, bool force) override;
};


// Exact current fixed-storage owner. Init and the mutation/allocation family
// are published incrementally. The physical object and lookup/broadcast-facing
// methods are no longer represented by a parallel structure model.
class CContainerDB final {
public:
    static constexpr std::int32_t kWorldPrimaryCount = 0x12100;
    static constexpr std::int32_t kWorldZSplitCount = 0x2000;
    static constexpr std::int32_t kVehicleCount = 0x32;
    static constexpr std::int32_t kAirShipCount = 100;
    static constexpr std::int32_t kShuttleCount = 100;

    static constexpr std::int32_t kWorldZSplitFirstId = 0x12100;
    static constexpr std::int32_t kVehicleFirstId = 0x14100;
    static constexpr std::int32_t kAirShipFirstId = 0x14132;
    static constexpr std::int32_t kShuttleFirstId = 0x14196;
    static constexpr std::int32_t kPrivateFirstId = 0x141fa;

    CContainerDB();
    ~CContainerDB();

    CContainerDB(const CContainerDB&) = delete;
    CContainerDB& operator=(const CContainerDB&) = delete;

    static CContainerDB& Inst();

    void Init(int maximumPrivateContainers);

    CContainerIndex GatherNeighborContainerIndex(CContainer* container);
    CContainerIndex GatherNeighborWorldContainerIndex(CContainer* container);

    CWorldContainer* GetWorldContainer(int x, int y, int z);
    CContainer* GetContainer(int containerId);

    CContainer* AllocVehicleContainer(int ownerId);
    void FreeVehicleContainer(CContainer* container);
    CContainer* AllocAirShipContainer(int ownerId);
    void FreeAirShipContainer(CContainer* container);
    bool CanAllocAirShipContainer();
    CContainer* AllocShuttleContainer(int ownerId);
    void FreeShuttleContainer(CContainer* container);
    CPrivateContainer* AllocPrivateContainer(int instantMapIndex);
    void FreePrivateContainer(CPrivateContainer* container);

    static bool IsVehicleContainerId(int containerId) noexcept;
    static bool IsAirShipContainerId(int containerId) noexcept;
    static bool IsShuttleContainerId(int containerId) noexcept;

    int GetVehicleID(int containerId);
    int GetAirShipID(int containerId);
    int GetShuttleID(int containerId);

    bool ContainerChanged(CObject* object, CContainer* source, CContainer* destination, bool force);

private:
    std::array<CWorldContainer, kWorldPrimaryCount> m_WorldContainers;
    std::array<CWorldContainer, kWorldZSplitCount> m_ZSplitContainers;
    std::int32_t m_nZSplitRegionCount;
    CYieldLock m_VehicleLock;
    std::array<bool, kVehicleCount> m_VehicleUsed;
    std::array<CVehicleContainer, kVehicleCount> m_VehicleContainers;
    CYieldLock m_AirShipLock;
    std::array<bool, kAirShipCount> m_AirShipUsed;
    std::int32_t m_nAirShipAllocatedCount;
    std::array<CAirShipContainer, kAirShipCount> m_AirShipContainers;
    CYieldLock m_ShuttleLock;
    std::array<bool, kShuttleCount> m_ShuttleUsed;
    std::int32_t m_nShuttleAllocatedCount;
    std::array<CShuttleContainer, kShuttleCount> m_ShuttleContainers;
    CYieldLock m_PrivateLock;
    std::int32_t m_nPrivateStorageCount;
    std::int32_t m_nPrivateFreeHead;
    std::int32_t m_nPrivateFreeTail;
    CPrivateContainer* m_pPrivateContainers;
};
