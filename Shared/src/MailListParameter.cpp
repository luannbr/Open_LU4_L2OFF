#include "Shared/src/MailListParameter.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/IOCPacket.h"

#include <algorithm>
#include <cstring>
#include <string>

CMailListParameter::CMailListParameter() : m_mailBoxType(0), m_page(1), m_rowsPerPage(10), m_searchTarget(0), m_keyword{} {
    // Current L2Server 0x00812ea8. Cached 0x004f0ea8.
}

void CMailListParameter::SetKeyword(const char16_t* const keyword) {
    // Current L2Server 0x00812f20. The target secure copy keeps at most
    // twenty UTF-16 units and then re-evaluates the length on every pass.
    if (keyword == nullptr) {
        m_keyword[0] = 0;
        return;
    }
    const std::size_t length = std::char_traits<char16_t>::length(keyword);
    const std::size_t copied = std::min<std::size_t>(length, 20);
    std::copy_n(keyword, copied, m_keyword.data());
    m_keyword[copied] = 0;
    for (std::size_t index = 0; index < std::char_traits<char16_t>::length(m_keyword.data()); ++index) {
        if (m_keyword[index] == u'_') {
            m_keyword[index] = u' ';
        }
    }
}

int CMailListParameter::GetRowsPerPage() noexcept {
    // Current L2Server 0x00812f10.
    return m_rowsPerPage < 1 ? 10 : m_rowsPerPage;
}

wchar_t* CMailListParameter::GetMailBoxName() noexcept {
    // Current L2Server 0x00812ee0.
    switch (m_mailBoxType) {
        case 1:
            return const_cast<wchar_t*>(L"&$918;");
        case 2:
            return const_cast<wchar_t*>(L"&$919;");
        case 3:
            return const_cast<wchar_t*>(L"&$920;");
        default:
            return const_cast<wchar_t*>(L"&$917;");
    }
}

int CMailListParameter::AssemblePacket(char* const buffer, const int bufferLength) {
    // Current L2Server 0x00812f8c. Cached 0x004f0ef0.
    L2_GUARD_FUNCTION();
    const int rowsPerPage = m_rowsPerPage < 1 ? 10 : m_rowsPerPage;
    const int page = m_page < 1 ? 1 : m_page;
    return ::Assemble(buffer, bufferLength, "ddddS", m_mailBoxType, page, rowsPerPage, m_searchTarget, m_keyword.data());
}

const unsigned char* CMailListParameter::DisassemblePacket(const unsigned char* const packet) {
    // Current L2Server 0x00813084. Cached 0x004f0fe8.
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "ddddS", &m_mailBoxType, &m_page, &m_rowsPerPage, &m_searchTarget, 0x2a, m_keyword.data());
}
