#include "ServerPacketEx.h"

#include "Alliance.h"
#include "AllianceDb.h"
#include "BossRecordDB.h"
#include "CAccount.h"
#include "CacheSocket.h"
#include "Cached.h"
#include "CastleDb.h"
#include "EventDataHelper.h"
#include "FileServerManager.h"
#include "FriendsDb.h"
#include "Item.h"
#include "Pet.h"
#include "Pledge.h"
#include "PledgeAnnounceDBHandlerImpl.h"
#include "PledgeContributionInCache.h"
#include "PledgeCrestDb.h"
#include "PledgeDb.h"
#include "ReservedAnnounce.h"
#include "SevenSignQuestDB.h"
#include "TimeAttackDB.h"
#include "User.h"
#include "UserDb.h"
#include "UserMacro.h"
#include "WareHouse.h"
#include "WareHouseType.h"

#include "L2Core/src/inc/Guard.h"
#include "L2Core/src/inc/GuardProfile.h"
#include "Shared/src/Runtime/DelimitedText.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/Win64Integer.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/CategoryDataContent.h"
#include "Shared/src/DBConn.h"
#include "Shared/src/EtcMani.h"
#include "Shared/src/Exception.h"
#include "Shared/src/GIP.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/PledgeAnnounceRegistry.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/SystemMail.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Vitality.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/IOCs.h"
#include "Shared/src/Log.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/proto/DBPacket.h"
#include "Shared/src/TextRuntime.h"
#include "Runtime.h"

#include <algorithm>
#include <array>
#include <optional>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// Current anonymous SendPacket's kept guard signature names this global
// enum and its machine code sign-extends a 16-bit value. These adjacent
// enumerator identifiers survive in the matching PDB while the current
// CacheD/L2Server machine pair fixes their numeric values.
enum RP_EX_PROTOCOL : std::int16_t {
    RP_LOAD_WEDDING_EX = 266,
    RP_EX_LOAD_FRIEND_DETAIL_INFO = 267,
    RP_EX_LOAD_DROIDNPC_APPEARANCE = 268,
    RP_EX_SIGNIN_PLEDGE = 269,
    RP_EX_LOAD_FACTION_POINT = 270,
    RP_EX_UPDATE_FACTION_POINT = 271,
    RP_EX_LOAD_BALTHUS_EVENT_USER_INFO = 272,
    RP_EX_LOAD_USER_INZONE_POINT = 273,
    RP_EX_WOLYMPIAD_LOAD_ALL_USER = 274,
    RP_EX_WOLYMPIAD_GET_TOP10 = 275,
    RP_EX_WOLYMPIAD_GET_MATCH_COUNT = 276,
    RP_EX_WOLYMPIAD_GET_ACHIEVEMENTS = 277,
    RP_EX_WOLYMPIAD_GET_MATCH_RESULT = 278,
    RP_EX_WOLYMPIAD_MATCHING_POINT_LOAD = 279,
    RP_EX_LOAD_ONE_DAY_REWARD_INFO = 280,
    RP_EX_LOAD_AUTO_SOULSHOT_UI = 281,
    RP_EX_LOAD_USER_PLEDGE_BONUS = 282,
    RP_EX_LOAD_USER_MONSTER_BOOK = 283,
    RP_EX_RESPONSE_UPGRADE_SYSTEM = 284,
    RP_EX_LOAD_CARD_UPDOWN_RECORD = 285,
    RP_EX_UPDATE_CARD_UPDOWN_RECORD = 286,
    RP_EX_SEND_SYSTEM_MESSAGE = 287,
    RP_EX_PLEDGE_MANIPULATE_ITEMS = 288,
    RP_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO = 289,
    RP_EX_PLEDGE_CONTRIBUTION_RANK = 290,
    RP_EX_PLEDGE_ITEM_LOAD = 291,
    RP_EX_PLEDGE_ITEM_SAVE = 292,
    RP_EX_PLEDGE_ITEM_DELETE = 293,
    RP_EX_PLEDGE_RAID_GET_INFO_LIST = 294,
    RP_EX_PLEDGE_RAID_INFO_UPDATE_REWARD_REASON = 295,
    RP_EX_PLEDGE_RAID_INZONE_LOAD = 296,
    RP_EX_PLEDGE_MISSION_LOAD = 297,
    RP_EX_PLEDGE_ANNOUNCE = 298,
    RP_EX_PLEDGE_ANNOUNCE_SET = 299,
    RP_EX_SEQUENCE_JOB = 300,
    RP_EX_BUILDER_COMMAND = 301,
    RP_EX_RESPONSE_LOCKED_ITEM = 302,
    RP_EX_LOAD_ITEM_REUSE_DELAY = 303,
    RP_EX_PLEDGE_MANIPULATE_ITEMS_NEW = 304,
    RP_EX_LOAD_RESERVATION_ANNOUNCE = 305,
    RP_EX_SHOW_RESERVATION_ANNOUNCE = 306,
    RP_EX_REFRASH_RESERVATION_ANNOUNCE = 307,
};

// Exact misspelling and owner recovered from current Cached.exe
// 0x00529378..0x005297ac. The target object captures legacy callback Send
// calls into ten pooled buffers before forwarding the sequence envelope.
class SequnceJobSocket final : public CSocket {
public:
    // Current Cached.exe constructor body 0x005294d0.
    SequnceJobSocket(std::uint64_t socket, int bufferSize) : CSocket(socket, bufferSize) {
        m_buffers[0] = DefaultIOBuffer::Alloc();
    }

    // Current complete destructor body 0x00529548. Scalar deleting wrapper
    // 0x005296dc.
    ~SequnceJobSocket() override {
        for (DefaultIOBuffer* buffer : m_buffers) {
            if (buffer)
                buffer->Release();
        }
    }

    void Send(const char* format, ...) override {
        L2_GUARD_FUNCTION();
        va_list arguments;
        va_start(arguments, format);
        SaveBufferV(format, arguments);
        va_end(arguments);
    }

    void SaveBufferV(const char* format, va_list arguments) {
        L2_GUARD_FUNCTION();

        while (m_currentIndex < static_cast<int>(m_buffers.size())) {
            DefaultIOBuffer* const buffer = m_buffers[static_cast<std::size_t>(m_currentIndex)];
            if (!buffer)
                return;
            va_list copy;
            va_copy(copy, arguments);
            ::VAssemble(
                reinterpret_cast<char*>(buffer->m_Buffer.data() + 4 + buffer->m_dwSize),
                0x4ff5 - static_cast<int>(buffer->m_dwSize), format, copy
            );
            va_end(copy);

            ++m_currentIndex;
            if (m_currentIndex >= static_cast<int>(m_buffers.size())) {
                break;
            }
            m_buffers[static_cast<std::size_t>(m_currentIndex)] = DefaultIOBuffer::Alloc();
        }
    }

    void SendSequnceJob(CSocket* socket, int jobId) {
        L2_GUARD_FUNCTION();
        if (m_status == 0) {
            socket->Send("chddd", 0xc6, RP_EX_SEQUENCE_JOB, jobId, 0, 0);
            return;
        }

        for (int index = 0; index <= m_currentIndex && index < static_cast<int>(m_buffers.size()); ++index) {
            DefaultIOBuffer* const buffer = m_buffers[static_cast<std::size_t>(index)];
            if (!buffer)
                continue;
            socket->Send(
                "chdddb", 0xc6, RP_EX_SEQUENCE_JOB, jobId, m_status, m_lengths[static_cast<std::size_t>(index)],
                static_cast<int>(buffer->m_dwSize), buffer->m_Buffer.data()
            );
        }
    }

private:
    std::array<DefaultIOBuffer*, 10> m_buffers{}; // +0x188
    std::array<int, 10> m_lengths{}; // +0x1d8
    int m_currentIndex = 0; // +0x200
    int m_status = 0; // +0x204
};


namespace {

// Current DAT_02a1e4d0 is constructed and destroyed as one
// CIOCriticalSection. Only RequestBuyBeautyItem enters it, around the two
// beauty-purchase SQL writes. Its original source identifier is not present.
CIOCriticalSection g_beautyPurchaseCriticalSection;

// Current process-global dword at 0x0083d2c8. RequestSavePCCafePoint type 4
// is its only current writer and no current reader or source identifier
// survives, so keep the observable storage private.
int g_unnamedPCCafeType4Value = 0;

// FUN_0054df9c binds its second input through the current enum32 DBConn
// specialization. The enum's source spelling is absent, so keep that ABI
// distinction local instead of publishing a guessed type name.
enum class BeautyTypeParameter : int {};

void SendPledgeManipulateItemsSystemMessage(int userDbId, short messageId, int itemType) {
    // Semantic name for current anonymous helper 0x0052d358. The payload's
    // leading 3 is the shipping system-message item parameter type.
    CPacketHelperEx<20480> parameters;
    parameters.Assemble("cd", 3, itemType);
    CSocket* const activeSocket = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (!activeSocket)
        return;
    activeSocket->Send(
        "cdhcdb", 0xbf, userDbId, static_cast<int>(messageId), 1, parameters.GetLength(), parameters.GetLength(),
        parameters.GetPacket()
    );
}

void SendPledgeManipulateItemsFailure(CSocket* socket, int pledgeId, int userDbId, int atomicJobId, int errorCode) {
    // Semantic name for current anonymous helper 0x0052d408.
    socket->Send(
        "chddddddd", 0xc6, RP_EX_PLEDGE_MANIPULATE_ITEMS, GetCacheDConfiguration().worldId, pledgeId, userDbId, atomicJobId,
        errorCode, 0, 0
    );
}

void SendPledgeManipulateItemsSuccess(
    CSocket* socket,
    int pledgeId,
    int userDbId,
    int atomicJobId,
    int etcManiCount,
    CPacketHelperEx<20480>& etcManiPacket,
    const std::vector<CreateItemStruct>& deletedItems
) {
    // Semantic name for current anonymous helper 0x00542a38.
    CPacketHelperEx<20480> deletedItemPacket;
    ItemUtil::AssembleItemList(deletedItems, deletedItemPacket);
    socket->Send(
        "chdddddddbb", 0xc6, RP_EX_PLEDGE_MANIPULATE_ITEMS, GetCacheDConfiguration().worldId, pledgeId, userDbId,
        atomicJobId, 0, etcManiCount, static_cast<int>(deletedItems.size()), etcManiPacket.GetLength(),
        etcManiPacket.GetPacket(), deletedItemPacket.GetLength(), deletedItemPacket.GetPacket()
    );
}

void SendPledgeManipulateItemsNewFailure(CSocket* socket, int pledgeId, int userDbId, int atomicJobId, int errorCode) {
    // Semantic name for current anonymous helper 0x0052d45c.
    socket->Send(
        "chddddddd", 0xc6, RP_EX_PLEDGE_MANIPULATE_ITEMS_NEW, GetCacheDConfiguration().worldId, pledgeId, userDbId,
        atomicJobId, errorCode, 0, 0
    );
}

void SendPledgeManipulateItemsNewSuccess(
    CSocket* socket,
    int pledgeId,
    int userDbId,
    int atomicJobId,
    int etcManiCount,
    CPacketHelperEx<20480>& etcManiPacket,
    const std::vector<CreateItemStruct>& changedItems
) {
    // Semantic name for current anonymous helper 0x00542ba4.
    CPacketHelperEx<20480> changedItemPacket;
    ItemUtil::AssembleItemList(changedItems, changedItemPacket);
    socket->Send(
        "chdddddddbb", 0xc6, RP_EX_PLEDGE_MANIPULATE_ITEMS_NEW, GetCacheDConfiguration().worldId, pledgeId, userDbId,
        atomicJobId, 0, etcManiCount, static_cast<int>(changedItems.size()), etcManiPacket.GetLength(),
        etcManiPacket.GetPacket(), changedItemPacket.GetLength(), changedItemPacket.GetPacket()
    );
}

void DestroyPledgeManipulateEtcManis(const std::vector<EtcMani*>& etcManis) {
    // The target's specialized xvector<EtcMani*> destructor at 0x004c2694
    // calls virtual Destroy on every element before freeing its storage.
    for (EtcMani* etcMani : etcManis) {
        if (etcMani)
            etcMani->Destroy();
    }
}

void SaveBeautyPurchaseList(DBConn& connection, int characterId, int beautyType, int beautyUpperId, int beautyId) {
    // Semantic name for current helper 0x0054df9c. Its complete body is the
    // four ordered bindings below followed by an ignored execution result.
    constexpr const wchar_t* statement = L"{Call lin_SaveBeautyPurchaseList(?,?,?,?)}";
    const BeautyTypeParameter typeParameter = static_cast<BeautyTypeParameter>(beautyType);
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&typeParameter);
    connection.BindParameter(&beautyUpperId);
    connection.BindParameter(&beautyId);
    connection.ExecutePrepared(statement);
}

void WriteBeautyItemAudit(
    const CUser& user, int logId, int beautyType, int beautyId, std::int64_t adenaPrice, std::int64_t beautyCoinPrice
) {
    // Current RequestBuyBeautyItem/RequestResetBeautyItem call the shared
    // formatter at 0x004efa60 with this exact 26-field layout.
    const std::u16string characterName = std::u16string(user.GetCharName());
    const std::u16string accountName = std::u16string(user.GetAccountName());
    CLogFormatter formatter;
    formatter.WriteLog(
        logId, user.GetId(), user.GetAccountID(), 0, 0, user.GetX(), user.GetY(), user.GetZ(), nullptr, nullptr, nullptr,
        user.GetClass(), user.GetRace(), user.GetGender(), user.GetLevel(), beautyType, beautyId, 0, adenaPrice,
        beautyCoinPrice, 0, characterName.c_str(), accountName.c_str(), nullptr, nullptr, 0
    );
}

class ClearOneEventTimeOperation {
public:
    ClearOneEventTimeOperation(int eventType, int eventVariable) noexcept :
        m_eventType(eventType), m_eventVariable(eventVariable) {}

    bool Do(CAccount* account) const {
        account->SetEventTime(0, m_eventType, m_eventVariable, false);
        return true;
    }

private:
    int m_eventType;
    int m_eventVariable;
};

bool BrLocalTime32(std::int32_t epochSeconds, std::tm& result) noexcept {
#if defined(_WIN32)
    const __time32_t value = static_cast<__time32_t>(epochSeconds);
    return ::_localtime32_s(&result, &value) == 0;
#else
    const std::time_t value = static_cast<std::time_t>(epochSeconds);
    return ::localtime_r(&value, &result) != nullptr;
#endif
}

void WriteTrainingZoneStartAudit(const CUser& user, int x, int y, int z, int remainingMinutes) {
    // Current RequestBrStartTrainingZone call at 0x005850e7.
    const std::u16string characterName = std::u16string(user.GetCharName());
    const std::u16string accountName = std::u16string(user.GetAccountName());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x1fb7, user.GetId(), user.GetAccountID(), 0, 0, x, y, z, nullptr, nullptr, nullptr, user.GetRace(),
        user.GetGender(), user.GetClass(), user.GetLevel(), remainingMinutes, 0, 0, 0, 0, 0, characterName.c_str(),
        accountName.c_str(), nullptr, nullptr, 0
    );
}

void WriteTrainingZoneEndAudit(
    const CUser& user, int trainingMinutes, int remainingMinutes, int x, int y, int z, int endReason
) {
    // Current RequestBrEndTrainingZone call at 0x005857ea.
    const std::u16string characterName = std::u16string(user.GetCharName());
    const std::u16string accountName = std::u16string(user.GetAccountName());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x1fb8, user.GetId(), user.GetAccountID(), 0, 0, user.GetX(), user.GetY(), user.GetZ(), nullptr, nullptr, nullptr,
        user.GetRace(), user.GetGender(), user.GetClass(), user.GetLevel(), trainingMinutes, remainingMinutes, x, y, z,
        endReason, characterName.c_str(), accountName.c_str(), nullptr, nullptr, 0
    );
}

void WriteAccountUserStampAudit(const CUser& user, int currentStamp, int maximumStamp, int previousStampTime, int now) {
    // Current RequestAccountUserStamp call at 0x005872b0. This handler
    // places the character/account strings in formatter fields
    // 22/23 instead of the ordinary 21/22 pair. Preserve that exact record.
    const std::u16string characterName = std::u16string(user.GetCharName());
    const std::u16string accountName = std::u16string(user.GetAccountName());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x1fbf, user.GetId(), user.GetAccountID(), 0, 0, user.GetX(), user.GetY(), user.GetZ(), nullptr, nullptr, nullptr,
        user.GetRace(), user.GetGender(), user.GetClass(), user.GetLevel(), currentStamp, maximumStamp, previousStampTime,
        now, 0, 0, nullptr, characterName.c_str(), accountName.c_str(), nullptr, 0
    );
}

std::u16string ServerPacketAuditDecimal(std::int64_t value) {
    char encoded[32]{};
    const auto converted = std::to_chars(encoded, encoded + sizeof(encoded), value);
    std::u16string result;
    result.reserve(static_cast<std::size_t>(converted.ptr - encoded));
    for (const char* cursor = encoded; cursor != converted.ptr; ++cursor) {
        result.push_back(static_cast<char16_t>(*cursor));
    }
    return result;
}

void WriteDeletedSkillListAudit(const CUser& user, int skillId, int previousPackedLevel) {
    // Current RequestDeleteAndSaveSkillList 0x0053e890 uses the ordinary
    // CLogFormatter owner for record 0x192.
    const std::u16string characterName = std::u16string(user.GetCharName());
    const std::u16string accountName = std::u16string(user.GetAccountName());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x192, user.GetId(), user.GetAccountID(), 0, 0, user.GetX(), user.GetY(), user.GetZ(), nullptr, nullptr, nullptr,
        user.GetRace(), user.GetGender(), user.GetClass(), user.GetLevel(), skillId, user.GetBuilder(), previousPackedLevel,
        0, 0, 0, characterName.c_str(), accountName.c_str(), nullptr, nullptr, 0
    );
}

void WriteSavedSkillListAudit(const CUser& user, int skillId, int packedLevel) {
    // The target writes record 0x191 directly through route 6. Its two
    // trailing numeric fields and final item field are empty, not numeric
    // zero, so keep the exact sparse CSV instead of routing this through
    // CLogFormatter.
    std::array<std::u16string, 26> fields{};
    fields[0] = ServerPacketAuditDecimal(0x191);
    fields[1] = ServerPacketAuditDecimal(user.GetId());
    fields[2] = ServerPacketAuditDecimal(user.GetAccountID());
    fields[5] = ServerPacketAuditDecimal(user.GetX());
    fields[6] = ServerPacketAuditDecimal(user.GetY());
    fields[7] = ServerPacketAuditDecimal(user.GetZ());
    fields[11] = ServerPacketAuditDecimal(user.GetRace());
    fields[12] = ServerPacketAuditDecimal(user.GetGender());
    fields[13] = ServerPacketAuditDecimal(user.GetClass());
    fields[14] = ServerPacketAuditDecimal(user.GetLevel());
    fields[15] = ServerPacketAuditDecimal(skillId);
    fields[16] = ServerPacketAuditDecimal(packedLevel);
    fields[17] = ServerPacketAuditDecimal(user.GetSP());
    fields[18] = ServerPacketAuditDecimal(user.GetBuilder());
    fields[21] = std::u16string(user.GetCharName());
    fields[22] = std::u16string(user.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteBrGiftPostAudit(const CUser& sender, const CUser& receiver, int postId, int itemCount) {
    // Current RequestBrSendShopGiftMail call 0x0057e8dc emits record 0x1f71
    // with three empty strings and zero for all monetary fields.
    const std::u16string senderName = std::u16string(sender.GetCharName());
    const std::u16string senderAccount = std::u16string(sender.GetAccountName());
    const std::u16string receiverName = std::u16string(receiver.GetCharName());
    const std::u16string receiverAccount = std::u16string(receiver.GetAccountName());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x1f71, sender.GetId(), sender.GetAccountID(), receiver.GetId(), receiver.GetAccountID(), sender.GetX(),
        sender.GetY(), sender.GetZ(), u"", u"", u"", sender.GetClass(), sender.GetRace(), sender.GetGender(),
        sender.GetLevel(), postId, 0, itemCount, 0, 0, 0, senderName.c_str(), senderAccount.c_str(), receiverName.c_str(),
        receiverAccount.c_str(), 0
    );
}

void WriteNRMemoAudit(const CUser& user, int questId, int journal) {
    // Original helper name is absent. Current RequestSaveNRMemo 0x00558014
    // calls adapter 0x004d4fa8 with this exact sparse route-6 record. The
    // index-marker field is zero and the final item field is
    // empty instead of CLogFormatter's numeric zero.
    std::array<std::u16string, 26> fields{};
    fields[0] = ServerPacketAuditDecimal(0x32e);
    fields[1] = ServerPacketAuditDecimal(user.GetId());
    fields[2] = ServerPacketAuditDecimal(user.GetAccountID());
    fields[5] = ServerPacketAuditDecimal(user.GetX());
    fields[6] = ServerPacketAuditDecimal(user.GetY());
    fields[7] = ServerPacketAuditDecimal(user.GetZ());
    fields[11] = ServerPacketAuditDecimal(0);
    fields[12] = ServerPacketAuditDecimal(questId);
    fields[13] = ServerPacketAuditDecimal(journal);
    fields[21] = std::u16string(user.GetCharName());
    fields[22] = std::u16string(user.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WritePremiumDeliveryAudit(
    int recipientCharacterId,
    int recipientAccountId,
    int buyerCharacterId,
    int buyerAccountId,
    std::int64_t warehouseNo,
    int itemType,
    std::int64_t itemAmount,
    std::u16string_view recipientName,
    std::u16string_view buyerName
) {
    // Current RequestSavePremiumItemForChar/Server emit the same sparse
    // route-0xc record 0x3e0. The two names occupy fields
    // 21/23 instead of the ordinary character/account 21/22 pair.
    std::array<std::u16string, 26> fields{};
    fields[0] = ServerPacketAuditDecimal(0x3e0);
    fields[1] = ServerPacketAuditDecimal(recipientCharacterId);
    fields[2] = ServerPacketAuditDecimal(recipientAccountId);
    fields[3] = ServerPacketAuditDecimal(buyerCharacterId);
    fields[4] = ServerPacketAuditDecimal(buyerAccountId);
    fields[11] = ServerPacketAuditDecimal(warehouseNo);
    fields[12] = ServerPacketAuditDecimal(itemType);
    fields[13] = ServerPacketAuditDecimal(itemAmount);
    fields[21].assign(recipientName);
    fields[23].assign(buyerName);
    runtime::process::WriteLogFormatterLine(0xc, runtime::JoinFields(fields, u','));
}

bool ExecuteSaveUserPremiumItem(
    DBConn& connection,
    const long long& warehouseNo,
    const int& buyerAccountId,
    const int& buyerCharacterId,
    const wchar_t* buyerCharacterName,
    const int& recipientAccountId,
    const int& recipientCharacterId,
    const wchar_t* recipientCharacterName,
    const int& itemType,
    const long long& itemAmount
) {
    constexpr const wchar_t* statement = L"{Call lin_SaveUserPremiumItem (?,?,?,?,?,?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&warehouseNo);
    connection.BindParameter(&buyerAccountId);
    connection.BindParameter(&buyerCharacterId);
    connection.BindParameter(buyerCharacterName);
    connection.BindParameter(&recipientAccountId);
    connection.BindParameter(&recipientCharacterId);
    connection.BindParameter(recipientCharacterName);
    connection.BindParameter(&itemType);
    connection.BindParameter(&itemAmount);
    return connection.ExecutePrepared(statement);
}

bool DeleteItemDirectly(int itemId) {
    int deleted = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&deleted);
    constexpr const wchar_t* statement = L"{Call lin_DeleteItem (?, NULL)}";
    connection.Prepare(statement);
    connection.BindParameter(&itemId);
    return connection.ExecutePrepared(statement) && connection.Fetch() && deleted == 1;
}

void LogPeriodicPremiumItemDeletion(unsigned int accountId, int characterId, int itemId, unsigned int itemType) {
    const long long amountBefore = 1;
    const long long amountAfter = 0;
    const int reason = 3;
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_LogForPremiumItemDelete (?,?,?,?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&accountId);
    connection.BindParameter(&characterId);
    connection.BindParameter(&itemId);
    connection.BindParameter(&amountBefore);
    connection.BindParameter(&amountAfter);
    connection.BindParameter(&reason);
    connection.BindParameter(&itemType);
    connection.ExecutePrepared(statement);
}

// Current Cached.exe 0x00531e8c. The typed request's virtual Decode receives
// the same fixed 0x4000-byte stream view used by the shipping helper.
bool DecodePacket(network::IPacket& typedPacket, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    network::Stream stream(const_cast<unsigned char*>(packet), 0x4000);
    return typedPacket.Decode(&stream);
}

// Current Cached.exe 0x005335b4.
void SendPacket(CSocket* socket, RP_EX_PROTOCOL protocol, const network::IPacket& typedPacket) {
    L2_GUARD_FUNCTION();
    if (!socket)
        return;

    CPacketHelperEx<20480> packet;
    network::Stream stream(reinterpret_cast<unsigned char*>(packet.GetPacket()), packet.GetBufferSize());
    if (!typedPacket.Encode(&stream))
        return;

    const int payloadLength = static_cast<int>(stream.GetCursor() - stream.GetBuffer());
    socket->Send("chb", 0xc6, static_cast<int>(protocol), payloadLength, stream.GetBuffer());
}

// Current unnamed helper 0x0052d2e8 has four semantic integer parameters but
// uses the five-integer format "chddddd". The paired current L2Server decoder
// consumes only the four semantic fields. Windows therefore transmitted one
// indeterminate stack word after them. Preserve the frame width without
// reproducing undefined variadic access by making that ignored word zero.
void SendSigninPledgeReply(CSocket* socket, int userServerId, int pledgeServerId, PledgeType pledgeType, int result) {
    if (!socket)
        return;
    socket->Send(
        "chddddd", 0xc6, RP_EX_SIGNIN_PLEDGE, userServerId, pledgeServerId, static_cast<int>(pledgeType), result, 0
    );
}

// Current unnamed helper 0x0054e370. Its source identifier is unavailable.
// both item-auction branches use this exact prepare/bind/execute sequence.
bool ExecuteItemAuctionInfoUpdate(
    DBConn& connection, const wchar_t* statement, int auctionId, const protocol::ItemAuctionInfo& item
) {
    connection.Prepare(statement);
    connection.BindParameter(&auctionId);
    connection.BindParameter(&item.itemType);
    connection.BindParameter(&item.itemAmount);
    connection.BindParameter(&item.initPrice);
    return connection.ExecutePrepared(statement);
}

// Current Cached.exe 0x0054d8d0. No usable symbol or guard keeps the
// original helper name. Its complete behavior is the shared six-parameter
// PCCafe Prepare/bind/execute sequence used by request IDs 77 and 78.
bool ExecutePCCafeCouponProcedure(
    DBConn& connection,
    const wchar_t* statement,
    int coupon1,
    int coupon2,
    int coupon3,
    int coupon4,
    std::uint32_t userDbId,
    std::uint32_t worldId
) {
    connection.Prepare(statement);
    connection.BindParameter(&coupon1);
    connection.BindParameter(&coupon2);
    connection.BindParameter(&coupon3);
    connection.BindParameter(&coupon4);
    connection.BindParameter(&userDbId);
    connection.BindParameter(&worldId);
    return connection.ExecutePrepared(statement);
}

// Current Cached.exe 0x00559690. No original symbol or guard name survives.
// the semantic name describes its complete three-operation SQL dispatch.
void PersistDominionRegistryChange(int operation, int dominionId, int registryType, int registryId) {
    switch (operation) {
    case 0: {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_SaveDominionRegistry (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&dominionId);
        connection.BindParameter(&registryType);
        connection.BindParameter(&registryId);
        if (!connection.ExecutePrepared(statement)) {
            // Log.AddT(
            //     LOG_ERROR, L"[%s][%d] Fail to save dominion registry. dominion[%d]", L"ServerPacketEx.cpp", 0x16e3,
            //     dominionId
            // )
            // L2_LOG_ERROR(Log, L"[%s][%d] Fail to save dominion registry. dominion[%d]", L"ServerPacketEx.cpp", 0x16e3,
            //     dominionId
            // )
            L2_LOG_ERROR(Log,
                "[dominion] Database request to save registration failed: dominion %d, registration type %d, entry %d.",
                dominionId, registryType, registryId
            );
        }
        return;
    }
    case 1: {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_ResetDominionRegistry (?)}";
        connection.Prepare(statement);
        connection.BindParameter(&dominionId);
        if (!connection.ExecutePrepared(statement)) {
            // Log.AddT(
            //     LOG_ERROR, L"[%s][%d] Fail to reset dominion registry. dominion[%d]", L"ServerPacketEx.cpp", 0x16ed,
            //     dominionId
            // )
            // L2_LOG_ERROR(Log, L"[%s][%d] Fail to reset dominion registry. dominion[%d]", L"ServerPacketEx.cpp", 0x16ed,
            //     dominionId
            // )
            L2_LOG_ERROR(Log, "[dominion] Database request to reset registrations for dominion %d failed.", dominionId
            );
        }
        return;
    }
    case 2: {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_DeleteDominionRegistry (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&dominionId);
        connection.BindParameter(&registryType);
        connection.BindParameter(&registryId);
        if (!connection.ExecutePrepared(statement)) {
            // Log.AddT(
            //     LOG_ERROR, L"[%s][%d] Fail to delete dominion registry. dominion[%d]", L"ServerPacketEx.cpp", 0x16f7,
            //     dominionId
            // )
            // L2_LOG_ERROR(Log, L"[%s][%d] Fail to delete dominion registry. dominion[%d]", L"ServerPacketEx.cpp", 0x16f7,
            //     dominionId
            // )
            L2_LOG_ERROR(Log,
                "[dominion] Database request to delete registration failed: dominion %d, registration type %d, entry %d.",
                dominionId, registryType, registryId
            );
        }
        return;
    }
    default:
        return;
    }
}

// Local carrier for the exact 16-column lin_LoadPostByPostId result. It is
// source-only state in ServerPacketEx.cpp, not a target ABI structure.
struct PostRecord {
    int senderId = 0;
    int receiverId = 0;
    int trade = 0;
    std::array<char16_t, 61> title{};
    std::array<char16_t, 1001> contents{};
    int process = 0;
    int returnable = 0;
    int returned = 0;
    int senderDeleted = 0;
    int receiverDeleted = 0;
    int sentTime = 0;
    int itemCount = 0;
    long long tradeMoney = 0;
    long long postFee = 0;
    int senderType = 0;
    int recordedId = 0;
};

bool LoadPostByPostId(DBConn& connection, int postId, PostRecord& post) {
    connection.Bind(&post.senderId);
    connection.Bind(&post.receiverId);
    connection.Bind(&post.trade);
    connection.Bind(post.title.data(), 0x7a);
    connection.Bind(post.contents.data(), 0x7d2);
    connection.Bind(&post.process);
    connection.Bind(&post.returnable);
    connection.Bind(&post.returned);
    connection.Bind(&post.senderDeleted);
    connection.Bind(&post.receiverDeleted);
    connection.Bind(&post.sentTime);
    connection.Bind(&post.itemCount);
    connection.Bind(&post.tradeMoney);
    connection.Bind(&post.postFee);
    connection.Bind(&post.senderType);
    connection.Bind(&post.recordedId);
    constexpr const wchar_t* statement = L"{Call lin_LoadPostByPostId (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&postId);
    return connection.ExecutePrepared(statement) && connection.Fetch();
}

bool ExecutePostIdStatement(const wchar_t* statement, int postId) {
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Prepare(statement);
    connection.BindParameter(&postId);
    return connection.ExecutePrepared(statement);
}

std::array<wchar_t, 32> CurrentCommissionTimestamp() {
    // All three current commission handlers use GetLocalTime followed by
    // wsprintfW("%d-%d-%d %d:%d:%d") into a local ServerPacketEx.cpp
    // buffer. Keep the non-zero-padded representation used by SQL Server.
    const runtime::SystemTime now = runtime::GetLocalTimeNow();
    std::array<wchar_t, 32> result{};
    std::swprintf(
        result.data(), result.size(), L"%d-%d-%d %d:%d:%d", static_cast<int>(now.wYear), static_cast<int>(now.wMonth),
        static_cast<int>(now.wDay), static_cast<int>(now.wHour), static_cast<int>(now.wMinute), static_cast<int>(now.wSecond)
    );
    return result;
}

// Current unnamed ServerPacketEx.cpp helper 0x0054cccc. The current caller
// has fifteen inputs: the older donor's trailing discount_type parameter is
// gone, while item_classid remains the final input.
bool ExecuteRegisterCommission(
    DBConn& connection,
    std::int64_t& commissionId,
    int characterId,
    const wchar_t* itemName,
    std::int64_t itemPrice,
    int commissionType,
    int firstType,
    std::int64_t commissionFee,
    int expiredType,
    int expirationTime,
    const wchar_t* characterName,
    int itemId,
    int itemGrade,
    int itemGeneral,
    const wchar_t* registerTime,
    int itemClassId
) {
    const int buyerId = 0;
    const long expirationTimeAsDouble = static_cast<long>(expirationTime);
    constexpr const wchar_t* statement = L"{Call lin_RegisterCommission ( ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)}";
    connection.Bind(&commissionId);
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&buyerId);
    connection.BindParameter(itemName);
    connection.BindParameter(&itemPrice);
    connection.BindParameter(&commissionType);
    connection.BindParameter(&firstType);
    connection.BindParameter(&commissionFee);
    connection.BindParameter(&expiredType);
    connection.BindParameter(&expirationTimeAsDouble);
    connection.BindParameter(characterName);
    connection.BindParameter(&itemId);
    connection.BindParameter(&itemGrade);
    connection.BindParameter(&itemGeneral);
    connection.BindParameter(registerTime);
    connection.BindParameter(&itemClassId);
    return connection.ExecutePrepared(statement) && connection.Fetch();
}

// Current unnamed ServerPacketEx.cpp helper 0x0054cdcc. sent_time uses the
// target Win64-long/SQL_DOUBLE binding even though the stored epoch is int32.
bool ExecuteCreateCommissionPost(
    DBConn& connection,
    int& postId,
    int senderId,
    int receiverId,
    const char16_t* title,
    const char16_t* contents,
    int itemCount,
    int mailType,
    int recordedId
) {
    const int trade = 0;
    const int process = 0;
    const int returnable = 0;
    const int senderDeleted = 0;
    const int receiverDeleted = 0;
    const long sentTimeAsDouble = static_cast<long>(runtime::crt::Time32Now());
    const std::int64_t tradeMoney = 0;
    const std::int64_t postFee = 0;
    constexpr const wchar_t* statement = L"{Call lin_CreatePost (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)}";
    connection.Bind(&postId);
    connection.Prepare(statement);
    connection.BindParameter(&senderId);
    connection.BindParameter(&receiverId);
    connection.BindParameter(&trade);
    connection.BindParameter(title);
    connection.BindParameter(contents);
    connection.BindParameter(&process);
    connection.BindParameter(&returnable);
    connection.BindParameter(&senderDeleted);
    connection.BindParameter(&receiverDeleted);
    connection.BindParameter(&sentTimeAsDouble);
    connection.BindParameter(&itemCount);
    connection.BindParameter(&tradeMoney);
    connection.BindParameter(&postFee);
    connection.BindParameter(&mailType);
    connection.BindParameter(&recordedId);
    return connection.ExecutePrepared(statement) && connection.Fetch();
}

// Current unnamed DB helper 0x00483dec.
bool ExecuteRegisterPostCommissionItemData(
    DBConn& connection, int postId, int itemClassId, int enchant, const std::array<int, ATTRT_MAX>& attributes
) {
    constexpr const wchar_t* statement = L"{Call lin_RegisterPostCommissionItemData (?, ?, ?, ?, ?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&postId);
    connection.BindParameter(&itemClassId);
    connection.BindParameter(&enchant);
    for (const int& attribute : attributes) {
        connection.BindParameter(&attribute);
    }
    return connection.ExecutePrepared(statement);
}

void WriteDeletePostLog(const CUserSP& receiver, const CUserSP& sender, int postId, int deleteAction, int requestReason) {
    // RequestDeletePost calls current LogFormatter.cpp 0x004efa60 with this
    // exact sparse 0x1f77 record after every successful state transition.
    const std::u16string receiverName = receiver ? std::u16string(receiver->GetCharName()) : std::u16string{};
    const std::u16string receiverAccount = receiver ? std::u16string(receiver->GetAccountName()) : std::u16string{};
    const std::u16string senderName = sender ? std::u16string(sender->GetCharName()) : std::u16string{};
    const std::u16string senderAccount = sender ? std::u16string(sender->GetAccountName()) : std::u16string{};

    CLogFormatter formatter;
    formatter.WriteLog(
        0x1f77, receiver ? receiver->GetId() : 0, receiver ? receiver->GetAccountID() : 0, sender ? sender->GetId() : 0,
        sender ? sender->GetAccountID() : 0, receiver ? receiver->GetX() : 0, receiver ? receiver->GetY() : 0,
        receiver ? receiver->GetZ() : 0, u"", u"", u"", receiver ? receiver->GetClass() : 0,
        receiver ? receiver->GetRace() : 0, receiver ? receiver->GetGender() : 0, receiver ? receiver->GetLevel() : 0,
        postId, deleteAction, requestReason, 0, 0, 0, receiverName.c_str(), receiverAccount.c_str(), senderName.c_str(),
        senderAccount.c_str(), 0
    );
}

void WritePostLog(
    const CUserSP& sender,
    const CUserSP& receiver,
    int postId,
    int trade,
    int itemCount,
    std::int64_t postFee,
    std::int64_t remainingAdena,
    std::int64_t tradeMoney,
    int dailyRemaining
) {
    // Current RequestWritePost calls LogFormatter.cpp 0x004efa60 with record
    // 0x1f71 immediately after lin_CreatePost succeeds. A system sender is
    // represented by the two literal names below. It is not a synthetic user.
    const bool systemSender = !sender;
    const std::u16string senderName =
        systemSender ? std::u16string(u"SenderCharName") : std::u16string(sender->GetCharName());
    const std::u16string senderAccount =
        systemSender ? std::u16string(u"SenderAccountName") : std::u16string(sender->GetAccountName());
    const std::u16string receiverName = receiver ? std::u16string(receiver->GetCharName()) : std::u16string{};
    const std::u16string receiverAccount = receiver ? std::u16string(receiver->GetAccountName()) : std::u16string{};
    const std::u16string dailyCount = systemSender ? std::u16string{} : ServerPacketAuditDecimal(dailyRemaining);

    CLogFormatter formatter;
    formatter.WriteLog(
        0x1f71, systemSender ? 0 : sender->GetId(), systemSender ? 0 : sender->GetAccountID(),
        receiver ? receiver->GetId() : 0, receiver ? receiver->GetAccountID() : 0, systemSender ? 0 : sender->GetX(),
        systemSender ? 0 : sender->GetY(), systemSender ? 0 : sender->GetZ(), u"", u"", dailyCount.c_str(),
        systemSender ? 0 : sender->GetClass(), systemSender ? 0 : sender->GetRace(), systemSender ? 0 : sender->GetGender(),
        systemSender ? 0 : sender->GetLevel(), postId, trade, itemCount, postFee, remainingAdena, tradeMoney,
        senderName.c_str(), senderAccount.c_str(), receiverName.c_str(), receiverAccount.c_str(), 0
    );
}

void WriteReceivePostLog(
    int logId,
    const CUserSP& receiver,
    const CUserSP& sender,
    int postId,
    int itemCount,
    std::int64_t requiredMoney,
    std::int64_t recipientAdenaAmount,
    std::int64_t transferredAdena
) {
    // Current RequestReceivePost emits record 0x1f74 after a successful
    // warehouse transaction and repeats the same payload as 0x2a8 for
    // sender type 13. Receiver remains the actor in both records.
    const std::u16string receiverName = std::u16string(receiver->GetCharName());
    const std::u16string receiverAccount = std::u16string(receiver->GetAccountName());
    const std::u16string senderName = std::u16string(sender->GetCharName());
    const std::u16string senderAccount = std::u16string(sender->GetAccountName());

    CLogFormatter formatter;
    formatter.WriteLog(
        logId, receiver->GetId(), receiver->GetAccountID(), sender->GetId(), sender->GetAccountID(), receiver->GetX(),
        receiver->GetY(), receiver->GetZ(), u"", u"", u"", receiver->GetClass(), receiver->GetRace(), receiver->GetGender(),
        receiver->GetLevel(), postId, requiredMoney != 0, itemCount, recipientAdenaAmount, transferredAdena, requiredMoney,
        receiverName.c_str(), receiverAccount.c_str(), senderName.c_str(), senderAccount.c_str(), 0
    );
}

void WriteRejectPostLog(const CUserSP& receiver, const CUserSP& sender, int postId, int requestContext, int requestValue) {
    // Current RequestRejectPost record 0x1f76 keeps the receiver as actor and
    // the original sender as target. Missing cached users contribute zeros
    // and empty names. No portable stand-in user is created.
    const std::u16string receiverName = receiver ? std::u16string(receiver->GetCharName()) : std::u16string{};
    const std::u16string receiverAccount = receiver ? std::u16string(receiver->GetAccountName()) : std::u16string{};
    const std::u16string senderName = sender ? std::u16string(sender->GetCharName()) : std::u16string{};
    const std::u16string senderAccount = sender ? std::u16string(sender->GetAccountName()) : std::u16string{};

    CLogFormatter formatter;
    formatter.WriteLog(
        0x1f76, receiver ? receiver->GetId() : 0, receiver ? receiver->GetAccountID() : 0, sender ? sender->GetId() : 0,
        sender ? sender->GetAccountID() : 0, receiver ? receiver->GetX() : 0, receiver ? receiver->GetY() : 0,
        receiver ? receiver->GetZ() : 0, u"", u"", u"", receiver ? receiver->GetClass() : 0,
        receiver ? receiver->GetRace() : 0, receiver ? receiver->GetGender() : 0, receiver ? receiver->GetLevel() : 0,
        postId, requestValue, requestContext, 0, 0, 0, receiverName.c_str(), receiverAccount.c_str(), senderName.c_str(),
        senderAccount.c_str(), 0
    );
}

void WriteCancelPostLog(const CUserSP& actor, int targetId, const CUserSP& target, int postId) {
    // Current RequestCancelPost record 0x1f72 uses the actual sender as the
    // mutation-time actor and the kept counterparty lookup
    // as the target.
    const std::u16string actorName = actor ? std::u16string(actor->GetCharName()) : std::u16string{};
    const std::u16string actorAccount = actor ? std::u16string(actor->GetAccountName()) : std::u16string{};
    const std::u16string targetName = target ? std::u16string(target->GetCharName()) : std::u16string{};
    const std::u16string targetAccount = target ? std::u16string(target->GetAccountName()) : std::u16string{};

    CLogFormatter formatter;
    formatter.WriteLog(
        0x1f72, actor ? actor->GetId() : 0, actor ? actor->GetAccountID() : 0, targetId, target ? target->GetAccountID() : 0,
        actor ? actor->GetX() : 0, actor ? actor->GetY() : 0, actor ? actor->GetZ() : 0, u"", u"", u"",
        actor ? actor->GetClass() : 0, actor ? actor->GetRace() : 0, actor ? actor->GetGender() : 0,
        actor ? actor->GetLevel() : 0, postId, 0, 0, 0, 0, 0, actorName.c_str(), actorAccount.c_str(), targetName.c_str(),
        targetAccount.c_str(), 0
    );
}

void WriteBirthdayPostLog(int logId, const CUserSP& user, int postId, int itemType, int itemCount) {
    // Current RequestSendBirthDayMail emits these calls inline. Record 0x1f71
    // identifies the post in num5. The mail-type-8 record 0x2334 identifies
    // the item type there and moves the post ID to num6.
    const std::u16string characterName = std::u16string(user->GetCharName());
    const std::u16string accountName = std::u16string(user->GetAccountName());
    const bool specialGiftRecord = logId == 0x2334;

    CLogFormatter formatter;
    formatter.WriteLog(
        logId, user->GetId(), user->GetAccountID(), user->GetId(), user->GetAccountID(), user->GetX(), user->GetY(),
        user->GetZ(), nullptr, nullptr, nullptr, user->GetClass(), user->GetRace(), user->GetGender(), user->GetLevel(),
        specialGiftRecord ? itemType : postId, specialGiftRecord ? postId : 0, itemCount, 0, 0, 0, characterName.c_str(),
        accountName.c_str(), characterName.c_str(), accountName.c_str(), 0
    );
}

void WriteDeletedBirthdayItemLog(const CUserSP& user, CItem& item) {
    // Current RequestDeleteBirthDayMail emits this complete record inline
    // after both the in-memory pop and lin_DeleteItem report success.
    std::array<char16_t, 256> shapeShiftAndVariation{};
    std::array<char16_t, 256> baseAttributeText{};
    std::array<char16_t, 256> ensoulText{};
    item.MakeShapeShiftAndVariationLogInfo(shapeShiftAndVariation.data());
    BaseAttribute baseAttribute(item.Attribute());
    baseAttribute.GetLogString(baseAttributeText.data());
    // item.MakeEnsoulLogInfo(ensoulText.data())
    item.MakeEnsoulLogInfo(ensoulText.data(), ensoulText.size());

    const std::u16string shape = std::u16string(shapeShiftAndVariation.data());
    const std::u16string attribute = std::u16string(baseAttributeText.data());
    const std::u16string ensoul = std::u16string(ensoulText.data());
    const std::u16string characterName = std::u16string(user->GetCharName());
    const std::u16string accountName = std::u16string(user->GetAccountName());

    CLogFormatter formatter;
    formatter.WriteLog(
        0x1f78, user->GetId(), user->GetAccountID(), user->GetId(), user->GetAccountID(), user->GetX(), user->GetY(),
        user->GetZ(), shape.c_str(), attribute.c_str(), ensoul.c_str(), user->GetClass(), user->GetRace(), user->GetGender(),
        user->GetLevel(), item.ItemID(), item.OwnerID(), item.Enchant(), item.Amount(), 0, 0, characterName.c_str(),
        accountName.c_str(), characterName.c_str(), accountName.c_str(), item.ItemID()
    );
}

bool ExecuteModifyPledgeRecruitBoard(
    int worldId,
    int pledgeId,
    int registrantId,
    int trend,
    const wchar_t* titleMessage,
    const wchar_t* bodyMessage,
    int recruitType,
    int pledgeType
) {
    // Semantic name for current helper 0x00515a98. No source identifier
    // survives. Cached.exe binds world, registrant, pledge. The PostgreSQL
    // function uses world, pledge, registrant, as bound below. Both keep
    // the same field meanings, single Fetch and result==1 test.
    int result = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    const int registerDate = runtime::crt::Time32Now();
    constexpr const wchar_t* statement = L"{Call lin_ModifyPledgeRecruitBoard (?,?,?,?,?,?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&worldId);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&registrantId);
    connection.BindParameter(&trend);
    connection.BindParameter(&registerDate);
    connection.BindParameter(titleMessage);
    connection.BindParameter(bodyMessage);
    connection.BindParameter(&recruitType);
    connection.BindParameter(&pledgeType);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }
    return result == 1;
}

} // namespace

bool LoadSevenSignQuestInfo(CSocket* socket, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    SevenSignQuestDB& database = SevenSignQuestDB::Instance();
    std::array<char, 0x5000> buffer{};
    int assembled;
    {
        L2_READ_GUARD(database);
        assembled = database.AssembleAll(buffer.data(), static_cast<int>(buffer.size()));
    }

    if (assembled == 0) {
        socket->Send("chd", 0xc6, 0, 0);
        // Log.AddT(LOG_ERROR, L"[%s][%d] Buffer Overflow!!!", L"ServerPacketEx.cpp", 0xb4)
        // L2_LOG_ERROR(Log, L"[%s][%d] Buffer Overflow!!!", L"ServerPacketEx.cpp", 0xb4)
        L2_LOG_ERROR(Log,
            "[seven signs] State serialization returned zero bytes for the 20480-byte reply buffer. Failure reply requested."
        );
    } else {
        socket->Send("chdb", 0xc6, 0, 1, assembled, buffer.data());
    }
    return false;
}

bool LoadTimeAttackInfo(CSocket* socket, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    std::array<char, 0x5000> buffer{};
    const int assembled = TimeAttackDB::Instance().AssembleAll(buffer.data(), static_cast<int>(buffer.size()));
    const int errorCode = assembled == 0 ? 1 : 0;
    socket->Send("chdb", 0xc6, 4, errorCode, assembled, buffer.data());
    return false;
}

bool SetDawnRoundToPledge(CSocket* socket, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int pledgeId = 0;
    int dawnRound = 0;
    ::Disassemble(packet, "dd", &pledgeId, &dawnRound);
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (pledge) {
        pledge->SetDawnRoundToMember(dawnRound);
    }
    return false;
}

bool StartSevenSignEvent(CSocket* socket, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    int round = 0;
    ::Disassemble(packet, "d", &round);

    SevenSignQuestDB& database = SevenSignQuestDB::Instance();
    std::array<char, 0x5000> buffer{};
    int assembled;
    {
        L2_WRITE_GUARD(database);
        database.StartEvent(round);
        assembled = database.AssembleAll(buffer.data(), static_cast<int>(buffer.size()));
        if (assembled == 0) {
            // Current 0x0053233d supplies only the first of the two `d` values
            // required by "chdd", leaking an uninitialized stack slot. Keep the
            // wire shape and error value while making that disclosure deterministic.
            socket->Send("chdd", 0xc6, 7, 2, 0);
        }
    }
    if (assembled != 0) {
        socket->Send("chdb", 0xc6, 7, 0, assembled, buffer.data());
    }
    return false;
}

bool InitializeTimeAttackDB(CSocket* socket, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    int round = 0;
    ::Disassemble(packet, "d", &round);
    TimeAttackDB::Instance().InitializeTo(round);

    std::array<char, 0x5000> buffer{};
    const int assembled = TimeAttackDB::Instance().AssembleAll(buffer.data(), static_cast<int>(buffer.size()));
    if (assembled != 0) {
        socket->Send("chdb", 0xc6, 4, 0, assembled, buffer.data());
    }
    return false;
}

bool ChangeSSQDropGuardStatus(CSocket* socket, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int enabled = 0;
    ::Disassemble(packet, "d", &enabled);
    SevenSignQuestDB& database = SevenSignQuestDB::Instance();
    {
        L2_WRITE_GUARD(database);
        database.SetDropGuardStatus(enabled != 0);
        database.SaveSystemInfo();
    }
    return false;
}

bool LoadMarketPriceList(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00532600. The row that causes a full 600-entry
    // page to flush is not copied. That is the shipping loop's
    // observable boundary behavior.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    int itemType = 0;
    int enchant = 0;
    double averagePrice = 0.0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&itemType);
    connection.Bind(&enchant);
    connection.Bind(&averagePrice);

    constexpr const wchar_t* statement = L"{Call lin_LoadMarketPriceList}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement)) {
        return false;
    }

    std::array<char, 0x5000> buffer{};
    int count = 0;
    int length = 0;
    while (connection.Fetch()) {
        if (count < 600) {
            length += ::Assemble(
                buffer.data() + length, static_cast<int>(buffer.size()) - length, "ddf", itemType, enchant, averagePrice
            );
            ++count;
        } else {
            socket->Send("chdb", 0xc6, 0x13, count, length, buffer.data());
            buffer.fill(0);
            count = 0;
            length = 0;
        }
    }
    if (count > 0) {
        socket->Send("chdb", 0xc6, 0x13, count, length, buffer.data());
    }
    return false;
}

bool RenewSubJob(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0054e794 and the paired current L2Server sender
    // CDB::RequestRenewSubJob 0x006504e0 fix all 23 request fields and both
    // reply layouts.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    int characterId = 0;
    int mode = 0;
    int requestContext1 = 0;
    int requestContext2 = 0;
    int subjobId = 0;
    int subjobType = 0;
    int newClass = 0;
    int oldSubjobId = 0;
    // The paired L2Server selects this value from a four-entry per-subjob
    // array and consumes it unchanged in ReplyRenewSubJob. Its source member
    // identifier is unavailable, so keep the wire role without renaming it.
    int renewReplyValue = 0;
    double hp = 0.0;
    double mp = 0.0;
    std::int64_t sp = 0;
    std::int64_t experience = 0;
    int level = 0;
    int henna1 = 0;
    int henna2 = 0;
    int henna3 = 0;
    int oldVitalPoint = 0;
    int oldRestoreCount = 0;
    int oldResetDateVP = 0;
    int oldResetDateRC = 0;
    int abilityPoints = 0;
    ::Disassemble(
        packet, "ddddddddddffQQddddddddd", &userServerId, &characterId, &mode, &requestContext1, &requestContext2, &subjobId,
        &subjobType, &newClass, &oldSubjobId, &renewReplyValue, &hp, &mp, &sp, &experience, &level, &henna1, &henna2,
        &henna3, &oldVitalPoint, &oldRestoreCount, &oldResetDateVP, &oldResetDateRC, &abilityPoints
    );

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send(
            "chddddddddd", 0xc6, 0x14, userServerId, mode, requestContext1, requestContext2, subjobId, subjobType, newClass,
            renewReplyValue, 0
        );
        return false;
    }

    const int previousSubjobId = user->GetSubjobId();
    const int previousClass = user->GetClass();
    DBConn vitalityConnection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* vitalityStatement = L"{Call lin_SaveCharVitality (?, ?, ?, ?, ?, ?)}";
    vitalityConnection.Prepare(vitalityStatement);
    vitalityConnection.BindParameter(&characterId);
    vitalityConnection.BindParameter(&previousSubjobId);
    vitalityConnection.BindParameter(&oldVitalPoint);
    vitalityConnection.BindParameter(&oldRestoreCount);
    vitalityConnection.BindParameter(&oldResetDateVP);
    vitalityConnection.BindParameter(&oldResetDateRC);
    vitalityConnection.ExecutePrepared(vitalityStatement);

    const bool renewed = user->RenewSubJob(
        subjobId, subjobType, newClass, oldSubjobId, hp, mp, sp, experience, level, henna1, henna2, henna3, abilityPoints
    );
    user->LoadUserVitality();
    const CVitalData newVitality = user->GetVitalData();

    socket->Send(
        "chddddddddddddd", 0xc6, 0x14, userServerId, mode, requestContext1, requestContext2, subjobId, subjobType, newClass,
        renewReplyValue, renewed ? 1 : 0, static_cast<int>(newVitality.vitalPoint),
        static_cast<int>(newVitality.restoreCount), static_cast<int>(newVitality.latestResetDate_VP),
        static_cast<int>(newVitality.latestResetDate_RC)
    );

    const std::u16string characterName = std::u16string(user->GetCharName());
    const std::u16string accountName = std::u16string(user->GetAccountName());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x1fa5, user->GetId(), user->GetAccountID(), 0, 0, user->GetX(), user->GetY(), user->GetZ(), nullptr, nullptr,
        nullptr, previousSubjobId, previousClass, subjobId, newClass, oldVitalPoint, newVitality.vitalPoint, oldRestoreCount,
        newVitality.restoreCount, 0, 0, characterName.c_str(), accountName.c_str(), nullptr, nullptr, 0
    );
    return false;
}

bool CreateDualJob(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0054ef34 and the paired current L2Server sender
    // CDB::RequestCreateDualJob 0x006507ac fix all 17 request fields and both
    // extended reply layouts.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    int characterId = 0;
    int mode = 0;
    int requestContext1 = 0;
    int requestContext2 = 0;
    int newSubjobId = 0;
    int newClass = 0;
    int oldSubjobId = 0;
    double hp = 0.0;
    double mp = 0.0;
    std::int64_t sp = 0;
    std::int64_t experience = 0;
    int level = 0;
    int henna1 = 0;
    int henna2 = 0;
    int henna3 = 0;
    int abilityPoints = 0;
    ::Disassemble(
        packet, "ddddddddffQQddddd", &userServerId, &characterId, &mode, &requestContext1, &requestContext2, &newSubjobId,
        &newClass, &oldSubjobId, &hp, &mp, &sp, &experience, &level, &henna1, &henna2, &henna3, &abilityPoints
    );

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("chdddddd", 0xc6, 0x102, userServerId, mode, requestContext1, requestContext2, 0, newClass);
        return false;
    }

    const CVitalData oldVitality = user->GetVitalData();
    const int oldClass = user->GetClass();
    DBConn vitalityConnection(runtime::process::CurrentDatabaseId());
    const long oldVitalPoint = oldVitality.vitalPoint;
    const long oldRestoreCount = oldVitality.restoreCount;
    const long oldResetDateVP = oldVitality.latestResetDate_VP;
    const long oldResetDateRC = oldVitality.latestResetDate_RC;
    constexpr const wchar_t* vitalityStatement = L"{Call lin_SaveCharVitality (?, ?, ?, ?, ?, ?)}";
    vitalityConnection.Prepare(vitalityStatement);
    vitalityConnection.BindParameter(&characterId);
    vitalityConnection.BindParameter(&oldSubjobId);
    vitalityConnection.BindParameter(&oldVitalPoint);
    vitalityConnection.BindParameter(&oldRestoreCount);
    vitalityConnection.BindParameter(&oldResetDateVP);
    vitalityConnection.BindParameter(&oldResetDateRC);
    vitalityConnection.ExecutePrepared(vitalityStatement);

    const int result = user->CreateDualJob(
        newSubjobId, newClass, oldSubjobId, hp, mp, sp, experience, level, henna1, henna2, henna3, abilityPoints
    );
    user->LoadUserVitality();
    const CVitalData newVitality = user->GetVitalData();

    socket->Send(
        "chdddddddddd", 0xc6, 0x102, userServerId, mode, requestContext1, requestContext2, result, newClass,
        static_cast<int>(newVitality.vitalPoint), static_cast<int>(newVitality.restoreCount),
        static_cast<int>(newVitality.latestResetDate_VP), static_cast<int>(newVitality.latestResetDate_RC)
    );

    const std::u16string characterName = std::u16string(user->GetCharName());
    const std::u16string accountName = std::u16string(user->GetAccountName());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x1fa5, user->GetId(), user->GetAccountID(), 0, 0, user->GetX(), user->GetY(), user->GetZ(), nullptr, nullptr,
        nullptr, oldSubjobId, oldClass, newSubjobId, newClass, oldVitality.vitalPoint, newVitality.vitalPoint,
        oldVitality.restoreCount, newVitality.restoreCount, 0, 0, characterName.c_str(), accountName.c_str(), nullptr,
        nullptr, 0
    );
    return false;
}

bool LoadAllNobless(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00532874 sends 100 rows per page and always emits
    // the final page, including an empty page after an exact multiple of 100.
    L2_GUARD_FUNCTION();

    int requestContext1 = 0;
    int requestContext2 = 0;
    ::Disassemble(packet, "dd", &requestContext1, &requestContext2);

    int characterId = 0;
    int noblessType = 0;
    int heroType = 0;
    int winCount = 0;
    int previousPoint = 0;
    int olympiadPoint = 0;
    int matchCount = 0;
    int olympiadWinCount = 0;
    int olympiadLoseCount = 0;
    int tradePoint = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&characterId);
    connection.Bind(&noblessType);
    connection.Bind(&heroType);
    connection.Bind(&winCount);
    connection.Bind(&previousPoint);
    connection.Bind(&olympiadPoint);
    connection.Bind(&matchCount);
    connection.Bind(&olympiadWinCount);
    connection.Bind(&olympiadLoseCount);
    connection.Bind(&tradePoint);

    constexpr const wchar_t* statement = L"{Call lin_GetAllNobless}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement)) {
        return false;
    }

    int page = 0;
    bool fullPage = false;
    do {
        std::array<char, 0x2004> buffer{};
        int length = 0;
        int count = 0;
        while (count != 100 && connection.Fetch()) {
            length += ::Assemble(
                buffer.data() + length, static_cast<int>(buffer.size()) - length, "dddddddddd", characterId, noblessType,
                heroType, winCount, previousPoint, olympiadPoint, matchCount, olympiadWinCount, olympiadLoseCount, tradePoint
            );
            ++count;
        }
        fullPage = count == 100;
        ++page;
        socket->Send("chddddb", 0xc6, 0x15, page, requestContext1, requestContext2, count, length, buffer.data());
    } while (fullPage);
    return false;
}

bool LoadNobless(CSocket* socket, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);

    int noblessType = 0;
    int heroType = 0;
    int winCount = 0;
    int previousPoint = 0;
    int olympiadPoint = 0;
    int matchCount = 0;
    int olympiadWinCount = 0;
    int olympiadLoseCount = 0;
    int historyOpen = 0;
    int tradePoint = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&noblessType);
    connection.Bind(&heroType);
    connection.Bind(&winCount);
    connection.Bind(&previousPoint);
    connection.Bind(&olympiadPoint);
    connection.Bind(&matchCount);
    connection.Bind(&olympiadWinCount);
    connection.Bind(&olympiadLoseCount);
    connection.Bind(&historyOpen);
    connection.Bind(&tradePoint);

    constexpr const wchar_t* statement = L"{Call lin_GetNoblessById (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        // This is the one-row form of the same subtype-0x15 page consumed by
        // paired L2Server ReplyLoadNobless 0x0067533c. Current Cached binds
        // history_open but omits it from this ten-field row.
        // trade_point remains the final field, matching LoadAllNobless.
        socket->Send(
            "chdddddddddddddd", 0xc6, 0x15, 1, 0, 0, 1, characterId, noblessType, heroType, winCount, previousPoint,
            olympiadPoint, matchCount, olympiadWinCount, olympiadLoseCount, tradePoint
        );
    }
    return false;
}

bool GetNoblessTop10(CSocket* socket, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    int olympiadClass = 0;
    int season = 0;
    ::Disassemble(packet, "dd", &olympiadClass, &season);

    int characterId = 0;
    std::array<char16_t, 25> characterName{};
    int point = 0;
    int matchCount = 0;
    int olympiadWinCount = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&characterId);
    connection.Bind(characterName.data(), 0x32);
    connection.Bind(&point);
    connection.Bind(&matchCount);
    connection.Bind(&olympiadWinCount);
    constexpr const wchar_t* statement = L"{Call lin_GetNoblessTop10 (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&olympiadClass);
    connection.BindParameter(&season);
    if (!connection.ExecutePrepared(statement)) {
        return false;
    }

    std::array<char, 0x2004> buffer{};
    int length = 0;
    int count = 0;
    while (connection.Fetch()) {
        length += ::Assemble(
            buffer.data() + length, static_cast<int>(buffer.size()) - length, "dSddd", characterId, characterName.data(),
            point, matchCount, olympiadWinCount
        );
        ++count;
    }
    socket->Send("chddb", 0xc6, 0x19, olympiadClass, count, length, buffer.data());
    return false;
}

bool GetNoblessAchievements(CSocket* socket, const unsigned char* packet) {
    // Current 0x005500a0 pages at 300 rows and stops after the request's
    // global maximum. An exact multiple produces a final empty page.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int maximumCount = 0;
    ::Disassemble(packet, "dd", &characterId, &maximumCount);

    int winType = 0;
    int target = 0;
    int winTime = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&winType);
    connection.Bind(&target);
    connection.Bind(&winTime);
    constexpr const wchar_t* statement = L"{Call lin_GetNoblessAchievements (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(statement)) {
        return false;
    }

    int totalCount = 0;
    bool fullPage = false;
    do {
        std::array<char, 0x2004> buffer{};
        int length = 0;
        int pageCount = 0;
        while (pageCount != 300) {
            if (totalCount >= maximumCount || !connection.Fetch()) {
                break;
            }
            length += ::Assemble(
                buffer.data() + length, static_cast<int>(buffer.size()) - length, "ddd", winType, target, winTime
            );
            ++pageCount;
            ++totalCount;
        }
        fullPage = pageCount == 300;
        socket->Send("chddb", 0xc6, 0x1a, characterId, pageCount, length, buffer.data());
    } while (fullPage);
    return false;
}

bool RequestLoadPledgeSiegeKillDeathCount(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005502f4.
    L2_GUARD_FUNCTION();

    int castleId = 0;
    ::Disassemble(packet, "d", &castleId);

    int pledgeId = 0;
    int killCount = 0;
    int deathCount = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&pledgeId);
    connection.Bind(&killCount);
    connection.Bind(&deathCount);
    constexpr const wchar_t* statement = L"{Call lin_GetPledgeKillDeathCount (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&castleId);
    if (!connection.ExecutePrepared(statement))
        return false;

    bool fullPage = false;
    do {
        std::array<char, 0x2004> buffer{};
        int length = 0;
        int count = 0;
        while (count != 200 && connection.Fetch()) {
            const int assembled = ::Assemble(
                buffer.data() + length, static_cast<int>(buffer.size()) - length, "ddd", pledgeId, killCount, deathCount
            );
            length += assembled;
            ++count;
        }
        fullPage = count == 200;
        // The shipping do/while sends the empty terminal page.
        socket->Send("chddb", 0xc6, 0x1d, castleId, count, length, buffer.data());
    } while (fullPage);
    return false;
}

bool RequestSetPledgeEmblem(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00550b3c. The returned cursor is the raw piece
    // body. Paired L2Server field origins and the physical call at 0x00550de1
    // fix all seven dwords and the AddPledgeEmblemPiece argument order.
    L2_GUARD_FUNCTION();

    int userSocketId = 0;
    int pledgeSocketId = 0;
    unsigned int pledgeId = 0;
    int pieceIndex = 0;
    int totalFileSize = 0;
    int pieceSize = 0;
    int skipCanRegister = 0;
    const unsigned char* const pieceData = ::Disassemble(
        packet, "ddddddd", &userSocketId, &pledgeSocketId, &pledgeId, &pieceIndex, &totalFileSize, &pieceSize,
        &skipCanRegister
    );

    // Noise: one progress line per emblem fragment, before validation or persistence.
    // Log.Add(
    //     LOG_ERROR, "[NO_ERROR] RQ_EX_SET_PLEDGE_EMBLEM %d %d %d %d %d", userSocketId, pledgeSocketId, pieceIndex,
    //     totalFileSize, pieceSize
    // )

    if (pieceSize == 0) {
        socket->Send("chddd", 0xc6, 0x16, userSocketId, pledgeSocketId, 0);
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    if (!g_fileServerManager.m_ready) {
        // Log.AddT(LOG_ERROR, L"[FileServer] File Server is not Ready! Check Config")
        // L2_LOG_ERROR(Log, L"[FileServer] File Server is not Ready! Check Config")
        // Previous: L2_LOG_ERROR(Log, "Cannot register clan %u emblem fragment %d (%d bytes): FileServer is not ready, no reply sent.", pledgeId, pieceIndex, pieceSize)
        L2_LOG_ERROR(Log, "[clan emblem] Cannot register clan %u emblem fragment %d (%d bytes): the file server is not ready; no reply sent.", pledgeId, pieceIndex, pieceSize);
        return false;
    }

    if (pieceIndex == 0) {
        int result = 0;
        connection.Bind(&result);
        if (skipCanRegister == 0) {
            constexpr const wchar_t* statement = L"{Call lin_CanRegisterPledgeNewEmblem (?)}";
            connection.Prepare(statement);
            connection.BindParameter(&pledgeId);
            if (connection.ExecutePrepared(statement) && connection.Fetch() && result < 0) {
                socket->Send("chddd", 0xc6, 0x16, userSocketId, pledgeSocketId, result);
                return false;
            }
        }
    }

    g_pledgeEmblemDb.AddPledgeEmblemPiece(
        userSocketId, static_cast<int>(pledgeId), pledgeSocketId, pieceIndex, pieceSize, pieceData
    );
    if (pieceIndex < 4) {
        socket->Send("chddd", 0xc6, 0xd3, userSocketId, pledgeSocketId, pieceIndex);
    }
    return false;
}

bool RequestGetPledgeEmblem(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00532e08 ignores the request socket after decode
    // and sends through the process's active L2Server socket.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int userSocketId = 0;
    int pledgeEmblemId = 0;
    int worldId = 0;
    int pledgeSocketId = 0;
    ::Disassemble(packet, "dddd", &userSocketId, &pledgeEmblemId, &worldId, &pledgeSocketId);

    L2_GUARD(g_pledgeEmblemDb);
    g_pledgeEmblemDb.LoadPledgeEmblem(userSocketId, pledgeEmblemId, worldId, pledgeSocketId);
    return false;
}

bool RequestDelPledgeEmblem(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00533848 and paired L2Server ReplyDelPledgeEmblem
    // fix the result values: 2 only after CPledge::DeleteEmblem succeeds,
    // otherwise 0.
    L2_GUARD_FUNCTION();

    int pledgeId = 0;
    ::Disassemble(packet, "d", &pledgeId);
    bool deleted = false;
    if (pledgeId != 0) {
        CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
        if (pledge) {
            deleted = pledge->DeleteEmblem();
        }
    }
    socket->Send("chdd", 0xc6, 0x18, pledgeId, deleted ? 2 : 0);
    return false;
}

bool RequestLoadPCCafePoint(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005339ac.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int type = 0;
    int date = 0;
    ::Disassemble(packet, "ddd", &characterId, &type, &date);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("chdddd", 0xc6, 0x1f, characterId, -1, type, date);
        return false;
    }

    int point = 0;
    if (type == 1) {
        point = user->LoadPCCafePoint();
    } else if (type == 3) {
        point = user->LoadHomePCCafeCount();
        if (point == -1) {
            const long now = static_cast<long>(runtime::crt::Time32Now());
            date = GetYYYYMMDDFromTime(now);
            socket->Send("chdddd", 0xc6, 0x1f, characterId, 0, type, date);
            return false;
        }
    }

    socket->Send("chdddd", 0xc6, 0x1f, characterId, point, type, date);
    return false;
}

bool RequestSavePCCafePoint(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00533c18.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int point = 0;
    int type = 0;
    int date = 0;
    ::Disassemble(packet, "dddd", &characterId, &point, &type, &date);

    if (type == 4) {
        if (point < 1) {
            // The final zero is an extra variadic argument in the shipping
            // call and is not consumed by the five-dword format string.
            socket->Send("chddddd", 0xc6, 0x20, characterId, 0, 0, 4, date, 0);
        }
        g_unnamedPCCafeType4Value = point;
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("chdddddd", 0xc6, 0x20, characterId, point, 0, type, date, 0);
        return false;
    }

    bool saved = false;
    if (type == 1) {
        saved = user->SavePCCafePoint(point);
    } else if (type == 3) {
        saved = user->SaveHomePCCafeCount(point);
    }
    socket->Send("chdddddd", 0xc6, 0x20, characterId, saved ? point : 0, 0, type, date, 0);
    return false;
}

bool RequestManipulateItemWithPoint(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005345e8 / ServerPacketEx.cpp:1452-1509.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    const unsigned char* const request = ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());

    auto sendMissingOwner = [&](int pointError) {
        int userServerId = 0;
        int pointDelta = 0;
        int pointType = 0;
        int requestValue = 0;
        int replyExtra = 0;
        int atomicJobId = 0;
        ::Disassemble(request, "dddddd", &userServerId, &pointDelta, &pointType, &requestValue, &replyExtra, &atomicJobId);

        if (pointType == 2) {
            const int pledgeId = user ? static_cast<int>(user->GetPledgeID()) : 0;
            socket->Send("chdddddd", 0xc6, 0x2c, userServerId, 0, pledgeId, 0, pointDelta, -3);
        } else if (pointType == 1) {
            socket->Send("chdddddd", 0xc6, 0x20, characterId, pointDelta, pointError, 1, 0, 0);
        } else {
            socket->Send("chddddd", 0xc6, 0x57, characterId, pointDelta, pointError, pointType, 0);
        }
        socket->Send("cddddd", 0xc2, userServerId, atomicJobId, pointType, pointDelta, -1);
    };

    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user", "ServerPacketEx.cpp", 0x5ac)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user", "ServerPacketEx.cpp", 0x5ac)
        L2_LOG_ERROR(Log, "[items] Cannot process item-and-point changes: character %d unavailable. Sending point and item failure replies.", characterId);
        sendMissingOwner(-1);
        return false;
    }

    CWareHouseSP inventory = user->GetWareHouse(0, true);
    if (!inventory) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find inventory warehouse, char dbid[%d]", "ServerPacketEx.cpp", 0x5ca,
        //     characterId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find inventory warehouse, char dbid[%d]", "ServerPacketEx.cpp", 0x5ca,
        //     characterId
        // )
        L2_LOG_ERROR(Log, "[items] Cannot process item-and-point changes for character %d: inventory unavailable. Sending point and item failure replies.", characterId);
        sendMissingOwner(-2);
        return false;
    }

    {
        L2_WRITE_GUARD(*inventory.operator->());
        inventory->ManipulatePointItems(socket, request);
    }
    return false;
}

bool RequestNPCPickItem(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00534bd8.
    L2_GUARD_FUNCTION();

    int first = 0;
    int itemId = 0;
    int third = 0;
    ::Disassemble(packet, "ddd", &first, &itemId, &third);

    bool success = false;
    std::int64_t amount = 0;
    if (itemId == 0) {
        success = true;
    } else if (itemId > 0) {
        CItemSP item = CItem::Load(static_cast<std::uint32_t>(itemId));
        if (item) {
            amount = item->Amount();
            success = item->Delete(true, false);
        }
    }

    socket->Send("chddddQ", 0xc6, 0x21, success ? 1 : 0, first, itemId, third, static_cast<long long>(amount));
    return false;
}

bool RequestUpdateBossRecord(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00535100.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int bossId = 0;
    int record = 0;
    ::Disassemble(packet, "ddd", &characterId, &bossId, &record);
    CUserSP user = g_userDb.GetUser(characterId, false, false, runtime::process::CurrentDatabaseId());
    if (user)
        user->UpdateBossRecord(bossId, record);
    return false;
}

bool RequestLoadBossRecord(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0053522c.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, false, false, runtime::process::CurrentDatabaseId());
    if (user)
        user->LoadBossRecord(socket);
    return false;
}

bool RequestCreateAcademy(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00535354.
    L2_GUARD_FUNCTION();

    int requesterServerId = 0;
    int npcServerId = 0;
    std::array<char16_t, 25> academyName{};
    int requesterId = 0;
    int pledgeId = 0;
    ::Disassemble(packet, "ddSdd", &requesterServerId, &npcServerId, 0x32, academyName.data(), &requesterId, &pledgeId);

    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    const std::wstring hostAcademyName = Utf16ToWide(academyName.data());
    const bool created = pledge && pledge->CreateAcademy(hostAcademyName.c_str());
    if (created) {
        CUserSP user = g_userDb.GetUser(requesterId, true, false, runtime::process::CurrentDatabaseId());
        if (user) {
            const std::u16string academy = academyName.data();
            const std::u16string characterName = std::u16string(user->GetCharName());
            const std::u16string accountName = std::u16string(user->GetAccountName());
            CLogFormatter formatter;
            formatter.WriteLog(
                0x113, user->GetId(), user->GetAccountID(), 0, 0, user->GetX(), user->GetY(), user->GetZ(), academy.c_str(),
                pledge->GetName(), nullptr, pledgeId, 0, 0, 0, 0, 0, 0, 0, 0, 0, characterName.c_str(), accountName.c_str(),
                nullptr, nullptr, 0
            );
        }
    }

    socket->Send("chddddS", 0xc6, 0x24, requesterServerId, npcServerId, pledgeId, created ? 1 : 0, academyName.data());
    return false;
}

bool RequestUpdatePledgeNameValue(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00535814. The paired L2Server sender at
    // 0x006536e8 fixes this four-dword request and reply echo order.
    L2_GUARD_FUNCTION();

    int pledgeServerId = 0;
    int pledgeId = 0;
    int requestContext = 0;
    int nameValueDelta = 0;
    ::Disassemble(packet, "dddd", &pledgeServerId, &pledgeId, &requestContext, &nameValueDelta);

    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (pledge) {
        const int result = pledge->UpdateNameValue(nameValueDelta, 0) ? 0 : -4;
        socket->Send("chdddddd", 0xc6, 0x2c, 0, pledgeServerId, pledgeId, requestContext, nameValueDelta, result);
    }
    return false;
}

bool RequestUpdatePledgeNameValueByPledgeWar(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005359a4. Paired current sender
    // CDB::UpdatePledgeNameValueByPledgeWar 0x00653800 fixes the six request
    // positions and the unsigned source types that survive in its signature.
    L2_GUARD_FUNCTION();

    unsigned int first = 0;
    unsigned int second = 0;
    unsigned int pledgeId = 0;
    int fourth = 0;
    int nameValueDelta = 0;
    unsigned int sixth = 0;
    ::Disassemble(packet, "dddddd", &first, &second, &pledgeId, &fourth, &nameValueDelta, &sixth);

    CPledgeSP pledge = g_pledgeDb.GetPledge(static_cast<int>(pledgeId), false, runtime::process::CurrentDatabaseId());
    if (!pledge)
        return false;

    const int result = pledge->UpdateNameValue(nameValueDelta, 0) ? 3 : -4;
    socket->Send("chddddddd", 0xc6, 0x2c, first, second, pledgeId, fourth, nameValueDelta, result, sixth);
    return false;
}

bool RequestLoadSubPledge(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00535e18.
    L2_GUARD_FUNCTION();

    int pledgeId = 0;
    int pledgeTypeMask = 0;
    ::Disassemble(packet, "dd", &pledgeId, &pledgeTypeMask);

    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        // Log.AddT(LOG_ERROR, L"[%S][%d]Cannot find pledge, id[%d]", "ServerPacketEx.cpp", 0x80c, pledgeId)
        // L2_LOG_ERROR(Log, L"[%S][%d]Cannot find pledge, id[%d]", "ServerPacketEx.cpp", 0x80c, pledgeId)
        L2_LOG_ERROR(Log, "[clans] Cannot load subdivisions for clan %d (type mask %d): clan unavailable. No reply is sent.", pledgeId, pledgeTypeMask);
        return false;
    }

    pledge->SendReplyToLoadSubPledge(socket, static_cast<unsigned long>(static_cast<unsigned int>(pledgeTypeMask)));
    return false;
}

bool RequestLoadPledgeSkill(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00535f74.
    L2_GUARD_FUNCTION();

    int pledgeId = 0;
    ::Disassemble(packet, "d", &pledgeId);

    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        // Log.AddT(LOG_ERROR, L"[%S][%d]Cannot find pledge, id[%d]", "ServerPacketEx.cpp", 0x823, pledgeId)
        // L2_LOG_ERROR(Log, L"[%S][%d]Cannot find pledge, id[%d]", "ServerPacketEx.cpp", 0x823, pledgeId)
        L2_LOG_ERROR(Log, "[clans] Cannot load skills for clan %d: clan unavailable. No reply is sent.", pledgeId);
        return false;
    }

    pledge->SendReplyToLoadSkill(socket);
    return false;
}

bool RequestFishingEventFinish(CSocket* socket [[maybe_unused]], const unsigned char* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0052a408 is guard-only.
    L2_GUARD_FUNCTION();
    return false;
}

bool RequestFishingEventRegister(CSocket* socket [[maybe_unused]], const unsigned char* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0052a488 is guard-only.
    L2_GUARD_FUNCTION();
    return false;
}

bool RequestFishingEventSavePrize(CSocket* socket [[maybe_unused]], const unsigned char* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0052a508 is guard-only.
    L2_GUARD_FUNCTION();
    return false;
}

bool RequestLoadBossRecordRanking(CSocket* socket, const unsigned char* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0052a588.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int point = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&characterId);
    connection.Bind(&point);
    constexpr const wchar_t* statement = L"{Call lin_LoadBossRecordRanking}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement))
        return false;

    bool fullPage = false;
    do {
        CPacketHelperEx<20480> records;
        int count = 0;
        while (count != 1000 && connection.Fetch()) {
            records.Assemble("dd", characterId, point);
            ++count;
        }
        fullPage = count == 1000;
        if (count > 0) {
            socket->Send("chdb", 0xc6, 0x25, count, records.GetLength(), records.GetPacket());
        }
    } while (fullPage);
    return false;
}

bool RequestLoadBossRecordRound(CSocket* socket, const unsigned char* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0052a7a0.
    L2_GUARD_FUNCTION();

    BossRecordDB& database = GetBossRecordDB();
    socket->Send("chddd", 0xc6, 0x26, database.GetRound(), database.GetStartTime(), database.GetEndTime());
    return false;
}

bool RequestMoveToNextBossRecordRound(CSocket* socket, const unsigned char* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0052a894.
    L2_GUARD_FUNCTION();
    GetBossRecordDB().MoveToNextRound(socket);
    return false;
}

bool RequestSetPledgePowerGrade(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052a940. Paired sender
    // CDB::RequestSetPledgePowerGrade 0x00653f48.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int characterId = 0;
    int gradeId = 0;
    ::Disassemble(packet, "dd", &characterId, &gradeId);
    g_userDb.SetPowerGrade(characterId, gradeId);
    return false;
}

bool RequestCreateSubPledge(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005360cc and paired ReplyCreateSubPledge
    // 0x0067f780 fix all seven request fields and the echoed reply order.
    L2_GUARD_FUNCTION();

    int userObjectId = 0;
    int npcObjectId = 0;
    std::array<char16_t, 25> subPledgeName{};
    int masterCharacterId = 0;
    int actorCharacterId = 0;
    int pledgeId = 0;
    int pledgeTypeValue = 0;
    ::Disassemble(
        packet, "ddSdddd", &userObjectId, &npcObjectId, 0x32, subPledgeName.data(), &masterCharacterId, &actorCharacterId,
        &pledgeId, &pledgeTypeValue
    );

    const int worldId = runtime::process::CurrentDatabaseId();
    const auto pledgeType = static_cast<PledgeType>(pledgeTypeValue);
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, worldId);
    const std::wstring hostSubPledgeName = Utf16ToWide(subPledgeName.data());
    const bool created = pledge &&
        pledge->CreateSubPledge(pledgeType, hostSubPledgeName.c_str(), static_cast<std::uint32_t>(masterCharacterId));

    CUserSP actor;
    CUserSP master;
    if (created) {
        actor = g_userDb.GetUser(actorCharacterId, true, false, worldId);
        master = g_userDb.GetUser(masterCharacterId, true, false, worldId);
        if (actor && master) {
            const std::u16string actorName = std::u16string(actor->GetCharName());
            const std::u16string actorAccount = std::u16string(actor->GetAccountName());
            const std::u16string masterName = std::u16string(master->GetCharName());
            const std::u16string masterAccount = std::u16string(master->GetAccountName());
            CLogFormatter formatter;
            formatter.WriteLog(
                0x112, actor->GetId(), actor->GetAccountID(), master->GetId(), master->GetAccountID(), actor->GetX(),
                actor->GetY(), actor->GetZ(), subPledgeName.data(), pledge->GetName(), u"", pledgeId, pledgeTypeValue, 0, 0,
                0, 0, 0, 0, 0, 0, actorName.c_str(), actorAccount.c_str(), masterName.c_str(), masterAccount.c_str(), 0
            );
        }
    }

    socket->Send(
        "chddSddddd", 0xc6, 0x31, userObjectId, npcObjectId, subPledgeName.data(), masterCharacterId, actorCharacterId,
        pledgeId, pledgeTypeValue, created ? 1 : 0
    );
    return false;
}

bool RequestSavePledgeSkill(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052a9fc decodes the fourth dword
    // from the cursor returned by the first three-dword decode.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int pledgeId = 0;
    int skillId = 0;
    int level = 0;
    int expirationTime = 0;
    const unsigned char* cursor = ::Disassemble(packet, "ddd", &pledgeId, &skillId, &level);
    ::Disassemble(cursor, "d", &expirationTime);
    g_pledgeDb.AcquirePledgeSkill(
        static_cast<std::uint32_t>(pledgeId), static_cast<std::uint32_t>(skillId), level, expirationTime
    );
    return false;
}

bool RequestSaveSubPledgeSkill(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052aaf8 is a shipping decode-only stub.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int field1 = 0;
    int field2 = 0;
    int field3 = 0;
    int field4 = 0;
    ::Disassemble(packet, "dddd", &field1, &field2, &field3, &field4);
    return false;
}

bool RequestDelPledgeSkill(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00536728.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int pledgeId = 0;
    int skillId = 0;
    ::Disassemble(packet, "dd", &pledgeId, &skillId);
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (pledge) {
        pledge->DelPledgeSkill(static_cast<std::uint32_t>(skillId));
    }
    return false;
}

bool RequestDelSubPledgeSkill(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052abb4 is a shipping decode-only stub.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int field1 = 0;
    int field2 = 0;
    int field3 = 0;
    ::Disassemble(packet, "ddd", &field1, &field2, &field3);
    return false;
}

bool RequestSavePledgePower(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052ac64. Paired sender
    // CDB::RequestSavePledgePower 0x006541d0.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int pledgeId = 0;
    int gradeId = 0;
    int power = 0;
    ::Disassemble(packet, "ddd", &pledgeId, &gradeId, &power);
    g_pledgeDb.SetPowerGrade(static_cast<std::uint32_t>(pledgeId), gradeId, static_cast<std::uint32_t>(power));
    return false;
}

bool RequestUpdateSubPledgeMaster(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052ad2c and paired
    // ReplyUpdateSubPledgeMaster 0x00677984.
    L2_GUARD_FUNCTION();

    int npcObjectId = 0;
    int userObjectId = 0;
    int requestContext = 0;
    int pledgeId = 0;
    int pledgeTypeValue = 0;
    int masterCharacterId = 0;
    ::Disassemble(
        packet, "dddddd", &npcObjectId, &userObjectId, &requestContext, &pledgeId, &pledgeTypeValue, &masterCharacterId
    );
    static_cast<void>(requestContext);

    const bool updated = g_pledgeDb.UpdateSubPledgeMaster(
        static_cast<std::uint32_t>(pledgeId), static_cast<PledgeType>(pledgeTypeValue),
        static_cast<std::uint32_t>(masterCharacterId)
    );
    socket->Send(
        "chdddddd", 0xc6, 0x33, npcObjectId, userObjectId, pledgeId, pledgeTypeValue, masterCharacterId, updated ? 1 : 0
    );
    return false;
}

bool RequestUpgradeSubPledgeMemberCount(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0053684c and paired
    // ReplyUpgradeSubPledgeMemberCount 0x00677c3c.
    L2_GUARD_FUNCTION();

    int npcObjectId = 0;
    int userObjectId = 0;
    int characterId = 0;
    int pledgeId = 0;
    int pledgeTypeValue = 0;
    int memberCount = 0;
    ::Disassemble(packet, "dddddd", &npcObjectId, &userObjectId, &characterId, &pledgeId, &pledgeTypeValue, &memberCount);

    const auto pledgeType = static_cast<PledgeType>(pledgeTypeValue);
    const bool upgraded =
        g_pledgeDb.UpgradeSubPledgeMemberCount(static_cast<std::uint32_t>(pledgeId), pledgeType, memberCount);
    const int worldId = runtime::process::CurrentDatabaseId();
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, worldId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, worldId);

    if (upgraded) {
        const int negativeMemberCount = static_cast<int>(0U - static_cast<std::uint32_t>(memberCount));
        socket->Send("chdddddd", 0xc6, 0x2c, userObjectId, 0, pledgeId, 0, negativeMemberCount, -1);

        if (user && pledge) {
            CLogFormatter formatter;
            formatter.WriteLog(
                0x7e3, user->GetId(), user->GetAccountID(), 0, 0, user->GetX(), user->GetY(), user->GetZ(),
                pledge->GetName(), pledge->GetSubpledgeName(pledgeType), u"", user->GetClass(), user->GetRace(),
                user->GetGender(), user->GetLevel(), pledgeId, memberCount, pledge->rootNameValue(), pledgeTypeValue, 0, 0,
                u"", u"", u"", u"", 0
            );
        }
    }

    socket->Send("chddddd", 0xc6, 0x34, npcObjectId, userObjectId, pledgeId, pledgeTypeValue, upgraded ? 1 : 0);
    return false;
}

bool RequestLoadCompeteAgit(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00550e74. Paired current receiver
    // CCompeteAgitDBHandler::ReplyLoadCompeteAgit 0x005c6044.
    L2_GUARD_FUNCTION();

    unsigned int agitId = 0;
    ::Disassemble(packet, "d", &agitId);

    int elapsedTime = 0;
    unsigned int winnerPledgeId = 0;
    unsigned int previousOwnerPledgeId = 0;
    int acceptOwner = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&elapsedTime);
    connection.Bind(&winnerPledgeId);
    connection.Bind(&previousOwnerPledgeId);
    connection.Bind(&acceptOwner);
    constexpr const wchar_t* statement = L"{Call lin_LoadCompeteAgitStatus (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&agitId);
    if (connection.ExecutePrepared(statement)) {
        // Current CacheD calls Fetch but ignores its result and
        // therefore sends the initialized zero fields for a missing row.
        connection.Fetch();
    }

    if (winnerPledgeId != 0) {
        CPledgeSP winner = g_pledgeDb.GetPledge(static_cast<int>(winnerPledgeId), false, runtime::process::CurrentDatabaseId());
        if (!winner) {
            // Log.Add(LOG_ERROR, L"invalid pledge for compete agit.")
            // L2_LOG_ERROR(Log, L"invalid pledge for compete agit.")
            // Previous: L2_LOG_ERROR(Log, "Cannot find winning clan %u for contested clan hall %d, status reply will still contain this clan ID.", winnerPledgeId, agitId)
            L2_LOG_ERROR(Log, "[contested clan hall] Cannot find winning clan %u for hall %d; the status reply will still contain this clan ID.", winnerPledgeId, agitId);
        }
    }

    socket->Send("chddddd", 0xc6, 0xd1, agitId, elapsedTime, winnerPledgeId, previousOwnerPledgeId, acceptOwner);
    return false;
}

bool RequestResetCompeteAgit(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005510bc. Paired current receiver
    // CCompeteAgitDBHandler::ReplyResetCompeteAgit 0x005c5f08.
    L2_GUARD_FUNCTION();

    unsigned int agitId = 0;
    ::Disassemble(packet, "d", &agitId);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_ResetCompeteAgitStatus (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&agitId);
    const int result = connection.ExecutePrepared(statement) ? 0 : -1;

    // The paired L2Server decoder consumes the error code first and uses the
    // second integer to resolve the CCompeteAgit instance.
    socket->Send("chdd", 0xc6, 0xd0, result, agitId);
    return false;
}

bool RequestSetCompeteAgitStatus(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00551224.
    L2_GUARD_FUNCTION();

    unsigned int agitId = 0;
    int elapsedTime = 0;
    ::Disassemble(packet, "dd", &agitId, &elapsedTime);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetCompeteAgitStatus (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&agitId);
    connection.BindParameter(&elapsedTime);
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestSetCompeteAgitWinner(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00551384.
    L2_GUARD_FUNCTION();

    unsigned int agitId = 0;
    int winnerPledgeId = 0;
    int acceptOwner = 0;
    ::Disassemble(packet, "ddd", &agitId, &winnerPledgeId, &acceptOwner);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetCompeteAgitWinner (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&agitId);
    connection.BindParameter(&winnerPledgeId);
    connection.BindParameter(&acceptOwner);
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestInitializeBossRecord(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00551504.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, false, false, runtime::process::CurrentDatabaseId());
    if (user) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_InitializeBossRecord (?)}";
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.ExecutePrepared(statement);
    }
    return false;
}

bool RequestGraduateAcademy(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00536bac and paired sender
    // CDB::RequestGraduateAcademy 0x00643c78.
    L2_GUARD_FUNCTION();

    int pledgeId = 0;
    int userObjectId = 0;
    int memberId = 0;
    int classGroup = 0;
    ::Disassemble(packet, "dddd", &pledgeId, &userObjectId, &memberId, &classGroup);

    const int worldId = runtime::process::CurrentDatabaseId();
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, worldId);
    if (!pledge)
        return false;

    pledge->GetAcademyGraduatedReward(static_cast<std::uint32_t>(memberId), classGroup);
    if (classGroup != 5 && classGroup != 6) {
        // Log.AddT(LOG_ERROR, L"RequestGraduateAcademy, ClassGroup[%d]", classGroup)
        // L2_LOG_ERROR(Log, L"RequestGraduateAcademy, ClassGroup[%d]", classGroup)
        // Previous: L2_LOG_WARN(Log, "Academy graduation stopped for member %d, clan %d: class group %d is unsupported, expected 5 or 6. Reward handler already called, no reply sent.", memberId, pledgeId, classGroup)
        L2_LOG_WARN(Log, "[clan academy] Graduation stopped for member %d, clan %d: class group %d is unsupported; expected 5 or 6. The reward handler was already called; no reply sent.", memberId, pledgeId, classGroup);
        return false;
    }

    if (!g_pledgeDb.GraduateAcademyMember(static_cast<std::uint32_t>(pledgeId), static_cast<std::uint32_t>(memberId))) {
        return false;
    }

    socket->Send("chddd", 0xc6, 0x35, pledgeId, userObjectId, memberId);

    CUserSP member = g_userDb.GetUser(memberId, true, false, worldId);
    if (member) {
        std::array<char16_t, 24> academyName{};
        std::copy_n(pledge->m_academyName, academyName.size(), academyName.begin());
        const std::u16string characterName = std::u16string(member->GetCharName());
        const std::u16string accountName = std::u16string(member->GetAccountName());
        CLogFormatter formatter;
        formatter.WriteLog(
            0x7f9, member->GetId(), member->GetAccountID(), 0, 0, member->GetX(), member->GetY(), member->GetZ(),
            academyName.data(), u"", u"", member->GetRace(), member->GetGender(), member->GetClass(), member->GetLevel(), 0,
            0, 0, 0, 0, 0, characterName.c_str(), accountName.c_str(), u"", u"", 0
        );

        // GIP._Add(
        //     L"CMD_WithdrawClan(%d, %d, %d, %d, %d, '%s', %d", GetCacheDConfiguration().worldId, pledgeId,
        //     static_cast<std::int64_t>(std::time(nullptr)), member->GetAccountID(), member->GetId(), member->GetCharName(), 2
        // )
        GIP._Add(
            L"CMD_WithdrawClan(%d, %d, %d, %d, %d, '%s', %d", GetCacheDConfiguration().worldId, pledgeId,
            static_cast<std::int64_t>(std::time(nullptr)), member->GetAccountID(), member->GetId(), Utf16ToWide(member->GetCharName()).c_str(), 2
        );
    }
    return false;
}

bool RequestSetAcademyMaster(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005516a0 and paired sender 0x006542ac.
    L2_GUARD_FUNCTION();

    int userObjectId = 0;
    int pledgeId = 0;
    int memberId = 0;
    int masterId = 0;
    ::Disassemble(packet, "dddd", &userObjectId, &pledgeId, &memberId, &masterId);

    bool updated = false;
    const int worldId = runtime::process::CurrentDatabaseId();
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, worldId);
    if (pledge && pledge->SetAcademyMaster(static_cast<std::uint32_t>(memberId), static_cast<std::uint32_t>(masterId))) {
        const std::uint32_t masterParameter = static_cast<std::uint32_t>(masterId);
        const std::uint32_t memberParameter = static_cast<std::uint32_t>(memberId);
        const std::uint32_t pledgeParameter = static_cast<std::uint32_t>(pledgeId);
        DBConn connection(worldId);
        constexpr const wchar_t* statement = L"{Call lin_SetAcademyMaster (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&masterParameter);
        connection.BindParameter(&memberParameter);
        connection.BindParameter(&pledgeParameter);
        updated = connection.ExecutePrepared(statement);

        if (updated) {
            CUserSP member = g_userDb.GetUser(memberId, true, false, worldId);
            CUserSP master;
            if (masterId != 0) {
                master = g_userDb.GetUser(masterId, true, false, worldId);
            }
            if (member) {
                const std::u16string memberName = std::u16string(member->GetCharName());
                const std::u16string memberAccount = std::u16string(member->GetAccountName());
                const std::u16string masterName = master ? std::u16string(master->GetCharName()) : std::u16string{};
                const std::u16string masterAccount = master ? std::u16string(master->GetAccountName()) : std::u16string{};
                CLogFormatter formatter;
                formatter.WriteLog(
                    0x116, member->GetId(), member->GetAccountID(), master ? master->GetId() : 0,
                    master ? master->GetAccountID() : 0, member->GetX(), member->GetY(), member->GetZ(), u"",
                    pledge->GetName(), u"", pledgeId, master ? 0 : 1, 0, 0, 0, 0, 0, 0, 0, 0, memberName.c_str(),
                    memberAccount.c_str(), masterName.c_str(), masterAccount.c_str(), 0
                );
            }
        }
    }

    socket->Send("chddddd", 0xc6, 0x36, userObjectId, pledgeId, memberId, masterId, updated ? 1 : 0);
    return false;
}

bool RequestPledgeMasterTransfer(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052ae78.
    L2_GUARD_FUNCTION();

    int npcObjectId = 0;
    int userObjectId = 0;
    int characterId = 0;
    int pledgeObjectId = 0;
    int pledgeId = 0;
    std::array<char16_t, 25> newMasterName{};
    ::Disassemble(
        packet, "dddddS", &npcObjectId, &userObjectId, &characterId, &pledgeObjectId, &pledgeId, 0x32, newMasterName.data()
    );

    std::wstring hostName = Utf16ToWide(newMasterName.data());
    const int result = g_pledgeDb.RequestPledgeMasterTransfer(static_cast<std::uint32_t>(pledgeId), hostName.data());
    socket->Send(
        "chdddddSd", 0xc6, 0x37, npcObjectId, userObjectId, characterId, pledgeObjectId, pledgeId, newMasterName.data(),
        result
    );
    return false;
}

bool RequestPledgeMasterTransferCancel(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052b004.
    L2_GUARD_FUNCTION();

    int npcObjectId = 0;
    int userObjectId = 0;
    int characterId = 0;
    int pledgeObjectId = 0;
    int pledgeId = 0;
    ::Disassemble(packet, "ddddd", &npcObjectId, &userObjectId, &characterId, &pledgeObjectId, &pledgeId);

    const int result = g_pledgeDb.RequestPledgeMasterTransferCancel(static_cast<std::uint32_t>(pledgeId));
    socket->Send("chdddddd", 0xc6, 0x38, npcObjectId, userObjectId, characterId, pledgeObjectId, pledgeId, result);
    return false;
}

bool RequestUpdatePledgeNameValueByUser(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00535b54.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int nameValueDelta = 0;
    int updateMode = 0;
    ::Disassemble(packet, "ddd", &characterId, &nameValueDelta, &updateMode);

    const int worldId = runtime::process::CurrentDatabaseId();
    CUserSP user = g_userDb.GetUser(characterId, true, false, worldId);
    if (user && user->GetPledgeID() > 0U) {
        CPledgeSP pledge = g_pledgeDb.GetPledge(static_cast<int>(user->GetPledgeID()), false, worldId);
        if (pledge && updateMode == 0 && pledge->skillLevel() > 4) {
            const int result = pledge->UpdateNameValue(nameValueDelta, 0) ? 0 : -4;
            socket->Send("chdddddd", 0xc6, 0x2c, 0, 0, static_cast<int>(user->GetPledgeID()), 0, nameValueDelta, result);
        }
    }
    return false;
}

bool RequestPCCafeCouponCheck(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00551cdc.
    L2_GUARD_FUNCTION();

    int userDbId = 0;
    int userObjectId = 0;
    const unsigned char* remainder = ::Disassemble(packet, "dd", &userDbId, &userObjectId);

    CUserSP user = g_userDb.GetUser(userDbId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    int worldId = 0;
    int coupon1 = 0;
    int coupon2 = 0;
    int coupon3 = 0;
    int coupon4 = 0;
    ::Disassemble(remainder, "ddddd", &worldId, &coupon1, &coupon2, &coupon3, &coupon4);

    int result = 0;
    DBConn connection(-3);
    connection.Bind(&result);
    constexpr const wchar_t* statement = L"{Call lin_PCCafeAccountCouponCheckState (?, ?, ?, ?, ?, ?)}";
    if (!ExecutePCCafeCouponProcedure(
            connection, statement, coupon1, coupon2, coupon3, coupon4, static_cast<std::uint32_t>(userDbId),
            static_cast<std::uint32_t>(worldId)
        ) ||
        !connection.Fetch()) {
        // Log.Add(LOG_ERROR, L"qsPCCafeCouponCheckState Failed")
        // L2_LOG_ERROR(Log, L"qsPCCafeCouponCheckState Failed")
        L2_LOG_ERROR(Log, "[PC Cafe] Coupon-state check failed for character %d in world %d: query failed or no result row fetched. No reply is sent to user object %d.",
            userDbId, worldId, userObjectId);
        return false;
    }

    socket->Send("chcddddd", 0xc6, 0x39, result, userObjectId, coupon1, coupon2, coupon3, coupon4);
    return false;
}

bool RequestPCCafeCouponUsed(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00551fa0.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int userDbId = 0;
    const unsigned char* remainder = ::Disassemble(packet, "d", &userDbId);
    CUserSP user = g_userDb.GetUser(userDbId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    int worldId = 0;
    int coupon1 = 0;
    int coupon2 = 0;
    int coupon3 = 0;
    int coupon4 = 0;
    ::Disassemble(remainder, "ddddd", &worldId, &coupon1, &coupon2, &coupon3, &coupon4);

    int result = 0;
    DBConn connection(-3);
    connection.Bind(&result);
    constexpr const wchar_t* statement = L"{Call lin_PCCafeAccountCouponSetused (?, ?, ?, ?, ?, ?)}";
    if (!ExecutePCCafeCouponProcedure(
            connection, statement, coupon1, coupon2, coupon3, coupon4, static_cast<std::uint32_t>(userDbId),
            static_cast<std::uint32_t>(worldId)
        ) ||
        !connection.Fetch()) {
        // Log.Add(LOG_ERROR, L"qsPCCafeCouponSetUsed Failed")
        // L2_LOG_ERROR(Log, L"qsPCCafeCouponSetUsed Failed")
        L2_LOG_ERROR(Log, "[PC Cafe] Cannot confirm coupon use for character %d in world %d: query failed or no result row fetched.", userDbId, worldId);
        return false;
    }
    if (result != 1) {
        // Log.Add(LOG_ERROR, "qsPCCafeCouponSetUsed Failed. Check")
        // L2_LOG_ERROR(Log, "qsPCCafeCouponSetUsed Failed. Check")
        L2_LOG_ERROR(Log, "[PC Cafe] Coupon-use procedure returned %d for character %d in world %d; expected result 1.", result, userDbId, worldId);
    }
    return false;
}

bool RequestRenameSubpledge(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052b13c.
    L2_GUARD_FUNCTION();

    int npcObjectId = 0;
    int userObjectId = 0;
    int characterId = 0;
    int pledgeObjectId = 0;
    int pledgeId = 0;
    int pledgeTypeValue = 0;
    std::array<char16_t, 25> newName{};
    ::Disassemble(
        packet, "ddddddS", &npcObjectId, &userObjectId, &characterId, &pledgeObjectId, &pledgeId, &pledgeTypeValue, 0x32,
        newName.data()
    );

    std::wstring hostName = Utf16ToWide(newName.data());
    const int result = g_pledgeDb.RenameSubpledge(
        static_cast<std::uint32_t>(pledgeId), static_cast<PledgeType>(pledgeTypeValue), hostName.data()
    );
    socket->Send(
        "chddddddSd", 0xc6, 0x3a, npcObjectId, userObjectId, characterId, pledgeObjectId, pledgeId, pledgeTypeValue,
        newName.data(), result
    );
    return false;
}

bool RequestLoadTransferredPledgeMaster(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005372a8. The two extra bound result columns are
    // not serialized by the shipping response.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    int pledgeId = 0;
    int newMasterId = 0;
    std::array<char16_t, 25> newMasterName{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&pledgeId);
    connection.Bind(&newMasterId);
    connection.Bind(newMasterName.data(), 0x32);
    constexpr const wchar_t* statement = L"{Call lin_GetTransferredPledgeMaster}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement)) {
        return false;
    }

    bool fullPage = false;
    do {
        std::array<char, 0x2004> buffer{};
        int length = 0;
        int count = 0;
        while (connection.Fetch()) {
            length += ::Assemble(buffer.data() + length, static_cast<int>(buffer.size()) - length, "d", pledgeId);
            ++count;

            CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
            if (pledge) {
                CUserSP ruler =
                    g_userDb.GetUser(static_cast<int>(pledge->m_rulerId), true, false, runtime::process::CurrentDatabaseId());
                if (ruler) {
                    const std::wstring pledgeName = Utf16ToWide(pledge->GetName());
                    // GIP._Add(
                    //     L"CMD_ClanInfoData(%d, %d, '%s', %d, %d, %d, %d, %d, '%s'", GetCacheDConfiguration().worldId,
                    //     pledgeId, pledgeName.c_str(), static_cast<std::int64_t>(std::time(nullptr)), pledge->m_skillLevel,
                    //     pledge->m_nameValue, ruler->GetAccountID(), ruler->GetId(), ruler->GetCharName()
                    // )
                    GIP._Add(
                        L"CMD_ClanInfoData(%d, %d, '%s', %d, %d, %d, %d, %d, '%s'", GetCacheDConfiguration().worldId,
                        pledgeId, pledgeName.c_str(), static_cast<std::int64_t>(std::time(nullptr)), pledge->m_skillLevel,
                        pledge->m_nameValue, ruler->GetAccountID(), ruler->GetId(), Utf16ToWide(ruler->GetCharName()).c_str()
                    );
                }
            }

            if (count == 128)
                break;
        }
        fullPage = count == 128;
        socket->Send("chddb", 0xc6, 0x3b, count, length, length, buffer.data());
    } while (fullPage);
    return false;
}

bool RequestSaveClancommTransferResult(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00552264 and paired
    // CDB::RequestUpdateCommPledgeMasterResult 0x00654670.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int rowCount = 0;
    const unsigned char* row = ::Disassemble(packet, "d", &rowCount);
    DBConn connection(runtime::process::CurrentDatabaseId());
    for (int index = 0; index < rowCount; ++index) {
        int pledgeId = 0;
        int result = 0;
        row = ::Disassemble(row, "dd", &pledgeId, &result);
        constexpr const wchar_t* statement = L"{Call lin_SaveClancommTransferResult (?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&pledgeId);
        connection.BindParameter(&result);
        if (!connection.ExecutePrepared(statement)) {
            // Log.Add(LOG_ERROR, "fail to update pledge_master_transfer when clan comm update")
            // L2_LOG_ERROR(Log, "fail to update pledge_master_transfer when clan comm update")
            L2_LOG_ERROR(Log,
                "[clans] Database request to save clan-community leadership transfer result %d for clan %d failed "
                "at row %d of %d. Continuing with remaining rows.", result, pledgeId, index + 1, rowCount
            );
        }
    }
    // Log.Add(LOG_NORMAL, "complete to update transferred pledge master at clancomm")
    // Noise: unconditional completion message does not indicate whether any transfer result was saved successfully.
    // L2_LOG_INFO(Log, "complete to update transferred pledge master at clancomm")
    return false;
}

bool RequestSetPreviousCastleOwnerID(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00552438 and paired
    // CDB::RequestSetPreviousCastleOwnerID 0x00654778.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int residenceId = 0;
    unsigned int previousPledgeId = 0;
    int previousWorldId = 0;
    ::Disassemble(packet, "ddd", &residenceId, &previousPledgeId, &previousWorldId);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetPreviousCastleOwnerID (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&residenceId);
    connection.BindParameter(&previousPledgeId);
    connection.BindParameter(&previousWorldId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "fail to update castle while do RequestSetPreviousCastleOwnerID")
        // L2_LOG_ERROR(Log, "fail to update castle while do RequestSetPreviousCastleOwnerID")
        L2_LOG_ERROR(Log,
            "[residences] Database request to save previous owner of castle %d failed: clan %u, previous world %d.",
            residenceId, previousPledgeId, previousWorldId
        );
    }
    return false;
}

bool RequestSetPreviousAgitOwnerID(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x005525e0. Its DBConn is constructed before packet
    // decoding and the second input uses the unsigned-32 ODBC specialization.
    L2_GUARD_FUNCTION();

    int agitId = 0;
    unsigned int previousPledgeId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    ::Disassemble(packet, "dd", &agitId, &previousPledgeId);

    constexpr const wchar_t* statement = L"{Call lin_SetPreviousAgitOwnerID (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&agitId);
    connection.BindParameter(&previousPledgeId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "fail to update castle while do RequestSetPreviousAgitOwnerID")
        // L2_LOG_ERROR(Log, "fail to update castle while do RequestSetPreviousAgitOwnerID")
        L2_LOG_ERROR(Log,
            "[residences] Database request to save previous owner of clan hall %d failed: clan %u.",
            agitId, previousPledgeId
        );
    }
    return false;
}

bool RequestAcademyGraduateList(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00552768 and paired
    // CDB::RequestAcademyGraduateList 0x00654918.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    unsigned int pledgeId = 0;
    ::Disassemble(packet, "dd", &userServerId, &pledgeId);

    std::array<char16_t, 25> characterName{};
    CPacketHelperEx<20480> graduates;
    int graduateCount = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(characterName.data(), 0x32);
    constexpr const wchar_t* statement = L"{Call lin_academyGraduateList (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledgeId);
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            // The target's wchar_t is UTF-16. Convert the PostgreSQL host
            // wchar_t carrier before passing it to the exact `S` packet
            // codec on platforms where wchar_t is 32-bit.
            if (graduates.Assemble("S", characterName.data()) < 1) {
                // Log.Add(LOG_ERROR, "Academy member list overflowed!!! pledge_id[%d]", static_cast<int>(pledgeId))
                // L2_LOG_ERROR(Log, "Academy member list overflowed!!! pledge_id[%d]", static_cast<int>(pledgeId))
                L2_LOG_ERROR(Log,
                    "[clans] Cannot encode academy graduate for clan %u, server object %d after %d encoded entries. "
                    "Continuing with remaining rows; reply includes successfully encoded entries.",
                    pledgeId, userServerId, graduateCount
                );
            } else {
                ++graduateCount;
            }
        }
        socket->Send("chddb", 0xc6, 0x3c, userServerId, graduateCount, graduates.GetLength(), graduates.GetPacket());
    }
    return false;
}

bool RequestReorgranizePledgeMember(CSocket* socket, const unsigned char* packet) {
    // The misspelling is the current CacheD symbol. Current root 0x00537784.
    // paired sender CDB::RequestReorganizePledgeMember is 0x006549e4.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    int actorCharacterId = 0;
    int pledgeId = 0;
    int operation = 0;
    unsigned int memberId = 0;
    int pledgeTypeRaw = 0;
    unsigned int swapMemberId = 0;
    ::Disassemble(
        packet, "ddddddd", &userServerId, &actorCharacterId, &pledgeId, &operation, &memberId, &pledgeTypeRaw, &swapMemberId
    );

    const int worldId = runtime::process::CurrentDatabaseId();
    const PledgeType requestedPledgeType = static_cast<PledgeType>(pledgeTypeRaw);
    CPledgeSP pledge;
    CUserSP firstMember;
    CUserSP secondMember;
    bool succeeded = false;
    PledgeType firstOldType = static_cast<PledgeType>(0);
    PledgeType secondOldType = static_cast<PledgeType>(0);

    if (operation == 0) {
        firstMember = g_userDb.GetUser(static_cast<int>(memberId), true, false, worldId);
        pledge = g_pledgeDb.GetPledge(pledgeId, false, worldId);
        if (pledge && firstMember && static_cast<int>(firstMember->GetPledgeID()) == pledgeId) {
            firstOldType = firstMember->GetPledgeType();
            pledge->ReorganizeMember(memberId, firstOldType, requestedPledgeType);
            firstMember->SetPledge(pledgeId, requestedPledgeType);
            succeeded = true;
        }
    } else if (operation == 1) {
        firstMember = g_userDb.GetUser(static_cast<int>(memberId), true, false, worldId);
        secondMember = g_userDb.GetUser(static_cast<int>(swapMemberId), true, false, worldId);
        pledge = g_pledgeDb.GetPledge(pledgeId, false, worldId);
        if (pledge && firstMember && secondMember && static_cast<int>(firstMember->GetPledgeID()) == pledgeId &&
            static_cast<int>(secondMember->GetPledgeID()) == pledgeId) {
            firstOldType = firstMember->GetPledgeType();
            pledge->ReorganizeMember(memberId, firstOldType, requestedPledgeType);
            secondOldType = secondMember->GetPledgeType();
            pledge->ReorganizeMember(swapMemberId, secondOldType, firstOldType);
            firstMember->SetPledge(pledgeId, requestedPledgeType);
            secondMember->SetPledge(pledgeId, firstOldType);
            succeeded = true;
        }
    }

    // All three current send sites (0x00537a4c, 0x005380d5 and
    // 0x00538824) use the exact seven-dword reply literal below and omit the
    // decoded actorCharacterId. It remains an input only for the audit actor
    // lookup after a successful reorganization.
    socket->Send(
        "chddddddd", 0xc6, 0x3e, userServerId, pledgeId, operation, static_cast<int>(memberId), pledgeTypeRaw,
        static_cast<int>(swapMemberId), succeeded ? 1 : 0
    );

    if (!succeeded)
        return false;

    CUserSP actor = g_userDb.GetUser(actorCharacterId, true, false, worldId);
    const std::u16string actorName = actor ? std::u16string(actor->GetCharName()) : std::u16string{};
    const std::u16string actorAccountName = actor ? std::u16string(actor->GetAccountName()) : std::u16string{};
    const std::int64_t actorId = actor ? actor->GetId() : 0;
    const std::int64_t actorAccountId = actor ? actor->GetAccountID() : 0;

    auto pledgeUnitName = [&](PledgeType type) {
        return static_cast<int>(type) == 0 ? pledge->GetName() : pledge->GetSubpledgeName(type);
    };
    auto writeReorganizationLog = [&](CUser& member, PledgeType oldType, PledgeType newType) {
        const std::u16string memberName = std::u16string(member.GetCharName());
        const std::u16string memberAccountName = std::u16string(member.GetAccountName());
        CLogFormatter formatter;
        formatter.WriteLog(
            0x121, actorId, actorAccountId, member.GetId(), member.GetAccountID(), 0, 0, 0, pledge->GetName(),
            pledgeUnitName(oldType), pledgeUnitName(newType), pledgeId, static_cast<int>(newType), 0, 0, 0, 0, 0, 0, 0, 0,
            actorName.c_str(), actorAccountName.c_str(), memberName.c_str(), memberAccountName.c_str(), 0
        );
    };

    writeReorganizationLog(*firstMember.get(), firstOldType, requestedPledgeType);
    if (operation == 1) {
        writeReorganizationLog(*secondMember.get(), secondOldType, firstOldType);
    }
    return false;
}

bool RequestOlympiadPenaltyLog(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00538850. The six-value order is independently
    // fixed by WOlympiadService::SaveOlympiadPenaltyLog's current carrier.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int characterId = 0;
    int point = 0;
    int totalPoint = 0;
    int fieldId = 0;
    int gameRule = 0;
    int penaltyType = 0;
    ::Disassemble(packet, "dddddd", &characterId, &point, &totalPoint, &fieldId, &gameRule, &penaltyType);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, L"[%s][%d]Cannot find user(DBId : %d)", L"ServerPacketEx.cpp", 0xbb5, characterId)
        // L2_LOG_ERROR(Log, L"[%s][%d]Cannot find user(DBId : %d)", L"ServerPacketEx.cpp", 0xbb5, characterId)
        L2_LOG_ERROR(Log, "[olympiad] Cannot write penalty audit for character %d: character unavailable (field %d, rule %d, penalty type %d). Audit skipped.",
            characterId, fieldId, gameRule, penaltyType);
        return false;
    }

    const std::u16string characterName = std::u16string(user->GetCharName());
    const std::u16string accountName = std::u16string(user->GetAccountName());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x372, characterId, user->GetAccountID(), 0, 0, user->GetX(), user->GetY(), user->GetZ(), nullptr, nullptr, nullptr,
        user->GetRace(), user->GetGender(), user->GetClass(), user->GetLevel(), fieldId, point, totalPoint, gameRule,
        penaltyType, 0, characterName.c_str(), accountName.c_str(), nullptr, nullptr, 0
    );
    return false;
}

bool RequestLoadSharedReuseDelaysOfItems(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00552c04 and paired sender/reply roots
    // 0x00654b1c/0x00680530.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, false, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("chdd", 0xc6, 0x3f, characterId, 0);
        return false;
    }

    int sharedDelayId = 0;
    int nextAvailableTime = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&sharedDelayId);
    connection.Bind(&nextAvailableTime);
    constexpr const wchar_t* statement = L"{Call lin_LoadSharedReuseDelaysOfItems (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
        return false;
    }

    CPacketHelperEx<20480> rows;
    int rowCount = 0;
    do {
        if (rowCount > 19) {
            // Log.Add(LOG_ERROR, L"shared item reuse delay count is greater than MAX_COUNT [%s]", user->GetCharName())
            // L2_LOG_ERROR(Log, L"shared item reuse delay count is greater than MAX_COUNT [%s]", user->GetCharName())
            L2_LOG_ERROR(Log, "[items] Shared cooldown list for character %d exceeds capacity 20. Sending the first 20 entries; remaining rows will not be loaded.", characterId);
            break;
        }
        rows.Assemble("dd", sharedDelayId, nextAvailableTime);
        ++rowCount;
    } while (connection.Fetch());

    if (rowCount > 0) {
        socket->Send("chdddb", 0xc6, 0x3f, characterId, 1, rowCount, rows.GetLength(), rows.GetPacket());
    }
    return false;
}

bool RequestSaveSharedReuseDelayOfItem(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005529b8 and paired sender 0x00654be4.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int characterId = 0;
    int sharedDelayId = 0;
    int nextAvailableTime = 0;
    ::Disassemble(packet, "ddd", &characterId, &sharedDelayId, &nextAvailableTime);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveSharedReuseDelayOfItem (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&sharedDelayId);
    connection.BindParameter(&nextAvailableTime);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR, L"save shared reuse delay of item failed.[%s][%d][%d]", user->GetCharName(), sharedDelayId,
        //     nextAvailableTime
        // )
        // L2_LOG_ERROR(Log, L"save shared reuse delay of item failed.[%s][%d][%d]", user->GetCharName(), sharedDelayId,
        //     nextAvailableTime
        // )
        L2_LOG_ERROR(Log, "[items] Cannot save shared cooldown %d for character %d (next available time %d): database operation failed.",
            sharedDelayId, characterId, nextAvailableTime);
    }
    return false;
}

bool RequestVariationItem(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00538ac0 and paired
    // CDB::RequestMakeVariationItem 0x00645f50.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    const unsigned char* itemPacket = ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        CWareHouseSP inventory = user->GetWareHouse(0, true);
        if (inventory) {
            {
                L2_WRITE_GUARD(*inventory.operator->());
                inventory->VariationItem(socket, itemPacket);
            }
            return false;
        }
        // Log.Add(LOG_ERROR, L"[%s][%d]cannot find warehouse.", L"ServerPacketEx.cpp", 0xbd3)
        // L2_LOG_ERROR(Log, L"[%s][%d]cannot find warehouse.", L"ServerPacketEx.cpp", 0xbd3)
        L2_LOG_ERROR(Log, "[items] Cannot augment item for character %d: inventory unavailable. Sending failure -1.", characterId);
    } else {
        // Log.Add(LOG_ERROR, L"[%s][%d]cannot find user. User id[%d]", L"ServerPacketEx.cpp", 0xbcc, characterId)
        // L2_LOG_ERROR(Log, L"[%s][%d]cannot find user. User id[%d]", L"ServerPacketEx.cpp", 0xbcc, characterId)
        L2_LOG_ERROR(Log, "[items] Cannot augment item for character %d: character unavailable. Sending failure -1.", characterId);
    }

    int userServerId = 0;
    int targetItemId = 0;
    int targetItemServerId = 0;
    int mineralItemId = 0;
    int mineralItemServerId = 0;
    int supportItemId = 0;
    int supportItemServerId = 0;
    std::int64_t supportAmount = 0;
    int option1 = 0;
    int option2 = 0;
    ::Disassemble(
        itemPacket, "dddddddQdd", &userServerId, &targetItemId, &targetItemServerId, &mineralItemId, &mineralItemServerId,
        &supportItemId, &supportItemServerId, &supportAmount, &option1, &option2
    );
    socket->Send("chddd", 0xc6, 0x40, -1, userServerId, targetItemServerId);
    return false;
}

bool RequestCancelVariationItem(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00538e34 and paired sender 0x00646198.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    const unsigned char* itemPacket = ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        CWareHouseSP inventory = user->GetWareHouse(0, true);
        if (inventory) {
            {
                L2_WRITE_GUARD(*inventory.operator->());
                inventory->CancelVariationItem(socket, itemPacket);
            }
            return false;
        }
        // Log.Add(LOG_ERROR, L"[%s][%d]cannot find warehouse.", L"ServerPacketEx.cpp", 0xc08)
        // L2_LOG_ERROR(Log, L"[%s][%d]cannot find warehouse.", L"ServerPacketEx.cpp", 0xc08)
        L2_LOG_ERROR(Log, "[items] Cannot cancel item augmentation for character %d: inventory unavailable. Sending failure -1.", characterId);
    } else {
        // Log.Add(LOG_ERROR, L"[%s][%d]cannot find user. User id[%d]", L"ServerPacketEx.cpp", 0xc01, characterId)
        // L2_LOG_ERROR(Log, L"[%s][%d]cannot find user. User id[%d]", L"ServerPacketEx.cpp", 0xc01, characterId)
        L2_LOG_ERROR(Log, "[items] Cannot cancel item augmentation for character %d: character unavailable. Sending failure -1.", characterId);
    }

    int userServerId = 0;
    int targetItemId = 0;
    int targetItemServerId = 0;
    std::int64_t cancellationPrice = 0;
    ::Disassemble(itemPacket, "dddQ", &userServerId, &targetItemId, &targetItemServerId, &cancellationPrice);
    socket->Send("chdddQ", 0xc6, 0x41, -1, userServerId, targetItemServerId, static_cast<long long>(0));
    return false;
}

bool RequestUpdateItemDuration(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00552f28 and paired
    // CDB::UpdateItemDuration 0x00654c98.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int characterId = 0;
    unsigned int itemId = 0;
    int duration = 0;
    ::Disassemble(packet, "ddd", &characterId, &itemId, &duration);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateItemDuration (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&itemId);
    connection.BindParameter(&duration);
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestSaveDuelRecord(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00539174.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int characterId = 0;
    int individualWin = 0;
    int individualLose = 0;
    int individualDraw = 0;
    int partyWin = 0;
    int partyLose = 0;
    int partyDraw = 0;
    ::Disassemble(
        packet, "ddddddd", &characterId, &individualWin, &individualLose, &individualDraw, &partyWin, &partyLose, &partyDraw
    );

    CUserSP user = g_userDb.GetUser(characterId, false, false, runtime::process::CurrentDatabaseId());
    if (user) {
        user->SaveDuelRecord(individualWin, individualLose, individualDraw, partyWin, partyLose, partyDraw);
    }
    return false;
}

bool RequestLoadDuelRecord(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005392f0.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, false, false, runtime::process::CurrentDatabaseId());
    if (user) {
        user->LoadDuelRecord(socket);
    }
    return false;
}

bool RequestLargePacket(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052b2d8 is an intentional decode-only stub.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int first = 0;
    int second = 0;
    ::Disassemble(packet, "dd", &first, &second);
    static_cast<void>(first);
    static_cast<void>(second);
    return false;
}

bool RequestGetHeroMatchResult(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0055309c.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int season = 0;
    ::Disassemble(packet, "dd", &characterId, &season);

    int matchTime = 0;
    std::array<char16_t, 25> rivalName{};
    int isWinner = 0;
    int elapsedTime = 0;
    int rivalClass = 0;
    int gameRule = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&matchTime);
    connection.Bind(rivalName.data(), 0x32);
    connection.Bind(&isWinner);
    connection.Bind(&elapsedTime);
    connection.Bind(&rivalClass);
    connection.Bind(&gameRule);

    constexpr const wchar_t* statement = L"{Call lin_GetOlympiadMatchResult (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&season);
    if (!connection.ExecutePrepared(statement)) {
        return false;
    }

    bool fullPage = false;
    do {
        std::array<char, 0x2004> buffer{};
        int length = 0;
        int rowCount = 0;
        while (rowCount != 100 && connection.Fetch()) {
            length += ::Assemble(
                buffer.data() + length, static_cast<int>(buffer.size()) - length, "dSdddd", matchTime,
                rivalName.data(), isWinner, elapsedTime, rivalClass, gameRule
            );
            ++rowCount;
        }
        fullPage = rowCount == 100;
        socket->Send("chddb", 0xc6, 0x44, characterId, rowCount, length, buffer.data());
    } while (fullPage);
    return false;
}

bool RequestSaveFieldCycleInfo(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00553348 and paired CFieldCycle::_SaveStatus
    // establish the seven persisted values and their order.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int fieldId = 0;
    int point = 0;
    int step = 0;
    int stepChangedTime = 0;
    int pointChangedTime = 0;
    int accumulatedPoint = 0;
    int missionStartedTime = 0;
    ::Disassemble(
        packet, "ddddddd", &fieldId, &point, &step, &stepChangedTime, &pointChangedTime, &accumulatedPoint,
        &missionStartedTime
    );

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveFieldCycleInfo (?,?,?,?,?, ?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&fieldId);
    connection.BindParameter(&point);
    connection.BindParameter(&step);
    connection.BindParameter(&stepChangedTime);
    connection.BindParameter(&pointChangedTime);
    connection.BindParameter(&accumulatedPoint);
    connection.BindParameter(&missionStartedTime);
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestLoadFieldCycleInfo(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005534d4.
    L2_GUARD_FUNCTION();

    int requestedFieldId = 0;
    ::Disassemble(packet, "d", &requestedFieldId);

    int fieldId = 0;
    int point = 0;
    int step = 0;
    int stepChangedTime = 0;
    int pointChangedTime = 0;
    int accumulatedPoint = 0;
    int missionStartedTime = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&fieldId);
    connection.Bind(&point);
    connection.Bind(&step);
    connection.Bind(&stepChangedTime);
    connection.Bind(&pointChangedTime);
    connection.Bind(&accumulatedPoint);
    connection.Bind(&missionStartedTime);

    constexpr const wchar_t* statement = L"{Call lin_LoadFieldCycleInfo (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&requestedFieldId);
    if (!connection.ExecutePrepared(statement)) {
        return false;
    }

    bool fullPage = false;
    do {
        std::array<char, 0x2004> buffer{};
        int length = 0;
        int rowCount = 0;
        while (rowCount != 300 && connection.Fetch()) {
            length += ::Assemble(
                buffer.data() + length, static_cast<int>(buffer.size()) - length, "ddddddd", fieldId, point, step,
                stepChangedTime, pointChangedTime, accumulatedPoint, missionStartedTime
            );
            ++rowCount;
        }
        fullPage = rowCount == 300;
        // Current call-site 0x005536fa loads rowCount, payload length and
        // payload pointer after the fixed c6/45 arguments. Its literal at
        // 0x006e2fe8 is exactly "chdb". The blob symbol consumes the latter
        // two values itself.
        socket->Send("chdb", 0xc6, 0x45, rowCount, length, buffer.data());
    } while (fullPage);
    return false;
}

bool RequestSaveCharacterSlotEx(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00539418 decodes one character ID followed by
    // exactly the 60 consecutive CUser::SetSlot values.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int characterId = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &characterId);
    std::array<int, 60> rawSlots{};
    for (int& slot : rawSlots) {
        cursor = ::Disassemble(cursor, "d", &slot);
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, L"Character save error. Cannot find user. id[%d]", characterId)
        // L2_LOG_ERROR(Log, L"Character save error. Cannot find user. id[%d]", characterId)
        L2_LOG_ERROR(Log,
            "[equipment] Cannot update equipment slots for character %d: character unavailable. Update skipped.",
            characterId
        );
        return false;
    }

    UserEquipmentSlots equipment{};
    std::memcpy(&equipment, rawSlots.data(), sizeof(equipment));
    {
        L2_WRITE_GUARD(*user.operator->());
        user->SetSlot(equipment);
    }
    return false;
}

bool RequestLoadAllFortress(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00553774.
    L2_GUARD_FUNCTION();

    int fortressCount = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &fortressCount);
    for (int index = 0; index < fortressCount; ++index) {
        unsigned int fortressId = 0;
        std::array<char16_t, 256> targetFortressName{};
        cursor = ::Disassemble(cursor, "dS", &fortressId, 0x200, targetFortressName.data());
        const std::u16string fortressName(targetFortressName.data());

        int state = 0;
        int stateChangedTime = 0;
        int stateElapsedTime = 0;
        int pledgeId = 0;
        int contractStatus = 0;
        int parentCastleId = 0;
        int lastOwnerChangedTime = 0;
        int lastRewardGivenTime = 0;
        int ownerRewardCycleCount = 0;
        int castleTreasureLevel = 0;
        int ownerSiegeDefendCount = 0;
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Bind(&state);
        connection.Bind(&stateChangedTime);
        connection.Bind(&stateElapsedTime);
        connection.Bind(&pledgeId);
        connection.Bind(&contractStatus);
        connection.Bind(&parentCastleId);
        connection.Bind(&lastOwnerChangedTime);
        connection.Bind(&lastRewardGivenTime);
        connection.Bind(&ownerRewardCycleCount);
        connection.Bind(&castleTreasureLevel);
        connection.Bind(&ownerSiegeDefendCount);

        constexpr const wchar_t* loadStatement = L"{Call lin_LoadFortress (?)}";
        connection.Prepare(loadStatement);
        connection.BindParameter(&fortressId);
        bool loaded = connection.ExecutePrepared(loadStatement) && connection.Fetch();
        if (!loaded) {
            connection.UnBind();
            constexpr const wchar_t* createStatement = L"{Call lin_CreateFortress (?, ?)}";
            connection.Prepare(createStatement);
            connection.BindParameter(&fortressId);
            connection.BindParameter(fortressName.c_str());
            connection.ExecutePrepared(createStatement);

            connection.UnBind();
            connection.Bind(&state);
            connection.Bind(&stateChangedTime);
            connection.Bind(&stateElapsedTime);
            connection.Bind(&pledgeId);
            connection.Bind(&contractStatus);
            connection.Bind(&parentCastleId);
            connection.Bind(&lastOwnerChangedTime);
            connection.Bind(&lastRewardGivenTime);
            connection.Bind(&ownerRewardCycleCount);
            connection.Bind(&castleTreasureLevel);
            connection.Bind(&ownerSiegeDefendCount);
            connection.Prepare(loadStatement);
            connection.BindParameter(&fortressId);
            loaded = connection.ExecutePrepared(loadStatement) && connection.Fetch();
            if (!loaded) {
                connection.UnBind();
                // Log.Add(
                //     LOG_ERROR, L"[%s][%d] fortress data not created, fortress [%d][%S]", L"ServerPacketEx.cpp", 0xe0b,
                //     static_cast<int>(fortressId), targetFortressName.data()
                // )
                // L2_LOG_ERROR(Log, L"[%s][%d] fortress data not created, fortress [%d][%S]", L"ServerPacketEx.cpp", 0xe0b,
                //     static_cast<int>(fortressId), targetFortressName.data()
                // )
                // Previous: L2_LOG_ERROR(Log, "Cannot load fortress %u (%s) after attempting creation: query failed or returned no row. Stopping at entry %d of %d, no replies for this or remaining entries.",
                //     fortressId, WideToUtf8(fortressName).c_str(), index + 1, fortressCount)
                // L2_LOG_ERROR(Log, "[fortress] Cannot load fortress %u (%s) after attempting creation: the query failed or returned no row. Stopping at entry %d of %d, no replies for this or the remaining entries.",
                //     fortressId, WideToUtf8(fortressName).c_str(), index + 1, fortressCount)
                L2_LOG_ERROR(Log, "[fortress] Cannot load fortress %u (%s) after attempting creation: the query failed or returned no row. Stopping at entry %d of %d; no replies for this or the remaining entries.",
                    fortressId, Utf16ToUtf8(fortressName).c_str(), index + 1, fortressCount);
                break;
            }
        }

        connection.UnBind();
        int decoType = 0;
        int decoId = 0;
        int decoLevel = 0;
        int decoExpire = 0;
        int decoActive = 0;
        std::int64_t nullableIndicator = -1;
        connection.Bind(&decoType);
        connection.Bind(&decoId);
        connection.BindNullable(&decoLevel, &nullableIndicator);
        connection.BindNullable(&decoExpire, &nullableIndicator);
        connection.BindNullable(&decoActive, &nullableIndicator);

        std::array<char, 0x2004> payload{};
        int payloadLength = 0;
        int decoCount = 0;
        constexpr const wchar_t* decoStatement = L"{Call lin_LoadAgitDeco (?)}";
        connection.Prepare(decoStatement);
        connection.BindParameter(&fortressId);
        if (connection.ExecutePrepared(decoStatement)) {
            while (connection.Fetch()) {
                if (decoActive == 1) {
                    payloadLength += ::Assemble(
                        payload.data() + payloadLength, static_cast<int>(payload.size()) - payloadLength, "dddd", decoType,
                        decoId, decoLevel, decoExpire
                    );
                    ++decoCount;
                }
            }
        }

        connection.UnBind();
        int facilityType = 0;
        int facilityLevel = 0;
        connection.Bind(&facilityType);
        connection.Bind(&facilityLevel);
        int facilityCount = 0;
        constexpr const wchar_t* facilityStatement = L"{Call lin_LoadFortressFacility (?)}";
        connection.Prepare(facilityStatement);
        connection.BindParameter(&fortressId);
        if (connection.ExecutePrepared(facilityStatement)) {
            while (connection.Fetch()) {
                payloadLength += ::Assemble(
                    payload.data() + payloadLength, static_cast<int>(payload.size()) - payloadLength, "dd", facilityType,
                    facilityLevel
                );
                ++facilityCount;
            }
        }

        int decoNpcCount = 0;
        payloadLength += AppendAgitDecoNpcs(
            payload.data() + payloadLength, static_cast<int>(payload.size()) - payloadLength, fortressId, decoNpcCount
        );

        if (decoCount < 1 && facilityCount < 1 && decoNpcCount < 1) {
            socket->Send(
                "chddddddddddddddd", 0xc6, 0x46, static_cast<int>(fortressId), state, stateChangedTime, stateElapsedTime,
                pledgeId, contractStatus, parentCastleId, lastOwnerChangedTime, lastRewardGivenTime, ownerRewardCycleCount,
                castleTreasureLevel, ownerSiegeDefendCount, decoCount, facilityCount, decoNpcCount
            );
        } else {
            socket->Send(
                "chdddddddddddddddb", 0xc6, 0x46, static_cast<int>(fortressId), state, stateChangedTime, stateElapsedTime,
                pledgeId, contractStatus, parentCastleId, lastOwnerChangedTime, lastRewardGivenTime, ownerRewardCycleCount,
                castleTreasureLevel, ownerSiegeDefendCount, decoCount, facilityCount, decoNpcCount, payloadLength,
                payload.data()
            );
        }
    }
    return false;
}

bool RequestLoadFortressSiege(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00553e7c.
    L2_GUARD_FUNCTION();

    int fortressId = 0;
    ::Disassemble(packet, "d", &fortressId);
    int siegeStatus = 0;
    int siegeStartTime = 0;
    int siegeElapsedTime = 0;
    int isValidInfo = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&siegeStatus);
    connection.Bind(&siegeStartTime);
    connection.Bind(&siegeElapsedTime);
    connection.Bind(&isValidInfo);

    constexpr const wchar_t* statement = L"{Call lin_LoadFortressSiege (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&fortressId);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }
    socket->Send("chddddd", 0xc6, 0x47, fortressId, siegeStatus, siegeStartTime, siegeElapsedTime, isValidInfo);
    return false;
}

bool RequestLoadFortressSiegeRegistry(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0055405c and paired
    // CFortressDBEventHandler::ReplyLoadFortressSiegeRegistry 0x00742c78.
    L2_GUARD_FUNCTION();

    int fortressId = 0;
    ::Disassemble(packet, "d", &fortressId);

    int pledgeId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&pledgeId);
    constexpr const wchar_t* statement = L"{Call lin_LoadFortressSiegeRegistry (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&fortressId);

    std::array<char, 0x5000> buffer{};
    int length = 0;
    int count = 0;
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            length += ::Assemble(buffer.data() + length, static_cast<int>(buffer.size()) - length, "d", pledgeId);
            ++count;
        }
    }

    // The shipping handler sends an empty successful-shaped page even when
    // ExecutePrepared fails. There is no separate status field.
    socket->Send("chddb", 0xc6, 0x48, fortressId, count, length, buffer.data());
    return false;
}

bool RequestUpdateFortressOwner(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00554250 and paired
    // CDB::RequestOwnFortress 0x006553dc.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int fortressId = 0;
    int pledgeId = 0;
    int lastOwnerChangedTime = 0;
    ::Disassemble(packet, "ddd", &fortressId, &pledgeId, &lastOwnerChangedTime);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateFortressOwner (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&fortressId);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&lastOwnerChangedTime);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "update fortress owner fail, fortress[%d], pledge[%d]", fortressId, pledgeId)
        // L2_LOG_ERROR(Log, "update fortress owner fail, fortress[%d], pledge[%d]", fortressId, pledgeId)
        L2_LOG_ERROR(Log,
            "[residences] Database request to update owner of fortress %d to clan %d failed; ownership-change time %d. "
            "Ownership audit will not be written.", fortressId, pledgeId, lastOwnerChangedTime
        );
        return false;
    }

    if (pledgeId == 0)
        return false;
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge)
        return false;

    CUserSP ruler = g_userDb.GetUser(static_cast<int>(pledge->rullerId()), true, false, runtime::process::CurrentDatabaseId());
    if (!ruler)
        return false;

    // Exact 26-field route-6 record written by the current direct CLog::Add
    // call. Machine stack arguments fix fields 0/1/2/8/11/12/21/22.
    const std::u16string pledgeName(pledge->GetName());
    const std::u16string characterName = std::u16string(ruler->GetCharName());
    const std::u16string accountName = std::u16string(ruler->GetAccountName());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x7e2, ruler->GetId(), ruler->GetAccountID(), 0, 0, 0, 0, 0, pledgeName.c_str(), u"", u"", pledgeId, fortressId, 0,
        0, 0, 0, 0, 0, 0, 0, characterName.c_str(), accountName.c_str(), u"", u"", 0
    );
    return false;
}

bool RequestPayFortressRent(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00539ae0 and paired
    // CDB::RequestPayFortressRent 0x006554c0.
    L2_GUARD_FUNCTION();

    int fortressId = 0;
    int ownerPledgeId = 0;
    int castlePledgeId = 0;
    std::int64_t rent = 0;
    ::Disassemble(packet, "dddQ", &fortressId, &ownerPledgeId, &castlePledgeId, &rent);

    bool paid = false;
    CPledgeSP ownerPledge = g_pledgeDb.GetPledge(ownerPledgeId, false, runtime::process::CurrentDatabaseId());
    if (ownerPledge) {
        {
            CWareHouseSP ownerWarehouse = ownerPledge->GetWareHouse();
            if (ownerWarehouse) {
                // MSVC's signed multiplication wraps in the current image.
                // Use unsigned arithmetic and keep the resulting bit
                // pattern where signed overflow would otherwise be undefined.
                const std::uint64_t doubledBits = static_cast<std::uint64_t>(rent) * 2U;
                std::int64_t doubledRent = 0;
                std::memcpy(&doubledRent, &doubledBits, sizeof(doubledRent));
                {
                    L2_WRITE_GUARD(*ownerWarehouse.operator->());
                    paid = ownerWarehouse->DelAdena(doubledRent);
                }
            }
        }

        // The current path does not roll the owner debit back when the
        // recipient pledge/warehouse is absent or AddAdena fails.
        if (paid && castlePledgeId != 0) {
            CPledgeSP castlePledge = g_pledgeDb.GetPledge(castlePledgeId, false, runtime::process::CurrentDatabaseId());
            if (castlePledge) {
                CWareHouseSP castleWarehouse = castlePledge->GetWareHouse();
                if (castleWarehouse) {
                    {
                        L2_WRITE_GUARD(*castleWarehouse.operator->());
                        castleWarehouse->AddAdena(rent);
                    }
                }
            }
        }
    }

    socket->Send(
        "chdddQd", 0xc6, 0x49, fortressId, ownerPledgeId, castlePledgeId, static_cast<long long>(rent), paid ? 1 : 0
    );
    return false;
}

bool RequestSaveFortressState(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005545ac.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int fortressId = 0;
    int state = 0;
    int stateChangedTime = 0;
    int stateElapsedTime = 0;
    ::Disassemble(packet, "dddd", &fortressId, &state, &stateChangedTime, &stateElapsedTime);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveFortressState (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&fortressId);
    connection.BindParameter(&state);
    connection.BindParameter(&stateChangedTime);
    connection.BindParameter(&stateElapsedTime);
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestSaveFortressSiegeState(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0055470c.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int fortressId = 0;
    int siegeStatus = 0;
    int siegeStartTime = 0;
    int siegeElapsedTime = 0;
    int isValidInfo = 0;
    ::Disassemble(packet, "ddddd", &fortressId, &siegeStatus, &siegeStartTime, &siegeElapsedTime, &isValidInfo);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveFortressSiegeState (?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&fortressId);
    connection.BindParameter(&siegeStatus);
    connection.BindParameter(&siegeStartTime);
    connection.BindParameter(&siegeElapsedTime);
    connection.BindParameter(&isValidInfo);
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestSaveFortressContractInfo(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00554884.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int fortressId = 0;
    int contractStatus = 0;
    int parentCastleId = 0;
    ::Disassemble(packet, "ddd", &fortressId, &contractStatus, &parentCastleId);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveFortressContractInfo (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&fortressId);
    connection.BindParameter(&contractStatus);
    connection.BindParameter(&parentCastleId);
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestSaveFortressRewardInfo(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00554a14.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int fortressId = 0;
    int lastRewardGivenTime = 0;
    int ownerRewardCycleCount = 0;
    int castleTreasureLevel = 0;
    ::Disassemble(packet, "dddd", &fortressId, &lastRewardGivenTime, &ownerRewardCycleCount, &castleTreasureLevel);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveFortressRewardInfo (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&fortressId);
    connection.BindParameter(&lastRewardGivenTime);
    connection.BindParameter(&ownerRewardCycleCount);
    connection.BindParameter(&castleTreasureLevel);
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestSaveFortressSiegeRegistry(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00554b74.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int fortressId = 0;
    int reset = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &fortressId, &reset);
    DBConn connection(runtime::process::CurrentDatabaseId());

    if (reset != 0) {
        constexpr const wchar_t* statement = L"{Call lin_ResetFortressSiegeRegistry (?)}";
        connection.Prepare(statement);
        connection.BindParameter(&fortressId);
        connection.ExecutePrepared(statement);
        return false;
    }

    int count = 0;
    cursor = ::Disassemble(cursor, "d", &count);
    constexpr const wchar_t* statement = L"{Call lin_SaveFortressSiegeRegistry (?, ?)}";
    for (int index = 0; index < count; ++index) {
        int pledgeId = 0;
        cursor = ::Disassemble(cursor, "d", &pledgeId);
        connection.Prepare(statement);
        connection.BindParameter(&fortressId);
        connection.BindParameter(&pledgeId);
        connection.ExecutePrepared(statement);
        connection.ResetParam();
    }
    return false;
}

bool RequestSaveFortressSiegeRegisterPledge(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00554d70.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    int fortressId = 0;
    int pledgeId = 0;
    int registered = 0;
    ::Disassemble(packet, "ddd", &fortressId, &pledgeId, &registered);

    const wchar_t* statement = registered == 0 ? L"{Call lin_SaveFortressSiegeUnregisterPledge (?, ?)}"
                                               : L"{Call lin_SaveFortressSiegeRegisterPledge (?, ?)}";
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Prepare(statement);
    connection.BindParameter(&fortressId);
    connection.BindParameter(&pledgeId);
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestLoadAllAuction(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00554f2c and paired ReplyLoadAllAuction
    // 0x0069b9f4. The current seven-result procedure is distinct from the
    // fourteen-column Classic donor shape.
    L2_GUARD_FUNCTION();

    int limitTime = 0;
    ::Disassemble(packet, "d", &limitTime);

    int auctionId = 0;
    unsigned int auctionStatus = 0;
    int extendStatus = 0;
    int auctionNpc = 0;
    int startTime = 0;
    int endTime = 0;
    int itemIndex = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&auctionId);
    connection.Bind(&auctionStatus);
    connection.Bind(&extendStatus);
    connection.Bind(&auctionNpc);
    connection.Bind(&startTime);
    connection.Bind(&endTime);
    connection.Bind(&itemIndex);

    constexpr const wchar_t* statement = L"{Call lin_LoadAllAuction (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&limitTime);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, L"RequestLoadAllAuction error")
        // Log.Add(LOG_ERROR, "component=auction event=load_failed procedure=lin_LoadAllAuction limit_time=%d, request aborted", limitTime)
        // Log.Add(LOG_ERROR, "[auction] lin_LoadAllAuction failed for time limit %d. Request aborted.", limitTime)
        L2_LOG_ERROR(Log, "[auction] lin_LoadAllAuction failed for time limit %d. Request aborted.", limitTime);
        return false;
    }

    bool packetRemained = false;
    do {
        std::array<char, 0x5000> buffer{};
        int length = 0;
        int count = 0;
        packetRemained = false;
        while (count != 300 && connection.Fetch()) {
            const int assembled = ::Assemble(
                buffer.data() + length, static_cast<int>(buffer.size()) - length, "ddddddd", auctionId, auctionStatus,
                extendStatus, auctionNpc, startTime, endTime, itemIndex
            );
            if (assembled > 0) {
                length += assembled;
                ++count;
            }
            // Noise: one success message per row. The batch summary carries the useful count.
            // Log.Add(LOG_ERROR, "[NO_ERROR] lin_LoadAllAuction [%d, %d, %d]", auctionId, auctionStatus, auctionNpc)
        }

        if (count == 300) {
            packetRemained = true;
            // Noise: constant full-page size duplicates the batch count.
            // Log.Add(LOG_ERROR, "[NO_ERROR] lin_LoadAllAuction bPacketRemained [%d]", 300)
        }
        // Log.Add(LOG_ERROR, "[NO_ERROR] lin_LoadAllAuction [%d] c = [%d]", limitTime, count)
        // Log.Add(LOG_NORMAL, "component=auction event=batch_loaded limit_time=%d rows=%d more=%d", limitTime, count, packetRemained ? 1 : 0)
        // Log.Add(LOG_NORMAL, "[auction] Prepared batch for time limit %d: %d rows, %s.", limitTime, count, packetRemained ? "full batch, continuing fetch" : "final batch")
        L2_LOG_INFO(Log, "[auction] Prepared batch for time limit %d: %d rows; %s.", limitTime, count, packetRemained ? "full batch, continuing fetch" : "final batch");
        socket->Send("chddb", 0xc6, 0x4a, packetRemained ? 1 : 0, count, length, buffer.data());
    } while (packetRemained);
    return false;
}

bool RequestLoadAllAuctionBid(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00555238.
    L2_GUARD_FUNCTION();

    int auctionId = 0;
    ::Disassemble(packet, "d", &auctionId);

    int characterId = 0;
    std::int64_t biddingPrice = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&characterId);
    connection.Bind(&biddingPrice);
    constexpr const wchar_t* statement = L"{Call lin_LoadAllAuctionBid (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&auctionId);
    if (!connection.ExecutePrepared(statement)) {
        return false;
    }

    bool packetRemained = false;
    do {
        std::array<char, 0x5000> buffer{};
        int length = 0;
        int count = 0;
        while (count != 2000 && connection.Fetch()) {
            const int assembled = ::Assemble(
                buffer.data() + length, static_cast<int>(buffer.size()) - length, "dQ", characterId, biddingPrice
            );
            if (assembled > 0) {
                length += assembled;
                ++count;
            }
        }
        packetRemained = count == 2000;
        socket->Send("chdddb", 0xc6, 0x4b, auctionId, packetRemained ? 1 : 0, count, length, buffer.data());
    } while (packetRemained);
    return false;
}

bool RequestCreateItemAuction(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00555454.
    L2_GUARD_FUNCTION();

    int auctionNpc = 0;
    int startTime = 0;
    int endTime = 0;
    int itemIndex = 0;
    ::Disassemble(packet, "dddd", &auctionNpc, &startTime, &endTime, &itemIndex);

    int auctionId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&auctionId);
    constexpr const wchar_t* statement = L"{Call lin_CreateItemAuction (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&auctionNpc);
    connection.BindParameter(&startTime);
    connection.BindParameter(&endTime);
    connection.BindParameter(&itemIndex);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        socket->Send("chddddd", 0xc6, 0x4d, auctionId, auctionNpc, startTime, endTime, itemIndex);
    } else {
        socket->Send("chdd", 0xc6, 0x4d, 0, auctionNpc);
    }
    return false;
}

bool Request_RQ_EX_UPDATE_ITEM_AUCTION_INFO_FOR_L2ASSIST(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0058ab60. The request is the exact typed
    // protocol/dbpacket.h carrier, not a format-string packet.
    L2_GUARD_FUNCTION();

    protocol::RQ_EX_UPDATE_ITEM_AUCTION_INFO_FOR_L2ASSIST request;
    if (!DecodePacket(request, packet)) {
        return false;
    }

    if (request.currentItem.itemType != 0) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        if (!ExecuteItemAuctionInfoUpdate(
                connection, L"{Call lin_UpdateCurrentItemAuctionInfo (?, ?, ?, ?)}", request.auctionId, request.currentItem
            )) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[%s][%d][item_auction] "
            //     L"qsUpdateCurrentItemAuctionInfo failed",
            //     L"ServerPacketEx.cpp", 0x4b17
            // )
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d][item_auction] "
            //     L"qsUpdateCurrentItemAuctionInfo failed",
            //     L"ServerPacketEx.cpp", 0x4b17
            // )
            L2_LOG_ERROR(Log,
                "[auction] Database request to update current item information for auction %d failed. "
                "Remaining updates in this request will still be attempted.", request.auctionId
            );
        }
    }
    if (request.nextItem.itemType != 0) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        if (!ExecuteItemAuctionInfoUpdate(
                connection, L"{Call lin_UpdateNextItemAuctionInfo (?, ?, ?, ?)}", request.auctionId, request.nextItem
            )) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[%s][%d][item_auction] "
            //     L"qsUpdateNextItemAuctionInfo failed",
            //     L"ServerPacketEx.cpp", 0x4b20
            // )
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d][item_auction] "
            //     L"qsUpdateNextItemAuctionInfo failed",
            //     L"ServerPacketEx.cpp", 0x4b20
            // )
            L2_LOG_ERROR(Log,
                "[auction] Database request to update next item information for auction %d failed. "
                "The display-start update will still be attempted if supplied.", request.auctionId
            );
        }
    }
    if (request.nextAuctionDisplayStartTime != 0) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_UpdateNextAuctionDisplayStartTime (?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&request.auctionId);
        connection.BindParameter(&request.nextAuctionDisplayStartTime);
        if (!connection.ExecutePrepared(statement)) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[%s][%d][item_auction] "
            //     L"qsUpdateNextAuctionDisplayStart failed",
            //     L"ServerPacketEx.cpp", 0x4b29
            // )
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d][item_auction] "
            //     L"qsUpdateNextAuctionDisplayStart failed",
            //     L"ServerPacketEx.cpp", 0x4b29
            // )
            L2_LOG_ERROR(Log,
                "[auction] Database request to set next display-start time to %d for auction %d failed.",
                request.nextAuctionDisplayStartTime, request.auctionId
            );
        }
    }
    return false;
}

bool RequestSetItemAuctionEndTime(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00555644.
    L2_GUARD_FUNCTION();

    int auctionId = 0;
    int endTime = 0;
    int extendStatus = 0;
    ::Disassemble(packet, "ddd", &auctionId, &endTime, &extendStatus);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetItemAuctionEndTime (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&auctionId);
    connection.BindParameter(&endTime);
    connection.BindParameter(&extendStatus);
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestSetItemAuctionStatus(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x005557c4.
    L2_GUARD_FUNCTION();

    int auctionId = 0;
    int status = 0;
    ::Disassemble(packet, "dd", &auctionId, &status);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetItemAuctionStatus (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&auctionId);
    connection.BindParameter(&status);
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestSetItemAuctionInfo(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00555924. The shipping body discards auctionId
    // and binds only itemIndex to a two-placeholder statement.
    L2_GUARD_FUNCTION();

    int auctionId = 0;
    int itemIndex = 0;
    ::Disassemble(packet, "dd", &auctionId, &itemIndex);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetItemAuctionInfo (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&itemIndex);
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestManipulateWarehouseItem(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00547fa4.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    const unsigned char* const payload = ::Disassemble(packet, "d", &characterId);
    const int configuredWorldId = GetCacheDConfiguration().worldId;

    auto sendFailure = [&](int worldId) {
        int responseId = 0;
        int responseId2 = 0;
        ::Disassemble(payload, "dd", &responseId, &responseId2);
        socket->Send("chddddd", 0xc6, 0x4e, characterId, worldId, responseId2, 0, 0);
    };

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.AddT(LOG_ERROR, L"[%S][%d]Cannot find user", "ServerPacketEx.cpp", 0x10b1)
        // L2_LOG_ERROR(Log, L"[%S][%d]Cannot find user", "ServerPacketEx.cpp", 0x10b1)
        L2_LOG_ERROR(Log, "[warehouse] Cannot modify stored items: character %d unavailable. Sending failure reply for configured world %d.", characterId, configuredWorldId);
        sendFailure(configuredWorldId);
        return false;
    }

    CWareHouseSP warehouse = user->GetWareHouse(WareHouseType_CharWarehouse, true);
    if (!warehouse) {
        // Log.AddT(
        //     LOG_ERROR, L"[%S][%d]Cannot find inventory warehouse, char dbid[%d]", "ServerPacketEx.cpp", 0x10ba, characterId
        // )
        // L2_LOG_ERROR(Log, L"[%S][%d]Cannot find inventory warehouse, char dbid[%d]", "ServerPacketEx.cpp", 0x10ba, characterId
        // )
        L2_LOG_ERROR(Log, "[warehouse] Cannot modify stored items for character %d: personal warehouse unavailable. Sending failure reply.", characterId);
        sendFailure(user->GetWorld());
        return false;
    }

    int responseId = 0;
    int responseId2 = 0;
    std::vector<EtcMani*> etcManis;
    CPacketHelperEx<20480> itemPacket;
    CPacketHelperEx<20480> etcManiPacket;
    unsigned int changedItemCount = 0;
    std::uint64_t processedEtcManiCount = 0;
    {
        L2_WRITE_GUARD(*warehouse.operator->());
        const bool success = warehouse->ManipulateItems(
            socket, payload, responseId, responseId2, etcManis, itemPacket, etcManiPacket, changedItemCount,
            processedEtcManiCount
        );
        if (success) {
            socket->Send(
                "chdddddbb", 0xc6, 0x4e, characterId, user->GetWorld(), responseId2, static_cast<int>(processedEtcManiCount),
                static_cast<int>(changedItemCount), etcManiPacket.GetLength(), etcManiPacket.GetPacket(), itemPacket.GetLength(),
                itemPacket.GetPacket()
            );
        } else {
            socket->Send("chddddd", 0xc6, 0x4e, characterId, user->GetWorld(), responseId2, 0, 0);
        }
    }
    return false;
}

bool RequestConvertItem(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00539e68.
    L2_GUARD_FUNCTION();

    int userSocketId = 0;
    int characterId = 0;
    int itemId = 0;
    int newItemType = 0;
    ::Disassemble(packet, "dddd", &userSocketId, &characterId, &itemId, &newItemType);

    auto sendResult = [&](int convertedItemId) {
        socket->Send("chddd", 0xc6, 0x4f, userSocketId, convertedItemId, newItemType);
    };

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.AddT(LOG_ERROR, L"[%S][%d]Cannot find user[%d]", "ServerPacketEx.cpp", 0x10e6, characterId)
        // L2_LOG_ERROR(Log, L"[%S][%d]Cannot find user[%d]", "ServerPacketEx.cpp", 0x10e6, characterId)
        L2_LOG_ERROR(Log, "[items] Cannot convert item %d to type %d: character %d unavailable. Sending result item ID 0 to socket %d.", itemId, newItemType, characterId, userSocketId);
        sendResult(0);
        return false;
    }

    CWareHouseSP inventory = user->GetWareHouse(WareHouseType_Inventory, true);
    if (!inventory) {
        // Log.AddT(LOG_ERROR, L"[%S][%d]Cannot find warehouse", "ServerPacketEx.cpp", 0x10ee)
        // L2_LOG_ERROR(Log, L"[%S][%d]Cannot find warehouse", "ServerPacketEx.cpp", 0x10ee)
        L2_LOG_ERROR(Log, "[items] Cannot convert item %d to type %d for character %d: inventory unavailable. Sending result item ID 0.", itemId, newItemType, characterId);
        sendResult(0);
        return false;
    }

    std::optional<CItemSP> itemStorage;
    const bool converted = [&]() {
        L2_WRITE_GUARD(*inventory.operator->());
        CItemSP& item = itemStorage.emplace(inventory->GetItem(itemId));
        if (!item) {
            // Log.AddT(
            //     LOG_ERROR, L"[%S][%d]Cannot find item[%d] user[%d]", "ServerPacketEx.cpp", 0x10f7, itemId, characterId
            // )
            // L2_LOG_ERROR(Log, L"[%S][%d]Cannot find item[%d] user[%d]", "ServerPacketEx.cpp", 0x10f7, itemId, characterId
            // )
            L2_LOG_ERROR(Log, "[items] Cannot convert item %d to type %d for character %d: item missing from inventory. Sending result item ID 0.", itemId, newItemType, characterId);
            return false;
        }
        item->m_itemType = static_cast<std::uint32_t>(newItemType);
        item->Save(true);
        return true;
    }();
    if (!converted) {
        sendResult(0);
        return false;
    }
    sendResult(itemId);
    return false;
}

bool RequestChangeMergedName(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00555a5c.
    L2_GUARD_FUNCTION();

    int userSocketId = 0;
    int type = 0;
    int objectId = 0;
    std::array<char16_t, 24> newName{};
    ::Disassemble(packet, "dddS", &userSocketId, &type, &objectId, 0x30, newName.data());
    std::wstring hostNewName = Utf16ToWide(newName.data());

    auto changeCharacterName = [&](bool requireMergedState) {
        CUserSP user = g_userDb.GetUser(objectId, true, false, runtime::process::CurrentDatabaseId());
        if (!user) {
            // Log.AddT(
            //     LOG_ERROR, L"[%s][%d]Cannot find user, char id[%d]", L"ServerPacketEx.cpp",
            //     requireMergedState ? 0x1115 : 0x1157, objectId
            // )
            // L2_LOG_ERROR(Log, L"[%s][%d]Cannot find user, char id[%d]", L"ServerPacketEx.cpp",
            //     requireMergedState ? 0x1115 : 0x1157, objectId
            // )
            L2_LOG_ERROR(Log, "[names] Cannot rename character %d: character unavailable (merge state required: %s). No reply is sent to socket %d.",
                objectId, requireMergedState ? "yes" : "no", userSocketId);
            return;
        }
        if (requireMergedState && user->m_previousServer < 1) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[%s][%d] User is not merged Char, but request name "
            //     L"change. user[%s]",
            //     L"ServerPacketEx.cpp", 0x111b, user->GetCharName()
            // )
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] User is not merged Char, but request name "
            //     L"change. user[%s]",
            //     L"ServerPacketEx.cpp", 0x111b, user->GetCharName()
            // )
            L2_LOG_WARN(Log, "[names] Merge rename rejected for character %d: previous-server ID %d is below 1. No reply is sent to socket %d.",
                objectId, user->m_previousServer, userSocketId);
            return;
        }

        const std::u16string previousName = std::u16string(user->m_nameOrigin.data());
        if (!user->ChangeCharacternameExtern(newName.data())) {
            socket->Send("chdddS", 0xc6, 0x50, userSocketId, 0, 0, previousName.c_str());
            return;
        }

        if (requireMergedState) {
            user->m_previousServer = 0;
            user->m_nameOrigin.fill(L'\0');
        }
        socket->Send("chdddSd", 0xc6, 0x50, userSocketId, 0, objectId, newName.data(), user->GetPledgeID());
    };

    if (type == 0) {
        changeCharacterName(true);
        return false;
    }
    if (type == 2) {
        type = 0;
        changeCharacterName(false);
        return false;
    }
    if (type != 1) {
        // Log.AddT(LOG_ERROR, L"[%s][%d] Invalid type[%d]", L"ServerPacketEx.cpp", 0x1166, type)
        // L2_LOG_ERROR(Log, L"[%s][%d] Invalid type[%d]", L"ServerPacketEx.cpp", 0x1166, type)
        L2_LOG_WARN(Log, "[names] Rename rejected for object %d: unsupported type %d (expected 0, 1 or 2). No reply is sent to socket %d.", objectId, type, userSocketId);
        return false;
    }

    CPledgeSP pledge = g_pledgeDb.GetPledge(objectId, false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        // Log.AddT(LOG_ERROR, L"[%s][%d]Cannot find pledge, pledge id[%d]", L"ServerPacketEx.cpp", 0x112e, objectId)
        // L2_LOG_ERROR(Log, L"[%s][%d]Cannot find pledge, pledge id[%d]", L"ServerPacketEx.cpp", 0x112e, objectId)
        L2_LOG_ERROR(Log, "[names] Cannot rename merged clan %d: clan unavailable. No reply is sent to socket %d.", objectId, userSocketId);
        return false;
    }
    if (pledge->m_mergedPledgeId == 0) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[%s][%d] Pledge is not merged pledge, but request name "
        //     L"change. pledge[%s]",
        //     L"ServerPacketEx.cpp", 0x1134, pledgeName.c_str()
        // )
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Pledge is not merged pledge, but request name "
        //     L"change. pledge[%s]",
        //     L"ServerPacketEx.cpp", 0x1134, pledgeName.c_str()
        // )
        L2_LOG_WARN(Log, "[names] Merge rename rejected for clan %d: merged-clan ID is 0. No reply is sent to socket %d.", objectId, userSocketId);
        return false;
    }

    int result = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    constexpr const wchar_t* statement = L"{Call lin_SetNewNameForMerged (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&type);
    connection.BindParameter(&objectId);
    connection.BindParameter(hostNewName.data());
    if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
        socket->Send("chdddSd", 0xc6, 0x50, userSocketId, type, objectId, pledge->m_mergedPledgeName, 0);
        return false;
    }

    socket->Send("chdddSd", 0xc6, 0x50, userSocketId, type, objectId, newName.data(), result);

    CUserSP ruler = g_userDb.GetUser(static_cast<int>(pledge->m_rulerId), true, false, runtime::process::CurrentDatabaseId());
    const std::wstring rulerName = ruler ? Utf16ToWide(ruler->GetCharName()) : std::wstring();
    GIP._Add(
        L"CMD_ClanInfoData(%d, %d, '%s', %d, %d, %d, %d, %d, '%s'", GetCacheDConfiguration().worldId, objectId,
        hostNewName.c_str(), static_cast<std::int64_t>(std::time(nullptr)), pledge->m_skillLevel, pledge->m_nameValue,
        ruler ? ruler->GetAccountID() : 0, ruler ? ruler->GetId() : 0, rulerName.c_str()
    );
    return false;
}

bool RequestSetTutorial(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0053a358. Paired current L2Server
    // CDB::RequestSetTutorial 0x00656320 fixes the two-int request order.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int tutorialId = 0;
    ::Disassemble(packet, "dd", &characterId, &tutorialId);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.AddT(LOG_ERROR, L"[%s][%d]Cannot find user, char id[%d]", L"ServerPacketEx.cpp", 0x117b, characterId)
        // L2_LOG_ERROR(Log, L"[%s][%d]Cannot find user, char id[%d]", L"ServerPacketEx.cpp", 0x117b, characterId)
        L2_LOG_ERROR(Log, "[tutorial] Cannot set tutorial %d for character %d: character unavailable. Update skipped.", tutorialId, characterId);
        return false;
    }
    user->SetTutorial(tutorialId);
    return false;
}

bool RequestTutorialList(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0053a4c4. The second field is the opaque
    // L2Server user-socket ID echoed by reply 0x51.
    L2_GUARD_FUNCTION();

    int characterId;
    int userSocketId;
    ::Disassemble(packet, "dd", &characterId, &userSocketId);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.AddT(LOG_ERROR, L"[%s][%d]Cannot find user, char id[%d]", L"ServerPacketEx.cpp", 0x1192, characterId)
        // L2_LOG_ERROR(Log, L"[%s][%d]Cannot find user, char id[%d]", L"ServerPacketEx.cpp", 0x1192, characterId)
        L2_LOG_ERROR(Log, "[tutorial] Cannot load tutorial list for character %d: character unavailable. No reply is sent to socket %d.", characterId, userSocketId);
        return false;
    }
    user->SendTutorial(socket, userSocketId);
    return false;
}

bool RequestLoadInZoneRestriction(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00556490. Paired current L2Server request and
    // reply handler 0x00679bc0 fix character-ID/user-SID roles.
    L2_GUARD_FUNCTION();

    int characterId;
    int userServerId;
    int inZoneTypeId;
    int groupRestriction;
    int lastUse;
    int entranceCount;
    int maxEntranceCount;
    DBConn connection(runtime::process::CurrentDatabaseId());
    int rowCount = 0;
    ::Disassemble(packet, "dd", &characterId, &userServerId);

    connection.Bind(&inZoneTypeId);
    connection.Bind(&groupRestriction);
    connection.Bind(&lastUse);
    connection.Bind(&entranceCount);
    connection.Bind(&maxEntranceCount);
    std::array<char, 0x5000> rows{};
    constexpr const wchar_t* statement = L"{Call lin_LoadInZoneRestriction (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(statement))
        return false;

    int rowBytes = 0;
    while (connection.Fetch()) {
        rowBytes += ::Assemble(
            rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "ddddd", inZoneTypeId, lastUse, entranceCount,
            groupRestriction, maxEntranceCount
        );
        ++rowCount;
    }
    socket->Send("chddb", 0xc6, 0x52, userServerId, rowCount, rowBytes, rows.data());
    return false;
}

bool RequestMarkInZoneRestriction(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x005566e4. Current L2Server
    // User::MarkInZoneRestriction 0x00b43e0c fixes the seven
    // packet roles and the non-group history gate below.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int inZoneTypeId = 0;
    int lastUse = 0;
    int entranceCount = 0;
    int groupRestriction = 0;
    int maxEntranceCount = 0;
    int inZoneDatabaseId = 0;
    ::Disassemble(
        packet, "ddddddd", &characterId, &inZoneTypeId, &lastUse, &entranceCount, &groupRestriction, &maxEntranceCount,
        &inZoneDatabaseId
    );

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_MarkInZoneRestriction (?, ?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&inZoneTypeId);
    connection.BindParameter(&groupRestriction);
    connection.BindParameter(&lastUse);
    connection.BindParameter(&entranceCount);
    connection.BindParameter(&maxEntranceCount);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[%s][%d] Fail to mark inzone restriction. char[%d] "
        //     L"inzone[%d] group[%d] stamp[%d] count[%d], max[%d]",
        //     L"ServerPacketEx.cpp", 0x11ce, characterId, inZoneTypeId, groupRestriction, lastUse, entranceCount,
        //     maxEntranceCount
        // )
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Fail to mark inzone restriction. char[%d] "
        //     L"inzone[%d] group[%d] stamp[%d] count[%d], max[%d]",
        //     L"ServerPacketEx.cpp", 0x11ce, characterId, inZoneTypeId, groupRestriction, lastUse, entranceCount,
        //     maxEntranceCount
        // )
        L2_LOG_ERROR(Log,
            "[instances] Database restriction update failed for character %d, instance type %d: "
            "group %d, last use %d, entrance count %d, maximum %d. History is processed independently.",
            characterId, inZoneTypeId, groupRestriction, lastUse, entranceCount, maxEntranceCount
        );
    }

    if (inZoneDatabaseId > 0 && groupRestriction == 0) {
        DBConn historyConnection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* historyStatement = L"{Call lin_SaveInZoneResrictHistory (?, ?, ?, ?)}";
        historyConnection.Prepare(historyStatement);
        historyConnection.BindParameter(&characterId);
        historyConnection.BindParameter(&inZoneTypeId);
        historyConnection.BindParameter(&lastUse);
        historyConnection.BindParameter(&inZoneDatabaseId);
        if (!historyConnection.ExecutePrepared(historyStatement)) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[%s][%d] Fail to save inzone restrict history. "
            //     L"char[%d] inzone[%d] stamp[%d] inzoneDbId[%d]",
            //     L"ServerPacketEx.cpp", 0x11d8, characterId, inZoneTypeId, lastUse, inZoneDatabaseId
            // )
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Fail to save inzone restrict history. "
            //     L"char[%d] inzone[%d] stamp[%d] inzoneDbId[%d]",
            //     L"ServerPacketEx.cpp", 0x11d8, characterId, inZoneTypeId, lastUse, inZoneDatabaseId
            // )
            L2_LOG_ERROR(Log,
                "[instances] Database restriction-history save failed for character %d, instance type %d, "
                "instance ID %d, last use %d.", characterId, inZoneTypeId, inZoneDatabaseId, lastUse
            );
        }
    }
    return false;
}

bool RequestLoadUI(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0055707c. No packet is returned when the query has
    // no row or the stored size is 0/1, matching the shipping branch.
    L2_GUARD_FUNCTION();

    int characterId;
    int userSocketId;
    ::Disassemble(packet, "dd", &characterId, &userSocketId);

    int settingSize = 0;
    std::array<unsigned char, 8000> setting{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    std::int64_t settingIndicator = -1;
    connection.Bind(&settingSize);
    connection.BindBinaryNullable(setting.data(), static_cast<int>(setting.size()), &settingIndicator);
    constexpr const wchar_t* statement = L"{Call lin_GetUI (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (connection.ExecutePrepared(statement) && connection.Fetch() && settingSize > 1) {
        socket->Send("chddb", 0xc6, 0x53, userSocketId, settingSize, settingSize, setting.data());
    }
    return false;
}

bool RequestSaveUI(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00532f2c. The first two SQL arguments are rendered
    // into the statement and only the blob is bound. Preserve that unusual
    // ODBC shape instead of normalizing it into three placeholders.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int userSocketId = 0;
    int settingSize = 0;
    const unsigned char* cursor = ::Disassemble(packet, "ddd", &characterId, &userSocketId, &settingSize);

    if (static_cast<unsigned int>(settingSize) - 1U >= 8000U) {
        return false;
    }

    std::array<unsigned char, 8000> setting{};
    ::Disassemble(cursor, "b", settingSize, setting.data());

    std::array<wchar_t, 0x5000> query{};
    std::swprintf(query.data(), 0x4fff, L"{Call lin_SaveUI (%d, %d, ?)}", characterId, settingSize);

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Prepare(query.data());
    const std::int64_t settingIndicator = settingSize;
    connection.BindParameterBinary(setting.data(), static_cast<std::size_t>(settingSize), &settingIndicator);
    if (!connection.ExecutePrepared(query.data())) {
        // Log.Add(LOG_ERROR, query.data())
        // Pass SQL as data. Its percent signs must never become log directives.
        // Log.Add(LOG_ERROR,
        //     L"[UI settings] Cannot save %d bytes for character %d. SQL: \"%ls\"",
        //     settingSize, characterId, query.data())
        L2_LOG_ERROR(
            Log, u"[UI settings] Cannot save %d bytes for character %d. SQL: \"%ls\"", settingSize, characterId, query.data()
        );
    }
    return false;
}

bool RequestResetInZoneRestriction(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00556964.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    const int allInZones = -1;
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_ResetInZoneRestriction (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&allInZones);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR, L"[%s][%d] Fail to reset inzone restriction. char[%d]", L"ServerPacketEx.cpp", 0x11ec, characterId
        // )
        // L2_LOG_ERROR(Log, L"[%s][%d] Fail to reset inzone restriction. char[%d]", L"ServerPacketEx.cpp", 0x11ec, characterId
        // )
        L2_LOG_ERROR(Log,
            "[instances] Database request to reset all instance restrictions for character %d failed.", characterId
        );
    }
    return false;
}

bool RequestSaveInventorySlotIndex(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0053a644. Current L2Server emits the remaining
    // counted d/dd stream directly after this character ID.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.AddT(LOG_ERROR, L"[%s][%d]Cannot find user, char id[%d]", L"ServerPacketEx.cpp", 0x12de, characterId)
        // L2_LOG_ERROR(Log, L"[%s][%d]Cannot find user, char id[%d]", L"ServerPacketEx.cpp", 0x12de, characterId)
        L2_LOG_ERROR(Log, "[inventory] Cannot save slot order for character %d: character unavailable. Save skipped.", characterId);
        return false;
    }

    CWareHouseSP inventory = user->GetWareHouse(WareHouseType_Inventory, true);
    if (!inventory) {
        // Log.AddT(
        //     LOG_ERROR, L"[%s][%d]Cannot find inventory warehouse, char dbid[%d]", L"ServerPacketEx.cpp", 0x12e5, characterId
        // )
        // L2_LOG_ERROR(Log, L"[%s][%d]Cannot find inventory warehouse, char dbid[%d]", L"ServerPacketEx.cpp", 0x12e5, characterId
        // )
        L2_LOG_ERROR(Log, "[inventory] Cannot save slot order for character %d: inventory unavailable. Save skipped.", characterId);
        return false;
    }
    inventory->SaveInventorySlotIndex(cursor);
    return false;
}

bool RequestSetCastleSiegeDefenceCount(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0053a878. Paired current L2Server
    // CDB::UpdateDefenceSiegeCount 0x00653920 fixes world/pledge/count order.
    // the first value is decoded but unused by CacheD.
    L2_GUARD_FUNCTION();

    int worldId;
    int pledgeId;
    int siegeDefenceCount;
    ::Disassemble(packet, "ddd", &worldId, &pledgeId, &siegeDefenceCount);

    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (pledge && pledge->SetCastleSiegeDefenceCount(siegeDefenceCount)) {
        socket->Send("chdd", 0xc6, 0x54, pledgeId, siegeDefenceCount);
    }
    return false;
}

bool RequestSetAssociatedInZone(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00557274 and paired current L2Server
    // CDB::RequestSetAssociatedInZone 0x00656df8.
    L2_GUARD_FUNCTION();

    std::uint32_t characterId = 0;
    std::uint32_t inZoneId = 0;
    std::uint32_t inZoneUid = 0;
    ::Disassemble(packet, "ddd", &characterId, &inZoneId, &inZoneUid);

    CUserSP user = g_userDb.GetUser(static_cast<int>(characterId), true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.AddT(LOG_ERROR, L"[%s][%d] Cannot find user, char id[%d]", L"ServerPacketEx.cpp", 0x1316, characterId)
        // L2_LOG_ERROR(Log, L"[%s][%d] Cannot find user, char id[%d]", L"ServerPacketEx.cpp", 0x1316, characterId)
        L2_LOG_ERROR(Log, "[instances] Cannot associate character %u with zone %u, instance %u: character unavailable. Update skipped.",
            static_cast<unsigned int>(characterId), static_cast<unsigned int>(inZoneId), static_cast<unsigned int>(inZoneUid));
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetAssociatedInZone (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&inZoneId);
    connection.BindParameter(&inZoneUid);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR, L"[%s][%d] Fail to set associated instant zone, user[%s]", L"ServerPacketEx.cpp", 0x1322,
        //     user->GetCharName()
        // )
        // L2_LOG_ERROR(Log, L"[%s][%d] Fail to set associated instant zone, user[%s]", L"ServerPacketEx.cpp", 0x1322,
        //     user->GetCharName()
        // )
        L2_LOG_ERROR(Log, "[instances] Cannot associate character %u with zone %u, instance %u: database operation failed. Cached association is unchanged.",
            static_cast<unsigned int>(characterId), static_cast<unsigned int>(inZoneId), static_cast<unsigned int>(inZoneUid));
        return false;
    }

    user->m_loadedInfo.associatedInzone = inZoneId;
    user->m_loadedInfo.associatedInzoneUid = static_cast<int>(inZoneUid);
    return false;
}

bool RequestWaitingTransferPledgeMaster(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0055759c.
    L2_GUARD_FUNCTION();

    int pledgeId = 0;
    ::Disassemble(packet, "d", &pledgeId);

    int waitingCount = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&waitingCount);
    constexpr const wchar_t* statement = L"{Call lin_GetWaitingTransferPledgeMaster (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledgeId);
    if (connection.ExecutePrepared(statement) && connection.Fetch() && waitingCount > 0) {
        socket->Send("chd", 0xc6, 0x55, pledgeId);
    }
    return false;
}

bool RequestDeleteSkillList(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00557738. The current paired L2Server routes its
    // newer delete-and-save request through slot 282. This registered legacy
    // production path remains recovered from its own current CacheD body.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    std::array<char16_t, 2048> skillList{};
    ::Disassemble(packet, "dS", &characterId, 0x1000, skillList.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    const int subjobId = user->GetSubjobId();
    const std::u16string hostSkillList(skillList.data());
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeleteSkillList (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&subjobId);
    connection.BindParameter(hostSkillList.c_str());
    if (connection.ExecutePrepared(statement)) {
        user->DeleteAcquireSkillList(skillList.data());
    }
    return false;
}

bool RequestSaveUserPoint(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00534074.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int pointType = 0;
    int newPoint = 0;
    int recentPoint = 0;
    ::Disassemble(packet, "dddd", &characterId, &pointType, &newPoint, &recentPoint);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, L"User point is not saved. User is invalid. [%d] Type[%d]", characterId, pointType)
        // L2_LOG_ERROR(Log, L"User point is not saved. User is invalid. [%d] Type[%d]", characterId, pointType)
        // Previous: L2_LOG_ERROR(Log, "Cannot save points for character %d: character unavailable, point type %d, requested value %d.", characterId, pointType, newPoint)
        L2_LOG_ERROR(Log, "[character points] Cannot save points for character %d: the character is unavailable; point type %d, requested value %d.", characterId, pointType, newPoint);
        return false;
    }

    const int currentPoint = user->LoadUserPoint(pointType);
    if (currentPoint != recentPoint) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"recent user point is not equal to current user point. "
        //     L"[%d] [%d -> %d]. change to [%d] type[%d]",
        //     characterId, currentPoint, recentPoint, newPoint, pointType
        // )
        // L2_LOG_ERROR(Log,
        //     L"recent user point is not equal to current user point. "
        //     L"[%d] [%d -> %d]. change to [%d] type[%d]",
        //     characterId, currentPoint, recentPoint, newPoint, pointType
        // )
        // Previous: L2_LOG_WARN(Log, "Point mismatch for character %d, type %d: loaded value %d, request's previous value %d. Still attempting to save requested value %d.", characterId, pointType, currentPoint, recentPoint, newPoint)
        L2_LOG_WARN(Log, "[character points] Point mismatch for character %d, type %d: loaded value %d, request's previous value %d. Still attempting to save requested value %d.", characterId, pointType, currentPoint, recentPoint, newPoint);
    }
    user->SaveUserPoint(pointType, newPoint);
    if ((pointType == 5 || pointType == 8) && newPoint < 0) {
        // Log.Add(
        //     LOG_ERROR, L"pvp/raid point set to zero. type[%d] User [%s], now [%d]", pointType, user->GetCharName(), newPoint
        // )
        // L2_LOG_ERROR(Log, L"pvp/raid point set to zero. type[%d] User [%s], now [%d]", pointType, user->GetCharName(), newPoint
        // )
        // Previous: L2_LOG_WARN(Log, "Negative PvP/raid point value %d submitted for character %d, type %d. Save was attempted, final stored value was not read back.", newPoint, characterId, pointType)
        L2_LOG_WARN(Log, "[character points] Negative PvP/raid value %d was submitted for character %d, type %d. The save was attempted; the final stored value was not read back.", newPoint, characterId, pointType);
    }
    return false;
}

bool RequestLoadUserPoint(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00533ec0.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int pointType = 0;
    ::Disassemble(packet, "dd", &characterId, &pointType);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    const int point = user ? user->LoadUserPoint(pointType) : -1;
    socket->Send("chddd", 0xc6, 0x56, characterId, pointType, point);
    return false;
}

bool RequestSavePVPPointRestrain(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x005342e8.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int giveCount = 0;
    int giveTime = 0;
    ::Disassemble(packet, "ddd", &characterId, &giveCount, &giveTime);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, L"User Not Valid! [%d]", characterId)
        // L2_LOG_ERROR(Log, L"User Not Valid! [%d]", characterId)
        L2_LOG_ERROR(Log, "[PvP] Cannot save point restrictions for character %d: character unavailable (give count %d, give time %d).", characterId, giveCount, giveTime);
    } else if (!user->SavePVPPointRestrain(giveCount, giveTime)) {
        // Log.Add(LOG_ERROR, L"PVP restrain save failed! [%d]", characterId)
        // L2_LOG_ERROR(Log, L"PVP restrain save failed! [%d]", characterId)
        L2_LOG_ERROR(Log, "[PvP] Point-restriction save failed for character %d (give count %d, give time %d).", characterId, giveCount, giveTime);
    }
    return false;
}

bool RequestLoadPVPPointRestrain(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00534478.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, L"User not valid. PVP restrain load failed! [%d]", characterId)
        // L2_LOG_ERROR(Log, L"User not valid. PVP restrain load failed! [%d]", characterId)
        L2_LOG_ERROR(Log, "[PvP] Cannot load point restrictions for character %d: character unavailable. No reply is sent.", characterId);
    } else {
        user->LoadPVPPointRestrain(socket);
    }
    return false;
}

bool RequestSaveLastColor(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0053b410.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int colorIndex = 0;
    int savedTime = 0;
    ::Disassemble(packet, "ddd", &characterId, &colorIndex, &savedTime);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        user->SaveLastColor(colorIndex, savedTime);
    }
    return false;
}

bool RequestLoadLastColor(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0053b2e8.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        user->LoadLastColor(socket);
    }
    return false;
}

bool RequestSaveNRMemo(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00558014 / ServerPacketEx.cpp:5280-5291.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    int characterId = 0;
    MemoStruct memo;
    ::Disassemble(
        packet, "ddddddd", &characterId, &memo.nQuestID, &memo.nJournal, &memo.nState, &memo.nState2, &memo.nState3,
        &memo.nState4
    );

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.AddT(LOG_ERROR, L"[%s][%d] Cannot find user, char id[%d]", L"ServerPacketEx.cpp", 0x14a0, characterId)
        // L2_LOG_ERROR(Log, L"[%s][%d] Cannot find user, char id[%d]", L"ServerPacketEx.cpp", 0x14a0, characterId)
        L2_LOG_ERROR(Log, "[quests] Cannot save NR memo for quest %d, character %d: character unavailable. Save and audit skipped.", memo.nQuestID, characterId);
        return false;
    }

    {
        L2_WRITE_GUARD(*user.operator->());
        constexpr const wchar_t* statement = L"{call lin_UpdateUserQuest (?, ?, ?, ?, ?, ?, ?, 1)}";
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.BindParameter(&memo.nQuestID);
        connection.BindParameter(&memo.nJournal);
        connection.BindParameter(&memo.nState);
        connection.BindParameter(&memo.nState2);
        connection.BindParameter(&memo.nState3);
        connection.BindParameter(&memo.nState4);
        if (!connection.ExecutePrepared(statement)) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[%s][%d] Fail to save NRMemo. char[%d] Quest ID[%d] "
            //     L"state1 [%d] state2 [%d] journal [%d]",
            //     L"ServerPacketEx.cpp", 0x14aa, characterId, memo.nQuestID, memo.nState, memo.nState2, memo.nJournal
            // )
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Fail to save NRMemo. char[%d] Quest ID[%d] "
            //     L"state1 [%d] state2 [%d] journal [%d]",
            //     L"ServerPacketEx.cpp", 0x14aa, characterId, memo.nQuestID, memo.nState, memo.nState2, memo.nJournal
            // )
            L2_LOG_ERROR(Log, "[quests] NR memo save failed for character %d, quest %d: database operation failed (journal %d, states %d, %d, %d, %d). Audit will still be written.",
                characterId, memo.nQuestID, memo.nJournal, memo.nState, memo.nState2, memo.nState3, memo.nState4);
        }
        WriteNRMemoAudit(*user.get(), memo.nQuestID, memo.nJournal);
    }
    return false;
}

bool RequestLoadNRMemo(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00557cac / ServerPacketEx.cpp:5222-5250.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.AddT(LOG_ERROR, L"[%s][%d] Cannot find user, char id[%d]", L"ServerPacketEx.cpp", 0x1466, characterId)
        // L2_LOG_ERROR(Log, L"[%s][%d] Cannot find user, char id[%d]", L"ServerPacketEx.cpp", 0x1466, characterId)
        L2_LOG_ERROR(Log, "[quests] Cannot load NR memos for character %d: character unavailable. No reply is sent.", characterId);
        return false;
    }

    int questId = 0;
    int journal = 0;
    int state1 = 0;
    int state2 = 0;
    int state3 = 0;
    int state4 = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&questId);
    connection.Bind(&journal);
    connection.Bind(&state1);
    connection.Bind(&state2);
    connection.Bind(&state3);
    connection.Bind(&state4);
    constexpr const wchar_t* statement = L"{call lin_SelectUserQuest (?,1,0)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(statement)) {
        return false;
    }

    std::array<char, 0x5000> rows{};
    int rowBytes = 0;
    int rowCount = 0;
    do {
        if (!connection.Fetch())
            break;
        rowBytes += ::Assemble(
            rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dddddd", questId, journal, state1, state2,
            state3, state4
        );
        ++rowCount;
    } while (rowCount < 0x15);

    if (rowCount >= 0x15) {
        // Log.AddT(
        //     LOG_ERROR, L"[%s][%d] [%d] character has too many unregistered quest.", L"ServerPacketEx.cpp", 0x1482,
        //     characterId
        // )
        // L2_LOG_ERROR(Log, L"[%s][%d] [%d] character has too many unregistered quest.", L"ServerPacketEx.cpp", 0x1482,
        //     characterId
        // )
        L2_LOG_WARN(Log, "[quests] NR memo load reached the 21-row limit for character %d. Sending %d rows; additional rows were not checked.", characterId, rowCount);
    }
    socket->Send("chddb", 0xc6, 0x5a, characterId, rowCount, rowBytes, rows.data());
    return false;
}

bool RequestDeleteNRMemo(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0055851c / ServerPacketEx.cpp:5316-5325.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int questId = 0;
    ::Disassemble(packet, "dd", &characterId, &questId);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{call lin_DeleteUserQuest (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&questId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR, L"[%s][%d] Fail to delete NRMemo. char[%d] Quest ID[%d]", L"ServerPacketEx.cpp", 0x14c4, characterId,
        //     questId
        // )
        // L2_LOG_ERROR(Log, L"[%s][%d] Fail to delete NRMemo. char[%d] Quest ID[%d]", L"ServerPacketEx.cpp", 0x14c4, characterId,
        //     questId
        // )
        L2_LOG_ERROR(Log, "[quests] Cannot delete NR memo for character %d, quest %d: database operation failed.", characterId, questId);
    }
    return false;
}

bool RequestSavePremiumItemForChar(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005586c0 / ServerPacketEx.cpp:5337-5377.
    L2_GUARD_FUNCTION();

    int recipientAccountId = 0;
    int recipientCharacterId = 0;
    std::array<char16_t, 24> recipientCharacterName{};
    int buyerAccountId = 0;
    int buyerCharacterId = 0;
    std::array<char16_t, 24> buyerCharacterName{};
    int itemCount = 0;
    const unsigned char* cursor = ::Disassemble(
        packet, "ddSddSd", &recipientAccountId, &recipientCharacterId, 0x30, recipientCharacterName.data(), &buyerAccountId,
        &buyerCharacterId, 0x30, buyerCharacterName.data(), &itemCount
    );

    const std::wstring recipientSqlName = Utf16ToWide(recipientCharacterName.data());
    const std::wstring buyerSqlName = Utf16ToWide(buyerCharacterName.data());
    const std::u16string_view recipientPacketName(recipientCharacterName.data());
    const std::u16string_view buyerPacketName(buyerCharacterName.data());

    DBConn connection(runtime::process::CurrentDatabaseId());
    CPacketHelperEx<20480> response;
    int savedCount = 0;
    for (int index = 0; index < itemCount; ++index) {
        long long warehouseNo = 0;
        int itemType = 0;
        long long itemAmount = 0;
        cursor = ::Disassemble(cursor, "QdQ", &warehouseNo, &itemType, &itemAmount);

        int result = -1;
        connection.Bind(&result);
        const bool saved = ExecuteSaveUserPremiumItem(
                               connection, warehouseNo, buyerAccountId, buyerCharacterId, buyerSqlName.c_str(),
                               recipientAccountId, recipientCharacterId, recipientSqlName.c_str(), itemType, itemAmount
                           ) &&
            connection.Fetch() && result == 0;
        if (saved) {
            response.Assemble("QdQd", warehouseNo, itemType, itemAmount, buyerAccountId != recipientAccountId);
            ++savedCount;
            WritePremiumDeliveryAudit(
                recipientCharacterId, recipientAccountId, buyerCharacterId, buyerAccountId, warehouseNo, itemType,
                itemAmount, recipientPacketName, buyerPacketName
            );
        } else {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[%s][%d] fail to save user_premium_item. "
            //     L"warehouseNo[%lld], buyer[%s], recipient[%s], "
            //     L"item_id[%d], item_amount[%lld]",
            //     L"ServerPacketEx.cpp", 0x14f4, warehouseNo, buyerSqlName.c_str(), recipientSqlName.c_str(), itemType,
            //     itemAmount
            // )
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] fail to save user_premium_item. "
            //     L"warehouseNo[%lld], buyer[%s], recipient[%s], "
            //     L"item_id[%d], item_amount[%lld]",
            //     L"ServerPacketEx.cpp", 0x14f4, warehouseNo, buyerSqlName.c_str(), recipientSqlName.c_str(), itemType,
            //     itemAmount
            // )
            // Previous: L2_LOG_ERROR(Log, "Premium item save was not confirmed: warehouse %lld, item %d, amount %lld, buyer character %d/account %d, recipient character %d/account %d, entry %d of %d. Entry omitted from delivery reply, continuing batch.",
            //     warehouseNo, itemType, itemAmount, buyerCharacterId, buyerAccountId,
            //     recipientCharacterId, recipientAccountId, index + 1, itemCount)
            L2_LOG_ERROR(Log, "[premium items] Save was not confirmed: warehouse %lld, item %d, amount %lld, buyer character %d/account %d, recipient character %d/account %d, entry %d of %d. The entry is omitted from the delivery reply; continuing the batch.",
                warehouseNo, itemType, itemAmount, buyerCharacterId, buyerAccountId,
                recipientCharacterId, recipientAccountId, index + 1, itemCount);
        }
        connection.UnBind();
    }

    if (savedCount > 0) {
        socket->Send(
            "chdSdb", 0xc6, 0x5d, recipientAccountId, buyerCharacterName.data(), savedCount, response.GetLength(),
            response.GetPacket()
        );
    }
    return false;
}

bool RequestSavePremiumItemForServer(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00558ae4 / ServerPacketEx.cpp:5388-5426.
    L2_GUARD_FUNCTION();

    int itemCount = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &itemCount);
    DBConn connection(runtime::process::CurrentDatabaseId());
    for (int index = 0; index < itemCount; ++index) {
        long long warehouseNo = 0;
        int itemType = 0;
        long long itemAmount = 0;
        int buyerAccountId = 0;
        int buyerCharacterId = 0;
        std::array<char16_t, 24> buyerCharacterName{};
        int recipientAccountId = 0;
        int recipientCharacterId = 0;
        std::array<char16_t, 24> recipientCharacterName{};
        cursor = ::Disassemble(
            cursor, "QdQddSddS", &warehouseNo, &itemType, &itemAmount, &buyerAccountId, &buyerCharacterId, 0x30,
            buyerCharacterName.data(), &recipientAccountId, &recipientCharacterId, 0x30, recipientCharacterName.data()
        );

        const std::wstring buyerSqlName = Utf16ToWide(buyerCharacterName.data());
        const std::wstring recipientSqlName = Utf16ToWide(recipientCharacterName.data());
        int result = -1;
        connection.Bind(&result);
        const bool saved = ExecuteSaveUserPremiumItem(
                               connection, warehouseNo, buyerAccountId, buyerCharacterId, buyerSqlName.c_str(),
                               recipientAccountId, recipientCharacterId, recipientSqlName.c_str(), itemType, itemAmount
                           ) &&
            connection.Fetch() && result == 0;
        if (saved) {
            socket->Send(
                "chdSdQdQd", 0xc6, 0x5d, recipientAccountId, buyerCharacterName.data(), 1, warehouseNo, itemType, itemAmount,
                buyerAccountId != recipientAccountId
            );
            WritePremiumDeliveryAudit(
                recipientCharacterId, recipientAccountId, buyerCharacterId, buyerAccountId, warehouseNo, itemType,
                itemAmount, std::u16string_view(recipientCharacterName.data()),
                std::u16string_view(buyerCharacterName.data())
            );
        } else {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[%s][%d] fail to save user_premium_item. "
            //     L"warehouseNo[%lld], buyer[%s], recipient[%s], "
            //     L"item_id[%d], item_amount[%lld]",
            //     L"ServerPacketEx.cpp", 0x1532, warehouseNo, buyerSqlName.c_str(), recipientSqlName.c_str(), itemType,
            //     itemAmount
            // )
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] fail to save user_premium_item. "
            //     L"warehouseNo[%lld], buyer[%s], recipient[%s], "
            //     L"item_id[%d], item_amount[%lld]",
            //     L"ServerPacketEx.cpp", 0x1532, warehouseNo, buyerSqlName.c_str(), recipientSqlName.c_str(), itemType,
            //     itemAmount
            // )
            // Previous: L2_LOG_ERROR(Log, "Premium item save was not confirmed: warehouse %lld, item %d, amount %lld, buyer character %d/account %d, recipient character %d/account %d, entry %d of %d. No delivery reply for this entry, continuing batch.",
            //     warehouseNo, itemType, itemAmount, buyerCharacterId, buyerAccountId,
            //     recipientCharacterId, recipientAccountId, index + 1, itemCount)
            L2_LOG_ERROR(Log, "[premium items] Save was not confirmed: warehouse %lld, item %d, amount %lld, buyer character %d/account %d, recipient character %d/account %d, entry %d of %d. No delivery reply will be sent for this entry; continuing the batch.",
                warehouseNo, itemType, itemAmount, buyerCharacterId, buyerAccountId,
                recipientCharacterId, recipientAccountId, index + 1, itemCount);
        }
        connection.UnBind();
    }
    return false;
}

bool RequestLoadPremiumItem(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00558ea4 / ServerPacketEx.cpp:5448-5483.
    L2_GUARD_FUNCTION();

    int userSocketId = 0;
    int accountId = 0;
    ::Disassemble(packet, "dd", &userSocketId, &accountId);

    long long warehouseNo = 0;
    int itemType = 0;
    long long itemRemain = 0;
    int buyerAccountId = 0;
    std::array<char16_t, 24> buyerCharacterName{};
    std::int64_t buyerNameIndicator = -1;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&warehouseNo);
    connection.Bind(&itemType);
    connection.Bind(&itemRemain);
    connection.Bind(&buyerAccountId);
    connection.BindNullable(buyerCharacterName.data(), 0x30, &buyerNameIndicator);
    constexpr const wchar_t* statement = L"{Call lin_LoadUserPremiumItem (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&accountId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR, L"[%s][%d] fail to load premium item, account[%d]", L"ServerPacketEx.cpp", 0x1562, accountId
        // )
        // L2_LOG_ERROR(Log, L"[%s][%d] fail to load premium item, account[%d]", L"ServerPacketEx.cpp", 0x1562, accountId
        // )
        L2_LOG_ERROR(Log,
            "[premium] Database request to load items for account %d, socket %d failed. No item pages will be sent.",
            accountId, userSocketId
        );
        return false;
    }

    int page = 0;
    bool fullPage = false;
    do {
        std::array<char, 0x5000> rows{};
        int rowBytes = 0;
        int rowCount = 0;
        fullPage = false;
        while (connection.Fetch()) {
            const std::u16string buyerPacketName =
                buyerNameIndicator < 0 ? std::u16string{} : std::u16string(buyerCharacterName.data());
            rowBytes += ::Assemble(
                rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "QdQdS", warehouseNo, itemType, itemRemain,
                buyerAccountId, buyerPacketName.c_str()
            );
            ++rowCount;
            if (rowCount == 200) {
                fullPage = true;
                break;
            }
        }

        ++page;
        if (rowCount > 0) {
            socket->Send("chdddb", 0xc6, 0x5e, userSocketId, page, rowCount, rowBytes, rows.data());
        } else {
            socket->Send("chddd", 0xc6, 0x5e, userSocketId, page, 0);
        }
    } while (fullPage);
    return false;
}

namespace UserBookmarkJobHandler {

void OnLoadInfo(CSocket* socket, int characterId, int userSocketId) {
    // Current Cached.exe 0x0055ae20.
    L2_GUARD_FUNCTION();

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return;

    int slotId = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    int iconId = 0;
    std::array<char16_t, 25> slotTitle{};
    std::array<char16_t, 25> iconTitle{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&slotId);
    connection.Bind(&x);
    connection.Bind(&y);
    connection.Bind(&z);
    connection.Bind(&iconId);
    connection.Bind(slotTitle.data(), 0x32);
    connection.Bind(iconTitle.data(), 0x32);
    constexpr const wchar_t* statement = L"{Call lin_LoadUserBookmark (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(statement))
        return;

    std::array<char, 0x5000> rows{};
    int rowBytes = 0;
    int rowCount = 0;
    while (connection.Fetch()) {
        rowBytes += ::Assemble(
            rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dddddSS", slotId, x, y, z, iconId,
            slotTitle.data(), iconTitle.data()
        );
        ++rowCount;
    }

    if (rowCount > 0) {
        socket->Send("chdddb", 0xc6, 0x5b, userSocketId, 0, rowCount, rowBytes, rows.data());
    } else {
        socket->Send("chddd", 0xc6, 0x5b, userSocketId, 0, 0);
    }
}

void OnAddSlot(int characterId, int slotCount) {
    // Current Cached.exe 0x0055b124 updates the in-memory value even when the
    // SQL execution fails.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_AddUserBookmarkSlot (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&slotCount);
    connection.ExecutePrepared(statement);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        user->m_loadedInfo.bookmarkSlot += slotCount;
    }
}

void OnSaveSlot(int characterId, int slotId, int x, int y, int z, int iconId, wchar_t* slotTitle, wchar_t* iconTitle) {
    // Current Cached.exe 0x0055b2c4.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveUserBookmarkSlot (?, ?, ?, ?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&slotId);
    connection.BindParameter(&x);
    connection.BindParameter(&y);
    connection.BindParameter(&z);
    connection.BindParameter(&iconId);
    connection.BindParameter(slotTitle);
    connection.BindParameter(iconTitle);
    connection.ExecutePrepared(statement);
}

void OnDeleteSlot(int characterId, int slotId) {
    // Current Cached.exe 0x0055b410.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeleteUserBookmarkSlot (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&slotId);
    connection.ExecutePrepared(statement);
}

void OnChangeSlot(int characterId, int oldSlotId, int newSlotId) {
    // Current Cached.exe 0x0055b53c.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_ChangeUserBookmarkSlot (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&oldSlotId);
    connection.BindParameter(&newSlotId);
    connection.ExecutePrepared(statement);
}

} // namespace UserBookmarkJobHandler

bool RequestUserBookmarkJob(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0055b68c.
    L2_GUARD_FUNCTION();

    int job = 0;
    int characterId = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &job, &characterId);
    switch (job) {
    case 0: {
        int userSocketId = 0;
        ::Disassemble(cursor, "d", &userSocketId);
        UserBookmarkJobHandler::OnLoadInfo(socket, characterId, userSocketId);
        break;
    }
    case 1: {
        int slotCount = 0;
        ::Disassemble(cursor, "d", &slotCount);
        UserBookmarkJobHandler::OnAddSlot(characterId, slotCount);
        break;
    }
    case 2: {
        int slotId = 0;
        int x = 0;
        int y = 0;
        int z = 0;
        int iconId = 0;
        std::array<char16_t, 65> slotTitle{};
        std::array<char16_t, 65> iconTitle{};
        ::Disassemble(cursor, "dddddSS", &slotId, &x, &y, &z, &iconId, 0x82, slotTitle.data(), 0x82, iconTitle.data());
        std::wstring hostSlotTitle = Utf16ToWide(slotTitle.data());
        std::wstring hostIconTitle = Utf16ToWide(iconTitle.data());
        UserBookmarkJobHandler::OnSaveSlot(characterId, slotId, x, y, z, iconId, hostSlotTitle.data(), hostIconTitle.data());
        break;
    }
    case 3: {
        int slotId = 0;
        ::Disassemble(cursor, "d", &slotId);
        UserBookmarkJobHandler::OnDeleteSlot(characterId, slotId);
        break;
    }
    case 4: {
        int oldSlotId = 0;
        int newSlotId = 0;
        ::Disassemble(cursor, "dd", &oldSlotId, &newSlotId);
        UserBookmarkJobHandler::OnChangeSlot(characterId, oldSlotId, newSlotId);
        break;
    }
    default:
        break;
    }
    return false;
}

namespace PeriodicItemJobHandler {

void OnLoadInfo(CSocket* socket, int characterId, int userSocketId) {
    // Current Cached.exe 0x0055bd10.
    L2_GUARD_FUNCTION();

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return;

    int itemId = 0;
    int startTime = 0;
    int period = 0;
    int itemType = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&itemId);
    connection.Bind(&startTime);
    connection.Bind(&period);
    connection.Bind(&itemType);
    constexpr const wchar_t* statement = L"{Call lin_LoadPeriodicItems (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(statement))
        return;

    const std::time_t hostNow = std::time(nullptr);
    const int now = hostNow < 0 || hostNow > std::numeric_limits<std::int32_t>::max() ? -1 : static_cast<int>(hostNow);
    bool pageFull = false;
    do {
        std::array<char, 0x2004> allRows{};
        std::array<char, 0x2004> expiredRows{};
        int allBytes = 0;
        int allCount = 0;
        int expiredBytes = 0;
        int expiredCount = 0;
        pageFull = false;
        for (;;) {
            if (!connection.Fetch())
                break;
            if (runtime::AddInt32Wrapping(startTime, period) <= now) {
                expiredBytes += ::Assemble(
                    expiredRows.data() + expiredBytes, static_cast<int>(expiredRows.size()) - expiredBytes, "dd", itemId,
                    itemType
                );
                ++expiredCount;
            }
            allBytes += ::Assemble(
                allRows.data() + allBytes, static_cast<int>(allRows.size()) - allBytes, "dddd", itemId, startTime, period,
                itemType
            );
            ++allCount;
            if (expiredCount + allCount == 1000) {
                pageFull = true;
                break;
            }
        }

        socket->Send(
            "chdddbdb", 0xc6, 0x5c, 0, userSocketId, allCount, allBytes, allRows.data(), expiredCount, expiredBytes,
            expiredRows.data()
        );
    } while (pageFull);
}

void OnRegister(int itemId, int startTime, int period, int itemType) {
    // Current Cached.exe 0x0055c010. The period precedes item type in both
    // this source signature and the SQL ABI.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_RegisterPeriodicItem (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&itemId);
    connection.BindParameter(&startTime);
    connection.BindParameter(&period);
    connection.BindParameter(&itemType);
    connection.ExecutePrepared(statement);
}

void OnExpired(CSocket* socket, int itemId, int consumeType, int characterId, int userServerId, int itemType) {
    // Current Cached.exe 0x0055c120. Paired current L2Server
    // CDB::RemoveExpiredPeriodicItem proves the five post-socket roles.
    L2_GUARD_FUNCTION();

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        GuardHelper missingUserFrame(L"periodic_item_expired_but_cannot_find_user", CThreadManager::GetThreadID());
        const bool deleted = DeleteItemDirectly(itemId);
        // Log.AddT(
        //     LOG_ERROR, L"[%S][%d]Cannot find user[%d] (item id[%d] deleted?[%d])", "ServerPacketEx.cpp", 0x1a13, characterId,
        //     itemId, deleted ? 1 : 0
        // )
        // L2_LOG_ERROR(Log, L"[%S][%d]Cannot find user[%d] (item id[%d] deleted?[%d])", "ServerPacketEx.cpp", 0x1a13, characterId,
        //     itemId, deleted ? 1 : 0
        // )
        L2_LOG_ERROR(Log,
            "[items] Cannot remove expired item %d through character %d: character unavailable. "
            "Direct database deletion %s; no reply will be sent.",
            itemId, characterId, deleted ? "confirmed" : "not confirmed"
        );
        if (deleted) {
            LogPeriodicPremiumItemDeletion(0, characterId, itemId, static_cast<unsigned int>(itemType));
        }
        return;
    }

    bool deleted = false;
    const bool useCharacterWarehouse = consumeType < 1;
    const bool canUseWarehouse = !useCharacterWarehouse || user->IsLoadedWareHouse(WareHouseType_CharWarehouse);
    if (!canUseWarehouse) {
        deleted = DeleteItemDirectly(itemId);
    } else {
        CWareHouseSP warehouse =
            user->GetWareHouse(useCharacterWarehouse ? WareHouseType_CharWarehouse : WareHouseType_Inventory, true);
        if (!warehouse) {
            // Log.AddT(
            //     LOG_ERROR, L"[%S][%d]Cannot find warehouse [%d], id[%d]", "ServerPacketEx.cpp", 0x1a2f,
            //     useCharacterWarehouse ? 1 : 0, characterId
            // )
            // L2_LOG_ERROR(Log, L"[%S][%d]Cannot find warehouse [%d], id[%d]", "ServerPacketEx.cpp", 0x1a2f,
            //     useCharacterWarehouse ? 1 : 0, characterId
            // )
            L2_LOG_ERROR(Log,
                "[items] Cannot remove expired item %d for character %d: %s unavailable. No reply will be sent.",
                itemId, characterId, useCharacterWarehouse ? "personal warehouse" : "inventory"
            );
            return;
        }

        DestoryItemParam parameters;
        parameters.itemId = itemId;
        parameters.itemServerId = 0;
        parameters.consumeType = 0;
        parameters.userServerId = userServerId;
        parameters.amount = 1;
        parameters.isPet = false;
        {
            L2_WRITE_GUARD(*warehouse.operator->());
            deleted = warehouse->DestroyItem(nullptr, parameters);
        }
    }

    if (deleted) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_RemoveExpiredPeriodicItem (?)}";
        connection.Prepare(statement);
        connection.BindParameter(&itemId);
        connection.ExecutePrepared(statement);
        connection.UnBind();
        LogPeriodicPremiumItemDeletion(
            static_cast<unsigned int>(user->GetAccountID()), characterId, itemId, static_cast<unsigned int>(itemType)
        );
    }

    socket->Send("chdddd", 0xc6, 0x5c, 2, userServerId, deleted ? itemId : 0, consumeType);
}

void OnSetPeriod(int itemId, int periodDelta) {
    // Current Cached.exe 0x0055c774.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_AddItemPeriod (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&itemId);
    connection.BindParameter(&periodDelta);
    connection.ExecutePrepared(statement);
}

} // namespace PeriodicItemJobHandler

namespace PeriodicShapeShiftedItemJobHandler {

void OnLoadInfo(CSocket* socket, int characterId, int userSocketId) {
    // Current Cached.exe 0x00581b64. The target counts every row in the
    // all-items carrier and expired rows a second time toward the 1000-entry
    // page boundary.
    L2_GUARD_FUNCTION();

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return;

    int itemId = 0;
    int startTime = 0;
    int period = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&itemId);
    connection.Bind(&startTime);
    connection.Bind(&period);
    constexpr const wchar_t* statement = L"{Call lin_LoadPeriodicShapeShiftedItems (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(statement))
        return;

    const int now = runtime::crt::Time32Now();
    bool pageFull = false;
    do {
        std::array<char, 0x2004> allRows{};
        std::array<char, 0x2004> expiredRows{};
        int allBytes = 0;
        int allCount = 0;
        int expiredBytes = 0;
        int expiredCount = 0;
        pageFull = false;

        for (;;) {
            if (!connection.Fetch())
                break;
            if (period != -1 && runtime::AddInt32Wrapping(startTime, period) <= now) {
                expiredBytes += ::Assemble(
                    expiredRows.data() + expiredBytes, static_cast<int>(expiredRows.size()) - expiredBytes, "d", itemId
                );
                ++expiredCount;
            }
            allBytes += ::Assemble(
                allRows.data() + allBytes, static_cast<int>(allRows.size()) - allBytes, "ddd", itemId, startTime, period
            );
            ++allCount;
            if (expiredCount + allCount == 1000) {
                pageFull = true;
                break;
            }
        }

        socket->Send(
            "chdddbdb", 0xc6, 0xdb, 0, userSocketId, allCount, allBytes, allRows.data(), expiredCount, expiredBytes,
            expiredRows.data()
        );
    } while (pageFull);
}

} // namespace PeriodicShapeShiftedItemJobHandler

bool RequestPeriodicItemJob(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0055c8a0.
    L2_GUARD_FUNCTION();

    int job = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &job);
    switch (job) {
    case 0: {
        int characterId = 0;
        int userSocketId = 0;
        ::Disassemble(cursor, "dd", &characterId, &userSocketId);
        PeriodicItemJobHandler::OnLoadInfo(socket, characterId, userSocketId);
        break;
    }
    case 1: {
        int itemId = 0;
        int startTime = 0;
        int period = 0;
        int itemType = 0;
        ::Disassemble(cursor, "dddd", &itemId, &startTime, &period, &itemType);
        PeriodicItemJobHandler::OnRegister(itemId, startTime, period, itemType);
        break;
    }
    case 2: {
        int itemId = 0;
        int consumeType = 0;
        int characterId = 0;
        int userServerId = 0;
        int itemType = 0;
        ::Disassemble(cursor, "ddddd", &itemId, &consumeType, &characterId, &userServerId, &itemType);
        PeriodicItemJobHandler::OnExpired(socket, itemId, consumeType, characterId, userServerId, itemType);
        break;
    }
    case 3: {
        int itemId = 0;
        int periodDelta = 0;
        ::Disassemble(cursor, "dd", &itemId, &periodDelta);
        PeriodicItemJobHandler::OnSetPeriod(itemId, periodDelta);
        break;
    }
    default:
        break;
    }
    return false;
}

bool RequestManipulatePremiumItems(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0053c0a0 / ServerPacketEx.cpp:6489-6507.
    L2_GUARD_FUNCTION();

    int requestId = 0;
    int characterId = 0;
    long long warehouseNo = 0;
    const unsigned char* cursor = ::Disassemble(packet, "ddQ", &requestId, &characterId, &warehouseNo);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.AddT(
        //     LOG_ERROR, L"[%s][%d][Premium] cannot find user. [user:%d item:%lld]", L"ServerPacketEx.cpp", 0x1959,
        //     characterId, warehouseNo
        // )
        // L2_LOG_ERROR(Log, L"[%s][%d][Premium] cannot find user. [user:%d item:%lld]", L"ServerPacketEx.cpp", 0x1959,
        //     characterId, warehouseNo
        // )
        L2_LOG_ERROR(Log,
            "[premium] Cannot process item changes for character %d: character unavailable. "
            "Warehouse record %lld, request %d; sending zero-result reply.", characterId, warehouseNo, requestId
        );
        socket->Send("chdQ", 0xc6, 0x5f, requestId, 0LL);
        return false;
    }

    CWareHouseSP warehouse = user->GetWareHouse(WareHouseType_Inventory, true);
    if (!warehouse) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[%s][%d][Premium] cannot find user warehouse. "
        //     L"[user:%d item:%lld]",
        //     L"ServerPacketEx.cpp", 0x1961, characterId, warehouseNo
        // )
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][Premium] cannot find user warehouse. "
        //     L"[user:%d item:%lld]",
        //     L"ServerPacketEx.cpp", 0x1961, characterId, warehouseNo
        // )
        L2_LOG_ERROR(Log,
            "[premium] Cannot process item changes for character %d: inventory unavailable. "
            "Warehouse record %lld, request %d; sending zero-result reply.", characterId, warehouseNo, requestId
        );
        socket->Send("chdQ", 0xc6, 0x5f, requestId, 0LL);
        return false;
    }

    {
        L2_WRITE_GUARD(*warehouse.operator->());
        warehouse->ManipulatePremiumItems(socket, cursor, requestId, warehouseNo);
    }
    return false;
}

bool RequestSetDeletedInIBServer(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0055b8f8.
    L2_GUARD_FUNCTION();

    int count = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &count);
    for (int index = 0; index < count; ++index) {
        long long warehouseNo = 0;
        cursor = ::Disassemble(cursor, "Q", &warehouseNo);
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_SetDeletedInIBServer (?)}";
        connection.Prepare(statement);
        connection.BindParameter(&warehouseNo);
        connection.ExecutePrepared(statement);
    }
    return false;
}

bool RequestDeletePremiumItem(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0055bbd4.
    L2_GUARD_FUNCTION();

    long long warehouseNo = 0;
    ::Disassemble(packet, "Q", &warehouseNo);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeletePremiumItem (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&warehouseNo);
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestSetRimPoint(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0055ba74.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int point = 0;
    int getTime = 0;
    int resetTime = 0;
    ::Disassemble(packet, "dddd", &characterId, &point, &getTime, &resetTime);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{call lin_SetRimPoint (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&point);
    connection.BindParameter(&getTime);
    connection.BindParameter(&resetTime);
    connection.ExecutePrepared(statement);
    return false;
}

bool GetRimPointRank(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0054f964.
    L2_GUARD_FUNCTION();

    int bindingTime = 0;
    ::Disassemble(packet, "d", &bindingTime);

    int characterId = 0;
    std::array<char16_t, 25> characterName{};
    int characterClass = 0;
    int level = 0;
    int maximumPoint = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&characterId);
    connection.Bind(characterName.data(), 0x32);
    connection.Bind(&characterClass);
    connection.Bind(&level);
    connection.Bind(&maximumPoint);
    constexpr const wchar_t* statement = L"{call lin_GetRimPointRank (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&bindingTime);
    if (!connection.ExecutePrepared(statement))
        return false;

    std::array<char, 0x2004> rows{};
    int rowBytes = 0;
    int rowCount = 0;
    while (connection.Fetch()) {
        rowBytes += ::Assemble(
            rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dSddd", characterId, characterName.data(),
            characterClass, level, maximumPoint
        );
        ++rowCount;
    }
    socket->Send("chdb", 0xc6, 0x61, rowCount, rowBytes, rows.data());
    return false;
}

bool RequestSetBotReport(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0053b6a4.
    L2_GUARD_FUNCTION();

    // This unused owner is present in the shipping handler and remains alive
    // across both CUser database calls.
    DBConn retainedConnection(runtime::process::CurrentDatabaseId());
    int characterId = 0;
    int reportPoint = 0;
    int confirmTimes = 0;
    int confirmSeconds = 0;
    int delaySeconds = 0;
    ::Disassemble(packet, "ddddd", &characterId, &reportPoint, &confirmTimes, &confirmSeconds, &delaySeconds);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        user->SetBotReport(reportPoint);
        user->SaveCaptchaConfirm(confirmTimes, confirmSeconds, delaySeconds);
    }
    return false;
}

bool RequestSaveBotReport(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0053b53c.
    L2_GUARD_FUNCTION();

    DBConn retainedConnection(runtime::process::CurrentDatabaseId());
    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user)
        user->SaveBotReport(socket);
    return false;
}

bool RequestLoadBotReport(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0053b8a4. The first ID is either -1 for an
    // ordinary self-load or the requesting GM character ID. The second is
    // always the target character ID in the paired current L2Server.
    L2_GUARD_FUNCTION();

    DBConn retainedConnection(runtime::process::CurrentDatabaseId());
    int requesterId = 0;
    int targetCharacterId = 0;
    ::Disassemble(packet, "dd", &requesterId, &targetCharacterId);

    int reportPoint = -1;
    int confirmTimes = 0;
    int confirmSeconds = 0;
    int delaySeconds = 0;
    CUserSP user = g_userDb.GetUser(targetCharacterId, true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        reportPoint = user->GetBotReport();
        confirmTimes = user->m_captchaConfirmTimes.load();
        confirmSeconds = user->m_captchaConfirmSec.load();
        delaySeconds = user->m_captchaDelaySec.load();
    }
    socket->Send(
        "chdddddd", 0xc6, 0x63, requesterId, targetCharacterId, reportPoint, confirmTimes, confirmSeconds, delaySeconds
    );
    return false;
}

bool RequestLoadBotReportTopTen(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0053bac0. Despite the historical name, the
    // current carrier always contains exactly 40 name/report rows.
    L2_GUARD_FUNCTION();

    int requesterId = 0;
    ::Disassemble(packet, "d", &requesterId);
    int databaseCharacterId = 0;
    int databaseReportPoint = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&databaseCharacterId);
    connection.Bind(&databaseReportPoint);
    constexpr const wchar_t* statement = L"{Call lin_LoadBotReportTopTen}";
    connection.Prepare(statement);

    std::array<std::array<char16_t, 25>, 40> names{};
    std::array<int, 40> reports{};
    if (connection.ExecutePrepared(statement)) {
        for (std::size_t index = 0; index < names.size(); ++index) {
            if (!connection.Fetch()) {
                // Current Cached.exe uses wsprintfW here. Copying the
                // literal directly keeps the result independent of the C locale.
                // Original: std::copy_n(u"순위없음", 4, names[index].data())
                constexpr std::u16string_view noRank = u"No rank";
                std::copy_n(noRank.data(), noRank.size(), names[index].data());
                reports[index] = 0;
                continue;
            }

            CUserSP user = g_userDb.GetUser(databaseCharacterId, true, false, runtime::process::CurrentDatabaseId());
            reports[index] = databaseReportPoint;
            if (!user) {
                const std::u16string missingName =
                    // Original: u"캐릭터못찾음(ID:"
                    std::u16string(u"Unknown(ID:") + Utf8ToUtf16(std::to_string(databaseCharacterId)) + u")";
                std::copy_n(missingName.data(), std::min(missingName.size(), names[index].size() - 1), names[index].data());
                // Log.AddT(LOG_ERROR, L"Cannot find bot rank 10 %d", databaseCharacterId)
                // L2_LOG_ERROR(Log, L"Cannot find bot rank 10 %d", databaseCharacterId)
                L2_LOG_ERROR(Log,
                    "[bot reports] Cannot resolve character %d at ranking position %u for requester %d. "
                    "Using a placeholder name and retaining report points %d.",
                    databaseCharacterId, static_cast<unsigned int>(index + 1), requesterId, databaseReportPoint
                );
                continue;
            }
            const std::u16string_view name(user->GetCharName());
            std::copy_n(name.data(), std::min(name.size(), names[index].size() - 1), names[index].data());
        }
    }

    std::array<char, 0x5000> rows{};
    int rowBytes = 0;
    for (std::size_t index = 0; index < names.size(); ++index) {
        const std::u16string packetName = std::u16string(names[index].data());
        rowBytes += ::Assemble(
            rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "Sd", packetName.c_str(), reports[index]
        );
    }
    socket->Send("chdb", 0xc6, 0x64, requesterId, rowBytes, rows.data());
    return false;
}

bool RequestResetBotReport(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0053be1c.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user)
        user->ResetBotReport();
    return false;
}

bool RequestSetBotAdmin(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x005591dc / ServerPacketEx.cpp.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetBotAdmin (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(LOG_ERROR, L"[%s][%d] Fail to set bot admin. char[%d]", L"ServerPacketEx.cpp", 0x1688, characterId)
        // L2_LOG_ERROR(Log, L"[%s][%d] Fail to set bot admin. char[%d]", L"ServerPacketEx.cpp", 0x1688, characterId)
        L2_LOG_ERROR(Log, "[bot reports] Database request to set bot admin for character %d failed.", characterId);
    }
    return false;
}

bool RequestDecayBotReport(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0053bf3c.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    DBConn retainedConnection(runtime::process::CurrentDatabaseId());
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user)
        user->DecayBotReport(socket);
    return false;
}

bool RequestChangeNicknameColor(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00559358. Paired current L2Server sender
    // 0x00656f98 proves that the decompiler-hidden third value is `S`.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int nicknameColor = 0;
    std::array<char16_t, 25> nickname{};
    ::Disassemble(packet, "ddS", &characterId, &nicknameColor, 0x32, nickname.data());
    std::wstring hostNickname = Utf16ToWide(nickname.data());

    DBConn connection(runtime::process::CurrentDatabaseId());
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.AddT(LOG_ERROR, L"[%s][%d] Cannot find user, char id[%d]", L"ServerPacketEx.cpp", 0x169f, characterId)
        // L2_LOG_ERROR(Log, L"[%s][%d] Cannot find user, char id[%d]", L"ServerPacketEx.cpp", 0x169f, characterId)
        L2_LOG_ERROR(Log,
            "[character] Cannot change nickname and color for character %d: character unavailable. "
            "Requested color 0x%08x; both changes skipped.",
            characterId, static_cast<unsigned int>(nicknameColor)
        );
        return false;
    }

    const int colorType = 1;
    constexpr const wchar_t* statement = L"{Call lin_SetColor (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&nicknameColor);
    connection.BindParameter(&colorType);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR, L"[%s][%d] Fail to set nickname color. char[%d]", L"ServerPacketEx.cpp", 0x16a5, characterId
        // )
        // L2_LOG_ERROR(Log, L"[%s][%d] Fail to set nickname color. char[%d]", L"ServerPacketEx.cpp", 0x16a5, characterId
        // )
        L2_LOG_ERROR(Log,
            "[character] Database request to save nickname color 0x%08x for character %d failed. "
            "The cached color will still be updated and the nickname change attempted.",
            static_cast<unsigned int>(nicknameColor), characterId
        );
    }
    if (!g_userDb.SetUserNickname(static_cast<unsigned int>(characterId), hostNickname.data())) {
        // Log.AddT(LOG_ERROR, L"[%s][%d] Fail to set nickname. char[%d]", L"ServerPacketEx.cpp", 0x16aa, characterId)
        // L2_LOG_ERROR(Log, L"[%s][%d] Fail to set nickname. char[%d]", L"ServerPacketEx.cpp", 0x16aa, characterId)
        L2_LOG_ERROR(Log,
            "[character] Nickname change failed for character %d. Cached nickname color will still be set to 0x%08x.",
            characterId, static_cast<unsigned int>(nicknameColor)
        );
    }
    user->m_nicknameColor = nicknameColor;
    return false;
}

bool RequestLoadPremiumItemForBatch(CSocket* socket, const unsigned char* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0052b4c8.
    L2_GUARD_FUNCTION();

    long long warehouseNo = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&warehouseNo);
    constexpr const wchar_t* statement = L"{Call lin_LoadPremiumItemsForBatch}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement))
        return false;

    std::array<char, 0x5000> rows{};
    int rowBytes = 0;
    int rowCount = 0;
    while (connection.Fetch()) {
        rowBytes += ::Assemble(rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "Q", warehouseNo);
        ++rowCount;
        if (rowCount == 100) {
            socket->Send("chdb", 0xc6, 0x60, rowCount, rowBytes, rows.data());
            rowBytes = 0;
            rowCount = 0;
        }
    }
    if (rowCount > 0) {
        socket->Send("chdb", 0xc6, 0x60, rowCount, rowBytes, rows.data());
    }
    return false;
}

bool RequestLoadAllDominion(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00559edc.
    L2_GUARD_FUNCTION();

    int dominionCount = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &dominionCount);
    for (int index = 0; index < dominionCount; ++index) {
        int dominionId = 0;
        cursor = ::Disassemble(cursor, "d", &dominionId);

        int dominionStatus = 0;
        int residenceStatus = 0;
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Bind(&dominionStatus);
        connection.Bind(&residenceStatus);
        constexpr const wchar_t* loadStatement = L"{Call lin_LoadDominion (?)}";
        connection.Prepare(loadStatement);
        connection.BindParameter(&dominionId);

        if (!connection.ExecutePrepared(loadStatement) || !connection.Fetch()) {
            connection.UnBind();
            const int defaultStatus = 0;
            const int defaultResidenceStatus = 0;
            constexpr const wchar_t* saveStatement = L"{Call lin_SaveDominion (?, ?, ?)}";
            connection.Prepare(saveStatement);
            connection.BindParameter(&dominionId);
            connection.BindParameter(&defaultStatus);
            connection.BindParameter(&defaultResidenceStatus);
            connection.ExecutePrepared(saveStatement);

            connection.UnBind();
            connection.Bind(&dominionStatus);
            connection.Bind(&residenceStatus);
            connection.Prepare(loadStatement);
            connection.BindParameter(&dominionId);
            if (!connection.ExecutePrepared(loadStatement) || !connection.Fetch()) {
                connection.UnBind();
                // Log.AddT(
                //     LOG_ERROR, L"[%s][%d] dominion data not created, dominion [%d]", L"ServerPacketEx.cpp", 0x17ab,
                //     dominionId
                // )
                // L2_LOG_ERROR(Log, L"[%s][%d] dominion data not created, dominion [%d]", L"ServerPacketEx.cpp", 0x17ab,
                //     dominionId
                // )
                L2_LOG_ERROR(Log,
                    "[dominion] Reload for dominion %d failed or returned no row after attempting to save defaults. "
                    "Stopping at entry %d of %d; this and subsequent entries will not be sent.", dominionId, index + 1, dominionCount
                );
                break;
            }
        }

        socket->Send("chddd", 0xc6, 0x65, dominionId, dominionStatus, residenceStatus);
    }
    return false;
}

bool RequestSaveDominion(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00559d1c.
    L2_GUARD_FUNCTION();

    int dominionId = 0;
    int dominionStatus = 0;
    int residenceStatus = 0;
    ::Disassemble(packet, "ddd", &dominionId, &dominionStatus, &residenceStatus);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveDominion (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&dominionId);
    connection.BindParameter(&dominionStatus);
    connection.BindParameter(&residenceStatus);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR, L"[%s][%d] Fail to save dominion. dominion[%d]", L"ServerPacketEx.cpp", 0x176e, dominionId
        // )
        // L2_LOG_ERROR(Log, L"[%s][%d] Fail to save dominion. dominion[%d]", L"ServerPacketEx.cpp", 0x176e, dominionId
        // )
        L2_LOG_ERROR(Log,
            "[dominion] Database save failed for dominion %d: status %d, residence status %d.",
            dominionId, dominionStatus, residenceStatus
        );
    }
    return false;
}

bool RequestSaveDominionRegistry(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00559908.
    L2_GUARD_FUNCTION();

    int dominionId = 0;
    int operation = 0;
    int registryType = 0;
    int registryId = 0;
    ::Disassemble(packet, "dddd", &dominionId, &operation, &registryType, &registryId);

    if (operation == 1) {
        PersistDominionRegistryChange(operation, dominionId, registryType, registryId);
        return false;
    }

    if (registryType == 1) {
        CPledgeSP pledge = g_pledgeDb.GetPledge(registryId, false, runtime::process::CurrentDatabaseId());
        if (pledge) {
            L2_WRITE_GUARD(pledge->m_pledgeLock);
            PersistDominionRegistryChange(operation, dominionId, registryType, registryId);
        }
        return false;
    }

    CUserSP user = g_userDb.GetUser(registryId, true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        L2_WRITE_GUARD(*user.operator->());
        PersistDominionRegistryChange(operation, dominionId, registryType, registryId);
    }
    return false;
}

bool RequestLoadDominionRegistry(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00559b00.
    L2_GUARD_FUNCTION();

    int dominionId = 0;
    ::Disassemble(packet, "d", &dominionId);

    int registryType = 0;
    int registryId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&registryType);
    connection.Bind(&registryId);
    constexpr const wchar_t* statement = L"{Call lin_LoadDominionRegistry (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&dominionId);

    std::array<char, 0x5000> rows{};
    int rowBytes = 0;
    int rowCount = 0;
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            rowBytes +=
                ::Assemble(rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dd", registryType, registryId);
            // Shipping increments the row count even if Assemble reports a
            // full 0x5000-byte carrier.
            ++rowCount;
        }
    }
    socket->Send("chddb", 0xc6, 0x67, dominionId, rowCount, rowBytes, rows.data());
    return false;
}

bool RequestSaveOwnthing(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0055a1e8.
    L2_GUARD_FUNCTION();

    int originalDominionId = 0;
    int currentDominionId = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    ::Disassemble(packet, "ddddd", &originalDominionId, &currentDominionId, &x, &y, &z);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveOwnthing (?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&originalDominionId);
    connection.BindParameter(&currentDominionId);
    connection.BindParameter(&x);
    connection.BindParameter(&y);
    connection.BindParameter(&z);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR, L"[%s][%d] Fail to save ownthing. dominion[%d/%d]", L"ServerPacketEx.cpp", 0x17cf, originalDominionId,
        //     currentDominionId
        // )
        // L2_LOG_ERROR(Log, L"[%s][%d] Fail to save ownthing. dominion[%d/%d]", L"ServerPacketEx.cpp", 0x17cf, originalDominionId,
        //     currentDominionId
        // )
        L2_LOG_ERROR(Log,
            "[dominion] Ownthing database save failed: original dominion %d, current dominion %d, position (%d, %d, %d).",
            originalDominionId, currentDominionId, x, y, z
        );
    }
    return false;
}

bool RequestLoadOwnthing(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0055a398.
    L2_GUARD_FUNCTION();

    int originalDominionId = 0;
    ::Disassemble(packet, "d", &originalDominionId);

    int currentDominionId = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&currentDominionId);
    connection.Bind(&x);
    connection.Bind(&y);
    connection.Bind(&z);
    constexpr const wchar_t* loadStatement = L"{Call lin_LoadOwnthing (?)}";
    connection.Prepare(loadStatement);
    connection.BindParameter(&originalDominionId);

    if (!connection.ExecutePrepared(loadStatement) || !connection.Fetch()) {
        connection.UnBind();
        const int defaultCurrentDominionId = originalDominionId;
        const int defaultCoordinate = 0;
        constexpr const wchar_t* saveStatement = L"{Call lin_SaveOwnthing (?, ?, ?, ?, ?)}";
        connection.Prepare(saveStatement);
        connection.BindParameter(&originalDominionId);
        connection.BindParameter(&defaultCurrentDominionId);
        connection.BindParameter(&defaultCoordinate);
        connection.BindParameter(&defaultCoordinate);
        connection.BindParameter(&defaultCoordinate);
        connection.ExecutePrepared(saveStatement);

        connection.UnBind();
        connection.Bind(&currentDominionId);
        connection.Bind(&x);
        connection.Bind(&y);
        connection.Bind(&z);
        connection.Prepare(loadStatement);
        connection.BindParameter(&originalDominionId);
        if (!connection.ExecutePrepared(loadStatement) || !connection.Fetch()) {
            connection.UnBind();
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[%s][%d] ownthing data not created, "
            //     L"ownthing of dominion [%d]",
            //     L"ServerPacketEx.cpp", 0x1804, originalDominionId
            // )
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] ownthing data not created, "
            //     L"ownthing of dominion [%d]",
            //     L"ServerPacketEx.cpp", 0x1804, originalDominionId
            // )
            L2_LOG_ERROR(Log,
                "[dominion] Ownthing reload for original dominion %d failed or returned no row after attempting to save defaults. "
                "No reply will be sent.", originalDominionId
            );
            return false;
        }
    }

    socket->Send("chddddd", 0xc6, 0x66, originalDominionId, currentDominionId, x, y, z);
    return false;
}

bool RequestSaveDominionSiege(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0055a678.
    L2_GUARD_FUNCTION();

    int siegeState = 0;
    int previousSiegeEndTime = 0;
    int siegeElapsedTime = 0;
    int nextSiegeStartTime = 0;
    ::Disassemble(packet, "dddd", &siegeState, &previousSiegeEndTime, &siegeElapsedTime, &nextSiegeStartTime);
    // Log.AddT(
    //     LOG_ERROR,
    //     L"[NO_ERROR] dominion state saved. state [%d] end time [%d] "
    //     L"elapsed time[%d] next siege start time[%d]",
    //     siegeState, previousSiegeEndTime, siegeElapsedTime, nextSiegeStartTime
    // )
    // Log.Add(LOG_NORMAL, "component=dominion event=save_requested state=%d previous_end=%d elapsed=%d next_start=%d", siegeState, previousSiegeEndTime, siegeElapsedTime, nextSiegeStartTime)
    // Log.Add(LOG_NORMAL, "[dominion] Saving siege state %d: previous end time %d, elapsed %d, next start time %d.", siegeState, previousSiegeEndTime, siegeElapsedTime, nextSiegeStartTime)
    L2_LOG_INFO(Log, "[dominion] Saving siege state %d: previous end time %d, elapsed %d, next start time %d.", siegeState, previousSiegeEndTime, siegeElapsedTime, nextSiegeStartTime);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveDominionSiege (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&siegeState);
    connection.BindParameter(&previousSiegeEndTime);
    connection.BindParameter(&siegeElapsedTime);
    connection.BindParameter(&nextSiegeStartTime);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(LOG_ERROR, L"[%s][%d] Fail to save dominion siege![%d]", L"ServerPacketEx.cpp", 0x1828, siegeState)
        // L2_LOG_ERROR(Log, L"[%s][%d] Fail to save dominion siege![%d]", L"ServerPacketEx.cpp", 0x1828, siegeState)
        L2_LOG_ERROR(Log,
            "[dominion] Siege database save failed: state %d, previous end %d, elapsed %d, next start %d.",
            siegeState, previousSiegeEndTime, siegeElapsedTime, nextSiegeStartTime
        );
    }
    return false;
}

bool RequestLoadDominionSiege(CSocket* socket, const unsigned char* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0055a840.
    L2_GUARD_FUNCTION();

    int siegeState = 0;
    int previousSiegeEndTime = 0;
    int siegeElapsedTime = 0;
    int nextSiegeStartTime = 0;
    std::int64_t nextSiegeStartIndicator = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&siegeState);
    connection.Bind(&previousSiegeEndTime);
    connection.Bind(&siegeElapsedTime);
    connection.BindNullable(&nextSiegeStartTime, &nextSiegeStartIndicator);
    constexpr const wchar_t* loadStatement = L"{Call lin_LoadDominionSiege}";
    connection.Prepare(loadStatement);

    if (!connection.ExecutePrepared(loadStatement) || !connection.Fetch()) {
        connection.UnBind();
        const int defaultSiegeState = 0;
        const int defaultPreviousSiegeEndTime = runtime::crt::Time32Now();
        const int defaultSiegeElapsedTime = 0;
        const int defaultNextSiegeStartTime = 0;
        constexpr const wchar_t* saveStatement = L"{Call lin_SaveDominionSiege (?, ?, ?, ?)}";
        connection.Prepare(saveStatement);
        connection.BindParameter(&defaultSiegeState);
        connection.BindParameter(&defaultPreviousSiegeEndTime);
        connection.BindParameter(&defaultSiegeElapsedTime);
        connection.BindParameter(&defaultNextSiegeStartTime);
        connection.ExecutePrepared(saveStatement);

        connection.UnBind();
        connection.Bind(&siegeState);
        connection.Bind(&previousSiegeEndTime);
        connection.Bind(&siegeElapsedTime);
        connection.BindNullable(&nextSiegeStartTime, &nextSiegeStartIndicator);
        connection.Prepare(loadStatement);
        if (!connection.ExecutePrepared(loadStatement) || !connection.Fetch()) {
            connection.UnBind();
            // Log.AddT(LOG_ERROR, L"[%s][%d] dominion siege not created", L"ServerPacketEx.cpp", 0x185d)
            // L2_LOG_ERROR(Log, L"[%s][%d] dominion siege not created", L"ServerPacketEx.cpp", 0x185d)
            L2_LOG_ERROR(Log,
                "[dominion] Siege reload failed or returned no row after attempting to save defaults. No reply will be sent."
            );
            return false;
        }
    }

    socket->Send("chdddd", 0xc6, 0x68, siegeState, previousSiegeEndTime, siegeElapsedTime, nextSiegeStartTime);
    return false;
}

bool RequestLoadBirthInfo(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0053ac5c.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user)
        user->LoadBirthInfo(socket);
    return false;
}

bool RequestSaveBirthdayGift(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0053b034.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int year = 0;
    int month = 0;
    int day = 0;
    ::Disassemble(packet, "dddd", &characterId, &year, &month, &day);
    SYSTEMTIME birthday{};
    birthday.wYear = static_cast<std::uint16_t>(year);
    birthday.wMonth = static_cast<std::uint16_t>(month);
    birthday.wDay = static_cast<std::uint16_t>(day);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user)
        user->SaveBirthdayGift(&birthday);
    return false;
}

bool RequestGetLastBirthdayGiftDate(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0053ad84.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user)
        user->LoadLastBirthdayGiftDate(socket);
    return false;
}

bool RequestLoad5YearInfo(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0053a9d4.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user)
        user->Load5YearInfo(socket);
    return false;
}

bool RequestSave5YearGift(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0053aafc.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    SYSTEMTIME giftDate{};
    ::Disassemble(packet, "db", &characterId, 0x10, &giftDate);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user)
        user->Save5YearGift(&giftDate);
    return false;
}

bool RequestSaveDominionRenamed(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0055aae8.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int renamed = 0;
    ::Disassemble(packet, "dd", &characterId, &renamed);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveDominionRenamed (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&renamed);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR, L"[%s][%d] Fail to save dominion renamed. user[%d]", L"ServerPacketEx.cpp", 0x1876, characterId
        // )
        // L2_LOG_ERROR(Log, L"[%s][%d] Fail to save dominion renamed. user[%d]", L"ServerPacketEx.cpp", 0x1876, characterId
        // )
        L2_LOG_ERROR(Log,
            "[dominion] Database request to save rename state %d for character %d failed.", renamed, characterId
        );
    }
    return false;
}

bool RequestLoadDominionRenamed(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0055ac84.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);

    int renamed = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&renamed);
    constexpr const wchar_t* statement = L"{Call lin_LoadDominionRenamed (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }
    socket->Send("chdd", 0xc6, 0x6c, characterId, renamed);
    return false;
}

bool RequestDeleteAllDominionRenamed(CSocket* socket [[maybe_unused]], const unsigned char* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0052b6c8.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeleteAllDominionRenamed}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(LOG_ERROR, L"Delete All Dominion Renamed Failed!")
        // L2_LOG_ERROR(Log, L"Delete All Dominion Renamed Failed!")
        L2_LOG_ERROR(Log, "[dominion] Database request to clear all character rename records failed.");
    }
    return false;
}

bool RequestSetDailyQuest(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0053b1bc.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int questId = 0;
    int completeDate = 0;
    ::Disassemble(packet, "ddd", &characterId, &questId, &completeDate);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        user->SetDailyQuest(questId, static_cast<long>(completeDate));
    }
    return false;
}

bool RequestLoadDailyQuest(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005579c4.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.AddT(LOG_ERROR, L"[%s][%d] Cannot find user, char id[%d]", L"ServerPacketEx.cpp", 0x1432, characterId)
        // L2_LOG_ERROR(Log, L"[%s][%d] Cannot find user, char id[%d]", L"ServerPacketEx.cpp", 0x1432, characterId)
        L2_LOG_ERROR(Log,
            "[quests] Cannot load daily quests for character %d: character unavailable. No reply will be sent.",
            characterId
        );
        return false;
    }

    int questId = 0;
    int completeDate = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&questId);
    connection.Bind(&completeDate);
    constexpr const wchar_t* statement = L"{Call lin_LoadDailyQuest (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);

    std::array<char, 0x5000> rows{};
    int rowBytes = 0;
    int rowCount = 0;
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            rowBytes +=
                ::Assemble(rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dd", questId, completeDate);
            ++rowCount;
            if (rowCount >= 0x12d) {
                // Log.AddT(
                //     LOG_ERROR,
                //     L"[%s][%d] [%d] character has too many "
                //     L"unregistered quest.",
                //     L"ServerPacketEx.cpp", 0x1447, characterId
                // )
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] [%d] character has too many "
                //     L"unregistered quest.",
                //     L"ServerPacketEx.cpp", 0x1447, characterId
                // )
                L2_LOG_WARN(Log,
                    "[quests] Daily-quest load for character %d reached the 301-row limit. "
                    "Sending %d rows; additional rows were not checked.", characterId, rowCount
                );
                break;
            }
        }
        socket->Send("chddb", 0xc6, 0x6d, characterId, rowCount, rowBytes, rows.data());
    }
    return false;
}

bool RequestSetBirthday(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0053aeac.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int year = 0;
    int month = 0;
    int day = 0;
    ::Disassemble(packet, "dddd", &characterId, &year, &month, &day);
    SYSTEMTIME birthday{};
    birthday.wYear = static_cast<std::uint16_t>(year);
    birthday.wMonth = static_cast<std::uint16_t>(month);
    birthday.wDay = static_cast<std::uint16_t>(day);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user)
        user->SetBirthday(&birthday);
    return false;
}

namespace AirShipJobHelper {

void OnLoad(CSocket* socket) {
    // Current Cached.exe 0x0052b7e4.
    L2_GUARD_FUNCTION();

    unsigned int airShipId = 0;
    int airShipType = 0;
    unsigned int ownerId = 0;
    int fuel = 0;
    int airShipTypeId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&airShipId);
    connection.Bind(&airShipType);
    connection.Bind(&ownerId);
    connection.Bind(&fuel);
    connection.Bind(&airShipTypeId);
    constexpr const wchar_t* statement = L"{Call lin_LoadAirShip}";
    connection.Prepare(statement);

    std::array<char, 0x5000> rows{};
    int rowBytes = 0;
    int rowCount = 0;
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        do {
            if (rowCount < 200) {
                rowBytes += ::Assemble(
                    rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "ddddd", airShipId, airShipType,
                    ownerId, fuel, airShipTypeId
                );
                ++rowCount;
            } else {
                // The shipping loop sends the first 200 rows and then
                // advances Fetch without assembling the already-fetched
                // boundary row.
                socket->Send("chddb", 0xc6, 0x6e, 2, rowCount, rowBytes, rows.data());
                rows.fill(0);
                rowCount = 0;
                rowBytes = 0;
            }
        } while (connection.Fetch());

        if (rowCount > 0) {
            socket->Send("chddb", 0xc6, 0x6e, 2, rowCount, rowBytes, rows.data());
        }
    }
}

void OnBuy(
    CSocket* socket,
    unsigned int userId,
    int airShipType,
    int airShipTypeId,
    unsigned int ownerId,
    int fuel,
    unsigned int requestId
) {
    // Current Cached.exe 0x0055ca54. The SQL parameter order and reply order
    // are fixed by the current machine call ABI, not donor source guesses.
    L2_GUARD_FUNCTION();

    unsigned int airShipId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&airShipId);
    constexpr const wchar_t* statement = L"{Call lin_CreateAirShip (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&airShipType);
    connection.BindParameter(&ownerId);
    connection.BindParameter(&fuel);
    connection.BindParameter(&airShipTypeId);
    if (connection.ExecutePrepared(statement) && connection.Fetch() && airShipId != 0) {
        socket->Send("chdddddddd", 0xc6, 0x6e, 0, userId, requestId, airShipId, airShipType, airShipTypeId, ownerId, fuel);
        return;
    }

    socket->Send("chdddd", 0xc6, 0x6e, 0, userId, requestId, 0);
}

void OnSave(unsigned int airShipId, int fuel) {
    // Current Cached.exe 0x0055cc14.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveAirShip (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&airShipId);
    connection.BindParameter(&fuel);
    connection.ExecutePrepared(statement);
}

void OnRemove(unsigned int airShipId, int reason) {
    // Current Cached.exe 0x0055cd40.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_RemoveAirShip (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&airShipId);
    connection.BindParameter(&reason);
    connection.ExecutePrepared(statement);
}

} // namespace AirShipJobHelper

bool RequestAirShipJob(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0055ce6c.
    L2_GUARD_FUNCTION();

    int jobType = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &jobType);
    switch (jobType) {
    case 0: {
        unsigned int userId = 0;
        int airShipType = 0;
        int airShipTypeId = 0;
        unsigned int ownerId = 0;
        int fuel = 0;
        unsigned int requestId = 0;
        ::Disassemble(cursor, "dddddd", &userId, &airShipType, &airShipTypeId, &ownerId, &fuel, &requestId);
        AirShipJobHelper::OnBuy(socket, userId, airShipType, airShipTypeId, ownerId, fuel, requestId);
        break;
    }
    case 1: {
        unsigned int airShipId = 0;
        int fuel = 0;
        ::Disassemble(cursor, "dd", &airShipId, &fuel);
        AirShipJobHelper::OnSave(airShipId, fuel);
        break;
    }
    case 2:
        AirShipJobHelper::OnLoad(socket);
        break;
    case 3: {
        unsigned int airShipId = 0;
        int reason = 0;
        ::Disassemble(cursor, "dd", &airShipId, &reason);
        AirShipJobHelper::OnRemove(airShipId, reason);
        break;
    }
    default:
        break;
    }
    return false;
}

bool RequestCachePing(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052bb24.
    L2_GUARD_FUNCTION();

    int ping = 0;
    ::Disassemble(packet, "d", &ping);
    socket->Send("chd", 0xc6, 0x6f, ping);
    return false;
}

bool RequestWritePost(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00560884. Current L2Server::CDB::RequestWritePost
    // fixes the dSdSSd + dQd[] + QdQd wire layout.
    L2_GUARD_FUNCTION();

    int senderId = 0;
    std::array<char16_t, 25> receiverNamePacket{};
    int trade = 0;
    std::array<char16_t, 61> titlePacket{};
    std::array<char16_t, 1001> contentsPacket{};
    int itemCount = 0;
    const unsigned char* cursor = ::Disassemble(
        packet, "dSdSSd", &senderId, 0x32, receiverNamePacket.data(), &trade, 0x7a, titlePacket.data(), 0x7d2,
        contentsPacket.data(), &itemCount
    );

    std::array<int, 8> itemIds{};
    std::array<std::int64_t, 8> amounts{};
    std::array<int, 8> consumeTypes{};
    if (itemCount < 9) {
        for (int index = 0; index < itemCount; ++index) {
            cursor = ::Disassemble(
                cursor, "dQd", &itemIds[static_cast<std::size_t>(index)], &amounts[static_cast<std::size_t>(index)],
                &consumeTypes[static_cast<std::size_t>(index)]
            );
        }
    }

    std::int64_t tradeMoney = 0;
    int returnable = 0;
    std::int64_t postFee = 0;
    int senderType = 0;
    if (itemCount < 9) {
        ::Disassemble(cursor, "QdQd", &tradeMoney, &returnable, &postFee, &senderType);
    }

    const std::u16string title(titlePacket.data());
    const std::u16string contents(contentsPacket.data());
    CUserSP receiver;
    int receiverId = 0;

    std::array<int, 8> resultingItemIds{};
    std::array<std::int64_t, 8> remainingAmounts{};
    int feeItemId = 0;
    std::int64_t remainingAdena = 0;
    int dailyRemaining = -1;
    std::array<char, 0x5000> responseItems{};
    int responseBytes = 0;

    auto sendReply = [&](int result, int postId, int replyItemCount) {
        const bool succeeded = result == 1;
        socket->Send(
            "chdddddddddddb", 0xc6, 0x71, result, postId, senderId, receiverId, trade, returnable, 0,
            succeeded ? feeItemId : 0, succeeded ? static_cast<int>(postFee) : 0, succeeded ? replyItemCount : 0,
            succeeded ? dailyRemaining : 0, succeeded ? responseBytes : 0, responseItems.data()
        );
    };

    // The current function takes the out-of-range branch before looking up
    // the receiver or decoding the QdQd trailer. Signed negative counts take
    // the ordinary path, exactly as the target's `count < 9` branch does.
    if (itemCount >= 9) {
        // Log.Add(LOG_WARN,
        //     "[mail] Write request rejected for sender %d: attachment count %d exceeds the limit of 8. Failure reply follows.",
        //     senderId, itemCount)
        L2_LOG_WARN(Log,
            "[mail] Write request rejected for sender %d: attachment count %d exceeds the limit of 8; failure reply follows.",
            senderId, itemCount);
        // sendReply(-3, 0, 0)
        sendReply(Post::FailedByInventoryError, 0, 0);
        return false;
    }

    receiver = g_userDb.GetUser(receiverNamePacket.data(), true);
    receiverId = receiver ? static_cast<int>(receiver->GetId()) : 0;
    if (!receiver) {
        // sendReply(-2, 0, 0)
        sendReply(Post::FailedByUserInvalid, 0, 0);
        return false;
    }

    auto receiverMailboxFull = [&]() {
        int receivedPostCount = 0;
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Bind(&receivedPostCount);
        constexpr const wchar_t* statement = L"{Call lin_GetReceivedPostCount (?)}";
        connection.Prepare(statement);
        connection.BindParameter(&receiverId);
        // return connection.ExecutePrepared(statement) && connection.Fetch() && receivedPostCount > 0xef
        const bool mailboxCountLoaded = connection.ExecutePrepared(statement) && connection.Fetch();
        if (!mailboxCountLoaded) {
            // Log.Add(LOG_ERROR,
            //     "[mail] No mailbox count returned for recipient %d (sender %d). Capacity check treats the mailbox as not full. Request processing continues.",
            //     receiverId, senderId)
            L2_LOG_ERROR(Log,
                "[mail] No mailbox count returned for recipient %d (sender %d); capacity check treats the mailbox as not full; request processing continues.",
                receiverId, senderId);
        }
        return mailboxCountLoaded && receivedPostCount > 0xef;
    };

    CUserSP sender;
    if (senderId != -1) {
        sender = g_userDb.GetUser(senderId, true, false, runtime::process::CurrentDatabaseId());
    }

    const bool systemSender = !sender && senderId < 1;
    if (!sender && !systemSender) {
        // sendReply(-2, 0, 0)
        sendReply(Post::FailedByUserInvalid, 0, 0);
        return false;
    }

    if (systemSender) {
        if (receiverMailboxFull()) {
            // sendReply(-1, 0, 0)
            sendReply(Post::FailedByReceivedCount, 0, 0);
            return false;
        }
    } else {
        if (sender->GetId() == receiver->GetId()) {
            // sendReply(-7, 0, 0)
            sendReply(Post::FailedByMeToMe, 0, 0);
            return false;
        }

        const CacheDConfiguration& configuration = GetCacheDConfiguration();
        if (sender->GetLevel() <= configuration.postLimitLevel && sender->GetAccountID() != receiver->GetAccountID()) {
            FriendIdSet friendIds;
            g_friendsDb.GetFriendIdList(sender->GetId(), friendIds);
            if (friendIds.find(receiver->GetId()) == friendIds.end()) {
                // sendReply(-10, 0, 0)
                sendReply(Post::FailedByNotFriend, 0, 0);
                return false;
            }
        }

        if (receiver->GetBuilder() > 0 && sender->GetBuilder() <= 0) {
            // sendReply(-2, 0, 0)
            sendReply(Post::FailedByUserInvalid, 0, 0);
            return false;
        }

        // if (configuration.dailyPostCountForFreeUser != -1 && senderType == 2) {
        if (configuration.dailyPostCountForFreeUser != -1 && senderType == DailyPostSystem::DPS_FOR_FREE_USER) {
            int dailyCount = 0;
            DBConn connection(runtime::process::CurrentDatabaseId());
            connection.Bind(&dailyCount);
            constexpr const wchar_t* statement = L"{Call lin_GetDailyCount (?, ?)}";
            connection.Prepare(statement);
            connection.BindParameter(&senderId);
            connection.BindParameter(&senderType);
            if (connection.ExecutePrepared(statement) && connection.Fetch()) {
                if (dailyCount >= configuration.dailyPostCountForFreeUser) {
                    // sendReply(-8, 0, 0)
                    sendReply(Post::FailedBySentCount, 0, 0);
                    return false;
                }
                dailyRemaining = configuration.dailyPostCountForFreeUser - dailyCount - 1;
            } else {
                // Log.Add(LOG_ERROR,
                //     "[mail] No daily mail count returned for sender %d, sender type %d. Daily limit is not enforced by this check and request processing continues for recipient %d.",
                //     senderId, senderType, receiverId)
                L2_LOG_ERROR(Log,
                    "[mail] No daily mail count returned for sender %d, sender type %d; daily limit is not enforced by this check and request processing continues for recipient %d.",
                    senderId, senderType, receiverId);
            }
        }

        if (receiverMailboxFull()) {
            // sendReply(-1, 0, 0)
            sendReply(Post::FailedByReceivedCount, 0, 0);
            return false;
        }

        int blocked = 0;
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Bind(&blocked);
        constexpr const wchar_t* statement = L"{Call lin_LoadBlocked (?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&receiverId);
        connection.BindParameter(&senderId);
        // if (connection.ExecutePrepared(statement) && connection.Fetch()) {
            // sendReply(-4, 0, 0)
            // return false
        // }
        const bool blockQueryExecuted = connection.ExecutePrepared(statement);
        if (blockQueryExecuted && connection.Fetch()) {
            // sendReply(-4, 0, 0)
            sendReply(Post::FailedByBlocked, 0, 0);
            return false;
        } else if (!blockQueryExecuted) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Block-list query failed for sender %d, recipient %d. This check does not block mail creation.",
            //     senderId, receiverId)
            L2_LOG_ERROR(Log,
                "[mail] Block-list query failed for sender %d, recipient %d; this check does not block mail creation.",
                senderId, receiverId);
        }

        CWareHouseSP inventory = sender->GetWareHouse(0, true);
        CWareHouseSP postWarehouse = sender->GetWareHouse(4, true);
        if (!inventory || !postWarehouse) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Cannot prepare mail from sender %d to recipient %d: %s is unavailable. Failure reply follows.",
            //     senderId, receiverId, !inventory ? "sender inventory" : "sender mail warehouse")
            L2_LOG_ERROR(Log,
                "[mail] Cannot prepare mail from sender %d to recipient %d: %s is unavailable; failure reply follows.",
                senderId, receiverId, !inventory ? "sender inventory" : "sender mail warehouse");
            // sendReply(-3, 0, 0)
            sendReply(Post::FailedByInventoryError, 0, 0);
            return false;
        }

        bool written = false;
        if (inventory->GetLockAddr() == postWarehouse->GetLockAddr()) {
            {
                L2_WRITE_GUARD(*inventory.operator->());
                written = inventory->WritePost(
                    socket, postWarehouse.get(), itemCount, itemIds.data(), amounts.data(), resultingItemIds.data(),
                    remainingAmounts.data(), consumeTypes.data(), postFee, feeItemId, remainingAdena, receiverId
                );
            }
        } else if (inventory->GetLockAddr() < postWarehouse->GetLockAddr()) {
            L2_WRITE_GUARD(*inventory.operator->());
            {
                L2_WRITE_GUARD(*postWarehouse.operator->());
                written = inventory->WritePost(
                    socket, postWarehouse.get(), itemCount, itemIds.data(), amounts.data(), resultingItemIds.data(),
                    remainingAmounts.data(), consumeTypes.data(), postFee, feeItemId, remainingAdena, receiverId
                );
            }
        } else {
            L2_WRITE_GUARD(*postWarehouse.operator->());
            {
                L2_WRITE_GUARD(*inventory.operator->());
                written = inventory->WritePost(
                    socket, postWarehouse.get(), itemCount, itemIds.data(), amounts.data(), resultingItemIds.data(),
                    remainingAmounts.data(), consumeTypes.data(), postFee, feeItemId, remainingAdena, receiverId
                );
            }
        }
        if (!written) {
            // sendReply(-3, 0, 0)
            sendReply(Post::FailedByInventoryError, 0, 0);
            return false;
        }
    }

    int postId = 0;
    {
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Bind(&postId);
        constexpr const wchar_t* statement = L"{Call lin_CreatePost (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)}";
        int process = 0;
        int senderDeleted = 0;
        int receiverDeleted = 0;
        const int sentTime = runtime::crt::Time32Now();
        // int storedSenderType = 0.
        int storedSenderType = Post::UserToUser;
        const int recordedId = senderId;
        connection.Prepare(statement);
        connection.BindParameter(&senderId);
        connection.BindParameter(&receiverId);
        connection.BindParameter(&trade);
        connection.BindParameter(title.c_str());
        connection.BindParameter(contents.c_str());
        connection.BindParameter(&process);
        connection.BindParameter(&returnable);
        connection.BindParameter(&senderDeleted);
        connection.BindParameter(&receiverDeleted);
        connection.BindParameter(&sentTime);
        connection.BindParameter(&itemCount);
        connection.BindParameter(&tradeMoney);
        connection.BindParameter(&postFee);
        connection.BindParameter(&storedSenderType);
        connection.BindParameter(&recordedId);
        if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
            // Log.AddT(LOG_ERROR, L"WritePost Failed. %d to %d.", senderId, receiverId)
            // Log.Add(LOG_ERROR,
            //     "[mail] Mail creation returned no database result for sender %d, recipient %d, attachment count %d (fee %lld, requested payment %lld, returned mail ID %d). %s Failure reply follows.",
            //     senderId, receiverId, itemCount, static_cast<long long>(postFee), static_cast<long long>(tradeMoney), postId, systemSender ? "No sender warehouse preparation was performed." : "Attachment and fee preparation already ran and is not reversed here.")
            L2_LOG_ERROR(Log,
                "[mail] Mail creation returned no database result for sender %d, recipient %d, attachment count %d (fee %lld, requested payment %lld, returned mail ID %d). %s Failure reply follows.",
                senderId, receiverId, itemCount, static_cast<long long>(postFee), static_cast<long long>(tradeMoney), postId, systemSender ? "No sender warehouse preparation was performed." : "Attachment and fee preparation already ran and is not reversed here.");
            // sendReply(-5, 0, 0)
            sendReply(Post::FailedByUnknown, 0, 0);
            return false;
        }
    }

    WritePostLog(sender, receiver, postId, trade, itemCount, postFee, remainingAdena, tradeMoney, dailyRemaining);

    for (int index = 0; index < itemCount; ++index) {
        const int resultingItemId = resultingItemIds[static_cast<std::size_t>(index)];
        const int sourceItemId = itemIds[static_cast<std::size_t>(index)];
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_CreatePostItemForAdmin (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&resultingItemId);
        connection.BindParameter(&postId);
        connection.BindParameter(&senderId);
        if (!connection.ExecutePrepared(statement)) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[%s][%d] CreatePostItem failed. item id[%d] "
            //     L"mail id[%d] sender id [%d]",
            //     L"ServerPacketEx.cpp", systemSender ? 0x1fbc : 0x1f74, resultingItemId, postId, senderId
            // )
            // Log.Add(LOG_ERROR,
            //     "[mail] Cannot attach prepared item %d (source item %d, position %d of %d) to mail %d from sender %d to recipient %d. Remaining attachments are processed and success reply still follows.",
            //     resultingItemId, sourceItemId, index + 1, itemCount, postId, senderId, receiverId)
            L2_LOG_ERROR(Log,
                "[mail] Cannot attach prepared item %d (source item %d, position %d of %d) to mail %d from sender %d to recipient %d; remaining attachments are processed and success reply still follows.",
                resultingItemId, sourceItemId, index + 1, itemCount, postId, senderId, receiverId);
        }

        responseBytes += ::Assemble(
            responseItems.data() + responseBytes, static_cast<int>(responseItems.size()) - responseBytes, "dQ", sourceItemId,
            remainingAmounts[static_cast<std::size_t>(index)]
        );
    }

    sendReply(1, postId, itemCount);
    return false;
}

bool RequestSendPost(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00562bbc.
    L2_GUARD_FUNCTION();

    int postId = 0;
    ::Disassemble(packet, "d", &postId);
    PostRecord post;
    DBConn connection(runtime::process::CurrentDatabaseId());
    const bool loaded = LoadPostByPostId(connection, postId, post);
    int result = 0;
    if (!loaded) {
        // Log.Add(LOG_WARN,
        //     "[mail] Cannot submit mail %d: no mail row returned (execution or fetch failed). Reply reports failure.",
        //     postId)
        L2_LOG_WARN(Log,
            "[mail] Cannot submit mail %d: no mail row returned (execution or fetch failed); reply reports failure.",
            postId);
    }
    if (loaded && post.process < 1) {
        connection.UnBind();
        int process = 1;
        int deleteItem = 0;
        constexpr const wchar_t* statement = L"{Call lin_UpdatePostProcess (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&postId);
        connection.BindParameter(&process);
        connection.BindParameter(&deleteItem);
        // connection.ExecutePrepared(statement)
        if (!connection.ExecutePrepared(statement)) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Cannot save the submitted state for mail %d (sender %d, recipient %d). Submission reply still reports success.",
            //     postId, post.senderId, post.receiverId)
            L2_LOG_ERROR(Log,
                "[mail] Cannot save the submitted state for mail %d (sender %d, recipient %d); submission reply still reports success.",
                postId, post.senderId, post.receiverId);
        }
        result = 1;
    }

    socket->Send(
        "chddddddd", 0xc6, 0x72, result, postId, post.senderId, post.receiverId, post.trade, post.returnable, post.returned
    );
    return false;
}

bool RequestReceivePost(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00562f80. The two ordered-lock phases are
    // separate in the target: COD Adena first, attachment transfer second.
    L2_GUARD_FUNCTION();

    int postId = 0;
    int requestContext = 0;
    int senderId = 0;
    int receiverId = 0;
    int senderType = 0;
    int itemCount = 0;
    const unsigned char* cursor =
        ::Disassemble(packet, "dddddd", &postId, &requestContext, &senderId, &receiverId, &senderType, &itemCount);

    std::array<int, 8> itemIds{};
    std::array<std::int64_t, 8> amounts{};
    std::array<int, 8> consumeTypes{};
    std::int64_t requiredMoney = 0;

    auto sendDirectReply = [&](int result) {
        socket->Send(
            "chddddddQdd", 0xc6, 0x73, result, postId, requestContext, senderId, receiverId, 0,
            static_cast<long long>(requiredMoney), 0, 0
        );
    };

    CUserSP receiver = g_userDb.GetUser(receiverId, true, false, runtime::process::CurrentDatabaseId());
    CUserSP sender = senderId < 1 ? receiver : g_userDb.GetUser(senderId, true, false, runtime::process::CurrentDatabaseId());
    if (!receiver || !sender || itemCount > 8) {
        if (!receiver || !sender) {
            // Log.AddT(LOG_ERROR, L"Invalid user id in receiving post. Send %d receive %d", senderId, receiverId)
            // Log.Add(LOG_ERROR,
            //     "[mail] Cannot receive mail %d: %s could not be loaded (sender %d, receiver %d). Failure reply follows.",
            //     postId, !receiver ? "receiver" : "sender", senderId, receiverId)
            L2_LOG_ERROR(Log,
                "[mail] Cannot receive mail %d: %s could not be loaded (sender %d, receiver %d); failure reply follows.",
                postId, !receiver ? "receiver" : "sender", senderId, receiverId);
        }
        if (itemCount > 8) {
            // Log.Add(LOG_WARN,
            //     "[mail] Receipt request rejected for mail %d: attachment count %d exceeds the limit of 8. Failure reply follows.",
            //     postId, itemCount)
            L2_LOG_WARN(Log,
                "[mail] Receipt request rejected for mail %d: attachment count %d exceeds the limit of 8; failure reply follows.",
                postId, itemCount);
        }
        sendDirectReply(0);
        return false;
    }

    if (itemCount < 1) {
        sendDirectReply(1);
        DBConn connection(runtime::process::CurrentDatabaseId());
        int process = 3;
        int deleteItem = 1;
        constexpr const wchar_t* statement = L"{Call lin_UpdatePostProcess (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&postId);
        connection.BindParameter(&process);
        connection.BindParameter(&deleteItem);
        // connection.ExecutePrepared(statement)
        if (!connection.ExecutePrepared(statement)) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Cannot save received state for mail %d (sender %d, receiver %d, requested attachment count %d). Success reply already sent.",
            //     postId, senderId, receiverId, itemCount)
            // L2_LOG_ERROR(Log,
            //     "[mail] Cannot save received state for mail %d (sender %d, receiver %d, requested attachment count %d). Success reply already sent.",
            //     postId, senderId, receiverId, itemCount)
            L2_LOG_ERROR(Log,
                "[mail] Cannot save received state for mail %d (sender %d, receiver %d, requested attachment count %d). A success reply was already attempted; delivery is not confirmed.",
                postId, senderId, receiverId, itemCount);
        }
        return false;
    }

    for (int index = 0; index < itemCount; ++index) {
        cursor = ::Disassemble(
            cursor, "dQd", &itemIds[static_cast<std::size_t>(index)], &amounts[static_cast<std::size_t>(index)],
            &consumeTypes[static_cast<std::size_t>(index)]
        );
    }
    ::Disassemble(cursor, "Q", &requiredMoney);
    if (senderId < 0)
        requiredMoney = 0;

    CWareHouseSP receiverInventory = receiver->GetWareHouse(0, true);
    CWareHouseSP senderPostWarehouse = sender->GetWareHouse(4, true);
    CWareHouseSP senderInventory = sender->GetWareHouse(0, true);
    if (!receiverInventory || !senderPostWarehouse || !senderInventory) {
        // Log.AddT(LOG_ERROR, L"Error, Invalid sp.")
        // Log.Add(LOG_ERROR,
        //     "[mail] Cannot receive mail %d (sender %d, receiver %d): %s is unavailable. Failure reply follows.",
        //     postId, senderId, receiverId, !receiverInventory ? "receiver inventory" : (!senderPostWarehouse ? "sender mail warehouse" : "sender inventory"))
        L2_LOG_ERROR(Log,
            "[mail] Cannot receive mail %d (sender %d, receiver %d): %s is unavailable; failure reply follows.",
            postId, senderId, receiverId, !receiverInventory ? "receiver inventory" : (!senderPostWarehouse ? "sender mail warehouse" : "sender inventory"));
        sendDirectReply(0);
        return false;
    }

    {
        std::array<char16_t, 1001> postContents{};
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Bind(postContents.data(), 0x7d2);
        constexpr const wchar_t* statement = L"{Call lin_LoadPostContent (?)}";
        connection.Prepare(statement);
        connection.BindParameter(&postId);
        if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
            // Log.AddT(LOG_ERROR, L"Cannot execute loadpostcontent. %d", postId)
            // Log.Add(LOG_ERROR,
            //     "[mail] No content row returned for mail %d (execution or fetch failed). Payment and attachment processing continue.",
            //     postId)
            L2_LOG_ERROR(Log,
                "[mail] No content row returned for mail %d (execution or fetch failed); payment and attachment processing continue.",
                postId);
        }
    }

    int recipientAdenaItemId = senderId < 1 ? 2 : 1;
    std::int64_t recipientAdenaAmount = 0;
    int deletedPayerAdenaItemId = 0;
    int payerAdenaRetained = 1;
    if (receiverInventory.get() != senderInventory.get() && requiredMoney > 0) {
        bool paid = false;
        if (receiverInventory->GetLockAddr() == senderInventory->GetLockAddr()) {
            {
                L2_WRITE_GUARD(*receiverInventory.operator->());
                paid = receiverInventory->TradeMoney(
                    socket, senderInventory.get(), requiredMoney, recipientAdenaItemId, recipientAdenaAmount,
                    deletedPayerAdenaItemId, payerAdenaRetained
                );
            }
        } else if (receiverInventory->GetLockAddr() < senderInventory->GetLockAddr()) {
            L2_WRITE_GUARD(*receiverInventory.operator->());
            {
                L2_WRITE_GUARD(*senderInventory.operator->());
                paid = receiverInventory->TradeMoney(
                    socket, senderInventory.get(), requiredMoney, recipientAdenaItemId, recipientAdenaAmount,
                    deletedPayerAdenaItemId, payerAdenaRetained
                );
            }
        } else {
            L2_WRITE_GUARD(*senderInventory.operator->());
            {
                L2_WRITE_GUARD(*receiverInventory.operator->());
                paid = receiverInventory->TradeMoney(
                    socket, senderInventory.get(), requiredMoney, recipientAdenaItemId, recipientAdenaAmount,
                    deletedPayerAdenaItemId, payerAdenaRetained
                );
            }
        }
        if (!paid) {
            // Log.AddT(
            //     LOG_ERROR, L"Trade money couldn't pay. %lld for %d -> %d", static_cast<long long>(requiredMoney), senderId,
            //     receiverId
            // )
            // Log.Add(LOG_WARN,
            //     "[mail] Payment of %lld adena from payer %d to recipient %d failed for mail %d. Attachment transfer is not attempted and failure reply follows.",
            //     static_cast<long long>(requiredMoney), receiverId, senderId, postId)
            L2_LOG_WARN(Log,
                "[mail] Payment of %lld adena from payer %d to recipient %d failed for mail %d; attachment transfer is not attempted and failure reply follows.",
                static_cast<long long>(requiredMoney), receiverId, senderId, postId);
            sendDirectReply(0);
            return false;
        }
    }

    std::int64_t transferredAdena = 0;
    bool received = false;
    if (receiverInventory->GetLockAddr() == senderPostWarehouse->GetLockAddr()) {
        {
            L2_WRITE_GUARD(*receiverInventory.operator->());
            received = senderPostWarehouse->ReceivePost(
                socket, receiverInventory.get(), itemCount, itemIds.data(), amounts.data(), consumeTypes.data(), postId,
                requestContext, senderId, receiverId, recipientAdenaItemId, recipientAdenaAmount, deletedPayerAdenaItemId,
                payerAdenaRetained, 0, transferredAdena, requiredMoney
            );
        }
    } else if (receiverInventory->GetLockAddr() < senderPostWarehouse->GetLockAddr()) {
        L2_WRITE_GUARD(*receiverInventory.operator->());
        {
            L2_WRITE_GUARD(*senderPostWarehouse.operator->());
            received = senderPostWarehouse->ReceivePost(
                socket, receiverInventory.get(), itemCount, itemIds.data(), amounts.data(), consumeTypes.data(), postId,
                requestContext, senderId, receiverId, recipientAdenaItemId, recipientAdenaAmount, deletedPayerAdenaItemId,
                payerAdenaRetained, 0, transferredAdena, requiredMoney
            );
        }
    } else {
        L2_WRITE_GUARD(*senderPostWarehouse.operator->());
        {
            L2_WRITE_GUARD(*receiverInventory.operator->());
            received = senderPostWarehouse->ReceivePost(
                socket, receiverInventory.get(), itemCount, itemIds.data(), amounts.data(), consumeTypes.data(), postId,
                requestContext, senderId, receiverId, recipientAdenaItemId, recipientAdenaAmount, deletedPayerAdenaItemId,
                payerAdenaRetained, 0, transferredAdena, requiredMoney
            );
        }
    }

    if (!received) {
        sendDirectReply(0);
        return false;
    }

    WriteReceivePostLog(0x1f74, receiver, sender, postId, itemCount, requiredMoney, recipientAdenaAmount, transferredAdena);
    // if (senderType == 13) {
    if (senderType == Post::ToAdminWeb) {
        WriteReceivePostLog(
            0x2a8, receiver, sender, postId, itemCount, requiredMoney, recipientAdenaAmount, transferredAdena
        );
    }

    {
        DBConn connection(runtime::process::CurrentDatabaseId());
        int process = 3;
        int deleteItem = 1;
        constexpr const wchar_t* statement = L"{Call lin_UpdatePostProcess (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&postId);
        connection.BindParameter(&process);
        connection.BindParameter(&deleteItem);
        // connection.ExecutePrepared(statement)
        if (!connection.ExecutePrepared(statement)) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Cannot save received state for mail %d (sender %d, receiver %d, requested attachment count %d). Success reply already sent.",
            //     postId, senderId, receiverId, itemCount)
            // L2_LOG_ERROR(Log,
            //     "[mail] Cannot save received state for mail %d (sender %d, receiver %d, requested attachment count %d). Success reply already sent.",
            //     postId, senderId, receiverId, itemCount)
            L2_LOG_ERROR(Log,
                "[mail] Cannot save received state for mail %d (sender %d, receiver %d, requested attachment count %d). A success reply was already attempted; delivery is not confirmed.",
                postId, senderId, receiverId, itemCount);
        }
    }
    // ExecutePostIdStatement(L"{Call lin_DeletePostItemForAdmin (?)}", postId)
    if (!ExecutePostIdStatement(L"{Call lin_DeletePostItemForAdmin (?)}", postId)) {
        // Log.Add(LOG_ERROR,
        //     "[mail] Cannot remove attachment records after receiving mail %d (sender %d, receiver %d). Attachment processing and success reply already completed.",
        //     postId, senderId, receiverId)
        L2_LOG_ERROR(Log,
            "[mail] Cannot remove attachment records after receiving mail %d (sender %d, receiver %d); attachment processing and success reply already completed.",
            postId, senderId, receiverId);
    }
    return false;
}

bool RequestCancelPost(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005654a4.
    L2_GUARD_FUNCTION();

    int postId = 0;
    int requestContext = 0;
    int senderId = 0;
    int receiverId = 0;
    int itemCount = 0;
    const unsigned char* cursor =
        ::Disassemble(packet, "ddddd", &postId, &requestContext, &senderId, &receiverId, &itemCount);

    std::array<int, 8> itemIds{};
    std::array<std::int64_t, 8> amounts{};
    std::array<int, 8> consumeTypes{};
    if (itemCount > 0 && itemCount <= 8) {
        for (int index = 0; index < itemCount; ++index) {
            cursor = ::Disassemble(
                cursor, "dQd", &itemIds[static_cast<std::size_t>(index)], &amounts[static_cast<std::size_t>(index)],
                &consumeTypes[static_cast<std::size_t>(index)]
            );
        }
    }
    std::int64_t postMoney = 0;
    if (itemCount <= 8) {
        ::Disassemble(cursor, "Q", &postMoney);
    }

    auto sendDirectReply = [&](int result) {
        socket->Send("chdddddd", 0xc6, 0x75, result, postId, requestContext, senderId, receiverId, 0);
    };
    if (itemCount > 8) {
        // Log.Add(LOG_WARN,
        //     "[mail] Cancellation request rejected for mail %d: attachment count %d exceeds the limit of 8. Failure reply follows.",
        //     postId, itemCount)
        L2_LOG_WARN(Log,
            "[mail] Cancellation request rejected for mail %d: attachment count %d exceeds the limit of 8; failure reply follows.",
            postId, itemCount);
        sendDirectReply(0);
        return false;
    }

    int process = 0;
    bool loadedProcess = false;
    {
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Bind(&process);
        constexpr const wchar_t* statement = L"{Call lin_GetLoadPostProcess (?)}";
        connection.Prepare(statement);
        connection.BindParameter(&postId);
        loadedProcess = connection.ExecutePrepared(statement) && connection.Fetch();
    }
    if (!loadedProcess) {
        // Log.Add(LOG_WARN,
        //     "[mail] Cannot cancel mail %d: no mail-state row returned (execution or fetch failed). Failure reply follows.",
        //     postId)
        L2_LOG_WARN(Log,
            "[mail] Cannot cancel mail %d: no mail-state row returned (execution or fetch failed); failure reply follows.",
            postId);
        sendDirectReply(0);
        return false;
    }

    CUserSP auditTarget = g_userDb.GetUser(receiverId, true, false, runtime::process::CurrentDatabaseId());
    if (process > 2) {
        // Log.AddT(LOG_ERROR, L"user attempted to cancel received post %d.", postId)
        // Log.Add(LOG_WARN,
        //     "[mail] Cancellation rejected for mail %d: state %d is no longer cancellable. Failure reply follows.",
        //     postId, process)
        L2_LOG_WARN(Log,
            "[mail] Cancellation rejected for mail %d: state %d is no longer cancellable; failure reply follows.",
            postId, process);
        sendDirectReply(0);
        return false;
    }

    if (itemCount < 1) {
        // ExecutePostIdStatement(L"{Call lin_DeletePost (?)}", postId)
        if (!ExecutePostIdStatement(L"{Call lin_DeletePost (?)}", postId)) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Cannot delete mail while cancelling mail %d for sender %d (attachment count %d). %s Remaining cancellation steps continue.",
            //     postId, senderId, itemCount, itemCount < 1 ? "Success reply still follows." : "Success reply already sent.")
            // L2_LOG_ERROR(Log,
            // "[mail] Cannot delete mail while cancelling mail %d for sender %d (attachment count %d). %s Remaining cancellation steps continue.",
            // postId, senderId, itemCount, itemCount < 1 ? "Success reply still follows." : "Success reply already sent.")
            L2_LOG_ERROR(Log,
                "[mail] Cannot delete mail while cancelling mail %d for sender %d (attachment count %d). A success reply will still be attempted.",
                postId, senderId, itemCount);
        }
        // ExecutePostIdStatement(L"{Call lin_DeletePostItemForAdmin (?)}", postId)
        if (!ExecutePostIdStatement(L"{Call lin_DeletePostItemForAdmin (?)}", postId)) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Cannot delete attachment records while cancelling mail %d for sender %d (attachment count %d). %s Remaining cancellation steps continue.",
            //     postId, senderId, itemCount, itemCount < 1 ? "Success reply still follows." : "Success reply already sent.")
            // L2_LOG_ERROR(Log,
            // "[mail] Cannot delete attachment records while cancelling mail %d for sender %d (attachment count %d). %s Remaining cancellation steps continue.",
            // postId, senderId, itemCount, itemCount < 1 ? "Success reply still follows." : "Success reply already sent.")
            L2_LOG_ERROR(Log,
                "[mail] Cannot delete attachment records while cancelling mail %d for sender %d (attachment count %d). A success reply will still be attempted.",
                postId, senderId, itemCount);
        }
        sendDirectReply(1);
        return false;
    }

    CUserSP sender = g_userDb.GetUser(senderId, true, false, runtime::process::CurrentDatabaseId());
    if (!sender) {
        // Log.Add(LOG_ERROR,
        //     "[mail] Cannot cancel mail %d: sender %d could not be loaded after reading the mail state. This branch sends no cancellation reply.",
        //     postId, senderId)
        L2_LOG_ERROR(Log,
            "[mail] Cannot cancel mail %d: sender %d could not be loaded after reading the mail state; this branch sends no cancellation reply.",
            postId, senderId);
        // The current branch has no explicit reply when this second lookup
        // fails after a valid process row.
        return false;
    }
    CWareHouseSP postWarehouse = sender->GetWareHouse(4, true);
    CWareHouseSP inventory = sender->GetWareHouse(0, true);
    if (!postWarehouse || !inventory) {
        // Log.AddT(LOG_ERROR, L"Error, Invalid sp.")
        // Log.Add(LOG_ERROR,
        //     "[mail] Cannot cancel mail %d for sender %d: %s is unavailable. This branch sends no cancellation reply.",
        //     postId, senderId, !postWarehouse ? "mail warehouse" : "inventory")
        L2_LOG_ERROR(Log,
            "[mail] Cannot cancel mail %d for sender %d: %s is unavailable; this branch sends no cancellation reply.",
            postId, senderId, !postWarehouse ? "mail warehouse" : "inventory");
        return false;
    }

    bool cancelled = false;
    if (postWarehouse->GetLockAddr() == inventory->GetLockAddr()) {
        {
            L2_WRITE_GUARD(*inventory.operator->());
            cancelled = postWarehouse->CancelPost(
                socket, inventory.get(), itemCount, itemIds.data(), amounts.data(), consumeTypes.data(), postId, requestContext,
                senderId, receiverId
            );
        }
    } else if (postWarehouse->GetLockAddr() < inventory->GetLockAddr()) {
        L2_WRITE_GUARD(*postWarehouse.operator->());
        {
            L2_WRITE_GUARD(*inventory.operator->());
            cancelled = postWarehouse->CancelPost(
                socket, inventory.get(), itemCount, itemIds.data(), amounts.data(), consumeTypes.data(), postId, requestContext,
                senderId, receiverId
            );
        }
    } else {
        L2_WRITE_GUARD(*inventory.operator->());
        {
            L2_WRITE_GUARD(*postWarehouse.operator->());
            cancelled = postWarehouse->CancelPost(
                socket, inventory.get(), itemCount, itemIds.data(), amounts.data(), consumeTypes.data(), postId, requestContext,
                senderId, receiverId
            );
        }
    }

    if (cancelled) {
        WriteCancelPostLog(sender, receiverId, auditTarget, postId);
        // ExecutePostIdStatement(L"{Call lin_DeletePost (?)}", postId)
        if (!ExecutePostIdStatement(L"{Call lin_DeletePost (?)}", postId)) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Cannot delete mail while cancelling mail %d for sender %d (attachment count %d). %s Remaining cancellation steps continue.",
            //     postId, senderId, itemCount, itemCount < 1 ? "Success reply still follows." : "Success reply already sent.")
            // L2_LOG_ERROR(Log,
            // "[mail] Cannot delete mail while cancelling mail %d for sender %d (attachment count %d). %s Remaining cancellation steps continue.",
            // postId, senderId, itemCount, itemCount < 1 ? "Success reply still follows." : "Success reply already sent.")
            L2_LOG_ERROR(Log,
                "[mail] Cannot delete mail while cancelling mail %d for sender %d (attachment count %d). A success reply was already attempted; delivery is not confirmed.",
                postId, senderId, itemCount);
        }
        // ExecutePostIdStatement(L"{Call lin_DeletePostItemForAdmin (?)}", postId)
        if (!ExecutePostIdStatement(L"{Call lin_DeletePostItemForAdmin (?)}", postId)) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Cannot delete attachment records while cancelling mail %d for sender %d (attachment count %d). %s Remaining cancellation steps continue.",
            //     postId, senderId, itemCount, itemCount < 1 ? "Success reply still follows." : "Success reply already sent.")
            // L2_LOG_ERROR(Log,
            // "[mail] Cannot delete attachment records while cancelling mail %d for sender %d (attachment count %d). %s Remaining cancellation steps continue.",
            // postId, senderId, itemCount, itemCount < 1 ? "Success reply still follows." : "Success reply already sent.")
            L2_LOG_ERROR(Log,
                "[mail] Cannot delete attachment records while cancelling mail %d for sender %d (attachment count %d). A success reply was already attempted; delivery is not confirmed.",
                postId, senderId, itemCount);
        }
    } else {
        sendDirectReply(0);
    }
    return false;
}

bool RequestDeletePost(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00566d08. Bits 0/1 select sender/receiver-side
    // deletion. Attachments are returned from mail (warehouse 4) to the
    // receiver's warehouse 1 only when both visible sides are being removed.
    L2_GUARD_FUNCTION();

    int deleteFlags = 0;
    int senderId = 0;
    int receiverId = 0;
    int requestReason = 0;
    int requestCount = 0;
    const unsigned char* cursor =
        ::Disassemble(packet, "ddddd", &deleteFlags, &senderId, &receiverId, &requestReason, &requestCount);

    CUserSP auditReceiver =
        receiverId > 0 ? g_userDb.GetUser(receiverId, true, false, runtime::process::CurrentDatabaseId()) : CUserSP{};
    CUserSP auditSender =
        senderId > 0 ? g_userDb.GetUser(senderId, true, false, runtime::process::CurrentDatabaseId()) : CUserSP{};

    std::array<char, 0x5000> response{};
    int responseBytes = 0;
    int replySenderId = senderId;
    int replyReceiverId = receiverId;

    auto appendResult = [&](int result, int postId, int requestValue) {
        responseBytes += ::Assemble(
            response.data() + responseBytes, static_cast<int>(response.size()) - responseBytes, "ddd", result, postId,
            requestValue
        );
    };

    auto returnAttachments = [&](DBConn& connection, int postId, [[maybe_unused]] const wchar_t* lockFile, [[maybe_unused]] int lockLine) {
        connection.UnBind();
        PostRecord post;
        // if (!LoadPostByPostId(connection, postId, post))
            // return.
        if (!LoadPostByPostId(connection, postId, post)) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Attachment return skipped while deleting mail %d: mail lookup returned no result (execution or fetch failed). Physical deletion still continues.",
            //     postId)
            L2_LOG_ERROR(Log,
                "[mail] Attachment return skipped while deleting mail %d: mail lookup returned no result (execution or fetch failed); physical deletion still continues.",
                postId);
            return;
        }

        // The current handler reuses these two request locals for the full
        // row, so the reply header reflects the last deleted post.
        replySenderId = post.senderId;
        replyReceiverId = post.receiverId;

        connection.UnBind();
        int itemId = 0;
        std::int64_t amount = 0;
        connection.Bind(&itemId);
        connection.Bind(&amount);
        constexpr const wchar_t* loadItemsStatement = L"{Call lin_LoadPostItem (?)}";
        connection.Prepare(loadItemsStatement);
        connection.BindParameter(&postId);
        // if (!connection.ExecutePrepared(loadItemsStatement))
            // return.
        if (!connection.ExecutePrepared(loadItemsStatement)) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Attachment return skipped while deleting mail %d: attachment query failed. Physical deletion still continues.",
            //     postId)
            L2_LOG_ERROR(Log,
                "[mail] Attachment return skipped while deleting mail %d: attachment query failed; physical deletion still continues.",
                postId);
            return;
        }

        std::array<int, 8> itemIds{};
        std::array<std::int64_t, 8> amounts{};
        int loadedItemCount = 0;
        while (connection.Fetch()) {
            if (loadedItemCount > 7) {
                // Log.AddT(LOG_ERROR, L"[DeletePost] Too many Item Count In Mail %d. User %d", postId, post.senderId)
                // Log.Add(LOG_ERROR,
                //     "[mail] Mail %d from sender %d has more than %zu attachment rows. Collected %d, declared %d. Return processing continues using the declared count before physical deletion.",
                //     postId, post.senderId, itemIds.size(), loadedItemCount, post.itemCount)
                L2_LOG_ERROR(Log,
                    "[mail] Mail %d from sender %d has more than %zu attachment rows; collected %d, declared %d. Return processing continues using the declared count before physical deletion.",
                    postId, post.senderId, itemIds.size(), loadedItemCount, post.itemCount);
                break;
            }
            itemIds[static_cast<std::size_t>(loadedItemCount)] = itemId;
            amounts[static_cast<std::size_t>(loadedItemCount)] = amount;
            ++loadedItemCount;
        }
        if (loadedItemCount != post.itemCount) {
            // Log.AddT(LOG_ERROR, L"[DeletePost] Invalid Item Count In Mail %d. User %d.", postId, post.senderId)
            // Log.Add(LOG_ERROR,
            //     "[mail] Attachment count mismatch while deleting mail %d: declared %d, collected %d (sender %d). Return processing continues using the declared count.",
            //     postId, post.itemCount, loadedItemCount, post.senderId)
            L2_LOG_ERROR(Log,
                "[mail] Attachment count mismatch while deleting mail %d: declared %d, collected %d (sender %d); return processing continues using the declared count.",
                postId, post.itemCount, loadedItemCount, post.senderId);
        }

        CUserSP receiver = g_userDb.GetUser(post.receiverId, true, false, runtime::process::CurrentDatabaseId());
        // if (!receiver)
            // return.
        if (!receiver) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Attachment return skipped while deleting mail %d: receiver could not be loaded. Physical deletion still continues.",
            //     postId)
            L2_LOG_ERROR(Log,
                "[mail] Attachment return skipped while deleting mail %d: receiver could not be loaded; physical deletion still continues.",
                postId);
            return;
        }

        CWareHouseSP postWarehouse = receiver->GetWareHouse(4, true);
        CWareHouseSP destinationWarehouse = receiver->GetWareHouse(1, true);
        if (!postWarehouse || !destinationWarehouse) {
            // Log.AddT(LOG_ERROR, L"Error, Invalid sp.")
            // Log.Add(LOG_ERROR,
            //     "[mail] Attachment return skipped while deleting mail %d for receiver %d: %s is unavailable. Physical deletion still continues.",
            //     postId, post.receiverId, !postWarehouse ? "mail warehouse" : "destination warehouse")
            L2_LOG_ERROR(Log,
                "[mail] Attachment return skipped while deleting mail %d for receiver %d: %s is unavailable; physical deletion still continues.",
                postId, post.receiverId, !postWarehouse ? "mail warehouse" : "destination warehouse");
            return;
        }

        bool deleted = false;
        if (postWarehouse->GetLockAddr() == destinationWarehouse->GetLockAddr()) {
            // Log.AddT(LOG_ERROR, L"Same warehouse double writelock?-_-,")
            // Log.Add(LOG_WARN,
            //     "[mail] Return of attachments while deleting mail %d for receiver %d uses the same source and destination warehouse. Continuing under one lock.",
            //     postId, post.receiverId)
            L2_LOG_WARN(Log,
                "[mail] Return of attachments while deleting mail %d for receiver %d uses the same source and destination warehouse; continuing under one lock.",
                postId, post.receiverId);
            {
                L2_WRITE_GUARD(*destinationWarehouse.operator->());
                deleted = postWarehouse->DeletePost(
                    socket, destinationWarehouse.get(), post.itemCount, itemIds.data(), amounts.data(), post.receiverId
                );
            }
        } else if (destinationWarehouse->GetLockAddr() < postWarehouse->GetLockAddr()) {
            L2_WRITE_GUARD(*destinationWarehouse.operator->());
            {
                L2_WRITE_GUARD(*postWarehouse.operator->());
                deleted = postWarehouse->DeletePost(
                    socket, destinationWarehouse.get(), post.itemCount, itemIds.data(), amounts.data(), post.receiverId
                );
            }
        } else {
            L2_WRITE_GUARD(*postWarehouse.operator->());
            {
                L2_WRITE_GUARD(*destinationWarehouse.operator->());
                deleted = postWarehouse->DeletePost(
                    socket, destinationWarehouse.get(), post.itemCount, itemIds.data(), amounts.data(), post.receiverId
                );
            }
        }
        if (!deleted) {
            // Log.AddT(LOG_ERROR, L"Invalid item deleting in mail id %d", postId)
            // Log.Add(LOG_ERROR,
            //     "[mail] Attachment return failed while deleting mail %d for receiver %d. Physical deletion still continues.",
            //     postId, post.receiverId)
            L2_LOG_ERROR(Log,
                "[mail] Attachment return failed while deleting mail %d for receiver %d; physical deletion still continues.",
                postId, post.receiverId);
        }
    };

    for (int index = 0; index < requestCount; ++index) {
        int postId = 0;
        int requestValue = 0;
        cursor = ::Disassemble(cursor, "dd", &postId, &requestValue);

        int senderDeleted = 0;
        int receiverDeleted = 0;
        int itemCount = 0;
        int senderType = 0;
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Bind(&senderDeleted);
        connection.Bind(&receiverDeleted);
        connection.Bind(&itemCount);
        connection.Bind(&senderType);
        constexpr const wchar_t* loadDeletedStatement = L"{Call lin_LoadPostDeleted (?)}";
        connection.Prepare(loadDeletedStatement);
        connection.BindParameter(&postId);
        if (!connection.ExecutePrepared(loadDeletedStatement) || !connection.Fetch()) {
            // Log.Add(LOG_WARN,
            //     "[mail] Cannot delete mail %d: no deletion-state row returned (execution or fetch failed). This result reports failure and the remaining batch continues.",
            //     postId)
            L2_LOG_WARN(Log,
                "[mail] Cannot delete mail %d: no deletion-state row returned (execution or fetch failed); this result reports failure and the remaining batch continues.",
                postId);
            appendResult(0, postId, requestValue);
            continue;
        }
        connection.UnBind();

        // if (replySenderId < 0 || senderType != 0) {
        if (replySenderId < 0 || senderType != Post::UserToUser) {
            senderDeleted = 1;
            receiverDeleted = 1;
        }

        if (senderDeleted == 0 || receiverDeleted == 0) {
            const bool deleteSender = (deleteFlags & 1) != 0;
            const bool deleteReceiver = (deleteFlags & 2) != 0;
            const bool keepDeletedRow =
                (!deleteSender || (!deleteReceiver && receiverDeleted == 0)) && (!deleteReceiver || senderDeleted == 0);
            if (keepDeletedRow) {
                int newSenderDeleted = senderDeleted;
                int newReceiverDeleted = receiverDeleted;
                if (!deleteSender) {
                    newReceiverDeleted = 1;
                } else {
                    newSenderDeleted = 1;
                }
                constexpr const wchar_t* updateStatement = L"{Call lin_UpdatePostDeleted (?, ?, ?)}";
                connection.Prepare(updateStatement);
                connection.BindParameter(&postId);
                connection.BindParameter(&newSenderDeleted);
                connection.BindParameter(&newReceiverDeleted);
                // connection.ExecutePrepared(updateStatement)
                if (!connection.ExecutePrepared(updateStatement)) {
                    // Log.Add(LOG_ERROR,
                    //     "[mail] Cannot save deletion flags for mail %d (sender flag %d, receiver flag %d). The batch still reports success for this mail.",
                    //     postId, newSenderDeleted, newReceiverDeleted)
                    L2_LOG_ERROR(Log,
                        "[mail] Cannot save deletion flags for mail %d (sender flag %d, receiver flag %d); the batch still reports success for this mail.",
                        postId, newSenderDeleted, newReceiverDeleted);
                }
                WriteDeletePostLog(auditReceiver, auditSender, postId, deleteSender ? 0 : 1, requestReason);
            } else {
                if (itemCount > 0) {
                    returnAttachments(connection, postId, __WFILE__, __LINE__);
                }
                // ExecutePostIdStatement(L"{Call lin_DeletePost (?)}", postId)
                if (!ExecutePostIdStatement(L"{Call lin_DeletePost (?)}", postId)) {
                    // Log.Add(LOG_ERROR,
                    //     "[mail] Cannot delete mail row for mail %d after attachment handling. Remaining deletion steps continue and the batch still reports success for this mail.",
                    //     postId)
                    L2_LOG_ERROR(Log,
                        "[mail] Cannot delete mail row for mail %d after attachment handling; remaining deletion steps continue and the batch still reports success for this mail.",
                        postId);
                }
                WriteDeletePostLog(auditReceiver, auditSender, postId, 2, requestReason);
                // ExecutePostIdStatement(L"{Call lin_DeletePostCommissionItemData (?)}", postId)
                if (!ExecutePostIdStatement(L"{Call lin_DeletePostCommissionItemData (?)}", postId)) {
                    // Log.Add(LOG_ERROR,
                    //     "[mail] Cannot delete commission item details for mail %d after attachment handling. Remaining deletion steps continue and the batch still reports success for this mail.",
                    //     postId)
                    L2_LOG_ERROR(Log,
                        "[mail] Cannot delete commission item details for mail %d after attachment handling; remaining deletion steps continue and the batch still reports success for this mail.",
                        postId);
                }
            }
            appendResult(1, postId, requestValue);
            continue;
        }

        // if (replySenderId >= 0 && senderType == 0) {
        if (replySenderId >= 0 && senderType == Post::UserToUser) {
            // Log.AddT(LOG_ERROR, L"dead mail lives forever. %d", postId)
            // Log.Add(LOG_WARN,
            //     "[mail] Mail %d is marked deleted for both parties but still has a database row (sender flag %d, receiver flag %d). Physical deletion continues.",
            //     postId, senderDeleted, receiverDeleted)
            L2_LOG_WARN(Log,
                "[mail] Mail %d is marked deleted for both parties but still has a database row (sender flag %d, receiver flag %d); physical deletion continues.",
                postId, senderDeleted, receiverDeleted);
        }
        if (itemCount > 0) {
            returnAttachments(connection, postId, __WFILE__, __LINE__);
        }
        // ExecutePostIdStatement(L"{Call lin_DeletePost (?)}", postId)
        if (!ExecutePostIdStatement(L"{Call lin_DeletePost (?)}", postId)) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Cannot delete mail row for mail %d after attachment handling. Remaining deletion steps continue and the batch still reports success for this mail.",
            //     postId)
            L2_LOG_ERROR(Log,
                "[mail] Cannot delete mail row for mail %d after attachment handling; remaining deletion steps continue and the batch still reports success for this mail.",
                postId);
        }
        // ExecutePostIdStatement(L"{Call lin_DeletePostItemForAdmin (?)}", postId)
        if (!ExecutePostIdStatement(L"{Call lin_DeletePostItemForAdmin (?)}", postId)) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Cannot delete attachment records for mail %d after attachment handling. Remaining deletion steps continue and the batch still reports success for this mail.",
            //     postId)
            L2_LOG_ERROR(Log,
                "[mail] Cannot delete attachment records for mail %d after attachment handling; remaining deletion steps continue and the batch still reports success for this mail.",
                postId);
        }
        appendResult(1, postId, requestValue);
        WriteDeletePostLog(auditReceiver, auditSender, postId, 2, requestReason);
        // ExecutePostIdStatement(L"{Call lin_DeletePostCommissionItemData (?)}", postId)
        if (!ExecutePostIdStatement(L"{Call lin_DeletePostCommissionItemData (?)}", postId)) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Cannot delete commission item details for mail %d after attachment handling. Remaining deletion steps continue and the batch still reports success for this mail.",
            //     postId)
            L2_LOG_ERROR(Log,
                "[mail] Cannot delete commission item details for mail %d after attachment handling; remaining deletion steps continue and the batch still reports success for this mail.",
                postId);
        }
    }

    socket->Send(
        "chddddb", 0xc6, 0x76, deleteFlags, replySenderId, replyReceiverId, requestCount, responseBytes, response.data()
    );
    return false;
}

bool RequestOpenPost(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00568de4. Attachment serialization remains on
    // its shipping owner, CWareHouse::AssembleOpenPost (0x00600074).
    L2_GUARD_FUNCTION();

    int postId = 0;
    int requestContext = 0;
    int requestValue = 0;
    ::Disassemble(packet, "ddd", &postId, &requestContext, &requestValue);

    PostRecord post;
    CPacketHelperEx<20480> itemPacket;
    std::u16string senderName;
    std::u16string receiverName;
    int processTransitioned = 0;

    auto sendReply = [&](int result, const std::array<int, 8>* commissionData) {
        const std::u16string title = std::u16string(post.title.data());
        const std::u16string contents = std::u16string(post.contents.data());
        const int processBeforeOpen = post.process < 2 ? 1 : 0;
        if (commissionData) {
            socket->Send(
                "chdddSSdSSdbQdddddddddddddddd", 0xc6, 0x77, result, postId, requestContext, senderName.c_str(),
                receiverName.c_str(), post.trade, title.c_str(), contents.c_str(), post.itemCount, itemPacket.GetLength(),
                itemPacket.GetPacket(), post.tradeMoney, post.returnable, post.returned,
                processTransitioned, post.sentTime, requestValue, processBeforeOpen, post.senderType, post.recordedId,
                (*commissionData)[0], (*commissionData)[1], (*commissionData)[2], (*commissionData)[3], (*commissionData)[4],
                (*commissionData)[5], (*commissionData)[6], (*commissionData)[7]
            );
            return;
        }

        socket->Send(
            "chdddSSdSSdbQdddddddd", 0xc6, 0x77, result, postId, requestContext, senderName.c_str(), receiverName.c_str(),
            post.trade, title.c_str(), contents.c_str(), post.itemCount, itemPacket.GetLength(), itemPacket.GetPacket(),
            post.tradeMoney, post.returnable, post.returned, processTransitioned, post.sentTime,
            requestValue, processBeforeOpen, post.senderType, post.recordedId
        );
    };

    DBConn connection(runtime::process::CurrentDatabaseId());
    if (!LoadPostByPostId(connection, postId, post)) {
        // Log.Add(LOG_WARN,
        //     "[mail] Cannot open mail %d: no mail row returned (execution or fetch failed). Failure reply follows.",
        //     postId)
        L2_LOG_WARN(Log,
            "[mail] Cannot open mail %d: no mail row returned (execution or fetch failed); failure reply follows.",
            postId);
        sendReply(0, nullptr);
        return false;
    }

    connection.UnBind();
    int loadedItemId = 0;
    std::int64_t loadedAmount = 0;
    connection.Bind(&loadedItemId);
    connection.Bind(&loadedAmount);
    constexpr const wchar_t* loadItemsStatement = L"{Call lin_LoadPostItem (?)}";
    connection.Prepare(loadItemsStatement);
    connection.BindParameter(&postId);
    if (!connection.ExecutePrepared(loadItemsStatement)) {
        // Log.Add(LOG_ERROR,
        //     "[mail] Cannot open mail %d: attachment query failed. This branch sends no open-mail reply.",
        //     postId)
        L2_LOG_ERROR(Log,
            "[mail] Cannot open mail %d: attachment query failed; this branch sends no open-mail reply.",
            postId);
        // The current owner exits without a reply on this database failure.
        return false;
    }

    std::array<int, 8> itemIds{};
    int loadedItemCount = 0;
    while (connection.Fetch()) {
        if (loadedItemCount > 7) {
            // Log.AddT(LOG_ERROR, L"[OpenPost] Too many Item Count In Mail %d. User %d", postId, post.senderId)
            // Log.Add(LOG_ERROR,
            //     "[mail] Mail %d from sender %d has more than %zu attachment rows. Only the first %d rows are collected for the open-mail reply.",
            //     postId, post.senderId, itemIds.size(), loadedItemCount)
            L2_LOG_ERROR(Log,
                "[mail] Mail %d from sender %d has more than %zu attachment rows; only the first %d rows are collected for the open-mail reply.",
                postId, post.senderId, itemIds.size(), loadedItemCount);
            break;
        }
        itemIds[static_cast<std::size_t>(loadedItemCount)] = loadedItemId;
        ++loadedItemCount;
    }
    if (loadedItemCount != post.itemCount) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[OpenPost] Invalid Item Count In Mail %d. User %d. "
        //     L"LoadedCnt : %d. ItemCnt : %d.",
        //     postId, post.senderId, loadedItemCount, post.itemCount
        // )
        // Log.Add(LOG_ERROR,
        //     "[mail] Attachment count mismatch when opening mail %d from sender %d: declared %d, collected %d. Reply uses the collected count.",
        //     postId, post.senderId, post.itemCount, loadedItemCount)
        L2_LOG_ERROR(Log,
            "[mail] Attachment count mismatch when opening mail %d from sender %d: declared %d, collected %d; reply uses the collected count.",
            postId, post.senderId, post.itemCount, loadedItemCount);
        post.itemCount = loadedItemCount;
    }
    connection.UnBind();

    CUserSP receiver = g_userDb.GetUser(post.receiverId, true, false, runtime::process::CurrentDatabaseId());
    bool missingReceiverAccepted = false;
    if (receiver) {
        receiverName = std::u16string(receiver->GetCharName());
    } else {
        receiverName = u"Unknown";
        if (requestContext == 0) {
            missingReceiverAccepted = true;
        } else {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"Invalid character requested open post. char [%d], "
            //     L"post [%d]",
            //     post.receiverId, postId
            // )
            // Log.Add(LOG_ERROR,
            //     "[mail] Receiver %d is unavailable while opening mail %d (request context %d). Reply names the receiver as Unknown and omits attachments.",
            //     post.receiverId, postId, requestContext)
            L2_LOG_ERROR(Log,
                "[mail] Receiver %d is unavailable while opening mail %d (request context %d); reply names the receiver as Unknown and omits attachments.",
                post.receiverId, postId, requestContext);
            post.itemCount = 0;
        }
    }

    CUserSP sender;
    if (post.senderId < 1) {
        sender = receiver;
    } else {
        sender = g_userDb.GetUser(post.senderId, true, false, runtime::process::CurrentDatabaseId());
    }

    // if (post.senderType == 0) {
    if (post.senderType == Post::UserToUser) {
        if (post.recordedId < 1) {
            senderName = u"Unknown";
        } else {
            CUserSP recorded = g_userDb.GetUser(post.recordedId, true, false, runtime::process::CurrentDatabaseId());
            senderName = recorded ? std::u16string(recorded->GetCharName()) : std::u16string(u"Unknown");
        }
    // } else if (post.senderType == 13) {
    } else if (post.senderType == Post::ToAdminWeb) {
        senderName = u"System";
    } else if (post.senderId < 1) {
        senderName = u"System";
    } else {
        senderName = sender ? std::u16string(sender->GetCharName()) : std::u16string(u"Unknown");
    }

    if (requestContext == 0) {
        if (missingReceiverAccepted)
            post.process = 1;
    } else if (post.process == 1) {
        const int process = post.itemCount < 1 ? 3 : 2;
        int deleteItem = 0;
        constexpr const wchar_t* updateStatement = L"{Call lin_UpdatePostProcess (?, ?, ?)}";
        connection.Prepare(updateStatement);
        connection.BindParameter(&postId);
        connection.BindParameter(&process);
        connection.BindParameter(&deleteItem);
        // connection.ExecutePrepared(updateStatement)
        if (!connection.ExecutePrepared(updateStatement)) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Cannot save state %d while opening mail %d. Reply still uses that state and marks a state transition.",
            //     process, postId)
            L2_LOG_ERROR(Log,
                "[mail] Cannot save state %d while opening mail %d; reply still uses that state and marks a state transition.",
                process, postId);
        }
        post.process = process;
        processTransitioned = 1;
    }

    if (post.itemCount > 0) {
        if (!sender) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Attachment owner is unavailable while opening mail %d (sender %d, receiver %d). Reply omits all %d attachments and still reports success.",
            //     postId, post.senderId, post.receiverId, post.itemCount)
            L2_LOG_ERROR(Log,
                "[mail] Attachment owner is unavailable while opening mail %d (sender %d, receiver %d); reply omits all %d attachments and still reports success.",
                postId, post.senderId, post.receiverId, post.itemCount);
            post.itemCount = 0;
        } else {
            CWareHouseSP postWarehouse = sender->GetWareHouse(4, true);
            if (!postWarehouse) {
                // Log.AddT(
                //     LOG_ERROR, L"[%s][%d]Cannot find warehouse, owner id[%d]", L"ServerPacketEx.cpp", 0x23df, post.senderId
                // )
                // Log.Add(LOG_ERROR,
                //     "[mail] Mail warehouse is unavailable for sender %d while opening mail %d. Reply retains attachment count %d without serialized item data.",
                //     post.senderId, postId, post.itemCount)
                L2_LOG_ERROR(Log,
                    "[mail] Mail warehouse is unavailable for sender %d while opening mail %d; reply retains attachment count %d without serialized item data.",
                    post.senderId, postId, post.itemCount);
            } else {
                const int expectedItemCount = post.itemCount;
                {
                    L2_WRITE_GUARD(*postWarehouse.operator->());
                    post.itemCount = postWarehouse->AssembleOpenPost(itemPacket, post.itemCount, itemIds.data());
                }
                if (post.itemCount != expectedItemCount) {
                    // Log.AddT(
                    //     LOG_ERROR,
                    //     L"item mismatch problem. old count %d / new count "
                    //     L"%d user [%d] mailid[%d]",
                    //     expectedItemCount, post.itemCount, post.senderId, postId
                    // )
                    // Log.Add(LOG_ERROR,
                    //     "[mail] Open-mail reply for mail %d, sender %d, includes %d of %d expected attachments. Unavailable attachments are omitted.",
                    //     postId, post.senderId, post.itemCount, expectedItemCount)
                    L2_LOG_ERROR(Log,
                        "[mail] Open-mail reply for mail %d, sender %d, includes %d of %d expected attachments; unavailable attachments are omitted.",
                        postId, post.senderId, post.itemCount, expectedItemCount);
                }
            }
        }
    }

    // if (post.senderType == 5) {
    if (post.senderType == Post::CommissionToUserBuy) {
        std::array<int, 8> commissionData{};
        connection.UnBind();
        for (int& value : commissionData)
            connection.Bind(&value);
        constexpr const wchar_t* commissionStatement = L"{Call lin_LoadPostCommissionItemData (?)}";
        connection.Prepare(commissionStatement);
        connection.BindParameter(&postId);
        if (!connection.ExecutePrepared(commissionStatement) || !connection.Fetch()) {
            // Log.AddT(
            //     LOG_ERROR, L"[%s][%d] PostCommissionItemData Fail!! Mail ID = [%d]", L"ServerPacketEx.cpp", 0x2406, postId
            // )
            // Log.Add(LOG_ERROR,
            //     "[mail] No commission item details returned for mail %d (execution or fetch failed). Successful open-mail reply still follows with default commission fields.",
            //     postId)
            L2_LOG_ERROR(Log,
                "[mail] No commission item details returned for mail %d (execution or fetch failed); successful open-mail reply still follows with default commission fields.",
                postId);
        }
        sendReply(1, &commissionData);
        return false;
    }

    sendReply(1, nullptr);
    return false;
}

bool RequestRejectPost(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00564a7c.
    L2_GUARD_FUNCTION();

    int postId = 0;
    int requestContext = 0;
    int requestValue = 0;
    ::Disassemble(packet, "ddd", &postId, &requestContext, &requestValue);

    PostRecord post;
    DBConn connection(runtime::process::CurrentDatabaseId());
    const bool loaded = LoadPostByPostId(connection, postId, post);
    CUserSP receiver;
    CUserSP sender;
    std::u16string receiverName;

    const bool rejectable = loaded && post.returnable != 0 && post.process <= 2 && post.returned == 0;
    if (rejectable) {
        connection.UnBind();
        const int newSenderId = -1;
        const int process = 0;
        const int returnable = 0;
        const int sentTime = runtime::crt::Time32Now();
        const int recordedId = post.receiverId;
        constexpr const wchar_t* statement = L"{Call lin_RejectPost (?, ?, ?, ?, ?, ?, ?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&postId);
        connection.BindParameter(&newSenderId);
        connection.BindParameter(&post.senderId);
        connection.BindParameter(&post.trade);
        connection.BindParameter(post.title.data());
        connection.BindParameter(&process);
        connection.BindParameter(&returnable);
        connection.BindParameter(&sentTime);
        connection.BindParameter(&recordedId);
        // connection.ExecutePrepared(statement)
        if (!connection.ExecutePrepared(statement)) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Cannot save rejection of mail %d (sender %d, receiver %d). Rejection reply still reports success and reverses the reply participant order.",
            //     postId, post.senderId, post.receiverId)
            L2_LOG_ERROR(Log,
                "[mail] Cannot save rejection of mail %d (sender %d, receiver %d); rejection reply still reports success and reverses the reply participant order.",
                postId, post.senderId, post.receiverId);
        }

        receiver = g_userDb.GetUser(post.receiverId, true, false, runtime::process::CurrentDatabaseId());
        if (receiver) {
            receiverName = std::u16string(receiver->GetCharName());
        }
        if (post.senderId > 0) {
            sender = g_userDb.GetUser(post.senderId, true, false, runtime::process::CurrentDatabaseId());
        }
        WriteRejectPostLog(receiver, sender, postId, requestContext, requestValue);
    } else if (loaded) {
        // Log.AddT(
        //     LOG_ERROR, L"this post %d cannot return. %d / %d / %d", postId, post.returnable, post.process, post.returned
        // )
        // Log.Add(LOG_WARN,
        //     "[mail] Return request rejected for mail %d: returnable flag %d, state %d, returned flag %d do not permit returning this mail.",
        //     postId, post.returnable, post.process, post.returned)
        L2_LOG_WARN(Log,
            "[mail] Return request rejected for mail %d: returnable flag %d, state %d, returned flag %d do not permit returning this mail.",
            postId, post.returnable, post.process, post.returned);
    }

    if (!loaded) {
        // Log.Add(LOG_WARN,
        //     "[mail] Cannot return mail %d: no mail row returned (execution or fetch failed). Failure reply follows.",
        //     postId)
        L2_LOG_WARN(Log,
            "[mail] Cannot return mail %d: no mail row returned (execution or fetch failed); failure reply follows.",
            postId);
    }

    const int replyFirstUserId = rejectable
        ? post.receiverId
        : post.senderId;
    const int replySecondUserId = rejectable
        ? post.senderId
        : post.receiverId;
    const int replyReturnable = rejectable
        ? 0
        : post.returnable;
    socket->Send(
        "chdddddddSdd", 0xc6, 0x74, rejectable ? 1 : 0, postId, replyFirstUserId, replySecondUserId, post.trade,
        replyReturnable, requestContext, receiverName.c_str(), requestValue, post.senderType
    );
    return false;
}

bool RequestLoadPostList(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00569cf0. One request emits the
    // sent-list 0xb7 packet followed by the received-list 0x78 packet.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);

    int postId = 0;
    int counterpartyId = 0;
    int trade = 0;
    std::array<char16_t, 61> title{};
    int process = 0;
    int returnable = 0;
    int returned = 0;
    int senderDeleted = 0;
    int receiverDeleted = 0;
    int sentTime = 0;
    int itemCount = 0;
    int senderType = 0;
    int recordedId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&postId);
    connection.Bind(&counterpartyId);
    connection.Bind(&trade);
    connection.Bind(title.data(), 0x7a);
    connection.Bind(&process);
    connection.Bind(&returnable);
    connection.Bind(&returned);
    connection.Bind(&senderDeleted);
    connection.Bind(&receiverDeleted);
    connection.Bind(&sentTime);
    connection.Bind(&itemCount);
    connection.Bind(&senderType);
    constexpr const wchar_t* sentStatement =
        L"{Call lin_LoadPostBySenderId (?)}";
    connection.Prepare(sentStatement);
    connection.BindParameter(&characterId);

    CPacketHelperEx<20480> sentRows;
    if (connection.ExecutePrepared(sentStatement)) {
        while (connection.Fetch()) {
            int deletionFlags = senderDeleted != 0 ? 1 : 0;
            if (receiverDeleted != 0) {
                deletionFlags |= 2;
            }
            if (senderDeleted != 0 && receiverDeleted != 0) {
                continue;
            }
            if (sentRows.Assemble(
                    "ddddddddd",
                    postId,
                    counterpartyId,
                    trade,
                    returnable,
                    process,
                    deletionFlags,
                    sentTime,
                    senderType,
                    itemCount) == 0) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"RequestLoadPostList1 sender_id[%d]",
                //     characterId)
                // Log.Add(
                //     LOG_ERROR,
                //     "[mail] Cannot include mail %d in the sent-mail summary for character %d. Returning %d assembled entries (%d bytes) and skipping the remaining rows.",
                //     postId, characterId, sentRows.GetAssembledCount(), sentRows.GetLength())
                L2_LOG_ERROR(Log,
                    "[mail] Cannot include mail %d in the sent-mail summary for character %d; returning %d assembled entries (%d bytes) and skipping the remaining rows.",
                    postId, characterId, sentRows.GetAssembledCount(), sentRows.GetLength());
                break;
            }
            if (sentRows.GetAssembledCount() > 300) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"post count over 250. sender %d",
                //     characterId)
                // Log.Add(
                //     LOG_WARN,
                //     "[mail] The sent-mail summary for character %d reached its stop threshold of more than 300 entries. Returning %d assembled entries without reading further rows.",
                //     characterId, sentRows.GetAssembledCount())
                L2_LOG_WARN(Log,
                    "[mail] The sent-mail summary for character %d reached its stop threshold of more than 300 entries; returning %d assembled entries without reading further rows.",
                    characterId, sentRows.GetAssembledCount());
                break;
            }
        }
    } else {
        // Log.Add(LOG_ERROR,
        //     "[mail] Cannot load the sent-mail summary for character %d: database query failed. An empty list reply still follows. The received-mail query still follows.",
        //     characterId)
        L2_LOG_ERROR(Log,
            "[mail] Cannot load the sent-mail summary for character %d: database query failed; an empty list reply still follows. The received-mail query still follows.",
            characterId);
    }
    socket->Send(
        "chddb",
        0xc6,
        0xb7,
        characterId,
        sentRows.GetAssembledCount(),
        sentRows.GetLength(),
        sentRows.GetPacket());

    connection.UnBind();
    connection.Bind(&postId);
    connection.Bind(&counterpartyId);
    connection.Bind(&trade);
    connection.Bind(title.data(), 0x7a);
    connection.Bind(&process);
    connection.Bind(&returnable);
    connection.Bind(&returned);
    connection.Bind(&senderDeleted);
    connection.Bind(&receiverDeleted);
    connection.Bind(&sentTime);
    connection.Bind(&itemCount);
    connection.BindNullable(&senderType, nullptr);
    connection.BindNullable(&recordedId, nullptr);
    constexpr const wchar_t* receivedStatement =
        L"{Call lin_LoadPostByReceiverId (?)}";
    connection.Prepare(receivedStatement);
    connection.BindParameter(&characterId);

    CPacketHelperEx<20480> receivedRows;
    if (connection.ExecutePrepared(receivedStatement)) {
        while (connection.Fetch()) {
            int deletionFlags = senderDeleted != 0 ? 1 : 0;
            if (receiverDeleted != 0) {
                deletionFlags |= 2;
            }
            if (senderDeleted != 0 && receiverDeleted != 0) {
                continue;
            }
            if (receivedRows.Assemble(
                    "ddddddddd",
                    postId,
                    counterpartyId,
                    trade,
                    returnable,
                    process,
                    deletionFlags,
                    sentTime,
                    senderType,
                    itemCount) == 0) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"RequestLoadPostList2 receiver_id[%d]",
                //     characterId)
                // Log.Add(
                //     LOG_ERROR,
                //     "[mail] Cannot include mail %d in the received-mail summary for character %d. Returning %d assembled entries (%d bytes) and skipping the remaining rows.",
                //     postId, characterId, receivedRows.GetAssembledCount(), receivedRows.GetLength())
                L2_LOG_ERROR(Log,
                    "[mail] Cannot include mail %d in the received-mail summary for character %d; returning %d assembled entries (%d bytes) and skipping the remaining rows.",
                    postId, characterId, receivedRows.GetAssembledCount(), receivedRows.GetLength());
                break;
            }
            if (receivedRows.GetAssembledCount() > 300) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[NO_ERROR] post count over 250. receiver_id[%d]",
                //     characterId)
                // Log.Add(LOG_WARN, "component=mail event=post_list_limit_exceeded character_id=%d threshold=300, partial list returned", characterId)
                // Log.Add(LOG_WARN, "[mail] Post list for character %d exceeded 300 entries. Returning the assembled partial list.", characterId)
                // Log.Add(
                //     LOG_WARN,
                //     "[mail] The received-mail summary for character %d reached its stop threshold of more than 300 entries. Returning %d assembled entries without reading further rows.",
                //     characterId, receivedRows.GetAssembledCount())
                L2_LOG_WARN(Log,
                    "[mail] The received-mail summary for character %d reached its stop threshold of more than 300 entries; returning %d assembled entries without reading further rows.",
                    characterId, receivedRows.GetAssembledCount());
                break;
            }
        }
    } else {
        // Log.Add(LOG_ERROR,
        //     "[mail] Cannot load the received-mail summary for character %d: database query failed. An empty list reply still follows.",
        //     characterId)
        L2_LOG_ERROR(Log,
            "[mail] Cannot load the received-mail summary for character %d: database query failed; an empty list reply still follows.",
            characterId);
    }
    socket->Send(
        "chddb",
        0xc6,
        0x78,
        characterId,
        receivedRows.GetAssembledCount(),
        receivedRows.GetLength(),
        receivedRows.GetPacket());
    return false;
}

bool RequestLoadReceivedPostList(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0056a2a0.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    int postId = 0;
    int senderId = 0;
    int trade = 0;
    std::array<char16_t, 61> title{};
    int process = 0;
    int returnable = 0;
    int returned = 0;
    int senderDeleted = 0;
    int receiverDeleted = 0;
    int sentTime = 0;
    int itemCount = 0;
    int senderType = 0;
    int recordedId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&postId);
    connection.Bind(&senderId);
    connection.Bind(&trade);
    connection.Bind(title.data(), 0x7a);
    connection.Bind(&process);
    connection.Bind(&returnable);
    connection.Bind(&returned);
    connection.Bind(&senderDeleted);
    connection.Bind(&receiverDeleted);
    connection.Bind(&sentTime);
    connection.Bind(&itemCount);
    connection.Bind(&senderType);
    connection.Bind(&recordedId);
    constexpr const wchar_t* statement = L"{Call lin_LoadPostByReceiverId (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);

    CPacketHelperEx<20480> rows;
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            if (process == 0 || receiverDeleted != 0)
                continue;

            std::u16string senderName;
            int systemSender = 0;
            // if (senderType == 0) {
            if (senderType == Post::UserToUser) {
                if (recordedId < 1) {
                    senderName = u"Unknown";
                } else {
                    CUserSP recorded = g_userDb.GetUser(recordedId, true, false, runtime::process::CurrentDatabaseId());
                    senderName = recorded ? std::u16string(recorded->GetCharName()) : std::u16string(u"Unknown");
                }
            } else {
                if (senderId < 0) {
                    senderName = u"System";
                    systemSender = 1;
                } else {
                    CUserSP sender = g_userDb.GetUser(senderId, true, false, runtime::process::CurrentDatabaseId());
                    senderName = sender ? std::u16string(sender->GetCharName()) : std::u16string(u"Unknown");
                }
            }

            if (rows.Assemble(
                    "dSdSdddddddd", postId, senderName.c_str(), trade, title.data(), process, sentTime, returnable,
                    returned, systemSender, itemCount, senderType, recordedId
                ) == 0) {
                // Log.Add(LOG_ERROR,
                //     "[mail] Cannot include mail %d in the received-mail list for character %d. Returning %d assembled entries (%d bytes) and skipping the remaining rows.",
                //     postId, characterId, rows.GetAssembledCount(), rows.GetLength())
                L2_LOG_ERROR(Log,
                    "[mail] Cannot include mail %d in the received-mail list for character %d; returning %d assembled entries (%d bytes) and skipping the remaining rows.",
                    postId, characterId, rows.GetAssembledCount(), rows.GetLength());
                break;
            }
        }
    } else {
        // Log.Add(LOG_ERROR,
        //     "[mail] Cannot load the received-mail list for character %d: database query failed. An empty list reply still follows.",
        //     characterId)
        L2_LOG_ERROR(Log,
            "[mail] Cannot load the received-mail list for character %d: database query failed; an empty list reply still follows.",
            characterId);
    }

    socket->Send("chddb", 0xc6, 0x7a, characterId, rows.GetAssembledCount(), rows.GetLength(), rows.GetPacket());
    return false;
}

bool RequestLoadSentPostList(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0056a804.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);

    int postId = 0;
    int receiverId = 0;
    int trade = 0;
    std::array<char16_t, 61> title{};
    int process = 0;
    int returnable = 0;
    int returned = 0;
    int senderDeleted = 0;
    int receiverDeleted = 0;
    int sentTime = 0;
    int itemCount = 0;
    int senderType = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&postId);
    connection.Bind(&receiverId);
    connection.Bind(&trade);
    connection.Bind(title.data(), 0x7a);
    connection.Bind(&process);
    connection.Bind(&returnable);
    connection.Bind(&returned);
    connection.Bind(&senderDeleted);
    connection.Bind(&receiverDeleted);
    connection.Bind(&sentTime);
    connection.Bind(&itemCount);
    connection.Bind(&senderType);
    constexpr const wchar_t* statement = L"{Call lin_LoadPostBySenderId (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);

    CPacketHelperEx<20480> rows;
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            // The sender-side list suppresses rows deleted by the sender.
            // receiverDeleted and returned are fetched but not serialized.
            if (senderDeleted != 0)
                continue;

            CUserSP receiver = g_userDb.GetUser(receiverId, true, false, runtime::process::CurrentDatabaseId());
            const std::u16string receiverName =
                receiver ? std::u16string(receiver->GetCharName()) : std::u16string(u"Unknown");
            if (rows.Assemble(
                    "dSdSddddd", postId, receiverName.c_str(), trade, title.data(), process, sentTime, returnable,
                    itemCount, senderType
                ) == 0) {
                // Log.Add(
                //     LOG_ERROR, "[%s][%d] RequestLoadSentPostList char_id[%d]", "ServerPacketEx.cpp", 0x256b,
                //     characterId
                // )
                // Log.Add(
                //     LOG_ERROR,
                //     "[mail] Cannot include mail %d in the sent-mail list for character %d. Returning %d assembled entries (%d bytes) and skipping the remaining rows.",
                //     postId, characterId, rows.GetAssembledCount(), rows.GetLength())
                L2_LOG_ERROR(Log,
                    "[mail] Cannot include mail %d in the sent-mail list for character %d; returning %d assembled entries (%d bytes) and skipping the remaining rows.",
                    postId, characterId, rows.GetAssembledCount(), rows.GetLength());
                break;
            }
        }
    } else {
        // Log.Add(LOG_ERROR,
        //     "[mail] Cannot load the sent-mail list for character %d: database query failed. An empty list reply still follows.",
        //     characterId)
        L2_LOG_ERROR(Log,
            "[mail] Cannot load the sent-mail list for character %d: database query failed; an empty list reply still follows.",
            characterId);
    }

    socket->Send("chddb", 0xc6, 0x79, characterId, rows.GetAssembledCount(), rows.GetLength(), rows.GetPacket());
    return false;
}

namespace RQExHandler {

bool RequestCacheTest(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0053c508.
    L2_GUARD_FUNCTION();

    int mode = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &mode);
    if (mode == 1) {
        int petId = 0;
        ::Disassemble(cursor, "d", &petId);
        CPetSP pet = g_petDb.GetPet(petId, runtime::process::CurrentDatabaseId());
    }
    return false;
}

bool RequestCommissionTradeRegisteredList(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00578d70.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);

    long long commissionId = 0;
    int firstType = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&commissionId);
    connection.Bind(&firstType);
    constexpr const wchar_t* statement = L"{Call lin_GetRegisteredCommission (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);

    int rowCount = 0;
    CPacketHelperEx<20480> rows;
    // if (connection.ExecutePrepared(statement) && connection.Fetch()) {
    const bool queryExecuted = connection.ExecutePrepared(statement);
    if (queryExecuted && connection.Fetch()) {
        do {
            rows.Assemble("Qd", commissionId, firstType);
            // The target increments its reply count of packet
            // assembly success.
            ++rowCount;
        } while (connection.Fetch());
    } else if (!queryExecuted) {
        // Log.Add(LOG_ERROR,
        //     "[commission] Cannot query registered listings for character %d in database %d. An empty-list reply follows.",
        //     characterId, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[commission] Cannot query registered listings for character %d in database %d; an empty-list reply follows.",
            characterId, runtime::process::CurrentDatabaseId());
    }

    if (rowCount != rows.GetAssembledCount()) {
        // Log.Add(LOG_ERROR,
        //     "[commission] Registered-list reply for character %d declares %d listings but contains %d assembled rows (%d bytes). The existing partial reply is still sent.",
        //     characterId, rowCount, rows.GetAssembledCount(), rows.GetLength())
        L2_LOG_ERROR(Log,
            "[commission] Registered-list reply for character %d declares %d listings but contains %d assembled rows (%d bytes); the existing partial reply is still sent.",
            characterId, rowCount, rows.GetAssembledCount(), rows.GetLength());
    }

    if (rowCount != 0) {
        socket->Send("chdddb", 0xc6, 0xad, 1, characterId, rowCount, rows.GetLength(), rows.GetPacket());
    } else {
        socket->Send("chdd", 0xc6, 0xad, -1, characterId);
    }
    return false;
}

bool RequestCommissionTradeSearchList(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00578fd8.
    L2_GUARD_FUNCTION();

    int requestId = 0;
    int depth = 0;
    int commissionType = 0;
    int itemGeneral = 0;
    int itemGrade = 0;
    std::array<char16_t, 100> packetItemName{};
    ::Disassemble(
        packet, "dddddS", &requestId, &depth, &commissionType, &itemGeneral, &itemGrade, 200, packetItemName.data()
    );
    const std::u16string itemName(packetItemName.data());

    long long commissionId = 0;
    int firstType = -1;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&commissionId);
    connection.Bind(&firstType);
    constexpr const wchar_t* statement = L"{Call lin_GetSearchCommission(?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&depth);
    connection.BindParameter(&commissionType);
    connection.BindParameter(&itemGeneral);
    connection.BindParameter(&itemGrade);
    connection.BindParameter(itemName.c_str());

    int rowCount = 0;
    CPacketHelperEx<20480> rows;
    // if (connection.ExecutePrepared(statement) && connection.Fetch()) {
    const bool queryExecuted = connection.ExecutePrepared(statement);
    if (queryExecuted && connection.Fetch()) {
        do {
            rows.Assemble("Qd", commissionId, firstType);
            ++rowCount;
            if (rowCount == 999)
                break;
        } while (connection.Fetch());
    } else if (!queryExecuted) {
        // Log.Add(LOG_ERROR,
        //     "[commission] Listing search query failed for request %d (depth %d, commission type %d, category %d, grade %d, name '%s'). An empty-list reply follows.",
        //     requestId, depth, commissionType, itemGeneral, itemGrade, WideToUtf8(itemName).c_str())
        // L2_LOG_ERROR(Log,
        //     "[commission] Listing search query failed for request %d (depth %d, commission type %d, category %d, grade %d, name '%s'). An empty-list reply follows.",
        //     requestId, depth, commissionType, itemGeneral, itemGrade, WideToUtf8(itemName).c_str())
        L2_LOG_ERROR(Log,
            "[commission] Listing search query failed for request %d (depth %d, commission type %d, category %d, grade %d, name '%s'); an empty-list reply follows.",
            requestId, depth, commissionType, itemGeneral, itemGrade, Utf16ToUtf8(itemName).c_str());
    }

    if (rowCount != 0) {
        socket->Send("chdddb", 0xc6, 0xae, 1, requestId, rowCount, rows.GetLength(), rows.GetPacket());
    } else {
        socket->Send("chdd", 0xc6, 0xae, -1, requestId);
    }
    return false;
}

bool RequestCommissionTradeDeleteAll(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0052ca58.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeleteAllCommission}";
    connection.Prepare(statement);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        // Log.AddT(
        //     LOG_DEBUG,
        //     L"[NO_ERROR][%s],[%d]Commission Data Delete All!! "
        //     L"UserDBID[%d]",
        //     L"ServerPacketEx.cpp", 0x30cb, characterId
        // )
        // Log.Add(LOG_NORMAL,
        //     "[commission] Global listing deletion returned a database result in database %d (request character %d). The operation has no character filter.",
        //     runtime::process::CurrentDatabaseId(), characterId)
        L2_LOG_INFO(Log,
            "[commission] Global listing deletion returned a database result in database %d (request character %d); the operation has no character filter.",
            runtime::process::CurrentDatabaseId(), characterId);
    }
    return false;
}

bool RequestCommissionTradeRegTotalCount(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005792b0.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int requestId = 0;
    ::Disassemble(packet, "dd", &characterId, &requestId);

    int totalCount = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&totalCount);
    constexpr const wchar_t* statement = L"{Call lin_RegTotalCountCommission(?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        socket->Send("chdddd", 0xc6, 0xaf, 1, characterId, requestId, totalCount);
    } else {
        // Log.Add(LOG_ERROR,
        //     "[commission] No registration count returned for character %d, request %d, database %d (execution or fetch failed). Failure reply follows.",
        //     characterId, requestId, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[commission] No registration count returned for character %d, request %d, database %d (execution or fetch failed); failure reply follows.",
            characterId, requestId, runtime::process::CurrentDatabaseId());
        socket->Send("chdd", 0xc6, 0xaf, -1, characterId);
    }
    return false;
}

bool RequestInstantAgitJob(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0058fdd4.
    L2_GUARD_FUNCTION();

    unsigned char job = 0;
    const unsigned char* cursor = ::Disassemble(packet, "c", &job);
    switch (job) {
    case 6:
        InstantAgitJobHandler::OnChooseOwner(socket, cursor);
        break;
    case 7:
        InstantAgitJobHandler::OnLoad(socket);
        break;
    case 11:
        InstantAgitJobHandler::LoadInstantAgitDeco(socket);
        break;
    case 13:
        InstantAgitJobHandler::LoadInstantAgitDecoNpc(socket);
        break;
    default:
        break;
    }
    return false;
}

bool RequestSaveVitality(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0052ccd8.
    L2_GUARD_FUNCTION();
    CVitality::SaveVitality(packet);
    return false;
}

} // namespace RQExHandler

bool LoadBlockedIP(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0056d070.
    L2_GUARD_FUNCTION();

    int status = 0;
    ::Disassemble(packet, "d", &status);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_GetBlockedIP (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&status);
    if (!connection.ExecutePrepared(statement))
        return false;

    long long startAddress = 0;
    long long endAddress = 0;
    connection.Bind(&startAddress);
    connection.Bind(&endAddress);

    std::array<char, 0x2004> rows;
    bool fullPage = false;
    do {
        int rowCount = 0;
        int rowBytes = 0;
        while (rowCount != 500 && connection.Fetch()) {
            rowBytes +=
                ::Assemble(rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "QQ", startAddress, endAddress);
            ++rowCount;
        }
        fullPage = rowCount == 500;
        socket->Send("chddb", 0xc6, 0x7b, status, rowCount, rowBytes, rows.data());
        if (status == 2)
            status = 1;
    } while (fullPage);
    return false;
}

bool LoadPetItemCount(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0053c634.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        socket->Send("chdd", 0xc6, 0x7c, characterId, user->LoadPetItemCount());
    }
    return false;
}

bool ReloadCharacter(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0054856c.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    unsigned int userSocketId = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    ::Disassemble(packet, "ddddd", &characterId, &userSocketId, &x, &y, &z);

    const int worldId = runtime::process::CurrentDatabaseId();
    CUserSP user = g_userDb.FindResidentUser(characterId);
    if (user) {
        user = g_userDb.ReLoadFromDb(user.get(), static_cast<CUser::INCLUDE_WAREHOUSE>(0));
    } else {
        user = g_userDb.GetUser(characterId, true, false, worldId);
        if (!user) {
            // Log.Add(
            //     LOG_ERROR, "[%s][%d]Cannot find user, char id[%d]", "ServerPacketEx.cpp", 0x2725, characterId
            // )
            // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, char id[%d]", "ServerPacketEx.cpp", 0x2725, characterId
            // )
            L2_LOG_ERROR(Log,
                "[character] Cannot reload character %d in world %d for socket %u: character unavailable. "
                "No selected-character reply will be sent.", characterId, worldId, userSocketId
            );
            return false;
        }
    }

    user->ReplyLoadSelectedChar(socket, userSocketId, static_cast<AddtionalLocationType>(2), x, y, z);
    return false;
}

bool RequestSummonSaveAbnormalStatusEx(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0056d960. Its generated 0x0054dba4 argument-
    // homing wrapper delegates to private statement helper 0x0054ca18,
    // which binds eight 16-byte records as five SQL parameters apiece.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    unsigned int characterId = 0;
    int summonClassId = 0;
    int summonDbId = 0;
    int atomicJobId = 0;
    int skillCount = 0;
    const unsigned char* cursor =
        ::Disassemble(packet, "dddddd", &userServerId, &characterId, &summonClassId, &summonDbId, &atomicJobId, &skillCount);

    auto sendReply = [&](int success) {
        socket->Send("chdddddd", 0xc6, 0xe8, success, userServerId, characterId, summonClassId, summonDbId, atomicJobId);
    };

    CUserSP user = g_userDb.GetUser(static_cast<int>(characterId), true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        sendReply(0);
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "ServerPacketEx.cpp", 0x27d2,
        //     static_cast<int>(characterId)
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "ServerPacketEx.cpp", 0x27d2,
        //     static_cast<int>(characterId)
        // )
        L2_LOG_ERROR(Log,
            "[summons] Cannot save abnormal status for summon %d (class %d): character %u unavailable. "
            "Failure reply requested for job %d.", summonDbId, summonClassId, characterId, atomicJobId
        );
        return false;
    }

    constexpr std::size_t abnormalStatusCount = 96;
    if (static_cast<unsigned int>(skillCount) > abnormalStatusCount) {
        sendReply(0);
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]overflow skill number[%d] char_id[%d] "
        //     "summon_class_id[%d] summon_id[%d]",
        //     "ServerPacketEx.cpp", 0x27e0, skillCount, static_cast<int>(characterId), summonClassId, summonDbId
        // )
        // L2_LOG_ERROR(Log,
        //     "[%s][%d]overflow skill number[%d] char_id[%d] "
        //     "summon_class_id[%d] summon_id[%d]",
        //     "ServerPacketEx.cpp", 0x27e0, skillCount, static_cast<int>(characterId), summonClassId, summonDbId
        // )
        L2_LOG_WARN(Log,
            "[summons] Rejected abnormal-status save: skill count %d is outside 0..96 for summon %d "
            "(class %d), character %u. Failure reply requested for job %d.",
            skillCount, summonDbId, summonClassId, characterId, atomicJobId
        );
        return false;
    }

    std::array<AbnormalStatus, abnormalStatusCount> statuses;
    for (AbnormalStatus& status : statuses) {
        status.skillId = 0;
        status.skillLevel = 0;
        status.skillSubLevel = 0;
        status.skillDuration = 0;
        status.skillSelfCast = 0;
    }
    for (int index = 0; index < skillCount; ++index) {
        int skillId = 0;
        int skillLevel = 0;
        int skillSubLevel = 0;
        int skillDuration = 0;
        int skillSelfCast = 0;
        cursor = ::Disassemble(cursor, "ddddd", &skillId, &skillLevel, &skillSubLevel, &skillDuration, &skillSelfCast);
        if (skillId == 0)
            continue;

        AbnormalStatus& status = statuses[static_cast<std::size_t>(index)];
        status.skillId = skillId;
        status.skillLevel = static_cast<std::int16_t>(skillLevel);
        status.skillSubLevel = static_cast<std::int16_t>(skillSubLevel);
        status.skillDuration = skillDuration;
        status.skillSelfCast = skillSelfCast;
    }

    {
        L2_WRITE_GUARD(*user.operator->());
        {
            DBConn connection(runtime::process::CurrentDatabaseId());
            if (statuses.front().skillId == 0) {
                constexpr const wchar_t* deleteStatement = L"{Call lin_DeleteSummonNewAbnormalState (?, ?, ?)}";
                connection.Prepare(deleteStatement);
                connection.BindParameter(&characterId);
                connection.BindParameter(&summonClassId);
                connection.BindParameter(&summonDbId);
                connection.ExecutePrepared(deleteStatement);
            }

            constexpr const wchar_t* updateBatchStatement = L"{Call lin_UpdateOrInsertSummonNewAbnormalStateBatch ("
                                                            L"?, ?, ?, ?, "
                                                            L"?, ?, ?, ?, ?, "
                                                            L"?, ?, ?, ?, ?, "
                                                            L"?, ?, ?, ?, ?, "
                                                            L"?, ?, ?, ?, ?, "
                                                            L"?, ?, ?, ?, ?, "
                                                            L"?, ?, ?, ?, ?, "
                                                            L"?, ?, ?, ?, ?, "
                                                            L"?, ?, ?, ?, ?)}";
            constexpr std::size_t statusesPerBatch = 8;
            constexpr std::size_t batchCount = abnormalStatusCount / statusesPerBatch;
            for (std::size_t batchIndex = 0; batchIndex < batchCount; ++batchIndex) {
                const std::size_t first = batchIndex * statusesPerBatch;
                if (statuses[first].skillId == 0)
                    break;

                const int deleteExisting = batchIndex == 0 ? 1 : 0;
                connection.Prepare(updateBatchStatement);
                connection.BindParameter(&characterId);
                connection.BindParameter(&summonClassId);
                connection.BindParameter(&summonDbId);
                connection.BindParameter(&deleteExisting);
                for (std::size_t statusIndex = first; statusIndex < first + statusesPerBatch; ++statusIndex) {
                    const AbnormalStatus& status = statuses[statusIndex];
                    connection.BindParameter(&status.skillId);
                    connection.BindParameter(&status.skillLevel);
                    connection.BindParameter(&status.skillDuration);
                    connection.BindParameter(&status.skillSelfCast);
                    connection.BindParameter(&status.skillSubLevel);
                }
                connection.ExecutePrepared(updateBatchStatement);
                connection.ResetParam();
            }
            sendReply(1);
        }
    }
    return false;
}

bool RequestSummonLoadAbnormalStatusEx(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0056dfdc.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    unsigned int characterId = 0;
    int summonClassId = 0;
    int summonDbId = 0;
    int rowCount = 0;
    CPacketHelperEx<20480> rows;
    ::Disassemble(packet, "dddd", &userServerId, &characterId, &summonClassId, &summonDbId);

    std::array<AbnormalStatus, 96> statuses;
    for (AbnormalStatus& status : statuses) {
        status.skillId = 0;
        status.skillLevel = 0;
        status.skillSubLevel = 0;
        status.skillDuration = 0;
        status.skillSelfCast = 0;
    }

    int skillId = 0;
    int skillLevel = 0;
    int skillSubLevel = 0;
    int skillDuration = 0;
    int skillSelfCast = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&skillId);
    connection.Bind(&skillLevel);
    connection.Bind(&skillSubLevel);
    connection.Bind(&skillDuration);
    connection.Bind(&skillSelfCast);
    constexpr const wchar_t* statement = L"{Call lin_LoadSummonAbnormalState(?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&summonClassId);
    connection.BindParameter(&summonDbId);

    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            AbnormalStatus& status = statuses[static_cast<std::size_t>(rowCount)];
            status.skillId = skillId;
            status.skillLevel = static_cast<std::int16_t>(skillLevel);
            status.skillSubLevel = static_cast<std::int16_t>(skillSubLevel);
            status.skillDuration = skillDuration;
            status.skillSelfCast = skillSelfCast;
            rows.Assemble(
                "ddddd", status.skillId, static_cast<int>(status.skillLevel), static_cast<int>(status.skillSubLevel),
                status.skillDuration, status.skillSelfCast
            );
            ++rowCount;
        }
    }
    socket->Send(
        "chdddddb", 0xc6, 0x7d, userServerId, characterId, summonClassId, summonDbId, rowCount, rows.GetLength(),
        rows.GetPacket()
    );
    return false;
}

bool RequestSaveSummon(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0053c784.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int summonClassId = 0;
    int summonDbId = 0;
    int summonLevel = 0;
    int summonOriginalRemainTime = 0;
    int summonRemainTime = 0;
    int summonExperienceRate = 0;
    double summonHp = 0.0;
    double summonMp = 0.0;
    int summonConsumeItemId = 0;
    int summonConsumeItemCount = 0;
    int summonIsSummoned = 0;
    ::Disassemble(
        packet, "dddddddffddd", &characterId, &summonClassId, &summonDbId, &summonLevel, &summonOriginalRemainTime,
        &summonRemainTime, &summonExperienceRate, &summonHp, &summonMp, &summonConsumeItemId, &summonConsumeItemCount,
        &summonIsSummoned
    );

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        user->SaveSummonInfo(
            summonClassId, summonDbId, summonLevel, summonOriginalRemainTime, summonRemainTime, summonExperienceRate,
            summonHp, summonMp, summonConsumeItemId, summonConsumeItemCount, summonIsSummoned
        );
    }
    return false;
}

bool RequestCheckRaidWorld(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0054387c.
    L2_GUARD_FUNCTION();

    int worldId = 0;
    ::Disassemble(packet, "d", &worldId);
    socket->Send("chdd", 0xc6, 0x7e, worldId, CSqlPoolManager::Instance().HasDatabaseId(worldId));
    return false;
}

bool RequestLoadAlliance(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0058f278.
    L2_GUARD_FUNCTION();

    int allianceId = 0;
    ::Disassemble(packet, "d", &allianceId);
    CAllianceSP alliance = g_allianceDb.GetAlliance(
        static_cast<std::uint32_t>(allianceId), static_cast<std::uint32_t>(runtime::process::CurrentDatabaseId())
    );
    if (!alliance)
        return false;

    PledgeIdList memberPledgeIds;
    alliance->GetAllMemberPledgeId(memberPledgeIds);
    std::array<char, 0x5000> members;
    int memberBytes = 0;
    int memberCount = 0;
    for (const std::uint32_t memberPledgeId : memberPledgeIds) {
        memberBytes +=
            ::Assemble(members.data() + memberBytes, static_cast<int>(members.size()) - memberBytes, "d", memberPledgeId);
        ++memberCount;
    }

    if (memberCount == 0) {
        socket->Send(
            "chdSddddd", 0xc6, 0x7f, alliance->id(), alliance->name(), alliance->masterPledgeId(), alliance->oustTime(),
            alliance->crestId(), alliance->worldId(), 0
        );
    } else {
        socket->Send(
            "chdSdddddb", 0xc6, 0x7f, alliance->id(), alliance->name(), alliance->masterPledgeId(), alliance->oustTime(),
            alliance->crestId(), alliance->worldId(), memberCount, memberBytes, members.data()
        );
    }
    return false;
}

bool RequestSetQuestNPCLog(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0053c970 delegates the nested record stream to
    // the shipping CUser owner after decoding only the character ID.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    const unsigned char* questLogPacket = ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "ServerPacketEx.cpp", 0x28d5, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "ServerPacketEx.cpp", 0x28d5, characterId)
        L2_LOG_ERROR(Log,
            "[quests] Cannot set NPC quest log for character %d: character unavailable. Update skipped.", characterId
        );
        return false;
    }
    user->SetQuestNPCLog(questLogPacket, characterId);
    return false;
}

bool RequestLoadInZoneFlag(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00556aec.
    L2_GUARD_FUNCTION();

    int inzoneUid = 0;
    int inzoneTypeId = 0;
    ::Disassemble(packet, "dd", &inzoneUid, &inzoneTypeId);

    int flagNo = 0;
    int flagValue = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&flagNo);
    connection.Bind(&flagValue);
    constexpr const wchar_t* statement = L"{call lin_GetInZoneFlag (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&inzoneUid);
    connection.BindParameter(&inzoneTypeId);
    if (!connection.ExecutePrepared(statement))
        return false;

    std::array<char, 0x5000> rows{};
    int rowBytes = 0;
    int rowCount = 0;
    while (connection.Fetch()) {
        rowBytes += ::Assemble(rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dd", flagNo, flagValue);
        ++rowCount;
    }
    socket->Send("chdddb", 0xc6, 0x82, inzoneUid, inzoneTypeId, rowCount, rowBytes, rows.data());
    return false;
}

bool RequestCreateInstantZone(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00556d10 decodes four dwords but deliberately
    // discards the first one before binding the remaining three parameters.
    L2_GUARD_FUNCTION();

    int unusedRequestValue = 0;
    int inzoneTypeId = 0;
    int inzoneDbId = 0;
    int createDate = 0;
    ::Disassemble(packet, "dddd", &unusedRequestValue, &inzoneTypeId, &inzoneDbId, &createDate);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{call lin_CreateInstantZone (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&inzoneTypeId);
    connection.BindParameter(&inzoneDbId);
    connection.BindParameter(&createDate);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "[%s][%d] Fail to save inzone db", "ServerPacketEx.cpp", 0x1229)
        // L2_LOG_ERROR(Log, "[%s][%d] Fail to save inzone db", "ServerPacketEx.cpp", 0x1229)
        L2_LOG_ERROR(Log,
            "[instances] Database request to create instance %d of type %d failed; creation date value %d.",
            inzoneDbId, inzoneTypeId, createDate
        );
    }
    return false;
}

bool RequestSaveInstantZoneFlag(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00556ec8.
    L2_GUARD_FUNCTION();

    int inzoneUid = 0;
    int flagNo = 0;
    int flagValue = 0;
    ::Disassemble(packet, "ddd", &inzoneUid, &flagNo, &flagValue);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{call lin_SaveInZoneFlag (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&inzoneUid);
    connection.BindParameter(&flagNo);
    connection.BindParameter(&flagValue);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] Fail to save inzone flag [%d]", "ServerPacketEx.cpp", 0x123e, inzoneUid
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] Fail to save inzone flag [%d]", "ServerPacketEx.cpp", 0x123e, inzoneUid
        // )
        L2_LOG_ERROR(Log,
            "[instances] Database request to save flag %d with value %d for instance UID %d failed.",
            flagNo, flagValue, inzoneUid
        );
    }
    return false;
}

bool RequestLoadInzoneMaxDBID(CSocket* socket, const unsigned char* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0052b37c does not check the payload.
    L2_GUARD_FUNCTION();

    int maximumDbId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&maximumDbId);
    constexpr const wchar_t* statement = L"{call lin_LoadInzoneMaxDBID }";
    connection.Prepare(statement);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        socket->Send("chd", 0xc6, 0x84, maximumDbId);
    }
    return false;
}

bool RequestSaveVoteSystem(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00534ee8.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int bonusTimeLimit = 0;
    int playTime = 0;
    int voteCount = 0;
    int bonusCount = 0;
    int playTimeCount = 0;
    SYSTEMTIME lastInitTime{};
    ::Disassemble(
        packet, "ddddddb", &characterId, &bonusTimeLimit, &playTime, &voteCount, &bonusCount, &playTimeCount, 0x10,
        &lastInitTime
    );

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, [%d]", "ServerPacketEx.cpp", 0x68c, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, [%d]", "ServerPacketEx.cpp", 0x68c, characterId)
        L2_LOG_ERROR(Log,
            "[voting] Cannot save voting state for character %d: character unavailable. Save skipped.", characterId
        );
        return false;
    }
    user->SaveVoteSystem(bonusTimeLimit, playTime, voteCount, bonusCount, playTimeCount, &lastInitTime);
    return false;
}

bool RequestClearEventTime(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00534d98.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        CAccountSP account = CAccount::Load(user->GetAccountID());
        if (account)
            account->ClearEventTime();
    }
    return false;
}

bool RequestAddPostFriend(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0056e308.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    std::array<char16_t, 25> friendNamePacket{};
    ::Disassemble(packet, "dS", &characterId, 0x32, friendNamePacket.data());
    const std::wstring friendName = Utf16ToWide(friendNamePacket.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    CUserSP friendUser = g_userDb.GetUser(friendNamePacket.data(), true);
    int result = -2;
    if (user && friendUser) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_CreatePostAdditionalFriend (?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.BindParameter(friendName.c_str());
        if (connection.ExecutePrepared(statement)) {
            result = 1;
        } else {
            // Log.Add(
            //     LOG_ERROR, "[AddPostFriend] procedure failed! [%d] [%ls]", characterId, friendName.c_str()
            // )
            // L2_LOG_ERROR(Log, "[AddPostFriend] procedure failed! [%d] [%ls]", characterId, friendName.c_str()
            // )
            L2_LOG_ERROR(Log,
                "[mail] Database request to add postal friend '%s' for character %d failed. Sending result -2.",
                WideToUtf8(friendName).c_str(), characterId
            );
        }
    }

    socket->Send("chdSd", 0xc6, 0x85, characterId, friendNamePacket.data(), result);
    return false;
}

bool RequestLoadPostFriend(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0056e804.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[LoadPostFriend] User invalid [%d]", characterId)
        // L2_LOG_ERROR(Log, "[LoadPostFriend] User invalid [%d]", characterId)
        L2_LOG_ERROR(Log,
            "[mail] Cannot load postal friends for character %d: character unavailable. Sending an empty list.", characterId
        );
        socket->Send("chdd", 0xc6, 0x86, characterId, 0);
        return false;
    }

    std::array<char16_t, 25> friendName{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(friendName.data(), 0x32);
    constexpr const wchar_t* statement = L"{Call lin_LoadPostAdditionalFriend (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(statement)) {
        socket->Send("chdd", 0xc6, 0x86, characterId, 0);
        return false;
    }

    std::array<char, 0x5000> rows{};
    int rowBytes = 0;
    int rowCount = 0;
    while (connection.Fetch()) {
        rowBytes += ::Assemble(rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "S", friendName.data());
        ++rowCount;
        if (rowBytes >= 0x4c18) {
            // Log.Add(LOG_ERROR, "[LoadPostFriend] Too much friends! char %d", characterId)
            L2_LOG_ERROR(Log, "[LoadPostFriend] Too much friends! char %d", characterId);
            break;
        }
    }
    socket->Send("chddb", 0xc6, 0x86, characterId, rowCount, rowBytes, rows.data());
    return false;
}

bool RequestDeletePostFriend(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0056e608.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    std::array<char16_t, 25> friendNamePacket{};
    ::Disassemble(packet, "dS", &characterId, 0x32, friendNamePacket.data());
    const std::u16string friendName(friendNamePacket.data());
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[DeletePostFriend] User invalid [%d] [%ls]", characterId, friendName.c_str())
        // L2_LOG_ERROR(Log, "[DeletePostFriend] User invalid [%d] [%ls]", characterId, friendName.c_str())
        // L2_LOG_ERROR(Log,
        //     "[mail] Cannot remove postal friend '%s' for character %d: character unavailable. Deletion skipped.",
        //     WideToUtf8(friendName).c_str(), characterId
        // )
        L2_LOG_ERROR(Log,
            "[mail] Cannot remove postal friend '%s' for character %d: character unavailable. Deletion skipped.",
            Utf16ToUtf8(friendName).c_str(), characterId
        );
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeletePostAdditionalFriend (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(friendName.c_str());
    connection.ExecutePrepared(statement);
    return false;
}

bool LoadOlympiadMatchCount(CSocket* socket, const unsigned char* packet [[maybe_unused]]) {
    // Current Cached.exe 0x00532bcc does not check the request payload.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int season = 0;
    int step = 0;
    int gameRule = 0;
    int matchCount = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&characterId);
    connection.Bind(&season);
    connection.Bind(&step);
    connection.Bind(&gameRule);
    connection.Bind(&matchCount);
    constexpr const wchar_t* statement = L"{Call lin_LoadOlympiadMatchCount}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement))
        return false;

    bool fullPage = false;
    do {
        fullPage = false;
        std::array<char, 0x2004> rows{};
        int rowBytes = 0;
        int rowCount = 0;
        while (rowCount != 100) {
            if (!connection.Fetch())
                break;
            rowBytes += ::Assemble(
                rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "ddddd", characterId, season, step,
                gameRule, matchCount
            );
            ++rowCount;
        }
        if (rowCount == 100)
            fullPage = true;
        socket->Send("chdb", 0xc6, 0x87, rowCount, rowBytes, rows.data());
    } while (fullPage);
    return false;
}

bool SaveOlympiadMatchCount(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0054f804.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int season = 0;
    int step = 0;
    int gameRule = 0;
    int matchCount = 0;
    ::Disassemble(packet, "ddddd", &characterId, &season, &step, &gameRule, &matchCount);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveOlympiadMatchCount (?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&season);
    connection.BindParameter(&step);
    connection.BindParameter(&gameRule);
    connection.BindParameter(&matchCount);
    connection.ExecutePrepared(statement);
    return false;
}

bool GetOlympiadSeasonPlayerCount(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0054f664.
    L2_GUARD_FUNCTION();

    int season = 0;
    int playerCount = 0;
    ::Disassemble(packet, "d", &season);

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&playerCount);
    constexpr const wchar_t* statement = L"{Call lin_GetOlympiadPlayerCount (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&season);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        socket->Send("chd", 0xc6, 0x88, playerCount);
    }
    return false;
}

bool RequestBrGiveItem(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0055cff0. Paired current L2Server.exe fixes both
    // the dQddddddd request order and the chddQddddddb reply order.
    L2_GUARD_FUNCTION();

    int productType = 0;
    long long warehouseOrPaymentAmount = 0;
    int characterId = 0;
    int productId = 0;
    int productAmount = 0;
    int itemCount = 0;
    int saveProductSoldCount = 0;
    int maximumCount = 0;
    int limitDay = 0;
    const unsigned char* itemPacket = ::Disassemble(
        packet, "dQddddddd", &productType, &warehouseOrPaymentAmount, &characterId, &productId, &productAmount, &itemCount,
        &saveProductSoldCount, &maximumCount, &limitDay
    );

    auto sendReply = [&](int replyProductType, int paymentItemId, long long paymentOrWarehouse, int soldCount,
                         BrBuyProductResultType result, int createdItemCount, CPacketHelperEx<20480>& items) {
        socket->Send(
            "chddQddddddb", 0xc6, 0x89, replyProductType, paymentItemId, paymentOrWarehouse, characterId, productId,
            productAmount, soldCount, static_cast<int>(result), createdItemCount, items.GetLength(), items.GetPacket()
        );
    };

    DBConn connection(runtime::process::CurrentDatabaseId());
    CPacketHelperEx<20480> createdItems;

    if (productType == 0 || productType == 99) {
        int receivedItemId = -1;
        connection.Bind(&receivedItemId);
        constexpr const wchar_t* statement = L"{Call lin_LoadPremiumItemReceive (?)}";
        connection.Prepare(statement);
        connection.BindParameter(&warehouseOrPaymentAmount);
        const bool alreadyReceived = connection.ExecutePrepared(statement) && connection.Fetch() && receivedItemId >= 0;
        connection.UnBind();
        if (alreadyReceived) {
            sendReply(productType, 0, warehouseOrPaymentAmount, 0, BR_BUY_INVALID_ITEM, 0, createdItems);
            return false;
        }
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d] GetUser Fail charId(%d)", "ServerPacketEx.cpp", 0x1b6c, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d] GetUser Fail charId(%d)", "ServerPacketEx.cpp", 0x1b6c, characterId)
        L2_LOG_ERROR(Log,
            "[shop] Cannot deliver product %d (amount %d) to character %d: character unavailable. No reply will be sent.",
            productId, productAmount, characterId
        );
        return false;
    }

    CWareHouseSP warehouse = user->GetWareHouse(0, true);
    if (!warehouse) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] Warehouse is invalid. charId(%d)", "ServerPacketEx.cpp", 0x1b82, characterId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] Warehouse is invalid. charId(%d)", "ServerPacketEx.cpp", 0x1b82, characterId
        // )
        L2_LOG_ERROR(Log,
            "[shop] Cannot deliver product %d (amount %d) to character %d: inventory unavailable. No reply will be sent.",
            productId, productAmount, characterId
        );
        return false;
    }

    int limitedBuyCount = 0;
    int limitedBuyTime = 0;
    bool resetLimitedPeriod = false;
    if (maximumCount >= 1) {
        connection.Bind(&limitedBuyCount);
        connection.Bind(&limitedBuyTime);
        const int accountId = user->GetAccountID();
        constexpr const wchar_t* statement = L"{Call lin_BrGetLimitedShopItemInfo (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&accountId);
        connection.BindParameter(&characterId);
        connection.BindParameter(&productId);
        if (connection.ExecutePrepared(statement)) {
            connection.Fetch();
        }
        resetLimitedPeriod = limitedBuyTime < 0;

        if (limitDay > 0 && limitedBuyTime > 0) {
            const std::time_t now = std::time(nullptr);
            std::tm boundaryLocal{};
            if (BrLocalTime32(limitedBuyTime, boundaryLocal)) {
                boundaryLocal.tm_mday += limitDay;
                boundaryLocal.tm_hour = 0;
                boundaryLocal.tm_min = 0;
                boundaryLocal.tm_sec = 0;
                const std::time_t boundary = std::mktime(&boundaryLocal);
                if (now != boundary && now - boundary >= 0) {
                    limitedBuyCount = 0;
                    resetLimitedPeriod = true;
                }
            }
        }

        if (runtime::AddInt32Wrapping(limitedBuyCount, productAmount) > maximumCount) {
            connection.UnBind();
            sendReply(productType, 0, warehouseOrPaymentAmount, 0, BR_BUY_NUMBER_LIMIT, 0, createdItems);
            return false;
        }
        connection.UnBind();
    }

    BrBuyProductResultType result = BR_BUY_SUCCESS;
    int createdItemCount = 0;
    int paymentItemId = 0;
    int remainingPaymentAmount = 0;
    if (productType == 1 || productType == 2) {
        {
            L2_WRITE_GUARD(*warehouse.operator->());
            result = warehouse->GetPaymentItems(productType, warehouseOrPaymentAmount, &paymentItemId, &remainingPaymentAmount);
        }
        warehouseOrPaymentAmount = 0;
    }

    const bool paymentAccepted = result == BR_BUY_SUCCESS;
    if (paymentAccepted) {
        const unsigned char* cursor = itemPacket;
        for (int index = 0; index < itemCount; ++index) {
            int itemType = 0;
            int itemAmountPerProduct = 0;
            cursor = ::Disassemble(cursor, "dd", &itemType, &itemAmountPerProduct);
            const int totalItemAmount = runtime::MultiplyInt32Wrapping(itemAmountPerProduct, productAmount);
            {
                L2_WRITE_GUARD(*warehouse.operator->());
                result = warehouse->GiveItems(
                    itemType, productAmount, totalItemAmount, warehouseOrPaymentAmount, productId, &createdItemCount,
                    createdItems
                );
            }
        }

        int productSoldCount = 0;
        if (saveProductSoldCount > 0) {
            connection.Bind(&productSoldCount);
            constexpr const wchar_t* statement = L"{Call lin_BrSaveProductSoldCount (?, ?)}";
            connection.Prepare(statement);
            connection.BindParameter(&productId);
            connection.BindParameter(&productAmount);
            if (connection.ExecutePrepared(statement)) {
                connection.Fetch();
            }
            connection.UnBind();
        }

        if (maximumCount > 0) {
            const int accountId = user->GetAccountID();
            const int buyTime = resetLimitedPeriod ? runtime::crt::Time32Now() : 0;
            constexpr const wchar_t* statement = L"{Call lin_BrSetLimitedShopItemInfo (?, ?, ?, ?, ?, ?, ?)}";
            connection.Prepare(statement);
            connection.BindParameter(&accountId);
            connection.BindParameter(&characterId);
            connection.BindParameter(&productId);
            connection.BindParameter(&productAmount);
            connection.BindParameter(&buyTime);
            connection.BindParameter(&maximumCount);
            connection.BindParameter(&limitDay);
            connection.ExecutePrepared(statement);
        }

        int replyProductType = productType;
        int replyPaymentItemId = paymentItemId;
        long long replyPaymentOrWarehouse = remainingPaymentAmount;
        if (productType == 0 || productType == 99 || productType == 98) {
            if (productType == 98)
                replyProductType = 0;
            replyPaymentItemId = 0;
            replyPaymentOrWarehouse = warehouseOrPaymentAmount;
        }
        sendReply(
            replyProductType, replyPaymentItemId, replyPaymentOrWarehouse, productSoldCount, result, createdItemCount,
            createdItems
        );
        return false;
    }

    int replyProductType = productType;
    int replyPaymentItemId = paymentItemId;
    long long replyPaymentOrWarehouse = remainingPaymentAmount;
    if (productType == 0 || productType == 99 || productType == 98) {
        if (productType == 98)
            replyProductType = 0;
        replyPaymentItemId = 0;
        replyPaymentOrWarehouse = warehouseOrPaymentAmount;
    }
    sendReply(replyProductType, replyPaymentItemId, replyPaymentOrWarehouse, 0, result, createdItemCount, createdItems);
    return false;
}

bool RequestBrPlayingUser(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0055daf0.
    L2_GUARD_FUNCTION();

    int playingUser = 0;
    int result = 0;
    ::Disassemble(packet, "d", &playingUser);
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    constexpr const wchar_t* statement = L"{Call lin_BrUpdatePlayingUser (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&playingUser);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        socket->Send("chd", 0xc6, 0x8a, result);
    }
    return false;
}

bool RequestBrProductSoldCount(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0055dc80.
    L2_GUARD_FUNCTION();

    int productCount = 0;
    std::array<int, 1000> productIds{};
    std::array<int, 1000> soldCounts{};
    ::Disassemble(packet, "db", &productCount, 4000, productIds.data());
    if (static_cast<unsigned int>(productCount - 1) >= 1000U) {
        // Log.Add(LOG_ERROR, "GetProductStock Fail, invalid nCount = %d", productCount)
        L2_LOG_ERROR(Log,
            "[product stock] Cannot load sold counts: requested product count %d is outside 1..1000. No reply sent.",
            productCount);
        return false;
    }

    for (int index = 0; index < productCount; ++index) {
        int soldCount = 0;
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Bind(&soldCount);
        constexpr const wchar_t* statement = L"{Call lin_BrLoadProductSoldCount (?)}";
        connection.Prepare(statement);
        connection.BindParameter(&productIds[static_cast<std::size_t>(index)]);
        if (!connection.ExecutePrepared(statement))
            continue;
        if (!connection.Fetch()) {
            // Log.Add(
            //     LOG_ERROR, "GetProductStock Fail, productId = %d", productIds[static_cast<std::size_t>(index)]
            // )
            L2_LOG_ERROR(Log,
                "[product stock] Cannot fetch sold count for product %d at entry %d of %d. "
                "Batch processing stopped; no reply sent.",
                productIds[static_cast<std::size_t>(index)], index + 1, productCount);
            return false;
        }
        soldCounts[static_cast<std::size_t>(index)] = soldCount;
    }

    const int byteCount = productCount * 4;
    // Cached.exe 0x0055de7f..0x0055decf places the post-format
    // arguments on the Windows x64 stack as
    //   productCount, 4, byteCount, productIds, 4, byteCount, soldCounts.
    // Each `b` therefore has its own non-wire length argument. The two
    // intervening `d` fields carry the four-byte element widths expected by
    // the current L2Server decoder.
    socket->Send("chddbdb", 0xc6, 0x8b, productCount, 4, byteCount, productIds.data(), 4, byteCount, soldCounts.data());
    return false;
}

bool RequestBrRecentProduct(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0055df24.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int requestedCount = 0;
    ::Disassemble(packet, "dd", &characterId, &requestedCount);
    if (static_cast<unsigned int>(requestedCount - 1) >= 30U) {
        // Log.Add(LOG_ERROR, "RequestBrRecentProduct, Invalid nCount (%d)", requestedCount)
        // L2_LOG_ERROR(Log, "RequestBrRecentProduct, Invalid nCount (%d)", requestedCount)
        L2_LOG_WARN(Log,
            "[shop] Rejected recent-product request for character %d: count %d is outside 1..30. No reply will be sent.",
            characterId, requestedCount
        );
        return false;
    }

    int productId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&productId);
    constexpr const wchar_t* statement = L"{Call lin_BrLoadRecentProduct (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&requestedCount);
    if (!connection.ExecutePrepared(statement))
        return false;

    std::array<char, 120> rows{};
    int rowBytes = 0;
    int rowCount = 0;
    while (connection.Fetch()) {
        rowBytes += ::Assemble(rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "d", productId);
        ++rowCount;
    }
    if (rowCount > 0) {
        socket->Send("chddb", 0xc6, 0x8c, characterId, rowCount, rowBytes, rows.data());
    }
    return false;
}

bool RequestBrInsertEventRanking(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0055e160. Paired current L2Server.exe
    // CDB::RequestInsertEventRanking fixes event/round/name/score order.
    L2_GUARD_FUNCTION();

    int eventId = 0;
    int roundNo = 0;
    int score = 0;
    std::array<char16_t, 25> characterNamePacket{};
    ::Disassemble(packet, "ddSd", &eventId, &roundNo, 0x32, characterNamePacket.data(), &score);
    const std::wstring characterName = Utf16ToWide(characterNamePacket.data());
    CUserSP user = g_userDb.GetUser(characterNamePacket.data(), false);
    if (!user)
        return false;

    const int characterId = static_cast<int>(user->GetId());
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_BrInsertEventRanking (?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&eventId);
    connection.BindParameter(&roundNo);
    connection.BindParameter(&characterId);
    connection.BindParameter(&score);
    connection.BindParameter(user->GetCharName());
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "RequestBrInsertEventRanking, fail to "
        //     "qsBrInsertEventRanking, char = %ls",
        //     characterName.c_str()
        // )
        // Previous: L2_LOG_ERROR(Log,
        //     "RequestBrInsertEventRanking, fail to "
        //     "qsBrInsertEventRanking, char = %ls",
        //     characterName.c_str()
        // )
        L2_LOG_ERROR(Log,
            "[event ranking] Cannot insert score %d for character %d (%ls), event %d, round %d: database request failed.",
            score, characterId, characterName.c_str(), eventId, roundNo);
    }
    return false;
}

bool RequestBrLoadEventTopRankers(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0055e358. Paired current L2Server reply consumer
    // proves the wire order charId,eventId,roundNo. SQL receives
    // eventId,roundNo,charId.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int eventId = 0;
    int roundNo = 0;
    ::Disassemble(packet, "ddd", &characterId, &eventId, &roundNo);

    int topPlayerCount = -1;
    int topPlayerScore = -1;
    int myScore = -1;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&topPlayerCount);
    connection.Bind(&topPlayerScore);
    connection.Bind(&myScore);
    constexpr const wchar_t* statement = L"{Call lin_BrLoadEventTopRankers (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&eventId);
    connection.BindParameter(&roundNo);
    connection.BindParameter(&characterId);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }
    socket->Send("chdddddd", 0xc6, 0x8d, characterId, eventId, roundNo, topPlayerCount, topPlayerScore, myScore);
    return false;
}

bool RequestBrUpdateEventRanking(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0055e568.
    L2_GUARD_FUNCTION();

    int eventId = 0;
    int roundNo = 0;
    ::Disassemble(packet, "dd", &eventId, &roundNo);

    std::array<char16_t, 25> characterName{};
    std::int64_t nullableIndicator = -1;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.BindNullable(characterName.data(), 0x32, &nullableIndicator);
    constexpr const wchar_t* statement = L"{Call lin_BrUpdateEventRanking (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&eventId);
    connection.BindParameter(&roundNo);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "[%s][%d] UpdateEventRanking Fail", "ServerPacketEx.cpp", 0x1ca9)
        // L2_LOG_ERROR(Log, "[%s][%d] UpdateEventRanking Fail", "ServerPacketEx.cpp", 0x1ca9)
        // Previous: L2_LOG_ERROR(Log, "Cannot update event ranking: database request failed for event %d, round %d, no reply sent", eventId, roundNo)
        L2_LOG_ERROR(Log, "[event ranking] Cannot update event %d, round %d: the database request failed; no reply sent.", eventId, roundNo);
        return false;
    }

    std::array<char, 0x5000> rows{};
    int rowBytes = 0;
    int rowCount = 0;
    while (connection.Fetch()) {
        rowBytes += ::Assemble(rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "S", characterName.data());
        ++rowCount;
        if (rowBytes > 0x1f9f) {
            socket->Send("chdddb", 0xc6, 0x8e, eventId, roundNo, rowCount, rowBytes, rows.data());
            rows.fill(0);
            rowBytes = 0;
            rowCount = 0;
        }
    }
    socket->Send("chdddb", 0xc6, 0x8e, eventId, roundNo, rowCount, rowBytes, rows.data());
    return false;
}

bool RequestGetEventScore(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0055e850. The optional name changes only the SQL
    // character ID. The paired current reply still echoes the request ID and
    // the original UTF-16 name.
    L2_GUARD_FUNCTION();

    int eventId = 0;
    int roundNo = 0;
    int requestCharacterId = 0;
    std::array<char16_t, 25> characterNamePacket{};
    ::Disassemble(packet, "dddS", &eventId, &roundNo, &requestCharacterId, 0x32, characterNamePacket.data());

    int databaseCharacterId = requestCharacterId;
    const std::wstring characterName = Utf16ToWide(characterNamePacket.data());
    if (!characterName.empty()) {
        CUserSP user = g_userDb.GetUser(characterNamePacket.data(), false);
        if (!user)
            return false;
        databaseCharacterId = static_cast<int>(user->GetId());
    }

    int score = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&score);
    constexpr const wchar_t* statement = L"{Call lin_BrGetEventScore (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&eventId);
    connection.BindParameter(&roundNo);
    connection.BindParameter(&databaseCharacterId);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        socket->Send("chdddSd", 0xc6, 0x8f, eventId, roundNo, requestCharacterId, characterNamePacket.data(), score);
    }
    return false;
}

bool RequestSetEventScore(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0055ebbc.
    L2_GUARD_FUNCTION();

    int eventId = 0;
    int roundNo = 0;
    int characterId = 0;
    int score = 0;
    ::Disassemble(packet, "dddd", &eventId, &roundNo, &characterId, &score);

    CUserSP user;
    if (characterId != 0) {
        user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
        if (!user)
            return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    // These four result bindings are present in the current image even though
    // the procedure has no result row and the handler never calls Fetch.
    connection.Bind(&eventId);
    connection.Bind(&roundNo);
    connection.Bind(&characterId);
    connection.Bind(&score);
    constexpr const wchar_t* statement = L"{Call lin_BrSetEventScore (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&eventId);
    connection.BindParameter(&roundNo);
    connection.BindParameter(&characterId);
    connection.BindParameter(&score);
    if (connection.ExecutePrepared(statement)) {
        socket->Send("chdddd", 0xc6, 0x90, eventId, roundNo, characterId, score);
    } else if (characterId != 0) {
        // Log.Add(LOG_ERROR, "RequestSetEventScore, fail to qsBrSetEventScore")
        // L2_LOG_ERROR(Log, "RequestSetEventScore, fail to qsBrSetEventScore")
        L2_LOG_ERROR(Log,
            "[events] Database request to save score %d for character %d, event %d, round %d failed. "
            "No reply will be sent.", score, characterId, eventId, roundNo
        );
    }
    return false;
}

bool RequestLoadMinigameScores(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0055ef10.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    CPacketHelperEx<20480> scores;
    DBConn connection(runtime::process::CurrentDatabaseId());

    int myRank = 0;
    int myScore = 0;
    connection.Bind(&myRank);
    connection.Bind(&myScore);
    constexpr const wchar_t* myScoreStatement = L"{Call lin_BrLoadMinigameMyScore (?)}";
    connection.Prepare(myScoreStatement);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(myScoreStatement) || !connection.Fetch()) {
        // Log.Add(
        //     LOG_ERROR,
        //     "RequestLoadMinigameScores, fail to "
        //     "qsBrLoadMinigameMyScore"
        // )
        // L2_LOG_ERROR(Log,
        //     "RequestLoadMinigameScores, fail to "
        //     "qsBrLoadMinigameMyScore"
        // )
        L2_LOG_ERROR(Log,
            "[minigames] Personal-score query failed or returned no row for character %d. No score reply will be sent.",
            characterId
        );
        return false;
    }
    scores.Assemble("ddd", characterId, myRank, myScore);

    connection.UnBind();
    int bottomRank = 0;
    int bottomScore = 0;
    connection.Bind(&bottomRank);
    connection.Bind(&bottomScore);
    constexpr const wchar_t* bottomRankerStatement = L"{Call lin_BrLoadMinigameBottomRanker}";
    connection.Prepare(bottomRankerStatement);
    // The shipping image attempts to bind characterId after preparing this
    // zero-marker call. ODBC rejects that number and the ignored bind has no
    // observable SQL input. Omitting the rejected transport operation keeps
    // the actual call and result behavior on PostgreSQL.
    if (!connection.ExecutePrepared(bottomRankerStatement) || !connection.Fetch()) {
        // Log.Add(
        //     LOG_ERROR,
        //     "RequestLoadMinigameScores, fail to "
        //     "qsBrLoadMinigameMyScore"
        // )
        // L2_LOG_ERROR(Log,
        //     "RequestLoadMinigameScores, fail to "
        //     "qsBrLoadMinigameMyScore"
        // )
        L2_LOG_ERROR(Log,
            "[minigames] Bottom-ranker query failed or returned no row while loading scores for character %d. "
            "No score reply will be sent.", characterId
        );
        return false;
    }
    scores.Assemble("dd", bottomRank, bottomScore);

    connection.UnBind();
    int topRank = 0;
    std::array<char16_t, 25> topCharacterName{};
    int topScore = 0;
    connection.Bind(&topRank);
    connection.Bind(topCharacterName.data(), 0x32);
    connection.Bind(&topScore);
    constexpr const wchar_t* topRankersStatement = L"{Call lin_BrLoadMinigameTopRankers}";
    connection.Prepare(topRankersStatement);
    if (!connection.ExecutePrepared(topRankersStatement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "RequestLoadMinigameScores, fail to "
        //     "qsBrLoadMinigameTopRankers"
        // )
        // L2_LOG_ERROR(Log,
        //     "RequestLoadMinigameScores, fail to "
        //     "qsBrLoadMinigameTopRankers"
        // )
        L2_LOG_ERROR(Log,
            "[minigames] Top-rankers query failed while loading scores for character %d. No score reply will be sent.",
            characterId
        );
        return false;
    }
    while (connection.Fetch()) {
        scores.Assemble("dSd", topRank, topCharacterName.data(), topScore);
    }
    scores.Assemble("d", -1);
    connection.UnBind();
    socket->Send("chb", 0xc6, 0x91, scores.GetLength(), scores.GetPacket());
    return false;
}

bool RequestInsertMinigameScore(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0055f3e0.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 25> characterNamePacket{};
    int score = 0;
    ::Disassemble(packet, "Sd", 0x32, characterNamePacket.data(), &score);
    const std::wstring characterName = Utf16ToWide(characterNamePacket.data());
    CUserSP user = g_userDb.GetUser(characterNamePacket.data(), false);
    if (!user) {
        socket->Send("chdd", 0xc6, 0x92, 0, 0);
        return false;
    }

    const int characterId = static_cast<int>(user->GetId());
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_BrInsertMinigameScore (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(characterName.c_str());
    connection.BindParameter(&score);
    const int result = connection.ExecutePrepared(statement) ? 1 : 0;
    socket->Send("chdd", 0xc6, 0x92, characterId, result);
    return false;
}

bool RequestGetLectureMark(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0055f654.
    L2_GUARD_FUNCTION();

    int accountId = 0;
    ::Disassemble(packet, "d", &accountId);
    CUserSP user = g_userDb.GetUserByAccountId(accountId);
    if (!user) {
        socket->Send("chdcc", 0xc6, 0x93, accountId, 0, 0);
        return false;
    }

    int lectureMark = 0;
    int resultMessage = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&lectureMark);
    connection.Bind(&resultMessage);
    constexpr const wchar_t* statement = L"{Call lin_BrGetLectureMark (?) }";
    connection.Prepare(statement);
    connection.BindParameter(&accountId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "RequestGetLectureMark Fail, accountId = %d", accountId)
        L2_LOG_ERROR(Log,
            "[lecture] Cannot load lecture mark for account %d: database execution failed. No reply sent.", accountId);
        return false;
    }
    if (!connection.Fetch() || lectureMark == -1) {
        // Log.Add(
        //     LOG_ERROR,
        //     "RequestGetLectureMark Fail, accountId = %d, "
        //     "nLectureMark = %d",
        //     accountId, lectureMark
        // )
        L2_LOG_ERROR(Log,
            "[lecture] Cannot obtain lecture mark for account %d: fetch failed or returned mark -1 "
            "(mark buffer %d). No reply sent.",
            accountId, lectureMark);
        return false;
    }

    socket->Send(
        "chdcc", 0xc6, 0x93, accountId, static_cast<int>(static_cast<signed char>(lectureMark)),
        static_cast<int>(static_cast<signed char>(resultMessage))
    );
    return false;
}

bool RequestSetLectureMark(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0055f8e4.
    L2_GUARD_FUNCTION();

    int accountId = 0;
    signed char requestedLectureMark = 0;
    ::Disassemble(packet, "dc", &accountId, &requestedLectureMark);
    CUserSP user = g_userDb.GetUserByAccountId(accountId);
    if (!user) {
        socket->Send("chdc", 0xc6, 0x94, accountId, 0);
        return false;
    }

    int result = 0;
    std::int64_t nullableIndicator = -1;
    const int lectureMark = static_cast<int>(requestedLectureMark);
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.BindNullable(&result, &nullableIndicator);
    constexpr const wchar_t* statement = L"{Call lin_BrSetLectureMark (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&accountId);
    connection.BindParameter(&lectureMark);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch() || nullableIndicator < 0) {
        result = 0;
    }
    socket->Send("chdc", 0xc6, 0x94, accountId, static_cast<int>(static_cast<signed char>(result)));
    return false;
}

bool RequestSetCreateDate(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0055fb40. The second reply dword is the requested
    // year on SQL success and zero on failure, not a separate result flag.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    ::Disassemble(packet, "dddddd", &characterId, &year, &month, &day, &hour, &minute);

    int successfulYear = 0;
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_BrSetCreateDate (?, ?, ?, ?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.BindParameter(&year);
        connection.BindParameter(&month);
        connection.BindParameter(&day);
        connection.BindParameter(&hour);
        connection.BindParameter(&minute);
        if (connection.ExecutePrepared(statement)) {
            successfulYear = year;
        }
    }
    socket->Send("chdddddd", 0xc6, 0x95, characterId, successfulYear, month, day, hour, minute);
    return false;
}

bool RequestLoadDropItemAmountPerServer(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0055fdd8.
    L2_GUARD_FUNCTION();

    int requestedCount = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &requestedCount);
    CPacketHelperEx<20480> rows;
    int rowCount = 0;

    int itemAmount = -1;
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    std::int64_t nullableIndicator = -1;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.BindNullable(&itemAmount, &nullableIndicator);
    connection.Bind(&year);
    connection.Bind(&month);
    connection.Bind(&day);
    connection.Bind(&hour);
    connection.Bind(&minute);
    connection.Bind(&second);

    int itemClassId = 0;
    bool inputBound = false;
    constexpr const wchar_t* statement = L"{Call lin_BrLoadDropItemAmountPerServer (?) }";
    for (int index = 0; index < requestedCount; ++index) {
        cursor = ::Disassemble(cursor, "d", &itemClassId);
        connection.Prepare(statement);
        if (!inputBound) {
            // SQLBindParameter keeps the pointer across the target's later
            // SQLPrepare calls. Later number-2+ bind attempts fail and are
            // ignored. Updating this local therefore supplies every row ID.
            connection.BindParameter(&itemClassId);
            inputBound = true;
        }
        if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
            continue;
        }

        std::tm localTime{};
        localTime.tm_year = year - 1900;
        localTime.tm_mon = month - 1;
        localTime.tm_mday = day;
        localTime.tm_hour = hour;
        localTime.tm_min = minute;
        localTime.tm_sec = second;
        localTime.tm_isdst = 0;
        const std::int32_t dropTime = runtime::crt::Mktime32(&localTime);
        rows.Assemble("ddd", itemClassId, itemAmount, dropTime);
        ++rowCount;
    }

    socket->Send("chdb", 0xc6, 0x96, rowCount, rows.GetLength(), rows.GetPacket());
    return false;
}

bool RequestSaveDropItemAmountPerServer(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005600a0.
    L2_GUARD_FUNCTION();

    int itemClassId = 0;
    int amount = 0;
    ::Disassemble(packet, "dd", &itemClassId, &amount);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_BrSaveDropItemAmountPerServer (?, ?) }";
    connection.Prepare(statement);
    connection.BindParameter(&itemClassId);
    connection.BindParameter(&amount);
    if (connection.ExecutePrepared(statement)) {
        socket->Send("chdd", 0xc6, 0x97, itemClassId, amount);
    }
    return false;
}

bool RequestLoadItemAgathionEnergy(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00560234. Paired current L2Server fixes the three
    // IDs as user-server, item-server and item-database identifiers.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    int itemServerId = 0;
    int itemDatabaseId = 0;
    ::Disassemble(packet, "ddd", &userServerId, &itemServerId, &itemDatabaseId);

    double agathionEnergy = 0.0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&agathionEnergy);
    constexpr const wchar_t* statement = L"{Call lin_BrLoadItemAgathionEnergy (?) }";
    connection.Prepare(statement);
    connection.BindParameter(&itemDatabaseId);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
        agathionEnergy = 0.0;
    }
    socket->Send("chdddf", 0xc6, 0x98, userServerId, itemServerId, itemDatabaseId, agathionEnergy);
    return false;
}

bool RequestSaveItemAgathionEnergy(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005603f4.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    int itemDatabaseId = 0;
    double agathionEnergy = 0.0;
    ::Disassemble(packet, "ddf", &userServerId, &itemDatabaseId, &agathionEnergy);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_BrSaveItemAgathionEnergy (?, ?) }";
    connection.Prepare(statement);
    connection.BindParameter(&itemDatabaseId);
    connection.BindParameter(&agathionEnergy);
    if (connection.ExecutePrepared(statement)) {
        socket->Send("chddf", 0xc6, 0x99, userServerId, itemDatabaseId, agathionEnergy);
    }
    return false;
}

bool RequestResetEventData(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0053c3dc.
    L2_GUARD_FUNCTION();

    int eventId = 0;
    int characterId = 0;
    ::Disassemble(packet, "dd", &eventId, &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        user->SetEventData(eventId, 0);
    }
    return false;
}

bool RequestSetEventDataPeriod(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0052ba74.
    L2_GUARD_FUNCTION();

    int eventDataPeriod = 0;
    ::Disassemble(packet, "d", &eventDataPeriod);
    g_userDb.SetEventDataPeriod(eventDataPeriod);
    return false;
}

bool RequestUserRaceSex(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0056059c.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int race = 0;
    int gender = 0;
    ::Disassemble(packet, "ddd", &characterId, &race, &gender);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateUserRaceSex (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&race);
    connection.BindParameter(&gender);
    if (connection.ExecutePrepared(statement)) {
        CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
        if (user) {
            {
                L2_WRITE_GUARD(*user.operator->());
                user->m_race = race;
                user->m_gender = gender;
            }
        }
    }
    return false;
}

bool RequestDeleteSummon(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0056eb20.
    L2_GUARD_FUNCTION();

    int requestContext = 0;
    int characterId = 0;
    int summonDbId = 0;
    int atomicJobId = 0;
    ::Disassemble(packet, "dddd", &requestContext, &characterId, &summonDbId, &atomicJobId);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_DeleteUserSummon (?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.BindParameter(&summonDbId);
        connection.ExecutePrepared(statement);
    }

    socket->Send("chddddd", 0xc6, 0xe9, 1, requestContext, characterId, summonDbId, atomicJobId);
    return false;
}

bool RequestItemCheck(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0053cad4.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user && !user->ItemCheckBetweenMemoryAndDB()) {
        user->OnItemCheckFailed(2);
    }
    return false;
}

bool RequestDBSecuritySetting(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0052c310. The target writes the two plain process
    // globals directly. No configuration object is reloaded here.
    L2_GUARD_FUNCTION();

    std::uint32_t setting = 0;
    ::Disassemble(packet, "d", &setting);
    switch (setting) {
    case UINT32_MAX:
        g_bUseDBSecurity = false;
        break;
    case 0:
        g_bUseDBSecurity = true;
        break;
    case 1:
    case 2:
    case 3:
        g_nDBSecurityPenaltyLevel = static_cast<int>(setting);
        break;
    default:
        break;
    }
    return false;
}

bool RequestSendBirthDayMail(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0056ac2c. Paired current L2Server fixes the
    // chdSSddddd request and chdddddddddddb response contracts.
    L2_GUARD_FUNCTION();

    int receiverId = 0;
    std::array<char16_t, 61> titlePacket{};
    std::array<char16_t, 1001> contentsPacket{};
    int itemType = 0;
    int mailType = 0;
    int itemAmount = 0;
    int senderId = 0;
    int requestContext = 0;
    const unsigned char* cursor = ::Disassemble(
        packet, "dSSddddd", &receiverId, 0x7a, titlePacket.data(), 0x7d2, contentsPacket.data(), &itemType, &mailType,
        &itemAmount, &senderId, &requestContext
    );

    // int itemCount = mailType == 10 ? 0 : 1.
    int itemCount = mailType == Post::PledgeJoinReject ? 0 : 1;
    // if (mailType != 10)
    if (mailType != Post::PledgeJoinReject)
        senderId = receiverId;
    // int returnable = mailType == 9 ? 0 : 1.
    int returnable = mailType == Post::LevelUpReward ? 0 : 1;
    // if (mailType == 14)
    if (mailType == Post::MobileGift)
        returnable = 0;

    CUserSP user = g_userDb.GetUser(receiverId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    CPacketHelperEx<20480> responseItem;
    auto sendReply = [&](int result, int postId, int replyReturnable, int replyItemCount) {
        socket->Send(
            "chdddddddddddb", 0xc6, 0xa0, result, itemType, itemAmount, postId, senderId, receiverId, 0, replyReturnable,
            mailType, replyItemCount, requestContext, responseItem.GetLength(), responseItem.GetPacket()
        );
    };

    DBConn connection(runtime::process::CurrentDatabaseId());
    int receivedPostCount = 0;
    connection.Bind(&receivedPostCount);
    constexpr const wchar_t* countStatement = L"{Call lin_GetReceivedPostCount (?)}";
    connection.Prepare(countStatement);
    connection.BindParameter(&receiverId);
    if (connection.ExecutePrepared(countStatement) && connection.Fetch() && receivedPostCount > 0xef) {
        sendReply(-6, 0, returnable, 0);
        return false;
    }

    CWareHouseSP postWarehouse = user->GetWareHouse(4, true);
    CItemSP item;
    if (itemAmount != 0) {
        ItemOptionKey optionKey{};
        BaseAttribute baseAttribute;
        item = postWarehouse->MakeNewItem(
            static_cast<unsigned int>(senderId), static_cast<unsigned int>(itemType), itemAmount, 0, 0, 0, 0, 0, 4,
            optionKey, 0, baseAttribute, 0, 0
        );
    }
    {
        L2_WRITE_GUARD(*postWarehouse.operator->());
        if (item)
            postWarehouse->PushItem(item.get());
    }

    const std::u16string title(titlePacket.data());
    const std::u16string contents(contentsPacket.data());
    int postId = 0;
    int trade = 0;
    int process = 0;
    int senderDeleted = 0;
    int receiverDeleted = 0;
    const int sentTime = runtime::crt::Time32Now();
    std::int64_t tradeMoney = 0;
    std::int64_t postFee = 0;
    const int recordedId = senderId;

    connection.UnBind();
    connection.Bind(&postId);
    constexpr const wchar_t* createPostStatement = L"{Call lin_CreatePost (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)}";
    connection.Prepare(createPostStatement);
    connection.BindParameter(&senderId);
    connection.BindParameter(&receiverId);
    connection.BindParameter(&trade);
    connection.BindParameter(title.c_str());
    connection.BindParameter(contents.c_str());
    connection.BindParameter(&process);
    connection.BindParameter(&returnable);
    connection.BindParameter(&senderDeleted);
    connection.BindParameter(&receiverDeleted);
    connection.BindParameter(&sentTime);
    connection.BindParameter(&itemCount);
    connection.BindParameter(&tradeMoney);
    connection.BindParameter(&postFee);
    connection.BindParameter(&mailType);
    connection.BindParameter(&recordedId);
    if (!connection.ExecutePrepared(createPostStatement) || !connection.Fetch()) {
        // Log.AddT(LOG_ERROR, L"WritePost Failed. %d to %d.", senderId, receiverId)
        // L2_LOG_ERROR(Log, L"WritePost Failed. %d to %d.", senderId, receiverId)
        L2_LOG_ERROR(Log,
            "[mail] Post creation query failed or returned no row: sender %d, recipient %d, mail type %d, "
            "request %d. Sending failure result -5.", senderId, receiverId, mailType, requestContext
        );
        sendReply(-5, 0, 0, 0);
        return false;
    }

    WriteBirthdayPostLog(0x1f71, user, postId, itemType, itemCount);
    if (item) {
        responseItem.Assemble("dQ", item->ItemID(), static_cast<std::int64_t>(1));
    }

    // This UnBind is present before the admin insert. The target does not
    // clear those three input bindings before the optional mentor-reward
    // call below, preserving the shipping six-binding failure mode.
    connection.UnBind();
    if (item) {
        const int itemId = static_cast<int>(item->ItemID());
        constexpr const wchar_t* createItemStatement = L"{Call lin_CreatePostItemForAdmin (?, ?, ?)}";
        connection.Prepare(createItemStatement);
        connection.BindParameter(&itemId);
        connection.BindParameter(&postId);
        connection.BindParameter(&senderId);
        if (!connection.ExecutePrepared(createItemStatement)) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[%s][%d] CreatePostItem failed. item id[%d] "
            //     L"mail id[%d] sender id [%d]",
            //     L"ServerPacketEx.cpp", 0x25f7, itemId, postId, senderId
            // )
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] CreatePostItem failed. item id[%d] "
            //     L"mail id[%d] sender id [%d]",
            //     L"ServerPacketEx.cpp", 0x25f7, itemId, postId, senderId
            // )
            L2_LOG_ERROR(Log,
                "[mail] Database request to attach item %d to post %d failed: sender %d, recipient %d. "
                "Post creation already returned an ID; processing continues.", itemId, postId, senderId, receiverId
            );
        }
    }

    // if (mailType == 3) {
    if (mailType == Post::BirthdayMail) {
        const runtime::SystemTime localTime = runtime::GetLocalTimeNow();
        SYSTEMTIME birthday{};
        birthday.wYear = localTime.wYear;
        birthday.wMonth = localTime.wMonth;
        birthday.wDayOfWeek = localTime.wDayOfWeek;
        birthday.wDay = localTime.wDay;
        birthday.wHour = localTime.wHour;
        birthday.wMinute = localTime.wMinute;
        birthday.wSecond = localTime.wSecond;
        birthday.wMilliseconds = localTime.wMilliseconds;
        user->SaveBirthdayGift(&birthday);
    // } else if (mailType == 6) {
    } else if (mailType == Post::Mentoring) {
        int menteeId = 0;
        ::Disassemble(cursor, "d", &menteeId);
        if (menteeId > 0) {
            constexpr const wchar_t* mentorStatement = L"{Call lin_ReceiveMentorReward (?, ?, ?)}";
            connection.Prepare(mentorStatement);
            connection.BindParameter(&receiverId);
            connection.BindParameter(&menteeId);
            connection.BindParameter(&itemAmount);
            connection.ExecutePrepared(mentorStatement);
        }
    // } else if (mailType == 8) {
    } else if (mailType == Post::KalieEventReward) {
        WriteBirthdayPostLog(0x2334, user, postId, itemType, itemCount);
    // } else if (mailType == 11 || mailType == 12) {
    } else if (mailType == Post::DormantUserGift || mailType == Post::NewbieGift) {
        connection.UnBind();
        const int accountId = user->GetAccountID();
        // const int dormantType = mailType == 11 ? 3 : 2.
        const int dormantType = mailType == Post::DormantUserGift ? DormantConst::DT_DORMANT : DormantConst::DT_NEWBIE;
        const std::int64_t giftTime = static_cast<std::int64_t>(std::time(nullptr));
        constexpr const wchar_t* dormantStatement = L"{Call lin_SetDormantGiftTime (?, ?, ?, ?)}";
        connection.Prepare(dormantStatement);
        connection.BindParameter(&accountId);
        connection.BindParameter(&receiverId);
        connection.BindParameter(&dormantType);
        connection.BindParameter(&giftTime);
        connection.ExecutePrepared(dormantStatement);
    }

    sendReply(1, postId, returnable, itemCount);
    return false;
}

bool RequestDeleteBirthDayMail(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0056bf84. Mutation and reply are both gated by a
    // loaded user and the first positive post-item ID.
    L2_GUARD_FUNCTION();

    int postId = 0;
    int requestValue = 0;
    int deleteFlags = 0;
    int receiverId = 0;
    int requestReason = 0;
    ::Disassemble(packet, "ddddd", &postId, &requestValue, &deleteFlags, &receiverId, &requestReason);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* loadPostStatement = L"{Call lin_LoadPostByPostId (?)}";
    connection.Prepare(loadPostStatement);
    connection.BindParameter(&postId);
    if (!connection.ExecutePrepared(loadPostStatement) || !connection.Fetch()) {
        return false;
    }

    connection.UnBind();
    int itemId = 0;
    std::int64_t amount = 0;
    connection.Bind(&itemId);
    connection.Bind(&amount);
    constexpr const wchar_t* loadItemStatement = L"{Call lin_LoadPostItem (?)}";
    connection.Prepare(loadItemStatement);
    connection.BindParameter(&postId);
    if (!connection.ExecutePrepared(loadItemStatement) || !connection.Fetch()) {
        return false;
    }

    CUserSP user = g_userDb.GetUser(receiverId, true, true, runtime::process::CurrentDatabaseId());
    if (!user || itemId <= 0)
        return false;

    CItemSP item;
    CWareHouseSP postWarehouse = user->GetWareHouse(4, true);
    if (postWarehouse) {
        item = postWarehouse->GetItem(itemId);
        if (!item) {
            // Log.AddT(LOG_ERROR, L"No post item %d for user %s post %d", itemId, user->GetCharName(), postId)
            // L2_LOG_ERROR(Log, L"No post item %d for user %s post %d", itemId, user->GetCharName(), postId)
            // Previous: L2_LOG_ERROR(Log, "Birthday mail %d: item %d is missing from character %d's mail warehouse, database deletion will still be attempted.", postId, itemId, receiverId)
            L2_LOG_ERROR(Log, "[birthday mail] Item %d is missing from character %d's mail warehouse for post %d; database deletion will still be attempted.", itemId, receiverId, postId);
        } else {
            {
                L2_WRITE_GUARD(*postWarehouse.operator->());
                postWarehouse->PopItem(itemId);
            }
        }
    }

    connection.UnBind();
    int deleteResult = 0;
    connection.Bind(&deleteResult);
    constexpr const wchar_t* deleteItemStatement = L"{Call lin_DeleteItem (?, NULL)}";
    connection.Prepare(deleteItemStatement);
    connection.BindParameter(&itemId);
    const bool deletedFromDatabase =
        connection.ExecutePrepared(deleteItemStatement) && connection.Fetch() && deleteResult == 1;
    if (item && deletedFromDatabase) {
        WriteDeletedBirthdayItemLog(user, *item.get());
    } else {
        // Log.AddT(LOG_ERROR, L"No post item %d in DB for user %s post %d", itemId, user->GetCharName(), postId)
        // L2_LOG_ERROR(Log, L"No post item %d in DB for user %s post %d", itemId, user->GetCharName(), postId)
        // Previous: L2_LOG_ERROR(Log, "Birthday mail %d: item deletion audit skipped for item %d, character %d, cached item %s, database deletion %s. Mail cleanup will continue.", postId, itemId, receiverId,
        //     item ? "available" : "unavailable", deletedFromDatabase ? "confirmed" : "not confirmed")
        L2_LOG_ERROR(Log, "[birthday mail] Item-deletion audit skipped for post %d, item %d, character %d: cached item %s, database deletion %s. Mail cleanup will continue.", postId, itemId, receiverId,
            item ? "available" : "unavailable", deletedFromDatabase ? "confirmed" : "not confirmed");
    }

    connection.UnBind();
    constexpr const wchar_t* deletePostStatement = L"{Call lin_DeletePost (?)}";
    connection.Prepare(deletePostStatement);
    connection.BindParameter(&postId);
    connection.ExecutePrepared(deletePostStatement);

    connection.UnBind();
    constexpr const wchar_t* deleteAdminItemStatement = L"{Call lin_DeletePostItemForAdmin (?)}";
    connection.Prepare(deleteAdminItemStatement);
    connection.BindParameter(&postId);
    connection.ExecutePrepared(deleteAdminItemStatement);

    CPacketHelperEx<20480> response;
    response.Assemble("ddd", 1, postId, requestValue);
    WriteDeletePostLog(user, user, postId, 2, requestReason);
    socket->Send("chddddb", 0xc6, 0xa1, deleteFlags, receiverId, receiverId, 1, response.GetLength(), response.GetPacket());
    return false;
}

bool RequestLoadGroupToggleStateEx(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0056d6f0.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int requestContext = 0;
    ::Disassemble(packet, "dd", &characterId, &requestContext);

    int groupId = 0;
    int skillId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&groupId);
    connection.Bind(&skillId);
    constexpr const wchar_t* statement = L"{Call lin_LoadGroupToggleState (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);

    CPacketHelperEx<20480> rows;
    int rowCount = 0;
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            rows.Assemble("dd", groupId, skillId);
            ++rowCount;
        }
    }

    if (rowCount > 0) {
        socket->Send("chddb", 0xc6, 0xa2, requestContext, rowCount, rows.GetLength(), rows.GetPacket());
    } else {
        socket->Send("chdd", 0xc6, 0xa2, requestContext, 0);
    }
    return false;
}

bool RequestSaveGroupToggleStateEx(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0056d2b4.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int rowCount = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &characterId, &rowCount);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.AddT(LOG_ERROR, L"[%S][%d]Cannot find user, id[%d]", "ServerPacketEx.cpp", 0x273d, characterId)
        // L2_LOG_ERROR(Log, L"[%S][%d]Cannot find user, id[%d]", "ServerPacketEx.cpp", 0x273d, characterId)
        L2_LOG_ERROR(Log,
            "[skills] Cannot save toggle-group state for character %d: character unavailable. "
            "Requested row count %d; update skipped.", characterId, rowCount
        );
        return false;
    }
    if (rowCount < 0)
        return false;

    DBConn connection(runtime::process::CurrentDatabaseId());
    int groupId = 0;
    int skillId = 0;
    if (rowCount == 0) {
        constexpr const wchar_t* statement = L"{Call lin_InitGroupToggleState(?)}";
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        if (!connection.ExecutePrepared(statement)) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[%S][%d]Cannot SaveGroupToggleState, "
            //     L"char id[%d][%d][%d]",
            //     "ServerPacketEx.cpp", 0x2750, characterId, groupId, skillId
            // )
            // L2_LOG_ERROR(Log,
            //     L"[%S][%d]Cannot SaveGroupToggleState, "
            //     L"char id[%d][%d][%d]",
            //     "ServerPacketEx.cpp", 0x2750, characterId, groupId, skillId
            // )
            L2_LOG_ERROR(Log,
                "[skills] Database request to clear toggle-group state for character %d failed.", characterId
            );
        }
        return false;
    }

    {
        L2_WRITE_GUARD(*user.operator->());
        for (int index = 0; index < rowCount; ++index) {
            cursor = ::Disassemble(cursor, "dd", &groupId, &skillId);
            if (groupId != 0 && skillId != 0) {
                constexpr const wchar_t* statement = L"{Call lin_SaveGroupToggleState (?, ?, ?)}";
                connection.Prepare(statement);
                connection.BindParameter(&characterId);
                connection.BindParameter(&groupId);
                connection.BindParameter(&skillId);
                if (!connection.ExecutePrepared(statement)) {
                    // Log.AddT(
                    //     LOG_ERROR,
                    //     L"[%S][%d]Cannot SaveGroupToggleState, "
                    //     L"char id[%d][%d][%d]",
                    //     "ServerPacketEx.cpp", 0x2761, characterId, groupId, skillId
                    // )
                    // L2_LOG_ERROR(Log,
                    //     L"[%S][%d]Cannot SaveGroupToggleState, "
                    //     L"char id[%d][%d][%d]",
                    //     "ServerPacketEx.cpp", 0x2761, characterId, groupId, skillId
                    // )
                    L2_LOG_ERROR(Log,
                        "[skills] Database request to save toggle group %d with skill %d for character %d failed "
                        "at row %d of %d. Continuing with remaining rows.",
                        groupId, skillId, characterId, index + 1, rowCount
                    );
                }
            }
            connection.ResetParam();
        }
    }
    return false;
}

bool RequestResetProfileEx(CSocket* socket [[maybe_unused]], const unsigned char* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0052bbfc.
    L2_GUARD_FUNCTION();

    GuardProfileHelper::ResetGuardProfile();
    SystemMail::sendResetProfile(mailServer);
    return false;
}

bool RequestReportProfileEx(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0052bc9c.
    L2_GUARD_FUNCTION();

    int reportType = 0;
    ::Disassemble(packet, "d", &reportType);
    if (reportType == 0) {
        ReportGuardProfile();
    } else if (reportType == 1) {
        SystemMail::sendReportProfile(mailServer);
    }
    return false;
}

bool RequestSavePledgeWarPoint(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0055053c.
    L2_GUARD_FUNCTION();

    int challengerId = 0;
    int challengeeId = 0;
    int warPoint = 0;
    ::Disassemble(packet, "ddd", &challengerId, &challengeeId, &warPoint);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SavePledgeWarPoint (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&challengerId);
    connection.BindParameter(&challengeeId);
    connection.BindParameter(&warPoint);
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestSavePledgeWarPointPrev(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x005506bc. Paired current
    // CDB::SavePledgeWarPointPrev 0x00648364 fixes challenger, challengee and
    // previous-point order.
    L2_GUARD_FUNCTION();

    int challengerId = 0;
    int challengeeId = 0;
    int previousWarPoint = 0;
    ::Disassemble(packet, "ddd", &challengerId, &challengeeId, &previousWarPoint);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SavePledgeWarPointPrev (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&challengerId);
    connection.BindParameter(&challengeeId);
    connection.BindParameter(&previousWarPoint);
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestUpdatePledgeWarKillCount(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0055083c. Paired current
    // CDB::RequestUpdatePledgeWarKillCount 0x00648440 fixes challenger,
    // challengee and death-count order.
    L2_GUARD_FUNCTION();

    int challengerId = 0;
    int challengeeId = 0;
    int deathCount = 0;
    ::Disassemble(packet, "ddd", &challengerId, &challengeeId, &deathCount);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdatePledgeWarKillCount (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&challengerId);
    connection.BindParameter(&challengeeId);
    connection.BindParameter(&deathCount);
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestResetWarTargeted(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x005509bc. All three compatible SQL donor families
    // keep the same challenger/challengee/cancel-time effect.
    L2_GUARD_FUNCTION();

    int challengerId = 0;
    int challengeeId = 0;
    int cancelTime = 0;
    ::Disassemble(packet, "ddd", &challengerId, &challengeeId, &cancelTime);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_ResetWarTargeted (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&challengerId);
    connection.BindParameter(&challengeeId);
    connection.BindParameter(&cancelTime);
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestSaveAgathionSummonTime(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0056ed34.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int agathionId = 0;
    int summonTime = 0;
    ::Disassemble(packet, "ddd", &characterId, &agathionId, &summonTime);

    int elapsedTime = -1;
    CUserSP user = g_userDb.GetUser(characterId, false, false, runtime::process::CurrentDatabaseId());
    if (user) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Bind(&elapsedTime);
        constexpr const wchar_t* statement = L"{Call lin_SaveAgathionSummonTime  (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.BindParameter(&agathionId);
        connection.BindParameter(&summonTime);
        if (!connection.ExecutePrepared(statement)) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"SaveAgathionSummonTime() failed. "
            //     L"nUserDBID[%d], nAgathionID[%d]",
            //     characterId, agathionId
            // )
            // L2_LOG_ERROR(Log,
            //     L"SaveAgathionSummonTime() failed. "
            //     L"nUserDBID[%d], nAgathionID[%d]",
            //     characterId, agathionId
            // )
            // Previous: L2_LOG_ERROR(Log, "Cannot save summon time %d for Agathion %d, character %d: database request failed, replying with elapsed time %d.", summonTime, agathionId, characterId, elapsedTime)
            L2_LOG_ERROR(Log, "[Agathion] Cannot save summon time %d for Agathion %d, character %d: the database request failed; replying with elapsed time %d.", summonTime, agathionId, characterId, elapsedTime);
        } else {
            // The current handler does not branch on the fetch result.
            connection.Fetch();
        }
    }

    socket->Send("chddd", 0xc6, 0xb2, characterId, agathionId, elapsedTime);
    return false;
}

bool RequestSaveAgathionDespawnTime(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0056ef90.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int agathionId = 0;
    int despawnTime = 0;
    ::Disassemble(packet, "ddd", &characterId, &agathionId, &despawnTime);

    CUserSP user = g_userDb.GetUser(characterId, false, false, runtime::process::CurrentDatabaseId());
    if (user) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_SaveAgathionDespawnTime  (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.BindParameter(&agathionId);
        connection.BindParameter(&despawnTime);
        connection.ExecutePrepared(statement);
    }
    return false;
}

bool RequestGetAgathionElapsedTime(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0056f168.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int agathionId = 0;
    ::Disassemble(packet, "dd", &characterId, &agathionId);

    CUserSP user = g_userDb.GetUser(characterId, false, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    // The shipping procedure returns a row on both its found and missing
    // branches. Zero is therefore the non-observable deterministic value for
    // the target's unchecked Fetch path.
    int elapsedTime = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&elapsedTime);
    constexpr const wchar_t* statement = L"{Call lin_GetAgathionElapsedTime  (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&agathionId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"GetAgathionElapsedTime() failed. "
        //     L"nUserDBID[%d], nAgathionID[%d]",
        //     characterId, agathionId
        // )
        // L2_LOG_ERROR(Log,
        //     L"GetAgathionElapsedTime() failed. "
        //     L"nUserDBID[%d], nAgathionID[%d]",
        //     characterId, agathionId
        // )
        // Previous: L2_LOG_ERROR(Log, "Cannot load elapsed time for Agathion %d, character %d: database query failed, no reply sent.", agathionId, characterId)
        L2_LOG_ERROR(Log, "[Agathion] Cannot load elapsed time for Agathion %d, character %d: the database query failed; no reply sent.", agathionId, characterId);
        return false;
    }
    connection.Fetch();
    socket->Send("chddd", 0xc6, 0xb2, characterId, agathionId, elapsedTime);
    return false;
}

bool RequestUpdateAgathionElapsedTime(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0056f3cc.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int agathionId = 0;
    int updateTime = 0;
    ::Disassemble(packet, "ddd", &characterId, &agathionId, &updateTime);

    CUserSP user = g_userDb.GetUser(characterId, false, false, runtime::process::CurrentDatabaseId());
    if (user) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_UpdateAgathionElapsedTime  (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.BindParameter(&agathionId);
        connection.BindParameter(&updateTime);
        connection.ExecutePrepared(statement);
    }
    return false;
}

bool RequestInitAgathionElapsedTime(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0056f5a4.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int agathionId = 0;
    int initTime = 0;
    ::Disassemble(packet, "ddd", &characterId, &agathionId, &initTime);

    CUserSP user = g_userDb.GetUser(characterId, false, false, runtime::process::CurrentDatabaseId());
    if (user) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_InitAgathionElapsedTime  (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.BindParameter(&agathionId);
        connection.BindParameter(&initTime);
        connection.ExecutePrepared(statement);
    }
    return false;
}

bool RequestSaveRetrieveGoods(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0056f77c.
    L2_GUARD_FUNCTION();

    long long goodsUid = 0;
    int characterId = 0;
    int goodsId = 0;
    int itemType = 0;
    int itemAmount = 0;
    ::Disassemble(packet, "Qdddd", &goodsUid, &characterId, &goodsId, &itemType, &itemAmount);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_CreatePremiumGoodsReceive (?,?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&goodsUid);
    connection.BindParameter(&characterId);
    connection.BindParameter(&goodsId);
    connection.BindParameter(&itemType);
    connection.BindParameter(&itemAmount);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"RequestSaveRetrieveGoods error, GoodsUId[%I64d] "
        //     L"GoodType[%d] CharId[%d]",
        //     goodsUid, goodsId, characterId
        // )
        // L2_LOG_ERROR(Log,
        //     L"RequestSaveRetrieveGoods error, GoodsUId[%I64d] "
        //     L"GoodType[%d] CharId[%d]",
        //     goodsUid, goodsId, characterId
        // )
        // Previous: L2_LOG_ERROR(Log, "Cannot record premium goods receipt: database request failed for goods UID %lld, goods %d, character %d, item %d, amount %d.", goodsUid, goodsId, characterId, itemType, itemAmount)
        L2_LOG_ERROR(Log, "[premium goods] Cannot record receipt: the database request failed for goods UID %lld, goods %d, character %d, item %d, amount %d.", goodsUid, goodsId, characterId, itemType, itemAmount);
    }
    return false;
}

bool RequestSaveDailyPCCafePoint(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0056f924.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int point = 0;
    int checkLevel = 0;
    ::Disassemble(packet, "ddd", &characterId, &point, &checkLevel);

    CUserSP user = g_userDb.GetUser(characterId, false, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    const int accountId = user->GetAccountID();
    int dailyPoint = 0;
    int savedCheckLevel = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&dailyPoint);
    connection.Bind(&savedCheckLevel);
    constexpr const wchar_t* statement = L"{Call lin_SaveAccountPCCafeDailyPoint (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&accountId);
    connection.BindParameter(&point);
    connection.BindParameter(&checkLevel);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"RequestSaveDailyPCCafePoint error, CharId[%d] "
        //     L"nAccountId[%d] Point[%d] CheckLevel[%d]",
        //     characterId, accountId, point, checkLevel
        // )
        // L2_LOG_ERROR(Log,
        //     L"RequestSaveDailyPCCafePoint error, CharId[%d] "
        //     L"nAccountId[%d] Point[%d] CheckLevel[%d]",
        //     characterId, accountId, point, checkLevel
        // )
        // Previous: L2_LOG_ERROR(Log, "Cannot save daily PC cafe points %d, check level %d for account %d, character %d: database request failed, replying with daily points %d and check level %d.", point, checkLevel, accountId, characterId, dailyPoint, savedCheckLevel)
        L2_LOG_ERROR(Log, "[PC cafe] Cannot save daily points %d, check level %d for account %d, character %d: the database request failed; replying with daily points %d and check level %d.", point, checkLevel, accountId, characterId, dailyPoint, savedCheckLevel);
    } else {
        connection.Fetch();
    }

    socket->Send("chddd", 0xc6, 0xb3, characterId, dailyPoint, savedCheckLevel);
    return false;
}

bool RequestSetDailyResetTime(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0056fba8.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 30> packetEventName{};
    std::array<char16_t, 6> packetResetTime{};
    ::Disassemble(packet, "SS", 0x3c, packetEventName.data(), 0x0c, packetResetTime.data());
    const std::u16string eventName(packetEventName.data());
    const std::u16string resetTime(packetResetTime.data());

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetDailyResetTime(?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(eventName.c_str());
    connection.BindParameter(resetTime.c_str());
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestClearOneEventTime(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0056fd08.
    L2_GUARD_FUNCTION();

    int eventType = 0;
    int eventVariable = 0;
    ::Disassemble(packet, "dd", &eventType, &eventVariable);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_ClearOneEventTime (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&eventType);
    connection.BindParameter(&eventVariable);
    if (connection.ExecutePrepared(statement)) {
        ClearOneEventTimeOperation operation(eventType, eventVariable);
        g_accounts.FOR_ALL(&operation, L"ServerPacketEx.cpp", 0x2b1a);
    }
    return false;
}

bool RequestLoadTimeToGiveKarmaPoint(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0053cc40.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        socket->Send("chdd", 0xc6, 0xb5, user->GetId(), user->LoadTimeToGiveKarmaPoint());
    }
    return false;
}

bool RequestSaveTimeToGiveKarmaPoint(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0053cddc.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int timeToGiveKarmaPoint = 0;
    ::Disassemble(packet, "dd", &characterId, &timeToGiveKarmaPoint);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        user->UpdateTimeToGiveKarmaPoint(timeToGiveKarmaPoint);
    }
    return false;
}

namespace InstantAgitJobHandler {

void SendInstantAgitDecoNpc(CSocket* socket, int instantAgitId, const CPacketHelperEx<20480>& packet) {
    // Current Cached.exe 0x0052cbbc. The target signature keeps a const
    // helper reference although its historical packet interface is not
    // const-qualified.
    L2_GUARD_FUNCTION();
    if (!socket)
        return;

    auto& mutablePacket = const_cast<CPacketHelperEx<20480>&>(packet);
    if (mutablePacket.GetAssembledCount() == 0)
        return;
    socket->Send(
        "chcddb", 0xc6, 0xb0, 13, instantAgitId, mutablePacket.GetAssembledCount(), mutablePacket.GetLength(),
        mutablePacket.GetPacket()
    );
}

int LoadInstantAgitLottery(CSocket* socket) {
    // Current Cached.exe 0x0053d848.
    L2_GUARD_FUNCTION();

    int instantAgitId = 0;
    int nextLotteryDate = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&instantAgitId);
    connection.Bind(&nextLotteryDate);
    constexpr const wchar_t* statement = L"{Call lin_LoadInstantAgitLottery}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement))
        return 0;

    CPacketHelperEx<20480> rows;
    while (connection.Fetch()) {
        rows.Assemble("dd", instantAgitId, nextLotteryDate);
    }
    if (rows.GetAssembledCount() <= 0)
        return 0;
    socket->Send("chcdb", 0xc6, 0xb0, 8, rows.GetAssembledCount(), rows.GetLength(), rows.GetPacket());
    return 1;
}

int LoadInstantAgit(CSocket* socket) {
    // Current Cached.exe 0x0053da38.
    L2_GUARD_FUNCTION();

    int instantAgitId = 0;
    int pledgeId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&instantAgitId);
    connection.Bind(&pledgeId);
    constexpr const wchar_t* statement = L"{Call lin_LoadInstantAgit}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement))
        return 0;

    int packetCount = 0;
    int currentInstantAgitId = -1;
    CPacketHelperEx<20480> pledges;
    while (connection.Fetch()) {
        if (currentInstantAgitId != instantAgitId && pledges.GetAssembledCount() > 0) {
            socket->Send(
                "chcddb", 0xc6, 0xb0, 9, currentInstantAgitId, pledges.GetAssembledCount(), pledges.GetLength(),
                pledges.GetPacket()
            );
            ++packetCount;
            pledges.Reset();
        }
        currentInstantAgitId = instantAgitId;
        pledges.Assemble("d", pledgeId);
    }
    if (pledges.GetAssembledCount() > 0) {
        socket->Send(
            "chcddb", 0xc6, 0xb0, 9, currentInstantAgitId, pledges.GetAssembledCount(), pledges.GetLength(),
            pledges.GetPacket()
        );
        ++packetCount;
    }
    return packetCount;
}

int LoadInstantAgitApplicant(CSocket* socket) {
    // Current Cached.exe 0x0053dcac.
    L2_GUARD_FUNCTION();

    int instantAgitId = 0;
    int pledgeId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&instantAgitId);
    connection.Bind(&pledgeId);
    constexpr const wchar_t* statement = L"{Call lin_LoadInstantAgitAllApplicant}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement))
        return 0;

    int packetCount = 0;
    int currentInstantAgitId = -1;
    CPacketHelperEx<20480> applicants;
    while (connection.Fetch()) {
        if (currentInstantAgitId == instantAgitId) {
            // The current body tests the existing assembled-row count before
            // adding the current pledge, so a packet can contain 4001 rows.
            if (applicants.GetAssembledCount() > 4000) {
                socket->Send(
                    "chcddb", 0xc6, 0xb0, 10, currentInstantAgitId, applicants.GetAssembledCount(), applicants.GetLength(),
                    applicants.GetPacket()
                );
                ++packetCount;
                applicants.Reset();
            }
        } else {
            if (applicants.GetAssembledCount() > 0) {
                socket->Send(
                    "chcddb", 0xc6, 0xb0, 10, currentInstantAgitId, applicants.GetAssembledCount(), applicants.GetLength(),
                    applicants.GetPacket()
                );
                ++packetCount;
                applicants.Reset();
            }
            currentInstantAgitId = instantAgitId;
        }
        applicants.Assemble("d", pledgeId);
    }
    if (applicants.GetAssembledCount() > 0) {
        socket->Send(
            "chcddb", 0xc6, 0xb0, 10, currentInstantAgitId, applicants.GetAssembledCount(), applicants.GetLength(),
            applicants.GetPacket()
        );
        ++packetCount;
    }
    return packetCount;
}

void LoadInstantAgitDeco(CSocket* socket) {
    // Current Cached.exe 0x0053df60.
    L2_GUARD_FUNCTION();

    int pledgeId = 0;
    int decoId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&pledgeId);
    connection.Bind(&decoId);
    constexpr const wchar_t* statement = L"{Call lin_LoadInstantAgitDeco}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement))
        return;

    int currentPledgeId = -1;
    CPacketHelperEx<20480> decorations;
    while (connection.Fetch()) {
        if (currentPledgeId != pledgeId && decorations.GetAssembledCount() > 0) {
            socket->Send(
                "chcddb", 0xc6, 0xb0, 11, currentPledgeId, decorations.GetAssembledCount(), decorations.GetLength(),
                decorations.GetPacket()
            );
            decorations.Reset();
        }
        currentPledgeId = pledgeId;
        decorations.Assemble("d", decoId);
    }
    if (decorations.GetAssembledCount() > 0) {
        socket->Send(
            "chcddb", 0xc6, 0xb0, 11, currentPledgeId, decorations.GetAssembledCount(), decorations.GetLength(),
            decorations.GetPacket()
        );
    }
}

void LoadInstantAgitDecoNpc(CSocket* socket) {
    // Current Cached.exe 0x0053e1c4.
    L2_GUARD_FUNCTION();

    int instantAgitId = 0;
    int slot = 0;
    int decoNpcId = 0;
    int expireTime = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&instantAgitId);
    connection.Bind(&slot);
    connection.Bind(&decoNpcId);
    connection.Bind(&expireTime);
    constexpr const wchar_t* statement = L"{Call lin_InstantAgitDecoNpcLoadAll}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement))
        return;

    int currentInstantAgitId = -1;
    CPacketHelperEx<20480> decorations;
    while (connection.Fetch()) {
        if (currentInstantAgitId != instantAgitId && decorations.GetAssembledCount() > 0) {
            SendInstantAgitDecoNpc(socket, currentInstantAgitId, decorations);
            decorations.Reset();
        }
        currentInstantAgitId = instantAgitId;
        decorations.Assemble("ddd", slot, decoNpcId, expireTime);
    }
    if (decorations.GetAssembledCount() > 0) {
        SendInstantAgitDecoNpc(socket, currentInstantAgitId, decorations);
    }
}

void OnLoad(CSocket* socket) {
    // Current Cached.exe 0x0053e3d4.
    L2_GUARD_FUNCTION();
    const int lotteryPackets = LoadInstantAgitLottery(socket);
    const int agitPackets = LoadInstantAgit(socket);
    const int applicantPackets = LoadInstantAgitApplicant(socket);
    socket->Send("chcddd", 0xc6, 0xb0, 7, lotteryPackets, agitPackets, applicantPackets);
}

void OnChooseOwner(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0058f78c.
    L2_GUARD_FUNCTION();

    int instantAgitId = 0;
    int nextLotteryDate = 0;
    std::int64_t refundAdena = 0;
    int winnerCount = 0;
    const unsigned char* cursor =
        ::Disassemble(packet, "ddQd", &instantAgitId, &nextLotteryDate, &refundAdena, &winnerCount);

    std::set<int> winners;
    CPacketHelperEx<20480> winnerPacket;
    for (int index = 0; index < winnerCount; ++index) {
        int pledgeId = 0;
        cursor = ::Disassemble(cursor, "d", &pledgeId);
        winners.insert(pledgeId);
        winnerPacket.Assemble("d", pledgeId);
    }

    int applicantPledgeId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&applicantPledgeId);
    constexpr const wchar_t* loadApplicants = L"{Call lin_LoadInstantAgitApplicant (?)}";
    connection.Prepare(loadApplicants);
    connection.BindParameter(&instantAgitId);
    if (connection.ExecutePrepared(loadApplicants)) {
        while (connection.Fetch()) {
            if (winners.find(applicantPledgeId) != winners.end()) {
                continue;
            }

            CPledgeSP pledge = g_pledgeDb.GetPledge(applicantPledgeId, false, runtime::process::CurrentDatabaseId());
            if (!pledge)
                continue;
            CWareHouseSP warehouse = pledge->GetWareHouse();
            if (!warehouse)
                continue;

            {
                L2_WRITE_GUARD(*warehouse.operator->());
                warehouse->AddAdena(refundAdena);
            }

            CLogFormatter formatter;
            formatter.WriteLog(
                0x80f, applicantPledgeId, 0, 0, 0, 0, 0, 0, u"", u"", u"", instantAgitId, refundAdena, 0, 0, 0, 0, 0, 0, 0,
                0, u"", u"", u"", u"", 0
            );
        }
    }

    connection.UnBind();
    constexpr const wchar_t* setNextLottery = L"{Call lin_SetInstantAgitNextLottery (?, ?)}";
    connection.Prepare(setNextLottery);
    connection.BindParameter(&instantAgitId);
    connection.BindParameter(&nextLotteryDate);
    connection.ExecutePrepared(setNextLottery);

    for (const int winnerPledgeId : winners) {
        connection.UnBind();
        // Current helper 0x004afbd0 binds _time32's four-byte value through
        // the Shared long-to-SQL_DOUBLE parameter specialization.
        const long rentalDate = runtime::crt::Time32Now();
        constexpr const wchar_t* setInstantAgit = L"{Call lin_SetInstantAgit (?, ?, ?, ?)}";
        connection.Prepare(setInstantAgit);
        connection.BindParameter(&winnerPledgeId);
        connection.BindParameter(&instantAgitId);
        connection.BindParameter(&rentalDate);
        connection.BindParameter(&nextLotteryDate);
        connection.ExecutePrepared(setInstantAgit);
    }

    connection.UnBind();
    constexpr const wchar_t* resetApplicants = L"{Call lin_ResetInstantAgitApplicant (?)}";
    connection.Prepare(resetApplicants);
    connection.BindParameter(&instantAgitId);
    connection.ExecutePrepared(resetApplicants);

    socket->Send(
        "chcddb", 0xc6, 0xb0, 6, instantAgitId, winnerPacket.GetAssembledCount(), winnerPacket.GetLength(),
        winnerPacket.GetPacket()
    );
}

} // namespace InstantAgitJobHandler

namespace DynamicQusetHandler {

void OnLoadInfo(CSocket* socket) {
    // Current Cached.exe 0x0052c40c. A successful empty query has its own
    // short reply, an Execute failure sends nothing.
    L2_GUARD_FUNCTION();

    int dynamicQuestId = 0;
    int lastExecutedTime = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&dynamicQuestId);
    connection.Bind(&lastExecutedTime);
    constexpr const wchar_t* statement = L"{Call lin_LoadAllDynamicQuest}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement))
        return;

    CPacketHelperEx<20480> quests;
    while (connection.Fetch()) {
        quests.Assemble("dd", dynamicQuestId, lastExecutedTime);
    }
    if (quests.GetAssembledCount() > 0) {
        // Current Cached.exe 0x0052c59e-0x0052c5b7 places zero, row count,
        // byte length, and the packet pointer in the four stack argument
        // slots following the c6/a3 register arguments.
        socket->Send("chcdb", 0xc6, 0xa3, 0, quests.GetAssembledCount(), quests.GetLength(), quests.GetPacket());
    } else {
        socket->Send("chcd", 0xc6, 0xa3, 0, 0);
    }
}

void OnSaveInfo(int dynamicQuestId, int lastExecutedTime) {
    // Current Cached.exe 0x00570028.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveDynamicQuest (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&dynamicQuestId);
    connection.BindParameter(&lastExecutedTime);
    connection.ExecutePrepared(statement);
}

} // namespace DynamicQusetHandler

namespace PeriodicPremiumHennaJobHandler {

void OnLoadInfo(CSocket* socket, int characterId, int userSocketId) {
    // Current Cached.exe 0x00586404.
    L2_GUARD_FUNCTION();

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "ServerPacketEx.cpp", 0x42ff, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "ServerPacketEx.cpp", 0x42ff, characterId)
        L2_LOG_ERROR(Log,
            "[henna] Cannot load periodic premium henna for character %d, socket %d: character unavailable. "
            "No reply will be sent.", characterId, userSocketId
        );
        return;
    }

    int hennaId = 0;
    int startTime = 0;
    int periodMinutes = 0;
    const int subjobId = user->GetSubjobId();
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&hennaId);
    connection.Bind(&startTime);
    connection.Bind(&periodMinutes);
    constexpr const wchar_t* statement = L"{Call lin_LoadHennaPremium (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&subjobId);

    if (!connection.ExecutePrepared(statement) || !connection.Fetch() || hennaId < 1) {
        socket->Send("chdddd", 0xc6, 0xfc, 0, userSocketId, 0, 0);
        return;
    }
    if (periodMinutes < 1) {
        socket->Send("chdddd", 0xc6, 0xfc, 0, userSocketId, hennaId, -1);
        return;
    }

    user->m_hennaPremiumId = hennaId;
    const int endTime = runtime::AddInt32Wrapping(startTime, runtime::MultiplyInt32Wrapping(periodMinutes, 60));
    if (runtime::crt::Time32Now() < endTime) {
        socket->Send("chdddd", 0xc6, 0xfc, 0, userSocketId, hennaId, endTime);
        return;
    }

    socket->Send("chddd", 0xc6, 0xfc, 2, userSocketId, hennaId);
    user->DelHennaPremium(hennaId);
}

void OnExpired(CSocket* socket, int hennaId, int characterId, int userSocketId) {
    // Current Cached.exe 0x00541340.
    L2_GUARD_FUNCTION();

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return;
    const bool deleted = user->DelHennaPremium(hennaId);
    socket->Send("chddd", 0xc6, 0xfc, 2, userSocketId, deleted ? hennaId : 0);
}

void OnSetPeriod(CSocket* socket, int characterId, int userSocketId, int hennaId, int periodMinutes) {
    // Current Cached.exe 0x00541498.
    L2_GUARD_FUNCTION();

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return;
    const int startTime = runtime::crt::Time32Now();
    if (!user->AddHennaPremium(hennaId, startTime, periodMinutes)) {
        return;
    }

    socket->Send(
        "chdddd", 0xc6, 0xfc, 3, userSocketId, hennaId, runtime::AddInt32Wrapping(startTime, runtime::MultiplyInt32Wrapping(periodMinutes, 60))
    );
}

} // namespace PeriodicPremiumHennaJobHandler

namespace RQExHandler {

bool RequestLoadReservedAnnounce(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052bd64.
    L2_GUARD_FUNCTION();
    CReservedAnnounce announce;
    announce.LoadReservedAnnounce(socket, packet);
    return false;
}

bool RequestAddReservedAnnounce(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052be28.
    L2_GUARD_FUNCTION();

    short operationType = 0;
    const unsigned char* cursor = ::Disassemble(packet, "h", &operationType);
    if (operationType == 0) {
        int announceId = 0;
        short announceType = 0;
        int year = 0;
        int month = 0;
        int day = 0;
        int hour = 0;
        int minute = 0;
        int repeat = 0;
        ::Disassemble(cursor, "dhdddddd", &announceId, &announceType, &year, &month, &day, &hour, &minute, &repeat);
        CReservedAnnounce announce;
        if (announce.AddReservedAnnounce(announceId, announceType, year, month, day, hour, minute, repeat)) {
            socket->Send(
                "chhdhdddddd", 0xc6, 0x9b, 0, announceId, static_cast<int>(announceType), year, month, day, hour, minute,
                repeat
            );
        }
    } else if (operationType == 1) {
        int announceId = 0;
        short messageOrder = 0;
        std::array<char16_t, 128> packetMessage{};
        ::Disassemble(cursor, "dhS", &announceId, &messageOrder, 0x100, packetMessage.data());
        std::wstring message = Utf16ToWide(packetMessage.data());
        CReservedAnnounce announce;
        if (announce.AddReservedAnnounceMsg(announceId, messageOrder, message.data())) {
            socket->Send("chhdhS", 0xc6, 0x9b, 1, announceId, static_cast<int>(messageOrder), packetMessage.data());
        }
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     "[ServerPacketEx.cpp][%d] RequestAddReservedAnnounce : "
        //     "nType is strange [%d]",
        //     0x2983, static_cast<int>(operationType)
        // )
        // L2_LOG_ERROR(Log,
        //     "[ServerPacketEx.cpp][%d] RequestAddReservedAnnounce : "
        //     "nType is strange [%d]",
        //     0x2983, static_cast<int>(operationType)
        // )
        // Previous: L2_LOG_WARN(Log, "Cannot add scheduled announcement: unsupported operation %d, expected 0 (schedule) or 1 (message). No reply sent.", static_cast<int>(operationType))
        L2_LOG_WARN(Log, "[announcements] Cannot add a scheduled announcement: operation %d is unsupported; expected 0 (schedule) or 1 (message). No reply sent.", static_cast<int>(operationType));
    }
    return false;
}

bool RequestDeleteReservedAnnounce(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052c13c.
    L2_GUARD_FUNCTION();

    short operationType = 0;
    const unsigned char* cursor = ::Disassemble(packet, "h", &operationType);
    if (operationType == 0) {
        int announceId = 0;
        ::Disassemble(cursor, "d", &announceId);
        CReservedAnnounce announce;
        if (announce.DeleteReservedAnnounce(announceId)) {
            socket->Send("chhd", 0xc6, 0x9c, 0, announceId);
        }
    } else if (operationType == 1) {
        int announceId = 0;
        short messageOrder = 0;
        ::Disassemble(cursor, "dh", &announceId, &messageOrder);
        CReservedAnnounce announce;
        if (announce.DeleteReservedAnnounceMsg(announceId, messageOrder)) {
            socket->Send("chhdh", 0xc6, 0x9c, 1, announceId, static_cast<int>(messageOrder));
        }
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     "[ServerPacketEx.cpp][%d] RequestAddReservedAnnounce : "
        //     "nType is strange [%d]",
        //     0x29ae, static_cast<int>(operationType)
        // )
        // L2_LOG_ERROR(Log,
        //     "[ServerPacketEx.cpp][%d] RequestAddReservedAnnounce : "
        //     "nType is strange [%d]",
        //     0x29ae, static_cast<int>(operationType)
        // )
        // Previous: L2_LOG_WARN(Log, "Cannot delete scheduled announcement: unsupported operation %d, expected 0 (schedule) or 1 (message). No reply sent.", static_cast<int>(operationType))
        L2_LOG_WARN(Log, "[announcements] Cannot delete a scheduled announcement: operation %d is unsupported; expected 0 (schedule) or 1 (message). No reply sent.", static_cast<int>(operationType));
    }
    return false;
}

bool RequestLoadEventData(CSocket* socket [[maybe_unused]], const unsigned char* packet [[maybe_unused]]) {
    // Current Cached.exe 0x005331d8.
    L2_GUARD_FUNCTION();
    CurrentEventDataHelper().Load();
    return false;
}

bool RequestReloadEventData(CSocket* socket [[maybe_unused]], const unsigned char* packet [[maybe_unused]]) {
    // Current Cached.exe 0x005332a0 is identical to Load.
    L2_GUARD_FUNCTION();
    CurrentEventDataHelper().Load();
    return false;
}

bool RequestDumpEventData(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00548804.
    L2_GUARD_FUNCTION();

    int index = 0;
    int total = 0;
    std::array<char16_t, 129> packetEventName{};
    std::array<char16_t, 4097> packetContents{};
    ::Disassemble(packet, "ddSS", &index, &total, 0x102, packetEventName.data(), 0x2002, packetContents.data());
    CurrentEventDataHelper().Save(
        index, total, std::u16string(std::u16string_view(packetEventName.data())), std::u16string(u"dump"),
        std::u16string(std::u16string_view(packetContents.data()))
    );
    return false;
}

bool RequestDynamicQuest(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00570154.
    L2_GUARD_FUNCTION();

    unsigned char jobType = 0;
    const unsigned char* cursor = ::Disassemble(packet, "c", &jobType);
    if (jobType == 0) {
        DynamicQusetHandler::OnLoadInfo(socket);
    } else if (jobType == 1) {
        int dynamicQuestId = 0;
        int lastExecutedTime = 0;
        ::Disassemble(cursor, "dd", &dynamicQuestId, &lastExecutedTime);
        DynamicQusetHandler::OnSaveInfo(dynamicQuestId, lastExecutedTime);
    } else {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] invalid job type[%d]", "ServerPacketEx.cpp", 0x2b9a, static_cast<int>(jobType)
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] invalid job type[%d]", "ServerPacketEx.cpp", 0x2b9a, static_cast<int>(jobType)
        // )
        L2_LOG_WARN(Log,
            "[quests] Rejected dynamic-quest operation %u; expected 0 (load) or 1 (save). No reply will be sent.",
            static_cast<unsigned int>(jobType)
        );
    }
    return false;
}

bool RequestLoadOtherUserMacro(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0053cf14.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    int destinationCharacterId = 0;
    std::array<char16_t, 25> sourceCharacterName{};
    ::Disassemble(packet, "ddS", &userServerId, &destinationCharacterId, 0x32, sourceCharacterName.data());

    CUserSP sourceUser = g_userDb.GetUser(sourceCharacterName.data(), true);
    if (!sourceUser) {
        socket->Send("chdSd", 0xc6, 0xa4, userServerId, sourceCharacterName.data(), 0);
        return false;
    }

    CopyOtherMacroStrategy strategy(
        static_cast<int>(sourceUser->GetId()), userServerId, socket, sourceCharacterName.data(), destinationCharacterId
    );
    UserMacroHandler::SendMacro(strategy);
    socket->Send("chdSd", 0xc6, 0xa4, userServerId, sourceCharacterName.data(), 1);
    return false;
}

bool RequestLoadOtherBuilderBookmark(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00570274.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    std::array<char16_t, 25> requestedName{};
    ::Disassemble(packet, "dS", &userServerId, 0x32, requestedName.data());
    CUserSP targetUser = g_userDb.GetUser(requestedName.data(), true);
    if (!targetUser) {
        socket->Send("chdSd", 0xc6, 0xa5, userServerId, requestedName.data(), 0);
        return false;
    }

    int bookmarkId = 0;
    std::array<char16_t, 51> bookmarkName{};
    int x = 0;
    int y = 0;
    int z = 0;
    std::int64_t nullableIndicator = -1;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.BindNullable(&bookmarkId, &nullableIndicator);
    connection.BindNullable(bookmarkName.data(), 0x66, &nullableIndicator);
    connection.BindNullable(&x, &nullableIndicator);
    connection.BindNullable(&y, &nullableIndicator);
    connection.BindNullable(&z, &nullableIndicator);

    const int option = 2;
    int characterId = static_cast<int>(targetUser->GetId());
    const int emptyValue = 0;
    constexpr const wchar_t* emptyName = L"";
    constexpr const wchar_t* statement = L"{Call lin_ManBookMark (?, ?, ?, ?, ?, ?, ? )}";
    connection.Prepare(statement);
    connection.BindParameter(&option);
    connection.BindParameter(&characterId);
    connection.BindParameter(&emptyValue);
    connection.BindParameter(emptyName);
    connection.BindParameter(&emptyValue);
    connection.BindParameter(&emptyValue);
    connection.BindParameter(&emptyValue);

    CPacketHelperEx<20480> bookmarks;
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            if (bookmarks.Assemble("Sddd", bookmarkName.data(), x, y, z) <= 0) {
                break;
            }
        }
    }
    socket->Send(
        "chdSdb", 0xc6, 0xa5, userServerId, requestedName.data(), bookmarks.GetAssembledCount(), bookmarks.GetLength(),
        bookmarks.GetPacket()
    );
    return false;
}

bool RequestUpdateBlockMemo(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052c624.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int blockedCharacterId = 0;
    std::array<char16_t, 51> packetMemo{};
    ::Disassemble(packet, "ddS", &characterId, &blockedCharacterId, 0x66, packetMemo.data());
    const std::wstring memo = Utf16ToWide(packetMemo.data());
    const int result = g_blockCharDb.UpdateBlockMemo(characterId, blockedCharacterId, memo.c_str());
    socket->Send("chddS", 0xc6, 0xa6, characterId, result > 0 ? result : 0, packetMemo.data());
    return false;
}

bool RequestUpdateFriendMemo(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052c788.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int friendCharacterId = 0;
    std::array<char16_t, 51> packetMemo{};
    ::Disassemble(packet, "ddS", &characterId, &friendCharacterId, 0x66, packetMemo.data());
    const std::wstring memo = Utf16ToWide(packetMemo.data());
    const bool updated = g_friendsDb.UpdateFriendMemo(characterId, friendCharacterId, memo.c_str());
    socket->Send("chddS", 0xc6, 0xa7, characterId, updated ? friendCharacterId : 0, packetMemo.data());
    return false;
}

bool RequestCommissionTradeLoadInfo(CSocket* socket, const unsigned char* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0052c8e8 ignores its packet parameter.
    L2_GUARD_FUNCTION();

    std::int64_t totalCount = 0;
    int result = -1;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&totalCount);
    constexpr const wchar_t* statement = L"{Call lin_LoadInfoCommission}";
    connection.Prepare(statement);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        result = 1;
    } else {
        // Log.Add(LOG_ERROR,
        //     "[commission] No startup listing count returned from database %d (execution or fetch failed). Replying with failure status and count %lld.",
        //     runtime::process::CurrentDatabaseId(), static_cast<long long>(totalCount))
        L2_LOG_ERROR(Log,
            "[commission] No startup listing count returned from database %d (execution or fetch failed); replying with failure status and count %lld.",
            runtime::process::CurrentDatabaseId(), static_cast<long long>(totalCount));
    }
    socket->Send("chdQ", 0xc6, 0xa8, result, static_cast<long long>(totalCount));
    return false;
}

bool RequestCommissionTradeLoadAll(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0053d128.
    L2_GUARD_FUNCTION();

    std::int64_t loadToken = 0;
    ::Disassemble(packet, "Q", &loadToken);
    if (loadToken == 0) {
        // Log.Add(LOG_WARN,
        //     "[commission] Listing load rejected: request token is zero, failure reply follows.")
        L2_LOG_WARN(Log,
            "[commission] Listing load rejected: request token is zero; failure reply follows.");
        socket->Send("chd", 0xc6, 0xa9, -2);
        return false;
    }

    std::int64_t commissionId = 0;
    int characterId = 0;
    int buyerId = 0;
    std::array<char16_t, 101> itemName{};
    std::int64_t itemPrice = 0;
    int commissionType = 0;
    int firstType = -1;
    std::int64_t commissionFee = 0;
    int expiredType = -1;
    int expiredTime = 0;
    std::array<char16_t, 31> characterName{};
    int itemId = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&commissionId);
    connection.Bind(&characterId);
    connection.Bind(&buyerId);
    connection.Bind(itemName.data(), 200);
    connection.Bind(&itemPrice);
    connection.Bind(&commissionType);
    connection.Bind(&firstType);
    connection.Bind(&commissionFee);
    connection.Bind(&expiredType);
    connection.Bind(&expiredTime);
    connection.Bind(characterName.data(), 0x3c);
    connection.Bind(&itemId);
    constexpr const wchar_t* statement = L"{Call lin_LoadAllCommission}";
    connection.Prepare(statement);
    // if (!connection.ExecutePrepared(statement))
        // return false
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR,
        //     "[commission] Listing load query failed in database %d (request token %lld). No listing replies sent.",
        //     runtime::process::CurrentDatabaseId(), static_cast<long long>(loadToken))
        L2_LOG_ERROR(Log,
            "[commission] Listing load query failed in database %d (request token %lld); no listing replies sent.",
            runtime::process::CurrentDatabaseId(), static_cast<long long>(loadToken));
        return false;
    }

    while (connection.Fetch()) {
        CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
        if (!user) {
            // Log.Add(LOG_ERROR,
            //     "[commission] Cannot load listing %lld: character %d is unavailable (item %d, request token %lld). Reporting this listing as unavailable and continuing.",
            //     static_cast<long long>(commissionId), characterId, itemId, static_cast<long long>(loadToken))
            L2_LOG_ERROR(Log,
                "[commission] Cannot load listing %lld: character %d is unavailable (item %d, request token %lld); reporting this listing as unavailable and continuing.",
                static_cast<long long>(commissionId), characterId, itemId, static_cast<long long>(loadToken));
            socket->Send("chdQd", 0xc6, 0xa9, -1, static_cast<long long>(commissionId), commissionType);
            continue;
        }

        CWareHouseSP commissionWarehouse = user->GetWareHouse(WareHouseType_Mail, true);
        CPacketHelperEx<20480> itemPacket;
        if (!commissionWarehouse || !commissionWarehouse->GetCommissionItem(itemId, itemPacket)) {
            // Log.Add(LOG_ERROR,
            //     "[commission] Cannot load listing %lld for character %d, item %d: %s. Reporting this listing as unavailable and continuing (request token %lld).",
            //     static_cast<long long>(commissionId), characterId, itemId, !commissionWarehouse ? "commission warehouse is unavailable" : "source item was not found", static_cast<long long>(loadToken))
            L2_LOG_ERROR(Log,
                "[commission] Cannot load listing %lld for character %d, item %d: %s; reporting this listing as unavailable and continuing (request token %lld).",
                static_cast<long long>(commissionId), characterId, itemId, !commissionWarehouse ? "commission warehouse is unavailable" : "source item was not found", static_cast<long long>(loadToken));
            socket->Send("chdQd", 0xc6, 0xa9, 0, static_cast<long long>(commissionId), commissionType);
            continue;
        }

        socket->Send(
            "chdQdddSQQddSdb", 0xc6, 0xa9, 1, static_cast<long long>(commissionId), commissionType, characterId, buyerId,
            itemName.data(), static_cast<long long>(itemPrice), static_cast<long long>(commissionFee), expiredTime,
            expiredType, characterName.data(), firstType, itemPacket.GetLength(), itemPacket.GetPacket()
        );
    }
    return false;
}

bool RequestCommissionTradeRegister(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005714a0.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int itemId = 0;
    std::array<char16_t, 100> packetItemName{};
    std::int64_t itemPrice = 0;
    std::int64_t itemAmount = 0;
    int consumeType = 0;
    std::int64_t commissionFee = 0;
    int commissionType = 0;
    int expiredType = -1;
    int owner = -1;
    std::array<char16_t, 30> packetCharacterName{};
    int itemGrade = 0;
    int itemGeneral = 0;
    int firstType = 6;
    int discountType = 0;
    int globalClass = 0;
    int premiumItemId = 0;
    std::int64_t premiumItemAmount = 0;
    int itemClassId = 0;
    ::Disassemble(
        packet, "ddSQQdQdddSddddddQd", &characterId, &itemId, 200, packetItemName.data(), &itemPrice, &itemAmount,
        &consumeType, &commissionFee, &commissionType, &expiredType, &owner, 0x3c, packetCharacterName.data(), &itemGrade,
        &itemGeneral, &firstType, &discountType, &globalClass, &premiumItemId, &premiumItemAmount, &itemClassId
    );

    if (discountType != 0 || globalClass != 0 || premiumItemId != 0) {
        // Log.AddT(LOG_ERROR, L"[%s],[%d][WARNING] Global Version Commission Warning!", L"ServerPacketEx.cpp", 0x2d93)
        // Log.Add(LOG_WARN,
        //     "[commission] Registration request for character %d, item %d uses global-version fields: discount type %d, global class %d, premium item %d, premium quantity %lld. Processing continues.",
        //     characterId, itemId, discountType, globalClass, premiumItemId, static_cast<long long>(premiumItemAmount))
        L2_LOG_WARN(Log,
            "[commission] Registration request for character %d, item %d uses global-version fields: discount type %d, global class %d, premium item %d, premium quantity %lld; processing continues.",
            characterId, itemId, discountType, globalClass, premiumItemId, static_cast<long long>(premiumItemAmount));
    }

    auto sendFailure = [&]() { socket->Send("chddd", 0xc6, 0xaa, -1, characterId, commissionType); };
    // auto logRegisterFailure = [&]() {
    auto logRegisterFailure = [&](const char* reason) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[%s],[%d]Commission Register Fail!! UserDBID[%d], "
        //     L"ItemDBID[%d]",
        //     L"ServerPacketEx.cpp", 0x2e29, characterId, itemId
        // )
        // Log.Add(LOG_ERROR,
        //     "[commission] Registration rejected for character %d, item %d, quantity %lld: %s. Failure reply follows.",
        //     characterId, itemId, static_cast<long long>(itemAmount), reason)
        L2_LOG_ERROR(Log,
            "[commission] Registration rejected for character %d, item %d, quantity %lld: %s; failure reply follows.",
            characterId, itemId, static_cast<long long>(itemAmount), reason);
    };

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // logRegisterFailure()
        logRegisterFailure("character could not be loaded");
        sendFailure();
        return false;
    }

    CWareHouseSP inventory = user->GetWareHouse(WareHouseType_Inventory, true);
    CWareHouseSP commissionWarehouse = user->GetWareHouse(WareHouseType_Mail, true);
    CItemSP originalItem = inventory ? inventory->GetItem(itemId) : CItemSP{};
    std::array<char16_t, 256> shapeAndVariationLog{};
    std::array<char16_t, 256> attributeLog{};
    std::array<char16_t, 256> ensoulLog{};
    if (originalItem) {
        originalItem->MakeShapeShiftAndVariationLogInfo(shapeAndVariationLog.data());
        BaseAttribute originalAttribute(originalItem->Attribute());
        originalAttribute.GetLogString(attributeLog.data());
        // originalItem->MakeEnsoulLogInfo(ensoulLog.data())
        originalItem->MakeEnsoulLogInfo(ensoulLog.data(), ensoulLog.size());
    } else {
        // Log.AddT(LOG_ERROR, L"[LOG_REGIST_COMMISSION] Item is invalid. %d", itemId)
        // Log.Add(LOG_ERROR,
        //     "[commission] Cannot read registration audit attributes for character %d: item %d is unavailable in inventory. Registration validation continues.",
        //     characterId, itemId)
        L2_LOG_ERROR(Log,
            "[commission] Cannot read registration audit attributes for character %d: item %d is unavailable in inventory; registration validation continues.",
            characterId, itemId);
    }

    if (!inventory || !commissionWarehouse) {
        // Noise: The following registration failure reports the unavailable warehouse with character, item and quantity.
        // Log.AddT(LOG_ERROR, L"Error, Invalid sp.")
        // logRegisterFailure()
        logRegisterFailure(!inventory ? "inventory is unavailable" : "commission warehouse is unavailable");
        sendFailure();
        return false;
    }

    CPacketHelperEx<20480> itemPacket;
    int resultingItemId = 0;
    int resultingItemType = 0;
    bool written = false;
    const std::uint64_t inventoryLock = inventory->GetLockAddr();
    const std::uint64_t commissionLock = commissionWarehouse->GetLockAddr();
    if (inventoryLock == commissionLock) {
        // Log.AddT(LOG_ERROR, L"Same warehouse double writelock?-_-,")
        // Log.Add(LOG_WARN,
        //     "[commission] Registration for character %d, item %d uses the same inventory and commission warehouse. Continuing under one lock.",
        //     characterId, itemId)
        L2_LOG_WARN(Log,
            "[commission] Registration for character %d, item %d uses the same inventory and commission warehouse; continuing under one lock.",
            characterId, itemId);
        {
            L2_WRITE_GUARD(*commissionWarehouse.operator->());
            written = inventory->WriteCommission(
                socket, itemPacket, commissionWarehouse.get(), itemId, itemAmount, consumeType, commissionFee, resultingItemId,
                resultingItemType, premiumItemId, premiumItemAmount
            );
        }
    } else if (commissionLock < inventoryLock) {
        L2_WRITE_GUARD(*commissionWarehouse.operator->());
        {
            L2_WRITE_GUARD(*inventory.operator->());
            written = inventory->WriteCommission(
                socket, itemPacket, commissionWarehouse.get(), itemId, itemAmount, consumeType, commissionFee, resultingItemId,
                resultingItemType, premiumItemId, premiumItemAmount
            );
        }
    } else {
        L2_WRITE_GUARD(*inventory.operator->());
        {
            L2_WRITE_GUARD(*commissionWarehouse.operator->());
            written = inventory->WriteCommission(
                socket, itemPacket, commissionWarehouse.get(), itemId, itemAmount, consumeType, commissionFee, resultingItemId,
                resultingItemType, premiumItemId, premiumItemAmount
            );
        }
    }

    if (!written) {
        // logRegisterFailure()
        logRegisterFailure("item transfer or fee processing failed");
        sendFailure();
        return false;
    }

    static constexpr std::array<int, 6> expirationDurations{86400, 259200, 432000, 604800, 1296000, 2592000};
    int expirationTime = 0;
    if (expiredType >= 0 && expiredType < static_cast<int>(expirationDurations.size())) {
        expirationTime =
            runtime::AddInt32Wrapping(runtime::crt::Time32Now(), expirationDurations[static_cast<std::size_t>(expiredType)]);
    }

    const std::wstring itemName = Utf16ToWide(packetItemName.data());
    const std::wstring characterName = Utf16ToWide(packetCharacterName.data());
    const std::array<wchar_t, 32> registerTime = CurrentCommissionTimestamp();
    std::int64_t commissionId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    const bool registered = ExecuteRegisterCommission(
        connection, commissionId, characterId, itemName.c_str(), itemPrice, commissionType, firstType, commissionFee,
        expiredType, expirationTime, characterName.c_str(), resultingItemId, itemGrade, itemGeneral, registerTime.data(),
        itemClassId
    );
    if (!registered) {
        bool rolledBack = false;
        auto rollbackCommission = [&]() {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[%s],[%d]Commission Register Item RollBack "
            //     L"CommissionID[%I64d]",
            //     L"ServerPacketEx.cpp", 0x2e1c, static_cast<long long>(commissionId)
            // )
            // Log.Add(LOG_ERROR,
            //     "[commission] Registration returned no result after item and fee processing for character %d, item %d, quantity %lld. Attempting reverse transfer and fee refund %lld (returned listing ID %lld).",
            //     characterId, itemId, static_cast<long long>(itemAmount), static_cast<long long>(commissionFee), static_cast<long long>(commissionId))
            L2_LOG_ERROR(Log,
                "[commission] Registration returned no result after item and fee processing for character %d, item %d, quantity %lld; attempting reverse transfer and fee refund %lld (returned listing ID %lld).",
                characterId, itemId, static_cast<long long>(itemAmount), static_cast<long long>(commissionFee), static_cast<long long>(commissionId));
            return commissionWarehouse->WriteCommission(
                socket, itemPacket, inventory.get(), itemId, itemAmount, consumeType, -commissionFee, resultingItemId,
                resultingItemType, premiumItemId, -premiumItemAmount
            );
        };
        if (inventoryLock == commissionLock) {
            // Log.AddT(LOG_ERROR, L"Same warehouse double writelock?-_-,")
            // Log.Add(LOG_WARN,
            //     "[commission] Registration recovery for character %d, item %d uses the same inventory and commission warehouse. Continuing under one lock.",
            //     characterId, itemId)
            L2_LOG_WARN(Log,
                "[commission] Registration recovery for character %d, item %d uses the same inventory and commission warehouse; continuing under one lock.",
                characterId, itemId);
            {
                L2_WRITE_GUARD(*commissionWarehouse.operator->());
                rolledBack = rollbackCommission();
            }
        } else if (commissionLock < inventoryLock) {
            L2_WRITE_GUARD(*commissionWarehouse.operator->());
            {
                L2_WRITE_GUARD(*inventory.operator->());
                rolledBack = rollbackCommission();
            }
        } else {
            L2_WRITE_GUARD(*inventory.operator->());
            {
                L2_WRITE_GUARD(*commissionWarehouse.operator->());
                rolledBack = rollbackCommission();
            }
        }
        if (!rolledBack) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[%s],[%d]Commission Register Item RollBack Fail!! "
            //     L"CommissionID[%I64d], UserDBID[%d], ItemDBID[%d], "
            //     L"CommissionFee[%I64d]",
            //     L"ServerPacketEx.cpp", 0x2e20, static_cast<long long>(commissionId), characterId, itemId,
            //     static_cast<long long>(commissionFee)
            // )
            // Log.Add(LOG_ERROR,
            //     "[commission] Registration recovery failed for character %d, item %d, quantity %lld, fee %lld (returned listing ID %lld). Item and fee state require reconciliation.",
            //     characterId, itemId, static_cast<long long>(itemAmount), static_cast<long long>(commissionFee), static_cast<long long>(commissionId))
            L2_LOG_ERROR(Log,
                "[commission] Registration recovery failed for character %d, item %d, quantity %lld, fee %lld (returned listing ID %lld); item and fee state require reconciliation.",
                characterId, itemId, static_cast<long long>(itemAmount), static_cast<long long>(commissionFee), static_cast<long long>(commissionId));
        }
        // Log.AddT(LOG_ERROR, L"[%s],[%d]Commission Register DB Fail!!", L"ServerPacketEx.cpp", 0x2e23)
        // Log.Add(LOG_ERROR,
        //     "[commission] Registration failed to return a database result for character %d, item %d (returned listing ID %lld). Reverse-transfer processing %s. Sending failure reply.",
        //     characterId, itemId, static_cast<long long>(commissionId), rolledBack ? "completed" : "failed")
        L2_LOG_ERROR(Log,
            "[commission] Registration failed to return a database result for character %d, item %d (returned listing ID %lld); reverse-transfer processing %s. Sending failure reply.",
            characterId, itemId, static_cast<long long>(commissionId), rolledBack ? "completed" : "failed");
        sendFailure();
        return false;
    }

    CLogFormatter formatter;
    formatter.WriteLog(
        0x1f84, user->GetId(), user->GetAccountID(), 0, 0, user->GetX(), user->GetY(), user->GetZ(),
        std::u16string(shapeAndVariationLog.data()).c_str(), std::u16string(attributeLog.data()).c_str(),
        std::u16string(ensoulLog.data()).c_str(), user->GetClass(), user->GetRace(), user->GetGender(), user->GetLevel(),
        commissionId, resultingItemType, resultingItemId, itemPrice, itemAmount, commissionFee,
        std::u16string(user->GetCharName()).c_str(), std::u16string(user->GetAccountName()).c_str(), u"", u"", itemId
    );
    if (premiumItemId != 0) {
        formatter.WriteLog(
            0x1f96, user->GetId(), user->GetAccountID(), 0, 0, user->GetX(), user->GetY(), user->GetZ(), u"", u"", u"",
            user->GetClass(), user->GetRace(), user->GetGender(), user->GetLevel(), commissionId, globalClass, premiumItemId,
            premiumItemAmount, 0, 0, std::u16string(user->GetCharName()).c_str(),
            std::u16string(user->GetAccountName()).c_str(), u"", u"", premiumItemId
        );
    }

    socket->Send(
        "chdQddSQQQdddSdddQb", 0xc6, 0xaa, 1, static_cast<long long>(commissionId), characterId, itemId,
        packetItemName.data(), static_cast<long long>(itemPrice), static_cast<long long>(itemAmount),
        static_cast<long long>(commissionFee), expirationTime, commissionType, expiredType, packetCharacterName.data(),
        firstType, discountType, premiumItemId, static_cast<long long>(premiumItemAmount), itemPacket.GetLength(),
        itemPacket.GetPacket()
    );
    return false;
}

bool RequestCommissionTradeDelete(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00573054.
    L2_GUARD_FUNCTION();

    std::int64_t commissionId = 0;
    int characterId = 0;
    int itemId = 0;
    std::int64_t itemAmount = 0;
    int owner = 0;
    int commissionType = 0;
    ::Disassemble(packet, "QddQdd", &commissionId, &characterId, &itemId, &itemAmount, &owner, &commissionType);

    int expiredType = 0;
    std::array<char16_t, 101> itemName{};
    int firstType = 0;
    int itemClassId = 0;
    DBConn lookup(runtime::process::CurrentDatabaseId());
    lookup.Bind(&commissionType);
    lookup.Bind(&expiredType);
    lookup.Bind(itemName.data(), 200);
    lookup.Bind(&firstType);
    lookup.Bind(&itemClassId);
    constexpr const wchar_t* lookupStatement = L"{Call lin_GetCommissionData (?, ?)}";
    lookup.Prepare(lookupStatement);
    lookup.BindParameter(&commissionId);
    lookup.BindParameter(&characterId);

    auto sendFailure = [&]() {
        socket->Send(
            "chdQddddd", 0xc6, 0xab, -1, static_cast<long long>(commissionId), expiredType, commissionType, characterId,
            firstType, owner
        );
    };
    // auto logDeleteFailure = [&]() {
    auto logDeleteFailure = [&](const char* reason) {
        if (owner == 1) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[%s],[%d]Commission Delete Item Fail!! "
            //     L"CommissionID[%I64d], CharDBID[%d]",
            //     L"ServerPacketEx.cpp", 0x2ea3, static_cast<long long>(commissionId), characterId
            // )
            // Log.Add(LOG_ERROR,
            //     "[commission] Cannot return listing %lld to inventory for character %d (item %d, quantity %lld): %s. Failure reply follows.",
            //     static_cast<long long>(commissionId), characterId, itemId, static_cast<long long>(itemAmount), reason)
            L2_LOG_ERROR(Log,
                "[commission] Cannot return listing %lld to inventory for character %d (item %d, quantity %lld): %s; failure reply follows.",
                static_cast<long long>(commissionId), characterId, itemId, static_cast<long long>(itemAmount), reason);
        } else {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[%s],[%d]Commission Delete Item Fail-Timer!! "
            //     L"CommissionID[%I64d]",
            //     L"ServerPacketEx.cpp", 0x2f20, static_cast<long long>(commissionId)
            // )
            // Log.Add(LOG_ERROR,
            //     "[commission] Cannot return listing %lld to mail storage for character %d (item %d, quantity %lld, owner mode %d): %s. Failure reply follows.",
            //     static_cast<long long>(commissionId), characterId, itemId, static_cast<long long>(itemAmount), owner, reason)
            L2_LOG_ERROR(Log,
                "[commission] Cannot return listing %lld to mail storage for character %d (item %d, quantity %lld, owner mode %d): %s; failure reply follows.",
                static_cast<long long>(commissionId), characterId, itemId, static_cast<long long>(itemAmount), owner, reason);
        }
    };
    if (!lookup.ExecutePrepared(lookupStatement) || !lookup.Fetch()) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"DeleteCommission GetData Fail!!. CommissionID[%I64d] "
        //     L"CharDBID[%d].",
        //     static_cast<long long>(commissionId), characterId
        // )
        // Log.Add(LOG_ERROR,
        //     "[commission] Cannot return listing %lld for character %d: database lookup returned no row (execution or fetch failed). Failure reply follows.",
        //     static_cast<long long>(commissionId), characterId)
        L2_LOG_ERROR(Log,
            "[commission] Cannot return listing %lld for character %d: database lookup returned no row (execution or fetch failed); failure reply follows.",
            static_cast<long long>(commissionId), characterId);
        sendFailure();
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    CUserSP auditUser = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user || !auditUser) {
        // Log.AddT(LOG_ERROR, L"DeleteCommission User valid!!. CharDBID[%d].", characterId)
        // Log.Add(LOG_ERROR,
        //     "[commission] Cannot return listing %lld for character %d: %s could not be loaded. Failure reply follows.",
        //     static_cast<long long>(commissionId), characterId, !user ? "destination character" : "audit character")
        L2_LOG_ERROR(Log,
            "[commission] Cannot return listing %lld for character %d: %s could not be loaded; failure reply follows.",
            static_cast<long long>(commissionId), characterId, !user ? "destination character" : "audit character");
        sendFailure();
        return false;
    }

    CWareHouseSP destinationWarehouse = user->GetWareHouse(owner == 1 ? WareHouseType_Inventory : WareHouseType_Manor, true);
    CWareHouseSP commissionWarehouse = auditUser->GetWareHouse(WareHouseType_Mail, true);
    if (!destinationWarehouse || !commissionWarehouse) {
        // Noise: The following return failure identifies the unavailable warehouse and listing.
        // Log.AddT(LOG_ERROR, L"Error, Invalid sp.")
        // logDeleteFailure()
        logDeleteFailure(!destinationWarehouse ? "destination warehouse is unavailable" : "commission warehouse is unavailable");
        sendFailure();
        return false;
    }

    // The current handler reads the source item and keeps its audit
    // fields before DeleteCommissionInven/DeleteCommissionPost mutates the
    // warehouses. The later post-item audit reads the moved
    // item again and is therefore kept separate below.
    CItemSP originalItem = commissionWarehouse->GetItem(itemId);
    std::array<char16_t, 256> originalShapeLog{};
    std::array<char16_t, 256> originalAttributeLog{};
    std::array<char16_t, 256> originalEnsoulLog{};
    std::int64_t originalItemAmount = 0;
    if (!originalItem) {
        // Log.AddT(
        //     LOG_ERROR,
        //     owner == 1 ? L"[LOG_DELETE_COMMISSION_USER] Item is invalid. %d"
        //                : L"[LOG_DELETE_COMMISSION_TIMER] Item is invalid. %d",
        //     itemId
        // )
        // Log.Add(LOG_ERROR,
        //     "[commission] Cannot read return audit attributes for listing %lld, character %d: source item %d is unavailable. Return processing continues (owner mode %d).",
        //     static_cast<long long>(commissionId), characterId, itemId, owner)
        L2_LOG_ERROR(Log,
            "[commission] Cannot read return audit attributes for listing %lld, character %d: source item %d is unavailable; return processing continues (owner mode %d).",
            static_cast<long long>(commissionId), characterId, itemId, owner);
    } else {
        originalItem->MakeShapeShiftAndVariationLogInfo(originalShapeLog.data());
        BaseAttribute originalAttribute(originalItem->Attribute());
        originalAttribute.GetLogString(originalAttributeLog.data());
        // originalItem->MakeEnsoulLogInfo(originalEnsoulLog.data())
        originalItem->MakeEnsoulLogInfo(originalEnsoulLog.data(), originalEnsoulLog.size());
        originalItemAmount = originalItem->Amount();
    }

    CPacketHelperEx<20480> itemPacket;
    int resultingItemId = 0;
    int enchant = 0;
    bool deleted = false;
    const std::uint64_t destinationLock = destinationWarehouse->GetLockAddr();
    const std::uint64_t commissionLock = commissionWarehouse->GetLockAddr();
    auto deleteUnderOrderedLocks = [&](auto&& deleteItem) {
        if (destinationLock == commissionLock) {
            // Log.AddT(LOG_ERROR, L"Same warehouse double writelock?-_-,")
            // Log.Add(LOG_WARN,
            //     "[commission] Return of listing %lld for character %d uses the same source and destination warehouse (owner mode %d). Continuing under one lock.",
            //     static_cast<long long>(commissionId), characterId, owner)
            L2_LOG_WARN(Log,
                "[commission] Return of listing %lld for character %d uses the same source and destination warehouse (owner mode %d); continuing under one lock.",
                static_cast<long long>(commissionId), characterId, owner);
            bool result;
            {
                L2_WRITE_GUARD(*destinationWarehouse.operator->());
                result = deleteItem();
            }
            return result;
        }
        if (destinationLock < commissionLock) {
            bool result;
            {
                L2_WRITE_GUARD(*destinationWarehouse.operator->());
                L2_WRITE_GUARD(*commissionWarehouse.operator->());
                result = deleteItem();
            }
            return result;
        }
        bool result;
        {
            L2_WRITE_GUARD(*commissionWarehouse.operator->());
            L2_WRITE_GUARD(*destinationWarehouse.operator->());
            result = deleteItem();
        }
        return result;
    };
    if (owner == 1) {
        deleted = deleteUnderOrderedLocks([&]() {
            return commissionWarehouse->DeleteCommissionInven(
                socket, itemPacket, destinationWarehouse.get(), characterId, itemId, commissionId, itemAmount,
                resultingItemId, static_cast<CommissionOwner::Enum>(owner), enchant
            );
        });
    } else {
        deleted = deleteUnderOrderedLocks([&]() {
            return commissionWarehouse->DeleteCommissionPost(
                socket, itemPacket, destinationWarehouse.get(), characterId, itemId, commissionId, itemAmount,
                resultingItemId, static_cast<CommissionOwner::Enum>(owner), enchant
            );
        });
    }
    if (!deleted) {
        // logDeleteFailure()
        logDeleteFailure("item transfer failed");
        sendFailure();
        return false;
    }

    const std::array<wchar_t, 32> deleteTime = CurrentCommissionTimestamp();
    {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_DeleteCommission (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&commissionId);
        connection.BindParameter(&characterId);
        connection.BindParameter(deleteTime.data());
        // connection.ExecutePrepared(statement)
        if (!connection.ExecutePrepared(statement)) {
            // Log.Add(LOG_ERROR,
            //     "[commission] Cannot delete listing %lld from the database after return processing for character %d, item %d (destination %s). Success reply still follows. Listing and item state require reconciliation.",
            //     static_cast<long long>(commissionId), characterId, resultingItemId, owner == 1 ? "inventory" : "mail storage")
            L2_LOG_ERROR(Log,
                "[commission] Cannot delete listing %lld from the database after return processing for character %d, item %d (destination %s); success reply still follows. Listing and item state require reconciliation.",
                static_cast<long long>(commissionId), characterId, resultingItemId, owner == 1 ? "inventory" : "mail storage");
        }
    }

    if (owner == 1) {
        socket->Send(
            "chdQddddddb", 0xc6, 0xab, 1, static_cast<long long>(commissionId), characterId, itemId, expiredType,
            commissionType, owner, firstType, itemPacket.GetLength(), itemPacket.GetPacket()
        );
    } else {
        socket->Send(
            "chdQdddddd", 0xc6, 0xab, 1, static_cast<long long>(commissionId), characterId, itemId, expiredType,
            commissionType, owner, firstType
        );
    }

    const std::u16string originalShapeText = std::u16string(originalShapeLog.data());
    const std::u16string originalAttributeText = std::u16string(originalAttributeLog.data());
    const std::u16string originalEnsoulText = std::u16string(originalEnsoulLog.data());
    const std::u16string auditCharacterName = std::u16string(auditUser->GetCharName());
    const std::u16string auditAccountName = std::u16string(auditUser->GetAccountName());
    CLogFormatter formatter;
    if (owner == 1) {
        formatter.WriteLog(
            0x1f85, auditUser->GetId(), auditUser->GetAccountID(), 0, 0, auditUser->GetX(), auditUser->GetY(),
            auditUser->GetZ(), originalShapeText.c_str(), originalAttributeText.c_str(), originalEnsoulText.c_str(),
            auditUser->GetClass(), auditUser->GetRace(), auditUser->GetGender(), auditUser->GetLevel(), commissionId,
            itemClassId, itemId, originalItemAmount, itemAmount, 0, auditCharacterName.c_str(), auditAccountName.c_str(),
            u"", u"", resultingItemId
        );
    } else {
        formatter.WriteLog(
            0x1f86, 0, 0, 0, 0, 0, 0, 0, originalShapeText.c_str(), originalAttributeText.c_str(),
            originalEnsoulText.c_str(), 0, 0, 0, 0, commissionId, itemClassId, itemId, originalItemAmount, itemAmount, 0,
            u"", u"", u"", u"", resultingItemId
        );
    }

    if (owner == 1)
        return false;

    int postId = 0;
    DBConn postConnection(runtime::process::CurrentDatabaseId());
    if (!ExecuteCreateCommissionPost(
            postConnection, postId, characterId, characterId, u"CommissionDeleteTitle", u"CommissionDeleteContent", 1, 4,
            characterId
        )) {
        // Log.AddT(LOG_ERROR, L"WritePost::Commission Failed. %d.", characterId)
        // Log.Add(LOG_ERROR,
        //     "[commission] Cannot create the return mail for listing %lld, character %d, item %d: database operation returned no result. Item return processing and success reply already completed. Mail attachment was not attempted.",
        //     static_cast<long long>(commissionId), characterId, resultingItemId)
        // L2_LOG_ERROR(Log,
        //     "[commission] Cannot create the return mail for listing %lld, character %d, item %d: database operation returned no result. Item return processing and success reply already completed. Mail attachment was not attempted.",
        //     static_cast<long long>(commissionId), characterId, resultingItemId)
        L2_LOG_ERROR(Log,
            "[commission] Cannot create return mail for listing %lld, character %d, item %d: database execution or result fetch failed. A return success reply was already attempted; mail attachment was not attempted.",
            static_cast<long long>(commissionId), characterId, resultingItemId);
        return false;
    }

    formatter.WriteLog(
        0x1f71, auditUser->GetId(), auditUser->GetAccountID(), auditUser->GetId(), auditUser->GetAccountID(),
        auditUser->GetX(), auditUser->GetY(), auditUser->GetZ(), u"", u"", u"", auditUser->GetClass(), auditUser->GetRace(),
        auditUser->GetGender(), auditUser->GetLevel(), postId, 0, itemAmount, 0, 0, 0, auditCharacterName.c_str(),
        auditAccountName.c_str(), auditCharacterName.c_str(), auditAccountName.c_str(), 0
    );
    {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_CreatePostItemForAdmin (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&resultingItemId);
        connection.BindParameter(&postId);
        connection.BindParameter(&characterId);
        if (!connection.ExecutePrepared(statement)) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[%s][%d] CreatePostItem failed. item id[%d] "
            //     L"mail id[%d] sender id [%d]",
            //     L"ServerPacketEx.cpp", 0x2ef7, resultingItemId, postId, characterId
            // )
            // Log.Add(LOG_ERROR,
            //     "[commission] Cannot attach returned item %d to mail %d for character %d, listing %lld. Return success reply already sent, mail notification still follows.",
            //     resultingItemId, postId, characterId, static_cast<long long>(commissionId))
            // L2_LOG_ERROR(Log,
            //     "[commission] Cannot attach returned item %d to mail %d for character %d, listing %lld. Return success reply already sent, mail notification still follows.",
            //     resultingItemId, postId, characterId, static_cast<long long>(commissionId))
            L2_LOG_ERROR(Log,
                "[commission] Cannot attach returned item %d to mail %d for character %d, listing %lld. A return success reply was already attempted; mail notification will still be attempted.",
                resultingItemId, postId, characterId, static_cast<long long>(commissionId));
        } else {
            CItemSP movedItem = destinationWarehouse->GetItem(resultingItemId);
            if (!movedItem) {
                // Log.AddT(LOG_ERROR, L"[WritePost] Item is invalid. %d", resultingItemId)
                // Log.Add(LOG_ERROR,
                //     "[commission] Returned item %d is unavailable in the destination cache after attachment to mail %d for character %d, listing %lld. Item audit record omitted, mail notification still follows.",
                //     resultingItemId, postId, characterId, static_cast<long long>(commissionId))
                L2_LOG_ERROR(Log,
                    "[commission] Returned item %d is unavailable in the destination cache after attachment to mail %d for character %d, listing %lld; item audit record omitted, mail notification still follows.",
                    resultingItemId, postId, characterId, static_cast<long long>(commissionId));
            } else {
                std::array<char16_t, 256> movedShapeLog{};
                std::array<char16_t, 256> movedAttributeLog{};
                std::array<char16_t, 256> movedEnsoulLog{};
                movedItem->MakeShapeShiftAndVariationLogInfo(movedShapeLog.data());
                BaseAttribute movedAttribute(movedItem->Attribute());
                movedAttribute.GetLogString(movedAttributeLog.data());
                // movedItem->MakeEnsoulLogInfo(movedEnsoulLog.data())
                movedItem->MakeEnsoulLogInfo(movedEnsoulLog.data(), movedEnsoulLog.size());
                const std::u16string movedShapeText = std::u16string(movedShapeLog.data());
                const std::u16string movedAttributeText = std::u16string(movedAttributeLog.data());
                const std::u16string movedEnsoulText = std::u16string(movedEnsoulLog.data());
                formatter.WriteLog(
                    0x1f73, auditUser->GetId(), auditUser->GetAccountID(), auditUser->GetId(), auditUser->GetAccountID(),
                    auditUser->GetX(), auditUser->GetY(), auditUser->GetZ(), movedShapeText.c_str(),
                    movedAttributeText.c_str(), movedEnsoulText.c_str(), auditUser->GetClass(), auditUser->GetRace(),
                    auditUser->GetGender(), auditUser->GetLevel(), resultingItemId, movedItem->ItemType(),
                    movedItem->OwnerID(), movedItem->Amount(), 0, 0, auditCharacterName.c_str(), auditAccountName.c_str(),
                    auditCharacterName.c_str(), auditAccountName.c_str(), resultingItemId
                );
            }
        }
    }
    socket->Send("chddddddddddd", 0xc6, 0x71, 1, postId, characterId, characterId, 0, 0, 4, 0, 0, 0, -1);
    return false;
}

bool RequestCommissionTradeBuyItem(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00575904.
    L2_GUARD_FUNCTION();

    std::int64_t commissionId = 0;
    int buyerId = 0;
    std::int64_t requestedAmount = 0;
    int owner = 0;
    ::Disassemble(packet, "QdQd", &commissionId, &buyerId, &requestedAmount, &owner);

    std::int64_t itemPrice = 0;
    int sellerId = 0;
    int itemId = 0;
    int commissionType = 0;
    int expiredType = 0;
    std::array<char16_t, 101> itemName{};
    int firstType = 0;
    int itemClassId = 0;
    DBConn lookup(runtime::process::CurrentDatabaseId());
    lookup.Bind(&itemPrice);
    lookup.Bind(&sellerId);
    lookup.Bind(&itemId);
    lookup.Bind(&commissionType);
    lookup.Bind(&expiredType);
    lookup.Bind(itemName.data(), 200);
    lookup.Bind(&firstType);
    lookup.Bind(&itemClassId);
    constexpr const wchar_t* lookupStatement = L"{Call lin_GetBuyItemCommission (?)}";
    lookup.Prepare(lookupStatement);
    lookup.BindParameter(&commissionId);

    auto sendFailure = [&]() { socket->Send("chddd", 0xc6, 0xac, -1, buyerId, commissionType); };
    if (!lookup.ExecutePrepared(lookupStatement) || !lookup.Fetch()) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[%s],[%d]Commission Buy Item DB Fail!! "
        //     L"CommissionID[%I64d], BuyerDBID[%d], ItemDBID[%d]",
        //     L"ServerPacketEx.cpp", 0x3059, static_cast<long long>(commissionId), buyerId, itemId
        // )
        // Log.Add(LOG_ERROR,
        //     "[commission] Purchase rejected for listing %lld, buyer %d: database lookup returned no row (execution or fetch failed). Failure reply follows.",
        //     static_cast<long long>(commissionId), buyerId)
        L2_LOG_ERROR(Log,
            "[commission] Purchase rejected for listing %lld, buyer %d: database lookup returned no row (execution or fetch failed); failure reply follows.",
            static_cast<long long>(commissionId), buyerId);
        sendFailure();
        return false;
    }
    // auto sendOperationFailure = [&]() {
    auto sendOperationFailure = [&](const char* reason) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[%s],[%d]Commission Buy Item Fail!! "
        //     L"CommissionID[%I64d], BuyerDBID[%d], SellerDBID[%d]",
        //     L"ServerPacketEx.cpp", 0x3053, static_cast<long long>(commissionId), buyerId, sellerId
        // )
        // Log.Add(LOG_ERROR,
        //     "[commission] Purchase rejected for listing %lld, buyer %d, seller %d, item %d, quantity %lld: %s. Failure reply follows.",
        //     static_cast<long long>(commissionId), buyerId, sellerId, itemId, static_cast<long long>(requestedAmount), reason)
        L2_LOG_ERROR(Log,
            "[commission] Purchase rejected for listing %lld, buyer %d, seller %d, item %d, quantity %lld: %s; failure reply follows.",
            static_cast<long long>(commissionId), buyerId, sellerId, itemId, static_cast<long long>(requestedAmount), reason);
        sendFailure();
    };
    if (buyerId == sellerId) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[%s],[%d]Commission Buy Item Fail!! Buyer==Seller "
        //     L"CommissionID[%I64d], BuyerDBID[%d], ItemDBID[%d]",
        //     L"ServerPacketEx.cpp", 0x2f73, static_cast<long long>(commissionId), buyerId, itemId
        // )
        // Log.Add(LOG_WARN,
        //     "[commission] Purchase rejected for listing %lld: character %d cannot buy their own item %d. Failure reply follows.",
        //     static_cast<long long>(commissionId), buyerId, itemId)
        L2_LOG_WARN(Log,
            "[commission] Purchase rejected for listing %lld: character %d cannot buy their own item %d; failure reply follows.",
            static_cast<long long>(commissionId), buyerId, itemId);
        sendFailure();
        return false;
    }

    int durationMultiplier = 0;
    switch (expiredType) {
    case 0:
        durationMultiplier = 1;
        break;
    case 1:
        durationMultiplier = 3;
        break;
    case 2:
        durationMultiplier = 5;
        break;
    case 3:
    case 4:
    case 5:
        durationMultiplier = 7;
        break;
    default:
        break;
    }

    CUserSP buyer = g_userDb.GetUser(buyerId, true, false, runtime::process::CurrentDatabaseId());
    CUserSP seller = g_userDb.GetUser(sellerId, true, false, runtime::process::CurrentDatabaseId());
    if (!buyer || !seller) {
        // Log.Add(LOG_ERROR,
        //     "[commission] Purchase rejected for listing %lld, buyer %d, seller %d: %s could not be loaded. Failure reply follows.",
        //     static_cast<long long>(commissionId), buyerId, sellerId, !buyer ? "buyer" : "seller")
        L2_LOG_ERROR(Log,
            "[commission] Purchase rejected for listing %lld, buyer %d, seller %d: %s could not be loaded; failure reply follows.",
            static_cast<long long>(commissionId), buyerId, sellerId, !buyer ? "buyer" : "seller");
        sendFailure();
        return false;
    }
    CWareHouseSP buyerInventory = buyer->GetWareHouse(WareHouseType_Inventory, true);
    CWareHouseSP sellerCommission = seller->GetWareHouse(WareHouseType_Mail, true);
    CWareHouseSP sellerPost = seller->GetWareHouse(WareHouseType_Manor, true);
    if (!buyerInventory || !sellerCommission || !sellerPost) {
        // Noise: The following purchase failure identifies the unavailable warehouse and listing.
        // Log.AddT(LOG_ERROR, L"Error, Invalid sp.")
        // sendOperationFailure()
        sendOperationFailure(!buyerInventory ? "buyer inventory is unavailable" : (!sellerCommission ? "seller commission warehouse is unavailable" : "seller mail storage is unavailable"));
        return false;
    }

    CItemSP originalItem = sellerCommission->GetItem(itemId);
    std::array<char16_t, 256> shapeLog{};
    std::array<char16_t, 256> originalAttributeLog{};
    std::array<char16_t, 256> ensoulLog{};
    if (originalItem) {
        originalItem->MakeShapeShiftAndVariationLogInfo(shapeLog.data());
        BaseAttribute attribute(originalItem->Attribute());
        attribute.GetLogString(originalAttributeLog.data());
        // originalItem->MakeEnsoulLogInfo(ensoulLog.data())
        originalItem->MakeEnsoulLogInfo(ensoulLog.data(), ensoulLog.size());
    } else {
        // Log.AddT(LOG_ERROR, L"[LOG_BUY_COMMISSION] Item is invalid. %d", itemId)
        // Log.Add(LOG_ERROR,
        //     "[commission] Cannot read purchase, sale and fee audit attributes for listing %lld, seller %d: source item %d is unavailable. Payment and item-transfer processing continue for buyer %d.",
        //     static_cast<long long>(commissionId), sellerId, itemId, buyerId)
        L2_LOG_ERROR(Log,
            "[commission] Cannot read purchase, sale and fee audit attributes for listing %lld, seller %d: source item %d is unavailable; payment and item-transfer processing continue for buyer %d.",
            static_cast<long long>(commissionId), sellerId, itemId, buyerId);
        // Noise: The preceding diagnostic covers the same missing source item and all three affected audit records.
        // Log.AddT(LOG_ERROR, L"[LOG_SELL_COMMISSION] Item is invalid. %d", itemId)
        // Noise: The preceding diagnostic covers the same missing source item and all three affected audit records.
        // Log.AddT(LOG_ERROR, L"[LOG_SELL_COMMISSION_FEE] Item is invalid. %d", itemId)
    }

    const std::int64_t totalPrice = runtime::MultiplyInt64Wrapping(itemPrice, requestedAmount);
    int resultingAdenaItemId = 0;
    bool transferSellerMoney = true;
    bool moneyTransferred = false;
    const std::uint64_t buyerLock = buyerInventory->GetLockAddr();
    const std::uint64_t sellerPostLock = sellerPost->GetLockAddr();
    auto transferMoney = [&]() {
        return buyerInventory->BuyCommissionMoney(
            socket, sellerPost.get(), totalPrice, durationMultiplier, resultingAdenaItemId, transferSellerMoney, 1.0
        );
    };
    if (buyerLock == sellerPostLock) {
        {
            L2_WRITE_GUARD(*sellerPost.operator->());
            moneyTransferred = transferMoney();
        }
    } else if (buyerLock < sellerPostLock) {
        L2_WRITE_GUARD(*buyerInventory.operator->());
        {
            L2_WRITE_GUARD(*sellerPost.operator->());
            moneyTransferred = transferMoney();
        }
    } else {
        L2_WRITE_GUARD(*sellerPost.operator->());
        {
            L2_WRITE_GUARD(*buyerInventory.operator->());
            moneyTransferred = transferMoney();
        }
    }
    if (!moneyTransferred) {
        // sendOperationFailure()
        sendOperationFailure("payment processing failed before item transfer");
        return false;
    }

    CPacketHelperEx<20480> itemPacket;
    int enchant = 0;
    std::array<int, ATTRT_MAX> attributes{};
    std::array<char16_t, 256> attributeLog{};
    bool itemTransferred = false;
    const std::uint64_t sellerCommissionLock = sellerCommission->GetLockAddr();
    auto transferItem = [&]() {
        return sellerCommission->BuyCommissionItem(
            socket, itemPacket, buyerInventory.get(), itemId, requestedAmount, itemClassId, enchant, attributes.data(),
            attributeLog.data()
        );
    };
    if (buyerLock == sellerCommissionLock) {
        {
            L2_WRITE_GUARD(*buyerInventory.operator->());
            itemTransferred = transferItem();
        }
    } else if (buyerLock < sellerCommissionLock) {
        L2_WRITE_GUARD(*buyerInventory.operator->());
        {
            L2_WRITE_GUARD(*sellerCommission.operator->());
            itemTransferred = transferItem();
        }
    } else {
        L2_WRITE_GUARD(*sellerCommission.operator->());
        {
            L2_WRITE_GUARD(*buyerInventory.operator->());
            itemTransferred = transferItem();
        }
    }

    if (!itemTransferred) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[%s],[%d]Commission Buy Item RollBack "
        //     L"CommissionID[%I64d]",
        //     L"ServerPacketEx.cpp", 0x2fd9, static_cast<long long>(commissionId)
        // )
        // Log.Add(LOG_ERROR,
        //     "[commission] Item transfer failed for listing %lld after payment processing (buyer %d, seller %d, item %d, quantity %lld). Attempting refund %lld from payment item %d.",
        //     static_cast<long long>(commissionId), buyerId, sellerId, itemId, static_cast<long long>(requestedAmount), static_cast<long long>(totalPrice), resultingAdenaItemId)
        L2_LOG_ERROR(Log,
            "[commission] Item transfer failed for listing %lld after payment processing (buyer %d, seller %d, item %d, quantity %lld); attempting refund %lld from payment item %d.",
            static_cast<long long>(commissionId), buyerId, sellerId, itemId, static_cast<long long>(requestedAmount), static_cast<long long>(totalPrice), resultingAdenaItemId);
        bool rolledBack = false;
        auto rollbackMoney = [&]() {
            return sellerPost->BuyCommissionRollBackMoney(socket, buyerInventory.get(), totalPrice, resultingAdenaItemId);
        };
        if (buyerLock == sellerPostLock) {
            {
                L2_WRITE_GUARD(*buyerInventory.operator->());
                rolledBack = rollbackMoney();
            }
        } else if (buyerLock < sellerPostLock) {
            L2_WRITE_GUARD(*buyerInventory.operator->());
            {
                L2_WRITE_GUARD(*sellerPost.operator->());
                rolledBack = rollbackMoney();
            }
        } else {
            L2_WRITE_GUARD(*sellerPost.operator->());
            {
                L2_WRITE_GUARD(*buyerInventory.operator->());
                rolledBack = rollbackMoney();
            }
        }
        if (rolledBack) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[%s],[%d]Commission Buy Item RollBack OK!! "
            //     L"CommissionID[%I64d], BuyerDBID[%d], SellerDBID[%d]",
            //     L"ServerPacketEx.cpp", 0x2fe2, static_cast<long long>(commissionId), buyerId, sellerId
            // )
            // Log.Add(LOG_NORMAL,
            //     "[commission] Refund processing completed for listing %lld, buyer %d, seller %d, requested refund %lld. Purchase remains rejected and failure reply follows.",
            //     static_cast<long long>(commissionId), buyerId, sellerId, static_cast<long long>(totalPrice))
            L2_LOG_INFO(Log,
                "[commission] Refund processing completed for listing %lld, buyer %d, seller %d, requested refund %lld; purchase remains rejected and failure reply follows.",
                static_cast<long long>(commissionId), buyerId, sellerId, static_cast<long long>(totalPrice));
            sendFailure();
        } else {
            // sendOperationFailure()
            sendOperationFailure("item transfer and payment refund both failed; money and item state require reconciliation");
        }
        return false;
    }

    const std::array<wchar_t, 32> deleteTime = CurrentCommissionTimestamp();
    {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_DeleteCommission (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&commissionId);
        connection.BindParameter(&buyerId);
        connection.BindParameter(deleteTime.data());
        // connection.ExecutePrepared(statement)
        if (!connection.ExecutePrepared(statement)) {
            // Log.Add(LOG_ERROR,
            //     "[commission] Cannot delete purchased listing %lld from the database after payment and item processing (buyer %d, seller %d, item %d). Success reply still follows. Listing and item state require reconciliation.",
            //     static_cast<long long>(commissionId), buyerId, sellerId, itemId)
            L2_LOG_ERROR(Log,
                "[commission] Cannot delete purchased listing %lld from the database after payment and item processing (buyer %d, seller %d, item %d); success reply still follows. Listing and item state require reconciliation.",
                static_cast<long long>(commissionId), buyerId, sellerId, itemId);
        }
    }

    socket->Send(
        "chdQddddQQdb", 0xc6, 0xac, 1, static_cast<long long>(commissionId), commissionType, expiredType, buyerId, sellerId,
        static_cast<long long>(totalPrice), static_cast<long long>(requestedAmount), firstType, itemPacket.GetLength(),
        itemPacket.GetPacket()
    );

    const std::u16string shapeText = std::u16string(shapeLog.data());
    const std::u16string originalAttributeText = std::u16string(originalAttributeLog.data());
    const std::u16string ensoulText = std::u16string(ensoulLog.data());
    const std::u16string buyerName = std::u16string(buyer->GetCharName());
    const std::u16string buyerAccount = std::u16string(buyer->GetAccountName());
    const std::u16string sellerName = std::u16string(seller->GetCharName());
    const std::u16string sellerAccount = std::u16string(seller->GetAccountName());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x1f87, buyer->GetId(), buyer->GetAccountID(), seller->GetId(), seller->GetAccountID(), buyer->GetX(), buyer->GetY(),
        buyer->GetZ(), shapeText.c_str(), ensoulText.c_str(), originalAttributeText.c_str(), buyer->GetClass(),
        buyer->GetRace(), buyer->GetGender(), buyer->GetLevel(), commissionId, itemClassId, enchant, totalPrice,
        requestedAmount, firstType, buyerName.c_str(), buyerAccount.c_str(), sellerName.c_str(), sellerAccount.c_str(),
        itemId
    );
    formatter.WriteLog(
        0x1f8e, seller->GetId(), seller->GetAccountID(), buyer->GetId(), buyer->GetAccountID(), seller->GetX(),
        seller->GetY(), seller->GetZ(), shapeText.c_str(), ensoulText.c_str(), originalAttributeText.c_str(),
        seller->GetClass(), seller->GetRace(), seller->GetGender(), seller->GetLevel(), commissionId, itemClassId, enchant,
        totalPrice, requestedAmount, firstType, sellerName.c_str(), sellerAccount.c_str(), buyerName.c_str(),
        buyerAccount.c_str(), itemId
    );
    std::int64_t commissionCharge =
        static_cast<std::int64_t>(static_cast<double>(durationMultiplier) * 0.005 * static_cast<double>(totalPrice));
    if (commissionCharge < 10000)
        commissionCharge = 1000;
    const std::int64_t priceAfterCharge = runtime::SubtractInt64Wrapping(totalPrice, commissionCharge);
    const std::int64_t commissionAuditAmount = priceAfterCharge > 0 ? commissionCharge : totalPrice;
    formatter.WriteLog(
        0x1f9c, seller->GetId(), seller->GetAccountID(), buyer->GetId(), buyer->GetAccountID(), seller->GetX(),
        seller->GetY(), seller->GetZ(), shapeText.c_str(), ensoulText.c_str(), originalAttributeText.c_str(),
        seller->GetClass(), seller->GetRace(), seller->GetGender(), seller->GetLevel(), commissionId, itemClassId, enchant,
        commissionAuditAmount, requestedAmount, firstType, sellerName.c_str(), sellerAccount.c_str(), buyerName.c_str(),
        buyerAccount.c_str(), itemId
    );

    int postId = 0;
    DBConn postConnection(runtime::process::CurrentDatabaseId());
    const int postItemCount = transferSellerMoney ? 1 : 0;
    if (!ExecuteCreateCommissionPost(
            postConnection, postId, sellerId, sellerId, u"CommissionBuyTitle", itemName.data(), postItemCount, 5, sellerId
        )) {
        // Log.AddT(LOG_ERROR, L"WritePost::Commission2 Failed. %d to %d.", sellerId, sellerId, sellerId)
        // Log.Add(LOG_ERROR,
        //     "[commission] Cannot create sale mail for listing %lld, seller %d, buyer %d: database operation returned no result (payment item %d, attachment count %d). Purchase success reply already sent. Payment attachment was not attempted.",
        //     static_cast<long long>(commissionId), sellerId, buyerId, resultingAdenaItemId, postItemCount)
        // L2_LOG_ERROR(Log,
        //     "[commission] Cannot create sale mail for listing %lld, seller %d, buyer %d: database operation returned no result (payment item %d, attachment count %d). Purchase success reply already sent. Payment attachment was not attempted.",
        //     static_cast<long long>(commissionId), sellerId, buyerId, resultingAdenaItemId, postItemCount)
        L2_LOG_ERROR(Log,
            "[commission] Cannot create sale mail for listing %lld, seller %d, buyer %d: database execution or result fetch failed (payment item %d, attachment count %d). Purchase success reply was attempted; payment attachment was not attempted.",
            static_cast<long long>(commissionId), sellerId, buyerId, resultingAdenaItemId, postItemCount);
        return false;
    }

    formatter.WriteLog(
        0x1f71, seller->GetId(), seller->GetAccountID(), seller->GetId(), seller->GetAccountID(), seller->GetX(),
        seller->GetY(), seller->GetZ(), u"", u"", u"", seller->GetClass(), seller->GetRace(), seller->GetGender(),
        seller->GetLevel(), postId, 0, commissionAuditAmount, 0, 0, 0, sellerName.c_str(), sellerAccount.c_str(),
        sellerName.c_str(), sellerAccount.c_str(), 0
    );
    if (transferSellerMoney) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_CreatePostItemForAdmin (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&resultingAdenaItemId);
        connection.BindParameter(&postId);
        connection.BindParameter(&sellerId);
        if (!connection.ExecutePrepared(statement)) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[%s][%d] CreatePostItem failed. item id[%d] "
            //     L"mail id[%d] sender id [%d]",
            //     L"ServerPacketEx.cpp", 0x3035, resultingAdenaItemId, postId, sellerId
            // )
            // Log.Add(LOG_ERROR,
            //     "[commission] Cannot attach payment item %d to sale mail %d for seller %d, listing %lld. Purchase success reply already sent, mail notification still follows.",
            //     resultingAdenaItemId, postId, sellerId, static_cast<long long>(commissionId))
            // L2_LOG_ERROR(Log,
            //     "[commission] Cannot attach payment item %d to sale mail %d for seller %d, listing %lld. Purchase success reply already sent, mail notification still follows.",
            //     resultingAdenaItemId, postId, sellerId, static_cast<long long>(commissionId))
            L2_LOG_ERROR(Log,
                "[commission] Cannot attach payment item %d to sale mail %d for seller %d, listing %lld. Purchase success reply was attempted; mail notification will still be attempted.",
                resultingAdenaItemId, postId, sellerId, static_cast<long long>(commissionId));
        } else {
            std::int64_t sellerPostAmount = commissionCharge;
            if (priceAfterCharge > 0) {
                sellerPostAmount = priceAfterCharge;
            }
            formatter.WriteLog(
                0x1f73, seller->GetId(), seller->GetAccountID(), seller->GetId(), seller->GetAccountID(), seller->GetX(),
                seller->GetY(), seller->GetZ(), u"", u"", u"", seller->GetClass(), seller->GetRace(), seller->GetGender(),
                seller->GetLevel(), resultingAdenaItemId, 57, 0, sellerPostAmount, 0, 0, sellerName.c_str(),
                sellerAccount.c_str(), sellerName.c_str(), sellerAccount.c_str(), resultingAdenaItemId
            );
        }
    }
    socket->Send("chddddddddddd", 0xc6, 0x71, 1, postId, sellerId, sellerId, 0, 0, 5, 0, 0, 0, -1);

    DBConn commissionDataConnection(runtime::process::CurrentDatabaseId());
    if (!ExecuteRegisterPostCommissionItemData(commissionDataConnection, postId, itemClassId, enchant, attributes)) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[%s][%d] RegisterPostCommissionItemData failed. "
        //     L"mail id[%d]",
        //     L"ServerPacketEx.cpp", 0x3048, postId
        // )
        // Log.Add(LOG_ERROR,
        //     "[commission] Cannot save sold-item details for mail %d, listing %lld, seller %d (item class %d, enchant %d). Purchase reply and mail notification already sent.",
        //     postId, static_cast<long long>(commissionId), sellerId, itemClassId, enchant)
        // L2_LOG_ERROR(Log,
        //     "[commission] Cannot save sold-item details for mail %d, listing %lld, seller %d (item class %d, enchant %d). Purchase reply and mail notification already sent.",
        //     postId, static_cast<long long>(commissionId), sellerId, itemClassId, enchant)
        L2_LOG_ERROR(Log,
            "[commission] Cannot save sold-item details for mail %d, listing %lld, seller %d (item class %d, enchant %d). Purchase reply and mail notification were already attempted; delivery is not confirmed.",
            postId, static_cast<long long>(commissionId), sellerId, itemClassId, enchant);
    }
    return false;
}

bool RequestCheckCharName(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0057c1e4.
    L2_GUARD_FUNCTION();

    int requestId = 0;
    int accountId = 0;
    std::array<char16_t, 25> packetCharacterName{};
    ::Disassemble(packet, "ddS", &requestId, &accountId, 0x32, packetCharacterName.data());
    const std::wstring characterName = Utf16ToWide(packetCharacterName.data());

    auto sendResult = [socket, requestId](int result) { socket->Send("chdd", 0xc6, 0xb1, requestId, result); };
    if (!CUser::CheckCharacterName(characterName.c_str(), GetCacheDConfiguration().country, false)) {
        sendResult(4);
        return false;
    }

    int databaseResult = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&databaseResult);
    constexpr const wchar_t* statement = L"{Call lin_CheckCharName  (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(characterName.c_str());
    connection.BindParameter(&accountId);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
        sendResult(0);
        return false;
    }

    if (databaseResult == 1) {
        sendResult(-1);
    } else if (databaseResult == -5 || databaseResult == -4) {
        sendResult(2);
    } else if (databaseResult >= -3 && databaseResult <= -1) {
        sendResult(4);
    }
    return false;
}

bool RequestSaveUserJobGroup(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0057c4b8.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int jobGroup = -1;
    ::Disassemble(packet, "dd", &characterId, &jobGroup);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user || jobGroup == -1) {
        // Log.AddT(LOG_ERROR, L"[%d] User type invalid [%d]!", characterId, jobGroup)
        // L2_LOG_ERROR(Log, L"[%d] User type invalid [%d]!", characterId, jobGroup)
        L2_LOG_WARN(Log,
            "[character] Rejected job-group save for character %d: character is %s, requested group %d "
            "(-1 is invalid). No changes applied.", characterId, user ? "available" : "unavailable", jobGroup
        );
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveUserJobGroup (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&jobGroup);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR, L"[%s][%d] Fail to save user [%d] job group [%d]", L"ServerPacketEx.cpp", 0x3484, characterId,
        //     jobGroup
        // )
        // L2_LOG_ERROR(Log, L"[%s][%d] Fail to save user [%d] job group [%d]", L"ServerPacketEx.cpp", 0x3484, characterId,
        //     jobGroup
        // )
        L2_LOG_ERROR(Log,
            "[character] Database request to save job group %d for character %d failed. Cached group is unchanged.",
            jobGroup, characterId
        );
    } else {
        // The target writes CUser+0x62c directly after SQL success. There is
        // no separately named setter in the current image or PDB.
        user->m_loadedInfo.jobGroup = jobGroup;
    }
    return false;
}

bool RequestSaveCharExtraInfo(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0057c750.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    std::array<double, 16> combatParameters{};
    std::array<int, 8> stats{};
    const unsigned char* cursor =
        ::Disassemble(packet, "dbb", &characterId, 0x80, combatParameters.data(), 0x20, stats.data());
    BaseAttribute attribute;
    cursor = CItem::DisassembleAttribute(cursor, attribute);
    char socialClassByte = 0;
    double carryWeight = 0.0;
    int subjobId = 0;
    ::Disassemble(cursor, "cfd", &socialClassByte, &carryWeight, &subjobId);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user || user->GetSubjobId() != subjobId) {
        return false;
    }

    // The raw blocks are CSharedCreatureData::m_dParam[16] and
    // CStats::m_N[8]. Named current/donor accessors and the current SQL
    // helper fix every selected index and all three scaling operations.
    // MSVC wrapper 0x0054e018 only homes these 31 arguments before calling
    // statement binder 0x0054cecc. The portable direct binds keep the same
    // value order without reproducing that ABI-only frame.
    const int physicalAttack = runtime::Win64CvttDoubleToInt32(combatParameters[2]);
    const int physicalDefense = runtime::Win64CvttDoubleToInt32(combatParameters[0]);
    const int physicalHit = runtime::Win64CvttDoubleToInt32(combatParameters[7]);
    const int physicalAvoid = runtime::Win64CvttDoubleToInt32(combatParameters[14]);
    const int physicalCritical = runtime::Win64CvttDoubleToInt32(combatParameters[9] * 10.0 + 0.5);
    const int physicalAttackSpeed = runtime::Win64CvttDoubleToInt32(combatParameters[4]);
    const int magicAttack = runtime::Win64CvttDoubleToInt32(combatParameters[3]);
    const int magicDefense = runtime::Win64CvttDoubleToInt32(combatParameters[1]);
    const int magicHit = runtime::Win64CvttDoubleToInt32(combatParameters[8]);
    const int magicAvoid = runtime::Win64CvttDoubleToInt32(combatParameters[15]);
    const int magicCritical = runtime::Win64CvttDoubleToInt32(combatParameters[10] * 10.0 + 0.5);
    const int magicAttackSpeed = runtime::Win64CvttDoubleToInt32(combatParameters[5] * 333.0);
    const int speed = runtime::Win64CvttDoubleToInt32(combatParameters[11]);
    const int statStrength = stats[1];
    const int statDexterity = stats[4];
    const int statConstitution = stats[2];
    const int statIntelligence = stats[0];
    const int statWit = stats[5];
    const int statMentality = stats[3];
    const int socialClass = static_cast<int>(socialClassByte);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveCharExtraInfo (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
                                         L"?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
                                         L"?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&physicalAttack);
    connection.BindParameter(&physicalDefense);
    connection.BindParameter(&physicalHit);
    connection.BindParameter(&physicalAvoid);
    connection.BindParameter(&physicalCritical);
    connection.BindParameter(&physicalAttackSpeed);
    connection.BindParameter(&magicAttack);
    connection.BindParameter(&magicDefense);
    connection.BindParameter(&magicHit);
    connection.BindParameter(&magicAvoid);
    connection.BindParameter(&magicCritical);
    connection.BindParameter(&magicAttackSpeed);
    connection.BindParameter(&speed);
    connection.BindParameter(&statStrength);
    connection.BindParameter(&statDexterity);
    connection.BindParameter(&statConstitution);
    connection.BindParameter(&statIntelligence);
    connection.BindParameter(&statWit);
    connection.BindParameter(&statMentality);
    connection.BindParameter(&attribute.m_nAttackType);
    connection.BindParameter(&attribute.m_nAttackValue);
    for (const short& defense : attribute.m_nDefendValue) {
        connection.BindParameter(&defense);
    }
    connection.BindParameter(&socialClass);
    connection.BindParameter(&carryWeight);
    connection.BindParameter(&subjobId);
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestChangeAttributeItem(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0053e4bc. The outer owner strips only the
    // character database ID. CWareHouse consumes the remaining eight fields.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    const unsigned char* warehousePacket = ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());

    auto sendMissingOwnerReply = [&] {
        int userServerId = 0;
        int sourceItemDatabaseId = 0;
        int sourceItemServerId = 0;
        int sourceConsumeType = 0;
        int targetItemDatabaseId = 0;
        int targetItemServerId = 0;
        int targetConsumeType = 0;
        int attributeType = -1;
        ::Disassemble(
            warehousePacket, "dddddddd", &userServerId, &sourceItemDatabaseId, &sourceItemServerId, &sourceConsumeType,
            &targetItemDatabaseId, &targetItemServerId, &targetConsumeType, &attributeType
        );
        socket->Send("chdd", 0xc6, 0xb6, 0, userServerId);
    };

    if (!user) {
        // Log.AddT(LOG_ERROR, L"[%s][%d]cannot find user. User id[%d]", L"ServerPacketEx.cpp", 0x34e6, characterId)
        // L2_LOG_ERROR(Log, L"[%s][%d]cannot find user. User id[%d]", L"ServerPacketEx.cpp", 0x34e6, characterId)
        L2_LOG_ERROR(Log,
            "[items] Cannot change item attributes for character %d: character unavailable. Sending failure reply.", characterId
        );
        sendMissingOwnerReply();
        return false;
    }

    CWareHouseSP warehouse = user->GetWareHouse(0, true);
    if (!warehouse) {
        // Log.AddT(LOG_ERROR, L"[%s][%d]cannot find warehouse.", L"ServerPacketEx.cpp", 0x3502)
        // L2_LOG_ERROR(Log, L"[%s][%d]cannot find warehouse.", L"ServerPacketEx.cpp", 0x3502)
        L2_LOG_ERROR(Log,
            "[items] Cannot change item attributes for character %d: inventory unavailable. Sending failure reply.", characterId
        );
        sendMissingOwnerReply();
        return false;
    }

    {
        L2_WRITE_GUARD(*warehouse.operator->());
        warehouse->ChangeAttributeItem(socket, warehousePacket);
    }
    return false;
}

bool RequestDeleteAndSaveSkillList(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0053e890. Both audit layouts and the packed
    // level/sublevel expression are taken from the shipping body.
    L2_GUARD_FUNCTION();
    int characterId = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.AddT(LOG_ERROR, L"DeleteAndSaveSkillList failed. %d", characterId)
        // L2_LOG_ERROR(Log, L"DeleteAndSaveSkillList failed. %d", characterId)
        L2_LOG_ERROR(Log,
            "[skills] Cannot delete and save skill lists for character %d: character unavailable. Changes skipped.", characterId
        );
        return false;
    }

    int deletedSkillCount = 0;
    cursor = ::Disassemble(cursor, "d", &deletedSkillCount);
    for (int index = 0; index < deletedSkillCount; ++index) {
        int skillId = 0;
        cursor = ::Disassemble(cursor, "d", &skillId);
        const int previousPackedLevel = user->GetAcquireSkillLevelSet(skillId);
        user->DeleteAquireSkill(skillId);
        WriteDeletedSkillListAudit(*user.get(), skillId, previousPackedLevel);
    }

    int savedSkillCount = 0;
    cursor = ::Disassemble(cursor, "d", &savedSkillCount);
    for (int index = 0; index < savedSkillCount; ++index) {
        int skillId = 0;
        int skillLevel = 0;
        int skillSubLevel = 0;
        int force = 0;
        cursor = ::Disassemble(cursor, "dddd", &skillId, &skillLevel, &skillSubLevel, &force);
        if (user->SetAquireSkill(skillId, skillLevel, skillSubLevel, force != 0, -1)) {
            const std::uint32_t packedBits =
                (static_cast<std::uint32_t>(skillSubLevel) << 16U) | (static_cast<std::uint32_t>(skillLevel) & 0xffffU);
            WriteSavedSkillListAudit(*user.get(), skillId, static_cast<std::int32_t>(packedBits));
        }
    }

    // Log.AddT(
    //     LOG_ERROR,
    //     L"[%s][%d] CharName [%s] SaveSkillCount [%d] "
    //     L"DeletedSkillCount [%d]",
    //     L"ServerPacketEx.cpp", 0x35a2, user->GetCharName(), savedSkillCount, deletedSkillCount
    // )
    // Noise: unconditional request counts are not errors or confirmed success counts. Per-skill audits remain above.
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] CharName [%s] SaveSkillCount [%d] "
    //     L"DeletedSkillCount [%d]",
    //     L"ServerPacketEx.cpp", 0x35a2, user->GetCharName(), savedSkillCount, deletedSkillCount
    // )
    return false;
}

bool RequestCancelDualClass(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0053f418. Paired L2Server fixes the two distinct
    // routing/database IDs and the third response context field.
    L2_GUARD_FUNCTION();
    int userServerId = 0;
    int characterId = 0;
    int requestContext = 0;
    ::Disassemble(packet, "ddd", &userServerId, &characterId, &requestContext);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    const int result = user ? user->CancelDualClass() : -1;
    socket->Send("chddd", 0xc6, 0xb8, userServerId, requestContext, result);
    return false;
}

bool RequestBrCanBuyShopItem(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0057d078. Paired current L2Server fixes the six
    // base fields and the optional receiver/title/content suffix.
    L2_GUARD_FUNCTION();

    int productId = 0;
    int productAmount = 0;
    int characterId = 0;
    int maximumLimitedCount = 0;
    int limitDay = 0;
    char gift = 0;
    const unsigned char* cursor =
        ::Disassemble(packet, "dddddc", &productId, &productAmount, &characterId, &maximumLimitedCount, &limitDay, &gift);
    CUserSP sender = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!sender) {
        // Log.AddT(LOG_ERROR, L"[%s][%d] GetUser Fail charId(%d)", L"ServerPacketEx.cpp", 0x3601, characterId)
        // L2_LOG_ERROR(Log, L"[%s][%d] GetUser Fail charId(%d)", L"ServerPacketEx.cpp", 0x3601, characterId)
        L2_LOG_ERROR(Log,
            "[shop] Cannot check purchase eligibility for character %d, product %d, amount %d: "
            "character unavailable. No reply will be sent.", characterId, productId, productAmount
        );
        return false;
    }
    DBConn connection(runtime::process::CurrentDatabaseId());
    if (maximumLimitedCount > 0) {
        int buyCount = 0;
        int buyTime = 0;
        connection.Bind(&buyCount);
        connection.Bind(&buyTime);
        const int accountId = sender->GetAccountID();
        constexpr const wchar_t* statement = L"{Call lin_BrGetLimitedShopItemInfo (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&accountId);
        connection.BindParameter(&characterId);
        connection.BindParameter(&productId);
        if (connection.ExecutePrepared(statement)) {
            connection.Fetch();
            if (limitDay > 0 && buyTime > 0) {
                const std::time_t now = std::time(nullptr);
                std::tm boundaryLocal{};
                if (BrLocalTime32(buyTime, boundaryLocal)) {
                    boundaryLocal.tm_mday += limitDay;
                    boundaryLocal.tm_hour = 0;
                    boundaryLocal.tm_min = 0;
                    boundaryLocal.tm_sec = 0;
                    const std::time_t boundary = std::mktime(&boundaryLocal);
                    if (now != boundary && now - boundary >= 0) {
                        buyCount = 0;
                    }
                }
            }
            if (maximumLimitedCount < runtime::AddInt32Wrapping(buyCount, productAmount)) {
                connection.UnBind();
                socket->Send(
                    "chddddc", 0xc6, 0xb9, static_cast<int>(BR_BUY_NUMBER_LIMIT), productId, productAmount, characterId, gift
                );
                return false;
            }
        }
        connection.UnBind();
    }
    std::array<char16_t, 25> receiverNamePacket{};
    std::array<char16_t, 61> titlePacket{};
    std::array<char16_t, 1001> contentsPacket{};
    auto sendGiftResult = [&](BrBuyProductResultType result) {
        socket->Send(
            "chddddcSSS", 0xc6, 0xb9, static_cast<int>(result), productId, productAmount, characterId, gift,
            receiverNamePacket.data(), titlePacket.data(), contentsPacket.data()
        );
    };

    if (gift == 1) {
        ::Disassemble(
            cursor, "SSS", 0x32, receiverNamePacket.data(), 0x7a, titlePacket.data(), 0x7d2, contentsPacket.data()
        );
        CUserSP receiver = g_userDb.GetUser(receiverNamePacket.data(), true);
        if (!receiver) {
            sendGiftResult(BR_BUY_INVALID_RECEIVER);
            return false;
        }

        int receivedPostCount = 0;
        connection.Bind(&receivedPostCount);
        const int receiverId = static_cast<int>(receiver->GetId());
        constexpr const wchar_t* countStatement = L"{Call lin_GetReceivedPostCount (?)}";
        connection.Prepare(countStatement);
        connection.BindParameter(&receiverId);
        if (connection.ExecutePrepared(countStatement) && connection.Fetch() && receivedPostCount > 0xef) {
            sendGiftResult(BR_BUY_RECEIVE_POST_COUNT);
            return false;
        }

        connection.UnBind();
        int blocked = 0;
        connection.Bind(&blocked);
        constexpr const wchar_t* blockedStatement = L"{Call lin_LoadBlocked (?, ?)}";
        connection.Prepare(blockedStatement);
        connection.BindParameter(&receiverId);
        connection.BindParameter(&characterId);
        if (connection.ExecutePrepared(blockedStatement) && connection.Fetch()) {
            sendGiftResult(BR_BUY_BLOCKED);
            return false;
        }
    }

    sendGiftResult(BR_BUY_SUCCESS);
    return false;
}

bool RequestBrSendShopGiftMail(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0057d918. The generated warehouse objects are
    // kept when post creation fails, matching the target.
    L2_GUARD_FUNCTION();

    long long purchaseNumber = 0;
    int senderId = 0;
    int productId = 0;
    int productAmount = 0;
    int itemCount = 0;
    int saveSoldCount = 0;
    const unsigned char* cursor =
        ::Disassemble(packet, "Qddddd", &purchaseNumber, &senderId, &productId, &productAmount, &itemCount, &saveSoldCount);

    struct GiftItem {
        int itemType = 0;
        int itemAmount = 0;
        char consumeType = 0;
    };
    std::vector<GiftItem> requestedItems;
    if (itemCount > 0) {
        requestedItems.reserve(static_cast<std::size_t>(itemCount));
        for (int index = 0; index < itemCount; ++index) {
            GiftItem item;
            cursor = ::Disassemble(cursor, "ddc", &item.itemType, &item.itemAmount, &item.consumeType);
            requestedItems.push_back(item);
        }
    }

    std::array<char16_t, 25> receiverNamePacket{};
    std::array<char16_t, 61> titlePacket{};
    std::array<char16_t, 1001> contentsPacket{};
    ::Disassemble(cursor, "SSS", 0x32, receiverNamePacket.data(), 0x7a, titlePacket.data(), 0x7d2, contentsPacket.data());
    const std::string receiverName = Utf16ToUtf8(receiverNamePacket.data());
    const std::u16string title(titlePacket.data());
    const std::u16string contents(contentsPacket.data());

    int soldCount = 0;
    int postId = 0;
    int receiverId = 0;
    auto sendReply = [&](BrBuyProductResultType result, int replyItemCount) {
        socket->Send(
            "chQddddddddddd", 0xc6, 0xba, purchaseNumber, productId, productAmount, soldCount, static_cast<int>(result),
            postId, senderId, receiverId, 0, 1, 7, replyItemCount
        );
    };

    CUserSP sender = g_userDb.GetUser(senderId, true, false, runtime::process::CurrentDatabaseId());
    if (!sender) {
        // Log.AddT(
        //     LOG_ERROR, L"[%s][%d] SendGiftMail Fail SenderId(%d) Receiver(%s)", L"ServerPacketEx.cpp", 0x368a, senderId,
        //     receiverName.c_str()
        // )
        // L2_LOG_ERROR(Log, L"[%s][%d] SendGiftMail Fail SenderId(%d) Receiver(%s)", L"ServerPacketEx.cpp", 0x368a, senderId,
        //     receiverName.c_str()
        // )
        L2_LOG_ERROR(Log,
            "[mail] Cannot send gift to '%s': sender character %d unavailable. Sending invalid-user reply.",
            receiverName.c_str(), senderId
        );
        sendReply(BR_BUY_INVALID_USER, itemCount);
        return false;
    }

    CUserSP receiver = g_userDb.GetUser(receiverNamePacket.data(), true);
    if (!receiver) {
        // Log.AddT(
        //     LOG_ERROR, L"[%s][%d] SendGiftMail Fail SenderId(%d) Receiver(%s)", L"ServerPacketEx.cpp", 0x3693, senderId,
        //     receiverName.c_str()
        // )
        // L2_LOG_ERROR(Log, L"[%s][%d] SendGiftMail Fail SenderId(%d) Receiver(%s)", L"ServerPacketEx.cpp", 0x3693, senderId,
        //     receiverName.c_str()
        // )
        L2_LOG_ERROR(Log,
            "[mail] Cannot send gift from character %d: recipient '%s' unavailable. Sending invalid-recipient reply.",
            senderId, receiverName.c_str()
        );
        sendReply(BR_BUY_INVALID_RECEIVER, itemCount);
        return false;
    }
    receiverId = static_cast<int>(receiver->GetId());

    DBConn connection(runtime::process::CurrentDatabaseId());
    CWareHouseSP postWarehouse = sender->GetWareHouse(4);
    std::vector<int> createdItemIds;
    for (const GiftItem& requested : requestedItems) {
        if (requested.itemAmount < 1 || productAmount < 1) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[%s][%d] SendGiftMail Fail SenderId(%d) Receiver(%s) "
            //     L"ProductAmount(%d) ItemAmount(%d)",
            //     L"ServerPacketEx.cpp", 0x36d0, senderId, receiverName.c_str(), productAmount, requested.itemAmount
            // )
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] SendGiftMail Fail SenderId(%d) Receiver(%s) "
            //     L"ProductAmount(%d) ItemAmount(%d)",
            //     L"ServerPacketEx.cpp", 0x36d0, senderId, receiverName.c_str(), productAmount, requested.itemAmount
            // )
            L2_LOG_WARN(Log,
                "[mail] Rejected gift from character %d to character %d: product amount %d, item amount %d; "
                "both must be positive. Sending invalid-item reply.", senderId, receiverId, productAmount, requested.itemAmount
            );
            sendReply(BR_BUY_INVALID_ITEM, itemCount);
            return false;
        }

        auto createGiftItem = [&](int amount, [[maybe_unused]] const wchar_t* lockFile, [[maybe_unused]] int lockLine) {
            ItemOptionKey optionKey{};
            BaseAttribute attribute;
            CItemSP item = postWarehouse->MakeNewItem(
                static_cast<unsigned int>(senderId), static_cast<unsigned int>(requested.itemType), amount, 0, 0, 0, 0, 0, 4,
                optionKey, 0, attribute, 0, 0
            );
            {
                L2_WRITE_GUARD(*postWarehouse.operator->());
                if (item) {
                    postWarehouse->PushItem(item.get());
                }
            }
            createdItemIds.push_back(static_cast<int>(item->ItemID()));
        };
        // Current 0x0057d918 has two static copies of the creation path: the
        // non-zero consume type uses source line 0x36b4 once, while consume
        // type zero uses line 0x36c3 once per product. Keep those as two real
        // call sites so __LINE__ continues to distinguish the paths.
        if (requested.consumeType != 0) {
            createGiftItem(runtime::MultiplyInt32Wrapping(productAmount, requested.itemAmount), __WFILE__, __LINE__);
        } else {
            for (int index = 0; index < productAmount; ++index) {
                createGiftItem(requested.itemAmount, __WFILE__, __LINE__);
            }
        }
    }

    const int createdItemCount = static_cast<int>(createdItemIds.size());
    const int trade = 0;
    const int process = 0;
    const int returnable = 1;
    const int senderDeleted = 0;
    const int receiverDeleted = 0;
    const long sentTime = static_cast<long>(runtime::crt::Time32Now());
    const long long tradeMoney = 0;
    const long long postFee = 0;
    const int mailType = 7;
    const int recordedId = senderId;

    connection.UnBind();
    connection.Bind(&postId);
    constexpr const wchar_t* createPostStatement = L"{Call lin_CreatePost (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)}";
    connection.Prepare(createPostStatement);
    connection.BindParameter(&senderId);
    connection.BindParameter(&receiverId);
    connection.BindParameter(&trade);
    connection.BindParameter(title.c_str());
    connection.BindParameter(contents.c_str());
    connection.BindParameter(&process);
    connection.BindParameter(&returnable);
    connection.BindParameter(&senderDeleted);
    connection.BindParameter(&receiverDeleted);
    connection.BindParameter(&sentTime);
    connection.BindParameter(&createdItemCount);
    connection.BindParameter(&tradeMoney);
    connection.BindParameter(&postFee);
    connection.BindParameter(&mailType);
    connection.BindParameter(&recordedId);
    if (!connection.ExecutePrepared(createPostStatement) || !connection.Fetch()) {
        return false;
    }

    WriteBrGiftPostAudit(*sender.get(), *receiver.get(), postId, createdItemCount);
    for (const int itemId : createdItemIds) {
        connection.UnBind();
        constexpr const wchar_t* itemStatement = L"{Call lin_CreatePostItemForAdmin (?, ?, ?)}";
        connection.Prepare(itemStatement);
        connection.BindParameter(&itemId);
        connection.BindParameter(&postId);
        connection.BindParameter(&senderId);
        if (!connection.ExecutePrepared(itemStatement)) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[%s][%d] CreatePostItem failed. item id[%d] "
            //     L"mail id[%d] sender id [%d]",
            //     L"ServerPacketEx.cpp", 0x36ec, itemId, postId, senderId
            // )
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] CreatePostItem failed. item id[%d] "
            //     L"mail id[%d] sender id [%d]",
            //     L"ServerPacketEx.cpp", 0x36ec, itemId, postId, senderId
            // )
            L2_LOG_ERROR(Log,
                "[mail] Database request to attach gift item %d to post %d failed; sender %d. "
                "Remaining attachments will be attempted and the purchase-success reply will still be sent.",
                itemId, postId, senderId
            );
        }
    }

    if (saveSoldCount > 0) {
        connection.UnBind();
        connection.Bind(&soldCount);
        constexpr const wchar_t* soldStatement = L"{Call lin_BrSaveProductSoldCount (?, ?)}";
        connection.Prepare(soldStatement);
        connection.BindParameter(&productId);
        connection.BindParameter(&productAmount);
        if (connection.ExecutePrepared(soldStatement)) {
            connection.Fetch();
        }
    }

    sendReply(BR_BUY_SUCCESS, createdItemCount);
    return false;
}

bool RequestGetDailyCount(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0057ebf8.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int eventId = 0;
    ::Disassemble(packet, "dd", &characterId, &eventId);
    CUserSP user = g_userDb.GetUser(characterId, false, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    int dailyCount = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&dailyCount);
    constexpr const wchar_t* statement = L"{Call lin_GetDailyCount (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&eventId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(LOG_ERROR, L"RequestGetDailyCount() failed. nUserDBID[%d], nEventID[%d]", characterId, eventId)
        // L2_LOG_ERROR(Log, L"RequestGetDailyCount() failed. nUserDBID[%d], nEventID[%d]", characterId, eventId)
        L2_LOG_ERROR(Log,
            "[events] Database request to load daily count for character %d, event %d failed. No reply will be sent.",
            characterId, eventId
        );
        return false;
    }
    connection.Fetch();
    socket->Send("chddd", 0xc6, 0xbb, characterId, eventId, dailyCount);
    return false;
}

bool RequestSetDailyCount(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0057ee5c.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int eventId = 0;
    ::Disassemble(packet, "dd", &characterId, &eventId);
    CUserSP user = g_userDb.GetUser(characterId, false, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    int result = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    constexpr const wchar_t* statement = L"{Call lin_SetDailyCount (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&eventId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(LOG_ERROR, L"RequestSetDailyCount() failed. nUserDBID[%d], nEventID[%d]", characterId, eventId)
        // L2_LOG_ERROR(Log, L"RequestSetDailyCount() failed. nUserDBID[%d], nEventID[%d]", characterId, eventId)
        L2_LOG_ERROR(Log,
            "[events] Database request to set daily count for character %d, event %d failed.", characterId, eventId
        );
        return false;
    }
    connection.Fetch();
    return false;
}

bool RequestClearDailyCount(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0057f090.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int eventId = 0;
    ::Disassemble(packet, "dd", &characterId, &eventId);
    CUserSP user = g_userDb.GetUser(characterId, false, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    int result = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    constexpr const wchar_t* statement = L"{Call lin_ClearDailyCount (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&eventId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(LOG_ERROR, L"RequestClearDailyCount() failed. nUserDBID[%d], nEventID[%d]", characterId, eventId)
        // L2_LOG_ERROR(Log, L"RequestClearDailyCount() failed. nUserDBID[%d], nEventID[%d]", characterId, eventId)
        L2_LOG_ERROR(Log,
            "[events] Database request to clear daily count for character %d, event %d failed.", characterId, eventId
        );
        return false;
    }
    connection.Fetch();
    return false;
}

bool RequestAddMentee(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00570624.
    L2_GUARD_FUNCTION();

    int mentorId = 0;
    int menteeId = 0;
    ::Disassemble(packet, "dd", &mentorId, &menteeId);

    int result = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    const long startTime = static_cast<long>(runtime::crt::Time32Now());
    constexpr const wchar_t* statement = L"{Call lin_AddNewMentee (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mentorId);
    connection.BindParameter(&menteeId);
    connection.BindParameter(&startTime);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        socket->Send("chddd", 0xc6, 0xc8, mentorId, menteeId, result);
    }
    return false;
}

bool RequestLoadMenteeList(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005707fc.
    L2_GUARD_FUNCTION();

    int mentorId = 0;
    ::Disassemble(packet, "d", &mentorId);

    int menteeId = 0;
    std::array<char16_t, 25> menteeName{};
    int characterClass = 0;
    int level = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&menteeId);
    connection.Bind(menteeName.data(), 0x32);
    connection.Bind(&characterClass);
    connection.Bind(&level);
    CPacketHelperEx<20480> rows;
    constexpr const wchar_t* listStatement = L"{Call lin_LoadMenteeList (?)}";
    connection.Prepare(listStatement);
    connection.BindParameter(&mentorId);
    if (connection.ExecutePrepared(listStatement)) {
        while (connection.Fetch()) {
            rows.Assemble("dSdd", menteeId, menteeName.data(), characterClass, level);
        }
        const int rowCount = rows.GetAssembledCount();
        if (rowCount > 0) {
            socket->Send("chddb", 0xc6, 0xbc, mentorId, rowCount, rows.GetLength(), rows.GetPacket());
        }
    }

    connection.UnBind();
    int penaltyStatus = 0;
    int penaltyEndTime = 0;
    connection.Bind(&penaltyStatus);
    connection.Bind(&penaltyEndTime);
    constexpr const wchar_t* penaltyStatement = L"{Call lin_LoadMentorPenalty (?)}";
    connection.Prepare(penaltyStatement);
    connection.BindParameter(&mentorId);
    if (connection.ExecutePrepared(penaltyStatement) && connection.Fetch() && penaltyEndTime > 0) {
        socket->Send("chddd", 0xc6, 0xbe, mentorId, penaltyStatus, penaltyEndTime);
    }
    return false;
}

bool RequestLoadMentor(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00570b28.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);

    int mentorId = 0;
    std::array<char16_t, 25> mentorName{};
    int characterClass = 0;
    int level = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&mentorId);
    connection.Bind(mentorName.data(), 0x32);
    connection.Bind(&characterClass);
    connection.Bind(&level);
    constexpr const wchar_t* statement = L"{Call lin_LoadMentor (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        socket->Send("chddSdd", 0xc6, 0xbd, characterId, mentorId, mentorName.data(), characterClass, level);
    }
    return false;
}

bool RequestUpdateMentorstatus(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0057102c. The lowercase 's' is kept from the
    // current guard/PDB method identity.
    L2_GUARD_FUNCTION();

    int mentorId = 0;
    int menteeId = 0;
    int status = 0;
    int endTime = 0;
    ::Disassemble(packet, "dddd", &mentorId, &menteeId, &status, &endTime);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateMentorStatus (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mentorId);
    connection.BindParameter(&menteeId);
    connection.BindParameter(&status);
    connection.BindParameter(&endTime);
    if (connection.ExecutePrepared(statement)) {
        socket->Send("chddd", 0xc6, 0xc9, mentorId, menteeId, status);
    }
    return false;
}

bool RequestUpdateMenteePlaytime(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00570d30.
    L2_GUARD_FUNCTION();

    int mentorId = 0;
    int menteeId = 0;
    int playtime = 0;
    ::Disassemble(packet, "ddd", &mentorId, &menteeId, &playtime);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateMenteePlaytime (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mentorId);
    connection.BindParameter(&menteeId);
    connection.BindParameter(&playtime);
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestUpdateMentorPlaytime(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00570ec0.
    L2_GUARD_FUNCTION();

    int mentorId = 0;
    int playtime = 0;
    ::Disassemble(packet, "dd", &mentorId, &playtime);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateMentorPlaytime (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mentorId);
    connection.BindParameter(&playtime);
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestResetMentorPenalty(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x005711c8.
    L2_GUARD_FUNCTION();

    int mentorId = 0;
    ::Disassemble(packet, "d", &mentorId);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_ResetMentorPenalty (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mentorId);
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestShapeShiftItem(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0053f5d8. The outer owner removes only the
    // character database ID. CWareHouse decodes the remaining eleven fields.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    const unsigned char* warehousePacket = ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.AddT(
        //     LOG_ERROR, L"[ShapeShifting][%S][%d]cannot find user. User id[%d]", "ServerPacketEx.cpp", 0x377c, characterId
        // )
        // L2_LOG_ERROR(Log, L"[ShapeShifting][%S][%d]cannot find user. User id[%d]", "ServerPacketEx.cpp", 0x377c, characterId
        // )
        L2_LOG_ERROR(Log,
            "[appearance] Cannot change item appearance for character %d: character unavailable. Sending failure reply.",
            characterId
        );
        socket->Send(
            "chdddQddddQd", 0xc6, 0xbf, 0, 0, 0, static_cast<long long>(-1), 0, 0, 0, 0, static_cast<long long>(0), -1
        );
        return false;
    }

    CWareHouseSP warehouse = user->GetWareHouse(0, true);
    if (!warehouse) {
        // Log.AddT(LOG_ERROR, L"[ShapeShifting][%S][%d]cannot find warehouse.", "ServerPacketEx.cpp", 0x3789)
        // L2_LOG_ERROR(Log, L"[ShapeShifting][%S][%d]cannot find warehouse.", "ServerPacketEx.cpp", 0x3789)
        L2_LOG_ERROR(Log,
            "[appearance] Cannot change item appearance for character %d: inventory unavailable. Sending failure reply.",
            characterId
        );
        // This shorter fixed-shaped reply is present in the shipping owner.
        socket->Send("chdddQddQd", 0xc6, 0xbf, 0, 0, 0, static_cast<long long>(-1), 0, 0, static_cast<long long>(0), -1);
        return false;
    }

    {
        L2_WRITE_GUARD(*warehouse.operator->());
        warehouse->ShapeShiftItem(socket, warehousePacket);
    }
    return false;
}

bool RequestShapeShiftItemFixed(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0053f944.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    const unsigned char* warehousePacket = ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.AddT(
        //     LOG_ERROR, L"[ShapeShifting][%S][%d]cannot find user. User id[%d]", "ServerPacketEx.cpp", 0x37b3, characterId
        // )
        // L2_LOG_ERROR(Log, L"[ShapeShifting][%S][%d]cannot find user. User id[%d]", "ServerPacketEx.cpp", 0x37b3, characterId
        // )
        L2_LOG_ERROR(Log,
            "[appearance] Cannot apply fixed item appearance for character %d: character unavailable. Sending failure reply.",
            characterId
        );
        socket->Send("chdddQddQd", 0xc6, 0xc0, 0, 0, 0, static_cast<long long>(-1), 0, 0, static_cast<long long>(0), -1);
        return false;
    }
    CWareHouseSP warehouse = user->GetWareHouse(0, true);
    if (!warehouse) {
        // Log.AddT(LOG_ERROR, L"[ShapeShifting][%S][%d]cannot find warehouse.", "ServerPacketEx.cpp", 0x37c0)
        // L2_LOG_ERROR(Log, L"[ShapeShifting][%S][%d]cannot find warehouse.", "ServerPacketEx.cpp", 0x37c0)
        L2_LOG_ERROR(Log,
            "[appearance] Cannot apply fixed item appearance for character %d: inventory unavailable. Sending failure reply.",
            characterId
        );
        socket->Send("chdddQddQd", 0xc6, 0xc0, 0, 0, 0, static_cast<long long>(-1), 0, 0, static_cast<long long>(0), -1);
        return false;
    }

    {
        L2_WRITE_GUARD(*warehouse.operator->());
        warehouse->ShapeShiftItemFixed(socket, warehousePacket);
    }
    return false;
}

bool RequestRestoreShapeShiftItem(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0053fca8.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    const unsigned char* warehousePacket = ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.AddT(
        //     LOG_ERROR, L"[ShapeShifting][%S][%d]cannot find user. User id[%d]", "ServerPacketEx.cpp", 0x37e8, characterId
        // )
        // L2_LOG_ERROR(Log, L"[ShapeShifting][%S][%d]cannot find user. User id[%d]", "ServerPacketEx.cpp", 0x37e8, characterId
        // )
        L2_LOG_ERROR(Log,
            "[appearance] Cannot restore item appearance for character %d: character unavailable. Sending failure reply.",
            characterId
        );
        socket->Send("chdddQdd", 0xc6, 0xc1, 0, 0, 0, static_cast<long long>(-1), 0, 0);
        return false;
    }
    CWareHouseSP warehouse = user->GetWareHouse(0, true);
    if (!warehouse) {
        // Log.AddT(LOG_ERROR, L"[ShapeShifting][%S][%d]cannot find warehouse.", "ServerPacketEx.cpp", 0x37f5)
        // L2_LOG_ERROR(Log, L"[ShapeShifting][%S][%d]cannot find warehouse.", "ServerPacketEx.cpp", 0x37f5)
        L2_LOG_ERROR(Log,
            "[appearance] Cannot restore item appearance for character %d: inventory unavailable. Sending failure reply.",
            characterId
        );
        socket->Send("chdddQdd", 0xc6, 0xc1, 0, 0, 0, static_cast<long long>(-1), 0, 0);
        return false;
    }

    {
        L2_WRITE_GUARD(*warehouse.operator->());
        warehouse->RestoreShapeShiftItem(socket, warehousePacket);
    }
    return false;
}

bool RequestExpireShapeShiftItem(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0053fff4.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    const unsigned char* warehousePacket = ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.AddT(
        //     LOG_ERROR, L"[ShapeShifting][%S][%d]cannot find user. User id[%d]", "ServerPacketEx.cpp", 0x381b, characterId
        // )
        // L2_LOG_ERROR(Log, L"[ShapeShifting][%S][%d]cannot find user. User id[%d]", "ServerPacketEx.cpp", 0x381b, characterId
        // )
        L2_LOG_ERROR(Log,
            "[appearance] Cannot process expired item appearance for character %d: character unavailable. Sending failure reply.",
            characterId
        );
        socket->Send("chdddd", 0xc6, 0xc2, 0, 0, 0, 0);
        return false;
    }
    CWareHouseSP warehouse = user->GetWareHouse(0, true);
    if (!warehouse) {
        // Log.AddT(LOG_ERROR, L"[ShapeShifting][%S][%d]cannot find warehouse.", "ServerPacketEx.cpp", 0x3828)
        // L2_LOG_ERROR(Log, L"[ShapeShifting][%S][%d]cannot find warehouse.", "ServerPacketEx.cpp", 0x3828)
        L2_LOG_ERROR(Log,
            "[appearance] Cannot process expired item appearance for character %d: inventory unavailable. Sending failure reply.",
            characterId
        );
        socket->Send("chdddd", 0xc6, 0xc2, 0, 0, 0, 0);
        return false;
    }

    {
        L2_WRITE_GUARD(*warehouse.operator->());
        warehouse->ExpireShapeShiftItem(socket, warehousePacket);
    }
    return false;
}

bool RequestSetCastleCampaign(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0057cb50.
    L2_GUARD_FUNCTION();

    int castleId = 0;
    int campaignStatus = 0;
    ::Disassemble(packet, "dd", &castleId, &campaignStatus);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetCastleCampaign (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&castleId);
    connection.BindParameter(&campaignStatus);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR, L"[%s][%d] Fail to save castle[%d] campaign [%d]", L"ServerPacketEx.cpp", 0x3533, castleId,
        //     campaignStatus
        // )
        // L2_LOG_ERROR(Log, L"[%s][%d] Fail to save castle[%d] campaign [%d]", L"ServerPacketEx.cpp", 0x3533, castleId,
        //     campaignStatus
        // )
        L2_LOG_ERROR(Log,
            "[residences] Database request to set campaign status %d for castle %d failed.", campaignStatus, castleId
        );
    }
    return false;
}

bool RequestLoadPledgeGame(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0057f2c4.
    L2_GUARD_FUNCTION();

    int requestedSeason = 0;
    ::Disassemble(packet, "d", &requestedSeason);

    int season = 0;
    int step = 0;
    int seasonStartTime = 0;
    int maxPrizeCount = 0;
    int previousWinnerPledgeId = 0;
    int legacyIntegerColumnSix = 0;
    int previousWinnerCharacterId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&season);
    connection.Bind(&step);
    connection.Bind(&seasonStartTime);
    connection.Bind(&maxPrizeCount);
    connection.Bind(&previousWinnerPledgeId);
    // The current handler binds only seven integer columns even though the
    // existing procedure's sixth result is the previous pledge name. Keep
    // that observable ODBC conversion and omit the later two result columns.
    connection.Bind(&legacyIntegerColumnSix);
    connection.Bind(&previousWinnerCharacterId);
    constexpr const wchar_t* statement = L"{Call lin_GetPledgeGame (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&requestedSeason);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        socket->Send(
            "chddddddd", 0xc6, 0xc3, season, step, seasonStartTime, maxPrizeCount, previousWinnerPledgeId,
            legacyIntegerColumnSix, previousWinnerCharacterId
        );
    }
    return false;
}

bool RequestStartPledgeGame(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0057f4ec.
    L2_GUARD_FUNCTION();

    int season = 0;
    int seasonStartTime = 0;
    ::Disassemble(packet, "dd", &season, &seasonStartTime);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_StartPledgeGame (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&season);
    connection.BindParameter(&seasonStartTime);
    if (connection.ExecutePrepared(statement)) {
        socket->Send("chdd", 0xc6, 0xc4, season, seasonStartTime);
    }
    return false;
}

bool RequestEndPledgeGame(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0057f680.
    L2_GUARD_FUNCTION();

    int season = 0;
    int seasonEndTime = 0;
    int winnerPledgeId = 0;
    int winnerCharacterId = 0;
    int startNextSeason = 0;
    ::Disassemble(packet, "ddddd", &season, &seasonEndTime, &winnerPledgeId, &winnerCharacterId, &startNextSeason);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_EndPledgeGame (?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&season);
    connection.BindParameter(&seasonEndTime);
    connection.BindParameter(&winnerPledgeId);
    connection.BindParameter(&winnerCharacterId);
    connection.BindParameter(&startNextSeason);
    if (connection.ExecutePrepared(statement)) {
        socket->Send("chddddd", 0xc6, 0xc5, season, seasonEndTime, winnerPledgeId, winnerCharacterId, startNextSeason);
    }
    return false;
}

bool RequestUpdatePledgeGameResult(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0057f82c.
    L2_GUARD_FUNCTION();

    int season = 0;
    int characterId = 0;
    int prizeCount = 0;
    ::Disassemble(packet, "ddd", &season, &characterId, &prizeCount);

    int totalPrizeCount = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&totalPrizeCount);
    constexpr const wchar_t* statement = L"{Call lin_UpdatePledgeGameResult (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&season);
    connection.BindParameter(&characterId);
    connection.BindParameter(&prizeCount);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        socket->Send("chdddd", 0xc6, 0xc6, season, characterId, prizeCount, totalPrizeCount);
    }
    return false;
}

bool RequestSetPledgeGameGotItem(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0057fa08.
    L2_GUARD_FUNCTION();

    int season = 0;
    ::Disassemble(packet, "d", &season);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdatePledgeGameGotItem (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&season);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(LOG_ERROR, L"RequestSetPledgeGameGotItem() failed. nSeason[%d]", season)
        // L2_LOG_ERROR(Log, L"RequestSetPledgeGameGotItem() failed. nSeason[%d]", season)
        L2_LOG_ERROR(Log,
            "[clan games] Database request to update item-receipt state for season %d failed.", season
        );
    }
    return false;
}

bool RequestIncreasePledgeGameSurrenderCount(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0057fb7c.
    L2_GUARD_FUNCTION();

    int season = 0;
    int characterId = 0;
    int increaseCount = 0;
    ::Disassemble(packet, "ddd", &season, &characterId, &increaseCount);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    DBConn connection(runtime::process::CurrentDatabaseId());
    {
        L2_WRITE_GUARD(*user.operator->());
        constexpr const wchar_t* statement = L"{Call lin_IncreasePledgeGameSurrenderCount (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&season);
        connection.BindParameter(&characterId);
        connection.BindParameter(&increaseCount);
        if (!connection.ExecutePrepared(statement)) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"RequestIncreasePledgeGameSurrenderCount() failed. "
            //     L"nSeason[%d], nCharDbId[%d]",
            //     season, characterId
            // )
            // L2_LOG_ERROR(Log,
            //     L"RequestIncreasePledgeGameSurrenderCount() failed. "
            //     L"nSeason[%d], nCharDbId[%d]",
            //     season, characterId
            // )
            // Previous: L2_LOG_ERROR(Log, "Cannot increase clan game surrender count by %d for character %d, season %d: database request failed.", increaseCount, characterId, season)
            L2_LOG_ERROR(Log, "[clan game] Cannot increase the surrender count by %d for character %d, season %d: the database request failed.", increaseCount, characterId, season);
        }
    }
    return false;
}

bool GetPledgeGameRecord(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0057fddc.
    L2_GUARD_FUNCTION();

    int season = 0;
    int characterId = 0;
    ::Disassemble(packet, "dd", &season, &characterId);

    int prizeCount = 0;
    int surrenderCount = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&prizeCount);
    connection.Bind(&surrenderCount);
    constexpr const wchar_t* statement = L"{Call lin_GetPledgeGameRecord (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&season);
    connection.BindParameter(&characterId);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        socket->Send("chdddd", 0xc6, 0xc7, season, characterId, prizeCount, surrenderCount);
    }
    return false;
}

bool RequestSetCastleType(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0057ccf4.
    L2_GUARD_FUNCTION();

    int castleId = 0;
    int castleType = 0;
    ::Disassemble(packet, "dd", &castleId, &castleType);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetCastleType (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&castleId);
    connection.BindParameter(&castleType);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR, L"[%s][%d] Fail to save castle[%d] castleType [%d]", L"ServerPacketEx.cpp", 0x3548, castleId,
        //     castleType
        // )
        // L2_LOG_ERROR(Log, L"[%s][%d] Fail to save castle[%d] castleType [%d]", L"ServerPacketEx.cpp", 0x3548, castleId,
        //     castleType
        // )
        L2_LOG_ERROR(Log, "[residences] Database request to set castle %d type to %d failed.", castleId, castleType
        );
    }
    return false;
}

bool RequestDeleteMacroAll(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0057ce98.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP retainedUser = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeleteMacroAll (? )}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    const int success = connection.ExecutePrepared(statement) ? 1 : 0;
    connection.UnBind();
    socket->Send("chdddd", 0xc6, 0xca, characterId, 0, 0, success);
    return false;
}

bool RequestMPCCTelBookMarkPacket(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0057ffb0.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int targetUserServerId = 0;
    std::array<char16_t, 50> targetBookmarkName{};
    ::Disassemble(packet, "ddS", &characterId, &targetUserServerId, 100, targetBookmarkName.data());
    if (targetUserServerId == 0)
        return false;

    int worldId = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    int found = 0;
    const std::u16string bookmarkName(targetBookmarkName.data());
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&worldId);
    connection.Bind(&x);
    connection.Bind(&y);
    connection.Bind(&z);
    constexpr const wchar_t* statement = L"{Call lin_GetBookMark (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(bookmarkName.c_str());
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        found = 1;
    }
    socket->Send("chdddddd", 0xc6, 0xcb, targetUserServerId, found, worldId, x, y, z);
    return false;
}

bool RequestLoadKalieEventPacket(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005801e8. Paired current receiver
    // DBPacketExHandler::ResponseLoadKalieEventEx 0x006724a4.
    L2_GUARD_FUNCTION();

    int eventId = 0;
    ::Disassemble(packet, "d", &eventId);
    int currentState = 0;
    int progress = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&currentState);
    connection.Bind(&progress);
    constexpr const wchar_t* statement = L"{Call lin_LoadKalieEvent (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&eventId);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        socket->Send("chdd", 0xc6, 0xcc, currentState, progress);
    }
    return false;
}

bool RequestLoadKalieEventUserPacket(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0058039c. Paired current receiver
    // DBPacketExHandler::ResponseLoadKalieEventUserEx 0x006a1bcc.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    int remainingToken = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&remainingToken);
    constexpr const wchar_t* statement = L"{Call lin_LoadKalieEventUser (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        if (remainingToken == -1) {
            remainingToken = 0;
        }
        socket->Send("chdd", 0xc6, 0xcd, characterId, remainingToken);
    }
    return false;
}

bool RequestUpdateKalieEventPacket(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00580548.
    L2_GUARD_FUNCTION();

    int eventId = 0;
    int currentState = 0;
    int progress = 0;
    ::Disassemble(packet, "ddd", &eventId, &currentState, &progress);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateKalieEvent (?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&eventId);
    connection.BindParameter(&currentState);
    connection.BindParameter(&progress);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"RequestUpdateKalieEventPacket() failed. "
        //     L"nEvent_Id[%d], nState[%d], nProgress[%d]",
        //     eventId, currentState, progress
        // )
        // L2_LOG_ERROR(Log,
        //     L"RequestUpdateKalieEventPacket() failed. "
        //     L"nEvent_Id[%d], nState[%d], nProgress[%d]",
        //     eventId, currentState, progress
        // )
        // Previous: L2_LOG_ERROR(Log, "Cannot update Kalie event %d to state %d, progress %d: database request failed.", eventId, currentState, progress)
        L2_LOG_ERROR(Log, "[Kalie event] Cannot update event %d to state %d, progress %d: the database request failed.", eventId, currentState, progress);
    }
    return false;
}

bool RequestUpdateKalieEventUserPacket(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00580708.
    L2_GUARD_FUNCTION();

    int rowCount = 0;
    const unsigned char* row = ::Disassemble(packet, "d", &rowCount);
    for (int index = 0; index < rowCount; ++index) {
        int characterId = 0;
        int token = 0;
        row = ::Disassemble(row, "dd", &characterId, &token);
        if (characterId <= 0)
            continue;

        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_UpdateKalieEventUser (?,?)}";
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.BindParameter(&token);
        if (!connection.ExecutePrepared(statement)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"RequestUpdateKalieEventUserPacket() failed. "
            //     L"Token[%d], nCharDbId[%d]",
            //     token, characterId
            // )
            // L2_LOG_ERROR(Log,
            //     L"RequestUpdateKalieEventUserPacket() failed. "
            //     L"Token[%d], nCharDbId[%d]",
            //     token, characterId
            // )
            // Previous: L2_LOG_ERROR(Log, "Cannot update Kalie event tokens to %d for character %d: database request failed at entry %d of %d, continuing batch.", token, characterId, index + 1, rowCount)
            L2_LOG_ERROR(Log, "[Kalie event] Cannot update tokens to %d for character %d: the database request failed at entry %d of %d; continuing the batch.", token, characterId, index + 1, rowCount);
        }
    }
    return false;
}

bool RequestLoadExdropCount(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00580e58. Paired current receiver
    // DBPacketExHandler::ResponseLoadExDropCountEx 0x00699f98.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    int resultCharacterId = 0;
    int exdropId = 0;
    int getCount = 0;
    int getTime = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&resultCharacterId);
    connection.Bind(&exdropId);
    connection.Bind(&getCount);
    connection.Bind(&getTime);

    std::array<char, 0x5000> rows{};
    int rowCount = 0;
    int rowBytes = 0;
    int latestGetTime = 0;
    constexpr const wchar_t* statement = L"{Call lin_LoadExdropCount (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            rowBytes +=
                ::Assemble(rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dd", exdropId, getCount);
            ++rowCount;
            if (latestGetTime < getTime) {
                latestGetTime = getTime;
            }
        }
    }

    if (rowCount != 0) {
        socket->Send("chdddb", 0xc6, 0xd2, characterId, latestGetTime, rowCount, rowBytes, rows.data());
    } else {
        socket->Send("chddd", 0xc6, 0xd2, characterId, latestGetTime, 0);
    }
    return false;
}

bool RequestUpdateExdropCount(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x005810ec.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int exdropId = 0;
    int getCount = 0;
    int getTime = 0;
    ::Disassemble(packet, "dddd", &characterId, &exdropId, &getCount, &getTime);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateExdropCount (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&exdropId);
    connection.BindParameter(&getCount);
    connection.BindParameter(&getTime);
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestResetExdropCount(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0058124c.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_ResetExdropCount (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestBeautyLoad(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00579498. The seven-field row is opaque at this
    // boundary. Only its two beauty identifiers and buy-count adjustment are
    // named by the unanimous SQL donors.
    L2_GUARD_FUNCTION();

    int groupCount = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &groupCount);
    CPacketHelperEx<20480> responseGroups;

    for (int groupIndex = 0; groupIndex < groupCount; ++groupIndex) {
        int beautyType = 0;
        int inputRowCount = 0;
        cursor = ::Disassemble(cursor, "dd", &beautyType, &inputRowCount);

        CPacketHelperEx<20480> responseRows;
        int outputRowCount = 0;
        for (int rowIndex = 0; rowIndex < inputRowCount; ++rowIndex) {
            int beautyUpperId = 0;
            int beautyId = 0;
            std::int64_t rowValue3 = 0;
            std::int64_t rowValue4 = 0;
            std::int64_t rowValue5 = 0;
            int rowValue6 = 0;
            int rowValue7 = 0;
            cursor = ::Disassemble(
                cursor, "ddQQQdd", &beautyUpperId, &beautyId, &rowValue3, &rowValue4, &rowValue5, &rowValue6, &rowValue7
            );

            int buyCount = 0;
            DBConn connection(runtime::process::CurrentDatabaseId());
            connection.Bind(&buyCount);
            constexpr const wchar_t* statement = L"{Call lin_LoadBeautyDataList(?,?,?)}";
            connection.Prepare(statement);
            connection.BindParameter(&beautyType);
            connection.BindParameter(&beautyUpperId);
            connection.BindParameter(&beautyId);
            if (connection.ExecutePrepared(statement) && connection.Fetch()) {
                responseRows.Assemble(
                    "ddQQQdd", beautyUpperId, beautyId, rowValue3, rowValue4, rowValue5,
                    runtime::SubtractInt32Wrapping(rowValue6, buyCount), rowValue7
                );
                ++outputRowCount;
            }
        }

        responseGroups.Assemble("ddb", beautyType, outputRowCount, responseRows.GetLength(), responseRows.GetPacket());
    }

    socket->Send("chdb", 0xc6, 0xd4, groupCount, responseGroups.GetLength(), responseGroups.GetPacket());
    return false;
}

bool RequestBuyBeautyItem(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0057981c. Paired current L2Server fixes response
    // subtype 0xd5 and the nested list contracts.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int builder = 0;
    std::int64_t adenaPrice = 0;
    std::int64_t beautyCoinPrice = 0;
    const unsigned char* cursor = ::Disassemble(packet, "ddQQ", &characterId, &builder, &adenaPrice, &beautyCoinPrice);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    const int originalFace = user->m_faceIndexEx;
    const int originalHairShape = user->m_hairShapeIndexEx;
    const int originalHairColor = user->m_hairColorIndexEx;
    CWareHouseSP warehouse = user->GetWareHouse(0, true);
    if (!warehouse) {
        socket->Send("chdd", 0xc6, 0xd5, 0, characterId);
        return false;
    }

    int selectedCount = 0;
    cursor = ::Disassemble(cursor, "d", &selectedCount);
    int beautyType = 3;
    int beautyId = -1;
    for (int index = 0; index < selectedCount; ++index) {
        cursor = ::Disassemble(cursor, "dd", &beautyType, &beautyId);
        if (beautyId == -1)
            continue;
        if (beautyType == 0) {
            user->m_faceIndexEx = beautyId;
        } else if (beautyType == 1) {
            user->m_hairColorIndexEx = beautyId;
        } else if (beautyType == 2) {
            user->m_hairShapeIndexEx = beautyId;
        }
    }

    int purchaseCount = 0;
    cursor = ::Disassemble(cursor, "d", &purchaseCount);
    if (runtime::AddInt32Wrapping(selectedCount, purchaseCount) != 3) {
        // Log.Add(LOG_ERROR, L"FILE[%s][%d] has problem dbid:[%d]", L"ServerPacketEx.cpp", 0x316a, characterId)
        // L2_LOG_ERROR(Log, L"FILE[%s][%d] has problem dbid:[%d]", L"ServerPacketEx.cpp", 0x316a, characterId)
        // Previous: L2_LOG_WARN(Log, "Beauty purchase rejected for character %d: selected count %d and purchase count %d do not sum to 3 under 32-bit wrapping, sending failure reply and restoring cached appearance", characterId, selectedCount, purchaseCount)
        L2_LOG_WARN(Log, "[beauty] Purchase rejected for character %d: selected count %d and purchase count %d do not sum to 3 under 32-bit wrapping; sending a failure reply and restoring the cached appearance.", characterId, selectedCount, purchaseCount);
        socket->Send("chdd", 0xc6, 0xd5, 0, characterId);
        // The shipping body sends the failure first, then restores the three
        // fields it modified while decoding the selected list.
        user->m_faceIndexEx = originalFace;
        user->m_hairColorIndexEx = originalHairColor;
        user->m_hairShapeIndexEx = originalHairShape;
        return false;
    }

    if (purchaseCount == 0) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_UpdateUserBeauty(?,?,?,?)}";
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.BindParameter(&user->m_faceIndexEx);
        connection.BindParameter(&user->m_hairShapeIndexEx);
        connection.BindParameter(&user->m_hairColorIndexEx);
        connection.ExecutePrepared(statement);
        socket->Send(
            "chdddddQQd", 0xc6, 0xd5, 1, characterId, user->m_faceIndexEx, user->m_hairShapeIndexEx,
            user->m_hairColorIndexEx, adenaPrice, beautyCoinPrice, 0
        );
        return false;
    }

    bool paid;
    {
        L2_WRITE_GUARD(*warehouse.operator->());
        paid = warehouse->BuyResetBeautyItem(warehouse.get(), builder, adenaPrice, beautyCoinPrice);
    }
    if (!paid) {
        socket->Send("chdd", 0xc6, 0xd5, 0, characterId);
        // Unlike the malformed-count branch, the current target deliberately
        // leaves the selected-list field mutations in place here.
        return false;
    }

    CPacketHelperEx<20480> purchasedRows;
    std::optional<DBConn> updateConnectionStorage;
    {
        L2_WRITE_GUARD(*user.operator->());
        int beautyUpperId = 0;
        for (int index = 0; index < purchaseCount; ++index) {
            cursor = ::Disassemble(cursor, "ddd", &beautyType, &beautyId, &beautyUpperId);

            if (beautyType == 0 && beautyId != -1 && user->m_faceIndexEx != beautyId) {
                DBConn incrementConnection(runtime::process::CurrentDatabaseId());
                std::optional<DBConn> purchaseConnectionStorage;
                {
                    L2_GUARD(g_beautyPurchaseCriticalSection);
                    constexpr const wchar_t* incrementStatement = L"{Call lin_IncrementBeautyData(?,?,?)}";
                    incrementConnection.Prepare(incrementStatement);
                    incrementConnection.BindParameter(&beautyType);
                    incrementConnection.BindParameter(&beautyId);
                    incrementConnection.BindParameter(&beautyUpperId);
                    incrementConnection.ExecutePrepared(incrementStatement);
                    DBConn& purchaseConnection = purchaseConnectionStorage.emplace(runtime::process::CurrentDatabaseId());
                    SaveBeautyPurchaseList(purchaseConnection, characterId, beautyType, beautyUpperId, beautyId);
                }
                user->m_faceIndexEx = beautyId;
                WriteBeautyItemAudit(*user.get(), 0x1fa2, beautyType, beautyId, adenaPrice, beautyCoinPrice);
            } else if (beautyType == 1 && beautyId != -1) {
                DBConn incrementConnection(runtime::process::CurrentDatabaseId());
                std::optional<DBConn> purchaseConnectionStorage;
                {
                    L2_GUARD(g_beautyPurchaseCriticalSection);
                    constexpr const wchar_t* incrementStatement = L"{Call lin_IncrementBeautyData(?,?,?)}";
                    incrementConnection.Prepare(incrementStatement);
                    incrementConnection.BindParameter(&beautyType);
                    incrementConnection.BindParameter(&beautyId);
                    incrementConnection.BindParameter(&beautyUpperId);
                    incrementConnection.ExecutePrepared(incrementStatement);
                    DBConn& purchaseConnection = purchaseConnectionStorage.emplace(runtime::process::CurrentDatabaseId());
                    SaveBeautyPurchaseList(purchaseConnection, characterId, beautyType, beautyUpperId, beautyId);
                }
                user->m_hairColorIndexEx = beautyId;
                WriteBeautyItemAudit(*user.get(), 0x1fa2, beautyType, beautyId, adenaPrice, beautyCoinPrice);
            } else if (beautyType == 2 && beautyId != -1) {
                DBConn incrementConnection(runtime::process::CurrentDatabaseId());
                std::optional<DBConn> purchaseConnectionStorage;
                {
                    L2_GUARD(g_beautyPurchaseCriticalSection);
                    constexpr const wchar_t* incrementStatement = L"{Call lin_IncrementBeautyData(?,?,?)}";
                    incrementConnection.Prepare(incrementStatement);
                    incrementConnection.BindParameter(&beautyType);
                    incrementConnection.BindParameter(&beautyId);
                    incrementConnection.BindParameter(&beautyUpperId);
                    incrementConnection.ExecutePrepared(incrementStatement);
                    DBConn& purchaseConnection = purchaseConnectionStorage.emplace(runtime::process::CurrentDatabaseId());
                    SaveBeautyPurchaseList(purchaseConnection, characterId, beautyType, beautyUpperId, beautyId);
                }
                user->m_hairShapeIndexEx = beautyId;
                WriteBeautyItemAudit(*user.get(), 0x1fa2, beautyType, beautyId, adenaPrice, beautyCoinPrice);
            }

            purchasedRows.Assemble("ddd", beautyType, beautyId, beautyUpperId);
        }

        DBConn& updateConnection = updateConnectionStorage.emplace(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* updateStatement = L"{Call lin_UpdateUserBeauty(?,?,?,?)}";
        updateConnection.Prepare(updateStatement);
        updateConnection.BindParameter(&characterId);
        updateConnection.BindParameter(&user->m_faceIndexEx);
        updateConnection.BindParameter(&user->m_hairShapeIndexEx);
        updateConnection.BindParameter(&user->m_hairColorIndexEx);
        updateConnection.ExecutePrepared(updateStatement);
        socket->Send(
            "chdddddQQdb", 0xc6, 0xd5, 1, characterId, user->m_faceIndexEx, user->m_hairShapeIndexEx, user->m_hairColorIndexEx,
            adenaPrice, beautyCoinPrice, purchasedRows.GetAssembledCount(), purchasedRows.GetLength(), purchasedRows.GetPacket()
        );
    }
    return false;
}

bool RequestResetBeautyItem(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0057b0d8. The paired receiver consumes exactly
    // three reset rows, so the response carries a blob without a row count.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int builder = 0;
    std::int64_t adenaPrice = 0;
    std::int64_t beautyCoinPrice = 0;
    const unsigned char* cursor = ::Disassemble(packet, "ddQQ", &characterId, &builder, &adenaPrice, &beautyCoinPrice);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    CWareHouseSP warehouse = user->GetWareHouse(0, true);
    if (!warehouse) {
        socket->Send("chdd", 0xc6, 0xd6, 0, characterId);
        return false;
    }

    {
        L2_WRITE_GUARD(*warehouse.operator->());
        const bool paid = warehouse->BuyResetBeautyItem(warehouse.get(), builder, adenaPrice, beautyCoinPrice);
        if (!paid) {
            socket->Send("chdd", 0xc6, 0xd6, 0, characterId);
            return false;
        }
    }

    CPacketHelperEx<20480> resetRows;
    std::optional<DBConn> updateConnectionStorage;
    {
        L2_WRITE_GUARD(*user.operator->());
        int beautyType = 3;
        int beautyId = -1;
        int beautyUpperId = 0;
        for (int index = 0; index < 3; ++index) {
            cursor = ::Disassemble(cursor, "ddd", &beautyType, &beautyId, &beautyUpperId);
            if (beautyId != -1) {
                if (beautyType == 0) {
                    user->m_faceIndexEx = 0;
                    WriteBeautyItemAudit(*user.get(), 0x1fa3, beautyType, beautyId, adenaPrice, beautyCoinPrice);
                } else if (beautyType == 1) {
                    user->m_hairColorIndexEx = 0;
                    WriteBeautyItemAudit(*user.get(), 0x1fa3, beautyType, beautyId, adenaPrice, beautyCoinPrice);
                } else if (beautyType == 2) {
                    user->m_hairShapeIndexEx = 0;
                    WriteBeautyItemAudit(*user.get(), 0x1fa3, beautyType, beautyId, adenaPrice, beautyCoinPrice);
                }
            }
            resetRows.Assemble("ddd", beautyType, beautyId, beautyUpperId);
        }

        DBConn& updateConnection = updateConnectionStorage.emplace(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* updateStatement = L"{Call lin_UpdateUserBeauty(?,?,?,?)}";
        updateConnection.Prepare(updateStatement);
        updateConnection.BindParameter(&characterId);
        updateConnection.BindParameter(&user->m_faceIndexEx);
        updateConnection.BindParameter(&user->m_hairShapeIndexEx);
        updateConnection.BindParameter(&user->m_hairColorIndexEx);
        updateConnection.ExecutePrepared(updateStatement);
        socket->Send(
            "chdddddQQb", 0xc6, 0xd6, 1, characterId, user->m_faceIndexEx, user->m_hairShapeIndexEx, user->m_hairColorIndexEx,
            adenaPrice, beautyCoinPrice, resetRows.GetLength(), resetRows.GetPacket()
        );
    }
    return false;
}

bool RequestResetBeautyList(CSocket* socket [[maybe_unused]], const unsigned char* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0053d614 is an exact no-op apart from the
    // load-enabled lookup (and immediate smart-pointer release) of user 0.
    L2_GUARD_FUNCTION();
    CUserSP user = g_userDb.GetUser(0, true, false, runtime::process::CurrentDatabaseId());
    return false;
}

bool RequestLoadNPCQuestLog(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0053d6fc delegates the fixed 41-by-10 traversal to
    // CUser::LoadQuestNPCLog after this three-dword lookup boundary.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int requestContext = 0;
    int worldId = 0;
    ::Disassemble(packet, "ddd", &characterId, &requestContext, &worldId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, worldId);
    if (!user) {
        // Log.Add(LOG_ERROR, "Load NPC Quest Log Fail!! nUserDBID[%d]", characterId)
        L2_LOG_ERROR(Log,
            "[quests] Cannot load NPC quest log for character %d, world %d, request context %d: "
            "character lookup failed. No reply sent.",
            characterId, worldId, requestContext);
        return false;
    }
    user->LoadQuestNPCLog(socket, characterId, requestContext);
    return false;
}

bool LeaveWorldSaveChar(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052cd7c is only this guarded delegation.
    L2_GUARD_FUNCTION();
    return CSocket::SaveCharacter(socket, packet);
}

bool LeaveWorldSaveSkillUse(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052ce24 is only this guarded delegation.
    L2_GUARD_FUNCTION();
    return CSocket::SaveSkillUse(socket, packet);
}

bool LeaveWorldSaveAbnormalStatus(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052cecc is only this guarded delegation.
    L2_GUARD_FUNCTION();
    return CSocket::SaveAbnormalStatus(socket, packet);
}

bool RequestLoadMenteeState(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00571300. Paired current L2Server receiver
    // 0x0068b56c consumes character ID followed by the normalized state bit.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);

    int menteeState = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&menteeState);
    constexpr const wchar_t* statement = L"{Call lin_LoadMenteeState (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }
    socket->Send("chdd", 0xc6, 0xd8, characterId, menteeState != 0 ? 1 : 0);
    return false;
}

bool RequestCancelEquipmentAddedEffectAndDropItem(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00540324 owns the user/warehouse lookup and lock.
    // CWareHouse 0x005e04f0 owns the remaining packet and response.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    const unsigned char* payload = ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, L"[%S][%d]cannot find user. User id[%d]", "ServerPacketEx.cpp", 0x3a79, characterId)
        // L2_LOG_ERROR(Log, L"[%S][%d]cannot find user. User id[%d]", "ServerPacketEx.cpp", 0x3a79, characterId)
        // Previous: L2_LOG_ERROR(Log, "Cannot cancel equipment effect and drop item: character %d is unavailable, no reply sent", characterId)
        L2_LOG_ERROR(Log, "[equipment] Cannot cancel the added effect and drop the item: character %d is unavailable; no reply sent.", characterId);
        runtime::process::ReportCallStack(LOG_ERROR, false);
    }

    CWareHouseSP warehouse;
    if (user) {
        warehouse = user->GetWareHouse(0, true);
        if (!warehouse) {
            // Log.Add(LOG_ERROR, L"[%S][%d]cannot find warehouse.", "ServerPacketEx.cpp", 0x3a80)
            // L2_LOG_ERROR(Log, L"[%S][%d]cannot find warehouse.", "ServerPacketEx.cpp", 0x3a80)
            // Previous: L2_LOG_ERROR(Log, "Cannot cancel equipment effect and drop item: inventory is unavailable for character %d, no reply sent", characterId)
            L2_LOG_ERROR(Log, "[equipment] Cannot cancel the added effect and drop the item: inventory is unavailable for character %d; no reply sent.", characterId);
        }
    }

    if (!user || !warehouse) {
        // The current image emits its smart-pointer NULL diagnostic and then
        // reaches CUser::GetWareHouse through null on the missing-user path.
        // Preserve the valid-input protocol while containing that target
        // crash at the portable process boundary. Both error paths still
        // consume the exact remaining carrier and send no reply.
        int ignoredValues[6]{};
        std::int64_t ignoredAmount = 0;
        short ignoredFlag = 0;
        int ignoredEffectMask = 0;
        ::Disassemble(
            payload, "ddddddQhd", &ignoredValues[0], &ignoredValues[1], &ignoredValues[2], &ignoredValues[3],
            &ignoredValues[4], &ignoredValues[5], &ignoredAmount, &ignoredFlag, &ignoredEffectMask
        );
        return false;
    }

    {
        L2_WRITE_GUARD(*warehouse.operator->());
        warehouse->CancelEquipmentAddedEffectAndDropItem(socket, payload);
    }
    return false;
}

bool RequestCharLevelUp(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00540670. Paired current L2Server sender
    // CDB::RequestCharLevelUp fixes the eight-dword carrier split below.
    L2_GUARD_FUNCTION();

    int requestValue1 = 0;
    int requestValue2 = 0;
    int characterId = 0;
    int requestValue4 = 0;
    int gate = 0;
    const unsigned char* trailing =
        ::Disassemble(packet, "ddddd", &requestValue1, &requestValue2, &characterId, &requestValue4, &gate);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user || user->GetPledgeID() == 0U)
        return false;

    CPledgeSP pledge =
        g_pledgeDb.GetPledge(static_cast<int>(user->GetPledgeID()), false, runtime::process::CurrentDatabaseId());
    if (!pledge || gate < 0 || gate >= 2)
        return false;

    int subjobId = 0;
    int level = 0;
    int unusedValue = 0;
    ::Disassemble(trailing, "ddd", &subjobId, &level, &unusedValue);
    if (user->CheckMaxLevel(subjobId, level)) {
        user->UpdateMaxLevel(subjobId, level, false);
    }
    return false;
}

bool RequestBrInterestedProduct(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00581650. The shipping loop increments its logical
    // row count and adds Assemble's signed return value without a cap.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);

    std::array<char, 0x5000> products{};
    int productId = 0;
    int productCount = 0;
    int responseLength = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&productId);
    constexpr const wchar_t* statement = L"{Call lin_BrLoadInterestedProduct (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            responseLength += ::Assemble(
                products.data() + responseLength, static_cast<int>(products.size()) - responseLength, "d", productId
            );
            ++productCount;
        }
    }
    if (productCount > 0) {
        socket->Send("chddb", 0xc6, 0xda, characterId, productCount, responseLength, products.data());
    }
    return false;
}

bool RequestBrAddInterestedProduct(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00581858. Raw instructions at 0x00581911 keep the
    // decoded product ID in r9 for the diagnostic's single vararg.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int productId = 0;
    ::Disassemble(packet, "dd", &characterId, &productId);
    if (productId < 1000001) {
        // Log.Add(LOG_ERROR, "RequestBrAddInterestedProduct, Invalid nProductID (%d)", productId)
        // L2_LOG_ERROR(Log, "RequestBrAddInterestedProduct, Invalid nProductID (%d)", productId)
        L2_LOG_WARN(Log,
            "[shop] Cannot add product %d to character %d's interested-product list: ID must be at least 1000001. "
            "Request rejected.", productId, characterId
        );
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_BrAddInterestedProduct (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&productId);
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestBrDeleteInterestedProduct(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x005819dc. Product zero means delete
    // all interested products for this character.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int productId = 0;
    ::Disassemble(packet, "dd", &characterId, &productId);
    if (productId != 0 && productId < 1000001) {
        // Log.Add(LOG_ERROR, "RequestBrDeleteInterestedProduct, Invalid nProductID (%d)", productId)
        // L2_LOG_ERROR(Log, "RequestBrDeleteInterestedProduct, Invalid nProductID (%d)", productId)
        L2_LOG_WARN(Log,
            "[shop] Cannot remove product %d from character %d's interested-product list: "
            "ID must be at least 1000001, or 0 to clear the list. Request rejected.", productId, characterId
        );
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_BrDeleteInterestedProduct (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&productId);
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestPeriodicShapeShiftedItemJob(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00581e48.
    L2_GUARD_FUNCTION();

    int operation = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &operation);
    if (operation == 0) {
        int characterId = 0;
        int userSocketId = 0;
        ::Disassemble(cursor, "dd", &characterId, &userSocketId);
        PeriodicShapeShiftedItemJobHandler::OnLoadInfo(socket, characterId, userSocketId);
    }
    return false;
}

bool RequestPledgeJoinSystemInit(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00590660.
    L2_GUARD_FUNCTION();

    int initializationPeriod = 0;
    ::Disassemble(packet, "d", &initializationPeriod);
    if (initializationPeriod == 0) {
        initializationPeriod = 0x278d00;
    }
    CPledgeJoinDB& pledgeJoinDb = CPledgeJoinDB::Instance();
    pledgeJoinDb.InitPledgeJoinSystemTables(socket, initializationPeriod);
    pledgeJoinDb.InitPledgeNotify();
    return false;
}

bool RequestLoadPledgeOnPledgeRecruitBoard(CSocket* socket [[maybe_unused]], const unsigned char* packet [[maybe_unused]]) {
    // Current Cached.exe 0x005907b4 ignores both carrier arguments and uses
    // CPledgeJoinDB's process-wide active-socket load path.
    L2_GUARD_FUNCTION();
    CPledgeJoinDB::Instance().LoadPledge();
    return false;
}

bool RequestWritePledgeRecruitBoard(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0059087c -> 0x0054d1bc binds world, registrant,
    // pledge. Our PostgreSQL callable places pledge before registrant.
    // the bindings below match that SQL signature, not the shipping order.
    L2_GUARD_FUNCTION();

    int worldId = 0;
    int pledgeId = 0;
    int registrantId = 0;
    int trend = 0;
    std::array<char16_t, 21> packetTitle{};
    std::array<char16_t, 257> packetBody{};
    int recruitType = 0;
    int pledgeType = 0;
    ::Disassemble(
        packet, "ddddSSdd", &worldId, &pledgeId, &registrantId, &trend, 0x2a, packetTitle.data(), 0x202, packetBody.data(),
        &recruitType, &pledgeType
    );
    if (worldId != runtime::process::CurrentDatabaseId())
        return false;

    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge)
        return false;

    const std::u16string pledgeName(pledge->GetName());
    const std::u16string pledgeMasterName(pledge->rullerName());
    const std::u16string titleMessage(packetTitle.data());
    const std::u16string bodyMessage(packetBody.data());
    const std::uint32_t pledgeLevel = static_cast<std::uint32_t>(pledge->skillLevel());
    const int pledgeNameValue = pledge->rootNameValue();
    const int registerDate = runtime::crt::Time32Now();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_WritePledgeRecruitBoard "
                                         L"(?,?,?,?,?,?,?,?,?,?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&worldId);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&registrantId);
    connection.BindParameter(pledgeName.c_str());
    connection.BindParameter(pledgeMasterName.c_str());
    connection.BindParameter(&pledgeLevel);
    connection.BindParameter(&pledgeNameValue);
    connection.BindParameter(&trend);
    connection.BindParameter(&registerDate);
    connection.BindParameter(titleMessage.c_str());
    connection.BindParameter(bodyMessage.c_str());
    connection.BindParameter(&recruitType);
    connection.BindParameter(&pledgeType);
    if (connection.ExecutePrepared(statement)) {
        CPledgeJoinDB::Instance().RegisterPledge(worldId, pledgeId);
        socket->Send("chdd", 0xc6, 0xe4, registrantId, 1);
    }
    return false;
}

bool RequestModifyPledgeRecruitBoard(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00590bd4.
    L2_GUARD_FUNCTION();

    int worldId = 0;
    int pledgeId = 0;
    int registrantId = 0;
    int trend = 0;
    std::array<char16_t, 21> packetTitle{};
    std::array<char16_t, 257> packetBody{};
    int recruitType = 0;
    int pledgeType = 0;
    ::Disassemble(
        packet, "ddddSSdd", &worldId, &pledgeId, &registrantId, &trend, 0x2a, packetTitle.data(), 0x202, packetBody.data(),
        &recruitType, &pledgeType
    );
    if (worldId != runtime::process::CurrentDatabaseId())
        return false;

    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge)
        return false;

    const std::wstring titleMessage = Utf16ToWide(packetTitle.data());
    const std::wstring bodyMessage = Utf16ToWide(packetBody.data());
    if (!ExecuteModifyPledgeRecruitBoard(
            worldId, pledgeId, registrantId, trend, titleMessage.c_str(), bodyMessage.c_str(), recruitType, pledgeType
        )) {
        return false;
    }

    CPledgeJoinDB& pledgeJoinDb = CPledgeJoinDB::Instance();
    pledgeJoinDb.RegisterPledge(worldId, pledgeId);
    if (recruitType == 1) {
        int characterId = 0;
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Bind(&characterId);
        constexpr const wchar_t* statement = L"{Call lin_DeletePledgeWaitingUsersByPledgeID (?,?)}";
        connection.Prepare(statement);
        connection.BindParameter(&worldId);
        connection.BindParameter(&pledgeId);
        if (connection.ExecutePrepared(statement)) {
            while (connection.Fetch()) {
                pledgeJoinDb.UnRegisterWaitingUser(worldId, characterId);
            }
        }
    }
    return false;
}

bool RequestDeletePledgeRecruitBoard(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00590f00.
    L2_GUARD_FUNCTION();

    int worldId = 0;
    int pledgeId = 0;
    ::Disassemble(packet, "dd", &worldId, &pledgeId);
    if (worldId != runtime::process::CurrentDatabaseId())
        return false;

    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge)
        return false;

    int characterId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&characterId);
    constexpr const wchar_t* statement = L"{Call lin_DeletePledgeRecruitBoard (?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&worldId);
    connection.BindParameter(&pledgeId);
    if (connection.ExecutePrepared(statement)) {
        CPledgeJoinDB& pledgeJoinDb = CPledgeJoinDB::Instance();
        pledgeJoinDb.UnRegisterPledge(worldId, pledgeId);
        while (connection.Fetch()) {
            pledgeJoinDb.UnRegisterWaitingUser(runtime::process::CurrentDatabaseId(), characterId);
        }
    }
    return false;
}

bool RequestSearchPledgeRecruitBoard(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00591170. The paired current receiver confirms the
    // five scalar header values followed by the ID and board-row carriers.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int worldId = 0;
    std::array<char16_t, 25> packetPledgeName{};
    int pledgeLevel = 0;
    int trend = 0;
    std::array<char16_t, 25> packetPledgeMasterName{};
    int sortCondition = 0;
    int orderCondition = 0;
    int page = 0;
    int recruitType = 0;
    ::Disassemble(
        packet, "ddSddSdddd", &characterId, &worldId, 0x32, packetPledgeName.data(), &pledgeLevel, &trend, 0x32,
        packetPledgeMasterName.data(), &sortCondition, &orderCondition, &page, &recruitType
    );
    if (worldId != runtime::process::CurrentDatabaseId())
        return false;

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    std::wstring pledgeName = Utf16ToWide(packetPledgeName.data());
    std::wstring pledgeMasterName = Utf16ToWide(packetPledgeMasterName.data());
    CPacketHelperEx<20480> pledgeIds;
    CPacketHelperEx<20480> pledgeRows;
    int totalPages = 0;
    const int resultCount = CPledgeJoinDB::Instance().SearchPledgeRecruitBoard(
        pledgeIds, pledgeRows, user, pledgeName.data(), pledgeLevel, trend, pledgeMasterName.data(), sortCondition,
        orderCondition, recruitType, totalPages, page
    );
    // Cached.exe 0x00591407..0x00591411 uses the row-buffer byte length
    // twice: the explicit length consumed by L2Server, then the 'b' size.
    // The former resultCount here truncated the forwarded client payload.
    socket->Send(
        "chdddddbdb", 0xc6, 0xde, characterId, worldId, page, totalPages, resultCount, pledgeIds.GetLength(),
        pledgeIds.GetPacket(), pledgeRows.GetLength(), pledgeRows.GetLength(), pledgeRows.GetPacket()
    );
    return false;
}

bool RequestModifyPledgeInfoOnBoard(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00581384.
    L2_GUARD_FUNCTION();

    int pledgeId = 0;
    ::Disassemble(packet, "d", &pledgeId);
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge)
        return false;

    const int worldId = runtime::process::CurrentDatabaseId();
    const std::uint32_t pledgeLevel = static_cast<std::uint32_t>(pledge->skillLevel());
    const std::u16string pledgeName(pledge->GetName());
    const std::u16string pledgeMasterName(pledge->rullerName());
    DBConn connection(worldId);
    constexpr const wchar_t* statement = L"{Call lin_ModifyPledgeInfoOnBoard (?,?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&worldId);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&pledgeLevel);
    connection.BindParameter(pledgeName.c_str());
    connection.BindParameter(pledgeMasterName.c_str());
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestPledgeOnPledgeRecruitBoard(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005914c0.
    L2_GUARD_FUNCTION();

    int worldId = 0;
    int characterId = 0;
    int pledgeId = 0;
    ::Disassemble(packet, "ddd", &worldId, &characterId, &pledgeId);
    if (worldId != runtime::process::CurrentDatabaseId())
        return false;

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user || !CPledgeJoinDB::Instance().IsPledgeRegistered(worldId, pledgeId)) {
        return false;
    }

    int trend = 0;
    std::array<char16_t, 21> titleMessage{};
    std::array<char16_t, 257> bodyMessage{};
    int recruitType = 0;
    int pledgeType = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&trend);
    connection.Bind(titleMessage.data(), 0x29);
    connection.Bind(bodyMessage.data(), 0x201);
    connection.Bind(&recruitType);
    connection.Bind(&pledgeType);
    constexpr const wchar_t* statement = L"{Call lin_PledgeOnPledgeRecruitBoard (?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&worldId);
    connection.BindParameter(&pledgeId);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        CPacketHelperEx<20480> text;
        text.Assemble("SS", titleMessage.data(), bodyMessage.data());
        socket->Send(
            "chdddddddb", 0xc6, 0xdf, worldId, characterId, pledgeId, trend, recruitType, pledgeType, text.GetLength(),
            text.GetLength(), text.GetPacket()
        );
    }
    return false;
}

bool RequestApplyInfoOnPledgeWaitingList(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00591830 checks the packet world in
    // the join registry without first comparing it with the process world.
    L2_GUARD_FUNCTION();

    int worldId = 0;
    int characterId = 0;
    ::Disassemble(packet, "dd", &worldId, &characterId);
    if (!CPledgeJoinDB::Instance().IsWaitingUserRegistered(worldId, characterId)) {
        return false;
    }

    int pledgeId = 0;
    std::array<char16_t, 25> pledgeName{};
    std::array<char16_t, 25> pledgeMasterName{};
    int pledgeLevel = 0;
    int trend = 0;
    std::array<char16_t, 17> titleMessage{};
    std::array<char16_t, 51> selfIntroduction{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&pledgeId);
    connection.Bind(pledgeName.data(), 0x32);
    connection.Bind(pledgeMasterName.data(), 0x32);
    connection.Bind(&pledgeLevel);
    connection.Bind(&trend);
    connection.Bind(titleMessage.data(), 0x22);
    connection.Bind(selfIntroduction.data(), 0x66);
    constexpr const wchar_t* statement = L"{Call lin_GetUserOnPledgeWaitingList2 (?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&worldId);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
        return false;
    }

    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge)
        return false;

    // The shipping buffers and packet strings are both two-byte wchar_t.
    // POSIX DBConn outputs use native wchar_t, so convert only at the
    // confirmed UTF-16 packet boundary.
    CPacketHelperEx<20480> pledgeInfo;
    pledgeInfo.Assemble(
        "SSdddSS", pledgeName.data(), pledgeMasterName.data(), pledgeLevel, trend, pledge->GetMemberCount(),
        titleMessage.data(), selfIntroduction.data()
    );
    socket->Send(
        "chddddb", 0xc6, 0xe3, worldId, characterId, pledgeId, pledgeInfo.GetLength(), pledgeInfo.GetLength(),
        pledgeInfo.GetPacket()
    );
    return false;
}

bool RequestWritePledgeWaitingUserList(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00591bfc. Paired current L2Server sender
    // CDB::RequestWritePledgeWaitingUserList fixes the two UTF-16 extents and
    // confirms that the transmitted timestamp is decoded but not persisted.
    L2_GUARD_FUNCTION();

    int worldId = 0;
    int characterId = 0;
    std::array<char16_t, 25> packetCharacterName{};
    int classId = 0;
    int level = 0;
    int pledgeId = 0;
    int transmittedTime = 0;
    std::array<char16_t, 51> packetSelfIntroduction{};
    ::Disassemble(
        packet, "ddSddddS", &worldId, &characterId, 0x32, packetCharacterName.data(), &classId, &level, &pledgeId,
        &transmittedTime, 0x66, packetSelfIntroduction.data()
    );
    if (worldId != runtime::process::CurrentDatabaseId())
        return false;

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    CPledgeJoinDB& pledgeJoinDb = CPledgeJoinDB::Instance();
    if (pledgeJoinDb.IsWaitingUserRegistered(worldId, characterId)) {
        return false;
    }
    if (!pledgeJoinDb.CanRegistratePledge(pledgeId)) {
        socket->Send("chdd", 0xc6, 0xe4, characterId, 0);
        return false;
    }

    const std::wstring characterName = Utf16ToWide(packetCharacterName.data());
    const std::wstring selfIntroduction = Utf16ToWide(packetSelfIntroduction.data());
    if (!PledgeDBUtil::WritePledgeWaitingUser(
            worldId, characterId, characterName.c_str(), classId, level, pledgeId, selfIntroduction.c_str()
        )) {
        const int result = pledgeJoinDb.IsDraftUserRegistered(worldId, characterId) ? 3 : 0;
        socket->Send("chdd", 0xc6, 0xe4, characterId, result);
        // Log.Add(LOG_ERROR, L"There is no pledge on pledge join system")
        // L2_LOG_ERROR(Log, L"There is no pledge on pledge join system")
        // Previous: L2_LOG_ERROR(Log, "Cannot write clan recruitment application for character %d, clan %d, world %d. Failure reply requested with result %d, waiting-list cache not updated.", characterId, pledgeId, worldId, result)
        L2_LOG_ERROR(Log, "[clan recruitment] Cannot write the application for character %d, clan %d, world %d. A failure reply was requested with result %d; the waiting-list cache was not updated.", characterId, pledgeId, worldId, result);
        return false;
    }

    pledgeJoinDb.RegisterWaitingUser(worldId, characterId, pledgeId);
    const int result = pledgeJoinDb.IsDraftUserRegistered(worldId, characterId) ? 4 : 2;
    socket->Send("chdd", 0xc6, 0xe4, characterId, result);
    return false;
}

bool RequestDeletePledgeWaitingUserList(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00591fe0 has two asymmetric forms:
    // requester -1 selects by name and unregisters only after SQL succeeds.
    // the numeric form unregisters regardless of ExecutePrepared's result.
    L2_GUARD_FUNCTION();

    int worldId = 0;
    int requesterId = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &worldId, &requesterId);
    CPledgeJoinDB& pledgeJoinDb = CPledgeJoinDB::Instance();

    if (requesterId == -1) {
        std::array<char16_t, 25> packetCharacterName{};
        ::Disassemble(cursor, "S", 0x32, packetCharacterName.data());
        CUserSP target = g_userDb.GetUser(packetCharacterName.data(), false);
        if (!target)
            return false;

        int returnedPledgeId = 0;
        const int targetId = static_cast<int>(target->GetId());
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Bind(&returnedPledgeId);
        constexpr const wchar_t* statement = L"{Call lin_DeletePledgeWaitingUser (?,?)}";
        connection.Prepare(statement);
        connection.BindParameter(&worldId);
        connection.BindParameter(&targetId);
        if (connection.ExecutePrepared(statement)) {
            while (connection.Fetch()) {
                pledgeJoinDb.DecresePledgeWaitUserCount(returnedPledgeId);
            }
            pledgeJoinDb.UnRegisterWaitingUser(worldId, targetId);
        }
        return false;
    }

    int targetId = 0;
    ::Disassemble(cursor, "d", &targetId);
    if (worldId != runtime::process::CurrentDatabaseId())
        return false;

    CUserSP target = g_userDb.GetUser(targetId, true, false, runtime::process::CurrentDatabaseId());
    if (!target)
        return false;

    int returnedPledgeId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&returnedPledgeId);
    constexpr const wchar_t* statement = L"{Call lin_DeletePledgeWaitingUser (?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&worldId);
    connection.BindParameter(&targetId);
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            pledgeJoinDb.DecresePledgeWaitUserCount(returnedPledgeId);
        }
    }
    pledgeJoinDb.UnRegisterWaitingUser(worldId, targetId);

    if (requesterId == targetId) {
        const int result = pledgeJoinDb.IsDraftUserRegistered(worldId, targetId) ? 3 : 0;
        socket->Send("chdd", 0xc6, 0xe4, targetId, result);
        return false;
    }

    CUserSP requester = g_userDb.GetUser(requesterId, true, false, runtime::process::CurrentDatabaseId());
    if (!requester)
        return false;
    CPledgeSP pledge =
        g_pledgeDb.GetPledge(static_cast<int>(requester->GetPledgeID()), false, runtime::process::CurrentDatabaseId());
    if (!pledge)
        return false;

    const std::u16string pledgeName(pledge->GetName());
    const std::u16string requesterName = std::u16string(requester->GetCharName());
    const std::u16string requesterAccountName = std::u16string(requester->GetAccountName());
    const std::u16string targetName = std::u16string(target->GetCharName());
    const std::u16string targetAccountName = std::u16string(target->GetAccountName());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x814, requester->GetId(), requester->GetAccountID(), target->GetId(), target->GetAccountID(), 0, 0, 0,
        pledgeName.c_str(), nullptr, nullptr, pledge->GetId(), pledge->skillLevel(), 0, 0, 0, 0, 0, 0, 0, 0,
        requesterName.c_str(), requesterAccountName.c_str(), targetName.c_str(), targetAccountName.c_str(), 0
    );
    return false;
}

bool RequestPledgeWaitingUserList(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005928a8.
    L2_GUARD_FUNCTION();

    int worldId = 0;
    int characterId = 0;
    int pledgeId = 0;
    ::Disassemble(packet, "ddd", &worldId, &characterId, &pledgeId);
    if (worldId != runtime::process::CurrentDatabaseId())
        return false;

    CUserSP user = g_userDb.FindResidentUser(characterId);
    if (!user)
        return false;

    CPacketHelperEx<20480> waitingUsers;
    const int count = CPledgeJoinDB::Instance().RequestPledgeWaitingUserList(waitingUsers, user, pledgeId);
    socket->Send(
        "chddddb", 0xc6, 0xe0, worldId, characterId, count, waitingUsers.GetLength(), waitingUsers.GetLength(),
        waitingUsers.GetPacket()
    );
    return false;
}

bool RequestUserOnPledgeWaitingUserList(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00592acc.
    L2_GUARD_FUNCTION();

    int worldId = 0;
    int requesterId = 0;
    int pledgeId = 0;
    int targetId = 0;
    ::Disassemble(packet, "dddd", &worldId, &requesterId, &pledgeId, &targetId);
    if (worldId != runtime::process::CurrentDatabaseId())
        return false;

    CUserSP requester = g_userDb.FindResidentUser(requesterId);
    if (!requester || !CPledgeJoinDB::Instance().IsWaitingUserRegistered(worldId, targetId)) {
        return false;
    }

    std::array<char16_t, 51> selfIntroduction{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(selfIntroduction.data(), 0x66);
    constexpr const wchar_t* statement = L"{Call lin_GetUserOnPledgeWaitingList (?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&worldId);
    connection.BindParameter(&targetId);
    connection.BindParameter(&pledgeId);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        socket->Send("chdddS", 0xc6, 0xe1, worldId, requesterId, targetId, selfIntroduction.data());
    }
    return false;
}

bool RequestWritePledgeDraftUserList(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00592d78. The SQL result is ignored.
    // the target publishes the in-memory registration and reply afterwards.
    L2_GUARD_FUNCTION();

    int worldId = 0;
    int characterId = 0;
    std::array<char16_t, 25> packetCharacterName{};
    int classId = 0;
    int level = 0;
    int trend = 0;
    int registerDate = 0;
    ::Disassemble(
        packet, "ddSdddd", &worldId, &characterId, 0x32, packetCharacterName.data(), &classId, &level, &trend, &registerDate
    );
    if (worldId != runtime::process::CurrentDatabaseId())
        return false;

    CUserSP user = g_userDb.FindResidentUser(characterId);
    if (!user)
        return false;

    CPledgeJoinDB& pledgeJoinDb = CPledgeJoinDB::Instance();
    if (pledgeJoinDb.IsDraftUserRegistered(worldId, characterId)) {
        return false;
    }

    const std::u16string characterName(packetCharacterName.data());
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_WritePledgeDraftUser (?,?,?,?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&worldId);
    connection.BindParameter(&characterId);
    connection.BindParameter(characterName.c_str());
    connection.BindParameter(&classId);
    connection.BindParameter(&level);
    connection.BindParameter(&trend);
    connection.BindParameter(&registerDate);
    connection.ExecutePrepared(statement);

    pledgeJoinDb.RegisterDraftUser(worldId, characterId);
    const int result = pledgeJoinDb.IsWaitingUserRegistered(worldId, characterId) ? 4 : 3;
    socket->Send("chdd", 0xc6, 0xe4, characterId, result);
    return false;
}

bool RequestDeletePledgeDraftUserList(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00593080 uses the loaded user's world only for SQL.
    // registry lookups keep the packet world without an equality gate.
    L2_GUARD_FUNCTION();

    int packetWorldId = 0;
    int characterId = 0;
    ::Disassemble(packet, "dd", &packetWorldId, &characterId);
    CUserSP user = g_userDb.FindResidentUser(characterId);
    if (!user)
        return false;

    const int databaseWorldId = user->GetWorld();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeletePledgeDraftUser (?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&databaseWorldId);
    connection.BindParameter(&characterId);
    connection.ExecutePrepared(statement);

    CPledgeJoinDB& pledgeJoinDb = CPledgeJoinDB::Instance();
    pledgeJoinDb.UnRegisterDraftUser(packetWorldId, characterId);
    const int result = pledgeJoinDb.IsWaitingUserRegistered(packetWorldId, characterId) ? 2 : 0;
    socket->Send("chdd", 0xc6, 0xe4, characterId, result);
    return false;
}

bool RequestPledgeDraftUserList(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005949a8. Class groups are generated by the
    // original Shared CategoryContent owner, not a portable hard-coded table.
    L2_GUARD_FUNCTION();

    int worldId = 0;
    int requesterId = 0;
    int minimumLevel = 0;
    int maximumLevel = 0;
    std::uint32_t classGroup = 0;
    std::array<char16_t, 25> packetName{};
    int sortCondition = 0;
    int orderCondition = 0;
    ::Disassemble(
        packet, "dddddSdd", &worldId, &requesterId, &minimumLevel, &maximumLevel, &classGroup, 0x32, packetName.data(),
        &sortCondition, &orderCondition
    );

    CUserSP requester = g_userDb.FindResidentUser(requesterId);
    if (!requester || classGroup > 9U)
        return false;

    CategoryContent::ClassRole& classRole = CategoryContent::ClassRole::inst();
    const std::u16string classList(classRole.FindClassString(static_cast<int>(classGroup)));
    const std::u16string name(packetName.data());

    int characterId = 0;
    std::array<char16_t, 25> characterName{};
    int classId = 0;
    int level = 0;
    int trend = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&characterId);
    connection.Bind(characterName.data(), 0x32);
    connection.Bind(&classId);
    connection.Bind(&level);
    connection.Bind(&trend);
    constexpr const wchar_t* statement = L"{Call lin_SearchPledgeDraftUserList (?,?,?,?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&worldId);
    connection.BindParameter(&minimumLevel);
    connection.BindParameter(&maximumLevel);
    connection.BindParameter(classList.c_str());
    connection.BindParameter(name.c_str());
    connection.BindParameter(&sortCondition);
    connection.BindParameter(&orderCondition);
    if (!connection.ExecutePrepared(statement))
        return false;

    CPacketHelperEx<20480> draftUsers;
    int count = 0;
    while (connection.Fetch()) {
        draftUsers.Assemble("dSddd", characterId, characterName.data(), classId, level, trend);
        ++count;
        // The shipping loop touches the lazy CPledgeJoinDB singleton after
        // each assembled row even though it calls no member on that path.
        CPledgeJoinDB::Instance();
        if (count >= 0xc9)
            break;
    }
    socket->Send(
        "chddddb", 0xc6, 0xe2, worldId, requesterId, count, draftUsers.GetLength(), draftUsers.GetLength(),
        draftUsers.GetPacket()
    );
    return false;
}

bool RequestPledgeRecruitApplyInfo(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00593328.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int pledgeId = 0;
    ::Disassemble(packet, "dd", &characterId, &pledgeId);
    CUserSP user = g_userDb.FindResidentUser(characterId);
    if (!user)
        return false;

    CPledgeJoinDB& pledgeJoinDb = CPledgeJoinDB::Instance();
    int result = 0;
    if (pledgeId == 0) {
        const int worldId = runtime::process::CurrentDatabaseId();
        const bool draft = pledgeJoinDb.IsDraftUserRegistered(worldId, characterId);
        const bool waiting = pledgeJoinDb.IsWaitingUserRegistered(worldId, characterId);
        if (draft) {
            result = waiting ? 4 : 3;
        } else if (waiting) {
            result = 2;
        }
    } else if (pledgeJoinDb.IsPledgeRegistered(runtime::process::CurrentDatabaseId(), pledgeId)) {
        result = 1;
    }
    socket->Send("chdd", 0xc6, 0xe4, characterId, result);
    return false;
}

bool RequestPledgeJoinSystemCode(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0059356c.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int requestType = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &characterId, &requestType);
    CPledgeJoinDB& pledgeJoinDb = CPledgeJoinDB::Instance();

    // if (requestType == 1) {
    if (requestType == PledgeJoinSystem::PJSC_SET_LIMIT_PLEDGE_COUNT) {
        int pledgeId = 0;
        int userCount = 0;
        ::Disassemble(cursor, "dd", &pledgeId, &userCount);
        pledgeJoinDb.SetPledgeWaitUserCount(pledgeId, userCount);
        return false;
    }

    int status = 0;
    int registrationTime = 0;
    // if (requestType == 2) {
    if (requestType == PledgeJoinSystem::PJSC_GET_USER_TIME) {
        const int worldId = runtime::process::CurrentDatabaseId();
        if (pledgeJoinDb.IsWaitingUserRegistered(worldId, characterId)) {
            status = 1;
            registrationTime = pledgeJoinDb.GetTimeOnWait(characterId);
        }
        if (pledgeJoinDb.IsDraftUserRegistered(worldId, characterId)) {
            status = 2;
            registrationTime = pledgeJoinDb.GetTimeOnDraft(characterId);
        }
    // } else if (requestType == 3) {
    } else if (requestType == PledgeJoinSystem::PJSC_GET_PLEDGE_TIME) {
        int pledgeId = 0;
        ::Disassemble(cursor, "d", &pledgeId);
        if (pledgeJoinDb.IsPledgeRegistered(runtime::process::CurrentDatabaseId(), pledgeId)) {
            status = 3;
            registrationTime = pledgeJoinDb.GetTimeOnPledge(pledgeId);
        }
    } else {
        return false;
    }

    socket->Send("chdddd", 0xc6, 0xe5, requestType, characterId, status, registrationTime);
    return false;
}

bool RequestModifyAppliedUserInfo(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00593884.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int level = 0;
    int classId = 0;
    ::Disassemble(packet, "ddd", &characterId, &level, &classId);
    CUserSP user = g_userDb.FindResidentUser(characterId);
    if (user) {
        CPledgeJoinDB::Instance().ModifyUserInfoOnPledgeJoinDB(characterId, level, classId);
    }
    return false;
}

bool RequestUpdateMaxLevel(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00540930. Paired current L2Server
    // CDB::RequestUpdateMaxLevel at 0x0065fe6c fixes the carrier as
    // character ID, subjob ID, then level.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int subjobId = 0;
    int level = 0;
    ::Disassemble(packet, "ddd", &characterId, &subjobId, &level);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        user->UpdateMaxLevel(subjobId, level, true);
    }
    return false;
}

bool RequestSavePetitionExternalNotify(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00581f2c and paired current L2Server sender
    // 0x0065ff44 fix the Sdd fields as character name, message ID and
    // character ID, in that order.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 25> characterNamePacket{};
    int messageId = 0;
    int characterId = 0;
    ::Disassemble(packet, "Sdd", 0x32, characterNamePacket.data(), &messageId, &characterId);
    const std::u16string characterName(characterNamePacket.data());
    const int worldId = runtime::process::CurrentDatabaseId();

    DBConn connection(worldId);
    constexpr const wchar_t* statement = L"{Call lin_SavePetitionExternalNotify(?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&worldId);
    connection.BindParameter(characterName.c_str());
    connection.BindParameter(&messageId);
    connection.ExecutePrepared(statement);

    CUserSP user = g_userDb.FindResidentUser(characterId);
    if (user && user->IsLoggedIn()) {
        socket->Send("chdd", 0xc6, 0xe6, worldId, characterId);
    }
    return false;
}

bool RequestDeletePetitionExternalNotify(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00582158. The paired sender at 0x0066002c proves
    // that the first dword is the world ID and the second is character ID.
    L2_GUARD_FUNCTION();

    int worldId = 0;
    int characterId = 0;
    ::Disassemble(packet, "dd", &worldId, &characterId);
    if (worldId != runtime::process::CurrentDatabaseId()) {
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeletePetitionExternalNotify(?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&worldId);
    connection.BindParameter(user->GetCharName());
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestLoadPetitionExternalNotify(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00582360. Its selected msg_id is used only as the
    // Fetch gate. The e6 reply carries the original world/character pair.
    L2_GUARD_FUNCTION();

    int worldId = 0;
    int characterId = 0;
    ::Disassemble(packet, "dd", &worldId, &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    int messageId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&messageId);
    constexpr const wchar_t* statement = L"{Call lin_LoadPetitionExternalNotify(?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&worldId);
    connection.BindParameter(user->GetCharName());
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        socket->Send("chdd", 0xc6, 0xe6, worldId, characterId);
    }
    return false;
}

bool RequestSetDormantTime(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x005825a4. The middle dword must be the -1 sentinel
    // compiled by paired CDB::RequestResetDormantHistory at 0x006601dc.
    L2_GUARD_FUNCTION();

    int accountId = 0;
    int sentinel = 0;
    int dormantType = 0;
    ::Disassemble(packet, "ddd", &accountId, &sentinel, &dormantType);
    DBConn connection(runtime::process::CurrentDatabaseId());
    if (sentinel != -1)
        return false;

    constexpr const wchar_t* statement = L"{Call lin_ResetDormantHistory(?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&accountId);
    connection.BindParameter(&dormantType);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "Failed to ResetDormantHistory account id=[%d]", accountId)
        // L2_LOG_ERROR(Log, "Failed to ResetDormantHistory account id=[%d]", accountId)
        L2_LOG_ERROR(Log,
            "[accounts] Database request to reset dormant history for account %d, dormant type %d failed.",
            accountId, dormantType
        );
    }
    return false;
}

bool RequestLoadAccountMaxLevel(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00582738. Its success branch sends even when the
    // single Fetch returns false, leaving both zero-initialized result cells.
    L2_GUARD_FUNCTION();

    int accountId = 0;
    ::Disassemble(packet, "d", &accountId);
    if (accountId <= 0)
        return false;

    int characterId = 0;
    int level = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&characterId);
    connection.Bind(&level);
    constexpr const wchar_t* statement = L"{Call lin_ReloadAccountHighestExpAndLevel (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&accountId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "Failed to RequestLoadAccountMaxLevel account id=[%d]", accountId)
        // L2_LOG_ERROR(Log, "Failed to RequestLoadAccountMaxLevel account id=[%d]", accountId)
        L2_LOG_ERROR(Log,
            "[accounts] Database request to load the highest character level for account %d failed. "
            "No reply will be sent.", accountId
        );
        return false;
    }

    connection.Fetch();
    socket->Send("chddd", 0xc6, 0xe7, accountId, characterId, level);
    return false;
}

bool RequestReloadUserFlag(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00548a58 uses the resident world/character map and
    // writes the exact CUser reload flag at target +0x25d8.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int reloadFlag = 0;
    ::Disassemble(packet, "dd", &characterId, &reloadFlag);
    CUserSP user = g_userDb.FindResidentUser(characterId);
    if (user) {
        user->SetReloadFlag(reloadFlag != 0);
    }
    return false;
}

bool RequestRegisterXmasWish(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00582910. The wish carrier is capped at 0x200
    // bytes. Only the successful insert path emits audit record 0x151.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    std::array<char16_t, 256> wishPacket{};
    ::Disassemble(packet, "dS", &characterId, 0x200, wishPacket.data());
    CUserSP user = g_userDb.FindResidentUser(characterId);
    if (!user)
        return false;

    const int worldId = runtime::process::CurrentDatabaseId();
    const std::u16string wish(wishPacket.data());
    int databaseResult = 0;
    DBConn connection(worldId);
    connection.Bind(&databaseResult);
    constexpr const wchar_t* statement = L"{Call lin_RegistXmasWish(?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&worldId);
    connection.BindParameter(&characterId);
    connection.BindParameter(user->GetCharName());
    connection.BindParameter(wish.c_str());
    if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
        socket->Send("chdd", 0xc6, 0xec, characterId, 4);
        return false;
    }
    if (databaseResult == 0) {
        socket->Send("chdd", 0xc6, 0xec, characterId, 2);
        return false;
    }

    socket->Send("chdd", 0xc6, 0xec, characterId, 0);

    std::u16string auditWish(wishPacket.data());
    if (auditWish.size() > 199U) {
        auditWish.resize(199U);
    }
    const std::u16string characterName = std::u16string(user->GetCharName());
    const std::u16string accountName = std::u16string(user->GetAccountName());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x151, characterId, user->GetAccountID(), 0, 0, 0, 0, 0, auditWish.c_str(), nullptr, nullptr, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, characterName.c_str(), accountName.c_str(), nullptr, nullptr, 0
    );
    return false;
}

bool RequestConfirmXmasWish(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00582de0. The selected char_id is deliberately
    // ignored: Execute+Fetch maps to reply 2 and either failure maps to 3.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.FindResidentUser(characterId);
    if (!user)
        return false;

    int selectedCharacterId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&selectedCharacterId);
    constexpr const wchar_t* statement = L"{Call lin_ConfirmXmasWish(?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    const int result = connection.ExecutePrepared(statement) && connection.Fetch() ? 2 : 3;
    socket->Send("chdd", 0xc6, 0xed, characterId, result);
    return false;
}

bool RequestGetDormantGiftInfo(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00582fd0. Paired RequestGetDormantInfo and
    // ReplyGetDormantGiftInfo prove the ordered roles below: account,
    // current character, dormant type and request kind. SQL returns the
    // previously recorded character and gift time.
    L2_GUARD_FUNCTION();

    int accountId = 0;
    int characterId = 0;
    int dormantType = 0;
    int requestKind = 0;
    ::Disassemble(packet, "dddd", &accountId, &characterId, &dormantType, &requestKind);

    int recordedCharacterId = 0;
    int giftTime = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&recordedCharacterId);
    connection.Bind(&giftTime);
    constexpr const wchar_t* statement = L"{Call lin_GetDormantGiftInfo(?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&accountId);
    connection.BindParameter(&characterId);
    connection.BindParameter(&dormantType);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
        // Log.Add(LOG_ERROR, "Failed to GetDormantGiftInfo account id=[%d]", accountId)
        // L2_LOG_ERROR(Log, "Failed to GetDormantGiftInfo account id=[%d]", accountId)
        L2_LOG_ERROR(Log,
            "[accounts] Dormant gift query failed or returned no row for account %d, character %d, "
            "dormant type %d, request kind %d. No reply will be sent.",
            accountId, characterId, dormantType, requestKind
        );
        return false;
    }

    socket->Send("chddddd", 0xc6, 0xee, accountId, recordedCharacterId, dormantType, giftTime, requestKind);
    return false;
}

bool RequestNewManipulateItem(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00548b6c.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &characterId);
    int manipulateType = 0x23;
    int requestContext = 0;
    cursor = ::Disassemble(cursor, "dd", &manipulateType, &requestContext);

    int responseId = 0;
    int responseId2 = 0;
    auto sendFailure = [&](bool decodeResponseIds) {
        if (decodeResponseIds) {
            ::Disassemble(cursor, "dd", &responseId, &responseId2);
        }
        socket->Send("chdddddd", 0xc6, 0xf0, responseId, manipulateType, requestContext, responseId2, 0, 0);
    };

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user", "ServerPacketEx.cpp", 0x4031)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user", "ServerPacketEx.cpp", 0x4031)
        L2_LOG_ERROR(Log,
            "[items] Cannot process item changes for character %d: character unavailable. "
            "Operation type %d, request %d; sending failure reply.", characterId, manipulateType, requestContext
        );
        sendFailure(true);
        return false;
    }

    CWareHouseSP warehouse = user->GetWareHouse(0, true);
    if (!warehouse) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find inventory warehouse, char dbid[%d]", "ServerPacketEx.cpp", 0x403a,
        //     characterId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find inventory warehouse, char dbid[%d]", "ServerPacketEx.cpp", 0x403a,
        //     characterId
        // )
        L2_LOG_ERROR(Log,
            "[items] Cannot process item changes for character %d: inventory unavailable. "
            "Operation type %d, request %d; sending failure reply.", characterId, manipulateType, requestContext
        );
        sendFailure(true);
        return false;
    }

    std::optional<std::vector<EtcMani*>> etcManisStorage;
    std::optional<CPacketHelperEx<20480>> itemPacketStorage;
    std::optional<CPacketHelperEx<20480>> etcManiPacketStorage;
    {
        L2_WRITE_GUARD(*warehouse.operator->());
        std::vector<EtcMani*>& etcManis = etcManisStorage.emplace();
        CPacketHelperEx<20480>& itemPacket = itemPacketStorage.emplace();
        CPacketHelperEx<20480>& etcManiPacket = etcManiPacketStorage.emplace();
        unsigned int changedItemCount = 0;
        std::uint64_t processedEtcManiCount = 0;
        const bool manipulated = warehouse->ManipulateItems(
            socket, cursor, responseId, responseId2, etcManis, itemPacket, etcManiPacket, changedItemCount, processedEtcManiCount
        );
        if (!manipulated) {
            sendFailure(false);
        } else {
            socket->Send(
                "chddddddbb", 0xc6, 0xf0, responseId, manipulateType, requestContext, responseId2,
                static_cast<int>(processedEtcManiCount), static_cast<int>(changedItemCount), etcManiPacket.GetLength(),
                etcManiPacket.GetPacket(), itemPacket.GetLength(), itemPacket.GetPacket()
            );
        }
    }
    return false;
}

bool TradeDivideAdena(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00540a60.
    L2_GUARD_FUNCTION();

    int sourceCharacterId = 0;
    int targetCharacterId = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &sourceCharacterId, &targetCharacterId);
    CUserSP sourceUser = g_userDb.GetUser(sourceCharacterId, true, false, runtime::process::CurrentDatabaseId());
    CUserSP targetUser = g_userDb.GetUser(targetCharacterId, true, false, runtime::process::CurrentDatabaseId());
    if (!sourceUser || !targetUser) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "ServerPacketEx.cpp", 0x406c, sourceCharacterId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "ServerPacketEx.cpp", 0x406c, sourceCharacterId
        // )
        L2_LOG_ERROR(Log,
            "[trade] Cannot divide adena: source character %d is %s; target character %d is %s. "
            "No reply will be sent.", sourceCharacterId, sourceUser ? "available" : "unavailable",
            targetCharacterId, targetUser ? "available" : "unavailable"
        );
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "ServerPacketEx.cpp", 0x406d, targetCharacterId
        // )
        // Noise: the combined diagnostic above reports both lookup results without claiming both characters are missing.
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "ServerPacketEx.cpp", 0x406d, targetCharacterId
        // )
        return false;
    }

    CWareHouseSP sourceWarehouse = sourceUser->GetWareHouse(0, true);
    CWareHouseSP targetWarehouse = targetUser->GetWareHouse(0, true);
    if (!sourceWarehouse || !targetWarehouse)
        return false;

    const std::uint64_t sourceLock = sourceWarehouse->GetLockAddr();
    const std::uint64_t targetLock = targetWarehouse->GetLockAddr();
    if (sourceLock == targetLock) {
        // Log.Add(LOG_ERROR, "Same warehouse double writelock?-_-,")
        // L2_LOG_ERROR(Log, "Same warehouse double writelock?-_-,")
        L2_LOG_WARN(Log,
            "[trade] Adena division from character %d to character %d uses the same inventory lock. "
            "Continuing with one write lock.", sourceCharacterId, targetCharacterId
        );
        {
            L2_WRITE_GUARD(*sourceWarehouse.operator->());
            sourceWarehouse->TradeDivideAdena(socket, cursor, targetWarehouse.get());
        }
        return false;
    }

    CWareHouse* first = sourceLock < targetLock ? sourceWarehouse.get() : targetWarehouse.get();
    CWareHouse* second = sourceLock < targetLock ? targetWarehouse.get() : sourceWarehouse.get();
    {
        L2_WRITE_GUARD(*first);
        {
            L2_WRITE_GUARD(*second);
            sourceWarehouse->TradeDivideAdena(socket, cursor, targetWarehouse.get());
        }
    }
    return false;
}

bool RequestBeautyPurchaseList(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005831f8.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    char requestFlag = 0;
    ::Disassemble(packet, "dc", &characterId, &requestFlag);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "FILE[%s][%d] Invalid User id[%d]", "ServerPacketEx.cpp", 0x40d8, characterId)
        // L2_LOG_ERROR(Log, "FILE[%s][%d] Invalid User id[%d]", "ServerPacketEx.cpp", 0x40d8, characterId)
        L2_LOG_ERROR(Log,
            "[beauty] Cannot load purchase list for character %d: character unavailable. No reply will be sent.",
            characterId
        );
        return false;
    }

    int beautyType = 0;
    int beautyUpperId = 0;
    int beautyId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&beautyType);
    connection.Bind(&beautyUpperId);
    connection.Bind(&beautyId);
    constexpr const wchar_t* statement = L"{Call lin_LoadBeautyPurchaseList(?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);

    CPacketHelperEx<20480> rows;
    int rowCount = 0;
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            rows.Assemble("ddd", beautyType, beautyUpperId, beautyId);
            ++rowCount;
        }
    }
    socket->Send(
        "chdcdb", 0xc6, 0xf2, characterId, static_cast<int>(requestFlag), rowCount, rows.GetLength(), rows.GetPacket()
    );
    return false;
}

bool RequestUpdateHairFlag(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x005834d4.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    char hairAccessoryFlag = 0;
    ::Disassemble(cursor, "c", &hairAccessoryFlag);
    const int databaseFlag = static_cast<unsigned char>(hairAccessoryFlag);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateHairAccInfo(?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&databaseFlag);
    connection.ExecutePrepared(statement);
    user->m_hairAccessoryFlag = hairAccessoryFlag == 1;
    return false;
}

bool RequestAcquirePotentialSkill(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005836f8.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int userSocketId = 0;
    int dualClass = 0;
    const unsigned char* cursor = ::Disassemble(packet, "ddd", &characterId, &userSocketId, &dualClass);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());

    auto sendFailure = [&]() {
        if (user) {
            user->ResetPotentialSkill(userSocketId, socket);
        }
        socket->Send("chdd", 0xc6, 0xf3, characterId, 0);
        return false;
    };
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "ServerPacketEx.cpp", 0x416f, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "ServerPacketEx.cpp", 0x416f, characterId)
        L2_LOG_ERROR(Log,
            "[skills] Cannot acquire potential skills for character %d, socket %d: character unavailable. "
            "Sending failure reply.", characterId, userSocketId
        );
        return sendFailure();
    }

    CPacketHelperEx<20480> echoPacket;
    int echoedValue = 0;
    cursor = ::Disassemble(cursor, "d", &echoedValue);
    echoPacket.Assemble("d", echoedValue);

    std::array<int, 3> pointTotals{};
    int acquiredPointDelta = 0;
    for (int potentialType = 1; potentialType <= 3; ++potentialType) {
        int skillCount = 0;
        cursor = ::Disassemble(cursor, "d", &skillCount);
        echoPacket.Assemble("d", skillCount);
        for (int index = 0; index < skillCount; ++index) {
            int skillId = 0;
            int skillLevel = 0;
            cursor = ::Disassemble(cursor, "dd", &skillId, &skillLevel);
            echoPacket.Assemble("dd", skillId, skillLevel);
            if (!user->SetAcquirePotentialSkill(skillId, skillLevel, dualClass, potentialType, acquiredPointDelta)) {
                return sendFailure();
            }
            pointTotals[static_cast<std::size_t>(potentialType - 1)] =
                runtime::AddInt32Wrapping(pointTotals[static_cast<std::size_t>(potentialType - 1)], skillLevel);
        }
    }

    const int requestedPointTotal = runtime::AddInt32Wrapping(runtime::AddInt32Wrapping(pointTotals[0], pointTotals[1]), pointTotals[2]);
    if (user->GetAP() < requestedPointTotal) {
        // Log.Add(LOG_ERROR, "qsSetAcquirePotentialPoint fail AP Point is not match")
        // L2_LOG_ERROR(Log, "qsSetAcquirePotentialPoint fail AP Point is not match")
        L2_LOG_WARN(Log,
            "[skills] Potential-skill request for character %d exceeds available AP: requested total %d "
            "across groups (%d, %d, %d). Attempting skill reset and sending failure reply.",
            characterId, requestedPointTotal, pointTotals[0], pointTotals[1], pointTotals[2]
        );
        return sendFailure();
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetPotentialPoint (?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&user->m_characterId);
    connection.BindParameter(&user->m_subjobId);
    connection.BindParameter(&pointTotals[0]);
    connection.BindParameter(&pointTotals[1]);
    connection.BindParameter(&pointTotals[2]);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "qsSetAcquirePotentialPoint fail")
        // L2_LOG_ERROR(Log, "qsSetAcquirePotentialPoint fail")
        L2_LOG_ERROR(Log,
            "[skills] Database request to save potential points for character %d failed: group totals (%d, %d, %d). "
            "Attempting skill reset and sending failure reply.",
            characterId, pointTotals[0], pointTotals[1], pointTotals[2]
        );
        return sendFailure();
    }

    const std::u16string characterName = std::u16string(user->GetCharName());
    const std::u16string accountName = std::u16string(user->GetAccountName());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x1fb5, user->GetId(), user->GetAccountID(), 0, 0, user->GetX(), user->GetY(), user->GetZ(), nullptr, nullptr,
        nullptr, user->GetRace(), user->GetGender(), user->GetClass(), user->GetLevel(), dualClass, user->m_subjobId,
        acquiredPointDelta, requestedPointTotal, user->GetAP(), 0, characterName.c_str(), accountName.c_str(), nullptr,
        nullptr, 0
    );
    socket->Send("chddb", 0xc6, 0xf3, characterId, 1, echoPacket.GetLength(), echoPacket.GetPacket());
    return false;
}

bool RequestResetPotentialSkill(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005842f4.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int userSocketId = 0;
    std::int64_t costSp = 0;
    ::Disassemble(packet, "ddQ", &characterId, &userSocketId, &costSp);
    auto sendResult = [&](int result) { socket->Send("chddQ", 0xc6, 0xf5, characterId, result, costSp); };

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "ServerPacketEx.cpp", 0x4187, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "ServerPacketEx.cpp", 0x4187, characterId)
        L2_LOG_ERROR(Log,
            "[skills] Cannot reset potential skills for character %d, socket %d: character unavailable. "
            "Requested SP cost %lld; sending failure reply.", characterId, userSocketId, static_cast<long long>(costSp)
        );
        sendResult(0);
        return false;
    }
    if (user->GetSP() < costSp) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]Not enough SP, id[%d] "
        //     "UserSP[%lld] CostSP[%lld]",
        //     "ServerPacketEx.cpp", 0x418e, characterId, static_cast<long long>(user->GetSP()), static_cast<long long>(costSp)
        // )
        // L2_LOG_ERROR(Log,
        //     "[%s][%d]Not enough SP, id[%d] "
        //     "UserSP[%lld] CostSP[%lld]",
        //     "ServerPacketEx.cpp", 0x418e, characterId, static_cast<long long>(user->GetSP()), static_cast<long long>(costSp)
        // )
        L2_LOG_WARN(Log,
            "[skills] Rejected potential-skill reset for character %d: available SP %lld is below requested cost %lld. "
            "Sending failure reply.", characterId, static_cast<long long>(user->GetSP()), static_cast<long long>(costSp)
        );
        sendResult(0);
        return false;
    }

    int zero;
    std::optional<DBConn> connectionStorage;
    {
        L2_WRITE_GUARD(*user.operator->());
        user->SetSP(runtime::SubtractInt64Wrapping(user->GetSP(), costSp));

        zero = 0;
        DBConn& connection = connectionStorage.emplace(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_SetPotentialPoint (?, ?, ?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&user->m_characterId);
        connection.BindParameter(&user->m_subjobId);
        connection.BindParameter(&zero);
        connection.BindParameter(&zero);
        connection.BindParameter(&zero);

        int result = 0;
        if (!connection.ExecutePrepared(statement)) {
            user->SetSP(runtime::AddInt64Wrapping(user->GetSP(), costSp));
            // Log.Add(LOG_ERROR, "qsSetAcquirePotentialPoint fail In ResetAbility")
            // L2_LOG_ERROR(Log, "qsSetAcquirePotentialPoint fail In ResetAbility")
            L2_LOG_ERROR(Log,
                "[skills] Database request to clear potential points for character %d failed. "
                "SP cost %lld was added back; sending failure reply.", characterId, static_cast<long long>(costSp)
            );
        } else if (!user->ResetPotentialSkill(userSocketId, socket)) {
            user->SetSP(runtime::AddInt64Wrapping(user->GetSP(), costSp));
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d]Fail to reset ability skill, "
            //     "id[%d] cost sp[%lld]",
            //     "ServerPacketEx.cpp", 0x41a3, characterId, static_cast<long long>(costSp)
            // )
            // L2_LOG_ERROR(Log,
            //     "[%s][%d]Fail to reset ability skill, "
            //     "id[%d] cost sp[%lld]",
            //     "ServerPacketEx.cpp", 0x41a3, characterId, static_cast<long long>(costSp)
            // )
            L2_LOG_ERROR(Log,
                "[skills] Potential-skill reset failed for character %d after the point-clear database request succeeded. "
                "SP cost %lld was added back; sending failure reply.", characterId, static_cast<long long>(costSp)
            );
        } else {
            result = 1;
        }
        sendResult(result);
    }
    user->Save();
    return false;
}

bool Request_RQ_EX_CHANGE_AP(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0054118c.
    L2_GUARD_FUNCTION();

    protocol::RQ_EX_CHANGE_AP request;
    if (!DecodePacket(request, packet))
        return false;

    CUserSP user = g_userDb.GetUser(request.characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "ServerPacketEx.cpp", 0x41c5, request.characterId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "ServerPacketEx.cpp", 0x41c5, request.characterId
        // )
        L2_LOG_ERROR(Log,
            "[skills] Cannot change AP for character %d: character unavailable. No reply will be sent.",
            request.characterId
        );
        return false;
    }

    protocol::RP_EX_CHANGE_AP response;
    response.characterId = request.characterId;
    response.abilityPoints = request.abilityPoints;
    response.saved = user->SaveAP(request.abilityPoints);
    SendPacket(socket, static_cast<RP_EX_PROTOCOL>(0xf6), response);
    return false;
}

bool RequestStartNewFIshingChampionship(CSocket* socket [[maybe_unused]], const unsigned char* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0052cf74 is a shipping guard-only stub.
    L2_GUARD_FUNCTION();
    return false;
}

bool RequestSaveFIshingChampionshipResult(CSocket* socket [[maybe_unused]], const unsigned char* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0052cff4 is a shipping guard-only stub.
    L2_GUARD_FUNCTION();
    return false;
}

bool RequestResetFIshingChampionship(CSocket* socket [[maybe_unused]], const unsigned char* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0052d074 is a shipping guard-only stub.
    L2_GUARD_FUNCTION();
    return false;
}

bool RequestGiveFIshingChampionshipReward(CSocket* socket [[maybe_unused]], const unsigned char* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0052d0f4 is a shipping guard-only stub.
    L2_GUARD_FUNCTION();
    return false;
}

bool RequestBrStartTrainingZone(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00584988.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &characterId);
    CacheDConfiguration& configuration = GetCacheDConfiguration();
    if (characterId < 0) {
        // The target's hidden configuration path applies a 32-bit NEG to the
        // first dword and returns without treating it as a character ID.
        configuration.trainingTimeInit = runtime::SubtractInt32Wrapping(0, characterId);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    int lastStartTime = 0;
    int remainingMinutes = 0;
    const int accountId = user->GetAccountID();
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&lastStartTime);
    connection.Bind(&remainingMinutes);
    constexpr const wchar_t* checkStatement = L"{Call lin_CheckTrainingZone(?)}";
    connection.Prepare(checkStatement);
    connection.BindParameter(&accountId);
    if (!connection.ExecutePrepared(checkStatement) || !connection.Fetch()) {
        return false;
    }

    if (lastStartTime < 1) {
        if (lastStartTime == 0) {
            remainingMinutes = configuration.trainingTimeLimit;
        } else if (lastStartTime == -1) {
            socket->Send("chdd", 0xc6, 0xf8, characterId, -1);
            return false;
        } else {
            remainingMinutes = 0;
        }
    } else {
        const int now = runtime::crt::Time32Now();
        const int adjustedNow = runtime::SubtractInt32Wrapping(now, configuration.trainingTimeInit);
        const int adjustedLastStart = runtime::SubtractInt32Wrapping(lastStartTime, configuration.trainingTimeInit);
        std::tm nowLocal{};
        std::tm lastStartLocal{};
        if (BrLocalTime32(adjustedNow, nowLocal) && BrLocalTime32(adjustedLastStart, lastStartLocal) &&
            (nowLocal.tm_year != lastStartLocal.tm_year || nowLocal.tm_mon != lastStartLocal.tm_mon ||
                nowLocal.tm_mday != lastStartLocal.tm_mday)) {
            remainingMinutes = configuration.trainingTimeLimit;
        }
    }

    if (remainingMinutes < 1) {
        socket->Send("chdd", 0xc6, 0xf8, characterId, 0);
        return false;
    }

    connection.UnBind();
    int x = 0;
    int y = 0;
    int z = 0;
    int startTime = 0;
    ::Disassemble(cursor, "dddd", &x, &y, &z, &startTime);
    const int subjobId = user->GetSubjobId();
    const int startLevel = user->GetLevel();
    constexpr const wchar_t* startStatement = L"{Call lin_StartTrainingZone(?,?,?,?,?,?,?,?,?)}";
    connection.Prepare(startStatement);
    connection.BindParameter(&accountId);
    connection.BindParameter(&startTime);
    connection.BindParameter(&remainingMinutes);
    connection.BindParameter(&characterId);
    connection.BindParameter(&subjobId);
    connection.BindParameter(&startLevel);
    connection.BindParameter(&x);
    connection.BindParameter(&y);
    connection.BindParameter(&z);
    if (connection.ExecutePrepared(startStatement)) {
        user->m_trainingZoneEndTime = runtime::AddInt32Wrapping(startTime, runtime::MultiplyInt32Wrapping(remainingMinutes, 60));
        socket->Send("chddd", 0xc6, 0xf8, characterId, startTime, remainingMinutes);
        WriteTrainingZoneStartAudit(*user.get(), x, y, z, remainingMinutes);
    }
    return false;
}

bool RequestBrEndTrainingZone(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0058517c.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int endReason = 0;
    int endTime = 0;
    ::Disassemble(packet, "ddd", &characterId, &endReason, &endTime);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    int x = 0;
    int y = 0;
    int z = 0;
    int trainingMinutes = 0;
    int remainingMinutes = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&x);
    connection.Bind(&y);
    connection.Bind(&z);
    connection.Bind(&trainingMinutes);
    connection.Bind(&remainingMinutes);
    const int accountId = user->GetAccountID();
    const double endTimeParameter = static_cast<double>(endTime);
    constexpr const wchar_t* statement = L"{Call lin_EndTrainingZone(?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&accountId);
    connection.BindParameter(&endTimeParameter);
    connection.BindParameter(&characterId);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        user->m_trainingZoneEndTime = 0;
        socket->Send("chddddddd", 0xc6, 0xf9, characterId, x, y, z, trainingMinutes, remainingMinutes, 0);
        WriteTrainingZoneEndAudit(*user.get(), trainingMinutes, remainingMinutes, x, y, z, endReason);
    }
    return false;
}

bool RequestBrRewardTrainingZone(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00585850.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    int result = 0;
    const int subjobId = user->GetSubjobId();
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    constexpr const wchar_t* statement = L"{Call lin_RewardTrainingZone(?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&subjobId);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        socket->Send("chdd", 0xc6, 0xfa, characterId, result);
    }
    return false;
}

bool RequestBrLoadTrainingZone(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00585a9c.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int requestKind = 0;
    int requestValue = 0;
    ::Disassemble(packet, "ddd", &characterId, &requestKind, &requestValue);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    DBConn connection(runtime::process::CurrentDatabaseId());
    if (requestKind == 1) {
        int result = 0;
        const int accountId = user->GetAccountID();
        const int remainingMinutes = runtime::SubtractInt32Wrapping(300, requestValue);
        connection.Bind(&result);
        constexpr const wchar_t* statement = L"{Call lin_TestTrainingZone(?,?)}";
        connection.Prepare(statement);
        connection.BindParameter(&accountId);
        connection.BindParameter(&remainingMinutes);
        if (connection.ExecutePrepared(statement) && connection.Fetch()) {
            socket->Send("chdd", 0xc6, 0xfb, characterId, result);
        }
        return false;
    }

    int state = 0;
    int trainingMinutes = 0;
    int startLevel = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    connection.Bind(&state);
    connection.Bind(&trainingMinutes);
    connection.Bind(&startLevel);
    connection.Bind(&x);
    connection.Bind(&y);
    connection.Bind(&z);
    constexpr const wchar_t* stateStatement = L"{Call lin_LoadTrainingZoneState(?)}";
    connection.Prepare(stateStatement);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(stateStatement) || !connection.Fetch()) {
        return false;
    }

    if (state == 0) {
        socket->Send("chdd", 0xc6, 0xfb, characterId, 0);
        return false;
    }
    if (state == 2) {
        socket->Send("chdddd", 0xc6, 0xfb, characterId, 2, trainingMinutes, startLevel);
        return false;
    }
    if (state != 1) {
        // Log.Add(LOG_ERROR, "Invalid training state [%d][%d]", characterId, state)
        // L2_LOG_ERROR(Log, "Invalid training state [%d][%d]", characterId, state)
        L2_LOG_ERROR(Log,
            "[training] Database returned unsupported state %d for character %d; expected 0, 1 or 2. No reply will be sent.",
            state, characterId
        );
        return false;
    }

    connection.UnBind();
    int startTime = 0;
    int totalMinutes = 0;
    const int accountId = user->GetAccountID();
    connection.Bind(&startTime);
    connection.Bind(&totalMinutes);
    constexpr const wchar_t* timeStatement = L"{Call lin_LoadTrainingZoneTime(?)}";
    connection.Prepare(timeStatement);
    connection.BindParameter(&accountId);
    if (!connection.ExecutePrepared(timeStatement) || !connection.Fetch()) {
        return false;
    }

    const int now = runtime::crt::Time32Now();
    const int elapsedWithGrace = runtime::AddInt32Wrapping(runtime::SubtractInt32Wrapping(now, startTime), 10);
    const int durationSeconds = runtime::MultiplyInt32Wrapping(totalMinutes, 60);
    if (elapsedWithGrace < durationSeconds) {
        socket->Send("chdddd", 0xc6, 0xfb, characterId, 1, startTime, totalMinutes);
        return false;
    }

    connection.UnBind();
    constexpr const wchar_t* endStatement = L"{Call lin_EndTrainingZoneForLogin(?,?,?)}";
    connection.Prepare(endStatement);
    connection.BindParameter(&accountId);
    connection.BindParameter(&characterId);
    connection.BindParameter(&totalMinutes);
    if (connection.ExecutePrepared(endStatement)) {
        user->m_trainingZoneEndTime = 0;
        socket->Send("chddddddd", 0xc6, 0xf9, characterId, x, y, z, totalMinutes, 0, 1);
        WriteTrainingZoneEndAudit(*user.get(), totalMinutes, 0, x, y, z, 1);
    }
    return false;
}

bool RequestPeriodicPremiumHennaJob(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00586804.
    L2_GUARD_FUNCTION();

    int job = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &job);
    if (job == 0) {
        int characterId = 0;
        int userSocketId = 0;
        ::Disassemble(cursor, "dd", &characterId, &userSocketId);
        CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
        if (user) {
            PeriodicPremiumHennaJobHandler::OnLoadInfo(socket, characterId, userSocketId);
        }
    } else if (job == 2) {
        int hennaId = 0;
        int characterId = 0;
        int userSocketId = 0;
        ::Disassemble(cursor, "ddd", &hennaId, &characterId, &userSocketId);
        CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
        if (user) {
            PeriodicPremiumHennaJobHandler::OnExpired(socket, hennaId, characterId, userSocketId);
        }
    } else if (job == 3) {
        int characterId = 0;
        int userSocketId = 0;
        int hennaId = 0;
        int periodMinutes = 0;
        ::Disassemble(cursor, "dddd", &characterId, &userSocketId, &hennaId, &periodMinutes);
        CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
        if (user) {
            PeriodicPremiumHennaJobHandler::OnSetPeriod(socket, characterId, userSocketId, hennaId, periodMinutes);
        }
    }
    return false;
}

bool RequestBrResetShopLimit(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00586a4c is a one-way database mutation.
    L2_GUARD_FUNCTION();

    int accountId = 0;
    int productId = 0;
    ::Disassemble(packet, "dd", &accountId, &productId);
    const int characterId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_BrResetLimitedShopItemInfo (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&accountId);
    connection.BindParameter(&characterId);
    connection.BindParameter(&productId);
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestAccountUserStamp(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00586bd0.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    char typeByte = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dc", &characterId, &typeByte);
    char updateByte = 1;
    cursor = ::Disassemble(cursor, "c", &updateByte);

    CUserSP user = g_userDb.FindResidentUser(characterId);
    if (!user) {
        // Log.Add(
        //     LOG_ERROR, "FILE[%s] LINE[%d] Cannot find user[%d]", "ServerPacketEx.cpp", 0x444f, characterId
        // )
        // L2_LOG_ERROR(Log, "FILE[%s] LINE[%d] Cannot find user[%d]", "ServerPacketEx.cpp", 0x444f, characterId
        // )
        L2_LOG_ERROR(Log,
            "[attendance] Cannot process account stamp request for character %d: resident character unavailable. "
            "No reply will be sent.", characterId
        );
        return false;
    }

    const int type = static_cast<int>(static_cast<std::int8_t>(typeByte));
    CAccountSP account = CAccount::Load(static_cast<unsigned int>(user->GetAccountID()));
    if (!account)
        return false;
    if (type != 0) {
        // Log.Add(LOG_ERROR, "FILE[%s] LINE[%d] unknown type!!!", "ServerPacketEx.cpp", 0x4449)
        // L2_LOG_ERROR(Log, "FILE[%s] LINE[%d] unknown type!!!", "ServerPacketEx.cpp", 0x4449)
        L2_LOG_WARN(Log,
            "[attendance] Rejected account stamp request for character %d: unsupported type %d; expected 0. "
            "No reply will be sent.", characterId, type
        );
        return false;
    }

    int maximumStamp = 0;
    int resetInterval = 0;
    int now = 0;
    int eventStart = 0;
    int eventEnd = 0;
    std::optional<std::map<int, int>> stampsStorage;
    int currentStamp;
    std::optional<CPacketHelperEx<20480>> logPacketStorage;
    {
        L2_WRITE_GUARD(*account.operator->());
        ::Disassemble(cursor, "ddddd", &maximumStamp, &resetInterval, &now, &eventStart, &eventEnd);

        auto& stamps = stampsStorage.emplace();
        account->LoadUserStamp(type, stamps);
        if (stamps.size() > 1) {
            // Log.Add(LOG_ERROR, "FILE[%s] LINE[%d] DB data corruption!!", "ServerPacketEx.cpp", 0x43de)
            // L2_LOG_ERROR(Log, "FILE[%s] LINE[%d] DB data corruption!!", "ServerPacketEx.cpp", 0x43de)
            L2_LOG_ERROR(Log,
                "[attendance] Account stamp load for character %d returned %llu entries; expected at most one. "
                "Continuing with the first map entry.", characterId, static_cast<unsigned long long>(stamps.size())
            );
        }

        std::tm localNow{};
        BrLocalTime32(now, localNow);
        localNow.tm_hour = 0;
        localNow.tm_min = 0;
        localNow.tm_sec = 0;
        int resetBoundary = runtime::AddInt32Wrapping(runtime::crt::Mktime32(&localNow), resetInterval);
        if (now < resetBoundary) {
            resetBoundary = runtime::SubtractInt32Wrapping(resetBoundary, 86400);
        }

        const bool update = updateByte == 1;
        currentStamp = 0;
        if (stamps.empty()) {
            if (update) {
                currentStamp = 1;
                account->SaveUserStamp(type, currentStamp, now);
                WriteAccountUserStampAudit(*user.get(), currentStamp, maximumStamp, 0, now);
            }
        } else {
            currentStamp = stamps.begin()->first;
            const int previousStampTime = stamps.begin()->second;
            if (previousStampTime < eventStart) {
                if (update) {
                    currentStamp = 1;
                    account->SaveUserStamp(type, currentStamp, now);
                    WriteAccountUserStampAudit(*user.get(), currentStamp, maximumStamp, previousStampTime, now);
                } else {
                    account->SaveUserStamp(type, 0, 0);
                }

                DBConn deleteConnection(runtime::process::CurrentDatabaseId());
                const int accountId = account->GetId();
                constexpr const wchar_t* deleteStatement = L"{Call lin_DeleteAccountUserStampLog(?, ?)}";
                deleteConnection.Prepare(deleteStatement);
                deleteConnection.BindParameter(&accountId);
                deleteConnection.BindParameter(&type);
                if (!deleteConnection.ExecutePrepared(deleteStatement)) {
                    // Log.Add(LOG_ERROR, "[%s][%d]Delete Error!", "ServerPacketEx.cpp", 0x4406)
                    // L2_LOG_ERROR(Log, "[%s][%d]Delete Error!", "ServerPacketEx.cpp", 0x4406)
                    L2_LOG_ERROR(Log,
                        "[attendance] Database request to delete stamp history for account %d, type %d failed "
                        "after the stamp reset was requested. Continuing request processing.", accountId, type
                    );
                }
            } else if (update && previousStampTime < resetBoundary) {
                currentStamp = std::min(runtime::AddInt32Wrapping(currentStamp, 1), maximumStamp);
                account->SaveUserStamp(type, currentStamp, now);
                WriteAccountUserStampAudit(*user.get(), currentStamp, maximumStamp, previousStampTime, now);
            }
        }

        auto& logPacket = logPacketStorage.emplace();
        account->MakeUserStampLogPacket(type, logPacket);
    }
    auto& logPacket = *logPacketStorage;
    socket->Send(
        "chdccddb", 0xc6, 0xfe, characterId, type, static_cast<int>(static_cast<std::int8_t>(updateByte)), currentStamp,
        logPacket.GetAssembledCount(), logPacket.GetLength(), logPacket.GetPacket()
    );
    return false;
}

bool RequestAccountUserStampBuilder(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005490b0.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int type = 0;
    int record = 0;
    ::Disassemble(packet, "ddd", &characterId, &type, &record);
    CUserSP user = g_userDb.FindResidentUser(characterId);
    if (!user)
        return false;
    CAccountSP account = CAccount::Load(static_cast<unsigned int>(user->GetAccountID()));
    if (!account || type != 0)
        return false;

    CPacketHelperEx<20480> logPacket;
    {
        L2_WRITE_GUARD(*account.operator->());
        const int now = runtime::crt::Time32Now();
        account->SaveUserStamp(type, record, now);
        account->MakeUserStampLogPacket(type, logPacket);
    }
    socket->Send(
        "chdccddb", 0xc6, 0xfe, characterId, type, 0, record, logPacket.GetAssembledCount(), logPacket.GetLength(),
        logPacket.GetPacket()
    );
    return false;
}

bool RequestDeleteAccountUserStampLogBuilder(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00587e00.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int type = 0;
    ::Disassemble(packet, "dd", &characterId, &type);
    CUserSP user = g_userDb.FindResidentUser(characterId);
    if (!user)
        return false;
    CAccountSP account = CAccount::Load(static_cast<unsigned int>(user->GetAccountID()));
    if (!account)
        return false;

    {
        L2_WRITE_GUARD(*account.operator->());
        {
            DBConn connection(runtime::process::CurrentDatabaseId());
            const int accountId = account->GetId();
            constexpr const wchar_t* statement = L"{Call lin_DeleteAccountUserStampLog(?, ?)}";
            connection.Prepare(statement);
            connection.BindParameter(&accountId);
            connection.BindParameter(&type);
            if (!connection.ExecutePrepared(statement)) {
                // Log.Add(LOG_ERROR, "[%s][%d]Delete Error!", "ServerPacketEx.cpp", 0x4496)
                // L2_LOG_ERROR(Log, "[%s][%d]Delete Error!", "ServerPacketEx.cpp", 0x4496)
                L2_LOG_ERROR(Log,
                    "[attendance] Builder request to delete stamp history failed in the database: account %d, "
                    "character %d, stamp type %d.", accountId, characterId, type
                );
            }
        }
    }

    if (type != 0)
        return false;
    std::map<int, int> stamps;
    account->LoadUserStamp(type, stamps);
    CPacketHelperEx<20480> logPacket;
    account->MakeUserStampLogPacket(type, logPacket);
    const int currentStamp = stamps.empty() ? 0 : stamps.begin()->first;
    socket->Send(
        "chdccddb", 0xc6, 0xfe, characterId, type, 0, currentStamp, logPacket.GetAssembledCount(), logPacket.GetLength(),
        logPacket.GetPacket()
    );
    return false;
}

bool RequestLoadWorldChatPoint(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00549418. This path uses the
    // resident-only CUser lookup and emits no failure reply.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int maximumCount = 0;
    ::Disassemble(packet, "dd", &characterId, &maximumCount);
    CUserSP user = g_userDb.FindResidentUser(characterId);
    if (!user) {
        // Log.Add(LOG_ERROR, "[WorldChat] user is not valid charid:[%d]", characterId)
        L2_LOG_ERROR(Log, "[WorldChat] user is not valid charid:[%d]", characterId);
        return false;
    }

    const int point = user->LoadWorldChatPoint(maximumCount);
    socket->Send("chdd", 0xc6, 0xff, characterId, point);
    return false;
}

bool RequestIncUsedWorldChatPoint(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0054956c is one-way and ignores the packet's
    // third integer after decoding it.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int increment = 0;
    int ignoredMaximumCount = 0;
    ::Disassemble(packet, "ddd", &characterId, &increment, &ignoredMaximumCount);
    CUserSP user = g_userDb.FindResidentUser(characterId);
    if (user)
        user->IncWorldChatPoint(increment);
    return false;
}

bool RequestResetWorldChatPoint(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0058829c binds a literal zero as the second SQL
    // input. The second decoded packet integer is discarded.
    L2_GUARD_FUNCTION();

    int nextResetTime = 0;
    int ignoredMaximumCount = 0;
    ::Disassemble(packet, "dd", &nextResetTime, &ignoredMaximumCount);

    const int maximumCount = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_ResetWorldChat(?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&nextResetTime);
    connection.BindParameter(&maximumCount);
    if (connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_NORMAL, "qsResetWorldChatPoint success")
        // L2_LOG_INFO(Log, "qsResetWorldChatPoint success")
        L2_LOG_INFO(Log,
            "[world chat] Reset database request completed; next reset time %d, maximum count %d.",
            nextResetTime, maximumCount
        );
    } else {
        // Log.Add(LOG_ERROR, "qsResetWorldChatPoint error")
        // L2_LOG_ERROR(Log, "qsResetWorldChatPoint error")
        L2_LOG_ERROR(Log,
            "[world chat] Reset database request failed; next reset time %d, maximum count %d.",
            nextResetTime, maximumCount
        );
    }
    return false;
}

bool RequestLoadWorldChatTime(CSocket* socket, const unsigned char* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0058842c ignores the request payload. On a
    // successful Execute it calls Fetch but sends the bound value regardless
    // of whether that Fetch produced a row.
    L2_GUARD_FUNCTION();

    int result = 0;
    const int sentinelCharacterId = -1;
    const int maximumCount = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    constexpr const wchar_t* statement = L"{Call lin_LoadWorldChat(?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&sentinelCharacterId);
    connection.BindParameter(&maximumCount);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "qsLoadWorldChatPoint error(RequestLoadWorldChatTime)")
        // L2_LOG_ERROR(Log, "qsLoadWorldChatPoint error(RequestLoadWorldChatTime)")
        L2_LOG_ERROR(Log, "[world chat] Database request to load reset time failed. No reply will be sent.");
        return false;
    }

    connection.Fetch();
    socket->Send("chd", 0xc6, 0x100, result);
    return false;
}

bool RequestLoadMatchingPoint(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00541648. Type 1 carries one additional reply
    // byte. Every row keeps its caller-supplied correlation identifier.
    L2_GUARD_FUNCTION();

    char typeByte = 0;
    const unsigned char* cursor = ::Disassemble(packet, "c", &typeByte);
    const auto typeValue = static_cast<unsigned char>(typeByte);
    const int wireType = static_cast<int>(static_cast<std::int8_t>(typeByte));
    if (typeValue >= static_cast<unsigned char>(MatchingPoint::MPT_MAX)) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] invalid matching point type: %d", "ServerPacketEx.cpp", 0x4619, wireType
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] invalid matching point type: %d", "ServerPacketEx.cpp", 0x4619, wireType
        // )
        L2_LOG_WARN(Log,
            "[matching] Rejected point-load request: type %d is unsupported; expected 0 through %d. Sending an empty reply.",
            wireType, static_cast<int>(MatchingPoint::MPT_MAX) - 1
        );
        socket->Send("chcd", 0xc6, 0x101, wireType, 0);
        return false;
    }

    char extraByte = 0;
    int requestedCount = 0;
    if (typeValue == MatchingPoint::MPT_OLYMPIAD) {
        cursor = ::Disassemble(cursor, "cd", &extraByte, &requestedCount);
    } else {
        cursor = ::Disassemble(cursor, "d", &requestedCount);
    }
    if (requestedCount < 1) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] invalid matching point count: %d", "ServerPacketEx.cpp", 0x4620, requestedCount
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] invalid matching point count: %d", "ServerPacketEx.cpp", 0x4620, requestedCount
        // )
        L2_LOG_WARN(Log,
            "[matching] Rejected point-load request: record count %d must be positive. Sending an empty reply.", requestedCount
        );
        socket->Send("chcd", 0xc6, 0x101, wireType, 0);
        return false;
    }

    std::array<char, 0x5000> rows{};
    int rowBytes = 0;
    for (int index = 0; index < requestedCount; ++index) {
        int correlationId = 0;
        int characterId = 0;
        cursor = ::Disassemble(cursor, "dd", &correlationId, &characterId);
        CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
        if (!user) {
            // The shipping call supplies only characterId to a format that
            // also contains %s/%d source placeholders. Replaying that
            // malformed vararg list would dereference the ID as a pointer.
            // Log.Add(LOG_ERROR, "can't find user, dbid: %d", characterId)
            // L2_LOG_ERROR(Log, "can't find user, dbid: %d", characterId)
            L2_LOG_ERROR(Log,
                "[matching] Cannot load points for character %d, correlation %d: character unavailable at record %d of %d. "
                "Sending an empty reply; previously assembled records will not be sent.",
                characterId, correlationId, index + 1, requestedCount
            );
            socket->Send("chcd", 0xc6, 0x101, wireType, 0);
            return false;
        }

        const std::int64_t point = user->GetMatchingPoint(static_cast<MatchingPoint::MATCHINGPOINT_TYPE>(typeValue));
        rowBytes += ::Assemble(rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dQ", correlationId, point);
    }

    if (typeValue == MatchingPoint::MPT_OLYMPIAD) {
        socket->Send(
            "chcdbc", 0xc6, 0x101, wireType, requestedCount, rowBytes, rows.data(),
            static_cast<int>(static_cast<std::int8_t>(extraByte))
        );
    } else {
        socket->Send("chcdb", 0xc6, 0x101, wireType, requestedCount, rowBytes, rows.data());
    }
    return false;
}

bool RequestSaveMatchingPoint(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00541a10 is one-way. A missing user terminates the
    // remaining batch, while unchanged points skip persistence.
    L2_GUARD_FUNCTION();

    char typeByte = 0;
    int requestedCount = 0;
    const unsigned char* cursor = ::Disassemble(packet, "cd", &typeByte, &requestedCount);
    const auto typeValue = static_cast<unsigned char>(typeByte);
    if (typeValue >= static_cast<unsigned char>(MatchingPoint::MPT_MAX)) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] invalid matching point type: %d", "ServerPacketEx.cpp", 0x4619,
        //     static_cast<int>(static_cast<std::int8_t>(typeByte))
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] invalid matching point type: %d", "ServerPacketEx.cpp", 0x4619,
        //     static_cast<int>(static_cast<std::int8_t>(typeByte))
        // )
        L2_LOG_WARN(Log,
            "[matching] Rejected point-save request: type %d is unsupported; expected 0 through %d.",
            static_cast<int>(static_cast<std::int8_t>(typeByte)), static_cast<int>(MatchingPoint::MPT_MAX) - 1
        );
        return false;
    }
    if (requestedCount < 1) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] invalid matching point count: %d", "ServerPacketEx.cpp", 0x4620, requestedCount
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] invalid matching point count: %d", "ServerPacketEx.cpp", 0x4620, requestedCount
        // )
        L2_LOG_WARN(Log,
            "[matching] Rejected point-save request: record count %d must be positive.", requestedCount
        );
        return false;
    }

    for (int index = 0; index < requestedCount; ++index) {
        int characterId = 0;
        std::int64_t point = 0;
        cursor = ::Disassemble(cursor, "dQ", &characterId, &point);
        CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
        if (!user) {
            // Current Cached.exe has the same malformed source-prefix
            // varargs here as in RequestLoadMatchingPoint.
            // Log.Add(LOG_ERROR, "fail to save matching point user dbid: %d", characterId)
            // L2_LOG_ERROR(Log, "fail to save matching point user dbid: %d", characterId)
            L2_LOG_ERROR(Log,
                "[matching] Cannot save points %lld for character %d: character unavailable at record %d of %d. "
                "Remaining records will not be processed.", static_cast<long long>(point), characterId, index + 1, requestedCount
            );
            break;
        }

        std::int64_t previousPoint = 0;
        if (user->SetMatchingPoint(static_cast<MatchingPoint::MATCHINGPOINT_TYPE>(typeValue), point, previousPoint)) {
            user->SaveMatchingPoint();
        }
    }
    return false;
}

bool RequestKamarokaRound(CSocket* socket, const unsigned char* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0052d174 ignores the request payload and replies
    // only after both Execute and Fetch succeed.
    L2_GUARD_FUNCTION();

    int round = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&round);
    constexpr const wchar_t* statement = L"{Call lin_LoadPremiumKamaRound}";
    connection.Prepare(statement);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        socket->Send("chd", 0xc6, 0x103, round);
    } else {
        // Log.Add(LOG_ERROR, "FILE[%s] LINE[%d] Cannot Load Kamaroka Round", "ServerPacketEx.cpp", 0x465f)
        // L2_LOG_ERROR(Log, "FILE[%s] LINE[%d] Cannot Load Kamaroka Round", "ServerPacketEx.cpp", 0x465f)
        // Previous: L2_LOG_ERROR(Log, "Cannot load Kamaroka round: database query failed or returned no row, no reply sent.")
        L2_LOG_ERROR(Log, "[Kamaroka] Cannot load the current round: the database query failed or returned no row; no reply sent.");
    }
    return false;
}

bool RequestKamarokaRankingList(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005885c8. Successful execution always produces a
    // reply, including an empty row buffer. Execution failure uses chd/zero.
    L2_GUARD_FUNCTION();

    int round = 0;
    int maximumRank = 0;
    ::Disassemble(packet, "dd", &round, &maximumRank);

    int rankNumber = 0;
    int partyNumber = 0;
    int score = 0;
    std::array<char16_t, 128> characterList{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&rankNumber);
    connection.Bind(&partyNumber);
    connection.Bind(&score);
    connection.Bind(characterList.data(), 0x100);
    constexpr const wchar_t* statement = L"{Call lin_LoadPremiumKamaRank(?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&round);
    connection.BindParameter(&maximumRank);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "FILE[%s] LINE[%d] Cannot Get Kamaroka Ranking", "ServerPacketEx.cpp", 0x4693)
        // L2_LOG_ERROR(Log, "FILE[%s] LINE[%d] Cannot Get Kamaroka Ranking", "ServerPacketEx.cpp", 0x4693)
        // Previous: L2_LOG_ERROR(Log, "Cannot load Kamaroka ranking for round %d, maximum rank %d: database query failed, sending empty ranking reply.", round, maximumRank)
        L2_LOG_ERROR(Log, "[Kamaroka] Cannot load the ranking for round %d, maximum rank %d: the database query failed; sending an empty ranking reply.", round, maximumRank);
        socket->Send("chd", 0xc6, 0x104, 0);
        return false;
    }

    std::array<char, 0x5000> rows{};
    int rowBytes = 0;
    int rowCount = 0;
    while (connection.Fetch()) {
        const int assembled = ::Assemble(
            rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dddS", rankNumber, partyNumber, score,
            characterList.data()
        );
        if (assembled > 0) {
            rowBytes += assembled;
            ++rowCount;
        }
    }
    socket->Send("chdb", 0xc6, 0x104, rowCount, rowBytes, rows.data());
    return false;
}

bool RequestKamarokaAddNewRecord(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00588874 and the paired current L2Server sender
    // fix the leader-ID, party number, score and UTF-16 party-list roles.
    L2_GUARD_FUNCTION();

    int round = 0;
    int score = 0;
    int leaderCharacterId = 0;
    const unsigned char* cursor = ::Disassemble(packet, "ddd", &round, &score, &leaderCharacterId);
    std::array<char16_t, 128> packetCharacterList{};
    cursor = ::Disassemble(cursor, "S", 0x100, packetCharacterList.data());
    const std::u16string characterList(packetCharacterList.data());

    int partyNumber = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&partyNumber);
    constexpr const wchar_t* partyStatement = L"{Call lin_SavePremiumKamaPartyRecord(?,?,?)}";
    connection.Prepare(partyStatement);
    connection.BindParameter(&round);
    connection.BindParameter(&score);
    connection.BindParameter(characterList.c_str());
    if (!connection.ExecutePrepared(partyStatement) || !connection.Fetch()) {
        // Log.Add(
        //     LOG_ERROR, "FILE[%s] LINE[%d] Kamaroka cannot recived partyNO.", "ServerPacketEx.cpp", 0x46cf
        // )
        // L2_LOG_ERROR(Log, "FILE[%s] LINE[%d] Kamaroka cannot recived partyNO.", "ServerPacketEx.cpp", 0x46cf
        // )
        // Previous: L2_LOG_ERROR(Log, "Cannot obtain Kamaroka party number for round %d, leader %d, score %d: save query failed or returned no row. Member records not attempted, no reply sent.", round, leaderCharacterId, score)
        L2_LOG_ERROR(Log, "[Kamaroka] Cannot obtain a party number for round %d, leader %d, score %d: the save query failed or returned no row. Member records were not attempted; no reply sent.", round, leaderCharacterId, score);
        return false;
    }

    if (leaderCharacterId == 0)
        return false;

    int memberCount = 0;
    cursor = ::Disassemble(cursor, "d", &memberCount);
    for (int index = 0; index < memberCount; ++index) {
        int memberCharacterId = 0;
        cursor = ::Disassemble(cursor, "d", &memberCharacterId);
        DBConn memberConnection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* memberStatement = L"{Call lin_SavePremiumKamaMemberRecord(?,?,?,?)}";
        memberConnection.Prepare(memberStatement);
        memberConnection.BindParameter(&round);
        memberConnection.BindParameter(&partyNumber);
        memberConnection.BindParameter(&memberCharacterId);
        memberConnection.BindParameter(&score);
        if (!memberConnection.ExecutePrepared(memberStatement)) {
            // Log.Add(
            //     LOG_ERROR, "FILE[%s] LINE[%d] Kamaroka Record Save Fail CharId[%d]", "ServerPacketEx.cpp", 0x46c7,
            //     memberCharacterId
            // )
            // L2_LOG_ERROR(Log, "FILE[%s] LINE[%d] Kamaroka Record Save Fail CharId[%d]", "ServerPacketEx.cpp", 0x46c7,
            //     memberCharacterId
            // )
            // Previous: L2_LOG_ERROR(Log, "Cannot save Kamaroka member record: database request failed for character %d, party %d, round %d, score %d, member %d of %d. Continuing, party reply will still be sent.", memberCharacterId, partyNumber, round, score, index + 1, memberCount)
            L2_LOG_ERROR(Log, "[Kamaroka] Cannot save the member record: the database request failed for character %d, party %d, round %d, score %d, member %d of %d. Continuing; the party reply will still be sent.", memberCharacterId, partyNumber, round, score, index + 1, memberCount);
        }
    }

    socket->Send("chdddS", 0xc6, 0x105, leaderCharacterId, partyNumber, score, packetCharacterList.data());
    return false;
}

bool RequestKamarokaPersonalRankInfo(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00588b9c. The paired current L2Server sender and
    // reply decoder fix both request integers and the reply
    // field order.
    L2_GUARD_FUNCTION();

    int round = 0;
    int characterId = 0;
    ::Disassemble(packet, "dd", &round, &characterId);

    int score = 0;
    int partyNumber = 0;
    std::array<char16_t, 128> characterList{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&score);
    connection.Bind(characterList.data(), 0x100);
    connection.Bind(&partyNumber);
    constexpr const wchar_t* statement = L"{Call lin_LoadPremiumKamaBestScore(?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&round);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
        score = -1;
        partyNumber = 0;
    }

    socket->Send("chdddS", 0xc6, 0x106, characterId, score, partyNumber, characterList.data());
    return false;
}

bool RequestUpdateUICount(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00588dc0 prepares and executes once per supplied
    // pair. A failed row is logged but does not stop later updates.
    L2_GUARD_FUNCTION();

    int count = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &count);
    DBConn connection(runtime::process::CurrentDatabaseId());
    for (int index = 0; index < count; ++index) {
        int uiId = 0;
        int uiCount = 0;
        cursor = ::Disassemble(cursor, "dd", &uiId, &uiCount);
        constexpr const wchar_t* statement = L"{Call lin_UpdateUICount(?,?)}";
        connection.Prepare(statement);
        connection.BindParameter(&uiId);
        connection.BindParameter(&uiCount);
        if (!connection.ExecutePrepared(statement)) {
            // Log.Add(
            //     LOG_ERROR, "FILE[%s] LINE[%d] UI Event Count Update fail.", "ServerPacketEx.cpp", 0x470d
            // )
            // L2_LOG_ERROR(Log, "FILE[%s] LINE[%d] UI Event Count Update fail.", "ServerPacketEx.cpp", 0x470d
            // )
            L2_LOG_ERROR(Log,
                "[events] Database request to set UI event %d count to %d failed at record %d of %d. "
                "Continuing with remaining records.", uiId, uiCount, index + 1, count
            );
        }
    }
    return false;
}

bool RequestLoadClientINI(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00588f84 emits one reply for every fetched INI
    // row and has no terminal empty marker.
    L2_GUARD_FUNCTION();

    int accountId = 0;
    ::Disassemble(packet, "d", &accountId);

    short iniType = 0;
    std::array<char, 0x3c01> iniText{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&iniType);
    connection.Bind(iniText.data(), static_cast<int>(iniText.size()));
    constexpr const wchar_t* statement = L"{Call lin_LoadClientIni(?)}";
    connection.Prepare(statement);
    connection.BindParameter(&accountId);
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            socket->Send("chdhs", 0xc6, 0x107, accountId, static_cast<int>(iniType), iniText.data());
        }
    }
    return false;
}

bool RequestSaveClientINI(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00589170 ignores the execution result and sends no
    // acknowledgement.
    L2_GUARD_FUNCTION();

    int accountId = 0;
    short iniType = 0;
    std::array<char, 0x3c01> iniText{};
    ::Disassemble(packet, "dhs", &accountId, &iniType, static_cast<int>(iniText.size()), iniText.data());

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveClientIni(?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&accountId);
    connection.BindParameter(&iniType);
    connection.BindParameter(iniText.data());
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestLoadDroidNPCAppearance(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00544348. The 0x150-byte carrier layout and every
    // field below are fixed by the current encoder, the paired
    // current L2Server decoder, and TotalChecker2015.pdb's kept type.
    L2_GUARD_FUNCTION();

    int command = 0;
    int requestContext = 0;
    int accountId = 0;
    int characterId = 0;
    ::Disassemble(packet, "dddd", &command, &requestContext, &accountId, &characterId);

    const auto sendFailure = [&]() { socket->Send("chddc", 0xc6, 0x10c, command, requestContext, 0); };

    CAccountSP account = CAccount::Load(static_cast<unsigned int>(accountId));
    if (!account) {
        sendFailure();
        return false;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     "[NO_ERROR][DroidNPC][LoadAccount] accountId[%d] TID[%d] "
    //     "char[%d] time[%u]",
    //     accountId, CThreadManager::GetThreadID(), characterId, static_cast<unsigned int>(runtime::GetTickCount() - loadBegin)
    // )
    // Noise: unconditional account-load timing is not an error and adds a record to every appearance request.
    // L2_LOG_ERROR(Log,
    //     "[NO_ERROR][DroidNPC][LoadAccount] accountId[%d] TID[%d] "
    //     "char[%d] time[%u]",
    //     accountId, CThreadManager::GetThreadID(), characterId, static_cast<unsigned int>(runtime::GetTickCount() - loadBegin)
    // )

    struct SelectDroidNPCCharacter {
        bool Do(CUser* candidate) {
            if (threadId >= 0 && threadId < ThreadID::MAX) {
                ++g_nDroidNPCCharMapLoop[threadId];
            }
            if (!candidate || candidate->m_deletionTime.wYear != 0 || candidate->m_loadedInfo.status != 0 ||
                static_cast<int>(candidate->GetId()) != characterId) {
                return true;
            }
            selected = candidate;
            return false;
        }

        int characterId = 0;
        int threadId = 0;
        CUser* selected = nullptr;
    };
    std::optional<SelectDroidNPCCharacter> selectorStorage;
    std::optional<CWareHouseSP> inventoryStorage;
    std::optional<std::array<int, 60>> itemIdsStorage;
    std::optional<std::array<CItemSP, 60>> itemsStorage;
    std::optional<CItemSP> weaponStorage;
    std::optional<protocol::RP_Load_DroidNPC_Appearance> appearanceStorage;
    std::optional<std::array<unsigned char, 0x5000>> encodedStorage;
    std::optional<network::Stream> streamStorage;
    {
        L2_READ_GUARD(*account.operator->());
        const int charMapThreadId = CThreadManager::GetThreadID();
        if (charMapThreadId >= 0 && charMapThreadId < ThreadID::MAX) {
            g_nDroidNPCCharMapLoop[charMapThreadId] = 0;
        }
        auto& selector = selectorStorage.emplace(SelectDroidNPCCharacter{characterId, charMapThreadId, nullptr});
        // Current 0x00544348 uses the in-owner
        // FOR_ALL_OBJECT_IN_OBJECT_MAP expansion, including this exact lock
        // source/line. It does not perform a direct key lookup.
        account->GetChars().FOR_ALL(&selector, L"ServerPacketEx.cpp", 0x47f7);
        CUser* const user = selector.selected;
        // Log.Add(
        //     LOG_ERROR,
        //     "[NO_ERROR][DroidNPC][LoopChar] accountId[%d] TID[%d] "
        //     "char[%d] time[%u]",
        //     accountId, CThreadManager::GetThreadID(), characterId, static_cast<unsigned int>(runtime::GetTickCount() - loopBegin)
        // )
        // Noise: unconditional character-map timing is not an error. The lookup and its counters remain intact.
        // L2_LOG_ERROR(Log,
        //     "[NO_ERROR][DroidNPC][LoopChar] accountId[%d] TID[%d] "
        //     "char[%d] time[%u]",
        //     accountId, CThreadManager::GetThreadID(), characterId, static_cast<unsigned int>(runtime::GetTickCount() - loopBegin)
        // )
        if (!user) {
            sendFailure();
            return false;
        }

        CWareHouseSP& inventory = inventoryStorage.emplace(user->GetWareHouse(0, true));
        if (!inventory) {
            sendFailure();
            return false;
        }

        auto& itemIds = itemIdsStorage.emplace();
        static_assert(sizeof(itemIds) == sizeof(UserEquipmentSlots));
        std::memcpy(itemIds.data(), &user->m_equipment, sizeof(itemIds));
        auto& items = itemsStorage.emplace();
        {
            L2_READ_GUARD(*inventory.operator->());
            for (std::size_t index = 0; index < items.size(); ++index) {
                items[index] = inventory->GetItem(itemIds[index]);
            }
            weaponStorage.emplace(items[7] ? items[7] : items[14]);
        }
        CItemSP& weapon = *weaponStorage;

        int pledgeCrestId = 0;
        int pledgeEmblemId = 0;
        const int pledgeId = static_cast<int>(user->GetPledgeID());
        if (pledgeId != 0) {
            CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
            if (pledge) {
                pledgeCrestId = static_cast<int>(pledge->crestId());
                pledgeEmblemId = static_cast<int>(pledge->m_newEmblemId);
            }
        }

        const int allianceId = user->GetAllianceID();
        int allianceCrestId = 0;
        if (allianceId != 0) {
            CAllianceSP alliance = g_allianceDb.GetAlliance(
                static_cast<std::uint32_t>(allianceId), static_cast<std::uint32_t>(runtime::process::CurrentDatabaseId())
            );
            if (alliance) {
                allianceCrestId = static_cast<int>(alliance->crestId());
            }
        }

        const auto copyFixedName = [](std::array<char16_t, 24>& destination, const char16_t* source) {
            const std::u16string_view converted(source);
            const std::size_t count = std::min<std::size_t>(destination.size() - 1, converted.size());
            std::copy_n(converted.data(), count, destination.data());
            destination[count] = u'\0';
        };
        const auto itemType = [&](std::size_t index) { return items[index] ? static_cast<int>(items[index]->ItemType()) : 0; };
        const auto appearanceType = [&](std::size_t index) { return items[index] ? items[index]->AppearanceType() : 0; };
        const auto enchant = [&](std::size_t index) { return items[index] ? items[index]->Enchant() : 0; };

        auto& appearance = appearanceStorage.emplace();
        copyFixedName(appearance.szCharName, user->GetCharName());
        copyFixedName(appearance.szNickName, user->m_nickname.c_str());
        appearance.cGender = static_cast<std::int8_t>(user->GetGender());
        appearance.cRace = static_cast<std::int8_t>(user->GetRace());
        appearance.nClass = user->GetClass();
        appearance.cJobGroup = static_cast<std::int8_t>(user->m_loadedInfo.jobGroup);
        appearance.cHero = user->IsHero();
        appearance.cNobless = user->IsNobless();
        appearance.nAlign = user->GetAlign();
        appearance.nPledgeType = static_cast<int>(user->GetPledgeType());
        appearance.nPledgeDBID = pledgeId;
        appearance.nPledgeCrestDBID = pledgeCrestId;
        appearance.nPledgeEmblemDBID = pledgeEmblemId;
        appearance.nAllianceDBID = allianceId;
        appearance.nAllianceCrestDBID = allianceCrestId;
        // Current 0x00545ce7..0x00545d08 uses the same base order as login.
        appearance.nFace = user->m_hairColor;
        appearance.nHairShape = user->m_hairShape;
        appearance.nHairColor = user->m_face;
        appearance.nFaceEx = user->m_faceIndexEx;
        appearance.nHairShapeEx = user->m_hairShapeIndexEx;
        appearance.nHairColorEx = user->m_hairColorIndexEx;
        appearance.cHairAccFlag = user->m_hairAccessoryFlag;

        appearance.nSlotUnderware = itemType(0);
        appearance.nSlotHead = itemType(6);
        appearance.nSlotRightHand = itemType(7);
        appearance.nSlotLeftHand = itemType(8);
        appearance.nSlotBothHands = itemType(14);
        appearance.nSlotGloves = itemType(9);
        appearance.nSlotChest = itemType(10);
        appearance.nSlotLegs = itemType(11);
        appearance.nSlotFeet = itemType(12);
        appearance.nSlotBack = itemType(13);
        appearance.nSlotHair = itemType(15);
        appearance.nSlotHair2 = itemType(16);

        appearance.nShapeShiftRightHand = appearanceType(7);
        appearance.nShapeShiftLeftHand = appearanceType(8);
        appearance.nShapeShiftBothHands = appearanceType(14);
        appearance.nShapeShiftGloves = appearanceType(9);
        appearance.nShapeShiftChest = appearanceType(10);
        appearance.nShapeShiftLegs = appearanceType(11);
        appearance.nShapeShiftFeet = appearanceType(12);
        appearance.nShapeShiftHair = appearanceType(15);
        appearance.nShapeShfitHair2 = appearanceType(16);

        appearance.nDeco1 = itemType(24);
        appearance.nDeco2 = itemType(25);
        appearance.nDeco3 = itemType(26);
        appearance.nDeco4 = itemType(27);
        appearance.nDeco5 = itemType(28);
        appearance.nDeco6 = itemType(29);

        appearance.nEnchantWeapon = weapon ? weapon->Enchant() : 0;
        appearance.nEnchantChest = enchant(10);
        appearance.nEnchantLegs = enchant(11);
        appearance.nEnchantHead = enchant(6);
        appearance.nEnchantGloves = enchant(9);
        appearance.nEnchantFeet = enchant(12);
        if (weapon) {
            appearance.nVariationOpt1 = weapon->OptionKey().variationOption1;
            appearance.nVariationOpt2 = weapon->OptionKey().variationOption2;
        }
        appearance.nNickNameColor = user->m_nicknameColor;
        appearance.nCharNameColor = user->m_characterNameColor;
        appearance.nKarma = user->GetAlign();
        appearance.nAP = user->GetAP();
        appearance.nBonusCount = user->m_bonusCount;

        auto& encoded = encodedStorage.emplace();
        auto& stream = streamStorage.emplace(encoded.data(), static_cast<int>(encoded.size()));
        if (!appearance.Encode(&stream)) {
            // Log.Add(LOG_ERROR, "[%s][%d] packet.Encode() failed", "ServerPacketEx.cpp", 0x48c9)
            // L2_LOG_ERROR(Log, "[%s][%d] packet.Encode() failed", "ServerPacketEx.cpp", 0x48c9)
            L2_LOG_ERROR(Log,
                "[droid NPC] Cannot encode appearance for character %d, account %d, command %d, request %d. "
                "Sending failure reply.", characterId, accountId, command, requestContext
            );
            sendFailure();
            return false;
        }

        const int encodedSize = static_cast<int>(stream.GetCursor() - stream.GetBuffer());
        // Current Cached.exe 0x005472c4..0x00547314 places encodedSize in both
        // the explicit `d` lane and the `b` length lane. The paired current
        // L2Server consumes the first value as the decoder stream extent. The
        // character ID is request-only and is not present in this reply.
        socket->Send("chddcdb", 0xc6, 0x10c, command, requestContext, 1, encodedSize, encodedSize, encoded.data());
    }
    return false;
}

bool RequestLoadHonorsSkill(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0054967c looks up only the resident current-world
    // user, reloads the map, then sends the target's non-empty-only list.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.FindResidentUser(characterId);
    if (user) {
        user->LoadHonorsSkill();
        user->SendHonorsSkillList(socket);
    }
    return false;
}

bool RequestSetHonorsSkill(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x005497d0. The successful mutation emits record
    // 0x191 directly through the shared formatter and has no socket reply.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int skillId = 0;
    int skillLevel = 0;
    ::Disassemble(packet, "ddd", &characterId, &skillId, &skillLevel);
    CUserSP user = g_userDb.FindResidentUser(characterId);
    if (user && user->SetHonorsSkill(skillId, skillLevel)) {
        const std::u16string characterName = std::u16string(user->GetCharName());
        const std::u16string accountName = std::u16string(user->GetAccountName());
        CLogFormatter formatter;
        formatter.WriteLog(
            0x191, user->GetId(), user->GetAccountID(), 0, 0, user->GetX(), user->GetY(), user->GetZ(), nullptr, nullptr,
            nullptr, user->GetRace(), user->GetGender(), user->GetClass(), user->GetLevel(), skillId,
            static_cast<std::uint16_t>(skillLevel), user->GetSP(), user->GetBuilder(), 0, 0, characterName.c_str(),
            accountName.c_str(), nullptr, nullptr, 0
        );
    }
    return false;
}

bool RequestDeleteHonorsSkill(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00549cdc records the old resident level only when
    // the database-backed deletion succeeds.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int skillId = 0;
    ::Disassemble(packet, "dd", &characterId, &skillId);
    CUserSP user = g_userDb.FindResidentUser(characterId);
    if (user) {
        const int oldSkillLevel = user->GetHonorsSkillLevel(skillId);
        if (user->DeleteHonorsSkill(skillId)) {
            const std::u16string characterName = std::u16string(user->GetCharName());
            const std::u16string accountName = std::u16string(user->GetAccountName());
            CLogFormatter formatter;
            formatter.WriteLog(
                0x192, user->GetId(), user->GetAccountID(), 0, 0, user->GetX(), user->GetY(), user->GetZ(), nullptr, nullptr,
                nullptr, user->GetRace(), user->GetGender(), user->GetClass(), user->GetLevel(), skillId, user->GetBuilder(),
                oldSkillLevel, 0, 0, 0, characterName.c_str(), accountName.c_str(), nullptr, nullptr, 0
            );
        }
    }
    return false;
}

bool RequestResetHonorsSkill(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0054a22c performs only the resident lookup and
    // CUser reset. No packet is sent and no audit record is written.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.FindResidentUser(characterId);
    if (user)
        user->ResetHonorsSkill();
    return false;
}

bool RequestUpdateWedding(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00589318 acknowledges only a successful start/end.
    // Ending the wedding then calls the existing friendship owner.
    L2_GUARD_FUNCTION();

    int firstCharacterId = 0;
    int secondCharacterId = 0;
    char updateType = 0;
    ::Disassemble(packet, "ddc", &firstCharacterId, &secondCharacterId, &updateType);

    DBConn connection(runtime::process::CurrentDatabaseId());
    const wchar_t* statement = nullptr;
    if (updateType == 1) {
        statement = L"{Call lin_StartWedding (?,?)}";
    } else if (updateType == 2) {
        statement = L"{Call lin_EndWedding (?,?)}";
    } else {
        return false;
    }

    connection.Prepare(statement);
    connection.BindParameter(&firstCharacterId);
    connection.BindParameter(&secondCharacterId);
    if (connection.ExecutePrepared(statement)) {
        socket->Send("chddc", 0xc6, 0x109, firstCharacterId, secondCharacterId, static_cast<int>(updateType));
        if (updateType == 2) {
            g_friendsDb.BreakFriendship(
                static_cast<unsigned int>(firstCharacterId), static_cast<unsigned int>(secondCharacterId)
            );
        }
    }
    return false;
}

bool RequestLoadWedding(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0058958c replies only when the procedure returns
    // one non-negative spouse ID.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);

    int spouseCharacterId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&spouseCharacterId);
    constexpr const wchar_t* statement = L"{Call lin_LoadWedding (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (connection.ExecutePrepared(statement) && connection.Fetch() && spouseCharacterId >= 0) {
        socket->Send("chdd", 0xc6, 0x10a, characterId, spouseCharacterId);
    }
    return false;
}

bool RequestLoadFriendDetailInfo(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00541d0c sends no reply when the requested ID is
    // not in the source character's CFriends map.
    L2_GUARD_FUNCTION();

    unsigned int characterId = 0;
    int userServerId = 0;
    unsigned int friendCharacterId = 0;
    ::Disassemble(packet, "ddd", &characterId, &userServerId, &friendCharacterId);

    CPacketHelperEx<20480> detailPacket;
    g_friendsDb.MakeFriendDetailPacket(characterId, friendCharacterId, detailPacket);
    if (detailPacket.GetLength() != 0) {
        socket->Send("chdb", 0xc6, 0x10b, userServerId, detailPacket.GetLength(), detailPacket.GetPacket());
    }
    return false;
}

bool Request_RQ_EX_SIGNIN_PLEDGE(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00541e90. This path remains distinct
    // from the primary join request and calls CPledgeDb::SigninPledge.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    int pledgeServerId = 0;
    int characterId = 0;
    int pledgeId = 0;
    PledgeType pledgeType = MASTER_PLEDGE;
    ::Disassemble(packet, "ddddd", &userServerId, &pledgeServerId, &characterId, &pledgeId, &pledgeType);

    std::array<char16_t, 24> pledgeName{};
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (pledge) {
        L2_READ_GUARD(*pledge.operator->());
        std::copy_n(pledge->GetName(), pledgeName.size(), pledgeName.begin());
    }

    CUserSP existingUser = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!existingUser) {
        SendSigninPledgeReply(socket, userServerId, pledgeServerId, pledgeType, 0);
        return false;
    }

    if (!g_pledgeDb.SigninPledge(
            static_cast<std::uint32_t>(pledgeId), static_cast<std::uint32_t>(characterId), pledgeType
        )) {
        SendSigninPledgeReply(socket, userServerId, pledgeServerId, pledgeType, 0);
        return false;
    }

    SendSigninPledgeReply(socket, userServerId, pledgeServerId, pledgeType, 1);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        const std::u16string characterName = std::u16string(user->GetCharName());
        const std::u16string accountName = std::u16string(user->GetAccountName());
        CLogFormatter formatter;
        formatter.WriteLog(
            0xca, user->GetId(), user->GetAccountID(), 0, 0, user->GetX(), user->GetY(), user->GetZ(), pledgeName.data(),
            u"", u"", user->GetRace(), user->GetGender(), user->GetClass(), user->GetLevel(), static_cast<int>(pledgeType),
            pledgeId, 0, 0, 0, 0, characterName.c_str(), accountName.c_str(), u"", u"", 0
        );

        // GIP._Add(
        //     L"CMD_JoinClan(%d, %d, %d, %d, %d, '%s', %d", GetCacheDConfiguration().worldId, pledgeId,
        //     static_cast<std::int64_t>(std::time(nullptr)), user->GetAccountID(), user->GetId(), user->GetCharName(),
        //     static_cast<int>(pledgeType)
        // )
        GIP._Add(
            L"CMD_JoinClan(%d, %d, %d, %d, %d, '%s', %d", GetCacheDConfiguration().worldId, pledgeId,
            static_cast<std::int64_t>(std::time(nullptr)), user->GetAccountID(), user->GetId(), Utf16ToWide(user->GetCharName()).c_str(),
            static_cast<int>(pledgeType)
        );
    }
    PledgeContributionInCache::AddInfo(pledgeId, characterId);
    return false;
}

bool RequestLoadFactionPoint(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00589b70 and typed encoder 0x00543c84.
    L2_GUARD_FUNCTION();

    protocol::RQ_LOAD_FACTION_POINT request;
    if (!DecodePacket(request, packet)) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] packet.Decode() failed, packet id[%d]", "ServerPacketEx.cpp", 0x498b, 0x19e
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] packet.Decode() failed, packet id[%d]", "ServerPacketEx.cpp", 0x498b, 0x19e
        // )
        L2_LOG_WARN(Log,
            "[factions] Cannot decode faction-point load request (opcode 0x19e). Request rejected; no reply will be sent."
        );
        return false;
    }

    CUserSP user = g_userDb.GetUser(request.nUserDBID, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] Cannot find user, char id[%d]", "ServerPacketEx.cpp", 0x4993, request.nUserDBID
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] Cannot find user, char id[%d]", "ServerPacketEx.cpp", 0x4993, request.nUserDBID
        // )
        L2_LOG_ERROR(Log,
            "[factions] Cannot load faction points for character %d: character unavailable. No reply will be sent.",
            request.nUserDBID
        );
        return false;
    }

    int factionType = 0;
    long long factionPoint = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&factionType);
    connection.Bind(&factionPoint);
    constexpr const wchar_t* statement = L"{Call lin_LoadFactionPoint (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&request.nUserDBID);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] Cannot Execute Query, qsAddCharFactionPoint", "ServerPacketEx.cpp", 0x49a0
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] Cannot Execute Query, qsAddCharFactionPoint", "ServerPacketEx.cpp", 0x49a0
        // )
        L2_LOG_ERROR(Log,
            "[factions] Database request to load faction points for character %d failed. No reply will be sent.",
            request.nUserDBID
        );
        return false;
    }

    protocol::RP_LOAD_FACTION_POINT response;
    response.nUserServerId = request.nUserServerId;
    while (connection.Fetch()) {
        response.factionList.push_back(protocol::FactionPointInfo{factionType, static_cast<std::int64_t>(factionPoint)});
    }
    SendPacket(socket, RP_EX_LOAD_FACTION_POINT, response);
    return false;
}

bool RequestUpdateFactionPoint(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00589730. nReason is reply-only. The SQL adapter
    // receives command, user DBID, faction type, point and maximum point.
    L2_GUARD_FUNCTION();

    protocol::RQ_UPDATE_FACTION_POINT request;
    if (!DecodePacket(request, packet)) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] packet.Decode() failed, packet id[%d]", "ServerPacketEx.cpp", 0x4946, 0x19f
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] packet.Decode() failed, packet id[%d]", "ServerPacketEx.cpp", 0x4946, 0x19f
        // )
        L2_LOG_WARN(Log,
            "[factions] Cannot decode faction-point update request (opcode 0x19f). Request rejected; no reply will be sent."
        );
        return false;
    }

    CUserSP user = g_userDb.GetUser(request.nUserDBID, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] Cannot find user, char id[%d]", "ServerPacketEx.cpp", 0x494e, request.nUserDBID
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] Cannot find user, char id[%d]", "ServerPacketEx.cpp", 0x494e, request.nUserDBID
        // )
        L2_LOG_ERROR(Log,
            "[factions] Cannot update faction points for character %d: character unavailable. No reply will be sent.",
            request.nUserDBID
        );
        return false;
    }

    long long updatedPoint = 0;
    const int command = static_cast<int>(request.cCommand);
    const long long requestedPoint = request.nFaction.nPoint;
    const long long maximumPoint = request.nMaxPoint;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&updatedPoint);
    constexpr const wchar_t* statement = L"{Call lin_UpdateFactionPoint (?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&command);
    connection.BindParameter(&request.nUserDBID);
    connection.BindParameter(&request.nFaction.nType);
    connection.BindParameter(&requestedPoint);
    connection.BindParameter(&maximumPoint);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] Cannot Execute Query, qsUpdateCharFactionPoint", "ServerPacketEx.cpp", 0x495d
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] Cannot Execute Query, qsUpdateCharFactionPoint", "ServerPacketEx.cpp", 0x495d
        // )
        L2_LOG_ERROR(Log,
            "[factions] Database request to update faction points for character %d failed: command %d, "
            "requested points %lld, maximum %lld. No reply will be sent.",
            request.nUserDBID, command, requestedPoint, maximumPoint
        );
        return false;
    }
    if (!connection.Fetch()) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] Cannot Fetch Query, qsUpdateCharFactionPoint", "ServerPacketEx.cpp", 0x4964
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] Cannot Fetch Query, qsUpdateCharFactionPoint", "ServerPacketEx.cpp", 0x4964
        // )
        L2_LOG_ERROR(Log,
            "[factions] Faction-point update returned no result row for character %d: command %d, "
            "requested points %lld, maximum %lld. No reply will be sent.",
            request.nUserDBID, command, requestedPoint, maximumPoint
        );
        return false;
    }

    protocol::RP_UPDATE_FACTION_POINT response;
    response.cCommand = request.cCommand;
    response.nReason = request.nReason;
    response.nUserServerId = request.nUserServerId;
    response.nFaction.nType = request.nFaction.nType;
    response.nFaction.nPoint = updatedPoint;
    SendPacket(socket, RP_EX_UPDATE_FACTION_POINT, response);
    return false;
}

bool RequestLoadBalthusEventUserInfo(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005808e4.
    L2_GUARD_FUNCTION();

    protocol::RQ_LOAD_BALTHUS_EVENT_USER_INFO request;
    if (!DecodePacket(request, packet)) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] packet.Decode() failed, packet id[%d]", "ServerPacketEx.cpp", 0x39a9, 0x1a0
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] packet.Decode() failed, packet id[%d]", "ServerPacketEx.cpp", 0x39a9, 0x1a0
        // )
        L2_LOG_WARN(Log,
            "[Balthus] Cannot decode event-info load request. Request rejected; no reply will be sent."
        );
        return false;
    }

    int remainTokenCount = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&remainTokenCount);
    constexpr const wchar_t* statement = L"{Call lin_LoadBalthusEventUserInfo (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&request.nUserDBID);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] Cannot Execute Query, "
        //     "qsLoadBalthusEventUserInfo ",
        //     "ServerPacketEx.cpp", 0x39b4
        // )
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] Cannot Execute Query, "
        //     "qsLoadBalthusEventUserInfo ",
        //     "ServerPacketEx.cpp", 0x39b4
        // )
        L2_LOG_ERROR(Log,
            "[Balthus] Database request to load remaining tokens for character %d failed. No reply will be sent.",
            request.nUserDBID
        );
        return false;
    }
    if (!connection.Fetch()) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] Cannot Fetch Query, "
        //     "qsLoadBalthusEventUserInfo ",
        //     "ServerPacketEx.cpp", 0x39ba
        // )
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] Cannot Fetch Query, "
        //     "qsLoadBalthusEventUserInfo ",
        //     "ServerPacketEx.cpp", 0x39ba
        // )
        L2_LOG_ERROR(Log,
            "[Balthus] Remaining-token query returned no row for character %d. No reply will be sent.", request.nUserDBID
        );
        return false;
    }

    protocol::RP_LOAD_BALTHUS_EVENT_USER_INFO response;
    response.nUserSID = request.nUserSID;
    response.nRemainToken = remainTokenCount == -1 ? 0 : remainTokenCount;
    SendPacket(socket, RP_EX_LOAD_BALTHUS_EVENT_USER_INFO, response);
    return false;
}

bool RequestUpdateBalthusEventUserInfo(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00580bf4 opens one DBConn per list element and
    // stops at the first failed execution.
    L2_GUARD_FUNCTION();

    protocol::RQ_UPDATE_BALTHUS_EVENT_USER_INFO request;
    if (!DecodePacket(request, packet)) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] packet.Decode() failed, packet id[%d]", "ServerPacketEx.cpp", 0x39dd, 0x1a0
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] packet.Decode() failed, packet id[%d]", "ServerPacketEx.cpp", 0x39dd, 0x1a0
        // )
        L2_LOG_WARN(Log, "[Balthus] Cannot decode event-info update request. No token updates will be processed."
        );
        return false;
    }

    constexpr const wchar_t* statement = L"{Call lin_UpdateBalthusEventUserInfo (?,?)}";
    for (const protocol::BalthusEventUserInfo& userInfo : request.userInfoList) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Prepare(statement);
        connection.BindParameter(&userInfo.nUserDBID);
        connection.BindParameter(&userInfo.nRemainTokenCount);
        if (!connection.ExecutePrepared(statement)) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d] Cannot Execute Query, "
            //     "qsUpdateBalthusEventUserInfo : "
            //     "nCharDbId[%d], Token[%d]",
            //     "ServerPacketEx.cpp", 0x39e9, userInfo.nUserDBID, userInfo.nRemainTokenCount
            // )
            // L2_LOG_ERROR(Log,
            //     "[%s][%d] Cannot Execute Query, "
            //     "qsUpdateBalthusEventUserInfo : "
            //     "nCharDbId[%d], Token[%d]",
            //     "ServerPacketEx.cpp", 0x39e9, userInfo.nUserDBID, userInfo.nRemainTokenCount
            // )
            L2_LOG_ERROR(Log,
                "[Balthus] Database request to set remaining tokens to %d for character %d failed. "
                "Remaining entries in this request will not be processed.", userInfo.nRemainTokenCount, userInfo.nUserDBID
            );
            return false;
        }
    }
    return false;
}

bool RequestUpdateUserInzonePoint(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0058a394. The four-int SQL helper establishes the
    // non-struct order: type ID, user DBID, current time, point.
    L2_GUARD_FUNCTION();

    protocol::RQ_UPDATE_USER_INZONE_POINT request;
    if (!DecodePacket(request, packet)) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] packet.Decode() failed, packet id[%d]", "ServerPacketEx.cpp", 0x4a02, 0x1a3
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] packet.Decode() failed, packet id[%d]", "ServerPacketEx.cpp", 0x4a02, 0x1a3
        // )
        L2_LOG_WARN(Log, "[instances] Cannot decode character point-update request. Update rejected."
        );
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateUserInzonePoint(?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&request.nInzoneTypeId);
    connection.BindParameter(&request.nUserDBID);
    connection.BindParameter(&request.nCurTime);
    connection.BindParameter(&request.nPoint);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] RequestUpdateUserInzonePoint fail, packet", "ServerPacketEx.cpp", 0x4a09
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] RequestUpdateUserInzonePoint fail, packet", "ServerPacketEx.cpp", 0x4a09
        // )
        L2_LOG_ERROR(Log,
            "[instances] Database point update failed for character %d, instance type %d: points %d, update time %d.",
            request.nUserDBID, request.nInzoneTypeId, request.nPoint, request.nCurTime
        );
    }
    return false;
}

bool RequestLoadUserInzonePoint(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0058a014 and typed encoder 0x00543d80.
    L2_GUARD_FUNCTION();

    protocol::RQ_LOAD_USER_INZONE_POINT request;
    if (!DecodePacket(request, packet)) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] packet.Decode() failed, packet id[%d]", "ServerPacketEx.cpp", 0x49cd, 0x1a3
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] packet.Decode() failed, packet id[%d]", "ServerPacketEx.cpp", 0x49cd, 0x1a3
        // )
        L2_LOG_WARN(Log,
            "[instances] Cannot decode character point-load request. Request rejected; no reply will be sent."
        );
        return false;
    }

    int inzoneTypeId = 0;
    int point = 0;
    int updateTime = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&inzoneTypeId);
    connection.Bind(&point);
    connection.Bind(&updateTime);
    constexpr const wchar_t* statement = L"{Call lin_LoadUserInzonePoint(?)}";
    connection.Prepare(statement);
    connection.BindParameter(&request.nUserDBID);
    if (!connection.ExecutePrepared(statement))
        return false;

    protocol::RP_LOAD_USER_INZONE_POINT response;
    response.nUserSID = request.nUserSID;
    response.nUserDBID = request.nUserDBID;
    while (connection.Fetch()) {
        response.userInzonePointList.push_back(protocol::UserInzonePoint{inzoneTypeId, point, updateTime});
    }
    SendPacket(socket, RP_EX_LOAD_USER_INZONE_POINT, response);
    return false;
}

bool Request_RQ_EX_WOLYMPIAD_LOAD_ALL_USER(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0054b344 emits pages of at most 100 users. An
    // exact multiple is followed by the target's empty terminating page.
    L2_GUARD_FUNCTION();

    protocol::RQ_EX_WOLYMPIAD_LOAD_ALL_USER request;
    if (!DecodePacket(request, packet))
        return false;

    protocol::PkWOlympiadUser row;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&row.nWorldID);
    connection.Bind(&row.nUserDBID);
    connection.Bind(&row.nHeroType);
    connection.Bind(&row.nWinCount);
    connection.Bind(&row.nPreviousPoint);
    connection.Bind(&row.nOlympiadPoint);
    connection.Bind(&row.nMatchCount);
    connection.Bind(&row.nOlympiadWinCount);
    connection.Bind(&row.nOlympiadLoseCount);
    connection.Bind(&row.nOlympiadTradePoint);
    connection.Bind(&row.nMatchPoint);
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadLoadAllUser}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement))
        return false;

    protocol::RP_EX_WOLYMPIAD_LOAD_ALL_USER response;
    response.bStartUp = request.bStartUp;
    response.bNewSeason = request.bNewSeason;
    bool pageWasFull = false;
    do {
        response.vUsers.clear();
        pageWasFull = false;
        while (response.vUsers.size() != 100U && connection.Fetch()) {
            response.vUsers.push_back(row);
        }
        if (response.vUsers.size() == 100U) {
            pageWasFull = true;
        }
        ++response.nPacketNum;
        SendPacket(socket, RP_EX_WOLYMPIAD_LOAD_ALL_USER, response);
    } while (pageWasFull);
    return false;
}

bool Request_RQ_EX_WOLYMPIAD_GET_MATCH_COUNT(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0054b65c. nReserve is decoded by the shipping
    // packet class but does not constrain the all-user query.
    L2_GUARD_FUNCTION();

    protocol::RQ_EX_WOLYMPIAD_GET_MATCH_COUNT request;
    if (!DecodePacket(request, packet))
        return false;

    protocol::PkWOlympiadMatchCount row;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&row.nWorldID);
    connection.Bind(&row.nUserDBID);
    connection.Bind(&row.nSeason);
    connection.Bind(&row.nStep);
    connection.Bind(&row.nGameRule);
    connection.Bind(&row.nMatchCount);
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadLoadMatchCount}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement))
        return false;

    protocol::RP_EX_WOLYMPIAD_GET_MATCH_COUNT response;
    bool pageWasFull = false;
    do {
        response.vMatchCounts.clear();
        while (response.vMatchCounts.size() != 100U && connection.Fetch()) {
            response.vMatchCounts.push_back(row);
        }
        pageWasFull = response.vMatchCounts.size() == 100U;
        SendPacket(socket, RP_EX_WOLYMPIAD_GET_MATCH_COUNT, response);
    } while (pageWasFull);
    return false;
}

bool Request_RQ_EX_WOLYMPIAD_GET_MATCH_RESULT(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0058a548. The exact 25-code-unit rival-name field
    // and the raw 0x48-byte result row survive in TotalChecker2015.pdb.
    L2_GUARD_FUNCTION();

    protocol::RQ_EX_WOLYMPIAD_GET_MATCH_RESULT request;
    if (!DecodePacket(request, packet))
        return false;

    protocol::PkWOlympiadMatchResult row;
    std::array<char16_t, 25> rivalName{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&row.nMatchTime);
    connection.Bind(rivalName.data(), 0x32);
    connection.Bind(&row.nIsWinner);
    connection.Bind(&row.nElapsedTime);
    connection.Bind(&row.nRivalClassType);
    connection.Bind(&row.nGameRule);
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadGetMatchResult (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&request.nWorldID);
    connection.BindParameter(&request.nUserDBID);
    connection.BindParameter(&request.nSeason);
    if (!connection.ExecutePrepared(statement))
        return false;

    protocol::RP_EX_WOLYMPIAD_GET_MATCH_RESULT response;
    response.nWorldID = request.nWorldID;
    response.nUserDBID = request.nUserDBID;
    bool pageWasFull = false;
    do {
        response.vResults.clear();
        while (response.vResults.size() != 100U && connection.Fetch()) {
            row.szRivalName.fill(u'\0');
            const std::u16string packetName = std::u16string(rivalName.data());
            std::copy_n(packetName.data(), std::min(packetName.size(), row.szRivalName.size()), row.szRivalName.data());
            response.vResults.push_back(row);
        }
        pageWasFull = response.vResults.size() == 100U;
        SendPacket(socket, RP_EX_WOLYMPIAD_GET_MATCH_RESULT, response);
    } while (pageWasFull);
    return false;
}

bool Request_RQ_EX_WOLYMPIAD_MATCHING_POINT_LOAD(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0058a85c opens one DB connection per user key and
    // emits one final response. An empty key list still emits an empty reply.
    L2_GUARD_FUNCTION();

    protocol::RQ_EX_WOLYMPIAD_MATCHING_POINT_LOAD request;
    if (!DecodePacket(request, packet))
        return false;

    protocol::RP_EX_WOLYMPIAD_MATCHING_POINT_LOAD response;
    response.cGameRule = request.cGameRule;
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadMatchingPointLoad (?, ?)}";
    for (const protocol::PkWOlympiadUserKey& key : request.vKeys) {
        long long point = 0;
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Bind(&point);
        connection.Prepare(statement);
        connection.BindParameter(&key.nWorldID);
        connection.BindParameter(&key.nUserDBID);
        if (connection.ExecutePrepared(statement) && connection.Fetch()) {
            response.vPoints.push_back(
                protocol::PkWOlympiadMatchingPoint{key.nWorldID, key.nUserDBID, static_cast<std::int64_t>(point)}
            );
        }
    }
    SendPacket(socket, RP_EX_WOLYMPIAD_MATCHING_POINT_LOAD, response);
    return false;
}

bool RequestSaveCategoryData(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0058add8 keeps the nested
    // type/category/object stream and aborts at the first failed DB write.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* deleteStatement = L"{Call lin_DeleteClassCategoryDB}";
    connection.Prepare(deleteStatement);
    if (!connection.ExecutePrepared(deleteStatement)) {
        // Log.Add(LOG_ERROR, "[%s][%d]delete class category db fail", "ServerPacketEx.cpp", 0x4c21)
        // L2_LOG_ERROR(Log, "[%s][%d]delete class category db fail", "ServerPacketEx.cpp", 0x4c21)
        L2_LOG_ERROR(Log,
            "[categories] Database request to clear class categories failed. Replacement data will not be inserted."
        );
        return false;
    }
    connection.UnBind();

    char encodedTypeCount = 0;
    const unsigned char* cursor = ::Disassemble(packet, "c", &encodedTypeCount);
    const int typeCount = static_cast<unsigned char>(encodedTypeCount);
    constexpr const wchar_t* addStatement = L"{Call lin_AddClassCategoryDB(?,?,?)}";
    for (int typeIndex = 0; typeIndex < typeCount; ++typeIndex) {
        char encodedType = 0;
        int categoryCount = 0;
        cursor = ::Disassemble(cursor, "cd", &encodedType, &categoryCount);
        const int categoryType = static_cast<unsigned char>(encodedType);
        for (int categoryIndex = 0; categoryIndex < categoryCount; ++categoryIndex) {
            int categoryId = 0;
            int objectCount = 0;
            cursor = ::Disassemble(cursor, "dd", &categoryId, &objectCount);
            for (int objectIndex = 0; objectIndex < objectCount; ++objectIndex) {
                int objectId = 0;
                cursor = ::Disassemble(cursor, "d", &objectId);
                connection.Prepare(addStatement);
                connection.BindParameter(&categoryType);
                connection.BindParameter(&categoryId);
                connection.BindParameter(&objectId);
                if (!connection.ExecutePrepared(addStatement)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "[%s][%d]qsAddClassCategoryDB db fail "
                    //     "[%d][%d][%d]",
                    //     "ServerPacketEx.cpp", 0x4c3d, categoryType, categoryId, objectId
                    // )
                    // L2_LOG_ERROR(Log,
                    //     "[%s][%d]qsAddClassCategoryDB db fail "
                    //     "[%d][%d][%d]",
                    //     "ServerPacketEx.cpp", 0x4c3d, categoryType, categoryId, objectId
                    // )
                    L2_LOG_ERROR(Log,
                        "[categories] Database insert failed for category type %d, category %d, object %d "
                        "after the category-clear request succeeded. Remaining replacement entries will not be processed.",
                        categoryType, categoryId, objectId
                    );
                    return false;
                }
                connection.UnBind();
            }
        }
    }
    return false;
}

bool RequestUpdateOneDayRewardInfo(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0058b0b8. REWARDED supplies the current time,
    // TEST carries one trailing explicit time and every other update uses 0.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int updateType = 0;
    int dailyMissionDbId = 0;
    int updateCheckTime = 0;
    int accumulateCount = 0;
    int stateType = 0;
    const unsigned char* cursor = ::Disassemble(
        packet, "dddddd", &characterId, &updateType, &dailyMissionDbId, &updateCheckTime, &accumulateCount, &stateType
    );

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "ServerPacketEx.cpp", 0x4c77, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "ServerPacketEx.cpp", 0x4c77, characterId)
        L2_LOG_ERROR(Log,
            "[missions] Cannot update daily mission %d for character %d: character unavailable. Update type %d skipped.",
            dailyMissionDbId, characterId, updateType
        );
        return false;
    }

    int timeRewarded = 0;
    if (updateType == 1) {
        timeRewarded = runtime::crt::Time32Now();
    } else if (updateType == 3) {
        ::Disassemble(cursor, "d", &timeRewarded);
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateOneDayRewardInfo(?,?,?,?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&updateType);
    connection.BindParameter(&dailyMissionDbId);
    connection.BindParameter(&timeRewarded);
    connection.BindParameter(&updateCheckTime);
    connection.BindParameter(&accumulateCount);
    connection.BindParameter(&stateType);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "[%s][%d] OneDayRewardInfo Update Error!!", "ServerPacketEx.cpp", 0x4c6e)
        // L2_LOG_ERROR(Log, "[%s][%d] OneDayRewardInfo Update Error!!", "ServerPacketEx.cpp", 0x4c6e)
        L2_LOG_ERROR(Log,
            "[missions] Database request to update daily mission %d for character %d failed: "
            "update type %d, count %d, state %d.", dailyMissionDbId, characterId, updateType, accumulateCount, stateType
        );
    }
    return false;
}

bool RequestLoadAutoSoulShotUI(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0058b338. The paired PDB identifies each of the
    // four interleaved values as AutoSoulShotUi::{m_ItemClassID,
    // m_IsActiveState}. The target tests the first item against -1.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int userServerId = 0;
    ::Disassemble(packet, "dd", &characterId, &userServerId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "ServerPacketEx.cpp", 0x4cb2, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "ServerPacketEx.cpp", 0x4cb2, characterId)
        L2_LOG_ERROR(Log,
            "[soulshots] Cannot load automatic-use settings for character %d (server object %d): "
            "character unavailable. No reply will be sent.", characterId, userServerId
        );
        return false;
    }

    std::array<int, 4> itemClassIds{-1, 0, 0, 0};
    std::array<int, 4> activeStates{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    for (std::size_t slot = 0; slot < itemClassIds.size(); ++slot) {
        connection.Bind(&itemClassIds[slot]);
        connection.Bind(&activeStates[slot]);
    }
    constexpr const wchar_t* statement = L"{Call lin_LoadAutoSoulShotUI(?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }

    CSocket* const replySocket = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (replySocket) {
        replySocket->Send(
            "chdddddddddd", 0xc6, RP_EX_LOAD_AUTO_SOULSHOT_UI, userServerId, itemClassIds[0] != -1 ? 1 : 0, itemClassIds[0],
            activeStates[0], itemClassIds[1], activeStates[1], itemClassIds[2], activeStates[2], itemClassIds[3],
            activeStates[3]
        );
    }
    return false;
}

bool RequestSaveAutoSoulShotUI(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0058b644 persists all four AutoSoulShotUi values
    // in their exact item/state wire order and emits no response.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    std::array<int, 4> itemClassIds{};
    std::array<int, 4> activeStates{};
    ::Disassemble(
        packet, "ddddddddd", &characterId, &itemClassIds[0], &activeStates[0], &itemClassIds[1], &activeStates[1],
        &itemClassIds[2], &activeStates[2], &itemClassIds[3], &activeStates[3]
    );
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "ServerPacketEx.cpp", 0x4ce2, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "ServerPacketEx.cpp", 0x4ce2, characterId)
        L2_LOG_ERROR(Log,
            "[soulshots] Cannot save automatic-use settings for character %d: character unavailable. Save skipped.",
            characterId
        );
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveAutoSoulShotUI(?,?,?,?,?,?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    for (std::size_t slot = 0; slot < itemClassIds.size(); ++slot) {
        connection.BindParameter(&itemClassIds[slot]);
        connection.BindParameter(&activeStates[slot]);
    }
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] RequestSaveAutoSoulShotUI Update Error!!", "ServerPacketEx.cpp", 0x4cdc
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] RequestSaveAutoSoulShotUI Update Error!!", "ServerPacketEx.cpp", 0x4cdc
        // )
        L2_LOG_ERROR(Log,
            "[soulshots] Database request to save automatic-use settings for character %d failed.", characterId
        );
    }
    return false;
}

bool RequestInitUserPledgeBonus(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0058b8dc and the paired L2Server sender establish
    // PledgeBonusInfo's exact index/current/max/save-time/reward-level order.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int pledgeId = 0;
    int bonusIndex = 0;
    int currentBonusPoint = 0;
    int maxBonusPoint = 0;
    int lastSaveTime = 0;
    int todayRewardLevel = 0;
    ::Disassemble(
        packet, "ddddddd", &characterId, &pledgeId, &bonusIndex, &currentBonusPoint, &maxBonusPoint, &lastSaveTime,
        &todayRewardLevel
    );

    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (pledge) {
        pledge->SetPledgeBonus(bonusIndex, currentBonusPoint, maxBonusPoint, lastSaveTime, todayRewardLevel);
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_InitPledgeBonus(?, ?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&bonusIndex);
    connection.BindParameter(&currentBonusPoint);
    connection.BindParameter(&maxBonusPoint);
    connection.BindParameter(&lastSaveTime);
    connection.BindParameter(&todayRewardLevel);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]RequestInitPledgeBonus DB Error"
        //     "[%d(%d)][%d][%d][%d/%d]",
        //     "ServerPacketEx.cpp", 0x4d0a, characterId, pledgeId, bonusIndex, currentBonusPoint, maxBonusPoint, lastSaveTime
        // )
        // L2_LOG_ERROR(Log,
        //     "[%s][%d]RequestInitPledgeBonus DB Error"
        //     "[%d(%d)][%d][%d][%d/%d]",
        //     "ServerPacketEx.cpp", 0x4d0a, characterId, pledgeId, bonusIndex, currentBonusPoint, maxBonusPoint, lastSaveTime
        // )
        L2_LOG_ERROR(Log,
            "[clan bonuses] Database initialization failed for clan %d, bonus %d, character %d: "
            "points %d, maximum %d, save time %d. Cached clan update was %s; character reward update skipped.",
            pledgeId, bonusIndex, characterId, currentBonusPoint, maxBonusPoint, lastSaveTime,
            pledge ? "requested" : "skipped because the clan was unavailable"
        );
        return false;
    }

    if (currentBonusPoint > 0 && bonusIndex == 0) {
        connection.UnBind();
        const int activity = 0;
        constexpr const wchar_t* userStatement = L"{Call lin_UpdateUserPledgeBonus(?, ?, ?)}";
        connection.Prepare(userStatement);
        connection.BindParameter(&characterId);
        connection.BindParameter(&activity);
        connection.BindParameter(&lastSaveTime);
        if (!connection.ExecutePrepared(userStatement)) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d]RequestInitUserPledgeBonus DB Error"
            //     "[%d(%d)][%d][%d/%d]",
            //     "ServerPacketEx.cpp", 0x4d04, characterId, pledgeId, bonusIndex, currentBonusPoint, lastSaveTime
            // )
            // L2_LOG_ERROR(Log,
            //     "[%s][%d]RequestInitUserPledgeBonus DB Error"
            //     "[%d(%d)][%d][%d/%d]",
            //     "ServerPacketEx.cpp", 0x4d04, characterId, pledgeId, bonusIndex, currentBonusPoint, lastSaveTime
            // )
            L2_LOG_ERROR(Log,
                "[clan bonuses] Database character reward update failed for character %d, clan %d, activity 0, "
                "save time %d after clan bonus initialization succeeded.", characterId, pledgeId, lastSaveTime
            );
        }
    }
    return false;
}

bool RequestUpdateUserPledgeBonus(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0058bc0c. The paired current L2Server sender
    // fixes the six request values and their order.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int pledgeId = 0;
    int bonusIndex = 0;
    int lastSaveTime = 0;
    int addedPoint = 0;
    int maxBonusPoint = 0;
    ::Disassemble(packet, "dddddd", &characterId, &pledgeId, &bonusIndex, &lastSaveTime, &addedPoint, &maxBonusPoint);

    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (pledge) {
        pledge->SetPledgeBonus(
            bonusIndex, runtime::AddInt32Wrapping(pledge->m_pledgeBonus[bonusIndex].m_nCurrentBonusPoint, addedPoint), maxBonusPoint,
            lastSaveTime, -1
        );
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdatePledgeBonus(?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&bonusIndex);
    connection.BindParameter(&addedPoint);
    connection.BindParameter(&maxBonusPoint);
    connection.BindParameter(&lastSaveTime);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]RequestUpdatePledgeBonus DB Error"
        //     "[%d(%d)][%d][%d][%d/%d]",
        //     "ServerPacketEx.cpp", 0x4d2e, characterId, pledgeId, bonusIndex, addedPoint, maxBonusPoint, lastSaveTime
        // )
        // L2_LOG_ERROR(Log,
        //     "[%s][%d]RequestUpdatePledgeBonus DB Error"
        //     "[%d(%d)][%d][%d][%d/%d]",
        //     "ServerPacketEx.cpp", 0x4d2e, characterId, pledgeId, bonusIndex, addedPoint, maxBonusPoint, lastSaveTime
        // )
        L2_LOG_ERROR(Log,
            "[clan bonuses] Database update failed for clan %d, bonus %d, character %d: "
            "added points %d, maximum %d, save time %d. Cached clan update was %s; character reward update skipped.",
            pledgeId, bonusIndex, characterId, addedPoint, maxBonusPoint, lastSaveTime,
            pledge ? "requested" : "skipped because the clan was unavailable"
        );
        return false;
    }

    if (bonusIndex == 0) {
        connection.UnBind();
        const int activity = 0;
        constexpr const wchar_t* userStatement = L"{Call lin_UpdateUserPledgeBonus(?, ?, ?)}";
        connection.Prepare(userStatement);
        connection.BindParameter(&characterId);
        connection.BindParameter(&activity);
        connection.BindParameter(&lastSaveTime);
        if (!connection.ExecutePrepared(userStatement)) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d]RequestUpdateUserPledgeBonus DB Error"
            //     "[%d(%d)][%d][%d/%d]",
            //     "ServerPacketEx.cpp", 0x4d28, characterId, pledgeId, bonusIndex, addedPoint, lastSaveTime
            // )
            // L2_LOG_ERROR(Log,
            //     "[%s][%d]RequestUpdateUserPledgeBonus DB Error"
            //     "[%d(%d)][%d][%d/%d]",
            //     "ServerPacketEx.cpp", 0x4d28, characterId, pledgeId, bonusIndex, addedPoint, lastSaveTime
            // )
            L2_LOG_ERROR(Log,
                "[clan bonuses] Database character reward update failed for character %d, clan %d, activity 0, "
                "save time %d after the clan bonus update succeeded.", characterId, pledgeId, lastSaveTime
            );
        }
    }
    return false;
}

bool RequestUpdateUserPledgeBonusReward(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0058bf2c. Activity 3 is the shipping reset-all
    // command and performs three independent procedure calls.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int activity = 0;
    int receivedTime = 0;
    ::Disassemble(packet, "ddd", &characterId, &activity, &receivedTime);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateUserPledgeBonus(?, ?, ?)}";
    const int beginActivity = activity == 3 ? 0 : activity;
    const int endActivity = activity == 3 ? 3 : activity + 1;
    for (int currentActivity = beginActivity; currentActivity < endActivity; ++currentActivity) {
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.BindParameter(&currentActivity);
        connection.BindParameter(&receivedTime);
        if (!connection.ExecutePrepared(statement)) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d]RequestUpdateUserPledgeBonusReward DB Error"
            //     "[%d][%d][%d]",
            //     "ServerPacketEx.cpp", activity == 3 ? 0x4d46 : 0x4d4e, characterId, activity, receivedTime
            // )
            // L2_LOG_ERROR(Log,
            //     "[%s][%d]RequestUpdateUserPledgeBonusReward DB Error"
            //     "[%d][%d][%d]",
            //     "ServerPacketEx.cpp", activity == 3 ? 0x4d46 : 0x4d4e, characterId, activity, receivedTime
            // )
            L2_LOG_ERROR(Log,
                "[clan bonuses] Database reward update failed for character %d, activity %d, received time %d "
                "(requested activity %d). Continuing with remaining activities.",
                characterId, currentActivity, receivedTime, activity
            );
        }
    }
    return false;
}

bool RequestLoadUserPledgeBonus(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0058c1b8.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_LoadUserPledgeBonus(?, ?)}";
    for (int activity = 0; activity < 3; ++activity) {
        int receivedTime = 0;
        connection.Bind(&receivedTime);
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.BindParameter(&activity);
        if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
            // Log.Add(
            //     LOG_ERROR, "[%s][%d]RequestLoadUserPledgeBonus DB Error[%d(%d)]", "ServerPacketEx.cpp", 0x4d70,
            //     characterId, activity
            // )
            // L2_LOG_ERROR(Log, "[%s][%d]RequestLoadUserPledgeBonus DB Error[%d(%d)]", "ServerPacketEx.cpp", 0x4d70,
            //     characterId, activity
            // )
            L2_LOG_ERROR(Log,
                "[clan bonuses] Reward query failed or returned no row for character %d, activity %d. "
                "No reply for this activity; continuing with remaining activities.", characterId, activity
            );
        } else if (receivedTime > 0 && runtime::crt::Time32Now() - receivedTime < 86400) {
            socket->Send("chddd", 0xc6, RP_EX_LOAD_USER_PLEDGE_BONUS, characterId, activity, receivedTime);
            connection.UnBind();
        } else {
            connection.UnBind();
        }
    }
    return false;
}

bool RequestLoadUserMonsterBook(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0058c3d4.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]RequestLoadUserMonsterBook DB Error[%d]", "ServerPacketEx.cpp", 0x4daa, characterId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]RequestLoadUserMonsterBook DB Error[%d]", "ServerPacketEx.cpp", 0x4daa, characterId
        // )
        L2_LOG_ERROR(Log,
            "[monster book] Cannot load records for character %d: character unavailable. No reply will be sent.", characterId
        );
        return false;
    }

    int cardId = 0;
    int trophyLevel = 0;
    int killCount = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&cardId);
    connection.Bind(&trophyLevel);
    connection.Bind(&killCount);
    constexpr const wchar_t* statement = L"{Call lin_LoadUserMonsterBook(?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]qsLoadUserMonsterBook DB Error[%d]", "ServerPacketEx.cpp", 0x4da4, characterId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]qsLoadUserMonsterBook DB Error[%d]", "ServerPacketEx.cpp", 0x4da4, characterId
        // )
        L2_LOG_ERROR(Log,
            "[monster book] Database request to load records for character %d failed. No reply will be sent.", characterId
        );
        return false;
    }

    CPacketHelperEx<20480> rows;
    while (connection.Fetch()) {
        rows.Assemble(
            "hcd", static_cast<int>(static_cast<short>(cardId)), static_cast<int>(static_cast<char>(trophyLevel)), killCount
        );
        if (rows.GetLength() >= 0x4f9c) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[RequestLoadUserMonsterBook] buffer size overflow! "
            //     "count[%d] user[%d]",
            //     rows.GetAssembledCount(), characterId
            // )
            // L2_LOG_ERROR(Log,
            //     "[RequestLoadUserMonsterBook] buffer size overflow! "
            //     "count[%d] user[%d]",
            //     rows.GetAssembledCount(), characterId
            // )
            L2_LOG_ERROR(Log,
                "[monster book] Reply for character %d reached the 20380-byte payload cutoff after %d records. "
                "Load aborted; no records will be sent.", characterId, rows.GetAssembledCount()
            );
            return false;
        }
    }
    socket->Send(
        "chddb", 0xc6, RP_EX_LOAD_USER_MONSTER_BOOK, characterId, rows.GetAssembledCount(), rows.GetLength(),
        rows.GetPacket()
    );
    return false;
}

bool RequestUpdateUserMonsterBook(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0058c73c.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int count = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &characterId, &count);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    bool allUpdated = true;
    if (!user) {
        // Log.Add(LOG_ERROR, "[RequestUpdateUserMonsterBook] where is user[%d] ?", characterId)
        // L2_LOG_ERROR(Log, "[RequestUpdateUserMonsterBook] where is user[%d] ?", characterId)
        L2_LOG_ERROR(Log,
            "[monster book] Cannot update character %d: character unavailable. Requested record count %d; update skipped.",
            characterId, count
        );
    } else {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_UpdateUserMonsterBook(?,?,?,?)}";
        for (int index = 0; index < count; ++index) {
            short cardId = 0;
            char trophyLevel = 0;
            int killCount = 0;
            cursor = ::Disassemble(cursor, "hcd", &cardId, &trophyLevel, &killCount);
            const short trophyParameter = trophyLevel;
            connection.Prepare(statement);
            connection.BindParameter(&characterId);
            connection.BindParameter(&cardId);
            connection.BindParameter(&trophyParameter);
            connection.BindParameter(&killCount);
            if (!connection.ExecutePrepared(statement)) {
                allUpdated = false;
                // Log.Add(
                //     LOG_ERROR,
                //     "[%s][%d]RequestUpdateUserMonsterBook DB Error, "
                //     "user[%d] cardid[%d] trophyLev[%d], killcnt[%d]",
                //     "ServerPacketEx.cpp", 0x4dce, characterId, static_cast<int>(cardId), static_cast<int>(trophyLevel),
                //     killCount
                // )
                // L2_LOG_ERROR(Log,
                //     "[%s][%d]RequestUpdateUserMonsterBook DB Error, "
                //     "user[%d] cardid[%d] trophyLev[%d], killcnt[%d]",
                //     "ServerPacketEx.cpp", 0x4dce, characterId, static_cast<int>(cardId), static_cast<int>(trophyLevel),
                //     killCount
                // )
                L2_LOG_ERROR(Log,
                    "[monster book] Database update failed for character %d, card %d, trophy level %d, kill count %d "
                    "at record %d of %d. Continuing with remaining records.",
                    characterId, static_cast<int>(cardId), static_cast<int>(trophyLevel), killCount, index + 1, count
                );
            }
        }
    }
    if (!allUpdated) {
        // Log.Add(LOG_ERROR, "[RequestUpdateUserMonsterBook] db update fail. user[%d]", characterId)
        // Noise: each failed record is already reported above with its values and position in this request.
        // L2_LOG_ERROR(Log, "[RequestUpdateUserMonsterBook] db update fail. user[%d]", characterId)
    }
    return false;
}

bool RequestUpgradeSystem(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00542718. Paired L2Server sends the character DB
    // ID as the first field. The warehouse owner consumes the original packet
    // again after this wrapper acquires the inventory write lock.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "ServerPacketEx.cpp", 0x4e10, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "ServerPacketEx.cpp", 0x4e10, characterId)
        L2_LOG_ERROR(Log,
            "[items] Cannot process upgrade for character %d: character unavailable. Sending failure reply.", characterId
        );
        socket->Send("chdd", 0xc6, RP_EX_RESPONSE_UPGRADE_SYSTEM, characterId, 0);
        return false;
    }

    CWareHouseSP inventory = user->GetWareHouse(0, true);
    if (!inventory) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find warehouse 0.", "ServerPacketEx.cpp", 0x4e18)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find warehouse 0.", "ServerPacketEx.cpp", 0x4e18)
        L2_LOG_ERROR(Log,
            "[items] Cannot process upgrade for character %d: inventory unavailable. Sending failure reply.", characterId
        );
        socket->Send("chdd", 0xc6, RP_EX_RESPONSE_UPGRADE_SYSTEM, characterId, 0);
        return false;
    }

    {
        L2_WRITE_GUARD(*inventory.operator->());
        inventory->UpgradeSystem(socket, packet);
    }
    return false;
}

bool RequestLoadCardUpdownGameRecord(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0058c9bc.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    int gameId = 0;
    int gameTime = 0;
    int fixedItemClassId = 0;
    std::int64_t fixedItemAmount = 0;
    int randomItemClassId = 0;
    std::int64_t randomItemAmount = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&gameId);
    connection.Bind(&gameTime);
    connection.Bind(&fixedItemClassId);
    connection.Bind(&fixedItemAmount);
    connection.Bind(&randomItemClassId);
    connection.Bind(&randomItemAmount);
    constexpr const wchar_t* statement = L"{Call lin_LoadCardUpdownGame(?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
        // Log.Add(LOG_ERROR, "[RequestLoadCardUpdownGameRecord] db load fail [%d]", characterId)
        // L2_LOG_ERROR(Log, "[RequestLoadCardUpdownGameRecord] db load fail [%d]", characterId)
        L2_LOG_ERROR(Log,
            "[minigames] Card up/down record query failed or returned no row for character %d. No reply will be sent.",
            characterId
        );
        return false;
    }
    socket->Send(
        "chddddQdQ", 0xc6, RP_EX_LOAD_CARD_UPDOWN_RECORD, characterId, gameId, gameTime, fixedItemClassId, fixedItemAmount,
        randomItemClassId, randomItemAmount
    );
    return false;
}

bool RequestUpdateCardUpdownGameRecord(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0058cc44.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    int gameId = 0;
    int gameTime = 0;
    int fixedItemClassId = 0;
    std::int64_t fixedItemAmount = 0;
    int randomItemClassId = 0;
    std::int64_t randomItemAmount = 0;
    ::Disassemble(
        cursor, "dddQdQ", &gameId, &gameTime, &fixedItemClassId, &fixedItemAmount, &randomItemClassId, &randomItemAmount
    );

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateCardUpdownGame(?,?,?,?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&gameId);
    connection.BindParameter(&gameTime);
    connection.BindParameter(&fixedItemClassId);
    connection.BindParameter(&fixedItemAmount);
    connection.BindParameter(&randomItemClassId);
    connection.BindParameter(&randomItemAmount);
    if (connection.ExecutePrepared(statement)) {
        socket->Send(
            "chddddQdQ", 0xc6, RP_EX_UPDATE_CARD_UPDOWN_RECORD, characterId, gameId, gameTime, fixedItemClassId,
            fixedItemAmount, randomItemClassId, randomItemAmount
        );
    }
    return false;
}

bool Request_RQ_EX_PLEDGE_MANIPULATE_ITEMS(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0058cea8. This legacy request combines the shared
    // EtcMani stream with a dQ pledge-warehouse deletion list. Its result is
    // consumed by paired L2Server's atomic-job owner.
    L2_GUARD_FUNCTION();

    int pledgeId = 0;
    int userDbId = 0;
    int atomicJobId = 0;
    int etcManiCount = 0;
    int itemCount = 0;
    const unsigned char* cursor =
        ::Disassemble(packet, "ddddd", &pledgeId, &userDbId, &atomicJobId, &etcManiCount, &itemCount);

    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        // Log.Add(
        //     LOG_ERROR, "[PLEDGE][%s][%d]Cannot find pledge: %d, %d", "ServerPacketEx.cpp", 0x4f0e, pledgeId,
        //     userDbId
        // )
        // L2_LOG_ERROR(Log, "[PLEDGE][%s][%d]Cannot find pledge: %d, %d", "ServerPacketEx.cpp", 0x4f0e, pledgeId,
        //     userDbId
        // )
        L2_LOG_ERROR(Log,
            "[clan items] Cannot process legacy item-change request: clan %d unavailable, character %d, job %d. "
            "Sending failure -1.", pledgeId, userDbId, atomicJobId
        );
        SendPledgeManipulateItemsFailure(socket, pledgeId, userDbId, atomicJobId, -1);
        return false;
    }

    CWareHouseSP warehouse = pledge->GetWareHouse();
    if (!warehouse) {
        // Log.Add(
        //     LOG_ERROR, "[PLEDGE][%s][%d]Cannot find warehouse: %d, %d", "ServerPacketEx.cpp", 0x4f17, pledgeId,
        //     userDbId
        // )
        // L2_LOG_ERROR(Log, "[PLEDGE][%s][%d]Cannot find warehouse: %d, %d", "ServerPacketEx.cpp", 0x4f17, pledgeId,
        //     userDbId
        // )
        L2_LOG_ERROR(Log,
            "[clan items] Cannot process legacy item-change request: warehouse unavailable for clan %d, "
            "character %d, job %d. Sending failure -2.", pledgeId, userDbId, atomicJobId
        );
        SendPledgeManipulateItemsFailure(socket, pledgeId, userDbId, atomicJobId, -2);
        return false;
    }
    if (etcManiCount < 0 || itemCount < 0) {
        // Log.Add(
        //     LOG_ERROR, "[PLEDGE][%s][%d]Invalid count: %d, %d, %d, %d", "ServerPacketEx.cpp", 0x4f1e, pledgeId,
        //     userDbId, etcManiCount, itemCount
        // )
        // L2_LOG_ERROR(Log, "[PLEDGE][%s][%d]Invalid count: %d, %d, %d, %d", "ServerPacketEx.cpp", 0x4f1e, pledgeId,
        //     userDbId, etcManiCount, itemCount
        // )
        L2_LOG_WARN(Log,
            "[clan items] Rejected legacy item-change request for clan %d, character %d, job %d: "
            "EtcMani count %d, item count %d; both must be nonnegative. Sending failure -3.",
            pledgeId, userDbId, atomicJobId, etcManiCount, itemCount
        );
        SendPledgeManipulateItemsFailure(socket, pledgeId, userDbId, atomicJobId, -3);
        return false;
    }

    std::vector<EtcMani*> etcManis;
    cursor = EtcManiUtil::DisassembleEtcManis(cursor, etcManiCount, etcManis);
    std::vector<CreateItemStruct> requestedItems;
    ItemUtil::DisassembleItemList(cursor, itemCount, requestedItems);

    CPacketHelperEx<20480> etcManiResult;
    std::vector<CreateItemStruct> deletedItems;
    {
        L2_WRITE_GUARD(*warehouse.operator->());

        bool succeeded = EtcManiUtil::DoAtCache(etcManis, etcManiResult);
        if (succeeded) {
            for (const CreateItemStruct& requested : requestedItems) {
                if (requested.ItemID <= 0 || requested.Count <= 0) {
                    continue;
                }
                unsigned int itemId = 0;
                std::int64_t remainingAmount = 0;
                if (warehouse->_DeleteAssetOrStackableItem(requested.ItemID, requested.Count, itemId, remainingAmount) !=
                    static_cast<CWareHouse::DeleteResult>(0)) {
                    SendPledgeManipulateItemsSystemMessage(userDbId, static_cast<short>(0x1123), requested.ItemID);
                    succeeded = false;
                    break;
                }
                deletedItems.push_back(requested);
            }
        }

        if (!succeeded) {
            for (const CreateItemStruct& deleted : deletedItems) {
                CItemSP restored = warehouse->AddAssetItem(deleted.ItemID, deleted.Count);
                if (!restored) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "[PLEDGE][%s][%d][PLEDGE] Deleted items but failed: "
                    //     "%d, %d, %lld",
                    //     "ServerPacketEx.cpp", 0x4f62, pledgeId, deleted.ItemID, static_cast<long long>(deleted.Count)
                    // )
                    // L2_LOG_ERROR(Log,
                    //     "[PLEDGE][%s][%d][PLEDGE] Deleted items but failed: "
                    //     "%d, %d, %lld",
                    //     "ServerPacketEx.cpp", 0x4f62, pledgeId, deleted.ItemID, static_cast<long long>(deleted.Count)
                    // )
                    L2_LOG_ERROR(Log,
                        "[clan items] Legacy request rollback could not restore deleted item entry %d, count %lld, "
                        "for clan %d, character %d, job %d. Rollback is incomplete; continuing remaining rollback steps.",
                        deleted.ItemID, static_cast<long long>(deleted.Count), pledgeId, userDbId, atomicJobId
                    );
                }
            }
            EtcManiUtil::Rollback(etcManis);
            SendPledgeManipulateItemsFailure(socket, pledgeId, userDbId, atomicJobId, -4);
        } else {
            SendPledgeManipulateItemsSuccess(socket, pledgeId, userDbId, atomicJobId, etcManiCount, etcManiResult, deletedItems);
        }

        DestroyPledgeManipulateEtcManis(etcManis);
    }
    return false;
}

bool Request_RQ_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00542d10. Unlike the adjacent legacy packets,
    // both sides dispatch this request through protocol/dbpacket.h.
    L2_GUARD_FUNCTION();

    protocol::RQ_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO request;
    if (!DecodePacket(request, packet))
        return false;

    protocol::RP_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO response;
    response.nJobKey = request.nJobKey;
    CUserSP user = g_userDb.GetUser(request.nUserDBID, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[PLEDGE][%s][%d]No user: %d", "ServerPacketEx.cpp", 0x4f89, request.nUserDBID)
        // L2_LOG_ERROR(Log, "[PLEDGE][%s][%d]No user: %d", "ServerPacketEx.cpp", 0x4f89, request.nUserDBID)
        L2_LOG_ERROR(Log,
            "[clan contributions] Cannot load member information for character %d: character unavailable. Sending failure reply.",
            request.nUserDBID
        );
        response.bSuccess = false;
        SendPacket(socket, RP_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO, response);
        return false;
    }

    response.nPledgeDBID = static_cast<int>(user->GetPledgeID());
    response.bSuccess = PledgeContributionInCache::RetrieveInfo(response.nPledgeDBID, request.nUserDBID, response);
    SendPacket(socket, RP_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO, response);
    return false;
}

bool Request_RQ_EX_PLEDGE_CONTRIBUTION_RANK(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0058e084.
    L2_GUARD_FUNCTION();

    protocol::RQ_EX_PLEDGE_CONTRIBUTION_RANK request;
    if (!DecodePacket(request, packet))
        return false;

    // Current stripped reply-carrier constructor 0x0054c734 stores the
    // socket, installs the RP vtable, constructs the empty member list and
    // copies the request job key. The portable carrier keeps the socket in
    // SendPacket and performs the same remaining initialization here.
    protocol::RP_EX_PLEDGE_CONTRIBUTION_RANK response;
    response.nJobKey = request.nJobKey;
    response.nPledgeDBID = request.nPledgeDBID;
    const bool retrieved = request.bCurrent ? PledgeContributionInCache::RetrieveRankInfo(request.nPledgeDBID, response)
                                            : PledgeContributionInCache::RetrievePrevRankInfo(request.nPledgeDBID, response);
    if (!retrieved) {
        response.nPledgeDBID = 0;
    }
    SendPacket(socket, RP_EX_PLEDGE_CONTRIBUTION_RANK, response);
    return false;
}

bool Request_RQ_EX_PLEDGE_ITEM_LOAD(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0054d6b4.
    L2_GUARD_FUNCTION();

    protocol::RQ_EX_PLEDGE_ITEM_LOAD request;
    if (!DecodePacket(request, packet))
        return false;

    CPledgeSP pledge = g_pledgeDb.GetPledge(request.nPledgeDBID, false, runtime::process::CurrentDatabaseId());
    protocol::RP_EX_PLEDGE_ITEM_LOAD response;
    response.nJobKey = request.nJobKey;
    response.nPledgeDBID = request.nPledgeDBID;
    if (!pledge) {
        // Log.Add(
        //     LOG_ERROR, "[PLEDGE][%s][%d]no pledge: %d", "ServerPacketEx.cpp", 0x50b1, request.nPledgeDBID
        // )
        // L2_LOG_ERROR(Log, "[PLEDGE][%s][%d]no pledge: %d", "ServerPacketEx.cpp", 0x50b1, request.nPledgeDBID
        // )
        L2_LOG_ERROR(Log,
            "[clan items] Cannot load items for clan %d: clan unavailable. Sending an empty item list.", request.nPledgeDBID
        );
    } else {
        PledgeItemStuff::SelectPledgeItems(pledge->GetPledgeItem(), response.vItems);
    }
    SendPacket(socket, RP_EX_PLEDGE_ITEM_LOAD, response);
    return false;
}

bool Request_RQ_EX_PLEDGE_ITEM_SAVE(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00542f6c.
    L2_GUARD_FUNCTION();

    protocol::RQ_EX_PLEDGE_ITEM_SAVE request;
    if (!DecodePacket(request, packet))
        return false;

    protocol::RP_EX_PLEDGE_ITEM_SAVE response;
    response.nJobKey = request.nJobKey;
    response.nPledgeDBID = request.nPledgeDBID;
    CPledgeSP pledge = g_pledgeDb.GetPledge(request.nPledgeDBID, false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        response.bSuccess = false;
        // Log.Add(
        //     LOG_ERROR, "[PLEDGE][%s][%d]no pledge: %d", "ServerPacketEx.cpp", 0x50d5, request.nPledgeDBID
        // )
        // L2_LOG_ERROR(Log, "[PLEDGE][%s][%d]no pledge: %d", "ServerPacketEx.cpp", 0x50d5, request.nPledgeDBID
        // )
        L2_LOG_ERROR(Log,
            "[clan items] Cannot save item for clan %d: clan unavailable. Sending failure reply.", request.nPledgeDBID
        );
    } else {
        response.bSuccess = pledge->GetPledgeItem()->AddItemAndSave(request.nItemClassID, request.nExpiryTime);
    }
    SendPacket(socket, RP_EX_PLEDGE_ITEM_SAVE, response);
    return false;
}

bool Request_RQ_EX_PLEDGE_ITEM_DELETE(CSocket* socket [[maybe_unused]], const unsigned char* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0052d4b0 contains only the shipping guard. It does
    // not decode, mutate persistence or send RP_EX_PLEDGE_ITEM_DELETE.
    L2_GUARD_FUNCTION();
    return false;
}

bool Request_RQ_EX_PLEDGE_RAID_GET_INFO_LIST(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0058d628.
    L2_GUARD_FUNCTION();

    protocol::RQ_EX_PLEDGE_RAID_GET_INFO_LIST request;
    if (!DecodePacket(request, packet))
        return false;

    protocol::RP_EX_PLEDGE_RAID_GET_INFO_LIST response;
    response.nJobKey = request.nJobKey;
    DBConn totalConnection(runtime::process::CurrentDatabaseId());
    totalConnection.Bind(&response.nTotalCount_);
    constexpr const wchar_t* totalStatement = L"{Call lin_PledgeRaidGetTotalCount (?)}";
    totalConnection.Prepare(totalStatement);
    totalConnection.BindParameter(&request.nSeason_);
    if (!totalConnection.ExecutePrepared(totalStatement) || !totalConnection.Fetch()) {
        // Log.Add(LOG_ERROR, "[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!", "ServerPacketEx.cpp", 0x4fb2)
        // L2_LOG_ERROR(Log, "[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!", "ServerPacketEx.cpp", 0x4fb2)
        L2_LOG_ERROR(Log,
            "[clan raids] Total-count query failed or returned no row for season %d. No raid-list reply will be sent.",
            request.nSeason_
        );
        return false;
    }

    int worldId = 0;
    int pledgeDbId = 0;
    std::array<char16_t, 25> pledgeName{};
    int rank = 0;
    long long point = 0;
    DBConn listConnection(runtime::process::CurrentDatabaseId());
    listConnection.Bind(&worldId);
    listConnection.Bind(&pledgeDbId);
    listConnection.Bind(pledgeName.data(), 0x32);
    listConnection.Bind(&rank);
    listConnection.Bind(&point);
    constexpr const wchar_t* listStatement = L"{Call lin_PledgeRaidGetInfoList (?)}";
    listConnection.Prepare(listStatement);
    listConnection.BindParameter(&request.nSeason_);
    if (!listConnection.ExecutePrepared(listStatement)) {
        // Log.Add(LOG_ERROR, "[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!", "ServerPacketEx.cpp", 0x4fc5)
        // L2_LOG_ERROR(Log, "[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!", "ServerPacketEx.cpp", 0x4fc5)
        L2_LOG_ERROR(Log,
            "[clan raids] Database list query failed for season %d. Sending an empty list with the previously loaded total count.",
            request.nSeason_
        );
    } else {
        while (listConnection.Fetch()) {
            protocol::PkPledgeRaidInfoData row{};
            row.nWorldId_ = worldId;
            row.nPledgeDbId_ = pledgeDbId;
            const std::u16string converted = std::u16string(pledgeName.data());
            std::copy_n(converted.data(), std::min(converted.size(), row.szPledgeName_.size()), row.szPledgeName_.data());
            row.nRank_ = rank;
            row.nPoint_ = point;
            response.vList_.push_back(row);
        }
    }
    SendPacket(socket, RP_EX_PLEDGE_RAID_GET_INFO_LIST, response);
    return false;
}

bool Request_RQ_EX_PLEDGE_RAID_INFO_UPDATE_REWARD_REASON(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0058d9bc.
    L2_GUARD_FUNCTION();

    protocol::RQ_EX_PLEDGE_RAID_INFO_UPDATE_REWARD_REASON request;
    if (!DecodePacket(request, packet))
        return false;

    for (const protocol::PkUpdateRewardPledgeData& update : request.vUpdateRewardPledgeList_) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_PledgeRaidUpdateRewardReason (?,?,?)}";
        connection.Prepare(statement);
        connection.BindParameter(&request.nSeason_);
        connection.BindParameter(&update.nPledgeDbId_);
        connection.BindParameter(&update.nRewardReason_);
        if (!connection.ExecutePrepared(statement)) {
            // Log.Add(LOG_ERROR, "[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!", "ServerPacketEx.cpp", 0x5021)
            // L2_LOG_ERROR(Log, "[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!", "ServerPacketEx.cpp", 0x5021)
            L2_LOG_ERROR(Log,
                "[clan raids] Database reward-reason update failed for season %d, clan %d, reason %d. "
                "Continuing remaining updates; job reply will still be sent.",
                request.nSeason_, static_cast<int>(update.nPledgeDbId_), update.nRewardReason_
            );
        }
    }

    protocol::RP_EX_PLEDGE_RAID_INFO_UPDATE_REWARD_REASON response;
    response.nJobKey = request.nJobKey;
    SendPacket(socket, RP_EX_PLEDGE_RAID_INFO_UPDATE_REWARD_REASON, response);
    return false;
}

bool Request_RQ_EX_PLEDGE_RAID_INZONE_LOAD(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0054d45c.
    L2_GUARD_FUNCTION();

    protocol::RQ_EX_PLEDGE_RAID_INZONE_LOAD request;
    if (!DecodePacket(request, packet))
        return false;

    protocol::RP_EX_PLEDGE_RAID_INZONE_LOAD response;
    response.nJobKey = request.nJobKey;
    protocol::PkPledgeRaidInzoneData row{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&row.nPledgeDbId_);
    connection.Bind(&row.nRestrictTime_);
    constexpr const wchar_t* statement = L"{Call lin_PledgeRaidInzoneLoad}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!", "ServerPacketEx.cpp", 0x4ff7)
        // L2_LOG_ERROR(Log, "[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!", "ServerPacketEx.cpp", 0x4ff7)
        L2_LOG_ERROR(Log, "[clan raids] Database request to load instance restrictions failed. Sending an empty list.");
    } else {
        while (connection.Fetch()) {
            response.vList_.push_back(row);
        }
    }
    SendPacket(socket, RP_EX_PLEDGE_RAID_INZONE_LOAD, response);
    return false;
}

bool Request_RQ_EX_PLEDGE_MISSION_LOAD(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0058dbf8.
    L2_GUARD_FUNCTION();

    protocol::RQ_EX_PLEDGE_MISSION_LOAD request;
    if (!DecodePacket(request, packet))
        return false;

    protocol::RP_EX_PLEDGE_MISSION_LOAD response;
    response.nJobKey = request.nJobKey;
    protocol::PkPledgeMissionLoadData row{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&row.nMissionId_);
    connection.Bind(&row.nCount_);
    connection.Bind(&row.nRewardTime_);
    constexpr const wchar_t* statement = L"{Call lin_PledgeMissionLoad (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&request.nUserDbId_);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!", "ServerPacketEx.cpp", 0x5045)
        // L2_LOG_ERROR(Log, "[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!", "ServerPacketEx.cpp", 0x5045)
        L2_LOG_ERROR(Log,
            "[clan missions] Database load request failed for character %d. Fetch and reply processing will still be attempted.",
            request.nUserDbId_
        );
    }
    while (connection.Fetch()) {
        response.vList_.push_back(row);
    }
    SendPacket(socket, RP_EX_PLEDGE_MISSION_LOAD, response);
    return false;
}

bool Request_RQ_EX_PLEDGE_MISSION_ADD_COUNT(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0058de7c has no response packet.
    L2_GUARD_FUNCTION();

    protocol::RQ_EX_PLEDGE_MISSION_ADD_COUNT request;
    if (!DecodePacket(request, packet))
        return false;
    for (const protocol::PkPledgeMissionAddCountData& update : request.vList_) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_PledgeMissionAddCount (?,?,?)}";
        connection.Prepare(statement);
        connection.BindParameter(&request.nUserDbId_);
        connection.BindParameter(&update.nMissionId_);
        connection.BindParameter(&update.nCount_);
        if (!connection.ExecutePrepared(statement)) {
            // Log.Add(LOG_ERROR, "[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!", "ServerPacketEx.cpp", 0x506b)
            // L2_LOG_ERROR(Log, "[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!", "ServerPacketEx.cpp", 0x506b)
            L2_LOG_ERROR(Log,
                "[clan missions] Database count update failed for character %d, mission %d, added count %d. "
                "Continuing with remaining updates.", request.nUserDbId_, update.nMissionId_, update.nCount_
            );
        }
    }
    return false;
}

bool Request_RQ_EX_PLEDGE_ANNOUNCE(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0059435c.
    L2_GUARD_FUNCTION();

    protocol::RQ_EX_PLEDGE_ANNOUNCE request;
    if (!DecodePacket(request, packet))
        return false;

    protocol::RP_EX_PLEDGE_ANNOUNCE response;
    response.nJobKey = request.nJobKey;
    CPledgeAnnounceSP pledgeAnnounce = CPledgeAnnounceRegistry::inst().Get(static_cast<unsigned int>(request.nPledgeDbId_));
    if (!pledgeAnnounce) {
        CPledgeAnnounceDBHandlerImpl dbHandler;
        pledgeAnnounce = dbHandler.Load(static_cast<unsigned int>(request.nPledgeDbId_));
    }
    if (pledgeAnnounce) {
        response.content.assign(runtime::StringView(pledgeAnnounce->GetContent()));
        response.bShow = pledgeAnnounce->DoesShow();
    } else {
        response.content.assign(std::u16string_view{});
        response.bShow = false;
    }
    SendPacket(socket, RP_EX_PLEDGE_ANNOUNCE, response);
    return false;
}

bool Request_RQ_EX_PLEDGE_ANNOUNCE_SET(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005945a0.
    L2_GUARD_FUNCTION();

    protocol::RQ_EX_PLEDGE_ANNOUNCE_SET request;
    if (!DecodePacket(request, packet))
        return false;

    protocol::RP_EX_PLEDGE_ANNOUNCE_SET response;
    response.nJobKey = request.nJobKey;
    CPledgeAnnounce* const created =
        new CPledgeAnnounce(static_cast<unsigned int>(request.nPledgeDBID), request.bShow, request.content.c_str());
    CPledgeAnnounceSP pledgeAnnounce(created, ObjectRefType::FunctionChain);
    L2_RELEASE(created, ObjectRefType::CreateDelete);

    CPledgeAnnounceDBHandlerImpl dbHandler;
    if (dbHandler.Set(pledgeAnnounce)) {
        response.bSuccess = CPledgeAnnounceRegistry::inst().Register(pledgeAnnounce);
        response.content.assign(runtime::StringView(pledgeAnnounce->GetContent()));
        response.bShow = pledgeAnnounce->DoesShow();
    }
    SendPacket(socket, RP_EX_PLEDGE_ANNOUNCE_SET, response);
    return false;
}

bool Request_RQ_EX_SEQUENCE_JOB(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00533368.
    L2_GUARD_FUNCTION();

    int jobId = 0;
    std::uint8_t protocol = 0;
    SequnceJobSocket sequenceSocket(socket->m_socket, 0x4ff9);
    const unsigned char* const cursor = ::Disassemble(packet, "dc", &jobId, &protocol);

    bool result = false;
    if (protocol < CacheDCallbackMap::SIZE) {
        CacheDPacketCallback callback = gPacketTableFromServer.GetCallback(protocol);
        result = callback(&sequenceSocket, cursor);
    }
    sequenceSocket.SendSequnceJob(socket, jobId);
    return result;
}

bool Request_RQ_EX_BUILDER_COMMAND(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00593e5c.
    L2_GUARD_FUNCTION();

    protocol::RQ_EX_BUILDER_COMMAND request;
    if (!DecodePacket(request, packet)) {
        // Log.Add(LOG_ERROR, "[BUILDER][%s][%d]wrong packet data", "ServerPacketEx.cpp", 0x5154)
        // L2_LOG_ERROR(Log, "[BUILDER][%s][%d]wrong packet data", "ServerPacketEx.cpp", 0x5154)
        L2_LOG_WARN(Log, "[builder] Cannot decode command request. Request rejected; no reply will be sent.");
        return false;
    }
    if (request.params.empty()) {
        // Log.Add(LOG_ERROR, "[BUILDER][%s][%d]param data is not exist", "ServerPacketEx.cpp", 0x515f)
        // L2_LOG_ERROR(Log, "[BUILDER][%s][%d]param data is not exist", "ServerPacketEx.cpp", 0x515f)
        L2_LOG_WARN(Log, "[builder] Command request contains no parameters. Request rejected; no reply will be sent.");
        return false;
    }
    if (runtime::StringView(*request.params.begin()) != u"version")
        return false;

    protocol::RP_EX_BUILDER_COMMAND response;
    response.nUserSid = request.nUserSid;
    response.params.emplace_back(u"version");
    response.params.emplace_back(u"L2_WORK_2018_05_15_DAILYBUILD_05_00");
    response.params.emplace_back(WideToUtf16(CBuildDate::inst().BuildDateTime()));
    SendPacket(socket, RP_EX_BUILDER_COMMAND, response);
    return false;
}

bool RequestLockedItem(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005423e0.
    L2_GUARD_FUNCTION();

    unsigned int userDbId = 0;
    std::uint8_t flag = 0;
    ::Disassemble(packet, "dc", &userDbId, &flag);
    CUserSP user = g_userDb.GetUser(static_cast<int>(userDbId), true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%u]", "ServerPacketEx.cpp", 0x4dee, userDbId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%u]", "ServerPacketEx.cpp", 0x4dee, userDbId)
        L2_LOG_ERROR(Log,
            "[items] Cannot process locked-item request for character %u: character unavailable. "
            "Request flag %u; sending failure reply.", userDbId, static_cast<unsigned int>(flag)
        );
        socket->Send("chdcc", 0xc6, RP_EX_RESPONSE_LOCKED_ITEM, userDbId, 0, flag);
        return false;
    }

    CWareHouseSP warehouse = user->GetWareHouse(0, true);
    if (!warehouse) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find warehouse 0.", "ServerPacketEx.cpp", 0x4df6)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find warehouse 0.", "ServerPacketEx.cpp", 0x4df6)
        L2_LOG_ERROR(Log,
            "[items] Cannot process locked-item request for character %u: inventory unavailable. "
            "Request flag %u; sending failure reply.", userDbId, static_cast<unsigned int>(flag)
        );
        socket->Send("chdcc", 0xc6, RP_EX_RESPONSE_LOCKED_ITEM, userDbId, 0, flag);
        return false;
    }

    {
        L2_WRITE_GUARD(*warehouse.operator->());
        warehouse->hLockedItem(socket, packet);
    }
    return false;
}

bool Request_RQ_EX_SET_ITEM_REUSE_DELAY(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x00543278 has no response packet.
    L2_GUARD_FUNCTION();

    int userDbId = 0;
    int reuseKey = 0;
    int reuseEndTime = 0;
    ::Disassemble(packet, "ddd", &userDbId, &reuseKey, &reuseEndTime);
    CUserSP user = g_userDb.GetUser(userDbId, true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        user->SetItemReuseDelay(reuseKey, reuseEndTime);
    }
    return false;
}

bool Request_RQ_EX_PLEDGE_MANIPULATE_ITEMS_NEW(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0058e428.
    L2_GUARD_FUNCTION();

    int pledgeId = 0;
    int userDbId = 0;
    int atomicJobId = 0;
    int etcManiCount = 0;
    int itemCount = 0;
    const unsigned char* cursor =
        ::Disassemble(packet, "ddddd", &pledgeId, &userDbId, &atomicJobId, &etcManiCount, &itemCount);

    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        // Log.Add(
        //     LOG_ERROR, "[PLEDGE][%s][%d]Cannot find pledge: %d, %d", "ServerPacketEx.cpp", 0x519d, pledgeId,
        //     userDbId
        // )
        // L2_LOG_ERROR(Log, "[PLEDGE][%s][%d]Cannot find pledge: %d, %d", "ServerPacketEx.cpp", 0x519d, pledgeId,
        //     userDbId
        // )
        L2_LOG_ERROR(Log,
            "[clan items] Cannot process new item-change request: clan %d unavailable, character %d, job %d. "
            "Sending failure -1.", pledgeId, userDbId, atomicJobId
        );
        SendPledgeManipulateItemsNewFailure(socket, pledgeId, userDbId, atomicJobId, -1);
        return false;
    }

    CWareHouseSP warehouse = pledge->GetWareHouse();
    if (!warehouse) {
        // Log.Add(
        //     LOG_ERROR, "[PLEDGE][%s][%d]Cannot find warehouse: %d, %d", "ServerPacketEx.cpp", 0x51a6, pledgeId,
        //     userDbId
        // )
        // L2_LOG_ERROR(Log, "[PLEDGE][%s][%d]Cannot find warehouse: %d, %d", "ServerPacketEx.cpp", 0x51a6, pledgeId,
        //     userDbId
        // )
        L2_LOG_ERROR(Log,
            "[clan items] Cannot process new item-change request: warehouse unavailable for clan %d, "
            "character %d, job %d. Sending failure -2.", pledgeId, userDbId, atomicJobId
        );
        SendPledgeManipulateItemsNewFailure(socket, pledgeId, userDbId, atomicJobId, -2);
        return false;
    }
    if (etcManiCount < 0 || itemCount < 0) {
        // Log.Add(
        //     LOG_ERROR, "[PLEDGE][%s][%d]Invalid count: %d, %d, %d, %d", "ServerPacketEx.cpp", 0x51ad, pledgeId,
        //     userDbId, etcManiCount, itemCount
        // )
        // L2_LOG_ERROR(Log, "[PLEDGE][%s][%d]Invalid count: %d, %d, %d, %d", "ServerPacketEx.cpp", 0x51ad, pledgeId,
        //     userDbId, etcManiCount, itemCount
        // )
        L2_LOG_WARN(Log,
            "[clan items] Rejected new item-change request for clan %d, character %d, job %d: "
            "EtcMani count %d, item count %d; both must be nonnegative. Sending failure -3.",
            pledgeId, userDbId, atomicJobId, etcManiCount, itemCount
        );
        SendPledgeManipulateItemsNewFailure(socket, pledgeId, userDbId, atomicJobId, -3);
        return false;
    }

    std::vector<EtcMani*> etcManis;
    cursor = EtcManiUtil::DisassembleEtcManis(cursor, etcManiCount, etcManis);
    std::vector<CreateItemStruct> requestedItems;
    ItemUtil::DisassembleItemList(cursor, itemCount, requestedItems);

    CPacketHelperEx<20480> etcManiResult;
    std::vector<CreateItemStruct> changedItems;
    {
        L2_WRITE_GUARD(*warehouse.operator->());
        bool succeeded = EtcManiUtil::DoAtCache(etcManis, etcManiResult);
        if (succeeded) {
            for (const CreateItemStruct& requested : requestedItems) {
                if (requested.ItemID <= 0 || requested.Count == 0)
                    continue;

                if (requested.Count < 1) {
                    unsigned int itemId = 0;
                    std::int64_t remainingAmount = 0;
                    if (warehouse->_DeleteAssetOrStackableItem(requested.ItemID, requested.Count, itemId, remainingAmount) !=
                        static_cast<CWareHouse::DeleteResult>(0)) {
                        succeeded = false;
                        break;
                    }
                } else {
                    CItemSP added = warehouse->AddAssetItem(requested.ItemID, requested.Count);
                    if (!added) {
                        succeeded = false;
                        break;
                    }
                }
                changedItems.push_back(requested);
            }
        }

        if (!succeeded) {
            for (const CreateItemStruct& changed : changedItems) {
                if (changed.ItemID <= 0 || changed.Count == 0)
                    continue;
                if (changed.Count < 1) {
                    CItemSP restored = warehouse->AddAssetItem(changed.ItemID, changed.Count);
                    if (!restored) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     "[PLEDGE][%s][%d][PLEDGE][Rollback] "
                        //     "Add items but failed: %d, %d, %lld",
                        //     "ServerPacketEx.cpp", 0x5214, pledgeId, changed.ItemID, static_cast<long long>(changed.Count)
                        // )
                        // L2_LOG_ERROR(Log,
                        //     "[PLEDGE][%s][%d][PLEDGE][Rollback] "
                        //     "Add items but failed: %d, %d, %lld",
                        //     "ServerPacketEx.cpp", 0x5214, pledgeId, changed.ItemID, static_cast<long long>(changed.Count)
                        // )
                        L2_LOG_ERROR(Log,
                            "[clan items] New request rollback AddAssetItem failed for entry %d with signed count %lld, "
                            "clan %d, character %d, job %d. Rollback is incomplete; continuing remaining rollback steps.",
                            changed.ItemID, static_cast<long long>(changed.Count), pledgeId, userDbId, atomicJobId
                        );
                    }
                } else {
                    unsigned int itemId = 0;
                    std::int64_t remainingAmount = 0;
                    if (warehouse->_DeleteAssetOrStackableItem(changed.ItemID, changed.Count, itemId, remainingAmount) !=
                        static_cast<CWareHouse::DeleteResult>(0)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     "[PLEDGE][%s][%d][PLEDGE][Rollback] "
                        //     "Deleted items but failed: %d, %d, %lld",
                        //     "ServerPacketEx.cpp", 0x5207, pledgeId, changed.ItemID, static_cast<long long>(changed.Count)
                        // )
                        // L2_LOG_ERROR(Log,
                        //     "[PLEDGE][%s][%d][PLEDGE][Rollback] "
                        //     "Deleted items but failed: %d, %d, %lld",
                        //     "ServerPacketEx.cpp", 0x5207, pledgeId, changed.ItemID, static_cast<long long>(changed.Count)
                        // )
                        L2_LOG_ERROR(Log,
                            "[clan items] New request rollback could not delete added item entry %d, count %lld, "
                            "for clan %d, character %d, job %d. Rollback is incomplete; continuing remaining rollback steps.",
                            changed.ItemID, static_cast<long long>(changed.Count), pledgeId, userDbId, atomicJobId
                        );
                    }
                }
            }
            EtcManiUtil::Rollback(etcManis);
            SendPledgeManipulateItemsNewFailure(socket, pledgeId, userDbId, atomicJobId, -4);
        } else {
            SendPledgeManipulateItemsNewSuccess(
                socket, pledgeId, userDbId, atomicJobId, etcManiCount, etcManiResult, changedItems
            );
        }

        DestroyPledgeManipulateEtcManis(etcManis);
    }
    return false;
}

bool Request_RQ_EX_LOAD_RESERVATION_ANNOUNCE(CSocket* socket, const unsigned char* packet [[maybe_unused]]) {
    // Current Cached.exe 0x00595f18 ignores the request body.
    L2_GUARD_FUNCTION();

    int announceId = 0;
    std::array<char16_t, 256> message{};
    int lineNumber = 0;
    int startDate = 0;
    int endDate = 0;
    int period = 0;
    int location = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&announceId);
    connection.Bind(message.data(), 0x200);
    connection.Bind(&lineNumber);
    connection.Bind(&startDate);
    connection.Bind(&endDate);
    connection.Bind(&period);
    connection.Bind(&location);

    protocol::RP_EX_LOAD_RESERVATION_ANNOUNCE response;
    constexpr const wchar_t* statement = L"{Call lin_GetGMAnnounce}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot connect", "ServerPacketEx.cpp", 0x5247)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot connect", "ServerPacketEx.cpp", 0x5247)
        L2_LOG_ERROR(Log, "[announcements] Database request to load scheduled announcements failed. Sending an empty list.");
        SendPacket(socket, RP_EX_LOAD_RESERVATION_ANNOUNCE, response);
        return false;
    }

    int currentAnnounceId = 0;
    protocol::PkReservationAnncounceData grouped;
    while (connection.Fetch()) {
        if (currentAnnounceId != announceId && !grouped.Msgs.empty()) {
            response.data.push_back(grouped);
            grouped = protocol::PkReservationAnncounceData{};
        }
        currentAnnounceId = announceId;
        grouped.announceId = announceId;
        grouped.Msgs.emplace_back(std::u16string(message.data()));
        grouped.lineNumber = lineNumber;
        grouped.StartDate = startDate;
        grouped.endDate = endDate;
        grouped.period = period;
        grouped.location = location;
    }
    if (!grouped.Msgs.empty())
        response.data.push_back(std::move(grouped));
    SendPacket(socket, RP_EX_LOAD_RESERVATION_ANNOUNCE, response);
    return false;
}

bool Request_RQ_EX_DELETE_RESERVATION_ANNOUNCE(CSocket* socket [[maybe_unused]], const unsigned char* packet) {
    // Current Cached.exe 0x0058ecd4 has no response packet.
    L2_GUARD_FUNCTION();

    protocol::RQ_EX_DELETE_RESERVATION_ANNOUNCE request;
    if (!DecodePacket(request, packet)) {
        // Log.Add(LOG_ERROR, "[BUILDER][%s][%d]wrong packet data", "ServerPacketEx.cpp", 0x5273)
        // L2_LOG_ERROR(Log, "[BUILDER][%s][%d]wrong packet data", "ServerPacketEx.cpp", 0x5273)
        L2_LOG_WARN(Log, "[announcements] Cannot decode scheduled-announcement deletion request. Request rejected.");
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DelGMAnnounce (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&request.announceNum);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot delete, id[%d]", "ServerPacketEx.cpp", 0x527c, request.announceNum
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot delete, id[%d]", "ServerPacketEx.cpp", 0x527c, request.announceNum
        // )
        L2_LOG_ERROR(Log,
            "[announcements] Database request to delete scheduled announcement %d failed.", request.announceNum
        );
    }
    return false;
}

bool Request_RQ_EX_SET_RESERVATION_ANNOUNCE(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0058ee7c.
    L2_GUARD_FUNCTION();

    protocol::RQ_EX_SET_RESERVATION_ANNOUNCE request;
    if (!DecodePacket(request, packet)) {
        // Log.Add(LOG_ERROR, "[BUILDER][%s][%d]wrong packet data", "ServerPacketEx.cpp", 0x528d)
        // L2_LOG_ERROR(Log, "[BUILDER][%s][%d]wrong packet data", "ServerPacketEx.cpp", 0x528d)
        L2_LOG_WARN(Log,
            "[announcements] Cannot decode scheduled-announcement save request. No refresh reply will be sent."
        );
        return false;
    }

    const std::u16string message(request.Msgs.c_str());
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetGMAnnounce (?, ?, ?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&request.announceId);
    connection.BindParameter(message.c_str());
    connection.BindParameter(&request.lineNumber);
    connection.BindParameter(&request.StartDate);
    connection.BindParameter(&request.endDate);
    connection.BindParameter(&request.period);
    connection.BindParameter(&request.location);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] set reservation announce failed "
        //     "[announceId : %d][message : %s] [line_number : %d]",
        //     "ServerPacketEx.cpp", 0x5296, request.announceId, WideToUtf8(message).c_str(), request.lineNumber
        // )
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] set reservation announce failed "
        //     "[announceId : %d][message : %s] [line_number : %d]",
        //     "ServerPacketEx.cpp", 0x5296, request.announceId, WideToUtf8(message).c_str(), request.lineNumber
        // )
        // L2_LOG_ERROR(Log,
        //     "[announcements] Database request to save scheduled announcement %d, line %d failed: '%s'. "
        //     "A refresh reply will still be sent.", request.announceId, request.lineNumber, WideToUtf8(message).c_str()
        // )
        L2_LOG_ERROR(Log,
            "[announcements] Database request to save scheduled announcement %d, line %d failed: '%s'. "
            "A refresh reply will still be sent.", request.announceId, request.lineNumber, Utf16ToUtf8(message).c_str()
        );
    }

    protocol::RP_EX_REFRASH_RESERVATION_ANNOUNCE response;
    response.temp = 0;
    SendPacket(socket, RP_EX_REFRASH_RESERVATION_ANNOUNCE, response);
    return false;
}

bool Request_RQ_EX_SHOW_RESERVATION_ANNOUNCE(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00595a04.
    L2_GUARD_FUNCTION();

    protocol::RQ_EX_SHOW_RESERVATION_ANNOUNCE request;
    if (!DecodePacket(request, packet)) {
        // Log.Add(LOG_ERROR, "[BUILDER][%s][%d]wrong packet data", "ServerPacketEx.cpp", 0x52a9)
        // L2_LOG_ERROR(Log, "[BUILDER][%s][%d]wrong packet data", "ServerPacketEx.cpp", 0x52a9)
        L2_LOG_WARN(Log,
            "[announcements] Cannot decode scheduled-announcement display request. No reply will be sent."
        );
        return false;
    }

    int announceId = 0;
    std::array<char16_t, 256> message{};
    int lineNumber = 0;
    int startDate = 0;
    int endDate = 0;
    int period = 0;
    int location = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&announceId);
    connection.Bind(message.data(), 0x200);
    connection.Bind(&lineNumber);
    connection.Bind(&startDate);
    connection.Bind(&endDate);
    connection.Bind(&period);
    connection.Bind(&location);

    constexpr const wchar_t* statement = L"{Call lin_GetGMAnnounce}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement))
        return false;

    protocol::RP_EX_SHOW_RESERVATION_ANNOUNCE response;
    response.nUserServerId = request.nUserServerId;
    int currentAnnounceId = 0;
    protocol::PkReservationAnncounceData grouped;
    while (connection.Fetch()) {
        if (currentAnnounceId != announceId && !grouped.Msgs.empty()) {
            response.data.push_back(grouped);
            grouped = protocol::PkReservationAnncounceData{};
        }
        currentAnnounceId = announceId;
        grouped.announceId = announceId;
        grouped.Msgs.emplace_back(std::u16string(message.data()));
        grouped.lineNumber = lineNumber;
        grouped.StartDate = startDate;
        grouped.endDate = endDate;
        grouped.period = period;
        grouped.location = location;
    }
    if (!grouped.Msgs.empty())
        response.data.push_back(std::move(grouped));
    SendPacket(socket, RP_EX_SHOW_RESERVATION_ANNOUNCE, response);
    return false;
}

} // namespace RQExHandler
