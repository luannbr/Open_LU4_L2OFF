#pragma once

#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CItem;

template <typename TObject>
class CSPointer;


class CMarketPriceList {
public:
    class CMarketPriceKey {
    public:
        CMarketPriceKey() = default;
        CMarketPriceKey(int itemType, int enchant) : m_itemType(itemType), m_enchant(enchant) {}

        bool operator<(const CMarketPriceKey& other) const noexcept {
            return m_itemType < other.m_itemType || (m_itemType == other.m_itemType && m_enchant < other.m_enchant);
        }

        std::int32_t m_itemType;
        std::int32_t m_enchant;
    };

    virtual ~CMarketPriceList();

    void Put(int itemType, int enchant, double price);
    double Get(int itemType, int enchant);
    void SwitchMarketPriceWarningFlag();
    bool GetMarketPriceWarning();
    std::int64_t GetMarketPrice(CItem* item);
    std::int64_t GetMarketPrice(CSPointer<CItem> item);

    static CMarketPriceList& Instance();
    static void Load();

private:
    CMarketPriceList();
    CMarketPriceList(const CMarketPriceList&) = delete;
    CMarketPriceList& operator=(const CMarketPriceList&) = delete;

    void RequestLoadMarketPriceList();
    void SetMarketPriceWarning(bool enabled);

    std::map<CMarketPriceKey, double> m_marketPrice; // +0x08
    RWLock m_lock; // +0x28
    bool m_warningFlag; // +0x60
    [[maybe_unused]] std::byte m_padding61[7];
};
