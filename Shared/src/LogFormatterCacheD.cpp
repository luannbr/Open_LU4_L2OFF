#include "Shared/src/LogFormatter.h"

#include "Item.h"
#include "User.h"

#include "L2Core/src/inc/Guard.h"
#include "TextRuntime.h"

#include <climits>
#include <string>

void CLogFormatter::WriteLogCache1(
    int nLogId,
    CUser* user,
    CItem* item,
    Utf16Text strEtcStr1_200,
    Utf16Text strEtcStr2_50,
    Utf16Text strEtcStr3_50,
    std::int64_t nEtcNum5,
    std::int64_t nEtcNum6,
    std::int64_t nEtcNum7,
    std::int64_t nEtcNum8,
    std::int64_t nEtcNum9,
    std::int64_t nEtcNum10
) {
    // Current Cached.exe 0x004efe58. The target rejects a null user or item,
    // reads these same CUser/CItem fields, and forwards them in this order to
    // CLogFormatter::WriteLog at 0x004efa60.
    L2_GUARD_FUNCTION();

    if (!user || !item) {
        return;
    }

    const std::u16string etc1 = strEtcStr1_200.String();
    const std::u16string etc2 = strEtcStr2_50.String();
    const std::u16string etc3 = strEtcStr3_50.String();
    const std::u16string characterName = user->m_characterName;
    const std::u16string accountName = user->m_accountName;

    WriteLog(
        nLogId, user->m_characterId, user->m_accountId, item->OwnerID(), item->Amount(), user->m_x, user->m_y, user->m_z,
        etc1.c_str(), etc2.c_str(), etc3.c_str(), user->m_characterClass, user->m_race, user->m_gender, user->m_level,
        nEtcNum5, nEtcNum6, nEtcNum7, nEtcNum8, nEtcNum9, nEtcNum10, characterName.c_str(), accountName.c_str(), nullptr,
        nullptr, item->ItemID()
    );
}

void CLogFormatter::WriteLogCache2(
    int nLogId,
    CUser* user,
    CItem* item,
    std::int64_t nEtcNum5,
    std::int64_t nEtcNum6,
    std::int64_t nEtcNum7,
    std::int64_t nEtcNum8,
    std::int64_t nEtcNum9,
    std::int64_t nEtcNum10
) {
    L2_GUARD_FUNCTION();
    WriteLogCache1(
        nLogId, user, item, nullptr, nullptr, nullptr, nEtcNum5, nEtcNum6, nEtcNum7, nEtcNum8, nEtcNum9, nEtcNum10
    );
}

// The shipping source owner is Shared/LogFormatter.cpp. This CacheD-only
// translation unit prevents the single Shared archive from importing CUser
// dependencies into L2NPC while retaining one physical Shared implementation.
void CLogFormatter::WriteLogCache3(
    int nLogId,
    CUser* user,
    std::int64_t nEtcNum5,
    std::int64_t nEtcNum6,
    std::int64_t nEtcNum7,
    std::int64_t nEtcNum8,
    std::int64_t nEtcNum9,
    std::int64_t nEtcNum10
) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return;
    }

    const std::u16string characterName = user->m_characterName;
    const std::u16string accountName = user->m_accountName;
    WriteLog(
        nLogId, user->m_characterId, user->m_accountId, 0, 0, user->m_x, user->m_y, user->m_z, nullptr, nullptr, nullptr,
        user->m_characterClass, user->m_race, user->m_gender, user->m_level, nEtcNum5, nEtcNum6, nEtcNum7, nEtcNum8,
        nEtcNum9, nEtcNum10, characterName.c_str(), accountName.c_str(), nullptr, nullptr, 0
    );
}
