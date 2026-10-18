#pragma once

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CDecoNpcDB;

class CDecoNpcData {
public:
    struct CostItem {
        int nClassID; // +0x00
        int nCount; // +0x04
    };

    CDecoNpcData();

    int GetFirstCostItemCID() const;
    int GetFirstCostItemCount() const;
    bool HasResidenceGrade(int nResidenceGrade) const;
    bool HasResidenceDomain(int nResidenceDomain) const;

    int m_nCID; // +0x000
    char16_t m_szName[256]; // +0x004
    int m_nType; // +0x204
    int m_nSubType; // +0x208
    int m_nFactionType; // +0x20c
    int m_nLevel; // +0x210
    int m_nNpcCID; // +0x214
    int m_nPeriod; // +0x218
    std::int64_t m_nCostAdena; // +0x220
    std::vector<CostItem,
        std::allocator<CostItem>> m_vCostItems; // +0x228
    std::vector<int,
        std::allocator<int>> m_residenceGrades; // +0x248
    std::vector<int,
        std::allocator<int>> m_residenceDomains; // +0x268
    bool m_isChangeable; // +0x288

private:
    friend class CDecoNpcDB;

    ~CDecoNpcData();
};

class CDecoNpcDB {
public:
    struct PrivateAddDecoNpc {
        static void Call(CDecoNpcData* data);
    };

    static CDecoNpcDB& Inst();

    const CDecoNpcData* FindDecoNpc(unsigned int cid) const;
    void AddDecoNpc(CDecoNpcData* data);
    void Load();

private:
    CDecoNpcDB();
    ~CDecoNpcDB();

    std::map<std::u16string, CDecoNpcData*, runtime::CStringLess<std::u16string>> m_nameToData; // +0x00
    std::map<unsigned int, CDecoNpcData*> m_idToData; // +0x20
};

namespace DecoNpcUtil {

bool IsRightSlot(const CDecoNpcData* pData, int nSlot);

} // namespace DecoNpcUtil
