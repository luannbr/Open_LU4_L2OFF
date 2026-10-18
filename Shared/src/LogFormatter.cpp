#include "Shared/src/LogFormatter.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Log.h"

#if defined(L2_BUILD_L2SERVER)
#include "Creature.h"
#include "Door.h"
#include "User.h"
#endif

#include <charconv>
#include <cmath>
#include <limits>
#include <string>

namespace {

class LogRecordBuilder {
public:
    void Append(std::int64_t value) {
        Separate();
        char buffer[32]{};
        const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value);
        for (const char* cursor = buffer; cursor != converted.ptr; ++cursor) {
            m_value.push_back(static_cast<char16_t>(*cursor));
        }
    }

    void Append(const char16_t* value) {
        Separate();
        if (value) {
            m_value.append(value);
        }
    }

    const std::u16string& Value() const noexcept { return m_value; }

private:
    void Separate() {
        if (m_hasFields) {
            m_value.push_back(u',');
        }
        m_hasFields = true;
    }

    std::u16string m_value;
    bool m_hasFields = false;
};

constexpr int kLogBoth = 6;
constexpr int kLogAudit = 8;
constexpr int kLogNCGuard = 13;

#if defined(L2_BUILD_L2SERVER)
std::int32_t ConvertTargetCoordinate(double value) noexcept {
    constexpr double kMinimum = static_cast<double>(std::numeric_limits<std::int32_t>::min());
    constexpr double kMaximumExclusive = static_cast<double>(std::numeric_limits<std::int32_t>::max()) + 1.0;
    if (!std::isfinite(value) || value < kMinimum || value >= kMaximumExclusive) {
        return std::numeric_limits<std::int32_t>::min();
    }
    return static_cast<std::int32_t>(value);
}

std::u16string ToTargetUtf16(const Utf16Text value) { return value.String(); }
#endif

} // namespace

void CLogFormatter::WriteLog(
    int nLogId,
    std::int64_t nActorDbId,
    std::int64_t nActorAccountId,
    std::int64_t nTargetDbId,
    std::int64_t nTargetAccountId,
    std::int64_t x,
    std::int64_t y,
    std::int64_t z,
    const char16_t* strEtcStr1_200,
    const char16_t* strEtcStr2_50,
    const char16_t* strEtcStr3_50,
    std::int64_t nEtcNum1,
    std::int64_t nEtcNum2,
    std::int64_t nEtcNum3,
    std::int64_t nEtcNum4,
    std::int64_t nEtcNum5,
    std::int64_t nEtcNum6,
    std::int64_t nEtcNum7,
    std::int64_t nEtcNum8,
    std::int64_t nEtcNum9,
    std::int64_t nEtcNum10,
    const char16_t* strActorName_32,
    const char16_t* strActorAccountName_32,
    const char16_t* strTargetName_32,
    const char16_t* strTargetAccountName_32,
    std::int64_t nItemId
) {
    LogRecordBuilder record;
    record.Append(nLogId);
    record.Append(nActorDbId);
    record.Append(nActorAccountId);
    record.Append(nTargetDbId);
    record.Append(nTargetAccountId);
    record.Append(x);
    record.Append(y);
    record.Append(z);
    record.Append(strEtcStr1_200);
    record.Append(strEtcStr2_50);
    record.Append(strEtcStr3_50);
    record.Append(nEtcNum1);
    record.Append(nEtcNum2);
    record.Append(nEtcNum3);
    record.Append(nEtcNum4);
    record.Append(nEtcNum5);
    record.Append(nEtcNum6);
    record.Append(nEtcNum7);
    record.Append(nEtcNum8);
    record.Append(nEtcNum9);
    record.Append(nEtcNum10);
    record.Append(strActorName_32);
    record.Append(strActorAccountName_32);
    record.Append(strTargetName_32);
    record.Append(strTargetAccountName_32);
    record.Append(nItemId);
    runtime::process::WriteLogFormatterLine(kLogBoth, record.Value());
}

void CLogFormatter::WriteAuditLog(
    int nLogId,
    std::int64_t nActorDbId,
    std::int64_t nActorAccountId,
    std::int64_t nTargetDbId,
    std::int64_t nTargetAccountId,
    std::int64_t x,
    std::int64_t y,
    std::int64_t z,
    const char16_t* strEtcStr1_200,
    const char16_t* strEtcStr2_50,
    const char16_t* strEtcStr3_50,
    std::int64_t nEtcNum1,
    std::int64_t nEtcNum2,
    std::int64_t nEtcNum3,
    std::int64_t nEtcNum4,
    std::int64_t nEtcNum5,
    std::int64_t nEtcNum6,
    std::int64_t nEtcNum7,
    std::int64_t nEtcNum8,
    std::int64_t nEtcNum9,
    std::int64_t nEtcNum10,
    const char16_t* strActorName_32,
    const char16_t* strActorAccountName_32,
    const char16_t* strTargetName_32,
    const char16_t* strTargetAccountName_32,
    std::int64_t nItemId
) {
    LogRecordBuilder record;
    record.Append(nLogId);
    record.Append(nActorDbId);
    record.Append(nActorAccountId);
    record.Append(nTargetDbId);
    record.Append(nTargetAccountId);
    record.Append(x);
    record.Append(y);
    record.Append(z);
    record.Append(strEtcStr1_200);
    record.Append(strEtcStr2_50);
    record.Append(strEtcStr3_50);
    record.Append(nEtcNum1);
    record.Append(nEtcNum2);
    record.Append(nEtcNum3);
    record.Append(nEtcNum4);
    record.Append(nEtcNum5);
    record.Append(nEtcNum6);
    record.Append(nEtcNum7);
    record.Append(nEtcNum8);
    record.Append(nEtcNum9);
    record.Append(nEtcNum10);
    record.Append(strActorName_32);
    record.Append(strActorAccountName_32);
    record.Append(strTargetName_32);
    record.Append(strTargetAccountName_32);
    record.Append(nItemId);
    runtime::process::WriteLogFormatterLine(kLogAudit, record.Value());
}

#if defined(L2_BUILD_L2SERVER)
void CLogFormatter::WriteNCGuardLog(
    int nLogId,
    std::int64_t nActorDbId,
    std::int64_t nActorAccountId,
    std::int64_t nTargetDbId,
    std::int64_t nTargetAccountId,
    std::int64_t x,
    std::int64_t y,
    std::int64_t z,
    const char16_t* strEtcStr1_200,
    const char16_t* strEtcStr2_50,
    const char16_t* strEtcStr3_50,
    std::int64_t nEtcNum1,
    std::int64_t nEtcNum2,
    std::int64_t nEtcNum3,
    std::int64_t nEtcNum4,
    std::int64_t nEtcNum5,
    std::int64_t nEtcNum6,
    std::int64_t nEtcNum7,
    std::int64_t nEtcNum8,
    std::int64_t nEtcNum9,
    std::int64_t nEtcNum10,
    std::int64_t nEtcNum11,
    std::int64_t nEtcNum12,
    std::int64_t nEtcNum13,
    std::int64_t nEtcNum14,
    std::int64_t nEtcNum15,
    const char16_t* strActorName_32,
    const char16_t* strActorAccountName_32,
    const char16_t* strTargetName_32,
    const char16_t* strTargetAccountName_32,
    std::int64_t nItemId
) {
    // Current L2Server 0x00810f90. Its dedicated log channel contains
    // fifteen numeric payload lanes instead of WriteLog's ten.
    LogRecordBuilder record;
    record.Append(nLogId);
    record.Append(nActorDbId);
    record.Append(nActorAccountId);
    record.Append(nTargetDbId);
    record.Append(nTargetAccountId);
    record.Append(x);
    record.Append(y);
    record.Append(z);
    record.Append(strEtcStr1_200);
    record.Append(strEtcStr2_50);
    record.Append(strEtcStr3_50);
    record.Append(nEtcNum1);
    record.Append(nEtcNum2);
    record.Append(nEtcNum3);
    record.Append(nEtcNum4);
    record.Append(nEtcNum5);
    record.Append(nEtcNum6);
    record.Append(nEtcNum7);
    record.Append(nEtcNum8);
    record.Append(nEtcNum9);
    record.Append(nEtcNum10);
    record.Append(nEtcNum11);
    record.Append(nEtcNum12);
    record.Append(nEtcNum13);
    record.Append(nEtcNum14);
    record.Append(nEtcNum15);
    record.Append(strActorName_32);
    record.Append(strActorAccountName_32);
    record.Append(strTargetName_32);
    record.Append(strTargetAccountName_32);
    record.Append(nItemId);
    runtime::process::WriteLogFormatterLine(kLogNCGuard, record.Value());
}

void CLogFormatter::WriteNCGuardLog1(
    int nLogId,
    CCreature* actor,
    CWorldObject* target,
    Utf16Text strEtcStr1_200,
    Utf16Text strEtcStr2_50,
    Utf16Text strEtcStr3_50,
    std::int64_t nEtcNum1,
    std::int64_t nEtcNum2,
    std::int64_t nEtcNum3,
    std::int64_t nEtcNum4,
    std::int64_t nEtcNum5,
    std::int64_t nEtcNum6,
    std::int64_t nEtcNum7,
    std::int64_t nEtcNum8,
    std::int64_t nEtcNum9,
    std::int64_t nEtcNum10,
    std::int64_t nEtcNum11,
    std::int64_t nEtcNum12,
    std::int64_t nEtcNum13,
    std::int64_t nEtcNum14,
    std::int64_t nEtcNum15
) {
    // Current L2Server 0x00811448. The common extractor at 0x008111dc
    // computes the standard actor/target identity and position lanes. This
    // wrapper publishes no item id.
    L2_GUARD_FUNCTION();
    std::int64_t actorDbId = 0;
    std::int64_t actorAccountId = 0;
    std::int64_t targetDbId = 0;
    std::int64_t targetAccountId = 0;
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t z = 0;
    const char16_t* actorName = nullptr;
    const char16_t* actorAccountName = nullptr;
    const char16_t* targetName = nullptr;
    const char16_t* targetAccountName = nullptr;
    std::u16string actorNameStorage;
    std::u16string targetNameStorage;

    if (actor) {
        const FVector position = actor->GetPos();
        x = ConvertTargetCoordinate(position.x);
        y = ConvertTargetCoordinate(position.y);
        z = ConvertTargetCoordinate(position.z);

        if (actor->IsPC()) {
            User* const user = actor->CastUser();
            actorAccountId = user->GetAccountID();
            actorDbId = actor->GetDBID();
            actorNameStorage = user->GetName();
            actorName = actorNameStorage.c_str();
            actorAccountName = user->m_sAccountName.data();
        } else if (actor->IsSummon()) {
            CCreature* const summon = reinterpret_cast<CCreature*>(actor->CastSummon());
            actorDbId = summon->m_data->m_nClassID;
            actorAccountId = summon->m_data->m_nClassID;
        }
    }

    if (target) {
        if (target->IsPC()) {
            User* const user = target->CastUser();
            targetAccountId = user->GetAccountID();
            targetDbId = target->GetDBID();
            targetNameStorage = user->GetName();
            targetName = targetNameStorage.c_str();
            targetAccountName = user->m_sAccountName.data();
        } else if (target->IsSummon()) {
            CCreature* const summon = reinterpret_cast<CCreature*>(target->CastSummon());
            targetDbId = summon->m_data->m_nClassID;
            targetAccountId = summon->m_data->m_nClassID;
        } else if (target->IsDoor()) {
            targetName = target->CastDoor()->GetDoorName();
        }
    }

    const std::u16string etcString1 = ToTargetUtf16(strEtcStr1_200);
    const std::u16string etcString2 = ToTargetUtf16(strEtcStr2_50);
    const std::u16string etcString3 = ToTargetUtf16(strEtcStr3_50);
    WriteNCGuardLog(
        nLogId, actorDbId, actorAccountId, targetDbId, targetAccountId, x, y, z,
        strEtcStr1_200 ? etcString1.c_str() : nullptr, strEtcStr2_50 ? etcString2.c_str() : nullptr,
        strEtcStr3_50 ? etcString3.c_str() : nullptr, nEtcNum1, nEtcNum2, nEtcNum3, nEtcNum4, nEtcNum5, nEtcNum6, nEtcNum7,
        nEtcNum8, nEtcNum9, nEtcNum10, nEtcNum11, nEtcNum12, nEtcNum13, nEtcNum14, nEtcNum15, actorName, actorAccountName,
        targetName, targetAccountName, 0
    );
}

void CLogFormatter::WriteChatLog(
    int nLogId,
    std::int64_t nActorDbId,
    std::int64_t nTargetDbId,
    std::int64_t x,
    std::int64_t y,
    std::int64_t z,
    Utf16Text message,
    const char16_t* actorName,
    const char16_t* targetName
) {
    std::u16string messageStorage = ToTargetUtf16(message);
    if (messageStorage.size() > 0x68) {
        // Log.Add(
        //     LOG_ERROR,
        //     "CHAT_LOG : chatting msg length over 200 ActorDbId : %lld",
        //     static_cast<long long>(nActorDbId))
        // Log.Add(
        //     LOG_WARN,
        //     "[chat log] Truncating text for character database ID %lld from %zu to 104 UTF-16 code units.",
        //     static_cast<long long>(nActorDbId), messageStorage.size())
        L2_LOG_WARN(
            Log, "[chat log] Truncating text for character database ID %lld from %zu to 104 UTF-16 code units.",
            static_cast<long long>(nActorDbId), messageStorage.size()
        );
        messageStorage.resize(0x68);
    }

    LogRecordBuilder record;
    record.Append(nLogId);
    record.Append(nActorDbId);
    record.Append(nTargetDbId);
    record.Append(x);
    record.Append(y);
    record.Append(z);
    record.Append(messageStorage.c_str());
    record.Append(actorName);
    record.Append(targetName);
    runtime::process::WriteLogFormatterLine(LOG_CHAT, record.Value());
}

void CLogFormatter::WriteLog1_1(
    int nLogId,
    CCreature* actor,
    CWorldObject* target,
    Utf16Text strEtcStr1_200,
    Utf16Text strEtcStr2_50,
    Utf16Text strEtcStr3_50,
    std::int64_t nEtcNum5,
    std::int64_t nEtcNum6,
    std::int64_t nEtcNum7,
    std::int64_t nEtcNum8,
    std::int64_t nEtcNum9,
    std::int64_t nEtcNum10,
    std::int64_t nItemId
) {
    std::int64_t actorDbId = 0;
    std::int64_t actorAccountId = 0;
    std::int64_t targetDbId = 0;
    std::int64_t targetAccountId = 0;
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t z = 0;
    std::int64_t actorClass = 0;
    std::int64_t actorRace = 0;
    std::int64_t actorSex = 0;
    std::int64_t actorLevel = 0;
    const char16_t* actorName = nullptr;
    const char16_t* actorAccountName = nullptr;
    const char16_t* targetName = nullptr;
    const char16_t* targetAccountName = nullptr;
    std::u16string actorNameStorage;
    std::u16string targetNameStorage;

    if (actor) {
        const FVector position = actor->GetPos();
        x = ConvertTargetCoordinate(position.x);
        y = ConvertTargetCoordinate(position.y);
        z = ConvertTargetCoordinate(position.z);

        if (actor->IsPC()) {
            User* const user = actor->CastUser();
            actorAccountId = user->GetAccountID();
            actorDbId = actor->GetDBID();
            actorNameStorage = user->GetName();
            actorName = actorNameStorage.c_str();
            actorAccountName = user->m_sAccountName.data();
        } else if (actor->IsSummon()) {
            CCreature* const summon = reinterpret_cast<CCreature*>(actor->CastSummon());
            actorDbId = summon->m_data->m_nClassID;
            actorAccountId = summon->m_data->m_nClassID;
        }

        actorClass = actor->m_data->m_nClass;
        actorRace = actor->m_data->m_Race;
        actorSex = actor->m_data->m_Sex;
        actorLevel = actor->m_data->m_nLevel;
    }

    if (target) {
        if (target->IsPC()) {
            User* const user = target->CastUser();
            targetAccountId = user->GetAccountID();
            targetDbId = target->GetDBID();
            targetNameStorage = user->GetName();
            targetName = targetNameStorage.c_str();
            targetAccountName = user->m_sAccountName.data();
        } else if (target->IsSummon()) {
            CCreature* const summon = reinterpret_cast<CCreature*>(target->CastSummon());
            targetDbId = summon->m_data->m_nClassID;
            targetAccountId = summon->m_data->m_nClassID;
        } else if (target->IsDoor()) {
            targetName = target->CastDoor()->GetDoorName();
        }
    }

    const std::u16string etcString1 = ToTargetUtf16(strEtcStr1_200);
    const std::u16string etcString2 = ToTargetUtf16(strEtcStr2_50);
    const std::u16string etcString3 = ToTargetUtf16(strEtcStr3_50);
    WriteLog(
        nLogId, actorDbId, actorAccountId, targetDbId, targetAccountId, x, y, z,
        strEtcStr1_200 ? etcString1.c_str() : nullptr, strEtcStr2_50 ? etcString2.c_str() : nullptr,
        strEtcStr3_50 ? etcString3.c_str() : nullptr, actorRace, actorSex, actorClass, actorLevel, nEtcNum5, nEtcNum6,
        nEtcNum7, nEtcNum8, nEtcNum9, nEtcNum10, actorName, actorAccountName, targetName, targetAccountName, nItemId
    );
}

void CLogFormatter::WriteLog1(
    int nLogId,
    CCreature* actor,
    CWorldObject* target,
    Utf16Text strEtcStr1_200,
    Utf16Text strEtcStr2_50,
    Utf16Text strEtcStr3_50,
    std::int64_t nEtcNum5,
    std::int64_t nEtcNum6,
    std::int64_t nEtcNum7,
    std::int64_t nEtcNum8,
    std::int64_t nEtcNum9,
    std::int64_t nEtcNum10,
    std::int64_t nItemId
) {
    std::int64_t actorDbId = 0;
    std::int64_t actorAccountId = 0;
    std::int64_t targetDbId = 0;
    std::int64_t targetAccountId = 0;
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t z = 0;
    std::int64_t actorClass = 0;
    std::int64_t actorRace = 0;
    std::int64_t actorSex = 0;
    std::int64_t actorLevel = 0;
    const char16_t* actorName = nullptr;
    const char16_t* actorAccountName = nullptr;
    const char16_t* targetName = nullptr;
    const char16_t* targetAccountName = nullptr;
    std::u16string actorNameStorage;
    std::u16string targetNameStorage;

    if (actor) {
        const FVector position = actor->GetPos();
        x = ConvertTargetCoordinate(position.x);
        y = ConvertTargetCoordinate(position.y);
        z = ConvertTargetCoordinate(position.z);

        if (actor->IsPC()) {
            User* const user = actor->CastUser();
            actorAccountId = user->GetAccountID();
            actorDbId = actor->GetDBID();
            actorNameStorage = user->GetName();
            actorName = actorNameStorage.c_str();
            actorAccountName = user->m_sAccountName.data();
        } else if (actor->IsSummon()) {
            // The current CastSummon edge is zero-adjusting. The returned
            // summon therefore exposes the same CCreature shared-data owner.
            CCreature* const summon = reinterpret_cast<CCreature*>(actor->CastSummon());
            actorDbId = summon->m_data->m_nClassID;
            actorAccountId = summon->m_data->m_nClassID;
        }

        actorClass = actor->m_data->m_nClass;
        actorRace = actor->m_data->m_Race;
        actorSex = actor->m_data->m_Sex;
        actorLevel = actor->m_data->m_nLevel;
    }

    if (target) {
        if (target->IsPC()) {
            User* const user = target->CastUser();
            targetAccountId = user->GetAccountID();
            targetDbId = target->GetDBID();
            targetNameStorage = user->GetName();
            targetName = targetNameStorage.c_str();
            targetAccountName = user->m_sAccountName.data();
        } else if (target->IsSummon()) {
            CCreature* const summon = reinterpret_cast<CCreature*>(target->CastSummon());
            targetDbId = summon->m_data->m_nClassID;
            targetAccountId = summon->m_data->m_nClassID;
        } else if (target->IsDoor()) {
            targetName = target->CastDoor()->GetDoorName();
        }
    }

    const std::u16string etcString1 = ToTargetUtf16(strEtcStr1_200);
    const std::u16string etcString2 = ToTargetUtf16(strEtcStr2_50);
    const std::u16string etcString3 = ToTargetUtf16(strEtcStr3_50);
    WriteLog(
        nLogId, actorDbId, actorAccountId, targetDbId, targetAccountId, x, y, z,
        strEtcStr1_200 ? etcString1.c_str() : nullptr, strEtcStr2_50 ? etcString2.c_str() : nullptr,
        strEtcStr3_50 ? etcString3.c_str() : nullptr, actorClass, actorRace, actorSex, actorLevel, nEtcNum5, nEtcNum6,
        nEtcNum7, nEtcNum8, nEtcNum9, nEtcNum10, actorName, actorAccountName, targetName, targetAccountName, nItemId
    );
}

void CLogFormatter::WriteAuditLog1(
    int nLogId,
    CCreature* actor,
    CWorldObject* target,
    Utf16Text strEtcStr1_200,
    Utf16Text strEtcStr2_50,
    Utf16Text strEtcStr3_50,
    std::int64_t nEtcNum5,
    std::int64_t nEtcNum6,
    std::int64_t nEtcNum7,
    std::int64_t nEtcNum8,
    std::int64_t nEtcNum9,
    std::int64_t nEtcNum10,
    std::int64_t nItemId
) {
    // Current L2Server 0x0081236c. Unlike WriteLog1 this terminates in the
    // audit-only writer at 0x00811b00.
    std::int64_t actorDbId = 0;
    std::int64_t actorAccountId = 0;
    std::int64_t targetDbId = 0;
    std::int64_t targetAccountId = 0;
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t z = 0;
    std::int64_t actorClass = 0;
    std::int64_t actorRace = 0;
    std::int64_t actorSex = 0;
    std::int64_t actorLevel = 0;
    const char16_t* actorName = nullptr;
    const char16_t* actorAccountName = nullptr;
    const char16_t* targetName = nullptr;
    const char16_t* targetAccountName = nullptr;
    std::u16string actorNameStorage;
    std::u16string targetNameStorage;

    if (actor) {
        const FVector position = actor->GetPos();
        x = ConvertTargetCoordinate(position.x);
        y = ConvertTargetCoordinate(position.y);
        z = ConvertTargetCoordinate(position.z);

        if (actor->IsPC()) {
            User* const user = actor->CastUser();
            actorAccountId = user->GetAccountID();
            actorDbId = actor->GetDBID();
            actorNameStorage = user->GetName();
            actorName = actorNameStorage.c_str();
            actorAccountName = user->m_sAccountName.data();
        } else if (actor->IsSummon()) {
            CCreature* const summon = reinterpret_cast<CCreature*>(actor->CastSummon());
            actorDbId = summon->m_data->m_nClassID;
            actorAccountId = summon->m_data->m_nClassID;
        }

        actorClass = actor->m_data->m_nClass;
        actorRace = actor->m_data->m_Race;
        actorSex = actor->m_data->m_Sex;
        actorLevel = actor->m_data->m_nLevel;
    }

    if (target) {
        if (target->IsPC()) {
            User* const user = target->CastUser();
            targetAccountId = user->GetAccountID();
            targetDbId = target->GetDBID();
            targetNameStorage = user->GetName();
            targetName = targetNameStorage.c_str();
            targetAccountName = user->m_sAccountName.data();
        } else if (target->IsSummon()) {
            CCreature* const summon = reinterpret_cast<CCreature*>(target->CastSummon());
            targetDbId = summon->m_data->m_nClassID;
            targetAccountId = summon->m_data->m_nClassID;
        } else if (target->IsDoor()) {
            targetName = target->CastDoor()->GetDoorName();
        }
    }

    const std::u16string etcString1 = ToTargetUtf16(strEtcStr1_200);
    const std::u16string etcString2 = ToTargetUtf16(strEtcStr2_50);
    const std::u16string etcString3 = ToTargetUtf16(strEtcStr3_50);
    WriteAuditLog(
        nLogId, actorDbId, actorAccountId, targetDbId, targetAccountId, x, y, z,
        strEtcStr1_200 ? etcString1.c_str() : nullptr, strEtcStr2_50 ? etcString2.c_str() : nullptr,
        strEtcStr3_50 ? etcString3.c_str() : nullptr, actorClass, actorRace, actorSex, actorLevel, nEtcNum5, nEtcNum6,
        nEtcNum7, nEtcNum8, nEtcNum9, nEtcNum10, actorName, actorAccountName, targetName, targetAccountName, nItemId
    );
}

void CLogFormatter::WriteLog2(
    int nLogId,
    CCreature* actor,
    CWorldObject* target,
    std::int64_t nEtcNum5,
    std::int64_t nEtcNum6,
    std::int64_t nEtcNum7,
    std::int64_t nEtcNum8,
    std::int64_t nEtcNum9,
    std::int64_t nEtcNum10,
    std::int64_t nItemId
) {
    WriteLog1(
        nLogId, actor, target, nullptr, nullptr, nullptr, nEtcNum5, nEtcNum6, nEtcNum7, nEtcNum8, nEtcNum9, nEtcNum10,
        nItemId
    );
}

void CLogFormatter::WriteLog2_1(
    int nLogId,
    CCreature* actor,
    CWorldObject* target,
    std::int64_t nEtcNum5,
    std::int64_t nEtcNum6,
    std::int64_t nEtcNum7,
    std::int64_t nEtcNum8,
    std::int64_t nEtcNum9,
    std::int64_t nEtcNum10,
    std::int64_t nItemId
) {
    WriteLog1_1(
        nLogId, actor, target, nullptr, nullptr, nullptr, nEtcNum5, nEtcNum6, nEtcNum7, nEtcNum8, nEtcNum9, nEtcNum10,
        nItemId
    );
}

void CLogFormatter::WriteLog4(
    int nLogId,
    CCreature* actor,
    CWorldObject* target,
    Utf16Text strEtcStr1_200,
    Utf16Text strEtcStr2_50,
    Utf16Text strEtcStr3_50,
    std::int64_t nEtcNum1,
    std::int64_t nEtcNum2,
    std::int64_t nEtcNum3,
    std::int64_t nEtcNum4,
    std::int64_t nEtcNum5,
    std::int64_t nEtcNum6,
    std::int64_t nEtcNum7,
    std::int64_t nEtcNum8,
    std::int64_t nEtcNum9,
    std::int64_t nEtcNum10,
    std::int64_t nItemId
) {
    std::int64_t actorDbId = 0;
    std::int64_t actorAccountId = 0;
    std::int64_t targetDbId = 0;
    std::int64_t targetAccountId = 0;
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t z = 0;
    const char16_t* actorName = nullptr;
    const char16_t* actorAccountName = nullptr;
    const char16_t* targetName = nullptr;
    const char16_t* targetAccountName = nullptr;
    std::u16string actorNameStorage;
    std::u16string targetNameStorage;

    if (actor) {
        const FVector position = actor->GetPos();
        x = ConvertTargetCoordinate(position.x);
        y = ConvertTargetCoordinate(position.y);
        z = ConvertTargetCoordinate(position.z);

        if (actor->IsPC()) {
            User* const user = actor->CastUser();
            actorAccountId = user->GetAccountID();
            actorDbId = actor->GetDBID();
            actorNameStorage = user->GetName();
            actorName = actorNameStorage.c_str();
            actorAccountName = user->m_sAccountName.data();
        } else if (actor->IsSummon()) {
            CCreature* const summon = reinterpret_cast<CCreature*>(actor->CastSummon());
            actorDbId = summon->m_data->m_nClassID;
            actorAccountId = summon->m_data->m_nClassID;
        }
    }

    if (target) {
        if (target->IsPC()) {
            User* const user = target->CastUser();
            targetAccountId = user->GetAccountID();
            targetDbId = target->GetDBID();
            targetNameStorage = user->GetName();
            targetName = targetNameStorage.c_str();
            targetAccountName = user->m_sAccountName.data();
        } else if (target->IsSummon()) {
            CCreature* const summon = reinterpret_cast<CCreature*>(target->CastSummon());
            targetDbId = summon->m_data->m_nClassID;
            targetAccountId = summon->m_data->m_nClassID;
        } else if (target->IsDoor()) {
            targetName = target->CastDoor()->GetDoorName();
        }
    }

    const std::u16string etcString1 = ToTargetUtf16(strEtcStr1_200);
    const std::u16string etcString2 = ToTargetUtf16(strEtcStr2_50);
    const std::u16string etcString3 = ToTargetUtf16(strEtcStr3_50);
    WriteLog(
        nLogId, actorDbId, actorAccountId, targetDbId, targetAccountId, x, y, z,
        strEtcStr1_200 ? etcString1.c_str() : nullptr, strEtcStr2_50 ? etcString2.c_str() : nullptr,
        strEtcStr3_50 ? etcString3.c_str() : nullptr, nEtcNum1, nEtcNum2, nEtcNum3, nEtcNum4, nEtcNum5, nEtcNum6, nEtcNum7,
        nEtcNum8, nEtcNum9, nEtcNum10, actorName, actorAccountName, targetName, targetAccountName, nItemId
    );
}
#endif
