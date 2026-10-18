#include "DecoNpcData.h"

#include "DecoNPCDataParser.h"

#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <cstring>

CDecoNpcDB::CDecoNpcDB() = default;

CDecoNpcDB::~CDecoNpcDB() = default;

CDecoNpcDB& CDecoNpcDB::Inst() {
    static CDecoNpcDB instance;
    return instance;
}

const CDecoNpcData* CDecoNpcDB::FindDecoNpc(unsigned int cid) const {
    L2_GUARD_FUNCTION();

    const auto position = m_idToData.find(cid);
    return position == m_idToData.end() ? nullptr : position->second;
}

void CDecoNpcDB::AddDecoNpc(CDecoNpcData* data) {
    L2_GUARD_FUNCTION();
    if (data == nullptr) {
        return;
    }

    CDecoNpcData*& slot = m_idToData[static_cast<unsigned int>(data->m_nCID)];
    if (slot != nullptr) {
        delete slot;
    }
    slot = data;
}

void CDecoNpcDB::PrivateAddDecoNpc::Call(CDecoNpcData* data) { CDecoNpcDB::Inst().AddDecoNpc(data); }

void CDecoNpcDB::Load() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kDataPath = L"..\\Script\\deconpcdata.txt";
#else
    // Linux file names are case sensitive. The data file is DecoNpcData.txt.
    constexpr const wchar_t* kDataPath = L"../script/DecoNpcData.txt";
#endif

    CSecureStream input(kDataPath, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC][%s][%d] Read from deconpcdata.txt file failed",
        //     L"DecoNpcData.cpp",
        //     0x60)
        // Log.Add(LOG_ERROR,
        //     L"[NPC decorations] Cannot read deconpcdata.txt. Loading stopped.")
        L2_LOG_ERROR(Log, u"[NPC decorations] Cannot read deconpcdata.txt. Loading stopped.");
        return;
    }

    CParserForDecoNpcData parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC][%s][%d] CParserForDecoNpcData::create failed",
        //     L"DecoNpcData.cpp",
        //     0x69)
        // Log.Add(LOG_ERROR,
        //     L"[NPC decorations] Cannot initialize the parser for deconpcdata.txt. Loading stopped.")
        L2_LOG_ERROR(Log, u"[NPC decorations] Cannot initialize the parser for deconpcdata.txt. Loading stopped.");
        return;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC][%s][%d] Parsing deconpcdata.txt file failed line[%d]",
        //     L"DecoNpcData.cpp",
        //     0x74,
        //     parser.m_lexer.line)
        // Log.Add(LOG_ERROR,
        //     L"[NPC decorations] Parsing deconpcdata.txt failed near line %d.",
        //     parser.m_lexer.line)
        L2_LOG_ERROR(Log, u"[NPC decorations] Parsing deconpcdata.txt failed near line %d.", parser.m_lexer.line);
    }
}

CDecoNpcData::CDecoNpcData() :
    m_nCID(0),
    m_nType(0),
    m_nSubType(0),
    m_nFactionType(0),
    m_nLevel(0),
    m_nNpcCID(0),
    m_nPeriod(0),
    m_nCostAdena(0),
    m_isChangeable(false) {
    L2_GUARD_FUNCTION();

    std::memset(m_szName, 0, sizeof(m_szName));
}

CDecoNpcData::~CDecoNpcData() { L2_GUARD_FUNCTION(); }

int CDecoNpcData::GetFirstCostItemCID() const {
    if (m_vCostItems.empty()) {
        return 0;
    }

    return runtime::CheckedVectorAt(m_vCostItems, 0).nClassID;
}

int CDecoNpcData::GetFirstCostItemCount() const {
    if (m_vCostItems.empty()) {
        return 0;
    }

    return runtime::CheckedVectorAt(m_vCostItems, 0).nCount;
}

bool CDecoNpcData::HasResidenceGrade(int nResidenceGrade) const {
    L2_GUARD_FUNCTION();

    return std::find(m_residenceGrades.begin(), m_residenceGrades.end(), nResidenceGrade) != m_residenceGrades.end();
}

bool CDecoNpcData::HasResidenceDomain(int nResidenceDomain) const {
    // The current image compares the result of the domain search with the
    // end iterator of m_residenceGrades. Preserve that compiled behavior.
    return std::find(m_residenceDomains.begin(), m_residenceDomains.end(), nResidenceDomain) != m_residenceGrades.end();
}

namespace DecoNpcUtil {

bool IsRightSlot(const CDecoNpcData* pData, int nSlot) {
    L2_GUARD_FUNCTION();

    if (pData == nullptr || nSlot < 0) {
        return false;
    }

    if (nSlot == 0) {
        return pData->m_nType == 1000;
    }

    return pData->m_nType != 1000;
}

} // namespace DecoNpcUtil
