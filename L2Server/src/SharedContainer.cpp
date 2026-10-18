#include "SharedContainer.h"
#include "Shared/src/ScopedLockGuards.h"
#include "Shared/src/TextRuntime.h"

#include "AirShip.h"
#include "CWorld.h"
#include "Creature.h"
#include "Item.h"
#include "Object.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/TimeAnalysis.h"
#include "Shuttle.h"
#include "StaticObject.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "Vehicle.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>

SkillOperationTargetType TerritoryElement::GetSkillOPTargetType() const noexcept { return m_eSkillOPTargetType; }

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
void CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::SetTlmBox3(FBox3& box, int x, int y, int z) {
    constexpr int kWorldLeft = -0x50000;
    constexpr int kWorldTop = -0x40000;
    constexpr int kWorldCellSize = 0x400;

    const int xIndex = std::clamp(runtime::SubtractInt32Wrapping(x, kWorldLeft) / kWorldCellSize, 0, 0x27f);
    const int yIndex = std::clamp(runtime::SubtractInt32Wrapping(y, kWorldTop) / kWorldCellSize, 0, 0x21f);
    const int cellX = xIndex * kWorldCellSize + kWorldLeft;
    const int cellY = yIndex * kWorldCellSize + kWorldTop;

    int minimumZ;
    int maximumZ;
    if (IsZDivided(xIndex, yIndex)) {
        const int zIndex = std::clamp(runtime::AddInt32Wrapping(z, 0x4000) / kWorldCellSize, 0, 0x1f);
        const int cellZ = (zIndex - 0x10) * kWorldCellSize;
        minimumZ = cellZ - 0x180;
        maximumZ = cellZ + 0x580;
    } else {
        minimumZ = -0x4000;
        maximumZ = 0x4000;
    }

    box.Set(cellX - 0x180, cellY - 0x180, minimumZ, cellX + 0x580, cellY + 0x580, maximumZ);
}

template void CThreadLocalWorldMap2<120000, 600000, 16>::SetTlmBox3(FBox3&, int, int, int);

template void CThreadLocalWorldMap2<1000, 3000, 16>::SetTlmBox3(FBox3&, int, int, int);

template <>
void CThreadLocalWorldMap2<120000, 600000, 16>::Push(int sharedIndex, const FVector& position, CObject* object) {
    L2_GUARD_FUNCTION();

    constexpr int kWorldLeft = -0x50000;
    constexpr int kWorldTop = -0x40000;
    constexpr int kWorldCellSize = 0x400;
    const short x = static_cast<short>(std::clamp(
        runtime::SubtractInt32Wrapping(runtime::Win64CvttDoubleToInt32(position.x), kWorldLeft) / kWorldCellSize, 0,
        0x27f
    ));
    const short y = static_cast<short>(std::clamp(
        runtime::SubtractInt32Wrapping(runtime::Win64CvttDoubleToInt32(position.y), kWorldTop) / kWorldCellSize, 0,
        0x21f
    ));

    const int threadId = CThreadManager::GetThreadID();
    if (threadId >= 16) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"g_tlThreadID[%d] should be less then nThreadCount[%d]",
        //     threadId,
        //     16)
        // L2_LOG_ERROR(Log,
        //     L"g_tlThreadID[%d] should be less then nThreadCount[%d]",
        //     threadId,
        //     16)
        L2_LOG_ERROR(Log, "Cannot gather user-map neighbors: worker %d exceeds the highest supported index 15.", threadId);
        return;
    }

    const char z = m_Map[static_cast<std::size_t>(threadId)].IsZDivided(x, y)
        ? static_cast<char>(std::clamp(
              runtime::AddInt32Wrapping(runtime::Win64CvttDoubleToInt32(position.z), 0x4000) / kWorldCellSize, 0, 0x1f
          ))
        : static_cast<char>(-1);
    Write2(x, y, z, sharedIndex, object);
}

CContainerIndex::CContainerIndex() noexcept : n(0), container{} {}

CContainerIndex::CContainerIndex(CContainerIndex& other) noexcept : n(0) {
    L2_GUARD_FUNCTION();
    for (int index = 0; index < other.n; ++index) {
        container[static_cast<std::size_t>(index)] = other.container[static_cast<std::size_t>(index)];
    }
    n = other.n;
}

void CContainerIndex::Add(CContainer* container) {
    L2_GUARD_FUNCTION();
    if (container == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Null Container at file[%s], line[%d]",
        //     "SharedContainer.cpp",
        //     699)
        // L2_LOG_ERROR(Log,
        //     "Null Container at file[%s], line[%d]",
        //     "SharedContainer.cpp",
        //     699)
        L2_LOG_ERROR(Log, "Cannot add a null container to the neighbor index.");
        return;
    }

    if (Have(container)) {
        return;
    }
    if (n >= static_cast<int>(kCapacity)) {
        // Log.Add(LOG_LOCAL, "CContainerIndex overflow!")
        // L2_LOG(Log, LOG_LOCAL, "CContainerIndex overflow!")
        L2_LOG_ERROR_WITH_LOCAL_COPY(
            Log, "Container neighbor index overflow: %d entries, capacity %zu; invoking the fatal size check.", n,
            static_cast<std::size_t>(kCapacity)
        );
        // Log.Add(LOG_ERROR, "CContainerIndex overflow!")
        // L2_LOG_ERROR(Log, "CContainerIndex overflow!")
        // Noise: ERROR_WITH_LOCAL_COPY above already keeps both error and mandatory local destinations.
        runtime::process::FailCheckSizeInt();
    }

    int insertion = n;
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(container);
    while (insertion > 0 &&
        reinterpret_cast<std::uintptr_t>(this->container[static_cast<std::size_t>(insertion - 1)]) > address) {
        this->container[static_cast<std::size_t>(insertion)] = this->container[static_cast<std::size_t>(insertion - 1)];
        --insertion;
    }
    this->container[static_cast<std::size_t>(insertion)] = container;
    ++n;
}

bool CContainerIndex::Have(CContainer* container) {
    L2_GUARD_FUNCTION();
    for (int index = 0; index < n; ++index) {
        if (this->container[static_cast<std::size_t>(index)] == container) {
            return true;
        }
    }
    return false;
}

void CContainerIndex::Substract(CContainerIndex& other) {
    L2_GUARD_FUNCTION();
    int retained = 0;
    for (int index = 0; index < n; ++index) {
        CContainer* const container = this->container[static_cast<std::size_t>(index)];
        if (container != nullptr && !other.Have(container)) {
            this->container[static_cast<std::size_t>(retained++)] = container;
        }
    }
    n = retained;
}

void FOR_ALL_OBJECT_IN_CONTAINER_READLOCK_ASSUMED(
    CContainer* container, CContainerFilter& filter, CContainerFunc& function
) {
    if (container == nullptr) {
        return;
    }

    int index = container->m_Data.m_nCreatureFirst;
    while (index != -1) {
        const std::uint32_t smartId = static_cast<std::uint32_t>(CCreatureFactory::Inst().GetIdByIndex(index));
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(smartId);
        if (creature != nullptr && filter.CanDo(creature)) {
            function.Do(creature);
        }
        index = CCreatureFactory::Inst().GetNextIndex(index);
    }

    index = container->m_Data.m_nItemFirst;
    while (index != -1) {
        const std::uint32_t smartId = static_cast<std::uint32_t>(CItemFactory::Inst().GetIdByIndex(index));
        CItem* const item = ItemSmartIdManager()->GetObjectW(smartId);
        if (item != nullptr && filter.CanDo(item)) {
            function.Do(item);
        }
        index = CItemFactory::Inst().GetNextIndex(index);
    }

    index = container->m_Data.m_nStaticObjectFirst;
    while (index != -1) {
        const std::uint32_t smartId = static_cast<std::uint32_t>(CStaticObjectFactory::Inst().GetIdByIndex(index));
        CStaticObject* const staticObject = CStaticObject::GetObjectW(smartId);
        if (staticObject != nullptr && filter.CanDo(staticObject)) {
            function.Do(staticObject);
        }
        index = CStaticObjectFactory::Inst().GetNextIndex(index);
    }

    index = container->m_Data.m_nVehicleFirst;
    while (index != -1) {
        CContainer* const vehicleContainer = CContainerDB::Inst().GetContainer(index);
        CVehicle* const vehicle = g_VehicleDB.FindVehicle(vehicleContainer->m_Data.m_nOwnerId);
        if (vehicle == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     "vehicle null at file[%s], line[%d]",
            //     "SharedContainer.cpp",
            //     0x61a)
            // L2_LOG_ERROR(Log,
            //     "vehicle null at file[%s], line[%d]",
            //     "SharedContainer.cpp",
            //     0x61a)
            L2_LOG_ERROR(
                Log, "World traversal skips vehicle owner %u in container %d: vehicle lookup failed.",
                static_cast<unsigned>(vehicleContainer->m_Data.m_nOwnerId), index
            );
        } else {
            CObject* const object = static_cast<CObject*>(vehicle);
            if (filter.CanDo(object)) {
                function.Do(object);
                L2_RELEASE(object, ObjectRefType::FindObject);
            }
        }
        index = container->m_Data.m_nNext;
    }

    index = container->m_Data.m_nAirShipFirst;
    while (index != -1) {
        const std::uint32_t smartId = static_cast<std::uint32_t>(CAirShipFactory::Inst().GetIdByIndex(index));
        CObject* const airShip = static_cast<CObject*>(g_AirShipSmartIdManager.GetObjectW(smartId));
        if (airShip != nullptr && filter.CanDo(airShip)) {
            function.Do(airShip);
        }
        index = CAirShipFactory::Inst().GetNextIndex(index);
    }

    index = container->m_Data.m_nShuttleFirst;
    while (index != -1) {
        const std::uint32_t smartId = static_cast<std::uint32_t>(CShuttleFactory::Inst().GetIdByIndex(index));
        CObject* const shuttle = static_cast<CObject*>(CShuttle::s_ShuttleSmartIdManager.GetObjectW(smartId));
        if (shuttle != nullptr && filter.CanDo(shuttle)) {
            function.Do(shuttle);
        }
        index = CShuttleFactory::Inst().GetNextIndex(index);
    }
}

void FOR_ALL_OBJECT_IN_CONTAINER__INSTANT(CContainer* container, CContainerFilter& filter, CContainerFunc& function) {
    // Current L2Server.exe 0x00a500c8 and collector 0x00a4f9a8.
    g_Timer.BeginJob(JOB_FOR_ALL_OBJECT_IN_CONTAINER__INSTANT);

    std::vector<CObject*> objects;
    class ContainerFunc final : public CContainerFunc {
    public:
        explicit ContainerFunc(std::vector<CObject*>& objects) : objects_(objects) {}

        void Do(CObject* object) override {
            if (!object->IsPC() || (!object->IsDeleting() && object->m_Deleted == 0)) {
                L2_ADD_REF(object, ObjectRefType::FindObject);
                objects_.push_back(object);
            }
        }

    private:
        std::vector<CObject*>& objects_;
    } collector(objects);

    FOR_ALL_OBJECT_IN_CONTAINER_READLOCK_ASSUMED(container, filter, collector);
    for (CObject* const object : objects) {
        function.Do(object);
        L2_RELEASE(object, ObjectRefType::FindObject);
    }
    g_Timer.EndJob(JOB_FOR_ALL_OBJECT_IN_CONTAINER__INSTANT);
}

void FOR_ALL_OBJECT_IN_CONTAINER_INDEX(CContainerIndex& index, CContainerFilter& filter, CContainerFunc& function) {
    // Current L2Server.exe 0x00a5001c owns the direct-index timer.
    g_Timer.BeginJob(JOB_FOR_ALL_OBJECT_IN_CONTAINER_INDEX);
    for (int containerIndex = 0; containerIndex < index.n; ++containerIndex) {
        FOR_ALL_OBJECT_IN_CONTAINER_READLOCK_ASSUMED(
            index.container[static_cast<std::size_t>(containerIndex)], filter, function
        );
    }
    g_Timer.EndJob(JOB_FOR_ALL_OBJECT_IN_CONTAINER_INDEX);
}

void FOR_ALL_OBJECT_IN_CONTAINER_INDEX__INSTANT(CContainerIndex& index, CContainerFilter& filter, CContainerFunc& function) {
    // Current L2Server.exe 0x00a50228 and collector 0x00a4fa14.
    g_Timer.BeginJob(JOB_FOR_ALL_OBJECT_IN_CONTAINER_INDEX__INSTANT);

    std::vector<CObject*> objects;
    class ContainerFunc final : public CContainerFunc {
    public:
        explicit ContainerFunc(std::vector<CObject*>& objects) : objects_(objects) {}

        void Do(CObject* object) override {
            if (!object->IsPC() || (!object->IsDeleting() && object->m_Deleted == 0)) {
                L2_ADD_REF(object, ObjectRefType::FindObject);
                objects_.push_back(object);
            }
        }

    private:
        std::vector<CObject*>& objects_;
    } collector(objects);

    for (int containerIndex = 0; containerIndex < index.n; ++containerIndex) {
        FOR_ALL_OBJECT_IN_CONTAINER_READLOCK_ASSUMED(
            index.container[static_cast<std::size_t>(containerIndex)], filter, collector
        );
    }
    for (CObject* const object : objects) {
        function.Do(object);
        L2_RELEASE(object, ObjectRefType::FindObject);
    }
    g_Timer.EndJob(JOB_FOR_ALL_OBJECT_IN_CONTAINER_INDEX__INSTANT);
}

CContainer::CContainer() = default;

CContainer::~CContainer() = default;

bool CContainer::IsDynamic() { return false; }

bool CContainer::MoveTo(CCreature& creature, const FVector& position, bool force) {
    L2_GUARD_FUNCTION();
    if (!CanMoveTo(creature, position, force)) {
        return false;
    }

    OnMoveTo(creature, position, force);
    SetCreaturePos(creature, position, force);
    CheckContainerChanged(creature, position, force);
    return true;
}

void CContainer::SetCreaturePos(CCreature& creature, const FVector& position, bool force) {
    L2_GUARD_FUNCTION();
    creature.m_data->SetPos(position);
    if (force && creature.IsNPC()) {
        creature.ResetPassingPos(position);
    }
    creature.AddPassingPos(position);
}

bool CContainer::PushCreature(CCreature* creature) {
    L2_GUARD_FUNCTION();
    const bool pushed = CCreatureFactory::Inst().Push(creature->GetIndex(), m_Data.m_nCreatureFirst, m_Data.m_nIndex);
    if (!pushed) {
        if (std::rand() % 10 == 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] PushCreature Failed. Name[%s]",
            //     L"SharedContainer.cpp",
            //     0x5e,
            //     creature->GetName())
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] PushCreature Failed. Name[%s]",
            //     L"SharedContainer.cpp",
            //     0x5e,
            //     creature->GetName())
            // L2_LOG_ERROR(Log,
            //     "Cannot insert creature '%s' (object %u) into container %d. Shared-container reference was not added.",
            //     WideToUtf8(creature->GetName()).c_str(), static_cast<unsigned>(creature->m_nId), m_Data.m_nIndex)
            L2_LOG_ERROR(
                Log, "Cannot insert creature '%s' (object %u) into container %d; shared-container reference was not added.",
                Utf16ToUtf8(creature->GetName()).c_str(), static_cast<unsigned>(creature->m_nId), m_Data.m_nIndex
            );
        }
        return false;
    }
    L2_ADD_REF(creature, ObjectRefType::SharedContainer);
    return true;
}

bool CContainer::PopCreature(CCreature* creature) {
    L2_GUARD_FUNCTION();
    const bool popped = CCreatureFactory::Inst().Pop(creature->GetIndex(), m_Data.m_nCreatureFirst);
    if (!popped) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] PopCreature Failed. Name[%s]",
        //     L"SharedContainer.cpp",
        //     0x70,
        //     creature->GetName())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] PopCreature Failed. Name[%s]",
        //     L"SharedContainer.cpp",
        //     0x70,
        //     creature->GetName())
        // L2_LOG_ERROR(Log,
        //     "Cannot remove creature '%s' (object %u) from container %d. Shared-container reference retained.",
        //     WideToUtf8(creature->GetName()).c_str(), static_cast<unsigned>(creature->m_nId), m_Data.m_nIndex)
        L2_LOG_ERROR(
            Log, "Cannot remove creature '%s' (object %u) from container %d; shared-container reference retained.",
            Utf16ToUtf8(creature->GetName()).c_str(), static_cast<unsigned>(creature->m_nId), m_Data.m_nIndex
        );
        return false;
    }
    L2_RELEASE(creature, ObjectRefType::SharedContainer);
    return true;
}

bool CContainer::PushItem(CItem* item) {
    L2_GUARD_FUNCTION();
    const bool pushed = CItemFactory::Inst().Push(item->GetIndex(), m_Data.m_nItemFirst, m_Data.m_nIndex);
    if (!pushed) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[AsyncPutItem] PushItem Failed! ItemID[%d]",
        //     item->GetDBID())
        // L2_LOG_ERROR(Log,
        //     L"[AsyncPutItem] PushItem Failed! ItemID[%d]",
        //     item->GetDBID())
        L2_LOG_ERROR(
            Log, "Cannot insert item %u into container %d; shared-container reference was not added.",
            static_cast<unsigned>(item->GetDBID()), m_Data.m_nIndex
        );
    } else {
        L2_ADD_REF(item, ObjectRefType::SharedContainer);
    }
    return pushed;
}

bool CContainer::PopItem(CItem* item) {
    L2_GUARD_FUNCTION();
    const bool popped = CItemFactory::Inst().Pop(item->GetIndex(), m_Data.m_nItemFirst);
    if (!popped) {
        return false;
    }
    L2_RELEASE(item, ObjectRefType::SharedContainer);
    return true;
}

bool CContainer::PushVehicle(CVehicle* vehicle) {
    L2_GUARD_FUNCTION();
    CContainer* const vehicleContainer = vehicle->m_pContainer;
    if (vehicleContainer == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid vehicle container",
        //     L"SharedContainer.cpp",
        //     0xe7)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid vehicle container",
        //     L"SharedContainer.cpp",
        //     0xe7)
        L2_LOG_ERROR(
            Log, "Cannot insert vehicle object %u into container %d: vehicle has no container.",
            static_cast<unsigned>(vehicle->m_nId), m_Data.m_nIndex
        );
        return false;
    }

    const int vehicleIndex = vehicle->GetIndex();
    if (vehicleContainer->m_Data.m_nNext != -1 || vehicleContainer->m_Data.m_nBefore != -1) {
        // Log.Add(LOG_ERROR, "trying to multiple push!!")
        // L2_LOG_ERROR(Log, "trying to multiple push!!")
        L2_LOG_ERROR(
            Log, "Cannot insert vehicle index %d into container %d: existing links previous=%d next=%d.", vehicleIndex,
            m_Data.m_nIndex, vehicleContainer->m_Data.m_nBefore, vehicleContainer->m_Data.m_nNext
        );
        return false;
    }

    if (m_Data.m_nVehicleFirst == vehicleIndex) {
        const FVector position = vehicle->GetPos();
        const std::wstring vehicleName = target_utf16::ToHostCodeUnits(vehicle->m_Name);
        // Log.Add(
        //     LOG_ERROR,
        //     L"infinite loop generated(%d)! vehicle(%s[%d] : "
        //     L"%d, %d, %d) at file[%s], line[%d]",
        //     m_Data.m_nIndex,
        //     vehicleName.c_str(),
        //     vehicleIndex,
        //     static_cast<int>(position.x),
        //     static_cast<int>(position.y),
        //     static_cast<int>(position.z),
        //     L"SharedContainer.cpp",
        //     0xfa)
        // L2_LOG_ERROR(Log,
        //     L"infinite loop generated(%d)! vehicle(%s[%d] : "
        //     L"%d, %d, %d) at file[%s], line[%d]",
        //     m_Data.m_nIndex,
        //     vehicleName.c_str(),
        //     vehicleIndex,
        //     static_cast<int>(position.x),
        //     static_cast<int>(position.y),
        //     static_cast<int>(position.z),
        //     L"SharedContainer.cpp",
        //     0xfa)
        L2_LOG_ERROR(
            Log,
            "Cannot insert vehicle '%s' (index %d) at (%.17g, %.17g, %.17g) into container %d: vehicle is already the list "
            "head; self-link rejected.",
            Utf16ToUtf8(target_utf16::FromHostCodeUnits(vehicleName)).c_str(), vehicleIndex, position.x, position.y,
            position.z, m_Data.m_nIndex
        );
        return false;
    }

    vehicleContainer->m_Data.m_nParentContainerIndex = m_Data.m_nIndex;
    vehicleContainer->m_Data.m_nNext = m_Data.m_nVehicleFirst;
    if (vehicleContainer->m_Data.m_nNext >= 0) {
        CContainer* const next = CContainerDB::Inst().GetContainer(vehicleContainer->m_Data.m_nNext);
        if (next == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid vehicle container",
            //     L"SharedContainer.cpp",
            //     0x10b)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid vehicle container",
            //     L"SharedContainer.cpp",
            //     0x10b)
            L2_LOG_ERROR(
                Log,
                "Vehicle index %d insertion into container %d failed: next container %d is missing; assigned parent and "
                "next indices remain.",
                vehicleIndex, m_Data.m_nIndex, vehicleContainer->m_Data.m_nNext
            );
            return false;
        }
        next->m_Data.m_nBefore = vehicleIndex;
    }
    m_Data.m_nVehicleFirst = vehicleIndex;
    vehicleContainer->m_Data.m_nBefore = -2;
    return true;
}

bool CContainer::PopVehicle(CVehicle* vehicle) {
    L2_GUARD_FUNCTION();
    CContainer* const vehicleContainer = vehicle->m_pContainer;
    if (vehicleContainer == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid vehicle container",
        //     L"SharedContainer.cpp",
        //     0x121)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid vehicle container",
        //     L"SharedContainer.cpp",
        //     0x121)
        L2_LOG_ERROR(
            Log, "Cannot remove vehicle object %u from container %d: vehicle has no container.",
            static_cast<unsigned>(vehicle->m_nId), m_Data.m_nIndex
        );
        return false;
    }

    const int vehicleIndex = vehicle->GetIndex();
    if (vehicleContainer->m_Data.m_nNext == -1 && vehicleContainer->m_Data.m_nBefore == -1) {
        // Log.Add(LOG_ERROR, "Trying Ghost Pop in Vehicle!")
        // L2_LOG_ERROR(Log, "Trying Ghost Pop in Vehicle!")
        L2_LOG_ERROR(
            Log, "Cannot remove vehicle index %d from container %d: both links are unset.", vehicleIndex, m_Data.m_nIndex
        );
        return false;
    }

    const int previousIndex = vehicleContainer->m_Data.m_nBefore;
    if (previousIndex < 0) {
        if (m_Data.m_nVehicleFirst == vehicleIndex) {
            m_Data.m_nVehicleFirst = vehicleContainer->m_Data.m_nNext;
        }
    } else {
        CContainer* const previous = CContainerDB::Inst().GetContainer(previousIndex);
        if (previous == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid vehicle container",
            //     L"SharedContainer.cpp",
            //     0x13b)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid vehicle container",
            //     L"SharedContainer.cpp",
            //     0x13b)
            L2_LOG_ERROR(
                Log, "Cannot unlink vehicle index %d from container %d: previous container %d is missing.", vehicleIndex,
                m_Data.m_nIndex, previousIndex
            );
            return false;
        }
        previous->m_Data.m_nNext = vehicleContainer->m_Data.m_nNext;
    }

    const int nextIndex = vehicleContainer->m_Data.m_nNext;
    if (nextIndex >= 0) {
        CContainer* const next = CContainerDB::Inst().GetContainer(nextIndex);
        if (next == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid vehicle container",
            //     L"SharedContainer.cpp",
            //     0x146)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid vehicle container",
            //     L"SharedContainer.cpp",
            //     0x146)
            L2_LOG_ERROR(
                Log,
                "Vehicle index %d removal from container %d failed: next container %d is missing; earlier "
                "head/previous-link updates remain.",
                vehicleIndex, m_Data.m_nIndex, nextIndex
            );
            return false;
        }
        next->m_Data.m_nBefore = previousIndex;
    }

    vehicleContainer->m_Data.m_nBefore = -1;
    vehicleContainer->m_Data.m_nNext = -1;
    vehicleContainer->m_Data.m_nParentContainerIndex = -1;
    return true;
}

bool CContainer::PushShuttle(CShuttle* shuttle) {
    L2_GUARD_FUNCTION();
    static std::int32_t failureCount = 0;
    const bool pushed = CShuttleFactory::Inst().Push(shuttle->GetIndex(), m_Data.m_nShuttleFirst, m_Data.m_nIndex);
    if (!pushed) {
        ++failureCount;
        if (failureCount % 10 == 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] PushShuttle Failed. ",
            //     L"SharedContainer.cpp",
            //     0x18f)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] PushShuttle Failed. ",
            //     L"SharedContainer.cpp",
            //     0x18f)
            L2_LOG_ERROR(
                Log,
                "Cannot insert shuttle object %u into container %d; cumulative failure count %d (reported every tenth "
                "failure).",
                static_cast<unsigned>(shuttle->m_nId), m_Data.m_nIndex, failureCount
            );
        }
        return false;
    }
    shuttle->m_pContainer->m_Data.m_nParentContainerIndex = m_Data.m_nIndex;
    L2_ADD_REF(shuttle, ObjectRefType::SharedContainer);
    return true;
}

bool CContainer::PopShuttle(CShuttle* shuttle) {
    L2_GUARD_FUNCTION();
    const bool popped = CShuttleFactory::Inst().Pop(shuttle->GetIndex(), m_Data.m_nShuttleFirst);
    if (!popped) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] PopCreature Failed. ",
        //     L"SharedContainer.cpp",
        //     0x1a3)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] PopCreature Failed. ",
        //     L"SharedContainer.cpp",
        //     0x1a3)
        L2_LOG_ERROR(
            Log, "Cannot remove shuttle object %u from container %d; parent index and reference retained.",
            static_cast<unsigned>(shuttle->m_nId), m_Data.m_nIndex
        );
        return false;
    }
    shuttle->m_pContainer->m_Data.m_nParentContainerIndex = -1;
    L2_RELEASE(shuttle, ObjectRefType::SharedContainer);
    return true;
}

bool CContainer::PushAirShip(CAirShip* airShip) {
    L2_GUARD_FUNCTION();
    static std::int32_t failureCount = 0;
    const bool pushed = CAirShipFactory::Inst().Push(airShip->GetIndex(), m_Data.m_nAirShipFirst, m_Data.m_nIndex);
    if (!pushed) {
        ++failureCount;
        if (failureCount % 10 == 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] PushAirShip Failed. ",
            //     L"SharedContainer.cpp",
            //     0x164)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] PushAirShip Failed. ",
            //     L"SharedContainer.cpp",
            //     0x164)
            L2_LOG_ERROR(
                Log,
                "Cannot insert airship object %u into container %d; cumulative failure count %d (reported every tenth "
                "failure).",
                static_cast<unsigned>(airShip->m_nId), m_Data.m_nIndex, failureCount
            );
        }
        return false;
    }
    airShip->m_pContainer->m_Data.m_nParentContainerIndex = m_Data.m_nIndex;
    L2_ADD_REF(airShip, ObjectRefType::SharedContainer);
    return true;
}

bool CContainer::PopAirShip(CAirShip* airShip) {
    L2_GUARD_FUNCTION();
    const bool popped = CAirShipFactory::Inst().Pop(airShip->GetIndex(), m_Data.m_nAirShipFirst);
    if (!popped) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] PopCreature Failed. ",
        //     L"SharedContainer.cpp",
        //     0x178)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] PopCreature Failed. ",
        //     L"SharedContainer.cpp",
        //     0x178)
        L2_LOG_ERROR(
            Log, "Cannot remove airship object %u from container %d; parent index and reference retained.",
            static_cast<unsigned>(airShip->m_nId), m_Data.m_nIndex
        );
        return false;
    }
    airShip->m_pContainer->m_Data.m_nParentContainerIndex = -1;
    L2_RELEASE(airShip, ObjectRefType::SharedContainer);
    return true;
}

bool CContainer::PushStaticObject(CStaticObject* object) {
    L2_GUARD_FUNCTION();
    const bool pushed = CStaticObjectFactory::Inst().Push(object->GetIndex(), m_Data.m_nStaticObjectFirst, m_Data.m_nIndex);
    if (pushed) {
        L2_ADD_REF(object, ObjectRefType::SharedContainer);
    }
    return pushed;
}

bool CContainer::PopStaticObject(CStaticObject* object) {
    L2_GUARD_FUNCTION();
    const bool popped = CStaticObjectFactory::Inst().Pop(object->GetIndex(), m_Data.m_nStaticObjectFirst);
    if (popped) {
        L2_RELEASE(object, ObjectRefType::SharedContainer);
    }
    return popped;
}

bool CContainer::ChangeContainerOf(CCreature* creature, CContainer* source, CContainer* destination) {
    L2_GUARD_FUNCTION();
    const bool changed = CCreatureFactory::Inst().Change(
        creature->GetIndex(), source->m_Data.m_nCreatureFirst, destination->m_Data.m_nCreatureFirst,
        destination->m_Data.m_nIndex
    );
    if (!changed) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ChangeContainerOf Creature Failed. Name[%s]",
        //     L"SharedContainer.cpp",
        //     0x8c,
        //     creature->GetName())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] ChangeContainerOf Creature Failed. Name[%s]",
        //     L"SharedContainer.cpp",
        //     0x8c,
        //     creature->GetName())
        // L2_LOG_ERROR(Log,
        //     "Cannot move creature '%s' (object %u) from container %d to %d: factory change failed.",
        //     WideToUtf8(creature->GetName()).c_str(), static_cast<unsigned>(creature->m_nId), source->m_Data.m_nIndex,
        //     destination->m_Data.m_nIndex)
        L2_LOG_ERROR(
            Log, "Cannot move creature '%s' (object %u) from container %d to %d: factory change failed.",
            Utf16ToUtf8(creature->GetName()).c_str(), static_cast<unsigned>(creature->m_nId), source->m_Data.m_nIndex,
            destination->m_Data.m_nIndex
        );
    }
    return changed;
}

CPrivateContainer::CPrivateContainer() = default;

CPrivateContainer::~CPrivateContainer() = default;

void CPrivateContainer::GatherNeighborContainerIndex(CContainerIndex& index) {
    L2_GUARD_FUNCTION();
    index.Add(this);
}

bool CPrivateContainer::CanMoveTo(CCreature&, const FVector&, bool) { return true; }

void CPrivateContainer::OnMoveTo(CCreature&, const FVector&, bool) {}

void CPrivateContainer::CheckContainerChanged(CCreature&, const FVector&, bool) {}

CWorldContainer::CWorldContainer() = default;

void CWorldContainer::ContainerMove(CCreature& creature, const FVector&, CContainer* destination, bool force) {
    L2_GUARD_FUNCTION();
    CWorld::Inst().ContainerMove(creature, destination, force);
}

CVehicleContainer::CVehicleContainer() = default;

CAirShipContainer::CAirShipContainer() = default;

CShuttleContainer::CShuttleContainer() = default;

void CVehicleContainer::ContainerMove(CCreature& creature, const FVector& position, CContainer*, bool) {
    L2_GUARD_FUNCTION();
    CVehicle* const vehicle = g_VehicleDB.FindVehicle(m_Data.m_nOwnerId);
    if (vehicle == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "vehicle null at file[%s], line[%d]",
        //     "SharedContainer.cpp",
        //     0x26c)
        // L2_LOG_ERROR(Log,
        //     "vehicle null at file[%s], line[%d]",
        //     "SharedContainer.cpp",
        //     0x26c)
        L2_LOG_ERROR(
            Log, "Cannot move creature object %u in vehicle container %d: owner %u is unavailable.",
            static_cast<unsigned>(creature.m_nId), m_Data.m_nIndex, static_cast<unsigned>(m_Data.m_nOwnerId)
        );
        return;
    }

    if (vehicle->GetPos().DistCube(position) > 100000000.0) {
        CWorld::Inst().GetOffVehicle(&creature, vehicle, position, true);
    }
    L2_RELEASE(vehicle, ObjectRefType::FindObject);
}

void CAirShipContainer::ContainerMove(CCreature& creature, const FVector& position, CContainer*, bool) {
    L2_GUARD_FUNCTION();
    CAirShip* const airShip = g_AirShipSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_Data.m_nOwnerId));
    if (airShip == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "airship null at file[%s], line[%d]",
        //     "SharedContainer.cpp",
        //     0x280)
        // L2_LOG_ERROR(Log,
        //     "airship null at file[%s], line[%d]",
        //     "SharedContainer.cpp",
        //     0x280)
        L2_LOG_ERROR(
            Log, "Cannot move creature object %u in airship container %d: owner %u is unavailable.",
            static_cast<unsigned>(creature.m_nId), m_Data.m_nIndex, static_cast<unsigned>(m_Data.m_nOwnerId)
        );
        return;
    }

    if (airShip->GetPos().DistCube(position) > 100000000.0) {
        CWorld::Inst().GetOffAirShip(&creature, airShip, position);
    }
}

void CShuttleContainer::ContainerMove(CCreature& creature, const FVector& position, CContainer*, bool) {
    L2_GUARD_FUNCTION();
    CShuttle* const shuttle = CShuttle::s_ShuttleSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_Data.m_nOwnerId));
    if (shuttle == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "shuttle null at file[%s], line[%d]",
        //     "SharedContainer.cpp",
        //     0x294)
        // L2_LOG_ERROR(Log,
        //     "shuttle null at file[%s], line[%d]",
        //     "SharedContainer.cpp",
        //     0x294)
        L2_LOG_ERROR(
            Log, "Cannot move creature object %u in shuttle container %d: owner %u is unavailable.",
            static_cast<unsigned>(creature.m_nId), m_Data.m_nIndex, static_cast<unsigned>(m_Data.m_nOwnerId)
        );
        return;
    }

    if (shuttle->GetPos().DistCube(position) > 1000000.0) {
        CWorld::Inst().GetOffShuttle(&creature, shuttle, position, true);
    }
}

void CStaticContainer::GatherNeighborContainerIndex(CContainerIndex& index) {
    L2_GUARD_FUNCTION();
    CContainerIndex neighbors = CContainerDB::Inst().GatherNeighborWorldContainerIndex(this);
    // std::memcpy(&index, &neighbors, sizeof(index))
    index = neighbors;
}

bool CStaticContainer::CanMoveTo(CCreature& creature, const FVector& position, bool) {
    L2_GUARD_FUNCTION();
    CWorldContainer* const destination = CContainerDB::Inst().GetWorldContainer(
        static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z)
    );
    if (destination == nullptr) {
        const int currentZ = static_cast<int>(creature.GetPos().z);
        const int currentY = static_cast<int>(creature.GetPos().y);
        const int currentX = static_cast<int>(creature.GetPos().x);
        // Log.Add(
        //     LOG_ERROR,
        //     L"pContTo is null while (%d,%d,%d)->(%d,%d,%d), "
        //     L"creature[%s] at file[%s], line[%d]",
        //     currentX,
        //     currentY,
        //     currentZ,
        //     static_cast<int>(position.x),
        //     static_cast<int>(position.y),
        //     static_cast<int>(position.z),
        //     creature.GetName(),
        //     L"SharedContainer.cpp",
        //     0x1e4)
        // L2_LOG_ERROR(Log,
        //     L"pContTo is null while (%d,%d,%d)->(%d,%d,%d), "
        //     L"creature[%s] at file[%s], line[%d]",
        //     currentX,
        //     currentY,
        //     currentZ,
        //     static_cast<int>(position.x),
        //     static_cast<int>(position.y),
        //     static_cast<int>(position.z),
        //     creature.GetName(),
        //     L"SharedContainer.cpp",
        //     0x1e4)
        // L2_LOG_ERROR(Log,
        //     "Movement rejected for creature '%s' (object %u): no destination container from (%d, %d, %d) to (%.17g, %.17g,
        //     %.17g).", WideToUtf8(creature.GetName()).c_str(), static_cast<unsigned>(creature.m_nId), currentX, currentY,
        //     currentZ, position.x, position.y, position.z)
        L2_LOG_ERROR(
            Log,
            "Movement rejected for creature '%s' (object %u): no destination container from (%d, %d, %d) to (%.17g, %.17g, "
            "%.17g).",
            Utf16ToUtf8(creature.GetName()).c_str(), static_cast<unsigned>(creature.m_nId), currentX, currentY, currentZ,
            position.x, position.y, position.z
        );
        runtime::process::ReportCallStack(2, false);
    }
    return destination != nullptr;
}

void CStaticContainer::OnMoveTo(CCreature& creature, const FVector& position, bool) {
    L2_GUARD_FUNCTION();

    const int x = runtime::Win64CvttDoubleToInt32(position.x);
    const int y = runtime::Win64CvttDoubleToInt32(position.y);
    const int z = runtime::Win64CvttDoubleToInt32(position.z);
    if (!creature.m_tlmBox3.Include(x, y, z)) {
        g_ptlCreatureMap->SetTlmBox3(creature.m_tlmBox3, x, y, z);
        g_ptlCreatureMap->Push(creature.m_data->index, position, &creature);
        if (creature.IsPC()) {
            g_ptlUserMap->Push(creature.m_data->index, position, &creature);
        }
    }

    if (creature.IsPC()) {
        creature.CastUser()->ChangeUserMapDensity(position);
    }
}

void CStaticContainer::CheckContainerChanged(CCreature& creature, const FVector& position, bool force) {
    L2_GUARD_FUNCTION();
    CContainer* const destination = CContainerDB::Inst().GetWorldContainer(
        static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z)
    );
    if (destination == nullptr) {
        const int currentZ = static_cast<int>(creature.GetPos().z);
        const int currentY = static_cast<int>(creature.GetPos().y);
        const int currentX = static_cast<int>(creature.GetPos().x);
        // Log.Add(
        //     LOG_ERROR,
        //     L"pContTo is null while (%d,%d,%d)->(%d,%d,%d), "
        //     L"creature[%s] at file[%s], line[%d]",
        //     currentX,
        //     currentY,
        //     currentZ,
        //     static_cast<int>(position.x),
        //     static_cast<int>(position.y),
        //     static_cast<int>(position.z),
        //     creature.GetName(),
        //     L"SharedContainer.cpp",
        //     0x218)
        // L2_LOG_ERROR(Log,
        //     L"pContTo is null while (%d,%d,%d)->(%d,%d,%d), "
        //     L"creature[%s] at file[%s], line[%d]",
        //     currentX,
        //     currentY,
        //     currentZ,
        //     static_cast<int>(position.x),
        //     static_cast<int>(position.y),
        //     static_cast<int>(position.z),
        //     creature.GetName(),
        //     L"SharedContainer.cpp",
        //     0x218)
        // L2_LOG_ERROR(Log,
        //     "Container change skipped for creature '%s' (object %u): no destination container from (%d, %d, %d) to (%.17g,
        //     %.17g, %.17g).", WideToUtf8(creature.GetName()).c_str(), static_cast<unsigned>(creature.m_nId), currentX,
        //     currentY, currentZ, position.x, position.y, position.z)
        L2_LOG_ERROR(
            Log,
            "Container change skipped for creature '%s' (object %u): no destination container from (%d, %d, %d) to (%.17g, "
            "%.17g, %.17g).",
            Utf16ToUtf8(creature.GetName()).c_str(), static_cast<unsigned>(creature.m_nId), currentX, currentY, currentZ,
            position.x, position.y, position.z
        );
        runtime::process::ReportCallStack(2, false);
    } else if (destination != this && IsEscaped(position)) {
        ContainerMove(creature, position, destination, force);
    }
}

bool CStaticContainer::IsEscaped(const FVector& position) {
    constexpr int kWorldXOrigin = -0x50000;
    constexpr int kWorldYOrigin = -0x40000;
    const int centerX = m_nGridX * 0x800 + 0x400 + kWorldXOrigin;
    const int centerY = m_nGridY * 0x800 + 0x400 + kWorldYOrigin;
    if (std::abs(centerX - static_cast<int>(position.x)) >= 0x500 ||
        std::abs(centerY - static_cast<int>(position.y)) >= 0x500) {
        return true;
    }
    if (m_nGridZ < 0) {
        return false;
    }
    const int centerZ = m_nGridZ * 0x400 - 0x3e00;
    return std::abs(centerZ - static_cast<int>(position.z)) >= 0x280;
}

void CDynamicContainer::GatherNeighborContainerIndex(CContainerIndex& index) {
    L2_GUARD_FUNCTION();
    index.Add(this);
    CContainer* const parent = CContainerDB::Inst().GetContainer(m_Data.m_nParentContainerIndex);
    if (parent != nullptr) {
        parent->GatherNeighborContainerIndex(index);
    }
}

bool CDynamicContainer::IsDynamic() { return true; }

CContainerDB::CContainerDB() : m_nPrivateFreeHead(0), m_nPrivateFreeTail(0) {}

CContainerDB::~CContainerDB() {
    L2_GUARD_FUNCTION();
    delete[] m_pPrivateContainers;
}

CContainerDB& CContainerDB::Inst() {
    static CContainerDB database;
    return database;
}

void CContainerDB::Init(int maximumPrivateContainers) {
    L2_GUARD_FUNCTION();
    // Log.Add(LOG_NORMAL, L"creating g_ContainerDB")
    // L2_LOG_INFO(Log, L"creating g_ContainerDB")
    L2_LOG_INFO(Log, "Initializing world containers with %d private-container slots requested.", maximumPrivateContainers);

    int containerId = 0;
    const auto initializeContainerData = [&containerId](CContainer& container) {
        container.m_Data.m_nCreatureFirst = -1;
        container.m_Data.m_nItemFirst = -1;
        container.m_Data.m_nVehicleFirst = -1;
        container.m_Data.m_nAirShipFirst = -1;
        container.m_Data.m_nShuttleFirst = -1;
        container.m_Data.m_nStaticObjectFirst = -1;
        container.m_Data.m_nIndex = containerId++;
        container.m_Data.m_nParentContainerIndex = -1;
        container.m_Data.m_nOwnerId = 0;
        container.m_Data.m_nNext = -1;
        container.m_Data.m_nBefore = -1;
    };

    for (CWorldContainer& container : m_WorldContainers) {
        initializeContainerData(container);
    }
    for (CWorldContainer& container : m_ZSplitContainers) {
        initializeContainerData(container);
    }
    for (CVehicleContainer& container : m_VehicleContainers) {
        initializeContainerData(container);
    }
    for (CAirShipContainer& container : m_AirShipContainers) {
        initializeContainerData(container);
    }
    m_nAirShipAllocatedCount = 0;
    for (CShuttleContainer& container : m_ShuttleContainers) {
        initializeContainerData(container);
    }
    m_nShuttleAllocatedCount = 0;

    if (containerId != kPrivateFirstId) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"container data size don't fit!")
        // L2_LOG_ERROR(Log,
        //     L"container data size don't fit!")
        L2_LOG_ERROR(
            Log,
            "World container initialization reached ID %d; expected first private-container ID %d. Initialization "
            "continues.",
            containerId, static_cast<int>(kPrivateFirstId)
        );
    }

    m_nPrivateStorageCount = maximumPrivateContainers + 1;
    m_nPrivateFreeTail = maximumPrivateContainers;
    m_pPrivateContainers = new CPrivateContainer[static_cast<std::size_t>(m_nPrivateStorageCount)];
    for (int index = 0; index < m_nPrivateStorageCount; ++index) {
        CPrivateContainer& container = m_pPrivateContainers[static_cast<std::size_t>(index)];
        container.m_nInstantMapIndex = index;
        container.m_nNextPrivateContainerIndex = index + 1;
        initializeContainerData(container);
    }
    m_pPrivateContainers[static_cast<std::size_t>(m_nPrivateStorageCount - 1)].m_nNextPrivateContainerIndex = 0;

    g_ptlCreatureMap->InitInstantMap(m_nPrivateStorageCount);
    g_ptlUserMap->InitInstantMap(m_nPrivateStorageCount);
    g_ptlItemMap->InitInstantMap(m_nPrivateStorageCount);
    g_ptlAirShipMap->InitInstantMap(m_nPrivateStorageCount);
    g_ptlStaticObjectMap->InitInstantMap(m_nPrivateStorageCount);

    m_nZSplitRegionCount = 0;
    for (int gridX = 0; gridX < 0x110; ++gridX) {
        for (int gridY = 0; gridY < 0x110; ++gridY) {
            CWorldContainer& primary = m_WorldContainers[static_cast<std::size_t>(gridX * 0x110 + gridY)];
            primary.m_nGridX = gridX;
            primary.m_nGridY = gridY;
            primary.m_nGridZ = -1;
            primary.m_nZDividedIndex = -1;

            if (gridX >= 213 && gridX <= 218 && gridY >= 133 && gridY <= 138) {
                primary.m_nZDividedIndex = m_nZSplitRegionCount;
                for (int gridZ = 0; gridZ < 0x20; ++gridZ) {
                    CWorldContainer& split =
                        m_ZSplitContainers[static_cast<std::size_t>(m_nZSplitRegionCount * 0x20 + gridZ)];
                    split.m_nGridX = gridX;
                    split.m_nGridY = gridY;
                    split.m_nGridZ = gridZ;
                }
                ++m_nZSplitRegionCount;
            }

            if (m_nZSplitRegionCount > 0x100) {
                runtime::process::FailCheckSizeInt();
            }
        }
    }

    for (CWorldContainer& container : m_ZSplitContainers) {
        container.m_nZDividedIndex = -1;
    }
    for (std::size_t index = 0; index < m_VehicleContainers.size(); ++index) {
        m_VehicleUsed[index] = false;
        m_VehicleContainers[index].m_nZDividedIndex = -1;
    }
    for (std::size_t index = 0; index < m_AirShipContainers.size(); ++index) {
        m_AirShipUsed[index] = false;
        m_AirShipContainers[index].m_nZDividedIndex = -1;
    }
    for (std::size_t index = 0; index < m_ShuttleContainers.size(); ++index) {
        m_ShuttleUsed[index] = false;
        m_ShuttleContainers[index].m_nZDividedIndex = -1;
    }

    // Log.Add(LOG_NORMAL, L"g_ContainerDB Created")
    // L2_LOG_INFO(Log, L"g_ContainerDB Created")
    L2_LOG_INFO(
        Log, "World containers initialized; private storage has %d entries including its sentinel.", m_nPrivateStorageCount
    );
}

CContainerIndex CContainerDB::GatherNeighborContainerIndex(CContainer* container) {
    L2_GUARD_FUNCTION();
    CContainerIndex result;
    if (container != nullptr) {
        container->GatherNeighborContainerIndex(result);
    }
    return result;
}

CContainerIndex CContainerDB::GatherNeighborWorldContainerIndex(CContainer* container) {
    L2_GUARD_FUNCTION();
    CContainerIndex result;
    int firstX = std::max(container->m_nGridX - 1, 0);
    firstX = std::min(firstX, 0x10f);
    const int lastX = std::min(container->m_nGridX + 1, 0x10f);
    int firstY = std::max(container->m_nGridY - 1, 0);
    firstY = std::min(firstY, 0x10f);
    const int lastY = std::min(container->m_nGridY + 1, 0x10f);

    int firstZ = 0;
    int lastZ = 0x1f;
    if (container->m_nGridZ >= 0) {
        firstZ = std::max(container->m_nGridZ - 1, 0);
        if (container->m_nGridZ + 1 < 0x1f) {
            lastZ = container->m_nGridZ + 1;
        }
    }

    for (int x = firstX; x <= lastX; ++x) {
        for (int y = firstY; y <= lastY; ++y) {
            if (x >= 0x110 || y >= 0x110) {
                continue;
            }
            CWorldContainer& primary = m_WorldContainers[static_cast<std::size_t>(x * 0x110 + y)];
            if (primary.m_nZDividedIndex < 0) {
                result.Add(&primary);
                continue;
            }
            for (int z = firstZ; z <= lastZ; ++z) {
                const std::size_t splitIndex = static_cast<std::size_t>(primary.m_nZDividedIndex * 0x20 + z);
                result.Add(&m_ZSplitContainers[splitIndex]);
            }
        }
    }
    return CContainerIndex(result);
}

bool CContainerDB::ContainerChanged(CObject* object, CContainer* source, CContainer* destination, bool force) {
    L2_GUARD_FUNCTION();

    if (source->IsDynamic() || destination->IsDynamic()) {
        return true;
    }

    CContainerIndex sourceNeighbors = GatherNeighborContainerIndex(source);
    CContainerIndex destinationNeighbors = GatherNeighborContainerIndex(destination);

    CContainerIndex leaving(sourceNeighbors);
    leaving.Substract(destinationNeighbors);
    CContainerIndex entering(destinationNeighbors);
    entering.Substract(sourceNeighbors);

    class ContainerFilter final : public CContainerFilter {
    public:
        explicit ContainerFilter(CObject* actor) : actor_(actor) {}

        bool CanDo(CObject* candidate) override { return candidate != actor_; }

    private:
        CObject* actor_;
    } filter(object);

    {
        class ContainerFunc final : public CContainerFunc {
        public:
            explicit ContainerFunc(CObject* actor) : actor_(actor) {}

            void Do(CObject* candidate) override {
                if (!CanUpdate(candidate) || !CanUpdate(actor_)) {
                    return;
                }
                candidate->OutOfSight(actor_, false);
                actor_->OutOfSight(candidate, false);
            }

        private:
            static bool CanUpdate(CObject* candidate) {
                return !candidate->IsPC() || (!candidate->IsDeleting() && candidate->m_Deleted == 0);
            }

            CObject* actor_;
        } function(object);

        FOR_ALL_OBJECT_IN_CONTAINER_INDEX__INSTANT(leaving, filter, function);
    }

    {
        class ContainerFunc final : public CContainerFunc {
        public:
            ContainerFunc(CObject* actor, bool force) : actor_(actor), force_(force) {}

            void Do(CObject* candidate) override {
                if (!CanUpdate(candidate) || !CanUpdate(actor_)) {
                    return;
                }
                candidate->InSight(actor_, force_);
                actor_->InSight(candidate, false);
            }

        private:
            static bool CanUpdate(CObject* candidate) {
                return !candidate->IsPC() || (!candidate->IsDeleting() && candidate->m_Deleted == 0);
            }

            CObject* actor_;
            bool force_;
        } function(object, force);

        FOR_ALL_OBJECT_IN_CONTAINER_INDEX__INSTANT(entering, filter, function);
    }
    return true;
}

CWorldContainer* CContainerDB::GetWorldContainer(int x, int y, int z) {
    L2_GUARD_FUNCTION();
    constexpr int kWorldXOrigin = -0x50000;
    constexpr int kWorldYOrigin = -0x40000;
    const std::uint32_t gridX = static_cast<std::uint32_t>((x - kWorldXOrigin) >> 11);
    const std::uint32_t gridY = static_cast<std::uint32_t>((y - kWorldYOrigin) >> 11);
    if (gridX >= 0x110U || gridY >= 0x111U) {
        return nullptr;
    }

    const std::size_t flat = static_cast<std::size_t>(gridX) * 0x110U + gridY;
    CWorldContainer* primary = flat < m_WorldContainers.size() ? &m_WorldContainers[flat] : &m_ZSplitContainers[0];
    if (primary->m_nZDividedIndex < 0) {
        return primary;
    }

    const std::uint32_t gridZ = static_cast<std::uint32_t>((z + 0x4000) >> 10);
    if (gridZ > 0x1fU) {
        runtime::process::FailCheckSizeInt();
    }
    const std::size_t splitIndex = static_cast<std::size_t>(primary->m_nZDividedIndex) * 0x20U + gridZ;
    return &m_ZSplitContainers[splitIndex];
}

CContainer* CContainerDB::GetContainer(int containerId) {
    L2_GUARD_FUNCTION();
    const std::uint32_t unsignedId = static_cast<std::uint32_t>(containerId);
    if (unsignedId < static_cast<std::uint32_t>(kWorldPrimaryCount)) {
        return &m_WorldContainers[unsignedId];
    }
    if (unsignedId - static_cast<std::uint32_t>(kWorldZSplitFirstId) < static_cast<std::uint32_t>(kWorldZSplitCount)) {
        return &m_ZSplitContainers[unsignedId - kWorldZSplitFirstId];
    }
    if (IsVehicleContainerId(containerId)) {
        return &m_VehicleContainers[unsignedId - kVehicleFirstId];
    }
    if (IsAirShipContainerId(containerId)) {
        return &m_AirShipContainers[unsignedId - kAirShipFirstId];
    }
    if (IsShuttleContainerId(containerId)) {
        return &m_ShuttleContainers[unsignedId - kShuttleFirstId];
    }
    if (containerId < kPrivateFirstId || containerId >= kPrivateFirstId + m_nPrivateStorageCount) {
        return nullptr;
    }
    return &m_pPrivateContainers[static_cast<std::size_t>(containerId - kPrivateFirstId)];
}

CContainer* CContainerDB::AllocVehicleContainer(int ownerId) {
    L2_GUARD_FUNCTION();
    CContainer* const result = [&]() -> CContainer* {
        L2_GUARD(m_VehicleLock);
        CContainer* result = nullptr;
        for (std::size_t index = 0; index < m_VehicleUsed.size(); ++index) {
            if (m_VehicleUsed[index]) {
                continue;
            }
            CVehicleContainer& container = m_VehicleContainers[index];
            container.m_Data.m_nCreatureFirst = -1;
            container.m_Data.m_nItemFirst = -1;
            container.m_Data.m_nVehicleFirst = -1;
            container.m_Data.m_nAirShipFirst = -1;
            container.m_Data.m_nShuttleFirst = -1;
            container.m_Data.m_nStaticObjectFirst = -1;
            container.m_Data.m_nParentContainerIndex = -1;
            container.m_Data.m_nOwnerId = 0;
            container.m_Data.m_nNext = -1;
            container.m_Data.m_nBefore = -1;
            container.m_Data.m_nOwnerId = ownerId;
            m_VehicleUsed[index] = true;
            result = &container;
            break;
        }
        return result;
    }();
    return result;
}

void CContainerDB::FreeVehicleContainer(CContainer* container) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_VehicleLock);
        for (std::size_t index = 0; index < m_VehicleContainers.size(); ++index) {
            if (&m_VehicleContainers[index] == container && m_VehicleUsed[index]) {
                m_VehicleUsed[index] = false;
                break;
            }
        }
    }
}

CContainer* CContainerDB::AllocAirShipContainer(int ownerId) {
    L2_GUARD_FUNCTION();
    CContainer* const result = [&]() -> CContainer* {
        L2_GUARD(m_AirShipLock);
        CContainer* result = nullptr;
        if (m_nAirShipAllocatedCount < kAirShipCount) {
            std::size_t index = 0;
            while (index < m_AirShipUsed.size() && m_AirShipUsed[index]) {
                ++index;
            }
            if (index < m_AirShipContainers.size()) {
                CAirShipContainer& container = m_AirShipContainers[index];
                container.m_Data.m_nCreatureFirst = -1;
                container.m_Data.m_nItemFirst = -1;
                container.m_Data.m_nVehicleFirst = -1;
                container.m_Data.m_nAirShipFirst = -1;
                container.m_Data.m_nShuttleFirst = -1;
                container.m_Data.m_nStaticObjectFirst = -1;
                container.m_Data.m_nParentContainerIndex = -1;
                container.m_Data.m_nOwnerId = ownerId;
                container.m_Data.m_nNext = -1;
                container.m_Data.m_nBefore = -1;
                m_AirShipUsed[index] = true;
                ++m_nAirShipAllocatedCount;
                result = &container;
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[AirShip] CContainerDB::AllocAirShipContainer error "
                //     L"[airship sid : %d] [Container id : %d / "
                //     L"MAX_AIRSHIP_CONTAINER : %d]",
                //     ownerId,
                //     static_cast<int>(index),
                //     kAirShipCount)
                // L2_LOG_ERROR(Log,
                //     L"[AirShip] CContainerDB::AllocAirShipContainer error "
                //     L"[airship sid : %d] [Container id : %d / "
                //     L"MAX_AIRSHIP_CONTAINER : %d]",
                //     ownerId,
                //     static_cast<int>(index),
                //     kAirShipCount)
                L2_LOG_ERROR(
                    Log,
                    "Cannot allocate container for airship object %u: scan reached index %zu, capacity %d, recorded "
                    "allocation count %d.",
                    static_cast<unsigned>(ownerId), index, static_cast<int>(kAirShipCount), m_nAirShipAllocatedCount
                );
            }
        }
        return result;
    }();
    return result;
}

void CContainerDB::FreeAirShipContainer(CContainer* container) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_AirShipLock);
        for (std::size_t index = 0; index < m_AirShipContainers.size(); ++index) {
            if (&m_AirShipContainers[index] == container && m_AirShipUsed[index]) {
                m_AirShipUsed[index] = false;
                --m_nAirShipAllocatedCount;
                break;
            }
        }
    }
}

bool CContainerDB::CanAllocAirShipContainer() {
    L2_GUARD_FUNCTION();
    const bool result = [&]() -> bool {
        L2_GUARD(m_AirShipLock);
        const bool result = m_nAirShipAllocatedCount < kAirShipCount;
        return result;
    }();
    return result;
}

CContainer* CContainerDB::AllocShuttleContainer(int ownerId) {
    L2_GUARD_FUNCTION();
    CContainer* const result = [&]() -> CContainer* {
        L2_GUARD(m_ShuttleLock);
        CContainer* result = nullptr;
        if (m_nShuttleAllocatedCount < kShuttleCount) {
            std::size_t index = 0;
            while (index < m_ShuttleUsed.size() && m_ShuttleUsed[index]) {
                ++index;
            }
            if (index < m_ShuttleContainers.size()) {
                CShuttleContainer& container = m_ShuttleContainers[index];
                container.m_Data.m_nCreatureFirst = -1;
                container.m_Data.m_nItemFirst = -1;
                container.m_Data.m_nVehicleFirst = -1;
                container.m_Data.m_nAirShipFirst = -1;
                container.m_Data.m_nShuttleFirst = -1;
                container.m_Data.m_nStaticObjectFirst = -1;
                container.m_Data.m_nParentContainerIndex = -1;
                container.m_Data.m_nOwnerId = ownerId;
                container.m_Data.m_nNext = -1;
                container.m_Data.m_nBefore = -1;
                m_ShuttleUsed[index] = true;
                ++m_nShuttleAllocatedCount;
                result = &container;
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[Shuttle] CContainerDB::AllocShuttleContainer error "
                //     L"[Shuttle sid : %d] [Container id : %d / "
                //     L"MAX_SHUTTLE_CONTAINER : %d]",
                //     ownerId,
                //     static_cast<int>(index),
                //     kShuttleCount)
                // L2_LOG_ERROR(Log,
                //     L"[Shuttle] CContainerDB::AllocShuttleContainer error "
                //     L"[Shuttle sid : %d] [Container id : %d / "
                //     L"MAX_SHUTTLE_CONTAINER : %d]",
                //     ownerId,
                //     static_cast<int>(index),
                //     kShuttleCount)
                L2_LOG_ERROR(
                    Log,
                    "Cannot allocate container for shuttle object %u: scan reached index %zu, capacity %d, recorded "
                    "allocation count %d.",
                    static_cast<unsigned>(ownerId), index, static_cast<int>(kShuttleCount), m_nShuttleAllocatedCount
                );
            }
        }
        return result;
    }();
    return result;
}

void CContainerDB::FreeShuttleContainer(CContainer* container) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_ShuttleLock);
        for (std::size_t index = 0; index < m_ShuttleContainers.size(); ++index) {
            if (&m_ShuttleContainers[index] == container && m_ShuttleUsed[index]) {
                m_ShuttleUsed[index] = false;
                --m_nShuttleAllocatedCount;
                break;
            }
        }
    }
}

CPrivateContainer* CContainerDB::AllocPrivateContainer(int) {
    L2_GUARD_FUNCTION();
    CPrivateContainer* const result = [&]() -> CPrivateContainer* {
        L2_GUARD(m_PrivateLock);
        if (m_nPrivateFreeHead == m_nPrivateFreeTail) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Insufficient instant container",
            //     L"SharedContainer.cpp",
            //     0x57c)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Insufficient instant container",
            //     L"SharedContainer.cpp",
            //     0x57c)
            L2_LOG_ERROR(
                Log, "Private container free list is empty (head %d, tail %d, storage %d); allocation returns null.",
                m_nPrivateFreeHead, m_nPrivateFreeTail, m_nPrivateStorageCount
            );
            return nullptr;
        }

        CPrivateContainer* const result = &m_pPrivateContainers[static_cast<std::size_t>(m_nPrivateFreeHead)];
        m_nPrivateFreeHead = result->m_nNextPrivateContainerIndex;
        result->m_nNextPrivateContainerIndex = -1;
        return result;
    }();
    return result;
}

void CContainerDB::FreePrivateContainer(CPrivateContainer* container) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_PrivateLock);
        CPrivateContainer& tail = m_pPrivateContainers[static_cast<std::size_t>(m_nPrivateFreeTail)];
        container->m_nNextPrivateContainerIndex = tail.m_nNextPrivateContainerIndex;
        tail.m_nNextPrivateContainerIndex = container->m_nInstantMapIndex;
        m_nPrivateFreeTail = container->m_nInstantMapIndex;
    }
}

bool CContainerDB::IsVehicleContainerId(int containerId) noexcept {
    return static_cast<std::uint32_t>(containerId - kVehicleFirstId) < static_cast<std::uint32_t>(kVehicleCount);
}

bool CContainerDB::IsAirShipContainerId(int containerId) noexcept {
    return static_cast<std::uint32_t>(containerId - kAirShipFirstId) < static_cast<std::uint32_t>(kAirShipCount);
}

bool CContainerDB::IsShuttleContainerId(int containerId) noexcept {
    return static_cast<std::uint32_t>(containerId - kShuttleFirstId) < static_cast<std::uint32_t>(kShuttleCount);
}

int CContainerDB::GetVehicleID(int containerId) {
    L2_GUARD_FUNCTION();
    CContainer* const container = IsVehicleContainerId(containerId) ? GetContainer(containerId) : nullptr;
    return container != nullptr ? container->m_Data.m_nOwnerId : 0;
}

int CContainerDB::GetAirShipID(int containerId) {
    L2_GUARD_FUNCTION();
    CContainer* const container = IsAirShipContainerId(containerId) ? GetContainer(containerId) : nullptr;
    return container != nullptr ? container->m_Data.m_nOwnerId : 0;
}

int CContainerDB::GetShuttleID(int containerId) {
    L2_GUARD_FUNCTION();
    CContainer* const container = IsShuttleContainerId(containerId) ? GetContainer(containerId) : nullptr;
    return container != nullptr ? container->m_Data.m_nOwnerId : 0;
}
