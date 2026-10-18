#pragma once

#include "User.h"

#include "L2Core/src/inc/XStd.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/platform/Runtime.h"

#include <atomic>
#include <cstdint>

class CSocket;
class CNpcDbSocket;
class L2Exception;
bool ReloadCharacter(CSocket* socket, const unsigned char* packet);
bool GiveEventData(CNpcDbSocket* socket, const std::uint8_t* packet);

using CUserMap = CObjectMap<CUser, 16384, 5, RWLock, std::CDualKey<int, int>>;
using CLoggedInUserMap = CObjectMap<CUser, 1, 5, RWLock, int>;
using CAccountUserMap = CObjectMap<CUser, 16384, 5, RWLock, int>;

// Shipping owner: CacheD/UserDb.{h,cpp}. The three kept indexes and the
// world-partitioned name index are the current 2018 owners, not C1 aliases.
class CUserDb {
public:
    // Current Cached.exe process counters/time at 0x0083d5c8..0x0083d5cf.
    // The names and CUserDb ownership survive in the C1 source donor. The
    // current target confirms every read, write and status-row position.
    static std::atomic<std::int32_t> s_usersOnline;
    static std::atomic<std::int32_t> s_maxUsersOnline;
    static runtime::SystemTime s_maxOnlineTime;

    CUserDb();
    ~CUserDb();

    CUserSP GetUser(int characterId, bool loadIfMissing, bool secondErrorVariant, int worldId);
    CUserSP GetUser(const char16_t* characterName, bool suppressLoadError);
    // Semantic name: the original identifier is unavailable. Current
    // Cached.exe 0x005127ec is the guardless resident-only adapter used by
    // packet handlers: it combines TLS database ID with character ID and
    // calls the primary CObjectMap::FindObject specialization.
    CUserSP FindResidentUser(int characterId);
    CUserSP GetUserForRaid(int characterId, bool secondErrorVariant);
    // Current Cached.exe calls this owner directly from CreateSubjobPacket,
    // ReloadCharInfo, two AQ handlers, ReloadCharacter, RefreshChar and
    // GetUserForRaid. It is therefore part of CUserDb's production surface,
    // not a private helper of the portable reconstruction.
    CUserSP ReLoadFromDb(CUser* user, CUser::INCLUDE_WAREHOUSE includeWarehouse);
    bool RefreshChar(CUser* user);
    bool SimpleLoad(
        int characterId,
        char16_t* characterName,
        int characterNameBufferBytes,
        char16_t* nickname,
        int nicknameBufferBytes,
        int* level,
        int* characterClass,
        int* gender,
        int* race,
        int* gradeId,
        int* pledgeBonus
    );
    CUserSP GetUserByAccountId(int accountId);
    bool SetUserNickname(unsigned int characterId, wchar_t* nickname);
    bool SetPowerGrade(int characterId, int gradeId);
    bool DeleteChar(CUser* user, bool deleteFromDatabase);

    int CreateUser(
        wchar_t* characterName,
        wchar_t* accountName,
        unsigned int accountId,
        unsigned int pledgeId,
        int builder,
        int gender,
        int race,
        int characterClass,
        int worldId,
        int x,
        int y,
        int z,
        double hp,
        double mp,
        std::int64_t sp,
        std::int64_t experience,
        int level,
        int alignment,
        int pkCount,
        unsigned int duelCount,
        unsigned int pkPardon,
        int faceIndex,
        int hairShapeIndex,
        int hairColorIndex,
        wchar_t* address
    );
    int CreateUser(
        wchar_t* characterName,
        wchar_t* accountName,
        unsigned int accountId,
        unsigned int pledgeId,
        int builder,
        int gender,
        int race,
        int characterClass,
        int worldId,
        int x,
        int y,
        int z,
        double hp,
        double mp,
        std::int64_t sp,
        std::int64_t experience,
        int level,
        int alignment,
        int pkCount,
        unsigned int duelCount,
        unsigned int pkPardon,
        int faceIndex,
        int hairShapeIndex,
        int hairColorIndex,
        int subjobId,
        int mainClass,
        int dualClassId,
        double cp
    );
    bool CopyChar(int sourceCharacterId, int builder, wchar_t* newCharacterName, wchar_t* accountName, int accountId);

    // Current startup preload at 0x005c5f94. It creates every seller
    // referenced by an active commission through the normal CUserDb path.
    void LoadCommissionUser();
    void PrintAllAccountData();
    void PrintAllAccountWarehouseData();

    bool LogoutAllChars();
    void ItemCheckOnAllCharLoaded();
    void SetReloadMatchingPointAll();
    void SetEventDataPeriod(int eventDataPeriod);

private:
    friend class CUserDbHelper;
    friend class CUser;
    // Current Cached.exe L2Exception::PrintObjectReport at 0x004cb978 calls
    // the leading CUser map's GetSize specialization directly. Friendship
    // keeps that exact production access without inventing a CUserDb API.
    friend class L2Exception;
    friend bool ::ReloadCharacter(CSocket* socket, const unsigned char* packet);
    // Current Cached.exe 0x004fa840 reads CUserDb +0x2c00f0 directly from
    // the global NPC protocol callback. No accessor exists in the target.
    friend bool ::GiveEventData(CNpcDbSocket* socket, const std::uint8_t* packet);

    void Add(CUser* user);
    void SetLoggedInIndex(CUser* user, bool loggedIn);
    CUserSP LoadFromDb(std::uint32_t characterId, int worldId);
    CUserSP LoadFromDb(const char16_t* characterName);
    bool RefreshMaps(const char16_t* oldCharacterName, const char16_t* newCharacterName);

    CUserMap m_usersByWorldAndId;
    CLoggedInUserMap m_loggedInUsers;
    CObjectByIntKeyStringMap<CUser> m_usersByNameByWorld;
    CAccountUserMap m_usersByAccountId;
    int m_eventDataPeriod = 0x15180;
    int m_eventDataPeriodState = 0;
};

extern CUserDb g_userDb;
