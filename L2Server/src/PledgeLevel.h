#pragma once

#include "Shared/src/Obj.h"
#include "Shared/src/Utils.h"

#include <cstddef>
#include <cstdint>

class User;
class CUserSocket;
class CPledge;

// Exact current 0x28 MemoryObject child owned by CPledge. Its only derived
// state is the 0x0c JobTransactionLock at +0x18. The final four bytes are ABI
// tail padding from the eight-byte-aligned MemoryObject base.
class PledgeLevel : public MemoryObject {
public:
    PledgeLevel();

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    // TotalChecker supplies the source-level public spelling. The current
    // L2Server fixes this member at +0x18 through every caller.
    JobTransactionLock m_TransactionLock;

private:
    L2_DECLARE_OBJECT_REGISTRATION(PledgeLevel)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(PledgeLevel)
};


namespace PledgeLevelStuff {

namespace Builder {
void SendPledgeLevelInfoTo(CUserSocket* socket, const CPledge* pledge);
}

bool RequestPledgeLevelUp(User* user);
int GetCostNameValue(int pledgeLevel);

} // namespace PledgeLevelStuff
