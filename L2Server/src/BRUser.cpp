#include "BRShopServer.h"
#include "CConfig.h"
#include "DB.h"
#include "Default.h"
#include "Item.h"
#include "ObjectDB.h"
#include "PremiumService.h"
#include "Protocol/src/SystemMessageIds.h"
#include "RuneItem.h"
#include "Skill.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/FHtml.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TextRuntime.h"

#include <array>
#include <charconv>
#include <cstring>

namespace {

void BrUserI64ToAscii(const std::int64_t value, char* const buffer, const std::size_t bufferSize) noexcept {
#if defined(_MSC_VER)
    static_cast<void>(::_i64toa_s(value, buffer, bufferSize, 10));
#else
    static_cast<void>(std::to_chars(buffer, buffer + bufferSize, value, 10));
#endif
}

std::int32_t BrUserCreateDateToTime32(const SYSTEMTIME& value) noexcept {
    // Current User::CheckBuyProduct initializes only the six calendar
    // inputs before _mktime32. Tm_wday/tm_yday/tm_isdst remain untouched.
    std::tm localTime;
    localTime.tm_year = static_cast<int>(value.wYear) - 1900;
    localTime.tm_mon = static_cast<int>(value.wMonth) - 1;
    localTime.tm_mday = static_cast<int>(value.wDay);
    localTime.tm_hour = static_cast<int>(value.wHour);
    localTime.tm_min = static_cast<int>(value.wMinute);
    localTime.tm_sec = static_cast<int>(value.wSecond);
    return runtime::crt::Mktime32(&localTime);
}


void DeleteFirstRuneSkill(User& user, const std::vector<const CSkillInfo*>& skills) {
    if (!skills.empty() && skills.front() != nullptr) {
        user._DeleteAcquireSkill(skills.front()->m_nSkillId);
    }
}

void AcquireSelectedRuneSkill(User& user, const std::vector<const CSkillInfo*>& skills, MaterialType materialType) {
    if (skills.empty()) {
        return;
    }

    const int lastSkillIndex = static_cast<int>(skills.size()) - 1;
    const int itemIndex = user.m_inv.GetRuneItemCount(materialType) - 1;
    const int selectedIndex = itemIndex < lastSkillIndex ? itemIndex : lastSkillIndex;
    if (selectedIndex < 0) {
        return;
    }

    const CSkillInfo* const skill = runtime::CheckedVectorAt(skills, static_cast<std::size_t>(selectedIndex));
    if (skill != nullptr && skill->CheckOperateCond(&user, &user, CSkillInfo::OCT_PASSIVE) == -2) {
        user._AcquireSkill(skill->m_nSkillId, skill->m_nLevel, skill->m_nSubLevel, true, skill->m_bSaveToDB);
    }
}

} // namespace

void User::AddExpBonusPercent(double bonusPercent) {
    // Current L2Server 0x004ce858.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(*m_lock);

        const double updated = m_SkillDurationMod.m_dExpBonusPercent + bonusPercent;
        if (updated >= 0.0) {
            m_SkillDurationMod.m_dExpBonusPercent = updated;
        } else {
            m_SkillDurationMod.m_dExpBonusPercent = 0.0;
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] EXP Bonus Percent Out of Bounds - "
            //     L"ExpBonusPercent : %d",
            //     kBrUserSourceFile,
            //     0x7f,
            //     updated)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] EXP Bonus Percent Out of Bounds - "
            //     L"ExpBonusPercent : %d",
            //     kBrUserSourceFile,
            //     0x7f,
            //     updated)
            L2_LOG_ERROR(Log, "Invalid EXP bonus %.17g percent after update; the stored bonus was reset to zero.", updated);
        }

        if (m_SkillDurationMod.m_dExpBonusPercent > 1200.0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] EXP Bonus Percent Out of Bounds - "
            //     L"UserName:%s, ExpBonusPercent:%d",
            //     kBrUserSourceFile,
            //     0x8c,
            //     GetName(),
            //     m_SkillDurationMod.m_dExpBonusPercent)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] EXP Bonus Percent Out of Bounds - "
            //     L"UserName:%s, ExpBonusPercent:%d",
            //     kBrUserSourceFile,
            //     0x8c,
            //     GetName(),
            //     m_SkillDurationMod.m_dExpBonusPercent)
            // L2_LOG_ERROR(Log,
            //     "Player '%s' has EXP bonus %.17g percent above 1200. The value is retained.",
            //     WideToUtf8(std::wstring_view(GetName())).c_str(), m_SkillDurationMod.m_dExpBonusPercent)
            L2_LOG_ERROR(
                Log, "Player '%s' has EXP bonus %.17g percent above 1200; the value is retained.",
                Utf16ToUtf8(GetName()).c_str(), m_SkillDurationMod.m_dExpBonusPercent
            );
        }
    }
}

void User::AddSPBonusPercent(double bonusPercent) {
    // Current L2Server 0x004ce9c4.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(*m_lock);

        const double updated = m_SkillDurationMod.m_dSPBonusPercent + bonusPercent;
        if (updated >= 0.0) {
            m_SkillDurationMod.m_dSPBonusPercent = updated;
        } else {
            m_SkillDurationMod.m_dSPBonusPercent = 0.0;
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] SP Bonus Percent Out of Bounds - "
            //     L"SPBonusPercent : %d",
            //     kBrUserSourceFile,
            //     0xa8,
            //     updated)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] SP Bonus Percent Out of Bounds - "
            //     L"SPBonusPercent : %d",
            //     kBrUserSourceFile,
            //     0xa8,
            //     updated)
            L2_LOG_ERROR(Log, "Invalid SP bonus %.17g percent after update; the stored bonus was reset to zero.", updated);
        }

        if (m_SkillDurationMod.m_dSPBonusPercent > 1200.0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] SP Bonus Percent Out of Bounds - "
            //     L"UserName:%s, ExpBonusPercent:%d",
            //     kBrUserSourceFile,
            //     0xb5,
            //     GetName(),
            //     m_SkillDurationMod.m_dSPBonusPercent)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] SP Bonus Percent Out of Bounds - "
            //     L"UserName:%s, ExpBonusPercent:%d",
            //     kBrUserSourceFile,
            //     0xb5,
            //     GetName(),
            //     m_SkillDurationMod.m_dSPBonusPercent)
            // L2_LOG_ERROR(Log,
            //     "Player '%s' has SP bonus %.17g percent above 1200. The value is retained.",
            //     WideToUtf8(std::wstring_view(GetName())).c_str(), m_SkillDurationMod.m_dSPBonusPercent)
            L2_LOG_ERROR(
                Log, "Player '%s' has SP bonus %.17g percent above 1200; the value is retained.",
                Utf16ToUtf8(GetName()).c_str(), m_SkillDurationMod.m_dSPBonusPercent
            );
        }
    }
}

void User::SetCrystalGradepBonus(double bonus) {
    // Current L2Server 0x004ceb30, original owner br_user.cpp.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(*m_lock);
        m_dCrystalGradeBonus = bonus;
    }
}

void User::StartBuyProduct() {
    // Current L2Server 0x004cf084, original owner br_user.cpp.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(*m_lock);
        m_bIsBuyingProduct = true;
    }
}

bool User::IsBuyingProduct() { return m_bIsBuyingProduct; }

void User::EndBuyProduct() {
    // Current L2Server 0x004cf158, original owner br_user.cpp.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(*m_lock);
        m_bIsBuyingProduct = false;
    }
}

void User::RequestBuyProduct(
    const int productId,
    const int productAmount,
    const bool gift,
    wchar_t* const receiverName,
    wchar_t* const title,
    wchar_t* const content
) {
    // Current L2Server 0x004cf224, original owner br_user.cpp.
    L2_GUARD_FUNCTION();
    StartBuyProduct();
    const BrBuyProductResultType result =
        g_PremiumSvc.SendBuyItemReq(static_cast<int>(Id()), productId, productAmount, gift, receiverName, title, content);
    if (result != BR_BUY_SUCCESS) {
        GetSocket()->Send("chd", 0xfe, gift ? 0x11a : 0xd9, static_cast<int>(result));
        EndBuyProduct();
    }
}

void User::_OnRuneItemsChange() {
    // Current L2Server 0x004d0a94.
    L2_GUARD_FUNCTION();

    DeleteFirstRuneSkill(*this, CRuneItem::m_RuneSelectSkillListXP);
    DeleteFirstRuneSkill(*this, CRuneItem::m_RuneSelectSkillListSP);
    DeleteFirstRuneSkill(*this, CRuneItem::m_RuneSelectSkillListRP);

    for (const CSkillInfo* const skill : CRuneItem::g_RuneItemSkillSet) {
        if (skill != nullptr) {
            _DeleteAcquireSkill(skill->m_nSkillId);
        }
    }

    AcquireSelectedRuneSkill(*this, CRuneItem::m_RuneSelectSkillListXP, MATT_RUNE_XP);
    AcquireSelectedRuneSkill(*this, CRuneItem::m_RuneSelectSkillListSP, MATT_RUNE_SP);
    AcquireSelectedRuneSkill(*this, CRuneItem::m_RuneSelectSkillListRP, MATT_RUNE_REMOVE_PENALTY);

    for (const std::uint32_t classId : m_inv.m_RuneItemClassIds) {
        CObject* const object = g_ObjectDB.GetObjectW(static_cast<std::int32_t>(classId), false);
        if (object == nullptr || !object->IsItem()) {
            continue;
        }

        CItem* const item = static_cast<CItem*>(object);
        for (const CSkillInfo* const skill : item->m_ItemSkillList) {
            if (skill != nullptr && skill->CheckOperateCond(this, this, CSkillInfo::OCT_PASSIVE) == -2) {
                _AcquireSkill(skill->m_nSkillId, skill->m_nLevel, skill->m_nSubLevel, false, skill->m_bSaveToDB);
            }
        }
    }
}

void User::OnRuneItemsChange() {
    // Current L2Server 0x004d0ed8.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_AcquiredSkillsSect);
    _OnRuneItemsChange();
}

void User::SendBrAgathionEnergyInfo() {
    // Current L2Server 0x004d0fa4, original owner br_user.cpp.
    L2_GUARD_FUNCTION();

    int itemIndex = m_inv.GetFirstIndex();
    CPacketHelperEx<20480> packet;
    int itemCount = 0;
    int depth = 0;
    while (itemIndex != -1) {
        CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemIndex);
        if (item && item->m_const->m_dAgathionMaxEnergy > 0.0) {
            packet.Assemble(
                "ddQdd", item->m_data->m_nId, item->GetClassId(), item->m_data->m_Slot,
                runtime::Win64CvttDoubleToInt32(item->GetAgathionEnergy() + 0.5),
                runtime::Win64CvttDoubleToInt32(item->m_const->m_dAgathionMaxEnergy + 0.5)
            );
            ++itemCount;
        }

        itemIndex = CItemFactory::Inst().GetNextIndex(itemIndex);
        ++depth;
        if (depth > 0x192) {
            // Log.Add(
            //     LOG_ERROR,
            //     "depth > %d at file[%s], line[%d]",
            //     0x192,
            //     "br_user.cpp",
            //     0x363)
            // L2_LOG_ERROR(Log,
            //     "depth > %d at file[%s], line[%d]",
            //     0x192,
            //     "br_user.cpp",
            //     0x363)
            L2_LOG_ERROR(
                Log,
                "Agathion energy scan reached %d inventory entries (next index %d, %d records); scanning stops and any "
                "accumulated records are sent.",
                depth, itemIndex, itemCount
            );
            break;
        }
    }

    if (itemCount > 0) {
        GetSocket()->Send("chdb", 0xfe, 0xdf, itemCount, packet.GetLength(), packet.GetPacket());
    }
}

bool User::CheckSystemBuff(const SYSTEM_BUFF_TYPE type) {
    // Current L2Server 0x004d01b0.
    L2_GUARD_FUNCTION();

    if (type == BUFF_ALL || type == BUFF_PREMIUM_SPECIAL) {
        const int skillId = F2PConfig::GetPremiumUserBuffSkillId(this);
        if (skillId == 0 || g_nDualBillingSystemLevel <= m_data->m_nLevel) {
            {
                L2_GUARD(m_InfectedSkillsSect);
                for (int index = 0; index < static_cast<int>(m_InfectedSkills.size()); ++index) {
                    const CSkillInfo* const skillInfo =
                        runtime::CheckedVectorAt(m_InfectedSkills, static_cast<std::size_t>(index)).pSkillInfo;
                    if (skillInfo->m_nSkillId == skillId) {
                        _DeleteAbnormalStatus(index, this, skillInfo, true, SPET_DISPEL, true);
                    }
                }
            }
            ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
        } else {
            const CSkillInfo* const skillInfo = g_SkillDB.GetSkillInfo(CSkillKey(skillId, 1, 0), nullptr);
            if (skillInfo != nullptr) {
                skillInfo->ActivateSkill(this, this, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
            }
        }
    }
    return true;
}

void User::ConfirmCaptchaAnswerFail() {
    // Current L2Server 0x004d0774, original owner br_user.cpp.
    L2_GUARD_FUNCTION();

    if (m_CaptchaConfirmTimes < 0 || m_CaptchaConfirmTimes > 3) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][Captcha] Invalid Confirm Times. "
        //     L"AccountId[%d], Times[%d]",
        //     L"br_user.cpp",
        //     0x516,
        //     GetAccountID(),
        //     m_CaptchaConfirmTimes)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][Captcha] Invalid Confirm Times. "
        //     L"AccountId[%d], Times[%d]",
        //     L"br_user.cpp",
        //     0x516,
        //     GetAccountID(),
        //     m_CaptchaConfirmTimes)
        L2_LOG_ERROR(
            Log,
            "Cannot process failed CAPTCHA confirmation for account %u: attempt count %d is outside 0 through 3; "
            "retry/penalty handling is skipped.",
            static_cast<unsigned>(GetAccountID()), m_CaptchaConfirmTimes
        );
        return;
    }

    if (m_CaptchaConfirmTimes < 3) {
        GetSocket()->Send(
            "chccdcd", 0x62, SystemMessageId::INVALID_VERIFICATION_NUMBER_WITH_FAILURE_LIMIT_AND_REMAINING_ATTEMPTS, 2, 1, 3,
            1, 3 - m_CaptchaConfirmTimes
        );
        m_nCaptchaConfirmTransactionId = 0;
        g_CaptchaManager.RequestCaptchaImage(GetAccountID());
    } else {
        GetSocket()->Send("chccdh", 0x62, SystemMessageId::ILLEGAL_PROGRAM_VERIFICATION_FAILURE_PENALTY, 1, 4, 0xd6f7, 1);
        ActivateCaptchaDebuff();
        EndCaptchaConfirm();
        GetSocket()->Send("chc", 0xfe, 0x18c, 0);
    }

    const int confirmSeconds = GetCaptchaConfirmSec();
    const int delaySeconds = GetCaptchaDelaySec();
    db.RequestSetBotReport(this, m_nBotReportPoint, m_CaptchaConfirmTimes, confirmSeconds, delaySeconds);
}

BrBuyProductResultType User::CheckBuyProduct(
    CSocket* const socket, const int productId, const int productAmount, const bool gift
) {
    // Current L2Server 0x004d14b8, original owner br_user.cpp.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    if (IsNowTrade()) {
        return BR_BUY_INVALID_USER_STATE;
    }

    BrBuyProductResultType result = BR_BUY_SUCCESS;
    {
        L2_GUARD(*m_lock);
        if (GetPrivateStoreType() != 0 || m_data->m_bGMInvisible) {
            result = BR_BUY_INVALID_USER_STATE;
        } else if (static_cast<std::uint32_t>(productAmount) - 1U > 99U) {
            result = BR_BUY_INVALID_AMOUNT;
        }
    }
    if (result != BR_BUY_SUCCESS) {
        return result;
    }

    ItemAmountDataStruct addableItems;
    CProductSP product = g_ShopServer.GetProduct(productId);
    if (!product) {
        return BR_BUY_INVALID_PRODUCT;
    }

    result = product->CheckBuyableCondition(productAmount);
    if (result != BR_BUY_SUCCESS) {
        return result;
    }
    if (product->m_price < 0) {
        return BR_BUY_CLOSED_PRODUCT;
    }

    L2_READ_GUARD(m_inv);

    const int totalPrice = runtime::MultiplyInt32Wrapping(product->m_price, productAmount);
    if (product->m_paymentType == 0) {
        if (m_GamePoint < static_cast<std::int64_t>(totalPrice)) {
            return BR_BUY_LACK_OF_POINT;
        }
    } else if (product->m_paymentType == 1) {
        if (gift) {
            return BR_BUY_INVALID_USER;
        }
        if (m_inv.GetAdenaAmount() < static_cast<std::int64_t>(totalPrice)) {
            return BR_BUY_LACK_OF_ADENA;
        }
    } else if (product->m_paymentType == 2) {
        if (gift) {
            return BR_BUY_INVALID_USER;
        }
        if (m_inv.GetItemAmount(0x5cfd, 0, true) < static_cast<std::int64_t>(totalPrice)) {
            return BR_BUY_LACK_OF_EVENTCOIN;
        }
    } else {
        return BR_BUY_INVALID_PRODUCT;
    }

    int generatedItemCount = 0;
    for (int index = 0; index < product->GetItemCount(); ++index) {
        const CProduct::ItemData* const itemData = product->GetItemData(index);
        if (itemData == nullptr) {
            return BR_BUY_INVALID_PRODUCT;
        }

        CObject* const object = g_ObjectDB.GetObjectW(itemData->typeId, false);
        if (object == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"itemclassid %d is wrong!!!",
            //     itemData->typeId)
            // L2_LOG_ERROR(Log,
            //     L"itemclassid %d is wrong!!!",
            //     itemData->typeId)
            L2_LOG_ERROR(
                Log,
                "Product %d references missing item prototype %d at slot %d; purchase validation returns invalid product.",
                productId, itemData->typeId, index
            );
            return BR_BUY_INVALID_PRODUCT;
        }
        CItem* const item = static_cast<CItem*>(object);
        if (gift && item->m_const->m_period > 0) {
            return BR_BUY_PERIODIC_ITEM;
        }

        const int totalItemAmount = runtime::MultiplyInt32Wrapping(itemData->amount, productAmount);
        if (!CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
            for (int unit = 0; unit < totalItemAmount; ++unit) {
                if (generatedItemCount > 199) {
                    return BR_BUY_INVALID_PRODUCT;
                }
                addableItems.Push(itemData->typeId, 1);
                ++generatedItemCount;
            }
        } else {
            addableItems.Push(itemData->typeId, static_cast<std::int64_t>(totalItemAmount));
            ++generatedItemCount;
        }
    }

    if (!gift) {
        if (!m_inv.CheckAddableMulti(addableItems, false)) {
            return BR_BUY_INVENTROY_OVERFLOW;
        }
    } else if (generatedItemCount > 8) {
        return BR_BUY_MAX_ITEM_IN_POST;
    }

    for (int index = 0; index < product->GetItemCount(); ++index) {
        const CProduct::ItemData* const itemData = product->GetItemData(index);
        CObject* const object = g_ObjectDB.GetObjectW(itemData->typeId, false);
        if (object == nullptr || !object->IsItem()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"invalid item(classid : %d) at [%S], [%d]",
            //     itemData->typeId,
            //     "br_user.cpp",
            //     0x27c)
            // L2_LOG_ERROR(Log,
            //     L"invalid item(classid : %d) at [%S], [%d]",
            //     itemData->typeId,
            //     "br_user.cpp",
            //     0x27c)
            L2_LOG_ERROR(
                Log,
                "Product %d references missing or non-item prototype %d at slot %d; purchase validation returns invalid "
                "item.",
                productId, itemData->typeId, index
            );
            return BR_BUY_INVALID_ITEM;
        }

        CItem* const item = static_cast<CItem*>(object);
        const int totalItemAmount = runtime::MultiplyInt32Wrapping(itemData->amount, productAmount);
        const std::uint64_t amountMinusOne = static_cast<std::uint64_t>(static_cast<std::int64_t>(totalItemAmount) - 1);

        bool validAmount = false;
        if (static_cast<int>(item->m_data->m_ItemConsumeType) == 3) {
            validAmount = amountMinusOne <= UINT64_C(0xe8d4a50ffe);
        } else if (amountMinusOne <= UINT64_C(999999)) {
            std::array<char, 0x20> amountText{};
            BrUserI64ToAscii(static_cast<std::int64_t>(totalItemAmount), amountText.data(), 0x0b);
            validAmount = std::strlen(amountText.data()) <= 8;
        }

        if (!validAmount) {
            SendSystemMessage(SystemMessageId::INPUT_QUANTITY_LIMIT_EXCEEDED);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] invalid amount",
            //     kBrUserSourceFile,
            //     0x289)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] invalid amount",
            //     kBrUserSourceFile,
            //     0x289)
            L2_LOG_WARN(
                Log,
                "Product %d item prototype %d has invalid total quantity %d for purchase count %d; the quantity warning was "
                "requested and validation returns invalid item.",
                productId, itemData->typeId, totalItemAmount, productAmount
            );
            return BR_BUY_INVALID_ITEM;
        }
    }

    if (product->m_price < 0 || static_cast<std::uint32_t>(product->m_price) > UINT32_C(0x7fffffff)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid price at file[%s], line[%d]",
        //     kBrUserSourceFile,
        //     0x28e)
        // L2_LOG_ERROR(Log,
        //     L"invalid price at file[%s], line[%d]",
        //     kBrUserSourceFile,
        //     0x28e)
        L2_LOG_ERROR(
            Log, "Product %d has invalid price %d; purchase validation returns invalid product.", productId, product->m_price
        );
        return BR_BUY_INVALID_PRODUCT;
    }

    if ((product->m_minLevel != 0 && m_data->m_nLevel < product->m_minLevel) ||
        (product->m_maxLevel != 0 && product->m_maxLevel < m_data->m_nLevel)) {
        return BR_BUY_LEVEL_LIMIT;
    }

    if (product->GetMinBirthday() != 0 || product->GetMaxBirthday() != 0) {
        if (product->GetMinBirthday() != 0) {
            const int createDate = BrUserCreateDateToTime32(m_data->m_tCreateDate);
            if (product->GetMinBirthday() > createDate) {
                return BR_BUY_BIRTHDAY_LIMIT;
            }
        }
        if (product->GetMaxBirthday() != 0) {
            const int createDate = BrUserCreateDateToTime32(m_data->m_tCreateDate);
            if (product->GetMaxBirthday() < createDate) {
                return BR_BUY_BIRTHDAY_LIMIT;
            }
        }
    }

    return BR_BUY_SUCCESS;
}

void User::CheckDormantAndGiveGift() {
    // Current L2Server 0x004cf8d8.
    L2_GUARD_FUNCTION();
    if (GetSocket() != nullptr) {
        if (!GetSocket()->IsDormantUser()) {
            if (GetSocket()->IsNewbieUser()) {
                db.RequestGetDormantInfo(GetAccountID(), GetDBID(), 2, 1);
            }
        } else if (m_data->m_nLevel > 39 && !IsFirstOrSecondClassGroup(m_data->m_nClass)) {
            db.RequestGetDormantInfo(GetAccountID(), GetDBID(), 3, 1);
        }
        CheckDormantUser(true);
    }
}

void User::GivePremiumItem() {
    // Current L2Server 0x004d22f0.
    L2_GUARD_FUNCTION();
    if (!F2PConfig::CanTakePremiumItem(this)) {
        for (const int configuredItem : g_nPremiumItem) {
            const int itemClassId = runtime::AbsoluteInt32Wrapping(configuredItem);
            if (itemClassId > 0 && m_inv.HaveItemByClassId(itemClassId, 1, false, false)) {
                static_cast<void>(DeleteItemInInventory(itemClassId));
                SendSystemMessage(SystemMessageId::PREMIUM_ACCOUNT_EXPIRED_PREMIUM_ITEM_DELETED);
            }
        }
        return;
    }

    const double initialCarryWeight = CarryWeight();
    for (const int itemClassId : g_nPremiumItem) {
        if (itemClassId <= 0 || m_inv.HaveItemByClassId(itemClassId, 1, false, false)) {
            continue;
        }

        if (GetInventoryLimit() < runtime::AddInt32Wrapping(m_data->m_nNumInventory, 5) ||
            CarryWeight() < static_cast<double>(m_inv.GetCarryingWeight()) + initialCarryWeight * 20.0 / 100.0) {
            SendSystemMessage(SystemMessageId::CANNOT_RECEIVE_PREMIUM_ITEM_INVENTORY_WEIGHT_OR_QUANTITY_EXCEEDED);
            continue;
        }

        InventoryParams parameters;
        parameters.nItemClassID = itemClassId;
        parameters.nCount = 1;
        static_cast<void>(AddItemToInventory(parameters));
        SendSystemMessage(SystemMessageId::PREMIUM_ITEM_GRANTED_UNTIL_PREMIUM_ACCOUNT_EXPIRES);
    }
}

void User::SendDormantGift(int dormantType) {
    // Current L2Server 0x004d2530.
    L2_GUARD_FUNCTION();

    // if (dormantType == 3) {
    if (dormantType == DormantConst::DT_DORMANT) {
        if (!g_bSendGiftToDormant || m_data->m_nLevel < 40 || IsFirstOrSecondClassGroup(m_data->m_nClass)) {
            return;
        }

        CFHTML::StrStrMapType parameters;
        const std::u16string title = CFHTML::MakeHTMLFromStrMap(u"g_dormant_account_mail001.htm", parameters, 0);
        const std::u16string contents = CFHTML::MakeHTMLFromStrMap(u"g_dormant_account_mail002.htm", parameters, 0);
        db.RequestWritePostBirth(
            static_cast<std::int32_t>(GetDBID()), title.c_str(), contents.c_str(), g_DormantUserGift, 0x0b, 1, 0, 0
        );
        // } else if (dormantType == 2 &&
    } else if (dormantType == DormantConst::DT_NEWBIE && g_bSendGiftToNewbie) {
        CFHTML::StrStrMapType parameters;
        const std::u16string title = CFHTML::MakeHTMLFromStrMap(u"g_new_account_mail001.htm", parameters, 0);
        const std::u16string contents = CFHTML::MakeHTMLFromStrMap(u"g_new_account_mail002.htm", parameters, 0);
        db.RequestWritePostBirth(
            static_cast<std::int32_t>(GetDBID()), title.c_str(), contents.c_str(), g_nNewbieGiftId, 0x0c, 1, 0, 0
        );
    }
}
