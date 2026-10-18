#include "Protocol/src/L2NPCProtocol.h"

#include "L2Server/src/Doppelganger.h"
#include "L2Server/src/NPC.h"
#include "L2Server/src/PVPMatch.h"
#include "L2Server/src/Party.h"
#include "L2Server/src/Pledge.h"
#include "L2Server/src/SharedContainer.h"
#include "L2Server/src/User.h"
#include "Shared/src/SharedFactory.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Win64Floating.h"

#include <cerrno>
#include <cstring>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace {

constexpr std::uint32_t kPacketTypeCount = 4;

bool IsValidPacketType(int packetType) noexcept { return static_cast<std::uint32_t>(packetType) < kPacketTypeCount; }

std::int16_t LowSigned16(std::int32_t value) noexcept {
    const auto bits = static_cast<std::uint16_t>(static_cast<std::uint32_t>(value));
    std::int16_t result = 0;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

std::uint32_t CurrentLastError() noexcept {
#if defined(_WIN32)
    return static_cast<std::uint32_t>(::GetLastError());
#else
    return static_cast<std::uint32_t>(errno);
#endif
}

} // namespace

const NPCPacketFilter g_PacketFilter[4] = {
    {
        0x15e,
        {0xfd, 0xbf, 0x5f, 0xf3, 0xec},
        {0xfd, 0xbf, 0x5f, 0xf3, 0xec},
        {
            1,
            1,
            1,
            1,
            1,
            1,
            0,
            1,
            1,
            0,
            1,
            1,
            1,
            1,
            1,
            1,
            0,
            1,
            0,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            0,
            0,
            1,
            1,
            1,
            1,
            1,
            0,
            1,
        },
    },
    {
        0x8b,
        {0xfd, 0xbf, 0x4f, 0xf3, 0xec},
        {0xfd, 0xbf, 0x4f, 0xf3, 0xec},
        {
            1,
            1,
            1,
            1,
            1,
            1,
            0,
            1,
            1,
            0,
            1,
            1,
            1,
            1,
            1,
            1,
            0,
            1,
            0,
            0,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            0,
            0,
            1,
            1,
            1,
            1,
            1,
            0,
            1,
        },
    },
    {
        0x0c,
        {0xff, 0xff, 0xff, 0xaf, 0xfc},
        {0xed, 0xbe, 0x4e, 0xa2, 0x0c},
        {
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            0,
            1,
            0,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
        },
    },
    {
        0x8b,
        {0xed, 0xbf, 0x4f, 0x02, 0x6c},
        {0xed, 0xbf, 0x4f, 0x02, 0x6c},
        {
            1,
            1,
            1,
            0,
            1,
            1,
            0,
            1,
            1,
            0,
            1,
            1,
            1,
            1,
            1,
            1,
            0,
            1,
            0,
            0,
            1,
            1,
            1,
            1,
            0,
            0,
            0,
            0,
            0,
            0,
            1,
            0,
            0,
            1,
            1,
            0,
            1,
        },
    },
};


NPCInfoPacket::NPCInfoPacket() = default;

NPCInfoPacket::~NPCInfoPacket() = default;

void NPCInfoPacket::SetSendDefaultPacket(int packetType) {
    if (!IsValidPacketType(packetType)) {
        return;
    }

    std::memcpy(
        m_NeedUpdatePacketBitSet.GetBits(), g_PacketFilter[packetType].nPacketInitMask,
        m_NeedUpdatePacketBitSet.GetByteCount()
    );
}

void NPCInfoPacket::ClearValue() {
    m_nId = 0;
    m_nClassId = 0;
    m_cIsEnemy = 0;
    m_nRelation = 0;
    std::memset(m_vInformingPos, 0, sizeof(m_vInformingPos));
    m_nMyAngle = 0;
    m_nVehicleId = 0;
    m_nMCastingSpeed = 0;
    m_nPCastingSpeed = 0;
    std::memset(m_nOrgSpeed, 0, sizeof(m_nOrgSpeed));
    m_dMoveSpeedModifier = 0.0F;
    m_dAttackSpeedModifier = 0.0F;
    m_dCollisionRadius = 0.0F;
    m_dCollisionHeight = 0.0F;
    m_dCollisionRadiusBig = 0.0F;
    m_dCollisionHeightBig = 0.0F;
    m_nRHandItemClassID = 0;
    m_nChestItemClassID = 0;
    m_nLHandItemClassID = 0;
    m_cStopMode = 0;
    m_cMoveMode = 0;
    m_cInCombatMode = 0;
    m_cIsDead = 0;
    m_cEnvironment = 0;
    m_cEventMatchTeamID = 0;
    m_nEnchantOfWeapon = 0;
    m_nCreatureMoveType = 0;
    m_nDoppelganggerSummonerSID = 0;
    m_nEvolutionId = 0;
    m_cTargetable = 0;
    m_cShowNameTag = 0;
    m_nState = 0;
    m_nMorphId = 0;
    m_nHP = 0;
    m_nMP = 0;
    m_nBaseHP = 0;
    m_nBaseMP = 0;
    m_cDoppleGangerType = 0;
    m_nFollowerId = 0;
    m_nFollowDistance = 0;
    m_dNpcCustomizedSizeRate = 0.0;
    m_cIsPetOrSummon = 0;
    m_szMasterName[0] = u'\0';
    m_szNickName[0] = u'\0';
    m_nNickNameIndex = -1;
    m_nMasterNameIndex = -1;
    m_cGuilty = 0;
    m_nCriminalRate = 0;
    m_nPledgeSID = 0;
    m_nPledgeCrestId = 0;
    m_nPledgeEmblemId = 0;
    m_nAllianceId = 0;
    m_nAllianceCrestId = 0;
    m_nEncodingTest = 0;
    m_NeedUpdatePacketBitSet.Clear();
    m_npcStatePacket.bitSet.Clear();
    m_npcSpeedPacket.bitSet.Clear();
}

bool NPCInfoPacket::MaskSendBits(int packetType) {
    if (!IsValidPacketType(packetType)) {
        return false;
    }

    for (std::size_t index = 0; index < sizeof(m_nBits); ++index) {
        m_nBits[index] &= g_PacketFilter[packetType].nPacketMask[index];
    }
    return true;
}

bool NPCInfoPacket::EncodePrivate(int packetType, User* user, CNPC* npc, CPacketHelperEx<20480>& packet) {
    if (!IsValidPacketType(packetType) || user == nullptr || npc == nullptr) {
        return false;
    }

    packet.Reset();
    const bool hideMasterInfo = npc->NeedToHideInfoAboutMaster();
    if (npc->IsDoppelganger()) {
        CDoppelganger* const doppelganger = npc->CastDoppelganger();
        User* const master = doppelganger->GetMaster();
        if (master != nullptr && !hideMasterInfo) {
            m_nRelation = master->GetRelationTo(user);
        }
    }

    m_cIsEnemy = npc->IsEnemyTo(user);
    if (g_PacketFilter[packetType].bPacketFilter[1]) {
        packet.Assemble("c", m_cIsEnemy != 0);
        m_NeedUpdatePacketBitSet.SetBit(1);
    }
    if (g_PacketFilter[packetType].bPacketFilter[2]) {
        packet.Assemble("d", m_nRelation);
        m_NeedUpdatePacketBitSet.SetBit(2);
    }
    if (g_PacketFilter[packetType].bPacketFilter[30]) {
        const char16_t* masterName = npc->m_sMasterName;
#if !defined(_WIN32)
        std::u16string targetMasterName;
#endif
        if (hideMasterInfo) {
            const char16_t* const visibleMasterName = npc->GetMaster()->GetName2(user);
#if defined(_WIN32)
            static_assert(sizeof(wchar_t) == sizeof(char16_t));
            masterName = reinterpret_cast<const char16_t*>(visibleMasterName);
#else
            targetMasterName = std::u16string(visibleMasterName);
            masterName = targetMasterName.c_str();
#endif
        }
        packet.Assemble("S", masterName);
        m_NeedUpdatePacketBitSet.SetBit(30);
    }
    return packet.GetLength() > 0;
}

bool NPCInfoPacket::EncodeCommon(bool initialize, int packetType, CNPC* npc, CPacketHelperEx<20480>& packet) {
    if (!IsValidPacketType(packetType) || npc == nullptr) {
        return false;
    }

    packet.Reset();
    const NPCPacketFilter& filter = g_PacketFilter[packetType];
    CVariableBitSet& updateBits = m_NeedUpdatePacketBitSet;
    bool changed = false;

    int vehicleId = 0;
    if (npc->IsInVehicle()) {
        vehicleId = CContainerDB::Inst().GetVehicleID(npc->m_data->container_index);
    } else if (npc->IsInAirShip()) {
        vehicleId = static_cast<int>(npc->GetMyAirShipID());
    } else if (npc->IsInShuttle()) {
        vehicleId = static_cast<int>(npc->GetMyShuttleID());
    }
    const FVector informingPosition = vehicleId > 0 ? npc->GetRelPos() : npc->m_data->GetPos();

    updateBits.SetIntValue_IfModifed(m_nClassId, npc->GetClassId(), 0);
    if (updateBits.IsSet(0)) {
        packet.Assemble("d", m_nClassId);
    }

    if (filter.bPacketFilter[4] && initialize) {
        changed |=
            updateBits.SetIntValue_IfModifed(m_vInformingPos[0], runtime::Win64CvttDoubleToInt32(informingPosition.x), 4);
        changed |=
            updateBits.SetIntValue_IfModifed(m_vInformingPos[1], runtime::Win64CvttDoubleToInt32(informingPosition.y), 4);
        changed |=
            updateBits.SetIntValue_IfModifed(m_vInformingPos[2], runtime::Win64CvttDoubleToInt32(informingPosition.z), 4);
        if (updateBits.IsSet(4)) {
            packet.Assemble("ddd", m_vInformingPos[0], m_vInformingPos[1], m_vInformingPos[2]);
            changed = false;
        }
    } else {
        updateBits.ClearBit(4);
    }

    if (filter.bPacketFilter[5] && initialize) {
        changed |= updateBits.SetIntValue_IfModifed(m_nMyAngle, static_cast<std::uint16_t>(npc->m_data->GetDirByInt()), 5);
        if (updateBits.IsSet(5)) {
            packet.Assemble("d", m_nMyAngle);
            changed = false;
        }
    } else {
        updateBits.ClearBit(5);
    }

    if (filter.bPacketFilter[6] && initialize) {
        changed |= updateBits.SetIntValue_IfModifed(m_nVehicleId, vehicleId, 6);
        if (updateBits.IsSet(6)) {
            packet.Assemble("d", m_nVehicleId);
            changed = false;
        }
    } else {
        updateBits.ClearBit(6);
    }

    if (filter.bPacketFilter[7] && initialize) {
        changed |= updateBits.SetIntValue_IfModifed(
            m_nMCastingSpeed, runtime::Win64CvttDoubleToInt32(npc->m_data->m_dParam[5]), 7
        );
        changed |= updateBits.SetIntValue_IfModifed(
            m_nPCastingSpeed, runtime::Win64CvttDoubleToInt32(npc->m_data->m_dParam[6]), 7
        );
        if (updateBits.IsSet(7)) {
            packet.Assemble("dd", m_nMCastingSpeed, m_nPCastingSpeed);
            changed = false;
        }
    } else {
        updateBits.ClearBit(7);
    }

    if (filter.bPacketFilter[8] && initialize) {
        changed |= updateBits.SetFloatValue_IfModifed(m_dMoveSpeedModifier, static_cast<float>(npc->GetMoveModifier()), 8);
        changed |=
            updateBits.SetFloatValue_IfModifed(m_dAttackSpeedModifier, static_cast<float>(npc->GetAttackModifier()), 8);
        if (updateBits.IsSet(8)) {
            packet.Assemble("FF", m_dMoveSpeedModifier, m_dAttackSpeedModifier);
            changed = false;
        }
    } else {
        updateBits.ClearBit(8);
    }

    if (filter.bPacketFilter[9]) {
        const int chestIndex = npc->m_data->m_nSlotIndex[ST_CHEST];
        int chestItemClassId = chestIndex == -1 ? -1 : CItemFactory::Inst().GetData(chestIndex)->m_nClassID;
        const int rightHandIndex = npc->m_data->m_nSlotIndex[ST_RHAND];
        int rightHandItemClassId =
            rightHandIndex == -1 ? npc->m_nRHandItemClassID : CItemFactory::Inst().GetData(rightHandIndex)->m_nClassID;
        const int leftHandIndex = npc->m_data->m_nSlotIndex[ST_LHAND];
        int leftHandItemClassId =
            leftHandIndex == -1 ? npc->m_nLHandItemClassID : CItemFactory::Inst().GetData(leftHandIndex)->m_nClassID;
        if (chestItemClassId < 1) {
            chestItemClassId = 0;
        }
        if (rightHandItemClassId < 1) {
            rightHandItemClassId = 0;
        }
        if (leftHandItemClassId < 1) {
            leftHandItemClassId = 0;
        }

        changed |= updateBits.SetIntValue_IfModifed(m_nRHandItemClassID, rightHandItemClassId, 9);
        changed |= updateBits.SetIntValue_IfModifed(m_nChestItemClassID, chestItemClassId, 9);
        changed |= updateBits.SetIntValue_IfModifed(m_nLHandItemClassID, leftHandItemClassId, 9);
        if (updateBits.IsSet(9)) {
            packet.Assemble("ddd", m_nRHandItemClassID, m_nChestItemClassID, m_nLHandItemClassID);
            changed = false;
        }
    }

    if (filter.bPacketFilter[10]) {
        changed |= updateBits.SetCharValue_IfModifed(m_cStopMode, static_cast<char>(npc->m_data->m_StopMode), 10);
        if (updateBits.IsSet(10)) {
            packet.Assemble("c", static_cast<int>(m_cStopMode));
            changed = false;
        }
    }

    if (filter.bPacketFilter[11] && initialize) {
        changed |= updateBits.SetCharValue_IfModifed(m_cMoveMode, static_cast<char>(npc->m_data->m_MoveMode), 11);
        if (updateBits.IsSet(11)) {
            packet.Assemble("c", static_cast<int>(m_cMoveMode));
            changed = false;
        }
    } else {
        updateBits.ClearBit(11);
    }

    if (filter.bPacketFilter[14]) {
        changed |= updateBits.SetCharValue_IfModifed(m_cEnvironment, static_cast<char>(npc->m_data->m_Environment), 14);
        if (updateBits.IsSet(14)) {
            packet.Assemble("c", static_cast<int>(m_cEnvironment));
            changed = false;
        }
    }

    if (filter.bPacketFilter[15]) {
        char eventMatchTeamId = 0;
        User* const master = npc->GetMaster();
        if (master == nullptr) {
            eventMatchTeamId = static_cast<char>(CPVPMatchSystem::inst().GetPVPMatchTeamIndex(npc));
        } else {
            // Current User slot +0x308 is the exact false body at
            // 0x0046c3fc, so the following current event-match block is
            // unreachable for every User-family vtable in this image.
            (void)master->IsEventMatch();
            if (master->IsInDuel() && npc->IsAlive()) {
                eventMatchTeamId = static_cast<char>(master->m_DuelUserInfo.GetTeamForEffect(master));
            }
            CParty* const party = master->GetParty();
            if (party != nullptr) {
                eventMatchTeamId = static_cast<char>(party->m_data->GetPVPMatchTeamIndex());
            }
        }
        changed |= updateBits.SetCharValue_IfModifed(m_cEventMatchTeamID, eventMatchTeamId, 15);
        if (updateBits.IsSet(15)) {
            packet.Assemble("c", static_cast<int>(m_cEventMatchTeamID));
            changed = false;
        }
    }

    if (filter.bPacketFilter[16]) {
        changed |= updateBits.SetIntValue_IfModifed(m_nEnchantOfWeapon, npc->m_nEnchantOfWeapon, 16);
        if (updateBits.IsSet(16)) {
            packet.Assemble("d", m_nEnchantOfWeapon);
            changed = false;
        }
    }

    int creatureMoveType = static_cast<int>(npc->m_data->m_moveType) == CMT_FLY ? 1 : 0;
    if (npc->m_data->m_bCanAct == 0) {
        creatureMoveType = 2;
    }
    int followerId = 0;
    int followDistance = 0;
    if (npc->m_FollowData.m_FollowType == CCreature::FOLLOW_ABSOLUTE_DIST_TYPE) {
        creatureMoveType = 3;
        followerId = npc->m_FollowData.m_FollowerSID;
        followDistance = npc->m_FollowData.m_FollowDistance;
    }

    if (filter.bPacketFilter[17] && initialize) {
        changed |= updateBits.SetIntValue_IfModifed(m_nCreatureMoveType, creatureMoveType, 17);
        if (updateBits.IsSet(17)) {
            packet.Assemble("d", m_nCreatureMoveType);
            changed = false;
        }
    } else {
        updateBits.ClearBit(17);
    }

    if (filter.bPacketFilter[18]) {
        changed |= updateBits.SetIntValue_IfModifed(m_nDoppelganggerSummonerSID, npc->GetDoppelgangerSummonerSID(), 18);
        if (updateBits.IsSet(18)) {
            packet.Assemble("d", m_nDoppelganggerSummonerSID);
            changed = false;
        }
    }

    if (filter.bPacketFilter[19]) {
        changed |= updateBits.SetIntValue_IfModifed(m_nEvolutionId, npc->m_data->m_EvolutionId, 19);
        if (updateBits.IsSet(19)) {
            packet.Assemble("d", m_nEvolutionId);
            changed = false;
        }
    }

    if (filter.bPacketFilter[22]) {
        changed |= updateBits.SetIntValue_IfModifed(m_nState, npc->State(), 22);
        if (updateBits.IsSet(22)) {
            packet.Assemble("d", m_nState);
            changed = false;
        }
    }

    if (filter.bPacketFilter[23] && initialize) {
        changed |= updateBits.SetIntValue_IfModifed(m_nMorphId, npc->GetMorphID(), 23);
        if (updateBits.IsSet(23)) {
            packet.Assemble("d", m_nMorphId);
            changed = false;
        }
    } else {
        updateBits.ClearBit(23);
    }

    if (filter.bPacketFilter[24]) {
        const bool wasSet = updateBits.IsSet(24);
        const bool hpChanged =
            updateBits.SetIntValue_IfModifed(m_nHP, runtime::Win64CvttDoubleToInt32(npc->m_data->GetHP()), 24);
        const bool sharedHpChanged = hpChanged && npc->m_data->m_NeedUpdatePacketBitSet.IsSet(11);
        changed |= sharedHpChanged;
        if (changed || wasSet) {
            packet.Assemble("d", m_nHP);
            updateBits.SetBit(24);
            changed = false;
            if (!npc->IsWorldObject()) {
                (void)npc->IsWorldTrap();
            }
        } else {
            updateBits.ClearBit(24);
        }
    }

    if (filter.bPacketFilter[25]) {
        const bool wasSet = updateBits.IsSet(25);
        const bool mpChanged =
            updateBits.SetIntValue_IfModifed(m_nMP, runtime::Win64CvttDoubleToInt32(npc->m_data->GetMP()), 25);
        const bool sharedMpChanged = mpChanged && npc->m_data->m_NeedUpdatePacketBitSet.IsSet(12);
        changed |= sharedMpChanged;
        if (changed || wasSet) {
            packet.Assemble("d", m_nMP);
            updateBits.SetBit(25);
            changed = false;
        } else {
            updateBits.ClearBit(25);
        }
    }

    bool baseHpCarry = changed;
    if (filter.bPacketFilter[26]) {
        baseHpCarry |= npc->m_data->m_NeedUpdatePacketBitSet.IsSet(8);
        baseHpCarry |= updateBits.IsSet(26);
        baseHpCarry |= updateBits.SetIntValue_IfModifed(m_nBaseHP, runtime::Win64CvttDoubleToInt32(npc->BaseHP()), 26);
        if (baseHpCarry) {
            packet.Assemble("d", m_nBaseHP);
            updateBits.SetBit(26);
            baseHpCarry = false;
            if (!npc->IsWorldObject()) {
                (void)npc->IsWorldTrap();
            }
        }
    }

    if (filter.bPacketFilter[27]) {
        bool baseMpChanged = baseHpCarry;
        baseMpChanged |= npc->m_data->m_NeedUpdatePacketBitSet.IsSet(9);
        baseMpChanged |= updateBits.IsSet(27);
        baseMpChanged |= updateBits.SetIntValue_IfModifed(m_nBaseMP, runtime::Win64CvttDoubleToInt32(npc->BaseMP()), 27);
        if (baseMpChanged) {
            packet.Assemble("d", m_nBaseMP);
            updateBits.SetBit(27);
        }
    }

    if (filter.bPacketFilter[28]) {
        updateBits.SetCharValue_IfModifed(m_cDoppleGangerType, npc->GetDoppelGangerType(), 28);
        if (updateBits.IsSet(28)) {
            packet.Assemble("c", static_cast<int>(m_cDoppleGangerType));
        }
    }

    if (filter.bPacketFilter[29]) {
        updateBits.SetIntValue_IfModifed(m_nFollowerId, followerId, 29);
        updateBits.SetIntValue_IfModifed(m_nFollowDistance, followDistance, 29);
        if (updateBits.IsSet(29)) {
            packet.Assemble("dd", m_nFollowerId, m_nFollowDistance);
        }
    }

    int pledgeEmblemId = 0;
    int pledgeServerId = 0;
    if (npc->m_data->m_bIsNPCRelatedToPledge) {
        CSPointer<CPledge> pledge =
            CPledge::FindPledge(runtime::process::CurrentDatabaseId(), static_cast<int>(npc->m_data->m_nPledgeID));
        if (pledge) {
            pledgeEmblemId = static_cast<int>(pledge->GetEmblemId());
            pledgeServerId = static_cast<int>(pledge->Id());
        }
    }

    const bool hideMasterInfo = npc->NeedToHideInfoAboutMaster();
    if (filter.bPacketFilter[3]) {
        static constexpr char16_t kEmptyName[] = u"";
        const char16_t* const nickname = hideMasterInfo ? kEmptyName : npc->m_sNickName;
        updateBits.SetStringValue_IfModifed(m_szNickName, sizeof(m_szNickName), nickname, 3);
        if (updateBits.IsSet(3)) {
            packet.Assemble("S", m_szNickName);
        }
    }

    if (filter.bPacketFilter[31]) {
        updateBits.SetIntValue_IfModifed(m_nNickNameIndex, hideMasterInfo ? -1 : npc->m_NickNameIndex, 31);
        if (updateBits.IsSet(31)) {
            packet.Assemble("d", m_nNickNameIndex);
        }
    }

    if (filter.bPacketFilter[32]) {
        updateBits.SetIntValue_IfModifed(m_nMasterNameIndex, hideMasterInfo ? -1 : npc->m_MasterNameIndex, 32);
        if (updateBits.IsSet(32)) {
            packet.Assemble("d", m_nMasterNameIndex);
        }
    }

    if (filter.bPacketFilter[33]) {
        updateBits.SetCharValue_IfModifed(m_cGuilty, hideMasterInfo ? 0 : npc->GetGuilty(), 33);
        if (updateBits.IsSet(33)) {
            packet.Assemble("c", static_cast<int>(m_cGuilty));
        }
    }

    if (filter.bPacketFilter[34]) {
        updateBits.SetIntValue_IfModifed(m_nCriminalRate, hideMasterInfo ? 0 : npc->CalcCriminalRate(), 34);
        if (updateBits.IsSet(34)) {
            packet.Assemble("d", m_nCriminalRate);
        }
    }

    if (filter.bPacketFilter[35]) {
        updateBits.SetIntValue_IfModifed(m_nPledgeSID, hideMasterInfo ? 0 : pledgeServerId, 35);
        updateBits.SetIntValue_IfModifed(m_nPledgeCrestId, hideMasterInfo ? 0 : npc->m_nPledgeCrestId, 35);
        updateBits.SetIntValue_IfModifed(m_nPledgeEmblemId, hideMasterInfo ? 0 : pledgeEmblemId, 35);
        updateBits.SetIntValue_IfModifed(
            m_nAllianceId, hideMasterInfo ? 0 : static_cast<int>(npc->m_data->m_nAllianceId), 35
        );
        updateBits.SetIntValue_IfModifed(m_nAllianceCrestId, hideMasterInfo ? 0 : npc->m_nAllianceCrestId, 35);
        if (updateBits.IsSet(35)) {
            packet.Assemble("ddddd", m_nPledgeSID, m_nPledgeCrestId, m_nPledgeEmblemId, m_nAllianceId, m_nAllianceCrestId);
        }
    }

    std::uint8_t encodedState = 0;
    CVariableBitSet stateBits;
    if (!stateBits.Init(8, 1, &encodedState)) {
        Log.Add(LOG_ERROR, L"[%s][%d]bitset init failed", L"..\\..\\..\\Protocol\\L2NPCProtocol.cpp", 0x3b2);
        return false;
    }
    stateBits.Clear();

    if (filter.bPacketFilter[12] && initialize) {
        updateBits.SetCharValue_IfModifed(m_cInCombatMode, npc->m_data->m_bIsInCombatMode ? 1 : 0, 12);
        if (updateBits.IsSet(12) && m_cInCombatMode != 0) {
            stateBits.SetBit(7);
        }
    } else {
        updateBits.ClearBit(12);
    }

    if (filter.bPacketFilter[13] && initialize) {
        updateBits.SetCharValue_IfModifed(m_cIsDead, npc->m_data->m_bIsAlive == 0 ? 1 : 0, 13);
        if (updateBits.IsSet(13) && m_cIsDead != 0) {
            stateBits.SetBit(6);
        }
    } else {
        updateBits.ClearBit(13);
    }

    if (filter.bPacketFilter[20]) {
        updateBits.SetCharValue_IfModifed(m_cTargetable, npc->m_data->m_StatusFlag[0] ? 1 : 0, 20);
        if (updateBits.IsSet(20) && m_cTargetable != 0) {
            stateBits.SetBit(5);
        }
    }

    if (filter.bPacketFilter[21]) {
        updateBits.SetCharValue_IfModifed(m_cShowNameTag, npc->m_data->m_StatusFlag[1] ? 1 : 0, 21);
        if (updateBits.IsSet(21) && m_cShowNameTag != 0) {
            stateBits.SetBit(4);
        }
    }

    if (updateBits.IsSet(12, 13, 20, 21, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX)) {
        packet.Assemble("c", static_cast<int>(encodedState));
    }
    return packet.GetLength() > 0;
}

bool NPCInfoPacket::EncodeVisualEffect(bool initialize, CNPC* npc, CPacketHelperEx<20480>& packet) {
    if (npc == nullptr) {
        return false;
    }

    packet.Reset();
    if (initialize) {
        npc->MakeAbnormalVisualEffectPacket(packet);
        m_NeedUpdatePacketBitSet.SetBit(36);
        return true;
    }

    const bool sharedEffectChanged = npc->m_data->m_NeedUpdatePacketBitSet.IsSet(75);
    const bool updateRequested = m_NeedUpdatePacketBitSet.IsSet(36);
    const int morphId = npc->GetMorphID();
    const bool changed = sharedEffectChanged || updateRequested || m_nMorphId != morphId;
    if (changed) {
        m_nMorphId = npc->GetMorphID();
        CPacketHelperEx<20480> visualEffects;
        npc->MakeAbnormalVisualEffectPacket(visualEffects);
        packet.Assemble(
            "cddhb", 0x4b, npc->m_nId, m_nMorphId, LowSigned16(visualEffects.GetAssembledCount()), visualEffects.GetLength(),
            visualEffects.GetPacket()
        );
        m_NeedUpdatePacketBitSet.ClearBit(36);
    }
    return changed;
}

bool NPCInfoPacket::EncodeState(bool initialize, CNPC* npc, CPacketHelperEx<20480>& packet) {
    if (npc == nullptr) {
        return false;
    }

    packet.Reset();
    const bool isDead = npc->m_data->m_bIsAlive == 0;
    const bool isInCombatMode = npc->m_data->m_bIsInCombatMode;
    const char moveMode = static_cast<char>(npc->m_data->m_MoveMode);
    char creatureMoveType = static_cast<std::int32_t>(npc->m_data->m_moveType) == CMT_FLY ? 1 : 0;
    if (npc->m_FollowData.m_FollowType != CCreature::FOLLOW_NONE_TYPE) {
        if (npc->m_data->m_bCanAct == 0) {
            creatureMoveType = 2;
        } else if (npc->m_FollowData.m_FollowType == CCreature::FOLLOW_ABSOLUTE_DIST_TYPE) {
            creatureMoveType = 3;
        }
    }

    if (initialize) {
        m_npcStatePacket.cIsDead = isDead;
        m_npcStatePacket.cIsCombatMode = isInCombatMode;
        m_npcStatePacket.cMoveMode = moveMode;
        m_npcStatePacket.cCreatureMoveType = creatureMoveType;
        m_npcSpeedPacket.bitSet.Clear();
        return false;
    }

    bool changed = m_npcStatePacket.bitSet.SetCharValue_IfModifed(m_npcStatePacket.cIsDead, isDead, 0);
    changed |= m_npcStatePacket.bitSet.SetCharValue_IfModifed(m_npcStatePacket.cIsCombatMode, isInCombatMode, 1);
    changed |= m_npcStatePacket.bitSet.SetCharValue_IfModifed(m_npcStatePacket.cMoveMode, moveMode, 2);
    changed |= m_npcStatePacket.bitSet.SetCharValue_IfModifed(m_npcStatePacket.cCreatureMoveType, creatureMoveType, 3);
    if (!changed) {
        return false;
    }

    CPacketHelperEx<20480> encodedPacket;
    network::Stream stream(reinterpret_cast<unsigned char*>(encodedPacket.GetPacket()), encodedPacket.GetBufferSize());
    if (!m_npcStatePacket.Encode(&stream)) {
        Log.Add(
            LOG_ERROR, L"[%s][%d] fail to EncodeStat, errorNo: %d", L"..\\..\\..\\Protocol\\L2NPCProtocol.cpp", 0x733,
            CurrentLastError()
        );
        return false;
    }

    m_npcStatePacket.bitSet.Clear();
    if (stream.GetCursor() == stream.GetBuffer()) {
        return false;
    }

    std::uint8_t state = isDead ? 1U : 0U;
    if (isInCombatMode) {
        state |= 2U;
    }
    if (moveMode != 0) {
        state |= 4U;
    }
    if (creatureMoveType == 1) {
        state |= 8U;
    } else if (creatureMoveType == 2) {
        state |= 0x10U;
    } else if (creatureMoveType == 3) {
        state |= 0x20U;
    }
    packet.Assemble("cdc", 0xc1, npc->m_nId, static_cast<int>(state));
    return true;
}

bool NPCInfoPacket::Encode(
    int packetType,
    int sendType,
    CNPC* npc,
    CPacketHelperEx<20480>& privatePacket,
    CPacketHelperEx<20480>& commonPacket,
    CPacketHelperEx<20480>& visualEffectPacket,
    CPacketHelperEx<20480>& packet
) {
    if (!IsValidPacketType(packetType) || !MaskSendBits(packetType)) {
        return false;
    }

    if (packetType == 0) {
        packet.Assemble(
            "chdchb", 0xfe, 0x15e, npc->m_nId, sendType, m_NeedUpdatePacketBitSet.GetBitCount(),
            m_NeedUpdatePacketBitSet.GetByteCount(), m_NeedUpdatePacketBitSet.GetBits()
        );
    } else {
        packet.Assemble(
            "cdchb", g_PacketFilter[packetType].nPacketNo, npc->m_nId, sendType, m_NeedUpdatePacketBitSet.GetBitCount(),
            m_NeedUpdatePacketBitSet.GetByteCount(), m_NeedUpdatePacketBitSet.GetBits()
        );
    }

    char* privateBytes = privatePacket.GetPacket();
    const int privateLength = privatePacket.GetLength();
    const int privatePrefix = privatePacket.GetLength();
    packet.Assemble("cb", privatePrefix, privateLength, privateBytes);

    char* commonBytes = commonPacket.GetPacket();
    const int commonLength = commonPacket.GetLength();
    const std::int16_t commonPrefix = LowSigned16(commonPacket.GetLength());
    packet.Assemble("hb", commonPrefix, commonLength, commonBytes);

    if (sendType != 1) {
        char* visualEffectBytes = visualEffectPacket.GetPacket();
        const int visualEffectLength = visualEffectPacket.GetLength();
        const int visualEffectPrefix = visualEffectPacket.GetAssembledCount();
        packet.Assemble("hb", visualEffectPrefix, visualEffectLength, visualEffectBytes);
    }
    return true;
}

bool NPCInfoPacket::EncodeProtocol152(
    User* const user, CNPC* const npc, const int sendType, CPacketHelperEx<20480>& packet
) {
    if (user == nullptr || npc == nullptr) {
        return false;
    }

    // Portable protocol extension only. Exact NA 5.0.0.35 Engine handler
    // S_NPC_INFO@0x2030c800 uses the same 37 component bodies as the current
    // packet owner, but wraps them in its older 0x0c envelope. Reuse the
    // recovered body producers: CVariableBitSet and the exact client both
    // number mask bits MSB-first, and EncodeCommon writes the
    // packed 12/13/20/21 state byte after every ordinary common component.
    const int packetType = npc->GetPacketType();
    CPacketHelperEx<20480> commonBlock;
    CPacketHelperEx<20480> privateBlock;
    CPacketHelperEx<20480> abnormalEffects;
    if (!EncodeCommon(true, packetType, npc, commonBlock) || !EncodePrivate(packetType, user, npc, privateBlock) ||
        !EncodeVisualEffect(true, npc, abnormalEffects) || !MaskSendBits(packetType)) {
        return false;
    }

    return EncodeProtocol152(sendType, npc, privateBlock, commonBlock, abnormalEffects, packet);
}

bool NPCInfoPacket::EncodeProtocol152(
    const int sendType,
    CNPC* const npc,
    CPacketHelperEx<20480>& privateBlock,
    CPacketHelperEx<20480>& commonBlock,
    CPacketHelperEx<20480>& abnormalEffects,
    CPacketHelperEx<20480>& packet
) {
    // P152 accepts partial updates. Keep the prepared bits so a state
    // update does not also send a position update.
    if (npc == nullptr || !MaskSendBits(npc->GetPacketType())) {
        return false;
    }
    packet.Reset();
    packet.Assemble(
        "cdchb", 0x0c,
        // Init() clears the packet's cached ID. The client needs the NPC's ID.
        static_cast<int>(npc->m_nId),
        sendType, static_cast<int>(m_NeedUpdatePacketBitSet.GetBitCount()),
        static_cast<int>(m_NeedUpdatePacketBitSet.GetByteCount()), m_NeedUpdatePacketBitSet.GetBits()
    );
    packet.Assemble(
        "cbhb", privateBlock.GetLength(), privateBlock.GetLength(), privateBlock.GetPacket(), commonBlock.GetLength(),
        commonBlock.GetLength(), commonBlock.GetPacket()
    );
    // Component 36 is not part of the length-delimited common block. Exact
    // S_NPC_INFO reads its H count and H entries only after that block.
    if (m_NeedUpdatePacketBitSet.IsSet(36)) {
        packet.Assemble(
            "hb", LowSigned16(abnormalEffects.GetAssembledCount()), abnormalEffects.GetLength(), abnormalEffects.GetPacket()
        );
    }
    return packet.GetLength() > 0;
}

bool NPCInfoPacket::Init(int packetType) {
    if (!IsValidPacketType(packetType) || !m_NeedUpdatePacketBitSet.Init(37, sizeof(m_nBits), m_nBits)) {
        return false;
    }

    ClearValue();
    SetSendDefaultPacket(packetType);

    if (!m_npcStatePacket.bitSet.Init(protocol::S_NPCInfo_State::bitCount, sizeof(m_npcStatePacket.bits), m_npcStatePacket.bits)) {
        return false;
    }
    m_npcStatePacket.bitSet.SetAllBits();

    if (!m_npcSpeedPacket.bitSet.Init(protocol::S_NPCInfo_Speed::bitCount, sizeof(m_npcSpeedPacket.bits), m_npcSpeedPacket.bits)) {
        return false;
    }
    m_npcSpeedPacket.bitSet.SetAllBits();
    return true;
}

bool NPCInfoPacket::Clear() {
    ClearValue();
    return true;
}

bool NPCInfoPacket::EncodeSpeed(bool initialize, CNPC* npc, CPacketHelperEx<20480>& packet) {
    if (!npc) {
        return false;
    }

    packet.Reset();

    const float moveSpeedModifier = static_cast<float>(npc->GetMoveModifier());
    const float attackSpeedModifier = static_cast<float>(npc->GetAttackModifier());
    const std::int16_t mCastingSpeed = LowSigned16(runtime::Win64CvttDoubleToInt32(npc->m_data->m_dParam[5]));
    const std::int16_t pCastingSpeed = LowSigned16(runtime::Win64CvttDoubleToInt32(npc->m_data->m_dParam[6]));

    if (initialize) {
        m_npcSpeedPacket.fMoveSpeedModifier = moveSpeedModifier;
        m_npcSpeedPacket.fAttackSpeedModifier = attackSpeedModifier;
        m_npcSpeedPacket.hMCastingSpeed = mCastingSpeed;
        m_npcSpeedPacket.hPCastingSpeed = pCastingSpeed;
        m_npcSpeedPacket.bitSet.Clear();
        return false;
    }

    bool changed =
        m_npcSpeedPacket.bitSet.SetFloatValue_IfModifed(m_npcSpeedPacket.fMoveSpeedModifier, moveSpeedModifier, 0);
    changed |=
        m_npcSpeedPacket.bitSet.SetFloatValue_IfModifed(m_npcSpeedPacket.fAttackSpeedModifier, attackSpeedModifier, 1);
    changed |= m_npcSpeedPacket.bitSet.SetShortValue_IfModifed(m_npcSpeedPacket.hMCastingSpeed, mCastingSpeed, 2);
    changed |= m_npcSpeedPacket.bitSet.SetShortValue_IfModifed(m_npcSpeedPacket.hPCastingSpeed, pCastingSpeed, 3);
    if (!changed) {
        return false;
    }

    CPacketHelperEx<20480> encodedPacket;
    network::Stream stream(reinterpret_cast<unsigned char*>(encodedPacket.GetPacket()), encodedPacket.GetBufferSize());
    if (!m_npcSpeedPacket.Encode(&stream)) {
        Log.Add(
            LOG_ERROR, L"[%s][%d] fail to EncodeSpeed, errorNo: %d", L"..\\..\\..\\Protocol\\L2NPCProtocol.cpp", 0x78b,
            CurrentLastError()
        );
        return false;
    }

    m_npcSpeedPacket.bitSet.Clear();
    const int encodedLength = static_cast<int>(stream.GetCursor() - stream.GetBuffer());
    if (encodedLength <= 0) {
        return false;
    }

    packet.Assemble("chdb", 0xfe, 0x132, npc->m_nId, encodedLength, stream.GetBuffer());
    return true;
}
