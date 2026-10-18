#include "Shared/src/Log.h"

#include "TimeAttackDB.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/DBConn.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/Runtime/TargetFormatting.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/StringTokenizer.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/Utils.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/TextRuntime.h"
#include "Runtime.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

namespace {

constexpr int kFirstRoom = 1;
constexpr int kRoomCount = 5;
constexpr int kMaximumMemberCount = 9;
constexpr std::size_t kMaximumMemberNameLength = 24;

std::u16string_view TargetStringView(const char16_t* value) {
    const char16_t* end = value;
    while (*end != u'\0')
        ++end;
    return {value, static_cast<std::size_t>(end - value)};
}

int LowTargetInt32(std::int64_t value) {
    const std::uint32_t low = static_cast<std::uint32_t>(value);
    int result = 0;
    static_assert(sizeof(result) == sizeof(low));
    std::memcpy(&result, &low, sizeof(result));
    return result;
}

bool IsTargetSpace(char16_t value) {
    return value == u' ' || value == u'\t' || value == u'\n' || value == u'\r' || value == u'\f' || value == u'\v';
}

int TargetWtoi(std::u16string_view value) {
    std::size_t offset = 0;
    while (offset < value.size() && IsTargetSpace(value[offset])) {
        ++offset;
    }

    bool negative = false;
    if (offset < value.size() && (value[offset] == u'+' || value[offset] == u'-')) {
        negative = value[offset] == u'-';
        ++offset;
    }
    if (offset >= value.size() || value[offset] < u'0' || value[offset] > u'9') {
        return 0;
    }

    constexpr std::uint64_t positiveLimit = static_cast<std::uint64_t>(std::numeric_limits<int>::max());
    constexpr std::uint64_t negativeLimit = positiveLimit + 1U;
    const std::uint64_t limit = negative ? negativeLimit : positiveLimit;
    std::uint64_t result = 0;
    while (offset < value.size() && value[offset] >= u'0' && value[offset] <= u'9') {
        const unsigned digit = static_cast<unsigned>(value[offset] - u'0');
        if (result > (limit - digit) / 10U) {
            result = limit;
        } else {
            result = result * 10U + digit;
        }
        ++offset;
    }

    if (!negative)
        return static_cast<int>(result);
    if (result == negativeLimit)
        return std::numeric_limits<int>::min();
    return -static_cast<int>(result);
}

} // namespace

TimeAttackRecord::TimeAttackRecord(int roomNo, int recordType, int round) {
    L2_GUARD_FUNCTION();

    m_roomNo = roomNo;
    m_recordType = recordType;
    m_ssqRound = round;
    m_ssqPart = 0;
    m_point = 0;
    m_recordTime = 0;
    m_elapsedTime = 0;
    m_memberCount = 0;
    m_fee = 0;
    ResetMemberInfo();
}

TimeAttackRecord::~TimeAttackRecord() = default;

void TimeAttackRecord::ResetMemberInfo() {
    L2_GUARD_FUNCTION();

    m_memberCount = 0;
    std::memset(m_memberNameList, 0, sizeof(m_memberNameList));
    std::memset(m_memberDatabaseIdList, 0, sizeof(m_memberDatabaseIdList));
    std::memset(m_memberRewardFlags, 0, sizeof(m_memberRewardFlags));
}

int TimeAttackRecord::Assemble(char* outputBuffer, int bufferSize) {
    L2_GUARD_FUNCTION();

    int assembled = ::Assemble(
        outputBuffer, bufferSize, "ddddQdddQ", m_roomNo, m_recordType, m_ssqRound, m_ssqPart,
        static_cast<long long>(m_point), LowTargetInt32(m_recordTime), m_elapsedTime, m_memberCount,
        static_cast<long long>(m_fee)
    );

    for (int index = 0; index < m_memberCount; ++index) {
        assembled += ::Assemble(outputBuffer + assembled, bufferSize - assembled, "S", m_memberNameList[index]);
        assembled += ::Assemble(
            outputBuffer + assembled, bufferSize - assembled, "dd", m_memberDatabaseIdList[index], m_memberRewardFlags[index]
        );
    }
    return assembled;
}

int TimeAttackRecord::IsMember(int userDatabaseId) const {
    L2_GUARD_FUNCTION();

    for (int index = 0; index < m_memberCount; ++index) {
        if (m_memberDatabaseIdList[index] == userDatabaseId)
            return 1;
    }
    return 0;
}

int TimeAttackRecord::SetRewardFlag(int userDatabaseId, int flag) {
    L2_GUARD_FUNCTION();

    for (int index = 0; index < m_memberCount; ++index) {
        if (m_memberDatabaseIdList[index] == userDatabaseId) {
            m_memberRewardFlags[index] = flag;
            return 1;
        }
    }
    return 0;
}

bool TimeAttackRecord::SetMemberFromDBField(const char16_t* names) {
    L2_GUARD_FUNCTION();

    const std::u16string source(names != nullptr ? names : u"");
    const std::u16string delimiter{u","};
    StringTokenizer tokenizer(source, delimiter);
    const int tokenCount = tokenizer.CountTokens();
    for (int index = 0; index < tokenCount; ++index) {
        const std::u16string token = tokenizer.NextToken();
        if (index < kMaximumMemberCount) {
            const std::size_t copied = std::min(token.size(), kMaximumMemberNameLength);
            std::copy_n(token.data(), copied, m_memberNameList[index]);
            m_memberNameList[index][copied] = u'\0';
            // The target writes the last slot even when the token
            // was shorter than the 24-code-unit secure-copy limit.
            m_memberNameList[index][kMaximumMemberNameLength] = u'\0';
        }
    }
    return true;
}

bool TimeAttackRecord::SetDBIDFromDBField(const char16_t* list) {
    L2_GUARD_FUNCTION();

    const std::u16string source(list != nullptr ? list : u"");
    const std::u16string delimiter{u","};
    StringTokenizer tokenizer(source, delimiter);
    const int tokenCount = tokenizer.CountTokens();
    int* destination = m_memberDatabaseIdList;
    for (int index = 0; index < tokenCount; ++index) {
        const std::u16string token = tokenizer.NextToken();
        *destination++ = TargetWtoi(runtime::StringView(token));
    }
    return true;
}

bool TimeAttackRecord::LogForAudit() const {
    L2_GUARD_FUNCTION();

    std::u16string memberNames;
    for (int index = 0; index < m_memberCount; ++index) {
        memberNames.append(TargetStringView(m_memberNameList[index]));
        if (index < m_memberCount - 1)
            memberNames.push_back(u',');
    }
    const std::u16string auditMemberNames = ReplaceStr(std::u16string(std::u16string_view(memberNames)), u",", u" ");

    // Exact route-8, 26-field current audit record at 0x0059bb1c.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x597);
    fields[8].assign(runtime::StringView(auditMemberNames));
    fields[11] = runtime::DecimalUtf16(m_roomNo);
    fields[12] = runtime::DecimalUtf16(m_recordType);
    fields[13] = runtime::DecimalUtf16(m_ssqRound);
    fields[14] = runtime::DecimalUtf16(m_ssqPart);
    fields[15] = runtime::DecimalUtf16(m_point);

    std::u16string record;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (index != 0)
            record.push_back(u',');
        record.append(fields[index]);
    }
    runtime::process::WriteLogFormatterLine(8, record);

    // The shipping epilogue leaves RAX unspecified and all current callers
    // discard it. A deterministic value is required by portable C++.
    return true;
}

bool TimeAttackRecord::Create() {
    L2_GUARD_FUNCTION();

    std::u16string targetNames;
    std::wstring databaseIds;
    int rewardFlags = 0;
    for (int index = 0; index < m_memberCount; ++index) {
        targetNames.append(TargetStringView(m_memberNameList[index]));
        databaseIds.append(std::to_wstring(m_memberDatabaseIdList[index]));
        if (m_memberRewardFlags[index] != 0) {
            rewardFlags |= 1 << (static_cast<unsigned>(index) & 31U);
        }
        if (index < m_memberCount - 1) {
            targetNames.push_back(u',');
            databaseIds.push_back(L',');
        }
    }
    const std::u16string memberNames(targetNames);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_CreateTimeAttackRecord (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
                                         L"?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_roomNo);
    connection.BindParameter(&m_recordType);
    connection.BindParameter(&m_ssqRound);
    connection.BindParameter(&m_ssqPart);
    connection.BindParameter(&m_point);
    connection.BindParameter(&m_recordTime);
    connection.BindParameter(&m_elapsedTime);
    connection.BindParameter(&m_memberCount);
    connection.BindParameter(memberNames.c_str());
    connection.BindParameter(databaseIds.c_str());
    connection.BindParameter(&rewardFlags);
    connection.BindParameter(&m_fee);

    const bool created = connection.ExecutePrepared(statement);
    if (!created) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]Cannot create time attack info. room_no=[%d], "
        //     "record_type=[%d]",
        //     kSourceFile, 0xd5, m_roomNo, m_recordType
        // )
        // Log.Add(LOG_ERROR,
        //     "[time attack] Cannot create the record for room %d, type %d, Seven Signs round %d in database %d.",
        //     m_roomNo, m_recordType, m_ssqRound, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[time attack] Cannot create the record for room %d, type %d, Seven Signs round %d in database %d.",
            m_roomNo, m_recordType, m_ssqRound, runtime::process::CurrentDatabaseId());
    }
    return created;
}

bool TimeAttackRecord::Save() {
    L2_GUARD_FUNCTION();

    std::u16string targetNames;
    std::wstring databaseIds;
    int rewardFlags = 0;
    for (int index = 0; index < m_memberCount; ++index) {
        targetNames.append(TargetStringView(m_memberNameList[index]));
        databaseIds.append(std::to_wstring(m_memberDatabaseIdList[index]));
        if (m_memberRewardFlags[index] != 0) {
            rewardFlags |= 1 << (static_cast<unsigned>(index) & 31U);
        }
        if (index < m_memberCount - 1) {
            targetNames.push_back(u',');
            databaseIds.push_back(L',');
        }
    }
    const std::u16string memberNames(targetNames);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveTimeAttackRecord (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
                                         L"?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_roomNo);
    connection.BindParameter(&m_recordType);
    connection.BindParameter(&m_ssqRound);
    connection.BindParameter(&m_ssqPart);
    connection.BindParameter(&m_point);
    connection.BindParameter(&m_recordTime);
    connection.BindParameter(&m_elapsedTime);
    connection.BindParameter(&m_memberCount);
    connection.BindParameter(memberNames.c_str());
    connection.BindParameter(databaseIds.c_str());
    connection.BindParameter(&rewardFlags);
    connection.BindParameter(&m_fee);

    const bool saved = connection.ExecutePrepared(statement);
    if (!saved) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]Cannot save time attack info. room_no=[%d], "
        //     "record_type=[%d]",
        //     kSourceFile, 0x14f, m_roomNo, m_recordType
        // )
        // Log.Add(LOG_ERROR,
        //     "[time attack] Cannot save the record for room %d, type %d, Seven Signs round %d in database %d.",
        //     m_roomNo, m_recordType, m_ssqRound, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[time attack] Cannot save the record for room %d, type %d, Seven Signs round %d in database %d.",
            m_roomNo, m_recordType, m_ssqRound, runtime::process::CurrentDatabaseId());
    }
    return saved;
}

// Current Cached.exe 0x0059cb2c constructs the ordered record map and clears
// the compiler's function-local-static initialization carrier.
TimeAttackDB::TimeAttackDB() = default;
TimeAttackDB::~TimeAttackDB() = default;

TimeAttackDB& TimeAttackDB::Instance() {
    static TimeAttackDB instance;
    return instance;
}

TimeAttackRecord* TimeAttackDB::GetRecord(int roomNo, int recordType) const {
    L2_GUARD_FUNCTION();

    const auto found = m_records.find({roomNo, recordType});
    return found == m_records.end() ? nullptr : found->second;
}

int TimeAttackDB::AssembleAll(char* outputBuffer, int bufferSize) {
    L2_GUARD_FUNCTION();

    int assembled = 0;
    for (int roomNo = kFirstRoom; roomNo < kFirstRoom + kRoomCount; ++roomNo) {
        for (int recordType = 0; recordType < TOP_OF_MAX; ++recordType) {
            TimeAttackRecord* const record = GetRecord(roomNo, recordType);
            if (!record)
                continue;

            int recordSize = 0;
            {
                L2_READ_GUARD(*record);
                recordSize = record->Assemble(outputBuffer + assembled, bufferSize - assembled);
            }
            if (recordSize == 0)
                return 0;
            assembled += recordSize;
        }
    }
    return assembled;
}

int TimeAttackDB::Create(int round) {
    L2_GUARD_FUNCTION();

    for (int roomNo = kFirstRoom; roomNo < kFirstRoom + kRoomCount; ++roomNo) {
        for (int recordType = 0; recordType < TOP_OF_MAX; ++recordType) {
            TimeAttackRecord* const record = new TimeAttackRecord(roomNo, recordType, round);
            record->Create();
            m_records[{roomNo, recordType}] = record;
        }
    }
    return static_cast<int>(m_records.size());
}

bool TimeAttackDB::Load(int roundNumber) {
    L2_GUARD_FUNCTION();

    int roomNo = 0;
    int recordType = 0;
    int round = 0;
    int part = 0;
    int point = 0;
    int recordTime = 0;
    int elapsedTime = 0;
    int memberCount = 0;
    char16_t memberNames[257]{};
    char16_t memberDatabaseIds[129]{};
    int rewardFlags = 0;
    int fee = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&roomNo);
    connection.Bind(&recordType);
    connection.Bind(&round);
    connection.Bind(&part);
    connection.Bind(&point);
    connection.Bind(&recordTime);
    connection.Bind(&elapsedTime);
    connection.Bind(&memberCount);
    connection.Bind(memberNames, 0x202);
    connection.Bind(memberDatabaseIds, 0x102);
    connection.Bind(&rewardFlags);
    connection.Bind(&fee);

    constexpr const wchar_t* statement = L"{Call lin_LoadTimeAttackRecord (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&roundNumber);
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            TimeAttackRecord* const record = new TimeAttackRecord(roomNo, recordType, round);
            record->m_ssqPart = part;
            record->m_point = point;
            record->m_recordTime = recordTime;
            record->m_elapsedTime = elapsedTime;
            record->m_memberCount = memberCount;
            record->m_fee = fee;
            record->SetMemberFromDBField(memberNames);
            record->SetDBIDFromDBField(memberDatabaseIds);
            for (int index = 0; index < memberCount; ++index) {
                record->m_memberRewardFlags[index] =
                    (static_cast<unsigned>(rewardFlags) >> (static_cast<unsigned>(index) & 31U)) & 1U;
            }
            m_records[{roomNo, recordType}] = record;
        }
    }

    if (m_records.empty())
        return Create(roundNumber) > 0;
    return true;
}

bool TimeAttackDB::InitializeTo(int round) {
    L2_GUARD_FUNCTION();

    // Current Cached.exe 0x0059cb90 delegates this traversal to the unnamed
    // range body at 0x0059ca9c and its per-record body at 0x0059c5ec.
    for (auto& entry : m_records) {
        TimeAttackRecord* const record = entry.second;
        if (!record)
            continue;

        L2_WRITE_GUARD(*record);
        record->m_fee = 0;
        record->m_ssqRound = round;
        if (record->m_recordType != TOP_OF_ROOM) {
            record->m_ssqPart = 0;
            record->m_point = 0;
            record->m_recordTime = 0;
            record->m_elapsedTime = 0;
            record->ResetMemberInfo();
        }
        record->Create();
    }
    return true;
}
