#pragma once

#include "MultiPartyCommandChannel.h"

#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeObjectMap.h"

#include <cstdint>

class CParty;
class User;

// Static current-L2Server registry recovered at 008557d4..00856960. It
// keeps channels for master-party and member-party lookups.
class CMultiPartyCommandChannelRegistry final {
public:
    static void SetMaxChannel(int maximumChannelCount);
    static bool IsAvailable();
    static int GetChannelCount();

    static CSPointer<CMultiPartyCommandChannel> FindByMasterParty(CParty* party);
    static CSPointer<CMultiPartyCommandChannel> FindByMemberParty(CParty* party);
    static CSPointer<CMultiPartyCommandChannel> FindByMember(User* user);

    static bool IsChannelOwner(CParty* party);
    static bool IsChannelMember(CParty* party);
    static bool IsMPCCMaster(User* user);
    static bool IsMPCCMasterPartyMember(User* user);
    static bool IsMPCCMemberPartyMaster(User* user);
    static bool IsMPCCMemberPartyMember(User* user);
    static bool IsMPCCPartyMember(User* user);

    static void Register(std::uint32_t partySid, CMultiPartyCommandChannel* channel);
    static void RegisterAsMember(std::uint32_t partySid, CMultiPartyCommandChannel* channel);
    static void Unregister(std::uint32_t partySid);
    static void UnregisterMember(std::uint32_t partySid);

private:
    using Registry = std::map<std::uint32_t, CSPointer<CMultiPartyCommandChannel>>;

    static CSPointer<CMultiPartyCommandChannel> Find(CParty* party, Registry& registry);

    static int s_maxChannel;
    static RWLock s_lock;
    static Registry s_registry;
    static Registry s_memberRegistry;
};
