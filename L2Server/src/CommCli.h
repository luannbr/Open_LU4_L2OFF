#pragma once

#include "Shared/src/MemorySub.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/Utils.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>

class CCommSocket;
class CHtmlCommand;
class User;

class CCommCli final : public MemoryObject {
public:
    CCommCli();
    ~CCommCli() override;

    L2_DECLARE_OBJECT_TYPE(CCommCli)

    static CCommCli* Inst() noexcept;

    void Clear();
    void SetCliStatus(int status);
    bool ConnectToCommD();
    bool CreateThread();
    void ToggleAutoConnect();
    bool CheckCommSvr(User* user, bool notify);
    bool CheckCommSvr(bool connect);
    bool Connect(CSPointer<CCommSocket>& socket);
    CSPointer<CCommSocket> GetCommSocket(int index) const;
    CSPointer<CCommSocket> GetCommSocket() const;
    void Send(const char* format, ...) const;

    void RequestBBSInit(User* user, int value1, int boardId, int value3, int value4, int value5);
    void RequestBBSList(User* user, int boardId, int page, int flags);
    void RequestBBSRead(
        User* user,
        int boardId,
        int articleId,
        int boardType,
        int listMode,
        int listPage,
        int searchType,
        SharedUtilsWideChar* search,
        int searchStart,
        int searchEnd
    );
    void RequestBBSReadThread(
        User* user,
        int boardId,
        int articleId,
        int boardType,
        int listMode,
        int listPage,
        int searchType,
        SharedUtilsWideChar* search,
        int searchStart,
        int searchEnd
    );
    void RequestBBSReadComment(
        User* user,
        int boardId,
        int articleId,
        int boardType,
        int listMode,
        int listPage,
        int searchType,
        SharedUtilsWideChar* search,
        int searchStart,
        int searchEnd
    );
    void RequestBBSReply(User* user, int value1, int value2, int value3);
    void RequestBBSWrite(
        User* user,
        int value1,
        int value2,
        int value3,
        SharedUtilsWideChar* string1,
        SharedUtilsWideChar* string2,
        SharedUtilsWideChar* string3
    );
    void RequestBBSWriteComment(User* user, int value1, int value2, int value3, SharedUtilsWideChar* string);
    void RequestBBSWriteForm(User* user, int value1, int value2);
    void RequestBBSSearch(User* user, int value1, int value2, int value3, int value4, SharedUtilsWideChar* search);
    void RequestBBSSysinfo(User* user);
    void RequestBBSModify(
        User* user,
        int value1,
        int value2,
        int value3,
        SharedUtilsWideChar* string1,
        SharedUtilsWideChar* string2,
        SharedUtilsWideChar* string3
    );
    void RequestBBSModifyForm(User* user, int value1, int value2, int value3);
    void RequestBBSDelete(User* user, int value1, int value2, int value3, int value4, int value5);
    void RequestBBSGetFavorite(User* user);
    void RequestBBSAddFavorite(
        User* user,
        SharedUtilsWideChar* string1,
        SharedUtilsWideChar* string2,
        int value1,
        int value2,
        int value3,
        int value4,
        int value5,
        SharedUtilsWideChar* string3
    );
    void RequestBBSDelFavorite(User* user, int value);
    void RequestBBSGetAnnounce(User* user, int value);
    void RequestGetMemoList(User* user, int page, SharedUtilsWideChar* search);
    void RequestGetMemo(User* user, int memoId, int page, SharedUtilsWideChar* search);
    void RequestCreateMemo(User* user, SharedUtilsWideChar* title, SharedUtilsWideChar* contents);
    void RequestDeleteMemo(User* user, int memoId);
    void RequestUpdateMemoForm(User* user, int memoId);
    void RequestUpdateMemo(User* user, int memoId, SharedUtilsWideChar* title, SharedUtilsWideChar* contents);

    void RequestClanBBSView(User* user, int pledgeId);
    void RequestClanBBSList(User* user, int page, int value, SharedUtilsWideChar* search);
    void RequestClanBBSView2(
        User* user,
        int value1,
        int value2,
        SharedUtilsWideChar* string1,
        int value3,
        SharedUtilsWideChar* string2,
        int value4,
        SharedUtilsWideChar* string3,
        int value5,
        int value6
    );
    void RequestClanBBSMake(User* user, int pledgeId, const char16_t* pledgeName, const char16_t* masterName);
    void RequestClanBBSDelete(int pledgeId, int worldId);
    void RequestPledgeMasterTransfer(const unsigned char* packet);
    void RequestSetPledgeInfo(int worldId, int pledgeId, const wchar_t* pledgeName, const wchar_t* masterName);
    void RequestChangePledgeMasterName(int worldId, int characterId, wchar_t* name);
    void RequestClanBBSInit(User* user);
    int CheckClanOwner(User* user);
    void RequestClanBBSAdmin(User* user, int pledgeId);
    void RequestClanSetIntro(User* user, SharedUtilsWideChar* introduction);
    void RequestClanBBSSetPer(User* user, int pledgeId, int setting1, int setting2, int setting3, int setting4);

    bool Connect();
    bool Init();

private:
    friend class CHtmlCommand;

    std::int32_t m_nStatus; // +0x18
    bool m_CommSendable; // +0x1c
    std::uint8_t m_alignment1DTo20[3]; // +0x1d
    std::uint32_t m_nConnectTick; // +0x20
    bool m_bAutoConnect; // +0x24
    std::uint8_t m_alignment25To28[3]; // +0x25
    CSPointer<CCommSocket> m_spCommSocket0; // +0x28
    CSPointer<CCommSocket> m_spCommSocket1; // +0x38
    std::uint32_t m_nConnectThreadId; // +0x48
    std::uint32_t m_alignment4C; // +0x4c
    void* m_hConnectThread; // +0x50
};

extern CCommCli commcli;
