#pragma once

#include "Platform/WindowsTypes.h"
#include "Lock.h"

#include <cstdint>
#include <map>

// Original owner: Src/connectediplist.h. L2Authd.pdb type 0x8270, target
// size 0x2C. The spelling is ConnectedIPList in both PDB and EXE.
class ConnectedIPList {
public:
    class UidListNode {
    public:
        UidListNode();
        ~UidListNode();

        UidListNode* at(int index);
        UidListNode* Find(int uid, int* index);
        bool Add(int uid, std::int32_t loginTime);
        bool Remove(UidListNode** head, int uid);

        int uid;
        std::int32_t logintime;
        UidListNode* next;
    };

    using IPMap = std::map<std::int64_t, UidListNode*>;

    ConnectedIPList();
    explicit ConnectedIPList(int connectionLimit);
    ~ConnectedIPList();

    void SetConnectionLimit(int limit);
    bool CheckIPOverConnection(
        int serverId,
        in_addr address,
        int uid);
    void AddConnectedIp(
        int serverId,
        in_addr address,
        int uid,
        std::int32_t loginTime);
    void RemoveConnectedIp(
        int serverId,
        in_addr address,
        int uid);
    void PrintMapDebug();

    static ConnectedIPList* Inst();

private:
    int connectionLimit;
    IPMap mIPMap;
    CLock mLock;

    static ConnectedIPList* sInstance;
};
