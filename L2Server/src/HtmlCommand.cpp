#include "HtmlCommand.h"
#include "ClanBbs.h"
#include "CommCli.h"
#include "DB.h"
#include "Friends.h"
#include "HtmlBuilder.h"
#include "HtmlGenerator.h"
#include "ParseHtml.h"
#include "Pledge.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/MemorySub.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/MailConstant.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/PledgeAnnounceRegistry.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/StringTokenizer.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace {


std::u16string ToTargetStringFromPacket(const SharedUtilsWideChar* value) {
    const auto* const first = reinterpret_cast<const char16_t*>(value);
    const auto* last = first;
    while (*last != u'\0') {
        ++last;
    }
    return std::u16string(std::u16string_view(first, static_cast<std::size_t>(last - first)));
}

bool EqualsTargetLiteral(const SharedUtilsWideChar* left, const char16_t* right) noexcept {
    for (;;) {
        const char16_t leftValue = static_cast<char16_t>(*left++);
        const char16_t rightValue = *right++;
        if (leftValue != rightValue) {
            return false;
        }
        if (leftValue == u'\0') {
            return true;
        }
    }
}

using HtmlCommandCreator = CHtmlCommandSP (*)();
using HtmlCommandMap = std::map<std::u16string, HtmlCommandCreator, runtime::CStringLess<std::u16string>>;

HtmlCommandMap g_htmlCommandMap;

template <typename Command>
CHtmlCommandSP CreateHtmlCommand() {
    Command* const command = new Command;
    CHtmlCommandSP result(command, ObjectRefType::FunctionChain);
    L2_RELEASE(command, ObjectRefType::CreateDelete);
    return result;
}

#define L2_HTML_COMMAND_CREATOR(ClassName)                                                                                  \
    CHtmlCommandSP CreateHtmlCommand_##ClassName() { return CreateHtmlCommand<ClassName>(); }

L2_HTML_COMMAND_CREATOR(ListMail)
L2_HTML_COMMAND_CREATOR(ReadMail)
L2_HTML_COMMAND_CREATOR(WriteMailForm)
L2_HTML_COMMAND_CREATOR(WriteFriendMailForm)
L2_HTML_COMMAND_CREATOR(SendMail)
L2_HTML_COMMAND_CREATOR(SaveMail)
L2_HTML_COMMAND_CREATOR(ModifyMailForm)
L2_HTML_COMMAND_CREATOR(ReplyMailForm)
L2_HTML_COMMAND_CREATOR(ForwardMailForm)
L2_HTML_COMMAND_CREATOR(ModifyMail)
L2_HTML_COMMAND_CREATOR(ModifySendMail)
L2_HTML_COMMAND_CREATOR(DeleteMail)
L2_HTML_COMMAND_CREATOR(ArchiveMail)
L2_HTML_COMMAND_CREATOR(SearchMail)
L2_HTML_COMMAND_CREATOR(WritePledgeMailForm)
L2_HTML_COMMAND_CREATOR(SendPledgeMail)
L2_HTML_COMMAND_CREATOR(ListFriend)
L2_HTML_COMMAND_CREATOR(DeleteFriend)
L2_HTML_COMMAND_CREATOR(DeleteAllFriendConfirm)
L2_HTML_COMMAND_CREATOR(DeleteAllFriend)
L2_HTML_COMMAND_CREATOR(ListBlock)
L2_HTML_COMMAND_CREATOR(AddBlock)
L2_HTML_COMMAND_CREATOR(DeleteBlock)
L2_HTML_COMMAND_CREATOR(DeleteAllBlockConfirm)
L2_HTML_COMMAND_CREATOR(DeleteAllBlock)
L2_HTML_COMMAND_CREATOR(WritePledgeAnnounceForm)
L2_HTML_COMMAND_CREATOR(WritePledgeAnnounce)
L2_HTML_COMMAND_CREATOR(SwitchPledgeAnnounceShowFlag)
L2_HTML_COMMAND_CREATOR(ViewCastleList)
L2_HTML_COMMAND_CREATOR(ViewCastleInfo)

#undef L2_HTML_COMMAND_CREATOR

bool RegisterHtmlCommand(const wchar_t* command, HtmlCommandCreator creator) {
    g_htmlCommandMap.insert_or_assign(WideToUtf16(command), creator);
    return true;
}

const bool IsRegistered_ListMail = RegisterHtmlCommand(MAIL_LIST_CMD, CreateHtmlCommand_ListMail);
const bool IsRegistered_ReadMail = RegisterHtmlCommand(MAIL_READ_CMD, CreateHtmlCommand_ReadMail);
const bool IsRegistered_WriteMailForm = RegisterHtmlCommand(MAIL_WRITE_FORM_CMD, CreateHtmlCommand_WriteMailForm);
const bool IsRegistered_WriteFriendMailForm =
    RegisterHtmlCommand(MAIL_WRITE_FRIEND_FORM_CMD, CreateHtmlCommand_WriteFriendMailForm);
const bool IsRegistered_SendMail = RegisterHtmlCommand(MAIL_SEND_CMD, CreateHtmlCommand_SendMail);
const bool IsRegistered_SaveMail = RegisterHtmlCommand(MAIL_SAVE_CMD, CreateHtmlCommand_SaveMail);
const bool IsRegistered_ModifyMailForm = RegisterHtmlCommand(MAIL_MODIFY_FORM_CMD, CreateHtmlCommand_ModifyMailForm);
const bool IsRegistered_ReplyMailForm = RegisterHtmlCommand(MAIL_REPLY_FORM_CMD, CreateHtmlCommand_ReplyMailForm);
const bool IsRegistered_ForwardMailForm = RegisterHtmlCommand(MAIL_FORWARD_FORM_CMD, CreateHtmlCommand_ForwardMailForm);
const bool IsRegistered_ModifyMail = RegisterHtmlCommand(MAIL_MODIFY_CMD, CreateHtmlCommand_ModifyMail);
const bool IsRegistered_ModifySendMail = RegisterHtmlCommand(MAIL_MODIFY_SEND_CMD, CreateHtmlCommand_ModifySendMail);
const bool IsRegistered_DeleteMail = RegisterHtmlCommand(MAIL_DELETE_CMD, CreateHtmlCommand_DeleteMail);
const bool IsRegistered_ArchiveMail = RegisterHtmlCommand(MAIL_ARCHVIE_CMD, CreateHtmlCommand_ArchiveMail);
const bool IsRegistered_SearchMail = RegisterHtmlCommand(MAIL_SEARCH_CMD, CreateHtmlCommand_SearchMail);
const bool IsRegistered_WritePledgeMailForm =
    RegisterHtmlCommand(MAIL_PLEDGE_WRITE_FORM_CMD, CreateHtmlCommand_WritePledgeMailForm);
const bool IsRegistered_SendPledgeMail = RegisterHtmlCommand(MAIL_PLEDGE_SEND_CMD, CreateHtmlCommand_SendPledgeMail);
const bool IsRegistered_ListFriend = RegisterHtmlCommand(FRIEND_LIST_CMD, CreateHtmlCommand_ListFriend);
const bool IsRegistered_DeleteFriend = RegisterHtmlCommand(FRIEND_DELETE_CMD, CreateHtmlCommand_DeleteFriend);
const bool IsRegistered_DeleteAllFriendConfirm =
    RegisterHtmlCommand(FRIEND_DELETE_ALL_CONFIRM_CMD, CreateHtmlCommand_DeleteAllFriendConfirm);
const bool IsRegistered_DeleteAllFriend = RegisterHtmlCommand(FRIEND_DELETE_ALL_CMD, CreateHtmlCommand_DeleteAllFriend);
const bool IsRegistered_ListBlock = RegisterHtmlCommand(FRIEND_BLOCK_LIST_CMD, CreateHtmlCommand_ListBlock);
const bool IsRegistered_AddBlock = RegisterHtmlCommand(FRIEND_ADD_BLOCK_CMD, CreateHtmlCommand_AddBlock);
const bool IsRegistered_DeleteBlock = RegisterHtmlCommand(FRIEND_DELETE_BLOCK_CMD, CreateHtmlCommand_DeleteBlock);
const bool IsRegistered_DeleteAllBlockConfirm =
    RegisterHtmlCommand(FRIEND_DELETE_ALL_BLOCK_CONFIRM_CMD, CreateHtmlCommand_DeleteAllBlockConfirm);
const bool IsRegistered_DeleteAllBlock = RegisterHtmlCommand(FRIEND_DELETE_ALL_BLOCK_CMD, CreateHtmlCommand_DeleteAllBlock);
const bool IsRegistered_WritePledgeAnnounceForm =
    RegisterHtmlCommand(L"announcepledgewriteform", CreateHtmlCommand_WritePledgeAnnounceForm);
const bool IsRegistered_WritePledgeAnnounce =
    RegisterHtmlCommand(L"announcepledgewrite", CreateHtmlCommand_WritePledgeAnnounce);
const bool IsRegistered_SwitchPledgeAnnounceShowFlag =
    RegisterHtmlCommand(L"announcepledgeswitchshowflag", CreateHtmlCommand_SwitchPledgeAnnounceShowFlag);
const bool IsRegistered_ViewCastleList = RegisterHtmlCommand(L"announcecastlelist", CreateHtmlCommand_ViewCastleList);
const bool IsRegistered_ViewCastleInfo = RegisterHtmlCommand(L"announcecastleinfo", CreateHtmlCommand_ViewCastleInfo);

} // namespace

CHtmlCommand::CHtmlCommand() = default;

CHtmlCommand::~CHtmlCommand() = default;

L2_DEFINE_OBJECT_CLASS_ID(CHtmlCommand)

L2_DEFINE_OBJECT_CLASS_NAME(CHtmlCommand, m_pClassName_CHtmlCommand)

L2_DEFINE_OBJECT_ALLOCATOR(CHtmlCommand, __GetClassName())

L2_DEFINE_OBJECT_NEW(CHtmlCommand)

L2_DEFINE_OBJECT_RELEASE(CHtmlCommand)

void CHtmlCommand::SetPacket(const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    m_packet = packet;
}

void CHtmlCommand::SetSocket(CSocket* socket) {
    L2_GUARD_FUNCTION();
    m_userSocket = static_cast<CUserSocket*>(socket);
    m_user = m_userSocket->GetUser();
}

void CHtmlCommand::SetCommand(CSocket* socket, const SharedUtilsWideChar* command) {
    L2_GUARD_FUNCTION();
    m_socket = socket;
    m_cmd = command;
    m_packet = nullptr;
    SetSocket(socket);
}

bool CHtmlCommand::CheckCommServer() {
    L2_GUARD_FUNCTION();
    const bool sendable = commcli.m_CommSendable;
    if (!sendable && m_user != nullptr) {
        m_userSocket->Send("cc", 0x7b, 0);
    }
    return sendable;
}

#define L2_HTML_COMMAND_MEMORY_DEF(ClassName)                                                                               \
    int ClassName::__GetClassID() noexcept { return L2_MEMORY_CLASS_ID(ClassName); }                                        \
    const wchar_t* ClassName::__GetClassName() noexcept { return m_pClassName_##ClassName; }                                \
    void* ClassName::operator new(const std::size_t requestedSize, const bool objectAllocation) {                           \
        return runtime::process::AllocateMemoryObject(                                                                   \
            requestedSize, objectAllocation, L2_MEMORY_CLASS_ID(ClassName), __GetClassName(), #ClassName,                   \
            static_cast<std::int32_t>(sizeof(ClassName))                                                                    \
        );                                                                                                                  \
    }                                                                                                                       \
    void* ClassName::operator new(const std::size_t requestedSize) { return operator new(requestedSize, true); }            \
    std::int32_t ClassName::Release(const char* callerFile, const int callerLine, ObjectRefType, bool) {                    \
        return ReleaseAs(                                                                                                   \
            L2_MEMORY_CLASS_ID(ClassName), #ClassName, L2_SOURCE_FILE, __LINE__, callerFile, callerLine           \
        );                                                                                                                  \
    }

L2_HTML_COMMAND_MEMORY_DEF(NullCommand)
L2_HTML_COMMAND_MEMORY_DEF(ListMail)
L2_HTML_COMMAND_MEMORY_DEF(ReadMail)
L2_HTML_COMMAND_MEMORY_DEF(WriteMailForm)
L2_HTML_COMMAND_MEMORY_DEF(WritePledgeMailForm)
L2_HTML_COMMAND_MEMORY_DEF(WriteFriendMailForm)
L2_HTML_COMMAND_MEMORY_DEF(SendMail)
L2_HTML_COMMAND_MEMORY_DEF(SaveMail)
L2_HTML_COMMAND_MEMORY_DEF(SendPledgeMail)
L2_HTML_COMMAND_MEMORY_DEF(ModifyMailForm)
L2_HTML_COMMAND_MEMORY_DEF(ReplyMailForm)
L2_HTML_COMMAND_MEMORY_DEF(ForwardMailForm)
L2_HTML_COMMAND_MEMORY_DEF(ModifyMail)
L2_HTML_COMMAND_MEMORY_DEF(ModifySendMail)
L2_HTML_COMMAND_MEMORY_DEF(DeleteMail)
L2_HTML_COMMAND_MEMORY_DEF(ArchiveMail)
L2_HTML_COMMAND_MEMORY_DEF(SearchMail)
L2_HTML_COMMAND_MEMORY_DEF(ListFriend)
L2_HTML_COMMAND_MEMORY_DEF(DeleteFriend)
L2_HTML_COMMAND_MEMORY_DEF(DeleteAllFriendConfirm)
L2_HTML_COMMAND_MEMORY_DEF(DeleteAllFriend)
L2_HTML_COMMAND_MEMORY_DEF(ListBlock)
L2_HTML_COMMAND_MEMORY_DEF(AddBlock)
L2_HTML_COMMAND_MEMORY_DEF(DeleteBlock)
L2_HTML_COMMAND_MEMORY_DEF(DeleteAllBlockConfirm)
L2_HTML_COMMAND_MEMORY_DEF(DeleteAllBlock)
L2_HTML_COMMAND_MEMORY_DEF(WritePledgeAnnounceForm)
L2_HTML_COMMAND_MEMORY_DEF(WritePledgeAnnounce)
L2_HTML_COMMAND_MEMORY_DEF(SwitchPledgeAnnounceShowFlag)
L2_HTML_COMMAND_MEMORY_DEF(ViewCastleList)
L2_HTML_COMMAND_MEMORY_DEF(ViewCastleInfo)

#undef L2_HTML_COMMAND_MEMORY_DEF

NullCommand CHtmlCommandFactory::m_nullCmd;

CHtmlCommandSP CHtmlCommandFactory::GetHtmlCommand(CSocket* socket, const SharedUtilsWideChar* command) {
    L2_GUARD_FUNCTION();
    std::array<SharedUtilsWideChar, 50> commandName{};
    const SharedUtilsWideChar* const remainder =
        ParseHTMLParam2(command + 1, const_cast<char*>("S"), static_cast<int>(sizeof(commandName)), commandName.data());

    const std::u16string key = ToTargetStringFromPacket(commandName.data());
    const auto found = g_htmlCommandMap.find(key);
    if (found == g_htmlCommandMap.end()) {
        CHtmlCommandSP result(&m_nullCmd, ObjectRefType::FunctionChain);
        result->SetCommand(socket, command);
        return result;
    }

    CHtmlCommandSP result = found->second();
    result->SetCommand(socket, remainder);
    return result;
}

void NullCommand::ExecuteInternal() {}

void NullCommand::ParseParameter() {}

void ListMail::ExecuteInternal() {
    L2_GUARD_FUNCTION();
    db.RequestMailList(m_user, m_listParam);
}

void ListMail::ParseParameter() {
    L2_GUARD_FUNCTION();
    int mailBoxType = 0;
    int page = 0;
    int searchTarget = 0;
    std::array<SharedUtilsWideChar, 21> keyword{};
    ParseHTMLParam(m_cmd, const_cast<char*>("dddS"), &mailBoxType, &page, &searchTarget, 0x2a, keyword.data());
    m_listParam.SetMailBoxType(mailBoxType);
    m_listParam.SetPage(page);
    m_listParam.SetSearchTarget(searchTarget);
    const std::u16string trimmed = Trim(keyword.data());
    m_listParam.SetKeyword(trimmed.c_str());
}

void ReadMail::ExecuteInternal() {
    L2_GUARD_FUNCTION();
    db.RequestReadMail(m_user, m_mailId, m_listParam);
}

void ReadMail::ParseParameter() {
    L2_GUARD_FUNCTION();
    int mailBoxType = 0;
    int page = 0;
    int searchTarget = 0;
    std::array<SharedUtilsWideChar, 21> keyword{};
    ParseHTMLParam(m_cmd, const_cast<char*>("ddddS"), &m_mailId, &mailBoxType, &page, &searchTarget, 0x2a, keyword.data());
    m_listParam.SetMailBoxType(mailBoxType);
    m_listParam.SetPage(page);
    m_listParam.SetSearchTarget(searchTarget);
    const std::u16string trimmed = Trim(keyword.data());
    m_listParam.SetKeyword(trimmed.c_str());
}

void WriteMailForm::ExecuteInternal() {
    L2_GUARD_FUNCTION();
    std::array<char, 0x5000> packet{};
    Assemble(
        packet.data(), static_cast<int>(packet.size()), "db", m_user->GetServerId(),
        static_cast<int>(sizeof(CMailListParameter)), &m_listParam
    );
    WriteMailFormHtmlBuilder builder(nullptr, reinterpret_cast<const std::uint8_t*>(packet.data()));
    builder.BuildHtml();
}

void WriteMailForm::ParseParameter() {
    L2_GUARD_FUNCTION();
    int mailBoxType = 0;
    int page = 0;
    int searchTarget = 0;
    std::array<SharedUtilsWideChar, 21> keyword{};
    ParseHTMLParam(m_cmd, const_cast<char*>("dddS"), &mailBoxType, &page, &searchTarget, 0x2a, keyword.data());
    m_listParam.SetMailBoxType(mailBoxType);
    m_listParam.SetPage(page);
    m_listParam.SetSearchTarget(searchTarget);
    m_listParam.SetKeyword(reinterpret_cast<const char16_t*>(keyword.data()));
}

void SendMail::ExecuteInternal() {
    L2_GUARD_FUNCTION();
    if (!CheckReceiverList() || !CheckEmptyTitle()) {
        ReturnMail();
        return;
    }

    const std::u16string convertedTitle = CParseHTML::ConvertStr(m_title.data(), 4);
    const std::u16string receiver(FixedUtf16View(m_receiverList));
    const std::u16string content(FixedUtf16View(m_content));
    db.RequestSendMail(m_user, m_mailType, m_relatedId, receiver.data(), convertedTitle.c_str(), content.data(), m_listParam);
}

bool SendMail::CheckReceiverList() {
    L2_GUARD_FUNCTION();
    StringTokenizer tokenizer(ToTargetStringFromPacket(m_receiverList.data()), std::u16string(u";"));
    int nonEmptyCount = 0;
    int index = 0;
    if (tokenizer.CountTokens() > 0) {
        do {
            std::u16string token = tokenizer.NextToken();
            const std::u16string trimmed = Trim(reinterpret_cast<SharedUtilsWideChar*>(token.data()));
            if (!trimmed.empty()) {
                ++nonEmptyCount;
            }
            ++index;
        } while (index < tokenizer.CountTokens());
        if (nonEmptyCount > 0 && nonEmptyCount < 6) {
            return true;
        }
    }
    m_errorType = -3;
    return false;
}

bool SendMail::CheckEmptyTitle() {
    const std::u16string trimmed = Trim(m_title.data());
    if (trimmed.empty()) {
        m_errorType = -6;
        return false;
    }
    return true;
}

void SendMail::ReturnMail() {
    L2_GUARD_FUNCTION();
    std::array<char, 0x5000> packet;
    Assemble(
        packet.data(), static_cast<int>(packet.size()), "ddbddSSSS", m_user->GetServerId(), m_errorType,
        static_cast<int>(sizeof(CMailListParameter)), &m_listParam, m_mailType, m_relatedId, m_receiverList.data(),
        m_title.data(), m_content.data(), m_blockedName.data()
    );
    SendMailHtmlBuilder builder(nullptr, reinterpret_cast<const std::uint8_t*>(packet.data()));
    builder.BuildHtml();
}

void SendMail::ParseParameter() {
    L2_GUARD_FUNCTION();
    int mailBoxType = 0;
    int page = 0;
    int searchTarget = 0;
    std::array<SharedUtilsWideChar, 21> keyword{};
    std::array<SharedUtilsWideChar, 32> scratch1;
    std::array<SharedUtilsWideChar, 32> scratch2;
    ParseHTMLParam(
        m_cmd, const_cast<char*>("dddddS"), &m_mailType, &m_relatedId, &mailBoxType, &page, &searchTarget, 0x2a,
        keyword.data()
    );
    m_listParam.SetMailBoxType(mailBoxType);
    m_listParam.SetPage(page);
    m_listParam.SetSearchTarget(searchTarget);
    m_listParam.SetKeyword(reinterpret_cast<const char16_t*>(keyword.data()));
    m_packet = Disassemble(
        m_packet, "SSSSS", 0x40, scratch1.data(), 0x40, scratch2.data(), 0x1f4, m_receiverList.data(), 0x190, m_title.data(),
        0x1770, m_content.data()
    );
    std::fill(m_blockedName.begin(), m_blockedName.end(), SharedUtilsWideChar{});
}

void SaveMail::ExecuteInternal() {
    L2_GUARD_FUNCTION();
    const std::u16string receiver(FixedUtf16View(m_receiverList));
    const std::u16string title(FixedUtf16View(m_title));
    const std::u16string content(FixedUtf16View(m_content));
    db.RequestSaveMail(m_user, m_mailType, m_relatedId, receiver.data(), title.data(), content.data(), m_listParam);
}

void SaveMail::ParseParameter() {
    L2_GUARD_FUNCTION();
    int mailBoxType = 0;
    int page = 0;
    int searchTarget = 0;
    std::array<SharedUtilsWideChar, 21> keyword{};
    std::array<SharedUtilsWideChar, 32> scratch1;
    std::array<SharedUtilsWideChar, 32> scratch2;
    ParseHTMLParam(
        m_cmd, const_cast<char*>("dddddS"), &m_mailType, &m_relatedId, &mailBoxType, &page, &searchTarget, 0x2a,
        keyword.data()
    );
    m_listParam.SetMailBoxType(mailBoxType);
    m_listParam.SetPage(page);
    m_listParam.SetSearchTarget(searchTarget);
    m_listParam.SetKeyword(reinterpret_cast<const char16_t*>(keyword.data()));
    m_packet = Disassemble(
        m_packet, "SSSSS", 0x40, scratch1.data(), 0x40, scratch2.data(), 0x1f4, m_receiverList.data(), 0x190, m_title.data(),
        0x1770, m_content.data()
    );
}

void SendPledgeMail::ExecuteInternal() {
    L2_GUARD_FUNCTION();
    const std::u16string title(FixedUtf16View(m_title));
    const std::u16string content(FixedUtf16View(m_content));
    db.RequestSendPledgeMail(m_user, title.data(), content.data());
}

void SendPledgeMail::ParseParameter() {
    L2_GUARD_FUNCTION();
    std::array<SharedUtilsWideChar, 32> scratch1;
    std::array<SharedUtilsWideChar, 32> scratch2;
    ParseHTMLParam(m_cmd, const_cast<char*>("d"), &m_pledgeId);
    m_packet = Disassemble(
        m_packet, "SSSSS", 0x40, scratch1.data(), 0x40, scratch2.data(), 0x40, scratch1.data(), 0x190, m_title.data(),
        0x1770, m_content.data()
    );
}

void ModifyMailForm::ExecuteInternal() {
    L2_GUARD_FUNCTION();
    db.RequestGetTempMail(m_user, m_relatedMailId, m_listParam);
}

void ModifyMailForm::ParseParameter() {
    L2_GUARD_FUNCTION();
    int mailBoxType = 0;
    int page = 0;
    int searchTarget = 0;
    std::array<SharedUtilsWideChar, 21> keyword{};
    ParseHTMLParam(
        m_cmd, const_cast<char*>("ddddS"), &m_relatedMailId, &mailBoxType, &page, &searchTarget, 0x2a, keyword.data()
    );
    m_listParam.SetMailBoxType(mailBoxType);
    m_listParam.SetPage(page);
    m_listParam.SetSearchTarget(searchTarget);
    m_listParam.SetKeyword(reinterpret_cast<const char16_t*>(keyword.data()));
}

void ReplyMailForm::ExecuteInternal() {
    L2_GUARD_FUNCTION();
    db.RequestReplyMail(m_user, m_relatedMailId, m_listParam);
}

void ReplyMailForm::ParseParameter() {
    L2_GUARD_FUNCTION();
    int mailBoxType = 0;
    int page = 0;
    int searchTarget = 0;
    std::array<SharedUtilsWideChar, 21> keyword{};
    ParseHTMLParam(
        m_cmd, const_cast<char*>("ddddS"), &m_relatedMailId, &mailBoxType, &page, &searchTarget, 0x2a, keyword.data()
    );
    m_listParam.SetMailBoxType(mailBoxType);
    m_listParam.SetPage(page);
    m_listParam.SetSearchTarget(searchTarget);
    m_listParam.SetKeyword(reinterpret_cast<const char16_t*>(keyword.data()));
}

void ForwardMailForm::ExecuteInternal() {
    L2_GUARD_FUNCTION();
    db.RequestForwardMail(m_user, m_relatedMailId, m_listParam);
}

void ForwardMailForm::ParseParameter() {
    L2_GUARD_FUNCTION();
    int mailBoxType = 0;
    int page = 0;
    int searchTarget = 0;
    std::array<SharedUtilsWideChar, 21> keyword{};
    ParseHTMLParam(
        m_cmd, const_cast<char*>("ddddS"), &m_relatedMailId, &mailBoxType, &page, &searchTarget, 0x2a, keyword.data()
    );
    m_listParam.SetMailBoxType(mailBoxType);
    m_listParam.SetPage(page);
    m_listParam.SetSearchTarget(searchTarget);
    m_listParam.SetKeyword(reinterpret_cast<const char16_t*>(keyword.data()));
}

void ModifyMail::ExecuteInternal() {
    L2_GUARD_FUNCTION();
    const std::u16string receiver(FixedUtf16View(m_receiverList));
    const std::u16string title(FixedUtf16View(m_title));
    const std::u16string content(FixedUtf16View(m_content));
    db.RequestModifyMail(m_user, m_mailId, receiver.data(), title.data(), content.data());
}

void ModifyMail::ParseParameter() {
    L2_GUARD_FUNCTION();
    std::array<SharedUtilsWideChar, 32> scratch1;
    std::array<SharedUtilsWideChar, 32> scratch2;
    ParseHTMLParam(m_cmd, const_cast<char*>("d"), &m_mailId);
    m_packet = Disassemble(
        m_packet, "SSSSS", 0x40, scratch1.data(), 0x40, scratch2.data(), 0x1f4, m_receiverList.data(), 0x190, m_title.data(),
        0x1770, m_content.data()
    );
}

void ModifySendMail::ExecuteInternal() {
    L2_GUARD_FUNCTION();
    if (CheckReceiverList()) {
        const std::u16string receiver(FixedUtf16View(m_receiverList));
        const std::u16string title(FixedUtf16View(m_title));
        const std::u16string content(FixedUtf16View(m_content));
        db.RequestModifySendMail(m_user, m_mailId, receiver.data(), title.data(), content.data(), m_listParam);
        return;
    }

    std::array<char, 0x5000> packet;
    Assemble(
        packet.data(), static_cast<int>(packet.size()), "dddbSSSS", m_user->GetServerId(), m_errorType, m_mailId,
        static_cast<int>(sizeof(CMailListParameter)), &m_listParam, m_receiverList.data(), m_title.data(), m_content.data(),
        m_blockedName.data()
    );
    ModifyTempMailHtmlBuilder builder(nullptr, reinterpret_cast<const std::uint8_t*>(packet.data()));
    builder.BuildHtml();
}

bool ModifySendMail::CheckReceiverList() {
    L2_GUARD_FUNCTION();
    StringTokenizer tokenizer(ToTargetStringFromPacket(m_receiverList.data()), std::u16string(u";"));
    int nonEmptyCount = 0;
    int index = 0;
    if (tokenizer.CountTokens() > 0) {
        do {
            std::u16string token = tokenizer.NextToken();
            const std::u16string trimmed = Trim(reinterpret_cast<SharedUtilsWideChar*>(token.data()));
            if (!trimmed.empty()) {
                ++nonEmptyCount;
            }
            ++index;
        } while (index < tokenizer.CountTokens());
        if (nonEmptyCount > 0 && nonEmptyCount < 6) {
            return true;
        }
    }
    m_errorType = -3;
    return false;
}

void ModifySendMail::ParseParameter() {
    L2_GUARD_FUNCTION();
    int mailBoxType = 0;
    int page = 0;
    int searchTarget = 0;
    std::array<SharedUtilsWideChar, 21> keyword{};
    std::array<SharedUtilsWideChar, 32> scratch1;
    std::array<SharedUtilsWideChar, 32> scratch2;
    ParseHTMLParam(m_cmd, const_cast<char*>("ddddS"), &m_mailId, &mailBoxType, &page, &searchTarget, 0x2a, keyword.data());
    m_listParam.SetMailBoxType(mailBoxType);
    m_listParam.SetPage(page);
    m_listParam.SetSearchTarget(searchTarget);
    m_listParam.SetKeyword(reinterpret_cast<const char16_t*>(keyword.data()));
    m_packet = Disassemble(
        m_packet, "SSSSS", 0x40, scratch1.data(), 0x40, scratch2.data(), 0x1f4, m_receiverList.data(), 0x190, m_title.data(),
        0x1770, m_content.data()
    );
}

void DeleteMail::ExecuteInternal() {
    L2_GUARD_FUNCTION();
    db.RequestDeleteMail(m_user, m_mailId, m_listParam);
}

void DeleteMail::ParseParameter() {
    L2_GUARD_FUNCTION();
    int mailBoxType = 0;
    ParseHTMLParam(m_cmd, const_cast<char*>("dd"), &m_mailId, &mailBoxType);
    m_listParam.SetMailBoxType(mailBoxType);
}

void ArchiveMail::ExecuteInternal() {
    L2_GUARD_FUNCTION();
    db.RequestArchiveMail(m_user, m_mailId, m_listParam);
}

void ArchiveMail::ParseParameter() {
    L2_GUARD_FUNCTION();
    int mailBoxType = 0;
    int page = 0;
    int searchTarget = 0;
    std::array<SharedUtilsWideChar, 21> keyword{};
    ParseHTMLParam(m_cmd, const_cast<char*>("ddddS"), &m_mailId, &mailBoxType, &page, &searchTarget, 0x2a, keyword.data());
    m_listParam.SetMailBoxType(mailBoxType);
    m_listParam.SetPage(page);
    m_listParam.SetSearchTarget(searchTarget);
    m_listParam.SetKeyword(reinterpret_cast<const char16_t*>(keyword.data()));
}

void SearchMail::ExecuteInternal() {
    L2_GUARD_FUNCTION();
    db.RequestMailList(m_user, m_listParam);
}

void SearchMail::ParseParameter() {
    L2_GUARD_FUNCTION();
    int mailBoxType = 0;
    std::array<SharedUtilsWideChar, 10> searchTarget{};
    std::array<SharedUtilsWideChar, 21> keyword{};
    std::array<SharedUtilsWideChar, 32> scratch1;
    std::array<SharedUtilsWideChar, 32> scratch2;
    ParseHTMLParam(m_cmd, const_cast<char*>("d"), &mailBoxType);
    m_packet = Disassemble(
        m_packet, "SSSSS", 0x40, scratch1.data(), 0x40, scratch2.data(), 0x14, searchTarget.data(), 0x14,
        searchTarget.data(), 0x2a, keyword.data()
    );
    m_listParam.SetMailBoxType(mailBoxType);
    m_listParam.SetSearchTarget(EqualsTargetLiteral(searchTarget.data(), u"Title") ? 1 : 0);
    const std::u16string trimmed = Trim(keyword.data());
    m_listParam.SetKeyword(trimmed.c_str());
}

void WritePledgeMailForm::ExecuteInternal() {
    // Current L2Server 0x0079cd2c.
    L2_GUARD_FUNCTION();
    if (!m_user->IsPledgeMaster() || m_user->GetPledge()->GetDbId() != static_cast<std::uint32_t>(m_pledgeId)) {
        m_userSocket->SendSystemMessage(SystemMessageId::ONLY_CLAN_LEADER_CAN_ACT);
        std::u16string command = FormatString(u"_%s_%d", u"_clbbsclan", m_pledgeId);
        ClanBBS(m_userSocket, command.c_str());
        return;
    }

    std::array<char, 0x5000> packet{};
    Assemble(packet.data(), static_cast<int>(packet.size()), "dd", m_user->GetServerId(), m_pledgeId);
    WritePledgeMailFormHtmlBuilder builder(nullptr, reinterpret_cast<const std::uint8_t*>(packet.data()));
    builder.BuildHtml();
}

void WritePledgeMailForm::ParseParameter() {
    L2_GUARD_FUNCTION();
    ParseHTMLParam(m_cmd, const_cast<char*>("d"), &m_pledgeId);
}

void WriteFriendMailForm::ExecuteInternal() {
    L2_GUARD_FUNCTION();
    std::array<char, 0x5000> packet;
    if (m_friendCount < 1) {
        Assemble(packet.data(), static_cast<int>(packet.size()), "dd", m_user->GetServerId(), 0);
        ListFriendHtmlBuilder builder(nullptr, reinterpret_cast<const std::uint8_t*>(packet.data()));
        builder.BuildHtml();
        return;
    }

    Assemble(
        packet.data(), static_cast<int>(packet.size()), "ddb", m_user->GetServerId(), m_friendCount, m_bufLen, m_buf.data()
    );
    WriteFriendMailFormHtmlBuilder builder(nullptr, reinterpret_cast<const std::uint8_t*>(packet.data()));
    builder.BuildHtml();
}

void WriteFriendMailForm::ParseParameter() {
    L2_GUARD_FUNCTION();
    m_cmd = ParseHTMLParam2(m_cmd, const_cast<char*>("d"), &m_friendCount);
    for (int index = 0; index < m_friendCount; ++index) {
        int friendDbId;
        m_cmd = ParseHTMLParam2(m_cmd, const_cast<char*>("d"), &friendDbId);
        const int assembled = Assemble(
            reinterpret_cast<char*>(m_buf.data()) + m_bufLen, static_cast<int>(m_buf.size()) - m_bufLen, "d", friendDbId
        );
        if (assembled > 0) {
            m_bufLen += assembled;
        }
    }
}

void ListFriend::ExecuteInternal() {
    L2_GUARD_FUNCTION();
    std::array<char, 0x5000> packet;
    Assemble(
        packet.data(), static_cast<int>(packet.size()), "ddb", m_user->GetServerId(), m_selectedFriendCount, m_bufLen,
        m_buf.data()
    );
    ListFriendHtmlBuilder builder(nullptr, reinterpret_cast<const std::uint8_t*>(packet.data()));
    builder.BuildHtml();
}

void ListFriend::ParseParameter() {
    L2_GUARD_FUNCTION();
    ParseSelectedFriendCount();
    ParseSelectedFriendIdList();
}

void ListFriend::ParseSelectedFriendCount() {
    m_cmd = ParseHTMLParam2(m_cmd, const_cast<char*>("d"), &m_selectedFriendCount);
}

void ListFriend::ParseSelectedFriendIdList() {
    m_bufLen = 0;
    for (int index = 0; index < m_selectedFriendCount; ++index) {
        if (index > 4) {
            return;
        }

        int friendDbId;
        m_cmd = ParseHTMLParam2(m_cmd, const_cast<char*>("d"), &friendDbId);
        const int assembled = Assemble(
            reinterpret_cast<char*>(m_buf.data()) + m_bufLen, static_cast<int>(m_buf.size()) - m_bufLen, "d", friendDbId
        );
        if (assembled > 0) {
            m_bufLen += assembled;
        }
    }
}

void DeleteFriend::ExecuteInternal() {
    L2_GUARD_FUNCTION();
    std::vector<int> friendDbIds;
    int friendDbId = 0;
    for (int index = 0; index < m_selectedFriendCount; ++index) {
        if (index > 4) {
            break;
        }
        m_cmd = ParseHTMLParam2(m_cmd, const_cast<char*>("d"), &friendDbId);
        friendDbIds.push_back(friendDbId);
    }
    db.RequestBreakFriendshipBatch(m_user, friendDbIds);
}

void DeleteFriend::ParseParameter() {
    L2_GUARD_FUNCTION();
    m_cmd = ParseHTMLParam2(m_cmd, const_cast<char*>("d"), &m_selectedFriendCount);
}

void DeleteAllFriendConfirm::ExecuteInternal() {
    L2_GUARD_FUNCTION();
    ListFriendHtmlBuilder::ShowDeleteAllFriendHtml(m_user->GetServerId());
}

void DeleteAllFriendConfirm::ParseParameter() {
    L2_GUARD_FUNCTION();
}

void DeleteAllFriend::ExecuteInternal() {
    L2_GUARD_FUNCTION();
    CSPointer<CFriends> friends = m_user->GetFriends();
    if (friends) {
        std::vector<int> friendDbIds;
        friends->BuildFriendDBIDList(friendDbIds);
        if (!friendDbIds.empty()) {
            db.RequestBreakFriendshipBatch(m_user, friendDbIds);
            return;
        }
    }
    ListFriendHtmlBuilder::Show(m_user->GetServerId());
}

void DeleteAllFriend::ParseParameter() {
    L2_GUARD_FUNCTION();
}

void ListBlock::ExecuteInternal() {
    L2_GUARD_FUNCTION();
    ListBlockHtmlBuilder::Show(m_user->GetServerId());
}

void ListBlock::ParseParameter() {
    L2_GUARD_FUNCTION();
}

void AddBlock::ExecuteInternal() {
    L2_GUARD_FUNCTION();
    std::array<char, 0x5000> packet;
    Assemble(packet.data(), static_cast<int>(packet.size()), "dS", 100, m_blockName.data());
    RequestBlockPacket(m_socket, reinterpret_cast<const std::uint8_t*>(packet.data()));
}

void AddBlock::ParseParameter() {
    L2_GUARD_FUNCTION();
    std::array<SharedUtilsWideChar, 32> scratch1;
    std::array<SharedUtilsWideChar, 32> scratch2;
    m_packet = Disassemble(
        m_packet, "SSSSS", 0x40, scratch1.data(), 0x40, scratch2.data(), 0x64, m_blockName.data(), 0x40, scratch1.data(),
        0x40, scratch2.data()
    );
}

void DeleteBlock::ExecuteInternal() {
    L2_GUARD_FUNCTION();
    const std::u16string targetName = m_user->GetBlockCharInfo(m_blockDbId, true);
    db.RequestDeleteBlockList(m_user, targetName.c_str());
    m_user->DeleteBlockList(m_blockDbId);
    ListBlockHtmlBuilder::Show(m_user->GetServerId());
}

void DeleteBlock::ParseParameter() {
    L2_GUARD_FUNCTION();
    m_cmd = ParseHTMLParam2(m_cmd, const_cast<char*>("d"), &m_blockDbId);
}

void DeleteAllBlockConfirm::ExecuteInternal() {
    L2_GUARD_FUNCTION();
    ListBlockHtmlBuilder::ShowDeleteAllBlockHtml(m_user->GetServerId());
}

void DeleteAllBlockConfirm::ParseParameter() {
    L2_GUARD_FUNCTION();
}

void DeleteAllBlock::ExecuteInternal() {
    L2_GUARD_FUNCTION();
    CPacketHelperEx<20480> packet;
    const int blockCount = m_user->GetBlockCharList(packet);
    const std::uint8_t* cursor = reinterpret_cast<const std::uint8_t*>(packet.GetPacket());
    int blockDbId = 0;
    std::array<SharedUtilsWideChar, 50> blockName{};
    for (int index = 0; index < blockCount; ++index) {
        cursor = Disassemble(cursor, "dS", &blockDbId, 100, blockName.data());
        m_blockDbIdList.push_back(blockDbId);
        m_blockNameList.push_back(ToTargetStringFromPacket(blockName.data()));
    }
    if (blockCount > 0) {
        db.RequestDeleteBlockListBatch(m_user, m_blockDbIdList, m_blockNameList);
        return;
    }
    ListBlockHtmlBuilder::Show(m_user->GetServerId());
}

void DeleteAllBlock::ParseParameter() {
    L2_GUARD_FUNCTION();
}

void WritePledgeAnnounceForm::ExecuteInternal() {
    // Current L2Server 0x0079d680.
    L2_GUARD_FUNCTION();
    if (!m_user->IsPledgeMaster() || m_user->GetPledge()->GetDbId() != m_pledgeDbId) {
        m_userSocket->SendSystemMessage(SystemMessageId::ONLY_CLAN_LEADER_CAN_ACT);
        std::u16string command = FormatString(u"_%s_%d", u"_clbbsclan", m_pledgeDbId);
        ClanBBS(m_userSocket, command.c_str());
        return;
    }

    CPledgeAnnounceSP announcement = CPledgeAnnounceRegistry::inst().Get(m_pledgeDbId);
    if (!announcement) {
        db.RequestGetPledgeAnnounce(m_user, m_pledgeDbId, 0);
        return;
    }
    PledgeAnnounceHtmlBuilder::Show(
        m_user->GetServerId(), m_pledgeDbId, announcement->DoesShow() ? 1 : 0,
        reinterpret_cast<SharedUtilsWideChar*>(announcement->GetContent().data())
    );
}

void WritePledgeAnnounceForm::ParseParameter() {
    L2_GUARD_FUNCTION();
    m_cmd = ParseHTMLParam2(m_cmd, const_cast<char*>("d"), &m_pledgeDbId);
}

void WritePledgeAnnounce::ExecuteInternal() {
    // Current L2Server 0x007999a4.
    L2_GUARD_FUNCTION();
    if (!m_user->IsPledgeMaster() || m_user->GetPledge()->GetDbId() != m_pledgeDbId) {
        m_userSocket->SendSystemMessage(SystemMessageId::ONLY_CLAN_LEADER_CAN_ACT);
        std::u16string command = FormatString(u"_%s_%d", u"_clbbsclan", m_pledgeDbId);
        ClanBBS(m_userSocket, command.c_str());
        return;
    }
    const std::u16string content(FixedUtf16View(m_content));
    db.RequestSetPledgeAnnounce(m_user, m_pledgeDbId, m_doesShow, content.data());
}

void WritePledgeAnnounce::ParseParameter() {
    L2_GUARD_FUNCTION();
    std::array<SharedUtilsWideChar, 32> scratch1;
    std::array<SharedUtilsWideChar, 32> scratch2;
    ParseHTMLParam(m_cmd, const_cast<char*>("dd"), &m_pledgeDbId, &m_doesShow);
    m_packet = Disassemble(m_packet, "SSS", 0x40, scratch1.data(), 0x40, scratch2.data(), 0x1772, m_content.data());
}

void SwitchPledgeAnnounceShowFlag::ExecuteInternal() {
    // Current L2Server 0x00799af0.
    L2_GUARD_FUNCTION();
    if (!m_user->IsPledgeMaster() || m_user->GetPledge()->GetDbId() != m_pledgeDbId) {
        m_userSocket->SendSystemMessage(SystemMessageId::ONLY_CLAN_LEADER_CAN_ACT);
        std::u16string command = FormatString(u"_%s_%d", u"_clbbsclan", m_pledgeDbId);
        ClanBBS(m_userSocket, command.c_str());
        return;
    }
    db.RequestSwitchPledgeAnnounce(m_user, m_pledgeDbId, m_doesShow);
}

void SwitchPledgeAnnounceShowFlag::ParseParameter() {
    L2_GUARD_FUNCTION();
    m_cmd = ParseHTMLParam2(m_cmd, const_cast<char*>("dd"), &m_pledgeDbId, &m_doesShow);
}

void ViewCastleList::ExecuteInternal() {
    L2_GUARD_FUNCTION();
    CastleListHtmlBuilder::Show(m_user->GetServerId(), m_user->m_nCommLocalCommunityBid);
}

void ViewCastleList::ParseParameter() {}

void ViewCastleInfo::ExecuteInternal() {
    L2_GUARD_FUNCTION();
    CastleInfoHtmlBuilder::Show(m_user->GetServerId(), m_castleId, m_user->m_nCommLocalCommunityBid);
}

void ViewCastleInfo::ParseParameter() {
    L2_GUARD_FUNCTION();
    m_cmd = ParseHTMLParam2(m_cmd, const_cast<char*>("d"), &m_castleId);
}
