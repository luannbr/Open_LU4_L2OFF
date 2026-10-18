#include "MarketPriceList.h"

#include "DB.h"
#include "Item.h"

#include "Shared/src/SharedFactory.h"
#include "Shared/src/ThreadSafeLibrary.h"

CMarketPriceList::CMarketPriceList() : m_warningFlag(false), m_padding61{} {}

CMarketPriceList::~CMarketPriceList() = default;

CMarketPriceList& CMarketPriceList::Instance() {
    // Current L2Server 0x0082cee0 uses this function-local owner and registers
    // its destructor with the CRT on first access.
    static CMarketPriceList marketPriceList;
    return marketPriceList;
}

void CMarketPriceList::Load() { Instance().RequestLoadMarketPriceList(); }

void CMarketPriceList::RequestLoadMarketPriceList() {
    // Current L2Server 0x0082c588.
    L2_GUARD_FUNCTION();
    db.RequestLoadMarketPriceList();
}

void CMarketPriceList::Put(int itemType, int enchant, double price) {
    // Current L2Server 0x0082ccd0.
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(m_lock);
        m_marketPrice[CMarketPriceKey(itemType, enchant)] = price;
    }
}

double CMarketPriceList::Get(int itemType, int enchant) {
    // Current L2Server 0x0082c980.
    L2_GUARD_FUNCTION();
    double result = 0.0;
    {
        L2_READ_GUARD(m_lock);
        const auto found = m_marketPrice.find(CMarketPriceKey(itemType, enchant));
        if (found != m_marketPrice.end()) {
            result = found->second;
        }
    }
    return result;
}

void CMarketPriceList::SwitchMarketPriceWarningFlag() {
    // Current L2Server 0x0082c57c.
    m_warningFlag = !m_warningFlag;
}

bool CMarketPriceList::GetMarketPriceWarning() {
    // Current L2Server 0x0082c578.
    return m_warningFlag;
}

void CMarketPriceList::SetMarketPriceWarning(bool enabled) { m_warningFlag = enabled; }

std::int64_t CMarketPriceList::GetMarketPrice(CItem* item) {
    // Current L2Server 0x0082ca84.
    L2_GUARD_FUNCTION();
    if (m_warningFlag) {
        const double price = Get(item->m_data->m_nClassID, item->m_data->GetEnchanted());
        if (price > 0.0) {
            return static_cast<std::int64_t>(price);
        }
    }
    return item->m_const->m_nDefaultPrice;
}

std::int64_t CMarketPriceList::GetMarketPrice(CSPointer<CItem> item) {
    // Current L2Server 0x0082cb50 consumes the smart pointer by value.
    L2_GUARD_FUNCTION();
    return GetMarketPrice(item.get());
}
