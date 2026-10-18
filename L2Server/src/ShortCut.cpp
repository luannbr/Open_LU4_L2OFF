#include "ShortCut.h"

#include "Shared/src/TextRuntime.h"

#include "DB.h"
#include "Item.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/SharedFactory.h"

#include <cstdint>

CShortCut::CShortCut(User& user) : m_User(&user) {
    L2_GUARD_FUNCTION();
    Init();
}

void CShortCut::Init() {
    L2_GUARD_FUNCTION();

    for (ShortCutStruct& shortcut : m_ShortCutSlot) {
        shortcut.shortCutType = SCT_NONE;
        shortcut.bIsEnabled = false;
    }
}

void CShortCut::Load(bool initialize) {
    L2_GUARD_FUNCTION();
    if (initialize) {
        Init();
    }
    db.RequestShortCutInfo(m_User);
}

bool CShortCut::AddShortCut(int slot, int type, int id, int category, bool save, int skillLevelSet, int arg7, int arg8) {
    L2_GUARD_FUNCTION();

    if (static_cast<unsigned int>(slot) >= 144U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] slotnum = %d > %d "
        //     L"(type[%d], id[%d], user[%s])",
        //     L"ShortCut.cpp",
        //     0x46,
        //     slot,
        //     144,
        //     type,
        //     id,
        //     m_User->GetName())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] slotnum = %d > %d "
        //     L"(type[%d], id[%d], user[%s])",
        //     L"ShortCut.cpp",
        //     0x46,
        //     slot,
        //     144,
        //     type,
        //     id,
        //     m_User->GetName())
        // L2_LOG_ERROR(Log,
        //     "Shortcut add rejected for character '%s' (DBID %u): slot %d is outside 0..143 (type %d, ID %d).",
        //     WideToUtf8(std::wstring_view(m_User->GetName())).c_str(),
        //     static_cast<unsigned>(m_User->GetDBID()), slot, type, id)
        L2_LOG_ERROR(
            Log, "Shortcut add rejected for character '%s' (DBID %u): slot %d is outside 0..143 (type %d, ID %d).",
            Utf16ToUtf8(m_User->GetName()).c_str(), static_cast<unsigned>(m_User->GetDBID()), slot, type, id
        );
        return false;
    }

    int storedId = 0;
    int classId = 0;
    int skillReuseDelayGroup = -1;
    bool isLockedSkill = false;
    bool skillLevelWasSaved = false;

    int itemDelayShareGroup = 0;
    int itemReuseRemainTime = 0;
    int itemReuseDelaySeconds = 0;
    int variationOption1 = 0;
    int variationOption2 = 0;
    int shapeShiftedClassId = 0;

    if (type == SCT_ITEM) {
        CItem* const item = ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(id));
        if (item == nullptr) {
            return false;
        }

        storedId = static_cast<int>(item->GetDBID());
        classId = item->m_nClassId;
        itemDelayShareGroup = item->m_const->m_nDelayShareGroup;
        itemReuseRemainTime = m_User->GetReuseRemainTime(itemDelayShareGroup);
        itemReuseDelaySeconds = item->m_const->m_nReuseDelay / 1000;
        const ItemOptionKey optionKey = item->m_itemOption.itemOptionKey;
        variationOption1 = optionKey.variationOption1;
        variationOption2 = optionKey.variationOption2;
        shapeShiftedClassId = item->m_data->GetShapeShiftedClassId();
    } else {
        storedId = id;
        classId = id;
        if (type == SCT_SKILL) {
            const std::uint32_t packedLevel = static_cast<std::uint32_t>(skillLevelSet);
            if (skillLevelSet < 1 || (packedLevel & 0xffffU) > 99U ||
                static_cast<std::int32_t>(packedLevel & 0xffff0000U) > 0x27100000) {
                skillLevelSet = 1;
            }

            const int skillLevel = m_User->GetSkillLevel(id);
            const int skillSubLevel = m_User->GetSkillSubLevel(id);
            const std::uint32_t currentLevel =
                (static_cast<std::uint32_t>(skillSubLevel) << 16U) | (static_cast<std::uint32_t>(skillLevel) & 0xffffU);
            if (static_cast<std::uint32_t>(skillLevelSet) != currentLevel && skillLevel > -1 && skillSubLevel > -1) {
                skillLevelSet = static_cast<int>(currentLevel);
                db.RequestSetShortCut(m_User, slot, SCT_SKILL, id, category, skillLevelSet);
                skillLevelWasSaved = true;
            }

            skillReuseDelayGroup = m_User->GetSkillReuseDelayGroup(id);
            isLockedSkill = m_User->IsLockedSkill(id) != 0;
        } else if (type < SCT_ACTION || type > SCT_BOOKMARK) {
            return false;
        }
    }

    {
        L2_GUARD(m_Sect);
        ShortCutStruct& shortcut = m_ShortCutSlot[slot];
        shortcut.shortCutType = static_cast<ShortCutTypeEnum>(type);
        shortcut.shortCutID = storedId;
        shortcut.classID = classId;
        shortcut.skillLevelSet = skillLevelSet;
        shortcut.skillReuseDelayGroup = skillReuseDelayGroup;
        shortcut.bIsEnabled = true;
        shortcut.category = category;
    }

    if (!save) {
        return true;
    }

    if (!skillLevelWasSaved) {
        db.RequestSetShortCut(m_User, slot, type, storedId, category, skillLevelSet);
    }

    CUserSocket* const socket = m_User->GetSocket();
    if (type == SCT_ITEM) {
        socket->Send(
            "cdddddddddd", 0x44, SCT_ITEM, slot, id, category + 1, itemDelayShareGroup, itemReuseRemainTime,
            itemReuseDelaySeconds, variationOption1, variationOption2, shapeShiftedClassId
        );
    } else if (type == SCT_SKILL) {
        socket->Send(
            "cdddddcddd", 0x44, SCT_SKILL, slot, id, skillLevelSet, skillReuseDelayGroup, isLockedSkill, category + 1, arg7,
            arg8
        );
    } else if (type >= SCT_ACTION && type <= SCT_BOOKMARK) {
        socket->Send("cdddd", 0x44, type, slot, id, category + 1);
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] what shortcuttype(%d)?",
        //     L"ShortCut.cpp",
        //     0xd5,
        //     type)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] what shortcuttype(%d)?",
        //     L"ShortCut.cpp",
        //     0xd5,
        //     type)
        L2_LOG_ERROR(
            Log,
            "Cannot send shortcut update for character %u, slot %d: unsupported type %d; the add operation still returns "
            "success.",
            static_cast<unsigned>(m_User->GetDBID()), slot, type
        );
    }

    return true;
}

void CShortCut::DeleteItemShortCut(int) { L2_GUARD_FUNCTION(); }

void CShortCut::DeleteShortCut(int slot) {
    L2_GUARD_FUNCTION();
    if (static_cast<unsigned int>(slot) < 144U) {
        {
            L2_GUARD(m_Sect);
            m_ShortCutSlot[slot].shortCutType = SCT_NONE;
        }
        db.RequestSetShortCut(m_User, slot, 0, 0, 0, 0);
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] slotnum = %d > %d, or slotnum < 0 user[%s]",
    //     L"ShortCut.cpp",
    //     0x124,
    //     slot,
    //     144,
    //     m_User->GetName())
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] slotnum = %d > %d, or slotnum < 0 user[%s]",
    //     L"ShortCut.cpp",
    //     0x124,
    //     slot,
    //     144,
    //     m_User->GetName())
    // L2_LOG_ERROR(Log,
    //     "Shortcut deletion rejected for character '%s' (DBID %u): slot %d is outside 0..143.",
    //     WideToUtf8(std::wstring_view(m_User->GetName())).c_str(),
    //     static_cast<unsigned>(m_User->GetDBID()), slot)
    L2_LOG_ERROR(
        Log, "Shortcut deletion rejected for character '%s' (DBID %u): slot %d is outside 0..143.",
        Utf16ToUtf8(m_User->GetName()).c_str(), static_cast<unsigned>(m_User->GetDBID()), slot
    );
}

void CShortCut::SendUseSharedGroupItem(CItem* item) {
    L2_GUARD_FUNCTION();
    if (item == nullptr) {
        return;
    }

    const int delayShareGroup = item->m_const->m_nDelayShareGroup;
    CUserSocket* const socket = m_User->GetSocket();
    const int reuseDelaySeconds = item->m_const->m_nReuseDelay / 1000;
    const int reuseRemainTime = m_User->GetReuseRemainTime(delayShareGroup);
    socket->Send("chdddd", 0xfe, 0x4b, item->m_data->m_nClassID, delayShareGroup, reuseRemainTime, reuseDelaySeconds);
}

void CShortCut::SendShortCutInfo() {
    L2_GUARD_FUNCTION();

    char buffer[8192];
    int bufferSize = 0;
    int shortcutCount = 0;
    m_User->SendItemList(false, false, nullptr);

    {
        L2_GUARD(m_Sect);
        for (int slot = 0; slot < 144; ++slot) {
            ShortCutStruct& shortcut = m_ShortCutSlot[slot];
            const int type = shortcut.shortCutType;
            if (type < SCT_ITEM || type > SCT_ATTRIBUTE) {
                continue;
            }

            if (type == SCT_ITEM) {
                CItem* const item = m_User->m_inv.GetByDBID(shortcut.shortCutID);
                if (item == nullptr) {
                    continue;
                }

                const ItemOptionKey optionKey = item->m_itemOption.itemOptionKey;
                const int delayShareGroup = item->m_const->m_nDelayShareGroup;
                const int reuseRemainTime = m_User->GetReuseRemainTime(delayShareGroup);
                const int reuseDelaySeconds = item->m_const->m_nReuseDelay / 1000;
                const int shapeShiftedClassId = item->m_data->GetShapeShiftedClassId();
                bufferSize += ::Assemble(
                    buffer + bufferSize, 8192 - bufferSize, "dddddddddd", type, slot, item->Id(), shortcut.category + 1,
                    delayShareGroup, reuseRemainTime, reuseDelaySeconds, optionKey.variationOption1,
                    optionKey.variationOption2, shapeShiftedClassId
                );
            } else if (type == SCT_SKILL) {
                const int skillLevel = m_User->GetSkillLevel(shortcut.shortCutID);
                const int skillSubLevel = m_User->GetSkillSubLevel(shortcut.shortCutID);
                const std::uint32_t currentLevel =
                    (static_cast<std::uint32_t>(skillSubLevel) << 16U) | (static_cast<std::uint32_t>(skillLevel) & 0xffffU);
                if (static_cast<std::uint32_t>(shortcut.skillLevelSet) != currentLevel && skillLevel > -1 &&
                    skillSubLevel > -1) {
                    shortcut.skillLevelSet = static_cast<int>(currentLevel);
                    db.RequestSetShortCut(
                        m_User, slot, type, shortcut.shortCutID, shortcut.category, shortcut.skillLevelSet
                    );
                }

                const bool isLockedSkill = m_User->IsLockedSkill(shortcut.shortCutID) != 0;
                bufferSize += ::Assemble(
                    buffer + bufferSize, 8192 - bufferSize, "dddddcd", type, slot, shortcut.shortCutID,
                    shortcut.skillLevelSet, shortcut.skillReuseDelayGroup, isLockedSkill, shortcut.category + 1
                );
            } else if (type >= SCT_ACTION && type <= SCT_BOOKMARK) {
                bufferSize += ::Assemble(
                    buffer + bufferSize, 8192 - bufferSize, "dddd", type, slot, shortcut.classID, shortcut.category + 1
                );
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] what shortcuttype(%d)?",
                //     L"ShortCut.cpp",
                //     0x21e,
                //     type)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] what shortcuttype(%d)?",
                //     L"ShortCut.cpp",
                //     0x21e,
                //     type)
                L2_LOG_ERROR(
                    Log,
                    "Cannot encode shortcut list entry for character %u, slot %d: unsupported type %d; the entry count "
                    "still advances.",
                    static_cast<unsigned>(m_User->GetDBID()), slot, type
                );
            }
            ++shortcutCount;
        }
    }

    m_User->GetSocket()->Send("cdb", 0x45, shortcutCount, bufferSize, buffer);
}

void CShortCut::DeleteShortCut(ShortCutTypeEnum type, int id) {
    L2_GUARD_FUNCTION();
    bool changed = false;
    {
        L2_GUARD(m_Sect);
        for (int slot = 0; slot < 144; ++slot) {
            ShortCutStruct& shortcut = m_ShortCutSlot[slot];
            if (shortcut.shortCutType == type && shortcut.shortCutID == id) {
                shortcut.shortCutType = SCT_NONE;
                db.RequestSetShortCut(m_User, slot, 0, 0, 0, 0);
                changed = true;
            }
        }
    }

    if (changed) {
        SendShortCutInfo();
    }
}

void CShortCut::DeleteRecipeShortCut(int recipeId) {
    L2_GUARD_FUNCTION();
    DeleteShortCut(SCT_RECIPE, recipeId);
}

void CShortCut::DeleteBookmarkshortCut(int bookmarkId) {
    L2_GUARD_FUNCTION();
    DeleteShortCut(SCT_BOOKMARK, bookmarkId);
}
