#include "L2Server/src/TotalChecker.h"

#include "Shared/src/Log.h"
#include "Shared/src/ParserSpawn.h"
#include "TextRuntime.h"

#include <string_view>

QuestHandlerEventDB g_AttackedEventDB;
QuestHandlerEventDB g_CreatedEventDB;
QuestHandlerEventDB g_MenuSelectedEventDB;
QuestHandlerEventDB g_MyDyingEventDB;
QuestHandlerEventDB g_QuestAcceptedEventDB;
QuestHandlerEventDB g_ScriptEventEventDB;
QuestHandlerEventDB g_SpelledEventDB;
QuestHandlerEventDB g_TalkSelectedEventDB;
QuestHandlerEventDB g_TalkedEventDB;
QuestHandlerEventDB g_TimerFiredEventDB;
QuestHandlerEventDB g_TimerFiredExEventDB;

AIClassInfo::AIClassInfo() : mpSuper(nullptr) {}

void AIClassInfo::AddParameter(const char16_t* name) { mParamSet.insert(std::u16string(name)); }

bool AIClassInfo::HaveParameter(const char16_t* name) const {
    const AIClassInfo* current = this;
    const std::u16string key(name);
    while (current != nullptr) {
        if (current->mParamSet.find(key) != current->mParamSet.end()) {
            return true;
        }
        current = current->mpSuper;
    }
    return false;
}

AIClassInfoTable* AIClassInfoTable::inst() {
    static AIClassInfoTable table;
    return &table;
}

AIClassInfo* AIClassInfoTable::GetClassInfoW(const char16_t* name) const {
    const auto found = mAIClassMap.find(name);
    return found == mAIClassMap.end() ? nullptr : found->second;
}

void AIClassInfoTable::AddClassInfo(AIClassInfo* info) {
    if (info != nullptr) {
        mAIClassMap[info->mName] = info;
    }
}

CNpcPosChecker* CNpcPosChecker::Inst() {
    static CNpcPosChecker checker;
    return &checker;
}

bool CNpcPosChecker::LoadData(char16_t* text, int codeUnitCount) {
    L2_GUARD_FUNCTION();

    CParserForNPCSpawn parser;
    parser.m_bOnlyMakerEventList = m_bOnlyMakerEventList;
    if (!parser.Create(text, codeUnitCount)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CParserForNPCSpawn::create failed")
        // Log.Add(
        //     LOG_ERROR,
        //     L"[npc positions] Cannot initialize the checker parser.")
        L2_LOG_ERROR(Log, u"[npc positions] Cannot initialize the checker parser.");
        return false;
    }

    parser.mCheckAI = true;
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Parsing %s failed at line[%d]",
        //     L"..\\Script\\npcpos.txt",
        //     parser.m_lexer.yylineno)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[npc positions] Parsing input failed near line %d.",
        //     parser.m_lexer.yylineno)
        L2_LOG_ERROR(Log, u"[npc positions] Parsing input failed near line %d.", parser.m_lexer.yylineno);
        return false;
    }

    // Log.Add(
    //     LOG_NORMAL,
    //     L"--- NpcPos Data Checking finished ---")
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[npc positions] NPC position checks completed.")
    L2_LOG_INFO(Log, u"[npc positions] NPC position checks completed.");
    return true;
}

void CNpcPosChecker::Uniputs(const char16_t* text) {
    L2_GUARD_FUNCTION();
    if (m_maker_event_list_file == nullptr || text == nullptr) {
        return;
    }

#if defined(_WIN32)
    std::fputws(reinterpret_cast<const wchar_t*>(text), m_maker_event_list_file);
#else
    const std::wstring hostText = Utf16ToWide(std::u16string_view(text));
    std::fputws(hostText.c_str(), m_maker_event_list_file);
#endif
}
