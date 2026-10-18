#pragma once

#include "GameGuard.h"

#include <cstdint>
#include <memory>

namespace authgated::platform {

class GameGuardProvider {
public:
    virtual ~GameGuardProvider() = default;

    virtual std::uint32_t GetVersion(bool useUpdateTimer) = 0;
    virtual std::uint32_t GetAuthQuery(
        std::uint32_t option,
        _GG_VERSION* version,
        std::uint32_t* userFlag,
        _GG_AUTH_DATA* query,
        _GG_AUTH_DATA* answer) = 0;
    virtual std::uint32_t CheckAuthAnswer(
        std::uint32_t option,
        _GG_VERSION* version,
        std::uint32_t* userFlag,
        _GG_AUTH_DATA* query,
        _GG_AUTH_DATA* answer) = 0;
    virtual std::uint32_t EncryptQuery(_GG_AUTH_DATA* query) = 0;
    virtual std::uint32_t DecryptAnswer(_GG_AUTH_DATA* answer) = 0;
    virtual std::uint32_t UpdateTimer() = 0;
};

// Error values are the original ggsrvlib25 LoadAuthProtocol contract:
// 2 = module/load failure, 3 = one or more required exports are absent.
std::unique_ptr<GameGuardProvider> LoadGameGuardProvider(
    const char* path,
    std::uint32_t& error);

} // namespace authgated::platform
