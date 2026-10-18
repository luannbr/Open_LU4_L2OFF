#include "NewCharDB.h"

#include "Cached.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/DBConn.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/TemplateHelper.h"

#include <cstdint>

CNewCharDB g_newCharDB;

CNewCharDB::CNewCharDB() = default;
CNewCharDB::~CNewCharDB() = default;

void CNewCharDB::LoadNewCharList() {
    std::uint32_t accountId = 0;
    std::uint32_t characterId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&accountId);
    connection.Bind(&characterId);

    // Declaration order is observable in current 0x004f96a8: this Guard is
    // destroyed (TemplateHelper.h:0x65) before the earlier DBConn local.
    L2_GUARD(static_cast<CIOCriticalSection&>(*this));

    constexpr const wchar_t* statement = L"{Call lin_LoadNewCharList}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement))
        return;

    while (connection.Fetch()) {
        m_newCharacters.emplace(static_cast<int>(accountId), static_cast<int>(characterId));
    }
}

void CNewCharDB::Insert(int accountId, int characterId) {
    L2_GUARD_FUNCTION();

    // Here the target constructs Guard before DBConn, so DBConn is destroyed
    // first and the lock remains held across the complete database operation.
    L2_GUARD(static_cast<CIOCriticalSection&>(*this));

    int result = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    constexpr const wchar_t* statement = L"{Call lin_SaveNewChar (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&accountId);
    connection.BindParameter(&characterId);
    if (connection.ExecutePrepared(statement) && connection.Fetch() && result == 1) {
        m_newCharacters.emplace(accountId, characterId);
    }
}
