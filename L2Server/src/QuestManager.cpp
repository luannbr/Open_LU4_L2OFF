#include "QuestManager.h"
#include "Shared/src/Runtime/ResourceDirectories.h"
#include "Shared/src/Runtime/Utf8.h"

#include "ParserTeleportQuestPath.h"
#include "QuestData.h"
#include "QuestDataYacc.h"

#include "Shared/src/Log.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>

QuestManager::QuestManager() {}

QuestManager::~QuestManager() = default;

void QuestManager::Uniputs(char16_t* text) {
    // Current L2Server.exe 0x00a1349c.
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    std::fputws(reinterpret_cast<const wchar_t*>(text), PCHFile);
#else
    while (*text != u'\0') {
        const std::uint16_t codeUnit = *text++;
        std::fputc(codeUnit & 0xff, PCHFile);
        std::fputc(codeUnit >> 8, PCHFile);
    }
#endif
}

UserQuest* QuestManager::CreateQuest(QuestData* const data) {
    // Current L2Server.exe 0x00a1342c.
    auto* const quest = new UserQuest;
    if (quest->Create(data)) {
        return quest;
    }
    delete quest;
    return nullptr;
}

int QuestManager::BuildQuestDB(std::list<QuestData*>& quests) {
    // Current L2Server.exe 0x00a138c0.
    for (QuestData* const data : quests) {
        UserQuest* const quest = CreateQuest(data);
        if (quest == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] QuestManager::BuildQuestDB() - CreateQuest returns NULL.",
            //     L"QuestManager.cpp",
            //     0x9f)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] QuestManager::BuildQuestDB() - CreateQuest returns NULL.",
            //     L"QuestManager.cpp",
            //     0x9f)
            L2_LOG_ERROR(
                Log,
                "Cannot construct quest %lld; quest database construction stops and previously registered quests remain.",
                static_cast<long long>(data->m_questID)
            );
            return 0;
        }
        m_questDB.emplace(static_cast<int>(quest->GetQuestID()), quest);
    }
    return 1;
}

int QuestManager::LoadUserQuest(const bool onlyPrecompiledHeader) {
    // Current L2Server.exe 0x00a1396c.
#if defined(_WIN32)
    constexpr const wchar_t* kInputPath = L"..\\Script\\UserQuestData.txt";
    constexpr const wchar_t* kPchPath = L"..\\Script\\userquestdata_pch.txt";
#else
    constexpr const wchar_t* kInputPath = L"../script/UserQuestData.txt";
    constexpr const char* kPchPath = "../script/userquestdata_pch.txt";
#endif

    CSecureStream input(kInputPath, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read UserQuestData.txt failed",
        //     L"QuestManager.cpp",
        //     0x37)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read UserQuestData.txt failed",
        //     L"QuestManager.cpp",
        //     0x37)
        L2_LOG_ERROR(
            Log, "Cannot read UserQuestData.txt for %s; no quests are registered by this load.",
            onlyPrecompiledHeader ? "PCH generation" : "quest loading"
        );
        return 0;
    }

    CParserForQuestData parser;
    parser.m_bOnlyPrecompiledHeader = onlyPrecompiledHeader;
    if (onlyPrecompiledHeader) {
#if defined(_WIN32)
        const int openError = ::_wfopen_s(&PCHFile, runtime::ResolveResourcePath(kPchPath).wstring().c_str(), L"wb");
#else
        PCHFile = std::fopen(runtime::ResolveResourcePath(kPchPath).string().c_str(), "wb");
        const int openError = PCHFile == nullptr ? errno : 0;
#endif
        if (openError != 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Can't open %s, errno[%d]",
            //     L"..\\Script\\userquestdata_pch.txt",
            //     openError)
            // L2_LOG_ERROR(Log,
            //     L"Can't open %s, errno[%d]",
            //     L"..\\Script\\userquestdata_pch.txt",
            //     openError)
            L2_LOG_ERROR(
                Log, "Cannot open quest PCH output '%s' for writing (errno %d); generation stops.",
                runtime::Utf8Bytes(runtime::ResolveResourcePath(kPchPath).generic_u8string()).c_str(), openError
            );
            return -1;
        }
        std::fputc(0xff, PCHFile);
        std::fputc(0xfe, PCHFile);
    }

    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Create CParserForQuestData failed",
        //     L"QuestManager.cpp",
        //     0x52)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Create CParserForQuestData failed",
        //     L"QuestManager.cpp",
        //     0x52)
        L2_LOG_ERROR(
            Log, "Cannot create the UserQuestData.txt parser for %s; no quests are registered by this load.",
            onlyPrecompiledHeader ? "PCH generation" : "quest loading"
        );
        return 0;
    }

    parser.m_lexer.yybegin(4);
    parser.m_lexer.yyin = nullptr;
    if (parser.yyparse() == 0) {
        if (onlyPrecompiledHeader) {
            std::fclose(PCHFile);
        }
        return BuildQuestDB(parser.m_quests);
    }

    std::array<char16_t, 16> errorText{};
    char16_t* source = parser.m_lexer.ErrorText();
    for (std::size_t index = 0; index + 1 < errorText.size() && *source != u'\0'; ++index, ++source) {
        errorText[index] = *source;
    }
    const std::string hostError = Utf16ToUtf8(errorText.data());
    // Log.Add(
    //     LOG_ERROR,
    //     L"QuestManager::LoadUserQuest() - Parsing failed. at line:[%d,%d], before %s",
    //     parser.m_lexer.ErrorLine(),
    //     parser.m_lexer.ErrorAt(),
    //     hostError.c_str())
    // L2_LOG_ERROR(Log,
    //     L"QuestManager::LoadUserQuest() - Parsing failed. at line:[%d,%d], before %s",
    //     parser.m_lexer.ErrorLine(),
    //     parser.m_lexer.ErrorAt(),
    //     hostError.c_str())
    L2_LOG_ERROR(
        Log, "Cannot parse UserQuestData.txt: line %d, UTF-16 offset in line %d, near '%s'; %s.", parser.m_lexer.ErrorLine(),
        parser.m_lexer.ErrorAt(), hostError.c_str(),
        onlyPrecompiledHeader ? "PCH output may be partial"
                              : "quest database construction is skipped; earlier NPC event attachments are not rolled back"
    );
    return 0;
}

bool QuestManager::ReloadUserQuest() {
    // Current L2Server.exe 0x00a13dd8.
    L2_GUARD_FUNCTION();
    m_questDB.clear();
    return LoadUserQuest(false) == 1;
}

UserQuest* QuestManager::GetQuest(const int questId) {
    // Current L2Server.exe 0x00a135c0.
    const auto found = m_questDB.find(questId);
    return found != m_questDB.end() ? found->second : nullptr;
}

int QuestManager::FindFirstTelList(const int npcClassId, const int selectedQuestZone) {
    // Current L2Server.exe 0x00a13618.
    L2_GUARD_FUNCTION();

    const auto npcPath = m_mapTelPath.find(npcClassId - 1000000);
    if (npcPath == m_mapTelPath.end()) {
        return -1;
    }

    const auto direct = npcPath->second.find(selectedQuestZone);
    if (direct != npcPath->second.end()) {
        return direct->second;
    }

    for (const auto& path : npcPath->second) {
        const auto relatedZones = m_mmmapNpcZone.equal_range(path.first);
        for (auto zone = relatedZones.first; zone != relatedZones.second; ++zone) {
            const auto relatedPath = m_mapTelPath.find(zone->second);
            if (relatedPath != m_mapTelPath.end() &&
                relatedPath->second.find(selectedQuestZone) != relatedPath->second.end()) {
                return path.second;
            }
        }
    }
    return -1;
}

void QuestManager::LoadTelQuestPath() {
    // Current L2Server.exe 0x00a13be8.
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\teleportquestpath.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/teleportquestpath.txt";
#endif

    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from %s file failed",
        //     L"QuestManager.cpp",
        //     0x73,
        //     kFileName)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from %s file failed",
        //     L"QuestManager.cpp",
        //     0x73,
        //     kFileName)
        L2_LOG_ERROR(Log, "Cannot read teleportquestpath.txt; existing teleport quest paths are retained.");
        return;
    }

    CParserForTeleportQuestPath parser;
    if (!parser.Create()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForTeleportQuestPath::create failed",
        //     L"QuestManager.cpp",
        //     0x7b)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForTeleportQuestPath::create failed",
        //     L"QuestManager.cpp",
        //     0x7b)
        L2_LOG_ERROR(Log, "Cannot create the teleportquestpath.txt parser; existing teleport quest paths are retained.");
        return;
    }

    parser.m_lexer.yybegin(4);
    parser.m_lexer.yyin = input.GetStream();
    // Portability improvement: consume only a leading UTF-16 BOM. The
    // original lexer reports it as an invalid character and then skips it.
    if (parser.m_lexer.yyin->peek() == 0xfeff) {
        parser.m_lexer.yyin->get();
    }
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing CParserForTeleportQuestPath file failed after line[%d]",
        //     L"QuestManager.cpp",
        //     0x87,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing CParserForTeleportQuestPath file failed after line[%d]",
        //     L"QuestManager.cpp",
        //     0x87,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log,
            "Cannot parse teleportquestpath.txt at line %lld; paths and zone mappings published before the error are "
            "retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
}
