#include "Shared/src/SharedFactory.h"
#include "Shared/src/Runtime/SourceFile.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TextRuntime.h"

#if defined(L2_BUILD_L2SERVER)

#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Log.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iterator>
#include <string>

namespace {

constexpr std::int32_t kUltimateSkillLevelInfo[4][2] = {
    {0, 84},
    {85, 89},
    {90, 94},
    {95, 99},
};

constexpr std::int32_t kUltimateSkillGaugeLimit[4] = {
    0,
    500,
    1000,
    1500,
};

// std::wstring SharedCreatureNameForLog(
//     const char16_t* value) {
//     std::wstring result
//     if (value == nullptr) return result.
//     while (*value != u'\0') {
//         std::uint32_t codePoint = *value++
//         if constexpr (sizeof(wchar_t) == 2) {
//             result.push_back(static_cast<wchar_t>(codePoint))
//             continue.
//         }
//         if (codePoint >= 0xd800U &&
//             codePoint <= 0xdbffU &&
//             *value >= 0xdc00U &&
//             *value <= 0xdfffU) {
//             codePoint =
//                 0x10000U +
//                 ((codePoint - 0xd800U) << 10U) +
//                 (static_cast<std::uint32_t>(*value++) -
//                  0xdc00U)
//         }
//         result.push_back(static_cast<wchar_t>(codePoint))
//     }
//     return result
// }
// const void* DoubleBitsAsPointer(double value) {
//     std::uint64_t bits = 0
//     std::memcpy(&bits, &value, sizeof(bits))
//     return reinterpret_cast<const void*>(
//         static_cast<std::uintptr_t>(bits))
// }
std::uint64_t DoubleBitsForLog(double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

} // namespace

CStats::CStats() { Init(); }

CStats::CStats(const CStats& other) { std::copy(std::begin(other.m_N), std::end(other.m_N), std::begin(m_N)); }

CStats& CStats::operator=(const CStats& other) {
    if (this != &other) {
        std::copy(std::begin(other.m_N), std::end(other.m_N), std::begin(m_N));
    }
    return *this;
}

CStats CStats::operator-(const CStats& other) const {
    CStats result;
    for (std::size_t index = 0; index < std::size(m_N); ++index) {
        result.m_N[index] =
            static_cast<std::int32_t>(static_cast<std::uint32_t>(m_N[index]) - static_cast<std::uint32_t>(other.m_N[index]));
    }
    return result;
}

CStats CStats::operator+(const CStats& other) const {
    CStats result;
    for (std::size_t index = 0; index < std::size(m_N); ++index) {
        result.m_N[index] =
            static_cast<std::int32_t>(static_cast<std::uint32_t>(m_N[index]) + static_cast<std::uint32_t>(other.m_N[index]));
    }
    return result;
}

CStats& CStats::operator+=(const CStats& other) {
    for (std::size_t index = 0; index < std::size(m_N); ++index) {
        m_N[index] =
            static_cast<std::int32_t>(static_cast<std::uint32_t>(m_N[index]) + static_cast<std::uint32_t>(other.m_N[index]));
    }
    return *this;
}

bool CStats::IsSameValue(const CStats& other) {
    // Current L2Server.exe CSharedCreatureData::SetStats at 0x00a52a48
    // compares 0x20 consecutive int32 values here even though CStats owns
    // only eight. This is shipped undefined behavior (most likely the
    // original source used sizeof(m_N) as an element count). Retain the
    // exact defective target loop disabled for recovery auditability.
#if 0
    const std::int32_t* targetLeft = m_N;
    const std::int32_t* targetRight = other.m_N;
    for (int targetIndex = 0; targetIndex < 32; ++targetIndex) {
        if (*targetLeft++ != *targetRight++) return false;
    }
#endif
    for (std::size_t index = 0; index < std::size(m_N); ++index) {
        if (m_N[index] != other.m_N[index]) {
            return false;
        }
    }
    return true;
}

void CStats::Fix() {
    for (std::int32_t& value : m_N) {
        value = std::clamp(value, 0, 200);
    }
}

void CStats::GuaranteeMax(int maximum) {
    for (std::int32_t& value : m_N) {
        value = std::max(value, maximum);
    }
}

void CStats::Init() { std::fill(std::begin(m_N), std::end(m_N), 0); }

bool CStats::IsValid() const {
    return std::all_of(std::begin(m_N), std::end(m_N), [](std::int32_t value) { return value >= 0 && value <= 200; });
}

void CStats::Report(wchar_t* sourceFile, int sourceLine, wchar_t* creatureName) const {
    if (sourceFile == nullptr || creatureName == nullptr) {
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] CreatureName:%s INT(%d) STR(%d) "
    //     L"CON(%d) MEN(%d) DEX(%d) WIT(%d)",
    //     sourceFile,
    //     sourceLine,
    //     creatureName,
    //     m_N[0],
    //     m_N[1],
    //     m_N[2],
    //     m_N[3],
    //     m_N[4],
    //     m_N[5])
    // Log.Add(
    //     LOG_ERROR,
    //     L"[stats] Report from \"%ls\", line %d, for \"%ls\": INT %d, STR %d, CON %d, MEN %d, DEX %d, WIT %d.",
    //     sourceFile, sourceLine, creatureName,
    //     m_N[0], m_N[1], m_N[2], m_N[3], m_N[4], m_N[5])
    L2_LOG_ERROR(
        Log, u"[stats] Report from \"%ls\", line %d, for \"%ls\": INT %d, STR %d, CON %d, MEN %d, DEX %d, WIT %d.",
        sourceFile, sourceLine, creatureName, m_N[0], m_N[1], m_N[2], m_N[3], m_N[4], m_N[5]
    );
}

FVector CSharedObject::GetPos() const { return m_Pos; }

void CSharedObject::SetPos(const FVector& position) { m_Pos = position; }

FAngle CSharedObject::GetDir() const { return m_Dir; }

int CSharedObject::GetDirByInt() const { return m_Dir.Yaw; }

void CSharedObject::SetDirByInt(int direction) {
    FAngle angle;
    angle.Yaw = direction;
    SetDir(angle);
}

void CSharedObject::SetDir(const FAngle& direction) { m_Dir = direction; }

void CSharedCreatureData::SetName(const char16_t* value) {
    L2_GUARD_FUNCTION();
    m_NeedUpdatePacketBitSet.SetStringValue_IfModifed(m_sName, sizeof(m_sName), value, 0);
    m_sName[24] = u'\0';
}

void CSharedCreatureData::SetRace(const RaceType value) {
    // Current L2Server inlines this exact field/update-bit contract at
    // CCreature::ChangeRace 0x005f12cc..0x005f12ea.
    if (m_Race == value) {
        return;
    }
    m_Race = value;
    m_NeedUpdatePacketBitSet.SetBit(1);
}

void CSharedCreatureData::SetSex(const SexType value) {
    // Current CSParamSettingPacket applies the adjacent sex lane and bit 2.
    if (m_Sex == value) {
        return;
    }
    m_Sex = value;
    m_NeedUpdatePacketBitSet.SetBit(2);
}

void CSharedCreatureData::SetClass(const ClassType value) {
    // Current CCreature::ChangeRace applies field +0x94 and update bit 3.
    if (m_nClass == value) {
        return;
    }
    m_nClass = value;
    m_NeedUpdatePacketBitSet.SetBit(3);
}

void CSharedCreatureData::SetPledgeID(std::uint32_t value) {
    m_NeedUpdatePacketBitSet.SetUnsignedIntValue_IfModifed(m_nPledgeID, value, 46);
}

void CSharedCreatureData::SetAllianceID(std::uint32_t value) {
    m_NeedUpdatePacketBitSet.SetUnsignedIntValue_IfModifed(m_nAllianceId, value, 47);
}

void CSharedCreatureData::SetPKCount(int value) {
    // Current L2Server inlines this exact owner in
    // User::DieCursedUser: field +0x2ec, update bit 30.
    m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nPKCount, value, 30);
}

void CSharedCreatureData::SetAttackType(AttackType value) {
    if (m_AttackType == value) {
        return;
    }
    m_AttackType = value;
    m_NeedUpdatePacketBitSet.SetBit(36);
}

void CSharedCreatureData::SetAttackRange(int value) {
    m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nAttackRange, value, 37);
}

void CSharedCreatureData::SetGuility(char value) { m_NeedUpdatePacketBitSet.SetCharValue_IfModifed(m_cIsGuilty, value, 38); }

bool CSharedCreatureData::AddAbnormalStatus(AbnormalStatusSlotType type, std::uint8_t level, int magicLevel) {
    L2_GUARD_FUNCTION();
    for (AbnormalData& abnormal : m_AbnormalStatus) {
        if (static_cast<int>(abnormal.m_Type) != -1) {
            continue;
        }
        abnormal.m_Type = type;
        abnormal.m_cAbnormalLevel = level;
        abnormal.m_nMagicLevel = magicLevel;
        return true;
    }
    return false;
}

bool CSharedCreatureData::DeleteAbnormalStatus(AbnormalStatusSlotType type) {
    L2_GUARD_FUNCTION();
    for (AbnormalData& abnormal : m_AbnormalStatus) {
        if (abnormal.m_Type != type) {
            continue;
        }
        abnormal.m_Type = static_cast<AbnormalStatusSlotType>(-1);
        return true;
    }
    return false;
}

bool CSharedCreatureData::SetAbnormalLevel(AbnormalStatusSlotType type, std::uint8_t level) {
    // Current L2Server.exe 0x00a51ad4. SharedFactory.cpp is built as a
    // distinct product TU, so the L2Server branch must own this body as well
    // as the L2NPC branch below.
    L2_GUARD_FUNCTION();
    for (AbnormalData& abnormal : m_AbnormalStatus) {
        if (abnormal.m_Type != type) {
            continue;
        }
        abnormal.m_cAbnormalLevel = level;
        return true;
    }
    m_NeedUpdatePacketBitSet.SetBit(75);
    return false;
}

void CSharedCreatureData::DeleteAllAbnormalStatus() {
    L2_GUARD_FUNCTION();
    for (AbnormalData& abnormal : m_AbnormalStatus) {
        abnormal.m_Type = static_cast<AbnormalStatusSlotType>(-1);
    }
}

bool CSharedCreatureData::IsAbnormalType(AbnormalStatusSlotType type) {
    // Current L2Server.exe 0x00a51bc4. SharedFactory.cpp is compiled as a
    // separate product TU, so this body must live in the L2Server branch as
    // well as in the L2NPC branch below.
    L2_GUARD_FUNCTION();
    return std::any_of(std::begin(m_AbnormalStatus), std::end(m_AbnormalStatus), [type](const AbnormalData& abnormal) {
        return abnormal.m_Type == type;
    });
}

void CSharedCreatureData::CopyFrom(CSharedCreatureData* source) {
    std::memcpy(
        &m_nCreatureStart, &source->m_nCreatureStart,
        offsetof(CSharedCreatureData, m_nCreatureEnd) - offsetof(CSharedCreatureData, m_nCreatureStart)
    );
    m_NeedUpdatePacketBitSet.Init(
        103, sizeof(m_bNeedUpdatePacketBits), reinterpret_cast<std::uint8_t*>(m_bNeedUpdatePacketBits)
    );
    m_NeedUpdatePacketBitSet.SetAllBits();
}

void CSharedCreatureData::SetHP(double value) {
    const std::wstring name = Utf16ToWide(m_sName);
    if (!FloatChecker::CheckValidation(value)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"FloatERR [%s] inval HP input:%.5f(%p) "
        //     L"fpclass:%04x base:%.5f(%p) now:%.5f(%p)",
        //     name.c_str(),
        //     value,
        //     DoubleBitsAsPointer(value),
        //     FloatChecker::FPClass(value),
        //     m_dBaseHP,
        //     DoubleBitsAsPointer(m_dBaseHP),
        //     m_dHP,
        //     DoubleBitsAsPointer(m_dHP))
        // Log.Add(
        //     LOG_ERROR,
        //     L"[vitals] HP validation failed. Will use the base value if finite, then apply bounds. Creature \"%ls\". Input
        //     %.17g (bits 0x%016llx, class 0x%04x), base %.17g (bits 0x%016llx), current %.17g (bits 0x%016llx).",
        //     name.c_str(), value,
        //     static_cast<unsigned long long>(DoubleBitsForLog(value)),
        //     static_cast<unsigned>(FloatChecker::FPClass(value)),
        //     m_dBaseHP, static_cast<unsigned long long>(DoubleBitsForLog(m_dBaseHP)),
        //     m_dHP, static_cast<unsigned long long>(DoubleBitsForLog(m_dHP)))
        L2_LOG_ERROR(
            Log,
            u"[vitals] HP validation failed. Will use the base value if finite, then apply bounds. Creature \"%ls\"; input "
            u"%.17g (bits 0x%016llx, class 0x%04x), base %.17g (bits 0x%016llx), current %.17g (bits 0x%016llx).",
            name.c_str(), value, static_cast<unsigned long long>(DoubleBitsForLog(value)),
            static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseHP,
            static_cast<unsigned long long>(DoubleBitsForLog(m_dBaseHP)), m_dHP,
            static_cast<unsigned long long>(DoubleBitsForLog(m_dHP))
        );
        runtime::process::ReportCallStack(LOG_ERROR, false);
        if (FloatChecker::IsValid(m_dBaseHP)) {
            value = m_dBaseHP;
        }
    }
    if (value < 0.0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"FloatERR [%s] minus HP input:%.5f(%p) "
        //     L"fpclass:%04x base:%.5f(%p) now:%.5f(%p)",
        //     name.c_str(),
        //     value,
        //     DoubleBitsAsPointer(value),
        //     FloatChecker::FPClass(value),
        //     m_dBaseHP,
        //     DoubleBitsAsPointer(m_dBaseHP),
        //     m_dHP,
        //     DoubleBitsAsPointer(m_dHP))
        // Log.Add(
        //     LOG_ERROR,
        //     L"[vitals] Negative HP input. Clamping to zero. Creature \"%ls\". Input %.17g (bits 0x%016llx, class 0x%04x),
        //     base %.17g (bits 0x%016llx), current %.17g (bits 0x%016llx).", name.c_str(), value, static_cast<unsigned long
        //     long>(DoubleBitsForLog(value)), static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseHP,
        //     static_cast<unsigned long long>(DoubleBitsForLog(m_dBaseHP)), m_dHP, static_cast<unsigned long
        //     long>(DoubleBitsForLog(m_dHP)))
        L2_LOG_ERROR(
            Log,
            u"[vitals] Negative HP input. Clamping to zero. Creature \"%ls\"; input %.17g (bits 0x%016llx, class 0x%04x), "
            u"base %.17g (bits 0x%016llx), current %.17g (bits 0x%016llx).",
            name.c_str(), value, static_cast<unsigned long long>(DoubleBitsForLog(value)),
            static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseHP,
            static_cast<unsigned long long>(DoubleBitsForLog(m_dBaseHP)), m_dHP,
            static_cast<unsigned long long>(DoubleBitsForLog(m_dHP))
        );
        runtime::process::ReportCallStack(LOG_ERROR, false);
        value = 0.0;
    }
    if (m_dBaseHP < value && 0.0 < m_dBaseHP) {
        if (FloatChecker::FPClass(value) != 0x0100) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"FloatERR [%s] huge HP input:%.5f(%p) "
            //     L"fpclass:%04x base:%.5f(%p) now:%.5f(%p)",
            //     name.c_str(),
            //     value,
            //     DoubleBitsAsPointer(value),
            //     FloatChecker::FPClass(value),
            //     m_dBaseHP,
            //     DoubleBitsAsPointer(m_dBaseHP),
            //     m_dHP,
            //     DoubleBitsAsPointer(m_dHP))
            // Log.Add(
            //     LOG_ERROR,
            //     L"[vitals] HP input exceeds its positive base. Clamping to the base value. Creature \"%ls\". Input %.17g
            //     (bits 0x%016llx, class 0x%04x), base %.17g (bits 0x%016llx), current %.17g (bits 0x%016llx).",
            //     name.c_str(), value,
            //     static_cast<unsigned long long>(DoubleBitsForLog(value)),
            //     static_cast<unsigned>(FloatChecker::FPClass(value)),
            //     m_dBaseHP, static_cast<unsigned long long>(DoubleBitsForLog(m_dBaseHP)),
            //     m_dHP, static_cast<unsigned long long>(DoubleBitsForLog(m_dHP)))
            L2_LOG_ERROR(
                Log,
                u"[vitals] HP input exceeds its positive base. Clamping to the base value. Creature \"%ls\"; input %.17g "
                u"(bits 0x%016llx, class 0x%04x), base %.17g (bits 0x%016llx), current %.17g (bits 0x%016llx).",
                name.c_str(), value, static_cast<unsigned long long>(DoubleBitsForLog(value)),
                static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseHP,
                static_cast<unsigned long long>(DoubleBitsForLog(m_dBaseHP)), m_dHP,
                static_cast<unsigned long long>(DoubleBitsForLog(m_dHP))
            );
            runtime::process::ReportCallStack(LOG_ERROR, false);
        }
        value = m_dBaseHP;
    }
    m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_dHP, value, 11);
}

void CSharedCreatureData::SetMP(double value) {
    const std::wstring name = Utf16ToWide(m_sName);
    if (!FloatChecker::CheckValidation(value)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"FloatERR [%s] inval MP input:%.5f(%p) "
        //     L"fpclass:%04x base:%.5f(%p) now:%.5f(%p)",
        //     name.c_str(),
        //     value,
        //     DoubleBitsAsPointer(value),
        //     FloatChecker::FPClass(value),
        //     m_dBaseMP,
        //     DoubleBitsAsPointer(m_dBaseMP),
        //     m_dMP,
        //     DoubleBitsAsPointer(m_dMP))
        // Log.Add(
        //     LOG_ERROR,
        //     L"[vitals] MP validation failed. Will use the base value if finite, then apply bounds. Creature \"%ls\". Input
        //     %.17g (bits 0x%016llx, class 0x%04x), base %.17g (bits 0x%016llx), current %.17g (bits 0x%016llx).",
        //     name.c_str(), value,
        //     static_cast<unsigned long long>(DoubleBitsForLog(value)),
        //     static_cast<unsigned>(FloatChecker::FPClass(value)),
        //     m_dBaseMP, static_cast<unsigned long long>(DoubleBitsForLog(m_dBaseMP)),
        //     m_dMP, static_cast<unsigned long long>(DoubleBitsForLog(m_dMP)))
        L2_LOG_ERROR(
            Log,
            u"[vitals] MP validation failed. Will use the base value if finite, then apply bounds. Creature \"%ls\"; input "
            u"%.17g (bits 0x%016llx, class 0x%04x), base %.17g (bits 0x%016llx), current %.17g (bits 0x%016llx).",
            name.c_str(), value, static_cast<unsigned long long>(DoubleBitsForLog(value)),
            static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseMP,
            static_cast<unsigned long long>(DoubleBitsForLog(m_dBaseMP)), m_dMP,
            static_cast<unsigned long long>(DoubleBitsForLog(m_dMP))
        );
        runtime::process::ReportCallStack(LOG_ERROR, false);
        if (FloatChecker::IsValid(m_dBaseMP)) {
            value = m_dBaseMP;
        }
    }
    if (value < 0.0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"FloatERR [%s] minus MP input:%.5f(%p) "
        //     L"fpclass:%04x base:%.5f(%p) now:%.5f(%p)",
        //     name.c_str(),
        //     value,
        //     DoubleBitsAsPointer(value),
        //     FloatChecker::FPClass(value),
        //     m_dBaseMP,
        //     DoubleBitsAsPointer(m_dBaseMP),
        //     m_dMP,
        //     DoubleBitsAsPointer(m_dMP))
        // Log.Add(
        //     LOG_ERROR,
        //     L"[vitals] Negative MP input. Clamping to zero. Creature \"%ls\". Input %.17g (bits 0x%016llx, class 0x%04x),
        //     base %.17g (bits 0x%016llx), current %.17g (bits 0x%016llx).", name.c_str(), value, static_cast<unsigned long
        //     long>(DoubleBitsForLog(value)), static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseMP,
        //     static_cast<unsigned long long>(DoubleBitsForLog(m_dBaseMP)), m_dMP, static_cast<unsigned long
        //     long>(DoubleBitsForLog(m_dMP)))
        L2_LOG_ERROR(
            Log,
            u"[vitals] Negative MP input. Clamping to zero. Creature \"%ls\"; input %.17g (bits 0x%016llx, class 0x%04x), "
            u"base %.17g (bits 0x%016llx), current %.17g (bits 0x%016llx).",
            name.c_str(), value, static_cast<unsigned long long>(DoubleBitsForLog(value)),
            static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseMP,
            static_cast<unsigned long long>(DoubleBitsForLog(m_dBaseMP)), m_dMP,
            static_cast<unsigned long long>(DoubleBitsForLog(m_dMP))
        );
        runtime::process::ReportCallStack(LOG_ERROR, false);
        value = 0.0;
    }
    if (m_dBaseMP < value && 0.0 < m_dBaseMP) {
        if (FloatChecker::FPClass(value) != 0x0100) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"FloatERR [%s] huge MP input:%.5f(%p) "
            //     L"fpclass:%04x base:%.5f(%p) now:%.5f(%p)",
            //     name.c_str(),
            //     value,
            //     DoubleBitsAsPointer(value),
            //     FloatChecker::FPClass(value),
            //     m_dBaseMP,
            //     DoubleBitsAsPointer(m_dBaseMP),
            //     m_dMP,
            //     DoubleBitsAsPointer(m_dMP))
            // Log.Add(
            //     LOG_ERROR,
            //     L"[vitals] MP input exceeds its positive base. Clamping to the base value. Creature \"%ls\". Input %.17g
            //     (bits 0x%016llx, class 0x%04x), base %.17g (bits 0x%016llx), current %.17g (bits 0x%016llx).",
            //     name.c_str(), value,
            //     static_cast<unsigned long long>(DoubleBitsForLog(value)),
            //     static_cast<unsigned>(FloatChecker::FPClass(value)),
            //     m_dBaseMP, static_cast<unsigned long long>(DoubleBitsForLog(m_dBaseMP)),
            //     m_dMP, static_cast<unsigned long long>(DoubleBitsForLog(m_dMP)))
            L2_LOG_ERROR(
                Log,
                u"[vitals] MP input exceeds its positive base. Clamping to the base value. Creature \"%ls\"; input %.17g "
                u"(bits 0x%016llx, class 0x%04x), base %.17g (bits 0x%016llx), current %.17g (bits 0x%016llx).",
                name.c_str(), value, static_cast<unsigned long long>(DoubleBitsForLog(value)),
                static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseMP,
                static_cast<unsigned long long>(DoubleBitsForLog(m_dBaseMP)), m_dMP,
                static_cast<unsigned long long>(DoubleBitsForLog(m_dMP))
            );
            runtime::process::ReportCallStack(LOG_ERROR, false);
        }
        value = m_dBaseMP;
    }
    m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_dMP, value, 12);
}

void CSharedCreatureData::ValidateMP(double value) {
    L2_GUARD_FUNCTION();
    if (m_dMP < value) {
        SetMP(value);
    } else if (m_dBaseMP < m_dMP) {
        SetMP(m_dBaseMP);
    }
}

double CSharedCreatureData::GetCP() const { return m_dCP; }

void CSharedCreatureData::SetCP(double value) {
    const std::wstring name = Utf16ToWide(m_sName);
    if (!FloatChecker::CheckValidation(value)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"FloatERR [%s] inval CP input:%.5f(%p) "
        //     L"fpclass:%04x base:%.5f(%p) now:%.5f(%p)",
        //     name.c_str(),
        //     value,
        //     DoubleBitsAsPointer(value),
        //     FloatChecker::FPClass(value),
        //     m_dBaseCP,
        //     DoubleBitsAsPointer(m_dBaseCP),
        //     m_dCP,
        //     DoubleBitsAsPointer(m_dCP))
        // Log.Add(
        //     LOG_ERROR,
        //     L"[vitals] CP validation failed. Will use the base value if finite, then apply bounds. Creature \"%ls\". Input
        //     %.17g (bits 0x%016llx, class 0x%04x), base %.17g (bits 0x%016llx), current %.17g (bits 0x%016llx).",
        //     name.c_str(), value,
        //     static_cast<unsigned long long>(DoubleBitsForLog(value)),
        //     static_cast<unsigned>(FloatChecker::FPClass(value)),
        //     m_dBaseCP, static_cast<unsigned long long>(DoubleBitsForLog(m_dBaseCP)),
        //     m_dCP, static_cast<unsigned long long>(DoubleBitsForLog(m_dCP)))
        L2_LOG_ERROR(
            Log,
            u"[vitals] CP validation failed. Will use the base value if finite, then apply bounds. Creature \"%ls\"; input "
            u"%.17g (bits 0x%016llx, class 0x%04x), base %.17g (bits 0x%016llx), current %.17g (bits 0x%016llx).",
            name.c_str(), value, static_cast<unsigned long long>(DoubleBitsForLog(value)),
            static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseCP,
            static_cast<unsigned long long>(DoubleBitsForLog(m_dBaseCP)), m_dCP,
            static_cast<unsigned long long>(DoubleBitsForLog(m_dCP))
        );
        runtime::process::ReportCallStack(LOG_ERROR, false);
        if (FloatChecker::IsValid(m_dBaseCP)) {
            value = m_dBaseCP;
        }
    }
    if (value < 0.0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"FloatERR [%s] minus CP input:%.5f(%p) "
        //     L"fpclass:%04x base:%.5f(%p) now:%.5f(%p)",
        //     name.c_str(),
        //     value,
        //     DoubleBitsAsPointer(value),
        //     FloatChecker::FPClass(value),
        //     m_dBaseCP,
        //     DoubleBitsAsPointer(m_dBaseCP),
        //     m_dCP,
        //     DoubleBitsAsPointer(m_dCP))
        // Log.Add(
        //     LOG_ERROR,
        //     L"[vitals] Negative CP input. Clamping to zero. Creature \"%ls\". Input %.17g (bits 0x%016llx, class 0x%04x),
        //     base %.17g (bits 0x%016llx), current %.17g (bits 0x%016llx).", name.c_str(), value, static_cast<unsigned long
        //     long>(DoubleBitsForLog(value)), static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseCP,
        //     static_cast<unsigned long long>(DoubleBitsForLog(m_dBaseCP)), m_dCP, static_cast<unsigned long
        //     long>(DoubleBitsForLog(m_dCP)))
        L2_LOG_ERROR(
            Log,
            u"[vitals] Negative CP input. Clamping to zero. Creature \"%ls\"; input %.17g (bits 0x%016llx, class 0x%04x), "
            u"base %.17g (bits 0x%016llx), current %.17g (bits 0x%016llx).",
            name.c_str(), value, static_cast<unsigned long long>(DoubleBitsForLog(value)),
            static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseCP,
            static_cast<unsigned long long>(DoubleBitsForLog(m_dBaseCP)), m_dCP,
            static_cast<unsigned long long>(DoubleBitsForLog(m_dCP))
        );
        runtime::process::ReportCallStack(LOG_ERROR, false);
        value = 0.0;
    }
    if (m_dBaseCP < value && 0.0 < m_dBaseCP) {
        if (FloatChecker::FPClass(value) != 0x0100) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"FloatERR [%s] huge CP input:%.5f(%p) "
            //     L"fpclass:%04x base:%.5f(%p) now:%.5f(%p)",
            //     name.c_str(),
            //     value,
            //     DoubleBitsAsPointer(value),
            //     FloatChecker::FPClass(value),
            //     m_dBaseCP,
            //     DoubleBitsAsPointer(m_dBaseCP),
            //     m_dCP,
            //     DoubleBitsAsPointer(m_dCP))
            // Log.Add(
            //     LOG_ERROR,
            //     L"[vitals] CP input exceeds its positive base. Clamping to the base value. Creature \"%ls\". Input %.17g
            //     (bits 0x%016llx, class 0x%04x), base %.17g (bits 0x%016llx), current %.17g (bits 0x%016llx).",
            //     name.c_str(), value,
            //     static_cast<unsigned long long>(DoubleBitsForLog(value)),
            //     static_cast<unsigned>(FloatChecker::FPClass(value)),
            //     m_dBaseCP, static_cast<unsigned long long>(DoubleBitsForLog(m_dBaseCP)),
            //     m_dCP, static_cast<unsigned long long>(DoubleBitsForLog(m_dCP)))
            L2_LOG_ERROR(
                Log,
                u"[vitals] CP input exceeds its positive base. Clamping to the base value. Creature \"%ls\"; input %.17g "
                u"(bits 0x%016llx, class 0x%04x), base %.17g (bits 0x%016llx), current %.17g (bits 0x%016llx).",
                name.c_str(), value, static_cast<unsigned long long>(DoubleBitsForLog(value)),
                static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseCP,
                static_cast<unsigned long long>(DoubleBitsForLog(m_dBaseCP)), m_dCP,
                static_cast<unsigned long long>(DoubleBitsForLog(m_dCP))
            );
            runtime::process::ReportCallStack(LOG_ERROR, false);
        }
        value = m_dBaseCP;
    }
    m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_dCP, value, 13);
}

void CSharedCreatureData::ValidateCP(double value) {
    L2_GUARD_FUNCTION();
    if (m_dCP < value) {
        SetCP(value);
    }
    if (m_dBaseCP < m_dCP) {
        SetCP(m_dBaseCP);
    }
}

void CSharedCreatureData::SetAP(int value, bool) {
    // Current L2Server 0x00a52900. The updatePacket parameter is ignored.
    L2_GUARD_FUNCTION();
    m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nAP, value, 100);
}

int CSharedCreatureData::GetAP() const {
    L2_GUARD_FUNCTION();
    return m_nAP;
}

int CSharedCreatureData::GetRemainAP() const {
    // Current L2Server.exe 0x00a52fc4.
    L2_GUARD_FUNCTION();
    const int remaining = GetAP() - m_stPotentialCount.nCount3 - m_stPotentialCount.nCount2 - m_stPotentialCount.nCount1;
    return std::clamp(remaining, 0, 36);
}

int CSharedCreatureData::CalcAPByLevel() const {
    // Current L2Server.exe 0x00a53078.
    L2_GUARD_FUNCTION();
    return std::clamp(m_nLevel - 84, 0, 36);
}

bool CSharedCreatureData::SetStats(CStats& value) {
    if (m_Stat.IsSameValue(value)) {
        return false;
    }
    m_Stat = value;
    m_Stat.Fix();
    m_NeedUpdatePacketBitSet.SetBit(60);
    return true;
}

int CSharedCreatureData::FindUpperBoundIndex(int factionIndex, int factionPoint) {
    L2_GUARD_FUNCTION();
    int lower = 0;
    int previousMiddle = -1;
    int upper = FactionMaxLevel[factionIndex] - 1;
    while (lower < upper) {
        const int middle = (lower + upper) / 2;
        if (middle == previousMiddle) {
            break;
        }
        const std::int64_t boundary = FactionByLevel[factionIndex][middle];
        if (boundary < factionPoint) {
            lower = middle + 1;
        } else {
            upper = middle;
            if (boundary <= factionPoint) {
                return middle + 1;
            }
        }
        previousMiddle = middle;
    }
    return upper;
}

// Current L2Server.exe 0x00a52ac0. Each lane is reset and the
// target stores the level before the matching point.
void CSharedCreatureData::InitFaction() {
    L2_GUARD_FUNCTION();
    for (int factionIndex = 0; factionIndex < 16; ++factionIndex) {
        runtime::raw_atomic::ExchangeI32(&m_nFactionLevel[factionIndex], 0);
        runtime::raw_atomic::ExchangeI64(&m_nFactionPoint[factionIndex], 0);
    }
}

// Current L2Server.exe 0x00a52b74. The target truncates the selected
// int64 point to a signed dword before atomically publishing it back into
// the int64 faction lane.
bool CSharedCreatureData::UpdateFactionPoint(int factionId, std::int64_t factionPoint) {
    L2_GUARD_FUNCTION();
    const std::uint32_t factionIndexBits = static_cast<std::uint32_t>(factionId) - 1U;
    if (factionIndexBits < 16U) {
        const int factionIndex = static_cast<int>(factionIndexBits);
        const std::int64_t maximumPoint = FactionByLevel[factionIndex][FactionMaxLevel[factionIndex] - 1] - 1;
        const std::int64_t selectedPoint = maximumPoint <= factionPoint ? maximumPoint : factionPoint;
        const std::int32_t storedPoint = runtime::Int32FromBits(static_cast<std::uint32_t>(selectedPoint));
        runtime::raw_atomic::ExchangeI64(&m_nFactionPoint[factionIndex], static_cast<std::int64_t>(storedPoint));
        return true;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Invalid Faction ID",
    //     L"..\\Shared\\SharedFactory.cpp",
    //     0x9ab)
    // Log.Add(
    //     LOG_ERROR,
    //     "[faction] Cannot update points: faction ID %d is outside 1..16 (requested points %lld).",
    //     factionId, static_cast<long long>(factionPoint))
    L2_LOG_ERROR(
        Log, "[faction] Cannot update points: faction ID %d is outside 1..16 (requested points %lld).", factionId,
        static_cast<long long>(factionPoint)
    );
    return false;
}

// Current L2Server.exe 0x00a52d70. This body also exists in the L2NPC
// product branch below, but SharedFactory.cpp is compiled per product and
// the L2Server branch must emit its own original owner.
bool CSharedCreatureData::IsLevelLimited(int factionId) {
    L2_GUARD_FUNCTION();
    const int factionIndex = factionId - 1;
    const std::int64_t maximumPoint = FactionByLevel[factionIndex][FactionMaxLevel[factionIndex] - 1] - 1;
    if (m_nFactionPoint[factionIndex] > maximumPoint) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Faction Point Overflow",
        //     L"..\\Shared\\SharedFactory.cpp",
        //     0x9f9)
        // Log.Add(
        //     LOG_ERROR,
        //     "[faction] Faction %d points exceed the limit of %lld. Treating the faction as level-limited.",
        //     factionId, static_cast<long long>(maximumPoint))
        L2_LOG_ERROR(
            Log, "[faction] Faction %d points exceed the limit of %lld. Treating the faction as level-limited.", factionId,
            static_cast<long long>(maximumPoint)
        );
        return true;
    }
    return m_nFactionPoint[factionIndex] == maximumPoint;
}

// Current L2Server.exe 0x00a52e7c. The current function does
// not validate factionId before using factionId - 1 as the table index.
float CSharedCreatureData::GetFactionPercentageForCurrentLevel(int factionId) {
    const int factionIndex = factionId - 1;
    const int factionLevel = m_nFactionLevel[factionIndex];
    if (factionLevel < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Faction Level Cannot less than 0",
        //     L"..\\Shared\\SharedFactory.cpp",
        //     0xa0e)
        // Log.Add(
        //     LOG_ERROR,
        //     "[faction] Cannot calculate progress for faction %d: level %d is negative. Returning zero progress.",
        //     factionId, factionLevel)
        L2_LOG_ERROR(
            Log, "[faction] Cannot calculate progress for faction %d: level %d is negative. Returning zero progress.",
            factionId, factionLevel
        );
        return 0.0F;
    }

    const std::int64_t currentLevelPoint = factionLevel == 0 ? 0 : FactionByLevel[factionIndex][factionLevel - 1];
    const std::int64_t levelSpan = FactionByLevel[factionIndex][factionLevel] - currentLevelPoint;
    if (levelSpan < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid Faction Level Table",
        //     L"..\\Shared\\SharedFactory.cpp",
        //     0xa1d)
        // Log.Add(
        //     LOG_ERROR,
        //     "[faction] Cannot calculate progress for faction %d, level %d: the level spans %lld points. A positive span is
        //     required, returning zero progress.", factionId, factionLevel, static_cast<long long>(levelSpan))
        L2_LOG_ERROR(
            Log,
            "[faction] Cannot calculate progress for faction %d, level %d: the level spans %lld points. A positive span is "
            "required; returning zero progress.",
            factionId, factionLevel, static_cast<long long>(levelSpan)
        );
        return 0.0F;
    }

    return static_cast<float>(m_nFactionPoint[factionIndex] - currentLevelPoint) / static_cast<float>(levelSpan);
}

int CSharedCreatureData::ConvertToInternalOneTimeQuestID(int questId) const {
    if (questId < 10255) {
        return questId;
    }
    if (questId < 12048) {
        return questId - 10000;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"current onetime quest id(%d) is over "
    //     L"onetimequest_end_id(%d)!!",
    //     questId,
    //     12047)
    // Log.Add(
    //     LOG_ERROR,
    //     "[quest] One-time quest ID %d exceeds the supported maximum %d. Returning internal ID 0.",
    //     questId, 12047)
    L2_LOG_ERROR(
        Log, "[quest] One-time quest ID %d exceeds the supported maximum %d. Returning internal ID 0.", questId, 12047
    );
    return 0;
}

void CSharedCreatureData::SetOneTimeFlag(int questId, bool value) {
    m_OneTimeQuestBitFlag.SetFlag(ConvertToInternalOneTimeQuestID(questId), value);
}

bool CSharedCreatureData::GetOneTimeFlag(int questId) const {
    return m_OneTimeQuestBitFlag.GetFlag(ConvertToInternalOneTimeQuestID(questId));
}

void CSharedCreatureData::GetOneTimeFlagAll(char* destination, int destinationSize) const {
    // The original SharedFactory.cpp supplies this same inline owner in
    // every product. The L2Server branch previously omitted it while the
    // L2NPC branch below kept the physical implementation.
    if (static_cast<std::uint32_t>(destinationSize) <= 0xffU) {
        return;
    }
    m_OneTimeQuestBitFlag.GetFlagAll(destination, static_cast<std::uint64_t>(static_cast<std::uint32_t>(destinationSize)));
}

void CSharedCreatureData::SetOneTimeFlagAll(char* source) {
    // Current L2Server 0x00a52fb0 copies the complete 0x100-byte store.
    m_OneTimeQuestBitFlag.SetFlagAll(source);
}

void CSharedCreatureData::InitDailyQuest() {
    // Current L2Server 0x00a51c80 clears all 300 physical records.
    L2_GUARD_FUNCTION();
    for (DailyQuestState& state : m_DailyQuest) {
        state.nQuestID = 0;
        state.nCompleteTime = 0;
    }
}

std::int32_t CSharedCreatureData::GetDailyBindTime() {
    // Current L2Server 0x00a5289c: the daily boundary is 06:30 local time.
    const std::int32_t now = runtime::crt::Time32Now();
    const std::tm* const local = runtime::crt::LocalTime32(now);
    std::int32_t elapsed = runtime::AddInt32Wrapping(
        runtime::MultiplyInt32Wrapping(local->tm_hour, 3600),
        runtime::AddInt32Wrapping(runtime::MultiplyInt32Wrapping(local->tm_min, 60), local->tm_sec)
    );
    if (local->tm_hour < 6 || (local->tm_hour == 6 && local->tm_min < 30)) {
        elapsed = runtime::AddInt32Wrapping(elapsed, 86400);
    }
    return runtime::AddInt32Wrapping(runtime::SubtractInt32Wrapping(now, elapsed), 23400);
}

std::int32_t CSharedCreatureData::GetWeeklyBindTime() {
    const std::int32_t now = runtime::crt::Time32Now();
    const std::tm* const local = runtime::crt::LocalTime32(now);
    const bool beforeReset =
        local->tm_wday < 3 || (local->tm_wday == 3 && local->tm_hour < 6) || (local->tm_hour == 6 && local->tm_min < 30);
    const std::int32_t daysSinceWednesday =
        beforeReset ? runtime::AddInt32Wrapping(local->tm_wday, 4) : runtime::AddInt32Wrapping(local->tm_wday, -3);
    const std::int32_t elapsed = runtime::AddInt32Wrapping(
        runtime::MultiplyInt32Wrapping(daysSinceWednesday, 86400),
        runtime::AddInt32Wrapping(
            runtime::MultiplyInt32Wrapping(local->tm_hour, 3600),
            runtime::AddInt32Wrapping(runtime::MultiplyInt32Wrapping(local->tm_min, 60), local->tm_sec)
        )
    );
    return runtime::AddInt32Wrapping(runtime::SubtractInt32Wrapping(now, elapsed), 23400);
}

std::vector<int> CSharedCreatureData::GetCompletedDailyQuestsAfterBindTime(std::int32_t bindTime) {
    std::vector<int> result;
    result.reserve(300);
    for (const DailyQuestState& state : m_DailyQuest) {
        if (state.nQuestID > 0 && bindTime < state.nCompleteTime) {
            result.push_back(state.nQuestID);
        }
    }
    return result;
}

bool CSharedItemData::IsStackable(ItemConsumeType consumeType) {
    return consumeType == ICT_STACKABLE || consumeType == ICT_ASSET;
}

int CSharedItemConstant::GetShotGrade(Shot::Type type) {
    if (type < Shot::SoulShot || type >= Shot::TypeMax) {
        return Shot::Invalid;
    }

    int grade = Shot::None;
    switch (m_Crystal) {
        case CRT_NONE:
            break;
        case CRT_D:
            grade = Shot::D;
            break;
        case CRT_C:
            grade = Shot::C;
            break;
        case CRT_B:
            grade = Shot::B;
            break;
        case CRT_A:
            grade = Shot::A;
            break;
        case CRT_S:
        case CRT_S80:
        case CRT_S84:
            grade = Shot::S;
            break;
        case CRT_R:
        case CRT_R95:
        case CRT_R99:
            grade = Shot::R;
            break;
        default:
            grade = Shot::Invalid;
            break;
    }

    if (type == Shot::SpiritShot) {
        return grade + Shot::GradeMax;
    }
    if (type == Shot::FishingShot) {
        return 16;
    }
    return grade;
}

void CSharedItemConstant::SetCommissionItemType(Commission::Enum type) {
    m_CommissionType = static_cast<std::int32_t>(type);
    SetCommissionItemFirstType(type);
}

void CSharedItemConstant::SetCommissionItemFirstType(Commission::Enum type) {
    const std::int32_t value = static_cast<std::int32_t>(type);

    // Current 0x00a51490 leaves the previous value intact for
    // enum holes (0, 11, 60 and out-of-range values).
    if ((value >= 1 && value <= 10) || (value >= 12 && value <= 18)) {
        m_CommissionFirstType = 0;
    } else if (value >= 19 && value <= 28) {
        m_CommissionFirstType = 1;
    } else if ((value >= 29 && value <= 34) || (value >= 62 && value <= 64)) {
        m_CommissionFirstType = 2;
    } else if (value >= 35 && value <= 41) {
        m_CommissionFirstType = 3;
    } else if (value >= 42 && value <= 43) {
        m_CommissionFirstType = 4;
    } else if (value >= 44 && value <= 59) {
        m_CommissionFirstType = 5;
    }
}

bool CSharedItemConstant::SetGeneralItemType(int type) {
    if (type == 0) {
        m_GeneralItemType = 0;
        return true;
    }
    if (type == 1) {
        m_GeneralItemType = 1;
        return true;
    }
    return false;
}

void CSharedItemConstant::SetCrystalType(CrystalType type) { m_Crystal = type; }

void CSharedItemConstant::SetShapeShiftableTargetItem(bool value) { m_bShapeShiftableTarget = value; }

void CSharedItemConstant::SetShapeShiftableExtractionItem(bool value) { m_bShapeShiftableExtarction = value; }

CSharedItemData* CSharedItemData::Next() { return CItemFactory::Inst().GetNext(index); }

void CSharedItemData::CopyFrom(const CSharedItemData* source) {
    std::memcpy(
        &m_nItemStart, &source->m_nItemStart, offsetof(CSharedItemData, m_nItemEnd) - offsetof(CSharedItemData, m_nItemStart)
    );
    const_index = source->const_index;
}

CSharedItemConstant* CSharedItemData::GetConstant() { return CItemFactory::Inst().GetConstant(const_index); }

void CSharedCreatureData::SetRideType(RideType value) {
    if (m_rideType == value) {
        return;
    }
    m_rideType = value;
    m_NeedUpdatePacketBitSet.SetBit(24);
}

void CSharedCreatureData::SetRideClassID(int value) {
    m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_rideClassID, value, 25);
}

void CSharedCreatureData::SetHeroType(int value) { m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nHeroType, value, 39); }

void CSharedCreatureData::SetNoblessType(int value) {
    m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nNoblessType, value, 40);
}

void CSharedCreatureData::SetNoblessIndex(int value) {
    m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nNoblessIndex, value, 41);
}

void CSharedCreatureData::SetOlympidApplicant(bool value) { m_bIsOlympiadApplicant = value; }

bool CSharedCreatureData::IsOlympiadApplicant() const { return m_bIsOlympiadApplicant; }

int CSharedCreatureData::GetAbnormalLevel(AbnormalStatusSlotType type) {
    // Current L2Server.exe 0x00a51a08. This body also exists in the
    // L2NPC product branch below. The original SharedFactory.cpp was
    // compiled for both products.
    L2_GUARD_FUNCTION();
    for (const AbnormalData& abnormal : m_AbnormalStatus) {
        if (abnormal.m_Type == type) {
            return abnormal.m_cAbnormalLevel;
        }
    }
    return -1;
}

void CSharedPartyData::SetPVPMatchStart(int teamIndex) {
    // Imported SharedFactory inline contract used by the current
    // UndergroundColosseum/PVPMatch owners.
    m_bPVPMatchStart = true;
    m_nPVPMatchTeamIndex = teamIndex;
}

void CSharedPartyData::SetPVPMatchEnd() { m_bPVPMatchStart = false; }

bool CSharedPartyData::IsPVPMatching() const { return m_bPVPMatchStart; }

void CSharedCreatureData::SetEnvironment(int value) {
    const EnvType converted = static_cast<EnvType>(value);
    if (m_Environment == converted) {
        return;
    }
    m_Environment = converted;
    m_NeedUpdatePacketBitSet.SetBit(83);
}

void CSharedCreatureData::SetUltimateSkillGauge(int value) {
    m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_UltimateSkillGauge, value, 89);
}

void CSharedCreatureData::UltimateSkillGauge(int value) {
    if (value < 0) {
        SetUltimateSkillGauge(0);
        return;
    }
    for (int index = 0; index < 4; ++index) {
        if (kUltimateSkillLevelInfo[index][0] <= m_nLevel && m_nLevel <= kUltimateSkillLevelInfo[index][1]) {
            SetUltimateSkillGauge(std::min(value, kUltimateSkillGaugeLimit[index]));
            return;
        }
    }
}

void CSharedCreatureData::SetSP(std::int64_t value) { m_NeedUpdatePacketBitSet.SetInt64Value_IfModifed(m_nSP, value, 14); }

void CSharedCreatureData::SetLevel(int value) {
    // Current L2Server inline contract: level is packet field five and a
    // changed level additionally marks the aggregate level/exp bit seven.
    if (m_nLevel == value) {
        return;
    }
    m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nLevel, value, 5);
    m_NeedUpdatePacketBitSet.SetBit(7);
}

void CSharedCreatureData::SetBuilder(int value) {
    // Current L2Server inlines this exact SharedFactory owner in
    // User::UpdateBuilder: builder is packet field 17.
    m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nBuilder, value, 17);
}

void CSharedCreatureData::RestoreHP() { SetHP(m_dBaseHP); }

void CSharedCreatureData::RestoreMP() { SetMP(m_dBaseMP); }

void CSharedCreatureData::RestoreCP() { SetCP(m_dBaseCP); }

bool CSharedCreatureData::IsSummon() {
    // Current SharedFactory.cpp owner. L2Server specialization.
    return m_SummonType == NST_SUMMON;
}

bool CSharedCreatureData::IsPet() { return m_SummonType == NST_PET; }

bool CSharedCreatureData::IsSummonOrPet() { return IsSummon() || IsPet(); }

bool CSharedCreatureData::GetDailyQuestFlag(int questId) {
    L2_GUARD_FUNCTION();
    const DailyQuestState* record = nullptr;
    for (const DailyQuestState& candidate : m_DailyQuest) {
        if (candidate.nQuestID == questId) {
            record = &candidate;
            break;
        }
    }
    if (!record) {
        return true;
    }

    const std::int32_t now = runtime::crt::Time32Now();
    const std::tm* const local = runtime::crt::LocalTime32(now);
    std::int32_t elapsed = runtime::AddInt32Wrapping(
        runtime::MultiplyInt32Wrapping(local->tm_hour, 3600),
        runtime::AddInt32Wrapping(runtime::MultiplyInt32Wrapping(local->tm_min, 60), local->tm_sec)
    );
    if (local->tm_hour < 6 || (local->tm_hour == 6 && local->tm_min < 30)) {
        elapsed = runtime::AddInt32Wrapping(elapsed, 86400);
    }
    const std::int32_t boundary = runtime::AddInt32Wrapping(runtime::SubtractInt32Wrapping(now, elapsed), 23400);
    return boundary >= record->nCompleteTime;
}

// Current L2Server.exe: 0x00a51d24.
int CSharedCreatureData::GetMaxMemoCount(QuestType questType) {
    L2_GUARD_FUNCTION();
    if (questType == QUEST_NORMAL) {
        return 41;
    }
    if (questType == QUEST_NONREGIST) {
        return 20;
    }
    return 0;
}

// Current L2Server.exe: 0x00a51dc0.
MemoStruct* CSharedCreatureData::GetMemoStructArray(QuestType questType) {
    L2_GUARD_FUNCTION();
    if (questType == QUEST_NORMAL) {
        return m_Memo;
    }
    if (questType == QUEST_NONREGIST) {
        return m_NRMemo;
    }
    return nullptr;
}

// Current L2Server.exe: 0x00a53880.
bool CSharedCreatureData::HaveMemo(QuestType questType, int questId) {
    L2_GUARD_FUNCTION();
    MemoStruct* current = GetMemoStructArray(questType);
    if (!current) {
        return false;
    }

    MemoStruct* const first = current;
    const int searchCount = GetMaxMemoCount(questType);
    while (current != first + searchCount && current->nQuestID != questId) {
        ++current;
    }
    return current != first + GetMaxMemoCount(questType);
}

// Current L2Server.exe: 0x00a54818.
bool CSharedCreatureData::HaveMemo(int questId) {
    L2_GUARD_FUNCTION();
    return HaveMemo(QUEST_NORMAL, questId) || HaveMemo(QUEST_NONREGIST, questId);
}

// Current L2Server.exe: 0x00a53978.
int CSharedCreatureData::GetMemoState(QuestType questType, int questId, int slot) {
    L2_GUARD_FUNCTION();
    MemoStruct* current = GetMemoStructArray(questType);
    if (!current) {
        return -1;
    }

    MemoStruct* const first = current;
    const int searchCount = GetMaxMemoCount(questType);
    while (current != first + searchCount && current->nQuestID != questId) {
        ++current;
    }
    if (current == first + GetMaxMemoCount(questType)) {
        return -1;
    }

    switch (slot) {
        case 0:
            return current->nState;
        case 1:
            return current->nState2;
        case 2:
            return current->nState3;
        case 3:
            return current->nState4;
        default:
            return -1;
    }
}

// Current L2Server.exe: 0x00a53aac.
int CSharedCreatureData::GetUserNPCLog(QuestType questType, int questId, int npcId, UserNPCLogType userLogType) {
    L2_GUARD_FUNCTION();
    if (!HaveMemo(questType, questId)) {
        return 0;
    }

    MemoStruct* current = GetMemoStructArray(questType);
    if (current == nullptr) {
        return 0;
    }
    MemoStruct* const first = current;
    const int searchCount = GetMaxMemoCount(questType);
    while (current != first + searchCount && current->nQuestID != questId) {
        ++current;
    }
    if (current == first + GetMaxMemoCount(questType)) {
        return 0;
    }

    for (const UserNPCLog& log : current->NPCLog) {
        const int count = log.userLogType == userLogType && log.nNPCID == npcId ? log.nCount : 0;
        if (count != 0) {
            return count;
        }
    }
    return 0;
}

// Current L2Server.exe: 0x00a53c04.
bool CSharedCreatureData::DeleteUserNPCLog(QuestType questType, int questId, int npcId, UserNPCLogType userLogType) {
    L2_GUARD_FUNCTION();
    if (!HaveMemo(questType, questId)) {
        return false;
    }

    MemoStruct* current = GetMemoStructArray(questType);
    if (current == nullptr) {
        return false;
    }
    MemoStruct* const first = current;
    const int searchCount = GetMaxMemoCount(questType);
    while (current != first + searchCount && current->nQuestID != questId) {
        ++current;
    }
    if (current == first + GetMaxMemoCount(questType)) {
        return false;
    }
    return current->DeleteNPCLog(userLogType, npcId);
}

// Current L2Server.exe: 0x00a541a8.
bool CSharedCreatureData::SetMemo(QuestType questType, int questId) {
    L2_GUARD_FUNCTION();
    if (HaveMemo(questType, questId)) {
        return false;
    }

    MemoStruct* current = GetMemoStructArray(questType);
    if (!current) {
        return false;
    }
    MemoStruct* const end = current + GetMaxMemoCount(questType);
    while (current != end && current->nQuestID != 0) {
        ++current;
    }
    if (current == end) {
        const std::wstring creatureName = Utf16ToWide(m_sName);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] SetMemo() failed. memo[%d] "
        //     L"not enough slot on creature[%s]",
        //     L"..\\Shared\\SharedFactory.cpp",
        //     0x56f,
        //     questId,
        //     creatureName.c_str())
        // Log.Add(
        //     LOG_WARN,
        //     L"[quest] Cannot add quest %d (type %d) for \"%ls\": no free memo slot.",
        //     questId, static_cast<int>(questType), creatureName.c_str())
        L2_LOG_WARN(
            Log, u"[quest] Cannot add quest %d (type %d) for \"%ls\": no free memo slot.", questId,
            static_cast<int>(questType), creatureName.c_str()
        );
        return false;
    }

    current->nQuestID = questId;
    current->nJournal = 1;
    current->nState = 0;
    current->nState2 = 0;
    current->nState3 = 0;
    current->nState4 = 0;
    current->bChanged = true;
    return true;
}

// Current L2Server.exe: 0x00a54b04.
bool CSharedCreatureData::SetMemo(int questId) {
    L2_GUARD_FUNCTION();
    return SetMemo(QUEST_NORMAL, questId);
}

// Current L2Server.exe: 0x00a5432c.
bool CSharedCreatureData::SetMemoState(QuestType questType, int questId, int slot, int value) {
    L2_GUARD_FUNCTION();
    if (!HaveMemo(questType, questId)) {
        return false;
    }

    MemoStruct* current = GetMemoStructArray(questType);
    if (!current) {
        return false;
    }
    MemoStruct* const end = current + GetMaxMemoCount(questType);
    while (current != end && current->nQuestID != questId) {
        ++current;
    }
    if (current == end) {
        const std::wstring creatureName = Utf16ToWide(m_sName);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] SetMemoState failed. no memo[%d] "
        //     L"slot[%d] state[%d] on creature[%s]",
        //     L"..\\Shared\\SharedFactory.cpp",
        //     0x5b1,
        //     questId,
        //     slot,
        //     value,
        //     creatureName.c_str())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[quest] Cannot set state slot %d to %d for quest %d (type %d) on \"%ls\": memo not found.",
        //     slot, value, questId, static_cast<int>(questType), creatureName.c_str())
        L2_LOG_ERROR(
            Log, u"[quest] Cannot set state slot %d to %d for quest %d (type %d) on \"%ls\": memo not found.", slot, value,
            questId, static_cast<int>(questType), creatureName.c_str()
        );
        return false;
    }

    switch (slot) {
        case 0:
            current->nState = value;
            break;
        case 1:
            current->nState2 = value;
            break;
        case 2:
            current->nState3 = value;
            break;
        case 3:
            current->nState4 = value;
            break;
        default: {
            const std::wstring creatureName = Utf16ToWide(m_sName);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] SetMemoState, slot[%d] is invalid "
            //     L"on creature[%s]",
            //     L"..\\Shared\\SharedFactory.cpp",
            //     0x5a6,
            //     slot,
            //     creatureName.c_str())
            // Log.Add(
            //     LOG_ERROR,
            //     L"[quest] Cannot set state slot %d to %d for quest %d (type %d) on \"%ls\": valid slots are 0..3.",
            //     slot, value, questId, static_cast<int>(questType), creatureName.c_str())
            L2_LOG_ERROR(
                Log, u"[quest] Cannot set state slot %d to %d for quest %d (type %d) on \"%ls\": valid slots are 0..3.",
                slot, value, questId, static_cast<int>(questType), creatureName.c_str()
            );
            return false;
        }
    }
    current->bChanged = true;
    return true;
}

// Current L2Server.exe: 0x00a5329c.
bool CSharedCreatureData::SetJournal(QuestType questType, int questId, int value) {
    L2_GUARD_FUNCTION();
    MemoStruct* current = GetMemoStructArray(questType);
    if (!current) {
        return false;
    }
    MemoStruct* const end = current + GetMaxMemoCount(questType);
    while (current != end && current->nQuestID != questId) {
        ++current;
    }
    if (current == end) {
        const std::wstring creatureName = Utf16ToWide(m_sName);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] SetJournal() failed. no memo[%d] "
        //     L"on creature[%s]",
        //     L"..\\Shared\\SharedFactory.cpp",
        //     0x5d6,
        //     questId,
        //     creatureName.c_str())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[quest] Cannot set journal to %d for quest %d (type %d) on \"%ls\": memo not found.",
        //     value, questId, static_cast<int>(questType), creatureName.c_str())
        L2_LOG_ERROR(
            Log, u"[quest] Cannot set journal to %d for quest %d (type %d) on \"%ls\": memo not found.", value, questId,
            static_cast<int>(questType), creatureName.c_str()
        );
        return false;
    }
    current->nJournal = value;
    return true;
}

// Current L2Server.exe: 0x00a53404.
bool CSharedCreatureData::SetFlagJournal(QuestType questType, int questId, int flag) {
    L2_GUARD_FUNCTION();
    MemoStruct* current = GetMemoStructArray(questType);
    if (!current) {
        return false;
    }
    MemoStruct* const end = current + GetMaxMemoCount(questType);
    while (current != end && current->nQuestID != questId) {
        ++current;
    }
    if (current == end) {
        const std::wstring creatureName = Utf16ToWide(m_sName);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] SetFlagJournal() failed. no memo[%d] "
        //     L"on creature[%s], flag[%d]",
        //     L"..\\Shared\\SharedFactory.cpp",
        //     0x5fd,
        //     questId,
        //     creatureName.c_str(),
        //     flag)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[quest] Cannot set journal flag %d for quest %d (type %d) on \"%ls\": memo not found.",
        //     flag, questId, static_cast<int>(questType), creatureName.c_str())
        L2_LOG_ERROR(
            Log, u"[quest] Cannot set journal flag %d for quest %d (type %d) on \"%ls\": memo not found.", flag, questId,
            static_cast<int>(questType), creatureName.c_str()
        );
        return false;
    }

    const std::uint32_t bit = 1U << ((static_cast<std::uint32_t>(flag) - 1U) & 31U);
    current->nJournal = static_cast<std::int32_t>(static_cast<std::uint32_t>(current->nJournal) | bit | 0x80000000U);
    return true;
}

// Current L2Server.exe: 0x00a53580.
bool CSharedCreatureData::ResetFlagJournal(QuestType questType, int questId, int flag) {
    L2_GUARD_FUNCTION();
    MemoStruct* current = GetMemoStructArray(questType);
    if (!current) {
        return false;
    }
    MemoStruct* const end = current + GetMaxMemoCount(questType);
    while (current != end && current->nQuestID != questId) {
        ++current;
    }
    if (current == end) {
        const std::wstring creatureName = Utf16ToWide(m_sName);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ResetFlagJournal() failed. no memo[%d] "
        //     L"on creature[%s], flag[%d]",
        //     L"..\\Shared\\SharedFactory.cpp",
        //     0x622,
        //     questId,
        //     creatureName.c_str(),
        //     flag)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[quest] Cannot clear journal flag %d for quest %d (type %d) on \"%ls\": memo not found.",
        //     flag, questId, static_cast<int>(questType), creatureName.c_str())
        L2_LOG_ERROR(
            Log, u"[quest] Cannot clear journal flag %d for quest %d (type %d) on \"%ls\": memo not found.", flag, questId,
            static_cast<int>(questType), creatureName.c_str()
        );
        return false;
    }

    const std::uint32_t bit = 1U << ((static_cast<std::uint32_t>(flag) - 1U) & 31U);
    current->nJournal = static_cast<std::int32_t>(static_cast<std::uint32_t>(current->nJournal) & ~bit);
    return true;
}

// Current L2Server.exe: 0x00a545f8.
bool CSharedCreatureData::SetFlagJournal(int questId, int flag) {
    L2_GUARD_FUNCTION();
    return SetFlagJournal(QUEST_NORMAL, questId, flag);
}

// Current L2Server.exe: 0x00a546b4.
bool CSharedCreatureData::ResetFlagJournal(int questId, int flag) {
    L2_GUARD_FUNCTION();
    return ResetFlagJournal(QUEST_NORMAL, questId, flag);
}

// Current L2Server.exe: 0x00a5453c.
bool CSharedCreatureData::SetJournal(int questId, int value) {
    L2_GUARD_FUNCTION();
    return SetJournal(QUEST_NORMAL, questId, value);
}

// Current L2Server.exe: 0x00a536f8.
bool CSharedCreatureData::RemoveMemo(QuestType questType, int questId) {
    L2_GUARD_FUNCTION();
    MemoStruct* current = GetMemoStructArray(questType);
    if (current == nullptr) {
        return false;
    }

    MemoStruct* const end = current + GetMaxMemoCount(questType);
    while (current != end && current->nQuestID != questId) {
        ++current;
    }
    if (current == end) {
        const std::wstring creatureName = Utf16ToWide(m_sName);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] RemoveMemo() failed. no memo[%d] "
        //     L"on creature[%s]",
        //     L"..\\Shared\\SharedFactory.cpp",
        //     0x650,
        //     questId,
        //     creatureName.c_str())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[quest] Cannot remove quest %d (type %d) from \"%ls\": memo not found.",
        //     questId, static_cast<int>(questType), creatureName.c_str())
        L2_LOG_ERROR(
            Log, u"[quest] Cannot remove quest %d (type %d) from \"%ls\": memo not found.", questId,
            static_cast<int>(questType), creatureName.c_str()
        );
        return false;
    }

    current->nQuestID = 0;
    current->nState = 0;
    current->nState2 = 0;
    current->nState3 = 0;
    current->nState4 = 0;
    current->nJournal = 1;
    current->bDeleted = true;
    for (UserNPCLog& log : current->NPCLog) {
        log.userLogType = USER_NPC_LOG_MAX;
        log.nNPCID = 0;
        base_structure_detail::InterlockedStore(&log.nCount, 0);
    }
    return true;
}

// Current L2Server.exe: 0x00a54770.
bool CSharedCreatureData::RemoveMemo(int questId) {
    L2_GUARD_FUNCTION();
    return RemoveMemo(QUEST_NORMAL, questId);
}

// Current L2Server.exe: 0x00a548e0.
int CSharedCreatureData::GetMemoState(int questId, int slot) {
    L2_GUARD_FUNCTION();
    return GetMemoState(QUEST_NORMAL, questId, slot);
}

// Current L2Server.exe: 0x00a54988.
bool CSharedCreatureData::SetNRMemo(int questId) {
    L2_GUARD_FUNCTION();
    return SetMemo(QUEST_NONREGIST, questId);
}

// Current L2Server.exe: 0x00a54a34.
bool CSharedCreatureData::SetNRMemoState(int questId, int slot, int value) {
    L2_GUARD_FUNCTION();
    return SetMemoState(QUEST_NONREGIST, questId, slot, value);
}

// Current L2Server.exe: 0x00a53d74.
bool CSharedCreatureData::SetNRJournal(int questId, int value) {
    L2_GUARD_FUNCTION();
    return SetJournal(QUEST_NONREGIST, questId, value);
}

// Current L2Server.exe: 0x00a53e30.
bool CSharedCreatureData::SetNRFlagJournal(int questId, int flag) {
    L2_GUARD_FUNCTION();
    return SetFlagJournal(QUEST_NONREGIST, questId, flag);
}

// Current L2Server.exe: 0x00a53eec.
bool CSharedCreatureData::ResetNRFlagJournal(int questId, int flag) {
    L2_GUARD_FUNCTION();
    return ResetFlagJournal(QUEST_NONREGIST, questId, flag);
}

// Current L2Server.exe: 0x00a54bac.
bool CSharedCreatureData::SetMemoState(int questId, int slot, int value) {
    L2_GUARD_FUNCTION();
    return SetMemoState(QUEST_NORMAL, questId, slot, value);
}

// Current L2Server.exe: 0x00a53fa8.
bool CSharedCreatureData::RemoveNRMemo(int questId) {
    L2_GUARD_FUNCTION();
    return RemoveMemo(QUEST_NONREGIST, questId);
}

// Current L2Server.exe: 0x00a52f20.
void CSharedStaticObjectData::CopyFrom(const CSharedStaticObjectData* source) {
    std::memcpy(
        &m_nStaticObjectStart, &source->m_nStaticObjectStart,
        offsetof(CSharedStaticObjectData, m_nStaticObjectEnd) - offsetof(CSharedStaticObjectData, m_nStaticObjectStart)
    );
}

// Current L2Server.exe: 0x00a52f3c.
void CSharedGeneralObjectData::CopyFrom(const CSharedGeneralObjectData* source) {
    std::memcpy(
        &m_nGeneralObjectStart, &source->m_nGeneralObjectStart,
        offsetof(CSharedGeneralObjectData, m_nGeneralObjectEnd) - offsetof(CSharedGeneralObjectData, m_nGeneralObjectStart)
    );
}

#else

#include "L2NPC/src/Creature.h"
#include "L2NPC/src/ScriptTypeInfo.h"
#include "L2NPC/src/User.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/SharedFactoryMapping.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <iterator>
#include <stdexcept>
#include <string>


// Portable mapping ownership specializes this singleton below. Declare the
// specialization before the original CSharedCreatureData::Next/GetConstant
// source-order calls so the compiler does not instantiate the primary body.
template <>
CCreatureFactory& CCreatureFactory::Inst();

namespace UltimateSkill {

// NASC64_2015.exe: 0x009f40c8.
constexpr std::int32_t LevelInfo[4][2] = {
    {0, 84},
    {85, 89},
    {90, 94},
    {95, 99},
};

} // namespace UltimateSkill

// NASC64_2015.exe: 0x009f40e8.
constexpr std::int32_t UltimateSkillLvTable[4] = {
    0,
    500,
    1000,
    1500,
};

namespace {

void AddSharedScriptVariable(ScriptTypeInfo& typeInfo, const wchar_t* name, int offset, ScriptTypeId typeId) {
    auto* const variable = new ScriptVarInfo;
    variable->SetScriptVarName(name);
    variable->Offset(offset);
    variable->Type(&GetBuiltinScriptType(typeId));
    typeInfo.AddVarInfo(variable);
}

// std::wstring SharedCreatureNameForLog(
//     const char16_t* value) {
//     std::wstring result
//     if (!value) return result.
//     while (*value != u'\0') {
//         std::uint32_t codePoint = *value++
//         if constexpr (sizeof(wchar_t) == 2) {
//             result.push_back(
//                 static_cast<wchar_t>(codePoint))
//             continue.
//         }
//         if (codePoint >= 0xd800U &&
//             codePoint <= 0xdbffU &&
//             *value >= 0xdc00U &&
//             *value <= 0xdfffU) {
//             codePoint =
//                 0x10000U +
//                 ((codePoint - 0xd800U) << 10U) +
//                 (static_cast<std::uint32_t>(*value++) -
//                  0xdc00U)
//         }
//         result.push_back(
//             static_cast<wchar_t>(codePoint))
//     }
//     return result
// }
} // namespace

using AllianceFactory = CSharedFactory<CAlliance, CSharedAllianceData, CSharedAllianceConstant, &SharedDefine_Alliance>;

CSharedDefine::CSharedDefine(
    const wchar_t* dataMMFName,
    const wchar_t* constantMMFName,
    const wchar_t* indexMMFName,
    const wchar_t* mapMMFName,
    int maxData,
    int maxClass,
    const wchar_t* lockName,
    const wchar_t* mapLockName,
    const wchar_t* eachLockName
) :
    DataMMFName(dataMMFName),
    ConstantMMFName(constantMMFName),
    IndexMMFName(indexMMFName),
    MapMMFName(mapMMFName),
    max_data(maxData),
    max_class(maxClass),
    lock_name(lockName),
    maplock_name(mapLockName),
    eachlock_name(eachLockName) {}

const CSharedDefine SharedDefine_Alliance(
    L"L2World_AllianceData",
    L"L2World_AllianceConstant",
    L"L2World_AllianceIndex",
    L"L2World_AllianceMap",
    5000,
    0,
    L"AllianceLock",
    L"AllianceMapLock",
    L"EachAllianceLock"
);

const CSharedDefine SharedDefine_AirShip(
    L"L2World_AirShipData",
    L"L2World_AirShipConstant",
    L"L2World_AirShipIndex",
    L"L2World_AirShipMap",
    1000,
    100,
    L"AirShipLock",
    L"AirShipMapLock",
    L"EachAirShipLock"
);

const CSharedDefine SharedDefine_Creature(
    L"L2World_CreatureData",
    L"L2World_CreatureConstant",
    L"L2World_CreatureIndex",
    L"L2World_CreatureMap",
    120000,
    0,
    L"CreatureLock",
    L"CreatureMapLock",
    L"EachCreatureLock"
);

const CSharedDefine SharedDefine_Party(
    L"L2World_PartyData",
    L"L2World_PartyConstant",
    L"L2World_PartyIndex",
    L"L2World_PartyMap",
    10003,
    0,
    L"PartyLock",
    L"PartyMapLock",
    L"EachPartyLock"
);

const CSharedDefine SharedDefine_Pledge(
    L"L2World_PledgeData",
    L"L2World_PledgeConstant",
    L"L2World_PledgeIndex",
    L"L2World_PledgeMap",
    10002,
    0,
    L"PledgeLock",
    L"PledgeMapLock",
    L"EachPledgeLock"
);

const CSharedDefine SharedDefine_GeneralObject(
    L"L2World_GeneralObjectData",
    L"L2World_GeneralObjectConstant",
    L"L2World_GeneralObjectIndex",
    L"L2World_GeneralObjectMap",
    1020000,
    0,
    L"GeneralObjectLock",
    L"GeneralObjectMapLock",
    L"EachGeneralObjectLock"
);

const CSharedDefine SharedDefine_Item(
    L"L2World_ItemData",
    L"L2World_ItemConstant",
    L"L2World_ItemIndex",
    L"L2World_ItemMap",
    900000,
    100000,
    L"ItemLock",
    L"ItemMapLock",
    L"EachItemLock"
);

const CSharedDefine SharedDefine_StaticObject(
    L"L2World_StaticObjectData",
    L"L2World_StaticObjectConstant",
    L"L2World_StaticObjectIndex",
    L"L2World_StaticObjectMap",
    10001,
    0,
    L"StaticObjectLock",
    L"StaticObjectMapLock",
    L"EachStaticObjectLock"
);

CStats::CStats() { Init(); }

CStats::CStats(const CStats& other) { std::copy(std::begin(other.m_N), std::end(other.m_N), std::begin(m_N)); }

CStats& CStats::operator=(const CStats& other) {
    if (this != &other) {
        std::copy(std::begin(other.m_N), std::end(other.m_N), std::begin(m_N));
    }
    return *this;
}

CStats CStats::operator-(const CStats& other) const {
    CStats result;
    for (std::size_t index = 0; index < std::size(m_N); ++index) {
        result.m_N[index] =
            static_cast<std::int32_t>(static_cast<std::uint32_t>(m_N[index]) - static_cast<std::uint32_t>(other.m_N[index]));
    }
    return result;
}

CStats CStats::operator+(const CStats& other) const {
    CStats result;
    for (std::size_t index = 0; index < std::size(m_N); ++index) {
        result.m_N[index] =
            static_cast<std::int32_t>(static_cast<std::uint32_t>(m_N[index]) + static_cast<std::uint32_t>(other.m_N[index]));
    }
    return result;
}

CStats& CStats::operator+=(const CStats& other) {
    for (std::size_t index = 0; index < std::size(m_N); ++index) {
        m_N[index] =
            static_cast<std::int32_t>(static_cast<std::uint32_t>(m_N[index]) + static_cast<std::uint32_t>(other.m_N[index]));
    }
    return *this;
}

bool CStats::IsSameValue(const CStats& other) {
    // shared/SharedFactory.cpp:618-632, NASC64_2015.exe 0x007b84f0.
    // The shipped loop uses sizeof(m_N) as an element count, so it compares
    // 32 consecutive int32 values instead of the eight values in CStats.
    // Retain the exact defective target loop disabled beside the safe
    // eight-element portable comparison.
#if 0
    const std::int32_t* targetLeft = m_N;
    const std::int32_t* targetRight = other.m_N;
    for (int targetIndex = 0; targetIndex < 32; ++targetIndex) {
        if (*targetLeft++ != *targetRight++) return false;
    }
#endif
    for (std::size_t index = 0; index < std::size(m_N); ++index) {
        if (m_N[index] != other.m_N[index]) {
            return false;
        }
    }
    return true;
}

void CStats::Fix() {
    for (std::int32_t& value : m_N) {
        value = std::clamp(value, 0, 200);
    }
}

void CStats::GuaranteeMax(int maximum) {
    for (std::int32_t& value : m_N) {
        value = std::max(value, maximum);
    }
}

void CStats::Init() { std::fill(std::begin(m_N), std::end(m_N), 0); }

bool CStats::IsValid() const {
    return std::all_of(std::begin(m_N), std::end(m_N), [](std::int32_t value) { return value >= 0 && value <= 200; });
}

void CStats::Report(wchar_t* sourceFile, int sourceLine, wchar_t* creatureName) const {
    if (!sourceFile || !creatureName) {
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] CreatureName:%s INT(%d) STR(%d) "
    //     L"CON(%d) MEN(%d) DEX(%d) WIT(%d)",
    //     sourceFile,
    //     sourceLine,
    //     creatureName,
    //     m_N[0],
    //     m_N[1],
    //     m_N[2],
    //     m_N[3],
    //     m_N[4],
    //     m_N[5])
    // Log.Add(
    //     LOG_ERROR,
    //     L"[stats] Report from \"%ls\", line %d, for \"%ls\": INT %d, STR %d, CON %d, MEN %d, DEX %d, WIT %d.",
    //     sourceFile, sourceLine, creatureName,
    //     m_N[0], m_N[1], m_N[2], m_N[3], m_N[4], m_N[5])
    L2_LOG_ERROR(
        Log, u"[stats] Report from \"%ls\", line %d, for \"%ls\": INT %d, STR %d, CON %d, MEN %d, DEX %d, WIT %d.",
        sourceFile, sourceLine, creatureName, m_N[0], m_N[1], m_N[2], m_N[3], m_N[4], m_N[5]
    );
}

FVector CSharedObject::GetPos() const { return m_Pos; }

void CSharedObject::SetPos(const FVector& position) { m_Pos = position; }

FAngle CSharedObject::GetDir() const { return m_Dir; }

int CSharedObject::GetDirByInt() const { return m_Dir.Yaw; }

void CSharedObject::SetDirByInt(int direction) {
    FAngle angle;
    angle.Yaw = direction;
    SetDir(angle);
}

void CSharedObject::SetDir(const FAngle& direction) { m_Dir = direction; }

CSharedCreatureData::CSharedCreatureData() {
    // shared/SharedFactory.cpp:709 in the matching VS2015 image.
    // CSharedObject initializes only position/direction here.
    // the factory owns its id/index/list metadata.
    std::fill(std::begin(m_OriginalStat.m_N), std::end(m_OriginalStat.m_N), 0);
    std::fill(std::begin(m_OriginAbnormalResist.m_N), std::end(m_OriginAbnormalResist.m_N), 0);
    m_pPet.id = -1;
    m_pPet.index = -1;
    for (CSafePointer2<CCreature>& summon : m_pSummon) {
        summon.id = -1;
        summon.index = -1;
    }
    for (MemoStruct& memo : m_Memo) {
        memo = MemoStruct{};
    }
    for (MemoStruct& memo : m_NRMemo) {
        memo = MemoStruct{};
    }

    std::fill(std::begin(m_DailyQuest), std::end(m_DailyQuest), DailyQuestState{});
    m_bEventFlag[0] = false;
    m_BaseDamageRange = {};
    std::fill(std::begin(m_Stat.m_N), std::end(m_Stat.m_N), 0);
    std::fill(std::begin(m_AbnormalResist.m_N), std::end(m_AbnormalResist.m_N), 0);
    m_DualClassLevel = 0;
    m_pMaster.id = -1;
    m_pMaster.index = -1;
    for (AbnormalData& abnormal : m_AbnormalStatus) {
        abnormal.m_Type = static_cast<AbnormalStatusSlotType>(-1);
        abnormal.m_cAbnormalLevel = 0;
        abnormal.m_nMagicLevel = 0;
    }
    m_EnableMoveAfterTalk = true;
    m_OnGoingDynamicContent = 0;
    m_stPotentialCount = {};

    m_bIsUser = false;
    m_bIsAlive = 0;
    m_nAge = 0;
    m_bExistInHash = false;
    m_nPrivateID = 0;
    m_nSubJobId = 0;
    m_nNoblessType = 0;
    // m_nHeroType = 0
    m_nHeroType = HERO_NONE;
    m_nNoblessIndex = 0;
    m_nSubJob0Class = -1;
    m_nSubJob1Class = -1;
    m_nSubJob2Class = -1;
    m_nSubJob3Class = -1;
    std::fill(std::begin(m_SubJobLevel), std::end(m_SubJobLevel), 0);
    m_DualClassID = -1;
    m_IsDualClass = false;
    m_bUndying = false;
    m_bCanBeAttacked = false;
    m_bGMInvisible = false;
    m_dCollisionRadius = 0.0;
    m_dCollisionHeight = 0.0;
    m_nFace = 0;
    m_nHairShape = 0;
    m_nHairColor = 0;
    m_nFaceEx = 0;
    m_nHairShapeEx = 0;
    m_nHairColorEx = 0;
    m_nExp = 0;
    m_nSP = 0;
    m_nInventoryIndex = 0;
    std::fill(std::begin(m_nSlotIndex), std::end(m_nSlotIndex), 0);
    m_dHP = 0.0;
    m_dMP = 0.0;
    m_dCP = 0.0;
    std::fill(std::begin(m_sName), std::end(m_sName), char16_t{0});

    m_TargetIndex = 0;
    m_TargetSID = 0;
    m_cIsGuilty = 0;
    m_bIsFreezed = false;
    m_nDuelCount = 0;
    m_nPKCount = 0;
    m_nPKPardonCount = 0;
    m_nKarma = 0;
    m_nSulffrageCount = 0;
    m_nSulffrageUsedCount = 0;
    m_nSociality = 0;
    m_nPKTimeStamp = 0;

    m_dwLastRewardTime = 0;
    m_LastAttackerServerID = 0;
    m_FirstAttackerServerID = 0;
    m_nPledgeID = 0;
    m_nPledgeIndex = 0;
    m_bPledgeMaster = false;
    m_nPledgeType = 0;
    m_nRelatedIndex = 0;
    m_bIsRelated = false;
    m_nGradeId = 0;
    m_bIsNPCRelatedToPledge = false;
    m_nAcademyPledgeDBID = 0;
    m_nAllianceId = 0;
    m_nAllianceIndex = 0;
    m_bCanJoinParty = false;
    m_nPartyId = 0;
    m_nPartyIndex = 0;
    m_bPartyMaster = false;
    m_nMPCCId = 0;
    m_bNotFunctionable = false;
    m_nResidenceID = 0;
    m_nResidenceOwnerID = 0;
    m_nRulingAgit = 0;
    m_nPrivateStoreType = 0;
    m_nPrivateStoreLimit = 0;
    m_nPrivateStoreBuyLimit = 0;
    m_nPrivateStoreRecipeType = 0;
    m_nSoulShot = 0;
    m_dSoulShotAttackBonusPer = 0.0;
    m_bSpiritShot = false;
    m_dSpiritShotAttackBonusPer = 0.0;
    m_dSpiritShotSpeedBonusPer = 0.0;
    m_nSoulShotCount = 0;
    m_nSpiritShotCount = 0;
    m_nFishingShot = 0;
    m_bIsDiss = false;
    m_bIsCorpse = false;
    m_bCanBeResurrected = false;
    m_bCanResurrection = 0;
    std::fill(std::begin(m_sResurrectSpellerName), std::end(m_sResurrectSpellerName), char16_t{0});
    m_nResurrectExp = 0;
    m_nResurrectRatio = 0;
    m_dwResurrectTime = 0;
    m_bIsFakeDeath = false;
    m_bFakeDeathFail = false;
    m_nDeathPenaltyLevel = 0;
    m_nNewbieStatus = 0;
    m_nDBID = 0;
    m_nBaseAttackRange = 0;
    m_nBaseRandomDamage = 0;
    m_nBaseReuseDelay = 0;
    std::fill(std::begin(m_nBaseParam), std::end(m_nBaseParam), 0);
    m_HennaId_1 = 0;
    m_HennaId_2 = 0;
    m_HennaId_3 = 0;
    m_cEnableHenna = 0;
    m_nLevel = 0;
    m_MainClassLevel = 0;
    m_nMaxInventoryNum = 0;
    m_nNumInventory = 0;
    m_nMaxQuestInventoryNum = 0;
    m_nCurrentQuestInventoryNum = 0;
    m_nMaxArtifactInventoryNum = 0;
    m_nCurrentArtifactInventoryNum = 0;
    m_nInventoryWeight = 0;
    m_dCarryWeight = 0.0;
    m_dOrgHP = 0.0;
    m_dOrgMP = 0.0;
    m_dOrgCP = 0.0;
    m_dBaseHP = 0.0;
    m_dBaseMP = 0.0;
    m_dBaseCP = 0.0;
    m_dOrgHPRegen = 0.0;
    m_dOrgMPRegen = 0.0;
    m_dOrgCPRegen = 0.0;
    m_dHPRegen = 0.0;
    m_dMPRegen = 0.0;
    m_dCPRegen = 0.0;
    m_dAttackTime = 0.0;
    m_EquipedWeaponClassID = 0;
    m_nAttackRange = 0;
    m_nRandomDamage = 0;
    std::fill(std::begin(m_nOrgParam), std::end(m_nOrgParam), 0);
    m_nMoveTypeSpeed = 0;
    std::fill(std::begin(m_dParam), std::end(m_dParam), 0.0);
    m_dHitTimeFactor = 0.0;
    m_dSkillHitTimeFactor = 0.0;
    m_nBuilder = 0;
    m_nCoolTime = 0.0;
    m_nLastBlowWeaponClassID = 0;
    m_nSSQRoundNumber = 0;
    m_nSSQPart = 0;
    m_nSSQPosition = 0;
    m_nSealSelectionNo = 0;
    m_nSSQPoint = 0;
    std::fill(std::begin(m_nTwilightItemCount), std::end(m_nTwilightItemCount), std::int64_t{0});
    std::fill(std::begin(m_nDawnItemCount), std::end(m_nDawnItemCount), std::int64_t{0});
    m_nTicketBuyCount = 0;
    m_nSSQDawnRound = 0;
    m_nInPeaceZone = 0;
    m_nInBattleField = 0;
    m_nPCCafePoint = 0;
    m_nDailyPCCafePoint = 0;
    m_nBonusCheckLevel = 0;
    m_bCanUsePCCafePoint = false;
    m_bPCCafeUser = false;
    m_nPVPPoint = 0;
    m_nInkEnergy = 0;
    m_nLastAttackTick = 0;
    m_dOverHitBonus = 0.0;
    m_bIsInCombatMode = false;
    std::fill(std::begin(m_sAI), std::end(m_sAI), char16_t{0});
    m_dAcquireSP = 0.0;
    m_nRaidPointAsset = 0;
    m_nAcquireRaidBossPoint = 0;
    m_nAgroRange = 450;
    m_bDecayComplete = false;
    m_dwSummonTime = 0;
    m_dwSummonLifeTime = 0;
    m_nDomainID = 0;
    m_nMasterExpRatio = 0;
    m_SummonerID = 0;
    m_BossID = 0;
    m_nClassID = 0;
    m_nWeightPoint = 0;
    m_nRespawnTime = 0;
    m_nClassIndex = 0;
    std::fill(std::begin(m_ClanId), std::end(m_ClanId), 0);
    m_nClanHelpRange = 0;
    m_dAcquireExpRate = 0.0;
    m_nPState = 0;
    m_pTarget = nullptr;
    m_spMaster = nullptr;
    m_bMoveTrouble = false;
    m_dwPathFindFailCount = 0;
    m_nCorpseTime = 0;
    m_bIsSpoil = false;
    m_bItemCollectable = false;
    m_nCanSweeper = 0;
    m_nSpoilerSmartID = 0;
    m_bIsPlunded = false;
    m_bIsSow = false;
    m_bIsSeed2 = false;
    m_nCanHarvest = 0;
    m_nSowerSmartID = 0;
    m_bUnsowing = false;
    m_nExCrtEffect = 0;
    flag = 0;
    m_rideClassID = 0;
    m_rideLevel = 0;
    m_Meal = 0;
    m_MaxMeal = 0;
    m_nPetDBID = 0;
    m_bPetActionBlocked = false;
    m_cNoSleepMode = 0;
    m_bCanAct = 0;
    m_nMaxMagicLevel = 0;
    m_bPassableDoor = false;
    m_bUnique = false;
    m_transformID = 0;
    m_nInstantZoneID = 0;
    m_nInstantZoneTypeID = 0;
    m_InstantZoneTypeID_InUse = 0;
    m_UltimateSkillGauge = 0;
    m_UltimateSkillLv = 0;
    m_tCreateDate = {};
    m_tGetGiftDate = {};
    m_nGetBirthdayGiftCount = 0;
    m_bDailyQuestFlag = false;
    m_nGet5YearGiftCount = 0;
    m_tGet5GiftDate = {};
    m_bTeleporting = false;
    std::fill(std::begin(m_StatusFlag), std::end(m_StatusFlag), false);
    m_nDominionSiegeId = 0;
    m_nDominionSiegeStatus = 0;
    m_AirShipId = 0;
    m_EvolutionId = 0;
    m_bDieEventSet = false;
    m_bRestartEventSet = false;
    m_nDieEventDistance = 0;
    m_nRestartEventDistance = 0;
    m_cPVPMatchTeamID = 0;
    m_nPrivateRespawnNoLog = 0;
    m_bStraightAttacker = false;
    m_bTurnToFollowee = false;
    m_nSummonAI = 0;
    std::fill(std::begin(m_AtomicQuestFlag), std::end(m_AtomicQuestFlag), std::int16_t{0});
    m_nDBValue = 0;
    m_nMakerParam1 = 0;
    m_nMakerParam2 = 0;
    m_nMakerParam3 = 0;
    for (int index = 0; index < 2; ++index) {
        m_MesTime[index] = 0;
        m_MesCount[index] = 0;
        m_InfectedMes[index] = false;
    }
    m_bCanMove = false;
    m_bAbsoluteDefence = false;
    m_bNoAttackDamage = false;
    m_bWeakenedDeathPenalty = false;
    // Current L2NPC.exe 0x00531e40 leaves m_nWorldId at +0x3c08
    // untouched between the initialized +0x3c06 and +0x3c0c fields.
    m_TrainingState = 0;
    m_TrainingLevel = 0;
    m_TrainingExp = 0;
    m_TrainingTime = 0;
    m_CountryCode = 0;
    m_bNpcDebugOn = false;
    m_bIsOlympiadApplicant = false;
    m_lNPCGiveItemJob = 0;

    m_moveType = static_cast<CreatureMoveType>(0);
    m_PVPMatchType = static_cast<PVPMatchType::Enum>(0);
    m_PVPMatchPlayerState = static_cast<PVPMatchPlayerState::Enum>(0);
    m_rideType = static_cast<RideType>(0);
    m_Action = static_cast<ActionType>(0);
    m_SummonType = static_cast<NpcSummonType>(0);
    m_AttackType = static_cast<AttackType>(0);
    m_ArmorType = static_cast<ArmorType>(0);
    m_AdditionalArmorType = static_cast<AdditionalArmorType>(0);
    m_BaseAttackType = static_cast<AttackType>(0);
    m_Race = static_cast<RaceType>(-1);
    m_nClass = static_cast<ClassType>(-1);
    m_Sex = static_cast<SexType>(0);
    m_MovingStatus = MSTE_MOVING;
    m_MoveMode = MT_SLOW;
    m_StopMode = STP_SIT;
    m_Environment = static_cast<EnvType>(0);
    m_nCreatureStart = 0;
    m_nCreatureEnd = 0;

    m_NeedUpdatePacketBitSet.Init(
        103, sizeof(m_bNeedUpdatePacketBits), reinterpret_cast<std::uint8_t*>(m_bNeedUpdatePacketBits)
    );
    std::fill(std::begin(m_nPacketSentCount), std::end(m_nPacketSentCount), 0);
}

CSharedCreatureData::~CSharedCreatureData() {
    m_pTarget = nullptr;
    m_spMaster = nullptr;
}

CSharedCreatureData* CSharedCreatureData::Next() { return CCreatureFactory::Inst().GetNext(index); }

CSharedCreatureConstant* CSharedCreatureData::GetConstant() { return CCreatureFactory::Inst().GetConstant(const_index); }

void CSharedCreatureData::SetName(const char16_t* value) {
    // NASC64_2015 sharedfactory.cpp:1097-1104. The target updates packet
    // bit zero only when the 25-code-unit inline value changes, then
    // terminates the last element.
    m_NeedUpdatePacketBitSet.SetStringValue_IfModifed(m_sName, sizeof(m_sName), value, 0);
    m_sName[24] = u'\0';
}

void CSharedCreatureData::SetRace(RaceType value) {
    if (m_Race == value) {
        return;
    }
    m_Race = value;
    m_NeedUpdatePacketBitSet.SetBit(1);
}

void CSharedCreatureData::SetSex(SexType value) {
    if (m_Sex == value) {
        return;
    }
    m_Sex = value;
    m_NeedUpdatePacketBitSet.SetBit(2);
}

void CSharedCreatureData::SetClass(ClassType value) {
    if (m_nClass == value) {
        return;
    }
    m_nClass = value;
    m_NeedUpdatePacketBitSet.SetBit(3);
}

void CSharedCreatureData::SetExp(std::int64_t value) { m_NeedUpdatePacketBitSet.SetInt64Value_IfModifed(m_nExp, value, 6); }

void CSharedCreatureData::SetLevel(int value) { m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nLevel, value, 7); }

void CSharedCreatureData::SetBaseHP(double value) { m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_dBaseHP, value, 8); }

void CSharedCreatureData::SetBaseMP(double value) { m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_dBaseMP, value, 9); }

void CSharedCreatureData::SetBaseCP(double value) {
    m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_dBaseCP, value, 10);
}

bool CSharedCreatureData::SetCarryWeight(double value) {
    return m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_dCarryWeight, value, 15);
}

void CSharedCreatureData::SetBuilder(int value) { m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nBuilder, value, 17); }

void CSharedCreatureData::SetFace(int value) { m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nFace, value, 18); }

void CSharedCreatureData::SetHairShape(int value) {
    m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nHairShape, value, 19);
}

void CSharedCreatureData::SetHairColor(int value) {
    m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nHairColor, value, 20);
}

void CSharedCreatureData::SetFaceEx(int value) { m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nFaceEx, value, 21); }

void CSharedCreatureData::SetHairShapeEx(int value) {
    m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nHairShapeEx, value, 22);
}

void CSharedCreatureData::SetHairColorEx(int value) {
    m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nHairColorEx, value, 23);
}

void CSharedCreatureData::SetRideType(RideType value) {
    if (m_rideType == value) {
        return;
    }
    m_rideType = value;
    m_NeedUpdatePacketBitSet.SetBit(24);
}

void CSharedCreatureData::SetRideClassID(int value) {
    m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_rideClassID, value, 25);
}

void CSharedCreatureData::SetPrivateStoreType(int value) {
    m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nPrivateStoreType, value, 26);
}

void CSharedCreatureData::SetCollisionRadius(double value) {
    m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_dCollisionRadius, value, 27);
}

void CSharedCreatureData::SetCollisionHeight(double value) {
    m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_dCollisionHeight, value, 28);
}

void CSharedCreatureData::SetPledgeType(std::uint32_t value) {
    m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nPledgeType, static_cast<std::int32_t>(value), 52);
}

void CSharedCreatureData::SetPledgeID(std::uint32_t value) {
    m_NeedUpdatePacketBitSet.SetUnsignedIntValue_IfModifed(m_nPledgeID, value, 46);
}

void CSharedCreatureData::SetAllianceID(std::uint32_t value) {
    m_NeedUpdatePacketBitSet.SetUnsignedIntValue_IfModifed(m_nAllianceId, value, 47);
}

void CSharedCreatureData::SetPKCount(int value) { m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nPKCount, value, 30); }

void CSharedCreatureData::SetPKPardonCount(int value) {
    m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nPKPardonCount, value, 29);
}

void CSharedCreatureData::SetDuelCount(int value) {
    m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nDuelCount, value, 31);
}

void CSharedCreatureData::SetAttackType(AttackType value) {
    if (m_AttackType == value) {
        return;
    }
    m_AttackType = value;
    m_NeedUpdatePacketBitSet.SetBit(36);
}

void CSharedCreatureData::SetAttackRange(int value) {
    m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nAttackRange, value, 37);
}

void CSharedCreatureData::SetGuility(char value) { m_NeedUpdatePacketBitSet.SetCharValue_IfModifed(m_cIsGuilty, value, 38); }

void CSharedCreatureData::SetHeroType(int value) { m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nHeroType, value, 39); }

void CSharedCreatureData::SetNoblessType(int value) {
    m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nNoblessType, value, 40);
}

void CSharedCreatureData::SetNoblessIndex(int value) {
    m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nNoblessIndex, value, 41);
}

void CSharedCreatureData::SetPVPPoint(int value) { m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nPVPPoint, value, 42); }

void CSharedCreatureData::SetRaidPoint(int value) {
    m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nRaidPointAsset, value, 43);
}

void CSharedCreatureData::SetEnvironment(int value) {
    const EnvType converted = static_cast<EnvType>(value);
    if (m_Environment == converted) {
        return;
    }
    m_Environment = converted;
    m_NeedUpdatePacketBitSet.SetBit(83);
}

void CSharedCreatureData::SetMoveMode(int value) {
    const MoveType converted = static_cast<MoveType>(value);
    if (m_MoveMode == converted) {
        return;
    }
    m_MoveMode = converted;
    m_NeedUpdatePacketBitSet.SetBit(84);
}

void CSharedCreatureData::SetUltimateSkillGauge(int value) {
    m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_UltimateSkillGauge, value, 89);
}

void CSharedCreatureData::UltimateSkillGauge(int value) {
    if (value < 0) {
        SetUltimateSkillGauge(0);
        return;
    }

    for (int index = 0; index < 4; ++index) {
        if (UltimateSkill::LevelInfo[index][0] <= m_nLevel && m_nLevel <= UltimateSkill::LevelInfo[index][1]) {
            SetUltimateSkillGauge(std::min(value, UltimateSkillLvTable[index]));
            return;
        }
    }
}

void CSharedCreatureData::SetMyAirShipID(std::uint32_t value) {
    m_NeedUpdatePacketBitSet.SetUnsignedIntValue_IfModifed(m_AirShipId, value, 65);
}

void CSharedCreatureData::SetMyShuttleID(std::uint32_t value) {
    m_NeedUpdatePacketBitSet.SetUnsignedIntValue_IfModifed(m_nShuttleId, value, 65);
}

void CSharedCreatureData::AddBaseAttribute(BaseAttribute& value) {
    m_BaseAttribute += value;
    m_NeedUpdatePacketBitSet.SetBit(36);
}

void CSharedCreatureData::SetAlive(bool value) {
    m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_bIsAlive, value ? 1 : 0, 97);
}

bool CSharedCreatureData::SetStats(CStats& value) {
    const bool same = m_Stat.IsSameValue(value);
    if (!same) {
        m_Stat = value;
        m_Stat.Fix();
        m_NeedUpdatePacketBitSet.SetBit(60);
    }
    return !same;
}

void CSharedCreatureData::SetOlympidApplicant(bool value) { m_bIsOlympiadApplicant = value; }

bool CSharedCreatureData::IsOlympiadApplicant() const { return m_bIsOlympiadApplicant; }

bool CSharedCreatureData::IsSummon() { return m_SummonType == NST_SUMMON; }

bool CSharedCreatureData::IsPet() { return m_SummonType == NST_PET; }

bool CSharedCreatureData::IsSummonOrPet() { return IsSummon() || IsPet(); }

bool CSharedCreatureData::GetEventFlag(NpcEventFlag flag) {
    if (flag != NEF_SEE_CREATURE) {
        throw std::out_of_range("invalid xarray<T> subscript");
    }
    return m_bEventFlag[0];
}

void CSharedCreatureData::SetEventFlag(NpcEventFlag flag, bool value) {
    if (flag != NEF_SEE_CREATURE) {
        throw std::out_of_range("invalid xarray<T> subscript");
    }
    m_bEventFlag[0] = value;
}

void CSharedCreatureData::InitFaction() {
    // SharedFactory.cpp:2453-2465. NASC64_2015.exe 0x007b7fd0.
    // The shipping function really executes an empty sixteen-iteration loop.
    for (int factionIndex = 0; factionIndex < 16; ++factionIndex) {
    }
}

bool CSharedCreatureData::UpdateFactionPoint(int factionId, std::int64_t factionPoint) {
    // SharedFactory.cpp:2469-2496. NASC64_2015.exe 0x007bc8c0.
    L2_GUARD_FUNCTION();
    const int factionIndex = factionId - 1;
    if (factionIndex < 0 || factionIndex >= 16) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid Faction ID",
        //     L"..\\Shared\\SharedFactory.cpp",
        //     0x9ab)
        // Log.Add(
        //     LOG_ERROR,
        //     "[faction] Cannot update points: faction ID %d is outside 1..16 (requested points %lld).",
        //     factionId, static_cast<long long>(factionPoint))
        L2_LOG_ERROR(
            Log, "[faction] Cannot update points: faction ID %d is outside 1..16 (requested points %lld).", factionId,
            static_cast<long long>(factionPoint)
        );
        return false;
    }

    // This temporary is present in the shipping machine code but is not
    // written back to m_nFactionPoint or m_nFactionLevel.
    const std::int64_t maximumPoint = FactionByLevel[factionIndex][FactionMaxLevel[factionIndex] - 1] - 1;
    [[maybe_unused]] const std::int32_t adjustedPoint = static_cast<std::int32_t>(std::min(factionPoint, maximumPoint));
    return true;
}

int CSharedCreatureData::FindUpperBoundIndex(int factionIndex, int factionPoint) {
    // SharedFactory.cpp:2502-2539. NASC64_2015.exe 0x007b3280.
    int lower = 0;
    int previousMiddle = -1;
    int upper = FactionMaxLevel[factionIndex] - 1;
    for (;;) {
        if (upper <= lower) {
            return upper;
        }

        const int middle = (lower + upper) / 2;
        if (middle == previousMiddle) {
            return upper;
        }
        previousMiddle = middle;

        if (FactionByLevel[factionIndex][middle] < factionPoint) {
            lower = middle + 1;
        } else {
            upper = middle;
            if (FactionByLevel[factionIndex][middle] <= factionPoint) {
                return middle + 1;
            }
        }
    }
}

bool CSharedCreatureData::IsLevelLimited(int factionId) {
    // SharedFactory.cpp:2543-2565. NASC64_2015.exe 0x007b8310.
    L2_GUARD_FUNCTION();
    const int factionIndex = factionId - 1;
    const std::int64_t maximumPoint = FactionByLevel[factionIndex][FactionMaxLevel[factionIndex] - 1] - 1;
    if (m_nFactionPoint[factionIndex] > maximumPoint) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Faction Point Overflow",
        //     L"..\\Shared\\SharedFactory.cpp",
        //     0x9f9)
        // Log.Add(
        //     LOG_ERROR,
        //     "[faction] Faction %d points exceed the limit of %lld. Treating the faction as level-limited.",
        //     factionId, static_cast<long long>(maximumPoint))
        L2_LOG_ERROR(
            Log, "[faction] Faction %d points exceed the limit of %lld. Treating the faction as level-limited.", factionId,
            static_cast<long long>(maximumPoint)
        );
        return true;
    }
    return m_nFactionPoint[factionIndex] == maximumPoint;
}

float CSharedCreatureData::GetFactionPercentageForCurrentLevel(int factionId) {
    // SharedFactory.cpp:2569-2597. NASC64_2015.exe 0x007b3e40.
    const int factionIndex = factionId - 1;
    const int factionLevel = m_nFactionLevel[factionIndex];
    if (factionLevel < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Faction Level Cannot less than 0",
        //     L"..\\Shared\\SharedFactory.cpp",
        //     0xa0e)
        // Log.Add(
        //     LOG_ERROR,
        //     "[faction] Cannot calculate progress for faction %d: level %d is negative. Returning zero progress.",
        //     factionId, factionLevel)
        L2_LOG_ERROR(
            Log, "[faction] Cannot calculate progress for faction %d: level %d is negative. Returning zero progress.",
            factionId, factionLevel
        );
        return 0.0F;
    }

    const std::int64_t currentLevelPoint = factionLevel == 0 ? 0 : FactionByLevel[factionIndex][factionLevel - 1];
    const std::int64_t levelSpan = FactionByLevel[factionIndex][factionLevel] - currentLevelPoint;
    if (levelSpan < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid Faction Level Table",
        //     L"..\\Shared\\SharedFactory.cpp",
        //     0xa1d)
        // Log.Add(
        //     LOG_ERROR,
        //     "[faction] Cannot calculate progress for faction %d, level %d: the level spans %lld points. A positive span is
        //     required, returning zero progress.", factionId, factionLevel, static_cast<long long>(levelSpan))
        L2_LOG_ERROR(
            Log,
            "[faction] Cannot calculate progress for faction %d, level %d: the level spans %lld points. A positive span is "
            "required; returning zero progress.",
            factionId, factionLevel, static_cast<long long>(levelSpan)
        );
        return 0.0F;
    }

    return static_cast<float>(m_nFactionPoint[factionIndex] - currentLevelPoint) / static_cast<float>(levelSpan);
}

std::int64_t CSharedCreatureData::GetFactionPoint(int factionId) { return m_nFactionPoint[factionId - 1]; }

int CSharedCreatureData::GetFactionLevel(int factionId) { return m_nFactionLevel[factionId - 1]; }

void CSharedCreatureData::SetHP(double value) {
    if (!FloatChecker::CheckValidation(value)) {
        // Noise: Variable storage addresses do not explain an invalid numeric value. Retain the values and floating-point
        // class. Log.Add(
        //     LOG_ERROR,
        //     L"FloatERR [%s] inval HP input:%.5f(%p) "
        //     L"fpclass:%04x base:%.5f(%p) now:%.5f(%p)",
        //     L"..\\Shared\\SharedFactory.cpp",
        //     value,
        //     static_cast<const void*>(&value),
        //     FloatChecker::FPClass(value),
        //     m_dBaseHP,
        //     static_cast<const void*>(&m_dBaseHP),
        //     m_dHP,
        //     static_cast<const void*>(&m_dHP))
        // Log.Add(
        //     LOG_ERROR,
        //     L"[vitals] HP validation failed. Will use the base value if finite, then apply bounds. Creature \"%ls\". Input
        //     %.17g (class 0x%04x), base %.17g, current %.17g.", SharedCreatureNameForLog(m_sName).c_str(), value,
        //     static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseHP, m_dHP)
        L2_LOG_ERROR(
            Log,
            u"[vitals] HP validation failed. Will use the base value if finite, then apply bounds. Creature \"%ls\"; input "
            u"%.17g (class 0x%04x), base %.17g, current %.17g.",
            m_sName, value, static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseHP, m_dHP
        );
        ReportCallStack(LOG_ERROR, false);
        if (FloatChecker::IsValid(m_dBaseHP)) {
            value = m_dBaseHP;
        }
    }
    if (value < 0.0) {
        // Noise: Variable storage addresses do not explain an invalid numeric value. Retain the values and floating-point
        // class. Log.Add(
        //     LOG_ERROR,
        //     L"FloatERR [%s] minus HP input:%.5f(%p) "
        //     L"fpclass:%04x base:%.5f(%p) now:%.5f(%p)",
        //     L"..\\Shared\\SharedFactory.cpp",
        //     value,
        //     static_cast<const void*>(&value),
        //     FloatChecker::FPClass(value),
        //     m_dBaseHP,
        //     static_cast<const void*>(&m_dBaseHP),
        //     m_dHP,
        //     static_cast<const void*>(&m_dHP))
        // Log.Add(
        //     LOG_ERROR,
        //     L"[vitals] Negative HP input. Clamping to zero. Creature \"%ls\". Input %.17g (class 0x%04x), base %.17g,
        //     current %.17g.", SharedCreatureNameForLog(m_sName).c_str(), value,
        //     static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseHP, m_dHP)
        L2_LOG_ERROR(
            Log,
            u"[vitals] Negative HP input. Clamping to zero. Creature \"%ls\"; input %.17g (class 0x%04x), base %.17g, "
            u"current %.17g.",
            m_sName, value, static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseHP, m_dHP
        );
        ReportCallStack(LOG_ERROR, false);
        value = 0.0;
    }
    if (m_dBaseHP <= value && value != m_dBaseHP && 0.0 < m_dBaseHP) {
        if (FloatChecker::FPClass(value) != 0x0100) {
            // Noise: Variable storage addresses do not explain an invalid numeric value. Retain the values and
            // floating-point class. Log.Add(
            //     LOG_ERROR,
            //     L"FloatERR [%s] huge HP input:%.5f(%p) "
            //     L"fpclass:%04x base:%.5f(%p) now:%.5f(%p)",
            //     L"..\\Shared\\SharedFactory.cpp",
            //     value,
            //     static_cast<const void*>(&value),
            //     FloatChecker::FPClass(value),
            //     m_dBaseHP,
            //     static_cast<const void*>(&m_dBaseHP),
            //     m_dHP,
            //     static_cast<const void*>(&m_dHP))
            // Log.Add(
            //     LOG_ERROR,
            //     L"[vitals] HP input exceeds its positive base. Clamping to the base value. Creature \"%ls\". Input %.17g
            //     (class 0x%04x), base %.17g, current %.17g.", SharedCreatureNameForLog(m_sName).c_str(), value,
            //     static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseHP, m_dHP)
            L2_LOG_ERROR(
                Log,
                u"[vitals] HP input exceeds its positive base. Clamping to the base value. Creature \"%ls\"; input %.17g "
                u"(class 0x%04x), base %.17g, current %.17g.",
                m_sName, value, static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseHP, m_dHP
            );
            ReportCallStack(LOG_ERROR, false);
        }
        value = m_dBaseHP;
    }
    m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_dHP, value, 11);
}

void CSharedCreatureData::ValidateHP(double value) {
    if (value <= m_dHP) {
        if (m_dBaseHP <= m_dHP && m_dHP != m_dBaseHP) {
            SetHP(m_dBaseHP);
        }
    } else {
        SetHP(value);
    }
}

void CSharedCreatureData::RestoreHP() { SetHP(m_dBaseHP); }

void CSharedCreatureData::SetMP(double value) {
    if (!FloatChecker::CheckValidation(value)) {
        // Noise: Variable storage addresses do not explain an invalid numeric value. Retain the values and floating-point
        // class. Log.Add(
        //     LOG_ERROR,
        //     L"FloatERR [%s] inval MP input:%.5f(%p) "
        //     L"fpclass:%04x base:%.5f(%p) now:%.5f(%p)",
        //     L"..\\Shared\\SharedFactory.cpp",
        //     value,
        //     static_cast<const void*>(&value),
        //     FloatChecker::FPClass(value),
        //     m_dBaseMP,
        //     static_cast<const void*>(&m_dBaseMP),
        //     m_dMP,
        //     static_cast<const void*>(&m_dMP))
        // Log.Add(
        //     LOG_ERROR,
        //     L"[vitals] MP validation failed. Will use the base value if finite, then apply bounds. Creature \"%ls\". Input
        //     %.17g (class 0x%04x), base %.17g, current %.17g.", SharedCreatureNameForLog(m_sName).c_str(), value,
        //     static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseMP, m_dMP)
        L2_LOG_ERROR(
            Log,
            u"[vitals] MP validation failed. Will use the base value if finite, then apply bounds. Creature \"%ls\"; input "
            u"%.17g (class 0x%04x), base %.17g, current %.17g.",
            m_sName, value, static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseMP, m_dMP
        );
        ReportCallStack(LOG_ERROR, false);
        if (FloatChecker::IsValid(m_dBaseMP)) {
            value = m_dBaseMP;
        }
    }
    if (value < 0.0) {
        // Noise: Variable storage addresses do not explain an invalid numeric value. Retain the values and floating-point
        // class. Log.Add(
        //     LOG_ERROR,
        //     L"FloatERR [%s] minus MP input:%.5f(%p) "
        //     L"fpclass:%04x base:%.5f(%p) now:%.5f(%p)",
        //     L"..\\Shared\\SharedFactory.cpp",
        //     value,
        //     static_cast<const void*>(&value),
        //     FloatChecker::FPClass(value),
        //     m_dBaseMP,
        //     static_cast<const void*>(&m_dBaseMP),
        //     m_dMP,
        //     static_cast<const void*>(&m_dMP))
        // Log.Add(
        //     LOG_ERROR,
        //     L"[vitals] Negative MP input. Clamping to zero. Creature \"%ls\". Input %.17g (class 0x%04x), base %.17g,
        //     current %.17g.", SharedCreatureNameForLog(m_sName).c_str(), value,
        //     static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseMP, m_dMP)
        L2_LOG_ERROR(
            Log,
            u"[vitals] Negative MP input. Clamping to zero. Creature \"%ls\"; input %.17g (class 0x%04x), base %.17g, "
            u"current %.17g.",
            m_sName, value, static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseMP, m_dMP
        );
        ReportCallStack(LOG_ERROR, false);
        value = 0.0;
    }
    if (m_dBaseMP <= value && value != m_dBaseMP && 0.0 < m_dBaseMP) {
        if (FloatChecker::FPClass(value) != 0x0100) {
            // Noise: Variable storage addresses do not explain an invalid numeric value. Retain the values and
            // floating-point class. Log.Add(
            //     LOG_ERROR,
            //     L"FloatERR [%s] huge MP input:%.5f(%p) "
            //     L"fpclass:%04x base:%.5f(%p) now:%.5f(%p)",
            //     L"..\\Shared\\SharedFactory.cpp",
            //     value,
            //     static_cast<const void*>(&value),
            //     FloatChecker::FPClass(value),
            //     m_dBaseMP,
            //     static_cast<const void*>(&m_dBaseMP),
            //     m_dMP,
            //     static_cast<const void*>(&m_dMP))
            // Log.Add(
            //     LOG_ERROR,
            //     L"[vitals] MP input exceeds its positive base. Clamping to the base value. Creature \"%ls\". Input %.17g
            //     (class 0x%04x), base %.17g, current %.17g.", SharedCreatureNameForLog(m_sName).c_str(), value,
            //     static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseMP, m_dMP)
            L2_LOG_ERROR(
                Log,
                u"[vitals] MP input exceeds its positive base. Clamping to the base value. Creature \"%ls\"; input %.17g "
                u"(class 0x%04x), base %.17g, current %.17g.",
                m_sName, value, static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseMP, m_dMP
            );
            ReportCallStack(LOG_ERROR, false);
        }
        value = m_dBaseMP;
    }
    m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_dMP, value, 12);
}

void CSharedCreatureData::ValidateMP(double value) {
    if (value <= m_dMP) {
        if (m_dBaseMP <= m_dMP && m_dMP != m_dBaseMP) {
            SetMP(m_dBaseMP);
        }
    } else {
        SetMP(value);
    }
}

void CSharedCreatureData::RestoreMP() { SetMP(m_dBaseMP); }

void CSharedCreatureData::SetSP(std::int64_t value) { m_NeedUpdatePacketBitSet.SetInt64Value_IfModifed(m_nSP, value, 14); }

double CSharedCreatureData::GetCP() const { return m_dCP; }

void CSharedCreatureData::SetCP(double value) {
    if (!FloatChecker::CheckValidation(value)) {
        // Noise: Variable storage addresses do not explain an invalid numeric value. Retain the values and floating-point
        // class. Log.Add(
        //     LOG_ERROR,
        //     L"FloatERR [%s] inval CP input:%.5f(%p) "
        //     L"fpclass:%04x base:%.5f(%p) now:%.5f(%p)",
        //     L"..\\Shared\\SharedFactory.cpp",
        //     value,
        //     static_cast<const void*>(&value),
        //     FloatChecker::FPClass(value),
        //     m_dBaseCP,
        //     static_cast<const void*>(&m_dBaseCP),
        //     m_dCP,
        //     static_cast<const void*>(&m_dCP))
        // Log.Add(
        //     LOG_ERROR,
        //     L"[vitals] CP validation failed. Will use the base value if finite, then apply bounds. Creature \"%ls\". Input
        //     %.17g (class 0x%04x), base %.17g, current %.17g.", SharedCreatureNameForLog(m_sName).c_str(), value,
        //     static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseCP, m_dCP)
        L2_LOG_ERROR(
            Log,
            u"[vitals] CP validation failed. Will use the base value if finite, then apply bounds. Creature \"%ls\"; input "
            u"%.17g (class 0x%04x), base %.17g, current %.17g.",
            m_sName, value, static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseCP, m_dCP
        );
        ReportCallStack(LOG_ERROR, false);
        if (FloatChecker::IsValid(m_dBaseCP)) {
            value = m_dBaseCP;
        }
    }
    if (value < 0.0) {
        // Noise: Variable storage addresses do not explain an invalid numeric value. Retain the values and floating-point
        // class. Log.Add(
        //     LOG_ERROR,
        //     L"FloatERR [%s] minus CP input:%.5f(%p) "
        //     L"fpclass:%04x base:%.5f(%p) now:%.5f(%p)",
        //     L"..\\Shared\\SharedFactory.cpp",
        //     value,
        //     static_cast<const void*>(&value),
        //     FloatChecker::FPClass(value),
        //     m_dBaseCP,
        //     static_cast<const void*>(&m_dBaseCP),
        //     m_dCP,
        //     static_cast<const void*>(&m_dCP))
        // Log.Add(
        //     LOG_ERROR,
        //     L"[vitals] Negative CP input. Clamping to zero. Creature \"%ls\". Input %.17g (class 0x%04x), base %.17g,
        //     current %.17g.", SharedCreatureNameForLog(m_sName).c_str(), value,
        //     static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseCP, m_dCP)
        L2_LOG_ERROR(
            Log,
            u"[vitals] Negative CP input. Clamping to zero. Creature \"%ls\"; input %.17g (class 0x%04x), base %.17g, "
            u"current %.17g.",
            m_sName, value, static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseCP, m_dCP
        );
        ReportCallStack(LOG_ERROR, false);
        value = 0.0;
    }
    if (m_dBaseCP <= value && value != m_dBaseCP && 0.0 < m_dBaseCP) {
        if (FloatChecker::FPClass(value) != 0x0100) {
            // Noise: Variable storage addresses do not explain an invalid numeric value. Retain the values and
            // floating-point class. Log.Add(
            //     LOG_ERROR,
            //     L"FloatERR [%s] huge CP input:%.5f(%p) "
            //     L"fpclass:%04x base:%.5f(%p) now:%.5f(%p)",
            //     L"..\\Shared\\SharedFactory.cpp",
            //     value,
            //     static_cast<const void*>(&value),
            //     FloatChecker::FPClass(value),
            //     m_dBaseCP,
            //     static_cast<const void*>(&m_dBaseCP),
            //     m_dCP,
            //     static_cast<const void*>(&m_dCP))
            // Log.Add(
            //     LOG_ERROR,
            //     L"[vitals] CP input exceeds its positive base. Clamping to the base value. Creature \"%ls\". Input %.17g
            //     (class 0x%04x), base %.17g, current %.17g.", SharedCreatureNameForLog(m_sName).c_str(), value,
            //     static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseCP, m_dCP)
            L2_LOG_ERROR(
                Log,
                u"[vitals] CP input exceeds its positive base. Clamping to the base value. Creature \"%ls\"; input %.17g "
                u"(class 0x%04x), base %.17g, current %.17g.",
                m_sName, value, static_cast<unsigned>(FloatChecker::FPClass(value)), m_dBaseCP, m_dCP
            );
            ReportCallStack(LOG_ERROR, false);
        }
        value = m_dBaseCP;
    }
    m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_dCP, value, 13);
}

void CSharedCreatureData::ValidateCP(double value) {
    if (m_dCP <= value && value != m_dCP) {
        SetCP(value);
    }
    if (m_dBaseCP <= m_dCP && m_dCP != m_dBaseCP) {
        SetCP(m_dBaseCP);
    }
}

void CSharedCreatureData::RestoreCP() { SetCP(m_dBaseCP); }

void CSharedCreatureData::CopyFrom(CSharedCreatureData* source) {
    std::memcpy(
        &m_nCreatureStart, &source->m_nCreatureStart,
        offsetof(CSharedCreatureData, m_nCreatureEnd) - offsetof(CSharedCreatureData, m_nCreatureStart)
    );
    m_NeedUpdatePacketBitSet.Init(
        103, sizeof(m_bNeedUpdatePacketBits), reinterpret_cast<std::uint8_t*>(m_bNeedUpdatePacketBits)
    );
    m_NeedUpdatePacketBitSet.SetAllBits();
}

int CSharedCreatureData::ConvertToInternalOneTimeQuestID(int questId) const {
    if (questId < 10255) {
        return questId;
    }
    if (questId < 12048) {
        return questId - 10000;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"current onetime quest id(%d) is over "
    //     L"onetimequest_end_id(%d)!!",
    //     questId,
    //     12047)
    // Log.Add(
    //     LOG_ERROR,
    //     "[quest] One-time quest ID %d exceeds the supported maximum %d. Returning internal ID 0.",
    //     questId, 12047)
    L2_LOG_ERROR(
        Log, "[quest] One-time quest ID %d exceeds the supported maximum %d. Returning internal ID 0.", questId, 12047
    );
    return 0;
}

void CSharedCreatureData::SetOneTimeFlag(int questId, bool value) {
    m_OneTimeQuestBitFlag.SetFlag(ConvertToInternalOneTimeQuestID(questId), value);
}

bool CSharedCreatureData::GetOneTimeFlag(int questId) const {
    return m_OneTimeQuestBitFlag.GetFlag(ConvertToInternalOneTimeQuestID(questId));
}

void CSharedCreatureData::GetOneTimeFlagAll(char* destination, int destinationSize) const {
    if (static_cast<std::uint32_t>(destinationSize) <= 0xffU) {
        return;
    }
    m_OneTimeQuestBitFlag.GetFlagAll(destination, static_cast<std::uint64_t>(static_cast<std::uint32_t>(destinationSize)));
}

void CSharedCreatureData::SetOneTimeFlagAll(char* source) { m_OneTimeQuestBitFlag.SetFlagAll(source); }

bool CSharedCreatureData::AddAbnormalStatus(AbnormalStatusSlotType type, std::uint8_t level, int magicLevel) {
    for (AbnormalData& abnormal : m_AbnormalStatus) {
        if (static_cast<int>(abnormal.m_Type) != -1) {
            continue;
        }
        abnormal.m_Type = type;
        abnormal.m_cAbnormalLevel = level;
        abnormal.m_nMagicLevel = magicLevel;
        return true;
    }
    return false;
}

bool CSharedCreatureData::DeleteAbnormalStatus(AbnormalStatusSlotType type) {
    for (AbnormalData& abnormal : m_AbnormalStatus) {
        if (abnormal.m_Type != type) {
            continue;
        }
        abnormal.m_Type = static_cast<AbnormalStatusSlotType>(-1);
        return true;
    }
    return false;
}

void CSharedCreatureData::DeleteAllAbnormalStatus() {
    for (AbnormalData& abnormal : m_AbnormalStatus) {
        abnormal.m_Type = static_cast<AbnormalStatusSlotType>(-1);
    }
}

bool CSharedCreatureData::IsAbnormalType(AbnormalStatusSlotType type) {
    return std::any_of(std::begin(m_AbnormalStatus), std::end(m_AbnormalStatus), [type](const AbnormalData& abnormal) {
        return abnormal.m_Type == type;
    });
}

bool CSharedCreatureData::SetAbnormalLevel(AbnormalStatusSlotType type, std::uint8_t level) {
    for (AbnormalData& abnormal : m_AbnormalStatus) {
        if (abnormal.m_Type != type) {
            continue;
        }
        abnormal.m_cAbnormalLevel = level;
        return true;
    }
    m_NeedUpdatePacketBitSet.SetBit(75);
    return false;
}

int CSharedCreatureData::GetAbnormalLevel(AbnormalStatusSlotType type) {
    L2_GUARD_FUNCTION();
    for (const AbnormalData& abnormal : m_AbnormalStatus) {
        if (abnormal.m_Type == type) {
            return abnormal.m_cAbnormalLevel;
        }
    }
    return -1;
}

int CSharedCreatureData::GetAbnormalMagicLevel(AbnormalStatusSlotType type) {
    L2_GUARD_FUNCTION();
    for (const AbnormalData& abnormal : m_AbnormalStatus) {
        if (abnormal.m_Type == type) {
            return abnormal.m_nMagicLevel;
        }
    }
    return -1;
}

void CSharedCreatureData::SetAP(int value, bool updatePacket) {
    m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nAP, value, 100);
}

int CSharedCreatureData::GetAP() const { return m_nAP; }

int CSharedCreatureData::GetAcquiredAbilityCount() const {
    return static_cast<std::int32_t>(
        static_cast<std::uint32_t>(m_stPotentialCount.nCount1) + static_cast<std::uint32_t>(m_stPotentialCount.nCount2) +
        static_cast<std::uint32_t>(m_stPotentialCount.nCount3)
    );
}

int CSharedCreatureData::GetRemainAP() const {
    const int remaining = m_nAP - m_stPotentialCount.nCount3 - m_stPotentialCount.nCount2 - m_stPotentialCount.nCount1;
    return std::clamp(remaining, 0, 36);
}

int CSharedCreatureData::CalcAPByLevel() const { return std::clamp(m_nLevel - 84, 0, 36); }

bool CSharedCreatureData::SetMemo(QuestType questType, int questId) {
    L2_GUARD_FUNCTION();
    if (HaveMemo(questType, questId)) {
        return false;
    }

    MemoStruct* current = GetMemoStructArray(questType);
    if (!current) {
        return false;
    }
    MemoStruct* const end = current + GetMaxMemoCount(questType);
    while (current != end && current->nQuestID != 0) {
        ++current;
    }
    if (current == end) {
        const std::wstring creatureName = Utf16ToWide(m_sName);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] SetMemo() failed. memo[%d] "
        //     L"not enough slot on creature[%s]",
        //     __WFILE__,
        //     __LINE__,
        //     questId,
        //     creatureName.c_str())
        // Log.Add(
        //     LOG_WARN,
        //     L"[quest] Cannot add quest %d (type %d) for \"%ls\": no free memo slot.",
        //     questId, static_cast<int>(questType), creatureName.c_str())
        L2_LOG_WARN(
            Log, u"[quest] Cannot add quest %d (type %d) for \"%ls\": no free memo slot.", questId,
            static_cast<int>(questType), creatureName.c_str()
        );
        return false;
    }

    current->nQuestID = questId;
    current->nJournal = 1;
    current->nState = 0;
    current->nState2 = 0;
    current->nState3 = 0;
    current->nState4 = 0;
    current->bChanged = true;
    return true;
}

bool CSharedCreatureData::SetMemo(int questId) {
    L2_GUARD_FUNCTION();
    return SetMemo(QUEST_NORMAL, questId);
}

bool CSharedCreatureData::SetMemoState(QuestType questType, int questId, int slot, int value) {
    L2_GUARD_FUNCTION();
    if (!HaveMemo(questType, questId)) {
        return false;
    }

    MemoStruct* current = GetMemoStructArray(questType);
    if (!current) {
        return false;
    }
    MemoStruct* const end = current + GetMaxMemoCount(questType);
    while (current != end && current->nQuestID != questId) {
        ++current;
    }
    if (current == end) {
        const std::wstring creatureName = Utf16ToWide(m_sName);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] SetMemoState failed. no memo[%d] "
        //     L"slot[%d] state[%d] on creature[%s]",
        //     __WFILE__,
        //     __LINE__,
        //     questId,
        //     slot,
        //     value,
        //     creatureName.c_str())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[quest] Cannot set state slot %d to %d for quest %d (type %d) on \"%ls\": memo not found.",
        //     slot, value, questId, static_cast<int>(questType), creatureName.c_str())
        L2_LOG_ERROR(
            Log, u"[quest] Cannot set state slot %d to %d for quest %d (type %d) on \"%ls\": memo not found.", slot, value,
            questId, static_cast<int>(questType), creatureName.c_str()
        );
        return false;
    }

    switch (slot) {
        case 0:
            current->nState = value;
            break;
        case 1:
            current->nState2 = value;
            break;
        case 2:
            current->nState3 = value;
            break;
        case 3:
            current->nState4 = value;
            break;
        default: {
            const std::wstring creatureName = Utf16ToWide(m_sName);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] SetMemoState, slot[%d] is invalid "
            //     L"on creature[%s]",
            //     __WFILE__,
            //     __LINE__,
            //     slot,
            //     creatureName.c_str())
            // Log.Add(
            //     LOG_ERROR,
            //     L"[quest] Cannot set state slot %d to %d for quest %d (type %d) on \"%ls\": valid slots are 0..3.",
            //     slot, value, questId, static_cast<int>(questType), creatureName.c_str())
            L2_LOG_ERROR(
                Log, u"[quest] Cannot set state slot %d to %d for quest %d (type %d) on \"%ls\": valid slots are 0..3.",
                slot, value, questId, static_cast<int>(questType), creatureName.c_str()
            );
            return false;
        }
    }
    current->bChanged = true;
    return true;
}

bool CSharedCreatureData::SetMemoState(int questId, int slot, int value) {
    L2_GUARD_FUNCTION();
    return SetMemoState(QUEST_NORMAL, questId, slot, value);
}

bool CSharedCreatureData::SetJournal(QuestType questType, int questId, int value) {
    L2_GUARD_FUNCTION();
    MemoStruct* current = GetMemoStructArray(questType);
    if (!current) {
        return false;
    }
    MemoStruct* const end = current + GetMaxMemoCount(questType);
    while (current != end && current->nQuestID != questId) {
        ++current;
    }
    if (current == end) {
        const std::wstring creatureName = Utf16ToWide(m_sName);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] SetJournal() failed. no memo[%d] "
        //     L"on creature[%s]",
        //     __WFILE__,
        //     __LINE__,
        //     questId,
        //     creatureName.c_str())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[quest] Cannot set journal to %d for quest %d (type %d) on \"%ls\": memo not found.",
        //     value, questId, static_cast<int>(questType), creatureName.c_str())
        L2_LOG_ERROR(
            Log, u"[quest] Cannot set journal to %d for quest %d (type %d) on \"%ls\": memo not found.", value, questId,
            static_cast<int>(questType), creatureName.c_str()
        );
        return false;
    }
    current->nJournal = value;
    return true;
}

bool CSharedCreatureData::SetJournal(int questId, int value) {
    L2_GUARD_FUNCTION();
    return SetJournal(QUEST_NORMAL, questId, value);
}

bool CSharedCreatureData::SetFlagJournal(QuestType questType, int questId, int flag) {
    L2_GUARD_FUNCTION();
    MemoStruct* current = GetMemoStructArray(questType);
    if (!current) {
        return false;
    }
    MemoStruct* const end = current + GetMaxMemoCount(questType);
    while (current != end && current->nQuestID != questId) {
        ++current;
    }
    if (current == end) {
        const std::wstring creatureName = Utf16ToWide(m_sName);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] SetFlagJournal() failed. no memo[%d] "
        //     L"on creature[%s], flag[%d]",
        //     __WFILE__,
        //     __LINE__,
        //     questId,
        //     creatureName.c_str(),
        //     flag)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[quest] Cannot set journal flag %d for quest %d (type %d) on \"%ls\": memo not found.",
        //     flag, questId, static_cast<int>(questType), creatureName.c_str())
        L2_LOG_ERROR(
            Log, u"[quest] Cannot set journal flag %d for quest %d (type %d) on \"%ls\": memo not found.", flag, questId,
            static_cast<int>(questType), creatureName.c_str()
        );
        return false;
    }
    const std::uint32_t bit = 1U << ((static_cast<std::uint32_t>(flag) - 1U) & 31U);
    current->nJournal = static_cast<std::int32_t>(static_cast<std::uint32_t>(current->nJournal) | bit | 0x80000000U);
    return true;
}

bool CSharedCreatureData::SetFlagJournal(int questId, int flag) {
    L2_GUARD_FUNCTION();
    return SetFlagJournal(QUEST_NORMAL, questId, flag);
}

bool CSharedCreatureData::ResetFlagJournal(QuestType questType, int questId, int flag) {
    L2_GUARD_FUNCTION();
    MemoStruct* current = GetMemoStructArray(questType);
    if (!current) {
        return false;
    }
    MemoStruct* const end = current + GetMaxMemoCount(questType);
    while (current != end && current->nQuestID != questId) {
        ++current;
    }
    if (current == end) {
        const std::wstring creatureName = Utf16ToWide(m_sName);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ResetFlagJournal() failed. no memo[%d] "
        //     L"on creature[%s], flag[%d]",
        //     __WFILE__,
        //     __LINE__,
        //     questId,
        //     creatureName.c_str(),
        //     flag)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[quest] Cannot clear journal flag %d for quest %d (type %d) on \"%ls\": memo not found.",
        //     flag, questId, static_cast<int>(questType), creatureName.c_str())
        L2_LOG_ERROR(
            Log, u"[quest] Cannot clear journal flag %d for quest %d (type %d) on \"%ls\": memo not found.", flag, questId,
            static_cast<int>(questType), creatureName.c_str()
        );
        return false;
    }
    const std::uint32_t bit = 1U << ((static_cast<std::uint32_t>(flag) - 1U) & 31U);
    current->nJournal = static_cast<std::int32_t>(static_cast<std::uint32_t>(current->nJournal) & ~bit);
    return true;
}

bool CSharedCreatureData::ResetFlagJournal(int questId, int flag) {
    L2_GUARD_FUNCTION();
    return ResetFlagJournal(QUEST_NORMAL, questId, flag);
}

bool CSharedCreatureData::RemoveMemo(QuestType questType, int questId) {
    L2_GUARD_FUNCTION();
    MemoStruct* current = GetMemoStructArray(questType);
    if (!current) {
        return false;
    }
    MemoStruct* const end = current + GetMaxMemoCount(questType);
    while (current != end && current->nQuestID != questId) {
        ++current;
    }
    if (current == end) {
        const std::wstring creatureName = Utf16ToWide(m_sName);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] RemoveMemo() failed. no memo[%d] "
        //     L"on creature[%s]",
        //     __WFILE__,
        //     __LINE__,
        //     questId,
        //     creatureName.c_str())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[quest] Cannot remove quest %d (type %d) from \"%ls\": memo not found.",
        //     questId, static_cast<int>(questType), creatureName.c_str())
        L2_LOG_ERROR(
            Log, u"[quest] Cannot remove quest %d (type %d) from \"%ls\": memo not found.", questId,
            static_cast<int>(questType), creatureName.c_str()
        );
        return false;
    }

    current->nQuestID = 0;
    current->nState = 0;
    current->nState2 = 0;
    current->nState3 = 0;
    current->nState4 = 0;
    current->nJournal = 1;
    current->bDeleted = true;
    for (UserNPCLog& log : current->NPCLog) {
        log.userLogType = USER_NPC_LOG_MAX;
        log.nNPCID = 0;
        base_structure_detail::InterlockedStore(&log.nCount, 0);
    }
    return true;
}

bool CSharedCreatureData::RemoveMemo(int questId) {
    L2_GUARD_FUNCTION();
    return RemoveMemo(QUEST_NORMAL, questId);
}

int CSharedCreatureData::GetMaxMemoCount(QuestType questType) {
    L2_GUARD_FUNCTION();
    if (questType == QUEST_NORMAL) {
        return 41;
    }
    if (questType == QUEST_NONREGIST) {
        return 20;
    }
    return 0;
}

MemoStruct* CSharedCreatureData::GetMemoStructArray(QuestType questType) {
    L2_GUARD_FUNCTION();
    if (questType == QUEST_NORMAL) {
        return m_Memo;
    }
    if (questType == QUEST_NONREGIST) {
        return m_NRMemo;
    }
    return nullptr;
}

MemoStruct* CSharedCreatureData::GetMemoStruct(QuestType questType, int questId) {
    L2_GUARD_FUNCTION();
    MemoStruct* current = GetMemoStructArray(questType);
    if (!current) {
        return nullptr;
    }

    MemoStruct* const end = current + GetMaxMemoCount(questType);
    while (current != end && current->nQuestID != questId) {
        ++current;
    }
    // The shipping source returns the end iterator when the quest is absent.
    return current;
}

bool CSharedCreatureData::HaveMemo(QuestType questType, int questId) {
    L2_GUARD_FUNCTION();
    MemoStruct* current = GetMemoStructArray(questType);
    if (!current) {
        return false;
    }

    MemoStruct* const first = current;
    const int searchCount = GetMaxMemoCount(questType);
    while (current != first + searchCount && current->nQuestID != questId) {
        ++current;
    }
    return current != first + GetMaxMemoCount(questType);
}

bool CSharedCreatureData::HaveMemo(int questId) {
    L2_GUARD_FUNCTION();
    return HaveMemo(QUEST_NORMAL, questId) || HaveMemo(QUEST_NONREGIST, questId);
}

QuestType CSharedCreatureData::GetQuestType(int questId) {
    L2_GUARD_FUNCTION();
    if (HaveMemo(QUEST_NORMAL, questId)) {
        return QUEST_NORMAL;
    }
    if (HaveMemo(QUEST_NONREGIST, questId)) {
        return QUEST_NONREGIST;
    }
    return QUEST_MAX;
}

int CSharedCreatureData::GetMemoState(QuestType questType, int questId, int slot) {
    L2_GUARD_FUNCTION();
    MemoStruct* current = GetMemoStructArray(questType);
    if (!current) {
        return -1;
    }

    MemoStruct* const first = current;
    const int searchCount = GetMaxMemoCount(questType);
    while (current != first + searchCount && current->nQuestID != questId) {
        ++current;
    }
    if (current == first + GetMaxMemoCount(questType)) {
        return -1;
    }

    switch (slot) {
        case 0:
            return current->nState;
        case 1:
            return current->nState2;
        case 2:
            return current->nState3;
        case 3:
            return current->nState4;
        default:
            return -1;
    }
}

int CSharedCreatureData::GetMemoState(int questId, int slot) {
    L2_GUARD_FUNCTION();
    return GetMemoState(QUEST_NORMAL, questId, slot);
}

int CSharedCreatureData::GetUserNPCLog(QuestType questType, int questId, int npcId, UserNPCLogType userLogType) {
    L2_GUARD_FUNCTION();
    if (!HaveMemo(questType, questId)) {
        return 0;
    }

    MemoStruct* current = GetMemoStructArray(questType);
    if (!current) {
        return 0;
    }
    MemoStruct* const first = current;
    const int searchCount = GetMaxMemoCount(questType);
    while (current != first + searchCount && current->nQuestID != questId) {
        ++current;
    }
    if (current == first + GetMaxMemoCount(questType)) {
        return 0;
    }

    for (const UserNPCLog& log : current->NPCLog) {
        const int count = log.userLogType == userLogType && log.nNPCID == npcId ? log.nCount : 0;
        if (count != 0) {
            return count;
        }
    }
    return 0;
}

int CSharedCreatureData::GetUserNPCLog(int questId, int npcId, UserNPCLogType userLogType) {
    L2_GUARD_FUNCTION();
    return GetUserNPCLog(GetQuestType(questId), questId, npcId, userLogType);
}

bool CSharedCreatureData::SetUserNPCLog(QuestType questType, int questId, int npcId, UserNPCLogType userLogType, int value) {
    L2_GUARD_FUNCTION();
    if (!HaveMemo(questType, questId)) {
        return false;
    }

    MemoStruct* current = GetMemoStructArray(questType);
    if (!current) {
        return false;
    }
    MemoStruct* const first = current;
    const int searchCount = GetMaxMemoCount(questType);
    while (current != first + searchCount && current->nQuestID != questId) {
        ++current;
    }
    if (current == first + GetMaxMemoCount(questType)) {
        return false;
    }
    return current->UpdateNPCLog(userLogType, npcId, value);
}

bool CSharedCreatureData::SetUserNPCLog(int questId, int npcId, UserNPCLogType userLogType, int value) {
    L2_GUARD_FUNCTION();
    return SetUserNPCLog(GetQuestType(questId), questId, npcId, userLogType, value);
}

int CSharedCreatureData::IncreaseUserNPCLog(
    QuestType questType, int questId, int npcId, UserNPCLogType userLogType, int maximum
) {
    L2_GUARD_FUNCTION();
    if (!HaveMemo(questType, questId)) {
        return 0;
    }

    MemoStruct* current = GetMemoStructArray(questType);
    if (!current) {
        return 0;
    }
    MemoStruct* const first = current;
    const int searchCount = GetMaxMemoCount(questType);
    while (current != first + searchCount && current->nQuestID != questId) {
        ++current;
    }
    if (current == first + GetMaxMemoCount(questType)) {
        return 0;
    }
    return current->IncreaseNPCLog(userLogType, npcId, maximum);
}

int CSharedCreatureData::IncreaseUserNPCLog(int questId, int npcId, UserNPCLogType userLogType, int maximum) {
    L2_GUARD_FUNCTION();
    return IncreaseUserNPCLog(GetQuestType(questId), questId, npcId, userLogType, maximum);
}

bool CSharedCreatureData::DeleteUserNPCLog(QuestType questType, int questId, int npcId, UserNPCLogType userLogType) {
    L2_GUARD_FUNCTION();
    if (!HaveMemo(questType, questId)) {
        return false;
    }

    MemoStruct* current = GetMemoStructArray(questType);
    if (!current) {
        return false;
    }
    MemoStruct* const first = current;
    const int searchCount = GetMaxMemoCount(questType);
    while (current != first + searchCount && current->nQuestID != questId) {
        ++current;
    }
    if (current == first + GetMaxMemoCount(questType)) {
        return false;
    }
    return current->DeleteNPCLog(userLogType, npcId);
}

bool CSharedCreatureData::DeleteUserNPCLog(int questId, int npcId, UserNPCLogType userLogType) {
    L2_GUARD_FUNCTION();
    return DeleteUserNPCLog(GetQuestType(questId), questId, npcId, userLogType);
}

bool CSharedCreatureData::SetNRMemo(int questId) {
    L2_GUARD_FUNCTION();
    return SetMemo(QUEST_NONREGIST, questId);
}

bool CSharedCreatureData::SetNRMemoState(int questId, int slot, int value) {
    L2_GUARD_FUNCTION();
    return SetMemoState(QUEST_NONREGIST, questId, slot, value);
}

bool CSharedCreatureData::SetNRJournal(int questId, int value) {
    L2_GUARD_FUNCTION();
    return SetJournal(QUEST_NONREGIST, questId, value);
}

bool CSharedCreatureData::SetNRFlagJournal(int questId, int flag) {
    L2_GUARD_FUNCTION();
    return SetFlagJournal(QUEST_NONREGIST, questId, flag);
}

bool CSharedCreatureData::ResetNRFlagJournal(int questId, int flag) {
    L2_GUARD_FUNCTION();
    return ResetFlagJournal(QUEST_NONREGIST, questId, flag);
}

bool CSharedCreatureData::RemoveNRMemo(int questId) {
    L2_GUARD_FUNCTION();
    return RemoveMemo(QUEST_NONREGIST, questId);
}

bool CSharedCreatureData::HaveNRMemo(int questId) {
    L2_GUARD_FUNCTION();
    return HaveMemo(QUEST_NONREGIST, questId);
}

int CSharedCreatureData::GetNRMemoState(int questId, int slot) {
    L2_GUARD_FUNCTION();
    return GetMemoState(QUEST_NONREGIST, questId, slot);
}

int CSharedCreatureData::CheckAndSetOneTimeUserFlag(int questId, CSharedCreatureData* creature) {
    if (!creature) {
        return -1;
    }

    User* const user = User::GetObjectW(creature->m_nId);
    if (!user) {
        return -1;
    }

    L2_GUARD(user->m_lock);
    const std::int16_t* pending = user->m_data->m_AtomicQuestFlag;
    for (int index = 0; index < 5; ++index) {
        if (static_cast<int>(pending[index]) == questId) {
            return -1;
        }
    }

    const bool alreadySet = GetOneTimeFlag(questId);
    if (alreadySet) {
        return 1;
    }

    pending = user->m_data->m_AtomicQuestFlag;
    for (int index = 0; index < 5; ++index) {
        if (pending[index] != 0) {
            continue;
        }
        user->m_data->m_AtomicQuestFlag[index] = static_cast<std::int16_t>(questId);
        return 0;
    }
    return -1;
}

void CSharedCreatureData::InitDailyQuest() {
    L2_GUARD_FUNCTION();
    for (DailyQuestState& state : m_DailyQuest) {
        state.nQuestID = 0;
        state.nCompleteTime = 0;
    }
}

bool CSharedCreatureData::GetDailyQuestFlag(int questId) {
    L2_GUARD_FUNCTION();
    const DailyQuestState* record = nullptr;
    for (const DailyQuestState& candidate : m_DailyQuest) {
        if (candidate.nQuestID == questId) {
            record = &candidate;
            break;
        }
    }
    if (!record) {
        return true;
    }

    const std::int32_t now = runtime::crt::Time32Now();
    const std::tm* const local = runtime::crt::LocalTime32(now);
    std::int32_t elapsed = runtime::AddInt32Wrapping(
        runtime::MultiplyInt32Wrapping(local->tm_hour, 3600),
        runtime::AddInt32Wrapping(runtime::MultiplyInt32Wrapping(local->tm_min, 60), local->tm_sec)
    );
    if (local->tm_hour < 6 || (local->tm_hour == 6 && local->tm_min < 30)) {
        elapsed = runtime::AddInt32Wrapping(elapsed, 86400);
    }
    const std::int32_t boundary = runtime::AddInt32Wrapping(runtime::SubtractInt32Wrapping(now, elapsed), 23400);
    return boundary >= record->nCompleteTime;
}

bool CSharedCreatureData::GetWeeklyQuestFlag(int questId) {
    L2_GUARD_FUNCTION();
    const DailyQuestState* record = nullptr;
    for (const DailyQuestState& candidate : m_DailyQuest) {
        if (candidate.nQuestID == questId) {
            record = &candidate;
            break;
        }
    }
    if (!record) {
        return true;
    }

    const std::int32_t now = runtime::crt::Time32Now();
    const std::tm* const local = runtime::crt::LocalTime32(now);
    // The third term is the target's real condition: 06:00-06:29 takes the
    // pre-reset branch on every weekday.
    const bool beforeReset =
        local->tm_wday < 3 || (local->tm_wday == 3 && local->tm_hour < 6) || (local->tm_hour == 6 && local->tm_min < 30);
    const std::int32_t daysSinceWednesday =
        beforeReset ? runtime::AddInt32Wrapping(local->tm_wday, 4) : runtime::AddInt32Wrapping(local->tm_wday, -3);
    const std::int32_t elapsed = runtime::AddInt32Wrapping(
        runtime::MultiplyInt32Wrapping(daysSinceWednesday, 86400),
        runtime::AddInt32Wrapping(
            runtime::MultiplyInt32Wrapping(local->tm_hour, 3600),
            runtime::AddInt32Wrapping(runtime::MultiplyInt32Wrapping(local->tm_min, 60), local->tm_sec)
        )
    );
    const std::int32_t boundary = runtime::AddInt32Wrapping(runtime::SubtractInt32Wrapping(now, elapsed), 23400);
    return boundary >= record->nCompleteTime;
}

std::int32_t CSharedCreatureData::GetDailyBindTime() {
    const std::int32_t now = runtime::crt::Time32Now();
    const std::tm* const local = runtime::crt::LocalTime32(now);
    std::int32_t elapsed = runtime::AddInt32Wrapping(
        runtime::MultiplyInt32Wrapping(local->tm_hour, 3600),
        runtime::AddInt32Wrapping(runtime::MultiplyInt32Wrapping(local->tm_min, 60), local->tm_sec)
    );
    if (local->tm_hour < 6 || (local->tm_hour == 6 && local->tm_min < 30)) {
        elapsed = runtime::AddInt32Wrapping(elapsed, 86400);
    }
    return runtime::AddInt32Wrapping(runtime::SubtractInt32Wrapping(now, elapsed), 23400);
}

std::int32_t CSharedCreatureData::GetWeeklyBindTime() {
    const std::int32_t now = runtime::crt::Time32Now();
    const std::tm* const local = runtime::crt::LocalTime32(now);
    const bool beforeReset =
        local->tm_wday < 3 || (local->tm_wday == 3 && local->tm_hour < 6) || (local->tm_hour == 6 && local->tm_min < 30);
    const std::int32_t daysSinceWednesday =
        beforeReset ? runtime::AddInt32Wrapping(local->tm_wday, 4) : runtime::AddInt32Wrapping(local->tm_wday, -3);
    const std::int32_t elapsed = runtime::AddInt32Wrapping(
        runtime::MultiplyInt32Wrapping(daysSinceWednesday, 86400),
        runtime::AddInt32Wrapping(
            runtime::MultiplyInt32Wrapping(local->tm_hour, 3600),
            runtime::AddInt32Wrapping(runtime::MultiplyInt32Wrapping(local->tm_min, 60), local->tm_sec)
        )
    );
    return runtime::AddInt32Wrapping(runtime::SubtractInt32Wrapping(now, elapsed), 23400);
}

std::vector<int> CSharedCreatureData::GetCompletedDailyQuestsAfterBindTime(std::int32_t bindTime) {
    std::vector<int> result;
    result.reserve(300);
    for (const DailyQuestState& state : m_DailyQuest) {
        if (state.nQuestID > 0 && bindTime < state.nCompleteTime) {
            result.push_back(state.nQuestID);
        }
    }
    return result;
}

void CSharedCreatureData::CollectVar(ScriptTypeInfo* typeInfo) {
    // Current L2NPC.exe FUN_00533298 (0x00533298). The matching PDB names
    // this CSharedCreatureData::CollectVar and resolves the four otherwise
    // ambiguous script types. Order and the duplicate final builder_level
    // registration are both present in the shipping image. The target also
    // dereferences typeInfo unconditionally.
    struct Variable {
        const wchar_t* name;
        int offset;
        ScriptTypeId type;
    };
    static constexpr Variable variables[] = {
        {L"subjob_id", 0x58, ScriptTypeId::Int},
        {L"nobless_type", 0x6c, ScriptTypeId::Int},
        {L"hero_type", 0x70, ScriptTypeId::Int},
        {L"subjob0_class", 0x78, ScriptTypeId::Int},
        {L"subjob1_class", 0x7c, ScriptTypeId::Int},
        {L"subjob2_class", 0x80, ScriptTypeId::Int},
        {L"subjob3_class", 0x84, ScriptTypeId::Int},
        {L"subjob0_level", 0x5c, ScriptTypeId::Int},
        {L"subjob1_level", 0x60, ScriptTypeId::Int},
        {L"subjob2_level", 0x64, ScriptTypeId::Int},
        {L"subjob3_level", 0x68, ScriptTypeId::Int},
        {L"dual_class", 0x88, ScriptTypeId::Int},
        {L"age", 0x4c, ScriptTypeId::Int},
        {L"level", 0x3240, ScriptTypeId::Int},
        {L"alive", 0x48, ScriptTypeId::Int},
        {L"hp", 0x200, ScriptTypeId::Double},
        {L"max_hp", 0x3288, ScriptTypeId::Double},
        {L"mp", 0x208, ScriptTypeId::Double},
        {L"max_mp", 0x3290, ScriptTypeId::Double},
        {L"name", 0x218, ScriptTypeId::String},
        {L"ai", 0x3488, ScriptTypeId::String},
        {L"is_pledge_master", 0x30c0, ScriptTypeId::Int},
        {L"pledge_id", 0x30b8, ScriptTypeId::Int},
        {L"world_id", 0x3c08, ScriptTypeId::Int},
        {L"pledge_type", 0x30c4, ScriptTypeId::Int},
        {L"grade_id", 0x30d0, ScriptTypeId::Int},
        {L"is_pc", 0x44, ScriptTypeId::Int},
        {L"occupation", 0x94, ScriptTypeId::Int},
        {L"faceEx", 0xbc, ScriptTypeId::Int},
        {L"hairshapeEx", 0xc0, ScriptTypeId::Int},
        {L"haircolorEx", 0xc4, ScriptTypeId::Int},
        {L"npc_class_id", 0x358c, ScriptTypeId::Int},
        {L"sp", 0xd0, ScriptTypeId::Int64},
        {L"class_id", 0x358c, ScriptTypeId::Int},
        {L"weight_point", 0x3590, ScriptTypeId::Int},
        {L"respawn_time", 0x3594, ScriptTypeId::Int},
        {L"race", 0x90, ScriptTypeId::Int},
        {L"karma", 0x2f4, ScriptTypeId::Int},
        {L"last_blow_weapon_class_id", 0x33d8, ScriptTypeId::Int},
        {L"clan_help_range", 0x35b4, ScriptTypeId::Int},
        {L"p_state", 0x35c0, ScriptTypeId::Int},
        {L"target_index", 0x2dc, ScriptTypeId::Int},
        {L"target_id", 0x2e0, ScriptTypeId::Int},
        {L"boss_id", 0x3588, ScriptTypeId::Int},
        {L"flag", 0x3608, ScriptTypeId::Int},
        {L"attack_type", 0x32dc, ScriptTypeId::Int},
        {L"residence_id", 0x30fc, ScriptTypeId::Int},
        {L"equiped_weapon_class_id", 0x32d8, ScriptTypeId::Int},
        {L"quest_last_reward_time", 0x30ac, ScriptTypeId::Int},
        {L"meal", 0x3618, ScriptTypeId::Int},
        {L"master", 0x35d0, ScriptTypeId::Creature},
        {L"max_magic_level", 0x3628, ScriptTypeId::Int},
        {L"param1", 0x3be0, ScriptTypeId::Int},
        {L"param2", 0x3be4, ScriptTypeId::Int},
        {L"param3", 0x3be8, ScriptTypeId::Int},
        {L"db_value", 0x3bdc, ScriptTypeId::Int},
        {L"is_freezed", 0x2e5, ScriptTypeId::Int},
        {L"pet_dbid", 0x3620, ScriptTypeId::Int},
        {L"pk_count", 0x2ec, ScriptTypeId::Int},
        {L"stop_mode", 0x254, ScriptTypeId::Int},
        {L"action", 0x35d8, ScriptTypeId::Int},
        {L"yongma_type", 0x360c, ScriptTypeId::Int},
        {L"yongma_class", 0x3610, ScriptTypeId::Int},
        {L"ssq_round", 0x33dc, ScriptTypeId::Int},
        {L"ssq_part", 0x33e0, ScriptTypeId::Int},
        {L"seal_selection_no", 0x33e8, ScriptTypeId::Int},
        {L"ssq_dawn_round", 0x342c, ScriptTypeId::Int},
        {L"in_peacezone", 0x3430, ScriptTypeId::Int},
        {L"in_battlefield", 0x3434, ScriptTypeId::Int},
        {L"strider_level", 0x3614, ScriptTypeId::Int},
        {L"summon_type", 0x357c, ScriptTypeId::Int},
        {L"summoner_id", 0x3584, ScriptTypeId::Int},
        {L"dbid", 0x31a4, ScriptTypeId::Int},
        {L"builder_level", 0x33c8, ScriptTypeId::Int},
        {L"death_penalty_level", 0x319c, ScriptTypeId::Int},
        {L"transformID", 0x3630, ScriptTypeId::Int},
        {L"instant_zone_id", 0x3634, ScriptTypeId::Int},
        {L"instant_zone_type_id", 0x3638, ScriptTypeId::Int},
        {L"instant_zone_type_id_in_use", 0x363c, ScriptTypeId::Int},
        {L"hp_regen", 0x32b8, ScriptTypeId::Double},
        {L"absolute_defence", 0x3c03, ScriptTypeId::Int},
        {L"no_attack_damage", 0x3c04, ScriptTypeId::Int},
        {L"straight_attacker", 0x36b8, ScriptTypeId::Int},
        {L"turn_to_followee", 0x36b9, ScriptTypeId::Int},
        {L"summon_ai", 0x36bc, ScriptTypeId::Int},
        {L"builder_level", 0x33c8, ScriptTypeId::Int},
    };

    for (const Variable& variable : variables) {
        AddSharedScriptVariable(*typeInfo, variable.name, variable.offset, variable.type);
    }
    CSharedObject::CollectVar(typeInfo);
}

void CSharedObject::CollectVar(ScriptTypeInfo* typeInfo) {
    AddSharedScriptVariable(*typeInfo, L"id", 0x24, ScriptTypeId::Int);
    AddSharedScriptVariable(*typeInfo, L"x", 0x08, ScriptTypeId::Double);
    AddSharedScriptVariable(*typeInfo, L"y", 0x10, ScriptTypeId::Double);
    AddSharedScriptVariable(*typeInfo, L"z", 0x18, ScriptTypeId::Double);
}

ScriptAccessibleEx* CSharedObject::GetScriptAccessibleEx() const { return const_cast<CSharedObject*>(this); }

template <>
AllianceFactory::CSharedFactory() {
    runtime::OpenDefaultAllianceSharedFactory(*this);
}

template <>
AllianceFactory::~CSharedFactory() {
    runtime::SharedFactoryMappingAccess::Detach(*this);
}

template <>
AllianceFactory& AllianceFactory::Inst() {
    static AllianceFactory inst;
    return inst;
}

template <>
CStaticObjectFactory::CSharedFactory() {
    runtime::OpenDefaultStaticObjectSharedFactory(*this);
}

template <>
CStaticObjectFactory::~CSharedFactory() {
    runtime::SharedFactoryMappingAccess::Detach(*this);
}

template <>
CStaticObjectFactory& CStaticObjectFactory::Inst() {
    static CStaticObjectFactory inst;
    return inst;
}

template <>
CAirShipFactory::CSharedFactory() {
    runtime::OpenDefaultAirShipSharedFactory(*this);
}

template <>
CAirShipFactory::~CSharedFactory() {
    runtime::SharedFactoryMappingAccess::Detach(*this);
}

template <>
CAirShipFactory& CAirShipFactory::Inst() {
    static CAirShipFactory inst;
    return inst;
}

template <>
CCreatureFactory::CSharedFactory() {
    runtime::OpenDefaultCreatureSharedFactory(*this);
}

template <>
CCreatureFactory::~CSharedFactory() {
    runtime::SharedFactoryMappingAccess::Detach(*this);
}

template <>
CCreatureFactory& CCreatureFactory::Inst() {
    static CCreatureFactory inst;
    return inst;
}

template <>
CCreature* CCreatureFactory::GetObjectW(int indexValue) {
    // shared/SharedFactory.h, current L2NPC.exe 0x0044149c inner path:
    // validate the shared slot, translate it through
    // L2World_CreatureIndex, then resolve that SID in the process-local
    // CSmartIdManager.
    const int smartId = GetIdByIndex(indexValue);
    return smartId == -1 ? nullptr : CCreature::GetObjectW(static_cast<std::uint32_t>(smartId));
}

template <>
CSPointer<CCreature> CCreatureFactory::FindObjectSP(int indexValue) {
    // NASC64_2015 PDB: shared/SharedFactory.h:2538-2546.
    // The current image contains the same body at 0x0044149c.
    CSPointer<CCreature> result;
    CCreature* const object = GetObjectW(indexValue);
    if (object) {
        result.SetObject(object, ObjectRefType::FindObject);
    }
    return result;
}

template <>
CPartyFactory::CSharedFactory() {
    runtime::OpenDefaultPartySharedFactory(*this);
}

template <>
CPartyFactory::~CSharedFactory() {
    runtime::SharedFactoryMappingAccess::Detach(*this);
}

template <>
CPartyFactory& CPartyFactory::Inst() {
    static CPartyFactory inst;
    return inst;
}

template <>
CPledgeFactory::CSharedFactory() {
    runtime::OpenDefaultPledgeSharedFactory(*this);
}

template <>
CPledgeFactory::~CSharedFactory() {
    runtime::SharedFactoryMappingAccess::Detach(*this);
}

template <>
CPledgeFactory& CPledgeFactory::Inst() {
    static CPledgeFactory inst;
    return inst;
}

template <>
CGeneralObjectFactory::CSharedFactory() {
    runtime::OpenDefaultGeneralObjectSharedFactory(*this);
}

template <>
CGeneralObjectFactory::~CSharedFactory() {
    runtime::SharedFactoryMappingAccess::Detach(*this);
}

template <>
CGeneralObjectFactory& CGeneralObjectFactory::Inst() {
    static CGeneralObjectFactory inst;
    return inst;
}

template <>
CItemFactory::CSharedFactory() {
    runtime::OpenDefaultItemSharedFactory(*this);
}

template <>
CItemFactory::~CSharedFactory() {
    runtime::SharedFactoryMappingAccess::Detach(*this);
}

template <>
CItemFactory& CItemFactory::Inst() {
    static CItemFactory inst;
    return inst;
}

CSharedAllianceData* CSharedAllianceData::Next() { return AllianceFactory::Inst().GetNext(index); }

void CSharedAllianceData::CopyFrom(const CSharedAllianceData* source) {
    if (!source) {
        return;
    }
    std::memcpy(
        &m_nAllianceStart, &source->m_nAllianceStart,
        offsetof(CSharedAllianceData, m_nAllianceEnd) - offsetof(CSharedAllianceData, m_nAllianceStart)
    );
}

CSharedStaticObjectData* CSharedStaticObjectData::Next() { return CStaticObjectFactory::Inst().GetNext(index); }

void CSharedStaticObjectData::CopyFrom(const CSharedStaticObjectData* source) {
    if (!source) {
        return;
    }
    std::memcpy(
        &m_nStaticObjectStart, &source->m_nStaticObjectStart,
        offsetof(CSharedStaticObjectData, m_nStaticObjectEnd) - offsetof(CSharedStaticObjectData, m_nStaticObjectStart)
    );
}

CSharedPartyData* CSharedPartyData::Next() { return CPartyFactory::Inst().GetNext(index); }

void CSharedPartyData::CopyFrom(const CSharedPartyData* source) {
    if (!source) {
        return;
    }
    std::memcpy(
        &m_nPartyStart, &source->m_nPartyStart,
        offsetof(CSharedPartyData, m_nPartyEnd) - offsetof(CSharedPartyData, m_nPartyStart)
    );
}

void CSharedPartyData::SetPVPMatchStart(int teamIndex) {
    m_bPVPMatchStart = true;
    m_nPVPMatchTeamIndex = teamIndex;
}

void CSharedPartyData::SetPVPMatchEnd() { m_bPVPMatchStart = false; }

bool CSharedPartyData::IsPVPMatching() const { return m_bPVPMatchStart; }

void CSharedPartyData::CollectVar(ScriptTypeInfo* typeInfo) {
    AddSharedScriptVariable(*typeInfo, L"master_dbid", 0x4c, ScriptTypeId::Int);
    AddSharedScriptVariable(*typeInfo, L"member_count", 0x50, ScriptTypeId::Int);
    CSharedObject::CollectVar(typeInfo);
}

CSharedPledgeData::CSharedPledgeData() : m_gradePowerList{}, m_subPledgeTypeBits(0) {}

CSharedPledgeData* CSharedPledgeData::Next() { return CPledgeFactory::Inst().GetNext(index); }

void CSharedPledgeData::CopyFrom(const CSharedPledgeData* source) {
    if (!source) {
        return;
    }
    std::memcpy(
        &m_nPledgeStart, &source->m_nPledgeStart,
        offsetof(CSharedPledgeData, m_nPledgeEnd) - offsetof(CSharedPledgeData, m_nPledgeStart)
    );
}

void CSharedPledgeData::CollectVar(ScriptTypeInfo* typeInfo) {
    struct Variable {
        const wchar_t* name;
        int offset;
        ScriptTypeId type;
    };
    static constexpr Variable variables[] = {
        {L"db_id", 0x44, ScriptTypeId::Int},
        {L"name", 0x48, ScriptTypeId::String},
        {L"master_name", 0x7c, ScriptTypeId::String},
        {L"skill_level", 0xc4, ScriptTypeId::Int},
        {L"status", 0xdc, ScriptTypeId::Int},
        {L"is_guilty", 0xe0, ScriptTypeId::Bool},
        {L"member_count", 0x150, ScriptTypeId::Int},
        {L"total_member_count", 0x5bc, ScriptTypeId::Int},
        {L"castle_id", 0xe8, ScriptTypeId::Int},
        {L"agit_id", 0xec, ScriptTypeId::Int},
        {L"cagit_id", 0xf0, ScriptTypeId::Int},
        {L"fortress_id", 0xf4, ScriptTypeId::Int},
        {L"prev_server", 0x7b8, ScriptTypeId::Int},
        {L"airship_count", 0x7ec, ScriptTypeId::Int},
    };
    for (const Variable& variable : variables) {
        AddSharedScriptVariable(*typeInfo, variable.name, variable.offset, variable.type);
    }
    CSharedObject::CollectVar(typeInfo);
}

CSharedGeneralObjectData* CSharedGeneralObjectData::Next() { return CGeneralObjectFactory::Inst().GetNext(index); }

void CSharedGeneralObjectData::CopyFrom(const CSharedGeneralObjectData* source) {
    if (!source) {
        return;
    }
    std::memcpy(
        &m_nGeneralObjectStart, &source->m_nGeneralObjectStart,
        offsetof(CSharedGeneralObjectData, m_nGeneralObjectEnd) - offsetof(CSharedGeneralObjectData, m_nGeneralObjectStart)
    );
}

void CSharedGeneralObjectData::CollectVar(ScriptTypeInfo* typeInfo) {
    AddSharedScriptVariable(*typeInfo, L"party_count", 0x48, ScriptTypeId::Int);
    AddSharedScriptVariable(*typeInfo, L"mpcc_id", 0x24, ScriptTypeId::Int);
    CSharedObject::CollectVar(typeInfo);
}

int CSharedItemData::GetEnchanted() const { return m_nEnchanted; }

void CSharedItemData::SetEnchanted(int value) { m_nEnchanted = value; }

std::int16_t CSharedItemData::GetLocked() const { return m_hLocked; }

void CSharedItemData::SetLocked(std::int16_t value) { m_hLocked = value; }

bool CSharedItemData::IsVariationItem() const { return m_bIsVariation; }

bool CSharedItemData::IsStackable(ItemConsumeType consumeType) {
    return consumeType == ICT_STACKABLE || consumeType == ICT_ASSET;
}

bool CSharedItemData::IsStackable() const { return IsStackable(m_ItemConsumeType); }

CSharedItemData* CSharedItemData::Next() { return CItemFactory::Inst().GetNext(index); }

void CSharedItemData::CopyFrom(const CSharedItemData* source) {
    if (!source) {
        return;
    }
    std::memcpy(
        &m_nItemStart, &source->m_nItemStart, offsetof(CSharedItemData, m_nItemEnd) - offsetof(CSharedItemData, m_nItemStart)
    );
    const_index = source->const_index;
}

CSharedItemConstant* CSharedItemData::GetConstant() { return CItemFactory::Inst().GetConstant(const_index); }

SlotType CSharedItemData::GetSlotType() const {
    for (int slot = ST_UNDERWEAR; slot < ST_MAX; ++slot) {
        if ((static_cast<std::uint64_t>(m_Slot) & (std::uint64_t{1} << slot)) != 0) {
            return static_cast<SlotType>(slot);
        }
    }
    return ST_NONE;
}

void CSharedItemData::CollectVar(ScriptTypeInfo* typeInfo) {
    AddSharedScriptVariable(*typeInfo, L"cls", 0x58, ScriptTypeId::Int);
    // Current L2NPC.exe 0x00516fb4 registers both names for the same field.
    AddSharedScriptVariable(*typeInfo, L"class_id", 0x58, ScriptTypeId::Int);
    AddSharedScriptVariable(*typeInfo, L"count", 0x48, ScriptTypeId::Int64);
    AddSharedScriptVariable(*typeInfo, L"dbid", 0x54, ScriptTypeId::Int);
    AddSharedScriptVariable(*typeInfo, L"pet_level", 0x74, ScriptTypeId::Int);
    CSharedObject::CollectVar(typeInfo);
}

int CSharedItemData::GetShapeShiftedClassId() const { return m_nShapeShiftedClassId; }

void CSharedItemData::SetShapeShiftedClassId(int value) { m_nShapeShiftedClassId = value; }

std::int16_t CSharedItemData::GetUseCount() const { return m_hUseCount; }

void CSharedItemData::SetUseCount(std::int16_t value) { m_hUseCount = value; }

#endif

#if defined(L2_BUILD_L2SERVER)
#include "Creature.h"
#include "Item.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include "Shared/src/Exception.h"

#include <cstdlib>
#include <cwchar>
#include <unordered_map>

template <>
CSPointer<CCreature> CCreatureFactory::FindObjectSP(int indexValue) {
    CSPointer<CCreature> result;
    if (indexValue < 0 || indexValue >= GetSize()) {
        return result;
    }

    CCreature* const object = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(GetIdByIndex(indexValue)));
    if (object != nullptr) {
        result.SetObject(object, ObjectRefType::FindObject);
    }
    return result;
}

template <>
CSPointer<CItem> CItemFactory::FindObjectSP(int indexValue) {
    CSPointer<CItem> result;
    if (indexValue < 0 || indexValue >= GetSize()) {
        return result;
    }

    CItem* const object = ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(GetIdByIndex(indexValue)));
    if (object != nullptr) {
        result.SetObject(object, ObjectRefType::FindObject);
    }
    return result;
}

template <>
CItem* CItemFactory::GetObjectW(int indexValue) {
    const int smartId = GetIdByIndex(indexValue);
    return smartId == -1 ? nullptr : ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(smartId));
}

namespace l2server_factory_detail {

[[noreturn]] void FailAdd(
    const CSharedDefine& define,
    AddFailure failure,
    std::int32_t value,
    std::int32_t previousValue,
    const std::source_location location
) {
    CrashInfo::ReasonType reason = CrashInfo::SHARED_FACTORY_IS_EMPTY;
    const wchar_t* guardName = L"shared factory ran out!";

    switch (failure) {
        case AddFailure::FactoryEmpty:
            break;
        case AddFailure::FactoryIndexInvalid:
            guardName = L"shared factory index is invalid!";
            reason = CrashInfo::SHARED_FACTORY_INDEX_IS_INVALID;
            break;
        case AddFailure::FreeIndexInvalid:
            guardName = L"shared memory free index invalid!";
            reason = CrashInfo::SHARED_FACTORY_FREE_INDEX_IS_INVALID;
            break;
    }

    GuardHelper failureGuard(guardName, CThreadManager::GetThreadID());
    switch (failure) {
        case AddFailure::FactoryEmpty:
            // Log.Add(
            //     LOG_LOCAL,
            //     L"Shared factory for %s ran out!",
            //     define.DataMMFName)
            // Previous: L2_LOG(Log, LOG_LOCAL,
            //     L"Shared factory for %s ran out!", define.DataMMFName)
            L2_LOG(
                Log, LOG_LOCAL,
                u"[shared factory] '%s' has no free entries; a crash report "
                u"will be generated.",
                define.DataMMFName
            );
            // Log.Add(
            //     LOG_ERROR,
            //     L"Shared factory for %s ran out!",
            //     define.DataMMFName)
            // Previous: L2_LOG_ERROR(Log,
            //     L"Shared factory for %s ran out!", define.DataMMFName)
            L2_LOG_ERROR(
                Log,
                u"[shared factory] '%s' has no free entries; a crash report "
                u"will be generated.",
                define.DataMMFName
            );
            break;
        case AddFailure::FactoryIndexInvalid:
            // Log.Add(
            //     LOG_LOCAL,
            //     "Shared factory Index is Invalid 0~[%d]~%d!",
            //     value,
            //     define.max_data)
            // Previous: L2_LOG(Log, LOG_LOCAL,
            //     "Shared factory Index is Invalid 0~[%d]~%d!",
            //     value, define.max_data)
            L2_LOG(
                Log, LOG_LOCAL,
                "[shared factory] Index %d is outside the valid range 0..%d; "
                "a crash report will be generated.",
                value, define.max_data - 1
            );
            // Log.Add(
            //     LOG_ERROR,
            //     "Shared factory Index is Invalid 0~[%d]~%d!",
            //     value,
            //     define.max_data)
            // Previous: L2_LOG_ERROR(Log,
            //     "Shared factory Index is Invalid 0~[%d]~%d!",
            //     value, define.max_data)
            L2_LOG_ERROR(
                Log,
                "[shared factory] Index %d is outside the valid range 0..%d; "
                "a crash report will be generated.",
                value, define.max_data - 1
            );
            break;
        case AddFailure::FreeIndexInvalid:
            // Log.Add(
            //     LOG_LOCAL,
            //     "Shared Memory Free Index Invalid 0~[%d]~%d! "
            //     "(last free_index:%d)",
            //     value,
            //     define.max_data,
            //     previousValue)
            // Previous: L2_LOG(
            //     Log, LOG_LOCAL,
            //     "Shared Memory Free Index Invalid 0~[%d]~%d! "
            //     "(last free_index:%d)",
            //     value, define.max_data, previousValue)
            L2_LOG(
                Log, LOG_LOCAL,
                "[shared factory] Free index %d is outside the valid range "
                "0..%d (previous free index %d); a crash report will be "
                "generated.",
                value, define.max_data - 1, previousValue
            );
            // Log.Add(
            //     LOG_ERROR,
            //     "Shared Memory Free Index Invalid 0~[%d]~%d! "
            //     "(last free_index:%d)",
            //     value,
            //     define.max_data,
            //     previousValue)
            // Previous: L2_LOG_ERROR(
            //     Log,
            //     "Shared Memory Free Index Invalid 0~[%d]~%d! "
            //     "(last free_index:%d)",
            //     value, define.max_data, previousValue)
            L2_LOG_ERROR(
                Log,
                "[shared factory] Free index %d is outside the valid range "
                "0..%d (previous free index %d); a crash report will be "
                "generated.",
                value, define.max_data - 1, previousValue
            );
            break;
    }

    ForceCrash(reason, location);
    std::abort();
}

void ReportRemoveMinusOne(std::int32_t indexValue) {
    // Log.Add(
    //     LOG_ERROR,
    //     "CSharedFactory<T, ST, CT, CONSTANT>::RemoveByIndex() : "
    //     "Invalid index detected!! %d",
    //     indexValue)
    // Previous: L2_LOG_ERROR(
    //     Log,
    //     "CSharedFactory<T, ST, CT, CONSTANT>::RemoveByIndex() : "
    //     "Invalid index detected!! %d",
    //     indexValue)
    L2_LOG_ERROR(
        Log,
        "[shared factory] Cannot remove index %d: -1 is invalid; "
        "reporting the call stack.",
        indexValue
    );
    runtime::process::ReportCallStack(2, true);
}

void ReportListFailure(const char* message) {
    // Log.Add(LOG_ERROR, message)
    // L2_LOG_ERROR(Log, message)
    L2_LOG_ERROR(Log, "%s", message);
}

void ReportListFailure(const wchar_t* message) {
    // Log.Add(LOG_ERROR, message)
    // L2_LOG_ERROR(Log, message)
    L2_LOG_ERROR(Log, u"%ls", message);
}

void ReportListCycleFailure(const std::source_location location) {
    // Log.Add(
    //     LOG_ERROR,
    //     "infinite loop generated! at file[%s], line[%d]",
    //     "C:\\Work\\Lineage2Build\\Server\\Main\\Shared\\SharedFactory.h",
    //     line)
    // Previous: L2_LOG_ERROR(
    //     Log, "infinite loop generated! at file[%s], line[%d]",
    //     "C:\\Work\\Lineage2Build\\Server\\Main\\Shared\\SharedFactory.h",
    //     line)
    // Previous: L2_LOG_ERROR(
    //     Log,
    //     "[shared factory] Linked-list cycle detected in %s:%d; traversal "
    //     "stopped.",
    //     "C:\\Work\\Lineage2Build\\Server\\Main\\Shared\\SharedFactory.h", line)
    L2_LOG_ERROR(
        Log,
        "[shared factory] Linked-list cycle detected in %s:%u; traversal "
        "stopped.",
        ::source_location::FileName(location.file_name()), static_cast<unsigned>(location.line())
    );
}

} // namespace l2server_factory_detail

#endif // L2_BUILD_L2SERVER
