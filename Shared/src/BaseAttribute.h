#pragma once

#include "Protocol/src/L2Constants.h"

// Shared/BaseAttribute.h enum recovered from the PDB. These integer values
// are also used directly by the current executable.
// enum AttributeTypeEnum : int {
//     ATTRT_NONE = -2,
//     ATTRT_START = -1,
//     ATTRT_FIRE = 0,
//     ATTRT_WATER = 1,
//     ATTRT_WIND = 2,
//     ATTRT_EARTH = 3,
//     ATTRT_HOLY = 4,
//     ATTRT_UNHOLY = 5,
//     ATTRT_MAX = 6,
// }

class BaseAttribute {
public:
    BaseAttribute();
    BaseAttribute(short attackType, short attackValue, short* defendValues);
    BaseAttribute(short attackType, short attackValue);
    BaseAttribute(short* defendValues);
    BaseAttribute(const BaseAttribute& other);

    BaseAttribute& operator=(const BaseAttribute& other);
    bool operator==(const BaseAttribute& other) const;
    bool operator!=(const BaseAttribute& other) const;
    bool operator<(const BaseAttribute& other) const;
    bool IsAddable(const BaseAttribute& other);
    bool IsNullAttribute() const;
    BaseAttribute& operator+=(const BaseAttribute& other);
    BaseAttribute operator+(const BaseAttribute& other) const;

    void Init();
    int GetAttackLevel();
    int GetAttackPoint();
    int GetArmorLevel();
    int GetArmorPoint();
    bool IsEnchanted(AttributeTypeEnum attributeType);
    void GetLogString(char16_t* destination);
    void Remove(AttributeTypeEnum attributeType);

    static BaseAttribute& GetNullAttribute();

    short m_nAttackType;
    short m_nAttackValue;
    short m_nDefendValue[6];
    short m_nNoneAttributeValue;
};
