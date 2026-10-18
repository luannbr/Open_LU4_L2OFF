#pragma once

#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"

#include <cstddef>
#include <cstdint>

class CItem;
class CUserBroadcaster;


struct ItemAnnounceItemInfo {
    std::int32_t m_idItem;
    std::int32_t m_nAnnounceLevel;
};

struct ItemAnnounceRandomBoxInfo {
    std::int32_t m_idItem;
    std::int32_t m_nAnnounceLevelMin;
    std::vector<std::int32_t> m_fixedItems;
};

class ItemEnchantAnnouncer {
public:
    explicit ItemEnchantAnnouncer(CUserBroadcaster* broadcaster) noexcept : m_broadcaster(broadcaster) {}

    bool NeedToAnnounce(const CItem& item) const;
    void Announce(const char16_t* userName, const CItem& item) const;

private:
    CUserBroadcaster* m_broadcaster;
};


class ItemAnnounceDB : public ::Singleton<ItemAnnounceDB> {
public:
    using ItemInfoMap = ::std::map<std::int32_t, ItemAnnounceItemInfo*>;
    using RandomBoxInfoMap = ::std::map<std::int32_t, ItemAnnounceRandomBoxInfo*>;

    class Loader {
    public:
        static void AddItemInfo(ItemAnnounceItemInfo* info);
        static void AddRandomBoxInfo(ItemAnnounceRandomBoxInfo* info);
    };

    class Builder {
    public:
        static void Reload();
    };

    ItemAnnounceDB();
    ~ItemAnnounceDB() override;

    ItemAnnounceItemInfo* GetItemInfo(std::int32_t itemId) const;
    ItemAnnounceRandomBoxInfo* GetRandomBoxInfo(std::int32_t itemId) const;

    void Load();


private:
    ItemInfoMap m_ItemInfos;
    RandomBoxInfoMap m_RandomBoxInfos;
};
