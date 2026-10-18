#pragma once

#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>

class User;
struct SharedNoblessInfo;

class SharedNoblessInfoMap {
public:
    SharedNoblessInfoMap();
    ~SharedNoblessInfoMap() = default;

    int GetNoblessIndex(int characterDatabaseId) const;
    const SharedNoblessInfo* GetNoblessInfo(int characterDatabaseId) const;
    void SetNoblessInfo(
        int characterDatabaseId,
        int noblessType,
        int heroType,
        int winCount,
        int previousPoint,
        int olympiadPoint,
        int matchCount,
        int olympiadWinCount,
        int olympiadLoseCount,
        int olympiadTradePoint,
        bool newSeason
    );

private:
    mutable CYieldLock m_Lock; // +0x00..+0x0f
    std::map<int, int> m_NoblessIndexMap; // +0x10..+0x2f
};


class NoblessSystem : public Singleton<NoblessSystem> {
public:
    NoblessSystem();
    ~NoblessSystem() override;

    static void Init();
    void LinkNobless(CSPointer<User> user);
    const SharedNoblessInfo* GetNoblessInfo(int characterDatabaseId) const;
    void SetNoblessInfoFromDB(int characterDatabaseId, int noblessType);
    void SetNoblessInfoFromOlympiad(
        int characterDatabaseId,
        int heroType,
        int winCount,
        int previousPoint,
        int olympiadPoint,
        int matchCount,
        int olympiadWinCount,
        int olympiadLoseCount,
        int olympiadTradePoint,
        bool newSeason
    );

private:
    SharedNoblessInfoMap m_NoblessInfoMap; // +0x08..+0x37
};


namespace NoblessSystemUtil {

void SetNoblessDataToUser(CSPointer<User> user, int noblessIndex);
bool SetNoblessSkillToUser(CSPointer<User> user);
void SendAddOlympiadPointToUser(int userDatabaseId, int point);
void BroadcastAddOlympiadPoint(int point);

} // namespace NoblessSystemUtil
