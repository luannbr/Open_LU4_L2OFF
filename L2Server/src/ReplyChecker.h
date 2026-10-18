#pragma once

#include "Shared/src/IOC.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <cstddef>
#include <cstdint>

class User;

// Current ReplyChecker.cpp owner. The class declaration is complete even
// while its out-of-line lifecycle is recovered separately: current allocates
// 0x28 bytes and stores the checker kind and the two participant smart IDs
// immediately after CIOObject's 0x18-byte prefix.
class CReplyChecker : public CIOObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CReplyChecker)

    CReplyChecker(int checkerType, User* requester, User* replier);
    ~CReplyChecker() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()

    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void TimerExpired(int id) override;

    static bool IsUserWaitingReply(User* user);
    static CSPointer<User> GetRequesterFromReplier(User* user);
    static CSPointer<User> GetReplierFromRequester(User* user);
    static bool AttachReplyChecker(int checkerType, User* requester, User* replier, int timeoutSeconds);
    static void FinishWaitingReplyByReplier(User* user);
    static void FinishWaitingReplyByRequester(User* user);

private:
    using ReplyCheckerMap = CObjectMap<CReplyChecker, 1, 5, RWLock, int>;

    std::int32_t m_eReplyChecker;
    std::uint32_t m_nRequesterId;
    std::uint32_t m_nReplierId;
    [[maybe_unused]] std::byte m_alignment24To28[4];

    static ReplyCheckerMap s_ReplyCheckerMap;
    static RWLock s_ReplyCheckerLock;

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CReplyChecker)
};


// Header-owned scope helper used by reply handlers. Its constructor is
// inlined in the current image. The destructor is the only compiled body.
class CReplyCheckerHelper {
public:
    CReplyCheckerHelper(User* user, int type) noexcept : m_pUser(user), m_nType(type), m_alignment0cTo10{} {}
    ~CReplyCheckerHelper();

private:
    User* m_pUser;
    std::int32_t m_nType;
    [[maybe_unused]] std::byte m_alignment0cTo10[4];
};
