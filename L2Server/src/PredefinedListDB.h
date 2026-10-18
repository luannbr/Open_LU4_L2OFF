#pragma once

#include "Shared/src/MemorySub.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

class TerritoryElement;

enum PredefinedListType : std::int32_t {
    PREDEFINED_LIST_SKILL = 1,
    PREDEFINED_LIST_ITEM = 2,
    PREDEFINED_LIST_TERRITORY = 3,
};

class BasePredefinedList : public MemoryObject {
public:
    int GetPredefinedListID() const noexcept { return m_nPredefinedListID; }
    PredefinedListType GetPredefinedListType() const noexcept { return m_ePredefinedListType; }

protected:
    BasePredefinedList(int predefinedListId, PredefinedListType predefinedListType) noexcept :
        m_nPredefinedListID(predefinedListId), m_ePredefinedListType(predefinedListType) {}

    ~BasePredefinedList() override = default;

private:
    int m_nPredefinedListID; // +0x18
    PredefinedListType m_ePredefinedListType; // +0x1c

    L2_DECLARE_OBJECT_REGISTRATION(BasePredefinedList)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(BasePredefinedList)
};


class PredefinedSkillList final : public BasePredefinedList {
public:
    explicit PredefinedSkillList(int predefinedListId);
    ~PredefinedSkillList() override;

    L2_DECLARE_OBJECT_TYPE_OUT_OF_LINE(PredefinedSkillList)

    void AddName(const char16_t* name);

    std::vector<std::int64_t> m_vPredefinedSkillIDList; // +0x20
    std::vector<std::u16string> m_vPredefinedSkillNameList; // +0x40
};

class PredefinedItemList final : public BasePredefinedList {
public:
    explicit PredefinedItemList(int predefinedListId);
    ~PredefinedItemList() override;

    L2_DECLARE_OBJECT_TYPE_OUT_OF_LINE(PredefinedItemList)

    void AddName(const char16_t* name);

    std::vector<std::int64_t> m_vPredefinedItemIDList; // +0x20
    std::vector<std::u16string> m_vPredefinedItemNameList; // +0x40
};

class PredefinedTerritoryList final : public BasePredefinedList {
public:
    explicit PredefinedTerritoryList(int predefinedListId);
    ~PredefinedTerritoryList() override;

    L2_DECLARE_OBJECT_TYPE_OUT_OF_LINE(PredefinedTerritoryList)

    std::vector<TerritoryElement*> m_vPredefinedTerritoryList; // +0x20
};


class PredefinedListDB : public Singleton<PredefinedListDB> {
public:
    PredefinedListDB();
    ~PredefinedListDB() override;

    BasePredefinedList* FindByPredefinedListID(int predefinedListId, bool logError);
    void RegisterNoToPredefinedList(int predefinedListId, BasePredefinedList* predefinedList);
    void RegisterNameToPredefinedList(const std::u16string& predefinedListName, BasePredefinedList* predefinedList);

    void Load();
    bool VerifyPredefinedListDB();

    std::vector<std::int64_t>* GetPredefinedSkillList(int predefinedListId);
    std::vector<std::int64_t>* GetPredefinedItemList(int predefinedListId);
    std::vector<TerritoryElement*>* GetPredefinedTerritoryList(int predefinedListId);

private:
    std::FILE* m_PredefinedList_pch_file; // +0x08
    bool m_bOnlyPrecompiledHeader; // +0x10
    RWLock m_lock; // +0x18
    std::map<int, BasePredefinedList*> m_mapPredefinedList; // +0x50
    std::map<std::u16string, int> m_mapPredefinedListNameMap; // +0x70
};
