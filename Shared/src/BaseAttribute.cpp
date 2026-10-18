#include "Shared/src/BaseAttribute.h"
#include "Shared/src/Runtime/Utf16Format.h"

#include <cwchar>

namespace runtime::process {
void ReportInvalidBaseAttributeRemoval(int attributeType);
}

namespace {

// Current L2NPC.exe 0x005a1260 and 0x005a1284. The complete tables are
// identical to the NASC64_2015 and TotalChecker2015 shared implementation.
constexpr short MaxAttrEnchantByLevelWeapon[9][2] = {
    {5, 25},
    {10, 75},
    {15, 150},
    {5, 175},
    {10, 225},
    {15, 300},
    {5, 325},
    {10, 375},
    {15, 450},
};

constexpr short MaxAttrEnchantByLevelArmor[9][2] = {
    {0, 0},
    {2, 12},
    {3, 30},
    {5, 60},
    {2, 72},
    {3, 90},
    {5, 120},
    {2, 132},
    {3, 150},
};

constexpr int kAttributeCount = ATTRT_MAX;
constexpr int kEnchantLevelCount = 9;

} // namespace

// Current L2NPC.exe 0x0041901c inlines Init into this constructor.
BaseAttribute::BaseAttribute() { Init(); }

BaseAttribute::BaseAttribute(short attackType, short attackValue, short* defendValues) :
    m_nAttackType(attackType), m_nAttackValue(attackValue), m_nNoneAttributeValue(0) {
    // Current Cached.exe 0x00450998.
    for (int index = 0; index < kAttributeCount; ++index) {
        m_nDefendValue[index] = defendValues[index];
    }
}

BaseAttribute::BaseAttribute(short attackType, short attackValue) :
    m_nAttackType(attackType), m_nAttackValue(attackValue), m_nNoneAttributeValue(0) {
    for (short& value : m_nDefendValue) {
        value = 0;
    }
}

BaseAttribute::BaseAttribute(short* defendValues) : m_nAttackType(ATTRT_NONE), m_nAttackValue(0), m_nNoneAttributeValue(0) {
    for (int index = 0; index < kAttributeCount; ++index) {
        m_nDefendValue[index] = defendValues[index];
    }
}

BaseAttribute::BaseAttribute(const BaseAttribute& other) {
    if (this != &other) {
        m_nAttackType = other.m_nAttackType;
        m_nAttackValue = other.m_nAttackValue;
        m_nNoneAttributeValue = other.m_nNoneAttributeValue;
        for (int index = 0; index < kAttributeCount; ++index) {
            m_nDefendValue[index] = other.m_nDefendValue[index];
        }
    }
}

BaseAttribute& BaseAttribute::operator=(const BaseAttribute& other) {
    // Current Cached.exe 0x004509c8.
    if (this != &other) {
        m_nAttackType = other.m_nAttackType;
        m_nAttackValue = other.m_nAttackValue;
        m_nNoneAttributeValue = other.m_nNoneAttributeValue;
        for (int index = 0; index < kAttributeCount; ++index) {
            m_nDefendValue[index] = other.m_nDefendValue[index];
        }
    }
    return *this;
}

bool BaseAttribute::operator==(const BaseAttribute& other) const {
    if (m_nAttackType != other.m_nAttackType || m_nAttackValue != other.m_nAttackValue ||
        m_nNoneAttributeValue != other.m_nNoneAttributeValue) {
        return false;
    }

    for (int index = 0; index < kAttributeCount; ++index) {
        if (m_nDefendValue[index] != other.m_nDefendValue[index]) {
            return false;
        }
    }
    return true;
}

bool BaseAttribute::operator!=(const BaseAttribute& other) const {
    if (m_nAttackType != other.m_nAttackType || m_nAttackValue != other.m_nAttackValue ||
        m_nNoneAttributeValue != other.m_nNoneAttributeValue) {
        return true;
    }

    for (int index = 0; index < kAttributeCount; ++index) {
        if (m_nDefendValue[index] != other.m_nDefendValue[index]) {
            return true;
        }
    }
    return false;
}

bool BaseAttribute::operator<(const BaseAttribute& other) const {
    if (m_nAttackType < other.m_nAttackType) {
        return true;
    }
    if (m_nAttackType != other.m_nAttackType) {
        return false;
    }

    for (int index = 0; index < kAttributeCount; ++index) {
        if (m_nDefendValue[index] < other.m_nDefendValue[index]) {
            return true;
        }
        if (other.m_nDefendValue[index] < m_nDefendValue[index]) {
            return false;
        }
    }
    return false;
}

bool BaseAttribute::IsAddable(const BaseAttribute& other) {
    return m_nAttackType == ATTRT_NONE || other.m_nAttackType == ATTRT_NONE || m_nAttackType == other.m_nAttackType;
}

bool BaseAttribute::IsNullAttribute() const { return *this == GetNullAttribute(); }

BaseAttribute& BaseAttribute::operator+=(const BaseAttribute& other) {
    if (!IsAddable(other)) {
        return *this;
    }

    if (m_nAttackType == ATTRT_NONE) {
        m_nAttackType = other.m_nAttackType;
    }
    m_nAttackValue = static_cast<short>(m_nAttackValue + other.m_nAttackValue);
    for (int index = 0; index < kAttributeCount; ++index) {
        m_nDefendValue[index] = static_cast<short>(m_nDefendValue[index] + other.m_nDefendValue[index]);
    }
    m_nNoneAttributeValue = static_cast<short>(m_nNoneAttributeValue + other.m_nNoneAttributeValue);
    return *this;
}

BaseAttribute BaseAttribute::operator+(const BaseAttribute& other) const {
    BaseAttribute result(*this);
    result += other;
    return result;
}

void BaseAttribute::Init() {
    // Current Cached.exe 0x00450a08.
    m_nAttackType = ATTRT_NONE;
    m_nAttackValue = 0;
    m_nNoneAttributeValue = 0;
    for (short& value : m_nDefendValue) {
        value = 0;
    }
}

int BaseAttribute::GetAttackLevel() {
    if (m_nAttackType == ATTRT_NONE) {
        return 0;
    }

    for (int level = 0; level < kEnchantLevelCount; ++level) {
        if (m_nAttackValue < MaxAttrEnchantByLevelWeapon[level][1]) {
            return level + 1;
        }
    }
    return kEnchantLevelCount + 1;
}

int BaseAttribute::GetAttackPoint() {
    if (m_nAttackType == ATTRT_NONE) {
        return 0;
    }

    for (int level = kEnchantLevelCount - 1; level >= 0; --level) {
        const short threshold = MaxAttrEnchantByLevelWeapon[level][1];
        if (m_nAttackValue == threshold) {
            return 0;
        }
        if (m_nAttackValue > threshold) {
            return (m_nAttackValue - threshold) / 5;
        }
    }

    // The shipping donor reads the zero word immediately before the table
    // after the loop. Express the same baseline without an out-of-bounds
    // access so the behavior is defined on every host.
    return m_nAttackValue / 5;
}

int BaseAttribute::GetArmorLevel() {
    int attributeType = ATTRT_NONE;
    for (int index = 0; index < kAttributeCount; ++index) {
        if (m_nDefendValue[index] > 0) {
            attributeType = index;
            break;
        }
    }
    if (attributeType == ATTRT_NONE) {
        return 0;
    }

    for (int level = 0; level < kEnchantLevelCount; ++level) {
        if (m_nDefendValue[attributeType] < MaxAttrEnchantByLevelArmor[level][1]) {
            return level + 1;
        }
    }
    return kEnchantLevelCount + 1;
}

int BaseAttribute::GetArmorPoint() {
    int attributeType = ATTRT_NONE;
    for (int index = 0; index < kAttributeCount; ++index) {
        if (m_nDefendValue[index] > 0) {
            attributeType = index;
            break;
        }
    }
    if (attributeType == ATTRT_NONE) {
        return 0;
    }

    const short value = m_nDefendValue[attributeType];
    for (int level = kEnchantLevelCount - 1; level >= 0; --level) {
        const short threshold = MaxAttrEnchantByLevelArmor[level][1];
        if (value == threshold) {
            return 0;
        }
        if (value > threshold) {
            return (value - threshold) / 6;
        }
    }

    // See GetAttackPoint: the target's post-loop table[-1] word is zero.
    return value / 6;
}

bool BaseAttribute::IsEnchanted(AttributeTypeEnum attributeType) {
    // Current Cached.exe 0x00450a38.
    const int type = static_cast<int>(attributeType);
    if (type < ATTRT_FIRE || type >= ATTRT_MAX) {
        if (m_nAttackType != ATTRT_NONE) {
            return true;
        }
        for (short value : m_nDefendValue) {
            if (value > 0) {
                return true;
            }
        }
        return false;
    }

    if (m_nAttackType == ATTRT_NONE) {
        return m_nDefendValue[type] > 0;
    }
    return type == m_nAttackType;
}

void BaseAttribute::GetLogString(char16_t* destination) {
    // Current Cached.exe 0x00450a80, 125 bytes, exact SHA-256
    // de65fe4b88d0921dd76cb1d9cdac713fbd1a48d41b45f93c1d59bef9f080cded.
    FormatUtf16To(
        destination, 128, u"%d:%d:%d:%d:%d:%d:%d:%d:%d", static_cast<int>(m_nAttackType), static_cast<int>(m_nAttackValue),
        static_cast<int>(m_nDefendValue[0]), static_cast<int>(m_nDefendValue[1]), static_cast<int>(m_nDefendValue[2]),
        static_cast<int>(m_nDefendValue[3]), static_cast<int>(m_nDefendValue[4]), static_cast<int>(m_nDefendValue[5]),
        static_cast<int>(m_nNoneAttributeValue)
    );
}

void BaseAttribute::Remove(AttributeTypeEnum attributeType) {
    // Current Cached.exe 0x00450b00.
    const int type = static_cast<int>(attributeType);
    if (type < ATTRT_FIRE || type >= ATTRT_MAX) {
        runtime::process::ReportInvalidBaseAttributeRemoval(type);
        return;
    }

    m_nNoneAttributeValue = 0;
    if (m_nAttackType == type) {
        m_nAttackType = ATTRT_NONE;
        m_nAttackValue = 0;
    } else {
        m_nDefendValue[type] = 0;
    }
}

BaseAttribute& BaseAttribute::GetNullAttribute() {
    static BaseAttribute nullAttribute;
    return nullAttribute;
}
