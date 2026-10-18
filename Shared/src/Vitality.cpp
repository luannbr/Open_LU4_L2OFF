#include "Shared/src/Vitality.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/DBConn.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/UserConst.h"

#if defined(L2_BUILD_L2SERVER)
#include "CConfig.h"
#include "DB.h"
#include "NPC.h"
#include "User.h"
#include "UserSocket.h"
#elif defined(L2_BUILD_CACHED)
#include "User.h"
#include "UserDb.h"
#endif

#include <cmath>
#include <cstring>
#include <ctime>

namespace {

constexpr int kResetPeriodSeconds = 0x93a80;
constexpr int kResetBindingDayMask = 8;
constexpr int kResetMinuteOfDay = 0x186;
constexpr int kResetVitalPoint = 140000;


std::int32_t ResetDate(std::int32_t now, std::int32_t dueTime, bool wasNeverReset) {
    std::int32_t resetDate = ((now - dueTime) / kResetPeriodSeconds) * kResetPeriodSeconds + dueTime;

    std::tm resetLocal{};
    if (runtime::crt::LocalTime32(resetDate, resetLocal)) {
        resetLocal.tm_hour = 6;
        resetLocal.tm_min = 30;
        resetLocal.tm_sec = 0;
        resetDate = runtime::crt::Mktime32(&resetLocal);
    }

    if (wasNeverReset) {
        std::tm nowLocal{};
        if (runtime::crt::LocalTime32(now, nowLocal)) {
            const int elapsedMinutes = ResetBindingDayHelper::FindLatestResetBindingDay(
                kResetBindingDayMask, kResetMinuteOfDay, nowLocal.tm_wday, nowLocal.tm_hour * 60 + nowLocal.tm_min
            );
            resetDate = now - elapsedMinutes * 60;
        }
    }
    return resetDate;
}

void SaveData(int worldId, int charDbId, int subjobId, const CVitalData& data) {
    DBConn connection(worldId);
    // Current helper 0x00433684 binds the two identifiers as SQL_INTEGER,
    // then routes all four CVitalData values through helper 0x0043238c:
    // SQL_C_SLONG -> SQL_DOUBLE. `long` selects that exact DBConn binding
    // on every portable host while retaining the target's int32 value.
    const long vitalPoint = data.vitalPoint;
    const long restoreCount = data.restoreCount;
    const long latestResetDateVP = data.latestResetDate_VP;
    const long latestResetDateRC = data.latestResetDate_RC;

    constexpr const wchar_t* query = L"{Call lin_SaveCharVitality (?, ?, ?, ?, ?, ?)}";
    connection.Prepare(query);
    connection.BindParameter(&charDbId);
    connection.BindParameter(&subjobId);
    connection.BindParameter(&vitalPoint);
    connection.BindParameter(&restoreCount);
    connection.BindParameter(&latestResetDateVP);
    connection.BindParameter(&latestResetDateRC);
    connection.ExecutePrepared(query);
}

} // namespace

CVitalData::CVitalData(const CVitalData& other) noexcept :
    vitalPoint(other.vitalPoint),
    restoreCount(other.restoreCount),
    latestResetDate_VP(other.latestResetDate_VP),
    latestResetDate_RC(other.latestResetDate_RC) {}

CVitalData& CVitalData::operator=(const CVitalData& other) noexcept {
    if (this != &other) {
        vitalPoint = other.vitalPoint;
        restoreCount = other.restoreCount;
        latestResetDate_VP = other.latestResetDate_VP;
        latestResetDate_RC = other.latestResetDate_RC;
    }
    return *this;
}

void CVitalData::Init(int newVitalPoint, int newRestoreCount, int newLatestResetDateVP, int newLatestResetDateRC) {
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::ExchangeI32(&vitalPoint, newVitalPoint);
    runtime::raw_atomic::ExchangeI32(&restoreCount, newRestoreCount);
    runtime::raw_atomic::ExchangeI32(&latestResetDate_VP, newLatestResetDateVP);
    runtime::raw_atomic::ExchangeI32(&latestResetDate_RC, newLatestResetDateRC);
}

#if defined(L2_BUILD_L2SERVER)
void CVitalData::Reset(long latestResetDateVP, long latestResetDateRC) {
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::ExchangeI32(&vitalPoint, kResetVitalPoint);
    runtime::raw_atomic::ExchangeI32(&restoreCount, 0);
    runtime::raw_atomic::ExchangeI32(&latestResetDate_VP, static_cast<std::int32_t>(latestResetDateVP));
    runtime::raw_atomic::ExchangeI32(&latestResetDate_RC, static_cast<std::int32_t>(latestResetDateRC));
}
#endif

void CVitalData::Assemble(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    packet.Assemble(
        "dddd", static_cast<int>(vitalPoint), static_cast<int>(restoreCount), static_cast<int>(latestResetDate_VP),
        static_cast<int>(latestResetDate_RC)
    );
}

const unsigned char* CVitalData::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(
        packet, "dddd", const_cast<std::int32_t*>(&vitalPoint), const_cast<std::int32_t*>(&restoreCount),
        const_cast<std::int32_t*>(&latestResetDate_VP), const_cast<std::int32_t*>(&latestResetDate_RC)
    );
}

L2_DEFINE_OBJECT_CLASS_ID(CVitality)

L2_DEFINE_OBJECT_CLASS_NAME(CVitality, m_pClassName_CVitality)

L2_DEFINE_OBJECT_NEW(CVitality)

// Both product extents are checked in Vitality.h.
L2_DEFINE_OBJECT_ALLOCATOR(CVitality, m_pClassName_CVitality)

L2_DEFINE_OBJECT_RELEASE(CVitality)

CVitality::CVitality(int charDbId, int worldId)
#if defined(L2_BUILD_L2SERVER)
    :
    EXP_BONUS(g_nVitalBonusForFreeUser),
    PREMIUM_EXP_BONUS(g_nVitalBonusForPremiumUser),
    MAX_RESTORE_COUNT(g_nMaxRestoreVitalItemCountForFreeUser),
    PREMIUM_MAX_RESTORE_COUNT(g_nMaxRestoreVitalItemCountForPremiumUser),
    m_nCharDBId(charDbId),
    m_nWorldId(worldId){}
#else
    :
    m_nCharDBId(charDbId), m_nWorldId(worldId) {
}
#endif

    CVitality::~CVitality() = default;

#if defined(L2_BUILD_L2SERVER)
int CVitality::GetWorldID() {
    L2_GUARD_FUNCTION();
    return m_nWorldId;
}

bool CVitality::OnTick(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    if (_CheckReset()) {
        WriteVitalityLog(user, VitalityLogType13);
    }
    if (runtime::raw_atomic::ExchangeI32(&m_SomethingChanged, 0) == 0) {
        return false;
    }
    _Save();
    return true;
}

void CVitality::OnLeaveWorld() {
    L2_GUARD_FUNCTION();
    _Save();
}

bool CVitality::IncRestoreCount(VitalityRestore::Type type, CUserSocket* socket, int option, std::int64_t amount) {
    L2_GUARD_FUNCTION();

    int maximum = MAX_RESTORE_COUNT;
    if (socket == nullptr) {
        return false;
    }
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return false;
    }
    if (!user->CheckPremiumType(PremiumUserType::PUT_FREE_USER)) {
        maximum = PREMIUM_MAX_RESTORE_COUNT;
    }

    if (type != VitalityRestore::BY_BUILDER && type != VitalityRestore::BY_SKILL_NONCOUNT) {
        const std::int32_t restoreCount = runtime::raw_atomic::IncrementI32(&m_Data.restoreCount);
        if (maximum < restoreCount) {
            runtime::raw_atomic::DecrementI32(&m_Data.restoreCount);
            return false;
        }

        int remaining = 0;
        if (!user->CheckPremiumType(PremiumUserType::PUT_FREE_USER)) {
            const std::int32_t difference = runtime::SubtractInt32Wrapping(
                PREMIUM_MAX_RESTORE_COUNT, runtime::raw_atomic::LoadI32Relaxed(&m_Data.restoreCount)
            );
            if (difference >= 0) {
                remaining = difference;
            }
        } else {
            const std::int32_t difference = runtime::SubtractInt32Wrapping(
                MAX_RESTORE_COUNT, runtime::raw_atomic::LoadI32Relaxed(&m_Data.restoreCount)
            );
            if (difference >= 0) {
                remaining = difference;
            }
        }
        socket->SendSystemMessage_d(0xdc8, remaining);

        CLogFormatter formatter;
        formatter.WriteLog2(
            0x1f8d, user, nullptr, runtime::raw_atomic::LoadI32Relaxed(&m_Data.restoreCount), option, amount,
            user->m_data->m_nDBID, user->m_data->m_nSubJobId, 0, 0
        );
        user->ParamChanged(std::int64_t{PCB_CHAR_VITALITY});
        _Save();
    }
    return true;
}

bool CVitality::DecRestoreCount(int count, CUserSocket* socket, int option, int limit) {
    L2_GUARD_FUNCTION();

    int maximum = MAX_RESTORE_COUNT;
    User* user = nullptr;
    if (socket != nullptr) {
        user = socket->GetUser();
        if (user != nullptr && !user->CheckPremiumType(PremiumUserType::PUT_FREE_USER)) {
            maximum = PREMIUM_MAX_RESTORE_COUNT;
        }
    }

    std::int32_t newRestoreCount =
        runtime::SubtractInt32Wrapping(runtime::raw_atomic::LoadI32Relaxed(&m_Data.restoreCount), count);
    std::int32_t unavailableCount =
        runtime::SubtractInt32Wrapping(count, runtime::raw_atomic::LoadI32Relaxed(&m_Data.restoreCount));
    if (unavailableCount < 1) {
        unavailableCount = 0;
    }
    if (newRestoreCount < 0) {
        newRestoreCount = 0;
    }
    if (newRestoreCount > maximum) {
        newRestoreCount = maximum;
    }
    SetRestoreCount(newRestoreCount);

    if (socket != nullptr) {
        const int removed = static_cast<int>(
            static_cast<std::int8_t>(static_cast<std::int8_t>(count) - static_cast<std::int8_t>(unavailableCount))
        );
        if (!user->CheckPremiumType(PremiumUserType::PUT_FREE_USER)) {
            int remaining = 0;
            const std::int32_t difference = runtime::SubtractInt32Wrapping(
                PREMIUM_MAX_RESTORE_COUNT, runtime::raw_atomic::LoadI32Relaxed(&m_Data.restoreCount)
            );
            if (difference >= 0) {
                remaining = static_cast<std::int16_t>(PREMIUM_MAX_RESTORE_COUNT) -
                    static_cast<std::int16_t>(runtime::raw_atomic::LoadI32Relaxed(&m_Data.restoreCount));
            }
            socket->Send("chcccch", 0x62, 0x17df, 2, 0x14, removed, 0x15, remaining);
        } else {
            int remaining = 0;
            const std::int32_t difference = runtime::SubtractInt32Wrapping(
                MAX_RESTORE_COUNT, runtime::raw_atomic::LoadI32Relaxed(&m_Data.restoreCount)
            );
            if (difference >= 0) {
                remaining = static_cast<std::int8_t>(MAX_RESTORE_COUNT) -
                    static_cast<std::int8_t>(runtime::raw_atomic::LoadI32Relaxed(&m_Data.restoreCount));
            }
            socket->Send("chccccc", 0x62, 0x17df, 2, 0x14, removed, 0x14, remaining);
        }

        user = socket->GetUser();
        if (user != nullptr) {
            CLogFormatter formatter;
            formatter.WriteLog2(
                0x1f8d, user, nullptr, runtime::raw_atomic::LoadI32Relaxed(&m_Data.restoreCount), option, limit, 0, 0, 0,
                0
            );
        }
    }

    runtime::raw_atomic::IncrementI32(&m_SendVitalEffectInfo);
    OnChanged();
    return true;
}

bool CVitality::Restore(int point, VitalityRestore::Type type, CUserSocket* socket, int option, int limit) {
    L2_GUARD_FUNCTION();

    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] invalid user",
        //     "..\\Shared\\Vitality.cpp",
        //     0x23c)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[vitality] Cannot restore vitality for character %d in world %d: the socket has no bound user.",
        //     m_nCharDBId,
        //     m_nWorldId)
        L2_LOG_ERROR(
            Log, u"[vitality] Cannot restore vitality for character %d in world %d: the socket has no bound user.",
            m_nCharDBId, m_nWorldId
        );
        return false;
    }
    if (user->m_nChangeSubjobFlag == 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Use Vitality Item During change subjob [%s][%d]",
        //     "..\\Shared\\Vitality.cpp",
        //     0x242,
        //     user->GetName(),
        //     user->m_data->m_nDBID)
        // Log.Add(
        //     LOG_WARN,
        //     L"[vitality] Restoration rejected for character \"%ls\" (ID %d): a subclass change is in progress.",
        //     user->GetName(),
        //     user->m_data->m_nDBID)
        // L2_LOG_WARN(Log,
        //     L"[vitality] Restoration rejected for character \"%ls\" (ID %d): a subclass change is in progress.",
        //     user->GetName(),
        //     user->m_data->m_nDBID)
        L2_LOG_WARN(
            Log, u"[vitality] Restoration rejected for character \"%ls\" (ID %d): a subclass change is in progress.",
            user->GetName(), user->m_data->m_nDBID
        );
        return false;
    }
    if (!IncRestoreCount(type, socket, option, limit)) {
        return false;
    }

    const std::int32_t previousForEffect = runtime::raw_atomic::LoadI32Relaxed(&m_Data.vitalPoint);
    const std::int32_t previousForLog = runtime::raw_atomic::LoadI32Relaxed(&m_Data.vitalPoint);
    runtime::raw_atomic::AddI32(&m_Data.vitalPoint, point);

    if ((previousForEffect < 1) != (runtime::raw_atomic::LoadI32Relaxed(&m_Data.vitalPoint) < 1)) {
        runtime::raw_atomic::IncrementI32(&m_SendVitalEffectInfo);
    }
    if (runtime::raw_atomic::LoadI32Relaxed(&m_Data.vitalPoint) < 0) {
        runtime::raw_atomic::ExchangeI32(&m_Data.vitalPoint, 0);
    }
    if (runtime::raw_atomic::LoadI32Relaxed(&m_Data.vitalPoint) > kResetVitalPoint) {
        runtime::raw_atomic::ExchangeI32(&m_Data.vitalPoint, kResetVitalPoint);
    }

    const std::int32_t currentVitalPoint = runtime::raw_atomic::LoadI32Relaxed(&m_Data.vitalPoint);
    CLogFormatter formatter;
    if (type != VitalityRestore::BY_BUILDER) {
        formatter.WriteLog1(
            0x1f9e, user, nullptr, nullptr, nullptr, nullptr, user->m_data->m_nDBID, user->m_data->m_nSubJobId,
            previousForEffect, point, currentVitalPoint, limit, option
        );
    }
    if ((previousForLog > 0) != (currentVitalPoint > 0)) {
        formatter.WriteLog1(
            0x1f9d, user, nullptr, nullptr, nullptr, nullptr, currentVitalPoint > 0, currentVitalPoint,
            user->m_data->m_nDBID, user->m_data->m_nSubJobId, 0, 0, 0
        );
    }
    user->m_nParamChanged |= std::int64_t{PCB_USER};
    _Save();
    return true;
}

double CVitality::GetItemDropBonus() {
    L2_GUARD_FUNCTION();
    return runtime::raw_atomic::LoadI32Relaxed(&m_Data.vitalPoint) < 1 ? 0.0 : 3.0;
}

int CVitality::GetConsumePoint(
    CCreature* creature,
    int experience,
    double bonus,
    int userLevel,
    double consumeRate,
    int reducePercent,
    bool directConsume
) {
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsNPC() || experience < 1) {
        return 0;
    }

    CNPC* const npc = creature->CastNPC();
    int consumePoint = 0;
    if (userLevel < 85) {
        int maximum = 100;
        double coefficient = 0.1;
        if (!npc->IsBoss()) {
            if (npc->m_data->m_nClass == CT_WARRIOR) {
                coefficient = g_dVitalityConsumeByMobPer * 1.5;
                maximum = runtime::Win64CvttDoubleToInt32(g_dVitalityConsumeByMobPer * 200.0);
            }
        } else {
            coefficient = g_dVitalityConsumeByBossPer * 0.75;
            maximum = runtime::Win64CvttDoubleToInt32(g_dVitalityConsumeByBossPer * 200.0);
        }
        if (!npc->IsBoss() && npc->m_data->m_nClass != CT_WARRIOR) {
            return 0;
        }

        const int calculated = runtime::Win64CvttDoubleToInt32(
            std::ceil(coefficient * npc->m_data->m_dAcquireExpRate * bonus * 0.5 / static_cast<double>(experience))
        );
        const int bounded = calculated < maximum ? calculated : maximum;
        consumePoint = runtime::Win64CvttDoubleToInt32(consumeRate * -1.0 * static_cast<double>(bounded));
        if (directConsume) {
            return consumePoint;
        }
    } else if (npc->IsBoss()) {
        if (!g_bVitalityByBossBeforeRenewal) {
            return 0;
        }
        const int maximum = runtime::Win64CvttDoubleToInt32(g_dVitalityConsumeByBossPer * 100.0);
        const int calculated = runtime::Win64CvttDoubleToInt32(
            std::ceil(
                g_dVitalityConsumeByBossPer * 0.1 * npc->m_data->m_dAcquireExpRate * bonus * 0.5 /
                static_cast<double>(experience)
            )
        );
        const int bounded = calculated < maximum ? calculated : maximum;
        consumePoint = runtime::Win64CvttDoubleToInt32(static_cast<double>(bounded) * consumeRate * -1.0);
    } else {
        if (npc->m_data->m_nClass != CT_WARRIOR) {
            return 0;
        }

        double calculated = 0.0;
        if (!g_bVitalityByMobBeforeRenewal) {
            const double levelDivisor = static_cast<double>(userLevel - 95) * 0.2;
            const double boundedDivisor = levelDivisor <= 0.0 ? 0.0 : levelDivisor;
            calculated = g_dVitalityConsumeByMobPer * 0.15 * npc->m_data->m_dAcquireExpRate / (boundedDivisor + 1.0) *
                bonus * 0.5 / static_cast<double>(experience);
        } else {
            calculated = g_dVitalityConsumeByMobPer * 0.33 * npc->m_data->m_dAcquireExpRate * bonus * 0.5 /
                static_cast<double>(experience);
        }
        const int maximum = runtime::Win64CvttDoubleToInt32(g_dVitalityConsumeByMobPer * 200.0);
        const int calculatedInt = runtime::Win64CvttDoubleToInt32(std::ceil(calculated));
        const int bounded = calculatedInt < maximum ? calculatedInt : maximum;
        consumePoint = runtime::Win64CvttDoubleToInt32(consumeRate * -1.0 * static_cast<double>(bounded));
    }

    if (directConsume) {
        double multiplier = 1.0 - static_cast<double>(reducePercent) / 100.0;
        if (multiplier <= 0.5) {
            multiplier = 0.5;
        }
        if (multiplier > 1.0 || multiplier == 1.0) {
            multiplier = 1.0;
        }
        return runtime::Win64CvttDoubleToInt32(static_cast<double>(consumePoint) * multiplier);
    }

    return runtime::Win64CvttDoubleToInt32(std::ceil(static_cast<double>(consumePoint) / 3.0 * -1.0));
}

double CVitality::GetExpBonus(CSPointer<User>& user) {
    L2_GUARD_FUNCTION();
    if (IsSuspendedVitalityEffect() || runtime::raw_atomic::LoadI32Relaxed(&m_Data.vitalPoint) < 1) {
        return 1.0;
    }

    const std::int32_t percent = runtime::AddInt32Wrapping(user->m_SkillDurationMod.m_AddedVitalBonusPer, EXP_BONUS);
    return static_cast<double>(percent) / 100.0;
}

double CVitality::GetPremiumExpBonus(CSPointer<User>& user) {
    L2_GUARD_FUNCTION();
    if (IsSuspendedVitalityEffect() || runtime::raw_atomic::LoadI32Relaxed(&m_Data.vitalPoint) < 1) {
        return 1.0;
    }

    const std::int32_t percent =
        runtime::AddInt32Wrapping(user->m_SkillDurationMod.m_AddedVitalBonusPer, PREMIUM_EXP_BONUS);
    return static_cast<double>(percent) / 100.0;
}

bool CVitality::Consume(CSPointer<User>& user, int point) {
    L2_GUARD_FUNCTION();
    User* const target = user.get();
    if (target == nullptr) {
        return false;
    }

    const std::int32_t vitalPoint = runtime::raw_atomic::LoadI32Relaxed(&m_Data.vitalPoint);
    if ((point < 0 && vitalPoint == 0) || (point >= 0 && vitalPoint == kResetVitalPoint)) {
        return false;
    }

    const std::int32_t percent = runtime::SubtractInt32Wrapping(100, user->m_SkillDurationMod.m_ConsumePointReducePer);
    const std::int32_t delta =
        static_cast<std::int32_t>(std::ceil((static_cast<double>(percent) / 100.0) * static_cast<double>(point)));
    runtime::raw_atomic::AddI32(&m_Data.vitalPoint, delta);

    const std::int32_t changedPoint = runtime::raw_atomic::LoadI32Relaxed(&m_Data.vitalPoint);
    if ((vitalPoint < 1) != (changedPoint < 1)) {
        runtime::raw_atomic::IncrementI32(&m_SendVitalEffectInfo);
    }
    OnChanged();
    return true;
}

int CVitality::Recharge(CSPointer<User>& user, int point, bool ignoreBonus) {
    L2_GUARD_FUNCTION();
    if (point < 0) {
        return 0;
    }

    const std::int32_t vitalPoint = runtime::raw_atomic::LoadI32Relaxed(&m_Data.vitalPoint);
    if (vitalPoint == kResetVitalPoint) {
        return 0;
    }

    std::int32_t delta = point;
    if (!ignoreBonus) {
        const std::int32_t bonus = static_cast<std::int32_t>(std::ceil(
            (static_cast<double>(user->m_SkillDurationMod.m_RechargeBoostPer) / 100.0) * static_cast<double>(point)
        ));
        delta = runtime::AddInt32Wrapping(bonus, point);
    }
    runtime::raw_atomic::AddI32(&m_Data.vitalPoint, delta);

    if (vitalPoint < 1 && runtime::raw_atomic::LoadI32Relaxed(&m_Data.vitalPoint) > 0) {
        runtime::raw_atomic::IncrementI32(&m_SendVitalEffectInfo);
    }
    OnChanged();
    return point;
}

void CVitality::OnChanged() {
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::LoadI32Relaxed(&m_Data.vitalPoint) < 0) {
        runtime::raw_atomic::ExchangeI32(&m_Data.vitalPoint, 0);
    }
    if (runtime::raw_atomic::LoadI32Relaxed(&m_Data.vitalPoint) > kResetVitalPoint) {
        runtime::raw_atomic::ExchangeI32(&m_Data.vitalPoint, kResetVitalPoint);
    }
    runtime::raw_atomic::IncrementI32(&m_SomethingChanged);
}

void CVitality::SetRestoreCount(int restoreCount) {
    L2_GUARD_FUNCTION();
    _Init(m_Data.vitalPoint, restoreCount, m_Data.latestResetDate_VP, m_Data.latestResetDate_RC);
    OnChanged();
}

bool CVitality::CanUseConsumeItem() {
    L2_GUARD_FUNCTION();
    return m_Data.restoreCount < MAX_RESTORE_COUNT;
}

bool CVitality::CanUseConsumeItmeForPremiumUser() {
    L2_GUARD_FUNCTION();
    return m_Data.restoreCount < PREMIUM_MAX_RESTORE_COUNT;
}

void CVitality::SetVitalConsumePointReducePer(CSPointer<User>& user, const int reducePercent) {
    L2_GUARD_FUNCTION();
    User* const target = user.get();
    if (target == nullptr) {
        return;
    }

    runtime::raw_atomic::ExchangeI32(
        &target->m_SkillDurationMod.m_ConsumePointReducePer, reducePercent < 0 ? 0 : reducePercent
    );
}

bool CVitality::CanSuspend(CSPointer<User>& user) {
    L2_GUARD_FUNCTION();
    User* const target = user.get();
    return target != nullptr && runtime::raw_atomic::LoadI32Relaxed(&target->m_SkillDurationMod.m_SuspendConsume) == 0;
}

bool CVitality::IsSuspended(CSPointer<User>& user) {
    L2_GUARD_FUNCTION();
    User* const target = user.get();
    return target != nullptr && runtime::raw_atomic::LoadI32Relaxed(&target->m_SkillDurationMod.m_SuspendConsume) == 1;
}

bool CVitality::SuspendConsume(CSPointer<User>& user, VitalityRestore::Type, CUserSocket*) {
    L2_GUARD_FUNCTION();
    User* const target = user.get();
    if (target == nullptr) {
        return true;
    }
    return runtime::raw_atomic::CompareExchangeI32(&target->m_SkillDurationMod.m_SuspendConsume, 1, 0) == 0;
}

bool CVitality::StopSuspend(CSPointer<User>& user) {
    L2_GUARD_FUNCTION();
    User* const target = user.get();
    return target != nullptr &&
        runtime::raw_atomic::CompareExchangeI32(&target->m_SkillDurationMod.m_SuspendConsume, 0, 1) == 1;
}

void CVitality::GetExpBonusValue(CSPointer<User>& user, int& expBonus, int& addedBonus) const {
    L2_GUARD_FUNCTION();
    if (IsSuspendedVitalityEffect()) {
        return;
    }
    expBonus = EXP_BONUS;
    addedBonus = user->m_SkillDurationMod.m_AddedVitalBonusPer;
}

void CVitality::GetPremiumExpBonusValue(CSPointer<User>& user, int& expBonus, int& addedBonus) const {
    L2_GUARD_FUNCTION();
    if (IsSuspendedVitalityEffect()) {
        return;
    }
    expBonus = PREMIUM_EXP_BONUS;
    addedBonus = user->m_SkillDurationMod.m_AddedVitalBonusPer;
}

bool CVitality::SuspendVitalityEffect(VitalityRestore::Type, CUserSocket*) {
    L2_GUARD_FUNCTION();
    return runtime::raw_atomic::CompareExchangeI32(&m_SuspendVitalityEffect, 1, 0) == 0;
}

bool CVitality::StopSuspendVitalityEffect() {
    L2_GUARD_FUNCTION();
    return runtime::raw_atomic::CompareExchangeI32(&m_SuspendVitalityEffect, 0, 1) == 1;
}

bool CVitality::IsSuspendedVitalityEffect() const {
    L2_GUARD_FUNCTION();
    return runtime::raw_atomic::LoadI32Relaxed(&m_SuspendVitalityEffect) == 1;
}

bool CVitality::CheckSendEffectInfo() {
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::ExchangeI32(&m_VitalEffectInfoReset, 0) != 0) {
        return true;
    }
    return runtime::raw_atomic::ExchangeI32(&m_SendVitalEffectInfo, 0) != 0;
}

void CVitality::ResetForBuilder() {
    L2_GUARD_FUNCTION();
    m_Data.Reset(0, 0);
    _CheckReset();
}
#endif

void CVitality::_Init(int vitalPoint, int restoreCount, int latestResetDateVP, int latestResetDateRC) {
    L2_GUARD_FUNCTION();
    m_Data.Init(vitalPoint, restoreCount, latestResetDateVP, latestResetDateRC);
    m_nPreviousVitalityPoint = vitalPoint;
    m_nPreviousRestoreCount = restoreCount;
}

void CVitality::_Assemble(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    m_Data.Assemble(packet);
}

void CVitality::WriteVitalityLog(
#if defined(L2_BUILD_L2SERVER)
    User* user,
#else
    CUser* user,
#endif
    VitalityGetType type
) {
    L2_GUARD_FUNCTION();
    const int typeValue = static_cast<int>(type);
    if (!user || typeValue < 12 || typeValue >= 14) {
        return;
    }

    CLogFormatter formatter;
#if defined(L2_BUILD_L2SERVER)
    wchar_t* const empty = const_cast<wchar_t*>(L"");
    formatter.WriteLog1(
        0x1f47, user, nullptr, WideToUtf16(empty), WideToUtf16(empty), WideToUtf16(empty), m_nPreviousVitalityPoint,
        runtime::SubtractInt32Wrapping(runtime::raw_atomic::LoadI32Relaxed(&m_Data.vitalPoint), m_nPreviousVitalityPoint),
        runtime::raw_atomic::LoadI32Relaxed(&m_Data.vitalPoint), typeValue, m_nPreviousRestoreCount,
        runtime::raw_atomic::LoadI32Relaxed(&m_Data.restoreCount), 0
    );
#else
    formatter.WriteLogCache3(
        0x1f47, user, m_nPreviousVitalityPoint, static_cast<int>(m_Data.vitalPoint) - m_nPreviousVitalityPoint,
        m_Data.vitalPoint, typeValue, m_nPreviousRestoreCount, m_Data.restoreCount
    );
#endif
}

void CVitality::AssembleForLoading(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    _Assemble(packet);
}

void CVitality::Init(int vitalPoint, int restoreCount, int latestResetDateVP, int latestResetDateRC) {
    L2_GUARD_FUNCTION();
    _Init(vitalPoint, restoreCount, latestResetDateVP, latestResetDateRC);
}

void CVitality::Init(CVitalData& data) {
    L2_GUARD_FUNCTION();
    _Init(data.vitalPoint, data.restoreCount, data.latestResetDate_VP, data.latestResetDate_RC);
}

#if defined(L2_BUILD_CACHED)
const unsigned char* CVitality::SaveVitality(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int charDbId = 0;
    int ignoredPacketValue = 0;
    CVitalData data;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &charDbId, &ignoredPacketValue);
    cursor = data.Disassemble(cursor);

    CUserSP user = g_userDb.GetUser(charDbId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[../Shared/Vitality.cpp][1401] Cannot find CharDbId[%d]. "
        //     "vital into[point:%d][restore:%d][reset_VP:%d][reset_RC:%d]",
        //     charDbId,
        //     static_cast<int>(data.vitalPoint),
        //     static_cast<int>(data.restoreCount),
        //     static_cast<int>(data.latestResetDate_VP),
        //     static_cast<int>(data.latestResetDate_RC))
        // Log.Add(
        //     LOG_ERROR,
        //     "[vitality] Update discarded: character %d was not found. Received %d points, %d restorations. "
        //     "last reset timestamps: points %d, restorations %d (Unix seconds).",
        //     charDbId,
        //     static_cast<int>(data.vitalPoint),
        //     static_cast<int>(data.restoreCount),
        //     static_cast<int>(data.latestResetDate_VP),
        //     static_cast<int>(data.latestResetDate_RC))
        L2_LOG_ERROR(
            Log,
            "[vitality] Update discarded: character %d was not found. Received %d points, %d restorations; "
            "last reset timestamps: points %d, restorations %d (Unix seconds).",
            charDbId, static_cast<int>(data.vitalPoint), static_cast<int>(data.restoreCount),
            static_cast<int>(data.latestResetDate_VP), static_cast<int>(data.latestResetDate_RC)
        );
    } else {
        user->UpdateVitality(data);
    }
    return cursor;
}
#endif

void CVitality::_Save() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    CPacketHelperEx<20480> packet;
    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(GetWorldID(), m_nCharDBId));
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid user[%d]",
        //     L"..\\Shared\\Vitality.cpp",
        //     0x16e,
        //     m_nCharDBId)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[vitality] Save request was not sent: character %d in world %d was not found.",
        //     m_nCharDBId,
        //     m_nWorldId)
        L2_LOG_ERROR(
            Log, u"[vitality] Save request was not sent: character %d in world %d was not found.", m_nCharDBId, m_nWorldId
        );
        runtime::process::ReportCallStack(2, false);
        return;
    }
    if (user->m_nChangeSubjobFlag == 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Use Vitality Item During change subjob [%d][%S]",
        //     L"..\\Shared\\Vitality.cpp",
        //     0x175,
        //     m_nCharDBId,
        //     user->GetName())
        // Log.Add(
        //     LOG_WARN,
        //     L"[vitality] Save request was not sent for character %d (\"%ls\"): a subclass change is in progress.",
        //     m_nCharDBId,
        //     user->GetName())
        // L2_LOG_WARN(Log,
        //     L"[vitality] Save request was not sent for character %d (\"%ls\"): a subclass change is in progress.",
        //     m_nCharDBId,
        //     user->GetName())
        L2_LOG_WARN(
            Log, u"[vitality] Save request was not sent for character %d (\"%ls\"): a subclass change is in progress.",
            m_nCharDBId, user->GetName()
        );
        return;
    }

    packet.Assemble("dd", m_nCharDBId, user->m_data->m_nDBID);
    _Assemble(packet);
    db.RequestSaveVitality(packet, GetWorldID());
#elif defined(L2_BUILD_CACHED)
    const int databaseId = runtime::process::CurrentDatabaseId();
    CUserSP user = g_userDb.GetUser(m_nCharDBId, true, false, databaseId);
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[../Shared/Vitality.cpp][388] invalid user[%d]",
        //     m_nCharDBId)
        // Log.Add(
        //     LOG_ERROR,
        //     "[vitality] Database save skipped: character %d was not found in world database %d.",
        //     m_nCharDBId,
        //     databaseId)
        L2_LOG_ERROR(
            Log, "[vitality] Database save skipped: character %d was not found in world database %d.", m_nCharDBId,
            databaseId
        );
        return;
    }
    SaveData(databaseId, m_nCharDBId, user->GetSubjobId(), m_Data);
#endif
}

void CVitality::Update(CVitalData& data) {
    L2_GUARD_FUNCTION();
    if (!m_SomethingChanged) {
        Init(data);
        _Save();
    } else {
        m_SomethingChanged = false;
    }
}

#if defined(L2_BUILD_CACHED)
void CVitality::_Save(int charDbId, int subjobId) {
    L2_GUARD_FUNCTION();
    const int databaseId = runtime::process::CurrentDatabaseId();
    CUserSP user = g_userDb.GetUser(m_nCharDBId, true, false, databaseId);
    if (!user) {
        return;
    }
    SaveData(databaseId, charDbId, subjobId, m_Data);
}
#endif

void CVitality::_Reset_VitalPoint(long now, long dueTime) {
    L2_GUARD_FUNCTION();
    const std::int32_t resetDate =
        ResetDate(static_cast<std::int32_t>(now), static_cast<std::int32_t>(dueTime), m_Data.latestResetDate_VP == 0);
    m_nPreviousVitalityPoint = runtime::raw_atomic::ExchangeI32(&m_Data.vitalPoint, kResetVitalPoint);
    runtime::raw_atomic::ExchangeI32(&m_Data.latestResetDate_VP, resetDate);
#if defined(L2_BUILD_L2SERVER)
    OnChanged();
#else
    _Save();
#endif
}

void CVitality::_Reset_RestoreCount(long now, long dueTime) {
    L2_GUARD_FUNCTION();
    const std::int32_t resetDate =
        ResetDate(static_cast<std::int32_t>(now), static_cast<std::int32_t>(dueTime), m_Data.latestResetDate_RC == 0);
    m_nPreviousRestoreCount = runtime::raw_atomic::ExchangeI32(&m_Data.restoreCount, 0);
    runtime::raw_atomic::ExchangeI32(&m_Data.latestResetDate_RC, resetDate);
#if defined(L2_BUILD_L2SERVER)
    OnChanged();
#else
    _Save();
#endif
}

#if defined(L2_BUILD_CACHED)
void CVitality::UpdateByAdmin(CVitalData& data, int charDbId, int subjobId) {
    L2_GUARD_FUNCTION();
    Init(data);
    _Save(charDbId, subjobId);
    m_SomethingChanged = true;
}
#endif

bool CVitality::_CheckReset() {
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    const std::int32_t vitalPointDue = static_cast<std::int32_t>(m_Data.latestResetDate_VP) + kResetPeriodSeconds;
    const bool resetVitalPoint = vitalPointDue <= now;
    if (resetVitalPoint) {
        _Reset_VitalPoint(now, vitalPointDue);
    }

    const std::int32_t restoreCountDue = static_cast<std::int32_t>(m_Data.latestResetDate_RC) + kResetPeriodSeconds;
    const bool resetRestoreCount = restoreCountDue <= now;
    if (resetRestoreCount) {
        _Reset_RestoreCount(now, restoreCountDue);
    }
#if defined(L2_BUILD_L2SERVER)
    if (resetVitalPoint || resetRestoreCount) {
        runtime::raw_atomic::IncrementI32(&m_VitalEffectInfoReset);
    }
#endif
    return resetVitalPoint || resetRestoreCount;
}

bool CVitality::CheckReset() {
    L2_GUARD_FUNCTION();
    return _CheckReset();
}
