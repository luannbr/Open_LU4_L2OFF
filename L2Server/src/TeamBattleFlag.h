#pragma once

#include "StaticObject.h"

#include <cstddef>
#include <cstdint>

class CUserSocket;

// Exact current TeamBattleFlag.cpp owner. The current target adds no data
// members to CStaticObject. Its distinct lifecycle exists for allocation
// accounting, identity dispatch and the dedicated 0x92 spawn packet.
class CTeamBattleFlag : public CStaticObject {
public:
    CTeamBattleFlag(wchar_t* className, FVector position, FAngle direction);
    CTeamBattleFlag(const CTeamBattleFlag& other);
    ~CTeamBattleFlag() override;

    L2_DECLARE_OBJECT_REGISTRATION(CTeamBattleFlag)

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    CStaticObject* Copy() override;
    bool IsTeamBattleFlag() override;
    CTeamBattleFlag* CastTeamBattleFlag() override;

    void Appear();
    void Disappear();
    void SendTeamBattleFlagInfo(CUserSocket* socket);

private:
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CTeamBattleFlag)
};
