#pragma once

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdio>


// Shared/CategoryDataDB.h. Current constructor 0x004015fc confirms the vptr,
// 0x20-byte map, FILE pointer and checker flag. SetMap owns raw set pointers.
// like the shipping destructor at 0x00406a24, teardown destroys only the map.
class CCategoryDataDB {
public:
    using CategorySet = std::set<int>;
    using SetMap = std::map<int, CategorySet*>;

    CCategoryDataDB();
    virtual ~CCategoryDataDB();

    CCategoryDataDB(const CCategoryDataDB&) = delete;
    CCategoryDataDB& operator=(const CCategoryDataDB&) = delete;

    static CCategoryDataDB& Inst();

    // PDB parameter names and current register use agree: Add receives
    // category_id then class_id. IsInCategory receives class_id then
    // category_id.
    bool AddObjectInCategory(int categoryId, int classId);
    bool IsInCategory(int classId, int categoryId);
    void GatherItemsInCategory(int categoryId, std::vector<int>& items);
    void Load();

    SetMap m_Categories; // +0x08
    std::FILE* m_category_pch_file = nullptr; // +0x28
    bool m_bOnlyPrecompiledHeader = false; // +0x30

private:
    void LoadCategoryData();
};
