#pragma once

#include "NPCEventHandler.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>


struct AIConstantData;
struct ScriptEvent;
class CAIParameter;

// Current L2NPC.exe 0x0050b868..0x0050bbe8. The stable wrapper is never
// replaced in CNPCAIManager's map. Reload changes only its first pointer.
class CReloadableNPCEventHandler {
public:
    CReloadableNPCEventHandler(const char16_t* aiClassName, CNPCEventHandler* eventHandler);

    CNPCEventHandler* InterlockedGetEventHandler();
    void InterlockedSetEventHandler(CNPCEventHandler* eventHandler);
    int InterlockedHandleEvent(ScriptEvent* event, CAIParameter* parameter);
    CAIParameter* GetParameter();
    AIConstantData GetParameterData(const char16_t* name);

    // These two four-byte accessors are the tiny current-image functions at
    // 0x0050b868/0x0050b86c.
    void ScriptType(int scriptType) { m_ScriptType = scriptType; }
    int ScriptType() { return m_ScriptType; }

    // Public plain pointer in the matching PDB. Only the named
    // Interlocked* paths use atomic instructions. Keeping this field plain
    // also keeps the target's compiler-generated copy operations.
    CNPCEventHandler* m_pRealHandler = nullptr; // +0x00
    std::u16string m_sAIClassName; // +0x08
    int m_ScriptType = 0; // +0x30
};
