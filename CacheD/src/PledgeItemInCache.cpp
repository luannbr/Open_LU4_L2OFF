#include "Shared/src/Log.h"
#include "Shared/src/TemplateHelper.h"

#include "PledgeItemInCache.h"

#include "Cached.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/DBConn.h"
#include "Portable/ProcessRuntime.h"
#include "Runtime.h"

PledgeItemInCache::PledgeItemInCache(std::int32_t pledgeId) : m_pledgeId(pledgeId), m_targetAlignment(0) {
    // Current constructor 0x00520200: RWLock, pledge id, then xmap.
}

PledgeItemInCache::~PledgeItemInCache() = default;
// Current Cached.exe 0x00507454 destroys the +0x40 item tree and its
// sentinel, then destroys the leading RWLock. The defaulted portable
// destructor expresses the same member-owned sequence.

bool PledgeItemInCache::_AddItem(std::int32_t itemClassId, std::int32_t expiryTime) {
    L2_GUARD_FUNCTION();

    const auto found = m_items.find(itemClassId);
    if (found == m_items.end()) {
        m_items.emplace(itemClassId, expiryTime);
    } else {
        found->second = expiryTime;
    }
    // Current 0x0051fae4 has no rejecting branch.
    return true;
}

bool PledgeItemInCache::AddItem(std::int32_t itemClassId, std::int32_t expiryTime) {
    L2_GUARD_FUNCTION();

    bool result;
    {
        L2_WRITE_GUARD(m_lock);
        result = _AddItem(itemClassId, expiryTime);
    }
    return result;
}

bool PledgeItemInCache::LoadFromDB() {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    std::int32_t itemClassId = 0;
    std::int32_t expiryTime = 0;
    connection.Bind(&itemClassId);
    connection.Bind(&expiryTime);

    constexpr const wchar_t* statement = L"{Call lin_PledgeItemLoad (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_pledgeId);
    if (!connection.ExecutePrepared(statement)) {
        return false;
    }

    {
        L2_WRITE_GUARD(m_lock);
        while (connection.Fetch()) {
            _AddItem(itemClassId, expiryTime);
        }
    }
    return true;
}

bool PledgeItemInCache::AddItemAndSave(std::int32_t itemClassId, std::int32_t expiryTime) {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_PledgeItemSave (?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_pledgeId);
    connection.BindParameter(&itemClassId);
    connection.BindParameter(&expiryTime);
    if (!connection.ExecutePrepared(statement)) {
        return false;
    }

    // Current 0x00520088 persists first and publishes the new expiry only
    // after SQL succeeds.
    return AddItem(itemClassId, expiryTime);
}

void PledgeItemStuff::SelectPledgeItems(const PledgeItemInCache* item, std::list<protocol::PkPledgeItem>& items) {
    L2_GUARD_FUNCTION();

    if (!item) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[PLEDGE][PledgeItemInCache.cpp][109]assert falied: "
        //     "(pItem != NULL)"
        // )
        // Log.Add(LOG_ERROR,
        //     "[clan items] Cannot select clan items: item cache is unavailable, output list left unchanged.")
        L2_LOG_ERROR(Log,
            "[clan items] Cannot select clan items: item cache is unavailable; output list left unchanged.");
        return;
    }

    item->SelectPledgeItems(items);
}
