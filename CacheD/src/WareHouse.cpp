#include "Shared/src/Log.h"
#include "Shared/src/MemorySub.h"

#include "WareHouse.h"

#include "CReadFile.h"
#include "CWatchList.h"
#include "CacheSocket.h"
#include "Cached.h"
#include "ItemAssembler.h"
#include "ManorDb.h"
#include "NpcServer.h"
#include "Pet.h"
#include "Pledge.h"
#include "PledgeDb.h"
#include "User.h"
#include "UserDb.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/DelimitedText.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/DBConn.h"
#include "Shared/src/EtcMani.h"
#include "Shared/src/LogFormatter.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/Runtime/TargetFormatting.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/WarehouseValidation.h"
#include "Runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

// Semantic name for the current WareHouse.obj helper at 0x005d7490. Its
// original identifier is absent from the PDB/guard material. The machine code
// fixes this exact lin_AmountChange parameter order and type sequence.
bool ExecuteWarehouseAmountChange(
    DBConn& connection,
    const wchar_t* statement,
    const int& characterId,
    const int& itemId,
    const std::int64_t& amountChange,
    const int& zeroDelete
) {
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&itemId);
    connection.BindParameter(&amountChange);
    connection.BindParameter(&zeroDelete);
    return connection.ExecutePrepared(statement);
}

// Semantic carrier for the values copied from CUser before the shipping
// warehouse audit loops. It is local source state, not a target ABI type.
struct WareHouseAuditActor {
    std::int64_t characterId = 0;
    std::int64_t accountId = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    int race = 0;
    int gender = 0;
    int characterClass = 0;
    int level = 0;
    std::u16string characterName;
    std::u16string accountName;
};

WareHouseAuditActor MakeWareHouseAuditActor(CUser* user) {
    WareHouseAuditActor actor;
    if (!user)
        return actor;

    actor.characterId = user->GetId();
    actor.accountId = user->GetAccountID();
    actor.x = user->GetX();
    actor.y = user->GetY();
    actor.z = user->GetZ();
    actor.race = user->GetRace();
    actor.gender = user->GetGender();
    actor.characterClass = user->GetClass();
    actor.level = user->GetLevel();
    actor.characterName = std::u16string(user->GetCharName());
    actor.accountName = std::u16string(user->GetAccountName());
    return actor;
}

std::u16string MakeWareHouseVariationLogText(int intensiveItemType, int variationOption1, int variationOption2) {
    return Utf8ToUtf16(
        std::to_string(intensiveItemType) + ":" + std::to_string(variationOption1) + ":" + std::to_string(variationOption2)
    );
}

// Semantic name for current WareHouse.obj helper 0x005d7128. The original
// private identifier is absent. The current call fixes the output bind and
// fourteen lin_CreateItem parameters exactly.
bool ExecuteWarehouseCreateItem(
    DBConn& connection,
    unsigned int& createdItemId,
    const unsigned int& ownerId,
    const unsigned int& itemType,
    const long long& amount,
    const int& enchant,
    const int& eroded,
    const int& bless,
    const int& ident,
    const int& wished,
    const int& warehouse,
    const ItemOptionKey& optionKey,
    const int& intensiveItemType,
    const short& hLocked,
    const short& useCount
) {
    connection.Bind(&createdItemId);
    constexpr const wchar_t* statement = L"{Call lin_CreateItem (?,?,?,?,?,?,?,?,?,?,?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&ownerId);
    connection.BindParameter(&itemType);
    connection.BindParameter(&amount);
    connection.BindParameter(&enchant);
    connection.BindParameter(&eroded);
    connection.BindParameter(&bless);
    connection.BindParameter(&ident);
    connection.BindParameter(&wished);
    connection.BindParameter(&warehouse);
    connection.BindParameter(&optionKey.variationOption1);
    connection.BindParameter(&optionKey.variationOption2);
    connection.BindParameter(&intensiveItemType);
    connection.BindParameter(&hLocked);
    connection.BindParameter(&useCount);
    return connection.ExecutePrepared(statement) && connection.Fetch();
}

// Semantic name for current WareHouse.obj helper 0x005d7218. Its binding
// order is the same current lin_UpdateUserItem ABI used by CItem::Save.
bool ExecuteWarehouseItemWrite(
    DBConn& connection,
    const unsigned int& ownerId,
    const unsigned int& itemType,
    const long long& amount,
    const int& enchant,
    const int& eroded,
    const int& bless,
    const int& ident,
    const int& wished,
    const int& warehouse,
    const unsigned int& itemId,
    const ItemOptionKey& optionKey,
    const int& intensiveItemType,
    const int& inventorySlotIndex,
    const short& hLocked,
    const short& useCount
) {
    constexpr const wchar_t* statement = L"{Call lin_UpdateUserItem "
                                         L"(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&ownerId);
    connection.BindParameter(&itemType);
    connection.BindParameter(&amount);
    connection.BindParameter(&enchant);
    connection.BindParameter(&eroded);
    connection.BindParameter(&bless);
    connection.BindParameter(&ident);
    connection.BindParameter(&wished);
    connection.BindParameter(&warehouse);
    connection.BindParameter(&itemId);
    connection.BindParameter(&optionKey.variationOption1);
    connection.BindParameter(&optionKey.variationOption2);
    connection.BindParameter(&intensiveItemType);
    connection.BindParameter(&inventorySlotIndex);
    connection.BindParameter(&hLocked);
    connection.BindParameter(&useCount);
    return connection.ExecutePrepared(statement);
}

bool ExecuteWarehouseSeizeItem2(DBConn& connection, const int& option, const int& warehouse, const int& itemId) {
    constexpr const wchar_t* statement = L"{Call lin_SeizeItem2 (?, ?, ? )}";
    connection.Prepare(statement);
    connection.BindParameter(&option);
    connection.BindParameter(&warehouse);
    connection.BindParameter(&itemId);
    return connection.ExecutePrepared(statement);
}

bool ExecuteWarehouseSeizeItem(
    DBConn& connection, bool useSeizeItem2, const int& option, const int& warehouse, const int& itemId
) {
    const wchar_t* const statement =
        useSeizeItem2 ? L"{Call lin_SeizeItem2 (?, ?, ? )}" : L"{Call lin_SeizeItem (?, ?, ? )}";
    connection.Prepare(statement);
    connection.BindParameter(&option);
    connection.BindParameter(&warehouse);
    connection.BindParameter(&itemId);
    return connection.ExecutePrepared(statement);
}

bool ExecuteWarehouseDeleteItemRow(DBConn& connection, const int& itemId) {
    constexpr const wchar_t* statement = L"{Call lin_DeleteItem (?, NULL)}";
    connection.Prepare(statement);
    connection.BindParameter(&itemId);
    return connection.ExecutePrepared(statement);
}

void WriteWarehousePremiumCreateLog(
    unsigned int accountId, int ownerId, int itemId, std::int64_t amount, unsigned int itemType
) {
    DBConn connection(runtime::process::CurrentDatabaseId());
    const long long zero = 0;
    const long long sqlAmount = static_cast<long long>(amount);
    const int reason = 2;
    constexpr const wchar_t* statement = L"{Call lin_LogForPremiumItemCreate (?,?,?,?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&accountId);
    connection.BindParameter(&ownerId);
    connection.BindParameter(&itemId);
    connection.BindParameter(&zero);
    connection.BindParameter(&sqlAmount);
    connection.BindParameter(&reason);
    connection.BindParameter(&itemType);
    connection.ExecutePrepared(statement);
}

void WriteWarehouseDeleteItemAudit(
    const WareHouseAuditActor& actor,
    const std::u16string& baseAttribute,
    const std::u16string& variation,
    int itemType,
    std::int64_t removedAmount,
    std::int64_t remainingAmount,
    int itemId
) {
    // Current CWareHouse::DeleteItem 0x005d27e4 uses a sparse literal rather
    // than CLogFormatter's zero-filled 26-field record.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x27f);
    fields[1] = runtime::DecimalUtf16(actor.characterId);
    fields[2] = runtime::DecimalUtf16(actor.accountId);
    fields[5] = runtime::DecimalUtf16(actor.x);
    fields[6] = runtime::DecimalUtf16(actor.y);
    fields[7] = runtime::DecimalUtf16(actor.z);
    fields[9] = baseAttribute;
    fields[10] = variation;
    fields[18] = runtime::DecimalUtf16(itemType);
    fields[19] = runtime::DecimalUtf16(removedAmount);
    fields[20] = runtime::DecimalUtf16(remainingAmount);
    fields[21] = actor.characterName;
    fields[22] = actor.accountName;
    fields[25] = runtime::DecimalUtf16(itemId);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteUpgradeSystemAudits(
    CUser& user,
    CItem& resultItem,
    int sourceItemId,
    int resultEnchant,
    int resultItemType,
    const std::wstring& consumedItems
) {
    // Current CWareHouse::UpgradeSystem 0x005eaab0 emits these two route-6
    // records only after every item transaction has committed. The first
    // describes the created item. The second carries the target's compact
    // item-id/type/amount consumption trace.
    const WareHouseAuditActor actor = MakeWareHouseAuditActor(&user);
    std::array<char16_t, 512> shapeShiftAndVariationText{};
    std::array<char16_t, 256> baseAttributeText{};
    std::array<char16_t, 256> ensoulText{};
    resultItem.MakeShapeShiftAndVariationLogInfo(shapeShiftAndVariationText.data());
    BaseAttribute baseAttribute(resultItem.Attribute());
    baseAttribute.GetLogString(baseAttributeText.data());
    // resultItem.MakeEnsoulLogInfo(ensoulText.data())
    resultItem.MakeEnsoulLogInfo(ensoulText.data(), ensoulText.size());

    const CReadItemData* itemData = g_readFile.FindItem(resultItemType);
    const int itemCategory = itemData ? static_cast<int>(itemData->m_type) : -1;
    const std::u16string shapeShiftAndVariation = std::u16string(shapeShiftAndVariationText.data());
    const std::u16string baseAttributeLog = std::u16string(baseAttributeText.data());
    const std::u16string ensoulLog = std::u16string(ensoulText.data());
    const std::u16string consumedItemsLog = WideToUtf16(consumedItems.c_str());

    CLogFormatter formatter;
    formatter.WriteLog(
        0x2359, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z, shapeShiftAndVariation.c_str(),
        baseAttributeLog.c_str(), ensoulLog.c_str(), actor.race, actor.gender, actor.characterClass, actor.level,
        sourceItemId, resultEnchant, resultItemType, itemCategory, 0, 0, actor.characterName.c_str(),
        actor.accountName.c_str(), nullptr, nullptr, resultItem.ItemID()
    );
    formatter.WriteLog(
        0x235a, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z, consumedItemsLog.c_str(), nullptr,
        nullptr, actor.race, actor.gender, actor.characterClass, actor.level, 0, 0, 0, 0, 0, 0, actor.characterName.c_str(),
        actor.accountName.c_str(), nullptr, nullptr, 0
    );
}

void WriteLockedItemAudit(CUser& user, CItem& targetItem, int sourceItemId, int targetItemId, int flag) {
    // Current CWareHouse::hLockedItem 0x005e9dc4 emits record 0x235b after
    // both ordered item transactions have committed.
    const WareHouseAuditActor actor = MakeWareHouseAuditActor(&user);
    std::array<char16_t, 256> shapeShiftAndVariationText{};
    std::array<char16_t, 256> baseAttributeText{};
    std::array<char16_t, 256> ensoulText{};
    targetItem.MakeShapeShiftAndVariationLogInfo(shapeShiftAndVariationText.data());
    BaseAttribute baseAttribute(targetItem.Attribute());
    baseAttribute.GetLogString(baseAttributeText.data());
    // targetItem.MakeEnsoulLogInfo(ensoulText.data())
    targetItem.MakeEnsoulLogInfo(ensoulText.data(), ensoulText.size());

    const CReadItemData* itemData = g_readFile.FindItem(static_cast<int>(targetItem.ItemType()));
    const int itemCategory = itemData ? static_cast<int>(itemData->m_type) : -1;
    const std::u16string shapeShiftAndVariation = std::u16string(shapeShiftAndVariationText.data());
    const std::u16string baseAttributeLog = std::u16string(baseAttributeText.data());
    const std::u16string ensoulLog = std::u16string(ensoulText.data());

    CLogFormatter formatter;
    formatter.WriteLog(
        0x235b, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z, shapeShiftAndVariation.c_str(),
        baseAttributeLog.c_str(), ensoulLog.c_str(), actor.race, actor.gender, actor.characterClass, actor.level,
        targetItemId, targetItem.Enchant(), targetItem.ItemType(), itemCategory, flag, 0, actor.characterName.c_str(),
        actor.accountName.c_str(), nullptr, nullptr, sourceItemId
    );
}

void WritePremiumManipulationAudit(
    std::uint32_t ownerId, CUser* user, CItem& item, std::int64_t receivedAmount, std::int64_t warehouseNo
) {
    // Current CWareHouse::ManipulatePremiumItems 0x005e8a48 emits this exact
    // sparse route-0xc record 0x3e1 after the reply and both item saves.
    const WareHouseAuditActor actor = MakeWareHouseAuditActor(user);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x3e1);
    fields[1] = runtime::DecimalUtf16(ownerId);
    fields[2] = runtime::DecimalUtf16(actor.accountId);
    fields[5] = runtime::DecimalUtf16(actor.x);
    fields[6] = runtime::DecimalUtf16(actor.y);
    fields[7] = runtime::DecimalUtf16(actor.z);
    fields[11] = runtime::DecimalUtf16(actor.race);
    fields[12] = runtime::DecimalUtf16(actor.gender);
    fields[13] = runtime::DecimalUtf16(actor.characterClass);
    fields[14] = runtime::DecimalUtf16(actor.level);
    fields[15] = runtime::DecimalUtf16(item.ItemType());
    fields[16] = runtime::DecimalUtf16(receivedAmount);
    fields[17] = runtime::DecimalUtf16(item.Amount());
    fields[18] = runtime::DecimalUtf16(warehouseNo);
    fields[21] = actor.characterName;
    fields[22] = actor.accountName;
    fields[25] = runtime::DecimalUtf16(item.ItemID());
    runtime::process::WriteLogFormatterLine(0xc, runtime::JoinFields(fields, u','));
}

void SendPremiumManipulationFailure(CSocket* socket, int requestId) { socket->Send("chdQ", 0xc6, 0x5f, requestId, 0LL); }

WareHouseAuditActor MakeDestroyItemAuditActor(CUser* user) {
    if (user)
        return MakeWareHouseAuditActor(user);

    // Current CWareHouse::DestroyItem initializes these three absent-user
    // values before attempting the user lookup. Other warehouse audit paths
    // have different defaults, so keep the distinction local to this owner.
    WareHouseAuditActor actor;
    actor.race = -1;
    actor.gender = 2;
    actor.characterClass = -1;
    return actor;
}

void WritePetGetItemAudit(
    const WareHouseAuditActor& actor, int petId, CItem& item, std::int64_t pickedAmount, int pickedItemId
) {
    // Current CPetWarehouse::PetGetItem call 0x005f2567 emits route-6
    // record 0x6d. The requested/picked object ID remains field 25 even when
    // the operation merged into a different pet inventory object.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x6d);
    fields[1] = runtime::DecimalUtf16(actor.characterId);
    fields[2] = runtime::DecimalUtf16(actor.accountId);
    fields[5] = runtime::DecimalUtf16(actor.x);
    fields[6] = runtime::DecimalUtf16(actor.y);
    fields[7] = runtime::DecimalUtf16(actor.z);
    fields[11] = runtime::DecimalUtf16(actor.race);
    fields[12] = runtime::DecimalUtf16(actor.gender);
    fields[13] = runtime::DecimalUtf16(actor.characterClass);
    fields[14] = runtime::DecimalUtf16(actor.level);
    fields[15] = runtime::DecimalUtf16(petId);
    fields[18] = runtime::DecimalUtf16(item.ItemType());
    fields[19] = runtime::DecimalUtf16(pickedAmount);
    fields[20] = runtime::DecimalUtf16(item.Amount());
    fields[21] = actor.characterName;
    fields[22] = actor.accountName;
    fields[25] = runtime::DecimalUtf16(pickedItemId);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

int AssemblePetTransferNewItem(CPacketHelperEx<20480>& packet, int creatureServerId, CItem& item, std::int64_t amount) {
    // Current DepositToPet/WithdrawFromPet manually emit this descriptor.
    // it is not the broader ItemAssembler wire shape. Paired
    // L2Server ReplyGiveItemToPet 0x006c74cc consumes these exact fields.
    const BaseAttribute& attribute = item.Attribute();
    int assembled = packet.Assemble(
        "ddddQdhhhhhhhhd", creatureServerId, 1, static_cast<int>(item.ItemID()), static_cast<int>(item.ItemType()),
        static_cast<long long>(amount), item.Enchant(), static_cast<int>(attribute.m_nAttackType),
        static_cast<int>(attribute.m_nAttackValue), static_cast<int>(attribute.m_nDefendValue[0]),
        static_cast<int>(attribute.m_nDefendValue[1]), static_cast<int>(attribute.m_nDefendValue[2]),
        static_cast<int>(attribute.m_nDefendValue[3]), static_cast<int>(attribute.m_nDefendValue[4]),
        static_cast<int>(attribute.m_nDefendValue[5]), item.AppearanceType()
    );
    assembled += CItem::AssembleEnsoulInfo(packet, const_cast<CItem::EnsoulInfo*>(item.EnsoulSlots()));
    assembled += packet.Assemble("h", static_cast<int>(item.HLocked()));
    assembled += packet.Assemble("h", static_cast<int>(item.UseCount()));
    return assembled;
}

void WritePetTransferAudit(
    int logId,
    const WareHouseAuditActor& actor,
    int petId,
    int npcClassId,
    CItem& item,
    std::int64_t amountChange,
    std::int64_t finalAmount,
    int loggedItemId
) {
    // Current DepositToPet/WithdrawFromPet emit route-6 records 0x6f/0x70.
    // Their field 10 remains blank even though the transfer descriptor also
    // transports Ensoul state.
    std::array<char16_t, 256> shapeShiftText{};
    std::array<char16_t, 256> baseAttributeText{};
    item.MakeShapeShiftLogInfo(shapeShiftText.data());
    BaseAttribute attribute(item.Attribute());
    attribute.GetLogString(baseAttributeText.data());

    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(logId);
    fields[1] = runtime::DecimalUtf16(actor.characterId);
    fields[2] = runtime::DecimalUtf16(actor.accountId);
    fields[5] = runtime::DecimalUtf16(actor.x);
    fields[6] = runtime::DecimalUtf16(actor.y);
    fields[7] = runtime::DecimalUtf16(actor.z);
    fields[8] = std::u16string(shapeShiftText.data());
    fields[9] = std::u16string(baseAttributeText.data());
    fields[11] = runtime::DecimalUtf16(actor.race);
    fields[12] = runtime::DecimalUtf16(actor.gender);
    fields[13] = runtime::DecimalUtf16(actor.characterClass);
    fields[14] = runtime::DecimalUtf16(actor.level);
    fields[15] = runtime::DecimalUtf16(petId);
    fields[16] = runtime::DecimalUtf16(npcClassId);
    fields[17] = runtime::DecimalUtf16(item.Enchant());
    fields[18] = runtime::DecimalUtf16(item.ItemType());
    fields[19] = runtime::DecimalUtf16(amountChange);
    fields[20] = runtime::DecimalUtf16(finalAmount);
    fields[21] = actor.characterName;
    fields[22] = actor.accountName;
    fields[25] = runtime::DecimalUtf16(loggedItemId);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

// Current CWareHouse::Withdraw emits these records inline. The helper keeps
// the exact 26-field order and route while avoiding a second invented owner.
void WriteWithdrawAudit(
    const WareHouseAuditActor& actor, CItem& item, CItem& rollbackItem, int auditContext, int intensiveItemType
) {
    const ItemChangeScope scope = item.ChangeScope();
    if (scope == ItemChangeScope_Unknown)
        return;

    std::array<char16_t, 256> baseAttributeText{};
    std::array<char16_t, 256> ensoulText{};
    BaseAttribute baseAttribute(item.Attribute());
    baseAttribute.GetLogString(baseAttributeText.data());
    // item.MakeEnsoulLogInfo(ensoulText.data())
    item.MakeEnsoulLogInfo(ensoulText.data(), ensoulText.size());

    const std::u16string targetBaseAttribute = std::u16string(baseAttributeText.data());
    const std::u16string targetEnsoul = std::u16string(ensoulText.data());
    const std::int64_t amountDifference = runtime::SubtractInt64Wrapping(item.Amount(), rollbackItem.Amount());

    CLogFormatter formatter;
    if (scope == ItemChangeScope_Added) {
        formatter.WriteLog(
            0x3a1, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z, targetEnsoul.c_str(),
            targetBaseAttribute.c_str(), nullptr, actor.race, actor.gender, actor.characterClass, actor.level, auditContext,
            0, item.Enchant(), item.ItemType(), amountDifference, item.Amount(), actor.characterName.c_str(),
            actor.accountName.c_str(), nullptr, nullptr, item.ItemID()
        );
        return;
    }

    if (scope < ItemChangeScope_Increased || scope > ItemChangeScope_Fee) {
        return;
    }

    std::array<char16_t, 256> shapeShiftText{};
    std::array<wchar_t, 256> variationText{};
    item.MakeShapeShiftLogInfo(shapeShiftText.data());
    std::swprintf(
        variationText.data(), variationText.size(), L"%d:%d:%d", intensiveItemType, item.OptionKey().variationOption1,
        item.OptionKey().variationOption2
    );

    const std::int64_t auditAmount = scope == ItemChangeScope_Increased ? amountDifference : item.Amount();
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x388);
    fields[1] = runtime::DecimalUtf16(actor.characterId);
    fields[2] = runtime::DecimalUtf16(actor.accountId);
    fields[5] = runtime::DecimalUtf16(actor.x);
    fields[6] = runtime::DecimalUtf16(actor.y);
    fields[7] = runtime::DecimalUtf16(actor.z);
    fields[8] = std::u16string(shapeShiftText.data());
    fields[9] = targetBaseAttribute;
    fields[10] = WideToUtf16(variationText.data());
    fields[11] = runtime::DecimalUtf16(actor.race);
    fields[12] = runtime::DecimalUtf16(actor.gender);
    fields[13] = runtime::DecimalUtf16(actor.characterClass);
    fields[14] = runtime::DecimalUtf16(actor.level);
    fields[15] = runtime::DecimalUtf16(auditContext);
    // Field 16 is blank in the shipping AddV format.
    fields[17] = runtime::DecimalUtf16(item.Enchant());
    fields[18] = runtime::DecimalUtf16(item.ItemType());
    fields[19] = runtime::DecimalUtf16(auditAmount);
    fields[20] = runtime::DecimalUtf16(item.Amount());
    fields[21] = actor.characterName;
    fields[22] = actor.accountName;
    fields[25] = runtime::DecimalUtf16(item.ItemID());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));

    formatter.WriteLog(
        0x2341, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z, targetEnsoul.c_str(), nullptr, nullptr,
        actor.race, actor.gender, actor.characterClass, actor.level, 0, 0, 0, 0, 0, 0, actor.characterName.c_str(),
        actor.accountName.c_str(), nullptr, nullptr, item.ItemID()
    );
}

// Current CWareHouse::WithdrawItemsFromBankEx emits one route-6 record per
// requested source item before CTransaction commits it. The source object
// supplies the three item-state strings and the logged object ID. A merged
// destination object supplies the item type and enchant fields.
void WriteWithdrawItemsFromBankExAudit(
    const WareHouseAuditActor& actor,
    CItem& sourceItem,
    CItem& resultItem,
    int warehouseClassType,
    std::int64_t requestedAmount,
    std::int64_t remainingAmount
) {
    std::array<char16_t, 256> shapeShiftAndVariationText{};
    std::array<char16_t, 256> baseAttributeText{};
    std::array<char16_t, 256> ensoulText{};
    sourceItem.MakeShapeShiftAndVariationLogInfo(shapeShiftAndVariationText.data());
    BaseAttribute baseAttribute(sourceItem.Attribute());
    baseAttribute.GetLogString(baseAttributeText.data());
    // sourceItem.MakeEnsoulLogInfo(ensoulText.data())
    sourceItem.MakeEnsoulLogInfo(ensoulText.data(), ensoulText.size());

    const std::u16string shapeShiftAndVariation = std::u16string(shapeShiftAndVariationText.data());
    const std::u16string baseAttributeLog = std::u16string(baseAttributeText.data());
    const std::u16string ensoul = std::u16string(ensoulText.data());

    CLogFormatter formatter;
    formatter.WriteLog(
        0x3af, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z, shapeShiftAndVariation.c_str(),
        baseAttributeLog.c_str(), ensoul.c_str(), actor.race, actor.gender, actor.characterClass, actor.level,
        warehouseClassType, 0, resultItem.Enchant(), resultItem.ItemType(), requestedAmount, remainingAmount,
        actor.characterName.c_str(), actor.accountName.c_str(), nullptr, nullptr, sourceItem.ItemID()
    );
}

// Current CWareHouse::DepositItemsToBankEx emits this inline route-6 record
// for each non-fee source item. These local semantic names do not assert a
// separate helper in the original source. The machine function contains the
// formatter call directly.
void WriteDepositItemsToBankExAudit(
    const WareHouseAuditActor& actor,
    CItem& sourceItem,
    int warehouseClassType,
    std::int64_t requestedAmount,
    std::int64_t remainingAmount
) {
    std::array<char16_t, 256> shapeShiftAndVariationText{};
    std::array<char16_t, 256> baseAttributeText{};
    std::array<char16_t, 256> ensoulText{};
    sourceItem.MakeShapeShiftAndVariationLogInfo(shapeShiftAndVariationText.data());
    BaseAttribute baseAttribute(sourceItem.Attribute());
    baseAttribute.GetLogString(baseAttributeText.data());
    // sourceItem.MakeEnsoulLogInfo(ensoulText.data())
    sourceItem.MakeEnsoulLogInfo(ensoulText.data(), ensoulText.size());

    const std::u16string shapeShiftAndVariation = std::u16string(shapeShiftAndVariationText.data());
    const std::u16string baseAttributeLog = std::u16string(baseAttributeText.data());
    const std::u16string ensoul = std::u16string(ensoulText.data());

    CLogFormatter formatter;
    formatter.WriteLog(
        0x3b0, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z, shapeShiftAndVariation.c_str(),
        baseAttributeLog.c_str(), ensoul.c_str(), actor.race, actor.gender, actor.characterClass, actor.level,
        warehouseClassType, 0, sourceItem.Enchant(), sourceItem.ItemType(), requestedAmount, remainingAmount,
        actor.characterName.c_str(), actor.accountName.c_str(), nullptr, nullptr, sourceItem.ItemID()
    );
}

// The final adena descriptor is logged by a direct CLog::AddV route in the
// target instead of CLogFormatter. Building the same 26 fields keeps
// the blank item-state columns and the remaining-amount field exactly.
void WriteDepositItemsToBankExFeeAudit(
    const WareHouseAuditActor& actor,
    CItem& feeItem,
    int warehouseClassType,
    std::int64_t feeAmount,
    std::int64_t remainingAmount
) {
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x3b1);
    fields[1] = runtime::DecimalUtf16(actor.characterId);
    fields[2] = runtime::DecimalUtf16(actor.accountId);
    fields[5] = runtime::DecimalUtf16(actor.x);
    fields[6] = runtime::DecimalUtf16(actor.y);
    fields[7] = runtime::DecimalUtf16(actor.z);
    fields[11] = runtime::DecimalUtf16(actor.race);
    fields[12] = runtime::DecimalUtf16(actor.gender);
    fields[13] = runtime::DecimalUtf16(actor.characterClass);
    fields[14] = runtime::DecimalUtf16(actor.level);
    fields[15] = runtime::DecimalUtf16(warehouseClassType);
    fields[17] = runtime::DecimalUtf16(feeItem.Enchant());
    fields[18] = runtime::DecimalUtf16(feeItem.ItemType());
    fields[19] = runtime::DecimalUtf16(feeAmount);
    fields[20] = runtime::DecimalUtf16(remainingAmount);
    fields[21] = actor.characterName;
    fields[22] = actor.accountName;
    fields[25] = runtime::DecimalUtf16(feeItem.ItemID());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

// Current CWareHouse::Deposit writes the fee record before walking the
// ordered transaction map. Its current format leaves field
// 25 blank even though the old C1 call supplied the adena object as an extra
// unused variadic argument.
void WriteDepositFeeAudit(const WareHouseAuditActor& actor, int auditContext, std::int64_t feeAmount) {
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x3a0);
    fields[1] = runtime::DecimalUtf16(actor.characterId);
    fields[2] = runtime::DecimalUtf16(actor.accountId);
    fields[5] = runtime::DecimalUtf16(actor.x);
    fields[6] = runtime::DecimalUtf16(actor.y);
    fields[7] = runtime::DecimalUtf16(actor.z);
    fields[11] = runtime::DecimalUtf16(actor.race);
    fields[12] = runtime::DecimalUtf16(actor.gender);
    fields[13] = runtime::DecimalUtf16(actor.characterClass);
    fields[14] = runtime::DecimalUtf16(actor.level);
    fields[15] = runtime::DecimalUtf16(auditContext);
    fields[17] = runtime::DecimalUtf16(0);
    fields[18] = runtime::DecimalUtf16(57);
    fields[19] = runtime::DecimalUtf16(feeAmount);
    fields[21] = actor.characterName;
    fields[22] = actor.accountName;
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteDepositAudit(
    const WareHouseAuditActor& actor,
    CItem& item,
    CItem& rollbackItem,
    int auditContext,
    std::uint32_t adenaItemId,
    std::int64_t feeAmount,
    int intensiveItemType
) {
    const ItemChangeScope scope = item.ChangeScope();
    if (scope < ItemChangeScope_Added || scope > ItemChangeScope_Deleted) {
        return;
    }

    std::array<char16_t, 256> shapeShiftText{};
    std::array<char16_t, 256> baseAttributeText{};
    std::array<char16_t, 256> ensoulText{};
    item.MakeShapeShiftLogInfo(shapeShiftText.data());
    BaseAttribute baseAttribute(item.Attribute());
    baseAttribute.GetLogString(baseAttributeText.data());
    // item.MakeEnsoulLogInfo(ensoulText.data())
    item.MakeEnsoulLogInfo(ensoulText.data(), ensoulText.size());

    const std::u16string targetShapeShift = std::u16string(shapeShiftText.data());
    const std::u16string targetBaseAttribute = std::u16string(baseAttributeText.data());
    const std::u16string targetEnsoul = std::u16string(ensoulText.data());
    const std::int64_t amountDifference = runtime::SubtractInt64Wrapping(item.Amount(), rollbackItem.Amount());

    if (scope == ItemChangeScope_Increased) {
        CLogFormatter formatter;
        formatter.WriteLog(
            0x39f, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z, targetShapeShift.c_str(),
            targetBaseAttribute.c_str(), targetEnsoul.c_str(), actor.race, actor.gender, actor.characterClass, actor.level,
            auditContext, 0, item.Enchant(), item.ItemType(), amountDifference, item.Amount(), actor.characterName.c_str(),
            actor.accountName.c_str(), nullptr, nullptr, item.ItemID()
        );
        return;
    }

    std::array<wchar_t, 256> variationText{};
    std::swprintf(
        variationText.data(), variationText.size(), L"%d:%d:%d", intensiveItemType, item.OptionKey().variationOption1,
        item.OptionKey().variationOption2
    );

    std::int64_t auditAmount = 0;
    std::int64_t newAmount = 0;
    if (scope == ItemChangeScope_Added) {
        auditAmount = amountDifference;
        if (item.ItemID() == adenaItemId) {
            auditAmount = runtime::AddInt64Wrapping(auditAmount, feeAmount);
        }
        newAmount = item.Amount();
    } else {
        auditAmount = runtime::NegateInt64Wrapping(item.Amount());
    }

    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x387);
    fields[1] = runtime::DecimalUtf16(actor.characterId);
    fields[2] = runtime::DecimalUtf16(actor.accountId);
    fields[5] = runtime::DecimalUtf16(actor.x);
    fields[6] = runtime::DecimalUtf16(actor.y);
    fields[7] = runtime::DecimalUtf16(actor.z);
    fields[8] = targetShapeShift;
    fields[9] = targetBaseAttribute;
    fields[10] = WideToUtf16(variationText.data());
    fields[11] = runtime::DecimalUtf16(actor.race);
    fields[12] = runtime::DecimalUtf16(actor.gender);
    fields[13] = runtime::DecimalUtf16(actor.characterClass);
    fields[14] = runtime::DecimalUtf16(actor.level);
    fields[15] = runtime::DecimalUtf16(auditContext);
    fields[17] = runtime::DecimalUtf16(item.Enchant());
    fields[18] = runtime::DecimalUtf16(item.ItemType());
    fields[19] = runtime::DecimalUtf16(auditAmount);
    fields[20] = runtime::DecimalUtf16(newAmount);
    fields[21] = actor.characterName;
    fields[22] = actor.accountName;
    fields[25] = runtime::DecimalUtf16(item.ItemID());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));

    CLogFormatter formatter;
    formatter.WriteLog(
        0x234f, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z, targetEnsoul.c_str(), nullptr, nullptr,
        actor.race, actor.gender, actor.characterClass, actor.level, 0, 0, 0, 0, 0, 0, actor.characterName.c_str(),
        actor.accountName.c_str(), nullptr, nullptr, item.ItemID()
    );
}

// Current CWareHouse::SellItems emits one 0x386 record for every ordered
// transaction entry, including a newly-created or changed adena item. The
// three text fields are shape+variation, base attribute and ensoul data in
// that exact order.
void WriteSellItemsAudit(const WareHouseAuditActor& actor, CItem& item, CItem& rollbackItem, int auditContext) {
    std::array<char16_t, 256> shapeShiftAndVariationText{};
    std::array<char16_t, 256> baseAttributeText{};
    std::array<char16_t, 256> ensoulText{};
    item.MakeShapeShiftAndVariationLogInfo(shapeShiftAndVariationText.data());
    BaseAttribute baseAttribute(item.Attribute());
    baseAttribute.GetLogString(baseAttributeText.data());
    // item.MakeEnsoulLogInfo(ensoulText.data())
    item.MakeEnsoulLogInfo(ensoulText.data(), ensoulText.size());

    const std::int64_t amountDifference = runtime::SubtractInt64Wrapping(item.Amount(), rollbackItem.Amount());
    const std::u16string shapeShiftAndVariation = std::u16string(shapeShiftAndVariationText.data());
    const std::u16string baseAttributeValue = std::u16string(baseAttributeText.data());
    const std::u16string ensoulValue = std::u16string(ensoulText.data());

    CLogFormatter formatter;
    formatter.WriteLog(
        0x386, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z, shapeShiftAndVariation.c_str(),
        baseAttributeValue.c_str(), ensoulValue.c_str(), actor.race, actor.gender, actor.characterClass, actor.level,
        auditContext, 0, item.Enchant(), item.ItemType(), amountDifference, item.Amount(), actor.characterName.c_str(),
        actor.accountName.c_str(), nullptr, nullptr, item.ItemID()
    );
}

// Current CWareHouse::TradeItems writes one aggregate 0x3b9 record before
// replying to L2Server. The target format ends with a blank field rather
// than numeric item id, so the explicit 26-field carrier is required here.
void WriteTradeAggregateAudit(CUser& source, CUser& target, std::int64_t sourceTotalPrice, std::int64_t targetTotalPrice) {
    const WareHouseAuditActor sourceActor = MakeWareHouseAuditActor(&source);
    const WareHouseAuditActor targetActor = MakeWareHouseAuditActor(&target);

    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x3b9);
    fields[1] = runtime::DecimalUtf16(sourceActor.characterId);
    fields[2] = runtime::DecimalUtf16(sourceActor.accountId);
    fields[3] = runtime::DecimalUtf16(targetActor.characterId);
    fields[4] = runtime::DecimalUtf16(targetActor.accountId);
    fields[5] = runtime::DecimalUtf16(sourceActor.x);
    fields[6] = runtime::DecimalUtf16(sourceActor.y);
    fields[7] = runtime::DecimalUtf16(sourceActor.z);
    fields[11] = runtime::DecimalUtf16(sourceActor.race);
    fields[12] = runtime::DecimalUtf16(sourceActor.gender);
    fields[13] = runtime::DecimalUtf16(sourceActor.characterClass);
    fields[14] = runtime::DecimalUtf16(sourceActor.level);
    fields[15] = runtime::DecimalUtf16(targetActor.characterClass);
    fields[16] = runtime::DecimalUtf16(targetActor.level);
    fields[17] = runtime::DecimalUtf16(source.GetPledgeID());
    fields[18] = runtime::DecimalUtf16(target.GetPledgeID());
    fields[19] = runtime::DecimalUtf16(sourceTotalPrice);
    fields[20] = runtime::DecimalUtf16(targetTotalPrice);
    fields[21] = sourceActor.characterName;
    fields[22] = sourceActor.accountName;
    fields[23] = targetActor.characterName;
    fields[24] = targetActor.accountName;
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteTradeDivideAdenaAudit(
    const WareHouseAuditActor& source,
    const WareHouseAuditActor& target,
    const CItem& sourceItem,
    const CItem& targetItem,
    std::int64_t amount
) {
    // Current CWareHouse::TradeDivideAdena 0x006180fc writes this raw route-6
    // record 0x2339 before its success reply. It is not one of the ordinary
    // per-item trade audit formats below.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x2339);
    fields[1] = runtime::DecimalUtf16(source.characterId);
    fields[2] = runtime::DecimalUtf16(source.accountId);
    fields[3] = runtime::DecimalUtf16(target.characterId);
    fields[4] = runtime::DecimalUtf16(target.accountId);
    fields[5] = runtime::DecimalUtf16(source.x);
    fields[6] = runtime::DecimalUtf16(source.y);
    fields[7] = runtime::DecimalUtf16(source.z);
    fields[11] = runtime::DecimalUtf16(source.race);
    fields[12] = runtime::DecimalUtf16(source.gender);
    fields[13] = runtime::DecimalUtf16(source.characterClass);
    fields[14] = runtime::DecimalUtf16(target.characterClass);
    fields[15] = runtime::DecimalUtf16(target.level);
    fields[16] = runtime::DecimalUtf16(source.level);
    fields[17] = runtime::DecimalUtf16(sourceItem.ItemID());
    fields[18] = runtime::DecimalUtf16(targetItem.ItemID());
    fields[19] = runtime::DecimalUtf16(amount);
    fields[20] = runtime::DecimalUtf16(0);
    fields[21] = source.characterName;
    fields[22] = source.accountName;
    fields[23] = target.characterName;
    fields[24] = target.accountName;
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteTradeItemRecord(
    int logId,
    const WareHouseAuditActor& actor,
    const WareHouseAuditActor& target,
    CItem& item,
    std::int64_t amount,
    std::int64_t finalAmount
) {
    std::array<char16_t, 256> shapeShiftAndVariationText{};
    std::array<char16_t, 256> baseAttributeText{};
    std::array<char16_t, 256> ensoulText{};
    item.MakeShapeShiftAndVariationLogInfo(shapeShiftAndVariationText.data());
    BaseAttribute baseAttribute(item.Attribute());
    baseAttribute.GetLogString(baseAttributeText.data());
    // item.MakeEnsoulLogInfo(ensoulText.data())
    item.MakeEnsoulLogInfo(ensoulText.data(), ensoulText.size());

    const std::u16string shapeShiftAndVariation = std::u16string(shapeShiftAndVariationText.data());
    const std::u16string baseAttributeValue = std::u16string(baseAttributeText.data());
    const std::u16string ensoulValue = std::u16string(ensoulText.data());

    CLogFormatter formatter;
    formatter.WriteLog(
        logId, actor.characterId, actor.accountId, target.characterId, target.accountId, actor.x, actor.y, actor.z,
        shapeShiftAndVariation.c_str(), baseAttributeValue.c_str(), ensoulValue.c_str(), actor.race, actor.gender,
        actor.characterClass, actor.level, target.characterClass, target.level, item.Enchant(), item.ItemType(), amount,
        finalAmount, actor.characterName.c_str(), actor.accountName.c_str(), target.characterName.c_str(),
        target.accountName.c_str(), item.ItemID()
    );
}

void WriteTradeLargeAdenaAudit(
    const WareHouseAuditActor& actor,
    const WareHouseAuditActor& target,
    CItem& item,
    std::int64_t amount,
    std::int64_t finalAmount
) {
    CLogFormatter formatter;
    formatter.WriteAuditLog(
        0x58a, actor.characterId, actor.accountId, target.characterId, target.accountId, actor.x, actor.y, actor.z, nullptr,
        nullptr, nullptr, actor.race, actor.gender, actor.characterClass, actor.level, target.characterClass, target.level,
        item.Enchant(), item.ItemType(), amount, finalAmount, actor.characterName.c_str(), actor.accountName.c_str(),
        target.characterName.c_str(), target.accountName.c_str(), item.ItemID()
    );
}

// Current CWareHouse::PrivateStoreCommit emits 0x58c directly when the
// per-item store price reaches the configured adena audit threshold. Unlike
// the ordinary 0x3a3/0x3a4 item records, this format leaves the
// three item-description fields blank.
void WritePrivateStoreLargePriceAudit(
    const WareHouseAuditActor& actor,
    const WareHouseAuditActor& target,
    CItem& item,
    std::int64_t amount,
    std::int64_t privateStorePrice
) {
    CLogFormatter formatter;
    formatter.WriteAuditLog(
        0x58c, actor.characterId, actor.accountId, target.characterId, target.accountId, actor.x, actor.y, actor.z, nullptr,
        nullptr, nullptr, actor.race, actor.gender, actor.characterClass, actor.level, target.characterClass, target.level,
        item.Enchant(), item.ItemType(), amount, privateStorePrice, actor.characterName.c_str(), actor.accountName.c_str(),
        target.characterName.c_str(), target.accountName.c_str(), item.ItemID()
    );
}

void WriteTradeItemAudit(
    const WareHouseAuditActor& actor, const WareHouseAuditActor& target, CItem& item, CItem& rollbackItem
) {
    const ItemChangeScope scope = rollbackItem.ChangeScope();
    const std::int64_t liveAmount = item.Amount();
    const std::int64_t rollbackAmount = rollbackItem.Amount();
    const std::uint64_t adenaLogLimit = GetCacheDConfiguration().adenaLogLimit;

    switch (scope) {
    case ItemChangeScope_Added: {
        const std::int64_t amount = runtime::SubtractInt64Wrapping(liveAmount, rollbackAmount);
        WriteTradeItemRecord(0x38d, actor, target, item, amount, liveAmount);
        const std::int64_t transferred = runtime::SubtractInt64Wrapping(rollbackAmount, liveAmount);
        if (item.ItemType() == 57U && transferred >= 0 && static_cast<std::uint64_t>(transferred) >= adenaLogLimit) {
            WriteTradeLargeAdenaAudit(actor, target, item, amount, liveAmount);
        }
        return;
    }
    case ItemChangeScope_Increased: {
        const std::int64_t amount = runtime::NegateInt64Wrapping(liveAmount);
        WriteTradeItemRecord(0x38d, actor, target, item, amount, 0);
        if (item.ItemType() == 57U && liveAmount >= 0 && static_cast<std::uint64_t>(liveAmount) >= adenaLogLimit) {
            WriteTradeLargeAdenaAudit(actor, target, item, amount, 0);
        }
        return;
    }
    case ItemChangeScope_Decreased:
        WriteTradeItemRecord(0x38e, actor, target, item, runtime::SubtractInt64Wrapping(liveAmount, rollbackAmount), liveAmount);
        return;
    case ItemChangeScope_Deleted:
        WriteTradeItemRecord(0x38e, actor, target, item, liveAmount, liveAmount);
        return;
    case ItemChangeScope_Fee: {
        const std::int64_t amount = runtime::NegateInt64Wrapping(liveAmount);
        WriteTradeItemRecord(0x38d, actor, target, item, amount, 0);
        if (item.ItemType() == 57U && liveAmount >= 0 && static_cast<std::uint64_t>(liveAmount) >= adenaLogLimit) {
            WriteTradeLargeAdenaAudit(actor, target, item, amount, 0);
        }
        WriteTradeItemRecord(0x38e, target, actor, item, liveAmount, liveAmount);
        return;
    }
    default:
        // Log.Add(
        //     LOG_ERROR, "unknown trade type [%d]. [%u][%u][%lld][%lld]", static_cast<int>(scope), item.ItemID(),
        //     item.ItemType(), static_cast<long long>(liveAmount), static_cast<long long>(rollbackAmount)
        // )
        // L2_LOG_ERROR(Log, "unknown trade type [%d]. [%u][%u][%lld][%lld]", static_cast<int>(scope), item.ItemID(),
        //     item.ItemType(), static_cast<long long>(liveAmount), static_cast<long long>(rollbackAmount)
        // )
        L2_LOG_ERROR(Log,
            "[trade audit] Unsupported change scope %d for item %u, class %u: current amount %lld, rollback amount %lld. "
            "Trade item records were not emitted by this branch.",
            static_cast<int>(scope), item.ItemID(), item.ItemType(),
            static_cast<long long>(liveAmount), static_cast<long long>(rollbackAmount));
        return;
    }
}

// Current vtable 0x007669d0 names this exact callback owner. Its sole method
// is current 0x005ce8b4.
class CDeleteAllItemInWareHouse {
public:
    explicit CDeleteAllItemInWareHouse(bool hardDelete) : m_hardDelete(hardDelete) {}

    bool Do(CItem* item) {
        if (!item->Delete(m_hardDelete, false)) {
            // Log.Add(
            //     LOG_ERROR, "[%s][%d] item delete fail[%u]", kWareHouseHeaderPath, 0x249, item->ItemID()
            // )
            L2_LOG_ERROR(Log,
                "[warehouse] Bulk deletion failed for item %u, class %u (hard delete %d). "
                "The callback continues; the bulk operation still clears the warehouse map and reports success.",
                item->ItemID(), item->ItemType(), static_cast<int>(m_hardDelete));
        }
        return true;
    }

private:
    bool m_hardDelete;
};

// This callback is inlined/macro-generated in the current image, so no
// source identifier survives. The semantic name records its exact condition
// at 0x005d38bc without presenting it as a recovered original identifier.
class CFindItemByTypeInWareHouse {
public:
    CFindItemByTypeInWareHouse(int itemType, int npcId, WareHouseType warehouseType) :
        m_itemType(itemType), m_npcId(npcId), m_warehouseType(warehouseType) {}

    bool Do(CItem* item) {
        if (item->ItemType() != static_cast<std::uint32_t>(m_itemType) ||
            item->Warehouse() != static_cast<int>(m_warehouseType) || item->Ident() != m_npcId) {
            return true;
        }
        m_result.SetObject(item, ObjectRefType::FunctionChain);
        return false;
    }

    CItemSP TakeResult() { return std::move(m_result); }

private:
    int m_itemType;
    int m_npcId;
    WareHouseType m_warehouseType;
    CItemSP m_result;
};

// The C1 owner keeps the original callback spelling. Current 0x005d3b74
// proves the same condition and early-stop behavior.
class GetAdenaPredicate {
public:
    bool Do(CItem* item) {
        if (item->ItemType() != 57U)
            return true;
        m_result.SetObject(item, ObjectRefType::FunctionChain);
        return false;
    }

    CItemSP TakeResult() { return std::move(m_result); }

private:
    CItemSP m_result;
};

// The C1 owner keeps this file-local source spelling. Current
// CWareHouse::AddAssetItem 0x005d9038 fixes the same item-type
// comparison, first-match stop and FunctionChain reference acquisition.
class ItemByTypePredicate {
public:
    explicit ItemByTypePredicate(int itemType) noexcept : m_itemType(itemType) {}

    bool Do(CItem* item) {
        if (item->ItemType() != static_cast<std::uint32_t>(m_itemType)) {
            return true;
        }
        m_result.SetObject(item, ObjectRefType::FunctionChain);
        return false;
    }

    CItemSP TakeResult() { return std::move(m_result); }

private:
    int m_itemType;
    CItemSP m_result;
};

// These three semantic callback names represent inlined
// FOR_ALL_OBJECT_IN_OBJECT_MAP bodies in current WareHouse.cpp. No separate
// callback identifiers survive. Their conditions, early-stop behavior and
// source lines are fixed by Cached.exe 0x005d47a4..0x005d513d.
class SendMonRaceTicketListPredicate {
public:
    explicit SendMonRaceTicketListPredicate(int page) noexcept :
        m_page(page < 1 ? 1 : page),
        m_pageEnd(runtime::MultiplyInt32Wrapping(m_page, 10)),
        m_pageBegin(runtime::AddInt32Wrapping(m_pageEnd, -10)) {}

    bool Do(CItem* item) {
        const std::uint32_t itemType = item->ItemType();
        if (itemType != 0x115bU && itemType != 0x115cU) {
            return true;
        }

        if (m_pageBegin <= m_ticketIndex && m_ticketIndex < m_pageEnd) {
            const int assembled = ::Assemble(
                m_packet.data() + m_packetBytes, static_cast<int>(m_packet.size()) - m_packetBytes, "ddQddddd",
                static_cast<int>(item->ItemID()), static_cast<int>(itemType), static_cast<long long>(item->Amount()),
                item->Enchant(), item->Eroded(), item->Bless(), item->Ident(), item->Wished()
            );
            if (assembled > 0) {
                m_packetBytes += assembled;
                ++m_itemCount;
            }
        }
        m_ticketIndex = runtime::AddInt32Wrapping(m_ticketIndex, 1);
        return true;
    }

    int Page() const noexcept { return m_page; }
    int ItemCount() const noexcept { return m_itemCount; }
    int PacketBytes() const noexcept { return m_packetBytes; }
    const char* Packet() const noexcept { return m_packet.data(); }

private:
    int m_page;
    int m_pageEnd;
    int m_pageBegin;
    int m_ticketIndex = 0;
    int m_itemCount = 0;
    int m_packetBytes = 0;
    std::array<char, 0x5000> m_packet{};
};

class CheckMonRaceTicketPredicate {
public:
    explicit CheckMonRaceTicketPredicate(unsigned int itemId) noexcept : m_itemId(itemId) {}

    bool Do(CItem* item) {
        const std::uint32_t itemType = item->ItemType();
        if ((itemType == 0x115bU || itemType == 0x115cU) && item->ItemID() == m_itemId) {
            m_found = true;
            return false;
        }
        return true;
    }

    bool Found() const noexcept { return m_found; }

private:
    unsigned int m_itemId;
    bool m_found = false;
};

class DeleteMonRaceTicketFromDepositPredicate {
public:
    explicit DeleteMonRaceTicketFromDepositPredicate(unsigned int itemId) noexcept : m_itemId(itemId) {}

    bool Do(CItem* item) {
        const std::uint32_t itemType = item->ItemType();
        if ((itemType != 0x115bU && itemType != 0x115cU) || item->ItemID() != m_itemId) {
            return true;
        }

        m_found = true;
        m_foundItemId = static_cast<int>(item->ItemID());
        item->setOwnerID(0);
        item->Save(true);
        item->Delete(false, false);
        return false;
    }

    bool Found() const noexcept { return m_found; }
    int FoundItemId() const noexcept { return m_foundItemId; }

private:
    unsigned int m_itemId;
    bool m_found = false;
    int m_foundItemId = 0;
};

// The current function is compiled inside the object-map traversal at
// 0x005d5140 and has no recoverable source identifier. Its four accessor
// calls and addition widths are nevertheless fixed by the machine code.
class CAccumulateWareHouseCheckSum {
public:
    bool Do(CItem* item) {
        const std::uint32_t itemIdentity = item->ItemID() + item->ItemType();
        m_value += item->Amount();
        m_value += static_cast<std::int64_t>(itemIdentity);
        m_value += static_cast<std::int64_t>(item->Enchant());
        return true;
    }

    std::int64_t Value() const noexcept { return m_value; }

private:
    std::int64_t m_value = 0;
};

// The older CacheD source keeps the original callback identifier. Current
// 0x005d7ed8 replaces its one-buffer payload with ItemAssembler packaging and
// widens all three audit aggregates. Those current meaning are authoritative.
class SendListPredicate {
public:
    explicit SendListPredicate(CPackageItemPacketManager& packetManager) : m_packetManager(packetManager) {}

    bool Do(CItem* item) {
        const std::uint32_t itemType = item->ItemType();
        const std::int64_t amount = item->Amount();
        if (itemType == 57U) {
            adenaSum += amount;
        }
        itemSum += amount;

        const CReadItemData* itemData = g_readFile.FindItem(static_cast<int>(itemType));
        if (itemData) {
            defaultPriceSum += itemData->m_defaultPrice * amount;
        }

        m_packetManager.CheckLimitPackageItemPacketDataSize();
        // CItemAssemblerLoadItem itemAssembler(*item, 0x7f)
        CItemAssemblerLoadItem itemAssembler(*item, ItemAssembleType::ALL);
        const int assembledLength = m_packetManager.PackageAssembleItem(itemAssembler);
        if (assembledLength < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     "Assembe Error at file[%s], line[%d], assemble result = "
            //     "[%d], total length = [%d]",
            //     "WareHouse.cpp", 0x31c, assembledLength, m_packetManager.GetTotalPackageItemLength()
            // )
            // L2_LOG_ERROR(Log,
            //     "Assembe Error at file[%s], line[%d], assemble result = "
            //     "[%d], total length = [%d]",
            //     "WareHouse.cpp", 0x31c, assembledLength, m_packetManager.GetTotalPackageItemLength()
            // )
            L2_LOG_ERROR(Log,
                "[warehouse] Cannot assemble load-list item for item %u, class %u: "
                "assembly result %d, total package length %d. Stopping this traversal.",
                static_cast<unsigned int>(item->ItemID()), static_cast<unsigned int>(item->ItemType()),
                assembledLength, m_packetManager.GetTotalPackageItemLength());
            return false;
        }

        if (m_packetManager.m_nTotalPackageItemCount == 0) {
            m_packetManager.m_nAssembleItemLengthPerItem += assembledLength;
        }
        PACKAGE_ITEM_PACKETHELPER& currentPackage =
            m_packetManager
                .m_vPackageItemPacketHelperList[static_cast<std::size_t>(m_packetManager.m_nCurrentPacketHelperIndex)];
        ++currentPackage.nItemCount;
        ++m_packetManager.m_nTotalPackageItemCount;
        return true;
    }

    std::int64_t adenaSum = 0;
    std::int64_t defaultPriceSum = 0;
    std::int64_t itemSum = 0;

private:
    CPackageItemPacketManager& m_packetManager;
};

// Current CWareHouse::SendItemList 0x005e98b8 keeps the C1 callback's
// identifier, but uses the complete item assembler and package manager. The
// target stops after including item 200 and does not filter by item type. Its
// duplicated item-type-3 machine branch has the same observable operations.
class SendItemListPredicate {
public:
    explicit SendItemListPredicate(CPackageItemPacketManager& packetManager) : m_packetManager(packetManager) {}

    bool Do(CItem* item) {
        m_packetManager.CheckLimitPackageItemPacketDataSize();
        // CItemAssemblerSendToL2Server itemAssembler(*item, 0x7f)
        CItemAssemblerSendToL2Server itemAssembler(*item, ItemAssembleType::ALL);
        const int assembledLength = m_packetManager.PackageAssembleItem(itemAssembler);
        if (assembledLength < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     "Assembe Error at file[%s], line[%d], assemble result = "
            //     "[%d], total length = [%d]",
            //     "WareHouse.cpp", 0x2cd4, assembledLength, m_packetManager.GetTotalPackageItemLength()
            // )
            // L2_LOG_ERROR(Log,
            //     "Assembe Error at file[%s], line[%d], assemble result = "
            //     "[%d], total length = [%d]",
            //     "WareHouse.cpp", 0x2cd4, assembledLength, m_packetManager.GetTotalPackageItemLength()
            // )
            L2_LOG_ERROR(Log,
                "[warehouse] Cannot assemble item-list entry for item %u, class %u: "
                "assembly result %d, total package length %d. Continuing with the next item.",
                static_cast<unsigned int>(item->ItemID()), static_cast<unsigned int>(item->ItemType()),
                assembledLength, m_packetManager.GetTotalPackageItemLength());
            // Current 0x005e9ae0/0x005e9b9a advances to the next map node.
            // one malformed item does not terminate the warehouse traversal.
            return true;
        }

        if (m_packetManager.m_nTotalPackageItemCount == 0) {
            m_packetManager.m_nAssembleItemLengthPerItem += assembledLength;
        }
        PACKAGE_ITEM_PACKETHELPER& currentPackage =
            m_packetManager
                .m_vPackageItemPacketHelperList[static_cast<std::size_t>(m_packetManager.m_nCurrentPacketHelperIndex)];
        ++currentPackage.nItemCount;
        ++m_packetManager.m_nTotalPackageItemCount;
        return m_packetManager.m_nTotalPackageItemCount < 200;
    }

private:
    CPackageItemPacketManager& m_packetManager;
};

// C1 keeps the original SendDepositItemsList callback idea, while current
// 0x005d791c replaces its eight-int item records with the complete item
// assembler, a trailing agathion-energy double and package splitting.
class SendDepositItemsListPredicate {
public:
    SendDepositItemsListPredicate(
        CPackageItemPacketManager& packetManager,
        CPacketHelperEx<20480>& consumeItemTypes,
        int requestedIdent,
        int itemListGetType
    ) :
        m_packetManager(packetManager),
        m_consumeItemTypes(consumeItemTypes),
        m_requestedIdent(requestedIdent),
        m_itemListGetType(itemListGetType) {}

    bool Do(CItem* item) {
        const int itemIdent = item->Ident();
        const bool selected = (m_requestedIdent > 0 && itemIdent > 0) || (m_requestedIdent == 0 && itemIdent == 0);
        if (!selected)
            return true;

        m_packetManager.CheckLimitPackageItemPacketDataSize();
        // CItemAssemblerSendToL2Server itemAssembler(*item, 0x7f)
        CItemAssemblerSendToL2Server itemAssembler(*item, ItemAssembleType::ALL);
        int assembledLength = m_packetManager.PackageAssembleItem(itemAssembler);
        assembledLength += m_packetManager.PackageAssemble("f", item->LoadAgathionEnergy());
        if (assembledLength < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     "Assembe Error at file[%s], line[%d], assemble result = "
            //     "[%d], total length = [%d]",
            //     "WareHouse.cpp", 0x28b, assembledLength, m_packetManager.GetTotalPackageItemLength()
            // )
            // L2_LOG_ERROR(Log,
            //     "Assembe Error at file[%s], line[%d], assemble result = "
            //     "[%d], total length = [%d]",
            //     "WareHouse.cpp", 0x28b, assembledLength, m_packetManager.GetTotalPackageItemLength()
            // )
            L2_LOG_ERROR(Log,
                "[warehouse] Cannot assemble deposit-list entry including agathion energy for item %u, class %u: "
                "assembly result %d, total package length %d. Continuing with the next item.",
                static_cast<unsigned int>(item->ItemID()), static_cast<unsigned int>(item->ItemType()),
                assembledLength, m_packetManager.GetTotalPackageItemLength());
            // Current 0x005d7bd4 advances to the next map node after logging.
            return true;
        }

        PACKAGE_ITEM_PACKETHELPER& currentPackage =
            m_packetManager
                .m_vPackageItemPacketHelperList[static_cast<std::size_t>(m_packetManager.m_nCurrentPacketHelperIndex)];
        if (m_packetManager.m_nTotalPackageItemCount == 0) {
            m_packetManager.m_nAssembleItemLengthPerItem += assembledLength;
        }
        ++currentPackage.nItemCount;
        ++m_packetManager.m_nTotalPackageItemCount;

        if (m_itemListGetType == 1) {
            const int itemType = static_cast<int>(item->ItemType());
            const ItemConsumeType consumeType = g_readFile.GetItemConsumeType(itemType);
            if (consumeType == ICT_STACKABLE || consumeType == ICT_ASSET) {
                m_consumeItemTypes.Assemble("d", itemType);
            }
        }

        // Current 0x005d7c7e stops after including item number 200.
        return m_packetManager.m_nTotalPackageItemCount < 200;
    }

private:
    CPackageItemPacketManager& m_packetManager;
    CPacketHelperEx<20480>& m_consumeItemTypes;
    int m_requestedIdent;
    int m_itemListGetType;
};

// Current CWareHouse::SendDepositItemsListEx 0x005f5384 emits this traversal
// directly inside WareHouse.cpp. It differs from SendDepositItemsList only by
// selecting every warehouse item and by retaining the current source line.
class SendDepositItemsListExPredicate {
public:
    SendDepositItemsListExPredicate(
        CPackageItemPacketManager& packetManager, CPacketHelperEx<20480>& consumeItemTypes, int itemListGetType
    ) :
        m_packetManager(packetManager), m_consumeItemTypes(consumeItemTypes), m_itemListGetType(itemListGetType) {}

    bool Do(CItem* item) {
        m_packetManager.CheckLimitPackageItemPacketDataSize();
        // CItemAssemblerSendToL2Server itemAssembler(*item, 0x7f)
        CItemAssemblerSendToL2Server itemAssembler(*item, ItemAssembleType::ALL);
        int assembledLength = m_packetManager.PackageAssembleItem(itemAssembler);
        assembledLength += m_packetManager.PackageAssemble("f", item->LoadAgathionEnergy());
        if (assembledLength < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     "Assembe Error at file[%s], line[%d], assemble result = "
            //     "[%d], total length = [%d]",
            //     "WareHouse.cpp", 0x389b, assembledLength, m_packetManager.GetTotalPackageItemLength()
            // )
            // L2_LOG_ERROR(Log,
            //     "Assembe Error at file[%s], line[%d], assemble result = "
            //     "[%d], total length = [%d]",
            //     "WareHouse.cpp", 0x389b, assembledLength, m_packetManager.GetTotalPackageItemLength()
            // )
            L2_LOG_ERROR(Log,
                "[warehouse] Cannot assemble extended deposit-list entry including agathion energy for item %u, class %u: "
                "assembly result %d, total package length %d. Continuing with the next item.",
                static_cast<unsigned int>(item->ItemID()), static_cast<unsigned int>(item->ItemType()),
                assembledLength, m_packetManager.GetTotalPackageItemLength());
            // Current 0x005f5659 rejoins the physical next-node path.
            return true;
        }

        PACKAGE_ITEM_PACKETHELPER& currentPackage =
            m_packetManager
                .m_vPackageItemPacketHelperList[static_cast<std::size_t>(m_packetManager.m_nCurrentPacketHelperIndex)];
        if (m_packetManager.m_nTotalPackageItemCount == 0) {
            m_packetManager.m_nAssembleItemLengthPerItem += assembledLength;
        }
        ++currentPackage.nItemCount;
        ++m_packetManager.m_nTotalPackageItemCount;

        if (m_itemListGetType == 1) {
            const int itemType = static_cast<int>(item->ItemType());
            const ItemConsumeType consumeType = g_readFile.GetItemConsumeType(itemType);
            if (consumeType == ICT_STACKABLE || consumeType == ICT_ASSET) {
                m_consumeItemTypes.Assemble("d", itemType);
            }
        }

        int packageItemCount = 0;
        for (const PACKAGE_ITEM_PACKETHELPER& package : m_packetManager.m_vPackageItemPacketHelperList) {
            packageItemCount += package.nItemCount;
        }
        return packageItemCount < 200;
    }

private:
    CPackageItemPacketManager& m_packetManager;
    CPacketHelperEx<20480>& m_consumeItemTypes;
    int m_itemListGetType;
};

} // namespace

CTransaction::CTransaction() : m_success(true), m_items(), m_order(0) { L2_GUARD_FUNCTION(); }

CTransaction::~CTransaction() {
    L2_GUARD_FUNCTION();
    if (!m_success) {
        RollbackTransactOrderMap(m_items);
        return;
    }

    for (const auto& entry : m_items) {
        CItem* rollbackItem = entry.second.rollbackItem;
        CItem* item = entry.second.item;
        item->CommitTransation(rollbackItem, true);
        L2_RELEASE(item, ObjectRefType::FunctionLocal);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
    }
}

void CTransaction::SetCommit() {
    L2_GUARD_FUNCTION();
    m_success = true;
}

void CTransaction::SetRollback() {
    L2_GUARD_FUNCTION();
    m_success = false;
}

bool CTransaction::AddItem(CItemSP item) {
    L2_GUARD_FUNCTION();
    if (item->Deleted()) {
        // Log.Add(LOG_ERROR, "User try change deleted item.")
        // Log.Add(LOG_ERROR,
        //     "[item transaction] Cannot enroll deleted item %u (owner %u, class %u). Item not added to the transaction.",
        //     item->ItemID(), item->OwnerID(), item->ItemType())
        L2_LOG_ERROR(Log,
            "[item transaction] Cannot enroll deleted item %u (owner %u, class %u); item not added to the transaction.",
            item->ItemID(), item->OwnerID(), item->ItemType());
        runtime::process::ReportNamedCallStack(L"CTransaction::AddItem", 2, false);
        return false;
    }

    for (const auto& entry : m_items) {
        CItem* existing = entry.second.item;
        if (existing->ItemID() != 0 && existing->ItemID() == item->ItemID()) {
            return false;
        }
    }

    item->SetTransation();
    CItemSP rollbackItem = item->Copy();
    rollbackItem->SetTrasactMode(TransactType_RollbackChanges);
    L2_ADD_REF(item, ObjectRefType::FunctionLocal);
    L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
    m_items.emplace(++m_order, CItemKey(rollbackItem.get(), item.get()));
    return true;
}

bool CTransaction::AddItemNew(CItemSP item) {
    L2_GUARD_FUNCTION();
    item->SetTransation();
    CItemSP rollbackItem = item->Copy();
    rollbackItem->SetTrasactMode(TransactType_RollbackCreation);
    L2_ADD_REF(item, ObjectRefType::FunctionLocal);
    L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
    m_items.emplace(++m_order, CItemKey(rollbackItem.get(), item.get()));
    return true;
}

void CTransaction::RollbackTransactOrderMap(CItemTransactionOrderMap& items) {
    L2_GUARD_FUNCTION();
    for (const auto& entry : items) {
        CItem* rollbackItem = entry.second.rollbackItem;
        CItem* item = entry.second.item;
        item->RollbackTransation(rollbackItem);
        L2_RELEASE(item, ObjectRefType::FunctionLocal);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
    }
}

bool CWareHouse::_CheckItemTradeValidation(
    CWareHouse* sourceWarehouse,
    CWareHouse* targetWarehouse,
    int itemType,
    int sourceTradeType,
    int sourceItemId,
    int sourceServerToken [[maybe_unused]],
    int targetTradeType,
    int targetItemId,
    int targetServerToken [[maybe_unused]],
    std::int64_t amount
) {
    // Current Cached.exe 0x005d0e88. The two server tokens are intentionally
    // not part of validation. The target only echoes them in the reply.
    L2_GUARD_FUNCTION();

    // auto invalid = [](int line) {
    //     Log.Add(LOG_ERROR, "[%s][%d]invalid", "WareHouse.cpp", line)
    //     return false
    // }
    auto invalid = [&](const char* reason) {
        // Log.Add(LOG_ERROR,
        //     "[trade] Cannot transfer item %d from owner %u to owner %u: %s (class %d, quantity %lld, destination item %d. Source action %d, destination action %d).",
        //     sourceItemId, sourceWarehouse->m_ownerId, targetWarehouse->m_ownerId, reason, itemType,
        //     static_cast<long long>(amount), targetItemId, sourceTradeType, targetTradeType)
        L2_LOG_ERROR(Log,
            "[trade] Cannot transfer item %d from owner %u to owner %u: %s (class %d, quantity %lld, destination item %d; source action %d, destination action %d).",
            sourceItemId, sourceWarehouse->m_ownerId, targetWarehouse->m_ownerId, reason, itemType,
            static_cast<long long>(amount), targetItemId, sourceTradeType, targetTradeType);
        return false;
    };

    if (!sourceWarehouse || !targetWarehouse) {
        // Log.Add(LOG_ERROR, "src warehouse is null")
        // Log.Add(LOG_ERROR,
        //     "[trade] Cannot validate transfer of item class %d, quantity %lld: source warehouse is %s, destination warehouse is %s.",
        //     itemType, static_cast<long long>(amount), sourceWarehouse ? "available" : "missing", targetWarehouse ? "available" : "missing")
        L2_LOG_ERROR(Log,
            "[trade] Cannot validate transfer of item class %d, quantity %lld: source warehouse is %s, destination warehouse is %s.",
            itemType, static_cast<long long>(amount), sourceWarehouse ? "available" : "missing", targetWarehouse ? "available" : "missing");
        return false;
    }

    // These are the current CacheD itemdata values, not the separate Shared
    // ItemConsumeType family: 1/3 are individual items, 2/4 are mergeable.
    const int consumeType = g_readFile.GetConsumeType(itemType);
    if (consumeType == 1 || consumeType == 3) {
        if (sourceTradeType != 3 || targetTradeType != 4) {
            // return invalid(0x558d)
            return invalid("individual items require a whole-item transfer to a new destination entry");
        }
        if (targetItemId != 0)
            // return invalid(0x5593)
            return invalid("a new destination entry must have item ID zero");
        if (amount != 1)
            // return invalid(0x55a2)
            return invalid("an individual item requires quantity one");

        CItemSP sourceItem = sourceWarehouse->GetItem(sourceItemId);
        if (!sourceItem || sourceItem->ItemType() != static_cast<std::uint32_t>(itemType) || sourceItem->Amount() != 1) {
            // return invalid(0x559c)
            return invalid(!sourceItem ? "source item is missing" : sourceItem->ItemType() != static_cast<std::uint32_t>(itemType) ? "source class does not match the requested class" : "individual source quantity is not one");
        }
        return true;
    }

    if (consumeType != 2 && consumeType != 4) {
        // Log.Add(LOG_ERROR, "invalid item consume type")
        // Log.Add(LOG_ERROR,
        //     "[trade] Cannot transfer item class %d from owner %u to owner %u: unsupported consume type %d.",
        //     itemType, sourceWarehouse->m_ownerId, targetWarehouse->m_ownerId, consumeType)
        L2_LOG_ERROR(Log,
            "[trade] Cannot transfer item class %d from owner %u to owner %u: unsupported consume type %d.",
            itemType, sourceWarehouse->m_ownerId, targetWarehouse->m_ownerId, consumeType);
        return false;
    }

    auto validateExistingTarget = [&]() {
        CItemSP targetById = targetWarehouse->GetItem(targetItemId);
        CItemSP targetByType = targetWarehouse->GetItemByType(itemType, 0);
        return targetById && targetByType && targetById->ItemType() == static_cast<std::uint32_t>(itemType) &&
            targetById->ItemID() == targetByType->ItemID();
    };

    if (sourceTradeType == 1) {
        CItemSP sourceItem = sourceWarehouse->GetItemByType(itemType, 0);
        if (!sourceItem || amount >= sourceItem->Amount() ||
            sourceItem->ItemID() != static_cast<std::uint32_t>(sourceItemId)) {
            // return invalid(0x55b0)
            return invalid(!sourceItem ? "source stack is missing" : amount >= sourceItem->Amount() ? "a partial-stack transfer must leave items in the source stack" : "source stack ID does not match the request");
        }

        if (targetTradeType == 1) {
            if (targetItemId == 0)
                // return invalid(0x55c4)
                return invalid("merging into an existing stack requires a nonzero destination item ID");
            // return validateExistingTarget() ? true : invalid(0x55be)
            return validateExistingTarget() ? true : invalid("destination item is missing or does not match the cached stack for this class");
        }
        if (targetTradeType == 2) {
            if (targetItemId != 0)
                // return invalid(0x55d5)
                return invalid("creating a destination stack requires item ID zero");
            // return targetWarehouse->GetItemByType(itemType, 0) ? invalid(0x55cf) : true
            return targetWarehouse->GetItemByType(itemType, 0) ? invalid("a destination stack already exists for the requested class") : true;
        }
        // return invalid(0x55db)
        return invalid("a partial-stack transfer requires merging into a stack or creating one");
    }

    if (sourceTradeType != 3)
        // return invalid(0x563a)
        return invalid("unsupported source transfer action for a stackable item");

    CItemSP sourceItem = sourceWarehouse->GetItemByType(itemType, 0);
    if (!sourceItem || sourceItem->Amount() != amount || sourceItem->ItemID() != static_cast<std::uint32_t>(sourceItemId)) {
        // return invalid(0x5609)
        return invalid(!sourceItem ? "source stack is missing" : sourceItem->Amount() != amount ? "whole-stack quantity does not match the source quantity" : "source stack ID does not match the request");
    }

    if (targetTradeType == 1) {
        if (targetItemId == 0)
            // return invalid(0x561d)
            return invalid("merging a whole stack requires a nonzero destination item ID");
        // return validateExistingTarget() ? true : invalid(0x5617)
        return validateExistingTarget() ? true : invalid("destination item is missing or does not match the cached stack for this class");
    }
    if (targetTradeType == 4) {
        if (targetItemId != 0)
            // return invalid(0x562e)
            return invalid("moving a whole stack to a new entry requires destination item ID zero");
        // return targetWarehouse->GetItemByType(itemType, 0) ? invalid(0x5628) : true
        return targetWarehouse->GetItemByType(itemType, 0) ? invalid("a destination stack already exists for the requested class") : true;
    }
    // return invalid(0x5634)
    return invalid("a whole-stack transfer requires merging into a stack or moving it to a new entry");
}

bool CWareHouse::_TransactTradeItems(
    CWareHouse* sourceWarehouse,
    CWareHouse* targetWarehouse,
    std::set<unsigned int>& tradedSourceItems,
    CItemTransactionOrderMap& transactionItems,
    ItemTradeInfo& tradeInfo
) {
    // Current Cached.exe 0x0060f394.
    L2_GUARD_FUNCTION();

    if (!sourceWarehouse || !targetWarehouse) {
        // Log.Add(LOG_ERROR, "[%s][%d]invalid warehouse", "WareHouse.cpp", 0x5651)
        // Log.Add(LOG_ERROR,
        //     "[trade] Cannot process source item %d, destination item %d, quantity %lld: source warehouse is %s, destination warehouse is %s.",
        //     tradeInfo.sourceItemId, tradeInfo.targetItemId, static_cast<long long>(tradeInfo.amount), sourceWarehouse ? "available" : "missing", targetWarehouse ? "available" : "missing")
        L2_LOG_ERROR(Log,
            "[trade] Cannot process source item %d, destination item %d, quantity %lld: source warehouse is %s, destination warehouse is %s.",
            tradeInfo.sourceItemId, tradeInfo.targetItemId, static_cast<long long>(tradeInfo.amount), sourceWarehouse ? "available" : "missing", targetWarehouse ? "available" : "missing");
        return false;
    }

    const unsigned int sourceItemId = static_cast<unsigned int>(tradeInfo.sourceItemId);
    if (!tradedSourceItems.insert(sourceItemId).second) {
        // Log.Add(LOG_ERROR, "[%s][%d]duplicated item. by dbid[%u]", "WareHouse.cpp", 0x5657, sourceItemId)
        // Log.Add(LOG_ERROR,
        //     "[trade] Source item %u occurs more than once in the transaction (owner %u, destination owner %u). Transfer rejected.",
        //     sourceItemId, sourceWarehouse->m_ownerId, targetWarehouse->m_ownerId)
        L2_LOG_ERROR(Log,
            "[trade] Source item %u occurs more than once in the transaction (owner %u, destination owner %u); transfer rejected.",
            sourceItemId, sourceWarehouse->m_ownerId, targetWarehouse->m_ownerId);
        return false;
    }

    tradeInfo.sourceItem = sourceWarehouse->GetItem(tradeInfo.sourceItemId);
    if (!tradeInfo.sourceItem) {
        // Log.Add(LOG_ERROR, "[%s][%d]cannot find item. by dbid[%u]", "WareHouse.cpp", 0x5665, sourceItemId)
        // Log.Add(LOG_ERROR,
        //     "[trade] Cannot transfer source item %u from owner %u to owner %u, quantity %lld: source item not found.",
        //     sourceItemId, sourceWarehouse->m_ownerId, targetWarehouse->m_ownerId, static_cast<long long>(tradeInfo.amount))
        L2_LOG_ERROR(Log,
            "[trade] Cannot transfer source item %u from owner %u to owner %u, quantity %lld: source item not found.",
            sourceItemId, sourceWarehouse->m_ownerId, targetWarehouse->m_ownerId, static_cast<long long>(tradeInfo.amount));
        return false;
    }

    if (!_CheckItemTradeValidation(
            sourceWarehouse, targetWarehouse, static_cast<int>(tradeInfo.sourceItem->ItemType()), tradeInfo.sourceTradeType,
            tradeInfo.sourceItemId, tradeInfo.sourceServerToken, tradeInfo.targetTradeType, tradeInfo.targetItemId,
            tradeInfo.targetServerToken, tradeInfo.amount
        )) {
        // Noise: The validation helper already reports the specific failed transfer constraint and both participants.
        // Log.Add(LOG_ERROR, "[%s][%d]invalid mani options", "WareHouse.cpp", 0x566d)
        return false;
    }

    auto addTransactionPair = [&](CItemSP& item, CItemSP& rollbackItem) {
        L2_ADD_REF(item, ObjectRefType::FunctionLocal);
        L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
        transactionItems.emplace(static_cast<int>(transactionItems.size()) + 1, CItemKey(rollbackItem.get(), item.get()));
    };

    tradeInfo.sourceItem->SetTransation();
    tradeInfo.sourceRollbackItem = tradeInfo.sourceItem->Copy();
    tradeInfo.sourceRollbackItem->SetTrasactMode(TransactType_RollbackChanges);
    addTransactionPair(tradeInfo.sourceItem, tradeInfo.sourceRollbackItem);

    if (tradeInfo.sourceTradeType == 1) {
        tradeInfo.sourceItem->SetAmount(runtime::SubtractInt64Wrapping(tradeInfo.sourceItem->Amount(), tradeInfo.amount));

        if (tradeInfo.targetTradeType == 1) {
            tradeInfo.targetItem = targetWarehouse->GetItem(tradeInfo.targetItemId);
            if (!tradeInfo.targetItem) {
                // Log.Add(
                //     LOG_ERROR, "[%s][%d] Try hack ? . dbid[%d]", "WareHouse.cpp", 0x5692, tradeInfo.targetItemId
                // )
                // Log.Add(LOG_ERROR,
                //     "[trade] Destination item %d for owner %u disappeared after validation. Source item %u (owner %u) was already reduced to %lld in memory. Transfer failed. Caller will attempt rollback.",
                //     tradeInfo.targetItemId, targetWarehouse->m_ownerId, tradeInfo.sourceItem->ItemID(), sourceWarehouse->m_ownerId, static_cast<long long>(tradeInfo.sourceItem->Amount()))
                L2_LOG_ERROR(Log,
                    "[trade] Destination item %d for owner %u disappeared after validation; source item %u (owner %u) was already reduced to %lld in memory. Transfer failed; caller will attempt rollback.",
                    tradeInfo.targetItemId, targetWarehouse->m_ownerId, tradeInfo.sourceItem->ItemID(), sourceWarehouse->m_ownerId, static_cast<long long>(tradeInfo.sourceItem->Amount()));
                return false;
            }

            tradeInfo.targetItem->SetTransation();
            tradeInfo.targetRollbackItem = tradeInfo.targetItem->Copy();
            tradeInfo.targetRollbackItem->SetTrasactMode(TransactType_RollbackChanges);
            addTransactionPair(tradeInfo.targetItem, tradeInfo.targetRollbackItem);
            tradeInfo.targetItem->SetAmount(runtime::AddInt64Wrapping(tradeInfo.targetItem->Amount(), tradeInfo.amount));

            tradeInfo.sourceNewTradeType = 1;
            tradeInfo.targetNewTradeType = 1;
            tradeInfo.sourceResponseItemId = tradeInfo.sourceItemId;
            tradeInfo.targetResponseItemId = tradeInfo.targetItemId;
            tradeInfo.sourceResponseServerToken = tradeInfo.sourceServerToken;
            tradeInfo.targetResponseServerToken = tradeInfo.targetServerToken;
            tradeInfo.sourceNewAmount = tradeInfo.sourceItem->Amount();
            tradeInfo.targetNewAmount = tradeInfo.targetItem->Amount();
            tradeInfo.sourceRollbackItem->SetChangeScope(ItemChangeScope_Added);
            tradeInfo.targetRollbackItem->SetChangeScope(ItemChangeScope_Decreased);
            return true;
        }

        if (tradeInfo.targetTradeType == 2) {
            CItem& sourceItem = *tradeInfo.sourceItem.get();
            tradeInfo.targetItem = targetWarehouse->MakeNewItem(
                targetWarehouse->m_ownerId, sourceItem.ItemType(), tradeInfo.amount, sourceItem.Enchant(),
                sourceItem.Eroded(), sourceItem.Bless(), sourceItem.Ident(), sourceItem.Wished(), WareHouseType_Inventory,
                sourceItem.OptionKey(), sourceItem.m_intensiveItemType, sourceItem.Attribute(), sourceItem.HLocked(),
                sourceItem.UseCount()
            );
            // if (!tradeInfo.targetItem)
                // return false
            if (!tradeInfo.targetItem) {
                // Log.Add(LOG_ERROR,
                //     "[trade] Cannot create a destination stack for owner %u from source item %u (owner %u), quantity %lld. Source quantity is already %lld in memory and the caller will attempt rollback.",
                //     targetWarehouse->m_ownerId, sourceItem.ItemID(), sourceWarehouse->m_ownerId,
                //     static_cast<long long>(tradeInfo.amount), static_cast<long long>(sourceItem.Amount()))
                L2_LOG_ERROR(Log,
                    "[trade] Cannot create a destination stack for owner %u from source item %u (owner %u), quantity %lld; source quantity is already %lld in memory and the caller will attempt rollback.",
                    targetWarehouse->m_ownerId, sourceItem.ItemID(), sourceWarehouse->m_ownerId,
                    static_cast<long long>(tradeInfo.amount), static_cast<long long>(sourceItem.Amount()));
                return false;
            }

            tradeInfo.targetItem->SetTransation();
            tradeInfo.targetRollbackItem = tradeInfo.targetItem->Copy();
            tradeInfo.targetRollbackItem->SetTrasactMode(TransactType_RollbackCreation);
            addTransactionPair(tradeInfo.targetItem, tradeInfo.targetRollbackItem);

            tradeInfo.sourceNewTradeType = 1;
            tradeInfo.targetNewTradeType = 2;
            tradeInfo.sourceResponseItemId = tradeInfo.sourceItemId;
            tradeInfo.targetResponseItemId = static_cast<int>(tradeInfo.targetItem->ItemID());
            tradeInfo.sourceResponseServerToken = tradeInfo.sourceServerToken;
            tradeInfo.targetResponseServerToken = tradeInfo.targetServerToken;
            tradeInfo.sourceNewAmount = tradeInfo.sourceItem->Amount();
            tradeInfo.targetNewAmount = tradeInfo.targetItem->Amount();
            tradeInfo.sourceRollbackItem->SetChangeScope(ItemChangeScope_Added);
            tradeInfo.targetRollbackItem->SetChangeScope(ItemChangeScope_Deleted);
            return true;
        }
        return false;
    }

    if (tradeInfo.sourceTradeType != 3)
        return false;

    if (tradeInfo.targetTradeType == 1) {
        tradeInfo.sourceItem->TransactDelete();
        tradeInfo.targetItem = targetWarehouse->GetItem(tradeInfo.targetItemId);
        if (!tradeInfo.targetItem) {
            // Log.Add(
            //     LOG_ERROR, "[%s][%d]cannot find item. pItemTarget = null by dbid[%d]", "WareHouse.cpp", 0x5704,
            //     tradeInfo.targetItemId
            // )
            // Log.Add(LOG_ERROR,
            //     "[trade] Destination item %d for owner %u disappeared after validation. Source item %u (owner %u) was already marked for deletion. Transfer failed. Caller will attempt rollback.",
            //     tradeInfo.targetItemId, targetWarehouse->m_ownerId, tradeInfo.sourceItem->ItemID(), sourceWarehouse->m_ownerId)
            L2_LOG_ERROR(Log,
                "[trade] Destination item %d for owner %u disappeared after validation; source item %u (owner %u) was already marked for deletion. Transfer failed; caller will attempt rollback.",
                tradeInfo.targetItemId, targetWarehouse->m_ownerId, tradeInfo.sourceItem->ItemID(), sourceWarehouse->m_ownerId);
            return false;
        }

        tradeInfo.targetItem->SetTransation();
        tradeInfo.targetRollbackItem = tradeInfo.targetItem->Copy();
        tradeInfo.targetRollbackItem->SetTrasactMode(TransactType_RollbackChanges);
        addTransactionPair(tradeInfo.targetItem, tradeInfo.targetRollbackItem);
        tradeInfo.targetItem->SetAmount(runtime::AddInt64Wrapping(tradeInfo.targetItem->Amount(), tradeInfo.amount));

        tradeInfo.sourceNewTradeType = 3;
        tradeInfo.targetNewTradeType = 1;
        tradeInfo.sourceResponseItemId = static_cast<int>(tradeInfo.sourceItem->ItemID());
        tradeInfo.targetResponseItemId = static_cast<int>(tradeInfo.targetItem->ItemID());
        tradeInfo.sourceResponseServerToken = tradeInfo.sourceServerToken;
        tradeInfo.targetResponseServerToken = tradeInfo.targetServerToken;
        tradeInfo.sourceNewAmount = 0;
        tradeInfo.targetNewAmount = tradeInfo.targetItem->Amount();
        tradeInfo.sourceRollbackItem->SetChangeScope(ItemChangeScope_Increased);
        tradeInfo.targetRollbackItem->SetChangeScope(ItemChangeScope_Decreased);
        return true;
    }

    if (tradeInfo.targetTradeType == 4) {
        if (tradeInfo.sourceItem->m_ownerId != targetWarehouse->m_ownerId) {
            tradeInfo.sourceItem->m_inventorySlotIndex = -1;
        }
        tradeInfo.sourceItem->m_ownerId = targetWarehouse->m_ownerId;

        tradeInfo.sourceNewTradeType = 3;
        tradeInfo.targetNewTradeType = 4;
        tradeInfo.sourceResponseItemId = 0;
        tradeInfo.targetResponseItemId = static_cast<int>(tradeInfo.sourceItem->ItemID());
        tradeInfo.sourceResponseServerToken = tradeInfo.sourceServerToken;
        tradeInfo.targetResponseServerToken = tradeInfo.targetServerToken;
        tradeInfo.sourceNewAmount = 0;
        tradeInfo.targetNewAmount = tradeInfo.sourceItem->Amount();
        tradeInfo.sourceRollbackItem->SetChangeScope(ItemChangeScope_Fee);
        return true;
    }

    return false;
}

bool CWareHouse::TradeItems(CSocket* socket, const std::uint8_t* packet, CWareHouse* targetWarehouse) {
    // Current Cached.exe 0x00619350.
    L2_GUARD_FUNCTION();

    if (!socket || !targetWarehouse)
        return false;

    const int sourceOwnerId = static_cast<int>(m_ownerId);
    const int targetOwnerId = static_cast<int>(targetWarehouse->m_ownerId);
    std::array<int, 2> affectedItemCounts{};
    std::array<int, 2> responseSizes{};
    std::array<std::array<char, 20480>, 2> responseBuffers{};
    std::array<std::int64_t, 2> tradeTotalPrices{};
    CItemTransactionOrderMap transactionItems;
    CItemTransactionOrderMap auditItems;
    std::set<unsigned int> tradedSourceItems;
    int auditOrder = 0;

    auto failTrade = [&]() {
        CTransaction::RollbackTransactOrderMap(transactionItems);
        socket->Send("cdddd", 0x28, sourceOwnerId, targetOwnerId, 0, 0);
        return false;
    };

    auto appendAuditPair = [&auditItems, &auditOrder](CItemSP& item, CItemSP& rollbackItem) {
        CItemSP itemCopy = item->Copy();
        CItemSP rollbackCopy = rollbackItem->Copy();
        L2_ADD_REF(itemCopy, ObjectRefType::FunctionLocal);
        L2_ADD_REF(rollbackCopy, ObjectRefType::FunctionLocal);
        itemCopy->SetChangeScope(item->ChangeScope());
        rollbackCopy->SetChangeScope(rollbackItem->ChangeScope());
        auditItems.emplace(++auditOrder, CItemKey(rollbackCopy.get(), itemCopy.get()));
    };

    const std::uint8_t* cursor = packet;
    for (int direction = 0; direction < 2; ++direction) {
        int itemCount = 0;
        cursor = ::Disassemble(cursor, "d", &itemCount);
        CWareHouse* sourceWarehouse = direction == 0 ? this : targetWarehouse;
        CWareHouse* destinationWarehouse = direction == 0 ? targetWarehouse : this;

        for (int index = 0; index < itemCount; ++index) {
            ItemTradeInfo tradeInfo;
            cursor = ::Disassemble(
                cursor, "ddddddQ", &tradeInfo.sourceTradeType, &tradeInfo.sourceItemId, &tradeInfo.sourceServerToken,
                &tradeInfo.targetTradeType, &tradeInfo.targetItemId, &tradeInfo.targetServerToken, &tradeInfo.amount
            );

            if (!_TransactTradeItems(
                    sourceWarehouse, destinationWarehouse, tradedSourceItems, transactionItems, tradeInfo
                )) {
                // Log.Add(LOG_ERROR, "[%s][%d] transact trade item fail", "WareHouse.cpp", 0x1b4a)
                // Log.Add(LOG_ERROR,
                //     "[trade] Exchange cancelled at item %d of %d from owner %u to owner %u. Attempting rollback of the exchange and sending a failure reply.",
                //     index + 1, itemCount, sourceWarehouse->m_ownerId, destinationWarehouse->m_ownerId)
                L2_LOG_ERROR(Log,
                    "[trade] Exchange cancelled at item %d of %d from owner %u to owner %u; attempting rollback of the exchange and sending a failure reply.",
                    index + 1, itemCount, sourceWarehouse->m_ownerId, destinationWarehouse->m_ownerId);
                return failTrade();
            }
            if (!tradeInfo.sourceItem)
                return failTrade();

            const std::int64_t itemPrice = g_readFile.GetDefaultPrice(static_cast<int>(tradeInfo.sourceItem->ItemType()));
            tradeTotalPrices[direction] =
                runtime::AddInt64Wrapping(tradeTotalPrices[direction], runtime::MultiplyInt64Wrapping(itemPrice, tradeInfo.amount));

            appendAuditPair(tradeInfo.sourceItem, tradeInfo.sourceRollbackItem);
            if (tradeInfo.sourceTradeType != 3 || tradeInfo.targetTradeType != 4) {
                appendAuditPair(tradeInfo.targetItem, tradeInfo.targetRollbackItem);
            }

            const int assembled = ::Assemble(
                responseBuffers[direction].data() + responseSizes[direction],
                static_cast<int>(responseBuffers[direction].size()) - responseSizes[direction], "dddQdddQ",
                tradeInfo.sourceNewTradeType, tradeInfo.sourceResponseItemId, tradeInfo.sourceResponseServerToken,
                tradeInfo.sourceNewAmount, tradeInfo.targetNewTradeType, tradeInfo.targetResponseItemId,
                tradeInfo.targetResponseServerToken, tradeInfo.targetNewAmount
            );
            if (assembled > 0) {
                responseSizes[direction] += assembled;
                ++affectedItemCounts[direction];
            }
        }
    }

    CUserSP sourceUser = g_userDb.GetUser(sourceOwnerId, true, false, runtime::process::CurrentDatabaseId());
    CUserSP targetUser = g_userDb.GetUser(targetOwnerId, true, false, runtime::process::CurrentDatabaseId());
    if (sourceUser && targetUser) {
        WriteTradeAggregateAudit(*sourceUser.get(), *targetUser.get(), tradeTotalPrices[0], tradeTotalPrices[1]);
    }

    socket->Send(
        "cdddbdb", 0x28, sourceOwnerId, targetOwnerId, affectedItemCounts[0], responseSizes[0], responseBuffers[0].data(),
        affectedItemCounts[1], responseSizes[1], responseBuffers[1].data()
    );

    auto transactionEntry = transactionItems.begin();
    auto auditEntry = auditItems.begin();
    while (transactionEntry != transactionItems.end() && auditEntry != auditItems.end()) {
        CItem* rollbackItem = transactionEntry->second.rollbackItem;
        CItem* item = transactionEntry->second.item;
        CItem* auditRollbackItem = auditEntry->second.rollbackItem;
        CItem* auditItem = auditEntry->second.item;

        const bool sourceOwned = auditRollbackItem->OwnerID() == m_ownerId;
        CUserSP actorUser =
            g_userDb.GetUser(sourceOwned ? sourceOwnerId : targetOwnerId, true, false, runtime::process::CurrentDatabaseId());
        CUserSP counterpartyUser =
            g_userDb.GetUser(sourceOwned ? targetOwnerId : sourceOwnerId, true, false, runtime::process::CurrentDatabaseId());
        WriteTradeItemAudit(
            MakeWareHouseAuditActor(actorUser.get()), MakeWareHouseAuditActor(counterpartyUser.get()), *auditItem,
            *auditRollbackItem
        );

        item->CommitTransation(rollbackItem, true);
        L2_RELEASE(item, ObjectRefType::FunctionLocal);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
        L2_RELEASE(auditItem, ObjectRefType::FunctionLocal);
        L2_RELEASE(auditRollbackItem, ObjectRefType::FunctionLocal);
        ++transactionEntry;
        ++auditEntry;
    }
    return true;
}

bool CWareHouse::TradeDivideAdena(CSocket* socket, const std::uint8_t* packet, CWareHouse* targetWarehouse) {
    // Current Cached.exe 0x006180fc.
    L2_GUARD_FUNCTION();

    const int sourceOwnerId = static_cast<int>(m_ownerId);
    const int targetOwnerId = static_cast<int>(targetWarehouse->m_ownerId);

    ItemTradeInfo tradeInfo;
    int requestValue = 0;
    char requestFlag = 0;
    ::Disassemble(
        packet, "ddddddQdc", &tradeInfo.sourceTradeType, &tradeInfo.sourceItemId, &tradeInfo.sourceServerToken,
        &tradeInfo.targetTradeType, &tradeInfo.targetItemId, &tradeInfo.targetServerToken, &tradeInfo.amount, &requestValue,
        &requestFlag
    );

    CItemTransactionOrderMap transactionItems;
    CItemTransactionOrderMap auditItems;
    std::set<unsigned int> tradedSourceItems;
    const bool transacted = _TransactTradeItems(this, targetWarehouse, tradedSourceItems, transactionItems, tradeInfo);
    if (!transacted) {
        CTransaction::RollbackTransactOrderMap(transactionItems);
        socket->Send("chddd", 0xc6, 0xf1, sourceOwnerId, targetOwnerId, 0);
        return false;
    }

    // Both assignments are present in the shipping body after the
    // transaction helper, even though its ordinary trade path calculated
    // the resulting stack amounts already.
    tradeInfo.sourceNewAmount = tradeInfo.amount;
    tradeInfo.targetNewAmount = tradeInfo.amount;

    std::array<char, 20480> response{};
    const int assembled = ::Assemble(
        response.data(), static_cast<int>(response.size()), "dddQdddQdQc", tradeInfo.sourceNewTradeType,
        tradeInfo.sourceResponseItemId, tradeInfo.sourceResponseServerToken, tradeInfo.sourceNewAmount,
        tradeInfo.targetNewTradeType, tradeInfo.targetResponseItemId, tradeInfo.targetResponseServerToken,
        tradeInfo.targetNewAmount, requestValue, tradeInfo.amount, static_cast<int>(requestFlag)
    );
    const int responseSize = assembled > 0 ? assembled : 0;

    auto appendAuditPair = [&auditItems](int order, CItemSP& item, CItemSP& rollbackItem) {
        CItemSP itemCopy = item->Copy();
        CItemSP rollbackCopy = rollbackItem->Copy();
        L2_ADD_REF(itemCopy, ObjectRefType::FunctionLocal);
        L2_ADD_REF(rollbackCopy, ObjectRefType::FunctionLocal);
        itemCopy->SetChangeScope(item->ChangeScope());
        rollbackCopy->SetChangeScope(rollbackItem->ChangeScope());
        auditItems.emplace(order, CItemKey(rollbackCopy.get(), itemCopy.get()));
    };
    appendAuditPair(1, tradeInfo.sourceItem, tradeInfo.sourceRollbackItem);
    appendAuditPair(2, tradeInfo.targetItem, tradeInfo.targetRollbackItem);

    CUserSP sourceUser = g_userDb.GetUser(sourceOwnerId, true, false, runtime::process::CurrentDatabaseId());
    CUserSP targetUser = g_userDb.GetUser(targetOwnerId, true, false, runtime::process::CurrentDatabaseId());
    if (sourceUser && targetUser) {
        WriteTradeDivideAdenaAudit(
            MakeWareHouseAuditActor(sourceUser.get()), MakeWareHouseAuditActor(targetUser.get()),
            *tradeInfo.sourceItem.get(), *tradeInfo.targetItem.get(), tradeInfo.amount
        );
    }

    socket->Send("chdddb", 0xc6, 0xf1, sourceOwnerId, targetOwnerId, 1, responseSize, response.data());

    auto transactionEntry = transactionItems.begin();
    auto auditEntry = auditItems.begin();
    while (transactionEntry != transactionItems.end() && auditEntry != auditItems.end()) {
        CItem* rollbackItem = transactionEntry->second.rollbackItem;
        CItem* item = transactionEntry->second.item;
        CItem* auditRollbackItem = auditEntry->second.rollbackItem;
        CItem* auditItem = auditEntry->second.item;

        const bool sourceOwned = auditRollbackItem->OwnerID() == m_ownerId;
        CUserSP actorUser =
            g_userDb.GetUser(sourceOwned ? sourceOwnerId : targetOwnerId, true, false, runtime::process::CurrentDatabaseId());
        CUserSP counterpartyUser =
            g_userDb.GetUser(sourceOwned ? targetOwnerId : sourceOwnerId, true, false, runtime::process::CurrentDatabaseId());
        // The shipping loop creates both actor snapshots but does not
        // submit another log record before committing this transaction.
        MakeWareHouseAuditActor(actorUser.get());
        MakeWareHouseAuditActor(counterpartyUser.get());

        item->CommitTransation(rollbackItem, true);
        L2_RELEASE(item, ObjectRefType::FunctionLocal);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
        L2_RELEASE(auditItem, ObjectRefType::FunctionLocal);
        L2_RELEASE(auditRollbackItem, ObjectRefType::FunctionLocal);
        ++transactionEntry;
        ++auditEntry;
    }
    return transacted;
}

void CWareHouse::UpdateMarketPrice(CItem* item, std::int64_t price) {
    // Current Cached.exe 0x005e7720.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    std::uint32_t itemType = item->ItemType();
    int enchant = item->Enchant();
    constexpr const wchar_t* statement = L"{Call lin_UpdateMarketPrice (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&itemType);
    connection.BindParameter(&enchant);
    connection.BindParameter(&price);
    // connection.ExecutePrepared(statement)
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR,
        //     "[private store] Market-price write failed for item class %u, enchant %d, price %lld, database %d. Item trade commit will still be attempted.",
        //     itemType, enchant, static_cast<long long>(price), runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[private store] Market-price write failed for item class %u, enchant %d, price %lld, database %d; item trade commit will still be attempted.",
            itemType, enchant, static_cast<long long>(price), runtime::process::CurrentDatabaseId());
    }
}

bool CWareHouse::PrivateStoreCommit(CSocket* socket, const std::uint8_t* packet, CWareHouse* purchaserWarehouse) {
    // Current Cached.exe 0x00615b30.
    L2_GUARD_FUNCTION();

    const int vendorOwnerId = static_cast<int>(m_ownerId);
    const int purchaserOwnerId = static_cast<int>(purchaserWarehouse->m_ownerId);
    std::int64_t totalPrice = 0;
    int itemCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "Qd", &totalPrice, &itemCount);

    std::array<char, 20480> itemResponse{};
    int itemResponseSize = 0;
    int affectedItemCount = 0;
    std::array<char, 20480> adenaResponse{};
    int adenaResponseSize = 0;
    int adenaResponseCount = 0;
    CItemTransactionOrderMap transactionItems;
    std::set<unsigned int> tradedSourceItems;

    auto failCommit = [&]() {
        CTransaction::RollbackTransactOrderMap(transactionItems);
        socket->Send("cdddd", 0x54, vendorOwnerId, purchaserOwnerId, 0, 0);
        return false;
    };

    auto addTransactionPair = [&transactionItems](CItemSP& item, CItemSP& rollbackItem) {
        L2_ADD_REF(item, ObjectRefType::FunctionLocal);
        L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
        transactionItems.emplace(static_cast<int>(transactionItems.size()) + 1, CItemKey(rollbackItem.get(), item.get()));
    };

    for (int index = 0; index < itemCount; ++index) {
        ItemTradeInfo tradeInfo;
        std::int64_t privateStorePrice = 0;
        cursor = ::Disassemble(
            cursor, "ddddddQQ", &tradeInfo.sourceTradeType, &tradeInfo.sourceItemId, &tradeInfo.sourceServerToken,
            &tradeInfo.targetTradeType, &tradeInfo.targetItemId, &tradeInfo.targetServerToken, &tradeInfo.amount,
            &privateStorePrice
        );

        if (!_TransactTradeItems(this, purchaserWarehouse, tradedSourceItems, transactionItems, tradeInfo)) {
            // Log.Add(LOG_ERROR, "[%s][%d] transact trade item fail", "WareHouse.cpp", 0x138b)
            // Log.Add(LOG_ERROR,
            //     "[private store] Purchase cancelled at item %d of %d from seller %u to buyer %u. Attempting rollback and sending a failure reply.",
            //     index + 1, itemCount, m_ownerId, purchaserWarehouse->m_ownerId)
            L2_LOG_ERROR(Log,
                "[private store] Purchase cancelled at item %d of %d from seller %u to buyer %u; attempting rollback and sending a failure reply.",
                index + 1, itemCount, m_ownerId, purchaserWarehouse->m_ownerId);
            return failCommit();
        }

        tradeInfo.sourceRollbackItem->SetPrivateStorePrice(privateStorePrice);
        if (tradeInfo.targetTradeType != 4) {
            tradeInfo.targetRollbackItem->SetPrivateStorePrice(privateStorePrice);
        }

        const int assembled = ::Assemble(
            itemResponse.data() + itemResponseSize, static_cast<int>(itemResponse.size()) - itemResponseSize, "dddQdddQQ",
            tradeInfo.sourceNewTradeType, tradeInfo.sourceResponseItemId, tradeInfo.sourceResponseServerToken,
            tradeInfo.sourceNewAmount, tradeInfo.targetNewTradeType, tradeInfo.targetResponseItemId,
            tradeInfo.targetResponseServerToken, tradeInfo.targetNewAmount, tradeInfo.amount
        );
        if (assembled > 0) {
            itemResponseSize += assembled;
            ++affectedItemCount;
        }
    }

    if (totalPrice > 0) {
        CItemSP purchaserAdena = purchaserWarehouse->GetAdena();
        if (!purchaserAdena) {
            // The current failure path attempts ItemID() after its null-SP
            // diagnostic. Zero is the value kept by the portable path.
            // Log.Add(LOG_ERROR, "[%s][%d]cannot find item. by dbid[%d]", "WareHouse.cpp", 0x13bc, 0)
            // Log.Add(LOG_WARN,
            //     "[private store] Buyer %u has no adena stack for payment %lld to seller %u. Attempting rollback of the purchase and sending a failure reply.",
            //     purchaserWarehouse->m_ownerId, static_cast<long long>(totalPrice), m_ownerId)
            L2_LOG_WARN(Log,
                "[private store] Buyer %u has no adena stack for payment %lld to seller %u; attempting rollback of the purchase and sending a failure reply.",
                purchaserWarehouse->m_ownerId, static_cast<long long>(totalPrice), m_ownerId);
            return failCommit();
        }

        purchaserAdena->SetTransation();
        CItemSP purchaserAdenaRollback = purchaserAdena->Copy();
        purchaserAdenaRollback->SetTrasactMode(TransactType_RollbackChanges);
        addTransactionPair(purchaserAdena, purchaserAdenaRollback);

        if (purchaserAdena->Amount() < totalPrice) {
            // Log.Add(
            //     LOG_ERROR, "[%s][%d] Try hack ? . dbid[%d]", "WareHouse.cpp", 0x13cb,
            //     static_cast<int>(purchaserAdena->ItemID())
            // )
            // Log.Add(LOG_WARN,
            //     "[private store] Buyer %u has %lld adena in item %u, but payment to seller %u requires %lld. Attempting rollback of the purchase and sending a failure reply.",
            //     purchaserWarehouse->m_ownerId, static_cast<long long>(purchaserAdena->Amount()), purchaserAdena->ItemID(), m_ownerId, static_cast<long long>(totalPrice))
            L2_LOG_WARN(Log,
                "[private store] Buyer %u has %lld adena in item %u, but payment to seller %u requires %lld; attempting rollback of the purchase and sending a failure reply.",
                purchaserWarehouse->m_ownerId, static_cast<long long>(purchaserAdena->Amount()), purchaserAdena->ItemID(), m_ownerId, static_cast<long long>(totalPrice));
            return failCommit();
        }

        int sourceTradeType = 0;
        int sourceItemId = 0;
        std::int64_t sourceNewAmount = 0;
        int targetTradeType = 0;
        int targetItemId = 0;
        std::int64_t targetNewAmount = 0;
        CItemSP vendorAdena;
        CItemSP vendorAdenaRollback;

        if (purchaserAdena->Amount() == totalPrice) {
            vendorAdena = GetAdena();
            if (!vendorAdena) {
                if (purchaserAdena->m_ownerId != m_ownerId) {
                    purchaserAdena->m_inventorySlotIndex = -1;
                }
                purchaserAdena->m_ownerId = m_ownerId;
                sourceTradeType = 3;
                targetTradeType = 4;
                targetItemId = static_cast<int>(purchaserAdena->ItemID());
                targetNewAmount = purchaserAdena->Amount();
                purchaserAdenaRollback->SetChangeScope(ItemChangeScope_Unknown);
            } else {
                purchaserAdena->TransactDelete();
                vendorAdena->SetTransation();
                vendorAdenaRollback = vendorAdena->Copy();
                vendorAdenaRollback->SetTrasactMode(TransactType_RollbackChanges);
                addTransactionPair(vendorAdena, vendorAdenaRollback);
                vendorAdena->SetAmount(runtime::AddInt64Wrapping(vendorAdena->Amount(), totalPrice));

                sourceTradeType = 3;
                sourceItemId = static_cast<int>(purchaserAdena->ItemID());
                targetTradeType = 1;
                targetItemId = static_cast<int>(vendorAdena->ItemID());
                targetNewAmount = vendorAdena->Amount();
                purchaserAdenaRollback->SetChangeScope(ItemChangeScope_Unknown);
                vendorAdenaRollback->SetChangeScope(ItemChangeScope_Unknown);
            }
        } else {
            purchaserAdena->SetAmount(runtime::SubtractInt64Wrapping(purchaserAdena->Amount(), totalPrice));
            vendorAdena = GetAdena();
            if (vendorAdena) {
                vendorAdena->SetTransation();
                vendorAdenaRollback = vendorAdena->Copy();
                vendorAdenaRollback->SetTrasactMode(TransactType_RollbackChanges);
                addTransactionPair(vendorAdena, vendorAdenaRollback);
                vendorAdena->SetAmount(runtime::AddInt64Wrapping(vendorAdena->Amount(), totalPrice));
                targetTradeType = 1;
            } else {
                vendorAdena = MakeNewItem(
                    m_ownerId, purchaserAdena->ItemType(), totalPrice, purchaserAdena->Enchant(), purchaserAdena->Eroded(),
                    purchaserAdena->Bless(), purchaserAdena->Ident(), purchaserAdena->Wished(), WareHouseType_Inventory,
                    purchaserAdena->OptionKey(), purchaserAdena->m_intensiveItemType, purchaserAdena->Attribute(),
                    purchaserAdena->HLocked(), purchaserAdena->UseCount()
                );
                // if (!vendorAdena)
                    // return failCommit()
                if (!vendorAdena) {
                    // Log.Add(LOG_ERROR,
                    //     "[private store] Cannot create the adena stack for seller %u, payment %lld from buyer %u. Buyer quantity is already %lld in memory. Attempting rollback of the purchase.",
                    //     m_ownerId, static_cast<long long>(totalPrice), purchaserWarehouse->m_ownerId,
                    //     static_cast<long long>(purchaserAdena->Amount()))
                    L2_LOG_ERROR(Log,
                        "[private store] Cannot create the adena stack for seller %u, payment %lld from buyer %u; buyer quantity is already %lld in memory. Attempting rollback of the purchase.",
                        m_ownerId, static_cast<long long>(totalPrice), purchaserWarehouse->m_ownerId,
                        static_cast<long long>(purchaserAdena->Amount()));
                    return failCommit();
                }

                vendorAdena->SetTransation();
                vendorAdenaRollback = vendorAdena->Copy();
                vendorAdenaRollback->SetTrasactMode(TransactType_RollbackCreation);
                addTransactionPair(vendorAdena, vendorAdenaRollback);
                targetTradeType = 2;
            }

            sourceTradeType = 1;
            sourceItemId = static_cast<int>(purchaserAdena->ItemID());
            sourceNewAmount = purchaserAdena->Amount();
            targetItemId = static_cast<int>(vendorAdena->ItemID());
            targetNewAmount = vendorAdena->Amount();
            purchaserAdenaRollback->SetChangeScope(ItemChangeScope_Unknown);
            vendorAdenaRollback->SetChangeScope(ItemChangeScope_Unknown);
        }

        const int assembled = ::Assemble(
            adenaResponse.data(), static_cast<int>(adenaResponse.size()), "dddQdddQQ", sourceTradeType, sourceItemId, 0,
            sourceNewAmount, targetTradeType, targetItemId, 0, targetNewAmount, static_cast<std::int64_t>(0)
        );
        if (assembled > 0) {
            adenaResponseSize = assembled;
            adenaResponseCount = 1;
        }
    }

    socket->Send(
        "cdddbdb", 0x54, vendorOwnerId, purchaserOwnerId, affectedItemCount, itemResponseSize, itemResponse.data(),
        adenaResponseCount, adenaResponseSize, adenaResponse.data()
    );

    const std::uint64_t adenaLogLimit = GetCacheDConfiguration().adenaLogLimit;
    for (const auto& entry : transactionItems) {
        CItem* rollbackItem = entry.second.rollbackItem;
        CItem* item = entry.second.item;
        const bool vendorOwned = rollbackItem->OwnerID() == m_ownerId;
        CUserSP actorUser = g_userDb.GetUser(
            vendorOwned ? vendorOwnerId : purchaserOwnerId, true, false, runtime::process::CurrentDatabaseId()
        );
        CUserSP counterpartyUser = g_userDb.GetUser(
            vendorOwned ? purchaserOwnerId : vendorOwnerId, true, false, runtime::process::CurrentDatabaseId()
        );
        const WareHouseAuditActor actor = MakeWareHouseAuditActor(actorUser.get());
        const WareHouseAuditActor counterparty = MakeWareHouseAuditActor(counterpartyUser.get());
        const std::int64_t privateStorePrice = rollbackItem->PrivateStorePrice();
        const std::int64_t liveAmount = item->Amount();
        const std::int64_t rollbackAmount = rollbackItem->Amount();
        auto writeLargePriceAudit = [&](std::int64_t amount) {
            if (privateStorePrice >= 0 && static_cast<std::uint64_t>(privateStorePrice) >= adenaLogLimit) {
                WritePrivateStoreLargePriceAudit(actor, counterparty, *item, amount, privateStorePrice);
            }
        };

        switch (rollbackItem->ChangeScope()) {
        case ItemChangeScope_Added: {
            const std::int64_t amount = runtime::SubtractInt64Wrapping(liveAmount, rollbackAmount);
            WriteTradeItemRecord(0x3a3, actor, counterparty, *item, amount, privateStorePrice);
            writeLargePriceAudit(amount);
            UpdateMarketPrice(item, privateStorePrice);
            break;
        }
        case ItemChangeScope_Increased: {
            const std::int64_t amount = runtime::NegateInt64Wrapping(liveAmount);
            WriteTradeItemRecord(0x3a3, actor, counterparty, *item, amount, privateStorePrice);
            writeLargePriceAudit(amount);
            UpdateMarketPrice(item, privateStorePrice);
            break;
        }
        case ItemChangeScope_Decreased:
            WriteTradeItemRecord(
                0x3a4, actor, counterparty, *item, runtime::SubtractInt64Wrapping(liveAmount, rollbackAmount), privateStorePrice
            );
            break;
        case ItemChangeScope_Deleted:
            WriteTradeItemRecord(0x3a4, actor, counterparty, *item, liveAmount, privateStorePrice);
            break;
        case ItemChangeScope_Fee: {
            const std::int64_t amount = runtime::NegateInt64Wrapping(liveAmount);
            WriteTradeItemRecord(0x3a3, actor, counterparty, *item, amount, privateStorePrice);
            writeLargePriceAudit(amount);
            UpdateMarketPrice(item, privateStorePrice);
            WriteTradeItemRecord(0x3a4, counterparty, actor, *item, liveAmount, privateStorePrice);
            break;
        }
        default:
            break;
        }

        item->CommitTransation(rollbackItem, true);
        L2_RELEASE(item, ObjectRefType::FunctionLocal);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
    }
    return true;
}

void CWareHouse::ManipulateItemTransactionFailed(
    CSocket* socket [[maybe_unused]],
    int responseId [[maybe_unused]],
    CItemTransactionOrderMap& transactionItems,
    std::vector<EtcMani*>& etcManis,
    int responseId2 [[maybe_unused]]
) {
    // Current Cached.exe 0x005d5b78. The socket and the two reply IDs are
    // part of the original member signature but the shipping body does not
    // read them.
    L2_GUARD_FUNCTION();

    CTransaction::RollbackTransactOrderMap(transactionItems);
    for (EtcMani* etcMani : etcManis) {
        if (etcMani->IsSuccess()) {
            etcMani->Rollback();
        }
        etcMani->Destroy();
    }
    etcManis.clear();
}

namespace {

// Current ManipulateItems keeps this information in a second ordered xmap
// keyed by the rollback object. Its original private mapped-type identifier
// is absent. The three mapped values are fixed by the insert at
// 0x00629f5c-0x00629fa7 and the find/read at 0x0062b081-0x0062b0c8.
struct ManipulateItemAuditState {
    std::uint32_t requestedItemId = 0;
    std::int64_t finalAmount = 0;
    ItemChangeScope changeScope = ItemChangeScope_Unknown;
};

bool ExecuteManipulateItemAttributeWrite(int itemId, const BaseAttribute& attribute) {
    // Current unnamed WareHouse.cpp call path at 0x0062915a uses the same
    // nine-parameter helper contract as Item.cpp/EtcMani.cpp.
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateUserItemAttribute (?, ?, ?, ?, ?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&itemId);
    connection.BindParameter(&attribute.m_nAttackType);
    connection.BindParameter(&attribute.m_nAttackValue);
    for (const short& value : attribute.m_nDefendValue) {
        connection.BindParameter(&value);
    }
    return connection.ExecutePrepared(statement);
}

bool ExecuteAddLottoItem(DBConn& connection, int round, int itemId, int numberFlag) {
    // Current anonymous DB helper 0x004335a0.
    constexpr const wchar_t* statement = L"{Call lin_AddLottoItem (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&round);
    connection.BindParameter(&itemId);
    connection.BindParameter(&numberFlag);
    return connection.ExecutePrepared(statement);
}

bool ExecuteCreateMonRaceTicket(
    DBConn& connection,
    int raceId,
    int betType,
    int firstSelection,
    int secondSelection,
    int thirdSelection,
    int betMoney,
    int taxMoney,
    int itemId,
    int remoteFee
) {
    // Current anonymous DB helper 0x00483dec.
    constexpr const wchar_t* statement = L"{Call lin_CreateMonRaceTicket (?, ?, ?, ?, ?, ?, ?, ?, ? )}";
    connection.Prepare(statement);
    connection.BindParameter(&raceId);
    connection.BindParameter(&betType);
    connection.BindParameter(&firstSelection);
    connection.BindParameter(&secondSelection);
    connection.BindParameter(&thirdSelection);
    connection.BindParameter(&betMoney);
    connection.BindParameter(&taxMoney);
    connection.BindParameter(&itemId);
    connection.BindParameter(&remoteFee);
    return connection.ExecutePrepared(statement);
}

int TargetInt32Percent(int percentage, int amount) noexcept {
    return runtime::MultiplyInt32Wrapping(
        percentage,
        amount) / 100;
}

void WriteManipulateEnsoulAudit(const WareHouseAuditActor& actor, CItem& item) {
    std::array<char16_t, 256> ensoulText{};
    // item.MakeEnsoulLogInfo(ensoulText.data())
    item.MakeEnsoulLogInfo(ensoulText.data(), ensoulText.size());
    const std::u16string ensoul = std::u16string(ensoulText.data());

    CLogFormatter formatter;
    formatter.WriteLog(
        0x2342, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z, ensoul.c_str(), nullptr, nullptr,
        actor.race, actor.gender, actor.characterClass, actor.level, 0, 0, 0, 0, 0, 0, actor.characterName.c_str(),
        actor.accountName.c_str(), nullptr, nullptr, item.ItemID()
    );
}

void WriteChangeAttributeItemAudits(
    const WareHouseAuditActor& actor,
    CItem& targetItem,
    CItem& sourceItem,
    int sourceItemDatabaseId,
    int targetItemDatabaseId,
    int targetIntensiveItemType,
    const std::u16string& oldTargetAttribute
) {
    // Current CWareHouse::ChangeAttributeItem 0x005e0d50 emits these four
    // records after committing both ordered transaction entries. The first
    // and third records use different formatter layouts.
    std::array<char16_t, 256> newTargetAttributeText{};
    std::array<wchar_t, 256> targetVariationText{};
    std::array<char16_t, 256> targetEnsoulText{};
    std::array<char16_t, 256> sourceShapeShiftText{};
    std::array<char16_t, 256> sourceEnsoulText{};

    BaseAttribute targetAttribute(targetItem.Attribute());
    targetAttribute.GetLogString(newTargetAttributeText.data());
    std::swprintf(
        targetVariationText.data(), targetVariationText.size(), L"%d:%d:%d", targetIntensiveItemType,
        targetItem.OptionKey().variationOption1, targetItem.OptionKey().variationOption2
    );
    // targetItem.MakeEnsoulLogInfo(targetEnsoulText.data())
    targetItem.MakeEnsoulLogInfo(targetEnsoulText.data(), targetEnsoulText.size());
    sourceItem.MakeShapeShiftLogInfo(sourceShapeShiftText.data());
    // sourceItem.MakeEnsoulLogInfo(sourceEnsoulText.data())
    sourceItem.MakeEnsoulLogInfo(sourceEnsoulText.data(), sourceEnsoulText.size());

    const std::u16string newTargetAttribute = std::u16string(newTargetAttributeText.data());
    const std::u16string targetVariation = WideToUtf16(targetVariationText.data());
    const std::u16string targetEnsoul = std::u16string(targetEnsoulText.data());
    const std::u16string sourceShapeShift = std::u16string(sourceShapeShiftText.data());
    const std::u16string sourceEnsoul = std::u16string(sourceEnsoulText.data());

    int targetCrystalType = -1;
    const CReadItemData* targetItemData = g_readFile.FindItem(static_cast<int>(targetItem.ItemType()));
    if (targetItemData) {
        targetCrystalType = static_cast<int>(targetItemData->m_crystalType);
    }

    CLogFormatter formatter;
    formatter.WriteLog(
        0x2330, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z, oldTargetAttribute.c_str(),
        newTargetAttribute.c_str(), targetVariation.c_str(), actor.race, actor.gender, actor.characterClass, actor.level,
        sourceItemDatabaseId, targetItem.ItemType(), sourceItem.ItemType(), targetCrystalType, targetItem.Enchant(), 0,
        actor.characterName.c_str(), actor.accountName.c_str(), nullptr, nullptr, targetItemDatabaseId
    );
    formatter.WriteLog(
        0x2349, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z, targetEnsoul.c_str(), nullptr, nullptr,
        actor.race, actor.gender, actor.characterClass, actor.level, 0, 0, 0, 0, 0, 0, actor.characterName.c_str(),
        actor.accountName.c_str(), nullptr, nullptr, targetItemDatabaseId
    );

    // The shipping route-6 call at 0x005e264f omits the formatter's two
    // target IDs, two trailing numeric fields and final target names. Its
    // second and third strings unexpectedly remain the changed target's
    // attribute and variation. Preserve that observable target quirk.
    std::array<std::u16string, 26> sourceFields{};
    sourceFields[0] = runtime::DecimalUtf16(0x38b);
    sourceFields[1] = runtime::DecimalUtf16(actor.characterId);
    sourceFields[2] = runtime::DecimalUtf16(actor.accountId);
    sourceFields[5] = runtime::DecimalUtf16(actor.x);
    sourceFields[6] = runtime::DecimalUtf16(actor.y);
    sourceFields[7] = runtime::DecimalUtf16(actor.z);
    sourceFields[8] = sourceShapeShift;
    sourceFields[9] = newTargetAttribute;
    sourceFields[10] = targetVariation;
    sourceFields[11] = runtime::DecimalUtf16(actor.race);
    sourceFields[12] = runtime::DecimalUtf16(actor.gender);
    sourceFields[13] = runtime::DecimalUtf16(actor.characterClass);
    sourceFields[14] = runtime::DecimalUtf16(actor.level);
    sourceFields[15] = runtime::DecimalUtf16(sourceItem.Enchant());
    sourceFields[16] = runtime::DecimalUtf16(sourceItem.ItemType());
    sourceFields[17] = runtime::DecimalUtf16(-1);
    sourceFields[18] = runtime::DecimalUtf16(sourceItem.Amount());
    sourceFields[21] = actor.characterName;
    sourceFields[22] = actor.accountName;
    sourceFields[25] = runtime::DecimalUtf16(sourceItem.ItemID());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(sourceFields, u','));

    formatter.WriteLog(
        0x2342, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z, sourceEnsoul.c_str(), nullptr, nullptr,
        actor.race, actor.gender, actor.characterClass, actor.level, 0, 0, 0, 0, 0, 0, actor.characterName.c_str(),
        actor.accountName.c_str(), nullptr, nullptr, sourceItem.ItemID()
    );
}

// Current EnchantItemFail call 0x005e6b36 emits this record immediately before
// committing the crystal-stack entry. This local helper does not claim a
// separate target owner: the shipping code is inline in WareHouse.cpp.
void WriteEnchantFailureCrystalAudit(const WareHouseAuditActor& actor, CItem& item, CItem& rollbackItem) {
    std::array<char16_t, 256> shapeShiftText{};
    std::array<char16_t, 256> baseAttributeText{};
    std::array<char16_t, 256> ensoulText{};
    item.MakeShapeShiftLogInfo(shapeShiftText.data());
    BaseAttribute baseAttribute(item.Attribute());
    baseAttribute.GetLogString(baseAttributeText.data());
    // The current body takes the ensoul text from the rollback copy, unlike
    // the shape-shift and base-attribute text taken from the changed item.
    // rollbackItem.MakeEnsoulLogInfo(ensoulText.data())
    rollbackItem.MakeEnsoulLogInfo(ensoulText.data(), ensoulText.size());

    const std::int64_t amountDifference = runtime::SubtractInt64Wrapping(item.Amount(), rollbackItem.Amount());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x38a, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z,
        std::u16string(shapeShiftText.data()).c_str(), std::u16string(baseAttributeText.data()).c_str(),
        std::u16string(ensoulText.data()).c_str(), actor.race, 7, actor.characterClass, actor.level, 0, item.ItemID(),
        item.Enchant(), item.ItemType(), amountDifference, item.Amount(), actor.characterName.c_str(),
        actor.accountName.c_str(), nullptr, nullptr, item.ItemID()
    );
}

// Current CWareHouse::EnchantItem and EnchantItemFail emit these records
// inline after committing their ordered item transactions. The two enchant
// records have different numeric-field layouts in the shipping
// machine code.
void WriteEnchantItemAudits(
    const WareHouseAuditActor& actor,
    CItem& targetItem,
    CItem* supportItem,
    CItem& scrollItem,
    int targetItemDatabaseId,
    int supportItemDatabaseId,
    int scrollItemDatabaseId,
    int previousEnchant,
    int newEnchant,
    int requestedTargetItemType,
    int successProbability,
    int intensiveItemType
) {
    std::array<char16_t, 256> shapeShiftText{};
    std::array<char16_t, 256> baseAttributeText{};
    std::array<wchar_t, 256> variationText{};
    std::array<char16_t, 256> ensoulText{};
    targetItem.MakeShapeShiftLogInfo(shapeShiftText.data());
    BaseAttribute baseAttribute(targetItem.Attribute());
    baseAttribute.GetLogString(baseAttributeText.data());
    std::swprintf(
        variationText.data(), variationText.size(), L"%d:%d:%d", intensiveItemType, targetItem.OptionKey().variationOption1,
        targetItem.OptionKey().variationOption2
    );
    // targetItem.MakeEnsoulLogInfo(ensoulText.data())
    targetItem.MakeEnsoulLogInfo(ensoulText.data(), ensoulText.size());

    // Current 0x005e3b21 calls CReadFile::FindItem and discards the returned
    // record. Keep the call because FindItem is not declared side-effect
    // free in the target and the optimizer kept it in Cached.exe.
    g_readFile.FindItem(static_cast<int>(targetItem.ItemType()));

    const std::u16string shapeShift = std::u16string(shapeShiftText.data());
    const std::u16string baseAttributeLog = std::u16string(baseAttributeText.data());
    const std::u16string variation = WideToUtf16(variationText.data());
    const std::u16string ensoul = std::u16string(ensoulText.data());
    const int supportItemType = supportItem ? static_cast<int>(supportItem->ItemType()) : 0;

    CLogFormatter formatter;
    if (newEnchant < 1) {
        // Current call at 0x005e3f07, log record 0x39e.
        formatter.WriteLog(
            0x39e, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z, shapeShift.c_str(),
            baseAttributeLog.c_str(), variation.c_str(), supportItemType, actor.gender, actor.characterClass,
            supportItemDatabaseId, scrollItemDatabaseId, scrollItem.ItemType(), previousEnchant, targetItem.ItemType(),
            requestedTargetItemType, successProbability, actor.characterName.c_str(), actor.accountName.c_str(), nullptr,
            nullptr, targetItemDatabaseId
        );
    } else {
        // Current call at 0x005e45e1, log record 0x39d.
        formatter.WriteLog(
            0x39d, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z, shapeShift.c_str(),
            baseAttributeLog.c_str(), variation.c_str(), supportItemType, supportItemDatabaseId, actor.characterClass,
            scrollItem.ItemType(), scrollItemDatabaseId, newEnchant, previousEnchant, targetItem.ItemType(),
            requestedTargetItemType, successProbability, actor.characterName.c_str(), actor.accountName.c_str(), nullptr,
            nullptr, targetItemDatabaseId
        );
    }

    formatter.WriteLog(
        newEnchant < 1 ? 0x2344 : 0x2343, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z,
        ensoul.c_str(), nullptr, nullptr, actor.race, actor.gender, actor.characterClass, actor.level, 0, 0, 0, 0, 0, 0,
        actor.characterName.c_str(), actor.accountName.c_str(), nullptr, nullptr, targetItemDatabaseId
    );

    if (newEnchant > 4) {
        // Current call at 0x005e4cfe bypasses CLogFormatter and writes the
        // sparse route-8 record directly through CLog::AddV.
        std::array<std::u16string, 26> fields{};
        fields[0] = runtime::DecimalUtf16(0x57c);
        fields[1] = runtime::DecimalUtf16(actor.characterId);
        fields[2] = runtime::DecimalUtf16(actor.accountId);
        fields[5] = runtime::DecimalUtf16(actor.x);
        fields[6] = runtime::DecimalUtf16(actor.y);
        fields[7] = runtime::DecimalUtf16(actor.z);
        fields[11] = runtime::DecimalUtf16(actor.race);
        fields[12] = runtime::DecimalUtf16(actor.gender);
        fields[13] = runtime::DecimalUtf16(actor.characterClass);
        fields[14] = runtime::DecimalUtf16(actor.level);
        fields[15] = runtime::DecimalUtf16(targetItem.ItemType());
        fields[16] = runtime::DecimalUtf16(newEnchant);
        fields[21] = actor.characterName;
        fields[22] = actor.accountName;
        fields[25] = runtime::DecimalUtf16(targetItemDatabaseId);
        runtime::process::WriteLogFormatterLine(8, runtime::JoinFields(fields, u','));
    }
}

void WriteDestroyItemAudit(
    const WareHouseAuditActor& actor,
    CItem& item,
    int intensiveItemType,
    std::int64_t requestedAmount,
    std::int64_t finalAmount
) {
    std::array<char16_t, 256> shapeShiftText{};
    std::array<char16_t, 256> baseAttributeText{};
    std::array<wchar_t, 256> variationText{};
    item.MakeShapeShiftLogInfo(shapeShiftText.data());
    BaseAttribute baseAttribute(item.Attribute());
    baseAttribute.GetLogString(baseAttributeText.data());
    std::swprintf(
        variationText.data(), variationText.size(), L"%d:%d:%d", intensiveItemType, item.OptionKey().variationOption1,
        item.OptionKey().variationOption2
    );

    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x38b);
    fields[1] = runtime::DecimalUtf16(actor.characterId);
    fields[2] = runtime::DecimalUtf16(actor.accountId);
    fields[5] = runtime::DecimalUtf16(actor.x);
    fields[6] = runtime::DecimalUtf16(actor.y);
    fields[7] = runtime::DecimalUtf16(actor.z);
    fields[8] = std::u16string(shapeShiftText.data());
    fields[9] = std::u16string(baseAttributeText.data());
    fields[10] = WideToUtf16(variationText.data());
    fields[11] = runtime::DecimalUtf16(actor.race);
    fields[12] = runtime::DecimalUtf16(actor.gender);
    fields[13] = runtime::DecimalUtf16(actor.characterClass);
    fields[14] = runtime::DecimalUtf16(actor.level);
    fields[17] = runtime::DecimalUtf16(item.Enchant());
    fields[18] = runtime::DecimalUtf16(item.ItemType());
    fields[19] = runtime::DecimalUtf16(runtime::NegateInt64Wrapping(requestedAmount));
    fields[20] = runtime::DecimalUtf16(finalAmount);
    fields[21] = actor.characterName;
    fields[22] = actor.accountName;
    fields[25] = runtime::DecimalUtf16(item.ItemID());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

// Current CWareHouse::CrystallizeItem stores one 0x20-byte value per
// successfully produced item row. The original local identifier is absent
// from the PDB. Offsets and field meanings are fixed by 0x005ecc29-0x005ed066.
struct CrystallizeItemChange {
    int itemId = 0; // +0x00
    int itemType = 0; // +0x04
    std::int64_t requestedAmount = 0; // +0x08
    std::int64_t resultingAmount = 0; // +0x10
    int created = 0; // +0x18
    int padding = 0; // +0x1c
};


struct CrystallizeAuditContext {
    WareHouseAuditActor actor;
    std::u16string shapeShift;
    std::u16string baseAttribute;
    std::u16string variation;
    std::u16string ensoul;
    int sourceItemType = 0;
    int sourceEnchant = 0;
};

CrystallizeAuditContext MakeCrystallizeAuditContext(CUser* user, CItem& sourceItem, int sourceIntensiveItemType) {
    CrystallizeAuditContext context;
    context.actor = MakeWareHouseAuditActor(user);
    context.sourceItemType = static_cast<int>(sourceItem.ItemType());
    context.sourceEnchant = sourceItem.Enchant();
    if (!user)
        return context;

    std::array<char16_t, 256> shapeShiftText{};
    std::array<char16_t, 256> baseAttributeText{};
    std::array<wchar_t, 256> variationText{};
    std::array<char16_t, 256> ensoulText{};
    sourceItem.MakeShapeShiftLogInfo(shapeShiftText.data());
    BaseAttribute baseAttribute(sourceItem.Attribute());
    baseAttribute.GetLogString(baseAttributeText.data());
    std::swprintf(
        variationText.data(), variationText.size(), L"%d:%d:%d", sourceIntensiveItemType,
        sourceItem.OptionKey().variationOption1, sourceItem.OptionKey().variationOption2
    );
    // sourceItem.MakeEnsoulLogInfo(ensoulText.data())
    sourceItem.MakeEnsoulLogInfo(ensoulText.data(), ensoulText.size());

    context.shapeShift = std::u16string(shapeShiftText.data());
    context.baseAttribute = std::u16string(baseAttributeText.data());
    context.variation = WideToUtf16(variationText.data());
    context.ensoul = std::u16string(ensoulText.data());
    return context;
}

void WriteCrystallizeItemAudits(const CrystallizeAuditContext& context, const CrystallizeItemChange& change) {
    const WareHouseAuditActor& actor = context.actor;

    // The first target call writes the route-6 CSV directly, retaining empty
    // target-id fields. It is not CLogFormatter::WriteLog, whose zero-valued
    // target fields would produce a different record.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x3a5);
    fields[1] = runtime::DecimalUtf16(actor.characterId);
    fields[2] = runtime::DecimalUtf16(actor.accountId);
    fields[5] = runtime::DecimalUtf16(actor.x);
    fields[6] = runtime::DecimalUtf16(actor.y);
    fields[7] = runtime::DecimalUtf16(actor.z);
    fields[8] = context.shapeShift;
    fields[9] = context.baseAttribute;
    fields[10] = context.variation;
    fields[11] = runtime::DecimalUtf16(actor.race);
    fields[12] = runtime::DecimalUtf16(actor.gender);
    fields[13] = runtime::DecimalUtf16(actor.characterClass);
    fields[14] = runtime::DecimalUtf16(actor.level);
    fields[15] = runtime::DecimalUtf16(change.itemType);
    fields[16] = runtime::DecimalUtf16(change.resultingAmount);
    fields[17] = runtime::DecimalUtf16(context.sourceEnchant);
    fields[18] = runtime::DecimalUtf16(context.sourceItemType);
    fields[19] = runtime::DecimalUtf16(1);
    fields[20] = runtime::DecimalUtf16(change.requestedAmount);
    fields[21] = actor.characterName;
    fields[22] = actor.accountName;
    fields[25] = runtime::DecimalUtf16(change.itemId);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));

    CLogFormatter formatter;
    formatter.WriteLog(
        0x2345, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z, context.ensoul.c_str(), nullptr,
        nullptr, actor.race, actor.gender, actor.characterClass, actor.level, 0, 0, 0, 0, 0, 0, actor.characterName.c_str(),
        actor.accountName.c_str(), nullptr, nullptr, change.itemId
    );

    if (change.requestedAmount < 20000)
        return;

    // Current 0x005ed8f5 omits shape-shift text from the audit
    // route while retaining base-attribute and variation strings.
    formatter.WriteAuditLog(
        0x5b4, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z, nullptr, context.baseAttribute.c_str(),
        context.variation.c_str(), actor.race, actor.gender, actor.characterClass, actor.level, change.itemType,
        change.resultingAmount, context.sourceEnchant, context.sourceItemType, 1, change.requestedAmount,
        actor.characterName.c_str(), actor.accountName.c_str(), nullptr, nullptr, change.itemId
    );
}

void WriteManipulateLargeAdenaAudit(
    const WareHouseAuditActor& actor, CItem& item, std::int64_t amount, std::int64_t finalAmount
) {
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x58b);
    fields[1] = runtime::DecimalUtf16(actor.characterId);
    fields[2] = runtime::DecimalUtf16(actor.accountId);
    fields[5] = runtime::DecimalUtf16(actor.x);
    fields[6] = runtime::DecimalUtf16(actor.y);
    fields[7] = runtime::DecimalUtf16(actor.z);
    fields[11] = runtime::DecimalUtf16(actor.race);
    fields[12] = runtime::DecimalUtf16(actor.gender);
    fields[13] = runtime::DecimalUtf16(actor.characterClass);
    fields[14] = runtime::DecimalUtf16(actor.level);
    fields[15] = runtime::DecimalUtf16(0);
    fields[17] = runtime::DecimalUtf16(item.Enchant());
    fields[18] = runtime::DecimalUtf16(item.ItemType());
    fields[19] = runtime::DecimalUtf16(amount);
    fields[20] = runtime::DecimalUtf16(finalAmount);
    fields[21] = actor.characterName;
    fields[22] = actor.accountName;
    fields[25] = runtime::DecimalUtf16(item.ItemID());
    runtime::process::WriteLogFormatterLine(8, runtime::JoinFields(fields, u','));
}

void WriteManipulateWatchAudit(const WareHouseAuditActor& actor, CItem& item, const CWatchItem& watchItem) {
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x57c);
    fields[1] = runtime::DecimalUtf16(actor.characterId);
    fields[2] = runtime::DecimalUtf16(actor.accountId);
    fields[5] = runtime::DecimalUtf16(actor.x);
    fields[6] = runtime::DecimalUtf16(actor.y);
    fields[7] = runtime::DecimalUtf16(actor.z);
    fields[8] = watchItem.m_strInformation;
    fields[11] = runtime::DecimalUtf16(actor.race);
    fields[12] = runtime::DecimalUtf16(actor.gender);
    fields[13] = runtime::DecimalUtf16(actor.characterClass);
    fields[14] = runtime::DecimalUtf16(actor.level);
    fields[15] = runtime::DecimalUtf16(item.ItemType());
    fields[16] = runtime::DecimalUtf16(item.Enchant());
    fields[21] = actor.characterName;
    fields[22] = actor.accountName;
    fields[25] = runtime::DecimalUtf16(item.ItemID());
    runtime::process::WriteLogFormatterLine(8, runtime::JoinFields(fields, u','));
}

void WriteManipulateItemAudit(
    const WareHouseAuditActor& actor,
    CItem& item,
    CItem& rollbackItem,
    const ManipulateItemAuditState& state,
    int intensiveItemType,
    bool premiumRelated
) {
    const ItemChangeScope scope = state.changeScope;
    const std::int64_t finalAmount = state.finalAmount;

    if (scope == ItemChangeScope_Added || scope == ItemChangeScope_Increased) {
        std::array<char16_t, 256> shapeShiftText{};
        std::array<char16_t, 256> baseAttributeText{};
        std::array<char16_t, 256> ensoulText{};
        item.MakeShapeShiftLogInfo(shapeShiftText.data());
        BaseAttribute baseAttribute(item.Attribute());
        baseAttribute.GetLogString(baseAttributeText.data());
        // item.MakeEnsoulLogInfo(ensoulText.data())
        item.MakeEnsoulLogInfo(ensoulText.data(), ensoulText.size());

        const std::int64_t amount =
            scope == ItemChangeScope_Added ? finalAmount : runtime::SubtractInt64Wrapping(finalAmount, rollbackItem.Amount());
        const std::u16string shapeShift = std::u16string(shapeShiftText.data());
        const std::u16string baseAttributeValue = std::u16string(baseAttributeText.data());
        const std::u16string ensoul = std::u16string(ensoulText.data());

        // Current 0x0062b281/0x0062b65d writes zero in the
        // second numeric actor field and keeps the request's original
        // item id in numeric field six.
        CLogFormatter formatter;
        formatter.WriteLog(
            0x38a, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z, shapeShift.c_str(),
            baseAttributeValue.c_str(), ensoul.c_str(), actor.race, 0, actor.characterClass, actor.level, 0,
            state.requestedItemId, item.Enchant(), item.ItemType(), amount, finalAmount, actor.characterName.c_str(),
            actor.accountName.c_str(), nullptr, nullptr, item.ItemID()
        );

        if (premiumRelated) {
            if (scope == ItemChangeScope_Added) {
                item.LogPremiumRelated(
                    static_cast<PremiumItemLogType>(0), 0, finalAmount, static_cast<PremiumItemLogReason>(1)
                );
            } else {
                item.LogPremiumRelated(
                    static_cast<PremiumItemLogType>(0), rollbackItem.Amount(), amount, static_cast<PremiumItemLogReason>(1)
                );
            }
        }

        if (item.ItemType() == 57 && amount >= 0 &&
            static_cast<std::uint64_t>(amount) >= GetCacheDConfiguration().adenaLogLimit) {
            WriteManipulateLargeAdenaAudit(actor, item, amount, finalAmount);
        }
        return;
    }

    if (scope != ItemChangeScope_Decreased && scope != ItemChangeScope_Deleted) {
        return;
    }

    CItem& shapeSource = scope == ItemChangeScope_Deleted ? rollbackItem : item;
    std::array<char16_t, 256> shapeShiftText{};
    std::array<char16_t, 256> baseAttributeText{};
    std::array<wchar_t, 256> variationText{};
    shapeSource.MakeShapeShiftLogInfo(shapeShiftText.data());
    BaseAttribute baseAttribute(item.Attribute());
    baseAttribute.GetLogString(baseAttributeText.data());
    std::swprintf(
        variationText.data(), variationText.size(), L"%d:%d:%d", intensiveItemType, item.OptionKey().variationOption1,
        item.OptionKey().variationOption2
    );

    const std::int64_t amount = scope == ItemChangeScope_Decreased ? runtime::SubtractInt64Wrapping(finalAmount, rollbackItem.Amount())
                                                                   : runtime::NegateInt64Wrapping(rollbackItem.Amount());
    const std::int64_t newAmount = scope == ItemChangeScope_Decreased ? finalAmount : 0;

    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x38b);
    fields[1] = runtime::DecimalUtf16(actor.characterId);
    fields[2] = runtime::DecimalUtf16(actor.accountId);
    fields[5] = runtime::DecimalUtf16(actor.x);
    fields[6] = runtime::DecimalUtf16(actor.y);
    fields[7] = runtime::DecimalUtf16(actor.z);
    fields[8] = std::u16string(shapeShiftText.data());
    fields[9] = std::u16string(baseAttributeText.data());
    fields[10] = WideToUtf16(variationText.data());
    fields[11] = runtime::DecimalUtf16(actor.race);
    fields[12] = runtime::DecimalUtf16(actor.gender);
    fields[13] = runtime::DecimalUtf16(actor.characterClass);
    fields[14] = runtime::DecimalUtf16(actor.level);
    fields[17] = runtime::DecimalUtf16(item.Enchant());
    fields[18] = runtime::DecimalUtf16(item.ItemType());
    fields[19] = runtime::DecimalUtf16(amount);
    fields[20] = runtime::DecimalUtf16(newAmount);
    fields[21] = actor.characterName;
    fields[22] = actor.accountName;
    fields[25] = runtime::DecimalUtf16(item.ItemID());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));

    WriteManipulateEnsoulAudit(actor, item);
    if (scope == ItemChangeScope_Deleted && premiumRelated) {
        item.LogPremiumRelated(
            static_cast<PremiumItemLogType>(1), rollbackItem.Amount(), 0, static_cast<PremiumItemLogReason>(1)
        );
    }
}

void SetManipulatePointAuditActor(
    std::array<std::u16string, 26>& fields, const WareHouseAuditActor& actor, bool classBeforeRace
) {
    fields[1] = runtime::DecimalUtf16(actor.characterId);
    fields[2] = runtime::DecimalUtf16(actor.accountId);
    fields[5] = runtime::DecimalUtf16(actor.x);
    fields[6] = runtime::DecimalUtf16(actor.y);
    fields[7] = runtime::DecimalUtf16(actor.z);
    if (classBeforeRace) {
        // Current records 0x1f43/0x1fba use class, race, gender. This is
        // different from the ordinary formatter layout.
        fields[11] = runtime::DecimalUtf16(actor.characterClass);
        fields[12] = runtime::DecimalUtf16(actor.race);
        fields[13] = runtime::DecimalUtf16(actor.gender);
    } else {
        fields[11] = runtime::DecimalUtf16(actor.race);
        fields[12] = runtime::DecimalUtf16(actor.gender);
        fields[13] = runtime::DecimalUtf16(actor.characterClass);
    }
    fields[14] = runtime::DecimalUtf16(actor.level);
    fields[21] = actor.characterName;
    fields[22] = actor.accountName;
}

std::int64_t ManipulatePointAuditAmount(CItem& item, CItem& rollbackItem) noexcept {
    // Current 0x0062e3f8 starts with the live amount and only rewrites it for
    // decrease/delete. In particular an increased stack logs the resulting
    // total, not the request delta.
    switch (item.ChangeScope()) {
    case ItemChangeScope_Decreased:
        return runtime::SubtractInt64Wrapping(item.Amount(), rollbackItem.Amount());
    case ItemChangeScope_Deleted:
        return runtime::NegateInt64Wrapping(rollbackItem.Amount());
    default:
        return item.Amount();
    }
}

void WriteManipulatePointPvpItemAudit(
    const WareHouseAuditActor& actor,
    CItem& item,
    CItem& rollbackItem,
    int lastGetReason,
    int intensiveItemType,
    std::int64_t auditAmount
) {
    // Current point-type 5 path in CWareHouse::ManipulatePointItems emits its
    // own inline 0x38a/0x38b/0x2342 records. It does not execute the generic
    // ManipulateItems premium, watch-list or large-adena side effects.
    const ItemChangeScope scope = item.ChangeScope();
    if (scope == ItemChangeScope_Added || scope == ItemChangeScope_Increased) {
        std::array<char16_t, 256> shapeShiftText{};
        std::array<char16_t, 256> baseAttributeText{};
        std::array<char16_t, 256> ensoulText{};
        item.MakeShapeShiftLogInfo(shapeShiftText.data());
        BaseAttribute baseAttribute(item.Attribute());
        baseAttribute.GetLogString(baseAttributeText.data());
        // item.MakeEnsoulLogInfo(ensoulText.data())
        item.MakeEnsoulLogInfo(ensoulText.data(), ensoulText.size());

        CLogFormatter formatter;
        formatter.WriteLog(
            0x38a, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z,
            std::u16string(shapeShiftText.data()).c_str(), std::u16string(baseAttributeText.data()).c_str(),
            std::u16string(ensoulText.data()).c_str(), actor.race, lastGetReason, actor.characterClass, actor.level, 0,
            rollbackItem.ItemID(), item.Enchant(), item.ItemType(), auditAmount, item.Amount(), actor.characterName.c_str(),
            actor.accountName.c_str(), nullptr, nullptr, item.ItemID()
        );
        return;
    }

    if (scope != ItemChangeScope_Decreased && scope != ItemChangeScope_Deleted) {
        return;
    }

    std::array<char16_t, 256> shapeShiftText{};
    std::array<char16_t, 256> baseAttributeText{};
    std::array<wchar_t, 256> variationText{};
    item.MakeShapeShiftLogInfo(shapeShiftText.data());
    BaseAttribute baseAttribute(item.Attribute());
    baseAttribute.GetLogString(baseAttributeText.data());
    std::swprintf(
        variationText.data(), variationText.size(), L"%d:%d:%d", intensiveItemType, item.OptionKey().variationOption1,
        item.OptionKey().variationOption2
    );

    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x38b);
    SetManipulatePointAuditActor(fields, actor, false);
    fields[8] = std::u16string(shapeShiftText.data());
    fields[9] = std::u16string(baseAttributeText.data());
    fields[10] = WideToUtf16(variationText.data());
    fields[17] = runtime::DecimalUtf16(item.Enchant());
    fields[18] = runtime::DecimalUtf16(item.ItemType());
    fields[19] = runtime::DecimalUtf16(auditAmount);
    fields[20] = runtime::DecimalUtf16(item.Amount());
    fields[25] = runtime::DecimalUtf16(item.ItemID());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    WriteManipulateEnsoulAudit(actor, item);
}

bool WriteManipulatePointItemAudits(
    int pointType,
    int pointDelta,
    int resultingPoint,
    int pledgeId,
    const std::u16string& pledgeName,
    const WareHouseAuditActor& actor,
    CItem& item,
    CItem& rollbackItem,
    int lastGetReason,
    int intensiveItemType
) {
    const std::int64_t auditAmount = ManipulatePointAuditAmount(item, rollbackItem);

    std::array<std::u16string, 26> fields{};
    switch (pointType) {
    case 1:
        fields[0] = runtime::DecimalUtf16(0x369);
        SetManipulatePointAuditActor(fields, actor, false);
        fields[16] = runtime::DecimalUtf16(item.Amount());
        fields[17] = runtime::DecimalUtf16(item.ItemType());
        fields[18] = runtime::DecimalUtf16(pointDelta);
        runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
        return false;

    case 2:
        fields[0] = runtime::DecimalUtf16(0x118);
        fields[1] = runtime::DecimalUtf16(pledgeId);
        fields[9] = pledgeName;
        fields[11] = runtime::DecimalUtf16(pointDelta);
        fields[12] = runtime::DecimalUtf16(item.ItemType());
        fields[13] = runtime::DecimalUtf16(auditAmount);
        fields[21] = actor.characterName;
        fields[22] = actor.accountName;
        runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
        return false;

    case 5:
        fields[0] = runtime::DecimalUtf16(pointDelta < 0 ? 0x1f43 : 0x1f42);
        SetManipulatePointAuditActor(fields, actor, true);
        if (pointDelta < 0) {
            fields[15] = runtime::DecimalUtf16(0);
            fields[16] = runtime::DecimalUtf16(pointDelta);
            fields[17] = runtime::DecimalUtf16(resultingPoint);
            fields[18] = runtime::DecimalUtf16(auditAmount);
            fields[19] = runtime::DecimalUtf16(item.ItemType());
        } else {
            fields[15] = runtime::DecimalUtf16(3);
            fields[16] = runtime::DecimalUtf16(7);
            fields[17] = runtime::DecimalUtf16(pointDelta);
            fields[18] = runtime::DecimalUtf16(resultingPoint);
        }
        runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
        WriteManipulatePointPvpItemAudit(actor, item, rollbackItem, lastGetReason, intensiveItemType, auditAmount);
        return true;

    case 8:
        fields[0] = runtime::DecimalUtf16(pointDelta < 0 ? 0x1fba : 0x36b);
        SetManipulatePointAuditActor(fields, actor, pointDelta < 0);
        fields[15] = runtime::DecimalUtf16(pointDelta < 0 ? 0 : 3);
        fields[16] = runtime::DecimalUtf16(pointDelta);
        fields[17] = runtime::DecimalUtf16(resultingPoint);
        if (pointDelta < 0) {
            fields[18] = runtime::DecimalUtf16(auditAmount);
            fields[19] = runtime::DecimalUtf16(item.ItemType());
        }
        runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
        return true;

    default:
        return false;
    }
}

void WriteManipulatePointWithoutItemAudit(
    int pointType, int pointDelta, int resultingPoint, const WareHouseAuditActor& actor
) {
    if (pointType != 5 && pointType != 8)
        return;

    const bool negative = pointDelta < 0;
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(pointType == 5 ? (negative ? 0x1f43 : 0x1f42) : (negative ? 0x1fba : 0x36b));
    SetManipulatePointAuditActor(fields, actor, negative);
    fields[15] = runtime::DecimalUtf16(negative ? 2 : 4);
    fields[16] = runtime::DecimalUtf16(pointDelta);
    if (pointType == 5) {
        if (!negative)
            fields[16] = runtime::DecimalUtf16(7);
        fields[17] = runtime::DecimalUtf16(negative ? resultingPoint : pointDelta);
        if (!negative) {
            fields[18] = runtime::DecimalUtf16(resultingPoint);
        }
    } else {
        fields[17] = runtime::DecimalUtf16(8);
    }
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

// Current CWareHouse::RecipeMakeItem 0x0061b4e0 emits these records inline.
// Its get-item record writes numeric value 5 in CSV field 12.
// the otherwise similar ManipulateItems owner writes zero there. Keep this
// owner separate so that distinction cannot be hidden by a generic helper.
void WriteRecipeMakeItemAudit(
    const WareHouseAuditActor& actor, CItem& item, CItem& rollbackItem, int requestedItemId, int intensiveItemType
) {
    const ItemChangeScope scope = item.ChangeScope();

    if (scope == ItemChangeScope_Added || scope == ItemChangeScope_Increased) {
        std::array<char16_t, 256> shapeShiftText{};
        std::array<char16_t, 256> baseAttributeText{};
        std::array<char16_t, 256> ensoulText{};
        item.MakeShapeShiftLogInfo(shapeShiftText.data());
        BaseAttribute baseAttribute(item.Attribute());
        baseAttribute.GetLogString(baseAttributeText.data());
        // item.MakeEnsoulLogInfo(ensoulText.data())
        item.MakeEnsoulLogInfo(ensoulText.data(), ensoulText.size());

        const std::int64_t amount =
            scope == ItemChangeScope_Added ? item.Amount() : runtime::SubtractInt64Wrapping(item.Amount(), rollbackItem.Amount());
        CLogFormatter formatter;
        formatter.WriteLog(
            0x38a, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z,
            std::u16string(shapeShiftText.data()).c_str(), std::u16string(baseAttributeText.data()).c_str(),
            std::u16string(ensoulText.data()).c_str(), actor.race, 5, actor.characterClass, actor.level, 0, requestedItemId,
            item.Enchant(), item.ItemType(), amount, item.Amount(), actor.characterName.c_str(), actor.accountName.c_str(),
            nullptr, nullptr, item.ItemID()
        );

        if (item.ItemType() == 57U && amount >= 0 &&
            static_cast<std::uint64_t>(amount) >= GetCacheDConfiguration().adenaLogLimit) {
            WriteManipulateLargeAdenaAudit(actor, item, amount, item.Amount());
        }
        return;
    }

    if (scope != ItemChangeScope_Decreased && scope != ItemChangeScope_Deleted) {
        return;
    }

    std::array<char16_t, 256> shapeShiftText{};
    std::array<char16_t, 256> baseAttributeText{};
    std::array<wchar_t, 256> variationText{};
    item.MakeShapeShiftLogInfo(shapeShiftText.data());
    BaseAttribute baseAttribute(item.Attribute());
    baseAttribute.GetLogString(baseAttributeText.data());
    std::swprintf(
        variationText.data(), variationText.size(), L"%d:%d:%d", intensiveItemType, item.OptionKey().variationOption1,
        item.OptionKey().variationOption2
    );

    const std::int64_t amount = scope == ItemChangeScope_Decreased ? runtime::SubtractInt64Wrapping(item.Amount(), rollbackItem.Amount())
                                                                   : runtime::NegateInt64Wrapping(rollbackItem.Amount());
    const std::int64_t finalAmount = scope == ItemChangeScope_Decreased ? item.Amount() : 0;

    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x38b);
    fields[1] = runtime::DecimalUtf16(actor.characterId);
    fields[2] = runtime::DecimalUtf16(actor.accountId);
    fields[5] = runtime::DecimalUtf16(actor.x);
    fields[6] = runtime::DecimalUtf16(actor.y);
    fields[7] = runtime::DecimalUtf16(actor.z);
    fields[8] = std::u16string(shapeShiftText.data());
    fields[9] = std::u16string(baseAttributeText.data());
    fields[10] = WideToUtf16(variationText.data());
    fields[11] = runtime::DecimalUtf16(actor.race);
    fields[12] = runtime::DecimalUtf16(actor.gender);
    fields[13] = runtime::DecimalUtf16(actor.characterClass);
    fields[14] = runtime::DecimalUtf16(actor.level);
    fields[17] = runtime::DecimalUtf16(item.Enchant());
    fields[18] = runtime::DecimalUtf16(item.ItemType());
    fields[19] = runtime::DecimalUtf16(amount);
    fields[20] = runtime::DecimalUtf16(finalAmount);
    fields[21] = actor.characterName;
    fields[22] = actor.accountName;
    fields[25] = runtime::DecimalUtf16(item.ItemID());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));

    WriteManipulateEnsoulAudit(actor, item);
}

// Current CWareHouse::GetPackage 0x005f3b34 emits the package-fee record
// inline before replying to L2Server. Keep this as source-local formatting,
// not as a target class that does not survive in the current PDB.
void WritePackageFeeAudit(const WareHouseAuditActor& actor, CItem& adenaItem, std::int64_t feeAmount) {
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x3b3);
    fields[1] = runtime::DecimalUtf16(actor.characterId);
    fields[2] = runtime::DecimalUtf16(actor.accountId);
    fields[5] = runtime::DecimalUtf16(actor.x);
    fields[6] = runtime::DecimalUtf16(actor.y);
    fields[7] = runtime::DecimalUtf16(actor.z);
    fields[11] = runtime::DecimalUtf16(actor.race);
    fields[12] = runtime::DecimalUtf16(actor.gender);
    fields[13] = runtime::DecimalUtf16(actor.characterClass);
    fields[14] = runtime::DecimalUtf16(actor.level);
    fields[17] = runtime::DecimalUtf16(adenaItem.Enchant());
    fields[18] = runtime::DecimalUtf16(adenaItem.ItemType());
    fields[19] = runtime::DecimalUtf16(feeAmount);
    fields[20] = runtime::DecimalUtf16(adenaItem.Amount());
    fields[21] = actor.characterName;
    fields[22] = actor.accountName;
    fields[25] = runtime::DecimalUtf16(adenaItem.ItemID());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

// The current GetPackage commit loop writes route-6 record 0x3b2 and event
// 0x2346 only for source-side scopes Added, Decreased and Deleted. The
// destination character name occupies the first free-form field of 0x3b2.
// it is not a second actor/target identity in CLogFormatter's name slots.
void WritePackageItemAudits(
    const WareHouseAuditActor& actor, const std::u16string& destinationCharacterName, CItem& item, int npcId
) {
    const ItemChangeScope scope = item.ChangeScope();
    if (scope != ItemChangeScope_Added && scope != ItemChangeScope_Decreased && scope != ItemChangeScope_Deleted) {
        return;
    }

    std::array<char16_t, 256> shapeShiftText{};
    std::array<char16_t, 256> baseAttributeText{};
    std::array<char16_t, 256> ensoulText{};
    item.MakeShapeShiftLogInfo(shapeShiftText.data());
    BaseAttribute baseAttribute(item.Attribute());
    baseAttribute.GetLogString(baseAttributeText.data());
    // item.MakeEnsoulLogInfo(ensoulText.data())
    item.MakeEnsoulLogInfo(ensoulText.data(), ensoulText.size());

    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x3b2);
    fields[1] = runtime::DecimalUtf16(actor.characterId);
    fields[2] = runtime::DecimalUtf16(actor.accountId);
    fields[5] = runtime::DecimalUtf16(actor.x);
    fields[6] = runtime::DecimalUtf16(actor.y);
    fields[7] = runtime::DecimalUtf16(actor.z);
    fields[8] = destinationCharacterName;
    fields[9] = std::u16string(baseAttributeText.data());
    fields[10] = std::u16string(shapeShiftText.data());
    fields[11] = runtime::DecimalUtf16(actor.race);
    fields[12] = runtime::DecimalUtf16(actor.gender);
    fields[13] = runtime::DecimalUtf16(actor.characterClass);
    fields[14] = runtime::DecimalUtf16(actor.level);
    fields[15] = runtime::DecimalUtf16(npcId);
    fields[17] = runtime::DecimalUtf16(item.Enchant());
    fields[18] = runtime::DecimalUtf16(item.ItemType());
    fields[19] = runtime::DecimalUtf16(item.AuditAmount());
    fields[20] = runtime::DecimalUtf16(item.Amount());
    fields[21] = actor.characterName;
    fields[22] = actor.accountName;
    fields[25] = runtime::DecimalUtf16(item.ItemID());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));

    const std::u16string ensoul = std::u16string(ensoulText.data());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x2346, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z, ensoul.c_str(), nullptr, nullptr,
        actor.race, actor.gender, actor.characterClass, actor.level, npcId, 0, 0, 0, 0, 0, actor.characterName.c_str(),
        actor.accountName.c_str(), nullptr, nullptr, item.ItemID()
    );
}

void WritePickItemLargeAdenaAudit(
    const WareHouseAuditActor& actor,
    int requestContext,
    int resultEnchant,
    int itemType,
    std::int64_t requestedAmount,
    std::int64_t resultAmount,
    int resultItemId
) {
    // Current CWareHouse::PickItem 0x005f3814 writes this record directly to
    // route 8. In particular, requestContext occupies numeric field five.
    // the similarly shaped generic manipulation audit writes zero there.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x58b);
    fields[1] = runtime::DecimalUtf16(actor.characterId);
    fields[2] = runtime::DecimalUtf16(actor.accountId);
    fields[5] = runtime::DecimalUtf16(actor.x);
    fields[6] = runtime::DecimalUtf16(actor.y);
    fields[7] = runtime::DecimalUtf16(actor.z);
    fields[11] = runtime::DecimalUtf16(actor.race);
    fields[12] = runtime::DecimalUtf16(actor.gender);
    fields[13] = runtime::DecimalUtf16(actor.characterClass);
    fields[14] = runtime::DecimalUtf16(actor.level);
    fields[15] = runtime::DecimalUtf16(requestContext);
    fields[17] = runtime::DecimalUtf16(resultEnchant);
    fields[18] = runtime::DecimalUtf16(itemType);
    fields[19] = runtime::DecimalUtf16(requestedAmount);
    fields[20] = runtime::DecimalUtf16(resultAmount);
    fields[21] = actor.characterName;
    fields[22] = actor.accountName;
    fields[25] = runtime::DecimalUtf16(resultItemId);
    runtime::process::WriteLogFormatterLine(8, runtime::JoinFields(fields, u','));
}

void WritePickItemAudits(
    const WareHouseAuditActor& actor,
    CItem* sourceItem,
    CItem* resultItem,
    int requestContext,
    int requestedItemId,
    int itemType,
    std::int64_t requestedAmount,
    int resultItemId,
    std::int64_t resultAmount,
    int resultEnchant
) {
    // The current owner uses the loaded world item for shape-shift and ensoul
    // text. For the base attribute it falls back to the resulting inventory
    // item when no database-backed source object existed.
    std::array<char16_t, 256> shapeShiftText{};
    std::array<char16_t, 256> baseAttributeText{};
    std::array<char16_t, 256> ensoulText{};
    if (sourceItem) {
        sourceItem->MakeShapeShiftLogInfo(shapeShiftText.data());
        // sourceItem->MakeEnsoulLogInfo(ensoulText.data())
        sourceItem->MakeEnsoulLogInfo(ensoulText.data(), ensoulText.size());
    }
    CItem* const attributeItem = sourceItem ? sourceItem : resultItem;
    if (attributeItem) {
        BaseAttribute attribute(attributeItem->Attribute());
        attribute.GetLogString(baseAttributeText.data());
    }

    const std::u16string shapeShift = std::u16string(shapeShiftText.data());
    const std::u16string baseAttribute = std::u16string(baseAttributeText.data());
    const std::u16string ensoul = std::u16string(ensoulText.data());

    CLogFormatter formatter;
    formatter.WriteLog(
        0x38a, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z, shapeShift.c_str(),
        baseAttribute.c_str(), ensoul.c_str(), actor.race, 6, actor.characterClass, actor.level, requestContext,
        requestedItemId, resultEnchant, itemType, requestedAmount, resultAmount, actor.characterName.c_str(),
        actor.accountName.c_str(), nullptr, nullptr, resultItemId
    );

    if (itemType == 57 && requestedAmount >= 0 &&
        static_cast<std::uint64_t>(requestedAmount) >= GetCacheDConfiguration().adenaLogLimit) {
        WritePickItemLargeAdenaAudit(
            actor, requestContext, resultEnchant, itemType, requestedAmount, resultAmount, resultItemId
        );
    }

    // Current 0x005f3821 checks the original database ID, not whether item
    // creation succeeded. Failed creation therefore still produces this
    // audit when the requested amount reaches 500.
    if (requestedItemId == 0 && itemType != 57 && requestedAmount >= 500) {
        formatter.WriteAuditLog(
            0x5b3, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z, nullptr, nullptr, nullptr,
            actor.race, actor.gender, actor.characterClass, actor.level, requestContext, resultEnchant, itemType,
            requestedAmount, resultAmount, 0, actor.characterName.c_str(), actor.accountName.c_str(), nullptr, nullptr,
            resultItemId
        );
    }
}

void WriteLottoManipulateAudit(const WareHouseAuditActor& actor, int round, int numberFlag, int totalCount, int itemId) {
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x3c3);
    fields[1] = runtime::DecimalUtf16(actor.characterId);
    fields[2] = runtime::DecimalUtf16(actor.accountId);
    fields[5] = runtime::DecimalUtf16(actor.x);
    fields[6] = runtime::DecimalUtf16(actor.y);
    fields[7] = runtime::DecimalUtf16(actor.z);
    fields[11] = runtime::DecimalUtf16(actor.race);
    fields[12] = runtime::DecimalUtf16(actor.gender);
    fields[13] = runtime::DecimalUtf16(actor.characterClass);
    fields[14] = runtime::DecimalUtf16(actor.level);
    fields[15] = runtime::DecimalUtf16(round);
    fields[16] = runtime::DecimalUtf16(numberFlag);
    fields[17] = runtime::DecimalUtf16(totalCount);
    fields[21] = actor.characterName;
    fields[22] = actor.accountName;
    fields[25] = runtime::DecimalUtf16(itemId);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteMonRaceManipulateAudit(
    const WareHouseAuditActor& actor, int raceId, int betType, int selection, int betMoney, int itemId
) {
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x3c5);
    fields[1] = runtime::DecimalUtf16(actor.characterId);
    fields[2] = runtime::DecimalUtf16(actor.accountId);
    fields[5] = runtime::DecimalUtf16(actor.x);
    fields[6] = runtime::DecimalUtf16(actor.y);
    fields[7] = runtime::DecimalUtf16(actor.z);
    fields[11] = runtime::DecimalUtf16(actor.race);
    fields[12] = runtime::DecimalUtf16(actor.gender);
    fields[13] = runtime::DecimalUtf16(actor.characterClass);
    fields[14] = runtime::DecimalUtf16(actor.level);
    fields[15] = runtime::DecimalUtf16(raceId);
    fields[16] = runtime::DecimalUtf16(betType);
    fields[17] = runtime::DecimalUtf16(selection);
    fields[19] = runtime::DecimalUtf16(betMoney);
    fields[21] = actor.characterName;
    fields[22] = actor.accountName;
    fields[25] = runtime::DecimalUtf16(itemId);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteMonRaceTicketRedemptionAudit(
    const WareHouseAuditActor& actor,
    int raceId,
    int betType,
    int selection,
    std::int64_t betMoney,
    std::int64_t reward,
    unsigned int itemId
) {
    // Current CWareHouse::DelMonRaceTicket 0x005f5c00 emits route-6 record
    // 0x3c6 after deleting the database ticket. Its 26 CSV fields are fixed
    // by the direct CLogFormatter call at 0x005f60a3.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x3c6);
    fields[1] = runtime::DecimalUtf16(actor.characterId);
    fields[2] = runtime::DecimalUtf16(actor.accountId);
    fields[5] = runtime::DecimalUtf16(actor.x);
    fields[6] = runtime::DecimalUtf16(actor.y);
    fields[7] = runtime::DecimalUtf16(actor.z);
    fields[11] = runtime::DecimalUtf16(actor.race);
    fields[12] = runtime::DecimalUtf16(actor.gender);
    fields[13] = runtime::DecimalUtf16(actor.characterClass);
    fields[14] = runtime::DecimalUtf16(actor.level);
    fields[15] = runtime::DecimalUtf16(raceId);
    fields[16] = runtime::DecimalUtf16(betType);
    fields[17] = runtime::DecimalUtf16(selection);
    fields[19] = runtime::DecimalUtf16(betMoney);
    fields[20] = runtime::DecimalUtf16(reward);
    fields[21] = actor.characterName;
    fields[22] = actor.accountName;
    fields[25] = runtime::DecimalUtf16(itemId);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

// Shape-shifting is implemented inline in the four shipping WareHouse.cpp
// owners. These source-local carriers only factor the repeated formatter
// setup. They do not add a target-visible class or API.
struct ShapeShiftAuditText {
    std::u16string shape;
    std::u16string attribute;
    std::u16string variation;
    std::u16string ensoul;
};

ShapeShiftAuditText MakeShapeShiftAuditText(CItem& item, int intensiveItemType) {
    std::array<char16_t, 256> shape{};
    std::array<char16_t, 256> attribute{};
    std::array<wchar_t, 256> variation{};
    std::array<char16_t, 256> ensoul{};
    item.MakeShapeShiftLogInfo(shape.data());
    BaseAttribute baseAttribute(item.Attribute());
    baseAttribute.GetLogString(attribute.data());
    std::swprintf(
        variation.data(), variation.size(), L"%d:%d:%d", intensiveItemType, item.OptionKey().variationOption1,
        item.OptionKey().variationOption2
    );
    // item.MakeEnsoulLogInfo(ensoul.data())
    item.MakeEnsoulLogInfo(ensoul.data(), ensoul.size());
    return ShapeShiftAuditText{std::u16string(shape.data()), std::u16string(attribute.data()), WideToUtf16(variation.data()),
        std::u16string(ensoul.data())};
}

void WriteShapeShiftMainAudit(
    int logId,
    const WareHouseAuditActor& actor,
    CItem& targetItem,
    const ShapeShiftAuditText& text,
    std::int64_t num5,
    std::int64_t num6,
    std::int64_t num7,
    std::int64_t num8,
    std::int64_t num9,
    std::int64_t num10
) {
    CLogFormatter formatter;
    formatter.WriteLog(
        logId, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z, text.shape.c_str(),
        text.attribute.c_str(), text.variation.c_str(), actor.race, actor.gender, actor.characterClass, actor.level, num5,
        num6, num7, num8, num9, num10, actor.characterName.c_str(), actor.accountName.c_str(), nullptr, nullptr,
        targetItem.ItemID()
    );
}

void WriteShapeShiftEnsoulAudit(
    int logId, const WareHouseAuditActor& actor, CItem& item, const std::u16string& ensoul, std::int64_t num5 = 0
) {
    CLogFormatter formatter;
    formatter.WriteLog(
        logId, actor.characterId, actor.accountId, 0, 0, actor.x, actor.y, actor.z, ensoul.c_str(), nullptr, nullptr,
        actor.race, actor.gender, actor.characterClass, actor.level, num5, 0, 0, 0, 0, 0, actor.characterName.c_str(),
        actor.accountName.c_str(), nullptr, nullptr, item.ItemID()
    );
}

void WriteShapeShiftRawItemAudit(
    const WareHouseAuditActor& actor,
    CItem& item,
    const ShapeShiftAuditText& text,
    std::int64_t amount,
    std::int64_t remainingAmount
) {
    // The target uses CLog::AddV for 0x38b. Its num5/num6 fields are empty,
    // not numeric zeroes, so the explicit 26-column carrier is required.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x38b);
    fields[1] = runtime::DecimalUtf16(actor.characterId);
    fields[2] = runtime::DecimalUtf16(actor.accountId);
    fields[5] = runtime::DecimalUtf16(actor.x);
    fields[6] = runtime::DecimalUtf16(actor.y);
    fields[7] = runtime::DecimalUtf16(actor.z);
    fields[8] = text.shape;
    fields[9] = text.attribute;
    fields[10] = text.variation;
    fields[11] = runtime::DecimalUtf16(actor.race);
    fields[12] = runtime::DecimalUtf16(actor.gender);
    fields[13] = runtime::DecimalUtf16(actor.characterClass);
    fields[14] = runtime::DecimalUtf16(actor.level);
    fields[17] = runtime::DecimalUtf16(item.Enchant());
    fields[18] = runtime::DecimalUtf16(item.ItemType());
    fields[19] = runtime::DecimalUtf16(amount);
    fields[20] = runtime::DecimalUtf16(remainingAmount);
    fields[21] = actor.characterName;
    fields[22] = actor.accountName;
    fields[25] = runtime::DecimalUtf16(item.ItemID());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void StageShapeShiftTransaction(
    CItemTransactionOrderMap& transactionItems, int order, const CItemSP& item) {
    item->SetTransation();
    CItemSP rollbackItem = item->Copy();
    rollbackItem->SetTrasactMode(TransactType_RollbackChanges);
    L2_ADD_REF(item, ObjectRefType::FunctionLocal);
    L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
    transactionItems.emplace(order, CItemKey(rollbackItem.get(), item.get()));
}

void CommitShapeShiftTransactions(CItemTransactionOrderMap& transactionItems) {
    for (const auto& entry : transactionItems) {
        CItem* const rollbackItem = entry.second.rollbackItem;
        CItem* const item = entry.second.item;
        item->CommitTransation(rollbackItem, true);
        L2_RELEASE(item, ObjectRefType::FunctionLocal);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
    }
}

} // namespace

bool CWareHouse::DestroyItem(CSocket* socket, DestoryItemParam& parameters) {
    // Current Cached.exe 0x005dd678.
    L2_GUARD_FUNCTION();

    if (parameters.amount < 0) {
        // Log.Add(
        //     LOG_ERROR, "DestroyItem minus[%d] item. dbid[%d][%d]", static_cast<int>(parameters.amount),
        //     static_cast<int>(m_ownerId), parameters.itemId
        // )
        // L2_LOG_ERROR(Log, "DestroyItem minus[%d] item. dbid[%d][%d]", static_cast<int>(parameters.amount),
        //     static_cast<int>(m_ownerId), parameters.itemId
        // )
        L2_LOG_ERROR(Log,
            "[warehouse] Item destruction rejected for owner %u, item %d: negative quantity %lld. Reporting the call stack.",
            m_ownerId, parameters.itemId, static_cast<long long>(parameters.amount));
        runtime::process::ReportCallStack(2, false);
        return false;
    }

    CItemTransactionOrderMap transactionItems;
    CItemSP item = GetItem(parameters.itemId);

    auto failTransaction = [&]() {
        if (socket) {
            socket->Send("cddQ", 0x1a, parameters.userServerId, parameters.itemServerId, static_cast<long long>(-1));
        }
        CTransaction::RollbackTransactOrderMap(transactionItems);
        return false;
    };

    if (!item)
        return failTransaction();

    item->SetTransation();
    CItemSP rollbackItem = item->Copy();
    rollbackItem->SetTrasactMode(TransactType_RollbackChanges);
    L2_ADD_REF(item, ObjectRefType::FunctionLocal);
    L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
    transactionItems.emplace(1, CItemKey(rollbackItem.get(), item.get()));

    std::int64_t finalAmount = 0;
    if (parameters.consumeType == 2 || parameters.consumeType == 3) {
        if (item->Amount() > 0) {
            finalAmount = runtime::SubtractInt64Wrapping(item->Amount(), parameters.amount);
        }
        if (finalAmount < 0) {
            return failTransaction();
        }
        item->SetAmount(finalAmount);
        if (finalAmount == 0) {
            item->TransactDelete();
        }
    } else {
        item->TransactDelete();
        finalAmount = 0;
    }

    if (socket) {
        socket->Send("cddQ", 0x1a, parameters.userServerId, parameters.itemServerId, static_cast<long long>(finalAmount));
    }

    CUserSP owner = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    const WareHouseAuditActor actor = MakeDestroyItemAuditActor(owner.get());

    for (const auto& entry : transactionItems) {
        CItem* const changedItem = entry.second.item;
        CItem* const originalItem = entry.second.rollbackItem;
        WriteDestroyItemAudit(actor, *changedItem, changedItem->m_intensiveItemType, parameters.amount, finalAmount);
        WriteManipulateEnsoulAudit(actor, *changedItem);

        if (!parameters.isPet) {
            changedItem->CommitTransation(originalItem, true);
        } else {
            CWareHouseSP rollbackWarehouse(this, ObjectRefType::FunctionChain);
            CWareHouseSP warehouse(this, ObjectRefType::FunctionChain);
            changedItem->CommitTransation(std::move(rollbackWarehouse), std::move(warehouse), originalItem, true);
        }

        L2_RELEASE(changedItem, ObjectRefType::FunctionLocal);
        L2_RELEASE(originalItem, ObjectRefType::FunctionLocal);
    }
    return true;
}

bool CWareHouse::DestroyItem(CSocket* socket, const std::uint8_t* packet, bool isPet) {
    // Current Cached.exe 0x005de214.
    L2_GUARD_FUNCTION();

    DestoryItemParam parameters;
    parameters.isPet = isPet;
    ::Disassemble(
        packet, "ddddQ", &parameters.itemId, &parameters.itemServerId, &parameters.consumeType, &parameters.userServerId,
        &parameters.amount
    );
    return DestroyItem(socket, parameters);
}

bool CWareHouse::VariationItem(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x005de31c. The three ordered transaction entries,
    // reply-before-commit ordering and all four negative result codes are
    // fixed by the shipping WareHouse.cpp body.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    int targetItemId = 0;
    int targetItemServerId = 0;
    int mineralItemId = 0;
    int mineralItemServerId = 0;
    int supportItemId = 0;
    int supportItemServerId = 0;
    std::int64_t supportItemAmount = 0;
    int variationOption1 = 0;
    int variationOption2 = 0;
    ::Disassemble(
        packet, "dddddddQdd", &userServerId, &targetItemId, &targetItemServerId, &mineralItemId, &mineralItemServerId,
        &supportItemId, &supportItemServerId, &supportItemAmount, &variationOption1, &variationOption2
    );

    CItemSP targetItem = GetItem(targetItemId);
    CItemSP mineralItem = GetItem(mineralItemId);
    CItemSP supportItem = GetItem(supportItemId);
    CItemTransactionOrderMap transactionItems;
    int transactionOrder = 0;

    auto beginTransaction = [&transactionItems, &transactionOrder](CItemSP item) -> CItemSP {
        item->SetTransation();
        CItemSP rollbackItem = item->Copy();
        rollbackItem->SetTrasactMode(TransactType_RollbackChanges);
        L2_ADD_REF(item, ObjectRefType::FunctionLocal);
        L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
        transactionItems.emplace(++transactionOrder, CItemKey(rollbackItem.get(), item.get()));
        return rollbackItem;
    };

    auto failTransaction = [&](int result) {
        socket->Send("chddd", 0xc6, 0x40, result, userServerId, targetItemServerId);
        CTransaction::RollbackTransactOrderMap(transactionItems);
        return false;
    };

    if (!targetItem || !mineralItem || (supportItemAmount > 0 && !supportItem) || supportItemAmount < 0) {
        return failTransaction(-1);
    }

    CItemSP targetRollback = beginTransaction(targetItem);
    CItemSP mineralRollback = beginTransaction(mineralItem);
    CItemSP supportRollback;
    if (supportItemAmount > 0) {
        supportRollback = beginTransaction(supportItem);
    }

    if (targetItem->Amount() < 1) {
        return failTransaction(-2);
    }
    if (mineralItem->Amount() < 1) {
        return failTransaction(-3);
    }
    if (supportItemAmount > 0 && supportItem->Amount() < supportItemAmount) {
        return failTransaction(-4);
    }

    mineralItem->SetAmount(runtime::SubtractInt64Wrapping(mineralItem->Amount(), 1));
    if (mineralItem->Amount() == 0) {
        mineralItem->TransactDelete();
    }
    if (supportItemAmount > 0) {
        supportItem->SetAmount(runtime::SubtractInt64Wrapping(supportItem->Amount(), supportItemAmount));
        if (supportItem->Amount() == 0) {
            supportItem->TransactDelete();
        }
    }

    targetItem->m_optionKey.variationOption1 = variationOption1;
    targetItem->m_optionKey.variationOption2 = variationOption2;
    targetItem->m_intensiveItemType = static_cast<int>(mineralItem->ItemType());

    const std::int64_t supportRemaining = supportItemAmount < 1 ? -1 : supportItem->Amount();
    socket->Send(
        "chdddddQddQ", 0xc6, 0x40, 1, userServerId, targetItemServerId, mineralItemServerId, supportItemServerId,
        static_cast<long long>(supportRemaining), variationOption1, variationOption2,
        static_cast<long long>(mineralItem->Amount())
    );

    for (const auto& entry : transactionItems) {
        CItem* const rollbackItem = entry.second.rollbackItem;
        CItem* const changedItem = entry.second.item;
        const std::int64_t amountBefore = rollbackItem->Amount();
        changedItem->CommitTransation(rollbackItem, true);

        // Current 0x005de31c emits the ordinary destroy and Ensoul records
        // only when at least one live variation-option component is absent.
        if (changedItem->OptionKey().variationOption1 < 1 || changedItem->OptionKey().variationOption2 < 1) {
            CUserSP owner = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
            if (owner) {
                const WareHouseAuditActor actor = MakeWareHouseAuditActor(owner.get());
                const std::int64_t consumedAmount = runtime::SubtractInt64Wrapping(amountBefore, changedItem->Amount());
                WriteDestroyItemAudit(
                    actor, *changedItem, changedItem->m_intensiveItemType, consumedAmount, changedItem->Amount()
                );
                WriteManipulateEnsoulAudit(actor, *changedItem);
            }
        }

        L2_RELEASE(changedItem, ObjectRefType::FunctionLocal);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
    }
    return true;
}

bool CWareHouse::CancelVariationItem(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x005dfbb4.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    int targetItemId = 0;
    int targetItemServerId = 0;
    std::int64_t cancellationPrice = 0;
    ::Disassemble(packet, "dddQ", &userServerId, &targetItemId, &targetItemServerId, &cancellationPrice);

    CItemSP targetItem = GetItem(targetItemId);
    CItemSP adenaItem = GetItemByType(57, 0);
    CItemTransactionOrderMap transactionItems;
    int transactionOrder = 0;

    auto beginTransaction = [&transactionItems, &transactionOrder](CItemSP item) -> CItemSP {
        item->SetTransation();
        CItemSP rollbackItem = item->Copy();
        rollbackItem->SetTrasactMode(TransactType_RollbackChanges);
        L2_ADD_REF(item, ObjectRefType::FunctionLocal);
        L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
        transactionItems.emplace(++transactionOrder, CItemKey(rollbackItem.get(), item.get()));
        return rollbackItem;
    };

    auto failTransaction = [&](int result) {
        socket->Send("chdddQ", 0xc6, 0x41, result, userServerId, targetItemServerId, static_cast<long long>(0));
        CTransaction::RollbackTransactOrderMap(transactionItems);
        return false;
    };

    if (!targetItem || (cancellationPrice > 0 && !adenaItem)) {
        return failTransaction(-1);
    }

    CItemSP targetRollback = beginTransaction(targetItem);
    CItemSP adenaRollback;
    if (adenaItem && cancellationPrice > 0) {
        adenaRollback = beginTransaction(adenaItem);
        if (adenaItem->Amount() < cancellationPrice) {
            return failTransaction(-5);
        }
        adenaItem->SetAmount(runtime::SubtractInt64Wrapping(adenaItem->Amount(), cancellationPrice));
        if (adenaItem->Amount() == 0) {
            adenaItem->TransactDelete();
        }
    }

    targetItem->m_optionKey.variationOption1 = 0;
    targetItem->m_optionKey.variationOption2 = 0;
    targetItem->m_intensiveItemType = 0;

    socket->Send(
        "chdddQ", 0xc6, 0x41, 1, userServerId, targetItemServerId,
        static_cast<long long>(adenaItem ? adenaItem->Amount() : 0)
    );

    for (const auto& entry : transactionItems) {
        CItem* const rollbackItem = entry.second.rollbackItem;
        CItem* const changedItem = entry.second.item;
        changedItem->CommitTransation(rollbackItem, true);
        L2_RELEASE(changedItem, ObjectRefType::FunctionLocal);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
    }
    return true;
}

bool CWareHouse::CancelEquipmentAddedEffectAndDropItem(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x005e04f0. Paired current L2Server receiver
    // 0x0067bc08 fixes the complete response field order.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    int itemId = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    int inventorySlot = 0;
    std::int64_t requestedAmount = 0;
    short responseFlag = 0;
    int effectMask = 0;
    ::Disassemble(
        packet, "ddddddQhd", &userServerId, &itemId, &x, &y, &z, &inventorySlot, &requestedAmount, &responseFlag, &effectMask
    );

    auto sendFailure = [&]() {
        socket->Send(
            "chdddddddQddQhd", 0xc6, 0xd9, -1, userServerId, x, y, z, 0, 0, static_cast<long long>(0), 0, 0,
            static_cast<long long>(0), responseFlag, effectMask
        );
    };

    CItemSP sourceItem = GetItem(itemId);
    if (m_lateSavingItemsPending) {
        SaveLateSavingItems();
    }
    if (!sourceItem) {
        sendFailure();
        return false;
    }

    const std::int64_t sourceAmount = sourceItem->Amount();
    if (sourceAmount < requestedAmount) {
        if (runtime::SubtractInt64Wrapping(requestedAmount, sourceAmount) > 5) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] CancelEquipmentAddedEffectAndDropItem "
            //     L"real[%I64d] try[%I64d] type[%d], drop all.",
            //     L"WareHouse.cpp", 0xf76, sourceAmount, requestedAmount, static_cast<int>(sourceItem->ItemType())
            // )
            // Noise: omit shipping coordinates. State the amount adjustment before the drop attempt.
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] CancelEquipmentAddedEffectAndDropItem "
            //     L"real[%I64d] try[%I64d] type[%d], drop all.",
            //     L"WareHouse.cpp", 0xf76, sourceAmount, requestedAmount, static_cast<int>(sourceItem->ItemType()))
            L2_LOG_ERROR(Log,
                "[items] Equipment-effect removal/drop requested %lld items but only %lld are available (item type %d). Reducing the drop amount to the available quantity.",
                static_cast<long long>(requestedAmount), static_cast<long long>(sourceAmount), static_cast<int>(sourceItem->ItemType()));
        }
        requestedAmount = sourceAmount;
    }

    CItemSP droppedItem = DropItem(sourceItem.get(), requestedAmount);
    if (!droppedItem) {
        sendFailure();
        return false;
    }

    bool recognizedEffect = false;
    if ((effectMask & 1) != 0) {
        droppedItem->m_optionKey = {};
        droppedItem->m_intensiveItemType = 0;
        droppedItem->Save(true);
        sourceItem->m_optionKey = {};
        sourceItem->m_intensiveItemType = 0;
        sourceItem->Save(true);
        recognizedEffect = true;
    }
    if ((effectMask & 2) != 0) {
        droppedItem->SetApperanceType(0, -1, static_cast<ShapeShiftingJobType>(1));
        sourceItem->SetApperanceType(0, -1, static_cast<ShapeShiftingJobType>(1));
        recognizedEffect = true;
    }

    if (effectMask == 0 || !recognizedEffect) {
        PushItem(droppedItem.get());
        sendFailure();
        return false;
    }

    // The target computes -abs(amount) immediately after DropItem, then
    // negates that temporary at 0x005e0b22 before placing it in the packet.
    // Therefore the wire value is positive abs(amount), including Win64's
    // wrapped INT64_MIN edge case.
    const std::int64_t responseAmount = requestedAmount < 0 ? runtime::NegateInt64Wrapping(requestedAmount) : requestedAmount;
    socket->Send(
        "chdddddddQddQhd", 0xc6, 0xd9, 1, userServerId, x, y, z, static_cast<int>(sourceItem->OwnerID()),
        static_cast<int>(sourceItem->ItemID()), static_cast<long long>(responseAmount),
        static_cast<int>(droppedItem->OwnerID()), static_cast<int>(droppedItem->ItemID()),
        static_cast<long long>(droppedItem->Amount()), responseFlag, effectMask
    );
    return true;
}

bool CWareHouse::ChangeAttributeItem(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x005e0d50. Both item transactions, the reply-before-
    // commit order and all four audit records are fixed by WareHouse.cpp's
    // shipping machine code.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    int sourceItemDatabaseId = 0;
    int sourceItemServerId = 0;
    int sourceConsumeType = 0;
    int targetItemDatabaseId = 0;
    int targetItemServerId = 0;
    int targetConsumeType = 0;
    int attributeType = -1;
    ::Disassemble(
        packet, "dddddddd", &userServerId, &sourceItemDatabaseId, &sourceItemServerId, &sourceConsumeType,
        &targetItemDatabaseId, &targetItemServerId, &targetConsumeType, &attributeType
    );

    CItemTransactionOrderMap transactionItems;
    CItemSP sourceItem = GetItem(sourceItemDatabaseId);
    CItemSP targetItem = GetItem(targetItemDatabaseId);

    auto failTransaction = [&] {
        CTransaction::RollbackTransactOrderMap(transactionItems);
        socket->Send("chdd", 0xc6, 0xb6, 0, userServerId);
        return false;
    };

    if (!sourceItem || !targetItem || static_cast<unsigned int>(attributeType + 1) > 6U || sourceItem->Amount() < 1) {
        return failTransaction();
    }

    auto beginTransaction = [&transactionItems](int order, const CItemSP& item) {
        item->SetTransation();
        CItemSP rollbackItem = item->Copy();
        rollbackItem->SetTrasactMode(TransactType_RollbackChanges);
        L2_ADD_REF(item, ObjectRefType::FunctionLocal);
        L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
        transactionItems.emplace(order, CItemKey(rollbackItem.get(), item.get()));
        return rollbackItem;
    };

    CItemSP sourceRollback = beginTransaction(1, sourceItem);
    CItemSP targetRollback = beginTransaction(2, targetItem);

    std::array<char16_t, 256> oldTargetAttributeText{};
    BaseAttribute oldTargetAttribute(targetItem->Attribute());
    oldTargetAttribute.GetLogString(oldTargetAttributeText.data());
    const std::u16string oldTargetAttributeLog = std::u16string(oldTargetAttributeText.data());
    const int targetIntensiveItemType = targetItem->m_intensiveItemType;

    BaseAttribute changedAttribute(targetItem->Attribute());
    changedAttribute.m_nAttackType = static_cast<short>(attributeType);
    targetItem->SetBaseAttribute(changedAttribute);

    // The target also creates these two source descriptions before the
    // decrement even though its later audit call does not consume either.
    std::array<char16_t, 256> unusedSourceAttributeText{};
    std::array<wchar_t, 256> unusedSourceVariationText{};
    BaseAttribute sourceAttribute(sourceItem->Attribute());
    sourceAttribute.GetLogString(unusedSourceAttributeText.data());
    std::swprintf(
        unusedSourceVariationText.data(), unusedSourceVariationText.size(), L"%d:%d:%d", sourceItem->m_intensiveItemType,
        sourceItem->OptionKey().variationOption1, sourceItem->OptionKey().variationOption2
    );

    sourceItem->SetAmount(runtime::SubtractInt64Wrapping(sourceItem->Amount(), 1));
    if (sourceItem->Amount() < 1) {
        sourceItem->TransactDelete();
    }
    if (sourceConsumeType != 2) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"change attribute caution!! check %s and find some "
        //     L"scrl_of_ench which is not stackable.",
        //     L"Itemdata.txt"
        // )
        // L2_LOG_ERROR(Log,
            // L"change attribute caution!! check %s and find some "
            // L"scrl_of_ench which is not stackable.",
            // L"Itemdata.txt"
        // )
        L2_LOG_ERROR(Log,
            "[warehouse] Attribute change for owner %u: source item %u, class %u has consume type %d; expected 2. "
            "Processing continues. Check its definition in Itemdata.txt.",
            m_ownerId, sourceItem->ItemID(), sourceItem->ItemType(), sourceConsumeType);
    }

    socket->Send(
        "chdddQd", 0xc6, 0xb6, 1, userServerId, sourceItemServerId, static_cast<long long>(sourceItem->Amount()),
        targetItemServerId
    );
    for (const auto& entry : transactionItems) {
        CItem* const rollbackItem = entry.second.rollbackItem;
        CItem* const changedItem = entry.second.item;
        changedItem->CommitTransation(rollbackItem, true);
        L2_RELEASE(changedItem, ObjectRefType::FunctionLocal);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
    }

    CUserSP owner = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    if (owner) {
        WriteChangeAttributeItemAudits(
            MakeWareHouseAuditActor(owner.get()), *targetItem.get(), *sourceItem.get(), sourceItemDatabaseId,
            targetItemDatabaseId, targetIntensiveItemType, oldTargetAttributeLog
        );
    }
    return true;
}

bool CWareHouse::EnchantItem(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x005e2be8.
    L2_GUARD_FUNCTION();

    int scrollItemDatabaseId = 0;
    int scrollItemServerId = 0;
    int scrollConsumeType = 0;
    int supportItemDatabaseId = 0;
    int supportItemServerId = 0;
    int supportConsumeType = 0;
    int targetItemDatabaseId = 0;
    int targetItemServerId = 0;
    int targetConsumeType = 0;
    int newEnchant = 0;
    int userServerId = 0;
    int requestedTargetItemType = 0;
    int successProbability = 0;
    std::uint8_t luckyStat = 0;
    ::Disassemble(
        packet, "dddddddddddddc", &scrollItemDatabaseId, &scrollItemServerId, &scrollConsumeType, &supportItemDatabaseId,
        &supportItemServerId, &supportConsumeType, &targetItemDatabaseId, &targetItemServerId, &targetConsumeType,
        &newEnchant, &userServerId, &requestedTargetItemType, &successProbability, &luckyStat
    );

    CItemTransactionOrderMap transactionItems;
    CItemSP scrollItem = GetItem(scrollItemDatabaseId);
    CItemSP targetItem = GetItem(targetItemDatabaseId);
    CItemSP supportItem = GetItem(supportItemDatabaseId);

    auto failTransaction = [&](int failureReason) {
        CTransaction::RollbackTransactOrderMap(transactionItems);
        // The current call at 0x005e4d90 omits the final `c`
        // from its format while still supplying the lucky-stat argument.
        socket->Send(
            "cdQdQddd", 0x1b, scrollItemServerId, static_cast<long long>(-1), supportItemServerId,
            static_cast<long long>(-1), targetItemServerId, -1, userServerId, luckyStat
        );
        if (failureReason >= 1) {
            socket->Send("chdddddd", 0xc6, 0x80, userServerId, 0, 0xc, failureReason, 0, 0);
        }
        return false;
    };

    if (!scrollItem || !targetItem || newEnchant < 0 || (supportItemDatabaseId > 0 && !supportItem)) {
        return failTransaction(0);
    }
    if (scrollItem->Amount() <= 0) {
        return failTransaction(1);
    }

    auto beginTransaction = [&transactionItems](int order, const CItemSP& item) {
        item->SetTransation();
        CItemSP rollbackItem = item->Copy();
        rollbackItem->SetTrasactMode(TransactType_RollbackChanges);
        L2_ADD_REF(item, ObjectRefType::FunctionLocal);
        L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
        transactionItems.emplace(order, CItemKey(rollbackItem.get(), item.get()));
        return rollbackItem;
    };

    CItemSP scrollRollback = beginTransaction(1, scrollItem);
    CItemSP targetRollback = beginTransaction(2, targetItem);
    CItemSP supportRollback;
    if (supportItemDatabaseId > 0) {
        supportRollback = beginTransaction(3, supportItem);
    }

    const int previousEnchant = targetItem->Enchant();
    targetItem->SetEnchant(newEnchant);

    scrollItem->SetAmount(runtime::SubtractInt64Wrapping(scrollItem->Amount(), 1));
    if (scrollItem->Amount() < 1) {
        scrollItem->TransactDelete();
    }
    if (scrollConsumeType != 2) {
        // Log.Add(
        //     LOG_ERROR,
        //     "enchant caution!! check %s and find some scrl_of_ench "
        //     "which is not stackable.",
        //     "Itemdata.txt"
        // )
        // L2_LOG_ERROR(Log,
            // "enchant caution!! check %s and find some scrl_of_ench "
            // "which is not stackable.",
            // "Itemdata.txt"
        // )
        L2_LOG_ERROR(Log,
            "[warehouse] Enchant processing for owner %u: item %u, class %u has consume type %d; expected 2. "
            "Processing continues. Check its definition in Itemdata.txt.",
            m_ownerId, scrollItem->ItemID(), scrollItem->ItemType(), scrollConsumeType);
    }

    if (supportItemDatabaseId > 0) {
        supportItem->SetAmount(runtime::SubtractInt64Wrapping(supportItem->Amount(), 1));
        if (supportItem->Amount() == 0) {
            supportItem->TransactDelete();
        }
        if (supportConsumeType != 2) {
            // Log.Add(
            //     LOG_ERROR,
            //     "enchant caution!! check %s and find some scrl_of_ench "
            //     "which is not stackable.",
            //     "Itemdata.txt"
            // )
            // L2_LOG_ERROR(Log,
                // "enchant caution!! check %s and find some scrl_of_ench "
                // "which is not stackable.",
                // "Itemdata.txt"
            // )
            L2_LOG_ERROR(Log,
                "[warehouse] Enchant support processing for owner %u: item %u, class %u has consume type %d; expected 2. "
                "Processing continues. Check its definition in Itemdata.txt.",
                m_ownerId, supportItem->ItemID(), supportItem->ItemType(), supportConsumeType);
        }
    }

    socket->Send(
        "cdQdQdddc", 0x1b, scrollItemServerId, static_cast<long long>(scrollItem->Amount()), supportItemServerId,
        supportItemDatabaseId > 0 ? static_cast<long long>(supportItem->Amount()) : static_cast<long long>(-1),
        targetItemServerId, newEnchant, userServerId, luckyStat
    );

    for (const auto& entry : transactionItems) {
        CItem* const rollbackItem = entry.second.rollbackItem;
        CItem* const changedItem = entry.second.item;
        changedItem->CommitTransation(rollbackItem, true);
        L2_RELEASE(changedItem, ObjectRefType::FunctionLocal);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
    }

    CUserSP owner = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    if (owner) {
        WriteEnchantItemAudits(
            MakeWareHouseAuditActor(owner.get()), *targetItem.get(), supportItem.get(), *scrollItem.get(),
            targetItemDatabaseId, supportItemDatabaseId, scrollItemDatabaseId, previousEnchant, newEnchant,
            requestedTargetItemType, successProbability, targetItem->m_intensiveItemType
        );
    }
    return true;
}

bool CWareHouse::EnchantItemFail(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x005e4ee8.
    L2_GUARD_FUNCTION();

    int scrollItemDatabaseId = 0;
    int scrollItemServerId = 0;
    int scrollConsumeType = 0;
    int supportItemDatabaseId = 0;
    int supportItemServerId = 0;
    int supportConsumeType = 0;
    int targetItemDatabaseId = 0;
    int targetItemServerId = 0;
    int targetConsumeType = 0;
    int userServerId = 0;
    int targetItemType = 0;
    int successProbability = 0;
    int crystalItemType = 0;
    int crystalAmount = 0;
    ::Disassemble(
        packet, "dddddddddddddd", &scrollItemDatabaseId, &scrollItemServerId, &scrollConsumeType, &supportItemDatabaseId,
        &supportItemServerId, &supportConsumeType, &targetItemDatabaseId, &targetItemServerId, &targetConsumeType,
        &userServerId, &targetItemType, &successProbability, &crystalItemType, &crystalAmount
    );

    CItemTransactionOrderMap transactionItems;
    CItemSP scrollItem = GetItem(scrollItemDatabaseId);
    CItemSP targetItem = GetItem(targetItemDatabaseId);
    CItemSP supportItem = GetItem(supportItemDatabaseId);
    // Current vtable slot +0x20 is called even when crystalAmount is zero.
    CItemSP crystalItem = GetItemByType(crystalItemType, 0);

    auto failTransaction = [&]() {
        CTransaction::RollbackTransactOrderMap(transactionItems);
        socket->Send(
            "cdQdQdddQ", 0x1c, scrollItemServerId, static_cast<long long>(-1), supportItemServerId,
            static_cast<long long>(-1), targetItemServerId, userServerId, -1, static_cast<long long>(-1)
        );
        return false;
    };

    if (!scrollItem || !targetItem || (supportItemDatabaseId > 0 && !supportItem) || scrollItem->Amount() <= 0) {
        return failTransaction();
    }

    int transactionOrder = 0;
    auto beginTransaction = [&transactionItems, &transactionOrder](
                                const CItemSP& item, TransactType rollbackMode
                            ) {
        item->SetTransation();
        CItemSP rollbackItem = item->Copy();
        rollbackItem->SetTrasactMode(rollbackMode);
        L2_ADD_REF(item, ObjectRefType::FunctionLocal);
        L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
        transactionItems.emplace(++transactionOrder, CItemKey(rollbackItem.get(), item.get()));
        return rollbackItem;
    };

    CItemSP scrollRollback = beginTransaction(scrollItem, TransactType_RollbackChanges);
    CItemSP targetRollback = beginTransaction(targetItem, TransactType_RollbackChanges);
    CItemSP supportRollback;
    if (supportItemDatabaseId > 0) {
        supportRollback = beginTransaction(supportItem, TransactType_RollbackChanges);
    }

    const bool hasCrystalResult = crystalAmount != 0;
    CItemSP crystalRollback;
    if (hasCrystalResult) {
        bool createdCrystalItem = false;
        if (!crystalItem) {
            const ItemOptionKey emptyOptionKey{};
            const BaseAttribute emptyBaseAttribute{};
            crystalItem = MakeNewItem(
                m_ownerId, static_cast<unsigned int>(crystalItemType), 0, 0, 0, 0, 0, 0, GetWarehouseNum(), emptyOptionKey,
                0, emptyBaseAttribute, 0, 0
            );
            if (!crystalItem)
                return failTransaction();
            createdCrystalItem = true;
        }

        crystalRollback = beginTransaction(
            crystalItem, createdCrystalItem ? TransactType_RollbackCreation : TransactType_RollbackChanges);
        crystalItem->SetAmount(
            createdCrystalItem ? static_cast<std::int64_t>(crystalAmount) : runtime::AddInt64Wrapping(crystalItem->Amount(), crystalAmount)
        );
    }

    const int previousEnchant = targetItem->Enchant();

    scrollItem->SetAmount(runtime::SubtractInt64Wrapping(scrollItem->Amount(), 1));
    if (scrollItem->Amount() < 1) {
        scrollItem->TransactDelete();
    }
    if (scrollConsumeType != 2) {
        // Log.Add(
        //     LOG_ERROR,
        //     "enchant caution!! check %s and find some scrl_of_ench "
        //     "which is not stackable.",
        //     "Itemdata.txt"
        // )
        // L2_LOG_ERROR(Log,
            // "enchant caution!! check %s and find some scrl_of_ench "
            // "which is not stackable.",
            // "Itemdata.txt"
        // )
        L2_LOG_ERROR(Log,
            "[warehouse] Failed-enchant processing for owner %u: item %u, class %u has consume type %d; expected 2. "
            "Processing continues. Check its definition in Itemdata.txt.",
            m_ownerId, scrollItem->ItemID(), scrollItem->ItemType(), scrollConsumeType);
    }

    if (supportItemDatabaseId > 0) {
        supportItem->SetAmount(runtime::SubtractInt64Wrapping(supportItem->Amount(), 1));
        if (supportItem->Amount() < 1) {
            supportItem->TransactDelete();
        }
        if (supportConsumeType != 2) {
            // Log.Add(
            //     LOG_ERROR,
            //     "enchant caution!! check %s and find some scrl_of_ench "
            //     "which is not stackable.",
            //     "Itemdata.txt"
            // )
            // L2_LOG_ERROR(Log,
                // "enchant caution!! check %s and find some scrl_of_ench "
                // "which is not stackable.",
                // "Itemdata.txt"
            // )
            L2_LOG_ERROR(Log,
                "[warehouse] Failed-enchant support processing for owner %u: item %u, class %u has consume type %d; expected 2. "
                "Processing continues. Check its definition in Itemdata.txt.",
                m_ownerId, supportItem->ItemID(), supportItem->ItemType(), supportConsumeType);
        }
    }

    targetItem->SetAmount(runtime::SubtractInt64Wrapping(targetItem->Amount(), 1));
    if (targetConsumeType == 1) {
        return failTransaction();
    }
    if (targetItem->Amount() > 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Item enchant fail. but target item amount is invalid. "
        //     "dbid[%d], amount[%d]",
        //     static_cast<int>(targetItem->ItemID()), static_cast<int>(targetItem->Amount())
        // )
        // L2_LOG_ERROR(Log,
            // "Item enchant fail. but target item amount is invalid. "
            // "dbid[%d], amount[%d]",
            // static_cast<int>(targetItem->ItemID()), static_cast<int>(targetItem->Amount())
        // )
        L2_LOG_ERROR(Log,
            "[warehouse] Failed-enchant target item %u, class %u, owner %u still has %lld units after decrement. "
            "Continuing with transactional deletion of the target item.",
            targetItem->ItemID(), targetItem->ItemType(), m_ownerId,
            static_cast<long long>(targetItem->Amount()));
    }
    targetItem->TransactDelete();

    socket->Send(
        "cdQdQdddQ", 0x1c, scrollItemServerId, static_cast<long long>(scrollItem->Amount()), supportItemServerId,
        supportItemDatabaseId > 0 ? static_cast<long long>(supportItem->Amount()) : static_cast<long long>(-1),
        targetItemServerId, userServerId, hasCrystalResult ? static_cast<int>(crystalItem->ItemID()) : 0,
        hasCrystalResult ? static_cast<long long>(crystalItem->Amount()) : static_cast<long long>(0)
    );

    CUserSP owner = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    const WareHouseAuditActor actor = MakeWareHouseAuditActor(owner.get());

    for (const auto& entry : transactionItems) {
        CItem* const rollbackItem = entry.second.rollbackItem;
        CItem* const changedItem = entry.second.item;
        if (hasCrystalResult && owner && changedItem->ItemID() == crystalItem->ItemID()) {
            WriteEnchantFailureCrystalAudit(actor, *changedItem, *rollbackItem);
        }
        changedItem->CommitTransation(rollbackItem, true);
        L2_RELEASE(changedItem, ObjectRefType::FunctionLocal);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
    }

    if (owner) {
        WriteEnchantItemAudits(
            actor, *targetItem.get(), supportItem.get(), *scrollItem.get(), targetItemDatabaseId, supportItemDatabaseId,
            scrollItemDatabaseId, previousEnchant, 0, targetItemType, successProbability, targetItem->m_intensiveItemType
        );
    }
    return true;
}

bool CWareHouse::BuyCommissionItem(
    CSocket* socket [[maybe_unused]],
    CPacketHelperEx<20480>& packet,
    CWareHouse* destinationWarehouse,
    int itemId,
    std::int64_t amount,
    int& itemType,
    int& enchant,
    int attributes[],
    char16_t attributeLog[]
) {
    // Current Cached.exe 0x00625e4c. This owner is between
    // EnchantItemFail and ManipulateItems in the shipping WareHouse.obj.
    L2_GUARD_FUNCTION();

    CItemTransactionOrderMap transactionItems;
    int transactionOrder = 0;
    auto stageItem = [&](CItemSP item, TransactType rollbackMode) -> bool {
        if (!item)
            return false;
        item->SetTransation();
        CItemSP rollbackItem = item->Copy();
        if (!rollbackItem)
            return false;
        rollbackItem->SetTrasactMode(rollbackMode);
        L2_ADD_REF(item, ObjectRefType::FunctionLocal);
        L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
        transactionItems.emplace(++transactionOrder, CItemKey(rollbackItem.get(), item.get()));
        return true;
    };
    auto rollback = [&]() {
        CTransaction::RollbackTransactOrderMap(transactionItems);
        transactionItems.clear();
        return false;
    };
    auto assembleAndPublishAttributes = [&](CItem& item) {
        // CItemAssemblerSendToL2Server assembler(item, 0x7f)
        CItemAssemblerSendToL2Server assembler(item, ItemAssembleType::ALL);
        assembler.AssembleItemInfoImpl(packet);
        enchant = item.Enchant();
        itemType = static_cast<int>(item.ItemType());

        const BaseAttribute& baseAttribute = item.Attribute();
        const int attackType = baseAttribute.m_nAttackType;
        if (attributes && attackType >= 0 && attackType < ATTRT_MAX) {
            attributes[attackType] = baseAttribute.m_nAttackValue;
        }
        if (attributes) {
            for (int index = 0; index < ATTRT_MAX; ++index) {
                if (baseAttribute.m_nDefendValue[index] != 0) {
                    attributes[index] = baseAttribute.m_nDefendValue[index];
                }
            }
        }
    };

    CItemSP sourceItem = GetItem(itemId);
    if (!sourceItem) {
        // Log.Add(
        //     LOG_ERROR, "[%s],[%d]BuyItemCommission Item invalid! ItemDBID[%d]", "WareHouse.cpp", 0x4bbb, itemId
        // )
        // Log.Add(LOG_ERROR,
        //     "[commission] Purchased-item transfer failed for source owner %u: item %d not found. Attempting rollback of item changes.",
        //     m_ownerId, itemId)
        L2_LOG_ERROR(Log,
            "[commission] Purchased-item transfer failed for source owner %u: item %d not found; attempting rollback of item changes.",
            m_ownerId, itemId);
        return rollback();
    }

    BaseAttribute sourceAttribute(sourceItem->Attribute());
    sourceAttribute.GetLogString(attributeLog);
    const ItemConsumeType consumeType = g_readFile.GetItemConsumeType(static_cast<int>(sourceItem->ItemType()));
    if (!stageItem(sourceItem, TransactType_RollbackChanges)) {
        return rollback();
    }

    const std::int64_t sourceAmount = sourceItem->Amount();
    if (sourceAmount < amount) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s],[%d]BuyItemCommission Item count is invalid. %d "
        //     "[%lld < %lld]",
        //     "WareHouse.cpp", 0x4bcf, itemId, static_cast<long long>(sourceAmount), static_cast<long long>(amount)
        // )
        // Log.Add(LOG_ERROR,
        //     "[commission] Purchased-item transfer failed for source owner %u: item %d has quantity %lld, requested %lld. Attempting rollback of item changes.",
        //     m_ownerId, itemId, static_cast<long long>(sourceAmount), static_cast<long long>(amount))
        L2_LOG_ERROR(Log,
            "[commission] Purchased-item transfer failed for source owner %u: item %d has quantity %lld, requested %lld; attempting rollback of item changes.",
            m_ownerId, itemId, static_cast<long long>(sourceAmount), static_cast<long long>(amount));
        return rollback();
    }

    if (consumeType == ICT_STACKABLE || consumeType == ICT_ASSET) {
        CItemSP destinationItem = destinationWarehouse->GetItemByType(static_cast<int>(sourceItem->ItemType()), 0);
        if (!destinationItem) {
            if (sourceAmount == amount) {
                sourceItem->setOwnerID(destinationWarehouse->m_ownerId);
                sourceItem->SetWarehouse(destinationWarehouse->GetWarehouseNum());
                sourceItem->m_ident = 0;
                assembleAndPublishAttributes(*sourceItem.get());
                sourceItem->SetChangeScope(ItemChangeScope_Decreased);
            } else {
                sourceItem->SetAmount(runtime::SubtractInt64Wrapping(sourceAmount, amount));
                destinationItem = destinationWarehouse->MakeNewItem(
                    destinationWarehouse->m_ownerId, sourceItem->ItemType(), amount, sourceItem->Enchant(),
                    sourceItem->Eroded(), sourceItem->Bless(), 0, sourceItem->Wished(),
                    destinationWarehouse->GetWarehouseNum(), sourceItem->OptionKey(), sourceItem->m_intensiveItemType,
                    sourceItem->Attribute(), sourceItem->HLocked(), sourceItem->UseCount()
                );
                if (!destinationItem) {
                    // Log.Add(
                    //     LOG_ERROR, "[%s],[%d]BuyItemCommission NewItem valid!", "WareHouse.cpp", 0x4c88
                    // )
                    // Log.Add(LOG_ERROR,
                    //     "[commission] Cannot create the purchased stack for destination owner %u from item %d (source owner %u), quantity %lld. Source quantity is already %lld in memory. Attempting rollback of item changes.",
                    //     destinationWarehouse->m_ownerId, itemId, m_ownerId, static_cast<long long>(amount), static_cast<long long>(sourceItem->Amount()))
                    L2_LOG_ERROR(Log,
                        "[commission] Cannot create the purchased stack for destination owner %u from item %d (source owner %u), quantity %lld; source quantity is already %lld in memory. Attempting rollback of item changes.",
                        destinationWarehouse->m_ownerId, itemId, m_ownerId, static_cast<long long>(amount), static_cast<long long>(sourceItem->Amount()));
                    return rollback();
                }
                if (!stageItem(destinationItem, TransactType_RollbackCreation)) {
                    return rollback();
                }
                assembleAndPublishAttributes(*destinationItem.get());
                sourceItem->SetChangeScope(ItemChangeScope_Added);
                destinationItem->SetChangeScope(ItemChangeScope_Fee);
            }
        } else {
            if (!stageItem(destinationItem, TransactType_RollbackChanges)) {
                return rollback();
            }
            sourceItem->SetAmount(runtime::SubtractInt64Wrapping(sourceAmount, amount));
            destinationItem->SetAmount(runtime::AddInt64Wrapping(destinationItem->Amount(), amount));
            destinationItem->m_ident = 0;
            if (sourceAmount == amount) {
                sourceItem->TransactDelete();
            }
            assembleAndPublishAttributes(*destinationItem.get());
            sourceItem->SetChangeScope(ItemChangeScope_Added);
            destinationItem->SetChangeScope(ItemChangeScope_Increased);
        }
    } else {
        sourceItem->setOwnerID(destinationWarehouse->m_ownerId);
        sourceItem->SetWarehouse(destinationWarehouse->GetWarehouseNum());
        sourceItem->m_ident = 0;
        assembleAndPublishAttributes(*sourceItem.get());
        sourceItem->SetChangeScope(ItemChangeScope_Deleted);
    }

    for (const auto& entry : transactionItems) {
        CItem* rollbackItem = entry.second.rollbackItem;
        CItem* item = entry.second.item;
        item->CommitTransation(rollbackItem, true);
        L2_RELEASE(item, ObjectRefType::FunctionLocal);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
    }
    transactionItems.clear();
    return true;
}

bool CWareHouse::ManipulateItems(
    CSocket* socket,
    const std::uint8_t* packet,
    int& responseId,
    int& responseId2,
    std::vector<EtcMani*>& etcManis,
    CPacketHelperEx<20480>& itemPacket,
    CPacketHelperEx<20480>& etcManiPacket,
    unsigned int& changedItemCount,
    std::uint64_t& processedEtcManiCount
) {
    // Current Cached.exe 0x00628bdc.
    L2_GUARD_FUNCTION();

    changedItemCount = 0;
    processedEtcManiCount = 0;

    int premiumRelated = 0;
    int etcManiCount = 0;
    int itemCount = 0;
    const std::uint8_t* cursor =
        ::Disassemble(packet, "ddddd", &responseId, &responseId2, &premiumRelated, &etcManiCount, &itemCount);

    CItemTransactionOrderMap transactionItems;
    std::map<CItem*, ManipulateItemAuditState> auditStates;
    int transactionOrder = 0;

    auto failTransaction = [&](bool sendManipulationKick = false) {
        ManipulateItemTransactionFailed(socket, responseId, transactionItems, etcManis, responseId2);
        if (sendManipulationKick) {
            socket->Send("chdddddd", 0xc6, 0x12, responseId, 0, 0, 0, 0, 0);
        }
        return false;
    };

    auto beginTransaction = [&transactionItems, &transactionOrder](
                                const CItemSP& item, TransactType rollbackMode
                            ) -> CItemSP {
        if (!item)
            return {};
        item->SetTransation();
        CItemSP rollbackItem = item->Copy();
        if (!rollbackItem)
            return {};
        rollbackItem->SetTrasactMode(rollbackMode);
        L2_ADD_REF(item, ObjectRefType::FunctionLocal);
        L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
        transactionItems.emplace(runtime::AddInt32Wrapping(transactionOrder, 1), CItemKey(rollbackItem.get(), item.get()));
        transactionOrder = runtime::AddInt32Wrapping(transactionOrder, 1);
        return rollbackItem;
    };

    if (itemCount == 0 && etcManiCount == 0) {
        return failTransaction();
    }

    cursor = EtcManiUtil::DisassembleEtcManis(cursor, etcManiCount, etcManis);
    if (!EtcManiUtil::DoAtCache(etcManis, etcManiPacket)) {
        return failTransaction();
    }

    int lottoRound = 0;
    int lottoNumberFlag = 0;
    int lottoItemId = 0;
    int monRaceId = 0;
    std::uint32_t monRacePackedBet = 0;
    int monRaceBetMoney = 0;
    int monRaceItemId = 0;

    for (int itemIndex = 0; itemIndex < itemCount; ++itemIndex) {
        CItemDisassemblerItemMani incomingItem;
        cursor = incomingItem.DisassembleItemInfoImpl(cursor);

        CItemSP item;
        CItemSP rollbackItem;
        ItemChangeScope changeScope = ItemChangeScope_Unknown;
        const bool stackable = incomingItem.m_eConsumeType == ICT_STACKABLE || incomingItem.m_eConsumeType == ICT_ASSET;

        if (!stackable) {
            if (incomingItem.m_nAmount == 1) {
                if (static_cast<std::int32_t>(incomingItem.m_nItemID) < 1) {
                    const ItemOptionKey emptyOptionKey{};
                    const BaseAttribute emptyBaseAttribute{};
                    item = MakeNewItem(
                        m_ownerId, incomingItem.m_nItemType, 0, 0, 0, 0, 0, 0, static_cast<int>(GetWarehouseNum()),
                        emptyOptionKey, 0, emptyBaseAttribute, 0, 0
                    );
                    if (!item)
                        return failTransaction();

                    rollbackItem = beginTransaction(item, TransactType_RollbackCreation);
                    if (!rollbackItem) {
                        return failTransaction();
                    }

                    if (!incomingItem.m_Attribute.IsNullAttribute() &&
                        !ExecuteManipulateItemAttributeWrite(static_cast<int>(item->ItemID()), incomingItem.m_Attribute)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     "request manipulate item char[%u] Cannot "
                        //     "persist item attribute. class id[%u]",
                        //     m_ownerId, incomingItem.m_nItemType
                        // )
                        // Previous: L2_LOG_ERROR(Log,
                        //     "request manipulate item char[%u] Cannot "
                        //     "persist item attribute. class id[%u]",
                        //     m_ownerId, incomingItem.m_nItemType
                        // )
                        L2_LOG_ERROR(Log,
                            "[items] Cannot manipulate items for character %u: failed to persist the attribute for class %u; rolling back the transaction.",
                            m_ownerId, incomingItem.m_nItemType);
                        return failTransaction();
                    }

                    item->SetAmount(incomingItem.m_nAmount);
                    item->m_enchant = incomingItem.m_nEnchant;
                    item->m_eroded = incomingItem.m_nEroded;
                    item->m_bless = static_cast<int>(static_cast<std::uint8_t>(incomingItem.m_nBless));
                    item->m_optionKey.variationOption1 = incomingItem.m_nVariation1;
                    item->m_optionKey.variationOption2 = incomingItem.m_nVariation2;
                    item->m_intensiveItemType = incomingItem.m_nIntensiveItemType;
                    item->SetBaseAttribute(incomingItem.m_Attribute);
                    if (incomingItem.m_nShapeShiftedClassID > 0) {
                        item->SetApperanceType(
                            incomingItem.m_nShapeShiftedClassID, incomingItem.m_nShapeShiftedPeriod,
                            static_cast<ShapeShiftingJobType>(0)
                        );
                    }
                    item->SetEnsoulInfo(incomingItem.m_mEnsoulInfo.data());
                    item->m_hLocked = incomingItem.m_hLocked;
                    item->m_useCount = incomingItem.m_hUseCount;
                    changeScope = ItemChangeScope_Added;
                    item->SetChangeScope(changeScope);

                    if (incomingItem.m_nItemType == 0x115a) {
                        lottoRound = static_cast<int>(static_cast<std::uint16_t>(incomingItem.m_nBless));
                        lottoNumberFlag = incomingItem.m_nEnchant;
                        lottoItemId = static_cast<int>(item->ItemID());
                    } else if (incomingItem.m_nItemType == 0x115b || incomingItem.m_nItemType == 0x115c) {
                        monRaceId = incomingItem.m_nEnchant;
                        monRacePackedBet = static_cast<std::uint16_t>(incomingItem.m_nBless);
                        monRaceBetMoney = incomingItem.m_nEroded;
                        monRaceItemId = static_cast<int>(item->ItemID());
                    }
                } else {
                    item = CItem::Load(incomingItem.m_nItemID);
                    if (!item || item->Amount() <= 0) {
                        return failTransaction();
                    }

                    rollbackItem = beginTransaction(item, TransactType_RollbackCreation);
                    if (!rollbackItem) {
                        return failTransaction();
                    }

                    if (item->OwnerID() != m_ownerId) {
                        item->SetInventorySlotIndex(-1);
                    }
                    item->m_ownerId = m_ownerId;
                    item->SetWarehouse(static_cast<int>(GetWarehouseNum()));
                    changeScope = ItemChangeScope_Added;
                    item->SetChangeScope(changeScope);
                }
            } else if (incomingItem.m_nAmount == -1) {
                item = GetItem(static_cast<int>(incomingItem.m_nItemID));
                if (!item || item->Amount() != 1) {
                    return failTransaction();
                }

                rollbackItem = beginTransaction(item, TransactType_RollbackChanges);
                if (!rollbackItem)
                    return failTransaction();

                if (incomingItem.m_nShapeShiftedClassID > 0) {
                    item->SetApperanceType(0, -1, static_cast<ShapeShiftingJobType>(2));
                }
                item->SetAmount(0);
                item->TransactDelete();
                changeScope = ItemChangeScope_Deleted;
                item->SetChangeScope(changeScope);
            } else {
                return failTransaction();
            }
        } else {
            item = GetItemByType(static_cast<int>(incomingItem.m_nItemType), 0);
            if (!item || item->Amount() == 0) {
                if (incomingItem.m_nAmount <= 0) {
                    return failTransaction(true);
                }

                const ItemOptionKey emptyOptionKey{};
                const BaseAttribute emptyBaseAttribute{};
                item = MakeNewItem(
                    m_ownerId, incomingItem.m_nItemType, 0, 0, 0, 0, 0, 0, static_cast<int>(GetWarehouseNum()),
                    emptyOptionKey, 0, emptyBaseAttribute, 0, 0
                );
                if (!item)
                    return failTransaction();

                rollbackItem = beginTransaction(item, TransactType_RollbackCreation);
                if (!rollbackItem)
                    return failTransaction();

                item->SetAmount(incomingItem.m_nAmount);
                changeScope = ItemChangeScope_Added;
                item->SetChangeScope(changeScope);
            } else {
                rollbackItem = beginTransaction(item, TransactType_RollbackChanges);
                if (!rollbackItem)
                    return failTransaction();

                if (incomingItem.m_nAmount == 0) {
                    return failTransaction();
                }
                if (incomingItem.m_nAmount > 0) {
                    item->SetAmount(runtime::AddInt64Wrapping(item->Amount(), incomingItem.m_nAmount));
                    changeScope = ItemChangeScope_Increased;
                } else {
                    const std::int64_t resultAmount = runtime::AddInt64Wrapping(item->Amount(), incomingItem.m_nAmount);
                    if (resultAmount < 0) {
                        return failTransaction(true);
                    }
                    item->SetAmount(resultAmount);
                    if (resultAmount == 0) {
                        item->TransactDelete();
                        changeScope = ItemChangeScope_Deleted;
                    } else {
                        changeScope = ItemChangeScope_Decreased;
                    }
                }
                item->SetChangeScope(changeScope);
            }
        }

        CItemAssemblerItemMani itemAssembler(
            // Current Cached.exe 0x00629ecc passes the computed result index
            // (1 add, 2 increase, 3 decrease, 4 delete) in R8. The original
            // request index remains the later change-scope field. Keeping
            // these adjacent dwords in the opposite order makes L2Server
            // reject successful NPC item grants as manipulate type 0.
            *item.get(), static_cast<int>(changeScope), incomingItem.m_nAmount,
            static_cast<ItemChangeScope>(incomingItem.m_nIndex), incomingItem.m_nGetReason,
            // incomingItem.m_nShapeShiftedPeriod, 0x7f
            incomingItem.m_nShapeShiftedPeriod, ItemAssembleType::ALL
        );
        if (itemAssembler.AssembleItemInfoImpl(itemPacket) < 1) {
            // Log.Add(LOG_ERROR, "request manipulate item char[%u] socket buffer overflow", m_ownerId)
            // Previous: L2_LOG_ERROR(Log, "request manipulate item char[%u] socket buffer overflow", m_ownerId)
            L2_LOG_ERROR(Log,
                "[items] Cannot assemble the manipulate-item reply for character %u: the socket buffer is full; rolling back the transaction.",
                m_ownerId);
            return failTransaction();
        }

        ++changedItemCount;
        auditStates.emplace(
            rollbackItem.get(), ManipulateItemAuditState{incomingItem.m_nItemID, item->Amount(), changeScope}
        );
    }

    processedEtcManiCount = etcManis.size();
    for (EtcMani* etcMani : etcManis) {
        etcMani->Destroy();
    }
    etcManis.clear();

    CUserSP owner = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    const WareHouseAuditActor actor = MakeWareHouseAuditActor(owner.get());
    const bool writeCache = GetCacheDConfiguration().writeCache;
    if (writeCache)
        m_unsavedItemsPending = true;

    for (const auto& entry : transactionItems) {
        CItem* rollback = entry.second.rollbackItem;
        CItem* changedItem = entry.second.item;
        ManipulateItemAuditState fallback{0, changedItem->Amount(), changedItem->ChangeScope()};
        const auto stateEntry = auditStates.find(rollback);
        const ManipulateItemAuditState& state = stateEntry == auditStates.end() ? fallback : stateEntry->second;

        WriteManipulateItemAudit(
            actor, *changedItem, *rollback, state, changedItem->m_intensiveItemType, premiumRelated != 0
        );

        if (state.changeScope == ItemChangeScope_Added || state.changeScope == ItemChangeScope_Increased) {
            CWatchItem* watchItem =
                g_WatchList.CheckWatchList(static_cast<WATCHTYPE>(2), static_cast<int>(changedItem->ItemType()));
            if (watchItem) {
                WriteManipulateWatchAudit(actor, *changedItem, *watchItem);
            }
        }

        if (writeCache)
            m_unsavedItemsPending = true;
        changedItem->CommitTransation(rollback, !writeCache);
        L2_RELEASE(rollback, ObjectRefType::FunctionLocal);
        L2_RELEASE(changedItem, ObjectRefType::FunctionLocal);
    }

    if (lottoItemId > 0) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        int totalCount = 0;
        connection.Bind(&totalCount);
        if (!ExecuteAddLottoItem(connection, lottoRound, lottoItemId, lottoNumberFlag) || !connection.Fetch()) {
            // Log.Add(LOG_ERROR, "[%s][%d] Lotto db update failed !!!", "WareHouse.cpp", 0x25b1)
            L2_LOG_ERROR(Log,
                "[lottery] Cannot obtain the ticket registration result for owner %u, item %d, round %d, number flags %d: "
                "database execution or fetch failed. NPC notification and lottery audit are skipped; request processing continues.",
                m_ownerId, lottoItemId, lottoRound, lottoNumberFlag);
        } else {
            g_NpcServer.Send("cddd", 2, 0, lottoItemId, totalCount);
            WriteLottoManipulateAudit(actor, lottoRound, lottoNumberFlag, totalCount, lottoItemId);
        }
    }

    if (monRaceItemId > 0) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        int ticketId = 0;
        std::int64_t ticketIdIndicator = -1;
        connection.BindNullable(&ticketId, &ticketIdIndicator);

        const int highSelection = static_cast<int>((monRacePackedBet >> 4U) & 0xfU);
        const int lowSelection = static_cast<int>(monRacePackedBet & 0xfU);
        const int remoteFeePercent = static_cast<int>((monRacePackedBet >> 8U) & 0xffU);

        int betType = 2;
        int firstSelection = 0;
        int secondSelection = 0;
        if (highSelection == 0) {
            betType = 1;
            firstSelection = static_cast<int>(monRacePackedBet & 0xffU);
        } else {
            firstSelection = std::min(lowSelection, highSelection);
            secondSelection = std::max(lowSelection, highSelection);
        }

        const int thirdSelection = 0;
        const int taxMoney = g_MonRaceTaxRate == 0 ? 0 : TargetInt32Percent(g_MonRaceTaxRate, monRaceBetMoney);
        const int remoteFee = remoteFeePercent == 0 ? 0 : TargetInt32Percent(remoteFeePercent, monRaceBetMoney);
        if (ExecuteCreateMonRaceTicket(
                connection, monRaceId, betType, firstSelection, secondSelection, thirdSelection, monRaceBetMoney, taxMoney,
                monRaceItemId, remoteFee
            )) {
            connection.Fetch();
        }

        const int selection = betType == 1 ? firstSelection : runtime::AddInt32Wrapping(firstSelection * 100, secondSelection);
        WriteMonRaceManipulateAudit(actor, monRaceId, betType, selection, monRaceBetMoney, monRaceItemId);
    }

    return true;
}

int CWareHouse::ManipulatePointItems(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0062c0f4 / WareHouse.cpp:10359-10908.
    L2_GUARD_FUNCTION();

    CUserSP owner = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    if (!owner) {
        // RequestManipulateItemWithPoint resolves this same resident owner
        // before obtaining the warehouse lock, so the current production
        // entry path cannot reach this state.
        return 0;
    }
    const WareHouseAuditActor actor = MakeWareHouseAuditActor(owner.get());

    int userServerId = 0;
    int pointDelta = 0;
    int pointType = 0;
    int requestValue = 0;
    int replyExtra = 0;
    int atomicJobId = 0;
    int itemCount = 0;
    int etcManiCount = 0;
    const std::uint8_t* cursor = ::Disassemble(
        packet, "dddddddd", &userServerId, &pointDelta, &pointType, &requestValue, &replyExtra, &atomicJobId, &itemCount,
        &etcManiCount
    );

    CPacketHelperEx<20480> itemPacket;
    CPacketHelperEx<20480> etcManiPacket;
    std::vector<EtcMani*> etcManis;
    cursor = EtcManiUtil::DisassembleEtcManis(cursor, etcManiCount, etcManis);

    CItemTransactionOrderMap transactionItems;
    int transactionOrder = 0;
    CPledgeSP pledge;
    int pledgeId = static_cast<int>(owner->GetPledgeID());
    std::u16string pledgeName;
    bool pledgePointApplied = false;

    auto failTransaction = [&]() {
        ManipulateItemTransactionFailed(socket, userServerId, transactionItems, etcManis, atomicJobId);
        if (pledgePointApplied && pledge) {
            pledge->UpdateNameValue(runtime::SubtractInt32Wrapping(0, pointDelta), 0);
        }

        socket->Send(
            "cddddddb", 0xc2, userServerId, atomicJobId, pointType, pointDelta, -1, static_cast<int>(etcManis.size()),
            etcManiPacket.GetLength(), etcManiPacket.GetPacket()
        );
        if (pointType == 2) {
            socket->Send("chdddddd", 0xc6, 0x2c, userServerId, 0, pledgeId, 0, pointDelta, -3);
        } else {
            socket->Send(
                "chddddd", 0xc6, pointType == 1 ? 0x20 : 0x57, static_cast<int>(m_ownerId), pointDelta, -4, pointType, 0
            );
        }
        return 0;
    };

    auto beginTransaction = [&transactionItems, &transactionOrder](
                                const CItemSP& item, TransactType rollbackMode
                            ) -> CItemSP {
        if (!item)
            return {};
        item->SetTransation();
        CItemSP rollbackItem = item->Copy();
        if (!rollbackItem)
            return {};
        rollbackItem->SetTrasactMode(rollbackMode);
        L2_ADD_REF(item, ObjectRefType::FunctionLocal);
        L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
        transactionOrder = runtime::AddInt32Wrapping(transactionOrder, 1);
        transactionItems.emplace(transactionOrder, CItemKey(rollbackItem.get(), item.get()));
        return rollbackItem;
    };

    if (!EtcManiUtil::DoAtCache(etcManis, etcManiPacket)) {
        return failTransaction();
    }

    int resultingPoint = 0;
    if (pointType == 2) {
        pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
        if (pledge)
            pledgeName.assign(pledge->GetName());

        if (!pledge || !pledge->CanApplyNameValueDelta(pointDelta) ||
            !(pledgePointApplied = pledge->UpdateNameValue(pointDelta, 0))) {
            socket->Send("chdddddd", 0xc6, 0x2c, userServerId, 0, pledgeId, 0, pointDelta, -4);
            socket->Send("cddddd", 0xc2, userServerId, atomicJobId, pointType, pointDelta, -100);
            return 0;
        }
    } else if (pointType != 7) {
        const int currentPoint = pointType == 1 ? owner->LoadPCCafePoint() : owner->LoadUserPoint(pointType);
        resultingPoint = runtime::AddInt32Wrapping(currentPoint, pointDelta);
        if (resultingPoint < 0) {
            socket->Send("cddddd", 0xc2, userServerId, atomicJobId, pointType, pointDelta, -100);
            if (pointType == 1) {
                socket->Send("chdddddd", 0xc6, 0x20, static_cast<int>(m_ownerId), pointDelta, -3, 1, 0, 0);
            } else {
                socket->Send("chddddd", 0xc6, 0x57, static_cast<int>(m_ownerId), pointDelta, -3, pointType, 0);
            }
            return 0;
        }

        int pointLimit = std::numeric_limits<int>::max();
        if (pointType == 1) {
            pointLimit = 200000;
        } else if (pointType == 5 || pointType == 8) {
            pointLimit = 20000000;
        }
        resultingPoint = std::min(resultingPoint, pointLimit);
    }

    int changedItemCount = 0;
    int lastGetReason = 0;
    for (int itemIndex = 0; itemIndex < itemCount; ++itemIndex) {
        CItemDisassemblerItemMani incomingItem;
        cursor = incomingItem.DisassembleItemInfoImpl(cursor);
        lastGetReason = incomingItem.m_nGetReason;

        CItemSP item;
        CItemSP rollbackItem;
        ItemChangeScope changeScope = ItemChangeScope_Unknown;
        const bool stackable = incomingItem.m_eConsumeType == ICT_STACKABLE || incomingItem.m_eConsumeType == ICT_ASSET;

        if (!stackable) {
            if (incomingItem.m_nAmount == 1) {
                if (static_cast<std::int32_t>(incomingItem.m_nItemID) < 1) {
                    const ItemOptionKey emptyOptionKey{};
                    const BaseAttribute emptyBaseAttribute{};
                    item = MakeNewItem(
                        m_ownerId, incomingItem.m_nItemType, 0, 0, 0, 0, 0, 0, static_cast<int>(GetWarehouseNum()),
                        emptyOptionKey, 0, emptyBaseAttribute, 0, 0
                    );
                    if (!item)
                        return failTransaction();

                    rollbackItem = beginTransaction(item, TransactType_RollbackCreation);
                    if (!rollbackItem) {
                        return failTransaction();
                    }

                    item->SetAmount(incomingItem.m_nAmount);
                    item->m_enchant = incomingItem.m_nEnchant;
                    item->m_eroded = incomingItem.m_nEroded;
                    item->m_bless = static_cast<int>(static_cast<std::uint8_t>(incomingItem.m_nBless));
                    item->m_optionKey.variationOption1 = incomingItem.m_nVariation1;
                    item->m_optionKey.variationOption2 = incomingItem.m_nVariation2;
                    item->m_intensiveItemType = incomingItem.m_nIntensiveItemType;
                    item->SetBaseAttribute(incomingItem.m_Attribute);
                    item->SetEnsoulInfo(incomingItem.m_mEnsoulInfo.data());
                    item->m_hLocked = incomingItem.m_hLocked;
                    item->m_useCount = incomingItem.m_hUseCount;
                    if (incomingItem.m_nShapeShiftedClassID > 0) {
                        item->SetApperanceType(
                            incomingItem.m_nShapeShiftedClassID, incomingItem.m_nShapeShiftedPeriod,
                            static_cast<ShapeShiftingJobType>(0)
                        );
                    }
                    changeScope = ItemChangeScope_Added;
                } else {
                    item = CItem::Load(incomingItem.m_nItemID);
                    if (!item || item->Amount() <= 0) {
                        return failTransaction();
                    }

                    rollbackItem = beginTransaction(item, TransactType_RollbackCreation);
                    if (!rollbackItem) {
                        return failTransaction();
                    }
                    if (item->OwnerID() != m_ownerId) {
                        item->SetInventorySlotIndex(-1);
                    }
                    item->m_ownerId = m_ownerId;
                    item->SetWarehouse(static_cast<int>(GetWarehouseNum()));
                    changeScope = ItemChangeScope_Added;
                }
            } else if (incomingItem.m_nAmount == -1) {
                item = GetItem(static_cast<int>(incomingItem.m_nItemID));
                if (!item || item->Amount() != 1) {
                    return failTransaction();
                }

                rollbackItem = beginTransaction(item, TransactType_RollbackChanges);
                if (!rollbackItem)
                    return failTransaction();
                if (incomingItem.m_nShapeShiftedClassID > 0) {
                    item->SetApperanceType(0, -1, static_cast<ShapeShiftingJobType>(2));
                }
                item->SetAmount(0);
                item->TransactDelete();
                changeScope = ItemChangeScope_Deleted;
            } else {
                return failTransaction();
            }
        } else {
            item = GetItemByType(static_cast<int>(incomingItem.m_nItemType), 0);
            if (!item) {
                if (incomingItem.m_nAmount <= 0) {
                    return failTransaction();
                }

                const ItemOptionKey emptyOptionKey{};
                const BaseAttribute emptyBaseAttribute{};
                item = MakeNewItem(
                    m_ownerId, incomingItem.m_nItemType, 0, 0, 0, 0, 0, 0, static_cast<int>(GetWarehouseNum()),
                    emptyOptionKey, 0, emptyBaseAttribute, 0, 0
                );
                if (!item)
                    return failTransaction();

                rollbackItem = beginTransaction(item, TransactType_RollbackCreation);
                if (!rollbackItem)
                    return failTransaction();
                item->SetAmount(incomingItem.m_nAmount);
                changeScope = ItemChangeScope_Added;
            } else {
                rollbackItem = beginTransaction(item, TransactType_RollbackChanges);
                if (!rollbackItem)
                    return failTransaction();

                if (incomingItem.m_nAmount == 0) {
                    return failTransaction();
                }
                const std::int64_t amount = runtime::AddInt64Wrapping(item->Amount(), incomingItem.m_nAmount);
                if (incomingItem.m_nAmount > 0) {
                    item->SetAmount(amount);
                    changeScope = ItemChangeScope_Increased;
                } else if (amount < 0) {
                    return failTransaction();
                } else if (amount == 0) {
                    item->SetAmount(0);
                    item->TransactDelete();
                    changeScope = ItemChangeScope_Deleted;
                } else {
                    item->SetAmount(amount);
                    changeScope = ItemChangeScope_Decreased;
                }
            }
        }

        item->SetChangeScope(changeScope);
        CItemAssemblerItemMani itemAssembler(
            // Current Cached.exe uses the result scope as the response index.
            // the incoming request index occupies the following wire field.
            *item.get(), static_cast<int>(changeScope), incomingItem.m_nAmount,
            static_cast<ItemChangeScope>(incomingItem.m_nIndex), incomingItem.m_nGetReason,
            // incomingItem.m_nShapeShiftedPeriod, 0x7f
            incomingItem.m_nShapeShiftedPeriod, ItemAssembleType::ALL
        );
        if (itemAssembler.AssembleItemInfoImpl(itemPacket) < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     "request manipulate item char[%u] socket buffer "
            //     "overflow at [%s][%d]",
            //     m_ownerId, "WareHouse.cpp", 0x2993
            // )
            // Noise: omit the shipping line number. Retain owner and failure stage.
            // L2_LOG_ERROR(Log,
            //     "request manipulate item char[%u] socket buffer "
            //     "overflow at [%s][%d]", m_ownerId, "WareHouse.cpp", 0x2993)
            L2_LOG_ERROR(Log,
                "[items] Cannot assemble the item-manipulation reply for character %u. Entering transaction failure handling.",
                m_ownerId);
            return failTransaction();
        }
        changedItemCount = runtime::AddInt32Wrapping(changedItemCount, 1);
    }

    if (pointType == 1) {
        owner->SavePCCafePoint(resultingPoint);
    } else if (pointType != 2 && pointType != 7) {
        if (pointType == 5 || pointType == 8) {
            const int currentPoint = owner->LoadUserPoint(pointType);
            const int expectedPoint = runtime::SubtractInt32Wrapping(resultingPoint, pointDelta);
            if (expectedPoint != currentPoint) {
                // Log.Add(
                //     LOG_ERROR,
                //     "[%ls] User has point gap in "
                //     "ManipulateItemWithPVPPoint. I want [%d] now [%d] "
                //     "change [%d]",
                //     owner->GetCharName(), expectedPoint, currentPoint, pointDelta
                // )
                // L2_LOG_ERROR(Log,
                //     "[%ls] User has point gap in "
                //     "ManipulateItemWithPVPPoint. I want [%d] now [%d] "
                //     "change [%d]",
                //     owner->GetCharName(), expectedPoint, currentPoint, pointDelta
                // )
                L2_LOG_ERROR(
                    Log,
                    u"[%ls] User has point gap in "
                    u"ManipulateItemWithPVPPoint. I want [%d] now [%d] "
                    u"change [%d]",
                    owner->GetCharName(), expectedPoint, currentPoint, pointDelta
                );
            }
        }
        owner->SaveUserPoint(pointType, resultingPoint);
    }

    m_unsavedItemsPending = true;
    const bool writeCache = GetCacheDConfiguration().writeCache;
    bool pointAuditEmitted = false;
    for (const auto& entry : transactionItems) {
        CItem* rollbackItem = entry.second.rollbackItem;
        CItem* item = entry.second.item;
        pointAuditEmitted = WriteManipulatePointItemAudits(
                                pointType, pointDelta, resultingPoint, pledgeId, pledgeName, actor, *item, *rollbackItem,
                                lastGetReason, item->m_intensiveItemType
                            ) ||
            pointAuditEmitted;

        if (writeCache)
            m_unsavedItemsPending = true;
        item->CommitTransation(rollbackItem, !writeCache);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
        L2_RELEASE(item, ObjectRefType::FunctionLocal);
    }

    if (pointType == 2) {
        if (pointDelta < 0) {
            socket->Send("chdddddd", 0xc6, 0x2c, userServerId, 0, pledgeId, 0, pointDelta, -2);
        }
    } else if (pointType == 5 || pointType == 7 || pointType == 8) {
        socket->Send("chddddd", 0xc6, 0x57, static_cast<int>(m_ownerId), pointDelta, requestValue, pointType, 0);
        if (!pointAuditEmitted) {
            WriteManipulatePointWithoutItemAudit(pointType, pointDelta, resultingPoint, actor);
        }
    } else if (pointType == 1) {
        socket->Send("chdddddd", 0xc6, 0x20, static_cast<int>(m_ownerId), resultingPoint, requestValue, 1, 0, replyExtra);
    } else {
        socket->Send("chddddd", 0xc6, 0x57, static_cast<int>(m_ownerId), resultingPoint, requestValue, pointType, 0);
    }

    if (changedItemCount == 0) {
        socket->Send(
            "cddddddb", 0xc2, userServerId, atomicJobId, pointType, pointDelta, 0, static_cast<int>(etcManis.size()),
            etcManiPacket.GetLength(), etcManiPacket.GetPacket()
        );
    } else {
        socket->Send(
            "cdddddbdb", 0xc2, userServerId, atomicJobId, pointType, pointDelta, changedItemCount, itemPacket.GetLength(),
            itemPacket.GetPacket(), static_cast<int>(etcManis.size()), etcManiPacket.GetLength(), etcManiPacket.GetPacket()
        );
    }

    for (EtcMani* etcMani : etcManis) {
        etcMani->Destroy();
    }
    etcManis.clear();
    return 0;
}

bool CWareHouse::ManipulatePremiumItems(
    CSocket* socket, const std::uint8_t* packet, int requestId, std::int64_t warehouseNo
) {
    // Current Cached.exe 0x005e8a48 / WareHouse.cpp:11163-11245.
    L2_GUARD_FUNCTION();

    CItemDisassemblerItemMani incoming;
    incoming.DisassembleItemInfoImpl(packet);
    const int consumeType = static_cast<int>(incoming.m_eConsumeType);
    const bool stackablePremiumRequest = consumeType == 2 || consumeType == 3;
    if (!stackablePremiumRequest && incoming.m_nAmount != 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d][Premium] item is non-stackable but amount is not 1. "
        //     "item[%u] user[%u]",
        //     "WareHouse.cpp", 0x2b9b, incoming.m_nItemType, m_ownerId
        // )
        L2_LOG_ERROR(Log,
            "[premium items] Rejecting request %d for owner %u, item class %u: consume type %d requires quantity 1, "
            "received %lld. Requesting failure response.",
            requestId, m_ownerId, incoming.m_nItemType, consumeType, static_cast<long long>(incoming.m_nAmount));
        SendPremiumManipulationFailure(socket, requestId);
        return false;
    }

    CItemSP item = GetItemByType(static_cast<int>(incoming.m_nItemType), 0);
    ItemChangeScope changeScope = ItemChangeScope_Unknown;
    if (!item || !stackablePremiumRequest) {
        changeScope = ItemChangeScope_Added;
        const ItemOptionKey optionKey{incoming.m_nVariation1, incoming.m_nVariation2};
        item = MakeNewPremiumItem(
            warehouseNo, m_ownerId, incoming.m_nItemType, incoming.m_nAmount, incoming.m_nEnchant, incoming.m_nEroded,
            incoming.m_nBless, 0, 0, 0, optionKey, incoming.m_nIntensiveItemType, incoming.m_Attribute, incoming.m_hLocked,
            incoming.m_hUseCount
        );
        if (!item) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d][Premium] fail to make item. "
            //     "warehouseNo[%lld] item[%u] user[%u]",
            //     "WareHouse.cpp", 0x2bac, static_cast<long long>(warehouseNo), incoming.m_nItemType, m_ownerId
            // )
            L2_LOG_ERROR(Log,
                "[premium items] Item creation failed for request %d, owner %u, warehouse %lld, item class %u, quantity %lld. "
                "Requesting failure response.",
                requestId, m_ownerId, static_cast<long long>(warehouseNo), incoming.m_nItemType,
                static_cast<long long>(incoming.m_nAmount));
            SendPremiumManipulationFailure(socket, requestId);
            return false;
        }

        const int reason = warehouseNo > 0 ? 0 : 2;
        item->LogPremiumRelated(
            static_cast<PremiumItemLogType>(0), 0, incoming.m_nAmount, static_cast<PremiumItemLogReason>(reason)
        );
        PushItem(item.get());
    } else {
        changeScope = ItemChangeScope_Increased;
        int result = 0;
        const int itemId = static_cast<int>(item->ItemID());
        const int ownerId = static_cast<int>(m_ownerId);
        const long long sqlWarehouseNo = static_cast<long long>(warehouseNo);
        const long long sqlAmount = static_cast<long long>(incoming.m_nAmount);
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Bind(&result);
        constexpr const wchar_t* statement = L"{Call lin_WithdrawPremiumItem (?,?,?,?)}";
        connection.Prepare(statement);
        connection.BindParameter(&itemId);
        connection.BindParameter(&ownerId);
        connection.BindParameter(&sqlWarehouseNo);
        connection.BindParameter(&sqlAmount);
        if (!connection.ExecutePrepared(statement) || !connection.Fetch() || result == 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d][Premium] fail to withdraw item. "
            //     "warehouseNo[%lld] item[%u] user[%u]",
            //     "WareHouse.cpp", 0x2bbf, static_cast<long long>(warehouseNo), incoming.m_nItemType, m_ownerId
            // )
            L2_LOG_ERROR(Log,
                "[premium items] Withdrawal failed for request %d, owner %u, warehouse %lld, item %d, class %u, quantity %lld: "
                "database execution/fetch failed or the procedure returned zero. Requesting failure response.",
                requestId, m_ownerId, static_cast<long long>(warehouseNo), itemId, incoming.m_nItemType,
                static_cast<long long>(incoming.m_nAmount));
            SendPremiumManipulationFailure(socket, requestId);
            return false;
        }

        const std::int64_t amountBefore = item->Amount();
        const std::int64_t amountAfter = runtime::AddInt64Wrapping(amountBefore, incoming.m_nAmount);
        const int reason = warehouseNo > 0 ? 0 : 2;
        item->LogPremiumRelated(
            static_cast<PremiumItemLogType>(0), amountBefore, amountAfter, static_cast<PremiumItemLogReason>(reason)
        );
        item->SetAmount(amountAfter);
    }

    CPacketHelperEx<20480> response;
    CItemAssemblerItemMani assembler(
        // Current Cached.exe 0x005e90c8 inlines the same field order:
        // result index at +0xd0, requested amount at +0xd8, then the
        // incoming request index at +0xe0.
        *item.get(), static_cast<int>(changeScope), incoming.m_nAmount,
        static_cast<ItemChangeScope>(incoming.m_nIndex), incoming.m_nGetReason,
        // incoming.m_nShapeShiftedPeriod, 3
        incoming.m_nShapeShiftedPeriod, (ItemAssembleType::VAIRATION | ItemAssembleType::ATTRIBUTE)
    );
    if (assembler.AssembleItemInfoImpl(response) < 1) {
        if (changeScope == ItemChangeScope_Added) {
            PopItem(static_cast<int>(item->ItemID()));
        } else if (changeScope == ItemChangeScope_Increased) {
            item->SetAmount(runtime::SubtractInt64Wrapping(item->Amount(), incoming.m_nAmount));
        }
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d][Premium] fail to assemble reply. "
        //     "warehouseNo[%lld] item[%u] user[%u]",
        //     "WareHouse.cpp", 0x2bdc, static_cast<long long>(warehouseNo), incoming.m_nItemType, m_ownerId
        // )
        L2_LOG_ERROR(Log,
            "[premium items] Cannot assemble response for request %d, owner %u, warehouse %lld, item class %u, quantity %lld. "
            "Local item change reversal was attempted; database changes are not rolled back by this branch. "
            "Requesting failure response.",
            requestId, m_ownerId, static_cast<long long>(warehouseNo), incoming.m_nItemType,
            static_cast<long long>(incoming.m_nAmount));
        SendPremiumManipulationFailure(socket, requestId);
        return false;
    }

    item->Save(true);
    item->SaveBaseAttribute();
    socket->Send(
        "chdQQb", 0xc6, 0x5f, requestId, static_cast<long long>(warehouseNo), static_cast<long long>(incoming.m_nAmount),
        response.GetLength(), response.GetPacket()
    );

    CUserSP user = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    WritePremiumManipulationAudit(m_ownerId, user.get(), *item.get(), incoming.m_nAmount, warehouseNo);
    return true;
}

bool CWareHouse::hLockedItem(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x005e9dc4.
    L2_GUARD_FUNCTION();

    int userDbId = 0;
    std::uint8_t flag = 0;
    int sourceItemId = 0;
    int sourceServerToken = 0;
    int targetItemId = 0;
    int targetServerToken = 0;
    ::Disassemble(packet, "dcdddd", &userDbId, &flag, &sourceItemId, &sourceServerToken, &targetItemId, &targetServerToken);

    CItemTransactionOrderMap transactionItems;
    CItemSP sourceItem = GetItem(sourceItemId);
    CItemSP targetItem = GetItem(targetItemId);
    if (!sourceItem || !targetItem) {
        CTransaction::RollbackTransactOrderMap(transactionItems);
        socket->Send("chdcc", 0xc6, 302, userDbId, 0, flag);
        return false;
    }

    sourceItem->SetTransation();
    CItemSP sourceRollback = sourceItem->Copy();
    sourceRollback->SetTrasactMode(TransactType_RollbackChanges);
    L2_ADD_REF(sourceItem, ObjectRefType::FunctionLocal);
    L2_ADD_REF(sourceRollback, ObjectRefType::FunctionLocal);
    transactionItems.emplace(1, CItemKey(sourceRollback.get(), sourceItem.get()));

    targetItem->SetTransation();
    CItemSP targetRollback = targetItem->Copy();
    targetRollback->SetTrasactMode(TransactType_RollbackChanges);
    L2_ADD_REF(targetItem, ObjectRefType::FunctionLocal);
    L2_ADD_REF(targetRollback, ObjectRefType::FunctionLocal);
    transactionItems.emplace(2, CItemKey(targetRollback.get(), targetItem.get()));

    targetItem->m_hLocked = flag == 0 ? 1 : 0;
    sourceItem->SetAmount(runtime::SubtractInt64Wrapping(sourceItem->Amount(), 1));
    if (sourceItem->Amount() < 1) {
        sourceItem->TransactDelete();
    }

    CPacketHelperEx<20480> response;
    response.Assemble("cdd", flag, sourceServerToken, targetServerToken);
    for (const auto& entry : transactionItems) {
        CItem* const rollbackItem = entry.second.rollbackItem;
        CItem* const changedItem = entry.second.item;
        changedItem->CommitTransation(rollbackItem, true);
        L2_RELEASE(changedItem, ObjectRefType::FunctionLocal);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
    }
    socket->Send("chddb", 0xc6, 302, userDbId, 1, response.GetLength(), response.GetPacket());

    CUserSP user = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        WriteLockedItemAudit(*user.get(), *targetItem.get(), sourceItemId, targetItemId, flag);
    }
    return true;
}

void CWareHouse::SaveInventorySlotIndex(const std::uint8_t* packet) {
    // Current Cached.exe 0x005d0d28. The whole counted item lookup/update
    // loop is protected by the warehouse write lock. Missing item IDs are
    // ignored.
    L2_GUARD_FUNCTION();

    int count = 0;
    int itemId = 0;
    int slotIndex = 0;
    {
        L2_WRITE_GUARD(*this);
        const std::uint8_t* cursor = ::Disassemble(packet, "d", &count);
        for (int index = 0; index < count; ++index) {
            cursor = ::Disassemble(cursor, "dd", &itemId, &slotIndex);
            CItemSP item = GetItem(itemId);
            if (item)
                item->SetInventorySlotIndex(slotIndex);
        }
    }
}

// These two semantic callback names stand for the inlined
// FOR_ALL_OBJECT_IN_OBJECT_MAP bodies. The current image has no recoverable
// source identifier for either callback carrier.
class CSaveUnsavedItemInWareHouse {
public:
    bool Do(CItem* item) {
        if (item->m_writeCachePending) {
            item->Save(true);
        }
        return true;
    }
};

class CSaveLateSavingItemInWareHouse {
public:
    bool Do(CItem* item) {
        if (item->m_lateSavingAmount <= 0)
            return true;

        const std::int64_t pendingAmount = item->m_lateSavingAmount;
        const std::int64_t databaseAmount = item->ChangeAmount(pendingAmount);
        // Current 0x005d4020 clears +0x78 after the call even when SQL fails.
        item->m_lateSavingAmount = 0;

        if (databaseAmount < 0) {
            // Log.Add(LOG_ERROR,
            //     "[warehouse] Deferred quantity update for item %u (owner %u, database %d) returned %lld. Pending deduction %lld was cleared without updating the cached quantity.",
            //     item->ItemID(), item->OwnerID(), runtime::process::CurrentDatabaseId(), static_cast<long long>(databaseAmount), static_cast<long long>(pendingAmount))
            L2_LOG_ERROR(Log,
                "[warehouse] Deferred quantity update for item %u (owner %u, database %d) returned %lld; pending deduction %lld was cleared without updating the cached quantity.",
                item->ItemID(), item->OwnerID(), runtime::process::CurrentDatabaseId(), static_cast<long long>(databaseAmount), static_cast<long long>(pendingAmount));
        }

        if (databaseAmount == 0) {
            m_zeroAmountItems.push_back(item->ItemID());
        } else if (databaseAmount > 0) {
            if (databaseAmount != item->Amount()) {
                // Log.Add(
                //     LOG_ERROR,
                //     "[WareHouse.cpp][%d] caching amount differ from DB "
                //     "[%lld:%lld]",
                //     0x2089, static_cast<long long>(databaseAmount), static_cast<long long>(item->Amount())
                // )
                // Log.Add(LOG_WARN,
                //     "[warehouse] Deferred save for item %u (owner %u) returned database quantity %lld instead of cached %lld. Replacing the cached quantity with the database result.",
                //     item->ItemID(), item->OwnerID(), static_cast<long long>(databaseAmount), static_cast<long long>(item->Amount()))
                L2_LOG_WARN(Log,
                    "[warehouse] Deferred save for item %u (owner %u) returned database quantity %lld instead of cached %lld; replacing the cached quantity with the database result.",
                    item->ItemID(), item->OwnerID(), static_cast<long long>(databaseAmount), static_cast<long long>(item->Amount()));
            }
            item->SetAmount(databaseAmount);
        }
        return true;
    }

    const std::vector<std::uint32_t>& ZeroAmountItems() const noexcept { return m_zeroAmountItems; }

private:
    // The Windows body calls PopItem while its object-map read lock is held.
    // The target RWLock cannot upgrade that semaphore recursively. Deferring
    // only the erases until the identical traversal has released its lock is
    // the cross-platform deadlock-free transport for the same final state.
    std::vector<std::uint32_t> m_zeroAmountItems;
};

L2_DEFINE_OBJECT_CLASS_ID(CWareHouse)

L2_DEFINE_OBJECT_CLASS_NAME(CWareHouse, m_pClassName_CWareHouse)

L2_DEFINE_OBJECT_NEW(CWareHouse)

// Current allocator 0x0049e644 publishes a 0xc0-byte class. CRT
// initializer 0x006a8210 assigns class ID 31.
L2_DEFINE_OBJECT_ALLOCATOR(CWareHouse, m_pClassName_CWareHouse)

// Current folded owner 0x004a0d28 is the exact MemoryObject release
// sequence specialized with class ID 31 and WareHouse.h line 0x7b.
L2_DEFINE_OBJECT_RELEASE(CWareHouse)

CWareHouse::CWareHouse() {
    // There is no standalone current constructor: heap allocations are
    // zeroed, then the inlined owner constructs the base, lock and one-bucket
    // map. Production owners call SetOwnerId followed by Init before use.
}

CWareHouse::~CWareHouse() {
    L2_GUARD_FUNCTION();
    DeleteAllItem(false);
}

void CWareHouse::Init(WareHouseType warehouseType, bool loadItems) {
    // Exact current helper 0x00610dd0. Its 16-bit zeroing store covers both
    // m_itemsLoaded and the aggregate unsaved-items flag.
    m_warehouseType = warehouseType;
    m_itemNumber = 0;
    m_itemsLoaded = false;
    m_unsavedItemsPending = false;
    m_lateSavingItemsPending = false;
    if (loadItems)
        LoadItems();
}

CItemSP CWareHouse::GetItemByType(int itemType, int npcId) {
    L2_GUARD_FUNCTION();
    CFindItemByTypeInWareHouse predicate(itemType, npcId, m_warehouseType);
    m_items.FOR_ALL(&predicate);
    return predicate.TakeResult();
}

CItemSP CWareHouse::GetItem(int itemId) {
    L2_GUARD_FUNCTION();
    CItemSP item = m_items.FindObject(itemId);
    if (!item || item->Warehouse() != GetWarehouseNum())
        return {};
    return item;
}

CItemSP CWareHouse::GetAdena() {
    L2_GUARD_FUNCTION();
    GetAdenaPredicate predicate;
    m_items.FOR_ALL(&predicate, L"WareHouse.cpp", 0x9f6);
    return predicate.TakeResult();
}

bool CWareHouse::AddAdena(std::int64_t amount) {
    // Current Cached.exe 0x005f594c. A zero amount succeeds without touching
    // the warehouse. Any positive amount delegates to AddAssetItem and the
    // returned pointer does not affect this bool result.
    L2_GUARD_FUNCTION();
    if (amount < 0)
        return false;
    if (amount != 0) {
        AddAssetItem(57, amount);
    }
    return true;
}

bool CWareHouse::DelAdena(std::int64_t amount) {
    // Current Cached.exe 0x005d444c. The current body expands its object-map
    // traversal in place instead of calling GetAdena, and therefore keeps
    // this separate source line in the generated FOR_ALL guard.
    L2_GUARD_FUNCTION();
    if (amount < 0)
        return false;
    if (amount == 0)
        return true;

    GetAdenaPredicate predicate;
    m_items.FOR_ALL(&predicate, L"WareHouse.cpp", 0x3cdb);
    CItemSP adena = predicate.TakeResult();
    if (!adena)
        return false;

    const std::int64_t currentAmount = adena->Amount();
    if (currentAmount == amount) {
        PopItem(static_cast<int>(adena->ItemID()));
        adena->Delete(true, false);
        return true;
    }
    if (currentAmount <= amount)
        return false;

    adena->SetAmount(currentAmount - amount);
    adena->Save(true);
    return true;
}

CWareHouse::DeleteResult CWareHouse::_DeleteAssetOrStackableItem(
    int itemType, std::int64_t amount, unsigned int& itemId, std::int64_t& remainingAmount
) {
    // Current Cached.exe 0x005d5360. The PDB keeps the method and nested
    // return type but not the enum-value identifiers, hence the numeric casts.
    L2_GUARD_FUNCTION();
    if (amount < 1) {
        return static_cast<DeleteResult>(2);
    }

    // Current file-local helper 0x00524f5c calls the 0x00524e00 definition
    // and accepts exactly Shared values 2 (stackable) and 3 (asset).
    const ItemConsumeType consumeType = g_readFile.GetItemConsumeType(itemType);
    if (consumeType != ICT_STACKABLE && consumeType != ICT_ASSET) {
        return static_cast<DeleteResult>(1);
    }

    ItemByTypePredicate predicate(itemType);
    m_items.FOR_ALL(&predicate, L"WareHouse.cpp", 0x576e);
    CItemSP item = predicate.TakeResult();
    if (!item) {
        return static_cast<DeleteResult>(3);
    }

    const std::int64_t currentAmount = item->Amount();
    if (currentAmount == amount) {
        PopItem(static_cast<int>(item->ItemID()));
        item->Delete(true, false);
        itemId = item->ItemID();
        remainingAmount = 0;
        return static_cast<DeleteResult>(0);
    }
    if (currentAmount <= amount) {
        return static_cast<DeleteResult>(3);
    }

    item->SetAmount(currentAmount - amount);
    item->Save(true);
    itemId = item->ItemID();
    remainingAmount = item->Amount();
    return static_cast<DeleteResult>(0);
}

std::uint32_t CWareHouse::PingTest() {
    // Current Cached.exe 0x005d4320 measures only the adena lookup/save
    // section. Unlike the C1 donor it returns elapsed milliseconds rather
    // than an epoch timestamp.
    L2_GUARD_FUNCTION();
    const std::uint32_t begin = runtime::GetTickCount();
    CItemSP adena = GetAdena();
    if (adena) {
        adena->Save(true);
    }
    return runtime::GetTickCount() - begin;
}

void CWareHouse::SendMonRaceTicketList(CSocket* socket, int command, int characterId, int userSocketId, int page) {
    // Current Cached.exe 0x005d47a4. Pagination counts only the two ticket
    // item types and advances of packet assembly success.
    L2_GUARD_FUNCTION();
    SendMonRaceTicketListPredicate predicate(page);
    m_items.FOR_ALL(&predicate, L"WareHouse.cpp", 0x3ed1);
    socket->Send(
        "cdddddb", 0xa1, command, characterId, userSocketId, predicate.ItemCount(), predicate.Page(),
        predicate.PacketBytes(), predicate.Packet()
    );
}

bool CWareHouse::CheckMonRaceTicket(unsigned int itemId) {
    // Current Cached.exe 0x005d4bc0 traverses the warehouse map
    // and stops at the first matching 0x115b/0x115c object.
    L2_GUARD_FUNCTION();
    CheckMonRaceTicketPredicate predicate(itemId);
    m_items.FOR_ALL(&predicate, L"WareHouse.cpp", 0x3ef2);
    return predicate.Found();
}

bool CWareHouse::DelMonRaceTicketFromDeposit(unsigned int itemId) {
    // Current Cached.exe 0x005d4e48 mutates and persists the object while the
    // map read lock is held, then removes the object from the map afterward.
    L2_GUARD_FUNCTION();
    DeleteMonRaceTicketFromDepositPredicate predicate(itemId);
    m_items.FOR_ALL(&predicate, L"WareHouse.cpp", 0x3f14);
    if (predicate.Found()) {
        PopItem(predicate.FoundItemId());
    }
    return predicate.Found();
}

void CWareHouse::DelMonRaceTicketFromDB(unsigned int itemId) {
    // Current Cached.exe 0x005f5a38.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DelMonRaceTicket (?  )}";
    connection.Prepare(statement);
    connection.BindParameter(&itemId);
    connection.ExecutePrepared(statement);
}

void CWareHouse::DelMonRaceTicketWithoutReward(unsigned int itemId) {
    // Current Cached.exe 0x005f5b50 calls both owners unconditionally.
    L2_GUARD_FUNCTION();
    DelMonRaceTicketFromDeposit(itemId);
    DelMonRaceTicketFromDB(itemId);
}

std::int64_t CWareHouse::DelMonRaceTicket(unsigned int itemId, int currentRaceId, int allowCurrentRace) {
    // Current Cached.exe 0x005f5c00. The third argument is the stage-derived
    // current-race redemption flag sent by current L2Server 0x0064e340.
    L2_GUARD_FUNCTION();

    int ticketId = 0;
    int ticketRaceId = 0;
    int betType = 0;
    int bet1 = 0;
    int bet2 = 0;
    int bet3 = 0;
    std::int64_t betMoney = 0;
    double winRate1 = 0.0;
    double winRate2 = 0.0;
    int winner1 = 0;
    int winner2 = 0;
    int raceEnd = 0;
    std::int64_t taxMoney = 0;
    std::int64_t remoteFee = 0;
    std::int64_t nullableIndicator = -1;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.BindNullable(&ticketId, &nullableIndicator);
    connection.BindNullable(&ticketRaceId, &nullableIndicator);
    connection.BindNullable(&betType, &nullableIndicator);
    connection.BindNullable(&bet1, &nullableIndicator);
    connection.BindNullable(&bet2, &nullableIndicator);
    connection.BindNullable(&bet3, &nullableIndicator);
    connection.BindNullable(&betMoney, &nullableIndicator);
    connection.BindNullable(&winRate1, &nullableIndicator);
    connection.BindNullable(&winRate2, &nullableIndicator);
    connection.BindNullable(&winner1, &nullableIndicator);
    connection.BindNullable(&winner2, &nullableIndicator);
    connection.BindNullable(&raceEnd, &nullableIndicator);
    connection.BindNullable(&taxMoney, &nullableIndicator);
    connection.BindNullable(&remoteFee, &nullableIndicator);

    constexpr const wchar_t* statement = L"{Call lin_GetMonRaceTicket (?  )}";
    connection.Prepare(statement);
    connection.BindParameter(&itemId);
    if (connection.ExecutePrepared(statement) && connection.Fetch() && ticketRaceId == currentRaceId &&
        allowCurrentRace == 0) {
        return 0;
    }

    if (!DelMonRaceTicketFromDeposit(itemId))
        return 0;

    std::int64_t calculatedReward = 0;
    bool winningTicket = false;
    if (raceEnd == 1) {
        if (betType == 1 && bet1 == winner1) {
            calculatedReward = runtime::Win64CvttDoubleToInt64(
                static_cast<double>(betMoney) * winRate1);
            winningTicket = true;
        } else if (betType == 2) {
            const int lowBet = std::min(bet1, bet2);
            const int highBet = std::max(bet1, bet2);
            const int lowWinner = std::min(winner1, winner2);
            const int highWinner = std::max(winner1, winner2);
            if (lowBet == lowWinner && highBet == highWinner) {
                calculatedReward = runtime::Win64CvttDoubleToInt64(
                    static_cast<double>(betMoney) * winRate2);
                winningTicket = true;
            }
        }
    } else if (currentRaceId > ticketRaceId || currentRaceId == 0) {
        calculatedReward = runtime::AddInt64Wrapping(runtime::AddInt64Wrapping(remoteFee, taxMoney), betMoney);
        winningTicket = true;
    }

    // The imul/high-word/sar sequence at 0x005f5f03 is MSVC's signed
    // division-by-100 lowering. It is also selected when a nominal winning
    // or refund path computes exactly zero.
    if (!winningTicket || calculatedReward == 0) {
        calculatedReward = betMoney / 100;
    }

    std::int64_t creditedReward = 0;
    if (calculatedReward > 0) {
        AddAdena(calculatedReward);
        creditedReward = calculatedReward;
    }
    DelMonRaceTicketFromDB(itemId);

    CUserSP user = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        const int selection = betType == 1 ? bet1 : runtime::AddInt32Wrapping(runtime::MultiplyInt32Wrapping(bet1, 100), bet2);
        WriteMonRaceTicketRedemptionAudit(
            MakeWareHouseAuditActor(user.get()), ticketRaceId, betType, selection, betMoney, calculatedReward, itemId
        );
    }
    return creditedReward;
}

bool CWareHouse::RemoveEquipped(int itemId) {
    // Current Cached.exe 0x005cfcf8. The target scans the complete
    // contiguous 60-dword CUser equipment block, clears every occurrence,
    // and saves even when no slot matched.
    L2_GUARD_FUNCTION();

    CUserSP user = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    std::array<int, 60> equipment{};
    static_assert(sizeof(equipment) == sizeof(user->m_equipment));
    std::memcpy(equipment.data(), &user->m_equipment, sizeof(equipment));
    for (int& equippedItemId : equipment) {
        if (equippedItemId == itemId)
            equippedItemId = 0;
    }
    std::memcpy(&user->m_equipment, equipment.data(), sizeof(equipment));
    user->Save();
    return true;
}

bool CWareHouse::ModifyItem(
    int itemId,
    unsigned int itemType,
    std::int64_t amount,
    int enchant,
    int eroded,
    int bless,
    int ident,
    int wished,
    int warehouse,
    ItemOptionKey optionKey,
    int intensiveItemType,
    BaseAttribute baseAttribute,
    short hLocked,
    short useCount
) {
    // Exact current Cached.exe 0x005cf18c / WareHouse.cpp:0x915. Normal and
    // charge items are forced to one before lookup. The existing owner ID and
    // appearance survive. Every other supplied creation field is replaced.
    L2_GUARD_FUNCTION();

    std::optional<CItemSP> itemStorage;
    {
        L2_WRITE_GUARD(*this);
        const int consumeType = g_readFile.GetConsumeType(static_cast<int>(itemType));
        if (consumeType == 1 || consumeType == 3) {
            amount = 1;
        }

        auto& item = itemStorage.emplace(GetItem(itemId));
        if (!item) {
            return false;
        }

        item->Set(
            item->OwnerID(), itemType, amount, enchant, eroded, bless, ident, wished, warehouse, optionKey, intensiveItemType,
            baseAttribute, -1, item->AppearanceType(), hLocked, useCount
        );
        item->Save(true);
    }
    return true;
}

bool CWareHouse::DeleteItem(int itemId, std::int64_t amount) {
    // Current Cached.exe 0x005d1e7c. Consume values are the current
    // CReadFile values: 1 normal, 2 asset, 3 charge and 4 stackable.
    L2_GUARD_FUNCTION();

    int itemType = 0;
    std::int64_t remainingAmount = 0;
    std::u16string baseAttributeLog;
    std::u16string variationLog;
    bool deleted = false;

    std::optional<CItemSP> itemStorage;
    {
        L2_WRITE_GUARD(*this);
        auto& item = itemStorage.emplace(GetItem(itemId));
        if (item) {
            itemType = static_cast<int>(item->ItemType());

            std::array<char16_t, 256> baseAttributeText{};
            BaseAttribute baseAttribute(item->Attribute());
            baseAttribute.GetLogString(baseAttributeText.data());
            baseAttributeLog = std::u16string(baseAttributeText.data());
            variationLog = MakeWareHouseVariationLogText(
                item->m_intensiveItemType, item->m_optionKey.variationOption1, item->m_optionKey.variationOption2
            );

            const auto deleteWholeItem = [&]() {
                PopItem(itemId);
                item->setOwnerID(0);
                item->Save(true);
                item->Delete(false, false);
                RemoveEquipped(itemId);
                remainingAmount = 0;
                deleted = true;
            };

            const std::int64_t currentAmount = item->Amount();
            switch (g_readFile.GetConsumeType(itemType)) {
            case 1:
            case 3:
                deleteWholeItem();
                break;
            case 2:
            case 4:
                if (amount == 0 || amount == currentAmount) {
                    deleteWholeItem();
                } else if (amount < currentAmount) {
                    remainingAmount = runtime::SubtractInt64Wrapping(currentAmount, amount);
                    item->SetAmount(remainingAmount);
                    item->Save(true);
                    deleted = true;
                }
                break;
            default:
                break;
            }
        }
    }

    if (deleted) {
        CUserSP user = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
        WriteWarehouseDeleteItemAudit(
            MakeWareHouseAuditActor(user.get()), baseAttributeLog, variationLog, itemType, amount, remainingAmount, itemId
        );
    }
    return deleted;
}

CItemSP CWareHouse::PopItem(int itemId) {
    L2_GUARD_FUNCTION();
    CItemSP item = m_items.FindObject(itemId);
    if (!item) {
        // Log.Add(LOG_ERROR, "[%s][%d] Cannot find item.[%d]", "WareHouse.cpp", 0x370, itemId)
        // Log.Add(LOG_ERROR,
        //     "[warehouse] Cannot remove item %d from the cache of owner %u, warehouse %d: item not found.",
        //     itemId, m_ownerId, static_cast<int>(m_warehouseType))
        L2_LOG_ERROR(Log,
            "[warehouse] Cannot remove item %d from the cache of owner %u, warehouse %d: item not found.",
            itemId, m_ownerId, static_cast<int>(m_warehouseType));
        return item;
    }
    m_items.DeleteObject(itemId, nullptr, -1);
    --m_itemNumber;
    return item;
}

void CWareHouse::DeleteItemBySP(CItemSP item) {
    L2_GUARD_FUNCTION();

    PopItem(static_cast<int>(item->ItemID()));
    item->Delete(true, false);
}

bool CWareHouse::PushItem(CItem* item) {
    L2_GUARD_FUNCTION();
    if (!item) {
        // Log.Add(LOG_ERROR, "[%s][%d] Push null item.", "WareHouse.cpp", 0x38e)
        // Log.Add(LOG_ERROR,
        //     "[warehouse] Cannot add an item to the cache of owner %u, warehouse %d: item is null.",
        //     m_ownerId, static_cast<int>(m_warehouseType))
        L2_LOG_ERROR(Log,
            "[warehouse] Cannot add an item to the cache of owner %u, warehouse %d: item is null.",
            m_ownerId, static_cast<int>(m_warehouseType));
        return false;
    }

    // Current 0x005d7328 ignores an already-present key.
    m_items.Insert(static_cast<int>(item->ItemID()), item, nullptr, -1);
    ++m_itemNumber;
    return true;
}

CItemSP CWareHouse::MakeNewItem(
    unsigned int ownerId,
    unsigned int itemType,
    std::int64_t amount,
    int enchant,
    int eroded,
    int bless,
    int ident,
    int wished,
    int warehouse,
    const ItemOptionKey& optionKey,
    int intensiveItemType,
    const BaseAttribute& baseAttribute,
    short hLocked,
    short useCount
) {
    L2_GUARD_FUNCTION();

    if (ownerId != 0 && ownerId != m_ownerId) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] Warehouse owner is invalid. owner[%d] maker[%d] "
        //     "item[%d]",
        //     "WareHouse.cpp", 0x770, static_cast<int>(m_ownerId), static_cast<int>(ownerId), static_cast<int>(itemType)
        // )
        // Log.Add(LOG_ERROR,
        //     "[warehouse] Cannot create item class %u for owner %u in warehouse %d: this warehouse belongs to owner %u.",
        //     itemType, ownerId, warehouse, m_ownerId)
        L2_LOG_ERROR(Log,
            "[warehouse] Cannot create item class %u for owner %u in warehouse %d: this warehouse belongs to owner %u.",
            itemType, ownerId, warehouse, m_ownerId);
        return {};
    }
    if (amount < 0) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] item amount is zero. owner[%d] maker[%d] item[%d]", "WareHouse.cpp", 0x778,
        //     static_cast<int>(m_ownerId), static_cast<int>(ownerId), static_cast<int>(itemType)
        // )
        // Log.Add(LOG_ERROR,
        //     "[warehouse] Cannot create item class %u for owner %u in warehouse %d: quantity %lld is negative.",
        //     itemType, ownerId, warehouse, static_cast<long long>(amount))
        L2_LOG_ERROR(Log,
            "[warehouse] Cannot create item class %u for owner %u in warehouse %d: quantity %lld is negative.",
            itemType, ownerId, warehouse, static_cast<long long>(amount));
        return {};
    }

    unsigned int itemId = 0;
    const long long sqlAmount = static_cast<long long>(amount);
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&itemId);
    constexpr const wchar_t* statement = L"{Call lin_CreateItem (?,?,?,?,?,?,?,?,?,?,?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&ownerId);
    connection.BindParameter(&itemType);
    connection.BindParameter(&sqlAmount);
    connection.BindParameter(&enchant);
    connection.BindParameter(&eroded);
    connection.BindParameter(&bless);
    connection.BindParameter(&ident);
    connection.BindParameter(&wished);
    connection.BindParameter(&warehouse);
    connection.BindParameter(&optionKey.variationOption1);
    connection.BindParameter(&optionKey.variationOption2);
    connection.BindParameter(&intensiveItemType);
    connection.BindParameter(&hLocked);
    connection.BindParameter(&useCount);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
        // Log.Add(LOG_ERROR,
        //     "[warehouse] No creation result was obtained for item class %u, owner %u, quantity %lld, warehouse %d, database %d. No item object returned.",
        //     itemType, ownerId, static_cast<long long>(amount), warehouse, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[warehouse] No creation result was obtained for item class %u, owner %u, quantity %lld, warehouse %d, database %d; no item object returned.",
            itemType, ownerId, static_cast<long long>(amount), warehouse, runtime::process::CurrentDatabaseId());
        return {};
    }

    CItem* item = new CItem(itemId);
    CItemSP result = item->Set(
        ownerId, itemType, amount, enchant, eroded, bless, ident, wished, warehouse, optionKey, intensiveItemType,
        baseAttribute, -1, 0, hLocked, useCount
    );
    item->InitExtraInfo();
    L2_RELEASE(item, ObjectRefType::CreateDelete);
    return result;
}

CItemSP CWareHouse::MakeNewPremiumItem(
    std::int64_t warehouseNo,
    unsigned int ownerId,
    unsigned int itemType,
    std::int64_t amount,
    int enchant,
    int eroded,
    int bless,
    int ident,
    int wished,
    int warehouse,
    const ItemOptionKey& optionKey,
    int intensiveItemType,
    const BaseAttribute& baseAttribute,
    short hLocked,
    short useCount
) {
    // Current Cached.exe 0x005d8c8c / WareHouse.cpp:1954-1973.
    L2_GUARD_FUNCTION();

    if (m_ownerId != ownerId) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] invalid owner. member[%u] param[%u]", "WareHouse.cpp", 0x7a2, m_ownerId, ownerId
        // )
        // Log.Add(LOG_ERROR,
        //     "[warehouse] Cannot create premium item class %u for owner %u (receipt warehouse %lld): this warehouse belongs to owner %u.",
        //     itemType, ownerId, static_cast<long long>(warehouseNo), m_ownerId)
        L2_LOG_ERROR(Log,
            "[warehouse] Cannot create premium item class %u for owner %u (receipt warehouse %lld): this warehouse belongs to owner %u.",
            itemType, ownerId, static_cast<long long>(warehouseNo), m_ownerId);
        return {};
    }

    unsigned int itemId = 0;
    const long long sqlAmount = static_cast<long long>(amount);
    const long long sqlWarehouseNo = static_cast<long long>(warehouseNo);
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&itemId);
    constexpr const wchar_t* statement = L"{Call lin_CreatePremiumItem (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&ownerId);
    connection.BindParameter(&itemType);
    connection.BindParameter(&sqlAmount);
    connection.BindParameter(&enchant);
    connection.BindParameter(&eroded);
    connection.BindParameter(&bless);
    connection.BindParameter(&ident);
    connection.BindParameter(&wished);
    connection.BindParameter(&warehouse);
    connection.BindParameter(&optionKey.variationOption1);
    connection.BindParameter(&optionKey.variationOption2);
    connection.BindParameter(&intensiveItemType);
    connection.BindParameter(&hLocked);
    connection.BindParameter(&useCount);
    connection.BindParameter(&sqlWarehouseNo);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch() || itemId == 0) {
        // Log.Add(LOG_ERROR,
        //     "[warehouse] No nonzero creation ID was obtained for premium item class %u, owner %u, quantity %lld, warehouse %d, receipt warehouse %lld, database %d. No item object returned.",
        //     itemType, ownerId, static_cast<long long>(amount), warehouse, static_cast<long long>(warehouseNo), runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[warehouse] No nonzero creation ID was obtained for premium item class %u, owner %u, quantity %lld, warehouse %d, receipt warehouse %lld, database %d; no item object returned.",
            itemType, ownerId, static_cast<long long>(amount), warehouse, static_cast<long long>(warehouseNo), runtime::process::CurrentDatabaseId());
        return {};
    }

    CItem* item = new CItem(
        itemId, ownerId, itemType, amount, enchant, eroded, bless, ident, wished, warehouse, optionKey, intensiveItemType,
        baseAttribute, -1, 0, hLocked, useCount
    );
    CItemSP result(item, ObjectRefType::FunctionChain);
    item->InitExtraInfo();
    L2_RELEASE(item, ObjectRefType::CreateDelete);
    return result;
}

CItemSP CWareHouse::AddAssetItem(int itemType, std::int64_t amount) {
    // Current Cached.exe 0x005d9038. The object-map traversal and its source
    // line are compiled directly in this owner instead of via GetItemByType.
    L2_GUARD_FUNCTION();
    if (amount < 1)
        return {};

    ItemByTypePredicate predicate(itemType);
    m_items.FOR_ALL(&predicate, L"WareHouse.cpp", 0x7d3);
    CItemSP item = predicate.TakeResult();
    if (item) {
        item->SetAmount(runtime::AddInt64Wrapping(item->Amount(), amount));
        item->Save(true);
        return item;
    }

    const ItemOptionKey optionKey{};
    const BaseAttribute baseAttribute{};
    CItemSP newItem = MakeNewItem(
        m_ownerId, static_cast<unsigned int>(itemType), amount, 0, 0, 0, 0, 0, static_cast<int>(m_warehouseType), optionKey,
        0, baseAttribute, 0, 0
    );
    if (newItem) {
        PushItem(newItem.get());
    }
    return newItem;
}

bool CWareHouse::CreatePet(
    int collar, int npcClassId, std::int64_t experience, int enchant, double hp, double mp, int meal, int& itemId, int eroded
) {
    // Current Cached.exe 0x005eda84.
    L2_GUARD_FUNCTION();

    const ItemOptionKey optionKey{};
    const BaseAttribute baseAttribute{};
    CItemSP collarItem = MakeNewItem(
        m_ownerId, static_cast<unsigned int>(collar), 1, enchant, eroded, 0, 0, 0, 0, optionKey, 0, baseAttribute, 0, 0
    );
    if (!collarItem) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] CreatePet Failed (Create Collar Failed) "
        //     "collar[%d] npc[%d]",
        //     "WareHouse.cpp", 0x2f94, collar, npcClassId
        // )
        // Log.Add(LOG_ERROR,
        //     "[pet] Cannot create pet class %d for owner %u: collar item class %d could not be created.",
        //     npcClassId, m_ownerId, collar)
        L2_LOG_ERROR(Log,
            "[pet] Cannot create pet class %d for owner %u: collar item class %d could not be created.",
            npcClassId, m_ownerId, collar);
        return false;
    }

    const unsigned int createdItemId = collarItem->ItemID();
    const long long sqlExperience = static_cast<long long>(experience);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_CreatePet (?, ?, ?, ?, ?, ?, 0)}";
    connection.Prepare(statement);
    connection.BindParameter(&createdItemId);
    connection.BindParameter(&npcClassId);
    connection.BindParameter(&sqlExperience);
    connection.BindParameter(&hp);
    connection.BindParameter(&mp);
    connection.BindParameter(&meal);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] CreatePet Failed (Create pet failed) "
        //     "collar[%d] npc[%d]",
        //     "WareHouse.cpp", 0x2f9c, collar, npcClassId
        // )
        // Log.Add(LOG_ERROR,
        //     "[pet] Pet creation query failed in database %d for owner %u, pet class %d, collar item %u (class %d). Attempting to delete the collar.",
        //     runtime::process::CurrentDatabaseId(), m_ownerId, npcClassId, createdItemId, collar)
        L2_LOG_ERROR(Log,
            "[pet] Pet creation query failed in database %d for owner %u, pet class %d, collar item %u (class %d); attempting to delete the collar.",
            runtime::process::CurrentDatabaseId(), m_ownerId, npcClassId, createdItemId, collar);
        if (!collarItem->Delete(true, false)) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d] -_- create pet rollback failed!  "
            //     "collar[%d] npc[%d]",
            //     "WareHouse.cpp", 0x2fa2, collar, npcClassId
            // )
            // Log.Add(LOG_ERROR,
            //     "[pet] Collar cleanup failed after pet creation failed: owner %u, pet class %d, collar item %u (class %d), database %d.",
            //     m_ownerId, npcClassId, createdItemId, collar, runtime::process::CurrentDatabaseId())
            L2_LOG_ERROR(Log,
                "[pet] Collar cleanup failed after pet creation failed: owner %u, pet class %d, collar item %u (class %d), database %d.",
                m_ownerId, npcClassId, createdItemId, collar, runtime::process::CurrentDatabaseId());
        }
        return false;
    }

    itemId = static_cast<int>(createdItemId);
    PushItem(collarItem.get());
    return true;
}

CItemSP CWareHouse::PopItemAndSave(CItem& item, std::int64_t amount) {
    L2_GUARD_FUNCTION();

    CItemSP poppedItem = PopItem(static_cast<int>(item.ItemID()));
    item.Set(
        0, item.ItemType(), amount, item.Enchant(), item.Eroded(), item.Bless(), item.Ident(), item.Wished(), 0,
        item.OptionKey(), item.m_intensiveItemType, item.Attribute(), -1, item.AppearanceType(), item.HLocked(),
        item.UseCount()
    );
    CItem::CacheItem(&item);
    item.Save(true);
    return poppedItem;
}

CItemSP CWareHouse::DropItem(CItem* itemToDrop, std::int64_t amountToDrop) {
    L2_GUARD_FUNCTION();

    if (!itemToDrop)
        return {};

    const std::int64_t originalAmount = itemToDrop->Amount();
    if (originalAmount < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] Drop item's org amount is invalid. "
        //     "real[%lld] try[%lld] type[%d], drop all.",
        //     "WareHouse.cpp", 0xa0a, static_cast<long long>(originalAmount), static_cast<long long>(amountToDrop),
        //     static_cast<int>(itemToDrop->ItemType())
        // )
        // Log.Add(LOG_ERROR,
        //     "[warehouse] Item %u (class %u, warehouse owner %u) has invalid quantity %lld. Requested drop %lld. Removing it from the warehouse cache, setting owner 0 and quantity 1, and attempting to save. No dropped item returned.",
        //     itemToDrop->ItemID(), itemToDrop->ItemType(), m_ownerId, static_cast<long long>(originalAmount), static_cast<long long>(amountToDrop))
        L2_LOG_ERROR(Log,
            "[warehouse] Item %u (class %u, warehouse owner %u) has invalid quantity %lld; requested drop %lld. Removing it from the warehouse cache, setting owner 0 and quantity 1, and attempting to save; no dropped item returned.",
            itemToDrop->ItemID(), itemToDrop->ItemType(), m_ownerId, static_cast<long long>(originalAmount), static_cast<long long>(amountToDrop));
        PopItemAndSave(*itemToDrop, 1);
        runtime::process::ReportNamedCallStack(L"CWareHouse::DropItem", 2, false);
        return {};
    }

    if (originalAmount < amountToDrop) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] Drop item's amount is invalid. "
        //     "real[%lld] try[%lld] type[%d], drop all.",
        //     "WareHouse.cpp", 0xa12, static_cast<long long>(originalAmount), static_cast<long long>(amountToDrop),
        //     static_cast<int>(itemToDrop->ItemType())
        // )
        // Log.Add(LOG_ERROR,
        //     "[warehouse] Cannot drop item %u (class %u, warehouse owner %u): available quantity %lld is less than requested %lld. Item unchanged.",
        //     itemToDrop->ItemID(), itemToDrop->ItemType(), m_ownerId, static_cast<long long>(originalAmount), static_cast<long long>(amountToDrop))
        L2_LOG_ERROR(Log,
            "[warehouse] Cannot drop item %u (class %u, warehouse owner %u): available quantity %lld is less than requested %lld; item unchanged.",
            itemToDrop->ItemID(), itemToDrop->ItemType(), m_ownerId, static_cast<long long>(originalAmount), static_cast<long long>(amountToDrop));
        runtime::process::ReportNamedCallStack(L"CWareHouse::DropItem", 2, false);
        return {};
    }

    if (amountToDrop == -1) {
        return PopItemAndSave(*itemToDrop, 1);
    }
    if (originalAmount == amountToDrop) {
        return PopItemAndSave(*itemToDrop, amountToDrop);
    }

    const std::int64_t remainingAmount = runtime::SubtractInt64Wrapping(originalAmount, amountToDrop);
    if (remainingAmount < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] Drop item's amount is invalid. "
        //     "real[%lld], try[%lld], drop all.",
        //     "WareHouse.cpp", 0xa26, static_cast<long long>(originalAmount), static_cast<long long>(amountToDrop)
        // )
        // Log.Add(LOG_ERROR,
        //     "[warehouse] Cannot split item %u (class %u, warehouse owner %u): quantity %lld minus requested %lld yields negative remainder %lld. Item unchanged.",
        //     itemToDrop->ItemID(), itemToDrop->ItemType(), m_ownerId, static_cast<long long>(originalAmount), static_cast<long long>(amountToDrop), static_cast<long long>(remainingAmount))
        L2_LOG_ERROR(Log,
            "[warehouse] Cannot split item %u (class %u, warehouse owner %u): quantity %lld minus requested %lld yields negative remainder %lld; item unchanged.",
            itemToDrop->ItemID(), itemToDrop->ItemType(), m_ownerId, static_cast<long long>(originalAmount), static_cast<long long>(amountToDrop), static_cast<long long>(remainingAmount));
        return {};
    }

    itemToDrop->SetAmount(remainingAmount);
    CItemSP droppedItem = MakeNewItem(
        0, itemToDrop->ItemType(), amountToDrop, itemToDrop->Enchant(), itemToDrop->Eroded(), itemToDrop->Bless(),
        itemToDrop->Ident(), itemToDrop->Wished(), 0, itemToDrop->OptionKey(), itemToDrop->m_intensiveItemType,
        itemToDrop->Attribute(), itemToDrop->HLocked(), itemToDrop->UseCount()
    );
    if (!droppedItem) {
        // Current 0x005da838 leaves the in-memory decrement intact and does
        // not save the source item when lin_CreateItem fails.
        // Log.Add(LOG_ERROR,
        //     "[warehouse] Cannot create the split portion of item %u (class %u, warehouse owner %u), quantity %lld. Source quantity remains %lld in memory instead of %lld and is not saved by this operation.",
        //     itemToDrop->ItemID(), itemToDrop->ItemType(), m_ownerId, static_cast<long long>(amountToDrop),
        //     static_cast<long long>(remainingAmount), static_cast<long long>(originalAmount))
        L2_LOG_ERROR(Log,
            "[warehouse] Cannot create the split portion of item %u (class %u, warehouse owner %u), quantity %lld; source quantity remains %lld in memory instead of %lld and is not saved by this operation.",
            itemToDrop->ItemID(), itemToDrop->ItemType(), m_ownerId, static_cast<long long>(amountToDrop),
            static_cast<long long>(remainingAmount), static_cast<long long>(originalAmount));
        return {};
    }

    CItem::CacheItem(droppedItem.get());
    droppedItem->Save(true);
    itemToDrop->Save(true);
    return droppedItem;
}

int CWareHouse::PickItem(CSocket* socket, const std::uint8_t* packet, int requestContext) {
    // Current Cached.exe 0x005f2acc. The third parameter is copied from the
    // seventh request field by RequestPickItem. Its original source name is
    // unavailable. Current code uses it only in item audit numeric field 5.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    int itemType = 0;
    int consumeType = 0;
    int requestedItemId = 0;
    int itemServerId = 0;
    std::int64_t requestedAmount = 0;
    int decodedRequestContext = 0;
    short deleteResidenceGuard = 0;
    const std::uint8_t* cursor = ::Disassemble(
        packet, "dddddQdh", &userServerId, &itemType, &consumeType, &requestedItemId, &itemServerId, &requestedAmount,
        &decodedRequestContext, &deleteResidenceGuard
    );

    int guardX = 0;
    int guardY = 0;
    int guardZ = 0;
    if (deleteResidenceGuard != 0) {
        ::Disassemble(cursor, "ddd", &guardX, &guardY, &guardZ);
    }

    int result = 2;
    int resultItemId = 0;
    std::int64_t resultAmount = 0;
    int resultEnchant = 0;
    CItemSP sourceItem;
    CItemSP resultItem;

    auto setResult = [&](CItemSP item, int itemResult) {
        resultItem = std::move(item);
        resultItemId = static_cast<int>(resultItem->ItemID());
        resultAmount = resultItem->Amount();
        resultEnchant = resultItem->Enchant();
        result = itemResult;
    };

    const ItemOptionKey emptyOptionKey{};
    const BaseAttribute emptyBaseAttribute{};
    const int warehouse = static_cast<int>(GetWarehouseNum());

    if (requestedItemId < 1) {
        if (consumeType >= ICT_NORMAL && consumeType <= ICT_CHARGE) {
            if (requestedAmount != 1) {
                // Log.Add(
                //     LOG_ERROR,
                //     "picked item's amount is over 1, get[%lld] "
                //     "BUT CONTINUE as 1",
                //     static_cast<long long>(requestedAmount)
                // )
                // L2_LOG_ERROR(Log,
                    // "picked item's amount is over 1, get[%lld] "
                    // "BUT CONTINUE as 1",
                    // static_cast<long long>(requestedAmount)
                // )
                L2_LOG_WARN(Log,
                    "[warehouse] Pickup for owner %u, class %d, consume type %d requested %lld items; "
                    "this branch requires one. Continuing with quantity 1.",
                    m_ownerId, itemType, consumeType, static_cast<long long>(requestedAmount));
                requestedAmount = 1;
            }

            CItemSP createdItem = MakeNewItem(
                m_ownerId, static_cast<unsigned int>(itemType), requestedAmount, 0, 0, 0, 0, 0, warehouse, emptyOptionKey, 0,
                emptyBaseAttribute, 0, 0
            );
            if (createdItem) {
                PushItem(createdItem.get());
                setResult(std::move(createdItem), 1);
            }
        } else if (consumeType == ICT_STACKABLE || consumeType == ICT_ASSET) {
            CItemSP existingItem = GetItemByType(itemType, 0);
            if (existingItem) {
                existingItem->SetAmount(runtime::AddInt64Wrapping(existingItem->Amount(), requestedAmount));
                existingItem->Save(true);
                setResult(std::move(existingItem), 0);
            } else {
                CItemSP createdItem = MakeNewItem(
                    m_ownerId, static_cast<unsigned int>(itemType), requestedAmount, 0, 0, 0, 0, 0, warehouse,
                    emptyOptionKey, 0, emptyBaseAttribute, 0, 0
                );
                if (createdItem) {
                    PushItem(createdItem.get());
                    setResult(std::move(createdItem), 1);
                }
            }
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     "Item stack type is invalid. class id[%d], "
            //     "stack type[%d]",
            //     itemType, consumeType
            // )
            // L2_LOG_ERROR(Log,
                // "Item stack type is invalid. class id[%d], "
                // "stack type[%d]",
                // itemType, consumeType
            // )
            L2_LOG_ERROR(Log,
                "[warehouse] Cannot create pickup item for owner %u, class %d: unsupported consume type %d.",
                m_ownerId, itemType, consumeType);
        }
    } else {
        sourceItem = CItem::Load(static_cast<std::uint32_t>(requestedItemId));
        if (!sourceItem) {
            // Log.Add(
            //     LOG_ERROR,
            //     "Cannot find item. dbid[%d], class id[%d], "
            //     "stack type[%d]",
            //     requestedItemId, itemType, consumeType
            // )
            // L2_LOG_ERROR(Log,
                // "Cannot find item. dbid[%d], class id[%d], "
                // "stack type[%d]",
                // requestedItemId, itemType, consumeType
            // )
            L2_LOG_ERROR(Log,
                "[warehouse] Cannot load pickup item %d for owner %u (class %d, consume type %d); "
                "item transfer not performed.",
                requestedItemId, m_ownerId, itemType, consumeType);
        } else if (consumeType >= ICT_NORMAL && consumeType <= ICT_CHARGE) {
            sourceItem->Delete(false, false);
            sourceItem->setOwnerID(m_ownerId);
            PushItem(sourceItem.get());
            sourceItem->Save(true);
            setResult(sourceItem, 1);
        } else if (consumeType == ICT_STACKABLE || consumeType == ICT_ASSET) {
            const std::int64_t sourceAmount = sourceItem->Amount();
            if (sourceAmount != requestedAmount) {
                // Log.Add(
                //     LOG_ERROR,
                //     "picked item's amount is different, get[%lld] "
                //     "cache[%lld] BUT CONTINUE",
                //     static_cast<long long>(requestedAmount), static_cast<long long>(sourceAmount)
                // )
                // L2_LOG_ERROR(Log,
                    // "picked item's amount is different, get[%lld] "
                    // "cache[%lld] BUT CONTINUE",
                    // static_cast<long long>(requestedAmount), static_cast<long long>(sourceAmount)
                // )
                L2_LOG_WARN(Log,
                    "[warehouse] Pickup quantity mismatch for item %d, owner %u, class %d: "
                    "request %lld, cached %lld. Continuing with the cached quantity.",
                    requestedItemId, m_ownerId, itemType,
                    static_cast<long long>(requestedAmount), static_cast<long long>(sourceAmount));
            }

            sourceItem->Delete(false, false);
            CItemSP existingItem = GetItemByType(itemType, 0);
            if (existingItem) {
                existingItem->SetAmount(runtime::AddInt64Wrapping(existingItem->Amount(), sourceAmount));
                sourceItem->SetAmount(0);
                sourceItem->Save(true);
                existingItem->Save(true);
                setResult(std::move(existingItem), 0);
            } else {
                sourceItem->setOwnerID(m_ownerId);
                PushItem(sourceItem.get());
                sourceItem->Save(true);
                setResult(sourceItem, 1);
            }
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     "Item stack type is invalid. class id[%d], "
            //     "stack type[%d]",
            //     itemType, consumeType
            // )
            // L2_LOG_ERROR(Log,
                // "Item stack type is invalid. class id[%d], "
                // "stack type[%d]",
                // itemType, consumeType
            // )
            L2_LOG_ERROR(Log,
                "[warehouse] Cannot transfer pickup item %d to owner %u (class %d): unsupported consume type %d.",
                requestedItemId, m_ownerId, itemType, consumeType);
        }
    }

    CUserSP owner = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    const WareHouseAuditActor actor = MakeWareHouseAuditActor(owner.get());
    WritePickItemAudits(
        actor, sourceItem.get(), resultItem.get(), requestContext, requestedItemId, itemType, requestedAmount, resultItemId,
        resultAmount, resultEnchant
    );

    if (deleteResidenceGuard == 0) {
        socket->Send(
            "cdddQdQdh", 0x71, userServerId, requestedItemId, itemServerId, static_cast<long long>(requestedAmount),
            resultItemId, static_cast<long long>(resultAmount), result, 0
        );
        return 0;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeleteResidenceGuard (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&guardX);
    connection.BindParameter(&guardY);
    connection.BindParameter(&guardZ);
    const bool guardDeleted = connection.ExecutePrepared(statement);
    socket->Send(
        "cdddQdQdhdddh", 0x71, userServerId, requestedItemId, itemServerId, static_cast<long long>(requestedAmount),
        resultItemId, static_cast<long long>(resultAmount), result, static_cast<int>(deleteResidenceGuard), guardX, guardY,
        guardZ, guardDeleted ? 1 : 0
    );
    return 0;
}

bool CWareHouse::GetPackage(CSocket* socket, const std::uint8_t* packet, CWareHouse* sourceWarehouse) {
    // Current Cached.exe 0x005f3b34. The older C1 body keeps the source
    // owner and transaction shape, while every packet width, hard-coded
    // package ident, copied modern item field and audit below is fixed by the
    // current machine code and paired current L2Server request/reply owners.
    L2_GUARD_FUNCTION();

    int sourceCharacterId = 0;
    int sourceUserServerId = 0;
    int targetCharacterId = 0;
    int npcId = 0;
    int packageItemCount = 0;
    const std::uint8_t* cursor = ::Disassemble(
        packet, "ddddd", &sourceCharacterId, &sourceUserServerId, &targetCharacterId, &npcId, &packageItemCount
    );

    std::array<char, 0x5000> responseItems{};
    int responseSize = 0;
    int responseItemCount = 0;
    int newItemCount = 0;
    int transactionOrder = 0;
    int result = -2;
    CItemTransactionOrderMap transactionItems;

    CUserSP sourceUser =
        g_userDb.GetUser(static_cast<int>(sourceWarehouse->m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    const WareHouseAuditActor actor = MakeWareHouseAuditActor(sourceUser.get());

    CUserSP destinationUser = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    const std::u16string destinationCharacterName =
        destinationUser ? std::u16string(destinationUser->GetCharName()) : std::u16string{};

    auto rollbackAndReply = [&]() {
        // Current 0x005f5228 sends the result before restoring any changed
        // item, so observers see the same reply/rollback ordering.
        socket->Send("cdd", 0x79, sourceUserServerId, result);
        CTransaction::RollbackTransactOrderMap(transactionItems);
        return false;
    };

    if (npcId <= 0) {
        return rollbackAndReply();
    }

    const int transferItemCount = runtime::AddInt32Wrapping(packageItemCount, -1);
    for (int index = 0; index < transferItemCount; ++index) {
        int itemId = 0;
        std::int64_t requestedAmount = 0;
        int consumeType = 0;
        cursor = ::Disassemble(cursor, "dQd", &itemId, &requestedAmount, &consumeType);

        CItemSP sourceItem = sourceWarehouse->GetItem(itemId);
        if (!sourceItem) {
            return rollbackAndReply();
        }

        sourceItem->SetTransation();
        CItemSP sourceRollback = sourceItem->Copy();
        sourceRollback->SetTrasactMode(TransactType_RollbackChanges);
        L2_ADD_REF(sourceItem, ObjectRefType::FunctionLocal);
        L2_ADD_REF(sourceRollback, ObjectRefType::FunctionLocal);
        transactionOrder = runtime::AddInt32Wrapping(transactionOrder, 1);
        transactionItems.emplace(transactionOrder, CItemKey(sourceRollback.get(), sourceItem.get()));

        const std::int64_t sourceAmount = sourceItem->Amount();
        if (sourceAmount < requestedAmount) {
            return rollbackAndReply();
        }

        if (consumeType == ICT_STACKABLE || consumeType == ICT_ASSET) {
            const std::int64_t remainingAmount = runtime::SubtractInt64Wrapping(sourceAmount, requestedAmount);
            CItemSP destinationItem = GetItemByType(static_cast<int>(sourceItem->ItemType()), 1000000);
            if (!destinationItem) {
                newItemCount = runtime::AddInt32Wrapping(newItemCount, 1);
                if (sourceAmount == requestedAmount) {
                    sourceItem->SetWarehouse(GetWarehouseNum());
                    sourceItem->setOwnerID(m_ownerId);
                    sourceItem->m_ident = 1000000;
                    sourceItem->SetChangeScope(ItemChangeScope_Decreased);
                    sourceItem->SetAuditAmount(requestedAmount);
                } else {
                    sourceItem->SetAmount(remainingAmount);

                    const ItemOptionKey emptyOptionKey{};
                    const BaseAttribute emptyBaseAttribute{};
                    destinationItem = MakeNewItem(
                        m_ownerId, sourceItem->ItemType(), 0, 0, 0, 0, 0, 0, WareHouseType_Inventory, emptyOptionKey, 0,
                        emptyBaseAttribute, 0, 0
                    );
                    if (!destinationItem) {
                        return rollbackAndReply();
                    }

                    destinationItem->Set(
                        m_ownerId, sourceItem->ItemType(), requestedAmount, sourceItem->Enchant(), sourceItem->Eroded(),
                        sourceItem->Bless(), sourceItem->Ident(), sourceItem->Wished(), GetWarehouseNum(),
                        sourceItem->OptionKey(), sourceItem->m_intensiveItemType, sourceItem->Attribute(), -1, 0,
                        sourceItem->HLocked(), sourceItem->UseCount()
                    );
                    destinationItem->SetTransation();
                    CItemSP destinationRollback = destinationItem->Copy();
                    destinationRollback->SetTrasactMode(TransactType_RollbackCreation);
                    destinationItem->m_ident = 1000000;
                    L2_ADD_REF(destinationItem, ObjectRefType::FunctionLocal);
                    L2_ADD_REF(destinationRollback, ObjectRefType::FunctionLocal);
                    transactionOrder = runtime::AddInt32Wrapping(transactionOrder, 1);
                    transactionItems.emplace(transactionOrder, CItemKey(destinationRollback.get(), destinationItem.get()));

                    sourceItem->SetChangeScope(ItemChangeScope_Added);
                    sourceItem->SetAuditAmount(requestedAmount);
                    destinationItem->SetChangeScope(ItemChangeScope_Increased);
                }
            } else {
                destinationItem->SetTransation();
                CItemSP destinationRollback = destinationItem->Copy();
                destinationRollback->SetTrasactMode(TransactType_RollbackChanges);
                L2_ADD_REF(destinationItem, ObjectRefType::FunctionLocal);
                L2_ADD_REF(destinationRollback, ObjectRefType::FunctionLocal);
                transactionOrder = runtime::AddInt32Wrapping(transactionOrder, 1);
                transactionItems.emplace(transactionOrder, CItemKey(destinationRollback.get(), destinationItem.get()));

                const std::int64_t combinedAmount = runtime::AddInt64Wrapping(destinationItem->Amount(), requestedAmount);
                constexpr std::uint64_t maximumItemAmount = 999999999999ULL;
                if (static_cast<std::uint64_t>(combinedAmount) > maximumItemAmount) {
                    result = -4;
                    return rollbackAndReply();
                }

                sourceItem->SetAmount(remainingAmount);
                destinationItem->SetAmount(combinedAmount);
                if (sourceAmount == requestedAmount) {
                    sourceItem->TransactDelete();
                }
                sourceItem->SetChangeScope(ItemChangeScope_Added);
                sourceItem->SetAuditAmount(requestedAmount);
                destinationItem->SetChangeScope(ItemChangeScope_Increased);
            }
        } else {
            newItemCount = runtime::AddInt32Wrapping(newItemCount, 1);
            sourceItem->SetWarehouse(GetWarehouseNum());
            sourceItem->setOwnerID(m_ownerId);
            sourceItem->m_ident = 1000000;
            sourceItem->SetChangeScope(ItemChangeScope_Deleted);
            sourceItem->SetAuditAmount(requestedAmount);
        }

        const int assembledSize = ::Assemble(
            responseItems.data() + responseSize, static_cast<int>(responseItems.size()) - responseSize, "dQ", itemId,
            static_cast<long long>(requestedAmount)
        );
        if (assembledSize > 0) {
            responseSize += assembledSize;
            responseItemCount = runtime::AddInt32Wrapping(responseItemCount, 1);
        }
    }

    int adenaItemId = 0;
    std::int64_t packageFee = 0;
    ::Disassemble(cursor, "dQ", &adenaItemId, &packageFee);
    CItemSP adenaItem = sourceWarehouse->GetItem(adenaItemId);
    if (!adenaItem) {
        return rollbackAndReply();
    }

    CItem* adenaRollbackItem = nullptr;
    for (const auto& entry : transactionItems) {
        if (entry.second.item == adenaItem.get()) {
            adenaRollbackItem = entry.second.rollbackItem;
            break;
        }
    }
    if (!adenaRollbackItem) {
        adenaItem->SetTransation();
        CItemSP adenaRollback = adenaItem->Copy();
        adenaRollback->SetTrasactMode(TransactType_RollbackChanges);
        L2_ADD_REF(adenaItem, ObjectRefType::FunctionLocal);
        L2_ADD_REF(adenaRollback, ObjectRefType::FunctionLocal);
        transactionOrder = runtime::AddInt32Wrapping(transactionOrder, 1);
        transactionItems.emplace(transactionOrder, CItemKey(adenaRollback.get(), adenaItem.get()));
        adenaRollbackItem = adenaRollback.get();
    }
    adenaRollbackItem->SetChangeScope(ItemChangeScope_Fee);

    const std::int64_t adenaAmount = adenaItem->Amount();
    if (adenaAmount < packageFee) {
        result = -1;
        return rollbackAndReply();
    }

    adenaItem->SetAmount(runtime::SubtractInt64Wrapping(adenaAmount, packageFee));
    if (adenaItem->Amount() == 0) {
        adenaItem->TransactDelete();
    }
    const int feeAssembledSize = ::Assemble(
        responseItems.data() + responseSize, static_cast<int>(responseItems.size()) - responseSize, "dQ", adenaItemId,
        static_cast<long long>(packageFee)
    );
    if (feeAssembledSize > 0) {
        responseSize += feeAssembledSize;
        responseItemCount = runtime::AddInt32Wrapping(responseItemCount, 1);
    }

    const int resultingItemCount = runtime::AddInt32Wrapping(newItemCount, m_itemNumber);
    if (resultingItemCount >= 201) {
        result = -4;
        return rollbackAndReply();
    }

    WritePackageFeeAudit(actor, *adenaItem.get(), packageFee);
    socket->Send("cddb", 0x79, sourceUserServerId, responseItemCount, responseSize, responseItems.data());

    for (const auto& entry : transactionItems) {
        CItem* rollbackItem = entry.second.rollbackItem;
        CItem* item = entry.second.item;
        WritePackageItemAudits(actor, destinationCharacterName, *item, npcId);
        item->CommitTransation(rollbackItem, true);
        L2_RELEASE(item, ObjectRefType::FunctionLocal);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
    }
    return true;
}

bool CWareHouse::BuyResetBeautyItem(
    CWareHouse* warehouse [[maybe_unused]], int builder, std::int64_t adenaPrice, std::int64_t beautyCoinPrice
) {
    // Current Cached.exe 0x005f99e4 / WareHouse.cpp:18073-18168. The first
    // explicit warehouse argument is present in the shipping signature but
    // is not read by its body. Item lookup still calls through `this`.
    L2_GUARD_FUNCTION();

    // Keep these smart pointers alive until after CTransaction has committed
    // or rolled back the raw item references stored in its ordered map.
    CItemSP adena = GetItemByType(0x39, 0);
    CItemSP beautyCoin = GetItemByType(0x8dd4, 0);
    CTransaction transaction;
    transaction.SetRollback();

    bool bought = false;
    if (beautyCoinPrice != 0) {
        if (!beautyCoin) {
            // Log.Add(LOG_ERROR, L"[%s],[%d]Beauty Item valid!", L"WareHouse.cpp", 0x4699)
            // Log.Add(LOG_WARN,
            //     "[appearance] Reset payment rejected for owner %u: no beauty-coin stack for requested price %lld. Attempting rollback of pending payment changes.",
            //     m_ownerId, static_cast<long long>(beautyCoinPrice))
            L2_LOG_WARN(Log,
                "[appearance] Reset payment rejected for owner %u: no beauty-coin stack for requested price %lld; attempting rollback of pending payment changes.",
                m_ownerId, static_cast<long long>(beautyCoinPrice));
            return false;
        }

        const std::int64_t currentAmount = beautyCoin->Amount();
        if (currentAmount < beautyCoinPrice) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s],[%d]Beauty Coin Item count is invalid. "
            //     L"[%I64d < %I64d]",
            //     L"WareHouse.cpp", 0x46a1, currentAmount, beautyCoinPrice
            // )
            // Log.Add(LOG_WARN,
            //     "[appearance] Reset payment rejected for owner %u: beauty-coin item %u has %lld, requested price %lld. Attempting rollback of pending payment changes.",
            //     m_ownerId, beautyCoin->ItemID(), static_cast<long long>(currentAmount), static_cast<long long>(beautyCoinPrice))
            L2_LOG_WARN(Log,
                "[appearance] Reset payment rejected for owner %u: beauty-coin item %u has %lld, requested price %lld; attempting rollback of pending payment changes.",
                m_ownerId, beautyCoin->ItemID(), static_cast<long long>(currentAmount), static_cast<long long>(beautyCoinPrice));
            return false;
        }
        if (!transaction.AddItem(beautyCoin))
            return false;
        beautyCoin->SetAmount(runtime::SubtractInt64Wrapping(currentAmount, beautyCoinPrice));
        if (beautyCoin->Amount() == 0) {
            beautyCoin->TransactDelete();
        }
        bought = true;
    }

    if (adenaPrice != 0) {
        if (!adena) {
            // Log.Add(LOG_ERROR, L"[%s],[%d]Beauty Adena valid!", L"WareHouse.cpp", 0x46c0)
            // Log.Add(LOG_WARN,
            //     "[appearance] Reset payment rejected for owner %u: no adena stack for requested price %lld. Attempting rollback of pending payments, including any beauty coins already deducted.",
            //     m_ownerId, static_cast<long long>(adenaPrice))
            L2_LOG_WARN(Log,
                "[appearance] Reset payment rejected for owner %u: no adena stack for requested price %lld; attempting rollback of pending payments, including any beauty coins already deducted.",
                m_ownerId, static_cast<long long>(adenaPrice));
            return false;
        }

        const std::int64_t currentAmount = adena->Amount();
        if (currentAmount < adenaPrice) {
            // The shipping format literal accidentally omits its source/line
            // placeholders even though that call also creates those two
            // arguments. Passing mismatched varargs would be undefined on a
            // non-Windows ABI, so keep the literal and its useful values.
            // Log.Add(LOG_ERROR, L"Beauty Adena count is invalid. . [%I64d < %I64d]", currentAmount, adenaPrice)
            // Log.Add(LOG_WARN,
            //     "[appearance] Reset payment rejected for owner %u: adena item %u has %lld, requested price %lld. Attempting rollback of pending payments, including any beauty coins already deducted.",
            //     m_ownerId, adena->ItemID(), static_cast<long long>(currentAmount), static_cast<long long>(adenaPrice))
            L2_LOG_WARN(Log,
                "[appearance] Reset payment rejected for owner %u: adena item %u has %lld, requested price %lld; attempting rollback of pending payments, including any beauty coins already deducted.",
                m_ownerId, adena->ItemID(), static_cast<long long>(currentAmount), static_cast<long long>(adenaPrice));
            return false;
        }
        if (!transaction.AddItem(adena))
            return false;
        adena->SetAmount(runtime::SubtractInt64Wrapping(currentAmount, adenaPrice));
        if (adena->Amount() == 0) {
            adena->TransactDelete();
        }
        bought = true;
    }

    if (builder != 0 && beautyCoinPrice == 0 && adenaPrice == 0) {
        // Log.Add(LOG_ERROR, L"[NO_ERROR] FILE[%s] Line[%d] BuilderCmd Used", L"WareHouse.cpp", 0x46e7)
        // Log.Add(LOG_NORMAL,
        //     "[appearance] Free reset payment accepted for owner %u with builder level %d. Adena and beauty-coin prices are both zero.",
        //     m_ownerId, builder)
        L2_LOG_INFO(Log,
            "[appearance] Free reset payment accepted for owner %u with builder level %d; adena and beauty-coin prices are both zero.",
            m_ownerId, builder);
        transaction.SetCommit();
        return true;
    }
    if (!bought)
        return false;

    transaction.SetCommit();
    return true;
}

bool CWareHouse::GetCommissionItem(int itemId, CPacketHelperEx<20480>& packet) {
    // Current Cached.exe 0x005fa348 / WareHouse.cpp:18198.
    L2_GUARD_FUNCTION();

    CItemSP item = GetItem(itemId);
    if (!item)
        return false;
    // CItemAssemblerSendToL2Server assembler(*item.get(), 0x7f)
    CItemAssemblerSendToL2Server assembler(*item.get(), ItemAssembleType::ALL);
    assembler.AssembleItemInfoImpl(packet);
    return true;
}

bool CWareHouse::WriteCommission(
    CSocket* socket [[maybe_unused]],
    CPacketHelperEx<20480>& packet,
    CWareHouse* destinationWarehouse,
    int itemId,
    std::int64_t amount,
    int consumeType,
    std::int64_t commissionFee,
    int& resultingItemId,
    int& itemType,
    int premiumItemId,
    std::int64_t premiumItemAmount
) {
    // Current Cached.exe 0x005fa4c0. The target stages the sold item, the
    // optional premium item and adena in one ordered transaction and commits
    // them only after every withdrawal succeeds.
    L2_GUARD_FUNCTION();

    CItemTransactionOrderMap transactionItems;
    int transactionOrder = 0;
    CItemSP adena = GetItemByType(57, 0);

    auto findRollback = [&](CItem* item) -> CItem* {
        for (const auto& entry : transactionItems) {
            if (entry.second.item == item) {
                return entry.second.rollbackItem;
            }
        }
        return nullptr;
    };
    auto stageItem = [&](CItemSP item, TransactType rollbackMode) -> CItem* {
        if (!item)
            return nullptr;
        if (CItem* rollbackItem = findRollback(item.get())) {
            return rollbackItem;
        }
        item->SetTransation();
        CItemSP rollbackItem = item->Copy();
        if (!rollbackItem)
            return nullptr;
        rollbackItem->SetTrasactMode(rollbackMode);
        L2_ADD_REF(item, ObjectRefType::FunctionLocal);
        L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
        transactionItems.emplace(++transactionOrder, CItemKey(rollbackItem.get(), item.get()));
        return rollbackItem.get();
    };
    auto rollback = [&]() {
        CTransaction::RollbackTransactOrderMap(transactionItems);
        transactionItems.clear();
        return false;
    };
    auto commit = [&]() {
        for (const auto& entry : transactionItems) {
            CItem* rollbackItem = entry.second.rollbackItem;
            CItem* item = entry.second.item;
            item->CommitTransation(rollbackItem, true);
            L2_RELEASE(item, ObjectRefType::FunctionLocal);
            L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
        }
        transactionItems.clear();
        return true;
    };
    auto assembleItem = [&](CItem& item) {
        // CItemAssemblerSendToL2Server assembler(item, 0x7f)
        CItemAssemblerSendToL2Server assembler(item, ItemAssembleType::ALL);
        assembler.AssembleItemInfoImpl(packet);
    };

    CItemSP sourceItem = GetItem(itemId);
    if (!sourceItem) {
        // Log.Add(
        //     LOG_ERROR, "[%s],[%d]WriteCommission Item valid! ItemDBID[%d]", "WareHouse.cpp", 0x4743, itemId
        // )
        // Log.Add(LOG_ERROR,
        //     "[commission] Item transfer failed for owner %u, source warehouse %d: item %d not found. Attempting rollback of transfer and fee changes.",
        //     m_ownerId, static_cast<int>(m_warehouseType), itemId)
        L2_LOG_ERROR(Log,
            "[commission] Item transfer failed for owner %u, source warehouse %d: item %d not found; attempting rollback of transfer and fee changes.",
            m_ownerId, static_cast<int>(m_warehouseType), itemId);
        return rollback();
    }
    if (!stageItem(sourceItem, TransactType_RollbackChanges)) {
        return rollback();
    }

    itemType = static_cast<int>(sourceItem->ItemType());
    const std::int64_t sourceAmount = sourceItem->Amount();
    if (sourceAmount < amount) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s],[%d]WriteCommission Item count is invalid. %d "
        //     "[%lld < %lld]",
        //     "WareHouse.cpp", 0x4755, itemId, static_cast<long long>(sourceAmount), static_cast<long long>(amount)
        // )
        // Log.Add(LOG_ERROR,
        //     "[commission] Item transfer failed for owner %u, source warehouse %d: item %d has quantity %lld, requested %lld. Attempting rollback of transfer and fee changes.",
        //     m_ownerId, static_cast<int>(m_warehouseType), itemId, static_cast<long long>(sourceAmount), static_cast<long long>(amount))
        L2_LOG_ERROR(Log,
            "[commission] Item transfer failed for owner %u, source warehouse %d: item %d has quantity %lld, requested %lld; attempting rollback of transfer and fee changes.",
            m_ownerId, static_cast<int>(m_warehouseType), itemId, static_cast<long long>(sourceAmount), static_cast<long long>(amount));
        return rollback();
    }

    if (consumeType == ICT_STACKABLE || consumeType == ICT_ASSET) {
        if (sourceAmount == amount) {
            sourceItem->SetWarehouse(destinationWarehouse->GetWarehouseNum());
            sourceItem->m_ident = 0;
            assembleItem(*sourceItem.get());
            sourceItem->SetChangeScope(ItemChangeScope_Decreased);
            resultingItemId = static_cast<int>(sourceItem->ItemID());
        } else {
            sourceItem->SetAmount(runtime::SubtractInt64Wrapping(sourceAmount, amount));
            CItemSP destinationItem = destinationWarehouse->MakeNewItem(
                destinationWarehouse->m_ownerId, sourceItem->ItemType(), amount, sourceItem->Enchant(), sourceItem->Eroded(),
                sourceItem->Bless(), 0, sourceItem->Wished(), destinationWarehouse->GetWarehouseNum(),
                sourceItem->OptionKey(), sourceItem->m_intensiveItemType, sourceItem->Attribute(), sourceItem->HLocked(),
                sourceItem->UseCount()
            );
            if (!destinationItem) {
                // Log.Add(LOG_ERROR, "[%s],[%d]WriteCommission NewItem valid!", "WareHouse.cpp", 0x4779)
                // Log.Add(LOG_ERROR,
                //     "[commission] Cannot create the destination stack for owner %u, warehouse %d, from item %d (source owner %u), quantity %lld. Source quantity is already %lld in memory. Attempting rollback of transfer and fee changes.",
                //     destinationWarehouse->m_ownerId, static_cast<int>(destinationWarehouse->m_warehouseType), itemId, m_ownerId, static_cast<long long>(amount), static_cast<long long>(sourceItem->Amount()))
                L2_LOG_ERROR(Log,
                    "[commission] Cannot create the destination stack for owner %u, warehouse %d, from item %d (source owner %u), quantity %lld; source quantity is already %lld in memory. Attempting rollback of transfer and fee changes.",
                    destinationWarehouse->m_ownerId, static_cast<int>(destinationWarehouse->m_warehouseType), itemId, m_ownerId, static_cast<long long>(amount), static_cast<long long>(sourceItem->Amount()));
                return rollback();
            }
            if (!stageItem(destinationItem, TransactType_RollbackCreation)) {
                return rollback();
            }
            assembleItem(*destinationItem.get());
            sourceItem->SetChangeScope(ItemChangeScope_Added);
            destinationItem->SetChangeScope(ItemChangeScope_Increased);
            resultingItemId = static_cast<int>(destinationItem->ItemID());
        }
    } else {
        sourceItem->SetWarehouse(destinationWarehouse->GetWarehouseNum());
        sourceItem->m_ident = 0;
        assembleItem(*sourceItem.get());
        sourceItem->SetChangeScope(ItemChangeScope_Fee);
        resultingItemId = itemId;
    }

    if (premiumItemId != 0) {
        CItemSP premiumItem = GetItem(premiumItemId);
        if (!premiumItem) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[F2P Commission] [%s],[%d] WriteCommission "
            //     "CommissionPremiumItem Invalid! "
            //     "CommissionPremiumItemDBID[%d]",
            //     "WareHouse.cpp", 0x47b2, premiumItemId
            // )
            // Log.Add(LOG_ERROR,
            //     "[commission] Item transfer failed for owner %u, source warehouse %d: premium fee item %d not found (requested fee quantity %lld). Attempting rollback of the item transfer and fees.",
            //     m_ownerId, static_cast<int>(m_warehouseType), premiumItemId, static_cast<long long>(premiumItemAmount))
            L2_LOG_ERROR(Log,
                "[commission] Item transfer failed for owner %u, source warehouse %d: premium fee item %d not found (requested fee quantity %lld); attempting rollback of the item transfer and fees.",
                m_ownerId, static_cast<int>(m_warehouseType), premiumItemId, static_cast<long long>(premiumItemAmount));
            return rollback();
        }
        CItem* premiumRollback = findRollback(premiumItem.get());
        if (!premiumRollback) {
            premiumRollback = stageItem(premiumItem, TransactType_RollbackChanges);
            if (!premiumRollback)
                return rollback();
            premiumRollback->SetChangeScope(ItemChangeScope_Fee);
        }
        const std::int64_t currentPremiumAmount = premiumItem->Amount();
        if (currentPremiumAmount < premiumItemAmount) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[F2P Commission] [%s],[%d] WriteCommission Warehouse "
            //     "error. (Amount[%lld] < CommissionPremiumItemAmount[%lld])",
            //     "WareHouse.cpp", 0x47ce, static_cast<long long>(currentPremiumAmount),
            //     static_cast<long long>(premiumItemAmount)
            // )
            // Log.Add(LOG_WARN,
            //     "[commission] Item transfer rejected for owner %u, source warehouse %d: premium fee item %d has quantity %lld, requested fee %lld. Attempting rollback of the item transfer and fees.",
            //     m_ownerId, static_cast<int>(m_warehouseType), premiumItemId, static_cast<long long>(currentPremiumAmount), static_cast<long long>(premiumItemAmount))
            L2_LOG_WARN(Log,
                "[commission] Item transfer rejected for owner %u, source warehouse %d: premium fee item %d has quantity %lld, requested fee %lld; attempting rollback of the item transfer and fees.",
                m_ownerId, static_cast<int>(m_warehouseType), premiumItemId, static_cast<long long>(currentPremiumAmount), static_cast<long long>(premiumItemAmount));
            return rollback();
        }
        premiumItem->SetAmount(runtime::SubtractInt64Wrapping(currentPremiumAmount, premiumItemAmount));
        if (premiumItem->Amount() == 0) {
            premiumItem->TransactDelete();
        }
        if (premiumItem->ItemType() == 0x5750) {
            return commit();
        }
    }

    if (!adena) {
        // Log.Add(LOG_ERROR, "[%s],[%d]WriteCommission No Adena", "WareHouse.cpp", 0x47ee)
        // Log.Add(LOG_WARN,
        //     "[commission] Item transfer rejected for owner %u, source warehouse %d: no adena stack for requested fee %lld. Attempting rollback of the item transfer and fees.",
        //     m_ownerId, static_cast<int>(m_warehouseType), static_cast<long long>(commissionFee))
        L2_LOG_WARN(Log,
            "[commission] Item transfer rejected for owner %u, source warehouse %d: no adena stack for requested fee %lld; attempting rollback of the item transfer and fees.",
            m_ownerId, static_cast<int>(m_warehouseType), static_cast<long long>(commissionFee));
        return rollback();
    }
    CItem* adenaRollback = findRollback(adena.get());
    if (!adenaRollback) {
        adenaRollback = stageItem(adena, TransactType_RollbackChanges);
        if (!adenaRollback)
            return rollback();
        adenaRollback->SetChangeScope(ItemChangeScope_Fee);
    }
    const std::int64_t currentAdena = adena->Amount();
    if (currentAdena < commissionFee) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s],[%d]WriteCommission Warehouse error. "
        //     "(Amount[%lld] < ItemAmount[%lld])",
        //     "WareHouse.cpp", 0x480a, static_cast<long long>(currentAdena), static_cast<long long>(commissionFee)
        // )
        // Log.Add(LOG_WARN,
        //     "[commission] Item transfer rejected for owner %u, source warehouse %d: adena item %u has %lld, requested fee %lld. Attempting rollback of the item transfer and fees.",
        //     m_ownerId, static_cast<int>(m_warehouseType), adena->ItemID(), static_cast<long long>(currentAdena), static_cast<long long>(commissionFee))
        L2_LOG_WARN(Log,
            "[commission] Item transfer rejected for owner %u, source warehouse %d: adena item %u has %lld, requested fee %lld; attempting rollback of the item transfer and fees.",
            m_ownerId, static_cast<int>(m_warehouseType), adena->ItemID(), static_cast<long long>(currentAdena), static_cast<long long>(commissionFee));
        return rollback();
    }
    adena->SetAmount(runtime::SubtractInt64Wrapping(currentAdena, commissionFee));
    if (adena->Amount() == 0) {
        adena->TransactDelete();
    }
    return commit();
}

bool CWareHouse::CancelCommissionInven(
    CSocket* socket [[maybe_unused]], CWareHouse* destinationWarehouse, int requestContext [[maybe_unused]], int itemId
) {
    // Current Cached.exe 0x005fb984. requestContext and socket are kept
    // by the shipping signature but the item transaction does not read them.
    L2_GUARD_FUNCTION();

    CItemTransactionOrderMap transactionItems;
    int transactionOrder = 0;
    auto stageItem = [&](CItemSP item, TransactType rollbackMode) -> bool {
        if (!item)
            return false;
        item->SetTransation();
        CItemSP rollbackItem = item->Copy();
        if (!rollbackItem)
            return false;
        rollbackItem->SetTrasactMode(rollbackMode);
        L2_ADD_REF(item, ObjectRefType::FunctionLocal);
        L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
        transactionItems.emplace(++transactionOrder, CItemKey(rollbackItem.get(), item.get()));
        return true;
    };
    auto rollback = [&]() {
        CTransaction::RollbackTransactOrderMap(transactionItems);
        transactionItems.clear();
        return false;
    };

    CItemSP sourceItem = GetItem(itemId);
    if (!sourceItem) {
        // Log.Add(
        //     LOG_ERROR, "[%s],[%d]CancelCommission Item invalid! ItemDBID[%d]", "WareHouse.cpp", 0x484f, itemId
        // )
        // Log.Add(LOG_ERROR,
        //     "[commission] Cannot return cancelled commission item %d for owner %u: source item not found. Attempting rollback of item changes.",
        //     itemId, m_ownerId)
        L2_LOG_ERROR(Log,
            "[commission] Cannot return cancelled commission item %d for owner %u: source item not found; attempting rollback of item changes.",
            itemId, m_ownerId);
        return rollback();
    }
    const ItemConsumeType consumeType = g_readFile.GetItemConsumeType(static_cast<int>(sourceItem->ItemType()));
    if (!stageItem(sourceItem, TransactType_RollbackChanges)) {
        return rollback();
    }

    const std::int64_t sourceAmount = sourceItem->Amount();
    if (consumeType == ICT_STACKABLE || consumeType == ICT_ASSET) {
        CItemSP destinationItem = destinationWarehouse->GetItemByType(static_cast<int>(sourceItem->ItemType()), 0);
        if (!destinationItem) {
            sourceItem->setOwnerID(destinationWarehouse->m_ownerId);
            sourceItem->SetWarehouse(destinationWarehouse->GetWarehouseNum());
            sourceItem->m_ident = 0;
            sourceItem->SetChangeScope(ItemChangeScope_Decreased);
        } else {
            if (!stageItem(destinationItem, TransactType_RollbackChanges)) {
                return rollback();
            }
            sourceItem->SetAmount(0);
            destinationItem->SetAmount(runtime::AddInt64Wrapping(destinationItem->Amount(), sourceAmount));
            destinationItem->m_ident = 0;
            sourceItem->TransactDelete();
            sourceItem->SetChangeScope(ItemChangeScope_Added);
            destinationItem->SetChangeScope(ItemChangeScope_Increased);
        }
    } else {
        sourceItem->setOwnerID(destinationWarehouse->m_ownerId);
        sourceItem->SetWarehouse(destinationWarehouse->GetWarehouseNum());
        sourceItem->m_ident = 0;
        sourceItem->SetChangeScope(ItemChangeScope_Deleted);
    }

    for (const auto& entry : transactionItems) {
        CItem* rollbackItem = entry.second.rollbackItem;
        CItem* item = entry.second.item;
        item->CommitTransation(rollbackItem, true);
        L2_RELEASE(item, ObjectRefType::FunctionLocal);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
    }
    transactionItems.clear();
    return true;
}

bool CWareHouse::DeleteCommissionInven(
    CSocket* socket [[maybe_unused]],
    CPacketHelperEx<20480>& packet,
    CWareHouse* destinationWarehouse,
    int requestContext [[maybe_unused]],
    int itemId,
    std::int64_t commissionId [[maybe_unused]],
    std::int64_t amount,
    int& resultingItemId,
    CommissionOwner::Enum owner,
    int& enchant
) {
    // Current Cached.exe 0x005fc3b8. The inventory variant merges stackable
    // items into an existing destination stack when one is present.
    L2_GUARD_FUNCTION();

    CItemTransactionOrderMap transactionItems;
    int transactionOrder = 0;
    auto stageItem = [&](CItemSP item, TransactType rollbackMode) -> bool {
        if (!item)
            return false;
        item->SetTransation();
        CItemSP rollbackItem = item->Copy();
        if (!rollbackItem)
            return false;
        rollbackItem->SetTrasactMode(rollbackMode);
        L2_ADD_REF(item, ObjectRefType::FunctionLocal);
        L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
        transactionItems.emplace(++transactionOrder, CItemKey(rollbackItem.get(), item.get()));
        return true;
    };
    auto rollback = [&]() {
        CTransaction::RollbackTransactOrderMap(transactionItems);
        transactionItems.clear();
        return false;
    };
    auto assembleItem = [&](CItem& item) {
        if (static_cast<int>(owner) != 1)
            return;
        // CItemAssemblerSendToL2Server assembler(item, 0x7f)
        CItemAssemblerSendToL2Server assembler(item, ItemAssembleType::ALL);
        assembler.AssembleItemInfoImpl(packet);
    };

    CItemSP sourceItem = GetItem(itemId);
    if (!sourceItem) {
        // Log.Add(
        //     LOG_ERROR, "[%s],[%d]DeleteCommission Item invalid! ItemDBID[%d]", "WareHouse.cpp", 0x48c2, itemId
        // )
        // Log.Add(LOG_ERROR,
        //     "[commission] Cannot return listing %lld to inventory for owner %u: source item %d not found. Attempting rollback of item changes.",
        //     static_cast<long long>(commissionId), m_ownerId, itemId)
        L2_LOG_ERROR(Log,
            "[commission] Cannot return listing %lld to inventory for owner %u: source item %d not found; attempting rollback of item changes.",
            static_cast<long long>(commissionId), m_ownerId, itemId);
        return rollback();
    }
    const ItemConsumeType consumeType = g_readFile.GetItemConsumeType(static_cast<int>(sourceItem->ItemType()));
    if (!stageItem(sourceItem, TransactType_RollbackChanges)) {
        return rollback();
    }

    const std::int64_t sourceAmount = sourceItem->Amount();
    if (sourceAmount < amount) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s],[%d]DeleteCommission Item count is invalid. %d "
        //     "[%lld < %lld]",
        //     "WareHouse.cpp", 0x48d4, itemId, static_cast<long long>(sourceAmount), static_cast<long long>(amount)
        // )
        // Log.Add(LOG_ERROR,
        //     "[commission] Cannot return listing %lld to inventory for owner %u: item %d has quantity %lld, requested %lld. Attempting rollback of item changes.",
        //     static_cast<long long>(commissionId), m_ownerId, itemId, static_cast<long long>(sourceAmount), static_cast<long long>(amount))
        L2_LOG_ERROR(Log,
            "[commission] Cannot return listing %lld to inventory for owner %u: item %d has quantity %lld, requested %lld; attempting rollback of item changes.",
            static_cast<long long>(commissionId), m_ownerId, itemId, static_cast<long long>(sourceAmount), static_cast<long long>(amount));
        return rollback();
    }

    if (consumeType == ICT_STACKABLE || consumeType == ICT_ASSET) {
        CItemSP destinationItem = destinationWarehouse->GetItemByType(static_cast<int>(sourceItem->ItemType()), 0);
        if (!destinationItem) {
            if (sourceAmount == amount) {
                sourceItem->setOwnerID(destinationWarehouse->m_ownerId);
                sourceItem->SetWarehouse(destinationWarehouse->GetWarehouseNum());
                sourceItem->m_ident = 0;
                assembleItem(*sourceItem.get());
                sourceItem->SetChangeScope(ItemChangeScope_Decreased);
                enchant = sourceItem->Enchant();
                resultingItemId = itemId;
            } else {
                sourceItem->SetAmount(runtime::SubtractInt64Wrapping(sourceAmount, amount));
                destinationItem = destinationWarehouse->MakeNewItem(
                    destinationWarehouse->m_ownerId, sourceItem->ItemType(), amount, sourceItem->Enchant(),
                    sourceItem->Eroded(), sourceItem->Bless(), 0, sourceItem->Wished(),
                    destinationWarehouse->GetWarehouseNum(), sourceItem->OptionKey(), sourceItem->m_intensiveItemType,
                    sourceItem->Attribute(), sourceItem->HLocked(), sourceItem->UseCount()
                );
                if (!destinationItem) {
                    // Log.Add(LOG_ERROR, "[%s],[%d]DeleteCommission NewItem valid!", "WareHouse.cpp", 0x492a)
                    // Log.Add(LOG_ERROR,
                    //     "[commission] Cannot create the inventory stack for listing %lld, destination owner %u, from source item %d, quantity %lld. Source quantity is already %lld in memory. Attempting rollback of item changes.",
                    //     static_cast<long long>(commissionId), destinationWarehouse->m_ownerId, itemId, static_cast<long long>(amount), static_cast<long long>(sourceItem->Amount()))
                    L2_LOG_ERROR(Log,
                        "[commission] Cannot create the inventory stack for listing %lld, destination owner %u, from source item %d, quantity %lld; source quantity is already %lld in memory. Attempting rollback of item changes.",
                        static_cast<long long>(commissionId), destinationWarehouse->m_ownerId, itemId, static_cast<long long>(amount), static_cast<long long>(sourceItem->Amount()));
                    return rollback();
                }
                if (!stageItem(destinationItem, TransactType_RollbackCreation)) {
                    return rollback();
                }
                assembleItem(*destinationItem.get());
                sourceItem->SetChangeScope(ItemChangeScope_Added);
                destinationItem->SetChangeScope(ItemChangeScope_Fee);
                enchant = destinationItem->Enchant();
                resultingItemId = static_cast<int>(destinationItem->ItemID());
            }
        } else {
            if (!stageItem(destinationItem, TransactType_RollbackChanges)) {
                return rollback();
            }
            sourceItem->SetAmount(runtime::SubtractInt64Wrapping(sourceAmount, amount));
            destinationItem->SetAmount(runtime::AddInt64Wrapping(destinationItem->Amount(), amount));
            destinationItem->m_ident = 0;
            if (sourceAmount == amount) {
                sourceItem->TransactDelete();
            }
            assembleItem(*destinationItem.get());
            sourceItem->SetChangeScope(ItemChangeScope_Added);
            destinationItem->SetChangeScope(ItemChangeScope_Increased);
            enchant = destinationItem->Enchant();
            resultingItemId = static_cast<int>(destinationItem->ItemID());
        }
    } else {
        sourceItem->setOwnerID(destinationWarehouse->m_ownerId);
        sourceItem->SetWarehouse(destinationWarehouse->GetWarehouseNum());
        sourceItem->m_ident = 0;
        assembleItem(*sourceItem.get());
        sourceItem->SetChangeScope(ItemChangeScope_Deleted);
        enchant = sourceItem->Enchant();
        resultingItemId = itemId;
    }

    for (const auto& entry : transactionItems) {
        CItem* rollbackItem = entry.second.rollbackItem;
        CItem* item = entry.second.item;
        item->CommitTransation(rollbackItem, true);
        L2_RELEASE(item, ObjectRefType::FunctionLocal);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
    }
    transactionItems.clear();
    return true;
}

bool CWareHouse::DeleteCommissionPost(
    CSocket* socket [[maybe_unused]],
    CPacketHelperEx<20480>& packet,
    CWareHouse* destinationWarehouse,
    int requestContext [[maybe_unused]],
    int itemId,
    std::int64_t commissionId [[maybe_unused]],
    std::int64_t amount,
    int& resultingItemId,
    CommissionOwner::Enum owner,
    int& enchant
) {
    // Current Cached.exe 0x005fd7d4. Unlike DeleteCommissionInven, this
    // shipping owner never searches for an existing destination stack.
    L2_GUARD_FUNCTION();

    CItemTransactionOrderMap transactionItems;
    int transactionOrder = 0;
    auto stageItem = [&](CItemSP item, TransactType rollbackMode) -> bool {
        if (!item)
            return false;
        item->SetTransation();
        CItemSP rollbackItem = item->Copy();
        if (!rollbackItem)
            return false;
        rollbackItem->SetTrasactMode(rollbackMode);
        L2_ADD_REF(item, ObjectRefType::FunctionLocal);
        L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
        transactionItems.emplace(++transactionOrder, CItemKey(rollbackItem.get(), item.get()));
        return true;
    };
    auto rollback = [&]() {
        CTransaction::RollbackTransactOrderMap(transactionItems);
        transactionItems.clear();
        return false;
    };
    auto assembleItem = [&](CItem& item) {
        if (static_cast<int>(owner) != 1)
            return;
        // CItemAssemblerSendToL2Server assembler(item, 0x7f)
        CItemAssemblerSendToL2Server assembler(item, ItemAssembleType::ALL);
        assembler.AssembleItemInfoImpl(packet);
    };

    CItemSP sourceItem = GetItem(itemId);
    if (!sourceItem) {
        // Log.Add(
        //     LOG_ERROR, "[%s],[%d]DeleteCommission Item invalid! ItemDBID[%d]", "WareHouse.cpp", 0x498c, itemId
        // )
        // Log.Add(LOG_ERROR,
        //     "[commission] Cannot return listing %lld to mail storage for owner %u: source item %d not found. Attempting rollback of item changes.",
        //     static_cast<long long>(commissionId), m_ownerId, itemId)
        L2_LOG_ERROR(Log,
            "[commission] Cannot return listing %lld to mail storage for owner %u: source item %d not found; attempting rollback of item changes.",
            static_cast<long long>(commissionId), m_ownerId, itemId);
        return rollback();
    }
    const ItemConsumeType consumeType = g_readFile.GetItemConsumeType(static_cast<int>(sourceItem->ItemType()));
    if (!stageItem(sourceItem, TransactType_RollbackChanges)) {
        return rollback();
    }

    const std::int64_t sourceAmount = sourceItem->Amount();
    if (sourceAmount < amount) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s],[%d]DeleteCommission Item count is invalid. %d "
        //     "[%lld < %lld]",
        //     "WareHouse.cpp", 0x499e, itemId, static_cast<long long>(sourceAmount), static_cast<long long>(amount)
        // )
        // Log.Add(LOG_ERROR,
        //     "[commission] Cannot return listing %lld to mail storage for owner %u: item %d has quantity %lld, requested %lld. Attempting rollback of item changes.",
        //     static_cast<long long>(commissionId), m_ownerId, itemId, static_cast<long long>(sourceAmount), static_cast<long long>(amount))
        L2_LOG_ERROR(Log,
            "[commission] Cannot return listing %lld to mail storage for owner %u: item %d has quantity %lld, requested %lld; attempting rollback of item changes.",
            static_cast<long long>(commissionId), m_ownerId, itemId, static_cast<long long>(sourceAmount), static_cast<long long>(amount));
        return rollback();
    }

    if (consumeType == ICT_STACKABLE || consumeType == ICT_ASSET) {
        if (sourceAmount == amount) {
            sourceItem->setOwnerID(destinationWarehouse->m_ownerId);
            sourceItem->SetWarehouse(destinationWarehouse->GetWarehouseNum());
            sourceItem->m_ident = 0;
            assembleItem(*sourceItem.get());
            sourceItem->SetChangeScope(ItemChangeScope_Decreased);
            enchant = sourceItem->Enchant();
            resultingItemId = itemId;
        } else {
            sourceItem->SetAmount(runtime::SubtractInt64Wrapping(sourceAmount, amount));
            CItemSP destinationItem = destinationWarehouse->MakeNewItem(
                destinationWarehouse->m_ownerId, sourceItem->ItemType(), amount, sourceItem->Enchant(), sourceItem->Eroded(),
                sourceItem->Bless(), 0, sourceItem->Wished(), destinationWarehouse->GetWarehouseNum(),
                sourceItem->OptionKey(), sourceItem->m_intensiveItemType, sourceItem->Attribute(), sourceItem->HLocked(),
                sourceItem->UseCount()
            );
            if (!destinationItem) {
                // Log.Add(LOG_ERROR, "[%s],[%d]DeleteCommission NewItem valid!", "WareHouse.cpp", 0x49e0)
                // Log.Add(LOG_ERROR,
                //     "[commission] Cannot create the mail storage stack for listing %lld, destination owner %u, from source item %d, quantity %lld. Source quantity is already %lld in memory. Attempting rollback of item changes.",
                //     static_cast<long long>(commissionId), destinationWarehouse->m_ownerId, itemId, static_cast<long long>(amount), static_cast<long long>(sourceItem->Amount()))
                L2_LOG_ERROR(Log,
                    "[commission] Cannot create the mail storage stack for listing %lld, destination owner %u, from source item %d, quantity %lld; source quantity is already %lld in memory. Attempting rollback of item changes.",
                    static_cast<long long>(commissionId), destinationWarehouse->m_ownerId, itemId, static_cast<long long>(amount), static_cast<long long>(sourceItem->Amount()));
                return rollback();
            }
            if (!stageItem(destinationItem, TransactType_RollbackCreation)) {
                return rollback();
            }
            assembleItem(*destinationItem.get());
            sourceItem->SetChangeScope(ItemChangeScope_Added);
            destinationItem->SetChangeScope(ItemChangeScope_Fee);
            enchant = destinationItem->Enchant();
            resultingItemId = static_cast<int>(destinationItem->ItemID());
        }
    } else {
        sourceItem->setOwnerID(destinationWarehouse->m_ownerId);
        sourceItem->SetWarehouse(destinationWarehouse->GetWarehouseNum());
        sourceItem->m_ident = 0;
        assembleItem(*sourceItem.get());
        sourceItem->SetChangeScope(ItemChangeScope_Deleted);
        enchant = sourceItem->Enchant();
        resultingItemId = itemId;
    }

    for (const auto& entry : transactionItems) {
        CItem* rollbackItem = entry.second.rollbackItem;
        CItem* item = entry.second.item;
        item->CommitTransation(rollbackItem, true);
        L2_RELEASE(item, ObjectRefType::FunctionLocal);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
    }
    transactionItems.clear();
    return true;
}

bool CWareHouse::BuyCommissionMoney(
    CSocket* socket [[maybe_unused]],
    CWareHouse* destinationWarehouse,
    std::int64_t price,
    std::int64_t itemAmount,
    int& resultingAdenaItemId,
    bool& transferSellerMoney,
    double feeRate
) {
    // Current Cached.exe 0x005fe6ac. The two integer conversions in the fee
    // expression are both present in the shipping machine code.
    L2_GUARD_FUNCTION();

    const std::int64_t baseFee =
        static_cast<std::int64_t>(static_cast<double>(itemAmount) * 0.005 * static_cast<double>(price));
    std::int64_t fee = static_cast<std::int64_t>(static_cast<double>(baseFee) * feeRate);
    if (feeRate > 0.0 && fee < 10000) {
        fee = 1000;
    }

    CItemTransactionOrderMap transactionItems;
    int transactionOrder = 0;
    auto stageItem = [&](CItemSP item, TransactType rollbackMode) -> bool {
        if (!item)
            return false;
        item->SetTransation();
        CItemSP rollbackItem = item->Copy();
        if (!rollbackItem)
            return false;
        rollbackItem->SetTrasactMode(rollbackMode);
        L2_ADD_REF(item, ObjectRefType::FunctionLocal);
        L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
        transactionItems.emplace(++transactionOrder, CItemKey(rollbackItem.get(), item.get()));
        return true;
    };
    auto rollback = [&]() {
        CTransaction::RollbackTransactOrderMap(transactionItems);
        transactionItems.clear();
        return false;
    };

    CItemSP buyerAdena = GetItemByType(57, 0);
    if (!buyerAdena) {
        // Log.Add(
        //     LOG_ERROR, "[%s],[%d]BuyItemCommission-Money ADENA_CLASS_ID valid!", "WareHouse.cpp", 0x4ab6
        // )
        // Log.Add(LOG_WARN,
        //     "[commission] Payment rejected for buyer %u: no adena stack for price %lld. Attempting rollback of payment changes.",
        //     m_ownerId, static_cast<long long>(price))
        L2_LOG_WARN(Log,
            "[commission] Payment rejected for buyer %u: no adena stack for price %lld; attempting rollback of payment changes.",
            m_ownerId, static_cast<long long>(price));
        return rollback();
    }
    if (!stageItem(buyerAdena, TransactType_RollbackChanges)) {
        return rollback();
    }

    const std::int64_t buyerAdenaAmount = buyerAdena->Amount();
    if (buyerAdenaAmount < price) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s],[%d]BuyItemCommission-Money ADENA count is invalid. "
        //     "[%lld < %lld]",
        //     "WareHouse.cpp", 0x4ac6, static_cast<long long>(buyerAdenaAmount), static_cast<long long>(price)
        // )
        // Log.Add(LOG_WARN,
        //     "[commission] Payment rejected for buyer %u: item %u has %lld adena, price is %lld. Attempting rollback of payment changes.",
        //     m_ownerId, buyerAdena->ItemID(), static_cast<long long>(buyerAdenaAmount), static_cast<long long>(price))
        L2_LOG_WARN(Log,
            "[commission] Payment rejected for buyer %u: item %u has %lld adena, price is %lld; attempting rollback of payment changes.",
            m_ownerId, buyerAdena->ItemID(), static_cast<long long>(buyerAdenaAmount), static_cast<long long>(price));
        return rollback();
    }

    const std::int64_t sellerMoney = runtime::SubtractInt64Wrapping(price, fee);
    if (sellerMoney < 1) {
        transferSellerMoney = false;
        resultingAdenaItemId = -1;
    }

    if (buyerAdenaAmount == price) {
        if (!transferSellerMoney) {
            buyerAdena->SetChangeScope(ItemChangeScope_Deleted);
            buyerAdena->TransactDelete();
        } else {
            buyerAdena->setOwnerID(destinationWarehouse->m_ownerId);
            buyerAdena->SetWarehouse(destinationWarehouse->GetWarehouseNum());
            buyerAdena->m_ident = 0;
            buyerAdena->SetChangeScope(ItemChangeScope_Decreased);
            buyerAdena->SetAmount(sellerMoney);
            resultingAdenaItemId = static_cast<int>(buyerAdena->ItemID());
        }
    } else {
        buyerAdena->SetAmount(runtime::SubtractInt64Wrapping(buyerAdenaAmount, price));
        if (transferSellerMoney) {
            CItemSP sellerAdena = destinationWarehouse->MakeNewItem(
                destinationWarehouse->m_ownerId, buyerAdena->ItemType(), price, buyerAdena->Enchant(), buyerAdena->Eroded(),
                buyerAdena->Bless(), 0, buyerAdena->Wished(), destinationWarehouse->GetWarehouseNum(),
                buyerAdena->OptionKey(), buyerAdena->m_intensiveItemType, buyerAdena->Attribute(), buyerAdena->HLocked(),
                buyerAdena->UseCount()
            );
            if (!sellerAdena) {
                // Log.Add(
                //     LOG_ERROR, "[%s],[%d]BuyItemCommission-Money NewItem valid!", "WareHouse.cpp", 0x4af2
                // )
                // Log.Add(LOG_ERROR,
                //     "[commission] Cannot create the seller payment stack for owner %u: price %lld, fee %lld, seller amount %lld. Buyer %u quantity is already %lld in memory. Attempting rollback of payment changes.",
                //     destinationWarehouse->m_ownerId, static_cast<long long>(price), static_cast<long long>(fee), static_cast<long long>(sellerMoney), m_ownerId, static_cast<long long>(buyerAdena->Amount()))
                L2_LOG_ERROR(Log,
                    "[commission] Cannot create the seller payment stack for owner %u: price %lld, fee %lld, seller amount %lld. Buyer %u quantity is already %lld in memory; attempting rollback of payment changes.",
                    destinationWarehouse->m_ownerId, static_cast<long long>(price), static_cast<long long>(fee), static_cast<long long>(sellerMoney), m_ownerId, static_cast<long long>(buyerAdena->Amount()));
                return rollback();
            }
            if (!stageItem(sellerAdena, TransactType_RollbackCreation)) {
                return rollback();
            }
            buyerAdena->SetChangeScope(ItemChangeScope_Added);
            sellerAdena->SetChangeScope(ItemChangeScope_Fee);
            sellerAdena->SetAmount(sellerMoney);
            resultingAdenaItemId = static_cast<int>(sellerAdena->ItemID());
        }
    }

    for (const auto& entry : transactionItems) {
        CItem* rollbackItem = entry.second.rollbackItem;
        CItem* item = entry.second.item;
        item->CommitTransation(rollbackItem, true);
        L2_RELEASE(item, ObjectRefType::FunctionLocal);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
    }
    transactionItems.clear();
    return true;
}

bool CWareHouse::BuyCommissionRollBackMoney(
    CSocket* socket [[maybe_unused]], CWareHouse* destinationWarehouse, std::int64_t amount, int adenaItemId
) {
    // Current Cached.exe 0x005ff43c. The no-destination-stack branch stages
    // the source object twice. That unusual transaction order is present in
    // the shipping body and is kept.
    L2_GUARD_FUNCTION();

    CItemTransactionOrderMap transactionItems;
    int transactionOrder = 0;
    auto stageItem = [&](CItemSP item, TransactType rollbackMode) -> bool {
        if (!item)
            return false;
        item->SetTransation();
        CItemSP rollbackItem = item->Copy();
        if (!rollbackItem)
            return false;
        rollbackItem->SetTrasactMode(rollbackMode);
        L2_ADD_REF(item, ObjectRefType::FunctionLocal);
        L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
        transactionItems.emplace(++transactionOrder, CItemKey(rollbackItem.get(), item.get()));
        return true;
    };
    auto rollback = [&]() {
        CTransaction::RollbackTransactOrderMap(transactionItems);
        transactionItems.clear();
        return false;
    };

    CItemSP sourceAdena = GetItem(adenaItemId);
    if (!sourceAdena) {
        // Log.Add(
        //     LOG_ERROR, "[%s],[%d]BuyItemCommission-Money ADENA_CLASS_ID valid!", "WareHouse.cpp", 0x4b3c
        // )
        // Log.Add(LOG_ERROR,
        //     "[commission] Refund failed for source owner %u: adena item %d not found (requested refund %lld). Attempting rollback of refund changes.",
        //     m_ownerId, adenaItemId, static_cast<long long>(amount))
        L2_LOG_ERROR(Log,
            "[commission] Refund failed for source owner %u: adena item %d not found (requested refund %lld); attempting rollback of refund changes.",
            m_ownerId, adenaItemId, static_cast<long long>(amount));
        return rollback();
    }
    if (!stageItem(sourceAdena, TransactType_RollbackChanges)) {
        return rollback();
    }

    CItemSP destinationAdena = destinationWarehouse->GetItemByType(57, 0);
    const std::int64_t sourceAmount = sourceAdena->Amount();
    if (!destinationAdena) {
        if (!stageItem(sourceAdena, TransactType_RollbackChanges)) {
            return rollback();
        }
        if (sourceAmount != amount) {
            // Log.Add(
            //     LOG_ERROR, "[%s],[%d]BuyItemCommission-RollBackMoney Amount not Equal2", "WareHouse.cpp", 0x4b7c
            // )
            // Log.Add(LOG_ERROR,
            //     "[commission] Refund amount mismatch for source owner %u, item %u: available %lld, requested %lld. Destination owner %u has no adena stack. Quantities have not changed. Attempting rollback of refund changes.",
            //     m_ownerId, sourceAdena->ItemID(), static_cast<long long>(sourceAmount), static_cast<long long>(amount), destinationWarehouse->m_ownerId)
            L2_LOG_ERROR(Log,
                "[commission] Refund amount mismatch for source owner %u, item %u: available %lld, requested %lld. Destination owner %u has no adena stack; quantities have not changed. Attempting rollback of refund changes.",
                m_ownerId, sourceAdena->ItemID(), static_cast<long long>(sourceAmount), static_cast<long long>(amount), destinationWarehouse->m_ownerId);
            return rollback();
        }
        sourceAdena->setOwnerID(destinationWarehouse->m_ownerId);
        sourceAdena->SetWarehouse(destinationWarehouse->GetWarehouseNum());
        sourceAdena->m_ident = 0;
        sourceAdena->SetChangeScope(ItemChangeScope_Decreased);
    } else {
        if (!stageItem(destinationAdena, TransactType_RollbackChanges)) {
            return rollback();
        }
        sourceAdena->SetAmount(runtime::SubtractInt64Wrapping(sourceAmount, amount));
        destinationAdena->SetAmount(runtime::AddInt64Wrapping(destinationAdena->Amount(), amount));
        destinationAdena->m_ident = 0;
        if (sourceAmount != amount) {
            // Log.Add(
            //     LOG_ERROR, "[%s],[%d]BuyItemCommission-RollBackMoney Amount not Equal1", "WareHouse.cpp", 0x4b60
            // )
            // Log.Add(LOG_ERROR,
            //     "[commission] Refund amount mismatch for source owner %u, item %u: previous quantity %lld, requested %lld. Quantities already changed in memory: source %lld, destination item %u (owner %u) %lld. Attempting rollback of refund changes.",
            //     m_ownerId, sourceAdena->ItemID(), static_cast<long long>(sourceAmount), static_cast<long long>(amount), static_cast<long long>(sourceAdena->Amount()), destinationAdena->ItemID(), destinationWarehouse->m_ownerId, static_cast<long long>(destinationAdena->Amount()))
            L2_LOG_ERROR(Log,
                "[commission] Refund amount mismatch for source owner %u, item %u: previous quantity %lld, requested %lld. Quantities already changed in memory: source %lld, destination item %u (owner %u) %lld; attempting rollback of refund changes.",
                m_ownerId, sourceAdena->ItemID(), static_cast<long long>(sourceAmount), static_cast<long long>(amount), static_cast<long long>(sourceAdena->Amount()), destinationAdena->ItemID(), destinationWarehouse->m_ownerId, static_cast<long long>(destinationAdena->Amount()));
            return rollback();
        }
        sourceAdena->TransactDelete();
        sourceAdena->SetChangeScope(ItemChangeScope_Increased);
        destinationAdena->SetChangeScope(ItemChangeScope_Added);
    }

    for (const auto& entry : transactionItems) {
        CItem* rollbackItem = entry.second.rollbackItem;
        CItem* item = entry.second.item;
        item->CommitTransation(rollbackItem, true);
        L2_RELEASE(item, ObjectRefType::FunctionLocal);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
    }
    transactionItems.clear();
    return true;
}

bool CWareHouse::MoveItemToChar(int itemId, CWareHouse* destinationWarehouse) {
    // Exact current Cached.exe 0x005d286c. This whole-item overload predates
    // the amount-aware commission path and merges only asset/stackable items
    // already present in the destination warehouse.
    L2_GUARD_FUNCTION();

    std::optional<CItemSP> itemStorage;
    bool moved;
    {
        L2_WRITE_GUARD(*this);
        auto& item = itemStorage.emplace(GetItem(itemId));
        if (!item) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[%S][%d] CWareHouse::MoveItemToChar failed. "
            //     L"nItemId(%d), from char_id(%d), to char_id(%d) ",
            //     "WareHouse.cpp", 0x2fbf, itemId, static_cast<int>(m_ownerId), static_cast<int>(destinationWarehouse->m_ownerId)
            // )
            // Log.Add(LOG_ERROR,
            //     "[warehouse] Cannot move item %d from owner %u to owner %u%s: source item not found.",
            //     itemId, m_ownerId, destinationWarehouse ? destinationWarehouse->m_ownerId : 0U,
            //     destinationWarehouse ? "" : " (destination unavailable)")
            L2_LOG_ERROR(Log,
                "[warehouse] Cannot move item %d from owner %u to owner %u%s: source item not found.",
                itemId, m_ownerId, destinationWarehouse ? destinationWarehouse->m_ownerId : 0U,
                destinationWarehouse ? "" : " (destination unavailable)");
            return false;
        }

        if (item->Ident() > 0) {
            const ItemOptionKey optionKey(item->OptionKey());
            const BaseAttribute baseAttribute(item->Attribute());
            item->Set(
                item->OwnerID(), item->ItemType(), item->Amount(), item->Enchant(), item->Eroded(), item->Bless(), 0,
                item->Wished(), item->Warehouse(), optionKey, item->m_intensiveItemType, baseAttribute, -1,
                item->AppearanceType(), item->HLocked(), item->UseCount()
            );
            item->Save(true);
        }

        moved = false;
        const int consumeType = g_readFile.GetConsumeType(static_cast<int>(item->ItemType()));
        if (consumeType == 1 || consumeType == 3) {
            item->setOwnerID(destinationWarehouse->m_ownerId);
            item->SetWarehouse(destinationWarehouse->GetWarehouseNum());
            item->Save(true);
            PopItem(itemId);
            item->Delete(false, false);
            CItem::CacheItem(item.get());
            RemoveEquipped(itemId);
            destinationWarehouse->PushItem(item.get());
            moved = true;
        } else if (consumeType == 2 || consumeType == 4) {
            CItemSP destinationItem = destinationWarehouse->GetItemByType(static_cast<int>(item->ItemType()), 0);
            if (!destinationItem) {
                item->setOwnerID(destinationWarehouse->m_ownerId);
                item->SetWarehouse(destinationWarehouse->GetWarehouseNum());
                item->Save(true);
                PopItem(itemId);
                item->Delete(false, false);
                CItem::CacheItem(item.get());
                RemoveEquipped(itemId);
                destinationWarehouse->PushItem(item.get());
            } else {
                destinationItem->SetAmount(runtime::AddInt64Wrapping(item->Amount(), destinationItem->Amount()));
                destinationItem->Save(true);
                PopItem(itemId);
                item->Delete(true, false);
            }
            moved = true;
        }
    }
    return moved;
}

bool CWareHouse::SeizeItem(int option, int itemId, int warehouse, int useSeizeItem2) {
    // Exact current Cached.exe 0x005d9470. This older four-int overload is
    // still the path used by MoveItemProc for pledge commission items. It
    // either moves a live warehouse object out or reconstructs the seized
    // database row inside this warehouse.
    L2_GUARD_FUNCTION();

    std::optional<CItemSP> itemStorage;
    unsigned int queriedItemId, queriedOwnerId, queriedItemType;
    long long queriedAmount;
    int queriedWarehouse;
    std::optional<DBConn> connectionStorage;
    std::optional<CItemSP> destinationStorage;
    unsigned int loadedOwnerId, loadedItemType;
    long long loadedAmount;
    int enchant, eroded, bless, ident, wished, sourceWarehouse;
    std::optional<ItemOptionKey> optionKeyStorage;
    int intensiveItemType, inventorySlotIndex, appearanceType;
    std::optional<BaseAttribute> baseAttributeStorage;
    short hLocked, useCount;
    {
        L2_WRITE_GUARD(*this);

        if (option == 1) {
            auto& item = itemStorage.emplace(GetItem(itemId));
            if (!item) {
                return false;
            }

            PopItem(itemId);
            item->Delete(false, false);

            auto& connection = connectionStorage.emplace(runtime::process::CurrentDatabaseId());
            const int seizeOption = 1;
            ExecuteWarehouseSeizeItem(connection, useSeizeItem2 != 0, seizeOption, warehouse, itemId);
            RemoveEquipped(itemId);
            return true;
        }

        if (option != 2) {
            return false;
        }

        queriedItemId = 0;
        queriedOwnerId = 0;
        queriedItemType = 0;
        queriedAmount = 0;
        queriedWarehouse = 0;

        auto& connection = connectionStorage.emplace(runtime::process::CurrentDatabaseId());
        connection.Bind(&queriedItemId);
        connection.Bind(&queriedOwnerId);
        connection.Bind(&queriedItemType);
        connection.Bind(&queriedAmount);
        connection.Bind(&queriedWarehouse);
        constexpr const wchar_t* itemTypeStatement = L"{Call lin_GetItemType (? )}";
        connection.Prepare(itemTypeStatement);
        connection.BindParameter(&itemId);
        if (connection.ExecutePrepared(itemTypeStatement)) {
            connection.Fetch();
        }
        connection.UnBind();

        if (queriedWarehouse < 1001 || queriedWarehouse > 1004) {
            return false;
        }

        const int consumeType = g_readFile.GetConsumeType(static_cast<int>(queriedItemType));
        bool reconstructItem = false;

        if (consumeType == 2 || consumeType == 4) {
            auto& destinationItem = destinationStorage.emplace(GetItemByType(static_cast<int>(queriedItemType), 0));
            if (destinationItem) {
                destinationItem->SetAmount(runtime::AddInt64Wrapping(destinationItem->Amount(), queriedAmount));
                destinationItem->Save(true);
                ExecuteWarehouseDeleteItemRow(connection, itemId);
                return true;
            }
            reconstructItem = true;
            destinationStorage.reset();
        } else if (consumeType == 1 || consumeType == 3) {
            reconstructItem = true;
        }

        if (!reconstructItem) {
            return false;
        }

        const int seizeOption = 2;
        ExecuteWarehouseSeizeItem(connection, useSeizeItem2 != 0, seizeOption, warehouse, itemId);
        connection.UnBind();

        loadedOwnerId = 0;
        loadedItemType = 0;
        loadedAmount = 0;
        enchant = 0;
        eroded = 0;
        bless = 0;
        ident = 0;
        wished = 0;
        sourceWarehouse = 0;
        auto& optionKey = optionKeyStorage.emplace();
        intensiveItemType = 0;
        inventorySlotIndex = -1;
        appearanceType = 0;
        auto& baseAttribute = baseAttributeStorage.emplace();
        hLocked = 0;
        useCount = 0;

        connection.Bind(&loadedOwnerId);
        connection.Bind(&loadedItemType);
        connection.Bind(&loadedAmount);
        connection.Bind(&enchant);
        connection.Bind(&eroded);
        connection.Bind(&bless);
        connection.Bind(&ident);
        connection.Bind(&wished);
        connection.Bind(&sourceWarehouse);
        connection.Bind(&optionKey.variationOption1);
        connection.Bind(&optionKey.variationOption2);
        connection.Bind(&intensiveItemType);
        connection.Bind(&inventorySlotIndex);
        connection.Bind(&appearanceType);
        connection.Bind(&baseAttribute.m_nAttackType);
        connection.Bind(&baseAttribute.m_nAttackValue);
        for (short& defendValue : baseAttribute.m_nDefendValue) {
            connection.Bind(&defendValue);
        }
        connection.Bind(&hLocked);
        connection.Bind(&useCount);

        constexpr const wchar_t* loadStatement = L"{Call lin_LoadItemByItemId (?)}";
        connection.Prepare(loadStatement);
        connection.BindParameter(&itemId);
        if (connection.ExecutePrepared(loadStatement) && connection.Fetch()) {
            CItem* loadedItem = nullptr;
            try {
                loadedItem = new CItem(
                    static_cast<unsigned int>(itemId), loadedOwnerId, loadedItemType, loadedAmount, enchant, eroded, bless,
                    ident, wished, sourceWarehouse, optionKey, intensiveItemType, baseAttribute, inventorySlotIndex,
                    appearanceType, hLocked, useCount
                );
            } catch (const std::bad_alloc&) {
                loadedItem = nullptr;
            }
            if (loadedItem) {
                loadedItem->LoadEnsoulInfo();
                PushItem(loadedItem);
                L2_RELEASE(loadedItem, ObjectRefType::CreateDelete);
            }
        }

        // The shipping overload reports success once the database seize path is
        // selected, even when the later load returns no row.
    }
    return true;
}

bool CWareHouse::AddItemByAdmin(
    int* itemId,
    int itemType,
    std::int64_t amount,
    int enchant,
    int eroded,
    int bless,
    int ident,
    int wished,
    int warehouse,
    ItemOptionKey optionKey,
    int intensiveItemType,
    BaseAttribute baseAttribute,
    CItem::EnsoulInfo ensoulInfo[],
    short hLocked,
    short useCount,
    int appearanceType,
    int modifyPeriod,
    int period,
    int duration
) {
    // Exact current Cached.exe 0x005d9c84. The supplied warehouse value is
    // kept for the target's failure diagnostic. Successful creation uses
    // this object's GetWarehouseNum(), exactly as the shipping body does.
    L2_GUARD_FUNCTION();

    CUserSP retainedUser = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());

    std::array<char16_t, 256> baseAttributeText{};
    baseAttribute.GetLogString(baseAttributeText.data());
    const std::u16string baseAttributeLog = std::u16string(baseAttributeText.data());
    const std::u16string variationLog =
        MakeWareHouseVariationLogText(intensiveItemType, optionKey.variationOption1, optionKey.variationOption2);
    std::array<char16_t, 256> ensoulLog{};

    bool added = false;
    CItemSP item;
    std::int64_t resultingAmount = 0;

    {
        L2_WRITE_GUARD(*this);
        const int consumeType = g_readFile.GetConsumeType(itemType);

        const auto createItem = [&]() {
            return MakeNewItem(
                m_ownerId, static_cast<unsigned int>(itemType), amount, enchant, eroded, bless, ident, wished, GetWarehouseNum(),
                optionKey, intensiveItemType, baseAttribute, hLocked, useCount
            );
        };
        const auto writeCreatePremiumLog = [&]() {
            if (g_readFile.IsPremiumItem(itemType)) {
                item->LogPremiumRelated(
                    static_cast<PremiumItemLogType>(0), 0, resultingAmount, static_cast<PremiumItemLogReason>(2)
                );
            }
        };
        // const auto logCreateFailure = [&](const wchar_t* format) {
        //     Log.AddT(LOG_ERROR, format, itemType, static_cast<int>(amount), warehouse)
        // }
        const auto logCreateFailure = [&](const char* reason) {
            // Log.Add(LOG_ERROR,
            //     "[warehouse] Administrative item grant failed for owner %u: %s (class %d, quantity %lld, consume type %d, requested warehouse %d, actual warehouse %d).",
            //     m_ownerId, reason, itemType, static_cast<long long>(amount), consumeType, warehouse, static_cast<int>(GetWarehouseNum()))
            L2_LOG_ERROR(Log,
                "[warehouse] Administrative item grant failed for owner %u: %s (class %d, quantity %lld, consume type %d, requested warehouse %d, actual warehouse %d).",
                m_ownerId, reason, itemType, static_cast<long long>(amount), consumeType, warehouse, static_cast<int>(GetWarehouseNum()));
        };

        if (consumeType == 1 || consumeType == 3) {
            item = createItem();
            if (item) {
                item->SaveBaseAttribute();
                PushItem(item.get());
                *itemId = static_cast<int>(item->ItemID());
                resultingAmount = item->Amount();
                added = true;
                writeCreatePremiumLog();
                item->SetEnsoulInfo(ensoulInfo);
            } else {
                // logCreateFailure(
                //     L"AddItemByAdmin err, normal, charge. "
                //     L"nItemType(%d), nAmount(%d), nWarehouse(%d)"
                // )
                logCreateFailure("item creation returned no item");
            }
        } else if (consumeType == 2 || consumeType == 4) {
            if (amount < 1) {
                // logCreateFailure(
                //     L"2AddItemByAdmin err, asset, stackable. "
                //     L"nItemType(%d), nAmount(%d), nWarehouse(%d)"
                // )
                logCreateFailure("stack quantity must be positive");
            } else {
                item = GetItemByType(itemType, 0);
                if (!item) {
                    item = createItem();
                    if (item) {
                        PushItem(item.get());
                        *itemId = static_cast<int>(item->ItemID());
                        resultingAmount = item->Amount();
                        added = true;
                        writeCreatePremiumLog();
                    } else {
                        // logCreateFailure(
                        //     L"1AddItemByAdmin err, asset, stackable. "
                        //     L"nItemType(%d), nAmount(%d), nWarehouse(%d)"
                        // )
                        logCreateFailure("new stack creation returned no item");
                    }
                } else {
                    const std::int64_t previousAmount = item->Amount();
                    item->SetAmount(runtime::AddInt64Wrapping(previousAmount, amount));
                    // item->Save(true)
                    if (!item->Save(true)) {
                        L2_LOG_ERROR(Log,
                            "[admin item] Cannot save item %u for owner %u after changing stack quantity from %lld to %lld. The cached quantity remains changed; addition still reports success.",
                            item->ItemID(), m_ownerId, static_cast<long long>(previousAmount),
                            static_cast<long long>(item->Amount()));
                    }
                    *itemId = static_cast<int>(item->ItemID());
                    resultingAmount = item->Amount();
                    added = true;
                    if (g_readFile.IsPremiumItem(itemType)) {
                        item->LogPremiumRelated(
                            static_cast<PremiumItemLogType>(0), runtime::SubtractInt64Wrapping(resultingAmount, amount), resultingAmount,
                            static_cast<PremiumItemLogReason>(2)
                        );
                    }
                }
            }
        } else {
            // Log.AddT(LOG_ERROR, L"Item stack type is invalid. class id[%d]", itemType)
            // Log.Add(LOG_ERROR,
            //     "[warehouse] Administrative item grant rejected for owner %u: unsupported consume type %d for class %d, quantity %lld.",
            //     m_ownerId, consumeType, itemType, static_cast<long long>(amount))
            L2_LOG_ERROR(Log,
                "[warehouse] Administrative item grant rejected for owner %u: unsupported consume type %d for class %d, quantity %lld.",
                m_ownerId, consumeType, itemType, static_cast<long long>(amount));
        }

        if (item) {
            const int persistedItemId = static_cast<int>(item->ItemID());
            if (modifyPeriod != 0) {
                DBConn connection(runtime::process::CurrentDatabaseId());
                constexpr const wchar_t* statement = L"{ Call lin_ModItemPeriod (?, ?)}";
                connection.Prepare(statement);
                connection.BindParameter(&persistedItemId);
                connection.BindParameter(&period);
                // connection.ExecutePrepared(statement)
                if (!connection.ExecutePrepared(statement)) {
                    L2_LOG_ERROR(Log,
                        "[admin item] Cannot save period %d for item %d, owner %u. Remaining item updates and accounting continue; the addition result is unchanged.",
                        period, persistedItemId, m_ownerId);
                }
            }
            if (duration != 0) {
                DBConn connection(runtime::process::CurrentDatabaseId());
                constexpr const wchar_t* statement = L"{Call lin_UpdateItemDuration (?, ?)}";
                connection.Prepare(statement);
                connection.BindParameter(&persistedItemId);
                connection.BindParameter(&duration);
                // connection.ExecutePrepared(statement)
                if (!connection.ExecutePrepared(statement)) {
                    L2_LOG_ERROR(Log,
                        "[admin item] Cannot save duration %d for item %d, owner %u. Remaining item updates and accounting continue; the addition result is unchanged.",
                        duration, persistedItemId, m_ownerId);
                }
            }
            if (appearanceType != 0) {
                item->SetApperanceType(appearanceType, period, static_cast<ShapeShiftingJobType>(0));
            }
            // item->MakeEnsoulLogInfo(ensoulLog.data())
            item->MakeEnsoulLogInfo(ensoulLog.data(), ensoulLog.size());
        }
    }

    if (added) {
        CUserSP auditUser = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
        const WareHouseAuditActor actor = MakeWareHouseAuditActor(auditUser.get());
        std::array<std::u16string, 26> fields{};
        fields[0] = runtime::DecimalUtf16(0x27e);
        fields[1] = runtime::DecimalUtf16(actor.characterId);
        fields[2] = runtime::DecimalUtf16(actor.accountId);
        fields[5] = runtime::DecimalUtf16(actor.x);
        fields[6] = runtime::DecimalUtf16(actor.y);
        fields[7] = runtime::DecimalUtf16(actor.z);
        fields[9] = baseAttributeLog;
        fields[10] = variationLog;
        fields[17] = runtime::DecimalUtf16(enchant);
        fields[18] = runtime::DecimalUtf16(itemType);
        fields[19] = runtime::DecimalUtf16(amount);
        fields[20] = runtime::DecimalUtf16(resultingAmount);
        fields[21] = actor.characterName;
        fields[22] = actor.accountName;
        fields[25] = runtime::DecimalUtf16(*itemId);
        runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    }

    return added;
}

bool CWareHouse::AddItemByAdmin(
    int* itemId,
    int itemType,
    std::int64_t amount,
    int enchant,
    int eroded,
    int bless,
    int ident,
    int wished,
    int warehouse,
    ItemOptionKey optionKey,
    int intensiveItemType,
    BaseAttribute baseAttribute,
    short hLocked,
    short useCount,
    int appearanceType,
    int modifyPeriod,
    int period,
    int duration
) {
    // Exact current wrapper 0x00615914 constructs three empty ensoul maps and
    // delegates to the full WareHouse.cpp owner.
    L2_GUARD_FUNCTION();
    CItem::EnsoulInfo ensoulInfo[3];
    return AddItemByAdmin(
        itemId, itemType, amount, enchant, eroded, bless, ident, wished, warehouse, optionKey, intensiveItemType,
        baseAttribute, ensoulInfo, hLocked, useCount, appearanceType, modifyPeriod, period, duration
    );
}

bool CWareHouse::MoveItemToChar(int itemId, std::int64_t amount, CWareHouse* destinationWarehouse) {
    // Exact amount-aware current overload at Cached.exe 0x005f6118.
    L2_GUARD_FUNCTION();

    std::optional<CItemSP> sourceStorage;
    std::optional<CItemSP> destinationStorage;
    bool moved;
    {
        L2_WRITE_GUARD(*this);
        auto& sourceItem = sourceStorage.emplace(GetItem(itemId));
        if (!sourceItem) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[%S][%d] CWareHouse::MoveItemToChar failed. "
            //     L"nItemId(%d), from char_id(%d), to char_id(%d) ",
            //     "WareHouse.cpp", 0x43fb, itemId, static_cast<int>(m_ownerId), static_cast<int>(destinationWarehouse->m_ownerId)
            // )
            // Log.Add(LOG_ERROR,
            //     "[warehouse] Cannot move item %d from owner %u to owner %u%s, requested quantity %lld: source item not found.",
            //     itemId, m_ownerId, destinationWarehouse ? destinationWarehouse->m_ownerId : 0U,
            //     destinationWarehouse ? "" : " (destination unavailable)", static_cast<long long>(amount))
            L2_LOG_ERROR(Log,
                "[warehouse] Cannot move item %d from owner %u to owner %u%s, requested quantity %lld: source item not found.",
                itemId, m_ownerId, destinationWarehouse ? destinationWarehouse->m_ownerId : 0U,
                destinationWarehouse ? "" : " (destination unavailable)", static_cast<long long>(amount));
            return false;
        }
        if (sourceItem->Amount() < amount) {
            return false;
        }

        const int consumeType = g_readFile.GetConsumeType(static_cast<int>(sourceItem->ItemType()));
        moved = false;

        if (consumeType == 1 || consumeType == 3) {
            if (amount != 1) {
                return false;
            }

            if (sourceItem->Ident() > 0) {
                sourceItem->Set(
                    sourceItem->OwnerID(), sourceItem->ItemType(), sourceItem->Amount(), sourceItem->Enchant(),
                    sourceItem->Eroded(), sourceItem->Bless(), 0, sourceItem->Wished(), sourceItem->Warehouse(),
                    sourceItem->OptionKey(), sourceItem->m_intensiveItemType, sourceItem->Attribute(), -1,
                    sourceItem->AppearanceType(), sourceItem->HLocked(), sourceItem->UseCount()
                );
                // sourceItem->Save(true)
                if (!sourceItem->Save(true)) {
                    L2_LOG_ERROR(Log,
                        "[warehouse] Cannot save cleared identification for item %d, source owner %u; owner change and in-memory transfer still continue.",
                        itemId, m_ownerId);
                }
            }

            sourceItem->setOwnerID(destinationWarehouse->m_ownerId);
            sourceItem->SetWarehouse(static_cast<int>(destinationWarehouse->GetWarehouseNum()));
            // sourceItem->Save(true)
            if (!sourceItem->Save(true)) {
                L2_LOG_ERROR(Log,
                    "[warehouse] Cannot save item %d with destination owner %u, warehouse %d; in-memory transfer from owner %u still continues.",
                    itemId, destinationWarehouse->m_ownerId,
                    static_cast<int>(destinationWarehouse->GetWarehouseNum()), m_ownerId);
            }
            PopItem(itemId);
            sourceItem->Delete(false, false);
            CItem::CacheItem(sourceItem.get());
            RemoveEquipped(itemId);
            destinationWarehouse->PushItem(sourceItem.get());
            moved = true;
        } else if (consumeType == 2 || consumeType == 4) {
            auto& destinationItem = destinationStorage.emplace(
                destinationWarehouse->GetItemByType(static_cast<int>(sourceItem->ItemType()), 0)
            );
            if (!destinationItem) {
                destinationItem = destinationWarehouse->MakeNewItem(
                    destinationWarehouse->m_ownerId, sourceItem->ItemType(), amount, sourceItem->Enchant(), sourceItem->Eroded(),
                    sourceItem->Bless(), sourceItem->Ident(), sourceItem->Wished(),
                    static_cast<int>(destinationWarehouse->GetWarehouseNum()), sourceItem->OptionKey(),
                    sourceItem->m_intensiveItemType, sourceItem->Attribute(), sourceItem->HLocked(), sourceItem->UseCount()
                );
                if (!destinationItem) {
                    return false;
                }
                // destinationItem->Save(true)
                if (!destinationItem->Save(true)) {
                    L2_LOG_ERROR(Log,
                        "[warehouse] Cannot save newly created destination item %u for owner %u while moving source item %d from owner %u. Destination insertion and source removal still continue.",
                        destinationItem->ItemID(), destinationWarehouse->m_ownerId, itemId, m_ownerId);
                }
                destinationWarehouse->PushItem(destinationItem.get());
                if (g_readFile.IsPremiumItem(static_cast<int>(destinationItem->ItemType()))) {
                    destinationItem->LogPremiumRelated(
                        static_cast<PremiumItemLogType>(0), 0, amount, static_cast<PremiumItemLogReason>(2)
                    );
                }
            } else {
                destinationItem->SetAmount(runtime::AddInt64Wrapping(destinationItem->Amount(), amount));
                // destinationItem->Save(true)
                if (!destinationItem->Save(true)) {
                    L2_LOG_ERROR(Log,
                        "[warehouse] Cannot save destination item %u for owner %u at quantity %lld while moving source item %d. The cached quantity remains changed; source removal still continues.",
                        destinationItem->ItemID(), destinationWarehouse->m_ownerId,
                        static_cast<long long>(destinationItem->Amount()), itemId);
                }
            }

            if (sourceItem->Amount() > amount) {
                sourceItem->SetAmount(runtime::SubtractInt64Wrapping(sourceItem->Amount(), amount));
                // sourceItem->Save(true)
                if (!sourceItem->Save(true)) {
                    L2_LOG_ERROR(Log,
                        "[warehouse] Cannot save source item %d for owner %u at remaining quantity %lld after destination handling. The cached quantity remains changed; transfer still reports success.",
                        itemId, m_ownerId, static_cast<long long>(sourceItem->Amount()));
                }
            } else {
                PopItem(itemId);
                RemoveEquipped(itemId);
                sourceItem->Delete(true, true);
            }
            moved = true;
            destinationStorage.reset();
        }
    }
    return moved;
}

bool CWareHouse::SeizeItem(int option, int itemId, std::int64_t amount, int unused [[maybe_unused]], int warehouse) {
    // Current Cached.exe 0x005f6ec4. The fourth explicit argument survives
    // in the guard/PDB signature but is not read. The fifth argument is the
    // destination warehouse written by lin_SeizeItem2/new-item paths.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    bool seized = false;
    {
        L2_WRITE_GUARD(*this);

        if (option == 1) {
            CItemSP item = GetItem(itemId);
            if (item && item->Amount() >= amount) {
                const int consumeType = g_readFile.GetConsumeType(static_cast<int>(item->ItemType()));
                bool removeWholeItem = false;

                if (consumeType == 1 || consumeType == 3) {
                    if (amount == 1) {
                        PopItem(itemId);
                        removeWholeItem = true;
                    }
                } else if (consumeType == 2 || consumeType == 4) {
                    if (amount < item->Amount()) {
                        item->SetAmount(runtime::SubtractInt64Wrapping(item->Amount(), amount));
                        // item->Save(true)
                        if (!item->Save(true)) {
                            L2_LOG_ERROR(Log,
                                "[warehouse] Cannot save remaining quantity %lld for item %d, owner %u during seizure. The cached quantity is reduced; creation of the seized stack still follows.",
                                static_cast<long long>(item->Amount()), itemId, m_ownerId);
                        }

                        unsigned int createdItemId = 0;
                        const unsigned int ownerId = m_ownerId;
                        const unsigned int itemType = item->ItemType();
                        const long long createdAmount = static_cast<long long>(amount);
                        const int enchant = item->Enchant();
                        const int eroded = item->Eroded();
                        const int bless = item->Bless();
                        const int ident = item->Ident();
                        const int wished = item->Wished();
                        const ItemOptionKey optionKey = item->OptionKey();
                        const int intensiveItemType = item->m_intensiveItemType;
                        const short hLocked = item->HLocked();
                        const short useCount = item->UseCount();
                        // ExecuteWarehouseCreateItem(
                        //     connection, createdItemId, ownerId, itemType, createdAmount, enchant, eroded, bless, ident, wished,
                        //     warehouse, optionKey, intensiveItemType, hLocked, useCount
                        // )
                        if (!ExecuteWarehouseCreateItem(
                                connection, createdItemId, ownerId, itemType, createdAmount, enchant, eroded, bless, ident, wished,
                                warehouse, optionKey, intensiveItemType, hLocked, useCount)) {
                            L2_LOG_ERROR(Log,
                                "[warehouse] Seized-stack creation was not confirmed for source item %d, owner %u, quantity %lld, destination warehouse %d: execution or result fetch failed. Seizure still reports success and premium accounting continues when applicable.",
                                itemId, ownerId, createdAmount, warehouse);
                        }
                        seized = true;

                        if (g_readFile.IsPremiumItem(static_cast<int>(itemType))) {
                            CUserSP user =
                                g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
                            const unsigned int accountId = user ? static_cast<unsigned int>(user->GetAccountID()) : 0U;
                            WriteWarehousePremiumCreateLog(
                                accountId, static_cast<int>(m_ownerId), static_cast<int>(createdItemId), amount, itemType
                            );
                        }
                        connection.UnBind();
                    } else {
                        PopItem(itemId);
                        removeWholeItem = true;
                    }
                }

                if (removeWholeItem) {
                    RemoveEquipped(itemId);
                    item->Delete(false, false);
                    // ExecuteWarehouseSeizeItem2(connection, option, warehouse, itemId)
                    if (!ExecuteWarehouseSeizeItem2(connection, option, warehouse, itemId)) {
                        L2_LOG_ERROR(Log,
                            "[warehouse] Cannot persist seizure of item %d, owner %u into warehouse %d. Removal from the source warehouse already ran; seizure still reports success.",
                            itemId, m_ownerId, warehouse);
                    }
                    seized = true;
                }
            }
        } else {
            unsigned int loadedOwnerId = 0;
            unsigned int itemType = 0;
            long long loadedAmount = 0;
            int enchant = 0;
            int eroded = 0;
            int bless = 0;
            int ident = 0;
            int wished = 0;
            int sourceWarehouse = 0;
            ItemOptionKey optionKey;
            int intensiveItemType = 0;
            int inventorySlotIndex = -1;
            int appearanceType = 0;
            BaseAttribute baseAttribute;
            short hLocked = 0;
            short useCount = 0;

            connection.Bind(&loadedOwnerId);
            connection.Bind(&itemType);
            connection.Bind(&loadedAmount);
            connection.Bind(&enchant);
            connection.Bind(&eroded);
            connection.Bind(&bless);
            connection.Bind(&ident);
            connection.Bind(&wished);
            connection.Bind(&sourceWarehouse);
            connection.Bind(&optionKey.variationOption1);
            connection.Bind(&optionKey.variationOption2);
            connection.Bind(&intensiveItemType);
            connection.Bind(&inventorySlotIndex);
            connection.Bind(&appearanceType);
            connection.Bind(&baseAttribute.m_nAttackType);
            connection.Bind(&baseAttribute.m_nAttackValue);
            for (short& defendValue : baseAttribute.m_nDefendValue) {
                connection.Bind(&defendValue);
            }
            connection.Bind(&hLocked);
            connection.Bind(&useCount);

            constexpr const wchar_t* loadStatement = L"{Call lin_LoadItemByItemId (?)}";
            connection.Prepare(loadStatement);
            connection.BindParameter(&itemId);
            const bool loaded = connection.ExecutePrepared(loadStatement) && connection.Fetch();
            connection.UnBind();

            const bool commissionWarehouse = sourceWarehouse >= 1001 && sourceWarehouse <= 1004;
            if (loaded && commissionWarehouse && loadedAmount >= amount) {
                const int consumeType = g_readFile.GetConsumeType(static_cast<int>(itemType));

                const auto makeLoadedItem = [&](bool loadEnsoul) -> CItem* {
                    CItem* item = nullptr;
                    try {
                        item = new CItem(
                            static_cast<unsigned int>(itemId), loadedOwnerId, itemType, amount, enchant, eroded, bless, ident,
                            wished, warehouse, optionKey, intensiveItemType, baseAttribute, inventorySlotIndex, appearanceType,
                            hLocked, useCount
                        );
                    } catch (const std::bad_alloc&) {
                        return nullptr;
                    }
                    if (loadEnsoul)
                        item->LoadEnsoulInfo();
                    return item;
                };

                if (consumeType == 1 || consumeType == 3) {
                    CItem* item = makeLoadedItem(true);
                    if (item) {
                        PushItem(item);
                        // ExecuteWarehouseSeizeItem2(connection, option, warehouse, itemId)
                        if (!ExecuteWarehouseSeizeItem2(connection, option, warehouse, itemId)) {
                            L2_LOG_ERROR(Log,
                                "[warehouse] Cannot persist return of seized item %d from warehouse %d to %d for owner %u. The item is already inserted into the cached warehouse; the operation still reports success.",
                                itemId, sourceWarehouse, warehouse, loadedOwnerId);
                        }
                        L2_RELEASE(item, ObjectRefType::CreateDelete);
                        seized = true;
                    }
                } else if (consumeType == 2 || consumeType == 4) {
                    CItemSP destinationItem = GetItemByType(static_cast<int>(itemType), 0);
                    if (!destinationItem) {
                        if (loadedAmount == amount) {
                            CItem* item = makeLoadedItem(false);
                            if (item) {
                                PushItem(item);
                                // ExecuteWarehouseSeizeItem2(connection, option, warehouse, itemId)
                                if (!ExecuteWarehouseSeizeItem2(connection, option, warehouse, itemId)) {
                                    L2_LOG_ERROR(Log,
                                        "[warehouse] Cannot persist return of seized stack %d from warehouse %d to %d for owner %u. The stack is already inserted into the cached warehouse; the operation still reports success.",
                                        itemId, sourceWarehouse, warehouse, loadedOwnerId);
                                }
                                L2_RELEASE(item, ObjectRefType::CreateDelete);
                                seized = true;
                            }
                        } else {
                            CItemSP newItem = MakeNewItem(
                                m_ownerId, itemType, amount, enchant, eroded, bless, ident, wished, warehouse, optionKey,
                                intensiveItemType, baseAttribute, hLocked, useCount
                            );
                            if (newItem) {
                                // newItem->Save(true)
                                if (!newItem->Save(true)) {
                                    L2_LOG_ERROR(Log,
                                        "[warehouse] Cannot save new stack %u for owner %u while returning %lld units of seized item %d. Cached insertion and source quantity update still follow.",
                                        newItem->ItemID(), m_ownerId, static_cast<long long>(amount), itemId);
                                }
                                PushItem(newItem.get());

                                const long long remainingAmount = static_cast<long long>(runtime::SubtractInt64Wrapping(loadedAmount, amount));
                                // ExecuteWarehouseItemWrite(
                                //     connection, loadedOwnerId, itemType, remainingAmount, enchant, eroded, bless, ident, wished,
                                //     sourceWarehouse, static_cast<unsigned int>(itemId), optionKey, intensiveItemType,
                                //     inventorySlotIndex, hLocked, useCount
                                // )
                                if (!ExecuteWarehouseItemWrite(
                                    connection, loadedOwnerId, itemType, remainingAmount, enchant, eroded, bless, ident, wished,
                                    sourceWarehouse, static_cast<unsigned int>(itemId), optionKey, intensiveItemType,
                                    inventorySlotIndex, hLocked, useCount
                                )) {
                                    L2_LOG_ERROR(Log,
                                        "[warehouse] Cannot save remaining quantity %lld for seized item %d in warehouse %d after returning %lld units. The new stack is already cached; return still reports success.",
                                        remainingAmount, itemId, sourceWarehouse, static_cast<long long>(amount));
                                }
                                seized = true;
                                if (g_readFile.IsPremiumItem(static_cast<int>(itemType))) {
                                    newItem->LogPremiumRelated(
                                        static_cast<PremiumItemLogType>(0), 0, amount, static_cast<PremiumItemLogReason>(2)
                                    );
                                }
                            }
                        }
                    } else {
                        destinationItem->SetAmount(runtime::AddInt64Wrapping(destinationItem->Amount(), amount));
                        // destinationItem->Save(true)
                        if (!destinationItem->Save(true)) {
                            L2_LOG_ERROR(Log,
                                "[warehouse] Cannot save destination stack %u for owner %u after merging %lld units of seized item %d. Cached quantity remains changed; source cleanup still follows.",
                                destinationItem->ItemID(), m_ownerId, static_cast<long long>(amount), itemId);
                        }
                        if (loadedAmount == amount) {
                            // ExecuteWarehouseDeleteItemRow(connection, itemId)
                            if (!ExecuteWarehouseDeleteItemRow(connection, itemId)) {
                                L2_LOG_ERROR(Log,
                                    "[warehouse] Cannot delete seized item %d from warehouse %d after merging it into stack %u for owner %u. Return still reports success.",
                                    itemId, sourceWarehouse, destinationItem->ItemID(), m_ownerId);
                            }
                        } else {
                            const long long remainingAmount = static_cast<long long>(runtime::SubtractInt64Wrapping(loadedAmount, amount));
                            // ExecuteWarehouseItemWrite(
                            //     connection, loadedOwnerId, itemType, remainingAmount, enchant, eroded, bless, ident, wished,
                            //     sourceWarehouse, static_cast<unsigned int>(itemId), optionKey, intensiveItemType,
                            //     inventorySlotIndex, hLocked, useCount
                            // )
                            if (!ExecuteWarehouseItemWrite(
                                connection, loadedOwnerId, itemType, remainingAmount, enchant, eroded, bless, ident, wished,
                                sourceWarehouse, static_cast<unsigned int>(itemId), optionKey, intensiveItemType,
                                inventorySlotIndex, hLocked, useCount
                            )) {
                                L2_LOG_ERROR(Log,
                                    "[warehouse] Cannot save remaining quantity %lld for seized item %d in warehouse %d after merging %lld units into stack %u. Return still reports success.",
                                    remainingAmount, itemId, sourceWarehouse, static_cast<long long>(amount), destinationItem->ItemID());
                            }
                        }
                        seized = true;
                    }
                }
            }
        }
    }
    return seized;
}

bool CWareHouse::WritePost(
    CSocket* socket [[maybe_unused]],
    CWareHouse* destinationWarehouse,
    int itemCount,
    const int itemIds[],
    const std::int64_t amounts[],
    int resultingItemIds[],
    std::int64_t remainingAmounts[],
    int consumeTypes[],
    std::int64_t fee,
    int& adenaItemId,
    std::int64_t& remainingAdena,
    int auditContext [[maybe_unused]]
) {
    // Current Cached.exe 0x005f7f1c. The target keeps the raw ordered map
    // because the rollback copy's change scope participates in the item
    // audit. Keep that ownership and commit order here as well.
    L2_GUARD_FUNCTION();

    if (!destinationWarehouse || itemCount < 0 ||
        (itemCount > 0 && (!itemIds || !amounts || !resultingItemIds || !remainingAmounts || !consumeTypes))) {
        // Log.Add(LOG_ERROR,
        //     "[mail] Cannot prepare attachments for owner %u, recipient %d (count %d): %s. No item or fee changes attempted.",
        //     m_ownerId, auditContext, itemCount, !destinationWarehouse ? "destination warehouse is unavailable" : (itemCount < 0 ? "negative attachment count" : "required item arrays are missing"))
        L2_LOG_ERROR(Log,
            "[mail] Cannot prepare attachments for owner %u, recipient %d (count %d): %s; no item or fee changes attempted.",
            m_ownerId, auditContext, itemCount, !destinationWarehouse ? "destination warehouse is unavailable" : (itemCount < 0 ? "negative attachment count" : "required item arrays are missing"));
        return false;
    }

    CItemTransactionOrderMap transactionItems;
    int transactionOrder = 0;

    auto stageItem = [&](CItemSP item, TransactType rollbackMode) -> CItem* {
        if (!item)
            return nullptr;
        item->SetTransation();
        CItemSP rollbackItem = item->Copy();
        if (!rollbackItem)
            return nullptr;
        rollbackItem->SetTrasactMode(rollbackMode);
        L2_ADD_REF(item, ObjectRefType::FunctionLocal);
        L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
        transactionItems.emplace(++transactionOrder, CItemKey(rollbackItem.get(), item.get()));
        return rollbackItem.get();
    };

    auto rollback = [&]() {
        CTransaction::RollbackTransactOrderMap(transactionItems);
        transactionItems.clear();
        return false;
    };

    auto commit = [&]() {
        for (const auto& entry : transactionItems) {
            CItem* rollbackItem = entry.second.rollbackItem;
            CItem* item = entry.second.item;
            // item->CommitTransation(rollbackItem, true)
            if (!item->CommitTransation(rollbackItem, true)) {
                // Log.Add(LOG_ERROR,
                //     "[mail] Attachment/fee commit rejected for item %u during mail preparation by owner %u, recipient %d. Remaining entries still processed and warehouse preparation still reports success.",
                //     item->ItemID(), m_ownerId, auditContext)
                L2_LOG_ERROR(Log,
                    "[mail] Attachment/fee commit rejected for item %u during mail preparation by owner %u, recipient %d; remaining entries still processed and warehouse preparation still reports success.",
                    item->ItemID(), m_ownerId, auditContext);
            }
            L2_RELEASE(item, ObjectRefType::FunctionLocal);
            L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
        }
        transactionItems.clear();
        return true;
    };

    for (int index = 0; index < itemCount; ++index) {
        CItemSP sourceItem = GetItem(itemIds[index]);
        if (!sourceItem) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Attachment %d of %d for owner %u, recipient %d was not found (item %d). Remaining attachments skipped. Fee processing and commit attempts for already staged items continue.",
            //     index + 1, itemCount, m_ownerId, auditContext, itemIds[index])
            L2_LOG_ERROR(Log,
                "[mail] Attachment %d of %d for owner %u, recipient %d was not found (item %d); remaining attachments skipped. Fee processing and commit attempts for already staged items continue.",
                index + 1, itemCount, m_ownerId, auditContext, itemIds[index]);
            // This break, instead of an immediate rollback, is present in
            // the shipping control flow.
            break;
        }

        if (!stageItem(sourceItem, TransactType_RollbackChanges)) {
            return rollback();
        }

        const std::int64_t sourceAmount = sourceItem->Amount();
        const std::int64_t requestedAmount = amounts[index];
        if (sourceAmount < requestedAmount) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Attachment %d of %d for owner %u, recipient %d has insufficient quantity (item %d: available %lld, requested %lld). Remaining attachments skipped. Fee processing and commit attempts for staged items continue.",
            //     index + 1, itemCount, m_ownerId, auditContext, itemIds[index], static_cast<long long>(sourceAmount), static_cast<long long>(requestedAmount))
            L2_LOG_ERROR(Log,
                "[mail] Attachment %d of %d for owner %u, recipient %d has insufficient quantity (item %d: available %lld, requested %lld); remaining attachments skipped. Fee processing and commit attempts for staged items continue.",
                index + 1, itemCount, m_ownerId, auditContext, itemIds[index], static_cast<long long>(sourceAmount), static_cast<long long>(requestedAmount));
            // The current target leaves already-staged entries intact and
            // continues with the fee/commit tail.
            break;
        }

        if (consumeTypes[index] == ICT_STACKABLE || consumeTypes[index] == ICT_ASSET) {
            if (sourceAmount == requestedAmount) {
                sourceItem->SetWarehouse(destinationWarehouse->GetWarehouseNum());
                sourceItem->m_ident = 0;
                sourceItem->SetChangeScope(ItemChangeScope_Decreased);
                resultingItemIds[index] = static_cast<int>(sourceItem->ItemID());
            } else {
                sourceItem->SetAmount(runtime::SubtractInt64Wrapping(sourceAmount, requestedAmount));
                CItemSP destinationItem = destinationWarehouse->MakeNewItem(
                    destinationWarehouse->m_ownerId, sourceItem->ItemType(), requestedAmount, sourceItem->Enchant(),
                    sourceItem->Eroded(), sourceItem->Bless(), 0, sourceItem->Wished(),
                    destinationWarehouse->GetWarehouseNum(), sourceItem->OptionKey(), sourceItem->m_intensiveItemType,
                    sourceItem->Attribute(), sourceItem->HLocked(), sourceItem->UseCount()
                );
                if (!destinationItem) {
                    // Log.Add(LOG_ERROR,
                    //     "[mail] Cannot create attachment %d of %d for owner %u, recipient %d from item %d, quantity %lld. Source quantity is already %lld in memory. Remaining attachments skipped. Fee processing and commit attempts continue.",
                    //     index + 1, itemCount, m_ownerId, auditContext, itemIds[index], static_cast<long long>(requestedAmount), static_cast<long long>(sourceItem->Amount()))
                    L2_LOG_ERROR(Log,
                        "[mail] Cannot create attachment %d of %d for owner %u, recipient %d from item %d, quantity %lld; source quantity is already %lld in memory. Remaining attachments skipped; fee processing and commit attempts continue.",
                        index + 1, itemCount, m_ownerId, auditContext, itemIds[index], static_cast<long long>(requestedAmount), static_cast<long long>(sourceItem->Amount()));
                    // MakeNewItem failure follows the same partial-loop exit
                    // as the current target.
                    break;
                }
                if (!stageItem(destinationItem, TransactType_RollbackCreation)) {
                    return rollback();
                }
                sourceItem->SetChangeScope(ItemChangeScope_Added);
                destinationItem->SetChangeScope(ItemChangeScope_Increased);
                resultingItemIds[index] = static_cast<int>(destinationItem->ItemID());
            }
        } else {
            sourceItem->SetWarehouse(destinationWarehouse->GetWarehouseNum());
            sourceItem->m_ident = 0;
            sourceItem->SetChangeScope(ItemChangeScope_Deleted);
            resultingItemIds[index] = itemIds[index];
        }

        remainingAmounts[index] = runtime::SubtractInt64Wrapping(sourceAmount, requestedAmount);
    }

    if (fee > 0) {
        CItemSP adena = GetItemByType(57, 0);
        // if (!adena)
            // return rollback()
        if (!adena) {
            // Log.Add(LOG_WARN,
            //     "[mail] Fee payment rejected for owner %u, recipient %d: no adena stack for fee %lld. Attempting rollback of attachment and fee changes.",
            //     m_ownerId, auditContext, static_cast<long long>(fee))
            L2_LOG_WARN(Log,
                "[mail] Fee payment rejected for owner %u, recipient %d: no adena stack for fee %lld; attempting rollback of attachment and fee changes.",
                m_ownerId, auditContext, static_cast<long long>(fee));
            return rollback();
        }

        CItem* adenaRollback = nullptr;
        for (const auto& entry : transactionItems) {
            if (entry.second.item == adena.get()) {
                adenaRollback = entry.second.rollbackItem;
                break;
            }
        }
        if (!adenaRollback) {
            adenaRollback = stageItem(adena, TransactType_RollbackChanges);
        }
        if (!adenaRollback)
            return rollback();
        adenaRollback->SetChangeScope(ItemChangeScope_Fee);

        const std::int64_t originalAmount = adena->Amount();
        // if (originalAmount < fee)
            // return rollback()
        if (originalAmount < fee) {
            // Log.Add(LOG_WARN,
            //     "[mail] Fee payment rejected for owner %u, recipient %d: item %u has %lld adena, fee %lld. Attempting rollback of attachment and fee changes.",
            //     m_ownerId, auditContext, adena->ItemID(), static_cast<long long>(originalAmount), static_cast<long long>(fee))
            L2_LOG_WARN(Log,
                "[mail] Fee payment rejected for owner %u, recipient %d: item %u has %lld adena, fee %lld; attempting rollback of attachment and fee changes.",
                m_ownerId, auditContext, adena->ItemID(), static_cast<long long>(originalAmount), static_cast<long long>(fee));
            return rollback();
        }
        remainingAdena = runtime::SubtractInt64Wrapping(originalAmount, fee);
        adenaItemId = static_cast<int>(adena->ItemID());
        adena->SetAmount(remainingAdena);
        if (remainingAdena == 0) {
            adena->TransactDelete();
        }
    }

    return commit();
}

int CWareHouse::AssembleOpenPost(CPacketHelperEx<20480>& packet, int itemCount, int itemIds[]) {
    // Current Cached.exe 0x00600074. Missing items are logged and skipped.
    // the shipping loop stops only after serializing its 200th valid item.
    L2_GUARD_FUNCTION();

    int assembledItemCount = 0;
    for (int index = 0; index < itemCount; ++index) {
        CItemSP item = GetItem(itemIds[index]);
        if (!item) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[AssembleOpenPost] Item invalid! Item Id [%d] "
            //     L"user id [%d]",
            //     itemIds[index], static_cast<int>(m_ownerId)
            // )
            // Log.Add(LOG_ERROR,
            //     "[mail] Cannot include attachment %d of %d for owner %u, warehouse %d: item %d was not found. Skipping this attachment in the open-mail reply.",
            //     index + 1, itemCount, m_ownerId, static_cast<int>(m_warehouseType), itemIds[index])
            L2_LOG_ERROR(Log,
                "[mail] Cannot include attachment %d of %d for owner %u, warehouse %d: item %d was not found; skipping this attachment in the open-mail reply.",
                index + 1, itemCount, m_ownerId, static_cast<int>(m_warehouseType), itemIds[index]);
            continue;
        }

        // CItemAssemblerSendToL2Server assembler(*item.operator->(), 0x7f)
        CItemAssemblerSendToL2Server assembler(*item.operator->(), ItemAssembleType::ALL);
        assembler.AssembleItemInfoImpl(packet);
        ++assembledItemCount;
        // if (assembledItemCount > 199)
        //     break.
        if (assembledItemCount > 199) {
            if (index + 1 < itemCount) {
                L2_LOG_WARN(Log,
                    "[mail] Open-mail reply for owner %u, warehouse %d reached the limit of 200 attachments. The remaining %d requested entries were not examined or included.",
                    m_ownerId, static_cast<int>(m_warehouseType), itemCount - index - 1);
            }
            break;
        }
    }
    return assembledItemCount;
}

bool CWareHouse::CancelPost(
    CSocket* socket,
    CWareHouse* destinationWarehouse,
    int itemCount,
    const int itemIds[],
    const std::int64_t amounts[],
    int consumeTypes[],
    int postId,
    int requestContext,
    int senderId,
    int senderServerId
) {
    // Current Cached.exe 0x00600278. The current owner assembles every
    // changed destination item with mask 0x7f, sends the successful 0x75
    // reply, and only then commits the ordered item transaction.
    L2_GUARD_FUNCTION();
    if (!socket || !destinationWarehouse || itemCount < 0 || (itemCount > 0 && (!itemIds || !amounts || !consumeTypes))) {
        // Log.Add(LOG_ERROR,
        //     "[mail] Cannot cancel mail %d for owner %u (attachment count %d): %s. No attachment changes or cancellation reply from this warehouse operation.",
        //     postId, m_ownerId, itemCount, !socket ? "reply socket is unavailable" : (!destinationWarehouse ? "destination warehouse is unavailable" : (itemCount < 0 ? "negative attachment count" : "required item arrays are missing")))
        L2_LOG_ERROR(Log,
            "[mail] Cannot cancel mail %d for owner %u (attachment count %d): %s; no attachment changes or cancellation reply from this warehouse operation.",
            postId, m_ownerId, itemCount, !socket ? "reply socket is unavailable" : (!destinationWarehouse ? "destination warehouse is unavailable" : (itemCount < 0 ? "negative attachment count" : "required item arrays are missing")));
        return false;
    }

    CItemTransactionOrderMap transactionItems;
    int transactionOrder = 0;
    CPacketHelperEx<20480> responseItems;
    int responseItemCount = 0;

    auto stageItem = [&](CItemSP item, TransactType rollbackMode) -> bool {
        if (!item)
            return false;
        item->SetTransation();
        CItemSP rollbackItem = item->Copy();
        if (!rollbackItem)
            return false;
        rollbackItem->SetTrasactMode(rollbackMode);
        L2_ADD_REF(item, ObjectRefType::FunctionLocal);
        L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
        transactionItems.emplace(++transactionOrder, CItemKey(rollbackItem.get(), item.get()));
        return true;
    };
    auto fail = [&]() {
        socket->Send("chdddddd", 0xc6, 0x75, 0, postId, requestContext, senderId, senderServerId, 0);
        CTransaction::RollbackTransactOrderMap(transactionItems);
        transactionItems.clear();
        return false;
    };
    auto assembleItem = [&](CItem& item) {
        // CItemAssemblerSendToL2Server assembler(item, 0x7f)
        CItemAssemblerSendToL2Server assembler(item, ItemAssembleType::ALL);
        assembler.AssembleItemInfoImpl(responseItems);
    };

    for (int index = 0; index < itemCount; ++index) {
        CItemSP sourceItem = GetItem(itemIds[index]);
        if (!sourceItem || !stageItem(sourceItem, TransactType_RollbackChanges)) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Cannot cancel mail %d for owner %u: attachment %d of %d, item %d, %s. Failure reply and an attachment rollback attempt follow.",
            //     postId, m_ownerId, index + 1, itemCount, itemIds[index], sourceItem ? "could not be prepared for the transaction" : "was not found")
            L2_LOG_ERROR(Log,
                "[mail] Cannot cancel mail %d for owner %u: attachment %d of %d, item %d, %s; failure reply and an attachment rollback attempt follow.",
                postId, m_ownerId, index + 1, itemCount, itemIds[index], sourceItem ? "could not be prepared for the transaction" : "was not found");
            return fail();
        }

        const std::int64_t sourceAmount = sourceItem->Amount();
        const std::int64_t requestedAmount = amounts[index];
        // if (sourceAmount < requestedAmount)
            // return fail()
        if (sourceAmount < requestedAmount) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Cannot cancel mail %d for owner %u: attachment %d of %d, item %d, has quantity %lld, requested %lld. Failure reply and an attachment rollback attempt follow.",
            //     postId, m_ownerId, index + 1, itemCount, itemIds[index], static_cast<long long>(sourceAmount), static_cast<long long>(requestedAmount))
            L2_LOG_ERROR(Log,
                "[mail] Cannot cancel mail %d for owner %u: attachment %d of %d, item %d, has quantity %lld, requested %lld; failure reply and an attachment rollback attempt follow.",
                postId, m_ownerId, index + 1, itemCount, itemIds[index], static_cast<long long>(sourceAmount), static_cast<long long>(requestedAmount));
            return fail();
        }

        ++responseItemCount;
        if (consumeTypes[index] == ICT_STACKABLE || consumeTypes[index] == ICT_ASSET) {
            CItemSP destinationItem = destinationWarehouse->GetItemByType(static_cast<int>(sourceItem->ItemType()), 0);
            if (!destinationItem) {
                if (sourceAmount == requestedAmount) {
                    sourceItem->SetWarehouse(destinationWarehouse->GetWarehouseNum());
                    sourceItem->m_ident = 0;
                    assembleItem(*sourceItem.get());
                    sourceItem->SetChangeScope(ItemChangeScope_Decreased);
                } else {
                    sourceItem->SetAmount(runtime::SubtractInt64Wrapping(sourceAmount, requestedAmount));
                    destinationItem = destinationWarehouse->MakeNewItem(
                        destinationWarehouse->m_ownerId, sourceItem->ItemType(), requestedAmount, sourceItem->Enchant(),
                        sourceItem->Eroded(), sourceItem->Bless(), 0, sourceItem->Wished(),
                        destinationWarehouse->GetWarehouseNum(), sourceItem->OptionKey(), sourceItem->m_intensiveItemType,
                        sourceItem->Attribute(), sourceItem->HLocked(), sourceItem->UseCount()
                    );
                    if (!destinationItem || !stageItem(destinationItem, TransactType_RollbackCreation)) {
                        // Log.Add(LOG_ERROR,
                        //     "[mail] Cannot cancel mail %d for owner %u: %s for attachment %d of %d, item %d, quantity %lld (destination owner %u). Source quantity is already %lld in memory. Failure reply and an attachment rollback attempt follow.",
                        //     postId, m_ownerId, destinationItem ? "destination transaction preparation failed" : "destination item creation failed", index + 1, itemCount, itemIds[index], static_cast<long long>(requestedAmount), destinationWarehouse->m_ownerId, static_cast<long long>(sourceItem->Amount()))
                        L2_LOG_ERROR(Log,
                            "[mail] Cannot cancel mail %d for owner %u: %s for attachment %d of %d, item %d, quantity %lld (destination owner %u); source quantity is already %lld in memory. Failure reply and an attachment rollback attempt follow.",
                            postId, m_ownerId, destinationItem ? "destination transaction preparation failed" : "destination item creation failed", index + 1, itemCount, itemIds[index], static_cast<long long>(requestedAmount), destinationWarehouse->m_ownerId, static_cast<long long>(sourceItem->Amount()));
                        return fail();
                    }
                    assembleItem(*destinationItem.get());
                    sourceItem->SetChangeScope(ItemChangeScope_Added);
                    destinationItem->SetChangeScope(ItemChangeScope_Fee);
                }
            } else {
                if (!stageItem(destinationItem, TransactType_RollbackChanges)) {
                    return fail();
                }
                sourceItem->SetAmount(runtime::SubtractInt64Wrapping(sourceAmount, requestedAmount));
                destinationItem->SetAmount(runtime::AddInt64Wrapping(destinationItem->Amount(), requestedAmount));
                destinationItem->m_ident = 0;
                if (sourceAmount == requestedAmount) {
                    sourceItem->TransactDelete();
                }
                assembleItem(*destinationItem.get());
                sourceItem->SetChangeScope(ItemChangeScope_Added);
                destinationItem->SetChangeScope(ItemChangeScope_Increased);
            }
        } else {
            sourceItem->SetWarehouse(destinationWarehouse->GetWarehouseNum());
            sourceItem->m_ident = 0;
            assembleItem(*sourceItem.get());
            sourceItem->SetChangeScope(ItemChangeScope_Deleted);
        }
    }

    socket->Send(
        "chddddddb", 0xc6, 0x75, 1, postId, requestContext, senderId, senderServerId, responseItemCount,
        responseItems.GetLength(), responseItems.GetPacket()
    );

    for (const auto& entry : transactionItems) {
        CItem* rollbackItem = entry.second.rollbackItem;
        CItem* item = entry.second.item;
        // item->CommitTransation(rollbackItem, true)
        if (!item->CommitTransation(rollbackItem, true)) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Cancellation commit rejected for item %u, mail %d, owner %u. Success reply already sent, remaining entries still processed and cancellation still reports success.",
            //     item->ItemID(), postId, m_ownerId)
            // L2_LOG_ERROR(Log,
            //     "[mail] Cancellation commit rejected for item %u, mail %d, owner %u. Success reply already sent, remaining entries still processed and cancellation still reports success.",
            //     item->ItemID(), postId, m_ownerId)
            L2_LOG_ERROR(Log,
                "[mail] Cancellation commit rejected for item %u, mail %d, owner %u. A success reply was already attempted; delivery is not confirmed. Remaining entries are still processed and cancellation still reports success.",
                item->ItemID(), postId, m_ownerId);
        }
        L2_RELEASE(item, ObjectRefType::FunctionLocal);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
    }
    transactionItems.clear();
    return true;
}

bool CWareHouse::TradeMoney(
    CSocket* socket [[maybe_unused]],
    CWareHouse* recipientWarehouse,
    std::int64_t amount,
    int& recipientAdenaItemId,
    std::int64_t& recipientAdenaAmount,
    int& deletedPayerAdenaItemId,
    int& payerAdenaRetained
) {
    // Current Cached.exe 0x00601678. The socket is kept by the original
    // interface for the surrounding post owner. This method itself performs
    // only the two-warehouse transaction.
    L2_GUARD_FUNCTION();
    // if (!recipientWarehouse)
        // return false
    if (!recipientWarehouse) {
        // Log.Add(LOG_ERROR,
        //     "[mail payment] Cannot transfer %lld adena from owner %u: recipient warehouse is unavailable. Payment not started.",
        //     static_cast<long long>(amount), m_ownerId)
        L2_LOG_ERROR(Log,
            "[mail payment] Cannot transfer %lld adena from owner %u: recipient warehouse is unavailable; payment not started.",
            static_cast<long long>(amount), m_ownerId);
        return false;
    }

    CItemTransactionOrderMap transactionItems;
    int transactionOrder = 0;
    auto stageItem = [&](CItemSP item, TransactType rollbackMode) -> bool {
        if (!item)
            return false;
        item->SetTransation();
        CItemSP rollbackItem = item->Copy();
        if (!rollbackItem)
            return false;
        rollbackItem->SetTrasactMode(rollbackMode);
        L2_ADD_REF(item, ObjectRefType::FunctionLocal);
        L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
        transactionItems.emplace(++transactionOrder, CItemKey(rollbackItem.get(), item.get()));
        return true;
    };
    auto rollback = [&]() {
        CTransaction::RollbackTransactOrderMap(transactionItems);
        transactionItems.clear();
        return false;
    };
    auto commit = [&]() {
        for (const auto& entry : transactionItems) {
            CItem* rollbackItem = entry.second.rollbackItem;
            CItem* item = entry.second.item;
            // item->CommitTransation(rollbackItem, true)
            if (!item->CommitTransation(rollbackItem, true)) {
                // Log.Add(LOG_ERROR,
                //     "[mail payment] Commit rejected for item %u in payment of %lld adena from payer %u to recipient %u. Remaining entries still processed and payment still reports success.",
                //     item->ItemID(), static_cast<long long>(amount), m_ownerId, recipientWarehouse->m_ownerId)
                L2_LOG_ERROR(Log,
                    "[mail payment] Commit rejected for item %u in payment of %lld adena from payer %u to recipient %u; remaining entries still processed and payment still reports success.",
                    item->ItemID(), static_cast<long long>(amount), m_ownerId, recipientWarehouse->m_ownerId);
            }
            L2_RELEASE(item, ObjectRefType::FunctionLocal);
            L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
        }
        transactionItems.clear();
        return true;
    };

    CItemSP payerAdena = GetItemByType(57, 0);
    CItemSP recipientAdena = recipientWarehouse->GetItemByType(57, 0);
    if (!payerAdena || payerAdena->Amount() < amount) {
        // Log.Add(LOG_WARN,
        //     "[mail payment] Payment rejected for payer %u to recipient %u: %s (available %lld, requested %lld). Attempting rollback of payment changes.",
        //     m_ownerId, recipientWarehouse->m_ownerId, payerAdena ? "insufficient adena" : "no adena stack", static_cast<long long>(payerAdena ? payerAdena->Amount() : 0), static_cast<long long>(amount))
        L2_LOG_WARN(Log,
            "[mail payment] Payment rejected for payer %u to recipient %u: %s (available %lld, requested %lld); attempting rollback of payment changes.",
            m_ownerId, recipientWarehouse->m_ownerId, payerAdena ? "insufficient adena" : "no adena stack", static_cast<long long>(payerAdena ? payerAdena->Amount() : 0), static_cast<long long>(amount));
        return rollback();
    }
    if (recipientAdena && runtime::AddInt64Wrapping(recipientAdena->Amount(), amount) > 999999999999LL) {
        // Log.Add(LOG_WARN,
        //     "[mail payment] Payment rejected for payer %u to recipient %u: adding %lld adena to item %u with quantity %lld exceeds the recipient limit of 999999999999. Attempting rollback of payment changes.",
        //     m_ownerId, recipientWarehouse->m_ownerId, static_cast<long long>(amount), recipientAdena->ItemID(), static_cast<long long>(recipientAdena->Amount()))
        L2_LOG_WARN(Log,
            "[mail payment] Payment rejected for payer %u to recipient %u: adding %lld adena to item %u with quantity %lld exceeds the recipient limit of 999999999999; attempting rollback of payment changes.",
            m_ownerId, recipientWarehouse->m_ownerId, static_cast<long long>(amount), recipientAdena->ItemID(), static_cast<long long>(recipientAdena->Amount()));
        return rollback();
    }

    if (!stageItem(payerAdena, TransactType_RollbackChanges)) {
        return rollback();
    }

    if (!recipientAdena) {
        const ItemOptionKey emptyOptionKey{};
        const BaseAttribute emptyBaseAttribute{};
        recipientAdena = recipientWarehouse->MakeNewItem(
            recipientWarehouse->m_ownerId, 57, 0, 0, 0, 0, 0, 0, recipientWarehouse->GetWarehouseNum(), emptyOptionKey, 0,
            emptyBaseAttribute, 0, 0
        );
        if (!recipientAdena || !stageItem(recipientAdena, TransactType_RollbackCreation)) {
            // Log.Add(LOG_ERROR,
            //     "[mail payment] Cannot prepare the recipient adena stack for payer %u, recipient %u, amount %lld: %s. Attempting rollback of payment changes.",
            //     m_ownerId, recipientWarehouse->m_ownerId, static_cast<long long>(amount), recipientAdena ? "transaction preparation failed" : "item creation failed")
            L2_LOG_ERROR(Log,
                "[mail payment] Cannot prepare the recipient adena stack for payer %u, recipient %u, amount %lld: %s; attempting rollback of payment changes.",
                m_ownerId, recipientWarehouse->m_ownerId, static_cast<long long>(amount), recipientAdena ? "transaction preparation failed" : "item creation failed");
            return rollback();
        }
        recipientAdena->SetChangeScope(ItemChangeScope_Added);
    } else {
        if (!stageItem(recipientAdena, TransactType_RollbackChanges)) {
            return rollback();
        }
        recipientAdena->SetChangeScope(ItemChangeScope_Increased);
    }

    recipientAdena->SetAmount(runtime::AddInt64Wrapping(recipientAdena->Amount(), amount));
    payerAdena->SetAmount(runtime::SubtractInt64Wrapping(payerAdena->Amount(), amount));
    if (payerAdena->Amount() == 0) {
        deletedPayerAdenaItemId = static_cast<int>(payerAdena->ItemID());
        payerAdena->TransactDelete();
        payerAdena->SetChangeScope(ItemChangeScope_Deleted);
        payerAdenaRetained = 0;
    } else {
        payerAdena->SetChangeScope(ItemChangeScope_Decreased);
    }

    recipientAdenaAmount = recipientAdena->Amount();
    recipientAdenaItemId = static_cast<int>(recipientAdena->ItemID());
    return commit();
}

bool CWareHouse::ReceivePost(
    CSocket* socket,
    CWareHouse* destinationWarehouse,
    int itemCount,
    const int itemIds[],
    const std::int64_t amounts[],
    int consumeTypes[],
    int postId,
    int requestContext,
    int senderId,
    int receiverId,
    int recipientAdenaItemId,
    std::int64_t recipientAdenaAmount,
    int deletedPayerAdenaItemId,
    int payerAdenaRetained,
    int replyContext,
    std::int64_t& transferredAdena,
    std::int64_t requiredMoney
) {
    // Current Cached.exe 0x006022c4. The five values after receiverId are
    // supplied by RequestReceivePost's preceding TradeMoney call. Preserving
    // that x64 callsite order is required by ReplyReceivePost in L2Server.
    L2_GUARD_FUNCTION();
    transferredAdena = 0;
    if (!socket || !destinationWarehouse || itemCount < 0 || (itemCount > 0 && (!itemIds || !amounts || !consumeTypes))) {
        // Log.Add(LOG_ERROR,
        //     "[mail] Cannot receive mail %d (sender %d, receiver %d, attachment count %d): %s. Any prior payment is not reversed by this operation.",
        //     postId, senderId, receiverId, itemCount, !socket ? "reply socket is unavailable" : (!destinationWarehouse ? "destination warehouse is unavailable" : (itemCount < 0 ? "negative attachment count" : "required item arrays are missing")))
        L2_LOG_ERROR(Log,
            "[mail] Cannot receive mail %d (sender %d, receiver %d, attachment count %d): %s; any prior payment is not reversed by this operation.",
            postId, senderId, receiverId, itemCount, !socket ? "reply socket is unavailable" : (!destinationWarehouse ? "destination warehouse is unavailable" : (itemCount < 0 ? "negative attachment count" : "required item arrays are missing")));
        return false;
    }

    CItemTransactionOrderMap transactionItems;
    int transactionOrder = 0;
    CPacketHelperEx<20480> responseItems;
    int responseItemCount = 0;
    bool containsAdenaAttachment = false;
    CItemSP destinationAdena = destinationWarehouse->GetItemByType(57, 0);

    auto stageItem = [&](CItemSP item, TransactType rollbackMode) -> bool {
        if (!item)
            return false;
        item->SetTransation();
        CItemSP rollbackItem = item->Copy();
        if (!rollbackItem)
            return false;
        rollbackItem->SetTrasactMode(rollbackMode);
        L2_ADD_REF(item, ObjectRefType::FunctionLocal);
        L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
        transactionItems.emplace(++transactionOrder, CItemKey(rollbackItem.get(), item.get()));
        return true;
    };
    auto rollback = [&]() {
        CTransaction::RollbackTransactOrderMap(transactionItems);
        transactionItems.clear();
        return false;
    };
    auto assembleItem = [&](CItem& item, std::int64_t changedAmount) {
        // CItemAssemblerSendToL2Server assembler(item, 0x7f)
        CItemAssemblerSendToL2Server assembler(item, ItemAssembleType::ALL);
        assembler.AssembleItemInfoImpl(responseItems);
        responseItems.Assemble("Q", static_cast<long long>(changedAmount));
    };

    for (int index = 0; index < itemCount; ++index) {
        CItemSP sourceItem = GetItem(itemIds[index]);
        // if (!sourceItem)
            // return rollback()
        if (!sourceItem) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Cannot receive mail %d (sender %d, receiver %d): attachment %d of %d, item %d, was not found. Attempting rollback of attachment changes. Any prior payment is separate and is not reversed here.",
            //     postId, senderId, receiverId, index + 1, itemCount, itemIds[index])
            L2_LOG_ERROR(Log,
                "[mail] Cannot receive mail %d (sender %d, receiver %d): attachment %d of %d, item %d, was not found; attempting rollback of attachment changes. Any prior payment is separate and is not reversed here.",
                postId, senderId, receiverId, index + 1, itemCount, itemIds[index]);
            return rollback();
        }
        if (sourceItem->ItemType() == 57) {
            containsAdenaAttachment = true;
        }
        if (!stageItem(sourceItem, TransactType_RollbackChanges)) {
            return rollback();
        }

        const std::int64_t sourceAmount = sourceItem->Amount();
        const std::int64_t requestedAmount = amounts[index];
        // if (sourceAmount < requestedAmount)
            // return rollback()
        if (sourceAmount < requestedAmount) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Cannot receive mail %d (sender %d, receiver %d): attachment %d of %d, item %d, has quantity %lld, requested %lld. Attempting rollback of attachment changes. Any prior payment is not reversed here.",
            //     postId, senderId, receiverId, index + 1, itemCount, itemIds[index], static_cast<long long>(sourceAmount), static_cast<long long>(requestedAmount))
            L2_LOG_ERROR(Log,
                "[mail] Cannot receive mail %d (sender %d, receiver %d): attachment %d of %d, item %d, has quantity %lld, requested %lld; attempting rollback of attachment changes. Any prior payment is not reversed here.",
                postId, senderId, receiverId, index + 1, itemCount, itemIds[index], static_cast<long long>(sourceAmount), static_cast<long long>(requestedAmount));
            return rollback();
        }

        ++responseItemCount;
        if (consumeTypes[index] == ICT_STACKABLE || consumeTypes[index] == ICT_ASSET) {
            CItemSP destinationItem = destinationWarehouse->GetItemByType(static_cast<int>(sourceItem->ItemType()), 0);
            if (!destinationItem) {
                if (sourceAmount == requestedAmount) {
                    sourceItem->setOwnerID(destinationWarehouse->m_ownerId);
                    sourceItem->SetWarehouse(destinationWarehouse->GetWarehouseNum());
                    sourceItem->m_ident = 0;
                    assembleItem(*sourceItem.get(), requestedAmount);
                    sourceItem->SetChangeScope(ItemChangeScope_Decreased);
                } else {
                    sourceItem->SetAmount(runtime::SubtractInt64Wrapping(sourceAmount, requestedAmount));
                    destinationItem = destinationWarehouse->MakeNewItem(
                        destinationWarehouse->m_ownerId, sourceItem->ItemType(), requestedAmount, sourceItem->Enchant(),
                        sourceItem->Eroded(), sourceItem->Bless(), 0, sourceItem->Wished(),
                        destinationWarehouse->GetWarehouseNum(), sourceItem->OptionKey(), sourceItem->m_intensiveItemType,
                        sourceItem->Attribute(), sourceItem->HLocked(), sourceItem->UseCount()
                    );
                    if (!destinationItem || !stageItem(destinationItem, TransactType_RollbackCreation)) {
                        // Log.Add(LOG_ERROR,
                        //     "[mail] Cannot receive mail %d (sender %d, receiver %d): %s for attachment %d of %d, item %d, quantity %lld. Source quantity is already %lld in memory. Attempting attachment rollback. Any prior payment is not reversed here.",
                        //     postId, senderId, receiverId, destinationItem ? "destination transaction preparation failed" : "destination item creation failed", index + 1, itemCount, itemIds[index], static_cast<long long>(requestedAmount), static_cast<long long>(sourceItem->Amount()))
                        L2_LOG_ERROR(Log,
                            "[mail] Cannot receive mail %d (sender %d, receiver %d): %s for attachment %d of %d, item %d, quantity %lld; source quantity is already %lld in memory. Attempting attachment rollback; any prior payment is not reversed here.",
                            postId, senderId, receiverId, destinationItem ? "destination transaction preparation failed" : "destination item creation failed", index + 1, itemCount, itemIds[index], static_cast<long long>(requestedAmount), static_cast<long long>(sourceItem->Amount()));
                        return rollback();
                    }
                    assembleItem(*destinationItem.get(), requestedAmount);
                    sourceItem->SetChangeScope(ItemChangeScope_Added);
                    destinationItem->SetChangeScope(ItemChangeScope_Fee);
                }
            } else {
                if (!stageItem(destinationItem, TransactType_RollbackChanges)) {
                    return rollback();
                }
                sourceItem->SetAmount(runtime::SubtractInt64Wrapping(sourceAmount, requestedAmount));
                destinationItem->SetAmount(runtime::AddInt64Wrapping(destinationItem->Amount(), requestedAmount));
                destinationItem->m_ident = 0;
                if (sourceAmount == requestedAmount) {
                    sourceItem->TransactDelete();
                }
                assembleItem(*destinationItem.get(), requestedAmount);
                sourceItem->SetChangeScope(ItemChangeScope_Added);
                destinationItem->SetChangeScope(ItemChangeScope_Increased);
            }
        } else {
            sourceItem->setOwnerID(destinationWarehouse->m_ownerId);
            sourceItem->SetWarehouse(destinationWarehouse->GetWarehouseNum());
            sourceItem->m_ident = 0;
            assembleItem(*sourceItem.get(), requestedAmount);
            sourceItem->SetChangeScope(ItemChangeScope_Deleted);
        }
    }

    if (destinationAdena) {
        transferredAdena = destinationAdena->Amount();
        if (!containsAdenaAttachment) {
            ++responseItemCount;
            assembleItem(*destinationAdena.get(), 0);
        }
    }

    socket->Send(
        "chddddddQddbdddQ", 0xc6, 0x73, 1, postId, requestContext, senderId,
        receiverId, replyContext, static_cast<long long>(requiredMoney), containsAdenaAttachment ? 1 : 0, responseItemCount,
        responseItems.GetLength(), responseItems.GetPacket(), payerAdenaRetained, deletedPayerAdenaItemId,
        recipientAdenaItemId, static_cast<long long>(recipientAdenaAmount)
    );

    for (const auto& entry : transactionItems) {
        CItem* rollbackItem = entry.second.rollbackItem;
        CItem* item = entry.second.item;
        // item->CommitTransation(rollbackItem, true)
        if (!item->CommitTransation(rollbackItem, true)) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Receipt commit rejected for item %u, mail %d (sender %d, receiver %d). Success reply already sent, remaining entries still processed and receipt still reports success. Any prior payment is separate.",
            //     item->ItemID(), postId, senderId, receiverId)
            // L2_LOG_ERROR(Log,
            //     "[mail] Receipt commit rejected for item %u, mail %d (sender %d, receiver %d). Success reply already sent, remaining entries still processed and receipt still reports success. Any prior payment is separate.",
            //     item->ItemID(), postId, senderId, receiverId)
            L2_LOG_ERROR(Log,
                "[mail] Receipt commit rejected for item %u, mail %d (sender %d, receiver %d). A success reply was already attempted; delivery is not confirmed. Remaining entries are still processed and receipt still reports success. Any prior payment is separate.",
                item->ItemID(), postId, senderId, receiverId);
        }
        L2_RELEASE(item, ObjectRefType::FunctionLocal);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
    }
    transactionItems.clear();
    return true;
}

bool CWareHouse::DeletePost(
    CSocket* socket [[maybe_unused]],
    CWareHouse* destinationWarehouse,
    int itemCount,
    const int itemIds[],
    const std::int64_t amounts[],
    int auditContext [[maybe_unused]]
) {
    // Current Cached.exe 0x006042c4. Despite its name this owner does not
    // destroy attachments: RequestDeletePost uses it to move surviving post
    // contents from warehouse 4 to warehouse 1.
    L2_GUARD_FUNCTION();
    if (!destinationWarehouse || itemCount < 0 || (itemCount > 0 && (!itemIds || !amounts))) {
        // Log.Add(LOG_ERROR,
        //     "[mail] Cannot return attachments from owner %u, warehouse %d (count %d): %s. No attachment changes attempted.",
        //     m_ownerId, static_cast<int>(m_warehouseType), itemCount, !destinationWarehouse ? "destination warehouse is unavailable" : (itemCount < 0 ? "negative attachment count" : "required item arrays are missing"))
        L2_LOG_ERROR(Log,
            "[mail] Cannot return attachments from owner %u, warehouse %d (count %d): %s; no attachment changes attempted.",
            m_ownerId, static_cast<int>(m_warehouseType), itemCount, !destinationWarehouse ? "destination warehouse is unavailable" : (itemCount < 0 ? "negative attachment count" : "required item arrays are missing"));
        return false;
    }

    CItemTransactionOrderMap transactionItems;
    int transactionOrder = 0;
    auto stageItem = [&](CItemSP item, TransactType rollbackMode) -> bool {
        if (!item)
            return false;
        item->SetTransation();
        CItemSP rollbackItem = item->Copy();
        if (!rollbackItem)
            return false;
        rollbackItem->SetTrasactMode(rollbackMode);
        L2_ADD_REF(item, ObjectRefType::FunctionLocal);
        L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
        transactionItems.emplace(++transactionOrder, CItemKey(rollbackItem.get(), item.get()));
        return true;
    };
    auto rollback = [&]() {
        CTransaction::RollbackTransactOrderMap(transactionItems);
        transactionItems.clear();
        return false;
    };

    for (int index = 0; index < itemCount; ++index) {
        CItemSP sourceItem = GetItem(itemIds[index]);
        if (!sourceItem || !stageItem(sourceItem, TransactType_RollbackChanges)) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Cannot return attachment %d of %d from owner %u to owner %u: item %d %s. Attempting rollback of attachment changes.",
            //     index + 1, itemCount, m_ownerId, destinationWarehouse->m_ownerId, itemIds[index], sourceItem ? "could not be prepared for the transaction" : "was not found")
            L2_LOG_ERROR(Log,
                "[mail] Cannot return attachment %d of %d from owner %u to owner %u: item %d %s; attempting rollback of attachment changes.",
                index + 1, itemCount, m_ownerId, destinationWarehouse->m_ownerId, itemIds[index], sourceItem ? "could not be prepared for the transaction" : "was not found");
            return rollback();
        }

        int deleteConsumeType = 0;
        switch (g_readFile.GetConsumeType(static_cast<int>(sourceItem->ItemType()))) {
        case 2:
            deleteConsumeType = 3;
            break;
        case 3:
            deleteConsumeType = 1;
            break;
        case 4:
            deleteConsumeType = 2;
            break;
        default:
            break;
        }

        const std::int64_t sourceAmount = sourceItem->Amount();
        const std::int64_t requestedAmount = amounts[index];
        // if (sourceAmount < requestedAmount)
            // return rollback()
        if (sourceAmount < requestedAmount) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Cannot return attachment %d of %d from owner %u to owner %u: item %d has quantity %lld, requested %lld. Attempting rollback of attachment changes.",
            //     index + 1, itemCount, m_ownerId, destinationWarehouse->m_ownerId, itemIds[index], static_cast<long long>(sourceAmount), static_cast<long long>(requestedAmount))
            L2_LOG_ERROR(Log,
                "[mail] Cannot return attachment %d of %d from owner %u to owner %u: item %d has quantity %lld, requested %lld; attempting rollback of attachment changes.",
                index + 1, itemCount, m_ownerId, destinationWarehouse->m_ownerId, itemIds[index], static_cast<long long>(sourceAmount), static_cast<long long>(requestedAmount));
            return rollback();
        }

        if (deleteConsumeType == 2 || deleteConsumeType == 3) {
            CItemSP destinationItem = destinationWarehouse->GetItemByType(static_cast<int>(sourceItem->ItemType()), 0);
            if (!destinationItem) {
                if (sourceAmount == requestedAmount) {
                    sourceItem->SetWarehouse(destinationWarehouse->GetWarehouseNum());
                    sourceItem->m_ident = 0;
                    sourceItem->SetChangeScope(ItemChangeScope_Decreased);
                } else {
                    sourceItem->SetAmount(runtime::SubtractInt64Wrapping(sourceAmount, requestedAmount));
                    destinationItem = destinationWarehouse->MakeNewItem(
                        destinationWarehouse->m_ownerId, sourceItem->ItemType(), requestedAmount, sourceItem->Enchant(),
                        sourceItem->Eroded(), sourceItem->Bless(), 0, sourceItem->Wished(),
                        destinationWarehouse->GetWarehouseNum(), sourceItem->OptionKey(), sourceItem->m_intensiveItemType,
                        sourceItem->Attribute(), sourceItem->HLocked(), sourceItem->UseCount()
                    );
                    if (!destinationItem || !stageItem(destinationItem, TransactType_RollbackCreation)) {
                        // Log.Add(LOG_ERROR,
                        //     "[mail] Cannot return attachment %d of %d from owner %u to owner %u: %s for item %d, quantity %lld. Source quantity is already %lld in memory. Attempting rollback of attachment changes.",
                        //     index + 1, itemCount, m_ownerId, destinationWarehouse->m_ownerId, destinationItem ? "destination transaction preparation failed" : "destination item creation failed", itemIds[index], static_cast<long long>(requestedAmount), static_cast<long long>(sourceItem->Amount()))
                        L2_LOG_ERROR(Log,
                            "[mail] Cannot return attachment %d of %d from owner %u to owner %u: %s for item %d, quantity %lld; source quantity is already %lld in memory. Attempting rollback of attachment changes.",
                            index + 1, itemCount, m_ownerId, destinationWarehouse->m_ownerId, destinationItem ? "destination transaction preparation failed" : "destination item creation failed", itemIds[index], static_cast<long long>(requestedAmount), static_cast<long long>(sourceItem->Amount()));
                        return rollback();
                    }
                    sourceItem->SetChangeScope(ItemChangeScope_Added);
                    destinationItem->SetChangeScope(ItemChangeScope_Fee);
                }
            } else {
                if (!stageItem(destinationItem, TransactType_RollbackChanges)) {
                    return rollback();
                }
                sourceItem->SetAmount(runtime::SubtractInt64Wrapping(sourceAmount, requestedAmount));
                destinationItem->SetAmount(runtime::AddInt64Wrapping(destinationItem->Amount(), requestedAmount));
                destinationItem->m_ident = 0;
                if (sourceAmount == requestedAmount) {
                    sourceItem->TransactDelete();
                }
                sourceItem->SetChangeScope(ItemChangeScope_Added);
                destinationItem->SetChangeScope(ItemChangeScope_Increased);
            }
        } else {
            sourceItem->SetWarehouse(destinationWarehouse->GetWarehouseNum());
            sourceItem->m_ident = 0;
            sourceItem->SetChangeScope(ItemChangeScope_Deleted);
        }
    }

    for (const auto& entry : transactionItems) {
        CItem* rollbackItem = entry.second.rollbackItem;
        CItem* item = entry.second.item;
        // item->CommitTransation(rollbackItem, true)
        if (!item->CommitTransation(rollbackItem, true)) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Attachment-return commit rejected for item %u, source owner %u, destination owner %u. Remaining entries still processed and attachment return still reports success.",
            //     item->ItemID(), m_ownerId, destinationWarehouse->m_ownerId)
            L2_LOG_ERROR(Log,
                "[mail] Attachment-return commit rejected for item %u, source owner %u, destination owner %u; remaining entries still processed and attachment return still reports success.",
                item->ItemID(), m_ownerId, destinationWarehouse->m_ownerId);
        }
        L2_RELEASE(item, ObjectRefType::FunctionLocal);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
    }
    transactionItems.clear();
    return true;
}

bool CWareHouse::DeleteAllItem(bool hardDelete) {
    L2_GUARD_FUNCTION();
    CDeleteAllItemInWareHouse callback(hardDelete);
    m_items.FOR_ALL_DO_AND_CLEAR(&callback, "WareHouse.cpp", 0x905);
    return true;
}

bool CWareHouse::LoadItems() {
    L2_GUARD_FUNCTION();

    std::uint32_t itemId = 0;
    std::uint32_t itemType = 0;
    long long amount = 0;
    int enchant = 0;
    int eroded = 0;
    int bless = 0;
    int ident = 0;
    int wished = 0;
    ItemOptionKey optionKey;
    int intensiveItemType = 0;
    int inventorySlotIndex = 0;
    int appearanceType = 0;
    BaseAttribute baseAttribute;
    short hLocked = 0;
    short useCount = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&itemId);
    connection.Bind(&itemType);
    connection.Bind(&amount);
    connection.Bind(&enchant);
    connection.Bind(&eroded);
    connection.Bind(&bless);
    connection.Bind(&ident);
    connection.Bind(&wished);
    connection.Bind(&optionKey.variationOption1);
    connection.Bind(&optionKey.variationOption2);
    connection.Bind(&intensiveItemType);
    connection.Bind(&inventorySlotIndex);
    connection.Bind(&appearanceType);
    connection.Bind(&baseAttribute.m_nAttackType);
    connection.Bind(&baseAttribute.m_nAttackValue);
    for (short& defendValue : baseAttribute.m_nDefendValue) {
        connection.Bind(&defendValue);
    }
    connection.Bind(&hLocked);
    connection.Bind(&useCount);

    int ownerId = static_cast<int>(m_ownerId);
    int warehouseType = static_cast<int>(m_warehouseType);
    constexpr const wchar_t* statement = L"{Call lin_LoadItems (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&ownerId);
    connection.BindParameter(&warehouseType);

    if (!connection.ExecutePrepared(statement)) {
        // Shipping 0x005d8444 leaves m_itemsLoaded clear but returns true.
        // Log.Add(LOG_ERROR,
        //     "[warehouse] Item-load query failed for owner %u, warehouse %d, database %d. Cached contents and loaded flag unchanged despite the success return value.",
        //     m_ownerId, warehouseType, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[warehouse] Item-load query failed for owner %u, warehouse %d, database %d; cached contents and loaded flag unchanged despite the success return value.",
            m_ownerId, warehouseType, runtime::process::CurrentDatabaseId());
        return true;
    }

    int loadedItemNumber = 0;
    while (connection.Fetch()) {
        if (amount < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     "item amount is invalid, owner[%u] id[%u] "
            //     "nItemType[%u] amount[%lld] warehouse[%d]",
            //     m_ownerId, itemId, itemType, amount, warehouseType
            // )
            // Log.Add(LOG_ERROR,
            //     "[warehouse] Skipping item %u (class %u) for owner %u, warehouse %d: database quantity %lld is not positive. Row left unchanged.",
            //     itemId, itemType, m_ownerId, warehouseType, amount)
            L2_LOG_ERROR(Log,
                "[warehouse] Skipping item %u (class %u) for owner %u, warehouse %d: database quantity %lld is not positive; row left unchanged.",
                itemId, itemType, m_ownerId, warehouseType, amount);
        } else {
            CItem* item = nullptr;
            try {
                item = new CItem(
                    itemId, m_ownerId, itemType, amount, enchant, eroded, bless, ident, wished, warehouseType, optionKey,
                    intensiveItemType, baseAttribute, inventorySlotIndex, appearanceType, hLocked, useCount
                );
            } catch (const std::bad_alloc&) {
                item = nullptr;
            }

            if (!item) {
                // Log.Add(
                //     LOG_ERROR,
                //     "item create failed at LoadItems char_id[%u] "
                //     "ware_num[%d] item_id[%u]",
                //     m_ownerId, warehouseType, itemId
                // )
                // Log.Add(LOG_ERROR,
                //     "[warehouse] Cannot allocate item %u (class %u) while loading owner %u, warehouse %d. Stopping the load and retaining items already in the cache.",
                //     itemId, itemType, m_ownerId, warehouseType)
                L2_LOG_ERROR(Log,
                    "[warehouse] Cannot allocate item %u (class %u) while loading owner %u, warehouse %d; stopping the load and retaining items already in the cache.",
                    itemId, itemType, m_ownerId, warehouseType);
                return false;
            }

            item->LoadEnsoulInfo();
            PushItem(item);
            L2_RELEASE(item, ObjectRefType::CreateDelete);
        }

        if (m_warehouseType != WareHouseType_Manor && loadedItemNumber > 0x19a) {
            // Log.Add(
            //     LOG_ERROR,
            //     "Item load overflow. number[%d], owner [db id(%u)], "
            //     "warehouse num[%d]",
            //     loadedItemNumber, m_ownerId, warehouseType
            // )
            // Log.Add(LOG_ERROR,
            //     "[warehouse] Load stopped at row %d for owner %u, warehouse %d: non-manor row limit reached. The current cache will still be marked loaded.",
            //     loadedItemNumber + 1, m_ownerId, warehouseType)
            L2_LOG_ERROR(Log,
                "[warehouse] Load stopped at row %d for owner %u, warehouse %d: non-manor row limit reached; the current cache will still be marked loaded.",
                loadedItemNumber + 1, m_ownerId, warehouseType);
            break;
        }
        ++loadedItemNumber;
    }

    m_itemsLoaded = true;
    return true;
}

void CWareHouse::SendDepositItemsList(
    CSocket* socket,
    int userSocketId,
    int contextId,
    int requestedIdent,
    int memoryId,
    int warehouseClass,
    int itemListGetType
) {
    L2_GUARD_FUNCTION();

    CPackageItemPacketManager packetManager;
    CPacketHelperEx<20480> consumeItemTypes;
    SendDepositItemsListPredicate predicate(packetManager, consumeItemTypes, requestedIdent, itemListGetType);
    m_items.FOR_ALL(&predicate, L"WareHouse.cpp", 0x28b);

    if (itemListGetType == 1) {
        int totalItemCount = 0;
        for (const PACKAGE_ITEM_PACKETHELPER& package : packetManager.m_vPackageItemPacketHelperList) {
            totalItemCount += package.nItemCount;
        }
        socket->Send(
            "cddddddddddb", 9, userSocketId, static_cast<int>(m_warehouseType), warehouseClass, 1, 0, contextId, memoryId,
            totalItemCount, 0, consumeItemTypes.GetAssembledCount(), consumeItemTypes.GetLength(),
            consumeItemTypes.GetPacket()
        );
        return;
    }

    CPacketHelperEx<20480> header;
    header.Assemble(
        "ddddddd", userSocketId, static_cast<int>(m_warehouseType), warehouseClass, itemListGetType, 0, contextId, memoryId
    );
    packetManager.SendPacket_PackageItemData(socket, 9, header);
}

void CWareHouse::SendDepositItemsListEx(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x005f5384. The request and reply field order are
    // fixed by paired L2Server CDB::RequestLoadWarehouseEx
    // 0x0064b818 and ReplyWarehouseItemsEx 0x006c7e78.
    L2_GUARD_FUNCTION();

    int warehouseClassType = 0;
    int warehouseId = 0;
    int characterId = 0;
    int userServerId = 0;
    int warehouseMemoryId = 0;
    int itemListGetType = 0;
    ::Disassemble(
        packet, "dddddd", &warehouseClassType, &warehouseId, &characterId, &userServerId, &warehouseMemoryId,
        &itemListGetType
    );

    CPackageItemPacketManager packetManager;
    CPacketHelperEx<20480> consumeItemTypes;
    SendDepositItemsListExPredicate predicate(packetManager, consumeItemTypes, itemListGetType);
    m_items.FOR_ALL(&predicate, L"WareHouse.cpp", 0x389b);

    if (itemListGetType == 1) {
        int totalItemCount = 0;
        for (const PACKAGE_ITEM_PACKETHELPER& package : packetManager.m_vPackageItemPacketHelperList) {
            totalItemCount += package.nItemCount;
        }
        socket->Send(
            "cddddddddb", 0x7d, warehouseClassType, warehouseId, userServerId, warehouseMemoryId, 1, totalItemCount, 0,
            consumeItemTypes.GetAssembledCount(), consumeItemTypes.GetLength(), consumeItemTypes.GetPacket()
        );
        return;
    }

    CPacketHelperEx<20480> header;
    header.Assemble("ddddd", warehouseClassType, warehouseId, userServerId, warehouseMemoryId, itemListGetType);
    packetManager.SendPacket_PackageItemData(socket, 0x7d, header);
}

bool CWareHouse::WithdrawItemsFromBankEx(CSocket* socket, const std::uint8_t* packet, CWareHouse* sourceWarehouse) {
    // Current Cached.exe 0x0061d53c. This later-revision path consumes the
    // complete CItemDisassemblerWareHouse record. The short six-dword C1
    // descriptor is donor history only.
    L2_GUARD_FUNCTION();

    int warehouseClassType = 0;
    int warehouseId = 0;
    int characterId = 0;
    int userServerId = 0;
    int warehouseMemoryId = 0;
    int requestContext = 0;
    int itemCount = 0;
    const std::uint8_t* cursor = ::Disassemble(
        packet, "ddddddd", &warehouseClassType, &warehouseId, &characterId, &userServerId, &warehouseMemoryId,
        &requestContext, &itemCount
    );

    if (sourceWarehouse == nullptr || sourceWarehouse == this || itemCount < 1 ||
        itemCount > warehouse::kMaxWithdrawalItems) {
        L2_LOG_ERROR(Log,
            "[warehouse] Extended withdrawal rejected for owner %u: invalid source warehouse or item count %d.",
            m_ownerId, itemCount);
        return false;
    }

    // The original server accepted nonpositive amounts and trusted the packet type.
    // Check the whole request before item snapshots, SQL writes or audit records.
    // The caller holds both warehouse write locks through validation and commit.
    std::set<std::uint32_t> sourceIds;
    std::map<std::uint32_t, std::int64_t> destinationAmounts;
    const std::uint8_t* validationCursor = cursor;
    for (int index = 0; index < itemCount; ++index) {
        CItemDisassemblerWareHouse incomingItem;
        validationCursor = incomingItem.DisassembleItemInfoImpl(validationCursor);
        CItemSP sourceItem = sourceWarehouse->GetItem(static_cast<int>(incomingItem.m_nItemID));
        if (!sourceItem || sourceItem->Deleted() || !sourceIds.insert(incomingItem.m_nItemID).second) {
            L2_LOG_ERROR(Log,
                "[warehouse] Extended withdrawal rejected for owner %u: source item %u is missing, deleted or repeated.",
                m_ownerId, incomingItem.m_nItemID);
            return false;
        }

        const ItemConsumeType consumeType = g_readFile.GetItemConsumeType(static_cast<int>(sourceItem->ItemType()));
        const bool stackable = consumeType == ICT_STACKABLE || consumeType == ICT_ASSET;
        const std::int64_t sourceAmount = sourceItem->Amount();
        const std::int64_t requestedAmount = incomingItem.m_nAmount;
        if (incomingItem.m_nItemType != sourceItem->ItemType() || incomingItem.m_eConsumeType != consumeType ||
            !warehouse::IsValidWithdrawalAmount(consumeType, requestedAmount) || sourceAmount < requestedAmount ||
            sourceAmount > warehouse::kMaxStoredAmount || (consumeType == ICT_NORMAL && sourceAmount != 1)) {
            L2_LOG_ERROR(Log,
                "[warehouse] Extended withdrawal rejected for owner %u: item %u has an invalid type or quantity "
                "(stored %lld, requested %lld).",
                m_ownerId, incomingItem.m_nItemID, static_cast<long long>(sourceAmount),
                static_cast<long long>(requestedAmount));
            return false;
        }

        if (stackable) {
            auto destination = destinationAmounts.find(sourceItem->ItemType());
            if (destination == destinationAmounts.end()) {
                CItemSP destinationItem = GetItemByType(static_cast<int>(sourceItem->ItemType()), 0);
                if (!destinationItem) {
                    // New stacks enter the inventory map at commit and do not merge in this request.
                    continue;
                }
                if (destinationItem->Deleted()) {
                    L2_LOG_ERROR(Log,
                        "[warehouse] Extended withdrawal rejected for owner %u: destination item %u is deleted.",
                        m_ownerId, destinationItem->ItemID());
                    return false;
                }
                destination = destinationAmounts.emplace(sourceItem->ItemType(), destinationItem->Amount()).first;
            }
            std::int64_t& destinationAmount = destination->second;
            if (destinationAmount < 0 || destinationAmount > warehouse::kMaxStoredAmount ||
                requestedAmount > warehouse::kMaxStoredAmount - destinationAmount) {
                L2_LOG_ERROR(Log,
                    "[warehouse] Extended withdrawal rejected for owner %u: the resulting stack of class %u "
                    "would exceed its quantity limit.",
                    m_ownerId, sourceItem->ItemType());
                return false;
            }
            destinationAmount += requestedAmount;
        }
    }

    CTransaction transaction;
    CPackageItemPacketManager packetManager;
    CUserSP owner = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    const WareHouseAuditActor actor = MakeWareHouseAuditActor(owner.get());

    auto packageItem = [&](CItem& item) {
        packetManager.CheckLimitPackageItemPacketDataSize();
        // CItemAssemblerSendToL2Server itemAssembler(item, 0x7f)
        CItemAssemblerSendToL2Server itemAssembler(item, ItemAssembleType::ALL);
        const int assembledLength = packetManager.PackageAssembleItem(itemAssembler);
        if (packetManager.m_nTotalPackageItemCount == 0) {
            packetManager.m_nAssembleItemLengthPerItem += assembledLength;
        }
        PACKAGE_ITEM_PACKETHELPER& currentPackage =
            packetManager
                .m_vPackageItemPacketHelperList[static_cast<std::size_t>(packetManager.m_nCurrentPacketHelperIndex)];
        ++currentPackage.nItemCount;
        ++packetManager.m_nTotalPackageItemCount;
    };

    auto rollback = [&]() {
        transaction.SetRollback();
        return false;
    };

    std::set<std::uint32_t> stagedDestinationIds;
    for (int index = 0; index < itemCount; ++index) {
        CItemDisassemblerWareHouse incomingItem;
        cursor = incomingItem.DisassembleItemInfoImpl(cursor);

        CItemSP sourceItem = sourceWarehouse->GetItem(static_cast<int>(incomingItem.m_nItemID));
        if (!sourceItem) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d]Cannot getitem. [%d][%d]-[%d][%d]"
            //     "[%lld][%d][%d][%d]",
            //     "WareHouse.cpp", 0x39bc, warehouseClassType, warehouseId, static_cast<int>(incomingItem.m_nItemID),
            //     static_cast<int>(incomingItem.m_nItemType), static_cast<long long>(incomingItem.m_nAmount),
            //     incomingItem.m_nEnchant, incomingItem.m_nEroded, static_cast<int>(incomingItem.m_eConsumeType)
            // )
            // Log.Add(LOG_ERROR,
            //     "[warehouse] Withdrawal failed for owner %u, bank class %d, bank ID %d: source item %u (requested class %u, quantity %lld) not found. Rollback requested.",
            //     m_ownerId, warehouseClassType, warehouseId, incomingItem.m_nItemID, incomingItem.m_nItemType, static_cast<long long>(incomingItem.m_nAmount))
            L2_LOG_ERROR(Log,
                "[warehouse] Withdrawal failed for owner %u, bank class %d, bank ID %d: source item %u (requested class %u, quantity %lld) not found; rollback requested.",
                m_ownerId, warehouseClassType, warehouseId, incomingItem.m_nItemID, incomingItem.m_nItemType, static_cast<long long>(incomingItem.m_nAmount));
            return rollback();
        }

        if (!transaction.AddItem(sourceItem)) {
            return rollback();
        }
        const std::int64_t sourceAmount = sourceItem->Amount();
        const std::int64_t requestedAmount = incomingItem.m_nAmount;
        if (sourceAmount < requestedAmount) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d]Amount diff. [%d][%d]-[%d][%d]"
            //     "[%lld][%d][%d][%d]",
            //     "WareHouse.cpp", 0x39c8, warehouseClassType, warehouseId, static_cast<int>(incomingItem.m_nItemID),
            //     static_cast<int>(incomingItem.m_nItemType), static_cast<long long>(requestedAmount), incomingItem.m_nEnchant,
            //     incomingItem.m_nEroded, static_cast<int>(incomingItem.m_eConsumeType)
            // )
            // Log.Add(LOG_ERROR,
            //     "[warehouse] Withdrawal failed for owner %u, bank class %d, bank ID %d: item %u has quantity %lld, requested %lld. Rollback requested.",
            //     m_ownerId, warehouseClassType, warehouseId, sourceItem->ItemID(), static_cast<long long>(sourceAmount), static_cast<long long>(requestedAmount))
            L2_LOG_ERROR(Log,
                "[warehouse] Withdrawal failed for owner %u, bank class %d, bank ID %d: item %u has quantity %lld, requested %lld; rollback requested.",
                m_ownerId, warehouseClassType, warehouseId, sourceItem->ItemID(), static_cast<long long>(sourceAmount), static_cast<long long>(requestedAmount));
            return rollback();
        }

        CItemSP resultItem;
        std::int64_t remainingAmount = 0;
        const ItemConsumeType consumeType = g_readFile.GetItemConsumeType(static_cast<int>(sourceItem->ItemType()));
        if (consumeType == ICT_STACKABLE || consumeType == ICT_ASSET) {
            CItemSP destinationItem = GetItemByType(static_cast<int>(sourceItem->ItemType()), 0);
            if (!destinationItem) {
                if (sourceAmount == requestedAmount) {
                    sourceItem->setOwnerID(m_ownerId);
                    sourceItem->SetWarehouse(GetWarehouseNum());
                    sourceItem->m_ident = 0;
                    packageItem(*sourceItem.get());
                    resultItem = sourceItem;
                    remainingAmount = 0;
                    sourceItem->SetChangeScope(ItemChangeScope_Decreased);
                } else {
                    sourceItem->SetAmount(sourceAmount - requestedAmount);
                    destinationItem = MakeNewItem(
                        m_ownerId, sourceItem->ItemType(), requestedAmount, sourceItem->Enchant(), sourceItem->Eroded(),
                        sourceItem->Bless(), 0, sourceItem->Wished(), GetWarehouseNum(), sourceItem->OptionKey(),
                        sourceItem->m_intensiveItemType, sourceItem->Attribute(), sourceItem->HLocked(),
                        sourceItem->UseCount()
                    );
                    if (!destinationItem) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     "[%s][%d]Cannot make new item. "
                        //     "[%d][%d]-[%d][%d][%lld][%d][%d][%d]",
                        //     "WareHouse.cpp", 0x3a18, warehouseClassType, warehouseId,
                        //     static_cast<int>(incomingItem.m_nItemID), static_cast<int>(incomingItem.m_nItemType),
                        //     static_cast<long long>(requestedAmount), incomingItem.m_nEnchant, incomingItem.m_nEroded,
                        //     consumeType
                        // )
                        // Log.Add(LOG_ERROR,
                        //     "[warehouse] Withdrawal failed for owner %u, bank class %d, bank ID %d: cannot create the destination stack for source item %u (class %u), quantity %lld. Rollback requested.",
                        //     m_ownerId, warehouseClassType, warehouseId, sourceItem->ItemID(), sourceItem->ItemType(), static_cast<long long>(requestedAmount))
                        L2_LOG_ERROR(Log,
                            "[warehouse] Withdrawal failed for owner %u, bank class %d, bank ID %d: cannot create the destination stack for source item %u (class %u), quantity %lld; rollback requested.",
                            m_ownerId, warehouseClassType, warehouseId, sourceItem->ItemID(), sourceItem->ItemType(), static_cast<long long>(requestedAmount));
                        return rollback();
                    }

                    transaction.AddItemNew(destinationItem);
                    packageItem(*destinationItem.get());
                    resultItem = destinationItem;
                    remainingAmount = sourceItem->Amount();
                    sourceItem->SetChangeScope(ItemChangeScope_Added);
                    destinationItem->SetChangeScope(ItemChangeScope_Increased);
                }
            } else {
                if (stagedDestinationIds.insert(destinationItem->ItemID()).second &&
                    !transaction.AddItem(destinationItem)) {
                    return rollback();
                }
                const std::int64_t destinationAmount = destinationItem->Amount();
                const std::int64_t mergedAmount = destinationAmount + requestedAmount;
                if (mergedAmount > warehouse::kMaxStoredAmount) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "[%s][%d]overflow max item amount. "
                    //     "[%d][%d]-[%d][%d][%lld][%d][%d][%d]",
                    //     "WareHouse.cpp", 0x39d9, warehouseClassType, warehouseId, static_cast<int>(incomingItem.m_nItemID),
                    //     static_cast<int>(incomingItem.m_nItemType), static_cast<long long>(requestedAmount),
                    //     incomingItem.m_nEnchant, incomingItem.m_nEroded, consumeType
                    // )
                    // Log.Add(LOG_ERROR,
                    //     "[warehouse] Withdrawal failed for owner %u, bank class %d, bank ID %d: destination item %u quantity %lld plus %lld gives %lld, above 999999999999. Rollback requested.",
                    //     m_ownerId, warehouseClassType, warehouseId, destinationItem->ItemID(), static_cast<long long>(destinationAmount), static_cast<long long>(requestedAmount), static_cast<long long>(mergedAmount))
                    L2_LOG_ERROR(Log,
                        "[warehouse] Withdrawal failed for owner %u, bank class %d, bank ID %d: destination item %u quantity %lld plus %lld gives %lld, above 999999999999; rollback requested.",
                        m_ownerId, warehouseClassType, warehouseId, destinationItem->ItemID(), static_cast<long long>(destinationAmount), static_cast<long long>(requestedAmount), static_cast<long long>(mergedAmount));
                    return rollback();
                }

                sourceItem->SetAmount(sourceAmount - requestedAmount);
                destinationItem->SetAmount(mergedAmount);
                destinationItem->m_ident = 0;
                if (sourceAmount == requestedAmount) {
                    sourceItem->TransactDelete();
                }
                packageItem(*destinationItem.get());
                resultItem = destinationItem;
                remainingAmount = sourceItem->Amount();
                sourceItem->SetChangeScope(ItemChangeScope_Added);
                destinationItem->SetChangeScope(ItemChangeScope_Increased);
            }
        } else {
            sourceItem->setOwnerID(m_ownerId);
            sourceItem->SetWarehouse(GetWarehouseNum());
            sourceItem->m_ident = 0;
            packageItem(*sourceItem.get());
            resultItem = sourceItem;
            remainingAmount = 0;
            sourceItem->SetChangeScope(ItemChangeScope_Deleted);
        }

        WriteWithdrawItemsFromBankExAudit(
            actor, *sourceItem.get(), *resultItem.get(), warehouseClassType, requestedAmount, remainingAmount
        );
    }

    CPacketHelperEx<20480> header;
    header.Assemble("dddd", warehouseClassType, warehouseId, userServerId, 0);
    packetManager.SendPacket_PackageItemData(socket, 0x7e, header);
    return true;
}

int CWareHouse::DepositItemsToBankEx(CSocket* socket, const std::uint8_t* packet, CWareHouse* destinationWarehouse) {
    // Current Cached.exe 0x0061ee90. The current L2Server sender supplies
    // six dwords followed by itemCount complete CItemAssemblerWareHouse
    // records. The final record is the adena banking fee.
    L2_GUARD_FUNCTION();

    int warehouseClassType = 0;
    int warehouseId = 0;
    int characterId = 0;
    int userServerId = 0;
    int warehouseItemLimit = 0;
    int itemCount = 0;
    const std::uint8_t* cursor = ::Disassemble(
        packet, "dddddd", &warehouseClassType, &warehouseId, &characterId, &userServerId, &warehouseItemLimit, &itemCount
    );

    CPacketHelperEx<20480> responseItems;
    CTransaction transaction;
    CUserSP owner = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    const WareHouseAuditActor actor = MakeWareHouseAuditActor(owner.get());

    int newItemCount = 0;
    auto rollback = [&]() {
        transaction.SetRollback();
        return 1;
    };

    const int depositItemCount = runtime::AddInt32Wrapping(itemCount, -1);
    for (int index = 0; index < depositItemCount; ++index) {
        CItemDisassemblerWareHouse incomingItem;
        cursor = incomingItem.DisassembleItemInfoImpl(cursor);

        CItemSP sourceItem = GetItem(static_cast<int>(incomingItem.m_nItemID));
        if (!sourceItem) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d]Cannot getitem. [%d][%d]-[%d][%d]"
            //     "[%lld][%d][%d][%d]",
            //     "WareHouse.cpp", 0x3bdc, warehouseClassType, warehouseId, static_cast<int>(incomingItem.m_nItemID),
            //     static_cast<int>(incomingItem.m_nItemType), static_cast<long long>(incomingItem.m_nAmount),
            //     incomingItem.m_nEnchant, incomingItem.m_nEroded, static_cast<int>(incomingItem.m_eConsumeType)
            // )
            // Log.Add(LOG_ERROR,
            //     "[warehouse] Deposit failed for owner %u, bank class %d, bank ID %d: source item %u (requested class %u, quantity %lld) not found. Rollback requested.",
            //     m_ownerId, warehouseClassType, warehouseId, incomingItem.m_nItemID, incomingItem.m_nItemType, static_cast<long long>(incomingItem.m_nAmount))
            L2_LOG_ERROR(Log,
                "[warehouse] Deposit failed for owner %u, bank class %d, bank ID %d: source item %u (requested class %u, quantity %lld) not found; rollback requested.",
                m_ownerId, warehouseClassType, warehouseId, incomingItem.m_nItemID, incomingItem.m_nItemType, static_cast<long long>(incomingItem.m_nAmount));
            return rollback();
        }

        transaction.AddItem(sourceItem);
        const std::int64_t sourceAmount = sourceItem->Amount();
        const std::int64_t requestedAmount = incomingItem.m_nAmount;
        if (sourceAmount < requestedAmount) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d]Amount diff. [%d][%d]-[%d][%d]"
            //     "[%lld][%d][%d][%d]",
            //     "WareHouse.cpp", 0x3be8, warehouseClassType, warehouseId, static_cast<int>(incomingItem.m_nItemID),
            //     static_cast<int>(incomingItem.m_nItemType), static_cast<long long>(requestedAmount), incomingItem.m_nEnchant,
            //     incomingItem.m_nEroded, static_cast<int>(incomingItem.m_eConsumeType)
            // )
            // Log.Add(LOG_ERROR,
            //     "[warehouse] Deposit failed for owner %u, bank class %d, bank ID %d: item %u has quantity %lld, requested %lld. Rollback requested.",
            //     m_ownerId, warehouseClassType, warehouseId, sourceItem->ItemID(), static_cast<long long>(sourceAmount), static_cast<long long>(requestedAmount))
            L2_LOG_ERROR(Log,
                "[warehouse] Deposit failed for owner %u, bank class %d, bank ID %d: item %u has quantity %lld, requested %lld; rollback requested.",
                m_ownerId, warehouseClassType, warehouseId, sourceItem->ItemID(), static_cast<long long>(sourceAmount), static_cast<long long>(requestedAmount));
            return rollback();
        }

        std::int64_t remainingAmount = 0;
        const int consumeType = static_cast<int>(incomingItem.m_eConsumeType);
        if (consumeType == 2 || consumeType == 3) {
            CItemSP destinationItem = destinationWarehouse->GetItemByType(static_cast<int>(sourceItem->ItemType()), 0);
            if (!destinationItem) {
                if (sourceAmount != requestedAmount) {
                    sourceItem->SetAmount(runtime::SubtractInt64Wrapping(sourceAmount, requestedAmount));
                    destinationItem = destinationWarehouse->MakeNewItem(
                        destinationWarehouse->m_ownerId, sourceItem->ItemType(), requestedAmount, sourceItem->Enchant(),
                        sourceItem->Eroded(), sourceItem->Bless(), sourceItem->Ident(), sourceItem->Wished(),
                        destinationWarehouse->GetWarehouseNum(), sourceItem->OptionKey(), sourceItem->m_intensiveItemType,
                        sourceItem->Attribute(), sourceItem->HLocked(), sourceItem->UseCount()
                    );
                    if (!destinationItem) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     "[%s][%d]Cannot make new item. "
                        //     "[%d][%d]-[%d][%d][%lld][%d][%d][%d]",
                        //     "WareHouse.cpp", 0x3c26, warehouseClassType, warehouseId,
                        //     static_cast<int>(incomingItem.m_nItemID), static_cast<int>(incomingItem.m_nItemType),
                        //     static_cast<long long>(requestedAmount), incomingItem.m_nEnchant, incomingItem.m_nEroded,
                        //     consumeType
                        // )
                        // Log.Add(LOG_ERROR,
                        //     "[warehouse] Deposit failed for owner %u, bank class %d, bank ID %d: cannot create the destination stack for source item %u (class %u), quantity %lld. Rollback requested.",
                        //     m_ownerId, warehouseClassType, warehouseId, sourceItem->ItemID(), sourceItem->ItemType(), static_cast<long long>(requestedAmount))
                        L2_LOG_ERROR(Log,
                            "[warehouse] Deposit failed for owner %u, bank class %d, bank ID %d: cannot create the destination stack for source item %u (class %u), quantity %lld; rollback requested.",
                            m_ownerId, warehouseClassType, warehouseId, sourceItem->ItemID(), sourceItem->ItemType(), static_cast<long long>(requestedAmount));
                        return rollback();
                    }

                    transaction.AddItemNew(destinationItem);
                    remainingAmount = sourceItem->Amount();
                    sourceItem->SetChangeScope(ItemChangeScope_Added);
                    destinationItem->SetChangeScope(ItemChangeScope_Increased);
                    newItemCount = runtime::AddInt32Wrapping(newItemCount, 1);
                } else {
                    sourceItem->setOwnerID(destinationWarehouse->m_ownerId);
                    sourceItem->SetWarehouse(destinationWarehouse->GetWarehouseNum());
                    remainingAmount = 0;
                    sourceItem->SetChangeScope(ItemChangeScope_Decreased);
                    newItemCount = runtime::AddInt32Wrapping(newItemCount, 1);
                }
            } else {
                const std::int64_t destinationAmount = destinationItem->Amount();
                const std::int64_t mergedAmount = runtime::AddInt64Wrapping(destinationAmount, requestedAmount);
                if (mergedAmount > 999999999999LL) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "Warehouse error. db[%d] AmtSum[%lld] is invalid. "
                    //     "OldAmt[%lld] ItemChangeAmt[%lld] at [%s][%d]",
                    //     static_cast<int>(m_ownerId), static_cast<long long>(mergedAmount),
                    //     static_cast<long long>(destinationAmount), static_cast<long long>(requestedAmount), "WareHouse.cpp",
                    //     0x3bfb
                    // )
                    // Log.Add(LOG_ERROR,
                    //     "[warehouse] Deposit failed for owner %u, bank class %d, bank ID %d: destination item %u quantity %lld plus %lld gives %lld, above 999999999999. Rollback requested.",
                    //     m_ownerId, warehouseClassType, warehouseId, destinationItem->ItemID(), static_cast<long long>(destinationAmount), static_cast<long long>(requestedAmount), static_cast<long long>(mergedAmount))
                    L2_LOG_ERROR(Log,
                        "[warehouse] Deposit failed for owner %u, bank class %d, bank ID %d: destination item %u quantity %lld plus %lld gives %lld, above 999999999999; rollback requested.",
                        m_ownerId, warehouseClassType, warehouseId, destinationItem->ItemID(), static_cast<long long>(destinationAmount), static_cast<long long>(requestedAmount), static_cast<long long>(mergedAmount));
                    return rollback();
                }

                transaction.AddItem(destinationItem);
                sourceItem->SetAmount(runtime::SubtractInt64Wrapping(sourceAmount, requestedAmount));
                destinationItem->SetAmount(mergedAmount);
                if (sourceAmount == requestedAmount) {
                    sourceItem->TransactDelete();
                }
                remainingAmount = sourceItem->Amount();
                sourceItem->SetChangeScope(ItemChangeScope_Added);
                destinationItem->SetChangeScope(ItemChangeScope_Increased);
            }
        } else {
            sourceItem->setOwnerID(destinationWarehouse->m_ownerId);
            sourceItem->SetWarehouse(destinationWarehouse->GetWarehouseNum());
            remainingAmount = 0;
            sourceItem->SetChangeScope(ItemChangeScope_Deleted);
            newItemCount = runtime::AddInt32Wrapping(newItemCount, 1);
        }

        // CItemAssemblerAmount responseItem(*sourceItem.get(), requestedAmount, 0)
        CItemAssemblerAmount responseItem(*sourceItem.get(), requestedAmount, ItemAssembleType::NONE);
        responseItem.AssembleItemInfoImpl(responseItems);
        WriteDepositItemsToBankExAudit(actor, *sourceItem.get(), warehouseClassType, requestedAmount, remainingAmount);
    }

    const int resultingItemCount = runtime::AddInt32Wrapping(newItemCount, destinationWarehouse->m_itemNumber);
    if (newItemCount > 0 && warehouseItemLimit < resultingItemCount) {
        // Log.Add(LOG_WARN,
        //     "[warehouse] Deposit rejected for owner %u, bank class %d, bank ID %d: %d existing entries plus %d new entries gives %d, above capacity %d. Rollback requested.",
        //     m_ownerId, warehouseClassType, warehouseId, destinationWarehouse->m_itemNumber, newItemCount, resultingItemCount, warehouseItemLimit)
        L2_LOG_WARN(Log,
            "[warehouse] Deposit rejected for owner %u, bank class %d, bank ID %d: %d existing entries plus %d new entries gives %d, above capacity %d; rollback requested.",
            m_ownerId, warehouseClassType, warehouseId, destinationWarehouse->m_itemNumber, newItemCount, resultingItemCount, warehouseItemLimit);
        transaction.SetRollback();
        return 2;
    }

    CItemDisassemblerWareHouse feeDescriptor;
    feeDescriptor.DisassembleItemInfoImpl(cursor);
    CItemSP feeItem = GetItem(static_cast<int>(feeDescriptor.m_nItemID));
    if (!feeItem) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]Cannot getitem. [%d][%d]-[%d][%d]"
        //     "[%lld][%d][%d][%d]",
        //     "WareHouse.cpp", 0x3c6d, warehouseClassType, warehouseId, static_cast<int>(feeDescriptor.m_nItemID),
        //     static_cast<int>(feeDescriptor.m_nItemType), static_cast<long long>(feeDescriptor.m_nAmount),
        //     feeDescriptor.m_nEnchant, feeDescriptor.m_nEroded, static_cast<int>(feeDescriptor.m_eConsumeType)
        // )
        // Log.Add(LOG_ERROR,
        //     "[warehouse] Deposit fee item %u is missing for owner %u, bank class %d, bank ID %d (requested fee %lld). Rollback requested.",
        //     feeDescriptor.m_nItemID, m_ownerId, warehouseClassType, warehouseId, static_cast<long long>(feeDescriptor.m_nAmount))
        L2_LOG_ERROR(Log,
            "[warehouse] Deposit fee item %u is missing for owner %u, bank class %d, bank ID %d (requested fee %lld); rollback requested.",
            feeDescriptor.m_nItemID, m_ownerId, warehouseClassType, warehouseId, static_cast<long long>(feeDescriptor.m_nAmount));
        return rollback();
    }

    if (feeItem->ItemType() != 57) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]Cannot get banking fee. NOT adena. "
        //     "[%d][%d]-[%d][%d][%lld][%d][%d][%d]",
        //     "WareHouse.cpp", 0x3c74, warehouseClassType, warehouseId, static_cast<int>(feeDescriptor.m_nItemID),
        //     static_cast<int>(feeDescriptor.m_nItemType), static_cast<long long>(feeDescriptor.m_nAmount),
        //     feeDescriptor.m_nEnchant, feeDescriptor.m_nEroded, static_cast<int>(feeDescriptor.m_eConsumeType)
        // )
        // Log.Add(LOG_ERROR,
        //     "[warehouse] Deposit fee item %u for owner %u has class %u instead of adena class 57 (bank class %d, bank ID %d). Rollback requested.",
        //     feeItem->ItemID(), m_ownerId, feeItem->ItemType(), warehouseClassType, warehouseId)
        L2_LOG_ERROR(Log,
            "[warehouse] Deposit fee item %u for owner %u has class %u instead of adena class 57 (bank class %d, bank ID %d); rollback requested.",
            feeItem->ItemID(), m_ownerId, feeItem->ItemType(), warehouseClassType, warehouseId);
        return rollback();
    }

    transaction.AddItem(feeItem);
    const std::int64_t feeAmount = feeDescriptor.m_nAmount;
    const std::int64_t originalFeeItemAmount = feeItem->Amount();
    if (originalFeeItemAmount < feeAmount) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]Cannot get banking fee. Amount invalid. "
        //     "[%d][%d]-[%d][%d][%lld][%d][%d][%d]",
        //     "WareHouse.cpp", 0x3c7f, warehouseClassType, warehouseId, static_cast<int>(feeDescriptor.m_nItemID),
        //     static_cast<int>(feeDescriptor.m_nItemType), static_cast<long long>(feeAmount), feeDescriptor.m_nEnchant,
        //     feeDescriptor.m_nEroded, static_cast<int>(feeDescriptor.m_eConsumeType)
        // )
        // Log.Add(LOG_ERROR,
        //     "[warehouse] Deposit fee cannot be paid by owner %u: item %u has %lld adena, requested fee %lld (bank class %d, bank ID %d). Rollback requested.",
        //     m_ownerId, feeItem->ItemID(), static_cast<long long>(originalFeeItemAmount), static_cast<long long>(feeAmount), warehouseClassType, warehouseId)
        L2_LOG_ERROR(Log,
            "[warehouse] Deposit fee cannot be paid by owner %u: item %u has %lld adena, requested fee %lld (bank class %d, bank ID %d); rollback requested.",
            m_ownerId, feeItem->ItemID(), static_cast<long long>(originalFeeItemAmount), static_cast<long long>(feeAmount), warehouseClassType, warehouseId);
        return rollback();
    }

    feeItem->SetAmount(runtime::SubtractInt64Wrapping(originalFeeItemAmount, feeAmount));
    const std::int64_t remainingFeeAmount = feeItem->Amount();
    if (remainingFeeAmount < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]Cannot get banking fee 2. Amount invalid. "
        //     "[%d][%d]-[%d][%d][%lld][%d][%d][%d]",
        //     "WareHouse.cpp", 0x3c88, warehouseClassType, warehouseId, static_cast<int>(feeDescriptor.m_nItemID),
        //     static_cast<int>(feeDescriptor.m_nItemType), static_cast<long long>(feeAmount), feeDescriptor.m_nEnchant,
        //     feeDescriptor.m_nEroded, static_cast<int>(feeDescriptor.m_eConsumeType)
        // )
        // Log.Add(LOG_ERROR,
        //     "[warehouse] Deposit fee left negative quantity %lld on item %u for owner %u: previous %lld, fee %lld (bank class %d, bank ID %d). Rollback requested.",
        //     static_cast<long long>(remainingFeeAmount), feeItem->ItemID(), m_ownerId, static_cast<long long>(originalFeeItemAmount), static_cast<long long>(feeAmount), warehouseClassType, warehouseId)
        L2_LOG_ERROR(Log,
            "[warehouse] Deposit fee left negative quantity %lld on item %u for owner %u: previous %lld, fee %lld (bank class %d, bank ID %d); rollback requested.",
            static_cast<long long>(remainingFeeAmount), feeItem->ItemID(), m_ownerId, static_cast<long long>(originalFeeItemAmount), static_cast<long long>(feeAmount), warehouseClassType, warehouseId);
        return rollback();
    }

    if (remainingFeeAmount == 0) {
        feeItem->TransactDelete();
    }
    // CItemAssemblerAmount responseFeeItem(*feeItem.get(), feeAmount, 0)
    CItemAssemblerAmount responseFeeItem(*feeItem.get(), feeAmount, ItemAssembleType::NONE);
    responseFeeItem.AssembleItemInfoImpl(responseItems);
    WriteDepositItemsToBankExFeeAudit(actor, *feeItem.get(), warehouseClassType, feeAmount, remainingFeeAmount);

    socket->Send(
        "cdddddb", 0x7f, warehouseClassType, warehouseId, userServerId, 0, itemCount, responseItems.GetLength(),
        responseItems.GetPacket()
    );
    return 0;
}

bool CWareHouse::Withdraw(CSocket* socket, const std::uint8_t* packet, CWareHouse* inventory) {
    L2_GUARD_FUNCTION();

    int responseId = 0;
    int auditContext = 0;
    int itemCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "ddd", &responseId, &auditContext, &itemCount);

    CItemTransactionOrderMap transactionItems;
    CPackageItemPacketManager packetManager;
    int transactionOrder = 0;

    auto rollbackAndReply = [&]() {
        socket->Send("cdd", 10, responseId, -2);
        CTransaction::RollbackTransactOrderMap(transactionItems);
        return false;
    };

    auto packageItem = [&](CItem& item) {
        packetManager.CheckLimitPackageItemPacketDataSize();
        // CItemAssemblerSendToL2Server itemAssembler(item, 0x7f)
        CItemAssemblerSendToL2Server itemAssembler(item, ItemAssembleType::ALL);
        const int assembledLength = packetManager.PackageAssembleItem(itemAssembler);
        if (packetManager.m_nTotalPackageItemCount == 0) {
            packetManager.m_nAssembleItemLengthPerItem += assembledLength;
        }
        PACKAGE_ITEM_PACKETHELPER& currentPackage =
            packetManager
                .m_vPackageItemPacketHelperList[static_cast<std::size_t>(packetManager.m_nCurrentPacketHelperIndex)];
        ++currentPackage.nItemCount;
        ++packetManager.m_nTotalPackageItemCount;
    };

    for (int index = 0; index < itemCount; ++index) {
        int itemId = 0;
        long long wireAmount = 0;
        int amountType = 0;
        cursor = ::Disassemble(cursor, "dQd", &itemId, &wireAmount, &amountType);
        const std::int64_t requestedAmount = wireAmount;

        CItemSP sourceItem = GetItem(itemId);
        if (!sourceItem) {
            // Log.Add(LOG_ERROR, "Withdraw warehouse item invalid. User[%d], itemid[%d]", responseId, itemId)
            // Log.Add(LOG_ERROR,
            //     "[warehouse] Withdrawal failed for owner %u, warehouse %d, response %d: source item %d not found. Attempting rollback and sending an invalid-items reply.",
            //     m_ownerId, static_cast<int>(m_warehouseType), responseId, itemId)
            L2_LOG_ERROR(Log,
                "[warehouse] Withdrawal failed for owner %u, warehouse %d, response %d: source item %d not found; attempting rollback and sending an invalid-items reply.",
                m_ownerId, static_cast<int>(m_warehouseType), responseId, itemId);
            return rollbackAndReply();
        }

        sourceItem->SetTransation();
        CItemSP sourceRollback = sourceItem->Copy();
        sourceRollback->SetTrasactMode(TransactType_RollbackChanges);
        L2_ADD_REF(sourceItem, ObjectRefType::FunctionLocal);
        L2_ADD_REF(sourceRollback, ObjectRefType::FunctionLocal);
        transactionItems.emplace(++transactionOrder, CItemKey(sourceRollback.get(), sourceItem.get()));

        const std::int64_t sourceAmount = sourceItem->Amount();
        if (sourceAmount < requestedAmount) {
            // Log.Add(
            //     LOG_ERROR,
            //     "Withdraw warehouse item amount is invalid. User[%d], "
            //     "itemid[%d] TotalAmount[%lld] withdrawAmount[%lld]",
            //     responseId, itemId, static_cast<long long>(sourceAmount), static_cast<long long>(requestedAmount)
            // )
            // Log.Add(LOG_ERROR,
            //     "[warehouse] Withdrawal failed for owner %u, warehouse %d: item %d has quantity %lld, requested %lld. Attempting rollback and sending an invalid-items reply.",
            //     m_ownerId, static_cast<int>(m_warehouseType), itemId, static_cast<long long>(sourceAmount), static_cast<long long>(requestedAmount))
            L2_LOG_ERROR(Log,
                "[warehouse] Withdrawal failed for owner %u, warehouse %d: item %d has quantity %lld, requested %lld; attempting rollback and sending an invalid-items reply.",
                m_ownerId, static_cast<int>(m_warehouseType), itemId, static_cast<long long>(sourceAmount), static_cast<long long>(requestedAmount));
            return rollbackAndReply();
        }

        if (amountType == 2 || amountType == 3) {
            CItemSP inventoryItem = inventory->GetItemByType(static_cast<int>(sourceItem->ItemType()), 0);
            if (!inventoryItem) {
                if (sourceAmount == requestedAmount) {
                    sourceItem->SetWarehouse(inventory->GetWarehouseNum());
                    sourceItem->m_ident = 0;
                    packageItem(*sourceItem.get());
                    sourceItem->SetChangeScope(ItemChangeScope_Decreased);
                } else {
                    sourceItem->SetAmount(runtime::SubtractInt64Wrapping(sourceAmount, requestedAmount));
                    inventoryItem = inventory->MakeNewItem(
                        sourceItem->OwnerID(), sourceItem->ItemType(), requestedAmount, sourceItem->Enchant(),
                        sourceItem->Eroded(), sourceItem->Bless(), 0, sourceItem->Wished(), inventory->GetWarehouseNum(),
                        sourceItem->OptionKey(), sourceItem->m_intensiveItemType, sourceItem->Attribute(),
                        sourceItem->HLocked(), sourceItem->UseCount()
                    );
                    if (!inventoryItem) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     "Withdraw warehouse New item is invalid. "
                        //     "User[%d], itemid[%d]",
                        //     responseId, itemId
                        // )
                        // Log.Add(LOG_ERROR,
                        //     "[warehouse] Withdrawal failed for owner %u, warehouse %d: cannot create the inventory stack for item %d, quantity %lld. Attempting rollback and sending an invalid-items reply.",
                        //     m_ownerId, static_cast<int>(m_warehouseType), itemId, static_cast<long long>(requestedAmount))
                        L2_LOG_ERROR(Log,
                            "[warehouse] Withdrawal failed for owner %u, warehouse %d: cannot create the inventory stack for item %d, quantity %lld; attempting rollback and sending an invalid-items reply.",
                            m_ownerId, static_cast<int>(m_warehouseType), itemId, static_cast<long long>(requestedAmount));
                        return rollbackAndReply();
                    }

                    inventoryItem->SetTransation();
                    CItemSP inventoryRollback = inventoryItem->Copy();
                    inventoryRollback->SetTrasactMode(TransactType_RollbackCreation);
                    L2_ADD_REF(inventoryItem, ObjectRefType::FunctionLocal);
                    L2_ADD_REF(inventoryRollback, ObjectRefType::FunctionLocal);
                    transactionItems.emplace(++transactionOrder, CItemKey(inventoryRollback.get(), inventoryItem.get()));

                    packageItem(*inventoryItem.get());
                    sourceItem->SetChangeScope(ItemChangeScope_Added);
                    inventoryItem->SetChangeScope(ItemChangeScope_Fee);
                }
            } else {
                inventoryItem->SetTransation();
                CItemSP inventoryRollback = inventoryItem->Copy();
                inventoryRollback->SetTrasactMode(TransactType_RollbackChanges);
                L2_ADD_REF(inventoryItem, ObjectRefType::FunctionLocal);
                L2_ADD_REF(inventoryRollback, ObjectRefType::FunctionLocal);
                transactionItems.emplace(++transactionOrder, CItemKey(inventoryRollback.get(), inventoryItem.get()));

                sourceItem->SetAmount(runtime::SubtractInt64Wrapping(sourceAmount, requestedAmount));
                inventoryItem->SetAmount(runtime::AddInt64Wrapping(inventoryItem->Amount(), requestedAmount));
                inventoryItem->m_ident = 0;
                if (sourceAmount == requestedAmount) {
                    sourceItem->TransactDelete();
                }
                packageItem(*inventoryItem.get());
                sourceItem->SetChangeScope(ItemChangeScope_Added);
                inventoryItem->SetChangeScope(ItemChangeScope_Increased);
            }
        } else {
            sourceItem->SetWarehouse(inventory->GetWarehouseNum());
            sourceItem->m_ident = 0;
            packageItem(*sourceItem.get());
            sourceItem->SetChangeScope(ItemChangeScope_Deleted);
        }
    }

    CUserSP owner = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    const WareHouseAuditActor actor = MakeWareHouseAuditActor(owner.get());

    CPacketHelperEx<20480> header;
    header.Assemble("d", responseId);
    packetManager.SendPacket_PackageItemData(socket, 10, header);

    for (const auto& entry : transactionItems) {
        CItem* rollbackItem = entry.second.rollbackItem;
        CItem* item = entry.second.item;
        WriteWithdrawAudit(actor, *item, *rollbackItem, auditContext, item->m_intensiveItemType);
        item->CommitTransation(rollbackItem, true);
        L2_RELEASE(item, ObjectRefType::FunctionLocal);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
    }
    return true;
}

bool CWareHouse::Deposit(CSocket* socket, const std::uint8_t* packet, CWareHouse* inventory) {
    L2_GUARD_FUNCTION();

    int responseId = 0;
    int auditContext = 0;
    int itemCount = 0;
    int warehouseLimit = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "dddd", &responseId, &auditContext, &itemCount, &warehouseLimit);

    std::array<char, 0x5000> responseItems{};
    int responseSize = 0;
    int responseItemCount = 0;
    int newItemsCounter = 0;
    int transactionOrder = 0;
    CItemTransactionOrderMap transactionItems;

    auto rollbackAndReply = [&](int errorCode) {
        // Noise: Diagnostic-only reason belonged to the duplicate rollback message below.
        // const char* reason = errorCode == 1 ? "WHR_ITEM_COUNT_OVERFLOW" : "WHR_INVALID_ITEMS"
        // Noise: Every caller reports the specific failed item, fee or capacity check. This generic rollback message duplicates it.
        // Log.Add(
        //     LOG_ERROR, "Warehouse error. db[%d] rollback. nErrorCode[%d][%s]", static_cast<int>(m_ownerId),
        //     errorCode, reason
        // )
        socket->Send("cdd", 12, responseId, errorCode == 1 ? -4 : -2);
        CTransaction::RollbackTransactOrderMap(transactionItems);
        return false;
    };

    const int depositItemCount = runtime::AddInt32Wrapping(itemCount, -1);
    for (int index = 0; index < depositItemCount; ++index) {
        int itemId = 0;
        std::int64_t requestedAmount = 0;
        int amountType = 0;
        cursor = ::Disassemble(cursor, "dQd", &itemId, &requestedAmount, &amountType);

        CItemSP sourceItem = inventory->GetItem(itemId);
        if (!sourceItem) {
            // Log.Add(
            //     LOG_ERROR,
            //     "Warehouse error. db[%d] cannot find item. sid[%d] "
            //     "amount[%lld] AmountType[%d] at [%s][%d]",
            //     static_cast<int>(m_ownerId), itemId, static_cast<long long>(requestedAmount), amountType, "WareHouse.cpp",
            //     0x54e
            // )
            // Log.Add(LOG_ERROR,
            //     "[warehouse] Deposit failed for owner %u, warehouse %d: inventory item %d not found (quantity %lld, consume type %d). Attempting rollback and sending an invalid-items reply.",
            //     m_ownerId, static_cast<int>(m_warehouseType), itemId, static_cast<long long>(requestedAmount), amountType)
            L2_LOG_ERROR(Log,
                "[warehouse] Deposit failed for owner %u, warehouse %d: inventory item %d not found (quantity %lld, consume type %d); attempting rollback and sending an invalid-items reply.",
                m_ownerId, static_cast<int>(m_warehouseType), itemId, static_cast<long long>(requestedAmount), amountType);
            return rollbackAndReply(0);
        }

        sourceItem->SetTransation();
        CItemSP sourceRollback = sourceItem->Copy();
        sourceRollback->SetTrasactMode(TransactType_RollbackChanges);
        L2_ADD_REF(sourceItem, ObjectRefType::FunctionLocal);
        L2_ADD_REF(sourceRollback, ObjectRefType::FunctionLocal);
        transactionOrder = runtime::AddInt32Wrapping(transactionOrder, 1);
        transactionItems.emplace(transactionOrder, CItemKey(sourceRollback.get(), sourceItem.get()));

        const std::int64_t sourceAmount = sourceItem->Amount();
        if (sourceAmount < requestedAmount) {
            // Log.Add(
            //     LOG_ERROR,
            //     "Warehouse error. db[%d] ServerAmt[%lld] > "
            //     "CacheAmt[%lld]. sid[%d] AmtType[%d]  at [%s][%d]",
            //     static_cast<int>(m_ownerId), static_cast<long long>(requestedAmount), static_cast<long long>(sourceAmount),
            //     itemId, amountType, "WareHouse.cpp", 0x55f
            // )
            // Log.Add(LOG_ERROR,
            //     "[warehouse] Deposit failed for owner %u, warehouse %d: inventory item %d has quantity %lld, requested %lld. Attempting rollback and sending an invalid-items reply.",
            //     m_ownerId, static_cast<int>(m_warehouseType), itemId, static_cast<long long>(sourceAmount), static_cast<long long>(requestedAmount))
            L2_LOG_ERROR(Log,
                "[warehouse] Deposit failed for owner %u, warehouse %d: inventory item %d has quantity %lld, requested %lld; attempting rollback and sending an invalid-items reply.",
                m_ownerId, static_cast<int>(m_warehouseType), itemId, static_cast<long long>(sourceAmount), static_cast<long long>(requestedAmount));
            return rollbackAndReply(0);
        }

        std::int64_t remainingAmount = 0;
        if (amountType == 2 || amountType == 3) {
            remainingAmount = runtime::SubtractInt64Wrapping(sourceAmount, requestedAmount);
            CItemSP targetItem = GetItemByType(static_cast<int>(sourceItem->ItemType()), 0);
            if (!targetItem) {
                newItemsCounter = runtime::AddInt32Wrapping(newItemsCounter, 1);
                if (sourceAmount == requestedAmount) {
                    sourceItem->SetWarehouse(GetWarehouseNum());
                    sourceItem->SetChangeScope(ItemChangeScope_Decreased);
                } else {
                    sourceItem->SetAmount(remainingAmount);

                    const ItemOptionKey emptyOptionKey{};
                    const BaseAttribute emptyBaseAttribute{};
                    targetItem = MakeNewItem(
                        sourceItem->OwnerID(), sourceItem->ItemType(), 0, 0, 0, 0, 0, 0, 0, emptyOptionKey, 0,
                        emptyBaseAttribute, 0, 0
                    );
                    if (!targetItem) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     "Warehouse error. db[%d] cannot make new item "
                        //     "sid[%d] ItemAmount[%lld] AmountType[%d] at "
                        //     "[%s][%d]",
                        //     static_cast<int>(m_ownerId), itemId, static_cast<long long>(requestedAmount), amountType,
                        //     "WareHouse.cpp", 0x59b
                        // )
                        // Log.Add(LOG_ERROR,
                        //     "[warehouse] Deposit failed for owner %u, warehouse %d: cannot create the destination stack for item %d, quantity %lld. Attempting rollback and sending an invalid-items reply.",
                        //     m_ownerId, static_cast<int>(m_warehouseType), itemId, static_cast<long long>(requestedAmount))
                        L2_LOG_ERROR(Log,
                            "[warehouse] Deposit failed for owner %u, warehouse %d: cannot create the destination stack for item %d, quantity %lld; attempting rollback and sending an invalid-items reply.",
                            m_ownerId, static_cast<int>(m_warehouseType), itemId, static_cast<long long>(requestedAmount));
                        return rollbackAndReply(0);
                    }

                    targetItem->Set(
                        sourceItem->OwnerID(), sourceItem->ItemType(), requestedAmount, sourceItem->Enchant(),
                        sourceItem->Eroded(), sourceItem->Bless(), sourceItem->Ident(), sourceItem->Wished(),
                        GetWarehouseNum(), sourceItem->OptionKey(), sourceItem->m_intensiveItemType, sourceItem->Attribute(),
                        -1, sourceItem->AppearanceType(), sourceItem->HLocked(), sourceItem->UseCount()
                    );
                    targetItem->SetTransation();
                    CItemSP targetRollback = targetItem->Copy();
                    targetRollback->SetTrasactMode(TransactType_RollbackCreation);
                    L2_ADD_REF(targetItem, ObjectRefType::FunctionLocal);
                    L2_ADD_REF(targetRollback, ObjectRefType::FunctionLocal);
                    transactionOrder = runtime::AddInt32Wrapping(transactionOrder, 1);
                    transactionItems.emplace(transactionOrder, CItemKey(targetRollback.get(), targetItem.get()));
                    sourceItem->SetChangeScope(ItemChangeScope_Added);
                    targetItem->SetChangeScope(ItemChangeScope_Increased);
                }
            } else {
                targetItem->SetTransation();
                CItemSP targetRollback = targetItem->Copy();
                targetRollback->SetTrasactMode(TransactType_RollbackChanges);
                L2_ADD_REF(targetItem, ObjectRefType::FunctionLocal);
                L2_ADD_REF(targetRollback, ObjectRefType::FunctionLocal);
                transactionOrder = runtime::AddInt32Wrapping(transactionOrder, 1);
                transactionItems.emplace(transactionOrder, CItemKey(targetRollback.get(), targetItem.get()));

                constexpr std::uint64_t maximumItemAmount = 999999999999ULL;
                const std::int64_t combinedAmount = runtime::AddInt64Wrapping(targetItem->Amount(), requestedAmount);
                if (static_cast<std::uint64_t>(combinedAmount) > maximumItemAmount) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "Warehouse error. db[%d] AmtSum[%lld] is invalid. "
                    //     "sid[%d]  AmtType[%d] OldAmt[%lld] ItemAmt[%lld] "
                    //     "at [%s][%d]",
                    //     static_cast<int>(m_ownerId), static_cast<long long>(combinedAmount), itemId, amountType,
                    //     static_cast<long long>(targetItem->Amount()), static_cast<long long>(requestedAmount),
                    //     "WareHouse.cpp", 0x579
                    // )
                    // Log.Add(LOG_ERROR,
                    //     "[warehouse] Deposit failed for owner %u, warehouse %d: destination item %u quantity %lld plus %lld gives %lld, outside 0..999999999999. Attempting rollback and sending an invalid-items reply.",
                    //     m_ownerId, static_cast<int>(m_warehouseType), targetItem->ItemID(), static_cast<long long>(targetItem->Amount()), static_cast<long long>(requestedAmount), static_cast<long long>(combinedAmount))
                    L2_LOG_ERROR(Log,
                        "[warehouse] Deposit failed for owner %u, warehouse %d: destination item %u quantity %lld plus %lld gives %lld, outside 0..999999999999; attempting rollback and sending an invalid-items reply.",
                        m_ownerId, static_cast<int>(m_warehouseType), targetItem->ItemID(), static_cast<long long>(targetItem->Amount()), static_cast<long long>(requestedAmount), static_cast<long long>(combinedAmount));
                    return rollbackAndReply(0);
                }

                sourceItem->SetAmount(remainingAmount);
                targetItem->SetAmount(combinedAmount);
                if (sourceAmount == requestedAmount) {
                    sourceItem->TransactDelete();
                }
                sourceItem->SetChangeScope(ItemChangeScope_Added);
                targetItem->SetChangeScope(ItemChangeScope_Increased);
            }
        } else {
            newItemsCounter = runtime::AddInt32Wrapping(newItemsCounter, 1);
            sourceItem->SetWarehouse(GetWarehouseNum());
            sourceItem->SetChangeScope(ItemChangeScope_Deleted);
        }

        const int assembledSize = ::Assemble(
            responseItems.data() + responseSize, static_cast<int>(responseItems.size()) - responseSize, "dQ", itemId,
            remainingAmount
        );
        if (assembledSize > 0) {
            responseSize += assembledSize;
            ++responseItemCount;
        }
    }

    int adenaItemId = 0;
    std::int64_t depositPrice = 0;
    ::Disassemble(cursor, "dQ", &adenaItemId, &depositPrice);
    CItemSP adenaItem = inventory->GetItem(adenaItemId);
    if (!adenaItem) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Warehouse error. db[%d] cannot get adena in bank. sid[%d] "
        //     "ItemAmount[%lld] at [%s][%d]",
        //     static_cast<int>(m_ownerId), adenaItemId, static_cast<long long>(depositPrice), "WareHouse.cpp", 0x5ca
        // )
        // Log.Add(LOG_ERROR,
        //     "[warehouse] Deposit fee item %d is missing for owner %u, warehouse %d (requested adena change %lld). Attempting rollback and sending an invalid-items reply.",
        //     adenaItemId, m_ownerId, static_cast<int>(m_warehouseType), static_cast<long long>(depositPrice))
        L2_LOG_ERROR(Log,
            "[warehouse] Deposit fee item %d is missing for owner %u, warehouse %d (requested adena change %lld); attempting rollback and sending an invalid-items reply.",
            adenaItemId, m_ownerId, static_cast<int>(m_warehouseType), static_cast<long long>(depositPrice));
        return rollbackAndReply(0);
    }

    bool adenaAlreadyTracked = false;
    for (const auto& entry : transactionItems) {
        if (entry.second.item == adenaItem.get()) {
            adenaAlreadyTracked = true;
            break;
        }
    }
    if (!adenaAlreadyTracked) {
        adenaItem->SetTransation();
        CItemSP adenaRollback = adenaItem->Copy();
        adenaRollback->SetTrasactMode(TransactType_RollbackChanges);
        adenaRollback->SetChangeScope(ItemChangeScope_Fee);
        L2_ADD_REF(adenaItem, ObjectRefType::FunctionLocal);
        L2_ADD_REF(adenaRollback, ObjectRefType::FunctionLocal);
        transactionOrder = runtime::AddInt32Wrapping(transactionOrder, 1);
        transactionItems.emplace(transactionOrder, CItemKey(adenaRollback.get(), adenaItem.get()));
    }

    const std::int64_t adenaAmount = adenaItem->Amount();
    const std::int64_t feeAmount = runtime::NegateInt64Wrapping(depositPrice);
    if (adenaAmount < feeAmount) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Warehouse error. db[%d] (Amount[%lld] + "
        //     "ItemAmount[%lld] < 0) sid[%d] at [%s][%d]",
        //     static_cast<int>(m_ownerId), static_cast<long long>(adenaAmount), static_cast<long long>(depositPrice),
        //     adenaItemId, "WareHouse.cpp", 0x5ea
        // )
        // Log.Add(LOG_ERROR,
        //     "[warehouse] Deposit fee cannot be paid by owner %u, warehouse %d: item %d has %lld adena, required %lld (requested change %lld). Attempting rollback and sending an invalid-items reply.",
        //     m_ownerId, static_cast<int>(m_warehouseType), adenaItemId, static_cast<long long>(adenaAmount), static_cast<long long>(feeAmount), static_cast<long long>(depositPrice))
        L2_LOG_ERROR(Log,
            "[warehouse] Deposit fee cannot be paid by owner %u, warehouse %d: item %d has %lld adena, required %lld (requested change %lld); attempting rollback and sending an invalid-items reply.",
            m_ownerId, static_cast<int>(m_warehouseType), adenaItemId, static_cast<long long>(adenaAmount), static_cast<long long>(feeAmount), static_cast<long long>(depositPrice));
        return rollbackAndReply(0);
    }

    adenaItem->SetAmount(runtime::AddInt64Wrapping(adenaAmount, depositPrice));
    if (adenaItem->Amount() == 0) {
        adenaItem->TransactDelete();
    }
    const int adenaAssembledSize = ::Assemble(
        responseItems.data() + responseSize, static_cast<int>(responseItems.size()) - responseSize, "dQ", adenaItemId,
        adenaItem->Amount()
    );
    if (adenaAssembledSize > 0) {
        responseSize += adenaAssembledSize;
        ++responseItemCount;
    }

    const int resultingItemCount = runtime::AddInt32Wrapping(m_itemNumber, newItemsCounter);
    if (newItemsCounter > 0 && resultingItemCount > warehouseLimit) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Warehouse error. db[%d] (RealNewCnt[%d] + "
        //     "GetDepositCnt[%d] > limit[%d]) sid[%d] Amt[%lld] at "
        //     "[%s][%d]",
        //     static_cast<int>(m_ownerId), newItemsCounter, m_itemNumber, warehouseLimit, adenaItemId,
        //     static_cast<long long>(depositPrice), "WareHouse.cpp", 0x604
        // )
        // Log.Add(LOG_WARN,
        //     "[warehouse] Deposit rejected for owner %u, warehouse %d: %d existing entries plus %d new entries gives %d, above capacity %d. Attempting rollback and sending a warehouse-full reply.",
        //     m_ownerId, static_cast<int>(m_warehouseType), m_itemNumber, newItemsCounter, resultingItemCount, warehouseLimit)
        L2_LOG_WARN(Log,
            "[warehouse] Deposit rejected for owner %u, warehouse %d: %d existing entries plus %d new entries gives %d, above capacity %d; attempting rollback and sending a warehouse-full reply.",
            m_ownerId, static_cast<int>(m_warehouseType), m_itemNumber, newItemsCounter, resultingItemCount, warehouseLimit);
        return rollbackAndReply(1);
    }

    CUserSP owner = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    const WareHouseAuditActor actor = MakeWareHouseAuditActor(owner.get());

    socket->Send("cddb", 12, responseId, responseItemCount, responseSize, responseItems.data());
    WriteDepositFeeAudit(actor, auditContext, feeAmount);

    for (const auto& entry : transactionItems) {
        CItem* rollbackItem = entry.second.rollbackItem;
        CItem* item = entry.second.item;
        if (rollbackItem->ChangeScope() != ItemChangeScope_Fee) {
            WriteDepositAudit(
                actor, *item, *rollbackItem, auditContext, static_cast<std::uint32_t>(adenaItemId), feeAmount,
                item->m_intensiveItemType
            );
        }
        item->CommitTransation(rollbackItem, true);
        L2_RELEASE(item, ObjectRefType::FunctionLocal);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
    }
    return true;
}

BuySellResult CWareHouse::BuyItems(CSocket* socket, const std::uint8_t* packet, unsigned int responseId) {
    L2_GUARD_FUNCTION();

    int auditContext = 0;
    std::int64_t totalPrice = 0;
    int echoedValue = 0;
    int residencePledgeMasterId = -1;
    char detailedAudit = 1;
    int itemCount = 0;
    const std::uint8_t* cursor = ::Disassemble(
        packet, "dQddcd", &auditContext, &totalPrice, &echoedValue, &residencePledgeMasterId, &detailedAudit, &itemCount
    );

    CItemTransactionOrderMap transactionItems;
    std::set<int> stackableItemTypes;
    CPacketHelperEx<20480> responseItems;
    int transactionOrder = 0;
    int responseItemCount = 0;
    int adenaItemId = 0;
    std::int64_t adenaAmount = 0;

    auto rollbackAndReply = [&](BuySellResult result) {
        CTransaction::RollbackTransactOrderMap(transactionItems);
        socket->Send(
            "cddQddd", 14, static_cast<int>(responseId), 0, static_cast<std::int64_t>(0), echoedValue,
            static_cast<int>(result), 0
        );
        return result;
    };

    if (totalPrice > 0) {
        CItemSP adenaItem = GetAdena();
        if (!adenaItem) {
            // Log.Add(LOG_WARN,
            //     "[shop] Purchase rejected for owner %u: no adena stack for payment %lld. Attempting rollback and sending an insufficient-funds reply.",
            //     m_ownerId, static_cast<long long>(totalPrice))
            L2_LOG_WARN(Log,
                "[shop] Purchase rejected for owner %u: no adena stack for payment %lld; attempting rollback and sending an insufficient-funds reply.",
                m_ownerId, static_cast<long long>(totalPrice));
            return rollbackAndReply(BSR_LACK_OF_MONEY);
        }

        adenaItem->SetTransation();
        CItemSP adenaRollback = adenaItem->Copy();
        adenaRollback->SetTrasactMode(TransactType_RollbackChanges);
        L2_ADD_REF(adenaItem, ObjectRefType::FunctionLocal);
        L2_ADD_REF(adenaRollback, ObjectRefType::FunctionLocal);
        transactionOrder = runtime::AddInt32Wrapping(transactionOrder, 1);
        transactionItems.emplace(transactionOrder, CItemKey(adenaRollback.get(), adenaItem.get()));

        if (adenaItem->Amount() < totalPrice) {
            // Log.Add(LOG_WARN,
            //     "[shop] Purchase rejected for owner %u: item %u has %lld adena, payment requires %lld. Attempting rollback and sending an insufficient-funds reply.",
            //     m_ownerId, adenaItem->ItemID(), static_cast<long long>(adenaItem->Amount()), static_cast<long long>(totalPrice))
            L2_LOG_WARN(Log,
                "[shop] Purchase rejected for owner %u: item %u has %lld adena, payment requires %lld; attempting rollback and sending an insufficient-funds reply.",
                m_ownerId, adenaItem->ItemID(), static_cast<long long>(adenaItem->Amount()), static_cast<long long>(totalPrice));
            return rollbackAndReply(BSR_LACK_OF_MONEY);
        }

        adenaItem->SetAmount(runtime::SubtractInt64Wrapping(adenaItem->Amount(), totalPrice));
        if (adenaItem->Amount() == 0) {
            adenaItem->TransactDelete();
        }
        adenaAmount = adenaItem->Amount();
        adenaItemId = static_cast<int>(adenaItem->ItemID());
    }

    for (int itemIndex = 0; itemIndex < itemCount; ++itemIndex) {
        CItemDisassemblerBuyItem incomingItem;
        cursor = incomingItem.DisassembleItemInfoImpl(cursor);
        if (incomingItem.m_nAmount < 1) {
            continue;
        }

        CItemSP item;
        CItemSP rollbackItem;
        const int consumeType = static_cast<int>(incomingItem.m_eConsumeType);
        if (consumeType == 2 || consumeType == 3) {
            if (incomingItem.m_nItemType == 0) {
                // Log.Add(
                //     LOG_ERROR, "[%s][%d]BuyItems error.[%u][%u][%lld][%d]", "WareHouse.cpp", 0xb88, m_ownerId,
                //     incomingItem.m_nItemType, static_cast<long long>(incomingItem.m_nAmount), consumeType
                // )
                // Log.Add(LOG_ERROR,
                //     "[shop] Purchase rejected for owner %u: stackable item class is zero (quantity %lld, consume type %d). Attempting rollback and sending an invalid-items reply.",
                //     m_ownerId, static_cast<long long>(incomingItem.m_nAmount), consumeType)
                L2_LOG_ERROR(Log,
                    "[shop] Purchase rejected for owner %u: stackable item class is zero (quantity %lld, consume type %d); attempting rollback and sending an invalid-items reply.",
                    m_ownerId, static_cast<long long>(incomingItem.m_nAmount), consumeType);
                return rollbackAndReply(BSR_INVALID_ITEMS);
            }

            const int itemType = static_cast<int>(incomingItem.m_nItemType);
            if (stackableItemTypes.find(itemType) != stackableItemTypes.end()) {
                // Log.Add(LOG_ERROR, "Trying To buy same stackable item[owner dbid : %u]", m_ownerId)
                // Log.Add(LOG_ERROR,
                //     "[shop] Purchase rejected for owner %u: item class %u is listed more than once as stackable. Attempting rollback and sending an invalid-items reply.",
                //     m_ownerId, incomingItem.m_nItemType)
                L2_LOG_ERROR(Log,
                    "[shop] Purchase rejected for owner %u: item class %u is listed more than once as stackable; attempting rollback and sending an invalid-items reply.",
                    m_ownerId, incomingItem.m_nItemType);
                return rollbackAndReply(BSR_INVALID_ITEMS);
            }

            item = GetItemByType(itemType, 0);
            if (!item) {
                const ItemOptionKey emptyOptionKey{};
                const BaseAttribute emptyBaseAttribute{};
                item = MakeNewItem(
                    m_ownerId, incomingItem.m_nItemType, 0, 0, 0, 0, 0, 0, WareHouseType_Inventory, emptyOptionKey, 0,
                    emptyBaseAttribute, 0, 0
                );
                if (!item) {
                    // Log.Add(LOG_ERROR,
                    //     "[shop] Cannot create the purchased stack of class %u for owner %u, quantity %lld. Attempting rollback of the purchase and sending an invalid-items reply.",
                    //     incomingItem.m_nItemType, m_ownerId, static_cast<long long>(incomingItem.m_nAmount))
                    L2_LOG_ERROR(Log,
                        "[shop] Cannot create the purchased stack of class %u for owner %u, quantity %lld; attempting rollback of the purchase and sending an invalid-items reply.",
                        incomingItem.m_nItemType, m_ownerId, static_cast<long long>(incomingItem.m_nAmount));
                    return rollbackAndReply(BSR_INVALID_ITEMS);
                }

                item->SetTransation();
                rollbackItem = item->Copy();
                rollbackItem->SetTrasactMode(TransactType_RollbackCreation);
                L2_ADD_REF(item, ObjectRefType::FunctionLocal);
                L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
            } else {
                item->SetTransation();
                rollbackItem = item->Copy();
                rollbackItem->SetTrasactMode(TransactType_RollbackChanges);
                L2_ADD_REF(item, ObjectRefType::FunctionLocal);
                L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
            }

            transactionOrder = runtime::AddInt32Wrapping(transactionOrder, 1);
            transactionItems.emplace(transactionOrder, CItemKey(rollbackItem.get(), item.get()));
            stackableItemTypes.emplace(itemType);

            item->SetAmount(runtime::AddInt64Wrapping(item->Amount(), incomingItem.m_nAmount));
            BaseAttribute emptyResponseAttribute{};
            // CItemAssemblerSendToL2Server itemAssembler(*item.get(), emptyResponseAttribute, 0x7f)
            CItemAssemblerSendToL2Server itemAssembler(*item.get(), emptyResponseAttribute, ItemAssembleType::ALL);
            if (itemAssembler.AssembleItemInfoImpl(responseItems) > 0) {
                responseItemCount = runtime::AddInt32Wrapping(responseItemCount, 1);
            }
        } else {
            if (incomingItem.m_nAmount != 1 || incomingItem.m_nItemType == 0) {
                // Log.Add(
                //     LOG_ERROR, "[%s][%d]BuyItems error.[%u][%u][%lld][%d]", "WareHouse.cpp", 0xbd0, m_ownerId,
                //     incomingItem.m_nItemType, static_cast<long long>(incomingItem.m_nAmount), consumeType
                // )
                // Log.Add(LOG_ERROR,
                //     "[shop] Purchase rejected for owner %u: individual item requires a nonzero class and quantity one (class %u, quantity %lld, consume type %d). Attempting rollback and sending an invalid-items reply.",
                //     m_ownerId, incomingItem.m_nItemType, static_cast<long long>(incomingItem.m_nAmount), consumeType)
                L2_LOG_ERROR(Log,
                    "[shop] Purchase rejected for owner %u: individual item requires a nonzero class and quantity one (class %u, quantity %lld, consume type %d); attempting rollback and sending an invalid-items reply.",
                    m_ownerId, incomingItem.m_nItemType, static_cast<long long>(incomingItem.m_nAmount), consumeType);
                return rollbackAndReply(BSR_INVALID_ITEMS);
            }

            const ItemOptionKey emptyOptionKey{};
            const BaseAttribute emptyBaseAttribute{};
            item = MakeNewItem(
                m_ownerId, incomingItem.m_nItemType, incomingItem.m_nAmount, 0, 0, 0, 0, 0, WareHouseType_Inventory,
                emptyOptionKey, 0, emptyBaseAttribute, incomingItem.m_hLocked, incomingItem.m_hUseCount
            );
            if (!item) {
                // Log.Add(LOG_ERROR, "[%s][%d]BuyItems error.", "WareHouse.cpp", 0xbe4)
                // Log.Add(LOG_ERROR,
                //     "[shop] Cannot create purchased item class %u for owner %u, quantity %lld. Attempting rollback of the purchase and sending an invalid-items reply.",
                //     incomingItem.m_nItemType, m_ownerId, static_cast<long long>(incomingItem.m_nAmount))
                L2_LOG_ERROR(Log,
                    "[shop] Cannot create purchased item class %u for owner %u, quantity %lld; attempting rollback of the purchase and sending an invalid-items reply.",
                    incomingItem.m_nItemType, m_ownerId, static_cast<long long>(incomingItem.m_nAmount));
                return rollbackAndReply(BSR_INVALID_ITEMS);
            }

            item->SetTransation();
            rollbackItem = item->Copy();
            rollbackItem->SetTrasactMode(TransactType_RollbackCreation);
            L2_ADD_REF(item, ObjectRefType::FunctionLocal);
            L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
            transactionOrder = runtime::AddInt32Wrapping(transactionOrder, 1);
            transactionItems.emplace(transactionOrder, CItemKey(rollbackItem.get(), item.get()));

            item->m_enchant = incomingItem.m_nEnchant;
            item->m_eroded = incomingItem.m_nEroded;
            item->m_bless = incomingItem.m_nBless;
            item->m_optionKey.variationOption1 = incomingItem.m_nVariation1;
            item->m_optionKey.variationOption2 = incomingItem.m_nVariation2;
            item->m_intensiveItemType = incomingItem.m_nIntensiveItemType;
            item->SetBaseAttribute(incomingItem.m_Attribute);
            item->SetApperanceType(incomingItem.m_nShapeShiftedClassID, -1, static_cast<ShapeShiftingJobType>(3));
            item->SetEnsoulInfo(incomingItem.m_mEnsoulInfo.data());
            item->m_hLocked = incomingItem.m_hLocked;
            item->m_useCount = incomingItem.m_hUseCount;

            // CItemAssemblerSendToL2Server itemAssembler(*item.get(), 0x7f)
            CItemAssemblerSendToL2Server itemAssembler(*item.get(), ItemAssembleType::ALL);
            if (itemAssembler.AssembleItemInfoImpl(responseItems) > 0) {
                responseItemCount = runtime::AddInt32Wrapping(responseItemCount, 1);
            }
        }

        item->SetPrivateStorePrice(incomingItem.m_nPrice);
        rollbackItem->SetPrivateStorePrice(incomingItem.m_nPrice);
    }

    CUserSP owner = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    CLogFormatter formatter;
    for (const auto& entry : transactionItems) {
        CItem* rollbackItem = entry.second.rollbackItem;
        CItem* item = entry.second.item;
        const std::int64_t amountDifference =
            item->Amount() == rollbackItem->Amount() ? 1 : runtime::SubtractInt64Wrapping(item->Amount(), rollbackItem->Amount());

        if (detailedAudit == 0) {
            std::array<char16_t, 256> shapeShiftAndVariationText{};
            std::array<char16_t, 256> baseAttributeText{};
            std::array<char16_t, 256> ensoulText{};
            item->MakeShapeShiftAndVariationLogInfo(shapeShiftAndVariationText.data());
            BaseAttribute baseAttribute(item->Attribute());
            baseAttribute.GetLogString(baseAttributeText.data());
            // item->MakeEnsoulLogInfo(ensoulText.data())
            item->MakeEnsoulLogInfo(ensoulText.data(), ensoulText.size());
            formatter.WriteLogCache1(
                0x232b, owner.get(), item, shapeShiftAndVariationText.data(), baseAttributeText.data(), ensoulText.data(),
                auditContext, item->PrivateStorePrice(), item->Enchant(), item->ItemType(), amountDifference, totalPrice
            );
        } else {
            formatter.WriteLogCache2(
                0x385, owner.get(), item, auditContext, item->PrivateStorePrice(), item->Enchant(), item->ItemType(),
                amountDifference, item->Amount()
            );
        }

        item->CommitTransation(rollbackItem, true);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
        L2_RELEASE(item, ObjectRefType::FunctionLocal);
    }

    socket->Send(
        "cddQdddb", 14, static_cast<int>(responseId), adenaItemId, adenaAmount, echoedValue, static_cast<int>(BSR_SUCCESS),
        responseItemCount, responseItems.GetLength(), responseItems.GetPacket()
    );
    return BSR_SUCCESS;
}

BuySellResult CWareHouse::SellItems(CSocket* socket, const std::uint8_t* packet, unsigned int responseId) {
    L2_GUARD_FUNCTION();

    int auditContext = 0;
    std::int64_t soldPrice = 0;
    int itemCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "dQd", &auditContext, &soldPrice, &itemCount);

    CItemTransactionOrderMap transactionItems;
    int transactionOrder = 0;
    auto rollbackAndReply = [&](BuySellResult result) {
        CTransaction::RollbackTransactOrderMap(transactionItems);
        socket->Send(
            "cddQdd", 13, static_cast<int>(responseId), 0, static_cast<std::int64_t>(0), static_cast<int>(result), 0
        );
        return result;
    };

    // The current function performs this lookup before testing soldPrice,
    // including the zero and negative-price paths.
    CItemSP adenaItem = GetAdena();
    CItemSP adenaRollback;
    int adenaItemId = 0;
    std::int64_t newAdenaAmount = 0;

    if (soldPrice != 0) {
        if (soldPrice < 0) {
            // The shipping variadic call narrows soldPrice to the first %d,
            // followed by itemCount and auditContext.
            // Log.Add(
            //     LOG_ERROR, "SellItems price amount error [%d][%d][%d]", static_cast<int>(soldPrice), itemCount,
            //     auditContext
            // )
            // Log.Add(LOG_ERROR,
            //     "[shop] Sale rejected for owner %u: total price %lld is negative (item count %d, audit context %d). Attempting rollback and sending an invalid-items reply.",
            //     m_ownerId, static_cast<long long>(soldPrice), itemCount, auditContext)
            L2_LOG_ERROR(Log,
                "[shop] Sale rejected for owner %u: total price %lld is negative (item count %d, audit context %d); attempting rollback and sending an invalid-items reply.",
                m_ownerId, static_cast<long long>(soldPrice), itemCount, auditContext);
            runtime::process::ReportNamedCallStack(L"CWareHouse::SellItems", 2, false);
            return rollbackAndReply(BSR_INVALID_ITEMS);
        }

        bool createdAdena = false;
        if (!adenaItem) {
            const ItemOptionKey emptyOptionKey{};
            const BaseAttribute emptyBaseAttribute{};
            adenaItem = MakeNewItem(
                m_ownerId, 57, 0, 0, 0, 0, 0, 0, WareHouseType_Inventory, emptyOptionKey, 0, emptyBaseAttribute, 0, 0
            );
            if (!adenaItem) {
                // Log.Add(LOG_ERROR,
                //     "[shop] Cannot create the adena stack to credit owner %u for a sale worth %lld. Attempting rollback and returning insufficient funds.",
                //     m_ownerId, static_cast<long long>(soldPrice))
                L2_LOG_ERROR(Log,
                    "[shop] Cannot create the adena stack to credit owner %u for a sale worth %lld; attempting rollback and returning insufficient funds.",
                    m_ownerId, static_cast<long long>(soldPrice));
                return rollbackAndReply(BSR_LACK_OF_MONEY);
            }
            createdAdena = true;
        }

        adenaItem->SetTransation();
        adenaRollback = adenaItem->Copy();
        adenaRollback->SetTrasactMode(createdAdena ? TransactType_RollbackCreation : TransactType_RollbackChanges);
        L2_ADD_REF(adenaItem, ObjectRefType::FunctionLocal);
        L2_ADD_REF(adenaRollback, ObjectRefType::FunctionLocal);
        transactionOrder = runtime::AddInt32Wrapping(transactionOrder, 1);
        transactionItems.emplace(transactionOrder, CItemKey(adenaRollback.get(), adenaItem.get()));

        adenaItem->SetAmount(runtime::AddInt64Wrapping(adenaItem->Amount(), soldPrice));
        newAdenaAmount = adenaItem->Amount();
        adenaItemId = static_cast<int>(adenaItem->ItemID());
    }

    std::array<char, 0x5000> responseItems{};
    int responseSize = 0;
    for (int itemIndex = 0; itemIndex < itemCount; ++itemIndex) {
        int itemId = 0;
        std::int64_t sellAmount = 0;
        std::int64_t refundPrice = 0;
        cursor = ::Disassemble(cursor, "dQQ", &itemId, &sellAmount, &refundPrice);

        CItemSP item = GetItem(itemId);
        if (!item) {
            // Log.Add(LOG_ERROR,
            //     "[shop] Sale rejected for owner %u: item %d not found (requested quantity %lld). Attempting rollback of the sale, including any adena credit already staged.",
            //     m_ownerId, itemId, static_cast<long long>(sellAmount))
            L2_LOG_ERROR(Log,
                "[shop] Sale rejected for owner %u: item %d not found (requested quantity %lld); attempting rollback of the sale, including any adena credit already staged.",
                m_ownerId, itemId, static_cast<long long>(sellAmount));
            return rollbackAndReply(BSR_INVALID_ITEMS);
        }

        item->SetTransation();
        CItemSP rollbackItem = item->Copy();
        rollbackItem->SetTrasactMode(TransactType_RollbackChanges);
        L2_ADD_REF(item, ObjectRefType::FunctionLocal);
        L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
        transactionOrder = runtime::AddInt32Wrapping(transactionOrder, 1);
        transactionItems.emplace(transactionOrder, CItemKey(rollbackItem.get(), item.get()));

        const std::int64_t remainingAmount = runtime::SubtractInt64Wrapping(item->Amount(), sellAmount);
        if (remainingAmount == 0) {
            item->SetAmount(0);
            item->TransactDelete();
        } else {
            if (remainingAmount < 1) {
                // Log.Add(LOG_ERROR,
                //     "[shop] Sale rejected for owner %u: item %d quantity %lld minus requested %lld gives invalid remainder %lld. Attempting rollback of the sale, including any adena credit already staged.",
                //     m_ownerId, itemId, static_cast<long long>(item->Amount()), static_cast<long long>(sellAmount), static_cast<long long>(remainingAmount))
                L2_LOG_ERROR(Log,
                    "[shop] Sale rejected for owner %u: item %d quantity %lld minus requested %lld gives invalid remainder %lld; attempting rollback of the sale, including any adena credit already staged.",
                    m_ownerId, itemId, static_cast<long long>(item->Amount()), static_cast<long long>(sellAmount), static_cast<long long>(remainingAmount));
                return rollbackAndReply(BSR_INVALID_ITEMS);
            }
            item->SetAmount(remainingAmount);
        }

        // Current response format at VA 0x0076b790 is dQQQ. L2Server's
        // paired User::OnSellItemsPacket consumes the fields in this exact
        // order and feeds sellAmount/refundPrice to _ItemRefundPush.
        const int assembledSize = ::Assemble(
            responseItems.data() + responseSize, static_cast<int>(responseItems.size()) - responseSize, "dQQQ", itemId,
            sellAmount, remainingAmount, refundPrice
        );
        if (assembledSize > 0) {
            responseSize += assembledSize;
        }
    }

    CUserSP owner = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    const WareHouseAuditActor actor = MakeWareHouseAuditActor(owner.get());

    for (const auto& entry : transactionItems) {
        CItem* rollbackItem = entry.second.rollbackItem;
        CItem* item = entry.second.item;
        WriteSellItemsAudit(actor, *item, *rollbackItem, auditContext);
        item->CommitTransation(rollbackItem, true);
        L2_RELEASE(item, ObjectRefType::FunctionLocal);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
    }

    socket->Send(
        "cddQddb", 13, static_cast<int>(responseId), adenaItemId, newAdenaAmount, static_cast<int>(BSR_SUCCESS), itemCount,
        responseSize, responseItems.data()
    );
    return BSR_SUCCESS;
}

bool CWareHouse::ChangeItemData(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x005e7884. The socket participates in the original
    // signature but the shipping body does not read it.
    L2_GUARD_FUNCTION();

    CUserSP owner = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    if (!owner)
        return false;

    int itemCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "d", &itemCount);

    for (int itemIndex = 0; itemIndex < itemCount; ++itemIndex) {
        CItemDisassemblerChangeItemData incoming;
        cursor = incoming.DisassembleItemInfoImpl(cursor);

        if (incoming.m_nAmount < 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     "nItemDeletedAmount is minus "
            //     "[%d][%d][%lld][%d][%d][%d][%d][%d][%d]",
            //     incoming.m_nChangeType, static_cast<int>(incoming.m_nItemType), static_cast<long long>(incoming.m_nAmount),
            //     incoming.m_nEnchant, incoming.m_nEroded, static_cast<int>(incoming.m_nBless), incoming.m_nIdent,
            //     static_cast<int>(incoming.m_nWished), incoming.m_nWarehouse
            // )
            // L2_LOG_ERROR(Log,
            //     "nItemDeletedAmount is minus "
            //     "[%d][%d][%lld][%d][%d][%d][%d][%d][%d]",
            //     incoming.m_nChangeType, static_cast<int>(incoming.m_nItemType), static_cast<long long>(incoming.m_nAmount),
            //     incoming.m_nEnchant, incoming.m_nEroded, static_cast<int>(incoming.m_nBless), incoming.m_nIdent,
            //     static_cast<int>(incoming.m_nWished), incoming.m_nWarehouse
            // )
            L2_LOG_ERROR(Log,
                "[warehouse] Stopping item-data update for owner %u at entry %d of %d: item %u, class %u has negative quantity %lld. "
                "Change type %d, enchant %d, eroded %d, bless %d, ident %d, wished %d, warehouse %d.",
                m_ownerId, itemIndex + 1, itemCount, static_cast<unsigned int>(incoming.m_nItemID),
                static_cast<unsigned int>(incoming.m_nItemType), static_cast<long long>(incoming.m_nAmount),
                incoming.m_nChangeType, incoming.m_nEnchant, incoming.m_nEroded, static_cast<int>(incoming.m_nBless),
                incoming.m_nIdent, static_cast<int>(incoming.m_nWished), incoming.m_nWarehouse);
            return false;
        }

        CItemSP item = GetItem(static_cast<int>(incoming.m_nItemID));
        if (!item) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d]Cannot find item[%d] "
            //     "[%d][%d][%lld][%d][%d][%d][%d][%d][%d]",
            //     "WareHouse.cpp", 0x2012, static_cast<int>(incoming.m_nItemID), incoming.m_nChangeType,
            //     static_cast<int>(incoming.m_nItemType), static_cast<long long>(incoming.m_nAmount), incoming.m_nEnchant,
            //     incoming.m_nEroded, static_cast<int>(incoming.m_nBless), incoming.m_nIdent,
            //     static_cast<int>(incoming.m_nWished), incoming.m_nWarehouse
            // )
            L2_LOG_ERROR(Log,
                "[warehouse] Skipping item-data update entry %d of %d for owner %u: item %u was not found. "
                "Change type %d, class %u, quantity %lld, enchant %d, eroded %d, bless %d, ident %d, wished %d, warehouse %d. "
                "Other entries continue processing.",
                itemIndex + 1, itemCount, m_ownerId, static_cast<unsigned int>(incoming.m_nItemID), incoming.m_nChangeType,
                static_cast<unsigned int>(incoming.m_nItemType), static_cast<long long>(incoming.m_nAmount), incoming.m_nEnchant,
                incoming.m_nEroded, static_cast<int>(incoming.m_nBless), incoming.m_nIdent,
                static_cast<int>(incoming.m_nWished), incoming.m_nWarehouse);
            continue;
        }

        const std::int64_t originalAmount = item->Amount();
        const std::int64_t remainingAmount = runtime::SubtractInt64Wrapping(originalAmount, incoming.m_nAmount);

        if (incoming.m_nChangeType == 2) {
            if (remainingAmount < 1) {
                DeleteItemBySP(item);
            } else {
                ItemOptionKey optionKey;
                optionKey.variationOption1 = incoming.m_nVariation1;
                optionKey.variationOption2 = incoming.m_nVariation2;
                BaseAttribute attribute(
                    incoming.m_Attribute.m_nAttackType, incoming.m_Attribute.m_nAttackValue,
                    incoming.m_Attribute.m_nDefendValue
                );
                item->Set(
                    m_ownerId, incoming.m_nItemType, remainingAmount, incoming.m_nEnchant, incoming.m_nEroded,
                    incoming.m_nBless, incoming.m_nIdent, incoming.m_nWished, static_cast<int>(GetWarehouseNum()), optionKey,
                    incoming.m_nIntensiveItemType, attribute, -1, incoming.m_nShapeShiftedClassID, incoming.m_hLocked,
                    incoming.m_hUseCount
                );
                item->Save(true);
            }
        } else if (incoming.m_nChangeType == 3) {
            DeleteItemBySP(item);
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d] flase flag "
            //     "[%d][%d][%lld][%d][%d][%d][%d][%d][%d]",
            //     "WareHouse.cpp", 0x2035, incoming.m_nChangeType, static_cast<int>(incoming.m_nItemType),
            //     static_cast<long long>(incoming.m_nAmount), incoming.m_nEnchant, incoming.m_nEroded,
            //     static_cast<int>(incoming.m_nBless), incoming.m_nIdent, static_cast<int>(incoming.m_nWished),
            //     incoming.m_nWarehouse
            // )
            L2_LOG_ERROR(Log,
                "[warehouse] Stopping item-data update for owner %u at entry %d of %d: unsupported change type %d "
                "for item %u, class %u, quantity %lld. Enchant %d, eroded %d, bless %d, ident %d, wished %d, warehouse %d. "
                "Earlier entries are not rolled back by this branch.",
                m_ownerId, itemIndex + 1, itemCount, incoming.m_nChangeType,
                static_cast<unsigned int>(incoming.m_nItemID), static_cast<unsigned int>(incoming.m_nItemType),
                static_cast<long long>(incoming.m_nAmount), incoming.m_nEnchant, incoming.m_nEroded,
                static_cast<int>(incoming.m_nBless), incoming.m_nIdent, static_cast<int>(incoming.m_nWished),
                incoming.m_nWarehouse);
            break;
        }

        CLogFormatter formatter;
        formatter.WriteLogCache2(
            0x232c, owner.get(), item.get(), originalAmount, incoming.m_nAmount, incoming.m_nChangeType, 0, 0, 0
        );
    }

    return true;
}

bool CWareHouse::UseItem(CSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int itemId = 0;
    int consumeType = 0;
    std::int64_t requestedAmount = 0;
    int characterId = 0;
    int itemServerId = 0;
    int userServerId = 0;
    int responseFlag = 0;
    ::Disassemble(
        packet, "ddQdddd", &itemId, &consumeType, &requestedAmount, &characterId, &itemServerId, &userServerId, &responseFlag
    );

    auto sendFailure = [&]() { socket->Send("cddQd", 16, userServerId, itemServerId, static_cast<std::int64_t>(-1), 0); };

    CItemSP item = GetItem(itemId);
    if (!item) {
        sendFailure();
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]Useitem::Cannot find item, "
        //     "[%d][%d][%lld][%d][%d][%d]",
        //     "WareHouse.cpp", 0x20a6, itemId, consumeType, static_cast<long long>(requestedAmount), characterId, itemServerId,
        //     userServerId
        // )
        L2_LOG_ERROR(Log,
            "[inventory] Cannot use item %d for character %d: item was not found "
            "(consume type %d, requested quantity %lld, item object %d, character object %d). Failure response requested.",
            itemId, characterId, consumeType, static_cast<long long>(requestedAmount), itemServerId, userServerId);
        return false;
    }

    const std::int64_t originalAmount = item->Amount();
    if (runtime::SubtractInt64Wrapping(originalAmount, requestedAmount) < 0) {
        sendFailure();
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]Useitem::amount fail, "
        //     "[%d][%d][%lld][%d][%d][%d][%lld]",
        //     "WareHouse.cpp", 0x20ae, itemId, consumeType, static_cast<long long>(requestedAmount), characterId, itemServerId,
        //     userServerId, static_cast<long long>(originalAmount)
        // )
        L2_LOG_ERROR(Log,
            "[inventory] Cannot use item %d for character %d: current quantity %lld minus requested quantity %lld "
            "produces a negative remainder (consume type %d, item object %d, character object %d). Failure response requested.",
            itemId, characterId, static_cast<long long>(originalAmount), static_cast<long long>(requestedAmount),
            consumeType, itemServerId, userServerId);
        return false;
    }

    std::int64_t remainingAmount = 0;
    const CacheDConfiguration& config = GetCacheDConfiguration();
    if (config.writeCache) {
        m_unsavedItemsPending = true;
        remainingAmount = runtime::SubtractInt64Wrapping(originalAmount, requestedAmount);
        item->m_writeCachePending = true;
    } else {
        bool usedLateSaving = false;
        if (config.lateSaving) {
            const int itemType = static_cast<int>(item->ItemType());
            const bool selected = std::find(config.lateSavingItems.begin(), config.lateSavingItems.end(), itemType) !=
                config.lateSavingItems.end();
            if (selected && g_readFile.GetConsumeType(itemType) == 4) {
                m_lateSavingItemsPending = true;
                remainingAmount = runtime::SubtractInt64Wrapping(originalAmount, requestedAmount);
                item->m_lateSavingAmount = runtime::AddInt64Wrapping(item->m_lateSavingAmount, requestedAmount);
                if (static_cast<std::int64_t>(config.lateSavingAmount) <= item->m_lateSavingAmount) {
                    item->ChangeAmount(item->m_lateSavingAmount);
                    item->m_lateSavingAmount = 0;
                    m_lateSavingItemsPending = false;
                }
                usedLateSaving = true;
            }
        }

        if (!usedLateSaving) {
            DBConn connection(runtime::process::CurrentDatabaseId());
            connection.Bind(&remainingAmount);
            const std::int64_t amountChange = runtime::NegateInt64Wrapping(requestedAmount);
            const int zeroDelete = consumeType == 1;
            constexpr const wchar_t* statement = L"{Call lin_AmountChange (?, ?, ?, ?)}";
            if (!ExecuteWarehouseAmountChange(connection, statement, characterId, itemId, amountChange, zeroDelete) ||
                !connection.Fetch()) {
                sendFailure();
                // Log.Add(
                //     LOG_ERROR,
                //     "[%s][%d]Useitem::exec fail, "
                //     "[%d][%d][%lld][%d][%d][%d]",
                //     "WareHouse.cpp", 0x20f4, itemId, consumeType, static_cast<long long>(requestedAmount), characterId,
                //     itemServerId, userServerId
                // )
                L2_LOG_ERROR(Log,
                    "[inventory] Cannot obtain the database amount-change result for item %d, character %d: "
                    "execution or fetch failed (consume type %d, requested quantity %lld, item object %d, character object %d). "
                    "Failure response requested.",
                    itemId, characterId, consumeType, static_cast<long long>(requestedAmount), itemServerId, userServerId);
                return false;
            }
            if (remainingAmount < 0) {
                sendFailure();
                // Log.Add(
                //     LOG_ERROR,
                //     "[%s][%d]Useitem::nItemRemainAmount is below 0, "
                //     "[%d][%d][%lld][%d][%d][%d]",
                //     "WareHouse.cpp", 0x20fd, itemId, consumeType, static_cast<long long>(requestedAmount), characterId,
                //     itemServerId, userServerId
                // )
                L2_LOG_ERROR(Log,
                    "[inventory] Database amount change returned negative quantity %lld for item %d, character %d "
                    "(consume type %d, requested quantity %lld, item object %d, character object %d). Failure response requested.",
                    static_cast<long long>(remainingAmount), itemId, characterId, consumeType,
                    static_cast<long long>(requestedAmount), itemServerId, userServerId);
                return false;
            }
        }
    }

    item->SetAmount(remainingAmount);
    if (item->Amount() == 0 && consumeType != 1) {
        PopItem(static_cast<int>(item->ItemID()));
        if (!item->Delete(true, false)) {
            // Log.Add(
            //     LOG_ERROR, "[%s][%d] item delete fail[%u][%u][%u][%d]", "WareHouse.cpp", 0x210b, item->ItemID(),
            //     item->ItemType(), m_ownerId, static_cast<int>(requestedAmount)
            // )
            L2_LOG_ERROR(Log,
                "[inventory] Deletion failed after using item %u, class %u, for owner %u (requested quantity %lld). "
                "Warehouse map removal was already attempted; processing continues to the response.",
                item->ItemID(), item->ItemType(), m_ownerId, static_cast<long long>(requestedAmount));
        }
    } else if (item->Amount() < 0) {
        PopItem(static_cast<int>(item->ItemID()));
        // Log.Add(LOG_ERROR, "[%s][%d]error. Item amount is below zero.", "WareHouse.cpp", 0x2113)
        L2_LOG_ERROR(Log,
            "[inventory] Item %d for character %d has negative quantity %lld after use (requested quantity %lld). "
            "Warehouse map removal was attempted; processing continues to the response.",
            itemId, characterId, static_cast<long long>(item->Amount()), static_cast<long long>(requestedAmount));
    }

    if (remainingAmount != item->Amount()) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]error. Item amount is not equal."
        //     "[%d][%d][%lld][%d]",
        //     "WareHouse.cpp", 0x211a, itemId, consumeType, static_cast<long long>(requestedAmount), characterId
        // )
        L2_LOG_ERROR(Log,
            "[inventory] Quantity mismatch after using item %d for character %d: expected remaining %lld, cached %lld "
            "(consume type %d, requested quantity %lld). The response will use the expected remaining quantity.",
            itemId, characterId, static_cast<long long>(remainingAmount), static_cast<long long>(item->Amount()),
            consumeType, static_cast<long long>(requestedAmount));
        // Noise: the second mismatch record is merged into the contextual message above.
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]error. Item amount is not equal."
        //     "[%lld][%lld]",
        //     "WareHouse.cpp", 0x211c, static_cast<long long>(remainingAmount), static_cast<long long>(item->Amount())
        // )
    }

    socket->Send("cddQd", 16, userServerId, itemServerId, remainingAmount, responseFlag);
    return true;
}

void CWareHouse::SendList(CSocket* socket, unsigned int userSocketId) {
    L2_GUARD_FUNCTION();

    CPackageItemPacketManager packetManager;
    SendListPredicate predicate(packetManager);
    m_items.FOR_ALL(&predicate, L"WareHouse.cpp", 0x31c);

    CUserSP owner = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    if (owner) {
        owner->CharAudit(predicate.adenaSum, predicate.defaultPriceSum, predicate.itemSum);
    }

    CPacketHelperEx<20480> header;
    header.Assemble("d", userSocketId);
    packetManager.SendPacket_PackageItemData(socket, 2, header);
}

bool CWareHouse::SendItemList(CSocket* socket, int userSocketId) {
    L2_GUARD_FUNCTION();

    CPackageItemPacketManager packetManager;
    SendItemListPredicate predicate(packetManager);
    m_items.FOR_ALL(&predicate, L"WareHouse.cpp", 0x2cd4);

    CPacketHelperEx<20480> header;
    header.Assemble("ddd", static_cast<int>(m_ownerId), userSocketId, static_cast<int>(m_warehouseType));
    packetManager.SendPacket_PackageItemData(socket, 0x4f, header);
    return true;
}

bool CWareHouse::UpgradeSystem(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x005eaab0. The caller owns this warehouse's write
    // lock. Paired current L2Server::RequestUpgradeSystem fixes the complete
    // request and response streams used below.
    L2_GUARD_FUNCTION();

    int userDbId = 0;
    int sourceItemId = 0;
    int sourceItemServerId = 0;
    int materialCount = 0;
    const std::uint8_t* cursor =
        ::Disassemble(packet, "dddd", &userDbId, &sourceItemId, &sourceItemServerId, &materialCount);

    CPacketHelperEx<20480> response;
    CItemTransactionOrderMap transactionItems;
    int transactionOrder = 0;
    auto trackTransaction = [&](CItemSP item, TransactType rollbackMode) -> bool {
        if (!item)
            return false;
        item->SetTransation();
        CItemSP rollbackItem = item->Copy();
        if (!rollbackItem)
            return false;
        rollbackItem->SetTrasactMode(rollbackMode);
        L2_ADD_REF(item, ObjectRefType::FunctionLocal);
        L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
        transactionItems.emplace(++transactionOrder, CItemKey(rollbackItem.get(), item.get()));
        return true;
    };
    auto fail = [&]() {
        CTransaction::RollbackTransactOrderMap(transactionItems);
        socket->Send("chdd", 0xc6, 0x11c, userDbId, 0);
        return false;
    };

    CItemSP sourceItem = GetItem(sourceItemId);
    if (!sourceItem || !trackTransaction(sourceItem, TransactType_RollbackChanges)) {
        return fail();
    }
    sourceItem->TransactDelete();
    response.Assemble("dd", sourceItemServerId, materialCount);

    std::wstring consumedItems;
    auto appendConsumedItem = [&consumedItems](CItem& item, int itemType, std::int64_t amount) {
        consumedItems.append(std::to_wstring(item.ItemID()));
        consumedItems.push_back(L':');
        consumedItems.append(std::to_wstring(itemType));
        consumedItems.push_back(L':');
        // The target uses Win64's 32-bit unsigned long with %lu here.
        consumedItems.append(std::to_wstring(static_cast<std::uint32_t>(amount)));
        consumedItems.push_back(L':');
    };

    for (int index = 0; index < materialCount; ++index) {
        int materialItemType = 0;
        std::int64_t materialAmount = 0;
        cursor = ::Disassemble(cursor, "dQ", &materialItemType, &materialAmount);
        CItemSP materialItem = GetItemByType(materialItemType, 0);
        if (!materialItem || !trackTransaction(materialItem, TransactType_RollbackChanges)) {
            return fail();
        }

        materialItem->SetAmount(runtime::SubtractInt64Wrapping(materialItem->Amount(), materialAmount));
        if (materialItem->Amount() == 0) {
            materialItem->TransactDelete();
        } else if (materialItem->Amount() < 0) {
            return fail();
        }
        response.Assemble("dQ", materialItemType, static_cast<long long>(materialAmount));
        appendConsumedItem(*materialItem.get(), materialItemType, materialAmount);
    }

    int adenaItemType = 0;
    std::int64_t adenaAmount = 0;
    cursor = ::Disassemble(cursor, "dQ", &adenaItemType, &adenaAmount);
    CItemSP adenaItem = GetItemByType(adenaItemType, 0);
    if (!adenaItem || !trackTransaction(adenaItem, TransactType_RollbackChanges)) {
        return fail();
    }
    adenaItem->SetAmount(runtime::SubtractInt64Wrapping(adenaItem->Amount(), adenaAmount));
    if (adenaItem->Amount() == 0) {
        adenaItem->TransactDelete();
    } else if (adenaItem->Amount() < 0) {
        return fail();
    }
    response.Assemble("Q", static_cast<long long>(adenaAmount));
    appendConsumedItem(*adenaItem.get(), adenaItemType, adenaAmount);

    int resultItemType = 0;
    int resultEnchant = 0;
    cursor = ::Disassemble(cursor, "dd", &resultItemType, &resultEnchant);
    const ItemOptionKey emptyOptionKey{};
    const BaseAttribute emptyBaseAttribute{};
    CItemSP resultItem = MakeNewItem(
        static_cast<unsigned int>(userDbId), static_cast<unsigned int>(resultItemType), 1, resultEnchant, 0, 0, 0, 0,
        static_cast<int>(GetWarehouseNum()), emptyOptionKey, 0, emptyBaseAttribute, 0, 0
    );
    if (!resultItem || !trackTransaction(resultItem, TransactType_RollbackCreation)) {
        return fail();
    }

    response.Assemble("dd", static_cast<int>(resultItem->ItemID()), resultItemType);
    resultItem->SetEnchant(resultEnchant);
    response.Assemble("d", resultEnchant);

    resultItem->m_optionKey = sourceItem->m_optionKey;
    resultItem->m_intensiveItemType = sourceItem->m_intensiveItemType;
    response.Assemble("dd", sourceItem->m_optionKey.variationOption1, sourceItem->m_optionKey.variationOption2);

    resultItem->SetBaseAttribute(sourceItem->Attribute());
    CItem::AssembleAttribute(sourceItem->Attribute(), response);

    int appearanceItemType = 0;
    int appearancePeriod = 0;
    cursor = ::Disassemble(cursor, "dd", &appearanceItemType, &appearancePeriod);
    resultItem->SetApperanceType(appearanceItemType, appearancePeriod, static_cast<ShapeShiftingJobType>(0));
    response.Assemble("dd", sourceItem->AppearanceType(), appearancePeriod);

    resultItem->SetEnsoulInfo(const_cast<CItem::EnsoulInfo*>(sourceItem->EnsoulSlots()));
    CItem::AssembleEnsoulInfo(response, const_cast<CItem::EnsoulInfo*>(sourceItem->EnsoulSlots()));

    for (const auto& entry : transactionItems) {
        CItem* const rollbackItem = entry.second.rollbackItem;
        CItem* const changedItem = entry.second.item;
        changedItem->CommitTransation(rollbackItem, true);
        L2_RELEASE(changedItem, ObjectRefType::FunctionLocal);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
    }

    socket->Send("chddb", 0xc6, 0x11c, userDbId, 1, response.GetLength(), response.GetPacket());

    CUserSP owner = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    if (owner) {
        WriteUpgradeSystemAudits(
            *owner.get(), *resultItem.get(), sourceItemId, resultEnchant, resultItemType, consumedItems
        );
    }
    return true;
}

bool CWareHouse::CrystallizeItem(CSocket* socket, const std::uint8_t* packet, bool isPet) {
    // Current Cached.exe 0x005ec41c.
    L2_GUARD_FUNCTION();

    int sourceItemId = 0;
    int itemServerId = 0;
    int userServerId = 0;
    int generatedItemCount = 0;
    const std::uint8_t* cursor =
        ::Disassemble(packet, "dddd", &sourceItemId, &itemServerId, &userServerId, &generatedItemCount);

    CItemTransactionOrderMap transactionItems;
    std::vector<CrystallizeItemChange> changes;
    CItemSP sourceItem = GetItem(sourceItemId);
    if (!sourceItem) {
        socket->Send("cddQ", 0x50, userServerId, itemServerId, static_cast<long long>(-1));
        CTransaction::RollbackTransactOrderMap(transactionItems);
        return false;
    }

    sourceItem->SetTransation();
    CItemSP sourceRollback = sourceItem->Copy();
    sourceRollback->SetTrasactMode(TransactType_RollbackChanges);
    L2_ADD_REF(sourceItem, ObjectRefType::FunctionLocal);
    L2_ADD_REF(sourceRollback, ObjectRefType::FunctionLocal);
    int transactionOrder = 1;
    transactionItems.emplace(transactionOrder, CItemKey(sourceRollback.get(), sourceItem.get()));
    sourceItem->TransactDelete();

    const ItemOptionKey emptyOptionKey{};
    const BaseAttribute emptyBaseAttribute{};
    for (int index = 0; index < generatedItemCount; ++index) {
        int generatedItemType = 0;
        std::int64_t generatedAmount = 0;
        cursor = ::Disassemble(cursor, "dQ", &generatedItemType, &generatedAmount);
        if (generatedAmount < 1) {
            // Log.Add(
            //     LOG_ERROR, "CrystalItem is Zero ItemID[%d] UserID[%d]", generatedItemType, userServerId
            // )
            // L2_LOG_ERROR(Log, "CrystalItem is Zero ItemID[%d] UserID[%d]", generatedItemType, userServerId
            // )
            L2_LOG_ERROR(Log,
                "[warehouse] Skipping generated item class %d for owner %u, character object %d: quantity %lld is below one. "
                "Continuing with the next generated entry.",
                generatedItemType, m_ownerId, userServerId, static_cast<long long>(generatedAmount));
            continue;
        }

        int created = 0;
        CItemSP generatedItem = GetItemByType(generatedItemType, 0);
        if (!generatedItem) {
            generatedItem = MakeNewItem(
                sourceItem->OwnerID(), static_cast<unsigned int>(generatedItemType), generatedAmount, 0, 0, 0, 0, 0,
                static_cast<int>(GetWarehouseNum()), emptyOptionKey, 0, emptyBaseAttribute, 0, 0
            );
            if (!generatedItem)
                break;

            generatedItem->SetTransation();
            CItemSP generatedRollback = generatedItem->Copy();
            generatedRollback->SetTrasactMode(TransactType_RollbackCreation);
            L2_ADD_REF(generatedItem, ObjectRefType::FunctionLocal);
            L2_ADD_REF(generatedRollback, ObjectRefType::FunctionLocal);
            transactionItems.emplace(++transactionOrder, CItemKey(generatedRollback.get(), generatedItem.get()));
            created = 1;
        } else {
            generatedItem->SetTransation();
            CItemSP generatedRollback = generatedItem->Copy();
            generatedRollback->SetTrasactMode(TransactType_RollbackChanges);
            L2_ADD_REF(generatedItem, ObjectRefType::FunctionLocal);
            L2_ADD_REF(generatedRollback, ObjectRefType::FunctionLocal);
            transactionItems.emplace(++transactionOrder, CItemKey(generatedRollback.get(), generatedItem.get()));
            generatedItem->SetAmount(runtime::AddInt64Wrapping(generatedItem->Amount(), generatedAmount));
        }

        changes.emplace_back(
            CrystallizeItemChange{static_cast<int>(generatedItem->ItemID()), generatedItemType, generatedAmount,
                generatedItem->Amount(), created, 0}
        );
    }

    CUserSP owner = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    const CrystallizeAuditContext auditContext =
        MakeCrystallizeAuditContext(owner.get(), *sourceItem.get(), sourceItem->m_intensiveItemType);

    for (const auto& entry : transactionItems) {
        CItem* const rollbackItem = entry.second.rollbackItem;
        CItem* const changedItem = entry.second.item;
        if (!isPet) {
            changedItem->CommitTransation(rollbackItem, true);
        } else {
            CWareHouseSP rollbackWarehouse(this, ObjectRefType::FunctionChain);
            CWareHouseSP warehouse(this, ObjectRefType::FunctionChain);
            changedItem->CommitTransation(std::move(rollbackWarehouse), std::move(warehouse), rollbackItem, true);
        }
        L2_RELEASE(changedItem, ObjectRefType::FunctionLocal);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
    }

    std::array<char, 0x5000> responseItems{};
    int responseSize = 0;
    for (const CrystallizeItemChange& change : changes) {
        const int assembledSize = ::Assemble(
            responseItems.data() + responseSize, static_cast<int>(responseItems.size()) - responseSize, "ddQd",
            change.itemId, change.itemType, change.requestedAmount, change.created
        );
        if (assembledSize > 0) {
            responseSize += assembledSize;
        }
        WriteCrystallizeItemAudits(auditContext, change);
    }

    // The shipping packet keeps the request's original row count even when
    // invalid rows were skipped or MakeNewItem stopped the production loop.
    socket->Send(
        "cddQdb", 0x50, userServerId, itemServerId, static_cast<long long>(0), generatedItemCount, responseSize,
        responseItems.data()
    );
    return true;
}

bool CWareHouse::UseRecipeItem(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x005cf538. The caller owns the inventory write
    // lock. This owner performs the exact one-unit mutation and reply.
    L2_GUARD_FUNCTION();

    int itemId = 0;
    int userServerId = 0;
    ::Disassemble(packet, "dd", &itemId, &userServerId);

    CItemSP item = GetItem(itemId);
    if (!item) {
        socket->Send("cdd", 0xa7, userServerId, -1);
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]UseRecipeItem::Cannot find item, [%d][%d]", "WareHouse.cpp", 0x213b, itemId,
        //     userServerId
        // )
        L2_LOG_ERROR(Log,
            "[crafting] Cannot consume recipe item %d for owner %u (character object %d): item was not found. "
            "Failure response -1 requested.",
            itemId, m_ownerId, userServerId);
        return false;
    }

    const std::int64_t amount = item->Amount();
    if (amount <= 0) {
        socket->Send("cdd", 0xa7, userServerId, -1);
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]UseRecipeItem::Cannot delete recipe, [%d][%d]", "WareHouse.cpp", 0x2142, itemId,
        //     userServerId
        // )
        L2_LOG_ERROR(Log,
            "[crafting] Cannot consume recipe item %d for owner %u (character object %d): quantity %lld is not positive. "
            "Failure response -1 requested.",
            itemId, m_ownerId, userServerId, static_cast<long long>(amount));
        return false;
    }

    const std::int64_t remaining = runtime::SubtractInt64Wrapping(amount, 1);
    if (remaining == 0) {
        // The target releases the additional PopItem smart pointer before it
        // deletes through the original GetItem reference.
        PopItem(itemId);
        item->Delete(true, false);
    } else {
        // This branch is extra after a positive signed amount, but is
        // present in the shipping machine body and keeps its distinct log.
        if (remaining < 1) {
            socket->Send("cdd", 0xa7, userServerId, -1);
            // Log.Add(
            //     LOG_ERROR, "[%s][%d]UseRecipeItem::Cannot delete recipe, [%d][%d]", "WareHouse.cpp", 0x2158,
            //     itemId, userServerId
            // )
            L2_LOG_ERROR(Log,
                "[crafting] Cannot consume recipe item %d for owner %u (character object %d): "
                "quantity %lld minus one produced invalid remaining quantity %lld. Failure response -1 requested.",
                itemId, m_ownerId, userServerId, static_cast<long long>(amount), static_cast<long long>(remaining));
            return false;
        }
        item->SetAmount(remaining);
        item->Save(true);
    }

    socket->Send("cdd", 0xa7, userServerId, itemId);
    return true;
}

int CWareHouse::RecipeMakeItem(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0061b4e0. The four response values and the byte
    // flag are opaque request context owned by L2Server. Their wire order is
    // fixed by the paired current CDB::RecipeMakeItems/RecipeMakeItem bodies.
    L2_GUARD_FUNCTION();

    int responseValue1 = 0;
    int responseValue2 = 0;
    int responseValue3 = -1;
    char responseFlag = 0;
    int responseValue4 = 0;
    int itemCount = 0;
    const std::uint8_t* cursor = ::Disassemble(
        packet, "dddcdd", &responseValue1, &responseValue2, &responseValue3, &responseFlag, &responseValue4, &itemCount
    );

    CItemTransactionOrderMap transactionItems;
    std::map<CItem*, int> requestedItemIds;
    std::array<char, 0x5000> responseItems{};
    int responseSize = 0;
    int changedItemCount = 0;
    int transactionOrder = 0;

    auto rollbackAndReply = [&]() {
        CTransaction::RollbackTransactOrderMap(transactionItems);
        socket->Send("cdddddc", 0x70, responseValue1, responseValue2, responseValue3, responseValue4, 0, responseFlag);
        return 0;
    };

    auto beginTransaction = [&transactionItems, &transactionOrder](
                                const CItemSP& item, TransactType rollbackMode
                            ) {
        item->SetTransation();
        CItemSP rollbackItem = item->Copy();
        rollbackItem->SetTrasactMode(rollbackMode);
        L2_ADD_REF(item, ObjectRefType::FunctionLocal);
        L2_ADD_REF(rollbackItem, ObjectRefType::FunctionLocal);
        transactionOrder = runtime::AddInt32Wrapping(transactionOrder, 1);
        transactionItems.emplace(transactionOrder, CItemKey(rollbackItem.get(), item.get()));
        return rollbackItem;
    };

    std::uint32_t remainingItemCount = static_cast<std::uint32_t>(itemCount);
    if (remainingItemCount == 0) {
        return rollbackAndReply();
    }

    const ItemOptionKey emptyOptionKey{};
    const BaseAttribute emptyBaseAttribute{};
    do {
        --remainingItemCount;

        int itemType = 0;
        std::int64_t requestedAmount = 0;
        int consumeType = 0;
        int itemId = 0;
        int requestContext = 0;
        cursor = ::Disassemble(cursor, "dQddd", &itemType, &requestedAmount, &consumeType, &itemId, &requestContext);

        const int requestedItemId = itemId;
        CItemSP changedItem;
        CItemSP rollbackItem;
        ItemChangeScope changeScope = ItemChangeScope_Unknown;

        if (itemId != 0) {
            changedItem = GetItem(itemId);
            if (!changedItem) {
                // Log.Add(LOG_ERROR, "Cannot find dbid [%d], char[%u]", itemId, m_ownerId)
                // L2_LOG_ERROR(Log, "Cannot find dbid [%d], char[%u]", itemId, m_ownerId)
                L2_LOG_ERROR(Log,
                    "[crafting] Owner %u is missing requested item %d, class %d. Rolling back item changes and requesting a failure reply.",
                    m_ownerId, itemId, itemType);
                return rollbackAndReply();
            }

            rollbackItem = beginTransaction(changedItem, TransactType_RollbackChanges);

            if (consumeType == 2 || consumeType == 3) {
                if (requestedAmount > 0) {
                    changedItem->SetAmount(runtime::AddInt64Wrapping(changedItem->Amount(), requestedAmount));
                    changeScope = ItemChangeScope_Increased;
                } else if (requestedAmount < 0) {
                    const std::int64_t previousAmount = changedItem->Amount();
                    const std::int64_t resultAmount = runtime::AddInt64Wrapping(previousAmount, requestedAmount);
                    if (resultAmount > 0) {
                        changedItem->SetAmount(resultAmount);
                        changeScope = ItemChangeScope_Decreased;
                    } else if (resultAmount == 0) {
                        changedItem->SetAmount(0);
                        changedItem->TransactDelete();
                        itemId = static_cast<int>(changedItem->ItemID());
                        changeScope = ItemChangeScope_Deleted;
                    } else {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     "[%s][%d]request RecipeMakeItem item char[%u] "
                        //     "result amount[%lld][%lld][%lld] < 0",
                        //     "WareHouse.cpp", 0x33a3, m_ownerId, static_cast<long long>(previousAmount),
                        //     static_cast<long long>(requestedAmount), static_cast<long long>(resultAmount)
                        // )
                        // L2_LOG_ERROR(Log,
                            // "[%s][%d]request RecipeMakeItem item char[%u] "
                            // "result amount[%lld][%lld][%lld] < 0",
                            // "WareHouse.cpp", 0x33a3, m_ownerId, static_cast<long long>(previousAmount),
                            // static_cast<long long>(requestedAmount), static_cast<long long>(resultAmount)
                        // )
                        L2_LOG_ERROR(Log,
                            "[crafting] Owner %u, item %d, class %d: quantity %lld plus change %lld gives negative result %lld "
                            "(consume type %d, request context %d). Rolling back item changes and requesting a failure reply.",
                            m_ownerId, itemId, itemType, static_cast<long long>(previousAmount),
                            static_cast<long long>(requestedAmount), static_cast<long long>(resultAmount), consumeType, requestContext);
                        // Log.Add(
                        //     LOG_ERROR,
                        //     "request RecipeMakeItem item char[%u] "
                        //     "item[%d][%lld][%d][%d][%d]",
                        //     m_ownerId, itemType, static_cast<long long>(requestedAmount), consumeType, itemId, requestContext
                        // )
                        // Noise: the preceding diagnostic now includes all request fields from this duplicate record.
                        // L2_LOG_ERROR(Log,
                            // "request RecipeMakeItem item char[%u] "
                            // "item[%d][%lld][%d][%d][%d]",
                            // m_ownerId, itemType, static_cast<long long>(requestedAmount), consumeType, itemId, requestContext
                        // )
                        return rollbackAndReply();
                    }
                } else {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "Amount could not be acceptable. amount[%lld], "
                    //     "dbid [%d], char[%u], nItemConsumeType[%d]",
                    //     static_cast<long long>(requestedAmount), itemId, m_ownerId, consumeType
                    // )
                    // L2_LOG_ERROR(Log,
                        // "Amount could not be acceptable. amount[%lld], "
                        // "dbid [%d], char[%u], nItemConsumeType[%d]",
                        // static_cast<long long>(requestedAmount), itemId, m_ownerId, consumeType
                    // )
                    L2_LOG_ERROR(Log,
                        "[crafting] Owner %u, item %d, class %d, consume type %d requested quantity %lld: a quantity change of zero is not allowed. "
                        "Rolling back item changes and requesting a failure reply.",
                        m_ownerId, itemId, itemType, consumeType, static_cast<long long>(requestedAmount));
                    return rollbackAndReply();
                }
            } else {
                if (requestedAmount != -1) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "Amount could not be acceptable. amount[%lld], "
                    //     "dbid [%d], char[%u], nItemConsumeType[%d]",
                    //     static_cast<long long>(requestedAmount), itemId, m_ownerId, consumeType
                    // )
                    // L2_LOG_ERROR(Log,
                        // "Amount could not be acceptable. amount[%lld], "
                        // "dbid [%d], char[%u], nItemConsumeType[%d]",
                        // static_cast<long long>(requestedAmount), itemId, m_ownerId, consumeType
                    // )
                    L2_LOG_ERROR(Log,
                        "[crafting] Owner %u, item %d, class %d, consume type %d requested quantity %lld: this branch requires a quantity change of -1. "
                        "Rolling back item changes and requesting a failure reply.",
                        m_ownerId, itemId, itemType, consumeType, static_cast<long long>(requestedAmount));
                    return rollbackAndReply();
                }

                changedItem->SetAmount(runtime::AddInt64Wrapping(changedItem->Amount(), requestedAmount));
                changedItem->TransactDelete();
                itemId = static_cast<int>(changedItem->ItemID());
                changeScope = ItemChangeScope_Deleted;
            }
        } else if (consumeType == 2 || consumeType == 3) {
            if (requestedAmount < 1) {
                // Log.Add(
                //     LOG_ERROR,
                //     "Amount could not be acceptable. amount[%lld], "
                //     "dbid [%d], char[%u], nItemConsumeType[%d]",
                //     static_cast<long long>(requestedAmount), 0, m_ownerId, consumeType
                // )
                // L2_LOG_ERROR(Log,
                    // "Amount could not be acceptable. amount[%lld], "
                    // "dbid [%d], char[%u], nItemConsumeType[%d]",
                    // static_cast<long long>(requestedAmount), 0, m_ownerId, consumeType
                // )
                L2_LOG_ERROR(Log,
                    "[crafting] Owner %u, item %d, class %d, consume type %d requested quantity %lld: adding stackable items requires a positive quantity. "
                    "Rolling back item changes and requesting a failure reply.",
                    m_ownerId, 0, itemType, consumeType, static_cast<long long>(requestedAmount));
                return rollbackAndReply();
            }

            changedItem = GetItemByType(itemType, 0);
            if (changedItem && changedItem->Amount() > 0) {
                rollbackItem = beginTransaction(changedItem, TransactType_RollbackChanges);
                changedItem->SetAmount(runtime::AddInt64Wrapping(changedItem->Amount(), requestedAmount));
                itemId = static_cast<int>(changedItem->ItemID());
                changeScope = ItemChangeScope_Increased;
            } else {
                changedItem = MakeNewItem(
                    m_ownerId, static_cast<unsigned int>(itemType), 0, 0, 0, 0, 0, 0,
                    static_cast<int>(WareHouseType_Inventory), emptyOptionKey, 0, emptyBaseAttribute, 0, 0
                );
                if (!changedItem) {
                    return rollbackAndReply();
                }
                rollbackItem = beginTransaction(changedItem, TransactType_RollbackCreation);
                changedItem->SetAmount(requestedAmount);
                itemId = static_cast<int>(changedItem->ItemID());
                changeScope = ItemChangeScope_Added;
            }
        } else {
            if (requestedAmount != 1) {
                // Log.Add(
                //     LOG_ERROR,
                //     "Amount could not be acceptable. amount[%lld], "
                //     "dbid [%d], char[%u], nItemConsumeType[%d]",
                //     static_cast<long long>(requestedAmount), 0, m_ownerId, consumeType
                // )
                // L2_LOG_ERROR(Log,
                    // "Amount could not be acceptable. amount[%lld], "
                    // "dbid [%d], char[%u], nItemConsumeType[%d]",
                    // static_cast<long long>(requestedAmount), 0, m_ownerId, consumeType
                // )
                L2_LOG_ERROR(Log,
                    "[crafting] Owner %u, item %d, class %d, consume type %d requested quantity %lld: this creation branch requires quantity 1. "
                    "Rolling back item changes and requesting a failure reply.",
                    m_ownerId, 0, itemType, consumeType, static_cast<long long>(requestedAmount));
                return rollbackAndReply();
            }

            changedItem = MakeNewItem(
                m_ownerId, static_cast<unsigned int>(itemType), 0, 0, 0, 0, 0, 0, static_cast<int>(WareHouseType_Inventory),
                emptyOptionKey, 0, emptyBaseAttribute, 0, 0
            );
            if (!changedItem) {
                return rollbackAndReply();
            }
            rollbackItem = beginTransaction(changedItem, TransactType_RollbackCreation);
            changedItem->SetAmount(requestedAmount);
            itemId = static_cast<int>(changedItem->ItemID());
            changeScope = ItemChangeScope_Added;
        }

        changedItem->SetChangeScope(changeScope);
        const int assembledSize = ::Assemble(
            responseItems.data() + responseSize, static_cast<int>(responseItems.size()) - responseSize, "dddQQd",
            static_cast<int>(changeScope), itemType, itemId, static_cast<long long>(requestedAmount),
            static_cast<long long>(changedItem->Amount()), requestContext
        );
        if (assembledSize < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d]request RecipeMakeItem item char[%u] "
            //     "socket buffer overflow",
            //     "WareHouse.cpp", 0x343e, m_ownerId
            // )
            // L2_LOG_ERROR(Log,
                // "[%s][%d]request RecipeMakeItem item char[%u] "
                // "socket buffer overflow",
                // "WareHouse.cpp", 0x343e, m_ownerId
            // )
            L2_LOG_ERROR(Log,
                "[crafting] Cannot assemble reply entry for owner %u, item %d, class %d (result %d; %d of %zu bytes used). "
                "Rolling back item changes and requesting a failure reply.",
                m_ownerId, itemId, itemType, assembledSize, responseSize, responseItems.size());
            return rollbackAndReply();
        }

        ++changedItemCount;
        responseSize += assembledSize;
        requestedItemIds.emplace(changedItem.get(), requestedItemId);
    } while (remainingItemCount != 0);

    CUserSP owner = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    const WareHouseAuditActor actor = MakeWareHouseAuditActor(owner.get());

    for (const auto& entry : transactionItems) {
        CItem* const rollbackItem = entry.second.rollbackItem;
        CItem* const changedItem = entry.second.item;
        int requestedItemId = 0;
        const auto requestedId = requestedItemIds.find(changedItem);
        if (requestedId != requestedItemIds.end()) {
            requestedItemId = requestedId->second;
        }

        WriteRecipeMakeItemAudit(actor, *changedItem, *rollbackItem, requestedItemId, changedItem->m_intensiveItemType);

        const ItemChangeScope scope = changedItem->ChangeScope();
        if (scope == ItemChangeScope_Added || scope == ItemChangeScope_Increased) {
            CWatchItem* watchItem =
                g_WatchList.CheckWatchList(static_cast<WATCHTYPE>(2), static_cast<int>(changedItem->ItemType()));
            if (watchItem) {
                WriteManipulateWatchAudit(actor, *changedItem, *watchItem);
            }
        }

        changedItem->CommitTransation(rollbackItem, true);
        L2_RELEASE(rollbackItem, ObjectRefType::FunctionLocal);
        L2_RELEASE(changedItem, ObjectRefType::FunctionLocal);
    }

    socket->Send(
        "cdddddcb", 0x70, responseValue1, responseValue2, responseValue3, responseValue4, changedItemCount, responseFlag,
        responseSize, responseItems.data()
    );
    return 0;
}

int CWareHouse::RecipeShopMakeItem(CSocket* socket, const std::uint8_t* packet, CWareHouse* makerWarehouse) {
    // Current Cached.exe 0x00620e5c. Unlike RecipeMakeItem, the shipping
    // owner uses CTransaction directly and commits the customer changes and
    // the maker's Adena change as one ordered transaction.
    L2_GUARD_FUNCTION();

    int makerCharacterId = 0;
    int makerServerId = 0;
    int customerCharacterId = 0;
    int customerServerId = 0;
    int responseValue1 = 0;
    int responseValue2 = -1;
    char responseFlag = 0;
    std::int64_t fee = 0;
    int itemCount = 0;
    const std::uint8_t* cursor = ::Disassemble(
        packet, "ddddddcQd", &makerCharacterId, &makerServerId, &customerCharacterId, &customerServerId, &responseValue1,
        &responseValue2, &responseFlag, &fee, &itemCount
    );

    std::array<char, 0x5000> customerChanges{};
    std::array<char, 0x5000> makerChanges{};
    int customerChangeBytes = 0;
    int customerChangeCount = 0;

    CTransaction transaction;
    auto rollback = [&]() {
        transaction.SetRollback();
        return 0;
    };

    std::uint32_t remainingItemCount = static_cast<std::uint32_t>(itemCount);
    if (remainingItemCount == 0 || fee < 0) {
        return rollback();
    }

    const ItemOptionKey emptyOptionKey{};
    const BaseAttribute emptyBaseAttribute{};
    do {
        --remainingItemCount;

        int itemType = 0;
        std::int64_t requestedAmount = 0;
        int consumeType = 0;
        int itemId = 0;
        int requestContext = 0;
        cursor = ::Disassemble(cursor, "dQddd", &itemType, &requestedAmount, &consumeType, &itemId, &requestContext);

        CItemSP changedItem;
        ItemChangeScope changeScope = ItemChangeScope_Unknown;
        if (itemId == 0) {
            if (consumeType == 2 || consumeType == 3) {
                if (requestedAmount < 1) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "Amount could not be acceptable. amount[%lld], "
                    //     "dbid [%d], char[%u], nItemConsumeType[%d]",
                    //     static_cast<long long>(requestedAmount), 0, m_ownerId, consumeType
                    // )
                    // L2_LOG_ERROR(Log,
                        // "Amount could not be acceptable. amount[%lld], "
                        // "dbid [%d], char[%u], nItemConsumeType[%d]",
                        // static_cast<long long>(requestedAmount), 0, m_ownerId, consumeType
                    // )
                    L2_LOG_ERROR(Log,
                        "[crafting service] Owner %u, item %d, class %d, consume type %d requested quantity %lld: adding stackable items requires a positive quantity. "
                        "Requesting transaction rollback.",
                        m_ownerId, 0, itemType, consumeType, static_cast<long long>(requestedAmount));
                    return rollback();
                }

                changedItem = GetItemByType(itemType, 0);
                if (changedItem && changedItem->Amount() > 0) {
                    transaction.AddItem(changedItem);
                    changedItem->SetAmount(runtime::AddInt64Wrapping(changedItem->Amount(), requestedAmount));
                    itemId = static_cast<int>(changedItem->ItemID());
                    changeScope = ItemChangeScope_Increased;
                } else {
                    changedItem = MakeNewItem(
                        m_ownerId, static_cast<unsigned int>(itemType), 0, 0, 0, 0, 0, 0,
                        static_cast<int>(GetWarehouseNum()), emptyOptionKey, 0, emptyBaseAttribute, 0, 0
                    );
                    if (!changedItem) {
                        return rollback();
                    }
                    transaction.AddItemNew(changedItem);
                    changedItem->SetAmount(requestedAmount);
                    itemId = static_cast<int>(changedItem->ItemID());
                    changeScope = ItemChangeScope_Added;
                }
            } else {
                if (requestedAmount != 1) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "Amount could not be acceptable. amount[%lld], "
                    //     "dbid [%d], char[%u], nItemConsumeType[%d]",
                    //     static_cast<long long>(requestedAmount), 0, m_ownerId, consumeType
                    // )
                    // L2_LOG_ERROR(Log,
                        // "Amount could not be acceptable. amount[%lld], "
                        // "dbid [%d], char[%u], nItemConsumeType[%d]",
                        // static_cast<long long>(requestedAmount), 0, m_ownerId, consumeType
                    // )
                    L2_LOG_ERROR(Log,
                        "[crafting service] Owner %u, item %d, class %d, consume type %d requested quantity %lld: this creation branch requires quantity 1. "
                        "Requesting transaction rollback.",
                        m_ownerId, 0, itemType, consumeType, static_cast<long long>(requestedAmount));
                    return rollback();
                }

                changedItem = MakeNewItem(
                    m_ownerId, static_cast<unsigned int>(itemType), 0, 0, 0, 0, 0, 0, static_cast<int>(GetWarehouseNum()),
                    emptyOptionKey, 0, emptyBaseAttribute, 0, 0
                );
                if (!changedItem) {
                    return rollback();
                }
                transaction.AddItemNew(changedItem);
                changedItem->SetAmount(requestedAmount);
                itemId = static_cast<int>(changedItem->ItemID());
                changeScope = ItemChangeScope_Added;
            }
        } else {
            changedItem = GetItem(itemId);
            if (!changedItem) {
                // Log.Add(LOG_ERROR, "Cannot find dbid [%d], char[%u]", itemId, m_ownerId)
                // L2_LOG_ERROR(Log, "Cannot find dbid [%d], char[%u]", itemId, m_ownerId)
                L2_LOG_ERROR(Log,
                    "[crafting service] Owner %u is missing requested item %d, class %d. Requesting transaction rollback.",
                    m_ownerId, itemId, itemType);
                return rollback();
            }
            transaction.AddItem(changedItem);

            if (consumeType == 2 || consumeType == 3) {
                if (requestedAmount > 0) {
                    changedItem->SetAmount(runtime::AddInt64Wrapping(changedItem->Amount(), requestedAmount));
                    changeScope = ItemChangeScope_Increased;
                } else if (requestedAmount < 0) {
                    const std::int64_t previousAmount = changedItem->Amount();
                    const std::int64_t resultAmount = runtime::AddInt64Wrapping(previousAmount, requestedAmount);
                    if (resultAmount > 0) {
                        changedItem->SetAmount(resultAmount);
                        changeScope = ItemChangeScope_Decreased;
                    } else if (resultAmount == 0) {
                        changedItem->SetAmount(0);
                        changedItem->TransactDelete();
                        itemId = static_cast<int>(changedItem->ItemID());
                        changeScope = ItemChangeScope_Deleted;
                    } else {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     "[%s][%d]request RecipeMakeItem item char[%u] "
                        //     "result amount[%lld][%lld][%lld] < 0",
                        //     "WareHouse.cpp", 0x3daa, m_ownerId, static_cast<long long>(previousAmount),
                        //     static_cast<long long>(requestedAmount), static_cast<long long>(resultAmount)
                        // )
                        // L2_LOG_ERROR(Log,
                            // "[%s][%d]request RecipeMakeItem item char[%u] "
                            // "result amount[%lld][%lld][%lld] < 0",
                            // "WareHouse.cpp", 0x3daa, m_ownerId, static_cast<long long>(previousAmount),
                            // static_cast<long long>(requestedAmount), static_cast<long long>(resultAmount)
                        // )
                        L2_LOG_ERROR(Log,
                            "[crafting service] Owner %u, item %d, class %d: quantity %lld plus change %lld gives negative result %lld "
                            "(consume type %d, request context %d). Requesting transaction rollback.",
                            m_ownerId, itemId, itemType, static_cast<long long>(previousAmount),
                            static_cast<long long>(requestedAmount), static_cast<long long>(resultAmount), consumeType, requestContext);
                        // Log.Add(
                        //     LOG_ERROR,
                        //     "request RecipeMakeItem item char[%u] "
                        //     "item[%d][%lld][%d][%d][%d]",
                        //     m_ownerId, itemType, static_cast<long long>(requestedAmount), consumeType, itemId, requestContext
                        // )
                        // Noise: all fields are included in the preceding negative-result diagnostic.
                        // L2_LOG_ERROR(Log,
                            // "request RecipeMakeItem item char[%u] "
                            // "item[%d][%lld][%d][%d][%d]",
                            // m_ownerId, itemType, static_cast<long long>(requestedAmount), consumeType, itemId, requestContext
                        // )
                        return rollback();
                    }
                } else {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "Amount could not be acceptable. amount[%lld], "
                    //     "dbid [%d], char[%u], nItemConsumeType[%d]",
                    //     static_cast<long long>(requestedAmount), itemId, m_ownerId, consumeType
                    // )
                    // L2_LOG_ERROR(Log,
                        // "Amount could not be acceptable. amount[%lld], "
                        // "dbid [%d], char[%u], nItemConsumeType[%d]",
                        // static_cast<long long>(requestedAmount), itemId, m_ownerId, consumeType
                    // )
                    L2_LOG_ERROR(Log,
                        "[crafting service] Owner %u, item %d, class %d, consume type %d requested quantity %lld: a quantity change of zero is not allowed. "
                        "Requesting transaction rollback.",
                        m_ownerId, itemId, itemType, consumeType, static_cast<long long>(requestedAmount));
                    return rollback();
                }
            } else {
                if (requestedAmount != -1) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "Amount could not be acceptable. amount[%lld], "
                    //     "dbid [%d], char[%u], nItemConsumeType[%d]",
                    //     static_cast<long long>(requestedAmount), itemId, m_ownerId, consumeType
                    // )
                    // L2_LOG_ERROR(Log,
                        // "Amount could not be acceptable. amount[%lld], "
                        // "dbid [%d], char[%u], nItemConsumeType[%d]",
                        // static_cast<long long>(requestedAmount), itemId, m_ownerId, consumeType
                    // )
                    L2_LOG_ERROR(Log,
                        "[crafting service] Owner %u, item %d, class %d, consume type %d requested quantity %lld: this branch requires a quantity change of -1. "
                        "Requesting transaction rollback.",
                        m_ownerId, itemId, itemType, consumeType, static_cast<long long>(requestedAmount));
                    return rollback();
                }

                changedItem->SetAmount(runtime::AddInt64Wrapping(changedItem->Amount(), requestedAmount));
                changedItem->TransactDelete();
                itemId = static_cast<int>(changedItem->ItemID());
                changeScope = ItemChangeScope_Deleted;
            }
        }

        changedItem->SetChangeScope(changeScope);
        const int assembledSize = ::Assemble(
            customerChanges.data() + customerChangeBytes, static_cast<int>(customerChanges.size()) - customerChangeBytes,
            "dddQQd", static_cast<int>(changeScope), itemType, itemId, static_cast<long long>(requestedAmount),
            static_cast<long long>(changedItem->Amount()), requestContext
        );
        if (assembledSize < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d]request RecipeMakeItem item char[%u] "
            //     "socket buffer overflow",
            //     "WareHouse.cpp", 0x3e37, m_ownerId
            // )
            // L2_LOG_ERROR(Log,
                // "[%s][%d]request RecipeMakeItem item char[%u] "
                // "socket buffer overflow",
                // "WareHouse.cpp", 0x3e37, m_ownerId
            // )
            L2_LOG_ERROR(Log,
                "[crafting] Cannot assemble customer item %d, class %d for owner %u (result %d; %d of %zu bytes used). "
                "Requesting transaction rollback.",
                itemId, itemType, m_ownerId, assembledSize, customerChangeBytes, customerChanges.size());
            return rollback();
        }
        ++customerChangeCount;
        customerChangeBytes += assembledSize;
    } while (remainingItemCount != 0);

    CItemSP customerAdena = GetAdena();
    if (!customerAdena || customerAdena->Amount() < fee) {
        return rollback();
    }
    transaction.AddItem(customerAdena);

    CItemSP makerAdena = makerWarehouse->GetAdena();
    ItemChangeScope makerAdenaScope;
    if (!makerAdena) {
        makerAdena = makerWarehouse->MakeNewItem(
            makerWarehouse->m_ownerId, customerAdena->ItemType(), 0, 0, 0, 0, 0, 0,
            static_cast<int>(makerWarehouse->GetWarehouseNum()), emptyOptionKey, 0, emptyBaseAttribute, 0, 0
        );
        if (!makerAdena) {
            return rollback();
        }
        transaction.AddItemNew(makerAdena);
        makerAdenaScope = ItemChangeScope_Added;
    } else {
        transaction.AddItem(makerAdena);
        makerAdenaScope = ItemChangeScope_Increased;
    }
    makerAdena->SetChangeScope(makerAdenaScope);

    customerAdena->SetAmount(runtime::SubtractInt64Wrapping(customerAdena->Amount(), fee));
    ItemChangeScope customerAdenaScope;
    if (customerAdena->Amount() == 0) {
        customerAdena->TransactDelete();
        customerAdenaScope = ItemChangeScope_Deleted;
    } else {
        customerAdenaScope = ItemChangeScope_Decreased;
    }
    customerAdena->SetChangeScope(customerAdenaScope);

    makerAdena->SetAmount(runtime::AddInt64Wrapping(makerAdena->Amount(), fee));

    int assembledSize = ::Assemble(
        customerChanges.data() + customerChangeBytes, static_cast<int>(customerChanges.size()) - customerChangeBytes,
        "dddQQd", static_cast<int>(customerAdenaScope), static_cast<int>(customerAdena->ItemType()),
        static_cast<int>(customerAdena->ItemID()), static_cast<long long>(runtime::NegateInt64Wrapping(fee)),
        static_cast<long long>(customerAdena->Amount()), 0
    );
    if (assembledSize < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]request RecipeMakeItem item char[%u] "
        //     "socket buffer overflow",
        //     "WareHouse.cpp", 0x3e7f, m_ownerId
        // )
        // L2_LOG_ERROR(Log,
            // "[%s][%d]request RecipeMakeItem item char[%u] "
            // "socket buffer overflow",
            // "WareHouse.cpp", 0x3e7f, m_ownerId
        // )
        L2_LOG_ERROR(Log,
            "[crafting] Cannot assemble the customer's fee deduction for owner %u (result %d; %d of %zu bytes used). "
            "Requesting transaction rollback.",
            m_ownerId, assembledSize, customerChangeBytes, customerChanges.size());
        return rollback();
    }
    ++customerChangeCount;
    customerChangeBytes += assembledSize;

    const int makerChangeBytes = ::Assemble(
        makerChanges.data(), static_cast<int>(makerChanges.size()), "dddQQd", static_cast<int>(makerAdenaScope),
        static_cast<int>(makerAdena->ItemType()), static_cast<int>(makerAdena->ItemID()), static_cast<long long>(fee),
        static_cast<long long>(makerAdena->Amount()), 0
    );
    if (makerChangeBytes < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]request RecipeMakeItem item char[%u] "
        //     "socket buffer overflow",
        //     "WareHouse.cpp", 0x3e8f, m_ownerId
        // )
        // L2_LOG_ERROR(Log,
            // "[%s][%d]request RecipeMakeItem item char[%u] "
            // "socket buffer overflow",
            // "WareHouse.cpp", 0x3e8f, m_ownerId
        // )
        L2_LOG_ERROR(Log,
            "[crafting] Cannot assemble the maker's fee credit for character %d (result %d; capacity %zu bytes). "
            "Requesting transaction rollback.",
            makerCharacterId, makerChangeBytes, makerChanges.size());
        return rollback();
    }

    transaction.SetCommit();
    socket->Send(
        "cdddddcbdb", 0xab, makerServerId, customerServerId, responseValue1, responseValue2, customerChangeCount,
        responseFlag, customerChangeBytes, customerChanges.data(), 1, makerChangeBytes, makerChanges.data()
    );
    return 1;
}

bool CWareHouse::EquipHenna(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x006221ec. The target commits the reagent, Adena,
    // and CUser Henna mutation together through CTransaction's destructor.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int userServerId = 0;
    int hennaId = 0;
    int reagentItemId = 0;
    std::int64_t reagentAmount = 0;
    std::int64_t adenaFee = 0;
    int unusedRequestValue = 0;
    const std::uint8_t* cursor = ::Disassemble(
        packet, "ddddQQd", &characterId, &userServerId, &hennaId, &reagentItemId, &reagentAmount, &adenaFee,
        &unusedRequestValue
    );

    if (hennaId < 1) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]invalid henna id, [%d][%d]", "WareHouse.cpp", 0x3fd3, characterId, hennaId
        // )
        L2_LOG_ERROR(Log,
            "[henna] Cannot equip henna %d for character %d: henna ID must be positive. Request processing stopped.",
            hennaId, characterId);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, [%d]", "WareHouse.cpp", 0x3fda, characterId)
        L2_LOG_ERROR(Log,
            "[henna] Cannot equip henna %d for character %d: character lookup failed. Request processing stopped.",
            hennaId, characterId);
        return false;
    }

    CTransaction transaction;
    CPacketHelperEx<20480> changedItems;

    if (reagentItemId < 1) {
        transaction.SetRollback();
        return false;
    }

    CItemSP reagent = GetItem(reagentItemId);
    if (!reagent) {
        transaction.SetRollback();
        return false;
    }
    transaction.AddItem(reagent);

    if (reagentAmount < 1) {
        transaction.SetRollback();
        return false;
    }

    const std::int64_t reagentDelta = runtime::NegateInt64Wrapping(reagentAmount);
    const std::int64_t reagentRemaining = runtime::AddInt64Wrapping(reagent->Amount(), reagentDelta);
    ItemChangeScope reagentScope = ItemChangeScope_Decreased;
    if (reagentRemaining < 1) {
        if (reagentRemaining != 0) {
            transaction.SetRollback();
            return false;
        }
        reagent->SetAmount(reagentRemaining);
        reagent->TransactDelete();
        reagentScope = ItemChangeScope_Deleted;
    } else {
        reagent->SetAmount(reagentRemaining);
    }
    reagent->SetChangeScope(reagentScope);
    changedItems.Assemble(
        "dddQQd", static_cast<int>(reagentScope), static_cast<int>(reagent->ItemType()), reagentItemId,
        static_cast<long long>(reagentDelta), static_cast<long long>(reagent->Amount()), 0
    );

    if (adenaFee < 0) {
        transaction.SetRollback();
        return false;
    }

    CItemSP adena;
    if (adenaFee == 0) {
        changedItems.Assemble(
            "dddQQd", static_cast<int>(ItemChangeScope_Unknown), 0, 0, static_cast<long long>(0), static_cast<long long>(0),
            0
        );
    } else {
        adena = GetAdena();
        if (!adena) {
            transaction.SetRollback();
            return false;
        }
        transaction.AddItem(adena);

        const std::int64_t adenaDelta = runtime::NegateInt64Wrapping(adenaFee);
        const std::int64_t adenaRemaining = runtime::AddInt64Wrapping(adena->Amount(), adenaDelta);
        ItemChangeScope adenaScope = ItemChangeScope_Decreased;
        if (adenaRemaining < 1) {
            if (adenaRemaining != 0) {
                transaction.SetRollback();
                return false;
            }
            adena->SetAmount(adenaRemaining);
            adena->TransactDelete();
            adenaScope = ItemChangeScope_Deleted;
        } else {
            adena->SetAmount(adenaRemaining);
        }
        adena->SetChangeScope(adenaScope);
        changedItems.Assemble(
            "dddQQd", static_cast<int>(adenaScope), static_cast<int>(adena->ItemType()), static_cast<int>(adena->ItemID()),
            static_cast<long long>(adenaDelta), static_cast<long long>(adena->Amount()), 0
        );
    }

    int premium = 0;
    int periodMinutes = -1;
    ::Disassemble(cursor, "dd", &premium, &periodMinutes);

    int expirationTime = 0;
    bool equipped = false;
    if (premium == 1) {
        const std::int32_t startTime = runtime::Int32FromBits(static_cast<std::uint32_t>(std::time(nullptr)));
        equipped = user->AddHennaPremium(hennaId, startTime, periodMinutes);
        if (equipped) {
            expirationTime = runtime::AddInt32Wrapping(startTime, runtime::MultiplyInt32Wrapping(periodMinutes, 60));
        }
    } else {
        equipped = user->AddHenna(hennaId);
    }

    if (!equipped) {
        transaction.SetRollback();
        return false;
    }

    socket->Send(
        "cdddbdd", 0xac, userServerId, hennaId, changedItems.GetAssembledCount(), changedItems.GetLength(),
        changedItems.GetPacket(), premium, expirationTime
    );
    return true;
}

bool CWareHouse::UnequipHenna(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00622b80. The two result rows are assembled into
    // the target's raw 0x5000 buffer and counted only when Assemble succeeds.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int userServerId = 0;
    int hennaId = 0;
    int refundItemType = 0;
    std::int64_t refundAmount = 0;
    std::int64_t adenaFee = 0;
    const std::uint8_t* cursor =
        ::Disassemble(packet, "ddddQQ", &characterId, &userServerId, &hennaId, &refundItemType, &refundAmount, &adenaFee);

    if (hennaId < 1) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]invalid henna id, [%d][%d]", "WareHouse.cpp", 0x40ba, characterId, hennaId
        // )
        L2_LOG_ERROR(Log,
            "[henna] Cannot unequip henna %d for character %d: henna ID must be positive. Request processing stopped.",
            hennaId, characterId);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, [%d]", "WareHouse.cpp", 0x40c1, characterId)
        L2_LOG_ERROR(Log,
            "[henna] Cannot unequip henna %d for character %d: character lookup failed. Request processing stopped.",
            hennaId, characterId);
        return false;
    }

    const bool hasHenna = std::find(user->m_henna.begin(), user->m_henna.end(), hennaId) != user->m_henna.end() ||
        user->m_hennaPremiumId == hennaId;
    if (!hasHenna)
        return false;

    CTransaction transaction;
    std::array<char, 0x5000> changedItems{};
    int changedItemBytes = 0;
    int changedItemCount = 0;
    int firstChangedItemType = 0;

    if (refundAmount < 0) {
        transaction.SetRollback();
        return false;
    }

    CItemSP refundItem;
    if (refundAmount == 0) {
        const int written = ::Assemble(
            changedItems.data(), static_cast<int>(changedItems.size()), "dddQQd", static_cast<int>(ItemChangeScope_Unknown),
            0, 0, static_cast<long long>(0), static_cast<long long>(0), 0
        );
        if (written > 0) {
            changedItemBytes = written;
            changedItemCount = 1;
        }
    } else {
        if (refundItemType < 1) {
            transaction.SetRollback();
            return false;
        }

        refundItem = GetItemByType(refundItemType, 0);
        ItemChangeScope refundScope = ItemChangeScope_Increased;
        if (!refundItem) {
            const ItemOptionKey emptyOptionKey{};
            const BaseAttribute emptyBaseAttribute{};
            refundItem = MakeNewItem(
                m_ownerId, static_cast<unsigned int>(refundItemType), 0, 0, 0, 0, 0, 0,
                static_cast<int>(WareHouseType_Inventory), emptyOptionKey, 0, emptyBaseAttribute, 0, 0
            );
            if (!refundItem) {
                transaction.SetRollback();
                return false;
            }
            transaction.AddItemNew(refundItem);
            refundItem->SetAmount(refundAmount);
            refundScope = ItemChangeScope_Added;
        } else {
            transaction.AddItem(refundItem);
            const std::int64_t resultingAmount = runtime::AddInt64Wrapping(refundItem->Amount(), refundAmount);
            if (resultingAmount < 1) {
                transaction.SetRollback();
                return false;
            }
            refundItem->SetAmount(resultingAmount);
        }

        refundItem->SetChangeScope(refundScope);
        firstChangedItemType = static_cast<int>(refundItem->ItemType());
        const int written = ::Assemble(
            changedItems.data(), static_cast<int>(changedItems.size()), "dddQQd", static_cast<int>(refundScope),
            firstChangedItemType, static_cast<int>(refundItem->ItemID()), static_cast<long long>(refundAmount),
            static_cast<long long>(refundItem->Amount()), 0
        );
        if (written > 0) {
            changedItemBytes = written;
            changedItemCount = 1;
        }
    }

    if (adenaFee < 0) {
        transaction.SetRollback();
        return false;
    }

    CItemSP adena;
    if (adenaFee == 0) {
        const int written = ::Assemble(
            changedItems.data() + changedItemBytes, static_cast<int>(changedItems.size()) - changedItemBytes, "dddQQd",
            static_cast<int>(ItemChangeScope_Unknown), firstChangedItemType, 0, static_cast<long long>(0),
            static_cast<long long>(0), 0
        );
        if (written > 0) {
            changedItemBytes += written;
            ++changedItemCount;
        }
    } else {
        adena = GetAdena();
        if (!adena) {
            transaction.SetRollback();
            return false;
        }
        transaction.AddItem(adena);

        const std::int64_t adenaDelta = runtime::NegateInt64Wrapping(adenaFee);
        const std::int64_t adenaRemaining = runtime::AddInt64Wrapping(adena->Amount(), adenaDelta);
        ItemChangeScope adenaScope = ItemChangeScope_Decreased;
        if (adenaRemaining < 1) {
            if (adenaRemaining != 0) {
                transaction.SetRollback();
                return false;
            }
            adena->SetAmount(adenaRemaining);
            adena->TransactDelete();
            adenaScope = ItemChangeScope_Deleted;
        } else {
            adena->SetAmount(adenaRemaining);
        }
        adena->SetChangeScope(adenaScope);

        const int written = ::Assemble(
            changedItems.data() + changedItemBytes, static_cast<int>(changedItems.size()) - changedItemBytes, "dddQQd",
            static_cast<int>(adenaScope), static_cast<int>(adena->ItemType()), static_cast<int>(adena->ItemID()),
            static_cast<long long>(adenaDelta), static_cast<long long>(adena->Amount()), 0
        );
        if (written > 0) {
            changedItemBytes += written;
            ++changedItemCount;
        }
    }

    int premium = 0;
    ::Disassemble(cursor, "d", &premium);
    const bool unequipped = premium == 1 ? user->DelHennaPremium(hennaId) : user->DelHenna(hennaId);
    if (!unequipped) {
        transaction.SetRollback();
        return false;
    }

    socket->Send("cdddbd", 0xad, userServerId, hennaId, changedItemCount, changedItemBytes, changedItems.data(), premium);
    return true;
}

void CWareHouse::AddProcuredCrop(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x006236dc. The paired current L2Server
    // CManor::TransferManorData at 0x00821d7c fixes all root
    // identities and the repeated `dd` itemType/amount rows.
    L2_GUARD_FUNCTION();

    int manorId = 0;
    int residenceId = 0;
    int pledgeId = 0;
    int itemCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "dddd", &manorId, &residenceId, &pledgeId, &itemCount);

    if (itemCount < 1 || itemCount > 200) {
        // Log.Add(LOG_ERROR, "[%s][%d]AddProcuredCrop error, count[%d]", "WareHouse.cpp", 0x41c4, itemCount)
        L2_LOG_ERROR(Log,
            "[manor] Cannot add procured crops to owner %u (manor %d, residence %d, clan %d): "
            "item count %d is outside 1..200. Request processing stopped before item changes.",
            m_ownerId, manorId, residenceId, pledgeId, itemCount);
        return;
    }

    CTransaction transaction;
    const ItemOptionKey emptyOptionKey{};
    const BaseAttribute emptyBaseAttribute{};
    for (int index = 0; index < itemCount; ++index) {
        int itemType = 0;
        int amount = 0;
        cursor = ::Disassemble(cursor, "dd", &itemType, &amount);

        int errorLine = 0;
        if (itemType < 1) {
            errorLine = 0x41d4;
        } else if (amount < 1) {
            errorLine = 0x41da;
        }
        if (errorLine != 0) {
            // The shipping body skips only this row. It neither aborts nor
            // rolls back otherwise-valid rows in the same request.
            // Log.Add(
            //     LOG_ERROR, "[%s][%d]AddProcuredCrop error, [%d][%d][%d][%d]", "WareHouse.cpp", errorLine, manorId,
            //     residenceId, pledgeId, itemType
            // )
            L2_LOG_ERROR(Log,
                "[manor] Skipping procured crop entry %d of %d for owner %u (manor %d, residence %d, clan %d): "
                "item class %d, quantity %d; %s must be positive. Other entries continue processing.",
                index + 1, itemCount, m_ownerId, manorId, residenceId, pledgeId, itemType, amount,
                itemType < 1 ? "item class" : "quantity");
            continue;
        }

        CItemSP changedItem = GetItemByType(itemType, 0);
        if (!changedItem) {
            changedItem = MakeNewItem(
                m_ownerId, static_cast<unsigned int>(itemType), amount, 0, 0, 0, 0, 0, static_cast<int>(GetWarehouseNum()),
                emptyOptionKey, 0, emptyBaseAttribute, 0, 0
            );
            if (!changedItem) {
                // Log.Add(
                //     LOG_ERROR, "[%s][%d]Cannot make new item. [%u]-[%d][%d]", "WareHouse.cpp", 0x41e4, m_ownerId,
                //     itemType, amount
                // )
                L2_LOG_ERROR(Log,
                    "[manor] Cannot create procured crop entry %d of %d for owner %u (manor %d, residence %d, clan %d, "
                    "item class %d, quantity %d). Requesting transaction rollback and stopping request processing.",
                    index + 1, itemCount, m_ownerId, manorId, residenceId, pledgeId, itemType, amount);
                transaction.SetRollback();
                return;
            }
            transaction.AddItemNew(changedItem);
        } else {
            transaction.AddItem(changedItem);
            changedItem->SetAmount(runtime::AddInt64Wrapping(changedItem->Amount(), amount));
        }

        CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
        if (changedItem && pledge) {
            std::array<char16_t, 24> pledgeName{};
            std::copy_n(pledge->GetName(), pledgeName.size(), pledgeName.begin());
            CLogFormatter formatter;
            formatter.WriteLog(
                0x106, 0, 0, 0, 0, 0, 0, 0, pledgeName.data(), nullptr, nullptr, 0, 0, 0, 0, manorId, pledgeId, itemType,
                amount, 0, 0, nullptr, nullptr, nullptr, nullptr, changedItem->ItemID()
            );
        }
    }
}

bool CWareHouse::BuySeed(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00623d08. The target re-decodes the root packet
    // here and commits the item mutations from CTransaction's destructor,
    // after sending the reply.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int userServerId = 0;
    int manorId = 0;
    int residenceId = 0;
    std::int64_t totalPrice = 0;
    int itemCount = 0;
    const std::uint8_t* cursor =
        ::Disassemble(packet, "ddddQd", &characterId, &userServerId, &manorId, &residenceId, &totalPrice, &itemCount);

    auto sendFailure = [&](int result) {
        socket->Send(
            "cdddddQd", 0xb1, result, characterId, userServerId, manorId, residenceId, static_cast<long long>(totalPrice),
            itemCount
        );
        return false;
    };

    if (itemCount < 1) {
        // Log.Add(LOG_ERROR, "[%s][%d]BuySeed count is below 0, [%d]", "WareHouse.cpp", 0x4239, itemCount)
        L2_LOG_ERROR(Log,
            "[manor] Cannot buy seeds for owner %u in manor %d: item count %d is below 1. Requesting failure response 5.",
            m_ownerId, manorId, itemCount);
        return sendFailure(5);
    }

    CItemSP adena = GetAdena();
    if (!adena || adena->Amount() < totalPrice) {
        return sendFailure(6);
    }

    CManorSP manor = g_manorDb.GetManor(manorId);
    if (!manor) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find manor, id[%d]", "WareHouse.cpp", 0x424d, manorId)
        L2_LOG_ERROR(Log,
            "[manor] Cannot buy seeds for owner %u: manor %d was not found. Requesting failure response 2.",
            m_ownerId, manorId);
        return sendFailure(2);
    }

    CTransaction transaction;
    std::array<char, 0x5000> changedItems{};
    int changedItemBytes = 0;
    int changedItemCount = 0;
    CItemSP changedItem;
    int changedItemId = -1;
    ItemChangeScope changedItemScope = ItemChangeScope_Unknown;

    auto rollbackAndFail = [&](int result) {
        transaction.SetRollback();
        return sendFailure(result);
    };

    manor->ResetManorSeedToChangeData();
    const ItemOptionKey emptyOptionKey{};
    const BaseAttribute emptyBaseAttribute{};
    for (int itemIndex = 0; itemIndex < itemCount; ++itemIndex) {
        int itemType = 0;
        std::int64_t amount = 0;
        int consumeType = 0;
        cursor = ::Disassemble(cursor, "dQd", &itemType, &amount, &consumeType);

        if (!manor->SetManorSeedToChangeData(itemType, amount)) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d]request sell seed item char[%u]. "
            //     "make item fail.",
            //     "WareHouse.cpp", 0x4265, m_ownerId
            // )
            L2_LOG_ERROR(Log,
                "[manor] Cannot reserve %lld seeds of item class %d in manor %d for owner %u: "
                "the seed is not listed or the remaining stock after pending changes is insufficient. "
                "Requesting transaction rollback and failure response 9.",
                static_cast<long long>(amount), itemType, manorId, m_ownerId);
            return rollbackAndFail(9);
        }

        // The shipping sender supplies only stackable/asset consume types.
        // Current 0x00623d08 has no fallback branch and keeps
        // the prior smart pointer/change fields for any other value.
        if (consumeType == 2 || consumeType == 3) {
            changedItem = GetItemByType(itemType, 0);
            if (!changedItem) {
                if (amount < 1) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "[%s][%d]request sell seed item char[%u] "
                    //     "change amount[%lld] = 0",
                    //     "WareHouse.cpp", 0x429a, m_ownerId, static_cast<long long>(amount)
                    // )
                    L2_LOG_ERROR(Log,
                        "[manor] Cannot create a seed stack for owner %u (manor %d, item class %d): "
                        "quantity %lld is below 1. Requesting transaction rollback and failure response 8.",
                        m_ownerId, manorId, itemType, static_cast<long long>(amount));
                    return rollbackAndFail(8);
                }

                changedItem = MakeNewItem(
                    m_ownerId, static_cast<unsigned int>(itemType), 0, 0, 0, 0, 0, 0,
                    static_cast<int>(WareHouseType_Inventory), emptyOptionKey, 0, emptyBaseAttribute, 0, 0
                );
                if (!changedItem) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "[%s][%d]request sell seed item char[%u]. "
                    //     "make item fail.",
                    //     "WareHouse.cpp", 0x428a, m_ownerId
                    // )
                    L2_LOG_ERROR(Log,
                        "[manor] Seed item creation failed for owner %u (manor %d, item class %d, quantity %lld). "
                        "Requesting transaction rollback and failure response 8.",
                        m_ownerId, manorId, itemType, static_cast<long long>(amount));
                    return rollbackAndFail(8);
                }
                transaction.AddItemNew(changedItem);
                changedItem->SetAmount(amount);
                changedItemId = static_cast<int>(changedItem->ItemID());
                changedItemScope = ItemChangeScope_Added;
                changedItem->SetChangeScope(ItemChangeScope_Added);
            } else {
                transaction.AddItem(changedItem);
                if (amount < 1) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "[%s][%d]request sell seed item char[%u] "
                    //     "change amount[%lld] = 0",
                    //     "WareHouse.cpp", 0x427c, m_ownerId, static_cast<long long>(amount)
                    // )
                    L2_LOG_ERROR(Log,
                        "[manor] Cannot increase the seed stack for owner %u (manor %d, item class %d): "
                        "quantity change %lld is below 1. Requesting transaction rollback and failure response 8.",
                        m_ownerId, manorId, itemType, static_cast<long long>(amount));
                    return rollbackAndFail(8);
                }
                changedItem->SetAmount(runtime::AddInt64Wrapping(changedItem->Amount(), amount));
                changedItemId = static_cast<int>(changedItem->ItemID());
                changedItemScope = ItemChangeScope_Increased;
                changedItem->SetChangeScope(ItemChangeScope_Increased);
            }
        }

        const int written = ::Assemble(
            changedItems.data() + changedItemBytes, static_cast<int>(changedItems.size()) - changedItemBytes, "dddQQ",
            static_cast<int>(changedItemScope), itemType, changedItemId, static_cast<long long>(amount),
            static_cast<long long>(changedItem->Amount())
        );
        if (written < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d]request sell seed item char[%u]. "
            //     "socket buffer overflow.",
            //     "WareHouse.cpp", 0x42ab, m_ownerId
            // )
            // L2_LOG_ERROR(Log,
                // "[%s][%d]request sell seed item char[%u]. "
                // "socket buffer overflow.",
                // "WareHouse.cpp", 0x42ab, m_ownerId
            // )
            L2_LOG_ERROR(Log,
                "[manor] Cannot assemble the seed item response for owner %u (result %d; %d of %zu bytes used). "
                "Requesting transaction rollback and failure response 7.",
                m_ownerId, written, changedItemBytes, changedItems.size());
            return rollbackAndFail(7);
        }
        ++changedItemCount;
        changedItemBytes += written;
    }

    // Current code performs this second item-type lookup instead of reusing
    // the earlier GetAdena smart pointer.
    changedItem = GetItemByType(57, 0);
    if (!changedItem) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]request sell seed item char[%u] price[%lld] = 0", "WareHouse.cpp", 0x42ee,
        //     m_ownerId, static_cast<long long>(totalPrice)
        // )
        L2_LOG_ERROR(Log,
            "[manor] Cannot collect seed payment from owner %u in manor %d: adena item was not found "
            "during the payment lookup (price %lld). Requesting transaction rollback and failure response 8.",
            m_ownerId, manorId, static_cast<long long>(totalPrice));
        return rollbackAndFail(8);
    }
    transaction.AddItem(changedItem);
    if (totalPrice < 1) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]request sell seed item char[%u] price[%lld]", "WareHouse.cpp", 0x42d6, m_ownerId,
        //     static_cast<long long>(totalPrice)
        // )
        L2_LOG_ERROR(Log,
            "[manor] Cannot collect seed payment from owner %u in manor %d: price %lld is below 1. "
            "Requesting transaction rollback and failure response 8.",
            m_ownerId, manorId, static_cast<long long>(totalPrice));
        return rollbackAndFail(8);
    }

    const std::int64_t currentAdena = changedItem->Amount();
    ItemChangeScope adenaScope = ItemChangeScope_Decreased;
    if (currentAdena < totalPrice) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]request sell seed item char[%u] price[%lld]", "WareHouse.cpp", 0x42cd, m_ownerId,
        //     static_cast<long long>(totalPrice)
        // )
        L2_LOG_ERROR(Log,
            "[manor] Cannot collect seed payment from owner %u in manor %d: available adena %lld is below price %lld. "
            "Requesting transaction rollback and failure response 8.",
            m_ownerId, manorId, static_cast<long long>(currentAdena), static_cast<long long>(totalPrice));
        return rollbackAndFail(8);
    }
    if (currentAdena == totalPrice) {
        changedItem->SetAmount(0);
        changedItem->TransactDelete();
        adenaScope = ItemChangeScope_Deleted;
        changedItem->SetChangeScope(ItemChangeScope_Deleted);
    } else {
        changedItem->SetAmount(runtime::SubtractInt64Wrapping(currentAdena, totalPrice));
        changedItem->SetChangeScope(ItemChangeScope_Decreased);
    }

    const int adenaWritten = ::Assemble(
        changedItems.data() + changedItemBytes, static_cast<int>(changedItems.size()) - changedItemBytes, "dddQQ",
        static_cast<int>(adenaScope), 57, static_cast<int>(changedItem->ItemID()),
        static_cast<long long>(runtime::NegateInt64Wrapping(totalPrice)), static_cast<long long>(changedItem->Amount())
    );
    if (adenaWritten < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]request sell seed item char[%u]. "
        //     "socket buffer overflow.",
        //     "WareHouse.cpp", 0x42e5, m_ownerId
        // )
        // L2_LOG_ERROR(Log,
            // "[%s][%d]request sell seed item char[%u]. "
            // "socket buffer overflow.",
            // "WareHouse.cpp", 0x42e5, m_ownerId
        // )
        L2_LOG_ERROR(Log,
            "[manor] Cannot assemble the seed payment response for owner %u (result %d; %d of %zu bytes used). "
            "Requesting transaction rollback and failure response 7.",
            m_ownerId, adenaWritten, changedItemBytes, changedItems.size());
        return rollbackAndFail(7);
    }
    ++changedItemCount;
    changedItemBytes += adenaWritten;

    manor->ApplyManorSeedToChangeData();
    socket->Send(
        "cdddddQdb", 0xb1, 0, characterId, userServerId, manorId, residenceId, static_cast<long long>(totalPrice),
        changedItemCount, changedItemBytes, changedItems.data()
    );
    return true;
}

bool CWareHouse::SellCrop(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00624ee4. Like BuySeed, the target re-decodes the
    // root packet and lets CTransaction's destructor commit after the reply.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int userServerId = 0;
    int manorId = 0;
    int residenceId = 0;
    int itemCount = 0;
    const std::uint8_t* cursor =
        ::Disassemble(packet, "ddddd", &characterId, &userServerId, &manorId, &residenceId, &itemCount);

    auto sendFailure = [&](int result) {
        socket->Send("cdddddd", 0xb2, result, characterId, userServerId, manorId, residenceId, itemCount);
        return false;
    };
    auto sendItemFailure = [&](int result) {
        // Current per-row result-8 sends use `cddddddd`, but never initialise
        // the final vararg home slot. The paired current L2Server decoder
        // reads only `dddddd` after opcode 0xb2 and ignores that trailing
        // scalar. Preserve the wire width while avoiding a stack-data leak.
        socket->Send("cddddddd", 0xb2, result, characterId, userServerId, manorId, residenceId, itemCount, 0);
        return false;
    };

    if (itemCount < 1) {
        // Log.Add(LOG_ERROR, "[%s][%d]SellCrop count is below 0, [%d]", "WareHouse.cpp", 0x4324, itemCount)
        L2_LOG_ERROR(Log,
            "[manor] Cannot sell crops for owner %u in manor %d: item count %d is below 1. Requesting failure response 5.",
            m_ownerId, manorId, itemCount);
        return sendFailure(5);
    }

    CManorSP manor = g_manorDb.GetManor(manorId);
    if (!manor) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find manor, id[%d]", "WareHouse.cpp", 0x432c, manorId)
        L2_LOG_ERROR(Log,
            "[manor] Cannot sell crops for owner %u: manor %d was not found. Requesting failure response 2.",
            m_ownerId, manorId);
        return sendFailure(2);
    }

    CTransaction transaction;
    std::array<char, 0x5000> changedItems{};
    int changedItemBytes = 0;
    int changedItemCount = 0;
    CItemSP changedItem;
    int changedItemId = -1;
    ItemChangeScope changedItemScope = ItemChangeScope_Unknown;

    auto rollbackAndFail = [&](int result, bool itemFailure) {
        transaction.SetRollback();
        return itemFailure ? sendItemFailure(result) : sendFailure(result);
    };

    manor->ResetManorCropToChangeData();
    const ItemOptionKey emptyOptionKey{};
    const BaseAttribute emptyBaseAttribute{};
    for (int itemIndex = 0; itemIndex < itemCount; ++itemIndex) {
        int itemType = 0;
        std::int64_t amount = 0;
        int consumeType = 0;
        cursor = ::Disassemble(cursor, "dQd", &itemType, &amount, &consumeType);

        if (amount < 1 && itemType != 57) {
            if (amount >= 0) {
                // Log.Add(
                //     LOG_ERROR,
                //     "[%s][%d]request sell crop item char[%d]. "
                //     "make item fail.",
                //     "WareHouse.cpp", 0x4352, static_cast<int>(m_ownerId)
                // )
                L2_LOG_ERROR(Log,
                    "[manor] Cannot sell crops for owner %u (manor %d, item class %d): quantity change is zero. "
                    "Requesting transaction rollback and failure response 8.",
                    m_ownerId, manorId, itemType);
                return rollbackAndFail(8, true);
            }
            if (!manor->SetManorCropToChangeData(itemType, runtime::NegateInt64Wrapping(amount))) {
                // Log.Add(
                //     LOG_ERROR,
                //     "[%s][%d]request sell crop item char[%d]. "
                //     "make item fail.",
                //     "WareHouse.cpp", 0x4349, static_cast<int>(m_ownerId)
                // )
                L2_LOG_ERROR(Log,
                    "[manor] Cannot reserve crop procurement for owner %u (manor %d, item class %d, quantity change %lld): "
                    "the crop is not listed or the remaining procurement allowance after pending changes is insufficient. "
                    "Requesting transaction rollback and failure response 8.",
                    m_ownerId, manorId, itemType, static_cast<long long>(amount));
                return rollbackAndFail(8, true);
            }
        }

        if (consumeType == 2 || consumeType == 3) {
            changedItem = GetItemByType(itemType, 0);
            if (!changedItem) {
                if (amount < 1) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "[%s][%d]request sell SellCrop item char[%d] "
                    //     "change amount[%d] = 0",
                    //     "WareHouse.cpp", 0x43a1, static_cast<int>(m_ownerId), static_cast<int>(amount)
                    // )
                    L2_LOG_ERROR(Log,
                        "[manor] Cannot create an item stack during crop sale for owner %u (manor %d, item class %d): "
                        "quantity %lld is below 1. Requesting transaction rollback and failure response 8.",
                        m_ownerId, manorId, itemType, static_cast<long long>(amount));
                    return rollbackAndFail(8, true);
                }

                changedItem = MakeNewItem(
                    m_ownerId, static_cast<unsigned int>(itemType), 0, 0, 0, 0, 0, 0,
                    static_cast<int>(WareHouseType_Inventory), emptyOptionKey, 0, emptyBaseAttribute, 0, 0
                );
                if (!changedItem) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "[%s][%d]request sell SellCrop item char[%d]. "
                    //     "make item fail.",
                    //     "WareHouse.cpp", 0x4391, static_cast<int>(m_ownerId)
                    // )
                    L2_LOG_ERROR(Log,
                        "[manor] Item creation failed during crop sale for owner %u (manor %d, item class %d, quantity %lld). "
                        "Requesting transaction rollback and failure response 8.",
                        m_ownerId, manorId, itemType, static_cast<long long>(amount));
                    return rollbackAndFail(8, true);
                }
                transaction.AddItemNew(changedItem);
                changedItem->SetAmount(amount);
                changedItemId = static_cast<int>(changedItem->ItemID());
                changedItemScope = ItemChangeScope_Added;
                changedItem->SetChangeScope(ItemChangeScope_Added);
            } else {
                transaction.AddItem(changedItem);
                if (amount < 1) {
                    if (amount == 0) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     "[%s][%d]request sell SellCrop item char[%d] "
                        //     "change amount[%d] = 0",
                        //     "WareHouse.cpp", 0x4383, static_cast<int>(m_ownerId), static_cast<int>(amount)
                        // )
                        L2_LOG_ERROR(Log,
                            "[manor] Cannot update an item stack during crop sale for owner %u (manor %d, item class %d): "
                            "quantity change is zero. Requesting transaction rollback and failure response 8.",
                            m_ownerId, manorId, itemType);
                        return rollbackAndFail(8, true);
                    }

                    const std::int64_t resultAmount = runtime::AddInt64Wrapping(changedItem->Amount(), amount);
                    if (resultAmount < 0) {
                        // Log.Add(
                        //     LOG_ERROR, "[%s][%d]request sell crop item char[%d]", "WareHouse.cpp", 0x437a,
                        //     static_cast<int>(m_ownerId)
                        // )
                        L2_LOG_ERROR(Log,
                            "[manor] Cannot update an item stack during crop sale for owner %u (manor %d, item class %d): "
                            "current quantity %lld plus change %lld produces negative quantity %lld. "
                            "Requesting transaction rollback and failure response 8.",
                            m_ownerId, manorId, itemType, static_cast<long long>(changedItem->Amount()),
                            static_cast<long long>(amount), static_cast<long long>(resultAmount));
                        return rollbackAndFail(8, true);
                    }
                    if (resultAmount == 0) {
                        changedItem->SetAmount(0);
                        changedItem->TransactDelete();
                        changedItemScope = ItemChangeScope_Deleted;
                    } else {
                        changedItem->SetAmount(resultAmount);
                        changedItemScope = ItemChangeScope_Decreased;
                    }
                } else {
                    changedItem->SetAmount(runtime::AddInt64Wrapping(changedItem->Amount(), amount));
                    changedItemScope = ItemChangeScope_Increased;
                }
                changedItemId = static_cast<int>(changedItem->ItemID());
                changedItem->SetChangeScope(changedItemScope);
            }
        } else if (amount < 1) {
            if (amount < 0) {
                changedItem = GetItemByType(itemType, 0);
                if (!changedItem) {
                    // Log.Add(
                    //     LOG_ERROR, "[%s][%d]request sell crop item char[%d] ", "WareHouse.cpp", 0x43cc,
                    //     static_cast<int>(m_ownerId)
                    // )
                    L2_LOG_ERROR(Log,
                        "[manor] Cannot remove an item during crop sale for owner %u (manor %d, item class %d, "
                        "consume type %d, quantity change %lld): item was not found. "
                        "Requesting transaction rollback and failure response 8.",
                        m_ownerId, manorId, itemType, consumeType, static_cast<long long>(amount));
                    return rollbackAndFail(8, true);
                }
                transaction.AddItem(changedItem);
                changedItem->SetAmount(0);
                changedItem->TransactDelete();
                changedItemId = static_cast<int>(changedItem->ItemID());
                changedItemScope = ItemChangeScope_Deleted;
                changedItem->SetChangeScope(ItemChangeScope_Deleted);
            }
            // Current 0x00624ee4 has no amount-zero fallback for an
            // unsupported consume type: it assembles the kept carrier.
        } else {
            changedItem = MakeNewItem(
                m_ownerId, static_cast<unsigned int>(itemType), 0, 0, 0, 0, 0, 0, static_cast<int>(WareHouseType_Inventory),
                emptyOptionKey, 0, emptyBaseAttribute, 0, 0
            );
            if (!changedItem) {
                // Log.Add(
                //     LOG_ERROR,
                //     "[%s][%d]request sell SellCrop item char[%d]. "
                //     "make item fail.",
                //     "WareHouse.cpp", 0x43b0, static_cast<int>(m_ownerId)
                // )
                L2_LOG_ERROR(Log,
                    "[manor] Item creation failed during crop sale for owner %u (manor %d, item class %d, "
                    "consume type %d, quantity %lld). Requesting transaction rollback and failure response 8.",
                    m_ownerId, manorId, itemType, consumeType, static_cast<long long>(amount));
                return rollbackAndFail(8, true);
            }
            transaction.AddItemNew(changedItem);
            changedItem->SetAmount(amount);
            changedItemId = static_cast<int>(changedItem->ItemID());
            changedItemScope = ItemChangeScope_Added;
            changedItem->SetChangeScope(ItemChangeScope_Added);
        }

        const int written = ::Assemble(
            changedItems.data() + changedItemBytes, static_cast<int>(changedItems.size()) - changedItemBytes, "dddQQ",
            static_cast<int>(changedItemScope), itemType, changedItemId, static_cast<long long>(amount),
            static_cast<long long>(changedItem->Amount())
        );
        if (written < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d]request sell SellCrop item char[%d]. "
            //     "socket buffer overflow.",
            //     "WareHouse.cpp", 0x43dd, static_cast<int>(m_ownerId)
            // )
            // L2_LOG_ERROR(Log,
                // "[%s][%d]request sell SellCrop item char[%d]. "
                // "socket buffer overflow.",
                // "WareHouse.cpp", 0x43dd, static_cast<int>(m_ownerId)
            // )
            L2_LOG_ERROR(Log,
                "[manor] Cannot assemble the crop item response for owner %u (result %d; %d of %zu bytes used). "
                "Requesting transaction rollback and failure response 7.",
                m_ownerId, written, changedItemBytes, changedItems.size());
            return rollbackAndFail(7, false);
        }
        ++changedItemCount;
        changedItemBytes += written;
    }

    manor->ApplyManorCropToChangeData();
    socket->Send(
        "cddddddb", 0xb2, 0, characterId, userServerId, manorId, residenceId, changedItemCount, changedItemBytes,
        changedItems.data()
    );
    return true;
}

void CWareHouse::SaveUnsavedItems() {
    L2_GUARD_FUNCTION();
    if (!GetCacheDConfiguration().writeCache || !m_unsavedItemsPending) {
        return;
    }

    CSaveUnsavedItemInWareHouse save;
    m_items.FOR_ALL(&save, L"WareHouse.cpp", 0x205a);
    m_unsavedItemsPending = false;
}

void CWareHouse::SaveLateSavingItems() {
    L2_GUARD_FUNCTION();
    if (!GetCacheDConfiguration().lateSaving || !m_lateSavingItemsPending) {
        return;
    }

    CSaveLateSavingItemInWareHouse save;
    m_items.FOR_ALL(&save, L"WareHouse.cpp", 0x2089);
    for (const std::uint32_t itemId : save.ZeroAmountItems()) {
        // Noise: Zero is a valid result when deferred consumption exhausts an item. The cache cleanup below needs no error record.
        // Log.Add(
        //     LOG_ERROR,
        //     "[WareHouse.cpp][%d] item amount is 0 after updating "
        //     "late saving item",
        //     0x2089
        // )
        CItemSP item = PopItem(static_cast<int>(itemId));
        if (item)
            item->Delete(false, false);
    }
    m_lateSavingItemsPending = false;
}

std::int64_t CWareHouse::GetCheckSumValue() {
    L2_GUARD_FUNCTION();
    CAccumulateWareHouseCheckSum accumulator;
    m_items.FOR_ALL(&accumulator);
    return accumulator.Value();
}

void CWareHouse::SetCheckSumOnDB() {
    L2_GUARD_FUNCTION();

    const CacheDConfiguration& config = GetCacheDConfiguration();
    DBConn connection(runtime::process::CurrentDatabaseId());
    int ownerId = static_cast<int>(m_ownerId);
    int warehouseType = static_cast<int>(GetWarehouseNum());
    const std::wstring securityKey = Utf8ToWide(config.dbSecurityKey);
    constexpr const wchar_t* statement = L"{Call lin_SetCheckSum (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&ownerId);
    connection.BindParameter(&warehouseType);
    connection.BindParameter(securityKey.c_str());
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "SetCheckSum failed.")
        // Log.Add(LOG_ERROR,
        //     "[warehouse integrity] Cannot write checksum for owner %u, warehouse %d, database %d.",
        //     m_ownerId, warehouseType, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[warehouse integrity] Cannot write checksum for owner %u, warehouse %d, database %d.",
            m_ownerId, warehouseType, runtime::process::CurrentDatabaseId());
    }
}

bool CWareHouse::ItemcheckOnDB() {
    L2_GUARD_FUNCTION();

    const CacheDConfiguration& config = GetCacheDConfiguration();
    int checkResult = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&checkResult);

    int ownerId = static_cast<int>(m_ownerId);
    int warehouseType = static_cast<int>(GetWarehouseNum());
    const std::wstring securityKey = Utf8ToWide(config.dbSecurityKey);
    constexpr const wchar_t* statement = L"{Call lin_ItemCheck (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&ownerId);
    connection.BindParameter(&warehouseType);
    connection.BindParameter(securityKey.c_str());
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "ItemCheckSum failed. Owner[%u], Warehouse[%d]", m_ownerId, warehouseType)
        // Log.Add(LOG_ERROR,
        //     "[warehouse integrity] Item-check query failed for owner %u, warehouse %d, database %d. Returning a failed check.",
        //     m_ownerId, warehouseType, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[warehouse integrity] Item-check query failed for owner %u, warehouse %d, database %d; returning a failed check.",
            m_ownerId, warehouseType, runtime::process::CurrentDatabaseId());
        return false;
    }

    connection.Fetch();
    return checkResult == 1;
}

std::int64_t CWareHouse::GetCheckSumValueOnDB() {
    L2_GUARD_FUNCTION();

    long long checksum = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&checksum);

    int ownerId = static_cast<int>(m_ownerId);
    int warehouseType = static_cast<int>(GetWarehouseNum());
    constexpr const wchar_t* statement = L"{Call lin_GetCheckSum (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&ownerId);
    connection.BindParameter(&warehouseType);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        return static_cast<std::int64_t>(checksum);
    }

    // Log.Add(LOG_ERROR, "GetCheckSumValueOnDB() failed. Owner[%u], Warehouse[%d]", m_ownerId, warehouseType)
    // Log.Add(LOG_ERROR,
    //     "[warehouse integrity] No checksum result obtained for owner %u, warehouse %d, database %d. Returning zero for the comparison.",
    //     m_ownerId, warehouseType, runtime::process::CurrentDatabaseId())
    L2_LOG_ERROR(Log,
        "[warehouse integrity] No checksum result obtained for owner %u, warehouse %d, database %d; returning zero for the comparison.",
        m_ownerId, warehouseType, runtime::process::CurrentDatabaseId());
    return 0;
}

bool CWareHouse::ItemCheckBetweenMemoryAndDB() {
    L2_GUARD_FUNCTION();
    return GetCheckSumValue() == GetCheckSumValueOnDB();
}

void CWareHouse::SetCheckSumOnDBForServerCrash() {
    L2_GUARD_FUNCTION();

    const CacheDConfiguration& config = GetCacheDConfiguration();
    if (!g_bUseDBSecurity)
        return;

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetCheckSumAfterServerDown (?)}";
    connection.Prepare(statement);
    const std::wstring securityKey = Utf8ToWide(config.dbSecurityKey);
    connection.BindParameter(securityKey.c_str());
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "CheckSum for ServerCrash failed.")
        // Log.Add(LOG_ERROR,
        //     "[warehouse integrity] Post-crash checksum update failed in database %d.",
        //     runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[warehouse integrity] Post-crash checksum update failed in database %d.",
            runtime::process::CurrentDatabaseId());
    }
}

bool CWareHouse::ItemsLoaded() const noexcept { return m_itemsLoaded; }

std::uint32_t CWareHouse::GetOwnerId() const noexcept { return m_ownerId; }

void CWareHouse::SetOwnerId(std::uint32_t ownerId) noexcept { m_ownerId = ownerId; }

void CPetWarehouse::SendPetItemList(CSocket* socket, int userSocketId, int contextId, int contextValue) {
    // Current Cached.exe 0x005ede6c. The guard compiled by the inlined
    // FOR_ALL_OBJECT_IN_OBJECT_MAP body keeps this exact source method
    // identity even though its ordinary entry guard was optimized away.
    CPacketHelperEx<20480> packet;
    int itemCount = 0;

    class AssemblePetItem {
    public:
        AssemblePetItem(CPacketHelperEx<20480>& packet, int& itemCount) : m_packet(packet), m_itemCount(itemCount) {}

        bool Do(CItem* item) {
            // CItemAssemblerSendToL2Server assembler(*item, 0x18)
            CItemAssemblerSendToL2Server assembler(*item, (ItemAssembleType::SHAPESHIFT | ItemAssembleType::ENSOUL));
            if (assembler.AssembleItemInfoImpl(m_packet) > 0) {
                ++m_itemCount;
            }
            return true;
        }

    private:
        CPacketHelperEx<20480>& m_packet;
        int& m_itemCount;
    } assemble(packet, itemCount);

    m_items.FOR_ALL(&assemble, L"WareHouse.cpp", 0x3061);
    socket->Send(
        "cdddddb", 0x62, 1, userSocketId, contextId, contextValue, itemCount, packet.GetLength(), packet.GetPacket()
    );
}

void CPetWarehouse::ClearItemWhenDespawn() {
    // Current Cached.exe 0x005d6238.
    L2_GUARD_FUNCTION();
    m_items.DeleteAllObjects(nullptr, -1);
}

bool CPetWarehouse::PetGetItem(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x005f1a88. The caller already holds this
    // warehouse's write lock.
    L2_GUARD_FUNCTION();

    int petId = 0;
    int responseContext = 0;
    int responseContext2 = 0;
    int requestContext = 0;
    int pickedItemId = 0;
    int itemType = 0;
    int consumeType = 0;
    std::int64_t pickedAmount = 0;
    ::Disassemble(
        packet, "dddddddQ", &petId, &responseContext, &responseContext2, &requestContext, &pickedItemId, &itemType,
        &consumeType, &pickedAmount
    );
    if (pickedAmount < 1)
        return false;

    CItemSP pickedItem = CItem::Load(static_cast<std::uint32_t>(pickedItemId));
    CItemSP petItem = GetItemByType(itemType, 0);
    if (pickedItemId != 0 && !pickedItem) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] CWareHouse::PetGetItem failed. invalid item[%d]", "WareHouse.cpp", 0x3265,
        //     pickedItemId
        // )
        L2_LOG_ERROR(Log,
            "[pet inventory] Cannot pick up item %d for pet %d (requested item class %d, consume type %d, quantity %lld): "
            "item load failed. Request processing stopped before transfer.",
            pickedItemId, petId, itemType, consumeType, static_cast<long long>(pickedAmount));
        return false;
    }

    if (pickedItem) {
        // The world drop object is first removed from the process-wide item
        // cache. Its intrusive reference remains alive for the move/merge.
        pickedItem->Delete(false, false);
    }

    CItemSP auditItem;
    if (consumeType == 0 || consumeType == 1 || !petItem) {
        if (!pickedItem) {
            const ItemOptionKey emptyOption;
            const BaseAttribute emptyAttribute;
            pickedItem = MakeNewItem(
                m_ownerId, static_cast<unsigned int>(itemType), pickedAmount, 0, 0, 0, 0, 0, WareHouseType_Pet, emptyOption,
                0, emptyAttribute, 0, 0
            );
            if (!pickedItem)
                return false;
        } else {
            pickedItem->setOwnerID(m_ownerId);
            pickedItem->SetWarehouse(WareHouseType_Pet);
            pickedItem->Save(true);
        }

        PushItem(pickedItem.get());
        socket->Send(
            "cddddddQd", 0x66, 1, responseContext2, responseContext, 1, itemType, static_cast<int>(pickedItem->ItemID()),
            static_cast<long long>(pickedAmount), static_cast<int>(pickedItem->Amount())
        );
        auditItem = pickedItem;
    } else {
        if (pickedItem) {
            pickedAmount = pickedItem->Amount();
            pickedItem->Delete(true, false);
        }
        petItem->SetAmount(runtime::AddInt64Wrapping(petItem->Amount(), pickedAmount));
        petItem->Save(true);
        socket->Send(
            "cdddddQ", 0x66, 1, responseContext2, responseContext, 3, static_cast<int>(petItem->ItemID()),
            static_cast<long long>(petItem->Amount())
        );
        auditItem = petItem;
    }

    CPetSP pet = g_petDb.GetPet(petId, runtime::process::CurrentDatabaseId());
    if (pet && pet->m_ownerId > 0) {
        CUserSP owner = g_userDb.GetUser(static_cast<int>(pet->m_ownerId), true, false, runtime::process::CurrentDatabaseId());
        WritePetGetItemAudit(MakeWareHouseAuditActor(owner.get()), petId, *auditItem.get(), pickedAmount, pickedItemId);
    }
    return true;
}

bool CPetWarehouse::PetDropItem(CItemSP itemToDrop) {
    // Current Cached.exe 0x005cfabc. The caller already holds this
    // warehouse's write lock.
    L2_GUARD_FUNCTION();

    CItemSP removedItem = PopItem(static_cast<int>(itemToDrop->ItemID()));
    if (!removedItem)
        return false;

    itemToDrop->SetWarehouse(WareHouseType_Inventory);
    itemToDrop->setOwnerID(0);
    CItem::CacheItem(itemToDrop.get());
    itemToDrop->Save(true);
    return true;
}

void CPetWarehouse::SetPetId(std::uint32_t petId) noexcept { m_petId = petId; }

void CPetWarehouse::SetNpcClassId(std::uint32_t npcClassId) noexcept { m_npcClassId = npcClassId; }

bool CPetWarehouse::DepositToPet(
    CSocket* socket,
    int userServerId,
    int petServerId,
    CWareHouse* userWarehouse,
    int itemId,
    ItemConsumeType consumeType,
    std::int64_t count
) {
    // Current Cached.exe 0x005ee114. Both warehouse write locks are held by
    // RequestGiveItemToPet before entering this owner.
    L2_GUARD_FUNCTION();

    if (count < 0) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] DepositToPet invalid item count[%lld]", "WareHouse.cpp", 0x3072,
        //     static_cast<long long>(count)
        // )
        // Log.Add(LOG_ERROR,
        //     "[pet inventory] Transfer to pet %u rejected for owner %u: item %d quantity %lld is negative. No transfer performed.",
        //     m_petId, m_ownerId, itemId, static_cast<long long>(count))
        L2_LOG_ERROR(Log,
            "[pet inventory] Transfer to pet %u rejected for owner %u: item %d quantity %lld is negative; no transfer performed.",
            m_petId, m_ownerId, itemId, static_cast<long long>(count));
        return false;
    }
    if ((consumeType == ICT_NORMAL || consumeType == ICT_CHARGE) && count != 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] DepositToPet item(normal, charge type) "
        //     "count[%lld] is invalid",
        //     "WareHouse.cpp", 0x3079, static_cast<long long>(count)
        // )
        // Log.Add(LOG_ERROR,
        //     "[pet inventory] Transfer to pet %u rejected for owner %u: item %d with consume type %d requires quantity 1, requested %lld. No transfer performed.",
        //     m_petId, m_ownerId, itemId, static_cast<int>(consumeType), static_cast<long long>(count))
        L2_LOG_ERROR(Log,
            "[pet inventory] Transfer to pet %u rejected for owner %u: item %d with consume type %d requires quantity 1, requested %lld; no transfer performed.",
            m_petId, m_ownerId, itemId, static_cast<int>(consumeType), static_cast<long long>(count));
        return false;
    }

    CItemSP userItem = userWarehouse->GetItem(itemId);
    if (!userItem) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] CWareHouse::DepositToPet failed. "
        //     "don't have item[%d]",
        //     "WareHouse.cpp", 0x3081, itemId
        // )
        // Log.Add(LOG_ERROR,
        //     "[pet inventory] Transfer to pet %u failed for owner %u: item %d not found in player inventory. No transfer performed.",
        //     m_petId, m_ownerId, itemId)
        L2_LOG_ERROR(Log,
            "[pet inventory] Transfer to pet %u failed for owner %u: item %d not found in player inventory; no transfer performed.",
            m_petId, m_ownerId, itemId);
        return false;
    }
    if (userItem->Amount() < count) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] CWareHouse::DepositToPet failed. count[%lld] "
        //     "> pItem->m_nAmout[%lld]",
        //     "WareHouse.cpp", 0x3088, static_cast<long long>(count), static_cast<long long>(userItem->Amount())
        // )
        // Log.Add(LOG_ERROR,
        //     "[pet inventory] Transfer to pet %u failed for owner %u: item %d has quantity %lld, requested %lld. No transfer performed.",
        //     m_petId, m_ownerId, itemId, static_cast<long long>(userItem->Amount()), static_cast<long long>(count))
        L2_LOG_ERROR(Log,
            "[pet inventory] Transfer to pet %u failed for owner %u: item %d has quantity %lld, requested %lld; no transfer performed.",
            m_petId, m_ownerId, itemId, static_cast<long long>(userItem->Amount()), static_cast<long long>(count));
        return false;
    }

    CItemSP petItem = GetItemByType(static_cast<int>(userItem->ItemType()), 0);
    const std::int64_t userAmountAfterDeposit = runtime::SubtractInt64Wrapping(userItem->Amount(), count);
    CPacketHelperEx<20480> response;

    if (consumeType == ICT_NORMAL || consumeType == ICT_CHARGE || (!petItem && userAmountAfterDeposit == 0)) {
        userItem->SetWarehouse(WareHouseType_Pet);
        userItem->Save(true);
        userWarehouse->PopItem(itemId);
        PushItem(userItem.get());

        response.Assemble("ddd", userServerId, 2, itemId);
        AssemblePetTransferNewItem(response, petServerId, *userItem.get(), count);

        CUserSP owner =
            g_userDb.GetUser(static_cast<int>(userWarehouse->GetOwnerId()), true, false, runtime::process::CurrentDatabaseId());
        WritePetTransferAudit(
            0x6f, MakeWareHouseAuditActor(owner.get()), static_cast<int>(m_petId), static_cast<int>(m_npcClassId),
            *userItem.get(), runtime::NegateInt64Wrapping(count), userAmountAfterDeposit, itemId
        );
        socket->Send("cdb", 0x64, 1, response.GetLength(), response.GetPacket());
        return true;
    }

    if (!petItem) {
        petItem = MakeNewItem(
            userWarehouse->GetOwnerId(), userItem->ItemType(), count, userItem->Enchant(), userItem->Eroded(),
            userItem->Bless(), userItem->Ident(), userItem->Wished(), WareHouseType_Pet, userItem->OptionKey(),
            userItem->m_intensiveItemType, userItem->Attribute(), userItem->HLocked(), userItem->UseCount()
        );
        if (!petItem) {
            // Log.Add(LOG_ERROR, "[%s][%d] DepositToPet failed. make item fail", "WareHouse.cpp", 0x30e3)
            // Log.Add(LOG_ERROR,
            //     "[pet inventory] Transfer to pet %u failed for owner %u: cannot create the pet inventory stack for source item %d (class %u), quantity %lld. Source quantity unchanged.",
            //     m_petId, m_ownerId, itemId, userItem->ItemType(), static_cast<long long>(count))
            L2_LOG_ERROR(Log,
                "[pet inventory] Transfer to pet %u failed for owner %u: cannot create the pet inventory stack for source item %d (class %u), quantity %lld; source quantity unchanged.",
                m_petId, m_ownerId, itemId, userItem->ItemType(), static_cast<long long>(count));
            return false;
        }
        PushItem(petItem.get());
        AssemblePetTransferNewItem(response, petServerId, *petItem.get(), count);
    } else {
        const std::int64_t petAmountAfterDeposit = runtime::AddInt64Wrapping(petItem->Amount(), count);
        if (petAmountAfterDeposit > 999999999999LL) {
            // Log.Add(
            //     LOG_ERROR,
            //     "Warehouse error. db[%d] AmtSum[%lld] is invalid. "
            //     "OldAmt[%lld] Cnt[%lld] at [%s][%d]",
            //     static_cast<int>(petItem->ItemID()), static_cast<long long>(petAmountAfterDeposit),
            //     static_cast<long long>(petItem->Amount()), static_cast<long long>(count), "WareHouse.cpp", 0x3117
            // )
            // Log.Add(LOG_ERROR,
            //     "[pet inventory] Transfer to pet %u rejected for owner %u: destination item %u quantity %lld plus %lld gives %lld, above 999999999999. No transfer performed.",
            //     m_petId, m_ownerId, petItem->ItemID(), static_cast<long long>(petItem->Amount()), static_cast<long long>(count), static_cast<long long>(petAmountAfterDeposit))
            L2_LOG_ERROR(Log,
                "[pet inventory] Transfer to pet %u rejected for owner %u: destination item %u quantity %lld plus %lld gives %lld, above 999999999999; no transfer performed.",
                m_petId, m_ownerId, petItem->ItemID(), static_cast<long long>(petItem->Amount()), static_cast<long long>(count), static_cast<long long>(petAmountAfterDeposit));
            return false;
        }
        petItem->SetAmount(petAmountAfterDeposit);
        petItem->Save(true);
        response.Assemble(
            "dddQ", petServerId, 3, static_cast<int>(petItem->ItemID()), static_cast<long long>(petItem->Amount())
        );
    }

    if (userAmountAfterDeposit < 1) {
        userWarehouse->PopItem(static_cast<int>(userItem->ItemID()));
        if (!userItem->Delete(true, false)) {
            // Log.Add(
            //     LOG_ERROR, "[%s][%d] item delete fail[%d]", "WareHouse.cpp", 0x3127,
            //     static_cast<int>(userItem->ItemID())
            // )
            // Log.Add(LOG_ERROR,
            //     "[pet inventory] Transfer to pet %u for owner %u: deletion of source item %u failed after its player inventory cache entry was removed. Transfer continues with a success reply.",
            //     m_petId, m_ownerId, userItem->ItemID())
            L2_LOG_ERROR(Log,
                "[pet inventory] Transfer to pet %u for owner %u: deletion of source item %u failed after its player inventory cache entry was removed; transfer continues with a success reply.",
                m_petId, m_ownerId, userItem->ItemID());
        }
        response.Assemble("ddd", userServerId, 2, itemId);
    } else {
        userItem->SetAmount(userAmountAfterDeposit);
        userItem->Save(true);
        response.Assemble("dddQ", userServerId, 3, itemId, static_cast<long long>(userAmountAfterDeposit));
    }

    CUserSP owner =
        g_userDb.GetUser(static_cast<int>(userWarehouse->GetOwnerId()), true, false, runtime::process::CurrentDatabaseId());
    WritePetTransferAudit(
        0x6f, MakeWareHouseAuditActor(owner.get()), static_cast<int>(m_petId), static_cast<int>(m_npcClassId),
        *userItem.get(), runtime::NegateInt64Wrapping(count), userAmountAfterDeposit, itemId
    );
    socket->Send("cdb", 0x64, 1, response.GetLength(), response.GetPacket());
    return true;
}

bool CPetWarehouse::WithdrawFromPet(
    CSocket* socket,
    int userServerId,
    int petServerId,
    CWareHouse* userWarehouse,
    int itemId,
    ItemConsumeType consumeType,
    std::int64_t count
) {
    // Current Cached.exe 0x005efcf4. Both warehouse write locks are held by
    // RequestGetItemFromPet before entering this owner.
    L2_GUARD_FUNCTION();

    if (count < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] CWareHouse::WithdrawFromPet Failed. "
        //     "(invalid item count[%lld])",
        //     "WareHouse.cpp", 0x315d, static_cast<long long>(count)
        // )
        // Log.Add(LOG_ERROR,
        //     "[pet inventory] Transfer from pet %u rejected for owner %u: item %d quantity %lld is negative. No transfer performed.",
        //     m_petId, m_ownerId, itemId, static_cast<long long>(count))
        L2_LOG_ERROR(Log,
            "[pet inventory] Transfer from pet %u rejected for owner %u: item %d quantity %lld is negative; no transfer performed.",
            m_petId, m_ownerId, itemId, static_cast<long long>(count));
        return false;
    }
    if ((consumeType == ICT_NORMAL || consumeType == ICT_CHARGE) && count != 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] CWareHouse::WithdrawFromPet failed. "
        //     "item(normal, charge type) count[%lld] is invalid",
        //     "WareHouse.cpp", 0x3164, static_cast<long long>(count)
        // )
        // Log.Add(LOG_ERROR,
        //     "[pet inventory] Transfer from pet %u rejected for owner %u: item %d with consume type %d requires quantity 1, requested %lld. No transfer performed.",
        //     m_petId, m_ownerId, itemId, static_cast<int>(consumeType), static_cast<long long>(count))
        L2_LOG_ERROR(Log,
            "[pet inventory] Transfer from pet %u rejected for owner %u: item %d with consume type %d requires quantity 1, requested %lld; no transfer performed.",
            m_petId, m_ownerId, itemId, static_cast<int>(consumeType), static_cast<long long>(count));
        return false;
    }

    CItemSP petItem = GetItem(itemId);
    if (!petItem) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] CWareHouse::WithdrawFromPet failed. "
        //     "don't have item[%d]",
        //     "WareHouse.cpp", 0x316c, itemId
        // )
        // Log.Add(LOG_ERROR,
        //     "[pet inventory] Transfer from pet %u failed for owner %u: item %d not found in pet inventory. No transfer performed.",
        //     m_petId, m_ownerId, itemId)
        L2_LOG_ERROR(Log,
            "[pet inventory] Transfer from pet %u failed for owner %u: item %d not found in pet inventory; no transfer performed.",
            m_petId, m_ownerId, itemId);
        return false;
    }
    if (petItem->Amount() < count) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] CWareHouse::WithdrawFromPet failed. item_id[%d] "
        //     "item_type[%d] nCount[%lld] > pItem->m_nAmount[%lld]",
        //     "WareHouse.cpp", 0x3173, static_cast<int>(petItem->ItemID()), static_cast<int>(petItem->ItemType()),
        //     static_cast<long long>(count), static_cast<long long>(petItem->Amount())
        // )
        // Log.Add(LOG_ERROR,
        //     "[pet inventory] Transfer from pet %u failed for owner %u: item %d has quantity %lld, requested %lld. No transfer performed.",
        //     m_petId, m_ownerId, itemId, static_cast<long long>(petItem->Amount()), static_cast<long long>(count))
        L2_LOG_ERROR(Log,
            "[pet inventory] Transfer from pet %u failed for owner %u: item %d has quantity %lld, requested %lld; no transfer performed.",
            m_petId, m_ownerId, itemId, static_cast<long long>(petItem->Amount()), static_cast<long long>(count));
        return false;
    }

    CItemSP userItem = userWarehouse->GetItemByType(static_cast<int>(petItem->ItemType()), 0);
    const std::int64_t petAmountAfterWithdraw = runtime::SubtractInt64Wrapping(petItem->Amount(), count);
    CPacketHelperEx<20480> response;

    if (consumeType == ICT_NORMAL || consumeType == ICT_CHARGE || (!userItem && petAmountAfterWithdraw == 0)) {
        petItem->SetWarehouse(WareHouseType_Inventory);
        petItem->Save(true);
        PopItem(itemId);
        userWarehouse->PushItem(petItem.get());

        response.Assemble("ddd", petServerId, 2, itemId);
        AssemblePetTransferNewItem(response, userServerId, *petItem.get(), count);

        CUserSP owner =
            g_userDb.GetUser(static_cast<int>(userWarehouse->GetOwnerId()), true, false, runtime::process::CurrentDatabaseId());
        WritePetTransferAudit(
            0x70, MakeWareHouseAuditActor(owner.get()), static_cast<int>(m_petId), static_cast<int>(m_npcClassId),
            *petItem.get(), count, petItem->Amount(), static_cast<int>(petItem->ItemID())
        );
        socket->Send("cdb", 0x64, 1, response.GetLength(), response.GetPacket());
        return true;
    }

    if (!userItem) {
        userItem = userWarehouse->MakeNewItem(
            userWarehouse->GetOwnerId(), petItem->ItemType(), count, petItem->Enchant(), petItem->Eroded(), petItem->Bless(),
            petItem->Ident(), petItem->Wished(), WareHouseType_Inventory, petItem->OptionKey(), petItem->m_intensiveItemType,
            petItem->Attribute(), petItem->HLocked(), petItem->UseCount()
        );
        if (!userItem) {
            // Log.Add(LOG_ERROR, "[%s][%d] WithdrawFromPet failed. make item fail", "WareHouse.cpp", 0x31d1)
            // Log.Add(LOG_ERROR,
            //     "[pet inventory] Transfer from pet %u failed for owner %u: cannot create the player inventory stack for source item %d (class %u), quantity %lld. Source quantity unchanged.",
            //     m_petId, m_ownerId, itemId, petItem->ItemType(), static_cast<long long>(count))
            L2_LOG_ERROR(Log,
                "[pet inventory] Transfer from pet %u failed for owner %u: cannot create the player inventory stack for source item %d (class %u), quantity %lld; source quantity unchanged.",
                m_petId, m_ownerId, itemId, petItem->ItemType(), static_cast<long long>(count));
            return false;
        }
        userWarehouse->PushItem(userItem.get());
        AssemblePetTransferNewItem(response, userServerId, *userItem.get(), count);
    } else {
        userItem->SetAmount(runtime::AddInt64Wrapping(userItem->Amount(), count));
        userItem->Save(true);
        response.Assemble(
            "dddQ", userServerId, 3, static_cast<int>(userItem->ItemID()), static_cast<long long>(userItem->Amount())
        );
    }

    if (petAmountAfterWithdraw < 1) {
        PopItem(static_cast<int>(petItem->ItemID()));
        if (!petItem->Delete(true, false)) {
            // Log.Add(
            //     LOG_ERROR, "[%s][%d] item delete fail[%d]", "WareHouse.cpp", 0x320a,
            //     static_cast<int>(petItem->ItemID())
            // )
            // Log.Add(LOG_ERROR,
            //     "[pet inventory] Transfer from pet %u for owner %u: deletion of source item %u failed after its pet inventory cache entry was removed. Transfer continues with a success reply.",
            //     m_petId, m_ownerId, petItem->ItemID())
            L2_LOG_ERROR(Log,
                "[pet inventory] Transfer from pet %u for owner %u: deletion of source item %u failed after its pet inventory cache entry was removed; transfer continues with a success reply.",
                m_petId, m_ownerId, petItem->ItemID());
        }
        response.Assemble("ddd", petServerId, 2, itemId);
    } else {
        petItem->SetAmount(petAmountAfterWithdraw);
        petItem->Save(true);
        response.Assemble("dddQ", petServerId, 3, itemId, static_cast<long long>(petAmountAfterWithdraw));
    }

    CUserSP owner =
        g_userDb.GetUser(static_cast<int>(userWarehouse->GetOwnerId()), true, false, runtime::process::CurrentDatabaseId());
    WritePetTransferAudit(
        0x70, MakeWareHouseAuditActor(owner.get()), static_cast<int>(m_petId), static_cast<int>(m_npcClassId),
        *userItem.get(), count, userItem->Amount(), static_cast<int>(userItem->ItemID())
    );
    socket->Send("cdb", 0x64, 1, response.GetLength(), response.GetPacket());
    return true;
}

bool CPetWarehouse::PetUseItem(CSocket* socket, int petServerId, int itemId, ItemConsumeType consumeType) {
    // Current Cached.exe 0x005f2630. The caller holds this warehouse's write
    // lock for the complete database mutation and reply.
    L2_GUARD_FUNCTION();

    CItemSP item = GetItem(itemId);
    if (!item) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] CPetWarehouse::PetUseItem failed. "
        //     "invalid item(%d) pet(%d)",
        //     "WareHouse.cpp", 0x32d5, itemId, petServerId
        // )
        // Log.Add(LOG_ERROR,
        //     "[pet inventory] Cannot use item %d for pet %u (world object %d, owner %u): item not found. No success reply sent.",
        //     itemId, m_petId, petServerId, m_ownerId)
        L2_LOG_ERROR(Log,
            "[pet inventory] Cannot use item %d for pet %u (world object %d, owner %u): item not found; no success reply sent.",
            itemId, m_petId, petServerId, m_ownerId);
        return false;
    }
    if (item->Amount() < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] CPetWarehouse::PetUseItem failed. "
        //     "item count(%lld) < 1",
        //     "WareHouse.cpp", 0x32dc, static_cast<long long>(item->Amount())
        // )
        // Log.Add(LOG_ERROR,
        //     "[pet inventory] Cannot use item %u for pet %u, owner %u: cached quantity %lld is not positive. No database change requested.",
        //     item->ItemID(), m_petId, m_ownerId, static_cast<long long>(item->Amount()))
        L2_LOG_ERROR(Log,
            "[pet inventory] Cannot use item %u for pet %u, owner %u: cached quantity %lld is not positive; no database change requested.",
            item->ItemID(), m_petId, m_ownerId, static_cast<long long>(item->Amount()));
        return false;
    }

    std::int64_t remainingAmount = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&remainingAmount);
    const int ownerId = static_cast<int>(m_ownerId);
    const std::int64_t amountChange = -1;
    const int zeroDelete = consumeType != ICT_CHARGE ? 1 : 0;
    constexpr const wchar_t* statement = L"{Call lin_AmountChange (?, ?, ?, ?)}";
    if (!ExecuteWarehouseAmountChange(connection, statement, ownerId, itemId, amountChange, zeroDelete) ||
        !connection.Fetch()) {
        // Log.Add(LOG_ERROR, "[%s][%d] CPetWarehouse::PetUseItem failed. db failed", "WareHouse.cpp", 0x32e7)
        // Log.Add(LOG_ERROR,
        //     "[pet inventory] No quantity-change result obtained for item %d, pet %u, owner %u, database %d. Cached quantity unchanged and no success reply sent.",
        //     itemId, m_petId, m_ownerId, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[pet inventory] No quantity-change result obtained for item %d, pet %u, owner %u, database %d; cached quantity unchanged and no success reply sent.",
            itemId, m_petId, m_ownerId, runtime::process::CurrentDatabaseId());
        return false;
    }
    if (remainingAmount < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] CPetWarehouse::PetUseItem failed. "
        //     "item amount(%lld) < 0",
        //     "WareHouse.cpp", 0x32ee, static_cast<long long>(remainingAmount)
        // )
        // Log.Add(LOG_ERROR,
        //     "[pet inventory] Quantity-change query returned negative remainder %lld for item %d, pet %u, owner %u. Cached quantity unchanged and no success reply sent.",
        //     static_cast<long long>(remainingAmount), itemId, m_petId, m_ownerId)
        L2_LOG_ERROR(Log,
            "[pet inventory] Quantity-change query returned negative remainder %lld for item %d, pet %u, owner %u; cached quantity unchanged and no success reply sent.",
            static_cast<long long>(remainingAmount), itemId, m_petId, m_ownerId);
        return false;
    }

    item->SetAmount(remainingAmount);
    if (remainingAmount == 0 && consumeType != ICT_CHARGE) {
        PopItem(static_cast<int>(item->ItemID()));
    }
    socket->Send("cdddQ", 0x69, 1, petServerId, itemId, static_cast<long long>(remainingAmount));
    return true;
}

bool CWareHouse::ShapeShiftItem(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x006062f8. The four-item ordered transaction,
    // reply-before-commit sequence and shipping logging defects are kept.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    int sourceItemId = 0;
    int sourceServerId = 0;
    int sourceConsumeType = 0;
    int targetItemId = 0;
    int targetServerId = 0;
    int extractionItemId = 0;
    int extractionServerId = 0;
    int retainExtractionItem = 0;
    std::int64_t adenaFee = 0;
    int expiration = -1;
    ::Disassemble(
        packet, "dddddddddQd", &userServerId, &sourceItemId, &sourceServerId, &sourceConsumeType, &targetItemId,
        &targetServerId, &extractionItemId, &extractionServerId, &retainExtractionItem, &adenaFee, &expiration
    );

    CItemSP sourceItem = GetItem(sourceItemId);
    CItemSP targetItem = GetItem(targetItemId);
    CItemSP extractionItem = GetItem(extractionItemId);
    CItemSP adenaItem = GetAdena();
    std::int64_t remainingAdena = adenaItem ? adenaItem->Amount() : 0;
    CItemTransactionOrderMap transactionItems;

    auto failTransaction = [&]() {
        CTransaction::RollbackTransactOrderMap(transactionItems);
        socket->Send(
            "chdddQddddQd", 0xc6, 0xbf, 0, userServerId, sourceServerId, static_cast<long long>(-1), targetServerId,
            extractionServerId, retainExtractionItem, 0, static_cast<long long>(0), expiration
        );
        return false;
    };

    if (!sourceItem || !targetItem || !extractionItem) {
        // Log.AddT(LOG_ERROR, L"[SHAPE_SHIFT_ERR] Normal Shape Shifting - Invalid Items")
        L2_LOG_ERROR(Log,
            "[appearance] Cannot transfer appearance for owner %u: source item %d %s, target item %d %s, extraction item %d %s. "
            "Attempting rollback and requesting failure response.",
            m_ownerId, sourceItemId, sourceItem ? "found" : "missing", targetItemId, targetItem ? "found" : "missing",
            extractionItemId, extractionItem ? "found" : "missing");
        return failTransaction();
    }
    if (sourceItem->Amount() < 1) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[SHAPE_SHIFT_ERR] Normal Shape Shifting - "
        //     L"Invalid source item amount : %I64d",
        //     static_cast<long long>(sourceItem->Amount())
        // )
        L2_LOG_ERROR(Log,
            "[appearance] Cannot transfer appearance for owner %u: source item %d has quantity %lld, below 1 (target item %d). "
            "Attempting rollback and requesting failure response.",
            m_ownerId, sourceItemId, static_cast<long long>(sourceItem->Amount()), targetItemId);
        return failTransaction();
    }

    StageShapeShiftTransaction(transactionItems, 1, sourceItem);
    StageShapeShiftTransaction(transactionItems, 2, targetItem);
    StageShapeShiftTransaction(transactionItems, 3, extractionItem);

    if (sourceConsumeType != 2) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[ShapeShifting]Shape shifting caution!! Check %s and find "
        //     L"some scrl_of_shape_shift which is not stackable.",
        //     L"Itemdata.txt"
        // )
        L2_LOG_ERROR(Log,
            "[appearance] Appearance transfer for owner %u: source item %d has consume type %d; expected 2. "
            "Processing continues. Check the item definition in Itemdata.txt.",
            m_ownerId, sourceItemId, sourceConsumeType);
    }

    sourceItem->SetAmount(runtime::SubtractInt64Wrapping(sourceItem->Amount(), 1));
    if (sourceItem->Amount() < 1) {
        sourceItem->TransactDelete();
    }

    if (retainExtractionItem == 0) {
        extractionItem->SetAmount(runtime::SubtractInt64Wrapping(extractionItem->Amount(), 1));
        if (extractionItem->Amount() > 1) {
            // The shipping diagnostic accidentally reads the target item.
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[ShapeShifting]Item shape shifting fail. Extraction "
            //     L"item amount is invalid. dbid[%d], amount[%d]",
            //     static_cast<int>(targetItem->ItemID()), static_cast<int>(targetItem->Amount())
            // )
            L2_LOG_ERROR(Log,
                "[appearance] Appearance transfer for owner %u: extraction item %d still has quantity %lld after decrement "
                "(target item %d). Processing continues with transactional deletion of the extraction item.",
                m_ownerId, extractionItemId, static_cast<long long>(extractionItem->Amount()), targetItemId);
        }
        extractionItem->TransactDelete();
    }

    if (adenaFee > 0) {
        if (!adenaItem) {
            // Log.AddT(LOG_ERROR, L"[SHAPE_SHIFT_ERR] Normal Shape Shifting - Invalid Adena")
            L2_LOG_ERROR(Log,
                "[appearance] Cannot pay appearance-transfer fee %lld for owner %u, target item %d: adena item is missing. "
                "Attempting rollback and requesting failure response.",
                static_cast<long long>(adenaFee), m_ownerId, targetItemId);
            return failTransaction();
        }
        StageShapeShiftTransaction(transactionItems, 4, adenaItem);
        if (adenaItem->Amount() < adenaFee) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[SHAPE_SHIFT_ERR] Normal Shape Shifting - Invalid Adena "
            //     L"Amount : my adena - %I64d, fee - %I64d",
            //     static_cast<long long>(adenaItem->Amount()), static_cast<long long>(adenaFee)
            // )
            L2_LOG_ERROR(Log,
                "[appearance] Cannot pay appearance-transfer fee for owner %u, target item %d: available adena %lld, required %lld. "
                "Attempting rollback and requesting failure response.",
                m_ownerId, targetItemId, static_cast<long long>(adenaItem->Amount()), static_cast<long long>(adenaFee));
            return failTransaction();
        }
        adenaItem->SetAmount(runtime::SubtractInt64Wrapping(adenaItem->Amount(), adenaFee));
        if (adenaItem->Amount() == 0) {
            adenaItem->TransactDelete();
        }
        remainingAdena = adenaItem->Amount();
    }

    targetItem->SetApperanceType(
        static_cast<int>(extractionItem->ItemType()), expiration, static_cast<ShapeShiftingJobType>(0)
    );
    socket->Send(
        "chdddQddddQd", 0xc6, 0xbf, 1, userServerId, sourceServerId, static_cast<long long>(sourceItem->Amount()),
        targetServerId, extractionServerId, retainExtractionItem, targetItem->AppearanceType(),
        static_cast<long long>(remainingAdena), expiration
    );

    CommitShapeShiftTransactions(transactionItems);

    CUserSP owner = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    if (owner) {
        const WareHouseAuditActor actor = MakeWareHouseAuditActor(owner.get());
        const ShapeShiftAuditText targetText = MakeShapeShiftAuditText(*targetItem.get(), targetItem->m_intensiveItemType);
        const ShapeShiftAuditText sourceText = MakeShapeShiftAuditText(*sourceItem.get(), sourceItem->m_intensiveItemType);
        const ShapeShiftAuditText extractionText =
            MakeShapeShiftAuditText(*extractionItem.get(), extractionItem->m_intensiveItemType);

        WriteShapeShiftMainAudit(
            0x2332, actor, *targetItem.get(), targetText, targetItem->ItemType(), sourceItem->ItemType(),
            sourceItem->ItemID(), extractionItem->ItemType(), extractionItem->ItemID(), expiration
        );
        WriteShapeShiftEnsoulAudit(0x234a, actor, *targetItem.get(), targetText.ensoul);
        WriteShapeShiftRawItemAudit(actor, *sourceItem.get(), sourceText, -1, sourceItem->Amount());
        WriteShapeShiftEnsoulAudit(0x2342, actor, *sourceItem.get(), sourceText.ensoul);
        WriteShapeShiftRawItemAudit(actor, *extractionItem.get(), extractionText, -1, extractionItem->Amount());
        WriteShapeShiftEnsoulAudit(0x2342, actor, *extractionItem.get(), extractionText.ensoul);

        if (adenaItem && adenaFee > 0) {
            const ShapeShiftAuditText adenaText = MakeShapeShiftAuditText(*adenaItem.get(), adenaItem->m_intensiveItemType);
            WriteShapeShiftRawItemAudit(actor, *adenaItem.get(), adenaText, adenaFee, adenaItem->Amount());
            // Current 0x00608f38 fills this record from extraction Ensoul
            // data while retaining the adena database ID.
            WriteShapeShiftEnsoulAudit(0x2342, actor, *adenaItem.get(), extractionText.ensoul);
        }
    }
    return true;
}

bool CWareHouse::ShapeShiftItemFixed(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00609e28.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    int sourceItemId = 0;
    int sourceServerId = 0;
    int sourceConsumeType = 0;
    int targetItemId = 0;
    int targetServerId = 0;
    int appearanceClassId = 0;
    std::int64_t adenaFee = 0;
    int expiration = -1;
    ::Disassemble(
        packet, "dddddddQd", &userServerId, &sourceItemId, &sourceServerId, &sourceConsumeType, &targetItemId,
        &targetServerId, &appearanceClassId, &adenaFee, &expiration
    );

    CItemSP sourceItem = GetItem(sourceItemId);
    CItemSP targetItem = GetItem(targetItemId);
    CItemSP adenaItem = GetAdena();
    std::int64_t remainingAdena = adenaItem ? adenaItem->Amount() : 0;
    CItemTransactionOrderMap transactionItems;

    auto failTransaction = [&]() {
        CTransaction::RollbackTransactOrderMap(transactionItems);
        // The final expiration argument is present in the target call but is
        // not consumed by its shorter format string.
        socket->Send(
            "chdddQddQ", 0xc6, 0xc0, 0, userServerId, sourceServerId, static_cast<long long>(-1), targetServerId, 0,
            static_cast<long long>(0), expiration
        );
        return false;
    };

    if (!sourceItem || !targetItem) {
        // Log.AddT(LOG_ERROR, L"[SHAPE_SHIFT_ERR] Fixed Shape Shifting - Invalid Items")
        L2_LOG_ERROR(Log,
            "[appearance] Cannot apply fixed appearance %d for owner %u: source item %d %s, target item %d %s. "
            "Attempting rollback and requesting failure response.",
            appearanceClassId, m_ownerId, sourceItemId, sourceItem ? "found" : "missing",
            targetItemId, targetItem ? "found" : "missing");
        return failTransaction();
    }
    if (sourceItem->Amount() < 1) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[SHAPE_SHIFT_ERR] Fixed Shape Shifting - "
        //     L"Invalid source item amount : %I64d",
        //     static_cast<long long>(sourceItem->Amount())
        // )
        L2_LOG_ERROR(Log,
            "[appearance] Cannot apply fixed appearance %d for owner %u: source item %d has quantity %lld, below 1 (target item %d). "
            "Attempting rollback and requesting failure response.",
            appearanceClassId, m_ownerId, sourceItemId, static_cast<long long>(sourceItem->Amount()), targetItemId);
        return failTransaction();
    }

    StageShapeShiftTransaction(transactionItems, 1, sourceItem);
    StageShapeShiftTransaction(transactionItems, 2, targetItem);
    if (sourceConsumeType != 2) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[ShapeShifting]Shape shifting caution!! Check %s and find "
        //     L"some scrl_of_shape_shift which is not stackable.",
        //     L"Itemdata.txt"
        // )
        L2_LOG_ERROR(Log,
            "[appearance] Fixed appearance for owner %u: source item %d has consume type %d; expected 2. "
            "Processing continues. Check the item definition in Itemdata.txt.",
            m_ownerId, sourceItemId, sourceConsumeType);
    }
    sourceItem->SetAmount(runtime::SubtractInt64Wrapping(sourceItem->Amount(), 1));
    if (sourceItem->Amount() < 1) {
        sourceItem->TransactDelete();
    }

    if (adenaFee > 0) {
        if (!adenaItem) {
            // Log.AddT(LOG_ERROR, L"[SHAPE_SHIFT_ERR] Fixed Shape Shifting - Invalid Adena")
            L2_LOG_ERROR(Log,
                "[appearance] Cannot pay fixed-appearance fee %lld for owner %u, target item %d: adena item is missing. "
                "Attempting rollback and requesting failure response.",
                static_cast<long long>(adenaFee), m_ownerId, targetItemId);
            return failTransaction();
        }
        StageShapeShiftTransaction(transactionItems, 3, adenaItem);
        if (adenaItem->Amount() < adenaFee) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[SHAPE_SHIFT_ERR] Fixed Shape Shifting - Invalid Adena "
            //     L"Amount : my adena - %I64d, fee - %I64d",
            //     static_cast<long long>(adenaItem->Amount()), static_cast<long long>(adenaFee)
            // )
            L2_LOG_ERROR(Log,
                "[appearance] Cannot pay fixed-appearance fee for owner %u, target item %d: available adena %lld, required %lld. "
                "Attempting rollback and requesting failure response.",
                m_ownerId, targetItemId, static_cast<long long>(adenaItem->Amount()), static_cast<long long>(adenaFee));
            return failTransaction();
        }
        adenaItem->SetAmount(runtime::SubtractInt64Wrapping(adenaItem->Amount(), adenaFee));
        if (adenaItem->Amount() == 0) {
            adenaItem->TransactDelete();
        }
        remainingAdena = adenaItem->Amount();
    }

    targetItem->SetApperanceType(appearanceClassId, expiration, static_cast<ShapeShiftingJobType>(0));
    socket->Send(
        "chdddQddQd", 0xc6, 0xc0, 1, userServerId, sourceServerId, static_cast<long long>(sourceItem->Amount()),
        targetServerId, targetItem->AppearanceType(), static_cast<long long>(remainingAdena), expiration
    );

    CommitShapeShiftTransactions(transactionItems);

    CUserSP owner = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    if (owner) {
        const WareHouseAuditActor actor = MakeWareHouseAuditActor(owner.get());
        const ShapeShiftAuditText targetText = MakeShapeShiftAuditText(*targetItem.get(), targetItem->m_intensiveItemType);
        const ShapeShiftAuditText sourceText = MakeShapeShiftAuditText(*sourceItem.get(), sourceItem->m_intensiveItemType);
        WriteShapeShiftMainAudit(
            0x2332, actor, *targetItem.get(), targetText, targetItem->ItemType(), sourceItem->ItemType(),
            sourceItem->ItemID(), appearanceClassId, 0, expiration
        );
        WriteShapeShiftRawItemAudit(actor, *sourceItem.get(), sourceText, -1, sourceItem->Amount());
        WriteShapeShiftEnsoulAudit(0x2342, actor, *sourceItem.get(), sourceText.ensoul);
        if (adenaItem && adenaFee > 0) {
            const ShapeShiftAuditText adenaText = MakeShapeShiftAuditText(*adenaItem.get(), adenaItem->m_intensiveItemType);
            WriteShapeShiftRawItemAudit(actor, *adenaItem.get(), adenaText, adenaFee, adenaItem->Amount());
            WriteShapeShiftEnsoulAudit(0x2342, actor, *adenaItem.get(), adenaText.ensoul);
        }
    }
    return true;
}

bool CWareHouse::RestoreShapeShiftItem(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0060c6f4.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    int sourceItemId = 0;
    int sourceServerId = 0;
    int sourceConsumeType = 0;
    int targetItemId = 0;
    int targetServerId = 0;
    ::Disassemble(
        packet, "dddddd", &userServerId, &sourceItemId, &sourceServerId, &sourceConsumeType, &targetItemId, &targetServerId
    );

    CItemSP sourceItem = GetItem(sourceItemId);
    CItemSP targetItem = GetItem(targetItemId);
    CItemTransactionOrderMap transactionItems;
    auto failTransaction = [&]() {
        CTransaction::RollbackTransactOrderMap(transactionItems);
        socket->Send("chdddQdd", 0xc6, 0xc1, 0, userServerId, sourceServerId, static_cast<long long>(-1), targetServerId, 0);
        return false;
    };

    if (!sourceItem || !targetItem) {
        // Log.AddT(LOG_ERROR, L"[SHAPE_SHIFT_ERR] Restore Shape Shifting - Invalid Items")
        L2_LOG_ERROR(Log,
            "[appearance] Cannot restore appearance for owner %u: source item %d %s, target item %d %s. "
            "Attempting rollback and requesting failure response.",
            m_ownerId, sourceItemId, sourceItem ? "found" : "missing", targetItemId, targetItem ? "found" : "missing");
        return failTransaction();
    }
    if (sourceItem->Amount() < 1) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[SHAPE_SHIFT_ERR] Restore Shape Shifting - "
        //     L"Invalid source item amount : %I64d",
        //     static_cast<long long>(sourceItem->Amount())
        // )
        L2_LOG_ERROR(Log,
            "[appearance] Cannot restore appearance for owner %u: source item %d has quantity %lld, below 1 (target item %d). "
            "Attempting rollback and requesting failure response.",
            m_ownerId, sourceItemId, static_cast<long long>(sourceItem->Amount()), targetItemId);
        return failTransaction();
    }

    StageShapeShiftTransaction(transactionItems, 1, sourceItem);
    StageShapeShiftTransaction(transactionItems, 2, targetItem);
    if (sourceConsumeType != 2) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[ShapeShifting]Shape shifting caution!! Check %s and find "
        //     L"some scrl_of_shape_shift which is not stackable.",
        //     L"Itemdata.txt"
        // )
        L2_LOG_ERROR(Log,
            "[appearance] Appearance restoration for owner %u: source item %d has consume type %d; expected 2. "
            "Processing continues. Check the item definition in Itemdata.txt.",
            m_ownerId, sourceItemId, sourceConsumeType);
    }
    sourceItem->SetAmount(runtime::SubtractInt64Wrapping(sourceItem->Amount(), 1));
    if (sourceItem->Amount() < 1) {
        sourceItem->TransactDelete();
    }

    const int previousAppearance = targetItem->AppearanceType();
    targetItem->SetApperanceType(0, -1, static_cast<ShapeShiftingJobType>(1));
    socket->Send(
        "chdddQdd", 0xc6, 0xc1, 1, userServerId, sourceServerId, static_cast<long long>(sourceItem->Amount()),
        targetServerId, targetItem->AppearanceType()
    );

    CommitShapeShiftTransactions(transactionItems);

    CUserSP owner = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    if (owner) {
        const WareHouseAuditActor actor = MakeWareHouseAuditActor(owner.get());
        const ShapeShiftAuditText targetText = MakeShapeShiftAuditText(*targetItem.get(), targetItem->m_intensiveItemType);
        const ShapeShiftAuditText sourceText = MakeShapeShiftAuditText(*sourceItem.get(), sourceItem->m_intensiveItemType);
        WriteShapeShiftMainAudit(
            0x2338, actor, *targetItem.get(), targetText, targetItem->ItemType(), sourceItem->ItemType(),
            sourceItem->ItemID(), 0, previousAppearance, 0
        );
        // Current 0x0060d8e0 carries the source database ID in num5.
        WriteShapeShiftEnsoulAudit(0x234b, actor, *targetItem.get(), targetText.ensoul, sourceItem->ItemID());
        WriteShapeShiftRawItemAudit(actor, *sourceItem.get(), sourceText, -1, sourceItem->Amount());
        WriteShapeShiftEnsoulAudit(0x2342, actor, *sourceItem.get(), sourceText.ensoul);
    }
    return true;
}

bool CWareHouse::ExpireShapeShiftItem(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0060e42c.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    int targetItemId = 0;
    int targetServerId = 0;
    ::Disassemble(packet, "ddd", &userServerId, &targetItemId, &targetServerId);

    CItemSP targetItem = GetItem(targetItemId);
    CItemTransactionOrderMap transactionItems;
    if (!targetItem) {
        CTransaction::RollbackTransactOrderMap(transactionItems);
        socket->Send("chdddd", 0xc6, 0xc2, 0, userServerId, targetServerId, 0);
        return false;
    }

    StageShapeShiftTransaction(transactionItems, 1, targetItem);
    const int previousAppearance = targetItem->AppearanceType();
    targetItem->SetApperanceType(0, -1, static_cast<ShapeShiftingJobType>(2));
    socket->Send("chdddd", 0xc6, 0xc2, 1, userServerId, targetServerId, targetItem->AppearanceType());

    CommitShapeShiftTransactions(transactionItems);

    CUserSP owner = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());
    if (owner) {
        const WareHouseAuditActor actor = MakeWareHouseAuditActor(owner.get());
        const ShapeShiftAuditText targetText = MakeShapeShiftAuditText(*targetItem.get(), targetItem->m_intensiveItemType);
        WriteShapeShiftMainAudit(
            0x2338, actor, *targetItem.get(), targetText, targetItem->ItemType(), 0, 0, 1, previousAppearance, 0
        );
        WriteShapeShiftEnsoulAudit(0x234b, actor, *targetItem.get(), targetText.ensoul);
    }
    return true;
}

WareHouseType CWareHouse::GetWarehouseNum() {
    L2_GUARD_FUNCTION();
    return m_warehouseType;
}

std::uint64_t CWareHouse::GetLockAddr() {
    L2_GUARD_FUNCTION();
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&m_lock));
}

void CWareHouse::WriteLock(const wchar_t* callerFile, int callerLine) {
    L2_GUARD_FUNCTION();
    m_lock.WriteLock(callerFile, callerLine);
}

void CWareHouse::WriteUnlock() {
    L2_GUARD_FUNCTION();
    m_lock.WriteUnlock();
}

void CWareHouse::ReadLock(const wchar_t* callerFile, int callerLine) {
    L2_GUARD_FUNCTION();
    m_lock.ReadLock(callerFile, callerLine);
}

void CWareHouse::ReadUnlock() {
    L2_GUARD_FUNCTION();
    m_lock.ReadUnlock();
}
