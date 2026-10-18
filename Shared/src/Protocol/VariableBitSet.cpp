#include "Protocol/VariableBitSet.h"

#include <algorithm>
#include <cmath>
#include <cstring>


std::uint8_t CVariableBitSet::s_bitmaskArray[8]{};
bool CVariableBitSet::s_isBitmaskArrayInited = false;

CVariableBitSet::CVariableBitSet() : m_bits(nullptr), m_bitCount(0), m_byteCount(0) {}

CVariableBitSet::~CVariableBitSet() {
    m_bits = nullptr;
    m_bitCount = 0;
    m_byteCount = 0;
}

void CVariableBitSet::InitBitmaskArray() {
    if (s_isBitmaskArrayInited) {
        return;
    }
    for (std::uint32_t index = 0; index < 8; ++index) {
        s_bitmaskArray[index] = static_cast<std::uint8_t>(1U << index);
    }
    s_isBitmaskArrayInited = true;
}

bool CVariableBitSet::Init(std::uint32_t bitCount, std::uint32_t byteCount, std::uint8_t* bits) {
    InitBitmaskArray();
    if (!s_isBitmaskArrayInited || bitCount == 0 || byteCount == 0 || bitCount >= MAX_BITS ||
        byteCount < ((bitCount + 7U) >> 3U) || !bits) {
        return false;
    }
    std::memset(bits, 0, byteCount);
    m_bitCount = bitCount;
    m_byteCount = byteCount;
    m_bits = bits;
    return true;
}

bool CVariableBitSet::SetBit(std::uint32_t bit) {
    if (!m_bits || m_bitCount == 0 || m_byteCount == 0 || bit >= m_bitCount) {
        return false;
    }
    m_bits[bit >> 3U] |= s_bitmaskArray[7U - (bit & 7U)];
    return true;
}

bool CVariableBitSet::SetAllBits() {
    if (!m_bits || m_bitCount == 0 || m_byteCount == 0) {
        return false;
    }
    std::memset(m_bits, 0xff, m_byteCount);
    return true;
}

bool CVariableBitSet::ClearBit(std::uint32_t bit) {
    if (!m_bits || m_bitCount == 0 || m_byteCount == 0 || bit >= m_bitCount) {
        return false;
    }
    m_bits[bit >> 3U] &= static_cast<std::uint8_t>(~s_bitmaskArray[7U - (bit & 7U)]);
    return true;
}

bool CVariableBitSet::Clear() {
    if (!m_bits || m_bitCount == 0 || m_byteCount == 0) {
        return false;
    }
    std::memset(m_bits, 0, m_byteCount);
    return true;
}

bool CVariableBitSet::IsSet(std::uint32_t bit) {
    if (!m_bits || m_bitCount == 0 || m_byteCount == 0 || bit >= m_bitCount) {
        return false;
    }
    return (m_bits[bit >> 3U] & s_bitmaskArray[7U - (bit & 7U)]) != 0;
}

bool CVariableBitSet::IsSet(
    std::uint32_t bit1,
    std::uint32_t bit2,
    std::uint32_t bit3,
    std::uint32_t bit4,
    std::uint32_t bit5,
    std::uint32_t bit6,
    std::uint32_t bit7,
    std::uint32_t bit8,
    std::uint32_t bit9,
    std::uint32_t bit10
) {
    if (!m_bits || m_bitCount == 0 || m_byteCount == 0) {
        return false;
    }
    const std::uint32_t bits[10]{bit1, bit2, bit3, bit4, bit5, bit6, bit7, bit8, bit9, bit10};
    for (const std::uint32_t bit : bits) {
        if (bit != UINT32_MAX && IsSet(bit)) {
            return true;
        }
    }
    return false;
}

namespace {

template <typename T>
bool SetValueIfModified(CVariableBitSet& bitSet, T& destination, T value, int bit) {
    if (destination == value) {
        return false;
    }
    destination = value;
    bitSet.SetBit(static_cast<std::uint32_t>(bit));
    return true;
}

} // namespace

bool CVariableBitSet::SetBoolValue_IfModifed(bool& destination, bool value, int bit) {
    return SetValueIfModified(*this, destination, value, bit);
}

bool CVariableBitSet::SetCharValue_IfModifed(char& destination, char value, int bit) {
    return SetValueIfModified(*this, destination, value, bit);
}

bool CVariableBitSet::SetUnsignedShortValue_IfModifed(std::uint16_t& destination, std::uint16_t value, int bit) {
    return SetValueIfModified(*this, destination, value, bit);
}

bool CVariableBitSet::SetShortValue_IfModifed(std::int16_t& destination, std::int16_t value, int bit) {
    return SetValueIfModified(*this, destination, value, bit);
}

bool CVariableBitSet::SetIntValue_IfModifed(std::int32_t& destination, std::int32_t value, int bit) {
    return SetValueIfModified(*this, destination, value, bit);
}

bool CVariableBitSet::SetUnsignedIntValue_IfModifed(std::uint32_t& destination, std::uint32_t value, int bit) {
    return SetValueIfModified(*this, destination, value, bit);
}

bool CVariableBitSet::SetInt64Value_IfModifed(std::int64_t& destination, std::int64_t value, int bit) {
    return SetValueIfModified(*this, destination, value, bit);
}

bool CVariableBitSet::SetDoubleValue_IfModifed(double& destination, double value, int bit) {
    const double difference = std::fabs(destination - value);
    if (!(9.999999747378752e-05 < difference)) {
        return false;
    }
    destination = value;
    SetBit(static_cast<std::uint32_t>(bit));
    return true;
}

bool CVariableBitSet::SetFloatValue_IfModifed(float& destination, float value, int bit) {
    const float difference = std::fabs(destination - value);
    if (!(0.0001F < difference)) {
        return false;
    }
    destination = value;
    SetBit(static_cast<std::uint32_t>(bit));
    return true;
}

bool CVariableBitSet::SetStringValue_IfModifed(
    char16_t* destination, std::uint64_t destinationBytes, const char16_t* value, int bit
) {
    if (!destination || destinationBytes == 0 || !value) {
        return false;
    }
    const std::size_t capacity = static_cast<std::size_t>(destinationBytes / sizeof(char16_t));
    std::size_t index = 0;
    while (index < capacity && destination[index] == value[index]) {
        if (value[index] == u'\0') {
            return false;
        }
        ++index;
    }
    if (index == capacity) {
        return false;
    }
    std::fill(destination, destination + capacity, u'\0');
    for (index = 0; index + 1 < capacity && value[index] != u'\0'; ++index) {
        destination[index] = value[index];
    }
    SetBit(static_cast<std::uint32_t>(bit));
    return true;
}

bool CVariableBitSet::SetFVectorValue_IfModifed(FVector& destination, const FVector& value, int bit) {
    if (destination == value) {
        return false;
    }
    destination = value;
    SetBit(static_cast<std::uint32_t>(bit));
    return true;
}

bool CVariableBitSet::SetFAngleValue_IfModifed(FAngle& destination, const FAngle& value, int bit) {
    return SetValueIfModified(*this, destination.Yaw, value.Yaw, bit);
}

CVariableBitSet& CVariableBitSet::operator|=(const CVariableBitSet& other) {
    if (!m_bits || m_bitCount == 0 || m_byteCount == 0 || !other.m_bits || other.m_bitCount == 0 || other.m_byteCount == 0 ||
        m_bitCount != other.m_bitCount || this == &other) {
        return *this;
    }
    for (std::uint32_t index = 0; index < m_byteCount; ++index) {
        m_bits[index] |= other.m_bits[index];
    }
    return *this;
}
