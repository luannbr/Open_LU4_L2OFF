#pragma once

#include "Shared/src/TemplateHelper.h"

#include <cstddef>
#include <cstdint>

#if defined(L2_BUILD_L2SERVER)
class CDBSocket;
using QueryPacketSocket = CDBSocket;
#else
class CSocket;
using QueryPacketSocket = CSocket;
#endif

using QueryPacketCallback = bool (*)(QueryPacketSocket*, const std::uint8_t*);
using QueryPacketCallbackMap = FixedSizeCallbackMap<4, QueryPacketCallback>;

// Original Shared/QueryPacket.cpp global. Current Cached.exe fixes the same
// four callbacks, layout and default owner of the 2015 PDB.
extern QueryPacketCallbackMap g_QueryPacketTable;

bool DummyQueryPacket(QueryPacketSocket* socket, const std::uint8_t* packet);

template <class Packet, int Protocol>
class TQuery {
public:
    static bool DoJob(QueryPacketSocket* socket, const std::uint8_t* packet);
#if defined(L2_BUILD_L2SERVER)
    static bool Do(int worldId, Packet* packet);
#endif
};


#pragma pack(push, 1)

// PDB-exact empty base from Shared/QueryPacket.h. Its private allocation
// operators force query carriers to remain caller-owned synchronous objects.
// empty-base optimization keeps every derived field at offset zero.
class QueryBase {
private:
    static void* operator new(std::size_t size);
    static void operator delete(void* allocation);
    bool DoCacheDJob();
};

class GetHeroRank : public QueryBase {
public:
    GetHeroRank();
    bool DoCacheDJob();
    void DoServerJob();

    int mHeroCount; // +0x000
    int mHeroClassList[36]; // +0x004
    int mHeroIdList[36]; // +0x094
};

class GetHeroById : public QueryBase {
public:
    explicit GetHeroById(int charId);
    bool DoCacheDJob();
    void DoServerJob();

    int mCharId; // +0x000
    char16_t mCharName[25]; // +0x004
    int mMainClass; // +0x036
    int mHeroType; // +0x03a
    int mPledgeId; // +0x03e
    char16_t mPledgeName[25]; // +0x042
    int mPledgeCrestId; // +0x074
    int mAllianceId; // +0x078
    char16_t mAllianceName[25]; // +0x07c
    int mAllianceCrestId; // +0x0ae
    int mWinCount; // +0x0b2
    char16_t mWords[129]; // +0x0b6
    int mHistoryOpen; // +0x1b8
    bool mReturn; // +0x1bc
};

class CQUERY_WOLYMPIAD_LOAD_ALL_HERO : public QueryBase {
public:
    CQUERY_WOLYMPIAD_LOAD_ALL_HERO();
    bool DoCacheDJob();

    int mHeroCount; // +0x000
    int mHeroWorldIDList[36]; // +0x004
    int mHeroIdList[36]; // +0x094
    int mHeroClassList[36]; // +0x124
};

class CQUERY_WOLYMPIAD_GET_HERO_BY_ID : public QueryBase {
public:
    CQUERY_WOLYMPIAD_GET_HERO_BY_ID(int worldId, int charId);
    bool DoCacheDJob();

    int m_nWorldID; // +0x000
    int mCharId; // +0x004
    char16_t mCharName[25]; // +0x008
    int mMainClass; // +0x03a
    int mHeroType; // +0x03e
    int mPledgeId; // +0x042
    char16_t mPledgeName[25]; // +0x046
    int mPledgeCrestId; // +0x078
    int mAllianceId; // +0x07c
    char16_t mAllianceName[25]; // +0x080
    int mAllianceCrestId; // +0x0b2
    int mWinCount; // +0x0b6
    char16_t mWords[129]; // +0x0ba
    int mHistoryOpen; // +0x1bc
    bool mReturn; // +0x1c0
};

#pragma pack(pop)
