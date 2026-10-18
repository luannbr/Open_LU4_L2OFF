#pragma once

#include "Shared/src/ItemOption.h"
#include "Shared/src/Obj.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CItem;
class CParserForVariation;
class Option;

enum VariationType : std::int32_t {
    VARI_NORMAL = 0,
    VARI_RANDOM = 1,
};

class CommissionInfo {
public:
    CommissionInfo() = default;
    CommissionInfo(std::int64_t cancelAdena, int itemClassID, std::int64_t itemCount) :
        cancelAdena(cancelAdena), itemClassID(itemClassID), itemCount(itemCount) {}

    std::int64_t cancelAdena = 0; // +0x00
    int itemClassID = 0; // +0x08
    std::int64_t itemCount = 0; // +0x10
};

class VariationKey {
public:
    enum WeaponType : std::int32_t {
        WARRIOR = 0,
        MAGE = 1,
        MAX = 2,
    };

    VariationKey() = default;
    VariationKey(int intensiveItemClassID, WeaponType weaponType) :
        intenItemClassID(intensiveItemClassID), weaponType(weaponType) {}

    bool operator<(const VariationKey& other) const noexcept;
    bool operator==(const VariationKey& other) const noexcept;
    bool operator!=(const VariationKey& other) const noexcept { return !(*this == other); }

    static WeaponType GetWeaponType(CItem* item);

    int intenItemClassID = 0; // +0x00
    WeaponType weaponType = WARRIOR; // +0x04
};

class CommissionKey {
public:
    CommissionKey() = default;
    CommissionKey(int intensiveItemClassID, std::uint16_t itemGroupNo) :
        intenItemClassID(intensiveItemClassID), itemGroupNo(itemGroupNo) {}

    bool operator<(const CommissionKey& other) const noexcept;
    bool operator==(const CommissionKey& other) const noexcept;

    int intenItemClassID = 0; // +0x00
    std::uint16_t itemGroupNo = 0; // +0x04
};

class OptionGroup : public MemoryObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(OptionGroup)

    struct OptionElement {
        float m_Chance = 0.0F;
        int m_OptionNo = 0;
    };

    static void* operator new(std::size_t requestedSize);

    OptionGroup();
    ~OptionGroup() override;

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    void InsertOption(int optionNo, double chance);
    Option* GetOption();
    bool IsMaxOptionList();
    int GetSizeOfOptionList();

private:
    friend class CParserForVariation;
    std::vector<OptionElement> optionList; // +0x18
};

class Variation : public MemoryObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(Variation)

    struct OptionGroupElement {
        OptionGroup* pOptionGroup = nullptr;
        float m_Chance = 0.0F;
        std::uint32_t m_targetPadding = 0;
    };

    static void* operator new(std::size_t requestedSize);

    Variation();
    ~Variation() override;

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    Option* GetNormalOption();
    Option* GetRandomOption();
    void InsertOptionGroup(OptionGroup* optionGroup, double chance, VariationType variationType);

    void SetVariationKey(const VariationKey& value) { key = value; }
    VariationKey GetVariationKey() const { return key; }

private:
    friend class CParserForVariation;

    using OptionGroupList = std::vector<OptionGroupElement>;

    Option* GetOptionImpl(OptionGroupList& optionGroupList);

    VariationKey key; // +0x18
    OptionGroupList normalOptionGroupList; // +0x20
    OptionGroupList randomOptionGroupList; // +0x40
};

class VariationDB : public MemoryObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(VariationDB)

    VariationDB();
    ~VariationDB() override;

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    void Load();

    void RegisterIntenSet(int intensiveItemClassID);
    void RegisterExceptionItem(int itemClassID);
    bool HasIntenItem(int intensiveItemClassID) const;
    bool IsExceptionItem(int itemClassID) const;
    bool IsValidCandidateItem(std::uint32_t itemClassID);
    std::uint16_t GetItemGroupNo(std::uint32_t itemClassID);
    void RegisterItemToGroup(std::uint32_t itemClassID, std::uint16_t itemGroupNo);
    void RegisterCommissionInfo(CommissionKey key, CommissionInfo info);

    int GetOptionNoBy(const char16_t* optionName, bool logError);
    Option* GetOptionBy(int optionNo, bool logError);
    ItemOption GetItemOptionOnLoad(ItemOptionKey optionKey);
    ItemOptionKey GetItemOptionKeyOnCreate(VariationKey key);
    ItemOption GetNewBlankItemOption();
    CommissionInfo GetCommissionInfo(std::uint32_t candidateItemClassID, std::uint32_t intensiveItemClassID);
    void RemoveAll();

private:
    friend class CParserForVariation;

    ItemOption CreateItemOption(Option* normalOption, Option* randomOption);

    std::set<int> intenSet; // +0x18
    std::set<int> exceptionSet; // +0x38
    std::map<VariationKey, Variation*> keyToVariationMap; // +0x58
    std::map<CommissionKey, CommissionInfo> keyToCommissionMap; // +0x78
    std::map<std::uint32_t, std::uint16_t> itemToGroupMap; // +0x98
};

extern VariationDB g_VariationDB;
