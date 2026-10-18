#include "Types.h"

// L2Authd.exe 0x0048F4F0, Src/types.cpp:8-32.
bool ParseNpSecAuthType(
    SecAuth::Enum& type,
    const NpSecAuth::Enum& npType) {
    switch (npType) {
    case NpSecAuth::TAS:
        type = SecAuth::TELEPHONEAUTH;
        return true;
    case NpSecAuth::GOOGLEOTP:
        type = SecAuth::GOOGLEOTP;
        return true;
    case NpSecAuth::SECURITYCARDNCJ:
        type = SecAuth::SECURITYCARD;
        return true;
    case NpSecAuth::DEVICEAUTH:
        type = SecAuth::DEVICEAUTH;
        return true;
    case NpSecAuth::FIDO:
        type = SecAuth::FIDO;
        return true;
    default:
        return false;
    }
}

// L2Authd.exe 0x0048F580, Src/types.cpp:35-37.
SecurityServiceFlag::SecurityServiceFlag() {
    m_serviceFlag.fill(NONE);
}

// L2Authd.exe 0x0048F5B0, Src/types.cpp:40-42.
bool SecurityServiceFlag::IsRegistered(
    const SecAuth::Enum& type) const {
    return IsActivated(type) ||
           IsInactivated(type) ||
           IsTentative(type);
}

// L2Authd.exe 0x0048F610, Src/types.cpp:45-50.
bool SecurityServiceFlag::IsActivated(
    const SecAuth::Enum& type) const {
    const auto index = static_cast<std::size_t>(type);
    return index < m_serviceFlag.size() &&
           m_serviceFlag[index] == ACTIVATED;
}

// L2Authd.exe 0x0048F660, Src/types.cpp:53-58.
bool SecurityServiceFlag::IsInactivated(
    const SecAuth::Enum& type) const {
    const auto index = static_cast<std::size_t>(type);
    return index < m_serviceFlag.size() &&
           m_serviceFlag[index] == INACTIVATED;
}

// L2Authd.exe 0x0048F6B0, Src/types.cpp:61-66.
bool SecurityServiceFlag::IsTentative(
    const SecAuth::Enum& type) const {
    const auto index = static_cast<std::size_t>(type);
    return index < m_serviceFlag.size() &&
           m_serviceFlag[index] == TENTATIVE;
}

// L2Authd.exe 0x0048F700, Src/types.cpp:69-75.
bool SecurityServiceFlag::Set(
    const SecAuth::Enum& type,
    char state) {
    const auto index = static_cast<std::size_t>(type);
    if (index >= m_serviceFlag.size()) {
        return false;
    }

    m_serviceFlag[index] = static_cast<Enum>(state);
    return true;
}

// L2Authd.exe 0x0048F740, Src/types.cpp:81-96.
bool SecurityServiceFlag::SetFromNp(
    const NpSecAuth::Enum& npType,
    const NpEnum& npState) {
    SecAuth::Enum type = SecAuth::NONE;
    if (!ParseNpSecAuthType(type, npType)) {
        return false;
    }

    switch (npState) {
    case NP_NONE:
        m_serviceFlag[type] = NONE;
        return true;
    case NP_ACTIVATED:
        m_serviceFlag[type] = ACTIVATED;
        return true;
    case NP_INACTIVATED:
        m_serviceFlag[type] = INACTIVATED;
        return true;
    case NP_TENTATIVE:
        m_serviceFlag[type] = TENTATIVE;
        return true;
    default:
        m_serviceFlag[type] = NONE;
        return false;
    }
}

// L2Authd.exe 0x0048F810, Src/types.cpp:99-101.
SecurityUsage::SecurityUsage() {
    m_usage.fill(NOT_USED);
}

// L2Authd.exe 0x0048F840, Src/types.cpp:104-113.
int SecurityUsage::BuildBitFlags() const {
    int flags = 0;
    for (std::int32_t value = SecAuth::PCREGISTER;
         value < SecAuth::MAX;
         ++value) {
        const auto type = static_cast<SecAuth::Enum>(value);
        if (IsPassed(type)) {
            flags |= 1 << (value - 1);
        }
    }
    return flags;
}

// L2Authd.exe 0x0048F8A0, Src/types.cpp:116-118.
bool SecurityUsage::IsPassed(
    const SecAuth::Enum& type) const {
    return m_usage[static_cast<std::size_t>(type)] == PASSED;
}

// L2Authd.exe 0x0048F8E0, Src/types.cpp:121-127.
bool SecurityUsage::SetPassed(
    const SecAuth::Enum& type) {
    const auto index = static_cast<std::size_t>(type);
    if (index >= m_usage.size()) {
        return false;
    }

    m_usage[index] = PASSED;
    return true;
}

// L2Authd.exe 0x0048F920, Src/types.cpp:130-140.
bool SecurityUsage::SetPassedFromNpCode(
    const NpSecAuth::Enum& npType) {
    SecAuth::Enum type = SecAuth::NONE;
    if (!ParseNpSecAuthType(type, npType)) {
        return false;
    }
    return SetPassed(type);
}

// L2Authd.exe 0x0048F970, Src/types.cpp:143-149.
bool SecurityUsage::SetFailed(
    const SecAuth::Enum& type) {
    const auto index = static_cast<std::size_t>(type);
    if (index >= m_usage.size()) {
        return false;
    }

    m_usage[index] = FAILED;
    return true;
}
