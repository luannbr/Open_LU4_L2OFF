#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
// Windows.h defines GetProfileString as a macro. Remove it in every translation
// unit so the method has one name.
#ifdef GetProfileString
#undef GetProfileString
#endif

namespace l2server {

// Current L2Server roots:
//   00a3e2e0 CSecureINIFileReader::Load
//   00a3cc58 CSecureINIFileReader::GetString
//   00a3cdac CSecureINIFileReader::GetInt
// Windows WCHAR is represented as a 16-bit code unit so this
// contract does not change when host wchar_t is 32 bits.
class CSecureINIFileReader final {
public:
    enum class FailureKind {
        None,
        Unreadable,
        AlreadyLoaded,
        Decode,
        Parse,
    };

    struct Failure {
        FailureKind kind = FailureKind::None;
        std::u16string logicalPath;
        std::size_t line = 0;
    };

    bool Load(std::u16string_view logicalPath);
    bool Load(std::u16string_view logicalPath, const std::filesystem::path& hostPath);

    std::uint32_t GetString(
        std::u16string_view section,
        std::u16string_view key,
        std::u16string_view defaultValue,
        char16_t* output,
        std::uint32_t capacity,
        std::u16string_view logicalPath
    ) const;

    // Platform replacement for the seven CConfig reads that call
    // GetPrivateProfileStringW directly in the current image. Unlike the
    // SecureINI GetString contract, profile values/defaults keep case.
    std::uint32_t GetProfileString(
        std::u16string_view section,
        std::u16string_view key,
        std::u16string_view defaultValue,
        char16_t* output,
        std::uint32_t capacity,
        std::u16string_view logicalPath
    ) const;

    std::int32_t GetInt(
        std::u16string_view section, std::u16string_view key, std::int32_t defaultValue, std::u16string_view logicalPath
    ) const;

    bool HasValue(std::u16string_view section, std::u16string_view key, std::u16string_view logicalPath) const;

    bool IsLoaded(std::u16string_view logicalPath) const;
    Failure LastFailure() const;

    std::u16string Print();

private:
    struct SecureValue {
        std::u16string normalized;
        std::u16string profile;
    };

    struct SecureSection {
        std::map<std::u16string, SecureValue> values;
    };

    struct SecureFile {
        std::map<std::u16string, SecureSection> sections;
    };

    static bool ParseFile(const std::u16string& text, SecureFile& file, std::size_t& errorLine);

    const SecureValue* FindValueLocked(
        std::u16string_view logicalPath, std::u16string_view section, std::u16string_view key
    ) const;

    mutable std::mutex mutex_;
    std::map<std::u16string, SecureFile> files_;
    Failure lastFailure_;
};

extern CSecureINIFileReader g_SecureINIFileDB;

} // namespace l2server
