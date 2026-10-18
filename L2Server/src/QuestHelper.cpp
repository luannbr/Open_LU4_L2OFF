#include "QuestHelper.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/ResourceDirectories.h"
#include "Shared/src/Runtime/Utf8.h"

#include "NPC.h"
#include "Object.h"
#include "ObjectDB.h"
#include "QuestHelperQuestParser.h"
#include "QuestHelperUserQuestParser.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <shellapi.h>
#include <sys/stat.h>
#include <windows.h>
#endif

CQuestHelper g_QuestHelper;

CParserBase::CParserBase(const QuestHelper::Event type, const QuestHelper::FileLocation fileLocation) :
    m_eType(type), m_eFileLocation(fileLocation) {}

CTalkSelectedParser::CTalkSelectedParser(const QuestHelper::FileLocation fileLocation) :
    CParserBase(QuestHelper::TalkSelectedEvent, fileLocation) {}

CQuestAccpetedParser::CQuestAccpetedParser(const QuestHelper::FileLocation fileLocation) :
    CParserBase(QuestHelper::QuestAccpetedEvent, fileLocation) {}

CMenuSelectParser::CMenuSelectParser(const QuestHelper::FileLocation fileLocation) :
    CParserBase(QuestHelper::MenuSelectedEvent, fileLocation) {}

CParserQuest::CParserQuest(CParserForQuestHelperQuest* const parser) : pParser(parser) {}

CParserUserQuest::CParserUserQuest(CParserForQuestHelperUserQuest* const parser) : pParser(parser) {}

CQuestTalkSelectedParser::CQuestTalkSelectedParser(CParserForQuestHelperQuest* const parser) :
    CTalkSelectedParser(QuestHelper::FileLocationQuest), CParserQuest(parser) {}

CQuestQuestAccpetedParser::CQuestQuestAccpetedParser(CParserForQuestHelperQuest* const parser) :
    CQuestAccpetedParser(QuestHelper::FileLocationQuest), CParserQuest(parser) {}

CQuestMenuSelectParser::CQuestMenuSelectParser(CParserForQuestHelperQuest* const parser) :
    CMenuSelectParser(QuestHelper::FileLocationQuest), CParserQuest(parser) {}

CUserQuestTalkSelectedParser::CUserQuestTalkSelectedParser(CParserForQuestHelperUserQuest* const parser) :
    CTalkSelectedParser(QuestHelper::FileLocationUserQuest), CParserUserQuest(parser) {}

CUserQuestMenuSelectParser::CUserQuestMenuSelectParser(CParserForQuestHelperUserQuest* const parser) :
    CMenuSelectParser(QuestHelper::FileLocationUserQuest), CParserUserQuest(parser) {}

bool CQuestHelperUtil::findAndSubString(
    std::u16string& orgStr, wchar_t* const startStr, wchar_t* const endStr, std::u16string* const outStr
) {
    // Current L2Server.exe 0x00cddd74.
    if (orgStr.empty() || outStr == nullptr) {
        return false;
    }

    outStr->assign(u"");
    const std::u16string start = WideToUtf16(startStr);
    const std::u16string end = WideToUtf16(endStr);
    const std::u16string_view source = runtime::StringView(orgStr);
    const std::size_t startPosition = source.find(start, 0);
    const std::size_t endPosition = source.find(end, startPosition);
    if (startPosition == std::u16string_view::npos || endPosition == std::u16string_view::npos) {
        return false;
    }

    const std::size_t valuePosition = startPosition + start.size();
    outStr->assign(source.substr(valuePosition, endPosition - valuePosition));
    return true;
}

CQuestHelper::CQuestHelper() :
    m_bUseQuestOpen(false),
    m_bUseHTMLOpen(false),
    m_nLoaded(0) {
    // Current L2Server.exe 0x00ce1a20.
}

// Current L2Server.exe 0x00ce1b5c.
L2_DEFINE_OBJECT_RELEASE(CQuestHelper)

void CQuestHelper::openNotePadPlusPlus(const wchar_t* const fileName, const std::int64_t line) {
    // Current L2Server.exe 0x00cdd0c0.
#if defined(_WIN32)
    const auto hostFileName = runtime::ResolveResourcePath(fileName).wstring();
    struct _stat64i32 status{};
    if (_wstat64i32(hostFileName.c_str(), &status) == 0) {
        wchar_t arguments[1024]{};
        ::wsprintfW(arguments, L"%s -n%I64d", hostFileName.c_str(), line);
        ::ShellExecuteW(nullptr, L"open", L"notepad++.exe", arguments, nullptr, SW_SHOWNORMAL);
        return;
    }
#else
    const std::string portableName = WideToUtf8(fileName);
    if (std::filesystem::exists(
            runtime::ResolveResourcePath(std::filesystem::path(runtime::Utf8CodeUnits(portableName)))
        )) {
        return;
    }
#endif
    // Log.Add(
    //     LOG_ERROR,
    //     L"[QuestHelper]Not found file : %s",
    //     fileName)
    // L2_LOG_ERROR(Log,
    //     L"[QuestHelper]Not found file : %s",
    //     fileName)
    L2_LOG_ERROR(
        Log, "Quest helper cannot open '%s' at line %lld: the file is unavailable.",
        fileName == nullptr ? "<null>" : WideToUtf8(fileName).c_str(), static_cast<long long>(line)
    );
}

void CQuestHelper::openHTML(wchar_t* const fileName, const std::int64_t line) {
    // Current L2Server.exe 0x00cdd1d8.
    L2_GUARD_FUNCTION();
    if (!m_bUseHTMLOpen) {
        return;
    }

    std::array<wchar_t, 1024> path{};
    std::swprintf(path.data(), path.size(), L"%ls%ls", L"..\\HTML\\", fileName);
    openNotePadPlusPlus(path.data(), line);
}

bool CQuestHelper::load() {
    // Current L2Server.exe 0x00cde278.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_lock);
    }

    if (m_nLoaded != 0) {
        return true;
    }

#if defined(_WIN32)
    constexpr wchar_t kQuestFile[] = L"..\\Script\\questdata.txt";
#else
    constexpr wchar_t kQuestFile[] = L"../script/questdata.txt";
#endif
    CSecureStream questInput(kQuestFile, true);
    if (questInput.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Read from %s file failed",
        //     kQuestFile)
        // L2_LOG_ERROR(Log,
        //     L"Read from %s file failed",
        //     kQuestFile)
        L2_LOG_ERROR(Log, "Quest helper cannot read questdata.txt; loading stops and existing helper entries are retained.");
        return false;
    }

    CParserForQuestHelperQuest questParser;
    if (!questParser.Create(questInput.GetRawString() + 1, questInput.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ParserForQuestHelper.create failed",
        //     L"QuestHelper.cpp",
        //     0xe2)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] ParserForQuestHelper.create failed",
        //     L"QuestHelper.cpp",
        //     0xe2)
        L2_LOG_ERROR(
            Log,
            "Cannot create the quest-helper parser for questdata.txt; loading stops and existing helper entries are "
            "retained."
        );
        return false;
    }
    questParser.m_lexer.yybegin(4);
    questParser.m_lexer.yyin = questInput.GetStream();
    if (questParser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing QuestHelper file failed after line[%d]",
        //     L"QuestHelper.cpp",
        //     0xef,
        //     questParser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing QuestHelper file failed after line[%d]",
        //     L"QuestHelper.cpp",
        //     0xef,
        //     questParser.m_lexer.line)
        L2_LOG_ERROR(
            Log,
            "Quest-helper parsing of questdata.txt failed at line %lld; entries collected before the error are retained.",
            static_cast<long long>(questParser.m_lexer.line) + 1
        );
        return false;
    }

#if defined(_WIN32)
    constexpr wchar_t kUserQuestFile[] = L"..\\Script\\UserQuestData.txt";
#else
    constexpr wchar_t kUserQuestFile[] = L"../script/UserQuestData.txt";
#endif
    CSecureStream userQuestInput(kUserQuestFile, true);
    if (userQuestInput.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Read from %s file failed",
        //     kUserQuestFile)
        // L2_LOG_ERROR(Log,
        //     L"Read from %s file failed",
        //     kUserQuestFile)
        L2_LOG_ERROR(Log, "Quest helper cannot read UserQuestData.txt; loading stops with questdata.txt entries retained.");
        return false;
    }

    CParserForQuestHelperUserQuest userQuestParser;
    if (!userQuestParser.Create(userQuestInput.GetRawString() + 1, userQuestInput.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForQuestHelperUserQuest.create failed",
        //     L"QuestHelper.cpp",
        //     0xff)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForQuestHelperUserQuest.create failed",
        //     L"QuestHelper.cpp",
        //     0xff)
        L2_LOG_ERROR(
            Log,
            "Cannot create the quest-helper parser for UserQuestData.txt; loading stops with questdata.txt entries retained."
        );
        return false;
    }
    userQuestParser.m_lexer.yybegin(4);
    userQuestParser.m_lexer.yyin = userQuestInput.GetStream();
    if (userQuestParser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing QuestHelperUserQuest file failed after line[%d]",
        //     L"QuestHelper.cpp",
        //     0x10c,
        //     questParser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing QuestHelperUserQuest file failed after line[%d]",
        //     L"QuestHelper.cpp",
        //     0x10c,
        //     questParser.m_lexer.line)
        L2_LOG_ERROR(
            Log,
            "Quest-helper parsing of UserQuestData.txt failed at line %lld; entries collected from both files before the "
            "error are retained.",
            static_cast<long long>(userQuestParser.m_lexer.m_line) + 1
        );
        return false;
    }

    runtime::raw_atomic::CompareExchangeI32(&m_nLoaded, 1, 0);
    return true;
}

void CQuestHelper::reload() {
    // Current L2Server.exe 0x00ce0828.
    L2_GUARD_FUNCTION();
    m_mTalkSelectedEventData.clear();
    m_mQuestAccpetedEventData.clear();
    m_mMenuSelectedEventData.clear();
    runtime::raw_atomic::CompareExchangeI32(&m_nLoaded, 0, 1);
    static_cast<void>(load());
}

void CQuestHelper::insert(std::u16string npcName, CQuestHelperEventDataBase* const eventData) {
    // Current L2Server.exe 0x00ce0a84.
    switch (eventData->m_eType) {
        case QuestHelper::TalkSelectedEvent: {
            const auto value = *static_cast<CQuestHelperTalkSelectedEventData*>(eventData);
            const auto found = m_mTalkSelectedEventData.find(npcName);
            if (found == m_mTalkSelectedEventData.end()) {
                TalkEventList values;
                values.push_back(value);
                m_mTalkSelectedEventData.emplace(std::move(npcName), std::move(values));
            } else {
                found->second.push_back(value);
            }
            return;
        }
        case QuestHelper::QuestAccpetedEvent: {
            const auto value = *static_cast<CQuestHelperQuestAccpetedEventData*>(eventData);
            const auto found = m_mQuestAccpetedEventData.find(npcName);
            if (found == m_mQuestAccpetedEventData.end()) {
                g_QuestHelper.m_mQuestAccpetedEventData[npcName].push_back(value);
            } else {
                found->second.push_back(value);
            }
            return;
        }
        case QuestHelper::MenuSelectedEvent: {
            const auto value = *static_cast<CQuestHelperMenuSelectedEventData*>(eventData);
            const auto found = m_mMenuSelectedEventData.find(npcName);
            if (found == m_mMenuSelectedEventData.end()) {
                MenuEventList values;
                values.push_back(value);
                m_mMenuSelectedEventData.emplace(std::move(npcName), std::move(values));
            } else {
                found->second.push_back(value);
            }
            return;
        }
        default:
            return;
    }
}

void CQuestTalkSelectedParser::parse(CQuestHelper& helper) {
    // Current L2Server.exe 0x00ce0fc0.
    std::wistringstream input(Utf16ToWide(pParser->Handler));
    std::wstring hostLine;
    std::u16string extracted;

    while (std::getline(input, hostLine)) {
        std::u16string line(WideToUtf16(hostLine));
        const auto emit = [&](const wchar_t* const start) {
            if (!CQuestHelperUtil::findAndSubString(
                    line, const_cast<wchar_t*>(start), const_cast<wchar_t*>(L"\")"), &extracted
                )) {
                return;
            }
            CQuestHelperTalkSelectedEventData eventData;
            eventData.m_nQuestID = pParser->QuestID;
            eventData.m_eType = m_eType;
            eventData.m_eFileLocation = m_eFileLocation;
            eventData.m_nLine = pParser->HandlerLine;
            eventData.m_nChoice = pParser->Choice;
            eventData.m_strScriptFileName = extracted;
            helper.insert(pParser->NPCName, &eventData);
        };

        emit(L"ShowPage(talker, \"");
        emit(L"ShowQuestPage(talker, \"");
        emit(L"FHTML_SetFileName(fhtml0, \"");
    }
}

void CQuestQuestAccpetedParser::parse(CQuestHelper&) {
    // Current L2Server.exe 0x00ce1554.
    CQuestHelperQuestAccpetedEventData eventData;
    eventData.m_nQuestID = pParser->QuestID;
    eventData.m_eType = m_eType;
    eventData.m_eFileLocation = m_eFileLocation;
    eventData.m_nLine = pParser->HandlerLine;
    g_QuestHelper.insert(pParser->NPCName, &eventData);
}

void CQuestMenuSelectParser::parse(CQuestHelper& helper) {
    // Current L2Server.exe 0x00ce15d0.
    std::wistringstream input(Utf16ToWide(pParser->Handler));
    std::wstring hostLine;
    std::u16string extracted;
    std::int64_t lineNumber = pParser->HandlerLine;
    std::int64_t ask = 0;
    std::uint32_t braceDepth = 0;
    bool inAsk = false;

    while (std::getline(input, hostLine)) {
        ++lineNumber;
        std::u16string line(WideToUtf16(hostLine));
        if (inAsk) {
            if (CQuestHelperUtil::findAndSubString(
                    line, const_cast<wchar_t*>(L"reply == "), const_cast<wchar_t*>(L")"), &extracted
                )) {
                CQuestHelperMenuSelectedEventData eventData;
                eventData.m_nQuestID = pParser->QuestID;
                eventData.m_eType = m_eType;
                eventData.m_eFileLocation = m_eFileLocation;
                eventData.m_nLine = lineNumber;
                eventData.m_nAsk = ask;
                eventData.m_nReply = WindowsWtol(Utf16ToWide(extracted));
                helper.insert(pParser->NPCName, &eventData);
            }
            if (hostLine.find(L'{') != std::wstring::npos) {
                ++braceDepth;
            }
            if (hostLine.find(L'}') != std::wstring::npos) {
                --braceDepth;
            }
            if (braceDepth == 0) {
                inAsk = false;
            }
        } else if (CQuestHelperUtil::findAndSubString(
                       line, const_cast<wchar_t*>(L"ask == "), const_cast<wchar_t*>(L")"), &extracted
                   )) {
            ask = WindowsWtol(Utf16ToWide(extracted));
            inAsk = true;
            if (hostLine.find(L'{') != std::wstring::npos) {
                ++braceDepth;
            }
        }
    }
}

void CUserQuestTalkSelectedParser::parse(CQuestHelper& helper) {
    // Current L2Server.exe 0x00ce13e4.
    CQuestHelperTalkSelectedEventData eventData;
    eventData.m_nQuestID = pParser->QuestID;
    eventData.m_eType = m_eType;
    eventData.m_eFileLocation = m_eFileLocation;
    eventData.m_nLine = pParser->m_line;
    eventData.m_nChoice = pParser->m_choice;
    eventData.m_strScriptFileName = pParser->strHtmlFileName;

    for (const std::int32_t npcClassId : pParser->m_npcList) {
        CObject* const object = g_ObjectDB.GetObjectW(npcClassId, false);
        if (object != nullptr && object->IsNPC()) {
            CNPC* const npc = object->CastNPC();
            helper.insert(std::u16string(npc->GetName()), &eventData);
        }
    }
}

void CUserQuestMenuSelectParser::parse(CQuestHelper& helper) {
    // Current L2Server.exe 0x00ce18f8.
    CQuestHelperMenuSelectedEventData eventData;
    eventData.m_nQuestID = pParser->QuestID;
    eventData.m_eType = m_eType;
    eventData.m_eFileLocation = m_eFileLocation;
    eventData.m_nLine = pParser->m_line;
    eventData.m_nAsk = pParser->m_ask;
    eventData.m_nReply = pParser->m_reply;

    for (const std::int32_t npcClassId : pParser->m_npcList) {
        CObject* const object = g_ObjectDB.GetObjectW(npcClassId, false);
        if (object != nullptr && object->IsNPC()) {
            CNPC* const npc = object->CastNPC();
            helper.insert(std::u16string(npc->GetName()), &eventData);
        }
    }
}

void CQuestHelper::openNotePadPlusPlus(const CQuestHelperEventDataBase* const eventData, const std::int64_t line) {
    // Current L2Server.exe 0x00cdd2d4.
    switch (eventData->m_eFileLocation) {
        case QuestHelper::FileLocationQuest:
            openNotePadPlusPlus(L"..\\Script\\questdata.txt", line);
            return;
        case QuestHelper::FileLocationUserQuest:
            openNotePadPlusPlus(L"..\\Script\\UserQuestData.txt", line);
            return;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[QuestHelper]Not found file location enum")
            // L2_LOG_ERROR(Log,
            //     L"[QuestHelper]Not found file location enum")
            L2_LOG_ERROR(
                Log, "Quest helper cannot open quest %d at line %lld: unsupported file-location code %d.",
                eventData->m_nQuestID, static_cast<long long>(line), static_cast<int>(eventData->m_eFileLocation)
            );
            return;
    }
}

void CQuestHelper::checkAndOpenQuest(const QuestHelper::Event event, const char16_t* npcName, ...) {
    // Current L2Server.exe 0x00ce0c70.
    L2_GUARD_FUNCTION();
    if (!m_bUseQuestOpen) {
        return;
    }

    const std::u16string name(npcName);
    va_list arguments;
    va_start(arguments, npcName);
    switch (event) {
        case QuestHelper::TalkSelectedEvent: {
            const wchar_t* const selection = va_arg(arguments, const wchar_t*);
            const std::u16string targetSelection = WideToUtf16(selection);
            const auto found = m_mTalkSelectedEventData.find(name);
            if (found != m_mTalkSelectedEventData.end()) {
                for (const auto& eventData : found->second) {
                    if (eventData.m_strScriptFileName == targetSelection) {
                        openNotePadPlusPlus(&eventData, eventData.m_nLine);
                    }
                }
            }
            break;
        }
        case QuestHelper::QuestAccpetedEvent: {
            const int questId = va_arg(arguments, int);
            const auto found = m_mQuestAccpetedEventData.find(name);
            if (found != m_mQuestAccpetedEventData.end()) {
                for (const auto& eventData : found->second) {
                    if (eventData.m_nQuestID == questId) {
                        openNotePadPlusPlus(&eventData, eventData.m_nLine);
                    }
                }
            }
            break;
        }
        case QuestHelper::MenuSelectedEvent: {
            const int ask = va_arg(arguments, int);
            const std::int64_t reply = va_arg(arguments, std::int64_t);
            const auto found = m_mMenuSelectedEventData.find(name);
            if (found != m_mMenuSelectedEventData.end()) {
                for (const auto& eventData : found->second) {
                    if (eventData.m_nAsk == ask && eventData.m_nReply == reply) {
                        openNotePadPlusPlus(&eventData, eventData.m_nLine);
                    }
                }
            }
            break;
        }
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[QuestHelper]Not found event enum")
            // L2_LOG_ERROR(Log,
            //     L"[QuestHelper]Not found event enum")
            L2_LOG_ERROR(
                Log, "Quest helper cannot select a source location for NPC '%s': unsupported event code %d.",
                Utf16ToUtf8(npcName).c_str(), static_cast<int>(event)
            );
            break;
    }
    va_end(arguments);
}
