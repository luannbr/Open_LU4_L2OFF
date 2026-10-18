#pragma once

#include "Shared/src/MemorySub.h"

#include "Shared/src/BaseStructure.h"
#include "Shared/src/Obj.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>


class CNPC;

enum FormationProcessType : std::int32_t {
    FPT_ARRIVED_GOAL = 0,
    FPT_GOTO_WAIT = 1,
    FPT_ARRIVED_WAIT = 2,
    FPT_GOTO_GOAL = 3,
};

enum FormationConstType : std::int32_t {
    FCT_SUPERPOINT = 0,
    FCT_FREEWAY = 1,
};

// FormationManager.h:25-28. char16_t keeps the target's two-byte wchar_t
// data layout on every supported host.
struct FormationConstData {
    int id = 0;
    int size = 0;
    char16_t name[24]{};
    FormationConstType type = FCT_SUPERPOINT;
    FVector pos_var[30]{};
};


struct NPCFormation {
    FVector wait;
    FVector goal;
    FVector wait_reserved;
    FVector goal_reserved;
    CNPC* npc = nullptr;
    bool leader = false;
    FormationProcessType type = FPT_ARRIVED_GOAL;
};


class CFormationInst : public MemoryObject {
public:
    CFormationInst(int uniqueId, FormationConstData* formationData);
    ~CFormationInst() override;

    bool InsertNPC(CNPC* npc, int index, bool leader);
    bool CheckAllArrived(FormationProcessType type);
    int WhereAmI(CNPC* npc);
    FVector GetNextWait(CNPC* npc);
    FVector GetNextGoal(CNPC* npc);
    void NotifyArrived(CNPC* npc, FormationProcessType type);
    void GoAll(FormationProcessType type);
    void ReserveNextPos(FVector goal, FVector wait);
    void SetNextPos();
    bool AllNPCLoaded();
    void SendNodeArrivedEventToAll();

    // These accessors are inline in the named source, so their reads are
    // folded into callers in the current executable.
    bool NextPosSet() { return m_bNextPosSet; }
    bool NextPosReserved() { return m_bNextPosReserved; }
    int GetUniqueID() { return m_nUniqueID; }

private:
    int m_nUniqueID;
    bool m_bNextPosSet;
    bool m_bNextPosReserved;
    bool m_bSentEvent;
    NPCFormation m_sNPCFormation[30]{};
    FormationConstData* m_pFormationData;
    CYieldLock m_Lock;
};

class CFormationManager : public MemoryObject {
public:
    CFormationManager();
    ~CFormationManager() override = default;

    L2_DECLARE_OBJECT_TYPE(CFormationManager)

    CFormationInst* GetFormationInst(int uniqueId);
    void InsertFormationConstData(FormationConstData data);
    CFormationInst* MakeNewFormationInst(int uniqueId, int formationId);
    bool InsertNPCToFormation(CNPC* npc, int uniqueId, int formationId, int formationIndex, bool leader);
    void Load();

private:
    using FormationConstDataList = std::vector<FormationConstData>;
    using FormationInstList = std::vector<CFormationInst*>;

    FormationConstDataList m_lstFormationConstData;
    FormationInstList m_lstFormationInst;
    CYieldLock m_InstLock;
};


#if INTPTR_MAX == INT64_MAX
// g_FormationManager is at 0x019ae520 and the following
// g_FreewayManager is at 0x019ae588 in the current executable.
#endif


extern CFormationManager g_FormationManager;
