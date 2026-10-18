#pragma once

#include "Protocol/src/L2Constants.h"

#include "Shared/src/StandardContainers.h"


// Current L2NPC PDB enum and Shared/pledgetype.h. The shipping image
// embeds the same ordered eight-value table used by pledgetype.cpp.
// enum PledgeType : int {
//     ACADEMY = -1,
//     MASTER_PLEDGE = 0,
//     SUB_1 = 100,
//     SUB_2 = 200,
//     SUB_1_1 = 1001,
//     SUB_1_2 = 1002,
//     SUB_2_1 = 2001,
//     SUB_2_2 = 2002,
//     MAX_PLEDGE_TYPE = 2003,
// }

class CPledgeTypeValidator {
public:
    CPledgeTypeValidator();

    bool IsValid(PledgeType type);
    std::vector<PledgeType> GetPledgetTypeList();
    unsigned int GetIndex(PledgeType type);
    PledgeType GetPledgeTypeByIndex(unsigned int index);

private:
    std::vector<PledgeType> m_pledgeTypeVec;
};


extern CPledgeTypeValidator g_PledgeTypeValidator;
