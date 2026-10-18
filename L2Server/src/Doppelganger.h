#pragma once

#include "NPC.h"

class CDoppelganger : public CNPC {
public:

    CDoppelganger();
    CDoppelganger(const CDoppelganger& other);
    ~CDoppelganger() override;

    static void* operator new(std::size_t requestedSize);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void ReportOrt(const char* callerFile, int callerLine) override;
    CObject* Copy() override;
    bool IsPCOrPCRelatedNPC() const override;
    bool IsDoppelganger() const override;
    CDoppelganger* CastDoppelganger() override;
    bool IsEnemyTo(CCreature* creature) override;
    void OnCreated() override;
    int GetDoppelgangerSummonerSID() override;
    bool HasSummoner() override;
};
