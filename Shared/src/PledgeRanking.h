#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <set>

class CSocket;
class RWLock;
#if defined(L2_BUILD_L2SERVER)
class CDBSocket;
#endif

// Shipping owner: Shared/PledgeRanking.{h,cpp}. All methods are static in the
// current image. The kept state consists of the three physical static
// members instead of an invented CPledgeRanking instance.
class CPledgeRanking {
public:
    static bool SendRankingList(CSocket* socket, const unsigned char* packet);
    static bool IsInRanking(std::uint32_t pledgeId);
    static int GetRanking(std::uint32_t pledgeId);
    static void Remove(std::uint32_t pledgeId);
    static void Insert(std::uint32_t pledgeId, int score);
    static void Update(std::uint32_t pledgeId, int score);
    static void Load();
#if defined(L2_BUILD_L2SERVER)
    static void RequestLoadRanking();
    static bool LoadRanking(CDBSocket* socket, const unsigned char* packet);
#endif

private:
    static RWLock s_lock;
    static std::map<std::uint32_t, int> s_nameValueMap;
    static std::multiset<int, std::greater<int>> s_rankingSet;
};
