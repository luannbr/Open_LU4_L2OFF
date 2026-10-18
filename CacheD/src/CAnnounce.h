#pragma once

#include "Shared/src/CriticalSectionStorage.h"

#include <cstddef>
#include <cstdint>

class CSocket;

// Shipping owner: CacheD/CAnnounce.{h,cpp}. Current Cached.exe keeps the
// raw Win64 CRITICAL_SECTION member used by the older C1 source, followed by
// the cached login-announcement packet at the exact current offsets.
class CAnnounce {
public:
    CAnnounce();
    ~CAnnounce();

    void ReadAnnounce(bool reload);
    bool ShowIntervalAnnounce(CSocket* socket, const std::uint8_t* packet);
    bool SetIntervalAnnounce(CSocket* socket, const std::uint8_t* packet);
    bool DelIntervalAnnounce(CSocket* socket, const std::uint8_t* packet);
    bool LoadIntervalAnnounce(CSocket* socket, const std::uint8_t* packet);

    // The "Annonces" spelling is retained from the original C1 owner.
    int GetLoginAnnoncesNumber() const;
    int GetPacketSize() const;
    const std::uint8_t* GetLoginAnnoncesPacket() const;

    void Lock();
    void Unlock();

private:

    l2portable_detail::CriticalSectionStorage m_lock; // target +0x0000
    bool m_loaded; // target +0x0028
    std::uint8_t m_loginAnnoncesPacket[0x1000]; // target +0x0029
    int m_loginAnnoncesSize; // target +0x102c
    int m_loginAnnoncesNumber; // target +0x1030
};



extern CAnnounce g_announce;
