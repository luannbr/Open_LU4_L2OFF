#include "ReplyChecker.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/ScopedLockGuards.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Exception.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"

#include <cstdint>
#include <cstring>

namespace {

char kReplyCheckerSource[] = "ReplyChecker.cpp";


} // namespace

// static_assert(L2ServerMemory::kCReplyCheckerClassId == 251)
static_assert(static_cast<std::int32_t>(ShippingMemoryClassId::CReplyChecker) == 251);

CReplyChecker::ReplyCheckerMap CReplyChecker::s_ReplyCheckerMap;
RWLock CReplyChecker::s_ReplyCheckerLock;

CReplyChecker::CReplyChecker(int checkerType, User* requester, User* replier) :
    m_eReplyChecker(checkerType),
    m_nRequesterId(requester->Id()),
    m_nReplierId(replier == nullptr ? 0U : replier->Id()),
    m_alignment24To28{} {
    L2_GUARD_FUNCTION();
}

CReplyChecker::~CReplyChecker() { L2_GUARD_FUNCTION(); }

L2_DEFINE_OBJECT_CLASS_ID(CReplyChecker)

L2_DEFINE_OBJECT_CLASS_NAME(CReplyChecker, m_pClassName_CReplyChecker)

L2_DEFINE_OBJECT_ALLOCATOR(CReplyChecker, __GetClassName())

L2_DEFINE_OBJECT_NEW(CReplyChecker)

L2_DEFINE_OBJECT_RELEASE(CReplyChecker)

bool CReplyChecker::IsUserWaitingReply(User* user) {
    L2_GUARD_FUNCTION();
    CSPointer<CReplyChecker> checker;
    const bool waiting = [&]() -> bool {
        L2_READ_GUARD(s_ReplyCheckerLock);
        checker = s_ReplyCheckerMap.FindObject(static_cast<int>(user->Id()));
        const bool waiting = static_cast<bool>(checker);
        return waiting;
    }();
    return waiting;
}

CSPointer<User> CReplyChecker::GetRequesterFromReplier(User* user) {
    L2_GUARD_FUNCTION();
    std::uint32_t requesterId = 0;
    CSPointer<CReplyChecker> checker;
    {
        L2_WRITE_GUARD(s_ReplyCheckerLock);
        checker = s_ReplyCheckerMap.FindObject(static_cast<int>(user->Id()));
        if (checker) {
            requesterId = checker->m_nRequesterId;
            if (requesterId != 0) {
                const CSPointer<CReplyChecker> counterpart = s_ReplyCheckerMap.FindObject(static_cast<int>(requesterId));
                if (counterpart.get() != checker.get()) {
                    requesterId = 0;
                }
            }
        }
    }
    return requesterId == 0 ? CSPointer<User>{} : g_UserMap.FindObject(static_cast<int>(requesterId));
}

CSPointer<User> CReplyChecker::GetReplierFromRequester(User* user) {
    L2_GUARD_FUNCTION();
    std::uint32_t replierId = 0;
    CSPointer<CReplyChecker> checker;
    {
        L2_WRITE_GUARD(s_ReplyCheckerLock);
        checker = s_ReplyCheckerMap.FindObject(static_cast<int>(user->Id()));
        if (checker) {
            replierId = checker->m_nReplierId;
            if (replierId != 0) {
                const CSPointer<CReplyChecker> counterpart = s_ReplyCheckerMap.FindObject(static_cast<int>(replierId));
                if (counterpart.get() != checker.get()) {
                    replierId = 0;
                }
            }
        }
    }
    return replierId == 0 ? CSPointer<User>{} : g_UserMap.FindObject(static_cast<int>(replierId));
}

void CReplyChecker::TimerExpired(int) {
    L2_GUARD_FUNCTION();

    CSPointer<CReplyChecker> requesterEntry;
    CSPointer<CReplyChecker> replierEntry;
    bool requesterMismatch;
    bool replierMismatch;
    {
        L2_WRITE_GUARD(s_ReplyCheckerLock);
        requesterEntry = s_ReplyCheckerMap.FindObject(static_cast<int>(m_nRequesterId));
        requesterMismatch = requesterEntry.get() != this;
        if (!requesterMismatch) {
            s_ReplyCheckerMap.DeleteObject(static_cast<int>(m_nRequesterId), kReplyCheckerSource, 0x3a);
        }

        replierEntry = s_ReplyCheckerMap.FindObject(static_cast<int>(m_nReplierId));
        replierMismatch = replierEntry.get() != this;
        if (!replierMismatch) {
            s_ReplyCheckerMap.DeleteObject(static_cast<int>(m_nReplierId), kReplyCheckerSource, 0x40);
        }
    }

    if (requesterMismatch || replierMismatch) {
        return;
    }

    const CSPointer<User> requester = g_UserMap.FindObject(static_cast<int>(m_nRequesterId));
    const CSPointer<User> replier = g_UserMap.FindObject(static_cast<int>(m_nReplierId));

    if (m_eReplyChecker == 0) {
        if (requester && replier) {
            requester->GetSocket()->SendSystemMessage_Char(
                SystemMessageId::CLAN_INVITATION_CANCELLED_TARGET_DID_NOT_RESPOND, replier->GetName()
            );
            replier->GetSocket()->SendSystemMessage_Char(
                SystemMessageId::CLAN_INVITATION_CANCELLED_YOU_DID_NOT_RESPOND, requester->GetName()
            );
        }
        return;
    }

    if (m_eReplyChecker == 1) {
        if (!requester) {
            return;
        }
        requester->GetSocket()->SendSystemMessage(SystemMessageId::CLAN_OPERATION_ALREADY_IN_PROGRESS);
        if (replier) {
            requester->GetSocket()->SendSystemMessage_Char(
                SystemMessageId::PARTY_INVITATION_EXPIRED_NO_TARGET_RESPONSE, replier->GetName()
            );
            replier->GetSocket()->SendSystemMessage_Char(
                SystemMessageId::PARTY_INVITATION_EXPIRED_NO_OWN_RESPONSE, requester->GetName()
            );
        }
        return;
    }

    if (m_eReplyChecker == 2) {
        if (requester && replier) {
            requester->GetSocket()->SendSystemMessage_Char(
                SystemMessageId::PARTY_INVITATION_EXPIRED_NO_TARGET_RESPONSE, replier->GetName()
            );
            replier->GetSocket()->SendSystemMessage_Char(
                SystemMessageId::PARTY_INVITATION_EXPIRED_NO_OWN_RESPONSE, requester->GetName()
            );
        }
        return;
    }

    if (m_eReplyChecker == 6) {
        if (requester && replier) {
            requester->GetSocket()->SendSystemMessage(SystemMessageId::ALLIANCE_INVITATION_CANCELLED_TARGET_DID_NOT_RESPOND);
            replier->GetSocket()->SendSystemMessage(SystemMessageId::ALLIANCE_INVITATION_CANCELLED_YOU_DID_NOT_RESPOND);
        }
        return;
    }

    if (m_eReplyChecker == 9 && requester && replier) {
        requester->GetSocket()->SendSystemMessage_Char(
            SystemMessageId::COMMAND_CHANNEL_INVITATION_FAILED_NO_RESPONSE, replier->GetName()
        );
    }
}

bool CReplyChecker::AttachReplyChecker(int checkerType, User* requester, User* replier, int timeoutSeconds) {
    L2_GUARD_FUNCTION();
    const int requesterId = static_cast<int>(requester->Id());
    const int replierId = static_cast<int>(replier->Id());
    if (requesterId == replierId && !requester->m_bPartyBuilder) {
        return false;
    }

    bool attached = false;
    CSPointer<CReplyChecker> checker;
    {
        L2_WRITE_GUARD(s_ReplyCheckerLock);
        checker = s_ReplyCheckerMap.FindObject(requesterId);
        if (!checker) {
            checker = s_ReplyCheckerMap.FindObject(replierId);
        }
        if (!checker) {
            CReplyChecker* const created = new CReplyChecker(checkerType, requester, replier);
            s_ReplyCheckerMap.Insert(requesterId, created, kReplyCheckerSource, 0xc6);
            s_ReplyCheckerMap.Insert(replierId, created, kReplyCheckerSource, 0xc7);
            created->AddTimer(static_cast<std::int64_t>(runtime::MultiplyInt32Wrapping(timeoutSeconds, 1000)), 0);
            attached = true;
            L2_RELEASE(created, ObjectRefType::CreateDelete);
        }
    }
    return attached;
}

void CReplyChecker::FinishWaitingReplyByReplier(User* user) {
    L2_GUARD_FUNCTION();
    const int userId = static_cast<int>(user->Id());
    CSPointer<CReplyChecker> checker;
    {
        L2_WRITE_GUARD(s_ReplyCheckerLock);
        checker = s_ReplyCheckerMap.FindObject(userId);
        if (checker) {
            const std::uint32_t requesterId = checker->m_nRequesterId;
            if (requesterId != 0) {
                const CSPointer<CReplyChecker> counterpart = s_ReplyCheckerMap.FindObject(static_cast<int>(requesterId));
                if (counterpart.get() == checker.get()) {
                    s_ReplyCheckerMap.DeleteObject(static_cast<int>(requesterId), kReplyCheckerSource, 0x137);
                }
            }
            s_ReplyCheckerMap.DeleteObject(userId, kReplyCheckerSource, 0x13a);
        }
    }
}

void CReplyChecker::FinishWaitingReplyByRequester(User* user) {
    L2_GUARD_FUNCTION();
    const int userId = static_cast<int>(user->Id());
    CSPointer<CReplyChecker> checker;
    {
        L2_WRITE_GUARD(s_ReplyCheckerLock);
        checker = s_ReplyCheckerMap.FindObject(userId);
        if (checker) {
            const std::uint32_t replierId = checker->m_nReplierId;
            if (replierId != 0) {
                const CSPointer<CReplyChecker> counterpart = s_ReplyCheckerMap.FindObject(static_cast<int>(replierId));
                if (counterpart.get() == checker.get()) {
                    s_ReplyCheckerMap.DeleteObject(static_cast<int>(replierId), kReplyCheckerSource, 0x155);
                }
            }
            s_ReplyCheckerMap.DeleteObject(userId, kReplyCheckerSource, 0x157);
        }
    }
}

CReplyCheckerHelper::~CReplyCheckerHelper() {
    if (m_nType == 0) {
        CReplyChecker::FinishWaitingReplyByReplier(m_pUser);
    } else if (m_nType == 1) {
        CReplyChecker::FinishWaitingReplyByRequester(m_pUser);
    }
}
