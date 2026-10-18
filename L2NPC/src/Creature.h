#pragma once

#include "Shared/src/MemorySub.h"

#include "L2NPC/src/Object.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/Exception.h"
#include "Shared/src/SharedFactory.h"
#include "ThreadSafeLibrary.h"

#include <cstddef>
#include <cstdint>


class User;
class CNPC;

// Process-local creature object recovered from l2npc/creature.h. The shared
// record is authoritative game state, but its presence does not imply that a
// CCreature object exists in this process.
class CCreature : public CObject {
public:
    CCreature();
    ~CCreature() override;

    L2_DECLARE_OBJECT_TYPE(CCreature)

    static CCreature* GetObjectW(const std::uint32_t smartId) { return g_CreatureSmartIdManager.GetObjectW(smartId); }

    static CSPointer<CCreature> FindObjectSP(std::uint32_t smartId) {
        return {GetObjectW(smartId), ObjectRefType::ObjectIndex};
    }

    bool Delete() override;

    FVector GetPos() const { return m_data->GetPos(); }

    FAngle GetDir() const { return m_data->GetDir(); }

    int GetIndex() const { return m_data->index; }

    bool IsAlive() const { return m_data->m_bIsActive && m_data->m_bIsAlive != 0; }

    virtual bool IsPC() const { return false; }

    virtual bool IsNPC() const { return false; }

    virtual User* CastUser() { return nullptr; }

    virtual CNPC* CastNPC() { return nullptr; }

    virtual void ReleaseWarning() {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] WARNING!!! creature release -> free  "
        //     L"with DeleteCount=0",
        //     L"c:\\work\\lineage2build\\server\\main\\"
        //     L"l2npc\\Creature.h",
        //     0x3a)
        // Log.Add(LOG_ERROR,
        //     "[object lifecycle] Creature %u was released without a prior Delete call. Dumping call stack.",
        //     static_cast<unsigned int>(Id()))
        L2_LOG_ERROR(
            Log, "[object lifecycle] Creature %u was released without a prior Delete call; dumping call stack.", Id()
        );
        ReportCallStack(LOG_ERROR, false);
    }

    bool IsVisible() const;

    bool IsGMVisible() const { return !m_data->m_bGMInvisible; }

    virtual int IsTargetable() const;

    virtual bool AddReverseHate(std::uint32_t, int) { return false; }

    virtual void SeeCreature(CCreature*) {}

    virtual void Spelled(CSharedCreatureData*, int, int, int) {}

    void ShowPosByAdena(const FVector& position);

    virtual char16_t* GetName() { return m_data->m_sName; }

public:
    // Public CCreature+0x20 in the target PDB.
    CSharedCreatureData* m_data;

private:
    // The target declares this constructor private and emits no body.
    CCreature(const CCreature&);
};
