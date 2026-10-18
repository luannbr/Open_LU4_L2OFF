#include "L2NPC/src/ScriptEngine.h"
#include "Shared/src/Runtime/OperatorLog.h"

#include "L2Core/src/inc/Guard.h"
#include "L2NPC/src/AIManager.h"
#include "L2NPC/src/Config.h"
#include "L2NPC/src/NPC.h"
#include "L2NPC/src/NPCEventFactory.h"
#include "L2NPC/src/NPCServer.h"
#include "L2NPC/src/NpcEvent.h"
#include "L2NPC/src/NpcMaker.h"
#include "L2NPC/src/NpcMakerDB.h"
#include "L2NPC/src/NpcMakerEx.h"
#include "L2NPC/src/ScriptBinCode.h"
#include "L2NPC/src/ScriptTypeInfo.h"
#include "L2NPC/src/User.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Runtime.h"
#include "Shared/src/AtomicJob.h"
#include "Shared/src/EtcMani.h"
#include "Shared/src/Exception.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/IOC.h"
#include "Shared/src/Inventory.h"
#include "Shared/src/Log.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/TimeAnalysis.h"
#include "Shared/src/World.h"
#include "TextRuntime.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>


thread_local int th_ChoiceN = 0;
thread_local int th_ChoiceCount1 = 0;
thread_local int th_ChoiceCount2 = 0;
thread_local int th_ChoiceCount3 = 0;
thread_local int th_ChoiceCount4 = 0;
thread_local ChoiceData th_Choices[4][100]{};

// Current L2NPC.exe 0x02790090..0x0280538f: the source global is a raw
// wchar_t[ThreadID::MAX][20000], not an eight-worker-only carrier. The
// adjacent PDB has the same two-dimensional type with a different historical
// ThreadID::MAX. The current image fixes the first dimension at twelve.
char16_t g_tempStringPool[ThreadID::MAX][20000]{};

namespace {

constexpr int kTempStringCodeUnits = 20000;

constexpr int kAtomicPacketMaximum = 0x4f9c;

// Only the cursor is TLS. The process-global slice is selected on every call
// through CThreadManager::GetThreadID(), exactly as in the current image.
thread_local int g_tempStringCursor = 0;

char16_t* CurrentTempStringBase() { return g_tempStringPool[CThreadManager::GetThreadID()]; }

char16_t* CurrentTempStringPointer() { return CurrentTempStringBase() + static_cast<std::ptrdiff_t>(g_tempStringCursor); }

std::u16string_view Msvc2010SpecialFloat(float value) noexcept {
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    if ((bits & 0x7f800000U) != 0x7f800000U) {
        return {};
    }

    const bool negative = (bits & 0x80000000U) != 0;
    const std::uint32_t mantissa = bits & 0x007fffffU;
    if (mantissa == 0) {
        return negative ? u"-1.#INF00" : u"1.#INF00";
    }
    if ((mantissa & 0x00400000U) == 0) {
        return negative ? u"-1.#SNAN0" : u"1.#SNAN0";
    }
    if (negative && mantissa == 0x00400000U) {
        return u"-1.#IND00";
    }
    return negative ? u"-1.#QNAN0" : u"1.#QNAN0";
}

void StoreSecureAsciiText(char16_t* destination, std::size_t capacity, std::string_view value) {
    // VS2010's _i64tow_s/_vswprintf_s_l leave a zero string on a non-zero
    // undersized destination. A zero capacity leaves the pointed-to storage
    // untouched. Alloc*String still performs wcslen on it afterwards.
    if (capacity == 0) {
        return;
    }
    destination[0] = u'\0';
    if (value.size() >= capacity) {
        return;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        destination[index] = static_cast<unsigned char>(value[index]);
    }
    destination[value.size()] = u'\0';
}

void StoreSecureUtf16Text(char16_t* destination, std::size_t capacity, std::u16string_view value) {
    if (capacity == 0) {
        return;
    }
    destination[0] = u'\0';
    if (value.size() >= capacity) {
        return;
    }
    std::copy(value.begin(), value.end(), destination);
    destination[value.size()] = u'\0';
}

} // namespace

AtomicJobCompositor::JobInfoForUser::JobInfoForUser(bool checkItemJob) : m_CheckItemJob(checkItemJob) {}

AtomicJobCompositor::JobInfoForUser::JobInfoForUser() = default;

bool AtomicJobCompositor::JobInfoForUser::CheckItemJob() const { return m_CheckItemJob; }

AtomicJobCompositor::AtomicJobCompositor() = default;

AtomicJobCompositor::~AtomicJobCompositor() = default;

void AtomicJobCompositor::Initialize() {
    // Current L2NPC.exe 0x00510284.
    L2_GUARD_FUNCTION();
    if (!mUserToJobInfoMap.empty()) {
        Clear();
    }
}

int AtomicJobCompositor::Process() {
    // Current L2NPC.exe 0x0050f63c. EtcMani, ItemManip and AtomicJob are
    // three distinct streams and keep the target's serialization order.
    L2_GUARD_FUNCTION();
    for (auto& entry : mUserToJobInfoMap) {
        const unsigned int creatureIndex = entry.first;
        JobInfoForUser& jobs = entry.second;
        std::array<char, 0x5000> buffer;

        int length = ::Assemble(
            buffer.data(), kAtomicPacketMaximum, "dd", static_cast<int>(jobs.mEtcManiList.size()),
            static_cast<int>(jobs.mItemManiList.size())
        );

        for (EtcMani* manipulation : jobs.mEtcManiList) {
            const int assembled = manipulation->Assemble(buffer.data() + length, kAtomicPacketMaximum - length);
            if (assembled == 0) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Buffer Overflow in "
                //     L"AtomicItemParamProcess(item)!!!",
                //     L"ScriptEngine.cpp",
                //     0x75a)
                // Log.Add(LOG_ERROR,
                //     "[AI] Cannot serialize miscellaneous item changes for creature index %u: %d of %d bytes used. Stopping
                //     the atomic batch.", creatureIndex, length, kAtomicPacketMaximum)
                L2_LOG_ERROR(
                    Log,
                    "[AI] Cannot serialize miscellaneous item changes for creature index %u: %d of %d bytes used. Stopping "
                    "the atomic batch.",
                    creatureIndex, length, kAtomicPacketMaximum
                );
                return -1;
            }
            length += assembled;
        }

        for (ItemManip& manipulation : jobs.mItemManiList) {
            const int assembled = manipulation.Assemble(buffer.data() + length, kAtomicPacketMaximum - length);
            if (assembled == 0) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Buffer Overflow in "
                //     L"AtomicItemParamProcess(item)!!!",
                //     L"ScriptEngine.cpp",
                //     0x769)
                // Log.Add(LOG_ERROR,
                //     "[AI] Cannot serialize item changes for creature index %u: %d of %d bytes used. Stopping the atomic
                //     batch.", creatureIndex, length, kAtomicPacketMaximum)
                L2_LOG_ERROR(
                    Log,
                    "[AI] Cannot serialize item changes for creature index %u: %d of %d bytes used. Stopping the atomic "
                    "batch.",
                    creatureIndex, length, kAtomicPacketMaximum
                );
                return -1;
            }
            length += assembled;
        }

        length += ::Assemble(
            buffer.data() + length, kAtomicPacketMaximum - length, "d", static_cast<int>(jobs.mAtomicJobList.size())
        );

        for (AtomicJob* job : jobs.mAtomicJobList) {
            const int assembled = job->Assemble(buffer.data() + length, kAtomicPacketMaximum - length);
            if (assembled == 0) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Buffer Overflow in "
                //     L"AtomicItemParamProcess(param)!!!",
                //     L"ScriptEngine.cpp",
                //     0x783)
                // Log.Add(LOG_ERROR,
                //     "[AI] Cannot serialize atomic jobs for creature index %u: %d of %d bytes used. Stopping the atomic
                //     batch.", creatureIndex, length, kAtomicPacketMaximum)
                L2_LOG_ERROR(
                    Log,
                    "[AI] Cannot serialize atomic jobs for creature index %u: %d of %d bytes used. Stopping the atomic "
                    "batch.",
                    creatureIndex, length, kAtomicPacketMaximum
                );
                return -1;
            }
            length += assembled;
        }

        const int index = static_cast<int>(creatureIndex);
        const int threadId = CThreadManager::GetThreadID();
        // RecursiveCounter has already decremented nRecursiveCount when it
        // calls Process. The EXE indexes g_ScriptContext with that new
        // depth directly. On the outermost return this is slot zero, which
        // remains populated until RecursiveCounter finishes.
        const ScriptContext& scriptContext =
            g_ScriptContext[threadId][static_cast<std::size_t>(g_RunScriptCallStatus[threadId].nRecursiveCount)];
        g_ns.Send(
            "cddddb", 0x3d, CCreatureFactory::Inst().GetIdByIndex(index), index, scriptContext.nEventID,
            jobs.CheckItemJob() ? 1 : 0, length, buffer.data()
        );
    }
    return 0;
}

void AtomicJobCompositor::Clear() {
    // Current L2NPC.exe 0x0051011c. Preserve its conditional ownership
    // path: EtcMani objects are destroyed in the same branch as AtomicJob
    // objects, then the value vectors themselves are discarded by map clear.
    L2_GUARD_FUNCTION();
    for (auto& entry : mUserToJobInfoMap) {
        JobInfoForUser& jobs = entry.second;
        if (!jobs.mAtomicJobList.empty()) {
            for (AtomicJob* job : jobs.mAtomicJobList) {
                job->Destroy();
            }
            for (EtcMani* manipulation : jobs.mEtcManiList) {
                manipulation->Destroy();
            }
        }
    }
    mUserToJobInfoMap.clear();
}

void AtomicJobCompositor::AddJob(int creatureIndex, AtomicJob* job) {
    // Current L2NPC.exe 0x0051032c.
    L2_GUARD_FUNCTION();
    const unsigned int key = static_cast<unsigned int>(creatureIndex);
    auto found = mUserToJobInfoMap.lower_bound(key);
    if (found == mUserToJobInfoMap.end() || key < found->first) {
        JobInfoForUser jobs;
        jobs.mAtomicJobList.push_back(job);
        const MAP_TYPE::value_type value(key, jobs);
        mUserToJobInfoMap.insert(found, value);
        return;
    }
    found->second.mAtomicJobList.push_back(job);
}

void AtomicJobCompositor::AddItemManip(int creatureIndex, const ItemManip& manipulation, bool checkItemJob) {
    // Current L2NPC.exe 0x005105fc. The flag initializes only a newly
    // inserted JobInfoForUser. Later calls never overwrite it.
    L2_GUARD_FUNCTION();
    const unsigned int key = static_cast<unsigned int>(creatureIndex);
    auto found = mUserToJobInfoMap.lower_bound(key);
    if (found == mUserToJobInfoMap.end() || key < found->first) {
        JobInfoForUser jobs(checkItemJob);
        jobs.mItemManiList.push_back(manipulation);
        const MAP_TYPE::value_type value(key, jobs);
        mUserToJobInfoMap.insert(found, value);
        return;
    }
    found->second.mItemManiList.push_back(manipulation);
}

void AtomicJobCompositor::AddEtcMani(int creatureIndex, EtcMani* manipulation) {
    // Current L2NPC.exe 0x00510494.
    L2_GUARD_FUNCTION();
    const unsigned int key = static_cast<unsigned int>(creatureIndex);
    auto found = mUserToJobInfoMap.lower_bound(key);
    if (found == mUserToJobInfoMap.end() || key < found->first) {
        JobInfoForUser jobs;
        jobs.mEtcManiList.push_back(manipulation);
        const MAP_TYPE::value_type value(key, jobs);
        mUserToJobInfoMap.insert(found, value);
        return;
    }
    found->second.mEtcManiList.push_back(manipulation);
}

bool AtomicJobCompositor::IsEmpty() const { return mUserToJobInfoMap.empty(); }

void InitTmpString() {
    // Current L2NPC.exe 0x0050ec7c.
    L2_GUARD_FUNCTION();
    g_tempStringCursor = 0;
}

char16_t* AllocTmpString(int length) {
    // Current L2NPC.exe 0x0050edb0. The target uses a strict
    // comparison and consumes length+1 code units, reserving the last for
    // the terminator.
    L2_GUARD_FUNCTION();
    char16_t* const sliceBase = CurrentTempStringBase();
    const int cursor = g_tempStringCursor;
    const int allocationEnd = runtime::AddInt32Wrapping(cursor, length);
    if (allocationEnd >= kTempStringCodeUnits) {
        return nullptr;
    }
    char16_t* const result = sliceBase + static_cast<std::ptrdiff_t>(cursor);
    result[length] = u'\0';
    g_tempStringCursor = runtime::AddInt32Wrapping(allocationEnd, 1);
    return result;
}

char16_t* AllocIntegerString(std::int64_t value) {
    // Current L2NPC.exe 0x0050ee90
    // (l2npc/ScriptEngine.cpp). The target uses _i64tow_s(..., 10) at the
    // current TLS-pool cursor, then advances by wcslen(result)+1.
    L2_GUARD_FUNCTION();
    char16_t* const result = CurrentTempStringPointer();
    char16_t* const end = GetTmpStringEnd();
    const std::size_t capacity = static_cast<std::size_t>(end - result);
    std::array<char, 32> text{};
    const auto converted = std::to_chars(text.data(), text.data() + text.size(), value, 10);
    if (converted.ec == std::errc{}) {
        StoreSecureAsciiText(
            result, capacity, std::string_view(text.data(), static_cast<std::size_t>(converted.ptr - text.data()))
        );
    } else if (capacity != 0) {
        result[0] = u'\0';
    }
    const int length = static_cast<int>(std::char_traits<char16_t>::length(result));
    result[length] = u'\0';
    g_tempStringCursor = runtime::AddInt32Wrapping(g_tempStringCursor, runtime::AddInt32Wrapping(length, 1));
    return result;
}

char16_t* AllocFloatString(float value) {
    // Current L2NPC.exe 0x0050ef90
    // (l2npc/ScriptEngine.cpp lines 126-140 in the named 2015 PDB donor).
    // Its secure wide formatter receives L"%f" and advances the same cursor
    // by the formatted length plus its terminator.
    L2_GUARD_FUNCTION();
    char16_t* const result = CurrentTempStringPointer();
    char16_t* const end = GetTmpStringEnd();
    const std::size_t capacity = static_cast<std::size_t>(end - result);
    const std::u16string_view special = Msvc2010SpecialFloat(value);
    if (!special.empty()) {
        StoreSecureUtf16Text(result, capacity, special);
    } else {
        std::array<char, 128> text{};
        const int formattedLength = std::snprintf(text.data(), text.size(), "%f", static_cast<double>(value));
        if (formattedLength < 0 || static_cast<std::size_t>(formattedLength) >= text.size()) {
            if (capacity != 0) {
                result[0] = u'\0';
            }
        } else {
            StoreSecureAsciiText(result, capacity, std::string_view(text.data(), static_cast<std::size_t>(formattedLength)));
        }
    }
    const int length = static_cast<int>(std::char_traits<char16_t>::length(result));
    result[length] = u'\0';
    g_tempStringCursor = runtime::AddInt32Wrapping(g_tempStringCursor, runtime::AddInt32Wrapping(length, 1));
    return result;
}

char16_t* GetTmpStringEnd() {
    // Current L2NPC.exe 0x0050ed0c.
    L2_GUARD_FUNCTION();
    return CurrentTempStringBase() + kTempStringCodeUnits;
}


// Run the 0x00..0x25 NPC bytecode.
// Limits: 100 eight-byte value slots, 30000 instructions and 1000 nested calls.
// Float flags are stored beside the values. Native calls use ScriptTypeTable
// and ScriptFuncInfo::Invoke. Worker TLS keeps each call's state.


namespace {

constexpr int kStackSlots = 100;
constexpr int kInstructionLimit = 30000;
constexpr int kRecursionLimit = 1000;
constexpr std::int64_t kAccessMinimum = 0x400000;

std::mutex g_scriptStringLock;
std::set<std::wstring, std::less<>> g_scriptStrings;

int ScriptThreadId() { return CThreadManager::GetThreadID(); }

RunScriptCallStatus& CurrentRunScriptCallStatus() { return g_RunScriptCallStatus[ScriptThreadId()]; }

wchar_t* StableWideString(const char* value) {
    static wchar_t empty[] = L"";
    if (!value || *value == '\0') {
        return empty;
    }

    std::wstring wide = Utf8ToWide(value);
    std::lock_guard lock(g_scriptStringLock);
    const auto [entry, inserted] = g_scriptStrings.emplace(std::move(wide));
    return const_cast<wchar_t*>(entry->c_str());
}

wchar_t* StableWideString(const char16_t* value) {
    return value ? StableWideString(Utf16ToUtf8(std::u16string_view(value)).c_str())
                 : StableWideString(static_cast<const char*>(nullptr));
}

ScriptContext* MutableCurrentScriptContext() {
    const int threadId = ScriptThreadId();
    const int recursiveCount = g_RunScriptCallStatus[threadId].nRecursiveCount;
    return &g_ScriptContext[threadId][static_cast<std::size_t>(recursiveCount - 1)];
}

void PublishScriptStackPointer(int stackPointer) {
    if (ScriptContext* const context = MutableCurrentScriptContext()) {
        context->nSP = stackPointer;
    }
}

void AdjustScriptStackPointer(int& stackPointer, int delta) {
    stackPointer = runtime::AddInt32Wrapping(stackPointer, delta);
    PublishScriptStackPointer(stackPointer);
}

const ScriptContext* CurrentScriptContext() { return MutableCurrentScriptContext(); }

class ScriptContextScope {
public:
    ScriptContextScope(const char* handlerName, int eventId, const char* eventName) {
        threadId_ = ScriptThreadId();
        const int recursiveCount = g_RunScriptCallStatus[threadId_].nRecursiveCount;
        context_ = &g_ScriptContext[threadId_][static_cast<std::size_t>(recursiveCount)];
        *context_ = {};
        context_->bSet = true;
        context_->pszHandlerName = StableWideString(handlerName);
        context_->nEventID = eventId;
        context_->pszEventName = StableWideString(eventName);
        context_->nPC = 0;
        context_->nSP = 0;
        context_->optr = -1;
        context_->pszFuncName = StableWideString(static_cast<const char*>(nullptr));
    }

    ~ScriptContextScope() {
        if (!context_ || !context_->bSet) {
            return;
        }
        // RecursiveCounter owns the normal target cleanup point. This only
        // covers a portable exception before that counter is constructed.
        context_->pszHandlerName = nullptr;
        context_->pszEventName = nullptr;
        context_->nEventID = -1;
        context_->nPC = 0;
        context_->nSP = 0;
        context_->optr = -1;
        context_->bSet = false;
        context_->pszFuncName = nullptr;
        g_ScriptNPC[threadId_] = nullptr;
    }

    ScriptContextScope(const ScriptContextScope&) = delete;
    ScriptContextScope& operator=(const ScriptContextScope&) = delete;

private:
    ScriptContext* context_ = nullptr;
    int threadId_ = -1;
};

void PublishScriptFunctionName(const wchar_t* functionName) {
    MutableCurrentScriptContext()->pszFuncName = const_cast<wchar_t*>(functionName);
}

void UpdateScriptContextInstruction(int programCounter, int stackPointer, std::uint8_t opcode) {
    ScriptContext* const context = MutableCurrentScriptContext();
    if (!context) {
        return;
    }
    context->nPC = programCounter;
    context->nSP = stackPointer;
    context->optr = static_cast<char>(opcode);
    context->pszFuncName = StableWideString(static_cast<const char*>(nullptr));
}

void ClearReturnedScriptContext(int threadId, int recursiveDepth) {
    ScriptContext& context = g_ScriptContext[threadId][static_cast<std::size_t>(recursiveDepth)];
    context.pszHandlerName = nullptr;
    context.pszEventName = nullptr;
    context.nEventID = -1;
    context.nPC = 0;
    context.nSP = 0;
    context.optr = -1;
    context.bSet = false;
    context.pszFuncName = nullptr;
    g_ScriptNPC[threadId] = nullptr;
}

void TraceNativeCall(
    ScriptFuncInfo& function,
    std::int64_t self,
    const std::int64_t* arguments,
    int argumentCount,
    ScriptEvent& event,
    CNPCEventHandler& handler
) {
    if (!g_bTraceNpcAI) {
        return;
    }

    ScriptAccessibleEx* const accessible = function.GetScriptAccessibleEx(self);

    bool trace = false;
    std::wstring ownerName = L"NoName";
    if (accessible->IsNPCInScript()) {
        CNPC* const npc = accessible->CastNPCInScript();
        ownerName = Utf16ToWide(npc->GetName());
        trace = npc->m_data->m_bNpcDebugOn;
    } else if (accessible->IsNPCMakerExInScript()) {
        l2npc::NpcMakerEx* const maker = accessible->CastNPCMakerExInScript();
        ownerName = Utf16ToWide(maker->GetName());
        trace = maker->m_bNpcMakerExDebugOn;
    } else if (accessible->IsGlobalObjectInScript()) {
        ownerName = L"GlobalObj";
        trace = g_bTraceGlobalObjectAI;
    }
    if (!trace) {
        return;
    }

    // The common log record supplies the timestamp.
    // const std::tm localTime =
    //     runtime::crt::LocalTime32OrZero(
    //         runtime::crt::Time32Now())
    const std::wstring handlerName = Utf16ToWide(std::u16string_view(handler.GetAIClassName()));
    const char16_t* const eventNameValue = event.GetEventName(event.m_nEventId);
    const std::wstring eventName = eventNameValue ? Utf16ToWide(std::u16string_view(eventNameValue)) : std::wstring{};
    // std::array<wchar_t, 1000>
    //     header{}.
    // std::swprintf(
    //     header.data(),
    //     header.size(),
    //     L"[%02d:%02d:%02d] t_ai [%ls] "
    //     L"ai[%ls] ev[%ls] tid[%d] %ls(",
    //     localTime.tm_hour,
    //     localTime.tm_min,
    //     localTime.tm_sec,
    //     ownerName.c_str(),
    //     handlerName.c_str(),
    //     eventName.c_str(),
    //     event.timer_id,
    //     function.Name())
    // std::wstring text(
    //     header.data())
    std::wstring text = L"[AI trace] Owner '" + ownerName + L"', script '" + handlerName + L"', event '" + eventName +
        L"', timer " + std::to_wstring(event.timer_id) + L"; calling " + function.Name() + L"(";

    ScriptTypeTable* const typeTable = ScriptTypeTable::inst();
    ScriptTypeInfo* const intType = typeTable->GetType(static_cast<int>(ScriptTypeId::Int));
    ScriptTypeInfo* const floatType = typeTable->GetType(static_cast<int>(ScriptTypeId::Float));
    ScriptTypeInfo* const stringType = typeTable->GetType(static_cast<int>(ScriptTypeId::String));
    ScriptTypeInfo* const doubleType = typeTable->GetType(static_cast<int>(ScriptTypeId::Double));
    ScriptTypeInfo* const int64Type = typeTable->GetType(static_cast<int>(ScriptTypeId::Int64));
    ScriptTypeInfo* const creatureType = typeTable->GetType(static_cast<int>(ScriptTypeId::CreatureData));
    ScriptTypeInfo* const itemType = typeTable->GetType(static_cast<int>(ScriptTypeId::ItemData));

    for (int index = 0; index < argumentCount; ++index) {
        ScriptTypeInfo* const type = function.ArgType(index);
        if (!type) {
            break;
        }
        const std::int64_t value = arguments[index];
        if (index != 0) {
            text += L", ";
        }
        text += L"argument " + std::to_wstring(index) + L" = ";
        if (type == floatType || type == doubleType) {
            float floatValue = 0.0F;
            std::memcpy(&floatValue, &value, sizeof(floatValue));
            std::array<wchar_t, 64> formatted{};
            std::swprintf(formatted.data(), formatted.size(), L"%.2f ", static_cast<double>(floatValue));
            text += formatted.data();
        } else if (type == intType || type == int64Type) {
            text += std::to_wstring(value);
            text += L' ';
        } else if (type == stringType) {
            text += Utf16ToWide(reinterpret_cast<const char16_t*>(static_cast<std::intptr_t>(value)));
            text += L' ';
        } else if (type == creatureType) {
            auto* const creature = reinterpret_cast<CSharedCreatureData*>(static_cast<std::intptr_t>(value));
            text += L"c:";
            text += creature ? Utf16ToWide(std::u16string_view(creature->Name())) : L"Null Creature";
            text += L' ';
        } else if (type == itemType) {
            const auto* const item = reinterpret_cast<const CSharedItemData*>(static_cast<std::intptr_t>(value));
            text += L"item:";
            text += std::to_wstring(item ? item->m_nClassID : -1);
            text += L' ';
        } else {
            text += type->GetScriptTypeName();
            text += L'_';
            text += std::to_wstring(static_cast<std::int32_t>(value));
            text += L' ';
        }
    }

    // Log.Add(
    //     LOG_NORMAL,
    //     L"%s)",
    //     text.c_str())
    // CLog's narrow payload must stay below 999 bytes. Trim at a UTF-8 boundary
    // before forwarding so a long opt-in trace gets a visible marker instead
    // of entering the logger's legacy overflow path. Keep file/LogD routing.
    std::string message = WideToUtf8(text + L")");
    constexpr std::size_t traceByteLimit = 960;
    if (message.size() > traceByteLimit) {
        std::size_t cut = traceByteLimit;
        while (cut > 0 && (static_cast<unsigned char>(message[cut]) & 0xc0U) == 0x80U) {
            --cut;
        }
        message.resize(cut);
        message += " [truncated]";
    }
    // Log.Add(LOG_NORMAL, "%s", message.c_str())
    L2_LOG_INFO(Log, "%s", message.c_str());
}

// Internal control-flow carrier only. Observable error ownership remains in
// RunScriptCallStatus::nLastErrno, as in the EXE.
struct RunResult {
    bool stopped = false;
};

thread_local int g_currentQuestId = 0;

class RecursiveCounter {
public:
    RecursiveCounter() {
        L2_GUARD_FUNCTION();
        RunScriptCallStatus& status = CurrentRunScriptCallStatus();
        const int previousDepth = status.nRecursiveCount++;
        if (previousDepth == 0) {
            status.nLastErrno = 0;
            InitTmpString();
            th_ChoiceN = 0;
            th_ChoiceCount1 = 0;
            th_ChoiceCount2 = 0;
            th_ChoiceCount3 = 0;
            th_ChoiceCount4 = 0;
            Singleton<ThreadLocalStorage<TempObjectPool<HateInfo, 32>, 8>>::inst()->Initialize();
            Singleton<ThreadLocalStorage<TempObjectPool<CodeInfoList, 5>, 8>>::inst()->Initialize();
            Singleton<ThreadLocalStorage<TempObjectPool<CodeInfo, 32>, 8>>::inst()->Initialize();
            Singleton<ThreadLocalStorage<TempObjectPool<PositionList, 5>, 8>>::inst()->Initialize();
            Singleton<ThreadLocalStorage<TempObjectPool<Position, 32>, 8>>::inst()->Initialize();
            Singleton<ThreadLocalStorage<AtomicJobCompositor, 8>>::inst()->Initialize();
            CScriptEngine::TLSetCurrentQuestID(0);
        }
    }

    ~RecursiveCounter() {
        L2_GUARD_FUNCTION();
        RunScriptCallStatus& status = CurrentRunScriptCallStatus();
        --status.nRecursiveCount;
        if (status.nRecursiveCount == 0) {
            AtomicJobCompositor& compositor = *Singleton<ThreadLocalStorage<AtomicJobCompositor, 8>>::inst();
            if (status.nLastErrno == 0) {
                compositor.Process();
                compositor.Clear();
            } else if (compositor.IsEmpty()) {
                // Noise: The script error has already been reported, and this compositor has no jobs to cancel.
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] All of atomic job is canceled by "
                //     L"error=%d, TLAtomicJobCompositor is Empty",
                //     L"ScriptEngine.cpp",
                //     0xed,
                //     status.nLastErrno)
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] All of atomic job is canceled by "
                //     L"error=%d",
                //     L"ScriptEngine.cpp",
                //     0xe6,
                //     status.nLastErrno)
                // Log.Add(LOG_WARN,
                //     "[AI] Discarding pending atomic jobs after script error %d.",
                //     status.nLastErrno)
                L2_LOG_WARN(Log, "[AI] Discarding pending atomic jobs after script error %d.", status.nLastErrno);
                compositor.Clear();
            }
        }

        ClearReturnedScriptContext(CThreadManager::GetThreadID(), status.nRecursiveCount);
    }
};

template <typename CallSuper>
void Execute(
    L2_OP* const* instructionStorage,
    CScriptBinCode& code,
    ScriptEvent& ev,
    CNPCEventHandler* handler,
    CAIParameter* params,
    CAIProperty* dispatchProperties,
    CallSuper& callSuper,
    RunResult& result
);

struct Vm {
    ScriptEvent& ev;
    CAIParameter* params;
    CAIProperty* properties;
    RunResult& result;
    // PDB types F365/F368 and the current prologue both show raw local
    // arrays. In particular, a target movss leaves the upper half of an
    // eight-byte value cell untouched.
    std::array<std::int64_t, kStackSlots> values;
    std::array<char, kStackSlots> types;
    // Provenance is separate from the legacy integer/float tag. Compiled AI
    // offsets identify fields. Offset() describes their current native storage.
    struct Address {
        ScriptTypeInfo* object = nullptr;
        ScriptVarInfo* field = nullptr;
        int stackSlot = -1;
    };
    std::array<Address, kStackSlots> addresses{};
    int sp = -1;

    Vm(ScriptEvent& event, CAIParameter* parameters, CAIProperty* propertyRoot, RunResult& runResult) :
        ev(event), params(parameters), properties(propertyRoot), result(runResult) {}

    float FloatAt(int slot) const {
        float value = 0.0f;
        std::memcpy(&value, &values[static_cast<std::size_t>(slot)], sizeof(value));
        return value;
    }

    void StoreFloatBits(int slot, float value) {
        // The x64 target uses movss/a four-byte store into an eight-byte
        // stack cell. Its upper half is left untouched.
        std::memcpy(&values[static_cast<std::size_t>(slot)], &value, sizeof(value));
    }

    void SetFloat(int slot, float value) {
        StoreFloatBits(slot, value);
        types[static_cast<std::size_t>(slot)] = 1;
        addresses[static_cast<std::size_t>(slot)] = {};
    }

    bool Reserve();

    bool Push(std::int64_t value, char type) {
        if (!Reserve()) {
            return false;
        }
        values[static_cast<std::size_t>(sp)] = value;
        types[static_cast<std::size_t>(sp)] = type;
        return true;
    }

    bool Need(int minimum) const { return sp >= minimum; }

    void SetObject(int slot, ScriptTypeInfo* type) {
        addresses[static_cast<std::size_t>(slot)] = {type && type->IsClass() ? type : nullptr};
    }

    bool AddField(int left, int right) {
        const Address base = addresses[static_cast<std::size_t>(left)];
        addresses[static_cast<std::size_t>(left)] = {};
        if (base.stackSlot >= 0 && values[right] % static_cast<std::int64_t>(sizeof(values[0])) == 0) {
            const std::int64_t slot = base.stackSlot + values[right] / static_cast<std::int64_t>(sizeof(values[0]));
            if (slot >= 0 && slot < kStackSlots) {
                values[left] = runtime::AddInt64Wrapping(values[left], values[right]);
                addresses[left].stackSlot = static_cast<int>(slot);
                return true;
            }
        }
        if (!base.object || values[right] < 0 || values[right] > INT32_MAX) {
            return false;
        }
        ScriptVarInfo* field = base.object->CompiledField(static_cast<int>(values[right]));
        if (!field) {
            return false;
        }
        values[left] = runtime::AddInt64Wrapping(values[left], field->Offset());
        addresses[left].field = field;
        return true;
    }

    Address FetchedAddress(int slot) const {
        const Address& source = addresses[static_cast<std::size_t>(slot)];
        if (source.stackSlot >= 0 && source.stackSlot < kStackSlots) {
            return addresses[static_cast<std::size_t>(source.stackSlot)];
        }
        if (source.field) {
            ScriptTypeInfo* type = source.field->Type();
            return {type && type->IsClass() ? type : nullptr};
        }
        return {};
    }

    void Fail(const std::string& note [[maybe_unused]], int errorCode) {
        result.stopped = true;
        if (errorCode != 0) {
            CurrentRunScriptCallStatus().nLastErrno = errorCode;
        }
    }

    void StopWithoutScriptError() { result.stopped = true; }

    std::int64_t NativeCall(ScriptFuncInfo& function, int selfSlot) {
        return function.Invoke(values[static_cast<std::size_t>(selfSlot)], values.data() + selfSlot + 1);
    }
};

bool Vm::Reserve() {
    // Every shipping push increments the signed 32-bit stack pointer and
    // checks only whether the result is negative. There is no upper-bound
    // guard before the fixed 100-cell local arrays.
    sp = runtime::AddInt32Wrapping(sp, 1);
    if (sp >= 0) {
        PublishScriptStackPointer(sp);
    }
    if (sp >= 0) {
        addresses[static_cast<std::size_t>(sp)] = {};
    }
    return sp >= 0;
}

int StackUnderflowSourceLine(std::uint8_t opcode) {
    switch (opcode) {
        case OP_PUSH_EVENT:
            return 0x16e;
        case OP_ADD:
            return 0x17e;
        case OP_ADD_STRING:
            return 0x1b0;
        case OP_SUB:
            return 0x1f8;
        case OP_MUL:
            return 0x22b;
        case OP_DIV:
            return 0x24f;
        case OP_MOD:
            return 0x273;
        case OP_FETCH_I:
            return 0x28f;
        case OP_FETCH_I4:
            return 0x2b3;
        case OP_FETCH_F:
            return 0x2d8;
        case OP_FETCH_D:
            return 0x2f7;
        case OP_EQUAL:
            return 0x317;
        case OP_NOT_EQUAL:
            return 0x337;
        case OP_LESS:
            return 0x357;
        case OP_LESS_EQUAL:
            return 0x37f;
        case OP_GREATER_EQUAL:
            return 0x3a7;
        case OP_GREATER:
            return 0x3cf;
        case OP_NEGATE:
            return 0x3f7;
        case OP_NOT:
            return 0x40c;
        case OP_AND:
            return 0x422;
        case OP_OR:
            return 0x43b;
        case OP_BRANCH_FALSE:
            return 0x454;
        case OP_BRANCH_TRUE:
            return 0x480;
        case OP_PUSH_CONST:
            return 0x49a;
        case OP_PUSH_STRING:
            return 0x4b9;
        case OP_PUSH_PARAMETER:
            return 0x4cd;
        case OP_PUSH_PROPERTY:
            return 0x504;
        case OP_PUSH_REG_SP:
            return 0x52d;
        case OP_FUNC_CALL:
            return 0x580;
        case OP_ASSIGN8:
            return 0x618;
        case OP_ASSIGN4:
            return 0x635;
        case OP_BIT_AND:
            return 0x662;
        case OP_BIT_OR:
            return 0x67b;
        case OP_BIT_XOR:
            return 0x694;
        default:
            return 0;
    }
}

// The target's diagnostics dereference the handler/event strings at each log
// site. The portable logger needs wchar_t text, so this carrier performs the
// UTF-16 conversion lazily instead of turning those live pointers into an
// entry-time snapshot.
template <typename Reader>
class LiveWideName {
public:
    explicit LiveWideName(Reader reader) : reader_(std::move(reader)) {}

    const wchar_t* c_str() const {
        cache_ = reader_();
        return cache_.c_str();
    }

private:
    Reader reader_;
    mutable std::wstring cache_;
};

void CopyLastCalledNpcName(char16_t (&destination)[32], const char16_t* source) {
    // Current 0x00510c68 calls wcsncpy_s(dst, 0x20, source, 0x18).
    // Deliberately measuring the raw source keeps its null failure boundary.
    const std::size_t sourceLength = std::char_traits<char16_t>::length(source);
    const std::size_t copyLength = std::min<std::size_t>(sourceLength, 0x18);
    std::copy_n(source, copyLength, destination);
    destination[copyLength] = u'\0';
}

template <typename CallSuper>
void Execute(
    L2_OP* const* instructionStorage,
    CScriptBinCode& code,
    ScriptEvent& ev,
    CNPCEventHandler* handler,
    CAIParameter* params,
    CAIProperty* dispatchProperties,
    CallSuper& callSuper,
    RunResult& result
) {
    const int threadId = ScriptThreadId();
    const int recursiveDepthBefore = g_RunScriptCallStatus[threadId].nRecursiveCount;
    HandlerInfo& lastCalled = g_lastcalledEventHandler[threadId][static_cast<std::size_t>(recursiveDepthBefore)];
    lastCalled.m_callTime = runtime::GetTickCount();
    const int eventId = ev.m_nEventId;
    lastCalled.m_lastEventID = eventId;
    CopyLastCalledNpcName(lastCalled.m_wszNpcName, ev.GetMySelfName());

    const char16_t* const handlerNameUtf16 = handler ? handler->GetAIClassName() : u"";
    const std::string handlerName = Utf16ToUtf8(std::u16string_view(handlerNameUtf16));
    const char16_t* const eventNameUtf16 = ev.GetEventName(eventId);
    const std::string eventName = eventNameUtf16 ? Utf16ToUtf8(std::u16string_view(eventNameUtf16)) : std::string{};
    const LiveWideName handlerNameWide([&]() {
        const char16_t* const value = handler ? handler->GetAIClassName() : u"";
        return Utf16ToWide(std::u16string_view(value));
    });
    const LiveWideName eventNameWide([&]() {
        const char16_t* const value = ev.GetEventName(ev.m_nEventId);
        return value ? Utf16ToWide(std::u16string_view(value)) : std::wstring{};
    });
    const LiveWideName npcNameWide([&]() {
        const char16_t* const value = ev.GetMySelfName();
        return value ? Utf16ToWide(std::u16string_view(value)) : std::wstring{};
    });
    ScriptContextScope scriptContext(handlerName.c_str(), eventId, eventName.c_str());
    g_ScriptNPC[threadId] = StableWideString(ev.GetMySelfName());
    CAutoStartTimer timer(SCRIPT_ENGINE__RUN_NPC_SCRIPT);
    RecursiveCounter recursiveCounter;
    ev.CreateVariable(code.GetVariableList());
    if (const ScriptContext* const context = CurrentScriptContext()) {
        const_cast<ScriptContext*>(context)->nSP = -1;
    }
    Vm vm(ev, params, dispatchProperties, result);
    int pc = 0;
    int retired = 0;

    while (pc >= 0 && pc < code.GetPC()) {
        if (retired >= kInstructionLimit) {
            break;
        }
        ++retired;
        const L2_OP instruction = (*instructionStorage)[static_cast<std::size_t>(pc)];
        UpdateScriptContextInstruction(pc, vm.sp, instruction.optr);
        int nextPc = pc + 1;
        auto& stack = vm.values;
        auto& types = vm.types;
        int& sp = vm.sp;
        // const auto logStackUnderflow =
        //     [&](int sourceLine) {
        const auto logStackUnderflow = [&](int sourceLine [[maybe_unused]]) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] stack underflow nSP[%d] "
            //     L"ai[%s] event[%s] nCount[%d] nPC[%d]",
            //     L"ScriptEngine.cpp",
            //     sourceLine,
            //     sp,
            //     handlerNameWide.c_str(),
            //     eventNameWide.c_str(),
            //     retired,
            //     nextPc)
            // Log.Add(LOG_ERROR,
            //     "[AI] Skipping opcode %u: insufficient stack values after %d instructions."
            //     " Script '%s', event '%s', instruction %d, stack %d.",
            //     static_cast<unsigned int>(instruction.optr), retired, WideToUtf8(handlerNameWide.c_str()).c_str(),
            //     WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp)
            L2_LOG_ERROR(
                Log,
                "[AI] Skipping opcode %u: insufficient stack values after %d instructions."
                " Script '%s', event '%s', instruction %d, stack %d.",
                static_cast<unsigned int>(instruction.optr), retired, WideToUtf8(handlerNameWide.c_str()).c_str(),
                WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp
            );
        };

        // Scalar operations must not keep the previous value's pointer provenance.
        if (sp >= 0 && (instruction.optr == OP_NEGATE || instruction.optr == OP_NOT)) {
            vm.addresses[static_cast<std::size_t>(sp)] = {};
        }
        if (sp >= 1 &&
            (instruction.optr == OP_SUB || instruction.optr == OP_MUL || instruction.optr == OP_DIV ||
                instruction.optr == OP_MOD || (instruction.optr >= OP_EQUAL && instruction.optr <= OP_GREATER) ||
                instruction.optr == OP_AND || instruction.optr == OP_OR || instruction.optr == OP_BIT_AND ||
                instruction.optr == OP_BIT_OR || instruction.optr == OP_BIT_XOR || instruction.optr == OP_ADD_STRING)) {
            vm.addresses[static_cast<std::size_t>(sp - 1)] = {};
        }
        switch (instruction.optr) {
            case OP_PUSH_EVENT:
                if (!vm.Push(static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(&ev)), 0)) {
                    logStackUnderflow(StackUnderflowSourceLine(instruction.optr));
                } else {
                    vm.addresses[static_cast<std::size_t>(sp)].object =
                        ScriptTypeTable::inst()->GetEventType(dynamic_cast<MakerEvent*>(&ev) ? 1 : 0);
                }
                break;

            case OP_PUSH_CONST:
                // Unlike the other push opcodes, the shipping code checks the
                // current nSP before incrementing it.
                if (sp < 0) {
                    logStackUnderflow(StackUnderflowSourceLine(instruction.optr));
                    break;
                }
                if (!vm.Reserve()) {
                    break;
                }
                types[static_cast<std::size_t>(sp)] = instruction.dataType;
                if (instruction.dataType == OPERAND_INTEGER) {
                    stack[static_cast<std::size_t>(sp)] = instruction.data.intData;
                } else if (instruction.dataType == OPERAND_FLOAT) {
                    // The target's movss writes only the low four bytes of the
                    // eight-byte value slot.
                    std::memcpy(&stack[static_cast<std::size_t>(sp)], &instruction.data.floatData, sizeof(float));
                } else {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] [PUSH_CONST] unknown "
                    //     L"type ai[%s] event[%s]",
                    //     L"ScriptEngine.cpp",
                    //     0x4ad,
                    //     handlerNameWide.c_str(),
                    //     eventNameWide.c_str())
                    // Log.Add(LOG_ERROR,
                    //     "[AI] PUSH_CONST cannot load type %u. Leaving the value slot unchanged."
                    //     " Script '%s', event '%s', instruction %d, stack %d.",
                    //     static_cast<unsigned int>(instruction.dataType), WideToUtf8(handlerNameWide.c_str()).c_str(),
                    //     WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp)
                    L2_LOG_ERROR(
                        Log,
                        "[AI] PUSH_CONST cannot load type %u; leaving the value slot unchanged."
                        " Script '%s', event '%s', instruction %d, stack %d.",
                        static_cast<unsigned int>(instruction.dataType), WideToUtf8(handlerNameWide.c_str()).c_str(),
                        WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp
                    );
                }
                break;

            case OP_PUSH_STRING:
                if (sp < 0) {
                    logStackUnderflow(StackUnderflowSourceLine(instruction.optr));
                    break;
                }
                vm.Push(instruction.data.intData, 0);
                break;

            case OP_PUSH_PARAMETER:
            case OP_PUSH_PROPERTY: {
                const bool parameterOperand = instruction.optr == OP_PUSH_PARAMETER;
                const char16_t* const scriptName =
                    reinterpret_cast<const char16_t*>(static_cast<std::intptr_t>(instruction.data.intData));
                if (!vm.Reserve()) {
                    logStackUnderflow(StackUnderflowSourceLine(instruction.optr));
                    break;
                }
                AIConstantData value;
                if (parameterOperand) {
                    value = vm.params->GetData(scriptName);
                } else {
                    value = vm.properties->GetData(const_cast<char16_t*>(scriptName));
                }
                if (value.type == &GetBuiltinScriptType(ScriptTypeId::Float)) {
                    vm.SetFloat(sp, value.data.floatData);
                } else if (value.type == &GetBuiltinScriptType(ScriptTypeId::Int)) {
                    stack[static_cast<std::size_t>(sp)] = value.data.intData;
                    types[static_cast<std::size_t>(sp)] = 0;
                } else if (value.type == &GetBuiltinScriptType(ScriptTypeId::String)) {
                    stack[static_cast<std::size_t>(sp)] =
                        static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(value.data.stringData));
                    types[static_cast<std::size_t>(sp)] = 0;
                } else if (value.type == &GetBuiltinScriptType(ScriptTypeId::TelPosList) ||
                    (parameterOperand &&
                        (value.type == &GetBuiltinScriptType(ScriptTypeId::WayPoints) ||
                            value.type == &GetBuiltinScriptType(ScriptTypeId::WayPointDelays))) ||
                    (!parameterOperand && value.type == &GetBuiltinScriptType(ScriptTypeId::BuySellList))) {
                    stack[static_cast<std::size_t>(sp)] =
                        static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(value.data.pointerData));
                    types[static_cast<std::size_t>(sp)] = 0;
                    vm.SetObject(sp, value.type);
                } else if (parameterOperand) {
                    const std::wstring parameterName =
                        scriptName ? Utf16ToWide(std::u16string_view(scriptName)) : std::wstring{};
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] Abnormal parameter [%s] "
                    //     L"ai[%s] event[%s]",
                    //     L"ScriptEngine.cpp",
                    //     0x4f6,
                    //     parameterName.c_str(),
                    //     handlerNameWide.c_str(),
                    //     eventNameWide.c_str())
                    // Log.Add(LOG_ERROR,
                    //     "[AI] Parameter '%s' has an unsupported type. Substituting zero."
                    //     " Script '%s', event '%s', instruction %d, stack %d.",
                    //     WideToUtf8(parameterName).c_str(), WideToUtf8(handlerNameWide.c_str()).c_str(),
                    //     WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp)
                    L2_LOG_ERROR(
                        Log,
                        "[AI] Parameter '%s' has an unsupported type; substituting zero."
                        " Script '%s', event '%s', instruction %d, stack %d.",
                        WideToUtf8(parameterName).c_str(), WideToUtf8(handlerNameWide.c_str()).c_str(),
                        WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp
                    );
                    stack[static_cast<std::size_t>(sp)] = 0;
                    types[static_cast<std::size_t>(sp)] = 0;
                }
                break;
            }

            case OP_PUSH_REG_SP:
                if (sp < -1) {
                    logStackUnderflow(StackUnderflowSourceLine(instruction.optr));
                    break;
                }
                {
                    const std::intptr_t address = reinterpret_cast<std::intptr_t>(stack.data()) +
                        static_cast<std::intptr_t>(sp) * static_cast<std::intptr_t>(sizeof(stack[0]));
                    const int sourceSlot = sp;
                    if (vm.Push(static_cast<std::int64_t>(address), 0)) {
                        vm.addresses[static_cast<std::size_t>(sp)].stackSlot = sourceSlot;
                    }
                }
                break;

            case OP_SHIFT_SP: {
                AdjustScriptStackPointer(sp, static_cast<std::int32_t>(instruction.data.intData));
                break;
            }

            case OP_ADD:
            case OP_SUB:
            case OP_MUL:
            case OP_DIV: {
                if (!vm.Need(1)) {
                    logStackUnderflow(StackUnderflowSourceLine(instruction.optr));
                    break;
                }
                const int left = sp - 1;
                const int right = sp;
                const char combinedType = static_cast<char>(
                    static_cast<unsigned char>(types[static_cast<std::size_t>(left)]) |
                    static_cast<unsigned char>(types[static_cast<std::size_t>(right)])
                );
                if (types[static_cast<std::size_t>(left)] == 0 && types[static_cast<std::size_t>(right)] == 0) {
                    const std::int64_t lhs = stack[static_cast<std::size_t>(left)];
                    const std::int64_t rhs = stack[static_cast<std::size_t>(right)];
                    if (instruction.optr == OP_ADD) {
                        if (!vm.AddField(left, right)) {
                            stack[static_cast<std::size_t>(left)] = runtime::AddInt64Wrapping(lhs, rhs);
                        }
                    } else if (instruction.optr == OP_SUB) {
                        stack[static_cast<std::size_t>(left)] = runtime::SubtractInt64Wrapping(lhs, rhs);
                    } else if (instruction.optr == OP_MUL) {
                        stack[static_cast<std::size_t>(left)] = runtime::MultiplyInt64Wrapping(lhs, rhs);
                    } else {
                        stack[static_cast<std::size_t>(left)] = lhs / rhs;
                    }
                    types[static_cast<std::size_t>(left)] = 0;
                } else {
                    const auto convertIntegerOperand = [&](int slot) {
                        const std::int64_t integer = stack[static_cast<std::size_t>(slot)];
                        // Both ADD and SUB contain this exact diagnostic
                        // before cvtsi2ss. The comparison is signed and
                        // strict.
                        if ((instruction.optr == OP_ADD || instruction.optr == OP_SUB) &&
                            integer > static_cast<std::int64_t>(0x80000000ULL)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"integer[%d] in stack is greater "
                            //     L"than int32. maybe int64. "
                            //     L"ai[%s] event[%s]",
                            //     static_cast<
                            //         std::int32_t>(
                            //             integer),
                            //     handlerNameWide.c_str(),
                            //     eventNameWide.c_str())
                            // Log.Add(LOG_WARN,
                            //     "[AI] Converting stack integer %lld to float. Precision may be lost."
                            //     " Script '%s', event '%s', instruction %d, stack %d.",
                            //     static_cast<long long>(integer), WideToUtf8(handlerNameWide.c_str()).c_str(),
                            //     WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp)
                            L2_LOG_WARN(
                                Log,
                                "[AI] Converting stack integer %lld to float; precision may be lost."
                                " Script '%s', event '%s', instruction %d, stack %d.",
                                static_cast<long long>(integer), WideToUtf8(handlerNameWide.c_str()).c_str(),
                                WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp
                            );
                        }
                        return static_cast<float>(integer);
                    };
                    const float lhs = types[static_cast<std::size_t>(left)] ? vm.FloatAt(left) : convertIntegerOperand(left);
                    const float rhs =
                        types[static_cast<std::size_t>(right)] ? vm.FloatAt(right) : convertIntegerOperand(right);
                    float value = 0.0f;
                    if (instruction.optr == OP_ADD) {
                        value = lhs + rhs;
                    } else if (instruction.optr == OP_SUB) {
                        value = lhs - rhs;
                    } else if (instruction.optr == OP_MUL) {
                        value = lhs * rhs;
                    } else {
                        value = lhs / rhs;
                    }
                    vm.StoreFloatBits(left, value);
                    vm.addresses[static_cast<std::size_t>(left)] = {};
                    // The shipping handlers OR the two raw tag bytes. Ordinary
                    // bytecode uses only 0/1, but keep the exact behavior for
                    // malformed or hand-authored objects as well.
                    types[static_cast<std::size_t>(left)] = combinedType;
                }
                if (!result.stopped) {
                    if (instruction.optr == OP_ADD || instruction.optr == OP_SUB) {
                        types[static_cast<std::size_t>(right)] = 0;
                    }
                    AdjustScriptStackPointer(sp, -1);
                }
                break;
            }

            case OP_MOD:
                if (!vm.Need(1)) {
                    logStackUnderflow(StackUnderflowSourceLine(instruction.optr));
                    break;
                }
                {
                    const int left = sp - 1;
                    if (types[static_cast<std::size_t>(left)] || types[static_cast<std::size_t>(sp)]) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%s][%d] [MOD] type is not "
                        //     L"integer. ai[%s] event[%s] "
                        //     L"nCount[%d] nPC[%d]",
                        //     L"ScriptEngine.cpp",
                        //     0x27a,
                        //     handlerNameWide.c_str(),
                        //     eventNameWide.c_str(),
                        //     retired,
                        //     nextPc)
                        // Log.Add(LOG_ERROR,
                        //     "[AI] MOD requires integer operands. Substituting zero."
                        //     " Script '%s', event '%s', instruction %d, stack %d.",
                        //     WideToUtf8(handlerNameWide.c_str()).c_str(),
                        //     WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp)
                        L2_LOG_ERROR(
                            Log,
                            "[AI] MOD requires integer operands; substituting zero."
                            " Script '%s', event '%s', instruction %d, stack %d.",
                            WideToUtf8(handlerNameWide.c_str()).c_str(), WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp
                        );
                        stack[static_cast<std::size_t>(left)] = 0;
                        types[static_cast<std::size_t>(left)] = 0;
                    } else {
                        const std::int64_t lhs = stack[static_cast<std::size_t>(left)];
                        const std::int64_t rhs = stack[static_cast<std::size_t>(sp)];
                        stack[static_cast<std::size_t>(left)] = lhs % rhs;
                    }
                    AdjustScriptStackPointer(sp, -1);
                }
                break;

            case OP_FETCH_I:
            case OP_FETCH_I4:
            case OP_FETCH_F:
            case OP_FETCH_D:
                if (!vm.Need(0)) {
                    logStackUnderflow(StackUnderflowSourceLine(instruction.optr));
                    break;
                }
                // Only tag 1 is a float in these four target handlers. Other
                // nonzero tags still follow the pointer path.
                if (types[static_cast<std::size_t>(sp)] == OPERAND_FLOAT) {
                    if (instruction.optr == OP_FETCH_I || instruction.optr == OP_FETCH_I4) {
                        // const int typeLine =
                        //     instruction.optr == OP_FETCH_I
                        //         ? 0x296
                        //         : 0x2ba.
                        // const int contextLine =
                        //     instruction.optr == OP_FETCH_I
                        //         ? 0x298
                        //         : 0x2bc.
                        // Log.Add(
                        //     LOG_ERROR,
                        //     instruction.optr == OP_FETCH_I
                        //         ? L"[%s][%d] FETCH_I : Not a pointer "
                        //           L"type on the top of the stack."
                        //         : L"[%s][%d] FETCH_I4 : Not a pointer "
                        //           L"type on the top of the stack.",
                        //     L"ScriptEngine.cpp",
                        //     typeLine)
                        // Log.Add(LOG_ERROR,
                        //     "[AI] %s requires a pointer, but the stack holds a float. Skipping the instruction (event ID
                        //     %d)." " Script '%s', event '%s', instruction %d, stack %d.", instruction.optr == OP_FETCH_I ?
                        //     "FETCH_I" : "FETCH_I4", ev.m_nEventId, WideToUtf8(handlerNameWide.c_str()).c_str(),
                        //     WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp)
                        L2_LOG_ERROR(
                            Log,
                            "[AI] %s requires a pointer, but the stack holds a float; skipping the instruction (event ID "
                            "%d)."
                            " Script '%s', event '%s', instruction %d, stack %d.",
                            instruction.optr == OP_FETCH_I ? "FETCH_I" : "FETCH_I4", ev.m_nEventId,
                            WideToUtf8(handlerNameWide.c_str()).c_str(), WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp
                        );
                        // Noise: The preceding error now includes the script, event ID/name, instruction and stack position
                        // in the same record. Log.Add(
                        //     LOG_ERROR,
                        //     L"[%s][%d]\t\t\tEvent[%s][%d], "
                        //     L"Handler[%s], nPC[%d], nSP[%d] ",
                        //     L"ScriptEngine.cpp",
                        //     contextLine,
                        //     eventNameWide.c_str(),
                        //     ev.m_nEventId,
                        //     handlerNameWide.c_str(),
                        //     nextPc,
                        //     sp)
                        ReportScriptContext(LOG_ERROR);
                    } else {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     instruction.optr == OP_FETCH_F
                        //         ? L"[%s][%d] FETCH_F: Not a pointer "
                        //           L"type on the top of the stack. "
                        //           L"ai[%s] event[%s]"
                        //         : L"[%s][%d] FETCH_D: Not a pointer "
                        //           L"type on the top of the stack. "
                        //           L"ai[%s] event[%s]",
                        //     L"ScriptEngine.cpp",
                        //     instruction.optr == OP_FETCH_F
                        //         ? 0x2e0
                        //         : 0x2ff,
                        //     handlerNameWide.c_str(),
                        //     eventNameWide.c_str())
                        // Log.Add(LOG_ERROR,
                        //     "[AI] %s requires a pointer, but the stack holds a float. Skipping the instruction."
                        //     " Script '%s', event '%s', instruction %d, stack %d.",
                        //     instruction.optr == OP_FETCH_F ? "FETCH_F" : "FETCH_D",
                        //     WideToUtf8(handlerNameWide.c_str()).c_str(), WideToUtf8(eventNameWide.c_str()).c_str(), pc,
                        //     sp)
                        L2_LOG_ERROR(
                            Log,
                            "[AI] %s requires a pointer, but the stack holds a float; skipping the instruction."
                            " Script '%s', event '%s', instruction %d, stack %d.",
                            instruction.optr == OP_FETCH_F ? "FETCH_F" : "FETCH_D",
                            WideToUtf8(handlerNameWide.c_str()).c_str(), WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp
                        );
                    }
                    break;
                }
                {
                    const std::int64_t pointer = stack[static_cast<std::size_t>(sp)];
                    if (pointer < kAccessMinimum) {
                        switch (instruction.optr) {
                            case OP_FETCH_I:
                                // Log.Add(
                                //     LOG_ERROR,
                                //     L"fetch_i : Access violation. "
                                //     L"Read[%x], ai[%s] event[%s], "
                                //     L"pc[%d], sp[%d]",
                                //     pointer,
                                //     handlerNameWide.c_str(),
                                //     eventNameWide.c_str(),
                                //     pc,
                                //     sp)
                                // Log.Add(LOG_ERROR,
                                //     "[AI] FETCH_I rejected address 0x%llx. Stopping the handler."
                                //     " Script '%s', event '%s', instruction %d, stack %d.",
                                //     static_cast<unsigned long long>(pointer), WideToUtf8(handlerNameWide.c_str()).c_str(),
                                //     WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp)
                                L2_LOG_ERROR(
                                    Log,
                                    "[AI] FETCH_I rejected address 0x%llx; stopping the handler."
                                    " Script '%s', event '%s', instruction %d, stack %d.",
                                    static_cast<unsigned long long>(pointer), WideToUtf8(handlerNameWide.c_str()).c_str(),
                                    WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp
                                );
                                break;
                            case OP_FETCH_I4:
                                // Log.Add(
                                //     LOG_ERROR,
                                //     L"fetch_i4 : Access violation. "
                                //     L"Read[%x], ai[%s] event[%s], "
                                //     L"pc[%d], sp[%d]",
                                //     pointer,
                                //     handlerNameWide.c_str(),
                                //     eventNameWide.c_str(),
                                //     pc,
                                //     sp)
                                // Log.Add(LOG_ERROR,
                                //     "[AI] FETCH_I4 rejected address 0x%llx. Stopping the handler."
                                //     " Script '%s', event '%s', instruction %d, stack %d.",
                                //     static_cast<unsigned long long>(pointer), WideToUtf8(handlerNameWide.c_str()).c_str(),
                                //     WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp)
                                L2_LOG_ERROR(
                                    Log,
                                    "[AI] FETCH_I4 rejected address 0x%llx; stopping the handler."
                                    " Script '%s', event '%s', instruction %d, stack %d.",
                                    static_cast<unsigned long long>(pointer), WideToUtf8(handlerNameWide.c_str()).c_str(),
                                    WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp
                                );
                                break;
                            case OP_FETCH_F:
                                // Log.Add(
                                //     LOG_ERROR,
                                //     L"fetch_f : Access violation. "
                                //     L"Read[%x], ai[%s], pc[%d], "
                                //     L"sp[%d] event[%s]",
                                //     pointer,
                                //     handlerNameWide.c_str(),
                                //     pc,
                                //     sp,
                                //     eventNameWide.c_str())
                                // Log.Add(LOG_ERROR,
                                //     "[AI] FETCH_F rejected address 0x%llx. Stopping the handler."
                                //     " Script '%s', event '%s', instruction %d, stack %d.",
                                //     static_cast<unsigned long long>(pointer), WideToUtf8(handlerNameWide.c_str()).c_str(),
                                //     WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp)
                                L2_LOG_ERROR(
                                    Log,
                                    "[AI] FETCH_F rejected address 0x%llx; stopping the handler."
                                    " Script '%s', event '%s', instruction %d, stack %d.",
                                    static_cast<unsigned long long>(pointer), WideToUtf8(handlerNameWide.c_str()).c_str(),
                                    WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp
                                );
                                break;
                            default:
                                // Log.Add(
                                //     LOG_ERROR,
                                //     L"fetch_d : Access violation. "
                                //     L"Read[%x], ai[%s], pc[%d], "
                                //     L"sp[%d] event[%s]",
                                //     pointer,
                                //     handlerNameWide.c_str(),
                                //     pc,
                                //     sp,
                                //     eventNameWide.c_str())
                                // Log.Add(LOG_ERROR,
                                //     "[AI] FETCH_D rejected address 0x%llx. Stopping the handler."
                                //     " Script '%s', event '%s', instruction %d, stack %d.",
                                //     static_cast<unsigned long long>(pointer), WideToUtf8(handlerNameWide.c_str()).c_str(),
                                //     WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp)
                                L2_LOG_ERROR(
                                    Log,
                                    "[AI] FETCH_D rejected address 0x%llx; stopping the handler."
                                    " Script '%s', event '%s', instruction %d, stack %d.",
                                    static_cast<unsigned long long>(pointer), WideToUtf8(handlerNameWide.c_str()).c_str(),
                                    WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp
                                );
                                break;
                        }
                        vm.Fail("fetch access violation", 3);
                        break;
                    }
                    const void* source = reinterpret_cast<const void*>(static_cast<std::intptr_t>(pointer));
                    const Vm::Address fetched = instruction.optr == OP_FETCH_I ? vm.FetchedAddress(sp) : Vm::Address{};
                    vm.addresses[static_cast<std::size_t>(sp)] = fetched;
                    if (instruction.optr == OP_FETCH_I) {
                        std::memcpy(&stack[static_cast<std::size_t>(sp)], source, sizeof(std::int64_t));
                    } else if (instruction.optr == OP_FETCH_I4) {
                        std::int32_t value = 0;
                        std::memcpy(&value, source, sizeof(value));
                        stack[static_cast<std::size_t>(sp)] = value;
                    } else if (instruction.optr == OP_FETCH_F) {
                        float value = 0.0f;
                        std::memcpy(&value, source, sizeof(value));
                        vm.SetFloat(sp, value);
                    } else {
                        double value = 0.0;
                        std::memcpy(&value, source, sizeof(value));
                        vm.SetFloat(sp, static_cast<float>(value));
                    }
                }
                break;

            case OP_EQUAL:
            case OP_NOT_EQUAL:
            case OP_LESS:
            case OP_LESS_EQUAL:
            case OP_GREATER_EQUAL:
            case OP_GREATER: {
                if (!vm.Need(1)) {
                    logStackUnderflow(StackUnderflowSourceLine(instruction.optr));
                    break;
                }
                const int left = sp - 1;
                const int right = sp;
                bool value = false;
                const char leftTag = types[static_cast<std::size_t>(left)];
                const char rightTag = types[static_cast<std::size_t>(right)];
                const bool equality = instruction.optr == OP_EQUAL || instruction.optr == OP_NOT_EQUAL;
                const bool leftFloat = equality ? leftTag == OPERAND_FLOAT : leftTag != 0;
                const bool rightFloat = equality ? rightTag == OPERAND_FLOAT : rightTag != 0;
                if (equality && leftTag != rightTag) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     instruction.optr == OP_EQUAL
                    //         ? L"[%s][%d] [EQUAL] type is "
                    //           L"different. ai[%s] event[%s]"
                    //         : L"[%s][%d] [NOT_EQUAL] type is "
                    //           L"different. ai[%s] event[%s]",
                    //     L"ScriptEngine.cpp",
                    //     instruction.optr == OP_EQUAL
                    //         ? 0x31f
                    //         : 0x33f,
                    //     handlerNameWide.c_str(),
                    //     eventNameWide.c_str())
                    // Log.Add(LOG_ERROR,
                    //     "[AI] %s operands have different type tags (%d and %d). Using false."
                    //     " Script '%s', event '%s', instruction %d, stack %d.",
                    //     instruction.optr == OP_EQUAL ? "EQUAL" : "NOT_EQUAL",
                    //     static_cast<int>(leftTag), static_cast<int>(rightTag),
                    //     WideToUtf8(handlerNameWide.c_str()).c_str(), WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp)
                    L2_LOG_ERROR(
                        Log,
                        "[AI] %s operands have different type tags (%d and %d); using false."
                        " Script '%s', event '%s', instruction %d, stack %d.",
                        instruction.optr == OP_EQUAL ? "EQUAL" : "NOT_EQUAL", static_cast<int>(leftTag),
                        static_cast<int>(rightTag), WideToUtf8(handlerNameWide.c_str()).c_str(),
                        WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp
                    );
                } else if (!leftFloat && !rightFloat) {
                    const std::int64_t lhs = stack[static_cast<std::size_t>(left)];
                    const std::int64_t rhs = stack[static_cast<std::size_t>(right)];
                    if (instruction.optr == OP_EQUAL) {
                        value = lhs == rhs;
                    } else if (instruction.optr == OP_NOT_EQUAL) {
                        value = lhs != rhs;
                    } else if (instruction.optr == OP_LESS) {
                        value = lhs < rhs;
                    } else if (instruction.optr == OP_LESS_EQUAL) {
                        value = lhs <= rhs;
                    } else if (instruction.optr == OP_GREATER_EQUAL) {
                        value = lhs >= rhs;
                    } else {
                        value = lhs > rhs;
                    }
                } else {
                    const float lhs =
                        leftFloat ? vm.FloatAt(left) : static_cast<float>(stack[static_cast<std::size_t>(left)]);
                    const float rhs =
                        rightFloat ? vm.FloatAt(right) : static_cast<float>(stack[static_cast<std::size_t>(right)]);
                    if (instruction.optr == OP_EQUAL) {
                        value = lhs == rhs;
                    } else if (instruction.optr == OP_NOT_EQUAL) {
                        value = lhs != rhs;
                    } else if (instruction.optr == OP_LESS) {
                        value = lhs < rhs;
                    } else if (instruction.optr == OP_LESS_EQUAL) {
                        value = lhs <= rhs;
                    } else if (instruction.optr == OP_GREATER_EQUAL) {
                        value = lhs >= rhs;
                    } else {
                        value = lhs > rhs;
                    }
                }
                stack[static_cast<std::size_t>(left)] = value ? 1 : 0;
                types[static_cast<std::size_t>(left)] = 0;
                // EQUAL/NOT_EQUAL clear only the surviving tag. The ordered
                // comparisons use a two-byte store and also clear the popped
                // slot, which can later be observed by an unknown property push.
                if (!equality) {
                    types[static_cast<std::size_t>(right)] = 0;
                }
                AdjustScriptStackPointer(sp, -1);
                break;
            }

            case OP_NEGATE:
                if (!vm.Need(0)) {
                    logStackUnderflow(StackUnderflowSourceLine(instruction.optr));
                    break;
                }
                if (types[static_cast<std::size_t>(sp)] == OPERAND_FLOAT) {
                    vm.SetFloat(sp, -vm.FloatAt(sp));
                } else {
                    stack[static_cast<std::size_t>(sp)] =
                        runtime::SubtractInt64Wrapping(0, stack[static_cast<std::size_t>(sp)]);
                }
                break;

            case OP_NOT:
                if (!vm.Need(0)) {
                    logStackUnderflow(StackUnderflowSourceLine(instruction.optr));
                    break;
                }
                if (types[static_cast<std::size_t>(sp)] == OPERAND_FLOAT) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] OP_NOT: Not a pointer "
                    //     L"type on the top of the stack. "
                    //     L"ai[%s] event[%s]",
                    //     L"ScriptEngine.cpp",
                    //     0x414,
                    //     handlerNameWide.c_str(),
                    //     eventNameWide.c_str())
                    // Log.Add(LOG_ERROR,
                    //     "[AI] NOT requires an integer, but the stack holds a float. Leaving the value unchanged."
                    //     " Script '%s', event '%s', instruction %d, stack %d.",
                    //     WideToUtf8(handlerNameWide.c_str()).c_str(),
                    //     WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp)
                    L2_LOG_ERROR(
                        Log,
                        "[AI] NOT requires an integer, but the stack holds a float; leaving the value unchanged."
                        " Script '%s', event '%s', instruction %d, stack %d.",
                        WideToUtf8(handlerNameWide.c_str()).c_str(), WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp
                    );
                } else {
                    stack[static_cast<std::size_t>(sp)] =
                        static_cast<std::int64_t>(~static_cast<std::uint64_t>(stack[static_cast<std::size_t>(sp)]));
                }
                break;

            case OP_AND:
            case OP_OR:
            case OP_BIT_AND:
            case OP_BIT_OR:
            case OP_BIT_XOR: {
                if (!vm.Need(1)) {
                    logStackUnderflow(StackUnderflowSourceLine(instruction.optr));
                    break;
                }
                const int left = sp - 1;
                const bool integerOperands =
                    types[static_cast<std::size_t>(left)] == 0 && types[static_cast<std::size_t>(sp)] == 0;
                if (!integerOperands) {
                    // const wchar_t* message =
                    //     instruction.optr == OP_AND
                    //         ? L"[%s][%d] [AND] floattype! "
                    //           L"ai[%s] event[%s]"
                    //     : instruction.optr == OP_OR
                    //         ? L"[%s][%d] [OR] floattype! "
                    //           L"ai[%s] event[%s]"
                    //     : instruction.optr == OP_BIT_AND
                    //         ? L"[%s][%d] Bitwise And - no "
                    //           L"floattype! ai[%s] event[%s]"
                    //     : instruction.optr == OP_BIT_OR
                    //         ? L"[%s][%d] Bitwise Or - no "
                    //           L"floattype! ai[%s] event[%s]"
                    //         : L"[%s][%d] Xor - no floattype! "
                    //           L"ai[%s] event[%s]".
                    // const int sourceLine =
                    //     instruction.optr == OP_AND
                    //         ? 0x42a
                    //     : instruction.optr == OP_OR
                    //         ? 0x443
                    //     : instruction.optr == OP_BIT_AND
                    //         ? 0x66a
                    //     : instruction.optr == OP_BIT_OR
                    //         ? 0x683
                    //         : 0x69c.
                    // Log.Add(
                    //     LOG_ERROR,
                    //     message,
                    //     L"ScriptEngine.cpp",
                    //     sourceLine,
                    //     handlerNameWide.c_str(),
                    //     eventNameWide.c_str())
                    // Log.Add(LOG_ERROR,
                    //     "[AI] %s requires integer operands. Leaving the left value unchanged and removing the right
                    //     operand." " Script '%s', event '%s', instruction %d, stack %d.", instruction.optr == OP_AND ?
                    //     "AND" : instruction.optr == OP_OR ? "OR" : instruction.optr == OP_BIT_AND ? "BIT_AND" :
                    //     instruction.optr == OP_BIT_OR ? "BIT_OR" : "BIT_XOR", WideToUtf8(handlerNameWide.c_str()).c_str(),
                    //     WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp)
                    L2_LOG_ERROR(
                        Log,
                        "[AI] %s requires integer operands; leaving the left value unchanged and removing the right operand."
                        " Script '%s', event '%s', instruction %d, stack %d.",
                        instruction.optr == OP_AND           ? "AND"
                            : instruction.optr == OP_OR      ? "OR"
                            : instruction.optr == OP_BIT_AND ? "BIT_AND"
                            : instruction.optr == OP_BIT_OR  ? "BIT_OR"
                                                             : "BIT_XOR",
                        WideToUtf8(handlerNameWide.c_str()).c_str(), WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp
                    );
                } else if (instruction.optr == OP_AND) {
                    stack[static_cast<std::size_t>(left)] =
                        stack[static_cast<std::size_t>(left)] != 0 && stack[static_cast<std::size_t>(sp)] != 0;
                    types[static_cast<std::size_t>(left)] = 0;
                } else if (instruction.optr == OP_OR) {
                    stack[static_cast<std::size_t>(left)] =
                        stack[static_cast<std::size_t>(left)] != 0 || stack[static_cast<std::size_t>(sp)] != 0;
                    types[static_cast<std::size_t>(left)] = 0;
                } else {
                    const std::uint64_t lhs = static_cast<std::uint64_t>(stack[static_cast<std::size_t>(left)]);
                    const std::uint64_t rhs = static_cast<std::uint64_t>(stack[static_cast<std::size_t>(sp)]);
                    const std::uint64_t value = instruction.optr == OP_BIT_AND ? lhs & rhs
                        : instruction.optr == OP_BIT_OR                        ? lhs | rhs
                                                                               : lhs ^ rhs;
                    stack[static_cast<std::size_t>(left)] = static_cast<std::int64_t>(value);
                    types[static_cast<std::size_t>(left)] = 0;
                }
                AdjustScriptStackPointer(sp, -1);
                break;
            }

            case OP_ASSIGN8:
            case OP_ASSIGN4:
                if (!vm.Need(1)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] [ASSIGN] stack underflow "
                    //     L"nSP[%d] ai[%s] event[%s] nCount[%d]",
                    //     L"ScriptEngine.cpp",
                    //     StackUnderflowSourceLine(
                    //         instruction.optr),
                    //     sp,
                    //     handlerNameWide.c_str(),
                    //     eventNameWide.c_str(),
                    //     retired)
                    // Log.Add(LOG_ERROR,
                    //     "[AI] Assignment requires a destination and a value. Skipping the instruction."
                    //     " Script '%s', event '%s', instruction %d, stack %d.",
                    //     WideToUtf8(handlerNameWide.c_str()).c_str(),
                    //     WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp)
                    L2_LOG_ERROR(
                        Log,
                        "[AI] Assignment requires a destination and a value; skipping the instruction."
                        " Script '%s', event '%s', instruction %d, stack %d.",
                        WideToUtf8(handlerNameWide.c_str()).c_str(), WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp
                    );
                    break;
                }
                {
                    const std::int64_t destination = stack[static_cast<std::size_t>(sp - 1)];
                    if (destination < kAccessMinimum) {
                        ReportScriptContext(LOG_ERROR);
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%s][%d] Script assign error !!!. "
                        //     L"ai[%s], event[%s], nPC[%d]",
                        //     L"ScriptEngine.cpp",
                        //     instruction.optr == OP_ASSIGN8
                        //         ? 0x622
                        //         : 0x63e,
                        //     handlerNameWide.c_str(),
                        //     eventNameWide.c_str(),
                        //     nextPc)
                        // Log.Add(LOG_ERROR,
                        //     "[AI] Assignment of %d bytes rejected destination 0x%llx. Stopping the handler."
                        //     " Script '%s', event '%s', instruction %d, stack %d.",
                        //     instruction.optr == OP_ASSIGN8 ? 8 : 4,
                        //     static_cast<unsigned long long>(destination), WideToUtf8(handlerNameWide.c_str()).c_str(),
                        //     WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp)
                        L2_LOG_ERROR(
                            Log,
                            "[AI] Assignment of %d bytes rejected destination 0x%llx; stopping the handler."
                            " Script '%s', event '%s', instruction %d, stack %d.",
                            instruction.optr == OP_ASSIGN8 ? 8 : 4, static_cast<unsigned long long>(destination),
                            WideToUtf8(handlerNameWide.c_str()).c_str(), WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp
                        );
                        vm.Fail("script assign error", 1);
                        break;
                    }
                    std::memcpy(
                        reinterpret_cast<void*>(static_cast<std::intptr_t>(destination)),
                        &stack[static_cast<std::size_t>(sp)], instruction.optr == OP_ASSIGN8 ? 8 : 4
                    );
                    const int destinationSlot = vm.addresses[static_cast<std::size_t>(sp - 1)].stackSlot;
                    if (destinationSlot >= 0 && destinationSlot < kStackSlots) {
                        vm.addresses[static_cast<std::size_t>(destinationSlot)] =
                            instruction.optr == OP_ASSIGN8 ? vm.addresses[static_cast<std::size_t>(sp)] : Vm::Address{};
                    }
                    AdjustScriptStackPointer(sp, -2);
                }
                break;

            case OP_BRANCH_FALSE:
            case OP_BRANCH_TRUE:
                if (!vm.Need(0)) {
                    logStackUnderflow(StackUnderflowSourceLine(instruction.optr));
                    break;
                }
                if (types[static_cast<std::size_t>(sp)] == OPERAND_FLOAT) {
                    if (instruction.optr == OP_BRANCH_FALSE) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%s][%d] [BRANCH_FALSE] "
                        //     L"floattype, pc[%d], sp[%d] "
                        //     L"ai[%s] event[%s]",
                        //     L"ScriptEngine.cpp",
                        //     0x45c,
                        //     nextPc,
                        //     sp,
                        //     handlerNameWide.c_str(),
                        //     eventNameWide.c_str())
                        // Log.Add(LOG_ERROR,
                        //     "[AI] BRANCH_FALSE cannot use a float condition. Ending this handler."
                        //     " Script '%s', event '%s', instruction %d, stack %d.",
                        //     WideToUtf8(handlerNameWide.c_str()).c_str(),
                        //     WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp)
                        L2_LOG_ERROR(
                            Log,
                            "[AI] BRANCH_FALSE cannot use a float condition; ending this handler."
                            " Script '%s', event '%s', instruction %d, stack %d.",
                            WideToUtf8(handlerNameWide.c_str()).c_str(), WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp
                        );
                        // Noise: Dumping every instruction and stack cell for one type error can flood logs. The preceding
                        // record identifies the failing instruction, script, event and stack position. Log.Add(
                        //     LOG_ERROR,
                        //     L"----- code -----")
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"max_pc[%d]",
                        //     code.GetPC())
                        // for (int index = 0.
                        //      index < code.GetPC()
                        //     ++index) {
                        //     const L2_OP codeItem =
                        //         (*instructionStorage)[
                        //             static_cast<
                        //                 std::size_t>(
                        //                     index)].
                        //     Log.Add(
                        //         LOG_ERROR,
                        //         L"  op[%d] type[%d] ",
                        //         static_cast<int>(
                        //             codeItem.optr),
                        //         static_cast<int>(
                        //             codeItem.dataType))
                        // }
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"----- stack -----")
                        // for (int index = 0.
                        //      index <= sp.
                        //      ++index) {
                        //     Log.Add(
                        //         LOG_ERROR,
                        //         L" type[%d] value[%x]",
                        //         static_cast<int>(
                        //             types[
                        //                 static_cast<
                        //                     std::size_t>(
                        //                         index)]),
                        //         stack[
                        //             static_cast<
                        //                 std::size_t>(
                        //                     index)])
                        // }
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"-----------------")
                    } else {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%s][%d] [BRANCH_TRUE] "
                        //     L"floattype ai[%s] event[%s]",
                        //     L"ScriptEngine.cpp",
                        //     0x488,
                        //     handlerNameWide.c_str(),
                        //     eventNameWide.c_str())
                        // Log.Add(LOG_ERROR,
                        //     "[AI] BRANCH_TRUE cannot use a float condition. Ending this handler."
                        //     " Script '%s', event '%s', instruction %d, stack %d.",
                        //     WideToUtf8(handlerNameWide.c_str()).c_str(),
                        //     WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp)
                        L2_LOG_ERROR(
                            Log,
                            "[AI] BRANCH_TRUE cannot use a float condition; ending this handler."
                            " Script '%s', event '%s', instruction %d, stack %d.",
                            WideToUtf8(handlerNameWide.c_str()).c_str(), WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp
                        );
                    }
                    nextPc = code.GetPC();
                } else {
                    const bool truth = stack[static_cast<std::size_t>(sp)] != 0;
                    if ((instruction.optr == OP_BRANCH_TRUE && truth) || (instruction.optr == OP_BRANCH_FALSE && !truth)) {
                        nextPc = static_cast<int>(instruction.data.intData);
                    }
                }
                AdjustScriptStackPointer(sp, -1);
                break;

            case OP_JUMP:
                nextPc = static_cast<int>(instruction.data.intData);
                break;

            case OP_EXIT_HANDLER:
                nextPc = code.GetPC();
                break;

            case OP_CALL_SUPER: {
                callSuper();
                break;
            }

            case OP_FUNC_CALL: {
                const std::uint32_t encodedId = static_cast<std::uint32_t>(instruction.data.intData);
                const int classId = static_cast<int>((encodedId >> 24) & 0xff);
                const int argumentCount = static_cast<int>((encodedId >> 16) & 0xff);
                const int functionIndex = static_cast<int>(encodedId & 0xffff);
                const int selfSlot = sp - argumentCount;

                ScriptTypeInfo* const typeInfo = ScriptTypeTable::inst()->GetType(classId);
                if (!typeInfo) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"func_call : Undefined Type Info. "
                    //     L"ai[%s] event[%s], pc[%d], sp[%d]",
                    //     handlerNameWide.c_str(),
                    //     eventNameWide.c_str(),
                    //     pc,
                    //     sp)
                    // Log.Add(LOG_ERROR,
                    //     "[AI] Cannot call function %d: class ID %d has no type information. Stopping the handler."
                    //     " Script '%s', event '%s', instruction %d, stack %d.",
                    //     functionIndex, classId, WideToUtf8(handlerNameWide.c_str()).c_str(),
                    //     WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp)
                    L2_LOG_ERROR(
                        Log,
                        "[AI] Cannot call function %d: class ID %d has no type information; stopping the handler."
                        " Script '%s', event '%s', instruction %d, stack %d.",
                        functionIndex, classId, WideToUtf8(handlerNameWide.c_str()).c_str(),
                        WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp
                    );
                    vm.Fail("func_call: undefined type info", 3);
                    break;
                }
                if (!typeInfo->IsClass()) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"func_call : Invalid class type. "
                    //     L"ai[%s] event[%s], pc[%d], sp[%d]",
                    //     handlerNameWide.c_str(),
                    //     eventNameWide.c_str(),
                    //     pc,
                    //     sp)
                    // Log.Add(LOG_ERROR,
                    //     "[AI] Cannot call function %d: type ID %d is not a class. Stopping the handler."
                    //     " Script '%s', event '%s', instruction %d, stack %d.",
                    //     functionIndex, classId, WideToUtf8(handlerNameWide.c_str()).c_str(),
                    //     WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp)
                    L2_LOG_ERROR(
                        Log,
                        "[AI] Cannot call function %d: type ID %d is not a class; stopping the handler."
                        " Script '%s', event '%s', instruction %d, stack %d.",
                        functionIndex, classId, WideToUtf8(handlerNameWide.c_str()).c_str(),
                        WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp
                    );
                    vm.Fail("func_call: invalid class type", 3);
                    break;
                }

                ScriptFuncInfo* const function = typeInfo->IndexToSFNP(functionIndex);
                if (!function) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"func_call : Undefined function. "
                    //     L"ai[%s] event[%s], pc[%d], sp[%d]",
                    //     handlerNameWide.c_str(),
                    //     eventNameWide.c_str(),
                    //     pc,
                    //     sp)
                    // Log.Add(LOG_ERROR,
                    //     "[AI] Function index %d is undefined in class ID %d. Stopping the handler."
                    //     " Script '%s', event '%s', instruction %d, stack %d.",
                    //     functionIndex, classId, WideToUtf8(handlerNameWide.c_str()).c_str(),
                    //     WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp)
                    L2_LOG_ERROR(
                        Log,
                        "[AI] Function index %d is undefined in class ID %d; stopping the handler."
                        " Script '%s', event '%s', instruction %d, stack %d.",
                        functionIndex, classId, WideToUtf8(handlerNameWide.c_str()).c_str(),
                        WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp
                    );
                    vm.Fail("func_call: undefined function", 3);
                    break;
                }
                PublishScriptFunctionName(function->Name());
                if (function->ArgN() != static_cast<std::size_t>(argumentCount)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"func_call : Invalid argument count. "
                    //     L"Func[%s], argcount[%d], sfnp.argN[%d], "
                    //     L"ai[%s] event[%s], pc[%d], sp[%d]",
                    //     function->Name(),
                    //     argumentCount,
                    //     static_cast<int>(
                    //         function->ArgN()),
                    //     handlerNameWide.c_str(),
                    //     eventNameWide.c_str(),
                    //     pc,
                    //     sp)
                    // Log.Add(LOG_ERROR,
                    //     "[AI] Cannot call '%s': supplied %d arguments, expected %zu. Stopping the handler."
                    //     " Script '%s', event '%s', instruction %d, stack %d.",
                    //     WideToUtf8(function->Name()).c_str(), argumentCount, function->ArgN(),
                    //     WideToUtf8(handlerNameWide.c_str()).c_str(), WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp)
                    L2_LOG_ERROR(
                        Log,
                        "[AI] Cannot call '%s': supplied %d arguments, expected %zu; stopping the handler."
                        " Script '%s', event '%s', instruction %d, stack %d.",
                        WideToUtf8(function->Name()).c_str(), argumentCount, function->ArgN(),
                        WideToUtf8(handlerNameWide.c_str()).c_str(), WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp
                    );
                    vm.Fail("func_call: invalid argument count", 3);
                    break;
                }
                if (selfSlot < 0) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] stack underflow nSP[%d] "
                    //     L"ai[%s] event[%s] nCount[%d] nPC[%d]",
                    //     L"ScriptEngine.cpp",
                    //     0x580,
                    //     sp,
                    //     handlerNameWide.c_str(),
                    //     eventNameWide.c_str(),
                    //     retired,
                    //     nextPc)
                    // Log.Add(LOG_ERROR,
                    //     "[AI] Cannot call '%s' with %d arguments: no receiver slot remains. Stopping the handler."
                    //     " Script '%s', event '%s', instruction %d, stack %d.",
                    //     WideToUtf8(function->Name()).c_str(), argumentCount, WideToUtf8(handlerNameWide.c_str()).c_str(),
                    //     WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp)
                    L2_LOG_ERROR(
                        Log,
                        "[AI] Cannot call '%s' with %d arguments: no receiver slot remains; stopping the handler."
                        " Script '%s', event '%s', instruction %d, stack %d.",
                        WideToUtf8(function->Name()).c_str(), argumentCount, WideToUtf8(handlerNameWide.c_str()).c_str(),
                        WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp
                    );
                    // This early-return branch does not assign nLastErrno in the
                    // shipping machine code.
                    vm.StopWithoutScriptError();
                    break;
                }

                ScriptTypeInfo* const floatType = ScriptTypeTable::inst()->GetType(static_cast<int>(ScriptTypeId::Float));
                for (int argument = 0; argument < argumentCount; ++argument) {
                    const int slot = selfSlot + argument + 1;
                    if (function->ArgType(argument) == floatType && types[static_cast<std::size_t>(slot)] == 0) {
                        // The target writes only the low float-sized half of the
                        // eight-byte argument slot and does not change the
                        // parallel stack type byte.
                        vm.StoreFloatBits(slot, static_cast<float>(stack[static_cast<std::size_t>(slot)]));
                    }
                }
                if (stack[static_cast<std::size_t>(selfSlot)] < kAccessMinimum) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"func_call : Access violation. "
                    //     L"Read[%x], ai[%s] event[%s], "
                    //     L"pc[%d], sp[%d]",
                    //     stack[
                    //         static_cast<
                    //             std::size_t>(
                    //                 selfSlot)],
                    //     handlerNameWide.c_str(),
                    //     eventNameWide.c_str(),
                    //     pc,
                    //     sp)
                    // Log.Add(LOG_ERROR,
                    //     "[AI] Call to '%s' rejected receiver address 0x%llx. Stopping the handler."
                    //     " Script '%s', event '%s', instruction %d, stack %d.",
                    //     WideToUtf8(function->Name()).c_str(),
                    //     static_cast<unsigned long long>(stack[static_cast<std::size_t>(selfSlot)]),
                    //     WideToUtf8(handlerNameWide.c_str()).c_str(), WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp)
                    L2_LOG_ERROR(
                        Log,
                        "[AI] Call to '%s' rejected receiver address 0x%llx; stopping the handler."
                        " Script '%s', event '%s', instruction %d, stack %d.",
                        WideToUtf8(function->Name()).c_str(),
                        static_cast<unsigned long long>(stack[static_cast<std::size_t>(selfSlot)]),
                        WideToUtf8(handlerNameWide.c_str()).c_str(), WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp
                    );
                    vm.Fail("func_call: access violation", 3);
                    break;
                }

                if (std::getenv("L2_QUEST_TRACE") != nullptr && (ev.m_nEventId == 0x1e || ev.m_nEventId == 0x1f)) {
                    // std::fprintf(
                    //     stderr,
                    //     "[QUEST_TRACE] L2NPC native pc=%d class=%d "
                    //     "index=%d argc=%d name=%ls self=0x%llx",
                    //     pc,
                    //     classId,
                    //     functionIndex,
                    //     argumentCount,
                    //     function->Name(),
                    //     static_cast<unsigned long long>(
                    //         stack[static_cast<std::size_t>(selfSlot)]))
                    // for (int argument = 0.
                    //      argument < argumentCount.
                    //      ++argument) {
                    //     std::fprintf(
                    //         stderr,
                    //         " arg%d=0x%llx",
                    //         argument,
                    //         static_cast<unsigned long long>(
                    //             stack[static_cast<std::size_t>(
                    //                 selfSlot + argument + 1)]))
                    // }
                    // std::fprintf(stderr, "\n")
                    // std::fflush(stderr)
                    // One record keeps concurrent quest traces together. The common
                    // sink escapes controls and marks any oversized record as truncated.
                    std::ostringstream trace;
                    trace << "[quest trace] Calling " << WideToUtf8(function->Name()) << " at instruction " << pc
                          << " (class " << classId << ", function " << functionIndex << ", " << argumentCount
                          << " arguments, self 0x" << std::hex
                          << static_cast<unsigned long long>(stack[static_cast<std::size_t>(selfSlot)]) << std::dec << ')';
                    for (int argument = 0; argument < argumentCount; ++argument) {
                        trace << "; argument " << argument << " = 0x" << std::hex
                              << static_cast<unsigned long long>(stack[static_cast<std::size_t>(selfSlot + argument + 1)])
                              << std::dec;
                    }
                    trace << '.';
                    // runtime::logging::Write("l2npc", runtime::logging::Level::Debug, trace.str())
                    runtime::logging::Write("l2npc", runtime::logging::Level::Debug, trace.str(), __FILE__);
                }

                TraceNativeCall(
                    *function, stack[static_cast<std::size_t>(selfSlot)], stack.data() + selfSlot + 1, argumentCount, ev,
                    *handler
                );
                const std::int64_t returned = vm.NativeCall(*function, selfSlot);
                ScriptTypeInfo* const returnType = function->RetType();
                if (returnType == floatType) {
                    float returnedFloat = 0.0F;
                    std::memcpy(&returnedFloat, &returned, sizeof(returnedFloat));
                    vm.SetFloat(selfSlot, returnedFloat);
                } else if (returnType == ScriptTypeTable::inst()->GetType(static_cast<int>(ScriptTypeId::Int))) {
                    stack[static_cast<std::size_t>(selfSlot)] = static_cast<std::int32_t>(returned);
                    types[static_cast<std::size_t>(selfSlot)] = 0;
                } else {
                    stack[static_cast<std::size_t>(selfSlot)] = returned;
                    types[static_cast<std::size_t>(selfSlot)] = 0;
                }
                vm.SetObject(selfSlot, returnType);
                PublishScriptFunctionName(L"");
                break;
            }

            case OP_ADD_STRING: {
                if (!vm.Need(1)) {
                    logStackUnderflow(StackUnderflowSourceLine(instruction.optr));
                    break;
                }
                const int left = sp - 1;
                const int leftKind = static_cast<int>((instruction.data.intData >> 8) & 0xff);
                const int rightKind = static_cast<int>(instruction.data.intData & 0xff);
                static constexpr char16_t kEmptyString[] = u"";
                const auto convert = [&](int slot, int kind, bool leftOperand) -> const char16_t* {
                    const auto store = [&](const char16_t* value) {
                        stack[static_cast<std::size_t>(slot)] =
                            static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(value));
                        return value;
                    };
                    if (kind == 2) {
                        return store(AllocIntegerString(stack[static_cast<std::size_t>(slot)]));
                    }
                    if (kind == 3) {
                        return store(AllocFloatString(vm.FloatAt(slot)));
                    }
                    if (kind == 4) {
                        return reinterpret_cast<const char16_t*>(
                            static_cast<std::intptr_t>(stack[static_cast<std::size_t>(slot)])
                        );
                    }
                    if (leftOperand) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%s][%d] oops a=%d "
                        //     L"ai[%s] event[%s]",
                        //     L"ScriptEngine.cpp",
                        //     0x1c7,
                        //     kind,
                        //     handlerNameWide.c_str(),
                        //     eventNameWide.c_str())
                        // Log.Add(LOG_ERROR,
                        //     "[AI] String concatenation has unsupported left operand kind %d. Substituting an empty
                        //     string." " Script '%s', event '%s', instruction %d, stack %d.", kind,
                        //     WideToUtf8(handlerNameWide.c_str()).c_str(), WideToUtf8(eventNameWide.c_str()).c_str(), pc,
                        //     sp)
                        L2_LOG_ERROR(
                            Log,
                            "[AI] String concatenation has unsupported left operand kind %d; substituting an empty string."
                            " Script '%s', event '%s', instruction %d, stack %d.",
                            kind, WideToUtf8(handlerNameWide.c_str()).c_str(), WideToUtf8(eventNameWide.c_str()).c_str(), pc,
                            sp
                        );
                    } else {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%s][%d] oops b=%d",
                        //     L"ScriptEngine.cpp",
                        //     0x1d8,
                        //     kind)
                        // Log.Add(LOG_ERROR,
                        //     "[AI] String concatenation has unsupported right operand kind %d. Substituting an empty
                        //     string." " Script '%s', event '%s', instruction %d, stack %d.", kind,
                        //     WideToUtf8(handlerNameWide.c_str()).c_str(), WideToUtf8(eventNameWide.c_str()).c_str(), pc,
                        //     sp)
                        L2_LOG_ERROR(
                            Log,
                            "[AI] String concatenation has unsupported right operand kind %d; substituting an empty string."
                            " Script '%s', event '%s', instruction %d, stack %d.",
                            kind, WideToUtf8(handlerNameWide.c_str()).c_str(), WideToUtf8(eventNameWide.c_str()).c_str(), pc,
                            sp
                        );
                    }
                    return store(kEmptyString);
                };

                const char16_t* const leftString = convert(left, leftKind, true);
                const char16_t* const rightString = convert(sp, rightKind, false);
                if (reinterpret_cast<std::intptr_t>(leftString) < kAccessMinimum ||
                    reinterpret_cast<std::intptr_t>(rightString) < kAccessMinimum) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"add_string : Access violation. "
                    //     L"Read[%x], ai[%s] event[%s], "
                    //     L"pc[%d], sp[%d]",
                    //     stack[
                    //         static_cast<
                    //             std::size_t>(
                    //                 sp)],
                    //     handlerNameWide.c_str(),
                    //     eventNameWide.c_str(),
                    //     pc,
                    //     sp)
                    // Log.Add(LOG_ERROR,
                    //     "[AI] String concatenation rejected operand addresses 0x%llx and 0x%llx. Stopping the handler."
                    //     " Script '%s', event '%s', instruction %d, stack %d.",
                    //     static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(leftString)),
                    //     static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(rightString)),
                    //     WideToUtf8(handlerNameWide.c_str()).c_str(), WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp)
                    L2_LOG_ERROR(
                        Log,
                        "[AI] String concatenation rejected operand addresses 0x%llx and 0x%llx; stopping the handler."
                        " Script '%s', event '%s', instruction %d, stack %d.",
                        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(leftString)),
                        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(rightString)),
                        WideToUtf8(handlerNameWide.c_str()).c_str(), WideToUtf8(eventNameWide.c_str()).c_str(), pc, sp
                    );
                    vm.Fail("add_string: access violation", 3);
                    break;
                }

                const std::size_t leftLength = std::char_traits<char16_t>::length(leftString);
                const std::size_t rightLength = std::char_traits<char16_t>::length(rightString);
                char16_t* const joined = AllocTmpString(static_cast<int>(leftLength + rightLength));
                std::copy_n(leftString, leftLength, joined);
                std::copy_n(rightString, rightLength, joined + leftLength);
                stack[static_cast<std::size_t>(left)] = static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(joined));
                types[static_cast<std::size_t>(left)] = 0;
                types[static_cast<std::size_t>(sp)] = 0;
                AdjustScriptStackPointer(sp, -1);
                break;
            }

            default:
                break;
        }

        if (result.stopped) {
            return;
        }
        pc = nextPc;
    }

    const std::uint32_t elapsedMilliseconds = runtime::GetTickCount() - lastCalled.m_callTime;
    if (elapsedMilliseconds > 4999) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%s] Executing npc[%s]'s handler "
        //     L"takes too long [%dsec]",
        //     handlerNameWide.c_str(),
        //     eventNameWide.c_str(),
        //     npcNameWide.c_str(),
        //     static_cast<int>(
        //         elapsedMilliseconds /
        //         1000))
        // Log.Add(LOG_WARN,
        //     "[AI] Handler took %u ms for NPC '%s' (warning threshold 5000 ms). Script '%s', event '%s'.",
        //     static_cast<unsigned int>(elapsedMilliseconds), WideToUtf8(npcNameWide.c_str()).c_str(),
        //     WideToUtf8(handlerNameWide.c_str()).c_str(), WideToUtf8(eventNameWide.c_str()).c_str())
        // L2_LOG_WARN(Log,
        //     "[AI] Handler took %u ms for NPC '%s' (warning threshold 5000 ms). Script '%s', event '%s'.",
        //     static_cast<unsigned int>(elapsedMilliseconds), WideToUtf8(npcNameWide.c_str()).c_str(),
        //     WideToUtf8(handlerNameWide.c_str()).c_str(), WideToUtf8(eventNameWide.c_str()).c_str())
        L2_LOG_WARN(
            Log, "[AI] Handler took %u ms for event owner '%s' (warning threshold 5000 ms). Script '%s', event '%s'.",
            static_cast<unsigned int>(elapsedMilliseconds), WideToUtf8(npcNameWide.c_str()).c_str(),
            WideToUtf8(handlerNameWide.c_str()).c_str(), WideToUtf8(eventNameWide.c_str()).c_str()
        );
    }
    // The target tests the retired count at the common epilogue, so a handler
    // that reaches its natural end on instruction 30,000 is reported too.
    if (retired >= kInstructionLimit) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Max Loop Count exceed!!! "
        //     L"aiName=%s, eventName=%s, "
        //     L"nRecursiveCount=%d",
        //     L"ScriptEngine.cpp",
        //     0x6c1,
        //     handlerNameWide.c_str(),
        //     eventNameWide.c_str(),
        //     recursiveDepthBefore)
        // Log.Add(LOG_ERROR,
        //     "[AI] Reached the %d-instruction limit after executing %d instructions (recursion depth %d). Script '%s',
        //     event '%s'.", kInstructionLimit, retired, recursiveDepthBefore, WideToUtf8(handlerNameWide.c_str()).c_str(),
        //     WideToUtf8(eventNameWide.c_str()).c_str())
        // L2_LOG_ERROR(Log,
        //     "[AI] Reached the %d-instruction limit after executing %d instructions (recursion depth %d). Script '%s',
        //     event '%s'.", kInstructionLimit, retired, recursiveDepthBefore, WideToUtf8(handlerNameWide.c_str()).c_str(),
        //     WideToUtf8(eventNameWide.c_str()).c_str())
        L2_LOG_ERROR(
            Log,
            "[AI] Reached the %d-instruction limit after executing %d instructions (recursion depth %d). Script '%s', event "
            "'%s', owner '%s'.",
            kInstructionLimit, retired, recursiveDepthBefore, WideToUtf8(handlerNameWide.c_str()).c_str(),
            WideToUtf8(eventNameWide.c_str()).c_str(), WideToUtf8(npcNameWide.c_str()).c_str()
        );
        CurrentRunScriptCallStatus().nLastErrno = 2;
    }
}

} // namespace

int GetCurrentNPCEventID() {
    // Current L2NPC.exe 0x0050f098. Matching PDB source lines 259-265.
    L2_GUARD_FUNCTION();
    const int threadId = ScriptThreadId();
    const int recursiveCount = g_RunScriptCallStatus[threadId].nRecursiveCount;
    if (recursiveCount < 1) {
        return -1;
    }
    return g_ScriptContext[threadId][static_cast<std::size_t>(recursiveCount - 1)].nEventID;
}

int CScriptEngine::TLGetCurrentQuestID() { return g_currentQuestId; }

void CScriptEngine::TLSetCurrentQuestID(int questId) { g_currentQuestId = questId; }

void CScriptEngine::AddAtomicItemProcess(
    int creatureIndex, int itemClassId, std::int64_t count, int enchanted, int blessed
) {
    // Current L2NPC.exe 0x00510778.
    L2_GUARD_FUNCTION();
    const ItemManip manipulation(0, itemClassId, count, enchanted, blessed, 0, true);
    Singleton<ThreadLocalStorage<AtomicJobCompositor, 8>>::inst()->AddItemManip(creatureIndex, manipulation, true);
}

void CScriptEngine::AddAtomicJob(int compositorKey, AtomicJob* job) {
    // Exact current signature at 0x0051088c. AtomicJobCompositor takes
    // ownership and destroys the object after processing or cancellation.
    L2_GUARD_FUNCTION();
    Singleton<ThreadLocalStorage<AtomicJobCompositor, 8>>::inst()->AddJob(compositorKey, job);
}

int CScriptEngine::RunNPCScript(
    ScriptEvent* event, CNPCEventHandler* handler, CScriptBinCode* code, CAIParameter* parameter, CAIProperty* property
) {
    L2_GUARD_FUNCTION();
    int nullMask = 0;
    if (!event) {
        nullMask |= 1;
    }
    if (!handler) {
        nullMask |= 2;
    }
    if (!code) {
        nullMask |= 4;
    }
    if (nullMask != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"RunNPCScript Null Error, "
        //     L"NullPoint = [%d]",
        //     nullMask)
        // Log.Add(LOG_ERROR,
        //     "[AI] Cannot run the handler: event %s, handler %s, bytecode %s.",
        //     event ? "present" : "missing", handler ? "present" : "missing", code ? "present" : "missing")
        // L2_LOG_ERROR(Log,
        //     "[AI] Cannot run the handler: event %s, handler %s, bytecode %s.",
        //     event ? "present" : "missing", handler ? "present" : "missing", code ? "present" : "missing")
        L2_LOG_ERROR(
            Log,
            "[AI] Cannot run the handler: event %s, handler %s, bytecode %s (event ID %d; -1 when unavailable). Script "
            "execution was not started.",
            event ? "present" : "missing", handler ? "present" : "missing", code ? "present" : "missing",
            event ? event->m_nEventId : -1
        );
        return -1;
    }

    if (CurrentRunScriptCallStatus().nRecursiveCount >= kRecursionLimit) {
        const std::string aiName = Utf16ToUtf8(std::u16string_view(handler->GetAIClassName()));
        const char16_t* const eventNameValue = event->GetEventName(event->m_nEventId);
        const std::wstring eventName = eventNameValue ? Utf16ToWide(std::u16string_view(eventNameValue)) : std::wstring{};
        // Log.Add(
        //     LOG_ERROR,
        //     L"Script stack overflow!! [%s][%s]",
        //     aiName.c_str(),
        //     eventName.c_str())
        // Log.Add(LOG_ERROR,
        //     "[AI] Cannot enter script '%s', event '%s': recursion depth %d reached the limit of %d.",
        //     WideToUtf8(aiName).c_str(), WideToUtf8(eventName).c_str(),
        //     CurrentRunScriptCallStatus().nRecursiveCount, kRecursionLimit)
        L2_LOG_ERROR(
            Log, "[AI] Cannot enter script '%s', event '%s': recursion depth %d reached the limit of %d.",
            aiName.c_str(), WideToUtf8(eventName).c_str(), CurrentRunScriptCallStatus().nRecursiveCount,
            kRecursionLimit
        );
        return -1;
    }

    auto callSuper = [&]() {
        if (!handler) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] pHandler is null.",
            //     L"ScriptEngine.cpp",
            //     0x655)
            // Log.Add(LOG_ERROR,
            //     "[AI] Cannot call the parent event handler: the current handler is null.")
            // Noise: RunNPCScript rejects a null handler before creating this
            // callback, and Execute receives the pointer by value. Keep the
            // original guard, without another diagnostic for the same input.
            // L2_LOG_ERROR(Log,
            //     "[AI] Cannot call the parent event handler: the current handler is null.")
            return;
        }
        if (handler->m_pSuperEventHandler) {
            handler->m_pSuperEventHandler->HandleEvent(event, parameter, property);
        }
    };

    RunResult result;
    Execute(&code->m_codes, *code, *event, handler, parameter, property, callSuper, result);
    return 0;
}
