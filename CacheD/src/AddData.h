#pragma once

// Shipping owner: CacheD/AddData.cpp. The current image exposes one virtual
// deleting destructor at 0x0040cf10 and no instance fields.
class CAddData {
public:
    CAddData();
    virtual ~CAddData();

    // Current Cached.exe 0x0040d048, 0x0040d5f4 and 0x0040dc18. Unlike the
    // C1 donor's void declarations, all three current bodies explicitly
    // return zero when the input file cannot be opened and one otherwise.
    bool AddItemData();
    bool AddSkillData();
    bool AddQuestData();
};


extern CAddData g_addData;
