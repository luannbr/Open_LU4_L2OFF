#pragma once

#include "Shared/src/Obj.h"

#include <cstddef>
#include <cstdint>

class CCreature;
class User;

class CEconomy : public MemoryObject {
public:
    CEconomy() noexcept : nNPCServerId(0), nNPCClassId(0), nNPCSId(0), m_dwShortTimeStamp(0) {}

    ~CEconomy() override = default;

    virtual bool IsWarehouse() { return false; }

    bool SetCreatureData(CCreature* creature);

protected:
    bool SetShortTimeStamp(std::uint32_t* timeStamp, std::uint32_t delay);
    bool IsInShortTime(std::uint32_t timeStamp, std::uint32_t delay);
    friend class User;

    std::int32_t nNPCServerId; // +0x18
    std::int32_t nNPCClassId; // +0x1c
    std::int32_t nNPCSId; // +0x20
    std::uint32_t m_dwShortTimeStamp; // +0x24

private:
    L2_DECLARE_OBJECT_REGISTRATION(CEconomy)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CEconomy)
};
