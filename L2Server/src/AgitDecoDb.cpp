#include "AgitDecoDb.h"

#include "DecoDataParser.h"

#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"

CResidenceDecoDb::CResidenceDecoDb() = default;

CResidenceDecoDb::~CResidenceDecoDb() = default;

CResidenceDecoDb& CResidenceDecoDb::Inst() {
    static CResidenceDecoDb instance;
    return instance;
}

CResidenceDeco* CResidenceDecoDb::FindResidenceDeco(std::uint32_t decoId) {
    L2_GUARD_FUNCTION();
    const auto position = m_mapIdToResidenceDeco.find(decoId);
    return position == m_mapIdToResidenceDeco.end() ? nullptr : position->second;
}

CResidenceDeco* CResidenceDecoDb::FindResidenceDeco(int type, int level) {
    L2_GUARD_FUNCTION();
    const auto position = m_mapTypeLevelToResidenceDeco.find(std::CDualKey<int, int>(type, level));
    return position == m_mapTypeLevelToResidenceDeco.end() ? nullptr : position->second;
}

void CResidenceDecoDb::Load() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kDataPath = L"..\\Script\\decodata.txt";
#else
    constexpr const wchar_t* kDataPath = L"../script/decodata.txt";
#endif

    CSecureStream input(kDataPath, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from decodata file failed",
        //     L"AgitDecoDb.cpp",
        //     0x4f)
        // Log.Add(LOG_ERROR,
        //     L"[residence decorations] Cannot read decodata.txt. Loading stopped.")
        L2_LOG_ERROR(Log, u"[residence decorations] Cannot read decodata.txt. Loading stopped.");
        return;
    }

    CParserForDecoData parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForDecoData::create failed",
        //     L"AgitDecoDb.cpp",
        //     0x57)
        // Log.Add(LOG_ERROR,
        //     L"[residence decorations] Cannot initialize the parser for decodata.txt. Loading stopped.")
        L2_LOG_ERROR(Log, u"[residence decorations] Cannot initialize the parser for decodata.txt. Loading stopped.");
        return;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing decodata file failed line[%d]",
        //     L"AgitDecoDb.cpp",
        //     99,
        //     parser.m_lexer.line)
        // Log.Add(LOG_ERROR,
        //     L"[residence decorations] Parsing decodata.txt failed near line %d.",
        //     parser.m_lexer.line)
        L2_LOG_ERROR(Log, u"[residence decorations] Parsing decodata.txt failed near line %d.", parser.m_lexer.line);
    }
}

void CResidenceDecoDb::AddResidenceDeco(CResidenceDeco* residenceDeco) {
    L2_GUARD_FUNCTION();
    m_mapIdToResidenceDeco[static_cast<std::uint32_t>(residenceDeco->m_nDecoId)] = residenceDeco;
    m_mapNameToResidenceDeco[std::u16string(residenceDeco->m_sDecoName)] = residenceDeco;
    m_mapTypeLevelToResidenceDeco[std::CDualKey<int, int>(residenceDeco->m_nType, residenceDeco->m_nLevel)] = residenceDeco;
}
