// config.h  -  a small Win32-profile-compatible INI reader used by the portable
// server targets. It accepts the current distribution's UTF-16 l2cache.ini
// [Section]/key=value form while preserving first-section/first-key behavior.
#pragma once
#include <map>
#include <string>

namespace runtime {

class Config {
public:
    bool Load(const std::string& path, bool emitDiagnostics = true);
    // WritePrivateProfileString-compatible mutation used by L2NPC's lotto
    // administration. The supplied value is written unchanged (and may be
    // quoted). The original UTF-8/UTF-16 encoding and newline style are kept.
    bool Set(const std::string& section, const std::string& key, const std::string& value);
    std::string Get(const std::string& section, const std::string& key, const std::string& def = "") const;
    int GetInt(const std::string& section, const std::string& key, int def = 0) const;

    const std::string& Path() const { return path_; }
    bool Loaded() const { return loaded_; }

private:
    std::map<std::string, std::string> kv_; // "section/key" (lowercased) -> value
    std::string path_;
    bool loaded_ = false;
};

} // namespace runtime
