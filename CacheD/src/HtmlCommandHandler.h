#pragma once

#include "Shared/src/MailListParameter.h"
#include "Shared/src/PledgeAnnounce.h"
#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CSocket;


// Current Cached.exe vftable 0x0072dd58 fixes this four-slot hierarchy and
// the base object extent. The current debug material does not keep its
// translation-unit path. HtmlCommandHandler.{h,cpp} is therefore a semantic
// portable owner, not a claimed original filename.
class CHtmlCommandHandler {
public:
    CHtmlCommandHandler(CSocket* socket, const unsigned char* packet);
    virtual ~CHtmlCommandHandler();

    virtual bool HandleInternal() = 0;
    virtual bool DisassemblePacket() = 0;
    virtual bool SendReply() = 0;

protected:

    // Current Cached.exe 0x004dc7ac. The current PDB does not keep the
    // original identifier, so the portable name records the recovered
    // behavior instead of claiming an original spelling.
    char16_t* MaskSqlApostrophes(char16_t* text);

    std::array<char, 0x5000> m_replyBuffer; // +0x0008
    std::int32_t m_replyLength; // +0x5008
    CSocket* m_socket; // +0x5010
    const unsigned char* m_packet; // +0x5018
};

// Current stack value consumed by CountMail::Execute 0x004e0a54.
class CountMail {
public:
    explicit CountMail(std::int32_t characterId) noexcept;
    void Execute();

    std::int32_t m_characterId;
    std::int32_t m_incomingCount;
    std::int32_t m_sentCount;
    std::int32_t m_archivedCount;
    std::int32_t m_temporaryCount;
};

class ListMailHtmlCommandHandler final : public CHtmlCommandHandler {
public:
    ListMailHtmlCommandHandler(CSocket* socket, const unsigned char* packet);
    // Current scalar deleting destructor 0x0052eac0 finalizes the embedded
    // CMailListParameter at +0x5030 and then the CHtmlCommandHandler base.
    ~ListMailHtmlCommandHandler() override = default;

    bool HandleInternal() override;
    bool DisassemblePacket() override;
    bool SendReply() override;

private:

    std::int32_t m_characterId; // +0x5020
    std::int32_t m_userSocketId; // +0x5024
    std::int32_t m_rowCount; // +0x5028
    std::int32_t m_totalCount; // +0x502c
    CMailListParameter m_listParameter; // +0x5030
};

class ReadMailHtmlCommandHandler final : public CHtmlCommandHandler {
public:
    ReadMailHtmlCommandHandler(CSocket* socket, const unsigned char* packet);
    // Current scalar deleting destructor 0x0052eb20 finalizes the embedded
    // CMailListParameter at +0x515c and then the CHtmlCommandHandler base.
    ~ReadMailHtmlCommandHandler() override = default;

    bool HandleInternal() override;
    bool DisassemblePacket() override;
    bool SendReply() override;

private:

    std::int32_t m_characterId; // +0x5020
    std::int32_t m_userSocketId; // +0x5024
    std::int32_t m_mailId; // +0x5028
    std::int32_t m_senderCharacterId; // +0x502c
    std::array<char16_t, 150> m_title; // +0x5030
    CMailListParameter m_listParameter; // +0x515c
};

class SendMailHtmlCommandHandler final : public CHtmlCommandHandler {
public:
    SendMailHtmlCommandHandler(CSocket* socket, const unsigned char* packet);
    // Current Cached.exe destructor body 0x0059034c.
    ~SendMailHtmlCommandHandler() override = default;

    bool HandleInternal() override;
    bool DisassemblePacket() override;
    bool SendReply() override;

private:

    bool ParseReceiverList();
    bool CheckSendMailLimit();
    bool CheckBlockList();
    std::u16string GetReceiverNameList() const;
    void SendMail();
    void SendSuccessReply();
    void SendFailureReply();

    std::int32_t m_characterId; // +0x5020
    std::int32_t m_userSocketId; // +0x5024
    std::int32_t m_relatedId; // +0x5028
    std::int32_t m_mailType; // +0x502c
    std::array<char16_t, 250> m_receivers; // +0x5030
    std::array<char16_t, 200> m_title; // +0x5224
    std::array<char16_t, 3000> m_content; // +0x53b4
    std::vector<std::u16string> m_receiverNames; // +0x6b28
    std::vector<std::int32_t> m_receiverIds; // +0x6b48
    bool m_success; // +0x6b68
    [[maybe_unused]] std::array<std::uint8_t, 3> m_alignParameter{};
    CMailListParameter m_listParameter; // +0x6b6c
    std::int32_t m_result; // +0x6ba8
    std::array<char16_t, 250> m_failureName; // +0x6bac
};

class SaveMailHtmlCommandHandler final : public CHtmlCommandHandler {
public:
    SaveMailHtmlCommandHandler(CSocket* socket, const unsigned char* packet);
    // Current scalar deleting destructor 0x0052eb80 finalizes the embedded
    // CMailListParameter at +0x6b24 and then the CHtmlCommandHandler base.
    ~SaveMailHtmlCommandHandler() override = default;

    bool HandleInternal() override;
    bool DisassemblePacket() override;
    bool SendReply() override;

private:

    bool CheckSaveLimit();
    void SendSuccessReply();
    void SendFailureReply();

    std::int32_t m_characterId; // +0x5020
    std::int32_t m_userSocketId; // +0x5024
    std::int32_t m_relatedId; // +0x5028
    std::int32_t m_mailType; // +0x502c
    std::array<char16_t, 250> m_receivers; // +0x5030
    std::array<char16_t, 200> m_title; // +0x5224
    std::array<char16_t, 3000> m_content; // +0x53b4
    CMailListParameter m_listParameter; // +0x6b24
    bool m_success; // +0x6b60
};

class GetTempMailHtmlCommandHandler final : public CHtmlCommandHandler {
public:
    GetTempMailHtmlCommandHandler(CSocket* socket, const unsigned char* packet);
    // Current Cached.exe folds this scalar deleting destructor, and the four
    // equivalent ones below, to 0x0052ebe0. It finalizes the embedded
    // CMailListParameter at +0x502c and then the CHtmlCommandHandler base.
    ~GetTempMailHtmlCommandHandler() override = default;

    bool HandleInternal() override;
    bool DisassemblePacket() override;
    bool SendReply() override;

private:

    std::int32_t m_characterId; // +0x5020
    std::int32_t m_userSocketId; // +0x5024
    std::int32_t m_mailId; // +0x5028
    CMailListParameter m_listParameter; // +0x502c
};

class ReplyMailHtmlCommandHandler final : public CHtmlCommandHandler {
public:
    ReplyMailHtmlCommandHandler(CSocket* socket, const unsigned char* packet);
    // Current folded scalar deleting destructor 0x0052ebe0.
    ~ReplyMailHtmlCommandHandler() override = default;

    bool HandleInternal() override;
    bool DisassemblePacket() override;
    bool SendReply() override;

private:

    std::int32_t m_characterId; // +0x5020
    std::int32_t m_userSocketId; // +0x5024
    std::int32_t m_mailId; // +0x5028
    CMailListParameter m_listParameter; // +0x502c
};

class ForwardMailHtmlCommandHandler final : public CHtmlCommandHandler {
public:
    ForwardMailHtmlCommandHandler(CSocket* socket, const unsigned char* packet);
    // Current folded scalar deleting destructor 0x0052ebe0.
    ~ForwardMailHtmlCommandHandler() override = default;

    bool HandleInternal() override;
    bool DisassemblePacket() override;
    bool SendReply() override;

private:

    std::int32_t m_characterId; // +0x5020
    std::int32_t m_userSocketId; // +0x5024
    std::int32_t m_mailId; // +0x5028
    CMailListParameter m_listParameter; // +0x502c
};

class ModifyTempMailHtmlCommandHandler final : public CHtmlCommandHandler {
public:
    ModifyTempMailHtmlCommandHandler(CSocket* socket, const unsigned char* packet);
    ~ModifyTempMailHtmlCommandHandler() override = default;

    bool HandleInternal() override;
    bool DisassemblePacket() override;
    bool SendReply() override;

private:

    std::int32_t m_characterId; // +0x5020
    std::int32_t m_userSocketId; // +0x5024
    std::int32_t m_mailId; // +0x5028
    std::array<char16_t, 250> m_receivers; // +0x502c
    std::array<char16_t, 200> m_title; // +0x5220
    std::array<char16_t, 3000> m_content; // +0x53b0
};

class ModifySendTempMailHtmlCommandHandler final : public CHtmlCommandHandler {
public:
    ModifySendTempMailHtmlCommandHandler(CSocket* socket, const unsigned char* packet);
    // Current Cached.exe destructor body 0x005904dc.
    ~ModifySendTempMailHtmlCommandHandler() override = default;

    bool HandleInternal() override;
    bool DisassemblePacket() override;
    bool SendReply() override;

private:

    bool ParseReceiverList();
    bool CheckSendMailLimit();
    bool CheckBlockList();
    std::u16string GetReceiverNameList() const;
    void SendMail();
    void SendSuccessReply();
    void SendFailureReply();

    std::int32_t m_characterId; // +0x5020
    std::int32_t m_userSocketId; // +0x5024
    std::int32_t m_mailId; // +0x5028
    std::array<char16_t, 250> m_receivers; // +0x502c
    std::array<char16_t, 200> m_title; // +0x5220
    std::array<char16_t, 3000> m_content; // +0x53b0
    CMailListParameter m_listParameter; // +0x6b20
    [[maybe_unused]] std::array<std::uint8_t, 4> m_alignNames{};
    std::vector<std::u16string> m_receiverNames; // +0x6b60
    std::vector<std::int32_t> m_receiverIds; // +0x6b80
    bool m_success; // +0x6ba0
    [[maybe_unused]] std::array<std::uint8_t, 3> m_alignResult{};
    std::int32_t m_result; // +0x6ba4
    std::array<char16_t, 250> m_failureName; // +0x6ba8
};

class DeleteMailHtmlCommandHandler final : public CHtmlCommandHandler {
public:
    DeleteMailHtmlCommandHandler(CSocket* socket, const unsigned char* packet);
    // Current folded scalar deleting destructor 0x0052ebe0.
    ~DeleteMailHtmlCommandHandler() override = default;

    bool HandleInternal() override;
    bool DisassemblePacket() override;
    bool SendReply() override;

private:

    std::int32_t m_characterId; // +0x5020
    std::int32_t m_userSocketId; // +0x5024
    std::int32_t m_mailId; // +0x5028
    CMailListParameter m_listParameter; // +0x502c
};

class ArchiveMailHtmlCommandHandler final : public CHtmlCommandHandler {
public:
    ArchiveMailHtmlCommandHandler(CSocket* socket, const unsigned char* packet);
    // Current folded scalar deleting destructor 0x0052ebe0.
    ~ArchiveMailHtmlCommandHandler() override = default;

    bool HandleInternal() override;
    bool DisassemblePacket() override;
    bool SendReply() override;

private:

    bool CheckArchiveLimit();
    void SendSuccessReply();
    void SendFailureReply();

    std::int32_t m_characterId; // +0x5020
    std::int32_t m_userSocketId; // +0x5024
    std::int32_t m_mailId; // +0x5028
    CMailListParameter m_listParameter; // +0x502c
    bool m_success; // +0x5068
};

class UnreadMailCountCommandHandler final : public CHtmlCommandHandler {
public:
    UnreadMailCountCommandHandler(CSocket* socket, const unsigned char* packet);
    ~UnreadMailCountCommandHandler() override = default;

    bool HandleInternal() override;
    bool DisassemblePacket() override;
    bool SendReply() override;

private:

    std::int32_t m_characterId; // +0x5020
    std::int32_t m_userSocketId; // +0x5024
    std::int32_t m_unreadCount; // +0x5028
};

class SendPledgeMailHtmlCommandHandler final : public CHtmlCommandHandler {
public:
    SendPledgeMailHtmlCommandHandler(CSocket* socket, const unsigned char* packet);
    ~SendPledgeMailHtmlCommandHandler() override = default;

    bool HandleInternal() override;
    bool DisassemblePacket() override;
    bool SendReply() override;

private:

    bool CheckPledge();
    bool CheckSendMailLimit();
    void SendMail();
    void SendSuccessReply();
    void SendFailureReply();

    std::int32_t m_characterId; // +0x5020
    std::int32_t m_userSocketId; // +0x5024
    std::array<char16_t, 50> m_pledgeName; // +0x5028
    std::array<char16_t, 200> m_title; // +0x508c
    std::array<char16_t, 3000> m_content; // +0x521c
    bool m_success; // +0x698c
    [[maybe_unused]] std::array<std::uint8_t, 3> m_alignResult{};
    std::int32_t m_result; // +0x6990
};

class DeleteFriendBatchHtmlCommandHandler final : public CHtmlCommandHandler {
public:
    DeleteFriendBatchHtmlCommandHandler(CSocket* socket, const unsigned char* packet);
    // Current Cached.exe complete destructor body 0x00543eb8. Scalar deleting
    // destructor wrapper 0x00547e7c.
    ~DeleteFriendBatchHtmlCommandHandler() override = default;

    bool HandleInternal() override;
    bool DisassemblePacket() override;
    bool SendReply() override;

private:

    std::int32_t m_characterId; // +0x5020
    std::int32_t m_userSocketId; // +0x5024
    std::vector<std::int32_t> m_succeededIds; // +0x5028
    std::vector<std::int32_t> m_failedIds; // +0x5048
};

class DeleteBlockBatchHtmlCommandHandler final : public CHtmlCommandHandler {
public:
    DeleteBlockBatchHtmlCommandHandler(CSocket* socket, const unsigned char* packet);
    // Current Cached.exe destructor body 0x005905e4.
    ~DeleteBlockBatchHtmlCommandHandler() override = default;

    bool HandleInternal() override;
    bool DisassemblePacket() override;
    bool SendReply() override;

private:

    std::int32_t m_characterId; // +0x5020
    std::int32_t m_userSocketId; // +0x5024
    std::vector<std::int32_t> m_results; // +0x5028
    std::vector<std::u16string> m_names; // +0x5048
};

class GetPledgeAnnounceHtmlCommandHandler final : public CHtmlCommandHandler {
public:
    GetPledgeAnnounceHtmlCommandHandler(CSocket* socket, const unsigned char* packet);
    ~GetPledgeAnnounceHtmlCommandHandler() override;

    bool HandleInternal() override;
    bool DisassemblePacket() override;
    bool SendReply() override;

private:

    std::int32_t m_userSocketId; // +0x5020
    std::int32_t m_pledgeDbId; // +0x5024
    std::int32_t m_viewType; // +0x5028
    CPledgeAnnounceSP m_pledgeAnnounce; // +0x5030
};

class SetPledgeAnnounceHtmlCommandHandler final : public CHtmlCommandHandler {
public:
    SetPledgeAnnounceHtmlCommandHandler(CSocket* socket, const unsigned char* packet);
    ~SetPledgeAnnounceHtmlCommandHandler() override = default;

    bool HandleInternal() override;
    bool DisassemblePacket() override;
    bool SendReply() override;

private:

    std::int32_t m_userSocketId; // +0x5020
    std::int32_t m_pledgeDbId; // +0x5024
    std::int32_t m_showFlag; // +0x5028
    std::array<char16_t, 3001> m_content; // +0x502c
    bool m_success; // +0x679e
};

class SwitchPledgeAnnounceShowFlagHtmlCommandHandler final : public CHtmlCommandHandler {
public:
    SwitchPledgeAnnounceShowFlagHtmlCommandHandler(CSocket* socket, const unsigned char* packet);
    ~SwitchPledgeAnnounceShowFlagHtmlCommandHandler() override = default;

    bool HandleInternal() override;
    bool DisassemblePacket() override;
    bool SendReply() override;

private:

    std::int32_t m_userSocketId; // +0x5020
    std::int32_t m_pledgeDbId; // +0x5024
    std::int32_t m_showFlag; // +0x5028
    bool m_success; // +0x502c
};
