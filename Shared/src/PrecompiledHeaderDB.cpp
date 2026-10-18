#include "Shared/src/PrecompiledHeaderDB.h"
#include "Shared/src/Runtime/SourceFile.h"
#include "Shared/src/Runtime/ResourceDirectories.h"
#include "Shared/src/Runtime/Utf8.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/CategoryDataDB.h"
#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/LogStorage.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"
#include "TextRuntime.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <limits>
#include <new>
#include <source_location>
#include <string>
#include <string_view>

namespace {

#if defined(L2_BUILD_CACHED)
constexpr std::array<const wchar_t*, 3> kNameFileOrder{{
    L"npc_pch.txt",
    L"manual_pch.txt",
    L"category_pch.txt",
}};
#else
constexpr std::array<const wchar_t*, 11> kNameFileOrder{{
    L"item_pch.txt",
    L"npc_pch.txt",
    L"skill_pch.txt",
    L"quest_pch.txt",
    L"manual_pch.txt",
    L"category_pch.txt",
    L"fishing_pch.txt",
    L"option_pch.txt",
    L"userquestdata_pch.txt",
    L"ensouldata_pch.txt",
    L"predefinedlist_pch.txt",
}};

#if defined(L2_BUILD_L2NPC)
constexpr std::array<const wchar_t*, 2> kCategoryPch2Files{{
    L"item_pch2.txt",
    L"skill_pch3.txt",
}};
#endif
#endif

bool IsIntegerStart(char16_t value) { return (value >= u'0' && value <= u'9') || value == u'-'; }

bool IsInt64Continuation(char16_t value) {
    return IsIntegerStart(value) || value == u'x' || (value >= u'a' && value <= u'f');
}

std::string NarrowTargetNumber(std::u16string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char16_t codeUnit : text) {
        if (codeUnit > 0x7f) {
            return {};
        }
        result.push_back(static_cast<char>(codeUnit));
    }
    return result;
}

// The matching PDB does not expose this private symbol's source name. The
// current L2NPC contains this complete body at 0x00509ac0. Current L2Server
// contains the matching body at 0x009e7c98 and calls it only from
// GetInt64Ex's hexadecimal branch.
std::int64_t ParseWideSignedInt64(const char16_t* text, const char16_t** end, int radix) {
    const char16_t* cursor = text;
    char16_t current = u'\0';
    do {
        current = *cursor++;
    } while (std::iswspace(static_cast<std::wint_t>(current)) != 0);

    const bool negative = current == u'-';
    if (negative || current == u'+') {
        current = *cursor++;
    }

    if ((radix == 0 || radix == 16) && current == u'0' && (*cursor == u'x' || *cursor == u'X')) {
        current = cursor[1];
        cursor += 2;
        radix = 16;
    }
    if (radix == 0) {
        radix = current == u'0' ? 8 : 10;
    }

    const std::uint64_t base = static_cast<std::uint64_t>(radix);
    const std::uint64_t limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + (negative ? 1u : 0u);
    std::uint64_t magnitude = 0;
    int conversion = 0;

    for (;;) {
        unsigned int digit = 0;
        if (current >= u'0' && current <= u'9') {
            digit = static_cast<unsigned int>(current - u'0');
        } else if (current >= u'a' && current <= u'z') {
            digit = static_cast<unsigned int>(current - u'a') + 10u;
        } else if (current >= u'A' && current <= u'Z') {
            digit = static_cast<unsigned int>(current - u'A') + 10u;
        } else {
            break;
        }

        if (radix <= static_cast<int>(digit)) {
            break;
        }
        if (conversion < 0 || magnitude > limit / base || (magnitude == limit / base && digit > limit % base)) {
            conversion = -1;
        } else {
            conversion = 1;
            magnitude = magnitude * base + digit;
        }
        current = *cursor++;
    }

    if (end) {
        *end = conversion == 0 ? text : cursor - 1;
    }
    if (conversion < 0) {
        return runtime::Int64FromBits(limit);
    }

    std::uint64_t result = magnitude;
    if (negative) {
        result = 0u - result;
    }
    return runtime::Int64FromBits(result);
}

std::int64_t ParseCrtDecimalInt64(std::u16string_view text) {
    const std::string ascii = NarrowTargetNumber(text);
    if (ascii.empty()) {
        return 0;
    }

    char* end = nullptr;
    const long long parsed = std::strtoll(ascii.c_str(), &end, 10);
    if (end == ascii.c_str()) {
        return 0;
    }
    static_assert(sizeof(parsed) == sizeof(std::int64_t));
    return static_cast<std::int64_t>(parsed);
}

int TruncateToTargetInt32(std::int64_t value) {
    const std::uint32_t bits = static_cast<std::uint32_t>(static_cast<std::uint64_t>(value));
    std::int32_t result = 0;
    static_assert(sizeof(result) == sizeof(bits));
    std::memcpy(&result, &bits, sizeof(result));
    return static_cast<int>(result);
}

int ParseCrtHexInt32(std::u16string_view text) {
    const std::string ascii = NarrowTargetNumber(text);
    if (ascii.empty()) {
        return 0;
    }

    char* end = nullptr;
    const long long parsed = std::strtoll(ascii.c_str(), &end, 16);
    if (end == ascii.c_str()) {
        return 0;
    }
    if (parsed > static_cast<long long>(std::numeric_limits<std::int32_t>::max())) {
        return std::numeric_limits<std::int32_t>::max();
    }
    if (parsed < static_cast<long long>(std::numeric_limits<std::int32_t>::min())) {
        return std::numeric_limits<std::int32_t>::min();
    }
    return static_cast<int>(parsed);
}

std::filesystem::path MakeHostPath(const wchar_t* directory, const wchar_t* filename) {
    std::string path = WideToUtf8(directory ? std::wstring_view(directory) : std::wstring_view());
    path += WideToUtf8(filename ? std::wstring_view(filename) : std::wstring_view());
#if !defined(_WIN32)
    std::replace(path.begin(), path.end(), '\\', '/');
#endif
    return runtime::ResolveResourcePath(path);
}

// Original file-opening implementation, kept for comparison.
/*
std::FILE* OpenBinaryInput(
    const std::filesystem::path& path) {
#if defined(_WIN32)
    std::FILE* stream = nullptr,
    ::_wfopen_s(
        &stream,
        path.c_str(),
        L"rb"),
    return stream,
#else
    return std::fopen(
        path.c_str(),
        "rb"),
#endif
}
*/
std::FILE* OpenBinaryInput(const std::filesystem::path& path) {
#if defined(_WIN32)
    std::FILE* stream = nullptr;
    const int openError = ::_wfopen_s(&stream, path.c_str(), L"rb");
#else
    std::FILE* const stream = std::fopen(path.c_str(), "rb");
    const int openError = errno;
#endif
    if (!stream) {
        const int savedErrno = errno;
        // Log.Add(LOG_ERROR,
        //     "[scripts] Cannot open precompiled header \"%s\" (CRT error %d: %s). Continuing without this file.",
        //     path.u8string().c_str(), openError, std::strerror(openError))
        L2_LOG_ERROR(
            Log, "[scripts] Cannot open precompiled header \"%s\" (CRT error %d: %s); continuing without this file.",
            runtime::Utf8Bytes(path.u8string()).c_str(), openError, std::strerror(openError)
        );
        errno = savedErrno;
    }
    return stream;
}

void CloseBinaryInput(std::FILE* input) {
    // Snapshot before fclose: even a failed close releases the FILE object.
    const auto path = runtime::logging::SnapshotLogFilePath(input);
    const int result = std::fclose(input);
    if (result != 0) {
        const runtime::logging::StorageErrorState preserveErrors;
        // Log.Add(LOG_ERROR,
        //     "[scripts] Cannot close precompiled input \"%s\" (close result %d. Errno %d: %s).",
        //     path[0] ? path.data() : "<path unavailable>", result, preserveErrors.savedErrno,
        //     preserveErrors.savedErrno ? std::strerror(preserveErrors.savedErrno) : "not reported")
        L2_LOG_ERROR(
            Log, "[scripts] Cannot close precompiled input \"%s\" (close result %d; errno %d: %s).",
            path[0] ? path.data() : "<path unavailable>", result, preserveErrors.savedErrno,
            preserveErrors.savedErrno ? std::strerror(preserveErrors.savedErrno) : "not reported"
        );
    }
}

void ReportMissingFile(std::FILE* reportFile, const wchar_t* filename) {
    constexpr const wchar_t* kReportFormat = L"Can't find precompiled file[%s]\r\n";
    if (reportFile) {
#if defined(_WIN32)
        const wchar_t* const hostFormat = kReportFormat;
#else
        const std::wstring translated = WindowsWideFormatToHost(kReportFormat);
        const wchar_t* const hostFormat = translated.c_str();
#endif
        // std::fwprintf(
        //     reportFile,
        //     hostFormat,
        //     filename)
        const int written = std::fwprintf(reportFile, hostFormat, filename);
        if (written < 0) {
            const runtime::logging::StorageErrorState preserveErrors;
            const auto path = runtime::logging::SnapshotLogFilePath(reportFile);
            // Log.Add(LOG_ERROR,
            //     "[scripts] Cannot write the missing-header report for \"%s\" to \"%s\" (write result %d. Observed errno
            //     %d).", WideToUtf8(filename ? filename : L"(unknown)").c_str(), path[0] ? path.data() : "<path
            //     unavailable>", written, preserveErrors.savedErrno)
            L2_LOG_ERROR(
                Log,
                "[scripts] Cannot write the missing-header report for \"%s\" to \"%s\" (write result %d; observed errno "
                "%d).",
                WideToUtf8(filename ? filename : L"(unknown)").c_str(), path[0] ? path.data() : "<path unavailable>",
                written, preserveErrors.savedErrno
            );
        }
    }

#ifdef L2_BUILD_L2SERVER
    // Current L2Server Load@009e8338 reports every missing name file to the
    // optional stream and to CLog. The peer products keep
    // their separately observed report-stream-only route.
    // Noise: OpenBinaryInput already reports the resolved path and open error in every product. Retain the separate
    // report-file record above. Log.Add(
    //     LOG_ERROR,
    //     L"Can't find precompiled file[%s]",
    //     filename)
#endif
}

#if defined(L2_BUILD_L2NPC)
// Portable diagnostic state only: one complete recent failure per OS thread.
// Copy names by value, including all active frames, so different callers or
// AI contexts cannot share a truncated/hash-only key. Larger contexts remain
// unthrottled. No database fields, gameplay timers or exit-time logging.
struct MissingSkillLogKey {
    const CPrecompiledHeaderDB* database = nullptr;
    std::int64_t skillNameId = 0;
    int worker = 0;
    int callerLine = 0;
    std::array<char16_t, 512> callerFile{};
    std::size_t depth = 0;
    std::array<std::array<int, 4>, 16> frames{};
    std::array<wchar_t, 2048> names{};

    bool operator==(const MissingSkillLogKey& other) const noexcept {
        return database == other.database && skillNameId == other.skillNameId && worker == other.worker &&
            callerLine == other.callerLine && callerFile == other.callerFile && depth == other.depth &&
            frames == other.frames && names == other.names;
    }
};

struct MissingSkillReports {
    bool active = false;
    MissingSkillLogKey key;
    unsigned long long repeats = 0;
    std::chrono::steady_clock::time_point lastReport;
};
thread_local MissingSkillReports missingSkillReports;

bool CaptureMissingSkillLogKey(
    MissingSkillLogKey& key,
    const CPrecompiledHeaderDB* database,
    std::int64_t skillNameId,
    const char16_t* callerFile,
    int callerLine
) noexcept {
    key.worker = CThreadManager::GetThreadID();
    if (!callerFile || !*callerFile || key.worker < 0 ||
        static_cast<std::size_t>(key.worker) >= std::size(g_ScriptContext)) {
        return false;
    }
    std::size_t fileLength = 0;
    while (fileLength < key.callerFile.size() && callerFile[fileLength]) {
        ++fileLength;
    }
    if (fileLength == key.callerFile.size()) {
        return false;
    }
    std::copy_n(callerFile, fileLength, key.callerFile.data());
    key.database = database;
    key.skillNameId = skillNameId;
    key.callerLine = callerLine;
    std::size_t nameLength = 0;
    const auto appendName = [&](const wchar_t* name) {
        if (!name) {
            name = L"";
        }
        for (;;) {
            if (nameLength == key.names.size()) {
                return false;
            }
            const wchar_t value = *name++;
            key.names[nameLength++] = value;
            if (!value) {
                return true;
            }
        }
    };
    for (const auto& frame : g_ScriptContext[key.worker]) {
        if (!frame.bSet) {
            return true;
        }
        if (key.depth == key.frames.size()) {
            return false;
        }
        key.frames[key.depth++] = {frame.nEventID, frame.nPC, frame.nSP, static_cast<unsigned char>(frame.optr)};
        if (!appendName(frame.pszHandlerName) || !appendName(frame.pszEventName) || !appendName(frame.pszFuncName)) {
            return false;
        }
    }
    return true;
}

void SummarizeMissingSkillLookups() {
    auto& reports = missingSkillReports;
    if (reports.repeats == 0) {
        return;
    }
    const auto repeats = reports.repeats;
    reports.repeats = 0;
    // Log.Add(LOG_ERROR,
    //     L"[skills] %ls%llu additional lookup failure%ls for skill %lld on worker %d since the last report, with the same
    //     AI context. Requested by \"%ls\" at line %d.", repeats == std::numeric_limits<unsigned long long>::max() ? L"At
    //     least " : L"", repeats, repeats == 1 ? L"" : L"s", static_cast<long long>(reports.key.skillNameId),
    //     reports.key.worker, Utf16ToWide(reports.key.callerFile.data()).c_str(), reports.key.callerLine)
    L2_LOG_ERROR(
        Log,
        u"[skills] %ls%llu additional lookup failure%ls for skill %lld on worker %d since the last report, with the same AI "
        u"context. Requested by \"%ls\" at line %d.",
        repeats == std::numeric_limits<unsigned long long>::max() ? L"At least " : L"", repeats, repeats == 1 ? L"" : L"s",
        static_cast<long long>(reports.key.skillNameId), reports.key.worker, reports.key.callerFile.data(),
        reports.key.callerLine
    );
}

bool ShouldReportMissingSkill(
    const CPrecompiledHeaderDB* database, std::int64_t skillNameId, const char16_t* callerFile, int callerLine
) {
    const runtime::logging::StorageErrorState preserveErrors;
    auto& reports = missingSkillReports;
    MissingSkillLogKey key;
    if (!CaptureMissingSkillLogKey(key, database, skillNameId, callerFile, callerLine)) {
        SummarizeMissingSkillLookups();
        reports.active = false;
        return true;
    }
    const auto now = std::chrono::steady_clock::now();
    if (reports.active && reports.key == key) {
        if (reports.repeats < std::numeric_limits<unsigned long long>::max()) {
            ++reports.repeats;
        }
        if (now - reports.lastReport >= std::chrono::seconds(30)) {
            reports.lastReport = now;
            SummarizeMissingSkillLookups();
        }
        return false;
    }
    SummarizeMissingSkillLookups();
    reports.key = key;
    reports.active = true;
    reports.lastReport = now;
    return true;
}

void ReportSkillLookupSuccess(
    const CPrecompiledHeaderDB* database, std::int64_t skillNameId, const char16_t* callerFile, int callerLine
) {
    const runtime::logging::StorageErrorState preserveErrors;
    auto& reports = missingSkillReports;
    // An unrelated successful lookup must not re-enable the noisy failure.
    if (!reports.active || reports.key.database != database || reports.key.skillNameId != skillNameId ||
        reports.key.callerLine != callerLine || reports.key.worker != CThreadManager::GetThreadID() || !callerFile ||
        std::u16string_view(reports.key.callerFile.data()) != std::u16string_view(callerFile)) {
        return;
    }
    SummarizeMissingSkillLookups();
    reports.active = false;
    // Log.Add(LOG_NORMAL,
    //     L"[skills] Lookup now finds precompiled data for skill %lld on worker %d. Requested by \"%ls\" at line %d.",
    //     static_cast<long long>(skillNameId), reports.key.worker,
    //     Utf16ToWide(reports.key.callerFile.data()).c_str(), callerLine)
    L2_LOG_INFO(
        Log, u"[skills] Lookup now finds precompiled data for skill %lld on worker %d. Requested by \"%ls\" at line %d.",
        static_cast<long long>(skillNameId), reports.key.worker, reports.key.callerFile.data(), callerLine
    );
}

int HighSigned32(std::int64_t value) {
    const std::uint32_t bits = static_cast<std::uint32_t>(static_cast<std::uint64_t>(value) >> 32);
    int result = 0;
    static_assert(sizeof(result) == sizeof(bits));
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

template <typename Member>
int SkillInteger(
    CPrecompiledHeaderDB& database,
    std::int64_t skillNameId,
    bool& found,
    Member member,
    int missingValue = 0,
    const std::source_location location = std::source_location::current()
) {
    found = true;
    CSkillInfoForL2Npc* const skill = database.FindSkillPdb(skillNameId, location);
    if (!skill) {
        found = false;
        return missingValue;
    }
    return skill->*member;
}
#endif

} // namespace

int GetIntEx(std::FILE* stream) {
    L2_GUARD_FUNCTION();

    std::u16string number;
    char16_t value = u'\0';
    do {
        value = GetChar(stream);
        if (IsDigit(value)) {
            break;
        }
    } while (std::feof(stream) == 0);

    do {
        number.append(std::u16string_view(&value, 1));
        if (std::feof(stream) != 0) {
            break;
        }
        value = GetChar(stream);
    } while (IsInt64Continuation(value));

    const std::u16string_view text = runtime::StringView(number);
    if (text.size() > 2 && text[0] == u'0' && (text[1] == u'x' || text[1] == u'X')) {
        return ParseCrtHexInt32(text.substr(2));
    }
    return TruncateToTargetInt32(ParseCrtDecimalInt64(text));
}

std::int64_t GetInt64Ex(std::FILE* stream) {
    L2_GUARD_FUNCTION();

    std::u16string number;
    char16_t value = u'\0';
    do {
        value = GetChar(stream);
        if (IsDigit(value)) {
            break;
        }
    } while (std::feof(stream) == 0);

    do {
        number.append(std::u16string_view(&value, 1));
        if (std::feof(stream) != 0) {
            break;
        }
        value = GetChar(stream);
    } while (IsInt64Continuation(value));

    const std::u16string_view text = runtime::StringView(number);
    if (text.size() > 2 && text[0] == u'0' && (text[1] == u'x' || text[1] == u'X')) {
        return ParseWideSignedInt64(text.data() + 2, nullptr, 16);
    }
    return ParseCrtDecimalInt64(text);
}

CPrecompiledHeaderDB g_PrecompiledHeaderDB;

CPrecompiledHeaderDB::CPrecompiledHeaderDB() = default;

CPrecompiledHeaderDB::~CPrecompiledHeaderDB() = default;

std::int64_t CPrecompiledHeaderDB::Search(const char16_t* name) const {
    L2_GUARD_FUNCTION();

    const auto found = m_NameMap.find(std::u16string(name));
    return found == m_NameMap.end() ? -1 : found->second;
}

#if defined(L2_BUILD_L2SERVER)
void CPrecompiledHeaderDB::Set(const char16_t* name, std::int64_t value) {
    L2_GUARD_FUNCTION();
    m_NameMap[std::u16string(name)] = value;
}
#endif

#if defined(L2_BUILD_L2NPC)
CSkillInfoForL2Npc* CPrecompiledHeaderDB::FindSkillPdb(const std::int64_t skillNameId, const std::source_location location) {
    L2_GUARD_FUNCTION();
    // The original passed __FILE__ and __LINE__ of the caller. Use the real
    // caller location in the same UTF-16 form.
    const std::string_view callerFileName = ::source_location::FileName(location.file_name());
    const std::u16string callerFileText(callerFileName.begin(), callerFileName.end());
    const char16_t* const callerFile = callerFileText.c_str();
    const int callerLine = static_cast<int>(location.line());

    if (skillNameId == 0) {
        return nullptr;
    }

    const auto found = m_SkillInfoDB.find(skillNameId);
    if (found != m_SkillInfoDB.end()) {
        if (found->second) {
            ReportSkillLookupSuccess(this, skillNameId, callerFile, callerLine);
        }
        return found->second;
    }

    const std::uint64_t bits = static_cast<std::uint64_t>(skillNameId);
    const std::wstring file = callerFile ? Utf16ToWide(callerFile) : std::wstring();
    const wchar_t* const fileForLog = callerFile ? file.c_str() : nullptr;
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] invalid skill_name_id[%I64d] "
    //     L"id[%d] level[%d] sublevel[%d]",
    //     fileForLog,
    //     callerLine,
    //     static_cast<long long>(skillNameId),
    //     HighSigned32(skillNameId),
    //     static_cast<int>(
    //         (bits >> 16) & 0xffffU),
    //     static_cast<int>(
    //         bits & 0xffffU))
    // Noise: Identical failures on this thread repeat the entire same AI
    // context. Keep the first report and summarize repeats. Any changed or
    // incompletely captured context is reported in full immediately.
    // Log.Add(
    //     LOG_ERROR,
    //     L"[skills] No precompiled data for skill %lld (ID %d, level %d, sublevel %d). Lookup failed. Requested by \"%ls\"
    //     at line %d.", static_cast<long long>(skillNameId), HighSigned32(skillNameId), static_cast<int>((bits >> 16) &
    //     0xffffU), static_cast<int>(bits & 0xffffU), fileForLog ? fileForLog : L"(unknown)", callerLine)
    // ReportScriptContext(LOG_ERROR)
    if (ShouldReportMissingSkill(this, skillNameId, callerFile, callerLine)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[skills] No precompiled data for skill %lld (ID %d, level %d, sublevel %d). Lookup failed. Requested by
        //     \"%ls\" at line %d.", static_cast<long long>(skillNameId), HighSigned32(skillNameId), static_cast<int>((bits
        //     >> 16) & 0xffffU), static_cast<int>(bits & 0xffffU), fileForLog ? fileForLog : L"(unknown)", callerLine)
        L2_LOG_ERROR(
            Log,
            u"[skills] No precompiled data for skill %lld (ID %d, level %d, sublevel %d); lookup failed. Requested by "
            u"\"%ls\" at line %d.",
            static_cast<long long>(skillNameId), HighSigned32(skillNameId), static_cast<int>((bits >> 16) & 0xffffU),
            static_cast<int>(bits & 0xffffU), fileForLog ? fileForLog : L"(unknown)", callerLine
        );
        ReportScriptContext(LOG_ERROR);
    }
    return nullptr;
}

void CPrecompiledHeaderDB::InsertPdb(CSkillInfoForL2Npc* skill) {
    L2_GUARD_FUNCTION();

    // std::map::insert preserves the first pointer. The target intentionally
    // has no owner cleanup on the duplicate or in its destructor.
    m_SkillInfoDB.emplace(skill->m_skill_name_id, skill);
}

int CPrecompiledHeaderDB::Skill_GetCastRange(std::int64_t skillNameId, bool& found) {
    L2_GUARD_FUNCTION();
    return SkillInteger(*this, skillNameId, found, &CSkillInfoForL2Npc::m_cast_range);
}

int CPrecompiledHeaderDB::Skill_GetConsumeHP(std::int64_t skillNameId, bool& found) {
    L2_GUARD_FUNCTION();
    return SkillInteger(*this, skillNameId, found, &CSkillInfoForL2Npc::m_hp);
}

int CPrecompiledHeaderDB::Skill_GetConsumeMP(std::int64_t skillNameId, bool& found) {
    L2_GUARD_FUNCTION();
    return SkillInteger(*this, skillNameId, found, &CSkillInfoForL2Npc::m_mp);
}

int CPrecompiledHeaderDB::Skill_GetTargetType(std::int64_t skillNameId, bool& found) {
    L2_GUARD_FUNCTION();
    return SkillInteger(*this, skillNameId, found, &CSkillInfoForL2Npc::m_target_type, 0x16);
}

int CPrecompiledHeaderDB::Skill_GetEffectPoint(std::int64_t skillNameId, bool& found) {
    L2_GUARD_FUNCTION();
    return SkillInteger(*this, skillNameId, found, &CSkillInfoForL2Npc::m_effect_point);
}

bool CPrecompiledHeaderDB::Skill_HaveAttribute(std::int64_t skillNameId, int attribute, bool& found) {
    L2_GUARD_FUNCTION();

    found = true;
    CSkillInfoForL2Npc* const skill = FindSkillPdb(skillNameId);
    if (!skill) {
        found = false;
        return false;
    }
    return skill->m_attribute == attribute;
}

int CPrecompiledHeaderDB::Skill_GetAbnormalType(std::int64_t skillNameId, bool& found) {
    L2_GUARD_FUNCTION();
    return SkillInteger(*this, skillNameId, found, &CSkillInfoForL2Npc::m_abnormal_type);
}

int CPrecompiledHeaderDB::Skill_GetAbnormalLevel(std::int64_t skillNameId, bool& found) {
    L2_GUARD_FUNCTION();
    return SkillInteger(*this, skillNameId, found, &CSkillInfoForL2Npc::m_abnormal_level);
}

int CPrecompiledHeaderDB::Skill_GetReuseDelay(std::int64_t skillNameId, bool& found) {
    L2_GUARD_FUNCTION();
    return SkillInteger(*this, skillNameId, found, &CSkillInfoForL2Npc::m_reuse_delay);
}

int CPrecompiledHeaderDB::Skill_GetHitCoolTime(std::int64_t skillNameId, bool& found) {
    L2_GUARD_FUNCTION();
    return SkillInteger(*this, skillNameId, found, &CSkillInfoForL2Npc::m_hit_cool_time);
}

int CPrecompiledHeaderDB::Skill_GetIsMagic(std::int64_t skillNameId, bool& found) {
    L2_GUARD_FUNCTION();
    return SkillInteger(*this, skillNameId, found, &CSkillInfoForL2Npc::m_is_magic);
}

int CPrecompiledHeaderDB::Skill_GetIsHate(std::int64_t skillNameId, bool& found) {
    L2_GUARD_FUNCTION();
    return SkillInteger(*this, skillNameId, found, &CSkillInfoForL2Npc::m_is_hate);
}
#endif

void CPrecompiledHeaderDB::Load(const wchar_t* directory, std::FILE* reportFile) {
    L2_GUARD_FUNCTION();

    for (const wchar_t* const filename : kNameFileOrder) {
        std::FILE* const input = OpenBinaryInput(MakeHostPath(directory, filename));
        if (!input) {
            ReportMissingFile(reportFile, filename);
            continue;
        }

        // Current Load consumes the BOM with one raw GetChar call.
        GetChar(input);
        while (std::feof(input) == 0) {
            const std::u16string name = GetString(input);
            if (std::feof(input) != 0 || name.empty()) {
                break;
            }
            const std::int64_t value = GetInt64Ex(input);
            // Target operator[] overwrites an earlier equal PCH name.
            m_NameMap[std::u16string(runtime::StringView(name))] = value;
        }
        // std::fclose(input)
        CloseBinaryInput(input);
    }

#if defined(L2_BUILD_L2NPC)
    {
        constexpr const wchar_t* kSkillFile = L"skill_pch2.txt";
        std::FILE* const input = OpenBinaryInput(MakeHostPath(directory, kSkillFile));
        if (!input) {
            // Noise: OpenBinaryInput already reports the resolved skill_pch2.txt path and open error.
            // Log.Add(
            //     LOG_ERROR,
            //     L"Can't load %s",
            //     kSkillFile)
        } else {
            GetChar(input);
            while (std::feof(input) == 0) {
                const std::int64_t skillNameId = GetInt64Ex(input);
                if (std::feof(input) != 0) {
                    break;
                }

                std::array<int, 13> fields{};
                for (int& field : fields) {
                    field = GetInt(input);
                }
                if (fields[12] != -12345) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"SkillDataChecker and NPC server "
                    //     L"version is different.")
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[skills] Incompatible skill_pch2.txt record for skill %lld: expected marker -12345, got %d.
                    //     remaining skill records skipped.", static_cast<long long>(skillNameId), fields[12])
                    L2_LOG_ERROR(
                        Log,
                        u"[skills] Incompatible skill_pch2.txt record for skill %lld: expected marker -12345, got %d; "
                        u"remaining skill records skipped.",
                        static_cast<long long>(skillNameId), fields[12]
                    );
                    break;
                }
                if (skillNameId == 0) {
                    continue;
                }

                auto* const skill = new (std::nothrow) CSkillInfoForL2Npc;
                if (skill) {
                    skill->m_skill_name_id = skillNameId;
                    skill->m_cast_range = fields[0];
                    skill->m_hp = fields[1];
                    skill->m_mp = fields[2];
                    skill->m_target_type = fields[3];
                    skill->m_effect_point = fields[4];
                    skill->m_abnormal_type = fields[5];
                    skill->m_abnormal_level = fields[6];
                    skill->m_hit_cool_time = fields[7];
                    skill->m_reuse_delay = fields[8];
                    skill->m_is_magic = fields[9];
                    skill->m_is_hate = fields[10];
                    skill->m_attribute = fields[11];
                }
                // The shipping body still calls InsertPdb after a null
                // allocation. InsertPdb then faults on the unchecked key
                // read. Preserve that OOM boundary instead of throwing.
                InsertPdb(skill);
            }
            // std::fclose(input)
            CloseBinaryInput(input);
        }
    }

    for (const wchar_t* const filename : kCategoryPch2Files) {
        std::FILE* const input = OpenBinaryInput(MakeHostPath(directory, filename));
        if (!input) {
            ReportMissingFile(reportFile, filename);
            continue;
        }

        GetChar(input);
        while (std::feof(input) == 0) {
            const std::u16string ignoredName = GetString(input);
            if (std::feof(input) != 0 || ignoredName.empty()) {
                break;
            }
            const int categoryId = GetInt(input);
            const int objectId = GetInt(input);
            CCategoryDataDB::Inst().AddObjectInCategory(categoryId, objectId);
        }
        // std::fclose(input)
        CloseBinaryInput(input);
    }
#endif
}
