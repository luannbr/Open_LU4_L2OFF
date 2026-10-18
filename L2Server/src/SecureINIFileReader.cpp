#include "Shared/src/Log.h"
#include "Shared/src/Runtime/ConfigDirectory.h"
#include "Shared/src/Runtime/Utf8.h"
#include "Shared/src/TextRuntime.h"

#include "SecureINIFileReader.h"


#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <utility>
#include <vector>

namespace l2server {
namespace {

bool IsSpace(char16_t value) {
    return value == u' ' || value == u'\t' || value == u'\r' || value == u'\n' || value == u'\v' || value == u'\f';
}

std::u16string Trim(std::u16string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && IsSpace(value[begin])) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && IsSpace(value[end - 1])) {
        --end;
    }
    return std::u16string(value.substr(begin, end - begin));
}

char16_t LowerCodeUnit(char16_t value) {
    // The current bodies call C tolower on each 16-bit unit. In the shipping
    // C locale that changes ASCII A..Z and leaves the Korean configuration
    // text untouched.
    if (value >= u'A' && value <= u'Z') {
        return static_cast<char16_t>(value + (u'a' - u'A'));
    }
    return value;
}

std::u16string Normalize(std::u16string_view value) {
    std::u16string result = Trim(value);
    std::transform(result.begin(), result.end(), result.begin(), LowerCodeUnit);
    return result;
}

bool DecodeUtf8(const std::vector<unsigned char>& bytes, std::size_t start, std::u16string& output) {
    output.clear();
    for (std::size_t offset = start; offset < bytes.size();) {
        const std::uint8_t lead = bytes[offset++];
        std::uint32_t codePoint = 0;
        int continuationCount = 0;
        std::uint32_t minimum = 0;
        if (lead < 0x80U) {
            codePoint = lead;
        } else if ((lead & 0xe0U) == 0xc0U) {
            continuationCount = 1;
            codePoint = lead & 0x1fU;
            minimum = 0x80U;
        } else if ((lead & 0xf0U) == 0xe0U) {
            continuationCount = 2;
            codePoint = lead & 0x0fU;
            minimum = 0x800U;
        } else if ((lead & 0xf8U) == 0xf0U) {
            continuationCount = 3;
            codePoint = lead & 0x07U;
            minimum = 0x10000U;
        } else {
            return false;
        }
        for (int index = 0; index < continuationCount; ++index) {
            if (offset >= bytes.size() || (bytes[offset] & 0xc0U) != 0x80U) {
                return false;
            }
            codePoint = (codePoint << 6U) | (bytes[offset++] & 0x3fU);
        }
        if ((continuationCount != 0 && codePoint < minimum) || codePoint > 0x10ffffU ||
            (codePoint >= 0xd800U && codePoint <= 0xdfffU)) {
            return false;
        }
        if (codePoint <= 0xffffU) {
            output.push_back(static_cast<char16_t>(codePoint));
        } else {
            codePoint -= 0x10000U;
            output.push_back(static_cast<char16_t>(0xd800U + (codePoint >> 10U)));
            output.push_back(static_cast<char16_t>(0xdc00U + (codePoint & 0x3ffU)));
        }
    }
    return true;
}

bool DecodeConfiguration(const std::vector<unsigned char>& bytes, std::u16string& output) {
    if (bytes.size() >= 2 && ((bytes[0] == 0xffU && bytes[1] == 0xfeU) || (bytes[0] == 0xfeU && bytes[1] == 0xffU))) {
        if ((bytes.size() & 1U) != 0U) {
            return false;
        }
        const bool littleEndian = bytes[0] == 0xffU;
        output.clear();
        output.reserve((bytes.size() - 2) / 2);
        for (std::size_t offset = 2; offset < bytes.size(); offset += 2) {
            const std::uint16_t word = littleEndian ? static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8U))
                                                    : static_cast<std::uint16_t>((bytes[offset] << 8U) | bytes[offset + 1]);
            if (word >= 0xd800U && word <= 0xdbffU) {
                if (offset + 3 >= bytes.size()) {
                    return false;
                }
                const std::uint16_t low = littleEndian
                    ? static_cast<std::uint16_t>(bytes[offset + 2] | (bytes[offset + 3] << 8U))
                    : static_cast<std::uint16_t>((bytes[offset + 2] << 8U) | bytes[offset + 3]);
                if (low < 0xdc00U || low > 0xdfffU) {
                    return false;
                }
                output.push_back(static_cast<char16_t>(word));
                output.push_back(static_cast<char16_t>(low));
                offset += 2;
            } else if (word >= 0xdc00U && word <= 0xdfffU) {
                return false;
            } else {
                output.push_back(static_cast<char16_t>(word));
            }
        }
        return true;
    }

    const std::size_t start = bytes.size() >= 3 && bytes[0] == 0xefU && bytes[1] == 0xbbU && bytes[2] == 0xbfU ? 3 : 0;
    return DecodeUtf8(bytes, start, output);
}

std::filesystem::path HostPath(std::u16string_view logicalPath) {
#if defined(_WIN32)
    return std::filesystem::path(std::u16string(logicalPath));
#else
    std::u16string normalized(logicalPath);
    std::replace(normalized.begin(), normalized.end(), u'\\', u'/');
    return std::filesystem::path(runtime::Utf8CodeUnits(Utf16ToUtf8(normalized)));
#endif
}

bool RemoveCommentAndQuotes(std::u16string_view raw, std::u16string& output) {
    char16_t quote = 0;
    std::size_t end = raw.size();
    for (std::size_t index = 0; index < raw.size(); ++index) {
        const char16_t value = raw[index];
        if ((value == u'\'' || value == u'"')) {
            if (quote == 0) {
                quote = value;
            } else if (quote == value) {
                quote = 0;
            }
        } else if (value == u';' && quote == 0) {
            end = index;
            break;
        }
    }
    if (quote != 0) {
        return false;
    }
    output = Trim(raw.substr(0, end));
    if (!output.empty() && (output.front() == u'\'' || output.front() == u'"')) {
        if (output.size() < 2 || output.back() != output.front()) {
            return false;
        }
        output = output.substr(1, output.size() - 2);
    }
    return true;
}

std::int32_t DecimalPrefix(std::u16string_view value, std::int32_t defaultValue) {
    std::size_t offset = 0;
    while (offset < value.size() && IsSpace(value[offset])) {
        ++offset;
    }
    bool negative = false;
    if (offset < value.size() && (value[offset] == u'+' || value[offset] == u'-')) {
        negative = value[offset] == u'-';
        ++offset;
    }
    const std::size_t digitStart = offset;
    const std::uint64_t limit = negative ? static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()) + 1U
                                         : static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max());
    std::uint64_t magnitude = 0;
    while (offset < value.size() && value[offset] >= u'0' && value[offset] <= u'9') {
        const std::uint64_t digit = value[offset++] - u'0';
        if (magnitude > (limit - digit) / 10U) {
            return defaultValue;
        }
        magnitude = magnitude * 10U + digit;
    }
    if (offset == digitStart) {
        return 0;
    }
    if (negative && magnitude == limit) {
        return std::numeric_limits<std::int32_t>::min();
    }
    const std::int32_t result = static_cast<std::int32_t>(magnitude);
    return negative ? -result : result;
}

} // namespace

CSecureINIFileReader g_SecureINIFileDB;

bool CSecureINIFileReader::ParseFile(const std::u16string& text, SecureFile& file, std::size_t& errorLine) {
    std::size_t lineNumber = 0;
    std::size_t offset = 0;
    SecureSection* activeSection = nullptr;
    bool sawSection = false;

    while (offset <= text.size()) {
        ++lineNumber;
        const std::size_t newline = text.find(u'\n', offset);
        const std::size_t end = newline == std::u16string::npos ? text.size() : newline;
        std::u16string line = Trim(std::u16string_view(text).substr(offset, end - offset));
        offset = newline == std::u16string::npos ? text.size() + 1 : newline + 1;

        if (line.empty() || line.front() == u';') {
            continue;
        }
        if (line.front() == u'[') {
            const std::size_t close = line.find(u']');
            if (close == std::u16string::npos) {
                errorLine = lineNumber;
                return false;
            }
            std::u16string trailing;
            if (!RemoveCommentAndQuotes(std::u16string_view(line).substr(close + 1), trailing) || !trailing.empty()) {
                errorLine = lineNumber;
                return false;
            }
            const std::u16string name = Normalize(std::u16string_view(line).substr(1, close - 1));
            if (name.empty()) {
                errorLine = lineNumber;
                return false;
            }
            sawSection = true;
            auto insertion = file.sections.emplace(name, SecureSection{});
            activeSection = insertion.second ? &insertion.first->second : nullptr;
            continue;
        }

        const std::size_t equals = line.find(u'=');
        if (equals == std::u16string::npos || !sawSection) {
            errorLine = lineNumber;
            return false;
        }
        // A later duplicate section is not merged into the first ordered-map
        // entry. Its syntactically valid key/value lines are ignored.
        if (!activeSection) {
            continue;
        }
        const std::u16string key = Normalize(std::u16string_view(line).substr(0, equals));
        if (key.empty()) {
            errorLine = lineNumber;
            return false;
        }
        std::u16string value;
        if (!RemoveCommentAndQuotes(std::u16string_view(line).substr(equals + 1), value)) {
            errorLine = lineNumber;
            return false;
        }
        activeSection->values.emplace(key, SecureValue{Normalize(value), std::move(value)});
    }
    errorLine = 0;
    return true;
}

bool CSecureINIFileReader::Load(std::u16string_view logicalPath) {
    L2_GUARD_FUNCTION();
    const auto hostPath = HostPath(logicalPath);
    return Load(logicalPath, runtime::ConfigFile(hostPath, hostPath));
}

bool CSecureINIFileReader::Load(std::u16string_view logicalPath, const std::filesystem::path& hostPath) {
    const std::u16string normalizedPath = Normalize(logicalPath);
    std::lock_guard<std::mutex> guard(mutex_);
    if (files_.find(normalizedPath) != files_.end()) {
        lastFailure_ = {FailureKind::AlreadyLoaded, std::u16string(logicalPath), 0};
        // Log.Add(
        //     LOG_ERROR,
        //     "Already loading %s",
        //     ToUtf8(logicalPath).c_str())
        // L2_LOG_ERROR(Log,
        //     "Already loading %s",
        //     ToUtf8(logicalPath).c_str())
        L2_LOG_ERROR(
            Log, "[configuration] Configuration \"%s\" is already loaded; the requested reload was rejected.",
            Utf16ToUtf8(logicalPath).c_str()
        );
        return false;
    }

    std::ifstream input(hostPath, std::ios::binary);
    if (!input) {
        lastFailure_ = {FailureKind::Unreadable, std::u16string(logicalPath), 0};
        // Log.Add(
        //     LOG_ERROR,
        //     "cannot read file %s",
        //     ToUtf8(logicalPath).c_str())
        // L2_LOG_ERROR(Log,
        //     "cannot read file %s",
        //     ToUtf8(logicalPath).c_str())
#if defined(_WIN32)
        L2_LOG_ERROR(Log, u"[configuration] Cannot open configuration file \"%ls\"; file was not loaded.", hostPath.c_str());
#else
        L2_LOG_ERROR(Log, "[configuration] Cannot open configuration file \"%s\"; file was not loaded.", hostPath.c_str());
#endif
        return false;
    }
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (!input.good() && !input.eof()) {
        lastFailure_ = {FailureKind::Unreadable, std::u16string(logicalPath), 0};
#if defined(_WIN32)
        L2_LOG_ERROR(
            Log, u"[configuration] Could not finish reading configuration file \"%ls\"; file was not loaded.",
            hostPath.c_str()
        );
#else
        L2_LOG_ERROR(
            Log, "[configuration] Could not finish reading configuration file \"%s\"; file was not loaded.", hostPath.c_str()
        );
#endif
        return false;
    }

    std::u16string decoded;
    if (!DecodeConfiguration(bytes, decoded)) {
        lastFailure_ = {FailureKind::Decode, std::u16string(logicalPath), 0};
        // Log.Add(
        //     LOG_ERROR,
        //     "decoding %s failed",
        //     ToUtf8(logicalPath).c_str())
        // L2_LOG_ERROR(Log,
        //     "decoding %s failed",
        //     ToUtf8(logicalPath).c_str())
#if defined(_WIN32)
        L2_LOG_ERROR(
            Log,
            u"[configuration] Invalid text encoding in configuration file \"%ls\"; expected UTF-8, or UTF-16 with a "
            u"byte-order mark.",
            hostPath.c_str()
        );
#else
        L2_LOG_ERROR(
            Log,
            "[configuration] Invalid text encoding in configuration file \"%s\"; expected UTF-8, or UTF-16 with a "
            "byte-order mark.",
            hostPath.c_str()
        );
#endif
        return false;
    }

    SecureFile file;
    std::size_t parseErrorLine = 0;
    if (!ParseFile(decoded, file, parseErrorLine)) {
        lastFailure_ = {FailureKind::Parse, std::u16string(logicalPath), parseErrorLine};
        // Log.Add(
        //     LOG_ERROR,
        //     "Parsing %s file failed, line[%zu]",
        //     ToUtf8(logicalPath).c_str(),
        //     parseErrorLine)
        // L2_LOG_ERROR(Log,
        //     "Parsing %s file failed, line[%zu]",
        //     ToUtf8(logicalPath).c_str(),
        //     parseErrorLine)
#if defined(_WIN32)
        L2_LOG_ERROR(
            Log, u"[configuration] Invalid configuration syntax in file \"%ls\" at line %zu; file was not loaded.",
            hostPath.c_str(), parseErrorLine
        );
#else
        L2_LOG_ERROR(
            Log, "[configuration] Invalid configuration syntax in file \"%s\" at line %zu; file was not loaded.",
            hostPath.c_str(), parseErrorLine
        );
#endif
        return false;
    }

    files_.emplace(normalizedPath, std::move(file));
    lastFailure_ = {};
    // Log.Add(
    //     LOG_NORMAL,
    //     "loading %s",
    //     ToUtf8(logicalPath).c_str())
    // L2_LOG_INFO(Log,
    //     "loading %s",
    //     ToUtf8(logicalPath).c_str())
#if defined(_WIN32)
    L2_LOG_INFO(Log, u"[configuration] Loaded configuration file \"%ls\".", hostPath.c_str());
#else
    L2_LOG_INFO(Log, "[configuration] Loaded configuration file \"%s\".", hostPath.c_str());
#endif
    return true;
}

const CSecureINIFileReader::SecureValue* CSecureINIFileReader::FindValueLocked(
    std::u16string_view logicalPath, std::u16string_view section, std::u16string_view key
) const {
    const auto file = files_.find(Normalize(logicalPath));
    if (file == files_.end()) {
        return nullptr;
    }
    const auto foundSection = file->second.sections.find(Normalize(section));
    if (foundSection == file->second.sections.end()) {
        return nullptr;
    }
    const auto value = foundSection->second.values.find(Normalize(key));
    return value == foundSection->second.values.end() ? nullptr : &value->second;
}

std::uint32_t CSecureINIFileReader::GetString(
    std::u16string_view section,
    std::u16string_view key,
    std::u16string_view defaultValue,
    char16_t* output,
    std::uint32_t capacity,
    std::u16string_view logicalPath
) const {
    L2_GUARD_FUNCTION();
    std::lock_guard<std::mutex> guard(mutex_);
    const SecureValue* found = FindValueLocked(logicalPath, section, key);
    const std::u16string fallback = found ? std::u16string{} : Normalize(defaultValue);
    const std::u16string& value = found ? found->normalized : fallback;
    if (!output || capacity == 0 || value.size() >= capacity) {
        if (output && capacity != 0) {
            output[0] = 0;
        }
        return 0;
    }
    std::copy(value.begin(), value.end(), output);
    output[value.size()] = 0;
    return static_cast<std::uint32_t>(value.size());
}

std::uint32_t CSecureINIFileReader::GetProfileString(
    std::u16string_view section,
    std::u16string_view key,
    std::u16string_view defaultValue,
    char16_t* output,
    std::uint32_t capacity,
    std::u16string_view logicalPath
) const {
    std::lock_guard<std::mutex> guard(mutex_);
    const SecureValue* found = FindValueLocked(logicalPath, section, key);
    const std::u16string fallback(defaultValue);
    const std::u16string& value = found ? found->profile : fallback;
    if (!output || capacity == 0 || value.size() >= capacity) {
        if (output && capacity != 0) {
            output[0] = 0;
        }
        return 0;
    }
    std::copy(value.begin(), value.end(), output);
    output[value.size()] = 0;
    return static_cast<std::uint32_t>(value.size());
}

std::int32_t CSecureINIFileReader::GetInt(
    std::u16string_view section, std::u16string_view key, std::int32_t defaultValue, std::u16string_view logicalPath
) const {
    L2_GUARD_FUNCTION();
    std::lock_guard<std::mutex> guard(mutex_);
    const SecureValue* value = FindValueLocked(logicalPath, section, key);
    return value ? DecimalPrefix(value->normalized, defaultValue) : defaultValue;
}

bool CSecureINIFileReader::HasValue(
    std::u16string_view section, std::u16string_view key, std::u16string_view logicalPath
) const {
    std::lock_guard<std::mutex> guard(mutex_);
    return FindValueLocked(logicalPath, section, key) != nullptr;
}

bool CSecureINIFileReader::IsLoaded(std::u16string_view logicalPath) const {
    std::lock_guard<std::mutex> guard(mutex_);
    return files_.find(Normalize(logicalPath)) != files_.end();
}

CSecureINIFileReader::Failure CSecureINIFileReader::LastFailure() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return lastFailure_;
}

std::u16string CSecureINIFileReader::Print() {
    // Current L2Server 0x00a3cee8. The current virtual file/section Print
    // owners at 0x00a3d13c/0x00a3d3fc serialize the already-loaded,
    // normalized maps while this reader lock is held.
    std::lock_guard<std::mutex> guard(mutex_);
    std::u16string result;
    for (const auto& [logicalPath, file] : files_) {
        result += u"FILE [";
        result += logicalPath;
        result += u"]\r\n";
        for (const auto& [sectionName, section] : file.sections) {
            result += u"[";
            result += sectionName;
            result += u"]\r\n";
            for (const auto& [key, value] : section.values) {
                result += key;
                result += u"=";
                result += value.normalized;
                result += u"\r\n";
            }
            result += u"\r\n";
        }
        result += u"\r\n";
    }
    return result;
}

} // namespace l2server
