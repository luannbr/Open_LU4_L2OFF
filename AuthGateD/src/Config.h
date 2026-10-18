#pragma once

#include "Shared/src/Runtime/WindowsTypes.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

class CConfig {
public:
    CConfig() = default;
    virtual ~CConfig() = default;

    CConfig(const CConfig&) = delete;
    CConfig& operator=(const CConfig&) = delete;

    bool Load(const char16_t* path);
    char16_t* Get(const char16_t* name);
    const char16_t* Get(const char16_t* name) const;
    bool GetBool(const char16_t* name, bool defaultValue) const;
    std::int32_t GetInt(
        const char16_t* name,
        std::int32_t defaultValue) const;
    in_addr GetInetAddr(const char16_t* name) const;

protected:
    virtual void InitConfig() = 0;

private:
    enum class Token : char16_t {
        End = 0,
        Value = 1,
        NewLine = u'\n',
        Comment = u';',
        Equal = u'=',
    };

    bool LoadFile(
        const std::filesystem::path& path,
        std::u16string& text) const;
    static Token GetToken(
        const char16_t*& cursor,
        std::u16string& value);

    std::unordered_map<std::u16string, std::u16string> m_mapConfig;
};
