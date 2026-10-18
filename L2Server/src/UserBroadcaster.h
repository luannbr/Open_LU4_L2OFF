#pragma once

#include "Shared/src/TemplateHelper.h"

#include <cstdarg>
#include <cstdint>

enum S_EX_PROTOCOL : std::int32_t;

namespace network {
struct IPacket;
}

class CUserBroadcaster : public Singleton<CUserBroadcaster> {
public:
    CUserBroadcaster();
    ~CUserBroadcaster() override;

    int GetIndex() const noexcept { return m_nIndex; }

    void Reset() noexcept { m_nIndex = 0; }

    void BroadcastToAllUser(const char* format, ...);
    void BroadcastToAllUser(S_EX_PROTOCOL protocol, const network::IPacket& packet);
    void BroadcastToAllUserEx(const char* format, ...);
    void BroadcastToAllUserAnnounce(const char16_t* message);
    void BroadcastToAllUserScreenAnnounce(const wchar_t* message);
    void BroadcastToAllUserScreenAnnounceEX(const wchar_t* message);
    void BroadcastToCanSunsetUser(const char* format, ...);

private:
    std::int32_t m_nIndex;
};
