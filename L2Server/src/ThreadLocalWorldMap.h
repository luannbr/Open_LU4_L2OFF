#pragma once

#include "CWorldObject.h"

#include "Shared/src/Socket.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CContainer;
class FBox3;
class CTerritory;

class ITL_Map2 {
public:
    virtual int Next(int index) = 0;
    virtual CObject* GetObject(int index) = 0;
    virtual ~ITL_Map2() = default;
};

class TLMIterator {
public:
    TLMIterator();
    TLMIterator(int index, ITL_Map2* map);

    CObject* operator*();
    void operator++();

    int Index;
    ITL_Map2* p;
};


class IThreadLocalWorldMap {
public:
    virtual bool IsZDivided(int x, int y) = 0;
    virtual void Read() = 0;
    virtual TLMIterator GetIterator(int instantMapIndex) = 0;
    virtual TLMIterator GetIterator(short x, short y, char z) = 0;
};


class CTLMFunc {
public:
    virtual ~CTLMFunc();
    virtual bool CanDo(CObject* object, double distance) = 0;
    virtual void Finish();
};


class CTLMFilter {
public:
    CTLMFilter(int inZoneId, double distance = 0.0);
    virtual ~CTLMFilter();
    virtual bool CanDo(CObject* object) = 0;

    int GetInstantMapIndex();
    double GetDistance() const;

    std::int32_t __nInZoneID;
    double __distance;
};


class CTLMFilterWithPos : public CTLMFilter {
public:
    CTLMFilterWithPos(int inZoneId, const FVector& position);

    const FVector& GetPosition() const;

    FVector __vPos;
};


class CTLMFilterDist : public CTLMFilterWithPos {
public:
    CTLMFilterDist(int inZoneId, const FVector& position, double distance);
    CTLMFilterDist(CWorldObject* object, double distance);

    bool CanDo(CObject* object) override;
    double GetRadius() const;

private:
    double __dist;
    double __distCube;
};


class CTLMFilterDistBetweenZ : public CTLMFilterDist {
public:
    CTLMFilterDistBetweenZ(int inZoneId, const FVector& position, double distance, int minZ, int maxZ);
    CTLMFilterDistBetweenZ(CWorldObject* object, double distance, int minZ, int maxZ);

    bool CanDo(CObject* object) override;

private:
    std::int32_t minZ;
    std::int32_t maxZ;
};


class CTLMFilterTerritory : public CTLMFilterDist {
public:
    CTLMFilterTerritory(int inZoneId, CTerritory* territory);

    bool CanDo(CObject* object) override;

    CTerritory* __pTerritory;
};


class CTLMFuncInTerritory : public CTLMFilterDist {
public:
    CTLMFuncInTerritory(int inZoneId, CTerritory* territory);

    bool CanDo(CObject* object) override;

    CTerritory* __vTerritory;
};


class CTLMFilterRect : public CTLMFilterWithPos {
public:
    CTLMFilterRect(int inZoneId, const FVector& position, double radius);
    CTLMFilterRect(CWorldObject* object, double radius);

    bool CanDo(CObject* object) override;
    double GetRadius() const;

private:
    double __radius;
};


class CTLMFilterContainer : public CTLMFilterWithPos {
public:
    CTLMFilterContainer(int inZoneId, const FVector& position, CContainer* container);

    bool CanDo(CObject* object) override;

    std::int32_t __containerX;
    std::int32_t __containerY;
};


class CTLMFilterContainerWithSenderVisibility : public CTLMFilterContainer {
public:
    CTLMFilterContainerWithSenderVisibility(CObject* sender, int inZoneId, const FVector& position, CContainer* container);

    bool CanDo(CObject* object) override;

    CObject* __pSender;
};


class CTLMFilterRange : public CTLMFilter {
public:
    CTLMFilterRange(int inZoneId, int minX, int maxX, int minY, int maxY);

    bool CanDo(CObject* object) override;

    std::int32_t __minx;
    std::int32_t __maxx;
    std::int32_t __miny;
    std::int32_t __maxy;
};


class CAsyncSendConstBuffer : public CTLMFunc {
public:
    CAsyncSendConstBuffer(int senderObjectId, DefaultIOBuffer* buffer, int pendingWriteLimit);

    bool CanDo(CObject* object, double distance) override;

    std::int32_t m_nSenderID;
    DefaultIOBuffer* m_pBuffer;
    std::int32_t m_nAcceptablePendingWrite;
};


class CAsyncSendConstBufferExceptSelf : public CAsyncSendConstBuffer {
public:
    CAsyncSendConstBufferExceptSelf(int senderObjectId, DefaultIOBuffer* buffer, int pendingWriteLimit);

    bool CanDo(CObject* object, double distance) override;
};


class AsyncSendConstBufferToMyParty : public CAsyncSendConstBuffer {
public:
    AsyncSendConstBufferToMyParty(int senderObjectId, DefaultIOBuffer* buffer, int pendingWriteLimit);

    bool CanDo(CObject* object, double distance) override;
};


class AsyncSendConstBufferToLawfulPVPMember : public CAsyncSendConstBuffer {
public:
    AsyncSendConstBufferToLawfulPVPMember(int senderObjectId, DefaultIOBuffer* buffer, int pendingWriteLimit);

    bool CanDo(CObject* object, double distance) override;
};


class CAsyncSendConstBuffer2 : public CTLMFunc {
public:
    CAsyncSendConstBuffer2(DefaultIOBuffer* buffer, int pendingWriteLimit);

    bool CanDo(CObject* object, double distance) override;

    DefaultIOBuffer* m_pBuffer;
    std::int32_t m_nAcceptablePendingWrite;
};


struct CTL_WorldMapNode2 {
    CObject* object;
    std::int32_t index;
    std::int16_t idx;
    std::int16_t idy;
    std::int8_t idz;
    std::array<std::byte, 3> padding_11;
    std::int32_t instantMapIndex;
};


template <std::size_t MaximumObjects, std::size_t PipeRecords, std::size_t ThreadCount>
class CThreadLocalWorldMap2 : public CTLPipeBase<CTL_WorldMapNode2, PipeRecords, ThreadCount>::CReadCallback,
                              public CIOObject,
                              public IThreadLocalWorldMap,
                              public CTLPipeBase<CTL_WorldMapNode2, PipeRecords, ThreadCount> {
    using PipeBase = CTLPipeBase<CTL_WorldMapNode2, PipeRecords, ThreadCount>;
    using ReadCallback = typename PipeBase::CReadCallback;

public:
    class CTL_Map2 : public ITL_Map2 {
    public:
        CTL_Map2();
        ~CTL_Map2() override;

        int Next(int index) override;
        CObject* GetObject(int index) override;

        void Insert(const CTL_WorldMapNode2& node);
        void Remove(const CTL_WorldMapNode2& node);
        void InitInstantMap(int instantMapCount);
        bool IsZDivided(int x, int y) const;
        TLMIterator GetIterator(int instantMapIndex);
        TLMIterator GetIterator(short x, short y, char z);

    private:
        using MapNode = struct MapNode {
            CObject* object;
            std::int32_t previous;
            std::int32_t next;
            std::int16_t idx;
            std::int16_t idy;
            std::int8_t idz;
            std::array<std::byte, 3> padding_15;
            std::int32_t instantMapIndex;
            std::array<std::byte, 4> padding_1c;
        };


        std::int32_t& Head(std::int16_t x, std::int16_t y, std::int8_t z);
        const std::int32_t& Head(std::int16_t x, std::int16_t y, std::int8_t z) const;

        std::array<MapNode, MaximumObjects> m_Nodes;
        std::array<std::int32_t, 0x55000> m_Head;
        std::array<std::int32_t, 0x88e0> m_ZHead;
        std::int32_t* m_pInstantHead;
        std::int32_t m_nInstantMapCount;
    };

    CThreadLocalWorldMap2();
    ~CThreadLocalWorldMap2() override = default;

    void Initialize(const wchar_t* name);
    void InitInstantMap(int instantMapCount);
    void Push(int sharedIndex, const FVector& position, CObject* object);
    void SetTlmBox3(FBox3& box, int x, int y, int z);
    void Remove(int sharedIndex, CObject* object);
    void Write2(short x, short y, char z, int sharedIndex, CObject* object);

    void BroadcastToNeighbor(CObject& sender, int pendingWriteLimit, FVector position, const char* format, ...);
    void BroadcastToNeighbor(
        int inZoneId, int senderObjectId, int pendingWriteLimit, FVector position, const char* format, ...
    );
    void BroadcastToNeighborExceptSelf(CObject* sender, int pendingWriteLimit, FVector position, const char* format, ...);
    int BroadcastToNeighborExceptSelfWithSenderVisibility(
        CObject* sender, int pendingWriteLimit, FVector position, const char* format, ...
    );
    void BroadcastToNeighborContainer(CObject* sender, int pendingWriteLimit, FVector position, const char* format, ...);
    void BroadcastToNeighborIndex(
        CObject* sender,
        int pendingWriteLimit,
        FVector position,
        int minXIndex,
        int maxXIndex,
        int minYIndex,
        int maxYIndex,
        const char* format,
        ...
    );
    void BroadcastToNeighborDist(
        CObject* sender, int pendingWriteLimit, FVector position, int distance, const char* format, ...
    );
    void BroadcastToNeighborDist(
        CObject* sender, int pendingWriteLimit, FVector position, int distance, DefaultIOBuffer* buffer
    );
    void BroadcastToNeighborParty(
        CObject* sender, int pendingWriteLimit, FVector position, int distance, const char* format, ...
    );
    void BroadcastToLawfulPVPMember(
        CObject* sender, int pendingWriteLimit, FVector position, int distance, const char* format, ...
    );
    void BroadcastToTargettingNeighbor(
        CObject* sender, int pendingWriteLimit, FVector position, int distance, const char* format, ...
    );
    void BroadcastToNeighborDistBetweenZ(
        CObject& sender, int pendingWriteLimit, FVector position, int distance, int minZ, int maxZ, const char* format, ...
    );
    void BroadcastToNeighborRect(
        CObject* sender, int pendingWriteLimit, FVector position, int radius, const char* format, ...
    );

    bool IsZDivided(int x, int y) override;
    void Read() override;
    TLMIterator GetIterator(int instantMapIndex) override;
    TLMIterator GetIterator(short x, short y, char z) override;

private:
    void OnRead(const CTL_WorldMapNode2& node) override;
    CTL_Map2& CurrentMap();
    const CTL_Map2& CurrentMap() const;

    std::array<CTL_Map2, ThreadCount> m_Map;
};

using CUserThreadLocalWorldMap = CThreadLocalWorldMap2<120000, 50000, 16>;


extern CUserThreadLocalWorldMap* g_ptlUserMap;
extern CThreadLocalWorldMap2<120000, 600000, 16>* g_ptlCreatureMap;

// Defined in CreatureCommon.cpp. The declaration must be visible in every
// translation unit, otherwise MSVC emits a second generic instance of this
// member and the link fails with LNK2005.
template <>
void CThreadLocalWorldMap2<120000, 600000, 16>::Write2(short x, short y, char z, int sharedIndex, CObject* object);
extern CThreadLocalWorldMap2<900000, 112500, 16>* g_ptlItemMap;
extern CThreadLocalWorldMap2<1000, 3000, 16>* g_ptlAirShipMap;
extern CThreadLocalWorldMap2<10001, 20002, 16>* g_ptlStaticObjectMap;

CAirShip* __SafeCastAirShip(CObject* object);
CNPC* __SafeCastNpc(CObject* object);

void BROADCAST_SET_SIZE(
    const FVector& position, int distance, int& minX, int& maxX, int& minY, int& maxY, int& minZ, int& maxZ
);
void BROADCAST_SET_SIZE_FAST(
    const FVector& position, int distance, int& minX, int& maxX, int& minY, int& maxY, int& minZ, int& maxZ
);
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
);
void FOR_ALL_DIST(IThreadLocalWorldMap* map, CTLMFilterDist* filter, CTLMFunc* function);
void FOR_ALL_RECT(IThreadLocalWorldMap* map, CTLMFilterRect* filter, CTLMFunc* function);
void FOR_ALL_NEIGHBOR_USER_DIST(CTLMFilterDist* filter, CTLMFunc* function);
void FOR_ALL_NEIGHBOR_CREATURE_DIST(CTLMFilterDist* filter, CTLMFunc* function);
void FOR_ALL_NEIGHBOR_CREATURE_RANGE(CTLMFilterRange* filter, CTLMFunc* function);
void FOR_ALL_NEIGHBOR_CREATURE_RECT(CTLMFilterRect* filter, CTLMFunc* function);
void FOR_ALL_NEIGHBOR_USER_RECT(CTLMFilterRect* filter, CTLMFunc* function);
void FOR_ALL_NEIGHBOR_USER_IN_TERRITORY(CTLMFilterTerritory* filter, CTLMFunc* function);
void FOR_ALL_NEIGHBOR_USER_RANGE(CTLMFilterRange& filter, CTLMFunc& function);
void FOR_ALL_NEIGHBOR_ITEM_RECT(CTLMFilterRect* filter, CTLMFunc* function);
void FOR_ALL_NEIGHBOR_STATIC_OBJECT_DIST(CTLMFilterDist* filter, CTLMFunc* function);
int FOR_ALL_NEIGHBOR_CONTAINER(CTLMFilterContainer& filter, CTLMFunc& function);
