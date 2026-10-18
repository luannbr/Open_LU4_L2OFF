#include "Recipe.h"
#include "CConfig.h"
#include "CreatureController.h"
#include "DB.h"
#include "GameEventSystem.h"
#include "Item.h"
#include "ObjectDB.h"
#include "Protocol/src/SystemMessageIds.h"
#include "RecipeParser.h"
#include "User.h"
#include "UserBroadcaster.h"
#include "UserSocket.h"

#include "Shared/src/Log.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace {


CItem* FirstUnequippedRecipeItem(User* user, const int itemClassId) {
    CItem* item = user->m_inv.GetFirstItemByClassID(itemClassId, false, false);
    while (item != nullptr && user->IsEquipedItem(item->m_data->index)) {
        item = user->m_inv.GetNextItemByClassID(item->m_data->index, false, false);
    }
    return item;
}

bool SendMissingRecipeItem(User* user, const int itemClassId, const int remaining) {
    user->GetSocket()->Send(
        "chccdcd", 0x62, SystemMessageId::REQUIRED_ITEM_QUANTITY_MISSING, 2, 3, itemClassId, 1, remaining
    );
    return false;
}

std::wstring RecipeAccountName(const User& user) {
    std::size_t length = 0;
    while (length < user.m_sAccountName.size() && user.m_sAccountName[length] != u'\0') {
        ++length;
    }
    return Utf16ToWide(std::u16string_view(user.m_sAccountName.data(), length));
}

} // namespace

CRecipeDB g_RecipeDB;

CRecipeInfo::CRecipeInfo() {
    m_nId = 0;
    m_nLevel = 0;
    m_nMpConsume = 0;
    m_nSuccessRate = 0;
    m_nItemId = 0;
    m_nCommonRecipe = 0;
    m_bCreateCritical = true;
    m_bUseOffering = false;
    m_nOfferingDP = 0;
    m_bAnnounce = false;
}

bool CRecipeInfo::IsEnoughItem(User* user, const int itemClassId, const int count, int& remaining) const {
    // Current L2Server 0x00a23568.
    L2_GUARD_FUNCTION();
    remaining = count;
    if (user == nullptr) {
        return false;
    }

    for (CItem* item = user->m_inv.GetFirstItemByClassID(itemClassId, false, false); item != nullptr;
        item = user->m_inv.GetNextItemByClassID(item->m_data->index, false, false)) {
        if (!user->IsEquipedItem(item->m_data->index)) {
            const std::uint32_t updated =
                static_cast<std::uint32_t>(remaining) - static_cast<std::uint32_t>(item->m_data->m_nCount);
            remaining = runtime::Int32FromBits(updated);
        }
        if (remaining < 1) {
            return true;
        }
    }
    return false;
}

RecipeProductItemStruct* CRecipeInfo::GetProduct(const bool critical) const {
    // Current L2Server 0x00a238b0.
    L2_GUARD_FUNCTION();
    if (m_vProduct.empty()) {
        return nullptr;
    }
    if (critical) {
        return m_vProduct.front();
    }

    const double roll = genrand64_real2();
    int accumulated = 0;
    for (RecipeProductItemStruct* const product : m_vProduct) {
        accumulated += product->Prob;
        if (runtime::Win64CvttDoubleToInt32(roll * 101.0) <= accumulated) {
            return product;
        }
    }
    return m_vProduct.back();
}

void CRecipeInfo::AnnounceOfferingRecipeMakeItem(User* user) {
    // Current L2Server 0x00a23b5c.
    L2_GUARD_FUNCTION();

    RecipeProductItemStruct* const product = GetProduct(false);
    if (product != nullptr && m_bAnnounce) {
        const std::u16string targetName = std::u16string(user->GetName());
        CUserBroadcaster::inst().BroadcastToAllUserEx(
            "chccScd", 0x62, 0x183c, 2, 0xc, targetName.c_str(), 3, product->ItemClassId
        );
    }
}

bool CRecipeInfo::MakeProductPacket(CPacketHelperEx<20480>& packet, const int multiplier) const {
    // Current L2Server 0x00a239d4. Selection remains the
    // ordinary product route even for a critical multiplier.
    L2_GUARD_FUNCTION();
    RecipeProductItemStruct* const product = GetProduct(false);
    if (product == nullptr) {
        return false;
    }
    CObject* const object = g_ObjectDB.GetObjectW(product->ItemClassId, false);
    if (object == nullptr) {
        return false;
    }

    CItem* const item = static_cast<CItem*>(object);
    const std::int64_t count = runtime::MultiplyInt64Wrapping(product->Count, multiplier);
    if (CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
        return packet.Assemble("dQddd", object->m_nClassId, count, item->m_data->m_ItemConsumeType, 0, 0) > 0;
    }

    for (std::int64_t ordinal = 0; ordinal < count; ++ordinal) {
        if (packet.Assemble("dQddd", object->m_nClassId, std::int64_t{1}, item->m_data->m_ItemConsumeType, 0, 0) < 1) {
            return false;
        }
    }
    return true;
}

bool CRecipeInfo::CheckMaterial(User* user) const {
    // Current L2Server 0x00a23c68.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    for (const RecipeItemStruct* const material : m_vMaterial) {
        int remaining = 0;
        if (!IsEnoughItem(
                user, material->ItemClassId, runtime::Int32FromBits(static_cast<std::uint32_t>(material->Count)),
                remaining
            )) {
            return SendMissingRecipeItem(user, material->ItemClassId, remaining);
        }
    }
    return true;
}

bool CRecipeInfo::CheckCatalyst(User* user) const {
    // Current L2Server 0x00a23db4.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    for (const RecipeItemStruct* const catalyst : m_vCatalyst) {
        int remaining = 0;
        if (!IsEnoughItem(
                user, catalyst->ItemClassId, runtime::Int32FromBits(static_cast<std::uint32_t>(catalyst->Count)),
                remaining
            )) {
            return SendMissingRecipeItem(user, catalyst->ItemClassId, remaining);
        }
    }
    return true;
}

bool CRecipeInfo::CheckNPCFee(User* user) const {
    // Current L2Server 0x00a23f00.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    for (const RecipeItemStruct* const fee : m_vNPCFee) {
        int remaining = 0;
        if (!IsEnoughItem(
                user, fee->ItemClassId, runtime::Int32FromBits(static_cast<std::uint32_t>(fee->Count)), remaining
            )) {
            return SendMissingRecipeItem(user, fee->ItemClassId, remaining);
        }
    }
    return true;
}

bool CRecipeInfo::CheckProduct(User* user) const {
    // Current L2Server 0x00a24054.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    for (const RecipeProductItemStruct* const product : m_vProduct) {
        if (!user->m_inv.CheckAddable(product->ItemClassId, product->Count, false)) {
            return false;
        }
    }
    return true;
}

bool CRecipeInfo::CheckCondition(User* user, const bool npcFee) const {
    // Current L2Server 0x00a24134.
    if (user == nullptr || user->m_bWaitRecipeMake) {
        return false;
    }
    if (user->GetController()->GetCurrentActionType() == ACT_SKILL) {
        user->SendSystemMessage(SystemMessageId::CANNOT_USE_RECIPE_BOOK_WHILE_USING_SKILL);
        return false;
    }

    const double currentMp = user->m_data->GetMP();
    if (currentMp <= static_cast<double>(m_nMpConsume) && currentMp != static_cast<double>(m_nMpConsume)) {
        user->SendSystemMessage(SystemMessageId::NOT_ENOUGH_MP);
        return false;
    }
    if (!CheckMaterial(user) || !CheckCatalyst(user)) {
        return false;
    }
    if (npcFee && !CheckNPCFee(user)) {
        return false;
    }
    return CheckProduct(user);
}

bool CRecipeInfo::CheckCondition(User* customer, User* maker) const {
    // Current L2Server 0x00a242b0.
    L2_GUARD_FUNCTION();
    if (customer == nullptr || maker == nullptr || maker->m_bWaitRecipeMake) {
        return false;
    }

    const double makerMp = maker->m_data->GetMP();
    const double requiredMp = static_cast<double>(m_nMpConsume);
    if (!(requiredMp < makerMp || requiredMp == makerMp)) {
        customer->GetSocket()->SendSystemMessage(SystemMessageId::NOT_ENOUGH_MP);
        return false;
    }
    const bool hasMaterial = CheckMaterial(customer);
    const bool hasCatalyst = CheckCatalyst(customer);
    if (!hasMaterial || !hasCatalyst) {
        return false;
    }
    return CheckProduct(customer);
}

bool CRecipeInfo::MakeMaterialPacket(CPacketHelperEx<20480>& packet, User* user) const {
    // Current L2Server 0x00a243f4.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    for (const RecipeItemStruct* const material : m_vMaterial) {
        CItem* const item = FirstUnequippedRecipeItem(user, material->ItemClassId);
        if (item == nullptr) {
            return false;
        }

        if (CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
            if (packet.Assemble(
                    "dQddd", item->m_nClassId, runtime::NegateInt64Wrapping(material->Count),
                    item->m_data->m_ItemConsumeType, item->GetDBID(), item->m_data->index
                ) < 1) {
                return false;
            }
            continue;
        }

        for (std::int64_t ordinal = 0; ordinal < material->Count; ++ordinal) {
            if (packet.Assemble(
                    "dQddd", item->m_nClassId, std::int64_t{-1}, item->m_data->m_ItemConsumeType, item->GetDBID(),
                    item->m_data->index
                ) < 1) {
                return false;
            }
        }
    }
    return true;
}

bool CRecipeInfo::MakeGiveBackPacket(CPacketHelperEx<20480>& packet, User* user) const {
    // Current L2Server 0x00a24638.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    for (const RecipeItemStruct* const material : m_vMaterial) {
        CObject* const object = g_ObjectDB.GetObjectW(material->ItemClassId, false);
        if (object == nullptr) {
            continue;
        }
        CItem* const item = static_cast<CItem*>(object);
        if (item->m_const->m_EtcitemType != 0x2c) {
            continue;
        }
        const int amount = runtime::Win64CvttDoubleToInt32(static_cast<double>(material->Count) * g_dRecipeGiveBackPer);
        if (amount != 0 &&
            packet.Assemble(
                "dQddd", object->m_nClassId, static_cast<std::int64_t>(amount), item->m_data->m_ItemConsumeType, 0, 0
            ) < 1) {
            return false;
        }
    }
    return true;
}

bool CRecipeInfo::MakeNPCFeePacket(CPacketHelperEx<20480>& packet, User* user) const {
    // Current L2Server 0x00a2478c.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    for (const RecipeItemStruct* const fee : m_vNPCFee) {
        CItem* const item = FirstUnequippedRecipeItem(user, fee->ItemClassId);
        if (item == nullptr) {
            return false;
        }
        if (CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
            if (packet.Assemble(
                    "dQddd", item->m_nClassId, runtime::NegateInt64Wrapping(fee->Count), item->m_data->m_ItemConsumeType,
                    item->GetDBID(), item->m_data->index
                ) < 1) {
                return false;
            }
        } else {
            for (std::int64_t ordinal = 0; ordinal < fee->Count; ++ordinal) {
                if (packet.Assemble(
                        "dQddd", item->m_nClassId, std::int64_t{-1}, item->m_data->m_ItemConsumeType, item->GetDBID(),
                        item->m_data->index
                    ) < 1) {
                    return false;
                }
            }
        }

        const FVector position = user->GetPos();
        const std::wstring accountName = RecipeAccountName(*user);
        // Log.Add(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,%I64d,%I64d,,,,%I64d,%I64d,,%s,%s,,,%I64d",
        //     static_cast<std::int64_t>(0x3b8),
        //     static_cast<std::int64_t>(user->GetDBID()),
        //     static_cast<std::int64_t>(user->GetAccountID()),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.x)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.y)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.z)),
        //     static_cast<std::int64_t>(user->m_data->m_Race),
        //     static_cast<std::int64_t>(user->m_data->m_Sex),
        //     static_cast<std::int64_t>(user->m_data->m_nClass),
        //     static_cast<std::int64_t>(user->m_data->m_nLevel),
        //     static_cast<std::int64_t>(fee->ItemClassId),
        //     fee->Count,
        //     user->GetName(),
        //     accountName.c_str(),
        //     static_cast<std::int64_t>(item->GetDBID()))
        Log.Add(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
            u"%I64d,%I64d,%I64d,%I64d,,,,%I64d,%I64d,,%s,%s,,,%I64d",
            static_cast<std::int64_t>(0x3b8), static_cast<std::int64_t>(user->GetDBID()),
            static_cast<std::int64_t>(user->GetAccountID()),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)),
            static_cast<std::int64_t>(user->m_data->m_Race), static_cast<std::int64_t>(user->m_data->m_Sex),
            static_cast<std::int64_t>(user->m_data->m_nClass), static_cast<std::int64_t>(user->m_data->m_nLevel),
            static_cast<std::int64_t>(fee->ItemClassId), fee->Count, user->GetName(), accountName.c_str(),
            static_cast<std::int64_t>(item->GetDBID())
        );
    }
    return true;
}

const RecipeItemStruct* CRecipeInfo::GetMeterialItem(const int itemClassId) const {
    // Current L2Server 0x00a24b2c. The original spelling is literal.
    L2_GUARD_FUNCTION();
    for (RecipeItemStruct* const material : m_vMaterial) {
        if (material->ItemClassId == itemClassId) {
            return material;
        }
    }
    return nullptr;
}

int CRecipeInfo::CalOfferingSuccessRate(User* user, std::vector<RecipeOfferingItemStruct*> offeringItems) const {
    // Current L2Server 0x00a24be8.
    L2_GUARD_FUNCTION();
    std::uint64_t offeringPrice = 0;
    for (const RecipeOfferingItemStruct* const offering : offeringItems) {
        CItem* const item = user->m_inv.GetByServerID(offering->ItemServerId);
        if (item != nullptr) {
            offeringPrice +=
                static_cast<std::uint64_t>(item->m_const->m_nDefaultPrice) * static_cast<std::uint64_t>(offering->Count);
        }
    }
    if (m_nOfferingDP < 1) {
        return 0;
    }

    const std::int64_t signedOfferingPrice = runtime::Int64FromBits(offeringPrice);
    const int rate = runtime::Win64CvttDoubleToInt32(
        (static_cast<double>(signedOfferingPrice) / static_cast<double>(m_nOfferingDP)) *
        static_cast<double>(std::max(100 - m_nSuccessRate, 0))
    );
    return std::min(rate, 100);
}

const RecipeOfferingItemStruct* CRecipeInfo::GetOfferingItem(
    User* user, std::vector<RecipeOfferingItemStruct*> offeringItems, const int itemClassId
) const {
    // Current L2Server 0x00a25004.
    L2_GUARD_FUNCTION();
    for (const RecipeOfferingItemStruct* const offering : offeringItems) {
        CItem* const item = user->m_inv.GetByServerID(offering->ItemServerId);
        if (item != nullptr && item->m_nClassId == itemClassId) {
            return offering;
        }
    }
    return nullptr;
}

bool CRecipeInfo::CheckOffering(User* user, std::vector<RecipeOfferingItemStruct*> offeringItems) const {
    // Current L2Server 0x00a24db0.
    L2_GUARD_FUNCTION();
    for (const RecipeOfferingItemStruct* const offering : offeringItems) {
        CItem* const item = user->m_inv.GetByServerID(offering->ItemServerId);
        if (item == nullptr) {
            return false;
        }
        if (CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
            std::uint64_t required = static_cast<std::uint64_t>(offering->Count);
            if (const RecipeItemStruct* const material = GetMeterialItem(item->m_nClassId)) {
                required += static_cast<std::uint64_t>(material->Count);
            }
            const std::int64_t signedRequired = runtime::Int64FromBits(required);
            if (user->m_inv.GetItemAmount(item->m_nClassId, 0, true) < signedRequired) {
                return false;
            }
        }
        if (user->IsEquipedItem(item->m_data->index) || item->IsPetCollarItem() || item->IsDuration() ||
            item->m_const->m_period > 0 || item->m_const->m_bIsPremium || item->m_const->m_nDefaultPrice == 0) {
            return false;
        }
    }
    return true;
}

bool CRecipeInfo::MakeOfferingPacket(
    CPacketHelperEx<20480>& packet, User* user, std::vector<RecipeOfferingItemStruct*> offeringItems
) const {
    // Current L2Server 0x00a251e8.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    for (const RecipeOfferingItemStruct* const offering : offeringItems) {
        CItem* const item = user->m_inv.GetByServerID(offering->ItemServerId);
        if (item == nullptr) {
            return false;
        }
        if (user->IsEquipedItem(item->m_data->index)) {
            user->SendSystemMessage(SystemMessageId::CANNOT_REGISTER_EQUIPPED_ITEM);
            return false;
        }

        std::int64_t count = runtime::NegateInt64Wrapping(offering->Count);
        if (!CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
            if (offering->Count > 1) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[Recipe_Offering]Not Stackable item count is more than 1 [%d]",
                //     runtime::Int32FromBits(static_cast<std::uint32_t>(offering->Count)))
                // L2_LOG_ERROR(Log,
                //     L"[Recipe_Offering]Not Stackable item count is more than 1 [%d]",
                //     runtime::Int32FromBits(static_cast<std::uint32_t>(offering->Count)))
                L2_LOG_ERROR(
                    Log,
                    "Cannot prepare recipe %d offering: item class %d is not stackable, but count is %lld; request "
                    "rejected.",
                    m_nId, item->m_nClassId, static_cast<long long>(offering->Count)
                );
                return false;
            }
            count = -1;
        }
        if (packet.Assemble(
                "dQddd", item->m_nClassId, count, item->m_data->m_ItemConsumeType, item->GetDBID(), item->m_data->index
            ) < 1) {
            return false;
        }
    }
    return true;
}

bool CRecipeInfo::RecipeShopMake(
    User* maker, User* customer, const std::int64_t makingFee, std::vector<RecipeOfferingItemStruct*> offeringItems
) {
    // Current L2Server 0x00a256c8.
    L2_GUARD_FUNCTION();
    if (maker == nullptr || customer == nullptr) {
        return false;
    }

    if (maker->IsNowTrade()) {
        maker->TradeCancel(false);
    }
    if (customer->IsNowTrade()) {
        customer->TradeCancel(false);
    }

    L2_GUARD(*maker->m_lock);
    if (!CheckCondition(customer, maker)) {
        return false;
    }
    if (m_bUseOffering && !CheckOffering(customer, offeringItems)) {
        customer->SendSystemMessage(SystemMessageId::ITEM_CANNOT_BE_USED_AS_OFFERING);
        return false;
    }

    std::int64_t offeredAdena = 0;
    const RecipeOfferingItemStruct* const offering = GetOfferingItem(customer, offeringItems, 0x39);
    if (offering != nullptr) {
        offeredAdena = offering->Count;
    }
    const std::int64_t requiredAdena = runtime::AddInt64Wrapping(offeredAdena, makingFee);
    if (customer->m_inv.GetAdenaAmount() < requiredAdena) {
        customer->SendSystemMessage(SystemMessageId::NOT_ENOUGH_ADENA);
        return false;
    }
    if (runtime::AddInt64Wrapping(maker->m_inv.GetAdenaAmount(), makingFee) >= 1000000000000LL) {
        customer->SendSystemMessage(SystemMessageId::ITEM_COMBINATION_FAILED);
        maker->SendSystemMessage(SystemMessageId::ITEM_COMBINATION_FAILED);
        return false;
    }

    CPacketHelperEx<20480> packet;
    if (!MakeMaterialPacket(packet, customer)) {
        return false;
    }
    if (m_bUseOffering && !MakeOfferingPacket(packet, customer, offeringItems)) {
        return false;
    }

    const bool eventLucky = customer->EventByLuckyStat(false);
    int successRate = m_nSuccessRate;
    CEvent_RecipeSuccessRate* const event =
        static_cast<CEvent_RecipeSuccessRate*>(CEventManager::inst().GetEvent(GAME_EVENT::ET_RECIPE_SUCCESS_RATE));
    if (event != nullptr && event->InEventTime()) {
        const int eventRate = event->GetRecipeSuccessRate(m_nId);
        if (eventRate != -1) {
            successRate = eventRate;
        }
    }
    if (m_bUseOffering) {
        successRate =
            runtime::AddInt32Wrapping(successRate, std::max(CalOfferingSuccessRate(customer, offeringItems), 0));
    }

    int success = 0;
    const bool succeeded = eventLucky || runtime::Win64CvttDoubleToInt32(genrand64_real2() * 100.0 + 1.0) <= successRate;
    if (succeeded) {
        success = 1;
        if (!MakeProductPacket(packet, 1)) {
            return false;
        }
        const double makerMp = maker->m_data->GetMP();
        const double requiredMp = static_cast<double>(m_nMpConsume);
        if (!(requiredMp <= makerMp)) {
            maker->SendSystemMessage(SystemMessageId::NOT_ENOUGH_MP);
            customer->SendSystemMessage(SystemMessageId::NOT_ENOUGH_MP);
            return false;
        }
        maker->m_data->SetMP(makerMp - static_cast<double>(m_nMpConsume));
    } else {
        RecipeProductItemStruct* const product = GetProduct(true);
        customer->GetSocket()->Send(
            "chccScdcQ", 0x62, SystemMessageId::CHARACTER_FAILED_TO_CRAFT_ITEM_FOR_ADENA, 3, 0xc, maker->GetName(), 3,
            product->ItemClassId, 6, makingFee
        );
        maker->GetSocket()->Send(
            "chccScdcQ", 0x62, SystemMessageId::FAILED_TO_CRAFT_ITEM_FOR_CHARACTER_FOR_ADENA, 3, 0xc, customer->GetName(), 3,
            product->ItemClassId, 6, makingFee
        );
        if (g_bRecipeGiveBack && !MakeGiveBackPacket(packet, customer)) {
            return false;
        }
    }

    const FVector position = maker->GetPos();
    const std::wstring accountName = RecipeAccountName(*maker);
    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,,,,%I64d,%I64d,,%s,%s,,,%I64d",
    //     static_cast<std::int64_t>(0x3c2),
    //     static_cast<std::int64_t>(maker->GetDBID()),
    //     static_cast<std::int64_t>(maker->GetAccountID()),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(position.x)),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(position.y)),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(position.z)),
    //     static_cast<std::int64_t>(maker->m_data->m_Race),
    //     static_cast<std::int64_t>(maker->m_data->m_Sex),
    //     static_cast<std::int64_t>(maker->m_data->m_nClass),
    //     static_cast<std::int64_t>(maker->m_data->m_nLevel),
    //     std::int64_t{0x39},
    //     makingFee,
    //     maker->GetName(),
    //     accountName.c_str(),
    //     static_cast<std::int64_t>(maker->m_inv.GetAdenaDBID()))
    Log.Add(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,,,,%I64d,%I64d,,%s,%s,,,%I64d",
        static_cast<std::int64_t>(0x3c2), static_cast<std::int64_t>(maker->GetDBID()),
        static_cast<std::int64_t>(maker->GetAccountID()),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)),
        static_cast<std::int64_t>(maker->m_data->m_Race), static_cast<std::int64_t>(maker->m_data->m_Sex),
        static_cast<std::int64_t>(maker->m_data->m_nClass), static_cast<std::int64_t>(maker->m_data->m_nLevel),
        std::int64_t{0x39}, makingFee, maker->GetName(), accountName.c_str(),
        static_cast<std::int64_t>(maker->m_inv.GetAdenaDBID())
    );
    maker->m_bWaitRecipeMake = true;
    return db.RecipeShopMakeItems(
        customer, maker, m_nId, success, makingFee, packet.GetAssembledCount(), packet.GetLength(), packet.GetPacket(),
        eventLucky
    );
}

bool CRecipeInfo::MakeProduct(User* user, std::vector<RecipeOfferingItemStruct*> offeringItems, const bool npcFee) const {
    // Current L2Server 0x00a25ef4.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    L2_GUARD(*user->m_lock);
    if (!CheckCondition(user, npcFee)) {
        return false;
    }
    if (m_bUseOffering && !CheckOffering(user, offeringItems)) {
        user->SendSystemMessage(SystemMessageId::ITEM_CANNOT_BE_USED_AS_OFFERING);
        return false;
    }

    const double currentMp = user->m_data->GetMP();
    if (currentMp < static_cast<double>(m_nMpConsume)) {
        user->SendSystemMessage(SystemMessageId::NOT_ENOUGH_MP);
        return false;
    }
    user->m_data->SetMP(currentMp - static_cast<double>(m_nMpConsume));

    CPacketHelperEx<20480> packet;
    if (!MakeMaterialPacket(packet, user)) {
        return false;
    }
    if (m_bUseOffering && !MakeOfferingPacket(packet, user, offeringItems)) {
        return false;
    }

    const bool eventLucky = user->EventByLuckyStat(false);
    int successRate = m_nSuccessRate;
    CEvent_RecipeSuccessRate* const event =
        static_cast<CEvent_RecipeSuccessRate*>(CEventManager::inst().GetEvent(GAME_EVENT::ET_RECIPE_SUCCESS_RATE));
    if (event != nullptr && event->InEventTime()) {
        const int eventRate = event->GetRecipeSuccessRate(m_nId);
        if (eventRate != -1) {
            successRate = eventRate;
        }
    }
    if (m_bUseOffering) {
        successRate += std::max(CalOfferingSuccessRate(user, offeringItems), 0);
    }

    int success = 0;
    int critical = 0;
    bool succeeded = eventLucky;
    if (!succeeded) {
        const int roll = runtime::Win64CvttDoubleToInt32(genrand64_real2() * 100.0 + 1.0);
        succeeded = successRate >= roll;
    }
    if (succeeded) {
        success = 1;
        int multiplier = 1;
        if (m_bCreateCritical && user->m_SkillMod.m_dCreateItemCriticalProb >= genrand64_real1() * 100.0) {
            multiplier = 2;
            critical = 1;
        }
        if (!MakeProductPacket(packet, multiplier)) {
            return false;
        }
    } else {
        user->SendSystemMessage(SystemMessageId::ITEM_COMBINATION_FAILED);
        if (g_bRecipeGiveBack && !MakeGiveBackPacket(packet, user)) {
            return false;
        }
    }

    if (npcFee && !MakeNPCFeePacket(packet, user)) {
        return false;
    }
    user->m_bWaitRecipeMake = true;
    db.RecipeMakeItems(
        user, m_nId, success, critical, packet.GetAssembledCount(), packet.GetLength(), packet.GetPacket(), eventLucky
    );
    return true;
}

CRecipeDB::~CRecipeDB() = default;

CRecipeInfo* CRecipeDB::find(int recipeId) {
    const auto position = m_mapRecipeInfo.find(recipeId);
    return position == m_mapRecipeInfo.end() ? nullptr : position->second;
}

void CRecipeDB::Add(CRecipeInfo* recipeInfo) { m_mapRecipeInfo.emplace(recipeInfo->m_nId, recipeInfo); }

void CRecipeDB::Load() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kRecipePath = L"..\\Script\\recipe.txt";
#else
    constexpr const wchar_t* kRecipePath = L"../script/recipe.txt";
#endif

    CSecureStream input(kRecipePath, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from recipe file failed",
        //     L"Recipe.cpp",
        //     0x48b)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from recipe file failed",
        //     L"Recipe.cpp",
        //     0x48b)
        L2_LOG_ERROR(Log, "Cannot read recipe.txt; recipe loading skipped and existing recipes retained.");
        return;
    }

    CParserForRecipe parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] parser.create failed",
        //     L"Recipe.cpp",
        //     0x494)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] parser.create failed",
        //     L"Recipe.cpp",
        //     0x494)
        L2_LOG_ERROR(Log, "Cannot initialize the recipe.txt parser; recipe loading skipped and existing recipes retained.");
        return;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing recipe file failed after line[%d]",
        //     L"Recipe.cpp",
        //     0x4a1,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing recipe file failed after line[%d]",
        //     L"Recipe.cpp",
        //     0x4a1,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "Parsing recipe.txt stopped at line %lld; previously registered recipes are retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
}
