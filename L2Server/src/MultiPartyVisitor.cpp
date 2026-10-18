#include "MultiPartyVisitor.h"
#include "Shared/src/TextRuntime.h"

#include "MultiPartyCommandChannelRegistry.h"
#include "Party.h"
#include "User.h"
#include "UserSocket.h"



namespace {

template <std::size_t Size>
void CopyMasterName(std::array<char16_t, Size>& destination, const char16_t* source) {
    std::size_t output = 0;
    if (source != nullptr) {
        while (source[output] != u'\0' && output + 1 < destination.size()) {
            destination[output] = source[output];
            ++output;
        }
    }
    destination[output] = u'\0';
}

} // namespace

CPartyMemberVisitor::~CPartyMemberVisitor() = default;
IMultiPartyVisitor::~IMultiPartyVisitor() = default;

CMultiPartyVisitor::CMultiPartyVisitor(CPartyMemberVisitor& visitor) noexcept : m_visitor(visitor) {}

CMultiPartyVisitor::~CMultiPartyVisitor() = default;

void CMultiPartyVisitor::Work(std::uint32_t partySid) {
    L2_GUARD_FUNCTION();
    if (CParty* const party = PartySmartIdManager()->GetObjectW(partySid)) {
        party->Accept(m_visitor);
    }
}

BroadcastCommand::BroadcastCommand(CSPointer<User> talker, const char16_t* const message) : m_talker(talker), m_msg(message) {}

BroadcastCommand::~BroadcastCommand() = default;

void BroadcastCommand::Work(User* user) {
    // Current L2Server 0x0085715c.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        User* const talker = m_talker.get();
        const char16_t* const talkerName = m_talker->GetName();
        user->Hear(talker, talkerName, m_msg, SPT_COMMANDER_CHAT);
    }
}

BroadcastInterPartyMasterChat::BroadcastInterPartyMasterChat(CSPointer<User> talker, const char16_t* const message) :
    m_talker(talker), m_msg(message) {}

BroadcastInterPartyMasterChat::~BroadcastInterPartyMasterChat() = default;

void BroadcastInterPartyMasterChat::Work(const std::uint32_t partySid) {
    // Current L2Server 0x0085731c.
    L2_GUARD_FUNCTION();
    CParty* const party = PartySmartIdManager()->GetObjectW(partySid);
    if (party == nullptr) {
        return;
    }

    User* const partyMaster = party->GetMaster();
    if (partyMaster != nullptr) {
        User* const talker = m_talker.get();
        const char16_t* const talkerName = m_talker->GetName();
        partyMaster->Hear(talker, talkerName, m_msg, SPT_INTER_PARTYMASTER_CHAT);
    }
}

void BroadcastChannelClosed::Work(User* user) {
    // Current L2Server 0x00856ae8.
    L2_GUARD_FUNCTION();
    if (user) {
        user->GetSocket()->Send("ch", 0xfe, 0x13);
    }
}

void BroadcastChannelOpened::Work(User* user) {
    // Current L2Server 0x00856a1c.
    L2_GUARD_FUNCTION();
    if (user) {
        user->GetSocket()->Send("ch", 0xfe, 0x12);
    }
}

BroadcastSystemMsg_S::BroadcastSystemMsg_S(
    const std::uint32_t messageId, const std::int32_t messageType, const char16_t* const text
) noexcept :
    m_messageId(messageId), m_messageType(messageType), m_text(text) {}

void BroadcastSystemMsg_S::Work(User* user) {
    // Current L2Server 0x00856c6c. The only current construction route uses
    // EST_CHAR_NAME (0x0c). Its text remains UTF-16 through the socket owner.
    L2_GUARD_FUNCTION();
    if (user && m_messageType == 0x0c) {
        user->GetSocket()->SendSystemMessage_Char(m_messageId, m_text);
    }
}

BroadcastSystemMsg::BroadcastSystemMsg(const std::uint32_t messageId) noexcept : m_messageId(messageId) {}

void BroadcastSystemMsg::Work(User* user) {
    // Current L2Server 0x00856bb4.
    L2_GUARD_FUNCTION();
    if (user) {
        user->GetSocket()->SendSystemMessage(m_messageId);
    }
}

SendMPCCRoutingInfo::SendMPCCRoutingInfo(const std::int32_t routing) noexcept : m_routing(routing) {}

void SendMPCCRoutingInfo::Work(User* user) {
    // Current L2Server 0x00857088.
    L2_GUARD_FUNCTION();
    if (user) {
        user->GetSocket()->Send("chd", 0xfe, 0x37, m_routing);
    }
}

BroadcastPartyJoin::BroadcastPartyJoin(const std::uint32_t partySid) :
    m_masterName{}, m_masterSid(0), m_memberCount(0), m_valid(false), m_padding_45{} {
    // Current L2Server 0x00857440.
    L2_GUARD_FUNCTION();
    CParty* const party = PartySmartIdManager()->GetObjectW(partySid);
    User* const master = party ? party->GetMaster() : nullptr;
    if (!master) {
        return;
    }

    const char16_t* const source = master->GetName();
    CopyMasterName(m_masterName, source);
    m_masterSid = master->m_nId;
    m_memberCount = party->GetMemberCount();
    m_valid = true;
}

void BroadcastPartyJoin::Work(User* user) {
    // Current L2Server 0x00856d54.
    L2_GUARD_FUNCTION();
    if (user && m_valid) {
        user->GetSocket()->Send("chSddd", 0xfe, 0x5c, m_masterName.data(), m_masterSid, m_memberCount, 1);
    }
}

BroadcastPartyLeave::BroadcastPartyLeave(const std::uint32_t partySid) :
    m_masterSid(0), m_masterName{}, m_valid(false), m_padding_3f{} {
    // Current L2Server 0x00857560.
    L2_GUARD_FUNCTION();
    CParty* const party = PartySmartIdManager()->GetObjectW(partySid);
    User* const master = party ? party->GetMaster() : nullptr;
    if (!master) {
        return;
    }

    m_masterSid = master->m_nId;
    const char16_t* const source = master->GetName();
    CopyMasterName(m_masterName, source);
    m_valid = true;
}

void BroadcastPartyLeave::Work(User* user) {
    // Current L2Server 0x00856e48.
    L2_GUARD_FUNCTION();
    if (user && m_valid) {
        user->GetSocket()->Send("chSddd", 0xfe, 0x5c, m_masterName.data(), m_masterSid, 0, 0);
    }
}

SetMemberMPCC::SetMemberMPCC(const std::uint32_t channelSid) noexcept : m_channelSid(channelSid) {}

void SetMemberMPCC::Work(User* user) {
    // Current L2Server 0x00856fd8.
    L2_GUARD_FUNCTION();
    if (user) {
        user->m_data->m_nMPCCId = m_channelSid;
    }
}

void ChannelDismissed::Work(const std::uint32_t partySid) {
    // Current L2Server 0x00856f38.
    L2_GUARD_FUNCTION();
    CMultiPartyCommandChannelRegistry::Unregister(partySid);
}

SetPartyMPCC::SetPartyMPCC(const std::uint32_t channelSid) noexcept : m_channelSid(channelSid) {}

void SetPartyMPCC::Work(const std::uint32_t partySid) {
    // Current L2Server 0x00857670.
    L2_GUARD_FUNCTION();
    if (CParty* const party = PartySmartIdManager()->GetObjectW(partySid)) {
        party->m_data->m_nMPCCId = m_channelSid;
    }
}

GatherChannelInfo::GatherChannelInfo(std::vector<CParty*>& parties) noexcept : m_parties(parties) {}

void GatherChannelInfo::Work(const std::uint32_t partySid) {
    // Current L2Server 0x0085772c.
    L2_GUARD_FUNCTION();
    CParty* const party = PartySmartIdManager()->GetObjectW(partySid);
    if (party != nullptr) {
        m_parties.push_back(party);
    }
}
