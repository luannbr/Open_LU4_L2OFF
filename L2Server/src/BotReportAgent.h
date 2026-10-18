#pragma once

#include "Shared/src/IOC.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

class User;

struct PointInfo final {
    PointInfo() = default;
    PointInfo(std::uint32_t accountId, std::int32_t point, std::int32_t reportedTime, bool ignoreDelay = false) noexcept;

    std::uint32_t m_nAccountID = 0; // +0x00
    std::int32_t m_nPoint = 0; // +0x04
    std::int32_t m_tReportedTime = 0; // +0x08
    bool m_bIgnoreDelay = false; // +0x0c
    std::array<std::byte, 0x03> m_alignment000dTo0010{}; // +0x0d..+0x0f
};


struct RestrainInfo final {
    using RestrainEntry = std::pair<std::uint32_t, std::int32_t>;
    using RestrainList = std::vector<RestrainEntry>;

    std::array<RestrainList, 3> m_lstRestrain; // +0x00..+0x5f
    std::uint32_t m_nBotCharDBID = 0; // +0x60
    std::array<std::byte, 0x04> m_alignment0064To0068{}; // +0x64..+0x67
};


class CBotReportAgent final : public CIOObject {
public:
    enum BotCheckReturnType : std::int32_t {
        BCRT_OK = 0,
        BCRT_FAILED_BY_PLEDGE = 1,
        BCRT_FAILED_BY_ACCOUNT = 2,
        BCRT_FAILED_BY_IP = 3,
        BCRT_FAILED_BY_NOT_ENOUGH_POINT = 4,
        BCRT_FAILED_BY_REUSE_DELAY = 5,
        BCRT_FAILED_BY_ETC = 6,
    };

    CBotReportAgent();
    ~CBotReportAgent() override;

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    static CBotReportAgent* Inst();

    void Init();
    int CheckRestrainAndUpdate(
        std::uint32_t accountId, std::uint32_t address, std::int32_t pledgeId, std::int32_t botCharacterDatabaseId
    );
    void Report(
        User* reporter,
        User* target,
        char16_t* reporterAddress,
        char16_t* reporterPledge,
        char16_t* targetAddress,
        char16_t* targetPledge
    );
    void ResetAll();
    void SetAvailablePointAndReuseDelay(std::uint32_t accountId, int amount);
    int GetAvailablePoint(std::uint32_t accountId);
    int GetRemainDelay(std::uint32_t accountId);
    void SetStopBotReport(std::uint32_t accountId, int seconds);
    void TimerExpired(int id) override;

private:
    L2_DECLARE_OBJECT_REGISTRATION(CBotReportAgent)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CBotReportAgent)

    enum BotReportRestrainType : std::int32_t {
        BRRT_PLEDGE = 0,
        BRRT_ACCOUNT = 1,
        BRRT_IP = 2,
        BRRT_MAX = 3,
    };

    CYieldLock m_RestrainLock; // +0x18
    CYieldLock m_PointLock; // +0x28
    std::vector<RestrainInfo> m_lstRestrainInfo; // +0x38
    std::vector<PointInfo> m_lstPointInfo; // +0x58
    std::int32_t m_nDate; // +0x78
    std::int32_t m_nSentReduceDate; // +0x7c
};
