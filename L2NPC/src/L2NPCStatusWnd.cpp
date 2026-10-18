#include "L2NPC/src/L2NPCStatusWnd.h"

#include "L2Core/src/inc/Guard.h"
#include "L2NPC/src/AIScriptCompiler.h"
#include "L2NPC/src/Default.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Exception.h"
#include "Shared/src/IOC.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/ThreadManager.h"

#include <cstdio>
#include <cstdlib>

L2NPCStatusWnd::L2NPCStatusWnd() {
    // Current L2NPC.exe 0x0045ccd8. Every revision below is present as a
    // "$Revision: #... $" literal in the shipping machine code.
    L2_GUARD_FUNCTION();

    char npcDbOrEventText[80];
    char npcOrSfText[80];
    char npchText[80];
    int nNPCHVersion;
    int nNPCVersion;
    int nNPCDBVersion;
    int nNPCEventVersion;
    int nSFVersion;
#if defined(_MSC_VER)
    ::sscanf_s("$Revision: #317 $", "%s #%d", npchText, static_cast<unsigned>(sizeof(npchText)), &nNPCHVersion);
    ::sscanf_s("$Revision: #272 $", "%s #%d", npcOrSfText, static_cast<unsigned>(sizeof(npcOrSfText)), &nNPCVersion);
    ::sscanf_s(
        "$Revision: #14 $", "%s #%d", npcDbOrEventText, static_cast<unsigned>(sizeof(npcDbOrEventText)), &nNPCDBVersion
    );
#else
    std::sscanf("$Revision: #317 $", "%79s #%d", npchText, &nNPCHVersion);
    std::sscanf("$Revision: #272 $", "%79s #%d", npcOrSfText, &nNPCVersion);
    std::sscanf("$Revision: #14 $", "%79s #%d", npcDbOrEventText, &nNPCDBVersion);
#endif
    SetMessageV(0, L"L2[%d] NPCDB[%d] NPC[%d] NPCH[%d]", GetL2ProtocolVersion(), nNPCDBVersion, nNPCVersion, nNPCHVersion);
#if defined(_MSC_VER)
    ::sscanf_s(
        "$Revision: #10 $", "%s #%d", npcDbOrEventText, static_cast<unsigned>(sizeof(npcDbOrEventText)), &nNPCEventVersion
    );
    ::sscanf_s("$Revision: #289 $", "%s #%d", npcOrSfText, static_cast<unsigned>(sizeof(npcOrSfText)), &nSFVersion);
#else
    std::sscanf("$Revision: #10 $", "%79s #%d", npcDbOrEventText, &nNPCEventVersion);
    std::sscanf("$Revision: #289 $", "%79s #%d", npcOrSfText, &nSFVersion);
#endif
    SetMessageV(1, L"SF[%d] NASC[%d]  NPCEvent[%d]", nSFVersion, GetAIScriptCompilerVersion(), nNPCEventVersion);
    SetMessageV(2, L"BinaryType[%s]", L"RELEASE");
    DisplayCommon();
}

void L2NPCStatusWnd::OnRefresh() {
    // Current L2NPC.exe 0x0045cecc.
    L2_GUARD_FUNCTION();
    DisplayCommon();
}

L2NPCStatusWnd& L2NPCStatusWnd::Inst() {
    // Current L2NPC.exe 0x0045cf70.
    L2_GUARD_FUNCTION();
    static L2NPCStatusWnd instance;
    return instance;
}

void L2NPCStatusWnd::DisplayCommon() {
    // Current L2NPC.exe 0x0045cc0c. The PDB places this definition at
    // l2npcstatuswnd.cpp:56-64, after Inst().
    SetMessageV(
        3, L"NPC [%d](cnt) Max [%d](cnt) Class [%d](cnt)", runtime::raw_atomic::LoadI32Relaxed(&g_nNPCCount),
        runtime::raw_atomic::LoadI32Relaxed(&g_nMaxNPCCount), runtime::raw_atomic::LoadI32Acquire(&g_nClassCount)
    );
    SetMessageV(
        4, L"pending write[%d], pending packet[%d]", g_nSocketPendingWrite.load(), CAbstractPacket::g_nPendingPacket.load()
    );
    SetMessageV(5, L"Server Up Time : %s", GetServerUpTimeString());
    SetMessageV(6, L"Current   Time : %s", GetCurrentTimeString());
    wchar_t* const elapsed = GetElapsedTimeString();
    SetMessageV(7, L"Elapsed   time : %s", elapsed);
    std::free(elapsed);
}
