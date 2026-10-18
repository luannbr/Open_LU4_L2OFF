#include "ItemAnnouncer.h"

#include "ItemAnnounceInfo.h"
#include "L2Packet.h"
#include "UserBroadcaster.h"
#include "UserPacketEx.h"


#include <string>
#include <string_view>

RandomBoxItemAnnouncer::RandomBoxItemAnnouncer(CUserBroadcaster* const broadcaster, ItemAnnounceDB* const database) noexcept
    :
    m_broadcaster(broadcaster), m_database(database) {}

bool RandomBoxItemAnnouncer::NeedToAnnounce(
    const ItemAnnounceDB& database, const int fromItemClassId, const int itemClassId
) {
    // Current L2Server.exe 0x0043ad28.
    L2_GUARD_FUNCTION();
    ItemAnnounceRandomBoxInfo* const randomBoxInfo = database.GetRandomBoxInfo(fromItemClassId);
    if (randomBoxInfo == nullptr) {
        return false;
    }
    for (const int fixedItem : randomBoxInfo->m_fixedItems) {
        if (fixedItem == itemClassId) {
            return true;
        }
    }
    ItemAnnounceItemInfo* const itemInfo = database.GetItemInfo(itemClassId);
    return itemInfo != nullptr && randomBoxInfo->m_nAnnounceLevelMin <= itemInfo->m_nAnnounceLevel;
}

bool RandomBoxItemAnnouncer::NeedToAnnounce(const int fromItemClassId, const int itemClassId) const {
    // Current L2Server.exe 0x0043ae24.
    L2_GUARD_FUNCTION();
    return NeedToAnnounce(*m_database, fromItemClassId, itemClassId);
}

void RandomBoxItemAnnouncer::Announce(const char16_t* const userName, const Announcement& announcement) const {
    // Current L2Server.exe 0x0043b188.
    L2_GUARD_FUNCTION();
    // const std::uint32_t begin = runtime::GetTickCount()

    protocol::S_EX_ITEM_ANNOUNCE packet{};
    // packet.cType = 1.
    packet.cType = EItemAnnounce::IA_REAR_ITEM;
    packet.sUserName.assign(std::u16string_view(userName));
    packet.nItemClassID = announcement.m_nItemClassID;
    packet.cEnchantCount = static_cast<char>(announcement.m_nEnchantCount);
    packet.nFromItemClassID = announcement.m_nFromItemClassID;
    m_broadcaster->BroadcastToAllUser(S_EX_ITEM_ANNOUNCE, packet);

    // Log.Add(
    //     LOG_ERROR,
    //     L"[PERF] %s@[%S][%d] performance : %ums",
    //     L"Announce Item Random box",
    //     "RandomBoxItemAnnouncer::Announce",
    //     0x90,
    //     runtime::GetTickCount() - begin)
    // L2_LOG_ERROR(Log,
    //     L"[PERF] %s@[%S][%d] performance : %ums",
    //     L"Announce Item Random box",
    //     "RandomBoxItemAnnouncer::Announce",
    //     0x90,
    //     runtime::GetTickCount() - begin)
    // Noise: Every random-box announcement produced an ERROR timing record without a slow-operation threshold.
}

void RandomBoxItemAnnouncer::Announce(const char16_t* const userName, const std::vector<Announcement>& announcements) const {
    // Current L2Server.exe 0x0043b310.
    L2_GUARD_FUNCTION();
    for (const Announcement& announcement : announcements) {
        Announce(userName, announcement);
    }
}

void RandomBoxItemAnnouncer::SelectAnnouncements(
    std::vector<Announcement>& announcements,
    const std::vector<int>& removedItemClassIds,
    const std::vector<std::pair<int, int>>& addedItems
) const {
    // Current L2Server.exe 0x0043b3dc.
    L2_GUARD_FUNCTION();
    announcements.clear();
    for (const int removedItemClassId : removedItemClassIds) {
        for (const std::pair<int, int>& addedItem : addedItems) {
            if (NeedToAnnounce(removedItemClassId, addedItem.first)) {
                announcements.push_back(Announcement{removedItemClassId, addedItem.first, addedItem.second});
            }
        }
    }
}
