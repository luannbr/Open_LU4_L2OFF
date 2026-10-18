#pragma once

#include "Protocol/src/L2Constants.h"
#include "Shared/src/IOC.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>
#include <utility>

class User;


class CNotice {
public:
    virtual ~CNotice();
    virtual bool CheckAwakening(User* user) = 0;

    int GetType() const noexcept { return m_nType; }

protected:
    explicit CNotice(int type) noexcept : m_nReserved(0), m_nType(type) {}

private:
    [[maybe_unused]] std::int32_t m_nReserved; // +0x08
    [[maybe_unused]] std::int32_t m_alignment0cTo10{};
    std::int32_t m_nType; // +0x10
    [[maybe_unused]] std::int32_t m_alignment14To18{};
};


class CNotice_LevelCond final : public CNotice {
public:
    using Reward = std::pair<int, std::int64_t>;
    using RewardMap = std::map<int, Reward>;

    CNotice_LevelCond();
    ~CNotice_LevelCond() override;

    bool CheckAwakening(User* user) override;
    bool Exists(int level) const;
    void AddLevelCond(int level, Reward reward);

private:
    RewardMap m_mapReward; // +0x18
};


class CPathToAwakeningNotice final : public CIOObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CPathToAwakeningNotice)

    CPathToAwakeningNotice();
    ~CPathToAwakeningNotice() override;

    static CPathToAwakeningNotice* Inst();
    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    CNotice* GetNotice(PathToAwakening::AwakeningType type);
    void AddAwakeningNotice(PathToAwakening::AwakeningType type, CNotice* notice);
    void LoadAwakeningNotice();

private:
    using NoticeMap = std::map<PathToAwakening::AwakeningType, CNotice*>;

    CYieldLock m_lock; // +0x18
    NoticeMap m_mapNotice; // +0x28

    inline static const wchar_t* m_pClassName = L"CPathToAwakeningNotice";
};
