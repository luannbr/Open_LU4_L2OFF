#include "CommissionTrade.h"
#include "DB.h"
#include "Item.h"
#include "ItemAssembler.h"
#include "L2Packet.h"
#include "ObjectDB.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/SharedFactory.h"
#include "User.h"
#include "UserSocket.h"

#include "Portable/ClientProtocolRuntime.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/ScopedLockGuards.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <string>

namespace {

constexpr const char* kCommissionTradeSource = "CommissionTrade.cpp";


void CopyCommissionUserName(char16_t (&destination)[25], const char16_t* const source) {
    if (source == nullptr) {
        destination[0] = u'\0';
        errno = EINVAL;
        runtime::process::HandleInvalidParameter();
        return;
    }
    std::size_t length = 0;
    while (length < 25 && source[length] != u'\0') {
        ++length;
    }
    if (length == 25) {
        destination[0] = u'\0';
        errno = ERANGE;
        runtime::process::HandleInvalidParameter();
        return;
    }
    std::copy_n(source, length + 1, destination);
}

template <typename TMap>
bool DeleteMappedObject(TMap& map, const std::int64_t commissionId, const int line) {
    return map.DeleteObject(commissionId, const_cast<char*>(kCommissionTradeSource), line);
}

} // namespace

#define L2_DEFINE_COMMISSION_RECORD(ClassToken)                                                                             \
    ClassToken::ClassToken(                                                                                                 \
        const std::int64_t commissionId, const Commission::Enum type, const CommissionExpired::Enum expiredType,            \
        const std::int32_t userDatabaseId, const std::int64_t itemPrice, const std::int32_t expiredDate,                    \
        CCommissionItem item, const char16_t* const userName                                                                \
    ) :                                                                                                                     \
        m_nCommissionID(commissionId),                                                                                      \
        m_eType(type),                                                                                                      \
        m_epType(expiredType),                                                                                              \
        m_nUserDBID(userDatabaseId),                                                                                        \
        m_padding2c{},                                                                                                      \
        m_ItemPrice(itemPrice),                                                                                             \
        m_tmExpiredDate(expiredDate),                                                                                       \
        m_padding3c{},                                                                                                      \
        m_pCommissionItem(new CCommissionItem(item)),                                                                       \
        m_sUserName{},                                                                                                      \
        m_targetTailPadding{} {                                                                                             \
        CopyCommissionUserName(m_sUserName, userName);                                                                      \
    }                                                                                                                       \
                                                                                                                            \
    ClassToken::~ClassToken() {                                                                                             \
        delete m_pCommissionItem;                                                                                           \
        m_pCommissionItem = nullptr;                                                                                        \
    }

L2_DEFINE_COMMISSION_RECORD(CommissionTradeWeapon)
L2_DEFINE_COMMISSION_RECORD(CommissionTradeArmor)
L2_DEFINE_COMMISSION_RECORD(CommissionTradeAccessary)
L2_DEFINE_COMMISSION_RECORD(CommissionTradeGoods)
L2_DEFINE_COMMISSION_RECORD(CommissionTradePet)
L2_DEFINE_COMMISSION_RECORD(CommissionTradeEtc)

#undef L2_DEFINE_COMMISSION_RECORD

ExpiredCommissionData::ExpiredCommissionData(
    const std::int64_t commissionId,
    const std::int32_t userDatabaseId,
    const std::int32_t itemId,
    const std::int32_t expiredDate,
    const Commission::Enum commissionType
) :
    m_nCommissionID(commissionId),
    m_nUserDBID(userDatabaseId),
    m_nItemID(itemId),
    m_tmExpiredDate(expiredDate),
    m_eType(commissionType) {}

ExpiredCommissionData::~ExpiredCommissionData() = default;

CCommissionTradeManager::CCommissionTradeManager() = default;

CCommissionTradeManager::~CCommissionTradeManager() = default;

CCommissionTradeManager& CCommissionTradeManager::Inst() {
    static CCommissionTradeManager instance;
    return instance;
}

void CCommissionTradeManager::Init() {
    // Current L2Server 0x005ada58.
    L2_GUARD_FUNCTION();
    for (volatile std::int32_t& job : m_nCommissionJob) {
        runtime::raw_atomic::ExchangeI32(&job, 1);
    }
    m_bLimit = true;
    db.RequestCommissionTradeLoadInfo();
}

void CCommissionTradeManager::SetCommissionStart() {
    // Current L2Server 0x005adb20.
    L2_GUARD_FUNCTION();
    for (volatile std::int32_t& job : m_nCommissionJob) {
        runtime::raw_atomic::ExchangeI32(&job, 0);
    }
    AddTimer(15000, 0);
}

bool CCommissionTradeManager::CheckCommissionCommon(User* const user) {
    // Current L2Server 0x005adbe4.
    L2_GUARD_FUNCTION();
    if (user->IsNowTrade()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_REGISTER_BUY_OR_CANCEL_ITEM_DURING_TRADE);
        return false;
    }
    if (user->GetPrivateStoreType() != PST_NONE) {
        user->SendSystemMessage(SystemMessageId::CANNOT_REGISTER_BUY_OR_CANCEL_ITEM_DURING_STORE_WORKSHOP_SEAL_OR_UNSEAL);
        return false;
    }
    if (user->GetPendingUseETCItem(PUIT_ENCHANT) != nullptr ||
        user->GetPendingUseETCItem(PUIT_ATTRIBUTE_ENCHANT) != nullptr ||
        user->GetPendingUseETCItem(PUIT_CRYSTAL) != nullptr || user->GetPendingUseETCItem(PUIT_ONE) != nullptr ||
        user->GetPendingUseETCItem(PUIT_TWO) != nullptr) {
        user->SendSystemMessage(SystemMessageId::CANNOT_USE_ITEM_AUCTION_DURING_ITEM_MODIFICATION);
        return false;
    }
    if (user->GetPendingUseETCItem(PUIT_SHAPE_SHIFTING) != nullptr) {
        return false;
    }
    if (user->m_data->m_nPrivateStoreType == PST_MANAGE) {
        user->SendSystemMessage(SystemMessageId::CANNOT_REGISTER_BUY_OR_CANCEL_ITEM_DURING_STORE_WORKSHOP_SEAL_OR_UNSEAL);
        return false;
    }
    if (user->GetPendingUseETCItem(PUIT_HLOCKED) != nullptr) {
        user->SendSystemMessage(SystemMessageId::CANNOT_REGISTER_BUY_OR_CANCEL_ITEM_DURING_STORE_WORKSHOP_SEAL_OR_UNSEAL);
        return false;
    }
    return true;
}

bool CCommissionTradeManager::CheckCommissionItem(CItem* const item) {
    // Current L2Server 0x005add88.
    L2_GUARD_FUNCTION();
    return item->IsTrade() && item->IsDrop() && item->m_const->m_bIsPrivateStore && !item->IsDuration() &&
        (item->m_itemOption.itemOptionKey.variationOption1 < 1 || item->m_itemOption.itemOptionKey.variationOption2 < 1) &&
        item->m_data->GetLocked() == 0;
}

bool CCommissionTradeManager::CheckCommissionRegister(User* const user, CItem* const item) {
    // Current L2Server 0x005adf2c.
    L2_GUARD_FUNCTION();
    if (!CheckCommissionCommon(user)) {
        return false;
    }
    if (CheckCommissionItem(item) && !item->IsPeriodicShapeShiftedItem(user)) {
        return true;
    }
    user->SendSystemMessage(SystemMessageId::ITEM_RESTRICTIONS_PREVENT_AUCTION_REGISTRATION);
    return false;
}

void CCommissionTradeManager::RequestCommissionTradeSearch(
    const int requestId,
    const int depth,
    const Commission::Enum commissionType,
    const int itemGeneral,
    const CrystalType itemGrade,
    SharedUtilsWideChar* const itemName
) {
    // Current L2Server 0x005ae038.
    L2_GUARD_FUNCTION();
    db.RequestCommissionTradeSearch(requestId, depth, commissionType, itemGeneral, itemGrade, itemName);
}

bool CCommissionTradeManager::CheckCommissionJob(const Commission::Enum type) {
    // Current L2Server 0x005ae118 is an unchecked aligned DWORD read.
    L2_GUARD_FUNCTION();
    return runtime::raw_atomic::LoadI32Relaxed(&m_nCommissionJob[static_cast<int>(type)]) == 0;
}

int CCommissionTradeManager::SetCommissionJob(const Commission::Enum type) {
    // Current L2Server 0x005ae1c0 returns the value present before the CAS.
    L2_GUARD_FUNCTION();
    return runtime::raw_atomic::CompareExchangeI32(&m_nCommissionJob[static_cast<int>(type)], 1, 0);
}

void CCommissionTradeManager::DoneCommissionJob(const Commission::Enum type) {
    // Current L2Server 0x005ae26c.
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::ExchangeI32(&m_nCommissionJob[static_cast<int>(type)], 0);
}

bool CCommissionTradeManager::CheckCommissionType(const Commission::Enum type) {
    // Current L2Server 0x005ae310 keeps the unusual accepted domain.
    L2_GUARD_FUNCTION();
    const int value = static_cast<int>(type);
    return value < static_cast<int>(Commission::Invalid) ||
        (value > static_cast<int>(Commission::None) && type != Commission::ShowNPC && type != Commission::TypeMAX);
}

void CCommissionTradeManager::CheckExpiredCommissionItem() {
    // Current L2Server 0x005b0098.
    L2_GUARD_FUNCTION();
    if (m_mapExpiredCommissionOne.GetSize() != 0) {
        CExpiredCommission function;
        m_mapExpiredCommissionOne.FOR_ALL(function);
    }
    if (m_mapExpiredCommissionThree.GetSize() != 0) {
        CExpiredCommission function;
        m_mapExpiredCommissionThree.FOR_ALL(function);
    }
    if (m_mapExpiredCommissionFive.GetSize() != 0) {
        CExpiredCommission function;
        m_mapExpiredCommissionFive.FOR_ALL(function);
    }
    if (m_mapExpiredCommissionSeven.GetSize() != 0) {
        CExpiredCommission function;
        m_mapExpiredCommissionSeven.FOR_ALL(function);
    }
    if (m_mapExpiredCommissionFifteen.GetSize() != 0) {
        CExpiredCommission function;
        m_mapExpiredCommissionFifteen.FOR_ALL(function);
    }
    if (m_mapExpiredCommissionThirty.GetSize() != 0) {
        CExpiredCommission function;
        m_mapExpiredCommissionThirty.FOR_ALL(function);
    }
}

void CCommissionTradeManager::TimerExpired(const int) {
    // Current L2Server 0x005b0210 ignores the timer identifier.
    L2_GUARD_FUNCTION();
    CheckExpiredCommissionItem();
    AddTimer(15000, 0);
}

bool CCommissionTradeManager::CheckCommissionInven(User* const user, const std::int64_t commissionId) {
    // Current L2Server 0x005b0738.
    L2_GUARD_FUNCTION();
    CSPointer<CommissionTradeAccessary> record = m_mapCommissionTradeAccessary.FindObject(commissionId);
    CObject* object = nullptr;
    if (record && record->m_pCommissionItem != nullptr) {
        object = g_ObjectDB.GetObjectW(static_cast<std::int32_t>(record->m_pCommissionItem->m_nItemClassID), false);
    }
    const bool artifact = object != nullptr && object->IsItem() && object->IsArtifact();
    if (!artifact) {
        const int total = user->m_inv.GetTotalCount();
        const int limit = user->GetInventoryLimit();
        if (static_cast<float>(total) <= static_cast<float>(limit) * 0.9F &&
            // BUG: user->m_data->m_nNumInventory < 3
            // Current EXE 0x005b08fb compares the weight-penalty tier at
            // User+0x2b70 with 2, not the shared inventory item count.
            user->m_nCurrentWeight < WEIGHT_OVERWEIGHT) {
            return true;
        }
        user->SendSystemMessage(SystemMessageId::CANNOT_BUY_OR_CANCEL_ITEM_INVENTORY_LIMIT_RESTRICTION);
        user->SendSystemMessage(SystemMessageId::NOT_ENOUGH_INVENTORY_SPACE);
        return false;
    }

    const int total = user->m_inv.GetArtifactCount();
    const int limit = user->GetArtifactInventoryLimit();
    if (static_cast<float>(total) <= static_cast<float>(limit) * 0.9F) {
        return true;
    }
    user->SendSystemMessage(SystemMessageId::CANNOT_BUY_OR_CANCEL_ITEM_INVENTORY_LIMIT_RESTRICTION);
    user->SendSystemMessage(SystemMessageId::NOT_ENOUGH_ARTIFACT_INVENTORY_SPACE);
    return false;
}

bool CCommissionTradeManager::CheckCommissionDelete(
    User* const user, const std::int32_t itemId, const std::int64_t commissionId, const CommissionOwner::Enum owner
) {
    // Current L2Server 0x005b09a8.
    L2_GUARD_FUNCTION();
    if (owner == CommissionOwner::CoUser && (!CheckCommissionCommon(user) || !CheckCommissionInven(user, commissionId))) {
        return false;
    }

    const auto matches = [user, itemId, commissionId](auto& map) {
        auto record = map.FindObject(commissionId);
        return record && record->m_nUserDBID == static_cast<std::int32_t>(user->GetDBID()) &&
            record->m_pCommissionItem != nullptr &&
            static_cast<std::int32_t>(record->m_pCommissionItem->m_nItemID) == itemId;
    };
    return matches(m_mapCommissionTradeWeapon) || matches(m_mapCommissionTradeArmor) ||
        matches(m_mapCommissionTradeAccessary) || matches(m_mapCommissionTradeGoods) || matches(m_mapCommissionTradePet) ||
        matches(m_mapCommissionTradeEtc);
}

bool CCommissionTradeManager::CheckCommissionBuy(const std::int64_t commissionId) {
    // Current L2Server 0x005b1128.
    L2_GUARD_FUNCTION();
    if (m_mapCommissionTradeWeapon.FindObject(commissionId)) {
        return true;
    }
    if (m_mapCommissionTradeArmor.FindObject(commissionId)) {
        return true;
    }
    if (m_mapCommissionTradeAccessary.FindObject(commissionId)) {
        return true;
    }
    if (m_mapCommissionTradeGoods.FindObject(commissionId)) {
        return true;
    }
    if (m_mapCommissionTradePet.FindObject(commissionId)) {
        return true;
    }
    if (m_mapCommissionTradeEtc.FindObject(commissionId)) {
        return true;
    }
    return false;
}

void CCommissionTradeManager::RequestCommissionTradeRegTotalCount(
    const std::int32_t characterId, const std::int32_t itemServerId
) {
    // Current L2Server 0x005b1614.
    L2_GUARD_FUNCTION();
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        return;
    }
    static_cast<void>(user->GetSocket());
    if (CheckCommissionCommon(user.get())) {
        db.RequestCommissionTradeRegTotalCount(characterId, itemServerId);
    }
}

void CCommissionTradeManager::RequestCommissionTradeRegister(
    const CommissionOwner::Enum owner,
    const std::int32_t characterId,
    const std::int32_t itemId,
    SharedUtilsWideChar* const itemName,
    const std::int64_t itemPrice,
    const std::int64_t itemAmount,
    const int itemConsumeType,
    const CommissionExpired::Enum expiredType,
    const Commission::Enum commissionType,
    const CommissionDiscountInfo::Enum discountType,
    const CommissionItemClassId::Enum premiumItemClassId,
    const int enchanted
) {
    // Current L2Server 0x005b1748.
    L2_GUARD_FUNCTION();

    std::int64_t days = 0;
    switch (expiredType) {
        case CommissionExpired::EpOne:
            days = 1;
            break;
        case CommissionExpired::EpThree:
            days = 3;
            break;
        case CommissionExpired::EpFive:
            days = 5;
            break;
        case CommissionExpired::EpSeven:
        case CommissionExpired::EpFifteen:
        case CommissionExpired::EpThirty:
            days = 7;
            break;
        default:
            break;
    }

    double discountRate = 1.0;
    if (discountType == CommissionDiscountInfo::CDI_30PERCENT_DISCOUNT) {
        discountRate = 0.7;
    } else if (discountType == CommissionDiscountInfo::CDI_NO_COMMISSION) {
        discountRate = 0.0;
    }

    const std::int64_t grossForFee =
        runtime::MultiplyInt64Wrapping(runtime::MultiplyInt64Wrapping(days, itemPrice), itemAmount);
    const std::int64_t undiscountedFee = runtime::Win64CvttDoubleToInt64(static_cast<double>(grossForFee) * 0.0001);
    std::int64_t commissionFee = runtime::Win64CvttDoubleToInt64(static_cast<double>(undiscountedFee) * discountRate);
    if (discountType != CommissionDiscountInfo::CDI_NO_COMMISSION && commissionFee < 10000) {
        commissionFee = runtime::Win64CvttDoubleToInt64(discountRate * 1000.0);
    }

    std::int64_t minimumSalePrice =
        runtime::Win64CvttDoubleToInt64(static_cast<double>(days) * 0.005 * static_cast<double>(itemPrice));
    if (minimumSalePrice < 10000) {
        minimumSalePrice = 1000;
    }

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        DoneCommissionJob(commissionType);
        return;
    }

    int premiumItemId = 0;
    if (premiumItemClassId != CommissionItemClassId::CICI_NONE) {
        CItem* const premiumItem = user->m_inv.GetFirstItemByClassID(static_cast<int>(premiumItemClassId), false, false);
        if (premiumItem == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[F2P Commission] Don't Have F2P Commission "
            //     L"Premium Item(%d) %s, %d",
            //     static_cast<int>(premiumItemClassId),
            //     L"CommissionTrade.cpp",
            //     0x241)
            // L2_LOG_ERROR(Log,
            //     L"[F2P Commission] Don't Have F2P Commission "
            //     L"Premium Item(%d) %s, %d",
            //     static_cast<int>(premiumItemClassId),
            //     L"CommissionTrade.cpp",
            //     0x241)
            L2_LOG_WARN(
                Log, "Commission registration cannot continue: character database ID %d lacks premium item class %d.",
                characterId, static_cast<int>(premiumItemClassId)
            );
            return;
        }
        premiumItemId = static_cast<int>(premiumItem->GetDBID());
    }

    const auto fail = [&](const int systemMessage) {
        DoneCommissionJob(commissionType);
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->CheckAndResetPendingUseETCItem(PUIT_COMMISSION);
        user->SendSystemMessage(systemMessage);
        user->GetSocket()->Send("chd", 0xfe, 0xf5, 0);
    };

    const std::int64_t adena = user->m_inv.GetAdenaAmount();
    const std::int64_t saleTotal = runtime::MultiplyInt64Wrapping(itemPrice, itemAmount);
    if (runtime::SubtractInt64Wrapping(saleTotal, minimumSalePrice) < 1) {
        fail(SystemMessageId::CANNOT_REGISTER_ITEM_REQUIREMENTS_NOT_MET);
        return;
    }
    if (runtime::SubtractInt64Wrapping(adena, commissionFee) < 0) {
        fail(SystemMessageId::NOT_ENOUGH_ADENA_FOR_COMMISSION_ITEM_REGISTRATION);
        return;
    }

    CItem* const pendingItem = user->GetPendingUseETCItem(PUIT_COMMISSION);
    if (pendingItem == nullptr) {
        fail(SystemMessageId::CANNOT_REGISTER_MISSING_ITEM);
        return;
    }
    CItem* const inventoryItem = user->m_inv.GetByServerID(static_cast<int>(pendingItem->m_data->m_nId));
    if (inventoryItem == nullptr) {
        fail(SystemMessageId::CANNOT_REGISTER_MISSING_ITEM);
        return;
    }
    const CrystalType crystalType = inventoryItem->m_const->m_Crystal;
    const GItemType::Enum generalType = static_cast<GItemType::Enum>(inventoryItem->m_const->m_GeneralItemType);
    if (!CheckCommissionType(commissionType) || !CheckCommissionRegister(user.get(), pendingItem)) {
        fail(SystemMessageId::CANNOT_REGISTER_ITEM_REQUIREMENTS_NOT_MET);
        return;
    }
    const CommissionFirst::Enum firstType = static_cast<CommissionFirst::Enum>(pendingItem->m_const->m_CommissionFirstType);
    if (firstType == static_cast<CommissionFirst::Enum>(-1)) {
        fail(SystemMessageId::CANNOT_REGISTER_ITEM_REQUIREMENTS_NOT_MET);
        return;
    }

    const std::u16string userName = user->GetName();
    db.RequestCommissionTradeRegister(
        owner, commissionType, characterId, itemId, itemName, itemPrice, itemAmount, itemConsumeType, commissionFee,
        expiredType, userName.c_str(), crystalType, generalType, firstType, discountType,
        static_cast<int>(premiumItemClassId), premiumItemId, 1, enchanted
    );
}

std::int32_t CCommissionTradeManager::GetCommissionItemDBID(const std::int64_t commissionId) {
    // Current L2Server 0x005b2418.
    L2_GUARD_FUNCTION();
    const auto getItemId = [commissionId](auto& map, bool& found) {
        auto record = map.FindObject(commissionId);
        found = static_cast<bool>(record);
        return found ? static_cast<std::int32_t>(record->m_pCommissionItem->m_nItemID) : std::int32_t{0};
    };
    bool found = false;
    std::int32_t itemId = getItemId(m_mapCommissionTradeWeapon, found);
    if (found) {
        return itemId;
    }
    itemId = getItemId(m_mapCommissionTradeArmor, found);
    if (found) {
        return itemId;
    }
    itemId = getItemId(m_mapCommissionTradeAccessary, found);
    if (found) {
        return itemId;
    }
    itemId = getItemId(m_mapCommissionTradeGoods, found);
    if (found) {
        return itemId;
    }
    itemId = getItemId(m_mapCommissionTradePet, found);
    if (found) {
        return itemId;
    }
    return getItemId(m_mapCommissionTradeEtc, found);
}

std::int64_t CCommissionTradeManager::GetCommissionPrice(const std::int64_t commissionId) {
    // Current L2Server 0x005b2944.
    L2_GUARD_FUNCTION();
    const auto getPrice = [commissionId](auto& map, bool& found) {
        auto record = map.FindObject(commissionId);
        found = static_cast<bool>(record);
        return found ? record->m_ItemPrice : std::int64_t{0};
    };
    bool found = false;
    std::int64_t price = getPrice(m_mapCommissionTradeWeapon, found);
    if (found) {
        return price;
    }
    price = getPrice(m_mapCommissionTradeArmor, found);
    if (found) {
        return price;
    }
    price = getPrice(m_mapCommissionTradeAccessary, found);
    if (found) {
        return price;
    }
    price = getPrice(m_mapCommissionTradeGoods, found);
    if (found) {
        return price;
    }
    price = getPrice(m_mapCommissionTradePet, found);
    if (found) {
        return price;
    }
    return getPrice(m_mapCommissionTradeEtc, found);
}

bool CCommissionTradeManager::CheckMyCommissionItem(const std::int64_t commissionId, const std::int32_t characterId) {
    // Current L2Server 0x005b2e64 checks every map.
    L2_GUARD_FUNCTION();
    bool result = false;
    CSPointer<CommissionTradeWeapon> weapon = m_mapCommissionTradeWeapon.FindObject(commissionId);
    if (weapon && weapon->m_nUserDBID == characterId) {
        result = true;
    }
    CSPointer<CommissionTradeArmor> armor = m_mapCommissionTradeArmor.FindObject(commissionId);
    if (armor && armor->m_nUserDBID == characterId) {
        result = true;
    }
    CSPointer<CommissionTradeAccessary> accessary = m_mapCommissionTradeAccessary.FindObject(commissionId);
    if (accessary && accessary->m_nUserDBID == characterId) {
        result = true;
    }
    CSPointer<CommissionTradeGoods> goods = m_mapCommissionTradeGoods.FindObject(commissionId);
    if (goods && goods->m_nUserDBID == characterId) {
        result = true;
    }
    CSPointer<CommissionTradePet> pet = m_mapCommissionTradePet.FindObject(commissionId);
    if (pet && pet->m_nUserDBID == characterId) {
        result = true;
    }
    CSPointer<CommissionTradeEtc> etc = m_mapCommissionTradeEtc.FindObject(commissionId);
    if (etc && etc->m_nUserDBID == characterId) {
        result = true;
    }
    return result;
}

bool CCommissionTradeManager::CheckItemRestrictInCommmision(const std::int64_t commissionId) {
    // Current L2Server 0x005b30ec keeps the source spelling and checks
    // every map even after one restricted item is found.
    L2_GUARD_FUNCTION();
    bool result = false;
    CSPointer<CommissionTradeWeapon> weapon = m_mapCommissionTradeWeapon.FindObject(commissionId);
    if (weapon && weapon->m_pCommissionItem != nullptr &&
        CItemRestrict::Inst().CheckItemRestrict(static_cast<int>(weapon->m_pCommissionItem->m_nItemClassID))) {
        result = true;
    }
    CSPointer<CommissionTradeArmor> armor = m_mapCommissionTradeArmor.FindObject(commissionId);
    if (armor && armor->m_pCommissionItem != nullptr &&
        CItemRestrict::Inst().CheckItemRestrict(static_cast<int>(armor->m_pCommissionItem->m_nItemClassID))) {
        result = true;
    }
    CSPointer<CommissionTradeAccessary> accessary = m_mapCommissionTradeAccessary.FindObject(commissionId);
    if (accessary && accessary->m_pCommissionItem != nullptr &&
        CItemRestrict::Inst().CheckItemRestrict(static_cast<int>(accessary->m_pCommissionItem->m_nItemClassID))) {
        result = true;
    }
    CSPointer<CommissionTradeGoods> goods = m_mapCommissionTradeGoods.FindObject(commissionId);
    if (goods && goods->m_pCommissionItem != nullptr &&
        CItemRestrict::Inst().CheckItemRestrict(static_cast<int>(goods->m_pCommissionItem->m_nItemClassID))) {
        result = true;
    }
    CSPointer<CommissionTradePet> pet = m_mapCommissionTradePet.FindObject(commissionId);
    if (pet && pet->m_pCommissionItem != nullptr &&
        CItemRestrict::Inst().CheckItemRestrict(static_cast<int>(pet->m_pCommissionItem->m_nItemClassID))) {
        result = true;
    }
    CSPointer<CommissionTradeEtc> etc = m_mapCommissionTradeEtc.FindObject(commissionId);
    if (etc && etc->m_pCommissionItem != nullptr &&
        CItemRestrict::Inst().CheckItemRestrict(static_cast<int>(etc->m_pCommissionItem->m_nItemClassID))) {
        result = true;
    }
    return result;
}

std::int64_t CCommissionTradeManager::GetCommissionAmount(const std::int64_t commissionId) {
    // Current L2Server 0x005b3520.
    L2_GUARD_FUNCTION();
    const auto getAmount = [commissionId](auto& map, bool& found) {
        auto record = map.FindObject(commissionId);
        found = static_cast<bool>(record);
        return found && record->m_pCommissionItem != nullptr ? record->m_pCommissionItem->m_nAmount : std::int64_t{0};
    };
    bool found = false;
    std::int64_t amount = getAmount(m_mapCommissionTradeWeapon, found);
    if (found) {
        return amount;
    }
    amount = getAmount(m_mapCommissionTradeArmor, found);
    if (found) {
        return amount;
    }
    amount = getAmount(m_mapCommissionTradeAccessary, found);
    if (found) {
        return amount;
    }
    amount = getAmount(m_mapCommissionTradeGoods, found);
    if (found) {
        return amount;
    }
    amount = getAmount(m_mapCommissionTradePet, found);
    if (found) {
        return amount;
    }
    return getAmount(m_mapCommissionTradeEtc, found);
}

void CCommissionTradeManager::RequestCommissionTradeBuyItem(
    const std::int32_t characterId,
    const std::int64_t commissionId,
    const Commission::Enum commissionType,
    const CommissionOwner::Enum owner
) {
    // Current L2Server 0x005b3a58.
    L2_GUARD_FUNCTION();
    const std::int64_t itemAmount = GetCommissionAmount(commissionId);
    const std::int64_t itemPrice = GetCommissionPrice(commissionId);
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        DoneCommissionJob(commissionType);
        return;
    }

    const auto fail = [&](const int systemMessage) {
        DoneCommissionJob(commissionType);
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        if (systemMessage != 0) {
            user->SendSystemMessage(systemMessage);
        }
        user->GetSocket()->Send("chd", 0xfe, 0xf9, 0);
    };

    if (CheckMyCommissionItem(commissionId, characterId)) {
        fail(SystemMessageId::ITEM_PURCHASE_FAILED);
        return;
    }
    if (CheckItemRestrictInCommmision(commissionId)) {
        fail(0);
        return;
    }
    if (!CheckCommissionBuy(commissionId)) {
        fail(SystemMessageId::ITEM_PURCHASE_FAILED);
        return;
    }
    const std::int64_t totalPrice = runtime::MultiplyInt64Wrapping(itemPrice, itemAmount);
    if (runtime::SubtractInt64Wrapping(user->m_inv.GetAdenaAmount(), totalPrice) < 0) {
        fail(SystemMessageId::NOT_ENOUGH_ADENA);
        return;
    }
    if (CheckCommissionCommon(user.get()) && CheckCommissionInven(user.get(), commissionId)) {
        db.RequestCommissionTradeBuyItem(characterId, commissionId, itemAmount, owner);
        return;
    }
    fail(0);
}

void CCommissionTradeManager::RequestCommissionTradeRegisteredList(const std::int32_t characterId) {
    // Current L2Server 0x005b4070.
    L2_GUARD_FUNCTION();
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user && user->GetSocket() != nullptr) {
        db.RequestCommissionTradeRegisteredList(characterId);
    }
}

void CCommissionTradeManager::RequestCommissionTradeDelete(
    const CommissionOwner::Enum owner,
    const std::int64_t commissionId,
    const std::int32_t characterId,
    const std::int32_t itemId,
    const Commission::Enum commissionType
) {
    // Current L2Server 0x005b42a8.
    L2_GUARD_FUNCTION();
    const std::int64_t amount = GetCommissionAmount(commissionId);
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        if (CheckCommissionType(commissionType)) {
            db.RequestCommissionTradeDelete(owner, commissionId, characterId, itemId, amount, commissionType);
        } else {
            DoneCommissionJob(commissionType);
        }
        return;
    }

    if (CheckCommissionType(commissionType) && CheckCommissionDelete(user.get(), itemId, commissionId, owner)) {
        db.RequestCommissionTradeDelete(owner, commissionId, characterId, itemId, amount, commissionType);
        return;
    }

    DoneCommissionJob(commissionType);
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
    user->SendSystemMessage(SystemMessageId::SALE_CANCELLATION_FAILED_REQUIREMENTS_NOT_MET);
    user->GetSocket()->Send("chd", 0xfe, 0xf6, 0);
}

int CCommissionTradeManager::ExpiredUserCommission(const std::int64_t commissionId) {
    // Current L2Server 0x005b9c9c.
    L2_GUARD_FUNCTION();
    CSPointer<ExpiredCommissionData> record = m_mapExpiredCommissionOne.FindObject(commissionId);
    if (!record) {
        record = m_mapExpiredCommissionThree.FindObject(commissionId);
    }
    if (!record) {
        record = m_mapExpiredCommissionFive.FindObject(commissionId);
    }
    if (!record) {
        record = m_mapExpiredCommissionSeven.FindObject(commissionId);
    }
    if (!record) {
        record = m_mapExpiredCommissionFifteen.FindObject(commissionId);
    }
    if (!record) {
        record = m_mapExpiredCommissionThirty.FindObject(commissionId);
    }
    if (!record) {
        return 3;
    }

    if (SetCommissionJob(record->m_eType) != 0) {
        return 2;
    }
    RequestCommissionTradeDelete(
        CommissionOwner::CoTimer, record->m_nCommissionID, record->m_nUserDBID, record->m_nItemID, record->m_eType
    );
    return 1;
}

void CCommissionTradeManager::RequestCommissionTradeDeleteAll(const int characterId) {
    // Current L2Server 0x005ade84.
    L2_GUARD_FUNCTION();
    db.RequestCommissionTradeDeleteAll(characterId);
}

void CCommissionTradeManager::RegisterExpiredCommissionData(
    const std::int64_t commissionId,
    const std::int32_t userDatabaseId,
    const std::int32_t itemId,
    const CommissionExpired::Enum expiredType,
    const std::int32_t expiredDate,
    const Commission::Enum commissionType
) {
    // Current L2Server 0x005b5b90.
    L2_GUARD_FUNCTION();
    ExpiredCommissionData* const record =
        new ExpiredCommissionData(commissionId, userDatabaseId, itemId, expiredDate, commissionType);

    bool inserted = false;
    switch (expiredType) {
        case CommissionExpired::EpOne:
            inserted =
                m_mapExpiredCommissionOne.Insert(commissionId, record, const_cast<char*>(kCommissionTradeSource), 0x407);
            if (!inserted) {
                // Log.Add(LOG_ERROR,
                //     "[%s][%d] InsertExpiredCommissionData Fail(1)!! CommissionID[%I64d]",
                //     kCommissionTradeSource, 0x409, commissionId)
                // L2_LOG_ERROR(Log,
                //     "[%s][%d] InsertExpiredCommissionData Fail(1)!! CommissionID[%I64d]",
                //     kCommissionTradeSource, 0x409, commissionId)
                L2_LOG_ERROR(
                    Log,
                    "Expired commission %lld could not be registered in expiry group 1 (character database ID %d, item ID "
                    "%d).",
                    static_cast<long long>(commissionId), static_cast<int>(userDatabaseId), static_cast<int>(itemId)
                );
            }
            break;
        case CommissionExpired::EpThree:
            inserted =
                m_mapExpiredCommissionThree.Insert(commissionId, record, const_cast<char*>(kCommissionTradeSource), 0x40d);
            if (!inserted) {
                // Log.Add(LOG_ERROR,
                //     "[%s][%d] InsertExpiredCommissionData Fail(3)!! CommissionID[%I64d]",
                //     kCommissionTradeSource, 0x40f, commissionId)
                // L2_LOG_ERROR(Log,
                //     "[%s][%d] InsertExpiredCommissionData Fail(3)!! CommissionID[%I64d]",
                //     kCommissionTradeSource, 0x40f, commissionId)
                L2_LOG_ERROR(
                    Log,
                    "Expired commission %lld could not be registered in expiry group 3 (character database ID %d, item ID "
                    "%d).",
                    static_cast<long long>(commissionId), static_cast<int>(userDatabaseId), static_cast<int>(itemId)
                );
            }
            break;
        case CommissionExpired::EpFive:
            inserted =
                m_mapExpiredCommissionFive.Insert(commissionId, record, const_cast<char*>(kCommissionTradeSource), 0x413);
            if (!inserted) {
                // Log.Add(LOG_ERROR,
                //     "[%s][%d] InsertExpiredCommissionData Fail(5)!! CommissionID[%I64d]",
                //     kCommissionTradeSource, 0x415, commissionId)
                // L2_LOG_ERROR(Log,
                //     "[%s][%d] InsertExpiredCommissionData Fail(5)!! CommissionID[%I64d]",
                //     kCommissionTradeSource, 0x415, commissionId)
                L2_LOG_ERROR(
                    Log,
                    "Expired commission %lld could not be registered in expiry group 5 (character database ID %d, item ID "
                    "%d).",
                    static_cast<long long>(commissionId), static_cast<int>(userDatabaseId), static_cast<int>(itemId)
                );
            }
            break;
        case CommissionExpired::EpSeven:
            inserted =
                m_mapExpiredCommissionSeven.Insert(commissionId, record, const_cast<char*>(kCommissionTradeSource), 0x419);
            if (!inserted) {
                // Log.Add(LOG_ERROR,
                //     "[%s][%d] InsertExpiredCommissionData Fail(7)!! CommissionID[%I64d]",
                //     kCommissionTradeSource, 0x41b, commissionId)
                // L2_LOG_ERROR(Log,
                //     "[%s][%d] InsertExpiredCommissionData Fail(7)!! CommissionID[%I64d]",
                //     kCommissionTradeSource, 0x41b, commissionId)
                L2_LOG_ERROR(
                    Log,
                    "Expired commission %lld could not be registered in expiry group 7 (character database ID %d, item ID "
                    "%d).",
                    static_cast<long long>(commissionId), static_cast<int>(userDatabaseId), static_cast<int>(itemId)
                );
            }
            break;
        case CommissionExpired::EpFifteen:
            inserted =
                m_mapExpiredCommissionFifteen.Insert(commissionId, record, const_cast<char*>(kCommissionTradeSource), 0x421);
            if (!inserted) {
                // Log.Add(LOG_ERROR,
                //     "[%s][%d] InsertExpiredCommissionData Fail(15)!! CommissionID[%I64d]",
                //     kCommissionTradeSource, 0x423, commissionId)
                // L2_LOG_ERROR(Log,
                //     "[%s][%d] InsertExpiredCommissionData Fail(15)!! CommissionID[%I64d]",
                //     kCommissionTradeSource, 0x423, commissionId)
                L2_LOG_ERROR(
                    Log,
                    "Expired commission %lld could not be registered in expiry group 15 (character database ID %d, item ID "
                    "%d).",
                    static_cast<long long>(commissionId), static_cast<int>(userDatabaseId), static_cast<int>(itemId)
                );
            }
            break;
        case CommissionExpired::EpThirty:
            inserted =
                m_mapExpiredCommissionThirty.Insert(commissionId, record, const_cast<char*>(kCommissionTradeSource), 0x427);
            if (!inserted) {
                // Log.Add(LOG_ERROR,
                //     "[%s][%d] InsertExpiredCommissionData Fail(30)!! CommissionID[%I64d]",
                //     kCommissionTradeSource, 0x429, commissionId)
                // L2_LOG_ERROR(Log,
                //     "[%s][%d] InsertExpiredCommissionData Fail(30)!! CommissionID[%I64d]",
                //     kCommissionTradeSource, 0x429, commissionId)
                L2_LOG_ERROR(
                    Log,
                    "Expired commission %lld could not be registered in expiry group 30 (character database ID %d, item ID "
                    "%d).",
                    static_cast<long long>(commissionId), static_cast<int>(userDatabaseId), static_cast<int>(itemId)
                );
            }
            break;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d] InsertExpiredCommissionData Fail!! CommissionID[%I64d], epType[%d]",
            //     kCommissionTradeSource,
            //     0x42e,
            //     commissionId,
            //     static_cast<int>(expiredType))
            // L2_LOG_ERROR(Log,
            //     "[%s][%d] InsertExpiredCommissionData Fail!! CommissionID[%I64d], epType[%d]",
            //     kCommissionTradeSource,
            //     0x42e,
            //     commissionId,
            //     static_cast<int>(expiredType))
            L2_LOG_ERROR(
                Log, "Expired commission %lld was not registered: unsupported expiry type %d.",
                static_cast<long long>(commissionId), static_cast<int>(expiredType)
            );
            break;
    }
    L2_RELEASE(record, ObjectRefType::CreateDelete);
}

void CCommissionTradeManager::RegisterCommissionItem(
    const std::int64_t commissionId,
    const std::int32_t userDatabaseId,
    const char16_t* const itemName,
    const std::int64_t itemPrice,
    const std::int64_t itemAmount,
    const std::int32_t expiredDate,
    CCommissionItem item,
    const Commission::Enum commissionType,
    const CommissionExpired::Enum expiredType,
    const char16_t* const userName,
    const CommissionFirst::Enum firstType
) {
    // Current L2Server 0x005b9730. itemName and itemAmount are present in
    // the source signature but are not consumed by the current body.
    L2_GUARD_FUNCTION();
    static_cast<void>(itemName);
    static_cast<void>(itemAmount);

    const auto insert = [=](auto& map, auto* record, int line) {
        map.Insert(commissionId, record, const_cast<char*>(kCommissionTradeSource), line);
        L2_RELEASE(record, ObjectRefType::CreateDelete);
    };

    switch (firstType) {
        case CommissionFirst::Weapon: {
            auto* record = new CommissionTradeWeapon(
                commissionId, commissionType, expiredType, userDatabaseId, itemPrice, expiredDate, item, userName
            );
            insert(m_mapCommissionTradeWeapon, record, 0x440);
            break;
        }
        case CommissionFirst::Armor: {
            auto* record = new CommissionTradeArmor(
                commissionId, commissionType, expiredType, userDatabaseId, itemPrice, expiredDate, item, userName
            );
            insert(m_mapCommissionTradeArmor, record, 0x448);
            break;
        }
        case CommissionFirst::Accessary: {
            auto* record = new CommissionTradeAccessary(
                commissionId, commissionType, expiredType, userDatabaseId, itemPrice, expiredDate, item, userName
            );
            insert(m_mapCommissionTradeAccessary, record, 0x450);
            break;
        }
        case CommissionFirst::Goods: {
            auto* record = new CommissionTradeGoods(
                commissionId, commissionType, expiredType, userDatabaseId, itemPrice, expiredDate, item, userName
            );
            insert(m_mapCommissionTradeGoods, record, 0x458);
            break;
        }
        case CommissionFirst::Pet: {
            auto* record = new CommissionTradePet(
                commissionId, commissionType, expiredType, userDatabaseId, itemPrice, expiredDate, item, userName
            );
            insert(m_mapCommissionTradePet, record, 0x460);
            break;
        }
        case CommissionFirst::Etc: {
            auto* record = new CommissionTradeEtc(
                commissionId, commissionType, expiredType, userDatabaseId, itemPrice, expiredDate, item, userName
            );
            insert(m_mapCommissionTradeEtc, record, 0x468);
            break;
        }
        default:
            break;
    }
}

void CCommissionTradeManager::DeleteCommissionItem(const std::int64_t commissionId, const CommissionFirst::Enum firstType) {
    // Current L2Server 0x005b5e88.
    L2_GUARD_FUNCTION();
    switch (firstType) {
        case CommissionFirst::Weapon:
            DeleteMappedObject(m_mapCommissionTradeWeapon, commissionId, 0x47a);
            return;
        case CommissionFirst::Armor:
            DeleteMappedObject(m_mapCommissionTradeArmor, commissionId, 0x47e);
            return;
        case CommissionFirst::Accessary:
            DeleteMappedObject(m_mapCommissionTradeAccessary, commissionId, 0x482);
            return;
        case CommissionFirst::Goods:
            DeleteMappedObject(m_mapCommissionTradeGoods, commissionId, 0x486);
            return;
        case CommissionFirst::Pet:
            DeleteMappedObject(m_mapCommissionTradePet, commissionId, 0x48a);
            return;
        case CommissionFirst::Etc:
            DeleteMappedObject(m_mapCommissionTradeEtc, commissionId, 0x48e);
            return;
        default:
            DeleteMappedObject(m_mapCommissionTradeWeapon, commissionId, 0x492);
            DeleteMappedObject(m_mapCommissionTradeArmor, commissionId, 0x493);
            DeleteMappedObject(m_mapCommissionTradeAccessary, commissionId, 0x494);
            DeleteMappedObject(m_mapCommissionTradeGoods, commissionId, 0x495);
            DeleteMappedObject(m_mapCommissionTradePet, commissionId, 0x496);
            DeleteMappedObject(m_mapCommissionTradeEtc, commissionId, 0x497);
            return;
    }
}

void CCommissionTradeManager::DeleteExpiredCommissionData(
    const std::int64_t commissionId, const CommissionExpired::Enum expiredType
) {
    // Current L2Server 0x005b6074.
    L2_GUARD_FUNCTION();
    if (expiredType == CommissionExpired::EpInvalid) {
        if (DeleteMappedObject(m_mapExpiredCommissionOne, commissionId, 0x4d1) ||
            DeleteMappedObject(m_mapExpiredCommissionThree, commissionId, 0x4d5) ||
            DeleteMappedObject(m_mapExpiredCommissionFive, commissionId, 0x4d9) ||
            DeleteMappedObject(m_mapExpiredCommissionSeven, commissionId, 0x4dd) ||
            DeleteMappedObject(m_mapExpiredCommissionFifteen, commissionId, 0x4e3) ||
            DeleteMappedObject(m_mapExpiredCommissionThirty, commissionId, 0x4e7)) {
            return;
        }
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] DeleteExpiredCommissionData Fail(-1)!! CommissionID[%I64d], epType[%d]",
        //     kCommissionTradeSource,
        //     0x4ec,
        //     commissionId,
        //     static_cast<int>(expiredType))
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] DeleteExpiredCommissionData Fail(-1)!! CommissionID[%I64d], epType[%d]",
        //     kCommissionTradeSource,
        //     0x4ec,
        //     commissionId,
        //     static_cast<int>(expiredType))
        L2_LOG_ERROR(
            Log, "Expired commission %lld could not be removed from any expiry group.", static_cast<long long>(commissionId)
        );
        return;
    }

    bool deleted = false;
    // const char* failure = nullptr
    [[maybe_unused]] const char* failure = nullptr;
    // int failureLine = 0
    [[maybe_unused]] int failureLine = 0;
    switch (expiredType) {
        case CommissionExpired::EpOne:
            deleted = DeleteMappedObject(m_mapExpiredCommissionOne, commissionId, 0x4a4);
            failure = "[%s][%d] DeleteExpiredCommissionData Fail(1)!! CommissionID[%I64d]";
            failureLine = 0x4a6;
            break;
        case CommissionExpired::EpThree:
            deleted = DeleteMappedObject(m_mapExpiredCommissionThree, commissionId, 0x4aa);
            failure = "[%s][%d] DeleteExpiredCommissionData Fail(3)!! CommissionID[%I64d]";
            failureLine = 0x4ac;
            break;
        case CommissionExpired::EpFive:
            deleted = DeleteMappedObject(m_mapExpiredCommissionFive, commissionId, 0x4b0);
            failure = "[%s][%d] DeleteExpiredCommissionData Fail(5)!! CommissionID[%I64d]";
            failureLine = 0x4b2;
            break;
        case CommissionExpired::EpSeven:
            deleted = DeleteMappedObject(m_mapExpiredCommissionSeven, commissionId, 0x4b6);
            failure = "[%s][%d] DeleteExpiredCommissionData Fail(7)!! CommissionID[%I64d]";
            failureLine = 0x4b8;
            break;
        case CommissionExpired::EpFifteen:
            deleted = DeleteMappedObject(m_mapExpiredCommissionFifteen, commissionId, 0x4be);
            failure = "[%s][%d] DeleteExpiredCommissionData Fail(15)!! CommissionID[%I64d]";
            failureLine = 0x4c2;
            break;
        case CommissionExpired::EpThirty:
            deleted = DeleteMappedObject(m_mapExpiredCommissionThirty, commissionId, 0x4c7);
            failure = "[%s][%d] DeleteExpiredCommissionData Fail(30)!! CommissionID[%I64d]";
            failureLine = 0x4cb;
            break;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d] DeleteExpiredCommissionData Fail(-2)!! CommissionID[%I64d], epType[%d]",
            //     kCommissionTradeSource,
            //     0x4ef,
            //     commissionId,
            //     static_cast<int>(expiredType))
            // L2_LOG_ERROR(Log,
            //     "[%s][%d] DeleteExpiredCommissionData Fail(-2)!! CommissionID[%I64d], epType[%d]",
            //     kCommissionTradeSource,
            //     0x4ef,
            //     commissionId,
            //     static_cast<int>(expiredType))
            L2_LOG_ERROR(
                Log, "Expired commission %lld was not removed: unsupported expiry type %d.",
                static_cast<long long>(commissionId), static_cast<int>(expiredType)
            );
            return;
    }
    if (!deleted) {
        // Log.Add(
        //     LOG_ERROR,
        //     failure,
        //     kCommissionTradeSource,
        //     failureLine,
        //     commissionId)
        // L2_LOG_ERROR(Log,
        //     failure,
        //     kCommissionTradeSource,
        //     failureLine,
        //     commissionId)
        L2_LOG_ERROR(
            Log, "Expired commission %lld could not be removed from expiry type %d.", static_cast<long long>(commissionId),
            static_cast<int>(expiredType)
        );
    }
}

void CCommissionTradeManager::RequestCommissionTradeInfo(User* const user, CItem* const item) {
    // Current L2Server 0x005afa90.
    L2_GUARD_FUNCTION();
    int itemClassId = 0;
    std::int64_t itemPrice = 0;
    std::int64_t itemAmount = 0;
    CommissionExpired::Enum expiredType = CommissionExpired::EpInvalid;
    CommissionDiscountInfo::Enum discountType = CommissionDiscountInfo::CDI_DEFAULT;
    const Commission::Enum commissionType = static_cast<Commission::Enum>(item->m_const->m_CommissionType);
    if (CheckCommissionType(commissionType) && CheckCommissionRegister(user, item)) {
        user->GetCommissionHistory(item, itemClassId, itemPrice, itemAmount, expiredType, discountType);
        user->GetSocket()->Send(
            "chddQQdd", 0xfe, 0xf4, 1, itemClassId, itemPrice, itemAmount, static_cast<int>(expiredType),
            static_cast<int>(discountType)
        );
        return;
    }
    user->SendSystemMessage(SystemMessageId::CANNOT_REGISTER_ITEM_REQUIREMENTS_NOT_MET);
    user->GetSocket()->Send("chd", 0xfe, 0xf4, 0);
    user->CheckAndResetPendingUseETCItem(PUIT_COMMISSION);
}

void CCommissionTradeManager::RequestCommissionTradeBuyInfo(
    const std::int32_t characterId, const std::int64_t commissionId, const Commission::Enum commissionType
) {
    // Current L2Server 0x005b6420. The decoded type is unused.
    L2_GUARD_FUNCTION();
    static_cast<void>(commissionType);
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        return;
    }

    CUserSocket* const socket = user->GetSocket();
    CPacketHelperEx<20480> packet;
    if (!CheckCommissionCommon(user.get()) || !CheckCommissionInven(user.get(), commissionId)) {
        socket->Send("chd", 0xfe, 0xf8, 0);
        return;
    }
    if (socket == nullptr) {
        return;
    }

    auto weaponRecord = m_mapCommissionTradeWeapon.FindObject(commissionId);
    if (weaponRecord) {
        CItemAssemblerCommission assembler(weaponRecord->m_pCommissionItem);
        assembler.AssembleItemInfo(packet);
        socket->Send(
            "chdQQdb", 0xfe, 0xf8, 1, weaponRecord->m_ItemPrice, weaponRecord->m_nCommissionID,
            static_cast<int>(weaponRecord->m_eType), packet.GetLength(), packet.GetPacket()
        );
        return;
    }

    auto armorRecord = m_mapCommissionTradeArmor.FindObject(commissionId);
    if (armorRecord) {
        CItemAssemblerCommission assembler(armorRecord->m_pCommissionItem);
        assembler.AssembleItemInfo(packet);
        socket->Send(
            "chdQQdb", 0xfe, 0xf8, 1, armorRecord->m_ItemPrice, armorRecord->m_nCommissionID,
            static_cast<int>(armorRecord->m_eType), packet.GetLength(), packet.GetPacket()
        );
        return;
    }

    auto accessaryRecord = m_mapCommissionTradeAccessary.FindObject(commissionId);
    if (accessaryRecord) {
        CItemAssemblerCommission assembler(accessaryRecord->m_pCommissionItem);
        assembler.AssembleItemInfo(packet);
        socket->Send(
            "chdQQdb", 0xfe, 0xf8, 1, accessaryRecord->m_ItemPrice, accessaryRecord->m_nCommissionID,
            static_cast<int>(accessaryRecord->m_eType), packet.GetLength(), packet.GetPacket()
        );
        return;
    }

    auto goodsRecord = m_mapCommissionTradeGoods.FindObject(commissionId);
    if (goodsRecord) {
        CItemAssemblerCommission assembler(goodsRecord->m_pCommissionItem);
        assembler.AssembleItemInfo(packet);
        socket->Send(
            "chdQQdb", 0xfe, 0xf8, 1, goodsRecord->m_ItemPrice, goodsRecord->m_nCommissionID,
            static_cast<int>(goodsRecord->m_eType), packet.GetLength(), packet.GetPacket()
        );
        return;
    }

    auto petRecord = m_mapCommissionTradePet.FindObject(commissionId);
    if (petRecord) {
        CItemAssemblerCommission assembler(petRecord->m_pCommissionItem);
        assembler.AssembleItemInfo(packet);
        socket->Send(
            "chdQQdb", 0xfe, 0xf8, 1, petRecord->m_ItemPrice, petRecord->m_nCommissionID,
            static_cast<int>(petRecord->m_eType), packet.GetLength(), packet.GetPacket()
        );
        return;
    }

    auto etcRecord = m_mapCommissionTradeEtc.FindObject(commissionId);
    if (etcRecord) {
        CItemAssemblerCommission assembler(etcRecord->m_pCommissionItem);
        assembler.AssembleItemInfo(packet);
        socket->Send(
            "chdQQdb", 0xfe, 0xf8, 1, etcRecord->m_ItemPrice, etcRecord->m_nCommissionID,
            static_cast<int>(etcRecord->m_eType), packet.GetLength(), packet.GetPacket()
        );
        return;
    }

    user->SendSystemMessage(SystemMessageId::CANNOT_BUY_MISSING_ITEM);
    socket->Send("chd", 0xfe, 0xf8, 0);
}

void CCommissionTradeManager::RequestCommissionTradeItemList(User* const user) {
    // Current L2Server 0x005b8830.
    L2_GUARD_FUNCTION();
    CPackageItemPacketManager packetManager;
    {
        L2_READ_GUARD(user->m_inv);
        for (CSharedItemData* data = user->m_inv.GetFirst(); data != nullptr; data = data->Next()) {
            CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(data->index);
            if (!item || !CheckCommissionType(static_cast<Commission::Enum>(item->m_const->m_CommissionType)) ||
                !CheckCommissionItem(item.get()) || item->IsPeriodicShapeShiftedItem(user) ||
                user->IsEquipedItem(item->m_data->index)) {
                continue;
            }

            packetManager.CheckLimitPackageItemPacketDataSize();
            CItemAssembler assembler(*user, *item.get(), false);
            const int assembled = packetManager.PackageAssembleItem(&assembler);
            if (assembled < 1) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"Assembe Error at file[%s], line[%d], "
                //     L"assemble result = [%d], total length = [%d]",
                //     L"CommissionTrade.cpp",
                //     0x19d,
                //     assembled,
                //     packetManager.GetTotalPackageItemLength())
                // L2_LOG_ERROR(Log,
                //     L"Assembe Error at file[%s], line[%d], "
                //     L"assemble result = [%d], total length = [%d]",
                //     L"CommissionTrade.cpp",
                //     0x19d,
                //     assembled,
                //     packetManager.GetTotalPackageItemLength())
                L2_LOG_ERROR(
                    Log,
                    "Commission inventory list could not encode an item (result %d, accumulated length %d); continuing with "
                    "the next entry.",
                    assembled, static_cast<int>(packetManager.GetTotalPackageItemLength())
                );
            } else {
                packetManager.SetAssembleItemLengthPerItem(assembled);
                packetManager.IncreasePackageItemCount();
            }
        }
    }

    CUserSocket* const socket = user->GetSocket();
    socket->Send("chcd", S_EX, S_EX_RESPONSE_COMMISSION_ITEM_LIST, 1, packetManager.GetTotalPackageItemCount());
    packetManager.SendPacket_PackageItemData(
        socket, S_EX, S_EX_RESPONSE_COMMISSION_ITEM_LIST, PackageItemPacket::PIPT_ITEM_INFO
    );
}

void CCommissionTradeManager::CommissionList(
    const CommissionStatus::Enum status, const int characterId, int remainingCount, const unsigned char*& packet
) {
    // Current L2Server 0x005b8c08.
    L2_GUARD_FUNCTION();
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        return;
    }
    CUserSocket* const socket = user->GetSocket();
    if (socket == nullptr) {
        return;
    }

    int page = (remainingCount - 1) / 120;
    if (page < 0) {
        return;
    }
    const int pageCount = page + 1;
    for (int pageLoop = 0; pageLoop < pageCount; ++pageLoop, --page) {
        CPacketHelperEx<20480> pagePacket;
        int appended = 0;
        const int rowCount = remainingCount > 120 ? 120 : remainingCount;
        for (int row = 0; row < rowCount; ++row) {
            std::int64_t commissionId = 0;
            CommissionFirst::Enum firstType = CommissionFirst::FirstMAX;
            packet = ::Disassemble(packet, "Qd", &commissionId, &firstType);

            switch (firstType) {
                case CommissionFirst::Weapon: {
                    auto record = m_mapCommissionTradeWeapon.FindObject(commissionId);
                    if (record) {
                        pagePacket.Assemble(
                            "QQdddS", record->m_nCommissionID, record->m_ItemPrice, static_cast<int>(record->m_eType),
                            static_cast<int>(record->m_epType), record->m_tmExpiredDate, record->m_sUserName
                        );
                        CItemAssemblerCommission assembler(record->m_pCommissionItem);
                        assembler.AssembleItemInfo(pagePacket);
                        ++appended;
                        --remainingCount;
                    }
                    break;
                }
                case CommissionFirst::Armor: {
                    auto record = m_mapCommissionTradeArmor.FindObject(commissionId);
                    if (record) {
                        pagePacket.Assemble(
                            "QQdddS", record->m_nCommissionID, record->m_ItemPrice, static_cast<int>(record->m_eType),
                            static_cast<int>(record->m_epType), record->m_tmExpiredDate, record->m_sUserName
                        );
                        CItemAssemblerCommission assembler(record->m_pCommissionItem);
                        assembler.AssembleItemInfo(pagePacket);
                        ++appended;
                        --remainingCount;
                    }
                    break;
                }
                case CommissionFirst::Accessary: {
                    auto record = m_mapCommissionTradeAccessary.FindObject(commissionId);
                    if (record) {
                        pagePacket.Assemble(
                            "QQdddS", record->m_nCommissionID, record->m_ItemPrice, static_cast<int>(record->m_eType),
                            static_cast<int>(record->m_epType), record->m_tmExpiredDate, record->m_sUserName
                        );
                        CItemAssemblerCommission assembler(record->m_pCommissionItem);
                        assembler.AssembleItemInfo(pagePacket);
                        ++appended;
                        --remainingCount;
                    }
                    break;
                }
                case CommissionFirst::Goods: {
                    auto record = m_mapCommissionTradeGoods.FindObject(commissionId);
                    if (record) {
                        pagePacket.Assemble(
                            "QQdddS", record->m_nCommissionID, record->m_ItemPrice, static_cast<int>(record->m_eType),
                            static_cast<int>(record->m_epType), record->m_tmExpiredDate, record->m_sUserName
                        );
                        CItemAssemblerCommission assembler(record->m_pCommissionItem);
                        assembler.AssembleItemInfo(pagePacket);
                        ++appended;
                        --remainingCount;
                    }
                    break;
                }
                case CommissionFirst::Pet: {
                    auto record = m_mapCommissionTradePet.FindObject(commissionId);
                    if (record) {
                        pagePacket.Assemble(
                            "QQdddS", record->m_nCommissionID, record->m_ItemPrice, static_cast<int>(record->m_eType),
                            static_cast<int>(record->m_epType), record->m_tmExpiredDate, record->m_sUserName
                        );
                        CItemAssemblerCommission assembler(record->m_pCommissionItem);
                        assembler.AssembleItemInfo(pagePacket);
                        ++appended;
                        --remainingCount;
                    }
                    break;
                }
                case CommissionFirst::Etc: {
                    auto record = m_mapCommissionTradeEtc.FindObject(commissionId);
                    if (record) {
                        pagePacket.Assemble(
                            "QQdddS", record->m_nCommissionID, record->m_ItemPrice, static_cast<int>(record->m_eType),
                            static_cast<int>(record->m_epType), record->m_tmExpiredDate, record->m_sUserName
                        );
                        CItemAssemblerCommission assembler(record->m_pCommissionItem);
                        assembler.AssembleItemInfo(pagePacket);
                        ++appended;
                        --remainingCount;
                    }
                    break;
                }
                default:
                    break;
            }
        }

        if (appended == 0) {
            if (status == CommissionStatus::CsRegisteredFail) {
                user->SendSystemMessage(SystemMessageId::SEARCHED_ITEM_NOT_FOUND);
                socket->Send("chd", 0xfe, 0xf7, -2);
            } else {
                user->SendSystemMessage(SystemMessageId::NO_REGISTERED_ITEMS);
                socket->Send("chd", 0xfe, 0xf7, -1);
            }
            continue;
        }
        socket->Send(
            "chddddb", 0xfe, 0xf7, status == CommissionStatus::CsSearchOk ? 3 : 2, runtime::crt::Time32Now(), page,
            appended, pagePacket.GetLength(), pagePacket.GetPacket()
        );
    }
}

bool CExpiredCommission::Do(MemoryObject* const object) {
    // Current L2Server 0x005b9bdc.
    auto* const record = static_cast<ExpiredCommissionData*>(object);
    if (record->m_tmExpiredDate < runtime::crt::Time32Now()) {
        CCommissionTradeManager& manager = CCommissionTradeManager::Inst();
        if (manager.SetCommissionJob(record->m_eType) == 0) {
            manager.RequestCommissionTradeDelete(
                CommissionOwner::CoTimer, record->m_nCommissionID, record->m_nUserDBID, record->m_nItemID, record->m_eType
            );
            return false;
        }
    }
    return true;
}
