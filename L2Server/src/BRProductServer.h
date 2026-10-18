#pragma once

#include "Shared/src/BaseStructure.h"
#include "Shared/src/BrBuyProductResultType.h"
#include "Shared/src/Obj.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CParserForProduct;
class CPremiumService;
class CShopServer;

class CProduct : public MemoryObject {
public:
    struct ItemData {
        std::int32_t typeId;
        std::int32_t amount;
    };

    struct SaleTime {
        std::int8_t hour;
        std::int8_t minute;
    };

    L2_DECLARE_OBJECT_REGISTRATION(CProduct)

    CProduct(
        int productId,
        const char16_t* name,
        int category,
        int paymentType,
        int price,
        bool eventProduct,
        bool bestProduct,
        bool newProduct,
        bool buyable,
        const char16_t* buyCondition,
        const SYSTEMTIME& saleStartDate,
        const SYSTEMTIME& saleEndDate,
        int location,
        int serverType,
        const char16_t* description
    );
    ~CProduct() override;

    static void* operator new(std::size_t requestedSize);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    void SetCategory(int category);
    void SetItemPanelType(int itemPanelType);
    void SetEventProduct(bool enabled);
    void SetBestProduct(bool enabled);
    void SetNewProduct(bool enabled);
    void SetSaleStartDate(const SYSTEMTIME& value);
    void SetSaleEndDate(const SYSTEMTIME& value);
    void SetPrice(int paymentType, int price);
    void SetLocation(int location);
    void SetServerType(int serverType);
    void SetDayOfWeek(int sunday, int monday, int tuesday, int wednesday, int thursday, int friday, int saturday);
    void SetTimeOfDay(int startHour, int startMinute, int endHour, int endMinute);
    void SetStock(int stock);
    void SetSoldCount(int soldCount);
    void SetSalePercent(int salePercent);
    void SetMinLevel(int minLevel);
    void SetMaxLevel(int maxLevel);
    void SetBirthdayStart(const SYSTEMTIME& value);
    void SetBirthdayEnd(const SYSTEMTIME& value);
    void SetSaleLimit(int period, int count);
    void SetBuyable(bool buyable);
    void SetName(const char16_t* name);
    bool AddItem(int itemId, int itemAmount);

    ItemData* GetItemData(int index);
    const ItemData* GetItemData(int index) const;
    int GetSaleStartTime() const;
    int GetSaleEndTime() const;
    int CompareSaleDate() const;
    int GetMinBirthday() const;
    int GetMaxBirthday() const;
    BrBuyProductResultType CheckBuyableCondition(int amount) const;

    int GetProductId() const noexcept { return m_productId; }
    int GetItemCount() const noexcept { return static_cast<int>(m_itemData.size()); }

private:
    friend class CParserForProduct;
    friend class CPremiumService;
    friend class CShopServer;

    static void CopyTargetString(std::array<char16_t, 256>& destination, const char16_t* source);

public:
    // The current PremiumService/br_user/free packet handlers read these
    // members directly. They are therefore part of the original public
    // source boundary, not private state reached through invented accessors.

    RWLock m_lock; // +0x018
    std::int32_t m_productId; // +0x050
    std::array<char16_t, 256> m_name; // +0x054
    std::int32_t m_category; // +0x254
    std::int32_t m_paymentType; // +0x258
    std::int32_t m_price; // +0x25c
    std::int32_t m_itemPanelType; // +0x260
    std::int32_t m_tab; // +0x264
    bool m_buyable; // +0x268
    std::byte m_alignment269To26a; // +0x269
    std::array<char16_t, 256> m_buyCondition; // +0x26a
    SYSTEMTIME m_saleStartDate; // +0x46a
    SYSTEMTIME m_saleEndDate; // +0x47a
    std::array<std::byte, 2> m_alignment48aTo48c; // +0x48a
    std::int32_t m_location; // +0x48c
    std::int32_t m_serverType; // +0x490
    std::array<char16_t, 256> m_description; // +0x494
    std::int8_t m_dayOfWeek; // +0x694
    SaleTime m_startTime; // +0x695
    SaleTime m_endTime; // +0x697
    std::array<std::byte, 3> m_alignment699To69c;
    std::int32_t m_stock;
    std::int32_t m_soldCount;
    std::int32_t m_salePercent;
    std::int32_t m_minLevel;
    std::int32_t m_maxLevel;
    SYSTEMTIME m_minBirthday;
    SYSTEMTIME m_maxBirthday;
    std::int32_t m_limitPeriod;
    std::int32_t m_limitCount;
    std::vector<ItemData> m_itemData;
};

using CProductSP = CSPointer<CProduct>;
