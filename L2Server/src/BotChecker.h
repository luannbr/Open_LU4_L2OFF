#pragma once

#include "Shared/src/IOC.h"
#include "Shared/src/IOCs.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadSafeObjectMap.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

class User;

struct BotBlockData : public MemoryObject {
    BotBlockData() = default;
    BotBlockData(std::uint32_t userDatabaseId, std::uint32_t bannedTick);
    ~BotBlockData() override = default;

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    L2_DECLARE_OBJECT_REGISTRATION(BotBlockData)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(BotBlockData)

    std::uint32_t m_UserDBID; // +0x18
    std::uint32_t m_BannedTick; // +0x1c
};


class CBotBlockManager : public CIOObject, public Singleton<CBotBlockManager> {
public:
    CBotBlockManager();
    ~CBotBlockManager() override;

    int Clear();
    bool IsBotUser(std::uint32_t userDatabaseId);
    bool RemoveBotUser(std::uint32_t userDatabaseId);
    bool AddAsBotUser(std::uint32_t userDatabaseId);

    std::uint32_t m_BanPeriodMSec; // +0x20

private:
    CObjectMap<BotBlockData, 16, 5, CIOCriticalSection,
        int> m_BlockList; // +0x28
};


class CBotCheckInfo : public MemoryObject {
public:
    CBotCheckInfo();
    CBotCheckInfo(const CBotCheckInfo& other);
    CBotCheckInfo(CBotCheckInfo&& other);
    ~CBotCheckInfo() override = default;

    CBotCheckInfo& operator=(const CBotCheckInfo& other) noexcept;
    CBotCheckInfo& operator=(CBotCheckInfo&& other) noexcept;

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    void Init();
    void SetData(int tickCount, int botUserId);
    int GetTickCount();
    void DecTickCount();
    int GetBotUserID();

private:
    L2_DECLARE_OBJECT_REGISTRATION(CBotCheckInfo)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CBotCheckInfo)

    int m_nTickCount; // +0x18
    int m_nBotUserID; // +0x1c
};


class CBotChecker : public MemoryObject {
public:
    CBotChecker();
    CBotChecker(const CBotChecker&) = delete;
    CBotChecker(CBotChecker&&) = delete;
    ~CBotChecker() override = default;

    CBotChecker& operator=(const CBotChecker&) = delete;
    CBotChecker& operator=(CBotChecker&&) = delete;

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    void Init(User* owner);
    void ReleaseOwner(User* owner);
    bool BeginCheck(int checkType, User* user, int tickCount);
    void OnTimerExpired();

    ::CYieldLock m_lock; // +0x18

private:
    bool IsCanCheck();
    void SetData(int botUserId, int tickCount);
    void AddBotUser(const char16_t* name);
    void DeleteBotUserList();
    void ShowBotUserList();
    void ShowBotUserCheckList();
    bool CheckImpl_1(User* user);
    bool CheckImpl_2(User* user);
    bool CheckImpl_3(User* user);

    L2_DECLARE_OBJECT_REGISTRATION(CBotChecker)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CBotChecker)

    User* m_pOwner; // +0x28
    std::vector<CBotCheckInfo> m_vInfo; // +0x30
    std::vector<std::u16string> m_vBotList; // +0x50
};
