#include "ThreadLocalWorldMap.h"

#include "CGeneralObject.h"
#include "Door.h"
#include "InstantZone.h"
#include "Party.h"
#include "SharedContainer.h"
#include "User.h"
#include "UserSocket.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Exception.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/Territory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/TimeAnalysis.h"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>

namespace {

constexpr int kWorldLeft = -0x50000;
constexpr int kWorldTop = -0x40000;
constexpr int kWorldMaxX = 0x27f;
constexpr int kWorldMaxY = 0x21f;
constexpr int kWorldMaxZ = 0x1f;
constexpr int kWorldCellSize = 0x400;
constexpr int kWorldZOffset = 0x4000;

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
constexpr const wchar_t* RemoveFunctionName() {
    static_assert(ThreadCount == 16);
    if constexpr (MaximumObjects == 120000 && PipeRecords == 600000) {
        return L"void __cdecl CThreadLocalWorldMap2<120000,600000,16>::Remove(int,class CObject *)";
    } else if constexpr (MaximumObjects == 900000 && PipeRecords == 112500) {
        return L"void __cdecl CThreadLocalWorldMap2<900000,112500,16>::Remove(int,class CObject *)";
    } else if constexpr (MaximumObjects == 1000 && PipeRecords == 3000) {
        return L"void __cdecl CThreadLocalWorldMap2<1000,3000,16>::Remove(int,class CObject *)";
    } else if constexpr (MaximumObjects == 10001 && PipeRecords == 20002) {
        return L"void __cdecl CThreadLocalWorldMap2<10001,20002,16>::Remove(int,class CObject *)";
    } else {
        static_assert(MaximumObjects == 120000 && PipeRecords == 50000);
        return L"void __cdecl CThreadLocalWorldMap2<120000,50000,16>::Remove(int,class CObject *)";
    }
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
constexpr const wchar_t* RemoveDiagnosticName() {
    static_assert(ThreadCount == 16);
    if constexpr (MaximumObjects == 120000 && PipeRecords == 600000) {
        return L"CThreadLocalWorldMap2<120000,600000,16>::Remove";
    } else if constexpr (MaximumObjects == 900000 && PipeRecords == 112500) {
        return L"CThreadLocalWorldMap2<900000,112500,16>::Remove";
    } else if constexpr (MaximumObjects == 1000 && PipeRecords == 3000) {
        return L"CThreadLocalWorldMap2<1000,3000,16>::Remove";
    } else if constexpr (MaximumObjects == 10001 && PipeRecords == 20002) {
        return L"CThreadLocalWorldMap2<10001,20002,16>::Remove";
    } else {
        static_assert(MaximumObjects == 120000 && PipeRecords == 50000);
        return L"CThreadLocalWorldMap2<120000,50000,16>::Remove";
    }
}

struct WorldMapExactNames {
    const wchar_t* write2;
    const wchar_t* push;
    const wchar_t* mapInsert;
    const wchar_t* mapRemove;
};

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
constexpr WorldMapExactNames ExactWorldMapNames() {
    static_assert(ThreadCount == 16);
    if constexpr (MaximumObjects == 120000 && PipeRecords == 600000) {
        return {L"void __cdecl CThreadLocalWorldMap2<120000,600000,16>::Write2(short,short,char,int,class CObject *)",
            L"void __cdecl CThreadLocalWorldMap2<120000,600000,16>::Push(int,const class FVector &,class CObject *)",
            L"void __cdecl CThreadLocalWorldMap2<120000,600000,16>::CTL_Map2::Insert(const struct CTL_WorldMapNode2 &)",
            L"void __cdecl CThreadLocalWorldMap2<120000,600000,16>::CTL_Map2::Remove(const struct CTL_WorldMapNode2 &)"};
    } else if constexpr (MaximumObjects == 900000 && PipeRecords == 112500) {
        return {L"void __cdecl CThreadLocalWorldMap2<900000,112500,16>::Write2(short,short,char,int,class CObject *)",
            L"void __cdecl CThreadLocalWorldMap2<900000,112500,16>::Push(int,const class FVector &,class CObject *)",
            L"void __cdecl CThreadLocalWorldMap2<900000,112500,16>::CTL_Map2::Insert(const struct CTL_WorldMapNode2 &)",
            L"void __cdecl CThreadLocalWorldMap2<900000,112500,16>::CTL_Map2::Remove(const struct CTL_WorldMapNode2 &)"};
    } else if constexpr (MaximumObjects == 1000 && PipeRecords == 3000) {
        return {L"void __cdecl CThreadLocalWorldMap2<1000,3000,16>::Write2(short,short,char,int,class CObject *)",
            L"void __cdecl CThreadLocalWorldMap2<1000,3000,16>::Push(int,const class FVector &,class CObject *)",
            L"void __cdecl CThreadLocalWorldMap2<1000,3000,16>::CTL_Map2::Insert(const struct CTL_WorldMapNode2 &)",
            L"void __cdecl CThreadLocalWorldMap2<1000,3000,16>::CTL_Map2::Remove(const struct CTL_WorldMapNode2 &)"};
    } else if constexpr (MaximumObjects == 10001 && PipeRecords == 20002) {
        return {L"void __cdecl CThreadLocalWorldMap2<10001,20002,16>::Write2(short,short,char,int,class CObject *)",
            L"void __cdecl CThreadLocalWorldMap2<10001,20002,16>::Push(int,const class FVector &,class CObject *)",
            L"void __cdecl CThreadLocalWorldMap2<10001,20002,16>::CTL_Map2::Insert(const struct CTL_WorldMapNode2 &)",
            L"void __cdecl CThreadLocalWorldMap2<10001,20002,16>::CTL_Map2::Remove(const struct CTL_WorldMapNode2 &)"};
    } else {
        static_assert(MaximumObjects == 120000 && PipeRecords == 50000);
        return {L"void __cdecl CThreadLocalWorldMap2<120000,50000,16>::Write2(short,short,char,int,class CObject *)",
            L"void __cdecl CThreadLocalWorldMap2<120000,50000,16>::Push(int,const class FVector &,class CObject *)",
            L"void __cdecl CThreadLocalWorldMap2<120000,50000,16>::CTL_Map2::Insert(const struct CTL_WorldMapNode2 &)",
            L"void __cdecl CThreadLocalWorldMap2<120000,50000,16>::CTL_Map2::Remove(const struct CTL_WorldMapNode2 &)"};
    }
}

int CellIndex(double coordinate, int origin) { return (static_cast<int>(coordinate) - origin) / kWorldCellSize; }

int ZCellIndex(double coordinate) { return (static_cast<int>(coordinate) + kWorldZOffset) / kWorldCellSize; }

int ClampIndex(int value, int maximum) { return std::min(std::max(value, 0), maximum); }


DefaultIOBuffer* AssembleConstBuffer(const char* format, va_list arguments) {
    DefaultIOBuffer* const buffer = DefaultIOBuffer::Alloc();
    const int bodySize = ::VAssemble(reinterpret_cast<char*>(buffer->m_Buffer.data() + 2), 0x4ffe, format, arguments);
    const std::uint16_t bodySize16 = static_cast<std::uint16_t>(bodySize);
    if (bodySize16 == 0) {
        buffer->Release();
        return nullptr;
    }

    const std::uint16_t packetSize = static_cast<std::uint16_t>(bodySize16 + 2);
    std::memcpy(buffer->m_Buffer.data(), &packetSize, sizeof(packetSize));
    buffer->m_dwSize = packetSize;
    return buffer;
}

} // namespace

CAirShip* __SafeCastAirShip(CObject* const object) {
    try {
        return object->CastAirShip();
    } catch (...) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"__SafeCastAirShip saved the server...")
        // L2_LOG_ERROR(Log,
        //     L"__SafeCastAirShip saved the server...")
        L2_LOG_ERROR(Log, "Airship cast threw for object pointer %p; returning null.", static_cast<void*>(object));
        return nullptr;
    }
}

CNPC* __SafeCastNpc(CObject* const object) {
    try {
        return object->CastNPC();
    } catch (...) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"__SafeCastNpc saved the server...")
        // L2_LOG_ERROR(Log,
        //     L"__SafeCastNpc saved the server...")
        L2_LOG_ERROR(Log, "NPC cast threw for object pointer %p; returning null.", static_cast<void*>(object));
        return nullptr;
    }
}

CThreadLocalWorldMap2<120000, 600000, 16>* g_ptlCreatureMap = nullptr;
CUserThreadLocalWorldMap* g_ptlUserMap = nullptr;
CThreadLocalWorldMap2<900000, 112500, 16>* g_ptlItemMap = nullptr;
CThreadLocalWorldMap2<1000, 3000, 16>* g_ptlAirShipMap = nullptr;
CThreadLocalWorldMap2<10001, 20002, 16>* g_ptlStaticObjectMap = nullptr;

TLMIterator::TLMIterator() : Index(-1), p(nullptr) {}

TLMIterator::TLMIterator(int index, ITL_Map2* map) : Index(index), p(map) {}

CObject* TLMIterator::operator*() {
    if (Index == -1) {
        return nullptr;
    }
    if (Index < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] something wrong",
        //     L"ThreadLocalWorldMap.cpp",
        //     0xd8)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] something wrong",
        //     L"ThreadLocalWorldMap.cpp",
        //     0xd8)
        L2_LOG_ERROR(Log, "World-map iterator has invalid negative index %d; resetting to end and returning null.", Index);
        Index = -1;
        return nullptr;
    }
    return p->GetObject(Index);
}

void TLMIterator::operator++() { Index = p->Next(Index); }

CTLMFunc::~CTLMFunc() = default;

void CTLMFunc::Finish() {}

CTLMFilter::CTLMFilter(int inZoneId, double distance) : __nInZoneID(inZoneId), __distance(distance) {}

CTLMFilter::~CTLMFilter() = default;

int CTLMFilter::GetInstantMapIndex() {
    L2_GUARD_FUNCTION();

    if (__nInZoneID != 0) {
        CGeneralObject* const object = CGeneralObject::GetObjectW(static_cast<std::uint32_t>(__nInZoneID));
        CInstantZone* const instantZone = object != nullptr && object->IsInZone() ? object->CastInZone() : nullptr;
        if (instantZone != nullptr && instantZone->m_pContainer != nullptr) {
            return instantZone->m_pContainer->m_nInstantMapIndex;
        }
    }
    return -1;
}

double CTLMFilter::GetDistance() const { return __distance; }

CTLMFilterWithPos::CTLMFilterWithPos(int inZoneId, const FVector& position) : CTLMFilter(inZoneId), __vPos(position) {}

const FVector& CTLMFilterWithPos::GetPosition() const { return __vPos; }

CTLMFilterDist::CTLMFilterDist(int inZoneId, const FVector& position, double distance) :
    CTLMFilterWithPos(inZoneId, position), __dist(distance), __distCube(distance * distance) {}

CTLMFilterDist::CTLMFilterDist(CWorldObject* object, double distance) :
    CTLMFilterDist(static_cast<int>(object->GetInZoneID()), object->GetPos(), distance) {}

bool CTLMFilterDist::CanDo(CObject* object) {
    L2_GUARD_FUNCTION();

    CWorldObject* const worldObject = object->CastWorldObject();
    if (worldObject == nullptr) {
        return false;
    }

    const FVector position = worldObject->GetPos();
    const double dx = position.x - __vPos.x;
    const double dy = position.y - __vPos.y;
    __distance = dx * dx + dy * dy;
    return __distance <= __distCube;
}

double CTLMFilterDist::GetRadius() const { return __dist; }

CTLMFilterDistBetweenZ::CTLMFilterDistBetweenZ(
    int inZoneId, const FVector& position, double distance, int minZValue, int maxZValue
) :
    CTLMFilterDist(inZoneId, position, distance), minZ(minZValue), maxZ(maxZValue) {}

CTLMFilterDistBetweenZ::CTLMFilterDistBetweenZ(CWorldObject* object, double distance, int minZValue, int maxZValue) :
    CTLMFilterDist(object, distance), minZ(minZValue), maxZ(maxZValue) {}

bool CTLMFilterDistBetweenZ::CanDo(CObject* object) {
    L2_GUARD_FUNCTION();
    if (!object->IsCreature() || !CTLMFilterDist::CanDo(object)) {
        return false;
    }

    CCreature* const creature = object->CastCreature();
    const FVector position = creature->m_data->GetPos();
    return static_cast<int>(position.z) <= maxZ &&
        minZ <= static_cast<int>(creature->m_data->m_dCollisionHeight) + static_cast<int>(position.z);
}

CTLMFilterTerritory::CTLMFilterTerritory(int inZoneId, CTerritory* territory) :
    CTLMFilterDist(inZoneId, territory->GetCenterPos(), static_cast<double>(territory->GetMaxDistanceFromCenterBy2D())),
    __pTerritory(territory) {}

bool CTLMFilterTerritory::CanDo(CObject* object) {
    L2_GUARD_FUNCTION();
    CWorldObject* const worldObject = object->CastWorldObject();
    if (worldObject == nullptr || !CTLMFilterDist::CanDo(object)) {
        return false;
    }
    return __pTerritory->IsInside(worldObject->GetPos(), true);
}

CTLMFuncInTerritory::CTLMFuncInTerritory(int inZoneId, CTerritory* territory) :
    CTLMFilterDist(inZoneId, territory->GetCenterPos(), static_cast<double>(territory->GetMaxDistanceFromCenterBy2D())),
    __vTerritory(territory) {}

bool CTLMFuncInTerritory::CanDo(CObject* object) {
    if (!CTLMFilterDist::CanDo(object)) {
        return false;
    }
    CWorldObject* const worldObject = object->CastWorldObject();
    return worldObject != nullptr && __vTerritory->IsInside(worldObject->GetPos(), true);
}

CTLMFilterRect::CTLMFilterRect(int inZoneId, const FVector& position, double radius) :
    CTLMFilterWithPos(inZoneId, position), __radius(radius) {}

CTLMFilterRect::CTLMFilterRect(CWorldObject* object, double radius) :
    CTLMFilterRect(static_cast<int>(object->GetInZoneID()), object->GetPos(), radius) {}

bool CTLMFilterRect::CanDo(CObject*) {
    L2_GUARD_FUNCTION();
    return true;
}

double CTLMFilterRect::GetRadius() const { return __radius; }

CTLMFilterContainer::CTLMFilterContainer(int inZoneId, const FVector& position, CContainer* container) :
    CTLMFilterWithPos(inZoneId, position),
    __containerX(container == nullptr ? 0 : container->m_nGridX),
    __containerY(container == nullptr ? 0 : container->m_nGridY) {}

bool CTLMFilterContainer::CanDo(CObject*) {
    L2_GUARD_FUNCTION();
    return true;
}

CTLMFilterContainerWithSenderVisibility::CTLMFilterContainerWithSenderVisibility(
    CObject* sender, int inZoneId, const FVector& position, CContainer* container
) :
    CTLMFilterContainer(inZoneId, position, container), __pSender(sender) {}

bool CTLMFilterContainerWithSenderVisibility::CanDo(CObject* object) {
    L2_GUARD_FUNCTION();
    return object->IsCreature() && __pSender->CanBeSeenBy(*object->CastCreature());
}

CTLMFilterRange::CTLMFilterRange(int inZoneId, int minX, int maxX, int minY, int maxY) :
    CTLMFilter(inZoneId), __minx(minX), __maxx(maxX), __miny(minY), __maxy(maxY) {}

bool CTLMFilterRange::CanDo(CObject*) {
    L2_GUARD_FUNCTION();
    return true;
}

CAsyncSendConstBuffer::CAsyncSendConstBuffer(int senderObjectId, DefaultIOBuffer* buffer, int pendingWriteLimit) :
    m_nSenderID(senderObjectId), m_pBuffer(buffer), m_nAcceptablePendingWrite(pendingWriteLimit) {}

bool CAsyncSendConstBuffer::CanDo(CObject* object, double) {
    if (object->IsPC() && !object->IsDeleting() && object->m_Deleted == 0) {
        User* const user = object->CastUser();
        CUserSocket* const socket = user->GetSocket();
        if (static_cast<int>(user->m_nId) != m_nSenderID && socket->PendingWrite() >= m_nAcceptablePendingWrite) {
            return true;
        }
        m_pBuffer->AddRef();
        socket->AsyncSendConstBuffer(m_pBuffer);
    }
    return true;
}

CAsyncSendConstBufferExceptSelf::CAsyncSendConstBufferExceptSelf(
    int senderObjectId, DefaultIOBuffer* buffer, int pendingWriteLimit
) :
    CAsyncSendConstBuffer(senderObjectId, buffer, pendingWriteLimit) {}

bool CAsyncSendConstBufferExceptSelf::CanDo(CObject* object, double) {
    if (object->IsPC() && !object->IsDeleting() && object->m_Deleted == 0) {
        User* const user = object->CastUser();
        CUserSocket* const socket = user->GetSocket();
        if (static_cast<int>(user->m_nId) != m_nSenderID && socket->PendingWrite() < m_nAcceptablePendingWrite) {
            m_pBuffer->AddRef();
            socket->AsyncSendConstBuffer(m_pBuffer);
        }
    }
    return true;
}

AsyncSendConstBufferToMyParty::AsyncSendConstBufferToMyParty(
    int senderObjectId, DefaultIOBuffer* buffer, int pendingWriteLimit
) :
    CAsyncSendConstBuffer(senderObjectId, buffer, pendingWriteLimit) {}

bool AsyncSendConstBufferToMyParty::CanDo(CObject* object, double) {
    // Current L2Server 0x00ae9ac8.
    if (!object->IsPC() || object->IsDeleting() || object->m_Deleted != 0) {
        return false;
    }

    User* const user = object->CastUser();
    if (user == nullptr) {
        return false;
    }
    CParty* const party = user->GetParty();
    if (party == nullptr) {
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    if (!party->IsMember(static_cast<std::uint32_t>(m_nSenderID)) || socket->PendingWrite() >= m_nAcceptablePendingWrite) {
        // The current body performs this second lookup even though its
        // result is discarded.
        party->IsMember(static_cast<std::uint32_t>(m_nSenderID));
        return true;
    }

    m_pBuffer->AddRef();
    socket->AsyncSendConstBuffer(m_pBuffer);
    return true;
}

AsyncSendConstBufferToLawfulPVPMember::AsyncSendConstBufferToLawfulPVPMember(
    int senderObjectId, DefaultIOBuffer* buffer, int pendingWriteLimit
) :
    CAsyncSendConstBuffer(senderObjectId, buffer, pendingWriteLimit) {}

bool AsyncSendConstBufferToLawfulPVPMember::CanDo(CObject* object, double) {
    // Current L2Server 0x00aea89c.
    if (!object->IsPC() || object->IsDeleting() || object->m_Deleted != 0) {
        return false;
    }

    User* const receiver = object->CastUser();
    if (receiver == nullptr) {
        return false;
    }

    CCreature* const senderCreature = g_CreatureSmartIdManager.GetObjectW(m_nSenderID);
    User* const sender = senderCreature != nullptr && senderCreature->IsPC() ? senderCreature->CastUser() : nullptr;
    if (sender == nullptr) {
        return false;
    }

    int relation = 0;
    if (receiver->IsInDuel() && sender->IsInDuel() && (receiver->IsDuelEnemy(sender) || receiver->IsDuelAlly(sender))) {
        relation = 1;
    }
    if (receiver->IsOlympiadPlayer() && sender->IsOlympiadPlayer()) {
        relation = 2;
    }
    if (receiver->m_PledgeGameUser.IsPlayer() && sender->m_PledgeGameUser.IsPlayer()) {
        relation = 3;
    }
    if (relation == 0) {
        return false;
    }

    CUserSocket* const socket = receiver->GetSocket();
    if (socket->PendingWrite() < m_nAcceptablePendingWrite) {
        m_pBuffer->AddRef();
        socket->AsyncSendConstBuffer(m_pBuffer);
    }
    return true;
}

CAsyncSendConstBuffer2::CAsyncSendConstBuffer2(DefaultIOBuffer* buffer, int pendingWriteLimit) :
    m_pBuffer(buffer), m_nAcceptablePendingWrite(pendingWriteLimit) {}

bool CAsyncSendConstBuffer2::CanDo(CObject* object, double) {
    if (object->IsPC() && !object->IsDeleting() && object->m_Deleted == 0) {
        User* const user = object->CastUser();
        CUserSocket* const socket = user->GetSocket();
        if (socket->PendingWrite() < m_nAcceptablePendingWrite) {
            m_pBuffer->AddRef();
            socket->AsyncSendConstBuffer(m_pBuffer);
        }
    }
    return true;
}

void BROADCAST_SET_SIZE(
    const FVector& position, int distance, int& minX, int& maxX, int& minY, int& maxY, int& minZ, int& maxZ
) {
    const int x = static_cast<int>(position.x);
    const int y = static_cast<int>(position.y);
    const int z = static_cast<int>(position.z);
    minX = ClampIndex(CellIndex(x - distance, kWorldLeft), kWorldMaxX);
    maxX = ClampIndex(CellIndex(x + distance, kWorldLeft), kWorldMaxX);
    minY = ClampIndex(CellIndex(y - distance, kWorldTop), kWorldMaxY);
    maxY = ClampIndex(CellIndex(y + distance, kWorldTop), kWorldMaxY);
    minZ = ClampIndex(ZCellIndex(z - distance), kWorldMaxZ);
    maxZ = ClampIndex(ZCellIndex(z + distance), kWorldMaxZ);
}

void BROADCAST_SET_SIZE_FAST(
    const FVector& position, int distance, int& minX, int& maxX, int& minY, int& maxY, int& minZ, int& maxZ
) {
    const int z = static_cast<int>(position.z);
    minZ = ClampIndex(ZCellIndex(z - distance), kWorldMaxZ);
    maxZ = ClampIndex(ZCellIndex(z + distance), kWorldMaxZ);

    const int relativeX = static_cast<int>(position.x) - kWorldLeft;
    const int relativeY = static_cast<int>(position.y) - kWorldTop;
    const auto centered = [](int value) { return value / kWorldCellSize; };
    const auto halfCentered = [](int value) { return (value + 0x200) / kWorldCellSize; };

    if (distance < 0x201) {
        maxX = std::min(halfCentered(relativeX), kWorldMaxX);
        maxY = std::min(halfCentered(relativeY), kWorldMaxY);
        minX = std::max(maxX - 1, 0);
        minY = std::max(maxY - 1, 0);
        return;
    }

    if (distance < 0x401) {
        const int x = ClampIndex(centered(relativeX), kWorldMaxX);
        const int y = ClampIndex(centered(relativeY), kWorldMaxY);
        minX = std::max(x - 1, 0);
        maxX = std::min(x + 1, kWorldMaxX);
        minY = std::max(y - 1, 0);
        maxY = std::min(y + 1, kWorldMaxY);
        return;
    }

    if (distance < 0x601) {
        const int x = halfCentered(relativeX);
        const int y = halfCentered(relativeY);
        minX = std::max(x - 2, 0);
        maxX = std::min(x + 1, kWorldMaxX);
        minY = std::max(y - 2, 0);
        maxY = std::min(y + 1, kWorldMaxY);
        return;
    }

    if (distance < 0x801) {
        const int x = ClampIndex(centered(relativeX), kWorldMaxX);
        const int y = ClampIndex(centered(relativeY), kWorldMaxY);
        minX = std::max(x - 2, 0);
        maxX = std::min(x + 2, kWorldMaxX);
        minY = std::max(y - 2, 0);
        maxY = std::min(y + 2, kWorldMaxY);
        return;
    }

    if (distance < 0xa01) {
        const int x = halfCentered(relativeX);
        const int y = halfCentered(relativeY);
        minX = std::max(x - 3, 0);
        maxX = std::min(x + 2, kWorldMaxX);
        minY = std::max(y - 3, 0);
        maxY = std::min(y + 2, kWorldMaxY);
        return;
    }

    if (distance < 0xc01) {
        const int x = ClampIndex(centered(relativeX), kWorldMaxX);
        const int y = ClampIndex(centered(relativeY), kWorldMaxY);
        minX = std::max(x - 3, 0);
        maxX = std::min(x + 3, kWorldMaxX);
        minY = std::max(y - 3, 0);
        maxY = std::min(y + 3, kWorldMaxY);
        return;
    }

    if (distance < 0xe01) {
        const int x = halfCentered(relativeX);
        const int y = halfCentered(relativeY);
        minX = std::max(x - 4, 0);
        maxX = std::min(x + 3, kWorldMaxX);
        minY = std::max(y - 4, 0);
        maxY = std::min(y + 3, kWorldMaxY);
        return;
    }

    const int x = ClampIndex(centered(relativeX), kWorldMaxX);
    const int y = ClampIndex(centered(relativeY), kWorldMaxY);
    minX = std::max(x - 4, 0);
    maxX = std::min(x + 4, kWorldMaxX);
    minY = std::max(y - 4, 0);
    maxY = std::min(y + 4, kWorldMaxY);
}

int FOR_ALL_XYZ_INDEX(
    IThreadLocalWorldMap* map,
    int minX,
    int maxX,
    int minY,
    int maxY,
    int minZ,
    int maxZ,
    CTLMFilter& filter,
    CTLMFunc& function
) {
    // Current L2Server.exe 0x00ae9c5c publishes this exact guarded root.
    L2_GUARD_FUNCTION();
    int count = 0;
    map->Read();
    const int instantMapIndex = filter.GetInstantMapIndex();
    if (instantMapIndex == -1) {
        maxX = std::min(maxX, kWorldMaxX);
        maxY = std::min(maxY, kWorldMaxY);
        for (int x = minX; x <= maxX; ++x) {
            for (int y = minY; y <= maxY; ++y) {
                const int lastZ = map->IsZDivided(x, y) ? maxZ : minZ;
                for (int z = minZ; z <= lastZ; ++z) {
                    TLMIterator iterator =
                        map->GetIterator(static_cast<short>(x), static_cast<short>(y), static_cast<char>(z));
                    while (CObject* const object = *iterator) {
                        if (filter.CanDo(object)) {
                            if (!function.CanDo(object, filter.GetDistance())) {
                                return count;
                            }
                            ++count;
                        }
                        ++iterator;
                    }
                }
            }
        }
    } else {
        TLMIterator iterator = map->GetIterator(instantMapIndex);
        while (CObject* const object = *iterator) {
            if (filter.CanDo(object)) {
                if (!function.CanDo(object, filter.GetDistance())) {
                    return count;
                }
                ++count;
            }
            ++iterator;
        }
    }
    function.Finish();
    return count;
}

void FOR_ALL_DIST(IThreadLocalWorldMap* map, CTLMFilterDist* filter, CTLMFunc* function) {
    int minX;
    int maxX;
    int minY;
    int maxY;
    int minZ;
    int maxZ;
    BROADCAST_SET_SIZE_FAST(
        filter->GetPosition(), static_cast<int>(filter->GetRadius()), minX, maxX, minY, maxY, minZ, maxZ
    );
    FOR_ALL_XYZ_INDEX(map, minX, maxX, minY, maxY, minZ, maxZ, *filter, *function);
}

void FOR_ALL_RECT(IThreadLocalWorldMap* map, CTLMFilterRect* filter, CTLMFunc* function) {
    int minX;
    int maxX;
    int minY;
    int maxY;
    int minZ;
    int maxZ;
    BROADCAST_SET_SIZE(filter->GetPosition(), static_cast<int>(filter->GetRadius()), minX, maxX, minY, maxY, minZ, maxZ);
    FOR_ALL_XYZ_INDEX(map, minX, maxX, minY, maxY, minZ, maxZ, *filter, *function);
}

void FOR_ALL_NEIGHBOR_USER_DIST(CTLMFilterDist* filter, CTLMFunc* function) {
    g_Timer.BeginJob(JOB_FOR_ALL_NEIGHBOR_USER_DIST);
    IThreadLocalWorldMap* const map = g_ptlUserMap == nullptr ? nullptr : static_cast<IThreadLocalWorldMap*>(g_ptlUserMap);
    FOR_ALL_DIST(map, filter, function);
    g_Timer.EndJob(JOB_FOR_ALL_NEIGHBOR_USER_DIST);
}

void FOR_ALL_NEIGHBOR_CREATURE_DIST(CTLMFilterDist* filter, CTLMFunc* function) {
    g_Timer.BeginJob(JOB_FOR_ALL_NEIGHBOR_CREATURE_DIST);
    IThreadLocalWorldMap* const map =
        g_ptlCreatureMap == nullptr ? nullptr : static_cast<IThreadLocalWorldMap*>(g_ptlCreatureMap);
    FOR_ALL_DIST(map, filter, function);
    g_Timer.EndJob(JOB_FOR_ALL_NEIGHBOR_CREATURE_DIST);
}

void FOR_ALL_NEIGHBOR_CREATURE_RANGE(CTLMFilterRange* filter, CTLMFunc* function) {
    g_Timer.BeginJob(JOB_FOR_ALL_NEIGHBOR_CREATURE_RANGE);
    const int minX = ClampIndex(CellIndex(filter->__minx, kWorldLeft), kWorldMaxX);
    const int maxX = ClampIndex(CellIndex(filter->__maxx, kWorldLeft), kWorldMaxX);
    const int minY = ClampIndex(CellIndex(filter->__miny, kWorldTop), kWorldMaxY);
    const int maxY = ClampIndex(CellIndex(filter->__maxy, kWorldTop), kWorldMaxY);
    IThreadLocalWorldMap* const map =
        g_ptlCreatureMap == nullptr ? nullptr : static_cast<IThreadLocalWorldMap*>(g_ptlCreatureMap);
    FOR_ALL_XYZ_INDEX(map, minX, maxX, minY, maxY, 0, kWorldMaxZ, *filter, *function);
    g_Timer.EndJob(JOB_FOR_ALL_NEIGHBOR_CREATURE_RANGE);
}

void FOR_ALL_NEIGHBOR_CREATURE_RECT(CTLMFilterRect* filter, CTLMFunc* function) {
    g_Timer.BeginJob(JOB_FOR_ALL_NEIGHBOR_CREATURE_RECT);
    IThreadLocalWorldMap* const map =
        g_ptlCreatureMap == nullptr ? nullptr : static_cast<IThreadLocalWorldMap*>(g_ptlCreatureMap);
    FOR_ALL_RECT(map, filter, function);
    g_Timer.EndJob(JOB_FOR_ALL_NEIGHBOR_CREATURE_RECT);
}

void FOR_ALL_NEIGHBOR_USER_RECT(CTLMFilterRect* filter, CTLMFunc* function) {
    g_Timer.BeginJob(JOB_FOR_ALL_NEIGHBOR_USER_RECT);
    IThreadLocalWorldMap* const map = g_ptlUserMap == nullptr ? nullptr : static_cast<IThreadLocalWorldMap*>(g_ptlUserMap);
    FOR_ALL_RECT(map, filter, function);
    g_Timer.EndJob(JOB_FOR_ALL_NEIGHBOR_USER_RECT);
}

void FOR_ALL_NEIGHBOR_USER_IN_TERRITORY(CTLMFilterTerritory* filter, CTLMFunc* function) {
    FOR_ALL_NEIGHBOR_USER_DIST(filter, function);
}

void FOR_ALL_NEIGHBOR_USER_RANGE(CTLMFilterRange& filter, CTLMFunc& function) {
    g_Timer.BeginJob(JOB_FOR_ALL_NEIGHBOR_USER_RANGE);
    const int minX = ClampIndex(CellIndex(filter.__minx, kWorldLeft), kWorldMaxX);
    const int maxX = ClampIndex(CellIndex(filter.__maxx, kWorldLeft), kWorldMaxX);
    const int minY = ClampIndex(CellIndex(filter.__miny, kWorldTop), kWorldMaxY);
    const int maxY = ClampIndex(CellIndex(filter.__maxy, kWorldTop), kWorldMaxY);
    IThreadLocalWorldMap* const map = g_ptlUserMap == nullptr ? nullptr : static_cast<IThreadLocalWorldMap*>(g_ptlUserMap);
    FOR_ALL_XYZ_INDEX(map, minX, maxX, minY, maxY, 0, kWorldMaxZ, filter, function);
    g_Timer.EndJob(JOB_FOR_ALL_NEIGHBOR_USER_RANGE);
}

void FOR_ALL_NEIGHBOR_ITEM_RECT(CTLMFilterRect* filter, CTLMFunc* function) {
    g_Timer.BeginJob(JOB_FOR_ALL_NEIGHBOR_ITEM_RECT);
    IThreadLocalWorldMap* const map = g_ptlItemMap == nullptr ? nullptr : static_cast<IThreadLocalWorldMap*>(g_ptlItemMap);
    FOR_ALL_RECT(map, filter, function);
    g_Timer.EndJob(JOB_FOR_ALL_NEIGHBOR_ITEM_RECT);
}

void FOR_ALL_NEIGHBOR_STATIC_OBJECT_DIST(CTLMFilterDist* filter, CTLMFunc* function) {
    IThreadLocalWorldMap* const map =
        g_ptlStaticObjectMap == nullptr ? nullptr : static_cast<IThreadLocalWorldMap*>(g_ptlStaticObjectMap);
    FOR_ALL_DIST(map, filter, function);
}

int FOR_ALL_NEIGHBOR_CONTAINER(CTLMFilterContainer& filter, CTLMFunc& function) {
    g_Timer.BeginJob(JOB_FOR_ALL_NEIGHBOR_CONTAINER);
    int minX = 0;
    int maxX = 0;
    int minY = 0;
    int maxY = 0;
    int minZ = 0;
    int maxZ = 0;
    if (filter.__nInZoneID < 1) {
        const int containerX = (filter.__containerX - 1) * 2;
        const int containerY = filter.__containerY * 2;
        minX = std::max(containerX, 0);
        maxX = std::min(containerX + 5, kWorldMaxX);
        minY = std::max(containerY - 2, 0);
        maxY = std::min(containerY + 3, kWorldMaxY);
        const int z = runtime::Win64CvttDoubleToInt32(filter.__vPos.z);
        minZ = ClampIndex((z + 0x3800) / kWorldCellSize, kWorldMaxZ);
        maxZ = ClampIndex((z + 0x4800) / kWorldCellSize, kWorldMaxZ);
    }

    IThreadLocalWorldMap* const map = g_ptlUserMap == nullptr ? nullptr : static_cast<IThreadLocalWorldMap*>(g_ptlUserMap);
    const int count = FOR_ALL_XYZ_INDEX(map, minX, maxX, minY, maxY, minZ, maxZ, filter, function);
    g_Timer.EndJob(JOB_FOR_ALL_NEIGHBOR_CONTAINER);
    return count;
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::CTL_Map2::CTL_Map2() :
    m_pInstantHead(nullptr), m_nInstantMapCount(0) {
    for (MapNode& node : m_Nodes) {
        node.object = nullptr;
        node.previous = -1;
        node.next = -1;
        node.idx = -1;
        node.idy = -1;
        node.idz = -1;
        node.instantMapIndex = -1;
    }
    m_Head.fill(-1);
    m_ZHead.fill(-1);

    struct ZDividedRect {
        int minX;
        int maxX;
        int minY;
        int maxY;
    };
    constexpr ZDividedRect dividedRects[] = {
        {100000, 0x1d204, 0x2c88, 0x50dc},
        {0x31bb9, 0x36965, 0x19c36, 0x1e89e},
        {0x29988, 0x2fa82, -0x1dcd5, -0x19443},
    };

    int dividedIndex = 0;
    for (const ZDividedRect& rect : dividedRects) {
        const int minX = ClampIndex((rect.minX - kWorldLeft) / kWorldCellSize, kWorldMaxX);
        const int maxX = ClampIndex((rect.maxX - kWorldLeft) / kWorldCellSize, kWorldMaxX);
        const int minY = ClampIndex((rect.minY - kWorldTop) / kWorldCellSize, kWorldMaxY);
        const int maxY = ClampIndex((rect.maxY - kWorldTop) / kWorldCellSize, kWorldMaxY);
        for (int x = minX; x <= maxX; ++x) {
            for (int y = minY; y <= maxY; ++y) {
                m_Head[static_cast<std::size_t>(x) * 0x220U + static_cast<std::size_t>(y)] = -2 - dividedIndex;
                ++dividedIndex;
            }
        }
    }
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::CTL_Map2::~CTL_Map2() {
    std::free(m_pInstantHead);
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
int CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::CTL_Map2::Next(int index) {
    return m_Nodes[static_cast<std::size_t>(index)].next;
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
CObject* CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::CTL_Map2::GetObject(int index) {
    return m_Nodes[static_cast<std::size_t>(index)].object;
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
std::int32_t& CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::CTL_Map2::Head(
    std::int16_t x, std::int16_t y, std::int8_t z
) {
    std::int32_t& head = m_Head[static_cast<std::size_t>(x) * 0x220U + static_cast<std::size_t>(y)];
    if (z < 0) {
        return head;
    }
    return m_ZHead[static_cast<std::size_t>(-head - 2) * 0x20U + static_cast<std::uint8_t>(z)];
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
const std::int32_t& CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::CTL_Map2::Head(
    std::int16_t x, std::int16_t y, std::int8_t z
) const {
    const std::int32_t& head = m_Head[static_cast<std::size_t>(x) * 0x220U + static_cast<std::size_t>(y)];
    if (z < 0) {
        return head;
    }
    return m_ZHead[static_cast<std::size_t>(-head - 2) * 0x20U + static_cast<std::uint8_t>(z)];
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
void CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::CTL_Map2::Insert(const CTL_WorldMapNode2& value) {
    L2_GUARD_FUNCTION();
    const std::size_t index = static_cast<std::size_t>(value.index);
    MapNode& node = m_Nodes[index];

    if (node.instantMapIndex == -1) {
        if (node.idx != -1 && node.idy != -1) {
            if (Head(node.idx, node.idy, node.idz) == value.index) {
                Head(node.idx, node.idy, node.idz) = node.next;
            }
        }
    } else if (node.idx != -1 && node.idy != -1) {
        if (m_pInstantHead[node.instantMapIndex] == value.index) {
            m_pInstantHead[node.instantMapIndex] = node.next;
        }
    }

    if (node.previous != -1) {
        m_Nodes[static_cast<std::size_t>(node.previous)].next = node.next;
    }
    if (node.next != -1) {
        m_Nodes[static_cast<std::size_t>(node.next)].previous = node.previous;
    }

    const std::int32_t newInstantMapIndex = value.instantMapIndex;
    const std::int32_t previousHead =
        newInstantMapIndex == -1 ? Head(value.idx, value.idy, value.idz) : m_pInstantHead[newInstantMapIndex];

    if (previousHead != -1 && m_Nodes[static_cast<std::size_t>(previousHead)].instantMapIndex != newInstantMapIndex &&
        value.object->IsDoor()) {
        CDoor* const door = value.object->CastDoor();
        const std::string doorName = Utf16ToUtf8(door->GetDoorName());
        const int threadId = CThreadManager::GetThreadID();
        // Log.Add(
        //     LOG_ERROR,
        //     L"Door [%s]'s oldHead num is weird. g_tlThreadID = %d",
        //     doorName.c_str(),
        //     threadId)
        // L2_LOG_ERROR(Log,
        //     L"Door [%s]'s oldHead num is weird. g_tlThreadID = %d",
        //     doorName.c_str(),
        //     threadId)
        L2_LOG_ERROR(
            Log,
            "Door '%s': world-map head %d belongs to instance %d, incoming instance %d on worker %d; insertion continues.",
            doorName.c_str(), previousHead, m_Nodes[static_cast<std::size_t>(previousHead)].instantMapIndex,
            newInstantMapIndex, threadId
        );
    }

    node.previous = -1;
    node.next = previousHead;
    if (previousHead != -1) {
        m_Nodes[static_cast<std::size_t>(previousHead)].previous = value.index;
    }
    if (value.instantMapIndex == -1) {
        Head(value.idx, value.idy, value.idz) = value.index;
    } else {
        m_pInstantHead[value.instantMapIndex] = value.index;
    }
    node.idx = value.idx;
    node.idy = value.idy;
    node.idz = value.idz;
    node.instantMapIndex = value.instantMapIndex;
    node.object = value.object;
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
void CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::CTL_Map2::Remove(const CTL_WorldMapNode2& value) {
    L2_GUARD_FUNCTION();
    const std::uint32_t unsignedIndex = static_cast<std::uint32_t>(value.index);
    if (unsignedIndex >= MaximumObjects) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid index 0>[%d]>%d",
        //     L"c:\\work\\lineage2build\\server\\main\\l2server\\ThreadLocalWorldMap.h",
        //     0x2cc,
        //     value.index,
        //     static_cast<int>(MaximumObjects))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid index 0>[%d]>%d",
        //     L"c:\\work\\lineage2build\\server\\main\\l2server\\ThreadLocalWorldMap.h",
        //     0x2cc,
        //     value.index,
        //     static_cast<int>(MaximumObjects))
        L2_LOG_ERROR(Log, "World-map removal rejected: shared index %d is outside 0..%zu.", value.index, MaximumObjects - 1);
        return;
    }

    MapNode& node = m_Nodes[unsignedIndex];
    if (node.idx == -1 || node.idy == -1) {
        // Noise: OnRead skips the insertion of an object that is already
        // deleted, for example an NPC removed right after it spawns. The
        // creature and airship delete paths set m_Deleted before they queue
        // the removal, so the removal of such an object finds no link. This is
        // expected. Report only a live object. A repeated removal is reported
        // by Remove(sharedIndex, object) through the thread-local reference count.
        if (value.object != nullptr && value.object->m_Deleted != 0) {
            return;
        }
        // make the existing one-shot diagnostic safe across
        // IO workers. Map mutation and the queued object's Release are unchanged.
        // static bool reported = false.
        // if (!reported) {
        //     reported = true
        static std::atomic_flag reported = ATOMIC_FLAG_INIT;
        if (!reported.test_and_set(std::memory_order_relaxed)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] removing not linked item",
            //     L"c:\\work\\lineage2build\\server\\main\\l2server\\ThreadLocalWorldMap.h",
            //     0x2d8)
            // include the map, object identity and local node
            // state. The queued object reference is held until OnRead releases it.
            // do not dereference the possibly stale local node.object pointer.
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] removing not linked item",
            //     L"c:\\work\\lineage2build\\server\\main\\l2server\\ThreadLocalWorldMap.h",
            //     0x2d8)
            constexpr const char* mapName = MaximumObjects == 900000 ? "item"
                : MaximumObjects == 1000                             ? "airship"
                : MaximumObjects == 10001                            ? "static object"
                : PipeRecords == 600000                              ? "creature"
                                                                     : "user";
            // L2_LOG_ERROR(Log,
            //     "[world map] Removal requested for an unlinked %s entry: shared index %d, worker %d. Queued object %p (ID
            //     %u, class ID %d, deleted flag %d), local object %p. Local cell (%d,%d,%d), links previous=%d next=%d,
            //     instance index queued=%d local=%d. Unlink skipped. The queued reference is still released by OnRead.
            //     Possible causes include a repeated removal or an insertion skipped because the object was already deleted.
            //     This report and stack are emitted once per map type.", mapName, value.index,
            //     CThreadManager::GetThreadID(),
            //     static_cast<void*>(value.object),
            //     value.object != nullptr ? value.object->Id() : 0U,
            //     value.object != nullptr ? value.object->m_nClassId : 0,
            //     value.object != nullptr ? value.object->m_Deleted : 0,
            //     static_cast<void*>(node.object),
            //     static_cast<int>(node.idx),
            //     static_cast<int>(node.idy),
            //     static_cast<int>(node.idz),
            //     node.previous,
            //     node.next,
            //     value.instantMapIndex,
            //     node.instantMapIndex)
            L2_LOG_ERROR(
                Log,
                "World-map %s removal skipped: shared index %d is unlinked (worker %d). Queued object %p, ID %u, class %d, "
                "deleted=%d; local object %p, cell (%d,%d,%d), previous=%d next=%d, instance queued=%d local=%d. OnRead "
                "still releases the queued reference. Reported once per map type.",
                mapName, value.index, CThreadManager::GetThreadID(), static_cast<void*>(value.object),
                value.object != nullptr ? value.object->Id() : 0U, value.object != nullptr ? value.object->m_nClassId : 0,
                value.object != nullptr ? value.object->m_Deleted : 0, static_cast<void*>(node.object),
                static_cast<int>(node.idx), static_cast<int>(node.idy), static_cast<int>(node.idz), node.previous, node.next,
                value.instantMapIndex, node.instantMapIndex
            );
            ReportCallStack(LOG_ERROR, false);
        }
        return;
    }

    if (node.instantMapIndex == -1) {
        if (Head(node.idx, node.idy, node.idz) == value.index) {
            Head(node.idx, node.idy, node.idz) = node.next;
        }
    } else if (m_pInstantHead[node.instantMapIndex] == value.index) {
        m_pInstantHead[node.instantMapIndex] = node.next;
    }
    if (node.previous != -1) {
        m_Nodes[static_cast<std::size_t>(node.previous)].next = node.next;
    }
    if (node.next != -1) {
        m_Nodes[static_cast<std::size_t>(node.next)].previous = node.previous;
    }

    node.idx = -1;
    node.idy = -1;
    node.previous = -1;
    node.next = -1;
    node.instantMapIndex = -1;
    node.object = nullptr;
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
void CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::CTL_Map2::InitInstantMap(int instantMapCount) {
    m_nInstantMapCount = instantMapCount;
    m_pInstantHead =
        static_cast<std::int32_t*>(std::malloc(static_cast<std::size_t>(instantMapCount) * sizeof(std::int32_t)));
    for (int index = 0; index < instantMapCount; ++index) {
        m_pInstantHead[index] = -1;
    }
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
bool CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::CTL_Map2::IsZDivided(int x, int y) const {
    return m_Head[static_cast<std::size_t>(x) * 0x220U + static_cast<std::size_t>(y)] < -1;
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
TLMIterator CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::CTL_Map2::GetIterator(int instantMapIndex) {
    if (instantMapIndex < 0 || instantMapIndex >= m_nInstantMapCount) {
        return TLMIterator{};
    }
    return TLMIterator{m_pInstantHead[instantMapIndex], this};
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
TLMIterator CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::CTL_Map2::GetIterator(
    short x, short y, char z
) {
    const std::int32_t regularHead = m_Head[static_cast<std::size_t>(x) * 0x220U + static_cast<std::size_t>(y)];
    const int head = regularHead < -1
        ? m_ZHead[static_cast<std::size_t>(-regularHead - 2) * 0x20U + static_cast<std::uint8_t>(z)]
        : regularHead;
    return TLMIterator{head, this};
}

// GCC 13 rejects the explicit instantiations below for an out-of-class
// "= default" definition. An empty body keeps the same user-provided
// constructor, so new without parentheses still does not zero the storage.
template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::CThreadLocalWorldMap2() {}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
void CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::Initialize(const wchar_t* name) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_NORMAL,
    //     L"initializing TLM[%s] [%04d]MB",
    //     name,
    //     static_cast<int>(sizeof(*this) / (1024U * 1024U)))
    // L2_LOG_INFO(Log,
    //     L"initializing TLM[%s] [%04d]MB",
    //     name,
    //     static_cast<int>(sizeof(*this) / (1024U * 1024U)))
    L2_LOG_INFO(
        Log,
        "Initializing thread-local world map '%s': object storage %zu bytes (%.1f MiB), capacity %zu, worker count %zu.",
        name ? WideToUtf8(name).c_str() : "<unnamed>", sizeof(*this), static_cast<double>(sizeof(*this)) / (1024.0 * 1024.0),
        MaximumObjects, ThreadCount
    );
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
void CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::InitInstantMap(int instantMapCount) {
    L2_GUARD_FUNCTION();
    const int threadCount = CThreadManager::GetThreadCount();
    for (int thread = 0; thread < threadCount; ++thread) {
        m_Map[static_cast<std::size_t>(thread)].InitInstantMap(instantMapCount);
    }
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
void CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::Write2(
    short x, short y, char z, int sharedIndex, CObject* object
) {
    L2_GUARD_FUNCTION();
    const int threadCount = CThreadManager::GetThreadCount();
    for (int thread = 0; thread < threadCount; ++thread) {
        CTL_WorldMapNode2 node;
        const int instantMapIndex = object->GetInstantZoneIndex();
        node.idx = x;
        node.idy = y;
        node.idz = static_cast<std::int8_t>(z);
        node.index = sharedIndex;
        node.object = object;
        node.instantMapIndex = instantMapIndex;
        PipeBase::Write(thread, node);
    }
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
void CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::Push(
    int sharedIndex, const FVector& position, CObject* object
) {
    L2_GUARD_FUNCTION();

    const std::int32_t xOffset =
        runtime::SubtractInt32Wrapping(runtime::Win64CvttDoubleToInt32(position.x), kWorldLeft);
    const std::int32_t yOffset =
        runtime::SubtractInt32Wrapping(runtime::Win64CvttDoubleToInt32(position.y), kWorldTop);
    const short x = static_cast<short>(std::clamp(xOffset / kWorldCellSize, 0, kWorldMaxX));
    const short y = static_cast<short>(std::clamp(yOffset / kWorldCellSize, 0, kWorldMaxY));

    const int threadId = CThreadManager::GetThreadID();
    if (threadId >= static_cast<int>(ThreadCount)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"g_tlThreadID[%d] should be less then nThreadCount[%d]",
        //     threadId,
        //     static_cast<int>(ThreadCount))
        // L2_LOG_ERROR(Log,
        //     L"g_tlThreadID[%d] should be less then nThreadCount[%d]",
        //     threadId,
        //     static_cast<int>(ThreadCount))
        L2_LOG_ERROR(
            Log, "Cannot gather world-map neighbors: worker %d is outside the configured worker count %zu.", threadId,
            ThreadCount
        );
        return;
    }

    char z = static_cast<char>(-1);
    if (m_Map[static_cast<std::size_t>(threadId)].IsZDivided(x, y)) {
        const std::int32_t zOffset =
            runtime::AddInt32Wrapping(runtime::Win64CvttDoubleToInt32(position.z), kWorldZOffset);
        z = static_cast<char>(std::clamp(zOffset / kWorldCellSize, 0, kWorldMaxZ));
    }
    Write2(x, y, z, sharedIndex, object);
}

template <>
void CThreadLocalWorldMap2<900000, 112500, 16>::Push(int sharedIndex, const FVector& position, CObject* object) {
    L2_GUARD_FUNCTION();

    const std::int32_t xOffset = runtime::Int32FromBits(
        static_cast<std::uint32_t>(runtime::Win64CvttDoubleToInt32(position.x)) - static_cast<std::uint32_t>(kWorldLeft)
    );
    const std::int32_t yOffset = runtime::Int32FromBits(
        static_cast<std::uint32_t>(runtime::Win64CvttDoubleToInt32(position.y)) - static_cast<std::uint32_t>(kWorldTop)
    );
    const short x = static_cast<short>(std::clamp(xOffset / kWorldCellSize, 0, kWorldMaxX));
    const short y = static_cast<short>(std::clamp(yOffset / kWorldCellSize, 0, kWorldMaxY));

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

    char z = static_cast<char>(-1);
    if (m_Map[static_cast<std::size_t>(threadId)].IsZDivided(x, y)) {
        const std::int32_t zOffset = runtime::Int32FromBits(
            static_cast<std::uint32_t>(runtime::Win64CvttDoubleToInt32(position.z)) +
            static_cast<std::uint32_t>(kWorldZOffset)
        );
        z = static_cast<char>(std::clamp(zOffset / kWorldCellSize, 0, kWorldMaxZ));
    }
    Write2(x, y, z, sharedIndex, object);
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
void CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::Remove(int sharedIndex, CObject* object) {
    L2_GUARD_FUNCTION();
    if (object->DecreaseTLRef() < 0) {
        const int classId = object->GetClassId();
        const int threadId = CThreadManager::GetThreadID();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[tid: %d, class_id: %d]%s was called 2 times",
        //     threadId,
        //     classId,
        //     RemoveDiagnosticName<
        //         MaximumObjects,
        //         PipeRecords,
        //         ThreadCount>())
        // L2_LOG_ERROR(Log,
        //     L"[tid: %d, class_id: %d]%s was called 2 times",
        //     threadId,
        //     classId,
        //     RemoveDiagnosticName<
        //         MaximumObjects,
        //         PipeRecords,
        //         ThreadCount>())
        L2_LOG_ERROR(
            Log,
            "World-map removal '%s': object class %d shared index %d has a negative thread-local reference count on worker "
            "%d; reporting references and queueing removal.",
            WideToUtf8(RemoveDiagnosticName<MaximumObjects, PipeRecords, ThreadCount>()).c_str(), classId, sharedIndex,
            threadId
        );
        object->ReportOrt(L2_SOURCE_FILE, __LINE__);
        ReportCallStack(LOG_ERROR, false);
    }
    Write2(-1, -1, static_cast<char>(-1), sharedIndex, object);
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
bool CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::IsZDivided(int x, int y) {
    return CurrentMap().IsZDivided(x, y);
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
void CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::Read() {
    g_Timer.BeginJob(JOB_SYNC_ALLMAP);
    PipeBase::Read(this);
    g_Timer.EndJob(JOB_SYNC_ALLMAP);
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
TLMIterator CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::GetIterator(int instantMapIndex) {
    return CurrentMap().GetIterator(instantMapIndex);
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
TLMIterator CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::GetIterator(short x, short y, char z) {
    return CurrentMap().GetIterator(x, y, z);
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
void CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::OnRead(const CTL_WorldMapNode2& node) {
    if (node.idx == -1 && node.idy == -1) {
        CurrentMap().Remove(node);
        GuardHelper removeGuard(L"CThreadLocalWorldMap::OnRead(a.idx==1 && a.idy==-1)", CThreadManager::GetThreadID());
        if (node.object == nullptr) {
            GuardHelper crashGuard(L"onread_crash", CThreadManager::GetThreadID());
            ForceCrash(CrashInfo::COBJECT_IS_NULL);
        }
        L2_RELEASE(node.object, ObjectRefType::ThreadLocalMap);
    } else if (node.object->m_Deleted == 0) {
        CurrentMap().Insert(node);
    }
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
typename CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::CTL_Map2& CThreadLocalWorldMap2<
    MaximumObjects,
    PipeRecords,
    ThreadCount>::CurrentMap() {
    return m_Map[static_cast<std::size_t>(CThreadManager::GetThreadID())];
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
const typename CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::CTL_Map2& CThreadLocalWorldMap2<
    MaximumObjects,
    PipeRecords,
    ThreadCount>::CurrentMap() const {
    return m_Map[static_cast<std::size_t>(CThreadManager::GetThreadID())];
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
void CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::BroadcastToNeighbor(
    CObject& sender, int pendingWriteLimit, FVector position, const char* format, ...
) {
    L2_GUARD_FUNCTION();
    va_list arguments;
    va_start(arguments, format);
    DefaultIOBuffer* const buffer = AssembleConstBuffer(format, arguments);
    va_end(arguments);
    if (buffer == nullptr) {
        return;
    }

    CAsyncSendConstBuffer function(static_cast<int>(sender.m_nId), buffer, pendingWriteLimit);
    CTLMFilterRect filter(static_cast<int>(sender.GetInZoneID()), position, 2048.0);
    FOR_ALL_NEIGHBOR_USER_RECT(&filter, &function);
    buffer->Release();
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
void CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::BroadcastToNeighborParty(
    CObject* sender, int pendingWriteLimit, FVector position, int, const char* format, ...
) {
    // Current L2Server 0x005d5a9c. The distance argument is present in
    // the ABI but the current body filters by container.
    L2_GUARD_FUNCTION();

    int senderObjectId = 0;
    int inZoneId = 0;
    if (sender != nullptr) {
        CObject* identity = sender;
        if (sender->IsSummon()) {
            if (User* const master = sender->GetUserOrMaster()) {
                identity = master;
            }
        }
        senderObjectId = static_cast<int>(identity->m_nId);
        inZoneId = static_cast<int>(identity->GetInZoneID());
    }

    va_list arguments;
    va_start(arguments, format);
    DefaultIOBuffer* const buffer = AssembleConstBuffer(format, arguments);
    va_end(arguments);
    if (buffer == nullptr) {
        return;
    }

    CWorldContainer* container = nullptr;
    if (sender == nullptr || sender->IsInRealWorld()) {
        container = CContainerDB::Inst().GetWorldContainer(
            runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
            runtime::Win64CvttDoubleToInt32(position.z)
        );
        if (container == nullptr) {
            buffer->Release();
            return;
        }
    }

    CTLMFilterContainer filter(inZoneId, position, container);
    AsyncSendConstBufferToMyParty function(senderObjectId, buffer, pendingWriteLimit);
    FOR_ALL_NEIGHBOR_CONTAINER(filter, function);
    buffer->Release();
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
void CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::BroadcastToLawfulPVPMember(
    CObject* sender, int pendingWriteLimit, FVector position, int, const char* format, ...
) {
    // Current L2Server 0x005d5d54. As above, current filtering is by
    // the resolved world container, not by the distance argument.
    L2_GUARD_FUNCTION();

    int senderObjectId = 0;
    int inZoneId = 0;
    if (sender != nullptr) {
        CObject* identity = sender;
        if (sender->IsSummon()) {
            if (User* const master = sender->GetUserOrMaster()) {
                identity = master;
            }
        }
        senderObjectId = static_cast<int>(identity->m_nId);
        inZoneId = static_cast<int>(identity->GetInZoneID());
    }

    va_list arguments;
    va_start(arguments, format);
    DefaultIOBuffer* const buffer = AssembleConstBuffer(format, arguments);
    va_end(arguments);
    if (buffer == nullptr) {
        return;
    }

    CWorldContainer* container = nullptr;
    if (sender == nullptr || sender->IsInRealWorld()) {
        container = CContainerDB::Inst().GetWorldContainer(
            runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
            runtime::Win64CvttDoubleToInt32(position.z)
        );
        if (container == nullptr) {
            buffer->Release();
            return;
        }
    }

    CTLMFilterContainer filter(inZoneId, position, container);
    AsyncSendConstBufferToLawfulPVPMember function(senderObjectId, buffer, pendingWriteLimit);
    FOR_ALL_NEIGHBOR_CONTAINER(filter, function);
    buffer->Release();
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
void CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::BroadcastToNeighbor(
    int inZoneId, int senderObjectId, int pendingWriteLimit, FVector position, const char* format, ...
) {
    L2_GUARD_FUNCTION();
    va_list arguments;
    va_start(arguments, format);
    DefaultIOBuffer* const buffer = AssembleConstBuffer(format, arguments);
    va_end(arguments);
    if (buffer == nullptr) {
        return;
    }

    CAsyncSendConstBuffer function(senderObjectId, buffer, pendingWriteLimit);
    CTLMFilterRect filter(inZoneId, position, 2048.0);
    FOR_ALL_NEIGHBOR_USER_RECT(&filter, &function);
    buffer->Release();
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
void CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::BroadcastToNeighborExceptSelf(
    CObject* sender, int pendingWriteLimit, FVector position, const char* format, ...
) {
    L2_GUARD_FUNCTION();
    const int senderObjectId = sender == nullptr ? 0 : static_cast<int>(sender->m_nId);
    const int inZoneId = sender == nullptr ? 0 : static_cast<int>(sender->GetInZoneID());
    L2_DECLARE_AUTO_TIMER(broadcastTimer, JOB_BroadcastToNeighborExceptSelf);

    va_list arguments;
    va_start(arguments, format);
    DefaultIOBuffer* const buffer = AssembleConstBuffer(format, arguments);
    va_end(arguments);
    if (buffer == nullptr) {
        return;
    }

    CWorldContainer* container = nullptr;
    if (sender == nullptr || sender->IsInRealWorld()) {
        container = CContainerDB::Inst().GetWorldContainer(
            runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
            runtime::Win64CvttDoubleToInt32(position.z)
        );
        if (container == nullptr) {
            buffer->Release();
            return;
        }
    }

    {
        L2_DECLARE_AUTO_TIMER(sendTimer, JOB_AsyncSendConstBuffer);
        CTLMFilterContainer filter(inZoneId, position, container);
        CAsyncSendConstBufferExceptSelf function(senderObjectId, buffer, pendingWriteLimit);
        FOR_ALL_NEIGHBOR_CONTAINER(filter, function);
        buffer->Release();
    }
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
int CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::BroadcastToNeighborExceptSelfWithSenderVisibility(
    CObject* sender, int pendingWriteLimit, FVector position, const char* format, ...
) {
    L2_GUARD_FUNCTION();
    if (sender == nullptr) {
        return 0;
    }

    const int senderObjectId = static_cast<int>(sender->m_nId);
    const int inZoneId = static_cast<int>(sender->GetInZoneID());
    L2_DECLARE_AUTO_TIMER(broadcastTimer, JOB_BroadcastToNeighborExceptSelfWithSenderVisibility);
    va_list arguments;
    va_start(arguments, format);
    DefaultIOBuffer* const buffer = AssembleConstBuffer(format, arguments);
    va_end(arguments);
    if (buffer == nullptr) {
        return 0;
    }

    CWorldContainer* container = nullptr;
    if (sender->IsInRealWorld()) {
        container = CContainerDB::Inst().GetWorldContainer(
            runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
            runtime::Win64CvttDoubleToInt32(position.z)
        );
        if (container == nullptr) {
            buffer->Release();
            return 0;
        }
    }

    int count = 0;
    {
        L2_DECLARE_AUTO_TIMER(sendTimer, JOB_AsyncSendConstBuffer);
        CTLMFilterContainerWithSenderVisibility filter(sender, inZoneId, position, container);
        CAsyncSendConstBufferExceptSelf function(senderObjectId, buffer, pendingWriteLimit);
        count = FOR_ALL_NEIGHBOR_CONTAINER(filter, function);
        buffer->Release();
    }
    return count;
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
void CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::BroadcastToNeighborContainer(
    CObject* sender, int pendingWriteLimit, FVector position, const char* format, ...
) {
    L2_GUARD_FUNCTION();
    const int inZoneId = sender == nullptr ? 0 : static_cast<int>(sender->GetInZoneID());
    // Current 0x00605420 shares the same outer job as the
    // ExceptSelf overload instead of using a separate Container job.
    L2_DECLARE_AUTO_TIMER(broadcastTimer, JOB_BroadcastToNeighborExceptSelf);

    va_list arguments;
    va_start(arguments, format);
    DefaultIOBuffer* const buffer = AssembleConstBuffer(format, arguments);
    va_end(arguments);
    if (buffer == nullptr) {
        return;
    }

    CWorldContainer* container = nullptr;
    if (sender == nullptr || sender->IsInRealWorld()) {
        container = CContainerDB::Inst().GetWorldContainer(
            runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
            runtime::Win64CvttDoubleToInt32(position.z)
        );
        if (container == nullptr) {
            buffer->Release();
            return;
        }
    }

    {
        L2_DECLARE_AUTO_TIMER(sendTimer, JOB_AsyncSendConstBuffer);
        CTLMFilterContainer filter(inZoneId, position, container);
        CAsyncSendConstBuffer2 function(buffer, pendingWriteLimit);
        FOR_ALL_NEIGHBOR_CONTAINER(filter, function);
        buffer->Release();
    }
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
void CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::BroadcastToNeighborIndex(
    CObject* sender,
    int pendingWriteLimit,
    FVector position,
    int minXIndex,
    int maxXIndex,
    int minYIndex,
    int maxYIndex,
    const char* format,
    ...
) {
    L2_GUARD_FUNCTION();
    const int senderObjectId = sender == nullptr ? 0 : static_cast<int>(sender->m_nId);
    const int inZoneId = sender == nullptr ? 0 : static_cast<int>(sender->GetInZoneID());

    va_list arguments;
    va_start(arguments, format);
    DefaultIOBuffer* const buffer = AssembleConstBuffer(format, arguments);
    va_end(arguments);
    if (buffer == nullptr) {
        return;
    }

    const std::uint32_t alignedX =
        (static_cast<std::uint32_t>(runtime::Win64CvttDoubleToInt32(position.x)) + UINT32_C(0x50000)) &
        UINT32_C(0xffff8000);
    const std::uint32_t alignedY =
        (static_cast<std::uint32_t>(runtime::Win64CvttDoubleToInt32(position.y)) + UINT32_C(0x40000)) &
        UINT32_C(0xffff8000);
    CTLMFilterRange filter(
        inZoneId, runtime::Int32FromBits(alignedX - ((static_cast<std::uint32_t>(minXIndex) + 10U) << 15U)),
        runtime::Int32FromBits(alignedX + (static_cast<std::uint32_t>(maxXIndex) << 15U) - UINT32_C(0x48001)),
        runtime::Int32FromBits(alignedY - ((static_cast<std::uint32_t>(minYIndex) + 8U) << 15U)),
        runtime::Int32FromBits(alignedY + (static_cast<std::uint32_t>(maxYIndex) << 15U) - UINT32_C(0x38001))
    );
    CAsyncSendConstBuffer function(senderObjectId, buffer, pendingWriteLimit);
    FOR_ALL_NEIGHBOR_USER_RANGE(filter, function);
    buffer->Release();
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
void CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::BroadcastToNeighborDist(
    CObject* sender, int pendingWriteLimit, FVector position, int distance, const char* format, ...
) {
    L2_GUARD_FUNCTION();
    const int senderObjectId = sender == nullptr ? 0 : static_cast<int>(sender->m_nId);
    const int inZoneId = sender == nullptr ? 0 : static_cast<int>(sender->GetInZoneID());

    va_list arguments;
    va_start(arguments, format);
    DefaultIOBuffer* const buffer = AssembleConstBuffer(format, arguments);
    va_end(arguments);
    if (buffer == nullptr) {
        return;
    }

    CAsyncSendConstBuffer function(senderObjectId, buffer, pendingWriteLimit);
    CTLMFilterDist filter(inZoneId, position, static_cast<double>(distance));
    FOR_ALL_NEIGHBOR_USER_DIST(&filter, &function);
    buffer->Release();
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
void CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::BroadcastToNeighborDist(
    CObject* sender, int pendingWriteLimit, FVector position, int distance, DefaultIOBuffer* buffer
) {
    L2_GUARD_FUNCTION();
    if (buffer == nullptr) {
        return;
    }

    const int senderObjectId = sender == nullptr ? 0 : static_cast<int>(sender->m_nId);
    const int inZoneId = sender == nullptr ? 0 : static_cast<int>(sender->GetInZoneID());
    CAsyncSendConstBuffer function(senderObjectId, buffer, pendingWriteLimit);
    CTLMFilterDist filter(inZoneId, position, static_cast<double>(distance));
    FOR_ALL_NEIGHBOR_USER_DIST(&filter, &function);
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
void CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::BroadcastToTargettingNeighbor(
    CObject* sender, int pendingWriteLimit, FVector position, int distance, const char* format, ...
) {
    L2_GUARD_FUNCTION();
    const int senderObjectId = sender == nullptr ? 0 : static_cast<int>(sender->m_nId);
    const int inZoneId = sender == nullptr ? 0 : static_cast<int>(sender->GetInZoneID());

    va_list arguments;
    va_start(arguments, format);
    DefaultIOBuffer* const buffer = AssembleConstBuffer(format, arguments);
    va_end(arguments);
    if (buffer == nullptr) {
        return;
    }

    CAsyncSendConstBuffer function(senderObjectId, buffer, pendingWriteLimit);
    CTLMFilterDist filter(inZoneId, position, static_cast<double>(distance));
    FOR_ALL_NEIGHBOR_USER_DIST(&filter, &function);
    buffer->Release();
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
void CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::BroadcastToNeighborDistBetweenZ(
    CObject& sender, int pendingWriteLimit, FVector position, int distance, int minZ, int maxZ, const char* format, ...
) {
    L2_GUARD_FUNCTION();
    const int senderObjectId = static_cast<int>(sender.m_nId);
    const int inZoneId = static_cast<int>(sender.GetInZoneID());
    L2_DECLARE_AUTO_TIMER(broadcastTimer, JOB_BroadcastToNeighborDistBetweenZ);
    va_list arguments;
    va_start(arguments, format);
    DefaultIOBuffer* const buffer = AssembleConstBuffer(format, arguments);
    va_end(arguments);
    if (buffer == nullptr) {
        return;
    }

    {
        L2_DECLARE_AUTO_TIMER(sendTimer, JOB_AsyncSendConstBuffer);
        CAsyncSendConstBuffer function(senderObjectId, buffer, pendingWriteLimit);
        CTLMFilterDistBetweenZ filter(inZoneId, position, static_cast<double>(distance), minZ, maxZ);
        FOR_ALL_NEIGHBOR_USER_DIST(&filter, &function);
        buffer->Release();
    }
}

template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
void CThreadLocalWorldMap2<MaximumObjects, PipeRecords, ThreadCount>::BroadcastToNeighborRect(
    CObject* sender, int pendingWriteLimit, FVector position, int radius, const char* format, ...
) {
    L2_GUARD_FUNCTION();
    const int senderObjectId = sender == nullptr ? 0 : static_cast<int>(sender->m_nId);
    const int inZoneId = sender == nullptr ? 0 : static_cast<int>(sender->GetInZoneID());

    va_list arguments;
    va_start(arguments, format);
    DefaultIOBuffer* const buffer = AssembleConstBuffer(format, arguments);
    va_end(arguments);
    if (buffer == nullptr) {
        return;
    }

    CAsyncSendConstBuffer function(senderObjectId, buffer, pendingWriteLimit);
    CTLMFilterRect filter(inZoneId, position, static_cast<double>(radius));
    FOR_ALL_NEIGHBOR_USER_RECT(&filter, &function);
    buffer->Release();
}

template class CThreadLocalWorldMap2<120000, 50000, 16>;

template CThreadLocalWorldMap2<120000, 600000, 16>::CThreadLocalWorldMap2();
template void CThreadLocalWorldMap2<120000, 600000, 16>::Initialize(const wchar_t*);
template bool CThreadLocalWorldMap2<120000, 600000, 16>::CTL_Map2::IsZDivided(int, int) const;

template CThreadLocalWorldMap2<900000, 112500, 16>::CThreadLocalWorldMap2();
template void CThreadLocalWorldMap2<900000, 112500, 16>::Initialize(const wchar_t*);

template CThreadLocalWorldMap2<1000, 3000, 16>::CThreadLocalWorldMap2();
template void CThreadLocalWorldMap2<1000, 3000, 16>::Initialize(const wchar_t*);

template CThreadLocalWorldMap2<10001, 20002, 16>::CThreadLocalWorldMap2();
template void CThreadLocalWorldMap2<10001, 20002, 16>::Initialize(const wchar_t*);

template void CThreadLocalWorldMap2<120000, 600000, 16>::InitInstantMap(int);
template void CThreadLocalWorldMap2<900000, 112500, 16>::InitInstantMap(int);
template void CThreadLocalWorldMap2<1000, 3000, 16>::InitInstantMap(int);
template void CThreadLocalWorldMap2<10001, 20002, 16>::InitInstantMap(int);

template void CThreadLocalWorldMap2<10001, 20002, 16>::Push(int, const FVector&, CObject*);

template void CThreadLocalWorldMap2<1000, 3000, 16>::Push(int, const FVector&, CObject*);

template void CThreadLocalWorldMap2<120000, 600000, 16>::Remove(int, CObject*);
template void CThreadLocalWorldMap2<900000, 112500, 16>::Remove(int, CObject*);
template void CThreadLocalWorldMap2<1000, 3000, 16>::Remove(int, CObject*);
template void CThreadLocalWorldMap2<10001, 20002, 16>::Remove(int, CObject*);
