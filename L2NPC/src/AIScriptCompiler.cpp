#include "L2NPC/src/AIScriptCompiler.h"

#include <cstdio>

int GetAIScriptCompilerVersion() {
    // Current L2NPC.exe 0x0040e8a4. Original PDB source lines 15-20.
    char sTmp[80];
    int nVersion;
#if defined(_MSC_VER)
    ::sscanf_s("$Revision: #73 $", "%s #%d", sTmp, static_cast<unsigned>(sizeof(sTmp)), &nVersion);
#else
    std::sscanf("$Revision: #73 $", "%79s #%d", sTmp, &nVersion);
#endif
    return nVersion;
}

void InitScriptAccessibleVariable() {
    // Original PDB source lines 23-24. The empty, unreferenced body was
    // discarded from the current shipping executable.
}
