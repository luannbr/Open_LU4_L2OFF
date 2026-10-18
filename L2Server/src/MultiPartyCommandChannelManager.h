#pragma once

#include "Shared/src/TemplateHelper.h"

#include <cstdint>

class CParty;
class User;
template <typename TObject>
class CSPointer;
enum MPCCCreateType : std::int32_t;

// Current MultiPartyCommandChannelManager.obj is a stateless command owner.
// Its construction/destruction therefore carries no target storage, while
// the methods operate on the owned channel registry.
class CMultiPartyCommandChannelManager {
public:
    CMultiPartyCommandChannelManager();
    ~CMultiPartyCommandChannelManager();

    static bool CanOpen(CSPointer<User> openUser);

    void Open(CSPointer<User> openUser, MPCCCreateType createType);
    void Join(CSPointer<User> channelOwner, CSPointer<User> targetUser);
    void Oust(CSPointer<User> channelOwner, CSPointer<User> targetUser);
    void Command(CSPointer<User> commander, const char16_t* message);
    void InterPartyMasterChat(CSPointer<User> partyMaster, const char16_t* message);

    void OnJoinParty(CSPointer<User> user);
    void OnWithdrawParty(CSPointer<User> user);
    void OnDismissParty(CParty* party);
    void OnHandOverPartyMaster(CSPointer<User> newMaster);

    // Current L2Server 0x00854028/0x00854504. These are the public
    // user-facing endpoints used by the party-duel constructor. The
    // mutation work remains in the two exact internal owners below.
    void Close(CSPointer<User> closeUser);
    void Withdraw(CSPointer<User> withdrawer);
    void Identify(CSPointer<User> observer);

private:
    bool DoesOwnChannel(CSPointer<User> user);
    bool DoesJoinChannel(CSPointer<User> user);
    bool IsInChannel(CSPointer<User> user);
    bool CanCommand(CSPointer<User> user);
    void CloseInternal(CParty* closedParty);
    void WithdrawInternal(CParty* withdrawnParty);
};
