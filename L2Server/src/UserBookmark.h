#pragma once

#include "Shared/src/BaseStructure.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"
#include "Skill.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

class CItem;
class CDBSocket;
class CSocket;
class CSkillInfo;
class User;

class CUserBookmarkSetting {
public:
    CUserBookmarkSetting() = default;
    CUserBookmarkSetting(const CUserBookmarkSetting&) = default;
    ~CUserBookmarkSetting();

    void SetTeleportSkill(const CSkillInfo* skillInfo);
    CSkillKey GetTeleportSkillKey();
    void SetTeleportItem(CItem* item);
    int HaveTeleportItem(User* user);
    void SetMaxSlotCount(int count);
    int GetMaxSlotCount();
    void SetAddItem(CItem* item);
    int HaveAddItem(User* user);

    CUserBookmarkSetting& operator=(const CUserBookmarkSetting&) = default;

private:
    CSkillKey m_skillKey; // +0x00
    int m_maxSlotCount; // +0x08
    std::vector<int> m_teleportItemList; // +0x10
    std::vector<int> m_AddItemList; // +0x30
};


extern CUserBookmarkSetting g_UserBookmarkSetting;

struct BookmarkSlot {
    BookmarkSlot();
    BookmarkSlot(const BookmarkSlot&) = default;
    BookmarkSlot(BookmarkSlot&&) noexcept = default;

    void Init();

    BookmarkSlot& operator=(const BookmarkSlot&) = default;
    BookmarkSlot& operator=(BookmarkSlot&&) noexcept = default;
    ~BookmarkSlot() = default;

    ::FVector pos; // +0x00
    std::u16string slotTitle; // +0x18
    int iconId; // +0x40
    std::u16string iconTitle; // +0x48
};


class CUserBookmark {
public:
    using BookmarkSlotMapType = ::std::map<int, BookmarkSlot*>;

    CUserBookmark();
    CUserBookmark(const CUserBookmark&) = default;
    ~CUserBookmark();

    void Load();
    bool CanAddSlot(int count);
    bool AddSlot(User* user, int count);
    bool SaveSlot(User* user, const char16_t* slotTitle, int iconId, const char16_t* iconTitle);
    bool _SaveSlot(User* user, const char16_t* slotTitle, int iconId, const char16_t* iconTitle);
    bool ModifySlot(User* user, int slotId, const char16_t* slotTitle, int iconId, const char16_t* iconTitle);
    bool DeleteSlot(User* user, int slotId);
    bool Teleport(User* user, int slotId);
    bool ChangeSlot(User* user, int fromSlotId, int toSlotId);
    void SendList(User* user);
    void OnLoadList(int count, const unsigned char* packet);
    void SetSlotCount(int count);
    bool TeleportTo(User* user);

    static bool CanUseBookmark(User* user);
    static bool CanSaveBookmark(User* user);
    static bool IsValidLocation(::FVector& position);

    static constexpr int MAX_SLOT = 30;
    static constexpr int BASE_SLOT_ID = 1;

private:
    BookmarkSlot* _FindSlot(int slotId) const;
    bool _IsValidSlotID(int slotId) const;

    BookmarkSlotMapType m_slotMap; // +0x00
    int m_slotCount; // +0x20
    ::CYieldLock m_bookmarkLock; // +0x24
    ::FVector m_reservedPos; // +0x38
};


class CUserBookmarkHandler {
public:
    static void OnLoadList(const unsigned char* packet);
    static bool DBPacketHandler(CDBSocket* socket, const unsigned char* packet);
    static bool CanAddSlot(User* user, int count);
    static bool AddSlot(User* user, int count);
    static void BuilderCmdHandler(User* user, char16_t* command);
    static void SendList(User* user);
    static void SaveSlot(User* user, const unsigned char* packet);
    static void ModifySlot(User* user, const unsigned char* packet);
    static void DeleteSlot(User* user, const unsigned char* packet);
    static void Teleport(User* user, const unsigned char* packet);
    static void ChangeSlot(User* user, const unsigned char* packet);
    static bool UserPacketHandler(CSocket* socket, const unsigned char* packet);
};
