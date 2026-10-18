#pragma once

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/LegacyCrtRandom.h"
#include "Shared/src/Runtime/TargetWideChar.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/UtilsString.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iosfwd>
#include <iterator>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

// Shared/Utils.cpp uses Win64 long at this source boundary. Keep that exact
// native type on Windows and replace its fixed target width elsewhere.
using SharedUtilsLong = std::conditional_t<sizeof(long) == sizeof(std::int32_t), long, std::int32_t>;


struct _SYSTEMTIME;
enum RaceType : std::int32_t;
class CIOObject;

// Shared/Utils.h owner recovered from current L2Server 0x00c3c53c and the
// symbol-complete VS2015 Shared PDB. The destructor schedules
// the timer, so callers can place it before work that must precede requeue.
class GuardAddTimer {
public:
    GuardAddTimer(CIOObject* object, int time, int id);
    ~GuardAddTimer();

private:
    CIOObject* m_pObject; // +0x00
    int m_nTime; // +0x08
    int m_nID; // +0x0c
};


// The target keeps both generators in thread-local CRT/TLS storage. These
// names and real-number intervals follow Shared/Utils.cpp from the donor PDB.
void InitializeRandomNumber();
void init_genrand64(std::uint64_t seed);
std::uint64_t genrand64_int64();
double genrand64_real1();
double genrand64_real2();
double genrand64_real3();

// Current L2Server 0x00415990 creates Random<target long> with this
// exact index calculation. The source template has no empty-
// vector guard. Callers must satisfy the same precondition as the target.
template <typename T>
T Random(const std::vector<T, std::allocator<T>>& values) {
    const int maximumIndex = static_cast<int>(values.size()) - 1;
    const int upper = maximumIndex < 0 ? 0 : maximumIndex;
    const int lower = maximumIndex < 0 ? maximumIndex : 0;
    const int index =
        static_cast<int>(genrand64_real2() * static_cast<double>(upper - lower + 1) + static_cast<double>(lower));
    return values[static_cast<std::size_t>(index)];
}

// Shared/Utils.cpp global helper used by LocalizationSystem. The low-level
// function keeps the target's fixed 0x400-wide-character destination
// contract. The third argument is a dead legacy parameter in the shipping
// executable. Selection uses the process-global g_nCountryCode. Its storage
// belongs to each product's config translation unit, not Shared/Utils.cpp.
extern int g_nCountryCode;
extern bool g_bScriptSecureEnabled;
// False sends PopUpWindow text to stderr instead of a blocking message box
// (for unattended runs such as L2Server --check-scripts).
extern bool g_bPopUpWindowsEnabled;
void PopUpWindowV(const wchar_t* format, std::va_list arguments);
void PopUpWindow(const wchar_t* format, ...);
void ChangeFileNameForLocalization(const wchar_t* source, wchar_t* destination, int country);

// Shared/Utils.cpp source owners recovered from the PDB. ReadIntMem advances
// over one target little-endian int. IsInvalidString is true for null/empty.
void* ReadIntMem(void* memory, int& value);
bool IsInvalidString(wchar_t* value);

// Current L2Server.exe 0x00c3cc74, original Shared/Utils.cpp owner. The
// shipping helper copies at most count characters into one 10000-character
// thread-local buffer and always returns that buffer.
wchar_t* SafeString(wchar_t* source, int count);

// Shared/Utils.cpp, lines 151-211. LoadBinaryAbsoluteW reports the number of
// Windows UTF-16 code units even on hosts where wchar_t is 32-bit. The caller
// owns the malloc allocation. CNpcMakerDB keeps its npcpos
// image for process lifetime, exactly as the shipping executable does.
char* LoadBinaryAbsolute(const wchar_t* fileName, int& byteCount, bool requireUnicode = false);
char* LoadBinaryAbsoluteSecure(const wchar_t* fileName, int& byteCount, bool secure);
std::uint16_t* LoadBinaryAbsoluteW(const wchar_t* fileName, int& codeUnitCount);

// shared/Utils.h: CSecureStream. The current 0x238-byte object is stack-local
// to category loading. Its two MSVC stream subobjects cannot be instantiated
// with the same ABI on libc++/libstdc++, so their exact 0x110/0x108 storage
// regions carry only the host stream pointer/open bit while all source-visible
// fields keep their target offsets.
class CSecureStream {
public:
    CSecureStream(const wchar_t* fileName, bool createRawString);
    virtual ~CSecureStream();

    char16_t* CreateRawString(const wchar_t* fileName, bool create);
    wchar_t* CreateValidName(const wchar_t* fileName);
    bool fail() { return m_inTargetImage[0] == std::byte{0}; }

    std::wistream* GetStream() { return *reinterpret_cast<std::wistringstream**>(m_secureInTargetImage.data()); }

    std::wistringstream& GetStringStream() {
        return **reinterpret_cast<std::wistringstream**>(m_secureInTargetImage.data());
    }

    wchar_t* GetFileName() { return m_validFileName; }

    char16_t* GetRawString() const { return m_pszRawString; }

    int GetRawStringLen() const { return m_RawStringLen; }

private:
    wchar_t* m_validFileName = nullptr; // +0x08
    char16_t* m_pszRawString = nullptr; // +0x10
    int m_RawStringLen = 0; // +0x18

    alignas(void*) std::array<std::byte, 0x110> m_inTargetImage{}; // +0x20
    alignas(void*) std::array<std::byte, 0x108> m_secureInTargetImage{}; // +0x130
};


// Current L2NPC.exe 0x00545590. Windows wchar_t is a UTF-16 code unit in the
// target, hence char16_t here. Both count arguments are kept for ABI/source
// fidelity but the shipping body ignores them and converts the complete
// NUL-terminated string through CP_ACP.
int UnicodeToAnsi(char16_t* source, int sourceCount, char* destination, int destinationCount);

// The two overloads linked into the current image at 0x00545b6c and
// 0x00545c28. WritePrivateProfileStringW is supplied by the portable
// configuration binding outside Windows.
int SetPrivateProfile(const wchar_t* section, const wchar_t* key, wchar_t* value, const wchar_t* fileName);
int SetPrivateProfile(const wchar_t* section, const wchar_t* key, int value, const wchar_t* fileName);

// Shared/Utils.cpp, current L2NPC.exe 0x00545244, 0x005452ac,
// 0x0054650c and 0x005466b8. These helpers read the target's raw
// little-endian UTF-16 PCH streams instead of host wchar_t text streams.
char16_t GetChar(std::FILE* stream);
bool IsDigit(char16_t value);
int GetInt(std::FILE* stream);
std::u16string GetString(std::FILE* stream);

// Shared/Utils.cpp. Current L2NPC.exe 0x00546414 and current L2Server.exe
// 0x00c3de80. This is the target's xallocator-backed UTF-16 decimal string
// helper, not std::to_wstring.
std::u16string IntToXStr(int value);

// Shared/Utils.h:974-997. The current L2Server instantiation at 0x0076549c
// keeps the historical floor(length / FixedSize) + 1 rule, including one
// empty trailing chunk when the input length is an exact multiple.
template <std::size_t FixedSize>
void SplitStringToMapFixedSize(std::map<std::int32_t, std::u16string>& output, const char16_t* source) {
    if (source == nullptr) {
        return;
    }

    const std::size_t length = std::char_traits<char16_t>::length(source);
    const int chunkCount = static_cast<int>(length / FixedSize) + 1;
    for (int index = 0; index < chunkCount; ++index) {
        std::array<char16_t, FixedSize + 1> chunk{};
        const std::size_t offset = static_cast<std::size_t>(index) * FixedSize;
        const std::size_t remaining = offset < length ? length - offset : 0;
        const std::size_t copied = std::min(FixedSize, remaining);
        if (copied != 0) {
            std::char_traits<char16_t>::copy(chunk.data(), source + offset, copied);
        }
        output.emplace(index, std::u16string(chunk.data()));
    }
}

// Shared/Utils.cpp, current L2Server 0x00ad88f8. Values are appended in the
// ordered map's key order without validating gaps or rewriting contents.
std::u16string JoinStringFromMap(std::map<std::int32_t, std::u16string>& input);

// Shared/Utils.cpp global helper. Current Cached.exe 0x005c99e8 and the
// symbol-complete VS2015 Shared donor both use the by-value xallocator string
// contract and replace every non-overlapping occurrence from left to right.
std::u16string ReplaceStr(std::u16string source, const char16_t* search, const char16_t* replacement);

char16_t* GetLeftString(char16_t* source, char16_t** token);

namespace L2Util {

// Shared/Utils.cpp. Current L2Server.exe 0x00c3e36c. The target formats into
// 1023 units of a 1024-unit stack buffer, so longer text is cut to 1023 units.
// The format follows the original printf rules, see Runtime/Utf16Format.h.
template <class... Args>
std::u16string StringFormat(std::u16string_view format, const Args&... arguments) {
    std::u16string text = FormatUtf16(format, arguments...);
    if (text.size() > 0x3ff) {
        text.resize(0x3ff);
    }
    return text;
}

// Shared/Utils.cpp. Current L2Server.exe 0x00c3c394.
void ReportByteArrayToHexStr(const wchar_t* label, const char* begin, const char* end);

int GetTokenizedStrings(char16_t* source, std::vector<std::u16string>& output);

namespace Time {

// The symbol-complete NASC64_2015 Shared donor supplies the original overload
// names. Current Cached.exe 0x005c8770/0x005c8784 fix their signed time32
// quotient bodies. 0x005c9090 fixes the guarded no-argument owner.
int GetElapsedMin(SharedUtilsLong* value);
int GetElapsedHour(SharedUtilsLong* value);
int GetElapsedHour();

} // namespace Time

} // namespace L2Util

// Shared/Utils.cpp lines 895-952 in the named VS2015 donor. Current
// L2NPC.exe 0x00545d14.
int GetDayDiff(_SYSTEMTIME* current, _SYSTEMTIME* previous);

// Inline helper used by CDesirePQ in the target. Despite its name, Random(n)
// returns a value in 1..n, plus n+1 in the extremely rare real1()==1 case.
int Random(int maximum);

// shared/Utils.h:123-150. The PDB records this as a one-byte structure with
// five public static methods. The bodies are inline in the original header.
struct FloatChecker {
    static bool IsNan(double value) { return std::isnan(value); }

    static bool IsInfinite(double value) {
        // MSVC _finite returns zero for infinities and NaNs. Callers test
        // IsNan first, matching the source's short-circuit order.
        return !std::isfinite(value);
    }

    static int FPClass(double value) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        const bool negative = (bits >> 63U) != 0;
        const std::uint64_t exponent = (bits >> 52U) & 0x7ffU;
        const std::uint64_t fraction = bits & UINT64_C(0x000fffffffffffff);

        if (exponent == 0x7ffU) {
            if (fraction == 0) {
                return negative ? 0x0004 : 0x0200;
            }
            return (fraction & UINT64_C(0x0008000000000000)) != 0 ? 0x0002 : 0x0001;
        }
        if (exponent == 0) {
            if (fraction == 0) {
                return negative ? 0x0020 : 0x0040;
            }
            return negative ? 0x0010 : 0x0080;
        }
        return negative ? 0x0008 : 0x0100;
    }

    static bool IsValid(double value) { return !IsNan(value) && !IsInfinite(value); }

    static bool CheckValidation(double value) {
        const bool nan = IsNan(value);
        if (!nan && !IsInfinite(value)) {
            return true;
        }

        std::uint64_t rawValue = 0;
        std::memcpy(&rawValue, &value, sizeof(rawValue));
        // Log.Add(
        //     LOG_ERROR,
        //     nan
        //         ? L"[FP] value is NAN(input:%.5f(%p) fpclass:%04x)"
        //         : L"[FP] value is INFINITE(input:%.5f(%p) fpclass:%04x)",
        //     value,
        //     reinterpret_cast<void*>(
        //         static_cast<std::uintptr_t>(
        //             rawValue)),
        //     FPClass(value))
        // Log.Add(
        //     LOG_ERROR,
        //     "[numeric] %s failed finite-value validation: %.17g (bits 0x%016llx, class 0x%04x).",
        //     nan ? "NaN" : "Infinity", value,
        //     static_cast<unsigned long long>(rawValue), static_cast<unsigned>(FPClass(value)))
        L2_LOG_ERROR(
            Log, "[numeric] %s failed finite-value validation: %.17g (bits 0x%016llx, class 0x%04x).",
            nan ? "NaN" : "Infinity", value, static_cast<unsigned long long>(rawValue), static_cast<unsigned>(FPClass(value))
        );
        return false;
    }
};


// shared/Utils.h, lines 257-307 in the matching VS2015 build. The target
// instantiation CBitStorage<2048,int,64> stores exactly 64 32-bit words.
template <int MaximumFlagCount, typename TBitUnit, std::size_t BitUnitCount>
class CBitStorage {
public:
    CBitStorage() { std::memset(m_BitFlag, 0, sizeof(m_BitFlag)); }

    bool GetFlag(int flagId) const {
        if (flagId >= MaximumFlagCount) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Bit flag Index[%d] overflow",
            //     L"c:\\work\\lineage2build\\server\\main\\shared\\Utils.h",
            //     0x106,
            //     flagId)
            // Log.Add(
            //     LOG_ERROR,
            //     "[flags] Cannot read flag %d: the index is outside a storage capacity of %d flags. Returning false.",
            //     flagId, MaximumFlagCount)
            L2_LOG_ERROR(
                Log, "[flags] Cannot read flag %d: the index is outside a storage capacity of %d flags. Returning false.",
                flagId, MaximumFlagCount
            );
            return false;
        }
        if (flagId < 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Bit flag index[%d] underflow",
            //     L"c:\\work\\lineage2build\\server\\main\\shared\\Utils.h",
            //     0x10c,
            //     flagId)
            // Log.Add(
            //     LOG_ERROR,
            //     "[flags] Cannot read flag %d: the index is negative. Returning false.",
            //     flagId)
            L2_LOG_ERROR(Log, "[flags] Cannot read flag %d: the index is negative. Returning false.", flagId);
            return false;
        }
        const auto word = static_cast<std::uint32_t>(m_BitFlag[IdToIndex(flagId)]);
        return ((word >> IdToOffset(flagId)) & 1U) != 0;
    }

    void SetFlag(int flagId, bool enabled) {
        if (flagId >= MaximumFlagCount) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Bit flag Index overflow",
            //     L"c:\\work\\lineage2build\\server\\main\\shared\\Utils.h",
            //     0x11f)
            // Log.Add(
            //     LOG_ERROR,
            //     "[flags] Cannot %s flag %d: the index is outside a storage capacity of %d flags. The flag is unchanged.",
            //     enabled ? "enable" : "disable", flagId, MaximumFlagCount)
            L2_LOG_ERROR(
                Log,
                "[flags] Cannot %s flag %d: the index is outside a storage capacity of %d flags. The flag is unchanged.",
                enabled ? "enable" : "disable", flagId, MaximumFlagCount
            );
            return;
        }
        if (flagId < 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Bit flag index underflow",
            //     L"c:\\work\\lineage2build\\server\\main\\shared\\Utils.h",
            //     0x125)
            // Log.Add(
            //     LOG_ERROR,
            //     "[flags] Cannot %s flag %d: the index is negative. The flag is unchanged.",
            //     enabled ? "enable" : "disable", flagId)
            L2_LOG_ERROR(
                Log, "[flags] Cannot %s flag %d: the index is negative. The flag is unchanged.",
                enabled ? "enable" : "disable", flagId
            );
            return;
        }
        const std::uint32_t mask = 1U << IdToOffset(flagId);
        auto& word = m_BitFlag[IdToIndex(flagId)];
        if (enabled) {
            word = static_cast<TBitUnit>(static_cast<std::uint32_t>(word) | mask);
        } else {
            word = static_cast<TBitUnit>(static_cast<std::uint32_t>(word) & ~mask);
        }
    }

    void SetFlagAll(char* source) {
        if (!source) {
            return;
        }
        std::memcpy(m_BitFlag, source, sizeof(m_BitFlag));
    }

    void GetFlagAll(char* destination, std::uint64_t destinationSize) const {
        if (!destination || destinationSize < sizeof(m_BitFlag)) {
            return;
        }
        std::memcpy(destination, m_BitFlag, sizeof(m_BitFlag));
    }

    bool operator[](int flagId) const { return GetFlag(flagId); }

    bool IsAllClear() const {
        return std::all_of(std::begin(m_BitFlag), std::end(m_BitFlag), [](TBitUnit value) { return value == 0; });
    }

private:
    static constexpr int IdToIndex(int flagId) { return flagId / static_cast<int>(sizeof(TBitUnit) * 8); }

    static constexpr int IdToOffset(int flagId) { return flagId % static_cast<int>(sizeof(TBitUnit) * 8); }

    TBitUnit m_BitFlag[BitUnitCount];
};


// Shared/Utils.cpp. The matching NASC64_2015 PDB supplies the original name.
// current CacheD.exe 0x005c86d0 confirms the same fixed-count loop.
char* ReplaceChar(char* buffer, std::size_t length, char source, char replacement);

// Current L2Server.exe Shared/Utils.cpp bodies at 0x00c3c978,
// 0x00c3cda0, 0x00c3d014, 0x00c3d570, 0x00c3d668, 0x00c3df78,
// 0x00c3e140 and 0x00c3e75c. TotalChecker2015 supplies the matching
// Shared/Utils.cpp declarations only. Current machine code is authoritative.
int AnsiToUnicode(const char* source, int sourceCount, SharedUtilsWideChar* destination, int destinationCount);
void ParseHTMLParam(const SharedUtilsWideChar* source, char* format, ...);
const SharedUtilsWideChar* ParseHTMLParam2(const SharedUtilsWideChar* source, char* format, ...);
char16_t* GetLeftString2(char16_t* source, char16_t** token);
int GetYYYYMMDDFromTime(SharedUtilsLong time);
int GetInitialClassType(int classType, RaceType raceType);
int GetIntFromString(SharedUtilsWideChar* source);
std::int64_t GetInt64FromString(SharedUtilsWideChar* source);
std::u16string Trim(SharedUtilsWideChar* source);

// Current L2Server.exe 0x00c3bf54/0x00c3e864. ToLowerCase operates on
// target UTF-16 code units (ASCII and the contiguous Cyrillic upper-case
// range only). TrimLR removes U+0020 from the two ends of its by-value
// xallocator string.
int ToLowerCase(const SharedUtilsWideChar* source, SharedUtilsWideChar* destination);
// Same Latin/Cyrillic code-unit mapping as ToLowerCase. No Unicode or locale fold.
std::u16string LowercaseGameName(std::u16string_view source);
std::u16string TrimLR(std::u16string source);

// Current L2Server.exe 0x00c3eb74. Retain the original source spelling.
std::u16string TrimDoubleQuotaionMarks(std::u16string source);

// Shared/Utils.h, current L2Server.exe 0x00791e60. Delimiters are individual
// UTF-16 code units. Adjacent/trailing delimiters keep empty tokens.
inline std::vector<std::u16string> split(const std::u16string& source, const std::u16string& delimiters) {
    std::vector<std::u16string> result;
    const auto value = runtime::StringView(source);
    std::size_t begin = 0;
    for (;;) {
        const auto end = value.find_first_of(runtime::StringView(delimiters), begin);
        if (end == std::u16string_view::npos) {
            result.push_back(std::u16string(value.substr(begin)));
            return result;
        }
        result.push_back(std::u16string(value.substr(begin, end - begin)));
        begin = end + 1;
    }
}

#if defined(_WIN32)
// Shared/Utils.cpp, current Cached.exe 0x005c8890/0x005c8a4c. These are
// genuine Win32 registry owners, not portable configuration helpers. Keep
// them global, as recorded by the current guards and the symbol-complete
// VS2015 Shared donor, and expose them only on the platform whose registry
// contract they implement.
bool LoadStrFromReg(wchar_t* output, const wchar_t* valueName, const wchar_t* subKey);
void SaveStrToReg(wchar_t* value, const wchar_t* valueName, const wchar_t* subKey);
#endif

// Shared/Utils.{h,cpp}. Current L2Server 0x00c3c324/0x00c3c358 proves the
// folded constructor/destructor shared with ServerAutoStarter, and
// 0x00c3daf8 proves the complete batch-launch body. The Win64 process
// structures stay opaque so the source owner keeps its 0x80-byte extent
// on every host.
class BatchAutoStarter {
public:
    struct Config {
        Config();

        bool m_enabled;
        std::u16string m_filePath;
        std::u16string m_workingFolder;
    };

    BatchAutoStarter();
    ~BatchAutoStarter();

    void Do();

    static Config config;

private:
    alignas(void*) std::array<std::byte, 0x68> startupInfo{};
    alignas(void*) std::array<std::byte, 0x18> info{};
};


// shared/Utils.{h,cpp}, source lines 1794-1823 in the matching VS2015
// Shared donor. The damaged CacheD PDB keeps the class,
// nested Config, method and static-data names in Utils.obj. Current
// Cached.exe 0x005c87e4/0x005c8818/0x005c92e4 confirms the complete object
// layout and all three method bodies. The Win64 structures remain opaque so
// this header has the same 0x80-byte class extent on every host without
// exporting Windows headers to the other server projects.
class ServerAutoStarter {
public:
    struct Config {
        Config();

        bool m_enabled;
        std::u16string m_filePath;
        std::u16string m_workingFolder;
    };

    ServerAutoStarter();
    ~ServerAutoStarter();

    void Do();

    static Config config;

private:
    alignas(void*) std::array<std::byte, 0x68> startupInfo{};
    alignas(void*) std::array<std::byte, 0x18> info{};
};


// shared/Utils.{h,cpp}. Win64 long is four bytes. Retain that spelling on
// Windows and only adapt its width on LP64 hosts.
using JobTransactionLong = std::conditional_t<sizeof(long) == sizeof(std::int32_t), long, std::int32_t>;


// Exact three-dword transaction gate used by the current L2Server pledge
// children. The current executable proves all four method
// bodies and offsets. TotalChecker and L2NPC are corroborating Shared-source
// donors only.
class JobTransactionLock {
public:
    JobTransactionLock();
    explicit JobTransactionLock(int timeout);

    bool BeginJob(JobTransactionLong* jobType);
    bool EndJob();

private:
    volatile std::int32_t m_nJobType;
    std::int32_t m_nTimeout;
    volatile JobTransactionLong m_tBeginJobTime;
};


// Shared/Utils.h transaction-lifetime owner used when an asynchronous
// request keeps a JobTransactionLock beyond the initiating call. Current
// L2Server 0x00906734 proves the complete inline state machine and layout.
// the TotalChecker2015 PDB supplies the original physical name and access.
class GuardBeginJob {
public:
    GuardBeginJob(JobTransactionLock& lock, JobTransactionLong& jobType, const wchar_t* file, int line) :
        m_pLock(&lock), m_bWaitForEndJob(false), m_pszFile(file), m_nLine(line), m_bLocked(lock.BeginJob(&jobType)) {}

    ~GuardBeginJob() {
        if (m_bLocked && !m_bWaitForEndJob && !m_pLock->EndJob()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Failed to end job at %s(%d)",
            //     m_pszFile,
            //     m_nLine)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[job] Guard cleanup failed to end the transaction begun at \"%ls\", line %d.",
            //     m_pszFile ? m_pszFile : L"unknown file", m_nLine)
            L2_LOG_ERROR(
                Log, u"[job] Guard cleanup failed to end the transaction begun at \"%ls\", line %d.",
                m_pszFile ? m_pszFile : L"unknown file", m_nLine
            );
        }
    }

    bool IsLocked() const { return m_bLocked; }

    void WaitForEndJob() { m_bWaitForEndJob = true; }

private:
    JobTransactionLock* m_pLock;
    bool m_bWaitForEndJob;
    const wchar_t* m_pszFile;
    int m_nLine;
    bool m_bLocked;
};
