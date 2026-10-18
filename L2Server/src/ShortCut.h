#pragma once

#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

class CItem;
class User;

enum ShortCutTypeEnum : std::int32_t {
    SCT_NONE = 0,
    SCT_ITEM = 1,
    SCT_SKILL = 2,
    SCT_ACTION = 3,
    SCT_MACRO = 4,
    SCT_RECIPE = 5,
    SCT_BOOKMARK = 6,
    SCT_ATTRIBUTE = 7,
    SCT_MAX = 8,
};

struct ShortCutStruct {
    ShortCutTypeEnum shortCutType;
    int shortCutID;
    int classID;
    int skillLevelSet;
    int skillReuseDelayGroup;
    int category;
    bool bIsEnabled;
};

static_assert(std::is_aggregate_v<ShortCutStruct>);
static_assert(std::is_trivially_copyable_v<ShortCutStruct>);
static_assert(std::is_trivially_destructible_v<ShortCutStruct>);

class CShortCut {
private:
    ShortCutStruct m_ShortCutSlot[144]; // +0x000
    ::CYieldLock m_Sect; // +0xfc0

public:
    explicit CShortCut(User& user);

    void Init();
    void Load(bool initialize);
    bool AddShortCut(int slot, int type, int id, int category, bool save, int skillLevelSet, int arg7, int arg8);
    void DeleteShortCut(int slot);
    void DeleteItemShortCut(int itemId);
    void DeleteRecipeShortCut(int recipeId);
    // No current standalone compiled body or caller. Retained declaration only.
    void DeleteSkillShortCut(int skillId);
    void DeleteBookmarkshortCut(int bookmarkId);
    void SendShortCutInfo();
    void SendUseSharedGroupItem(CItem* item);

private:
    void DeleteShortCut(ShortCutTypeEnum type, int id);

    User* m_User; // +0xfd0
};
