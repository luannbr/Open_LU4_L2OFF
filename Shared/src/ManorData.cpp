#include "Shared/src/ManorData.h"

#include "Shared/src/CenteredSharedRWLock.h"
#include "Shared/src/SharedType.h"

CSharedRWLock g_ManorLock(L"L2ManorSMLock", 0, nullptr, 4000);

CManorData& CManorData::Inst(int index) {
    // Current L2NPC.exe 0x00469194 indexes the process-lifetime,
    // function-local CSharedVector<CManorData,16,2,4> named g_ManorData
    // in the PDB.
    static CSharedVector<CManorData, 16, 2, 4> g_ManorData(L"L2ManorSMData");
    return g_ManorData[index];
}
