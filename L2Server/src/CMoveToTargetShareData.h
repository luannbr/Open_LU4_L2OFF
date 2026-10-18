#pragma once

#include <cstddef>

// Exact current CMoveToTargetActionBase embedded carrier at +0x9c.
// Current L2Server accesses all five fields at +0/+4/+8/+12/+16. The
// TotalChecker type record supplies only the original member spellings.
class CMoveToTargetShareData {
public:
    int m_nDistance;
    int m_nAcceptableDistance;
    int m_nSkillId;
    int m_nSkillRange;
    bool m_bForceAttack;
};
