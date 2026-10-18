#pragma once

#include "Shared/src/BaseStructure.h"

#include <algorithm>
#include <cstddef>
#include <string>

// Complete process-local training-zone setting from the current L2Server.
// Target wchar_t is a two-byte UTF-16 code unit, represented by char16_t in
// the portable build.
class CTrainingZoneSetting {
public:
    static constexpr std::size_t kCompleteHtmlCapacity = 0x100;

    CTrainingZoneSetting();
    ~CTrainingZoneSetting();

    static CTrainingZoneSetting& Inst();

    void SetCompleteHTML(const char16_t* completeHtml) {
        const std::size_t length = std::char_traits<char16_t>::length(completeHtml);
        if (length >= kCompleteHtmlCapacity) {
            m_sCompleteHTML[0] = u'\0';
            return;
        }

        std::copy_n(completeHtml, length + 1, m_sCompleteHTML);
    }

    void SetTrainingZonePos(int x, int y, int z) {
        const ::FVector position{x, y, z};
        m_vTrainingZonePos = position;
    }

    const char16_t* GetCompleteHTML() { return m_sCompleteHTML; }

    const ::FVector GetTrainingZonePos() { return m_vTrainingZonePos; }


private:
    char16_t m_sCompleteHTML[kCompleteHtmlCapacity]; // +0x000
    ::FVector m_vTrainingZonePos; // +0x200
};
