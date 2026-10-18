#pragma once

#include "L2Core/src/inc/XStd.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/Obj.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CUser;
class CAccount;
template <std::size_t Capacity>
class CPacketHelperEx;

using CAccountSP = CSPointer<CAccount>;
using CAccountKey = std::CDualKey<int, int>;
using CAccountMap = CObjectMap<CAccount, 128, 5, RWLock, CAccountKey>;

// Shipping owner: CacheD/Account.{h,cpp}. The portable object uses native
// containers, but keeps the target's MemoryObject lifetime, account map,
// key order and database-visible state.
class CAccount : public MemoryObject {
public:
    explicit CAccount(int accountId);
    ~CAccount() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    static void Init();
    static void ClearUpFromAddedService3ChangeName();
    static CAccountSP Find(CAccountKey accountKey);
    static CAccountSP Load(unsigned int accountId);
    static CAccountSP Reload(unsigned int accountId);

    int GetId() const noexcept;
    const char16_t* GetName() const noexcept;
    CObjectMap<CUser, 1, 5, RWLock, int>& GetChars() noexcept;
    int GetCharCount();
    int GetMaxLevel();
    bool IsNewbieAccount(int levelLimit);
    CSPointer<CUser> GetNewbieUser(int levelLimit);
    void SetCharacter(CUser* user);
    int DeleteCharacter(unsigned int characterId);

    int GetWeekPlayTime();
    void SetWeekPlayTime(int weekPlayTime) noexcept { m_weekPlayTime = weekPlayTime; }
    SYSTEMTIME GetWeekPlayTimeLastChanged();
    bool SaveWeekPlayTime(int weekPlayTime, SYSTEMTIME* lastSaveTime);
    bool SetLastSaveTIme(const SYSTEMTIME* lastSaveTime);
    void SetLastLogoutTime(const SYSTEMTIME& lastLogoutTime);

    int GetEventTime(int eventType, int eventVariable);
    void SetEventTime(int eventTime, int eventType, int eventVariable, bool saveToDatabase);
    void ClearEventTime();
    bool LoadUserStamp(int type, std::map<int, int>& stamps);
    bool LoadUserStamp(int type, int record, std::map<int, int>& stamps);
    bool SaveUserStamp(int type, int record, int stamp, bool enabled = true);
    bool MakeUserStampLogPacket(int type, CPacketHelperEx<20480>& packet);

    void ReadLock(const wchar_t* callerFile, int callerLine);
    void ReadUnlock();
    void WriteLock(const wchar_t* callerFile, int callerLine);
    void WriteUnlock();

private:
    friend class CUser;

    static void DeleteChars(int requestDate, int deletionDate, int deletionId, int accountId, int sequence);
    int GetWeekPlayTimeFromDB();
    void LoadEventTime();

    L2_DECLARE_OBJECT_REGISTRATION(CAccount)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CAccount)
    inline static RWLock s_rwLock;

    // Current target +0x18: one-bucket character map kept as ORT 5.
    CObjectMap<CUser, 1, 5, RWLock, int> m_chars;
    int m_id = 0;
    std::array<char16_t, 32> m_name{};
    int m_weekPlayTime = -1;
    SYSTEMTIME m_lastSaveTime{};
    int m_lastLogoutTime = 0;

    // Current target +0xd8. The key packs the two int arguments of
    // GetEventTime/SetEventTime into their observed 16-bit halves.
    std::map<int, int> m_eventTimes;

    // Current target +0xf8. Constructor 0x0040c07c and destructor
    // 0x0040c30c prove a second std::xmap<int, int> immediately after the
    // event-time map. No current Cached.exe function reads or writes it, and
    // the available PDB does not keep its private source spelling, so do
    // not attach invented behavior to this kept legacy state.
    std::map<int, int> m_unusedLegacyIntMap;
};

extern CAccountMap g_accounts;
