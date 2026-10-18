#include "TrainingZoneSetting.h"

#include "L2Core/src/inc/Guard.h"

#include <cstring>

CTrainingZoneSetting::CTrainingZoneSetting() {
    L2_GUARD_FUNCTION();

    std::memset(m_sCompleteHTML, 0, sizeof(m_sCompleteHTML));
    const ::FVector nullPosition{};
    m_vTrainingZonePos = nullPosition;
}

CTrainingZoneSetting::~CTrainingZoneSetting() = default;

CTrainingZoneSetting& CTrainingZoneSetting::Inst() {
    static CTrainingZoneSetting instance;
    return instance;
}
