#pragma once

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>
#include <utility>

class CUserBroadcaster;
class ItemAnnounceDB;

class RandomBoxItemAnnouncer {
public:
    struct Announcement {
        std::int32_t m_nFromItemClassID;
        std::int32_t m_nItemClassID;
        std::int32_t m_nEnchantCount;
    };

    RandomBoxItemAnnouncer(CUserBroadcaster* broadcaster, ItemAnnounceDB* database) noexcept;

    static bool NeedToAnnounce(const ItemAnnounceDB& database, int fromItemClassId, int itemClassId);
    bool NeedToAnnounce(int fromItemClassId, int itemClassId) const;
    void Announce(const char16_t* userName, const Announcement& announcement) const;
    void Announce(const char16_t* userName, const std::vector<Announcement>& announcements) const;
    void SelectAnnouncements(
        std::vector<Announcement>& announcements,
        const std::vector<int>& removedItemClassIds,
        const std::vector<std::pair<int, int>>& addedItems
    ) const;

private:
    CUserBroadcaster* m_broadcaster; // +0x00
    ItemAnnounceDB* m_database; // +0x08
};
