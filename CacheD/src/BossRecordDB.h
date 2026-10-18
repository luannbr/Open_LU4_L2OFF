#pragma once

class CSocket;

// Shipping owner recovered from Cached.exe 0x004517c8..0x00451c59.
// The object is exactly the three persisted round values used by the current
// ServerPacketEx handlers. It has no lock or portable sidecar state.
class BossRecordDB {
public:
    int GetRound();
    int GetStartTime();
    int GetEndTime();
    void MoveToNextRound(CSocket* socket);

private:
    int m_round = 0;
    int m_startTime = 0;
    int m_endTime = 0;
};


// Current unnamed lazy-static accessor at 0x0045179c. Its source identifier
// is absent from the surviving guard/PDB material, so this semantic name is
// documented instead of being presented as an original name.
BossRecordDB& GetBossRecordDB();
