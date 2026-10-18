#pragma once

#include "Economy.h"

#include "Shared/src/MemorySub.h"
#include "Shared/src/ThreadSafeObjectMap.h"

#include <cstddef>
#include <cstdint>

class CNPC;
class User;

// Exact current BuySell.cpp/NpcSocket.cpp owner. Current RTTI fixes the
// sole CEconomy base and ten-slot vtable. The allocation at 0x008b7ab3 fixes
// the complete 0x838-byte extent.
class CBuy final : public CEconomy {
public:
    CBuy(std::int64_t buySellListId, int residenceId) noexcept;
    ~CBuy() override = default;

    L2_DECLARE_OBJECT_TYPE(CBuy)

    virtual std::int64_t GetListID() const { return m_nBuySellListID; }
    virtual int GetResidenceID() const { return m_nResidenceID; }
    virtual int GetTaxRate() const;
    virtual int GetBuyTaxRate() const;
    virtual int ReportShopTax(std::int64_t amount);

    void SetResultPage(char16_t* resultPage);
    char16_t* GetResultPage() { return m_pResultPage; }

protected:
    std::int32_t m_nResidenceID; // +0x28
    std::byte m_alignment2CTo30[0x04]; // +0x2c
    std::int64_t m_nBuySellListID; // +0x30
    char16_t m_pResultPage[1024]; // +0x38
};

// Exact current BuySell.cpp owner. The current RTTI fixes CEconomy as the
// sole direct base and the constructor at 0x0057e20c fixes the 0x48 extent.
class CBuySell final : public CEconomy {
public:
    ~CBuySell() override = default;

    L2_DECLARE_OBJECT_REGISTRATION(CBuySell)

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    virtual int GetTaxRate() const;
    virtual int GetBuyTaxRate() const;
    virtual int ReportShopTax(std::int64_t amount, bool isSell);

    static CSPointer<CBuySell> SetBuySell(
        User& user, CNPC& npc, std::int64_t itemNpcSellListId, std::int64_t itemNpcBuyBanListId, double ratio
    );

private:
    friend class User;

    CBuySell(std::int64_t itemNpcSellListId, std::int64_t itemNpcBuyBanListId, int residenceId, double ratio) noexcept;

    std::int64_t m_nItemNpcSellListID; // +0x28
    std::int64_t m_nItemNpcBuyBanListID; // +0x30
    std::int32_t m_nResidenceID; // +0x38
    std::byte m_alignment3CTo40[0x04]; // +0x3c
    double m_dRatio; // +0x40
};
