#pragma once

#include "HtmlGenerator.h"

#include "Shared/src/MailListParameter.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/Utils.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CDBSocket;
class CUserSocket;
class CCastle;
class User;
enum eSMsgType : std::int32_t;


// L2Server/HtmlBuilder.{h,cpp} is the current physical owner. Target WCHAR
// buffers use SharedUtilsWideChar so their Win64 UTF-16 extents remain exact
// on hosts whose native wchar_t is four bytes.
class CHtmlBuilder {
public:
    CHtmlBuilder(const CHtmlBuilder&) = default;
    CHtmlBuilder(CDBSocket* dbSocket, const unsigned char* packet);
    virtual ~CHtmlBuilder();

    void BuildHtml() {
        DisassemblePacket();
        SetTemplateFileName(GetTemplateFileName());
        BuildHtmlInternal();
        SendHtml();
    }

protected:
    virtual void DisassemblePacket() = 0;
    virtual void BuildHtmlInternal() = 0;
    virtual void SendHtml() = 0;
    virtual const char16_t* GetTemplateFileName() { return nullptr; }

    static CUserSocket* GetUserSocket(int userServerId);
    static User* GetUser(int userServerId);

    std::u16string BuildHtmlDataPacket(
        int htmlType,
        SharedUtilsWideChar* receiverNameList,
        SharedUtilsWideChar* title,
        SharedUtilsWideChar* content,
        int convertOption
    );
    void BuildTreeLinkHtml(SharedUtilsWideChar* treeLink);

    static void SendSystemMsg(int userServerId, int messageId, eSMsgType type, SharedUtilsWideChar* value);
    static void SendSystemMsg(int userServerId, int messageId, int value);
    static void SendSystemMsg(int userServerId, int messageId);
    static void SendSystemMsg(int userServerId, SharedUtilsWideChar* value);

    CDBSocket* m_dbSocket; // +0x08
    const unsigned char* m_packet; // +0x10
    const unsigned char* m_remainPacket; // +0x18
    CHtmlGenerator m_htmlGenerator; // +0x20..+0x97

private:
    void SetTemplateFileName(const char16_t* templateFileName);
};

class ListMailHtmlBuilder final : public CHtmlBuilder {
public:
    ListMailHtmlBuilder(CDBSocket* dbSocket, const unsigned char* packet) : CHtmlBuilder(dbSocket, packet) {}
    ~ListMailHtmlBuilder() override = default;

protected:
    void DisassemblePacket() override;
    void BuildHtmlInternal() override;
    void SendHtml() override;
    const char16_t* GetTemplateFileName() override;

private:
    void BuildTreeLink();
    void BuildMailBoxLink();
    void BuildMailListHeader();
    void BuildMailListLink();
    void BuildWriteButton();
    void BuildPageIndexLink();
    int GetTotalPage();

    std::int32_t m_userServerId; // +0x98
    std::int32_t m_incommingMailCount; // +0x9c
    std::int32_t m_sentMailCount; // +0xa0
    std::int32_t m_archivedMailCount; // +0xa4
    std::int32_t m_tempMailCount; // +0xa8
    CMailListParameter m_listParam; // +0xac..+0xe7
    std::int32_t m_count; // +0xe8
    std::int32_t m_totalCount; // +0xec
};

class ReadMailHtmlBuilder final : public CHtmlBuilder {
public:
    ReadMailHtmlBuilder(CDBSocket* dbSocket, const unsigned char* packet) : CHtmlBuilder(dbSocket, packet) {}
    ~ReadMailHtmlBuilder() override = default;

protected:
    void DisassemblePacket() override;
    void BuildHtmlInternal() override;
    void SendHtml() override;
    const char16_t* GetTemplateFileName() override;

private:
    void Build1Packet();
    void Build2Packet();

    std::int32_t m_userServerId; // +0x98
    std::int32_t m_mailId; // +0x9c
    std::int32_t m_senderCharId; // +0xa0
    std::array<SharedUtilsWideChar, 50> m_senderCharName; // +0xa4..+0x107
    std::array<SharedUtilsWideChar, 250> m_receiverNameList; // +0x108..+0x2fb
    std::array<SharedUtilsWideChar, 200> m_title; // +0x2fc..+0x48b
    std::array<SharedUtilsWideChar, 4000> m_content; // +0x48c..+0x23cb
    std::int32_t m_sendDate; // +0x23cc
    std::int32_t m_mailType; // +0x23d0
    CMailListParameter m_listParam; // +0x23d4..+0x240f
    std::u16string m_htmlData; // +0x2410..+0x2437
};

class SendMailHtmlBuilder final : public CHtmlBuilder {
public:
    SendMailHtmlBuilder(CDBSocket* dbSocket, const unsigned char* packet) : CHtmlBuilder(dbSocket, packet) {}
    ~SendMailHtmlBuilder() override = default;

protected:
    void DisassemblePacket() override;
    void BuildHtmlInternal() override;
    void SendHtml() override;
    const char16_t* GetTemplateFileName() override;

private:
    void Build1001Packet();
    void Build1002Packet();
    void SendMsg();

    std::int32_t m_userServerId; // +0x98
    std::int32_t m_sentMailCount; // +0x9c
    std::u16string m_htmlData; // +0xa0..+0xc7
    CMailListParameter m_listParam; // +0xc8..+0x103
};

class SaveMailHtmlBuilder final : public CHtmlBuilder {
public:
    SaveMailHtmlBuilder(CDBSocket* dbSocket, const unsigned char* packet) : CHtmlBuilder(dbSocket, packet) {}
    ~SaveMailHtmlBuilder() override = default;

protected:
    void DisassemblePacket() override;
    void BuildHtmlInternal() override;
    void SendHtml() override;
    const char16_t* GetTemplateFileName() override;

private:
    bool IsMailSaveSuccess();
    void Build1001Packet();
    void Build1002Packet();

    std::int32_t m_userServerId; // +0x98
    std::int32_t m_errorType; // +0x9c
    std::u16string m_htmlData; // +0xa0..+0xc7
    CMailListParameter m_listParam; // +0xc8..+0x103
};

class ModifyTempMailFormHtmlBuilder final : public CHtmlBuilder {
public:
    ModifyTempMailFormHtmlBuilder(CDBSocket* dbSocket, const unsigned char* packet) :
        CHtmlBuilder(dbSocket, packet), m_userServerId(0) {}
    ~ModifyTempMailFormHtmlBuilder() override = default;

protected:
    void DisassemblePacket() override;
    void BuildHtmlInternal() override;
    void SendHtml() override;
    const char16_t* GetTemplateFileName() override;

private:
    void Build1001Packet();
    void Build1002Packet();

    std::int32_t m_userServerId; // +0x98
    CMailListParameter m_listParam; // +0x9c..+0xd7
    std::int32_t m_mailId; // +0xd8
    std::array<SharedUtilsWideChar, 250> m_receiverNameList; // +0xdc..+0x2cf
    std::array<SharedUtilsWideChar, 200> m_title; // +0x2d0..+0x45f
    std::array<SharedUtilsWideChar, 4000> m_content; // +0x460..+0x239f
    std::u16string m_htmlData; // +0x23a0..+0x23c7
};

class ReplyMailFormHtmlBuilder final : public CHtmlBuilder {
public:
    ReplyMailFormHtmlBuilder(CDBSocket* dbSocket, const unsigned char* packet) :
        CHtmlBuilder(dbSocket, packet), m_userServerId(0) {}
    ~ReplyMailFormHtmlBuilder() override = default;

protected:
    void DisassemblePacket() override;
    void BuildHtmlInternal() override;
    void SendHtml() override;
    const char16_t* GetTemplateFileName() override;

private:
    void Build1001Packet();
    void Build1002Packet();

    std::int32_t m_userServerId; // +0x98
    CMailListParameter m_listParam; // +0x9c..+0xd7
    std::int32_t m_mailId; // +0xd8
    std::array<SharedUtilsWideChar, 50> m_senderCharName; // +0xdc..+0x13f
    std::array<SharedUtilsWideChar, 250> m_receiverNameList; // +0x140..+0x333
    std::array<SharedUtilsWideChar, 200> m_title; // +0x334..+0x4c3
    std::array<SharedUtilsWideChar, 4000> m_content; // +0x4c4..+0x2403
    std::u16string m_htmlData; // +0x2408..+0x242f
};

class ForwardMailFormHtmlBuilder final : public CHtmlBuilder {
public:
    ForwardMailFormHtmlBuilder(CDBSocket* dbSocket, const unsigned char* packet) :
        CHtmlBuilder(dbSocket, packet), m_userServerId(0) {}
    ~ForwardMailFormHtmlBuilder() override = default;

protected:
    void DisassemblePacket() override;
    void BuildHtmlInternal() override;
    void SendHtml() override;
    const char16_t* GetTemplateFileName() override;

private:
    void Build1001Packet();
    void Build1002Packet();

    std::int32_t m_userServerId; // +0x98
    CMailListParameter m_listParam; // +0x9c..+0xd7
    std::int32_t m_mailId; // +0xd8
    std::array<SharedUtilsWideChar, 50> m_senderCharName; // +0xdc..+0x13f
    std::array<SharedUtilsWideChar, 250> m_receiverNameList; // +0x140..+0x333
    std::array<SharedUtilsWideChar, 200> m_title; // +0x334..+0x4c3
    std::array<SharedUtilsWideChar, 4000> m_content; // +0x4c4..+0x2403
    std::u16string m_htmlData; // +0x2408..+0x242f
};

class ModifyTempMailHtmlBuilder final : public CHtmlBuilder {
public:
    ModifyTempMailHtmlBuilder(CDBSocket* dbSocket, const unsigned char* packet) :
        CHtmlBuilder(dbSocket, packet), m_userServerId(0) {}
    ~ModifyTempMailHtmlBuilder() override = default;

protected:
    void DisassemblePacket() override;
    void BuildHtmlInternal() override;
    void SendHtml() override;

private:
    std::int32_t m_userServerId; // +0x98
};

class ModifySendTempMailHtmlBuilder final : public CHtmlBuilder {
public:
    ModifySendTempMailHtmlBuilder(CDBSocket* dbSocket, const unsigned char* packet) :
        CHtmlBuilder(dbSocket, packet), m_userServerId(0), m_sentMailCount(0) {}
    ~ModifySendTempMailHtmlBuilder() override = default;

protected:
    void DisassemblePacket() override;
    void BuildHtmlInternal() override;
    void SendHtml() override;
    const char16_t* GetTemplateFileName() override;

private:
    bool IsMailSendingSuccess();
    void SendMsg();
    void Build1001Packet();
    void Build1002Packet();

    std::int32_t m_userServerId; // +0x98
    std::int32_t m_sentMailCount; // +0x9c
    std::u16string m_htmlData; // +0xa0..+0xc7
    CMailListParameter m_listParam; // +0xc8..+0x103
};

class DeleteMailHtmlBuilder final : public CHtmlBuilder {
public:
    DeleteMailHtmlBuilder(CDBSocket* dbSocket, const unsigned char* packet) :
        CHtmlBuilder(dbSocket, packet), m_userServerId(0) {}
    ~DeleteMailHtmlBuilder() override = default;

protected:
    void DisassemblePacket() override;
    void BuildHtmlInternal() override;
    void SendHtml() override;

private:
    std::int32_t m_userServerId; // +0x98
    CMailListParameter m_listParam; // +0x9c..+0xd7
};

class ArchiveMailHtmlBuilder final : public CHtmlBuilder {
public:
    ArchiveMailHtmlBuilder(CDBSocket* dbSocket, const unsigned char* packet) :
        CHtmlBuilder(dbSocket, packet), m_userServerId(0), m_errorType(0) {}
    ~ArchiveMailHtmlBuilder() override = default;

protected:
    void DisassemblePacket() override;
    void BuildHtmlInternal() override;
    void SendHtml() override;

private:
    bool IsMailArchivingSuccess();

    CMailListParameter m_listParam; // +0x98..+0xd3
    std::int32_t m_userServerId; // +0xd4
    std::int32_t m_errorType; // +0xd8
};

class UnreadMailCountMsgSender final : public CHtmlBuilder {
public:
    UnreadMailCountMsgSender(CDBSocket* dbSocket, const unsigned char* packet) :
        CHtmlBuilder(dbSocket, packet), m_userServerId(0), m_unreadMailCount(0) {}
    ~UnreadMailCountMsgSender() override = default;

protected:
    void DisassemblePacket() override;
    void BuildHtmlInternal() override;
    void SendHtml() override;

private:
    std::int32_t m_userServerId; // +0x98
    std::int32_t m_unreadMailCount; // +0x9c
};

class SendPledgeMailHandler final : public CHtmlBuilder {
public:
    SendPledgeMailHandler(CDBSocket* dbSocket, const unsigned char* packet) :
        CHtmlBuilder(dbSocket, packet), m_userServerId(0), m_errorType(0), m_pledgeName{} {}
    ~SendPledgeMailHandler() override = default;

protected:
    void DisassemblePacket() override;
    void BuildHtmlInternal() override;
    void SendHtml() override;

private:
    int GetPledgeId();
    void SendMsg();

    std::int32_t m_userServerId; // +0x98
    std::int32_t m_errorType; // +0x9c
    std::array<SharedUtilsWideChar, 50> m_pledgeName; // +0xa0..+0x103
};

class WriteMailFormHtmlBuilder final : public CHtmlBuilder {
public:
    WriteMailFormHtmlBuilder(CDBSocket* dbSocket, const unsigned char* packet) :
        CHtmlBuilder(dbSocket, packet), m_userServerId(0) {}
    ~WriteMailFormHtmlBuilder() override = default;

protected:
    void DisassemblePacket() override;
    void BuildHtmlInternal() override;
    void SendHtml() override;
    const char16_t* GetTemplateFileName() override;

private:
    std::int32_t m_userServerId; // +0x98
    CMailListParameter m_listParam; // +0x9c..+0xd7
};

class WritePledgeMailFormHtmlBuilder final : public CHtmlBuilder {
public:
    WritePledgeMailFormHtmlBuilder(CDBSocket* dbSocket, const unsigned char* packet) : CHtmlBuilder(dbSocket, packet) {
        // Current inline construction leaves both decoded lanes untouched.
    }
    ~WritePledgeMailFormHtmlBuilder() override = default;

protected:
    void DisassemblePacket() override;
    void BuildHtmlInternal() override;
    void SendHtml() override;
    const char16_t* GetTemplateFileName() override;

private:
    const char16_t* GetPledgeName();

    std::int32_t m_userServerId; // +0x98
    std::int32_t m_pledgeId; // +0x9c
};

class WriteFriendMailFormHtmlBuilder final : public CHtmlBuilder {
public:
    WriteFriendMailFormHtmlBuilder(CDBSocket* dbSocket, const unsigned char* packet) :
        CHtmlBuilder(dbSocket, packet), m_userServerId(0), m_friendCount(0) {}
    ~WriteFriendMailFormHtmlBuilder() override = default;

protected:
    void DisassemblePacket() override;
    void BuildHtmlInternal() override;
    void SendHtml() override;
    const char16_t* GetTemplateFileName() override;

private:
    void Build1001Packet();
    void Build1002Packet();
    std::u16string GetFriendNameList();

    std::int32_t m_userServerId; // +0x98
    std::int32_t m_friendCount; // +0x9c
    std::u16string m_htmlData; // +0xa0..+0xc7
};

class CastleListHtmlBuilder final : public CHtmlBuilder {
public:
    CastleListHtmlBuilder(CDBSocket* dbSocket, const unsigned char* packet) :
        CHtmlBuilder(dbSocket, packet), m_userServerId(0), m_communityId(0) {}
    ~CastleListHtmlBuilder() override = default;

    static void Show(unsigned int userServerId, int communityId);

protected:
    void DisassemblePacket() override;
    void BuildHtmlInternal() override;
    void SendHtml() override;
    const char16_t* GetTemplateFileName() override;

private:
    std::int32_t m_userServerId; // +0x98
    std::int32_t m_communityId; // +0x9c
};

class CastleInfoHtmlBuilder final : public CHtmlBuilder {
public:
    CastleInfoHtmlBuilder(CDBSocket* dbSocket, const unsigned char* packet) :
        CHtmlBuilder(dbSocket, packet), m_userServerId(0), m_castleId(0), m_communityId(0) {}
    ~CastleInfoHtmlBuilder() override = default;

    static void Show(unsigned int userServerId, int castleId, int communityId);

protected:
    void DisassemblePacket() override;
    void BuildHtmlInternal() override;
    void SendHtml() override;
    const char16_t* GetTemplateFileName() override;

private:
    CCastle* GetCastle();
    void BuildCastleInfo();
    void BuildAgitList();

    std::int32_t m_userServerId; // +0x98
    std::int32_t m_castleId; // +0x9c
    std::int32_t m_communityId; // +0xa0
};

class ListFriendHtmlBuilder final : public CHtmlBuilder {
public:
    ListFriendHtmlBuilder(CDBSocket* dbSocket, const unsigned char* packet);
    ~ListFriendHtmlBuilder() override;

    static void Show(int userServerId);
    static void ShowDeleteAllFriendHtml(int userServerId);

protected:
    void DisassemblePacket() override;
    void BuildHtmlInternal() override;
    void SendHtml() override;
    const char16_t* GetTemplateFileName() override;

private:
    void Build1Packet();
    void Build2Packet();
    void Build3Packet();
    std::u16string GetFriendDbIdListString();
    std::u16string GetFriendDbIdListStringAdd(int friendDbId);
    std::u16string GetFriendDbIdListStringDel(int friendDbId);
    int GetFriendDbIdCountToAdd(int friendDbId);
    void SetDeleteAllFriendCmd(bool enabled) { m_isDeleteAllFriendCmd = enabled; }

    bool m_isDeleteAllFriendCmd; // +0x98
    std::int32_t m_userServerId; // +0x9c
    [[maybe_unused]] std::int32_t m_page; // +0xa0, current constructor leaves untouched
    std::int32_t m_selectedFriendCount; // +0xa4, set by packet decode
    std::u16string m_htmlData; // +0xa8..+0xcf
    std::u16string m_selectedFriendNameList; // +0xd0..+0xf7
    std::vector<int> m_friendDbIdList; // +0xf8..+0x117
};

class ListBlockHtmlBuilder final : public CHtmlBuilder {
public:
    ListBlockHtmlBuilder(CDBSocket* dbSocket, const unsigned char* packet) :
        CHtmlBuilder(dbSocket, packet), m_userServerId(0), m_isDeleteAllBlockCmd(false) {}
    ~ListBlockHtmlBuilder() override = default;

    static void Show(int userServerId);
    static void ShowDeleteAllBlockHtml(int userServerId);

protected:
    void DisassemblePacket() override;
    void BuildHtmlInternal() override;
    void SendHtml() override;
    const char16_t* GetTemplateFileName() override;

private:
    void SetDeleteAllBlockCmd(bool enabled) { m_isDeleteAllBlockCmd = enabled; }

    std::int32_t m_userServerId; // +0x98
    bool m_isDeleteAllBlockCmd; // +0x9c
};

class DeleteFriendBatchHandler final : public CHtmlBuilder {
public:
    DeleteFriendBatchHandler(CDBSocket* dbSocket, const unsigned char* packet) :
        CHtmlBuilder(dbSocket, packet), m_userServerId(0) {}
    ~DeleteFriendBatchHandler() override = default;

protected:
    void DisassemblePacket() override;
    void BuildHtmlInternal() override;
    void SendHtml() override;

private:
    std::int32_t m_userServerId; // +0x98
    std::vector<int> m_friendDbIdList; // +0xa0..+0xbf
    std::vector<int> m_failedFriendDbIdList; // +0xc0..+0xdf
};

class DeleteBlockBatchHandler final : public CHtmlBuilder {
public:
    DeleteBlockBatchHandler(CDBSocket* dbSocket, const unsigned char* packet) :
        CHtmlBuilder(dbSocket, packet), m_userServerId(0) {}
    ~DeleteBlockBatchHandler() override = default;

protected:
    void DisassemblePacket() override;
    void BuildHtmlInternal() override;
    void SendHtml() override;

private:
    std::int32_t m_userServerId; // +0x98
    std::vector<int> m_blockDbIdList; // +0xa0..+0xbf
    std::vector<std::u16string> m_blockNameList; // +0xc0..+0xdf
};

class PledgeAnnounceHtmlBuilder final : public CHtmlBuilder {
public:
    PledgeAnnounceHtmlBuilder(CDBSocket* dbSocket, const unsigned char* packet) :
        CHtmlBuilder(dbSocket, packet), m_userServerId(0) {}
    ~PledgeAnnounceHtmlBuilder() override = default;

    static void Show(unsigned int userServerId, unsigned int pledgeDbId, int doesShow, SharedUtilsWideChar* content);
    static bool Announce(unsigned int userServerId, unsigned int pledgeDbId);

protected:
    void DisassemblePacket() override;
    void BuildHtmlInternal() override;
    void SendHtml() override;
    const char16_t* GetTemplateFileName() override;

private:
    void Build1001Packet();
    void Build1002Packet();

    std::int32_t m_userServerId; // +0x98
    std::int32_t m_doesShow; // +0x9c
    std::uint32_t m_pledgeDbId; // +0xa0
    std::int32_t m_replyForAnnounce; // +0xa4
    std::array<SharedUtilsWideChar, 3001> m_content; // +0xa8..+0x1819
    std::u16string m_htmlData; // +0x1820..+0x1847
};

class SetPledgeAnnounceHtmlHandler final : public CHtmlBuilder {
public:
    SetPledgeAnnounceHtmlHandler(CDBSocket* dbSocket, const unsigned char* packet) :
        CHtmlBuilder(dbSocket, packet), m_userServerId(0) {}
    ~SetPledgeAnnounceHtmlHandler() override = default;

protected:
    void DisassemblePacket() override;
    void BuildHtmlInternal() override;
    void SendHtml() override;

private:
    std::int32_t m_userServerId; // +0x98
    std::int32_t m_doesShow; // +0x9c
    std::uint32_t m_pledgeDbId; // +0xa0
    bool m_success; // +0xa4
    std::array<SharedUtilsWideChar, 3001> m_content; // +0xa6..+0x1817
};
