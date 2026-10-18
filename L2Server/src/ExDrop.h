#pragma once

#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"

#include <cstddef>
#include <cstdint>

struct ExDropInfo {
    std::int32_t nCount; // +0x00
    double dAdditionalProb; // +0x08
};


using ExDropInfoMap = std::map<std::int32_t, ExDropInfo>;


// One extended-drop rule produced by parser_exdrop. The current L2Server
// parser allocates exactly 0x50 bytes and creates the category carrier as
// a 0x20-byte std::xvector, the historical TotalChecker layout is only 0x48.
class ExDropItemData {
    struct Range {
        int min;
        int max;
    };

    int id; // +0x00
    int itemId; // +0x04
    int giveCount; // +0x08
    int maxCount; // +0x0c
    int countId; // +0x10
    double prob; // +0x18
    double additionalProb; // +0x20
    Range pcLevel; // +0x28
    std::vector<int> m_vCategory; // +0x30

public:
    ExDropItemData(int dropId) :
        id(dropId),
        itemId(0),
        giveCount(0),
        maxCount(0),
        countId(dropId),
        prob(0.0),
        additionalProb(0.0),
        pcLevel{0, 0} {}

    void setItemId(int value) { itemId = value; }

    void setGiveCount(int value) { giveCount = value; }

    void setMaxCount(int value) { maxCount = value; }

    void setCountId(int value) { countId = value; }

    void setProb(int value) { prob = static_cast<double>(value); }

    void setPcLevelRange(int min, int max) {
        pcLevel.min = min;
        pcLevel.max = max;
    }

    void setCategory(std::vector<int>* categories) { m_vCategory = *categories; }

    void setAdditionalProb(double value) { additionalProb = value; }

    int getId() { return id; }

    int getItemId() { return itemId; }

    int getGiveCount() { return giveCount; }

    int getMaxCount() { return maxCount; }

    int getCountId() { return countId; }

    double getProb() { return prob; }

    double getAdditionalProb() { return additionalProb; }

    bool IsInLevel(int level) { return pcLevel.min <= level && level <= pcLevel.max; }

    bool IsInCategory(int classId);
};


class CNPC;
class CCreature;
class CParserForExDrop;

class ExDropItemSystem : public Singleton<ExDropItemSystem> {
public:
    ExDropItemSystem();
    ~ExDropItemSystem() override;

    void InitLastResetTime();
    bool IsNeedInit(std::int32_t time) const;
    ExDropItemData* getExDrop(int id);

    void LoadExDropData();
    void CheckAndDropItem(CNPC* npc, CCreature* creature);

private:
    friend class CParserForExDrop;

    std::map<int, ExDropItemData*> exDropDataMap; // +0x08
    void* m_category_pch_file; // +0x28
    bool m_bOnlyPrecompiledHeader; // +0x30
    std::byte m_alignment31To34[3]; // +0x31
    std::int32_t m_tmPrevResetTime; // +0x34
    std::int32_t m_tmNextResetTime; // +0x38
    std::byte m_alignment3cTo40[4]; // +0x3c
};
