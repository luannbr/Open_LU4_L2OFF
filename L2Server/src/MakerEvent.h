#pragma once

#include "Shared/src/IOC.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>


struct NpcPosEventRewardInfo {
    std::int32_t nEventVariable;
    std::int32_t nRewardItemEnable;
    std::int32_t nRewardItemResetDB;
    std::int64_t nItemCount;
    std::int32_t nNpcExistTimeDay;
    std::int32_t nResetInzoneBindingEnabled;
    std::int32_t nResetInzoneBindingResetDB;
    std::int32_t nResetInzoneBindingStartSec;
    std::int32_t nResetInzoneBindingEndSec;
};

static_assert(std::is_aggregate_v<NpcPosEventRewardInfo>);
static_assert(std::is_trivially_default_constructible_v<NpcPosEventRewardInfo>);
static_assert(std::is_trivially_copyable_v<NpcPosEventRewardInfo>);
static_assert(std::is_trivially_destructible_v<NpcPosEventRewardInfo>);

class NpcPosEventManager final : public CIOObject {
public:

    NpcPosEventManager();

    void StartTimer(int eventIndex, int days);
    void TimerExpired(int id) override;

private:
    int mTimerId; // +0x18
    int mLastTimerId[2]; // +0x1c
    [[maybe_unused]] std::uint32_t mPadding024{}; // +0x24
};


class NpcPosEventMap : public Singleton<NpcPosEventMap> {
public:
    class IEventInfo {
    public:
        explicit IEventInfo(int enabled = 0) noexcept;
        virtual ~IEventInfo();

        virtual void SendToNpcServer(const char16_t* eventName) = 0;

        int Enabled() const noexcept { return mEnabled; }

        void Enabled(int value) noexcept { mEnabled = value; }

    protected:
        int mEnabled; // +0x08
        std::uint32_t mPadding00c{}; // +0x0c
    };

    class DefaultEventInfo final : public IEventInfo {
    public:
        explicit DefaultEventInfo(int enabled = 0) noexcept;
        void SendToNpcServer(const char16_t* eventName) override;
    };

    class RewardEventInfo final : public IEventInfo {
    public:
        RewardEventInfo(int enabled, const NpcPosEventRewardInfo& rewardInfo) noexcept;
        void SendToNpcServer(const char16_t* eventName) override;

    private:
        NpcPosEventRewardInfo mRewardInfo; // +0x10
    };

    using EventInfoPtr = std::shared_ptr<IEventInfo>;
    using EventMap = std::map<std::u16string, EventInfoPtr, runtime::CStringLess<std::u16string>>;

    NpcPosEventMap();
    ~NpcPosEventMap() override;

    void MakerFinished();
    void OnNpcSocketClosed();
    void TriggerNpcPosEvent(const char16_t* eventName, int enabled);
    void TriggerRewardNpcPosEvent(const char16_t* eventName, int enabled, const NpcPosEventRewardInfo& rewardInfo);

private:
    EventInfoPtr setPosEvent(const char16_t* eventName, int enabled);
    EventInfoPtr _setPosEvent(const char16_t* eventName, int enabled);
    EventInfoPtr setPosEventWithRewardInfo(const char16_t* eventName, int enabled, const NpcPosEventRewardInfo& rewardInfo);
    void initApologyNpcSeries(const NpcPosEventRewardInfo& rewardInfo, int eventIndex);

    NpcPosEventManager* mNpcPosEventManager; // +0x08
    bool mMakerFinished; // +0x10
    [[maybe_unused]] std::array<std::byte, 0x03> mPadding011;
    CYieldLock mLock; // +0x14
    EventMap mPosEvents; // +0x28
};


void EnableNpcPosEvent(const char16_t* eventName);
void DisableNpcPosEvent(const char16_t* eventName);

class CDoor;

class DoorEventMap : public Singleton<DoorEventMap> {
public:
    using EventMap = std::map<std::u16string, std::int32_t, runtime::CStringLess<std::u16string>>;

    DoorEventMap();
    ~DoorEventMap() override;

    void SetEvent(CDoor* door, int eventValue);
    void OnNpcSocketClosed();
    void RegisterDoorEvent(const char16_t* doorName);
    void MakerFinished();

private:
    bool mMakerFinished; // +0x08
    [[maybe_unused]] std::array<std::byte, 0x03> mPadding009;
    CYieldLock mLock; // +0x0c
    [[maybe_unused]] std::array<std::byte, 0x04> mPadding01c;
    EventMap mDoorEvents; // +0x20
};


class DoorStatusMap : public Singleton<DoorStatusMap> {
public:
    using StatusMap = std::map<std::u16string, std::int32_t, runtime::CStringLess<std::u16string>>;

    DoorStatusMap();
    ~DoorStatusMap() override;

    void SetEvent(const char16_t* doorName, int worldId);
    void UnRegisterDoorStatus(const char16_t* doorName, int worldId);
    void RegisterDoorStatus(const char16_t* doorName, int worldId);

private:
    bool mDoorStatusFinished; // +0x08
    [[maybe_unused]] std::array<std::byte, 0x03> mPadding009;
    CYieldLock mLock; // +0x0c
    [[maybe_unused]] std::array<std::byte, 0x04> mPadding01c;
    StatusMap mDoorStatus; // +0x20
};


class AreaStatusMap : public Singleton<AreaStatusMap> {
public:
    using StatusMap = std::map<std::u16string, std::int32_t, runtime::CStringLess<std::u16string>>;

    AreaStatusMap();
    ~AreaStatusMap() override;

    void RegisterAreaStatus(const char16_t* areaName, int worldId);
    void UnRegisterAreaStatus(const char16_t* areaName, int worldId);
    void OnNpcSocketClosed();
    void SetEvent(const char16_t* areaName, int worldId);

private:
    bool mAreaStatusFinished;
    [[maybe_unused]] std::array<std::byte, 0x03> m_padding009;
    CYieldLock mLock;
    [[maybe_unused]] std::array<std::byte, 0x04> m_padding01c;
    StatusMap mAreaStatus;
};
