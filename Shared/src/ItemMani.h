#pragma once

#include "Shared/src/BaseAttribute.h"
#include "Shared/src/IOC.h"
#include "Shared/src/ItemOptionKey.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>
#include <utility>

#if defined(L2_BUILD_L2SERVER)

namespace ItemGetReason {

enum Enum : std::int32_t {
    GetFromNpc = 0,
    GetFromPc = 1,
    GetFromBuilder = 2,
    GetByRefund = 3,
    Etc = 4,
    GetByRecipe = 5,
    Pick = 6,
    GetByEnchantFail = 7,
    DQuestReward = 8,
    Combination = 9,
    Alchemy = 10,
    Ensoul = 11,
    MonsterBook = 12,
    CardUpDownGame = 13,
    PledgeTotalContribution = 14,
    PledgeItemBuy = 15,
    PledgeMissionReward = 16,
    UseCount = 17,
    EnchantArtifact = 18,
};

} // namespace ItemGetReason

class EnsoulOptionData;
class CCreature;
class CInventory;
class CItem;

class ItemManip;

bool SendSysMsg701(CCreature* creature, const ItemManip* manipulation);

// Shared/ItemMani.h value used by the current L2Server image. Current
// producers, consumers and vector algorithms all use the exact 0x30-byte
// stride below. Methods whose signatures are current-visible but whose
// dependency closure is not yet portable remain declarations instead of
// fabricated fallback bodies.
class ItemManip {
public:
    using OnErrorFunc = bool (*)(CCreature*, const ItemManip*);

    ItemManip(
        int relatedQuestID, int classId, std::int64_t count, int enchanted, int blessed, int eroded, bool checkVariation
    ) :
        mRelatedQuestID(relatedQuestID),
        mClassId(classId),
        mDBId(0),
        mCount(count),
        mEnchanted(enchanted),
        mBlessed(blessed),
        mEroded(eroded),
        mCheckVariation(checkVariation),
        mOnInvalidCount(&SendSysMsg701) {}

    ItemManip(int relatedQuestID, int classId, std::int64_t count, int enchanted, int blessed, int eroded) :
        ItemManip(relatedQuestID, classId, count, enchanted, blessed, eroded, true) {}

    ItemManip() : ItemManip(0, 0, 0, 0, 0, 0, true) {}

    const unsigned char* Disassemble(const unsigned char* packet);

    // Current bodies 0x007f2488 and 0x007f3804/0x007f3bec are published in
    // the physical Shared itemmani.cpp owner.
    bool SimulateWithInventory(CInventory& inventory, std::pair<int, std::int64_t>& result) const;
    bool MakeDBPacket(char* buffer, int bufferSize, int& bufferPosition, int& itemCount) const;
    bool MakeDBPacket(
        CInventory& inventory, char* buffer, int bufferSize, int& bufferPosition, int& itemCount, ItemGetReason::Enum reason
    ) const;

    void SetPolicyOnInvalidCount(OnErrorFunc policy) { mOnInvalidCount = policy; }

    void Dump();

    int mRelatedQuestID; // +0x00
    int mClassId; // +0x04
    int mDBId; // +0x08
    std::int64_t mCount; // +0x10
    int mEnchanted; // +0x18
    int mBlessed; // +0x1c
    int mEroded; // +0x20
    bool mCheckVariation; // +0x24

private:
    bool IsDbId() const;

    OnErrorFunc mOnInvalidCount; // +0x28
};

// Current Shared/ItemMani.h transaction carrier. Both members are target
// std::vector<ItemManip> objects (three pointers each), so the complete
// physical envelope is 0x30 bytes on Win64.
class ItemManipList {
public:
    void Push(int count, const ItemManip* manipulations);
    void Push(const ItemManip& manipulation);
    void Serialize();

    std::vector<ItemManip> m_vInputList;
    std::vector<ItemManip> m_vOutputList;
};


// Current L2Server Shared/ItemMani.h value. The three map owners are deep
// copied and own only their nodes. Mapped EnsoulOptionData pointers are
// borrowed.
struct InventoryParams {
    InventoryParams();
    InventoryParams(const InventoryParams& other);

    std::int32_t nItemClassID;
    std::int64_t nCount;
    ItemGetReason::Enum m_Reason;
    std::int32_t nEnchanted;
    std::int32_t nBless;
    std::int32_t nEroded;
    ItemOptionKey key;
    std::int32_t nIntensiveItemClassID;
    BaseAttribute attribute;
    std::int32_t nShapeShiftedClassId;
    std::int32_t nShapeShiftedPeriod;
    std::map<int, EnsoulOptionData*> mEnsoulOptionMap[3];
    std::int16_t hLocked;
    std::int16_t hUseCount;
};

int MsgHelper_ManipulateItem(char* buffer, int bufferSize, int& bufferPosition, CItem* item, InventoryParams& parameters);

int MsgHelper_ManipulateItem(
    char* buffer,
    int bufferSize,
    int& bufferPosition,
    CItem* item,
    std::int64_t count,
    ItemGetReason::Enum reason,
    int enchanted,
    int blessed,
    int eroded,
    ItemOptionKey key,
    int intensiveItemClassId,
    int shapeShiftedClassId,
    int shapeShiftedPeriod
);


#elif defined(L2_BUILD_L2NPC)

class CCreature;

class ItemManip;

bool SendSysMsg701(CCreature* creature, const ItemManip* manipulation);

// Shared/itemmani.h. The current L2NPC.exe ItemManip::Assemble at
// 0x0050f254 and the matching NASC64_2015 PDB both identify this exact
// 0x30-byte revision of the class.
class ItemManip {
public:
    using OnErrorFunc = bool (*)(CCreature*, const ItemManip*);

    ItemManip(
        int relatedQuestID, int classId, std::int64_t count, int enchanted, int blessed, int eroded, bool checkVariation
    ) :
        mRelatedQuestID(relatedQuestID),
        mClassId(classId),
        mDBId(0),
        mCount(count),
        mEnchanted(enchanted),
        mBlessed(blessed),
        mEroded(eroded),
        mCheckVariation(checkVariation),
        mOnInvalidCount(&SendSysMsg701) {}

    ItemManip(int relatedQuestID, int classId, std::int64_t count, int enchanted, int blessed, int eroded) :
        ItemManip(relatedQuestID, classId, count, enchanted, blessed, eroded, true) {}

    ItemManip() : ItemManip(0, 0, 0, 0, 0, 0, true) {}

    int Assemble(char* outBuffer, int bufferSize);

    const unsigned char* Disassemble(const unsigned char* packet) {
        return ::Disassemble(
            packet, "ddQdddc", &mRelatedQuestID, &mClassId, &mCount, &mEnchanted, &mBlessed, &mEroded, &mCheckVariation
        );
    }

    void SetPolicyOnInvalidCount(OnErrorFunc policy) { mOnInvalidCount = policy; }

    int mRelatedQuestID = 0;
    int mClassId = 0;
    int mDBId = 0;
    std::int64_t mCount = 0;
    int mEnchanted = 0;
    int mBlessed = 0;
    int mEroded = 0;
    bool mCheckVariation = true;

private:
    bool IsDbId() const { return (static_cast<std::uint32_t>(mClassId) & 0x80000000U) != 0; }

    OnErrorFunc mOnInvalidCount = &SendSysMsg701;
};


#endif
