#pragma once

#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CParty;
class User;

// The current image fixes the same two-slot shape for party-member visitors.
// Work(User*) is donor-named and current-machine-code-typed.
class CPartyMemberVisitor {
public:
    virtual void Work(User* user) = 0;
    virtual ~CPartyMemberVisitor();
};

// The current image fixes a two-slot vtable: Work(unsigned) followed by the
// virtual destructor. The Work spelling is corroborated by the
// donor type stream. Its one-argument transport and slot are current-image
// facts.
class IMultiPartyVisitor {
public:
    virtual void Work(std::uint32_t partySid) = 0;
    virtual ~IMultiPartyVisitor();
};

// Exact current 0x10 adapter used to apply a party-member visitor to every
// party SID accepted by a command channel.
class CMultiPartyVisitor final : public IMultiPartyVisitor {
public:
    explicit CMultiPartyVisitor(CPartyMemberVisitor& visitor) noexcept;
    ~CMultiPartyVisitor() override;

    void Work(std::uint32_t partySid) override;

private:
    CPartyMemberVisitor& m_visitor;
};

// Current 0x20 command-chat visitors. The constructors are original inline
// header owners: copying the by-value argument into m_talker keeps it once,
// after which destruction of the argument releases the caller's temporary.
class BroadcastCommand final : public CPartyMemberVisitor {
public:
    BroadcastCommand(CSPointer<User> talker, const char16_t* message);
    ~BroadcastCommand() override;

    void Work(User* user) override;

private:
    CSPointer<User> m_talker; // +0x08
    const char16_t* m_msg; // +0x18
};

class BroadcastInterPartyMasterChat final : public IMultiPartyVisitor {
public:
    BroadcastInterPartyMasterChat(CSPointer<User> talker, const char16_t* message);
    ~BroadcastInterPartyMasterChat() override;

    void Work(std::uint32_t partySid) override;

private:
    CSPointer<User> m_talker; // +0x08
    const char16_t* m_msg; // +0x18
};

// Exact current visitor envelopes used by
// CMultiPartyCommandChannelManager. These are original named owners, not
// manager-local adapters: current RTTI gives each a two-slot vtable rooted in
// CPartyMemberVisitor/IMultiPartyVisitor and current Work bodies fix the
// fields below.
class BroadcastChannelClosed final : public CPartyMemberVisitor {
public:
    ~BroadcastChannelClosed() override = default;
    void Work(User* user) override;
};

class BroadcastChannelOpened final : public CPartyMemberVisitor {
public:
    ~BroadcastChannelOpened() override = default;
    void Work(User* user) override;
};

class BroadcastSystemMsg_S final : public CPartyMemberVisitor {
public:
    BroadcastSystemMsg_S(std::uint32_t messageId, std::int32_t messageType, const char16_t* text) noexcept;
    ~BroadcastSystemMsg_S() override = default;

    void Work(User* user) override;

private:
    std::uint32_t m_messageId;
    std::int32_t m_messageType;
    const char16_t* m_text;
};

class BroadcastSystemMsg final : public CPartyMemberVisitor {
public:
    explicit BroadcastSystemMsg(std::uint32_t messageId) noexcept;
    ~BroadcastSystemMsg() override = default;

    void Work(User* user) override;

private:
    std::uint32_t m_messageId;
};

class SendMPCCRoutingInfo final : public CPartyMemberVisitor {
public:
    explicit SendMPCCRoutingInfo(std::int32_t routing) noexcept;
    ~SendMPCCRoutingInfo() override = default;

    void Work(User* user) override;

private:
    std::int32_t m_routing;
};

class BroadcastPartyJoin final : public CPartyMemberVisitor {
public:
    explicit BroadcastPartyJoin(std::uint32_t partySid);
    ~BroadcastPartyJoin() override = default;

    void Work(User* user) override;

private:
    std::array<char16_t, 25> m_masterName;
    std::uint32_t m_masterSid;
    std::int32_t m_memberCount;
    bool m_valid;
    [[maybe_unused]] std::byte m_padding_45[3];
};

class BroadcastPartyLeave final : public CPartyMemberVisitor {
public:
    explicit BroadcastPartyLeave(std::uint32_t partySid);
    ~BroadcastPartyLeave() override = default;

    void Work(User* user) override;

private:
    std::uint32_t m_masterSid;
    std::array<char16_t, 25> m_masterName;
    bool m_valid;
    [[maybe_unused]] std::byte m_padding_3f;
};

class SetMemberMPCC final : public CPartyMemberVisitor {
public:
    explicit SetMemberMPCC(std::uint32_t channelSid) noexcept;
    ~SetMemberMPCC() override = default;

    void Work(User* user) override;

private:
    std::uint32_t m_channelSid;
};

class ChannelDismissed final : public IMultiPartyVisitor {
public:
    ~ChannelDismissed() override = default;
    void Work(std::uint32_t partySid) override;
};

class SetPartyMPCC final : public IMultiPartyVisitor {
public:
    explicit SetPartyMPCC(std::uint32_t channelSid) noexcept;
    ~SetPartyMPCC() override = default;

    void Work(std::uint32_t partySid) override;

private:
    std::uint32_t m_channelSid;
};

// Current MultiPartyVisitor.obj 0x0085772c. Identify owns the vector while
// this synchronous visitor resolves each channel party SID in order.
class GatherChannelInfo final : public IMultiPartyVisitor {
public:
    explicit GatherChannelInfo(std::vector<CParty*>& parties) noexcept;
    ~GatherChannelInfo() override = default;

    void Work(std::uint32_t partySid) override;

private:
    std::vector<CParty*>& m_parties;
};
