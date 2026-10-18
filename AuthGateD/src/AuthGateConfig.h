#pragma once

#include "Config.h"

#include <array>
#include <cstdint>

class CAuthGateConfig final : public CConfig {
public:
    CAuthGateConfig() = default;
    ~CAuthGateConfig() override = default;

    void InitConfig() override;
    bool ValidateConfig() const;
    void ReportConfig() const;

    std::int32_t serverPort = 0;
    in_addr authAddr{};
    std::int32_t authPort = 0;
    std::int32_t authReconnectInterval = 0;
    std::int32_t numThread = 0;
    std::int32_t numIOThread = 8;
    std::int32_t acceptCallNum = 0;
    std::int32_t socketLimit = 0;
    bool useForbiddenIPList = false;
    char16_t* logDirectory = nullptr;
    bool dumpPacket = false;
    std::int32_t sessionTimeout = 0;
    std::int32_t tryInterval = 0;
    std::int32_t tryCount = 0;
    std::int32_t tryBlockInterval = 0;
    bool useGameGuard = false;
    bool checkGameGuard = false;
    bool useNotifyCSResult = false;
    std::int32_t ggNumActive = 0;
    bool ggUseUpdateTimer = false;
    std::int32_t ggUseLog = 0;
    bool ggLogInvalid = false;
    std::int32_t maxPenaltySeconds = 120;
    bool useLogd = false;
    in_addr logdAddr{};
    std::int32_t logdPort = 0;
    std::int32_t loginType = 0;
    std::array<char, 3> companyCode{};
    std::int32_t appLaunchBanDelay = 5;
    bool useGCSideExtendAccount = false;
    // Portable: port of the LU4 client login (Lu4Login.h); 0 disables it.
    std::int32_t lu4Port = 0;
    bool useReportMail = false;
    char16_t* reportMailServer = nullptr;
    std::int32_t reportMailPort = 25;
    char16_t* reportMailFrom = nullptr;
    char16_t* reportMailTo = nullptr;
    char16_t* tmpCompanyCode = nullptr;
};

extern CAuthGateConfig config;
