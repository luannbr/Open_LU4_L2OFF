#include "L2NPC/src/GlobalObject.h"
#include "Protocol/src/L2Constants.h"


#include "L2NPC/src/AIManager.h"
#include "L2NPC/src/CScriptEventHelperClass.h"
#include "L2NPC/src/Config.h"
#include "L2NPC/src/DBNPC.h"
#include "L2NPC/src/Default.h"
#include "L2NPC/src/MakerScriptEventHelper.h"
#include "L2NPC/src/NPC.h"
#include "L2NPC/src/NPCServer.h"
#include "L2NPC/src/NpcMakerDB.h"
#include "L2NPC/src/NpcMakerEx.h"
#include "L2NPC/src/NpcSpawnDefineEx.h"
#include "L2NPC/src/SSQNPCServer.h"
#include "L2NPC/src/ScriptEngine.h"
#include "L2NPC/src/SuperPoint.h"
#include "L2NPC/src/User.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Runtime.h"
#include "Shared/src/AtomicJob.h"
#include "Shared/src/CategoryDataDB.h"
#include "Shared/src/Exception.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/IOC.h"
#include "Shared/src/Inventory.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/PledgeType.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "Shared/src/ResidenceData.h"
#include "Shared/src/ShareInfo.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"
#include "Shared/src/World.h"
#include "TextRuntime.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <limits>
#include <new>
#include <utility>

namespace {

char16_t* EmptyScriptString() {
    static char16_t empty[1]{};
    return empty;
}

char16_t* FormatScriptInteger(std::int64_t value, bool fstring) {
    char16_t* const output = AllocTmpString(0x14);
    if (!output) {
        return EmptyScriptString();
    }

    std::array<char, 20> ascii{};
    const std::int32_t narrowed = static_cast<std::int32_t>(static_cast<std::uint32_t>(value));
    if (fstring) {
        std::snprintf(ascii.data(), ascii.size(), "#%d", narrowed);
    } else {
        std::snprintf(ascii.data(), ascii.size(), "%d", narrowed);
    }
    std::size_t index = 0;
    while (index + 1 < 0x14 && ascii[index] != '\0') {
        output[index] = static_cast<unsigned char>(ascii[index]);
        ++index;
    }
    output[index] = u'\0';
    return output;
}

long long LogInteger(std::int64_t value) {
    // MSVC's %I64d consumes a signed 64-bit integer. The portability
    // boundary translates it to %lld on POSIX hosts.
    return static_cast<long long>(value);
}


std::int32_t SignedHighDword(std::int64_t value) {
    const std::uint32_t bits = static_cast<std::uint32_t>(static_cast<std::uint64_t>(value) >> 32);
    return runtime::Int32FromBits(bits);
}

std::int64_t ParseTargetInt64(const char16_t* value) {
    // The target calls _wcstoi64(value, nullptr, 10). wchar_t is 16-bit in
    // that image. Widen each UTF-16 code unit for hosts whose wchar_t is
    // 32-bit and let the C runtime keep the conversion/overflow behavior.
    std::wstring widened;
    while (*value != u'\0') {
        widened.push_back(static_cast<wchar_t>(*value++));
    }
    return static_cast<std::int64_t>(std::wcstoll(widened.c_str(), nullptr, 10));
}

template <typename>
inline constexpr bool kUnsupportedScriptType = false;

template <typename T>
constexpr ScriptTypeId ScriptTypeIdFor() {
    using Value = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_same_v<Value, void>) {
        return ScriptTypeId::Void;
    } else if constexpr (std::is_same_v<Value, bool> || std::is_same_v<Value, int> || std::is_enum_v<Value>) {
        return ScriptTypeId::Int;
    } else if constexpr (std::is_same_v<Value, float>) {
        return ScriptTypeId::Float;
    } else if constexpr (std::is_same_v<Value, double>) {
        return ScriptTypeId::Double;
    } else if constexpr (std::is_same_v<Value, std::int64_t>) {
        return ScriptTypeId::Int64;
    } else if constexpr (std::is_pointer_v<Value>) {
        using Pointee = std::remove_cv_t<std::remove_pointer_t<Value>>;
        if constexpr (std::is_same_v<Pointee, char16_t>) {
            return ScriptTypeId::String;
        } else if constexpr (std::is_same_v<Pointee, void>) {
            return ScriptTypeId::Pointer;
        } else if constexpr (std::is_same_v<Pointee, CSharedCreatureData>) {
            return ScriptTypeId::CreatureData;
        } else if constexpr (std::is_same_v<Pointee, CSharedPartyData>) {
            return ScriptTypeId::PartyData;
        } else if constexpr (std::is_same_v<Pointee, CSharedGeneralObjectData>) {
            return ScriptTypeId::MpccData;
        } else if constexpr (std::is_same_v<Pointee, l2npc::NpcMakerEx>) {
            return ScriptTypeId::NpcMaker;
        } else if constexpr (std::is_same_v<Pointee, CNPC>) {
            return ScriptTypeId::Npc;
        } else if constexpr (std::is_same_v<Pointee, CodeInfoList>) {
            return ScriptTypeId::CodeInfoList;
        } else if constexpr (std::is_same_v<Pointee, RoomInfoList>) {
            return ScriptTypeId::RoomInfoList;
        } else if constexpr (std::is_same_v<Pointee, Position>) {
            return ScriptTypeId::Position;
        } else {
            static_assert(kUnsupportedScriptType<Pointee>, "unmapped GlobalObject script type");
        }
    } else {
        static_assert(kUnsupportedScriptType<Value>, "unmapped GlobalObject script type");
    }
}

template <typename T>
ScriptTypeInfo* ScriptTypeFor() {
    return &GetBuiltinScriptType(ScriptTypeIdFor<T>());
}

template <typename MemberPointer, std::size_t... Indices>
void AddGlobalFunctionImpl(
    ScriptTypeInfo* typeInfo, const wchar_t* name, MemberPointer function, std::index_sequence<Indices...>
) {
    using Traits = script_type_detail::MemberFunctionTraits<MemberPointer>;
    using Arguments = typename Traits::ArgumentTuple;
    typeInfo->AddFunction(
        name, function, ScriptTypeFor<typename Traits::ReturnType>(),
        {
            ScriptTypeFor<std::tuple_element_t<Indices, Arguments>>()...,
        }
    );
}

template <typename MemberPointer>
void AddGlobalFunction(ScriptTypeInfo* typeInfo, const wchar_t* name, MemberPointer function) {
    using Arguments = typename script_type_detail::MemberFunctionTraits<MemberPointer>::ArgumentTuple;
    AddGlobalFunctionImpl(typeInfo, name, function, std::make_index_sequence<std::tuple_size_v<Arguments>>{});
}

std::int32_t TargetInt32(std::uint32_t bits) noexcept {
    std::int32_t value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

TimeValue CurrentUnixTimeValue() {
    const auto microseconds =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::int64_t seconds = microseconds / 1000000;
    std::int64_t remainder = microseconds % 1000000;
    if (remainder < 0) {
        --seconds;
        remainder += 1000000;
    }
    return TimeValue(TargetInt32(static_cast<std::uint32_t>(seconds)), static_cast<std::int32_t>(remainder));
}

std::int64_t DaysFromCivil(int year, unsigned int month, unsigned int day) noexcept {
    // Proleptic Gregorian conversion used only as the portable transport for
    // GetLocalTime -> SystemTimeToFileTime in GlobalObject::IsJackpotTime.
    year -= month <= 2U ? 1 : 0;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned int yearOfEra = static_cast<unsigned int>(year - era * 400);
    const unsigned int adjustedMonth = month > 2U ? month - 3U : month + 9U;
    const unsigned int dayOfYear = (153U * adjustedMonth + 2U) / 5U + day - 1U;
    const unsigned int dayOfEra = yearOfEra * 365U + yearOfEra / 4U - yearOfEra / 100U + dayOfYear;
    return static_cast<std::int64_t>(era) * 146097 + static_cast<std::int64_t>(dayOfEra) - 719468;
}

TimeValue CurrentLocalWallClockTimeValue() {
    const SYSTEMTIME local = runtime::crt::LocalSystemTimeNow<SYSTEMTIME>();
    const std::int64_t seconds = DaysFromCivil(local.wYear, local.wMonth, local.wDay) * 86400 +
        static_cast<std::int64_t>(local.wHour) * 3600 + static_cast<std::int64_t>(local.wMinute) * 60 + local.wSecond;
    return TimeValue(
        TargetInt32(static_cast<std::uint32_t>(seconds)), static_cast<std::int32_t>(local.wMilliseconds) * 1000
    );
}

} // namespace

// Current L2NPC.exe 0x004432c8. The bundled donor PDB does not name this
// wrapper, while the native registration and its direct manager call identify
// the script-visible owner and meaning.
int GlobalObject::Superpoint_GetMoveType(const char16_t* superPointName, int npcClassId) {
    return l2geo::CSuperPointInfoManager::Inst().GetMoveType(std::u16string(superPointName), npcClassId);
}

void GlobalObject::AddScriptLog(int level, char16_t* text) {
    L2_GUARD_FUNCTION();
    if (level != 2 && (level != 3 || !g_bScriptDebugLog)) {
        return;
    }

    const int threadId = CThreadManager::GetThreadID();
    const ScriptContext& context = g_ScriptContext[threadId][0];
    const wchar_t* handler = L"";
    const wchar_t* event = L"";
    if (context.bSet) {
        handler = context.pszHandlerName;
        event = context.pszEventName;
    }
    const std::wstring message = text ? Utf16ToWide(std::u16string_view(text)) : std::wstring{};
    // Log.Add(
    //     LOG_ERROR,
    //     L"[Script Log][%s][%s] %s",
    //     handler,
    //     event,
    //     message.c_str())
    if (context.bSet) {
        // Log.Add(level == 3 ? LOG_DEBUG : LOG_ERROR,
        //     "[script] AI '%s', callback '%s': %s",
        //     handler ? WideToUtf8(handler).c_str() : "<unknown>",
        //     event ? WideToUtf8(event).c_str() : "<unknown>",
        //     WideToUtf8(message).c_str())
        L2_LOG(
            Log, level == 3 ? LOG_DEBUG : LOG_ERROR, "[script] AI '%s', callback '%s': %s",
            handler ? WideToUtf8(handler).c_str() : "<unknown>", event ? WideToUtf8(event).c_str() : "<unknown>",
            WideToUtf8(message).c_str()
        );
    } else {
        // Log.Add(level == 3 ? LOG_DEBUG : LOG_ERROR,
        //     "[script] %s", WideToUtf8(message).c_str())
        L2_LOG(Log, level == 3 ? LOG_DEBUG : LOG_ERROR, "[script] %s", WideToUtf8(message).c_str());
    }
}

int GlobalObject::GetDateTime(int timestamp, int field) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043e994 constructs the same TimeValue/DateTime
    // pair before selecting one of DateTime's eight fixed fields.
    TimeValue selected(0, 0);
    DateTime dateTime;
    if (timestamp > 0) {
        selected.Sec(timestamp);
    } else if (timestamp == -1) {
        const SharedRaidServerData* const raidServerData = SharedRaidServerData::GetInfo();
        const int hourDifference = raidServerData->m_nRaidServerTimeDiff;
        selected = CurrentUnixTimeValue();
        if (hourDifference != 0) {
            const std::int32_t offset = TargetInt32(static_cast<std::uint32_t>(hourDifference) * UINT32_C(3600));
            selected += TimeValue(offset, 0);
        }
    } else {
        selected = CurrentUnixTimeValue();
    }

    dateTime.Update(selected);
    switch (field) {
        case 0:
            return dateTime.Year();
        case 1:
            return dateTime.Month();
        case 2:
            return dateTime.Day();
        case 3:
            return dateTime.Hour();
        case 4:
            return dateTime.Minute();
        case 5:
            return dateTime.Second();
        case 6:
            return dateTime.Weekday();
        default:
            return 0;
    }
}

int GlobalObject::GetItemCollectable(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00436090.
    return creature && creature->m_bItemCollectable ? 1 : 0;
}

int GlobalObject::HaveMemo(CSharedCreatureData* creature, int questId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x004376f8 scans exactly 41 normal memo slots.
    if (!creature) {
        return 0;
    }
    for (const MemoStruct& memo : creature->m_Memo) {
        if (memo.nQuestID == questId) {
            return 1;
        }
    }
    return 0;
}

int GlobalObject::GetMemoState(CSharedCreatureData* creature, int questId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x004377bc calls HaveMemo and then repeats the scan.
    if (HaveMemo(creature, questId) == 0) {
        return -1;
    }
    for (const MemoStruct& memo : creature->m_Memo) {
        if (memo.nQuestID == questId) {
            return memo.nState;
        }
    }
    return -1;
}

int GlobalObject::HaveNRMemo(CSharedCreatureData* creature, int questId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x004378a0 scans exactly 20 no-repeat memo slots.
    if (!creature) {
        return 0;
    }
    for (const MemoStruct& memo : creature->m_NRMemo) {
        if (memo.nQuestID == questId) {
            return 1;
        }
    }
    return 0;
}

int GlobalObject::GetNRMemoState(CSharedCreatureData* creature, int questId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00437964.
    if (HaveNRMemo(creature, questId) == 0) {
        return -1;
    }
    for (const MemoStruct& memo : creature->m_NRMemo) {
        if (memo.nQuestID == questId) {
            return memo.nState;
        }
    }
    return -1;
}

int GlobalObject::GetMemoStateEx(CSharedCreatureData* creature, int questId, int slot) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043f2d4. An invalid slot is reported only after
    // the requested memo has been found.
    if (HaveMemo(creature, questId) == 0) {
        return -1;
    }
    for (const MemoStruct& memo : creature->m_Memo) {
        if (memo.nQuestID != questId) {
            continue;
        }
        switch (slot) {
            case 0:
                return memo.nState;
            case 1:
                return memo.nState2;
            case 2:
                return memo.nState3;
            case 3:
                return memo.nState4;
            default:
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] GetMemoStateEx, invalid slot[%d]",
                //     L"GlobalObject.cpp",
                //     0x7d3,
                //     slot)
                // Log.Add(LOG_ERROR,
                //     "[NPC quest] Cannot read quest %d state for creature %u: slot %d is outside 0..3. Returning -1.",
                //     questId, static_cast<unsigned int>(creature->m_nId), slot)
                L2_LOG_ERROR(
                    Log, "[NPC quest] Cannot read quest %d state for creature %u: slot %d is outside 0..3; returning -1.",
                    questId, static_cast<unsigned int>(creature->m_nId), slot
                );
                break;
        }
    }
    return -1;
}

int GlobalObject::GetNRMemoStateEx(CSharedCreatureData* creature, int questId, int slot) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043f45c.
    if (HaveNRMemo(creature, questId) == 0) {
        return -1;
    }
    for (const MemoStruct& memo : creature->m_NRMemo) {
        if (memo.nQuestID != questId) {
            continue;
        }
        switch (slot) {
            case 0:
                return memo.nState;
            case 1:
                return memo.nState2;
            case 2:
                return memo.nState3;
            case 3:
                return memo.nState4;
            default:
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] GetNRMemoStateEx, invalid slot[%d]",
                //     L"GlobalObject.cpp",
                //     0x816,
                //     slot)
                // Log.Add(LOG_ERROR,
                //     "[NPC quest] Cannot read non-repeatable quest %d state for creature %u: slot %d is outside 0..3.
                //     returning -1.", questId, static_cast<unsigned int>(creature->m_nId), slot)
                L2_LOG_ERROR(
                    Log,
                    "[NPC quest] Cannot read non-repeatable quest %d state for creature %u: slot %d is outside 0..3; "
                    "returning -1.",
                    questId, static_cast<unsigned int>(creature->m_nId), slot
                );
                break;
        }
    }
    return -1;
}

int GlobalObject::OwnItemEnchantCount(CSharedCreatureData* creature, int classId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00444e54.
    if (!creature || classId == 0) {
        return 0;
    }
    CInventory inventory;
    inventory.SetIndex(creature->index, creature->m_bIsUser);
    L2_READ_GUARD(inventory);
    int enchant = 0;
    CSharedItemData* item = inventory.GetFirst();
    while (item) {
        CSharedItemData* const current = item;
        item = current->Next();
        if (!current->m_bIsVariation && current->m_nClassID == classId) {
            enchant = current->GetEnchanted();
        }
    }
    return enchant;
}

std::int64_t GlobalObject::OwnItemCount(CSharedCreatureData* creature, int classId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00444fb0 excludes variation items.
    if (!creature || classId == 0) {
        return 0;
    }
    CInventory inventory;
    inventory.SetIndex(creature->index, creature->m_bIsUser);
    L2_READ_GUARD(inventory);
    std::uint64_t count = 0;
    CSharedItemData* item = inventory.GetFirst();
    while (item) {
        CSharedItemData* const current = item;
        item = current->Next();
        if (current->m_bIsVariation || current->m_nClassID != classId) {
            continue;
        }
        count += current->IsStackable() ? static_cast<std::uint64_t>(current->m_nCount) : UINT64_C(1);
    }
    std::int64_t result = 0;
    std::memcpy(&result, &count, sizeof(result));
    return result;
}

std::int64_t GlobalObject::OwnItemCount2(CSharedCreatureData* creature, int classId) {
    L2_GUARD_FUNCTION();
    // Current-only L2NPC.exe 0x00445128 includes variation items.
    if (!creature || classId == 0) {
        return 0;
    }
    CInventory inventory;
    inventory.SetIndex(creature->index, creature->m_bIsUser);
    L2_READ_GUARD(inventory);
    std::uint64_t count = 0;
    CSharedItemData* item = inventory.GetFirst();
    while (item) {
        CSharedItemData* const current = item;
        item = current->Next();
        if (current->m_nClassID != classId) {
            continue;
        }
        count += current->IsStackable() ? static_cast<std::uint64_t>(current->m_nCount) : UINT64_C(1);
    }
    std::int64_t result = 0;
    std::memcpy(&result, &count, sizeof(result));
    return result;
}

int GlobalObject::IsVariationItem(CSharedCreatureData* creature, int classId) {
    L2_GUARD_FUNCTION();
    // Current-only L2NPC.exe 0x00445294.
    if (!creature || classId == 0) {
        return -1;
    }
    CInventory inventory;
    inventory.SetIndex(creature->index, creature->m_bIsUser);
    L2_READ_GUARD(inventory);
    if (inventory.GetItemAmount(classId, -1, false) != 1) {
        return -1;
    }
    for (CSharedItemData* item = inventory.GetFirst(); item; item = item->Next()) {
        if (item->m_nClassID == classId) {
            return item->m_bIsVariation ? 1 : 0;
        }
    }
    return -1;
}

int GlobalObject::IsEnchantedItem(CSharedCreatureData* creature, int classId) {
    L2_GUARD_FUNCTION();
    // Current-only L2NPC.exe 0x00445430.
    if (!creature || classId == 0) {
        return -1;
    }
    CInventory inventory;
    inventory.SetIndex(creature->index, creature->m_bIsUser);
    L2_READ_GUARD(inventory);
    if (inventory.GetItemAmount(classId, -1, true) != 1) {
        return -1;
    }
    for (CSharedItemData* item = inventory.GetFirst(); item; item = item->Next()) {
        if (item->m_nClassID == classId) {
            return item->GetEnchanted() > 0 ? 1 : 0;
        }
    }
    return -1;
}

bool GlobalObject::IsParticipantInDynamicContent(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current-only L2NPC.exe 0x00439d58.
    return creature && creature->m_OnGoingDynamicContent > 0;
}

int GlobalObject::GetElapsedTimeFromLastLogout(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current-only L2NPC.exe 0x0043ca14.
    if (!creature || !creature->m_bIsUser) {
        return -1;
    }
    const std::time_t hostNow = std::time(nullptr);
    const std::int32_t now = hostNow == static_cast<std::time_t>(-1) ||
            (std::numeric_limits<std::time_t>::is_signed && hostNow < static_cast<std::time_t>(0)) ||
            hostNow > static_cast<std::time_t>(std::numeric_limits<std::int32_t>::max())
        ? -1
        : static_cast<std::int32_t>(hostNow);
    const int lastLogout = creature->GetAccountLastLogoutTime();
    if (lastLogout == 0) {
        return -1;
    }
    return runtime::Int32FromBits(static_cast<std::uint32_t>(now) - static_cast<std::uint32_t>(lastLogout));
}

int GlobalObject::GetSex(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current-only L2NPC.exe 0x0043cac4.
    return creature ? creature->m_Sex : 0;
}

bool GlobalObject::IsWeakenedDeathPenalty(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current-only L2NPC.exe 0x0043cb5c.
    return creature && creature->m_bWeakenedDeathPenalty;
}

bool GlobalObject::IsCreateDate(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043a940, including its historical leap-year rule.
    if (!creature) {
        return false;
    }

    const SYSTEMTIME current = runtime::crt::LocalSystemTimeNow<SYSTEMTIME>();
    const SYSTEMTIME& created = creature->m_tCreateDate;
    if (created.wMonth == 2 && created.wDay == 29) {
        const bool leapYear = (current.wYear & 3U) == 0U && current.wYear % 100U != 0U;
        return current.wMonth == 2 && current.wDay == (leapYear ? 29 : 28) && created.wYear < current.wYear;
    }
    return current.wMonth == created.wMonth && current.wDay == created.wDay && created.wYear < current.wYear;
}

bool GlobalObject::CanGetBirthdayGift(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043ab58 obtains the comparison date before its
    // nested IsCreateDate call.
    if (!creature) {
        return false;
    }
    SYSTEMTIME current = runtime::crt::LocalSystemTimeNow<SYSTEMTIME>();
    if (!IsCreateDate(creature)) {
        return false;
    }
    bool canGet = GetDayDiff(&current, &creature->m_tGetGiftDate) > 0;
    if (creature->m_tGetGiftDate.wYear == 0) {
        canGet = true;
    }
    return canGet;
}

void GlobalObject::SaveGetBirthdayGiftTime(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043aa98: NPCServer::Send("cd", 0xc0, SID).
    if (!creature) {
        return;
    }
    g_ns.Send("cd", 0xc0, creature->m_nId);
}

int GlobalObject::GetBirthdayGiftCount(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043ac48.
    return creature ? creature->m_nGetBirthdayGiftCount : 0;
}

bool GlobalObject::Is5Yeardate(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043a708.
    if (!creature) {
        return false;
    }

    const SYSTEMTIME current = runtime::crt::LocalSystemTimeNow<SYSTEMTIME>();
    const SYSTEMTIME& created = creature->m_tCreateDate;
    const unsigned int anniversaryYear = static_cast<unsigned int>(created.wYear) + 5U;
    if (current.wYear > anniversaryYear) {
        return true;
    }
    if (current.wYear != anniversaryYear) {
        return false;
    }
    if (current.wMonth > created.wMonth) {
        return true;
    }
    if (current.wMonth != created.wMonth) {
        return false;
    }
    return current.wDay >= created.wDay;
}

int GlobalObject::Get5YearGiftCount(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043a7e8.
    return creature ? creature->m_nGet5YearGiftCount : 0;
}

void GlobalObject::SaveGet5YearGiftTimeCount(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043a880: NPCServer::Send("cd", 0xc1, SID).
    if (!creature) {
        return;
    }
    g_ns.Send("cd", 0xc1, creature->m_nId);
}

bool GlobalObject::CanGet5YearGift(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043f6f0 calls GetLocalTime before the two nested
    // helpers even though that first value is not later read.
    if (!creature) {
        return false;
    }
    runtime::crt::LocalSystemTimeNow<SYSTEMTIME>();
    return Get5YearGiftCount(creature) == 0 && Is5Yeardate(creature);
}

bool GlobalObject::GetDailyQuestFlag(CSharedCreatureData* creature, int questId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043ace0.
    return creature && creature->GetDailyQuestFlag(questId);
}

void GlobalObject::SetDailyQuestFlag(CSharedCreatureData* creature, int questId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00440fe8 resolves the SID through
    // L2World_CreatureIndex, constructs AtomicChangeDailyQuestFlag, then
    // calls CScriptEngine::AddAtomicJob with the shared-record index.
    if (!creature) {
        return;
    }
    // The target reads index once while constructing the job and again when
    // selecting its queue. Keep those live reads ordered instead
    // of relying on a compiler's function-argument evaluation order.
    AtomicChangeDailyQuestFlag* const job =
        new AtomicChangeDailyQuestFlag(CCreatureFactory::Inst().GetIdByIndex(creature->index), questId);
    CScriptEngine::AddAtomicJob(creature->index, job);
}

bool GlobalObject::GetWeeklyQuestFlag(CSharedCreatureData* creature, int questId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043ae50.
    return creature && creature->GetWeeklyQuestFlag(questId);
}

void GlobalObject::SetWeeklyQuestFlag(CSharedCreatureData* creature, int questId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043ad8c sends only the request. L2Server owns the
    // eventual shared-record update.
    if (!creature) {
        return;
    }
    g_ns.Send("cdd", 0xed, creature->m_nId, questId);
}

void GlobalObject::Quest_GiveQuest(CSharedCreatureData* creature, int questId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043aefc.
    if (!creature || questId <= 0) {
        return;
    }
    g_ns.Send("chdd", 0xfe, 4, creature->m_nId, questId);
}

void GlobalObject::Quest_CompleteQuest(CSharedCreatureData* creature, int questId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043afcc.
    if (!creature || questId <= 0) {
        return;
    }
    g_ns.Send("chdd", 0xfe, 5, creature->m_nId, questId);
}

void GlobalObject::Quest_StartJournal(CSharedCreatureData* creature, int questId, int journalId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043b09c.
    if (!creature || questId <= 0 || journalId <= 0) {
        return;
    }
    g_ns.Send("chddd", 0xfe, 6, creature->m_nId, questId, journalId);
}

void GlobalObject::Quest_CompleteJournal(CSharedCreatureData* creature, int questId, int journalId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043b188.
    if (!creature || questId <= 0 || journalId <= 0) {
        return;
    }
    g_ns.Send("chddd", 0xfe, 7, creature->m_nId, questId, journalId);
}

void GlobalObject::SetSkillAll(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043a63c addresses the creature by shared-factory
    // index, not by server object id.
    if (!creature) {
        return;
    }
    g_ns.Send("cddd", 0xcc, creature->index, 0, 0);
}

void GlobalObject::SetSkillAllByEtcClass(CSharedCreatureData* creature, int etcClassId) {
    L2_GUARD_FUNCTION();
    if (!creature) {
        return;
    }
    // Current L2NPC.exe 0x0043f900 allocates before reading the shared index,
    // constructs only for non-null storage, rereads the index for the queue
    // key, and publishes a null job when allocation failed.
    AtomicNpcSetSkillAll* job = nullptr;
    void* const storage = AtomicJob::operator new(sizeof(AtomicNpcSetSkillAll));
    if (storage) {
        try {
            job = ::new (storage) AtomicNpcSetSkillAll(creature->index, 2, etcClassId);
        } catch (...) {
            AtomicJob::operator delete(storage, true);
            throw;
        }
    }
    CScriptEngine::AddAtomicJob(creature->index, job);
}

CNPC* GlobalObject::GetNPCFromID(int objectId) {
    // The current registration is the 0x00436128 tail thunk to the exact
    // CNPC::GetObjectW implementation at 0x00491574.
    return CNPC::GetObjectW(objectId);
}

void GlobalObject::Announce(char16_t* text) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00435c00 stores text in the first stack vararg,
    // sets R9 to -1, and uses the target-resident "cdS" format.
    g_ns.Send("cdS", 0x78, -1, text);
}

void GlobalObject::AnnounceFStr(
    int messageId, char16_t* string1, char16_t* string2, char16_t* string3, char16_t* string4, char16_t* string5
) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00435cbc:
    // NPCServer::Send("cdSSSSS", 0x78, ...).
    g_ns.Send("cdSSSSS", 0x78, messageId, string1, string2, string3, string4, string5);
}

void GlobalObject::SendUIEvent(
    CSharedCreatureData* creature,
    int argument1,
    int argument2,
    int argument3,
    char16_t* string1,
    char16_t* string2,
    char16_t* string3,
    char16_t* string4,
    char16_t* string5,
    char16_t* string6
) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00437d48:
    // NPCServer::Send("cddddSSSSSS", 0xbb, ...).
    if (!creature) {
        // Log.Add(
        //     LOG_ERROR,
        //     "SendUIEvent(), c==NULL!!")
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot send UI event (%d, %d, %d): recipient creature is missing. Request not sent.",
        //     argument1, argument2, argument3)
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot send UI event (%d, %d, %d): recipient creature is missing; request not sent.",
            argument1, argument2, argument3
        );
        return;
    }
    g_ns.Send(
        "cddddSSSSSS", 0xbb, creature->m_nId, argument1, argument2, argument3, string1, string2, string3, string4, string5,
        string6
    );
}

void GlobalObject::SendUIEventFStr(
    CSharedCreatureData* creature,
    int argument1,
    int argument2,
    int argument3,
    char16_t* label1,
    char16_t* label2,
    char16_t* label3,
    char16_t* label4,
    char16_t* label5,
    int messageId,
    char16_t* string1,
    char16_t* string2,
    char16_t* string3,
    char16_t* string4,
    char16_t* string5
) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00437e98:
    // NPCServer::Send("cddddSSSSSdSSSSS", 0xbc, ...).
    if (!creature) {
        // Log.Add(
        //     LOG_ERROR,
        //     "SendUIEventFStr(), c==NULL!!")
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot send formatted UI event (%d, %d, %d), message %d: recipient creature is missing. Request
        //     not sent.", argument1, argument2, argument3, messageId)
        L2_LOG_ERROR(
            Log,
            "[NPC script] Cannot send formatted UI event (%d, %d, %d), message %d: recipient creature is missing; request "
            "not sent.",
            argument1, argument2, argument3, messageId
        );
        return;
    }
    g_ns.Send(
        "cddddSSSSSdSSSSS", 0xbc, creature->m_nId, argument1, argument2, argument3, label1, label2, label3, label4, label5,
        messageId, string1, string2, string3, string4, string5
    );
}

void GlobalObject::BroadcastUIEventFStr(
    CSharedCreatureData* creature,
    int argument1,
    int argument2,
    int argument3,
    int argument4,
    char16_t* label1,
    char16_t* label2,
    char16_t* label3,
    char16_t* label4,
    char16_t* label5,
    int messageId,
    char16_t* string1,
    char16_t* string2,
    char16_t* string3,
    char16_t* string4,
    char16_t* string5
) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00438030:
    // NPCServer::Send("cdddddSSSSSdSSSSS", 0xf5, ...). The PDB supplies
    // the symbol missing from the stripped function label in the export.
    if (!creature) {
        // Log.Add(
        //     LOG_ERROR,
        //     "BroadcastUIEventFStr(), c==NULL!!")
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot broadcast formatted UI event (%d, %d, %d, %d), message %d: origin creature is missing.
        //     request not sent.", argument1, argument2, argument3, argument4, messageId)
        L2_LOG_ERROR(
            Log,
            "[NPC script] Cannot broadcast formatted UI event (%d, %d, %d, %d), message %d: origin creature is missing; "
            "request not sent.",
            argument1, argument2, argument3, argument4, messageId
        );
        return;
    }
    g_ns.Send(
        "cdddddSSSSSdSSSSS", 0xf5, creature->m_nId, argument1, argument2, argument3, argument4, label1, label2, label3,
        label4, label5, messageId, string1, string2, string3, string4, string5
    );
}

void GlobalObject::BroadcastUIEventFStrInTerritory(
    int territoryId,
    char16_t* territoryName,
    int argument1,
    int argument2,
    int argument3,
    char16_t* label1,
    char16_t* label2,
    char16_t* label3,
    char16_t* label4,
    char16_t* label5,
    int messageId,
    char16_t* string1,
    char16_t* string2,
    char16_t* string3,
    char16_t* string4,
    char16_t* string5
) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x004381d4:
    // NPCServer::Send("cdSdddSSSSSdSSSSS", 0xf6, ...).
    if (!territoryName || *territoryName == u'\0') {
        // Log.Add(
        //     LOG_ERROR,
        //     L"BroadcastUIEventFStrInTerritory "
        //     L"error at name.")
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot broadcast formatted UI event to territory %d, message %d: territory name is %s. Request
        //     not sent.", territoryId, messageId, territoryName ? "empty" : "missing")
        L2_LOG_ERROR(
            Log,
            "[NPC script] Cannot broadcast formatted UI event to territory %d, message %d: territory name is %s; request "
            "not sent.",
            territoryId, messageId, territoryName ? "empty" : "missing"
        );
        ReportScriptContext(LOG_ERROR);
        return;
    }
    g_ns.Send(
        "cdSdddSSSSSdSSSSS", 0xf6, territoryId, territoryName, argument1, argument2, argument3, label1, label2, label3,
        label4, label5, messageId, string1, string2, string3, string4, string5
    );
}

void GlobalObject::SendUSMEvent(CSharedCreatureData* creature, int eventId, int argument) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00437b64:
    // NPCServer::Send("cddd", 0xf8, ...).
    if (!creature) {
        // Log.Add(
        //     LOG_ERROR,
        //     "SendUSMEvent(), c==NULL")
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot send USM event %d (argument %d): recipient creature is missing. Request not sent.",
        //     eventId, argument)
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot send USM event %d (argument %d): recipient creature is missing; request not sent.",
            eventId, argument
        );
        return;
    }
    g_ns.Send("cddd", 0xf8, creature->m_nId, eventId, argument);
}

void GlobalObject::BroadcastUSMEvent(CSharedCreatureData* creature, int eventId, int argument1, int argument2) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00437c50:
    // NPCServer::Send("cdddd", 0xf9, ...).
    if (!creature) {
        // Log.Add(
        //     LOG_ERROR,
        //     "BroadcastUSMEvent(), c==NULL")
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot broadcast USM event %d (arguments %d, %d): origin creature is missing. Request not
        //     sent.", eventId, argument1, argument2)
        L2_LOG_ERROR(
            Log,
            "[NPC script] Cannot broadcast USM event %d (arguments %d, %d): origin creature is missing; request not sent.",
            eventId, argument1, argument2
        );
        return;
    }
    g_ns.Send("cdddd", 0xf9, creature->m_nId, eventId, argument1, argument2);
}

void GlobalObject::ShowOnScreenMsg(
    CSharedCreatureData* creature,
    int messageId,
    int argument1,
    int argument2,
    int argument3,
    int argument4,
    int argument5,
    int argument6,
    int argument7,
    int argument8
) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043838c:
    // NPCServer::Send("cddddddddddd", 0x85, 0, ...). messageId is the
    // first script argument but the final dword in the packet.
    if (!creature) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"ShowOnScreenMsg(), c==NULL!!")
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot display on-screen message %d: recipient creature is missing. Request not sent.",
        //     messageId)
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot display on-screen message %d: recipient creature is missing; request not sent.",
            messageId
        );
        return;
    }
    g_ns.Send(
        "cddddddddddd", 0x85, 0, creature->m_nId, argument1, argument2, argument3, argument4, argument5, argument6,
        argument7, argument8, messageId
    );
}

void GlobalObject::ShowOnScreenMsgStr(
    CSharedCreatureData* creature,
    int argument1,
    int argument2,
    int argument3,
    int argument4,
    int argument5,
    int argument6,
    int argument7,
    int argument8,
    char16_t* message
) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x004384d4:
    // NPCServer::Send("cddddddddddS", 0x85, 1, ...).
    if (!creature) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"ShowOnScreenMsgStr(), c==NULL!!")
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot display on-screen text: recipient creature is missing, request not sent.")
        L2_LOG_ERROR(Log, "[NPC script] Cannot display on-screen text: recipient creature is missing; request not sent.");
        return;
    }
    g_ns.Send(
        "cddddddddddS", 0x85, 1, creature->m_nId, argument1, argument2, argument3, argument4, argument5, argument6,
        argument7, argument8, message
    );
}

void GlobalObject::ShowOnScreenMsgFStr(
    CSharedCreatureData* creature,
    int argument1,
    int argument2,
    int argument3,
    int argument4,
    int argument5,
    int argument6,
    int argument7,
    int argument8,
    int argument9,
    const char16_t* string1,
    const char16_t* string2,
    const char16_t* string3,
    const char16_t* string4,
    const char16_t* string5
) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00438620:
    // NPCServer::Send("cdddddddddddSSSSS", 0x86, 1, ...).
    if (!creature) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"ShowOnScreenMsgFStr(), c==NULL!!")
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot display formatted on-screen text: recipient creature is missing, request not sent.")
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot display formatted on-screen text: recipient creature is missing; request not sent."
        );
        ReportScriptContext(LOG_ERROR);
        return;
    }
    g_ns.Send(
        "cdddddddddddSSSSS", 0x86, 1, creature->m_nId, argument1, argument2, argument3, argument4, argument5, argument6,
        argument7, argument8, argument9, string1, string2, string3, string4, string5
    );
}

void GlobalObject::BroadcastOnScreenMsg(
    CSharedCreatureData* creature,
    int range,
    int messageId,
    int argument1,
    int argument2,
    int argument3,
    int argument4,
    int argument5,
    int argument6,
    int argument7,
    int argument8
) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x004387c4:
    // NPCServer::Send("cdddddddddddd", 0x87, 0, ...).
    if (!creature || static_cast<unsigned int>(range) > 0x4000u) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] BroadcastOnScreenMsg failed. "
        //     L"invalid argument. c==null or Range< 0 or "
        //     L"Range>16384",
        //     L"GlobalObject.cpp",
        //     0x94e)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot broadcast on-screen message %d: origin creature %s (object %u), range %d. Origin must
        //     exist and range must be 0..16384. Request not sent.", messageId, creature ? "present" : "missing", creature ?
        //     static_cast<unsigned int>(creature->m_nId) : 0U, range)
        L2_LOG_ERROR(
            Log,
            "[NPC script] Cannot broadcast on-screen message %d: origin creature %s (object %u), range %d; origin must "
            "exist and range must be 0..16384. Request not sent.",
            messageId, creature ? "present" : "missing", creature ? static_cast<unsigned int>(creature->m_nId) : 0U, range
        );
        return;
    }
    g_ns.Send(
        "cdddddddddddd", 0x87, 0, creature->m_nId, range, argument1, argument2, argument3, argument4, argument5, argument6,
        argument7, argument8, messageId
    );
}

void GlobalObject::BroadcastOnScreenMsgStr(
    CSharedCreatureData* creature,
    int range,
    int argument1,
    int argument2,
    int argument3,
    int argument4,
    int argument5,
    int argument6,
    int argument7,
    int argument8,
    char16_t* message
) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00438938:
    // NPCServer::Send("cdddddddddddS", 0x87, 1, ...).
    if (!creature || static_cast<unsigned int>(range) > 0x4000u) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] BroadcastOnScreenMsgStr failed. "
        //     L"invalid argument. c==null or Range< 0 or "
        //     L"Range>16384",
        //     L"GlobalObject.cpp",
        //     0x95d)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot broadcast on-screen text: origin creature %s (object %u), range %d. Origin must exist and
        //     range must be 0..16384. Request not sent.", creature ? "present" : "missing", creature ? static_cast<unsigned
        //     int>(creature->m_nId) : 0U, range)
        L2_LOG_ERROR(
            Log,
            "[NPC script] Cannot broadcast on-screen text: origin creature %s (object %u), range %d; origin must exist and "
            "range must be 0..16384. Request not sent.",
            creature ? "present" : "missing", creature ? static_cast<unsigned int>(creature->m_nId) : 0U, range
        );
        return;
    }
    g_ns.Send(
        "cdddddddddddS", 0x87, 1, creature->m_nId, range, argument1, argument2, argument3, argument4, argument5, argument6,
        argument7, argument8, message
    );
}

void GlobalObject::BroadcastOnScreenMsgFStr(
    CSharedCreatureData* creature,
    int range,
    int argument1,
    int argument2,
    int argument3,
    int argument4,
    int argument5,
    int argument6,
    int argument7,
    int argument8,
    int argument9,
    const char16_t* string1,
    const char16_t* string2,
    const char16_t* string3,
    const char16_t* string4,
    const char16_t* string5
) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00438ab4:
    // NPCServer::Send("cddddddddddddSSSSS", 0x88, 1, ...).
    if (!creature || static_cast<unsigned int>(range) > 0x4000u) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] BroadcastOnScreenMsgFStr failed. "
        //     L"invalid argument. c==null or Range< 0 or "
        //     L"Range>16384",
        //     L"GlobalObject.cpp",
        //     0x96d)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot broadcast formatted on-screen text: origin creature %s (object %u), range %d. Origin must
        //     exist and range must be 0..16384. Request not sent.", creature ? "present" : "missing", creature ?
        //     static_cast<unsigned int>(creature->m_nId) : 0U, range)
        L2_LOG_ERROR(
            Log,
            "[NPC script] Cannot broadcast formatted on-screen text: origin creature %s (object %u), range %d; origin must "
            "exist and range must be 0..16384. Request not sent.",
            creature ? "present" : "missing", creature ? static_cast<unsigned int>(creature->m_nId) : 0U, range
        );
        return;
    }
    g_ns.Send(
        "cddddddddddddSSSSS", 0x88, 1, creature->m_nId, range, argument1, argument2, argument3, argument4, argument5,
        argument6, argument7, argument8, argument9, string1, string2, string3, string4, string5
    );
}

void GlobalObject::ShowMsgInTerritory(int territoryId, char16_t* territoryName, int messageId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043ba80:
    // NPCServer::Send("cddSd", 0xd2, ...).
    if (!territoryName || *territoryName == u'\0') {
        // Log.Add(
        //     LOG_ERROR,
        //     L"ShowMsgInTerritory error")
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot display message %d in territory %d: territory name is %s. Request not sent.",
        //     messageId, territoryId, territoryName ? "empty" : "missing")
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot display message %d in territory %d: territory name is %s; request not sent.",
            messageId, territoryId, territoryName ? "empty" : "missing"
        );
        ReportScriptContext(LOG_ERROR);
        return;
    }
    g_ns.Send("cddSd", 0xd2, territoryId, 0, territoryName, messageId);
}

void GlobalObject::ShowMsgInTerritory2(int territoryId, char16_t* territoryName, char16_t* message) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043bb84:
    // NPCServer::Send("cddSS", 0xd2, ...).
    if (!territoryName || *territoryName == u'\0') {
        // Log.Add(
        //     LOG_ERROR,
        //     L"ShowMsgInTerritory2 error at name.")
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot display text in territory %d: territory name is %s. Request not sent.",
        //     territoryId, territoryName ? "empty" : "missing")
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot display text in territory %d: territory name is %s; request not sent.", territoryId,
            territoryName ? "empty" : "missing"
        );
        ReportScriptContext(LOG_ERROR);
        return;
    }
    if (!message || *message == u'\0') {
        // Log.Add(
        //     LOG_ERROR,
        //     L"ShowMsgInTerritory2 error at msg")
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot display text in territory '%s' (%d): message is %s. Request not sent.",
        //     Utf16ToUtf8(territoryName).c_str(), territoryId, message ? "empty" : "missing")
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot display text in territory '%s' (%d): message is %s; request not sent.",
            Utf16ToUtf8(territoryName).c_str(), territoryId, message ? "empty" : "missing"
        );
        ReportScriptContext(LOG_ERROR);
        return;
    }
    g_ns.Send("cddSS", 0xd2, territoryId, 1, territoryName, message);
}

void GlobalObject::ShowFStrMsgInTerritory2(
    int territoryId,
    char16_t* territoryName,
    int messageId,
    char16_t* string1,
    char16_t* string2,
    char16_t* string3,
    char16_t* string4,
    char16_t* string5
) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043bcbc:
    // NPCServer::Send("cddSdSSSSS", 0xd2, ...).
    if (!territoryName || *territoryName == u'\0') {
        // Log.Add(
        //     LOG_ERROR,
        //     L"ShowFStrMsgInTerritory2 error at name.")
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot display formatted message %d in territory %d: territory name is %s. Request not sent.",
        //     messageId, territoryId, territoryName ? "empty" : "missing")
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot display formatted message %d in territory %d: territory name is %s; request not sent.",
            messageId, territoryId, territoryName ? "empty" : "missing"
        );
        ReportScriptContext(LOG_ERROR);
        return;
    }
    if (messageId < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"ShowFStrMsgInTerritory2 error at msgIndex")
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot display formatted message %d in territory '%s' (%d): message ID must be non-negative.
        //     request not sent.", messageId, Utf16ToUtf8(territoryName).c_str(), territoryId)
        L2_LOG_ERROR(
            Log,
            "[NPC script] Cannot display formatted message %d in territory '%s' (%d): message ID must be non-negative; "
            "request not sent.",
            messageId, Utf16ToUtf8(territoryName).c_str(), territoryId
        );
        ReportScriptContext(LOG_ERROR);
        return;
    }
    g_ns.Send("cddSdSSSSS", 0xd2, territoryId, 2, territoryName, messageId, string1, string2, string3, string4, string5);
}

void GlobalObject::InstantTeleportEx(CSharedCreatureData* target, int x, int y, int z, int heading) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00435af0 returns silently for null.
    if (!target) {
        return;
    }
    const FVector destination{
        static_cast<double>(x),
        static_cast<double>(y),
        static_cast<double>(z),
    };
    NPCServer::SendInstantTeleport(
        *target, destination, heading, 0, 0, TeleportSourceType::GlobalObjectInstantTeleportEx, nullptr, 0
    );
}

int GlobalObject::IsInWater(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00438e40 has no null guard.
    return static_cast<int>(creature->m_Environment) == 1 ? 1 : 0;
}

int GlobalObject::GetL2Time(int fieldType) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00435db4 reads only the selected global dword.
    switch (fieldType) {
        case L2F_HOUR:
            return g_L2TimeInfo.mHour;
        case L2F_MIN:
            return g_L2TimeInfo.mMin;
        case L2F_IS_NIGHT:
            return g_L2TimeInfo.mIsNight;
        default:
            return 0;
    }
}

int GlobalObject::GetTimeOfDay() {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00435e60 returns TimeValue::Sec() after converting
    // GetSystemTimeAsFileTime.
    return CurrentUnixTimeValue().Sec();
}

int GlobalObject::IsNull(void* value) {
    // Current registration points at the exact 0x00435ae4 leaf.
    return value == nullptr ? 1 : 0;
}

void GlobalObject::SetAsNull(void* value [[maybe_unused]]) {
    // Current registration points at the intentional three-byte
    // 0x004191b8 no-op. The argument is passed by value in the target too.
}

CSharedCreatureData* GlobalObject::GetNullCreature() {
    // Both null factories share the exact 0x00409db8 zero-return leaf.
    return nullptr;
}

l2npc::NpcMakerEx* GlobalObject::GetNullNpcMaker() { return nullptr; }

std::int64_t GlobalObject::FloatToInt(float value) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043684c.
    return runtime::Win64CvttFloatToInt64(value);
}

float GlobalObject::DistFromTo(CSharedCreatureData* from, CSharedCreatureData* to) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x004368dc performs no null check and obtains the
    // second operand's position before the first operand's position.
    const FVector toPosition = to->GetPos();
    const FVector fromPosition = from->GetPos();
    const double dx = fromPosition.x - toPosition.x;
    const double dy = fromPosition.y - toPosition.y;
    const double dz = fromPosition.z - toPosition.z;
    return static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
}

int GlobalObject::Skill_IsMagic(std::int64_t skillNameId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x004369d8. The current guard literal proves the
    // widened __int64 key, superseding the donor PDB's older int signature.
    bool found = true;
    return g_PrecompiledHeaderDB.Skill_GetIsMagic(skillNameId, found);
}

int GlobalObject::Skill_IsHate(std::int64_t skillNameId) {
    L2_GUARD_FUNCTION();
    // Current-only registration at 0x00436a7c.
    bool found = true;
    return g_PrecompiledHeaderDB.Skill_GetIsHate(skillNameId, found);
}

int GlobalObject::Skill_GetAbnormalLevel(std::int64_t skillNameId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00436b20.
    bool found = true;
    return g_PrecompiledHeaderDB.Skill_GetAbnormalLevel(skillNameId, found);
}

int GlobalObject::GetAbnormalLevel(CSharedCreatureData* creature, int abnormalType) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00436bc4.
    if (!creature) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] int GlobalObject::GetAbnormalLevel "
        //     L"failed. c=NULL",
        //     L"GlobalObject.cpp",
        //     0x512)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot read abnormal status level for type %d: creature is missing. Returning -1.",
        //     abnormalType)
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot read abnormal status level for type %d: creature is missing; returning -1.",
            abnormalType
        );
        return -1;
    }
    return creature->GetAbnormalLevel(static_cast<AbnormalStatusSlotType>(abnormalType));
}

int GlobalObject::GetAbnormalMagicLevel(CSharedCreatureData* creature, int abnormalType) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00436c98.
    if (!creature) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] int GlobalObject::GetAbnormalMagicLevel "
        //     L"failed. c=NULL",
        //     L"GlobalObject.cpp",
        //     0x51f)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot read abnormal magic level for type %d: creature is missing. Returning -1.",
        //     abnormalType)
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot read abnormal magic level for type %d: creature is missing; returning -1.",
            abnormalType
        );
        return -1;
    }
    return creature->GetAbnormalMagicLevel(static_cast<AbnormalStatusSlotType>(abnormalType));
}

int GlobalObject::GetIndexFromCreature(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00436d6c.
    if (!creature) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] GlobalObject::GetIndexFromCreature "
        //     L"failed. spCreature=NULL",
        //     L"GlobalObject.cpp",
        //     0x55a)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot read shared creature index: creature is missing, returning zero.")
        L2_LOG_ERROR(Log, "[NPC script] Cannot read shared creature index: creature is missing; returning zero.");
        ReportScriptContext(LOG_ERROR);
        return 0;
    }
    return creature->index;
}

std::int64_t GlobalObject::StrToIntW(char16_t* value) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00437a48 calls
    // _wcstoi64(value, nullptr, 10).
    return ParseTargetInt64(value);
}

float GlobalObject::GetCos(float value) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043b400.
    return std::cos(value);
}

float GlobalObject::GetSin(float value) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043b490.
    return std::sin(value);
}

float GlobalObject::GetSqrt(float value) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043b520.
    return std::sqrt(value);
}

CodeInfoList* GlobalObject::AllocCodeInfoList() {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00444214 allocates from the logical current
    // thread's TempObjectPool<CodeInfoList,5>.
    return Singleton<ThreadLocalStorage<TempObjectPool<CodeInfoList, 5>, 8>>::inst()->Alloc();
}

int GlobalObject::GetStep_FieldCycle(int cycleId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00438ed4.
    if (cycleId <= 0) {
        return -1;
    }
    SharedFieldCycleInfo* const header = SharedFieldCycleInfo::GetInfo(0);
    if (cycleId > header->cycleID) {
        return -1;
    }
    SharedFieldCycleInfo* const cycle = SharedFieldCycleInfo::GetInfo(cycleId);
    return cycle->nowStep;
}

int GlobalObject::GetPoint_FieldCycle(int cycleId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00438f8c.
    if (cycleId <= 0) {
        return -1;
    }
    SharedFieldCycleInfo* const header = SharedFieldCycleInfo::GetInfo(0);
    if (cycleId > header->cycleID) {
        return -1;
    }
    SharedFieldCycleInfo* const cycle = SharedFieldCycleInfo::GetInfo(cycleId);
    return cycle->nowPoint;
}

bool GlobalObject::AddPoint_FieldCycle(int cycleId, int side, int point, CSharedCreatureData* actor) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00439044 clears AL after sending.
    if (cycleId <= 0) {
        return false;
    }
    SharedFieldCycleInfo* const header = SharedFieldCycleInfo::GetInfo(0);
    if (cycleId <= header->cycleID) {
        g_ns.Send(
            "cddddd", 0x9f, SharedFieldCycleInfo::ADD_POINT_FIELD_CYCLE, cycleId, side, point, actor ? actor->m_nId : 0
        );
    }
    return false;
}

bool GlobalObject::AddPointWithoutActor_FieldCycle(int cycleId, int side, int point) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043913c duplicates the range check and packet
    // emission instead of entering AddPoint_FieldCycle. It still
    // returns false after a successful send.
    if (cycleId <= 0) {
        return false;
    }
    SharedFieldCycleInfo* const header = SharedFieldCycleInfo::GetInfo(0);
    if (cycleId <= header->cycleID) {
        g_ns.Send("cddddd", 0x9f, SharedFieldCycleInfo::ADD_POINT_FIELD_CYCLE, cycleId, side, point, 0);
    }
    return false;
}

bool GlobalObject::SetStep_FieldCycle(int cycleId, int side, int step, CSharedCreatureData* actor) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00439224.
    if (cycleId <= 0) {
        return false;
    }
    SharedFieldCycleInfo* const header = SharedFieldCycleInfo::GetInfo(0);
    if (cycleId > header->cycleID) {
        return false;
    }
    g_ns.Send("cddddd", 0x9f, SharedFieldCycleInfo::SET_STEP_FIELD_CYCLE, cycleId, side, step, actor ? actor->m_nId : 0);
    return true;
}

bool GlobalObject::SetStepWithoutActor_FieldCycle(int cycleId, int side, int step) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00439324 has its own body instead of calling
    // SetStep_FieldCycle, so this path produces only its own trace frame.
    if (cycleId <= 0) {
        return false;
    }
    SharedFieldCycleInfo* const header = SharedFieldCycleInfo::GetInfo(0);
    if (cycleId > header->cycleID) {
        return false;
    }
    g_ns.Send("cddddd", 0x9f, SharedFieldCycleInfo::SET_STEP_FIELD_CYCLE, cycleId, side, step, 0);
    return true;
}

bool GlobalObject::SetPoint_FieldCycle(int cycleId, int point) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00439414.
    if (cycleId <= 0) {
        return false;
    }
    SharedFieldCycleInfo* const header = SharedFieldCycleInfo::GetInfo(0);
    if (cycleId > header->cycleID) {
        return false;
    }
    g_ns.Send("cddd", 0x9f, SharedFieldCycleInfo::SET_POINT_FIELD_CYCLE, cycleId, point);
    return true;
}

bool GlobalObject::SetPointLock_FieldCycle(int cycleId, int lockFlag, int lockValue) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x004394ec.
    if (cycleId <= 0) {
        return false;
    }
    SharedFieldCycleInfo* const header = SharedFieldCycleInfo::GetInfo(0);
    if (cycleId > header->cycleID) {
        return false;
    }
    g_ns.Send("cddd", 0xc7, cycleId, lockFlag, lockFlag == 1 ? lockValue : 0);
    return true;
}

int GlobalObject::GetElapsedTimeAfterStepChanged_FieldCycle(int cycleId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x004395dc uses signed 32-bit _time32 arithmetic.
    if (cycleId <= 0) {
        return -1;
    }
    SharedFieldCycleInfo* const header = SharedFieldCycleInfo::GetInfo(0);
    if (cycleId > header->cycleID) {
        return -1;
    }
    const std::time_t hostNow = std::time(nullptr);
    const std::int32_t now = hostNow == static_cast<std::time_t>(-1) ||
            (std::numeric_limits<std::time_t>::is_signed && hostNow < static_cast<std::time_t>(0)) ||
            hostNow > static_cast<std::time_t>(std::numeric_limits<std::int32_t>::max())
        ? -1
        : static_cast<std::int32_t>(hostNow);
    SharedFieldCycleInfo* const cycle = SharedFieldCycleInfo::GetInfo(cycleId);
    return runtime::Int32FromBits(static_cast<std::uint32_t>(now) - static_cast<std::uint32_t>(cycle->stepChangedTime));
}

int GlobalObject::GetElapsedTimeAfterMissionStarted_FieldCycle(int cycleId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x004396a0.
    if (cycleId <= 0) {
        return -1;
    }
    SharedFieldCycleInfo* const header = SharedFieldCycleInfo::GetInfo(0);
    if (cycleId > header->cycleID) {
        return -1;
    }
    SharedFieldCycleInfo* const cycle = SharedFieldCycleInfo::GetInfo(cycleId);
    if (cycle->tMissionStarted == 0) {
        return 0;
    }
    const std::time_t hostNow = std::time(nullptr);
    const std::int32_t now = hostNow == static_cast<std::time_t>(-1) ||
            (std::numeric_limits<std::time_t>::is_signed && hostNow < static_cast<std::time_t>(0)) ||
            hostNow > static_cast<std::time_t>(std::numeric_limits<std::int32_t>::max())
        ? -1
        : static_cast<std::int32_t>(hostNow);
    return runtime::Int32FromBits(static_cast<std::uint32_t>(now) - static_cast<std::uint32_t>(cycle->tMissionStarted));
}

int GlobalObject::GetMissionStatus_FieldCycle(int cycleId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043976c.
    if (cycleId <= 0) {
        return 0;
    }
    SharedFieldCycleInfo* const header = SharedFieldCycleInfo::GetInfo(0);
    if (cycleId > header->cycleID) {
        return 0;
    }
    SharedFieldCycleInfo* const cycle = SharedFieldCycleInfo::GetInfo(cycleId);
    return cycle->eMissionStatus;
}

bool GlobalObject::SetResult_FieldCycle(int cycleId, int side, int result, CSharedCreatureData* actor) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00439824.
    if (cycleId <= 0) {
        return false;
    }
    SharedFieldCycleInfo* const header = SharedFieldCycleInfo::GetInfo(0);
    if (cycleId > header->cycleID) {
        return false;
    }
    g_ns.Send("cddddd", 0x9f, 3, cycleId, side, result, actor ? actor->m_nId : 0);
    return true;
}

void GlobalObject::BroadcastOnScreenMsgFStr_FieldCycle(
    int value1,
    int value2,
    int value3,
    int value4,
    int value5,
    int value6,
    int value7,
    int value8,
    int value9,
    int value10,
    const char16_t* string1,
    const char16_t* string2,
    const char16_t* string3,
    const char16_t* string4,
    const char16_t* string5
) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00439924:
    // NPCServer::Send("chddddddddddSSSSS", 0xfe, 0x2a, ...).
    g_ns.Send(
        "chddddddddddSSSSS", 0xfe, 0x2a, value1, value2, value3, value4, value5, value6, value7, value8, value9, value10,
        string1, string2, string3, string4, string5
    );
}

void GlobalObject::BroadcastFieldEvent(CSharedCreatureData* creature, int eventId, int argument) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00439a98. The PDB supplies the symbol for the
    // stripped target function.
    if (!creature) {
        // Log.Add(
        //     LOG_ERROR,
        //     "BroadcastFieldEvent(), c==NULL")
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot broadcast field event %d (argument %d): source creature is missing. Request not sent.",
        //     eventId, argument)
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot broadcast field event %d (argument %d): source creature is missing; request not sent.",
            eventId, argument
        );
        return;
    }
    g_ns.Send("chddd", 0xfe, 0x2b, creature->m_nId, eventId, argument);
}

char16_t* GlobalObject::GetSubpledgeMasterName(CSharedCreatureData* creature, int pledgeType) {
    L2_GUARD_FUNCTION();
    const PledgeType type = static_cast<PledgeType>(pledgeType);
    if (creature && g_PledgeTypeValidator.IsValid(type) && type != ACADEMY && type != MASTER_PLEDGE) {
        const int pledgeIndex = creature->m_nPledgeIndex;
        if (pledgeIndex == -1) {
            return nullptr;
        }
        CSharedPledgeData* const pledge = CPledgeFactory::Inst().GetData(pledgeIndex);
        if (pledge) {
            return pledge->m_subpledgeMasterName[g_PledgeTypeValidator.GetIndex(type)];
        }
    }
    return EmptyScriptString();
}

int GlobalObject::GetOwnResidenceType(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    if (!creature) {
        return 0;
    }
    // Current L2NPC.exe 0x00440ecc reads and keeps the pledge index before
    // testing the pledge SID. The resulting factory pointer is dereferenced
    // without a null check in the shipping image.
    const int pledgeIndex = creature->m_nPledgeIndex;
    if (creature->m_nPledgeID == 0 || pledgeIndex == -1) {
        return 0;
    }
    CSharedPledgeData* const pledge = CPledgeFactory::Inst().GetData(pledgeIndex);
    const int castleId = static_cast<int>(pledge->m_nCastleDbId);
    if (castleId == 0) {
        return 0;
    }
    CResidenceData& residence = CResidenceData::Inst(castleId);
    return residence.m_ResidenceType;
}

int GlobalObject::Castle_GetDomainFortressContractStatus(int castleId) {
    L2_GUARD_FUNCTION();
    if (static_cast<unsigned int>(castleId) >= 21u) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid castle[%d] in "
        //     L"Castle_GetDomainFortressContractStatus",
        //     castleId)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot read domain fortress contract status: castle ID %d is outside 0..20. Returning -1.",
        //     castleId)
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot read domain fortress contract status: castle ID %d is outside 0..20; returning -1.",
            castleId
        );
        return -1;
    }
    CResidenceData& residence = CResidenceData::Inst(castleId);
    return residence.m_nDomainFortressContractStatus;
}

bool GlobalObject::IsValidFortressId(int fortressId) {
    L2_GUARD_FUNCTION();
    return static_cast<unsigned int>(fortressId) < 128u;
}

int GlobalObject::Fortress_GetOwnerPledgeId(int residenceId) {
    // Current L2NPC.exe 0x00439f2c. The adjacent PDB supplies the name.
    if (static_cast<unsigned int>(residenceId - 1) >= 999u) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"get pledge id in invalid residence[%d]",
        //     residenceId)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot read the owner clan: residence ID %d is outside 1..999. Returning 0.",
        //     residenceId)
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot read the owner clan: residence ID %d is outside 1..999; returning 0.", residenceId
        );
        return 0;
    }
    // Current 0x00439f2c performs two independent Inst lookups and reads
    // both fields before applying the conjunction. Keep those live reads.
    // the residence table may be updated by another process between them.
    const int ownerPledgeId = runtime::Int32FromBits(CResidenceData::Inst(residenceId).m_nResidenceOwnerID);
    const std::uint32_t ownerIndex = CResidenceData::Inst(residenceId).m_nResidenceOwnerIndex;
    if (ownerPledgeId != 0 && ownerIndex != UINT32_MAX) {
        return ownerPledgeId;
    }
    return 0;
}

int GlobalObject::Fortress_GetParentCastleId(int fortressId) {
    L2_GUARD_FUNCTION();
    if (!IsValidFortressId(fortressId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"get pledge id in invalid residence[%d]",
        //     fortressId)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot read the parent castle: fortress ID %d is outside 0..127. Returning -1.",
        //     fortressId)
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot read the parent castle: fortress ID %d is outside 0..127; returning -1.", fortressId
        );
        return -1;
    }
    // Current L2NPC.exe 0x00439f8c performs two independent Inst lookups on
    // the contracted branch.
    if (CResidenceData::Inst(fortressId).m_nFortressContractStatus != 1) {
        return -1;
    }
    return CResidenceData::Inst(fortressId).m_nFortressParentCastleId;
}

int GlobalObject::Fortress_GetFacilityLevel(int fortressId, int facilityType) {
    L2_GUARD_FUNCTION();
    if (static_cast<unsigned int>(facilityType) >= 6u) {
        return -1;
    }
    if (fortressId < 1) {
        return 0;
    }
    CResidenceData& residence = CResidenceData::Inst(fortressId);
    // Selector five is accepted by the current machine code and reaches the
    // dword immediately following m_nFacilityLevel[5].
    return facilityType == 5 ? residence.m_tLastOwnerChangedTime
                             : residence.m_nFacilityLevel[static_cast<std::size_t>(facilityType)];
}

int GlobalObject::Fortress_GetContractStatus(int fortressId) {
    L2_GUARD_FUNCTION();
    if (!IsValidFortressId(fortressId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"get contract status in invalid fortress[%d]",
        //     fortressId)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot read contract status: fortress ID %d is outside 0..127. Returning -1.",
        //     fortressId)
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot read contract status: fortress ID %d is outside 0..127; returning -1.", fortressId
        );
        return -1;
    }
    CResidenceData& residence = CResidenceData::Inst(fortressId);
    return residence.m_nFortressContractStatus;
}

int GlobalObject::Fortress_GetRentCost(int fortressId) {
    L2_GUARD_FUNCTION();
    if (!IsValidFortressId(fortressId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"get rent cost in invalid fortress[%d]",
        //     fortressId)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot read rent cost: fortress ID %d is outside 0..127. Returning 0.",
        //     fortressId)
        L2_LOG_ERROR(Log, "[NPC script] Cannot read rent cost: fortress ID %d is outside 0..127; returning 0.", fortressId);
        return 0;
    }
    CResidenceData& residence = CResidenceData::Inst(fortressId);
    return runtime::Int32FromBits(static_cast<std::uint32_t>(static_cast<std::uint64_t>(residence.m_nFortressRent)));
}

int GlobalObject::Fortress_GetNextRewardRemainTime(int fortressId) {
    L2_GUARD_FUNCTION();
    if (!IsValidFortressId(fortressId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"get next reward remain time in invalid "
        //     L"fortress[%d]",
        //     fortressId)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot read the reward countdown: fortress ID %d is outside 0..127. Returning -1.",
        //     fortressId)
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot read the reward countdown: fortress ID %d is outside 0..127; returning -1.", fortressId
        );
        return -1;
    }
    if (Fortress_GetContractStatus(fortressId) != 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"cannot try get contract status from "
        //     L"uncontracted fortress[%d]",
        //     fortressId)
        // Log.Add(LOG_WARN,
        //     "[NPC script] Reward countdown is unavailable for fortress %d because it is not contracted. Returning -1.",
        //     fortressId)
        L2_LOG_WARN(
            Log, "[NPC script] Reward countdown is unavailable for fortress %d because it is not contracted; returning -1.",
            fortressId
        );
        return -1;
    }
    CResidenceData& residence = CResidenceData::Inst(fortressId);
    const std::time_t hostNow = std::time(nullptr);
    const std::int32_t now = hostNow == static_cast<std::time_t>(-1) ||
            (std::numeric_limits<std::time_t>::is_signed && hostNow < static_cast<std::time_t>(0)) ||
            hostNow > static_cast<std::time_t>(std::numeric_limits<std::int32_t>::max())
        ? -1
        : static_cast<std::int32_t>(hostNow);
    const std::int32_t remaining = runtime::Int32FromBits(
        static_cast<std::uint32_t>(residence.m_tLastRewardGivenTime) - static_cast<std::uint32_t>(now) + 0x16da0U
    );
    const int minutes = remaining / 60;
    if (minutes < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"givetime is minus.[%d]",
        //     fortressId)
        // Log.Add(LOG_WARN,
        //     "[NPC script] Fortress %d reward countdown is %d seconds (%d whole minutes). Returning -1 because fewer than
        //     one minute remains.", fortressId, static_cast<int>(remaining), minutes)
        L2_LOG_WARN(
            Log,
            "[NPC script] Fortress %d reward countdown is %d seconds (%d whole minutes); returning -1 because fewer than "
            "one minute remains.",
            fortressId, static_cast<int>(remaining), minutes
        );
        return -1;
    }
    return minutes;
}

int GlobalObject::Fortress_GetAvailableOwnMinutes(int fortressId) {
    L2_GUARD_FUNCTION();
    if (!IsValidFortressId(fortressId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"get contract status in invalid fortress[%d]",
        //     fortressId)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot read remaining ownership time: fortress ID %d is outside 0..127. Returning 0.",
        //     fortressId)
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot read remaining ownership time: fortress ID %d is outside 0..127; returning 0.",
            fortressId
        );
        return 0;
    }
    CResidenceData& residence = CResidenceData::Inst(fortressId);
    // Current L2NPC.exe 0x0043f5e4 contains this calculation in the wrapper.
    // it calls the fixed-limit accessor once for the comparison and again
    // for the selected result.
    const std::time_t hostNow = std::time(nullptr);
    const std::int32_t now = hostNow == static_cast<std::time_t>(-1) ||
            (std::numeric_limits<std::time_t>::is_signed && hostNow < static_cast<std::time_t>(0)) ||
            hostNow > static_cast<std::time_t>(std::numeric_limits<std::int32_t>::max())
        ? -1
        : static_cast<std::int32_t>(hostNow);
    const std::int32_t elapsed = runtime::Int32FromBits(
        static_cast<std::uint32_t>(now) - static_cast<std::uint32_t>(residence.m_tLastOwnerChangedTime)
    );
    int remaining = 0;
    const std::int32_t firstRemaining = runtime::Int32FromBits(
        static_cast<std::uint32_t>(GetMaxFortressOwnSeconds()) - static_cast<std::uint32_t>(elapsed)
    );
    if (firstRemaining > 0) {
        remaining = runtime::Int32FromBits(
            static_cast<std::uint32_t>(GetMaxFortressOwnSeconds()) - static_cast<std::uint32_t>(elapsed)
        );
    }
    return remaining / 60;
}

void GlobalObject::SetDominionState(int dominionId, int state) {
    L2_GUARD_FUNCTION();
    if (static_cast<unsigned int>(dominionId) >= 128u) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Script:SetDominionState : Over max "
        //     L"residence, dominion id [%d]",
        //     dominionId)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot set dominion %d to state %d: ID is outside the residence table range 0..127.",
        //     dominionId, state)
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot set dominion %d to state %d: ID is outside the residence table range 0..127.",
            dominionId, state
        );
        return;
    }
    if (static_cast<unsigned int>(dominionId - 0x51) >= 9u) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Script:GetDominionState : dominion id "
        //     L"is not valid, id [%d]",
        //     dominionId)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot set dominion %d to state %d: dominion ID must be in 81..89.",
        //     dominionId, state)
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot set dominion %d to state %d: dominion ID must be in 81..89.", dominionId, state
        );
        return;
    }
    CResidenceData& residence = CResidenceData::Inst(dominionId);
    residence.m_nDominionStatus = state;
}

int GlobalObject::GetDominionState(int dominionId) {
    L2_GUARD_FUNCTION();
    if (static_cast<unsigned int>(dominionId) >= 128u) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Script:GetDominionState : Over max "
        //     L"residence, dominion id [%d]",
        //     dominionId)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot read dominion %d state: ID is outside the residence table range 0..127. Returning 0.",
        //     dominionId)
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot read dominion %d state: ID is outside the residence table range 0..127; returning 0.",
            dominionId
        );
        return 0;
    }
    if (static_cast<unsigned int>(dominionId - 0x51) >= 9u) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Script:GetDominionState : dominion id "
        //     L"is not valid, id [%d]",
        //     dominionId)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot read dominion %d state: dominion ID must be in 81..89. Returning 0.",
        //     dominionId)
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot read dominion %d state: dominion ID must be in 81..89; returning 0.", dominionId
        );
        return 0;
    }
    CResidenceData& residence = CResidenceData::Inst(dominionId);
    return residence.m_nDominionStatus;
}

int GlobalObject::GetDominionWarState(int residenceId) {
    L2_GUARD_FUNCTION();
    if (static_cast<unsigned int>(residenceId) >= 128u) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Script:GetDominionWarState : Over max "
        //     L"residence")
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot read dominion war state: residence ID %d is outside 0..127. Returning the invalid state
        //     %d.", residenceId, static_cast<int>(RS_MAX))
        L2_LOG_ERROR(
            Log,
            "[NPC script] Cannot read dominion war state: residence ID %d is outside 0..127; returning the invalid state "
            "%d.",
            residenceId, static_cast<int>(RS_MAX)
        );
        return RS_MAX;
    }
    CResidenceData& residence = CResidenceData::Inst(residenceId);
    return residence.m_ResidenceState;
}

int GlobalObject::IsInstantAgitOwnerPledge(int pledgeId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043c96c.
    return SharedInstantAgitDecoData::GetInfo(pledgeId) ? 1 : 0;
}

void GlobalObject::LoadEventScheduledTime(int eventId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00447540.
    g_ns.Send("cd", 0xe1, eventId);
    m_EventScheduledTimeMap.clear();
}

bool GlobalObject::IsEventScheduledTime(int eventId) {
    L2_GUARD_FUNCTION();
    if (!m_HitSchduledEvent || m_EventScheduledTimeMap.empty()) {
        return false;
    }

    L2_GUARD(m_EventLock);
    const auto found = m_EventScheduledTimeMap.find(eventId);

    // This is the shipping branch direction at 0x00442193..0x004421b5:
    // a found key releases the lock and returns false. The opposite branch
    // dereferences the MSVC tree's end sentinel as if it contained a vector.
    // That sentinel has no scheduled values in a valid target map, so keep
    // the observable false result without invoking host-STL undefined
    // behavior.
    if (found != m_EventScheduledTimeMap.end()) {
        return false;
    }
    return false;
}

int GlobalObject::GetDBSavingMap(int key) {
    L2_GUARD_FUNCTION();
    int value;
    {
        L2_GUARD(l2npc::g_dbsaving_map_lock);
        const auto found = l2npc::g_dbsaving_map.find(key);
        value = found == l2npc::g_dbsaving_map.end() ? -1 : found->second;
    }
    return value;
}

void GlobalObject::RegisterDBSavingMap(int key, int value) {
    L2_GUARD_FUNCTION();
    l2npc::dbnpc.SaveDBSavingMap(key, value);
    {
        L2_GUARD(l2npc::g_dbsaving_map_lock);
        l2npc::g_dbsaving_map[key] = value;
    }
}

int GlobalObject::UnregisterDBSavingMap(int key) {
    L2_GUARD_FUNCTION();
    bool removed;
    {
        L2_GUARD(l2npc::g_dbsaving_map_lock);
        removed = l2npc::g_dbsaving_map.erase(key) != 0;
    }
    if (removed) {
        l2npc::dbnpc.DeleteDBSavingMap(key);
    }
    return removed ? 1 : 0;
}

void GlobalObject::LoadDBSavingMap(CSharedCreatureData* target, int key) {
    L2_GUARD_FUNCTION();
    if (target) {
        l2npc::dbnpc.LoadDBSavingMap(target->m_nId, key);
    }
}

int GlobalObject::IsSameString(char16_t* left, char16_t* right) {
    L2_GUARD_FUNCTION();
    if (!left || !right) {
        return 0;
    }
    for (;;) {
        const char16_t leftValue = *left;
        const char16_t rightValue = *right;
        if (leftValue != rightValue) {
            return 0;
        }
        if (leftValue == u'\0') {
            return 1;
        }
        ++left;
        ++right;
    }
}

int GlobalObject::GetAreaStatusEx(char16_t* name, int inzoneId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_AreaStatusLock);
    for (auto current = m_vAreaStatus.begin(); current != m_vAreaStatus.end(); ++current) {
        const AreaStatusEventInfo& info = *current;
        if (info.m_nInZoneId == inzoneId && IsSameString(info.m_sName, name) != 0) {
            return info.m_bOnOff;
        }
    }
    return -1;
}

int GlobalObject::GetDoorStatusEx(char16_t* name, int inzoneId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_DoorStatusLock);
    for (auto current = m_vDoorStatus.begin(); current != m_vDoorStatus.end(); ++current) {
        const DoorStatusEventInfo& info = *current;
        if (info.m_nInZoneId == inzoneId && IsSameString(info.m_sName, name) != 0) {
            return info.m_bOnOff;
        }
    }
    return -1;
}

bool GlobalObject::Area_RegisterStatus(CSharedCreatureData* target, char16_t* name, int inzoneId) {
    L2_GUARD_FUNCTION();
    if (!target) {
        return false;
    }
    if (!name || *name == u'\0') {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid area name inzone[%d]",
        //     L"GlobalObject.cpp",
        //     0x3fd,
        //     inzoneId)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot register area status notifications in instance %d: name is %s. Request not sent.",
        //     inzoneId, name ? "empty" : "missing")
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot register area status notifications in instance %d: name is %s; request not sent.",
            inzoneId, name ? "empty" : "missing"
        );
        ReportScriptContext(LOG_ERROR);
        return false;
    }

    L2_GUARD(m_AreaStatusLock);
    m_vAreaStatus.push_back(AreaStatusEventInfo(target->m_nId, 0, inzoneId, name));
    g_ns.Send("cdSd", 0xeb, 0, name, inzoneId);
    return true;
}

bool GlobalObject::Door_RegisterStatus(CSharedCreatureData* target, char16_t* name, int inzoneId) {
    L2_GUARD_FUNCTION();
    if (!target) {
        return false;
    }
    if (!name || *name == u'\0') {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid door name inzone[%d]",
        //     L"GlobalObject.cpp",
        //     0x3dc,
        //     inzoneId)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot register door status notifications in instance %d: name is %s. Request not sent.",
        //     inzoneId, name ? "empty" : "missing")
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot register door status notifications in instance %d: name is %s; request not sent.",
            inzoneId, name ? "empty" : "missing"
        );
        ReportScriptContext(LOG_ERROR);
        return false;
    }

    L2_GUARD(m_DoorStatusLock);
    m_vDoorStatus.push_back(DoorStatusEventInfo(target->m_nId, 0, inzoneId, name));
    g_ns.Send("cdSd", 0xeb, 1, name, inzoneId);
    return true;
}

bool GlobalObject::Area_UnRegisterStatus(CSharedCreatureData* target, char16_t* name, int inzoneId) {
    L2_GUARD_FUNCTION();
    if (!target) {
        return false;
    }
    if (!name || *name == u'\0') {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid area name inzone[%d]",
        //     L"GlobalObject.cpp",
        //     0x3b2,
        //     inzoneId)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot unregister area status notifications in instance %d: name is %s. Request not sent.",
        //     inzoneId, name ? "empty" : "missing")
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot unregister area status notifications in instance %d: name is %s; request not sent.",
            inzoneId, name ? "empty" : "missing"
        );
        ReportScriptContext(LOG_ERROR);
        return false;
    }

    L2_GUARD(m_AreaStatusLock);
    auto found = m_vAreaStatus.begin();
    while (found != m_vAreaStatus.end()) {
        if (found->m_nInZoneId == inzoneId && IsSameString(found->m_sName, name) != 0) {
            break;
        }
        ++found;
    }
    if (found == m_vAreaStatus.end()) {
        return false;
    }
    m_vAreaStatus.erase(found);
    g_ns.Send("cdSd", 0xeb, 2, name, inzoneId);
    return true;
}

bool GlobalObject::Door_UnRegisterStatus(CSharedCreatureData* target, char16_t* name, int inzoneId) {
    L2_GUARD_FUNCTION();
    if (!target) {
        return false;
    }
    if (!name || *name == u'\0') {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid door name inzone[%d]",
        //     L"GlobalObject.cpp",
        //     0x38c,
        //     inzoneId)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot unregister door status notifications in instance %d: name is %s. Request not sent.",
        //     inzoneId, name ? "empty" : "missing")
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot unregister door status notifications in instance %d: name is %s; request not sent.",
            inzoneId, name ? "empty" : "missing"
        );
        ReportScriptContext(LOG_ERROR);
        return false;
    }

    L2_GUARD(m_DoorStatusLock);
    auto found = m_vDoorStatus.begin();
    while (found != m_vDoorStatus.end()) {
        if (found->m_nInZoneId == inzoneId && IsSameString(found->m_sName, name) != 0) {
            break;
        }
        ++found;
    }
    if (found == m_vDoorStatus.end()) {
        return false;
    }
    m_vDoorStatus.erase(found);
    g_ns.Send("cdSd", 0xeb, 3, name, inzoneId);
    return true;
}

void GlobalObject::RegisterEventStatus(AreaDoorStautsType type, char16_t* name, int inzoneId, int status) {
    L2_GUARD_FUNCTION();
    if (type == ADST_AREA_REGISTER || type == ADST_AREA_SETEVENT) {
        L2_GUARD(m_AreaStatusLock);
        for (auto current = m_vAreaStatus.begin(); current != m_vAreaStatus.end(); ++current) {
            AreaStatusEventInfo& info = *current;
            if (info.m_nInZoneId != inzoneId || IsSameString(info.m_sName, name) == 0) {
                continue;
            }
            info.m_bOnOff = status;
            if (type == ADST_AREA_SETEVENT) {
                if (CNPC* const npc = CNPC::GetObjectW(info.m_nId)) {
                    npc->SendAreaStatusEvent(info.m_nInZoneId, info.m_sName, info.m_bOnOff != 0);
                }
            }
        }
        return;
    }

    if (type == ADST_DOOR_REGISTER || type == ADST_DOOR_SETEVENT) {
        L2_GUARD(m_DoorStatusLock);
        for (auto current = m_vDoorStatus.begin(); current != m_vDoorStatus.end(); ++current) {
            DoorStatusEventInfo& info = *current;
            if (info.m_nInZoneId != inzoneId || IsSameString(info.m_sName, name) == 0) {
                continue;
            }
            info.m_bOnOff = status;
            if (type == ADST_DOOR_SETEVENT) {
                if (CNPC* const npc = CNPC::GetObjectW(info.m_nId)) {
                    npc->SendDoorStatusEvent(info.m_nInZoneId, info.m_sName, info.m_bOnOff != 0);
                }
            }
        }
    }
}

GlobalObject gGlobalObj;

GlobalObject::GlobalObject() : mServerTime(0) {
    // Current L2NPC.exe 0x004477d0 performs these writes only after every
    // map, lock, vector, and AdminRewardSystem subobject has been built.
    // m_HitSchduledEvent is absent: gGlobalObj obtains its
    // initial zero from static storage, exactly as the shipping singleton.
    std::memset(mEventValue, 0, sizeof(mEventValue));
    mJackpotTimeOver = true;
}

GlobalObject::~GlobalObject() = default;

void GlobalObject::CollectFunc(ScriptTypeInfo* typeInfo) {
    // Current L2NPC.exe 0x0044f2e4. Registration order is part of the
    // ai.obj ABI: it produces the low 16-bit function index embedded in
    // every GlobalObject native opcode.
    const auto add = [typeInfo](const wchar_t* name, auto function) { AddGlobalFunction(typeInfo, name, function); };

    add(L"Superpoint_GetMoveType", &GlobalObject::Superpoint_GetMoveType);
    add(L"ShowOnScreenMsg", &GlobalObject::ShowOnScreenMsg);
    add(L"BroadcastOnScreenMsg", &GlobalObject::BroadcastOnScreenMsg);
    add(L"ShowOnScreenMsgStr", &GlobalObject::ShowOnScreenMsgStr);
    add(L"ShowOnScreenMsgFStr", &GlobalObject::ShowOnScreenMsgFStr);
    add(L"BroadcastOnScreenMsgStr", &GlobalObject::BroadcastOnScreenMsgStr);
    add(L"BroadcastOnScreenMsgFStr", &GlobalObject::BroadcastOnScreenMsgFStr);
    add(L"Rand", &GlobalObject::Rand);
    add(L"RandOrPreset", &GlobalObject::RandOrPreset);
    add(L"GetL2Time", &GlobalObject::GetL2Time);
    add(L"GetTimeOfDay", &GlobalObject::GetTimeOfDay);
    add(L"GetNpcMaker", &GlobalObject::GetNpcMaker);
    add(L"InstantZone_GetNpcMaker", &GlobalObject::InstantZone_GetNpcMaker);
    add(L"AllocCodeInfoList", &GlobalObject::AllocCodeInfoList);
    add(L"GetItemCollectable", &GlobalObject::GetItemCollectable);
    add(L"GetNPCFromID", &GlobalObject::GetNPCFromID);
    add(L"SendMakerScriptEvent", &GlobalObject::SendMakerScriptEvent);
    add(L"CreateRoomInfoList", &GlobalObject::CreateRoomInfoList);
    add(L"GetRoomInfoList", &GlobalObject::GetRoomInfoList);
    add(L"Announce", &GlobalObject::Announce);
    add(L"AnnounceFStr", &GlobalObject::AnnounceFStr);
    add(L"GetDateTime", &GlobalObject::GetDateTime);
    add(L"IsNull", &GlobalObject::IsNull);
    add(L"SendScriptEvent", &GlobalObject::SendScriptEvent);
    add(L"SendScriptEventEx", &GlobalObject::SendScriptEventEx);
    add(L"IsSameString", &GlobalObject::IsSameString);
    add(L"InstantTeleportEx", &GlobalObject::InstantTeleportEx);
    add(L"AddLogEx", &GlobalObject::AddLogEx);
    add(L"AddLogExWithoutCreature", &GlobalObject::AddLogExWithoutCreature);
    add(L"GetParty", &GlobalObject::GetParty);
    add(L"GetMPCC", &GlobalObject::GetMPCC);
    add(L"Area_SetOnOff", &GlobalObject::Area_SetOnOff);
    add(L"Castle_GateOpenClose2", &GlobalObject::Castle_GateOpenClose2);
    add(L"Castle_GateOpenCloseEx", &GlobalObject::Castle_GateOpenCloseEx);
    add(L"Castle_GetDomainFortressContractStatus", &GlobalObject::Castle_GetDomainFortressContractStatus);
    add(L"FloatToInt", &GlobalObject::FloatToInt);
    add(L"Dist", &GlobalObject::DistFromTo);
    add(L"Skill_IsMagic", &GlobalObject::Skill_IsMagic);
    add(L"Skill_IsHate", &GlobalObject::Skill_IsHate);
    add(L"Skill_GetAbnormalLevel", &GlobalObject::Skill_GetAbnormalLevel);
    add(L"GetAbnormalLevel", &GlobalObject::GetAbnormalLevel);
    add(L"GetAbnormalMagicLevel", &GlobalObject::GetAbnormalMagicLevel);
    add(L"GetCreatureFromID", &GlobalObject::GetCreatureFromID);
    add(L"GetCreatureFromIndex", &GlobalObject::GetCreatureFromIndex);
    add(L"GetIndexFromCreature", &GlobalObject::GetIndexFromCreature);
    add(L"GetPartyFromEventRoom", &GlobalObject::GetPartyFromEventRoom);
    add(L"GetPartyFromID", &GlobalObject::GetPartyFromID);
    add(L"IntToStr", &GlobalObject::IntToStr);
    add(L"IntToFStr", &GlobalObject::IntToFStr);
    add(L"GetTimeAttackRecordInfo", &GlobalObject::GetTimeAttackRecordInfo);
    add(L"Party_GetCount", &GlobalObject::Party_GetCount);
    add(L"AddLog", &GlobalObject::AddLog);
    add(L"GetSSQPart", &GlobalObject::GetSSQPart);
    add(L"IsWinnerOfTimeAttackEvent", &GlobalObject::IsWinnerOfTimeAttackEvent);
    add(L"GetTimeAttackRewardFlag", &GlobalObject::GetTimeAttackRewardFlag);
    add(L"OwnItemCount", &GlobalObject::OwnItemCount);
    add(L"OwnItemCount2", &GlobalObject::OwnItemCount2);
    add(L"GetMemoStateEx", &GlobalObject::GetMemoStateEx);
    add(L"HaveMemo", &GlobalObject::HaveMemo);
    add(L"GetMemoState", &GlobalObject::GetMemoState);
    add(L"Party_GetCreature", &GlobalObject::Party_GetCreature);
    add(L"Party_GetLeader", &GlobalObject::Party_GetLeader);
    add(L"StrToInt", &GlobalObject::StrToIntW);
    add(L"AddPartyToEventRoom", &GlobalObject::AddPartyToEventRoom);
    add(L"ClearEventRoom", &GlobalObject::ClearEventRoom);
    add(L"IsEventServer", &GlobalObject::IsEventServer);
    add(L"SetNpcParam", &GlobalObject::SetNpcParam);
    add(L"ShowCursedWeaponStatus", &GlobalObject::ShowCursedWeaponStatus);
    add(L"DropItem3", &GlobalObject::DropItem3);
    add(L"IsInWater", &GlobalObject::IsInWater);
    add(L"GetSubpledgeMasterName", &GlobalObject::GetSubpledgeMasterName);
    add(L"AddPCSocial", &GlobalObject::AddPCSocial);
    add(L"SetAsNull", &GlobalObject::SetAsNull);
    add(L"GetNullCreature", &GlobalObject::GetNullCreature);
    add(L"GetNullNpcMaker", &GlobalObject::GetNullNpcMaker);
    add(L"GetStep_FieldCycle", &GlobalObject::GetStep_FieldCycle);
    add(L"GetPoint_FieldCycle", &GlobalObject::GetPoint_FieldCycle);
    add(L"AddPoint_FieldCycle", &GlobalObject::AddPoint_FieldCycle);
    add(L"AddPointWithoutActor_FieldCycle", &GlobalObject::AddPointWithoutActor_FieldCycle);
    add(L"SetStep_FieldCycle", &GlobalObject::SetStep_FieldCycle);
    add(L"SetStepWithoutActor_FieldCycle", &GlobalObject::SetStepWithoutActor_FieldCycle);
    add(L"SetPoint_FieldCycle", &GlobalObject::SetPoint_FieldCycle);
    add(L"SetPointLock_FieldCycle", &GlobalObject::SetPointLock_FieldCycle);
    add(L"GetElapsedTimeAfterStepChanged_FieldCycle", &GlobalObject::GetElapsedTimeAfterStepChanged_FieldCycle);
    add(L"GetElapsedTimeAfterMissionStarted_FieldCycle", &GlobalObject::GetElapsedTimeAfterMissionStarted_FieldCycle);
    add(L"GetMissionStatus_FieldCycle", &GlobalObject::GetMissionStatus_FieldCycle);
    add(L"SetResult_FieldCycle", &GlobalObject::SetResult_FieldCycle);
    add(L"BroadcastOnScreenMsgFStr_FieldCycle", &GlobalObject::BroadcastOnScreenMsgFStr_FieldCycle);
    add(L"BroadcastFieldEvent", &GlobalObject::BroadcastFieldEvent);
    add(L"GetDynamicQuestProgress", &GlobalObject::GetDynamicQuestProgress);
    add(L"IsParticipantInDynamicContent", &GlobalObject::IsParticipantInDynamicContent);
    add(L"GetDoorStatus", &GlobalObject::GetDoorStatus);
    add(L"GetAreaStatusEx", &GlobalObject::GetAreaStatusEx);
    add(L"GetDoorStatusEx", &GlobalObject::GetDoorStatusEx);
    add(L"Fortress_GetOwnerPledgeId", &GlobalObject::Fortress_GetOwnerPledgeId);
    add(L"Fortress_GetParentCastleId", &GlobalObject::Fortress_GetParentCastleId);
    add(L"Fortress_GetFacilityLevel", &GlobalObject::Fortress_GetFacilityLevel);
    add(L"Fortress_GetContractStatus", &GlobalObject::Fortress_GetContractStatus);
    add(L"Fortress_GetAvailableOwnMinutes", &GlobalObject::Fortress_GetAvailableOwnMinutes);
    add(L"Fortress_GetRentCost", &GlobalObject::Fortress_GetRentCost);
    add(L"Fortress_GetNextRewardRemainTime", &GlobalObject::Fortress_GetNextRewardRemainTime);
    add(L"Area_SetOnOffEx", &GlobalObject::Area_SetOnOffEx);
    add(L"Area_SetBannedTerritoryOnOff", &GlobalObject::Area_SetBannedTerritoryOnOff);
    add(L"GetOwnResidenceType", &GlobalObject::GetOwnResidenceType);
    add(L"AddScriptLog", &GlobalObject::AddScriptLog);
    add(L"GetNRMemoStateEx", &GlobalObject::GetNRMemoStateEx);
    add(L"HaveNRMemo", &GlobalObject::HaveNRMemo);
    add(L"GetNRMemoState", &GlobalObject::GetNRMemoState);
    add(L"SetPoint_RimKamaroka", &GlobalObject::SetPoint_RimKamaroka);
    add(L"GetRank_RimKamaroka", &GlobalObject::GetRank_RimKamaroka);
    add(L"OwnItemEnchantCount", &GlobalObject::OwnItemEnchantCount);
    add(L"SendUIEvent", &GlobalObject::SendUIEvent);
    add(L"SendUIEventFStr", &GlobalObject::SendUIEventFStr);
    add(L"IsCreateDate", &GlobalObject::IsCreateDate);
    add(L"CanGetBirthdayGift", &GlobalObject::CanGetBirthdayGift);
    add(L"SaveGetBirthdayGiftTime", &GlobalObject::SaveGetBirthdayGiftTime);
    add(L"GetBirthdayGiftCount", &GlobalObject::GetBirthdayGiftCount);
    add(L"GetDailyQuestFlag", &GlobalObject::GetDailyQuestFlag);
    add(L"SetDailyQuestFlag", &GlobalObject::SetDailyQuestFlag);
    add(L"GetWeeklyQuestFlag", &GlobalObject::GetWeeklyQuestFlag);
    add(L"SetWeeklyQuestFlag", &GlobalObject::SetWeeklyQuestFlag);
    add(L"SunRise", &GlobalObject::SunRise);
    add(L"CanGet5YearGift", &GlobalObject::CanGet5YearGift);
    add(L"Is5Yeardate", &GlobalObject::Is5Yeardate);
    add(L"SaveGet5YearGiftTimeCount", &GlobalObject::SaveGet5YearGiftTimeCount);
    add(L"Get5YearGiftCount", &GlobalObject::Get5YearGiftCount);
    add(L"GetCos", &GlobalObject::GetCos);
    add(L"GetSin", &GlobalObject::GetSin);
    add(L"GetSqrt", &GlobalObject::GetSqrt);
    add(L"InzoneDoorBreakable", &GlobalObject::InzoneDoorBreakable);
    add(L"SendDominionScriptEvent", &GlobalObject::SendDominionScriptEvent);
    add(L"RegisterDBSavingMap", &GlobalObject::RegisterDBSavingMap);
    add(L"UnregisterDBSavingMap", &GlobalObject::UnregisterDBSavingMap);
    add(L"GetDBSavingMap", &GlobalObject::GetDBSavingMap);
    add(L"LoadDBSavingMap", &GlobalObject::LoadDBSavingMap);
    add(L"SetDominionState", &GlobalObject::SetDominionState);
    add(L"GetDominionState", &GlobalObject::GetDominionState);
    add(L"SetSkillAll", &GlobalObject::SetSkillAll);
    add(L"SetXMasEventState", &GlobalObject::SetXMasEventState);
    add(L"SetYearCatchEventState", &GlobalObject::SetYearCatchEventState);
    add(L"GetDominionWarState", &GlobalObject::GetDominionWarState);
    add(L"ShowMsgInTerritory", &GlobalObject::ShowMsgInTerritory);
    add(L"ShowMsgInTerritory2", &GlobalObject::ShowMsgInTerritory2);
    add(L"ShowFStrMsgInTerritory2", &GlobalObject::ShowFStrMsgInTerritory2);
    add(L"GetRandomPosInTerritory", &GlobalObject::GetRandomPosInTerritory);
    add(L"GetRandomPosInCreature", &GlobalObject::GetRandomPosInCreature);
    add(L"GetRandomPosInPos", &GlobalObject::GetRandomPosInPos);
    add(L"PrintVarInt", &GlobalObject::PrintVarInt);
    add(L"GetEventValue", &GlobalObject::GetEventValue);
    add(L"SetEventValue", &GlobalObject::SetEventValue);
    add(L"GetInZoneFlag", &GlobalObject::GetInZoneFlag);
    add(L"SetInZoneFlag", &GlobalObject::SetInZoneFlag);
    add(L"LoadEventScheduledTime", &GlobalObject::LoadEventScheduledTime);
    add(L"IsEventScheduledTime", &GlobalObject::IsEventScheduledTime);
    add(L"LoadJackpotTime", &GlobalObject::LoadJackpotTime);
    add(L"IsJackpotTime", &GlobalObject::IsJackpotTime);
    add(L"GetPlayingUserAverage", &GlobalObject::GetPlayingUserAverage);
    add(L"StartBuffEvent", &GlobalObject::StartBuffEvent);
    add(L"SetEventState", &GlobalObject::SetEventState);
    add(L"GetEventElapsedTime", &GlobalObject::GetEventElapsedTime);
    add(L"SetEventDebugInfo", &GlobalObject::SetEventDebugInfo);
    add(L"Area_RegisterStatus", &GlobalObject::Area_RegisterStatus);
    add(L"Door_RegisterStatus", &GlobalObject::Door_RegisterStatus);
    add(L"Area_UnRegisterStatus", &GlobalObject::Area_UnRegisterStatus);
    add(L"Door_UnRegisterStatus", &GlobalObject::Door_UnRegisterStatus);
    add(L"GetTerritoryNpcPos", &GlobalObject::GetTerritoryNpcPos);
    add(L"AddDynamicQuestPoint", &GlobalObject::AddDynamicQuestPoint);
    add(L"GetOlympiadGameRule", &GlobalObject::GetOlympiadGameRule);
    add(L"GetOlympiadRank", &GlobalObject::GetOlympiadRank);
    add(L"AddOlympiadPoint", &GlobalObject::AddOlympiadPoint);
    add(L"ResetOlympiadPoint", &GlobalObject::ResetOlympiadPoint);
    add(L"BroadcastUIEventFStr", &GlobalObject::BroadcastUIEventFStr);
    add(L"BroadcastUIEventFStrInTerritory", &GlobalObject::BroadcastUIEventFStrInTerritory);
    add(L"IsInstantAgitOwnerPledge", &GlobalObject::IsInstantAgitOwnerPledge);
    add(L"GetElapsedTimeFromLastLogout", &GlobalObject::GetElapsedTimeFromLastLogout);
    add(L"SendUSMEvent", &GlobalObject::SendUSMEvent);
    add(L"BroadcastUSMEvent", &GlobalObject::BroadcastUSMEvent);
    add(L"SetSkillAllByEtcClass", &GlobalObject::SetSkillAllByEtcClass);
    add(L"GetSex", &GlobalObject::GetSex);
    add(L"IsWeakenedDeathPenalty", &GlobalObject::IsWeakenedDeathPenalty);
    add(L"GetAgathionCount", &GlobalObject::GetAgathionCount);
    add(L"GetEventGoal", &GlobalObject::GetEventGoal);
    add(L"Quest_GiveQuest", &GlobalObject::Quest_GiveQuest);
    add(L"Quest_CompleteQuest", &GlobalObject::Quest_CompleteQuest);
    add(L"Quest_StartJournal", &GlobalObject::Quest_StartJournal);
    add(L"Quest_CompleteJournal", &GlobalObject::Quest_CompleteJournal);
    add(L"SetShillienMoon", &GlobalObject::SetShillienMoon);
    add(L"GetWinnerPledgeNameLastSeason", &GlobalObject::GetWinnerPledgeNameLastSeason);
    add(L"GetWinnerPlayerNameLastSeason", &GlobalObject::GetWinnerPlayerNameLastSeason);
    add(L"IsMemberOfWinnersPledgeLastSeason", &GlobalObject::IsMemberOfWinnersPledgeLastSeason);
    add(L"IsWinnerLastSeason", &GlobalObject::IsWinnerLastSeason);
    add(L"IsWinnerLastSeasonAndDidNotGetPrize", &GlobalObject::IsWinnerLastSeasonAndDidNotGetPrize);
    add(L"GetPledgeGameTopRankerPointThisSeason", &GlobalObject::GetPledgeGameTopRankerPointThisSeason);
    add(L"SetWinnerLastSeasonGotPrize", &GlobalObject::SetWinnerLastSeasonGotPrize);
    add(L"GetDynamicQuestGoalInfo", &GlobalObject::GetDynamicQuestGoalInfo);
    add(L"GetDynamicQuestLivePoint", &GlobalObject::GetDynamicQuestLivePoint);
    add(L"GotoServer", &GlobalObject::GotoServer);
    add(L"GotoServerWithPos", &GlobalObject::GotoServerWithPos);
    add(L"ReturnToOriginServer", &GlobalObject::ReturnToOriginServer);
    add(L"IsRaidUser", &GlobalObject::IsRaidUser);
    add(L"IsVariationItem", &GlobalObject::IsVariationItem);
    add(L"IsEnchantedItem", &GlobalObject::IsEnchantedItem);
    add(L"AddEventCampaignPoint", &GlobalObject::AddEventCampaignPoint);
    add(L"IsPreviousOwner", &GlobalObject::IsPreviousOwner);
    add(L"IncreaseAP", &GlobalObject::IncreaseAP);
    add(L"ShowAPListWnd", &GlobalObject::ShowAPListWnd);
    add(L"ChairGroup_AssignParty", &GlobalObject::ChairGroup_AssignParty);
    add(L"ChairGroup_ClearParty", &GlobalObject::ChairGroup_ClearParty);
    add(L"IsDormantUser", &GlobalObject::IsDormantUser);
    add(L"IsNewbieUser", &GlobalObject::IsNewbieUser);
    add(L"IsDormantParty", &GlobalObject::IsDormantParty);
    add(L"IsNewbieParty", &GlobalObject::IsNewbieParty);
    add(L"IsDormantOrNewbieParty", &GlobalObject::IsDormantOrNewbieParty);
    add(L"GetLegendaryWorldID", &GlobalObject::GetLegendaryWorldID);
    add(L"SetMasterFriendNPC", &GlobalObject::SetMasterFriendNPC);
    add(L"GetMasterIDFriendNPC", &GlobalObject::GetMasterIDFriendNPC);
}

void GlobalObject::CollectVar(ScriptTypeInfo* typeInfo) {
    // Current L2NPC.exe 0x004497ac registers only this field.
    auto* const variable = new ScriptVarInfo;
    variable->SetScriptVarName(L"server_time");
    // Shipping offset: variable->Offset(0x88). Host containers use their native layout.
    variable->Offset(static_cast<int>(offsetof(GlobalObject, mServerTime)));
    variable->Type(&GetBuiltinScriptType(ScriptTypeId::Int));
    typeInfo->AddVarInfo(variable);
    typeInfo->SetCompiledOffset(L"server_time", 0x88);
}

bool GlobalObject::IsGlobalObjectInScript() const { return true; }

GlobalObject* GlobalObject::CastGlobalObjectInScript() { return this; }

ScriptAccessibleEx* GlobalObject::GetScriptAccessibleEx() const { return const_cast<GlobalObject*>(this); }

void GlobalObject::RegistCheckPCDie(l2npc::NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    m_CheckPCDieMakerList.push_back(maker);
}

int GlobalObject::GetEventValue(int index) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043be30.
    if (static_cast<unsigned int>(index) >= 5u) {
        return 0;
    }
    return mEventValue[static_cast<std::size_t>(index)];
}

bool GlobalObject::SetEventValue(int index, int value) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043bed8.
    if (static_cast<unsigned int>(index) >= 5u) {
        return false;
    }
    mEventValue[static_cast<std::size_t>(index)] = value;
    return true;
}

void GlobalObject::PrintVarInt(int, int, int, int, int, int, int, int) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x004372f8 contains only call tracing.
}

int GlobalObject::GetDynamicQuestGoalInfo(int, int) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00439c40.
    return 0;
}

int GlobalObject::GetDynamicQuestLivePoint(int) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00439ccc.
    return 0;
}

void GlobalObject::LoadJackpotTime(int requestId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00443b78:
    // NPCServer::Send("cd", 0xe7, requestId).
    g_ns.Send("cd", 0xe7, requestId);
    mJackpotTimes.clear();
    mJackpotTimeOver = false;
}

int GlobalObject::IsJackpotTime(int) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043f7c4.
    if (mJackpotTimeOver || mJackpotTimes.empty()) {
        return 0;
    }

    // The target feeds local calendar fields through
    // SystemTimeToFileTime, so jackpot schedules live in local-wall-clock
    // seconds instead of UTC Unix seconds.
    const TimeValue now = CurrentLocalWallClockTimeValue();
    std::int32_t index = 0;
    if (!mJackpotTimes.empty()) {
        do {
            const TimeValue scheduled = runtime::CheckedVectorAt(mJackpotTimes, static_cast<std::size_t>(index));
            const TimeValue difference = scheduled - now;
            if (difference.Sec() >= 0 && difference.Sec() < 60) {
                mJackpotTimeOver = true;
                return 1;
            }
            ++index;
        } while (static_cast<std::uint64_t>(static_cast<std::int64_t>(index)) < mJackpotTimes.size());
    }
    return 0;
}

l2npc::NpcMakerEx* GlobalObject::GetNpcMaker(const char16_t* name) {
    L2_GUARD_FUNCTION();
    if (!name || *name == u'\0') {
        // Noise: The old placeholder conversion served only the replaced invalid-name diagnostic.
        // const std::wstring logName = name
        //     ? Utf16ToWide(name)
        //     : std::wstring(L"name_null")
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Can't find maker[%s]",
        //     L"GlobalObject.cpp",
        //     0x170,
        //     logName.c_str())
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot look up an NPC maker: name is %s. Returning no maker.",
        //     name ? "empty" : "missing")
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot look up an NPC maker: name is %s; returning no maker.", name ? "empty" : "missing"
        );
        ReportScriptContext(LOG_ERROR);
        return nullptr;
    }
    l2npc::NpcMakerEx* const maker = l2npc::CNpcMakerDB::Inst().FindNpcMakerEx(std::u16string(name));
    return maker;
}

l2npc::NpcMakerEx* GlobalObject::InstantZone_GetNpcMaker(int inzoneId, const char16_t* name) {
    L2_GUARD_FUNCTION();
    if (!name || *name == u'\0') {
        // Log.Add(
        //     LOG_ERROR,
        //     L"InstantZone_GetNpcMaker. "
        //     L"inzone maker name null[%d]",
        //     inzoneId)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot look up an NPC maker in instance %d: name is %s. Returning no maker.",
        //     inzoneId, name ? "empty" : "missing")
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot look up an NPC maker in instance %d: name is %s; returning no maker.", inzoneId,
            name ? "empty" : "missing"
        );
        ReportScriptContext(LOG_ERROR);
        return nullptr;
    }

    l2npc::NpcMakerEx* maker = inzoneId == 0 ? l2npc::CNpcMakerDB::Inst().FindNpcMakerEx(std::u16string(name))
                                             : l2npc::CNpcMakerDB::Inst().GetInstantZoneMakerEx(inzoneId, name, true);
    if (!maker) {
        const std::string hostName = Utf16ToUtf8(name);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Can't find inzone maker[%d][%s]",
        //     L"GlobalObject.cpp",
        //     0x19b,
        //     inzoneId,
        //     hostName.c_str())
        // Log.Add(LOG_ERROR,
        //     "[NPC script] NPC maker '%s' is unavailable in instance %d. Returning no maker.",
        //     WideToUtf8(hostName).c_str(), inzoneId)
        L2_LOG_ERROR(
            Log, "[NPC script] NPC maker '%s' is unavailable in instance %d; returning no maker.",
            hostName.c_str(), inzoneId
        );
        ReportScriptContext(LOG_ERROR);
    }
    return maker;
}

Position* GlobalObject::GetTerritoryNpcPos(int inzoneId, const char16_t* makerName, int spawnDefinitionIndex) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x004429b8 consumes a Position from the thread-local
    // 32-object pool before validating any argument or maker lookup.
    Position* const result = Singleton<ThreadLocalStorage<TempObjectPool<Position, 32>, 8>>::inst()->Alloc();
    if (!makerName || *makerName == u'\0') {
        // Noise: The old placeholder conversion served only the replaced invalid-name diagnostic.
        // const std::wstring logName = makerName
        //     ? Utf16ToWide(makerName)
        //     : std::wstring(L"name_null")
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Can't find maker[%s]",
        //     L"GlobalObject.cpp",
        //     0x13e,
        //     logName.c_str())
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot read spawn position %d in instance %d: maker name is %s. Returning no position.",
        //     spawnDefinitionIndex, inzoneId, makerName ? "empty" : "missing")
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot read spawn position %d in instance %d: maker name is %s; returning no position.",
            spawnDefinitionIndex, inzoneId, makerName ? "empty" : "missing"
        );
        ReportScriptContext(LOG_ERROR);
        return nullptr;
    }

    l2npc::NpcMakerEx* const maker = inzoneId == 0
        ? l2npc::CNpcMakerDB::Inst().FindNpcMakerEx(std::u16string(makerName))
        : l2npc::CNpcMakerDB::Inst().GetInstantZoneMakerEx(inzoneId, makerName, true);
    NpcSpawnDefineEx* definition = nullptr;
    if (maker && spawnDefinitionIndex >= 0 &&
        static_cast<std::size_t>(spawnDefinitionIndex) <= maker->mSpawnDefineList.size()) {
        // The shipping comparison accepts index == size and
        // then performs the unchecked vector access.
        definition = runtime::CheckedVectorAt(maker->mSpawnDefineList, static_cast<std::size_t>(spawnDefinitionIndex));
    }
    if (!definition) {
        return nullptr;
    }

    l2npc::SpawnPos* const spawnPosition = definition->GetNpcSpawnPosition();
    result->m_X = spawnPosition->x;
    result->m_Y = spawnPosition->y;
    result->m_Z = spawnPosition->z;
    return result;
}

CSharedCreatureData* GlobalObject::GetCreatureFromID(int id) {
    L2_GUARD_FUNCTION();
    CCreature* object = CCreature::GetObjectW(static_cast<std::uint32_t>(id));
    return object ? object->m_data : nullptr;
}

CSharedCreatureData* GlobalObject::GetCreatureFromIndex(int index) {
    L2_GUARD_FUNCTION();
    if (index < 0 || index >= 120000) {
        return nullptr;
    }
    const int objectId = CCreatureFactory::Inst().GetIdByIndex(index);
    CCreature* smartObject = CCreature::GetObjectW(static_cast<std::uint32_t>(objectId));
    if (!smartObject) {
        return nullptr;
    }
    CSharedCreatureData* const shared = smartObject->m_data;
    return shared->m_bIsActive ? shared : nullptr;
}

CSharedPartyData* GlobalObject::GetParty(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    if (!creature) {
        return nullptr;
    }
    const int partyIndex = creature->m_nPartyIndex;
    if (partyIndex == -1) {
        return nullptr;
    }
    return CPartyFactory::Inst().GetData(partyIndex);
}

CSharedPartyData* GlobalObject::GetPartyFromID(int partyId) {
    L2_GUARD_FUNCTION();
    const std::uint32_t bits = static_cast<std::uint32_t>(partyId);
    // if ((bits & 0xf8000000u) != 0x68000000u) {
    if ((bits & CLASS_MASK) != (SCLS_PARTY << CLASS_SHIFT)) {
        return nullptr;
    }
    return CPartyFactory::Inst().GetData(static_cast<int>(bits & 0x000fffffu));
}

CSharedPartyData* GlobalObject::GetPartyFromEventRoom(int roomNumber, int partType) {
    L2_GUARD_FUNCTION();
    const int partyId = TimeAttackEventRoom::inst().GetParty(roomNumber, partType);
    if (partyId < 0) {
        return nullptr;
    }
    return CPartyFactory::Inst().GetData(static_cast<int>(static_cast<std::uint32_t>(partyId) & 0x000fffffu));
}

int GlobalObject::Party_GetCount(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x004406c8 performs the factory lookup directly. The
    // separately guarded GlobalObject::GetParty is not part of this path.
    if (!creature) {
        return 0;
    }
    const int partyIndex = creature->m_nPartyIndex;
    if (partyIndex == -1) {
        return 0;
    }
    CSharedPartyData* const party = CPartyFactory::Inst().GetData(partyIndex);
    return party ? static_cast<int>(party->m_nOnlineMemberCount) : 0;
}

CSharedCreatureData* GlobalObject::Party_GetCreature(CSharedCreatureData* creature, int memberIndex) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x004407d8 reads the party index here directly. It
    // enters neither GetParty nor GetCreatureFromIndex, so neither helper's
    // additional guard/active check is part of this call path.
    if (!creature) {
        return nullptr;
    }
    const int partyIndex = creature->m_nPartyIndex;
    if (partyIndex == -1) {
        return nullptr;
    }
    CSharedPartyData* const party = CPartyFactory::Inst().GetData(partyIndex);
    if (!party) {
        return nullptr;
    }

    int logicalIndex = 0;
    for (const IdIndexPair& member : party->m_OnlineMember) {
        if (member.id == 0) {
            continue;
        }
        if (logicalIndex == memberIndex) {
            return CCreatureFactory::Inst().GetData(static_cast<int>(member.index));
        }
        ++logicalIndex;
    }
    return nullptr;
}

CSharedCreatureData* GlobalObject::Party_GetLeader(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00440960 reads the party index here directly. It
    // does not enter the separately guarded GlobalObject::GetParty method.
    if (!creature) {
        return nullptr;
    }
    const int partyIndex = creature->m_nPartyIndex;
    if (partyIndex == -1) {
        return nullptr;
    }
    CSharedPartyData* const party = CPartyFactory::Inst().GetData(partyIndex);
    if (!party || party->m_OnlineMaster.id == 0) {
        return nullptr;
    }
    return CCreatureFactory::Inst().GetData(static_cast<int>(party->m_OnlineMaster.index));
}

int GlobalObject::DropItem3(CSharedCreatureData* creature, int itemClassId, std::int64_t count) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00440ac4. Unlike the other atomic-job producers,
    // this method maps the shared index to the SID first, verifies that the
    // SID names a live CNPC, and then also uses that SID as the compositor
    // key. That unusual second use is present in both the 2015 donor and the
    // shipping 2018 machine code.
    if (!creature || count <= 0 || itemClassId == 0) {
        return 0;
    }

    const int objectId = CCreatureFactory::Inst().GetIdByIndex(creature->index);
    if (!CNPC::GetObjectW(objectId)) {
        return 0;
    }

    AtomicDropItem* job = nullptr;
    void* const storage = AtomicJob::operator new(sizeof(AtomicDropItem));
    if (storage) {
        FVector position = creature->GetPos();
        try {
            job = ::new (storage) AtomicDropItem(objectId, itemClassId, count, position);
        } catch (...) {
            AtomicJob::operator delete(storage, true);
            throw;
        }
    }
    CScriptEngine::AddAtomicJob(objectId, job);
    return 1;
}

void GlobalObject::ChairGroup_AssignParty(CSharedCreatureData* creature, int chairGroupId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00441130.
    if (!creature) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid creature",
        //     L"GlobalObject.cpp",
        //     0x11d5)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot assign a party to chair group %d: source creature is missing. Request not sent.",
        //     chairGroupId)
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot assign a party to chair group %d: source creature is missing; request not sent.",
            chairGroupId
        );
        ReportScriptContext(LOG_ERROR);
        return;
    }
    CSharedPartyData* const party = GetParty(creature);
    if (!party) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid party",
        //     L"GlobalObject.cpp",
        //     0x11de)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot assign a party to chair group %d: creature %u has no available party. Request not sent.",
        //     chairGroupId, static_cast<unsigned int>(creature->m_nId))
        L2_LOG_ERROR(
            Log,
            "[NPC script] Cannot assign a party to chair group %d: creature %u has no available party; request not sent.",
            chairGroupId, static_cast<unsigned int>(creature->m_nId)
        );
        ReportScriptContext(LOG_ERROR);
        return;
    }

    g_ns.Send("chccdd", 0xfe, 0x24, 1, 1, chairGroupId, party->m_nId);
}

void GlobalObject::ChairGroup_ClearParty(int chairGroupId1, int chairGroupId2, int chairGroupId3, int chairGroupId4) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043d824.
    g_ns.Send("chccdddd", 0xfe, 0x24, 1, 2, chairGroupId1, chairGroupId2, chairGroupId3, chairGroupId4);
}

int GlobalObject::AddPartyToEventRoom(int roomNumber, int partType, int partySid) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00446d10.
    const bool inserted = TimeAttackEventRoom::inst().AddParty(roomNumber, partType, partySid);
    if (inserted) {
        g_ns.Send("cddd", 0x6b, roomNumber, partType, partySid);
    }
    return inserted ? 1 : 0;
}

int GlobalObject::ClearEventRoom(int roomNumber, int partType, int parameter) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00446e00.
    const int partySid = TimeAttackEventRoom::inst().Clear(roomNumber, partType);
    if (partySid < 0) {
        return 0;
    }
    g_ns.Send("cdddd", 0x6c, roomNumber, partType, partySid, parameter);
    return partySid;
}

char16_t* GlobalObject::GetRank_RimKamaroka(int oneBasedRank, int fieldType) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043a434.
    const int index = runtime::Int32FromBits(static_cast<std::uint32_t>(oneBasedRank) - 1u);
    SharedRimPointInfo* const info = SharedRimPointInfo::GetInfo();
    if (static_cast<std::uint64_t>(static_cast<std::int64_t>(index)) >= info->nRankingCount ||
        static_cast<unsigned int>(index) >= SharedRimPointInfo::RANKING_COUNT) {
        return EmptyScriptString();
    }

    sRimRankInfo& rank = info->arrRankingList[index];
    switch (fieldType) {
        case 1:
            return IntToStr(rank.nRanking);
        case 2:
            return rank.sName;
        case 3:
            return IntToStr(rank.nType);
        case 4:
            return IntToStr(rank.nPoint);
        case 5:
            return IntToStr(rank.nLevel);
        default:
            return EmptyScriptString();
    }
}

void GlobalObject::SetPoint_RimKamaroka(CSharedCreatureData* creature, int type, int point) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043a564.
    if (!creature) {
        return;
    }
    g_ns.Send("cddd", 0xb9, creature->m_nId, type, point);
}

CSharedGeneralObjectData* GlobalObject::GetMPCC(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    if (!creature) {
        return nullptr;
    }
    // Current L2NPC.exe 0x0043fe98 reads the shared handle for the zero gate
    // and then reads it again for the masked factory index.
    if (creature->m_nMPCCId == 0) {
        return nullptr;
    }
    return CGeneralObjectFactory::Inst().GetData(
        static_cast<int>(static_cast<std::uint32_t>(creature->m_nMPCCId) & 0x000fffffu)
    );
}

int GlobalObject::GetSSQPart(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00437374.
    if (!creature) {
        return 0;
    }
    // The image performs two independent singleton lookups and treats the
    // published shared record as an invariant once the creature is valid.
    return SharedSSQInfo::GetInfo()->mCurrentStatus != 0 &&
            SharedSSQInfo::GetInfo()->mRoundNumber == creature->m_nSSQRoundNumber
        ? creature->m_nSSQPart
        : 0;
}

void GlobalObject::GotoServerWithPos(CSharedCreatureData* creature, int worldId, int x, int y, int z) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043cf98. RaidServerId is the process-wide
    // CConfig value at DAT_006af99c, not CNPC state.
    if (!creature) {
        return;
    }
    if (g_nRaidServerID >= 0) {
        worldId = g_nRaidServerID;
    }

    g_ns.Send("chddddd", 0xfe, 0x14, creature->m_nId, worldId, x, y, z);
}

void GlobalObject::GotoServer(CSharedCreatureData* creature, int worldId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043fa00 reads RaidServerId here and again in the
    // nested GotoServerWithPos call.
    if (!creature) {
        return;
    }
    if (g_nRaidServerID >= 0) {
        worldId = g_nRaidServerID;
    }
    const FVector position = creature->GetPos();
    // The Win64 image converts z, y and x in that order before entering the
    // separately guarded GotoServerWithPos wrapper.
    const int z = runtime::Win64CvttDoubleToInt32(position.z);
    const int y = runtime::Win64CvttDoubleToInt32(position.y);
    const int x = runtime::Win64CvttDoubleToInt32(position.x);
    GotoServerWithPos(creature, worldId, x, y, z);
}

void GlobalObject::ReturnToOriginServer(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043d098.
    if (!creature) {
        return;
    }
    // The shipping call provides no argument for the final `d`. Keep
    // that mixed-width vararg bug instead of normalizing the format.
    g_ns.Send("chdd", 0xfe, 0x15, creature->m_nId);
}

bool GlobalObject::IsRaidUser(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043d160 compares m_nWorldID with CConfig's
    // process-wide [World]/WorldId value at DAT_006afcd4.
    return creature && creature->GetWorldId() != g_nWorldId;
}

void GlobalObject::IncreaseAP(CSharedCreatureData* creature, int amount) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043d63c.
    if (!creature) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid creature",
        //     L"GlobalObject.cpp",
        //     0x11b7)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot send IncreaseAP(%d): recipient creature is missing. Request not sent.",
        //     amount)
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot send IncreaseAP(%d): recipient creature is missing; request not sent.", amount
        );
        ReportScriptContext(LOG_ERROR);
        return;
    }
    g_ns.Send("chdd", 0xfe, 0x17, creature->m_nId, amount);
}

void GlobalObject::ShowAPListWnd(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043d738.
    if (!creature) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid creature",
        //     L"GlobalObject.cpp",
        //     0x11c6)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot show the AP list window: recipient creature is missing, request not sent.")
        L2_LOG_ERROR(Log, "[NPC script] Cannot show the AP list window: recipient creature is missing; request not sent.");
        return;
    }
    g_ns.Send("chd", 0xfe, 0x18, creature->m_nId);
}

bool GlobalObject::IsDormantUser(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043d914.
    return creature &&
        // creature->m_nDormantType == 3.
        creature->m_nDormantType == DormantConst::DT_DORMANT;
}

bool GlobalObject::IsNewbieUser(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043d9b0.
    return creature &&
        // creature->m_nDormantType == 2.
        creature->m_nDormantType == DormantConst::DT_NEWBIE;
}

bool GlobalObject::IsDormantParty(CSharedPartyData* party) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0044193c.
    if (!party || party->m_nOnlineMemberCount == 0) {
        return false;
    }
    std::uint32_t index = 0;
    do {
        CSPointer<CCreature> creature =
            CCreatureFactory::Inst().FindObjectSP(static_cast<int>(party->m_OnlineMember[index].index));
        if (creature && creature->IsPC() &&
            // creature->m_data->m_nDormantType == 3) {
            creature->m_data->m_nDormantType == DormantConst::DT_DORMANT) {
            return true;
        }
        ++index;
    } while (index < party->m_nOnlineMemberCount);
    return false;
}

bool GlobalObject::IsNewbieParty(CSharedPartyData* party) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00441af0.
    if (!party || party->m_nOnlineMemberCount == 0) {
        return false;
    }
    std::uint32_t index = 0;
    do {
        CSPointer<CCreature> creature =
            CCreatureFactory::Inst().FindObjectSP(static_cast<int>(party->m_OnlineMember[index].index));
        if (creature && creature->IsPC() &&
            // creature->m_data->m_nDormantType == 2) {
            creature->m_data->m_nDormantType == DormantConst::DT_NEWBIE) {
            return true;
        }
        ++index;
    } while (index < party->m_nOnlineMemberCount);
    return false;
}

bool GlobalObject::IsDormantOrNewbieParty(CSharedPartyData* party) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00441cb0.
    if (!party || party->m_nOnlineMemberCount == 0) {
        return false;
    }
    std::uint32_t index = 0;
    do {
        CSPointer<CCreature> creature =
            CCreatureFactory::Inst().FindObjectSP(static_cast<int>(party->m_OnlineMember[index].index));
        if (creature && creature->IsPC() &&
            // (creature->m_data->m_nDormantType == 3 ||
            (creature->m_data->m_nDormantType == DormantConst::DT_DORMANT ||
                // creature->m_data->m_nDormantType == 2)) {
                creature->m_data->m_nDormantType == DormantConst::DT_NEWBIE)) {
            return true;
        }
        ++index;
    } while (index < party->m_nOnlineMemberCount);
    return false;
}

int GlobalObject::GetMasterIDFriendNPC(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043fae4.
    return creature ? creature->m_pMaster.id : 0;
}

bool GlobalObject::SetMasterFriendNPC(CSharedCreatureData* master, CSharedCreatureData* npc) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00441288.
    if (!master) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] pMasterUser is NULL",
        //     L"GlobalObject.cpp",
        //     0x126f)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot assign a FriendNPC master: master creature is missing, assignment unchanged.")
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot assign a FriendNPC master: master creature is missing; assignment unchanged."
        );
        return false;
    }

    CCreature* const liveMaster = CCreatureFactory::Inst().GetObjectW(master->index);
    if (!liveMaster || !liveMaster->m_data->m_bIsUser || !liveMaster->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Master User Set failed. invalid user",
        //     L"GlobalObject.cpp",
        //     0x1276)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot assign creature %u as a FriendNPC master: %s. Assignment unchanged.",
        //     static_cast<unsigned int>(master->m_nId), liveMaster ? "object is not a player" : "object is no longer
        //     registered")
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot assign creature %u as a FriendNPC master: %s; assignment unchanged.",
            static_cast<unsigned int>(master->m_nId),
            liveMaster ? "object is not a player" : "object is no longer registered"
        );
        return false;
    }
    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] pNPC is NULL",
        //     L"GlobalObject.cpp",
        //     0x127c)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot assign player %u as a FriendNPC master: NPC is missing. Assignment unchanged.",
        //     static_cast<unsigned int>(master->m_nId))
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot assign player %u as a FriendNPC master: NPC is missing; assignment unchanged.",
            static_cast<unsigned int>(master->m_nId)
        );
        return false;
    }
    if (npc->m_Race != 0x1d) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] pNPC is not FriendNPC",
        //     L"GlobalObject.cpp",
        //     0x1282)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot assign player %u as master of NPC %u: race %d is not the FriendNPC race 29. Assignment
        //     unchanged.", static_cast<unsigned int>(master->m_nId), static_cast<unsigned int>(npc->m_nId),
        //     static_cast<int>(npc->m_Race))
        L2_LOG_ERROR(
            Log,
            "[NPC script] Cannot assign player %u as master of NPC %u: race %d is not the FriendNPC race 29; assignment "
            "unchanged.",
            static_cast<unsigned int>(master->m_nId), static_cast<unsigned int>(npc->m_nId), static_cast<int>(npc->m_Race)
        );
        return false;
    }

    npc->m_spMaster = master;
    g_ns.Send("chdd", 0xfe, 0x39, master->m_nId, npc->m_nId);
    return true;
}

void GlobalObject::AddPCSocial(int sharedCreatureIndex, int socialAction) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00440c3c resolves the live CCreature through
    // L2World_CreatureIndex and requires virtual IsPC, shared active/alive,
    // and an action in the exact inclusive range 1..29.
    CCreature* const creature = CCreatureFactory::Inst().GetObjectW(sharedCreatureIndex);
    if (!creature || !creature->IsPC() || !creature->m_data->m_bIsActive || !creature->m_data->m_bIsAlive ||
        static_cast<std::uint32_t>(socialAction) - 1u >= 29u) {
        return;
    }

    g_ns.Send("cdd", 0x93, static_cast<int>(creature->Id()), socialAction);
}

void GlobalObject::SendDominionScriptEvent(int eventId, int firstArgument, int secondArgument) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00435efc.
    if (!g_DominionNPC) {
        return;
    }
    g_ns.Send("cddd", 0xc9, eventId, firstArgument, secondArgument);
}

void GlobalObject::Area_SetOnOff(char16_t* areaName, int onOff) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00436130. The cSdd format always includes an
    // inzone id. This non-Ex entry places zero in that slot.
    if (!areaName || *areaName == u'\0') {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid area name onoff[%d]",
        //     L"GlobalObject.cpp",
        //     0x34b,
        //     onOff)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot set area switch to %d in the base world: name is %s. Request not sent.",
        //     onOff, areaName ? "empty" : "missing")
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot set area switch to %d in the base world: name is %s; request not sent.", onOff,
            areaName ? "empty" : "missing"
        );
        ReportScriptContext(LOG_ERROR);
        return;
    }
    g_ns.Send("cSdd", 0x5f, areaName, onOff, 0);
}

void GlobalObject::Area_SetOnOffEx(char16_t* areaName, int onOff, int inzoneId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00436234.
    if (!areaName || *areaName == u'\0') {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid area name onoff[%d] "
        //     L"inzone[%d]",
        //     L"GlobalObject.cpp",
        //     0x46f,
        //     onOff,
        //     inzoneId)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot set area switch to %d in instance %d: name is %s. Request not sent.",
        //     onOff, inzoneId, areaName ? "empty" : "missing")
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot set area switch to %d in instance %d: name is %s; request not sent.", onOff, inzoneId,
            areaName ? "empty" : "missing"
        );
        ReportScriptContext(LOG_ERROR);
        return;
    }
    g_ns.Send("cSdd", 0x5f, areaName, onOff, inzoneId);
}

void GlobalObject::Area_SetBannedTerritoryOnOff(char16_t* areaName, int onOff, int territoryId, int inzoneId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00436348.
    if (!areaName || *areaName == u'\0') {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid area name onoff[%d] "
        //     L"territory[%d] inzone[%d]",
        //     L"GlobalObject.cpp",
        //     0x484,
        //     onOff,
        //     territoryId,
        //     inzoneId)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot set banned territory %d switch to %d in instance %d: area name is %s. Request not sent.",
        //     territoryId, onOff, inzoneId, areaName ? "empty" : "missing")
        L2_LOG_ERROR(
            Log,
            "[NPC script] Cannot set banned territory %d switch to %d in instance %d: area name is %s; request not sent.",
            territoryId, onOff, inzoneId, areaName ? "empty" : "missing"
        );
        ReportScriptContext(LOG_ERROR);
        return;
    }
    g_ns.Send("cSddd", 0xa5, areaName, onOff, territoryId, inzoneId);
}

void GlobalObject::Castle_GateOpenClose2(char16_t* doorName, int openClose) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043646c. As with Area_SetOnOff, cSdd carries a
    // zero inzone id for the non-Ex entry.
    if (!doorName || *doorName == u'\0') {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid door name openclose[%d]",
        //     L"GlobalObject.cpp",
        //     0x49a,
        //     openClose)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot send door open/close value %d in the base world: door name is %s. Request not sent.",
        //     openClose, doorName ? "empty" : "missing")
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot send door open/close value %d in the base world: door name is %s; request not sent.",
            openClose, doorName ? "empty" : "missing"
        );
        ReportScriptContext(LOG_ERROR);
        return;
    }
    g_ns.Send("cSdd", 0x3c, doorName, openClose, 0);
}

void GlobalObject::Castle_GateOpenCloseEx(char16_t* doorName, int openClose, int inzoneId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00436570.
    if (!doorName || *doorName == u'\0') {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid door name openclose[%d] "
        //     L"nInZoneID[%d]",
        //     L"GlobalObject.cpp",
        //     0x4af,
        //     openClose,
        //     inzoneId)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot send door open/close value %d in instance %d: door name is %s. Request not sent.",
        //     openClose, inzoneId, doorName ? "empty" : "missing")
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot send door open/close value %d in instance %d: door name is %s; request not sent.",
            openClose, inzoneId, doorName ? "empty" : "missing"
        );
        ReportScriptContext(LOG_ERROR);
        return;
    }
    g_ns.Send("cSdd", 0x3c, doorName, openClose, inzoneId);
}

void GlobalObject::InzoneDoorBreakable(char16_t* doorName, int inzoneId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00436684.
    if (!doorName || *doorName == u'\0') {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid door name nInZoneID[%d]",
        //     L"GlobalObject.cpp",
        //     0x4c4,
        //     inzoneId)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot make a door breakable in instance %d: door name is %s. Request not sent.",
        //     inzoneId, doorName ? "empty" : "missing")
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot make a door breakable in instance %d: door name is %s; request not sent.", inzoneId,
            doorName ? "empty" : "missing"
        );
        ReportScriptContext(LOG_ERROR);
        return;
    }
    g_ns.Send("cSd", 0xc3, doorName, inzoneId);
}

void GlobalObject::SetNpcParam(CSharedCreatureData* creature, int parameterType, float value) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00438c78.
    if (!creature || static_cast<unsigned int>(parameterType) > 0x28u) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] SetNpcParam failed. invalid argument. "
        //     L"c == null or nParamType < VCP_CLASS or "
        //     L"nParamType > VCP_MAX",
        //     L"GlobalObject.cpp",
        //     0x97d)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot set NPC parameter %d to %.9g: %s. Request not sent.",
        //     parameterType, static_cast<double>(value), creature ? "parameter type is outside 0..40" : "recipient creature
        //     is missing")
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot set NPC parameter %d to %.9g: %s; request not sent.", parameterType,
            static_cast<double>(value), creature ? "parameter type is outside 0..40" : "recipient creature is missing"
        );
        return;
    }
    g_ns.Send("cddf", 0x89, creature->m_nId, parameterType, static_cast<double>(value));
}

void GlobalObject::AddLog(int selector, CSharedCreatureData* creature, int argument) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x004458ac.
    if (!creature) {
        return;
    }

    const FVector position = creature->GetPos();

    switch (selector) {
        case 1:
        case 2:
        case 3: {
            static constexpr int logTypes[] = {
                0x12f,
                0x131,
                0x134,
            };
            const int z = runtime::Win64CvttDoubleToInt32(position.z);
            const int y = runtime::Win64CvttDoubleToInt32(position.y);
            const int x = runtime::Win64CvttDoubleToInt32(position.x);
            const std::wstring name = Utf16ToWide(std::u16string_view(creature->Name()));
            Log.Add(
                LOG_BOTH,
                u"%I64d,%I64d,,,,%I64d,%I64d,%I64d,,,,"
                u"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,,,,",
                LogInteger(logTypes[selector - 1]), LogInteger(creature->m_nDBID), LogInteger(x), LogInteger(y),
                LogInteger(z), LogInteger(creature->m_Race), LogInteger(creature->m_Sex),
                LogInteger(static_cast<int>(creature->m_nClass)), LogInteger(creature->m_nLevel), LogInteger(argument),
                name.c_str()
            );
            return;
        }

        case 4: {
            // The shipping order is observable because all four helpers
            // read mutable shared state.
            const int ssqPart = GetSSQPart(creature);
            const std::int64_t itemCount = OwnItemCount(creature, 0x170d);
            const int memoState = GetMemoStateEx(creature, 0x1f9, 1);
            const int partyCount = Party_GetCount(creature);
            if (itemCount <= 0 || memoState <= 0 || partyCount <= 0) {
                return;
            }
            const int z = runtime::Win64CvttDoubleToInt32(position.z);
            const int y = runtime::Win64CvttDoubleToInt32(position.y);
            const int x = runtime::Win64CvttDoubleToInt32(position.x);
            const std::wstring name = Utf16ToWide(std::u16string_view(creature->Name()));
            Log.Add(
                LOG_BOTH,
                u"%I64d,%I64d,,,,%I64d,%I64d,%I64d,,,,"
                u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
                u"%I64d,%I64d,,,%s,,,,",
                LogInteger(0x13a), LogInteger(creature->m_nDBID), LogInteger(x), LogInteger(y), LogInteger(z),
                LogInteger(creature->m_Race), LogInteger(creature->m_Sex), LogInteger(static_cast<int>(creature->m_nClass)),
                LogInteger(creature->m_nLevel), LogInteger(ssqPart), LogInteger(itemCount), LogInteger(memoState),
                LogInteger(partyCount), name.c_str()
            );
            return;
        }

        case 5: {
            const int z = runtime::Win64CvttDoubleToInt32(position.z);
            const int y = runtime::Win64CvttDoubleToInt32(position.y);
            const int x = runtime::Win64CvttDoubleToInt32(position.x);
            const std::wstring name = Utf16ToWide(std::u16string_view(creature->Name()));
            Log.Add(
                LOG_BOTH,
                u"%I64d,%I64d,,,,%I64d,%I64d,%I64d,,,,"
                u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,,,,",
                LogInteger(0x13f), LogInteger(creature->m_nDBID), LogInteger(x), LogInteger(y), LogInteger(z),
                LogInteger(creature->m_Race), LogInteger(creature->m_Sex), LogInteger(static_cast<int>(creature->m_nClass)),
                LogInteger(creature->m_nLevel), LogInteger(argument / 1000), LogInteger(argument % 1000), name.c_str()
            );
            return;
        }

        case 6: {
            const std::wstring name = Utf16ToWide(std::u16string_view(creature->Name()));
            Log.Add(
                LOG_AUDIT, u"%I64d,%I64d,,,,,,,,,,%I64d,,,,,,,,,,%s,,,,", LogInteger(0x593), LogInteger(creature->m_nDBID),
                LogInteger(argument), name.c_str()
            );
            return;
        }

        case 8:
        case 9: {
            CSharedPartyData* const party = GetParty(creature);
            if (!party) {
                return;
            }
            if (party->m_nOnlineMemberCount == 0) {
                return;
            }

            std::uint32_t index = 0;
            do {
                CSPointer<CCreature> member =
                    CCreatureFactory::Inst().FindObjectSP(static_cast<int>(party->m_OnlineMember[index].index));
                if (!member || !member->IsPC()) {
                    ++index;
                    continue;
                }
                // The current Win64 body performs three separate shared
                // position reads in right-to-left argument order.
                const int memberZ = runtime::Win64CvttDoubleToInt32(member->m_data->GetPos().z);
                const int memberY = runtime::Win64CvttDoubleToInt32(member->m_data->GetPos().y);
                const int memberX = runtime::Win64CvttDoubleToInt32(member->m_data->GetPos().x);
                const std::wstring memberName = Utf16ToWide(member->GetName());
                if (selector == 8) {
                    Log.Add(
                        LOG_BOTH,
                        u"%I64d,%I64d,,,,%I64d,%I64d,%I64d,,,,"
                        u"%I64d,%I64d,%I64d,%I64d,,,%I64d,"
                        u"%I64d,,,%s,,,,",
                        LogInteger(0x142), LogInteger(member->m_data->m_nDBID), LogInteger(memberX), LogInteger(memberY),
                        LogInteger(memberZ), LogInteger(member->m_data->m_Race), LogInteger(member->m_data->m_Sex),
                        LogInteger(static_cast<int>(member->m_data->m_nClass)), LogInteger(member->m_data->m_nLevel),
                        LogInteger(party->m_nId), LogInteger(argument), memberName.c_str()
                    );
                } else {
                    Log.Add(
                        LOG_BOTH,
                        u"%I64d,%I64d,,,,%I64d,%I64d,%I64d,,,,"
                        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
                        u"%I64d,,,,%s,,,,",
                        LogInteger(0x143), LogInteger(member->m_data->m_nDBID), LogInteger(memberX), LogInteger(memberY),
                        LogInteger(memberZ), LogInteger(member->m_data->m_Race), LogInteger(member->m_data->m_Sex),
                        LogInteger(static_cast<int>(member->m_data->m_nClass)), LogInteger(member->m_data->m_nLevel),
                        LogInteger(argument), LogInteger(party->m_nOnlineMemberCount), LogInteger(party->m_nId),
                        memberName.c_str()
                    );
                }
                ++index;
            } while (index < party->m_nOnlineMemberCount);
            return;
        }

        case 10: {
            if (creature->m_nPledgeID == 0 || runtime::Int32FromBits(creature->m_nPledgeIndex) == -1) {
                return;
            }
            CSharedPledgeData* const pledge = CPledgeFactory::Inst().GetData(creature->m_nPledgeIndex);
            if (!pledge || pledge->m_OnlineMaster.id == 0) {
                return;
            }
            CSharedCreatureData* const master =
                CCreatureFactory::Inst().GetData(static_cast<int>(pledge->m_OnlineMaster.index));

            const int currentZ = runtime::Win64CvttDoubleToInt32(creature->GetPos().z);
            const int currentY = runtime::Win64CvttDoubleToInt32(creature->GetPos().y);
            const int currentX = runtime::Win64CvttDoubleToInt32(creature->GetPos().x);

            const std::wstring pledgeName = FixedUtf16ToWide(pledge->m_sPledgeName);
            const std::wstring masterName = Utf16ToWide(std::u16string_view(master->Name()));
            const std::wstring name = Utf16ToWide(std::u16string_view(creature->Name()));
            Log.Add(
                LOG_BOTH,
                u"%I64d,%I64d,,%I64d,,%I64d,%I64d,%I64d,,"
                u"%s,,%I64d,%I64d,,,,,,,,,%s,,%s,,",
                LogInteger(0x122), LogInteger(creature->m_nDBID), LogInteger(master->m_nDBID), LogInteger(currentX),
                LogInteger(currentY), LogInteger(currentZ), pledgeName.c_str(), LogInteger(pledge->m_nDbId),
                LogInteger(argument), name.c_str(), masterName.c_str()
            );
            return;
        }

        case 0x145: {
            const int currentZ = runtime::Win64CvttDoubleToInt32(creature->GetPos().z);
            const int currentY = runtime::Win64CvttDoubleToInt32(creature->GetPos().y);
            const int currentX = runtime::Win64CvttDoubleToInt32(creature->GetPos().x);
            const std::wstring name = Utf16ToWide(std::u16string_view(creature->Name()));
            Log.Add(
                LOG_BOTH,
                u"%I64d,%I64d,,,,%I64d,%I64d,%I64d,,,,"
                u"%I64d,,,,,,,,,,%s,,,,",
                LogInteger(0x145), LogInteger(creature->m_nDBID), LogInteger(currentX), LogInteger(currentY),
                LogInteger(currentZ), LogInteger(argument), name.c_str()
            );
            return;
        }

        case 0x150: {
            const int currentZ = runtime::Win64CvttDoubleToInt32(creature->GetPos().z);
            const int currentY = runtime::Win64CvttDoubleToInt32(creature->GetPos().y);
            const int currentX = runtime::Win64CvttDoubleToInt32(creature->GetPos().x);
            CLogFormatter formatter;
            formatter.WriteLog(
                0x150, creature->m_nDBID, 0, 0, 0, currentX, currentY, currentZ, nullptr, nullptr, nullptr, creature->m_Race,
                creature->m_Sex, static_cast<int>(creature->m_nClass), creature->m_nLevel, argument, 0, 0, 0, 0, 0,
                creature->Name(), nullptr, nullptr, nullptr, 0
            );
            return;
        }

        default:
            return;
    }
}

void GlobalObject::AddLogEx(int selector, CSharedCreatureData* creature, std::int64_t argument1, std::int64_t argument2) {
    // Current L2NPC.exe 0x004442c0. The current public symbols omit its
    // name, but the embedded GuardHelper signature identifies the method.
    L2_GUARD_FUNCTION();
    if (!creature) {
        return;
    }

    const FVector position = creature->GetPos();
    const int lowArgument1 = static_cast<std::int32_t>(argument1);
    const int lowArgument2 = static_cast<std::int32_t>(argument2);

    if (selector == 1) {
        CInventory inventory;
        inventory.SetIndex(creature->index, creature->m_bIsUser);
        const std::int64_t resultingItemCount =
            runtime::AddInt64Wrapping(inventory.GetItemAmount(0x19fb, -1, true), argument2);
        const int z = runtime::Win64CvttDoubleToInt32(position.z);
        const int y = runtime::Win64CvttDoubleToInt32(position.y);
        const int x = runtime::Win64CvttDoubleToInt32(position.x);
        const std::wstring name = Utf16ToWide(std::u16string_view(creature->Name()));
        Log.Add(
            LOG_BOTH,
            u"%I64d,%I64d,,,,%I64d,%I64d,%I64d,,,,"
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
            u"%I64d,,,,%s,,,,",
            LogInteger(0x3ca), LogInteger(creature->m_nDBID), LogInteger(x), LogInteger(y), LogInteger(z),
            LogInteger(creature->m_Race), LogInteger(creature->m_Sex), LogInteger(static_cast<int>(creature->m_nClass)),
            LogInteger(creature->m_nLevel), LogInteger(lowArgument1), LogInteger(lowArgument2),
            LogInteger(resultingItemCount), name.c_str()
        );
        return;
    }

    if (selector == 7) {
        CSharedPartyData* const party = GetParty(creature);
        if (!party) {
            return;
        }
        if (party->m_nOnlineMemberCount == 0) {
            return;
        }

        std::uint32_t index = 0;
        do {
            CSPointer<CCreature> member =
                CCreatureFactory::Inst().FindObjectSP(static_cast<int>(party->m_OnlineMember[index].index));
            if (!member || !member->IsPC()) {
                ++index;
                continue;
            }
            const int memberZ = runtime::Win64CvttDoubleToInt32(member->m_data->GetPos().z);
            const int memberY = runtime::Win64CvttDoubleToInt32(member->m_data->GetPos().y);
            const int memberX = runtime::Win64CvttDoubleToInt32(member->m_data->GetPos().x);
            const std::wstring memberName = Utf16ToWide(member->GetName());
            Log.Add(
                LOG_BOTH,
                u"%I64d,%I64d,,,,%I64d,%I64d,%I64d,,,,"
                u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
                u"%I64d,%I64d,,,%s,,,,",
                LogInteger(0x141), LogInteger(member->m_data->m_nDBID), LogInteger(memberX), LogInteger(memberY),
                LogInteger(memberZ), LogInteger(member->m_data->m_Race), LogInteger(member->m_data->m_Sex),
                LogInteger(static_cast<int>(member->m_data->m_nClass)), LogInteger(member->m_data->m_nLevel),
                LogInteger(lowArgument1), LogInteger(party->m_nOnlineMemberCount), LogInteger(party->m_nId),
                LogInteger(argument2), memberName.c_str()
            );
            ++index;
        } while (index < party->m_nOnlineMemberCount);
        return;
    }

    if (selector == 0x6f || selector == 0x70) {
        const int z = runtime::Win64CvttDoubleToInt32(position.z);
        const int y = runtime::Win64CvttDoubleToInt32(position.y);
        const int x = runtime::Win64CvttDoubleToInt32(position.x);
        CLogFormatter formatter;
        formatter.WriteLog(
            0x368, creature->m_nDBID, 0, 0, 0, x, y, z, nullptr, nullptr, nullptr, creature->m_Race, creature->m_Sex,
            static_cast<int>(creature->m_nClass), creature->m_nLevel, 0, selector == 0x70 ? 1 : 0,
            SignedHighDword(argument1), argument2, 0, 0, creature->Name(), nullptr, nullptr, nullptr, 0
        );
        return;
    }

    if (selector == 0x144 || (selector >= 1001 && selector <= 1018)) {
        const int logType = selector == 0x144 ? 0x144 : 0x7b;
        const int first = selector == 0x144 ? lowArgument1 : selector - 1000;
        const int second = selector == 0x144 ? lowArgument2 : lowArgument1;
        const int z = runtime::Win64CvttDoubleToInt32(position.z);
        const int y = runtime::Win64CvttDoubleToInt32(position.y);
        const int x = runtime::Win64CvttDoubleToInt32(position.x);
        const std::wstring name = Utf16ToWide(std::u16string_view(creature->Name()));
        Log.Add(
            LOG_BOTH,
            u"%I64d,%I64d,,,,%I64d,%I64d,%I64d,,,,"
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,,,,",
            LogInteger(logType), LogInteger(creature->m_nDBID), LogInteger(x), LogInteger(y), LogInteger(z),
            LogInteger(creature->m_Race), LogInteger(creature->m_Sex), LogInteger(static_cast<int>(creature->m_nClass)),
            LogInteger(creature->m_nLevel), LogInteger(first), LogInteger(second), name.c_str()
        );
    }
}

void GlobalObject::AddLogExWithoutCreature(int selector, int argument1, int argument2) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043f114.
    if (selector == 1001 || selector == 1002) {
        Log.Add(
            LOG_BOTH,
            u"%I64d,%I64d,,,,,,,,,,%I64d,%I64d,%I64d,"
            u"%I64d,%I64d,%I64d,,,,,%s,,,,",
            LogInteger(0x82), LogInteger(argument1), LogInteger(argument2), LogInteger(0), LogInteger(0), LogInteger(0),
            LogInteger(selector), LogInteger(0), u""
        );
        return;
    }

    if (selector < 1003 || selector > 1018) {
        return;
    }

    const bool special = selector == 1014;
    Log.Add(
        LOG_BOTH,
        u"%I64d,%I64d,,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,,,,",
        LogInteger(0x7b), LogInteger(0), LogInteger(0), LogInteger(0), LogInteger(0), LogInteger(special ? argument1 : 0),
        LogInteger(0), LogInteger(0), LogInteger(0), LogInteger(selector - 1000),
        LogInteger(special ? argument2 : argument1), u""
    );
}

void GlobalObject::ShowCursedWeaponStatus(CSharedCreatureData* creature, int weaponId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00438d80.
    if (!creature) {
        return;
    }
    g_ns.Send("cdd", 0x8d, creature->m_nId, weaponId);
}

void GlobalObject::SetShillienMoon(int state) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043b274.
    if (static_cast<unsigned int>(state) >= 2u) {
        return;
    }
    g_ns.Send("chd", 0xfe, 8, state);
}

void GlobalObject::SunRise(CSharedCreatureData* creature, int mode) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043b338.
    if (!creature || static_cast<unsigned int>(mode) >= 2u) {
        return;
    }
    g_ns.Send("cdd", 0xfd, creature->m_nId, mode);
}

void GlobalObject::SetXMasEventState(int eventId, int state) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043b848 checks the first argument, then sends
    // both dwords. The previous VM branch incorrectly checked and sent only
    // the second argument.
    if (eventId <= 0) {
        return;
    }
    g_ns.Send("cdd", 0xce, eventId, state);
}

void GlobalObject::SetYearCatchEventState(int state) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043b904.
    g_ns.Send("cd", 0xd0, state);
}

int GlobalObject::StartBuffEvent(int eventType, int firstArgument, int secondArgument, int thirdArgument) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043c188.
    if (static_cast<unsigned int>(eventType) > 2u) {
        return 0;
    }
    g_ns.Send("cdddd", 0xe8, eventType, firstArgument, secondArgument, thirdArgument);
    return 1;
}

int GlobalObject::SetEventState(
    int value1, int value2, int value3, int value4, int value5, int value6, int value7, char16_t* string1, char16_t* string2
) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043c270.
    g_ns.Send("cdddddddSS", 0xe9, value1, value2, value3, value4, value5, value6, value7, string1, string2);
    return 1;
}

void GlobalObject::AddDynamicQuestPoint(
    CSharedCreatureData* creature, int questId, int firstArgument, int secondArgument, int pointType, int thirdArgument
) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043c4dc.
    if (!creature) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid creature",
        //     L"GlobalObject.cpp",
        //     0xfa8)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot add dynamic quest %d points (type %d): recipient creature is missing. Request not sent.",
        //     questId, pointType)
        L2_LOG_ERROR(
            Log,
            "[NPC script] Cannot add dynamic quest %d points (type %d): recipient creature is missing; request not sent.",
            questId, pointType
        );
        ReportScriptContext(LOG_ERROR);
        return;
    }
    if (pointType >= 2) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid pointType[%d]",
        //     L"GlobalObject.cpp",
        //     0xfaf,
        //     pointType)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot add dynamic quest %d points to creature %u: point type %d must be below 2. Request not
        //     sent.", questId, static_cast<unsigned int>(creature->m_nId), pointType)
        L2_LOG_ERROR(
            Log,
            "[NPC script] Cannot add dynamic quest %d points to creature %u: point type %d must be below 2; request not "
            "sent.",
            questId, static_cast<unsigned int>(creature->m_nId), pointType
        );
        ReportScriptContext(LOG_ERROR);
        return;
    }

    g_ns.Send("ccdddddd", 0xec, 2, creature->m_nId, questId, firstArgument, secondArgument, pointType, thirdArgument);
}

void GlobalObject::AddEventCampaignPoint(
    CSharedCreatureData* creature, int campaignId, int firstArgument, int pointType, int secondArgument, int thirdArgument
) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043d440.
    if (!creature) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid creature",
        //     L"GlobalObject.cpp",
        //     0x1198)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot add campaign %d points (type %d): recipient creature is missing. Request not sent.",
        //     campaignId, pointType)
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot add campaign %d points (type %d): recipient creature is missing; request not sent.",
            campaignId, pointType
        );
        ReportScriptContext(LOG_ERROR);
        return;
    }

    g_ns.Send(
        "chcdddddd", 0xfe, 0x10, 2, creature->m_nId, campaignId, firstArgument,
        static_cast<int>(static_cast<std::uint32_t>(pointType) - 1u), secondArgument, thirdArgument
    );
}

int GlobalObject::IsEventServer() {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00437ad8 reads the default.obj global directly.
    return g_nIsEventServer != 0;
}

int GlobalObject::GetDynamicQuestProgress(int index) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00439b90.
    if (static_cast<unsigned int>(index) >= 100u) {
        return 0;
    }
    const SharedDynamicQuestInfo* const info = SharedDynamicQuestInfo::GetInfo(index);
    return runtime::Win64CvttDoubleToInt32(info->m_Progress);
}

bool GlobalObject::IsPreviousOwner(CSharedCreatureData* creature, int residenceId) {
    L2_GUARD_FUNCTION();
    if (!creature) {
        return false;
    }

    const SharedCastleInfo* const castle = SharedCastleInfo::GetInfo(residenceId);
    return static_cast<std::uint32_t>(castle->m_nPreviousOwnerId) == creature->m_nPledgeID && castle->m_nPreviousOwnerWorldId == creature->GetWorldId();
}

int GlobalObject::GetPlayingUserAverage() {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043c0f4.
    const SharedEventInfo* const info = SharedEventInfo::Inst();
    return info->m_gameEvent.m_PlayingUserAverage;
}

int GlobalObject::GetEventElapsedTime() {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043c388 uses signed 32-bit _time32 arithmetic.
    const std::time_t hostNow = std::time(nullptr);
    const std::int32_t now = hostNow == static_cast<std::time_t>(-1) ||
            (std::numeric_limits<std::time_t>::is_signed && hostNow < static_cast<std::time_t>(0)) ||
            hostNow > static_cast<std::time_t>(std::numeric_limits<std::int32_t>::max())
        ? -1
        : static_cast<std::int32_t>(hostNow);
    const SharedEventInfo* const info = SharedEventInfo::Inst();
    return runtime::Int32FromBits(
        static_cast<std::uint32_t>(now) - static_cast<std::uint32_t>(info->m_gameEvent.m_FirstDropTime)
    );
}

int GlobalObject::SetEventDebugInfo(int index, int value) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043c428.
    if (static_cast<unsigned int>(index) >= 10u) {
        return 0;
    }
    SharedEventInfo* const info = SharedEventInfo::Inst();
    info->m_gameEvent.m_DebugInfo[index] = value;
    return 1;
}

int GlobalObject::GetAgathionCount() {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043cbf4.
    const SharedEventInfo* const info = SharedEventInfo::Inst();
    return info->m_gameEvent.m_AgathionCount;
}

int GlobalObject::GetEventGoal() {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043cc88.
    const SharedEventInfo* const info = SharedEventInfo::Inst();
    return info->m_gameEvent.m_Goal;
}

int GlobalObject::GetOlympiadGameRule() {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043c648.
    if (!SharedOlympiadInfo::GetInfo()->m_bHostConnected) {
        return -2;
    }
    return SharedOlympiadInfo::GetInfo()->m_nStep == 0 ? -1 : 1;
}

int GlobalObject::GetOlympiadRank(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043c700.
    if (!creature || !creature->m_bIsUser) {
        return 0;
    }
    SharedOlympiadInfo* const info = SharedOlympiadInfo::GetInfo();
    return info->GetRankByName(creature->m_nClass, creature->Name());
}

void GlobalObject::AddOlympiadPoint(CSharedCreatureData* creature, int point) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043c7c4:
    // NPCServer::Send("ccddd", 0x79, 6, 0, index, point).
    if (!creature || !creature->m_bIsUser) {
        return;
    }
    g_ns.Send("ccddd", 0x79, 6, 0, creature->index, point);
}

void GlobalObject::ResetOlympiadPoint(CSharedCreatureData* creature, int point) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043c898:
    // NPCServer::Send("ccddd", 0x79, 7, 0, index, point).
    if (!creature || !creature->m_bIsUser) {
        return;
    }
    g_ns.Send("ccddd", 0x79, 7, 0, creature->index, point);
}

int GlobalObject::GetLegendaryWorldID() const {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043da4c.
    const SharedOlympiadInfo* const info = SharedOlympiadInfo::GetInfo();
    return info->m_idLegendary;
}

const char16_t* GlobalObject::GetWinnerPledgeNameLastSeason() {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043cd1c.
    SharedPledgeGameInfo* const info = SharedPledgeGameInfo::GetInfo();
    return info->m_PrevSeasonWinnerPledgeName;
}

const char16_t* GlobalObject::GetWinnerPlayerNameLastSeason() {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043cdb0.
    SharedPledgeGameInfo* const info = SharedPledgeGameInfo::GetInfo();
    return info->m_PrevSeasonWinnerCharacterName;
}

void GlobalObject::SetWinnerLastSeasonGotPrize() {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043ce48, NPCServer::Send("ch", 0xfe, 0x0e).
    g_ns.Send("ch", 0xfe, 0x0e);
    SharedPledgeGameInfo* const info = SharedPledgeGameInfo::GetInfo();
    info->m_nPrevSeasonWinnerGotItem = 1;
}

int GlobalObject::GetPledgeGameTopRankerPointThisSeason() {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043cf04.
    SharedPledgeGameInfo* const info = SharedPledgeGameInfo::GetInfo();
    return info->m_nTopRankerTotalPrize;
}

bool GlobalObject::IsMemberOfWinnersPledgeLastSeason(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043d204.
    if (!creature || creature->m_nPledgeID == 0) {
        return false;
    }
    SharedPledgeGameInfo* const info = SharedPledgeGameInfo::GetInfo();
    return creature->m_nPledgeID == static_cast<std::uint32_t>(info->m_nPrevSeasonWinnerPledgeDbId);
}

bool GlobalObject::IsWinnerLastSeason(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043d2c4.
    if (!creature) {
        return false;
    }
    SharedPledgeGameInfo* const info = SharedPledgeGameInfo::GetInfo();
    return creature->m_nDBID == info->m_nPrevSeasonWinnerCharacterDbId;
}

bool GlobalObject::IsWinnerLastSeasonAndDidNotGetPrize(CSharedCreatureData* creature) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043d37c calls IsWinnerLastSeason first.
    if (!creature || !IsWinnerLastSeason(creature)) {
        return false;
    }
    SharedPledgeGameInfo* const info = SharedPledgeGameInfo::GetInfo();
    return info->m_nPrevSeasonWinnerGotItem == 0;
}

char16_t* GlobalObject::IntToStr(std::int64_t value) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00436e44. Despite the __int64 signature, the
    // target format string is "%d" and therefore consumes the low dword.
    char16_t* const result = FormatScriptInteger(value, false);
    if (result == EmptyScriptString()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Can't allocate buffer for fstring",
        //     L"GlobalObject.cpp",
        //     0x58b)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot allocate a temporary buffer for IntToStr(%lld). Returning an empty string.",
        //     static_cast<long long>(value))
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot allocate a temporary buffer for IntToStr(%lld); returning an empty string.",
            static_cast<long long>(value)
        );
    }
    return result;
}

char16_t* GlobalObject::IntToFStr(std::int64_t value) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00436f40.
    char16_t* const result = FormatScriptInteger(value, true);
    if (result == EmptyScriptString()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Can't allocate buffer for fstring",
        //     L"GlobalObject.cpp",
        //     0x59e)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot allocate a temporary buffer for IntToFStr(%lld). Returning an empty string.",
        //     static_cast<long long>(value))
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot allocate a temporary buffer for IntToFStr(%lld); returning an empty string.",
            static_cast<long long>(value)
        );
    }
    return result;
}

char16_t* GlobalObject::GetTimeAttackRecordInfo(int roomNo, int recordType, int fieldType, int memberIndex) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043703c.
    SharedTimeAttackInfo* const info = SharedTimeAttackInfo::GetInfo(roomNo, recordType);
    if (!info) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] GetTimeAttackRecordInfo() failed. "
        //     L"Invalid (RoomNO, Type)=(%d, %d)",
        //     L"GlobalObject.cpp",
        //     0x5be,
        //     roomNo,
        //     recordType)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot read time-attack field %d for room %d, record type %d: expected room 1..5 and type 0..2.
        //     returning an empty string.", fieldType, roomNo, recordType)
        L2_LOG_ERROR(
            Log,
            "[NPC script] Cannot read time-attack field %d for room %d, record type %d: expected room 1..5 and type 0..2; "
            "returning an empty string.",
            fieldType, roomNo, recordType
        );
        return EmptyScriptString();
    }

    switch (fieldType) {
        case RF_ROUND_NUMBER:
            return IntToStr(info->mSSQRound);
        case RF_PART_TYPE:
            return IntToStr(info->mSSQPart);
        case RF_MEMBER_COUNT:
            return IntToStr(info->mMemberCount);
        case RF_MEMBER_NAME_LIST:
            if (memberIndex >= 0 && memberIndex < info->mMemberCount) {
                return info->mMemberNameList[memberIndex];
            }
            return EmptyScriptString();
        case RF_POINT:
            return IntToStr(info->mPoint);
        case RF_RECORD_TIME:
            return IntToStr(info->mRecordTime);
        case RF_ELPASED_TIME:
            return IntToStr(info->mElapsedTime);
        default:
            return EmptyScriptString();
    }
}

int GlobalObject::IsWinnerOfTimeAttackEvent(CSharedCreatureData* creature, int roomNo) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00437440.
    if (!creature) {
        return 0;
    }
    const int part = GetSSQPart(creature);
    if (part == 0) {
        return 0;
    }

    SharedTimeAttackInfo* const own = SharedTimeAttackInfo::GetInfo(roomNo, GetSSQPart(creature));
    if (!own) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] IsWinnerOfTimeAttackEvent() failed. "
        //     L"Invalid (RoomNO, Type)=(%d, %d)",
        //     L"GlobalObject.cpp",
        //     0x705,
        //     roomNo,
        //     part)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot determine the time-attack winner for creature %u in room %d: own record lookup failed
        //     (initial faction %d), returning 0.", static_cast<unsigned int>(creature->m_nId), roomNo, part)
        L2_LOG_ERROR(
            Log,
            "[NPC script] Cannot determine the time-attack winner for creature %u in room %d: own record lookup failed "
            "(initial faction %d); returning 0.",
            static_cast<unsigned int>(creature->m_nId), roomNo, part
        );
        return 0;
    }

    const int opponentPart = part == 1 ? 2 : part == 2 ? 1 : 0;
    SharedTimeAttackInfo* const opponent = SharedTimeAttackInfo::GetInfo(roomNo, opponentPart);
    if (!opponent) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] IsWinnerOfTimeAttackEvent() failed. "
        //     L"Invalid (RoomNO, Type)=(%d, %d)",
        //     L"GlobalObject.cpp",
        //     0x711,
        //     roomNo,
        //     opponentPart)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot determine the time-attack winner for creature %u in room %d: opponent record type %d is
        //     unavailable, returning 0.", static_cast<unsigned int>(creature->m_nId), roomNo, opponentPart)
        L2_LOG_ERROR(
            Log,
            "[NPC script] Cannot determine the time-attack winner for creature %u in room %d: opponent record type %d is "
            "unavailable; returning 0.",
            static_cast<unsigned int>(creature->m_nId), roomNo, opponentPart
        );
        return 0;
    }
    return opponent->mPoint < own->mPoint ? own->IsMember(creature->m_nDBID) : 0;
}

int GlobalObject::GetTimeAttackRewardFlag(CSharedCreatureData* creature, int roomNo) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x004375d4.
    if (!creature) {
        return 0;
    }
    const int part = GetSSQPart(creature);
    if (part == 0) {
        return 0;
    }
    SharedTimeAttackInfo* const info = SharedTimeAttackInfo::GetInfo(roomNo, part);
    if (!info) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] GetTimeAttackRewardFlag() failed. "
        //     L"Invalid (RoomNO, Type)=(%d, %d)",
        //     L"GlobalObject.cpp",
        //     0x738,
        //     roomNo,
        //     part)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot read the time-attack reward flag for creature %u in room %d: record type %d is
        //     unavailable, returning 1.", static_cast<unsigned int>(creature->m_nId), roomNo, part)
        L2_LOG_ERROR(
            Log,
            "[NPC script] Cannot read the time-attack reward flag for creature %u in room %d: record type %d is "
            "unavailable; returning 1.",
            static_cast<unsigned int>(creature->m_nId), roomNo, part
        );
        return 1;
    }
    return info->GetRewardFlag(creature->m_nDBID);
}

int GlobalObject::CreateRoomInfoList(char16_t* name, int size) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00446a10, GlobalObject.cpp:0x82.
    L2_GUARD(mRoomInfoLock);
    if (mRoomInfoListMap.find(std::u16string(name)) != mRoomInfoListMap.end()) {
        return 0;
    }
    mRoomInfoListMap[std::u16string(name)] = new RoomInfoList(name, size);
    return 1;
}

RoomInfoList* GlobalObject::GetRoomInfoList(char16_t* name) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00442850, GlobalObject.cpp:0x98.
    L2_GUARD(mRoomInfoLock);
    const auto found = mRoomInfoListMap.find(std::u16string(name));
    return found == mRoomInfoListMap.end() ? nullptr : found->second;
}

int GlobalObject::Rand(int upperExclusive) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x0043e7bc logs the invalid bound before returning
    // zero. RandOrPreset has a separate diagnostic at source line 0x55.
    if (upperExclusive < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Rand(0) -_-",
        //     L"GlobalObject.cpp",
        //     0x37)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Rand upper bound %d must be positive. Returning 0.",
        //     upperExclusive)
        L2_LOG_ERROR(Log, "[NPC script] Rand upper bound %d must be positive; returning 0.", upperExclusive);
        return 0;
    }
    // genrand64_real2(): the top 53 bits scaled to [0,1), followed by
    // truncation of real*upperExclusive in GlobalObject::Rand.
    return static_cast<int>(genrand64_real2() * static_cast<double>(upperExclusive));
}

int GlobalObject::RandOrPreset(int upperExclusive) {
    L2_GUARD_FUNCTION();
    const NpcSpawnInfo* const spawnInfo = NpcSpawnInfo::Inst();
    const int preset = spawnInfo->Npc_RandPresetValue;
    if (preset < 0) {
        return Rand(upperExclusive);
    }
    if (upperExclusive < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] RandOrPreset(0) -_-",
        //     L"GlobalObject.cpp",
        //     0x55)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] RandOrPreset upper bound %d must be positive (preset %d). Returning 0.",
        //     upperExclusive, preset)
        L2_LOG_ERROR(
            Log, "[NPC script] RandOrPreset upper bound %d must be positive (preset %d); returning 0.", upperExclusive,
            preset
        );
        return 0;
    }
    return std::min(upperExclusive, preset);
}

Position* GlobalObject::GetRandomPosInPos(int x, int y, int z, bool flying, int minimumDistance, int maximumDistance) {
    L2_GUARD_FUNCTION();
    Position* const result = Singleton<ThreadLocalStorage<TempObjectPool<Position, 32>, 8>>::inst()->Alloc();
    if (!result) {
        return nullptr;
    }

    const std::array<double, 3> source{{
        static_cast<double>(x),
        static_cast<double>(y),
        static_cast<double>(z),
    }};
    std::array<double, 3> selected = source;
    const std::int32_t distanceExtent = runtime::Int32FromBits(
        static_cast<std::uint32_t>(maximumDistance) - static_cast<std::uint32_t>(minimumDistance) + 1U
    );

    for (int attempt = 0; attempt < 5; ++attempt) {
        const int distance = runtime::Win64CvttDoubleToInt32(
            genrand64_real2() * static_cast<double>(distanceExtent) + static_cast<double>(minimumDistance)
        );
        const int angle = runtime::Win64CvttDoubleToInt32(genrand64_real2() * 361.0);
        const FVector2D rotated = FVector2D(0, distance).Rotate(angle);
        std::array<double, 3> candidate{{
            source[0] + rotated.x,
            source[1] + rotated.y,
            source[2] + 40.0,
        }};
        const int groundZ = runtime::Win64CvttDoubleToInt32(candidate[2]);
        const int groundY = runtime::Win64CvttDoubleToInt32(candidate[1]);
        const int groundX = runtime::Win64CvttDoubleToInt32(candidate[0]);
        CGeoData* const geodata = l2geo::CWorld::Inst().GetGeo();
        candidate[2] = static_cast<double>(geodata->FindGround(groundX, groundY, groundZ));

        const FVector from{
            source[0],
            source[1],
            source[2],
        };
        const FVector to{
            candidate[0],
            candidate[1],
            candidate[2],
        };
        const double deltaX = to.x - from.x;
        const double deltaY = to.y - from.y;
        const double deltaZ = to.z - from.z;
        if (l2geo::CWorld::Inst().GetGeo()->MoveStraightTestBoth(
                from, to, std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ), true, flying, 0
            )) {
            selected = candidate;
            break;
        }
    }

    result->m_Z = runtime::Win64CvttDoubleToInt32(selected[2]);
    result->m_Y = runtime::Win64CvttDoubleToInt32(selected[1]);
    result->m_X = runtime::Win64CvttDoubleToInt32(selected[0]);
    return result;
}

Position* GlobalObject::GetRandomPosInCreature(CSharedCreatureData* creature, int minimumDistance, int maximumDistance) {
    L2_GUARD_FUNCTION();
    if (!creature) {
        return nullptr;
    }
    const FVector position = creature->GetPos();
    // Current L2NPC.exe 0x00441f90 reads the move type after the position
    // copy, then performs the z/y/x conversions.
    const bool flying = creature->IsFlying();
    const int z = runtime::Win64CvttDoubleToInt32(position.z);
    const int y = runtime::Win64CvttDoubleToInt32(position.y);
    const int x = runtime::Win64CvttDoubleToInt32(position.x);
    return GetRandomPosInPos(x, y, z, flying, minimumDistance, maximumDistance);
}

Position* GlobalObject::GetRandomPosInTerritory(char16_t* territoryName, bool floating, char16_t* npcName) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00443424 and ai.obj descriptor 0x10030090 both
    // expose (String, Int, String). The nearby PDB's two-argument class
    // record is stale. Its compiled registration and shipping scripts use
    // this three-argument form too.
    Position* const result = Singleton<ThreadLocalStorage<TempObjectPool<Position, 32>, 8>>::inst()->Alloc();
    if (!result) {
        return nullptr;
    }

    CTerritory* const territory = l2npc::CNpcMakerDB::Inst().GetTerritory(std::u16string(territoryName));
    if (!territory) {
        const std::wstring hostNpcName = npcName ? Utf16ToWide(npcName) : std::wstring{};
        // Log.Add(
        //     LOG_ERROR,
        //     L"GetRandomPosInTerritory() Name[%s]",
        //     npcName ? hostNpcName.c_str() : nullptr)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Territory '%s' is unavailable for NPC '%s'. No random position returned.",
        //     territoryName ? Utf16ToUtf8(std::u16string_view(territoryName)).c_str() : "<missing>", npcName ?
        //     WideToUtf8(hostNpcName).c_str() : "<unspecified>")
        L2_LOG_ERROR(
            Log, "[NPC script] Territory '%s' is unavailable for NPC '%s'; no random position returned.",
            territoryName ? Utf16ToUtf8(std::u16string_view(territoryName)).c_str() : "<missing>",
            npcName ? WideToUtf8(hostNpcName).c_str() : "<unspecified>"
        );
        return nullptr;
    }

    const FVector selected = territory->PickRandomPos(floating);
    result->m_X = runtime::Win64CvttDoubleToInt32(selected.x);
    result->m_Y = runtime::Win64CvttDoubleToInt32(selected.y);
    result->m_Z = runtime::Win64CvttDoubleToInt32(selected.z);
    return result;
}

void GlobalObject::SendScriptEvent(CSharedCreatureData* target, int argument1, int argument2) {
    L2_GUARD_FUNCTION();
    SendScriptEventEx(target, argument1, argument2, 0);
}

void GlobalObject::SendScriptEventEx(CSharedCreatureData* target, int argument1, int argument2, int argument3) {
    L2_GUARD_FUNCTION();
    if (!target) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][%d][%d][%d] "
        //     L"SendScriptEvent(to=null)",
        //     L"GlobalObject.cpp",
        //     0x1d7,
        //     argument1,
        //     argument2,
        //     argument3)
        // Log.Add(LOG_ERROR,
        //     "[NPC script] Cannot post script event (%d, %d, %d): recipient creature is missing. Event not queued.",
        //     argument1, argument2, argument3)
        L2_LOG_ERROR(
            Log, "[NPC script] Cannot post script event (%d, %d, %d): recipient creature is missing; event not queued.",
            argument1, argument2, argument3
        );
        return;
    }
    auto* const helper = new CScriptEventHelperClass(target->m_nId, argument1, argument2, argument3);
    helper->Post(0);
}

void GlobalObject::SendMakerScriptEvent(l2npc::NpcMakerEx* maker, int argument1, int argument2, int argument3) {
    L2_GUARD_FUNCTION();
    if (!maker) {
        return;
    }
    auto* const helper = new MakerScriptEventHelper(maker, argument1, argument2, argument3);
    helper->Post(0);
}

void GlobalObject::SetInZoneFlag(int inzoneId, int flagId, int value) {
    L2_GUARD_FUNCTION();
    l2npc::CNpcMakerDB::Inst().SetInZoneMapFlag(inzoneId, flagId, value);
}

int GlobalObject::GetInZoneFlag(int inzoneId, int flagId) {
    L2_GUARD_FUNCTION();
    return l2npc::CNpcMakerDB::Inst().GetInZoneMapFlag(inzoneId, flagId);
}

bool GlobalObject::GetDoorStatus(const char16_t* doorName) {
    L2_GUARD_FUNCTION();
    return l2npc::CNpcMakerDB::Inst().GetDoorStatus(doorName);
}
