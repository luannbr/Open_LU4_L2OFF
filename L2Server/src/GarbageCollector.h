#pragma once

#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>

class User;

// Original two-word carrier used by the per-world-thread user garbage
// queues. The fourth dword is target padding copied by the MSVC deque.
template <typename TObject>
struct GarbageData {
    TObject* m_pObj{};
    std::uint32_t m_nTime{};
    std::uint32_t m_alignment0cTo10{};
};

template <typename TObject>
class GarbageCollectorBase {
public:
    virtual void Initialize() = 0;
    virtual void Insert(TObject* object) = 0;
    virtual void Collect() = 0;
};


// Current L2Server owns one queue pair for each of its sixteen world/IO
// lanes. RTTI and the constructor at 0x004500b0 fix the vptr, both array
// offsets and the complete 0x608-byte extent.
class UserObjGC final : public GarbageCollectorBase<User> {
public:
    UserObjGC() = default;
    ~UserObjGC() = default;

    void Initialize() override;
    void Insert(User* user) override;
    void Collect() override;


private:
    static constexpr std::size_t kWorldThreadCount = 16;

    bool CheckSizeCorruption(GarbageData<User>& data);
    bool ReleaseResources(GarbageData<User>& data);
    void KeepGarbageInfo(GarbageData<User>& data);
    void ClearGarbageInfo();
    bool ReleaseUser(GarbageData<User>& data);

    std::array<std::deque<GarbageData<User>>, kWorldThreadCount> m_gcMustQueue;
    std::array<std::deque<GarbageData<User>>, kWorldThreadCount> m_gcDelayQueue;
};


extern UserObjGC g_UserGC;
