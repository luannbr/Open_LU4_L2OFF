#include "L2Packet.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace protocol {
namespace {

bool IsSet(const ::CVariableBitSet& bitSet, std::uint32_t bit) {
    // The shipping IPacket::Encode and CVariableBitSet::IsSet methods are
    // both non-const. The portable IPacket surface already exposes Encode as
    // const, so keep the target call without changing the shared ABI.
    return const_cast<::CVariableBitSet&>(bitSet).IsSet(bit);
}

bool SkipExtensionBytes(network::Stream* stream, int byteCount) {
    if (byteCount <= 0) {
        return true;
    }

    unsigned char* const buffer = stream->GetBuffer();
    unsigned char* const cursor = stream->GetCursor();
    const int bufferSize = stream->GetSize();
    if (!buffer || !cursor || bufferSize < 0 || cursor < buffer) {
        return false;
    }

    const std::ptrdiff_t used = cursor - buffer;
    if (used > bufferSize || byteCount > bufferSize - used) {
        return false;
    }
    stream->SetCursor(cursor + byteCount);
    return true;
}

std::array<const S_UserInfo_EquipSlot*, 60> EquipSlots(const S_UserInfo_EquipInfo& packet) {
    return {
        &packet.equipUnderwear,
        &packet.equipRear,
        &packet.equipLear,
        &packet.equipNeck,
        &packet.equipRFinger,
        &packet.equipLFinger,
        &packet.equipHead,
        &packet.equipRHand,
        &packet.equipLHand,
        &packet.equipGlobes,
        &packet.equipChest,
        &packet.equipLegs,
        &packet.equipFeet,
        &packet.equipBack,
        &packet.equipRLHand,
        &packet.equipHair,
        &packet.equipHair2,
        &packet.equipRBracelet,
        &packet.equipLBracelet,
        &packet.equipAgathionMain,
        &packet.equipAgathionSub1,
        &packet.equipAgathionSub2,
        &packet.equipAgathionSub3,
        &packet.equipAgathionSub4,
        &packet.equipDeco1,
        &packet.equipDeco2,
        &packet.equipDeco3,
        &packet.equipDeco4,
        &packet.equipDeco5,
        &packet.equipDeco6,
        &packet.equipWaist,
        &packet.equipBrooch,
        &packet.equipJewel1,
        &packet.equipJewel2,
        &packet.equipJewel3,
        &packet.equipJewel4,
        &packet.equipJewel5,
        &packet.equipJewel6,
        &packet.equipArtifactbook,
        &packet.equipArtifact_a1,
        &packet.equipArtifact_a2,
        &packet.equipArtifact_a3,
        &packet.equipArtifact_a4,
        &packet.equipArtifact_a5,
        &packet.equipArtifact_a6,
        &packet.equipArtifact_a7,
        &packet.equipArtifact_a8,
        &packet.equipArtifact_a9,
        &packet.equipArtifact_a10,
        &packet.equipArtifact_a11,
        &packet.equipArtifact_a12,
        &packet.equipArtifact_b1,
        &packet.equipArtifact_b2,
        &packet.equipArtifact_b3,
        &packet.equipArtifact_c1,
        &packet.equipArtifact_c2,
        &packet.equipArtifact_c3,
        &packet.equipArtifact_d1,
        &packet.equipArtifact_d2,
        &packet.equipArtifact_d3,
    };
}

std::array<S_UserInfo_EquipSlot*, 60> EquipSlots(S_UserInfo_EquipInfo& packet) {
    return {
        &packet.equipUnderwear,
        &packet.equipRear,
        &packet.equipLear,
        &packet.equipNeck,
        &packet.equipRFinger,
        &packet.equipLFinger,
        &packet.equipHead,
        &packet.equipRHand,
        &packet.equipLHand,
        &packet.equipGlobes,
        &packet.equipChest,
        &packet.equipLegs,
        &packet.equipFeet,
        &packet.equipBack,
        &packet.equipRLHand,
        &packet.equipHair,
        &packet.equipHair2,
        &packet.equipRBracelet,
        &packet.equipLBracelet,
        &packet.equipAgathionMain,
        &packet.equipAgathionSub1,
        &packet.equipAgathionSub2,
        &packet.equipAgathionSub3,
        &packet.equipAgathionSub4,
        &packet.equipDeco1,
        &packet.equipDeco2,
        &packet.equipDeco3,
        &packet.equipDeco4,
        &packet.equipDeco5,
        &packet.equipDeco6,
        &packet.equipWaist,
        &packet.equipBrooch,
        &packet.equipJewel1,
        &packet.equipJewel2,
        &packet.equipJewel3,
        &packet.equipJewel4,
        &packet.equipJewel5,
        &packet.equipJewel6,
        &packet.equipArtifactbook,
        &packet.equipArtifact_a1,
        &packet.equipArtifact_a2,
        &packet.equipArtifact_a3,
        &packet.equipArtifact_a4,
        &packet.equipArtifact_a5,
        &packet.equipArtifact_a6,
        &packet.equipArtifact_a7,
        &packet.equipArtifact_a8,
        &packet.equipArtifact_a9,
        &packet.equipArtifact_a10,
        &packet.equipArtifact_a11,
        &packet.equipArtifact_a12,
        &packet.equipArtifact_b1,
        &packet.equipArtifact_b2,
        &packet.equipArtifact_b3,
        &packet.equipArtifact_c1,
        &packet.equipArtifact_c2,
        &packet.equipArtifact_c3,
        &packet.equipArtifact_d1,
        &packet.equipArtifact_d2,
        &packet.equipArtifact_d3,
    };
}

template <class Payload>
bool EncodeFrame(const Payload& payload, network::Stream* stream) {
    unsigned char* const frameStart = stream->GetCursor();
    const std::int16_t placeholder = 0;
    if (!stream->Encode(&placeholder) || !payload.Encode(stream)) {
        return false;
    }

    const std::uint16_t narrowedFrameSize = static_cast<std::uint16_t>(stream->GetCursor() - frameStart);
    std::memcpy(frameStart, &narrowedFrameSize, sizeof(narrowedFrameSize));
    return true;
}

template <class Payload>
bool DecodeFrame(Payload& payload, network::Stream* stream) {
    unsigned char* const frameStart = stream->GetCursor();
    std::int16_t frameSize = 0;
    if (!stream->Decode(&frameSize) || !payload.Decode(stream)) {
        return false;
    }

    const std::ptrdiff_t consumed = stream->GetCursor() - frameStart;
    if (static_cast<std::ptrdiff_t>(frameSize) < consumed) {
        return false;
    }

    unsigned char* const buffer = stream->GetBuffer();
    const int bufferSize = stream->GetSize();
    if (!buffer || !frameStart || bufferSize < 0 || frameStart < buffer) {
        return false;
    }
    const std::ptrdiff_t frameStartOffset = frameStart - buffer;
    const std::ptrdiff_t frameEndOffset = frameStartOffset + static_cast<std::ptrdiff_t>(frameSize);
    if (frameEndOffset < 0 || frameEndOffset > bufferSize) {
        return false;
    }
    stream->SetCursor(buffer + frameEndOffset);
    return true;
}

} // namespace

bool S_NPCInfo_State::Init() { return bitSet.Init(bitCount, sizeof(bits), bits); }

bool S_NPCInfo_State::Encode(network::Stream* stream) const {
    if (!stream->Encode(&bitCount) || !stream->Encode(bits, sizeof(bits))) {
        return false;
    }
    if (IsSet(bitSet, 0) && !stream->Encode(&cIsDead)) {
        return false;
    }
    if (IsSet(bitSet, 1) && !stream->Encode(&cIsCombatMode)) {
        return false;
    }
    if (IsSet(bitSet, 2) && !stream->Encode(&cMoveMode)) {
        return false;
    }
    if (IsSet(bitSet, 3) && !stream->Encode(&cCreatureMoveType)) {
        return false;
    }
    return true;
}

bool S_NPCInfo_State::Decode(network::Stream* stream) {
    std::uint16_t transmittedBitCount = 0;
    if (!stream->Decode(&transmittedBitCount) || transmittedBitCount < bitCount || !stream->Decode(bits, sizeof(bits))) {
        return false;
    }

    const int extensionByteCount = static_cast<int>((transmittedBitCount + 7U) >> 3U) - static_cast<int>(sizeof(bits));
    if (!SkipExtensionBytes(stream, extensionByteCount)) {
        return false;
    }
    if (bitSet.IsSet(0) && !stream->Decode(&cIsDead)) {
        return false;
    }
    if (bitSet.IsSet(1) && !stream->Decode(&cIsCombatMode)) {
        return false;
    }
    if (bitSet.IsSet(2) && !stream->Decode(&cMoveMode)) {
        return false;
    }
    if (bitSet.IsSet(3) && !stream->Decode(&cCreatureMoveType)) {
        return false;
    }
    return true;
}

bool S_NPCInfo_Speed::Init() { return bitSet.Init(bitCount, sizeof(bits), bits); }

bool S_NPCInfo_Speed::Encode(network::Stream* stream) const {
    if (!stream->Encode(&bitCount) || !stream->Encode(bits, sizeof(bits))) {
        return false;
    }
    if (IsSet(bitSet, 0) &&
        !stream->Encode(reinterpret_cast<const unsigned char*>(&fMoveSpeedModifier), sizeof(fMoveSpeedModifier))) {
        return false;
    }
    if (IsSet(bitSet, 1) &&
        !stream->Encode(reinterpret_cast<const unsigned char*>(&fAttackSpeedModifier), sizeof(fAttackSpeedModifier))) {
        return false;
    }
    if (IsSet(bitSet, 2) && !stream->Encode(&hMCastingSpeed)) {
        return false;
    }
    if (IsSet(bitSet, 3) && !stream->Encode(&hPCastingSpeed)) {
        return false;
    }
    return true;
}

bool S_NPCInfo_Speed::Decode(network::Stream* stream) {
    std::uint16_t transmittedBitCount = 0;
    if (!stream->Decode(&transmittedBitCount) || transmittedBitCount < bitCount || !stream->Decode(bits, sizeof(bits))) {
        return false;
    }

    const int extensionByteCount = static_cast<int>((transmittedBitCount + 7U) >> 3U) - static_cast<int>(sizeof(bits));
    if (!SkipExtensionBytes(stream, extensionByteCount)) {
        return false;
    }
    if (bitSet.IsSet(0) &&
        !stream->Decode(reinterpret_cast<unsigned char*>(&fMoveSpeedModifier), sizeof(fMoveSpeedModifier))) {
        return false;
    }
    if (bitSet.IsSet(1) &&
        !stream->Decode(reinterpret_cast<unsigned char*>(&fAttackSpeedModifier), sizeof(fAttackSpeedModifier))) {
        return false;
    }
    if (bitSet.IsSet(2) && !stream->Decode(&hMCastingSpeed)) {
        return false;
    }
    if (bitSet.IsSet(3) && !stream->Decode(&hPCastingSpeed)) {
        return false;
    }
    return true;
}

bool UserInfo_Basic::Encode(network::Stream* stream) const {
    return stream->Encode(&wstrName) && stream->Encode(&nBuilder) && stream->Encode(&nRace) && stream->Encode(&nSex) &&
        stream->Encode(&nOriginalClass) && stream->Encode(&nNowClass) && stream->Encode(&nLevel);
}

bool UserInfo_Basic::Decode(network::Stream* stream) {
    return stream->Decode(&wstrName) && stream->Decode(&nBuilder) && stream->Decode(&nRace) && stream->Decode(&nSex) &&
        stream->Decode(&nOriginalClass) && stream->Decode(&nNowClass) && stream->Decode(&nLevel);
}

bool UserInfo_Stat::Encode(network::Stream* stream) const {
    return stream->Encode(&nStr) && stream->Encode(&nDex) && stream->Encode(&nCon) && stream->Encode(&nInt) &&
        stream->Encode(&nWit) && stream->Encode(&nMen) && stream->Encode(&nLuc) && stream->Encode(&nCha);
}

bool UserInfo_Stat::Decode(network::Stream* stream) {
    return stream->Decode(&nStr) && stream->Decode(&nDex) && stream->Decode(&nCon) && stream->Decode(&nInt) &&
        stream->Decode(&nWit) && stream->Decode(&nMen) && stream->Decode(&nLuc) && stream->Decode(&nCha);
}

bool UserInfo_BaseAttr::Encode(network::Stream* stream) const {
    return stream->Encode(&nBaseHP) && stream->Encode(&nBaseMP) && stream->Encode(&nBaseCP);
}

bool UserInfo_BaseAttr::Decode(network::Stream* stream) {
    return stream->Decode(&nBaseHP) && stream->Decode(&nBaseMP) && stream->Decode(&nBaseCP);
}

bool UserInfo_Attr::Encode(network::Stream* stream) const {
    return stream->Encode(&nHP) && stream->Encode(&nMP) && stream->Encode(&nCP) && stream->Encode(&nSP) &&
        stream->Encode(&nExp) && stream->Encode(reinterpret_cast<const unsigned char*>(&expRatio), sizeof(expRatio));
}

bool UserInfo_Attr::Decode(network::Stream* stream) {
    return stream->Decode(&nHP) && stream->Decode(&nMP) && stream->Decode(&nCP) && stream->Decode(&nSP) &&
        stream->Decode(&nExp) && stream->Decode(reinterpret_cast<unsigned char*>(&expRatio), sizeof(expRatio));
}

bool UserInfo_EquipSlot::Encode(network::Stream* stream) const {
    return stream->Encode(&cEnchant) && stream->Encode(&hMinSetItemEnchanted);
}

bool UserInfo_EquipSlot::Decode(network::Stream* stream) {
    return stream->Decode(&cEnchant) && stream->Decode(&hMinSetItemEnchanted);
}

bool UserInfo_HairShop::Encode(network::Stream* stream) const {
    return stream->Encode(&nFace) && stream->Encode(&nHairShape) && stream->Encode(&nHairColor) &&
        stream->Encode(&bIsUseHairAccessory);
}

bool UserInfo_HairShop::Decode(network::Stream* stream) {
    return stream->Decode(&nFace) && stream->Decode(&nHairShape) && stream->Decode(&nHairColor) &&
        stream->Decode(&bIsUseHairAccessory);
}

bool UserInfo_RideStoreCrystalizeAp::Encode(network::Stream* stream) const {
    return stream->Encode(&cYongmaType) && stream->Encode(&cPrivateStore) && stream->Encode(&canCrystallize) &&
        stream->Encode(&cRemainAbilityPoint);
}

bool UserInfo_RideStoreCrystalizeAp::Decode(network::Stream* stream) {
    return stream->Decode(&cYongmaType) && stream->Decode(&cPrivateStore) && stream->Decode(&canCrystallize) &&
        stream->Decode(&cRemainAbilityPoint);
}

bool UserInfo_Parameter::Encode(network::Stream* stream) const {
    return stream->Encode(&hAttackRange) && stream->Encode(&nPAttack) && stream->Encode(&nPAttackSpeed) &&
        stream->Encode(&nPDefence) && stream->Encode(&nPAvoid) && stream->Encode(&nPHitRate) &&
        stream->Encode(&nPCriticalRate) && stream->Encode(&nMAttack) && stream->Encode(&nMCastingSpeed) &&
        stream->Encode(&nPCastingSpeed) && stream->Encode(&nMAvoid) && stream->Encode(&nMDefend) &&
        stream->Encode(&nMHitRate) && stream->Encode(&nMCriticalRate);
}

bool UserInfo_Parameter::Decode(network::Stream* stream) {
    return stream->Decode(&hAttackRange) && stream->Decode(&nPAttack) && stream->Decode(&nPAttackSpeed) &&
        stream->Decode(&nPDefence) && stream->Decode(&nPAvoid) && stream->Decode(&nPHitRate) &&
        stream->Decode(&nPCriticalRate) && stream->Decode(&nMAttack) && stream->Decode(&nMCastingSpeed) &&
        stream->Decode(&nPCastingSpeed) && stream->Decode(&nMAvoid) && stream->Decode(&nMDefend) &&
        stream->Decode(&nMHitRate) && stream->Decode(&nMCriticalRate);
}

bool UserInfo_Defend::Encode(network::Stream* stream) const {
    return stream->Encode(&hFire) && stream->Encode(&hWater) && stream->Encode(&hWind) && stream->Encode(&hEarth) &&
        stream->Encode(&hHoly) && stream->Encode(&hUnholy);
}

bool UserInfo_Defend::Decode(network::Stream* stream) {
    return stream->Decode(&hFire) && stream->Decode(&hWater) && stream->Decode(&hWind) && stream->Decode(&hEarth) &&
        stream->Decode(&hHoly) && stream->Decode(&hUnholy);
}

bool UserInfo_PosVehicle::Encode(network::Stream* stream) const {
    return stream->Encode(&nPosX) && stream->Encode(&nPosY) && stream->Encode(&nPosZ) && stream->Encode(&nVehicleID);
}

bool UserInfo_PosVehicle::Decode(network::Stream* stream) {
    return stream->Decode(&nPosX) && stream->Decode(&nPosY) && stream->Decode(&nPosZ) && stream->Decode(&nVehicleID);
}

bool UserInfo_OrgSpeed::Encode(network::Stream* stream) const {
    return stream->Encode(reinterpret_cast<const unsigned char*>(nOrgSpeed), sizeof(nOrgSpeed));
}

bool UserInfo_OrgSpeed::Decode(network::Stream* stream) {
    return stream->Decode(reinterpret_cast<unsigned char*>(nOrgSpeed), sizeof(nOrgSpeed));
}

bool UserInfo_SpeedModifier::Encode(network::Stream* stream) const {
    return stream->Encode(reinterpret_cast<const unsigned char*>(&dMoveSpeedModifier), sizeof(dMoveSpeedModifier)) &&
        stream->Encode(reinterpret_cast<const unsigned char*>(&dAttackSpeedModifier), sizeof(dAttackSpeedModifier));
}

bool UserInfo_SpeedModifier::Decode(network::Stream* stream) {
    return stream->Decode(reinterpret_cast<unsigned char*>(&dMoveSpeedModifier), sizeof(dMoveSpeedModifier)) &&
        stream->Decode(reinterpret_cast<unsigned char*>(&dAttackSpeedModifier), sizeof(dAttackSpeedModifier));
}

bool UserInfo_CollisionInfo::Encode(network::Stream* stream) const {
    return stream->Encode(reinterpret_cast<const unsigned char*>(&dCollisionRadius), sizeof(dCollisionRadius)) &&
        stream->Encode(reinterpret_cast<const unsigned char*>(&dCollisionHeight), sizeof(dCollisionHeight));
}

bool UserInfo_CollisionInfo::Decode(network::Stream* stream) {
    return stream->Decode(reinterpret_cast<unsigned char*>(&dCollisionRadius), sizeof(dCollisionRadius)) &&
        stream->Decode(reinterpret_cast<unsigned char*>(&dCollisionHeight), sizeof(dCollisionHeight));
}

bool UserInfo_AttackInfo::Encode(network::Stream* stream) const {
    return stream->Encode(&nAttackType) && stream->Encode(&nAttackValue);
}

bool UserInfo_AttackInfo::Decode(network::Stream* stream) {
    return stream->Decode(&nAttackType) && stream->Decode(&nAttackValue);
}

bool UserInfo_Pledge::Encode(network::Stream* stream) const {
    return stream->Encode(&wstrNickName) && stream->Encode(&nPledgetype) && stream->Encode(&nPledgeSId) &&
        stream->Encode(&nPledgeEmblemId) && stream->Encode(&nPledgeCrestId) && stream->Encode(&nPledgePower) &&
        stream->Encode(&cIsPledgeMaster) && stream->Encode(&nAllianceId) && stream->Encode(&nAllianceCrestId) &&
        stream->Encode(&cIsShowPartyWantedMessage);
}

bool UserInfo_Pledge::Decode(network::Stream* stream) {
    return stream->Decode(&wstrNickName) && stream->Decode(&nPledgetype) && stream->Decode(&nPledgeSId) &&
        stream->Decode(&nPledgeEmblemId) && stream->Decode(&nPledgeCrestId) && stream->Decode(&nPledgePower) &&
        stream->Decode(&cIsPledgeMaster) && stream->Decode(&nAllianceId) && stream->Decode(&nAllianceCrestId) &&
        stream->Decode(&cIsShowPartyWantedMessage);
}

bool UserInfo_Social::Encode(network::Stream* stream) const {
    return stream->Encode(&nGuility) && stream->Encode(&nCriminalRate) && stream->Encode(&cIsNobless) &&
        stream->Encode(&cIsHero) && stream->Encode(&nSocialClass) && stream->Encode(&nPkCount) &&
        stream->Encode(&nDuelCount) && stream->Encode(&m_sVoteCount) && stream->Encode(&m_sBonusCount);
}

bool UserInfo_Social::Decode(network::Stream* stream) {
    return stream->Decode(&nGuility) && stream->Decode(&nCriminalRate) && stream->Decode(&cIsNobless) &&
        stream->Decode(&cIsHero) && stream->Decode(&nSocialClass) && stream->Decode(&nPkCount) &&
        stream->Decode(&nDuelCount) && stream->Decode(&m_sVoteCount) && stream->Decode(&m_sBonusCount);
}

bool UserInfo_Point::Encode(network::Stream* stream) const {
    return stream->Encode(&nVitalPoint) && stream->Encode(&cBR_LectureMark) && stream->Encode(&nPVPPoint) &&
        stream->Encode(&nRaidPoint);
}

bool UserInfo_Point::Decode(network::Stream* stream) {
    return stream->Decode(&nVitalPoint) && stream->Decode(&cBR_LectureMark) && stream->Decode(&nPVPPoint) &&
        stream->Decode(&nRaidPoint);
}

bool UserInfo_Etc1::Encode(network::Stream* stream) const {
    return stream->Encode(&nSkillModMaxDecoSlotCount) && stream->Encode(&nSkillModMaxJewelSlotCount) &&
        stream->Encode(&cEventMatchTeamID) && stream->Encode(&nFootEffect) &&
        stream->Encode(&nSkillModMaxAgathionMainSlotCount) && stream->Encode(&nSkillModMaxAgathionSubSlotCount) &&
        stream->Encode(&nSkillModMaxArtifactSlotGroupCount);
}

bool UserInfo_Etc1::Decode(network::Stream* stream) {
    return stream->Decode(&nSkillModMaxDecoSlotCount) && stream->Decode(&nSkillModMaxJewelSlotCount) &&
        stream->Decode(&cEventMatchTeamID) && stream->Decode(&nFootEffect) &&
        stream->Decode(&nSkillModMaxAgathionMainSlotCount) && stream->Decode(&nSkillModMaxAgathionSubSlotCount) &&
        stream->Decode(&nSkillModMaxArtifactSlotGroupCount);
}

bool UserInfo_Etc2::Encode(network::Stream* stream) const {
    return stream->Encode(&cEnvironment) && stream->Encode(&cIsFastMoveMode);
}

bool UserInfo_Etc2::Decode(network::Stream* stream) {
    return stream->Decode(&cEnvironment) && stream->Decode(&cIsFastMoveMode);
}

bool UserInfo_Etc3::Encode(network::Stream* stream) const {
    return stream->Encode(&nCharNameColor) && stream->Encode(&nNickNameColor);
}

bool UserInfo_Etc3::Decode(network::Stream* stream) {
    return stream->Decode(&nCharNameColor) && stream->Decode(&nNickNameColor);
}

bool UserInfo_Etc4::Encode(network::Stream* stream) const {
    return stream->Encode(&nYongmaClass) && stream->Encode(&hInvenLimit) && stream->Encode(&nCursedWeaponLevel);
}

bool UserInfo_Etc4::Decode(network::Stream* stream) {
    return stream->Decode(&nYongmaClass) && stream->Decode(&hInvenLimit) && stream->Decode(&nCursedWeaponLevel);
}

bool UserInfo_Etc5::Encode(network::Stream* stream) const {
    return stream->Encode(&cPVPRestrainStatus) && stream->Encode(&nUltimateSkillGauge) &&
        stream->Encode(&cIsSheathingWeapon) && stream->Encode(&cPledgeGameMemberFlag);
}

bool UserInfo_Etc5::Decode(network::Stream* stream) {
    return stream->Decode(&cPVPRestrainStatus) && stream->Decode(&nUltimateSkillGauge) &&
        stream->Decode(&cIsSheathingWeapon) && stream->Decode(&cPledgeGameMemberFlag);
}

bool UserInfo_Elemental::Encode(network::Stream* stream) const {
    return stream->Encode(&nElementalAttack) && stream->Encode(&nFireDefend) && stream->Encode(&nWaterDefend) &&
        stream->Encode(&nWindDefend) && stream->Encode(&nEarthDefend) && stream->Encode(&nCurrentElemental);
}

bool UserInfo_Elemental::Decode(network::Stream* stream) {
    return stream->Decode(&nElementalAttack) && stream->Decode(&nFireDefend) && stream->Decode(&nWaterDefend) &&
        stream->Decode(&nWindDefend) && stream->Decode(&nEarthDefend) && stream->Decode(&nCurrentElemental);
}

bool S_UserInfo::Init() { return bitSet.Init(bitCount, sizeof(bits), bits) && bitSet.SetAllBits(); }

bool S_UserInfo::Encode(network::Stream* stream) const {
    if (!stream->Encode(&bitCount) || !stream->Encode(bits, sizeof(bits))) {
        return false;
    }
    if (IsSet(bitSet, 0) && !stream->Encode(&nMyStatus)) {
        return false;
    }
    if (IsSet(bitSet, 1) && !EncodeFrame(basic, stream)) {
        return false;
    }
    if (IsSet(bitSet, 2) && !EncodeFrame(stat, stream)) {
        return false;
    }
    if (IsSet(bitSet, 3) && !EncodeFrame(baseAttr, stream)) {
        return false;
    }
    if (IsSet(bitSet, 4) && !EncodeFrame(attribute, stream)) {
        return false;
    }
    if (IsSet(bitSet, 5) && !EncodeFrame(equipSlot, stream)) {
        return false;
    }
    if (IsSet(bitSet, 6) && !EncodeFrame(hairShop, stream)) {
        return false;
    }
    if (IsSet(bitSet, 7) && !EncodeFrame(rideStoreCrystalizeAp, stream)) {
        return false;
    }
    if (IsSet(bitSet, 8) && !EncodeFrame(parameter, stream)) {
        return false;
    }
    if (IsSet(bitSet, 9) && !EncodeFrame(defend, stream)) {
        return false;
    }
    if (IsSet(bitSet, 10) && !EncodeFrame(posVehicle, stream)) {
        return false;
    }
    if (IsSet(bitSet, 11) && !EncodeFrame(orgSpeed, stream)) {
        return false;
    }
    if (IsSet(bitSet, 12) && !EncodeFrame(speedModifier, stream)) {
        return false;
    }
    if (IsSet(bitSet, 13) && !EncodeFrame(collisionInfo, stream)) {
        return false;
    }
    if (IsSet(bitSet, 14) && !EncodeFrame(attackInfo, stream)) {
        return false;
    }
    if (IsSet(bitSet, 15) && !EncodeFrame(pledge, stream)) {
        return false;
    }
    if (IsSet(bitSet, 16) && !EncodeFrame(social, stream)) {
        return false;
    }
    if (IsSet(bitSet, 17) && !EncodeFrame(point, stream)) {
        return false;
    }
    if (IsSet(bitSet, 18) && !EncodeFrame(etc1, stream)) {
        return false;
    }
    if (IsSet(bitSet, 19) && !EncodeFrame(etc2, stream)) {
        return false;
    }
    if (IsSet(bitSet, 20) && !EncodeFrame(etc3, stream)) {
        return false;
    }
    if (IsSet(bitSet, 21) && !EncodeFrame(etc4, stream)) {
        return false;
    }
    if (IsSet(bitSet, 22) && !EncodeFrame(etc5, stream)) {
        return false;
    }
    if (IsSet(bitSet, 23) && !EncodeFrame(elemental, stream)) {
        return false;
    }
    return true;
}

bool S_UserInfo::Decode(network::Stream* stream) {
    std::uint16_t transmittedBitCount = 0;
    if (!stream->Decode(&transmittedBitCount) || transmittedBitCount < bitCount || !stream->Decode(bits, sizeof(bits))) {
        return false;
    }

    const int extensionByteCount = static_cast<int>((transmittedBitCount + 7U) >> 3U) - static_cast<int>(sizeof(bits));
    if (!SkipExtensionBytes(stream, extensionByteCount)) {
        return false;
    }
    if (bitSet.IsSet(0) && !stream->Decode(&nMyStatus)) {
        return false;
    }
    if (bitSet.IsSet(1) && !DecodeFrame(basic, stream)) {
        return false;
    }
    if (bitSet.IsSet(2) && !DecodeFrame(stat, stream)) {
        return false;
    }
    if (bitSet.IsSet(3) && !DecodeFrame(baseAttr, stream)) {
        return false;
    }
    if (bitSet.IsSet(4) && !DecodeFrame(attribute, stream)) {
        return false;
    }
    if (bitSet.IsSet(5) && !DecodeFrame(equipSlot, stream)) {
        return false;
    }
    if (bitSet.IsSet(6) && !DecodeFrame(hairShop, stream)) {
        return false;
    }
    if (bitSet.IsSet(7) && !DecodeFrame(rideStoreCrystalizeAp, stream)) {
        return false;
    }
    if (bitSet.IsSet(8) && !DecodeFrame(parameter, stream)) {
        return false;
    }
    if (bitSet.IsSet(9) && !DecodeFrame(defend, stream)) {
        return false;
    }
    if (bitSet.IsSet(10) && !DecodeFrame(posVehicle, stream)) {
        return false;
    }
    if (bitSet.IsSet(11) && !DecodeFrame(orgSpeed, stream)) {
        return false;
    }
    if (bitSet.IsSet(12) && !DecodeFrame(speedModifier, stream)) {
        return false;
    }
    if (bitSet.IsSet(13) && !DecodeFrame(collisionInfo, stream)) {
        return false;
    }
    if (bitSet.IsSet(14) && !DecodeFrame(attackInfo, stream)) {
        return false;
    }
    if (bitSet.IsSet(15) && !DecodeFrame(pledge, stream)) {
        return false;
    }
    if (bitSet.IsSet(16) && !DecodeFrame(social, stream)) {
        return false;
    }
    if (bitSet.IsSet(17) && !DecodeFrame(point, stream)) {
        return false;
    }
    if (bitSet.IsSet(18) && !DecodeFrame(etc1, stream)) {
        return false;
    }
    if (bitSet.IsSet(19) && !DecodeFrame(etc2, stream)) {
        return false;
    }
    if (bitSet.IsSet(20) && !DecodeFrame(etc3, stream)) {
        return false;
    }
    if (bitSet.IsSet(21) && !DecodeFrame(etc4, stream)) {
        return false;
    }
    if (bitSet.IsSet(22) && !DecodeFrame(etc5, stream)) {
        return false;
    }
    if (bitSet.IsSet(23) && !DecodeFrame(elemental, stream)) {
        return false;
    }
    return true;
}

bool S_UserInfo_EquipSlot::Encode(network::Stream* stream) const {
    return stream->Encode(&nSlotItemId) && stream->Encode(&nSlotItemClassId) && stream->Encode(&nNormalOptionNo) &&
        stream->Encode(&nRandomOptionNo) && stream->Encode(&nShapeShiftClassId);
}

bool S_UserInfo_EquipSlot::Decode(network::Stream* stream) {
    return stream->Decode(&nSlotItemId) && stream->Decode(&nSlotItemClassId) && stream->Decode(&nNormalOptionNo) &&
        stream->Decode(&nRandomOptionNo) && stream->Decode(&nShapeShiftClassId);
}

bool S_UserInfo_EquipInfo::Init() { return bitSet.Init(bitCount, sizeof(bits), bits) && bitSet.SetAllBits(); }

bool S_UserInfo_EquipInfo::Encode(network::Stream* stream) const {
    if (!stream->Encode(&bitCount) || !stream->Encode(bits, sizeof(bits))) {
        return false;
    }

    const auto slots = EquipSlots(*this);
    for (std::uint32_t bit = 0; bit < bitCount; ++bit) {
        if (IsSet(bitSet, bit) && !EncodeFrame(*slots[bit], stream)) {
            return false;
        }
    }
    return true;
}

bool S_UserInfo_EquipInfo::Decode(network::Stream* stream) {
    std::uint16_t transmittedBitCount = 0;
    if (!stream->Decode(&transmittedBitCount) || transmittedBitCount < bitCount || !stream->Decode(bits, sizeof(bits))) {
        return false;
    }

    const int extensionByteCount = static_cast<int>((transmittedBitCount + 7U) >> 3U) - static_cast<int>(sizeof(bits));
    if (!SkipExtensionBytes(stream, extensionByteCount)) {
        return false;
    }

    const auto slots = EquipSlots(*this);
    for (std::uint32_t bit = 0; bit < bitCount; ++bit) {
        if (bitSet.IsSet(bit) && !DecodeFrame(*slots[bit], stream)) {
            return false;
        }
    }
    return true;
}

} // namespace protocol
