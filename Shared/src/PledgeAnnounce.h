#pragma once

#include "Shared/src/Obj.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <cstddef>
#include <cstdint>

class CPledgeAnnounce;
using CPledgeAnnounceSP = CSPointer<CPledgeAnnounce>;


// Shipping owner: Shared/PledgeAnnounce.{h,cpp}. TotalChecker2015.pdb fixes
// the source member names and API. Current Cached.exe fixes class ID 26, the
// 0x28-byte xwstring carrier and the resulting 0x48-byte object layout.
class CPledgeAnnounce : public MemoryObject {
public:
    CPledgeAnnounce(unsigned int pledgeId, bool doesShow, const char16_t* content);
    ~CPledgeAnnounce() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    int GetId() const;
    bool DoesShow() const;
    void SetShowFlag(bool flag);
    void SetContent(const char16_t* content);
    std::u16string& GetContent();

private:
#if defined(L2_BUILD_L2SERVER)
    L2_DECLARE_OBJECT_REGISTRATION(CPledgeAnnounce)
#else
    L2_DECLARE_OBJECT_REGISTRATION(CPledgeAnnounce)
#endif
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CPledgeAnnounce)

    unsigned int m_pledgeId; // +0x18
    bool m_doesShow; // +0x1c
    std::u16string m_content; // +0x20
};
