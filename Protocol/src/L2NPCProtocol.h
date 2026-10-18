#pragma once

#include "L2Server/src/L2Packet.h"

#include "Shared/src/PacketHelper.h"

#include <cstdint>

class CNPC;
class User;

// Original Protocol/L2NPCProtocol.h record. The four current rows are compiled
// by L2NPCProtocol.obj as one 0xd0-byte constant.
struct NPCPacketFilter {
    std::int32_t nPacketNo;
    std::uint8_t nPacketMask[5];
    std::uint8_t nPacketInitMask[5];
    bool bPacketFilter[37];
};


extern const NPCPacketFilter g_PacketFilter[4];


// Original Protocol/L2NPCProtocol.h owner. Current L2Server fixes the complete
// 0x1a8-byte layout and the packet-type/mask contract below.
struct NPCInfoPacket {
    NPCInfoPacket();
    ~NPCInfoPacket();

    bool Init(int packetType);
    bool Clear();
    void SetSendDefaultPacket(int packetType);
    void ClearValue();
    bool MaskSendBits(int packetType);

    bool EncodePrivate(int packetType, User* user, CNPC* npc, CPacketHelperEx<20480>& packet);
    bool EncodeCommon(bool initialize, int packetType, CNPC* npc, CPacketHelperEx<20480>& packet);
    bool EncodeVisualEffect(bool initialize, CNPC* npc, CPacketHelperEx<20480>& packet);
    bool Encode(
        int packetType,
        int sendType,
        CNPC* npc,
        CPacketHelperEx<20480>& privatePacket,
        CPacketHelperEx<20480>& commonPacket,
        CPacketHelperEx<20480>& visualEffectPacket,
        CPacketHelperEx<20480>& packet
    );
    bool EncodeState(bool initialize, CNPC* npc, CPacketHelperEx<20480>& packet);
    bool EncodeSpeed(bool initialize, CNPC* npc, CPacketHelperEx<20480>& packet);

    // Portable Salvation-P152 serializer. This is separate
    // from Encode(): the latter is the exact current revision-162 delta
    // envelope and must keep its recovered ABI and wire contract.
    bool EncodeProtocol152(User* user, CNPC* npc, int sendType, CPacketHelperEx<20480>& packet);

    // Portable P152 envelope for already prepared common/private deltas.
    bool EncodeProtocol152(
        int sendType,
        CNPC* npc,
        CPacketHelperEx<20480>& privateBlock,
        CPacketHelperEx<20480>& commonBlock,
        CPacketHelperEx<20480>& abnormalEffects,
        CPacketHelperEx<20480>& packet
    );

    std::uint8_t m_nBits[5]; // +0x000
    CVariableBitSet m_NeedUpdatePacketBitSet; // +0x008
    std::int32_t m_nId; // +0x018
    std::int32_t m_nClassId; // +0x01c
    char m_cIsEnemy; // +0x020
    std::int32_t m_nRelation; // +0x024
    std::int32_t m_vInformingPos[3]; // +0x028
    std::int32_t m_nMyAngle; // +0x034
    std::int32_t m_nVehicleId; // +0x038
    std::int32_t m_nMCastingSpeed; // +0x03c
    std::int32_t m_nPCastingSpeed; // +0x040
    std::int32_t m_nOrgSpeed[8]; // +0x044
    float m_dMoveSpeedModifier; // +0x064
    float m_dAttackSpeedModifier; // +0x068
    float m_dCollisionRadius; // +0x06c
    float m_dCollisionHeight; // +0x070
    float m_dCollisionRadiusBig; // +0x074
    float m_dCollisionHeightBig; // +0x078
    std::int32_t m_nRHandItemClassID; // +0x07c
    std::int32_t m_nChestItemClassID; // +0x080
    std::int32_t m_nLHandItemClassID; // +0x084
    char m_cStopMode; // +0x088
    char m_cMoveMode; // +0x089
    char m_cInCombatMode; // +0x08a
    char m_cIsDead; // +0x08b
    char m_cEnvironment; // +0x08c
    char m_cEventMatchTeamID; // +0x08d
    std::int32_t m_nEnchantOfWeapon; // +0x090
    std::int32_t m_nCreatureMoveType; // +0x094
    std::int32_t m_nDoppelganggerSummonerSID; // +0x098
    std::int32_t m_nEvolutionId; // +0x09c
    char m_cTargetable; // +0x0a0
    char m_cShowNameTag; // +0x0a1
    std::int32_t m_nState; // +0x0a4
    std::int32_t m_nMorphId; // +0x0a8
    std::int32_t m_nHP; // +0x0ac
    std::int32_t m_nMP; // +0x0b0
    std::int32_t m_nBaseHP; // +0x0b4
    std::int32_t m_nBaseMP; // +0x0b8
    char m_cDoppleGangerType; // +0x0bc
    std::int32_t m_nFollowerId; // +0x0c0
    std::int32_t m_nFollowDistance; // +0x0c4
    double m_dNpcCustomizedSizeRate; // +0x0c8
    char m_cIsPetOrSummon; // +0x0d0
    char16_t m_szMasterName[25]; // +0x0d2
    char16_t m_szNickName[25]; // +0x104
    std::int32_t m_nNickNameIndex; // +0x138
    std::int32_t m_nMasterNameIndex; // +0x13c
    char m_cGuilty; // +0x140
    std::int32_t m_nCriminalRate; // +0x144
    std::int32_t m_nPledgeSID; // +0x148
    std::int32_t m_nPledgeCrestId; // +0x14c
    std::int32_t m_nPledgeEmblemId; // +0x150
    std::int32_t m_nAllianceId; // +0x154
    std::int32_t m_nAllianceCrestId; // +0x158
    std::int32_t m_nEncodingTest; // +0x15c
    protocol::S_NPCInfo_State m_npcStatePacket; // +0x160
    protocol::S_NPCInfo_Speed m_npcSpeedPacket; // +0x17d
};
