#include "HtmlCommandHandler.h"

#include "CacheSocket.h"
#include "FriendsDb.h"
#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/DelimitedText.h"
#include "Shared/src/Runtime/TargetFormatting.h"
#include "PledgeAnnounceDBHandlerImpl.h"
#include "PledgeDb.h"
#include "Shared/src/DBConn.h"
#include "Shared/src/Log.h"
#include "Shared/src/PledgeAnnounceRegistry.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/TextRuntime.h"
#include "User.h"
#include "UserDb.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

constexpr int kHtmlReplyOpcode = 0xc6;

template <std::size_t Capacity>
void CopyTargetText(std::array<char16_t, Capacity>& destination, std::u16string_view value) {
    destination.fill(0);
    if constexpr (Capacity != 0) {
        const std::size_t copied = std::min(value.size(), Capacity - 1);
        std::copy_n(value.begin(), copied, destination.begin());
    }
}

template <std::size_t Capacity>
void CopyTargetText(std::array<char16_t, Capacity>& destination, const wchar_t* value) {
    CopyTargetText(destination, WideToUtf16(value));
}

std::u16string RemoveAsciiSpaces(std::u16string_view value) {
    // Current global Trim 0x005c9cb0 removes every U+0020 code unit.
    std::u16string result;
    result.reserve(value.size());
    for (const char16_t codeUnit : value) {
        if (codeUnit != u' ')
            result.push_back(codeUnit);
    }
    return result;
}

void WriteMailAudit(int logId, const CUser& actor, const char16_t* detail, const CUser* target = nullptr) {
    // Current 0x004dfdfc/0x004e0464/0x004ded90 and 0x004de7ec
    // write sparse 26-field route-6 records directly.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(logId);
    fields[1] = runtime::DecimalUtf16(actor.GetId());
    fields[2] = runtime::DecimalUtf16(actor.GetAccountID());
    if (target) {
        fields[3] = runtime::DecimalUtf16(target->GetId());
        fields[4] = runtime::DecimalUtf16(target->GetAccountID());
    }
    fields[5] = runtime::DecimalUtf16(actor.GetX());
    fields[6] = runtime::DecimalUtf16(actor.GetY());
    fields[7] = runtime::DecimalUtf16(actor.GetZ());
    if (detail)
        fields[8] = detail;
    fields[11] = runtime::DecimalUtf16(actor.GetRace());
    fields[12] = runtime::DecimalUtf16(actor.GetGender());
    fields[13] = runtime::DecimalUtf16(actor.GetClass());
    fields[14] = runtime::DecimalUtf16(actor.GetLevel());
    fields[21] = std::u16string(actor.GetCharName());
    fields[22] = std::u16string(actor.GetAccountName());
    if (target) {
        fields[23] = std::u16string(target->GetCharName());
        fields[24] = std::u16string(target->GetAccountName());
    }
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void ExecuteSixParameterMailStatement(
    DBConn& connection,
    const wchar_t* statement,
    int characterId,
    int mailType,
    int relatedId,
    const char16_t* receiverNames,
    const char16_t* title,
    const char16_t* content
) {
    // Current unnamed helper 0x004e09c8.
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&mailType);
    connection.BindParameter(&relatedId);
    connection.BindParameter(receiverNames);
    connection.BindParameter(title);
    connection.BindParameter(content);
    // Original: connection.ExecutePrepared(statement)
    if (!connection.ExecutePrepared(statement)) {
        // Previous format: "Mail operation failed for character %d (mail type %d, related ID %d). The caller's success flag is unchanged. SQL: %ls"
        L2_LOG_ERROR(Log,
            "[mail] Operation failed for character %d (mail type %d, related ID %d). The caller's success flag is unchanged. SQL: %ls",
            characterId, mailType, relatedId, statement);
    }
}

} // namespace

CHtmlCommandHandler::CHtmlCommandHandler(CSocket* socket, const unsigned char* packet) :
    m_replyBuffer{}, m_replyLength(0), m_socket(socket), m_packet(packet) {}

CHtmlCommandHandler::~CHtmlCommandHandler() {
    // Current scalar deleting destructor 0x0052ec40 clears both borrowed
    // transport pointers before returning.
    m_socket = nullptr;
    m_packet = nullptr;
}

char16_t* CHtmlCommandHandler::MaskSqlApostrophes(char16_t* text) {
    // Current Cached.exe 0x004dc7ac re-evaluates the UTF-16 length on every
    // iteration and replaces each apostrophe with U+007f in place.
    for (std::uint32_t index = 0; index < std::char_traits<char16_t>::length(text); ++index) {
        if (text[index] == u'\'') {
            text[index] = u'\x7f';
        }
    }
    return text;
}

CountMail::CountMail(std::int32_t characterId) noexcept :
    m_characterId(characterId), m_incomingCount(0), m_sentCount(0), m_archivedCount(0), m_temporaryCount(0) {}

void CountMail::Execute() {
    // Current Cached.exe 0x004e0a54.
    L2_GUARD_FUNCTION();
    m_incomingCount = 0;
    m_sentCount = 0;
    m_archivedCount = 0;
    m_temporaryCount = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&m_incomingCount);
    connection.Bind(&m_sentCount);
    connection.Bind(&m_archivedCount);
    connection.Bind(&m_temporaryCount);
    constexpr const wchar_t* statement = L"{Call lin_GetMailCount (? )}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    // connection.ExecutePrepared(statement)
    // connection.Fetch()
    const bool executed = connection.ExecutePrepared(statement);
    const bool fetched = connection.Fetch();
    if (!executed || !fetched) {
        // Previous format: "Cannot load mail counts for character %d (query %s, row %s). Continuing with incoming %d, sent %d, archived %d, drafts %d. SQL: %ls"
        L2_LOG_ERROR(Log, "[mail] Cannot load counts for character %d (query %s, row %s); continuing with incoming %d, sent %d, archived %d, drafts %d. SQL: %ls",
                     m_characterId, executed ? "executed" : "failed", fetched ? "fetched" : "unavailable",
                     m_incomingCount, m_sentCount, m_archivedCount, m_temporaryCount, statement);
    }
}

// Current Cached.exe 0x00529ccc.
ListMailHtmlCommandHandler::ListMailHtmlCommandHandler(CSocket* socket, const unsigned char* packet) :
    CHtmlCommandHandler(socket, packet),
    m_characterId(0),
    m_userSocketId(0),
    m_rowCount(0),
    m_totalCount(0) {}

bool ListMailHtmlCommandHandler::DisassemblePacket() {
    // Current Cached.exe 0x004dca70.
    L2_GUARD_FUNCTION();
    const unsigned char* cursor = ::Disassemble(m_packet, "dd", &m_characterId, &m_userSocketId);
    m_listParameter.DisassemblePacket(cursor);
    return true;
}

// Current Cached.exe 0x00529d94.
ReadMailHtmlCommandHandler::ReadMailHtmlCommandHandler(CSocket* socket, const unsigned char* packet) :
    CHtmlCommandHandler(socket, packet),
    m_characterId(0),
    m_userSocketId(0),
    m_mailId(0),
    m_senderCharacterId(0),
    m_title{} {}

bool ReadMailHtmlCommandHandler::DisassemblePacket() {
    // Current Cached.exe 0x004dcb3c.
    L2_GUARD_FUNCTION();
    const unsigned char* cursor = ::Disassemble(m_packet, "ddd", &m_characterId, &m_userSocketId, &m_mailId);
    m_listParameter.DisassemblePacket(cursor);
    return true;
}

// Current Cached.exe 0x00590224. The machine constructor zeroes the complete
// fixed UTF-16 fields, both vectors, result/success state and constructs the
// resident CMailListParameter at target +0x6b6c.
SendMailHtmlCommandHandler::SendMailHtmlCommandHandler(CSocket* socket, const unsigned char* packet) :
    CHtmlCommandHandler(socket, packet),
    m_characterId(0),
    m_userSocketId(0),
    m_relatedId(0),
    m_mailType(0),
    m_receivers{},
    m_title{},
    m_content{},
    m_success(false),
    m_result(0),
    m_failureName{} {}

bool SendMailHtmlCommandHandler::DisassemblePacket() {
    // Current Cached.exe 0x004dcc14.
    L2_GUARD_FUNCTION();
    const unsigned char* cursor = ::Disassemble(
        m_packet, "ddddSSS", &m_characterId, &m_userSocketId, &m_mailType, &m_relatedId, 500, m_receivers.data(), 400,
        m_title.data(), 6000, m_content.data()
    );
    m_listParameter.DisassemblePacket(cursor);
    return true;
}

// Current Cached.exe 0x00529e70.
SaveMailHtmlCommandHandler::SaveMailHtmlCommandHandler(CSocket* socket, const unsigned char* packet) :
    CHtmlCommandHandler(socket, packet),
    m_characterId(0),
    m_userSocketId(0),
    m_relatedId(0),
    m_mailType(0),
    m_receivers{},
    m_title{},
    m_content{},
    m_success(false) {}

bool SaveMailHtmlCommandHandler::DisassemblePacket() {
    // Current Cached.exe 0x004dcea4.
    L2_GUARD_FUNCTION();
    const unsigned char* cursor = ::Disassemble(
        m_packet, "ddddSSS", &m_characterId, &m_userSocketId, &m_mailType, &m_relatedId, 500, m_receivers.data(), 400,
        m_title.data(), 6000, m_content.data()
    );
    m_listParameter.DisassemblePacket(cursor);
    return true;
}

// Current Cached.exe 0x00529f78.
GetTempMailHtmlCommandHandler::GetTempMailHtmlCommandHandler(CSocket* socket, const unsigned char* packet) :
    CHtmlCommandHandler(socket, packet), m_characterId(0), m_userSocketId(0), m_mailId(0) {}

bool GetTempMailHtmlCommandHandler::DisassemblePacket() {
    // Current Cached.exe 0x004dd1f8.
    L2_GUARD_FUNCTION();
    const unsigned char* cursor = ::Disassemble(m_packet, "ddd", &m_characterId, &m_userSocketId, &m_mailId);
    m_listParameter.DisassemblePacket(cursor);
    return true;
}

// Current Cached.exe 0x00529ff4.
ReplyMailHtmlCommandHandler::ReplyMailHtmlCommandHandler(CSocket* socket, const unsigned char* packet) :
    CHtmlCommandHandler(socket, packet), m_characterId(0), m_userSocketId(0), m_mailId(0) {}

bool ReplyMailHtmlCommandHandler::DisassemblePacket() {
    // Current Cached.exe 0x004dd3fc.
    L2_GUARD_FUNCTION();
    const unsigned char* cursor = ::Disassemble(m_packet, "ddd", &m_characterId, &m_userSocketId, &m_mailId);
    m_listParameter.DisassemblePacket(cursor);
    return true;
}

// Current Cached.exe 0x0052a070.
ForwardMailHtmlCommandHandler::ForwardMailHtmlCommandHandler(CSocket* socket, const unsigned char* packet) :
    CHtmlCommandHandler(socket, packet), m_characterId(0), m_userSocketId(0), m_mailId(0) {}

bool ForwardMailHtmlCommandHandler::DisassemblePacket() {
    // Current Cached.exe 0x004dd600.
    L2_GUARD_FUNCTION();
    const unsigned char* cursor = ::Disassemble(m_packet, "ddd", &m_characterId, &m_userSocketId, &m_mailId);
    m_listParameter.DisassemblePacket(cursor);
    return true;
}

// Current Cached.exe 0x0052a130.
ModifyTempMailHtmlCommandHandler::ModifyTempMailHtmlCommandHandler(CSocket* socket, const unsigned char* packet) :
    CHtmlCommandHandler(socket, packet),
    m_characterId(0),
    m_userSocketId(0),
    m_mailId(0),
    m_receivers{},
    m_title{},
    m_content{} {}

bool ModifyTempMailHtmlCommandHandler::DisassemblePacket() {
    // Current Cached.exe 0x004dd804.
    L2_GUARD_FUNCTION();
    ::Disassemble(
        m_packet, "dddSSS", &m_characterId, &m_userSocketId, &m_mailId, 500, m_receivers.data(), 400, m_title.data(), 6000,
        m_content.data()
    );
    return true;
}

// Current Cached.exe 0x005903d4. The matching list parameter is at
// target +0x6b20, followed by the two vectors and reply state.
ModifySendTempMailHtmlCommandHandler::ModifySendTempMailHtmlCommandHandler(CSocket* socket, const unsigned char* packet) :
    CHtmlCommandHandler(socket, packet),
    m_characterId(0),
    m_userSocketId(0),
    m_mailId(0),
    m_receivers{},
    m_title{},
    m_content{},
    m_success(false),
    m_result(0),
    m_failureName{} {}

bool ModifySendTempMailHtmlCommandHandler::DisassemblePacket() {
    // Current Cached.exe 0x004dd9d8.
    L2_GUARD_FUNCTION();
    const unsigned char* cursor = ::Disassemble(
        m_packet, "dddSSS", &m_characterId, &m_userSocketId, &m_mailId, 500, m_receivers.data(), 400, m_title.data(), 6000,
        m_content.data()
    );
    m_listParameter.DisassemblePacket(cursor);
    return true;
}

// Current Cached.exe 0x0052a1cc.
DeleteMailHtmlCommandHandler::DeleteMailHtmlCommandHandler(CSocket* socket, const unsigned char* packet) :
    CHtmlCommandHandler(socket, packet), m_characterId(0), m_userSocketId(0), m_mailId(0) {}

bool DeleteMailHtmlCommandHandler::DisassemblePacket() {
    // Current Cached.exe 0x004ddc54.
    L2_GUARD_FUNCTION();
    const unsigned char* cursor = ::Disassemble(m_packet, "ddd", &m_characterId, &m_userSocketId, &m_mailId);
    m_listParameter.DisassemblePacket(cursor);
    return true;
}

// Current Cached.exe 0x0052a248.
ArchiveMailHtmlCommandHandler::ArchiveMailHtmlCommandHandler(CSocket* socket, const unsigned char* packet) :
    CHtmlCommandHandler(socket, packet),
    m_characterId(0),
    m_userSocketId(0),
    m_mailId(0),
    m_success(false) {}

bool ArchiveMailHtmlCommandHandler::DisassemblePacket() {
    // Current Cached.exe 0x004dde44.
    L2_GUARD_FUNCTION();
    const unsigned char* cursor = ::Disassemble(m_packet, "ddd", &m_characterId, &m_userSocketId, &m_mailId);
    m_listParameter.DisassemblePacket(cursor);
    return true;
}

UnreadMailCountCommandHandler::UnreadMailCountCommandHandler(CSocket* socket, const unsigned char* packet) :
    CHtmlCommandHandler(socket, packet), m_characterId(0), m_userSocketId(0), m_unreadCount(0) {}

bool UnreadMailCountCommandHandler::DisassemblePacket() {
    // Current Cached.exe 0x004de114.
    L2_GUARD_FUNCTION();
    ::Disassemble(m_packet, "dd", &m_characterId, &m_userSocketId);
    return true;
}

// Current Cached.exe 0x0052a2e8.
SendPledgeMailHtmlCommandHandler::SendPledgeMailHtmlCommandHandler(CSocket* socket, const unsigned char* packet) :
    CHtmlCommandHandler(socket, packet),
    m_characterId(0),
    m_userSocketId(0),
    m_pledgeName{},
    m_title{},
    m_content{},
    m_success(false),
    m_result(0) {}

bool SendPledgeMailHtmlCommandHandler::DisassemblePacket() {
    // Current Cached.exe 0x004de2a8.
    L2_GUARD_FUNCTION();
    ::Disassemble(
        m_packet, "ddSSS", &m_characterId, &m_userSocketId, 100, m_pledgeName.data(), 400, m_title.data(), 6000,
        m_content.data()
    );
    return true;
}

// Current Cached.exe constructor body 0x00547dfc.
DeleteFriendBatchHtmlCommandHandler::DeleteFriendBatchHtmlCommandHandler(CSocket* socket, const unsigned char* packet) :
    CHtmlCommandHandler(socket, packet), m_characterId(0), m_userSocketId(0) {}

bool DeleteFriendBatchHtmlCommandHandler::DisassemblePacket() {
    // Current Cached.exe 0x004e3308.
    L2_GUARD_FUNCTION();
    int count = 0;
    const unsigned char* cursor = ::Disassemble(m_packet, "ddd", &m_characterId, &m_userSocketId, &count);
    for (int index = 0; index < count && index < 5; ++index) {
        int friendCharacterId = 0;
        cursor = ::Disassemble(cursor, "d", &friendCharacterId);
        m_succeededIds.push_back(friendCharacterId);
    }
    return true;
}

// Current Cached.exe 0x00590564.
DeleteBlockBatchHtmlCommandHandler::DeleteBlockBatchHtmlCommandHandler(CSocket* socket, const unsigned char* packet) :
    CHtmlCommandHandler(socket, packet), m_characterId(0), m_userSocketId(0) {}

bool DeleteBlockBatchHtmlCommandHandler::DisassemblePacket() {
    // Current Cached.exe 0x004e4240.
    L2_GUARD_FUNCTION();
    int count = 0;
    const unsigned char* cursor = ::Disassemble(m_packet, "ddd", &m_characterId, &m_userSocketId, &count);
    for (int index = 0; index < count && index < 5; ++index) {
        int result = 0;
        std::array<char16_t, 25> name{};
        cursor = ::Disassemble(cursor, "dS", &result, 0x32, name.data());
        m_results.push_back(result);
        m_names.emplace_back(std::u16string_view(name.data()));
    }
    return true;
}

bool ListMailHtmlCommandHandler::HandleInternal() {
    // Current Cached.exe 0x004e0bd0.
    L2_GUARD_FUNCTION();
    int mailId = 0;
    int senderCharacterId = 0;
    std::array<char16_t, 250> senderName{};
    std::array<char16_t, 200> title{};
    int sendDate = 0;
    int mailType = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&mailId);
    connection.Bind(&senderCharacterId);
    connection.Bind(senderName.data(), 500);
    connection.Bind(title.data(), 400);
    connection.Bind(&sendDate);
    connection.Bind(&mailType);
    connection.Bind(&m_totalCount);
    constexpr const wchar_t* statement = L"{Call lin_GetMailList (?, ?, ?, ?, ?, ? )}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    int mailBoxType = m_listParameter.GetMailBoxType();
    connection.BindParameter(&mailBoxType);
    int page = m_listParameter.GetPage();
    if (page < 1) {
        page = 1;
    }
    const int rowsPerPage = m_listParameter.GetRowsPerPage();
    connection.BindParameter(&page);
    connection.BindParameter(&rowsPerPage);
    int searchTarget = m_listParameter.GetSearchTarget();
    connection.BindParameter(&searchTarget);
    char16_t* const keywordText = m_listParameter.GetKeyword();
    MaskSqlApostrophes(keywordText);
    const std::u16string keyword(keywordText);
    connection.BindParameter(keyword.c_str());

    if (connection.ExecutePrepared(statement)) {
        int oneBasedRow = 0;
        while (connection.Fetch()) {
            ++oneBasedRow;
            if (rowsPerPage * (page - 1) >= oneBasedRow) {
                continue;
            }
            const int assembled = ::Assemble(
                m_replyBuffer.data() + m_replyLength, static_cast<int>(m_replyBuffer.size()) - m_replyLength, "ddSSd",
                mailId, senderCharacterId, senderName.data(), title.data(), sendDate
            );
            if (assembled > 0) {
                m_replyLength += assembled;
                ++m_rowCount;
            }
        }
    } else {
        // Previous format: "Cannot load mail list for character %d, mailbox %d, page %d. No rows added to the reply. SQL: %ls"
        L2_LOG_ERROR(Log, "[mail] Cannot load list for character %d, mailbox %d, page %d; no rows added to the reply. SQL: %ls",
                     m_characterId, mailBoxType, page, statement);
    }
    return true;
}

bool ListMailHtmlCommandHandler::SendReply() {
    // Current Cached.exe 0x004e2f40.
    L2_GUARD_FUNCTION();
    CountMail counts(m_characterId);
    counts.Execute();
    std::array<char, 1024> parameterPacket{};
    const int parameterLength =
        m_listParameter.AssemblePacket(parameterPacket.data(), static_cast<int>(parameterPacket.size()));
    m_socket->Send(
        "chddddddbdb", kHtmlReplyOpcode, 2, m_userSocketId, counts.m_incomingCount, counts.m_sentCount,
        counts.m_archivedCount, counts.m_temporaryCount, m_totalCount, parameterLength, parameterPacket.data(), m_rowCount,
        m_replyLength, m_replyBuffer.data()
    );
    return true;
}

bool ReadMailHtmlCommandHandler::HandleInternal() {
    // Current Cached.exe 0x004e0eb0.
    L2_GUARD_FUNCTION();
    int rowMailId = 0;
    m_senderCharacterId = 0;
    std::array<char16_t, 50> senderName{};
    std::array<char16_t, 250> receiverNames{};
    m_title.fill(0);
    std::array<char16_t, 3000> content{};
    int sendDate = 0;
    int mailType = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&rowMailId);
    connection.Bind(&m_senderCharacterId);
    connection.Bind(senderName.data(), 100);
    connection.Bind(receiverNames.data(), 500);
    std::array<char16_t, 150> title{};
    connection.Bind(title.data(), 300);
    connection.Bind(content.data(), 6000);
    connection.Bind(&sendDate);
    connection.Bind(&mailType);
    constexpr const wchar_t* statement = L"{Call lin_GetMail (?, ? )}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&m_mailId);
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            CopyTargetText(m_title, title.data());
            const int assembled = ::Assemble(
                m_replyBuffer.data() + m_replyLength, static_cast<int>(m_replyBuffer.size()) - m_replyLength, "ddSSSSdd",
                rowMailId, m_senderCharacterId, senderName.data(), receiverNames.data(), m_title.data(),
                content.data(), sendDate, mailType
            );
            if (assembled > 0)
                m_replyLength += assembled;
        }
    } else {
        // Previous format: "Cannot read mail %d for character %d. No mail data added to the reply. SQL: %ls"
        L2_LOG_ERROR(Log, "[mail] Cannot read mail %d for character %d; no mail data added to the reply. SQL: %ls",
                     m_mailId, m_characterId, statement);
    }
    return true;
}

bool ReadMailHtmlCommandHandler::SendReply() {
    // Current Cached.exe 0x004de7ec.
    L2_GUARD_FUNCTION();
    std::array<char, 1024> parameterPacket{};
    const int parameterLength =
        m_listParameter.AssemblePacket(parameterPacket.data(), static_cast<int>(parameterPacket.size()));
    m_socket->Send(
        "chdbb", kHtmlReplyOpcode, 3, m_userSocketId, parameterLength, parameterPacket.data(), m_replyLength,
        m_replyBuffer.data()
    );

    CUserSP actor = g_userDb.GetUser(m_characterId, true, false, runtime::process::CurrentDatabaseId());
    CUserSP sender = g_userDb.GetUser(m_senderCharacterId, true, true, runtime::process::CurrentDatabaseId());
    if (actor && sender) {
        WriteMailAudit(0x353, *actor.get(), m_title.data(), sender.get());
    }
    return true;
}

bool SendMailHtmlCommandHandler::ParseReceiverList() {
    // Current Cached.exe 0x004e3900.
    L2_GUARD_FUNCTION();
    const std::u16string receiverText(m_receivers.data());
    std::size_t begin = 0;
    while (begin <= receiverText.size()) {
        const std::size_t separator = receiverText.find(u';', begin);
        const std::size_t end = separator == std::u16string::npos ? receiverText.size() : separator;
        const std::u16string receiverName = RemoveAsciiSpaces(std::u16string_view(receiverText).substr(begin, end - begin));
        if (!receiverName.empty()) {
            CUserSP receiver = g_userDb.GetUser(receiverName.c_str(), false);
            if (!receiver) {
                m_result = -1;
                CopyTargetText(m_failureName, receiverName);
                return false;
            }
            const int builder = receiver->GetBuilder();
            if (builder > 0 && builder < 7) {
                m_result = -5;
                CopyTargetText(m_failureName, receiverName);
                return false;
            }
            m_receiverNames.emplace_back(std::u16string_view(receiverName));
            m_receiverIds.push_back(static_cast<int>(receiver->GetId()));
        }
        if (separator == std::u16string::npos)
            break;
        begin = separator + 1;
    }
    if (m_receiverNames.empty() || m_receiverNames.size() >= 6) {
        m_result = -3;
        return false;
    }
    return true;
}

std::u16string SendMailHtmlCommandHandler::GetReceiverNameList() const {
    // Current Cached.exe 0x004dfcf8.
    L2_GUARD_FUNCTION();
    std::u16string result;
    for (const std::u16string& name : m_receiverNames) {
        result += name;
        result += u";";
    }
    return result;
}

bool SendMailHtmlCommandHandler::CheckSendMailLimit() {
    // Current Cached.exe 0x004e131c.
    L2_GUARD_FUNCTION();
    int count = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&count);
    constexpr const wchar_t* statement = L"{Call lin_GetSendMailCount (? )}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    // connection.ExecutePrepared(statement)
    // connection.Fetch()
    const bool executed = connection.ExecutePrepared(statement);
    const bool fetched = connection.Fetch();
    if (!executed || !fetched) {
        // Previous format: "Cannot check the mail sending limit for character %d (query %s, row %s). Continuing with count %d. SQL: %ls"
        L2_LOG_ERROR(Log, "[mail] Cannot check the sending limit for character %d (query %s, row %s); continuing with count %d. SQL: %ls",
                     m_characterId, executed ? "executed" : "failed", fetched ? "fetched" : "unavailable",
                     count, statement);
    }
    if (count > 9) {
        m_result = -2;
        return false;
    }
    return true;
}

bool SendMailHtmlCommandHandler::CheckBlockList() {
    // Current Cached.exe 0x004e148c.
    L2_GUARD_FUNCTION();
    int blockCount = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    for (const int receiverId : m_receiverIds) {
        connection.Bind(&blockCount);
        constexpr const wchar_t* statement = L"{Call lin_GetBlockCount (?, ? )}";
        connection.Prepare(statement);
        connection.BindParameter(&m_characterId);
        connection.BindParameter(&receiverId);
        // connection.ExecutePrepared(statement)
        // connection.Fetch()
        const bool executed = connection.ExecutePrepared(statement);
        const bool fetched = connection.Fetch();
        if (!executed || !fetched) {
            // Previous format: "Cannot check the mail block list for character %d and receiver %d (query %s, row %s). Continuing with block count %d. SQL: %ls"
            L2_LOG_ERROR(Log, "[mail] Cannot check the block list for character %d and receiver %d (query %s, row %s); continuing with block count %d. SQL: %ls",
                         m_characterId, receiverId, executed ? "executed" : "failed",
                         fetched ? "fetched" : "unavailable", blockCount, statement);
        }
        if (blockCount > 0) {
            m_result = -4;
            CUserSP receiver = g_userDb.GetUser(receiverId, true, false, runtime::process::CurrentDatabaseId());
            if (receiver) {
                CopyTargetText(m_failureName, receiver->GetCharName());
            }
            return false;
        }
        connection.UnBind();
    }
    return true;
}

void SendMailHtmlCommandHandler::SendMail() {
    // Current Cached.exe 0x004e1198.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    const std::u16string receiverList = GetReceiverNameList();
    MaskSqlApostrophes(m_content.data());
    MaskSqlApostrophes(m_title.data());
    ExecuteSixParameterMailStatement(
        connection, L"{Call lin_SendMail (?, ?, ?, ?, ?, ? )}", m_characterId, m_mailType, m_relatedId,
        receiverList.c_str(), m_title.data(), m_content.data()
    );
}

bool SendMailHtmlCommandHandler::HandleInternal() {
    // Current Cached.exe 0x004e4480.
    L2_GUARD_FUNCTION();
    m_success = ParseReceiverList() && CheckSendMailLimit() && CheckBlockList();
    if (m_success)
        SendMail();
    return true;
}

void SendMailHtmlCommandHandler::SendSuccessReply() {
    // Current Cached.exe 0x004dfdfc.
    L2_GUARD_FUNCTION();
    std::array<char, 0x5000> receiverPacket{};
    int receiverPacketLength = 0;
    for (const int receiverId : m_receiverIds) {
        const int assembled = ::Assemble(
            receiverPacket.data() + receiverPacketLength, static_cast<int>(receiverPacket.size()) - receiverPacketLength,
            "d", receiverId
        );
        if (assembled > 0)
            receiverPacketLength += assembled;
    }
    m_socket->Send(
        "chddb", kHtmlReplyOpcode, 5, m_userSocketId, static_cast<int>(m_receiverIds.size()), receiverPacketLength,
        receiverPacket.data()
    );

    CUserSP actor = g_userDb.GetUser(m_characterId, true, false, runtime::process::CurrentDatabaseId());
    if (actor) {
        const std::u16string receiverList = GetReceiverNameList();
        WriteMailAudit(0x352, *actor.get(), receiverList.c_str());
    }
}

void SendMailHtmlCommandHandler::SendFailureReply() {
    // Current Cached.exe 0x004dcd38.
    L2_GUARD_FUNCTION();
    std::array<char, 1024> parameterPacket{};
    const int parameterLength =
        m_listParameter.AssemblePacket(parameterPacket.data(), static_cast<int>(parameterPacket.size()));
    m_socket->Send(
        "chddbddSSSS", kHtmlReplyOpcode, 5, m_userSocketId, m_result, parameterLength, parameterPacket.data(), m_mailType,
        m_relatedId, m_receivers.data(), m_title.data(), m_content.data(), m_failureName.data()
    );
}

bool SendMailHtmlCommandHandler::SendReply() {
    // Current Cached.exe 0x004e16b0.
    L2_GUARD_FUNCTION();
    if (m_success) {
        SendSuccessReply();
    } else {
        SendFailureReply();
    }
    return true;
}

bool SaveMailHtmlCommandHandler::CheckSaveLimit() {
    // Current Cached.exe 0x004e30d4.
    L2_GUARD_FUNCTION();
    CountMail counts(m_characterId);
    counts.Execute();
    return counts.m_temporaryCount < 10;
}

bool SaveMailHtmlCommandHandler::HandleInternal() {
    // Current Cached.exe 0x004e3518.
    L2_GUARD_FUNCTION();
    if (!CheckSaveLimit()) {
        m_success = false;
        return true;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    MaskSqlApostrophes(m_content.data());
    MaskSqlApostrophes(m_title.data());
    MaskSqlApostrophes(m_receivers.data());
    ExecuteSixParameterMailStatement(
        connection, L"{Call lin_SaveMail (?, ?, ?, ?, ?, ?)}", m_characterId, m_mailType, m_relatedId, m_receivers.data(),
        m_title.data(), m_content.data()
    );
    // Current target sets this of ExecutePrepared's result.
    m_success = true;
    return true;
}

void SaveMailHtmlCommandHandler::SendSuccessReply() {
    // Current Cached.exe 0x004dcfc8.
    L2_GUARD_FUNCTION();
    m_socket->Send("chdd", kHtmlReplyOpcode, 6, m_userSocketId, 0);
}

void SaveMailHtmlCommandHandler::SendFailureReply() {
    // Current Cached.exe 0x004dd098.
    L2_GUARD_FUNCTION();
    std::array<char, 1024> parameterPacket{};
    const int parameterLength =
        m_listParameter.AssemblePacket(parameterPacket.data(), static_cast<int>(parameterPacket.size()));
    m_socket->Send(
        "chddbddSSS", kHtmlReplyOpcode, 6, m_userSocketId, 1, parameterLength, parameterPacket.data(), m_mailType,
        m_relatedId, m_receivers.data(), m_title.data(), m_content.data()
    );
}

bool SaveMailHtmlCommandHandler::SendReply() {
    // Current Cached.exe 0x004deb44.
    L2_GUARD_FUNCTION();
    if (m_success) {
        SendSuccessReply();
    } else {
        SendFailureReply();
    }
    return true;
}

bool GetTempMailHtmlCommandHandler::HandleInternal() {
    // Current Cached.exe 0x004e1764.
    L2_GUARD_FUNCTION();
    int rowMailId = 0;
    std::array<char16_t, 250> receivers{};
    std::array<char16_t, 200> title{};
    std::array<char16_t, 3000> content{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&rowMailId);
    connection.Bind(receivers.data(), 500);
    connection.Bind(title.data(), 400);
    connection.Bind(content.data(), 6000);
    constexpr const wchar_t* statement = L"{Call lin_GetTempMail (?, ? )}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&m_mailId);
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            const int assembled = ::Assemble(
                m_replyBuffer.data() + m_replyLength, static_cast<int>(m_replyBuffer.size()) - m_replyLength, "dSSS",
                rowMailId, receivers.data(), title.data(), content.data()
            );
            if (assembled > 0)
                m_replyLength += assembled;
        }
    } else {
        // Previous format: "Cannot load mail draft %d for character %d. No draft data added to the reply. SQL: %ls"
        L2_LOG_ERROR(Log, "[mail] Cannot load draft %d for character %d; no draft data added to the reply. SQL: %ls",
                     m_mailId, m_characterId, statement);
    }
    return true;
}

bool GetTempMailHtmlCommandHandler::SendReply() {
    // Current Cached.exe 0x004dd2d0.
    L2_GUARD_FUNCTION();
    std::array<char, 1024> parameterPacket{};
    const int parameterLength =
        m_listParameter.AssemblePacket(parameterPacket.data(), static_cast<int>(parameterPacket.size()));
    m_socket->Send(
        "chdbb", kHtmlReplyOpcode, 8, m_userSocketId, parameterLength, parameterPacket.data(), m_replyLength,
        m_replyBuffer.data()
    );
    return true;
}

bool ReplyMailHtmlCommandHandler::HandleInternal() {
    // Current Cached.exe 0x004e19d8.
    L2_GUARD_FUNCTION();
    int rowMailId = 0;
    int senderCharacterId = 0;
    std::array<char16_t, 50> senderName{};
    std::array<char16_t, 250> receiverNames{};
    std::array<char16_t, 200> title{};
    std::array<char16_t, 3000> content{};
    int sendDate = 0;
    int mailType = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&rowMailId);
    connection.Bind(&senderCharacterId);
    connection.Bind(senderName.data(), 100);
    connection.Bind(receiverNames.data(), 500);
    connection.Bind(title.data(), 400);
    connection.Bind(content.data(), 6000);
    connection.Bind(&sendDate);
    connection.Bind(&mailType);
    constexpr const wchar_t* statement = L"{Call lin_GetMail (?, ? )}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&m_mailId);
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            const int assembled = ::Assemble(
                m_replyBuffer.data() + m_replyLength, static_cast<int>(m_replyBuffer.size()) - m_replyLength, "dSSSS",
                rowMailId, senderName.data(), receiverNames.data(), title.data(), content.data()
            );
            if (assembled > 0)
                m_replyLength += assembled;
        }
    } else {
        // Previous format: "Cannot load mail %d for character %d to compose a reply. No mail data added to the reply packet. SQL: %ls"
        L2_LOG_ERROR(Log, "[mail] Cannot load mail %d for character %d to compose a reply; no mail data added to the reply packet. SQL: %ls",
                     m_mailId, m_characterId, statement);
    }
    return true;
}

bool ReplyMailHtmlCommandHandler::SendReply() {
    // Current Cached.exe 0x004dd4d4.
    L2_GUARD_FUNCTION();
    std::array<char, 1024> parameterPacket{};
    const int parameterLength =
        m_listParameter.AssemblePacket(parameterPacket.data(), static_cast<int>(parameterPacket.size()));
    m_socket->Send(
        "chdbb", kHtmlReplyOpcode, 9, m_userSocketId, parameterLength, parameterPacket.data(), m_replyLength,
        m_replyBuffer.data()
    );
    return true;
}

bool ForwardMailHtmlCommandHandler::HandleInternal() {
    // Current Cached.exe 0x004e1cb0.
    L2_GUARD_FUNCTION();
    int rowMailId = 0;
    int senderCharacterId = 0;
    std::array<char16_t, 50> senderName{};
    std::array<char16_t, 250> receiverNames{};
    std::array<char16_t, 200> title{};
    std::array<char16_t, 3000> content{};
    int sendDate = 0;
    int mailType = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&rowMailId);
    connection.Bind(&senderCharacterId);
    connection.Bind(senderName.data(), 100);
    connection.Bind(receiverNames.data(), 500);
    connection.Bind(title.data(), 400);
    connection.Bind(content.data(), 6000);
    connection.Bind(&sendDate);
    connection.Bind(&mailType);
    constexpr const wchar_t* statement = L"{Call lin_GetMail (?, ? )}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&m_mailId);
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            const int assembled = ::Assemble(
                m_replyBuffer.data() + m_replyLength, static_cast<int>(m_replyBuffer.size()) - m_replyLength, "dSSSS",
                rowMailId, senderName.data(), receiverNames.data(), title.data(), content.data()
            );
            if (assembled > 0)
                m_replyLength += assembled;
        }
    } else {
        // Previous format: "Cannot load mail %d for character %d for forwarding. No mail data added to the reply. SQL: %ls"
        L2_LOG_ERROR(Log, "[mail] Cannot load mail %d for character %d for forwarding; no mail data added to the reply. SQL: %ls",
                     m_mailId, m_characterId, statement);
    }
    return true;
}

bool ForwardMailHtmlCommandHandler::SendReply() {
    // Current Cached.exe 0x004dd6d8.
    L2_GUARD_FUNCTION();
    std::array<char, 1024> parameterPacket{};
    const int parameterLength =
        m_listParameter.AssemblePacket(parameterPacket.data(), static_cast<int>(parameterPacket.size()));
    m_socket->Send(
        "chdbb", kHtmlReplyOpcode, 10, m_userSocketId, parameterLength, parameterPacket.data(), m_replyLength,
        m_replyBuffer.data()
    );
    return true;
}

bool ModifyTempMailHtmlCommandHandler::HandleInternal() {
    // Current Cached.exe 0x004e1f88.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    MaskSqlApostrophes(m_content.data());
    MaskSqlApostrophes(m_title.data());
    MaskSqlApostrophes(m_receivers.data());
    const std::u16string receivers(m_receivers.data());
    const std::u16string title(m_title.data());
    const std::u16string content(m_content.data());
    constexpr const wchar_t* statement = L"{Call lin_ModifyTempMail (?, ?, ?, ?, ? )}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&m_mailId);
    connection.BindParameter(receivers.c_str());
    connection.BindParameter(title.c_str());
    connection.BindParameter(content.c_str());
    // Original: connection.ExecutePrepared(statement)
    if (!connection.ExecutePrepared(statement)) {
        // Previous format: "Cannot modify mail draft %d for character %d. The handler still returns success. SQL: %ls"
        L2_LOG_ERROR(Log,
            "[mail] Cannot modify draft %d for character %d; the handler still returns success. SQL: %ls",
            m_mailId, m_characterId, statement);
    }
    return true;
}

bool ModifyTempMailHtmlCommandHandler::SendReply() {
    // Current Cached.exe 0x004dd90c.
    L2_GUARD_FUNCTION();
    m_socket->Send("chd", kHtmlReplyOpcode, 11, m_userSocketId);
    return true;
}

bool ModifySendTempMailHtmlCommandHandler::ParseReceiverList() {
    // Current Cached.exe 0x004e3da0.
    L2_GUARD_FUNCTION();
    const std::u16string receiverText(m_receivers.data());
    std::size_t begin = 0;
    while (begin <= receiverText.size()) {
        const std::size_t separator = receiverText.find(u';', begin);
        const std::size_t end = separator == std::u16string::npos ? receiverText.size() : separator;
        const std::u16string receiverName = RemoveAsciiSpaces(std::u16string_view(receiverText).substr(begin, end - begin));
        if (!receiverName.empty()) {
            CUserSP receiver = g_userDb.GetUser(receiverName.c_str(), false);
            if (!receiver) {
                m_result = -1;
                CopyTargetText(m_failureName, receiverName);
                return false;
            }
            const int builder = receiver->GetBuilder();
            if (builder > 0 && builder < 7) {
                m_result = -5;
                CopyTargetText(m_failureName, receiverName);
                return false;
            }
            m_receiverNames.emplace_back(std::u16string_view(receiverName));
            m_receiverIds.push_back(static_cast<int>(receiver->GetId()));
        }
        if (separator == std::u16string::npos)
            break;
        begin = separator + 1;
    }
    if (m_receiverNames.empty() || m_receiverNames.size() >= 6) {
        m_result = -3;
        return false;
    }
    return true;
}

std::u16string ModifySendTempMailHtmlCommandHandler::GetReceiverNameList() const {
    // Current Cached.exe 0x004e0360.
    L2_GUARD_FUNCTION();
    std::u16string result;
    for (const std::u16string& name : m_receiverNames) {
        result += name;
        result += u";";
    }
    return result;
}

bool ModifySendTempMailHtmlCommandHandler::CheckSendMailLimit() {
    // Current Cached.exe 0x004e2308 omits Fetch.
    L2_GUARD_FUNCTION();
    int count = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&count);
    constexpr const wchar_t* statement = L"{Call lin_GetSendMailCount (? )}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    // Original: connection.ExecutePrepared(statement)
    if (!connection.ExecutePrepared(statement)) {
        // Previous format: "Cannot query the send-mail limit for character %d while sending a modified draft. This path does not fetch a row and continues with count %d. SQL: %ls"
        L2_LOG_ERROR(Log,
            "[mail] Cannot query the sending limit for character %d while sending a modified draft. This path does not fetch a row and continues with count %d. SQL: %ls",
            m_characterId, count, statement);
    }
    if (count > 9) {
        m_result = -2;
        return false;
    }
    return true;
}

bool ModifySendTempMailHtmlCommandHandler::CheckBlockList() {
    // Current Cached.exe 0x004e246c.
    L2_GUARD_FUNCTION();
    int blockCount = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    for (const int receiverId : m_receiverIds) {
        connection.Bind(&blockCount);
        constexpr const wchar_t* statement = L"{Call lin_GetBlockCount (?, ? )}";
        connection.Prepare(statement);
        connection.BindParameter(&m_characterId);
        connection.BindParameter(&receiverId);
        // Original: connection.ExecutePrepared(statement)
        // Original: connection.Fetch()
        const bool executed = connection.ExecutePrepared(statement);
        const bool fetched = connection.Fetch();
        if (!executed || !fetched) {
            // Previous format: "Cannot read mail blocking status for character %d and receiver %d while sending a modified draft (query %s, row %s). Continuing with block count %d. SQL: %ls"
            L2_LOG_ERROR(Log,
                "[mail] Cannot read blocking status for character %d and receiver %d while sending a modified draft (query %s, row %s). Continuing with block count %d. SQL: %ls",
                m_characterId, receiverId, executed ? "executed" : "failed",
                fetched ? "fetched" : "not fetched", blockCount, statement);
        }
        if (blockCount > 0) {
            m_result = -4;
            CUserSP receiver = g_userDb.GetUser(receiverId, true, false, runtime::process::CurrentDatabaseId());
            if (receiver) {
                CopyTargetText(m_failureName, receiver->GetCharName());
            }
            return false;
        }
        connection.UnBind();
    }
    return true;
}

void ModifySendTempMailHtmlCommandHandler::SendMail() {
    // Current Cached.exe 0x004e2134.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    const std::u16string receiverList = GetReceiverNameList();
    MaskSqlApostrophes(m_content.data());
    MaskSqlApostrophes(m_title.data());
    const std::u16string wideReceiverList(receiverList.c_str());
    const std::u16string title(m_title.data());
    const std::u16string content(m_content.data());
    constexpr const wchar_t* statement = L"{Call lin_ModifySendTempMail (?, ?, ?, ?, ? )}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&m_mailId);
    connection.BindParameter(wideReceiverList.c_str());
    connection.BindParameter(title.c_str());
    connection.BindParameter(content.c_str());
    // Original: connection.ExecutePrepared(statement)
    if (!connection.ExecutePrepared(statement)) {
        // Previous format: "Cannot send modified mail draft %d for character %d. The caller's success flag is unchanged. SQL: %ls"
        L2_LOG_ERROR(Log,
            "[mail] Cannot send modified draft %d for character %d; the caller's success flag is unchanged. SQL: %ls",
            m_mailId, m_characterId, statement);
    }
}

bool ModifySendTempMailHtmlCommandHandler::HandleInternal() {
    // Current Cached.exe 0x004e4558.
    L2_GUARD_FUNCTION();
    m_success = ParseReceiverList() && CheckSendMailLimit() && CheckBlockList();
    if (m_success)
        SendMail();
    return true;
}

void ModifySendTempMailHtmlCommandHandler::SendSuccessReply() {
    // Current Cached.exe 0x004e0464.
    L2_GUARD_FUNCTION();
    std::array<char, 0x5000> receiverPacket{};
    int receiverPacketLength = 0;
    for (const int receiverId : m_receiverIds) {
        const int assembled = ::Assemble(
            receiverPacket.data() + receiverPacketLength, static_cast<int>(receiverPacket.size()) - receiverPacketLength,
            "d", receiverId
        );
        if (assembled > 0)
            receiverPacketLength += assembled;
    }
    m_socket->Send(
        "chddb", kHtmlReplyOpcode, 12, m_userSocketId, static_cast<int>(m_receiverIds.size()), receiverPacketLength,
        receiverPacket.data()
    );

    CUserSP actor = g_userDb.GetUser(m_characterId, true, false, runtime::process::CurrentDatabaseId());
    if (actor) {
        const std::u16string receiverList = GetReceiverNameList();
        WriteMailAudit(0x352, *actor.get(), receiverList.c_str());
    }
}

void ModifySendTempMailHtmlCommandHandler::SendFailureReply() {
    // Current Cached.exe 0x004ddaf0.
    L2_GUARD_FUNCTION();
    std::array<char, 1024> parameterPacket{};
    const int parameterLength =
        m_listParameter.AssemblePacket(parameterPacket.data(), static_cast<int>(parameterPacket.size()));
    m_socket->Send(
        "chdddbSSSS", kHtmlReplyOpcode, 12, m_userSocketId, m_result, m_mailId, parameterLength, parameterPacket.data(),
        m_receivers.data(), m_title.data(), m_content.data(), m_failureName.data()
    );
}

bool ModifySendTempMailHtmlCommandHandler::SendReply() {
    // Current Cached.exe 0x004e2690.
    L2_GUARD_FUNCTION();
    if (m_success) {
        SendSuccessReply();
    } else {
        SendFailureReply();
    }
    return true;
}

bool DeleteMailHtmlCommandHandler::HandleInternal() {
    // Current Cached.exe 0x004e2744.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeleteMail (?, ?, ? )}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&m_mailId);
    int mailBoxType = m_listParameter.GetMailBoxType();
    connection.BindParameter(&mailBoxType);
    // connection.ExecutePrepared(statement)
    if (!connection.ExecutePrepared(statement)) {
        // Previous format: "Cannot delete mail %d for character %d from mailbox %d. The handler still returns success. SQL: %ls"
        L2_LOG_ERROR(Log, "[mail] Cannot delete mail %d for character %d from mailbox %d; the handler still returns success. SQL: %ls",
                     m_mailId, m_characterId, mailBoxType, statement);
    }
    return true;
}

bool DeleteMailHtmlCommandHandler::SendReply() {
    // Current Cached.exe 0x004ddd2c.
    L2_GUARD_FUNCTION();
    std::array<char, 1024> parameterPacket{};
    const int parameterLength =
        m_listParameter.AssemblePacket(parameterPacket.data(), static_cast<int>(parameterPacket.size()));
    m_socket->Send("chdb", kHtmlReplyOpcode, 13, m_userSocketId, parameterLength, parameterPacket.data());
    return true;
}

bool ArchiveMailHtmlCommandHandler::CheckArchiveLimit() {
    // Current Cached.exe 0x004e3188.
    L2_GUARD_FUNCTION();
    CountMail counts(m_characterId);
    counts.Execute();
    return counts.m_archivedCount < 100;
}

bool ArchiveMailHtmlCommandHandler::HandleInternal() {
    // Current Cached.exe 0x004e3684.
    L2_GUARD_FUNCTION();
    if (!CheckArchiveLimit()) {
        m_success = false;
        return true;
    }
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_ArchiveMail (?, ?, ? )}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&m_mailId);
    int mailBoxType = m_listParameter.GetMailBoxType();
    connection.BindParameter(&mailBoxType);
    // connection.ExecutePrepared(statement)
    if (!connection.ExecutePrepared(statement)) {
        // Previous format: "Cannot archive mail %d for character %d from mailbox %d. The success reply remains selected. SQL: %ls"
        L2_LOG_ERROR(Log, "[mail] Cannot archive mail %d for character %d from mailbox %d; the success reply remains selected. SQL: %ls",
                     m_mailId, m_characterId, mailBoxType, statement);
    }
    // Current target does not gate this flag on the SQL return value.
    m_success = true;
    return true;
}

void ArchiveMailHtmlCommandHandler::SendSuccessReply() {
    // Current Cached.exe 0x004ddf1c.
    L2_GUARD_FUNCTION();
    m_socket->Send("chdd", kHtmlReplyOpcode, 14, m_userSocketId, 0);
}

void ArchiveMailHtmlCommandHandler::SendFailureReply() {
    // Current Cached.exe 0x004ddfec.
    L2_GUARD_FUNCTION();
    std::array<char, 1024> parameterPacket{};
    const int parameterLength =
        m_listParameter.AssemblePacket(parameterPacket.data(), static_cast<int>(parameterPacket.size()));
    m_socket->Send("chdddb", kHtmlReplyOpcode, 14, m_userSocketId, 1, m_mailId, parameterLength, parameterPacket.data());
}

bool ArchiveMailHtmlCommandHandler::SendReply() {
    // Current Cached.exe 0x004debf8.
    L2_GUARD_FUNCTION();
    if (m_success) {
        SendSuccessReply();
    } else {
        SendFailureReply();
    }
    return true;
}

bool UnreadMailCountCommandHandler::HandleInternal() {
    // Current Cached.exe 0x004e28a8.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&m_unreadCount);
    constexpr const wchar_t* statement = L"{Call lin_GetUnreadMailCount (? )}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    // connection.ExecutePrepared(statement)
    // connection.Fetch()
    const bool executed = connection.ExecutePrepared(statement);
    const bool fetched = connection.Fetch();
    if (!executed || !fetched) {
        // Previous format: "Cannot load unread-mail count for character %d (query %s, row %s). Continuing with count %d. SQL: %ls"
        L2_LOG_ERROR(Log, "[mail] Cannot load unread count for character %d (query %s, row %s); continuing with count %d. SQL: %ls",
                     m_characterId, executed ? "executed" : "failed", fetched ? "fetched" : "unavailable",
                     m_unreadCount, statement);
    }
    return true;
}

bool UnreadMailCountCommandHandler::SendReply() {
    // Current Cached.exe 0x004de1d0 sends nothing for zero.
    L2_GUARD_FUNCTION();
    if (m_unreadCount > 0) {
        m_socket->Send("chdd", kHtmlReplyOpcode, 15, m_userSocketId, m_unreadCount);
    }
    return true;
}

bool SendPledgeMailHtmlCommandHandler::CheckPledge() {
    // Current Cached.exe 0x004decac.
    L2_GUARD_FUNCTION();
    std::wstring pledgeName = Utf16ToWide(m_pledgeName.data());
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeName.data(), false);
    return static_cast<bool>(pledge);
}

bool SendPledgeMailHtmlCommandHandler::CheckSendMailLimit() {
    // Current Cached.exe 0x004e2b1c.
    L2_GUARD_FUNCTION();
    int count = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&count);
    constexpr const wchar_t* statement = L"{Call lin_GetSendMailCount (? )}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    // connection.ExecutePrepared(statement)
    // connection.Fetch()
    const bool executed = connection.ExecutePrepared(statement);
    const bool fetched = connection.Fetch();
    if (!executed || !fetched) {
        // Previous format: "Cannot check the pledge-mail sending limit for character %d (query %s, row %s). Continuing with count %d. SQL: %ls"
        L2_LOG_ERROR(Log, "[mail] Cannot check the clan-mail sending limit for character %d (query %s, row %s); continuing with count %d. SQL: %ls",
                     m_characterId, executed ? "executed" : "failed", fetched ? "fetched" : "unavailable",
                     count, statement);
    }
    if (count > 9) {
        m_result = -2;
        return false;
    }
    return true;
}

void SendPledgeMailHtmlCommandHandler::SendMail() {
    // Current Cached.exe 0x004e29e4.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    MaskSqlApostrophes(m_content.data());
    MaskSqlApostrophes(m_title.data());
    ExecuteSixParameterMailStatement(
        connection, L"{Call lin_SendMail (?, ?, ?, ?, ?, ? )}", m_characterId, 1, 0, m_pledgeName.data(), m_title.data(),
        m_content.data()
    );
}

bool SendPledgeMailHtmlCommandHandler::HandleInternal() {
    // Current Cached.exe 0x004e323c.
    L2_GUARD_FUNCTION();
    m_success = CheckPledge() && CheckSendMailLimit();
    if (m_success)
        SendMail();
    return true;
}

void SendPledgeMailHtmlCommandHandler::SendSuccessReply() {
    // Current Cached.exe 0x004ded90.
    L2_GUARD_FUNCTION();
    m_socket->Send("chddS", kHtmlReplyOpcode, 16, m_userSocketId, 0, m_pledgeName.data());
    CUserSP actor = g_userDb.GetUser(m_characterId, true, false, runtime::process::CurrentDatabaseId());
    if (actor) {
        WriteMailAudit(0x352, *actor.get(), m_pledgeName.data());
    }
}

void SendPledgeMailHtmlCommandHandler::SendFailureReply() {
    // Current Cached.exe 0x004de3a4.
    L2_GUARD_FUNCTION();
    m_socket->Send("chddS", kHtmlReplyOpcode, 16, m_userSocketId, m_result, m_pledgeName.data());
}

bool SendPledgeMailHtmlCommandHandler::SendReply() {
    // Current Cached.exe 0x004df2f0.
    L2_GUARD_FUNCTION();
    if (m_success) {
        SendSuccessReply();
    } else {
        SendFailureReply();
    }
    return true;
}

bool DeleteFriendBatchHtmlCommandHandler::HandleInternal() {
    // Current Cached.exe 0x004e3408.
    L2_GUARD_FUNCTION();
    auto current = m_succeededIds.begin();
    while (current != m_succeededIds.end()) {
        if (!g_friendsDb.BreakFriendship(static_cast<unsigned int>(m_characterId), static_cast<unsigned int>(*current))) {
            m_failedIds.push_back(*current);
            current = m_succeededIds.erase(current);
        } else {
            ++current;
        }
    }
    return true;
}

bool DeleteFriendBatchHtmlCommandHandler::SendReply() {
    // Current Cached.exe 0x004df3a4.
    L2_GUARD_FUNCTION();
    std::array<char, 0x5000> succeededPacket{};
    int succeededLength = 0;
    for (const int id : m_succeededIds) {
        succeededLength += ::Assemble(
            succeededPacket.data() + succeededLength, static_cast<int>(succeededPacket.size()) - succeededLength, "d", id
        );
    }
    std::array<char, 0x5000> failedPacket{};
    int failedLength = 0;
    for (const int id : m_failedIds) {
        failedLength +=
            ::Assemble(failedPacket.data() + failedLength, static_cast<int>(failedPacket.size()) - failedLength, "d", id);
    }
    m_socket->Send(
        "chddbdb", kHtmlReplyOpcode, 17, m_userSocketId, static_cast<int>(m_succeededIds.size()), succeededLength,
        succeededPacket.data(), static_cast<int>(m_failedIds.size()), failedLength, failedPacket.data()
    );
    return true;
}

bool DeleteBlockBatchHtmlCommandHandler::HandleInternal() {
    // Current Cached.exe 0x004e2c8c.
    L2_GUARD_FUNCTION();
    for (std::size_t index = 0; index < m_results.size(); ++index) {
        int ignoredResult = 0;
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Bind(&ignoredResult);
        const std::u16string name(runtime::CheckedVectorAt(m_names,index).c_str());
        constexpr const wchar_t* statement = L"{Call lin_DeleteBlockList (?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&m_characterId);
        connection.BindParameter(name.c_str());
        // if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
        const bool executed = connection.ExecutePrepared(statement);
        if (!executed || !connection.Fetch()) {
            // Previous format: "Cannot remove block-list entry %zu for character %d: %s. Returning result 0 for this entry. SQL: %ls"
            L2_LOG_ERROR(Log, "[mail] Cannot remove block-list entry %zu for character %d: %s; returning result 0 for this entry. SQL: %ls",
                         index, m_characterId, executed ? "no result row fetched" : "query execution failed", statement);
            runtime::CheckedVectorAt(m_results,index) = 0;
        }
    }
    return true;
}

bool DeleteBlockBatchHtmlCommandHandler::SendReply() {
    // Current Cached.exe 0x004deff4.
    L2_GUARD_FUNCTION();
    std::array<char, 0x5000> resultPacket{};
    int resultLength = 0;
    for (std::size_t index = 0; index < m_results.size(); ++index) {
        resultLength += ::Assemble(
            resultPacket.data() + resultLength, static_cast<int>(resultPacket.size()) - resultLength, "dS", runtime::CheckedVectorAt(m_results,index),
            runtime::CheckedVectorAt(m_names,index).c_str()
        );
    }
    m_socket->Send(
        "chddb", kHtmlReplyOpcode, 18, m_userSocketId, static_cast<int>(m_results.size()), resultLength, resultPacket.data()
    );
    return true;
}

GetPledgeAnnounceHtmlCommandHandler::GetPledgeAnnounceHtmlCommandHandler(CSocket* socket, const unsigned char* packet)
    // Current Cached.exe 0x005334dc: base construction, derived vtable,
    // three zeroed request fields and a null CPledgeAnnounceSP in that order.
    :
    CHtmlCommandHandler(socket, packet), m_userSocketId(0), m_pledgeDbId(0), m_viewType(0) {}

GetPledgeAnnounceHtmlCommandHandler::
    // Current Cached.exe 0x00533548 releases the kept smart pointer
    // through its virtual Release slot before restoring the base handler.
    ~GetPledgeAnnounceHtmlCommandHandler() = default;

bool GetPledgeAnnounceHtmlCommandHandler::DisassemblePacket() {
    // Current Cached.exe 0x004de484.
    L2_GUARD_FUNCTION();
    ::Disassemble(m_packet, "ddd", &m_userSocketId, &m_pledgeDbId, &m_viewType);
    return true;
}

bool GetPledgeAnnounceHtmlCommandHandler::HandleInternal() {
    // Current Cached.exe 0x004e4630. A database-loaded miss is kept by
    // this request object only. The shipping old path does not publish it.
    L2_GUARD_FUNCTION();
    m_pledgeAnnounce = CPledgeAnnounceRegistry::inst().Get(static_cast<unsigned int>(m_pledgeDbId));
    if (!m_pledgeAnnounce) {
        CPledgeAnnounceDBHandlerImpl databaseHandler;
        m_pledgeAnnounce = databaseHandler.Load(static_cast<unsigned int>(m_pledgeDbId));
    }
    return true;
}

bool GetPledgeAnnounceHtmlCommandHandler::SendReply() {
    // Current Cached.exe 0x004df1c0.
    L2_GUARD_FUNCTION();
    const int doesShow = m_pledgeAnnounce && m_pledgeAnnounce->DoesShow() ? 1 : 0;
    const char16_t* const content = m_pledgeAnnounce ? m_pledgeAnnounce->GetContent().c_str() : u"";
    m_socket->Send("chddddS", 0xc6, 0x1b, m_userSocketId, doesShow, m_pledgeDbId, m_viewType, content);
    return true;
}

// Current Cached.exe 0x0052a38c.
SetPledgeAnnounceHtmlCommandHandler::SetPledgeAnnounceHtmlCommandHandler(CSocket* socket, const unsigned char* packet) :
    CHtmlCommandHandler(socket, packet), m_userSocketId(0), m_pledgeDbId(0), m_showFlag(0), m_content{}, m_success(false) {}

bool SetPledgeAnnounceHtmlCommandHandler::DisassemblePacket() {
    // Current Cached.exe 0x004de54c. 0x1772 is the target byte capacity,
    // including the UTF-16 terminator.
    L2_GUARD_FUNCTION();
    ::Disassemble(m_packet, "dddS", &m_userSocketId, &m_pledgeDbId, &m_showFlag, 0x1772, m_content.data());
    return true;
}

bool SetPledgeAnnounceHtmlCommandHandler::HandleInternal() {
    // Current Cached.exe 0x004e4780.
    L2_GUARD_FUNCTION();
    CPledgeAnnounce* const rawPledgeAnnounce =
        new CPledgeAnnounce(static_cast<unsigned int>(m_pledgeDbId), m_showFlag == 1, m_content.data());
    CPledgeAnnounceSP pledgeAnnounce(rawPledgeAnnounce, ObjectRefType::FunctionChain);
    L2_RELEASE(rawPledgeAnnounce, ObjectRefType::Anonymous);

    CPledgeAnnounceDBHandlerImpl databaseHandler;
    if (databaseHandler.Set(pledgeAnnounce)) {
        m_success = CPledgeAnnounceRegistry::inst().Register(pledgeAnnounce);
    }
    return true;
}

bool SetPledgeAnnounceHtmlCommandHandler::SendReply() {
    // Current Cached.exe 0x004de62c.
    L2_GUARD_FUNCTION();
    m_socket->Send("chdddSc", 0xc6, 0x1c, m_userSocketId, m_showFlag, m_pledgeDbId, m_content.data(), m_success ? 1 : 0);
    return true;
}

SwitchPledgeAnnounceShowFlagHtmlCommandHandler::SwitchPledgeAnnounceShowFlagHtmlCommandHandler(
    CSocket* socket, const unsigned char* packet
) :
    CHtmlCommandHandler(socket, packet), m_userSocketId(0), m_pledgeDbId(0), m_showFlag(0), m_success(false) {}

bool SwitchPledgeAnnounceShowFlagHtmlCommandHandler::DisassemblePacket() {
    // Current Cached.exe 0x004de724.
    L2_GUARD_FUNCTION();
    ::Disassemble(m_packet, "ddd", &m_userSocketId, &m_pledgeDbId, &m_showFlag);
    return true;
}

bool SwitchPledgeAnnounceShowFlagHtmlCommandHandler::HandleInternal() {
    // Current Cached.exe 0x004e4918.
    L2_GUARD_FUNCTION();
    CPledgeAnnounceDBHandlerImpl databaseHandler;
    if (databaseHandler.SwitchFlag(static_cast<unsigned int>(m_pledgeDbId), m_showFlag == 1)) {
        m_success = CPledgeAnnounceRegistry::inst().SwitchFlag(static_cast<unsigned int>(m_pledgeDbId), m_showFlag == 1);
    }
    return true;
}

bool SwitchPledgeAnnounceShowFlagHtmlCommandHandler::SendReply() {
    // Current Cached.exe 0x004e4a00.
    L2_GUARD_FUNCTION();
    CPledgeAnnounceSP pledgeAnnounce = CPledgeAnnounceRegistry::inst().Get(static_cast<unsigned int>(m_pledgeDbId));
    const char16_t* const content = pledgeAnnounce ? pledgeAnnounce->GetContent().c_str() : u"";
    m_socket->Send("chdddSc", 0xc6, 0x1c, m_userSocketId, m_showFlag, m_pledgeDbId, content, m_success ? 1 : 0);
    return true;
}
