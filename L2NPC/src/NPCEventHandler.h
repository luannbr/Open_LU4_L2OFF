#pragma once

#include "Shared/src/StandardContainers.h"

#include <cstddef>


class CAIParameter;
class CAIProperty;
class CParserForLoader;
class CReloadableNPCEventHandler;
class CScriptBinCode;
struct ScriptEvent;

// Current L2NPC.exe 0x004e3d1c and 0x004e4168..0x004e42c4.
// The adjacent PDB describes the older 0x60-byte revision. The current image
// widened std::map from 0x18 to 0x20 and consequently places the two trailing
// pointers at +0x58/+0x60, for a total size of 0x68.
class CNPCEventHandler {
public:
    CNPCEventHandler(CNPCEventHandler* superEventHandler, CAIParameter* parameter, CAIProperty* property);
    CNPCEventHandler(const CNPCEventHandler& other);
    CNPCEventHandler& operator=(const CNPCEventHandler& other);
    ~CNPCEventHandler();

    int AddEventHandler(int eventId, CScriptBinCode* scriptBinCode);
    void SuperEventHandler(CNPCEventHandler* superEventHandler);
    void SetAIClassName(const char16_t* value);
    const char16_t* GetAIClassName() const;

    virtual int HandleEvent(ScriptEvent* event, CAIParameter* parameter);

protected:
    virtual int HandleEvent(ScriptEvent* event, CAIParameter* parameter, CAIProperty* property);

private:
    friend class CScriptEngine;
    friend class CParserForLoader;
    friend class CReloadableNPCEventHandler;
    friend CNPCEventHandler* GetSuperEventHandler(const CNPCEventHandler* handler);
    friend const CScriptBinCode* FindEventHandlerCode(
        const CNPCEventHandler* handler, int eventId, const CNPCEventHandler** owner
    );

    void CheckEventCount(ScriptEvent* event);

    std::u16string m_sAIClassName; // +0x08
    CNPCEventHandler* m_pSuperEventHandler = nullptr; // +0x30
    std::map<int, CScriptBinCode*> m_mapEventIdToScriptCode; // +0x38
    CAIParameter* m_pParameter = nullptr; // +0x58
    CAIProperty* m_pProperty = nullptr; // +0x60
};


CNPCEventHandler* GetSuperEventHandler(const CNPCEventHandler* handler);
const CScriptBinCode* FindEventHandlerCode(
    const CNPCEventHandler* handler, int eventId, const CNPCEventHandler** owner = nullptr
);
