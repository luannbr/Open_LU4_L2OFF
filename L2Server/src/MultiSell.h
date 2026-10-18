#pragma once

#include "Shared/src/BaseAttribute.h"
#include "Shared/src/ItemOptionKey.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CItem;
class CNPC;
class EnsoulOptionData;
class User;

struct MultiSellItemStruct final {
    std::int32_t ItemClassId; // +0x00
    std::int64_t Count; // +0x08
    double Probability; // +0x10
    std::int32_t nEnchant; // +0x18
};


class CMultiSellKeepDataInfo final {
public:
    CMultiSellKeepDataInfo();
    CMultiSellKeepDataInfo(const CMultiSellKeepDataInfo& other);
    ~CMultiSellKeepDataInfo() = default;

    bool operator<(const CMultiSellKeepDataInfo& other) const;

    std::int32_t m_nEnchant; // +0x00
    ItemOptionKey m_OptionKey; // +0x04
    BaseAttribute m_Attribute; // +0x0c
    std::byte m_alignment01eTo020[0x02]{};
    std::map<int, EnsoulOptionData*> m_ensoulOptionDataMap[3]; // +0x20
};


class CMultiSellInfo final {
public:
    CMultiSellInfo();
    ~CMultiSellInfo();

    bool CheckUsingPledgePoint();
    int GetTaxTo(CNPC* npc);

    std::int32_t m_nId; // +0x00
    std::int32_t m_nTaxBase; // +0x04
    bool m_bUsePledgePoint; // +0x08
    std::uint8_t m_targetPad009[7]{};
    std::vector<MultiSellItemStruct*> m_vInputList; // +0x10
    std::vector<MultiSellItemStruct*> m_vOutputList; // +0x30
};


class CMultiSellList final {
public:
    CMultiSellList();
    ~CMultiSellList();

    CMultiSellInfo* find(std::int32_t infoId);
    void Add(CMultiSellInfo* info);
    void ClearMultiSellInfo();
    void ClearNPCList();
    void AddRequiredNPC(std::uint32_t npcId);
    int GenerateMultisellTypeSet();
    bool IsKeyItem(CItem* item);
    bool HasKeyItem(CMultiSellInfo* info);
    bool CheckSendable(CMultiSellInfo* info, User* user);
    bool IsAllowed(User* user, CNPC* npc);
    void SendList(User* user, CNPC* npc, bool showLast);

    int AssembleOneMultisellItem(
        char* destination,
        int capacity,
        User* user,
        CMultiSellInfo* info,
        std::int64_t multiplier,
        bool keepEnchanted,
        int enchanted,
        std::int64_t tax,
        bool showVariation,
        const ItemOptionKey& optionKey,
        const BaseAttribute& attribute,
        bool showEnsoul,
        bool keepEnsoul,
        const std::map<int, EnsoulOptionData*> ensoulOptionDataMap[3]
    );
    void SendMultiSellListPerPage(
        int* itemCount,
        int pageSize,
        int* page,
        User* user,
        CNPC* npc,
        int* bufferSize,
        char* buffer,
        int capacity,
        bool showLast
    );

    bool HasRequiredNPC() const noexcept { return !m_RequiedNpcList.empty(); }
    bool IsWithoutNPC() const noexcept { return m_nIsWithoutNPC != 0; }

    std::int32_t m_nLastInfoId; // +0x00
    std::int32_t m_nShowAll; // +0x04
    std::int32_t m_nKeepEnchanted; // +0x08
    std::int32_t m_nIsDutyfree; // +0x0c
    std::int32_t m_nShowType; // +0x10
    std::int32_t m_nId; // +0x14
    bool m_bUsePledgePoint; // +0x18
    bool m_bShowVariationItem; // +0x19
    std::uint8_t m_targetPad01a[2]{};
    std::int32_t m_nSSQFlag; // +0x1c
    std::int32_t m_nType; // +0x20
    std::int32_t m_nKeepShapeShifted; // +0x24
    bool m_bShowEnsoulItem; // +0x28
    bool m_bKeepEnsoul; // +0x29
    std::uint8_t m_targetPad02a[2]{};
    std::int32_t m_nIsWithoutNPC; // +0x2c

private:
    std::map<std::int32_t, CMultiSellInfo*> m_mapMultiSellInfo; // +0x30
    std::set<std::uint32_t> m_RequiedNpcList; // +0x50
};


class CMultiSellDB final {
public:
    CMultiSellDB() = default;
    ~CMultiSellDB() = default;

    CMultiSellList* find(std::int32_t listId);
    void Add(CMultiSellList* list);
    void Load();

    std::size_t GetCount() const noexcept { return m_mapMultiSellList.size(); }

private:
    std::map<std::int32_t, CMultiSellList*> m_mapMultiSellList;
};


extern CMultiSellDB g_MultiSellDB;
