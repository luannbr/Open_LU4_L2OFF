#include "FontCheck.h"

#include "Shared/src/Log.h"
#include "Shared/src/TextRuntime.h"

#include <cstdio>
#include <limits>
#include <new>
#include <string>
#include <string_view>

std::uint64_t gTotalSize = 0;
NFontCheck g_FontCheck{};

NFontCheck::~NFontCheck() {
    // Current g_FontCheck atexit wrapper 0x00dd63c4. It clears only the
    // pointer lane. The target leaves the allocated array and
    // all nested glyph buffers process-owned through termination.
    m_pCharSet = nullptr;
}

NCharSet::NCharSet() : m_start(0), m_range(0), m_Info(nullptr) {}

NCharSet::~NCharSet() { m_Info = nullptr; }

bool NCharSet::LoadGlyphInfo(const int start, const int range, std::FILE* const fp) {
    L2_GUARD_FUNCTION();

    if (!fp) {
        return false;
    }

    m_start = start;
    m_range = range;

    std::size_t allocationSize = std::numeric_limits<std::size_t>::max();
    if (range >= 0) {
        allocationSize = static_cast<std::size_t>(range) * sizeof(FFontGlyphInfo);
    }
    m_Info = static_cast<FFontGlyphInfo*>(::operator new[](allocationSize));

    for (int index = 0; index < range; ++index) {
        gTotalSize += std::fread(m_Info + index, 1, sizeof(FFontGlyphInfo), fp);
    }
    return true;
}

bool NCharSet::CheckGlyphInfo(const int nCode) {
    L2_GUARD_FUNCTION();

    const std::int32_t upperBound =
        static_cast<std::int32_t>(static_cast<std::uint32_t>(m_start) + static_cast<std::uint32_t>(m_range));
    if (m_start <= nCode && nCode < upperBound) {
        const auto index = static_cast<std::ptrdiff_t>(nCode) - static_cast<std::ptrdiff_t>(m_start);
        return m_Info[index].X >= 0;
    }
    return false;
}

bool NFontCheck::Load(const char16_t* const filename) {
    // Current L2Server 0x00737a30.
    L2_GUARD_FUNCTION();

    const std::u16string_view targetFilename(filename);
    std::wstring hostFilename;
#if defined(_WIN32)
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    const wchar_t* const logFilename = reinterpret_cast<const wchar_t*>(filename);
#else
    hostFilename = Utf16ToWide(targetFilename);
    const wchar_t* const logFilename = hostFilename.c_str();
#endif

    if (!targetFilename.empty()) {
        std::FILE* input = nullptr;
#if defined(_WIN32)
        ::_wfopen_s(&input, logFilename, L"rb");
#else
        const std::string hostPath = Utf16ToUtf8(targetFilename);
        input = std::fopen(hostPath.c_str(), "rb");
#endif
        if (input == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"font check failed... can not open file[%s]",
            //     logFilename)
            // L2_LOG_ERROR(Log,
            //     L"font check failed... can not open file[%s]",
            //     logFilename)
            L2_LOG_ERROR(Log, "Cannot open font-check data '%s'; loading stops.", WideToUtf8(logFilename).c_str());
            return false;
        }

        gTotalSize += std::fread(&m_FontWidth, 1, sizeof(m_FontWidth), input);
        gTotalSize += std::fread(&m_FontHeight, 1, sizeof(m_FontHeight), input);
        gTotalSize += std::fread(&m_nCharSet, 1, sizeof(m_nCharSet), input);

        if (m_nCharSet != 0) {
            m_pCharSet = new NCharSet[static_cast<std::size_t>(m_nCharSet)];
            for (int index = 0; index < m_nCharSet; ++index) {
                int start = 0;
                int range = 0;
                gTotalSize += std::fread(&start, 1, sizeof(start), input);
                gTotalSize += std::fread(&range, 1, sizeof(range), input);
                if (!m_pCharSet[index].LoadGlyphInfo(start, range, input)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"font check failed... can not LoadGlyphInfo [%s]",
                    //     logFilename)
                    // L2_LOG_ERROR(Log,
                    //     L"font check failed... can not LoadGlyphInfo [%s]",
                    //     logFilename)
                    L2_LOG_ERROR(
                        Log,
                        "Cannot load font-check glyph group %d from '%s' (start %d, range %d); earlier data is retained.",
                        index, WideToUtf8(logFilename).c_str(), start, range
                    );
                    // The current failure edge leaves the stream
                    // and newly allocated charset array open/owned.
                    return false;
                }
            }
        }
        std::fclose(input);
    }

    // Log.Add(LOG_NORMAL, L"font check loaded...")
    // L2_LOG_INFO(Log, L"font check loaded...")
    L2_LOG_INFO(
        Log, "Font-check load returned successfully for '%s'%s; configured glyph-group count %d.",
        WideToUtf8(logFilename).c_str(), targetFilename.empty() ? " (empty path; no file read)" : "", m_nCharSet
    );
    return true;
}

bool NFontCheck::CheckCode(const int code) {
    // Current L2Server 0x00737c98.
    L2_GUARD_FUNCTION();
    if (m_nCharSet < 1) {
        return true;
    }
    for (int index = 0; index < m_nCharSet; ++index) {
        if (m_pCharSet[index].CheckGlyphInfo(code)) {
            return true;
        }
    }
    return false;
}

bool NFontCheck::Init() {
    // Current L2Server 0x00737d6c. Load failure is ignored.
    L2_GUARD_FUNCTION();
    extern int g_nFontCheck;
    extern char16_t g_sFontCheckName[256];
    if (g_nFontCheck > 0) {
        if (g_sFontCheckName[0] == u'\0') {
            // Log.Add(
            //     LOG_ERROR,
            //     L"font check failed... no font file name")
            // L2_LOG_ERROR(Log,
            //     L"font check failed... no font file name")
            L2_LOG_ERROR(Log, "Font checking is enabled but the filename is empty; loading skipped.");
        } else {
            Load(g_sFontCheckName);
        }
    }
    return true;
}
