#pragma once

#include "Shared/src/Obj.h"
#include "Shared/src/PledgeMasteryID.h"
#include "Shared/src/Utils.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CPledge;
class User;
class CUserSocket;


// Exact current 0x78 PledgeMastery.cpp owner. The current constructor at
// 009cde28 fixes the JobTransactionLock and all three pledge identifiers.
// the 17 dword points and loaded byte are covered by the DB
// callback and every query path.
class PledgeMastery : public MemoryObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(PledgeMastery)

    PledgeMastery(std::uint32_t pledgeSid, int worldId, int pledgeDatabaseId);
    ~PledgeMastery() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    void OnSetPoint(PledgeMasteryID::Type masteryId, int point);
    void OnResetPoint();
    void LoadFromDB();

    bool IsLoaded() const noexcept { return m_bLoadedFromDB; }
    int GetPoint(PledgeMasteryID::Type masteryId) const;
    const std::array<int, PledgeMasteryID::MAX_COUNT>& GetPoints() const noexcept { return m_anPoints; }
    int GetTotalPoint() const noexcept;
    int GetPledgeDBid() const noexcept { return m_dbidPledge; }
    JobTransactionLock& GetTransactionLock() noexcept { return m_TransactionLock; }
    bool RequestResetPoint(User* user, CPledge* pledge);
    bool RequestSetPoint(User* user, CPledge* pledge, PledgeMasteryID::Type masteryId);

private:
    JobTransactionLock m_TransactionLock; // +0x18
    std::uint32_t m_sidPledge; // +0x24
    int m_idWorld; // +0x28
    int m_dbidPledge; // +0x2c
    std::array<int, PledgeMasteryID::MAX_COUNT> m_anPoints; // +0x30
    bool m_bLoadedFromDB; // +0x74
    [[maybe_unused]] std::byte m_padding75To78[3]{};

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(PledgeMastery)
};


namespace PledgeMasteryStuff {

namespace Builder {
void SendPledgeMasteryInfoTo(CUserSocket* socket, const CPledge* pledge);
}

int GetMaxMasteryPoint(const CPledge* pledge);
int GetRemainMasteryPoint(const CPledge* pledge);
void RequestResetPoint(User* user);
void RequestSetPoint(User* user, PledgeMasteryID::Type masteryId);
void SendPledgeMasteryInfoTo(User* user);
void SendPledgeMasteryInfoToMembers(CPledge* pledge);

} // namespace PledgeMasteryStuff
