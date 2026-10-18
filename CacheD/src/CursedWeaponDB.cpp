#include "CursedWeaponDB.h"
#include "Shared/src/TemplateHelper.h"

#include "CacheSocket.h"
#include "Item.h"
#include "User.h"
#include "UserDb.h"
#include "WareHouse.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/DBConn.h"
#include "Shared/src/LogFormatter.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/TextRuntime.h"

#include <array>
#include <cstdint>
#include <string>

namespace {

// Current unnamed helpers 0x004a248c and 0x004a253c are the signed- and
// unsigned-item-ID instantiations of this exact nine-parameter operation. No
// surviving symbol establishes an original helper identifier, so it remains
// private to the confirmed CCursedWeaponDB owner.
template <class ItemId>
void PersistCursedWeaponRow(
    DBConn& connection,
    const ItemId& itemId,
    const int& createDate,
    const int& lastKillDate,
    const int& expiredDate,
    const double& killPoint,
    const int& characterId,
    const int& itemClassId,
    const int& originalPk,
    const int& totalPk
) {
    constexpr const wchar_t* statement = L"{Call lin_AddCursedWeapon (?,?,?,?,?,?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&itemId);
    connection.BindParameter(&createDate);
    connection.BindParameter(&lastKillDate);
    connection.BindParameter(&expiredDate);
    connection.BindParameter(&killPoint);
    connection.BindParameter(&characterId);
    connection.BindParameter(&itemClassId);
    connection.BindParameter(&originalPk);
    connection.BindParameter(&totalPk);
    connection.ExecutePrepared(statement);
}

} // namespace

bool CCursedWeaponDB::RequestLoadCursedWeaponList(CSocket* socket, const unsigned char* packet [[maybe_unused]]) {
    // Current Cached.exe 0x004a2224. Paired current L2Server consumer
    // CCursedWeaponDBHandler::ReplyLoadCursedWeaponList is at 0x00641838.
    L2_GUARD_FUNCTION();

    int itemId = 0;
    int createDate = 0;
    int lastKillDate = 0;
    int expiredDate = 0;
    double killPoint = 0.0;
    int characterId = 0;
    int itemClassId = 0;
    int originalPk = 0;
    int totalPk = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&itemId);
    connection.Bind(&createDate);
    connection.Bind(&lastKillDate);
    connection.Bind(&expiredDate);
    connection.Bind(&killPoint);
    connection.Bind(&characterId);
    connection.Bind(&itemClassId);
    connection.Bind(&originalPk);
    connection.Bind(&totalPk);

    std::array<char, 0x5000> rows{};
    int rowCount = 0;
    int rowBytes = 0;
    constexpr const wchar_t* statement = L"{Call lin_LoadCursedWeaponList}";
    connection.Prepare(statement);
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            // Shipping code increments the count even after Assemble returns
            // zero at the fixed 0x5000 boundary. Preserve that wire anomaly.
            rowBytes += ::Assemble(
                rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "ddddfdddd", itemId, createDate,
                lastKillDate, expiredDate, killPoint, characterId, itemClassId, originalPk, totalPk
            );
            ++rowCount;
        }
    }

    socket->Send("chdb", 0xc6, 0x2f, rowCount, rowBytes, rows.data());
    return false;
}

bool CCursedWeaponDB::RequestPickCursedWeapon(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x004a2690 and CursedWeaponDB.cpp line 0x61. The
    // opposite current CDB::RequestPickItem branch is at 0x0066fefc.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int requestedItemId = 0;
    int itemClassId = 0;
    int createDate = 0;
    int lastKillDate = 0;
    int expiredDate = 0;
    double killPoint = 0.0;
    int originalPk = 0;
    int totalPk = 0;
    int itemServerId = 0;
    // The current sender reads this final dword directly from its CItem+0x74.
    // CacheD uses it only as nEtcNum5 in record 0x38a. No original member name
    // survives, so the portable name states only that observed role.
    int itemAuditContext = 0;
    ::Disassemble(
        packet, "ddddddfdddd", &characterId, &requestedItemId, &itemClassId, &createDate, &lastKillDate, &expiredDate,
        &killPoint, &originalPk, &totalPk, &itemServerId, &itemAuditContext
    );

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    CItemSP item;
    DBConn connection(runtime::process::CurrentDatabaseId());
    bool changed = false;

    if (user) {
        {
            CWareHouseSP inventory = user->GetWareHouse(0, true);
            if (inventory) {
                L2_WRITE_GUARD(*inventory.operator->());
                if (requestedItemId < 1) {
                    const ItemOptionKey emptyOptionKey{};
                    const BaseAttribute emptyBaseAttribute{};
                    item = inventory->MakeNewItem(
                        static_cast<unsigned int>(characterId), static_cast<unsigned int>(itemClassId), 1, 0, 0, 0, 0, 0,
                        static_cast<int>(inventory->GetWarehouseNum()), emptyOptionKey, 0, emptyBaseAttribute, 0, 0
                    );
                    if (item) {
                        inventory->PushItem(item.get());
                        const unsigned int generatedItemId = item->ItemID();
                        PersistCursedWeaponRow(
                            connection, generatedItemId, createDate, lastKillDate, expiredDate, killPoint, characterId,
                            itemClassId, originalPk, totalPk
                        );
                        changed = true;
                    }
                } else {
                    item = CItem::Load(static_cast<std::uint32_t>(requestedItemId));
                    if (item) {
                        item->Delete(false, false);
                        item->setOwnerID(static_cast<std::uint32_t>(characterId));
                        inventory->PushItem(item.get());
                        item->Save(true);
                        PersistCursedWeaponRow(
                            connection, requestedItemId, createDate, lastKillDate, expiredDate, killPoint, characterId,
                            itemClassId, originalPk, totalPk
                        );
                        changed = true;
                    }
                }
            }
        }

        if (changed) {
            std::array<char16_t, 256> shapeShiftText{};
            std::array<char16_t, 256> baseAttributeText{};
            std::array<char16_t, 256> ensoulText{};
            item->MakeShapeShiftLogInfo(shapeShiftText.data());
            BaseAttribute baseAttribute(item->Attribute());
            baseAttribute.GetLogString(baseAttributeText.data());
            // item->MakeEnsoulLogInfo(ensoulText.data())
            item->MakeEnsoulLogInfo(ensoulText.data(), ensoulText.size());

            const std::u16string shapeShift = std::u16string(shapeShiftText.data());
            const std::u16string attribute = std::u16string(baseAttributeText.data());
            const std::u16string ensoul = std::u16string(ensoulText.data());
            const std::u16string characterName = std::u16string(user->GetCharName());
            const std::u16string accountName = std::u16string(user->GetAccountName());

            CLogFormatter formatter;
            formatter.WriteLog(
                0x38a, user->GetId(), user->GetAccountID(), 0, 0, user->GetX(), user->GetY(), user->GetZ(),
                shapeShift.c_str(), attribute.c_str(), ensoul.c_str(), user->GetRace(), 0, user->GetClass(),
                user->GetLevel(), itemAuditContext, requestedItemId, 0, itemClassId, 1, 1, characterName.c_str(),
                accountName.c_str(), nullptr, nullptr, item->ItemID()
            );

            formatter.WriteLog(
                0x3cf, user->GetId(), user->GetAccountID(), 0, 0, user->GetX(), user->GetY(), user->GetZ(), nullptr, nullptr,
                nullptr, itemClassId, user->GetRace(), user->GetGender(), user->GetClass(), user->GetLevel(), 0, 0, 0, 0, 0,
                characterName.c_str(), accountName.c_str(), nullptr, nullptr, item->ItemID()
            );

            socket->Send(
                "chddddd", 0xc6, 0x3d, characterId, static_cast<int>(item->ItemID()), itemServerId, originalPk, characterId
            );
        }
    }
    return false;
}

bool CCursedWeaponDB::RequestAddCursedWeapon(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x004a3530.
    L2_GUARD_FUNCTION();

    int itemId = 0;
    int createDate = 0;
    int lastKillDate = 0;
    int expiredDate = 0;
    double killPoint = 0.0;
    int characterId = 0;
    int itemClassId = 0;
    int originalPk = 0;
    int totalPk = 0;
    ::Disassemble(
        packet, "ddddfdddd", &itemId, &createDate, &lastKillDate, &expiredDate, &killPoint, &characterId, &itemClassId,
        &originalPk, &totalPk
    );

    DBConn connection(runtime::process::CurrentDatabaseId());
    PersistCursedWeaponRow(
        connection, itemId, createDate, lastKillDate, expiredDate, killPoint, characterId, itemClassId, originalPk, totalPk
    );
    return false;
}

bool CCursedWeaponDB::RequestUpdateCursedWeapon(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x004a36d8 and unnamed six-parameter helper
    // 0x004a25ec.
    L2_GUARD_FUNCTION();

    int itemId = 0;
    double killPoint = 0.0;
    int lastKillDate = 0;
    int expiredDate = 0;
    int characterId = 0;
    int totalPk = 0;
    ::Disassemble(packet, "dfdddd", &itemId, &killPoint, &lastKillDate, &expiredDate, &characterId, &totalPk);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateCursedWeapon (?,?,?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&itemId);
    connection.BindParameter(&killPoint);
    connection.BindParameter(&lastKillDate);
    connection.BindParameter(&expiredDate);
    connection.BindParameter(&characterId);
    connection.BindParameter(&totalPk);
    connection.ExecutePrepared(statement);
    return false;
}

bool CCursedWeaponDB::RequestDelCursedWeapon(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x004a384c.
    L2_GUARD_FUNCTION();

    int itemId = 0;
    ::Disassemble(packet, "d", &itemId);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DelCursedWeapon (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&itemId);
    connection.ExecutePrepared(statement);
    return false;
}

bool CCursedWeaponDB::RequestDelCursedWeaponOnlyInDB(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x004a3980 and CursedWeaponDB.cpp lines
    // 0x10d/0x116.
    L2_GUARD_FUNCTION();

    int itemId = 0;
    int originalPk = 0;
    ::Disassemble(packet, "dd", &itemId, &originalPk);

    {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_DelCursedWeapon (?)}";
        connection.Prepare(statement);
        connection.BindParameter(&itemId);
        connection.ExecutePrepared(statement);
    }

    CItemSP item = CItem::Load(static_cast<std::uint32_t>(itemId));
    if (!item)
        return false;

    CUserSP user = g_userDb.GetUser(static_cast<int>(item->OwnerID()), true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        {
            L2_WRITE_GUARD(*user.operator->());
            user->ResetPKCountWhenCursedPCDied(originalPk);
        }
        user->Save();

        CWareHouseSP inventory = user->GetWareHouse(0, false);
        if (inventory) {
            {
                L2_WRITE_GUARD(*inventory.operator->());
                inventory->PopItem(itemId);
            }
        }
    }
    item->Delete(true, false);
    return false;
}
