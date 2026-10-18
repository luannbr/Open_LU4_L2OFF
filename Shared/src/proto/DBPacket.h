#pragma once

// HeroType defaults use the main protocol declaration.
#include "Protocol/src/L2Constants.h"

#include "Shared/src/PledgePacket.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/proto/Stream.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <list>

namespace protocol {

// protocol/dbpacket.h:972-985 in TotalChecker2015.pdb supplies spellings only.
// Current L2Server.exe roots 0x006665ec/0x006666b0 and the enclosing
// 0x00666754/0x006668a0 virtual pair fix the
// int32/int64/int64 wire body, the 0x18 value extent and both framed fields.
struct ItemAuctionInfo {
    int itemType = 0;
    std::int64_t itemAmount = 0;
    std::int64_t initPrice = 0;

    bool Encode(network::Stream* stream) const {
        // Current Cached.exe 0x00530fb4: int32, int64, int64.
        return stream->Encode(&itemType) && stream->Encode(&itemAmount) && stream->Encode(&initPrice);
    }

    bool Decode(network::Stream* stream) {
        // Current Cached.exe 0x00531078: the inverse three-field read.
        return stream->Decode(&itemType) && stream->Decode(&itemAmount) && stream->Decode(&initPrice);
    }
};


// protocol/dbpacket.h:996-1039. Each nested body is prefixed by a signed
// int16 segment length that includes its own two bytes. Extra segment bytes
// are skipped. A body that overruns its declared segment is rejected.
class RQ_EX_UPDATE_ITEM_AUCTION_INFO_FOR_L2ASSIST final : public network::IPacket {
public:
    ~RQ_EX_UPDATE_ITEM_AUCTION_INFO_FOR_L2ASSIST() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&auctionId) && EncodeSegment(stream, currentItem) && EncodeSegment(stream, nextItem) &&
            stream->Encode(&nextAuctionDisplayStartTime);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&auctionId) && DecodeSegment(stream, currentItem) && DecodeSegment(stream, nextItem) &&
            stream->Decode(&nextAuctionDisplayStartTime);
    }

    int auctionId = 0; // +0x08
    ItemAuctionInfo currentItem; // +0x10
    ItemAuctionInfo nextItem; // +0x28
    int nextAuctionDisplayStartTime = 0; // +0x40

private:
    static bool EncodeSegment(network::Stream* stream, const ItemAuctionInfo& value) {
        unsigned char* const segmentStart = stream->GetCursor();
        std::int16_t segmentLength = 0;
        if (!stream->Encode(&segmentLength) || !value.Encode(stream)) {
            return false;
        }
        segmentLength = static_cast<std::int16_t>(stream->GetCursor() - segmentStart);
        std::memcpy(segmentStart, &segmentLength, sizeof(segmentLength));
        return true;
    }

    static bool DecodeSegment(network::Stream* stream, ItemAuctionInfo& value) {
        unsigned char* const segmentStart = stream->GetCursor();
        std::int16_t segmentLength = 0;
        if (!stream->Decode(&segmentLength) || !value.Decode(stream)) {
            return false;
        }
        const std::ptrdiff_t consumed = stream->GetCursor() - segmentStart;
        if (consumed > segmentLength) {
            return false;
        }
        const std::ptrdiff_t startOffset = segmentStart - stream->GetBuffer();
        if (startOffset < 0 || startOffset > stream->GetSize() || segmentLength > stream->GetSize() - startOffset) {
            return false;
        }
        unsigned char* const segmentEnd = segmentStart + segmentLength;
        stream->SetCursor(segmentEnd);
        return true;
    }
};


// Current protocol/dbpacket.h carriers used by extended request 0x179.
// Decoder 0x0053064c and encoder 0x00531480 fix both field offsets and the
// exact int32/int32[/bool] wire order of donor headers.
class RQ_EX_CHANGE_AP final : public network::IPacket {
public:
    ~RQ_EX_CHANGE_AP() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream && stream->Encode(&characterId) && stream->Encode(&abilityPoints);
    }

    bool Decode(network::Stream* stream) override {
        return stream && stream->Decode(&characterId) && stream->Decode(&abilityPoints);
    }

    int characterId = 0; // +0x08
    int abilityPoints = 0; // +0x0c
};


class RP_EX_CHANGE_AP final : public network::IPacket {
public:
    ~RP_EX_CHANGE_AP() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream && stream->Encode(&characterId) && stream->Encode(&abilityPoints) && stream->Encode(&saved);
    }

    bool Decode(network::Stream* stream) override {
        return stream && stream->Decode(&characterId) && stream->Decode(&abilityPoints) && stream->Decode(&saved);
    }

    int characterId = 0; // +0x08
    int abilityPoints = 0; // +0x0c
    bool saved = false; // +0x10
};


namespace DROIDNPC_COMMAND {

// TotalChecker2015.pdb keeps the original scoped owner and all four
// enumerators. Current paired L2Server.exe rejects values at
// or above DC_MAX before it sends extended request 0x195.
enum DROIDNPC_COMMAND_TYPE : int {
    DC_BASE = 0,
    DC_SPAWNNPC = 0,
    DC_CHANGENPC = 1,
    DC_MAX = 2,
};

} // namespace DROIDNPC_COMMAND

// protocol/dbpacket.h carrier kept by TotalChecker2015.pdb. Current
// Cached.exe encoder 0x0052ece8 and paired current L2Server.exe decoder
// 0x00673f0c fix the same field order and widths. char16_t
// keeps the target's two-byte wchar_t arrays on every host.
class RP_Load_DroidNPC_Appearance final : public network::IPacket {
public:
    ~RP_Load_DroidNPC_Appearance() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream &&
            stream->Encode(
                reinterpret_cast<const unsigned char*>(szCharName.data()), static_cast<int>(sizeof(szCharName))
            ) &&
            stream->Encode(
                reinterpret_cast<const unsigned char*>(szNickName.data()), static_cast<int>(sizeof(szNickName))
            ) &&
            stream->Encode(&cGender) && stream->Encode(&cRace) && stream->Encode(&nClass) && stream->Encode(&cJobGroup) &&
            stream->Encode(&cHero) && stream->Encode(&cNobless) && stream->Encode(&nAlign) && stream->Encode(&nPledgeType) &&
            stream->Encode(&nPledgeDBID) && stream->Encode(&nPledgeCrestDBID) && stream->Encode(&nPledgeEmblemDBID) &&
            stream->Encode(&nAllianceDBID) && stream->Encode(&nAllianceCrestDBID) && stream->Encode(&nFace) &&
            stream->Encode(&nHairShape) && stream->Encode(&nHairColor) && stream->Encode(&nFaceEx) &&
            stream->Encode(&nHairShapeEx) && stream->Encode(&nHairColorEx) && stream->Encode(&cHairAccFlag) &&
            stream->Encode(&nSlotUnderware) && stream->Encode(&nSlotHead) && stream->Encode(&nSlotRightHand) &&
            stream->Encode(&nSlotLeftHand) && stream->Encode(&nSlotBothHands) && stream->Encode(&nSlotGloves) &&
            stream->Encode(&nSlotChest) && stream->Encode(&nSlotLegs) && stream->Encode(&nSlotFeet) &&
            stream->Encode(&nSlotBack) && stream->Encode(&nSlotHair) && stream->Encode(&nSlotHair2) &&
            stream->Encode(&nShapeShiftRightHand) && stream->Encode(&nShapeShiftLeftHand) &&
            stream->Encode(&nShapeShiftBothHands) && stream->Encode(&nShapeShiftGloves) &&
            stream->Encode(&nShapeShiftChest) && stream->Encode(&nShapeShiftLegs) && stream->Encode(&nShapeShiftFeet) &&
            stream->Encode(&nShapeShiftHair) && stream->Encode(&nShapeShfitHair2) && stream->Encode(&nDeco1) &&
            stream->Encode(&nDeco2) && stream->Encode(&nDeco3) && stream->Encode(&nDeco4) && stream->Encode(&nDeco5) &&
            stream->Encode(&nDeco6) && stream->Encode(&nEnchantWeapon) && stream->Encode(&nEnchantChest) &&
            stream->Encode(&nEnchantLegs) && stream->Encode(&nEnchantHead) && stream->Encode(&nEnchantGloves) &&
            stream->Encode(&nEnchantFeet) && stream->Encode(&nVariationOpt1) && stream->Encode(&nVariationOpt2) &&
            stream->Encode(&nNickNameColor) && stream->Encode(&nCharNameColor) && stream->Encode(&nKarma) &&
            stream->Encode(&nAP) && stream->Encode(&nBonusCount);
    }

    bool Decode(network::Stream* stream) override {
        return stream &&
            stream->Decode(reinterpret_cast<unsigned char*>(szCharName.data()), static_cast<int>(sizeof(szCharName))) &&
            stream->Decode(reinterpret_cast<unsigned char*>(szNickName.data()), static_cast<int>(sizeof(szNickName))) &&
            stream->Decode(&cGender) && stream->Decode(&cRace) && stream->Decode(&nClass) && stream->Decode(&cJobGroup) &&
            stream->Decode(&cHero) && stream->Decode(&cNobless) && stream->Decode(&nAlign) && stream->Decode(&nPledgeType) &&
            stream->Decode(&nPledgeDBID) && stream->Decode(&nPledgeCrestDBID) && stream->Decode(&nPledgeEmblemDBID) &&
            stream->Decode(&nAllianceDBID) && stream->Decode(&nAllianceCrestDBID) && stream->Decode(&nFace) &&
            stream->Decode(&nHairShape) && stream->Decode(&nHairColor) && stream->Decode(&nFaceEx) &&
            stream->Decode(&nHairShapeEx) && stream->Decode(&nHairColorEx) && stream->Decode(&cHairAccFlag) &&
            stream->Decode(&nSlotUnderware) && stream->Decode(&nSlotHead) && stream->Decode(&nSlotRightHand) &&
            stream->Decode(&nSlotLeftHand) && stream->Decode(&nSlotBothHands) && stream->Decode(&nSlotGloves) &&
            stream->Decode(&nSlotChest) && stream->Decode(&nSlotLegs) && stream->Decode(&nSlotFeet) &&
            stream->Decode(&nSlotBack) && stream->Decode(&nSlotHair) && stream->Decode(&nSlotHair2) &&
            stream->Decode(&nShapeShiftRightHand) && stream->Decode(&nShapeShiftLeftHand) &&
            stream->Decode(&nShapeShiftBothHands) && stream->Decode(&nShapeShiftGloves) &&
            stream->Decode(&nShapeShiftChest) && stream->Decode(&nShapeShiftLegs) && stream->Decode(&nShapeShiftFeet) &&
            stream->Decode(&nShapeShiftHair) && stream->Decode(&nShapeShfitHair2) && stream->Decode(&nDeco1) &&
            stream->Decode(&nDeco2) && stream->Decode(&nDeco3) && stream->Decode(&nDeco4) && stream->Decode(&nDeco5) &&
            stream->Decode(&nDeco6) && stream->Decode(&nEnchantWeapon) && stream->Decode(&nEnchantChest) &&
            stream->Decode(&nEnchantLegs) && stream->Decode(&nEnchantHead) && stream->Decode(&nEnchantGloves) &&
            stream->Decode(&nEnchantFeet) && stream->Decode(&nVariationOpt1) && stream->Decode(&nVariationOpt2) &&
            stream->Decode(&nNickNameColor) && stream->Decode(&nCharNameColor) && stream->Decode(&nKarma) &&
            stream->Decode(&nAP) && stream->Decode(&nBonusCount);
    }

    std::array<char16_t, 24> szCharName{}; // +0x08
    std::array<char16_t, 24> szNickName{}; // +0x38
    std::int8_t cGender = 0; // +0x68
    std::int8_t cRace = 0; // +0x69
    int nClass = 0; // +0x6c
    std::int8_t cJobGroup = 0; // +0x70
    bool cHero = false; // +0x71
    bool cNobless = false; // +0x72
    int nAlign = 0; // +0x74
    int nPledgeType = 0; // +0x78
    int nPledgeDBID = 0; // +0x7c
    int nPledgeCrestDBID = 0; // +0x80
    int nPledgeEmblemDBID = 0; // +0x84
    int nAllianceDBID = 0; // +0x88
    int nAllianceCrestDBID = 0; // +0x8c
    int nFace = 0; // +0x90
    int nHairShape = 0; // +0x94
    int nHairColor = 0; // +0x98
    int nFaceEx = 0; // +0x9c
    int nHairShapeEx = 0; // +0xa0
    int nHairColorEx = 0; // +0xa4
    bool cHairAccFlag = false; // +0xa8
    int nSlotUnderware = 0; // +0xac
    int nSlotHead = 0; // +0xb0
    int nSlotRightHand = 0; // +0xb4
    int nSlotLeftHand = 0; // +0xb8
    int nSlotBothHands = 0; // +0xbc
    int nSlotGloves = 0; // +0xc0
    int nSlotChest = 0; // +0xc4
    int nSlotLegs = 0; // +0xc8
    int nSlotFeet = 0; // +0xcc
    int nSlotBack = 0; // +0xd0
    int nSlotHair = 0; // +0xd4
    int nSlotHair2 = 0; // +0xd8
    int nShapeShiftRightHand = 0; // +0xdc
    int nShapeShiftLeftHand = 0; // +0xe0
    int nShapeShiftBothHands = 0; // +0xe4
    int nShapeShiftGloves = 0; // +0xe8
    int nShapeShiftChest = 0; // +0xec
    int nShapeShiftLegs = 0; // +0xf0
    int nShapeShiftFeet = 0; // +0xf4
    int nShapeShiftHair = 0; // +0xf8
    int nShapeShfitHair2 = 0; // +0xfc
    int nDeco1 = 0; // +0x100
    int nDeco2 = 0; // +0x104
    int nDeco3 = 0; // +0x108
    int nDeco4 = 0; // +0x10c
    int nDeco5 = 0; // +0x110
    int nDeco6 = 0; // +0x114
    int nEnchantWeapon = 0; // +0x118
    int nEnchantChest = 0; // +0x11c
    int nEnchantLegs = 0; // +0x120
    int nEnchantHead = 0; // +0x124
    int nEnchantGloves = 0; // +0x128
    int nEnchantFeet = 0; // +0x12c
    int nVariationOpt1 = 0; // +0x130
    int nVariationOpt2 = 0; // +0x134
    int nNickNameColor = 0; // +0x138
    int nCharNameColor = 0; // +0x13c
    int nKarma = 0; // +0x140
    int nAP = 0; // +0x144
    int nBonusCount = 0; // +0x148
};


namespace Faction {

// protocol/dbpacket.h types kept by TotalChecker2015.pdb. The enum
// records keep both original enumerator names and values.
enum UpdateCommand : int {
    SET = 0,
    MODIFY = 1,
};

enum Reason : int {
    QUEST = 1,
    KILLING_NPC = 2,
    FISHING = 3,
    MONSTER_BOOK = 4,
    CUSTOM_SETTING = 98,
    BUILDER_CMD = 99,
};

} // namespace Faction

// protocol/dbpacket.h: current Cached.exe 0x00530548/0x005305d4 and
// TotalChecker2015.pdb fix the 0x10 memory layout. Encode
// and Decode carry only the packed int32/int64 body. Response
// lists below use the target's distinct raw 0x10-byte representation.
struct FactionPointInfo {
    int nType = 0; // +0x00
    std::int64_t nPoint = 0; // +0x08

    bool Encode(network::Stream* stream) const { return stream && stream->Encode(&nType) && stream->Encode(&nPoint); }

    bool Decode(network::Stream* stream) { return stream && stream->Decode(&nType) && stream->Decode(&nPoint); }
};


class RQ_LOAD_FACTION_POINT final : public network::IPacket {
public:
    ~RQ_LOAD_FACTION_POINT() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream && stream->Encode(&nUserServerId) && stream->Encode(&nUserDBID);
    }

    bool Decode(network::Stream* stream) override {
        return stream && stream->Decode(&nUserServerId) && stream->Decode(&nUserDBID);
    }

    int nUserServerId = 0; // +0x08
    int nUserDBID = 0; // +0x0c
};


class RP_LOAD_FACTION_POINT final : public network::IPacket {
public:
    ~RP_LOAD_FACTION_POINT() override = default;

    bool Encode(network::Stream* stream) const override {
        if (!stream || factionList.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            return false;
        }

        const int count = static_cast<int>(factionList.size());
        if (!stream->Encode(&nUserServerId) || !stream->Encode(&count)) {
            return false;
        }

        for (const FactionPointInfo& faction : factionList) {
            // Encoder 0x00543c84 copies the complete target object, including
            // its four alignment bytes. Zeroing those bytes makes the same
            // ignored wire padding deterministic on every host.
            const int padding = 0;
            if (!stream->Encode(&faction.nType) || !stream->Encode(&padding) || !stream->Encode(&faction.nPoint)) {
                return false;
            }
        }
        return true;
    }

    bool Decode(network::Stream* stream) override {
        int count = 0;
        if (!stream || !stream->Decode(&nUserServerId) || !stream->Decode(&count)) {
            return false;
        }

        factionList.clear();
        for (int index = 0; index < count; ++index) {
            FactionPointInfo faction;
            int padding = 0;
            if (!stream->Decode(&faction.nType) || !stream->Decode(&padding) || !stream->Decode(&faction.nPoint)) {
                return false;
            }
            factionList.push_back(faction);
        }
        return true;
    }

    int nUserServerId = 0; // target +0x08
    std::list<FactionPointInfo> factionList; // target +0x10
};

class RQ_UPDATE_FACTION_POINT final : public network::IPacket {
public:
    ~RQ_UPDATE_FACTION_POINT() override = default;

    // Current Cached.exe typed encoder: 0x005306c8.
    bool Encode(network::Stream* stream) const override {
        if (!stream || !stream->Encode(&cCommand) || !stream->Encode(&nReason) || !stream->Encode(&nUserServerId) ||
            !stream->Encode(&nUserDBID) || !stream->Encode(&nMaxPoint)) {
            return false;
        }
        return EncodeFactionSegment(stream, nFaction);
    }

    // Current Cached.exe typed decoder: 0x00530858.
    bool Decode(network::Stream* stream) override {
        if (!stream || !stream->Decode(&cCommand) || !stream->Decode(&nReason) || !stream->Decode(&nUserServerId) ||
            !stream->Decode(&nUserDBID) || !stream->Decode(&nMaxPoint)) {
            return false;
        }
        return DecodeFactionSegment(stream, nFaction);
    }

    std::int8_t cCommand = 0; // +0x08
    std::int16_t nReason = 0; // +0x0a
    int nUserServerId = 0; // +0x0c
    int nUserDBID = 0; // +0x10
    std::int64_t nMaxPoint = 0; // +0x18
    FactionPointInfo nFaction; // +0x20

private:
    static bool EncodeFactionSegment(network::Stream* stream, const FactionPointInfo& faction) {
        unsigned char* const segmentStart = stream->GetCursor();
        std::int16_t segmentLength = 0;
        if (!stream->Encode(&segmentLength) || !faction.Encode(stream)) {
            return false;
        }

        const std::ptrdiff_t encoded = stream->GetCursor() - segmentStart;
        if (encoded < 0 || encoded > std::numeric_limits<std::int16_t>::max()) {
            return false;
        }
        segmentLength = static_cast<std::int16_t>(encoded);
        std::memcpy(segmentStart, &segmentLength, sizeof(segmentLength));
        return true;
    }

    static bool DecodeFactionSegment(network::Stream* stream, FactionPointInfo& faction) {
        unsigned char* const segmentStart = stream->GetCursor();
        std::int16_t segmentLength = 0;
        if (!stream->Decode(&segmentLength) || segmentLength < 2) {
            return false;
        }

        const std::ptrdiff_t startOffset = segmentStart - stream->GetBuffer();
        if (startOffset < 0 || startOffset > stream->GetSize() || segmentLength > stream->GetSize() - startOffset) {
            return false;
        }

        unsigned char* const segmentEnd = segmentStart + segmentLength;
        if (!faction.Decode(stream) || stream->GetCursor() > segmentEnd) {
            return false;
        }
        stream->SetCursor(segmentEnd);
        return true;
    }
};


class RP_UPDATE_FACTION_POINT final : public network::IPacket {
public:
    ~RP_UPDATE_FACTION_POINT() override = default;

    // Current Cached.exe typed encoder: 0x005309ec.
    bool Encode(network::Stream* stream) const override {
        return stream && stream->Encode(&cCommand) && stream->Encode(&nReason) && stream->Encode(&nUserServerId) &&
            EncodeFactionSegment(stream, nFaction);
    }

    // Current Cached.exe typed decoder: 0x00530b1c.
    bool Decode(network::Stream* stream) override {
        return stream && stream->Decode(&cCommand) && stream->Decode(&nReason) && stream->Decode(&nUserServerId) &&
            DecodeFactionSegment(stream, nFaction);
    }

    std::int8_t cCommand = 0; // +0x08
    std::int16_t nReason = 0; // +0x0a
    int nUserServerId = 0; // +0x0c
    FactionPointInfo nFaction; // +0x10

private:
    static bool EncodeFactionSegment(network::Stream* stream, const FactionPointInfo& faction) {
        unsigned char* const segmentStart = stream->GetCursor();
        std::int16_t segmentLength = 0;
        if (!stream->Encode(&segmentLength) || !faction.Encode(stream)) {
            return false;
        }
        const std::ptrdiff_t encoded = stream->GetCursor() - segmentStart;
        if (encoded < 0 || encoded > std::numeric_limits<std::int16_t>::max()) {
            return false;
        }
        segmentLength = static_cast<std::int16_t>(encoded);
        std::memcpy(segmentStart, &segmentLength, sizeof(segmentLength));
        return true;
    }

    static bool DecodeFactionSegment(network::Stream* stream, FactionPointInfo& faction) {
        unsigned char* const segmentStart = stream->GetCursor();
        std::int16_t segmentLength = 0;
        if (!stream->Decode(&segmentLength) || segmentLength < 2) {
            return false;
        }
        const std::ptrdiff_t startOffset = segmentStart - stream->GetBuffer();
        if (startOffset < 0 || startOffset > stream->GetSize() || segmentLength > stream->GetSize() - startOffset) {
            return false;
        }
        unsigned char* const segmentEnd = segmentStart + segmentLength;
        if (!faction.Decode(stream) || stream->GetCursor() > segmentEnd) {
            return false;
        }
        stream->SetCursor(segmentEnd);
        return true;
    }
};


struct BalthusEventUserInfo {
    int nUserDBID = 0; // +0x00
    int nRemainTokenCount = 0; // +0x04
};


class RQ_LOAD_BALTHUS_EVENT_USER_INFO final : public network::IPacket {
public:
    ~RQ_LOAD_BALTHUS_EVENT_USER_INFO() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream && stream->Encode(&nUserSID) && stream->Encode(&nUserDBID);
    }

    bool Decode(network::Stream* stream) override {
        return stream && stream->Decode(&nUserSID) && stream->Decode(&nUserDBID);
    }

    int nUserSID = 0; // +0x08
    int nUserDBID = 0; // +0x0c
};


class RP_LOAD_BALTHUS_EVENT_USER_INFO final : public network::IPacket {
public:
    ~RP_LOAD_BALTHUS_EVENT_USER_INFO() override = default;

    // Current Cached.exe typed encoder: 0x005313f4.
    bool Encode(network::Stream* stream) const override {
        return stream && stream->Encode(&nUserSID) && stream->Encode(&nRemainToken);
    }

    bool Decode(network::Stream* stream) override {
        return stream && stream->Decode(&nUserSID) && stream->Decode(&nRemainToken);
    }

    int nUserSID = 0; // +0x08
    int nRemainToken = 0; // +0x0c
};


class RQ_UPDATE_BALTHUS_EVENT_USER_INFO final : public network::IPacket {
public:
    ~RQ_UPDATE_BALTHUS_EVENT_USER_INFO() override = default;

    bool Encode(network::Stream* stream) const override {
        if (!stream || userInfoList.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            return false;
        }
        const int count = static_cast<int>(userInfoList.size());
        if (!stream->Encode(&count)) {
            return false;
        }
        for (const BalthusEventUserInfo& userInfo : userInfoList) {
            if (!stream->Encode(&userInfo.nUserDBID) || !stream->Encode(&userInfo.nRemainTokenCount)) {
                return false;
            }
        }
        return true;
    }

    // Current Cached.exe typed list decoder: 0x00547eac.
    bool Decode(network::Stream* stream) override {
        int count = 0;
        if (!stream || !stream->Decode(&count)) {
            return false;
        }
        userInfoList.clear();
        for (int index = 0; index < count; ++index) {
            BalthusEventUserInfo userInfo;
            if (!stream->Decode(&userInfo.nUserDBID) || !stream->Decode(&userInfo.nRemainTokenCount)) {
                return false;
            }
            userInfoList.push_back(userInfo);
        }
        return true;
    }

    std::list<BalthusEventUserInfo> userInfoList; // target +0x08
};

class RQ_LOAD_USER_INZONE_POINT final : public network::IPacket {
public:
    ~RQ_LOAD_USER_INZONE_POINT() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream && stream->Encode(&nUserSID) && stream->Encode(&nUserDBID);
    }

    bool Decode(network::Stream* stream) override {
        return stream && stream->Decode(&nUserSID) && stream->Decode(&nUserDBID);
    }

    int nUserSID = 0; // +0x08
    int nUserDBID = 0; // +0x0c
};


class RQ_UPDATE_USER_INZONE_POINT final : public network::IPacket {
public:
    ~RQ_UPDATE_USER_INZONE_POINT() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream && stream->Encode(&nUserDBID) && stream->Encode(&nInzoneTypeId) && stream->Encode(&nPoint) &&
            stream->Encode(&nCurTime);
    }

    // Current Cached.exe typed decoder: 0x00531b14.
    bool Decode(network::Stream* stream) override {
        return stream && stream->Decode(&nUserDBID) && stream->Decode(&nInzoneTypeId) && stream->Decode(&nPoint) &&
            stream->Decode(&nCurTime);
    }

    int nUserDBID = 0; // +0x08
    int nInzoneTypeId = 0; // +0x0c
    int nPoint = 0; // +0x10
    int nCurTime = 0; // +0x14
};


struct UserInzonePoint {
    int nInzoneTypeId = 0; // +0x00
    int nPoint = 0; // +0x04
    int nUpdateTime = 0; // +0x08

    bool Encode(network::Stream* stream) const {
        // Current L2Server 0x00693cd0 publishes each list value as one
        // contiguous 0x0c-byte lane, with one capacity decision.
        return stream->Encode(reinterpret_cast<const unsigned char*>(this), static_cast<int>(sizeof(*this)));
    }

    bool Decode(network::Stream* stream) {
        // Current L2Server 0x0067e788 likewise consumes the full value or
        // leaves the cursor at the start of that value.
        return stream->Decode(reinterpret_cast<unsigned char*>(this), static_cast<int>(sizeof(*this)));
    }
};


struct RP_LOAD_USER_INZONE_POINT final : public network::IPacket {
public:
    ~RP_LOAD_USER_INZONE_POINT() override = default;

    bool Encode(network::Stream* stream) const override {
        const std::uint32_t lowCount = static_cast<std::uint32_t>(userInzonePointList.size());
        int count = 0;
        std::memcpy(&count, &lowCount, sizeof(count));
        if (!stream->Encode(&nUserSID) || !stream->Encode(&nUserDBID) || !stream->Encode(&count)) {
            return false;
        }
        for (const UserInzonePoint& point : userInzonePointList) {
            if (!point.Encode(stream)) {
                return false;
            }
        }
        return true;
    }

    bool Decode(network::Stream* stream) override {
        int count = 0;
        if (!stream->Decode(&nUserSID) || !stream->Decode(&nUserDBID) || !stream->Decode(&count)) {
            return false;
        }
        for (int index = 0; index < count; ++index) {
            UserInzonePoint point;
            if (!point.Decode(stream)) {
                return false;
            }
            userInzonePointList.push_back(point);
        }
        return true;
    }

    int nUserSID = 0; // +0x08
    int nUserDBID = 0; // +0x0c
    std::list<UserInzonePoint> userInzonePointList; // +0x10
};


struct PkWOlympiadUser {
    int nWorldID = 0;
    int nUserDBID = 0;
    // int nHeroType = 0
    int nHeroType = HERO_NONE;
    int nWinCount = 0;
    int nPreviousPoint = 0;
    int nOlympiadPoint = 0;
    int nMatchCount = 0;
    int nOlympiadWinCount = 0;
    int nOlympiadLoseCount = 0;
    int nOlympiadTradePoint = 0;
    int nMatchPoint = 0;

    bool Encode(network::Stream* stream) const {
        return stream->Encode(reinterpret_cast<const unsigned char*>(this), static_cast<int>(sizeof(*this)));
    }

    bool Decode(network::Stream* stream) {
        return stream->Decode(reinterpret_cast<unsigned char*>(this), static_cast<int>(sizeof(*this)));
    }
};


class RQ_EX_WOLYMPIAD_LOAD_ALL_USER final : public network::IPacket {
public:
    ~RQ_EX_WOLYMPIAD_LOAD_ALL_USER() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream && stream->Encode(&bStartUp) && stream->Encode(&bNewSeason);
    }

    bool Decode(network::Stream* stream) override {
        return stream && stream->Decode(&bStartUp) && stream->Decode(&bNewSeason);
    }

    bool bStartUp = false; // +0x08
    bool bNewSeason = false; // +0x09
};


struct RP_EX_WOLYMPIAD_LOAD_ALL_USER final : network::IPacket {
public:
    ~RP_EX_WOLYMPIAD_LOAD_ALL_USER() override = default;

    bool Encode(network::Stream* stream) const override {
        const std::uint32_t lowCount = static_cast<std::uint32_t>(vUsers.size());
        int count = 0;
        std::memcpy(&count, &lowCount, sizeof(count));
        if (!stream->Encode(&nPacketNum) || !stream->Encode(&bStartUp) || !stream->Encode(&bNewSeason) ||
            !stream->Encode(&count)) {
            return false;
        }
        for (const PkWOlympiadUser& user : vUsers) {
            if (!user.Encode(stream)) {
                return false;
            }
        }
        return true;
    }

    bool Decode(network::Stream* stream) override {
        int count = 0;
        if (!stream->Decode(&nPacketNum) || !stream->Decode(&bStartUp) || !stream->Decode(&bNewSeason) ||
            !stream->Decode(&count)) {
            return false;
        }
        for (int index = 0; index < count; ++index) {
            PkWOlympiadUser user;
            if (!user.Decode(stream)) {
                return false;
            }
            vUsers.push_back(user);
        }
        return true;
    }

    int nPacketNum = 0; // +0x08
    bool bStartUp = false; // +0x0c
    bool bNewSeason = false; // +0x0d
    std::list<PkWOlympiadUser> vUsers; // target +0x10
};


// The following World Olympiad carriers are kept unchanged by
// TotalChecker2015.pdb. Current Cached.exe fixes their codecs:
// requests 422/424/425 and replies 276/278/279.
class RQ_EX_WOLYMPIAD_GET_MATCH_COUNT final : public network::IPacket {
public:
    ~RQ_EX_WOLYMPIAD_GET_MATCH_COUNT() override = default;

    bool Encode(network::Stream* stream) const override { return stream && stream->Encode(&nReserve); }

    bool Decode(network::Stream* stream) override { return stream && stream->Decode(&nReserve); }

    int nReserve = 0; // +0x08
};


struct PkWOlympiadMatchCount {
    int nWorldID = 0;
    int nUserDBID = 0;
    int nSeason = 0;
    int nStep = 0;
    int nGameRule = 0;
    int nMatchCount = 0;

    bool Encode(network::Stream* stream) const {
        return stream->Encode(reinterpret_cast<const unsigned char*>(this), static_cast<int>(sizeof(*this)));
    }

    bool Decode(network::Stream* stream) {
        return stream->Decode(reinterpret_cast<unsigned char*>(this), static_cast<int>(sizeof(*this)));
    }
};


struct RP_EX_WOLYMPIAD_GET_MATCH_COUNT final : network::IPacket {
public:
    ~RP_EX_WOLYMPIAD_GET_MATCH_COUNT() override = default;

    bool Encode(network::Stream* stream) const override {
        const std::uint32_t lowCount = static_cast<std::uint32_t>(vMatchCounts.size());
        int count = 0;
        std::memcpy(&count, &lowCount, sizeof(count));
        if (!stream->Encode(&count)) {
            return false;
        }
        for (const PkWOlympiadMatchCount& matchCount : vMatchCounts) {
            if (!matchCount.Encode(stream)) {
                return false;
            }
        }
        return true;
    }

    bool Decode(network::Stream* stream) override {
        int count = 0;
        if (!stream->Decode(&count)) {
            return false;
        }
        for (int index = 0; index < count; ++index) {
            PkWOlympiadMatchCount matchCount;
            if (!matchCount.Decode(stream)) {
                return false;
            }
            vMatchCounts.push_back(matchCount);
        }
        return true;
    }

    std::list<PkWOlympiadMatchCount> vMatchCounts; // target +0x08
};


class RQ_EX_WOLYMPIAD_GET_MATCH_RESULT final : public network::IPacket {
public:
    ~RQ_EX_WOLYMPIAD_GET_MATCH_RESULT() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream && stream->Encode(&nWorldID) && stream->Encode(&nUserDBID) && stream->Encode(&nSeason);
    }

    bool Decode(network::Stream* stream) override {
        return stream && stream->Decode(&nWorldID) && stream->Decode(&nUserDBID) && stream->Decode(&nSeason);
    }

    int nWorldID = 0; // +0x08
    int nUserDBID = 0; // +0x0c
    int nSeason = 0; // +0x10
};


struct PkWOlympiadMatchResult {
    int nMatchTime = 0; // +0x00
    std::array<char16_t, 25> szRivalName{}; // +0x04
    int nIsWinner = 0; // +0x38
    int nElapsedTime = 0; // +0x3c
    int nRivalClassType = 0; // +0x40
    int nGameRule = 0; // +0x44

    bool Encode(network::Stream* stream) const {
        return stream->Encode(reinterpret_cast<const unsigned char*>(this), static_cast<int>(sizeof(*this)));
    }

    bool Decode(network::Stream* stream) {
        return stream->Decode(reinterpret_cast<unsigned char*>(this), static_cast<int>(sizeof(*this)));
    }
};


struct RP_EX_WOLYMPIAD_GET_MATCH_RESULT final : network::IPacket {
public:
    ~RP_EX_WOLYMPIAD_GET_MATCH_RESULT() override = default;

    bool Encode(network::Stream* stream) const override {
        const std::uint32_t lowCount = static_cast<std::uint32_t>(vResults.size());
        int count = 0;
        std::memcpy(&count, &lowCount, sizeof(count));
        if (!stream->Encode(&nWorldID) || !stream->Encode(&nUserDBID) || !stream->Encode(&count)) {
            return false;
        }
        for (const PkWOlympiadMatchResult& result : vResults) {
            if (!result.Encode(stream)) {
                return false;
            }
        }
        return true;
    }

    bool Decode(network::Stream* stream) override {
        int count = 0;
        if (!stream->Decode(&nWorldID) || !stream->Decode(&nUserDBID) || !stream->Decode(&count)) {
            return false;
        }
        for (int index = 0; index < count; ++index) {
            PkWOlympiadMatchResult result;
            if (!result.Decode(stream)) {
                return false;
            }
            vResults.push_back(result);
        }
        return true;
    }

    int nWorldID = 0; // +0x08
    int nUserDBID = 0; // +0x0c
    std::list<PkWOlympiadMatchResult> vResults; // target +0x10
};


struct PkWOlympiadUserKey {
    int nWorldID = 0;
    int nUserDBID = 0;

    bool Encode(network::Stream* stream) const { return stream->Encode(&nWorldID) && stream->Encode(&nUserDBID); }

    bool Decode(network::Stream* stream) { return stream->Decode(&nWorldID) && stream->Decode(&nUserDBID); }
};


// Current L2Server.exe vtables 0x00f5f718/0x00f706a0 bind virtual pairs
// 0x0066c2a4/0x0066c384 and 0x006946ac/0x0069478c. The target std::list
// carrier is two words at +0x10, making each target packet 0x20. Host STL
// extent is a cross-platform creation detail. Decode
// appends complete positive-count rows and never clears an existing list.
class RQ_EX_WOLYMPIAD_MATCHING_POINT_LOAD final : public network::IPacket {
public:
    ~RQ_EX_WOLYMPIAD_MATCHING_POINT_LOAD() override = default;

    bool Encode(network::Stream* stream) const override {
        const int count = static_cast<int>(vKeys.size());
        if (!stream->Encode(&cGameRule) || !stream->Encode(&count)) {
            return false;
        }
        for (const PkWOlympiadUserKey& key : vKeys) {
            if (!key.Encode(stream)) {
                return false;
            }
        }
        return true;
    }

    bool Decode(network::Stream* stream) override {
        int count = 0;
        if (!stream->Decode(&cGameRule) || !stream->Decode(&count)) {
            return false;
        }
        for (int index = 0; index < count; ++index) {
            PkWOlympiadUserKey key;
            if (!key.Decode(stream)) {
                return false;
            }
            vKeys.push_back(key);
        }
        return true;
    }

    char cGameRule = 0; // +0x08
    std::list<PkWOlympiadUserKey> vKeys; // target +0x10
};

struct PkWOlympiadMatchingPoint {
    int nWorldID = 0;
    int nUserDBID = 0;
    std::int64_t nPoint = 0;

    bool Encode(network::Stream* stream) const {
        return stream->Encode(reinterpret_cast<const unsigned char*>(this), static_cast<int>(sizeof(*this)));
    }

    bool Decode(network::Stream* stream) {
        return stream->Decode(reinterpret_cast<unsigned char*>(this), static_cast<int>(sizeof(*this)));
    }
};


struct RP_EX_WOLYMPIAD_MATCHING_POINT_LOAD final : network::IPacket {
public:
    ~RP_EX_WOLYMPIAD_MATCHING_POINT_LOAD() override = default;

    bool Encode(network::Stream* stream) const override {
        const std::uint32_t lowCount = static_cast<std::uint32_t>(vPoints.size());
        int count = 0;
        std::memcpy(&count, &lowCount, sizeof(count));
        if (!stream->Encode(&cGameRule) || !stream->Encode(&count)) {
            return false;
        }
        for (const PkWOlympiadMatchingPoint& point : vPoints) {
            if (!point.Encode(stream)) {
                return false;
            }
        }
        return true;
    }

    bool Decode(network::Stream* stream) override {
        int count = 0;
        if (!stream->Decode(&cGameRule) || !stream->Decode(&count)) {
            return false;
        }
        for (int index = 0; index < count; ++index) {
            PkWOlympiadMatchingPoint point;
            if (!point.Decode(stream)) {
                return false;
            }
            vPoints.push_back(point);
        }
        return true;
    }

    char cGameRule = 0; // +0x08
    std::list<PkWOlympiadMatchingPoint> vPoints; // target +0x10
};


// protocol/dbpacket.h carriers kept by TotalChecker2015.pdb supply names
// only. Current L2Server.exe roots 0x008e5738/0x00674cf0 and
// 0x00675034/0x006751b8 fix the offsets and wire behavior.
// The portable named byte pair represents the target compiler's natural
// alignment gap: the segmented member-info codec skips it, while the rank
// codec at 0x00694c54/0x00694d80 copies and keeps the complete 0x44 row.
class RQ_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO final : public network::IPacket {
public:
    ~RQ_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nJobKey) && stream->Encode(&nUserDBID); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nJobKey) && stream->Decode(&nUserDBID); }

    int nJobKey = 0; // +0x08
    int nUserDBID = 0; // +0x0c
};


struct PkPledgeContributionMemberInfo {
    bool Encode(network::Stream* stream) const {
        // Current Cached.exe 0x00531544 copies 4, 0x32, 4, 4 and 4 bytes.
        // the natural-alignment pair at +0x36 is skipped.
        return stream->Encode(&nRank) &&
            stream->Encode(reinterpret_cast<const unsigned char*>(szName.data()), static_cast<int>(sizeof(szName))) &&
            stream->Encode(&nPledgeType) && stream->Encode(&nPeriod) && stream->Encode(&nTotal);
    }

    bool Decode(network::Stream* stream) {
        // Current Cached.exe 0x00531664 performs the inverse five reads and
        // likewise leaves the +0x36 padding pair untouched.
        return stream->Decode(&nRank) &&
            stream->Decode(reinterpret_cast<unsigned char*>(szName.data()), static_cast<int>(sizeof(szName))) &&
            stream->Decode(&nPledgeType) && stream->Decode(&nPeriod) && stream->Decode(&nTotal);
    }

    int nRank = 0; // +0x00
    std::array<char16_t, 25> szName{}; // +0x04
    std::array<unsigned char, 2> targetPadding{}; // +0x36
    int nPledgeType = 0; // +0x38
    int nPeriod = 0; // +0x3c
    int nTotal = 0; // +0x40
};


class RP_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO final : public network::IPacket {
public:
    ~RP_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO() override = default;

    bool Encode(network::Stream* stream) const override {
        if (!stream->Encode(&nJobKey) || !stream->Encode(&bSuccess) || !stream->Encode(&nPledgeDBID) ||
            !stream->Encode(&nRewardedStep) || !stream->Encode(&nPeriodReward)) {
            return false;
        }
        return EncodeInfoSegment(stream);
    }

    bool Decode(network::Stream* stream) override {
        if (!stream->Decode(&nJobKey) || !stream->Decode(&bSuccess) || !stream->Decode(&nPledgeDBID) ||
            !stream->Decode(&nRewardedStep) || !stream->Decode(&nPeriodReward)) {
            return false;
        }
        return DecodeInfoSegment(stream);
    }

    int nJobKey = 0; // +0x08
    bool bSuccess = false; // +0x0c
    int nPledgeDBID = 0; // +0x10
    int nRewardedStep = 0; // +0x14
    int nPeriodReward = 0; // +0x18
    PkPledgeContributionMemberInfo Info; // +0x1c

private:
    bool EncodeInfoSegment(network::Stream* stream) const {
        unsigned char* const start = stream->GetCursor();
        std::int16_t length = 0;
        if (!stream->Encode(&length) || !Info.Encode(stream)) {
            return false;
        }
        length = static_cast<std::int16_t>(stream->GetCursor() - start);
        std::memcpy(start, &length, sizeof(length));
        return true;
    }

    bool DecodeInfoSegment(network::Stream* stream) {
        unsigned char* const start = stream->GetCursor();
        std::int16_t length = 0;
        if (!stream->Decode(&length) || !Info.Decode(stream)) {
            return false;
        }
        const std::ptrdiff_t consumed = stream->GetCursor() - start;
        if (consumed > length) {
            return false;
        }
        const std::ptrdiff_t startOffset = start - stream->GetBuffer();
        if (startOffset < 0 || startOffset > stream->GetSize() || length > stream->GetSize() - startOffset) {
            return false;
        }
        unsigned char* const end = start + length;
        stream->SetCursor(end);
        return true;
    }
};


// The current L2Server request pair 0x009949ec/0x00674d6c and response pair
// 0x00694c54/0x00694d80 fix these fields and wire widths.
// Shared/xlist.h creates the same source container as 0x10 in CacheD and
// the current three-word 0x18 carrier in L2Server. Rank Decode appends and
// never clears the existing list.
class RQ_EX_PLEDGE_CONTRIBUTION_RANK final : public network::IPacket {
public:
    ~RQ_EX_PLEDGE_CONTRIBUTION_RANK() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&nJobKey) && stream->Encode(&nPledgeDBID) && stream->Encode(&bCurrent);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&nJobKey) && stream->Decode(&nPledgeDBID) && stream->Decode(&bCurrent);
    }

    int nJobKey = 0; // +0x08
    int nPledgeDBID = 0; // +0x0c
    bool bCurrent = false; // +0x10
};


class RP_EX_PLEDGE_CONTRIBUTION_RANK final : public network::IPacket {
public:
    ~RP_EX_PLEDGE_CONTRIBUTION_RANK() override = default;

    bool Encode(network::Stream* stream) const override {
        const int count = static_cast<int>(vMembers.size());
        if (!stream->Encode(&nJobKey) || !stream->Encode(&nPledgeDBID) || !stream->Encode(&count)) {
            return false;
        }
        for (const PkPledgeContributionMemberInfo& member : vMembers) {
            if (!stream->Encode(reinterpret_cast<const unsigned char*>(&member), static_cast<int>(sizeof(member)))) {
                return false;
            }
        }
        return true;
    }

    bool Decode(network::Stream* stream) override {
        int count = 0;
        if (!stream->Decode(&nJobKey) || !stream->Decode(&nPledgeDBID) || !stream->Decode(&count)) {
            return false;
        }
        for (int index = 0; index < count; ++index) {
            PkPledgeContributionMemberInfo member{};
            if (!stream->Decode(reinterpret_cast<unsigned char*>(&member), static_cast<int>(sizeof(member)))) {
                return false;
            }
            vMembers.push_back(member);
        }
        return true;
    }

    int nJobKey = 0; // +0x08
    int nPledgeDBID = 0; // +0x0c
    std::list<PkPledgeContributionMemberInfo> vMembers; // +0x10
};


class RQ_EX_PLEDGE_ITEM_LOAD final : public network::IPacket {
public:
    ~RQ_EX_PLEDGE_ITEM_LOAD() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream && stream->Encode(&nJobKey) && stream->Encode(&nPledgeDBID);
    }

    bool Decode(network::Stream* stream) override {
        return stream && stream->Decode(&nJobKey) && stream->Decode(&nPledgeDBID);
    }

    int nJobKey = 0; // +0x08
    int nPledgeDBID = 0; // +0x0c
};


class RP_EX_PLEDGE_ITEM_LOAD final : public network::IPacket {
public:
    ~RP_EX_PLEDGE_ITEM_LOAD() override = default;

    bool Encode(network::Stream* stream) const override {
        if (!stream || vItems.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            return false;
        }
        const int count = static_cast<int>(vItems.size());
        if (!stream->Encode(&nJobKey) || !stream->Encode(&nPledgeDBID) || !stream->Encode(&count)) {
            return false;
        }
        for (const PkPledgeItem& item : vItems) {
            if (!stream->Encode(reinterpret_cast<const unsigned char*>(&item), static_cast<int>(sizeof(item)))) {
                return false;
            }
        }
        return true;
    }

    bool Decode(network::Stream* stream) override {
        int count = 0;
        if (!stream || !stream->Decode(&nJobKey) || !stream->Decode(&nPledgeDBID) || !stream->Decode(&count)) {
            return false;
        }
        vItems.clear();
        for (int index = 0; index < count; ++index) {
            PkPledgeItem item{};
            if (!stream->Decode(reinterpret_cast<unsigned char*>(&item), static_cast<int>(sizeof(item)))) {
                return false;
            }
            vItems.push_back(item);
        }
        return true;
    }

    int nJobKey = 0; // +0x08
    int nPledgeDBID = 0; // +0x0c
    std::list<PkPledgeItem> vItems; // +0x10
};


class RQ_EX_PLEDGE_ITEM_SAVE final : public network::IPacket {
public:
    ~RQ_EX_PLEDGE_ITEM_SAVE() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream && stream->Encode(&nJobKey) && stream->Encode(&nPledgeDBID) && stream->Encode(&nItemClassID) &&
            stream->Encode(&nExpiryTime);
    }

    bool Decode(network::Stream* stream) override {
        return stream && stream->Decode(&nJobKey) && stream->Decode(&nPledgeDBID) && stream->Decode(&nItemClassID) &&
            stream->Decode(&nExpiryTime);
    }

    int nJobKey = 0; // +0x08
    int nPledgeDBID = 0; // +0x0c
    int nItemClassID = 0; // +0x10
    int nExpiryTime = 0; // +0x14
};


class RP_EX_PLEDGE_ITEM_SAVE final : public network::IPacket {
public:
    ~RP_EX_PLEDGE_ITEM_SAVE() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream && stream->Encode(&nJobKey) && stream->Encode(&bSuccess) && stream->Encode(&nPledgeDBID);
    }

    bool Decode(network::Stream* stream) override {
        return stream && stream->Decode(&nJobKey) && stream->Decode(&bSuccess) && stream->Decode(&nPledgeDBID);
    }

    int nJobKey = 0; // +0x08
    bool bSuccess = false; // +0x0c
    int nPledgeDBID = 0; // +0x10
};


class RQ_EX_PLEDGE_ITEM_DELETE final : public network::IPacket {
public:
    ~RQ_EX_PLEDGE_ITEM_DELETE() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream && stream->Encode(&nJobKey) && stream->Encode(&nPledgeDBID) && stream->Encode(&nItemClassID);
    }

    bool Decode(network::Stream* stream) override {
        return stream && stream->Decode(&nJobKey) && stream->Decode(&nPledgeDBID) && stream->Decode(&nItemClassID);
    }

    int nJobKey = 0; // +0x08
    int nPledgeDBID = 0; // +0x0c
    int nItemClassID = 0; // +0x10
};


class RP_EX_PLEDGE_ITEM_DELETE final : public network::IPacket {
public:
    ~RP_EX_PLEDGE_ITEM_DELETE() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream && stream->Encode(&nJobKey) && stream->Encode(&bSuccess);
    }

    bool Decode(network::Stream* stream) override { return stream && stream->Decode(&nJobKey) && stream->Decode(&bSuccess); }

    int nJobKey = 0; // +0x08
    bool bSuccess = false; // +0x0c
};


struct PkPledgeRaidInfoData {
    int nWorldId_ = 0; // +0x00
    int nPledgeDbId_ = 0; // +0x04
    std::array<char16_t, 25> szPledgeName_{}; // +0x08
    int nRank_ = 0; // +0x3c
    std::int64_t nPoint_ = 0; // +0x40
};


class RQ_EX_PLEDGE_RAID_GET_INFO_LIST final : public network::IPacket {
public:
    ~RQ_EX_PLEDGE_RAID_GET_INFO_LIST() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream && stream->Encode(&nJobKey) && stream->Encode(&nSeason_);
    }

    bool Decode(network::Stream* stream) override { return stream && stream->Decode(&nJobKey) && stream->Decode(&nSeason_); }

    int nJobKey = 0; // +0x08
    int nSeason_ = 0; // +0x0c
};


class RP_EX_PLEDGE_RAID_GET_INFO_LIST final : public network::IPacket {
public:
    ~RP_EX_PLEDGE_RAID_GET_INFO_LIST() override = default;

    bool Encode(network::Stream* stream) const override {
        if (!stream || vList_.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            return false;
        }
        const int count = static_cast<int>(vList_.size());
        if (!stream->Encode(&nJobKey) || !stream->Encode(&nTotalCount_) || !stream->Encode(&count)) {
            return false;
        }
        for (const PkPledgeRaidInfoData& row : vList_) {
            std::array<unsigned char, 0x48> wire{};
            std::memcpy(wire.data() + 0x00, &row.nWorldId_, 4);
            std::memcpy(wire.data() + 0x04, &row.nPledgeDbId_, 4);
            std::memcpy(wire.data() + 0x08, row.szPledgeName_.data(), sizeof(row.szPledgeName_));
            std::memcpy(wire.data() + 0x3c, &row.nRank_, 4);
            std::memcpy(wire.data() + 0x40, &row.nPoint_, 8);
            if (!stream->Encode(wire.data(), static_cast<int>(wire.size()))) {
                return false;
            }
        }
        return true;
    }

    bool Decode(network::Stream* stream) override {
        int count = 0;
        if (!stream || !stream->Decode(&nJobKey) || !stream->Decode(&nTotalCount_) || !stream->Decode(&count)) {
            return false;
        }
        vList_.clear();
        for (int index = 0; index < count; ++index) {
            std::array<unsigned char, 0x48> wire{};
            if (!stream->Decode(wire.data(), static_cast<int>(wire.size()))) {
                return false;
            }
            PkPledgeRaidInfoData row{};
            std::memcpy(&row.nWorldId_, wire.data() + 0x00, 4);
            std::memcpy(&row.nPledgeDbId_, wire.data() + 0x04, 4);
            std::memcpy(row.szPledgeName_.data(), wire.data() + 0x08, sizeof(row.szPledgeName_));
            std::memcpy(&row.nRank_, wire.data() + 0x3c, 4);
            std::memcpy(&row.nPoint_, wire.data() + 0x40, 8);
            vList_.push_back(row);
        }
        return true;
    }

    int nJobKey = 0; // +0x08
    int nTotalCount_ = 0; // +0x0c
    std::list<PkPledgeRaidInfoData> vList_; // +0x10
};


struct PkUpdateRewardPledgeData {
    int nPledgeDbId_ = 0;
    int nRewardReason_ = 0;
};


class RQ_EX_PLEDGE_RAID_INFO_UPDATE_REWARD_REASON final : public network::IPacket {
public:
    ~RQ_EX_PLEDGE_RAID_INFO_UPDATE_REWARD_REASON() override = default;

    bool Encode(network::Stream* stream) const override {
        if (!stream || vUpdateRewardPledgeList_.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            return false;
        }
        const int count = static_cast<int>(vUpdateRewardPledgeList_.size());
        if (!stream->Encode(&nJobKey) || !stream->Encode(&nSeason_) || !stream->Encode(&count)) {
            return false;
        }
        for (const PkUpdateRewardPledgeData& row : vUpdateRewardPledgeList_) {
            if (!stream->Encode(reinterpret_cast<const unsigned char*>(&row), static_cast<int>(sizeof(row)))) {
                return false;
            }
        }
        return true;
    }

    bool Decode(network::Stream* stream) override {
        int count = 0;
        if (!stream || !stream->Decode(&nJobKey) || !stream->Decode(&nSeason_) || !stream->Decode(&count)) {
            return false;
        }
        vUpdateRewardPledgeList_.clear();
        for (int index = 0; index < count; ++index) {
            PkUpdateRewardPledgeData row{};
            if (!stream->Decode(reinterpret_cast<unsigned char*>(&row), static_cast<int>(sizeof(row)))) {
                return false;
            }
            vUpdateRewardPledgeList_.push_back(row);
        }
        return true;
    }

    int nJobKey = 0; // +0x08
    int nSeason_ = 0; // +0x0c
    std::list<PkUpdateRewardPledgeData> vUpdateRewardPledgeList_; // +0x10
};


// Current L2Server vtable 0x00f64cf0 binds the single-int32 virtual pair
// 0x008ed89c/0x00994af8. ProcessAtomicJobPacket at 0x006967e0 creates
// the complete 0x10 object and registration 0x127 calls that response.
class RP_EX_PLEDGE_RAID_INFO_UPDATE_REWARD_REASON final : public network::IPacket {
public:
    ~RP_EX_PLEDGE_RAID_INFO_UPDATE_REWARD_REASON() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nJobKey); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nJobKey); }

    int nJobKey = 0; // +0x08
};


struct PkPledgeRaidInzoneData {
    int nPledgeDbId_ = 0;
    int nRestrictTime_ = 0;
};


class RQ_EX_PLEDGE_RAID_INZONE_LOAD final : public network::IPacket {
public:
    ~RQ_EX_PLEDGE_RAID_INZONE_LOAD() override = default;

    bool Encode(network::Stream* stream) const override { return stream && stream->Encode(&nJobKey); }

    bool Decode(network::Stream* stream) override { return stream && stream->Decode(&nJobKey); }

    int nJobKey = 0; // +0x08
};


class RP_EX_PLEDGE_RAID_INZONE_LOAD final : public network::IPacket {
public:
    ~RP_EX_PLEDGE_RAID_INZONE_LOAD() override = default;

    bool Encode(network::Stream* stream) const override {
        if (!stream || vList_.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            return false;
        }
        const int count = static_cast<int>(vList_.size());
        if (!stream->Encode(&nJobKey) || !stream->Encode(&count)) {
            return false;
        }
        for (const PkPledgeRaidInzoneData& row : vList_) {
            if (!stream->Encode(reinterpret_cast<const unsigned char*>(&row), static_cast<int>(sizeof(row)))) {
                return false;
            }
        }
        return true;
    }

    bool Decode(network::Stream* stream) override {
        int count = 0;
        if (!stream || !stream->Decode(&nJobKey) || !stream->Decode(&count)) {
            return false;
        }
        vList_.clear();
        for (int index = 0; index < count; ++index) {
            PkPledgeRaidInzoneData row{};
            if (!stream->Decode(reinterpret_cast<unsigned char*>(&row), static_cast<int>(sizeof(row)))) {
                return false;
            }
            vList_.push_back(row);
        }
        return true;
    }

    int nJobKey = 0; // +0x08
    std::list<PkPledgeRaidInzoneData> vList_; // +0x10
};


struct PkPledgeMissionLoadData {
    int nMissionId_ = 0;
    int nCount_ = 0;
    int nRewardTime_ = 0;
};


class RQ_EX_PLEDGE_MISSION_LOAD final : public network::IPacket {
public:
    ~RQ_EX_PLEDGE_MISSION_LOAD() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream && stream->Encode(&nJobKey) && stream->Encode(&nUserDbId_);
    }

    bool Decode(network::Stream* stream) override {
        return stream && stream->Decode(&nJobKey) && stream->Decode(&nUserDbId_);
    }

    int nJobKey = 0; // +0x08
    int nUserDbId_ = 0; // +0x0c
};


class RP_EX_PLEDGE_MISSION_LOAD final : public network::IPacket {
public:
    ~RP_EX_PLEDGE_MISSION_LOAD() override = default;

    bool Encode(network::Stream* stream) const override {
        if (!stream || vList_.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            return false;
        }
        const int count = static_cast<int>(vList_.size());
        if (!stream->Encode(&nJobKey) || !stream->Encode(&count)) {
            return false;
        }
        for (const PkPledgeMissionLoadData& row : vList_) {
            if (!stream->Encode(reinterpret_cast<const unsigned char*>(&row), static_cast<int>(sizeof(row)))) {
                return false;
            }
        }
        return true;
    }

    bool Decode(network::Stream* stream) override {
        int count = 0;
        if (!stream || !stream->Decode(&nJobKey) || !stream->Decode(&count)) {
            return false;
        }
        vList_.clear();
        for (int index = 0; index < count; ++index) {
            PkPledgeMissionLoadData row{};
            if (!stream->Decode(reinterpret_cast<unsigned char*>(&row), static_cast<int>(sizeof(row)))) {
                return false;
            }
            vList_.push_back(row);
        }
        return true;
    }

    int nJobKey = 0; // +0x08
    std::list<PkPledgeMissionLoadData> vList_; // +0x10
};


struct PkPledgeMissionAddCountData {
    int nMissionId_ = 0;
    int nCount_ = 0;
};


class RQ_EX_PLEDGE_MISSION_ADD_COUNT final : public network::IPacket {
public:
    ~RQ_EX_PLEDGE_MISSION_ADD_COUNT() override = default;

    bool Encode(network::Stream* stream) const override {
        if (!stream || vList_.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            return false;
        }
        const int count = static_cast<int>(vList_.size());
        if (!stream->Encode(&nUserDbId_) || !stream->Encode(&count)) {
            return false;
        }
        for (const PkPledgeMissionAddCountData& row : vList_) {
            if (!stream->Encode(reinterpret_cast<const unsigned char*>(&row), static_cast<int>(sizeof(row)))) {
                return false;
            }
        }
        return true;
    }

    bool Decode(network::Stream* stream) override {
        int count = 0;
        if (!stream || !stream->Decode(&nUserDbId_) || !stream->Decode(&count)) {
            return false;
        }
        vList_.clear();
        for (int index = 0; index < count; ++index) {
            PkPledgeMissionAddCountData row{};
            if (!stream->Decode(reinterpret_cast<unsigned char*>(&row), static_cast<int>(sizeof(row)))) {
                return false;
            }
            vList_.push_back(row);
        }
        return true;
    }

    int nUserDbId_ = 0; // +0x08
    std::list<PkPledgeMissionAddCountData> vList_; // +0x10
};


class RQ_EX_PLEDGE_ANNOUNCE final : public network::IPacket {
public:
    ~RQ_EX_PLEDGE_ANNOUNCE() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream && stream->Encode(&nJobKey) && stream->Encode(&nPledgeDbId_);
    }

    bool Decode(network::Stream* stream) override {
        return stream && stream->Decode(&nJobKey) && stream->Decode(&nPledgeDbId_);
    }

    int nJobKey = 0; // +0x08
    int nPledgeDbId_ = 0; // +0x0c
};


class RP_EX_PLEDGE_ANNOUNCE final : public network::IPacket {
public:
    // Current folded destructor 0x00543518 is shared with the same-layout
    // SET reply: release xwstring storage, restore network::IPacket vtable.
    ~RP_EX_PLEDGE_ANNOUNCE() override = default;

    bool Encode(network::Stream* stream) const override {
        if (!stream || content.size() > static_cast<std::size_t>(std::numeric_limits<std::int16_t>::max())) {
            return false;
        }
        const std::int16_t characterCount = static_cast<std::int16_t>(content.size());
        return stream->Encode(&nJobKey) && stream->Encode(&characterCount) &&
            (characterCount == 0 ||
                stream->Encode(
                    reinterpret_cast<const unsigned char*>(content.data()), static_cast<int>(characterCount) * 2
                )) &&
            stream->Encode(&bShow);
    }

    bool Decode(network::Stream* stream) override {
        std::int16_t characterCount = 0;
        if (!stream || !stream->Decode(&nJobKey) || !stream->Decode(&characterCount) || characterCount < 0) {
            return false;
        }
        std::u16string decoded(static_cast<std::size_t>(characterCount), u'\0');
        if (characterCount != 0 &&
            !stream->Decode(reinterpret_cast<unsigned char*>(decoded.data()), static_cast<int>(characterCount) * 2)) {
            return false;
        }
        content.assign(decoded);
        return stream->Decode(&bShow);
    }

    int nJobKey = 0; // +0x08
    std::u16string content; // +0x10
    bool bShow = false; // +0x38
};


class RQ_EX_PLEDGE_ANNOUNCE_SET final : public network::IPacket {
public:
    ~RQ_EX_PLEDGE_ANNOUNCE_SET() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream && stream->Encode(&nJobKey) && stream->Encode(&nPledgeDBID) && stream->Encode(&content) &&
            stream->Encode(&bShow);
    }

    bool Decode(network::Stream* stream) override {
        return stream && stream->Decode(&nJobKey) && stream->Decode(&nPledgeDBID) && stream->Decode(&content) &&
            stream->Decode(&bShow);
    }

    int nJobKey = 0; // +0x08
    int nPledgeDBID = 0; // +0x0c
    std::u16string content; // +0x10
    bool bShow = false; // +0x38
};


class RP_EX_PLEDGE_ANNOUNCE_SET final : public network::IPacket {
public:
    // Same current folded destructor body 0x00543518.
    ~RP_EX_PLEDGE_ANNOUNCE_SET() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream && stream->Encode(&nJobKey) && stream->Encode(&bSuccess) && stream->Encode(&content) &&
            stream->Encode(&bShow);
    }

    bool Decode(network::Stream* stream) override {
        return stream && stream->Decode(&nJobKey) && stream->Decode(&bSuccess) && stream->Decode(&content) &&
            stream->Decode(&bShow);
    }

    int nJobKey = 0; // +0x08
    bool bSuccess = false; // +0x0c
    std::u16string content; // +0x10
    bool bShow = false; // +0x38
};


class RQ_EX_BUILDER_COMMAND final : public network::IPacket {
public:
    ~RQ_EX_BUILDER_COMMAND() override = default;

    bool Encode(network::Stream* stream) const override {
        // Current Cached.exe folded encoder 0x0058e1f0 is shared with the RP
        // carrier and keeps list iteration order.
        if (!stream || params.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            return false;
        }
        const int count = static_cast<int>(params.size());
        if (!stream->Encode(&nUserSid) || !stream->Encode(&count)) {
            return false;
        }
        for (const std::u16string& param : params) {
            if (!stream->Encode(&param)) {
                return false;
            }
        }
        return true;
    }

    bool Decode(network::Stream* stream) override {
        // Current folded decoder 0x0058f09c appends each decoded string to the
        // existing list. It contains no pre-read clear.
        int count = 0;
        if (!stream || !stream->Decode(&nUserSid) || !stream->Decode(&count)) {
            return false;
        }
        for (int index = 0; index < count; ++index) {
            std::u16string param;
            if (!stream->Decode(&param)) {
                return false;
            }
            params.push_back(param);
        }
        return true;
    }

    int nUserSid = 0; // +0x08
    std::list<std::u16string> params; // +0x10
};


class RP_EX_BUILDER_COMMAND final : public network::IPacket {
public:
    ~RP_EX_BUILDER_COMMAND() override = default;

    bool Encode(network::Stream* stream) const override {
        // Folded with the RQ carrier at current Cached.exe 0x0058e1f0.
        if (!stream || params.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            return false;
        }
        const int count = static_cast<int>(params.size());
        if (!stream->Encode(&nUserSid) || !stream->Encode(&count)) {
            return false;
        }
        for (const std::u16string& param : params) {
            if (!stream->Encode(&param)) {
                return false;
            }
        }
        return true;
    }

    bool Decode(network::Stream* stream) override {
        // Folded with the RQ carrier at current Cached.exe 0x0058f09c. The
        // target appends and does not clear an already populated list.
        int count = 0;
        if (!stream || !stream->Decode(&nUserSid) || !stream->Decode(&count)) {
            return false;
        }
        for (int index = 0; index < count; ++index) {
            std::u16string param;
            if (!stream->Decode(&param)) {
                return false;
            }
            params.push_back(param);
        }
        return true;
    }

    int nUserSid = 0; // +0x08
    std::list<std::u16string> params; // +0x10
};


class RQ_EX_LOAD_RESERVATION_ANNOUNCE final : public network::IPacket {
public:
    ~RQ_EX_LOAD_RESERVATION_ANNOUNCE() override = default;

    bool Encode(network::Stream* stream) const override {
        // Current L2Server 0x009ce340 copies the byte at +0x08 literally.
        // it does not perform a bool lvalue-to-rvalue conversion or
        // normalize the representation. The raw overload keeps that
        // object-representation copy for the unwritten lane.
        return stream &&
            stream->Encode(reinterpret_cast<const unsigned char*>(&isBuilder), static_cast<int>(sizeof(isBuilder)));
    }

    bool Decode(network::Stream* stream) override {
        return stream && stream->Decode(reinterpret_cast<unsigned char*>(&isBuilder), static_cast<int>(sizeof(isBuilder)));
    }

    // Current CDB::LoadReservationAnnounce@0x006675ac installs only the vptr
    // before Encode. Do not add a default initializer to this byte.
    bool isBuilder; // +0x08
};


struct PkReservationAnncounceData {
    bool Encode(network::Stream* stream) const {
        // Current Cached.exe 0x00594d48 fixes both dwords, the explicit
        // message count/list and the four-dword-plus-byte tail.
        if (!stream || Msgs.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            return false;
        }
        const int messageCount = static_cast<int>(Msgs.size());
        if (!stream->Encode(&announceId) || !stream->Encode(&lineNumber) || !stream->Encode(&messageCount)) {
            return false;
        }
        for (const std::u16string& message : Msgs) {
            if (!stream->Encode(&message)) {
                return false;
            }
        }
        return stream->Encode(&StartDate) && stream->Encode(&endDate) && stream->Encode(&period) &&
            stream->Encode(&location) && stream->Encode(&isBuilder);
    }

    bool Decode(network::Stream* stream) {
        // Current Cached.exe 0x00594fe4 appends decoded messages. It does not
        // clear Msgs before processing the count.
        int messageCount = 0;
        if (!stream || !stream->Decode(&announceId) || !stream->Decode(&lineNumber) || !stream->Decode(&messageCount)) {
            return false;
        }
        for (int index = 0; index < messageCount; ++index) {
            std::u16string message;
            if (!stream->Decode(&message)) {
                return false;
            }
            Msgs.push_back(message);
        }
        return stream->Decode(&StartDate) && stream->Decode(&endDate) && stream->Decode(&period) &&
            stream->Decode(&location) && stream->Decode(&isBuilder);
    }

    int announceId = 0; // +0x00
    int lineNumber = 0; // +0x04
    std::list<std::u16string> Msgs; // +0x08
    int StartDate = 0; // +0x18
    int endDate = 0; // +0x1c
    int period = 0; // +0x20
    int location = 0; // +0x24
    bool isBuilder = false; // +0x28
};


class RP_EX_LOAD_RESERVATION_ANNOUNCE final : public network::IPacket {
public:
    ~RP_EX_LOAD_RESERVATION_ANNOUNCE() override = default;

    bool Encode(network::Stream* stream) const override {
        // Current Cached.exe 0x00595678.
        if (!stream || data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            return false;
        }
        const int count = static_cast<int>(data.size());
        if (!stream->Encode(&count)) {
            return false;
        }
        for (const PkReservationAnncounceData& row : data) {
            if (!row.Encode(stream)) {
                return false;
            }
        }
        return true;
    }

    bool Decode(network::Stream* stream) override {
        // Current Cached.exe 0x005957a8 appends rows and performs no clear.
        int count = 0;
        if (!stream || !stream->Decode(&count)) {
            return false;
        }
        for (int index = 0; index < count; ++index) {
            PkReservationAnncounceData row;
            if (!row.Decode(stream)) {
                return false;
            }
            data.push_back(row);
        }
        return true;
    }

    std::list<PkReservationAnncounceData> data; // +0x08
};


class RQ_EX_DELETE_RESERVATION_ANNOUNCE final : public network::IPacket {
public:
    ~RQ_EX_DELETE_RESERVATION_ANNOUNCE() override = default;

    bool Encode(network::Stream* stream) const override { return stream && stream->Encode(&announceNum); }

    bool Decode(network::Stream* stream) override { return stream && stream->Decode(&announceNum); }

    int announceNum = 0; // +0x08
};


class RQ_EX_SET_RESERVATION_ANNOUNCE final : public network::IPacket {
public:
    ~RQ_EX_SET_RESERVATION_ANNOUNCE() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream && stream->Encode(&announceId) && stream->Encode(&lineNumber) && stream->Encode(&Msgs) &&
            stream->Encode(&StartDate) && stream->Encode(&endDate) && stream->Encode(&period) && stream->Encode(&location);
    }

    bool Decode(network::Stream* stream) override {
        return stream && stream->Decode(&announceId) && stream->Decode(&lineNumber) && stream->Decode(&Msgs) &&
            stream->Decode(&StartDate) && stream->Decode(&endDate) && stream->Decode(&period) && stream->Decode(&location);
    }

    int announceId = 0; // +0x08
    int lineNumber = 0; // +0x0c
    std::u16string Msgs; // +0x10
    int StartDate = 0; // +0x38
    int endDate = 0; // +0x3c
    int period = 0; // +0x40
    int location = 0; // +0x44
};


class RQ_EX_SHOW_RESERVATION_ANNOUNCE final : public network::IPacket {
public:
    ~RQ_EX_SHOW_RESERVATION_ANNOUNCE() override = default;

    bool Encode(network::Stream* stream) const override { return stream && stream->Encode(&nUserServerId); }

    bool Decode(network::Stream* stream) override { return stream && stream->Decode(&nUserServerId); }

    int nUserServerId = 0; // +0x08
};


class RP_EX_SHOW_RESERVATION_ANNOUNCE final : public network::IPacket {
public:
    ~RP_EX_SHOW_RESERVATION_ANNOUNCE() override = default;

    bool Encode(network::Stream* stream) const override {
        // Current Cached.exe 0x0059528c.
        if (!stream || data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            return false;
        }
        const int count = static_cast<int>(data.size());
        if (!stream->Encode(&nUserServerId) || !stream->Encode(&count)) {
            return false;
        }
        for (const PkReservationAnncounceData& row : data) {
            if (!row.Encode(stream)) {
                return false;
            }
        }
        return true;
    }

    bool Decode(network::Stream* stream) override {
        // Current Cached.exe 0x005953f0 appends rows and performs no clear.
        int count = 0;
        if (!stream || !stream->Decode(&nUserServerId) || !stream->Decode(&count)) {
            return false;
        }
        for (int index = 0; index < count; ++index) {
            PkReservationAnncounceData row;
            if (!row.Decode(stream)) {
                return false;
            }
            data.push_back(row);
        }
        return true;
    }

    int nUserServerId = 0; // +0x08
    std::list<PkReservationAnncounceData> data; // +0x10
};


class RP_EX_REFRASH_RESERVATION_ANNOUNCE final : public network::IPacket {
public:
    ~RP_EX_REFRASH_RESERVATION_ANNOUNCE() override = default;

    bool Encode(network::Stream* stream) const override { return stream && stream->Encode(&temp); }

    bool Decode(network::Stream* stream) override { return stream && stream->Decode(&temp); }

    int temp = 0; // +0x08
};


} // namespace protocol
