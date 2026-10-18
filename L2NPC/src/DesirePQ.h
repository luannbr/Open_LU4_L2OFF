#pragma once

#include "Desire.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <string>

// Current L2NPC.exe keeps the largest desire-queue size observed by each
// worker in TLS +0x14. The matching PDB keeps this original name.
extern thread_local int g_tlMaxDesireSize;


class CNPC;
class GlobalObject;
class CYieldLock;


// Current L2NPC.exe CDesirePQ. The adjacent PDB describes the preceding
// revision, whose xpriority_queue base was eight bytes larger and whose
// CDesire was eight bytes smaller. Current machine code fixes the resulting
// field offsets below while retaining the same total 0x110-byte size.
class CDesirePQ : public runtime::PriorityQueue<CDesire, std::vector<CDesire, std::allocator<CDesire>>, std::less<CDesire>> {
public:
    using Parent = runtime::PriorityQueue<CDesire, std::vector<CDesire, std::allocator<CDesire>>, std::less<CDesire>>;
    using DesireVec = std::vector<CDesire, std::allocator<CDesire>>;
    using _It = DesireVec::iterator;

    explicit CDesirePQ(CYieldLock& desireLock);
    ~CDesirePQ();

    void SetNPC(CNPC* npc) { m_pNPC = npc; }

    int AddValue(const CDesire& desire);
    int AddValue(const CDesire& desire, int& topChanged);
    int SetValue(const CDesire& desire);
    int BoostUp(const CDesire& desire);
    int RemoveKey(const CDesire& desire, bool removeHate = false);
    int RemoveAllDesire(int type);
    bool IsDQEmpty();
    CDesire& GetTopDesire();
    int RandomizeDesire(PhysicalState type);
    int SendNoDesireEvent();
    int Decay();
    int RemoveAll();
    int ClearDesire();
    int SetActive(int active);
    bool ResetAbsoluteDesire();
    bool RemoveTopDesire(int type);

    int _AddValue(const CDesire& desire, int& topChanged);
    int _BoostDown(const CDesire& desire);
    int _BoostUp(const CDesire& desire);
    int _GetDesire(const CDesire& key, CDesire*& found);
    float _GetMaxValue(PhysicalState type);
    CDesire& _GetTopDesire();
    bool _IsDQEmpty() const;
    int _RemoveKey(const CDesire& desire, bool removeHate);

    int GetQueueSize() const;

    void Report(std::u16string& output, PhysicalState physicalState);

protected:
    CNPC* m_pNPC = nullptr; // +0x28
    int m_bActive = 0; // +0x30
    CYieldLock* m_DesireLock = nullptr; // +0x38

public:
    bool m_bSetAbsoluteDesire = false; // +0x40
    CDesire m_AbsoluteDesire; // +0x48

private:
    // CNPC::AsyncTimerExpired1 directly walks and re-heaps the target
    // container while holding CNPC::m_DesirePQSect.
    friend class CNPC;

    bool empty() const { return Parent::empty(); }

    CDesire& top() { return const_cast<CDesire&>(Parent::top()); }
};
