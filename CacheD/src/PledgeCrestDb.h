#pragma once

#include "Shared/src/IOCs.h"
#include "Shared/src/PledgeCrest.h"
#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstdint>
#include <utility>

// Shipping owner: CacheD/PledgeCrestDb.{h,cpp}. The interface is stateless. The
// kept world-qualified cache belongs to the Shared CPledgeCrest contract.
class CPledgeCrestDb {
public:
    CPledgeCrestDb() = default;
    ~CPledgeCrestDb() = default;

    std::uint32_t CreatePledgeCrest(int bitmapSize, const std::uint8_t* bitmap);
    CPledgeCrestSP LoadPledgeCrest(std::uint32_t crestId);
    CPledgeCrestSP GetPledgeCrest(std::uint32_t crestId);
    bool DeleteCrest(std::uint32_t crestId);
};


extern CPledgeCrestDb g_pledgeCrestDb;

// Current Cached.exe keeps the five-part FileServer emblem upload beside the
// legacy bitmap-crest owner in CacheD/PledgeCrestDb.{h,cpp}. The unnamed
// constructor at 0x00510ac4 fixes CIOCriticalSection at +0x00 and the outer
// xmap at +0x28. The complete object is 0x48 bytes.
class CPledgeEmblemDb : public CIOCriticalSection {
public:
    void SendPledgeEmblem(int userSocketId, int pledgeEmblemId, const char16_t* downloadUrl, int worldId, int pledgeSocketId);
    bool CreatePledgeEmblem(int userSocketId, int pledgeId, int pledgeSocketId);
    void LoadPledgeEmblem(int userSocketId, int pledgeEmblemId, int worldId, int pledgeSocketId);
    int AddPledgeEmblemPiece(
        int userSocketId, int pledgeId, int pledgeSocketId, int pieceIndex, int pieceSize, const unsigned char* pieceData
    );

private:
    struct PledgeEmblemPiece {
        int size = 0;
        std::array<unsigned char, 0x3800> data{};
    };

    using PledgeEmblemKey = std::pair<int, int>;
    using PledgeEmblemPieces = std::map<int, PledgeEmblemPiece>;

    std::map<PledgeEmblemKey, PledgeEmblemPieces> m_pledgeEmblems;
};


// The PDB does not keep the original spelling of DAT_02a1a3d0. Keep one
// semantic process global instead of exposing an address-derived alias.
extern CPledgeEmblemDb g_pledgeEmblemDb;
