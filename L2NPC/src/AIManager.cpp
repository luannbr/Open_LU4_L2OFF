#include "AIManager.h"
#include "Shared/src/Runtime/ResourceDirectories.h"
#include "Shared/src/Runtime/Utf8.h"


#include "L2Core/src/inc/Guard.h"
#include "L2NPC/src/Config.h"
#include "LoaderParser.h"
#include "NPC.h"
#include "NPCEventFactory.h"
#include "NpcEvent.h"
#include "Runtime.h"
#include "ScriptBinCode.h"
#include "Shared/src/Log.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadManager.h"
#include "TextRuntime.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>


CNPC* g_TutorialNPC = nullptr;
CNPC* g_DominionNPC = nullptr;
CNPC* g_FishingNPC = nullptr;

RWLock CNPCAIManager::s_lock;
CNPCAIManager::map_type CNPCAIManager::s_mapClassToEventHandler;
bool CNPCAIManager::s_bLoaded = false;
bool CNPCAIManager::m_bEventCreated = false;

namespace {

std::filesystem::path LoaderFileSystemPath(std::u16string_view targetPath) {
    std::u16string path(targetPath);
#if !defined(_WIN32)
    for (char16_t& character : path) {
        if (character == u'\\') {
            character = u'/';
        }
    }
#endif
    return runtime::ResolveResourcePath(std::filesystem::path(runtime::Utf8CodeUnits(Utf16ToUtf8(path))));
}

} // namespace

CReloadableNPCEventHandler* CNPCAIManager::FindClass(int type_id, const char16_t* pszClassName) {
    // Current L2NPC.exe 0x0040c2a8.
    L2_GUARD_FUNCTION();
    const key_type key(type_id, pszClassName);
    CReloadableNPCEventHandler* ret = nullptr;
    L2_READ_GUARD(s_lock);
    const map_type::iterator it = s_mapClassToEventHandler.find(key);
    if (it != s_mapClassToEventHandler.end()) {
        ret = it->second;
    }
    return ret;
}

int CNPCAIManager::AddClass(int typeID, const char16_t* className, CNPCEventHandler* pHandler, int scriptType) {
    // Current L2NPC.exe 0x0040dd2c.
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(s_lock);
    const key_type key(typeID, className);
    const map_type::iterator it = s_mapClassToEventHandler.find(key);
    if (it == s_mapClassToEventHandler.end()) {
        CReloadableNPCEventHandler* evHandler = new CReloadableNPCEventHandler(className, pHandler);
        evHandler->ScriptType(scriptType);
        s_mapClassToEventHandler[key] = evHandler;
    } else {
        it->second->InterlockedSetEventHandler(pHandler);
    }
    return 1;
}

int CNPCAIManager::ReplaceClass(int typeID, const char16_t* className, CNPCEventHandler* pHandler, int scriptType) {
    // Current L2NPC.exe 0x0040c3e0. The PDB name is recovered from its
    // surviving function-signature guard string.
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(s_lock);
    const key_type key(typeID, className);
    const map_type::iterator it = s_mapClassToEventHandler.find(key);
    if (it == s_mapClassToEventHandler.end()) {
        return 0;
    }
    it->second->InterlockedSetEventHandler(pHandler);
    static_cast<void>(scriptType);
    return 1;
}

int CNPCAIManager::LoadNPCAI(char16_t* objFile) {
    // Current L2NPC.exe 0x0040d5d8.
    L2_GUARD_FUNCTION();
    CParserForLoader parser;
    std::u16string targetPath = u"..\\script\\";
    targetPath += objFile;

    if (parser.Create(targetPath.data(), false) == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Fail to create loader parser")
        // Log.Add(LOG_ERROR,
        //     "[AI load] Cannot create the parser for '%s'.",
        //     LoaderFileSystemPath(targetPath).u8string().c_str())
        L2_LOG_ERROR(
            Log, "[AI load] Cannot create the parser for '%s'.",
            runtime::Utf8Bytes(LoaderFileSystemPath(targetPath).u8string()).c_str()
        );
        return 0;
    }

    std::wifstream input(LoaderFileSystemPath(targetPath), std::ios::in);
    if (!input) {
        // Current 0x0040d766 loads the 32-bit lexer line into R9D even
        // though the format consumes a string pointer. Preserve that
        // shipping vararg mismatch instead of substituting the path.
        // The original call is kept below. The logging review uses the resolved path.
        // Log.Add(
        //     LOG_ERROR,
        //     L"Fail to open ai.obj[%s]",
        //     parser.m_lexer.line)
        // Log.Add(LOG_ERROR,
        //     "[AI load] Cannot open '%s' for reading.",
        //     LoaderFileSystemPath(targetPath).u8string().c_str())
        L2_LOG_ERROR(
            Log, "[AI load] Cannot open '%s' for reading.",
            runtime::Utf8Bytes(LoaderFileSystemPath(targetPath).u8string()).c_str()
        );
        return 0;
    }

    parser.m_lexer.yyin = &input;
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"AI Script parsing error. Line[%d]",
        //     parser.m_lexer.line)
        // Log.Add(LOG_ERROR,
        //     "[AI load] Parse error in '%s' at line %d. Clearing the AI class registry.",
        //     LoaderFileSystemPath(targetPath).u8string().c_str(), parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     "[AI load] Parse error in '%s' at line %d. Clearing the AI class registry.",
        //     LoaderFileSystemPath(targetPath).u8string().c_str(), parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "[AI load] Parse error in '%s' at line %lld; clearing the AI class registry.",
            runtime::Utf8Bytes(LoaderFileSystemPath(targetPath).u8string()).c_str(),
            static_cast<long long>(parser.m_lexer.line) + 1
        );
        s_mapClassToEventHandler.clear();
        s_bLoaded = false;
        return 0;
    }
    if (parser.yyerrorcount != 0) {
        L2_LOG_WARN(
            Log,
            "[AI load] Parser accepted '%s' after reporting %d errors; the loader still returns success. Check the "
            "preceding parser diagnostics.",
            runtime::Utf8Bytes(LoaderFileSystemPath(targetPath).u8string()).c_str(), parser.yyerrorcount
        );
    }
    return 1;
}

int CNPCAIManager::LoadNPCAI(bool isLiveReload) {
    // Current L2NPC.exe 0x0040d8ac.
    L2_GUARD_FUNCTION();
    CParserForLoader parser;

    if (parser.Create(const_cast<char16_t*>(u"..\\script\\ai.obj"), isLiveReload) == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Fail to create loader parser")
        // Log.Add(LOG_ERROR,
        //     "[AI load] Cannot create the %s parser for '%s'.",
        //     (isLiveReload ? "reload" : "startup"), LoaderFileSystemPath(u"..\\script\\ai.obj").u8string().c_str())
        L2_LOG_ERROR(
            Log, "[AI load] Cannot create the %s parser for '%s'.", (isLiveReload ? "reload" : "startup"),
            runtime::Utf8Bytes(LoaderFileSystemPath(u"..\\script\\ai.obj").u8string()).c_str()
        );
        return 0;
    }

    std::wifstream input(LoaderFileSystemPath(u"..\\script\\ai.obj"), std::ios::in);
    if (!input) {
        // Current 0x0040d9b7 repeats the same line-as-%s vararg bug as the
        // named-file overload.
        // Log.Add(
        //     LOG_ERROR,
        //     L"Fail to open ai.obj[%s]",
        //     parser.m_lexer.line)
        // Log.Add(LOG_ERROR,
        //     "[AI load] Cannot open '%s' for %s.",
        //     LoaderFileSystemPath(u"..\\script\\ai.obj").u8string().c_str(), isLiveReload ? "reload" : "startup")
        L2_LOG_ERROR(
            Log, "[AI load] Cannot open '%s' for %s.",
            runtime::Utf8Bytes(LoaderFileSystemPath(u"..\\script\\ai.obj").u8string()).c_str(),
            isLiveReload ? "reload" : "startup"
        );
        return 0;
    }

    parser.m_lexer.yyin = &input;
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"AI Script parsing error. Line[%d]",
        //     parser.m_lexer.line)
        // Log.Add(LOG_ERROR,
        //     "[AI load] Parse error in '%s' at line %d. Clearing the AI class registry.",
        //     LoaderFileSystemPath(u"..\\script\\ai.obj").u8string().c_str(), parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     "[AI load] Parse error in '%s' at line %d. Clearing the AI class registry.",
        //     LoaderFileSystemPath(u"..\\script\\ai.obj").u8string().c_str(), parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "[AI load] Parse error in '%s' at line %lld; clearing the AI class registry.",
            runtime::Utf8Bytes(LoaderFileSystemPath(u"..\\script\\ai.obj").u8string()).c_str(),
            static_cast<long long>(parser.m_lexer.line) + 1
        );
        s_mapClassToEventHandler.clear();
        s_bLoaded = false;
        return 0;
    }
    if (!parser.CompileSucceeded()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Fail to load. Reset the AI. Line[%d]",
        //     parser.m_lexer.line)
        // Log.Add(LOG_ERROR,
        //     "[AI load] Compilation failed for '%s' at line %d. Clearing the AI class registry.",
        //     LoaderFileSystemPath(u"..\\script\\ai.obj").u8string().c_str(), parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     "[AI load] Compilation failed for '%s' at line %d. Clearing the AI class registry.",
        //     LoaderFileSystemPath(u"..\\script\\ai.obj").u8string().c_str(), parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "[AI load] Compilation failed for '%s' at line %lld; clearing the AI class registry.",
            runtime::Utf8Bytes(LoaderFileSystemPath(u"..\\script\\ai.obj").u8string()).c_str(),
            static_cast<long long>(parser.m_lexer.line) + 1
        );
        s_mapClassToEventHandler.clear();
        s_bLoaded = false;
        return 0;
    }

    // Log.Add(
    //     LOG_NORMAL,
    //     L"Loading NPC AI succeess! "
    //     L"Allocated string size[%d]",
    //     parser.m_lexer.mWordSize)
    // Log.Add(LOG_NORMAL,
    //     "[AI load] Parsed '%s'. Lexer string allocation counter is %d bytes.",
    //     LoaderFileSystemPath(u"..\\script\\ai.obj").u8string().c_str(), parser.m_lexer.mWordSize)
    // Original: L2_LOG_INFO(Log,
    //     "[AI load] Parsed '%s'. Lexer string allocation counter is %d bytes.",
    //     LoaderFileSystemPath(u"..\\script\\ai.obj").u8string().c_str(), parser.m_lexer.mWordSize)
    if (parser.yyerrorcount != 0) {
        L2_LOG_WARN(
            Log,
            "[AI load] Parser accepted '%s' after reporting %d errors; compilation reported success and AI initialization "
            "continues. Lexer string allocation counter is %d bytes.",
            runtime::Utf8Bytes(LoaderFileSystemPath(u"..\\script\\ai.obj").u8string()).c_str(), parser.yyerrorcount,
            parser.m_lexer.mWordSize
        );
    } else {
        L2_LOG_INFO(
            Log, "[AI load] Parsed '%s'; lexer string allocation counter is %d bytes.",
            runtime::Utf8Bytes(LoaderFileSystemPath(u"..\\script\\ai.obj").u8string()).c_str(), parser.m_lexer.mWordSize
        );
    }
    s_bLoaded = true;

    g_TutorialNPC = new CNPC();
    g_TutorialNPC->Set(0, 0);
    g_TutorialNPC->SetAI(u"tutorial_quest_npc");

    g_DominionNPC = new CNPC();
    g_DominionNPC->Set(0, 0);
    g_DominionNPC->SetAI(u"dominion_quest_npc");

    g_FishingNPC = new CNPC();
    g_FishingNPC->Set(0, 0);
    g_FishingNPC->SetAI(u"fishing_quest_npc");

    g_TutorialNPC->m_data->m_cNoSleepMode = 1;
    g_DominionNPC->m_data->m_cNoSleepMode = 1;
    g_FishingNPC->m_data->m_cNoSleepMode = 1;

    g_ScriptCodeInfo.Sort();
    g_ScriptCodeInfo.Dump();
    return 1;
}

void CNPCAIManager::ShowAllNpcAI() {
    // Current L2NPC.exe 0x00409b3c.
    L2_GUARD_FUNCTION();
}

void CNPCAIManager::CreateDominionNPCCreatedEvent() {
    // Current L2NPC.exe 0x00409bb8.
    L2_GUARD_FUNCTION();
    if (m_bEventCreated) {
        return;
    }

    CNPCEvent* const pEvent = CNPCEventFactory::CreateCreatedEvent(g_DominionNPC, 0);
    if (pEvent) {
        g_DominionNPC->HandleEvent(pEvent);
        L2_RELEASE(pEvent, ObjectRefType::CreateDelete);
    }
    m_bEventCreated = true;
}
