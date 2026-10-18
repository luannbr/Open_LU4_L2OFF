#include "IOCPMessageCached.h"
#include "Shared/src/TemplateHelper.h"

#include "CacheSocket.h"
#include "Pledge.h"
#include "User.h"
#include "UserDb.h"
#include "WareHouse.h"

#include "L2Core/src/inc/Guard.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/Log.h"

std::atomic<std::int32_t> g_nPendingCharacterItemChecks{-1};
std::atomic<std::int32_t> g_nPendingPledgeItemChecks{-1};

namespace {

void CompleteIfAllItemChecksFinished(const char* message) {
    if (g_nPendingCharacterItemChecks.load(std::memory_order_seq_cst) == 0 &&
        g_nPendingPledgeItemChecks.load(std::memory_order_seq_cst) == 0) {
        CompleteCacheDShutdownPersistence(message);
    }
}

} // namespace

CItemCheckOnCharLoadedMessage::CItemCheckOnCharLoadedMessage(CUser* user) noexcept : m_user(user) {}

void CItemCheckOnCharLoadedMessage::OnReceive(int argument [[maybe_unused]]) {
    L2_GUARD_FUNCTION();
    // if (m_user)
    //     m_user->ItemCheckOnUnload()
    if (m_user) {
        m_user->ItemCheckOnUnload();
    } else {
        // Log.Add(LOG_ERROR,
        //     "[disconnect cleanup] Cannot check character items: the callback has no character object, its pending count will still be decremented.")
        L2_LOG_ERROR(Log,
            "[disconnect cleanup] Cannot check character items: the callback has no character object; its pending count will still be decremented.");
    }
    g_nPendingCharacterItemChecks.fetch_sub(1, std::memory_order_seq_cst);
    // CompleteIfAllItemChecksFinished(
    //     "[NO_ERROR] All Characters are log-out. "
    //     "Now CacheD can be shut-down!!!"
    // )
    CompleteIfAllItemChecksFinished(
        "[disconnect cleanup] Character and clan item-check callbacks completed (reported by a character callback)."
    );
}

CItemCheckOnPledgeLoadedMessage::CItemCheckOnPledgeLoadedMessage(CPledge* pledge) noexcept : m_pledge(pledge) {}

void CItemCheckOnPledgeLoadedMessage::OnReceive(int argument [[maybe_unused]]) {
    L2_GUARD_FUNCTION();
    // if (m_pledge)
    //     m_pledge->ItemCheckOnUnload()
    if (m_pledge) {
        m_pledge->ItemCheckOnUnload();
    } else {
        // Log.Add(LOG_ERROR,
        //     "[disconnect cleanup] Cannot check clan items: the callback has no clan object, its pending count will still be decremented.")
        L2_LOG_ERROR(Log,
            "[disconnect cleanup] Cannot check clan items: the callback has no clan object; its pending count will still be decremented.");
    }
    g_nPendingPledgeItemChecks.fetch_sub(1, std::memory_order_seq_cst);
    // CompleteIfAllItemChecksFinished(
    //     "[NO_ERROR] All Pledges are checked. "
    //     "Now CacheD can be shut-down!!!"
    // )
    CompleteIfAllItemChecksFinished(
        "[disconnect cleanup] Character and clan item-check callbacks completed (reported by a clan callback)."
    );
}

CLogOutCharMessage::CLogOutCharMessage(CUser* user) noexcept : m_user(user) {}

void CLogOutCharMessage::OnReceive(int argument [[maybe_unused]]) {
    L2_GUARD_FUNCTION();

    if (m_user) {
        CWareHouseSP inventory = m_user->GetWareHouse(0, false);
        if (inventory && inventory->m_unsavedItemsPending) {
            {
                L2_WRITE_GUARD(*inventory.operator->());
                inventory->SaveUnsavedItems();
            }
        }
        if (inventory && inventory->m_lateSavingItemsPending) {
            {
                L2_WRITE_GUARD(*inventory.operator->());
                inventory->SaveLateSavingItems();
            }
        }

        if (m_user->IsLoggedIn()) {
            m_user->SetCheckSumOnDB();
            m_user->WriteLogout(static_cast<int>(m_user->GetId()), -1);
            m_user->SaveSlot();
        }
    }

    if (!m_user) {
        // Log.Add(LOG_ERROR,
        //     "[disconnect cleanup] Cannot process logout: the callback has no character object, its pending count will still be decremented.")
        L2_LOG_ERROR(Log,
            "[disconnect cleanup] Cannot process logout: the callback has no character object; its pending count will still be decremented.");
    }

    const std::int32_t previous = g_nPendingLogoutCharacters.fetch_sub(1, std::memory_order_seq_cst);
    if (previous == 1) {
        // Log.Add(LOG_NORMAL,
        //     "[disconnect cleanup] Last scheduled logout callback finished, starting item checks for loaded characters.")
        L2_LOG_INFO(Log,
            "[disconnect cleanup] Last scheduled logout callback finished; starting item checks for loaded characters.");
        g_userDb.ItemCheckOnAllCharLoaded();
    }
}
