#pragma once

#include "Shared/src/BaseAttribute.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class User;

// One item-class attribute-conversion rule produced by the current
// ItemAttrChange parser. The two current xvector carriers account for the
// target's growth from the historical donor layout to 0x48 bytes.
class CItemAttrChangeStruct {
public:
    using AttrType = std::vector<AttributeTypeEnum, std::allocator<AttributeTypeEnum>>;

    CItemAttrChangeStruct();

    std::int32_t m_nClassID; // +0x00
    AttrType InputAttrTypeList; // +0x08
    AttrType OutputAttrTypeList; // +0x28
};


// One current parser list. The map owns only its tree nodes. Its mapped
// CItemAttrChangeStruct pointers are borrowed process-lifetime records.
class CItemAttrChangeList {
public:
    using ItemAttrChangeInfo = std::map<std::int32_t, CItemAttrChangeStruct*>;

    CItemAttrChangeList();

    void AddList(CItemAttrChangeStruct* itemAttrChange);
    bool IsExist(int classId);
    bool IsInputAttr(int classId, int attributeType);
    std::uint32_t GetOutputAttr(int classId, int excludedAttributeType);
    bool IsOutputAttr(int classId, int attributeType);

    std::int32_t m_nId; // +0x00
    ItemAttrChangeInfo m_mapItemAttrChangeInfo; // +0x08
};


// Current ItemAttrChange.cpp process-wide database. The map owns its tree
// nodes. Parser-produced lists and rules live for the process
// lifetime, as in the shipping server.
class CItemAttrChangeDB {
public:
    using ItemAttrChanges = std::map<std::int32_t, CItemAttrChangeList*>;

    CItemAttrChangeList* find(int id);
    void Load();
    void Add(CItemAttrChangeList* itemAttrChangeList);
    // Current inline-only declarations have no compiled procedure or caller.
    void RemoveAll();
    int GetCount() const;
    void ItemAssemble(User* user, int id);

private:
    ItemAttrChanges m_mapItemAttrChange;
};


extern CItemAttrChangeDB g_ItemAttrChangeDB;
