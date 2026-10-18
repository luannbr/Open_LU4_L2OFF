#include "PledgeMemberService.h"
#include "Alliance.h"
#include "Castle.h"
#include "HtmlGenerator.h"
#include "InstantAgit.h"
#include "Item.h"
#include "Packet.h"
#include "Pet.h"
#include "Pledge.h"
#include "PledgeRaid.h"
#include "PledgeStuff.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Raid.h"
#include "Residence.h"
#include "SerializedMessage.h"
#include "Shared/src/Config.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/SlotType.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/UserConst.h"
#include "User.h"
#include "UserSocket.h"

#include <array>
#include <cstdint>
#include <string>

namespace {


#if !defined(_WIN32)
#endif

} // namespace

CPledgeMemberService::CPledgeMemberService() : m_canServe(true) {}

CPledgeMemberService::~CPledgeMemberService() = default;

SetCastleSiege::SetCastleSiege(const std::int32_t (&castleIds)[9], const std::int32_t (&castleStatuses)[9]) noexcept :
    m_castleIds{}, m_castleStatuses{} {
    std::copy_n(castleIds, 9, m_castleIds);
    std::copy_n(castleStatuses, 9, m_castleStatuses);
}

void SetCastleSiege::ServeInternal(User* const user) {
    // Current L2Server 0x009d8698.
    L2_GUARD_FUNCTION();
    std::copy_n(m_castleIds, 9, user->m_nCastleSiegeId);
    std::copy_n(m_castleStatuses, 9, user->m_nCastleSiegeStatus);
}

void SetCastleSiege::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x009daf38.
    L2_GUARD_FUNCTION();
    if (pledge.GetWorldId() == g_nWorldId) {
        RaidPledgeMemberService_SetCastleSiege service(m_castleIds, m_castleStatuses);
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, service);
    }
}

SetFortressSiege::SetFortressSiege(const int defenceFortressId, const int offenceFortressId) noexcept :
    m_defenceFortressId(defenceFortressId), m_offenceFortressId(offenceFortressId) {}

void SetFortressSiege::ServeInternal(User* const user) {
    // Current L2Server 0x009d875c.
    L2_GUARD_FUNCTION();
    user->m_nFortressUnderDefenceSiegeId = m_defenceFortressId;
    user->m_nFortressUnderOffenceSiegeId = m_offenceFortressId;
}

void SetFortressSiege::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x009db03c.
    L2_GUARD_FUNCTION();
    if (pledge.GetWorldId() == g_nWorldId) {
        RaidPledgeMemberService_SetFortressSiege service(m_defenceFortressId, m_offenceFortressId);
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, service);
    }
}

SetAlliance::SetAlliance(CAlliance* const alliance, const bool masterPledge) noexcept :
    m_alliance(alliance), m_isMasterPledges(masterPledge) {}

void SetAlliance::ServeInternal(User* const user) {
    // Current L2Server 0x009d88c4.
    L2_GUARD_FUNCTION();
    user->SetAlliance(m_alliance, m_isMasterPledges);
    if (!user->IsDeleting() && user->m_Deleted == 0) {
        user->ParamChanged(std::int64_t{PCB_USER});
    }
}

void SetAlliance::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x009db1ec.
    L2_GUARD_FUNCTION();
    if (pledge.GetWorldId() == g_nWorldId) {
        std::uint32_t allianceDatabaseId = 0;
        std::int32_t masterPledge = 0;
        if (m_alliance != nullptr) {
            allianceDatabaseId = m_alliance->m_data->m_nDbId;
            masterPledge = m_alliance->m_data->m_nMasterPledgeDbId == pledge.GetDbId();
        }
        RaidPledgeMemberService_SetAlliance service(allianceDatabaseId, masterPledge != 0);
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, service);
    }
}

void SetRelationChanged::ServeInternal(User* const user) {
    // Current L2Server 0x009d8e00.
    L2_GUARD_FUNCTION();
    (void)runtime::raw_atomic::CompareExchangeI32(&user->m_nRelationChanged, 1, 0);
}

void SetRelationChanged::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x009dbb1c.
    L2_GUARD_FUNCTION();
    if (pledge.GetWorldId() == g_nWorldId) {
        RaidPledgeMemberService_SetRelationChanged service;
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, service);
    }
}

void AffectedByWar::ServeInternal(User* const user) {
    // Current L2Server 0x009d8c94.
    L2_GUARD_FUNCTION();
    user->AffectedByWar();
}

void AffectedByWar::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x009db9cc.
    L2_GUARD_FUNCTION();
    if (pledge.GetWorldId() == g_nWorldId) {
        RaidPledgeMemberService_AffectedByWar service;
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, service);
    }
}

void ArrivedMail::ServeInternal(User* const user) {
    // Current L2Server 0x009d8d38.
    L2_GUARD_FUNCTION();
    user->GetSocket()->Send("ch", 0xfe, 0x2e);
}

void ArrivedMail::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x009dba84.
    if (pledge.GetWorldId() == g_nWorldId) {
        CPacketHelperEx<20480> packet;
        const int length = packet.Assemble("ch", 0xfe, 0x2e);
        if (length > 0) {
            RaidPledgeMemberService_PacketRelay relay(length, packet.GetPacket());
            RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, relay);
        }
    }
}

PMS_SyncPledgeSkill::PMS_SyncPledgeSkill(const int skillId, const int skillLevel) noexcept :
    m_skillId(skillId), m_skillLevel(skillLevel) {}

PMS_SyncPledgeType::PMS_SyncPledgeType(const std::uint32_t characterDatabaseId, const PledgeType pledgeType) noexcept :
    m_characterDatabaseId(characterDatabaseId), m_pledgeType(pledgeType) {}

void PMS_SyncPledgeType::ServeInternal(User*) {
    // Current vtable slot 006fe5d4 is the shared folded no-op.
}

void PMS_SyncPledgeType::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x009dc7b8.
    L2_GUARD_FUNCTION();
    if (pledge.GetWorldId() == g_nWorldId) {
        RaidPledgeMemberService_SyncPledgeType service(m_characterDatabaseId, m_pledgeType);
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, service);
    }
}

CSendPledgeSkillAdd::CSendPledgeSkillAdd(const int skillId, const int skillLevel) noexcept :
    m_skillId(skillId), m_skillLevel(skillLevel) {}

PMS_SyncPledgeLevel::PMS_SyncPledgeLevel(const int level) noexcept :
    m_level(level), m_alignment14To18(0) {}

PMS_SyncPledgeMastery::PMS_SyncPledgeMastery(const PledgeMasteryID::Type masteryId, const int point) noexcept :
    m_id(masteryId), m_nPoint(point) {}

void PMS_SyncPledgeMastery::ServeInternal(User*) {
    // Current local slot is empty. The owning pledge already
    // applied the point before broadcasting this cross-world command.
}

void PMS_SyncPledgeMastery::ServeInternalForRaid(CPledge& pledge) {
    L2_GUARD_FUNCTION();
    if (pledge.GetWorldId() == g_nWorldId) {
        RaidPledgeMemberService_SyncPledgeMastery service(m_id, m_nPoint);
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, service);
    }
}

void PMS_SyncPledgeSkill::ServeInternal(User*) {
    // Current slot is a folded no-op: CPledge updates local users directly.
}

void PMS_SyncPledgeSkill::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x009dc890.
    L2_GUARD_FUNCTION();
    if (pledge.GetWorldId() == g_nWorldId) {
        RaidPledgeMemberService_SyncPledgeSkill service(m_skillId, m_skillLevel);
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, service);
    }
}

void CSendPledgeSkillAdd::ServeInternal(User* const user) {
    L2_GUARD_FUNCTION();
    user->GetSocket()->Send("chddd", 0xfe, 0x3b, m_skillId, m_skillLevel);
}

void CSendPledgeSkillAdd::ServeInternalForRaid(CPledge& pledge) {
    L2_GUARD_FUNCTION();
    if (pledge.GetWorldId() != g_nWorldId) {
        return;
    }
    CPacketHelperEx<20480> packet;
    const int length = packet.Assemble("chddd", 0xfe, 0x3b, m_skillId, m_skillLevel);
    if (length > 0) {
        RaidPledgeMemberService_PacketRelay relay(length, packet.GetPacket());
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, relay);
    }
}

void PMS_SyncPledgeLevel::ServeInternal(User*) {
    // The owner has already changed local state. This command only crosses
    // the raid boundary in the current product.
}

void PMS_SyncPledgeLevel::ServeInternalForRaid(CPledge& pledge) {
    L2_GUARD_FUNCTION();
    if (pledge.GetWorldId() == g_nWorldId) {
        RaidPledgeMemberService_SyncPledgeLevel service(m_level);
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, service);
    }
}

void SetCrest::ServeInternal(User* const user) {
    // Current L2Server 0x009d8124.
    L2_GUARD_FUNCTION();
    user->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
        reinterpret_cast<std::int32_t&>(user->m_nPledgeCrestDbId), m_crestId, 0x30
    );
    if (!user->IsDeleting() && user->m_Deleted == 0) {
        user->ParamChanged(std::int64_t{PCB_USER});
    }
}

void SetCrest::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x009da7ec.
    L2_GUARD_FUNCTION();
    if (pledge.GetWorldId() == g_nWorldId) {
        RaidPledgeMemberService_SetCrest service(m_crestId);
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, service);
    }
}

void SetEmblem::ServeInternal(User* const user) {
    // Current L2Server 0x009d8204 has no guard frame.
    user->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
        reinterpret_cast<std::int32_t&>(user->m_nPledgeEmblemDbId), m_emblemId, 0x32
    );
    if (!user->IsDeleting() && user->m_Deleted == 0) {
        user->ParamChanged(std::int64_t{PCB_USER});
    }
}

void SetEmblem::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x009da8c0.
    L2_GUARD_FUNCTION();
    if (pledge.GetWorldId() == g_nWorldId) {
        RaidPledgeMemberService_SetEmblem service(m_emblemId);
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, service);
    }
}

void SetAllianceCrest::ServeInternal(User* const user) {
    // Current L2Server 0x009d9118.
    L2_GUARD_FUNCTION();
    user->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
        reinterpret_cast<std::int32_t&>(user->m_nAllianceCrestDbId), m_allianceCrestId, 0x31
    );
    if (!user->IsDeleting() && user->m_Deleted == 0) {
        user->ParamChanged(std::int64_t{PCB_USER});
    }
}

void SetAllianceCrest::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x009dbdac.
    L2_GUARD_FUNCTION();
    if (pledge.GetWorldId() == g_nWorldId) {
        RaidPledgeMemberService_SetAllianceCrest service(m_allianceCrestId);
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, service);
    }
}

void SetParamChanged::ServeInternal(User* user) {
    // Current L2Server 0x009d8258: the comparison is unsigned and the
    // accepted parameter mask is strictly below bit 44.
    L2_GUARD_FUNCTION();
    if (static_cast<std::uint64_t>(m_flag) < UINT64_C(0x100000000000)) {
        user->m_nParamChanged |= m_flag;
    }
}

void SetParamChanged::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x009da994.
    L2_GUARD_FUNCTION();
    if (pledge.GetWorldId() == g_nWorldId) {
        RaidPledgeMemberService_SetParamChanged service(m_flag);
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, service);
    }
}

void ResetPledgeActivityMark::ServeInternal(User* const user) {
    // Current L2Server 0x009d99f8.
    L2_GUARD_FUNCTION();
    user->GetSocket()->Send("ch", 0xfe, 0x1b0);
}

void ResetPledgeActivityMark::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x009dc42c. The relay carries only the
    // low byte of 0x1b0. The local endpoint above carries FE:01B0.
    L2_GUARD_FUNCTION();
    if (pledge.GetWorldId() != g_nWorldId) {
        return;
    }
    std::array<char, 0x5000> packet{};
    const int length = ::Assemble(packet.data(), static_cast<int>(packet.size()), "c", 0x1b0);
    if (length > 0) {
        RaidPledgeMemberService_PacketRelay relay(length, packet.data());
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, relay);
    }
}

UpdateMemberList::UpdateMemberList(User* const updatedMember, const bool online) noexcept :
    m_updatedMember(updatedMember), m_isOnline(online), m_padding19To20{} {}

namespace {

int AssembleUpdatedPledgeMember(CPacketHelperEx<20480>& packet, User& updatedMember, const bool online) {
    const std::uint32_t serverId = online ? updatedMember.GetServerId() : 0;
    return packet.Assemble(
        "cSdddddddc", 0x5b, updatedMember.GetName(), updatedMember.m_data->m_nLevel, updatedMember.m_data->m_nClass,
        static_cast<int>(updatedMember.m_data->m_Sex), static_cast<int>(updatedMember.m_data->m_Race), serverId,
        updatedMember.m_data->m_nPledgeType, updatedMember.HasAcademyStudentMasterRelation() ? 1 : 0,
        updatedMember.GetPledgeActivity()
    );
}

} // namespace

void UpdateMemberList::ServeInternal(User* const user) {
    // Current L2Server 0x009d8990.
    const std::uint32_t serverId = m_isOnline ? m_updatedMember->GetServerId() : 0;
    CUserSocket* const socket = user->GetSocket();
    auto* const updatedData = m_updatedMember->m_data;
    const std::int32_t pledgeType = updatedData->m_nPledgeType;
    const PledgeBonus::Activity pledgeActivity = m_updatedMember->GetPledgeActivity();
    const bool hasAcademyRelation = m_updatedMember->HasAcademyStudentMasterRelation();
    const auto updatedName = std::u16string(m_updatedMember->GetName());
    socket->Send(
        "cSdddddddc", 0x5b, updatedName.c_str(), updatedData->m_nLevel, updatedData->m_nClass,
        static_cast<int>(updatedData->m_Sex), static_cast<int>(updatedData->m_Race), serverId, pledgeType,
        hasAcademyRelation ? 1 : 0, pledgeActivity
    );
}

void UpdateMemberList::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x009db2e8.
    if (pledge.GetWorldId() != g_nWorldId) {
        return;
    }
    CPacketHelperEx<20480> packet;
    const int length = AssembleUpdatedPledgeMember(packet, *m_updatedMember, m_isOnline);
    if (length > 0) {
        RaidPledgeMemberService_PacketRelay relay(length, packet.GetPacket());
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, relay);
    }
}

AddWindowPacket::AddWindowPacket(User* const member) noexcept : m_member(member) {}

void AddWindowPacket::ServeInternal(User* const user) {
    // Current L2Server 0x009d830c.
    L2_GUARD_FUNCTION();
    if (user->Id() == m_member->Id()) {
        return;
    }
    user->GetSocket()->Send(
        "cSddddddc", 0x5c, m_member->GetName(), m_member->Level(), m_member->m_data->m_nClass,
        static_cast<int>(m_member->m_data->m_Sex), static_cast<int>(m_member->m_data->m_Race), m_member->GetServerId(),
        m_member->m_data->m_nPledgeType, m_member->GetPledgeActivity()
    );
}

void AddWindowPacket::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x009daa68.
    L2_GUARD_FUNCTION();
    std::array<char, 0x5000> packet{};
    const int length = ::Assemble(
        packet.data(), static_cast<int>(packet.size()), "cSddddddc", 0x5c, m_member->GetName(), m_member->Level(),
        m_member->m_data->m_nClass, static_cast<int>(m_member->m_data->m_Sex), static_cast<int>(m_member->m_data->m_Race),
        m_member->GetServerId(), m_member->m_data->m_nPledgeType, m_member->GetPledgeActivity()
    );
    if (length > 0) {
        RaidPledgeMemberService_PacketRelay relay(length, packet.data());
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, relay);
    }
}

void SetPledgeMember::ServeInternal(User* const user) {
    // Current L2Server 0x009d91f8.
    L2_GUARD_FUNCTION();
    m_pledge->SetPledgeMember(user);
}

void SetPledgeMember::ServeInternalForRaid(CPledge&) {
    // Current slot 0x006fe5d4 is the folded no-op.
}

void OpenAllWindowPacket::ServeInternal(User* const user) {
    // Current L2Server 0x009d847c.
    L2_GUARD_FUNCTION();
    m_pledge->OpenAllWindowPacket(user, false, nullptr);
}

void OpenAllWindowPacket::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x009dac28.
    L2_GUARD_FUNCTION();
    if (pledge.GetWorldId() == g_nWorldId) {
        RaidPledgeMemberService_OpenAllWindowPacket service;
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, service);
    }
}

void CSendPledgeOnlineMemberCount::ServeInternal(User* const user) {
    // Current L2Server packet endpoint, FE:13D.
    L2_GUARD_FUNCTION();
    user->GetSocket()->Send("chd", 0xfe, 0x13d, m_onlineCount);
}

void CSendPledgeOnlineMemberCount::ServeInternalForRaid(CPledge& pledge) {
    if (pledge.GetWorldId() != g_nWorldId) {
        return;
    }
    CPacketHelperEx<20480> packet;
    const int length = packet.Assemble("chd", 0xfe, 0x13d, m_onlineCount);
    if (length > 0) {
        RaidPledgeMemberService_PacketRelay relay(length, packet.GetPacket());
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, relay);
    }
}

void CSendPledgeSkillList::ServeInternal(User* const user) {
    // Current L2Server packet endpoint, FE:03A.
    L2_GUARD_FUNCTION();
    if (m_packet == nullptr) {
        return;
    }
    user->GetSocket()->Send(
        "chddb", 0xfe, 0x3a, m_pledgeSkillCount, m_subPledgeSkillCount, m_packet->GetLength(), m_packet->GetPacket()
    );
}

void CSendPledgeSkillList::ServeInternalForRaid(CPledge& pledge) {
    if (pledge.GetWorldId() != g_nWorldId || m_packet == nullptr) {
        return;
    }
    CPacketHelperEx<20480> packet;
    const int length = packet.Assemble(
        "chddb", 0xfe, 0x3a, m_pledgeSkillCount, m_subPledgeSkillCount, m_packet->GetLength(), m_packet->GetPacket()
    );
    if (length > 0) {
        RaidPledgeMemberService_PacketRelay relay(length, packet.GetPacket());
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, relay);
    }
}

void CPledgeMemberService::Serve(User* user) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        ServeInternal(user);
    }
}

void CPledgeMemberService::ServeForRaid(CPledge& pledge) {
    L2_GUARD_FUNCTION();
    if (pledge.IsUnderRaid()) {
        ServeInternalForRaid(pledge);
    }
}

template <>
SystemMessage<NoCheck, User>::SystemMessage(const int messageId) noexcept :
    NoCheck<User>(), m_messageId(messageId), m_skillId(0), m_skillLevel(0), m_alignment1cTo20(0) {}

template <>
void SystemMessage<NoCheck, User>::ServeInternal(User* user) {
    if (m_skillId > 0) {
        user->GetSocket()->Send(
            "chccdd", 0x62, static_cast<std::int16_t>(m_messageId), 1, 4, m_skillId, static_cast<std::uint16_t>(m_skillLevel)
        );
        return;
    }
    user->GetSocket()->SendSystemMessage(m_messageId);
}

template <>
void SystemMessage<NoCheck, User>::ServeInternalForRaid(CPledge& pledge) {
    CPacketHelperEx<20480> packet;
    const int length = packet.Assemble("chc", 0x62, static_cast<std::int16_t>(m_messageId), 0);
    if (length > 0) {
        RaidPledgeMemberService_PacketRelay relay(length, packet.GetPacket());
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, relay);
    }
}

template class SystemMessage<NoCheck, User>;

template <>
SystemMessage_Item<NoCheck, User>::SystemMessage_Item(const int messageId, const int itemClassId, User*) noexcept :
    NoCheck<User>(), m_msgNo(messageId), m_idItem(itemClassId) {}

template <>
SystemMessage_Item<NoCheck, User>::SystemMessage_Item(const int messageId, const int itemClassId) noexcept :
    NoCheck<User>(), m_msgNo(messageId), m_idItem(itemClassId) {}

template <>
void SystemMessage_Item<NoCheck, User>::ServeInternal(User* user) {
    // Current L2Server 0x009e0730.
    user->GetSocket()->Send("chccd", 0x62, static_cast<std::int16_t>(m_msgNo), 1, 3, m_idItem);
}

template <>
void SystemMessage_Item<NoCheck, User>::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x009e21a4.
    CPacketHelperEx<20480> packet;
    const int length = packet.Assemble("chccd", 0x62, static_cast<std::int16_t>(m_msgNo), 1, 3, m_idItem);
    if (length > 0) {
        RaidPledgeMemberService_PacketRelay relay(length, packet.GetPacket());
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, relay);
    }
}

template class SystemMessage_Item<NoCheck, User>;

template <>
SystemMessage_S<NoCheck, User>::SystemMessage_S(
    const int messageId, const eSMsgType argumentType, const char16_t* const text, User*
) :
    NoCheck<User>(), m_msgNo(messageId), m_str(text ? text : u""), m_eType(argumentType) {}

template <>
SystemMessage_S<NoCheck, User>::SystemMessage_S(
    const int messageId, const eSMsgType argumentType, const char16_t* const text
) :
    NoCheck<User>(), m_msgNo(messageId), m_str(text ? text : u""), m_eType(argumentType) {}

template <>
void SystemMessage_S<NoCheck, User>::ServeInternal(User* user) {
    user->GetSocket()->Send("chccS", 0x62, static_cast<std::int16_t>(m_msgNo), 1, static_cast<int>(m_eType), m_str.c_str());
}

template <>
void SystemMessage_S<NoCheck, User>::ServeInternalForRaid(CPledge& pledge) {
    CPacketHelperEx<20480> packet;
    const int length =
        packet.Assemble("chccS", 0x62, static_cast<std::int16_t>(m_msgNo), 1, static_cast<int>(m_eType), m_str.c_str());
    if (length > 0) {
        RaidPledgeMemberService_PacketRelay relay(length, packet.GetPacket());
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, relay);
    }
}

template class SystemMessage_S<NoCheck, User>;

template <>
bool SameMemberCheck<User>::Check(User* const user) const {
    // Current L2Server 0x009d7ef0.
    if (m_t == nullptr || user == nullptr) {
        return false;
    }
    return m_t->Id() != user->Id();
}

template <>
SystemMessage_S<SameMemberCheck, User>::SystemMessage_S(
    const int messageId, const eSMsgType argumentType, const char16_t* const text, User* const object
) :
    SameMemberCheck<User>(object),
    m_msgNo(messageId),
    m_str(text ? text : u""),
    m_eType(argumentType) {}

template <>
void SystemMessage_S<SameMemberCheck, User>::ServeInternal(User* const user) {
    if (!Check(user)) {
        return;
    }
    user->GetSocket()->Send("chccS", 0x62, static_cast<std::int16_t>(m_msgNo), 1, static_cast<int>(m_eType), m_str.c_str());
}

template <>
void SystemMessage_S<SameMemberCheck, User>::ServeInternalForRaid(CPledge& pledge) {
    CPacketHelperEx<20480> packet;
    const int length =
        packet.Assemble("chccS", 0x62, static_cast<std::int16_t>(m_msgNo), 1, static_cast<int>(m_eType), m_str.c_str());
    if (length > 0) {
        RaidPledgeMemberService_PacketRelay relay(length, packet.GetPacket());
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, relay);
    }
}

template <>
SystemMessage_d<NoCheck, User>::SystemMessage_d(const int messageId, const int parameter) noexcept :
    NoCheck<User>(), m_msgNo(messageId), m_nParam(parameter) {}

template <>
void SystemMessage_d<NoCheck, User>::ServeInternal(User* user) {
    user->GetSocket()->SendSystemMessage_d(m_msgNo, m_nParam);
}

template <>
void SystemMessage_d<NoCheck, User>::ServeInternalForRaid(CPledge& pledge) {
    CPacketHelperEx<20480> packet;
    const int length = packet.Assemble("chccd", 0x62, static_cast<std::int16_t>(m_msgNo), 1, 1, m_nParam);
    if (length > 0) {
        RaidPledgeMemberService_PacketRelay relay(length, packet.GetPacket());
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, relay);
    }
}

template class SystemMessage_d<NoCheck, User>;

template <>
SystemMessage_S_d<NoCheck, User>::SystemMessage_S_d(
    const int messageId, const eSMsgType argumentType, const char16_t* const text, const int parameter
) :
    NoCheck<User>(),
    m_msgNo(messageId),
    m_str(text ? text : u""),
    m_nParam(parameter),
    m_eType(argumentType) {}

template <>
void SystemMessage_S_d<NoCheck, User>::ServeInternal(User* user) {
    // Current L2Server 0x00970a54.
    user->GetSocket()->Send(
        "chccScd", 0x62, static_cast<std::int16_t>(m_msgNo), 2, static_cast<int>(m_eType), m_str.c_str(), 1, m_nParam
    );
}

template <>
void SystemMessage_S_d<NoCheck, User>::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x0098ad38.
    CPacketHelperEx<20480> packet;
    const int length = packet.Assemble(
        "chccScd", 0x62, static_cast<std::int16_t>(m_msgNo), 2, static_cast<int>(m_eType), m_str.c_str(), 1, m_nParam
    );
    if (length > 0) {
        RaidPledgeMemberService_PacketRelay relay(length, packet.GetPacket());
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, relay);
    }
}

template class SystemMessage_S_d<NoCheck, User>;

template <>
SystemMessage_S_S_d<NoCheck, User>::SystemMessage_S_S_d(
    const int messageId,
    const eSMsgType firstArgumentType,
    const char16_t* const firstText,
    const eSMsgType secondArgumentType,
    const char16_t* const secondText,
    const int parameter
) :
    NoCheck<User>(),
    m_msgNo(messageId),
    m_firstText(firstText ? firstText : u""),
    m_secondText(secondText ? secondText : u""),
    m_nParam(parameter),
    m_firstType(firstArgumentType),
    m_secondType(secondArgumentType) {}

template <>
void SystemMessage_S_S_d<NoCheck, User>::ServeInternal(User* user) {
    // Current L2Server 0x00970ab8.
    user->GetSocket()->Send(
        "chccScScd", 0x62, static_cast<std::int16_t>(m_msgNo), 3, static_cast<int>(m_firstType), m_firstText.c_str(),
        static_cast<int>(m_secondType), m_secondText.c_str(), 1, m_nParam
    );
}

template <>
void SystemMessage_S_S_d<NoCheck, User>::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x0098aeb4.
    CPacketHelperEx<20480> packet;
    const int length = packet.Assemble(
        "chccScScd", 0x62, static_cast<std::int16_t>(m_msgNo), 3, static_cast<int>(m_firstType), m_firstText.c_str(),
        static_cast<int>(m_secondType), m_secondText.c_str(), 1, m_nParam
    );
    if (length > 0) {
        RaidPledgeMemberService_PacketRelay relay(length, packet.GetPacket());
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, relay);
    }
}

template class SystemMessage_S_S_d<NoCheck, User>;

template <>
SystemMessage_d_d<NoCheck, User>::SystemMessage_d_d(
    const int messageId, const int firstParameter, const int secondParameter
) noexcept :
    NoCheck<User>(),
    m_msgNo(messageId),
    m_firstParameter(firstParameter),
    m_secondParameter(secondParameter),
    m_padding1cTo20(0) {}

template <>
void SystemMessage_d_d<NoCheck, User>::ServeInternal(User* user) {
    // Current L2Server 0x00970b58.
    user->GetSocket()->Send(
        "chccdcd", 0x62, static_cast<std::int16_t>(m_msgNo), 2, 1, m_firstParameter, 1, m_secondParameter
    );
}

template <>
void SystemMessage_d_d<NoCheck, User>::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x0098b040.
    CPacketHelperEx<20480> packet;
    const int length =
        packet.Assemble("chccdcd", 0x62, static_cast<std::int16_t>(m_msgNo), 2, 1, m_firstParameter, 1, m_secondParameter);
    if (length > 0) {
        RaidPledgeMemberService_PacketRelay relay(length, packet.GetPacket());
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, relay);
    }
}

template class SystemMessage_d_d<NoCheck, User>;

template <>
bool BattleFieldChecker<CCastle>::Check(User* user) const {
    return m_t->IsInBattleField(user->GetPos(), true);
}

template <>
SystemMessage<BattleFieldChecker, CCastle>::SystemMessage(
    const int messageId, CCastle* const object, const int skillId, const int skillLevel
) noexcept :
    BattleFieldChecker<CCastle>(object),
    m_messageId(messageId),
    m_skillId(skillId),
    m_skillLevel(skillLevel),
    m_alignment1cTo20(0) {}

template <>
void SystemMessage<BattleFieldChecker, CCastle>::ServeInternal(User* user) {
    if (!Check(user)) {
        return;
    }
    if (m_skillId < 1) {
        user->GetSocket()->SendSystemMessage(m_messageId);
        return;
    }
    user->GetSocket()->Send(
        "chccdd", 0x62, static_cast<std::int16_t>(m_messageId), 1, 4, m_skillId, static_cast<std::uint16_t>(m_skillLevel)
    );
}

template <>
void SystemMessage<BattleFieldChecker, CCastle>::ServeInternalForRaid(CPledge& pledge) {
    CPacketHelperEx<20480> packet;
    const int length = packet.Assemble("chc", 0x62, static_cast<std::int16_t>(m_messageId), 0);
    if (length > 0) {
        RaidPledgeMemberService_PacketRelay relay(length, packet.GetPacket());
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, relay);
    }
}

template class SystemMessage<BattleFieldChecker, CCastle>;

template <>
bool BattleFieldChecker<CResidence>::Check(User* user) const {
    return m_t->IsInBattleField(user->GetPos(), true);
}

template <>
SystemMessage<BattleFieldChecker, CResidence>::SystemMessage(
    const int messageId, CResidence* const object, const int skillId, const int skillLevel
) noexcept :
    BattleFieldChecker<CResidence>(object),
    m_messageId(messageId),
    m_skillId(skillId),
    m_skillLevel(skillLevel) {}

template <>
void SystemMessage<BattleFieldChecker, CResidence>::ServeInternal(User* user) {
    if (!Check(user)) {
        return;
    }
    if (m_skillId < 1) {
        user->GetSocket()->SendSystemMessage(m_messageId);
        return;
    }
    user->GetSocket()->Send(
        "chccdd", 0x62, static_cast<std::int16_t>(m_messageId), 1, 4, m_skillId, static_cast<std::uint16_t>(m_skillLevel)
    );
}

template <>
void SystemMessage<BattleFieldChecker, CResidence>::ServeInternalForRaid(CPledge& pledge) {
    CPacketHelperEx<20480> packet;
    const int length = packet.Assemble("chc", 0x62, static_cast<std::int16_t>(m_messageId), 0);
    if (length > 0) {
        RaidPledgeMemberService_PacketRelay relay(length, packet.GetPacket());
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, relay);
    }
}

template class SystemMessage<BattleFieldChecker, CResidence>;

PMS_PledgeRaidInzoneBinding::PMS_PledgeRaidInzoneBinding(const std::int32_t bindingTime, const bool force) noexcept :
    tTime_(bindingTime), bForce_(force), m_padding15To18{} {}

void PMS_PledgeRaidInzoneBinding::ServeInternal(User* user) {
    L2_GUARD_FUNCTION();
    PledgeRaidSystem& system = Singleton<PledgeRaidSystem>::inst();
    if (bForce_) {
        system.ForceSetPledgeRaidInzoneBinding(user, tTime_);
    } else {
        system.CheckPledgeRaidInzoneBinding(user);
    }
}

void PMS_PledgeRaidInzoneBinding::ServeInternalForRaid(CPledge&) {
    // Current slot is the common folded no-op body at 0x006fe5d4.
}

SetDominionSiege::SetDominionSiege(const int dominionSiegeId, const int dominionSiegeStatus) noexcept :
    m_nDominionSiegeId(dominionSiegeId), m_nDominionSiegeStatus(dominionSiegeStatus) {}

void SetDominionSiege::ServeInternal(User* user) {
    L2_GUARD_FUNCTION();
    user->m_data->m_nDominionSiegeId = m_nDominionSiegeId;
    user->m_data->m_nDominionSiegeStatus = m_nDominionSiegeStatus;
}

void SetDominionSiege::ServeInternalForRaid(CPledge& pledge) {
    L2_GUARD_FUNCTION();
    if (pledge.GetWorldId() != g_nWorldId) {
        return;
    }
    RaidPledgeMemberService_SetDominionSiege service(m_nDominionSiegeId, m_nDominionSiegeStatus);
    RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, service);
}

void CancelRenamed::ServeInternal(User* user) {
    L2_GUARD_FUNCTION();
    user->m_nDominionRenamedID = 0;
    user->PushSerializedMessage(new CSerializedRenameByDominionSiegeMessage(user, false));
}

void CancelRenamed::ServeInternalForRaid(CPledge& pledge) {
    L2_GUARD_FUNCTION();
    if (pledge.GetWorldId() != g_nWorldId) {
        return;
    }
    RaidPledgeMemberService_CancelRenamed service;
    RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, service);
}

void UnequipCastleItem::ServeInternal(User* const user) {
    // Current L2Server 0x009da104. The target visits these slots in this
    // exact order.
    L2_GUARD_FUNCTION();
    const SlotType slots[] = {
        ST_HAIR,
        ST_LBRACELET,
        ST_BACK,
    };
    for (const SlotType slot : slots) {
        CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(user->m_data->m_nSlotIndex[static_cast<int>(slot)]);
        if (item && !user->CanEquip(item.get())) {
            user->UnequipItem(item.get(), false);
        }
    }
}

void UnequipCastleItem::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x009db914.
    L2_GUARD_FUNCTION();
    if (pledge.GetWorldId() == g_nWorldId) {
        RaidPledgeMemberService_UnequipCastleItem service;
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, service);
    }
}

PMS_PledgeMissionCheckAndAddCountInCastleSiege::PMS_PledgeMissionCheckAndAddCountInCastleSiege(
    const PledgeMission::GoalType goalType, const int firstValue, const int secondValue, const int count
) noexcept :
    m_goalType(goalType), m_firstValue(firstValue), m_secondValue(secondValue), m_count(count) {}

void PMS_PledgeMissionCheckAndAddCountInCastleSiege::ServeInternal(User* const user) {
    // Current L2Server 0x009d9c04.
    L2_GUARD_FUNCTION();
    if (user->IsInMySiegeBattleField() != 1) {
        return;
    }
    if (user->m_data->m_nWorldId == g_nWorldId) {
        user->PledgeMissionCheckAndAddCount(m_goalType, m_firstValue, m_secondValue, m_count);
    } else {
        user->PledgeMissionCheckAndAddCountInRaid(m_goalType, m_firstValue, m_secondValue, m_count);
    }
}

void PMS_PledgeMissionCheckAndAddCountInCastleSiege::ServeInternalForRaid(CPledge&) {
    // Current slot is the shared folded no-op at 0x006fe5d4.
}

void PlayMusic::ServeInternal(User* const user) {
    // Current L2Server 0x009d8bec.
    L2_GUARD_FUNCTION();
    user->PlayMusic(u"Siege_Victory");
}

void PlayMusic::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x009db83c.
    if (pledge.GetWorldId() != g_nWorldId) {
        return;
    }
    CPacketHelperEx<20480> packet;
    const int length = packet.Assemble("cdSdddddd", 0x9e, 1, u"Siege_Victory", 0, 0, 0, 0, 0, 0);
    if (length > 0) {
        RaidPledgeMemberService_PacketRelay relay(length, packet.GetPacket());
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, relay);
    }
}

CloseWindowPacket::CloseWindowPacket(const char16_t* memberName) :
    m_memberName(memberName ? memberName : u"") {}

void CloseWindowPacket::ServeInternal(User* user) {
    L2_GUARD_FUNCTION();
    user->GetSocket()->Send("cS", 0x5d, m_memberName.c_str());
}

void CloseWindowPacket::ServeInternalForRaid(CPledge& pledge) {
    L2_GUARD_FUNCTION();
    if (pledge.GetWorldId() != g_nWorldId) {
        return;
    }

    std::array<char, 0x5000> packet{};
    const int length = ::Assemble(packet.data(), static_cast<int>(packet.size()), "cS", 0x5d, m_memberName.c_str());
    if (length > 0) {
        RaidPledgeMemberService_PacketRelay relay(length, packet.data());
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, relay);
    }
}

void DismissPledge::ServeInternal(User* const user) {
    // Current L2Server 0x009d8aa0.
    L2_GUARD_FUNCTION();
    CUserSocket* const socket = user->GetSocket();
    if (socket != nullptr) {
        socket->Send("chddd", 0xfe, 0x17d, 6, user->m_data->m_nPledgeID, user->m_data->m_nWorldId);
        socket->Send("c", 0x38);
    }
    user->SetPledge(nullptr);
    user->GetSocket()->SendSystemMessage_S(SystemMessageId::CLAN_DISSOLVED, m_pledgeName);
    user->m_nParamChanged |= std::int64_t{PCB_PLEDGE | PCB_USER};
    (void)runtime::raw_atomic::CompareExchangeI32(&user->m_nRelationChanged, 1, 0);
}

void DismissPledge::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x009db784.
    L2_GUARD_FUNCTION();
    if (pledge.GetWorldId() == g_nWorldId) {
        RaidPledgeMemberService_DismissPledge service;
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, service);
    }
}

void CloseAllWindowPacket::ServeInternal(User* const user) {
    // Current L2Server 0x009d85f4.
    L2_GUARD_FUNCTION();
    user->CloseAllPledgeWindowPacket();
}

void CloseAllWindowPacket::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x009dae10. The one-byte packet deliberately
    // keeps the current low-byte value 0x88.
    L2_GUARD_FUNCTION();
    if (pledge.GetWorldId() != g_nWorldId) {
        return;
    }
    std::array<char, 0x5000> packet{};
    const int length = ::Assemble(packet.data(), static_cast<int>(packet.size()), "c", 0x88);
    if (length > 0) {
        RaidPledgeMemberService_PacketRelay relay(length, packet.data());
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, relay);
    }
}

GenericService::GenericService(CPacketHelperEx<20480>* packet) noexcept : m_packet(packet) {}

void GenericService::ServeInternal(User* user) {
    L2_GUARD_FUNCTION();
    if (m_packet != nullptr) {
        user->GetSocket()->Send("b", m_packet->GetLength(), m_packet->GetPacket());
    }
}

void GenericService::ServeInternalForRaid(CPledge& pledge) {
    if (pledge.GetWorldId() != g_nWorldId || m_packet == nullptr) {
        return;
    }
    RaidPledgeMemberService_PacketRelay relay(m_packet->GetLength(), m_packet->GetPacket());
    RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, relay);
}

ViewPledgeMemberInfo::ViewPledgeMemberInfo(CPacketHelperEx<20480>* const packet) noexcept :
    m_packet(packet) {}

void ViewPledgeMemberInfo::ServeInternal(User* const user) {
    // Current L2Server 0x009d964c.
    L2_GUARD_FUNCTION();
    user->GetSocket()->Send("chb", 0xfe, 0x3f, m_packet->GetLength(), m_packet->GetPacket());
}

void ViewPledgeMemberInfo::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x009dc0fc.
    if (pledge.GetWorldId() != g_nWorldId) {
        return;
    }
    CPacketHelperEx<20480> packet;
    const int length = packet.Assemble("chb", 0xfe, 0x3f, m_packet->GetLength(), m_packet->GetPacket());
    if (length > 0) {
        RaidPledgeMemberService_PacketRelay relay(length, packet.GetPacket());
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, relay);
    }
}

UpdatePledgePower::UpdatePledgePower(const int gradeId, const std::uint32_t pledgePowerBits) noexcept :
    pledgePowerBits(pledgePowerBits), gradeId(gradeId) {}

void UpdatePledgePower::ServeInternal(User* const user) {
    // Current L2Server 0x009d9748.
    L2_GUARD_FUNCTION();
    if (user->m_data->m_nGradeId == gradeId) {
        user->GetSocket()->Send("chd", 0xfe, 0x43, pledgePowerBits);
    }
    if (!user->IsDeleting() && user->m_Deleted == 0) {
        user->ParamChanged(std::int64_t{PCB_USER});
    }
}

void UpdatePledgePower::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x009dc1d4.
    L2_GUARD_FUNCTION();
    if (pledge.GetWorldId() == g_nWorldId) {
        RaidPledgeMemberService_UpdatePledgePower service(gradeId, pledgePowerBits);
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, service);
    }
}

SubPledgeUpdated::SubPledgeUpdated(CPacketHelperEx<20480>* packet) noexcept : m_packet(packet) {}

void SubPledgeUpdated::ServeInternal(User* user) {
    L2_GUARD_FUNCTION();
    if (m_packet != nullptr) {
        user->GetSocket()->Send("chb", 0xfe, 0x41, m_packet->GetLength(), m_packet->GetPacket());
    }
}

void SubPledgeUpdated::ServeInternalForRaid(CPledge& pledge) {
    if (pledge.GetWorldId() != g_nWorldId || m_packet == nullptr) {
        return;
    }

    CPacketHelperEx<20480> packet;
    const int length = packet.Assemble("chb", 0xfe, 0x41, m_packet->GetLength(), m_packet->GetPacket());
    if (length > 0) {
        RaidPledgeMemberService_PacketRelay relay(length, packet.GetPacket());
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, relay);
    }
}

UpdatePledgeInfoWindow::UpdatePledgeInfoWindow(CPledge* pledge) :
    m_pledge(pledge),
    m_allianceId(0),
    m_alignment1cTo20(0),
    m_allianceCrestId(0),
    m_IsNotInPeace(0) {
    SetAllianceInfo();
}

void UpdatePledgeInfoWindow::SetAllianceInfo() {
    L2_GUARD_FUNCTION();
    if (m_pledge == nullptr) {
        return;
    }

    if (CAlliance* const alliance = m_pledge->GetAlliance()) {
        CSharedAllianceData* const data = alliance->m_data;
        m_allianceId = data->m_nDbId;
        m_allianceName = std::u16string(data->m_sAllianceName);
        m_allianceCrestId = data->m_nCrestId;
        for (const EnemyWarPair& war : data->m_EnemyWarPair) {
            if (war.nEnemyId != 0) {
                m_IsNotInPeace = 1;
                break;
            }
        }
    }
    if (m_pledge->IsInWar()) {
        m_IsNotInPeace = 1;
    }
}

void CombatModeChecker::ServeInternal(User* const user) {
    // Current L2Server 0x009da48c.
    L2_GUARD_FUNCTION();
    if (user->m_data->m_bIsInCombatMode) {
        ItsInCombatMode();
        return;
    }

    CPet* const pet = user->GetPet();
    if (pet != nullptr && pet->m_data->m_bIsInCombatMode) {
        ItsInCombatMode();
        return;
    }

    FOR_ALL_SUMMON_IN_SUMMON_LIST(user, summon, if (summon->m_data->m_bIsInCombatMode) { ItsInCombatMode(); });
}

void CombatModeChecker::ServeInternalForRaid(CPledge&) {
    // Current vtable slot 010eefa0 is the shared folded no-op 0x006fe5d4.
}

void UpdatePledgeInfoWindow::ServeInternal(User* user) {
    L2_GUARD_FUNCTION();
    int residenceId = 0;
    int instantResidence = 0;
    if (m_pledge->GetWorldAgitDbId() != 0) {
        residenceId = static_cast<int>(m_pledge->GetWorldAgitDbId());
    } else if (CInstantAgit* const agit =
                   CInstantAgitManager::inst().GetInstantAgit(static_cast<int>(m_pledge->GetDbId()))) {
        residenceId = agit->GetInstantZoneTypeId();
        instantResidence = 1;
    }
    const std::uint32_t emblem = m_pledge->HasResidence() ? m_pledge->GetEmblemId() : 0;
    CSharedPledgeData* const data = m_pledge->SharedData();
    user->GetSocket()->Send(
        "cdddddddddddddSdddd", 0x8e, m_pledge->Id(), m_pledge->GetWorldId(), m_pledge->GetCrestId(),
        m_pledge->GetPledgeLevel(), m_pledge->GetCastleDbId(), instantResidence, residenceId, data->m_nFortressDbId,
        m_pledge->GetRank(), m_pledge->GetNameValue(), m_pledge->GetStatus(), m_pledge->IsGuilty() ? 1 : 0, m_allianceId,
        m_allianceName.c_str(), m_allianceCrestId, m_IsNotInPeace, emblem, data->m_nDominionSiegeId
    );
    PledgeStuff::Send_S_EX_PLEDGE_SHOW_INFO_UPDATE(user, m_pledge);
    if (!user->IsDeleting() && user->m_Deleted == 0) {
        user->ParamChanged(std::int64_t{PCB_USER});
    }
}

void UpdatePledgeInfoWindow::ServeInternalForRaid(CPledge& pledge) {
    L2_GUARD_FUNCTION();
    if (pledge.GetWorldId() != g_nWorldId) {
        return;
    }

    std::array<char, 0x5000> packet{};
    int residenceId = 0;
    int instantResidence = 0;
    if (m_pledge->GetWorldAgitDbId() != 0) {
        residenceId = static_cast<int>(m_pledge->GetWorldAgitDbId());
    } else if (CInstantAgit* const agit =
                   CInstantAgitManager::inst().GetInstantAgit(static_cast<int>(m_pledge->GetDbId()))) {
        residenceId = agit->GetInstantZoneTypeId();
        instantResidence = 1;
    }
    const std::uint32_t emblem = m_pledge->HasResidence() ? m_pledge->GetEmblemId() : 0;
    CSharedPledgeData* const data = m_pledge->SharedData();
    int length = ::Assemble(
        packet.data(), static_cast<int>(packet.size()), "cdddddddddddddSdddd", 0x8e, m_pledge->Id(), m_pledge->GetWorldId(),
        m_pledge->GetCrestId(), m_pledge->GetPledgeLevel(), m_pledge->GetCastleDbId(), instantResidence, residenceId,
        data->m_nFortressDbId, m_pledge->GetRank(), m_pledge->GetNameValue(), m_pledge->GetStatus(),
        m_pledge->IsGuilty() ? 1 : 0, m_allianceId, m_allianceName.c_str(), m_allianceCrestId, m_IsNotInPeace, emblem,
        data->m_nDominionSiegeId
    );
    if (length > 0) {
        RaidPledgeMemberService_PacketRelay relay(length, packet.data());
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, relay);
    }

    length = PledgeStuff::Assemble_S_EX_PLEDGE_SHOW_INFO_UPDATE(packet.data(), static_cast<int>(packet.size()), m_pledge);
    if (length > 0) {
        RaidPledgeMemberService_PacketRelay relay(length, packet.data());
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, relay);
    }
}

void UpdatePledgeBonusPoint::ServeInternal(User* user) {
    // Current L2Server 0x009d9ac0.
    L2_GUARD_FUNCTION();
    user->GetSocket()->Send("chcd", 0xfe, 0x1b1, m_missionType, m_point);
    if (m_rewardLevel <= 0) {
        return;
    }

    int messageId = 0;
    if (m_missionType == 0) {
        messageId = SystemMessageId::CLAN_LOGIN_BONUS_LEVEL_REACHED;
    } else if (m_missionType == 1) {
        messageId = SystemMessageId::CLAN_HUNTING_BONUS_LEVEL_REACHED;
    } else {
        return;
    }
    user->GetSocket()->Send("chccQ", 0x62, messageId, 1, 1, static_cast<std::int64_t>(m_rewardLevel));
}

void UpdatePledgeBonusPoint::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x009dc554. The raid relay carries only the point
    // packet. Reward-level system messages are local-world notifications.
    L2_GUARD_FUNCTION();
    if (pledge.GetWorldId() != g_nWorldId) {
        return;
    }

    std::array<char, 0x5000> packet{};
    const int length =
        ::Assemble(packet.data(), static_cast<int>(packet.size()), "chcd", 0xfe, 0x1b1, m_missionType, m_point);
    if (length > 0) {
        RaidPledgeMemberService_PacketRelay relay(length, packet.data());
        RaidClientManager::inst().RequestRaidPledgeMemberService(pledge, relay);
    }
}

void WarReport::ServeInternal(User* const user) {
    // Current L2Server 0x009da6a8.
    L2_GUARD_FUNCTION();
    CLocalHtmlGenerator local(m_html->GetTemplet(1));
    std::u16string memberName = FormatString(u"%s", user->GetName());
    local.AddValue(u"member_name", memberName);
    m_html->AddValue(u"MEMBER_LIST", local);
}

void WarReport::ServeInternalForRaid(CPledge&) {
    // Current vtable slot 010ef000 is the shared folded no-op 0x006fe5d4.
}

Say::Say(
    User* const teller, const char16_t* const raidName, const char16_t* const message, const SayPacketType type, const bool needToRelay
) noexcept :
    m_teller(teller),
    m_sRaidName(raidName),
    m_msg(message),
    m_type(type),
    m_bNeedToRelay(needToRelay) {}

SayForRaid::SayForRaid(
    const int tellerServerId,
    const int tellerDatabaseId,
    const char16_t* const tellerName,
    const char16_t* const message,
    const SayPacketType type
) noexcept :
    m_nTellerServerID(tellerServerId),
    m_nTellerDBID(tellerDatabaseId),
    m_sTellerName(tellerName),
    m_msg(message),
    m_type(type) {}

void SayForRaid::ServeInternal(User* const user) {
    // Current L2Server 0x009d8f68.
    L2_GUARD_FUNCTION();
    user->Hear(
        m_nTellerServerID, m_nTellerDBID, m_sTellerName, m_msg, static_cast<int>(m_type)
    );
}

void SayForRaid::ServeInternalForRaid(CPledge&) {
    // Current vtable selects the shared folded no-op 0x006fe5d4.
}

SayByMobile::SayByMobile(
    const int serverId,
    const int characterDatabaseId,
    const char16_t* const characterName,
    const char16_t* const message,
    const SayPacketType type
) noexcept :
    m_serverId(serverId),
    m_characterDatabaseId(characterDatabaseId),
    m_characterName(characterName),
    m_message(message),
    m_type(type) {}

void SayByMobile::ServeInternal(User* const user) {
    // Current L2Server 0x009d9058.
    L2_GUARD_FUNCTION();
    user->HearMobile(
        m_serverId, m_characterDatabaseId, m_characterName, m_message, m_type
    );
}

void SayByMobile::ServeInternalForRaid(CPledge&) {
    // Current vtable selects the shared folded no-op.
}

void Say::ServeInternal(User* const user) {
    // Current L2Server 0x009d8e98.
    L2_GUARD_FUNCTION();
    const char16_t* chatName = m_sRaidName;
    if (m_type != SPT_PLEDGE_INRAIDSERVER && m_type != SPT_ALLIANCE_INRAIDSERVER) {
        chatName = m_teller->m_sChatName;
    }
    user->Hear(m_teller, chatName, m_msg, static_cast<int>(m_type));
}

void Say::ServeInternalForRaid(CPledge& pledge) {
    // Current L2Server 0x009dbbd4.
    L2_GUARD_FUNCTION();
    if (!m_bNeedToRelay) {
        return;
    }

    if (pledge.GetWorldId() == g_nWorldId) {
        const char16_t* chatName = m_sRaidName;
        if (m_type != SPT_PLEDGE_INRAIDSERVER && m_type != SPT_ALLIANCE_INRAIDSERVER) {
            chatName = m_teller->m_sChatName;
        }
        const int characterDatabaseId = static_cast<int>(m_teller->GetDBID());
        const int serverId = static_cast<int>(m_teller->GetServerId());
        RaidClientManager::inst().RequestPledgeChattingRelay(
            pledge, serverId, characterDatabaseId, chatName, m_msg, m_type, 0
        );
        return;
    }

    const char16_t* const senderName = m_teller->GetName();
    const int characterDatabaseId = static_cast<int>(m_teller->GetDBID());
    const int serverId = static_cast<int>(m_teller->GetServerId());
    RaidServerManager::inst().RequestPledgeChattingRelay(pledge, serverId, characterDatabaseId, senderName, m_msg, m_type);
}
