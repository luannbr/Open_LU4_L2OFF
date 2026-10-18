#pragma once

#include "ReloadableNPCEventHandler.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <string>
#include <utility>


class CNPC;

// Exact process pointer slots read by CNPC::AsyncTimerExpired1 and server
// callbacks. The third current-image slot has no surviving PDB name.
extern CNPC* g_TutorialNPC;
extern CNPC* g_DominionNPC;
extern CNPC* g_FishingNPC;

class CNPCAIManager {
public:
    using key_type = std::pair<int, std::u16string>;
    using map_type = std::map<key_type, CReloadableNPCEventHandler*>;

    static RWLock s_lock;
    static map_type s_mapClassToEventHandler;
    static bool s_bLoaded;
    static bool m_bEventCreated;

    static CReloadableNPCEventHandler* FindClass(int type_id, const char16_t* pszClassName);
    static int AddClass(int typeID, const char16_t* className, CNPCEventHandler* pHandler, int scriptType);
    static int ReplaceClass(int typeID, const char16_t* className, CNPCEventHandler* pHandler, int scriptType);

    // The older adjacent PDB calls this overload parameterless. The current
    // executable and matching NASC64_2015 PDB both identify isLiveReload.
    static int LoadNPCAI(bool isLiveReload);
    static int LoadNPCAI(char16_t* objFile);

    static void ShowAllNpcAI();
    static void CreateDominionNPCCreatedEvent();
};
