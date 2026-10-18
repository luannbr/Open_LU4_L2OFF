#include "BattleCampDb.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/DBConn.h"
#include "Shared/src/ScopedLockGuards.h"
#include "Portable/ProcessRuntime.h"

CBattleCampDb g_battleCampDb;

CBattleCampDb::CBattleCampDb() = default;
CBattleCampDb::~CBattleCampDb() = default;

std::uint32_t CBattleCampDb::InstallBattleCamp(
    std::uint32_t castleId, std::uint32_t pledgeId, std::uint32_t pledgeWorldId, int maxHp, int x, int y, int z, int baseType
) {
    // Current Cached.exe 0x004512cc.
    L2_GUARD_FUNCTION();

    std::uint32_t battleCampId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&battleCampId);

    // The target clamps only values greater than two. Negative values keep
    // their bit pattern and are passed to the procedure unchanged.
    if (baseType > 2)
        baseType = 0;

    {
        L2_WRITE_GUARD(m_lock);
        constexpr const wchar_t* statement = L"{Call lin_InstallBattleCamp (?, ?, ?, ?, ?, ?, ?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&pledgeId);
        connection.BindParameter(&pledgeWorldId);
        connection.BindParameter(&castleId);
        connection.BindParameter(&maxHp);
        connection.BindParameter(&maxHp);
        connection.BindParameter(&x);
        connection.BindParameter(&y);
        connection.BindParameter(&z);
        connection.BindParameter(&baseType);
        if (connection.ExecutePrepared(statement)) {
            connection.Fetch();
        }
    }
    return battleCampId;
}

std::uint32_t CBattleCampDb::UninstallBattleCamp(std::uint32_t pledgeId, std::uint32_t pledgeWorldId, int baseType) {
    // Current Cached.exe 0x0045145c.
    L2_GUARD_FUNCTION();

    std::uint32_t battleCampId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&battleCampId);

    {
        L2_WRITE_GUARD(m_lock);
        constexpr const wchar_t* statement = L"{Call lin_UninstallBattleCamp (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&pledgeId);
        connection.BindParameter(&pledgeWorldId);
        connection.BindParameter(&baseType);
        if (connection.ExecutePrepared(statement)) {
            connection.Fetch();
        }
    }
    return battleCampId;
}

void CBattleCampDb::UninstallAllBattleCamp(std::uint32_t castleId, std::vector<std::uint32_t>* battleCamps) {
    // Current Cached.exe 0x004515ec. Both base-type passes execute under one
    // write lock and reuse one DBConn after clearing its bindings.
    L2_GUARD_FUNCTION();

    std::uint32_t battleCampId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&battleCampId);

    {
        L2_WRITE_GUARD(m_lock);
        for (int baseType = 0; baseType < 2; ++baseType) {
            constexpr const wchar_t* statement = L"{Call lin_UninstallAllBattleCamp (?, ?)}";
            connection.Prepare(statement);
            connection.BindParameter(&castleId);
            connection.BindParameter(&baseType);
            if (connection.ExecutePrepared(statement)) {
                while (connection.Fetch()) {
                    battleCamps->push_back(battleCampId);
                }
            }
            connection.UnBind();
            connection.Bind(&battleCampId);
        }
    }
}
