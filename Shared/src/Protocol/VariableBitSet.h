#pragma once

#include "Shared/src/BaseStructure.h"

#include <cstddef>
#include <cstdint>


// Protocol/VariableBitSet.h. The carrier points at storage owned by its
// enclosing protocol/shared record. It never allocates or frees that storage.
class CVariableBitSet {
public:
    static constexpr std::uint32_t MAX_BITS = 256;

    CVariableBitSet();
    ~CVariableBitSet();

    static void InitBitmaskArray();

    bool Init(std::uint32_t bitCount, std::uint32_t byteCount, std::uint8_t* bits);
    bool SetBit(std::uint32_t bit);
    bool SetAllBits();
    bool ClearBit(std::uint32_t bit);
    bool Clear();
    bool IsSet(std::uint32_t bit);
    bool IsSet(
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
    );

    std::uint32_t GetBitCount() { return m_bitCount; }
    std::uint32_t GetByteCount() { return m_byteCount; }
    std::uint8_t* GetBits() { return m_bits; }

    bool SetBoolValue_IfModifed(bool& destination, bool value, int bit);
    bool SetCharValue_IfModifed(char& destination, char value, int bit);
    bool SetUnsignedShortValue_IfModifed(std::uint16_t& destination, std::uint16_t value, int bit);
    bool SetShortValue_IfModifed(std::int16_t& destination, std::int16_t value, int bit);
    bool SetIntValue_IfModifed(std::int32_t& destination, std::int32_t value, int bit);
    bool SetUnsignedIntValue_IfModifed(std::uint32_t& destination, std::uint32_t value, int bit);
    bool SetInt64Value_IfModifed(std::int64_t& destination, std::int64_t value, int bit);
    bool SetDoubleValue_IfModifed(double& destination, double value, int bit);
    bool SetFloatValue_IfModifed(float& destination, float value, int bit);
    bool SetStringValue_IfModifed(char16_t* destination, std::uint64_t destinationBytes, const char16_t* value, int bit);
    bool SetFVectorValue_IfModifed(FVector& destination, const FVector& value, int bit);
    bool SetFAngleValue_IfModifed(FAngle& destination, const FAngle& value, int bit);

    std::uint32_t GetByteOffset(std::uint32_t bit) { return bit >> 3; }
    std::uint32_t GetBitOffset(std::uint32_t bit) { return bit & 7U; }

    CVariableBitSet& operator|=(const CVariableBitSet& other);

    std::uint8_t* m_bits;
    std::uint32_t m_bitCount;
    std::uint32_t m_byteCount;

private:
    static std::uint8_t s_bitmaskArray[8];
    static bool s_isBitmaskArrayInited;
};
