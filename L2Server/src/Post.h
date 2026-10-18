#pragma once

#include "Protocol/src/L2Constants.h"

#include "Shared/src/ItemConsumeType.h"
#include "Shared/src/Obj.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CUserSocket;
class CSocket;
class User;

struct PostConst {
    static int _POST_WAITING_SENDING_TIME;
    static int _POST_AUTO_REJECT_NORMAL_TIME;
    static int _POST_AUTO_REJECT_TRADE_TIME;
    static int _POST_AUTO_DELETE_TIME;
};

namespace PostUtil {

std::int32_t DecideNextJobTime(
    std::int32_t tTime, Post::PostProcessType eProcessType, int nTrade, bool bReturnable, bool bIgnoreWaiting
);

void WriteTimeForDebug(std::int32_t time);
void DeletePostByUserDisconnected(User* user);
void OpenPost(int mailId, int receiverId, int senderId);

} // namespace PostUtil

struct PostItemInfo {
    int db_id; // +0x00
    std::int64_t amount; // +0x08
    int class_id; // +0x10
};


class CPost : public MemoryObject {
public:
    struct ItemPair {
        int db_id; // +0x00
        std::int64_t amount; // +0x08
    };

    enum PostJobType : std::int32_t {
        JobNormal = 0,
        JobTrade = 1,
        JobMaximum = 2,
    };

    L2_DECLARE_OBJECT_REGISTRATION(CPost)

    CPost();
    ~CPost() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    L2_DECLARE_OBJECT_ALLOCATORS()

    std::int32_t Release(const char* callerFile, int callerLine, ObjectRefType type, bool releaseFromTimer = false) override;

    void Init(
        int id,
        int senderId,
        int receiverId,
        bool returnable,
        bool trade,
        unsigned char state,
        Post::PostProcessType processType,
        unsigned char deleted,
        std::int32_t nextJobTime,
        bool haveItem,
        int senderType
    );
    bool StartDB();
    void EndDB();

    CSPointer<User> GetReceiver() const;
    CSPointer<User> GetSender() const;

    int Id() const noexcept { return m_nId; }
    int GetSenderId() const noexcept { return m_nSenderId; }
    int GetReceiverId() const noexcept { return m_nReceiverId; }
    unsigned char GetDeleted() const noexcept { return m_eDeleted; }
    void SetDeleted(unsigned char value) noexcept { m_eDeleted = value; }
    void AddDeleted(unsigned char value) noexcept { m_eDeleted |= value; }
    bool CanDelete() const noexcept { return (m_eDeleted & 1U) != 0 && (m_eDeleted & 2U) != 0; }
    bool IsReturnable() const noexcept { return m_bReturnable; }
    bool HaveItem() const noexcept { return m_bHaveItem; }
    bool IsTrade() const noexcept { return m_eJobType == JobTrade; }
    Post::PostProcessType GetProcessType() const noexcept { return m_eProcessType; }
    void SetSent() noexcept { m_eProcessType = Post::ProcessSent; }
    void SetReceived() noexcept { m_eProcessType = Post::ProcessReceived; }
    PostJobType GetJobType() const noexcept { return m_eJobType; }
    std::int32_t GetNextJobTime() const noexcept { return m_tNextJobTime; }
    void SetNextJobTime(std::int32_t value) noexcept { m_tNextJobTime = value; }
    int GetSenderType() const noexcept { return m_nSenderType; }
    int GetDate() const noexcept { return m_nDate; }
    CYieldLock& GetProcessLock() noexcept { return m_ProcessLock; }

private:
    friend void PostUtil::OpenPost(int mailId, int receiverId, int senderId);

    int m_nId; // +0x18
    int m_nSenderId; // +0x1c
    int m_nReceiverId; // +0x20
    unsigned char m_eDeleted; // +0x24
    bool m_bReturnable; // +0x25
    bool m_bHaveItem; // +0x26
    [[maybe_unused]] std::byte m_alignment27; // +0x27
    Post::PostProcessType m_eProcessType; // +0x28
    PostJobType m_eJobType; // +0x2c
    std::int32_t m_tNextJobTime; // +0x30
    std::int32_t m_tLastLockedTick; // +0x34
    volatile std::int32_t m_nWaitingDB; // +0x38
    int m_nSenderType; // +0x3c
    int m_nDate; // +0x40
    CYieldLock m_ProcessLock; // +0x44

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CPost)
};

namespace PostUtil {

void UpdatePostItems(User* user, CPost::ItemPair* item);
bool ReplyWritePost(User* user, int itemCount, CPost::ItemPair (&items)[8], int adenaDatabaseId, int spentAdena);
CSPointer<CPost> FindPostByUserWithRef(int mailId, int receiverId, int senderId);
void SendPost(int mailId, int receiverId, int senderId);
void ReceivePost(int mailId, int receiverId, int senderId);
void CancelPost(int mailId, int receiverId, int senderId, int ignored);
void DeletePost(int mailId, int receiverId, int senderId, unsigned char deleteMask);
void AddPostToUser(unsigned char userMask, int senderId, int receiverId, CSPointer<CPost> post);
void MakeNewPost(
    int mailId,
    int senderId,
    int receiverId,
    int trade,
    bool returnable,
    Post::PostProcessType processType,
    std::int32_t time,
    bool haveItem,
    int senderType,
    unsigned char userMask,
    bool findExisting,
    unsigned char deleted
);
bool CheckWritePost(
    CSocket* socket,
    User* user,
    int itemCount,
    CPost::ItemPair items[],
    std::int64_t unused,
    std::int64_t& fee,
    ItemConsumeType consumeTypes[]
);
bool CheckCancelPost(CSocket* socket, User* user, int itemCount, PostItemInfo items[], std::int64_t unused);
bool CheckReceivePost(
    CSocket* socket, User* user, int itemCount, PostItemInfo items[], std::int64_t tradeMoney, bool returnable
);

} // namespace PostUtil

class CPostFriend {
public:
    CPostFriend();
    ~CPostFriend() = default;

    void AddPostFriend(const char16_t* name);
    void DeletePostFriend(const char16_t* name);
    bool CheckNameExist(const char16_t* name) const;
    void LoadAndSendPostFriend(User& user) const;
    int CheckAddable(const char16_t* name) const;
    void SendPostFriend(CUserSocket* socket) const;
    void SetLoaded() noexcept { m_bLoaded = true; }
    bool IsLoaded() const noexcept { return m_bLoaded; }

private:
    bool _CheckNameExist(const char16_t* name) const;

    mutable CYieldLock m_Lock; // +0x00
    std::vector<std::u16string> m_NameList; // +0x10
    bool m_bLoaded; // +0x30
    [[maybe_unused]] std::array<std::byte, 0x03> m_alignment31To34;
    std::int32_t m_tLastTick; // +0x34
};

class CPostInfoForUser : public MemoryObject {
public:
    enum ReceiveProcessType : std::uint8_t {
        ProcessNothing = 0,
        ProcessSentOpening = 1,
        ProcessReceivedOpening = 2,
        ProcessCanceling = 3,
        ProcessReceiving = 4,
    };

    L2_DECLARE_OBJECT_REGISTRATION(CPostInfoForUser)

    CPostInfoForUser();
    ~CPostInfoForUser() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    L2_DECLARE_OBJECT_ALLOCATORS()

    std::int32_t Release(const char* callerFile, int callerLine, ObjectRefType type, bool releaseFromTimer = false) override;

    void Clear();
    void AddReceivedPost(CSPointer<CPost> post, int userId);
    void AddSentPost(CSPointer<CPost> post);
    void RemoveSentPost(int mailId);
    void RemoveReceivedPost(int mailId);
    CSPointer<CPost> FindSentPostWithRef(int mailId) const;
    CSPointer<CPost> FindReceivedPostWithRef(int mailId) const;

    bool CheckSendingDelay();
    void RecoverSendingDelay();
    bool CheckSentListDelay();
    bool CheckReceivedListDelay();
    bool CheckPacketReceivedDelay();

    bool OpenPost(
        int mailId, int itemCount, CPost::ItemPair items[], int classIds[], std::int64_t tradeMoney, bool sentPost
    );
    bool TryReceivePost(int mailId);
    void CancelTryReceivePost();
    void ReceivePost();
    bool TryCancelPost(int mailId);
    void CancelTryCancelPost();
    void CancelPost();
    bool TryWrite() const;
    int GetSentListSize() const;
    int GetReceivedListSize() const;
    void ProcessList();

    int GetItemCount() const noexcept { return m_nItemCount; }
    PostItemInfo* GetItemInfo() noexcept { return m_ItemInfo; }
    const PostItemInfo* GetItemInfo() const noexcept { return m_ItemInfo; }
    std::int64_t GetTradeMoney() const noexcept { return m_nTradeMoney; }
    CPostFriend& GetPostFriend() noexcept { return m_Friend; }
    const CPostFriend& GetPostFriend() const noexcept { return m_Friend; }

private:
    PostItemInfo m_ItemInfo[8]; // +0x018
    mutable CYieldLock m_Lock; // +0x0d8
    std::vector<CSPointer<CPost>> m_lstReceived; // +0x0e8
    std::vector<CSPointer<CPost>> m_lstSent; // +0x108
    ReceiveProcessType m_eReceiveProcessType; // +0x128
    [[maybe_unused]] std::array<std::byte, 0x03> m_alignment129To12c;
    int m_nMailId; // +0x12c
    int m_nItemCount; // +0x130
    [[maybe_unused]] std::array<std::byte, 0x04> m_alignment134To138;
    std::int64_t m_nTradeMoney; // +0x138
    CPostFriend m_Friend; // +0x140
    std::int32_t m_tLastWroteTime; // +0x178
    std::int32_t m_tLastSentListTime; // +0x17c
    std::int32_t m_tLastReceivedListTime; // +0x180
    std::int32_t m_tPacketReceivedTime; // +0x184

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CPostInfoForUser)
};
