#include "BRProductServer.h"
#include "Shared/src/MemorySub.h"

#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"

#include <algorithm>
#include <array>
#include <ctime>
#include <tuple>

namespace {


auto CalendarKey(const SYSTEMTIME& value) {
    return std::tuple{value.wYear, value.wMonth, value.wDay, value.wHour, value.wMinute, value.wSecond, value.wMilliseconds};
}

SYSTEMTIME CurrentSystemTime() { return runtime::crt::LocalSystemTimeNow<SYSTEMTIME>(); }

std::int32_t SaleTimeToTime32(const SYSTEMTIME& value) noexcept {
    // The current CProduct helpers initialize only the six calendar lanes.
    // tm_wday, tm_yday and tm_isdst keep their incoming stack state.
    std::tm localTime;
    localTime.tm_year = static_cast<int>(value.wYear) - 1900;
    localTime.tm_mon = static_cast<int>(value.wMonth) - 1;
    localTime.tm_mday = static_cast<int>(value.wDay);
    localTime.tm_hour = static_cast<int>(value.wHour);
    localTime.tm_min = static_cast<int>(value.wMinute);
    localTime.tm_sec = static_cast<int>(value.wSecond);
    return runtime::crt::Mktime32(&localTime);
}

std::int32_t BirthdayToTime32(const SYSTEMTIME& value) noexcept {
    // Birthday conversion discards the record's time of day.
    std::tm localTime;
    localTime.tm_year = static_cast<int>(value.wYear) - 1900;
    localTime.tm_mon = static_cast<int>(value.wMonth) - 1;
    localTime.tm_mday = static_cast<int>(value.wDay);
    localTime.tm_hour = 0;
    localTime.tm_min = 0;
    localTime.tm_sec = 0;
    return runtime::crt::Mktime32(&localTime);
}

} // namespace

// Portable registration: the runtime ID is no longer a shipping number.
// static_assert(CProduct::kMemoryClassId == L2ServerMemory::kCProductClassId)

CProduct::CProduct(
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
) {
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(m_lock);
        m_productId = productId;
        CopyTargetString(m_name, name);
        m_category = category;
        m_paymentType = paymentType;
        m_price = price;
        m_tab = (eventProduct ? 1 : 0) | (bestProduct ? 2 : 0) | (newProduct ? 4 : 0);
        m_buyable = buyable;
        CopyTargetString(m_buyCondition, buyCondition);
        m_saleStartDate = saleStartDate;
        m_saleEndDate = saleEndDate;
        m_location = location;
        m_serverType = serverType;
        CopyTargetString(m_description, description);
        m_dayOfWeek = 0x7f;
        m_startTime.hour = 0;
        m_startTime.minute = 0;
        m_endTime.hour = 23;
        m_endTime.minute = 59;
        m_stock = -1;
        m_soldCount = 0;
        m_salePercent = 0;
        m_minLevel = 0;
        m_maxLevel = 0;
        m_minBirthday.wYear = 1970;
        m_minBirthday.wMonth = 1;
        m_minBirthday.wDay = 1;
        m_maxBirthday.wYear = 2037;
        m_maxBirthday.wMonth = 1;
        m_maxBirthday.wDay = 1;
        m_limitPeriod = 0;
        m_limitCount = 0;
    }
}

CProduct::~CProduct() = default;

L2_DEFINE_OBJECT_SINGLE_ALLOCATOR_LITERAL(CProduct)

L2_DEFINE_OBJECT_RELEASE(CProduct)

void CProduct::CopyTargetString(std::array<char16_t, 256>& destination, const char16_t* source) {
    if (source == nullptr) {
        destination[0] = u'\0';
        return;
    }
    std::size_t length = 0;
    while (length < destination.size() - 1 && source[length] != u'\0') {
        destination[length] = source[length];
        ++length;
    }
    destination[length] = u'\0';
}

void CProduct::SetCategory(int category) { m_category = category; }
void CProduct::SetItemPanelType(int itemPanelType) { m_itemPanelType = itemPanelType; }
void CProduct::SetEventProduct(bool enabled) { m_tab |= enabled ? 1 : 0; }
void CProduct::SetBestProduct(bool enabled) { m_tab |= enabled ? 2 : 0; }
void CProduct::SetNewProduct(bool enabled) { m_tab |= enabled ? 4 : 0; }
void CProduct::SetSaleStartDate(const SYSTEMTIME& value) {
    m_saleStartDate = value;
    m_saleStartDate.wYear = std::min<std::uint16_t>(m_saleStartDate.wYear, 2037);
}
void CProduct::SetSaleEndDate(const SYSTEMTIME& value) {
    m_saleEndDate = value;
    m_saleEndDate.wYear = std::min<std::uint16_t>(m_saleEndDate.wYear, 2037);
}
void CProduct::SetPrice(int paymentType, int price) {
    m_paymentType = paymentType;
    m_price = price;
}
void CProduct::SetLocation(int location) { m_location = location; }
void CProduct::SetServerType(int serverType) { m_serverType = serverType; }
void CProduct::SetDayOfWeek(int sunday, int monday, int tuesday, int wednesday, int thursday, int friday, int saturday) {
    m_dayOfWeek = static_cast<std::int8_t>(
        (sunday > 0 ? 0x01 : 0) | (monday > 0 ? 0x02 : 0) | (tuesday > 0 ? 0x04 : 0) | (wednesday > 0 ? 0x08 : 0) |
        (thursday > 0 ? 0x10 : 0) | (friday > 0 ? 0x20 : 0) | (saturday > 0 ? 0x40 : 0)
    );
}
void CProduct::SetTimeOfDay(int startHour, int startMinute, int endHour, int endMinute) {
    m_startTime.hour = static_cast<std::int8_t>(startHour);
    m_startTime.minute = static_cast<std::int8_t>(startMinute);
    m_endTime.hour = static_cast<std::int8_t>(endHour);
    m_endTime.minute = static_cast<std::int8_t>(endMinute);
}
void CProduct::SetStock(int stock) { m_stock = stock; }
void CProduct::SetSoldCount(int soldCount) {
    m_soldCount = soldCount;
    if (m_stock < soldCount) {
        m_soldCount = m_stock;
    }
}
void CProduct::SetSalePercent(int salePercent) { m_salePercent = salePercent; }
void CProduct::SetMinLevel(int minLevel) { m_minLevel = minLevel; }
void CProduct::SetMaxLevel(int maxLevel) { m_maxLevel = maxLevel; }
void CProduct::SetBirthdayStart(const SYSTEMTIME& value) { m_minBirthday = value; }
void CProduct::SetBirthdayEnd(const SYSTEMTIME& value) { m_maxBirthday = value; }
void CProduct::SetSaleLimit(int period, int count) {
    m_limitPeriod = period;
    m_limitCount = count;
}
void CProduct::SetBuyable(bool buyable) {
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(m_lock);
        m_buyable = buyable;
    }
}
void CProduct::SetName(const char16_t* name) { CopyTargetString(m_name, name); }

bool CProduct::AddItem(int itemId, int itemAmount) {
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(m_lock);
        for (ItemData& item : m_itemData) {
            if (item.typeId == itemId) {
                item.amount = std::max(0, runtime::AddInt32Wrapping(item.amount, itemAmount));
                return true;
            }
        }
        m_itemData.push_back(ItemData{itemId, itemAmount});
    }
    return true;
}

CProduct::ItemData* CProduct::GetItemData(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= m_itemData.size()) {
        return nullptr;
    }
    return &m_itemData[static_cast<std::size_t>(index)];
}

const CProduct::ItemData* CProduct::GetItemData(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= m_itemData.size()) {
        return nullptr;
    }
    return &m_itemData[static_cast<std::size_t>(index)];
}

int CProduct::GetSaleStartTime() const {
    L2_GUARD_FUNCTION();
    return SaleTimeToTime32(m_saleStartDate);
}

int CProduct::GetSaleEndTime() const {
    L2_GUARD_FUNCTION();
    return SaleTimeToTime32(m_saleEndDate);
}

int CProduct::CompareSaleDate() const {
    L2_GUARD_FUNCTION();
    const auto now = CalendarKey(CurrentSystemTime());
    if (now < CalendarKey(m_saleStartDate)) {
        return -1;
    }
    return now > CalendarKey(m_saleEndDate) ? 1 : 0;
}

int CProduct::GetMinBirthday() const {
    L2_GUARD_FUNCTION();
    return m_minBirthday.wYear == 1970 ? 0 : BirthdayToTime32(m_minBirthday);
}

int CProduct::GetMaxBirthday() const {
    L2_GUARD_FUNCTION();
    return m_maxBirthday.wYear == 2037 ? 0 : BirthdayToTime32(m_maxBirthday);
}

BrBuyProductResultType CProduct::CheckBuyableCondition(int amount) const {
    L2_GUARD_FUNCTION();
    const std::tm now = runtime::crt::LocalTime32OrZero(runtime::crt::Time32Now());
    if (!m_buyable) {
        return BR_BUY_CLOSED_PRODUCT;
    }
    const int dateRelation = CompareSaleDate();
    if (dateRelation == -1) {
        return BR_BUY_BEFORE_SALE_DATE;
    }
    if (dateRelation == 1) {
        return BR_BUY_AFTER_SALE_DATE;
    }
    if (amount > 0 && m_stock >= 0 && runtime::AddInt32Wrapping(m_soldCount, amount) > m_stock) {
        return BR_BUY_SOLD_OUT;
    }
    const auto dayMask = static_cast<std::uint8_t>(m_dayOfWeek);
    if (((dayMask >> static_cast<unsigned>(now.tm_wday)) & 1U) == 0U) {
        return BR_BUY_NOT_DAY_OF_WEEK;
    }
    const int currentMinute = now.tm_hour * 60 + now.tm_min;
    const int startMinute = static_cast<int>(m_startTime.hour) * 60 + static_cast<int>(m_startTime.minute);
    const int endMinute = static_cast<int>(m_endTime.hour) * 60 + static_cast<int>(m_endTime.minute);
    if (currentMinute < startMinute || endMinute < currentMinute) {
        return BR_BUY_NOT_TIME_OF_DAY;
    }
    return BR_BUY_SUCCESS;
}
