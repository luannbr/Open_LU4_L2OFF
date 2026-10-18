#pragma once

#include <cstddef>

class User;

class CKillPVPPointChecker {
public:
    CKillPVPPointChecker();
    ~CKillPVPPointChecker();

    bool AddGiveCount(User* user);
    bool CheckGivableAndSendChange(User* user);
    void SaveGiveCount(User* user);
    void LoadGiveCount(User* user);
    void SetLastGiveTime();
    void ResetAll(User* user);
    void SetGiveCount(int giveCount, int lastGiveTime, User* user);
    bool CanGet();

protected:
    friend class User;

    bool m_bStatus; // +0x00
    int m_nGiveCount; // +0x04
    int m_tLastGiveTime; // +0x08
};
