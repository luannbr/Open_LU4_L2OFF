#pragma once

#include "StaticObject.h"

#include "Shared/src/YieldLock.h"

#include <cstddef>


class CFortress;
class CUserSocket;
class User;

// Exact current L2Server fortress-flagpole envelope. The fortress pointer is
// borrowed. The embedded CYieldLock is non-allocating and has no destructor
// action in the current teardown root.
class CFortressFlagpole : public CStaticObject {
public:
    explicit CFortressFlagpole(CFortress* fortress);
    ~CFortressFlagpole() override;

    bool IsFortressFlagpole() override;
    CFortressFlagpole* CastFortressFlagpole() override;
    bool IsAlive() override;
    void DecideDistance(CMoveToTargetShareData* data, CSmartID creatureId) override;

    bool IsFlagSet();
    void SendFortressFlagpoleInfo(CUserSocket* socket);
    void CapturedInFortressSiegeBy(User* user);
    void CaptureInFortressSiegeStart(User* user);


protected:
    friend class CFortress;

    bool m_bIsFlagSet;
    std::byte padding_4e9[3];
    ::CYieldLock m_FlagLock;
    std::byte padding_4fc[4];
    CFortress* m_pFortress;
};
