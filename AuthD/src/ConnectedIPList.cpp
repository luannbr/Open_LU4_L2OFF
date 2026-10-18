#include "ConnectedIPList.h"
#include "Shared/src/ScopedLockGuards.h"

#include "Log.h"
#include "WhiteListIPMgr.h"

#include <cstdint>
#include <cstring>

ConnectedIPList* ConnectedIPList::sInstance = nullptr;

namespace {

// ServerIdAndIp is the eight-byte stack value shown by PDB local 0x8F9E.
// The target writes the low byte of serverId followed by the four in_addr
// bytes and leaves the high three bytes zero. memcpy keeps that byte contract
// independent of the host's integer endianness.
std::int64_t MakeServerIdAndIp(
    int serverId,
    in_addr address) {
    std::uint64_t key = 0;
    auto* const bytes =
        reinterpret_cast<unsigned char*>(&key);
    bytes[0] = static_cast<unsigned char>(serverId);
    std::memcpy(bytes + 1, &address.s_addr, 4);
    return static_cast<std::int64_t>(key);
}

unsigned char* AddressBytes(
    std::int64_t& serverIdAndIp) {
    return reinterpret_cast<unsigned char*>(
               &serverIdAndIp) +
           1;
}

} // namespace

// L2Authd.exe 0x0041A780, Src/connectediplist.h:19.
ConnectedIPList* ConnectedIPList::Inst() {
    return sInstance;
}

// L2Authd.exe 0x00429980, Src/connectediplist.cpp:19-21. The target leaves
// connectionLimit untouched in this overload.
ConnectedIPList::ConnectedIPList()
    : mLock(eCriticalSection, 0) {
    sInstance = this;
}

// L2Authd.exe 0x004299F0, Src/connectediplist.cpp:24-27.
ConnectedIPList::ConnectedIPList(
    int limit)
    : mLock(eCriticalSection, 0) {
    sInstance = this;
    connectionLimit = limit;
}

// L2Authd.exe 0x00429A70, Src/connectediplist.cpp:31-37.
ConnectedIPList::~ConnectedIPList() {
    {
        L2_GUARD(mLock);
        for (auto& entry : mIPMap) {
            delete entry.second;
        }
        mIPMap.clear();
    }
}

// L2Authd.exe 0x00429B70, Src/connectediplist.cpp:95-97.
void ConnectedIPList::SetConnectionLimit(
    int limit) {
    connectionLimit = limit;
}

// L2Authd.exe 0x00429B90, Src/connectediplist.cpp:100-132.
bool ConnectedIPList::CheckIPOverConnection(
    int serverId,
    in_addr address,
    int uidToFind) {
    std::int64_t key =
        MakeServerIdAndIp(serverId, address);
    if (whiteListIPMgr.IsWhiteIP(AddressBytes(key))) {
        return false;
    }

    bool overConnectionLimit = false;
    {
        L2_GUARD(mLock);
        const auto found = mIPMap.find(key);
        if (found != mIPMap.end() &&
            found->second != nullptr) {
            int connectedCount = 0;
            UidListNode* const duplicate =
                found->second->Find(
                    uidToFind,
                    &connectedCount);
            if (duplicate == nullptr) {
                if (connectionLimit <= connectedCount) {
                    overConnectionLimit = true;
                }
            } else {
                // Log.AddLog(
                //     LOG_ERROR,
                //     "%s(%d):connection info(uid) is already exists, "
                //     "please check the logic",
                //     "ConnectedIPList.cpp",
                //     126)
                // Log.AddLog(
                //     LOG_WARN,
                //     "component=authentication event=duplicate_connection_record server_id=%d user_id=%d action=allow_existing_record limit_check=skipped",
                //     serverId, uidToFind)
                // Log.AddLog(
                //     LOG_WARN,
                //     "[client] Server %d already has a connection record for account %d. Allowing the existing record without checking the IP limit again.",
                //     serverId, uidToFind)
                L2_LOG_WARN(Log,
                    "[client] Server %d already has a connection record for account %d. Allowing the existing record without checking the IP limit again.",
                    serverId, uidToFind);
            }
        }
    }
    return overConnectionLimit;
}

// L2Authd.exe 0x00429CD0, Src/connectediplist.cpp:135-157.
void ConnectedIPList::AddConnectedIp(
    int serverId,
    in_addr address,
    int connectedUid,
    std::int32_t loginTime) {
    const std::int64_t key =
        MakeServerIdAndIp(serverId, address);

    {
        L2_GUARD(mLock);
        const auto found = mIPMap.find(key);
        if (found != mIPMap.end()) {
            if (found->second != nullptr) {
                found->second->Add(connectedUid, loginTime);
            }
        } else {
            auto* const list = new UidListNode;
            list->uid = connectedUid;
            list->logintime = loginTime;
            mIPMap.insert({key, list});
        }
    }
}

// L2Authd.exe 0x00429E50, Src/connectediplist.cpp:160-178.
void ConnectedIPList::RemoveConnectedIp(
    int serverId,
    in_addr address,
    int connectedUid) {
    const std::int64_t key =
        MakeServerIdAndIp(serverId, address);

    {
        L2_GUARD(mLock);
        const auto found = mIPMap.find(key);
        if (found != mIPMap.end()) {
            if (found->second != nullptr) {
                found->second->Remove(
                    &found->second,
                    connectedUid);
            }
            if (found->second == nullptr) {
                mIPMap.erase(found);
            }
        }
    }
}

// PrintMapDebug is present in PDB field list 0x826F but has no compiled body or
// reference in the selected EXE. It remains declaration-only.

// L2Authd.exe 0x00429F60, Src/connectediplist.cpp:40-42.
ConnectedIPList::UidListNode::UidListNode()
    : next(nullptr) {}

// L2Authd.exe 0x00429F80, Src/connectediplist.cpp:45-47.
ConnectedIPList::UidListNode::~UidListNode() {
    delete next;
}

// L2Authd.exe 0x00429FD0, Src/connectediplist.cpp:50-55.
ConnectedIPList::UidListNode*
ConnectedIPList::UidListNode::at(
    int index) {
    if (index < 0) {
        return nullptr;
    }
    if (index == 0) {
        return this;
    }
    if (next == nullptr) {
        return nullptr;
    }
    return next->at(index - 1);
}

// L2Authd.exe 0x0042A020, Src/connectediplist.cpp:58-63.
ConnectedIPList::UidListNode*
ConnectedIPList::UidListNode::Find(
    int uidToFind,
    int* countToFind) {
    ++*countToFind;
    if (uid == uidToFind) {
        return this;
    }
    if (next == nullptr) {
        return nullptr;
    }
    return next->Find(uidToFind, countToFind);
}

// L2Authd.exe 0x0042A070, Src/connectediplist.cpp:66-78.
bool ConnectedIPList::UidListNode::Add(
    int connectedUid,
    std::int32_t loginTime) {
    if (uid == connectedUid) {
        logintime = loginTime;
        return false;
    }
    if (next == nullptr) {
        next = new UidListNode;
        next->uid = connectedUid;
        next->logintime = loginTime;
        return true;
    }
    return next->Add(connectedUid, loginTime);
}

// L2Authd.exe 0x0042A140, Src/connectediplist.cpp:81-92.
bool ConnectedIPList::UidListNode::Remove(
    UidListNode** owner,
    int connectedUid) {
    if (uid == connectedUid) {
        *owner = next;
        next = nullptr;
        delete this;
        return true;
    }
    if (next == nullptr) {
        return false;
    }
    return next->Remove(&next, connectedUid);
}
