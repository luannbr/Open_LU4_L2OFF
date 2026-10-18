#pragma once

#include "QuestionAnswer.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

using NewInspectorUnsignedLong =
    std::conditional_t<sizeof(unsigned long) == sizeof(std::uint32_t), unsigned long, std::uint32_t>;

class CNewInspectorDB {
public:
    void Load();
    RGBA GetAnswer(const Query& query) const;
    NewInspectorUnsignedLong GetCRC() const { return m_crc; }

private:
    RGBA m_qa[15][128][64]; // +0x000000..+0x1dffff
    NewInspectorUnsignedLong m_crc; // +0x1e0000
};

extern CNewInspectorDB g_NewInspectorDB;
