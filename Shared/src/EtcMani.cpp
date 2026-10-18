#include "Shared/src/EtcMani.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/SourceFile.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"

#if defined(L2_BUILD_CACHED)
#include "CAccount.h"
#include "CacheSocket.h"
#include "Cached.h"
#include "Item.h"
#include "Pledge.h"
#include "PledgeDb.h"
#include "Shared/src/DBConn.h"
#include "Shared/src/LogFormatter.h"
#include "User.h"
#include "UserDb.h"
#include "WareHouse.h"
#endif

#include <cstdint>
#include <ctime>
#include <limits>
#include <string>

namespace ItemUtil {

void AssembleItemList(const std::vector<CreateItemStruct>& items, CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();
    for (const CreateItemStruct& item : items) {
        packet.Assemble("dQ", item.ItemID, item.Count);
    }
}

const unsigned char* DisassembleItemList(const unsigned char* packet, int count, std::vector<CreateItemStruct>& items) {
    L2_GUARD_FUNCTION();
    while (count-- > 0) {
        CreateItemStruct item;
        packet = ::Disassemble(packet, "dQ", &item.ItemID, &item.Count);
        items.push_back(item);
    }
    return packet;
}

} // namespace ItemUtil


#if defined(L2_BUILD_CACHED)
namespace {

::CSocket* ActiveL2ServerSocket() noexcept { return g_pActiveServerSocket.load(std::memory_order_acquire); }

void SendEtcManiSystemMessage(int userSid, short messageId) {
    ::CSocket* socket = ActiveL2ServerSocket();
    if (!socket) {
        return;
    }
    // Current Cached.exe helper 0x004ba43c.
    socket->Send("chdhc", 0xc6, 0x11f, userSid, static_cast<int>(messageId), 0);
}

void SendEventTimeMessage(int userDbId, int messageId, int parameterCount, CPacketHelperEx<20480>& parameters) {
    ::CSocket* socket = ActiveL2ServerSocket();
    if (!socket) {
        return;
    }
    socket->Send(
        "cdhcdb", 0xbf, userDbId, messageId, parameterCount, parameters.GetLength(), parameters.GetLength(),
        parameters.GetPacket()
    );
}

void SendCacheSystemMessage(int userDbId, short messageId) {
    ::CSocket* socket = ActiveL2ServerSocket();
    if (!socket) {
        return;
    }
    socket->Send("cdhc", 0xbf, userDbId, static_cast<int>(messageId), 0);
}

// Original source name is absent from the PDB. Current Cached.exe 0x004c2d3c
// receives an existing DBConn, the statement, the item id and eight shorts.
// each caller owns the request-routed connection.
bool ExecuteUserItemAttributeUpdate(
    DBConn& connection,
    const wchar_t* statement,
    int itemDbId,
    short attackType,
    short attackValue,
    short fireDefence,
    short waterDefence,
    short windDefence,
    short earthDefence,
    short holyDefence,
    short unholyDefence
) {
    connection.Prepare(statement);
    connection.BindParameter(&itemDbId);
    connection.BindParameter(&attackType);
    connection.BindParameter(&attackValue);
    connection.BindParameter(&fireDefence);
    connection.BindParameter(&waterDefence);
    connection.BindParameter(&windDefence);
    connection.BindParameter(&earthDefence);
    connection.BindParameter(&holyDefence);
    connection.BindParameter(&unholyDefence);
    return connection.ExecutePrepared(statement);
}


// The current PDB lost both private helper identifiers. These semantic names
// keep the two separately compiled EtcMani.cpp bodies instead of folding
// their diagnostics and CSPointer-by-value lifetime into ModifyUserPoint.
int LoadUserPointForModification(CUserSP user, UserPointType pointType) {
    // Current Cached.exe 0x004c0f4c.
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"..\\Shared\\EtcMani.cpp",
        //     0xe3e,
        //     L"(spUser.IsValid())")
        L2_LOG_ERROR(Log, u"[PLEDGE][%s][%d]assert falied: %s", __WFILE__, __LINE__, u"(spUser.IsValid())");
        return 0;
    }
    return pointType == PT_PCCAFE_POINT ? user->LoadPCCafePoint() : user->LoadUserPoint(static_cast<int>(pointType));
}

bool SaveUserPointForModification(CUserSP user, UserPointType pointType, int point) {
    // Current Cached.exe 0x004c1018.
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"..\\Shared\\EtcMani.cpp",
        //     0xe45,
        //     L"(spUser.IsValid())")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"..\\Shared\\EtcMani.cpp",
        //     0xe45,
        //     L"(spUser.IsValid())")
        // Previous: L2_LOG_ERROR(Log,
        //     "Cannot save character points for modification: character unavailable. Point type %d, requested value %d.",
        //     static_cast<int>(pointType), point)
        L2_LOG_ERROR(
            Log,
            "[character points] Cannot save a modification: the character is unavailable; point type %d, requested value "
            "%d.",
            static_cast<int>(pointType), point
        );
        return false;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][PLEDGE][%s][%d]Save User Point: %d, %d, %d",
    //     L"..\\Shared\\EtcMani.cpp",
    //     0xe47,
    //     static_cast<int>(user->GetId()),
    //     static_cast<int>(pointType),
    //     point)
    // Noise: unconditional trace before saving. It neither indicates an error
    // nor confirms persistence. Save methods keep their failure diagnostics.
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][PLEDGE][%s][%d]Save User Point: %d, %d, %d",
    //     L"..\\Shared\\EtcMani.cpp",
    //     0xe47,
    //     static_cast<int>(user->GetId()),
    //     static_cast<int>(pointType),
    //     point)
    return pointType == PT_PCCAFE_POINT ? user->SavePCCafePoint(point)
                                        : user->SaveUserPoint(static_cast<int>(pointType), point);
}

bool ModifyUserPoint(CUserSP user, UserPointType pointType, int delta) {
    // Current anonymous EtcMani.cpp helper 0x004c11b0.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"..\\Shared\\EtcMani.cpp",
        //     0xe53,
        //     L"(spUser.IsValid())")
        L2_LOG_ERROR(Log, u"[PLEDGE][%s][%d]assert falied: %s", __WFILE__, __LINE__, u"(spUser.IsValid())");
        return false;
    }

    const int current = LoadUserPointForModification(user, pointType);
    int maximum = std::numeric_limits<int>::max();
    if (pointType == PT_PCCAFE_POINT) {
        maximum = 200000;
    } else if (pointType == PT_PVP_POINT || pointType == PT_RAID_POINT) {
        maximum = 20000000;
    }

    int updated = runtime::AddInt32Wrapping(current, delta);
    if (updated >= maximum) {
        updated = maximum;
    }
    if (updated < 0) {
        return false;
    }
    return SaveUserPointForModification(user, pointType, updated);
}

} // namespace
#endif

EtcMani::EtcMani() = default;

int EtcMani::Assemble(IPacketHelper& packet) {
    // Current L2NPC.exe 0x00426fd0. The target asks the helper for its current
    // cursor and remaining capacity, calls the char-buffer overload, then
    // advances the helper by the returned byte count.
    L2_GUARD_FUNCTION();
    const int previousLength = packet.GetLength();
    const int assembled = Assemble(packet.GetPacket() + previousLength, packet.GetBufferSize() - previousLength);
    packet.SetLength(previousLength + assembled);
    return assembled;
}

int EtcMani::Assemble(char* buffer, int bufferSize) { return 0; }

const unsigned char* EtcMani::Disassemble(const unsigned char* packet) { return packet; }

void EtcMani::DumpError() {}

void EtcMani::Destroy() {
    // Shared/EtcMani.cpp:0x61 in the matching PDB. Current L2NPC.exe
    // 0x004270c4 follows the same virtual Release ownership path.
    L2_GUARD_FUNCTION();
    L2_RELEASE(this, ObjectRefType::CreateDelete);
}

bool EtcMani::DoAtCache() {
    m_isSuccess = DoAtCacheInternal();
    return m_isSuccess;
}

bool EtcMani::IsSuccess() { return m_isSuccess; }

EtcManiUpdatePledgeNameValue::EtcManiUpdatePledgeNameValue() = default;

EtcManiUpdatePledgeNameValue::EtcManiUpdatePledgeNameValue(int worldId, int userSid, unsigned int pledgeDbId, int delta) :
    m_worldId(worldId), m_userSid(userSid), m_pledgeDbId(pledgeDbId), m_delta(delta) {}

int EtcManiUpdatePledgeNameValue::Assemble(IPacketHelper& packet) {
    // Current L2NPC.exe 0x00427258.
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hdddd", ETC_MANI_UPDATE_PLEDGE_NAMEVALUE, m_worldId, m_userSid, static_cast<int>(m_pledgeDbId), m_delta
    );
}

int EtcManiUpdatePledgeNameValue::Assemble(char* buffer, int bufferSize) {
    // Current L2NPC.exe 0x0042717c.
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hdddd", ETC_MANI_UPDATE_PLEDGE_NAMEVALUE, m_worldId, m_userSid, static_cast<int>(m_pledgeDbId),
        m_delta
    );
}

const unsigned char* EtcManiUpdatePledgeNameValue::Disassemble(const unsigned char* packet) {
    // Current L2NPC.exe 0x00427324.
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "dddd", &m_worldId, &m_userSid, &m_pledgeDbId, &m_delta);
}

bool EtcManiUpdatePledgeNameValue::DoAtCacheInternal() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004bf5b8. CacheD uses its active world context,
    // not the serialized world field, for the pledge lookup.
    CPledgeSP pledge = g_pledgeDb.GetPledge(static_cast<int>(m_pledgeDbId), false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        return false;
    }
    if (m_delta < 0 && !pledge->CanApplyNameValueDelta(m_delta)) {
        SendEtcManiSystemMessage(m_userSid, 0x917);
        return false;
    }
    const int appliedDelta =
        m_delta > 0 ? static_cast<int>(GetCacheDConfiguration().pledgeNameValueRate) * m_delta : m_delta;
    return pledge->UpdateNameValue(appliedDelta, 0);
#else
    // Current L2NPC.exe 0x004273e8.
    return false;
#endif
}

bool EtcManiUpdatePledgeNameValue::Rollback() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004bf7d0 applies the exact inverse of the delta
    // selected by DoAtCacheInternal.
    CPledgeSP pledge = g_pledgeDb.GetPledge(static_cast<int>(m_pledgeDbId), false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        return false;
    }
    const int appliedDelta =
        m_delta > 0 ? static_cast<int>(GetCacheDConfiguration().pledgeNameValueRate) * m_delta : m_delta;
    return pledge->UpdateNameValue(-appliedDelta, 0);
#else
    // Current L2NPC.exe 0x00427468.
    return false;
#endif
}

bool EtcManiUpdatePledgeNameValue::DoAtServer() {
    // Current L2NPC.exe 0x004274e8.
    L2_GUARD_FUNCTION();
    return false;
}

void EtcManiUpdatePledgeNameValue::DumpError() {
    // Current L2NPC.exe 0x00427568.
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_LOCAL,
    //     L"EtcManiUpdatePledgeNameValue::DumpError() => "
    //     L"PledgeID=[%d], NameValueDelta=[%d]",
    //     static_cast<int>(m_pledgeDbId),
    //     m_delta)
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"EtcManiUpdatePledgeNameValue::DumpError() => "
    //     L"PledgeID=[%d], NameValueDelta=[%d]",
    //     static_cast<int>(m_pledgeDbId),
    //     m_delta)
    L2_LOG(
        Log, LOG_LOCAL, u"[clan name] Modification failed for clan %d with value delta %d.", static_cast<int>(m_pledgeDbId),
        m_delta
    );
}

EtcManiUpdateUserEventTime::EtcManiUpdateUserEventTime() = default;

int EtcManiUpdateUserEventTime::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hddddddQdQdd", ETC_MANI_UPDATE_USER_EVENT_TIME, m_userDBID, m_timeGap, m_eventTime, m_eventType,
        m_eventVariable, m_nDelItemClassID, static_cast<long long>(m_nDelItemAmount), m_nAddItemClassID,
        static_cast<long long>(m_nAddItemAmount), m_nAddItemEnchant, m_nRet
    );
}

int EtcManiUpdateUserEventTime::Assemble(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hddddddQdQdd", ETC_MANI_UPDATE_USER_EVENT_TIME, m_userDBID, m_timeGap, m_eventTime, m_eventType, m_eventVariable,
        m_nDelItemClassID, static_cast<long long>(m_nDelItemAmount), m_nAddItemClassID,
        static_cast<long long>(m_nAddItemAmount), m_nAddItemEnchant, m_nRet
    );
}

const unsigned char* EtcManiUpdateUserEventTime::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(
        packet, "ddddddQdQdd", &m_userDBID, &m_timeGap, &m_eventTime, &m_eventType, &m_eventVariable, &m_nDelItemClassID,
        &m_nDelItemAmount, &m_nAddItemClassID, &m_nAddItemAmount, &m_nAddItemEnchant, &m_nRet
    );
}

bool EtcManiUpdateUserEventTime::DoAtCacheInternal() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004bf91c.
    CUserSP user = g_userDb.GetUser(m_userDBID, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        m_nRet = 0;
        return false;
    }
    CAccountSP account = CAccount::Load(static_cast<unsigned int>(user->GetAccountID()));
    if (!account) {
        m_nRet = 0;
        return false;
    }

    const int previousEventTime = account->GetEventTime(m_eventType, m_eventVariable);
    const int cooldownSeconds = m_timeGap * 0xe10;
    if (m_eventTime - previousEventTime < cooldownSeconds) {
        ::CSocket* socket = ActiveL2ServerSocket();
        if (m_eventType == 0xc) {
            if (socket) {
                socket->Send("cdhc", 0xbf, m_userDBID, 0xcd9, 0);
            }
        } else {
            const int remainingSeconds = previousEventTime + cooldownSeconds - m_eventTime;
            const int hours = remainingSeconds / 0xe10;
            const int minutes = (remainingSeconds % 0xe10) / 0x3c;
            CPacketHelperEx<20480> parameters;
            if (hours == 0) {
                parameters.Assemble("cc", 1, minutes);
                SendEventTimeMessage(m_userDBID, 0x7f3, 1, parameters);
            } else {
                parameters.Assemble("chcc", 0x15, hours, 0x14, minutes);
                SendEventTimeMessage(m_userDBID, 0x7f2, 2, parameters);
            }
        }
        m_nRet = 0;
        return false;
    }

    m_oldEventTime = previousEventTime;
    m_nRet = 1;
    account->SetEventTime(m_eventTime, m_eventType, m_eventVariable, true);
    return true;
#else
    return false;
#endif
}

bool EtcManiUpdateUserEventTime::Rollback() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004bfce4 writes m_eventTime again. It does not use
    // the captured m_oldEventTime despite the latter's source-level name.
    CUserSP user = g_userDb.GetUser(m_userDBID, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        m_nRet = 0;
        return false;
    }
    CAccountSP account = CAccount::Load(static_cast<unsigned int>(user->GetAccountID()));
    if (!account) {
        m_nRet = 0;
        return false;
    }
    account->SetEventTime(m_eventTime, m_eventType, m_eventVariable, true);
    return true;
#else
    return false;
#endif
}

bool EtcManiUpdateUserEventTime::DoAtServer() {
    // Current Cached.exe 0x004bb2dc is a guard-only false stub.
    L2_GUARD_FUNCTION();
    return false;
}

void EtcManiUpdateUserEventTime::DumpError() {
    // Current Cached.exe 0x004bb35c is a guard-only void stub.
    L2_GUARD_FUNCTION();
}

EtcManiUpdateUserItemBaseAttribute::EtcManiUpdateUserItemBaseAttribute() = default;

int EtcManiUpdateUserItemBaseAttribute::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hdddddhhhhhhhh", ETC_MANI_UPDATE_USER_ITEM_BASE_ATTRIBUTE, static_cast<int>(m_nUserSid),
        m_nUserDBID, m_nTargetItemDBID, m_nIntensiveItemClassId, m_nEnchantType, static_cast<int>(m_Attribute.m_nAttackType),
        static_cast<int>(m_Attribute.m_nAttackValue), static_cast<int>(m_Attribute.m_nDefendValue[0]),
        static_cast<int>(m_Attribute.m_nDefendValue[1]), static_cast<int>(m_Attribute.m_nDefendValue[2]),
        static_cast<int>(m_Attribute.m_nDefendValue[3]), static_cast<int>(m_Attribute.m_nDefendValue[4]),
        static_cast<int>(m_Attribute.m_nDefendValue[5])
    );
}

int EtcManiUpdateUserItemBaseAttribute::Assemble(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hdddddhhhhhhhh", ETC_MANI_UPDATE_USER_ITEM_BASE_ATTRIBUTE, static_cast<int>(m_nUserSid), m_nUserDBID,
        m_nTargetItemDBID, m_nIntensiveItemClassId, m_nEnchantType, static_cast<int>(m_Attribute.m_nAttackType),
        static_cast<int>(m_Attribute.m_nAttackValue), static_cast<int>(m_Attribute.m_nDefendValue[0]),
        static_cast<int>(m_Attribute.m_nDefendValue[1]), static_cast<int>(m_Attribute.m_nDefendValue[2]),
        static_cast<int>(m_Attribute.m_nDefendValue[3]), static_cast<int>(m_Attribute.m_nDefendValue[4]),
        static_cast<int>(m_Attribute.m_nDefendValue[5])
    );
}

const unsigned char* EtcManiUpdateUserItemBaseAttribute::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(
        packet, "dddddhhhhhhhh", &m_nUserSid, &m_nUserDBID, &m_nTargetItemDBID, &m_nIntensiveItemClassId, &m_nEnchantType,
        &m_Attribute.m_nAttackType, &m_Attribute.m_nAttackValue, &m_Attribute.m_nDefendValue[0],
        &m_Attribute.m_nDefendValue[1], &m_Attribute.m_nDefendValue[2], &m_Attribute.m_nDefendValue[3],
        &m_Attribute.m_nDefendValue[4], &m_Attribute.m_nDefendValue[5]
    );
}

bool EtcManiUpdateUserItemBaseAttribute::DoAtCacheInternal() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c36e8.
    CUserSP user = g_userDb.GetUser(m_nUserDBID, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        return false;
    }
    CWareHouseSP warehouse = user->GetWareHouse(0, true);
    if (!warehouse) {
        return false;
    }
    CItemSP item = warehouse->GetItem(m_nTargetItemDBID);
    if (!item) {
        return false;
    }

    m_oldAttribute = item->Attribute();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateUserItemAttribute (?, ?, ?, ?, ?, ?, ?, ?, ?)}";
    if (!ExecuteUserItemAttributeUpdate(
            connection, statement, m_nTargetItemDBID, m_Attribute.m_nAttackType, m_Attribute.m_nAttackValue,
            m_Attribute.m_nDefendValue[0], m_Attribute.m_nDefendValue[1], m_Attribute.m_nDefendValue[2],
            m_Attribute.m_nDefendValue[3], m_Attribute.m_nDefendValue[4], m_Attribute.m_nDefendValue[5]
        )) {
        return false;
    }
    item->SetBaseAttribute(m_Attribute);
    return true;
#else
    return false;
#endif
}

bool EtcManiUpdateUserItemBaseAttribute::Rollback() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c3ad8.
    CUserSP user = g_userDb.GetUser(m_nUserDBID, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        return false;
    }
    CWareHouseSP warehouse = user->GetWareHouse(0, true);
    if (!warehouse) {
        return false;
    }
    CItemSP item = warehouse->GetItem(m_nTargetItemDBID);
    if (!item) {
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateUserItemAttribute (?, ?, ?, ?, ?, ?, ?, ?, ?)}";
    if (!ExecuteUserItemAttributeUpdate(
            connection, statement, m_nTargetItemDBID, m_oldAttribute.m_nAttackType, m_oldAttribute.m_nAttackValue,
            m_oldAttribute.m_nDefendValue[0], m_oldAttribute.m_nDefendValue[1], m_oldAttribute.m_nDefendValue[2],
            m_oldAttribute.m_nDefendValue[3], m_oldAttribute.m_nDefendValue[4], m_oldAttribute.m_nDefendValue[5]
        )) {
        return false;
    }
    item->SetBaseAttribute(m_oldAttribute);
    return true;
#else
    return false;
#endif
}

bool EtcManiUpdateUserItemBaseAttribute::DoAtServer() {
    // Current Cached.exe 0x004bbb04 is a guard-only false stub.
    L2_GUARD_FUNCTION();
    return false;
}

void EtcManiUpdateUserItemBaseAttribute::DumpError() {
    // Current Cached.exe 0x004bbb84 is a guard-only void stub.
    L2_GUARD_FUNCTION();
}

EtcManiRollbackUserItemBaseAttribute::EtcManiRollbackUserItemBaseAttribute() = default;

int EtcManiRollbackUserItemBaseAttribute::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hddddd", ETC_MANI_ROLLBACK_USER_ITEM_BASE_ATTRIBUTE, static_cast<int>(m_nUserSid), m_nUserDBID,
        m_nTargetItemDBID, m_nRemoveAttribute, m_nResult
    );
}

int EtcManiRollbackUserItemBaseAttribute::Assemble(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hddddd", ETC_MANI_ROLLBACK_USER_ITEM_BASE_ATTRIBUTE, static_cast<int>(m_nUserSid), m_nUserDBID, m_nTargetItemDBID,
        m_nRemoveAttribute, m_nResult
    );
}

const unsigned char* EtcManiRollbackUserItemBaseAttribute::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "ddddd", &m_nUserSid, &m_nUserDBID, &m_nTargetItemDBID, &m_nRemoveAttribute, &m_nResult);
}

bool EtcManiRollbackUserItemBaseAttribute::DoAtCacheInternal() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c3e90.
    CUserSP user = g_userDb.GetUser(m_nUserDBID, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        return false;
    }
    CWareHouseSP warehouse = user->GetWareHouse(0, true);
    if (!warehouse) {
        return false;
    }
    CItemSP item = warehouse->GetItem(m_nTargetItemDBID);
    if (!item) {
        return false;
    }

    BaseAttribute changed(item->Attribute());
    changed.Remove(static_cast<AttributeTypeEnum>(m_nRemoveAttribute));
    m_oldAttribute = item->Attribute();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateUserItemAttribute (?, ?, ?, ?, ?, ?, ?, ?, ?)}";
    if (!ExecuteUserItemAttributeUpdate(
            connection, statement, m_nTargetItemDBID, changed.m_nAttackType, changed.m_nAttackValue,
            changed.m_nDefendValue[0], changed.m_nDefendValue[1], changed.m_nDefendValue[2], changed.m_nDefendValue[3],
            changed.m_nDefendValue[4], changed.m_nDefendValue[5]
        )) {
        return false;
    }
    item->SetBaseAttribute(changed);
    m_nResult = 1;
    return true;
#else
    return false;
#endif
}

bool EtcManiRollbackUserItemBaseAttribute::Rollback() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c42d0.
    CUserSP user = g_userDb.GetUser(m_nUserDBID, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        return false;
    }
    CWareHouseSP warehouse = user->GetWareHouse(0, true);
    if (!warehouse) {
        return false;
    }
    CItemSP item = warehouse->GetItem(m_nTargetItemDBID);
    if (!item) {
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateUserItemAttribute (?, ?, ?, ?, ?, ?, ?, ?, ?)}";
    if (!ExecuteUserItemAttributeUpdate(
            connection, statement, m_nTargetItemDBID, m_oldAttribute.m_nAttackType, m_oldAttribute.m_nAttackValue,
            m_oldAttribute.m_nDefendValue[0], m_oldAttribute.m_nDefendValue[1], m_oldAttribute.m_nDefendValue[2],
            m_oldAttribute.m_nDefendValue[3], m_oldAttribute.m_nDefendValue[4], m_oldAttribute.m_nDefendValue[5]
        )) {
        return false;
    }
    item->SetBaseAttribute(m_oldAttribute);
    m_nResult = 0;
    return true;
#else
    return false;
#endif
}

bool EtcManiRollbackUserItemBaseAttribute::DoAtServer() {
    // Current Cached.exe 0x004bbe8c is a guard-only false stub.
    L2_GUARD_FUNCTION();
    return false;
}

void EtcManiRollbackUserItemBaseAttribute::DumpError() {
    // Current Cached.exe 0x004bbf0c is a guard-only void stub.
    L2_GUARD_FUNCTION();
}

EtcManiCreateItemAuctionBid::EtcManiCreateItemAuctionBid() = default;

int EtcManiCreateItemAuctionBid::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hddddQd", ETC_MANI_CREATE_ITEM_AUCTION_BID, static_cast<int>(m_nUserSid),
        static_cast<int>(m_nUserDBID), m_nAuctionID, m_nBidID, static_cast<long long>(m_nBiddingPrice), m_nTransactionID
    );
}

int EtcManiCreateItemAuctionBid::Assemble(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hddddQd", ETC_MANI_CREATE_ITEM_AUCTION_BID, static_cast<int>(m_nUserSid), static_cast<int>(m_nUserDBID),
        m_nAuctionID, m_nBidID, static_cast<long long>(m_nBiddingPrice), m_nTransactionID
    );
}

const unsigned char* EtcManiCreateItemAuctionBid::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(
        packet, "ddddQd", &m_nUserSid, &m_nUserDBID, &m_nAuctionID, &m_nBidID, &m_nBiddingPrice, &m_nTransactionID
    );
}

bool EtcManiCreateItemAuctionBid::DoAtCacheInternal() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c468c.
    int transactionDisorder = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&m_nBidID);
    connection.Bind(&transactionDisorder);
    constexpr const wchar_t* statement = L"{Call lin_CreateItemAuctionBid (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nAuctionID);
    connection.BindParameter(&m_nUserDBID);
    connection.BindParameter(&m_nBiddingPrice);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        if (transactionDisorder == 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] AuctionBid db tran disordered. "
            //     L"user[%d] Auction[%d] bid[%d] tran[%d]",
            //     L"..\\Shared\\EtcMani.cpp",
            //     0x595,
            //     static_cast<int>(m_nUserDBID),
            //     m_nAuctionID,
            //     m_nBidID,
            //     m_nTransactionID)
            L2_LOG_ERROR(
                Log,
                u"[%s][%d] AuctionBid db tran disordered. "
                u"user[%d] Auction[%d] bid[%d] tran[%d]",
                __WFILE__, __LINE__, static_cast<int>(m_nUserDBID), m_nAuctionID, m_nBidID, m_nTransactionID
            );
        }
        if (m_nBidID > 0) {
            return true;
        }
    }

    SendCacheSystemMessage(static_cast<int>(m_nUserDBID), 0x81f);
    return false;
#else
    return false;
#endif
}

bool EtcManiCreateItemAuctionBid::Rollback() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c4b28.
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeleteItemAuctionBid (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nBidID);
    return connection.ExecutePrepared(statement);
#else
    return false;
#endif
}

bool EtcManiCreateItemAuctionBid::DoAtServer() {
    // Current Cached.exe 0x004bc230 is a guard-only false stub.
    L2_GUARD_FUNCTION();
    return false;
}

void EtcManiCreateItemAuctionBid::DumpError() {
    // Current Cached.exe 0x004bc2b0.
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"EtcManiCreateItemAuctionBid::DumpError() => "
    //     L"user=[%d], auction id=[%d], bid id=[%d], price=[%lld]",
    //     static_cast<int>(m_nUserDBID),
    //     m_nAuctionID,
    //     m_nBidID,
    //     static_cast<long long>(m_nBiddingPrice))
    L2_LOG_ERROR(
        Log, "[item auction] Bid-creation diagnostic: character %u, auction %d, bid %d, price %lld.", m_nUserDBID,
        m_nAuctionID, m_nBidID, static_cast<long long>(m_nBiddingPrice)
    );
}

EtcManiWithdrawItemAuctionBid::EtcManiWithdrawItemAuctionBid() = default;

int EtcManiWithdrawItemAuctionBid::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hdddQd", ETC_MANI_WITHDRAW_ITEM_AUCTION_BID, m_nBidID, static_cast<int>(m_nUserDBID),
        m_nAuctionID, static_cast<long long>(m_nAdenaAmount), m_bLog ? 1 : 0
    );
}

int EtcManiWithdrawItemAuctionBid::Assemble(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hdddQd", ETC_MANI_WITHDRAW_ITEM_AUCTION_BID, m_nBidID, static_cast<int>(m_nUserDBID), m_nAuctionID,
        static_cast<long long>(m_nAdenaAmount), m_bLog ? 1 : 0
    );
}

const unsigned char* EtcManiWithdrawItemAuctionBid::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "dddQd", &m_nBidID, &m_nUserDBID, &m_nAuctionID, &m_nAdenaAmount, &m_bLog);
}

bool EtcManiWithdrawItemAuctionBid::DoAtCacheInternal() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c4c58.
    std::int64_t withdrawnAmount = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&m_nBidID);
    connection.Bind(&withdrawnAmount);
    const int inputBidId = 0;
    const int withdrawFlag = 1;
    constexpr const wchar_t* statement = L"{Call lin_WithdrawItemAuctionBid (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&inputBidId);
    connection.BindParameter(&m_nAuctionID);
    connection.BindParameter(&m_nUserDBID);
    connection.BindParameter(&withdrawFlag);
    if (connection.ExecutePrepared(statement) && connection.Fetch() && withdrawnAmount == m_nAdenaAmount && m_nBidID > 0) {
        return true;
    }

    SendCacheSystemMessage(static_cast<int>(m_nUserDBID), 0x821);
    return false;
#else
    return false;
#endif
}

bool EtcManiWithdrawItemAuctionBid::Rollback() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c507c binds both result columns and lets the
    // rollback query overwrite m_nAdenaAmount.
    int returnedBidId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&returnedBidId);
    connection.Bind(&m_nAdenaAmount);
    const int withdrawFlag = 0;
    constexpr const wchar_t* statement = L"{Call lin_WithdrawItemAuctionBid (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nBidID);
    connection.BindParameter(&m_nAuctionID);
    connection.BindParameter(&m_nUserDBID);
    connection.BindParameter(&withdrawFlag);
    return connection.ExecutePrepared(statement) && connection.Fetch();
#else
    return false;
#endif
}

bool EtcManiWithdrawItemAuctionBid::DoAtServer() {
    // Current Cached.exe 0x004bc614 is a guard-only false stub.
    L2_GUARD_FUNCTION();
    return false;
}

void EtcManiWithdrawItemAuctionBid::DumpError() {
    // Current Cached.exe 0x004bc694 is a guard-only void stub.
    L2_GUARD_FUNCTION();
}

EtcManiGiveItemByCastleSiegeCount::EtcManiGiveItemByCastleSiegeCount() = default;

int EtcManiGiveItemByCastleSiegeCount::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hddddd", ETC_MANI_GIVE_ITEM_BY_CASTLE_SIEGE_COUNT, static_cast<int>(m_UserSid), m_UserDBID,
        m_PledgeDBID, m_CastleSiegeCount, m_NpcId
    );
}

int EtcManiGiveItemByCastleSiegeCount::Assemble(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hddddd", ETC_MANI_GIVE_ITEM_BY_CASTLE_SIEGE_COUNT, static_cast<int>(m_UserSid), m_UserDBID, m_PledgeDBID,
        m_CastleSiegeCount, m_NpcId
    );
}

const unsigned char* EtcManiGiveItemByCastleSiegeCount::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "ddddd", &m_UserSid, &m_UserDBID, &m_PledgeDBID, &m_CastleSiegeCount, &m_NpcId);
}

bool EtcManiGiveItemByCastleSiegeCount::DoAtCacheInternal() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c00fc.
    CPledgeSP pledge = g_pledgeDb.GetPledge(m_PledgeDBID, false, runtime::process::CurrentDatabaseId());
    return pledge && pledge->SetCastleSiegeDefenceCount(m_CastleSiegeCount);
#else
    return false;
#endif
}

bool EtcManiGiveItemByCastleSiegeCount::Rollback() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c01fc is identical to apply.
    CPledgeSP pledge = g_pledgeDb.GetPledge(m_PledgeDBID, false, runtime::process::CurrentDatabaseId());
    return pledge && pledge->SetCastleSiegeDefenceCount(m_CastleSiegeCount);
#else
    return false;
#endif
}

bool EtcManiGiveItemByCastleSiegeCount::DoAtServer() {
    // Current Cached.exe 0x004bc99c is a guard-only false stub.
    L2_GUARD_FUNCTION();
    return false;
}

void EtcManiGiveItemByCastleSiegeCount::DumpError() {
    // Current Cached.exe 0x004bca1c is a guard-only void stub.
    L2_GUARD_FUNCTION();
}

EtcManiGiveItemOnAccount::EtcManiGiveItemOnAccount() = default;

EtcManiGiveItemOnAccount::EtcManiGiveItemOnAccount(
    int userDbId,
    int condition,
    int deleteItemClassId,
    std::int64_t deleteItemAmount,
    int addItemCount,
    const int* addItemClassIds,
    const std::int64_t* addItemAmounts,
    const int* addItemEnchants,
    int type,
    unsigned int accountId
) :
    m_userDBID(userDbId),
    m_nCond(condition),
    m_nAccountID(accountId),
    m_nDelItemClassID(deleteItemClassId),
    m_nDelItemAmount(deleteItemAmount),
    m_nAddItemCount(addItemCount),
    m_nType(type) {
    for (int index = 0; index < addItemCount; ++index) {
        const std::size_t offset = static_cast<std::size_t>(index);
        m_AddItemClassID[offset] = addItemClassIds != nullptr ? addItemClassIds[index] : 0;
        m_AddItemAmount[offset] = addItemAmounts != nullptr ? addItemAmounts[index] : 0;
        m_AddItemEnchant[offset] = addItemEnchants != nullptr ? addItemEnchants[index] : 0;
    }
}

int EtcManiGiveItemOnAccount::Assemble(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    int result = packet.Assemble("hdd", ETC_MANI_GIVE_ITEM_ON_ACCOUNT, m_userDBID, m_nCond);
    result = packet.Assemble("dQ", m_nDelItemClassID, static_cast<long long>(m_nDelItemAmount));
    result = packet.Assemble("d", m_nAddItemCount);
    for (int index = 0; index < m_nAddItemCount; ++index) {
        result = packet.Assemble(
            "dQd", m_AddItemClassID[static_cast<std::size_t>(index)],
            static_cast<long long>(m_AddItemAmount[static_cast<std::size_t>(index)]),
            m_AddItemEnchant[static_cast<std::size_t>(index)]
        );
    }
    result = packet.Assemble("d", static_cast<int>(m_nAccountID));
    result = packet.Assemble("dd", m_nRet, m_nType);
    result = packet.Assemble("dSd", m_nNpcId, m_sEventName.data(), m_nEventStartTime);
    return result;
}

int EtcManiGiveItemOnAccount::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    CPacketHelperEx<20480> payload;
    payload.Assemble("dd", m_userDBID, m_nCond);
    payload.Assemble("dQ", m_nDelItemClassID, static_cast<long long>(m_nDelItemAmount));
    payload.Assemble("d", m_nAddItemCount);
    for (int index = 0; index < m_nAddItemCount; ++index) {
        payload.Assemble(
            "dQd", m_AddItemClassID[static_cast<std::size_t>(index)],
            static_cast<long long>(m_AddItemAmount[static_cast<std::size_t>(index)]),
            m_AddItemEnchant[static_cast<std::size_t>(index)]
        );
    }
    payload.Assemble("d", static_cast<int>(m_nAccountID));
    payload.Assemble("dd", m_nRet, m_nType);
    payload.Assemble("dSd", m_nNpcId, m_sEventName.data(), m_nEventStartTime);
    return ::Assemble(buffer, bufferSize, "hb", ETC_MANI_GIVE_ITEM_ON_ACCOUNT, payload.GetLength(), payload.GetPacket());
}

const unsigned char* EtcManiGiveItemOnAccount::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    packet = ::Disassemble(packet, "dddQd", &m_userDBID, &m_nCond, &m_nDelItemClassID, &m_nDelItemAmount, &m_nAddItemCount);
    for (int index = 0; index < m_nAddItemCount; ++index) {
        packet = ::Disassemble(
            packet, "dQd", &m_AddItemClassID[static_cast<std::size_t>(index)],
            &m_AddItemAmount[static_cast<std::size_t>(index)], &m_AddItemEnchant[static_cast<std::size_t>(index)]
        );
    }
    packet = ::Disassemble(packet, "d", &m_nAccountID);
    packet = ::Disassemble(packet, "dd", &m_nRet, &m_nType);
    return ::Disassemble(
        packet, "dSd", &m_nNpcId, static_cast<int>(m_sEventName.size() * sizeof(char16_t)), m_sEventName.data(),
        &m_nEventStartTime
    );
}

bool EtcManiGiveItemOnAccount::DoAtServer() {
    // Current Cached.exe 0x004bce24 is a guard-only false stub.
    L2_GUARD_FUNCTION();
    return false;
}

void EtcManiGiveItemOnAccount::DumpError() {
    // Current Cached.exe 0x004bcea4 is a guard-only void stub.
    L2_GUARD_FUNCTION();
}

bool EtcManiGiveItemOnAccount::Rollback() {
    L2_GUARD_FUNCTION();
    // Current Cached.exe 0x004bcda4 is an unconditional false stub.
    return false;
}

bool EtcManiGiveItemOnAccount::ProcessByAI() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c2dec. This path records one daily stamp for
    // every requested item class. The actual item transaction is owned by
    // CWareHouse::ManipulateItems.
    CUserSP user = g_userDb.GetUser(m_userDBID, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        return false;
    }

    CAccountSP account = CAccount::Load(static_cast<unsigned int>(user->GetAccountID()));
    if (!account) {
        return false;
    }

    bool result = true;
    for (int index = 0; index < m_nAddItemCount; ++index) {
        const int itemClassId = m_AddItemClassID[static_cast<std::size_t>(index)];
        std::map<int, int> stamps;
        if (!account->LoadUserStamp(100, itemClassId, stamps)) {
            // Log.Add(
            //     LOG_ERROR,
            //     "FILE[%s] LINE[%d] load stamp value error",
            //     "..\\Shared\\EtcMani.cpp",
            //     0x872)
            L2_LOG_ERROR(
                Log,
                "[account rewards] Cannot load daily stamp for character %d, item class %d (entry %d of %d). "
                "Skipping this entry; later entries continue.",
                m_userDBID, itemClassId, index + 1, m_nAddItemCount
            );
            result = false;
            continue;
        }

        int previousStamp = 0;
        const auto found = stamps.find(itemClassId);
        if (found != stamps.end()) {
            previousStamp = found->second;
        }

        const std::int32_t now = runtime::crt::Time32Now();
        std::tm local{};
        if (!runtime::crt::LocalTime32(now, local)) {
            result = false;
            continue;
        }
        local.tm_hour = 0;
        local.tm_min = 0;
        local.tm_sec = 0;
        std::int32_t eligibilityStart = runtime::AddInt32Wrapping(runtime::crt::Mktime32(&local), m_nCond);
        if (now < eligibilityStart) {
            eligibilityStart = runtime::AddInt32Wrapping(eligibilityStart, -86400);
        }
        if (eligibilityStart <= previousStamp) {
            SendCacheSystemMessage(m_userDBID, static_cast<short>(0xcd9));
            result = false;
            break;
        }

        if (!account->SaveUserStamp(100, itemClassId, now, true)) {
            // Log.Add(
            //     LOG_ERROR,
            //     "FILE[%s] LINE[%d] save stamp error",
            //     "..\\Shared\\EtcMani.cpp",
            //     0x865)
            L2_LOG_ERROR(
                Log,
                "[account rewards] Cannot save daily stamp %d for character %d, item class %d (entry %d of %d). "
                "Skipping this entry; later entries continue.",
                now, m_userDBID, itemClassId, index + 1, m_nAddItemCount
            );
            result = false;
            continue;
        }
        m_nRet = 1;
        result = true;
    }
    return result;
#else
    return false;
#endif
}

bool EtcManiGiveItemOnAccount::ProcessAttendanceEvent() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c51c4.
    CUserSP user = g_userDb.GetUser(m_userDBID, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        return false;
    }

    CAccountSP account = CAccount::Load(static_cast<unsigned int>(user->GetAccountID()));
    if (!account) {
        return false;
    }

    std::map<int, int> stamps;
    account->LoadUserStamp(m_nType, stamps);
    if (stamps.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     "FILE[%s] LINE[%d] 10th event error!",
        //     "..\\Shared\\EtcMani.cpp",
        //     0x896)
        L2_LOG_ERROR(
            Log,
            "[account rewards] Cannot process attendance reward for character %d, stamp type %d: "
            "no stamp data is available (no records or load failure). Reward processing stopped.",
            m_userDBID, m_nType
        );
        return false;
    }

    const auto first = stamps.begin();
    if (first->first < m_nCond || first->second < m_nEventStartTime) {
        return false;
    }

    int loggedRecord = 0;
    std::array<char16_t, 256> loggedText{};
    int loggedStamp = 0;
    DBConn loadLog(runtime::process::CurrentDatabaseId());
    loadLog.Bind(&loggedRecord);
    loadLog.Bind(loggedText.data(), 0x100);
    loadLog.Bind(&loggedStamp);
    constexpr const wchar_t* loadQuery = L"{Call lin_LoadAccountUserStampLog(?,?)}";
    loadLog.Prepare(loadQuery);
    const int accountId = account->GetId();
    loadLog.BindParameter(&accountId);
    loadLog.BindParameter(&m_nType);
    bool unusedRecord = true;
    if (loadLog.ExecutePrepared(loadQuery) && loadLog.Fetch()) {
        do {
            if (loggedRecord == m_nCond) {
                unusedRecord = false;
            }
        } while (loadLog.Fetch());
        if (!unusedRecord) {
            return false;
        }
    }

    const int characterId = static_cast<int>(user->GetId());
    const std::int32_t now = runtime::crt::Time32Now();
    long nowAsDouble = static_cast<long>(now);
    DBConn saveLog(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* saveQuery = L"{Call lin_SaveAccountUserStampLog(?,?,?,?,?,?)}";
    saveLog.Prepare(saveQuery);
    saveLog.BindParameter(&accountId);
    saveLog.BindParameter(&characterId);
    saveLog.BindParameter(&m_nType);
    saveLog.BindParameter(&m_nCond);
    saveLog.BindParameter(L"");
    saveLog.BindParameter(&nowAsDouble);
    if (!saveLog.ExecutePrepared(saveQuery)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]Save Error!",
        //     "..\\Shared\\EtcMani.cpp",
        //     0x8b7)
        L2_LOG_ERROR(
            Log,
            "[account rewards] Cannot save attendance reward history for account %d, character %d, stamp type %d, condition "
            "%d: "
            "database execution failed. Returning failure.",
            accountId, characterId, m_nType, m_nCond
        );
        return false;
    }

    m_nRet = 1;
    return true;
#else
    return false;
#endif
}

bool EtcManiGiveItemOnAccount::DoAtCacheInternal() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c7404.
    switch (m_nType) {
        case 100:
            return ProcessByAI();
        case 0:
            return ProcessAttendanceEvent();
        case 2:
            m_nRet = 1;
            return true;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     "FILE[%s] LINE[%d] unknow type",
            //     "..\\Shared\\EtcMani.cpp",
            //     0x7bd)
            L2_LOG_ERROR(
                Log,
                "[account rewards] Unsupported reward type %d for character %d; supported types are 0, 2 and 100. Returning "
                "failure.",
                m_nType, m_userDBID
            );
            m_nRet = 0;
            return false;
    }
#else
    return false;
#endif
}

EtcManiUpdateUserItemCombination::EtcManiUpdateUserItemCombination() = default;

int EtcManiUpdateUserItemCombination::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hddcddd", ETC_MANI_UPDATE_USER_ITEM_COMBINATION, static_cast<int>(m_nUserSid), m_nUserDBID,
        m_isSuccess ? 1 : 0, m_nItemID, m_nDel1ItemID, m_nDel2ItemID
    );
}

int EtcManiUpdateUserItemCombination::Assemble(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hddcddd", ETC_MANI_UPDATE_USER_ITEM_COMBINATION, static_cast<int>(m_nUserSid), m_nUserDBID, m_isSuccess ? 1 : 0,
        m_nItemID, m_nDel1ItemID, m_nDel2ItemID
    );
}

const unsigned char* EtcManiUpdateUserItemCombination::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(
        packet, "ddcddd", &m_nUserSid, &m_nUserDBID, &m_isSuccess, &m_nItemID, &m_nDel1ItemID, &m_nDel2ItemID
    );
}

bool EtcManiUpdateUserItemCombination::DoAtCacheInternal() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004bfe74 only verifies/creates the user.
    return static_cast<bool>(g_userDb.GetUser(m_nUserDBID, true, false, runtime::process::CurrentDatabaseId()));
#else
    return false;
#endif
}

bool EtcManiUpdateUserItemCombination::Rollback() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004bffb8 has the same lookup-only body.
    return static_cast<bool>(g_userDb.GetUser(m_nUserDBID, true, false, runtime::process::CurrentDatabaseId()));
#else
    return false;
#endif
}

bool EtcManiUpdateUserItemCombination::DoAtServer() {
    // Current Cached.exe 0x004bb67c is a guard-only false stub.
    L2_GUARD_FUNCTION();
    return false;
}

void EtcManiUpdateUserItemCombination::DumpError() {
    // Current Cached.exe 0x004bb6fc is a guard-only void stub.
    L2_GUARD_FUNCTION();
}

EtcManiEnsoul::EtcManiEnsoul() = default;

int EtcManiEnsoul::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    int optionCount = 0;
    for (int slotType = 1; slotType < 3; ++slotType) {
        optionCount += static_cast<int>(m_mEnsoulInfo[static_cast<std::size_t>(slotType)].size());
    }

    int assembled =
        ::Assemble(buffer, bufferSize, "hdddd", ETC_MANI_ENSOUL, m_nUserSid, m_nUserDBID, m_nTargetItemDBID, optionCount);
    // Current Cached.exe 0x004c21b4 passes the same buffer and
    // capacity to every free Assemble call. This overload is not the normal
    // production packet-helper path. Preserve its overwrite behaviour.
    assembled += ::Assemble(buffer, bufferSize, "c", m_bIsExtractionJob ? 1 : 0);
    for (int slotType = 1; slotType < 3; ++slotType) {
        const auto& options = m_mEnsoulInfo[static_cast<std::size_t>(slotType)];
        for (const auto& option : options) {
            assembled += ::Assemble(buffer, bufferSize, "ddd", slotType, option.first, option.second);
        }
    }
    return assembled;
}

int EtcManiEnsoul::Assemble(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    int optionCount = 0;
    for (int slotType = 1; slotType < 3; ++slotType) {
        optionCount += static_cast<int>(m_mEnsoulInfo[static_cast<std::size_t>(slotType)].size());
    }

    int assembled = packet.Assemble("hdddd", ETC_MANI_ENSOUL, m_nUserSid, m_nUserDBID, m_nTargetItemDBID, optionCount);
    assembled += packet.Assemble("c", m_bIsExtractionJob ? 1 : 0);
    for (int slotType = 1; slotType < 3; ++slotType) {
        const auto& options = m_mEnsoulInfo[static_cast<std::size_t>(slotType)];
        for (const auto& option : options) {
            assembled += packet.Assemble("ddd", slotType, option.first, option.second);
        }
    }
    return assembled;
}

const unsigned char* EtcManiEnsoul::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int optionCount = 0;
    char extractionJob = 0;
    packet = ::Disassemble(packet, "dddd", &m_nUserSid, &m_nUserDBID, &m_nTargetItemDBID, &optionCount);
    packet = ::Disassemble(packet, "c", &extractionJob);
    m_bIsExtractionJob = extractionJob == 1;
    for (int index = 0; index < optionCount; ++index) {
        int slotType = -1;
        int slotIndex = -1;
        int optionId = -1;
        packet = ::Disassemble(packet, "ddd", &slotType, &slotIndex, &optionId);
        m_mEnsoulInfo[static_cast<std::size_t>(slotType)].emplace(slotIndex, optionId);
    }
    return packet;
}

bool EtcManiEnsoul::DoAtCacheInternal() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c7504.
    CUserSP user = g_userDb.GetUser(m_nUserDBID, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]cannot find user. User id[%d]",
        //     "..\\Shared\\EtcMani.cpp",
        //     0x9fb,
        //     m_nUserDBID)
        L2_LOG_ERROR(Log, "[%s][%d]cannot find user. User id[%d]", L2_SOURCE_FILE, __LINE__, m_nUserDBID);
        return false;
    }

    CWareHouseSP warehouse = user->GetWareHouse(0, true);
    if (!warehouse) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]cannot find warehouse.",
        //     "..\\Shared\\EtcMani.cpp",
        //     0xa02)
        L2_LOG_ERROR(Log, "[%s][%d]cannot find warehouse.", L2_SOURCE_FILE, __LINE__);
        return false;
    }

    CItemSP item = warehouse->GetItem(m_nTargetItemDBID);
    if (!item) {
        return false;
    }

    std::array<char16_t, 256> oldEnsoulText{};
    // item->MakeEnsoulLogInfo(oldEnsoulText.data())
    item->MakeEnsoulLogInfo(oldEnsoulText.data(), oldEnsoulText.size());

    constexpr const wchar_t* setStatement = L"{Call lin_SetItemEnsoulInfo(?, ?, ?, ?)}";
    for (int slotType = 1; slotType < 3; ++slotType) {
        m_mOldEnsoulInfo[static_cast<std::size_t>(slotType)] = item->m_ensoulInfo[slotType];
        for (const auto& [slotIndex, optionId] : m_mEnsoulInfo[static_cast<std::size_t>(slotType)]) {
            DBConn connection(runtime::process::CurrentDatabaseId());
            connection.Prepare(setStatement);
            connection.BindParameter(&m_nTargetItemDBID);
            connection.BindParameter(&slotType);
            connection.BindParameter(&slotIndex);
            connection.BindParameter(&optionId);
            if (!connection.ExecutePrepared(setStatement)) {
                CLogFormatter formatter;
                formatter.WriteLogCache2(m_bIsExtractionJob ? 0x2356 : 0x234c, user.get(), item.get(), 0, 0, 0, 0, 0, 0);
                return false;
            }
            item->SetEnsoulInfo(slotType, slotIndex, optionId);
        }
    }

    std::array<char16_t, 256> newEnsoulText{};
    // item->MakeEnsoulLogInfo(newEnsoulText.data())
    item->MakeEnsoulLogInfo(newEnsoulText.data(), newEnsoulText.size());
    CLogFormatter formatter;
    formatter.WriteLogCache1(
        m_bIsExtractionJob ? 0x2357 : 0x234d, user.get(), item.get(), oldEnsoulText.data(), newEnsoulText.data(), nullptr, 0,
        0, 0, 0, 0, 0
    );
    return true;
#else
    return false;
#endif
}

bool EtcManiEnsoul::Rollback() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c5948.
    CUserSP user = g_userDb.GetUser(m_nUserDBID, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]cannot find user. User id[%d]",
        //     "..\\Shared\\EtcMani.cpp",
        //     0x95a,
        //     m_nUserDBID)
        L2_LOG_ERROR(Log, "[%s][%d]cannot find user. User id[%d]", L2_SOURCE_FILE, __LINE__, m_nUserDBID);
        return false;
    }

    CWareHouseSP warehouse = user->GetWareHouse(0, true);
    if (!warehouse) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]cannot find warehouse.",
        //     "..\\Shared\\EtcMani.cpp",
        //     0x961)
        L2_LOG_ERROR(Log, "[%s][%d]cannot find warehouse.", L2_SOURCE_FILE, __LINE__);
        return false;
    }

    CItemSP item = warehouse->GetItem(m_nTargetItemDBID);
    if (!item) {
        return false;
    }

    constexpr const wchar_t* resetStatement = L"{Call lin_ResetItemEnsoulInfo(?)}";
    {
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Prepare(resetStatement);
        connection.BindParameter(&m_nTargetItemDBID);
        if (!connection.ExecutePrepared(resetStatement)) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[Ensoul] EtcManiEnsoul Rollback Failed! ItemID[%d]",
            //     m_nTargetItemDBID)
            L2_LOG_ERROR(Log, "[Ensoul] EtcManiEnsoul Rollback Failed! ItemID[%d]", m_nTargetItemDBID);
            return false;
        }
    }

    constexpr const wchar_t* setStatement = L"{Call lin_SetItemEnsoulInfo(?, ?, ?, ?)}";
    for (int slotType = 1; slotType < 3; ++slotType) {
        item->m_ensoulInfo[slotType].clear();
        for (const auto& [slotIndex, optionId] : m_mOldEnsoulInfo[static_cast<std::size_t>(slotType)]) {
            DBConn connection(runtime::process::CurrentDatabaseId());
            connection.Prepare(setStatement);
            connection.BindParameter(&m_nTargetItemDBID);
            connection.BindParameter(&slotType);
            connection.BindParameter(&slotIndex);
            connection.BindParameter(&optionId);
            if (!connection.ExecutePrepared(setStatement)) {
                // Log.Add(
                //     LOG_ERROR,
                //     "[Ensoul] EtcManiEnsoul Rollback Failed! "
                //     "ItemID[%d] SlotType[%d] SlotIndex[%d] OptionID[%d]",
                //     m_nTargetItemDBID,
                //     slotType,
                //     slotIndex,
                //     optionId)
                L2_LOG_ERROR(
                    Log,
                    "[Ensoul] EtcManiEnsoul Rollback Failed! "
                    "ItemID[%d] SlotType[%d] SlotIndex[%d] OptionID[%d]",
                    m_nTargetItemDBID, slotType, slotIndex, optionId
                );
                return false;
            }
            item->SetEnsoulInfo(slotType, slotIndex, optionId);
        }
    }
    return true;
#else
    return false;
#endif
}

bool EtcManiEnsoul::DoAtServer() {
    // Current Cached.exe 0x004bcf20 is a guard-only false stub.
    L2_GUARD_FUNCTION();
    return false;
}

void EtcManiEnsoul::DumpError() {
    // Current Cached.exe 0x004bcfa0 is a guard-only void stub.
    L2_GUARD_FUNCTION();
}

EtcManiMultisellResult::EtcManiMultisellResult() = default;

int EtcManiMultisellResult::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hdddddc", ETC_MANI_MULTISELL, m_nUserSID, m_nUserDBID, m_ePointType, m_nPoint, m_nGroupId,
        m_bSuccess ? 1 : 0
    );
}

int EtcManiMultisellResult::Assemble(IPacketHelper& packet) {
    // Current Cached.exe compact virtual body 0x004ba47c.
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hdddddc", ETC_MANI_MULTISELL, m_nUserSID, m_nUserDBID, m_ePointType, m_nPoint, m_nGroupId, m_bSuccess ? 1 : 0
    );
}

const unsigned char* EtcManiMultisellResult::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "dddddc", &m_nUserSID, &m_nUserDBID, &m_ePointType, &m_nPoint, &m_nGroupId, &m_bSuccess);
}

bool EtcManiMultisellResult::DoAtCacheInternal() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c0514. The supplied point is an adjustment to
    // the persisted balance. Unsupported point families leave
    // it unchanged while still reporting a created user as success.
    CUserSP user = g_userDb.GetUser(m_nUserDBID, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        m_bSuccess = false;
        return true;
    }

    m_bSuccess = true;
    switch (m_ePointType) {
        case PT_PCCAFE_POINT:
            m_nPoint += user->LoadPCCafePoint();
            break;
        case PT_PLEDGE_POINT: {
            CPledgeSP pledge =
                g_pledgeDb.GetPledge(static_cast<int>(user->GetPledgeID()), false, runtime::process::CurrentDatabaseId());
            if (pledge) {
                m_nPoint += pledge->rootNameValue();
            }
            break;
        }
        case PT_PVP_POINT:
        case PT_RAID_POINT:
            m_nPoint += user->LoadUserPoint(m_ePointType);
            break;
        default:
            break;
    }
    return true;
#else
    return false;
#endif
}

bool EtcManiMultisellResult::Rollback() {
    // Current Cached.exe 0x004bd1e4 is an unconditional success stub.
    L2_GUARD_FUNCTION();
    return true;
}

bool EtcManiMultisellResult::DoAtServer() {
    // Current Cached.exe 0x004bd264 is a guard-only false stub.
    L2_GUARD_FUNCTION();
    return false;
}

void EtcManiMultisellResult::DumpError() {
    // Current Cached.exe 0x004bd2e4 is a guard-only void stub.
    L2_GUARD_FUNCTION();
}

EtcManiSuccessfulItemAuctionBid::EtcManiSuccessfulItemAuctionBid() = default;

int EtcManiSuccessfulItemAuctionBid::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hdddQddd", ETC_MANI_SUCCESSFUL_ITEM_AUCTION_BID, m_nBidID, static_cast<int>(m_nUserDBID),
        m_nAuctionID, static_cast<long long>(m_nAdenaAmount), m_nItemClassId, m_nItemCount, m_nAuctionNPCId
    );
}

int EtcManiSuccessfulItemAuctionBid::Assemble(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hdddQddd", ETC_MANI_SUCCESSFUL_ITEM_AUCTION_BID, m_nBidID, static_cast<int>(m_nUserDBID), m_nAuctionID,
        static_cast<long long>(m_nAdenaAmount), m_nItemClassId, m_nItemCount, m_nAuctionNPCId
    );
}

const unsigned char* EtcManiSuccessfulItemAuctionBid::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(
        packet, "dddQddd", &m_nBidID, &m_nUserDBID, &m_nAuctionID, &m_nAdenaAmount, &m_nItemClassId, &m_nItemCount,
        &m_nAuctionNPCId
    );
}

bool EtcManiSuccessfulItemAuctionBid::DoAtCacheInternal() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c5f70.
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&m_nBidID);
    constexpr const wchar_t* query = L"{Call lin_SuccessfulItemAuctionBid (?, ?, ?)}";
    connection.Prepare(query);
    connection.BindParameter(&m_nAuctionID);
    connection.BindParameter(&m_nUserDBID);
    connection.BindParameter(&m_nAdenaAmount);
    if (connection.ExecutePrepared(query) && connection.Fetch() && m_nBidID > 0) {
        return true;
    }

    CUserSP user = g_userDb.GetUser(static_cast<int>(m_nUserDBID), true, false, runtime::process::CurrentDatabaseId());
    const std::u16string characterName = user ? std::u16string(user->GetCharName()) : std::u16string{};
    const std::u16string accountName = user ? std::u16string(user->GetAccountName()) : std::u16string{};
    CLogFormatter formatter;
    formatter.WriteLog(
        0x2352, m_nUserDBID, user ? user->GetAccountID() : 0, 0, 0, 0, 0, 0, u"", u"", u"", m_nAuctionID, m_nItemClassId,
        m_nItemCount, m_nAdenaAmount, m_nAuctionNPCId, m_nBidID, 0, 0, 0, 0, characterName.c_str(), accountName.c_str(), u"",
        u"", 0
    );
    return false;
#else
    return false;
#endif
}

bool EtcManiSuccessfulItemAuctionBid::Rollback() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c6380 requires both ExecutePrepared and Fetch.
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* query = L"{Call lin_SuccessfulItemAuctionBid_Rollback (?, ?)}";
    connection.Prepare(query);
    connection.BindParameter(&m_nBidID);
    connection.BindParameter(&m_nAuctionID);
    if (!connection.ExecutePrepared(query)) {
        return false;
    }
    return connection.Fetch();
#else
    return false;
#endif
}

bool EtcManiSuccessfulItemAuctionBid::DoAtServer() {
    // Current Cached.exe 0x004bd620 is a guard-only false stub.
    L2_GUARD_FUNCTION();
    return false;
}

void EtcManiSuccessfulItemAuctionBid::DumpError() {
    // Current Cached.exe 0x004bd6a0 is a guard-only void stub.
    L2_GUARD_FUNCTION();
}

EtcManiCardUpDownGame::EtcManiCardUpDownGame() = default;

int EtcManiCardUpDownGame::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hdddddd", ETC_MANI_CARD_UPDOWN_GAME, m_nReqType, m_nResult, m_nGameId, m_nRollbackGameState,
        m_nUserSid, m_nUserDBID
    );
}

int EtcManiCardUpDownGame::Assemble(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hdddddd", ETC_MANI_CARD_UPDOWN_GAME, m_nReqType, m_nResult, m_nGameId, m_nRollbackGameState, m_nUserSid, m_nUserDBID
    );
}

const unsigned char* EtcManiCardUpDownGame::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(
        packet, "dddddd", &m_nReqType, &m_nResult, &m_nGameId, &m_nRollbackGameState, &m_nUserSid, &m_nUserDBID
    );
}

bool EtcManiCardUpDownGame::DoAtCacheInternal() {
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe vtable slot +0x58 -> 0x004c64e4. The database
    // failure is diagnostic only. The result field and return value remain 1.
    m_nResult = 1;
    const int zero = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* query = L"{Call lin_UpdateCardUpdownGame(?,?,?,?,?,?,?)}";
    connection.Prepare(query);
    connection.BindParameter(&m_nUserDBID);
    for (int index = 0; index < 6; ++index) {
        connection.BindParameter(&zero);
    }
    if (!connection.ExecutePrepared(query)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[EtcManiCardUpDownGame] db update fail!!")
        L2_LOG_ERROR(Log, "[EtcManiCardUpDownGame] db update fail!!");
    }
    return true;
#else
    return false;
#endif
}

bool EtcManiCardUpDownGame::Rollback() {
    // Current Cached.exe compact virtual body 0x004ba4cc.
    m_nResult = 0;
    return false;
}

bool EtcManiCardUpDownGame::DoAtServer() {
    // Folded current false-return body 0x004ba4d4.
    return false;
}
void EtcManiCardUpDownGame::DumpError() {}

EtcManiRemoveInzoneBindingByEvent::EtcManiRemoveInzoneBindingByEvent() = default;

int EtcManiRemoveInzoneBindingByEvent::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hdddd", ETC_MANI_REMOVE_INZONE_BINDING_BY_EVENT, m_nUserSid, m_nResult, m_nInzoneTypeID,
        m_isGroupRestriction
    );
}

int EtcManiRemoveInzoneBindingByEvent::Assemble(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hdddd", ETC_MANI_REMOVE_INZONE_BINDING_BY_EVENT, m_nUserSid, m_nResult, m_nInzoneTypeID, m_isGroupRestriction
    );
}

const unsigned char* EtcManiRemoveInzoneBindingByEvent::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "dddd", &m_nUserSid, &m_nResult, &m_nInzoneTypeID, &m_isGroupRestriction);
}

bool EtcManiRemoveInzoneBindingByEvent::Rollback() {
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004ba4d8.
    m_nResult = 0;
    return false;
#else
    m_nResult = 0;
    return false;
#endif
}

bool EtcManiRemoveInzoneBindingByEvent::DoAtServer() {
    // Folded current false-return body 0x004ba4d4.
    return false;
}
void EtcManiRemoveInzoneBindingByEvent::DumpError() {}

bool EtcManiRemoveInzoneBindingByEvent::DoAtCacheInternal() {
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004ba4e0.
    m_nResult = 1;
    return true;
#else
    // Current L2NPC.exe has the same tiny body.
    m_nResult = 1;
    return true;
#endif
}

CETC_MANI_PLEDGE_MASTERY_SET::CETC_MANI_PLEDGE_MASTERY_SET() :
    m_idPledge(0), m_idMastery(PledgeMasteryID::NONE), m_nPoint(0), m_idUser(0) {
    // Current L2NPC.exe 0x00427624 and matching PDB lines 3291-3296.
    // m_idWorld and m_nPrevPoint are not initialized by the
    // source constructor. Heap instances still start at zero because the
    // target OBJECT_TYPE allocator clears the complete allocation.
}

CETC_MANI_PLEDGE_MASTERY_SET::CETC_MANI_PLEDGE_MASTERY_SET(
    int worldId, int pledgeId, PledgeMasteryID::Type masteryId, int point, unsigned int userId
) :
    m_idWorld(worldId), m_idPledge(pledgeId), m_idMastery(masteryId), m_nPoint(point), m_idUser(userId) {
    // Matching PDB Shared/EtcMani.cpp:3304-3310. m_nPrevPoint is cache-side
    // rollback state and is not initialized by this constructor.
}

int CETC_MANI_PLEDGE_MASTERY_SET::Assemble(char* buffer, int bufferSize) {
    // Current L2NPC.exe 0x00426cdc. Cached.exe compact body 0x004ba534.
    return ::Assemble(
        buffer, bufferSize, "hddddd", ETC_MANI_PLEDGE_MASTERY_SET, m_idWorld, m_idPledge, static_cast<int>(m_idMastery),
        m_nPoint, static_cast<int>(m_idUser)
    );
}

const unsigned char* CETC_MANI_PLEDGE_MASTERY_SET::Disassemble(const unsigned char* packet) {
    // Current L2NPC.exe 0x004277d4.
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "ddddd", &m_idWorld, &m_idPledge, &m_idMastery, &m_nPoint, &m_idUser);
}

bool CETC_MANI_PLEDGE_MASTERY_SET::Rollback() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c06a8.
    CPledgeSP pledge = g_pledgeDb.GetPledge(m_idPledge, false, runtime::process::CurrentDatabaseId());
    if (!pledge || !pledge->m_mastery) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[EtcMani.cpp][3322] No pledge: %d",
        //     m_idPledge)
        L2_LOG_ERROR(Log, "[EtcMani.cpp][3322] No pledge: %d", m_idPledge);
        return false;
    }
    if (!pledge->m_mastery->SetMasteryPoint(m_idMastery, m_nPrevPoint)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[EtcMani.cpp][3328] Failed to rollback mastery point: %d, %d, %d",
        //     m_idPledge,
        //     static_cast<int>(m_idMastery),
        //     m_nPrevPoint)
        L2_LOG_ERROR(
            Log, "[EtcMani.cpp][3328] Failed to rollback mastery point: %d, %d, %d", m_idPledge,
            static_cast<int>(m_idMastery), m_nPrevPoint
        );
        return false;
    }
    return true;
#else
    // Current L2NPC.exe 0x00427654.
    return false;
#endif
}

bool CETC_MANI_PLEDGE_MASTERY_SET::DoAtServer() {
    // Current L2NPC.exe 0x00427754.
    L2_GUARD_FUNCTION();
    return false;
}

void CETC_MANI_PLEDGE_MASTERY_SET::DumpError() {
    // Current L2NPC.exe 0x00426c94. Cached.exe 0x004ba4ec.
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][%s][%d]"
    //     L"CETC_MANI_PLEDGE_MASTERY_SET::"
    //     L"DumpError(): %d, %d, %d",
    //     L"..\\Shared\\EtcMani.cpp",
    //     0xd4e,
    //     m_idPledge,
    //     static_cast<int>(m_idMastery),
    //     m_nPoint)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][%s][%d]"
    //     L"CETC_MANI_PLEDGE_MASTERY_SET::"
    //     L"DumpError(): %d, %d, %d",
    //     L"..\\Shared\\EtcMani.cpp",
    //     0xd4e,
    //     m_idPledge,
    //     static_cast<int>(m_idMastery),
    //     m_nPoint)
    // Previous: L2_LOG_ERROR(Log,
    //     "Clan mastery modification diagnostic: clan %d, mastery %d, requested points %d.",
    //     m_idPledge, static_cast<int>(m_idMastery), m_nPoint)
    L2_LOG_ERROR(
        Log, "[clan mastery] Modification failed for clan %d, mastery %d, requested points %d.", m_idPledge,
        static_cast<int>(m_idMastery), m_nPoint
    );
}

bool CETC_MANI_PLEDGE_MASTERY_SET::DoAtCacheInternal() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c087c.
    CPledgeSP pledge = g_pledgeDb.GetPledge(m_idPledge, false, runtime::process::CurrentDatabaseId());
    if (!pledge || !pledge->m_mastery) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[EtcMani.cpp][3350] No pledge: %d",
        //     m_idPledge)
        L2_LOG_ERROR(Log, "[EtcMani.cpp][3350] No pledge: %d", m_idPledge);
        return false;
    }
    const std::uint32_t mastery = static_cast<std::uint32_t>(m_idMastery);
    if (mastery - 1U >= 16U) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[EtcMani.cpp][3356] Invalid mastery id: %d, %d",
        //     m_idPledge,
        //     static_cast<int>(m_idMastery))
        L2_LOG_ERROR(Log, "[EtcMani.cpp][3356] Invalid mastery id: %d, %d", m_idPledge, static_cast<int>(m_idMastery));
        return false;
    }
    m_nPrevPoint = pledge->m_mastery->GetMasteryPoint(m_idMastery);
    if (!pledge->m_mastery->SetMasteryPoint(m_idMastery, m_nPoint)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[EtcMani.cpp][3364] Failed to set mastery point: %d, %d, %d",
        //     m_idPledge,
        //     static_cast<int>(m_idMastery),
        //     m_nPoint)
        L2_LOG_ERROR(
            Log, "[EtcMani.cpp][3364] Failed to set mastery point: %d, %d, %d", m_idPledge, static_cast<int>(m_idMastery),
            m_nPoint
        );
        return false;
    }
    return true;
#else
    // Current L2NPC.exe 0x004276d4.
    return false;
#endif
}

CETC_MANI_PLEDGE_MASTERY_RESET::CETC_MANI_PLEDGE_MASTERY_RESET() : m_idWorld(0), m_idPledge(0), m_idUser(0) {
    // Current L2NPC.exe 0x004278a4 and matching PDB lines 3443-3447.
    // m_anPrevPoint is populated only by the cache-side implementation.
}

CETC_MANI_PLEDGE_MASTERY_RESET::CETC_MANI_PLEDGE_MASTERY_RESET(int worldId, int pledgeId, unsigned int userId) :
    m_idWorld(worldId), m_idPledge(pledgeId), m_idUser(userId) {
    // Matching PDB Shared/EtcMani.cpp:3450-3454.
}

int CETC_MANI_PLEDGE_MASTERY_RESET::Assemble(char* buffer, int bufferSize) {
    // Current L2NPC.exe 0x004278d4.
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hddd", ETC_MANI_PLEDGE_MASTERY_RESET, m_idWorld, m_idPledge, static_cast<int>(m_idUser)
    );
}

const unsigned char* CETC_MANI_PLEDGE_MASTERY_RESET::Disassemble(const unsigned char* packet) {
    // Current L2NPC.exe 0x004279ac.
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "ddd", &m_idWorld, &m_idPledge, &m_idUser);
}

bool CETC_MANI_PLEDGE_MASTERY_RESET::Rollback() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c0d48 attempts all sixteen writes and reports
    // success once the pledge exists, even if an individual write fails.
    CPledgeSP pledge = g_pledgeDb.GetPledge(m_idPledge, false, runtime::process::CurrentDatabaseId());
    if (!pledge || !pledge->m_mastery) {
        return false;
    }
    for (int mastery = 1; mastery < PledgeMasteryID::MAX_COUNT; ++mastery) {
        if (!pledge->m_mastery->SetMasteryPoint(static_cast<PledgeMasteryID::Type>(mastery), m_anPrevPoint[mastery])) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[EtcMani.cpp][3536] Failed to rollback mastery point: %d, %d, %d",
            //     m_idPledge,
            //     mastery,
            //     m_anPrevPoint[mastery])
            L2_LOG_ERROR(
                Log, "[EtcMani.cpp][3536] Failed to rollback mastery point: %d, %d, %d", m_idPledge, mastery,
                m_anPrevPoint[mastery]
            );
        }
    }
    return true;
#else
    // Current L2NPC.exe 0x00427ae8.
    return false;
#endif
}

bool CETC_MANI_PLEDGE_MASTERY_RESET::DoAtServer() {
    // Current L2NPC.exe 0x00427b68.
    L2_GUARD_FUNCTION();
    return false;
}

void CETC_MANI_PLEDGE_MASTERY_RESET::DumpError() {
    // Current L2NPC.exe 0x00426d28. Cached.exe 0x004ba580.
    // Log.Add(
    //     LOG_ERROR,
    //     L"[PLEDGE][%s][%d]"
    //     L"CETC_MANI_PLEDGE_MASTERY_RESET::"
    //     L"DumpError(): %d",
    //     L"..\\Shared\\EtcMani.cpp",
    //     0xdf8,
    //     m_idPledge)
    L2_LOG_ERROR(
        Log,
        u"[PLEDGE][%s][%d]"
        u"CETC_MANI_PLEDGE_MASTERY_RESET::"
        u"DumpError(): %d",
        __WFILE__, __LINE__, m_idPledge
    );
}

bool CETC_MANI_PLEDGE_MASTERY_RESET::DoAtCacheInternal() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c0b40.
    CPledgeSP pledge = g_pledgeDb.GetPledge(m_idPledge, false, runtime::process::CurrentDatabaseId());
    if (!pledge || !pledge->m_mastery) {
        return false;
    }
    pledge->m_mastery->GetPoints(m_anPrevPoint);
    return pledge->m_mastery->ResetMasteryPoint();
#else
    // Current L2NPC.exe 0x00427a68.
    return false;
#endif
}

CETC_MANI_POINT_UPDATE::CETC_MANI_POINT_UPDATE() : m_sidUser(0), m_dbidUser(0), m_pointType(PT_NONE), m_pointAmount(0) {
    // Current L2NPC.exe 0x00427be8.
}

CETC_MANI_POINT_UPDATE::CETC_MANI_POINT_UPDATE(
    unsigned int userSid, int userDbId, UserPointType pointType, int pointAmount
) :
    m_sidUser(userSid), m_dbidUser(userDbId), m_pointType(pointType), m_pointAmount(pointAmount) {
    // Matching PDB Shared/EtcMani.cpp:3590-3595.
}

int CETC_MANI_POINT_UPDATE::Assemble(char* buffer, int bufferSize) {
    // Current L2NPC.exe 0x00427c18.
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hdddd", ETC_MANI_POINT_UPDATE, static_cast<int>(m_sidUser), m_dbidUser,
        static_cast<int>(m_pointType), m_pointAmount
    );
}

const unsigned char* CETC_MANI_POINT_UPDATE::Disassemble(const unsigned char* packet) {
    // Current L2NPC.exe 0x00427cf8.
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "dddd", &m_sidUser, &m_dbidUser, &m_pointType, &m_pointAmount);
}

bool CETC_MANI_POINT_UPDATE::Rollback() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c1518.
    CUserSP user = g_userDb.GetUser(m_dbidUser, true, false, runtime::process::CurrentDatabaseId());
    return ModifyUserPoint(user, m_pointType, runtime::NegateInt32Wrapping(m_pointAmount));
#else
    // Current L2NPC.exe 0x00427e3c.
    return false;
#endif
}

bool CETC_MANI_POINT_UPDATE::DoAtServer() {
    // Current L2NPC.exe 0x00427ebc.
    L2_GUARD_FUNCTION();
    return false;
}

void CETC_MANI_POINT_UPDATE::DumpError() {
    // Current L2NPC.exe 0x00426d60. Cached.exe 0x004ba5b8.
    // Log.Add(
    //     LOG_ERROR,
    //     L"[PLEDGE][%s][%d]DumpError: %d, %d, %d",
    //     L"..\\Shared\\EtcMani.cpp",
    //     0xecc,
    //     m_dbidUser,
    //     static_cast<int>(m_pointType),
    //     m_pointAmount)
    L2_LOG_ERROR(
        Log, u"[PLEDGE][%s][%d]DumpError: %d, %d, %d", __WFILE__, __LINE__, m_dbidUser, static_cast<int>(m_pointType),
        m_pointAmount
    );
}

bool CETC_MANI_POINT_UPDATE::DoAtCacheInternal() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c137c.
    CUserSP user = g_userDb.GetUser(m_dbidUser, true, false, runtime::process::CurrentDatabaseId());
    return ModifyUserPoint(user, m_pointType, m_pointAmount);
#else
    // Current L2NPC.exe 0x00427dbc.
    return false;
#endif
}

CETC_MANI_PLEDGE_MISSION_REWARD::CETC_MANI_PLEDGE_MISSION_REWARD() :
    nUserDbId_(0), nMissionId_(0), nCount_(0), tSaveRewardTime_(0), tRewardTime_(0) {
    // Current L2NPC.exe 0x00427f3c.
}

CETC_MANI_PLEDGE_MISSION_REWARD::CETC_MANI_PLEDGE_MISSION_REWARD(
    int userDbId, int missionId, int count, std::int32_t saveRewardTime
) :
    nUserDbId_(userDbId), nMissionId_(missionId), nCount_(count), tSaveRewardTime_(saveRewardTime), tRewardTime_(0) {
    // Matching PDB Shared/EtcMani.cpp:3802-3808.
}

int CETC_MANI_PLEDGE_MISSION_REWARD::Assemble(char* buffer, int bufferSize) {
    // Current L2NPC.exe 0x00427f70.
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hddddd", ETC_MANI_PLEDGE_MISSION_REWARD, nUserDbId_, nMissionId_, nCount_, tSaveRewardTime_,
        tRewardTime_
    );
}

const unsigned char* CETC_MANI_PLEDGE_MISSION_REWARD::Disassemble(const unsigned char* packet) {
    // Current L2NPC.exe 0x00428054.
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "ddddd", &nUserDbId_, &nMissionId_, &nCount_, &tSaveRewardTime_, &tRewardTime_);
}

bool CETC_MANI_PLEDGE_MISSION_REWARD::Rollback() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c6760 refreshes the reply-time field but saves
    // the original request's tSaveRewardTime_. Execute failure is diagnostic
    // only and Rollback still reports success.
    tRewardTime_ = runtime::crt::Time32Now();
    long saveTimeAsDouble = static_cast<long>(tSaveRewardTime_);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* query = L"{Call lin_PledgeMissionSave (?,?,?,?)}";
    connection.Prepare(query);
    connection.BindParameter(&nUserDbId_);
    connection.BindParameter(&nMissionId_);
    connection.BindParameter(&nCount_);
    connection.BindParameter(&saveTimeAsDouble);
    if (!connection.ExecutePrepared(query)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[PLEDGE][EtcMani.cpp][3889] NOT IMPLEMENTED!!!!!")
        L2_LOG_ERROR(Log, "[PLEDGE][EtcMani.cpp][3889] NOT IMPLEMENTED!!!!!");
    }
    return true;
#else
    // Current L2NPC.exe 0x004281a4.
    return false;
#endif
}

bool CETC_MANI_PLEDGE_MISSION_REWARD::DoAtServer() {
    // Current L2NPC.exe 0x00428224.
    L2_GUARD_FUNCTION();
    return false;
}

void CETC_MANI_PLEDGE_MISSION_REWARD::DumpError() {
    // Current L2NPC.exe 0x00426da8. Cached.exe 0x004ba600.
    // Log.Add(
    //     LOG_ERROR,
    //     L"[PLEDGE][%s][%d][Mission]"
    //     L"DumpError: %d, %d, %d, %d",
    //     L"..\\Shared\\EtcMani.cpp",
    //     0xf53,
    //     nUserDbId_,
    //     nMissionId_,
    //     nCount_,
    //     tSaveRewardTime_)
    L2_LOG_ERROR(
        Log,
        u"[PLEDGE][%s][%d][Mission]"
        u"DumpError: %d, %d, %d, %d",
        __WFILE__, __LINE__, nUserDbId_, nMissionId_, nCount_, tSaveRewardTime_
    );
}

bool CETC_MANI_PLEDGE_MISSION_REWARD::DoAtCacheInternal() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c65ac.
    int result = 0;
    tRewardTime_ = runtime::crt::Time32Now();
    long rewardTimeAsDouble = static_cast<long>(tRewardTime_);
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    constexpr const wchar_t* query = L"{Call lin_PledgeMissionReward (?,?,?,?)}";
    connection.Prepare(query);
    connection.BindParameter(&nUserDbId_);
    connection.BindParameter(&nMissionId_);
    connection.BindParameter(&nCount_);
    connection.BindParameter(&rewardTimeAsDouble);
    if (!connection.ExecutePrepared(query)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[PLEDGE][EtcMani.cpp][3858] NOT IMPLEMENTED!!!!!")
        L2_LOG_ERROR(Log, "[PLEDGE][EtcMani.cpp][3858] NOT IMPLEMENTED!!!!!");
    }
    // Shipping calls Fetch even after ExecutePrepared failure.
    connection.Fetch();
    return result != 0;
#else
    // Current L2NPC.exe 0x00428124.
    return false;
#endif
}

CETC_MANI_PLEDGE_CONTRIBUTION_REWARD::CETC_MANI_PLEDGE_CONTRIBUTION_REWARD() :
    m_sidUser(0), m_dbidUser(0), m_idPledge(0), m_nRewardStep(0), m_nMinPoint(0) {
    // Current L2NPC.exe 0x004282a4.
}

CETC_MANI_PLEDGE_CONTRIBUTION_REWARD::CETC_MANI_PLEDGE_CONTRIBUTION_REWARD(
    unsigned int userSid, int userDbId, int pledgeId, int rewardStep, int minPoint
) :
    m_sidUser(userSid), m_dbidUser(userDbId), m_idPledge(pledgeId), m_nRewardStep(rewardStep), m_nMinPoint(minPoint) {
    // Matching PDB Shared/EtcMani.cpp:3942-3948.
}

int CETC_MANI_PLEDGE_CONTRIBUTION_REWARD::Assemble(char* buffer, int bufferSize) {
    // Current L2NPC.exe 0x004282d8.
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hddddd", ETC_MANI_PLEDGE_CONTRIBUTION_REWARD, static_cast<int>(m_sidUser), m_dbidUser,
        m_idPledge, m_nRewardStep, m_nMinPoint
    );
}

const unsigned char* CETC_MANI_PLEDGE_CONTRIBUTION_REWARD::Disassemble(const unsigned char* packet) {
    // Current L2NPC.exe 0x004283bc.
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "ddddd", &m_sidUser, &m_dbidUser, &m_idPledge, &m_nRewardStep, &m_nMinPoint);
}

bool CETC_MANI_PLEDGE_CONTRIBUTION_REWARD::Rollback() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c68a0.
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* query = L"{Call lin_PledgeContributionMemberSaveReward (?,?,?)}";
    connection.Prepare(query);
    connection.BindParameter(&m_idPledge);
    connection.BindParameter(&m_dbidUser);
    connection.BindParameter(&m_nRewardStep);
    return connection.ExecutePrepared(query);
#else
    // Current L2NPC.exe 0x0042848c.
    return false;
#endif
}

bool CETC_MANI_PLEDGE_CONTRIBUTION_REWARD::DoAtServer() {
    // Current L2NPC.exe 0x0042858c.
    L2_GUARD_FUNCTION();
    return false;
}

void CETC_MANI_PLEDGE_CONTRIBUTION_REWARD::DumpError() {
    // Current L2NPC.exe 0x00426df4. Cached.exe 0x004ba64c.
    // Log.Add(
    //     LOG_ERROR,
    //     L"[PLEDGE][%s][%d]DumpError: %d, %d, %d, %d",
    //     L"..\\Shared\\EtcMani.cpp",
    //     0xfcc,
    //     m_idPledge,
    //     m_dbidUser,
    //     m_nRewardStep,
    //     m_nMinPoint)
    L2_LOG_ERROR(
        Log, u"[PLEDGE][%s][%d]DumpError: %d, %d, %d, %d", __WFILE__, __LINE__, m_idPledge, m_dbidUser, m_nRewardStep,
        m_nMinPoint
    );
}

bool CETC_MANI_PLEDGE_CONTRIBUTION_REWARD::DoAtCacheInternal() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c6a0c.
    int result = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    constexpr const wchar_t* query = L"{Call lin_PledgeContributionMemberTakeReward (?,?,?,?)}";
    connection.Prepare(query);
    connection.BindParameter(&m_idPledge);
    connection.BindParameter(&m_dbidUser);
    connection.BindParameter(&m_nRewardStep);
    connection.BindParameter(&m_nMinPoint);
    if (!connection.ExecutePrepared(query)) {
        return false;
    }
    return connection.Fetch() && result != 0;
#else
    // Current L2NPC.exe 0x0042850c.
    return false;
#endif
}

CETC_MANI_USE_COUNT::CETC_MANI_USE_COUNT() : m_sidUser(0), m_dbidUser(0), m_itemId(0), m_useCount(0), m_maxUseCount(0) {
    // Current L2NPC.exe 0x0042860c.
}

CETC_MANI_USE_COUNT::CETC_MANI_USE_COUNT(
    unsigned int userSid, int userDbId, unsigned int itemId, short useCount, short maxUseCount
) :
    m_sidUser(userSid), m_dbidUser(userDbId), m_itemId(itemId), m_useCount(useCount), m_maxUseCount(maxUseCount) {
    // Matching PDB Shared/EtcMani.cpp:4065-4071.
}

int CETC_MANI_USE_COUNT::Assemble(char* buffer, int bufferSize) {
    // Current L2NPC.exe 0x0042863c.
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hdddhh", ETC_MANI_USE_COUNT, static_cast<int>(m_sidUser), m_dbidUser,
        static_cast<int>(m_itemId), static_cast<int>(m_useCount), static_cast<int>(m_maxUseCount)
    );
}

const unsigned char* CETC_MANI_USE_COUNT::Disassemble(const unsigned char* packet) {
    // Current L2NPC.exe 0x00428724.
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "dddhh", &m_sidUser, &m_dbidUser, &m_itemId, &m_useCount, &m_maxUseCount);
}

bool CETC_MANI_USE_COUNT::Rollback() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c6b64 always returns true. It decrements the
    // live item value instead of restoring the serialized member value.
    CUserSP user = g_userDb.GetUser(m_dbidUser, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        return true;
    }
    CWareHouseSP warehouse = user->GetWareHouse(0, true);
    if (!warehouse) {
        return true;
    }
    CItemSP item = warehouse->GetItem(static_cast<int>(m_itemId));
    if (!item) {
        return true;
    }

    m_useCount = runtime::AddInt16Wrapping(item->UseCount(), -1);
    if (m_useCount < 0) {
        return true;
    }
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* query = L"{Call lin_UpdateItemUseCount(?, ?)}";
    connection.Prepare(query);
    connection.BindParameter(&m_itemId);
    connection.BindParameter(&m_useCount);
    if (connection.ExecutePrepared(query)) {
        item->m_useCount = m_useCount;
    }
    return true;
#else
    // Current L2NPC.exe 0x004287f4.
    return false;
#endif
}

bool CETC_MANI_USE_COUNT::DoAtServer() {
    // Current L2NPC.exe 0x004288f4.
    L2_GUARD_FUNCTION();
    return false;
}

void CETC_MANI_USE_COUNT::DumpError() {
    // Current L2NPC.exe 0x00426e40. Cached.exe 0x004ba698.
    // The missing [%s][%d] prefix is an original source bug: current machine
    // code still passes EtcMani.cpp and line 0x10a4 before the four object
    // fields. The exact AddT argument sequence is kept below. The
    // authorized logging overlay reports the actual fields with matching types.
    // Log.AddT(
    //     LOG_ERROR,
    //     L"DumpError: %d, %d, %d, %c",
    //     "..\\Shared\\EtcMani.cpp",
    //     0x10a4,
    //     m_sidUser,
    //     m_dbidUser,
    //     m_itemId,
    //     m_useCount)
    L2_LOG_ERROR(
        Log, "[item use count] Modification diagnostic: character object %u, character %d, item %u, requested use count %d.",
        m_sidUser, m_dbidUser, m_itemId, static_cast<int>(m_useCount)
    );
}

bool CETC_MANI_USE_COUNT::DoAtCacheInternal() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c6e90. m_maxUseCount is not
    // consulted by the shipping cache-side implementation.
    CUserSP user = g_userDb.GetUser(m_dbidUser, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        return false;
    }
    CWareHouseSP warehouse = user->GetWareHouse(0, true);
    if (!warehouse) {
        return false;
    }
    CItemSP item = warehouse->GetItem(static_cast<int>(m_itemId));
    if (!item) {
        return false;
    }

    m_useCount = runtime::AddInt16Wrapping(item->UseCount(), 1);
    if (m_useCount < 0) {
        return false;
    }
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* query = L"{Call lin_UpdateItemUseCount(?, ?)}";
    connection.Prepare(query);
    connection.BindParameter(&m_itemId);
    connection.BindParameter(&m_useCount);
    if (!connection.ExecutePrepared(query)) {
        return false;
    }
    item->m_useCount = m_useCount;
    return true;
#else
    // Current L2NPC.exe 0x00428874.
    return false;
#endif
}

EtcManiEnchantArtifact::EtcManiEnchantArtifact() :
    m_nUserSID(0), m_nUserDBID(0), m_bEnchantSuccess(false), m_nTargetItemDBID(0), m_nTargetNewEnchant(0) {
    // Current L2NPC.exe 0x00428974 and matching PDB lines 4267-4273.
    m_nMaterialItemDBID.fill(0);
    // m_nTargetOrgEnchant is not initialized by the target
    // constructor and is not part of this process's wire representation.
}

int EtcManiEnchantArtifact::Assemble(IPacketHelper& packet) {
    // Current L2NPC.exe 0x00428aac.
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hddcddddd", ETC_MANI_ENCHANT_ARTIFACT, m_nUserSID, m_nUserDBID, static_cast<int>(m_bEnchantSuccess),
        m_nTargetItemDBID, m_nTargetNewEnchant, m_nMaterialItemDBID[0], m_nMaterialItemDBID[1], m_nMaterialItemDBID[2]
    );
}

int EtcManiEnchantArtifact::Assemble(char* buffer, int bufferSize) {
    // Current L2NPC.exe 0x004289b0 contains the literal "hddcddddddd" but
    // supplies only the nine values below. VAssemble so reads two
    // nonexistent varargs: the optimized Win64 frame happens to expose its
    // -2 EH state first and arbitrary old stack contents second. Preserve
    // that shipping mismatch instead of shortening the packet grammar.
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hddcddddddd", ETC_MANI_ENCHANT_ARTIFACT, m_nUserSID, m_nUserDBID,
        static_cast<int>(m_bEnchantSuccess), m_nTargetItemDBID, m_nTargetNewEnchant, m_nMaterialItemDBID[0],
        m_nMaterialItemDBID[1], m_nMaterialItemDBID[2]
    );
}

const unsigned char* EtcManiEnchantArtifact::Disassemble(const unsigned char* packet) {
    // Current L2NPC.exe 0x00428b98.
    L2_GUARD_FUNCTION();
    return ::Disassemble(
        packet, "ddcddddd", &m_nUserSID, &m_nUserDBID, reinterpret_cast<char*>(&m_bEnchantSuccess), &m_nTargetItemDBID,
        &m_nTargetNewEnchant, &m_nMaterialItemDBID[0], &m_nMaterialItemDBID[1], &m_nMaterialItemDBID[2]
    );
}

bool EtcManiEnchantArtifact::Rollback() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c16b4. A failed enchant does not modify the
    // target item, so there is nothing to restore in that case.
    CUserSP user = g_userDb.GetUser(m_nUserDBID, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        return false;
    }
    CWareHouseSP warehouse = user->GetWareHouse(0, true);
    if (!warehouse) {
        return false;
    }

    // Declare the lock guard first so the item is released
    // before the warehouse is unlocked.
    L2_WRITE_GUARD(*warehouse.operator->());
    CItemSP item = warehouse->GetItem(m_nTargetItemDBID);
    if (!item) {
        return false;
    }
    if (m_bEnchantSuccess) {
        item->SetEnchant(m_nTargetOrgEnchant);
        if (!item->Save(true)) {
            return false;
        }
    }
    return true;
#else
    // Current L2NPC.exe 0x00428c84.
    return false;
#endif
}

bool EtcManiEnchantArtifact::DoAtServer() {
    // Current L2NPC.exe 0x00428d04.
    L2_GUARD_FUNCTION();
    return false;
}

void EtcManiEnchantArtifact::DumpError() {
    // Current L2NPC.exe 0x00428d84.
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"EtcManiEnchantArtifact::DumpError() => "
    //     L"UserSID[%d], UserDBID[%d], isSuccess[%d]"
    //     L"TargetItemDBID[%d], TargetItemNewEnchant[%d], "
    //     L"MaterialDBID1[%d], MaterialDBID2[%d], "
    //     L"MaterialDBID3[%d]",
    //     m_nUserSID,
    //     m_nUserDBID,
    //     m_bEnchantSuccess ? 1 : 0,
    //     m_nTargetItemDBID,
    //     m_nTargetNewEnchant,
    //     m_nMaterialItemDBID[0],
    //     m_nMaterialItemDBID[1],
    //     m_nMaterialItemDBID[2])
    L2_LOG_ERROR(
        Log,
        "[artifact enchant] Modification diagnostic: character object %d, character %d, requested success flag %d, "
        "target item %d, requested enchant %d, material items %d/%d/%d.",
        m_nUserSID, m_nUserDBID, m_bEnchantSuccess ? 1 : 0, m_nTargetItemDBID, m_nTargetNewEnchant, m_nMaterialItemDBID[0],
        m_nMaterialItemDBID[1], m_nMaterialItemDBID[2]
    );
}

void EtcManiEnchantArtifact::Set(
    int userSid,
    int userDbId,
    bool enchantSuccess,
    int targetItemDbId,
    int targetNewEnchant,
    int materialItemDbId1,
    int materialItemDbId2,
    int materialItemDbId3
) {
    // PDB-declared inline Shared/EtcMani.h surface. It is not compiled as a
    // separate current-L2NPC function because this executable never calls it.
    m_nUserSID = userSid;
    m_nUserDBID = userDbId;
    m_bEnchantSuccess = enchantSuccess;
    m_nTargetItemDBID = targetItemDbId;
    m_nTargetNewEnchant = targetNewEnchant;
    m_nMaterialItemDBID[0] = materialItemDbId1;
    m_nMaterialItemDBID[1] = materialItemDbId2;
    m_nMaterialItemDBID[2] = materialItemDbId3;
}

bool EtcManiEnchantArtifact::DoAtCacheInternal() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004c1b44.
    CUserSP user = g_userDb.GetUser(m_nUserDBID, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        return false;
    }
    CWareHouseSP warehouse = user->GetWareHouse(0, true);
    if (!warehouse) {
        return false;
    }
    CItemSP item = warehouse->GetItem(m_nTargetItemDBID);
    if (!item) {
        return false;
    }

    if (m_bEnchantSuccess) {
        m_nTargetOrgEnchant = item->Enchant();
        item->SetEnchant(m_nTargetNewEnchant);
        if (!item->Save(true)) {
            item->SetEnchant(m_nTargetOrgEnchant);
            return false;
        }
    }
    return true;
#else
    // Current L2NPC.exe 0x00428e70.
    return false;
#endif
}

namespace EtcManiUtil {

void AssembleEtcManis(const std::vector<EtcMani*>& etcManis, CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();
    for (EtcMani* const etcMani : etcManis) {
        if (etcMani != nullptr) {
            etcMani->Assemble(packet);
        }
    }
}

void Destroy(std::vector<EtcMani*>& etcManis) {
    for (EtcMani* const etcMani : etcManis) {
        if (etcMani != nullptr) {
            etcMani->Destroy();
        }
    }
    etcManis.clear();
}

bool DoAtCache(const std::vector<EtcMani*>& etcManis, CPacketHelperEx<20480>& resultPacket) {
    // Current Cached.exe 0x004baadc.
    L2_GUARD_FUNCTION();
    for (EtcMani* etcMani : etcManis) {
        if (!etcMani->DoAtCache()) {
            etcMani->DumpError();
            return false;
        }
        etcMani->Assemble(resultPacket);
    }
    return true;
}

void Rollback(const std::vector<EtcMani*>& etcManis) {
    L2_GUARD_FUNCTION();
    for (EtcMani* etcMani : etcManis) {
        if (etcMani->IsSuccess()) {
            etcMani->Rollback();
        }
    }
}

const unsigned char* DisassembleEtcManis(const unsigned char* packet, int count, std::vector<EtcMani*>& etcManis) {
    L2_GUARD_FUNCTION();
    while (count-- > 0) {
        short typeValue = 0;
        packet = ::Disassemble(packet, "h", &typeValue);
        const ETC_MANI_TYPE type = static_cast<ETC_MANI_TYPE>(typeValue);
        EtcMani* etcMani = Singleton<EtcManiFactory>::inst().CreateObject(type);
        if (!etcMani) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Failed to create etc mani: %d",
            //     static_cast<int>(typeValue))
            L2_LOG_ERROR(
                Log,
                "[modifications] Cannot create handler for type %d; %d declared entries remain. "
                "Decoding continues after the type field without consuming a handler payload.",
                static_cast<int>(typeValue), count
            );
            continue;
        }
        packet = etcMani->Disassemble(packet);
        etcManis.push_back(etcMani);
    }
    return packet;
}

} // namespace EtcManiUtil

EtcMani* Create_EtcManiUpdatePledgeNameValue() { return new EtcManiUpdatePledgeNameValue(); }

EtcMani* Create_EtcManiUpdateUserEventTime() { return new EtcManiUpdateUserEventTime(); }

EtcMani* Create_EtcManiUpdateUserItemBaseAttribute() { return new EtcManiUpdateUserItemBaseAttribute(); }

EtcMani* Create_EtcManiRollbackUserItemBaseAttribute() { return new EtcManiRollbackUserItemBaseAttribute(); }

EtcMani* Create_EtcManiCreateItemAuctionBid() { return new EtcManiCreateItemAuctionBid(); }

EtcMani* Create_EtcManiWithdrawItemAuctionBid() { return new EtcManiWithdrawItemAuctionBid(); }

EtcMani* Create_EtcManiGiveItemByCastleSiegeCount() { return new EtcManiGiveItemByCastleSiegeCount(); }

EtcMani* Create_EtcManiGiveItemOnAccount() { return new EtcManiGiveItemOnAccount(); }

EtcMani* Create_EtcManiUpdateUserItemCombination() { return new EtcManiUpdateUserItemCombination(); }

EtcMani* Create_EtcManiEnsoul() { return new EtcManiEnsoul(); }

EtcMani* Create_EtcManiMultisellResult() { return new EtcManiMultisellResult(); }

EtcMani* Create_EtcManiSuccessfulItemAuctionBid() { return new EtcManiSuccessfulItemAuctionBid(); }

EtcMani* Create_EtcManiCardUpDownGame() { return new EtcManiCardUpDownGame(); }

EtcMani* Create_EtcManiRemoveInzoneBindingByEvent() { return new EtcManiRemoveInzoneBindingByEvent(); }

EtcMani* Create_CETC_MANI_PLEDGE_MASTERY_SET() {
    // Current L2NPC.exe 0x00428ef0.
    return new CETC_MANI_PLEDGE_MASTERY_SET();
}

EtcMani* Create_CETC_MANI_PLEDGE_MASTERY_RESET() {
    // Current L2NPC.exe 0x00428f24.
    return new CETC_MANI_PLEDGE_MASTERY_RESET();
}

EtcMani* Create_CETC_MANI_POINT_UPDATE() {
    // Current L2NPC.exe 0x00428f58.
    return new CETC_MANI_POINT_UPDATE();
}

EtcMani* Create_CETC_MANI_PLEDGE_MISSION_REWARD() {
    // Current L2NPC.exe 0x00428f8c.
    return new CETC_MANI_PLEDGE_MISSION_REWARD();
}

EtcMani* Create_CETC_MANI_PLEDGE_CONTRIBUTION_REWARD() {
    // Current L2NPC.exe 0x00428fc0.
    return new CETC_MANI_PLEDGE_CONTRIBUTION_REWARD();
}

EtcMani* Create_CETC_MANI_USE_COUNT() {
    // Current L2NPC.exe 0x00428ff4.
    return new CETC_MANI_USE_COUNT();
}

EtcMani* Create_EtcManiEnchantArtifact() {
    // Current L2NPC.exe 0x00429028.
    return new EtcManiEnchantArtifact();
}

namespace {

#if defined(L2_BUILD_CACHED)
// Current Cached.exe 0x004c7258 registers subtypes 0..13 here. The seven
// shared registrations below complete 0..20. Current L2NPC registers only
// those seven outgoing variants, hence this process-specific block.
const bool isReg_EtcManiUpdatePledgeNameValue =
    Singleton<EtcManiFactory>::inst().Register(ETC_MANI_UPDATE_PLEDGE_NAMEVALUE, Create_EtcManiUpdatePledgeNameValue);
const bool isReg_EtcManiUpdateUserEventTime =
    Singleton<EtcManiFactory>::inst().Register(ETC_MANI_UPDATE_USER_EVENT_TIME, Create_EtcManiUpdateUserEventTime);
const bool isReg_EtcManiUpdateUserItemBaseAttribute = Singleton<EtcManiFactory>::inst().Register(
    ETC_MANI_UPDATE_USER_ITEM_BASE_ATTRIBUTE, Create_EtcManiUpdateUserItemBaseAttribute
);
const bool isReg_EtcManiRollbackUserItemBaseAttribute = Singleton<EtcManiFactory>::inst().Register(
    ETC_MANI_ROLLBACK_USER_ITEM_BASE_ATTRIBUTE, Create_EtcManiRollbackUserItemBaseAttribute
);
const bool isReg_EtcManiCreateItemAuctionBid =
    Singleton<EtcManiFactory>::inst().Register(ETC_MANI_CREATE_ITEM_AUCTION_BID, Create_EtcManiCreateItemAuctionBid);
const bool isReg_EtcManiWithdrawItemAuctionBid =
    Singleton<EtcManiFactory>::inst().Register(ETC_MANI_WITHDRAW_ITEM_AUCTION_BID, Create_EtcManiWithdrawItemAuctionBid);
const bool isReg_EtcManiGiveItemByCastleSiegeCount = Singleton<EtcManiFactory>::inst().Register(
    ETC_MANI_GIVE_ITEM_BY_CASTLE_SIEGE_COUNT, Create_EtcManiGiveItemByCastleSiegeCount
);
const bool isReg_EtcManiGiveItemOnAccount =
    Singleton<EtcManiFactory>::inst().Register(ETC_MANI_GIVE_ITEM_ON_ACCOUNT, Create_EtcManiGiveItemOnAccount);
const bool isReg_EtcManiUpdateUserItemCombination = Singleton<EtcManiFactory>::inst().Register(
    ETC_MANI_UPDATE_USER_ITEM_COMBINATION, Create_EtcManiUpdateUserItemCombination
);
const bool isReg_EtcManiEnsoul = Singleton<EtcManiFactory>::inst().Register(ETC_MANI_ENSOUL, Create_EtcManiEnsoul);
const bool isReg_EtcManiMultisellResult =
    Singleton<EtcManiFactory>::inst().Register(ETC_MANI_MULTISELL, Create_EtcManiMultisellResult);
const bool isReg_EtcManiSuccessfulItemAuctionBid =
    Singleton<EtcManiFactory>::inst().Register(ETC_MANI_SUCCESSFUL_ITEM_AUCTION_BID, Create_EtcManiSuccessfulItemAuctionBid);
const bool isReg_EtcManiCardUpDownGame =
    Singleton<EtcManiFactory>::inst().Register(ETC_MANI_CARD_UPDOWN_GAME, Create_EtcManiCardUpDownGame);
const bool isReg_EtcManiRemoveInzoneBindingByEvent = Singleton<EtcManiFactory>::inst().Register(
    ETC_MANI_REMOVE_INZONE_BINDING_BY_EVENT, Create_EtcManiRemoveInzoneBindingByEvent
);
#endif

// Current L2NPC.exe 0x00586b80-0x00586ca0 and current Cached.exe both contain
// these seven registration initializers.
const bool isReg_CETC_MANI_PLEDGE_MASTERY_SET =
    Singleton<EtcManiFactory>::inst().Register(ETC_MANI_PLEDGE_MASTERY_SET, Create_CETC_MANI_PLEDGE_MASTERY_SET);

const bool isReg_CETC_MANI_PLEDGE_MASTERY_RESET =
    Singleton<EtcManiFactory>::inst().Register(ETC_MANI_PLEDGE_MASTERY_RESET, Create_CETC_MANI_PLEDGE_MASTERY_RESET);

const bool isReg_CETC_MANI_POINT_UPDATE =
    Singleton<EtcManiFactory>::inst().Register(ETC_MANI_POINT_UPDATE, Create_CETC_MANI_POINT_UPDATE);

const bool isReg_CETC_MANI_PLEDGE_MISSION_REWARD =
    Singleton<EtcManiFactory>::inst().Register(ETC_MANI_PLEDGE_MISSION_REWARD, Create_CETC_MANI_PLEDGE_MISSION_REWARD);

const bool isReg_CETC_MANI_PLEDGE_CONTRIBUTION_REWARD = Singleton<EtcManiFactory>::inst().Register(
    ETC_MANI_PLEDGE_CONTRIBUTION_REWARD, Create_CETC_MANI_PLEDGE_CONTRIBUTION_REWARD
);

const bool isReg_CETC_MANI_USE_COUNT =
    Singleton<EtcManiFactory>::inst().Register(ETC_MANI_USE_COUNT, Create_CETC_MANI_USE_COUNT);

const bool isReg_EtcManiEnchantArtifact =
    Singleton<EtcManiFactory>::inst().Register(ETC_MANI_ENCHANT_ARTIFACT, Create_EtcManiEnchantArtifact);

} // namespace
