#include "Shared/src/TerritoryInfo.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"
#include "Shared/src/World.h"
#include "TextRuntime.h"

#if defined(L2_BUILD_L2SERVER)
#include "CWorld.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace {

int RandomIntegerInclusive(int minimum, int maximum) {
    const int range =
        runtime::Int32FromBits(static_cast<std::uint32_t>(maximum) - static_cast<std::uint32_t>(minimum) + 1U);
    return runtime::Win64CvttDoubleToInt32(genrand64_real2() * static_cast<double>(range) + static_cast<double>(minimum));
}

std::size_t RandomIndex(std::size_t size) {
    int minimum = 0;
    int maximum = runtime::Int32FromBits(static_cast<std::uint32_t>(size) - 1U);
    if (maximum < minimum) {
        std::swap(minimum, maximum);
    }
    return static_cast<std::size_t>(RandomIntegerInclusive(minimum, maximum));
}

} // namespace

TerritoryInfo::TerritoryInfo() {
    mRandomPosGenerated = false;
    mBannedTerritoryList.clear();
    mTerritoryList.clear();
    mRandomPosList.clear();
}

TerritoryInfo::TerritoryInfo(const TerritoryInfo& other) :
    mTerritoryList(other.mTerritoryList),
    mBannedTerritoryList(other.mBannedTerritoryList),
    mRandomPosGenerated(other.mRandomPosGenerated) {
    // The shipping copy constructor leaves both generated
    // position vectors empty and constructs a fresh CYieldLock.
}

void TerritoryInfo::AddTerritory(CTerritory* territory) {
    L2_GUARD_FUNCTION();
    mTerritoryList.push_back(territory);
}

void TerritoryInfo::AddBannedTerritory(CTerritory* territory) {
    L2_GUARD_FUNCTION();
    mBannedTerritoryList.push_back(territory);
}

int TerritoryInfo::GetTerritorySize() {
    L2_GUARD_FUNCTION();
    return static_cast<int>(mTerritoryList.size());
}

CTerritory* TerritoryInfo::GetBannedTerritory(int index) {
    L2_GUARD_FUNCTION();
    // Current 0x004887d0 performs the unchecked vector subscript directly.
    return runtime::CheckedVectorAt(mBannedTerritoryList, static_cast<std::size_t>(index));
}

int TerritoryInfo::GetBannedTerritorySize() {
    L2_GUARD_FUNCTION();
    return static_cast<int>(mBannedTerritoryList.size());
}

const char16_t* TerritoryInfo::GetFirstTerritoryName() {
    L2_GUARD_FUNCTION();
    if (GetTerritorySize() == 0) {
        return u"No territory";
    }
    return GetTerritory(0)->sName.c_str();
}

const char16_t* TerritoryInfo::GetLastTerritoryName() {
    L2_GUARD_FUNCTION();
    if (GetTerritorySize() == 0) {
        return u"No territory";
    }
    return GetTerritory(GetTerritorySize() - 1)->sName.c_str();
}

CTerritory* TerritoryInfo::GetTerritory(int index) {
    L2_GUARD_FUNCTION();
    if (index < 0 || static_cast<std::size_t>(index) >= mTerritoryList.size()) {
        return nullptr;
    }
    return runtime::CheckedVectorAt(mTerritoryList, static_cast<std::size_t>(index));
}

bool TerritoryInfo::ValidRandomPos2(CTerritory& territory, const FVector& position, bool sky) {
    L2_GUARD_FUNCTION();
    static const FVector sampleOffset[4]{
        {100.0, 0.0, 0.0},
        {-100.0, 0.0, 0.0},
        {0.0, 100.0, 0.0},
        {0.0, -100.0, 0.0},
    };

    if (!territory.IsInside(position, true)) {
        return false;
    }

    for (const FVector& offset : sampleOffset) {
        FVector sample{
            position.x + offset.x,
            position.y + offset.y,
            position.z + offset.z,
        };
        if (!sky) {
            sample.z = static_cast<double>(
#if defined(L2_BUILD_L2SERVER)
                ::CWorld::Inst()
                    .
#else
                l2geo::CWorld::Inst()
                    .
#endif
                GetGeo()
                    ->FindGround(sample)
            );
        }
#if defined(L2_BUILD_L2SERVER)
        if (!::CWorld::Inst()
                .
#else
        if (!l2geo::CWorld::Inst()
                .
#endif
            GetGeo()
                ->MoveStraightTest(position, sample, 100.0, false, sky ? 1 : 0)) {
            return false;
        }
    }
#if defined(L2_BUILD_L2SERVER)
    return ::CWorld::Inst()
        .
#else
    return l2geo::CWorld::Inst()
        .
#endif
        GetGeo()
        ->IsOpen(position);
}

FVector TerritoryInfo::PickRandomPos(bool sky) {
    L2_GUARD_FUNCTION();
    if (!mRandomPosGenerated) {
        const std::string name = Utf16ToUtf8(GetFirstTerritoryName());
        // Log.Add(
        //     LOG_ERROR,
        //     L"it's not random generated pos [%s]",
        //     name.c_str())
        // Log.Add(LOG_ERROR,
        //     "[spawn] Random positions have not been initialized for territory '%s'. Returning (0, 0, 0).",
        //     WideToUtf8(name).c_str())
        L2_LOG_ERROR(
            Log, "[spawn] Random positions have not been initialized for territory '%s'; returning (0, 0, 0).",
            name.c_str()
        );
        return {};
    }

    std::vector<FVector>& positions = sky ? mRandomPosWithSkyList : mRandomPosList;
    if (positions.empty()) {
        const std::string name = Utf16ToUtf8(GetFirstTerritoryName());
        // Log.Add(
        //     LOG_ERROR,
        //     sky
        //         ? L"picking random pos with sky from "
        //           L"null random pos [%s]"
        //         : L"picking random pos from null "
        //           L"random pos [%s]",
        //     name.c_str())
        // Log.Add(LOG_ERROR,
        //     "[spawn] No %s positions are available for territory '%s'. Returning (0, 0, 0).",
        //     sky ? "airborne" : "ground", WideToUtf8(name).c_str())
        L2_LOG_ERROR(
            Log, "[spawn] No %s positions are available for territory '%s'; returning (0, 0, 0).",
            sky ? "airborne" : "ground", name.c_str()
        );
        return {};
    }
    return runtime::CheckedVectorAt(positions, RandomIndex(positions.size()));
}

bool TerritoryInfo::IsInBannedTerritory(const FVector& position, bool includeZ) {
    L2_GUARD_FUNCTION();
    if (mBannedTerritoryList.empty()) {
        return false;
    }
    auto current = mBannedTerritoryList.begin();
    while (current != mBannedTerritoryList.end()) {
        if ((*current)->IsInside(position, includeZ)) {
            return true;
        }
        ++current;
    }
    return false;
}

bool TerritoryInfo::IsInTerritory(const FVector& position, bool includeZ) {
    L2_GUARD_FUNCTION();
    if (IsInBannedTerritory(position, includeZ)) {
        return false;
    }
    for (std::size_t index = 0; index < mTerritoryList.size(); ++index) {
        if (runtime::CheckedVectorAt(mTerritoryList, index)->IsInside(position, includeZ)) {
            return true;
        }
    }
    return false;
}

bool TerritoryInfo::IsInTerritoryWithLogVisitor(const FVector& position, ZLogVisitor& visitor, bool includeZ) {
    L2_GUARD_FUNCTION();
    if (IsInBannedTerritory(position, includeZ)) {
        const std::string name = Utf16ToUtf8(GetFirstTerritoryName());
        // Log.Add(
        //     LOG_ERROR,
        //     L"NPC Wanted to spawn in Banned Territory. "
        //     L"[%s] [%d %d %d]",
        //     name.c_str(),
        //     runtime::Win64CvttDoubleToInt32(position.x),
        //     runtime::Win64CvttDoubleToInt32(position.y),
        //     runtime::Win64CvttDoubleToInt32(position.z))
        // Log.Add(LOG_ERROR,
        //     "[spawn] Rejected position (%d, %d, %d) for territory '%s': it lies in a banned area.",
        //     runtime::Win64CvttDoubleToInt32(position.x),
        //     runtime::Win64CvttDoubleToInt32(position.y),
        //     runtime::Win64CvttDoubleToInt32(position.z), WideToUtf8(name).c_str())
        L2_LOG_ERROR(
            Log, "[spawn] Rejected position (%d, %d, %d) for territory '%s': it lies in a banned area.",
            runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
            runtime::Win64CvttDoubleToInt32(position.z), name.c_str()
        );
        return false;
    }

    int reason = 0;
    for (std::size_t index = 0; index < mTerritoryList.size(); ++index) {
        if (runtime::CheckedVectorAt(mTerritoryList, index)->IsInsideWithLogVisitor(position, visitor, includeZ, reason)) {
            return true;
        }
    }

    const std::string name = Utf16ToUtf8(GetLastTerritoryName());
    const int x = runtime::Win64CvttDoubleToInt32(position.x);
    const int y = runtime::Win64CvttDoubleToInt32(position.y);
    const int z = runtime::Win64CvttDoubleToInt32(position.z);
    if (reason == 2 || reason == 3) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"IsInTerritoryWithLogVisitor. "
        //     L"MakerLog(Territory: %s (%d, %d, %d)) "
        //     L"Set MaxZ or MinZ Failed [%d]",
        //     name.c_str(),
        //     x,
        //     y,
        //     z,
        //     reason)
        // Log.Add(LOG_ERROR,
        //     "[spawn] Height check failed for position (%d, %d, %d) in territory '%s' (reason %d).",
        //     x, y, z, WideToUtf8(name).c_str(), reason)
        L2_LOG_ERROR(
            Log, "[spawn] Height check failed for position (%d, %d, %d) in territory '%s' (reason %d).", x, y, z,
            name.c_str(), reason
        );
    } else if (reason == 4) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s] [%d/%d/%d] in not in Territory",
        //     name.c_str(),
        //     x,
        //     y,
        //     z)
        // Log.Add(LOG_ERROR,
        //     "[spawn] Position (%d, %d, %d) is outside territory '%s'.",
        //     x, y, z, WideToUtf8(name).c_str())
        L2_LOG_ERROR(Log, "[spawn] Position (%d, %d, %d) is outside territory '%s'.", x, y, z, name.c_str());
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s] [%d/%d/%d] in not in Territory. "
        //     L"reason[%d]",
        //     name.c_str(),
        //     x,
        //     y,
        //     z,
        //     reason)
        // Log.Add(LOG_ERROR,
        //     "[spawn] Rejected position (%d, %d, %d) for territory '%s' (reason %d).",
        //     x, y, z, WideToUtf8(name).c_str(), reason)
        L2_LOG_ERROR(
            Log, "[spawn] Rejected position (%d, %d, %d) for territory '%s' (reason %d).", x, y, z, name.c_str(),
            reason
        );
    }
    visitor.SetEnable(true);
    return false;
}

bool TerritoryInfo::ValidRandomPos1(CTerritory* territory, const FVector& position) {
    L2_GUARD_FUNCTION();
    return !IsInBannedTerritory(position, true) && territory->IsInside(position, true);
}

bool TerritoryInfo::ValidRandomPos(CTerritory* territory, const FVector& position, bool sky) {
    L2_GUARD_FUNCTION();
    return ValidRandomPos1(territory, position) && ValidRandomPos2(*territory, position, sky);
}

bool TerritoryInfo::InitializeRandomPos(bool sky, int sampleTarget) {
    L2_GUARD_FUNCTION();
    int density = std::max(400, sampleTarget);
    if (density > 400) {
        density = runtime::Int32FromBits(static_cast<std::uint32_t>(density) * 4U);
    }

    if (mTerritoryList.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"keep npcmaker from creating npc for no "
        //     L"territory at file[%s], line[%d]",
        //     "..\\Shared\\TerritoryInfo.cpp",
        //     0x125)
        // Log.Add(LOG_ERROR,
        //     "[spawn] Cannot initialize random NPC positions: the maker has no territories.")
        L2_LOG_ERROR(Log, "[spawn] Cannot initialize random NPC positions: the maker has no territories.");
        return false;
    }

    auto territoryPosition = mTerritoryList.begin();
    while (territoryPosition != mTerritoryList.end()) {
        CTerritory* const territory = *territoryPosition;
        for (const FVector2D& point : territory->m_TerritoryPoints) {
#if defined(L2_BUILD_L2SERVER)
            if (::CWorld::Inst().
#else
            if (l2geo::CWorld::Inst().
#endif
                IsValidPosition(runtime::Win64CvttDoubleToInt32(point.x), runtime::Win64CvttDoubleToInt32(point.y))) {
                continue;
            }
            const std::string name = Utf16ToUtf8(territory->sName.data());
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid territory point : "
            //     L"[%s] at (%d, %d)",
            //     L"..\\Shared\\TerritoryInfo.cpp",
            //     0x13a,
            //     name.c_str(),
            //     runtime::Win64CvttDoubleToInt32(point.x),
            //     runtime::Win64CvttDoubleToInt32(point.y))
            // Log.Add(LOG_ERROR,
            //     "[spawn] Territory '%s' has a point outside the world at (%d, %d). Random-position initialization
            //     stopped.", WideToUtf8(name).c_str(), runtime::Win64CvttDoubleToInt32(point.x),
            //     runtime::Win64CvttDoubleToInt32(point.y))
            L2_LOG_ERROR(
                Log,
                "[spawn] Territory '%s' has a point outside the world at (%d, %d); random-position initialization stopped.",
                name.c_str(), runtime::Win64CvttDoubleToInt32(point.x),
                runtime::Win64CvttDoubleToInt32(point.y)
            );
            return false;
        }
        ++territoryPosition;
    }

    L2_GUARD(m_RandomPosLock);

    const int stepDivisor = runtime::Win64CvttDoubleToInt32(std::sqrt(static_cast<double>(density)));
    std::vector<FVector>& positions = sky ? mRandomPosWithSkyList : mRandomPosList;
    positions.reserve(static_cast<std::size_t>(density));

    territoryPosition = mTerritoryList.begin();
    while (territoryPosition != mTerritoryList.end()) {
        CTerritory* const territory = *territoryPosition;
        int top = territory->m_Top;
        int bottom = territory->m_Bottom;
        const int east = territory->m_East;
        const int west = territory->m_West;
        const int south = territory->m_South;
        const int north = territory->m_North;
        if (sky) {
            top = std::clamp(top, -500, 0x157c);
            bottom = std::clamp(bottom, -500, 0x157c);
        }

        const int stepX = std::max(1, runtime::SubtractInt32Wrapping(east, west) / stepDivisor);
        const int stepY = std::max(1, runtime::SubtractInt32Wrapping(south, north) / stepDivisor);
        for (int x = west; x < east; x = runtime::AddInt32Wrapping(x, stepX)) {
            for (int y = north; y < south; y = runtime::AddInt32Wrapping(y, stepY)) {
                if (!sky) {
                    FVector candidate{
                        static_cast<double>(x),
                        static_cast<double>(y),
                        static_cast<double>(top),
                    };
                    candidate.z = static_cast<double>(
#if defined(L2_BUILD_L2SERVER)
                        ::CWorld::Inst()
                            .
#else
                        l2geo::CWorld::Inst()
                            .
#endif
                        GetGeo()
                            ->FindGround(candidate)
                    );
                    if (ValidRandomPos(territory, candidate, false)) {
                        positions.push_back(candidate);
                    }
                    continue;
                }

                for (int attempt = 0; attempt < 3; ++attempt) {
                    const FVector candidate{
                        static_cast<double>(x),
                        static_cast<double>(y),
                        static_cast<double>(RandomIntegerInclusive(bottom, top)),
                    };
                    if (ValidRandomPos(territory, candidate, true)) {
                        positions.push_back(candidate);
                        break;
                    }
                }
            }
        }

        if (positions.empty()) {
            const std::wstring name = Utf16ToWide(territory->sName.data());
            // Noise: An empty grid sample is not final failure. Center and random fallback sampling follow. The final
            // empty-position error remains. Log.Add(
            //     LOG_ERROR,
            //     sky
            //         ? L"[%s][%d] Cannot generate random "
            //           L"position sky in territory[%s]"
            //         : L"[%s][%d] Cannot generate random "
            //           L"position in territory[%s]",
            //     L"..\\Shared\\TerritoryInfo.cpp",
            //     sky ? 0x195 : 0x18e,
            //     name.c_str())
        }
        ++territoryPosition;
    }

    int randomAttempt = 0;
    if (!sky) {
        if (mRandomPosList.empty()) {
            territoryPosition = mTerritoryList.begin();
            while (territoryPosition != mTerritoryList.end()) {
                CTerritory* const territory = *territoryPosition;
                FVector center = territory->GetCenterPos();
                center.z = static_cast<double>(
#if defined(L2_BUILD_L2SERVER)
                    ::CWorld::Inst()
                        .
#else
                    l2geo::CWorld::Inst()
                        .
#endif
                    GetGeo()
                        ->FindGround(center)
                );
                if (ValidRandomPos1(territory, center)) {
                    mRandomPosList.push_back(center);
                } else {
                    const std::wstring name = Utf16ToWide(territory->sName.data());
                    // Noise: A rejected center candidate is an expected fallback step. Random sampling follows. The final
                    // empty-position error remains. Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] CenterPos(%d,%d,%d) of "
                    //     L"territory[%s] is not inside",
                    //     L"..\\Shared\\TerritoryInfo.cpp",
                    //     0x1ab,
                    //     runtime::Win64CvttDoubleToInt32(center.x),
                    //     runtime::Win64CvttDoubleToInt32(center.y),
                    //     runtime::Win64CvttDoubleToInt32(center.z),
                    //     name.c_str())
                }
                ++territoryPosition;
            }
            while (mRandomPosList.size() < 0x19 && randomAttempt < 0x32) {
                CTerritory* const territory = runtime::CheckedVectorAt(mTerritoryList, RandomIndex(mTerritoryList.size()));
                const FVector candidate = territory->PickRandomPos(false);
                if (ValidRandomPos1(territory, candidate)) {
                    mRandomPosList.push_back(candidate);
                }
                ++randomAttempt;
            }
        }
        if (mRandomPosList.empty()) {
            CTerritory* const firstTerritory = runtime::CheckedVectorAt(mTerritoryList, 0);
            const std::string name = Utf16ToUtf8(firstTerritory->sName.data());
            // Diagnostic improvement: show the geodata ground at the center
            // beside the territory height range. Most failures are a height
            // range that does not match the geodata, for example a dungeon
            // layer that the geodata does not contain.
            FVector center = firstTerritory->GetCenterPos();
            center.z = static_cast<double>(firstTerritory->m_Top);
            const int centerGround =
#if defined(L2_BUILD_L2SERVER)
                ::CWorld::Inst()
                    .
#else
                l2geo::CWorld::Inst()
                    .
#endif
                GetGeo()
                    ->FindGround(center);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Cannot generate random postion "
            //     L"in npcmaker, 1st territory[%s]",
            //     L"..\\Shared\\TerritoryInfo.cpp",
            //     0x1f6,
            //     name.c_str())
            // Log.Add(LOG_ERROR,
            //     "[spawn] No ground positions remain after fallback sampling. First maker territory is '%s'. Check
            //     territory bounds, banned areas and geodata.", WideToUtf8(name).c_str())
            L2_LOG_ERROR(
                Log,
                "[spawn] No ground positions remain after fallback sampling; first maker territory is '%s' (height "
                "range %d..%d, geodata ground at its center %d). Check territory bounds, banned areas and geodata.",
                name.c_str(), firstTerritory->m_Bottom, firstTerritory->m_Top, centerGround
            );
        }
    } else {
        if (mRandomPosWithSkyList.empty()) {
            territoryPosition = mTerritoryList.begin();
            while (territoryPosition != mTerritoryList.end()) {
                CTerritory* const territory = *territoryPosition;
                FVector center = territory->GetCenterPos();
                for (int attempt = 0; attempt < 3; ++attempt) {
                    center.z = static_cast<double>(RandomIntegerInclusive(territory->m_Bottom, territory->m_Top));
                    if (ValidRandomPos1(territory, center)) {
                        mRandomPosWithSkyList.push_back(center);
                        continue;
                    }
                    const std::wstring name = Utf16ToWide(territory->sName.data());
                    // Noise: Airborne fallback probes multiple heights. Rejecting one candidate does not mean spawning
                    // failed. The final empty-position error remains. Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] CenterPos(%d,%d,%d) "
                    //     L"of territory[%s] sky is not "
                    //     L"inside",
                    //     L"..\\Shared\\TerritoryInfo.cpp",
                    //     0x1d6,
                    //     runtime::Win64CvttDoubleToInt32(center.x),
                    //     runtime::Win64CvttDoubleToInt32(center.y),
                    //     runtime::Win64CvttDoubleToInt32(center.z),
                    //     name.c_str())
                }
                ++territoryPosition;
            }
            while (mRandomPosWithSkyList.size() < 0x19 && randomAttempt < 0x32) {
                CTerritory* const territory = runtime::CheckedVectorAt(mTerritoryList, RandomIndex(mTerritoryList.size()));
                for (int attempt = 0; attempt < 3; ++attempt) {
                    const FVector candidate = territory->PickRandomPos(true);
                    if (ValidRandomPos1(territory, candidate)) {
                        mRandomPosWithSkyList.push_back(candidate);
                    }
                }
                ++randomAttempt;
            }
        }
        if (mRandomPosWithSkyList.empty()) {
            const std::string name = Utf16ToUtf8(runtime::CheckedVectorAt(mTerritoryList, 0)->sName.data());
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Cannot generate random postion "
            //     L"sky in npcmaker, 1st territory[%s]",
            //     L"..\\Shared\\TerritoryInfo.cpp",
            //     0x1fd,
            //     name.c_str())
            // Log.Add(LOG_ERROR,
            //     "[spawn] No airborne positions remain after fallback sampling. First maker territory is '%s'. Check
            //     territory bounds, banned areas and geodata.", WideToUtf8(name).c_str())
            L2_LOG_ERROR(
                Log,
                "[spawn] No airborne positions remain after fallback sampling; first maker territory is '%s'. Check "
                "territory bounds, banned areas and geodata.",
                name.c_str()
            );
        }
    }

    mRandomPosGenerated = true;
    return true;
}
