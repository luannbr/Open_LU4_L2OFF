#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

class CSharedRWLock;

extern CSharedRWLock g_ManorLock;

// Shared/ManorData.h layout from the VS2015 type record, independently
// confirmed by every current-target CNPC manor getter and by the 0x1658
// CManorData::Inst stride in L2NPC.exe.
struct CManorData {
    static CManorData& Inst(int index);

    std::uint8_t m_byChangeState = 0; // +0x0000
    std::array<char16_t, 41> m_wsManorName{}; // +0x0002
    int m_nManorId = 0; // +0x0054
    int m_nResidenceID = 0; // +0x0058
    int m_nManorOwnerID = 0; // +0x005c
    int m_nManorOwnerIndex = 0; // +0x0060
    std::int64_t m_nSeedSellBank = 0; // +0x0068
    std::int64_t m_nProcureBuyRemainBank = 0; // +0x0070
    std::array<int, 40> m_nSeedIdList{}; // +0x0078
    std::array<int, 40> m_nSeedLevel{}; // +0x0118
    std::array<int, 40> m_nCropIdList{}; // +0x01b8
    std::array<int, 40> m_nSeedMaxCount{}; // +0x0258
    std::array<int, 40> m_nCropMaxCount{}; // +0x02f8
    std::array<int, 40> m_nSeedPriceDefault{}; // +0x0398
    std::array<int, 40> m_nCropPriceDefault{}; // +0x0438
    std::array<int, 40> m_nSeedList{}; // +0x04d8
    std::array<std::int64_t, 40> m_nSeedPrice{}; // +0x0578
    std::array<std::int64_t, 40> m_nSeedSetCount{}; // +0x06b8
    std::array<std::int64_t, 40> m_nSeedRemainCount{}; // +0x07f8
    std::array<int, 40> m_nProcuringCropList{}; // +0x0938
    std::array<std::int64_t, 40> m_nProcuringCropSetCount{}; // +0x09d8
    std::array<std::int64_t, 40> m_nProcuringCropCount{}; // +0x0b18
    std::array<std::int64_t, 40> m_nProcuringCropPrice{}; // +0x0c58
    std::array<int, 40> m_nProcuringCropType{}; // +0x0d98
    std::array<std::int64_t, 40> m_nProcuredCropList{}; // +0x0e38
    std::array<int, 40> m_nSeedList_n{}; // +0x0f78
    std::array<std::int64_t, 40> m_nSeedPrice_n{}; // +0x1018
    std::array<std::int64_t, 40> m_nSeedSetCount_n{}; // +0x1158
    std::array<int, 40> m_nProcuringCropList_n{}; // +0x1298
    std::array<std::int64_t, 40> m_nProcuringCropCount_n{}; // +0x1338
    std::array<std::int64_t, 40> m_nProcuringCropPrice_n{}; // +0x1478
    std::array<int, 40> m_nProcuringCropType_n{}; // +0x15b8
};
