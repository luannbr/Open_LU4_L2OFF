#include "MultiPartyCommandChannelManager.h"
#include "MultiPartyCommandChannel.h"
#include "MultiPartyCommandChannelRegistry.h"
#include "MultiPartyVisitor.h"
#include "Party.h"
#include "PartyMatchingRequest.h"
#include "Protocol/src/SystemMessageIds.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/TargetWideChar.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>
#include <utility>

namespace {

template <std::size_t Capacity>
const wchar_t* AccountNameForMpccLog(
    const std::array<char16_t, Capacity>& source, std::array<wchar_t, Capacity + 1>& storage
) {
#if defined(_WIN32)
    static_cast<void>(storage);
    return reinterpret_cast<const wchar_t*>(source.data());
#else
    std::size_t length = 0;
    while (length < source.size() && source[length] != u'\0') {
        ++length;
    }
    const std::wstring converted = Utf16ToWide(std::u16string_view(source.data(), length));
    const std::size_t copyLength = std::min(converted.size(), storage.size() - 1U);
    std::copy_n(converted.data(), copyLength, storage.data());
    storage[copyLength] = L'\0';
    return storage.data();
#endif
}

} // namespace

CMultiPartyCommandChannelManager::CMultiPartyCommandChannelManager() =
    default; // Current L2Server 0x006ed9c4: identity no-op.

CMultiPartyCommandChannelManager::~CMultiPartyCommandChannelManager() = default; // Current L2Server 0x006fe5d4: no-op.

bool CMultiPartyCommandChannelManager::CanOpen(CSPointer<User> openUser) {
    // Current L2Server 0x00853a8c. The current body reads the
    // skill-modifier byte directly before releasing the value smart pointer.
    L2_GUARD_FUNCTION();
    return openUser && openUser->m_SkillMod.m_bCanCreateMPCC;
}

bool CMultiPartyCommandChannelManager::DoesOwnChannel(CSPointer<User> user) {
    // Current L2Server 0x00853b84.
    L2_GUARD_FUNCTION();
    if (!user->IsPartyMaster()) {
        return false;
    }
    return static_cast<bool>(CMultiPartyCommandChannelRegistry::FindByMasterParty(user->GetParty()));
}

bool CMultiPartyCommandChannelManager::DoesJoinChannel(CSPointer<User> user) {
    // Current L2Server 0x00853d30.
    L2_GUARD_FUNCTION();
    if (!user->IsPartyMaster()) {
        return false;
    }
    return static_cast<bool>(CMultiPartyCommandChannelRegistry::FindByMemberParty(user->GetParty()));
}

bool CMultiPartyCommandChannelManager::IsInChannel(CSPointer<User> user) {
    // Current L2Server 0x00854be4.
    L2_GUARD_FUNCTION();
    return DoesOwnChannel(user) || DoesJoinChannel(std::move(user));
}

bool CMultiPartyCommandChannelManager::CanCommand(CSPointer<User> user) {
    // Current L2Server 0x00853edc has no guard frame.
    return DoesOwnChannel(user);
}

void CMultiPartyCommandChannelManager::Command(CSPointer<User> commander, const char16_t* const message) {
    // Current L2Server 0x008546c8.
    L2_GUARD_FUNCTION();
    if (!CanCommand(commander)) {
        if (!commander->IsPartyMaster()) {
            commander->GetSocket()->SendSystemMessage(SystemMessageId::COMMAND_CHAT_REQUIRES_CHANNEL_OR_PARTY_LEADER);
        } else {
            commander->SendSystemMessage(SystemMessageId::ONLY_CHANNEL_CREATOR_CAN_ISSUE_GLOBAL_COMMAND);
        }
        return;
    }

    const CSPointer<CMultiPartyCommandChannel> channel =
        CMultiPartyCommandChannelRegistry::FindByMasterParty(commander->GetParty());
    if (!channel) {
        return;
    }

    if (commander) {
        std::array<SharedUtilsWideChar, 1024> filteredMessage;
        filteredMessage[0] = SharedUtilsWideChar{};
        std::memset(filteredMessage.data() + 1, 0, 0x7fe);

        const std::u16string_view targetMessageStorage = Utf16Text(message).View();
        const SharedUtilsWideChar* const targetMessage = targetMessageStorage.data();
        const int messageLength = static_cast<int>(targetMessageStorage.size());

        std::int64_t inputIndex = 0;
        std::int64_t outputIndex = 0;
        while (inputIndex < messageLength) {
            SharedUtilsWideChar value = targetMessage[inputIndex];
            if (value == static_cast<SharedUtilsWideChar>(',')) {
                value = static_cast<SharedUtilsWideChar>(' ');
                filteredMessage[outputIndex++] = value;
            } else if (value != static_cast<SharedUtilsWideChar>('\b')) {
                filteredMessage[outputIndex++] = value;
            } else {
                bool copyFormattingSegment = false;
                for (;;) {
                    ++inputIndex;
                    value = targetMessage[inputIndex];
                    if (value == static_cast<SharedUtilsWideChar>('\b') || value == SharedUtilsWideChar{}) {
                        break;
                    }
                    if (value == static_cast<SharedUtilsWideChar>(0x1b)) {
                        copyFormattingSegment = !copyFormattingSegment;
                    }
                    if (copyFormattingSegment) {
                        filteredMessage[outputIndex++] = value;
                    }
                }
            }
            ++inputIndex;
        }

        CLogFormatter formatter{};
        const std::int32_t z = runtime::Win64CvttDoubleToInt32(commander->GetPos().z);
        const std::int32_t y = runtime::Win64CvttDoubleToInt32(commander->GetPos().y);
        const std::int32_t x = runtime::Win64CvttDoubleToInt32(commander->GetPos().x);
        std::u16string commanderName = commander->GetName();
        const std::int64_t commanderDbId = static_cast<std::int64_t>(commander->GetDBID());
        // formatter.WriteChatLog(
        //     SPT_COMMANDER_CHAT,
        //     commanderDbId,
        //     0,
        //     x,
        //     y,
        //     z,
        //     hostMessage,
        //     commanderName.data(),
        //     const_cast<wchar_t*>(L""))
        // formatter.WriteChatLog(
        //     SPT_COMMANDER_CHAT,
        //     commanderDbId,
        //     0,
        //     x,
        //     y,
        //     z,
        //     hostMessage,
        //     commanderName,
        //     const_cast<wchar_t*>(L""))
        formatter.WriteChatLog(
            SPT_COMMANDER_CHAT, commanderDbId, 0, x, y, z, FixedUtf16View(filteredMessage), commanderName.data(), u""
        );
    }

    BroadcastCommand broadcast(commander, message);
    CMultiPartyVisitor visitor(broadcast);
    channel->Accept(visitor);
}

void CMultiPartyCommandChannelManager::InterPartyMasterChat(CSPointer<User> partyMaster, const char16_t* const message) {
    // Current L2Server 0x00853008.
    L2_GUARD_FUNCTION();
    if (!partyMaster->IsPartyMaster()) {
        partyMaster->GetSocket()->SendSystemMessage(SystemMessageId::COMMAND_CHAT_REQUIRES_CHANNEL_OR_PARTY_LEADER);
        return;
    }

    const CSPointer<CMultiPartyCommandChannel> channel =
        CMultiPartyCommandChannelRegistry::FindByMemberParty(partyMaster->GetParty());
    if (!channel) {
        return;
    }

    if (partyMaster) {
        std::array<SharedUtilsWideChar, 1024> filteredMessage;
        filteredMessage[0] = SharedUtilsWideChar{};
        std::memset(filteredMessage.data() + 1, 0, 0x7fe);

        const std::u16string_view targetMessageStorage = Utf16Text(message).View();
        const SharedUtilsWideChar* const targetMessage = targetMessageStorage.data();
        const int messageLength = static_cast<int>(targetMessageStorage.size());

        std::int64_t inputIndex = 0;
        std::int64_t outputIndex = 0;
        while (inputIndex < messageLength) {
            SharedUtilsWideChar value = targetMessage[inputIndex];
            if (value == static_cast<SharedUtilsWideChar>(',')) {
                value = static_cast<SharedUtilsWideChar>(' ');
                filteredMessage[outputIndex++] = value;
            } else if (value != static_cast<SharedUtilsWideChar>('\b')) {
                filteredMessage[outputIndex++] = value;
            } else {
                bool copyFormattingSegment = false;
                for (;;) {
                    ++inputIndex;
                    value = targetMessage[inputIndex];
                    if (value == static_cast<SharedUtilsWideChar>('\b') || value == SharedUtilsWideChar{}) {
                        break;
                    }
                    if (value == static_cast<SharedUtilsWideChar>(0x1b)) {
                        copyFormattingSegment = !copyFormattingSegment;
                    }
                    if (copyFormattingSegment) {
                        filteredMessage[outputIndex++] = value;
                    }
                }
            }
            ++inputIndex;
        }

        CLogFormatter formatter{};
        const std::int32_t z = runtime::Win64CvttDoubleToInt32(partyMaster->GetPos().z);
        const std::int32_t y = runtime::Win64CvttDoubleToInt32(partyMaster->GetPos().y);
        const std::int32_t x = runtime::Win64CvttDoubleToInt32(partyMaster->GetPos().x);
        std::u16string partyMasterName = partyMaster->GetName();
        const std::int64_t partyMasterDbId = static_cast<std::int64_t>(partyMaster->GetDBID());
        // formatter.WriteChatLog(
        //     SPT_INTER_PARTYMASTER_CHAT,
        //     partyMasterDbId,
        //     0,
        //     x,
        //     y,
        //     z,
        //     hostMessage,
        //     partyMasterName.data(),
        //     const_cast<wchar_t*>(L""))
        // formatter.WriteChatLog(
        //     SPT_INTER_PARTYMASTER_CHAT,
        //     partyMasterDbId,
        //     0,
        //     x,
        //     y,
        //     z,
        //     hostMessage,
        //     partyMasterName,
        //     const_cast<wchar_t*>(L""))
        formatter.WriteChatLog(
            SPT_INTER_PARTYMASTER_CHAT, partyMasterDbId, 0, x, y, z, FixedUtf16View(filteredMessage), partyMasterName.data(), u""
        );
    }

    BroadcastInterPartyMasterChat visitor(partyMaster, message);
    channel->Accept(visitor);
}

void CMultiPartyCommandChannelManager::Open(CSPointer<User> openUser, const MPCCCreateType createType) {
    // Current L2Server 0x00854cec.
    L2_GUARD_FUNCTION();
    if (IsInChannel(openUser)) {
        openUser->GetSocket()->SendSystemMessage(SystemMessageId::ALREADY_IN_OPEN_CHANNEL);
        return;
    }
    if (!CanOpen(openUser) && createType != MCT_ITEM) {
        openUser->GetSocket()->SendSystemMessage(
            SystemMessageId::COMMAND_CHANNEL_CREATION_REQUIRES_PARTY_LEADER_WHO_LEADS_LEVEL_FIVE_CLAN
        );
        return;
    }

    CParty* const party = openUser->GetParty();
    if (party == nullptr) {
        return;
    }

    auto* const channel = new CMultiPartyCommandChannel(party->m_nId);
    CMultiPartyCommandChannelRegistry::Register(party->m_nId, channel);

    const std::uint32_t channelSid = channel->Id();
    SetPartyMPCC setParty(channelSid);
    channel->Accept(setParty);

    SetMemberMPCC setMember(channelSid);
    CMultiPartyVisitor setMemberVisitor(setMember);
    channel->Accept(setMemberVisitor);

    BroadcastChannelOpened opened;
    CMultiPartyVisitor openedVisitor(opened);
    channel->Accept(openedVisitor);

    BroadcastPartyJoin joined(party->m_nId);
    CMultiPartyVisitor joinedVisitor(joined);
    channel->Accept(joinedVisitor);

    const FVector position = openUser->GetPos();
    std::array<wchar_t, 16> accountStorage{};
    // Pass each integer using the CSV 64-bit type. Retain the existing field layout.
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
    //     0x361,
    //     openUser->GetDBID(),
    //     openUser->GetAccountID(),
    //     runtime::Win64CvttDoubleToInt32(position.x),
    //     runtime::Win64CvttDoubleToInt32(position.y),
    //     runtime::Win64CvttDoubleToInt32(position.z),
    //     openUser->m_data->m_Race,
    //     openUser->m_data->m_Sex,
    //     openUser->m_data->m_nClass,
    //     openUser->m_data->m_nLevel,
    //     CMultiPartyCommandChannelRegistry::GetChannelCount(),
    //     openUser->GetName(),
    //     AccountNameForMpccLog(
    //         openUser->m_sAccountName,
    //         accountStorage))
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
    //     static_cast<std::int64_t>(0x361),
    //     static_cast<std::int64_t>(openUser->GetDBID()),
    //     static_cast<std::int64_t>(openUser->GetAccountID()),
    //     static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
    //     static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
    //     static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)),
    //     static_cast<std::int64_t>(openUser->m_data->m_Race),
    //     static_cast<std::int64_t>(openUser->m_data->m_Sex),
    //     static_cast<std::int64_t>(openUser->m_data->m_nClass),
    //     static_cast<std::int64_t>(openUser->m_data->m_nLevel),
    //     static_cast<std::int64_t>(CMultiPartyCommandChannelRegistry::GetChannelCount()),
    //     openUser->GetName(),
    //     AccountNameForMpccLog(
    //         openUser->m_sAccountName,
    //         accountStorage))
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
        static_cast<std::int64_t>(0x361), static_cast<std::int64_t>(openUser->GetDBID()),
        static_cast<std::int64_t>(openUser->GetAccountID()),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)),
        static_cast<std::int64_t>(openUser->m_data->m_Race), static_cast<std::int64_t>(openUser->m_data->m_Sex),
        static_cast<std::int64_t>(openUser->m_data->m_nClass), static_cast<std::int64_t>(openUser->m_data->m_nLevel),
        static_cast<std::int64_t>(CMultiPartyCommandChannelRegistry::GetChannelCount()), openUser->GetName(),
        AccountNameForMpccLog(openUser->m_sAccountName, accountStorage)
    );
    openUser->GetSocket()->SendSystemMessage(SystemMessageId::COMMAND_CHANNEL_CREATED);
}

void CMultiPartyCommandChannelManager::Join(CSPointer<User> channelOwner, CSPointer<User> targetUser) {
    // Current L2Server 0x008518a4.
    L2_GUARD_FUNCTION();
    CParty* const ownerParty = channelOwner->GetParty();
    CParty* const targetParty = targetUser->GetParty();
    if (!targetUser->IsPartyMaster()) {
        return;
    }

    const CSPointer<CMultiPartyCommandChannel> memberChannel =
        CMultiPartyCommandChannelRegistry::FindByMemberParty(targetParty);
    if (memberChannel) {
        return;
    }

    const CSPointer<CMultiPartyCommandChannel> channel = CMultiPartyCommandChannelRegistry::FindByMasterParty(ownerParty);
    if (!channel || channel->m_isTeleporting) {
        return;
    }
    if (!channel->AddParty(targetParty)) {
        channelOwner->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_ADD_PARTY_COMMAND_CHANNEL_FULL);
        targetUser->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_ADD_PARTY_COMMAND_CHANNEL_FULL);
        return;
    }

    CMultiPartyCommandChannelRegistry::RegisterAsMember(targetParty->m_nId, channel.get());
    const std::uint32_t channelSid = channel->Id();
    targetParty->m_data->m_nMPCCId = channelSid;

    SetMemberMPCC setMember(channelSid);
    targetParty->Accept(setMember);

    BroadcastChannelOpened opened;
    targetParty->Accept(opened);

    BroadcastSystemMsg joinedMessage(SystemMessageId::JOINED_COMMAND_CHANNEL);
    targetParty->Accept(joinedMessage);

    BroadcastPartyJoin joined(targetParty->m_nId);
    CMultiPartyVisitor joinedVisitor(joined);
    channel->Accept(joinedVisitor);

    const FVector position = targetUser->GetPos();
    std::array<wchar_t, 16> targetAccountStorage{};
    std::array<wchar_t, 16> ownerAccountStorage{};
    // Pass each integer using the CSV 64-bit type. Retain the existing field layout.
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,%s,%s,",
    //     0x362,
    //     targetUser->GetDBID(),
    //     targetUser->GetAccountID(),
    //     channelOwner->GetDBID(),
    //     channelOwner->GetAccountID(),
    //     runtime::Win64CvttDoubleToInt32(position.x),
    //     runtime::Win64CvttDoubleToInt32(position.y),
    //     runtime::Win64CvttDoubleToInt32(position.z),
    //     targetUser->m_data->m_Race,
    //     targetUser->m_data->m_Sex,
    //     targetUser->m_data->m_nClass,
    //     targetUser->m_data->m_nLevel,
    //     channel->GetPartyCount(),
    //     targetUser->GetName(),
    //     AccountNameForMpccLog(
    //         targetUser->m_sAccountName,
    //         targetAccountStorage),
    //     channelOwner->GetName(),
    //     AccountNameForMpccLog(
    //         channelOwner->m_sAccountName,
    //         ownerAccountStorage))
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,%s,%s,",
    //     static_cast<std::int64_t>(0x362),
    //     static_cast<std::int64_t>(targetUser->GetDBID()),
    //     static_cast<std::int64_t>(targetUser->GetAccountID()),
    //     static_cast<std::int64_t>(channelOwner->GetDBID()),
    //     static_cast<std::int64_t>(channelOwner->GetAccountID()),
    //     static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
    //     static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
    //     static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)),
    //     static_cast<std::int64_t>(targetUser->m_data->m_Race),
    //     static_cast<std::int64_t>(targetUser->m_data->m_Sex),
    //     static_cast<std::int64_t>(targetUser->m_data->m_nClass),
    //     static_cast<std::int64_t>(targetUser->m_data->m_nLevel),
    //     static_cast<std::int64_t>(channel->GetPartyCount()),
    //     std::wstring(targetUser->GetName()).c_str(),
    //     AccountNameForMpccLog(
    //         targetUser->m_sAccountName,
    //         targetAccountStorage),
    //     std::wstring(channelOwner->GetName()).c_str(),
    //     AccountNameForMpccLog(
    //         channelOwner->m_sAccountName,
    //         ownerAccountStorage))
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,%s,%s,",
        static_cast<std::int64_t>(0x362), static_cast<std::int64_t>(targetUser->GetDBID()),
        static_cast<std::int64_t>(targetUser->GetAccountID()), static_cast<std::int64_t>(channelOwner->GetDBID()),
        static_cast<std::int64_t>(channelOwner->GetAccountID()),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)),
        static_cast<std::int64_t>(targetUser->m_data->m_Race), static_cast<std::int64_t>(targetUser->m_data->m_Sex),
        static_cast<std::int64_t>(targetUser->m_data->m_nClass), static_cast<std::int64_t>(targetUser->m_data->m_nLevel),
        static_cast<std::int64_t>(channel->GetPartyCount()), targetUser->GetName(),
        AccountNameForMpccLog(targetUser->m_sAccountName, targetAccountStorage), channelOwner->GetName(),
        AccountNameForMpccLog(channelOwner->m_sAccountName, ownerAccountStorage)
    );
}

void CMultiPartyCommandChannelManager::Oust(CSPointer<User> channelOwner, CSPointer<User> targetUser) {
    // Current L2Server 0x008520b0.
    L2_GUARD_FUNCTION();
    if (!channelOwner->IsPartyMaster()) {
        channelOwner->SendSystemMessage(SystemMessageId::ONLY_CHANNEL_CREATOR_CAN_DISMISS_PARTY);
        return;
    }

    CParty* const ownerParty = channelOwner->GetParty();
    CParty* const targetParty = targetUser->GetParty();
    if (ownerParty == targetParty || targetParty == nullptr) {
        return;
    }

    const CSPointer<CMultiPartyCommandChannel> channel = CMultiPartyCommandChannelRegistry::FindByMasterParty(ownerParty);
    if (!channel) {
        channelOwner->SendSystemMessage(SystemMessageId::ONLY_CHANNEL_CREATOR_CAN_DISMISS_PARTY);
        return;
    }
    if (!channel->Contains(targetParty)) {
        return;
    }

    channel->RemoveParty(targetParty);
    CMultiPartyCommandChannelRegistry::UnregisterMember(targetParty->m_nId);
    targetParty->m_data->m_nMPCCId = 0;

    SetMemberMPCC clearMembers(0);
    targetParty->Accept(clearMembers);

    BroadcastChannelClosed closed;
    targetParty->Accept(closed);

    BroadcastSystemMsg ousted(SystemMessageId::EXPELLED_FROM_COMMAND_CHANNEL);
    targetParty->Accept(ousted);

    User* const targetMaster = targetParty->GetMaster();
    if (targetMaster != nullptr) {
        channelOwner->GetSocket()->SendSystemMessage_Char(
            SystemMessageId::PARTY_EXPELLED_FROM_COMMAND_CHANNEL, targetMaster->GetName()
        );
    }

    {
        BroadcastPartyLeave partyLeave(targetParty->m_nId);
        CMultiPartyVisitor partyLeaveVisitor(partyLeave);
        channel->Accept(partyLeaveVisitor);
    }

    const std::int32_t targetZ = runtime::Win64CvttDoubleToInt32(targetUser->GetPos().z);
    const std::int32_t targetY = runtime::Win64CvttDoubleToInt32(targetUser->GetPos().y);
    const std::int32_t targetX = runtime::Win64CvttDoubleToInt32(targetUser->GetPos().x);
    // Snapshot the name in the native-wide representation used by audit formatting.
    // const wchar_t* const ownerName = channelOwner->GetName()
    const std::wstring ownerNameStorage = Utf16ToWide(channelOwner->GetName());
    const wchar_t* const ownerName = ownerNameStorage.c_str();
    // Snapshot the name in the native-wide representation used by audit formatting.
    // const wchar_t* const targetNameForLog = targetUser->GetName()
    const std::wstring targetNameForLogStorage = Utf16ToWide(targetUser->GetName());
    const wchar_t* const targetNameForLog = targetNameForLogStorage.c_str();
    const int remainingPartyCount = channel->GetPartyCount();
    const std::uint32_t ownerAccountId = channelOwner->GetAccountID();
    const auto ownerDatabaseId = channelOwner->GetDBID();
    const std::uint32_t targetAccountId = targetUser->GetAccountID();
    const auto targetDatabaseId = targetUser->GetDBID();
    std::array<wchar_t, 16> targetAccountStorage{};
    std::array<wchar_t, 16> ownerAccountStorage{};
    // Pass each integer using the CSV 64-bit type. Retain the existing field layout.
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,%s,%s,",
    //     0x363,
    //     targetDatabaseId,
    //     targetAccountId,
    //     ownerDatabaseId,
    //     ownerAccountId,
    //     targetX,
    //     targetY,
    //     targetZ,
    //     targetUser->m_data->m_Race,
    //     targetUser->m_data->m_Sex,
    //     targetUser->m_data->m_nClass,
    //     targetUser->m_data->m_nLevel,
    //     remainingPartyCount,
    //     targetNameForLog,
    //     AccountNameForMpccLog(
    //         targetUser->m_sAccountName,
    //         targetAccountStorage),
    //     ownerName,
    //     AccountNameForMpccLog(
    //         channelOwner->m_sAccountName,
    //         ownerAccountStorage))
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,%s,%s,",
        static_cast<std::int64_t>(0x363), static_cast<std::int64_t>(targetDatabaseId),
        static_cast<std::int64_t>(targetAccountId), static_cast<std::int64_t>(ownerDatabaseId),
        static_cast<std::int64_t>(ownerAccountId), static_cast<std::int64_t>(targetX), static_cast<std::int64_t>(targetY),
        static_cast<std::int64_t>(targetZ), static_cast<std::int64_t>(targetUser->m_data->m_Race),
        static_cast<std::int64_t>(targetUser->m_data->m_Sex), static_cast<std::int64_t>(targetUser->m_data->m_nClass),
        static_cast<std::int64_t>(targetUser->m_data->m_nLevel), static_cast<std::int64_t>(remainingPartyCount),
        targetNameForLog, AccountNameForMpccLog(targetUser->m_sAccountName, targetAccountStorage), ownerName,
        AccountNameForMpccLog(channelOwner->m_sAccountName, ownerAccountStorage)
    );

    if (channel->IsAboutToClose()) {
        const std::int32_t ownerZ = runtime::Win64CvttDoubleToInt32(channelOwner->GetPos().z);
        const std::int32_t ownerY = runtime::Win64CvttDoubleToInt32(channelOwner->GetPos().y);
        const std::int32_t ownerX = runtime::Win64CvttDoubleToInt32(channelOwner->GetPos().x);
        const char16_t* const closeOwnerName = channelOwner->GetName();
        const std::uint32_t closeOwnerAccountId = channelOwner->GetAccountID();
        const auto closeOwnerDatabaseId = channelOwner->GetDBID();
        std::array<wchar_t, 16> closeOwnerAccountStorage{};
        // Pass each integer using the CSV 64-bit type. Retain the existing field layout.
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
        //     0x365,
        //     closeOwnerDatabaseId,
        //     closeOwnerAccountId,
        //     ownerX,
        //     ownerY,
        //     ownerZ,
        //     channelOwner->m_data->m_Race,
        //     channelOwner->m_data->m_Sex,
        //     channelOwner->m_data->m_nClass,
        //     channelOwner->m_data->m_nLevel,
        //     1,
        //     closeOwnerName,
        //     AccountNameForMpccLog(
        //         channelOwner->m_sAccountName,
        //         closeOwnerAccountStorage))
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
        //     static_cast<std::int64_t>(0x365),
        //     static_cast<std::int64_t>(closeOwnerDatabaseId),
        //     static_cast<std::int64_t>(closeOwnerAccountId),
        //     static_cast<std::int64_t>(ownerX),
        //     static_cast<std::int64_t>(ownerY),
        //     static_cast<std::int64_t>(ownerZ),
        //     static_cast<std::int64_t>(channelOwner->m_data->m_Race),
        //     static_cast<std::int64_t>(channelOwner->m_data->m_Sex),
        //     static_cast<std::int64_t>(channelOwner->m_data->m_nClass),
        //     static_cast<std::int64_t>(channelOwner->m_data->m_nLevel),
        //     static_cast<std::int64_t>(1),
        //     closeOwnerName,
        //     AccountNameForMpccLog(
        //         channelOwner->m_sAccountName,
        //         closeOwnerAccountStorage))
        Log.AddT(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
            u"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
            static_cast<std::int64_t>(0x365), static_cast<std::int64_t>(closeOwnerDatabaseId),
            static_cast<std::int64_t>(closeOwnerAccountId), static_cast<std::int64_t>(ownerX),
            static_cast<std::int64_t>(ownerY), static_cast<std::int64_t>(ownerZ),
            static_cast<std::int64_t>(channelOwner->m_data->m_Race), static_cast<std::int64_t>(channelOwner->m_data->m_Sex),
            static_cast<std::int64_t>(channelOwner->m_data->m_nClass),
            static_cast<std::int64_t>(channelOwner->m_data->m_nLevel), static_cast<std::int64_t>(1), closeOwnerName,
            AccountNameForMpccLog(channelOwner->m_sAccountName, closeOwnerAccountStorage)
        );
        CloseInternal(ownerParty);
    }
}

void CMultiPartyCommandChannelManager::Close(CSPointer<User> closeUser) {
    // Current L2Server 0x00854028. The shipping body also writes the
    // standard character-audit row before closing the channel. That logging
    // is observational and the complete state/packet route is this branch.
    L2_GUARD_FUNCTION();
    if (!closeUser || !closeUser->IsPartyMaster()) {
        return;
    }
    CloseInternal(closeUser->GetParty());
}

void CMultiPartyCommandChannelManager::Withdraw(CSPointer<User> withdrawer) {
    // Current L2Server 0x00854504.
    L2_GUARD_FUNCTION();
    if (!withdrawer) {
        return;
    }
    if (!withdrawer->IsPartyMaster()) {
        withdrawer->SendSystemMessage(SystemMessageId::ONLY_PARTY_LEADER_CAN_LEAVE_COMMAND_CHANNEL);
        return;
    }
    CPartyMatchingRequest::inst().OnLeaveWorld(withdrawer);
    WithdrawInternal(withdrawer->GetParty());
}

void CMultiPartyCommandChannelManager::Identify(CSPointer<User> observer) {
    // Current L2Server 0x0085538c.
    L2_GUARD_FUNCTION();
    if (!observer) {
        return;
    }

    const CSPointer<CMultiPartyCommandChannel> channel =
        CMultiPartyCommandChannelRegistry::FindByMemberParty(observer->GetParty());
    if (!channel) {
        return;
    }

    CParty* const masterParty = channel->GetMasterParty();
    if (masterParty == nullptr) {
        return;
    }

    std::vector<CParty*> parties;
    GatherChannelInfo gather(parties);
    channel->Accept(gather);

    std::array<char, 0x5000> packet{};
    int packetLength = 0;
    int memberCount = 0;
    for (CParty* const party : parties) {
        if (party == nullptr || party->GetMaster() == nullptr) {
            continue;
        }

        User* const masterForName = party->GetMaster();
        const int partyMemberCount = party->GetMemberCount();
        User* const masterForSid = party->GetMaster();
        const char16_t* const masterNameSource = masterForName->GetName();
        const std::u16string masterName = std::u16string(masterNameSource);
        packetLength += ::Assemble(
            packet.data() + packetLength, 0x5000 - packetLength, "Sdd", masterName.c_str(), masterForSid->m_nId,
            partyMemberCount
        );
        memberCount += partyMemberCount;
    }

    User* const channelMaster = masterParty->GetMaster();
    std::u16string masterName;
    if (channelMaster != nullptr) {
        const char16_t* const masterNameSource = masterParty->GetMaster()->GetName();
        masterName = std::u16string(masterNameSource);
    }

    CUserSocket* const socket = observer->GetSocket();
    const int partyCount = channel->GetPartyCount();
    const int routing = channel->GetRouting();
    socket->Send("chSdddb", 0xfe, 0x31, masterName.c_str(), routing, memberCount, partyCount, packetLength, packet.data());
}

void CMultiPartyCommandChannelManager::OnJoinParty(CSPointer<User> user) {
    // Current L2Server 0x008534d8.
    L2_GUARD_FUNCTION();
    if (!user) {
        return;
    }

    const CSPointer<CMultiPartyCommandChannel> channel = CMultiPartyCommandChannelRegistry::FindByMember(user.get());
    if (channel) {
        BroadcastChannelOpened visitor;
        visitor.Work(user.get());
    }

    CPartyMatchingRequest::inst().OnJoinMPCC(std::move(user));
}

void CMultiPartyCommandChannelManager::OnWithdrawParty(CSPointer<User> user) {
    // Current L2Server 0x0085365c.
    L2_GUARD_FUNCTION();
    if (!user) {
        return;
    }

    const CSPointer<CMultiPartyCommandChannel> channel = CMultiPartyCommandChannelRegistry::FindByMember(user.get());
    if (channel) {
        BroadcastChannelClosed visitor;
        visitor.Work(user.get());
    }

    CPartyMatchingRequest::inst().OnWithdrawMPCC(std::move(user));
}

void CMultiPartyCommandChannelManager::OnHandOverPartyMaster(CSPointer<User> newMaster) {
    // Current L2Server 0x00853894.
    L2_GUARD_FUNCTION();
    if (!newMaster) {
        return;
    }

    const CSPointer<CMultiPartyCommandChannel> channel =
        CMultiPartyCommandChannelRegistry::FindByMasterParty(newMaster->GetParty());
    if (!channel) {
        return;
    }

    BroadcastSystemMsg_S message(
        SystemMessageId::COMMAND_CHANNEL_LEADERSHIP_TRANSFERRED_TO_CHARACTER, 0x0c, newMaster->GetName()
    );
    CMultiPartyVisitor visitor(message);
    channel->Accept(visitor);
}

void CMultiPartyCommandChannelManager::OnDismissParty(CParty* party) {
    // Current L2Server 0x008537e0. The two full mutation endpoints are
    // recovered below as part of this same owner instead of bypassed here.
    L2_GUARD_FUNCTION();
    CloseInternal(party);
    WithdrawInternal(party);
}

void CMultiPartyCommandChannelManager::CloseInternal(CParty* closedParty) {
    // Current L2Server 0x00851588.
    L2_GUARD_FUNCTION();
    if (!closedParty) {
        return;
    }

    CSPointer<CMultiPartyCommandChannel> channel = CMultiPartyCommandChannelRegistry::FindByMasterParty(closedParty);
    if (!channel) {
        return;
    }

    CPartyMatchingRequest::inst().DismissMPCCRoom(CSPointer<User>(closedParty->GetMaster(), ObjectRefType::FunctionChain));

    SetPartyMPCC clearParties(0);
    channel->Accept(clearParties);

    SetMemberMPCC clearMembers(0);
    CMultiPartyVisitor clearMemberVisitor(clearMembers);
    channel->Accept(clearMemberVisitor);

    BroadcastChannelClosed closed;
    CMultiPartyVisitor closePacketVisitor(closed);
    channel->Accept(closePacketVisitor);

    BroadcastSystemMsg dismissed(SystemMessageId::COMMAND_CHANNEL_DISBANDED);
    CMultiPartyVisitor dismissMessageVisitor(dismissed);
    channel->Accept(dismissMessageVisitor);

    ChannelDismissed unregisterParties;
    channel->Accept(unregisterParties);
    channel->Delete();
}

void CMultiPartyCommandChannelManager::WithdrawInternal(CParty* withdrawnParty) {
    // Current L2Server 0x00852a14. Diagnostic/audit CSV emission is kept in
    // the logging owner. Every state mutation and client-visible packet from
    // the current body is kept here.
    L2_GUARD_FUNCTION();
    if (!withdrawnParty) {
        return;
    }

    CSPointer<CMultiPartyCommandChannel> channel = CMultiPartyCommandChannelRegistry::FindByMemberParty(withdrawnParty);
    if (!channel || channel->GetMasterParty() == withdrawnParty) {
        return;
    }

    channel->RemoveParty(withdrawnParty);
    CMultiPartyCommandChannelRegistry::UnregisterMember(withdrawnParty->m_nId);
    withdrawnParty->m_data->m_nMPCCId = 0;

    SetMemberMPCC clearMembers(0);
    withdrawnParty->Accept(clearMembers);

    BroadcastChannelClosed closed;
    withdrawnParty->Accept(closed);

    BroadcastSystemMsg withdrawn(SystemMessageId::LEFT_COMMAND_CHANNEL);
    withdrawnParty->Accept(withdrawn);

    BroadcastPartyLeave partyLeave(withdrawnParty->m_nId);
    CMultiPartyVisitor partyLeaveVisitor(partyLeave);
    channel->Accept(partyLeaveVisitor);

    User* const channelMaster = channel->GetMaster();
    User* const withdrawnMaster = withdrawnParty->GetMaster();
    if (channelMaster && withdrawnMaster) {
        channelMaster->GetSocket()->SendSystemMessage_S(
            SystemMessageId::PARTY_LEFT_COMMAND_CHANNEL, withdrawnMaster->GetName()
        );

        const std::int32_t withdrawnZ = runtime::Win64CvttDoubleToInt32(withdrawnMaster->GetPos().z);
        const std::int32_t withdrawnY = runtime::Win64CvttDoubleToInt32(withdrawnMaster->GetPos().y);
        const std::int32_t withdrawnX = runtime::Win64CvttDoubleToInt32(withdrawnMaster->GetPos().x);
        // Snapshot the name in the native-wide representation used by audit formatting.
        // const wchar_t* const channelMasterName =
        //     channelMaster->GetName()
        const std::wstring channelMasterNameStorage = Utf16ToWide(channelMaster->GetName());
        const wchar_t* const channelMasterName = channelMasterNameStorage.c_str();
        // Snapshot the name in the native-wide representation used by audit formatting.
        // const wchar_t* const withdrawnMasterName =
        //     withdrawnMaster->GetName()
        const std::wstring withdrawnMasterNameStorage = Utf16ToWide(withdrawnMaster->GetName());
        const wchar_t* const withdrawnMasterName = withdrawnMasterNameStorage.c_str();
        const int remainingPartyCount = channel->GetPartyCount();
        const std::uint32_t channelMasterAccountId = channelMaster->GetAccountID();
        const auto channelMasterDatabaseId = channelMaster->GetDBID();
        const std::uint32_t withdrawnMasterAccountId = withdrawnMaster->GetAccountID();
        const auto withdrawnMasterDatabaseId = withdrawnMaster->GetDBID();
        std::array<wchar_t, 16> withdrawnAccountStorage{};
        std::array<wchar_t, 16> channelAccountStorage{};
        // Pass each integer using the CSV 64-bit type. Retain the existing field layout.
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,%s,%s,",
        //     0x364,
        //     withdrawnMasterDatabaseId,
        //     withdrawnMasterAccountId,
        //     channelMasterDatabaseId,
        //     channelMasterAccountId,
        //     withdrawnX,
        //     withdrawnY,
        //     withdrawnZ,
        //     withdrawnMaster->m_data->m_Race,
        //     withdrawnMaster->m_data->m_Sex,
        //     withdrawnMaster->m_data->m_nClass,
        //     withdrawnMaster->m_data->m_nLevel,
        //     remainingPartyCount,
        //     withdrawnMasterName,
        //     AccountNameForMpccLog(
        //         withdrawnMaster->m_sAccountName,
        //         withdrawnAccountStorage),
        //     channelMasterName,
        //     AccountNameForMpccLog(
        //         channelMaster->m_sAccountName,
        //         channelAccountStorage))
        Log.AddT(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
            u"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,%s,%s,",
            static_cast<std::int64_t>(0x364), static_cast<std::int64_t>(withdrawnMasterDatabaseId),
            static_cast<std::int64_t>(withdrawnMasterAccountId), static_cast<std::int64_t>(channelMasterDatabaseId),
            static_cast<std::int64_t>(channelMasterAccountId), static_cast<std::int64_t>(withdrawnX),
            static_cast<std::int64_t>(withdrawnY), static_cast<std::int64_t>(withdrawnZ),
            static_cast<std::int64_t>(withdrawnMaster->m_data->m_Race),
            static_cast<std::int64_t>(withdrawnMaster->m_data->m_Sex),
            static_cast<std::int64_t>(withdrawnMaster->m_data->m_nClass),
            static_cast<std::int64_t>(withdrawnMaster->m_data->m_nLevel), static_cast<std::int64_t>(remainingPartyCount),
            withdrawnMasterName, AccountNameForMpccLog(withdrawnMaster->m_sAccountName, withdrawnAccountStorage),
            channelMasterName, AccountNameForMpccLog(channelMaster->m_sAccountName, channelAccountStorage)
        );
    }

    if (channel->IsAboutToClose()) {
        if (channelMaster) {
            const std::int32_t channelMasterZ = runtime::Win64CvttDoubleToInt32(channelMaster->GetPos().z);
            const std::int32_t channelMasterY = runtime::Win64CvttDoubleToInt32(channelMaster->GetPos().y);
            const std::int32_t channelMasterX = runtime::Win64CvttDoubleToInt32(channelMaster->GetPos().x);
            const char16_t* const channelMasterName = channelMaster->GetName();
            const std::uint32_t channelMasterAccountId = channelMaster->GetAccountID();
            const auto channelMasterDatabaseId = channelMaster->GetDBID();
            std::array<wchar_t, 16> channelAccountStorage{};
            // Pass each integer using the CSV 64-bit type. Retain the existing field layout.
            // Log.AddT(
            //     LOG_BOTH,
            //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
            //     L"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
            //     0x365,
            //     channelMasterDatabaseId,
            //     channelMasterAccountId,
            //     channelMasterX,
            //     channelMasterY,
            //     channelMasterZ,
            //     channelMaster->m_data->m_Race,
            //     channelMaster->m_data->m_Sex,
            //     channelMaster->m_data->m_nClass,
            //     channelMaster->m_data->m_nLevel,
            //     1,
            //     channelMasterName,
            //     AccountNameForMpccLog(
            //         channelMaster->m_sAccountName,
            //         channelAccountStorage))
            // Log.AddT(
            //     LOG_BOTH,
            //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
            //     L"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
            //     static_cast<std::int64_t>(0x365),
            //     static_cast<std::int64_t>(channelMasterDatabaseId),
            //     static_cast<std::int64_t>(channelMasterAccountId),
            //     static_cast<std::int64_t>(channelMasterX),
            //     static_cast<std::int64_t>(channelMasterY),
            //     static_cast<std::int64_t>(channelMasterZ),
            //     static_cast<std::int64_t>(channelMaster->m_data->m_Race),
            //     static_cast<std::int64_t>(channelMaster->m_data->m_Sex),
            //     static_cast<std::int64_t>(channelMaster->m_data->m_nClass),
            //     static_cast<std::int64_t>(channelMaster->m_data->m_nLevel),
            //     static_cast<std::int64_t>(1),
            //     channelMasterName,
            //     AccountNameForMpccLog(
            //         channelMaster->m_sAccountName,
            //         channelAccountStorage))
            Log.AddT(
                LOG_BOTH,
                u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                u"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
                static_cast<std::int64_t>(0x365), static_cast<std::int64_t>(channelMasterDatabaseId),
                static_cast<std::int64_t>(channelMasterAccountId), static_cast<std::int64_t>(channelMasterX),
                static_cast<std::int64_t>(channelMasterY), static_cast<std::int64_t>(channelMasterZ),
                static_cast<std::int64_t>(channelMaster->m_data->m_Race),
                static_cast<std::int64_t>(channelMaster->m_data->m_Sex),
                static_cast<std::int64_t>(channelMaster->m_data->m_nClass),
                static_cast<std::int64_t>(channelMaster->m_data->m_nLevel), static_cast<std::int64_t>(1), channelMasterName,
                AccountNameForMpccLog(channelMaster->m_sAccountName, channelAccountStorage)
            );
        }
        CloseInternal(channel->GetMasterParty());
    }
}
