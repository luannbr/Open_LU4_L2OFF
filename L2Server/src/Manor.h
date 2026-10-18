#pragma once

#include "Shared/src/BaseStructure.h"
#include "Shared/src/IOC.h"
#include "Shared/src/ManorData.h"
#include "Shared/src/ScopedLockGuards.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

class CCreature;
class CManorFuncHandler;
class CManorPacketHandler;
class CParserForManor;
class CPledge;
class CSocket;
class User;
enum ObjectFieldType : std::int32_t;

class CManorMenuParser {
public:
    static void Parse(User* user, const char16_t* command);
};

struct sDefaultCropInfo {
    std::int32_t nCropId; // +0x00
    std::int32_t nLevel; // +0x04
    std::vector<int> vType[2]; // +0x08, +0x28
    std::int32_t nSeedPrice; // +0x48
    std::int32_t nCropPrice; // +0x4c
};


// Current manordata parser products. The executable fixes the two pointer
// carriers at 0x10 bytes each and ProcureTempData's target std::map at +0x18.
struct ProcureStructEach {
    std::int32_t nTypeId; // +0x00
    std::byte m_alignment04To08[4];
    std::vector<int>* pProcureItemList; // +0x08
};

struct ProcureStruct {
    std::int32_t nCropId; // +0x00
    std::byte m_alignment04To08[4];
    std::vector<ProcureStructEach*>* m_pProcureList; // +0x08
};

struct ProcureTempData {
    ProcureTempData() : nCropId(0), nAmount(0), nTypeId(0), nPriceRate(0) {}

    std::int32_t nCropId; // +0x00
    std::byte m_alignment04To08[4];
    std::int64_t nAmount; // +0x08
    std::int32_t nTypeId; // +0x10
    std::int32_t nPriceRate; // +0x14
    std::map<int, std::int64_t> mapOut; // +0x18
};

struct CropDataStruct {
    std::int32_t nSeedId; // +0x00
    std::int32_t nCropId; // +0x04
    std::int32_t nSeedLev; // +0x08
    std::int32_t nSeedMaxPerDay; // +0x0c
    std::int32_t nCropMaxPerDay; // +0x10
};


// Current Manor.cpp concrete owner. The current vtable differs from the
// CIOObject base only at Release and the deleting-destructor slot. Keep the
// class allocator separate from AddRef so the inherited AddRef slot remains
// unchanged.
class CManor : public CIOObject {
public:
    CManor();
    ~CManor() override;

    L2_DECLARE_MEMORY_ALLOCATION(CManor)

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    void Set(ObjectFieldType field, int value);
    void Set(ObjectFieldType field, const char16_t* value);
    void Set(ObjectFieldType field, ProcureStruct* value);
    void AddCrop(int cropId, int seedId, int itemId, int seedLevel, int seedMaxPerDay, int cropMaxPerDay);
    void SetPledge(CPledge* pledge);
    void CalcNextManor();
    void TransferManorData();
    bool SellSeed(User* user, int itemCount, const unsigned char* packet);
    bool CheckBuySeed(int seedId, std::int64_t count, User* user);
    std::int64_t GetSeedPrice(int seedId);
    bool ManorCheckTime();
    void WriteLock(const wchar_t* file, int line);
    void WriteUnlock(const wchar_t* file, int line);
    void SetSeedSold(int seedId, std::int64_t count);
    void SetCropProcured(int cropId, std::int64_t count);
    void SetInfo(
        int residenceId,
        std::int64_t seedSellBank,
        std::int64_t procureBuyRemainBank,
        std::uint8_t changeState,
        const SYSTEMTIME* lastChanged
    );
    void SetSeedData(const std::int64_t currentData[40][10], const std::int64_t nextData[40][7]);
    void ChcekDayChanged();

private:
    friend class CManorFuncHandler;
    friend class CManorPacketHandler;
    friend class CManorDB;
    friend class CResidence;
    friend class User;
    template <typename T>
    friend auto scoped_lock_detail::ReadLock(T& lock, const wchar_t* file, int line, int)
        -> decltype(lock.ReadLock(file, line), void());
    template <typename T>
    friend auto scoped_lock_detail::ReadUnlock(T& lock, const wchar_t*, int, int) -> decltype(lock.ReadUnlock(), void());

    void ReadLock(const wchar_t* file, int line);
    void ReadUnlock();
    int GetManorSeedIndex(int seedId);
    int GetManorCropIndex(int cropId);
    int GetMaxCropCountById(int cropId);
    int GetMaxCropCount(int index);
    int GetMaxSeedCount(int index);
    void SetMaxCropCount(int index, int count);
    void SetMaxSeedCount(int index, int count);
    bool SetNextSeedInfo(int seedId, std::int64_t price, std::int64_t count);
    bool SetNextProcureInfo(int cropId, std::int64_t price, std::int64_t count, int type);
    bool CheckProcureCrop(int cropId, std::int64_t count);
    std::int64_t GetCommissionForProcureCrop(int cropId, std::int64_t count);
    void RollProcureDice(ProcureTempData* data, User* user);
    bool ProcureCrop(
        User* user, int itemServerId, int cropId, std::int64_t amount, bool sameResidence, std::int64_t& commission
    );
    void SayNeedSeedMoney(User* user);
    void SayNeedCropMoney(User* user);
    std::int64_t CalcRemainSeedToAdena();
    std::int64_t CalcRemainCropToAdena();
    std::int64_t CalcCropToAdena();
    std::int64_t GetAdenaForNextSeed();
    std::int64_t GetAdenaForNextCrop();
    void ClearSeedData();
    void ClearCropData();
    void ClearNextSeedData();
    void ClearNextCropData();
    void SaveNowManorInfo();
    void SaveNowSeedInfo(int index);
    void SaveNextSeedInfo(int index);
    void TransferSeedData();
    void TransferCropData();
    CPledge* GetManorOwnerPledge();
    void LogManorData();
    void LogManorInfo();
    void LogNextManorInfo(std::int64_t nextSeedAdena, std::int64_t nextCropAdena, std::int64_t oldIncome);

    std::int32_t m_nManorOwnerPledgeServerId; // +0x18
    SYSTEMTIME m_SystemTime; // +0x1c
    bool m_bManorSettingTime; // +0x2c
    std::byte m_alignment2dTo30[3];
    double m_dMaxChangeRate; // +0x30
    std::map<int, ProcureStruct*> m_ProcureMap; // +0x38
    std::vector<CropDataStruct*> m_CropList; // +0x58

public:
    CManorData* m_data; // +0x78
};


class CManorDB {
public:
    CManorDB();

    CManorData* RequireManorData(int manorId);
    void Add(CManor* manor);
    CManor* GetManor(std::int32_t manorId);
    void CalcNextAllManor();
    void TransferAllManorData();
    void Load();
    void LoadFromDB();

private:
    friend class CManorFuncHandler;
    friend class CManorPacketHandler;

    void MakeDefaultCropInfo();

    std::map<int, CManor*> m_ManorIDMap; // +0x00
    CYieldLock m_Sect; // +0x20
    std::map<int, sDefaultCropInfo> m_mapCrop2DefaultCropInfo; // +0x30
    std::map<int, sDefaultCropInfo*> m_mapSeed2DefaultCropInfo; // +0x50
};

class CManorPacketHandler {
public:
    static bool RequestManorList(CSocket* socket, const std::uint8_t* packet);
    static bool ProcureCropList(CSocket* socket, const std::uint8_t* packet);
    static bool SetSeed(CSocket* socket, const std::uint8_t* packet);
    static bool SetCrop(CSocket* socket, const std::uint8_t* packet);
};


// Current L2Server Manor.obj owns this non-polymorphic command carrier.
// NpcSocket.obj emits the inline destructor when dispatching NC_PROTOCOL
// 0x74. The two CSPointer members therefore release NPC first, then user.
class CManorFuncHandler {
public:
    CManorFuncHandler(std::uint8_t function, int npcId, int userId, int manorId, int time, int cropId);

    bool PreCheck();
    void Execute();

private:
    bool IsChamberlain();
    int MakeProcureTypeBuffer(char* buffer, int bufferSize, const sDefaultCropInfo& info);
    bool InitManor(CManor*& manor, CManorData*& data, const wchar_t* file, int line);
    void ShowManorDefaultInfo();
    void ShowSeedInfo();
    void ShowCropInfo();
    void ShowSeedSetting();
    void ShowCropSetting();
    void ShowProcureCropList();
    void ShowProcureCropDetail();

    std::uint8_t m_byFunc; // +0x00
    [[maybe_unused]] std::byte m_alignment01To04[3]; // +0x01..+0x03
    std::int32_t m_nNpcId; // +0x04
    std::int32_t m_nUserId; // +0x08
    std::int32_t m_nManorId; // +0x0c
    std::uint32_t m_nTime; // +0x10
    std::int32_t m_nCropId; // +0x14
    CSPointer<CCreature> m_pUser; // +0x18
    CSPointer<CCreature> m_pNpc; // +0x28
};


extern CManorDB g_ManorDB;
extern volatile std::int32_t g_nManorLock;
