#pragma once

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

class PetDataSheetTable;
class PetDataParser;
struct AttrTypeInfo;

// Current L2Server's pet-data owner. The current constructor/allocation,
// parser writes and consumers fix this complete physical
// layout. The older PDB supplies only the original source names.
class PetDataSheet {
public:
    struct Record {
        enum Attr : std::int32_t {
            LEVEL = 0,
            MAX_MEAL = 1,
            EXP = 2,
            CON_MEAL_BATTLE = 3,
            CON_MEAL_NORMAL = 4,
            ORG_PATTACK = 5,
            ORG_PDEFEND = 6,
            ORG_MATTACK = 7,
            ORG_MDEFEND = 8,
            ORG_HP = 9,
            ORG_MP = 10,
            ORG_HPREGEN = 11,
            ORG_MPREGEN = 12,
            SPEED_ON_RIDE = 13,
            ATTACK_SPEED_ON_RIDE = 14,
            PATTACK_ON_RIDE = 15,
            MATTACK_ON_RIDE = 16,
            MAX_HP_ON_RIDE = 17,
            MAX_MP_ON_RIDE = 18,
            GET_EXP_TYPE = 19,
            CON_MEAL_BATTLE_ON_RIDE = 20,
            CON_MEAL_NORMAL_ON_RIDE = 21,
            FOOD_CLASS_ID = 22,
            HUNGRY_LIMIT = 23,
            SOULSHOT_COUNT = 24,
            SPIRITSHOT_COUNT = 25,
            IS_PENALTY = 26,
        };

        Record();

        void Init();
        void Set(Attr attr, double value);
        void Set(Attr attr, const std::vector<std::int64_t>& values);

        std::int32_t mLevel; // +0x00
        std::int64_t mExp; // +0x08
        std::int32_t mMaxMeal; // +0x10
        std::int32_t mConsumeMealInBattle; // +0x14
        std::int32_t mConsumeMealInNormal; // +0x18
        double mdOrgPAttack; // +0x20
        double mdOrgPDefend; // +0x28
        double mdOrgMAttack; // +0x30
        double mdOrgMDefend; // +0x38
        double mdOrgHP; // +0x40
        double mdOrgMP; // +0x48
        double mdOrgHPRegen; // +0x50
        double mdOrgMPRegen; // +0x58
        std::int32_t mSpeedOnRide[2][4]; // +0x60
        double mdAttackSpeedOnRide; // +0x80
        double mdPAttackOnRide; // +0x88
        double mdMAttackOnRide; // +0x90
        double mdMaxHPOnRide; // +0x98
        double mdMaxMPOnRide; // +0xa0
        std::int32_t mGetExpType; // +0xa8
        std::int32_t mConsumeMealInBattleOnRide; // +0xac
        std::int32_t mConsumeMealInNormalOnRide; // +0xb0
        std::int32_t m_nFoodClassId[4]; // +0xb4
        std::int32_t m_nHungryLimit; // +0xc4
        std::int32_t m_nSoulShotCount; // +0xc8
        std::int32_t m_nSpiritShotCount; // +0xcc
        bool m_bIsPenalty; // +0xd0
    };

    static constexpr std::size_t kRecordCount = 122;

    PetDataSheet();
    virtual ~PetDataSheet();

    virtual Record* GetRecord(std::int32_t level);
    static AttrTypeInfo* GetTypeInfo(const char16_t* symbol);
    void SetRecord(const Record& record) {
        if (static_cast<std::uint32_t>(record.mLevel) < kRecordCount) {
            std::memcpy(&mRecords[record.mLevel], &record, sizeof(record));
        }
    }
    void ClearEvolutionInfo();
    void SetEvolutionInfo(const std::vector<std::int32_t>& evolutionData);
    void SetName(const char16_t* name);
    void Verify();
    std::int32_t CheckEvolutionID(std::int32_t level);
    bool IsSyncLevel() const { return m_bSyncLevel; }
    std::int32_t Index() const { return m_Index; }

    Record mRecords[kRecordCount]; // +0x0008
    std::u16string mName; // +0x66f8
    std::int32_t mNpcClassId; // +0x6720

private:
    friend class PetDataSheetTable;
    friend class PetDataParser;

    static AttrTypeInfo msTable[27];

    std::int32_t m_Index; // +0x6724
    std::int32_t m_itemClassID; // +0x6728
    bool m_bSyncLevel; // +0x672c
    std::vector<std::int32_t> m_evolutionData; // +0x6730
};

struct AttrTypeInfo {
    const char16_t* mSymbol;
    PetDataSheet::Record::Attr mAttr;
    std::int32_t mPadding = 0;
};


// The current image uses four 0x20-byte VS2017 std::map owners. The older
// same-source donor supplies their original names. Current constructor,
// lookup, insertion and destruction code fixes their current extents and
// ordering independently.
class PetDataSheetTable {
public:
    using PetDataPetIdMap = std::map<std::int32_t, PetDataSheet*>;
    using PetDataNpcClassIdMap = std::map<std::int32_t, PetDataSheet*>;
    using PetDataItemClassIdMap = std::map<std::int32_t, PetDataSheet*>;
    using PetDataNameMap = std::map<std::u16string, PetDataSheet*>;

    PetDataSheetTable();
    ~PetDataSheetTable();

    static PetDataSheetTable& Inst();

    bool Load(wchar_t* fileName);
    bool Load(const wchar_t* fileName) { return Load(const_cast<wchar_t*>(fileName)); }
    void Insert(PetDataSheet* dataSheet);
    void Remove(std::int32_t petIndex);
    void RemoveAll();
    void Verify();
    PetDataSheet* FindDataSheet(std::int32_t npcClassId);
    PetDataSheet* FindDataSheet(const char16_t* name);
    virtual PetDataSheet* GetPetDataSheetByIndex(std::int32_t index);
    std::int32_t NpcClassIdFromIndex(std::int32_t index);
    std::int32_t ItemClassIdFromIndex(std::int32_t index);
    std::int32_t NpcClassIdFromItemClassID(std::int32_t itemClassId);

private:
    PetDataPetIdMap m_PetDataPetIdMap; // +0x08
    PetDataNpcClassIdMap m_PetDataNpcClassIdMap; // +0x28
    PetDataItemClassIdMap m_PetDataItemClassIdMap; // +0x48
    PetDataNameMap m_PetDataNameMap; // +0x68
    PetDataSheet* m_pDefaultPetDataSheet; // +0x88
};


extern PetDataSheetTable gPetDataSheet;
