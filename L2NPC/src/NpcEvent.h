#pragma once

#include "Shared/src/MemorySub.h"

#include "Shared/src/Obj.h"
#include "Shared/src/ScriptAccessible.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

namespace l2npc {
class DBNpcInfo;
class NpcMakerEx;
} // namespace l2npc


class CFHTML;
class CodeInfo;
class CodeInfoList;
class CSharedCreatureData;
class CSharedGeneralObjectData;
class CSharedItemData;
class CSharedPartyData;
class CSharedPledgeData;
class CSharedStaticObjectData;
class GlobalObject;
class HateInfo;
class ItemIndexList;
class NpcSpawnDefineEx;
class Position;
class PositionList;
class RoomInfo;
class RoomInfoList;
class CNPC;

// NpcEvent.h, current L2NPC.exe size 0x380. MemoryObject occupies +0x00
// through +0x17 and ScriptAccessibleEx is the secondary base at +0x18.
// NASC bytecode addresses the fields below by these physical offsets.
struct ScriptEvent : MemoryObject, ScriptAccessibleEx {
    explicit ScriptEvent(int eventId);
    ~ScriptEvent() override;

    L2_DECLARE_OBJECT_TYPE(ScriptEvent)

    // Shipping ScriptEvent primary-vtable slots +0x20 through +0x38.
    virtual int GetEventID(const char16_t* name);
    virtual const char16_t* GetEventName(int id);
    virtual char16_t* GetMySelfName();
    virtual int GetMySelfID();

    void CollectVar(ScriptTypeInfo* typeInfo) override;
    ScriptAccessibleEx* GetScriptAccessibleEx() const override;

    void CreateVariable(std::vector<std::u16string>& variables);

    int m_nEventId = 0; // +0x020
    int _sav_start_ = 0; // +0x024
    CSharedCreatureData* talker = nullptr; // +0x028
    CSharedCreatureData* attacker = nullptr; // +0x030
    CSharedCreatureData* victim = nullptr; // +0x038
    CSharedCreatureData* _private = nullptr; // +0x040
    CSharedCreatureData* _friend = nullptr; // +0x048
    CSharedCreatureData* commander = nullptr; // +0x050
    CSharedCreatureData* speller = nullptr; // +0x058
    CSharedCreatureData* target = nullptr; // +0x060
    CSharedCreatureData* from = nullptr; // +0x068
    CSharedCreatureData* creature = nullptr; // +0x070
    CSharedStaticObjectData* victim_so = nullptr; // +0x078
    CSharedItemData* item = nullptr; // +0x080
    char16_t* script = nullptr; // +0x088
    int damage = 0; // +0x090
    int state = 0; // +0x094
    std::uint32_t who = 0; // +0x098
    int quest_id = 0; // +0x09c
    int ask = 0; // +0x0a0
    int skill_id = 0; // +0x0a4
    int skill_level = 0; // +0x0a8
    int skill_sub_level = 0; // +0x0ac
    int need_quest = 0; // +0x0b0
    int occupation_name_id = 0; // +0x0b4
    int way_point_index = 0; // +0x0b8
    int next_way_point_index = 0; // +0x0bc
    int time = 0; // +0x0c0
    int weapon_class_id = 0; // +0x0c4
    int success = 0; // +0x0c8
    int x = 0; // +0x0cc
    int y = 0; // +0x0d0
    int z = 0; // +0x0d4
    int pledge_index = 0; // +0x0d8
    int desire = 0; // +0x0dc
    int residence_id = 0; // +0x0e0
    int inzone_id = 0; // +0x0e4
    int inzone_type_id = 0; // +0x0e8
    int event_id = 0; // +0x0ec
    int question_id = 0; // +0x0f0
    int timer_id = 0; // +0x0f4
    int script_event_arg1 = 0; // +0x0f8
    int script_event_arg2 = 0; // +0x0fc
    int script_event_arg3 = 0; // +0x100
    int level = 0; // +0x104
    int action_id = 0; // +0x108
    int IsCritical = 0; // +0x10c
    int scene_id = 0; // +0x110
    int rank = 0; // +0x114
    int reward_flag = 0; // +0x118
    int _pad_11c = 0; // +0x11c
    std::int64_t reply = 0; // +0x120
    std::int64_t skill_name_id = 0; // +0x128
    std::int64_t i0 = 0; // +0x130
    std::int64_t i1 = 0; // +0x138
    std::int64_t i2 = 0; // +0x140
    std::int64_t i3 = 0; // +0x148
    std::int64_t i4 = 0; // +0x150
    std::int64_t i5 = 0; // +0x158
    std::int64_t i6 = 0; // +0x160
    std::int64_t i7 = 0; // +0x168
    std::int64_t i8 = 0; // +0x170
    std::int64_t i9 = 0; // +0x178
    std::int64_t i10 = 0; // +0x180
    std::int64_t i11 = 0; // +0x188
    float f0 = 0.0F; // +0x190
    float f1 = 0.0F; // +0x194
    float f2 = 0.0F; // +0x198
    float f3 = 0.0F; // +0x19c
    float f4 = 0.0F; // +0x1a0
    float f5 = 0.0F; // +0x1a4
    float f6 = 0.0F; // +0x1a8
    float f7 = 0.0F; // +0x1ac
    float f8 = 0.0F; // +0x1b0
    float f9 = 0.0F; // +0x1b4
    CSharedCreatureData* c0 = nullptr; // +0x1b8
    CSharedCreatureData* c1 = nullptr; // +0x1c0
    CSharedCreatureData* c2 = nullptr; // +0x1c8
    CSharedCreatureData* c3 = nullptr; // +0x1d0
    CSharedCreatureData* c4 = nullptr; // +0x1d8
    CSharedPledgeData* pledge0 = nullptr; // +0x1e0
    CSharedPledgeData* pledge1 = nullptr; // +0x1e8
    CSharedPartyData* party0 = nullptr; // +0x1f0
    CSharedPartyData* party1 = nullptr; // +0x1f8
    CSharedItemData* item0 = nullptr; // +0x200
    CSharedItemData* item1 = nullptr; // +0x208
    CSharedStaticObjectData* so0 = nullptr; // +0x210
    HateInfo* h0 = nullptr; // +0x218
    HateInfo* h1 = nullptr; // +0x220
    HateInfo* h2 = nullptr; // +0x228
    HateInfo* h3 = nullptr; // +0x230
    HateInfo* h4 = nullptr; // +0x238
    CNPC* npc0 = nullptr; // +0x240
    CNPC* npc1 = nullptr; // +0x248
    CNPC* npc2 = nullptr; // +0x250
    char16_t* s0 = nullptr; // +0x258
    char16_t* s1 = nullptr; // +0x260
    CFHTML* fhtml0 = nullptr; // +0x268
    CFHTML* fhtml1 = nullptr; // +0x270
    CSharedCreatureData* c5 = nullptr; // +0x278
    CSharedCreatureData* c6 = nullptr; // +0x280
    CSharedCreatureData* c7 = nullptr; // +0x288
    CSharedCreatureData* c8 = nullptr; // +0x290
    CSharedCreatureData* c9 = nullptr; // +0x298
    HateInfo* h5 = nullptr; // +0x2a0
    HateInfo* h6 = nullptr; // +0x2a8
    HateInfo* h7 = nullptr; // +0x2b0
    HateInfo* h8 = nullptr; // +0x2b8
    HateInfo* h9 = nullptr; // +0x2c0
    Position* pos2 = nullptr; // +0x2c8
    Position* pos3 = nullptr; // +0x2d0
    Position* pos4 = nullptr; // +0x2d8
    l2npc::NpcMakerEx* maker2 = nullptr; // +0x2e0
    l2npc::NpcMakerEx* maker3 = nullptr; // +0x2e8
    l2npc::NpcMakerEx* maker4 = nullptr; // +0x2f0
    char16_t* s2 = nullptr; // +0x2f8
    char16_t* s3 = nullptr; // +0x300
    char16_t* s4 = nullptr; // +0x308
    int _code = 0; // +0x310
    int _choiceN = 0; // +0x314
    int _from_choice = 0; // +0x318
    int _pad_31c = 0; // +0x31c
    l2npc::NpcMakerEx* maker0 = nullptr; // +0x320
    l2npc::NpcMakerEx* maker1 = nullptr; // +0x328
    RoomInfo* room0 = nullptr; // +0x330
    RoomInfo* room1 = nullptr; // +0x338
    RoomInfoList* rlist0 = nullptr; // +0x340
    RoomInfoList* rlist1 = nullptr; // +0x348
    Position* pos0 = nullptr; // +0x350
    Position* pos1 = nullptr; // +0x358
    PositionList* plist0 = nullptr; // +0x360
    PositionList* plist1 = nullptr; // +0x368
    GlobalObject* gg = nullptr; // +0x370
    int _sav_end_ = 0; // +0x378
    int _pad_37c; // +0x37c
};

struct CNPCEvent final : ScriptEvent {
    CNPCEvent();
    ~CNPCEvent() override;

    int GetEventID(const char16_t* name) override;
    const char16_t* GetEventName(int id) override;
    char16_t* GetMySelfName() override;
    int GetMySelfID() override;
    void CollectVar(ScriptTypeInfo* typeInfo) override;

    int _npc_event_start = 0; // +0x380
    int _pad_384 = 0; // +0x384
    CSharedCreatureData* last_attacker = nullptr; // +0x388
    CSharedCreatureData* member = nullptr; // +0x390
    CSharedPartyData* lparty = nullptr; // +0x398
    CSharedGeneralObjectData* lmpcc = nullptr; // +0x3a0
    CSharedPartyData* aparty = nullptr; // +0x3a8
    CodeInfoList* random1_list = nullptr; // +0x3b0
    CodeInfoList* always_list = nullptr; // +0x3b8
    CodeInfo* code_info = nullptr; // +0x3c0
    ItemIndexList* m_ItemIndexList = nullptr; // +0x3c8
    CNPC* myself = nullptr;
    int _npc_event_end = 0; // +0x3d8
    int _pad_3dc; // +0x3dc
};

struct MakerEvent final : ScriptEvent {
    MakerEvent();
    ~MakerEvent() override;

    int GetEventID(const char16_t* name) override;
    const char16_t* GetEventName(int id) override;
    char16_t* GetMySelfName() override;
    void CollectVar(ScriptTypeInfo* typeInfo) override;

    int _maker_event_start = 0; // +0x380
    int _pad_384 = 0; // +0x384
    l2npc::NpcMakerEx* myself = nullptr;
    NpcSpawnDefineEx* def0 = nullptr;
    NpcSpawnDefineEx* deleted_def = nullptr;
    NpcSpawnDefineEx* loaded_def = nullptr;
    NpcSpawnDefineEx* created_def = nullptr;
    CNPC* deleted_npc = nullptr;
    CNPC* created_npc = nullptr;
    l2npc::DBNpcInfo* record0 = nullptr;
    int enabled = 0;
    int died = 0;
    int _maker_event_end = 0; // +0x3d0
    int _pad_3d4; // +0x3d4
};
