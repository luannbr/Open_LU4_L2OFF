#pragma once

#include "Castle.h"

#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/RWLock.h"

#include <cstddef>
#include <cstdint>

class CSocket;
template <std::size_t Capacity>
class CPacketHelperEx;

// Current helper 0x0049fed8 is shared by the agit, castle and fortress load
// paths. Its original source identifier is unavailable. This semantic name
// keeps the exact buffer, residence-ID and output-count contract.
int AppendAgitDecoNpcs(char* output, int outputCapacity, std::uint32_t residenceId, int& npcCount);

using CCastleMap = CObjectMap<CCastle, 1, 5, RWLock, int>;


// Shipping owner: CacheD/CastleDb.{h,cpp}. The apparently extra outer
// RWLock is kept at +0x00. Current GetCastle and traversal use the map's
// own +0x28 lock and do not acquire the outer lock.
class CCastleDb {
public:
    CCastleDb();
    ~CCastleDb();

    CCastleSP GetCastle(int castleId);
    void ItemCheckOnAllCastle();
    void SendReplyToLoadCastle(CSocket* socket, std::uint32_t castleId, const wchar_t* castleName);
    void SendReplyToViewSiegeList(
        CSocket* socket, std::uint32_t userServerId, std::uint32_t castleId, const wchar_t* returnHtml
    );
    void SendReplyToApproveBattle(
        CSocket* socket, std::uint32_t userServerId, std::uint32_t castleId, const wchar_t* returnHtml
    );
    int GetRelatedCastleSiege(CPacketHelperEx<20480>& packet, std::uint32_t castleId, std::uint32_t pledgeId, int worldId);

private:

    void Add(CCastle* castle);
    CCastleSP LoadFromDb(int castleId);

    RWLock m_lock; // +0x00
    CCastleMap m_castles; // +0x38
};



extern CCastleDb g_castleDb;
