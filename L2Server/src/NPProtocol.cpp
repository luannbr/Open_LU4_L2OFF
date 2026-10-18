#include "NPProtocol.h"
#include "Shared/src/MemorySub.h"

#include "NPConst.h"
#include "NPSystem.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/RawAtomic.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace {


bool AppendFormat(char*& destination, std::size_t destinationSize, const char* format, ...) {
    if (!destination || destinationSize == 0 || !format) {
        return false;
    }

    va_list arguments;
    va_start(arguments, format);
    const int written = std::vsnprintf(destination, destinationSize, format, arguments);
    va_end(arguments);

    if (written < 0 || static_cast<std::size_t>(written) >= destinationSize) {
        destination += destinationSize - 1;
        return false;
    }
    destination += written;
    return true;
}

char* FindBounded(char* buffer, const char* pattern, std::size_t bufferSize) {
    if (!buffer || !pattern || bufferSize == 0) {
        return nullptr;
    }

    const std::size_t patternSize = std::strlen(pattern);
    if (patternSize > bufferSize) {
        return nullptr;
    }

    char* const last = buffer + (bufferSize - patternSize);
    for (char* current = buffer; current <= last; ++current) {
        if (*current == *pattern && std::memcmp(current, pattern, patternSize) == 0) {
            return current;
        }
    }
    return nullptr;
}

// Noise: Command lookup is not an OS operation. Its unused last-error formatter input is kept below.
// int CurrentLastError() noexcept {
// #if defined(_WIN32)
//     return static_cast<int>(::GetLastError())
// #else
//     return errno
// #endif
// }

} // namespace

namespace NPSystem {

void CSTSHeaderParser::convert_lower::operator()(char& value) const {
    value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

CSTSHeaderParser::CSTSHeaderParser() { Clear(); }

CSTSHeaderParser::~CSTSHeaderParser() { Clear(); }

void CSTSHeaderParser::Clear() {
    m_pStart = nullptr;
    m_pEnd = nullptr;
    m_pWork = nullptr;
    m_pStringStart = nullptr;
    m_pStringEnd = nullptr;
    m_nParseState = PS_NONE;
}

bool CSTSHeaderParser::Init(char* buffer, std::uint64_t bufferSize) {
    Clear();
    if (!buffer || *buffer == '\0' || bufferSize == 0) {
        return false;
    }

    m_pStart = buffer;
    m_pEnd = buffer + bufferSize;
    m_pWork = buffer;
    return true;
}

bool CSTSHeaderParser::IsValid() { return m_pStart && m_pEnd && m_pWork && m_pStart <= m_pWork && m_pWork <= m_pEnd; }

void CSTSHeaderParser::MarkStringStart() { m_pStringStart = m_pWork; }

void CSTSHeaderParser::MarkStringEnd() { m_pStringEnd = m_pWork; }

bool CSTSHeaderParser::IsWS(char value) { return value == ' '; }

bool CSTSHeaderParser::IsCR(char value) { return value == '\r'; }

bool CSTSHeaderParser::IsLF(char value) { return value == '\n'; }

bool CSTSHeaderParser::IsNVSep(char value) { return value == HEADER_NVSEP; }

bool CSTSHeaderParser::IsEnd(char value) { return value == '\0'; }

bool CSTSHeaderParser::GetToken(Token& token, bool afterNameValueSeparator) {
    if (!IsValid()) {
        token.nType = TOKEN_ERROR;
        return false;
    }
    if (m_pWork == m_pEnd) {
        token.nType = TOKEN_END;
        return true;
    }

    while (m_pWork < m_pEnd) {
        const char value = *m_pWork;
        switch (m_nParseState) {
            case PS_NONE:
                if (IsEnd(value)) {
                    Consume();
                    token.nType = TOKEN_END;
                    return true;
                }
                if (IsWS(value)) {
                    Consume();
                    token.nType = TOKEN_WS;
                    return true;
                }
                if (!afterNameValueSeparator && IsNVSep(value)) {
                    Consume();
                    token.nType = TOKEN_NVSEP;
                    return true;
                }
                if (IsCR(value)) {
                    m_nParseState = PS_IN_CR;
                } else if (IsLF(value)) {
                    Consume();
                    token.nType = TOKEN_LF;
                    return true;
                } else {
                    MarkStringStart();
                    m_nParseState = PS_IN_STR;
                }
                break;

            case PS_IN_CR:
                m_nParseState = PS_NONE;
                if (IsLF(value)) {
                    Consume();
                    token.nType = TOKEN_LINESEP;
                } else {
                    token.nType = TOKEN_CR;
                }
                return true;

            case PS_IN_STR:
                if (IsEnd(value) || IsWS(value) || (!afterNameValueSeparator && IsNVSep(value)) || IsCR(value) ||
                    IsLF(value)) {
                    m_nParseState = PS_NONE;
                    MarkStringEnd();
                    token.nType = TOKEN_STRING;
                    token.pStringStart = m_pStringStart;
                    token.pStringEnd = m_pStringEnd;
                    return true;
                }
                break;

            default:
                break;
        }
        Consume();
    }

    token.nType = TOKEN_ERROR;
    return false;
}

void CSTSHeaderParser::Consume() {
    ++m_pWork;
    if (m_pEnd < m_pWork) {
        m_pWork = m_pEnd;
    }
}

int CSTSHeaderParser::GetLineTokenCount() { return m_nTokenCount; }

CSTSHeaderParser::Token* CSTSHeaderParser::GetLineTokens() { return m_TokenArray; }

bool CSTSHeaderParser::ParseLineRule(EToken* tokenRule, int tokenCount) {
    if (!tokenRule || tokenCount <= 0 || m_nTokenCount != tokenCount) {
        return false;
    }

    for (int index = 0; index < m_nTokenCount; ++index) {
        if (m_TokenArray[index].nType != tokenRule[index]) {
            return false;
        }
    }
    return true;
}

bool CSTSHeaderParser::ParseLine() {
    m_nTokenCount = 0;
    std::memset(m_TokenArray, 0, sizeof(m_TokenArray));
    if (!IsValid()) {
        return false;
    }

    int nameValueSeparatorCount = 0;
    while (m_nTokenCount < MAX_TOKEN_COUNT) {
        Token token;
        if (!GetToken(token, nameValueSeparatorCount > 0)) {
            return false;
        }
        if (token.nType == TOKEN_LINESEP) {
            return true;
        }
        if (token.nType == TOKEN_WS) {
            continue;
        }

        m_TokenArray[m_nTokenCount] = token;
        ++m_nTokenCount;
        if (token.nType != TOKEN_STRING) {
            if (token.nType != TOKEN_NVSEP) {
                return false;
            }
            ++nameValueSeparatorCount;
        }
    }
    return false;
}

bool CSTSRequest::Init(int method, int command) {
    Clear();
    m_pIOBuffer = CIOBuffer::Alloc();
    if (!m_pIOBuffer || static_cast<unsigned int>(method) >= METHOD_MAX ||
        static_cast<unsigned int>(command) >= COMMAND_MAX) {
        return false;
    }

    m_pBufferContentLengthPos = nullptr;
    m_pBufferStart = reinterpret_cast<char*>(m_pIOBuffer->m_Buffer.data());
    m_pBufferEnd = reinterpret_cast<char*>(&m_pIOBuffer->m_dwSize);
    m_pBufferCur = m_pBufferStart;
    m_nMethod = static_cast<EMethod>(method);
    m_nCommand = static_cast<ECommand>(command);
    return _SetMethods();
}

void CSTSRequest::Clear() {
    m_nMethod = static_cast<EMethod>(-1);
    m_nCommand = static_cast<ECommand>(-1);
    if (m_pIOBuffer) {
        m_pIOBuffer->Release();
        m_pIOBuffer = nullptr;
    }
    m_pBufferStart = nullptr;
    m_pBufferEnd = nullptr;
    m_pBufferCur = nullptr;
    m_pBufferContentLengthPos = nullptr;
    m_pBufferBodyStart = nullptr;
    m_pBufferBodyEnd = nullptr;
    m_isTransaction = false;
    m_nTransactionID = 0;
    m_nAccountID = 0;
}

bool CSTSRequest::IsValid() const {
    return static_cast<unsigned int>(m_nMethod) < METHOD_MAX && static_cast<unsigned int>(m_nCommand) < COMMAND_MAX &&
        m_pIOBuffer && m_pBufferStart && m_pBufferEnd && m_pBufferCur && m_pBufferStart <= m_pBufferCur &&
        m_pBufferCur < m_pBufferEnd;
}

bool CSTSRequest::IsValidBody() const {
    return m_pBufferBodyStart && m_pBufferBodyEnd && m_pIOBuffer && m_pBufferStart <= m_pBufferBodyStart &&
        m_pBufferBodyStart < m_pBufferEnd && m_pBufferStart <= m_pBufferBodyEnd && m_pBufferBodyEnd < m_pBufferEnd;
}

bool CSTSRequest::_SetMethods() {
    if (!IsValid()) {
        return false;
    }

    AppendFormat(
        m_pBufferCur, static_cast<std::size_t>(m_pBufferEnd - m_pBufferStart), "%s %s %s%s", g_szMethods[m_nMethod],
        g_szCommandNames[m_nCommand][1], PROTOCOL_VERSION, CRLF
    );
    if (!IsValid()) {
        return false;
    }

    m_pBufferContentLengthPos = m_pBufferCur + 2;
    AppendFormat(
        m_pBufferCur, static_cast<std::size_t>(m_pBufferEnd - m_pBufferStart), "%s:%05d%s",
        g_szHeaderNamesShort[HEADERNAME_CONTENTLENGTH], 0, CRLF
    );
    return IsValid();
}

bool CSTSRequest::_SetBodyLength(std::uint64_t bodyLength) {
    if (!IsValid() || !m_pBufferContentLengthPos) {
        return false;
    }

    char encodedLength[8]{};
    std::snprintf(encodedLength, sizeof(encodedLength), "%05d", static_cast<int>(bodyLength));
    std::memcpy(m_pBufferContentLengthPos, encodedLength, 5);
    return true;
}

bool CSTSRequest::SetHeaderAsString(EHeaderName headerName, const char* value) {
    if (!IsValid() || static_cast<unsigned int>(headerName) >= HEADERNAME_MAX || !value || *value == '\0') {
        return false;
    }

    AppendFormat(
        m_pBufferCur, static_cast<std::size_t>(m_pBufferEnd - m_pBufferStart), "%s%c%s%s", g_szHeaderNamesShort[headerName],
        ':', value, CRLF
    );
    return IsValid();
}

bool CSTSRequest::SetHeaderAsInt(EHeaderName headerName, int value) {
    char encodedValue[32]{};
    std::snprintf(encodedValue, sizeof(encodedValue), "%d", value);
    return SetHeaderAsString(headerName, encodedValue);
}

bool CSTSRequest::SetHeaderAsTransaction(unsigned int accountId) {
    m_isTransaction = true;
    m_nAccountID = accountId;
    m_nTransactionID = static_cast<std::uint32_t>(
        runtime::raw_atomic::IncrementI32(reinterpret_cast<volatile std::int32_t*>(&s_nLastRequestTransactionNO))
    );
    return SetHeaderAsInt(HEADERNAME_SUBJECT, static_cast<int>(m_nTransactionID));
}

bool CSTSRequest::StartBody() {
    if (!IsValid()) {
        return false;
    }

    AppendFormat(m_pBufferCur, static_cast<std::size_t>(m_pBufferEnd - m_pBufferStart), "%s", CRLF);
    m_pBufferBodyStart = m_pBufferCur;
    m_pBufferBodyEnd = m_pBufferCur;
    return IsValid();
}

bool CSTSRequest::EndBody() {
    if (!IsValid() || !m_tagStack.empty() || !m_pBufferBodyStart || !m_pBufferBodyEnd) {
        return false;
    }

    if (m_pBufferBodyStart < m_pBufferCur) {
        AppendFormat(m_pBufferCur, static_cast<std::size_t>(m_pBufferEnd - m_pBufferStart), "%s", CRLF);
    }
    m_pBufferBodyEnd = m_pBufferCur;
    if (!IsValid() || !IsValidBody() || !_SetBodyLength(static_cast<std::uint64_t>(m_pBufferBodyEnd - m_pBufferBodyStart)) ||
        !_SaveTransactionInfo()) {
        return false;
    }

    m_pBufferEnd = m_pBufferBodyEnd;
    m_pIOBuffer->m_dwSize = static_cast<std::uint32_t>(m_pBufferBodyEnd - m_pBufferStart);
    return true;
}

bool CSTSRequest::_SaveTransactionInfo() {
    if (m_isTransaction) {
        L2_GUARD(g_requestTransactionMapSect);
        g_requestTransactionMap.emplace(static_cast<std::int32_t>(m_nTransactionID), STSRequestTI{m_nAccountID, m_nCommand});
    }
    return true;
}

bool CSTSRequest::StartElement(const char* elementName) {
    if (!IsValid() || !IsValidBody() || !elementName || *elementName == '\0') {
        return false;
    }

    AppendFormat(m_pBufferCur, static_cast<std::size_t>(m_pBufferEnd - m_pBufferCur), "<%s>%s", elementName, CRLF);
    m_tagStack.push(elementName);
    return true;
}

bool CSTSRequest::EndElement() {
    if (!IsValid() || !IsValidBody() || m_tagStack.empty()) {
        return false;
    }

    const std::string& elementName = m_tagStack.top();
    if (elementName.empty()) {
        return false;
    }
    AppendFormat(m_pBufferCur, static_cast<std::size_t>(m_pBufferEnd - m_pBufferCur), "</%s>\n", elementName.c_str());
    m_tagStack.pop();
    return true;
}

bool CSTSRequest::AddElementStringValue(const char* elementName, const char* value) {
    if (!elementName || *elementName == '\0' || !value || *value == '\0' || !IsValid() || !IsValidBody()) {
        return false;
    }

    AppendFormat(
        m_pBufferCur, static_cast<std::size_t>(m_pBufferEnd - m_pBufferCur), "<%s>%s</%s>\n", elementName, value, elementName
    );
    return IsValid();
}

bool CSTSRequest::AddElementIntValue(const char* elementName, int value) {
    char encodedValue[256]{};
    std::snprintf(encodedValue, sizeof(encodedValue), "%d", value);
    return AddElementStringValue(elementName, encodedValue);
}

CSTSRequest::CIOBuffer* CSTSRequest::GeneratePakcet() {
    CIOBuffer* buffer = m_pIOBuffer;
    m_pIOBuffer = nullptr;
    return buffer;
}

L2_DEFINE_OBJECT_CLASS_ID(CSTSResponse)

L2_DEFINE_OBJECT_CLASS_NAME(CSTSResponse, m_pClassName_CSTSResponse)

L2_DEFINE_OBJECT_NEW(CSTSResponse)

L2_DEFINE_OBJECT_ALLOCATOR(CSTSResponse, m_pClassName_CSTSResponse)

L2_DEFINE_OBJECT_ADD_REF(CSTSResponse)

L2_DEFINE_OBJECT_RELEASE(CSTSResponse)

CSTSResponse::CSTSResponse() : m_pNextResponse(nullptr), m_pReadBuffer(nullptr) { Clear(); }

CSTSResponse::~CSTSResponse() { Clear(); }

void CSTSResponse::Clear() {
    m_nResponseCode = 400;
    m_isSingleTransaction = true;
    m_isTransactionComplete = false;
    m_nMethod = METHOD_POST;
    m_nProtocol = PROTOCOL_GATE;
    m_isResponse = false;
    m_pResponseString = nullptr;
    m_isTransaction = false;
    m_nTRID = 0;
    m_nCommand = COMMAND_MIN;
    m_pBufferStart = nullptr;
    m_pBufferEnd = nullptr;
    m_pBufferHeaderStart = nullptr;
    m_pBufferHeaderEnd = nullptr;
    m_nBodyLength = 0;
    m_pBufferBodyStart = nullptr;
    m_pBufferBodyEnd = nullptr;
    m_headerMap.clear();
    m_nParseStep = RPS_NONE;

    if (m_pNextResponse) {
        L2_RELEASE(m_pNextResponse, ObjectRefType::CreateDelete);
        m_pNextResponse = nullptr;
    }
    if (m_pReadBuffer) {
        m_pReadBuffer->Release();
        m_pReadBuffer = nullptr;
    }

    m_bIsKnownCommand = true;
    m_isNeedResetBuffer = true;
    m_nBytesToDrop = 0;
}

bool CSTSResponse::SetBuffer(char* buffer, std::size_t bufferSize) {
    if (!buffer || bufferSize == 0) {
        return false;
    }

    m_pBufferStart = buffer;
    m_pBufferEnd = buffer + bufferSize;
    return true;
}

bool CSTSResponse::PrepareParseHeader() {
    if (!IsReadBufferReady()) {
        return false;
    }

    if (m_nParseStep >= RPS_HEADER_COMPLETE) {
        return true;
    }
    if (m_nParseStep != RPS_NONE) {
        return false;
    }

    char* const headerEnd = FindBounded(m_pBufferStart, CRLFCRLF, static_cast<std::size_t>(m_pBufferEnd - m_pBufferStart));
    if (!headerEnd) {
        return false;
    }

    m_pBufferHeaderStart = m_pBufferStart;
    m_pBufferHeaderEnd = headerEnd;
    m_pBufferBodyStart = headerEnd + std::strlen(CRLFCRLF);
    return true;
}

bool CSTSResponse::ResetBuffer(char* buffer) {
    if (m_nParseStep != RPS_HEADER_COMPLETE || !buffer) {
        return false;
    }

    if (m_pBufferStart != buffer) {
        m_pBufferBodyStart = buffer;
    }
    return true;
}

bool CSTSResponse::PrepareParseBody() {
    if (!IsReadBufferReady()) {
        return false;
    }

    if (m_nParseStep > RPS_HEADER_COMPLETE) {
        return true;
    }
    if (m_nParseStep != RPS_HEADER_COMPLETE) {
        return false;
    }

    if (static_cast<std::ptrdiff_t>(m_nBodyLength) <= m_pBufferEnd - m_pBufferBodyStart) {
        m_pBufferBodyEnd = m_pBufferBodyStart + m_nBodyLength;
        return true;
    }
    return false;
}

bool CSTSResponse::IsParsingComplete() {
    if (m_nParseStep > RPS_HEADER_COMPLETE) {
        return true;
    }
    if (m_nParseStep != RPS_HEADER_COMPLETE) {
        return false;
    }

    if (m_nBodyLength == 0) {
        m_nParseStep = RPS_BODY_COMPLETE;
        m_pBufferBodyEnd = m_pBufferBodyStart;
        return true;
    }
    if (m_nBodyLength <= 0x10000 && m_pBufferBodyStart && m_pBufferBodyStart + m_nBodyLength <= m_pBufferEnd &&
        static_cast<int>(std::strlen(CRLF)) <= m_nBodyLength) {
        m_nParseStep = RPS_BODY_COMPLETE;
        m_pBufferBodyEnd = m_pBufferBodyStart + m_nBodyLength;
        return true;
    }
    return false;
}

bool CSTSResponse::_ParseHeaderMethod(const char* method, EMethod& parsedMethod) {
    if (!method || *method == '\0') {
        return false;
    }

    const auto position = g_MethodToIdMap.find(method);
    if (position == g_MethodToIdMap.end()) {
        return false;
    }

    parsedMethod = static_cast<EMethod>(position->second);
    return static_cast<unsigned int>(parsedMethod) < METHOD_MAX;
}

bool CSTSResponse::_ParseHeaderCommand(const char* command, EProtocol& parsedProtocol, ECommand& parsedCommand) {
    if (!command || *command == '\0') {
        return false;
    }

    const auto position = g_szFullCommandNameToIdMap.find(command);
    if (position == g_szFullCommandNameToIdMap.end()) {
        // Noise: NPSocket reports the unrecognized-command outcome. The raw peer-supplied token is not needed in a second
        // record. Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(e=%d) Unknown Command 1. Command is (%S)",
        //     L"NPProtocol.cpp",
        //     0x442,
        //     CurrentLastError(),
        //     command)
        m_bIsKnownCommand = false;
        return true;
    }

    parsedCommand = static_cast<ECommand>(position->second);
    if (static_cast<unsigned int>(parsedCommand) >= COMMAND_MAX) {
        return false;
    }

    parsedProtocol = static_cast<EProtocol>(g_nCommandToProtocol[parsedCommand][1]);
    return static_cast<unsigned int>(parsedProtocol) < PROTOCOL_MAX;
}

bool CSTSResponse::_ParseHeadeName(const char* nameStart, const char* nameEnd, EHeaderName& parsedHeaderName) {
    if (!nameStart || *nameStart == '\0' || !nameEnd || nameStart >= nameEnd) {
        return false;
    }

    CStringToIdMap& names = nameEnd - nameStart == 1 ? g_HeaderShortNameToIdMap : g_HeaderFullNameToIdMap;
    const auto position = names.find(nameStart);
    if (position == names.end()) {
        return false;
    }

    parsedHeaderName = static_cast<EHeaderName>(position->second);
    return static_cast<unsigned int>(parsedHeaderName) < HEADERNAME_MAX;
}

void CSTSResponse::DecideBytesToDrop() {
    if (m_nBytesToDrop > 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError. Packet Filtering duplicated",
        //     L"NPProtocol.cpp",
        //     0x539)
        // Log.Add(LOG_WARN,
        //     "[NP] Response filtering was requested again while %d body bytes remain to be discarded (transaction %u).",
        //     m_nBytesToDrop, static_cast<unsigned>(m_nTRID))
        L2_LOG_WARN(
            Log, "[NP] Response filtering was requested again while %d body bytes remain to be discarded (transaction %u).",
            m_nBytesToDrop, static_cast<unsigned>(m_nTRID)
        );
        return;
    }

    if (m_isResponse && m_isTransaction) {
        L2_GUARD(g_responseTransactionMapSect);
        const std::int32_t transactionId = static_cast<std::int32_t>(m_nTRID);
        const auto position = g_responseTransactionMap.find(transactionId);
        if (position == g_responseTransactionMap.end()) {
            if (!m_isTransactionComplete) {
                m_nBytesToDrop = m_nBodyLength;
                g_responseTransactionMap.emplace(transactionId, this);
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d]NPError. Drop Multiple Reply Responses "
                //     L"Messages. TransactionID[%d]",
                //     L"NPProtocol.cpp",
                //     0x554,
                //     m_nTRID)
                // Log.Add(LOG_WARN,
                //     "[NP] Multipart replies are unsupported for transaction %u. Discarding this %d-byte body and its
                //     subsequent replies.", static_cast<unsigned>(m_nTRID), m_nBodyLength)
                L2_LOG_WARN(
                    Log,
                    "[NP] Multipart replies are unsupported for transaction %u; discarding this %d-byte body and its "
                    "subsequent replies.",
                    static_cast<unsigned>(m_nTRID), m_nBodyLength
                );
            }
        } else {
            m_nBytesToDrop = m_nBodyLength;
            if (m_isTransactionComplete) {
                g_responseTransactionMap.erase(position);
            }
            // Noise: The first multipart reply already reports this transaction as unsupported. Subsequent filtered replies
            // repeat the same outcome. Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d]NPError. Drop Multiple Reply Responses "
            //     L"Messages. TransactionID[%d]",
            //     L"NPProtocol.cpp",
            //     0x54b,
            //     m_nTRID)
        }
    }

    if (m_nBodyLength > 0x8000) {
        m_nBytesToDrop = m_nBodyLength;
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError. Drop large packet. packet body size(%d) "
        //     L"is greater than NPSocket BufferSize(%d)",
        //     L"NPProtocol.cpp",
        //     0x560,
        //     m_nBodyLength,
        //     0x8000)
        // Log.Add(LOG_WARN,
        //     "[NP] Discarding a %d-byte response body: the receive limit is 32768 bytes (transaction %u).",
        //     m_nBodyLength, static_cast<unsigned>(m_nTRID))
        L2_LOG_WARN(
            Log, "[NP] Discarding a %d-byte response body: the receive limit is 32768 bytes (transaction %u).",
            m_nBodyLength, static_cast<unsigned>(m_nTRID)
        );
    }
}

bool CSTSResponse::ParseHeader() {
    if (!IsReadBufferReady()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError. IsReadBufferReady() == false",
        //     L"NPProtocol.cpp",
        //     0x307)
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot parse the response header: the read buffer is empty or unavailable.")
        L2_LOG_ERROR(Log, "[NP] Cannot parse the response header: the read buffer is empty or unavailable.");
        return false;
    }
    if (m_nParseStep > RPS_NONE) {
        return true;
    }
    if (m_nParseStep != RPS_NONE) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError. m_nParseStep != RPS_NONE",
        //     L"NPProtocol.cpp",
        //     0x314)
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot parse the response header from parser state %d.",
        //     static_cast<int>(m_nParseStep))
        L2_LOG_ERROR(Log, "[NP] Cannot parse the response header from parser state %d.", static_cast<int>(m_nParseStep));
        return false;
    }

    CSTSHeaderParser parser;
    if (!parser.Init(m_pBufferStart, static_cast<std::uint64_t>(m_pBufferBodyStart - m_pBufferStart))) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError. parser.Init() == false",
        //     L"NPProtocol.cpp",
        //     0x31d)
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot initialize the response header parser: the header buffer is empty or unavailable.")
        L2_LOG_ERROR(Log, "[NP] Cannot initialize the response header parser: the header buffer is empty or unavailable.");
        return false;
    }
    if (!parser.ParseLine()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError. parser.ParseLine() == false",
        //     L"NPProtocol.cpp",
        //     0x326)
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot tokenize the first line of the response header.")
        L2_LOG_ERROR(Log, "[NP] Cannot tokenize the first line of the response header.");
        return false;
    }

    CSTSHeaderParser::EToken firstLineRule[] = {
        CSTSHeaderParser::TOKEN_STRING,
        CSTSHeaderParser::TOKEN_STRING,
        CSTSHeaderParser::TOKEN_STRING,
    };
    if (!parser.ParseLineRule(firstLineRule, 3)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError. parser.ParseLineRule() == false",
        //     L"NPProtocol.cpp",
        //     0x32f)
        // Log.Add(LOG_ERROR,
        //     "[NP] Invalid first header line: expected three space-separated fields.")
        L2_LOG_ERROR(Log, "[NP] Invalid first header line: expected three space-separated fields.");
        return false;
    }

    CSTSHeaderParser::Token* firstLine = parser.GetLineTokens();
    *firstLine[0].pStringEnd = '\0';
    *firstLine[1].pStringEnd = '\0';
    *firstLine[2].pStringEnd = '\0';
    m_isResponse = std::strcmp(firstLine[0].pStringStart, PROTOCOL_VERSION) == 0;
    if (m_isResponse) {
        m_nResponseCode = std::atoi(firstLine[1].pStringStart);
        m_pResponseString = firstLine[2].pStringStart;
    } else {
        if (!_ParseHeaderMethod(firstLine[0].pStringStart, m_nMethod)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d]NPError. _ParseHeaderMethod() == false",
            //     L"NPProtocol.cpp",
            //     0x34b)
            // Log.Add(LOG_ERROR,
            //     "[NP] Invalid request method in the first header line.")
            L2_LOG_ERROR(Log, "[NP] Invalid request method in the first header line.");
            return false;
        }
        if (!_ParseHeaderCommand(firstLine[1].pStringStart, m_nProtocol, m_nCommand)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d]NPError. _ParseHeaderCommand(%S) == false",
            //     L"NPProtocol.cpp",
            //     0x352,
            //     firstLine[1].pStringStart)
            // Log.Add(LOG_ERROR,
            //     "[NP] Cannot resolve the request command in the first header line.")
            L2_LOG_ERROR(Log, "[NP] Cannot resolve the request command in the first header line.");
            return false;
        }
        m_nResponseCode = 200;
    }

    int parsedLineCount = 0;
    do {
        if (!parser.ParseLine()) {
            // Log.Add(LOG_ERROR,
            //     "[NP] Cannot tokenize header line %d.",
            //     parsedLineCount + 2)
            L2_LOG_ERROR(Log, "[NP] Cannot tokenize header line %d.", parsedLineCount + 2);
            return false;
        }
        if (parsedLineCount != 0 && parser.GetLineTokenCount() == 0) {
            break;
        }

        CSTSHeaderParser::EToken headerLineRule[] = {
            CSTSHeaderParser::TOKEN_STRING,
            CSTSHeaderParser::TOKEN_NVSEP,
            CSTSHeaderParser::TOKEN_STRING,
        };
        if (!parser.ParseLineRule(headerLineRule, 3)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d]NPError. parser.ParseLineRule() == false",
            //     L"NPProtocol.cpp",
            //     0x36d)
            // Log.Add(LOG_ERROR,
            //     "[NP] Invalid header line %d: expected a name, colon and value.",
            //     parsedLineCount + 2)
            L2_LOG_ERROR(Log, "[NP] Invalid header line %d: expected a name, colon and value.", parsedLineCount + 2);
            return false;
        }

        CSTSHeaderParser::Token* headerLine = parser.GetLineTokens();
        std::for_each(headerLine[0].pStringStart, headerLine[0].pStringEnd, CSTSHeaderParser::convert_lower{});
        *headerLine[0].pStringEnd = '\0';
        *headerLine[2].pStringEnd = '\0';

        EHeaderName headerName = HEADERNAME_MIN;
        if (!_ParseHeadeName(headerLine[0].pStringStart, headerLine[0].pStringEnd, headerName)) {
            if (std::strlen(headerLine[0].pStringStart) < 2 || std::strncmp(headerLine[0].pStringStart, "x-", 2) != 0) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d]NPError. _ParseHeadeName() == false",
                //     L"NPProtocol.cpp",
                //     0x390)
                // Log.Add(LOG_ERROR,
                //     "[NP] Unrecognized header on line %d. Only x- prefixed extension headers may be skipped.",
                //     parsedLineCount + 2)
                L2_LOG_ERROR(
                    Log, "[NP] Unrecognized header on line %d; only x- prefixed extension headers may be skipped.",
                    parsedLineCount + 2
                );
                return false;
            }
        } else {
            m_headerMap.emplace(static_cast<std::int32_t>(headerName), headerLine[2].pStringStart);
        }
        ++parsedLineCount;
    } while (parsedLineCount < 0x400);

    if (!GetHeaderValueAsInt(HEADERNAME_CONTENTLENGTH, m_nBodyLength)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError. GetHeaderValueAsInt() == false",
        //     L"NPProtocol.cpp",
        //     0x39a)
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot parse the response header: the required content-length (l) field is missing.")
        L2_LOG_ERROR(Log, "[NP] Cannot parse the response header: the required content-length (l) field is missing.");
        return false;
    }
    if (!GetHeaderValueAsTransaction(
            HEADERNAME_SUBJECT, m_isTransaction, m_nTRID, m_isSingleTransaction, m_isTransactionComplete
        )) {
        m_isTransaction = false;
        m_nTRID = 0;
        m_isSingleTransaction = true;
        m_isTransactionComplete = true;
    }

    m_pBufferStart = m_pBufferBodyStart;
    m_pBufferHeaderStart = nullptr;
    m_pBufferHeaderEnd = nullptr;
    m_headerMap.clear();
    DecideBytesToDrop();
    m_nParseStep = RPS_HEADER_COMPLETE;
    return true;
}

bool CSTSResponse::ParseBody(pugi::xml_document& document) {
    if (!m_pBufferBodyStart || !m_pBufferBodyEnd) {
        return false;
    }

    const std::size_t bodyLength = static_cast<std::size_t>(m_pBufferBodyEnd - m_pBufferBodyStart);
    if (bodyLength == 0) {
        return true;
    }

    const pugi::xml_parse_result result =
        document.load_buffer(m_pBufferBodyStart, bodyLength, pugi::parse_default, pugi::encoding_utf8);
    return result.status == pugi::status_ok;
}

bool CSTSResponse::AttachReadBuffer(CIOBuffer* readBuffer) {
    if (!readBuffer || m_pReadBuffer) {
        return false;
    }

    m_pReadBuffer = readBuffer;
    m_pReadBuffer->AddRef();
    return true;
}

bool CSTSResponse::GetHeaderValueAsString(EHeaderName headerName, const char** value) {
    if (static_cast<unsigned int>(headerName) >= HEADERNAME_MAX) {
        return false;
    }

    const auto header = m_headerMap.find(static_cast<std::int32_t>(headerName));
    if (header == m_headerMap.end()) {
        return false;
    }
    *value = header->second;
    return true;
}

bool CSTSResponse::GetHeaderValueAsInt(EHeaderName headerName, int& value) {
    const char* text = nullptr;
    const bool result = GetHeaderValueAsString(headerName, &text);
    if (result) {
        value = std::atoi(text);
    }
    return result;
}

bool CSTSResponse::GetHeaderValueAsTransaction(
    EHeaderName headerName,
    bool& isTransaction,
    std::uint32_t& transactionId,
    bool& isSingleTransaction,
    bool& isTransactionComplete
) {
    const char* text = nullptr;
    if (!GetHeaderValueAsString(headerName, &text)) {
        return false;
    }

    char* suffix = nullptr;
    const unsigned long parsed = std::strtoul(text, &suffix, 10);
    transactionId = static_cast<std::uint32_t>(parsed);
    if (parsed > std::numeric_limits<std::uint32_t>::max() || transactionId == std::numeric_limits<std::uint32_t>::max() ||
        !suffix) {
        return false;
    }

    switch (*suffix) {
        case 'r':
            isSingleTransaction = false;
            isTransactionComplete = false;
            break;
        case 'R':
            isSingleTransaction = false;
            isTransactionComplete = true;
            break;
        case '\0':
            isSingleTransaction = true;
            isTransactionComplete = true;
            break;
        default:
            break;
    }
    isTransaction = true;
    return true;
}

} // namespace NPSystem
