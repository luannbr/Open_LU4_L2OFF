#include "Shared/src/Log.h"

#include "Inventory.h"

#include "Creature.h"
#include "Item.h"
#include "Object.h"
#include "Shared/src/SharedFactory.h"
#include "Skill.h"
#include "User.h"


#include <cstdint>
#include <optional>

namespace {

// Current br_inventory_server.cpp copy at .rdata 0x00e7c868. BaseCrystal is
// translation-unit-local in the original header as well.
constexpr std::int32_t BaseCrystal[] = {0, 1, 2, 3, 4, 5, 5, 5, 8, 8, 8, 11, 12};

} // namespace

int CInventory::UseArrow(const CrystalType crystalType, const int count) {
    // Current L2Server 0x004cabf0.
    L2_GUARD_FUNCTION();
    CSPointer<CCreature> ownerReference;
    CCreature* owner = nullptr;
    CItem* projectile = nullptr;
    int itemClassId = 0;
    bool isLastProjectile = false;
    {
        L2_READ_GUARD(*this);
        ownerReference = CCreatureFactory::Inst().FindObjectSP(m_nCreatureIndex);
        owner = ownerReference.get();
        if (owner != nullptr) {
            projectile = GetByIndex(owner->m_data->m_nSlotIndex[ST_LHAND]);
            if (projectile == nullptr || projectile->m_const->m_EtcitemType != static_cast<EtcItemType>(2) ||
                BaseCrystal[static_cast<int>(projectile->m_const->GetCrystalType())] !=
                    BaseCrystal[static_cast<int>(crystalType)]) {
                projectile = GetArrowByCrystal(crystalType);
                if (projectile == nullptr) {
                    owner->m_data->m_nSlotIndex[ST_LHAND] = -1;
                    owner->m_data->m_iLastArrowIndex = owner->m_data->m_nSlotIndex[ST_LHAND];
                    owner->ValidateChanged(VB_ITEMSKILL);
                    return false;
                }

                owner->m_data->m_nSlotIndex[ST_LHAND] = projectile->m_data->index;
                owner->m_data->m_iLastArrowIndex = owner->m_data->m_nSlotIndex[ST_LHAND];
                owner->ValidateChanged(VB_ITEMSKILL);
                if (owner->IsPC()) {
                    owner->CastUser()->SendItemList(false, true, nullptr);
                }
            } else if (CItemRestrict::Inst().CheckItemRestrict(projectile->m_data->m_nClassID)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d]item restrict on ItemClassID[%d]",
                //     L"br_inventory_server.cpp",
                //     0x20,
                //     projectile->m_data->m_nClassID)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d]item restrict on ItemClassID[%d]",
                //     L"br_inventory_server.cpp",
                //     0x20,
                //     projectile->m_data->m_nClassID)
                L2_LOG_WARN(
                    Log, "Cannot consume restricted arrow prototype %d for owner index %d; consumption is skipped.",
                    projectile->m_data->m_nClassID, m_nCreatureIndex
                );
                return false;
            }

            if (!CSharedItemData::IsStackable(projectile->m_data->m_ItemConsumeType)) {
                return true;
            }

            itemClassId = projectile->m_data->m_nClassID;
            isLastProjectile = projectile->m_data->m_nCount == 1;
        }
    }

    if (owner == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]error, Cannot find creature.",
        //     "br_inventory_server.cpp",
        //     0x16)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]error, Cannot find creature.",
        //     "br_inventory_server.cpp",
        //     0x16)
        L2_LOG_ERROR(
            Log, "Cannot consume arrows: inventory owner index %d is unavailable; consumption is skipped.", m_nCreatureIndex
        );
        return false;
    }

    if (!isLastProjectile || !owner->IsPC() || owner->CastUser() == nullptr) {
        return owner->DeleteItemInInventoryBeforeCommit(itemClassId, count);
    }

    User* const user = owner->CastUser();
    std::map<int, const CSkillInfo*> itemSkills;
    user->GetSkillsFromItem(*projectile, itemSkills, User::CCT_WITHIN_CRYSTAL_LEVEL);
    const bool deleted = user->DeleteItemInInventoryBeforeCommit(itemClassId, count);
    if (!deleted) {
        return false;
    }

    {
        L2_GUARD(user->m_AcquiredSkillsSect);
        for (const auto& skill : itemSkills) {
            if (skill.second != nullptr && !user->DeleteAcquireSkill(skill.second->m_nSkillId)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d]Cannot delete acquire skill on arrow",
                //     L"br_inventory_server.cpp",
                //     0x3e)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d]Cannot delete acquire skill on arrow",
                //     L"br_inventory_server.cpp",
                //     0x3e)
                L2_LOG_ERROR(
                    Log,
                    "Cannot remove skill %d after consuming the last arrow (prototype %d, owner index %d); remaining skill "
                    "removals are skipped, validation and left-hand slot cleanup follow.",
                    skill.second->m_nSkillId, itemClassId, m_nCreatureIndex
                );
                break;
            }
        }
        user->ValidateChanged(VB_ITEMSKILL | VB_SKILLLIST | VB_SKILLMOD | VB_SIM);
        user->m_data->m_nSlotIndex[ST_LHAND] = -1;
        user->m_data->m_iLastArrowIndex = owner->m_data->m_nSlotIndex[ST_LHAND];
    }
    return true;
}

bool CInventory::UseBolt(const CrystalType crystalType, const std::int64_t count) {
    // Current L2Server 0x004cb55c.
    L2_GUARD_FUNCTION();
    CSPointer<CCreature> ownerReference;
    CCreature* owner = nullptr;
    CItem* projectile = nullptr;
    int itemClassId = 0;
    bool isLastProjectile = false;
    {
        L2_READ_GUARD(*this);
        ownerReference = CCreatureFactory::Inst().FindObjectSP(m_nCreatureIndex);
        owner = ownerReference.get();
        if (owner != nullptr) {
            projectile = GetByIndex(owner->m_data->m_nSlotIndex[ST_LHAND]);
            if (projectile == nullptr || projectile->m_const->m_EtcitemType != static_cast<EtcItemType>(0x1b) ||
                BaseCrystal[static_cast<int>(projectile->m_const->GetCrystalType())] !=
                    BaseCrystal[static_cast<int>(crystalType)]) {
                projectile = GetBoltByCrystal(crystalType);
                if (projectile == nullptr) {
                    owner->m_data->m_nSlotIndex[ST_LHAND] = -1;
                    owner->m_data->m_iLastArrowIndex = owner->m_data->m_nSlotIndex[ST_LHAND];
                    owner->ValidateChanged(VB_ITEMSKILL);
                    return false;
                }

                owner->m_data->m_nSlotIndex[ST_LHAND] = projectile->m_data->index;
                owner->m_data->m_iLastArrowIndex = owner->m_data->m_nSlotIndex[ST_LHAND];
                owner->ValidateChanged(VB_ITEMSKILL);
                if (owner->IsPC()) {
                    owner->CastUser()->SendItemList(false, true, nullptr);
                }
            } else if (CItemRestrict::Inst().CheckItemRestrict(projectile->m_data->m_nClassID)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d]item restrict on ItemClassID[%d]",
                //     L"br_inventory_server.cpp",
                //     0x97,
                //     projectile->m_data->m_nClassID)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d]item restrict on ItemClassID[%d]",
                //     L"br_inventory_server.cpp",
                //     0x97,
                //     projectile->m_data->m_nClassID)
                L2_LOG_WARN(
                    Log, "Cannot consume restricted bolt prototype %d for owner index %d; consumption is skipped.",
                    projectile->m_data->m_nClassID, m_nCreatureIndex
                );
                return false;
            }

            if (!CSharedItemData::IsStackable(projectile->m_data->m_ItemConsumeType)) {
                return true;
            }

            itemClassId = projectile->m_data->m_nClassID;
            isLastProjectile = projectile->m_data->m_nCount == 1;
        }
    }

    if (owner == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]error, Cannot find creature.",
        //     "br_inventory_server.cpp",
        //     0x8e)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]error, Cannot find creature.",
        //     "br_inventory_server.cpp",
        //     0x8e)
        L2_LOG_ERROR(
            Log, "Cannot consume bolts: inventory owner index %d is unavailable; consumption is skipped.", m_nCreatureIndex
        );
        return false;
    }

    if (!isLastProjectile || !owner->IsPC() || owner->CastUser() == nullptr) {
        return owner->DeleteItemInInventoryBeforeCommit(itemClassId, count);
    }

    User* const user = owner->CastUser();
    std::map<int, const CSkillInfo*> itemSkills;
    user->GetSkillsFromItem(*projectile, itemSkills, User::CCT_WITHIN_CRYSTAL_LEVEL);
    const bool deleted = user->DeleteItemInInventoryBeforeCommit(itemClassId, count);
    if (!deleted) {
        return false;
    }

    {
        L2_GUARD(user->m_AcquiredSkillsSect);
        for (const auto& skill : itemSkills) {
            if (skill.second != nullptr && !user->DeleteAcquireSkill(skill.second->m_nSkillId)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d]Cannot delete acquire skill on arrow",
                //     L"br_inventory_server.cpp",
                //     0xb6)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d]Cannot delete acquire skill on arrow",
                //     L"br_inventory_server.cpp",
                //     0xb6)
                L2_LOG_ERROR(
                    Log,
                    "Cannot remove skill %d after consuming the last bolt (prototype %d, owner index %d); remaining skill "
                    "removals are skipped, validation and left-hand slot cleanup follow.",
                    skill.second->m_nSkillId, itemClassId, m_nCreatureIndex
                );
                break;
            }
        }
        user->ValidateChanged(VB_ITEMSKILL | VB_SKILLLIST | VB_SKILLMOD | VB_SIM);
        user->m_data->m_nSlotIndex[ST_LHAND] = -1;
        user->m_data->m_iLastArrowIndex = owner->m_data->m_nSlotIndex[ST_LHAND];
    }
    return true;
}

CItem* CInventory::GetArrowByCrystal(CrystalType crystalType) {
    L2_GUARD_FUNCTION();

    CItem* result = nullptr;
    CSPointer<CItem> item;
    CSPointer<CCreature> owner;
    {
        L2_READ_GUARD(*this);
        owner = CCreatureFactory::Inst().FindObjectSP(m_nCreatureIndex);
        if (owner) {
            CCreature* const ownerCreature = owner.get();
            CSharedCreatureData* const ownerData = ownerCreature->m_data;
            CItem* const equipped = GetByIndex(ownerData->m_nSlotIndex[ST_LHAND]);
            if (equipped) {
                CObject* const equippedObject = static_cast<CObject*>(equipped);
                const CSharedItemConstant* const constant = equipped->m_const;
                if (constant->m_EtcitemType == 2 &&
                    BaseCrystal[static_cast<std::int32_t>(constant->GetCrystalType())] ==
                        BaseCrystal[static_cast<std::int32_t>(crystalType)]) {
                    result = equippedObject->CastItem();
                    return result;
                }
            }

            CItemFactory& items = CItemFactory::Inst();
            int currentIndex = GetFirstIndex();
            int depth = 0;
            while (currentIndex != -1) {
                item = items.FindObjectSP(currentIndex);
                if (item) {
                    CItem* const typedItem = item->CastItem();
                    const CSharedItemConstant* const constant = typedItem->m_const;
                    if (constant->m_EtcitemType == 2 &&
                        BaseCrystal[static_cast<std::int32_t>(constant->GetCrystalType())] ==
                            BaseCrystal[static_cast<std::int32_t>(crystalType)] &&
                        (result == nullptr || result->m_data->m_nClassID < typedItem->m_data->m_nClassID)) {
                        result = typedItem;
                    }
                }

                currentIndex = items.GetNextIndex(currentIndex);
                ++depth;
                if (depth >= kMaximumTraversalDepth) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "depth > %d at file[%s], line[%d]",
                    //     0x192,
                    //     "br_inventory_server.cpp",
                    //     0x134)
                    // L2_LOG_ERROR(Log,
                    //     "depth > %d at file[%s], line[%d]",
                    //     0x192,
                    //     "br_inventory_server.cpp",
                    //     0x134)
                    L2_LOG_ERROR(
                        Log,
                        "Arrow search reached the %d-entry traversal limit for owner index %d (next index %d); returning "
                        "the selection accumulated so far.",
                        kMaximumTraversalDepth, m_nCreatureIndex, currentIndex
                    );
                    break;
                }
            }
        }
    }

    if (!owner) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]error, Cannot find creature.",
        //     "br_inventory_server.cpp",
        //     0x106)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]error, Cannot find creature.",
        //     "br_inventory_server.cpp",
        //     0x106)
        L2_LOG_ERROR(
            Log,
            "Cannot select an arrow for crystal grade %d: inventory owner index %d is unavailable; no item is returned.",
            static_cast<int>(crystalType), m_nCreatureIndex
        );
        return nullptr;
    }

    return result;
}

CItem* CInventory::GetBoltByCrystal(CrystalType crystalType) {
    L2_GUARD_FUNCTION();

    CItem* result = nullptr;
    CSPointer<CItem> item;
    CSPointer<CCreature> owner;
    {
        L2_READ_GUARD(*this);
        owner = CCreatureFactory::Inst().FindObjectSP(m_nCreatureIndex);
        if (owner) {
            CCreature* const ownerCreature = owner.get();
            CSharedCreatureData* const ownerData = ownerCreature->m_data;
            CItem* const equipped = GetByIndex(ownerData->m_nSlotIndex[ST_LHAND]);
            if (equipped) {
                CObject* const equippedObject = static_cast<CObject*>(equipped);
                const CSharedItemConstant* const constant = equipped->m_const;
                if (constant->m_EtcitemType == 0x1b &&
                    BaseCrystal[static_cast<std::int32_t>(constant->GetCrystalType())] ==
                        BaseCrystal[static_cast<std::int32_t>(crystalType)]) {
                    result = equippedObject->CastItem();
                    return result;
                }
            }

            CItemFactory& items = CItemFactory::Inst();
            int currentIndex = GetFirstIndex();
            int depth = 0;
            while (currentIndex != -1) {
                item = items.FindObjectSP(currentIndex);
                if (item) {
                    CItem* const typedItem = item->CastItem();
                    const CSharedItemConstant* const constant = typedItem->m_const;
                    if (constant->m_EtcitemType == 0x1b &&
                        BaseCrystal[static_cast<std::int32_t>(constant->GetCrystalType())] ==
                            BaseCrystal[static_cast<std::int32_t>(crystalType)] &&
                        (result == nullptr || result->m_data->m_nClassID < typedItem->m_data->m_nClassID)) {
                        result = typedItem;
                    }
                }

                currentIndex = items.GetNextIndex(currentIndex);
                ++depth;
                if (depth >= kMaximumTraversalDepth) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"depth > %d at file[%s], line[%d]",
                    //     0x192,
                    //     L"br_inventory_server.cpp",
                    //     0x176)
                    // L2_LOG_ERROR(Log,
                    //     L"depth > %d at file[%s], line[%d]",
                    //     0x192,
                    //     L"br_inventory_server.cpp",
                    //     0x176)
                    L2_LOG_ERROR(
                        Log,
                        "Bolt search reached the %d-entry traversal limit for owner index %d (next index %d); returning the "
                        "selection accumulated so far.",
                        kMaximumTraversalDepth, m_nCreatureIndex, currentIndex
                    );
                    break;
                }
            }
        }
    }

    if (!owner) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]error, Cannot find creature.",
        //     "br_inventory_server.cpp",
        //     0x14b)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]error, Cannot find creature.",
        //     "br_inventory_server.cpp",
        //     0x14b)
        L2_LOG_ERROR(
            Log, "Cannot select a bolt for crystal grade %d: inventory owner index %d is unavailable; no item is returned.",
            static_cast<int>(crystalType), m_nCreatureIndex
        );
        return nullptr;
    }

    return result;
}

int CInventory::GetRuneItemCount(MaterialType materialType) {
    L2_GUARD_FUNCTION();

    {
        L2_READ_GUARD(*this);
    }

    switch (materialType) {
        case MATT_RUNE_XP:
            return m_nEXPRuneCount;
        case MATT_RUNE_SP:
            return m_nSPRuneCount;
        case MATT_RUNE_REMOVE_PENALTY:
            return m_nRPRuneCount;
        default:
            return 0;
    }
}

void CInventory::ClearRuneState() {
    // Current L2Server.exe 0x00601c00. This is the exact reload-time
    // reset used by LoadSelectedCharacterPacket before inventory reload.
    m_nEXPRuneCount = 0;
    m_nSPRuneCount = 0;
    m_nRPRuneCount = 0;
    m_RuneItemClassIds.clear();
}

void CInventory::UpdateRuneItem() {
    // Current L2Server.exe 0x004cbf4c.
    L2_GUARD_FUNCTION();
    std::optional<std::vector<std::uint32_t>> previousRuneItemsStorage;
    const bool changed = [&] {
        L2_WRITE_GUARD(*this);
        if (GetOwner() == nullptr) {
            return false;
        }
        CCreature* const owner = GetOwner();
        if (!owner->IsPC()) {
            return false;
        }

        const int previousExperienceRunes = m_nEXPRuneCount;
        const int previousSpRunes = m_nSPRuneCount;
        const int previousPenaltyRunes = m_nRPRuneCount;
        const auto& previousRuneItems = previousRuneItemsStorage.emplace(m_RuneItemClassIds);

        m_nEXPRuneCount = 0;
        m_nSPRuneCount = 0;
        m_nRPRuneCount = 0;
        m_RuneItemClassIds.clear();

        CItemFactory& items = CItemFactory::Inst();
        int itemIndex = GetFirstIndex();
        int depth = 0;
        while (itemIndex != -1) {
            CSPointer<CItem> item = items.FindObjectSP(itemIndex);
            if (!item) {
                // Log.Add(
                //     LOG_ERROR,
                //     "spItem is Invalid at file[%s], line[%d]",
                //     "br_inventory_server.cpp",
                //     0x1ed)
                // L2_LOG_ERROR(Log,
                //     "spItem is Invalid at file[%s], line[%d]",
                //     "br_inventory_server.cpp",
                //     0x1ed)
                L2_LOG_ERROR(
                    Log, "Rune inventory scan cannot find item index %d for owner index %d; the entry is skipped.",
                    itemIndex, m_nCreatureIndex
                );
            } else {
                CItem* const typedItem = item.get();
                const CSharedItemConstant* const constant = typedItem->m_const;
                if (constant != nullptr) {
                    if (constant->m_EtcitemType == 0x23) {
                        switch (constant->m_Material) {
                            case MATT_RUNE_XP:
                                ++m_nEXPRuneCount;
                                break;
                            case MATT_RUNE_SP:
                                ++m_nSPRuneCount;
                                break;
                            case MATT_RUNE_REMOVE_PENALTY:
                                ++m_nRPRuneCount;
                                break;
                            default:
                                break;
                        }
                    } else if (constant->m_EtcitemType == 0x22) {
                        m_RuneItemClassIds.push_back(static_cast<std::uint32_t>(typedItem->m_data->m_nClassID));
                    }
                }
            }

            itemIndex = items.GetNextIndex(itemIndex);
            ++depth;
            if (depth >= kMaximumTraversalDepth) {
                // Log.Add(
                //     LOG_ERROR,
                //     "depth > %d at file[%s], line[%d]",
                //     0x192,
                //     "br_inventory_server.cpp",
                //     0x207)
                // L2_LOG_ERROR(Log,
                //     "depth > %d at file[%s], line[%d]",
                //     0x192,
                //     "br_inventory_server.cpp",
                //     0x207)
                L2_LOG_ERROR(
                    Log,
                    "Rune inventory scan reached the %d-entry traversal limit for owner index %d (next index %d); keeping "
                    "accumulated rune state and checking whether notifications are needed.",
                    kMaximumTraversalDepth, m_nCreatureIndex, itemIndex
                );
                break;
            }
        }

        const bool runeItemsChanged = previousRuneItems.size() != m_RuneItemClassIds.size() ||
            !std::equal(previousRuneItems.begin(), previousRuneItems.end(), m_RuneItemClassIds.begin());
        return previousExperienceRunes != m_nEXPRuneCount || previousSpRunes != m_nSPRuneCount ||
            previousPenaltyRunes != m_nRPRuneCount || runeItemsChanged;
    }();

    if (changed) {
        User* const user = GetOwner()->CastUser();
        if (user != nullptr) {
            user->OnRuneItemsChange();
            user->SendMyInfo();
            user->SendRuneItemSkillList();
        }
    }
}
