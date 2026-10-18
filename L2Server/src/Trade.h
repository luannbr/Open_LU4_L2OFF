#pragma once

#include "Economy.h"

#include "Shared/src/IOC.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeObjectMap.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cstddef>
#include <cstdint>

class User;
class CInventory;
class CItem;
class CPackageItemPacketManager;

class CTrade : public CEconomy {
public:
    CTrade(User* first, User* second);
    ~CTrade() override;

    L2_DECLARE_OBJECT_REGISTRATION(CTrade)

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    void OnCreate();
    void Delete();
    int AddItems(
        const unsigned char* packet,
        unsigned int traderNumber,
        CPackageItemPacketManager& traderPacketManager,
        CPackageItemPacketManager& otherPacketManager
    );
    bool CheckItemCount(const unsigned char* packet, int traderNumber);
    bool CheckItemRestrict();
    bool CheckIventoryWeightAndCount(const std::vector<int>& tradeList, CInventory& inventory);
    CItem* GetItem(User* user, int databaseId);
    int GetTradeItemWeight(int userId);
    bool SameDBItemExistInTradeList(CItem* item, unsigned int traderNumber);
    int SendTradableList(User* user);
    bool Request();
    bool RequestAddItems(const unsigned char* packet, User* user);
    bool PressDone(User* user);
    void Done();
    bool CheckTradeTimeout();
    bool Canceled(User* user, bool force);

private:
    friend class User;

    std::array<std::int32_t, 2> m_nTrader; // +0x28
    std::array<std::vector<int>, 2> m_TradeList; // +0x30
    std::array<bool, 2> m_bIsTradeDone; // +0x70
    bool m_bEnd; // +0x72
    bool m_bIsStart; // +0x73
    CYieldLock m_lock; // +0x74
    std::uint32_t m_dwTradeBegin; // +0x84
};

class CTradeManager {
public:
    CTradeManager();
    ~CTradeManager() = default;

    CSPointer<CTrade> GetTradeSP(int tradeId);
    bool DeleteTrade(int tradeId);
    int CreateTrade(User* first, User* second);

private:
    std::map<int, CTrade*> m_TradeSID_to_Trade; // +0x00
    CYieldLock m_lock; // +0x20
};

extern CTradeManager g_TradeManager;

class CTradeReleaseVerifier : public ::CIOObject {
public:
    explicit CTradeReleaseVerifier(CTrade* trade);
    ~CTradeReleaseVerifier() override = default;

    void TimerExpired(int id) override;

private:
    CTrade* m_pTrade;
};
