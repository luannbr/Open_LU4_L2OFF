#include "Post.h"
#include <optional>
#include "Area.h"
#include "CConfig.h"
#include "DB.h"
#include "Item.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/MemorySub.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Config.h"
#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SlotType.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"
#include "Shared/src/net/Serialize.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

int PostConst::_POST_WAITING_SENDING_TIME = 30;
int PostConst::_POST_AUTO_REJECT_NORMAL_TIME = 1296000;
int PostConst::_POST_AUTO_REJECT_TRADE_TIME = 43200;
int PostConst::_POST_AUTO_DELETE_TIME = 1296000;

#if defined(__GNUC__) || defined(__clang__)
__attribute__((used))
#endif
const int POST_WAITING_SENDING_TIME_GAP = PostConst::_POST_WAITING_SENDING_TIME / 2;


CPost::CPost() {}

CPost::~CPost() = default;

L2_DEFINE_OBJECT_CLASS_ID(CPost)

L2_DEFINE_OBJECT_CLASS_NAME(CPost, m_pClassName_CPost)

// Current L2Server 0x009e2254, memory class 235.
L2_DEFINE_OBJECT_ALLOCATOR(CPost, __GetClassName())

L2_DEFINE_OBJECT_NEW(CPost)

// Current L2Server 0x009e2b54.
L2_DEFINE_OBJECT_RELEASE(CPost)

void CPost::Init(
    const int id,
    const int senderId,
    const int receiverId,
    const bool returnable,
    const bool trade,
    unsigned char,
    const Post::PostProcessType processType,
    const unsigned char deleted,
    const std::int32_t nextJobTime,
    const bool haveItem,
    const int senderType
) {
    // Current L2Server 0x009e235c. The sixth argument is intentionally
    // unused by the target body.
    L2_GUARD_FUNCTION();
    m_nId = id;
    m_nSenderId = senderId;
    m_nReceiverId = receiverId;
    m_bReturnable = returnable;
    m_tNextJobTime = nextJobTime;
    m_eDeleted = deleted;
    m_eProcessType = processType;
    m_eJobType = trade ? JobTrade : JobNormal;
    m_nWaitingDB = 0;
    m_bHaveItem = haveItem;
    m_nDate = GetYYYYMMDDFromTime(nextJobTime);
    m_nSenderType = senderType;
}

bool CPost::StartDB() {
    // Current L2Server 0x009e2480.
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    const std::int32_t observed = runtime::raw_atomic::CompareExchangeI32(&m_nWaitingDB, 1, 0);
    if (observed == 1) {
        if (now - m_tLastLockedTick < 61) {
            return false;
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"Too long time for post [%d].",
        //     m_nId)
        // L2_LOG_ERROR(Log,
        //     L"Too long time for post [%d].",
        //     m_nId)
        L2_LOG_ERROR(
            Log, "Mail %d: DB operation is still pending after %lld seconds; another attempt is allowed.", m_nId,
            static_cast<long long>(now) - static_cast<long long>(m_tLastLockedTick)
        );
        return true;
    }

    m_tLastLockedTick = runtime::crt::Time32Now();
    return true;
}

void CPost::EndDB() {
    // Current L2Server 0x009e256c.
    L2_GUARD_FUNCTION();
    const std::int32_t observed = runtime::raw_atomic::CompareExchangeI32(&m_nWaitingDB, 0, 1);
    if (observed == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"unlocked double [%d].",
        //     m_nId)
        // L2_LOG_ERROR(Log,
        //     L"unlocked double [%d].",
        //     m_nId)
        L2_LOG_ERROR(Log, "Mail %d: DB completion reported with no pending operation.", m_nId);
    }
}

CSPointer<User> CPost::GetReceiver() const {
    // Current L2Server 0x009e41f0.
    L2_GUARD_FUNCTION();
    return g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, m_nReceiverId));
}

CSPointer<User> CPost::GetSender() const {
    // Current L2Server 0x009e42b8.
    L2_GUARD_FUNCTION();
    return g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, m_nSenderId));
}

CPostFriend::CPostFriend() : m_bLoaded(false), m_alignment31To34{}, m_tLastTick(0) {
    // Current L2Server 0x009e7934.
    L2_GUARD_FUNCTION();
    m_NameList.clear();
}

bool CPostFriend::_CheckNameExist(const char16_t* name) const {
    // Current L2Server 0x009e3800. The caller owns m_Lock.
    L2_GUARD_FUNCTION();
    const std::u16string target = Utf16Text(name).String();
    for (const std::u16string& candidate : m_NameList) {
        if (runtime::StringView(candidate) == std::u16string_view(target)) {
            return true;
        }
    }
    return false;
}

bool CPostFriend::CheckNameExist(const char16_t* name) const {
    // Current L2Server 0x009e3dec.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    return _CheckNameExist(name);
}

int CPostFriend::CheckAddable(const char16_t* name) const {
    // Current L2Server 0x009e3fbc.
    L2_GUARD_FUNCTION();
    if (!m_bLoaded) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"post friend request before not loaded! name [%s]",
        //     name)
        // L2_LOG_ERROR(Log,
        //     L"post friend request before not loaded! name [%s]",
        //     name)
        L2_LOG_ERROR(
            Log, u"Cannot add mail contact '%s': the contact list has not loaded; request rejected.",
            name != nullptr ? name : u"<null>"
        );
        return -2;
    }

    {
        L2_GUARD(m_Lock);
        if (m_NameList.size() >= 100) {
            return -3;
        }
    }

    if (static_cast<std::uint32_t>(runtime::GetTickCount() - static_cast<std::uint32_t>(m_tLastTick)) < 5000U) {
        return -1;
    }
    return CheckNameExist(name) ? -4 : 1;
}

void CPostFriend::AddPostFriend(const char16_t* name) {
    // Current L2Server 0x009e7b4c.
    L2_GUARD_FUNCTION();
    m_tLastTick = static_cast<std::int32_t>(runtime::GetTickCount());
    const std::u16string target = Utf16Text(name).String();
    const std::u16string stored{std::u16string_view(target)};
    L2_GUARD(m_Lock);
    if (!_CheckNameExist(name)) {
        m_NameList.push_back(stored);
    }
}

void CPostFriend::DeletePostFriend(const char16_t* name) {
    // Current L2Server 0x009e5eac.
    L2_GUARD_FUNCTION();
    const std::u16string target = Utf16Text(name).String();
    L2_GUARD(m_Lock);
    for (auto it = m_NameList.begin(); it != m_NameList.end(); ++it) {
        if (runtime::StringView(*it) == std::u16string_view(target)) {
            m_NameList.erase(it);
            return;
        }
    }
}

void CPostFriend::SendPostFriend(CUserSocket* socket) const {
    // Current L2Server 0x009e38d0.
    L2_GUARD_FUNCTION();
    std::array<char, 0x5000> packet{};
    int packetLength = 0;
    int friendCount = 0;
    {
        L2_GUARD(m_Lock);
        for (const std::u16string& name : m_NameList) {
            packetLength +=
                ::Assemble(packet.data() + packetLength, static_cast<int>(packet.size()) - packetLength, "S", name.c_str());
            ++friendCount;
        }
    }
    if (socket != nullptr) {
        socket->Send("chdb", 0xfe, 0xd4, friendCount, packetLength, packet.data());
    }
}

void CPostFriend::LoadAndSendPostFriend(User& user) const {
    // Current L2Server 0x009e3ec0.
    L2_GUARD_FUNCTION();
    if (!m_bLoaded) {
        db.RequestLoadPostFriend(user);
    } else {
        SendPostFriend(user.GetSocket());
    }
}

CPostInfoForUser::CPostInfoForUser() {
    // Current L2Server 0x009e79f4.
    L2_GUARD_FUNCTION();
    m_eReceiveProcessType = ProcessNothing;
    m_nMailId = 0;
    m_nItemCount = 0;
    for (PostItemInfo& item : m_ItemInfo) {
        item.db_id = 0;
        item.amount = 0;
    }
    m_tPacketReceivedTime = 0;
    Clear();
}

CPostInfoForUser::~CPostInfoForUser() = default;

L2_DEFINE_OBJECT_CLASS_ID(CPostInfoForUser)

L2_DEFINE_OBJECT_CLASS_NAME(CPostInfoForUser, m_pClassName_CPostInfoForUser)

// Current L2Server class initializer 236, target size 0x188.
L2_DEFINE_OBJECT_ALLOCATOR(CPostInfoForUser, __GetClassName())

L2_DEFINE_OBJECT_NEW(CPostInfoForUser)

// Current L2Server 0x009e758c.
L2_DEFINE_OBJECT_RELEASE(CPostInfoForUser)

void CPostInfoForUser::Clear() {
    // Current L2Server 0x009e6024.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_lstReceived.clear();
    m_lstSent.clear();
}

void CPostInfoForUser::AddReceivedPost(CSPointer<CPost> post, const int userId) {
    // Current L2Server 0x009e6118.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_lstReceived.push_back(post);
    const std::size_t size = m_lstReceived.size();
    if (size > 1000 && size % 500 == 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"too manypost for user %d / %d",
        //     userId,
        //     static_cast<int>(size))
        // L2_LOG_ERROR(Log,
        //     L"too manypost for user %d / %d",
        //     userId,
        //     static_cast<int>(size))
        L2_LOG_ERROR(Log, "Character %d now has %zu received mail entries; the new entry is retained.", userId, size);
        ::ReportCallStackNew(const_cast<wchar_t*>(L"ManyPost"), LOG_ERROR, false);
    }
}

void CPostInfoForUser::AddSentPost(CSPointer<CPost> post) {
    // Current L2Server 0x009e62ac.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_lstSent.push_back(post);
}

void CPostInfoForUser::RemoveSentPost(const int mailId) {
    // Current L2Server 0x009e5340.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    for (auto it = m_lstSent.begin(); it != m_lstSent.end(); ++it) {
        if ((*it)->Id() == mailId) {
            m_lstSent.erase(it);
            return;
        }
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"No post found for user. mail %d",
    //     mailId)
    // L2_LOG_ERROR(Log,
    //     L"No post found for user. mail %d",
    //     mailId)
    L2_LOG_ERROR(Log, "Cannot remove mail %d from the sent list: entry not found.", mailId);
}

void CPostInfoForUser::RemoveReceivedPost(const int mailId) {
    // Current L2Server 0x009e54a0.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    for (auto it = m_lstReceived.begin(); it != m_lstReceived.end(); ++it) {
        if ((*it)->Id() == mailId) {
            m_lstReceived.erase(it);
            return;
        }
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"No post found for user. mail %d",
    //     mailId)
    // L2_LOG_ERROR(Log,
    //     L"No post found for user. mail %d",
    //     mailId)
    L2_LOG_ERROR(Log, "Cannot remove mail %d from the received list: entry not found.", mailId);
}

CSPointer<CPost> CPostInfoForUser::FindSentPostWithRef(const int mailId) const {
    // Current L2Server 0x009e3a74.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    for (const CSPointer<CPost>& post : m_lstSent) {
        if (post->Id() == mailId) {
            return post;
        }
    }
    return {};
}

CSPointer<CPost> CPostInfoForUser::FindReceivedPostWithRef(const int mailId) const {
    // Current L2Server 0x009e3c30.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    for (const CSPointer<CPost>& post : m_lstReceived) {
        if (post->Id() == mailId) {
            return post;
        }
    }
    return {};
}

bool CPostInfoForUser::CheckSendingDelay() {
    // Current L2Server 0x009e2630.
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    if (now - m_tLastWroteTime < 11) {
        return false;
    }
    m_tLastWroteTime = now;
    return true;
}

void CPostInfoForUser::RecoverSendingDelay() {
    // Current ReplyWritePost@0x0068301c inlines this original Post.h
    // member and stores time32-now minus ten at +0x178.
    m_tLastWroteTime = runtime::AddInt32Wrapping(runtime::crt::Time32Now(), -10);
}

bool CPostInfoForUser::CheckSentListDelay() {
    // Current L2Server 0x009e26d8.
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    if (now - m_tLastSentListTime <= 0) {
        return false;
    }
    m_tLastSentListTime = now;
    return true;
}

bool CPostInfoForUser::CheckReceivedListDelay() {
    // Current L2Server 0x009e2784.
    L2_GUARD_FUNCTION();
    const std::uint32_t now = runtime::GetTickCount();
    if (now - static_cast<std::uint32_t>(m_tLastReceivedListTime) < 334U) {
        return false;
    }
    m_tLastReceivedListTime = static_cast<std::int32_t>(now);
    return true;
}

bool CPostInfoForUser::CheckPacketReceivedDelay() {
    // Current L2Server 0x009e2830.
    L2_GUARD_FUNCTION();
    const std::uint32_t now = runtime::GetTickCount();
    if (now - static_cast<std::uint32_t>(m_tPacketReceivedTime) < 334U) {
        return false;
    }
    m_tPacketReceivedTime = static_cast<std::int32_t>(now);
    return true;
}

bool CPostInfoForUser::OpenPost(
    const int mailId,
    const int itemCount,
    CPost::ItemPair items[],
    int classIds[],
    const std::int64_t tradeMoney,
    const bool sentPost
) {
    // Current L2Server 0x009e2c94.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    if (m_eReceiveProcessType == ProcessReceiving || m_eReceiveProcessType == ProcessCanceling) {
        return false;
    }

    m_eReceiveProcessType = static_cast<ReceiveProcessType>(2 - static_cast<int>(sentPost));
    m_nMailId = mailId;
    m_nItemCount = itemCount;
    m_nTradeMoney = tradeMoney;
    for (int index = 0; index < itemCount; ++index) {
        m_ItemInfo[index].db_id = items[index].db_id;
        m_ItemInfo[index].amount = items[index].amount;
        m_ItemInfo[index].class_id = classIds[index];
    }
    return true;
}

bool CPostInfoForUser::TryReceivePost(const int mailId) {
    // Current L2Server 0x009e2e10.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    if (m_eReceiveProcessType == ProcessReceivedOpening && m_nItemCount != 0) {
        if (mailId == m_nMailId) {
            m_eReceiveProcessType = ProcessReceiving;
            return true;
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"Not matching mail tried receiving. %d",
        //     mailId)
        // L2_LOG_ERROR(Log,
        //     L"Not matching mail tried receiving. %d",
        //     mailId)
        L2_LOG_ERROR(Log, "Mail receipt rejected: requested mail %d does not match opened mail %d.", mailId, m_nMailId);
    }
    return false;
}

void CPostInfoForUser::CancelTryReceivePost() {
    // Current L2Server 0x009e2f54.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_eReceiveProcessType = ProcessReceivedOpening;
}

void CPostInfoForUser::ReceivePost() {
    // Current L2Server 0x009e3028.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_eReceiveProcessType = ProcessReceivedOpening;
    m_nItemCount = 0;
}

bool CPostInfoForUser::TryCancelPost(const int mailId) {
    // Current L2Server 0x009e3104.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    if (m_eReceiveProcessType != ProcessSentOpening) {
        return false;
    }
    if (mailId == m_nMailId) {
        m_eReceiveProcessType = ProcessCanceling;
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"Not matching mail tried canceling. %d",
    //     mailId)
    // L2_LOG_ERROR(Log,
    //     L"Not matching mail tried canceling. %d",
    //     mailId)
    L2_LOG_ERROR(Log, "Mail cancellation rejected: requested mail %d does not match opened mail %d.", mailId, m_nMailId);
    return false;
}

void CPostInfoForUser::CancelTryCancelPost() {
    // Current L2Server 0x009e3248.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_eReceiveProcessType = ProcessSentOpening;
}

void CPostInfoForUser::CancelPost() {
    // Current L2Server 0x009e331c.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_eReceiveProcessType = ProcessSentOpening;
    m_nItemCount = 0;
}

bool CPostInfoForUser::TryWrite() const {
    // Current L2Server 0x009e33f8.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    return m_eReceiveProcessType < ProcessCanceling;
}

int CPostInfoForUser::GetSentListSize() const {
    // Current L2Server 0x009e34d0.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    return static_cast<int>(m_lstSent.size());
}

int CPostInfoForUser::GetReceivedListSize() const {
    // Current L2Server 0x009e35b0.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    return static_cast<int>(m_lstReceived.size());
}

void CPostInfoForUser::ProcessList() {
    // Current L2Server 0x009e63b0. Sent posts are processed first.
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    const std::vector<CSPointer<CPost>>* lists[2] = {
        &m_lstSent,
        &m_lstReceived,
    };

    for (const auto* list : lists) {
        L2_GUARD(m_Lock);
        const std::vector<CSPointer<CPost>> snapshot(*list);
        for (const CSPointer<CPost>& retainedPost : snapshot) {
            CPost* const post = retainedPost.get();
            if (post == nullptr || post->GetNextJobTime() > now) {
                continue;
            }
            CYieldLock& processLock = post->GetProcessLock();
            if (processLock.IsEntered()) {
                continue;
            }
            L2_GUARD(processLock);
            if (!post->StartDB()) {
                continue;
            }

            PostUtil::WriteTimeForDebug(post->GetNextJobTime());
            // Current 0x009e64d9 reads CPost + 0x28, the process state.
            // The adjacent job/trade discriminator lives at +0x2c and is
            // not consulted by this timer dispatch.
            const Post::PostProcessType processType = post->GetProcessType();
            if (processType != Post::ProcessSent && processType != Post::ProcessOpened) {
                if (processType == Post::ProcessWaiting) {
                    if (post->GetNextJobTime() < now) {
                        db.RequestSendPost(post->Id());
                    }
                } else {
                    std::array<char, 100> packet{};
                    const int length =
                        ::Assemble(packet.data(), static_cast<int>(packet.size()), "dd", post->Id(), post->GetNextJobTime());
                    db.RequestDeletePost(1, length, packet.data(), 3, post->GetSenderId(), post->GetReceiverId(), 0);
                }
                post->EndDB();
                continue;
            }

            if (post->GetSenderType() != 3 && post->GetSenderType() != 6) {
                if (post->IsReturnable()) {
                    db.RequestRejectPost(post->Id(), post->GetNextJobTime(), 0);
                } else {
                    std::array<char, 100> packet{};
                    const int length =
                        ::Assemble(packet.data(), static_cast<int>(packet.size()), "dd", post->Id(), post->GetNextJobTime());
                    post->EndDB();
                    db.RequestDeletePost(1, length, packet.data(), 3, post->GetSenderId(), post->GetReceiverId(), 0);
                }
                continue;
            }

            post->EndDB();
            db.RequestDeletePostBirth(post->Id(), post->GetNextJobTime(), 3, post->GetReceiverId(), 0);
        }
    }
}

std::int32_t PostUtil::DecideNextJobTime(
    std::int32_t tTime, Post::PostProcessType eProcessType, int nTrade, bool bReturnable, bool bIgnoreWaiting
) {
    L2_GUARD_FUNCTION();

    if (eProcessType == Post::ProcessWaiting && !bIgnoreWaiting) {
        return runtime::AddInt32Wrapping(tTime, PostConst::_POST_WAITING_SENDING_TIME);
    }

    const std::int32_t rejectionDelay =
        nTrade ? PostConst::_POST_AUTO_REJECT_TRADE_TIME : PostConst::_POST_AUTO_REJECT_NORMAL_TIME;
    if (bReturnable && eProcessType < Post::ProcessReceived) {
        std::int32_t tReturn = runtime::AddInt32Wrapping(tTime, rejectionDelay);
        if (bIgnoreWaiting) {
            tReturn = runtime::AddInt32Wrapping(tReturn, PostConst::_POST_WAITING_SENDING_TIME);
        }
        return tReturn;
    }

    return runtime::AddInt32Wrapping(tTime, PostConst::_POST_AUTO_DELETE_TIME);
}

void PostUtil::WriteTimeForDebug(std::int32_t time [[maybe_unused]]) { L2_GUARD_FUNCTION(); }

void PostUtil::UpdatePostItems(User* const user, CPost::ItemPair* const itemPair) {
    // Current L2Server.exe 0x009e2a54.
    CItem* const item = user->m_inv.GetByDBID(itemPair->db_id);
    if (item == nullptr) {
        return;
    }

    if (itemPair->amount < 1) {
        user->m_inv.Pop(item->m_data->index, false);
        user->m_inv.SetInventoryChanged(item, IUT_DELETE);
        item->Delete();
        return;
    }

    {
        L2_GUARD(*item->m_lock);
        if (!item->SetAmount(itemPair->amount)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
            //     "Post.cpp",
            //     0x661,
            //     static_cast<long long>(item->m_data->m_nCount),
            //     static_cast<long long>(itemPair->amount))
            // L2_LOG_ERROR(Log,
            //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
            //     "Post.cpp",
            //     0x661,
            //     static_cast<long long>(item->m_data->m_nCount),
            //     static_cast<long long>(itemPair->amount))
            L2_LOG_ERROR(
                Log,
                "Mail inventory update for character %u, item DBID %d: cannot set amount to %lld (stored %lld); inventory "
                "update notification continues.",
                static_cast<unsigned>(user->GetDBID()), itemPair->db_id, static_cast<long long>(itemPair->amount),
                static_cast<long long>(item->m_data->m_nCount)
            );
        }
    }
    user->m_inv.SetInventoryChanged(item, IUT_UPDATE);
}

bool PostUtil::ReplyWritePost(
    User* const user, const int itemCount, CPost::ItemPair (&items)[8], const int adenaDatabaseId, const int spentAdena
) {
    // Current L2Server.exe 0x009e3690.
    L2_GUARD_FUNCTION();

    {
        L2_WRITE_GUARD(user->m_inv);
        for (int index = 0; index < itemCount; ++index) {
            UpdatePostItems(user, &items[index]);
        }
        if (spentAdena != 0) {
            CPost::ItemPair adenaItem;
            adenaItem.db_id = adenaDatabaseId;
            adenaItem.amount = runtime::Int64FromBits(
                static_cast<std::uint64_t>(user->m_inv.GetAdenaAmount()) - static_cast<std::uint64_t>(spentAdena)
            );
            UpdatePostItems(user, &adenaItem);
        }
    }
    user->SendItemList(true, true, nullptr);
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
    return false;
}

CSPointer<CPost> PostUtil::FindPostByUserWithRef(const int mailId, const int receiverId, const int senderId) {
    // Current L2Server.exe 0x009e4380.
    L2_GUARD_FUNCTION();

    CSPointer<User> receiver = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, receiverId));
    if (receiver) {
        CSPointer<CPost> post = receiver->m_pPostInfo->FindReceivedPostWithRef(mailId);
        if (post) {
            return post;
        }
    }

    CSPointer<User> sender = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, senderId));
    if (!sender) {
        return {};
    }
    return sender->m_pPostInfo->FindSentPostWithRef(mailId);
}

void PostUtil::AddPostToUser(const unsigned char userMask, const int senderId, const int receiverId, CSPointer<CPost> post) {
    // Current L2Server.exe 0x009e6734.
    L2_GUARD_FUNCTION();

    // if ((userMask & 1U) != 0) {
    if ((userMask & static_cast<unsigned>(Post::ConnectedSender)) != 0) {
        CSPointer<User> sender = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, senderId));
        if (sender) {
            const CSPointer<CPost> existing = sender->m_pPostInfo->FindSentPostWithRef(post->Id());
            const int senderType = post->GetSenderType();
            if (!existing &&
                // senderType >= 0 &&
                senderType >= Post::UserToUser &&
                // (senderType < 3 || senderType == 7)) {
                (senderType < Post::BirthdayMail || senderType == Post::InGameShopGift)) {
                sender->m_pPostInfo->AddSentPost(post);
            }
        }
    }

    // if ((userMask & 2U) != 0) {
    if ((userMask & static_cast<unsigned>(Post::ConnectedReceiver)) != 0) {
        CSPointer<User> receiver = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, receiverId));
        if (receiver) {
            const CSPointer<CPost> existing = receiver->m_pPostInfo->FindReceivedPostWithRef(post->Id());
            if (!existing) {
                receiver->m_pPostInfo->AddReceivedPost(post, receiverId);
            }
        }
    }
}

void PostUtil::SendPost(const int mailId, const int receiverId, const int senderId) {
    // Current L2Server.exe 0x009e6ad0.
    L2_GUARD_FUNCTION();

    CSPointer<CPost> post = FindPostByUserWithRef(mailId, receiverId, senderId);
    if (!post) {
        return;
    }

    {
        GuardHelper lockGuard(L"L\"SendPost lock start\"", CThreadManager::GetThreadID());
        L2_GUARD(post->GetProcessLock());
        if (post->GetProcessType() == Post::ProcessSent) {
            return;
        }
        post->SetSent();
        post->SetNextJobTime(DecideNextJobTime(
            runtime::crt::Time32Now(), Post::ProcessSent, post->IsTrade(), post->IsReturnable() == true, false
        ));
    }

    const int senderType = post->GetSenderType();
    // if (senderType == 0) {
    if (senderType == Post::UserToUser) {
        // AddPostToUser(3, senderId, receiverId, post)
        AddPostToUser((Post::ConnectedSender | Post::ConnectedReceiver), senderId, receiverId, post);
        return;
    }
    // if (senderType > 0) {
    if (senderType > Post::UserToUser) {
        // if (senderType > 6) {
        if (senderType > Post::Mentoring) {
            // if (senderType == 7) {
            if (senderType == Post::InGameShopGift) {
                // AddPostToUser(3, senderId, receiverId, post)
                AddPostToUser((Post::ConnectedSender | Post::ConnectedReceiver), senderId, receiverId, post);
                return;
            }
            if (7U < static_cast<unsigned int>(senderType) - 8U) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Invalid Type[%d]",
                //     L"Post.cpp",
                //     0x37a,
                //     senderType)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] Invalid Type[%d]",
                //     L"Post.cpp",
                //     0x37a,
                //     senderType)
                L2_LOG_ERROR(
                    Log,
                    "Mail %d has unsupported sender type %d (sender %d, receiver %d); state is sent, but adding it to user "
                    "lists is skipped.",
                    mailId, senderType, senderId, receiverId
                );
                return;
            }
        }
        // AddPostToUser(2, senderId, receiverId, post)
        AddPostToUser(Post::ConnectedReceiver, senderId, receiverId, post);
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Invalid Type[%d]",
    //     L"Post.cpp",
    //     0x37a,
    //     senderType)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Invalid Type[%d]",
    //     L"Post.cpp",
    //     0x37a,
    //     senderType)
    L2_LOG_ERROR(
        Log,
        "Mail %d has unsupported sender type %d (sender %d, receiver %d); state is sent, but adding it to user lists is "
        "skipped.",
        mailId, senderType, senderId, receiverId
    );
}

void PostUtil::OpenPost(const int mailId, const int receiverId, const int senderId) {
    // Current L2Server.exe 0x009e45e8.
    L2_GUARD_FUNCTION();

    CSPointer<CPost> post = FindPostByUserWithRef(mailId, receiverId, senderId);
    if (!post) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"No opened post %d found!",
        //     mailId)
        // L2_LOG_ERROR(Log,
        //     L"No opened post %d found!",
        //     mailId)
        L2_LOG_ERROR(
            Log, "Cannot mark mail %d opened: not found in receiver %d or sender %d lists.", mailId, receiverId, senderId
        );
        return;
    }

    GuardHelper lockGuard(L"OpenPost", CThreadManager::GetThreadID());
    L2_GUARD(post->m_ProcessLock);
    if (static_cast<int>(post->m_eProcessType) < 2) {
        if (post->m_bReturnable || post->m_eJobType == CPost::JobTrade || post->m_bHaveItem ||
            // post->m_nSenderType == 4 ||
            post->m_nSenderType == Post::CommissionToUserDelete ||
            // post->m_nSenderType == 5) {
            post->m_nSenderType == Post::CommissionToUserBuy) {
            post->m_eProcessType = Post::ProcessOpened;
        } else {
            post->m_eProcessType = Post::ProcessReceived;
        }
    }
}

void PostUtil::ReceivePost(const int mailId, const int receiverId, const int senderId) {
    // Current L2Server.exe 0x009e4820.
    L2_GUARD_FUNCTION();

    CSPointer<CPost> post = FindPostByUserWithRef(mailId, receiverId, senderId);
    if (!post) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"No received post %d found! [ReceivePost]",
        //     mailId)
        // L2_LOG_ERROR(Log,
        //     L"No received post %d found! [ReceivePost]",
        //     mailId)
        L2_LOG_ERROR(
            Log, "Cannot mark mail %d received: not found in receiver %d or sender %d lists.", mailId, receiverId, senderId
        );
        return;
    }

    GuardHelper lockGuard(L"ReceivePost", CThreadManager::GetThreadID());
    L2_GUARD(post->GetProcessLock());
    post->SetReceived();
}

void PostUtil::CancelPost(const int mailId, const int receiverId, const int senderId, int ignored [[maybe_unused]]) {
    // Current L2Server.exe 0x009e5600. The fourth ABI argument is kept
    // by the shipping signature but is not read by the body.
    L2_GUARD_FUNCTION();

    CSPointer<CPost> post = FindPostByUserWithRef(mailId, receiverId, senderId);
    if (!post) {
        return;
    }

    WriteTimeForDebug(post->GetNextJobTime());
    if (static_cast<int>(post->GetProcessType()) > 2) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Error! Received post %d canceled!",
        //     mailId)
        // L2_LOG_ERROR(Log,
        //     L"Error! Received post %d canceled!",
        //     mailId)
        L2_LOG_ERROR(
            Log, "Mail %d cancellation continues despite process type %d (sender %d, receiver %d).", mailId,
            static_cast<int>(post->GetProcessType()), senderId, receiverId
        );
    }
    post->AddDeleted(3);

    CSPointer<User> receiver = post->GetReceiver();
    if (receiver) {
        receiver->m_pPostInfo->RemoveReceivedPost(post->Id());
    }
    CSPointer<User> sender = post->GetSender();
    if (sender) {
        sender->m_pPostInfo->RemoveSentPost(post->Id());
    }
}

void PostUtil::DeletePost(const int mailId, int receiverId, int senderId, unsigned char deleteMask) {
    // Current L2Server.exe 0x009e592c.
    L2_GUARD_FUNCTION();

    CSPointer<CPost> post = FindPostByUserWithRef(mailId, receiverId, senderId);
    if (!post) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"No received post %d found! [DeletePost]",
        //     mailId)
        // L2_LOG_ERROR(Log,
        //     L"No received post %d found! [DeletePost]",
        //     mailId)
        L2_LOG_ERROR(
            Log,
            "Mail %d deletion: not found in receiver %d or sender %d lists; client notifications still follow delete mask "
            "0x%02x.",
            mailId, receiverId, senderId, static_cast<unsigned>(deleteMask)
        );
    } else {
        receiverId = post->GetReceiverId();
        senderId = post->GetSenderId();
        {
            CSPointer<User> receiver = post->GetReceiver();
            CSPointer<User> sender = post->GetSender();
            const int senderType = post->GetSenderType();
            // if (senderType == 0) {
            if (senderType == Post::UserToUser) {
                post->AddDeleted(deleteMask);
                if (post->CanDelete()) {
                    if (receiver) {
                        receiver->m_pPostInfo->RemoveReceivedPost(post->Id());
                    }
                    if (sender) {
                        sender->m_pPostInfo->RemoveSentPost(post->Id());
                    }
                }
                // } else if (senderType > 0) {
            } else if (senderType > Post::UserToUser) {
                // if (senderType > 6) {
                if (senderType > Post::Mentoring) {
                    // if (senderType == 7) {
                    if (senderType == Post::InGameShopGift) {
                        post->AddDeleted(deleteMask);
                        if (post->CanDelete()) {
                            if (receiver) {
                                receiver->m_pPostInfo->RemoveReceivedPost(post->Id());
                            }
                            if (sender) {
                                sender->m_pPostInfo->RemoveSentPost(post->Id());
                            }
                        }
                    } else if (7U < static_cast<unsigned int>(senderType) - 8U) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%s][%d] Invalid Type[%d]",
                        //     L"Post.cpp",
                        //     0x43f,
                        //     senderType)
                        // L2_LOG_ERROR(Log,
                        //     L"[%s][%d] Invalid Type[%d]",
                        //     L"Post.cpp",
                        //     0x43f,
                        //     senderType)
                        L2_LOG_ERROR(
                            Log,
                            "Mail %d deletion: unsupported sender type %d (sender %d, receiver %d); list cleanup skipped, "
                            "client notifications still follow mask 0x%02x.",
                            mailId, senderType, senderId, receiverId, static_cast<unsigned>(deleteMask)
                        );
                    } else {
                        // deleteMask = 2.
                        deleteMask = Post::ConnectedReceiver;
                        if (receiver) {
                            receiver->m_pPostInfo->RemoveReceivedPost(post->Id());
                        }
                    }
                } else {
                    // deleteMask = 2.
                    deleteMask = Post::ConnectedReceiver;
                    if (receiver) {
                        receiver->m_pPostInfo->RemoveReceivedPost(post->Id());
                    }
                }
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Invalid Type[%d]",
                //     L"Post.cpp",
                //     0x43f,
                //     senderType)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] Invalid Type[%d]",
                //     L"Post.cpp",
                //     0x43f,
                //     senderType)
                L2_LOG_ERROR(
                    Log,
                    "Mail %d deletion: unsupported sender type %d (sender %d, receiver %d); list cleanup skipped, client "
                    "notifications still follow mask 0x%02x.",
                    mailId, senderType, senderId, receiverId, static_cast<unsigned>(deleteMask)
                );
            }
        }
    }

    std::array<char, 1024> packet;
    // if ((deleteMask & 2U) != 0) {
    if ((deleteMask & static_cast<unsigned>(Post::ConnectedReceiver)) != 0) {
        CSPointer<User> receiver = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, receiverId));
        if (receiver) {
            const int packetLength = ::Assemble(packet.data(), static_cast<int>(packet.size()), "dd", mailId, 0);
            receiver->GetSocket()->Send("chddb", 0xfe, 0xb4, 1, 1, packetLength, packet.data());
        }
    }
    // if ((deleteMask & 1U) != 0) {
    if ((deleteMask & static_cast<unsigned>(Post::ConnectedSender)) != 0) {
        CSPointer<User> sender = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, senderId));
        if (sender) {
            const int packetLength = ::Assemble(packet.data(), static_cast<int>(packet.size()), "dd", mailId, 0);
            sender->GetSocket()->Send("chddb", 0xfe, 0xb4, 0, 1, packetLength, packet.data());
        }
    }
}

void PostUtil::MakeNewPost(
    const int mailId,
    const int senderId,
    const int receiverId,
    const int trade,
    const bool returnable,
    const Post::PostProcessType processType,
    const std::int32_t time,
    const bool haveItem,
    const int senderType,
    const unsigned char userMask,
    const bool findExisting,
    const unsigned char deleted
) {
    // Current L2Server.exe 0x009e76cc.
    L2_GUARD_FUNCTION();

    const std::int32_t nextJobTime = DecideNextJobTime(time, processType, trade, returnable, false);
    if (findExisting) {
        CSPointer<CPost> existing = FindPostByUserWithRef(mailId, receiverId, senderId);
        if (existing) {
            AddPostToUser(userMask, senderId, receiverId, existing);
            return;
        }
    }

    CPost* const created = new CPost();
    if (created == nullptr) {
        return;
    }
    CSPointer<CPost> post(created, ObjectRefType::FunctionChain);
    L2_RELEASE(created, ObjectRefType::Anonymous);
    created->Init(
        mailId, senderId, receiverId, returnable, trade != 0, userMask, processType, deleted, nextJobTime, haveItem,
        senderType
    );
    WriteTimeForDebug(nextJobTime);
    if (processType == Post::ProcessWaiting && findExisting) {
        db.RequestSendPost(created->Id());
    }
    AddPostToUser(userMask, senderId, receiverId, post);
}

bool PostUtil::CheckWritePost(
    CSocket* socket,
    User* user,
    int itemCount,
    CPost::ItemPair items[],
    std::int64_t unused,
    std::int64_t& fee,
    ItemConsumeType consumeTypes[]
) {
    // Current L2Server.exe 0x009e4b4c.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);
    static_cast<void>(unused);

    CUserSocket* const userSocket = user->GetSocket();
    if (userSocket->m_bEconomicRestriction) {
        user->SendSystemMessage(SystemMessageId::ACCOUNT_NOT_PAID);
        return false;
    }
    if (!AreaBasedRule::CancelPost(user) && itemCount > 0) {
        user->SendSystemMessage(SystemMessageId::CANNOT_SEND_MAIL_OUTSIDE_PEACE_ZONE);
        return false;
    }
    if (user->IsNowTrade()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_SEND_MAIL_DURING_TRADE);
        return false;
    }
    if (user->GetPrivateStoreType() != 0) {
        user->SendSystemMessage(SystemMessageId::CANNOT_SEND_MAIL_DURING_PRIVATE_STORE_OR_WORKSHOP);
        return false;
    }
    if (user->GetPendingUseETCItem(PUIT_ENCHANT) != nullptr ||
        user->GetPendingUseETCItem(PUIT_ATTRIBUTE_ENCHANT) != nullptr ||
        user->GetPendingUseETCItem(PUIT_HLOCKED) != nullptr) {
        user->SendSystemMessage(SystemMessageId::CANNOT_SEND_MAIL_DURING_ENCHANT_ATTRIBUTE_JEWEL_SEAL_OR_UNSEAL_OPERATION);
        return false;
    }
    if (user->GetPendingUseETCItem(PUIT_SHAPE_SHIFTING) != nullptr || !user->IsItemUsable(0)) {
        return false;
    }
    if (user->m_data->m_TrainingState == 1) {
        user->SendSystemMessage(SystemMessageId::CANNOT_TAKE_OTHER_ACTIONS_WHILE_IN_TRAINING_CAMP);
        return false;
    }
    if (runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 1) == 1) {
        user->SendSystemMessage(SystemMessageId::FINISH_PREVIOUS_OPERATION_BEFORE_RETRYING);
        return false;
    }

    fee = g_nPostFeeBase;
    std::int64_t transferredAdena = 0;
    bool valid = true;
    if (itemCount > 0) {
        std::optional<std::set<int>> uniqueDatabaseIdsStorage;
        const bool continueRequest = [&] {
            L2_READ_GUARD(user->m_inv);
            std::set<int>& uniqueDatabaseIds = uniqueDatabaseIdsStorage.emplace();
            for (int index = 0; index < itemCount; ++index) {
                const int databaseId = items[index].db_id;
                if (!uniqueDatabaseIds.insert(databaseId).second) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d][Kick] write post. but item is duplicated. "
                    //     L"user[%s] item[%d:%d]",
                    //     L"Post.cpp",
                    //     0x4f8,
                    //     user->GetName(),
                    //     databaseId,
                    //     items[index].amount)
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d][Kick] write post. but item is duplicated. "
                    //     L"user[%s] item[%d:%d]",
                    //     L"Post.cpp",
                    //     0x4f8,
                    //     user->GetName(),
                    //     databaseId,
                    //     items[index].amount)
                    // L2_LOG_ERROR(Log,
                    //     "Mail send rejected for character '%s' (DBID %u): duplicate item DBID %d, requested amount %lld.
                    //     closing the client connection.", WideToUtf8(std::wstring_view(user->GetName())).c_str(),
                    //     static_cast<unsigned>(user->GetDBID()), databaseId,
                    //     static_cast<long long>(items[index].amount))
                    L2_LOG_ERROR(
                        Log,
                        "Mail send rejected for character '%s' (DBID %u): duplicate item DBID %d, requested amount %lld; "
                        "closing the client connection.",
                        Utf16ToUtf8(user->GetName()).c_str(), static_cast<unsigned>(user->GetDBID()), databaseId,
                        static_cast<long long>(items[index].amount)
                    );
                    userSocket->Close();
                    return false;
                }

                CItem* const item = user->m_inv.GetByDBID(databaseId);
                if (item == nullptr || !item->IsTradable(user, false)) {
                    valid = false;
                    break;
                }

                const ItemConsumeType consumeType = item->m_data->m_ItemConsumeType;
                bool amountValid = false;
                if (consumeType == ICT_ASSET) {
                    amountValid = items[index].amount > 0 && items[index].amount <= 999999999999LL;
                } else if (items[index].amount > 0 && items[index].amount <= 1000000LL) {
                    std::array<char, 32> amountText{};
                    std::snprintf(amountText.data(), amountText.size(), "%lld", static_cast<long long>(items[index].amount));
                    amountValid = std::strlen(amountText.data()) < 9;
                }
                if (!amountValid) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "invalid amount[%I64d] (Request) at file[%s], line[%d]",
                    //     items[index].amount,
                    //     "Post.cpp",
                    //     0x515)
                    // L2_LOG_ERROR(Log,
                    //     "invalid amount[%I64d] (Request) at file[%s], line[%d]",
                    //     items[index].amount,
                    //     "Post.cpp",
                    //     0x515)
                    L2_LOG_ERROR(
                        Log, "Mail send rejected for character %u: invalid amount %lld for item DBID %d (consume type %d).",
                        static_cast<unsigned>(user->GetDBID()), static_cast<long long>(items[index].amount), databaseId,
                        static_cast<int>(consumeType)
                    );
                    userSocket->SendSystemMessage(SystemMessageId::CANNOT_SEND_MAIL_WITH_UNSUITABLE_ITEM);
                    return false;
                }

                const std::int64_t available = item->m_data->m_nCount;
                if (items[index].amount > available && available > 0) {
                    items[index].amount = available;
                } else if (available == 0) {
                    valid = false;
                    break;
                }
                if (item->m_nClassId == 0x39) {
                    transferredAdena = runtime::AddInt64Wrapping(transferredAdena, items[index].amount);
                }

                int slotIndex = 0;
                while (slotIndex < ST_MAX && (ST2SBT[slotIndex] & item->m_data->m_Slot) == 0) {
                    ++slotIndex;
                }
                const int equippedIndex =
                    slotIndex < ST_MAX ? user->m_data->m_nSlotIndex[slotIndex] : user->m_data->m_OriginalStat.m_N[0];
                CEtcItem* const etcItem = item->IsEtcItem() ? item->CastEtcItem() : nullptr;
                if (equippedIndex == item->GetIndex() || (etcItem != nullptr && !etcItem->m_bPetCollarTradable)) {
                    valid = false;
                    break;
                }
                consumeTypes[index] = consumeType;
                fee = runtime::AddInt64Wrapping(fee, g_nPostFeePerSlot);
            }
            return true;
        }();
        if (!continueRequest) {
            runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
            return false;
        }
    }

    if (!valid) {
        userSocket->SendSystemMessage(SystemMessageId::CANNOT_SEND_MAIL_WITH_UNSUITABLE_ITEM);
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        return false;
    }

    std::int64_t availableAdena = 0;
    if (CItem* const adena = user->m_inv.GetFirstItemByClassID(0x39, false, false)) {
        availableAdena = adena->m_data->m_nCount;
    }
    if (runtime::AddInt64Wrapping(transferredAdena, fee) <= availableAdena) {
        return true;
    }

    runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
    user->SendSystemMessage(SystemMessageId::CANNOT_SEND_MAIL_NOT_ENOUGH_ADENA);
    return false;
}

bool PostUtil::CheckCancelPost(CSocket* socket, User* user, int itemCount, PostItemInfo items[], std::int64_t unused) {
    // Current L2Server.exe 0x009e6f10.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);
    static_cast<void>(unused);

    if (!AreaBasedRule::CancelPost(user)) {
        user->SendSystemMessage(SystemMessageId::CANNOT_CANCEL_MAIL_OUTSIDE_PEACE_ZONE);
        return false;
    }
    if (user->IsNowTrade()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_CANCEL_MAIL_DURING_TRADE);
        return false;
    }
    if (user->GetPrivateStoreType() != 0) {
        user->SendSystemMessage(SystemMessageId::CANNOT_CANCEL_MAIL_DURING_PRIVATE_STORE_OR_WORKSHOP);
        return false;
    }
    if (user->GetPendingUseETCItem(PUIT_ENCHANT) != nullptr ||
        user->GetPendingUseETCItem(PUIT_ATTRIBUTE_ENCHANT) != nullptr ||
        user->GetPendingUseETCItem(PUIT_HLOCKED) != nullptr) {
        user->SendSystemMessage(SystemMessageId::CANNOT_CANCEL_MAIL_DURING_ENCHANT_ATTRIBUTE_SEAL_OR_UNSEAL_OPERATION);
        return false;
    }
    if (user->GetPendingUseETCItem(PUIT_SHAPE_SHIFTING) != nullptr || !user->IsItemUsable(0)) {
        return false;
    }
    if (user->m_data->m_TrainingState == 1) {
        user->SendSystemMessage(SystemMessageId::CANNOT_TAKE_OTHER_ACTIONS_WHILE_IN_TRAINING_CAMP);
        return false;
    }
    if (itemCount < 1) {
        return true;
    }

    ItemAmountDataStruct addableItems;
    for (int index = 0; index < itemCount; ++index) {
        if (items[index].class_id < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]Invalid withdraw item. dbid[%d], count[%lld]",
            //     "Post.cpp",
            //     0x5ca,
            //     items[index].db_id,
            //     static_cast<long long>(items[index].amount))
            // L2_LOG_ERROR(Log,
            //     L"[%S][%d]Invalid withdraw item. dbid[%d], count[%lld]",
            //     "Post.cpp",
            //     0x5ca,
            //     items[index].db_id,
            //     static_cast<long long>(items[index].amount))
            L2_LOG_ERROR(
                Log,
                "Mail cancellation rejected for character %u: attachment index %d has invalid item class %d (DBID %d, "
                "amount %lld).",
                static_cast<unsigned>(user->GetDBID()), index, items[index].class_id, items[index].db_id,
                static_cast<long long>(items[index].amount)
            );
            return false;
        }
        addableItems.Push(items[index].class_id, items[index].amount);
    }
    if (!user->m_inv.CheckAddableMulti(addableItems, false)) {
        user->SendSystemMessage(SystemMessageId::MAIL_RECEIPT_CANCELLATION_FAILED_INVENTORY_ERROR);
        return false;
    }
    return runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 1) != 1;
}

bool PostUtil::CheckReceivePost(
    CSocket* socket, User* user, int itemCount, PostItemInfo items[], std::int64_t tradeMoney, bool returnable
) {
    // Current L2Server.exe 0x009e7204.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    if (!AreaBasedRule::CancelPost(user)) {
        user->SendSystemMessage(SystemMessageId::CANNOT_RECEIVE_MAIL_OUTSIDE_PEACE_ZONE);
        return false;
    }
    if (user->IsNowTrade()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_RECEIVE_MAIL_DURING_TRADE);
        return false;
    }
    if (user->GetPrivateStoreType() != 0) {
        user->SendSystemMessage(SystemMessageId::CANNOT_RECEIVE_MAIL_DURING_PRIVATE_STORE_OR_WORKSHOP);
        return false;
    }
    if (user->GetPendingUseETCItem(PUIT_ENCHANT) != nullptr ||
        user->GetPendingUseETCItem(PUIT_ATTRIBUTE_ENCHANT) != nullptr ||
        user->GetPendingUseETCItem(PUIT_HLOCKED) != nullptr) {
        user->SendSystemMessage(
            SystemMessageId::CANNOT_RECEIVE_MAIL_DURING_ENCHANT_ATTRIBUTE_JEWEL_SEAL_OR_UNSEAL_OPERATION
        );
        return false;
    }
    if (user->GetPendingUseETCItem(PUIT_SHAPE_SHIFTING) != nullptr || !user->IsItemUsable(0)) {
        return false;
    }
    if (user->m_data->m_TrainingState == 1) {
        user->SendSystemMessage(SystemMessageId::CANNOT_TAKE_OTHER_ACTIONS_WHILE_IN_TRAINING_CAMP);
        return false;
    }
    if (returnable && tradeMoney > user->m_inv.GetAdenaAmount()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_RECEIVE_MAIL_NOT_ENOUGH_ADENA);
        return false;
    }
    if (itemCount < 1) {
        return true;
    }

    ItemAmountDataStruct addableItems;
    for (int index = 0; index < itemCount; ++index) {
        if (items[index].class_id < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]Invalid withdraw item. dbid[%d], count[%lld]",
            //     "Post.cpp",
            //     0x638,
            //     items[index].db_id,
            //     static_cast<long long>(items[index].amount))
            // L2_LOG_ERROR(Log,
            //     L"[%S][%d]Invalid withdraw item. dbid[%d], count[%lld]",
            //     "Post.cpp",
            //     0x638,
            //     items[index].db_id,
            //     static_cast<long long>(items[index].amount))
            L2_LOG_ERROR(
                Log,
                "Mail receipt rejected for character %u: attachment index %d has invalid item class %d (DBID %d, amount "
                "%lld).",
                static_cast<unsigned>(user->GetDBID()), index, items[index].class_id, items[index].db_id,
                static_cast<long long>(items[index].amount)
            );
            return false;
        }
        addableItems.Push(items[index].class_id, items[index].amount);
    }
    if (!user->m_inv.CheckAddableMulti(addableItems, false)) {
        user->SendSystemMessage(SystemMessageId::MAIL_RECEIPT_FAILED);
        return false;
    }
    return runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 1) != 1;
}

void PostUtil::DeletePostByUserDisconnected(User* user) {
    // Current L2Server.exe 0x009e6e64.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        user->m_pPostInfo->Clear();
    }
}
