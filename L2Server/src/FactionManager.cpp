#include "FactionManager.h"

#include "LexerFaction.h"
#include "ParserFaction.h"

#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"

#include <cstdint>

bool CFactionManager::LoadLexYacc() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kFactionPath = L"..\\Script\\factiondata.txt";
#else
    constexpr const wchar_t* kFactionPath = L"../script/factiondata.txt";
#endif

    CSecureStream input(kFactionPath, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from file failed",
        //     L"FactionManager.cpp",
        //     0x10)
        // Log.Add(LOG_ERROR,
        //     L"[factions] Cannot read factiondata.txt. Loading stopped.")
        L2_LOG_ERROR(Log, u"[factions] Cannot read factiondata.txt. Loading stopped.");
        return false;
    }

    CLexerForFaction lexer;
    CParserForFaction parser;
    lexer.m_inputBufferOffset = 0;
    lexer.m_inputBuffer = input.GetRawString() + 1;
    lexer.m_inputBufferLength = static_cast<std::uint64_t>(input.GetRawStringLen() - 1);

    // The original lexer.yycreate(&parser) set up the old lexer runtime here.
    // The re2c scanner needs no setup, so that step and its error report are gone.
    if (!parser.Create(&lexer)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForFaction create failed",
        //     L"FactionManager.cpp",
        //     0x28)
        // Log.Add(LOG_ERROR,
        //     L"[factions] Cannot initialize the parser for factiondata.txt. Loading stopped.")
        L2_LOG_ERROR(Log, u"[factions] Cannot initialize the parser for factiondata.txt. Loading stopped.");
        return false;
    }

    lexer.yybegin(4);
    const int result = parser.yyparse();
    if (result != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing CParserForFaction file failed after line[%d]",
        //     L"FactionManager.cpp",
        //     0x31,
        //     lexer.line)
        // Log.Add(LOG_ERROR,
        //     L"[factions] Parsing factiondata.txt failed near line %d (parser result %d).",
        //     lexer.line, result)
        L2_LOG_ERROR(Log, u"[factions] Parsing factiondata.txt failed near line %d (parser result %d).", lexer.line, result);
    }
    return result == 0;
}
