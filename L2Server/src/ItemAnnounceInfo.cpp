#include "ItemAnnounceInfo.h"

#include "Item.h"
#include "ItemAnnounceParser.h"
#include "L2Packet.h"
#include "UserBroadcaster.h"
#include "UserPacketEx.h"

#include "Shared/src/Log.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/SlotType.h"
#include "Shared/src/Utils.h"

#include <array>
#include <string>
#include <string_view>

namespace {

// constexpr wchar_t kSourceFile[] = L"ItemAnnounceInfo.cpp"

#if defined(_WIN32)
constexpr wchar_t kItemAnnouncePath[] = L"..\\Script\\ItemAnnounce.txt";
#else
constexpr wchar_t kItemAnnouncePath[] = L"../Script/ItemAnnounce.txt";
#endif

} // namespace

bool ItemEnchantAnnouncer::NeedToAnnounce(const CItem& item) const {
    // Current L2Server.exe 0x0043a9b0.
    L2_GUARD_FUNCTION();

    if (item.IsWeapon()) {
        return item.m_data->GetEnchanted() > 14;
    }
    if (item.IsArmor()) {
        return item.m_data->GetEnchanted() > 9;
    }
    if (!item.IsAccessary() || item.m_data->GetEnchanted() <= 9) {
        return false;
    }
    if (item.IsArtifact()) {
        return true;
    }

    int slot = 0;
    while (slot < ST_MAX && (ST2SBT[slot] & item.m_data->m_Slot) == 0) {
        ++slot;
    }
    constexpr std::array<int, 11> kAnnouncedSlots{1, 2, 4, 5, 3, 0x14, 0x15, 0x1b, 0x23, 0x16, 0x17};
    for (const int announcedSlot : kAnnouncedSlots) {
        if (slot == announcedSlot) {
            return true;
        }
    }
    return false;
}

void ItemEnchantAnnouncer::Announce(const char16_t* userName, const CItem& item) const {
    // Current L2Server.exe 0x0043afc8.
    L2_GUARD_FUNCTION();
    if (!item.IsWeapon() && !item.IsArmor() && !item.IsAccessary()) {
        return;
    }

    // const std::uint32_t begin = runtime::GetTickCount()
    protocol::S_EX_ITEM_ANNOUNCE packet{};
    // packet.cType = 0.
    packet.cType = EItemAnnounce::IA_ENCHANT;
    packet.sUserName.assign(std::u16string_view(userName));
    packet.nItemClassID = item.GetClassId();
    packet.cEnchantCount = static_cast<char>(item.m_data->GetEnchanted());
    packet.nFromItemClassID = 0;
    m_broadcaster->BroadcastToAllUser(S_EX_ITEM_ANNOUNCE, packet);

    // Log.Add(
    //     LOG_ERROR,
    //     L"[PERF] %s@[%S][%d] performance : %ums",
    //     L"Announce Item Enchant",
    //     "ItemEnchantAnnouncer::Announce",
    //     0x4b,
    //     runtime::GetTickCount() - begin)
    // L2_LOG_ERROR(Log,
    //     L"[PERF] %s@[%S][%d] performance : %ums",
    //     L"Announce Item Enchant",
    //     "ItemEnchantAnnouncer::Announce",
    //     0x4b,
    //     runtime::GetTickCount() - begin)
    // Noise: Every enchant announcement produced an ERROR timing record without a slow-operation threshold.
}

ItemAnnounceDB::ItemAnnounceDB() = default;

ItemAnnounceDB::~ItemAnnounceDB() = default;

ItemAnnounceItemInfo* ItemAnnounceDB::GetItemInfo(std::int32_t itemId) const {
    L2_GUARD_FUNCTION();

    const auto found = m_ItemInfos.find(itemId);
    return found != m_ItemInfos.end() ? found->second : nullptr;
}

ItemAnnounceRandomBoxInfo* ItemAnnounceDB::GetRandomBoxInfo(std::int32_t itemId) const {
    L2_GUARD_FUNCTION();

    const auto found = m_RandomBoxInfos.find(itemId);
    return found != m_RandomBoxInfos.end() ? found->second : nullptr;
}

void ItemAnnounceDB::Loader::AddItemInfo(ItemAnnounceItemInfo* info) {
    ItemAnnounceDB& database = ItemAnnounceDB::inst();
    database.m_ItemInfos[info->m_idItem] = info;
}

void ItemAnnounceDB::Loader::AddRandomBoxInfo(ItemAnnounceRandomBoxInfo* info) {
    ItemAnnounceDB& database = ItemAnnounceDB::inst();
    database.m_RandomBoxInfos[info->m_idItem] = info;
}

void ItemAnnounceDB::Load() {
    L2_GUARD_FUNCTION();

    ::CSecureStream input(kItemAnnouncePath, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ITEM_ANNOUNCE][%s][%d]Read from %s file failed.",
        //     kSourceFile,
        //     0x23,
        //     kItemAnnouncePath)
        // L2_LOG_ERROR(Log,
        //     L"[ITEM_ANNOUNCE][%s][%d]Read from %s file failed.",
        //     kSourceFile,
        //     0x23,
        //     kItemAnnouncePath)
        L2_LOG_ERROR(Log, "Cannot read ItemAnnounce.txt; item-announcement loading stops.");
        return;
    }

    CParserForItemAnnounceInfo parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ITEM_ANNOUNCE][%s][%d]Parsing failed",
        //     kSourceFile,
        //     0x2c)
        // L2_LOG_ERROR(Log,
        //     L"[ITEM_ANNOUNCE][%s][%d]Parsing failed",
        //     kSourceFile,
        //     0x2c)
        L2_LOG_ERROR(Log, "Cannot initialize the ItemAnnounce.txt parser; item-announcement loading stops.");
        return;
    }
    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ITEM_ANNOUNCE][%s][%d]Parsing failed : line[%d]",
        //     kSourceFile,
        //     0x37,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[ITEM_ANNOUNCE][%s][%d]Parsing failed : line[%d]",
        //     kSourceFile,
        //     0x37,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log,
            "Cannot parse ItemAnnounce.txt at script line %lld; already inserted announcement definitions are retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
}

void ItemAnnounceDB::Builder::Reload() {
    L2_GUARD_FUNCTION();

    ItemAnnounceDB::inst().m_ItemInfos.clear();
    ItemAnnounceDB::inst().m_RandomBoxInfos.clear();
    ItemAnnounceDB::inst().Load();
}
