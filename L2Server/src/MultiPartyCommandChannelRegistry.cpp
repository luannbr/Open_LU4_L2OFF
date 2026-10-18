#include "MultiPartyCommandChannelRegistry.h"

#include "Party.h"
#include "User.h"


#include <cstddef>

int CMultiPartyCommandChannelRegistry::s_maxChannel = 300;
RWLock CMultiPartyCommandChannelRegistry::s_lock;
CMultiPartyCommandChannelRegistry::Registry CMultiPartyCommandChannelRegistry::s_registry;
CMultiPartyCommandChannelRegistry::Registry CMultiPartyCommandChannelRegistry::s_memberRegistry;


void CMultiPartyCommandChannelRegistry::SetMaxChannel(int maximumChannelCount) {
    L2_GUARD_FUNCTION();
    if (static_cast<std::uint32_t>(maximumChannelCount) - 1U < 300U) {
        s_maxChannel = maximumChannelCount;
    }
}

bool CMultiPartyCommandChannelRegistry::IsAvailable() {
    L2_GUARD_FUNCTION();
    return s_registry.size() < static_cast<std::size_t>(s_maxChannel);
}

int CMultiPartyCommandChannelRegistry::GetChannelCount() {
    L2_GUARD_FUNCTION();
    return static_cast<int>(s_registry.size());
}

CSPointer<CMultiPartyCommandChannel> CMultiPartyCommandChannelRegistry::Find(CParty* party, Registry& registry) {
    L2_GUARD_FUNCTION();

    CSPointer<CMultiPartyCommandChannel> result;
    if (!party) {
        return result;
    }

    L2_READ_GUARD(s_lock);
    const auto found = registry.find(party->GetSmartID().Sid());
    if (found != registry.end()) {
        result = found->second;
    }
    return result;
}

CSPointer<CMultiPartyCommandChannel> CMultiPartyCommandChannelRegistry::FindByMasterParty(CParty* party) {
    L2_GUARD_FUNCTION();
    return Find(party, s_registry);
}

CSPointer<CMultiPartyCommandChannel> CMultiPartyCommandChannelRegistry::FindByMemberParty(CParty* party) {
    L2_GUARD_FUNCTION();
    return Find(party, s_memberRegistry);
}

CSPointer<CMultiPartyCommandChannel> CMultiPartyCommandChannelRegistry::FindByMember(User* user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return {};
    }
    CParty* const party = user->GetParty();
    if (!party) {
        return {};
    }
    CSPointer<CMultiPartyCommandChannel> channel = FindByMasterParty(party);
    if (channel) {
        return channel;
    }
    return FindByMemberParty(party);
}

bool CMultiPartyCommandChannelRegistry::IsChannelOwner(CParty* party) {
    L2_GUARD_FUNCTION();
    return static_cast<bool>(FindByMasterParty(party));
}

bool CMultiPartyCommandChannelRegistry::IsChannelMember(CParty* party) {
    L2_GUARD_FUNCTION();
    return static_cast<bool>(FindByMemberParty(party));
}

bool CMultiPartyCommandChannelRegistry::IsMPCCMaster(User* user) {
    L2_GUARD_FUNCTION();
    return user && user->GetParty() && IsChannelOwner(user->GetParty()) && user->IsPartyMaster();
}

bool CMultiPartyCommandChannelRegistry::IsMPCCMasterPartyMember(User* user) {
    L2_GUARD_FUNCTION();
    return user && user->GetParty() && IsChannelOwner(user->GetParty()) && !user->IsPartyMaster();
}

bool CMultiPartyCommandChannelRegistry::IsMPCCMemberPartyMaster(User* user) {
    L2_GUARD_FUNCTION();
    return user && user->GetParty() && IsChannelMember(user->GetParty()) && user->IsPartyMaster();
}

bool CMultiPartyCommandChannelRegistry::IsMPCCMemberPartyMember(User* user) {
    L2_GUARD_FUNCTION();
    return user && user->GetParty() && IsChannelMember(user->GetParty()) && !user->IsPartyMaster();
}

bool CMultiPartyCommandChannelRegistry::IsMPCCPartyMember(User* user) {
    L2_GUARD_FUNCTION();
    return IsMPCCMasterPartyMember(user) || IsMPCCMemberPartyMember(user);
}

void CMultiPartyCommandChannelRegistry::Register(std::uint32_t partySid, CMultiPartyCommandChannel* channel) {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(s_lock);
    s_registry[partySid] = CSPointer<CMultiPartyCommandChannel>(channel, ObjectRefType::FunctionChain);
    s_memberRegistry[partySid] = CSPointer<CMultiPartyCommandChannel>(channel, ObjectRefType::FunctionChain);
}

void CMultiPartyCommandChannelRegistry::RegisterAsMember(std::uint32_t partySid, CMultiPartyCommandChannel* channel) {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(s_lock);
    s_memberRegistry[partySid] = CSPointer<CMultiPartyCommandChannel>(channel, ObjectRefType::FunctionChain);
}

void CMultiPartyCommandChannelRegistry::Unregister(std::uint32_t partySid) {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(s_lock);
    s_registry.erase(partySid);
    s_memberRegistry.erase(partySid);
}

void CMultiPartyCommandChannelRegistry::UnregisterMember(std::uint32_t partySid) {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(s_lock);
    s_memberRegistry.erase(partySid);
}
