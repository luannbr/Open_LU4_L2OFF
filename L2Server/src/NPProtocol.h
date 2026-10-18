#pragma once

#include <stack>

#include "PugiXml.hpp"
#include "Shared/src/IOC.h"
#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <type_traits>

namespace NPSystem {

enum EMethod : std::int32_t {
    METHOD_MIN = 0,
    METHOD_GET = 0,
    METHOD_POST = 1,
    METHOD_MAX = 2,
};

enum EProtocol : std::int32_t {
    PROTOCOL_MIN = 0,
    PROTOCOL_GATE = 0,
    PROTOCOL_AUTH = 1,
    PROTOCOL_SECONDPASSWD = 2,
    PROTOCOL_STS = 3,
    PROTOCOL_GAME = 4,
    PROTOCOL_PRESENCE = 5,
    PROTOCOL_MTX = 6,
    PROTOCOL_SLOT = 7,
    PROTOCOL_LICENSE = 8,
    PROTOCOL_FRIEND = 9,
    PROTOCOL_ACCOUNT = 10,
    PROTOCOL_GRADE = 11,
    PROTOCOL_MAX = 12,
};

enum ECommand : std::int32_t {
    COMMAND_MIN = 0,
    COMMAND_STS_CONNECT = 0,
    COMMAND_STS_PING = 1,
    COMMAND_AUTH_LOGINTOKENFORSERVER = 2,
    COMMAND_AUTH_REQUESTTOKENFORCLIENT = 3,
    NOTIFY_PRESENCE_USERINFO = 4,
    COMMAND_PRESENCE_LOGOUT = 5,
    NOTIFY_PRESENCE_USERLOGOUT = 6,
    COMMAND_LIST_GRADE = 7,
    NOTIFY_GRADE_CHANGED = 8,
    COMMAND_MAX = 9,
};

enum EHeaderName : std::int32_t {
    HEADERNAME_MIN = 0,
    HEADERNAME_ACCEPT = 0,
    HEADERNAME_CONTENTENCODING = 1,
    HEADERNAME_CONTENTLENGTH = 2,
    HEADERNAME_CONTENTTYPE = 3,
    HEADERNAME_FROM = 4,
    HEADERNAME_REFERTO = 5,
    HEADERNAME_REPLYTO = 6,
    HEADERNAME_STREAM = 7,
    HEADERNAME_SUBJECT = 8,
    HEADERNAME_DATE = 9,
    HEADERNAME_TO = 10,
    HEADERNAME_VIA = 11,
    HEADERNAME_ACCEPTENCODING = 12,
    HEADERNAME_ACCEPTLANGUAGE = 13,
    HEADERNAME_CONTENTLANGUAGE = 14,
    HEADERNAME_GEOLOCATION = 15,
    HEADERNAME_TRANSFERENCODING = 16,
    HEADERNAME_MAX = 17,
};

enum EResponseParseStep : std::int32_t {
    RPS_NONE = 0,
    RPS_HEADER_COMPLETE = 1,
    RPS_BODY_COMPLETE = 2,
    RPS_ALL_COMPLETE = 3,
    RPS_MAX = 4,
};

class CSTSHeaderParser {
public:
    struct convert_lower {
        void operator()(char& value) const;
    };

    enum EToken : std::int32_t {
        TOKEN_MIN = 0,
        TOKEN_ERROR = 0,
        TOKEN_WS = 1,
        TOKEN_NVSEP = 2,
        TOKEN_CR = 3,
        TOKEN_LF = 4,
        TOKEN_LINESEP = 5,
        TOKEN_STRING = 6,
        TOKEN_END = 7,
        TOKEN_MAX = 8,
    };

    enum EParseState : std::int32_t {
        PS_NONE = 0,
        PS_IN_CR = 1,
        PS_IN_STR = 2,
        PS_MAX = 3,
    };

    struct Token {
        EToken nType;
        char* pStringStart;
        char* pStringEnd;
    };

    CSTSHeaderParser();
    ~CSTSHeaderParser();

    void Clear();
    bool Init(char* buffer, std::uint64_t bufferSize);
    bool IsValid();
    void MarkStringStart();
    void MarkStringEnd();
    bool IsWS(char value);
    bool IsCR(char value);
    bool IsLF(char value);
    bool IsNVSep(char value);
    bool IsEnd(char value);
    bool GetToken(Token& token, bool afterNameValueSeparator);
    void Consume();

    static const int MAX_TOKEN_COUNT = 5;

    int GetLineTokenCount();
    Token* GetLineTokens();
    bool ParseLineRule(EToken* tokenRule, int tokenCount);
    bool ParseLine();

    char* m_pStart;
    char* m_pEnd;
    char* m_pWork;
    char* m_pStringStart;
    char* m_pStringEnd;
    EParseState m_nParseState;
    Token m_TokenArray[MAX_TOKEN_COUNT];
    int m_nTokenCount;
};


class CSTSRequest {
public:
    using CIOBuffer = IOBuffer32k;
    using StringStack = std::stack<std::string>;

    CSTSRequest() {
        m_pIOBuffer = nullptr;
        Clear();
    }

    ~CSTSRequest() { Clear(); }

    bool Init(int method, int command);
    void Clear();
    bool IsValid() const;
    bool IsValidBody() const;
    bool SetHeaderAsTransaction(unsigned int accountId);
    bool SetHeaderAsString(EHeaderName headerName, const char* value);
    bool SetHeaderAsInt(EHeaderName headerName, int value);
    bool StartBody();
    bool EndBody();
    bool _SaveTransactionInfo();
    bool StartElement(const char* elementName);
    bool EndElement();
    bool AddElementStringValue(const char* elementName, const char* value);
    bool AddElementIntValue(const char* elementName, int value);
    CIOBuffer* GeneratePakcet();

    EMethod m_nMethod;
    ECommand m_nCommand;
    CIOBuffer* m_pIOBuffer;
    char* m_pBufferStart;
    char* m_pBufferEnd;
    char* m_pBufferCur;
    char* m_pBufferContentLengthPos;
    char* m_pBufferBodyStart;
    char* m_pBufferBodyEnd;
    StringStack m_tagStack;
    bool m_isTransaction;
    std::uint32_t m_nTransactionID;
    std::uint32_t m_nAccountID;

private:
    bool _SetMethods();
    bool _SetBodyLength(std::uint64_t bodyLength);
};


class CSTSResponse : public MemoryObject {
public:
    using CIOBuffer = IOBuffer32k;

    CSTSResponse();
    ~CSTSResponse() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    void Clear();
    bool IsReadBufferReady() { return m_pBufferStart && m_pBufferEnd && m_pBufferStart < m_pBufferEnd; }
    bool SetBuffer(char* buffer, std::size_t bufferSize);
    bool PrepareParseHeader();
    void SetNeedResetBuffer(bool needResetBuffer) { m_isNeedResetBuffer = needResetBuffer; }
    bool IsNeedResetBuffer() { return m_isNeedResetBuffer; }
    bool ResetBuffer(char* buffer);
    bool PrepareParseBody();
    bool ParseHeader();
    bool ParseBody(pugi::xml_document& document);
    bool AttachReadBuffer(CIOBuffer* readBuffer);
    bool _ParseHeaderMethod(const char* method, EMethod& parsedMethod);
    bool _ParseHeaderCommand(const char* command, EProtocol& parsedProtocol, ECommand& parsedCommand);
    bool _ParseHeadeName(const char* nameStart, const char* nameEnd, EHeaderName& parsedHeaderName);
    bool IsParsingComplete();
    bool GetHeaderValueAsString(EHeaderName headerName, const char** value);
    bool GetHeaderValueAsInt(EHeaderName headerName, int& value);
    bool GetHeaderValueAsTransaction(
        EHeaderName headerName,
        bool& isTransaction,
        std::uint32_t& transactionId,
        bool& isSingleTransaction,
        bool& isTransactionComplete
    );

    bool m_isResponse;
    std::int32_t m_nResponseCode;
    const char* m_pResponseString;
    bool m_isTransaction;
    std::uint32_t m_nTRID;
    bool m_isSingleTransaction;
    bool m_isTransactionComplete;
    EMethod m_nMethod;
    EProtocol m_nProtocol;
    ECommand m_nCommand;
    char* m_pBufferStart;
    char* m_pBufferEnd;
    char* m_pBufferHeaderStart;
    char* m_pBufferHeaderEnd;
    std::int32_t m_nBodyLength;
    char* m_pBufferBodyStart;
    char* m_pBufferBodyEnd;
    std::map<std::int32_t, const char*> m_headerMap;
    EResponseParseStep m_nParseStep;
    CSTSResponse* m_pNextResponse;
    IOBuffer32k* m_pReadBuffer;
    bool m_bIsKnownCommand;
    bool m_isNeedResetBuffer;
    std::int32_t m_nBytesToDrop;

private:
    L2_DECLARE_OBJECT_REGISTRATION(CSTSResponse)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CSTSResponse)

    void DecideBytesToDrop();
};

struct STSRequestTI {
    unsigned int m_nAccountID;
    ECommand m_nCommand;
};

} // namespace NPSystem

static_assert(NPSystem::METHOD_MIN == 0);
static_assert(NPSystem::METHOD_GET == 0);
static_assert(NPSystem::METHOD_POST == 1);
static_assert(NPSystem::METHOD_MAX == 2);
static_assert(NPSystem::PROTOCOL_MIN == 0);
static_assert(NPSystem::PROTOCOL_GATE == 0);
static_assert(NPSystem::PROTOCOL_AUTH == 1);
static_assert(NPSystem::PROTOCOL_SECONDPASSWD == 2);
static_assert(NPSystem::PROTOCOL_STS == 3);
static_assert(NPSystem::PROTOCOL_GAME == 4);
static_assert(NPSystem::PROTOCOL_PRESENCE == 5);
static_assert(NPSystem::PROTOCOL_MTX == 6);
static_assert(NPSystem::PROTOCOL_SLOT == 7);
static_assert(NPSystem::PROTOCOL_LICENSE == 8);
static_assert(NPSystem::PROTOCOL_FRIEND == 9);
static_assert(NPSystem::PROTOCOL_ACCOUNT == 10);
static_assert(NPSystem::PROTOCOL_GRADE == 11);
static_assert(NPSystem::PROTOCOL_MAX == 12);
static_assert(NPSystem::COMMAND_MIN == 0);
static_assert(NPSystem::COMMAND_STS_CONNECT == 0);
static_assert(NPSystem::COMMAND_STS_PING == 1);
static_assert(NPSystem::COMMAND_AUTH_LOGINTOKENFORSERVER == 2);
static_assert(NPSystem::COMMAND_AUTH_REQUESTTOKENFORCLIENT == 3);
static_assert(NPSystem::NOTIFY_PRESENCE_USERINFO == 4);
static_assert(NPSystem::COMMAND_PRESENCE_LOGOUT == 5);
static_assert(NPSystem::NOTIFY_PRESENCE_USERLOGOUT == 6);
static_assert(NPSystem::COMMAND_LIST_GRADE == 7);
static_assert(NPSystem::NOTIFY_GRADE_CHANGED == 8);
static_assert(NPSystem::COMMAND_MAX == 9);
static_assert(NPSystem::HEADERNAME_MIN == 0);
static_assert(NPSystem::HEADERNAME_ACCEPT == 0);
static_assert(NPSystem::HEADERNAME_CONTENTENCODING == 1);
static_assert(NPSystem::HEADERNAME_CONTENTLENGTH == 2);
static_assert(NPSystem::HEADERNAME_CONTENTTYPE == 3);
static_assert(NPSystem::HEADERNAME_FROM == 4);
static_assert(NPSystem::HEADERNAME_REFERTO == 5);
static_assert(NPSystem::HEADERNAME_REPLYTO == 6);
static_assert(NPSystem::HEADERNAME_STREAM == 7);
static_assert(NPSystem::HEADERNAME_SUBJECT == 8);
static_assert(NPSystem::HEADERNAME_DATE == 9);
static_assert(NPSystem::HEADERNAME_TO == 10);
static_assert(NPSystem::HEADERNAME_VIA == 11);
static_assert(NPSystem::HEADERNAME_ACCEPTENCODING == 12);
static_assert(NPSystem::HEADERNAME_ACCEPTLANGUAGE == 13);
static_assert(NPSystem::HEADERNAME_CONTENTLANGUAGE == 14);
static_assert(NPSystem::HEADERNAME_GEOLOCATION == 15);
static_assert(NPSystem::HEADERNAME_TRANSFERENCODING == 16);
static_assert(NPSystem::HEADERNAME_MAX == 17);
static_assert(NPSystem::RPS_NONE == 0);
static_assert(NPSystem::RPS_HEADER_COMPLETE == 1);
static_assert(NPSystem::RPS_BODY_COMPLETE == 2);
static_assert(NPSystem::RPS_ALL_COMPLETE == 3);
static_assert(NPSystem::RPS_MAX == 4);
static_assert(pugi::parse_default == 0x74);
static_assert(pugi::encoding_utf8 == 1);
static_assert(pugi::status_ok == 0);
static_assert(NPSystem::CSTSHeaderParser::TOKEN_MIN == 0);
static_assert(NPSystem::CSTSHeaderParser::TOKEN_ERROR == 0);
static_assert(NPSystem::CSTSHeaderParser::TOKEN_WS == 1);
static_assert(NPSystem::CSTSHeaderParser::TOKEN_NVSEP == 2);
static_assert(NPSystem::CSTSHeaderParser::TOKEN_CR == 3);
static_assert(NPSystem::CSTSHeaderParser::TOKEN_LF == 4);
static_assert(NPSystem::CSTSHeaderParser::TOKEN_LINESEP == 5);
static_assert(NPSystem::CSTSHeaderParser::TOKEN_STRING == 6);
static_assert(NPSystem::CSTSHeaderParser::TOKEN_END == 7);
static_assert(NPSystem::CSTSHeaderParser::TOKEN_MAX == 8);
static_assert(NPSystem::CSTSHeaderParser::PS_NONE == 0);
static_assert(NPSystem::CSTSHeaderParser::PS_IN_CR == 1);
static_assert(NPSystem::CSTSHeaderParser::PS_IN_STR == 2);
static_assert(NPSystem::CSTSHeaderParser::PS_MAX == 3);
static_assert(NPSystem::CSTSHeaderParser::MAX_TOKEN_COUNT == 5);
static_assert(std::is_aggregate_v<NPSystem::STSRequestTI>);
static_assert(std::is_trivially_copyable_v<NPSystem::STSRequestTI>);
static_assert(std::is_trivially_destructible_v<NPSystem::STSRequestTI>);
