#pragma once

#include "Shared/src/ItemMani.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>


class AtomicJob;
class CAIParameter;
class CAIProperty;
class CNPCEventHandler;
class CScriptBinCode;
class EtcMani;
struct ScriptEvent;

// l2npc/ScriptEngine.cpp, current L2NPC.exe 0x0050f098.
int GetCurrentNPCEventID();

// l2npc/ScriptEngine.h. The class itself is stateless. The current quest id,
// temporary strings and atomic-job compositor are all indexed by the current
// logical IO thread in the shipping process.
class CScriptEngine {
public:
    static int TLGetCurrentQuestID();
    static void TLSetCurrentQuestID(int questId);
    static void AddAtomicItemProcess(int creatureIndex, int itemClassId, std::int64_t count, int enchanted, int blessed);
    static void AddAtomicJob(int compositorKey, AtomicJob* job);
    static int RunNPCScript(
        ScriptEvent* event, CNPCEventHandler* handler, CScriptBinCode* code, CAIParameter* parameter, CAIProperty* property
    );
};

// L2NPC.pdb: ScriptEngine.obj's exact POD used by CNPC::AddChoice and
// CNPC::ShowChoicePage. The target wchar_t is a two-byte Windows code unit.
// char16_t keeps both its contents and its 0x7d4-byte cross-platform
// layout.
struct ChoiceData {
    int Choice;
    char16_t ChoiceString[1000];
};


// Exact process TLS globals exposed by L2NPC.pdb. Every outermost script
// invocation clears the five counters. The 4x100 backing records are reused.
extern thread_local int th_ChoiceN;
extern thread_local int th_ChoiceCount1;
extern thread_local int th_ChoiceCount2;
extern thread_local int th_ChoiceCount3;
extern thread_local int th_ChoiceCount4;
extern thread_local ChoiceData th_Choices[4][100];

// l2npc/ScriptEngine.cpp. The matching NASC64_2015 PDB exposes this exact
// source-level compositor: one ordered map from creature index to three
// vectors and the check-item flag. The current executable confirms the
// Add*/Process/Clear behavior and packet order.
class AtomicJobCompositor {
public:
    class JobInfoForUser {
    public:
        explicit JobInfoForUser(bool checkItemJob);
        JobInfoForUser();

        bool CheckItemJob() const;

        std::vector<ItemManip> mItemManiList;
        std::vector<EtcMani*> mEtcManiList;
        std::vector<AtomicJob*> mAtomicJobList;

    private:
        bool m_CheckItemJob = false;
    };

    AtomicJobCompositor();
    ~AtomicJobCompositor();

    void Initialize();
    int Process();
    void Clear();
    void AddJob(int creatureIndex, AtomicJob* job);
    void AddItemManip(int creatureIndex, const ItemManip& manipulation, bool checkItemJob);
    void AddEtcMani(int creatureIndex, EtcMani* manipulation);
    bool IsEmpty() const;

private:
    using MAP_TYPE = std::map<unsigned int, JobInfoForUser>;

    MAP_TYPE mUserToJobInfoMap;
};


// l2npc/ScriptEngine.cpp. The shipping VM owns one 20,000 UTF-16-code-unit
// temporary-string pool per worker and resets its cursor for each outermost
// script invocation.
void InitTmpString();
char16_t* AllocTmpString(int length);
char16_t* AllocIntegerString(std::int64_t value);
char16_t* AllocFloatString(float value);
char16_t* GetTmpStringEnd();
