#include "BuySell.h"
#include "BuySellListManager.h"
#include "CreatureController.h"
#include "CursedWeapon.h"
#include "DB.h"
#include "Fishing.h"
#include "Item.h"
#include "ItemAssembler.h"
#include "ObjectDB.h"
#include "Protocol/src/SystemMessageIds.h"
#include "QuestData.h"
#include "QuestManager.h"
#include "Shared/src/TextRuntime.h"
#include "User.h"
#include "UserSocket.h"

#include "Portable/RuntimeTimingTrace.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/SlotType.h"
#include "Shared/src/Utils.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace {

constexpr std::int32_t BaseCrystal[] = {0, 1, 2, 3, 4, 5, 5, 5, 8, 8, 8, 11, 12};


bool IsValidNpcShopAmount(const CItem& item, const std::int64_t amount) noexcept {
    const std::uint64_t amountMinusOne = static_cast<std::uint64_t>(amount) - 1U;
    if (item.m_data->m_ItemConsumeType == ICT_ASSET) {
        return amountMinusOne <= UINT64_C(999999999998);
    }
    return amountMinusOne < UINT64_C(1000000);
}

int AssembleForBuyItem(
    CItem& item, const std::int64_t amount, const std::int64_t totalPrice, CPacketHelperEx<20480>& packet
) {
    // Current L2Server 0x00b9d71c, original anonymous user_packet.cpp owner.
    L2_GUARD_FUNCTION();
    BaseAttribute attribute;
    CItemAssemblerBuyItem assembler(item, amount, totalPrice, attribute, 0x7f);
    return assembler.AssembleItemInfo(packet);
}

const wchar_t* AccountNameForLog(const std::array<char16_t, 15>& accountName, std::array<wchar_t, 16>& storage) noexcept {
#if defined(_WIN32)
    static_cast<void>(storage);
    return reinterpret_cast<const wchar_t*>(accountName.data());
#else
    storage.fill(wchar_t{});
    std::size_t index = 0;
    while (index < accountName.size() && accountName[index] != u'\0') {
        storage[index] = static_cast<wchar_t>(accountName[index]);
        ++index;
    }
    return storage.data();
#endif
}

} // namespace

namespace UserHelper {

bool CheckSameStackableItem(const ItemAmountDataStruct& items);

} // namespace UserHelper

bool User::OnAttackPacket(const std::uint32_t targetId, const char forceAttack) {
    // Current L2Server.exe 0x00b9b7c0.
    L2_GUARD_FUNCTION();

    if (targetId == UINT32_MAX || targetId == 0) {
        static std::int32_t count = 0;
        const bool shouldLog = count < 30;
        count = runtime::AddInt32Wrapping(count, 1);
        if (shouldLog) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"attack packet target = %x",
            //     targetId)
            // Log.Add(
            //     LOG_WARN,
            //     "[attack] Request ignored for character runtime ID %u: target ID 0x%08x is an invalid sentinel. An
            //     action-failed reply will be requested.", m_nId, targetId)
            L2_LOG_WARN(
                Log,
                "[attack] Request ignored for character runtime ID %u: target ID 0x%08x is an invalid sentinel; an "
                "action-failed reply will be requested.",
                m_nId, targetId
            );
        }
        // ActionFailed(0)
        ActionFailed(AF_DEFAULT);
        return false;
    }

    CObject* const target = CObject::GetObjectW(targetId);
    if (target == nullptr) {
        // ActionFailed(0)
        ActionFailed(AF_DEFAULT);
        return false;
    }

    if (runtime::runtime_timing::Matches(this, target)) {
        CObject* const selected = m_spTarget.GetObjectW();
        runtime::runtime_timing::Record(
            "user_attack_ingress", this, target,
            "force_byte=%d selected_sid=%u current_action=%d "
            "next_action=%d actor_hp=%.6f target_hp=%.6f",
            static_cast<int>(forceAttack), selected != nullptr ? selected->Id() : 0U,
            static_cast<int>(GetController()->GetCurrentActionType()),
            static_cast<int>(GetController()->GetNextActionType()), m_data->m_dHP,
            target->IsCreature() ? target->CastCreature()->m_data->m_dHP : -1.0
        );
    }

    SetAvoidAgroFalse();
    Action(target, true, forceAttack != 0);
    return false;
}

bool User::OnDestroyQuest(const int questId) {
    // Current L2Server.exe 0x00b9d270.
    L2_GUARD_FUNCTION();
    bool result = false;

    UserQuest* const quest = QuestManager::inst().GetQuest(questId);
    if (quest != nullptr) {
        {
            L2_GUARD(m_questListLock);
            const auto position = m_acceptedQuest.find(questId);
            if (position != m_acceptedQuest.end()) {
                m_acceptedQuest.erase(position);
                quest->OnCanceled(*this);
                result = true;
            }
        }
    }

    if (g_QuestItemDB.DestroyQuestItem(this, questId, false)) {
        if (m_data->RemoveMemo(questId)) {
            db.SaveQuestInfo(this);
            result = true;
            CLogFormatter formatter;
            formatter.WriteLog2(0x133, this, nullptr, questId, 0, 0, 0, 0, 0, 0);
        }
        SendQuestList(nullptr);
    }

    if (!result) {
        SendSystemMessage(SystemMessageId::QUEST_CANNOT_BE_DELETED);
    }
    return result;
}

bool User::OnSellItemsPacket(
    const int itemCount,
    const int adenaDatabaseId,
    const std::int64_t adenaAmount,
    const BuySellResult result,
    const std::uint8_t* packet
) {
    // Current L2Server 0x00b9cca4, original owner user_packet.cpp.
    L2_GUARD_FUNCTION();
    CSPointer<CBuySell> buySell = GetBuySell();
    char refresh = 1;

    switch (result) {
        case BSR_USER_CANCEL:
        case BSR_LACK_OF_MONEY:
            refresh = 2;
            break;
        case BSR_INVALID_ITEMS:
            refresh = 3;
            break;
        case BSR_SUCCESS: {
            refresh = 0;
            {
                L2_GUARD(*m_lock);
                L2_WRITE_GUARD(m_inv);
                const unsigned char* cursor = packet;
                for (int index = 0; index < itemCount; ++index) {
                    int itemDatabaseId = 0;
                    std::int64_t soldAmount = 0;
                    std::int64_t remainingAmount = 0;
                    std::int64_t totalPrice = 0;
                    cursor = ::Disassemble(cursor, "dQQQ", &itemDatabaseId, &soldAmount, &remainingAmount, &totalPrice);

                    CItem* const item = m_inv.GetByDBID(itemDatabaseId);
                    if (item == nullptr) {
                        continue;
                    }
                    if (::g_nCountryCode != 1) {
                        _ItemRefundPush(item, soldAmount, totalPrice);
                    }
                    if (remainingAmount == 0) {
                        m_inv.Pop(item->m_data->index, false);
                        m_inv.SetInventoryChanged(item, IUT_DELETE);
                        item->Delete();
                        continue;
                    }

                    {
                        L2_GUARD(*item->m_lock);
                        if (!item->SetAmount(remainingAmount)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                            //     "user_packet.cpp",
                            //     0x339,
                            //     static_cast<long long>(item->m_data->m_nCount),
                            //     static_cast<long long>(remainingAmount))
                            // Log.Add(
                            //     LOG_ERROR,
                            //     "[shop sell] Sale reply could not update item DB ID %d for character runtime ID %u:
                            //     requested remaining amount %lld was rejected (observed count %lld). Inventory-update
                            //     processing continues.", itemDatabaseId, m_nId, static_cast<long long>(remainingAmount),
                            //     static_cast<long long>(item->m_data->m_nCount))
                            L2_LOG_ERROR(
                                Log,
                                "[shop sell] Sale reply could not update item DB ID %d for character runtime ID %u: "
                                "requested remaining amount %lld was rejected (observed count %lld); inventory-update "
                                "processing continues.",
                                itemDatabaseId, m_nId, static_cast<long long>(remainingAmount),
                                static_cast<long long>(item->m_data->m_nCount)
                            );
                        }
                    }
                    m_inv.SetInventoryChanged(item, IUT_UPDATE);
                }

                if (adenaAmount != 0) {
                    CItem* adena = m_inv.GetByDBID(adenaDatabaseId);
                    if (adena == nullptr) {
                        adena = g_ObjectDB.CreateItem(0x39);
                        adena->SetDBID(static_cast<unsigned int>(adenaDatabaseId));
                        if (!m_inv.Push(adena->m_data->index)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     "inven push failed at file[%s], line[%d]",
                            //     "user_packet.cpp",
                            //     0x357)
                            // Log.Add(
                            //     LOG_ERROR,
                            //     "[shop sell] Sale reply could not insert the new adena item (DB ID %d) into the inventory
                            //     of character runtime ID %u. The handler continues marking it added and applying the
                            //     returned amount.", adenaDatabaseId, m_nId)
                            L2_LOG_ERROR(
                                Log,
                                "[shop sell] Sale reply could not insert the new adena item (DB ID %d) into the inventory "
                                "of character runtime ID %u; the handler continues marking it added and applying the "
                                "returned amount.",
                                adenaDatabaseId, m_nId
                            );
                        }
                        m_inv.SetInventoryChanged(adena, IUT_ADD);
                    }
                    if (!adena->SetAmount(adenaAmount)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                        //     "user_packet.cpp",
                        //     0x35f,
                        //     static_cast<long long>(adena->m_data->m_nCount),
                        //     static_cast<long long>(adenaAmount))
                        // Log.Add(
                        //     LOG_ERROR,
                        //     "[shop sell] Sale reply could not set adena amount %lld for item DB ID %d, character runtime
                        //     ID %u (observed count %lld), reply processing continues.", static_cast<long
                        //     long>(adenaAmount), adenaDatabaseId, m_nId, static_cast<long long>(adena->m_data->m_nCount))
                        L2_LOG_ERROR(
                            Log,
                            "[shop sell] Sale reply could not set adena amount %lld for item DB ID %d, character runtime ID "
                            "%u (observed count %lld); reply processing continues.",
                            static_cast<long long>(adenaAmount), adenaDatabaseId, m_nId,
                            static_cast<long long>(adena->m_data->m_nCount)
                        );
                    }
                }
            }
            break;
        }
        default:
            break;
    }

    if (buySell) {
        ShowBuySellList(refresh);
    }
    return false;
}

bool User::OnSellPacket(const std::uint8_t* packet) {
    // Current L2Server 0x00b9c1e8, original owner user_packet.cpp.
    L2_GUARD_FUNCTION();
    CSPointer<CBuySell> buySell = GetBuySell();
    if (!CanUseNpcMerchant(buySell)) {
        return false;
    }

    CUserSocket* const socket = GetSocket();
    int buySellToken = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &buySellToken);
    const int expectedToken = static_cast<int>(reinterpret_cast<std::intptr_t>(buySell.get()) / 72);
    if (buySellToken != expectedToken) {
        // ActionFailed(0)
        ActionFailed(AF_DEFAULT);
        return false;
    }

    const BuySellListCache::Key banListKey{
        buySell->m_nResidenceID, static_cast<int>(GetInZoneID()), buySell->m_nItemNpcBuyBanListID};
    const BuySellListCache::List* const banList = BuySellListCache::Inst().GetList(banListKey);

    int requestedItemCount = 0;
    cursor = ::Disassemble(cursor, "d", &requestedItemCount);
    if (static_cast<unsigned int>(requestedItemCount - 1) >= 200U || banList == nullptr) {
        {
            L2_GUARD(*m_lock);
            ReleaseEconomy();
        }
        // ActionFailed(0)
        ActionFailed(AF_DEFAULT);
        return false;
    }

    std::array<char, 0x2000> request;
    int requestLength = 0;
    std::int64_t totalPrice = 0;
    std::int64_t totalTax = 0;
    for (int index = 0; index < requestedItemCount; ++index) {
        int itemServerId = 0;
        int itemClassId = 0;
        std::int64_t amount = 0;
        cursor = ::Disassemble(cursor, "ddQ", &itemServerId, &itemClassId, &amount);
        if (CItemRestrict::Inst().CheckItemRestrict(itemClassId)) {
            return false;
        }

        CItem* const item = m_inv.GetByServerID(itemServerId);
        if (item == nullptr) {
            // ActionFailed(0)
            ActionFailed(AF_DEFAULT);
            return false;
        }
        if (item->m_nClassId != itemClassId) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"class id don't match!!!!!! hack?      disconnect this user")
            // Log.Add(
            //     LOG_WARN,
            //     "[shop sell] Request rejected for character runtime ID %u: item runtime ID %u has observed class %d, but
            //     the request specifies class %d. Requesting socket closure.", m_nId,
            //     static_cast<std::uint32_t>(itemServerId), item->m_nClassId, itemClassId)
            L2_LOG_WARN(
                Log,
                "[shop sell] Request rejected for character runtime ID %u: item runtime ID %u has observed class %d, but "
                "the request specifies class %d; requesting socket closure.",
                m_nId, static_cast<std::uint32_t>(itemServerId), item->m_nClassId, itemClassId
            );
            LogAbnormalAction(AUA_TRY_ITEM_NOT_ITEM, true, 0, 0, 0, 0);
            return true;
        }
        if (!IsValidNpcShopAmount(*item, amount)) {
            socket->SendSystemMessage(SystemMessageId::INPUT_QUANTITY_LIMIT_EXCEEDED);
            // Log.Add(
            //     LOG_ERROR,
            //     L"invalid amount at file[%s], line[%d] "
            //     L"ItemConsumeType[%d]/amount[%I64d]",
            //     L"user_packet.cpp",
            //     0x72,
            //     static_cast<int>(item->m_data->m_ItemConsumeType),
            //     static_cast<long long>(amount))
            // Log.Add(
            //     LOG_WARN,
            //     "[shop sell] Request rejected for character runtime ID %u, item class %d: amount %lld failed the quantity
            //     limit for consume type %d, no database sale was requested.", m_nId, itemClassId, static_cast<long
            //     long>(amount), static_cast<int>(item->m_data->m_ItemConsumeType))
            L2_LOG_WARN(
                Log,
                "[shop sell] Request rejected for character runtime ID %u, item class %d: amount %lld failed the quantity "
                "limit for consume type %d; no database sale was requested.",
                m_nId, itemClassId, static_cast<long long>(amount), static_cast<int>(item->m_data->m_ItemConsumeType)
            );
            // ActionFailed(0)
            ActionFailed(AF_DEFAULT);
            return false;
        }
        if (item->m_data->m_nCount < amount) {
            SendSystemMessage(SystemMessageId::SELL_QUANTITY_EXCEEDS_OWNED_AMOUNT);
            return false;
        }
        if (!item->IsSellableToNpcStore(this, false)) {
            return false;
        }
        if (BuySellListCache::IsInBuySellBanList(item->m_nClassId, *banList)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] try hack? IsInBuySellBanList. %s [%d]",
            //     L"user_packet.cpp",
            //     0x83,
            //     GetName(),
            //     static_cast<int>(GetDBID()))
            // Log.Add(
            //     LOG_WARN,
            //     L"[shop sell] Request rejected for character %s (DB ID %u): item class %d is on the NPC shop ban list.
            //     requesting socket closure.", GetName(), GetDBID(), itemClassId)
            // L2_LOG_WARN(Log,
            //     L"[shop sell] Request rejected for character %s (DB ID %u): item class %d is on the NPC shop ban list.
            //     requesting socket closure.", GetName(), GetDBID(), itemClassId)
            L2_LOG_WARN(
                Log,
                u"[shop sell] Request rejected for character %s (DB ID %u): item class %d is on the NPC shop ban list; "
                u"requesting socket closure.",
                GetName(), GetDBID(), itemClassId
            );
            return true;
        }

        CObject* const templateObject = g_ObjectDB.GetObjectW(itemClassId, false);
        if (templateObject == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"OnSellPacket Undefined item(%d)",
            //     itemClassId)
            // Log.Add(
            //     LOG_ERROR,
            //     "[shop sell] Request stopped for character runtime ID %u: owned item class %d has no object definition. No
            //     database sale was requested.", m_nId, itemClassId)
            L2_LOG_ERROR(
                Log,
                "[shop sell] Request stopped for character runtime ID %u: owned item class %d has no object definition; no "
                "database sale was requested.",
                m_nId, itemClassId
            );
            // ActionFailed(0)
            ActionFailed(AF_DEFAULT);
            return false;
        }
        CItem* const templateItem = static_cast<CItem*>(templateObject);
        if (!CSharedItemData::IsStackable(templateItem->m_data->m_ItemConsumeType) && amount != 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"OnSellPacket Wrong Item Count(%d, %I64d)",
            //     itemClassId,
            //     static_cast<long long>(amount))
            // Log.Add(
            //     LOG_WARN,
            //     "[shop sell] Request rejected for character runtime ID %u: non-stackable item class %d requires amount 1,
            //     received %lld, no database sale was requested.", m_nId, itemClassId, static_cast<long long>(amount))
            L2_LOG_WARN(
                Log,
                "[shop sell] Request rejected for character runtime ID %u: non-stackable item class %d requires amount 1, "
                "received %lld; no database sale was requested.",
                m_nId, itemClassId, static_cast<long long>(amount)
            );
            // ActionFailed(0)
            ActionFailed(AF_DEFAULT);
            return false;
        }

        const int crystal = BaseCrystal[static_cast<int>(item->m_const->GetCrystalType())];
        const double bonus = crystal == CRT_MAX ? 1.0 : m_SkillMod.m_dTradeBonusPer[crystal];
        const int taxRate = buySell->GetBuyTaxRate();
        const std::int64_t defaultPrice = templateItem->m_const->m_nDefaultPrice;
        const std::int64_t unitPrice = runtime::Win64CvttDoubleToInt64(
            ((buySell->m_dRatio + 100.0) - static_cast<double>(taxRate)) * 0.01 * static_cast<double>(defaultPrice) * bonus
        );
        const std::int64_t itemPrice = runtime::MultiplyInt64Wrapping(unitPrice, amount);
        totalPrice = runtime::AddInt64Wrapping(totalPrice, itemPrice);

        const std::int64_t unitTax =
            runtime::Win64CvttDoubleToInt64((static_cast<double>(defaultPrice) * static_cast<double>(taxRate)) / 100.0);
        totalTax = runtime::AddInt64Wrapping(totalTax, runtime::MultiplyInt64Wrapping(unitTax, amount));
        const int assembled = ::Assemble(
            request.data() + requestLength, static_cast<int>(request.size()) - requestLength, "dQQ",
            static_cast<int>(item->GetDBID()), amount, itemPrice
        );
        if (assembled > 0) {
            requestLength += assembled;
        }

        if (defaultPrice < unitPrice) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][shop] item(%d), item.nSellPrice(%d) > "
            //     L"item.nDefaultPrice(%d)",
            //     L"user_packet.cpp",
            //     0xaf,
            //     templateItem->m_nClassId,
            //     static_cast<int>(unitPrice),
            //     static_cast<int>(defaultPrice))
            // Log.Add(
            //     LOG_WARN,
            //     "[shop sell] Computed unit price %lld exceeds default price %lld for item class %d, character runtime ID
            //     %u, this check does not reject the sale.", static_cast<long long>(unitPrice), static_cast<long
            //     long>(defaultPrice), templateItem->m_nClassId, m_nId)
            L2_LOG_WARN(
                Log,
                "[shop sell] Computed unit price %lld exceeds default price %lld for item class %d, character runtime ID "
                "%u; this check does not reject the sale.",
                static_cast<long long>(unitPrice), static_cast<long long>(defaultPrice), templateItem->m_nClassId, m_nId
            );
        }
    }

    const std::int64_t resultingAdena = runtime::AddInt64Wrapping(m_inv.GetAdenaAmount(), totalPrice);
    if (resultingAdena < 0) {
        // Log.Add(LOG_ERROR, L"OnSellPacket over int values")
        // Log.Add(
        //     LOG_ERROR,
        //     "[shop sell] Request rejected for character runtime ID %u: the post-sale adena calculation is negative (%lld,
        //     sale total %lld), no database sale was requested.", m_nId, static_cast<long long>(resultingAdena),
        //     static_cast<long long>(totalPrice))
        L2_LOG_ERROR(
            Log,
            "[shop sell] Request rejected for character runtime ID %u: the post-sale adena calculation is negative (%lld, "
            "sale total %lld); no database sale was requested.",
            m_nId, static_cast<long long>(resultingAdena), static_cast<long long>(totalPrice)
        );
        // ActionFailed(0)
        ActionFailed(AF_DEFAULT);
        socket->SendSystemMessage(SystemMessageId::SALE_FAILED);
        return false;
    }
    if (resultingAdena >= INT64_C(1000000000000)) {
        SendBuySellFail(BuySellConst::BSFT_AMOUNT_FULL);
        // ActionFailed(0)
        ActionFailed(AF_DEFAULT);
        socket->SendSystemMessage(SystemMessageId::CANNOT_PROCEED_POSSESSION_LIMIT_EXCEEDED);
        return false;
    }

    buySell->ReportShopTax(totalTax, false);
    db.RequestSellItems(buySell->nNPCClassId, this, totalPrice, requestedItemCount, requestLength, request.data());
    return false;
}

bool User::OnRefundPacket(const std::uint8_t* packet) {
    // Current L2Server 0x00b9d490, original owner user_packet.cpp.
    L2_GUARD_FUNCTION();
    CSPointer<CBuySell> buySell = GetBuySell();
    if (!CanUseNpcMerchant(buySell)) {
        return false;
    }

    int buySellToken = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &buySellToken);
    const int expectedToken = static_cast<int>(reinterpret_cast<std::intptr_t>(buySell.get()) / 72);
    if (buySellToken != expectedToken) {
        return false;
    }

    int requestedItemCount = 0;
    cursor = ::Disassemble(cursor, "d", &requestedItemCount);
    std::vector<int> indices;
    for (int index = 0; index < requestedItemCount; ++index) {
        int refundIndex = 0;
        cursor = ::Disassemble(cursor, "d", &refundIndex);
        indices.push_back(refundIndex);
    }

    if (!ItemRefundGet(indices, buySell)) {
        ShowBuySellList(0);
    }
    return false;
}

bool User::OnBuyPacket(const std::uint8_t* packet) {
    // Current L2Server 0x00b9d848, original owner user_packet.cpp.
    L2_GUARD_FUNCTION();
    CSPointer<CBuySell> buySell = GetBuySell();
    if (!CanUseNpcMerchant(buySell)) {
        SendBuySellFail(BuySellConst::BSFT_INVALID_DEAL);
        return false;
    }

    CUserSocket* const socket = GetSocket();
    int buySellToken = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &buySellToken);
    const int expectedToken = static_cast<int>(reinterpret_cast<std::intptr_t>(buySell.get()) / 72);
    if (buySellToken != expectedToken) {
        SendBuySellFail(BuySellConst::BSFT_INVALID_DEAL);
        return false;
    }

    const BuySellListCache::Key listKey{
        buySell->m_nResidenceID, static_cast<int>(GetInZoneID()), buySell->m_nItemNpcSellListID};
    BuySellListCache::List* const list = BuySellListCache::Inst().GetList(listKey);

    int requestedItemCount = 0;
    cursor = ::Disassemble(cursor, "d", &requestedItemCount);
    if (static_cast<unsigned int>(requestedItemCount - 1) >= 200U || list == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] try hack? nBuyItemCount [%d][%d] %s [%d]",
        //     L"user_packet.cpp",
        //     0x145,
        //     requestedItemCount,
        //     requestedItemCount,
        //     GetName(),
        //     static_cast<int>(GetDBID()))
        // Log.Add(
        //     LOG_WARN,
        //     L"[shop buy] Request rejected for character %s (DB ID %u): requested item count %d must be within 1..200 and
        //     the shop list must be available (observed list: %S). Requesting socket closure.", GetName(), GetDBID(),
        //     requestedItemCount, list == nullptr ? "missing" : "available")
        // L2_LOG_WARN(Log,
        //     L"[shop buy] Request rejected for character %s (DB ID %u): requested item count %d must be within 1..200 and
        //     the shop list must be available (observed list: %S). Requesting socket closure.", GetName(), GetDBID(),
        //     requestedItemCount, list == nullptr ? "missing" : "available")
        L2_LOG_WARN(
            Log,
            u"[shop buy] Request rejected for character %s (DB ID %u): requested item count %d must be within 1..200 and "
            u"the shop list must be available (observed list: %S); requesting socket closure.",
            GetName(), GetDBID(), requestedItemCount, list == nullptr ? "missing" : "available"
        );
        return true;
    }

    ItemAmountDataStruct items;
    for (int index = 0; index < requestedItemCount; ++index) {
        int itemClassId = 0;
        std::int64_t amount = 0;
        cursor = ::Disassemble(cursor, "dQ", &itemClassId, &amount);
        if (CItemRestrict::Inst().CheckItemRestrict(itemClassId)) {
            return false;
        }
        items.Push(itemClassId, amount);
    }

    if (!UserHelper::CheckSameStackableItem(items)) {
        SendBuySellFail(BuySellConst::BSFT_NOT_STACKABLE);
        return false;
    }

    CPacketHelperEx<20480> request;
    std::int64_t totalPrice = 0;
    std::int64_t totalTax = 0;
    int outgoingItemCount = requestedItemCount;
    {
        L2_READ_GUARD(m_inv);
        if (!m_inv.CheckAddableMulti(items, false)) {
            SendBuySellFail(BuySellConst::BSFT_INVANTORY_FULL);
            return false;
        }

        for (const ItemAmountData* const requested : items.dataList) {
            CObject* const object = g_ObjectDB.GetObjectW(requested->m_nItemClassId, false);
            if (object == nullptr || !object->IsItem()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"invalid item(classid : %d) at [%S], [%d]",
                //     requested->m_nItemClassId,
                //     "user_packet.cpp",
                //     0x17d)
                // Log.Add(
                //     LOG_WARN,
                //     "[shop buy] Request rejected for character runtime ID %u, item class %d: %s. Requesting socket closure
                //     before any database purchase.", m_nId, requested->m_nItemClassId, object == nullptr ? "the object
                //     definition is missing" : "the object definition is not an item")
                L2_LOG_WARN(
                    Log,
                    "[shop buy] Request rejected for character runtime ID %u, item class %d: %s; requesting socket closure "
                    "before any database purchase.",
                    m_nId, requested->m_nItemClassId,
                    object == nullptr ? "the object definition is missing" : "the object definition is not an item"
                );
                return true;
            }

            CItem* const item = static_cast<CItem*>(object);
            const std::int64_t amount = requested->m_nItemAmount;
            if (!IsValidNpcShopAmount(*item, amount)) {
                socket->SendSystemMessage(SystemMessageId::INPUT_QUANTITY_LIMIT_EXCEEDED);
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] invalid amount",
                //     L"user_packet.cpp",
                //     0x18b)
                // Log.Add(
                //     LOG_WARN,
                //     "[shop buy] Request rejected for character runtime ID %u, item class %d: amount %lld failed the
                //     item-type quantity limit. No database purchase was requested.", m_nId, requested->m_nItemClassId,
                //     static_cast<long long>(amount))
                L2_LOG_WARN(
                    Log,
                    "[shop buy] Request rejected for character runtime ID %u, item class %d: amount %lld failed the "
                    "item-type quantity limit; no database purchase was requested.",
                    m_nId, requested->m_nItemClassId, static_cast<long long>(amount)
                );
                SendBuySellFail(BuySellConst::BSFT_OVER_MILLION);
                return false;
            }

            bool foundStock = false;
            bool stockFailure = false;
            for (BuySell& stock : *list) {
                if (stock.m_nItemClassId != requested->m_nItemClassId) {
                    continue;
                }
                if (stock.m_dStockRate == 0.0) {
                    foundStock = true;
                    break;
                }
                if (stock.m_nCurrentStock != 0) {
                    foundStock = true;
                    stockFailure = stock.m_nCurrentStock < amount;
                    break;
                }

                const std::int32_t now = runtime::crt::Time32Now();
                if (runtime::SubtractInt32Wrapping(now, stock.m_nStockTimestamp) <= 0xe10) {
                    continue;
                }

                if (stock.m_dStockRate <= 0.0 || 1.0 < stock.m_dStockRate || stock.m_dStockRate < ::genrand64_real1()) {
                    stock.m_nCurrentStock = 0;
                } else {
                    stock.m_nCurrentStock = stock.m_nStock;
                }
                stock.m_nStockTimestamp = now;
                foundStock = true;
                stockFailure = stock.m_nCurrentStock < amount;
                break;
            }
            if (!foundStock || stockFailure) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] buysell error. [%d][%I64d]",
                //     "user_packet.cpp",
                //     0x1f5,
                //     requested->m_nItemClassId,
                //     static_cast<long long>(amount))
                // Log.Add(
                //     LOG_NORMAL,
                //     "[shop buy] Purchase declined for character runtime ID %u, item class %d, amount %lld: %s. No database
                //     purchase was requested.", m_nId, requested->m_nItemClassId, static_cast<long long>(amount),
                //     stockFailure ? "available stock is below the requested amount" : "no available stock entry matched the
                //     request")
                L2_LOG_INFO(
                    Log,
                    "[shop buy] Purchase declined for character runtime ID %u, item class %d, amount %lld: %s; no database "
                    "purchase was requested.",
                    m_nId, requested->m_nItemClassId, static_cast<long long>(amount),
                    stockFailure ? "available stock is below the requested amount"
                                 : "no available stock entry matched the request"
                );
                SendBuySellFail(BuySellConst::BSFT_STOCK_EMPTY);
                SendSystemMessage(SystemMessageId::CANNOT_BUY_ITEM_OUT_OF_STOCK);
                return false;
            }

            const int crystal = BaseCrystal[static_cast<int>(item->m_const->GetCrystalType())];
            const double bonus = crystal == CRT_MAX ? 1.0 : m_SkillMod.m_dTradeBonusPer[crystal];
            const int taxRate = buySell->GetTaxRate();
            const std::int64_t defaultPrice = item->m_const->m_nDefaultPrice;

            // The current machine reads the first list row's price
            // after separately finding the requested class's stock row.
            const double listPrice = static_cast<double>(list->front().m_nPrice);
            const std::int64_t unitPrice = runtime::Win64CvttDoubleToInt64(
                (listPrice + 100.0 + static_cast<double>(taxRate)) * static_cast<double>(defaultPrice) * 0.01 * (2.0 - bonus)
            );

            const double taxDenominator = listPrice + static_cast<double>(taxRate);
            if (taxDenominator != 0.0) {
                const std::int64_t weightedDifference =
                    runtime::MultiplyInt64Wrapping(runtime::SubtractInt64Wrapping(unitPrice, defaultPrice), amount);
                totalTax = runtime::AddInt64Wrapping(
                    totalTax,
                    runtime::Win64CvttDoubleToInt64(
                        static_cast<double>(weightedDifference) * (static_cast<double>(taxRate) / taxDenominator)
                    )
                );
            }

            if (CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
                const std::int64_t itemTotal = runtime::MultiplyInt64Wrapping(unitPrice, amount);
                totalPrice = runtime::AddInt64Wrapping(totalPrice, itemTotal);
                static_cast<void>(AssembleForBuyItem(*item, amount, itemTotal, request));
            } else {
                for (std::int64_t count = 0; count < amount; ++count) {
                    totalPrice = runtime::AddInt64Wrapping(totalPrice, unitPrice);
                    static_cast<void>(AssembleForBuyItem(*item, 1, unitPrice, request));
                }
                outgoingItemCount += static_cast<int>(amount) - 1;
            }
        }

        if (totalPrice < 0 || totalPrice > INT64_C(999999999999)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"invalid price at file[%s], line[%d]",
            //     L"user_packet.cpp",
            //     0x201)
            // Log.Add(
            //     LOG_ERROR,
            //     "[shop buy] Purchase rejected for character runtime ID %u: calculated total price %lld is outside
            //     0..999999999999, no database purchase was requested.", m_nId, static_cast<long long>(totalPrice))
            L2_LOG_ERROR(
                Log,
                "[shop buy] Purchase rejected for character runtime ID %u: calculated total price %lld is outside "
                "0..999999999999; no database purchase was requested.",
                m_nId, static_cast<long long>(totalPrice)
            );
            SendBuySellFail(BuySellConst::BSFT_INVALID_PRICE);
            return false;
        }
        if (m_inv.GetAdenaAmount() < totalPrice) {
            SendBuySellFail(BuySellConst::BSFT_LACK_ADENA);
            SendSystemMessage(SystemMessageId::NOT_ENOUGH_ADENA);
            return false;
        }

        for (const ItemAmountData* const requested : items.dataList) {
            for (BuySell& stock : *list) {
                if (stock.m_nItemClassId != requested->m_nItemClassId) {
                    continue;
                }
                if (stock.m_dStockRate != 0.0) {
                    if (stock.m_nCurrentStock < requested->m_nItemAmount) {
                        SendBuySellFail(BuySellConst::BSFT_STOCK_EMPTY);
                        SendSystemMessage(SystemMessageId::CANNOT_BUY_ITEM_OUT_OF_STOCK);
                        return false;
                    }
                    stock.m_nStockTimestamp = runtime::crt::Time32Now();
                    stock.m_nCurrentStock =
                        runtime::SubtractInt64Wrapping(stock.m_nCurrentStock, requested->m_nItemAmount);
                }
                break;
            }
        }
    }

    buySell->ReportShopTax(totalTax, true);
    db.RequestBuyItems(
        buySell->nNPCClassId, this, totalPrice, static_cast<int>(totalTax), buySell->m_nResidenceID, true, outgoingItemCount,
        request.GetLength(), request.GetPacket()
    );

    const FVector position = GetPos();
    std::array<wchar_t, 16> accountNameStorage{};
    const wchar_t* const accountName = AccountNameForLog(m_sAccountName, accountNameStorage);
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
    //     L"%I64d,%I64d,%s,%s,,,",
    //     std::int64_t{0x3b4},
    //     static_cast<std::int64_t>(GetDBID()),
    //     static_cast<std::int64_t>(GetAccountID()),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(position.x)),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(position.y)),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(position.z)),
    //     static_cast<std::int64_t>(m_data->m_Race),
    //     static_cast<std::int64_t>(m_data->m_nClass),
    //     static_cast<std::int64_t>(m_data->m_Sex),
    //     static_cast<std::int64_t>(m_data->m_nLevel),
    //     static_cast<std::int64_t>(buySell->m_nResidenceID),
    //     static_cast<std::int64_t>(buySell->GetTaxRate()),
    //     totalPrice,
    //     totalTax,
    //     static_cast<std::int64_t>(outgoingItemCount),
    //     static_cast<std::int64_t>(buySell->nNPCClassId),
    //     GetName(),
    //     accountName)
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
        u"%I64d,%I64d,%s,%s,,,",
        std::int64_t{0x3b4}, static_cast<std::int64_t>(GetDBID()), static_cast<std::int64_t>(GetAccountID()),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)), static_cast<std::int64_t>(m_data->m_Race),
        static_cast<std::int64_t>(m_data->m_nClass), static_cast<std::int64_t>(m_data->m_Sex),
        static_cast<std::int64_t>(m_data->m_nLevel), static_cast<std::int64_t>(buySell->m_nResidenceID),
        static_cast<std::int64_t>(buySell->GetTaxRate()), totalPrice, totalTax, static_cast<std::int64_t>(outgoingItemCount),
        static_cast<std::int64_t>(buySell->nNPCClassId), GetName(), accountName
    );
    return false;
}

bool User::OnUnequipItemPacket(const std::int64_t slotBitMask) {
    // Current L2Server 0x00b9e830, original owner user_packet.cpp.
    L2_GUARD_FUNCTION();
    CUserSocket* const socket = GetSocket();
    if (IsFrozen(true)) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_ACT_WHILE_FROZEN);
        return false;
    }
    if (!IsItemUsable(0)) {
        return false;
    }
    if (m_pFishingController->m_FishingState != FISHINGS_NONE || m_pFishingController->m_bIsAutoFishing == true) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_EQUIP_UNEQUIP_DESTROY_OR_DROP_ITEMS_WHILE_FISHING);
        return false;
    }

    static_cast<void>(CCursedWeaponMgr::inst());
    if (m_nCursedWeaponClassId > 0 && (static_cast<std::uint64_t>(slotBitMask) & 0x4180U) != 0) {
        return false;
    }

    const ActionType action = GetController()->GetCurrentActionType();
    if (static_cast<unsigned int>(action - ACT_ATTACK) < 2U) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_CHANGE_WEAPONS_DURING_ATTACK);
        return false;
    }
    if (GetPrivateStoreType() > 0) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_USE_ITEMS_DURING_PRIVATE_STORE_OR_WORKSHOP);
        return false;
    }

    int slot = ST_UNDERWEAR;
    while (slot < ST_MAX && (ST2SBT[slot] & slotBitMask) == 0) {
        ++slot;
    }
    CItem* const item = m_inv.GetByIndex(m_data->m_nSlotIndex[slot]);
    if (item == nullptr || !item->m_const->m_bForceEquip) {
        static_cast<void>(UnequipItem(static_cast<SlotType>(slot), false));
    }
    return false;
}
