#pragma once

#include "StaticObject.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>
#include <utility>

class CResidence;

// Standalone decoration definition loaded from decodata.txt. Target
// wchar_t storage is represented as UTF-16 so the fixed name
// array keeps its current Win64 extent on portable hosts.
class CResidenceDeco {
public:
    using ResidenceDecoCost = std::pair<std::uint32_t, std::int64_t>;
    using ResidenceDecoCostList = std::vector<ResidenceDecoCost, std::allocator<ResidenceDecoCost>>;

    CResidenceDeco();

    // The current callers inline this exact first-record projection and do
    // not test the list for emptiness before dereferencing it.
    std::int64_t GetCost() { return m_lstCost.front().second; }

    std::int32_t m_nDecoId; // +0x000
    char16_t m_sDecoName[256]; // +0x004
    std::int32_t m_nType; // +0x204
    std::int32_t m_nLevel; // +0x208
    std::int32_t m_nDepth; // +0x20c
    std::int32_t m_nFunction; // +0x210
    double m_dArgument; // +0x218
    ResidenceDecoCostList m_lstCost; // +0x220

private:
    // The historical declaration supplies this access qualifier. The current
    // image has no compiled destructor body to promote as independent evidence.
    ~CResidenceDeco() = default;
};


// The current InstantAgit and Residence owners create this static-world
// object directly. The 32-byte depth payload is sent unchanged to clients.
class CResidenceDecoVisible : public CStaticObject {
public:
    CResidenceDecoVisible(std::uint32_t residenceId, FVector position);
    CResidenceDecoVisible(const CResidenceDecoVisible& other);
    ~CResidenceDecoVisible() override;

    L2_DECLARE_OBJECT_REGISTRATION(CResidenceDecoVisible)

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    CObject* Copy() override;
    bool IsResidenceDecoVisible() override;
    CResidenceDecoVisible* CastResidenceDecoVisible() override;
    bool IsEnemyTo(CCreature* creature) override;
    bool IsAlive() override;
    bool IsActive() override;
    double AttackedBy(CCreature* attacker, double damage) override;
    double GotDamageBy(CCreature* attacker, double damage) override;
    virtual bool IsEnabled();

    void SendResidenceDecoVisibleInfo(CUserSocket* socket);
    void SetDecoDepth(int index, int depth);


private:
    friend class CResidence;

    std::uint32_t m_nResidenceId;
    std::uint8_t m_aDepthForClient[32];

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CResidenceDecoVisible)
};
