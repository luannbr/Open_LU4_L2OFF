#include "Shared/src/FHtml.h"
#include "Shared/src/MemorySub.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/TextRuntime.h"

#if defined(L2_BUILD_L2SERVER)
#include "HTMLCache.h"

#include <new>
// #  include "Shared/log.h"
#endif

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>


namespace {

std::u16string_view TargetUtf16View(const char16_t* value) {
    // The current CFHTML methods construct their xwstring key/file-name
    // directly. The target calls wcslen before touching a map, so a null
    // pointer is not normalized to an empty portable string.
    return std::u16string_view(value, std::char_traits<char16_t>::length(value));
}

template <std::size_t Size>
void CopyUtf16Text(char16_t (&destination)[Size], std::u16string_view source) noexcept {
    const std::size_t length = source.size() < Size ? source.size() : Size - 1;
    for (std::size_t index = 0; index < length; ++index) {
        destination[index] = source[index];
    }
    destination[length] = u'\0';
}

std::u16string_view Msvc2010SpecialFloat(float value) noexcept {
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    const std::uint32_t exponent = bits & 0x7f800000U;
    if (exponent != 0x7f800000U) {
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

template <std::size_t Size>
void CopyNumericText(char16_t (&destination)[Size], const std::array<char, Size>& source, int length) noexcept {
    if (length < 0 || static_cast<std::size_t>(length) >= Size) {
        destination[0] = u'\0';
        return;
    }

    for (int index = 0; index < length; ++index) {
        destination[index] = static_cast<unsigned char>(source[static_cast<std::size_t>(index)]);
    }
    destination[length] = u'\0';
}

template <std::size_t Size>
void FormatDecimal(char16_t (&destination)[Size], int value) noexcept {
    std::array<char, Size> source{};
    const int length = std::snprintf(source.data(), source.size(), "%d", value);
    CopyNumericText(destination, source, length);
}

template <std::size_t Size>
void FormatDecimal(char16_t (&destination)[Size], std::int64_t value) noexcept {
    std::array<char, Size> source{};
    const int length = std::snprintf(source.data(), source.size(), "%lld", static_cast<long long>(value));
    CopyNumericText(destination, source, length);
}

template <std::size_t Size>
void FormatFloat(char16_t (&destination)[Size], float value) {
    // The image statically links the VS2010 CRT. Its %f spellings for
    // IEEE-754 special values differ from modern libc spellings.
    const std::u16string_view special = Msvc2010SpecialFloat(value);
    if (!special.empty()) {
        CopyUtf16Text(destination, special);
        return;
    }

    std::array<char, Size> source{};
    const int length = std::snprintf(source.data(), source.size(), "%f", static_cast<double>(value));
    CopyNumericText(destination, source, length);
}

} // namespace

CFHTML::CFHTML() { L2_GUARD_FUNCTION(); }

CFHTML::~CFHTML() { L2_GUARD_FUNCTION(); }

// Shared/fhtml.h:17 in the matching PDB is the class-specific
// OBJECT_TYPE macro expansion.
L2_DEFINE_OBJECT_ADD_REF(CFHTML)

L2_DEFINE_OBJECT_RELEASE(CFHTML)

void CFHTML::SetHTMLFileName(const char16_t* pszFileName) {
    // Current L2NPC.exe 0x0042eb94.
    L2_GUARD_FUNCTION();
    m_sHTMLFileName = std::u16string(TargetUtf16View(pszFileName));
#if defined(L2_BUILD_L2NPC)
    if (std::getenv("L2_QUEST_TRACE") != nullptr) {
        // std::fprintf(
        //     stderr,
        //     "[QUEST_TRACE] CFHTML::SetHTMLFileName this=%p "
        //     "source_units=%zu stored_units=%zu first=0x%04x\n",
        //     static_cast<void*>(this),
        //     TargetUtf16View(pszFileName).size(),
        //     m_sHTMLFileName.size(),
        //     m_sHTMLFileName.empty()
        //         ? 0U
        //         : static_cast<unsigned int>(
        //               m_sHTMLFileName[0]))
        // std::fflush(stderr)
        // Log.Add(LOG_NORMAL,
        //     "[HTML trace] Set filename \"%s\" on object %p (%zu UTF-16 units).",
        //     Utf16ToUtf8(m_sHTMLFileName.view()).c_str(),
        //     static_cast<void*>(this), m_sHTMLFileName.size())
        L2_LOG_INFO(
            Log, "[HTML trace] Set filename \"%s\" on object %p (%zu UTF-16 units).",
            Utf16ToUtf8(runtime::StringView(m_sHTMLFileName)).c_str(), static_cast<void*>(this), m_sHTMLFileName.size()
        );
    }
#endif
}

void CFHTML::Set(const char16_t* pszVarName, int nIntData) {
    // Current L2NPC.exe 0x0042fe28.
    L2_GUARD_FUNCTION();
    m_mapIntData[std::u16string(TargetUtf16View(pszVarName))] = nIntData;
}

void CFHTML::Set(const char16_t* pszVarName, std::int64_t nInt64Data) {
    // Current L2NPC.exe 0x0042ff38.
    L2_GUARD_FUNCTION();
    m_mapInt64Data[std::u16string(TargetUtf16View(pszVarName))] = nInt64Data;
#if defined(L2_BUILD_L2NPC)
    if (std::getenv("L2_QUEST_TRACE") != nullptr) {
        // std::fprintf(
        //     stderr,
        //     "[QUEST_TRACE] CFHTML::SetInt64 this=%p key_units=%zu "
        //     "value=%lld count=%zu\n",
        //     static_cast<void*>(this),
        //     TargetUtf16View(pszVarName).size(),
        //     static_cast<long long>(nInt64Data),
        //     m_mapInt64Data.size())
        // std::fflush(stderr)
        // Log.Add(LOG_NORMAL,
        //     "[HTML trace] Set integer parameter \"%s\" to %lld on object %p (%zu integer parameters).",
        //     Utf16ToUtf8(TargetUtf16View(pszVarName)).c_str(),
        //     static_cast<long long>(nInt64Data), static_cast<void*>(this), m_mapInt64Data.size())
        L2_LOG_INFO(
            Log, "[HTML trace] Set integer parameter \"%s\" to %lld on object %p (%zu integer parameters).",
            Utf16ToUtf8(TargetUtf16View(pszVarName)).c_str(), static_cast<long long>(nInt64Data), static_cast<void*>(this),
            m_mapInt64Data.size()
        );
    }
#endif
}

void CFHTML::Set(const char16_t* pszVarName, float dFloatData) {
    // Current L2NPC.exe 0x00430048.
    L2_GUARD_FUNCTION();
    m_mapFloatData[std::u16string(TargetUtf16View(pszVarName))] = dFloatData;
}

void CFHTML::Set(const char16_t* pszVarName, const char16_t* pszStrData) {
    // Current L2NPC.exe 0x00430158 stores L"*" for a null value.
    L2_GUARD_FUNCTION();
    m_mapStrData[std::u16string(TargetUtf16View(pszVarName))] = std::u16string(pszStrData ? pszStrData : u"*");
}

char* CFHTML::MakeStream(char* pBuffer, int nBufferSize) {
    // Current L2NPC.exe 0x0042e818 serializes the four maps in this exact
    // order. Its 0x0042e72c helper is _vsnwprintf_s with literal L"%f".
    L2_GUARD_FUNCTION();

#if defined(L2_BUILD_L2NPC)
    if (std::getenv("L2_QUEST_TRACE") != nullptr) {
        // std::fprintf(
        //     stderr,
        //     "[QUEST_TRACE] CFHTML::MakeStream this=%p file_units=%zu "
        //     "int=%zu int64=%zu float=%zu str=%zu first=0x%04x\n",
        //     static_cast<void*>(this),
        //     m_sHTMLFileName.size(),
        //     m_mapIntData.size(),
        //     m_mapInt64Data.size(),
        //     m_mapFloatData.size(),
        //     m_mapStrData.size(),
        //     m_sHTMLFileName.empty()
        //         ? 0U
        //         : static_cast<unsigned int>(
        //               m_sHTMLFileName[0]))
        // std::fflush(stderr)
        // Log.Add(LOG_NORMAL,
        //     "[HTML trace] Serializing \"%s\" from object %p: %zu int, %zu int64, %zu float, %zu string parameters.",
        //     Utf16ToUtf8(m_sHTMLFileName.view()).c_str(), static_cast<void*>(this),
        //     m_mapIntData.size(), m_mapInt64Data.size(), m_mapFloatData.size(), m_mapStrData.size())
        L2_LOG_INFO(
            Log, "[HTML trace] Serializing \"%s\" from object %p: %zu int, %zu int64, %zu float, %zu string parameters.",
            Utf16ToUtf8(runtime::StringView(m_sHTMLFileName)).c_str(), static_cast<void*>(this), m_mapIntData.size(),
            m_mapInt64Data.size(), m_mapFloatData.size(), m_mapStrData.size()
        );
    }
#endif

    char16_t pszBuf[1000]{};
    int nSize = 0;

    nSize += ::Assemble(pBuffer, nBufferSize, "S", m_sHTMLFileName.c_str());

    const int nTotalCount =
        static_cast<int>(m_mapIntData.size() + m_mapInt64Data.size() + m_mapFloatData.size() + m_mapStrData.size());
    nSize += ::Assemble(pBuffer + nSize, nBufferSize - nSize, "d", nTotalCount);

    for (auto it_int = m_mapIntData.begin(); it_int != m_mapIntData.end(); ++it_int) {
        FormatDecimal(pszBuf, it_int->second);
        nSize += ::Assemble(pBuffer + nSize, nBufferSize - nSize, "SS", it_int->first.c_str(), pszBuf);
    }
    for (auto it_int64 = m_mapInt64Data.begin(); it_int64 != m_mapInt64Data.end(); ++it_int64) {
        FormatDecimal(pszBuf, it_int64->second);
        nSize += ::Assemble(pBuffer + nSize, nBufferSize - nSize, "SS", it_int64->first.c_str(), pszBuf);
    }
    for (auto it_float = m_mapFloatData.begin(); it_float != m_mapFloatData.end(); ++it_float) {
        FormatFloat(pszBuf, it_float->second);
        nSize += ::Assemble(pBuffer + nSize, nBufferSize - nSize, "SS", it_float->first.c_str(), pszBuf);
    }
    for (StrStrMapItrType it_str = m_mapStrData.begin(); it_str != m_mapStrData.end(); ++it_str) {
        nSize += ::Assemble(pBuffer + nSize, nBufferSize - nSize, "SS", it_str->first.c_str(), it_str->second.c_str());
    }

    return pBuffer + nSize;
}

ScriptAccessibleEx* CFHTML::GetScriptAccessibleEx() const {
    // The compiler-generated secondary-base thunk adjusts this by +0x18.
    return const_cast<CFHTML*>(this);
}

#if defined(L2_BUILD_L2SERVER)
std::u16string CFHTML::MakeHTMLFromStream(const unsigned char*& packet, const std::uint8_t countryCode) {
    // Current L2Server.exe 0x0072a9e8.
    L2_GUARD_FUNCTION();

    StrStrMapType parameters;
    std::array<char16_t, 264> fileName;
    std::array<char16_t, 1000> key;
    std::array<char16_t, 20480> value;
    int count;

    packet = ::Disassemble(packet, "Sd", 0x208, fileName.data(), &count);
    if (std::getenv("L2_QUEST_TRACE") != nullptr) {
        // std::fprintf(
        //     stderr,
        //     "[QUEST_TRACE] CFHTML::MakeHTMLFromStream "
        //     "file_units=%zu count=%d first=0x%04x country=%u\n",
        //     TargetUtf16View(fileName.data()).size(),
        //     count,
        //     static_cast<unsigned int>(fileName[0]),
        //     static_cast<unsigned int>(countryCode))
        // std::fflush(stderr)
        // Log.Add(LOG_NORMAL,
        //     "[HTML trace] Rendering \"%s\" for country %u with %d parameters from the stream.",
        //     Utf16ToUtf8(TargetUtf16View(fileName.data())).c_str(),
        //     static_cast<unsigned int>(countryCode), count)
        L2_LOG_INFO(
            Log, "[HTML trace] Rendering \"%s\" for country %u with %d parameters from the stream.",
            Utf16ToUtf8(TargetUtf16View(fileName.data())).c_str(), static_cast<unsigned int>(countryCode), count
        );
    }
    while (count != 0) {
        count = static_cast<int>(static_cast<std::uint32_t>(count) - 1U);
        packet = ::Disassemble(packet, "SS", 2000, key.data(), 0xa000, value.data());
        parameters[std::u16string(TargetUtf16View(key.data()))] = std::u16string(TargetUtf16View(value.data()));
    }

    const std::u16string name(TargetUtf16View(fileName.data()));
    return MakeHTMLFromStrMap(name.c_str(), parameters, countryCode);
}

std::u16string CFHTML::MakeHTMLFromStrMap(
    const char16_t* const fileName, const StrStrMapType& parameters, const std::uint8_t countryCode
) {
    L2_GUARD_FUNCTION();

    if (fileName == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] html file name is null",
        //     "fhtml.cpp",
        //     0x91)
        // Log.Add(LOG_ERROR,
        //     "[HTML] HTML filename is null (country %u).",
        //     static_cast<unsigned int>(countryCode))
        L2_LOG_ERROR(Log, "[HTML] HTML filename is null (country %u).", static_cast<unsigned int>(countryCode));
    }

    std::u16string source;
    g_HTMLCache.GetHTMLFile(fileName, source, countryCode);

    try {
        std::u16string result;
        const std::u16string_view text = runtime::StringView(source);
        std::size_t cursor = 0;
        while (cursor < text.size()) {
            const std::size_t begin = text.find(u"<?", cursor);
            if (begin == std::u16string_view::npos) {
                result.append(text.substr(cursor));
                break;
            }
            result.append(text.substr(cursor, begin - cursor));

            const std::size_t keyBegin = begin + 2;
            const std::size_t end = text.find(u"?>", keyBegin);
            if (text.size() - keyBegin < 3 || end == std::u16string_view::npos) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Invalid html parameter",
                //     L"fhtml.cpp",
                //     0xb8)
                // Log.Add(LOG_ERROR,
                //     "[HTML] Malformed placeholder at UTF-16 offset %zu in \"%s\" (country %u). Appending the invalid-page
                //     marker and stopping substitution.", begin,
                //     Utf16ToUtf8(target_utf16::FromHostCodeUnits(fileName)).c_str(), static_cast<unsigned
                //     int>(countryCode))
                L2_LOG_ERROR(
                    Log,
                    "[HTML] Malformed placeholder at UTF-16 offset %zu in \"%s\" (country %u); appending the invalid-page "
                    "marker and stopping substitution.",
                    begin, Utf16ToUtf8(std::u16string_view(fileName ? fileName : u"")).c_str(),
                    static_cast<unsigned int>(countryCode)
                );
                result += u"***HTML page is invalid***";
                break;
            }

            const std::u16string key(text.substr(keyBegin, end - keyBegin));
            const auto found = parameters.find(key);
            if (found != parameters.end()) {
                result += found->second;
            }
            cursor = end + 2;
        }
        return result;
    } catch (const std::bad_alloc&) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] FHTML Failed : Low Memory",
        //     L"fhtml.cpp",
        //     0xa1)
        // Log.Add(LOG_ERROR,
        //     "[HTML] Not enough memory to substitute HTML parameters (country %u). Returning the fallback error page.",
        //     static_cast<unsigned int>(countryCode))
        L2_LOG_ERROR(
            Log, "[HTML] Not enough memory to substitute HTML parameters (country %u); returning the fallback error page.",
            static_cast<unsigned int>(countryCode)
        );
        return std::u16string(u"<html><body>FHTML Failed</body></html>");
    }
}
#endif
