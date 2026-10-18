#include "DelayedCreator.h"

#include "NpcMakerCommon.h"
#include "Shared/src/TemplateHelper.h"

namespace l2npc {

DelayedCreator::DelayedCreator(
    BaseSpawnDefine* define,
    const char16_t* name,
    int count,
    int x,
    int y,
    int z,
    int direction,
    int hp,
    int mp,
    int dbValue,
    std::uint32_t inZoneId,
    int makerParam1,
    int makerParam2,
    int makerParam3
) :
    mpDefine(define),
    mName(name),
    mCount(count),
    mX(x),
    mY(y),
    mZ(z),
    mDir(direction),
    mHP(hp),
    mMP(mp),
    mDBValue(dbValue),
    m_nInZoneID(inZoneId),
    m_nMakerParam1(makerParam1),
    m_nMakerParam2(makerParam2),
    m_nMakerParam3(makerParam3) {
    if (mpDefine) {
        L2_ADD_REF(mpDefine, ::ObjectRefType::NpcRelated);
    }
}

DelayedCreator::~DelayedCreator() {
    if (mpDefine) {
        L2_RELEASE(mpDefine, ::ObjectRefType::NpcRelated);
    }
}

void DelayedCreator::TimerExpired(int) {
    for (int index = 0; index < mCount; ++index) {
        SendCreateNpcPacket(
            mName.c_str(), mpDefine, 0, mX, mY, mZ, mDir, mHP, mMP, mDBValue, m_nInZoneID, 0, '\0', 0, m_nMakerParam1,
            m_nMakerParam2, m_nMakerParam3, '\0', 0, 0, '\0', 0, 0, 0
        );
    }

    // CIOObject's independent ORT_TIMER reference keeps `this` alive until
    // the timer callback epilogue returns.
    L2_RELEASE(this, ::ObjectRefType::CreateDelete);
}

} // namespace l2npc
