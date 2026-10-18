#include "BaseSpawnDefine.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Utils.h"

#include <cmath>
#include <cstdint>


// Current L2NPC.exe 0x00419050 initializes CIOObject, installs this vtable
// and clears only m_nInZoneID at +0x18.
BaseSpawnDefine::BaseSpawnDefine() : m_nInZoneID(0) {}

// Current L2NPC.exe 0x004191d0 copies CIOObject and the single semantic
// BaseSpawnDefine field. The target's implicit tail padding is untouched.
BaseSpawnDefine::BaseSpawnDefine(const BaseSpawnDefine& other) : CIOObject(other), m_nInZoneID(other.m_nInZoneID) {}

BaseSpawnDefine::~BaseSpawnDefine() = default;

int BaseSpawnDefine::GetSpawnTime(int baseTime, int spread) {
    L2_GUARD_FUNCTION();

    if (baseTime < 1) {
        return 0;
    }
    if (spread == 0) {
        return baseTime;
    }

    // Exact target expression: base + trunc(real2 * (2*spread+1) - spread).
    // Keep the two's-complement dword arithmetic used by the executable.
    const std::uint32_t doubled = static_cast<std::uint32_t>(spread) * 2U + 1U;
    const double offset =
        genrand64_real2() * static_cast<double>(static_cast<std::int32_t>(doubled)) - static_cast<double>(spread);
    const int result = static_cast<int>(
        static_cast<std::uint32_t>(baseTime) + static_cast<std::uint32_t>(runtime::Win64CvttDoubleToInt32(offset))
    );
    return result < 1 ? 0 : result;
}

bool BaseSpawnDefine::IsWorldSpawnDefine() { return false; }

l2npc::NpcMakerEx* BaseSpawnDefine::GetMyMaker() { return nullptr; }

TerritoryInfo* BaseSpawnDefine::GetTerritoryInfo() { return nullptr; }

FVector BaseSpawnDefine::GenerateSpawnPos(int&, bool) { return {}; }

int BaseSpawnDefine::GetTotalNPC() { return 0; }

bool BaseSpawnDefine::ReserveNPC() { return false; }

void BaseSpawnDefine::OnNpcCreated(CNPC*) {}

void BaseSpawnDefine::OnNpcDeleted(CNPC*) {}

void BaseSpawnDefine::OnNpcEnterWorld(CNPC*) {}

bool BaseSpawnDefine::SetAIParameter(CNPC*, const char16_t*, AiParameterMap&) { return false; }

bool BaseSpawnDefine::IsInTerritory(FVector, bool) { return true; }

bool BaseSpawnDefine::IsInTerritoryWithLogVisitor(FVector, ZLogVisitor&, bool) { return true; }

bool BaseSpawnDefine::IsInBannedTerritory(FVector, bool) { return false; }

void BaseSpawnDefine::SpawnNpc(bool) {}

int BaseSpawnDefine::CreateMakerKey() { return 0; }

CSharedCreatureData* BaseSpawnDefine::GetNpcFromKey(int) { return nullptr; }

int BaseSpawnDefine::GetChaseDistance() { return 0; }

int BaseSpawnDefine::GetPledgeDBID() const { return 0; }
