#pragma once

class CSocket;

// Shipping owner: CacheD/ReservedAnnounce.{h,cpp}. The current class carries
// no persistent object state. Each operation owns its DBConn and the request
// handlers construct it as a short-lived local object.
class CReservedAnnounce {
public:
    // Current Cached.exe 0x005282d4 is the complete four-byte identity
    // constructor for this empty owner.
    CReservedAnnounce() = default;
    ~CReservedAnnounce() = default;

    bool LoadReservedAnnounce(CSocket* socket, const unsigned char* packet);
    bool AddReservedAnnounce(
        int announceId, short announceType, int year, int month, int day, int hour, int minute, int repeat
    );
    bool DeleteReservedAnnounce(int announceId);
    bool AddReservedAnnounceMsg(int announceId, short messageOrder, wchar_t* message);
    bool DeleteReservedAnnounceMsg(int announceId, short messageOrder);
};
