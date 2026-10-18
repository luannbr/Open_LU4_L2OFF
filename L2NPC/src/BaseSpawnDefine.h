#pragma once

#include "IOC.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/StandardContainers.h"

namespace l2npc {
class NpcMakerEx;
}


class CNPC;
class CSharedCreatureData;
class TerritoryInfo;
class ZLogVisitor;

// Recovered l2npc/BaseSpawnDefine. The Windows object is a 0x20-byte
// CIOObject whose only semantic data member is m_nInZoneID at +0x18. The
// portable hierarchy keeps the original class ownership and polymorphic
// boundary used by CNPC::SetSpawnDefine.
class BaseSpawnDefine : public CIOObject {
public:
    using AiParameterMap = std::map<std::u16string, std::u16string, runtime::CStringLess<std::u16string>>;

    BaseSpawnDefine();
    BaseSpawnDefine(const BaseSpawnDefine& other);
    ~BaseSpawnDefine() override;

    static int GetSpawnTime(int baseTime, int spread);

    // Keep this declaration order aligned with BaseSpawnDefine::vftable in
    // the shipping executable. The inherited CIOObject portion ends at
    // +0x58. These methods occupy +0x60 through +0xe8.
    virtual bool IsWorldSpawnDefine();
    virtual l2npc::NpcMakerEx* GetMyMaker();
    virtual TerritoryInfo* GetTerritoryInfo();
    virtual FVector GenerateSpawnPos(int& heading, bool withSky);
    virtual int GetTotalNPC();
    virtual bool ReserveNPC();
    virtual void OnNpcCreated(CNPC* npc);
    virtual void OnNpcDeleted(CNPC* npc);
    virtual void OnNpcEnterWorld(CNPC* npc);
    virtual bool SetAIParameter(CNPC* npc, const char16_t* aiName, AiParameterMap& parameters);
    virtual bool IsInTerritory(FVector position, bool log);
    virtual bool IsInTerritoryWithLogVisitor(FVector position, ZLogVisitor& visitor, bool log);
    virtual bool IsInBannedTerritory(FVector position, bool log);
    virtual void SpawnNpc(bool alreadyReserved);
    virtual int CreateMakerKey();
    virtual CSharedCreatureData* GetNpcFromKey(int key);
    virtual int GetChaseDistance();
    // The older adjacent L2NPC PDB predates this final slot. The current EXE
    // and NASC64_2015 PDB agree on its const signature and placement.
    virtual int GetPledgeDBID() const;

    unsigned int GetInZoneID() { return m_nInZoneID; }
    void SetInZoneID(unsigned int value) { m_nInZoneID = value; }

private:
    unsigned int m_nInZoneID;

    // MSVC does not reuse this class's 4-byte tail padding for members of a
    // derived class. Itanium-ABI compilers do, so create the target
    // padding to keep CNpcSpawnDefine's first member at +0x20 on every host.
    // It is not initialized: this is not an original member,
    // and both target constructors leave the bytes at +0x1c untouched.
    [[maybe_unused]] unsigned int m_TargetTailPadding;
};
