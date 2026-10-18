#pragma once

#include "WareHouse.h"

#include "Shared/src/BaseStructure.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/Obj.h"
#include "Shared/src/RWLock.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CSocket;

// CacheD keeps each current manor row contiguously. The field meanings and
// order are fixed by current Cached.exe, its SQL bindings and
// Shared/CManorData in the paired current L2NPC image.
struct CManorSeedData {
    std::int64_t m_nSeedList = 0;
    std::int64_t m_nSeedPrice = 0;
    std::int64_t m_nSeedSetCount = 0;
    std::int64_t m_nSeedRemainCount = 0;
    std::int64_t m_nProcuringCropList = 0;
    std::int64_t m_nProcuringCropSetCount = 0;
    std::int64_t m_nProcuringCropCount = 0;
    std::int64_t m_nProcuringCropPrice = 0;
    std::int64_t m_nProcuringCropType = 0;
    std::int64_t m_nProcuredCropList = 0;
};

struct CManorSeedDataN {
    std::int64_t m_nSeedList_n = 0;
    std::int64_t m_nSeedPrice_n = 0;
    std::int64_t m_nSeedSetCount_n = 0;
    std::int64_t m_nProcuringCropList_n = 0;
    std::int64_t m_nProcuringCropCount_n = 0;
    std::int64_t m_nProcuringCropPrice_n = 0;
    std::int64_t m_nProcuringCropType_n = 0;
};


class CManor;
using CManorSP = CSPointer<CManor>;


// Shipping owner: CacheD/ManorDb.{h,cpp}. Current Cached.exe fixes the
// complete 0x1850-byte layout, including both 40-row SQL/wire blocks.
class CManor : public MemoryObject {
public:
    explicit CManor(int manorId);
    ~CManor() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    bool SetAdena(int seedSellBank, int procureBuyRemainBank);
    void SendManorInfoAndData(CSocket* socket);
    bool SetManorSeedToChangeData(int itemType, std::int64_t amount);
    bool SetManorCropToChangeData(int itemType, std::int64_t amount);

    // The current image contains two void reset bodies and two bool apply
    // bodies whose guard strings collide on ResetManorSeedToChangeData.
    // Distinct semantic names keep all four machine-confirmed operations.
    void ResetManorSeedToChangeData();
    void ResetManorCropToChangeData();
    bool ApplyManorSeedToChangeData();
    bool ApplyManorCropToChangeData();

    CWareHouseSP GetWareHouse();
    bool LoadData();
    bool LoadInfo();
    bool SaveInfo();
    void SetNowManorInfo(CSocket* socket, const std::uint8_t* packet);
    void SetNowManorSeed(CSocket* socket, const std::uint8_t* packet);
    void SetNextManorSeed(CSocket* socket, const std::uint8_t* packet);

    int GetId() const noexcept;

private:

    CManorSeedDataN ReadNextDataTargetIndex(int index) const;
    void WriteNextDataTargetIndex(int index, const CManorSeedDataN& data);

    L2_DECLARE_OBJECT_REGISTRATION(CManor)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CManor)

    CObjectStorage<CWareHouse> m_warehouse; // +0x0018
    RWLock m_lock; // +0x0020
    int m_nManorId; // +0x0058
    [[maybe_unused]] std::uint32_t m_targetPadding005c; // +0x005c
    std::array<CManorSeedData, 40> m_manorSeedData; // +0x0060
    std::array<CManorSeedDataN, 40> m_manorSeedDataN; // +0x0ce0
    std::array<std::int64_t, 40> m_seedToChangeData; // +0x15a0
    std::array<std::int64_t, 40> m_cropToChangeData; // +0x16e0
    int m_nResidenceID; // +0x1820
    [[maybe_unused]] std::uint32_t m_targetPadding1824; // +0x1824
    std::int64_t m_nSeedSellBank; // +0x1828
    std::int64_t m_nProcureBuyRemainBank; // +0x1830
    SYSTEMTIME m_lastChanged; // +0x1838
    std::uint8_t m_byChangeState; // +0x1848
    [[maybe_unused]] std::array<std::uint8_t, 7> m_targetPadding1849; // +0x1849
};



using CManorMap = CObjectMap<CManor, 1, 5, RWLock, int>;


class CManorDb {
public:
    CManorDb();
    ~CManorDb();

    CManorSP GetManor(int manorId);

private:

    void Add(CManor* manor);
    CManorSP LoadFromDb(int manorId);

    RWLock m_lock; // +0x00
    CManorMap m_manors; // +0x38
};



extern CManorDb g_manorDb;
