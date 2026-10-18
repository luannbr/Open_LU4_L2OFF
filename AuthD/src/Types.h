#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Original owner: Src/types.h. Names and integral values come from the
// selected L2Authd.pdb. The spelling of deprecated entries is kept.
namespace SecAuth {

enum Enum : std::int32_t {
    NONE = 0,
    PCREGISTER = 1,
    __DEPRECATED__OTP = 2,
    SECURITYCARD = 3,
    TELEPHONEAUTH = 4,
    GOOGLEOTP = 5,
    FIDO = 6,
    DEVICEAUTH = 9,
    MAX = 10,
};

} // namespace SecAuth

namespace NpSecAuth {

enum Enum : std::int32_t {
    NONE = 0,
    VERISIGNOTP = 1,
    __DEPRECATED__MOBILIANSOTP = 2,
    TAS = 3,
    SECONDPWD = 4,
    GOOGLEOTP = 5,
    SECURITYCARDNCJ = 6,
    SECURITYCARDNCW = 7,
    IPAUTHZ = 8,
    DEVICEAUTH = 9,
    FIDO = 10,
    MAX = 11,
};

} // namespace NpSecAuth

bool ParseNpSecAuthType(
    SecAuth::Enum& type,
    const NpSecAuth::Enum& npType);

// L2Authd.pdb type 0x4F5C, sizeof 0x28.
class SecurityServiceFlag {
public:
    enum Enum : std::int32_t {
        NONE = 0,
        ACTIVATED = 1,
        INACTIVATED = 2,
        DEACTIVATED = 3,
        TENTATIVE = 4,
        MAX = 5,
    };

    enum NpEnum : std::int32_t {
        NP_NONE = 0,
        NP_ACTIVATED = 1,
        NP_INACTIVATED = 2,
        NP_LOCKED = 3,
        NP_TENTATIVE = 4,
        NP_MAX = 5,
    };

    SecurityServiceFlag();

    bool IsRegistered(const SecAuth::Enum& type) const;
    bool IsActivated(const SecAuth::Enum& type) const;
    bool IsInactivated(const SecAuth::Enum& type) const;
    bool IsTentative(const SecAuth::Enum& type) const;
    bool Set(const SecAuth::Enum& type, char state);
    bool SetFromNp(
        const NpSecAuth::Enum& npType,
        const NpEnum& npState);

private:
    std::array<Enum, SecAuth::MAX> m_serviceFlag;
};

// L2Authd.pdb type 0x4F68, sizeof 0x28.
class SecurityUsage {
public:
    enum Enum : std::int32_t {
        NOT_USED = 0,
        PASSED = 1,
        SUSPENED_TEMPORARY = 2,
        FAILED = 3,
        MAX = 4,
    };

    SecurityUsage();

    int BuildBitFlags() const;
    bool IsPassed(const SecAuth::Enum& type) const;
    bool SetPassed(const SecAuth::Enum& type);
    bool SetPassedFromNpCode(const NpSecAuth::Enum& npType);
    bool SetFailed(const SecAuth::Enum& type);

private:
    std::array<Enum, SecAuth::MAX> m_usage;
};
