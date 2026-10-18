#pragma once

#include "Shared/src/MailListParameter.h"
#include "Shared/src/Runtime/TargetWideChar.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CSocket;
class CUserSocket;
class User;

class CHtmlCommand;
using CHtmlCommandSP = CSPointer<CHtmlCommand>;

// Current HtmlCommand.h owner. MemoryObject contributes the first four
// vtable slots. This owner adds ExecuteInternal and ParseParameter, producing
// the exact six-slot vtable shared by the base and every concrete command.
class CHtmlCommand : public MemoryObject {
public:
    CHtmlCommand();
    ~CHtmlCommand() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    void Execute() {
        if (CheckCommServer() && IsValidUser()) {
            ParseParameter();
            ExecuteInternal();
        }
    }

    void SetPacket(const std::uint8_t* packet);

protected:
    virtual void ExecuteInternal() = 0;
    virtual void ParseParameter() = 0;

private:
    friend class CHtmlCommandFactory;

    void SetSocket(CSocket* socket);
    void SetCommand(CSocket* socket, const SharedUtilsWideChar* command);
    bool IsValidUser() { return m_user != nullptr; }
    bool CheckCommServer();

    L2_DECLARE_OBJECT_REGISTRATION(CHtmlCommand)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CHtmlCommand)

protected:
    CSocket* m_socket; // +0x18
    CUserSocket* m_userSocket; // +0x20
    User* m_user; // +0x28
    const SharedUtilsWideChar* m_cmd; // +0x30
    const std::uint8_t* m_packet; // +0x38
};

#define L2_HTML_COMMAND_WIDEN_IMPL(Value) L##Value
#define L2_HTML_COMMAND_WIDEN(Value) L2_HTML_COMMAND_WIDEN_IMPL(Value)
#define L2_HTML_COMMAND_MEMORY_DECL(ClassName)                                                                              \
private:                                                                                                                    \
    inline static const int m_nID_##ClassName = L2_MEMORY_CLASS_ID(ClassName);                                              \
    inline static const wchar_t* m_pClassName_##ClassName = L2_HTML_COMMAND_WIDEN(#ClassName);                              \
                                                                                                                            \
public:                                                                                                                     \
    static int __GetClassID() noexcept;                                                                                     \
    static const wchar_t* __GetClassName() noexcept;                                                                        \
    static void* operator new(std::size_t requestedSize);                                                                   \
    static void* operator new(std::size_t requestedSize, bool objectAllocation);                                            \
    std::int32_t Release(const char* callerFile, int callerLine, ObjectRefType type, bool releaseFromTimer) override

class NullCommand final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(NullCommand);

public:
    NullCommand() = default;
    ~NullCommand() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;
};

class ListMail final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(ListMail);

public:
    ListMail() = default;
    ~ListMail() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;

private:
    CMailListParameter m_listParam; // +0x40
};

class ReadMail final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(ReadMail);

public:
    ReadMail() = default;
    ~ReadMail() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;

private:
    std::int32_t m_mailId; // +0x40
    CMailListParameter m_listParam; // +0x44
};

class WriteMailForm final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(WriteMailForm);

public:
    WriteMailForm() = default;
    ~WriteMailForm() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;

private:
    CMailListParameter m_listParam; // +0x40
};

class WritePledgeMailForm final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(WritePledgeMailForm);

public:
    WritePledgeMailForm() = default;
    ~WritePledgeMailForm() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;

private:
    std::int32_t m_pledgeId; // +0x40
};

class WriteFriendMailForm final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(WriteFriendMailForm);

public:
    WriteFriendMailForm() : m_friendCount(0), m_buf{}, m_bufLen(0) {}
    ~WriteFriendMailForm() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;

private:
    std::int32_t m_friendCount; // +0x40
    std::array<std::uint8_t, 0x5000> m_buf; // +0x44
    std::int32_t m_bufLen; // +0x5044
};

class SendMail final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(SendMail);

public:
    SendMail() = default;
    ~SendMail() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;

private:
    bool CheckReceiverList();
    bool CheckEmptyTitle();
    void ReturnMail();

    std::int32_t m_mailType; // +0x40
    std::int32_t m_relatedId; // +0x44
    std::array<SharedUtilsWideChar, 250> m_receiverList; // +0x48
    std::array<SharedUtilsWideChar, 200> m_title; // +0x23c
    std::array<SharedUtilsWideChar, 3000> m_content; // +0x3cc
    CMailListParameter m_listParam; // +0x1b3c
    std::array<SharedUtilsWideChar, 250> m_blockedName; // +0x1b78
    std::int32_t m_errorType; // +0x1d6c
};

class SaveMail final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(SaveMail);

public:
    SaveMail() = default;
    ~SaveMail() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;

private:
    std::int32_t m_mailType; // +0x40
    std::int32_t m_relatedId; // +0x44
    std::array<SharedUtilsWideChar, 250> m_receiverList; // +0x48
    std::array<SharedUtilsWideChar, 200> m_title; // +0x23c
    std::array<SharedUtilsWideChar, 3000> m_content; // +0x3cc
    CMailListParameter m_listParam; // +0x1b3c
};

class SendPledgeMail final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(SendPledgeMail);

public:
    SendPledgeMail() = default;
    ~SendPledgeMail() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;

private:
    std::int32_t m_pledgeId; // +0x40
    std::array<SharedUtilsWideChar, 200> m_title; // +0x44
    std::array<SharedUtilsWideChar, 3000> m_content; // +0x1d4
};

class ModifyMailForm final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(ModifyMailForm);

public:
    ModifyMailForm() = default;
    ~ModifyMailForm() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;

private:
    std::int32_t m_relatedMailId; // +0x40
    CMailListParameter m_listParam; // +0x44
};

class ReplyMailForm final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(ReplyMailForm);

public:
    ReplyMailForm() = default;
    ~ReplyMailForm() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;

private:
    std::int32_t m_relatedMailId; // +0x40
    CMailListParameter m_listParam; // +0x44
};

class ForwardMailForm final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(ForwardMailForm);

public:
    ForwardMailForm() = default;
    ~ForwardMailForm() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;

private:
    std::int32_t m_relatedMailId; // +0x40
    CMailListParameter m_listParam; // +0x44
};

class ModifyMail final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(ModifyMail);

public:
    ModifyMail() = default;
    ~ModifyMail() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;

private:
    std::int32_t m_mailId; // +0x40
    std::array<SharedUtilsWideChar, 250> m_receiverList; // +0x44
    std::array<SharedUtilsWideChar, 200> m_title; // +0x238
    std::array<SharedUtilsWideChar, 3000> m_content; // +0x3c8
};

class ModifySendMail final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(ModifySendMail);

public:
    ModifySendMail() = default;
    ~ModifySendMail() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;

private:
    bool CheckReceiverList();

    std::int32_t m_mailId; // +0x40
    std::array<SharedUtilsWideChar, 250> m_receiverList; // +0x44
    std::array<SharedUtilsWideChar, 200> m_title; // +0x238
    std::array<SharedUtilsWideChar, 3000> m_content; // +0x3c8
    CMailListParameter m_listParam; // +0x1b38
    std::array<SharedUtilsWideChar, 250> m_blockedName; // +0x1b74
    std::int32_t m_errorType; // +0x1d68
};

class DeleteMail final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(DeleteMail);

public:
    DeleteMail() = default;
    ~DeleteMail() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;

private:
    std::int32_t m_mailId; // +0x40
    CMailListParameter m_listParam; // +0x44
};

class ArchiveMail final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(ArchiveMail);

public:
    ArchiveMail() = default;
    ~ArchiveMail() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;

private:
    std::int32_t m_mailId; // +0x40
    CMailListParameter m_listParam; // +0x44
};

class SearchMail final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(SearchMail);

public:
    SearchMail() = default;
    ~SearchMail() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;

private:
    CMailListParameter m_listParam; // +0x40
};

class ListFriend final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(ListFriend);

public:
    ListFriend() = default;
    ~ListFriend() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;

private:
    void ParseSelectedFriendCount();
    void ParseSelectedFriendIdList();

    std::int32_t m_selectedFriendCount; // +0x40
    std::array<std::uint8_t, 0x5000> m_buf; // +0x44
    std::int32_t m_bufLen; // +0x5044
};

class DeleteFriend final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(DeleteFriend);

public:
    DeleteFriend() = default;
    ~DeleteFriend() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;

private:
    std::int32_t m_selectedFriendCount; // +0x40
};

class DeleteAllFriendConfirm final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(DeleteAllFriendConfirm);

public:
    DeleteAllFriendConfirm() = default;
    ~DeleteAllFriendConfirm() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;
};

class DeleteAllFriend final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(DeleteAllFriend);

public:
    DeleteAllFriend() = default;
    ~DeleteAllFriend() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;
};

class ListBlock final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(ListBlock);

public:
    ListBlock() = default;
    ~ListBlock() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;
};

class AddBlock final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(AddBlock);

public:
    AddBlock() = default;
    ~AddBlock() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;

private:
    std::array<SharedUtilsWideChar, 50> m_blockName; // +0x40
};

class DeleteBlock final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(DeleteBlock);

public:
    DeleteBlock() = default;
    ~DeleteBlock() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;

private:
    std::int32_t m_blockDbId; // +0x40
};

class DeleteAllBlockConfirm final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(DeleteAllBlockConfirm);

public:
    DeleteAllBlockConfirm() = default;
    ~DeleteAllBlockConfirm() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;
};

class DeleteAllBlock final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(DeleteAllBlock);

public:
    DeleteAllBlock() = default;
    ~DeleteAllBlock() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;

private:
    std::vector<int> m_blockDbIdList; // +0x40
    std::vector<std::u16string> m_blockNameList; // +0x60
};

class WritePledgeAnnounceForm final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(WritePledgeAnnounceForm);

public:
    WritePledgeAnnounceForm() = default;
    ~WritePledgeAnnounceForm() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;

private:
    std::uint32_t m_pledgeDbId; // +0x40
};

class WritePledgeAnnounce final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(WritePledgeAnnounce);

public:
    WritePledgeAnnounce() = default;
    ~WritePledgeAnnounce() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;

private:
    std::uint32_t m_pledgeDbId; // +0x40
    std::int32_t m_doesShow; // +0x44
    std::array<SharedUtilsWideChar, 3001> m_content; // +0x48
};

class SwitchPledgeAnnounceShowFlag final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(SwitchPledgeAnnounceShowFlag);

public:
    SwitchPledgeAnnounceShowFlag() = default;
    ~SwitchPledgeAnnounceShowFlag() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;

private:
    std::uint32_t m_pledgeDbId; // +0x40
    std::int32_t m_doesShow; // +0x44
};

class ViewCastleList final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(ViewCastleList);

public:
    ViewCastleList() = default;
    ~ViewCastleList() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;
};

class ViewCastleInfo final : public CHtmlCommand {
    L2_HTML_COMMAND_MEMORY_DECL(ViewCastleInfo);

public:
    ViewCastleInfo() = default;
    ~ViewCastleInfo() override = default;

protected:
    void ExecuteInternal() override;
    void ParseParameter() override;

private:
    std::int32_t m_castleId; // +0x40
};

#undef L2_HTML_COMMAND_MEMORY_DECL
#undef L2_HTML_COMMAND_WIDEN
#undef L2_HTML_COMMAND_WIDEN_IMPL

class CHtmlCommandFactory {
public:
    static CHtmlCommandSP GetHtmlCommand(CSocket* socket, const SharedUtilsWideChar* command);

private:
    static NullCommand m_nullCmd;
};
