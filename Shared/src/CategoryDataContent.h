#pragma once

#include "Protocol/src/L2Constants.h"

#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"

#include <cstddef>

namespace ClassGroup {

// Exact enum recovered from the shared 2015 PDB and fixed by
// the nine key/value insertions in current Cached.exe 0x004036cc.
// enum ClassRoleType : int {
//     CRT_NONE = 0,
//     CRT_KNIGHT = 1,
//     CRT_WARRIOR = 2,
//     CRT_ROGUE = 3,
//     CRT_ARCHOR = 4,
//     CRT_WIZARD = 5,
//     CRT_SUMMONER = 6,
//     CRT_ENCHANTER = 7,
//     CRT_SUPPORTER = 8,
//     CRT_NOVICE = 9,
//     CRT_MAX = 10,
// }

} // namespace ClassGroup

namespace CategoryContent {

// Shared/CategoryDataContent.{h,cpp}. ContentBase is the non-polymorphic
// second base in ClassRole: target offsets put its xmap at +0x08 in the
// complete object and its category-content discriminator at +0x28.
class ContentBase {
public:
    int GetGroupId(int classType);
    void GatherClassList(int groupId, std::vector<int>& classList);
    void CheckItemIntegrity();
    void SaveCategoryToDB();

protected:
    explicit ContentBase(int categoryContentType);

    std::map<int, const char16_t*> m_mapGroup;
    int m_categoryContentType;
};


// Current Cached.exe 0x00402b10. The target object is exactly the Singleton
// base followed by ContentBase. Unlike ClassRole it owns no generated string
// map.
class ClassChange final : public Singleton<ClassChange>, public ContentBase {
public:
    ClassChange();
    // Its implicit scalar deleting destructor is current 0x00402c8c: clear
    // ContentBase's xmap, release its sentinel and finish the Singleton base.
};


// The target inherits the exact Shared Singleton<T> owner before ContentBase.
// its generated class-string map so remains at complete offset
// +0x30 and the full object remains 0x50 bytes.
class ClassRole final : public Singleton<ClassRole>, public ContentBase {
public:
    ClassRole();
    ~ClassRole() override;

    void GenerateClassString();

    // Semantic spelling for current inline accessor 0x00402048. Its machine
    // behavior is a non-inserting map lookup returning the stored c_str(), or
    // null on a miss. No source-level name survives in the available PDB.
    const char16_t* FindClassString(int role) const noexcept;

private:
    std::map<int, std::u16string> m_mapClassStr;
};


void CategoryContentInit();
void SetupCategoryDB();

} // namespace CategoryContent
