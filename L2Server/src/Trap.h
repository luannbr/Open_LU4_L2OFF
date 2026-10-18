#pragma once

#include "NPC.h"

#include "Shared/src/BaseStructure.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>


// The spellings are donor-selected source vocabulary. The three current
// interaction wrappers produce the complete numeric domain.
enum TrapInteractType : std::int32_t {
    TIT_DEFUSE = 0,
    TIT_ACTIVATE = 1,
    TIT_DETECTED = 2,
};


class CSkillInfo;
class CTerritory;
class CWorldTrap;

// Current materializers allocate exactly 0x30 bytes. The area and skill
// pointers are borrowed from the script-data catalog.
class CWorldTrapInfo {
public:
    void SetTrapArea(CTerritory* territory);

    CTerritory* m_pTrapArea = nullptr;
    FVector m_TrapPos;
    std::int32_t m_Id = 0;
    std::byte m_padding024[4]{};
    CSkillInfo* m_pSkillInfo = nullptr;
};


// Exact current CTrap prefix. Both constructors reset activation rather
// than copying it and force the inherited creature visibility off.
class CTrap : public CNPC {
public:
    enum StepType : std::int32_t {
        STEP_IN = 0,
        STEP_OUT = 1,
    };

    L2_DECLARE_OBJECT_REGISTRATION(CTrap)

    CTrap();
    CTrap(const CTrap& other);
    ~CTrap() override = default;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void ReportOrt(const char* callerFile, int callerLine) override;

    bool IsTrap() override;
    CTrap* CastTrap() override;

    bool Activate(CCreature* creature);
    bool Defuse(CCreature* creature);
    void OnInteracted(CCreature* creature, TrapInteractType interaction);

    virtual bool CheckTrap(CCreature& creature, StepType step) = 0;
    virtual void OnActivateTo(CCreature* creature);
    virtual void OnDefuse(CCreature* creature);
    virtual void OnDetected(CCreature* creature);


protected:
    volatile std::int32_t m_nActivated;
    std::byte m_padding3d7c[4];
};


class CPCTrap final : public CTrap {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CPCTrap)

    CPCTrap();
    CPCTrap(const CPCTrap& other);
    ~CPCTrap() override = default;

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()
    void ReportOrt(const char* callerFile, int callerLine) override;

    CObject* Copy() override;
    bool IsPCTrap() override;
    bool IsPCOrPCRelatedNPC() const override;
    CPCTrap* CastPCTrap() override;
    User* GetUserOrMaster() override;
    void SendSystemMessageWithSelf(unsigned int messageId) override;
    bool IsEnemyTo(CCreature* creature) override;
    void InSight(CObject* object, bool flag) override;
    bool Die(CCreature* killer, int reason) override;
    void EnterWorld(bool unknown, int first, int second, unsigned int third) override;
    User* GetMaster() const override;
    bool HasSummoner() override;

    bool CheckTrap(CCreature& creature, StepType step) override;
    void OnDefuse(CCreature* creature) override;
    void OnDetected(CCreature* creature) override;
};


class CWorldTrap final : public CTrap {
    friend class CWorldTrapDB;

public:
    L2_DECLARE_OBJECT_REGISTRATION(CWorldTrap)

    CWorldTrap();
    CWorldTrap(const CWorldTrap& other);
    ~CWorldTrap() override;

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()
    void ReportOrt(const char* callerFile, int callerLine) override;

    CObject* Copy() override;
    bool IsWorldTrap() override;
    CWorldTrap* CastWorldTrap() override;
    void InSight(CObject* object, bool flag) override;
    void OutOfSight(CObject* object, bool flag) override;
    void OnCreated() override;
    void Delete() override;
    void EnterWorld(bool unknown, int first, int second, unsigned int third) override;

    void SetTrapInfo(CWorldTrapInfo* trapInfo) { m_pTrapInfo = trapInfo; }
    CTerritory* GetTrapArea() const { return m_pTrapInfo != nullptr ? m_pTrapInfo->m_pTrapArea : nullptr; }

    bool ValidateCheckTrap(CCreature& creature);
    bool CheckTrap(CCreature& creature, StepType step) override;
    bool CheckTrapByPos(CCreature& creature, const FVector& previousPosition, const FVector& currentPosition);
    void OnStepIn(CCreature& creature);
    void OnStepOut(CCreature& creature);


private:
    CYieldLock m_Lock;
    CWorldTrapInfo* m_pTrapInfo;
    std::set<std::int32_t> m_CreatureInTrap;
    std::uint64_t m_CreatureNum;
};


class CWorldTrapDB {
public:
    using TrapInfoMap = std::map<int, CWorldTrapInfo*>;
    using TrapObjectMap = std::map<int, int>;
    using InZoneTrapMap = std::map<int, TrapObjectMap>;

    CWorldTrapDB();
    ~CWorldTrapDB() = default;

    bool AddTrapInfo(CWorldTrapInfo* info);
    bool AddWorldTrap(CWorldTrap* trap);
    bool DeleteWorldTrap(CWorldTrap* trap);
    CWorldTrap* GetWorldTrap(int inZoneId, int classId);
    void OnInZoneDelete(int inZoneId);

private:
    TrapInfoMap m_WorldTrapInfoDB; // +0x00
    InZoneTrapMap m_WorldTrapInzoneDB; // +0x20
    CYieldLock m_Lock; // +0x40
};

extern CWorldTrapDB g_WorldTrapDB;
