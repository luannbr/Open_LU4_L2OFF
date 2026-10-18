#include "AirShip.h"
#include "CConfig.h"
#include "CreatureController.h"
#include "Cubic.h"
#include "CursedWeapon.h"
#include "DB.h"
#include "Duel.h"
#include "Fishing.h"
#include "GameEventSystem.h"
#include "Item.h"
#include "NPC.h"
#include "NoblessSystem.h"
#include "OlympiadMatch.h"
#include "OlympiadStuff.h"
#include "PCParam.h"
#include "PVPMatch.h"
#include "ParserExpData.h"
#include "Party.h"
#include "PartyRoomRegistry.h"
#include "Pet.h"
#include "Pledge.h"
#include "Protocol/src/L2Constants.h"
#include "Setting.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/UserConst.h"
#include "SharedContainer.h"
#include "Skill.h"
#include "SkillAcquire.h"
#include "ThreadLocalWorldMap.h"
#include "Transform.h"
#include "User.h"
#include "UserSocket.h"

#include "Portable/ClientProtocolRuntime.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SlotType.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"
#include "Shared/src/Vitality.h"

#include <array>
#include <climits>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

class ProtocolAwareCharInfoSender final : public CTLMFunc {
public:
    ProtocolAwareCharInfoSender(
        const int senderObjectId,
        const int pendingWriteLimit,
        CPacketHelperEx<20480>& currentPacket,
        CPacketHelperEx<20480>& protocol152Packet
    ) :
        m_senderObjectId(senderObjectId),
        m_pendingWriteLimit(pendingWriteLimit),
        m_currentPacket(currentPacket),
        m_protocol152Packet(protocol152Packet) {}

    bool CanDo(CObject* object, double) override {
        if (object == nullptr || !object->IsPC() || object->IsDeleting() || object->m_Deleted != 0) {
            return true;
        }

        User* const receiver = object->CastUser();
        CUserSocket* const socket = receiver == nullptr ? nullptr : receiver->GetSocket();
        if (receiver == nullptr || socket == nullptr || static_cast<int>(receiver->m_nId) == m_senderObjectId ||
            socket->PendingWrite() >= m_pendingWriteLimit) {
            return true;
        }

        CPacketHelperEx<20480>& packet =
            runtime::client_protocol::NegotiatedRevision(socket) == runtime::client_protocol::kNorthAmericanRevision
            ? m_protocol152Packet
            : m_currentPacket;
        socket->Send("b", packet.GetLength(), packet.GetPacket());
        return true;
    }

private:
    int m_senderObjectId;
    int m_pendingWriteLimit;
    CPacketHelperEx<20480>& m_currentPacket;
    CPacketHelperEx<20480>& m_protocol152Packet;
};

const void* UserBroadcastDoubleBitsAsPointer(const double value) noexcept {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return reinterpret_cast<const void*>(static_cast<std::uintptr_t>(bits));
}

constexpr char kGmViewCharacterInfoFormat[] =
    "cdddddSddddQfddddddddddddQddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
    "ddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
    "dddddddddddddddddddddddddddddddccccchdddddddddddddddddddddddffffddddSdddcccddhhddddccdccddhhhhhhhhdddd";

// Current UserBroadcast.obj data at 0x015e9888..0x015e9f57. These arrays
// contain only ancestors (never the queried class itself),
// including the duplicated Ertheia entries present in the current image.
ClassType baseJobs0To57[58][4] = {
    {CT_NONE, CT_NONE, CT_NONE, CT_NONE},
    {CT_FIGHTER, CT_NONE, CT_NONE, CT_NONE},
    {CT_FIGHTER, CT_WARRIOR, CT_NONE, CT_NONE},
    {CT_FIGHTER, CT_WARRIOR, CT_NONE, CT_NONE},
    {CT_FIGHTER, CT_NONE, CT_NONE, CT_NONE},
    {CT_FIGHTER, CT_KNIGHT, CT_NONE, CT_NONE},
    {CT_FIGHTER, CT_KNIGHT, CT_NONE, CT_NONE},
    {CT_FIGHTER, CT_NONE, CT_NONE, CT_NONE},
    {CT_FIGHTER, CT_ROGUE, CT_NONE, CT_NONE},
    {CT_FIGHTER, CT_ROGUE, CT_NONE, CT_NONE},
    {CT_NONE, CT_NONE, CT_NONE, CT_NONE},
    {CT_MAGE, CT_NONE, CT_NONE, CT_NONE},
    {CT_MAGE, CT_WIZARD, CT_NONE, CT_NONE},
    {CT_MAGE, CT_WIZARD, CT_NONE, CT_NONE},
    {CT_MAGE, CT_WIZARD, CT_NONE, CT_NONE},
    {CT_MAGE, CT_NONE, CT_NONE, CT_NONE},
    {CT_MAGE, CT_CLERIC, CT_NONE, CT_NONE},
    {CT_MAGE, CT_CLERIC, CT_NONE, CT_NONE},
    {CT_NONE, CT_NONE, CT_NONE, CT_NONE},
    {CT_ELVEN_FIGHTER, CT_NONE, CT_NONE, CT_NONE},
    {CT_ELVEN_FIGHTER, CT_ELVEN_KNIGHT, CT_NONE, CT_NONE},
    {CT_ELVEN_FIGHTER, CT_ELVEN_KNIGHT, CT_NONE, CT_NONE},
    {CT_ELVEN_FIGHTER, CT_NONE, CT_NONE, CT_NONE},
    {CT_ELVEN_FIGHTER, CT_ELVEN_SCOUT, CT_NONE, CT_NONE},
    {CT_ELVEN_FIGHTER, CT_ELVEN_SCOUT, CT_NONE, CT_NONE},
    {CT_NONE, CT_NONE, CT_NONE, CT_NONE},
    {CT_ELVEN_MAGE, CT_NONE, CT_NONE, CT_NONE},
    {CT_ELVEN_MAGE, CT_ELVEN_WIZARD, CT_NONE, CT_NONE},
    {CT_ELVEN_MAGE, CT_ELVEN_WIZARD, CT_NONE, CT_NONE},
    {CT_ELVEN_MAGE, CT_NONE, CT_NONE, CT_NONE},
    {CT_ELVEN_MAGE, CT_ORACLE, CT_NONE, CT_NONE},
    {CT_NONE, CT_NONE, CT_NONE, CT_NONE},
    {CT_DARK_FIGHTER, CT_NONE, CT_NONE, CT_NONE},
    {CT_DARK_FIGHTER, CT_PALUS_KNIGHT, CT_NONE, CT_NONE},
    {CT_DARK_FIGHTER, CT_PALUS_KNIGHT, CT_NONE, CT_NONE},
    {CT_DARK_FIGHTER, CT_NONE, CT_NONE, CT_NONE},
    {CT_DARK_FIGHTER, CT_ASSASIN, CT_NONE, CT_NONE},
    {CT_DARK_FIGHTER, CT_ASSASIN, CT_NONE, CT_NONE},
    {CT_NONE, CT_NONE, CT_NONE, CT_NONE},
    {CT_DARK_MAGE, CT_NONE, CT_NONE, CT_NONE},
    {CT_DARK_MAGE, CT_DARK_WIZARD, CT_NONE, CT_NONE},
    {CT_DARK_MAGE, CT_DARK_WIZARD, CT_NONE, CT_NONE},
    {CT_DARK_MAGE, CT_NONE, CT_NONE, CT_NONE},
    {CT_DARK_MAGE, CT_SHILLIEN_ORACLE, CT_NONE, CT_NONE},
    {CT_NONE, CT_NONE, CT_NONE, CT_NONE},
    {CT_ORC_FIGHTER, CT_NONE, CT_NONE, CT_NONE},
    {CT_ORC_FIGHTER, CT_ORC_RAIDER, CT_NONE, CT_NONE},
    {CT_ORC_FIGHTER, CT_NONE, CT_NONE, CT_NONE},
    {CT_ORC_FIGHTER, CT_ORC_MONK, CT_NONE, CT_NONE},
    {CT_NONE, CT_NONE, CT_NONE, CT_NONE},
    {CT_ORC_MAGE, CT_NONE, CT_NONE, CT_NONE},
    {CT_ORC_MAGE, CT_ORC_SHAMAN, CT_NONE, CT_NONE},
    {CT_ORC_MAGE, CT_ORC_SHAMAN, CT_NONE, CT_NONE},
    {CT_NONE, CT_NONE, CT_NONE, CT_NONE},
    {CT_DWARVEN_FIGHTER, CT_NONE, CT_NONE, CT_NONE},
    {CT_DWARVEN_FIGHTER, CT_SCAVENGER, CT_NONE, CT_NONE},
    {CT_DWARVEN_FIGHTER, CT_NONE, CT_NONE, CT_NONE},
    {CT_DWARVEN_FIGHTER, CT_ARTISAN, CT_NONE, CT_NONE},
};

ClassType baseJobs88To118[31][4] = {
    {CT_FIGHTER, CT_WARRIOR, CT_GLADIATOR, CT_NONE},
    {CT_FIGHTER, CT_WARRIOR, CT_WARLORD, CT_NONE},
    {CT_FIGHTER, CT_KNIGHT, CT_PALADIN, CT_NONE},
    {CT_FIGHTER, CT_KNIGHT, CT_DARK_AVENGER, CT_NONE},
    {CT_FIGHTER, CT_ROGUE, CT_HAWKEYE, CT_NONE},
    {CT_FIGHTER, CT_ROGUE, CT_TREASURE_HUNTER, CT_NONE},
    {CT_MAGE, CT_WIZARD, CT_SORCERER, CT_NONE},
    {CT_MAGE, CT_WIZARD, CT_NECROMANCER, CT_NONE},
    {CT_MAGE, CT_WIZARD, CT_WARLOCK, CT_NONE},
    {CT_MAGE, CT_CLERIC, CT_BISHOP, CT_NONE},
    {CT_MAGE, CT_CLERIC, CT_PROPHET, CT_NONE},
    {CT_ELVEN_FIGHTER, CT_ELVEN_KNIGHT, CT_TEMPLE_KNIGHT, CT_NONE},
    {CT_ELVEN_FIGHTER, CT_ELVEN_KNIGHT, CT_SWORDSINGER, CT_NONE},
    {CT_ELVEN_FIGHTER, CT_ELVEN_SCOUT, CT_PLAIN_WALKER, CT_NONE},
    {CT_ELVEN_FIGHTER, CT_ELVEN_SCOUT, CT_SILVER_RANGER, CT_NONE},
    {CT_ELVEN_MAGE, CT_ELVEN_WIZARD, CT_SPELLSINGER, CT_NONE},
    {CT_ELVEN_MAGE, CT_ELVEN_WIZARD, CT_ELEMENTAL_SUMMONER, CT_NONE},
    {CT_ELVEN_MAGE, CT_ORACLE, CT_ELDER, CT_NONE},
    {CT_DARK_FIGHTER, CT_PALUS_KNIGHT, CT_SHILLIEN_KNIGHT, CT_NONE},
    {CT_DARK_FIGHTER, CT_PALUS_KNIGHT, CT_BLADEDANCER, CT_NONE},
    {CT_DARK_FIGHTER, CT_ASSASIN, CT_ABYSS_WALKER, CT_NONE},
    {CT_DARK_FIGHTER, CT_ASSASIN, CT_PHANTOM_RANGER, CT_NONE},
    {CT_DARK_MAGE, CT_DARK_WIZARD, CT_SPELLHOWLER, CT_NONE},
    {CT_DARK_MAGE, CT_DARK_WIZARD, CT_PHANTOM_SUMMONER, CT_NONE},
    {CT_DARK_MAGE, CT_SHILLIEN_ORACLE, CT_SHILLIEN_ELDER, CT_NONE},
    {CT_ORC_FIGHTER, CT_ORC_RAIDER, CT_DESTROYER, CT_NONE},
    {CT_ORC_FIGHTER, CT_ORC_MONK, CT_TYRANT, CT_NONE},
    {CT_ORC_MAGE, CT_ORC_SHAMAN, CT_OVERLORD, CT_NONE},
    {CT_ORC_MAGE, CT_ORC_SHAMAN, CT_WARCRYER, CT_NONE},
    {CT_DWARVEN_FIGHTER, CT_SCAVENGER, CT_BOUNTY_HUNTER, CT_NONE},
    {CT_DWARVEN_FIGHTER, CT_ARTISAN, CT_WARSMITH, CT_NONE},
};

ClassType baseJobs123To136[14][4] = {
    {CT_NONE, CT_NONE, CT_NONE, CT_NONE},
    {CT_NONE, CT_NONE, CT_NONE, CT_NONE},
    {CT_KAMAEL_M_SOLDIER, CT_NONE, CT_NONE, CT_NONE},
    {CT_KAMAEL_F_SOLDIER, CT_NONE, CT_NONE, CT_NONE},
    {CT_KAMAEL_M_SOLDIER, CT_TROOPER, CT_NONE, CT_NONE},
    {CT_KAMAEL_M_SOLDIER, CT_TROOPER, CT_NONE, CT_NONE},
    {CT_KAMAEL_F_SOLDIER, CT_WARDER, CT_NONE, CT_NONE},
    {CT_KAMAEL_F_SOLDIER, CT_WARDER, CT_NONE, CT_NONE},
    {CT_KAMAEL_M_SOLDIER, CT_TROOPER, CT_BERSERKER, CT_NONE},
    {CT_KAMAEL_M_SOLDIER, CT_TROOPER, CT_M_SOUL_BREAKER, CT_NONE},
    {CT_KAMAEL_F_SOLDIER, CT_WARDER, CT_F_SOUL_BREAKER, CT_NONE},
    {CT_KAMAEL_F_SOLDIER, CT_WARDER, CT_ARBALESTER, CT_NONE},
    {CT_KAMAEL_F_SOLDIER, CT_WARDER, CT_NONE, CT_NONE},
    {CT_KAMAEL_F_SOLDIER, CT_WARDER, CT_INSPECTOR, CT_NONE},
};

ClassType baseJobs182To187[6][4] = {
    {CT_NONE, CT_NONE, CT_NONE, CT_NONE},
    {CT_NONE, CT_NONE, CT_NONE, CT_NONE},
    {CT_ERTHEIA_FIGHTER, CT_ERTHEIA_FIGHTER, CT_NONE, CT_NONE},
    {CT_ERTHEIA_WIZARD, CT_ERTHEIA_WIZARD, CT_NONE, CT_NONE},
    {CT_ERTHEIA_FIGHTER, CT_NONE, CT_MARAUDER, CT_NONE},
    {CT_ERTHEIA_WIZARD, CT_NONE, CT_MENACE, CT_NONE},
};

} // namespace

int User::GetOlympiadTeamID() {
    // Current L2Server.exe 0x00b15680.
    return m_pWOlympiadUser->GetTeamId();
}

char User::GetEventMatchTeamId() {
    // Current L2Server.exe 0x00bd805c.
    L2_GUARD_FUNCTION();

    std::uint8_t team = 0;
    if (m_EventMatchUser.m_byEventMatchTeamId != 0) {
        CSPointer<CEventMatch> match = m_EventMatchUser.GetEventMatch();
        if (match) {
            const EventMatchStatus status = match->GetStatus();
            if (status == EMStatus_PLAYING || status == EMStatus_PAUSE) {
                team = m_EventMatchUser.m_byEventMatchTeamId;
            }
        }
    }

    if (IsOlympiadPlayer()) {
        team = static_cast<std::uint8_t>(GetOlympiadTeamID());
    }

    if (m_DuelUserInfo.IsInFightStatus()) {
        team = m_DuelUserInfo.GetTeamForEffect(this);
    } else {
        CParty* const party = GetParty();
        if (party != nullptr && party->m_data->IsPVPMatching()) {
            team = static_cast<std::uint8_t>(party->m_data->GetPVPMatchTeamIndex());
        } else if (m_data->m_cPVPMatchTeamID != 0 && IsInPVPMatchExcept(PVPMatchType::CLEFT, PVPMatchPlayerState::NONE)) {
            team = static_cast<std::uint8_t>(m_data->m_cPVPMatchTeamID);
        }
    }
    return static_cast<char>(team);
}

void User::BeforeAcquireSubjobSkills(const bool initializeShortcuts) {
    // Current L2Server.exe 0x00bd6b78.
    L2_GUARD_FUNCTION();

    DispelByBuffProtectionFlag(0x20);
    // Clear the active skill under its own lock. Keep the learned skills.
    {
        L2_GUARD(m_InstantDurationSect);
        m_InstantDurationSkill.pSkillInfo = nullptr;
    }
    ToggleSkillAllOff(false);
    AuraSkillAllOff(false);
    m_pFishingController->m_nExperienceLevel = 0;
    m_pFishingController->m_dExperienceBonus = 0.0;
    m_nEnergyCount = 0;
    m_nSongDanceCount = 0;
    m_SoulCount = 0;
    // Keep the soul timestamp when clearing the soul count.
    std::memset(m_nAutoSoulshotFlags, 0, 0x44);

    if (!g_bUseAutoSoulShotUI) {
        GetSocket()->Send("chddd", 0xfe, 0x0c, 0, 0, -1);
    } else {
        for (int index = 0; index < 4; ++index) {
            m_AutoSoulShotUi[index].m_ItemClassID = 0;
            GetSocket()->Send("chddd", 0xfe, 0x0c, 0, m_AutoSoulShotUi[index].m_IsActiveState, index);
        }
    }

    if (initializeShortcuts) {
        m_ShortCut.Init();
    }
    TakeOffItems();
}

void User::SendNicknameChanged() {
    // Current L2Server.exe 0x00bdb4d8.
    L2_GUARD_FUNCTION();
    const FVector position = m_data->m_Pos;
    g_ptlUserMap->BroadcastToNeighbor(*this, 0x80000, position, "cdS", 0xcc, static_cast<int>(m_nId), m_sNickName);
}

void User::SendPledgeStatusChanged() {
    // Current L2Server.exe 0x00bdb5dc.
    L2_GUARD_FUNCTION();
    const FVector position = m_data->m_Pos;
    g_ptlUserMap->BroadcastToNeighbor(
        *this, 0x80000, position, "cdddddddd", 0xcd, m_data->m_nWorldId, static_cast<int>(m_nId),
        static_cast<int>(m_nPledgeSid), static_cast<int>(m_nPledgeCrestDbId), static_cast<int>(m_data->m_nAllianceId),
        static_cast<int>(m_nAllianceCrestDbId), static_cast<int>(m_nPledgeEmblemDbId), m_data->m_nPledgeType
    );
}

void User::AcquireSubjobSkills() {
    // Current L2Server.exe 0x00bdbb28.
    L2_GUARD_FUNCTION();

    std::map<int, AcquiredSkillInfo> changedSkills;
    {
        L2_GUARD(m_AcquiredSkillsSect);
        _DeleteAllAcquireSkills(false);

        const auto& acquirableSkills = g_SkillAcquireDB.GetAcquirableSkills(m_data->m_nClass);
        for (SkillAcquireInfo* const acquireInfo : acquirableSkills) {
            if (acquireInfo->nGetLevel > m_data->m_nLevel) {
                continue;
            }
            if (acquireInfo->bAutoGet && !acquireInfo->m_RaceConditions.empty() &&
                acquireInfo->m_RaceConditions.find(m_data->m_Race) == acquireInfo->m_RaceConditions.end()) {
                continue;
            }

            if (_AcquireSkill(acquireInfo->nSkillID, acquireInfo->nSkillLevel, 0, false, !acquireInfo->bAutoGet) &&
                !acquireInfo->bAutoGet) {
                const AcquiredSkillInfo changedSkill(acquireInfo->nSkillID, acquireInfo->nSkillLevel, 0, -1, true);
                const auto inserted = changedSkills.emplace(acquireInfo->nSkillID, changedSkill);
                if (!inserted.second) {
                    inserted.first->second = changedSkill;
                }
            }
        }
    }
    AcquireSkills(g_SkillAcquireDB.m_vectorRaceSkill[m_data->m_Race], true, false);
    db.RequestSaveAcquiredSkills(this, changedSkills);
}

void User::InitDualClass(const int dualClassId, const int classId) {
    // Current L2Server.exe 0x00bd7650.
    L2_GUARD_FUNCTION();

    // Retained manual ownership: current EXE acquires at 0x00bd76f4.
    // invalid current subjob branches 0x00bd7706/0x00bd7710 reach
    // 0x00bd77d7 without the valid-path release at 0x00bd77cf.
    // A scope guard would change this shipping behavior.
    m_lock->Enter(L"UserBroadcast.cpp", 0xb34);
    if (m_data->m_nSubJobId >= 0 && m_data->m_nSubJobId < 4) {
        m_data->m_SubJobLevel[m_data->m_nSubJobId] = m_data->m_nLevel;
        m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
            reinterpret_cast<std::int32_t&>(m_data->m_nClass), classId, 3
        );
        m_data->m_DualClassLevel = m_data->m_nLevel;

        switch (dualClassId) {
            case 0:
                m_data->m_nSubJob0Class = classId;
                break;
            case 1:
                m_data->m_nSubJob1Class = classId;
                break;
            case 2:
                m_data->m_nSubJob2Class = classId;
                break;
            case 3:
                m_data->m_nSubJob3Class = classId;
                break;
            default:
                break;
        }

        m_data->m_DualClassID = dualClassId == 0 ? -1 : dualClassId;
        m_data->m_IsDualClass = true;
        m_lock->Leave(nullptr, 0);
    }
}

void User::InitSubJob(const int subJobId, const int classId) {
    // Current L2Server.exe 0x00bd77f4.
    L2_GUARD_FUNCTION();

    // Retained manual ownership: current EXE acquires at 0x00bd7899.
    // invalid current subjob branches 0x00bd78a9/0x00bd78b3 reach
    // 0x00bd7af5 without the valid-path release at 0x00bd7aed.
    // A scope guard would change this shipping behavior.
    m_lock->Enter(L"UserBroadcast.cpp", 0xb67);
    if (m_data->m_nSubJobId >= 0 && m_data->m_nSubJobId < 4) {
        m_data->m_SubJobLevel[m_data->m_nSubJobId] = m_data->m_nLevel;
        m_data->m_nSubJobId = subJobId;
        m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
            reinterpret_cast<std::int32_t&>(m_data->m_nClass), classId, 3
        );

        switch (subJobId) {
            case 0:
                m_data->m_nSubJob0Class = classId;
                break;
            case 1:
                m_data->m_nSubJob1Class = classId;
                break;
            case 2:
                m_data->m_nSubJob2Class = classId;
                break;
            case 3:
                m_data->m_nSubJob3Class = classId;
                break;
            default:
                break;
        }

        m_data->m_IsDualClass = false;
        if (m_data->m_nLevel != 40) {
            m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_data->m_nLevel, 40, 5);
            m_data->m_NeedUpdatePacketBitSet.SetBit(7);
        }

        if (static_cast<unsigned int>(subJobId) < 4U) {
            m_data->m_SubJobLevel[subJobId] = m_data->m_nLevel;
            if (static_cast<unsigned int>(m_data->m_DualClassID) == static_cast<unsigned int>(subJobId)) {
                m_data->m_DualClassLevel = m_data->m_nLevel;
            }
        }

        const std::int64_t experience = ExpByLevel[m_data->m_nLevel];
        if (m_data->m_nExp != experience) {
            m_data->m_NeedUpdatePacketBitSet.SetInt64Value_IfModifed(m_data->m_nExp, experience, 6);
            m_data->m_NeedUpdatePacketBitSet.SetBit(7);
        }
        m_data->m_HennaId_1 = 0;
        m_data->m_HennaId_2 = 0;
        m_data->m_HennaId_3 = 0;
        m_data->m_NeedUpdatePacketBitSet.SetInt64Value_IfModifed(m_data->m_nSP, 0, 0x0e);
        m_data->m_stPotentialCount.nCount1 = 0;
        m_data->m_stPotentialCount.nCount2 = 0;
        m_data->m_stPotentialCount.nCount3 = 0;
        m_data->SetAP(0, false);
        SetExp(experience);

        const int maximumHennaSlots = g_PCParamDB.GetMaxHennaSlotCount(m_data->m_nClass);
        if (m_data->m_cEnableHenna != maximumHennaSlots) {
            m_data->m_cEnableHenna = static_cast<char>(maximumHennaSlots);
        }
        m_data->m_NeedUpdatePacketBitSet.SetAllBits();
        m_lock->Leave(nullptr, 0);
    }
}

void User::InitDualJob(const int dualJobId, const int classId) {
    // Current L2Server.exe 0x00bd7b14.
    L2_GUARD_FUNCTION();

    // Retained manual ownership: current EXE acquires at 0x00bd7bb7.
    // invalid current subjob branches 0x00bd7bcb/0x00bd7bd5 reach
    // 0x00bd7e3f without the valid-path release at 0x00bd7e37.
    // A scope guard would change this shipping behavior.
    m_lock->Enter(L"UserBroadcast.cpp", 0xbb2);
    if (m_data->m_nSubJobId >= 0 && m_data->m_nSubJobId < 4) {
        m_data->m_SubJobLevel[m_data->m_nSubJobId] = m_data->m_nLevel;
        m_data->m_nSubJobId = dualJobId;
        m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
            reinterpret_cast<std::int32_t&>(m_data->m_nClass), classId, 3
        );
        m_data->m_DualClassLevel = m_data->m_nLevel;

        switch (dualJobId) {
            case 0:
                m_data->m_nSubJob0Class = classId;
                break;
            case 1:
                m_data->m_nSubJob1Class = classId;
                break;
            case 2:
                m_data->m_nSubJob2Class = classId;
                break;
            case 3:
                m_data->m_nSubJob3Class = classId;
                break;
            default:
                break;
        }

        m_data->m_DualClassID = dualJobId == 0 ? -1 : dualJobId;
        m_data->m_IsDualClass = true;
        if (m_data->m_nLevel != 85) {
            m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_data->m_nLevel, 85, 5);
            m_data->m_NeedUpdatePacketBitSet.SetBit(7);
        }

        if (static_cast<unsigned int>(dualJobId) < 4U) {
            m_data->m_SubJobLevel[dualJobId] = m_data->m_nLevel;
            if (static_cast<unsigned int>(m_data->m_DualClassID) == static_cast<unsigned int>(dualJobId)) {
                m_data->m_DualClassLevel = m_data->m_nLevel;
            }
        }

        const std::int64_t experience = ExpByLevel[m_data->m_nLevel];
        if (m_data->m_nExp != experience) {
            m_data->m_NeedUpdatePacketBitSet.SetInt64Value_IfModifed(m_data->m_nExp, experience, 6);
            m_data->m_NeedUpdatePacketBitSet.SetBit(7);
        }
        m_data->m_HennaId_1 = 0;
        m_data->m_HennaId_2 = 0;
        m_data->m_HennaId_3 = 0;
        m_data->m_NeedUpdatePacketBitSet.SetInt64Value_IfModifed(m_data->m_nSP, 0, 0x0e);
        m_data->m_stPotentialCount.nCount1 = 0;
        m_data->m_stPotentialCount.nCount2 = 0;
        m_data->m_stPotentialCount.nCount3 = 0;
        m_data->SetAP(0, false);
        SetExp(experience);

        const int maximumHennaSlots = g_PCParamDB.GetMaxHennaSlotCount(m_data->m_nClass);
        if (m_data->m_cEnableHenna != maximumHennaSlots) {
            m_data->m_cEnableHenna = static_cast<char>(maximumHennaSlots);
        }
        m_data->m_NeedUpdatePacketBitSet.SetAllBits();
        m_lock->Leave(nullptr, 0);
    }
}

void User::ValidateSubJobParameters() {
    // Current L2Server.exe 0x00bd8374.
    L2_GUARD_FUNCTION();

    InitializeBuilderCmdMod();
    InitializeSkillMod();
    m_data->m_cEnableHenna = static_cast<char>(g_PCParamDB.GetMaxHennaSlotCount(m_data->m_nClass));

    CSetting& setting = Singleton<CSetting>::inst();
    const int initialClassType = GetInitialClassType(static_cast<int>(m_data->m_nClass), m_data->m_Race);
    if (initialClassType == -1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s] has invalid class[%d] at file[%s], line[%d]",
        //     GetName(),
        //     static_cast<int>(m_data->m_nClass),
        //     L"UserBroadcast.cpp",
        //     0xa17)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[subclass] Initial class could not be resolved for character %s (class %d). Parameter validation continues
        //     with initial-class index -1.", GetName(), static_cast<int>(m_data->m_nClass))
        // L2_LOG_ERROR(Log,
        //     L"[subclass] Initial class could not be resolved for character %s (class %d). Parameter validation continues
        //     with initial-class index -1.", GetName(), static_cast<int>(m_data->m_nClass))
        L2_LOG_ERROR(
            Log,
            u"[subclass] Initial class could not be resolved for character %s (class %d); parameter validation continues "
            u"with initial-class index -1.",
            GetName(), static_cast<int>(m_data->m_nClass)
        );
    }

    const CSetting::StatValueType& stat = setting.m_recommended_stat[initialClassType];
    m_data->m_OriginalStat.m_N[1] = stat.Str;
    m_data->m_OriginalStat.m_N[4] = stat.Dex;
    m_data->m_OriginalStat.m_N[2] = stat.Con;
    m_data->m_OriginalStat.m_N[0] = stat.Int;
    m_data->m_OriginalStat.m_N[5] = stat.Wit;
    m_data->m_OriginalStat.m_N[3] = stat.Men;
    m_data->SetStats(m_data->m_OriginalStat);

    m_data->m_nPrivateStoreLimit = g_PCParamDB.GetOriMaxPrivateStoreCount(m_data->m_nClass, m_data->m_Race);
    m_data->m_nPrivateStoreBuyLimit = g_PCParamDB.GetOriMaxPrivateStoreBuyCount(m_data->m_nClass, m_data->m_Race);

    const SexType sex = m_data->m_Sex;
    const ClassType classType = m_data->m_nClass;
    const RaceType race = m_data->m_Race;
    m_data->m_BaseDamageRange = g_PCParamDB.GetBaseDamageRangeValue(sex, classType, race);
    m_data->m_BaseAttackType =
        static_cast<AttackType>(g_PCParamDB.GetIntValueByField(sex, classType, PC_BASE_ATTACK_TYPE, race));
    m_data->m_nBaseAttackRange = g_PCParamDB.GetIntValueByField(sex, classType, PC_BASE_ATTACK_RANGE, race);
    m_data->m_nBaseRandomDamage = g_PCParamDB.GetIntValueByField(sex, classType, PC_BASE_RAND_DAM, race);
    m_data->m_nBaseParam[VALIDATE::PATTACK] = g_PCParamDB.GetIntValueByField(sex, classType, PC_BASE_PATTACK, race);
    m_data->m_nBaseParam[VALIDATE::MATTACK] = g_PCParamDB.GetIntValueByField(sex, classType, PC_BASE_MATTACK, race);
    m_data->m_nBaseParam[VALIDATE::PDEFEND] =
        static_cast<int>(g_PCParamDB.GetBodyPart(sex, classType, PCBODY::PDEFEND_SUM, VALIDATE::PDEFEND, race));
    m_data->m_nBaseParam[VALIDATE::MDEFEND] =
        static_cast<int>(g_PCParamDB.GetBodyPart(sex, classType, PCBODY::MDEFEND_SUM, VALIDATE::MDEFEND, race));
    m_data->m_nBaseParam[VALIDATE::PATTACKSPEED] =
        g_PCParamDB.GetIntValueByField(sex, classType, PC_BASE_ATTACK_SPEED, race);
    m_data->m_nBaseParam[VALIDATE::PCRITICAL] = g_PCParamDB.GetIntValueByField(sex, classType, PC_BASE_CRITICAL, race);

    m_data->m_nOrgParam[VALIDATE::PATTACK] = m_data->m_nBaseParam[VALIDATE::PATTACK];
    m_data->m_nOrgParam[VALIDATE::PDEFEND] = m_data->m_nBaseParam[VALIDATE::PDEFEND];
    m_data->m_nOrgParam[VALIDATE::MATTACK] = m_data->m_nBaseParam[VALIDATE::MATTACK];
    m_data->m_nOrgParam[VALIDATE::MDEFEND] = m_data->m_nBaseParam[VALIDATE::MDEFEND];
    m_data->m_nOrgParam[VALIDATE::PATTACKSPEED] = m_data->m_nBaseParam[VALIDATE::PATTACKSPEED];
    m_data->m_nOrgParam[VALIDATE::MATTACKSPEED] = m_data->m_nBaseParam[VALIDATE::MATTACKSPEED];
    m_data->m_nOrgParam[VALIDATE::PCRITICAL] = m_data->m_nBaseParam[VALIDATE::PCRITICAL];
    m_data->m_nOrgParam[VALIDATE::PHIT] = m_data->m_nBaseParam[VALIDATE::PHIT];
    m_data->m_nOrgParam[VALIDATE::PAVOID] = m_data->m_nBaseParam[VALIDATE::PAVOID];
    m_data->m_nOrgParam[VALIDATE::SHIELDDEFENSE] = m_data->m_nBaseParam[VALIDATE::SHIELDDEFENSE];
    m_data->m_nOrgParam[VALIDATE::SHIELDDEFENSERATE] = m_data->m_nBaseParam[VALIDATE::SHIELDDEFENSERATE];

    m_data->m_dCollisionRadius = g_PCParamDB.GetCollisionRadius(sex, m_nBaseClass, race);
    m_data->m_dCollisionHeight = g_PCParamDB.GetCollisionHeight(sex, m_nBaseClass, race);
    m_nMaxPrivateBankCount = g_PCParamDB.GetOriMaxPrivateBankCount(classType, race);
    m_nMaxInventoryLimit = g_PCParamDB.GetOriMaxInventoryCount(classType, race);
    m_data->m_nMaxInventoryNum = m_nMaxInventoryLimit;
    m_data->m_nMaxQuestInventoryNum = m_nMaxQuestInventoryLimit;
    m_inv.SetMaxInventoryLimit(m_nMaxQuestInventoryLimit);
    m_nMaxDwarvenRecipeCount = 100;
    m_nMaxCommonRecipeCount = 100;
    m_nMaxDwarvenRecipeStore = 40;
    m_nMaxArtifactInventoryLimit = 100;
    m_data->m_nMaxArtifactInventoryNum = 100;
    m_inv.SetMaxArtifactInventoryLimit(100);

    if (m_data->m_dCollisionRadius == 0.0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"collison radius invalid... file[%s], line[%d], "
        //     L"account[%s], char[%s]",
        //     L"UserBroadcast.cpp",
        //     0xa57,
        //     m_sAccountName.data(),
        //     GetName())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[subclass] Account %s, character %s, has zero collision radius after parameter lookup (base class %d, class
        //     %d, race %d, sex %d). Parameter validation continues.",
        //     ::FixedUtf16ToWide(m_sAccountName).c_str(), GetName(),
        //     static_cast<int>(m_nBaseClass), static_cast<int>(classType),
        //     static_cast<int>(race), static_cast<int>(sex))
        // L2_LOG_ERROR(Log,
        //     L"[subclass] Account %s, character %s, has zero collision radius after parameter lookup (base class %d, class
        //     %d, race %d, sex %d). Parameter validation continues.",
        //     ::FixedUtf16ToWide(m_sAccountName).c_str(), GetName(),
        //     static_cast<int>(m_nBaseClass), static_cast<int>(classType),
        //     static_cast<int>(race), static_cast<int>(sex))
        L2_LOG_ERROR(
            Log,
            u"[subclass] Account %s, character %s, has zero collision radius after parameter lookup (base class %d, class "
            u"%d, race %d, sex %d); parameter validation continues.",
            ::FixedUtf16ToWide(m_sAccountName).c_str(), GetName(), static_cast<int>(m_nBaseClass),
            static_cast<int>(classType), static_cast<int>(race), static_cast<int>(sex)
        );
    }

    AcquireSkills(g_SkillAcquireDB.m_vectorClsSkill[m_data->m_nClass], true, false);
    if (m_data->m_nNoblessType == 1 || m_data->m_nNoblessType == 2) {
        static_cast<void>(NoblessSystemUtil::SetNoblessSkillToUser(CSPointer<User>(this, ObjectRefType::FunctionChain)));
    }
    ValidateHeroSkills();
    m_nDwarfCreateItemLevel = 0;
    const int maximumHennaSlots = g_PCParamDB.GetMaxHennaSlotCount(m_data->m_nClass);
    if (m_data->m_cEnableHenna != maximumHennaSlots) {
        m_data->m_cEnableHenna = static_cast<char>(maximumHennaSlots);
    }
    ValidateHenna();
    m_nSetItemClassId = 0;
    ValidateSetItem(ST_MAX);
    ValidateNewSetItem();
    ValidateShapeShiftedSetItem();
    m_inv.ValidateCarringWeight();
}

void User::OnChangeSubJobEnd(const SubJob::Event event, CVitalData vitalData) {
    // Current L2Server.exe 0x00bddc10.
    L2_GUARD_FUNCTION();

    bool initializeVitality = true;
    switch (event) {
        case SubJob::CREATE:
            BeforeAcquireSubjobSkills(true);
            AcquireSubjobSkills();
            AcquireSubjobCertSkills();
            break;
        case SubJob::CHANGE:
            BeforeAcquireSubjobSkills(true);
            AcquireSubjobCertSkills();
            initializeVitality = false;
            break;
        case SubJob::RENEW_SUBJOB:
            BeforeAcquireSubjobSkills(false);
            AcquireSubjobSkills();
            AcquireSubjobCertSkills();
            break;
        case SubJob::RENEW_DUAL_CLASS:
            break;
        case SubJob::CREATE_DUALJOB:
            BeforeAcquireSubjobSkills(true);
            AcquireSubjobSkills();
            AcquireSubjobCertSkills();
            break;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid subjob event [%d]",
            //     L"UserBroadcast.cpp",
            //     0x947,
            //     static_cast<int>(event))
            // Log.Add(
            //     LOG_ERROR,
            //     "[subclass] Unknown completion event %d for character runtime ID %u. Event-specific skill handling was
            //     skipped, while common vitality/parameter/save processing continues.", static_cast<int>(event), m_nId)
            L2_LOG_ERROR(
                Log,
                "[subclass] Unknown completion event %d for character runtime ID %u; event-specific skill handling was "
                "skipped, while common vitality/parameter/save processing continues.",
                static_cast<int>(event), m_nId
            );
            break;
    }
    if (initializeVitality) {
        InitVitality(vitalData);
    }

    ValidateSubJobParameters();
    OnRuneItemsChange();
    AcquireAllHonorsSkill();
    runtime::raw_atomic::ExchangeI32(&m_ArtifactSlotChanged, 1);
    db.RequestSaveCharacterForSubJob(this);
    m_data->m_bIsActive = true;

    if (m_data->HaveMemo(0x1a6) && m_data->RemoveMemo(0x1a6)) {
        g_QuestItemDB.DestroyQuestItem(this, 0x1a6, true);
        CLogFormatter formatter;
        formatter.WriteLog2(0x133, this, nullptr, 0x1a6, 0, 0, 0, 0, 0, 0);
        db.SendSaveCharacterInfo(this, true, false);
        db.SaveQuestInfo(this);
        SendQuestList(nullptr);
        g_PetManager.DeletePet(GetDBID(), GetServerId());
    }

    SetMaxMagicLevel();
    DespawnAllCubic(false);
    m_maxCubic = 1;
    DespawnAllSummon(true, false);

    if (CParty* const party = GetParty(); party != nullptr) {
        party->ValidateLevel();
    }
    if (m_EventMatchUser.m_byEventMatchTeamId != 0) {
        CSPointer<CEventMatch> match = m_EventMatchUser.GetEventMatch();
        if (match) {
            match->UpdateUserStatus(this, 0);
        }
    }
    if (IsOlympiadPlayer()) {
        WOlympiadMatch* const field = m_pWOlympiadUser->GetField();
        if (field != nullptr) {
            field->UpdateUserStatus(GetServerId(), 0);
        }
    }
    if (m_DuelUserInfo.IsInFightStatus()) {
        Singleton<CDuelManager>::inst().UpdateDuelPlayerStatus(this);
    }

    if (GetPledge() != nullptr) {
        AcquirePledgeSkillsAfterReload(true);
        if (m_data->m_nPledgeType == -1) {
            runtime::raw_atomic::ExchangeI32(&m_nRelationChanged, 2);
        }
    }

    m_data->m_NeedUpdatePacketBitSet.SetAllBits();
    ParamChanged(PCB_USER);
    SendCoolTime();
    ValidateParameters(VB_MAX, false);
    SetAllOverPointToDefault();
    SendETCBuffStatusInfo();
    CheckAbnormalVisualEffect();
    m_bSpelledInfoChanged = true;
    m_NeedUpdatePacketBits |= 4U;
    SendShortBuffStatusInfo();
    m_bAbnormalStatusChanged = true;
    ParamChanged(PCB_USER | PCB_CHAR_SUBJOB);
    GetController()->Stand(0x9f6);
    BroadcastSocialAction(0x84a, 0x800, 0);
    m_ShortCut.Load(false);
    SendStorageMaxCount();
    UpdateMentorMentee();
    MentorSubjobChange();
    m_nTimeActivatePledgeSkill = 0;
    runtime::raw_atomic::ExchangeI32(&m_nAbilityClass, -1);

    CUserSocket* const socket = GetSocket();
    if (socket != nullptr) {
        socket->Send("ch", 0xfe, S_EX_CLOSE_AP_LIST_WND);
        runtime::raw_atomic::ExchangeI32(&m_nChangeSubjobFlag, 0);

        if (m_data->m_nSubJobId == 0 && IsDoneQuest(0x28e2)) {
            DeleteAcquireSkills(ESAT_DUALJOB);
            DeleteAcquireSkills(ESAT_DUALJOB_SHARING);
            m_data->SetOneTimeFlag(0x28e2, false);
            db.SaveQuestInfo(this);

            CLogFormatter formatter;
            formatter.WriteLog4(
                0x1fa1, this, nullptr, nullptr, nullptr, nullptr, static_cast<int>(m_data->m_nClass), m_data->m_nLevel,
                m_inv.GetAdenaAmount(), 0, 0, 0, 0, 0, 0, 0, 0
            );
        }

        if (m_data->m_bPCCafeUser) {
            if (g_PCCafeBonusBuff) {
                const CSkillInfo* const skillInfo = g_SkillDB.GetSkillInfo(CSkillKey(0x243f, 1, 0), nullptr);
                if (skillInfo != nullptr) {
                    skillInfo->ActivateSkill(this, this, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
                }
            }

            CEvent* const baseEvent = Singleton<CEventManager>::inst().GetEvent(GAME_EVENT::ET_PCCAFE_SETTING);
            if (baseEvent != nullptr) {
                const int skillId =
                    static_cast<CEvent_PCCafe*>(baseEvent)->GetPCCafeBuffSkillBonus(CEvent_PCCafe::BT_BUFFSKILL);
                if (skillId > 0) {
                    const CSkillInfo* const skillInfo = g_SkillDB.GetSkillInfo(CSkillKey(skillId, 1, 0), nullptr);
                    if (skillInfo != nullptr) {
                        skillInfo->ActivateSkill(this, this, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
                    }
                }
            }
        }
        CheckSystemBuff(BUFF_PREMIUM_SPECIAL);
    }
}

ClassType* User::GetBaseJobClass(int classType) {
    // Current L2Server.exe 0x00bd6e28. The holes are deliberate: NPC,
    // awakened, and the newest Ertheia class IDs return null in this build.
    L2_GUARD_FUNCTION();

    if (classType >= 0 && classType <= 57) {
        return baseJobs0To57[classType];
    }
    if (classType >= 88 && classType <= 118) {
        return baseJobs88To118[classType - 88];
    }
    if (classType >= 123 && classType <= 136) {
        return baseJobs123To136[classType - 123];
    }
    if (classType >= 182 && classType <= 187) {
        return baseJobs182To187[classType - 182];
    }
    return nullptr;
}

bool User::GetJobClassLevel(const int classType, int* const level) {
    // Current L2Server.exe 0x00bd7620.
    ClassType* const baseJobs = GetBaseJobClass(classType);
    *level = 0;
    if (baseJobs == nullptr) {
        return false;
    }
    while (baseJobs[*level] != CT_NONE) {
        ++*level;
    }
    return true;
}

void User::GetBaseJobClass(std::vector<ClassType>& classes) {
    // Current L2Server.exe 0x00bdbe0c.
    L2_GUARD_FUNCTION();

    ClassType* const baseJobs = GetBaseJobClass(m_data->m_nClass);
    if (baseJobs == nullptr) {
        return;
    }
    for (int index = 0; baseJobs[index] != CT_NONE; ++index) {
        classes.push_back(baseJobs[index]);
    }
}

void User::BroadcastSocialAction(int actionId, int distance, int parameter) {
    L2_GUARD_FUNCTION();

    if (IsGMInvisible()) {
        GetSocket()->Send("cddd", 0x27, static_cast<int>(m_nId), actionId, parameter);
        return;
    }

    const FVector position = m_data->m_Pos;
    g_ptlUserMap->BroadcastToNeighborDist(
        this, 0x5000, position, distance, "cddd", 0x27, static_cast<int>(m_nId), actionId, parameter
    );
}

void User::GatherRelationChanged() {
    L2_GUARD_FUNCTION();
    class TLMFunc final : public CTLMFunc {
    public:
        TLMFunc(User* source, CPacketHelperEx<20480>* packet) : source_(source), packet_(packet) {}

        bool CanDo(CObject* object, double) override {
            User* const target = object->CastUser();
            if (target != source_ && !target->IsDeleting() && target->m_Deleted == 0 && !source_->IsDeleting() &&
                source_->m_Deleted == 0) {
                const int relation = target->GetRelationTo(source_);
                const bool enemy = target->QueryEnemy(relation);
                packet_->Assemble(
                    "ddcdc", static_cast<int>(target->m_nId), relation, enemy, target->CalcCriminalRate(),
                    static_cast<int>(target->GetGuilty())
                );
                if (packet_->GetLength() > 0x4f9c) {
                    SendPacket();
                    packet_->Reset();
                }
            }
            return true;
        }

        void Finish() override { SendPacket(); }

    private:
        void SendPacket() {
            CUserSocket* const socket = source_->GetSocket();
            char* const bytes = packet_->GetPacket();
            const int length = packet_->GetLength();
            const int count = packet_->GetAssembledCount();
            socket->Send("cchb", 0xce, 4, count, length, bytes);
        }

        User* source_;
        CPacketHelperEx<20480>* packet_;
    };


    if (GetSocket() == nullptr || m_pPolymorphNPC != nullptr) {
        return;
    }

    const FVector position = GetPos();
    CTLMFilterRect filter(static_cast<int>(GetInZoneID()), position, 2048.0);
    CPacketHelperEx<20480> packet;
    TLMFunc function(this, &packet);
    FOR_ALL_NEIGHBOR_USER_RECT(&filter, &function);
}

void User::GatherSpelledInfoChanged() {
    // Current L2Server.exe 0x00bdad88.
    L2_GUARD_FUNCTION();

    CParty* const party = GetParty();
    if (party == nullptr) {
        return;
    }

    std::vector<User*> members;
    party->GetAllMember(members);
    const int memberCount = static_cast<int>(members.size());
    for (int index = 0; index < memberCount; ++index) {
        User* const member = runtime::CheckedVectorAt(members, static_cast<std::size_t>(index));
        if (member->m_nId == m_nId) {
            continue;
        }

        std::array<char, 0x1000> buffer;
        int length = static_cast<int>(buffer.size());
        const int count = member->PrepareSpelledInfoChangedBuffer(buffer.data(), length);
        if (count != 0) {
            GetSocket()->Send("cdddb", 0xf4, 0, runtime::Int32FromBits(member->m_nId), count, length, buffer.data());
        }
    }
}

void User::SendRelationChanged(CUserSocket* socket) {
    // Current L2Server.exe 0x00bd8b0c and its local TLM visitor at
    // 0x00bd6980.
    L2_GUARD_FUNCTION();

    class TLMFunc final : public CTLMFunc, public CPacketHelperEx<20480> {
    public:
        TLMFunc(User* source, const int criminalRate, const char guilty) :
            source_(source), criminalRate_(criminalRate), guilty_(guilty) {}

        bool CanDo(CObject* object, double) override {
            User* const target = object->CastUser();
            if (target == source_ || source_->IsDeleting() || source_->m_Deleted != 0) {
                return true;
            }
            if (target->IsPC() && (target->IsDeleting() || target->m_Deleted != 0)) {
                return true;
            }

            const int relation = source_->GetRelationTo(target);
            const bool enemy = source_->QueryEnemy(relation);
            CUserSocket* const targetSocket = target->GetSocket();
            if (!source_->m_PledgeGameUser.IsPlayer()) {
                targetSocket->Send(
                    "ccddcdc", 0xce, 2, static_cast<int>(source_->m_nId), relation, enemy, criminalRate_,
                    static_cast<int>(guilty_)
                );
            } else {
                targetSocket->Send("ccd", 0xce, 1, static_cast<int>(source_->m_nId));
            }
            return true;
        }

    private:
        User* source_;
        int criminalRate_;
        char guilty_;
    };


    if (m_pPolymorphNPC != nullptr) {
        return;
    }

    const int criminalRate = CalcCriminalRate();
    const char guilty = GetGuilty();
    if (socket == nullptr) {
        const FVector position = GetPos();
        CWorldContainer* const container = CContainerDB::Inst().GetWorldContainer(
            runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
            runtime::Win64CvttDoubleToInt32(position.z)
        );
        if (container != nullptr) {
            CTLMFilterContainer filter(static_cast<int>(GetInZoneID()), position, container);
            TLMFunc function(this, criminalRate, guilty);
            static_cast<void>(FOR_ALL_NEIGHBOR_CONTAINER(filter, function));
        }
        return;
    }

    User* const target = socket->GetUser();
    int relation = 0;
    bool enemy = false;
    if (target != nullptr) {
        relation = GetRelationTo(target);
        enemy = QueryEnemy(relation);
    }

    char packet[0x32]{};
    int length = 0;
    int type = 0;
    if (!m_PledgeGameUser.IsPlayer()) {
        length = ::Assemble(
            packet, sizeof(packet), "ddcdc", static_cast<int>(m_nId), relation, enemy, criminalRate, static_cast<int>(guilty)
        );
        type = 2;
    } else {
        length = ::Assemble(packet, sizeof(packet), "d", static_cast<int>(m_nId));
        type = 1;
    }
    socket->Send("ccb", 0xce, type, length, packet);
}

void User::SendSpelledInfoChanged() {
    // Current L2Server.exe 0x00bdaf3c.
    L2_GUARD_FUNCTION();

    std::array<char, 0x1000> buffer;
    int length = static_cast<int>(buffer.size());
    const int count = PrepareSpelledInfoChangedBuffer(buffer.data(), length);

    if (m_EventMatchUser.m_byEventMatchTeamId != 0) {
        CSPointer<CEventMatch> eventMatch = m_EventMatchUser.GetEventMatch();
        if (eventMatch) {
            eventMatch->Broadcast2Observer(
                "chdddb", 0xfe, 4, m_EventMatchUser.m_nEventMatchId, runtime::Int32FromBits(m_nId), count, length,
                buffer.data()
            );
        }
    }

    if (IsOlympiadPlayer()) {
        if (WOlympiadMatch* const field = m_pWOlympiadUser->GetField()) {
            field->UpdateSpelledInfo(GetServerId());
        }
    }

    if (CParty* const party = GetParty()) {
        std::vector<User*> members;
        party->GetAllMember(members);
        for (User* const member : members) {
            if (member->m_nId != m_nId) {
                member->GetSocket()->Send("cdddb", 0xf4, 0, runtime::Int32FromBits(m_nId), count, length, buffer.data());
            }
        }
    }
}

void User::SendExSpelledInfoChanged() {
    // Current L2Server.exe 0x00bdb198.
    L2_GUARD_FUNCTION();

    std::array<char, 0x1000> retainedBuffer;
    std::array<char, 0x1000> deletedBuffer;
    int retainedLength = static_cast<int>(retainedBuffer.size());
    int deletedLength = static_cast<int>(deletedBuffer.size());
    int retainedCount = 0;
    int deletedCount = 0;
    if (!SpelledInfoChangedBuffer(
            retainedBuffer.data(), retainedLength, retainedCount, deletedBuffer.data(), deletedLength, deletedCount
        )) {
        return;
    }

    if (m_EventMatchUser.m_byEventMatchTeamId != 0) {
        CSPointer<CEventMatch> eventMatch = m_EventMatchUser.GetEventMatch();
        if (eventMatch) {
            std::array<char, 0x1000> buffer;
            int length = static_cast<int>(buffer.size());
            const int count = PrepareSpelledInfoChangedBuffer(buffer.data(), length);
            eventMatch->Broadcast2Observer(
                "chdddb", 0xfe, 4, m_EventMatchUser.m_nEventMatchId, runtime::Int32FromBits(m_nId), count, length,
                buffer.data()
            );
        }
    }

    if (IsOlympiadPlayer()) {
        if (WOlympiadMatch* const field = m_pWOlympiadUser->GetField()) {
            field->UpdateSpelledInfo(GetServerId());
        }
    }

    CParty* const party = GetParty();
    if (party == nullptr || (deletedCount == 0 && retainedCount == 0)) {
        return;
    }

    std::vector<User*> members;
    party->GetAllMember(members);
    for (User* const member : members) {
        if (member->m_nId != m_nId) {
            member->GetSocket()->Send(
                "chdddbdb", 0xfe, 0x15a, 0, runtime::Int32FromBits(m_nId), deletedCount, deletedLength,
                deletedBuffer.data(), retainedCount, retainedLength, retainedBuffer.data()
            );
        }
    }
}

void User::SendUserInfo(CUserSocket* socket, CVariableBitSet* updateBits) {
    // Current L2Server.exe 0x00bd8d94.
    L2_GUARD_FUNCTION();

    if (!m_data->m_bIsActive) {
        return;
    }
    if (m_nPreviewTimeStamp > 0 &&
        runtime::crt::Time32Now() <
            runtime::Int32FromBits(static_cast<std::uint32_t>(m_nPreviewTimeStamp) + UINT32_C(10))) {
        return;
    }

    if (!FloatChecker::CheckValidation(m_data->m_dHP) || !FloatChecker::CheckValidation(m_data->m_dCP)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLog] HP(%.5f(%p)/%.5f(%p)) "
        //     L"CP(%.5f(%p)/%.5f(%p))",
        //     m_data->m_dHP,
        //     UserBroadcastDoubleBitsAsPointer(m_data->m_dHP),
        //     m_data->m_dBaseHP,
        //     UserBroadcastDoubleBitsAsPointer(m_data->m_dBaseHP),
        //     m_data->m_dCP,
        //     UserBroadcastDoubleBitsAsPointer(m_data->m_dCP),
        //     m_data->m_dBaseCP,
        //     UserBroadcastDoubleBitsAsPointer(m_data->m_dBaseCP))
        // Log.Add(
        //     LOG_ERROR,
        //     "[character state] HP/CP validation failed during user-info preparation for character runtime ID %u. Observed
        //     HP %.17g (bits %p), base HP %.17g (bits %p), CP %.17g (bits %p), base CP %.17g (bits %p). Requesting optional
        //     packet history and continuing user-info processing.", m_nId, m_data->m_dHP,
        //     UserBroadcastDoubleBitsAsPointer(m_data->m_dHP), m_data->m_dBaseHP,
        //     UserBroadcastDoubleBitsAsPointer(m_data->m_dBaseHP), m_data->m_dCP,
        //     UserBroadcastDoubleBitsAsPointer(m_data->m_dCP), m_data->m_dBaseCP,
        //     UserBroadcastDoubleBitsAsPointer(m_data->m_dBaseCP))
        L2_LOG_ERROR(
            Log,
            "[character state] HP/CP validation failed during user-info preparation for character runtime ID %u. Observed "
            "HP %.17g (bits %p), base HP %.17g (bits %p), CP %.17g (bits %p), base CP %.17g (bits %p); requesting optional "
            "packet history and continuing user-info processing.",
            m_nId, m_data->m_dHP, UserBroadcastDoubleBitsAsPointer(m_data->m_dHP), m_data->m_dBaseHP,
            UserBroadcastDoubleBitsAsPointer(m_data->m_dBaseHP), m_data->m_dCP,
            UserBroadcastDoubleBitsAsPointer(m_data->m_dCP), m_data->m_dBaseCP,
            UserBroadcastDoubleBitsAsPointer(m_data->m_dBaseCP)
        );
        GetSocket()->ReportPacketLog(false);
    }

    CVariableBitSet& updates = m_data->m_NeedUpdatePacketBitSet;
    const std::u16string userName = std::u16string(GetName());
    const int race = static_cast<int>(m_data->m_Race);
    const int sex = static_cast<int>(m_data->m_Sex);
    const int originalClass = static_cast<int>(m_nBaseClass);
    const int currentClass = static_cast<int>(m_data->m_nClass);
    const int level = m_data->m_nLevel;
    const std::int64_t experience = m_data->m_nExp;
    double experienceRatio = 0.0;
    if (level > 0) {
        experienceRatio = static_cast<double>(experience - ExpByLevel[level - 1]) /
            static_cast<double>(ExpByLevel[level] - ExpByLevel[level - 1]);
    }

    const std::array<int, 8> stats{{
        m_data->m_Stat.m_N[1],
        m_data->m_Stat.m_N[4],
        m_data->m_Stat.m_N[2],
        m_data->m_Stat.m_N[0],
        m_data->m_Stat.m_N[5],
        m_data->m_Stat.m_N[3],
        m_data->m_Stat.m_N[6],
        m_data->m_Stat.m_N[7],
    }};
    const int baseHp = runtime::Win64CvttDoubleToInt32(m_data->m_dBaseHP);
    const int currentHp = runtime::Win64CvttDoubleToInt32(m_data->m_dHP);
    const int baseMp = runtime::Win64CvttDoubleToInt32(m_data->m_dBaseMP);
    const int currentMp = runtime::Win64CvttDoubleToInt32(m_data->m_dMP);
    const int baseCp = runtime::Win64CvttDoubleToInt32(m_data->m_dBaseCP);
    const int currentCp = runtime::Win64CvttDoubleToInt32(m_data->m_dCP);
    const std::int64_t sp = m_data->m_nSP;
    const int inventoryWeight = m_inv.GetCarryingWeight();
    const int maximumWeight = runtime::Win64CvttDoubleToInt32(m_data->m_dCarryWeight);

    const int face = GetFaceForBroadcast();
    const int hairShape = GetHairShapeForBroadcast();
    const int hairColor = m_data->m_nHairColorEx != 0 ? m_data->m_nHairColorEx : m_data->m_nHairColor;
    const int rideClassId = m_data->m_rideClassID;
    const int privateStoreType = GetPrivateStoreType();

    const int physicalAttack = runtime::Win64CvttDoubleToInt32(m_data->m_dParam[VALIDATE::PATTACK]);
    const int physicalDefence = runtime::Win64CvttDoubleToInt32(m_data->m_dParam[VALIDATE::PDEFEND]);
    const int magicalAttack = runtime::Win64CvttDoubleToInt32(m_data->m_dParam[VALIDATE::MATTACK]);
    const int magicalDefence = runtime::Win64CvttDoubleToInt32(m_data->m_dParam[VALIDATE::MDEFEND]);
    const int physicalAttackSpeed = runtime::Win64CvttDoubleToInt32(m_data->m_dParam[VALIDATE::PATTACKSPEED]);
    const int magicalCastingSpeed = runtime::Win64CvttDoubleToInt32(m_data->m_dParam[VALIDATE::MATTACKSPEED] * 333.0);
    const int physicalCastingSpeed = runtime::Win64CvttDoubleToInt32(m_data->m_dParam[VALIDATE::PSKILLSPEED] * 333.0);
    const int physicalCriticalRate = CriticalRateForInfo(VALIDATE::PCRITICAL);
    const int physicalHitRate = runtime::Win64CvttDoubleToInt32(m_data->m_dParam[VALIDATE::PHIT]);
    const int physicalAvoidRate = runtime::Win64CvttDoubleToInt32(m_data->m_dParam[VALIDATE::PAVOID]);
    const int magicalHitRate = runtime::Win64CvttDoubleToInt32(m_data->m_dParam[VALIDATE::MHIT]);
    const int magicalAvoidRate = runtime::Win64CvttDoubleToInt32(m_data->m_dParam[VALIDATE::MAVOID]);
    const int magicalCriticalRate = CriticalRateForInfo(VALIDATE::MCRITICAL);

    std::array<int, 8> originalSpeeds{};
    SetOrgSpeed(
        originalSpeeds[0], originalSpeeds[1], originalSpeeds[2], originalSpeeds[3], originalSpeeds[4], originalSpeeds[5],
        originalSpeeds[6], originalSpeeds[7]
    );
    for (std::size_t index = 0; index < originalSpeeds.size(); ++index) {
        updates.SetIntValue_IfModifed(m_nOrgSpeed[index], originalSpeeds[index], 0x3d);
    }

    const bool canCrystallize = m_CrystallizeLevel > 0;
    updates.SetDoubleValue_IfModifed(m_dAttackSpeedModifier, GetAttackModifier(), 0x3e);
    updates.SetDoubleValue_IfModifed(m_dMoveSpeedModifier, GetMoveModifier(), 0x3f);
    if (!IsRiding()) {
        updates.SetDoubleValue_IfModifed(m_dCollisionRadius, m_data->m_dCollisionRadius, 0x1b);
        updates.SetDoubleValue_IfModifed(m_dCollisionHeight, m_data->m_dCollisionHeight, 0x1c);
    } else {
        updates.SetDoubleValue_IfModifed(m_dCollisionRadius, m_pRidingInfo->Radius(), 0x1b);
        updates.SetDoubleValue_IfModifed(m_dCollisionHeight, m_pRidingInfo->Height(), 0x1c);
    }

    const FVector sharedPosition(m_data->m_Pos);
    updates.SetFVectorValue_IfModifed(m_MyPos, sharedPosition, 0x40);

    int vehicleId = 0;
    int currentVehicleId = 0;
    if (IsInVehicle()) {
        currentVehicleId = CContainerDB::Inst().GetVehicleID(m_data->container_index);
    } else if (IsInAirShip()) {
        currentVehicleId = runtime::Int32FromBits(GetMyAirShipID());
    } else if (IsInShuttle()) {
        currentVehicleId = runtime::Int32FromBits(GetMyShuttleID());
    }
    updates.SetIntValue_IfModifed(vehicleId, currentVehicleId, 0x41);

    const FVector informingPosition = vehicleId > 0 ? GetRelPos() : m_MyPos;
    updates.SetFVectorValue_IfModifed(m_InformingPos, informingPosition, 0x42);

    const int pledgeSid = runtime::Int32FromBits(m_nPledgeSid);
    const int pledgeEmblemId = runtime::Int32FromBits(m_nPledgeEmblemDbId);
    updates.SetIntValue_IfModifed(m_nMyStatus, GetMyStatus(), 0x43);
    updates.SetShortValue_IfModifed(
        m_sBonusCount, runtime::Int16FromBits(static_cast<std::uint16_t>(m_nBonusCount)), 0x21
    );
    updates.SetIntValue_IfModifed(m_nVitalPoint, GetVitalPoint(), 0x22);
    updates.SetIntValue_IfModifed(m_nSocialClass, GetSocialClass(), 0x23);

    std::array<int, ST_MAX> itemIds{};
    std::array<int, ST_MAX> itemClassIds{};
    std::array<ItemOptionKey, ST_MAX> optionKeys{};
    [[maybe_unused]] std::array<int, ST_MAX> shapeShiftedClassIds{};
    CItemFactory& itemFactory = CItemFactory::Inst();
    for (int slot = 0; slot < ST_MAX; ++slot) {
        CSPointer<CItem> item = itemFactory.FindObjectSP(m_data->m_nSlotIndex[slot]);
        if (!item) {
            continue;
        }
        const std::size_t index = static_cast<std::size_t>(slot);
        itemIds[index] = static_cast<int>(item->m_nId);
        itemClassIds[index] = item->m_nClassId;
        optionKeys[index] = item->m_itemOption.itemOptionKey;
        shapeShiftedClassIds[index] = item->m_data->GetShapeShiftedClassId();
    }

    if (IsInAirShip()) {
        CAirShip* const airShip = g_AirShipSmartIdManager.GetObjectW(GetMyAirShipID());
        if (airShip != nullptr && airShip->IsDriver(this)) {
            itemClassIds[ST_RHAND] = airShip->m_const->GetSteeringClassId();
        }
    }

    const int pkCount = runtime::Int32FromBits(
        static_cast<std::uint32_t>(m_data->m_nPKCount) - static_cast<std::uint32_t>(m_data->m_nPKPardonCount)
    );
    const int duelCount = m_data->m_nDuelCount;
    static_cast<void>(GetInventoryLimit());

    std::array<char, 128> cubicPacket{};
    int cubicLength = 0;
    {
        L2_GUARD(m_spriteLock);
        for (const auto& cubic : m_cubicMap) {
            const int assembled = ::Assemble(
                cubicPacket.data() + cubicLength, static_cast<int>(cubicPacket.size()) - cubicLength, "h",
                static_cast<int>(static_cast<std::int16_t>(cubic.second.first->m_classID))
            );
            if (assembled > 0) {
                cubicLength += assembled;
            }
        }
    }

    updates.SetIntValue_IfModifed(m_nCriminalRate, CalcCriminalRate(), 0x45);
    updates.SetIntValue_IfModifed(m_pledgePower, runtime::Int32FromBits(GetPledgePowerByGrade()), 0x46);
    {
        bool cachedTeam = m_cEventMatchTeamID != 0;
        updates.SetBoolValue_IfModifed(cachedTeam, GetEventMatchTeamId() != 0, 0x47);
        m_cEventMatchTeamID = static_cast<char>(cachedTeam);
    }

    CPacketHelperEx<20480> abnormalVisualEffects;
    MakeAbnormalVisualEffectPacket(abnormalVisualEffects);

    if (socket == nullptr) {
        CUserSocket* const destination = GetSocket();
        SendUserInfo2(false, destination);
        if (updates.IsSet(
                0x10, 0x0f, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX
            )) {
            SendInvenCarryingWeight(destination);
        }
        if (updates.IsSet(
                0x49, 0x60, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX
            )) {
            SendEquipSlotInfo(destination);
        }
        if (updates.IsSet(
                0x4a, 0x44, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX
            )) {
            SendCubicInfo(destination);
        }
        if (updates.IsSet(
                0x4b, 0x51, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX
            )) {
            SendAbnormalVisualEffectInfo(destination);
        }
        if (updates.IsSet(0x4c)) {
            SendFishingInfo(destination);
        }
        if (updateBits != nullptr) {
            *updateBits |= updates;
        }
        updates.Clear();

        SendPrivateStoreMessage(GetSocket(), PSMT_NO_BROADCAST);
        if (CParty* const party = GetParty(); party != nullptr) {
            party->UpdateWindowPacket(this, 0, 0, 0, 0);
            party->SendTacticalSignOnJoinParty(*this);
        }
        if (m_EventMatchUser.m_byEventMatchTeamId != 0) {
            CSPointer<CEventMatch> match = m_EventMatchUser.GetEventMatch();
            if (match) {
                match->UpdateUserStatus(this, 0);
            }
        }
        if (IsOlympiadPlayer()) {
            WOlympiadMatch* const field = m_pWOlympiadUser->GetField();
            if (field != nullptr) {
                field->UpdateUserStatus(GetServerId(), 0);
            }
        }
        if (m_DuelUserInfo.IsInFightStatus()) {
            Singleton<CDuelManager>::inst().UpdateDuelPlayerStatus(this);
        }
        return;
    }

    const auto signedByte = [](const auto value) {
        return static_cast<int>(static_cast<std::int8_t>(static_cast<std::uint8_t>(value)));
    };
    const int packetX = runtime::Win64CvttDoubleToInt32(m_MyPos.x);
    const int packetY = runtime::Win64CvttDoubleToInt32(m_MyPos.y);
    const int packetZ = runtime::Win64CvttDoubleToInt32(m_MyPos.z);
    const int gmBonusCount = runtime::Int16FromBits(static_cast<std::uint16_t>(m_nBonusCount));
    const int gmMyStatus = m_nMyStatus;
    const int lectureMark = signedByte(m_cLectureMark);
    const int vitalityPoint = GetVitalPoint();
    const int socialClass = GetSocialClass();
    const int eventMatchTeamId = signedByte(m_cEventMatchTeamID);
    const int enchant = static_cast<int>(m_snEnchant);
    const int voteCount = runtime::Int16FromBits(static_cast<std::uint16_t>(m_nVoteCount));
    const int pledgeMaster = static_cast<int>((static_cast<std::uint32_t>(gmMyStatus) >> 6U) & 1U);
    const int guilty = static_cast<int>(GetGuilty());
    const int attackRange = GetAttackRange();

    socket->Send(
        kGmViewCharacterInfoFormat, 0x95, packetX, packetY, packetZ, vehicleId, static_cast<int>(m_nId), userName.c_str(),
        race, sex, originalClass, level, experience, experienceRatio, stats[0], stats[1], stats[2], stats[3], stats[4],
        stats[5], stats[6], stats[7], baseHp, currentHp, baseMp, currentMp, sp, inventoryWeight, maximumWeight, attackRange,
        itemIds[0], itemIds[1], itemIds[2], itemIds[3], itemIds[4], itemIds[5], itemIds[6], itemIds[7], itemIds[8],
        itemIds[9], itemIds[10], itemIds[11], itemIds[12], itemIds[13], itemIds[14], itemIds[15], itemIds[16], itemIds[17],
        itemIds[18], itemIds[19], itemIds[20], itemIds[21], itemIds[22], itemIds[23], itemIds[24], itemIds[25], itemIds[26],
        itemIds[27], itemIds[28], itemIds[29], itemIds[30], itemIds[31], itemIds[32], itemIds[33], itemIds[34], itemIds[35],
        itemIds[36], itemIds[37], itemIds[38], itemIds[39], itemIds[40], itemIds[41], itemIds[42], itemIds[43], itemIds[44],
        itemIds[45], itemIds[46], itemIds[47], itemIds[48], itemIds[49], itemIds[50], itemIds[51], itemIds[52], itemIds[53],
        itemIds[54], itemIds[55], itemIds[56], itemIds[57], itemIds[58], itemIds[59], itemClassIds[0], itemClassIds[1],
        itemClassIds[2], itemClassIds[3], itemClassIds[4], itemClassIds[5], itemClassIds[6], itemClassIds[7],
        itemClassIds[8], itemClassIds[9], itemClassIds[10], itemClassIds[11], itemClassIds[12], itemClassIds[13],
        itemClassIds[14], itemClassIds[15], itemClassIds[16], itemClassIds[17], itemClassIds[18], itemClassIds[19],
        itemClassIds[20], itemClassIds[21], itemClassIds[22], itemClassIds[23], itemClassIds[24], itemClassIds[25],
        itemClassIds[26], itemClassIds[27], itemClassIds[28], itemClassIds[29], itemClassIds[30], itemClassIds[31],
        itemClassIds[32], itemClassIds[33], itemClassIds[34], itemClassIds[35], itemClassIds[36], itemClassIds[37],
        itemClassIds[38], itemClassIds[39], itemClassIds[40], itemClassIds[41], itemClassIds[42], itemClassIds[43],
        itemClassIds[44], itemClassIds[45], itemClassIds[46], itemClassIds[47], itemClassIds[48], itemClassIds[49],
        itemClassIds[50], itemClassIds[51], itemClassIds[52], itemClassIds[53], itemClassIds[54], itemClassIds[55],
        itemClassIds[56], itemClassIds[57], itemClassIds[58], itemClassIds[59], optionKeys[0].variationOption1,
        optionKeys[0].variationOption2, optionKeys[1].variationOption1, optionKeys[1].variationOption2,
        optionKeys[2].variationOption1, optionKeys[2].variationOption2, optionKeys[3].variationOption1,
        optionKeys[3].variationOption2, optionKeys[4].variationOption1, optionKeys[4].variationOption2,
        optionKeys[5].variationOption1, optionKeys[5].variationOption2, optionKeys[6].variationOption1,
        optionKeys[6].variationOption2, optionKeys[7].variationOption1, optionKeys[7].variationOption2,
        optionKeys[8].variationOption1, optionKeys[8].variationOption2, optionKeys[9].variationOption1,
        optionKeys[9].variationOption2, optionKeys[10].variationOption1, optionKeys[10].variationOption2,
        optionKeys[11].variationOption1, optionKeys[11].variationOption2, optionKeys[12].variationOption1,
        optionKeys[12].variationOption2, optionKeys[13].variationOption1, optionKeys[13].variationOption2,
        optionKeys[14].variationOption1, optionKeys[14].variationOption2, optionKeys[15].variationOption1,
        optionKeys[15].variationOption2, optionKeys[16].variationOption1, optionKeys[16].variationOption2,
        optionKeys[17].variationOption1, optionKeys[17].variationOption2, optionKeys[18].variationOption1,
        optionKeys[18].variationOption2, optionKeys[19].variationOption1, optionKeys[19].variationOption2,
        optionKeys[20].variationOption1, optionKeys[20].variationOption2, optionKeys[21].variationOption1,
        optionKeys[21].variationOption2, optionKeys[22].variationOption1, optionKeys[22].variationOption2,
        optionKeys[23].variationOption1, optionKeys[23].variationOption2, optionKeys[24].variationOption1,
        optionKeys[24].variationOption2, optionKeys[25].variationOption1, optionKeys[25].variationOption2,
        optionKeys[26].variationOption1, optionKeys[26].variationOption2, optionKeys[27].variationOption1,
        optionKeys[27].variationOption2, optionKeys[28].variationOption1, optionKeys[28].variationOption2,
        optionKeys[29].variationOption1, optionKeys[29].variationOption2, optionKeys[30].variationOption1,
        optionKeys[30].variationOption2, optionKeys[31].variationOption1, optionKeys[31].variationOption2,
        optionKeys[32].variationOption1, optionKeys[32].variationOption2, optionKeys[33].variationOption1,
        optionKeys[33].variationOption2, optionKeys[34].variationOption1, optionKeys[34].variationOption2,
        optionKeys[35].variationOption1, optionKeys[35].variationOption2, optionKeys[36].variationOption1,
        optionKeys[36].variationOption2, optionKeys[37].variationOption1, optionKeys[37].variationOption2,
        optionKeys[38].variationOption1, optionKeys[38].variationOption2, optionKeys[39].variationOption1,
        optionKeys[39].variationOption2, optionKeys[40].variationOption1, optionKeys[40].variationOption2,
        optionKeys[41].variationOption1, optionKeys[41].variationOption2, optionKeys[42].variationOption1,
        optionKeys[42].variationOption2, optionKeys[43].variationOption1, optionKeys[43].variationOption2,
        optionKeys[44].variationOption1, optionKeys[44].variationOption2, optionKeys[45].variationOption1,
        optionKeys[45].variationOption2, optionKeys[46].variationOption1, optionKeys[46].variationOption2,
        optionKeys[47].variationOption1, optionKeys[47].variationOption2, optionKeys[48].variationOption1,
        optionKeys[48].variationOption2, optionKeys[49].variationOption1, optionKeys[49].variationOption2,
        optionKeys[50].variationOption1, optionKeys[50].variationOption2, optionKeys[51].variationOption1,
        optionKeys[51].variationOption2, optionKeys[52].variationOption1, optionKeys[52].variationOption2,
        optionKeys[53].variationOption1, optionKeys[53].variationOption2, optionKeys[54].variationOption1,
        optionKeys[54].variationOption2, optionKeys[55].variationOption1, optionKeys[55].variationOption2,
        optionKeys[56].variationOption1, optionKeys[56].variationOption2, optionKeys[57].variationOption1,
        optionKeys[57].variationOption2, optionKeys[58].variationOption1, optionKeys[58].variationOption2,
        optionKeys[59].variationOption1, optionKeys[59].variationOption2, signedByte(m_SkillMod.m_dMaxDecoSlotCount),
        signedByte(m_SkillMod.m_dMaxJewelSlotCount), signedByte(m_SkillMod.m_dMaxAgathionMainSlotCount),
        signedByte(m_SkillMod.m_dMaxAgathionSubSlotCount), signedByte(m_SkillMod.m_nActiveArtifactSlotGroup),
        static_cast<int>(m_hMinNewSetItemEnchantedEffect), physicalAttack, magicalAttack, physicalDefence, magicalDefence,
        physicalHitRate, physicalCriticalRate, physicalAttackSpeed, magicalCastingSpeed, physicalCastingSpeed,
        physicalAvoidRate, magicalAvoidRate, magicalHitRate, magicalCriticalRate, guilty, m_nCriminalRate, m_nOrgSpeed[0],
        m_nOrgSpeed[1], m_nOrgSpeed[2], m_nOrgSpeed[3], m_nOrgSpeed[4], m_nOrgSpeed[5], m_nOrgSpeed[6], m_nOrgSpeed[7],
        m_dMoveSpeedModifier, m_dAttackSpeedModifier, m_dCollisionRadius, m_dCollisionHeight, face, hairShape, hairColor,
        rideClassId, m_sNickName, pledgeSid, pledgeMaster, gmMyStatus, signedByte(m_data->m_rideType),
        signedByte(privateStoreType), static_cast<int>(canCrystallize), pkCount, duelCount, voteCount, gmBonusCount,
        currentClass, static_cast<int>(m_FootEffect), baseCp, currentCp, enchant, eventMatchTeamId, pledgeEmblemId,
        signedByte(m_data->m_nNoblessType), signedByte(m_data->m_nHeroType), socialClass, m_data->m_nPledgeType,
        static_cast<int>(m_data->m_BaseAttribute.m_nAttackType), static_cast<int>(m_data->m_BaseAttribute.m_nAttackValue),
        static_cast<int>(m_data->m_BaseAttribute.m_nDefendValue[0]),
        static_cast<int>(m_data->m_BaseAttribute.m_nDefendValue[1]),
        static_cast<int>(m_data->m_BaseAttribute.m_nDefendValue[2]),
        static_cast<int>(m_data->m_BaseAttribute.m_nDefendValue[3]),
        static_cast<int>(m_data->m_BaseAttribute.m_nDefendValue[4]),
        static_cast<int>(m_data->m_BaseAttribute.m_nDefendValue[5]), m_data->m_nPVPPoint, vitalityPoint,
        m_data->m_nRaidPointAsset, lectureMark
    );

    SendPrivateStoreMessage(socket, PSMT_NO_BROADCAST);
    SendUserHennaInfo(socket);
    SendVitalityEffectInfo(socket);
}

void User::BroadcastCharInfoUpdate(bool isPolymorph) {
    // Current L2Server.exe 0x00bddaa0 is the guarded one-call wrapper.
    L2_GUARD_FUNCTION();
    SendCharInfo_internal(nullptr, isPolymorph, false);
}

void User::SendCreateObject(User* target, bool isPolymorph) {
    // Current L2Server.exe 0x00bddb4c. This is a direct-send
    // wrapper. The fourth argument is not the ordinary broadcast route.
    L2_GUARD_FUNCTION();
    if (target != nullptr) {
        SendCharInfo_internal(target, isPolymorph, true);
    }
}

int User::GetFaceForBroadcast() {
    // Current 0x00b146cc checks equipped slots +0x11c/+0x124/+0x128.
    // The recovered name is positional: +0xb0/+0xbc carry hair style.
    if (m_bHairAccFlag &&
        (m_data->m_nSlotIndex[ST_HAIR] != -1 || m_data->m_nSlotIndex[ST_HAIR2] != -1 ||
            m_data->m_nSlotIndex[ST_HAIRALL] != -1)) {
        return m_data->m_nFace;
    }
    return m_data->m_nFaceEx != 0 ? m_data->m_nFaceEx : m_data->m_nFace;
}

int User::GetHairShapeForBroadcast() {
    // Current 0x00b14718 applies the same equipment gate to hair color.
    if (m_bHairAccFlag &&
        (m_data->m_nSlotIndex[ST_HAIR] != -1 || m_data->m_nSlotIndex[ST_HAIR2] != -1 ||
            m_data->m_nSlotIndex[ST_HAIRALL] != -1)) {
        return m_data->m_nHairShape;
    }
    return m_data->m_nHairShapeEx != 0 ? m_data->m_nHairShapeEx : m_data->m_nHairShape;
}

void User::SetOrgSpeed(
    int& groundRun,
    int& groundWalk,
    int& underwaterRun,
    int& underwaterWalk,
    int& hoverRun,
    int& hoverWalk,
    int& airRun,
    int& airWalk
) {
    // Current L2Server.exe 0x00bd64ac. The packet always starts from the PC
    // parameter table, then applies the same three whole-table overrides as
    // the original: polymorph, riding, and finally transform.
    L2_GUARD_FUNCTION();

    const auto pcSpeed = [this](MoveType move, EnvType environment) {
        return runtime::Win64CvttDoubleToInt32(
            g_PCParamDB.GetOrgSpeed(m_data->m_Sex, m_data->m_nClass, move, environment, m_data->m_Race)
        );
    };

    groundRun = pcSpeed(MT_FAST, ENV_GROUND);
    groundWalk = pcSpeed(MT_SLOW, ENV_GROUND);
    underwaterRun = pcSpeed(MT_FAST, ENV_UNDERWATER);
    underwaterWalk = pcSpeed(MT_SLOW, ENV_UNDERWATER);
    hoverRun = pcSpeed(MT_FAST, ENV_HOVER);
    hoverWalk = pcSpeed(MT_SLOW, ENV_HOVER);
    airRun = pcSpeed(MT_FAST, ENV_AIR);
    airWalk = pcSpeed(MT_SLOW, ENV_AIR);

    if (m_pPolymorphNPC != nullptr) {
        const std::int32_t (*speed)[4] = m_pPolymorphNPC->m_pClassConst->m_SpeedByME;
        groundRun = speed[1][0];
        groundWalk = speed[0][0];
        underwaterRun = speed[1][1];
        underwaterWalk = speed[0][1];
        hoverRun = speed[1][3];
        hoverWalk = speed[0][3];
        airRun = speed[1][2];
        airWalk = speed[0][2];
    }

    if (IsRiding()) {
        const PetDataSheet::Record& record = m_pRidingInfo->Record();
        groundRun = record.mSpeedOnRide[1][0];
        groundWalk = record.mSpeedOnRide[0][0];
        underwaterRun = record.mSpeedOnRide[1][1];
        underwaterWalk = record.mSpeedOnRide[0][1];
        hoverRun = record.mSpeedOnRide[1][3];
        hoverWalk = record.mSpeedOnRide[0][3];
        airRun = record.mSpeedOnRide[1][2];
        airWalk = record.mSpeedOnRide[0][2];
    }

    if (IsTransformedExceptModeChange() != nullptr) {
        const double (*speed)[4] = m_transform->m_commonData->orgSpeed;
        groundRun = runtime::Win64CvttDoubleToInt32(speed[1][0]);
        groundWalk = runtime::Win64CvttDoubleToInt32(speed[0][0]);
        underwaterRun = runtime::Win64CvttDoubleToInt32(speed[1][1]);
        underwaterWalk = runtime::Win64CvttDoubleToInt32(speed[0][1]);
        hoverRun = runtime::Win64CvttDoubleToInt32(speed[1][3]);
        hoverWalk = runtime::Win64CvttDoubleToInt32(speed[0][3]);
        airRun = runtime::Win64CvttDoubleToInt32(speed[1][2]);
        airWalk = runtime::Win64CvttDoubleToInt32(speed[0][2]);
    }
}

void User::SendCharInfo_internal(User* target, bool isPolymorph, bool directSend) {
    // Current L2Server.exe 0x00bdbfb4. This is the 108-field user
    // character-info envelope. In particular, User uses fields 71/72 for
    // cubics and 104/105 for abnormal visual effects. The superficially
    // similar droid-NPC packet uses those binary fields differently.
    L2_GUARD_FUNCTION();

    CUserSocket* const targetSocket = target == nullptr ? nullptr : target->GetSocket();
    if (IsGMInvisible() || m_data->m_TrainingState == 1) {
        return;
    }

    const std::u16string packetName = std::u16string(CastUser()->GetName2(target));
    const int race = static_cast<int>(static_cast<std::int16_t>(m_data->m_Race));
    const int sex = static_cast<int>(static_cast<std::int8_t>(m_data->m_Sex));
    const int face = GetFaceForBroadcast();
    const int hairShape = GetHairShapeForBroadcast();
    const int hairColor = m_data->m_nHairColorEx != 0 ? m_data->m_nHairColorEx : m_data->m_nHairColor;

    const int magicCastingSpeed = runtime::Win64CvttDoubleToInt32(m_data->m_dParam[VALIDATE::MATTACKSPEED] * 333.0);
    const int physicalSkillCastingSpeed =
        runtime::Win64CvttDoubleToInt32(m_data->m_dParam[VALIDATE::PSKILLSPEED] * 333.0);

    int groundRun = 0;
    int groundWalk = 0;
    int underwaterRun = 0;
    int underwaterWalk = 0;
    int hoverRun = 0;
    int hoverWalk = 0;
    int airRun = 0;
    int airWalk = 0;
    SetOrgSpeed(groundRun, groundWalk, underwaterRun, underwaterWalk, hoverRun, hoverWalk, airRun, airWalk);

    const double moveModifier = GetMoveModifier();
    const double attackModifier = GetAttackModifier();
    const bool riding = IsRiding();
    const double collisionRadius = riding ? m_pRidingInfo->Radius() : m_data->m_dCollisionRadius;
    const double collisionHeight = riding ? m_pRidingInfo->Height() : m_data->m_dCollisionHeight;

    const FVector sharedPosition(m_data->m_Pos);
    int carrierId = 0;
    if (IsInVehicle()) {
        carrierId = CContainerDB::Inst().GetVehicleID(m_data->container_index);
    } else if (IsInAirShip()) {
        carrierId = static_cast<int>(GetMyAirShipID());
    } else if (IsInShuttle()) {
        carrierId = static_cast<int>(GetMyShuttleID());
    }
    const FVector packetPosition(carrierId > 0 ? GetRelPos() : sharedPosition);

    std::array<int, ST_MAX> itemClassIds{};
    std::array<ItemOptionKey, ST_MAX> optionKeys{};
    std::array<int, ST_MAX> shapeShiftedClassIds{};
    CItemFactory& itemFactory = CItemFactory::Inst();
    for (int slot = 0; slot < ST_MAX; ++slot) {
        // The five jewellery slots are not exposed by this
        // packet in the current protocol.
        if (slot >= 1 && slot <= 5) {
            continue;
        }

        const int itemIndex = m_data->m_nSlotIndex[slot];
        CSharedItemData* const itemData = itemFactory.GetData(itemIndex);
        if (itemData == nullptr) {
            continue;
        }
        itemClassIds[static_cast<std::size_t>(slot)] = itemData->m_nClassID;
        CSPointer<CItem> item = itemFactory.FindObjectSP(itemIndex);
        if (item) {
            optionKeys[static_cast<std::size_t>(slot)] = item->m_itemOption.itemOptionKey;
            shapeShiftedClassIds[static_cast<std::size_t>(slot)] = item->m_data->GetShapeShiftedClassId();
        }
    }

    if (IsInAirShip()) {
        CAirShip* const airShip = g_AirShipSmartIdManager.GetObjectW(GetMyAirShipID());
        if (airShip != nullptr && airShip->IsDriver(this)) {
            itemClassIds[ST_RHAND] = airShip->m_const->GetSteeringClassId();
        }
    }

    const bool hideIdentity = NeedToHideIdentityInfo();
    const int guilty = hideIdentity ? 0 : static_cast<int>(GetGuilty());
    const int criminalRate = hideIdentity ? 0 : CalcCriminalRate();
    const char16_t* const nickname = hideIdentity ? u"" : m_sNickName;
    const int pledgeSid = hideIdentity ? 0 : static_cast<int>(m_nPledgeSid);
    const int pledgeCrest = hideIdentity ? 0 : static_cast<int>(m_nPledgeCrestDbId);
    const int allianceId = hideIdentity ? 0 : static_cast<int>(m_data->m_nAllianceId);
    const int allianceCrest = hideIdentity ? 0 : static_cast<int>(m_nAllianceCrestDbId);
    const int pledgeEmblem = hideIdentity ? 0 : static_cast<int>(m_nPledgeEmblemDbId);
    const int bonusCount = hideIdentity ? 0 : m_nBonusCount;
    const int socialClass = hideIdentity ? 0 : GetSocialClass();
    const int characterNameColor = hideIdentity ? 0x00ffffff : m_nCharNameColor;
    const int nicknameColor = hideIdentity ? DEFAULT_NICKNAME_COLOR : m_nNickNameColor;

    int pledgeNameValue = 0;
    if (CPledge* const pledge = GetPledge(); pledge != nullptr) {
        pledgeNameValue = pledge->GetNameValue();
    }

    CPacketHelperEx<20480> abnormalVisualEffects;
    MakeAbnormalVisualEffectPacket(abnormalVisualEffects);

    std::array<char, 128> cubicPacket{};
    int cubicLength = 0;
    int cubicCount = 0;
    {
        L2_GUARD(m_spriteLock);
        for (const auto& cubic : m_cubicMap) {
            const int assembled = ::Assemble(
                cubicPacket.data() + cubicLength, static_cast<int>(cubicPacket.size()) - cubicLength, "h",
                static_cast<int>(static_cast<std::int16_t>(cubic.second.first->m_classID))
            );
            if (assembled > 0) {
                cubicLength += assembled;
            }
            ++cubicCount;
        }
    }

    const int weaponSoulShotCount = EquipedWeaponSoulShotCount();
    const int eventMatchTeamId = GetEventMatchTeamId();
    bool dead = false;
    if (m_DuelUserInfo.IsDuelDead(this)) {
        dead = true;
    } else if (m_PledgeGameUser.IsPlayer() && m_PledgeGameUser.IsAlive()) {
        // This apparently inverted condition is literal current behavior.
        dead = true;
    } else {
        dead = m_data->m_bIsAlive == 0;
    }

    if (IsCheckBot2(magicCastingSpeed, physicalSkillCastingSpeed)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[CHECK_BOT] [%s][%d] S_CHAR_INFO UserId = %d, "
        //     L"UserName = %s, magicCatingSpeed = % d, "
        //     L"phsicalSkillCastingSpeed = %d",
        //     L"UserBroadcast.cpp",
        //     0x5e6,
        //     static_cast<int>(m_data->m_nId),
        //     GetName(),
        //     magicCastingSpeed,
        //     physicalSkillCastingSpeed)
        // Log.Add(
        //     LOG_WARN,
        //     L"[character info] Casting-speed guard rejected shared object ID %d (%s): magic casting speed %d, physical
        //     skill casting speed %d. Publication stopped and socket closure will be requested if a socket is available.",
        //     static_cast<int>(m_data->m_nId), GetName(), magicCastingSpeed, physicalSkillCastingSpeed)
        // L2_LOG_WARN(Log,
        //     L"[character info] Casting-speed guard rejected shared object ID %d (%s): magic casting speed %d, physical
        //     skill casting speed %d. Publication stopped and socket closure will be requested if a socket is available.",
        //     static_cast<int>(m_data->m_nId), GetName(), magicCastingSpeed, physicalSkillCastingSpeed)
        L2_LOG_WARN(
            Log,
            u"[character info] Casting-speed guard rejected shared object ID %d (%s): magic casting speed %d, physical "
            u"skill casting speed %d; publication stopped and socket closure will be requested if a socket is available.",
            static_cast<int>(m_data->m_nId), GetName(), magicCastingSpeed, physicalSkillCastingSpeed
        );
        CUserSocket* const socket = GetSocket();
        if (socket == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[CHECK_BOT] [%s][%d] S_CHAR_INFO Socket is NULL",
            //     L"UserBroadcast.cpp",
            //     0x5ef)
            // Log.Add(
            //     LOG_WARN,
            //     "[character info] No socket is available to close for character runtime ID %u after the casting-speed
            //     guard rejected publication.", m_nId)
            L2_LOG_WARN(
                Log,
                "[character info] No socket is available to close for character runtime ID %u after the casting-speed guard "
                "rejected publication.",
                m_nId
            );
        } else {
            socket->ForceClose(SCR_ILLEGAL_BEHAVIOR);
        }
        return;
    }

    const int morphId = GetMorphID();
    const bool packetAllowed = m_pPolymorphNPC == nullptr || morphId != 0;
    if (packetAllowed) {
        CCursedWeaponMgr& cursedWeapons = Singleton<CCursedWeaponMgr>::inst();
        const int cursedWeaponLevel = m_nCursedWeaponClassId > 0 ? cursedWeapons.GetWeaponLevel(m_nCursedWeaponClassId) : 0;
        const int transformDisplayId = morphId != 0 ? morphId : m_data->m_transformID;
        const int agathionId = m_pAgathion == nullptr ? 0 : m_pAgathion->m_npcClassID - 1000000;
        const bool fishing = m_pFishingController->m_FishingState != FISHINGS_NONE || m_pFishingController->m_bIsAutoFishing;
        const FVector fishingPosition(m_pFishingController->m_vFishingPos);
        const bool stopMode = (static_cast<std::uint32_t>(m_data->m_StopMode) & ~UINT32_C(2)) != 0;
        const bool moveMode = m_data->m_MoveMode != 0;
        CSPointer<User> self(this, ObjectRefType::FunctionChain);
        const bool showPartyWanted = CPartyRoomRegistry::inst().DoesShowPartyWantedMessage(self);

        CPacketHelperEx<20480> packet;
        packet.Assemble(
            "ccdddddShcdddddddddddddddddddcdddddddddcdddhhhhhhhhffffdddSddddccccccchbcchdddccdcccdddddchdcdddcdddddcdbccc",
            0x31, !directSend, runtime::Win64CvttDoubleToInt32(packetPosition.x),
            runtime::Win64CvttDoubleToInt32(packetPosition.y), runtime::Win64CvttDoubleToInt32(packetPosition.z),
            carrierId, static_cast<int>(m_nId), packetName.c_str(), race, sex, static_cast<int>(m_nBaseClass),
            itemClassIds[ST_UNDERWEAR], itemClassIds[ST_HEAD], itemClassIds[ST_RHAND], itemClassIds[ST_LHAND],
            itemClassIds[ST_GLOVES], itemClassIds[ST_CHEST], itemClassIds[ST_LEGS], itemClassIds[ST_FEET],
            itemClassIds[ST_BACK], itemClassIds[ST_RLHAND], itemClassIds[ST_HAIR], itemClassIds[ST_HAIR2],
            optionKeys[ST_RHAND].variationOption1, optionKeys[ST_RHAND].variationOption2,
            optionKeys[ST_LHAND].variationOption1, optionKeys[ST_LHAND].variationOption2,
            optionKeys[ST_RLHAND].variationOption1, optionKeys[ST_RLHAND].variationOption2,
            static_cast<int>(static_cast<std::int8_t>(m_hMinNewSetItemEnchantedEffect & 0xffU)),
            shapeShiftedClassIds[ST_RHAND], shapeShiftedClassIds[ST_LHAND], shapeShiftedClassIds[ST_RLHAND],
            shapeShiftedClassIds[ST_GLOVES], shapeShiftedClassIds[ST_CHEST], shapeShiftedClassIds[ST_LEGS],
            shapeShiftedClassIds[ST_FEET], shapeShiftedClassIds[ST_HAIR], shapeShiftedClassIds[ST_HAIR2], guilty,
            criminalRate, magicCastingSpeed, physicalSkillCastingSpeed,
            static_cast<int>(static_cast<std::int16_t>(groundRun)), static_cast<int>(static_cast<std::int16_t>(groundWalk)),
            static_cast<int>(static_cast<std::int16_t>(underwaterRun)),
            static_cast<int>(static_cast<std::int16_t>(underwaterWalk)),
            static_cast<int>(static_cast<std::int16_t>(hoverRun)), static_cast<int>(static_cast<std::int16_t>(hoverWalk)),
            static_cast<int>(static_cast<std::int16_t>(airRun)), static_cast<int>(static_cast<std::int16_t>(airWalk)),
            moveModifier, attackModifier, collisionRadius, collisionHeight, face, hairShape, hairColor, nickname, pledgeSid,
            pledgeCrest, allianceId, allianceCrest, static_cast<int>(stopMode), static_cast<int>(moveMode),
            static_cast<int>(m_data->m_bIsInCombatMode), static_cast<int>(dead), static_cast<int>(isPolymorph),
            static_cast<int>(static_cast<std::int8_t>(m_data->m_rideType)),
            static_cast<int>(static_cast<std::int8_t>(GetPrivateStoreType())), cubicCount, cubicLength, cubicPacket.data(),
            static_cast<int>(showPartyWanted), static_cast<int>(static_cast<std::int8_t>(m_data->m_Environment)),
            static_cast<int>(static_cast<std::int16_t>(bonusCount)), m_data->m_rideClassID,
            static_cast<int>(m_data->m_nClass), static_cast<int>(m_FootEffect),
            static_cast<int>(static_cast<std::int8_t>(weaponSoulShotCount)),
            static_cast<int>(static_cast<std::int8_t>(eventMatchTeamId)), pledgeEmblem,
            static_cast<int>(static_cast<std::int8_t>(m_data->m_nNoblessType)),
            static_cast<int>(static_cast<std::int8_t>(m_data->m_nHeroType)), static_cast<int>(fishing),
            runtime::Win64CvttDoubleToInt32(fishingPosition.x), runtime::Win64CvttDoubleToInt32(fishingPosition.y),
            runtime::Win64CvttDoubleToInt32(fishingPosition.z), characterNameColor,
            static_cast<int>(static_cast<std::uint16_t>(m_data->GetDirByInt())),
            static_cast<int>(static_cast<std::int8_t>(socialClass)),
            static_cast<int>(static_cast<std::int16_t>(m_data->m_nPledgeType)), nicknameColor,
            static_cast<int>(static_cast<std::int8_t>(cursedWeaponLevel)), pledgeNameValue, transformDisplayId, agathionId,
            static_cast<int>(KillPVPPointChecker.m_bStatus), runtime::Win64CvttDoubleToInt32(m_data->m_dCP),
            runtime::Win64CvttDoubleToInt32(m_data->m_dHP), runtime::Win64CvttDoubleToInt32(m_data->m_dBaseHP),
            runtime::Win64CvttDoubleToInt32(m_data->m_dMP), runtime::Win64CvttDoubleToInt32(m_data->m_dBaseMP),
            static_cast<int>(static_cast<std::int8_t>(m_cLectureMark)), abnormalVisualEffects.GetAssembledCount(),
            abnormalVisualEffects.GetLength(), abnormalVisualEffects.GetPacket(),
            static_cast<int>(static_cast<std::int8_t>(g_PledgeGameSystem.GetUserFlag(this))),
            static_cast<int>(static_cast<std::int8_t>(m_bHairAccFlag)),
            static_cast<int>(static_cast<std::int8_t>(m_data->GetRemainAP()))
        );

        const bool directProtocol152 = targetSocket != nullptr &&
            runtime::client_protocol::NegotiatedRevision(targetSocket) ==
                runtime::client_protocol::kNorthAmericanRevision;
        const bool mixedProtocolBroadcast = target == nullptr && runtime::client_protocol::HasNorthAmericanClients();
        CPacketHelperEx<20480> protocol152Packet;
        if (directProtocol152 || mixedProtocolBroadcast) {
            // Portable P152 compatibility extension. Protocol 152 owns the
            // older flat S_CHAR_INFO layout. The current revision-162
            // packet above has additional lanes and different field order.
            // Build a second packet from the same recovered User state and
            // select it per receiving socket below.
            // Both revisions use the common computed appearance. Its
            // positional locals face/hairShape/hairColor mean style/color/face.
            protocol152Packet.Assemble(
                "ccdddddShc", 0x31, 0, runtime::Win64CvttDoubleToInt32(packetPosition.x),
                runtime::Win64CvttDoubleToInt32(packetPosition.y), runtime::Win64CvttDoubleToInt32(packetPosition.z),
                carrierId, static_cast<int>(m_nId), packetName.c_str(), race, sex
            );
            protocol152Packet.Assemble(
                "ddddddddddddd", static_cast<int>(m_nBaseClass), itemClassIds[ST_UNDERWEAR], itemClassIds[ST_HEAD],
                itemClassIds[ST_RHAND], itemClassIds[ST_LHAND], itemClassIds[ST_GLOVES], itemClassIds[ST_CHEST],
                itemClassIds[ST_LEGS], itemClassIds[ST_FEET], itemClassIds[ST_BACK], itemClassIds[ST_RHAND],
                itemClassIds[ST_HAIR], itemClassIds[ST_HAIR2]
            );
            protocol152Packet.Assemble(
                "ddddddc", optionKeys[ST_RHAND].variationOption1, optionKeys[ST_RHAND].variationOption2,
                optionKeys[ST_LHAND].variationOption1, optionKeys[ST_LHAND].variationOption2,
                optionKeys[ST_RHAND].variationOption1, optionKeys[ST_RHAND].variationOption2,
                static_cast<int>(m_hMinNewSetItemEnchantedEffect)
            );
            protocol152Packet.Assemble(
                "ddddddddd", shapeShiftedClassIds[ST_RHAND], shapeShiftedClassIds[ST_LHAND], shapeShiftedClassIds[ST_RHAND],
                shapeShiftedClassIds[ST_GLOVES], shapeShiftedClassIds[ST_CHEST], shapeShiftedClassIds[ST_LEGS],
                shapeShiftedClassIds[ST_FEET], shapeShiftedClassIds[ST_HAIR], shapeShiftedClassIds[ST_HAIR2]
            );
            protocol152Packet.Assemble(
                // P152's four writeF lanes are eight-byte doubles. Lowercase
                // 'f' is their exact Shared::VAssemble representation.
                "cdddhhhhhhhhffffdddSdddd", guilty, criminalRate, magicCastingSpeed, physicalSkillCastingSpeed, groundRun,
                groundWalk, underwaterRun, underwaterWalk, m_data->m_moveType == CMT_FLY ? airRun : 0,
                m_data->m_moveType == CMT_FLY ? airWalk : 0, m_data->m_moveType == CMT_FLY ? airRun : 0,
                m_data->m_moveType == CMT_FLY ? airWalk : 0, moveModifier, attackModifier, collisionRadius, collisionHeight,
                face, hairShape, hairColor, nickname, pledgeSid, pledgeCrest, allianceId, allianceCrest
            );
            protocol152Packet.Assemble(
                "ccccccchb", static_cast<int>(stopMode), static_cast<int>(moveMode),
                static_cast<int>(m_data->m_bIsInCombatMode), static_cast<int>(dead), static_cast<int>(IsInvisibleBySkill()),
                static_cast<int>(m_data->m_rideType), GetPrivateStoreType(), cubicCount, cubicLength, cubicPacket.data()
            );

            CSharedItemData* const equippedWeapon = GetEquipedWeapon();
            const int weaponEnchant = equippedWeapon == nullptr ? 0 : equippedWeapon->m_nEnchanted;
            protocol152Packet.Assemble(
                "cchdddccdcccdddddchdcdddcdddddcdbccc", static_cast<int>(showPartyWanted),
                static_cast<int>(m_data->m_Environment), bonusCount, m_data->m_rideClassID,
                static_cast<int>(m_data->m_nClass), 0, riding ? 0 : weaponEnchant, eventMatchTeamId, pledgeEmblem,
                static_cast<int>(m_data->m_nNoblessType),
                // m_data->m_nHeroType != 0 ? 2 : 0,
                m_data->m_nHeroType != HERO_NONE ? 2 : 0, static_cast<int>(fishing),
                runtime::Win64CvttDoubleToInt32(fishingPosition.x), runtime::Win64CvttDoubleToInt32(fishingPosition.y),
                runtime::Win64CvttDoubleToInt32(fishingPosition.z), characterNameColor,
                m_data->GetDirByInt(), socialClass, static_cast<int>(m_data->m_nPledgeType), nicknameColor,
                cursedWeaponLevel, pledgeNameValue, transformDisplayId, agathionId, 0,
                runtime::Win64CvttDoubleToInt32(m_data->m_dCP), runtime::Win64CvttDoubleToInt32(m_data->m_dBaseHP),
                runtime::Win64CvttDoubleToInt32(m_data->m_dHP), runtime::Win64CvttDoubleToInt32(m_data->m_dBaseMP),
                runtime::Win64CvttDoubleToInt32(m_data->m_dMP), 0, abnormalVisualEffects.GetAssembledCount(),
                abnormalVisualEffects.GetLength(), abnormalVisualEffects.GetPacket(),
                static_cast<int>(g_PledgeGameSystem.GetUserFlag(this)), static_cast<int>(m_bHairAccFlag),
                static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(m_data->m_stPotentialCount.nCount1) +
                    static_cast<std::uint32_t>(m_data->m_stPotentialCount.nCount2) +
                    static_cast<std::uint32_t>(m_data->m_stPotentialCount.nCount3)
                )
            );
        }

        if (target == nullptr) {
            int receivers = 0;
            if (!mixedProtocolBroadcast) {
                // Preserve the exact current async-buffer fast path when no
                // P152 client is present in the process.
                receivers = g_ptlUserMap->BroadcastToNeighborExceptSelfWithSenderVisibility(
                    this, 0x80000, sharedPosition, "b", packet.GetLength(), packet.GetPacket()
                );
            } else {
                CWorldContainer* container = nullptr;
                if (IsInRealWorld()) {
                    container = CContainerDB::Inst().GetWorldContainer(
                        runtime::Win64CvttDoubleToInt32(sharedPosition.x),
                        runtime::Win64CvttDoubleToInt32(sharedPosition.y),
                        runtime::Win64CvttDoubleToInt32(sharedPosition.z)
                    );
                }
                if (!IsInRealWorld() || container != nullptr) {
                    CTLMFilterContainerWithSenderVisibility filter(
                        this, static_cast<int>(GetInZoneID()), sharedPosition, container
                    );
                    ProtocolAwareCharInfoSender function(static_cast<int>(m_nId), 0x80000, packet, protocol152Packet);
                    receivers = FOR_ALL_NEIGHBOR_CONTAINER(filter, function);
                }
            }
            if (g_bServerToUserPacketStatMonitor) {
                AccmulateCharInfoStat(false, receivers);
            }
        } else if (targetSocket != nullptr) {
            CPacketHelperEx<20480>& selectedPacket = directProtocol152 ? protocol152Packet : packet;
            targetSocket->Send("b", selectedPacket.GetLength(), selectedPacket.GetPacket());
        }
    }

    if (target == nullptr) {
        SendPrivateStoreMessage(GetSocket(), PSMT_BROADCAST_EXCEPT_ME);
        BroadcastTacticalSignInfo();
    } else {
        if (g_bServerToUserPacketStatMonitor) {
            // The current direct path records one attempted receiver even
            // when the polymorph gate suppresses the packet itself.
            AccmulateCharInfoStat(true, 1);
        }
        SendPrivateStoreMessage(targetSocket, PSMT_NO_BROADCAST);
        SendTacticalSignInfo(*target);
    }
}

void User::OnChangeSubJobBegin() {
    L2_GUARD_FUNCTION();

    m_data->m_bIsActive = false;
    StopMove();
    // ActionFailed(0)
    ActionFailed(AF_DEFAULT);
    Singleton<TalkChannelCache>::inst().ResetCache(Id());
}

void User::SendDominionStartEnd(bool start, CUserSocket* socket) {
    L2_GUARD_FUNCTION();

    const FVector position = m_data->m_Pos;
    if (socket == nullptr) {
        g_ptlUserMap->BroadcastToNeighborDist(
            this, 0x80000, position, 0x800, "chddddd", 0xfe, 0xa4, static_cast<int>(m_nId), start,
            m_data->m_nDominionSiegeId, m_bDominionRenamed, m_nDominionRenamedID
        );
        socket = GetSocket();
    }

    socket->Send(
        "chddddd", 0xfe, 0xa4, static_cast<int>(m_nId), start, m_data->m_nDominionSiegeId, m_bDominionRenamed,
        m_nDominionRenamedID
    );
}
