#include "HtmlBuilder.h"
#include "Agit.h"
#include "Alliance.h"
#include "Castle.h"
#include "ClanBbs.h"
#include "DB.h"
#include "Friends.h"
#include "ParseHtml.h"
#include "Pledge.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Relay.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "User.h"
#include "UserSocket.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Config.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/PledgeAnnounceRegistry.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace {

SharedUtilsWideChar* TargetWide(const std::u16string& value) {
    static_assert(sizeof(SharedUtilsWideChar) == sizeof(char16_t));
    return const_cast<SharedUtilsWideChar*>(reinterpret_cast<const SharedUtilsWideChar*>(value.c_str()));
}

template <std::size_t Capacity>
std::u16string MakeTargetString(const std::array<SharedUtilsWideChar, Capacity>& value) {
    const std::size_t length = std::char_traits<SharedUtilsWideChar>::length(value.data());
    return std::u16string(std::u16string_view(reinterpret_cast<const char16_t*>(value.data()), length));
}

template <std::size_t Capacity>
void SecureTargetCopy(std::array<SharedUtilsWideChar, Capacity>& destination, const std::u16string& source) {
    destination.fill(0);
    if (source.size() >= Capacity) {
        // The target's wcscpy_s clears the destination on a range failure.
        return;
    }
    std::memcpy(destination.data(), source.data(), source.size() * sizeof(char16_t));
}

int PageOrDefault(CMailListParameter& parameter) {
    const int page = parameter.GetPage();
    return page < 1 ? 1 : page;
}

} // namespace

CHtmlBuilder::CHtmlBuilder(CDBSocket* const dbSocket, const unsigned char* const packet) :
    m_dbSocket(dbSocket), m_packet(packet), m_remainPacket(nullptr) {
    // Current L2Server 0x0078ce30. CHtmlGenerator's default constructor
    // leaves its non-owning file-name lane untouched.
}

CHtmlBuilder::~CHtmlBuilder() {
    // Current deleting-dtor body 0x0069f4d4 clears the three non-owning
    // packet/socket lanes before CHtmlGenerator is destroyed.
    m_dbSocket = nullptr;
    m_packet = nullptr;
    m_remainPacket = nullptr;
}

void CHtmlBuilder::SetTemplateFileName(const char16_t* const templateFileName) {
    // Current L2Server 0x00781d18.
    L2_GUARD_FUNCTION();
    if (templateFileName != nullptr) {
        m_htmlGenerator.m_htmlTemplateFileName = templateFileName;
        m_htmlGenerator.ReadTemplet();
        m_htmlGenerator.ParseTemplet();
    }
}

User* CHtmlBuilder::GetUser(const int userServerId) {
    // Current L2Server 0x007832f8.
    L2_GUARD_FUNCTION();
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(userServerId);
    if (creature != nullptr && creature->IsPC()) {
        return creature->CastUser();
    }
    return nullptr;
}

CUserSocket* CHtmlBuilder::GetUserSocket(const int userServerId) {
    // Current L2Server 0x0078571c.
    L2_GUARD_FUNCTION();
    User* const user = GetUser(userServerId);
    return user != nullptr ? user->GetSocket() : nullptr;
}

std::u16string CHtmlBuilder::BuildHtmlDataPacket(
    const int htmlType,
    SharedUtilsWideChar* const receiverNameList,
    SharedUtilsWideChar* const title,
    SharedUtilsWideChar* const content,
    const int convertOption
) {
    // Current L2Server 0x007833c8. The eleven leading separator lanes and
    // the seemingly extra zero fields are part of the client contract.
    L2_GUARD_FUNCTION();
    const std::u16string convertedContent = CParseHTML::ConvertStr(content, convertOption);
    const std::u16string convertedTitle = CParseHTML::ConvertStr(title, convertOption);
    const std::u16string convertedReceivers = CParseHTML::ConvertStr(receiverNameList, convertOption);
    const std::wstring hostContent = Utf16ToWide(convertedContent);
    const std::wstring hostTitle = Utf16ToWide(convertedTitle);
    const std::wstring hostReceivers = Utf16ToWide(convertedReceivers);
    return FormatString(
        u"%d%c0 %c0 %c0 %c0 %c0 %c0 %c0 %c0 %c %c0 %c%s%c%s%c%s%c %c %c0 %c 0%c %c", htmlType, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, hostReceivers.c_str(), 8, hostTitle.c_str(), 8, hostContent.c_str(), 8, 8, 8, 8, 8, 8
    );
}

void CHtmlBuilder::BuildTreeLinkHtml(SharedUtilsWideChar* const treeLink) {
    // Current L2Server 0x007835f0.
    L2_GUARD_FUNCTION();
    const std::wstring hostTreeLink = Utf16ToWide(treeLink);
    m_htmlGenerator.AddValue(
        u"TREE", FormatString(u"<a action=\"bypass _bbsinit_0\"> HOME </a>&nbsp;&gt;%s", hostTreeLink.c_str())
    );
}

void CHtmlBuilder::SendSystemMsg(const int userServerId, const int messageId, const int value) {
    // Current L2Server 0x007857d0.
    CUserSocket* const socket = GetUserSocket(userServerId);
    if (socket != nullptr) {
        socket->SendSystemMessage_d(messageId, value);
    }
}

void CHtmlBuilder::SendSystemMsg(const int userServerId, const int messageId) {
    CUserSocket* const socket = GetUserSocket(userServerId);
    if (socket != nullptr) {
        socket->SendSystemMessage(messageId);
    }
}

void CHtmlBuilder::SendSystemMsg(
    const int userServerId, const int messageId, const eSMsgType type, SharedUtilsWideChar* const value
) {
    // Current L2Server 0x00785804 literally uses the narrow-string `s` lane
    // for this UTF-16 pointer. Preserve that target oddity as compiled.
    CUserSocket* const socket = GetUserSocket(userServerId);
    if (socket != nullptr) {
        socket->Send(
            "chccs", 0x62, static_cast<short>(messageId), 1, static_cast<char>(type), reinterpret_cast<const char*>(value)
        );
    }
}

void ListMailHtmlBuilder::DisassemblePacket() {
    // Current L2Server 0x00781dd0.
    L2_GUARD_FUNCTION();
    m_remainPacket = ::Disassemble(
        m_packet, "dddddd", &m_userServerId, &m_incommingMailCount, &m_sentMailCount, &m_archivedMailCount, &m_tempMailCount,
        &m_totalCount
    );
    m_remainPacket = m_listParam.DisassemblePacket(m_remainPacket);
    m_remainPacket = ::Disassemble(m_remainPacket, "d", &m_count);
}

const char16_t* ListMailHtmlBuilder::GetTemplateFileName() {
    // Current L2Server 0x00781ee4.
    return u"bbs_mail_list.htm";
}

int ListMailHtmlBuilder::GetTotalPage() {
    // Current L2Server 0x00781fd0 calls GetRowsPerPage twice.
    L2_GUARD_FUNCTION();
    const int divisorForRemainder = m_listParam.GetRowsPerPage();
    const int totalCount = m_totalCount;
    const int divisorForQuotient = m_listParam.GetRowsPerPage();
    return totalCount / divisorForQuotient + (totalCount % divisorForRemainder != 0 ? 1 : 0);
}

void ListMailHtmlBuilder::BuildTreeLink() {
    // Current L2Server 0x007836e0.
    L2_GUARD_FUNCTION();
    const std::u16string link = FormatString(
        u"<a action=\"bypass _%s_%d_1_0__\">%s</a>", u"maillist", m_listParam.GetMailBoxType(), m_listParam.GetMailBoxName()
    );
    BuildTreeLinkHtml(TargetWide(link));
}

void ListMailHtmlBuilder::BuildMailBoxLink() {
    // Current L2Server 0x007837e4.
    L2_GUARD_FUNCTION();
    m_htmlGenerator.AddValue(u"incomming_mail_no", FormatString(u"%d", m_incommingMailCount));
    m_htmlGenerator.AddValue(u"sent_mail_no", FormatString(u"%d", m_sentMailCount));
    m_htmlGenerator.AddValue(u"archived_mail_no", FormatString(u"%d", m_archivedMailCount));
    m_htmlGenerator.AddValue(u"temp_mail_no", FormatString(u"%d", m_tempMailCount));
}

void ListMailHtmlBuilder::BuildMailListHeader() {
    // Current L2Server 0x00781eec.
    L2_GUARD_FUNCTION();
    const int mailBoxType = m_listParam.GetMailBoxType();
    m_htmlGenerator.AddValue(u"writer_header", mailBoxType == 1 || mailBoxType == 3 ? u"&$909;" : u"&$911;");
    m_htmlGenerator.AddValue(u"post_date_header", u"&$910;");
}

void ListMailHtmlBuilder::BuildMailListLink() {
    // Current L2Server 0x0078c0ec.
    L2_GUARD_FUNCTION();
    for (int index = 0; index < m_count; ++index) {
        int mailId = 0;
        int senderCharId = 0;
        int sendDate = 0;
        std::array<SharedUtilsWideChar, 250> writer{};
        std::array<SharedUtilsWideChar, 200> title{};
        m_remainPacket =
            ::Disassemble(m_remainPacket, "ddSSd", &mailId, &senderCharId, 500, writer.data(), 400, title.data(), &sendDate);
        static_cast<void>(senderCharId);

        CLocalHtmlGenerator local(m_htmlGenerator.GetTemplet(2));

        const std::u16string convertedTitle = CParseHTML::ConvertStr(title.data(), 4);
        const std::u16string hostConvertedTitle(convertedTitle);
        const std::u16string shortTitle = AbbreviateString(hostConvertedTitle, 30, u"...");
        const std::u16string hostShortTitle(shortTitle);
        local.AddValue(u"title", hostShortTitle);

        const std::u16string shortWriter = AbbreviateString(SanitizeUtf16(writer.data()), 8, u"...");
        const std::u16string hostShortWriter(shortWriter);
        local.AddValue(u"writer", hostShortWriter);

        const wchar_t* const command = m_listParam.GetMailBoxType() == 3 ? L"mailmodifyform" : L"mailread";
        const std::wstring hostKeyword = Utf16ToWide(m_listParam.GetKeyword());
        local.AddValue(
            u"action",
            FormatString(
                u"bypass _%s_%d_%d_%d_%d_%s_", command, mailId, m_listParam.GetMailBoxType(), PageOrDefault(m_listParam),
                m_listParam.GetSearchTarget(), hostKeyword.c_str()
            )
        );

        const std::u16string formattedDate = CParseHTML::MakeFormatedDate(sendDate, 3);
        const std::u16string hostDate(formattedDate);
        local.AddValue(u"post_date", hostDate);
        m_htmlGenerator.AddValue(u"MAIL_LIST", local);
    }
}

void ListMailHtmlBuilder::BuildWriteButton() {
    // Current L2Server 0x007839b0.
    L2_GUARD_FUNCTION();
    const std::wstring hostKeyword = Utf16ToWide(m_listParam.GetKeyword());
    m_htmlGenerator.AddValue(
        u"ACTION_WRITEBUTTON",
        FormatString(
            u"bypass _%s_%d_%d_%d_%s", u"mailwriteform", m_listParam.GetMailBoxType(), PageOrDefault(m_listParam),
            m_listParam.GetSearchTarget(), hostKeyword.c_str()
        )
    );
    m_htmlGenerator.AddValue(u"mailbox_type", FormatString(u"%d_", m_listParam.GetMailBoxType()));
}

void ListMailHtmlBuilder::BuildPageIndexLink() {
    // Current L2Server 0x00789a48.
    L2_GUARD_FUNCTION();
    const std::wstring hostKeyword = Utf16ToWide(m_listParam.GetKeyword());
    const int rawPage = m_listParam.GetPage();
    if (rawPage > 1) {
        m_htmlGenerator.AddValue(
            u"ACTION_GO_LEFT",
            FormatString(
                u"bypass _%s_%d_%d_%d_%s_", u"maillist", m_listParam.GetMailBoxType(), rawPage - 1,
                m_listParam.GetSearchTarget(), hostKeyword.c_str()
            )
        );
    }

    const int currentPage = PageOrDefault(m_listParam);
    int pageIndex = currentPage - 10;
    if (currentPage - 10 < 2) {
        pageIndex = 1;
    }
    while (pageIndex < PageOrDefault(m_listParam)) {
        CLocalHtmlGenerator local(m_htmlGenerator.GetTemplet(4));
        local.AddValue(
            u"action_link",
            FormatString(
                u"bypass _%s_%d_%d_%d_%s_", u"maillist", m_listParam.GetMailBoxType(), pageIndex,
                m_listParam.GetSearchTarget(), hostKeyword.c_str()
            )
        );
        local.AddValue(u"action_name", FormatString(u"%d", pageIndex));
        m_htmlGenerator.AddValue(u"GO_LIST", local);
        ++pageIndex;
    }

    m_htmlGenerator.AddValue(u"NPAGE", FormatString(u"%d", PageOrDefault(m_listParam)));

    pageIndex = PageOrDefault(m_listParam);
    for (;;) {
        ++pageIndex;
        const int loopCurrentPage = PageOrDefault(m_listParam);
        const int candidateEnd = loopCurrentPage + 10;
        const int totalPage = GetTotalPage();
        const int endPage = totalPage <= candidateEnd ? totalPage : candidateEnd;
        if (endPage < pageIndex) {
            break;
        }
        CLocalHtmlGenerator local(m_htmlGenerator.GetTemplet(4));
        local.AddValue(
            u"action_link",
            FormatString(
                u"bypass _%s_%d_%d_%d_%s_", u"maillist", m_listParam.GetMailBoxType(), pageIndex,
                m_listParam.GetSearchTarget(), hostKeyword.c_str()
            )
        );
        local.AddValue(u"action_name", FormatString(u"%d", pageIndex));
        m_htmlGenerator.AddValue(u"GO_LIST2", local);
    }

    const int rightCurrentPage = PageOrDefault(m_listParam);
    if (rightCurrentPage < GetTotalPage()) {
        m_htmlGenerator.AddValue(
            u"ACTION_GO_RIGHT",
            FormatString(
                u"bypass _%s_%d_%d_%d_%s_", u"maillist", m_listParam.GetMailBoxType(), rightCurrentPage + 1,
                m_listParam.GetSearchTarget(), hostKeyword.c_str()
            )
        );
    }
}

void ListMailHtmlBuilder::BuildHtmlInternal() {
    // Current L2Server 0x0078c548.
    L2_GUARD_FUNCTION();
    BuildTreeLink();
    BuildMailBoxLink();
    BuildMailListHeader();
    BuildMailListLink();
    BuildWriteButton();
    BuildPageIndexLink();
}

void ListMailHtmlBuilder::SendHtml() {
    // Current L2Server 0x00785864.
    L2_GUARD_FUNCTION();
    CHtmlSender sender(GetUserSocket(m_userServerId));
    std::u16string html = m_htmlGenerator.Generate();
    sender.SendHtmlWith3Packet(html);
}

void ReadMailHtmlBuilder::DisassemblePacket() {
    // Current L2Server 0x00787aec.
    L2_GUARD_FUNCTION();
    m_remainPacket = ::Disassemble(m_packet, "d", &m_userServerId);
    m_remainPacket = m_listParam.DisassemblePacket(m_remainPacket);
    m_remainPacket = ::Disassemble(
        m_remainPacket, "ddSSSSdd", &m_mailId, &m_senderCharId, 100, m_senderCharName.data(), 500, m_receiverNameList.data(),
        400, m_title.data(), 8000, m_content.data(), &m_sendDate, &m_mailType
    );

    std::u16string escapedTitle = ReplaceStr(MakeTargetString(m_title), u"<", u"&lt;");
    escapedTitle = ReplaceStr(escapedTitle, u">", u"&gt;");
    SecureTargetCopy(m_title, escapedTitle);

    std::u16string escapedContent = ReplaceStr(MakeTargetString(m_content), u"<", u"&lt;");
    escapedContent = ReplaceStr(escapedContent, u">", u"&gt;");
    SecureTargetCopy(m_content, escapedContent);
}

const char16_t* ReadMailHtmlBuilder::GetTemplateFileName() {
    // Current L2Server 0x007820a4.
    return u"bbs_mail_read.htm";
}

void ReadMailHtmlBuilder::Build1Packet() {
    // Current L2Server 0x00789ec8.
    L2_GUARD_FUNCTION();
    m_htmlGenerator.AddValue(u"MULTI_1", FormatString(u"1%c", 8));

    const std::u16string tree = FormatString(
        u"<a action=\"bypass _%s_%d_1_0__\">%s</a>", u"maillist", m_listParam.GetMailBoxType(), m_listParam.GetMailBoxName()
    );
    BuildTreeLinkHtml(TargetWide(tree));

    m_htmlGenerator.AddValue(u"writer", SanitizeUtf16(m_senderCharName.data()));
    m_htmlGenerator.AddValue(u"char_name", SanitizeUtf16(m_receiverNameList.data()));
    m_htmlGenerator.AddValue(u"title", SanitizeUtf16(m_title.data()));

    const std::u16string hostPostDate(CParseHTML::MakeFormatedDate(m_sendDate, 3));
    m_htmlGenerator.AddValue(u"post_date", hostPostDate);
    const std::int32_t deleteDate = static_cast<std::int32_t>(static_cast<std::uint32_t>(m_sendDate) + 0x76a700U);
    const std::u16string hostDeleteDate(CParseHTML::MakeFormatedDate(deleteDate, 3));
    m_htmlGenerator.AddValue(u"del_date", hostDeleteDate);

    const int mailBoxType = m_listParam.GetMailBoxType();
    const int page = PageOrDefault(m_listParam);
    const int searchTarget = m_listParam.GetSearchTarget();
    const std::wstring hostKeyword = Utf16ToWide(m_listParam.GetKeyword());
    m_htmlGenerator.AddValue(
        u"GOTO_LIST_LINK",
        FormatString(u"bypass _%s_%d_%d_%d_%s_", u"maillist", mailBoxType, page, searchTarget, hostKeyword.c_str())
    );
    m_htmlGenerator.AddValue(
        u"REPLY_BUTTON_LINK",
        FormatString(
            u"bypass _%s_%d_%d_%d_%d_%s_", u"mailreplyform", m_mailId, mailBoxType, page, searchTarget, hostKeyword.c_str()
        )
    );
    m_htmlGenerator.AddValue(
        u"FORWARD_BUTTON_LINK",
        FormatString(
            u"bypass _%s_%d_%d_%d_%d_%s_", u"mailforwardform", m_mailId, mailBoxType, page, searchTarget, hostKeyword.c_str()
        )
    );
    m_htmlGenerator.AddValue(
        u"DELETE_BUTTON_LINK", FormatString(u"bypass _%s_%d_%d_", u"maildelete", m_mailId, mailBoxType)
    );

    if (mailBoxType == 0 || mailBoxType == 1) {
        CLocalHtmlGenerator archiveButton(m_htmlGenerator.GetTemplet(2));
        archiveButton.AddValue(
            u"ARCHIVE_BUTTON_LINK", FormatString(u"bypass _%s_%d_%d_", u"mailarchive", m_mailId, mailBoxType)
        );
        std::u16string generated = archiveButton.Generate();
        m_htmlGenerator.AddValue(u"ARCHIVE_BUTTON", generated);
    }

    m_htmlGenerator.AddValue(
        u"NEW_BUTTON_LINK",
        FormatString(u"bypass _%s_%d_%d_%d_%s_", u"mailwriteform", mailBoxType, page, searchTarget, hostKeyword.c_str())
    );
}

void ReadMailHtmlBuilder::Build2Packet() {
    // Current L2Server 0x00787f00.
    L2_GUARD_FUNCTION();
    m_htmlData = BuildHtmlDataPacket(2, m_receiverNameList.data(), m_title.data(), m_content.data(), 4);
}

void ReadMailHtmlBuilder::BuildHtmlInternal() {
    // Current L2Server 0x0078c614.
    L2_GUARD_FUNCTION();
    Build1Packet();
    Build2Packet();
}

void ReadMailHtmlBuilder::SendHtml() {
    // Current L2Server 0x00785944.
    L2_GUARD_FUNCTION();
    CHtmlSender sender(GetUserSocket(m_userServerId));
    std::u16string html = m_htmlGenerator.Generate();
    sender.SendHtml(html);
    sender.SendHtml(m_htmlData);
    std::u16string third = FormatString(u"3%c", 8);
    sender.SendHtml(third);
    std::u16string fourth = FormatString(u"4%c", 8);
    sender.SendHtml(fourth);
}

void SendMailHtmlBuilder::DisassemblePacket() {
    // Current L2Server 0x007820ac.
    L2_GUARD_FUNCTION();
    m_remainPacket = ::Disassemble(m_packet, "dd", &m_userServerId, &m_sentMailCount);
}

const char16_t* SendMailHtmlBuilder::GetTemplateFileName() {
    // Current L2Server 0x007829c4.
    return u"bbs_mail_write.htm";
}

void SendMailHtmlBuilder::Build1001Packet() {
    // Current L2Server 0x00783b18.
    L2_GUARD_FUNCTION();
    int firstValue = 0;
    int secondValue = 0;
    m_remainPacket = m_listParam.DisassemblePacket(m_remainPacket);
    m_remainPacket = ::Disassemble(m_remainPacket, "dd", &firstValue, &secondValue);

    const std::u16string tree = FormatString(
        u"<a action=\"bypass _%s_%d_1_0__\">%s</a>", u"maillist", m_listParam.GetMailBoxType(), m_listParam.GetMailBoxName()
    );
    BuildTreeLinkHtml(TargetWide(tree));
    m_htmlGenerator.AddValue(u"MULTI_1", FormatString(u"1001%c", 8));

    const int mailBoxType = m_listParam.GetMailBoxType();
    const int page = PageOrDefault(m_listParam);
    const int searchTarget = m_listParam.GetSearchTarget();
    const std::u16string hostKeywordUtf16 = SanitizeUtf16(m_listParam.GetKeyword());
    const std::wstring hostKeyword = Utf16ToWide(hostKeywordUtf16);
    m_htmlGenerator.AddValue(
        u"SEND_ACTION",
        FormatString(
            u"_%s_%d_%d_%d_%d_%d_%s_", u"mailsend", firstValue, secondValue, mailBoxType, page, searchTarget,
            hostKeyword.c_str()
        )
    );
    m_htmlGenerator.AddValue(
        u"SAVE_ACTION",
        FormatString(
            u"_%s_%d_%d_%d_%d_%d_%s_", u"mailsave", firstValue, secondValue, mailBoxType, page, searchTarget,
            hostKeyword.c_str()
        )
    );
    m_htmlGenerator.AddValue(u"mailbox_type", FormatString(u"%d", mailBoxType));
    m_htmlGenerator.AddValue(u"page", FormatString(u"%d", page));
    m_htmlGenerator.AddValue(u"search_target", FormatString(u"%d", searchTarget));
    m_htmlGenerator.AddValue(u"keyword", hostKeywordUtf16);
}

void SendMailHtmlBuilder::Build1002Packet() {
    // Current L2Server 0x00788008.
    L2_GUARD_FUNCTION();
    std::array<SharedUtilsWideChar, 250> receivers{};
    std::array<SharedUtilsWideChar, 200> title{};
    std::array<SharedUtilsWideChar, 3000> content{};
    m_remainPacket = ::Disassemble(m_remainPacket, "SSS", 500, receivers.data(), 400, title.data(), 6000, content.data());
    m_htmlData = BuildHtmlDataPacket(1002, receivers.data(), title.data(), content.data(), 3);
}

void SendMailHtmlBuilder::BuildHtmlInternal() {
    // Current L2Server 0x007881c0.
    L2_GUARD_FUNCTION();
    if (m_sentMailCount < 1) {
        Build1001Packet();
        Build1002Packet();
    }
}

void SendMailHtmlBuilder::SendMsg() {
    // Current L2Server 0x00786f40.
    L2_GUARD_FUNCTION();
    SendSystemMsg(m_userServerId, SystemMessageId::COMMUNITY_MAIL_SENT);
    for (int index = 0; index < m_sentMailCount; ++index) {
        int targetDatabaseId = 0;
        m_remainPacket = ::Disassemble(m_remainPacket, "d", &targetDatabaseId);
        CSPointer<User> target = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, targetDatabaseId));
        if (target && (target->m_data->m_nBuilder < 1 || target->m_data->m_nBuilder > 6)) {
            const int targetServerId = static_cast<int>(target->GetServerId());
            SendSystemMsg(targetServerId, SystemMessageId::MAIL_ARRIVED);
            CUserSocket* const targetSocket = GetUserSocket(static_cast<int>(target->GetServerId()));
            if (targetSocket != nullptr) {
                targetSocket->Send("ch", 0xfe, 0x2e);
            }
        }
    }
}

void SendMailHtmlBuilder::SendHtml() {
    // Current L2Server 0x00788274.
    L2_GUARD_FUNCTION();
    if (m_sentMailCount > 0) {
        SendMsg();
        CMailListParameter parameter;
        parameter.SetMailBoxType(1);
        db.RequestMailList(GetUser(m_userServerId), parameter);
        return;
    }

    CHtmlSender sender(GetUserSocket(m_userServerId));
    std::u16string html = m_htmlGenerator.Generate();
    sender.SendHtml(html);
    sender.SendHtml(m_htmlData);

    switch (m_sentMailCount) {
        case -6:
            SendSystemMsg(m_userServerId, SystemMessageId::ENTER_POST_TITLE);
            break;
        case -5: {
            std::array<SharedUtilsWideChar, 250> name;
            m_remainPacket = ::Disassemble(m_remainPacket, "S", 500, name.data());
            SendSystemMsg(
                m_userServerId, SystemMessageId::CANNOT_SEND_MAIL_TO_GM, static_cast<eSMsgType>(0x0c), name.data()
            );
            break;
        }
        case -4: {
            std::array<SharedUtilsWideChar, 250> name;
            m_remainPacket = ::Disassemble(m_remainPacket, "S", 500, name.data());
            SendSystemMsg(
                m_userServerId, SystemMessageId::CANNOT_SEND_MAIL_RECIPIENT_BLOCKED_SENDER, static_cast<eSMsgType>(0x0c),
                name.data()
            );
            break;
        }
        case -3:
            SendSystemMsg(m_userServerId, SystemMessageId::MAIL_RECIPIENT_LIMIT_REACHED);
            break;
        case -2:
            SendSystemMsg(m_userServerId, SystemMessageId::DAILY_MAIL_SEND_LIMIT_REACHED);
            break;
        case -1: {
            std::array<SharedUtilsWideChar, 250> name;
            m_remainPacket = ::Disassemble(m_remainPacket, "S", 500, name.data());
            SendSystemMsg(m_userServerId, SystemMessageId::PLAYER_DOES_NOT_EXIST, static_cast<eSMsgType>(0x0c), name.data());
            break;
        }
        default:
            SendSystemMsg(m_userServerId, SystemMessageId::MAIL_SEND_FAILED);
            break;
    }
}

void SaveMailHtmlBuilder::DisassemblePacket() {
    // Current L2Server 0x00782168.
    L2_GUARD_FUNCTION();
    m_remainPacket = ::Disassemble(m_packet, "dd", &m_userServerId, &m_errorType);
}

const char16_t* SaveMailHtmlBuilder::GetTemplateFileName() {
    // Current L2Server 0x007829c4 (folded with SendMailHtmlBuilder).
    return u"bbs_mail_write.htm";
}

bool SaveMailHtmlBuilder::IsMailSaveSuccess() {
    // Current L2Server 0x00782224.
    L2_GUARD_FUNCTION();
    return m_errorType == 0;
}

void SaveMailHtmlBuilder::Build1001Packet() {
    // Current L2Server 0x00783ecc.
    L2_GUARD_FUNCTION();
    int firstValue = 0;
    int secondValue = 0;
    m_remainPacket = m_listParam.DisassemblePacket(m_remainPacket);
    m_remainPacket = ::Disassemble(m_remainPacket, "dd", &firstValue, &secondValue);

    const std::u16string tree = FormatString(
        u"<a action=\"bypass _%s_%d_1_0__\">%s</a>", u"maillist", m_listParam.GetMailBoxType(), m_listParam.GetMailBoxName()
    );
    BuildTreeLinkHtml(TargetWide(tree));
    m_htmlGenerator.AddValue(u"MULTI_1", FormatString(u"1001%c", 8));

    const int mailBoxType = m_listParam.GetMailBoxType();
    const int page = PageOrDefault(m_listParam);
    const int searchTarget = m_listParam.GetSearchTarget();
    const std::u16string hostKeywordUtf16 = SanitizeUtf16(m_listParam.GetKeyword());
    const std::wstring hostKeyword = Utf16ToWide(hostKeywordUtf16);
    m_htmlGenerator.AddValue(
        u"SEND_ACTION",
        FormatString(
            u"_%s_%d_%d_%d_%d_%d_%s_", u"mailsend", firstValue, secondValue, mailBoxType, page, searchTarget,
            hostKeyword.c_str()
        )
    );
    m_htmlGenerator.AddValue(
        u"SAVE_ACTION",
        FormatString(
            u"_%s_%d_%d_%d_%d_%d_%s_", u"mailsave", firstValue, secondValue, mailBoxType, page, searchTarget,
            hostKeyword.c_str()
        )
    );
    m_htmlGenerator.AddValue(u"mailbox_type", FormatString(u"%d", mailBoxType));
    m_htmlGenerator.AddValue(u"page", FormatString(u"%d", page));
    m_htmlGenerator.AddValue(u"search_target", FormatString(u"%d", searchTarget));
    m_htmlGenerator.AddValue(u"keyword", hostKeywordUtf16);
}

void SaveMailHtmlBuilder::Build1002Packet() {
    // Current L2Server 0x00788534.
    L2_GUARD_FUNCTION();
    std::array<SharedUtilsWideChar, 250> receivers{};
    std::array<SharedUtilsWideChar, 200> title{};
    std::array<SharedUtilsWideChar, 3000> content{};
    m_remainPacket = ::Disassemble(m_remainPacket, "SSS", 500, receivers.data(), 400, title.data(), 6000, content.data());
    m_htmlData = BuildHtmlDataPacket(1002, receivers.data(), title.data(), content.data(), 3);
}

void SaveMailHtmlBuilder::BuildHtmlInternal() {
    // Current L2Server 0x007886ec.
    L2_GUARD_FUNCTION();
    if (!IsMailSaveSuccess()) {
        Build1001Packet();
        Build1002Packet();
    }
}

void SaveMailHtmlBuilder::SendHtml() {
    // Current L2Server 0x00785abc.
    L2_GUARD_FUNCTION();
    if (!IsMailSaveSuccess()) {
        SendSystemMsg(m_userServerId, SystemMessageId::DRAFT_MAIL_STORAGE_FULL);
        CHtmlSender sender(GetUserSocket(m_userServerId));
        std::u16string html = m_htmlGenerator.Generate();
        sender.SendHtml(html);
        sender.SendHtml(m_htmlData);
        return;
    }

    SendSystemMsg(m_userServerId, SystemMessageId::MAIL_SAVED_TO_DRAFTS);
    CMailListParameter parameter;
    parameter.SetMailBoxType(3);
    db.RequestMailList(GetUser(m_userServerId), parameter);
}

void ModifyTempMailFormHtmlBuilder::DisassemblePacket() {
    // Current L2Server 0x00782500.
    L2_GUARD_FUNCTION();
    m_remainPacket = ::Disassemble(m_packet, "d", &m_userServerId);
    m_remainPacket = m_listParam.DisassemblePacket(m_remainPacket);
    m_remainPacket = ::Disassemble(
        m_remainPacket, "dSSS", &m_mailId, 500, m_receiverNameList.data(), 400, m_title.data(), 8000, m_content.data()
    );
}

const char16_t* ModifyTempMailFormHtmlBuilder::GetTemplateFileName() {
    // Current L2Server 0x007829c4.
    return u"bbs_mail_write.htm";
}

void ModifyTempMailFormHtmlBuilder::Build1001Packet() {
    // Current L2Server 0x0078a554.
    L2_GUARD_FUNCTION();
    m_htmlGenerator.AddValue(u"MULTI_1", FormatString(u"1001%c", 8));

    const int mailBoxType = m_listParam.GetMailBoxType();
    const std::u16string tree =
        FormatString(u"<a action=\"bypass _%s_%d_1_0__\">%s</a>", u"maillist", mailBoxType, m_listParam.GetMailBoxName());
    BuildTreeLinkHtml(TargetWide(tree));

    const int page = PageOrDefault(m_listParam);
    const int searchTarget = m_listParam.GetSearchTarget();
    const std::u16string hostKeywordUtf16 = SanitizeUtf16(m_listParam.GetKeyword());
    const std::wstring hostKeyword = Utf16ToWide(hostKeywordUtf16);
    m_htmlGenerator.AddValue(
        u"SEND_ACTION",
        FormatString(
            u"_%s_%d_%d_%d_%d_%s_", u"mailmodifysend", m_mailId, mailBoxType, page, searchTarget, hostKeyword.c_str()
        )
    );
    m_htmlGenerator.AddValue(u"SAVE_ACTION", FormatString(u"_%s_%d_", u"mailmodify", m_mailId));

    CLocalHtmlGenerator deleteButton(m_htmlGenerator.GetTemplet(2));
    deleteButton.AddValue(u"DELETE_BUTTON_LINK", FormatString(u"bypass _%s_%d_%d_", u"maildelete", m_mailId, mailBoxType));
    std::u16string generatedDeleteButton = deleteButton.Generate();
    m_htmlGenerator.AddValue(u"DELETE_BUTTON", generatedDeleteButton);

    m_htmlGenerator.AddValue(u"mailbox_type", FormatString(u"%d", mailBoxType));
    m_htmlGenerator.AddValue(u"page", FormatString(u"%d", page));
    m_htmlGenerator.AddValue(u"search_target", FormatString(u"%d", searchTarget));
    m_htmlGenerator.AddValue(u"keyword", hostKeywordUtf16);
}

void ModifyTempMailFormHtmlBuilder::Build1002Packet() {
    // Current L2Server 0x0078899c.
    L2_GUARD_FUNCTION();
    m_htmlData = BuildHtmlDataPacket(1002, m_receiverNameList.data(), m_title.data(), m_content.data(), 3);
}

void ModifyTempMailFormHtmlBuilder::BuildHtmlInternal() {
    // Current L2Server 0x0078c6c0.
    L2_GUARD_FUNCTION();
    Build1001Packet();
    Build1002Packet();
}

void ModifyTempMailFormHtmlBuilder::SendHtml() {
    // Current L2Server 0x00786044.
    L2_GUARD_FUNCTION();
    CHtmlSender sender(GetUserSocket(m_userServerId));
    std::u16string html = m_htmlGenerator.Generate();
    sender.SendHtml(html);
    sender.SendHtml(m_htmlData);
}

void ReplyMailFormHtmlBuilder::DisassemblePacket() {
    // Current L2Server 0x00782628.
    L2_GUARD_FUNCTION();
    m_remainPacket = ::Disassemble(m_packet, "d", &m_userServerId);
    m_remainPacket = m_listParam.DisassemblePacket(m_remainPacket);
    m_remainPacket = ::Disassemble(
        m_remainPacket, "dSSSS", &m_mailId, 100, m_senderCharName.data(), 500, m_receiverNameList.data(), 400,
        m_title.data(), 8000, m_content.data()
    );
}

const char16_t* ReplyMailFormHtmlBuilder::GetTemplateFileName() {
    // Current L2Server 0x007829c4.
    return u"bbs_mail_write.htm";
}

void ReplyMailFormHtmlBuilder::Build1001Packet() {
    // Current L2Server 0x00784970.
    L2_GUARD_FUNCTION();
    m_htmlGenerator.AddValue(u"MULTI_1", FormatString(u"1001%c", 8));

    const int mailBoxType = m_listParam.GetMailBoxType();
    const std::u16string tree =
        FormatString(u"<a action=\"bypass _%s_%d_1_0__\">%s</a>", u"maillist", mailBoxType, m_listParam.GetMailBoxName());
    BuildTreeLinkHtml(TargetWide(tree));

    const int page = PageOrDefault(m_listParam);
    const int searchTarget = m_listParam.GetSearchTarget();
    const std::u16string hostKeywordUtf16 = SanitizeUtf16(m_listParam.GetKeyword());
    const std::wstring hostKeyword = Utf16ToWide(hostKeywordUtf16);
    m_htmlGenerator.AddValue(
        u"SEND_ACTION",
        FormatString(
            u"_%s_%d_%d_%d_%d_%d_%s_", u"mailsend", 2, m_mailId, mailBoxType, page, searchTarget, hostKeyword.c_str()
        )
    );
    m_htmlGenerator.AddValue(
        u"SAVE_ACTION",
        FormatString(
            u"_%s_%d_%d_%d_%d_%d_%s_", u"mailsave", 2, m_mailId, mailBoxType, page, searchTarget, hostKeyword.c_str()
        )
    );
    m_htmlGenerator.AddValue(u"mailbox_type", FormatString(u"%d", mailBoxType));
    m_htmlGenerator.AddValue(u"page", FormatString(u"%d", page));
    m_htmlGenerator.AddValue(u"search_target", FormatString(u"%d", searchTarget));
    m_htmlGenerator.AddValue(u"keyword", hostKeywordUtf16);
}

void ReplyMailFormHtmlBuilder::Build1002Packet() {
    // Current L2Server 0x00788aa4. The current reply body intentionally
    // excludes the original content from the generated header block.
    L2_GUARD_FUNCTION();
    const std::wstring hostSender = Utf16ToWide(m_senderCharName.data());
    const std::wstring hostReceivers = Utf16ToWide(m_receiverNameList.data());
    const std::wstring hostTitle = Utf16ToWide(m_title.data());
    const std::u16string content = FormatString(
        u"Sender : %s\nReceiver : %s\nTitle : %s", hostSender.c_str(), hostReceivers.c_str(), hostTitle.c_str()
    );
    const std::u16string title = FormatString(u"Re: %s", hostTitle.c_str());
    m_htmlData = BuildHtmlDataPacket(1002, m_senderCharName.data(), TargetWide(title), TargetWide(content), 3);
}

void ReplyMailFormHtmlBuilder::BuildHtmlInternal() {
    // Current L2Server 0x007892c8.
    L2_GUARD_FUNCTION();
    Build1001Packet();
    Build1002Packet();
}

void ReplyMailFormHtmlBuilder::SendHtml() {
    // Current L2Server 0x00786134.
    L2_GUARD_FUNCTION();
    CHtmlSender sender(GetUserSocket(m_userServerId));
    std::u16string html = m_htmlGenerator.Generate();
    sender.SendHtml(html);
    sender.SendHtml(m_htmlData);
}

void ForwardMailFormHtmlBuilder::DisassemblePacket() {
    // Current L2Server 0x00782760.
    L2_GUARD_FUNCTION();
    m_remainPacket = ::Disassemble(m_packet, "d", &m_userServerId);
    m_remainPacket = m_listParam.DisassemblePacket(m_remainPacket);
    m_remainPacket = ::Disassemble(
        m_remainPacket, "dSSSS", &m_mailId, 100, m_senderCharName.data(), 500, m_receiverNameList.data(), 400,
        m_title.data(), 8000, m_content.data()
    );
}

const char16_t* ForwardMailFormHtmlBuilder::GetTemplateFileName() {
    // Current L2Server 0x007829c4.
    return u"bbs_mail_write.htm";
}

void ForwardMailFormHtmlBuilder::Build1001Packet() {
    // Current L2Server 0x00784d08.
    L2_GUARD_FUNCTION();
    m_htmlGenerator.AddValue(u"MULTI_1", FormatString(u"1001%c", 8));

    const int mailBoxType = m_listParam.GetMailBoxType();
    const std::u16string tree =
        FormatString(u"<a action=\"bypass _%s_%d_1_0__\">%s</a>", u"maillist", mailBoxType, m_listParam.GetMailBoxName());
    BuildTreeLinkHtml(TargetWide(tree));

    const int page = PageOrDefault(m_listParam);
    const int searchTarget = m_listParam.GetSearchTarget();
    const std::u16string hostKeywordUtf16 = SanitizeUtf16(m_listParam.GetKeyword());
    const std::wstring hostKeyword = Utf16ToWide(hostKeywordUtf16);
    m_htmlGenerator.AddValue(
        u"SEND_ACTION",
        FormatString(
            u"_%s_%d_%d_%d_%d_%d_%s_", u"mailsend", 3, m_mailId, mailBoxType, page, searchTarget, hostKeyword.c_str()
        )
    );
    m_htmlGenerator.AddValue(
        u"SAVE_ACTION",
        FormatString(
            u"_%s_%d_%d_%d_%d_%d_%s_", u"mailsave", 3, m_mailId, mailBoxType, page, searchTarget, hostKeyword.c_str()
        )
    );
    m_htmlGenerator.AddValue(u"mailbox_type", FormatString(u"%d", mailBoxType));
    m_htmlGenerator.AddValue(u"page", FormatString(u"%d", page));
    m_htmlGenerator.AddValue(u"search_target", FormatString(u"%d", searchTarget));
    m_htmlGenerator.AddValue(u"keyword", hostKeywordUtf16);
}

void ForwardMailFormHtmlBuilder::Build1002Packet() {
    // Current L2Server 0x00788c60.
    L2_GUARD_FUNCTION();
    const std::wstring hostSender = Utf16ToWide(m_senderCharName.data());
    const std::wstring hostReceivers = Utf16ToWide(m_receiverNameList.data());
    const std::wstring hostTitle = Utf16ToWide(m_title.data());
    const std::wstring hostContent = Utf16ToWide(m_content.data());
    const std::u16string content = FormatString(
        u"Sender : %s\nReceiver : %s\nTitle : %s\nContent : %s", hostSender.c_str(), hostReceivers.c_str(),
        hostTitle.c_str(), hostContent.c_str()
    );
    const std::u16string title = FormatString(u"Fwd: %s", hostTitle.c_str());
    m_htmlData = BuildHtmlDataPacket(1002, m_senderCharName.data(), TargetWide(title), TargetWide(content), 3);
}

void ForwardMailFormHtmlBuilder::BuildHtmlInternal() {
    // Current L2Server 0x00789374.
    L2_GUARD_FUNCTION();
    Build1001Packet();
    Build1002Packet();
}

void ForwardMailFormHtmlBuilder::SendHtml() {
    // Current L2Server 0x00786224.
    L2_GUARD_FUNCTION();
    CHtmlSender sender(GetUserSocket(m_userServerId));
    std::u16string html = m_htmlGenerator.Generate();
    sender.SendHtml(html);
    sender.SendHtml(m_htmlData);
}

void ModifyTempMailHtmlBuilder::DisassemblePacket() {
    // Current L2Server 0x00782898 discards the returned cursor.
    L2_GUARD_FUNCTION();
    static_cast<void>(::Disassemble(m_packet, "d", &m_userServerId));
}

void ModifyTempMailHtmlBuilder::BuildHtmlInternal() {
    // Current L2Server 0x007850a0.
    L2_GUARD_FUNCTION();
    CMailListParameter parameter;
    parameter.SetMailBoxType(3);
    db.RequestMailList(GetUser(m_userServerId), parameter);
}

void ModifyTempMailHtmlBuilder::SendHtml() {
    // Current L2Server 0x00782948 is an intentional no-op.
    L2_GUARD_FUNCTION();
}

void ModifySendTempMailHtmlBuilder::DisassemblePacket() {
    // Current L2Server 0x007829cc.
    L2_GUARD_FUNCTION();
    m_remainPacket = ::Disassemble(m_packet, "dd", &m_userServerId, &m_sentMailCount);
}

const char16_t* ModifySendTempMailHtmlBuilder::GetTemplateFileName() {
    // Current L2Server 0x007829c4.
    return u"bbs_mail_write.htm";
}

bool ModifySendTempMailHtmlBuilder::IsMailSendingSuccess() { return m_sentMailCount > 0; }

void ModifySendTempMailHtmlBuilder::Build1001Packet() {
    // Current L2Server 0x0078a988.
    L2_GUARD_FUNCTION();
    int mailId = 0;
    m_remainPacket = ::Disassemble(m_remainPacket, "d", &mailId);
    m_remainPacket = m_listParam.DisassemblePacket(m_remainPacket);

    m_htmlGenerator.AddValue(u"MULTI_1", FormatString(u"1001%c", 8));
    const int mailBoxType = m_listParam.GetMailBoxType();
    const std::u16string tree =
        FormatString(u"<a action=\"bypass _%s_%d_1_0__\">%s</a>", u"maillist", mailBoxType, m_listParam.GetMailBoxName());
    BuildTreeLinkHtml(TargetWide(tree));

    const int page = PageOrDefault(m_listParam);
    const int searchTarget = m_listParam.GetSearchTarget();
    const std::u16string hostKeywordUtf16 = SanitizeUtf16(m_listParam.GetKeyword());
    const std::wstring hostKeyword = Utf16ToWide(hostKeywordUtf16);
    m_htmlGenerator.AddValue(
        u"SEND_ACTION",
        FormatString(u"_%s_%d_%d_%d_%d_%s_", u"mailmodifysend", mailId, mailBoxType, page, searchTarget, hostKeyword.c_str())
    );
    m_htmlGenerator.AddValue(u"SAVE_ACTION", FormatString(u"_%s_%d", u"mailmodify", mailId));

    CLocalHtmlGenerator deleteButton(m_htmlGenerator.GetTemplet(2));
    deleteButton.AddValue(u"DELETE_BUTTON_LINK", FormatString(u"bypass _%s_%d_%d_", u"maildelete", mailId, mailBoxType));
    std::u16string generatedDeleteButton = deleteButton.Generate();
    m_htmlGenerator.AddValue(u"DELETE_BUTTON", generatedDeleteButton);

    m_htmlGenerator.AddValue(u"mailbox_type", FormatString(u"%d", mailBoxType));
    m_htmlGenerator.AddValue(u"page", FormatString(u"%d", page));
    m_htmlGenerator.AddValue(u"search_target", FormatString(u"%d", searchTarget));
    m_htmlGenerator.AddValue(u"keyword", hostKeywordUtf16);
}

void ModifySendTempMailHtmlBuilder::Build1002Packet() {
    // Current L2Server 0x00788e2c clears all three local buffers.
    L2_GUARD_FUNCTION();
    std::array<SharedUtilsWideChar, 250> receivers{};
    std::array<SharedUtilsWideChar, 200> title{};
    std::array<SharedUtilsWideChar, 3000> content{};
    m_remainPacket = ::Disassemble(m_remainPacket, "SSS", 500, receivers.data(), 400, title.data(), 6000, content.data());
    m_htmlData = BuildHtmlDataPacket(1002, receivers.data(), title.data(), content.data(), 3);
}

void ModifySendTempMailHtmlBuilder::BuildHtmlInternal() {
    // Current L2Server 0x0078c76c.
    L2_GUARD_FUNCTION();
    if (!IsMailSendingSuccess()) {
        Build1001Packet();
        Build1002Packet();
    }
}

void ModifySendTempMailHtmlBuilder::SendMsg() {
    // Current L2Server 0x00787124 is normalized-identical to
    // SendMailHtmlBuilder::SendMsg at 0x00786f40.
    L2_GUARD_FUNCTION();
    SendSystemMsg(m_userServerId, SystemMessageId::COMMUNITY_MAIL_SENT);
    for (int index = 0; index < m_sentMailCount; ++index) {
        int targetDatabaseId = 0;
        m_remainPacket = ::Disassemble(m_remainPacket, "d", &targetDatabaseId);
        CSPointer<User> target = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, targetDatabaseId));
        if (target && (target->m_data->m_nBuilder < 1 || target->m_data->m_nBuilder > 6)) {
            const int targetServerId = static_cast<int>(target->GetServerId());
            SendSystemMsg(targetServerId, SystemMessageId::MAIL_ARRIVED);
            CUserSocket* const targetSocket = GetUserSocket(static_cast<int>(target->GetServerId()));
            if (targetSocket != nullptr) {
                targetSocket->Send("ch", 0xfe, 0x2e);
            }
        }
    }
}

void ModifySendTempMailHtmlBuilder::SendHtml() {
    // Current L2Server 0x00787308 is normalized-identical to
    // SendMailHtmlBuilder::SendHtml at 0x00788274.
    L2_GUARD_FUNCTION();
    if (IsMailSendingSuccess()) {
        SendMsg();
        CMailListParameter parameter;
        parameter.SetMailBoxType(1);
        db.RequestMailList(GetUser(m_userServerId), parameter);
        return;
    }

    CHtmlSender sender(GetUserSocket(m_userServerId));
    std::u16string html = m_htmlGenerator.Generate();
    sender.SendHtml(html);
    sender.SendHtml(m_htmlData);

    switch (m_sentMailCount) {
        case -6:
            SendSystemMsg(m_userServerId, SystemMessageId::ENTER_POST_TITLE);
            break;
        case -5: {
            std::array<SharedUtilsWideChar, 250> name;
            m_remainPacket = ::Disassemble(m_remainPacket, "S", 500, name.data());
            SendSystemMsg(
                m_userServerId, SystemMessageId::CANNOT_SEND_MAIL_TO_GM, static_cast<eSMsgType>(0x0c), name.data()
            );
            break;
        }
        case -4: {
            std::array<SharedUtilsWideChar, 250> name;
            m_remainPacket = ::Disassemble(m_remainPacket, "S", 500, name.data());
            SendSystemMsg(
                m_userServerId, SystemMessageId::CANNOT_SEND_MAIL_RECIPIENT_BLOCKED_SENDER, static_cast<eSMsgType>(0x0c),
                name.data()
            );
            break;
        }
        case -3:
            SendSystemMsg(m_userServerId, SystemMessageId::MAIL_RECIPIENT_LIMIT_REACHED);
            break;
        case -2:
            SendSystemMsg(m_userServerId, SystemMessageId::DAILY_MAIL_SEND_LIMIT_REACHED);
            break;
        case -1: {
            std::array<SharedUtilsWideChar, 250> name;
            m_remainPacket = ::Disassemble(m_remainPacket, "S", 500, name.data());
            SendSystemMsg(m_userServerId, SystemMessageId::PLAYER_DOES_NOT_EXIST, static_cast<eSMsgType>(0x0c), name.data());
            break;
        }
        default:
            SendSystemMsg(m_userServerId, SystemMessageId::MAIL_SEND_FAILED);
            break;
    }
}

void DeleteMailHtmlBuilder::DisassemblePacket() {
    // Current L2Server 0x00782a88 leaves m_remainPacket at the
    // base-constructor null value after the list parameter consumes its cursor.
    L2_GUARD_FUNCTION();
    const unsigned char* const packet = ::Disassemble(m_packet, "d", &m_userServerId);
    static_cast<void>(m_listParam.DisassemblePacket(packet));
}

void DeleteMailHtmlBuilder::BuildHtmlInternal() {
    // Current L2Server 0x00782b48 is an intentional no-op.
    L2_GUARD_FUNCTION();
}

void DeleteMailHtmlBuilder::SendHtml() {
    // Current L2Server 0x00785178.
    L2_GUARD_FUNCTION();
    db.RequestMailList(GetUser(m_userServerId), m_listParam);
}

void ArchiveMailHtmlBuilder::DisassemblePacket() {
    // Current L2Server 0x00782bc4.
    L2_GUARD_FUNCTION();
    m_remainPacket = ::Disassemble(m_packet, "dd", &m_userServerId, &m_errorType);
}

void ArchiveMailHtmlBuilder::BuildHtmlInternal() {
    // Current L2Server 0x00782c80 is an intentional no-op.
    L2_GUARD_FUNCTION();
}

bool ArchiveMailHtmlBuilder::IsMailArchivingSuccess() { return m_errorType == 0; }

void ArchiveMailHtmlBuilder::SendHtml() {
    // Current L2Server 0x00786314.
    L2_GUARD_FUNCTION();
    if (IsMailArchivingSuccess()) {
        CMailListParameter parameter;
        parameter.SetMailBoxType(2);
        db.RequestMailList(GetUser(m_userServerId), parameter);
        return;
    }

    SendSystemMsg(m_userServerId, SystemMessageId::MAIL_STORAGE_FULL);
    CMailListParameter parameter;
    int mailId;
    m_remainPacket = ::Disassemble(m_remainPacket, "d", &mailId);
    m_remainPacket = parameter.DisassemblePacket(m_remainPacket);
    db.RequestReadMail(GetUser(m_userServerId), mailId, parameter);
}

void UnreadMailCountMsgSender::DisassemblePacket() {
    // Current L2Server 0x00782cfc.
    L2_GUARD_FUNCTION();
    m_remainPacket = ::Disassemble(m_packet, "dd", &m_userServerId, &m_unreadMailCount);
}

void UnreadMailCountMsgSender::BuildHtmlInternal() {
    // Current L2Server 0x00782db8 is an intentional no-op.
    L2_GUARD_FUNCTION();
}

void UnreadMailCountMsgSender::SendHtml() {
    // Current L2Server 0x00786468.
    L2_GUARD_FUNCTION();
    if (m_unreadMailCount > 0) {
        SendSystemMsg(m_userServerId, SystemMessageId::UNREAD_MAIL_COUNT, m_unreadMailCount);
    }
}

void SendPledgeMailHandler::DisassemblePacket() {
    // Current L2Server 0x00782ef4.
    L2_GUARD_FUNCTION();
    m_remainPacket = ::Disassemble(m_packet, "ddS", &m_userServerId, &m_errorType, 100, m_pledgeName.data());
}

void SendPledgeMailHandler::BuildHtmlInternal() {
    // Current L2Server 0x00782fc4 is an intentional no-op.
    L2_GUARD_FUNCTION();
}

int SendPledgeMailHandler::GetPledgeId() {
    // Current L2Server 0x00785234.
    L2_GUARD_FUNCTION();
    CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, m_pledgeName.data());
    return pledge ? static_cast<int>(pledge->GetDbId()) : 0;
}

void SendPledgeMailHandler::SendMsg() {
    // Current L2Server 0x00786748.
    L2_GUARD_FUNCTION();
    SendSystemMsg(m_userServerId, SystemMessageId::COMMUNITY_MAIL_SENT);
    User* const user = GetUser(m_userServerId);
    if (user != nullptr) {
        CPledge* const pledge = user->GetPledge();
        if (pledge != nullptr) {
            pledge->MailArrived();
        }
    }
}

void SendPledgeMailHandler::SendHtml() {
    // Current L2Server 0x00786aec.
    L2_GUARD_FUNCTION();
    if (m_errorType == 0) {
        SendMsg();
    } else if (m_errorType == -2) {
        SendSystemMsg(m_userServerId, SystemMessageId::DAILY_MAIL_SEND_LIMIT_REACHED);
    } else {
        SendSystemMsg(m_userServerId, SystemMessageId::MAIL_SEND_FAILED);
    }

    CUserSocket* const socket = GetUserSocket(m_userServerId);
    if (socket != nullptr) {
        const std::u16string command = FormatString(u"%s_%d", u"_clbbsclan", GetPledgeId());
        ClanBBS(socket, TargetWide(command));
    }
}

const char16_t* WriteMailFormHtmlBuilder::GetTemplateFileName() {
    // Current L2Server 0x007829c4.
    return u"bbs_mail_write.htm";
}

void WriteMailFormHtmlBuilder::DisassemblePacket() {
    // Current L2Server 0x007822b8.
    L2_GUARD_FUNCTION();
    m_remainPacket = ::Disassemble(m_packet, "d", &m_userServerId);
    m_remainPacket = m_listParam.DisassemblePacket(m_remainPacket);
}

void WriteMailFormHtmlBuilder::BuildHtmlInternal() {
    // Current L2Server 0x00784280.
    L2_GUARD_FUNCTION();
    const std::u16string tree = FormatString(
        u"<a action=\"bypass _%s_%d_1_0__\">%s</a>", u"maillist", m_listParam.GetMailBoxType(), m_listParam.GetMailBoxName()
    );
    BuildTreeLinkHtml(TargetWide(tree));

    const std::wstring sendKeyword = Utf16ToWide(m_listParam.GetKeyword());
    m_htmlGenerator.AddValue(
        u"SEND_ACTION",
        FormatString(
            u"_%s_%d_%d_%d_%d_%d_%s_", u"mailsend", 0, 0, m_listParam.GetMailBoxType(), PageOrDefault(m_listParam),
            m_listParam.GetSearchTarget(), sendKeyword.c_str()
        )
    );

    const std::wstring saveKeyword = Utf16ToWide(m_listParam.GetKeyword());
    m_htmlGenerator.AddValue(
        u"SAVE_ACTION",
        FormatString(
            u"_%s_%d_%d_%d_%d_%d_%s_", u"mailsave", 0, 0, m_listParam.GetMailBoxType(), PageOrDefault(m_listParam),
            m_listParam.GetSearchTarget(), saveKeyword.c_str()
        )
    );
    m_htmlGenerator.AddValue(u"mailbox_type", FormatString(u"%d", m_listParam.GetMailBoxType()));
    m_htmlGenerator.AddValue(u"page", FormatString(u"%d", PageOrDefault(m_listParam)));
    m_htmlGenerator.AddValue(u"search_target", FormatString(u"%d", m_listParam.GetSearchTarget()));
    const std::u16string valueKeyword(m_listParam.GetKeyword());
    m_htmlGenerator.AddValue(u"keyword", valueKeyword);
}

void WriteMailFormHtmlBuilder::SendHtml() {
    // Current L2Server 0x00785c38.
    L2_GUARD_FUNCTION();
    CHtmlSender sender(GetUserSocket(m_userServerId));
    std::u16string html = m_htmlGenerator.Generate();
    sender.SendHtml(html);
}

const char16_t* WritePledgeMailFormHtmlBuilder::GetTemplateFileName() {
    // Current L2Server 0x00782380.
    return u"bbs_pledge_mail_write.htm";
}

void WritePledgeMailFormHtmlBuilder::DisassemblePacket() {
    // Current L2Server 0x00782388.
    L2_GUARD_FUNCTION();
    m_remainPacket = ::Disassemble(m_packet, "dd", &m_userServerId, &m_pledgeId);
}

const char16_t* WritePledgeMailFormHtmlBuilder::GetPledgeName() {
    // Current L2Server 0x007845c4.
    L2_GUARD_FUNCTION();
    User* const user = GetUser(m_userServerId);
    if (user == nullptr) {
        return u"";
    }
    CPledge* const pledge = user->GetPledge();
    return pledge != nullptr ? pledge->GetPledgeName() : u"";
}

void WritePledgeMailFormHtmlBuilder::BuildHtmlInternal() {
    // Current L2Server 0x00785d18.
    L2_GUARD_FUNCTION();
    m_htmlGenerator.AddValue(u"SEND_ACTION", FormatString(u"_%s_%d_", u"mailpledgesend", m_pledgeId));
    m_htmlGenerator.AddValue(u"pledge_name", std::u16string(GetPledgeName()));
    m_htmlGenerator.AddValue(u"pledge_id", FormatString(u"%d", m_pledgeId));
}

void WritePledgeMailFormHtmlBuilder::SendHtml() {
    // Current L2Server 0x00785e74.
    L2_GUARD_FUNCTION();
    CHtmlSender sender(GetUserSocket(m_userServerId));
    std::u16string html = m_htmlGenerator.Generate();
    sender.SendHtml(html);
}

const char16_t* WriteFriendMailFormHtmlBuilder::GetTemplateFileName() {
    // Current L2Server shares 0x007829c4 with WriteMailFormHtmlBuilder.
    return u"bbs_mail_write.htm";
}

void WriteFriendMailFormHtmlBuilder::DisassemblePacket() {
    // Current L2Server 0x00782444.
    L2_GUARD_FUNCTION();
    m_remainPacket = ::Disassemble(m_packet, "dd", &m_userServerId, &m_friendCount);
}

void WriteFriendMailFormHtmlBuilder::Build1001Packet() {
    // Current L2Server 0x00784694. The SAVE action's two zero lanes are
    // in different positions from the SEND action.
    CMailListParameter parameter;
    const std::u16string tree = FormatString(u"<a action=\"bypass _%s_0_\">&$904;</a> > &$915;", u"friendlist");
    BuildTreeLinkHtml(TargetWide(tree));
    m_htmlGenerator.AddValue(u"MULTI_1", FormatString(u"1001%c", 8));

    const int sendPage = PageOrDefault(parameter);
    const std::wstring sendKeyword = Utf16ToWide(parameter.GetKeyword());
    m_htmlGenerator.AddValue(
        u"SEND_ACTION",
        FormatString(
            u"_%s_%d_0_%d_%d_%d_%s_", u"mailsend", 0, parameter.GetMailBoxType(), sendPage, parameter.GetSearchTarget(),
            sendKeyword.c_str()
        )
    );

    const int savePage = PageOrDefault(parameter);
    const std::wstring saveKeyword = Utf16ToWide(parameter.GetKeyword());
    m_htmlGenerator.AddValue(
        u"SAVE_ACTION",
        FormatString(
            u"_%s_%d_0_%d_%d_%d_%s_", u"mailsave", parameter.GetMailBoxType(), savePage, 0, parameter.GetSearchTarget(),
            saveKeyword.c_str()
        )
    );
    m_htmlGenerator.AddValue(u"mailbox_type", FormatString(u"%d", parameter.GetMailBoxType()));
    m_htmlGenerator.AddValue(u"page", FormatString(u"%d", PageOrDefault(parameter)));
    m_htmlGenerator.AddValue(u"search_target", FormatString(u"%d", parameter.GetSearchTarget()));
    const std::u16string valueKeyword(parameter.GetKeyword());
    m_htmlGenerator.AddValue(u"keyword", valueKeyword);
}

std::u16string WriteFriendMailFormHtmlBuilder::GetFriendNameList() {
    // Current L2Server 0x007887a4. Packet ids are consumed only after both
    // the User and CFriends owners are available.
    std::u16string result;
    result = std::u16string(u"");
    int friendDbId = 0;
    User* const user = GetUser(m_userServerId);
    if (user != nullptr) {
        CSPointer<CFriends> friends = user->GetFriends();
        if (friends && m_friendCount > 0) {
            for (int index = 0; index < m_friendCount; ++index) {
                m_remainPacket = ::Disassemble(m_remainPacket, "d", &friendDbId);
                if (friends->HasFriend(static_cast<unsigned int>(friendDbId))) {
                    std::array<char16_t, 50> friendName{};
                    friends->GetFriendName(static_cast<unsigned int>(friendDbId), friendName.data(), 50);
                    result += FormatString(u"%s;", friendName.data());
                }
            }
        }
    }
    return result;
}

void WriteFriendMailFormHtmlBuilder::Build1002Packet() {
    // Current L2Server 0x007891f4.
    std::u16string friendNameList = GetFriendNameList();
    std::u16string empty;
    m_htmlData = BuildHtmlDataPacket(1002, TargetWide(friendNameList), TargetWide(empty), TargetWide(empty), 3);
}

void WriteFriendMailFormHtmlBuilder::BuildHtmlInternal() {
    // Current L2Server 0x00789420.
    L2_GUARD_FUNCTION();
    Build1001Packet();
    Build1002Packet();
}

void WriteFriendMailFormHtmlBuilder::SendHtml() {
    // Current L2Server 0x00785f54 reuses one sender/socket for both packets.
    L2_GUARD_FUNCTION();
    CHtmlSender sender(GetUserSocket(m_userServerId));
    std::u16string html = m_htmlGenerator.Generate();
    sender.SendHtml(html);
    sender.SendHtml(m_htmlData);
}

const char16_t* CastleListHtmlBuilder::GetTemplateFileName() {
    // Current L2Server 0x00783130.
    return u"bbs_castle_list.htm";
}

void CastleListHtmlBuilder::DisassemblePacket() {
    // Current L2Server 0x00783138.
    L2_GUARD_FUNCTION();
    m_remainPacket = ::Disassemble(m_packet, "dd", &m_userServerId, &m_communityId);
}

void CastleListHtmlBuilder::BuildHtmlInternal() {
    // Current L2Server 0x0078b960 walks the ResidenceID map without taking
    // the ResidenceDB lock and filters through the current virtual cast pair.
    L2_GUARD_FUNCTION();
    m_htmlGenerator.AddValue(u"community_id", FormatString(u"%d", m_communityId));

    for (const auto& entry : g_ResidenceDB.m_ResidenceIDMap) {
        CResidence* const residence = entry.second;
        if (!residence->IsCastle()) {
            continue;
        }
        CCastle* const castle = residence->CastCastle();
        CLocalHtmlGenerator localGenerator(m_htmlGenerator.GetTemplet(1));
        localGenerator.AddValue(
            u"castle_id", FormatString(u"%d", castle->m_data != nullptr ? castle->m_data->m_nResidenceID : 0)
        );

        CPledge* const pledge = castle->GetResidenceOwnerPledge();
        if (pledge == nullptr) {
            localGenerator.AddValue(u"pledge_name", u"&$27;");
            localGenerator.AddValue(u"alliance_name", u"&$27;");
        } else {
            localGenerator.AddValue(u"pledge_name", std::u16string(pledge->GetPledgeName()));
            CAlliance* const alliance = pledge->GetAlliance();
            if (alliance == nullptr) {
                localGenerator.AddValue(u"alliance_name", u"&$27;");
            } else {
                const std::u16string allianceName(alliance->m_data->m_sAllianceName);
                localGenerator.AddValue(u"alliance_name", allianceName);
            }
        }

        localGenerator.AddValue(
            u"current_tax_rate",
            FormatString(
                u"%d",
                static_cast<int>(
                    residence->m_data->m_TaxInfoData[static_cast<std::size_t>(residence->m_data->m_ResidenceType)].m_nTaxRate
                )
            )
        );
        localGenerator.AddValue(u"next_tax_rate", FormatString(u"%d", residence->m_data->m_nTaxRateToChange));
        m_htmlGenerator.AddValue(u"CASTLE_LIST", localGenerator);
    }
}

void CastleListHtmlBuilder::SendHtml() {
    // Current L2Server 0x0078692c.
    L2_GUARD_FUNCTION();
    CHtmlSender sender(GetUserSocket(m_userServerId));
    std::u16string html = m_htmlGenerator.Generate();
    sender.SendHtmlWith3Packet(html);
}

void CastleListHtmlBuilder::Show(const unsigned int userServerId, const int communityId) {
    // Current L2Server 0x0078de74.
    L2_GUARD_FUNCTION();
    std::array<char, 20480> packet;
    ::Assemble(packet.data(), static_cast<int>(packet.size()), "dd", userServerId, communityId);
    CastleListHtmlBuilder builder(nullptr, reinterpret_cast<const unsigned char*>(packet.data()));
    builder.BuildHtml();
}

const char16_t* CastleInfoHtmlBuilder::GetTemplateFileName() {
    // Current L2Server 0x007831f4.
    return u"bbs_castle_info.htm";
}

void CastleInfoHtmlBuilder::DisassemblePacket() {
    // Current L2Server 0x007831fc.
    L2_GUARD_FUNCTION();
    m_remainPacket = ::Disassemble(m_packet, "ddd", &m_userServerId, &m_castleId, &m_communityId);
}

CCastle* CastleInfoHtmlBuilder::GetCastle() {
    // Current L2Server 0x00786c40 uses the ResidenceID tree and the exact
    // IsCastle/CastCastle virtual pair.
    L2_GUARD_FUNCTION();
    const auto position = g_ResidenceDB.m_ResidenceIDMap.find(m_castleId);
    if (position != g_ResidenceDB.m_ResidenceIDMap.end() && position->second->IsCastle()) {
        return position->second->CastCastle();
    }
    return nullptr;
}

void CastleInfoHtmlBuilder::BuildCastleInfo() {
    // Current L2Server 0x007875c8, including the duplicate pledge_name write
    // on the ownerless path and its unchecked tax-data lanes.
    L2_GUARD_FUNCTION();
    m_htmlGenerator.AddValue(u"community_id", FormatString(u"%d", m_communityId));
    CCastle* const castle = GetCastle();
    if (castle == nullptr) {
        return;
    }

    m_htmlGenerator.AddValue(
        u"castle_id", FormatString(u"%d", castle->m_data != nullptr ? castle->m_data->m_nResidenceID : 0)
    );
    CPledge* const pledge = castle->GetResidenceOwnerPledge();
    if (pledge == nullptr) {
        m_htmlGenerator.AddValue(u"pledge_name", u"&$27;");
        m_htmlGenerator.AddValue(u"pledge_ruler_name", u"&$27;");
        m_htmlGenerator.AddValue(u"pledge_name", u"&$27;");
        m_htmlGenerator.AddValue(u"alliance_name", u"&$27;");
    } else {
        m_htmlGenerator.AddValue(u"pledge_name", std::u16string(pledge->GetPledgeName()));
        m_htmlGenerator.AddValue(u"pledge_ruler_name", std::u16string(pledge->GetPledgeMasterName()));
        CAlliance* const alliance = pledge->GetAlliance();
        if (alliance == nullptr) {
            m_htmlGenerator.AddValue(u"alliance_name", u"&$27;");
        } else {
            const std::u16string allianceName(alliance->m_data->m_sAllianceName);
            m_htmlGenerator.AddValue(u"alliance_name", allianceName);
        }
    }

    m_htmlGenerator.AddValue(
        u"current_tax_rate",
        FormatString(
            u"%d",
            static_cast<int>(
                castle->m_data->m_TaxInfoData[static_cast<std::size_t>(castle->m_data->m_ResidenceType)].m_nTaxRate
            )
        )
    );
    m_htmlGenerator.AddValue(u"next_tax_rate", FormatString(u"%d", castle->m_data->m_nTaxRateToChange));

    CResidenceData* const data = castle->m_data;
    int nextSiege = 0;
    if (data != nullptr) {
        nextSiege = data->m_NextSiege;
    }
    if (nextSiege < 1) {
        m_htmlGenerator.AddValue(u"next_siege_date", u"&$27;");
    } else {
        if (data != nullptr) {
            nextSiege = data->m_NextSiege;
        }
        const std::u16string date = CParseHTML::MakeFormatedDateLocal(nextSiege, 2);
        const std::u16string hostDate(date);
        m_htmlGenerator.AddValue(u"next_siege_date", hostDate);
    }
}

void CastleInfoHtmlBuilder::BuildAgitList() {
    // Current L2Server 0x0078bd70 tests IsWorldAgit and the parent-castle id
    // before issuing the CastWorldAgit virtual call.
    L2_GUARD_FUNCTION();
    CCastle* const castle = GetCastle();
    if (castle == nullptr) {
        return;
    }

    for (const auto& entry : g_ResidenceDB.m_ResidenceIDMap) {
        CResidence* const residence = entry.second;
        if (!residence->IsWorldAgit()) {
            continue;
        }
        const int castleId = castle->m_data != nullptr ? castle->m_data->m_nResidenceID : 0;
        if (residence->m_data->m_nAgitParentCastleId != castleId) {
            continue;
        }
        CWorldAgit* const agit = residence->CastWorldAgit();

        CLocalHtmlGenerator localGenerator(m_htmlGenerator.GetTemplet(1));
        localGenerator.AddValue(u"agit_id", FormatString(u"%d", agit->m_data != nullptr ? agit->m_data->m_nResidenceID : 0));
        CPledge* const pledge = agit->GetResidenceOwnerPledge();
        if (pledge == nullptr) {
            localGenerator.AddValue(u"pledge_name", u"&$27;");
            localGenerator.AddValue(u"pledge_ruler_name", u"&$27;");
        } else {
            localGenerator.AddValue(u"pledge_id", FormatString(u"%d", static_cast<int>(pledge->GetDbId())));
            localGenerator.AddValue(u"pledge_name", std::u16string(pledge->GetPledgeName()));
            localGenerator.AddValue(u"pledge_ruler_name", std::u16string(pledge->GetPledgeMasterName()));
        }
        m_htmlGenerator.AddValue(u"AGIT_LIST", localGenerator);
    }
}

void CastleInfoHtmlBuilder::BuildHtmlInternal() {
    // Current L2Server 0x0078ca44.
    L2_GUARD_FUNCTION();
    BuildCastleInfo();
    BuildAgitList();
}

void CastleInfoHtmlBuilder::SendHtml() {
    // Current L2Server 0x00786a0c.
    L2_GUARD_FUNCTION();
    CHtmlSender sender(GetUserSocket(m_userServerId));
    std::u16string html = m_htmlGenerator.Generate();
    sender.SendHtmlWith3Packet(html);
}

void CastleInfoHtmlBuilder::Show(const unsigned int userServerId, const int castleId, const int communityId) {
    // Current L2Server 0x0078dfe0.
    L2_GUARD_FUNCTION();
    std::array<char, 20480> packet;
    ::Assemble(packet.data(), static_cast<int>(packet.size()), "ddd", userServerId, castleId, communityId);
    CastleInfoHtmlBuilder builder(nullptr, reinterpret_cast<const unsigned char*>(packet.data()));
    builder.BuildHtml();
}

ListFriendHtmlBuilder::ListFriendHtmlBuilder(CDBSocket* const dbSocket, const unsigned char* const packet) :
    CHtmlBuilder(dbSocket, packet),
    m_isDeleteAllFriendCmd(false),
    m_userServerId(0) {
    // Current L2Server 0x0078d548 leaves m_page and
    // m_selectedFriendCount untouched until packet disassembly.
}

ListFriendHtmlBuilder::~ListFriendHtmlBuilder() = default;

const char16_t* ListFriendHtmlBuilder::GetTemplateFileName() {
    // Current L2Server 0x00782e34.
    return u"bbs_friend_list.htm";
}

void ListFriendHtmlBuilder::DisassemblePacket() {
    // Current L2Server 0x007894cc caps the created id list at five but
    // keeps the uncapped decoded count in m_selectedFriendCount.
    L2_GUARD_FUNCTION();
    m_remainPacket = ::Disassemble(m_packet, "dd", &m_userServerId, &m_selectedFriendCount);
    for (int index = 0; index < m_selectedFriendCount && index < 5; ++index) {
        int friendDbId = 0;
        m_remainPacket = ::Disassemble(m_remainPacket, "d", &friendDbId);
        m_friendDbIdList.push_back(friendDbId);
    }
}

std::u16string ListFriendHtmlBuilder::GetFriendDbIdListString() {
    // Current L2Server 0x00788fe4.
    std::u16string result;
    for (const int friendDbId : m_friendDbIdList) {
        result += FormatString(u"%d_", friendDbId);
    }
    return result;
}

std::u16string ListFriendHtmlBuilder::GetFriendDbIdListStringDel(const int friendDbId) {
    // Current L2Server 0x007890e0.
    std::u16string result;
    for (const int selectedFriendDbId : m_friendDbIdList) {
        if (selectedFriendDbId != friendDbId) {
            result += FormatString(u"%d_", selectedFriendDbId);
        }
    }
    return result;
}

int ListFriendHtmlBuilder::GetFriendDbIdCountToAdd(const int friendDbId) {
    // Current L2Server 0x00786524.
    if (std::find(m_friendDbIdList.begin(), m_friendDbIdList.end(), friendDbId) != m_friendDbIdList.end()) {
        return m_selectedFriendCount;
    }
    return std::min(m_selectedFriendCount + 1, 5);
}

std::u16string ListFriendHtmlBuilder::GetFriendDbIdListStringAdd(const int friendDbId) {
    // Current L2Server 0x007895bc keeps the extra second exclusion
    // check after building its temporary list and drops the oldest id at 5.
    std::u16string result;
    std::vector<int> friendDbIdList;
    for (const int selectedFriendDbId : m_friendDbIdList) {
        if (selectedFriendDbId != friendDbId) {
            friendDbIdList.push_back(selectedFriendDbId);
        }
    }
    if (friendDbIdList.size() > 4) {
        friendDbIdList.erase(friendDbIdList.begin());
    }
    for (const int selectedFriendDbId : friendDbIdList) {
        if (selectedFriendDbId != friendDbId) {
            result += FormatString(u"%d_", selectedFriendDbId);
        }
    }
    result += FormatString(u"%d_", friendDbId);
    return result;
}

void ListFriendHtmlBuilder::Build1Packet() {
    // Current L2Server 0x0078adec.
    L2_GUARD_FUNCTION();
    m_htmlGenerator.AddValue(u"friend_id_count", FormatString(u"%d", m_selectedFriendCount));
    m_htmlGenerator.AddValue(u"friend_id_list", GetFriendDbIdListString());
    if (m_isDeleteAllFriendCmd) {
        CLocalHtmlGenerator localGenerator(m_htmlGenerator.GetTemplet(4));
        m_htmlGenerator.AddValue(u"delete_all_msg", localGenerator.Generate());
    }
}

void ListFriendHtmlBuilder::Build2Packet() {
    // Current L2Server 0x0078af94.
    L2_GUARD_FUNCTION();
    User* const user = GetUser(m_userServerId);
    if (user == nullptr) {
        return;
    }
    CSPointer<CFriends> friends = user->GetFriends();
    if (!friends) {
        return;
    }

    CPacketHelperEx<20480> packet;
    friends->MakeL2FriendList(packet, user->m_nSpouseDbID);
    const unsigned char* cursor = reinterpret_cast<const unsigned char*>(packet.GetPacket());
    const int count = packet.GetAssembledCount();
    for (int index = 0; index < count; ++index) {
        int friendDbId = 0;
        std::array<SharedUtilsWideChar, 24> friendName{};
        int loginStatus = 0;
        int friendServerId = 0;
        int friendLevel = 0;
        int friendClass = 0;
        std::array<SharedUtilsWideChar, 51> friendMemo{};
        cursor = ::Disassemble(
            cursor, "dSddddS", &friendDbId, 0x30, friendName.data(), &loginStatus, &friendServerId, &friendLevel,
            &friendClass, 0x66, friendMemo.data()
        );
        static_cast<void>(friendServerId);
        static_cast<void>(friendLevel);
        static_cast<void>(friendClass);
        static_cast<void>(friendMemo);

        CLocalHtmlGenerator localGenerator(m_htmlGenerator.GetTemplet(2));
        const std::u16string friendDbIdList = GetFriendDbIdListStringAdd(friendDbId);
        const std::wstring hostFriendDbIdList = Utf16ToWide(friendDbIdList);
        localGenerator.AddValue(
            u"action",
            FormatString(u"bypass _friendlist_%d_%s", GetFriendDbIdCountToAdd(friendDbId), hostFriendDbIdList.c_str())
        );
        const std::u16string hostFriendName = SanitizeUtf16(friendName.data());
        localGenerator.AddValue(u"friend_name", hostFriendName);
        localGenerator.AddValue(u"on_off", WideToUtf16(loginStatus != 0 ? L"on" : L"off"));
        m_htmlData += localGenerator.Generate();
    }
}

void ListFriendHtmlBuilder::Build3Packet() {
    // Current L2Server 0x0078b3c4 passes capacity 100 for its
    // 50-WCHAR friend-name object.
    L2_GUARD_FUNCTION();
    User* const user = GetUser(m_userServerId);
    if (user == nullptr) {
        return;
    }
    CSPointer<CFriends> friends = user->GetFriends();
    if (!friends) {
        return;
    }

    for (const int friendDbId : m_friendDbIdList) {
        std::array<char16_t, 50> friendName;
        friends->GetFriendName(static_cast<unsigned int>(friendDbId), friendName.data(), 100);
        CLocalHtmlGenerator localGenerator(m_htmlGenerator.GetTemplet(3));
        const std::u16string friendDbIdList = GetFriendDbIdListStringDel(friendDbId);
        const std::wstring hostFriendDbIdList = Utf16ToWide(friendDbIdList);
        localGenerator.AddValue(
            u"action", FormatString(u"bypass _friendlist_%d_%s", m_selectedFriendCount - 1, hostFriendDbIdList.c_str())
        );
        localGenerator.AddValue(u"friend_name", std::u16string(friendName.data()));
        m_selectedFriendNameList += localGenerator.Generate();
    }
}

void ListFriendHtmlBuilder::BuildHtmlInternal() {
    // Current L2Server 0x0078c820.
    L2_GUARD_FUNCTION();
    Build1Packet();
    Build2Packet();
    Build3Packet();
}

void ListFriendHtmlBuilder::SendHtml() {
    // Current L2Server 0x0078655c.
    L2_GUARD_FUNCTION();
    m_htmlGenerator.AddValue(u"friend_list", m_htmlData);
    m_htmlGenerator.AddValue(u"selected_friend_list", m_selectedFriendNameList);
    CHtmlSender sender(GetUserSocket(m_userServerId));
    std::u16string html = m_htmlGenerator.Generate();
    sender.SendHtml(html);
}

void ListFriendHtmlBuilder::Show(const int userServerId) {
    // Current L2Server 0x0078d5f4.
    std::array<char, 20480> packet;
    ::Assemble(packet.data(), static_cast<int>(packet.size()), "dd", userServerId, 0);
    ListFriendHtmlBuilder builder(nullptr, reinterpret_cast<const unsigned char*>(packet.data()));
    builder.BuildHtml();
}

void ListFriendHtmlBuilder::ShowDeleteAllFriendHtml(const int userServerId) {
    // Current L2Server 0x0078d698.
    std::array<char, 20480> packet;
    ::Assemble(packet.data(), static_cast<int>(packet.size()), "dd", userServerId, 0);
    ListFriendHtmlBuilder builder(nullptr, reinterpret_cast<const unsigned char*>(packet.data()));
    builder.SetDeleteAllFriendCmd(true);
    builder.BuildHtml();
}

const char16_t* ListBlockHtmlBuilder::GetTemplateFileName() {
    // Current L2Server 0x00782eec.
    return u"bbs_block_list.htm";
}

void ListBlockHtmlBuilder::DisassemblePacket() {
    // Current L2Server 0x00782e3c discards the returned cursor.
    L2_GUARD_FUNCTION();
    ::Disassemble(m_packet, "d", &m_userServerId);
}

void ListBlockHtmlBuilder::BuildHtmlInternal() {
    // Current L2Server 0x0078b6a0.
    L2_GUARD_FUNCTION();
    User* const user = GetUser(m_userServerId);
    if (user == nullptr) {
        return;
    }

    CPacketHelperEx<20480> packet;
    const int count = user->GetBlockCharList(packet);
    const unsigned char* cursor = reinterpret_cast<const unsigned char*>(packet.GetPacket());
    int blockDbId = 0;
    std::array<SharedUtilsWideChar, 50> blockName{};
    for (int index = 0; index < count; ++index) {
        cursor = ::Disassemble(cursor, "dS", &blockDbId, 100, blockName.data());
        CLocalHtmlGenerator localGenerator(m_htmlGenerator.GetTemplet(2));
        const std::u16string hostBlockName = SanitizeUtf16(blockName.data());
        localGenerator.AddValue(u"block_name", hostBlockName);
        localGenerator.AddValue(u"block_id", FormatString(u"%d", blockDbId));
        m_htmlGenerator.AddValue(u"block_list", localGenerator);
    }
    if (m_isDeleteAllBlockCmd) {
        CLocalHtmlGenerator localGenerator(m_htmlGenerator.GetTemplet(3));
        m_htmlGenerator.AddValue(u"delete_all_msg", localGenerator.Generate());
    }
}

void ListBlockHtmlBuilder::SendHtml() {
    // Current L2Server 0x00786668.
    L2_GUARD_FUNCTION();
    CHtmlSender sender(GetUserSocket(m_userServerId));
    std::u16string html = m_htmlGenerator.Generate();
    sender.SendHtml(html);
}

void ListBlockHtmlBuilder::Show(const int userServerId) {
    // Current L2Server 0x0078d740.
    std::array<char, 20480> packet;
    ::Assemble(packet.data(), static_cast<int>(packet.size()), "d", userServerId);
    ListBlockHtmlBuilder builder(nullptr, reinterpret_cast<const unsigned char*>(packet.data()));
    builder.BuildHtml();
}

void ListBlockHtmlBuilder::ShowDeleteAllBlockHtml(const int userServerId) {
    // Current L2Server 0x0078d810.
    std::array<char, 20480> packet;
    ::Assemble(packet.data(), static_cast<int>(packet.size()), "d", userServerId);
    ListBlockHtmlBuilder builder(nullptr, reinterpret_cast<const unsigned char*>(packet.data()));
    builder.SetDeleteAllBlockCmd(true);
    builder.BuildHtml();
}

void DeleteFriendBatchHandler::DisassemblePacket() {
    // Current L2Server 0x007897a4.
    L2_GUARD_FUNCTION();
    int friendCount = 0;
    m_remainPacket = ::Disassemble(m_packet, "dd", &m_userServerId, &friendCount);
    for (int index = 0; index < friendCount; ++index) {
        int friendDbId = 0;
        m_remainPacket = ::Disassemble(m_remainPacket, "d", &friendDbId);
        m_friendDbIdList.push_back(friendDbId);
    }

    int failedFriendCount = 0;
    m_remainPacket = ::Disassemble(m_remainPacket, "d", &failedFriendCount);
    for (int index = 0; index < failedFriendCount; ++index) {
        int friendDbId = 0;
        m_remainPacket = ::Disassemble(m_remainPacket, "d", &friendDbId);
        m_failedFriendDbIdList.push_back(friendDbId);
    }
}

void DeleteFriendBatchHandler::BuildHtmlInternal() {
    // Current L2Server 0x00786828 resolves the user separately for every
    // successful id and once more for the aggregate failure message.
    L2_GUARD_FUNCTION();
    for (const int friendDbId : m_friendDbIdList) {
        User* const user = GetUser(m_userServerId);
        if (user != nullptr) {
            user->BreakFriendship(friendDbId);
        }
    }
    if (!m_failedFriendDbIdList.empty()) {
        User* const user = GetUser(m_userServerId);
        if (user != nullptr) {
            user->GetSocket()->SendSystemMessage(SystemMessageId::CHECK_FRIEND_LIST_AFTER_UPDATE_FAILURE);
        }
    }
}

void DeleteFriendBatchHandler::SendHtml() {
    // Current L2Server 0x0078d8e0.
    L2_GUARD_FUNCTION();
    ListFriendHtmlBuilder::Show(m_userServerId);
}

void DeleteBlockBatchHandler::DisassemblePacket() {
    // Current L2Server 0x0078c8d4 uses a 0x32-byte UTF-16 destination.
    L2_GUARD_FUNCTION();
    int blockCount = 0;
    m_remainPacket = ::Disassemble(m_packet, "dd", &m_userServerId, &blockCount);
    for (int index = 0; index < blockCount; ++index) {
        int blockDbId = 0;
        std::array<SharedUtilsWideChar, 25> blockName;
        m_remainPacket = ::Disassemble(m_remainPacket, "dS", &blockDbId, 0x32, blockName.data());
        m_blockDbIdList.push_back(blockDbId);
        m_blockNameList.push_back(MakeTargetString(blockName));
    }
}

void DeleteBlockBatchHandler::BuildHtmlInternal() {
    // Current L2Server 0x00785328.
    L2_GUARD_FUNCTION();
    User* const user = GetUser(m_userServerId);
    if (user == nullptr) {
        return;
    }

    bool failed = false;
    for (std::size_t index = 0; index < m_blockDbIdList.size(); ++index) {
        const int blockDbId = runtime::CheckedVectorAt(m_blockDbIdList, index);
        if (blockDbId == 0) {
            failed = true;
        } else {
            user->DeleteBlockList(blockDbId);
            const std::u16string blockName = SanitizeUtf16(runtime::CheckedVectorAt(m_blockNameList, index));
            user->GetSocket()->SendSystemMessage_S(
                SystemMessageId::USER_REMOVED_FROM_BLOCK_LIST, blockName.c_str()
            );
        }
    }
    if (failed) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::BLOCK_LIST_REMOVAL_FAILED);
    }
}

void DeleteBlockBatchHandler::SendHtml() {
    // Current L2Server 0x0078d984 reopens the friend list.
    L2_GUARD_FUNCTION();
    ListFriendHtmlBuilder::Show(m_userServerId);
}

const char16_t* PledgeAnnounceHtmlBuilder::GetTemplateFileName() {
    // Current L2Server 0x00783040.
    return u"bbs_clanannounce.htm";
}

void PledgeAnnounceHtmlBuilder::DisassemblePacket() {
    // Current L2Server 0x0078ce64.
    L2_GUARD_FUNCTION();
    m_remainPacket = ::Disassemble(
        m_packet, "ddddS", &m_userServerId, &m_doesShow, &m_pledgeDbId, &m_replyForAnnounce, 0x1772, m_content.data()
    );

    CPledgeAnnounceSP announcement(
        new CPledgeAnnounce(m_pledgeDbId, m_doesShow == 1, m_content.data()), ObjectRefType::FunctionChain
    );
    CPledgeAnnounceRegistry::inst().Register(announcement);
}

void PledgeAnnounceHtmlBuilder::Build1001Packet() {
    // Current L2Server 0x00785498.
    L2_GUARD_FUNCTION();
    m_htmlGenerator.AddValue(u"MULTI_1", FormatString(u"1001%c", 8));
    m_htmlGenerator.AddValue(u"pledge_id", FormatString(u"%d", m_pledgeDbId));
    m_htmlGenerator.AddValue(u"flag", FormatString(u"%d", m_doesShow));

    if (m_doesShow == 1) {
        m_htmlGenerator.AddValue(u"usage", u"on");
        m_htmlGenerator.AddValue(u"switch_flag", u"0");
        m_htmlGenerator.AddValue(u"usage_flag", u"off");
    } else {
        m_htmlGenerator.AddValue(u"usage", u"off");
        m_htmlGenerator.AddValue(u"switch_flag", u"1");
        m_htmlGenerator.AddValue(u"usage_flag", u"on");
    }
}

void PledgeAnnounceHtmlBuilder::Build1002Packet() {
    // Current L2Server 0x0078cfe4.
    L2_GUARD_FUNCTION();
    CPledgeAnnounceSP announcement = CPledgeAnnounceRegistry::inst().Get(m_pledgeDbId);
    std::u16string empty;
    if (!announcement) {
        m_htmlData = BuildHtmlDataPacket(1002, TargetWide(empty), TargetWide(empty), TargetWide(empty), 3);
        return;
    }

    std::u16string zero(u"0");
    m_htmlData = BuildHtmlDataPacket(1002, TargetWide(zero), TargetWide(zero), TargetWide(announcement->GetContent()), 3);
}

bool PledgeAnnounceHtmlBuilder::Announce(const unsigned int userServerId, const unsigned int pledgeDbId) {
    // Current L2Server 0x0078d180.
    L2_GUARD_FUNCTION();
    CPledgeAnnounceSP announcement = CPledgeAnnounceRegistry::inst().Get(pledgeDbId);
    if (!announcement) {
        return false;
    }

    if (announcement->DoesShow()) {
        User* const user = GetUser(static_cast<int>(userServerId));
        if (user != nullptr) {
            CPledge* const pledge = user->GetPledge();
            CHtmlGenerator generator(u"clan_popup.htm");
            generator.AddValue(u"pledge_name", std::u16string(pledge != nullptr ? pledge->GetPledgeName() : u""));
            generator.AddValue(u"content", CParseHTML::ConvertStr(TargetWide(announcement->GetContent()), 4));
            const std::u16string html = generator.Generate();
            const std::u16string hostHtml(html);
            user->ShowHTML(u"", hostHtml.c_str(), 0, HTML::LAUNCH, true);
        }
    }
    return true;
}

void PledgeAnnounceHtmlBuilder::Show(
    const unsigned int userServerId, const unsigned int pledgeDbId, const int doesShow, SharedUtilsWideChar* const content
) {
    // Current L2Server 0x0078da28.
    L2_GUARD_FUNCTION();
    std::array<char, 0x5000> packet;
    ::Assemble(packet.data(), static_cast<int>(packet.size()), "ddddS", userServerId, doesShow, pledgeDbId, 0, content);
    PledgeAnnounceHtmlBuilder builder(nullptr, reinterpret_cast<const unsigned char*>(packet.data()));
    builder.BuildHtml();
}

void PledgeAnnounceHtmlBuilder::BuildHtmlInternal() {
    // Current L2Server 0x0078dbb4.
    L2_GUARD_FUNCTION();
    if (m_replyForAnnounce == 0) {
        Build1001Packet();
        Build1002Packet();
    }
}

void PledgeAnnounceHtmlBuilder::SendHtml() {
    // Current L2Server 0x0078dc68.
    L2_GUARD_FUNCTION();
    if (m_replyForAnnounce == 0) {
        CHtmlSender sender(GetUserSocket(m_userServerId));
        std::u16string html = m_htmlGenerator.Generate();
        sender.SendHtml(html);
        sender.SendHtml(m_htmlData);
    } else {
        Announce(m_userServerId, m_pledgeDbId);
    }
}

void SetPledgeAnnounceHtmlHandler::DisassemblePacket() {
    // Current L2Server 0x00783048.
    L2_GUARD_FUNCTION();
    m_remainPacket =
        ::Disassemble(m_packet, "dddSc", &m_userServerId, &m_doesShow, &m_pledgeDbId, 0x1772, m_content.data(), &m_success);
}

void SetPledgeAnnounceHtmlHandler::BuildHtmlInternal() {
    // Current L2Server 0x0078d410.
    L2_GUARD_FUNCTION();
    if (m_success == true) {
        CPledgeAnnounceSP announcement(
            new CPledgeAnnounce(m_pledgeDbId, m_doesShow == 1, m_content.data()), ObjectRefType::FunctionChain
        );
        CPledgeAnnounceRegistry::inst().Register(announcement);
    }
}

void SetPledgeAnnounceHtmlHandler::SendHtml() {
    // Current L2Server 0x0078dd70.
    L2_GUARD_FUNCTION();
    PledgeAnnounceHtmlBuilder::Show(
        static_cast<unsigned int>(m_userServerId), m_pledgeDbId, m_doesShow == 1, m_content.data()
    );
    RelayClient::Inst()->SendChangePledgeAnnounce(static_cast<int>(m_pledgeDbId), m_content.data());
    if (m_userServerId != 0 && m_success == true) {
        CUserSocket* const socket = GetUserSocket(m_userServerId);
        if (socket != nullptr) {
            socket->SendSystemMessage(SystemMessageId::CLAN_NOTICE_SAVED);
        }
    }
}
