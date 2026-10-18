#include "UserBookmark.h"
#include "CConfig.h"
#include "CWorld.h"
#include "CreatureController.h"
#include "DB.h"
#include "Item.h"
#include "Path.h"
#include "Pledge.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/GeoData.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/net/Serialize.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>
#include <string_view>

namespace {

int CompareBookmarkWideIgnoreCase(const char16_t* left, const char16_t* right) {
#if defined(_WIN32)
    return ::_wcsicmp(Utf16ToWide(left).c_str(), Utf16ToWide(right).c_str());
#else
    while (*left != u'\0' && *right != u'\0') {
        const wint_t foldedLeft = std::towlower(*left);
        const wint_t foldedRight = std::towlower(*right);
        if (foldedLeft != foldedRight) {
            return foldedLeft < foldedRight ? -1 : 1;
        }
        ++left;
        ++right;
    }
    if (*left == *right) {
        return 0;
    }
    return *left == u'\0' ? -1 : 1;
#endif
}

int TakeBookmarkNumber(char16_t*& command, const int fallback) {
    char16_t* token = nullptr;
    command = ::GetLeftString(command, &token);
    return token != nullptr ? WindowsWtol(std::u16string_view(token)) : fallback;
}

const wchar_t* BookmarkAccountName(const std::array<char16_t, 15>& source, std::array<wchar_t, 16>& storage) noexcept {
#if defined(_WIN32)
    static_cast<void>(storage);
    return reinterpret_cast<const wchar_t*>(source.data());
#else
    storage.fill(wchar_t{});
    std::size_t output = 0;
    for (std::size_t input = 0; input < source.size() && source[input] != u'\0' && output + 1 < storage.size(); ++input) {
        const std::uint32_t first = source[input];
        if (first >= 0xd800U && first <= 0xdbffU && input + 1 < source.size()) {
            const std::uint32_t second = source[input + 1];
            if (second >= 0xdc00U && second <= 0xdfffU) {
                storage[output++] = static_cast<wchar_t>(0x10000U + ((first - 0xd800U) << 10U) + (second - 0xdc00U));
                ++input;
                continue;
            }
        }
        storage[output++] = static_cast<wchar_t>(first);
    }
    return storage.data();
#endif
}

std::u16string SanitizeBookmarkLogText(std::u16string_view value) {
    std::u16string text(value);
    std::replace(text.begin(), text.end(), u',', u' ');
    return text;
}

void WriteBookmarkModifySlotLog(User* user, const BookmarkSlot* slot, int fromSlotId, int toSlotId) {
    CLogFormatter formatter;
    formatter.WriteLog1(
        0x1f4c, user, nullptr, slot->slotTitle, slot->slotTitle, slot->iconTitle,
        runtime::Win64CvttDoubleToInt32(slot->pos.x), runtime::Win64CvttDoubleToInt32(slot->pos.y),
        runtime::Win64CvttDoubleToInt32(slot->pos.z), fromSlotId, toSlotId, 0, 0
    );
}

} // namespace

CUserBookmarkSetting g_UserBookmarkSetting;

CUserBookmarkSetting::~CUserBookmarkSetting() = default;

void CUserBookmarkSetting::SetTeleportSkill(const CSkillInfo* skillInfo) {
    L2_GUARD_FUNCTION();

    m_skillKey.m_nSkillId = static_cast<std::uint16_t>(skillInfo->m_nSkillId);
    m_skillKey.m_nLevel = static_cast<std::uint16_t>(skillInfo->m_nLevel);
}

CSkillKey CUserBookmarkSetting::GetTeleportSkillKey() { return m_skillKey; }

void CUserBookmarkSetting::SetTeleportItem(CItem* item) {
    L2_GUARD_FUNCTION();

    m_teleportItemList.push_back(item->m_data->m_nClassID);
}

int CUserBookmarkSetting::HaveTeleportItem(User* user) {
    // Current L2Server.exe 0x00bd3808.
    L2_GUARD_FUNCTION();
    for (const int itemClassId : m_teleportItemList) {
        if (user->m_inv.HaveItemByClassId(itemClassId, 1, false, false)) {
            return itemClassId;
        }
    }
    return 0;
}

void CUserBookmarkSetting::SetMaxSlotCount(int count) { m_maxSlotCount = count; }

int CUserBookmarkSetting::GetMaxSlotCount() { return m_maxSlotCount; }

void CUserBookmarkSetting::SetAddItem(CItem* item) {
    L2_GUARD_FUNCTION();

    if (item != nullptr) {
        m_AddItemList.push_back(item->m_data->m_nClassID);
    }
}

int CUserBookmarkSetting::HaveAddItem(User* user) {
    // Current L2Server.exe 0x00bd38d8.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return 0;
    }
    for (const int itemClassId : m_AddItemList) {
        if (user->m_inv.HaveItemByClassId(itemClassId, 1, false, false)) {
            return itemClassId;
        }
    }
    return 0;
}

BookmarkSlot::BookmarkSlot() : iconId(0) {}

CUserBookmark::CUserBookmark() : m_slotCount(0) {
    L2_GUARD_FUNCTION();
    m_slotMap.clear();
}

CUserBookmark::~CUserBookmark() {
    L2_GUARD_FUNCTION();

    L2_GUARD(m_bookmarkLock);
    for (auto& entry : m_slotMap) {
        delete entry.second;
    }
    m_slotMap.clear();
}

bool CUserBookmark::CanAddSlot(int count) {
    L2_GUARD_FUNCTION();

    L2_GUARD(m_bookmarkLock);
    const bool canAdd = runtime::AddInt32Wrapping(m_slotCount, count) <= g_UserBookmarkSetting.GetMaxSlotCount();
    return canAdd;
}

bool CUserBookmark::AddSlot(User* const user, const int count) {
    // Current L2Server.exe 0x00bd2eb4.
    L2_GUARD_FUNCTION();

    if (!CanAddSlot(count)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Try hack? too many slot. "
        //     L"now(%d) + want(%d) <= MAX(%d)",
        //     L"UserBookmark.cpp",
        //     0x5c,
        //     m_slotCount,
        //     count,
        //     g_UserBookmarkSetting.GetMaxSlotCount())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Try hack? too many slot. "
        //     L"now(%d) + want(%d) <= MAX(%d)",
        //     L"UserBookmark.cpp",
        //     0x5c,
        //     m_slotCount,
        //     count,
        //     g_UserBookmarkSetting.GetMaxSlotCount())
        L2_LOG_ERROR(
            Log, "Bookmark slot expansion rejected: current capacity %d, requested change %d, configured maximum %d.",
            m_slotCount, count, g_UserBookmarkSetting.GetMaxSlotCount()
        );
        return false;
    }

    L2_GUARD(m_bookmarkLock);
    db.AddUserBookmarkSlot(user, count);
    m_slotCount = runtime::AddInt32Wrapping(m_slotCount, count);
    user->SendSystemMessage(SystemMessageId::MY_TELEPORTS_SLOTS_EXPANDED);

    const FVector position = user->GetPos();
    std::array<wchar_t, 16> accountNameStorage{};
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,,",
    //     0x1f4d,
    //     user->GetDBID(),
    //     user->GetAccountID(),
    //     runtime::Win64CvttDoubleToInt32(position.x),
    //     runtime::Win64CvttDoubleToInt32(position.y),
    //     runtime::Win64CvttDoubleToInt32(position.z),
    //     user->m_data->m_nClass,
    //     user->m_data->m_Race,
    //     user->m_data->m_Sex,
    //     user->m_data->m_nLevel,
    //     count,
    //     m_slotCount,
    //     user->GetName(),
    //     BookmarkAccountName(
    //         user->m_sAccountName,
    //         accountNameStorage))
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,,",
        0x1f4d, user->GetDBID(), user->GetAccountID(), runtime::Win64CvttDoubleToInt32(position.x),
        runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z), user->m_data->m_nClass,
        user->m_data->m_Race, user->m_data->m_Sex, user->m_data->m_nLevel, count, m_slotCount, user->GetName(),
        BookmarkAccountName(user->m_sAccountName, accountNameStorage)
    );
    return true;
}

bool CUserBookmarkHandler::CanAddSlot(User* user, int count) {
    // Current L2Server 0x00bd3760.
    L2_GUARD_FUNCTION();
    return user->m_bookmark.CanAddSlot(count);
}

bool CUserBookmarkHandler::AddSlot(User* user, int count) {
    // Current L2Server 0x00bd41ac.
    L2_GUARD_FUNCTION();
    const bool added = user->m_bookmark.AddSlot(user, count);
    if (added) {
        user->m_bookmark.SendList(user);
    }
    return added;
}

namespace BuilderCmdHandler {

void OnAddSlot(User* user, char16_t* command) {
    // Current L2Server 0x00bd3610.
    L2_GUARD_FUNCTION();
    char16_t* countToken = nullptr;
    static_cast<void>(::GetLeftString(command, &countToken));
    if (countToken == nullptr) {
        user->GetSocket()->SendSystemMessage(u"SYS", u"//bookmark add_slot [slot_num]");
        return;
    }

    const int count = WindowsWtol(std::u16string_view(countToken));
    const bool accepted = user->m_bookmark.AddSlot(user, count);
    std::array<char16_t, 512> message{};
    FormatUtf16To(
        message.data(), message.size(),
        accepted ? u"To add %d bookmark slot is done." : u"To add %d bookmark slot is failed.", count
    );
    user->GetSocket()->SendSystemMessage(u"SYS", message.data());
}

void OnSendList(User* user, char16_t*) {
    // Current L2Server 0x00bd4104.
    L2_GUARD_FUNCTION();
    user->m_bookmark.SendList(user);
}

void OnChangeSlot(User* user, char16_t* command) {
    // Current L2Server 0x00bd5f3c.
    L2_GUARD_FUNCTION();
    const int fromSlotId = TakeBookmarkNumber(command, -1);
    const int toSlotId = TakeBookmarkNumber(command, -1);

    const char16_t* message = nullptr;
    if (fromSlotId < 1 || toSlotId < 1) {
        message = u"//bookmark change_slot [from_slot_num] [to_slot_num]";
    } else {
        message =
            user->m_bookmark.ChangeSlot(user, fromSlotId, toSlotId) ? u"Change slot is done" : u"Change slot is failed";
    }
    user->GetSocket()->SendSystemMessage(u"SYS", message);
}

} // namespace BuilderCmdHandler

void CUserBookmarkHandler::BuilderCmdHandler(User* user, char16_t* command) {
    // Current L2Server 0x00bd606c.
    L2_GUARD_FUNCTION();
    char16_t* action = nullptr;
    char16_t* const remaining = ::GetLeftString(command, &action);
    if (action == nullptr) {
        user->GetSocket()->SendSystemMessage(u"SYS", u"//bookmark command");
        return;
    }

    if (CompareBookmarkWideIgnoreCase(action, u"add_slot") == 0) {
        ::BuilderCmdHandler::OnAddSlot(user, remaining);
    }
    if (CompareBookmarkWideIgnoreCase(action, u"list") == 0) {
        ::BuilderCmdHandler::OnSendList(user, remaining);
    } else if (CompareBookmarkWideIgnoreCase(action, u"change_slot") == 0) {
        ::BuilderCmdHandler::OnChangeSlot(user, remaining);
    }
}

bool CUserBookmark::_SaveSlot(User* user, const char16_t* slotTitle, int iconId, const char16_t* iconTitle) {
    // Current L2Server.exe 0x00bd50c4.
    L2_GUARD_FUNCTION();

    const int previousSlotCount = static_cast<int>(m_slotMap.size());
    if (previousSlotCount >= m_slotCount) {
        user->SendSystemMessage(SystemMessageId::NO_FREE_TELEPORT_BOOKMARK_SLOT);
        return false;
    }

    int slotId = BASE_SLOT_ID;
    while (_FindSlot(slotId) != nullptr) {
        ++slotId;
    }

    FVector position = user->GetPos();
    BookmarkSlot* const slot = new BookmarkSlot;
    slot->iconId = iconId;
    slot->iconTitle.assign(iconTitle);
    slot->pos = position;
    slot->slotTitle.assign(slotTitle);
    static_cast<void>(m_slotMap.emplace(slotId, slot));
    db.SaveUserBookmarkSlot(user, slotId, position, iconId, slotTitle, iconTitle);

    const std::u16string sanitizedTitle = SanitizeBookmarkLogText(slotTitle);
    CLogFormatter formatter;
    formatter.WriteLog1(
        0x1f4a, user, nullptr, sanitizedTitle, nullptr, nullptr, m_slotCount, m_slotCount - previousSlotCount,
        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        runtime::Win64CvttDoubleToInt32(position.z), slotId, 0
    );
    return true;
}

bool CUserBookmark::SaveSlot(User* user, const char16_t* slotTitle, int iconId, const char16_t* iconTitle) {
    // Current L2Server.exe 0x00bd58dc.
    L2_GUARD_FUNCTION();
    if (!CanSaveBookmark(user)) {
        return false;
    }

    L2_GUARD(m_bookmarkLock);
    if (!g_bFreeTeleportNeedItem) {
        return _SaveSlot(user, slotTitle, iconId, iconTitle);
    }
    if (!_SaveSlot(user, slotTitle, iconId, iconTitle)) {
        return false;
    }
    const int itemClassId = g_UserBookmarkSetting.HaveAddItem(user);
    user->PayTeleportAddItem(itemClassId, -1);
    return true;
}

bool CUserBookmark::ModifySlot(User* user, int slotId, const char16_t* slotTitle, int iconId, const char16_t* iconTitle) {
    // Current L2Server.exe 0x00bd4a58.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_bookmarkLock);

    const auto position = m_slotMap.find(slotId);
    if (position == m_slotMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Try hack? This User doesnot have "
        //     L"char_id(%d) slot(%d). now(%d), max(%d)",
        //     L"UserBookmark.cpp",
        //     0xfe,
        //     user->GetDBID(),
        //     slotId,
        //     static_cast<int>(m_slotMap.size()),
        //     m_slotCount)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Try hack? This User doesnot have "
        //     L"char_id(%d) slot(%d). now(%d), max(%d)",
        //     L"UserBookmark.cpp",
        //     0xfe,
        //     user->GetDBID(),
        //     slotId,
        //     static_cast<int>(m_slotMap.size()),
        //     m_slotCount)
        L2_LOG_ERROR(
            Log, "Cannot modify bookmark slot %d for character %u: slot not found (%zu saved bookmarks, capacity %d).",
            slotId, static_cast<unsigned>(user->GetDBID()), m_slotMap.size(), m_slotCount
        );
        return false;
    }

    BookmarkSlot* const slot = position->second;
    const std::u16string oldTitle = SanitizeBookmarkLogText(slot->slotTitle);
    const std::u16string newTitle = SanitizeBookmarkLogText(slotTitle);
    const std::u16string newIconTitle = SanitizeBookmarkLogText(iconTitle);
    CLogFormatter formatter;
    formatter.WriteLog1(
        0x1f4c, user, nullptr, oldTitle, newTitle, newIconTitle, runtime::Win64CvttDoubleToInt32(slot->pos.x),
        runtime::Win64CvttDoubleToInt32(slot->pos.y), runtime::Win64CvttDoubleToInt32(slot->pos.z), 0, 0, 0, 0
    );

    slot->iconId = iconId;
    slot->slotTitle.assign(slotTitle);
    slot->iconTitle.assign(iconTitle);
    db.SaveUserBookmarkSlot(user, slotId, slot->pos, iconId, slotTitle, iconTitle);
    return true;
}

bool CUserBookmark::DeleteSlot(User* user, int slotId) {
    // Current L2Server.exe 0x00bd4274.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_bookmarkLock);

    const int previousSlotCount = static_cast<int>(m_slotMap.size());
    const auto position = m_slotMap.find(slotId);
    if (position == m_slotMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Try hack? This User doesnot have "
        //     L"slot(%d). now(%d), max(%d)",
        //     L"UserBookmark.cpp",
        //     0x140,
        //     slotId,
        //     static_cast<int>(m_slotMap.size()),
        //     m_slotCount)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Try hack? This User doesnot have "
        //     L"slot(%d). now(%d), max(%d)",
        //     L"UserBookmark.cpp",
        //     0x140,
        //     slotId,
        //     static_cast<int>(m_slotMap.size()),
        //     m_slotCount)
        L2_LOG_ERROR(
            Log, "Cannot delete bookmark slot %d for character %u: slot not found (%zu saved bookmarks, capacity %d).",
            slotId, static_cast<unsigned>(user->GetDBID()), m_slotMap.size(), m_slotCount
        );
        return false;
    }

    BookmarkSlot* const slot = position->second;
    db.DeleteUserBookmarkSlot(user, slotId);
    const std::u16string sanitizedTitle = SanitizeBookmarkLogText(slot->slotTitle);
    CLogFormatter formatter;
    formatter.WriteLog1(
        0x1f4b, user, nullptr, sanitizedTitle, nullptr, nullptr, m_slotCount, m_slotCount - previousSlotCount,
        runtime::Win64CvttDoubleToInt32(slot->pos.x), runtime::Win64CvttDoubleToInt32(slot->pos.y),
        runtime::Win64CvttDoubleToInt32(slot->pos.z), slotId, 0
    );
    delete slot;
    m_slotMap.erase(position);
    user->m_ShortCut.DeleteBookmarkshortCut(slotId);
    return true;
}

bool CUserBookmark::Teleport(User* user, int slotId) {
    // Current L2Server.exe 0x00bd455c.
    L2_GUARD_FUNCTION();
    if (!CanUseBookmark(user) || user->GetController()->GetCurrentActionType() != ACT_NOTHING) {
        return false;
    }

    const int itemClassId = [&] {
        L2_WRITE_GUARD(user->m_inv);
        const int itemClassId = g_UserBookmarkSetting.HaveTeleportItem(user);
        if (itemClassId < 1) {
            user->SendSystemMessage(SystemMessageId::CANNOT_TELEPORT_REQUIRED_ITEM_MISSING);
        }
        return itemClassId;
    }();
    if (itemClassId < 1) {
        return false;
    }

    L2_GUARD(m_bookmarkLock);
    BookmarkSlot* const slot = _FindSlot(slotId);
    if (slot == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Try hack? This User doesnot have "
        //     L"slot(%d). now(%d), max(%d)",
        //     L"UserBookmark.cpp",
        //     0x180,
        //     slotId,
        //     static_cast<int>(m_slotMap.size()),
        //     m_slotCount)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Try hack? This User doesnot have "
        //     L"slot(%d). now(%d), max(%d)",
        //     L"UserBookmark.cpp",
        //     0x180,
        //     slotId,
        //     static_cast<int>(m_slotMap.size()),
        //     m_slotCount)
        L2_LOG_ERROR(
            Log, "Bookmark teleport rejected for character %u: slot %d not found (%zu saved bookmarks, capacity %d).",
            static_cast<unsigned>(user->GetDBID()), slotId, m_slotMap.size(), m_slotCount
        );
        return false;
    }
    if (!IsValidLocation(slot->pos)) {
        user->SendSystemMessage(SystemMessageId::DESTINATION_UNREACHABLE_BY_MY_TELEPORTS);
        return false;
    }

    m_reservedPos = slot->pos;
    const CSkillKey skillKey = g_UserBookmarkSetting.GetTeleportSkillKey();
    if (skillKey.m_nSkillId <= 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Bookmark Teleport Skill is not set",
        //     L"UserBookmark.cpp",
        //     0x177)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Bookmark Teleport Skill is not set",
        //     L"UserBookmark.cpp",
        //     0x177)
        L2_LOG_ERROR(
            Log,
            "Bookmark teleport cannot start for character %u, slot %d: teleport skill is not configured; the reserved "
            "position remains set.",
            static_cast<unsigned>(user->GetDBID()), slotId
        );
        return false;
    }
    SkillUseInfo skillUseInfo;
    skillUseInfo.m_skillID = skillKey.m_nSkillId;
    skillUseInfo.m_skillLevel = skillKey.m_nLevel;
    skillUseInfo.m_skillSubLevel = skillKey.m_nSubLevel;
    user->GetController()->Skill(skillUseInfo, false, true, 0);
    return true;
}

bool CUserBookmark::ChangeSlot(User* user, int fromSlotId, int toSlotId) {
    // Current L2Server.exe 0x00bd5a68.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_bookmarkLock);
    if (!_IsValidSlotID(fromSlotId) || !_IsValidSlotID(toSlotId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid from slot id: %d, %d",
        //     fromSlotId,
        //     toSlotId)
        // L2_LOG_ERROR(Log,
        //     L"Invalid from slot id: %d, %d",
        //     fromSlotId,
        //     toSlotId)
        L2_LOG_ERROR(
            Log,
            "Bookmark slot change rejected for character %u: source %d or destination %d is outside the allowed range "
            "%d..%d.",
            static_cast<unsigned>(user->GetDBID()), fromSlotId, toSlotId, static_cast<int>(BASE_SLOT_ID), m_slotCount
        );
        return false;
    }

    BookmarkSlot* const fromSlot = _FindSlot(fromSlotId);
    if (fromSlot == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid from slot: %d",
        //     fromSlotId)
        // L2_LOG_ERROR(Log,
        //     L"Invalid from slot: %d",
        //     fromSlotId)
        L2_LOG_ERROR(
            Log, "Bookmark slot change rejected for character %u: source slot %d not found (destination %d).",
            static_cast<unsigned>(user->GetDBID()), fromSlotId, toSlotId
        );
        return false;
    }
    BookmarkSlot* const toSlot = _FindSlot(toSlotId);
    db.ChangeUserBookmarkSlot(user, fromSlotId, toSlotId);
    m_slotMap[toSlotId] = fromSlot;
    if (toSlot == nullptr) {
        m_slotMap.erase(fromSlotId);
    } else {
        m_slotMap[fromSlotId] = toSlot;
    }
    WriteBookmarkModifySlotLog(user, fromSlot, fromSlotId, toSlotId);
    if (toSlot != nullptr) {
        WriteBookmarkModifySlotLog(user, toSlot, toSlotId, fromSlotId);
    }
    return true;
}

void CUserBookmark::SetSlotCount(int count) {
    L2_GUARD_FUNCTION();

    if (static_cast<std::uint32_t>(count) < static_cast<std::uint32_t>(MAX_SLOT + 1)) {
        L2_GUARD(m_bookmarkLock);
        m_slotCount = count;
    }
}

BookmarkSlot* CUserBookmark::_FindSlot(int slotId) const {
    L2_GUARD_FUNCTION();

    const auto position = m_slotMap.find(slotId);
    return position == m_slotMap.end() ? nullptr : position->second;
}

bool CUserBookmark::_IsValidSlotID(int slotId) const { return BASE_SLOT_ID <= slotId && slotId <= m_slotCount; }

void CUserBookmark::SendList(User* user) {
    L2_GUARD_FUNCTION();

    std::array<char, 0x5000> buffer{};
    int packetLength = 0;
    int bookmarkCount = 0;

    {
        L2_GUARD(m_bookmarkLock);
        for (const auto& entry : m_slotMap) {
            const BookmarkSlot* const slot = entry.second;
            if (slot == nullptr) {
                continue;
            }

            ++bookmarkCount;
            packetLength += ::Assemble(
                buffer.data() + packetLength, static_cast<int>(buffer.size()) - packetLength, "ddddSdS", entry.first,
                static_cast<int>(slot->pos.x), static_cast<int>(slot->pos.y), static_cast<int>(slot->pos.z),
                slot->slotTitle.c_str(), slot->iconId, slot->iconTitle.c_str()
            );
        }
    }

    CUserSocket* const socket = user->GetSocket();
    if (bookmarkCount < 1) {
        socket->Send("chddd", 0xfe, 0x85, 0, m_slotCount, bookmarkCount);
    } else {
        socket->Send("chdddb", 0xfe, 0x85, 0, m_slotCount, bookmarkCount, packetLength, buffer.data());
    }
}

void CUserBookmarkHandler::SendList(User* user) {
    // Current L2Server.exe 0x00bd405c.
    L2_GUARD_FUNCTION();
    user->m_bookmark.SendList(user);
}

void CUserBookmarkHandler::SaveSlot(User* user, const unsigned char* packet) {
    // Current L2Server.exe 0x00bd5c78.
    L2_GUARD_FUNCTION();
    std::array<char16_t, 65> slotTitle{};
    std::array<char16_t, 5> iconTitle{};
    int iconId = 0;
    ::Disassemble(
        packet, "SdS", static_cast<int>(sizeof(slotTitle)), slotTitle.data(), &iconId, static_cast<int>(sizeof(iconTitle)),
        iconTitle.data()
    );
    if (user->m_bookmark.SaveSlot(user, slotTitle.data(), iconId, iconTitle.data())) {
        user->m_bookmark.SendList(user);
    }
}

void CUserBookmarkHandler::ModifySlot(User* user, const unsigned char* packet) {
    // Current L2Server.exe 0x00bd4e20.
    L2_GUARD_FUNCTION();
    std::array<char16_t, 65> slotTitle{};
    std::array<char16_t, 5> iconTitle{};
    int slotId = 0;
    int iconId = 0;
    ::Disassemble(
        packet, "dSdS", &slotId, static_cast<int>(sizeof(slotTitle)), slotTitle.data(), &iconId,
        static_cast<int>(sizeof(iconTitle)), iconTitle.data()
    );
    if (user->m_bookmark.ModifySlot(user, slotId, slotTitle.data(), iconId, iconTitle.data())) {
        user->m_bookmark.SendList(user);
    }
}

void CUserBookmarkHandler::DeleteSlot(User* user, const unsigned char* packet) {
    // Current L2Server.exe 0x00bd484c.
    L2_GUARD_FUNCTION();
    int slotId = 0;
    ::Disassemble(packet, "d", &slotId);
    if (user->m_bookmark.DeleteSlot(user, slotId)) {
        user->m_bookmark.SendList(user);
    }
}

void CUserBookmarkHandler::Teleport(User* user, const unsigned char* packet) {
    // Current L2Server.exe 0x00bd4924.
    L2_GUARD_FUNCTION();
    int slotId = 0;
    ::Disassemble(packet, "d", &slotId);
    static_cast<void>(user->m_bookmark.Teleport(user, slotId));
}

void CUserBookmarkHandler::ChangeSlot(User* user, const unsigned char* packet) {
    // Current L2Server.exe 0x00bd5db8.
    L2_GUARD_FUNCTION();
    int fromSlotId = 0;
    int toSlotId = 0;
    ::Disassemble(packet, "dd", &fromSlotId, &toSlotId);
    if (user->m_bookmark.ChangeSlot(user, fromSlotId, toSlotId)) {
        user->m_bookmark.SendList(user);
    }
}

bool CUserBookmarkHandler::UserPacketHandler(CSocket* socket, const unsigned char* packet) {
    // Current L2Server.exe 0x00bd61ac.
    L2_GUARD_FUNCTION();
    auto* const userSocket = static_cast<CUserSocket*>(socket);
    User* const user = userSocket != nullptr ? userSocket->GetUser() : nullptr;
    if (user == nullptr) {
        return true;
    }
    if (User::IsInFifthClassGroup(static_cast<ClassType>(user->GetClass()))) {
        user->SendSystemMessage(SystemMessageId::FUNCTION_NOT_YET_AVAILABLE_IN_CURRENT_STATE);
        return false;
    }
    if (user->m_data->m_TrainingState == 1) {
        user->SendSystemMessage(SystemMessageId::CANNOT_TAKE_OTHER_ACTIONS_WHILE_IN_TRAINING_CAMP);
        return false;
    }

    int action = 0;
    const unsigned char* const cursor = ::Disassemble(packet, "d", &action);
    switch (action) {
        case 0:
            SendList(user);
            break;
        case 1:
            SaveSlot(user, cursor);
            break;
        case 2:
            ModifySlot(user, cursor);
            break;
        case 3:
            DeleteSlot(user, cursor);
            break;
        case 4:
            Teleport(user, cursor);
            break;
        case 5:
            ChangeSlot(user, cursor);
            break;
        default:
            return true;
    }
    return false;
}

void CUserBookmark::OnLoadList(const int count, const unsigned char* packet) {
    // Current L2Server.exe 0x00bd53e8.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 65> slotTitle{};
    std::array<char16_t, 5> iconTitle{};
    L2_GUARD(m_bookmarkLock);
    m_slotMap.clear();

    for (int index = 0; index < count; ++index) {
        int slotId;
        int x;
        int y;
        int z;
        int iconId;
        packet = ::Disassemble(
            packet, "dddddSS", &slotId, &x, &y, &z, &iconId, static_cast<int>(sizeof(slotTitle)), slotTitle.data(),
            static_cast<int>(sizeof(iconTitle)), iconTitle.data()
        );
        if (slotId < BASE_SLOT_ID) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid slotId[%d]",
            //     L"UserBookmark.cpp",
            //     0x256,
            //     slotId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid slotId[%d]",
            //     L"UserBookmark.cpp",
            //     0x256,
            //     slotId)
            L2_LOG_ERROR(
                Log, "Skipping loaded bookmark at index %d (%d entries): slot ID %d is below minimum %d.", index, count,
                slotId, static_cast<int>(BASE_SLOT_ID)
            );
            continue;
        }

        BookmarkSlot* const slot = new BookmarkSlot;
        slot->iconId = iconId;
        slot->iconTitle.assign(std::u16string_view(iconTitle.data()));
        slot->pos = FVector(x, y, z);
        slot->slotTitle.assign(std::u16string_view(slotTitle.data()));
        static_cast<void>(m_slotMap.emplace(slotId, slot));
    }
}

void CUserBookmarkHandler::OnLoadList(const unsigned char* packet) {
    // Current L2Server.exe 0x00bd56a4.
    L2_GUARD_FUNCTION();

    int userServerId;
    int count;
    packet = ::Disassemble(packet, "dd", &userServerId, &count);
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"UserBookmark.cpp",
        //     0x3de,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"UserBookmark.cpp",
        //     0x3de,
        //     userServerId)
        L2_LOG_ERROR(
            Log, "Ignoring bookmark list reply for object %u: no player found; %d entries are not loaded.",
            static_cast<unsigned>(userServerId), count
        );
        return;
    }

    user->m_bookmark.OnLoadList(count, packet);
    user->m_bookmark.SendList(user);
}

bool CUserBookmarkHandler::DBPacketHandler(CDBSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x00bd5e8c.
    L2_GUARD_FUNCTION();

    int job;
    packet = ::Disassemble(packet, "d", &job);
    if (job == 0) {
        OnLoadList(packet);
    }
    return false;
}

bool CUserBookmark::TeleportTo(User* user) {
    // Current L2Server.exe 0x00bd39b4.
    L2_GUARD_FUNCTION();

    FVector position;
    {
        L2_GUARD(m_bookmarkLock);
        position = m_reservedPos;
        m_reservedPos = FVector{};
    }
    if (position.IsZero() || !CanUseBookmark(user)) {
        return false;
    }

    const int itemClassId = [&] {
        L2_WRITE_GUARD(user->m_inv);
        const int itemClassId = g_UserBookmarkSetting.HaveTeleportItem(user);
        return itemClassId;
    }();
    if (itemClassId < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Cannot find bookmark teleport item [%d]",
        //     L"UserBookmark.cpp",
        //     0x1a7,
        //     itemClassId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Cannot find bookmark teleport item [%d]",
        //     L"UserBookmark.cpp",
        //     0x1a7,
        //     itemClassId)
        L2_LOG_ERROR(
            Log,
            "Bookmark teleport canceled for character %u: no configured teleport item is available (lookup result %d); "
            "reserved position has been cleared.",
            static_cast<unsigned>(user->GetDBID()), itemClassId
        );
        return false;
    }

    user->GetController()->CancelReservedAction();
    FAngle direction = user->GetDir();
    direction.Yaw = static_cast<std::uint16_t>(direction.Yaw);
    return user->TeleportToLocationWithItem(position, direction.Yaw, itemClassId, 1);
}

bool CUserBookmark::CanUseBookmark(User* user) {
    // Current L2Server.exe 0x00bd32ec.
    L2_GUARD_FUNCTION();
    if (user->m_data->m_bIsInCombatMode) {
        user->SendSystemMessage(SystemMessageId::CANNOT_USE_TELEPORT_BOOKMARK_DURING_COMBAT);
        return false;
    }
    if (!user->IsNoSiegeRelationForBookmark() || user->IsInFortressSiege() || user->IsInAgitWar(false)) {
        user->SendSystemMessage(SystemMessageId::CANNOT_USE_TELEPORT_BOOKMARK_DURING_SIEGE);
        return false;
    }
    if (user->IsInDuel()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_USE_TELEPORT_BOOKMARK_DURING_DUEL);
        return false;
    }
    if (user->IsRidingWyvern() || user->IsInAirShip() || static_cast<int>(user->m_data->m_moveType) == CMT_FLY ||
        user->IsInAirShip()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_USE_MY_TELEPORTS_WHILE_FLYING);
        return false;
    }
    if (user->IsOlympiadPlaying()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_USE_MY_TELEPORTS_DURING_OLYMPIAD);
        return false;
    }
    if (user->m_PledgeGameUser.IsPlaying()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_USE_TELEPORT_BOOKMARK_DURING_COMBAT);
        return false;
    }
    if (user->m_SkillMod.m_nBlockMoveCount != 0 || !user->m_data->m_bCanMove || !user->m_SkillMod.m_bCanAct) {
        user->SendSystemMessage(SystemMessageId::CANNOT_TELEPORT_WHILE_PETRIFIED_OR_PARALYZED);
        return false;
    }
    if (!user->IsActive()) {
        user->SendSystemMessage(
            user->m_data->m_bIsAlive ? SystemMessageId::ANOTHER_TELEPORT_IN_PROGRESS
                                     : SystemMessageId::CANNOT_USE_MY_TELEPORTS_WHILE_DEAD
        );
        return false;
    }
    if (user->m_AreaMod.m_bIsUnderWater) {
        user->SendSystemMessage(SystemMessageId::CANNOT_USE_MY_TELEPORTS_UNDERWATER);
        return false;
    }
    if (!user->IsInRealWorld()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_USE_MY_TELEPORTS_IN_INSTANCE);
        return false;
    }
    if (user->m_bFlyMoving) {
        user->SendSystemMessage(SystemMessageId::CANNOT_ACT_WHILE_USING_SAYUNE);
        return false;
    }

    const FVector position = user->GetPos();
    const int mapX = (runtime::AddInt32Wrapping(runtime::Win64CvttDoubleToInt32(position.x), 0x50000) >> 15) + 10;
    const int mapY = (runtime::AddInt32Wrapping(runtime::Win64CvttDoubleToInt32(position.y), 0x40000) >> 15) + 10;
    CGeoZone* const zone = CWorld::Inst().GetGeo()->GetZone(mapX, mapY);
    if (zone != nullptr && !zone->IsInNoUseBookmarkArea(position)) {
        return true;
    }
    user->SendSystemMessage(SystemMessageId::CANNOT_USE_MY_TELEPORTS_IN_CURRENT_AREA);
    return false;
}

bool CUserBookmark::IsValidLocation(FVector& position) {
    // Current L2Server.exe 0x00bd2bb8.
    L2_GUARD_FUNCTION();
    const int mapX = (runtime::AddInt32Wrapping(runtime::Win64CvttDoubleToInt32(position.x), 0x50000) >> 15) + 10;
    if (mapX <= 14 || !g_pnWorld.FindPathNodeTest(position)) {
        return false;
    }
    const int mapY = (runtime::AddInt32Wrapping(runtime::Win64CvttDoubleToInt32(position.y), 0x40000) >> 15) + 10;
    CGeoZone* const zone = CWorld::Inst().GetGeo()->GetZone(mapX, mapY);
    return zone != nullptr && !zone->IsInNoSaveBookmarkArea(position);
}

bool CUserBookmark::CanSaveBookmark(User* user) {
    // Current L2Server.exe 0x00bd3f10.
    L2_GUARD_FUNCTION();
    if (g_bFreeTeleportNeedItem) {
        const int itemClassId = [&] {
            L2_WRITE_GUARD(user->m_inv);
            const int itemClassId = g_UserBookmarkSetting.HaveAddItem(user);
            if (itemClassId < 1) {
                user->SendSystemMessage(SystemMessageId::CANNOT_SAVE_BOOKMARK_MY_TELEPORT_FLAG_MISSING);
            }
            return itemClassId;
        }();
        if (itemClassId < 1) {
            return false;
        }
    }
    if (!CanUseBookmark(user)) {
        return false;
    }
    FVector position = user->GetPos();
    if (!IsValidLocation(position)) {
        user->SendSystemMessage(SystemMessageId::CANNOT_USE_MY_TELEPORTS_IN_CURRENT_AREA);
        return false;
    }
    return true;
}
