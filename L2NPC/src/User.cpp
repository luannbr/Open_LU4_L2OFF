#include "L2NPC/src/User.h"

#include "L2Core/src/inc/Guard.h"
#include "L2NPC/src/Config.h"
#include "L2NPC/src/Default.h"
#include "L2NPC/src/NPC.h"
#include "L2NPC/src/NPCEventFactory.h"
#include "L2NPC/src/NpcEvent.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "TextRuntime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <utility>

namespace {

std::size_t Msvc2015ShuffleIndex(std::size_t count) {
    if (count < 2) {
        return 0;
    }
    constexpr std::uint32_t kRandMask = 0x7fff;
    const std::uint32_t targetCount = static_cast<std::uint32_t>(count);
    std::uint32_t covered = 0;
    std::uint32_t value = 0;
    do {
        covered = (covered << 15) | kRandMask;
        value = (value << 15) | static_cast<std::uint32_t>(LegacyCrtRand());
    } while (covered != std::numeric_limits<std::uint32_t>::max() && covered < targetCount);
    return static_cast<std::size_t>(value % targetCount);
}

std::int64_t PackSkillNameId(int skillId, int skillLevel, int skillSubLevel) {
    const auto signedBits = [](int value) { return static_cast<std::uint64_t>(static_cast<std::int64_t>(value)); };
    return static_cast<std::int64_t>(
        ((signedBits(skillId) << 16) | signedBits(skillLevel)) << 16 | signedBits(skillSubLevel)
    );
}

long long UserLogInteger(std::int64_t value) { return static_cast<long long>(value); }

template <std::size_t Size>
void TargetWideStringAppend(std::array<wchar_t, Size>& destination, const wchar_t* source) {
    std::size_t destinationLength = 0;
    while (destinationLength < Size && destination[destinationLength] != L'\0') {
        ++destinationLength;
    }

    std::size_t sourceLength = 0;
    while (source[sourceLength] != L'\0') {
        ++sourceLength;
    }

    if (destinationLength == Size || sourceLength >= Size - destinationLength) {
        destination[0] = L'\0';
        myInvalidParameterHandler(L"(L\"Buffer is too small\" && 0)", L"wcscat_s", nullptr, 0, 0);
        return;
    }

    for (std::size_t index = 0; index <= sourceLength; ++index) {
        destination[destinationLength + index] = source[index];
    }
}

template <std::size_t Size>
void TargetIntToWideDecimal(int value, std::array<wchar_t, Size>& destination) {
    std::array<wchar_t, 11> reversed{};
    std::size_t digitCount = 0;
    const bool negative = value < 0;
    std::uint32_t magnitude = static_cast<std::uint32_t>(value);
    if (negative) {
        magnitude = 0U - magnitude;
    }

    do {
        reversed[digitCount++] = static_cast<wchar_t>(L'0' + magnitude % 10U);
        magnitude /= 10U;
    } while (magnitude != 0);

    const std::size_t characterCount = digitCount + (negative ? 1U : 0U);
    if (characterCount >= Size) {
        destination[0] = L'\0';
        myInvalidParameterHandler(L"(L\"Buffer is too small\" && 0)", L"_itow_s", nullptr, 0, 0);
        return;
    }

    std::size_t output = 0;
    if (negative) {
        destination[output++] = L'-';
    }
    while (digitCount != 0) {
        destination[output++] = reversed[--digitCount];
    }
    destination[output] = L'\0';
}

} // namespace

User::User() : m_nEventHandling(0), m_nBingoBoardSize(0), m_nBingoSelectCount(0), mHTMLCookie(-1) {
    // Current User::User 0x00545098 increments the object-report counter
    // after initializing the complete User suffix.
    g_ServerContext.User.fetch_add(1, std::memory_order_seq_cst);
}

User::~User() {
    L2_GUARD_FUNCTION();
    // Current User::~User 0x00544dd8 decrements this before member cleanup.
    g_ServerContext.User.fetch_sub(1, std::memory_order_seq_cst);
}

User* User::GetObjectW(int objectId) {
    L2_GUARD_FUNCTION();
    CCreature* object = CCreature::GetObjectW(static_cast<std::uint32_t>(objectId));
    return object && object->IsPC() ? object->CastUser() : nullptr;
}

bool User::Set(unsigned int objectId, unsigned int sharedIndex [[maybe_unused]]) {
    L2_GUARD_FUNCTION();
    // Shipping User::Set @ 0x005426a0 stores the packet SmartID first,
    // binds the fixed shared-factory slot selected by its low 20 bits, then
    // calls the two-argument CSmartIdManager::RegisterObject directly.
    // Its second source parameter is present at the call site and in the
    // guard signature but is not read by the optimized body.
    m_nId = objectId;
    m_data = CCreatureFactory::Inst().GetData(static_cast<int>(objectId & 0x000fffffU));
    return g_CreatureSmartIdManager.RegisterObject(objectId, this);
}

bool User::IsPC() const { return true; }

User* User::CastUser() { return this; }

int User::GetCookie(char16_t* key) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0054350c.
    const auto found = m_CookieMap.find(key);
    return found == m_CookieMap.end() ? -1 : found->second;
}

void User::SetCookie(char16_t* key, int value) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00544cdc.
    if (!key || *key == u'\0') {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] void User::SetCookie failed. "
        //     L"pszStrKey is invalid",
        //     L"user.cpp", 0xd5
        // )
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot set a script cookie for player %u: key is %s. Existing cookies unchanged.",
        //     static_cast<unsigned int>(Id()), key ? "empty" : "missing")
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot set a script cookie for player %u: key is %s; existing cookies unchanged.",
            static_cast<unsigned int>(Id()), key ? "empty" : "missing"
        );
        ReportScriptContext(LOG_ERROR);
        return;
    }
    m_CookieMap[key] = value;
}

int User::CreateBingoBoard(int size) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x005445a8.
    if (size % 2 == 0) {
        return 0;
    }

    const int cellCount = runtime::MultiplyInt32Wrapping(size, size);
    m_BingoBoard.reserve(cellCount);
    m_BingoBoard.resize(0);
    m_nBingoBoardSize = size;

    int number = 0;
    while (number < cellCount) {
        ++number;
        m_BingoBoard.push_back(number);
    }
    // Forward Fisher-Yates as instantiated by the target's VS2015
    // std::shuffle, including its rand() range expansion.
    for (std::size_t index = 1; index < m_BingoBoard.size(); ++index) {
        std::swap(
            runtime::CheckedVectorAt(m_BingoBoard, index), runtime::CheckedVectorAt(m_BingoBoard, Msvc2015ShuffleIndex(index + 1))
        );
    }

    const int selectedCount = runtime::AddInt32Wrapping(cellCount, 1);
    m_SelectedBingoNumbers.reserve(static_cast<std::size_t>(static_cast<std::int64_t>(selectedCount)));
    m_SelectedBingoNumbers.resize(0);
    number = 0;
    while (number < selectedCount) {
        m_SelectedBingoNumbers.push_back(0);
        ++number;
    }
    m_nBingoSelectCount = 0;
    return size;
}

int User::GetBingoBoardSize() {
    // Current L2NPC.exe 0x00488a5c.
    L2_GUARD_FUNCTION();
    return m_nBingoBoardSize;
}

int User::GetNumberFromBingoBoard(int index) {
    // Current L2NPC.exe 0x00488aec.
    L2_GUARD_FUNCTION();
    return index >= 0 && index < static_cast<int>(m_BingoBoard.size())
        ? runtime::CheckedVectorAt(m_BingoBoard, static_cast<std::size_t>(index))
        : 0;
}

int User::SelectBingoNumber(int number) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00542118.
    if (number > 0 && number < static_cast<int>(m_SelectedBingoNumbers.size()) &&
        runtime::CheckedVectorAt(m_SelectedBingoNumbers, static_cast<std::size_t>(number)) != 1) {
        runtime::CheckedVectorAt(m_SelectedBingoNumbers, static_cast<std::size_t>(number)) = 1;
        m_nBingoSelectCount = runtime::AddInt32Wrapping(m_nBingoSelectCount, 1);
    }
    return m_nBingoSelectCount;
}

int User::GetBingoSelectCount() {
    // Current L2NPC.exe 0x00488bb0.
    L2_GUARD_FUNCTION();
    return m_nBingoSelectCount;
}

int User::IsSelectedBingoNumber(int number) {
    // Current L2NPC.exe 0x00488c40.
    L2_GUARD_FUNCTION();
    return number > 0 && number < static_cast<int>(m_SelectedBingoNumbers.size())
        ? runtime::CheckedVectorAt(m_SelectedBingoNumbers, static_cast<std::size_t>(number))
        : 0;
}

int User::GetMatchedBingoLineCount() {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x005421ec.
    const int size = m_nBingoBoardSize;
    int lines = 0;
    if (size > 0) {
        std::int64_t rowOffset = 0;
        std::int64_t rowsRemaining = size;
        do {
            // The target snapshots both vector bases once per row.
            int* const selectedBase = m_SelectedBingoNumbers.data();
            int* boardEntry = m_BingoBoard.data() + rowOffset;
            int selectedCount = 0;
            std::int64_t columnsRemaining = size;
            do {
                if (selectedBase[*boardEntry] != 0) {
                    selectedCount = runtime::AddInt32Wrapping(selectedCount, 1);
                }
                ++boardEntry;
                --columnsRemaining;
            } while (columnsRemaining != 0);
            if (selectedCount == size) {
                lines = runtime::AddInt32Wrapping(lines, 1);
            }
            rowOffset += static_cast<std::int64_t>(size);
            --rowsRemaining;
        } while (rowsRemaining != 0);

        std::int64_t columnOffset = 0;
        std::int64_t columnsRemaining = size;
        do {
            // The bases are reloaded for every column, but not for every
            // element within that column.
            int* const selectedBase = m_SelectedBingoNumbers.data();
            int* boardEntry = m_BingoBoard.data() + columnOffset;
            int selectedCount = 0;
            std::int64_t rowsRemaining = size;
            do {
                if (selectedBase[*boardEntry] != 0) {
                    selectedCount = runtime::AddInt32Wrapping(selectedCount, 1);
                }
                boardEntry += size;
                --rowsRemaining;
            } while (rowsRemaining != 0);
            if (selectedCount == size) {
                lines = runtime::AddInt32Wrapping(lines, 1);
            }
            ++columnOffset;
            --columnsRemaining;
        } while (columnsRemaining != 0);
    }

    int selectedCount = 0;
    if (size > 0) {
        int* const selectedBase = m_SelectedBingoNumbers.data();
        int* boardEntry = m_BingoBoard.data();
        const std::int64_t diagonalStride = static_cast<std::int64_t>(runtime::AddInt32Wrapping(size, 1));
        std::int64_t remaining = size;
        do {
            if (selectedBase[*boardEntry] != 0) {
                selectedCount = runtime::AddInt32Wrapping(selectedCount, 1);
            }
            boardEntry += diagonalStride;
            --remaining;
        } while (remaining != 0);
    }
    if (selectedCount == size) {
        lines = runtime::AddInt32Wrapping(lines, 1);
    }

    selectedCount = 0;
    int row = runtime::AddInt32Wrapping(size, -1);
    int column = 0;
    if (row >= 0) {
        int rowOffset = runtime::MultiplyInt32Wrapping(size, row);
        const int negativeSize = runtime::MultiplyInt32Wrapping(size, -1);
        do {
            if (column >= size) {
                break;
            }
            const int boardIndex = runtime::AddInt32Wrapping(rowOffset, column);
            // Unlike the other three scans, the target reloads both bases
            // for every anti-diagonal element.
            const int number = m_BingoBoard.data()[boardIndex];
            if (m_SelectedBingoNumbers.data()[number] != 0) {
                selectedCount = runtime::AddInt32Wrapping(selectedCount, 1);
            }
            rowOffset = runtime::AddInt32Wrapping(rowOffset, negativeSize);
            column = runtime::AddInt32Wrapping(column, 1);
            row = runtime::AddInt32Wrapping(row, -1);
        } while (row >= 0);
    }
    if (selectedCount == size) {
        lines = runtime::AddInt32Wrapping(lines, 1);
    }

    if (size < 5) {
        std::array<wchar_t, 40> boardText{};
        std::array<wchar_t, 20> selectedText{};
        std::array<wchar_t, 2> numberText{};
        bool hasSelectedNumber = false;
        int liveSize = size;
        row = 0;
        if (size > 0) {
            do {
                column = 0;
                if (liveSize > 0) {
                    do {
                        const int boardIndex =
                            runtime::AddInt32Wrapping(runtime::MultiplyInt32Wrapping(liveSize, row), column);
                        const int number = m_BingoBoard.data()[boardIndex];
                        if (boardIndex > 0) {
                            TargetWideStringAppend(boardText, L" ");
                        }
                        TargetIntToWideDecimal(number, numberText);
                        TargetWideStringAppend(boardText, numberText.data());
                        if (m_SelectedBingoNumbers.data()[number] != 0) {
                            if (hasSelectedNumber) {
                                TargetWideStringAppend(selectedText, L" ");
                            } else {
                                hasSelectedNumber = true;
                            }
                            TargetIntToWideDecimal(number, numberText);
                            TargetWideStringAppend(selectedText, numberText.data());
                        }
                        column = runtime::AddInt32Wrapping(column, 1);
                        liveSize = m_nBingoBoardSize;
                    } while (column < liveSize);
                }
                row = runtime::AddInt32Wrapping(row, 1);
                liveSize = m_nBingoBoardSize;
            } while (row < liveSize);
        }

        const int z = runtime::Win64CvttDoubleToInt32(m_data->GetPos().z);
        const int y = runtime::Win64CvttDoubleToInt32(m_data->GetPos().y);
        const int x = runtime::Win64CvttDoubleToInt32(m_data->GetPos().x);
        CSharedCreatureData* const data = m_data;
        const char16_t* const rawName = GetName();
        const int level = data->m_nLevel;
        const int characterClass = data->m_nClass;
        const int sex = data->m_Sex;
        const int race = data->m_Race;
        const int dbId = data->m_nDBID;
        const std::wstring name = Utf16ToWide(std::u16string_view(rawName));
        Log.Add(
            LOG_BOTH,
            u"%I64d,%I64d,,,,%I64d,%I64d,%I64d,%s,%s,,"
            u"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,,,,",
            UserLogInteger(0x140), UserLogInteger(dbId), UserLogInteger(x), UserLogInteger(y), UserLogInteger(z),
            boardText.data(), selectedText.data(), UserLogInteger(race), UserLogInteger(sex), UserLogInteger(characterClass),
            UserLogInteger(level), UserLogInteger(lines), name.c_str()
        );
    }
    return lines;
}

void User::ClearBingoBoard() {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00544720.
    m_BingoBoard.resize(0);
    m_SelectedBingoNumbers.resize(0);
    m_nBingoSelectCount = 0;
    m_nBingoBoardSize = 0;
}

bool User::AddReverseHate(std::uint32_t npcObjectId, int hate) {
    L2_GUARD_FUNCTION();
    CSharedCreatureData* sharedData = m_data;
    if (!sharedData) {
        // Log.Add(LOG_ERROR, L"[%s][%d] m_data is NULL!!!", L"user.cpp", 0x47)
        // Log.Add(LOG_ERROR,
        //     "[NPC combat] Cannot record aggression from NPC %u toward player %u: player shared data is missing. Update
        //     skipped.", static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(Id()))
        L2_LOG_ERROR(
            Log,
            "[NPC combat] Cannot record aggression from NPC %u toward player %u: player shared data is missing; update "
            "skipped.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(Id())
        );
        return false;
    }

    // Shipping 0x00543a10 tests active (+0x34) before is-PC (+0x44)
    // it does not consult m_bIsAlive (+0x48).
    if (!sharedData->m_bIsActive) {
        // Noise: This lookup and the name conversions served only the inactive-player diagnostic below.
        // CNPC* const npc = CNPC::GetObjectW(static_cast<int>(npcObjectId))
        // const std::wstring userName = Utf16ToWide(GetName())
        // const std::wstring npcName = npc ? Utf16ToWide(npc->GetName()) : L""
        // Noise: Late aggression updates can outlive player activity. The existing inactive check discards them without
        // changing gameplay state. Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] user is inactive, "
        //     L"user[%s] npc[%s]",
        //     L"user.cpp", 0x50, userName.c_str(), npcName.c_str()
        // )
        return false;
    }
    if (!sharedData->m_bIsUser) {
        // Log.Add(LOG_ERROR, L"[%s][%d] Invalid user.", L"user.cpp", 0x56)
        // Log.Add(LOG_ERROR,
        //     "[NPC combat] Cannot record aggression from NPC %u toward object %u: shared data does not describe a player.
        //     update skipped.", static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(Id()))
        L2_LOG_ERROR(
            Log,
            "[NPC combat] Cannot record aggression from NPC %u toward object %u: shared data does not describe a player; "
            "update skipped.",
            static_cast<unsigned int>(npcObjectId), static_cast<unsigned int>(Id())
        );
        return false;
    }

    L2_GUARD(m_ReverseHateSect);
    const auto found = m_ReverseHateMap.find(npcObjectId);
    if (found != m_ReverseHateMap.end()) {
        found->second = static_cast<int>(static_cast<std::uint32_t>(found->second) + static_cast<std::uint32_t>(hate));
    } else {
        // The target clears only when an absent key arrives while the map
        // already contains more than twenty entries.
        if (m_ReverseHateMap.size() > 20) {
            m_ReverseHateMap.clear();
        }
        m_ReverseHateMap.emplace(npcObjectId, hate);
    }
    return true;
}

void User::ResetReverseHate() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_ReverseHateSect);
    int processed = 0;
    for (const auto& entry : m_ReverseHateMap) {
        if (processed >= 20) {
            break;
        }
        if (CNPC* const npc = CNPC::GetObjectW(static_cast<int>(entry.first))) {
            npc->RemoveAttackDesire(m_data->m_nId);
        }
        ++processed;
    }
}

void User::ClearReverseHate() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_ReverseHateSect);
    m_ReverseHateMap.clear();
}

void User::SeeCreature(CCreature* creature) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00541fd0 relies on the virtual IsNPC/CastNPC
    // contract and does not add null fallbacks around either object.
    if (creature->IsNPC() && IsVisible()) {
        creature->CastNPC()->SeeCreatureData(m_data);
    }
}

void User::TimerExpired(int) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0054209c has no body beyond the
    // generated call-stack guard.
}

void User::Spelled(CSharedCreatureData* speller, int skillId, int skillLevel, int skillSubLevel) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x005430c4 copies at most twenty map entries into
    // two fixed stack arrays before releasing m_ReverseHateSect.
    std::array<std::uint32_t, 20> npcObjectIds;
    std::array<int, 20> hateValues;
    std::size_t snapshotSize = 0;
    const std::int64_t skillNameId = PackSkillNameId(skillId, skillLevel, skillSubLevel);
    {
        L2_GUARD(m_ReverseHateSect);
        for (const auto& entry : m_ReverseHateMap) {
            if (snapshotSize == npcObjectIds.size()) {
                break;
            }
            npcObjectIds[snapshotSize] = entry.first;
            hateValues[snapshotSize] = entry.second;
            ++snapshotSize;
        }
    }


    for (std::size_t index = 0; index < snapshotSize; ++index) {
        CNPC* const npc = CNPC::GetObjectW(static_cast<int>(npcObjectIds[index]));
        if (!npc) {
            continue;
        }
        CSharedCreatureData* npcData = npc->m_data;
        if (!npcData->m_bIsActive || !npcData->m_bIsAlive || npcData->m_nId == speller->m_nId) {
            continue;
        }
        const FVector source = speller->GetPos();
        const FVector destination = npc->m_data->GetPos();
        const double dx = destination.x - source.x;
        const double dy = destination.y - source.y;
        const double dz = destination.z - source.z;
        if (std::sqrt(dx * dx + dy * dy + dz * dz) > 1000.0) {
            continue;
        }
        npc->SeeSpell(speller, m_data, skillNameId, hateValues[index]);
    }
}

void User::HandleMasterAttackEvent(
    CCreature* target,
    int damage,
    int skillId [[maybe_unused]],
    int skillLevel [[maybe_unused]],
    int skillSubLevel [[maybe_unused]]
) {
    L2_GUARD_FUNCTION();
    CSharedCreatureData* sharedData = m_data;

    for (int index = 0; index < static_cast<int>(std::size(sharedData->m_pSummon)); ++index) {
        CSPointer<CCreature> summon = sharedData->m_pSummon[index].FindObjectSP();
        if (summon && summon->IsNPC()) {
            CNPC* const npc = summon->CastNPC();
            if (npc && npc->m_data->m_bIsActive && npc->m_data->m_SummonType != 6) {
                CNPCEvent* const event =
                    CNPCEventFactory::CreateMasterAttackEvent(npc, sharedData, target->m_data, damage, 0, 0, 0);
                if (!event) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] "
                    //     L"CreateMasterAttackEvent failed",
                    //     L"user.cpp", 0x1c8
                    // )
                    // Log.Add(LOG_ERROR,
                    //     "[NPC AI] Cannot create the master-attack event for summon %u of player %u (summon reference slot
                    //     %d), target %u, damage %d, skipping this delivery.", static_cast<unsigned int>(npc->Id()),
                    //     static_cast<unsigned int>(Id()), index, static_cast<unsigned int>(target->Id()), damage)
                    L2_LOG_ERROR(
                        Log,
                        "[NPC AI] Cannot create the master-attack event for summon %u of player %u (summon reference slot "
                        "%d), target %u, damage %d; skipping this delivery.",
                        static_cast<unsigned int>(npc->Id()), static_cast<unsigned int>(Id()), index,
                        static_cast<unsigned int>(target->Id()), damage
                    );
                } else {
                    npc->HandleEvent(event);
                    L2_RELEASE(event, ObjectRefType::CreateDelete);
                }
            }
        }

        // The target performs this second, independent lookup for every
        // slot even when the CSafePointer2 above resolved the same NPC.
        CNPC* const npc = CNPC::GetObjectW(sharedData->m_nSummonedNpc[index]);
        if (!npc || !npc->m_data->m_bIsActive || npc->m_data->m_SummonType == 6) {
            continue;
        }
        CNPCEvent* const event = CNPCEventFactory::CreateMasterAttackEvent(npc, sharedData, target->m_data, damage, 0, 0, 0);
        if (!event) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] "
            //     L"CreateMasterAttackEvent failed",
            //     L"user.cpp", 0x1d9
            // )
            // Log.Add(LOG_ERROR,
            //     "[NPC AI] Cannot create the master-attack event for summon %u of player %u (summon ID slot %d), target %u,
            //     damage %d, skipping this delivery.", static_cast<unsigned int>(npc->Id()), static_cast<unsigned
            //     int>(Id()), index, static_cast<unsigned int>(target->Id()), damage)
            L2_LOG_ERROR(
                Log,
                "[NPC AI] Cannot create the master-attack event for summon %u of player %u (summon ID slot %d), target %u, "
                "damage %d; skipping this delivery.",
                static_cast<unsigned int>(npc->Id()), static_cast<unsigned int>(Id()), index,
                static_cast<unsigned int>(target->Id()), damage
            );
            continue;
        }
        npc->HandleEvent(event);
        L2_RELEASE(event, ObjectRefType::CreateDelete);
    }
}

void User::HandleMasterAttackedEvent(
    CCreature* attacker,
    int damage,
    int skillId [[maybe_unused]],
    int skillLevel [[maybe_unused]],
    int skillSubLevel [[maybe_unused]]
) {
    L2_GUARD_FUNCTION();
    CSharedCreatureData* sharedData = m_data;

    for (int index = 0; index < static_cast<int>(std::size(sharedData->m_pSummon)); ++index) {
        CSPointer<CCreature> summon = sharedData->m_pSummon[index].FindObjectSP();
        if (summon && summon->IsNPC()) {
            CNPC* const npc = summon->CastNPC();
            if (npc && npc->m_data->m_bIsActive && npc->m_data->m_SummonType != 6) {
                CNPCEvent* const event =
                    CNPCEventFactory::CreateMasterAttackedEvent(npc, attacker->m_data, sharedData, damage, 0, 0, 0);
                if (!event) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] "
                    //     L"CreateMasterAttackedEvent failed",
                    //     L"user.cpp", 0x1f6
                    // )
                    // Log.Add(LOG_ERROR,
                    //     "[NPC AI] Cannot create the master-attacked event for summon %u of player %u (summon reference
                    //     slot %d), attacker %u, damage %d, skipping this delivery.", static_cast<unsigned int>(npc->Id()),
                    //     static_cast<unsigned int>(Id()), index, static_cast<unsigned int>(attacker->Id()), damage)
                    L2_LOG_ERROR(
                        Log,
                        "[NPC AI] Cannot create the master-attacked event for summon %u of player %u (summon reference slot "
                        "%d), attacker %u, damage %d; skipping this delivery.",
                        static_cast<unsigned int>(npc->Id()), static_cast<unsigned int>(Id()), index,
                        static_cast<unsigned int>(attacker->Id()), damage
                    );
                } else {
                    npc->HandleEvent(event);
                    L2_RELEASE(event, ObjectRefType::CreateDelete);
                }
            }
        }

        CNPC* const npc = CNPC::GetObjectW(sharedData->m_nSummonedNpc[index]);
        if (!npc || !npc->m_data->m_bIsActive || npc->m_data->m_SummonType == 6) {
            continue;
        }
        CNPCEvent* const event =
            CNPCEventFactory::CreateMasterAttackedEvent(npc, attacker->m_data, sharedData, damage, 0, 0, 0);
        if (!event) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] "
            //     L"CreateMasterAttackedEvent failed",
            //     L"user.cpp", 0x206
            // )
            // Log.Add(LOG_ERROR,
            //     "[NPC AI] Cannot create the master-attacked event for summon %u of player %u (summon ID slot %d), attacker
            //     %u, damage %d, skipping this delivery.", static_cast<unsigned int>(npc->Id()), static_cast<unsigned
            //     int>(Id()), index, static_cast<unsigned int>(attacker->Id()), damage)
            L2_LOG_ERROR(
                Log,
                "[NPC AI] Cannot create the master-attacked event for summon %u of player %u (summon ID slot %d), attacker "
                "%u, damage %d; skipping this delivery.",
                static_cast<unsigned int>(npc->Id()), static_cast<unsigned int>(Id()), index,
                static_cast<unsigned int>(attacker->Id()), damage
            );
            continue;
        }
        npc->HandleEvent(event);
        L2_RELEASE(event, ObjectRefType::CreateDelete);
    }
}

ReverseHateNPCList::ReverseHateNPCList(std::uint32_t userObjectId) : m_nUserID(static_cast<int>(userObjectId)) {
    L2_GUARD(m_lock);
    m_NPCList.clear();
    m_NPCSet.clear();
}

void ReverseHateNPCList::AddNPC(std::uint32_t npcObjectId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    if (m_NPCSet.find(npcObjectId) == m_NPCSet.end()) {
        int count = static_cast<int>(m_NPCList.size());
        while (g_nMaxChaseCount <= count) {
            const std::uint32_t evictedNpc = m_NPCList.at(static_cast<std::size_t>(count - 1));
            if (CNPC* npc = CNPC::GetObjectW(static_cast<int>(evictedNpc))) {
                npc->RemoveAttackDesireOnly(m_nUserID);
                npc->StopMove();
            }
            m_NPCList.pop_back();
            const auto found = m_NPCSet.find(evictedNpc);
            if (found != m_NPCSet.end()) {
                m_NPCSet.erase(found);
            }
            --count;
        }

        m_NPCList.push_back(npcObjectId);
        m_NPCSet.insert(npcObjectId);
    }
}

void ReverseHateNPCList::DeleteNPC(std::uint32_t npcObjectId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    const auto setFound = m_NPCSet.find(npcObjectId);
    if (setFound != m_NPCSet.end()) {
        const auto listFound = std::find(m_NPCList.begin(), m_NPCList.end(), npcObjectId);
        if (listFound != m_NPCList.end()) {
            m_NPCList.erase(listFound);
            m_NPCSet.erase(setFound);
        }
    }
}

void ReverseHateNPCList::DeleteAll() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    m_NPCList.clear();
    m_NPCSet.clear();
}

ReverseHateManager::ReverseHateManager() {
    L2_GUARD(m_lock);
    m_ReverseHateMap.clear();
}

ReverseHateManager& ReverseHateManager::inst() {
    L2_GUARD_FUNCTION();
    static ReverseHateManager obj;
    return obj;
}

void ReverseHateManager::Init() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    m_ReverseHateMap.clear();
}

void ReverseHateManager::AddHate(std::uint32_t userObjectId, std::uint32_t npcObjectId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    auto found = m_ReverseHateMap.find(userObjectId);
    if (found == m_ReverseHateMap.end()) {
        auto* const list = new ReverseHateNPCList(userObjectId);
        // Shipping 0x00544a4c keeps the freshly created
        // MemoryObject at user.cpp:0x22d before publishing the raw
        // pointer. ResetHate performs exactly one matching Release.
        L2_ADD_REF(list, ObjectRefType::CreateDelete);
        list->AddNPC(npcObjectId);
        m_ReverseHateMap.emplace(userObjectId, list);
    } else {
        found->second->AddNPC(npcObjectId);
    }
}

void ReverseHateManager::DeleteHate(std::uint32_t userObjectId, std::uint32_t npcObjectId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    const auto found = m_ReverseHateMap.find(userObjectId);
    if (found != m_ReverseHateMap.end()) {
        found->second->DeleteNPC(npcObjectId);
    }
}

void ReverseHateManager::ResetHate(std::uint32_t userObjectId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    const auto found = m_ReverseHateMap.find(userObjectId);
    if (found != m_ReverseHateMap.end()) {
        ReverseHateNPCList* const list = found->second;
        list->DeleteAll();
        m_ReverseHateMap.erase(found);
        L2_RELEASE(list, ObjectRefType::CreateDelete);
    }
}
