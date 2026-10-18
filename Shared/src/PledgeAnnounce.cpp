#include "PledgeAnnounce.h"
#include "Shared/src/MemorySub.h"


L2_DEFINE_OBJECT_CLASS_ID(CPledgeAnnounce)

L2_DEFINE_OBJECT_CLASS_NAME(CPledgeAnnounce, m_pClassName_CPledgeAnnounce)

L2_DEFINE_OBJECT_NEW(CPledgeAnnounce)

// Current allocator 0x004dc728 publishes 0x48 bytes. The complete class
// initializer table assigns the entry after CPledge (25), hence ID 26.
L2_DEFINE_OBJECT_ALLOCATOR(CPledgeAnnounce, m_pClassName_CPledgeAnnounce)

L2_DEFINE_OBJECT_RELEASE(CPledgeAnnounce)

// Current Cached.exe 0x004df5d4, 134 bytes.
CPledgeAnnounce::CPledgeAnnounce(unsigned int pledgeId, bool doesShow, const char16_t* content) :
    m_pledgeId(pledgeId), m_doesShow(doesShow) {
    SetContent(content);
}

// Current Cached.exe complete destructor 0x0050a190.
CPledgeAnnounce::~CPledgeAnnounce() = default;

int CPledgeAnnounce::GetId() const { return static_cast<int>(m_pledgeId); }

bool CPledgeAnnounce::DoesShow() const { return m_doesShow; }

void CPledgeAnnounce::SetShowFlag(bool flag) { m_doesShow = flag; }

void CPledgeAnnounce::SetContent(const char16_t* content) { m_content.assign(content); }

std::u16string& CPledgeAnnounce::GetContent() { return m_content; }
