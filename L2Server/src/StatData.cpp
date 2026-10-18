#include "StatData.h"

#include "Shared/src/Runtime/RawAtomic.h"

#include <ctime>

UserStatData::UserStatData() {
    for (volatile std::int64_t& value : m_UserStat) {
        value = 0;
    }
}

void UserStatData::Clear() {
    for (volatile std::int64_t& value : m_UserStat) {
        value = 0;
    }
}

void UserStatData::OnEnterWorld() { m_timeEnterWorld = static_cast<std::int32_t>(std::time(nullptr)); }

void UserStatData::AddUserStat2(UserStat::UserStatType2 type, std::int64_t value) {
    runtime::raw_atomic::AddI64(&m_UserStat[static_cast<std::size_t>(type)], value);
}

void UserStatData::OnEnchantSucceeded(UserStat::ItemCategory, int) {
    // Current L2Server 0x006fe5d4, 3-byte intentional no-op.
}
