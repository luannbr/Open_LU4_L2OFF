#include "PledgeStuff.h"

#include "DB.h"
#include "Item.h"
#include "L2Packet.h"
#include "ObjectDB.h"
#include "Pledge.h"
#include "PledgeFunction.h"
#include "PledgeInfo.h"
#include "PledgeItem.h"
#include "PledgeLevelInfo.h"
#include "PledgeMastery.h"
#include "PledgeMemberService.h"
#include "PledgeSkill.h"
#include "Shared/src/AtomicJob.h"
#include "Shared/src/DXPacket.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/proto/Stream.h"
#include "User.h"
#include "UserPacketEx.h"
#include "UserSocket.h"
#include "XmlLoader.h"

#include <algorithm>
#include <array>
#include <bitset>

namespace PledgeStuff {

bool IsValidPledgeType(const PledgeType pledgeType) {
    // Current L2Server 0x009decd4.
    return pledgeType == MASTER_PLEDGE || pledgeType == SUB_1;
}

bool IsValidItem(const CObject* const object) {
    // Current L2Server 0x009df34c.
    L2_GUARD_FUNCTION();
    if (object == nullptr || !object->IsItem()) {
        return false;
    }
    CItem* const item = const_cast<CObject*>(object)->CastItem();
    return item != nullptr && !item->IsPetCollarItem();
}

bool GiveItem(User* const user, const int itemClassId, const std::int64_t amount) {
    // Current L2Server 0x009df420.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeStuff.cpp",
        //     0x1ff,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeStuff.cpp",
        //     0x1ff,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot give clan reward item %d (count %lld): the user is unavailable.", itemClassId,
            static_cast<long long>(amount)
        );
        return false;
    }
    if (itemClassId == -1 || amount <= 0) {
        return false;
    }

    CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
    if (!IsValidItem(object)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeStuff.cpp",
        //     0x20a)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeStuff.cpp",
        //     0x20a)
        L2_LOG_ERROR(
            Log,
            "Cannot give clan reward item %d (count %lld): its prototype is missing, is not an item or is a pet collar; the "
            "reward is rejected.",
            itemClassId, static_cast<long long>(amount)
        );
        return false;
    }

    CItem* const item = object->CastItem();
    {
        L2_WRITE_GUARD(user->m_inv);
        const ItemOptionKey optionKey{};
        const BaseAttribute attribute;
        static_cast<void>(user->AddItemToInventory(
            itemClassId, amount, ItemGetReason::PledgeTotalContribution, item->m_data->GetEnchanted(), 0, 0, optionKey, 0,
            attribute
        ));
    }
    return true;
}

bool GiveNameValue(User* const user, const int amount) {
    // Current L2Server 0x009df5f0.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeStuff.cpp",
        //     0x21f,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeStuff.cpp",
        //     0x21f,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot give %d fame points for the clan reward: the user is unavailable.", amount);
        return false;
    }
    if (amount <= 0) {
        return false;
    }
    user->AddPoint(5, amount, true);
    return true;
}

void UpdateNameValueOf(CPledge* const pledge, const int amount) {
    // Current L2Server 0x009def20.
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeStuff.cpp",
        //     0x17f,
        //     L"(pPledge != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeStuff.cpp",
        //     0x17f,
        //     L"(pPledge != NULL)")
        L2_LOG_ERROR(Log, "Cannot apply clan reputation change %d: the clan is unavailable.", amount);
        return;
    }
    db.UpdatePledgeNameValue(pledge->Id(), pledge->GetDbId(), pledge->GetNameValue(), amount, pledge->GetWorldId());
}

void SendPacket(const User* const user, const S_EX_PROTOCOL extendedProtocol, network::IPacket& packet) {
    // Current L2Server 0x009df6e4.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        user->GetSocket()->Send(S_EX, extendedProtocol, packet);
    }
}

void SendPacket(const User* const user, const CPacketHelperEx<20480>& packet) {
    // Current L2Server 0x009df7bc. The target obtains the packet pointer
    // before the length and then forwards the already-assembled raw bytes.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        CPacketHelperEx<20480>& mutablePacket = const_cast<CPacketHelperEx<20480>&>(packet);
        char* const buffer = mutablePacket.GetPacket();
        const int length = packet.GetLength();
        user->GetSocket()->Send("b", length, buffer);
    }
}

} // namespace PledgeStuff

namespace {

// Current L2Server 0x009e0f9c. The original private source spelling is
// unavailable. The descriptive spelling is internal to the
// physical PledgeStuff.cpp owner.
std::bitset<25> GetDefaultPledgePower(const int grade) {
    std::bitset<25> power;
    if (grade == 6) {
        for (const int position : {1, 3, 8, 11, 12, 14, 16, 19, 20}) {
            power.set(static_cast<std::size_t>(position));
        }
    } else if (grade == 7) {
        for (const int position : {1, 3, 4, 6, 8, 9, 11, 12, 14, 15, 24, 16, 18, 19, 20}) {
            power.set(static_cast<std::size_t>(position));
        }
    }
    return power;
}

void Write_LOG_TEMPLETE(
    const int logId,
    const User* const user,
    const CPledge* const pledge,
    const std::int64_t number1,
    const std::int64_t number2,
    const std::int64_t number3,
    const std::int64_t number4,
    const std::int64_t number10
) {
    L2_GUARD_FUNCTION();
    const char16_t* pledgeName = nullptr;
    std::uint32_t pledgeDatabaseId = 0;
    if (pledge != nullptr) {
        pledgeName = pledge->GetPledgeName();
        pledgeDatabaseId = const_cast<CPledge*>(pledge)->GetDbId();
    }
    CLogFormatter formatter;
    formatter.WriteLog4(
        logId, const_cast<User*>(user), nullptr, pledgeName, nullptr, nullptr, number1, number2, number3, number4, 0, 0, 0,
        0, number10, static_cast<std::int64_t>(pledgeDatabaseId), 0
    );
}

} // namespace

namespace PledgeStuff {

void SetDefaultPledgePower(CPledge* const pledge, const int grade) {
    // Current L2Server 0x009e1040. Preserve the two independent
    // std::bitset::to_ulong materializations in the shipping body.
    const std::bitset<25> power = GetDefaultPledgePower(grade);
    pledge->SetPowerGrade(grade, power.to_ulong());
    db.RequestSavePledgePower(*pledge, grade, static_cast<std::uint32_t>(power.to_ulong()));
}

PledgeCostHelper::PledgeCostHelper(User* const user, CPledge* const pledge) :
    m_pUser(user), m_pPledge(pledge), m_nNameValue(0) {}

PledgeCostHelper::~PledgeCostHelper() {
    L2_GUARD_FUNCTION();
    EtcManiUtil::Destroy(m_etcManis);
}

void PledgeCostHelper::NeedItem(const int itemClassId, const std::int64_t amount) {
    L2_GUARD_FUNCTION();
    m_items.emplace_back(itemClassId, amount, 0);
}

void PledgeCostHelper::SetNameValue(const int nameValue) {
    L2_GUARD_FUNCTION();
    m_nNameValue = nameValue;
}

void PledgeCostHelper::SetCost(const PledgeCostInfo& cost) {
    L2_GUARD_FUNCTION();
    if (cost.adena > 0) {
        NeedItem(57, cost.adena);
    }
    if (cost.item_class > 0 && cost.item_amount > 0) {
        NeedItem(cost.item_class, cost.item_amount);
    }
    SetNameValue(-cost.name_value);
}

void PledgeCostHelper::AppendEtcMani(EtcMani* const etcMani) {
    L2_GUARD_FUNCTION();
    m_etcManis.push_back(etcMani);
}

void PledgeCostHelper::Do(AtomicLambdaJob* const job) {
    L2_GUARD_FUNCTION();
    if (m_nNameValue != 0) {
        AppendEtcMani(new EtcManiUpdatePledgeNameValue(
            m_pPledge->GetWorldId(), static_cast<int>(m_pUser->Id()), m_pPledge->GetDbId(), m_nNameValue
        ));
    }
    CPacketHelperEx<20480> packet;
    EtcManiUtil::AssembleEtcManis(m_etcManis, packet);
    db.RequestManipulatePledgeItem(
        m_pUser, static_cast<int>(m_pPledge->GetDbId()), m_items, job, static_cast<int>(m_etcManis.size()), &packet
    );
}

ItemManipHelper::ItemManipHelper(User* const user, const ItemGetReason::Enum reason) :
    m_pUser(user), m_eReason(reason) {}

ItemManipHelper::~ItemManipHelper() {
    L2_GUARD_FUNCTION();
    EtcManiUtil::Destroy(m_etcManis);
}

void ItemManipHelper::PushManiplutor(const int itemClassId, const std::int64_t amount) {
    L2_GUARD_FUNCTION();
    if (g_ObjectDB.GetObjectW(itemClassId, false) == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeStuff.cpp",
        //     0x95)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeStuff.cpp",
        //     0x95)
        L2_LOG_ERROR(
            Log,
            "Cannot append item manipulation for class ID %d (count %lld): no object definition exists; this entry is "
            "omitted.",
            itemClassId, static_cast<long long>(amount)
        );
        return;
    }
    m_items.emplace_back(itemClassId, amount, 0);
}

void ItemManipHelper::PushPointEtcMani(const UserPointType pointType, const std::int64_t amount) {
    L2_GUARD_FUNCTION();
    PushEtcMani(
        new CETC_MANI_POINT_UPDATE(m_pUser->Id(), static_cast<int>(m_pUser->GetDBID()), pointType, static_cast<int>(amount))
    );
}

void ItemManipHelper::PushEtcMani(EtcMani* const etcMani) {
    L2_GUARD_FUNCTION();
    m_etcManis.push_back(etcMani);
}

void ItemManipHelper::Do(AtomicLambdaJob* const job) {
    L2_GUARD_FUNCTION();
    std::array<ItemManip, 100> manipulations{};
    const int itemCount = static_cast<int>(std::min<std::size_t>(m_items.size(), manipulations.size()));
    for (int index = 0; index < itemCount; ++index) {
        manipulations[static_cast<std::size_t>(index)].mClassId =
            runtime::CheckedVectorAt(m_items, static_cast<std::size_t>(index)).ItemID;
        manipulations[static_cast<std::size_t>(index)].mCount =
            runtime::CheckedVectorAt(m_items, static_cast<std::size_t>(index)).Count;
    }

    CPacketHelperEx<20480> packet;
    EtcManiUtil::AssembleEtcManis(m_etcManis, packet);
    User::etcMsg message = m_pUser->SaveEtcMsg(false, false, -2, 0, 0, std::u16string(), std::u16string(), 0);
    User::ManipulateQueue queue = m_pUser->SaveManipulateQueue(
        ManipulateType::MANI_WITH_LAMBDA_JOB, 0, itemCount, manipulations.data(), job, static_cast<int>(m_etcManis.size()),
        packet, false, m_eReason, message
    );

    const User::ManiItemResultType result = m_pUser->_SetManiItemJob(queue);
    if (result == User::MRT_NOJOB) {
        if (!m_pUser->_HandleItemManipulation(queue)) {
            DoJob(job, -2, true);
            DestroyJob(job);
        }
    } else if (result == User::MRT_JOBMAP) {
        DoJob(job, -3, true);
        DestroyJob(job);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][PLEDGE][%s][%d]ManiItemQueue Duple",
        //     L"PledgeStuff.cpp",
        //     0xe9)
        // Noise: A duplicate item request already completes its callback with -3 and destroys the job. This trailing generic
        // queue trace adds no failure context. L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][PLEDGE][%s][%d]ManiItemQueue Duple",
        //     L"PledgeStuff.cpp",
        //     0xe9)
    } else if (result == User::MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][PLEDGE][%s][%d]ManiItemQueue Insert",
        //     L"PledgeStuff.cpp",
        //     0xf0)
        // Noise: The item operation is queued behind an active operation. Normal queue waiting is not an operator error.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][PLEDGE][%s][%d]ManiItemQueue Insert",
        //     L"PledgeStuff.cpp",
        //     0xf0)
    } else {
        DoJob(job, -2, true);
        DestroyJob(job);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][PLEDGE][%s][%d]ManiItemQueue ERROR",
        //     L"PledgeStuff.cpp",
        //     0xf6)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][PLEDGE][%s][%d]ManiItemQueue ERROR",
        //     L"PledgeStuff.cpp",
        //     0xf6)
        L2_LOG_ERROR(
            Log,
            "Clan item manipulation returned unexpected queue result %d; the callback has been invoked with -2 and the job "
            "destroyed.",
            static_cast<int>(result)
        );
    }
}

PledgeWarehouseManipHelper::PledgeWarehouseManipHelper(User* const user, CPledge* const pledge) :
    m_pUser(user), m_pPledge(pledge) {}

PledgeWarehouseManipHelper::~PledgeWarehouseManipHelper() {
    L2_GUARD_FUNCTION();
    EtcManiUtil::Destroy(m_etcManis);
}

void PledgeWarehouseManipHelper::AddItem(const int itemId, const std::int64_t count) {
    L2_GUARD_FUNCTION();
    for (CreateItemStruct& item : m_items) {
        if (item.ItemID == itemId) {
            item.Count += count;
            return;
        }
    }
    m_items.emplace_back(itemId, count, 0);
}

void PledgeWarehouseManipHelper::AppendEtcMani(EtcMani* const etcMani) {
    L2_GUARD_FUNCTION();
    m_etcManis.push_back(etcMani);
}

void PledgeWarehouseManipHelper::Do(AtomicLambdaJob* const job) {
    L2_GUARD_FUNCTION();
    CPacketHelperEx<20480> packet;
    EtcManiUtil::AssembleEtcManis(m_etcManis, packet);
    const int userDatabaseId = m_pUser != nullptr ? static_cast<int>(m_pUser->GetDBID()) : 0;
    db.RequestManipulatePledgeItemNew(
        static_cast<int>(m_pPledge->GetDbId()), userDatabaseId, m_items, job, static_cast<int>(m_etcManis.size()), &packet
    );
}

bool IsValidMasteryID(PledgeMasteryID::Type masteryId) noexcept { return static_cast<std::uint32_t>(masteryId) - 1U < 16U; }

std::int32_t CalculateExpiryTime(const int seconds) { return seconds == 0 ? 0 : runtime::crt::Time32Now() + seconds; }

void AddPrivilegeByLevelAndMastery(CPledge* const pledge, const bool synchronizeRaid) {
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeStuff.cpp",
        //     0x112,
        //     L"(pPledge != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeStuff.cpp",
        //     0x112,
        //     L"(pPledge != NULL)")
        L2_LOG_ERROR(Log, "Cannot update clan privileges from level and mastery: the clan is unavailable.");
        return;
    }
    pledge->GetFunction()->UpdateFunction(static_cast<int>(pledge->GetPledgeLevel()), pledge->GetMastery());
    PledgeSkillStuff::AddPledgeSkillsByLevelAndMastery(pledge, synchronizeRaid);
    PledgeItemStuff::AddPledgeItemByLevelAndMastery(pledge);
}

void RemovePrivilegeByMastery(CPledge* const pledge) {
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        return;
    }
    pledge->GetFunction()->UpdateFunction(static_cast<int>(pledge->GetPledgeLevel()), pledge->GetMastery());
    pledge->RemovePledgeSkills(true);
    PledgeItemStuff::RequestRemovePledgeItems(pledge, true);
}

void DoJob(AtomicJob* const job, const int result, const bool strict) {
    L2_GUARD_FUNCTION();
    if (job == nullptr) {
        return;
    }
    if (job->eType == AtomicType::LambdaJob) {
        static_cast<AtomicLambdaJob*>(job)->m_nResult = result;
    } else if (strict) {
        return;
    }
    job->Do();
}

void DoJobDx(AtomicJob* const job, CDX_ATOMICJOB_BASE* const packet, const bool strict) {
    L2_GUARD_FUNCTION();
    if (job == nullptr) {
        return;
    }
    if (job->eType == AtomicType::LambdaJobDx) {
        static_cast<AtomicLambdaJobDx*>(job)->m_pDx = packet;
    } else if (strict) {
        return;
    }
    job->Do();
}

void DoJobPacket(AtomicJob* const job, network::IPacket* const packet, const bool strict) {
    L2_GUARD_FUNCTION();
    if (job == nullptr) {
        return;
    }
    if (job->eType == AtomicType::LambdaJobPacket) {
        static_cast<AtomicLambdaJobPacket*>(job)->m_pPacket = packet;
    } else if (strict) {
        return;
    }
    job->Do();
}

void DestroyJob(AtomicJob* const job) {
    L2_GUARD_FUNCTION();
    if (job != nullptr) {
        job->Destroy();
    }
}

int GetAttributeAsClassID(const XMLElement* element, const std::u16string& attributeName) {
    L2_GUARD_FUNCTION();

    const std::u16string empty;
    const std::u16string& value = XMLUtil::GetAttributeValue(element, attributeName, empty);
    if (value.empty()) {
        return -1;
    }
    return g_ObjectDB.GetClassIdFromName(value.c_str(), false);
}

namespace {

int GetPledgeLevelCostNameValue(int level) {
    const PledgeLevelInfo* const info = PledgeLevelDB::inst().FindInfo(level);
    return info != nullptr ? info->Cost.name_value : 0;
}

protocol::S_EX_PLEDGE_SHOW_INFO_UPDATE MakePledgeInfoUpdate(CPledge& pledge) {
    protocol::S_EX_PLEDGE_SHOW_INFO_UPDATE packet{};
    packet.nPledgeSId = static_cast<std::int32_t>(pledge.Id());
    packet.nNextLevelNameValue = GetPledgeLevelCostNameValue(static_cast<int>(pledge.GetPledgeLevel()) + 1);
    PledgeFunction* const function = pledge.GetFunction();
    packet.nMaster = function->GetMaxMemberCount();
    packet.nElite = function->GetMaxEliteCount();
    return packet;
}

} // namespace

int Assemble_S_EX_PLEDGE_SHOW_INFO_UPDATE(char* buffer, int bufferSize, CPledge* pledge) {
    L2_GUARD_FUNCTION();
    if (buffer == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeStuff.cpp",
        //     0x147,
        //     L"(pBuffer != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeStuff.cpp",
        //     0x147,
        //     L"(pBuffer != NULL)")
        L2_LOG_ERROR(Log, "Cannot encode clan-information update: the output buffer is unavailable; returning zero bytes.");
        return 0;
    }
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeStuff.cpp",
        //     0x148,
        //     L"(pPledge != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeStuff.cpp",
        //     0x148,
        //     L"(pPledge != NULL)")
        L2_LOG_ERROR(Log, "Cannot encode clan-information update: the clan is unavailable; returning zero bytes.");
        return 0;
    }

    const protocol::S_EX_PLEDGE_SHOW_INFO_UPDATE packet = MakePledgeInfoUpdate(*pledge);
    network::Stream stream(reinterpret_cast<unsigned char*>(buffer), bufferSize);
    const std::uint8_t packetId = 0xfe;
    const std::int16_t extendedPacketId = 0x1df;
    if (!stream.Encode(&packetId) || !stream.Encode(&extendedPacketId) || !packet.Encode(&stream)) {
        return 0;
    }
    return static_cast<int>(stream.GetCursor() - stream.GetBuffer());
}

void Send_S_EX_PLEDGE_SHOW_INFO_UPDATE(User* user, CPledge* pledge) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeStuff.cpp",
        //     0x132,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeStuff.cpp",
        //     0x132,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot send clan-information update: the user is unavailable.");
        return;
    }
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeStuff.cpp",
        //     0x133,
        //     L"(pPledge != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeStuff.cpp",
        //     0x133,
        //     L"(pPledge != NULL)")
        L2_LOG_ERROR(Log, "Cannot send clan-information update: the clan is unavailable.");
        return;
    }

    protocol::S_EX_PLEDGE_SHOW_INFO_UPDATE packet = MakePledgeInfoUpdate(*pledge);
    SendPacket(user, S_EX_PLEDGE_SHOW_INFO_UPDATE, packet);
}

void SendSystemMessage(CUserSocket* const socket, const std::u16string& message) {
    L2_GUARD_FUNCTION();
    if (socket == nullptr) {
        return;
    }

    socket->SendSystemMessage(u"SYS", message);
}

void SendSystemMessage(User* const user, const std::u16string& message) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        SendSystemMessage(user->GetSocket(), message);
    }
}

void SendSystemMessage_MasteryID(CPledge* const pledge, const int messageId, const PledgeMasteryID::Type masteryId) {
    // Current L2Server 0x009e0ce8.
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        return;
    }

    CPacketHelperEx<20480> packet;
    packet.Assemble("chccc", 0x62, static_cast<std::int16_t>(messageId), 1, 0x19, static_cast<int>(masteryId));
    GenericService service(&packet);
    pledge->ServeForAllMember(service, false);
}

void SendSystemMessage_Skill(
    CPledge* const pledge, const int messageId, const int skillId, const int skillLevel, const int skillSubLevel
) {
    // Current L2Server 0x009e0b5c.
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        return;
    }

    CPacketHelperEx<20480> packet;
    packet.Assemble(
        "chccdd", 0x62, static_cast<std::int16_t>(messageId), 1, 4, skillId, (skillSubLevel << 16) | (skillLevel & 0xffff)
    );
    GenericService service(&packet);
    pledge->ServeForAllMember(service, false);
}

void SendSystemMessage_Item(CPledge* const pledge, const int messageId, const int itemClassId) {
    // Current L2Server 0x009e08f4.
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        return;
    }

    SystemMessage_Item<NoCheck, User> service(messageId, itemClassId);
    pledge->ServeForAllMember(service, false);
}

void SendSystemMessage_UsernameAndItem(
    CPledge* const pledge, const int messageId, const char16_t* const username, const int itemClassId
) {
    // Current L2Server 0x009e0828.
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        return;
    }

    CPacketHelperEx<20480> packet;
    packet.Assemble("chccScd", 0x62, static_cast<std::int16_t>(messageId), 2, 0, username, 3, itemClassId);
    GenericService service(&packet);
    pledge->ServeForAllMember(service, false);
}

void Write_LOG_PLEDGE_CONTRIB_ADD(const User* const user, const CPledge* const pledge, const int point) {
    L2_GUARD_FUNCTION();
    Write_LOG_TEMPLETE(0x826, user, pledge, point, 0, 0, 0, 0);
}

void Write_LOG_PLEDGE_CONTRIB_PERIOD_CHANGE(const int season, const std::int32_t nextSeasonTime) {
    L2_GUARD_FUNCTION();
    Write_LOG_TEMPLETE(0x827, nullptr, nullptr, season, nextSeasonTime, 0, 0, 0);
}

void Write_LOG_PLEDGE_CONTRIB_REWARD(
    const User* const user,
    const CPledge* const pledge,
    const int step,
    const int point,
    const int minimumPoint,
    const ActionStep::Value action
) {
    L2_GUARD_FUNCTION();
    Write_LOG_TEMPLETE(0x828, user, pledge, step, point, minimumPoint, 0, action);
}

void Write_LOG_PLEDGE_ITEM_ADD(const CPledge* const pledge, const int itemClassId, const int expiryTimeSeconds) {
    L2_GUARD_FUNCTION();
    Write_LOG_TEMPLETE(0x824, nullptr, pledge, itemClassId, expiryTimeSeconds, 0, 0, 0);
}

void Write_LOG_PLEDGE_ITEM_ACTIVATE(
    const User* const user,
    const CPledge* const pledge,
    const int itemClassId,
    const int expiryTimeSeconds,
    const ActionStep::Value action
) {
    L2_GUARD_FUNCTION();
    Write_LOG_TEMPLETE(0x822, user, pledge, itemClassId, expiryTimeSeconds, 0, 0, action);
}

void Write_LOG_PLEDGE_ITEM_BUY(
    const User* const user, const CPledge* const pledge, const int itemClassId, const ActionStep::Value action
) {
    L2_GUARD_FUNCTION();
    Write_LOG_TEMPLETE(0x823, user, pledge, itemClassId, 0, 0, 0, action);
}

void Write_LOG_PLEDGE_ITEM_REMOVE(const CPledge* const pledge, const int itemClassId) {
    L2_GUARD_FUNCTION();
    Write_LOG_TEMPLETE(0x825, nullptr, pledge, itemClassId, 0, 0, 0, 0);
}

void Write_LOG_PLEDGE_LEVEL_UP(
    const User* const user, const CPledge* const pledge, const int level, const ActionStep::Value action
) {
    L2_GUARD_FUNCTION();
    Write_LOG_TEMPLETE(0x81c, user, pledge, level, 0, 0, 0, action);
}

void Write_LOG_PLEDGE_MASTERY_RESET(const User* const user, const CPledge* const pledge, const ActionStep::Value action) {
    L2_GUARD_FUNCTION();
    Write_LOG_TEMPLETE(0x81e, user, pledge, 0, 0, 0, 0, action);
}

void Write_LOG_PLEDGE_MASTERY_SET(
    const User* const user,
    const CPledge* const pledge,
    const PledgeMasteryID::Type masteryId,
    const int point,
    const ActionStep::Value action
) {
    L2_GUARD_FUNCTION();
    Write_LOG_TEMPLETE(0x81d, user, pledge, static_cast<int>(masteryId), point, 0, 0, action);
}

void Write_LOG_PLEDGE_SKILL_ACTIVATE(
    const User* const user,
    const CPledge* const pledge,
    const int skillId,
    const int skillLevel,
    const int remainSecond,
    const int expirySecond,
    const ActionStep::Value action
) {
    L2_GUARD_FUNCTION();
    Write_LOG_TEMPLETE(0x81f, user, pledge, skillId, skillLevel, remainSecond, expirySecond, action);
}

void Write_LOG_PLEDGE_SKILL_ADD(
    const CPledge* const pledge, const int skillId, const int skillLevel, const int expirySecond
) {
    L2_GUARD_FUNCTION();
    Write_LOG_TEMPLETE(0x820, nullptr, pledge, skillId, skillLevel, expirySecond, 0, 0);
}

void Write_LOG_PLEDGE_SKILL_REMOVE(const CPledge* const pledge, const int skillId) {
    // Current L2Server 0x009dfe50.
    L2_GUARD_FUNCTION();
    Write_LOG_TEMPLETE(0x821, nullptr, pledge, skillId, 0, 0, 0, 0);
}

} // namespace PledgeStuff
