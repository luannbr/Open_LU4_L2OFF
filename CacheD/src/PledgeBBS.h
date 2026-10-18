#pragma once

#include <cstdint>

class CSocket;

// Current Cached.exe 0x0050b4f8..0x0050bf1a. The target object is
// fieldless. All request state is carried through these methods.
class CPledgeBBS {
public:
    CPledgeBBS();
    ~CPledgeBBS();

    bool GetClanList(int characterId, int userServerId, int option, const std::uint8_t* packet, CSocket* socket);
    bool GetClanIntro(int characterId, int userServerId, int option, const std::uint8_t* packet, CSocket* socket);
    bool GetClanInfo(int characterId, int userServerId, int option, const std::uint8_t* packet, CSocket* socket);
    bool GetClanAdmin(int characterId, int userServerId, int option, const std::uint8_t* packet, CSocket* socket);
    bool DoPledgeBBS(int characterId, int userServerId, int option, const std::uint8_t* packet, CSocket* socket);
};
