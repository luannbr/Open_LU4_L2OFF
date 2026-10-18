#pragma once

#include "Platform/WindowsTypes.h"
#include "Lock.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

class CIOBuffer;
class CSocketServer;

// Original owner: Src/serverlist.h. L2Authd.pdb type 0x5D68, target size
// 0x74. std::string intentionally follows the portable host ABI.
struct WorldServer {
    char serverid;
    char ip[16];
    char inner_ip[16];
    char name[26];
    in_addr outer_addr;
    in_addr inner_addr;
    char ageLimit;
    unsigned char pkflag;
    std::int32_t UserNum;
    std::uint32_t s;
    std::int32_t serverKind;
    std::int32_t port;
    char region;
    std::string domainName;
};

// Original owner: Src/serverlist.h. L2Authd.pdb type 0x6E6E, target size
// 0x44. This declaration keeps the actual lock/vector/map ownership needed
// by CServerList instead of replacing the field with opaque padding.
class CServerListPacket6 {
public:
    using PacketPageList = std::vector<CIOBuffer*>;
    using ServerIdType = char;
    using ServerStatusType = char;
    using UserNumType = short;

    struct PageOffsetType {
        PageOffsetType()
            : m_pageNo(0), m_offset(0) {}

        PageOffsetType(int pageNo, int offset)
            : m_pageNo(pageNo), m_offset(offset) {}

        std::int32_t m_pageNo;
        std::int32_t m_offset;
    };

    using DynValueMap = std::map<char, PageOffsetType>;

    struct PacketPageDynOffsets {
        DynValueMap m_currentUser;
        DynValueMap m_maxLimitUser;
        DynValueMap m_serverStatus;
    };

    CServerListPacket6();
    ~CServerListPacket6();

    bool MakePacketFrame(
        const std::vector<WorldServer>& servers);
    void SetServerStatus(
        const char serverId,
        const char status);
    void SetUserNum(
        const char serverId,
        const short currentUser,
        const short maximumUser);
    bool GetPacketFrame(
        const int pageNumber,
        char*& packet,
        int& packetLength,
        bool& isMorePages);

private:
    void FreePacketPageList(
        PacketPageList& pages,
        PacketPageDynOffsets& offsets);

    CRWLock m_lock;
    PacketPageList m_pages;
    PacketPageDynOffsets m_infoOffsets;
};

// Original owner: Src/serverlist.h. L2Authd.pdb type 0x4215, target size
// 0x28. Its method bodies are compiled by Src/csocketserver.cpp.
class CServerKickList {
public:
    CServerKickList();
    ~CServerKickList();

    bool AddKickUser(int uid, const char* account);
    bool PopKickUser(CSocketServer* socket);

protected:
    std::map<int, std::string> KickList; // target +0x00, VS2013 size 0x08
    CLock mLock;                        // target +0x08
};

// Original owner: Src/serverlist.h. L2Authd.pdb type 0x6C9C, target size
// 0x31A8. The complete data-member graph and method surface are recorded
// here. Emitted bodies remain in their original Src/serverlist.cpp owner.
class CServerList {
public:
    CServerList();
    virtual ~CServerList();

    void SetServerStatus(char serverId, char status);
    // Portable: normal (non-revival) servers with their last status, for the
    // LU4 client list (ClientSession::ServerListPacket).
    std::vector<std::pair<WorldServer, char>> Lu4Servers();
    WorldServer GetAt(int serverId);
    bool ExistId(int serverId);
    int GetAgeLimit(int serverId);
    int GetAgeLimit() {
        return ageLimit;
    }
    void MapForbiddenServers(
        unsigned char* availableServers,
        int availableServerCount,
        char* forbiddenServers,
        unsigned int capacity);
    void MapForbiddenServers(
        int age,
        char* forbiddenServers,
        unsigned int capacity);
    bool IsRevivalServer(int serverId);
    void ReloadServer();

private:
    void MakeServerListFrame1();
    void MakeServerListFrame2();
    void MakeServerListFrame3();
    void MakeServerListFrame4();
    void MakeServerListFrame5();
    void MakeServerListFrame6();
    void MakeServerListFrame7();

    std::int32_t ageLimit;
    CRWLock mylock;
    std::vector<WorldServer> serverlist;
    std::map<int, int> servermap;
    std::map<int, int> totalservermap;
    std::map<int, char> m_lu4Status;

public:
    WorldServer worldserver;
    char* ServerListPacketFrame[10];
    std::int32_t ServerListLen[10];
    char* RevivalServerListPacketFrame[10];
    std::int32_t RevivalServerListLen[10];
    char listpacket[6137];
    std::int32_t packetlen;
    char revivallistpacket[6137];
    std::int32_t revivalpacketlen;
    CServerListPacket6 m_listPacket6;
    int SetServerSocket(in_addr address, unsigned int socket);
    int SetServerUserNum(
        in_addr address,
        int userCount,
        char serverId);
    std::int32_t servernum;
    int GetMaxServerNum() {
        return servernum;
    }
    std::int32_t normalservernum;
    int GetMaxNormalServerNum() {
        return normalservernum;
    }
    std::int32_t revivalservernum;
    int GetMaxRevivalServerNum() {
        return revivalservernum;
    }
    void CheckAllServerStatus();
    bool GetServerStatus(char serverId);
    bool CheckBeginnerWorld(int serverId);
};

extern CServerList g_ServerList;

struct world_user_num {
    char serverid;
    std::int32_t limituser;
    std::int32_t playuser;
};

// Original owner: Src/serverlist.h. PDB type 0x7B23, target size 0x620.
class CServerUserCountStatus {
public:
    CServerUserCountStatus();
    ~CServerUserCountStatus();

    void UpdateUserNum(
        unsigned char serverId,
        short playUser,
        short limitUser);
    void UpdateAllServerNumToDB();
    void SetCounter();

private:
    world_user_num wun[128];
    authd::platform::CriticalSectionStorage32 count_section;
    std::int32_t CountID;
    void* m_timer;
};

extern CServerUserCountStatus g_UserCount;

// Exact free-function surface compiled by Src/serverlist.cpp and used by the
// socket owners.
int SetServerSocket(in_addr address, std::uint32_t socket);
void SetServerStatus(char serverId, char status);
bool GetServerStatus(char serverId);
WorldServer GetWorldServer(char serverId);
void SetServerUserNum(
    in_addr address,
    int userCount,
    char serverId);
