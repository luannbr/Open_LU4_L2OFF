#include "Residence.h"
#include "Agit.h"
#include "AgitDeco.h"
#include "AgitDecoDb.h"
#include "Alliance.h"
#include "AuctionAgit.h"
#include "CWorld.h"
#include "CastleParser.h"
#include "DB.h"
#include "DecoNpcData.h"
#include "Dominion.h"
#include "Door.h"
#include "Fortress.h"
#include "HolyThing.h"
#include "ItemAssembler.h"
#include "L2Packet.h"
#include "Manor.h"
#include "NPC.h"
#include "NpcServer.h"
#include "ObjectDB.h"
#include "Packet.h"
#include "Pledge.h"
#include "Portable/ProcessRuntime.h"
#include "Protocol/src/NPCProtocol.h"
#include "Protocol/src/SystemMessageIds.h"
#include "SSQ.h"
#include "Shared/src/CenteredSharedRWLock.h"
#include "Shared/src/Config.h"
#include "Shared/src/DXPacket.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/SystemMail.h"
#include "Shared/src/Territory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/net/Serialize.h"
#include "SignBoard.h"
#include "Skill.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserPacketEx.h"
#include "UserSocket.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>


CastleSiegeMusicPlayer g_CastleSiegeMusicPlayer[2];
CResidenceDB g_ResidenceDB;
CExpelTimer g_ExpelTimer;

CastleSiegeMusicPlayer::CastleSiegeMusicPlayer() {}

CastleSiegeMusicPlayer::~CastleSiegeMusicPlayer() = default;

void CastleSiegeMusicPlayer::TimerExpired(int) {
    // Current L2Server 0x00a315d0 is an intentional no-op.
    L2_GUARD_FUNCTION();
}

const wchar_t* CastleSiegeMusicPlayer::GetMusicName(const int elapsedTime) const {
    // Current L2Server 0x00a32a78. Records are consumed in publication
    // order and the last start time not greater than the query wins.
    L2_GUARD_FUNCTION();
    const wchar_t* musicName = nullptr;
    for (const CastleSiegeMusicRecord& record : m_MusicData) {
        if (elapsedTime < record.StartTime) {
            break;
        }
        musicName = record.MusicName;
    }
    return musicName;
}

CExpelTimer::CExpelTimer() {}

CExpelTimer::~CExpelTimer() = default;

void CExpelTimer::WriteLock(const wchar_t* const callerFile, const int callerLine) {
    // Current L2Server 0x00a303f4.
    L2_GUARD_FUNCTION();
    m_Lock.WriteLock(callerFile, callerLine);
}

void CExpelTimer::WriteUnlock() {
    // Current L2Server 0x00a304b0.
    L2_GUARD_FUNCTION();
    m_Lock.WriteUnlock();
}

void CExpelTimer::ReserveExpelWithLock(User* const user, const FVector position) {
    // Current L2Server 0x005966f8. Keep the three direct appends instead
    // of routing through ReserveExpelWithNoLock: that is the compiled owner.
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(m_Lock);
        m_lstCommand.push_back(1);
        m_lstUser.push_back(user);
        m_lstPos.push_back(position);
    }
}

void CExpelTimer::ReserveExpelWithNoLock(User* const user, const FVector position) {
    // Current L2Server 0x00a37cc0.
    L2_GUARD_FUNCTION();
    m_lstCommand.push_back(1);
    m_lstUser.push_back(user);
    m_lstPos.push_back(position);
}

void CExpelTimer::TimerExpired(int) {
    // Current L2Server 0x00a370b0. The command vector is not
    // consumed or erased by this body.
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(m_Lock);
        if (!m_lstUser.empty()) {
            auto user = m_lstUser.begin();
            auto position = m_lstPos.begin();
            int processed = 0;
            while (user != m_lstUser.end()) {
                if (processed >= 100) {
                    break;
                }

                User* const target = *user;
                const FVector destination = *position;
                target->StopMove();
                target->TeleportToLocation(
                    static_cast<int>(destination.x), static_cast<int>(destination.y), static_cast<int>(destination.z), 0,
                    0x10000, TeleportSourceType::CExpelTimerExpired, nullptr, 0
                );
                L2_RELEASE(target, ObjectRefType::EventRetain);

                ++processed;
                ++user;
                ++position;
            }
            m_lstUser.erase(m_lstUser.begin(), user);
            m_lstPos.erase(m_lstPos.begin(), position);
        }
    }
    AddTimer(1000, 0);
}

CResidence::CResidence() :
    m_data(nullptr),
    m_bPaused(false),
    m_bSiegeContinued(false),
    m_bMidVictory(false),
    m_nSiegePeriod(static_cast<std::uint32_t>(BP::_CASTLE_IN_WAR_TIME)),
    m_nLostTime(0),
    m_nSiegeElapsed(0),
    m_nResidenceOwnerPledgeServerId(0),
    m_bIsDominionSiege(false),
    m_nWorldId(g_nWorldId),
    m_pObserver(nullptr),
    m_nDecoFunc(0),
    m_bDecoCheck(true),
    m_pDecoVisible(nullptr),
    m_ResidenceSkills{},
    m_decoNpcJobLock(0x1e),
    m_tLastDecoNpcCheck(0) {
    // Current 00a37d80 leaves m_pszHolyThingName and every
    // explicit alignment lane untouched.
    L2_GUARD_FUNCTION();
}

CResidence::~CResidence() {
    // Current 00a37408 clears these borrowed pointers before unwinding the
    // owned container envelopes.
    L2_GUARD_FUNCTION();
    m_data = nullptr;
    m_pObserver = nullptr;
    m_pDecoVisible = nullptr;
}

// Current 00a37938. Class-id 255 is fixed by the current CRT
// registration initializer whose counter is 2013e92c.
L2_DEFINE_OBJECT_RELEASE(CResidence)

bool CResidence::IsCastle() const { return false; }

CCastle* CResidence::CastCastle() const { return nullptr; }

bool CResidence::IsExtended() const { return false; }

int CResidence::GetDominionSiegeID() const { return 0; }

bool CResidence::IsUnderDominionSiege() const { return false; }

bool CResidence::IsWorldAgit() const { return false; }

CWorldAgit* CResidence::CastWorldAgit() const { return nullptr; }

bool CResidence::IsFortress() const { return false; }

CFortress* CResidence::CastFortress() const { return nullptr; }

bool CResidence::IsDominion() const { return false; }

CDominion* CResidence::CastDominion() const { return nullptr; }

CResidence* CResidence::CastResidence() const { return const_cast<CResidence*>(this); }

bool CResidence::IsUnderSiege() const {
    // Current shared leaf 0042af00 dereferences m_data.
    return m_data->m_ResidenceState == RS_SIEGE || m_data->m_ResidenceState == RS_MID_VICTORY;
}

void CResidence::ChangeTaxRate() {
    // Current 00a3164c.
    L2_GUARD_FUNCTION();
    const int residenceType = m_data->m_ResidenceType;
    const int oldRate = m_data->m_TaxInfoData[static_cast<std::size_t>(residenceType)].m_nTaxRate;
    if (oldRate != m_data->m_nTaxRateToChange) {
        db.ReqWriteTax(this, 1, oldRate, m_data->m_nTaxRateToChange, IsCastle());
    }
}

bool CResidence::IsRegisteredForSiege(CPledge*) const { return false; }

void CResidence::MakeDoorsBreakable(const bool breakable) {
    // Current 00a32264.
    L2_GUARD_FUNCTION();
    for (CDoor* const door : m_DoorList) {
        if (!door->IsSiegeProof()) {
            door->SetBreakable(breakable);
            door->SendDoorStatus(nullptr);
        }
    }
}

void CResidence::MakeHolyThingBreakable(const bool breakable) {
    // Current 00a32444.
    L2_GUARD_FUNCTION();
    for (CHolyThing* const holyThing : m_HolyThingList) {
        holyThing->SetBreakable(breakable);
    }
}

void CResidence::MakeNPCDisable(const bool disabled) {
    // Current 00a33340.
    L2_GUARD_FUNCTION();
    {
        L2_READ_GUARD(*this);
        for (const std::uint32_t serverId : m_OwningNPCServerIdList) {
            CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(serverId);
            CNPC* const npc = creature != nullptr && creature->IsNPC() ? creature->CastNPC() : nullptr;
            if (npc != nullptr && npc->m_data != nullptr) {
                npc->m_data->m_bNotFunctionable = disabled;
            }
        }
    }
}

void CResidence::MakeDoorsOpenClose(const int state) {
    // Current 00a3235c.
    L2_GUARD_FUNCTION();
    for (CDoor* const door : m_DoorList) {
        if (state == 0) {
            door->Open(false, false);
        } else if (state == 1) {
            door->Close(false, false);
        }
    }
}

void CResidence::OnStartInBattleField(User*, FVector&, bool&) {}

void CResidence::SetBattleField(const bool enabled) {
    // Current L2Server 0x00a32b44. The target derives the
    // zone castle id only from the first battle-field territory.
    L2_GUARD_FUNCTION();
    CGeoData* const geo = CWorld::Inst().GetGeo();
    for (CTerritory* const territory : m_lstBattleFieldTerritory) {
        geo->SetField(territory, GEO_FIELD_BATTLE_RESIDENCE, enabled);
    }

    if (!m_lstBattleFieldTerritory.empty()) {
        const FVector center = (*m_lstBattleFieldTerritory.begin())->GetCenterPos();
        CGeoZone* const zone = geo->GetZone(
            ((static_cast<int>(center.x) + 0x50000) >> 15) + 10, ((static_cast<int>(center.y) + 0x40000) >> 15) + 10
        );
        if (zone != nullptr && enabled) {
            zone->castleId = m_data != nullptr ? m_data->m_nResidenceID : 0;
        }
    }
}

void CResidence::SummonMercenary(const FVector position, ResidenceGuardData* const guardData) {
    // Current L2Server 0x00a34520.
    L2_GUARD_FUNCTION();

    const std::int32_t npcId = runtime::Int32FromBits(guardData->nNpcId);
    const std::int32_t npcClassId = runtime::Int32FromBits(guardData->nNpcId + std::uint32_t{1'000'000});
    if (npcClassId < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "invalid npc id at file[%s], line[%d]",
        //     "Residence.cpp",
        //     0x256)
        // L2_LOG_ERROR(Log,
        //     "invalid npc id at file[%s], line[%d]",
        //     "Residence.cpp",
        //     0x256)
        L2_LOG_ERROR(
            Log, "Cannot summon mercenary NPC %d for residence %d: calculated NPC class ID %d is negative.", npcId,
            m_data != nullptr ? m_data->m_nResidenceID : 0, npcClassId
        );
        return;
    }

    CNPC* const npc = g_ObjectDB.CreateNPC(npcClassId, true);
    if (npc == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "There is no NPC id at file[%s], line[%d]",
        //     "Residence.cpp",
        //     0x25d)
        // L2_LOG_ERROR(Log,
        //     "There is no NPC id at file[%s], line[%d]",
        //     "Residence.cpp",
        //     0x25d)
        L2_LOG_ERROR(
            Log, "Cannot summon mercenary NPC %d for residence %d: creation returned null for class %d.", npcId,
            m_data != nullptr ? m_data->m_nResidenceID : 0, npcClassId
        );
        return;
    }

    const wchar_t* const aiName = g_ObjectDB.GetAI(npcClassId);
    if (aiName == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "AI does not specified [clsid=%d]",
        //     npc->m_nClassId)
        // L2_LOG_ERROR(Log,
        //     "AI does not specified [clsid=%d]",
        //     npc->m_nClassId)
        L2_LOG_ERROR(
            Log,
            "Cannot summon mercenary NPC %d for residence %d: class %d has no AI name; the created NPC is deleted without "
            "sending a spawn request.",
            npcId, m_data != nullptr ? m_data->m_nResidenceID : 0, npc->m_nClassId
        );
        npc->Delete();
        return;
    }

    std::array<char, 0x2000> buffer;
    int bufferLength = 0;
    int parameterCount = 0;
    for (const auto& parameter : npc->m_pClassConst->m_mapParameter) {
        const int assembled = ::Assemble(
            buffer.data() + bufferLength, static_cast<int>(buffer.size()) - bufferLength, "SS", parameter.first.c_str(),
            parameter.second.c_str()
        );
        ++parameterCount;
        if (assembled > 0) {
            bufferLength += assembled;
        }
    }

    npc->m_data->m_SummonType = NST_NOTHING;
    npc->m_data->m_BossID = 0;
    npc->m_data->m_nMasterExpRatio = 0;
    npc->m_data->m_nResidenceID = m_data != nullptr ? m_data->m_nResidenceID : 0;
    npc->m_data->m_nResidenceOwnerID = m_data != nullptr ? m_data->m_nResidenceOwnerID : 0;

    guardData->aAngle.Yaw = static_cast<std::uint16_t>(guardData->aAngle.Yaw);
    NpcServer::NPCCreateParam parameter{};
    parameter.pNPC = npc;
    parameter.pos = position;
    parameter.dir = guardData->aAngle.Yaw;
    parameter.aiName = const_cast<wchar_t*>(aiName);
    parameter.paramCount = parameterCount;
    parameter.bufLen = bufferLength;
    parameter.pBuf = buffer.data();
    g_npcserver.SendNPCCreatePacket(parameter);
    npc->OnCreated();

    db.RequestSaveMercenary(
        *this, npc->GetIndex(), npcId, position, guardData->aAngle, npc->m_data->m_dBaseHP, npc->m_data->m_dBaseMP
    );
}

bool operator<(const FVector& left, const FVector& right) {
    // Current L2Server 0x00a31808, original owner Residence.cpp.
    if (left.x < right.x) {
        return true;
    }
    if (left.x == right.x) {
        if (left.y <= right.y && right.y != left.y) {
            return true;
        }
        if (left.y == right.y && left.z <= right.z && right.z != left.z) {
            return true;
        }
    }
    return false;
}

namespace ResidenceUtil {

void SendDecoNpcSet(CSPointer<User> user, int result, int agitId, int slot, int decoCID, std::int32_t expireTime) {
    if (!user) {
        return;
    }

    CUserSocket* const socket = user->GetSocket();
    if (socket == nullptr) {
        return;
    }

    protocol::S_EX_DECO_NPC_SET packet{};
    packet.cResult = static_cast<char>(result);
    packet.nAgitCID = agitId;
    packet.cSlot = static_cast<char>(slot);
    packet.nDecoCID = decoCID;
    packet.nExpire = decoCID == 0 ? 0 : expireTime - runtime::crt::Time32Now();
    socket->Send(S_EX, S_EX_DECO_NPC_SET, packet);
}

void SendDecoNpcSetForReset(CSPointer<User> user, int result, int agitId, int slot) {
    SendDecoNpcSet(std::move(user), result, agitId, slot, 0, 0);
}

void WriteDecoNpcSetLog(
    int logId,
    int actorDbId,
    int actorAccountId,
    const FVector& position,
    int agitId,
    int pledgeDbId,
    int slot,
    int decoNpcId,
    std::int32_t expireTime,
    DecoNpcSetReason::Type reason
) {
    L2_GUARD_FUNCTION();

    CLogFormatter formatter;
    formatter.WriteLog(
        logId, actorDbId, actorAccountId, 0, 0, static_cast<std::int32_t>(position.x), static_cast<std::int32_t>(position.y),
        static_cast<std::int32_t>(position.z), u"", u"", u"", agitId, pledgeDbId, slot, decoNpcId, expireTime,
        static_cast<int>(reason), 0, 0, 0, 0, u"", u"", u"", u"", 0
    );
}

const unsigned char* DisassembleDecoNpc(const unsigned char* packet, const int count, CResidence* const residence) {
    // Current L2Server 0x00a31f48.
    L2_GUARD_FUNCTION();
    if (packet == nullptr || residence == nullptr || count <= 0) {
        return packet;
    }

    for (int index = 0; index < count; ++index) {
        int slot;
        int decoNpcId;
        std::int32_t expireTime;
        packet = ::Disassemble(packet, "ddd", &slot, &decoNpcId, &expireTime);
        CResidenceData* const data = residence->m_data;
        if (slot < 0 || data->m_nDecoNpcSlotCount <= slot) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[DECONPC]Deco Npc Slot is invalid : %d, %d, %d",
            //     data != nullptr ? data->m_nResidenceID : 0,
            //     slot,
            //     data->m_nDecoNpcSlotCount)
            // L2_LOG_ERROR(Log,
            //     L"[DECONPC]Deco Npc Slot is invalid : %d, %d, %d",
            //     data != nullptr ? data->m_nResidenceID : 0,
            //     slot,
            //     data->m_nDecoNpcSlotCount)
            L2_LOG_ERROR(
                Log,
                "Residence %d: decoration NPC entry %d of %d has invalid slot %d (slot count %d); decoding stops after this "
                "entry and earlier updates are retained.",
                data != nullptr ? data->m_nResidenceID : 0, index + 1, count, slot,
                data != nullptr ? data->m_nDecoNpcSlotCount : 0
            );
            break;
        }
        DecoNpc& decoNpc = data->m_aDecoNpc.data()[slot];
        decoNpc.nCID = decoNpcId;
        decoNpc.tExpire = expireTime;
        decoNpc.nNpcSID = 0;
    }
    return packet;
}

bool HasRightToSetDecoNpc(CResidence* residence, CSPointer<User> user) {
    // Current L2Server 0x00a33a18, original owner Residence.cpp.
    L2_GUARD_FUNCTION();
    if (residence == nullptr || residence->m_data == nullptr || !user) {
        return false;
    }
    CSPointer<CPledge> pledge(user->GetPledge(), ObjectRefType::FunctionChain);
    if (!pledge || pledge->GetDbId() != residence->m_data->m_nResidenceOwnerID) {
        return false;
    }
    if (residence->IsCastle() || residence->IsFortress()) {
        return user->HavePledgePower(PP_CONTROL_CASTLE_FUNC);
    }
    if (residence->IsWorldAgit()) {
        return user->HavePledgePower(PP_CONTROL_AGIT_FUNC);
    }

    const std::wstring residenceName = Utf16ToWide(
        std::u16string_view(residence->m_data->m_sResidenceName.data(), residence->m_data->m_sResidenceName.size())
    );
    // Log.Add(
    //     LOG_ERROR,
    //     L"[DECONPC]Never get here at %s(%d)",
    //     residenceName.c_str(),
    //     residence->m_data->m_nResidenceID)
    // L2_LOG_ERROR(Log,
    //     L"[DECONPC]Never get here at %s(%d)",
    //     residenceName.c_str(),
    //     residence->m_data->m_nResidenceID)
    L2_LOG_ERROR(
        Log,
        u"Cannot authorize decoration NPC changes for residence '%ls' (ID %d): unsupported residence kind; permission is "
        u"denied.",
        residenceName.c_str(), residence->m_data->m_nResidenceID
    );
    return false;
}

} // namespace ResidenceUtil

void CResidence::AddResidenceSkill(CPledge& pledge, const CastleType::Enum castleType) {
    // Current L2Server 0x00a33848.
    L2_GUARD_FUNCTION();
    for (const CSkillInfo* const skill : m_ResidenceSkills[static_cast<int>(castleType)]) {
        pledge.AddPledgeSkillWithNotify(static_cast<unsigned int>(skill->m_nSkillId), skill->m_nLevel, 0);
    }
    pledge.ApplyPledgeSkillsToMembers(true, false, false);
}

void CResidence::RemoveResidenceSkill(CPledge& pledge) {
    // Current L2Server 0x00a33934.
    L2_GUARD_FUNCTION();

    for (const auto& skillList : m_ResidenceSkills) {
        for (const CSkillInfo* const skill : skillList) {
            pledge.RemovePledgeSkill(skill->m_nSkillId, false);
        }
    }
    pledge.ApplyPledgeSkillsToMembers(false, true, false);
}

void CResidence::SendDecoInfo(CUserSocket* socket) {
    // Current L2Server 0x00a31494.
    L2_GUARD_FUNCTION();
    if (m_pDecoVisible != nullptr) {
        m_pDecoVisible->SendResidenceDecoVisibleInfo(socket);
    }
}

void CResidence::ValidateDeco() {
    // Current L2Server 0x00a390c0. The client depth image is filled in
    // place. SetDecoDepth is not used because it broadcasts
    // after every individual byte.
    L2_GUARD_FUNCTION();
    if (m_pDecoVisible == nullptr) {
        return;
    }

    std::uint32_t functions = 0;
    for (std::size_t index = 0; index < m_data->m_aDeco.size(); ++index) {
        const Deco& residenceDeco = m_data->m_aDeco[index];
        CResidenceDeco* const deco = residenceDeco.nId != 0
            ? CResidenceDecoDb::Inst().FindResidenceDeco(static_cast<std::uint32_t>(residenceDeco.nId))
            : nullptr;
        if (deco == nullptr) {
            m_pDecoVisible->m_aDepthForClient[index] = 0;
            continue;
        }

        switch (deco->m_nFunction) {
            case 1:
                functions |= 0x02;
                m_AgitDecoEffect.dHpBonus = deco->m_dArgument;
                break;
            case 2:
                functions |= 0x04;
                m_AgitDecoEffect.dMpBonus = deco->m_dArgument;
                break;
            case 3:
            case 4:
            case 5:
            case 8:
                functions |= 0x20;
                break;
            case 6:
                functions |= 0x40;
                m_AgitDecoEffect.dExpRestore = deco->m_dArgument;
                break;
            default:
                break;
        }

        m_pDecoVisible->m_aDepthForClient[index] = static_cast<std::uint8_t>(deco->m_nDepth);
    }
    m_nDecoFunc = functions;
}

void CResidence::DoDecoCheck(const std::uint32_t currentTime) {
    // Current L2Server 0x00a39274. Renewal is checked only on the second
    // tick of each minute and remains disabled until the CacheD reply sets
    // m_bDecoCheck again.
    L2_GUARD_FUNCTION();
    if (currentTime % 60 != 1 || !m_bDecoCheck) {
        return;
    }

    int decoTypes[32];
    int expires[32];
    std::int64_t prices[32];
    int count = 0;
    for (std::size_t index = 0; index < m_data->m_aDeco.size(); ++index) {
        Deco& installed = m_data->m_aDeco[index];
        if (installed.nExpire == 0 || installed.nExpire > currentTime) {
            continue;
        }

        CResidenceDeco* const deco = CResidenceDecoDb::Inst().FindResidenceDeco(static_cast<std::uint32_t>(installed.nId));
        if (deco == nullptr) {
            installed.nId = 0;
            installed.nExpire = 0;
            installed.nLevel = 0;
            continue;
        }

        const auto& cost = deco->m_lstCost.front();
        decoTypes[count] = static_cast<int>(index + 1);
        expires[count] = static_cast<std::int32_t>(static_cast<std::uint32_t>(cost.first) * 86400u + installed.nExpire);
        prices[count] = cost.second;
        ++count;
    }

    if (count != 0) {
        m_bDecoCheck = false;
        const int residenceId = m_data != nullptr ? m_data->m_nResidenceID : 0;
        const int pledgeId = m_data != nullptr ? static_cast<int>(m_data->m_nResidenceOwnerID) : 0;
        CRenewResidenceDeco packet(pledgeId, residenceId, count, decoTypes, expires, prices);
        db.RequestTPacketProcess(g_nWorldId, 4, static_cast<int>(sizeof(packet)), packet);
    }

    if (m_data->m_nHpStoveExpire != 0 && static_cast<std::uint32_t>(m_data->m_nHpStoveExpire) < currentTime) {
        db.RequestInstallResidenceDeco(*this, 0, 0, 0);
    }
    if (m_data->m_nMpFlameExpire != 0 && static_cast<std::uint32_t>(m_data->m_nMpFlameExpire) < currentTime) {
        db.RequestInstallResidenceDeco(*this, 1, 0, 0);
    }
    if (m_data->m_nTeleportExpire != 0 && static_cast<std::uint32_t>(m_data->m_nTeleportExpire) < currentTime) {
        db.RequestInstallResidenceDeco(*this, 3, 0, 0);
    }
}

void CResidence::DoDecoNpcCheck(const std::int32_t currentTime) {
    // Current L2Server 0x00a3963c. The transaction remains open only when
    // an asynchronous expiration packet was sent. Its DX reply owns EndJob.
    L2_GUARD_FUNCTION();
    const std::int32_t elapsed =
        static_cast<std::int32_t>(static_cast<std::uint32_t>(currentTime) - static_cast<std::uint32_t>(m_tLastDecoNpcCheck));
    if (elapsed <= 9) {
        return;
    }

    JobTransactionLong jobType = 0x3e;
    if (!m_decoNpcJobLock.BeginJob(&jobType)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC][NO_ERROR] To begin deco npc job is failed at %s : %d, %d",
        //     L"CResidence::DoDecoNpcCheck",
        //     m_data != nullptr ? m_data->m_nResidenceID : 0,
        //     static_cast<int>(jobType))
        // Noise: A decoration NPC job is already active. This periodic check defers to a later call. JobTransactionLock
        // reports an expired configured timeout itself. L2_LOG_ERROR(Log,
        //     L"[DECONPC][NO_ERROR] To begin deco npc job is failed at %s : %d, %d",
        //     L"CResidence::DoDecoNpcCheck",
        //     m_data != nullptr ? m_data->m_nResidenceID : 0,
        //     static_cast<int>(jobType))
        return;
    }

    m_tLastDecoNpcCheck = currentTime;
    for (int slot = 0; slot < 5; ++slot) {
        DecoNpc& npc = m_data->m_aDecoNpc[slot];
        if (npc.nCID == 0 || (npc.tExpire != 0 && currentTime >= npc.tExpire) || npc.nNpcSID != 0) {
            continue;
        }

        const CDecoNpcData* const data = CDecoNpcDB::Inst().FindDecoNpc(static_cast<unsigned int>(npc.nCID));
        if (data == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[DECONPC]Deco Npc Id is invalid %d, %d",
            //     m_data != nullptr ? m_data->m_nResidenceID : 0,
            //     npc.nCID)
            // L2_LOG_ERROR(Log,
            //     L"[DECONPC]Deco Npc Id is invalid %d, %d",
            //     m_data != nullptr ? m_data->m_nResidenceID : 0,
            //     npc.nCID)
            L2_LOG_ERROR(
                Log,
                "Residence %d, slot %d: decoration NPC definition %d was not found; clearing the installed ID and "
                "expiration, then checking the next slot.",
                m_data != nullptr ? m_data->m_nResidenceID : 0, slot, npc.nCID
            );
            npc.nCID = 0;
            npc.tExpire = 0;
            continue;
        }

        const char16_t* const npcName = g_ObjectDB.GetName(data->m_nNpcCID);
        if (npcName == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[DECONPC]No Npc Class name at %s : %d, %d",
            //     L"CResidence::DoDecoNpcCheck",
            //     npc.nCID,
            //     data->m_nNpcCID)
            // L2_LOG_ERROR(Log,
            //     L"[DECONPC]No Npc Class name at %s : %d, %d",
            //     L"CResidence::DoDecoNpcCheck",
            //     npc.nCID,
            //     data->m_nNpcCID)
            L2_LOG_ERROR(
                Log,
                "Residence %d, slot %d: decoration %d references NPC class %d without a name; the remaining decoration NPC "
                "check is skipped.",
                m_data != nullptr ? m_data->m_nResidenceID : 0, slot, npc.nCID, data->m_nNpcCID
            );
            if (!m_decoNpcJobLock.EndJob()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"Failed to end job at %s(%d)",
                //     L"Residence.cpp",
                //     0x98b)
                // L2_LOG_ERROR(Log,
                //     L"Failed to end job at %s(%d)",
                //     L"Residence.cpp",
                //     0x98b)
                L2_LOG_ERROR(
                    Log, "Residence %d: the decoration NPC job was already inactive when aborting the check for slot %d.",
                    m_data != nullptr ? m_data->m_nResidenceID : 0, slot
                );
            }
            return;
        }

        NpcServerUtil::SendRequestDecoNpcSpawn(
            npcName, m_data->m_aDecoNpcSlots[slot].pos, 0, m_data->m_aDecoNpcSlots[slot].nYaw, 1,
            m_data != nullptr ? m_data->m_nResidenceID : 0, slot
        );
    }

    int slots[5];
    int count = 0;
    for (int slot = 0; slot < 5; ++slot) {
        const DecoNpc& npc = m_data->m_aDecoNpc[slot];
        if (npc.nCID != 0 && npc.tExpire != 0 && npc.tExpire <= currentTime) {
            slots[count++] = slot;
        }
    }

    bool asynchronous = false;
    if (count > 0) {
        asynchronous = true;
        const int pledgeId = m_data != nullptr ? static_cast<int>(m_data->m_nResidenceOwnerID) : 0;
        const int residenceId = m_data != nullptr ? m_data->m_nResidenceID : 0;
        CExpireResidenceDecoNpc packet(pledgeId, residenceId, count, slots);
        db.RequestTPacketProcess(g_nWorldId, 0x3e, static_cast<int>(sizeof(packet)), packet);
    }

    if (!asynchronous && !m_decoNpcJobLock.EndJob()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Failed to end job at %s(%d)",
        //     L"Residence.cpp",
        //     0x98b)
        // L2_LOG_ERROR(Log,
        //     L"Failed to end job at %s(%d)",
        //     L"Residence.cpp",
        //     0x98b)
        L2_LOG_ERROR(
            Log, "Residence %d: the decoration NPC job was already inactive at the end of the synchronous check.",
            m_data != nullptr ? m_data->m_nResidenceID : 0
        );
    }
}

void CResidence::OnDecoSet() {
    // Current L2Server 0x00a3952c dereferences the first owner
    // restart point without an empty-list guard.
    L2_GUARD_FUNCTION();
    const std::uint32_t residenceId = m_data != nullptr ? static_cast<std::uint32_t>(m_data->m_nResidenceID) : 0;
    m_pDecoVisible = new CResidenceDecoVisible(residenceId, *m_OwnerRestartPointList.begin());
    ValidateDeco();
    CWorld::Inst().StaticObjectAppear(m_pDecoVisible, m_pDecoVisible->GetPos());
}

CResidenceDB::CResidenceDB() = default;
CResidenceDB::~CResidenceDB() = default;

void CResidenceDB::ResidenceGuardLoad() {
    // Current L2Server 0x00a36240 trusts every map value.
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_ResidenceIDMap) {
        entry.second->LoadGuard();
    }
}

void CResidenceDB::Load() {
    // Current L2Server 0x00a38434.
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    const wchar_t* const fileName =
        g_L2ServerType == L2ServerType::WORLDSERVER ? L"..\\Script\\castledata_world.txt" : L"..\\Script\\castledata.txt";
#else
    const wchar_t* const fileName =
        g_L2ServerType == L2ServerType::WORLDSERVER ? L"../script/castledata_world.txt" : L"../script/castledata.txt";
#endif

    CSecureStream input(fileName, true);
    CParserForResidence parser;
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from Objects file failed",
        //     L"Residence.cpp",
        //     0x768)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from Objects file failed",
        //     L"Residence.cpp",
        //     0x768)
        L2_LOG_ERROR(Log, u"Cannot read residence definitions from '%ls'; loading returns without parsing.", fileName);
        return;
    }
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForResidence::create failed",
        //     L"Residence.cpp",
        //     0x76f)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForResidence::create failed",
        //     L"Residence.cpp",
        //     0x76f)
        L2_LOG_ERROR(Log, u"Cannot create the residence-data parser for '%ls'; definitions are not parsed.", fileName);
        return;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing ResidenceData file failed after line[%d]",
        //     L"Residence.cpp",
        //     0x77b,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing ResidenceData file failed after line[%d]",
        //     L"Residence.cpp",
        //     0x77b,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, u"Parsing '%ls' failed at line %lld; previously published residence definitions are retained.", fileName,
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
}

void CResidence::IsInAllAuctions(const int pledgeId, int& residenceId, int& auctionId, int& flags) {
    // Current L2Server 0x00a34214. The first matching foreign auction
    // returns immediately. Ownership-only matches accumulate bit 1 while
    // walking the current ResidenceID map order.
    L2_GUARD_FUNCTION();
    residenceId = 0;
    auctionId = 0;
    flags = 0;

    for (const auto& entry : g_ResidenceDB.m_ResidenceIDMap) {
        CResidence* const residence = entry.second;
        if (residence->IsWorldAgit()) {
            CWorldAgit* const worldAgit = residence->CastWorldAgit();
            CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
            if (auctionAgit != nullptr && m_data->m_nResidenceID != auctionAgit->m_data->m_nResidenceID &&
                auctionAgit->IsInAuction(pledgeId)) {
                flags |= 1;
                residenceId = auctionAgit->m_data != nullptr ? auctionAgit->m_data->m_nResidenceID : 0;
                auctionId = auctionAgit->m_nAuctionId;
                return;
            }

            const int ownerPledgeId =
                residence->m_data != nullptr ? static_cast<int>(residence->m_data->m_nResidenceOwnerID) : 0;
            if (pledgeId == ownerPledgeId) {
                flags |= 2;
            }
        }
    }
}

void CResidence::CheckAttendedAuction(const int pledgeId) {
    // Current L2Server 0x00a343dc.
    L2_GUARD_FUNCTION();
    int residenceId = 0;
    int auctionId = 0;
    int flags = 0;
    CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, pledgeId);
    if (!pledge) {
        return;
    }
    User* const master = pledge->GetMaster();
    if (master == nullptr) {
        return;
    }
    IsInAllAuctions(pledgeId, residenceId, auctionId, flags);
    if (residenceId > 0 && auctionId > 0 && (flags & 1) != 0) {
        db.RequestAgitBidCancelRequest(master, residenceId, auctionId, pledgeId, 0);
    }
}

CResidenceData* CResidenceDB::InitializeResidenceData(const int residenceId) {
    // Current L2Server 0x00a31748 performs three independent
    // singleton lookups in this order.
    L2_GUARD_FUNCTION();
    CResidenceData::Inst(residenceId).m_nResidenceID = residenceId;
    CResidenceData* const result = &CResidenceData::Inst(residenceId);
    CResidenceData::Inst(residenceId).InitializeData();
    return result;
}

void CResidence::SetData(CResidenceData* const data) { m_data = data; }

void CResidence::SetName(const char16_t* const name) {
    if (m_data == nullptr) {
        return;
    }

    // The current parser uses wcscpy_s(..., 0x28, ...). Preserve its
    // destination-clear edge for an overlong/null name on the portable
    // UTF-16 carrier.
    if (name == nullptr) {
        m_data->m_sResidenceName[0] = u'\0';
        return;
    }
    const std::size_t length = std::char_traits<char16_t>::length(name);
    if (length >= m_data->m_sResidenceName.size()) {
        m_data->m_sResidenceName[0] = u'\0';
        return;
    }
    std::copy_n(name, length + 1, m_data->m_sResidenceName.begin());
}

void CResidence::SetResidenceID(const int residenceId) {
    if (m_data != nullptr) {
        m_data->m_nResidenceID = residenceId;
    }
}

void CResidence::Set(
    const int hpStove,
    const int hpStoveExpire,
    const int mpFlame,
    const int mpFlameExpire,
    const int teleportLevel,
    const int teleportExpire,
    const int hatcherId
) {
    // Current L2Server 0x00a310e0.
    L2_GUARD_FUNCTION();
    m_data->m_nHpStove = hpStove;
    m_data->m_nHpStoveExpire = hpStoveExpire;
    m_data->m_nMpFlame = mpFlame;
    m_data->m_nMpFlameExpire = mpFlameExpire;
    m_data->m_nTeleportLevel = teleportLevel;
    m_data->m_nTeleportExpire = teleportExpire;
    m_data->m_nHatcher = hatcherId;
}

void CResidence::InstallHpStove(const int level, const int expireTime) {
    // Current L2Server 0x00a311e0.
    L2_GUARD_FUNCTION();
    m_data->m_nHpStove = level;
    m_data->m_nHpStoveExpire = expireTime;
}

void CResidence::InstallMpFlame(const int level, const int expireTime) {
    // Current L2Server 0x00a31290.
    L2_GUARD_FUNCTION();
    m_data->m_nMpFlame = level;
    m_data->m_nMpFlameExpire = expireTime;
}

void CResidence::InstallHatcher(const int hatcherId) {
    // Current L2Server 0x00a313f0.
    L2_GUARD_FUNCTION();
    m_data->m_nHatcher = hatcherId;
}

void CResidence::InstallTeleport(const int level, const int expireTime) {
    // Current L2Server 0x00a31340.
    L2_GUARD_FUNCTION();
    m_data->m_nTeleportLevel = level;
    m_data->m_nTeleportExpire = expireTime;
}

void CResidence::SetResidenceGrade(const int grade) {
    // Current L2Server 0x00a31ca0.
    if (m_data != nullptr) {
        m_data->m_nResidenceGrade = grade;
    }
}

void CResidence::AddResidenceDomain(const int domainId) {
    // Current L2Server 0x00a31cb0.
    if (m_data != nullptr && m_data->m_nResidenceDomainCount < 10) {
        m_data->m_aResidenceDomains[static_cast<std::size_t>(m_data->m_nResidenceDomainCount)] = domainId;
        ++m_data->m_nResidenceDomainCount;
    }
}

void CResidence::AddDecoNpcSlot(const FVector& position, const int yaw) {
    // Current L2Server 0x00a31ce0.
    if (m_data != nullptr && m_data->m_nDecoNpcSlotCount < 5) {
        DecoNpcSlot& slot = m_data->m_aDecoNpcSlots[static_cast<std::size_t>(m_data->m_nDecoNpcSlotCount)];
        slot.pos = position;
        slot.nYaw = yaw;
        ++m_data->m_nDecoNpcSlotCount;
    }
}

void CResidenceDB::AddResidence(const char16_t* const name, CResidence* const residence) {
    // Current L2Server 0x00a38804. The same yield lock encloses both tree
    // insertions. Neither insertion overwrites an existing key.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_ResidenceMapLock);
        m_ResidenceMap.emplace(std::u16string(name), residence);
        const int residenceId = residence->m_data != nullptr ? residence->m_data->m_nResidenceID : 0;
        m_ResidenceIDMap.emplace(residenceId, residence);
    }

    if (residence->IsCastle() || residence->IsFortress()) {
        m_lstResidenceWithBattleCampZone.push_back(residence);
        return;
    }
    if (!residence->IsWorldAgit()) {
        return;
    }

    CWorldAgit* const agit = residence->CastWorldAgit();
    if (agit == nullptr) {
        return;
    }
    if (agit->IsBattleTypeAgit()) {
        m_lstResidenceWithBattleCampZone.push_back(agit);
    }
    if (agit->IsSiegeAgit()) {
        m_setSiegeAgit.insert(agit->m_data != nullptr ? agit->m_data->m_nResidenceID : 0);
    }
}

bool CResidence::IsSiegeRegistered(CPledge* pledge) {
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        return false;
    }
    for (const auto& entry : g_ResidenceDB.m_ResidenceIDMap) {
        CResidence* const residence = entry.second;
        if (residence != nullptr && residence->IsRegisteredForSiege(pledge)) {
            return true;
        }
    }
    return false;
}

bool CResidence::IsDefendingPledge(int pledgeId, int worldId) {
    L2_GUARD_FUNCTION();

    return m_data != nullptr && m_data->IsDefendingPledge(pledgeId, worldId);
}

bool CResidence::SetDecoNpc(const int agitSlot, const int npcSid) {
    // Current L2Server 0x00a36d04.
    L2_GUARD_FUNCTION();
    if (m_data == nullptr || agitSlot < 0 || agitSlot >= m_data->m_nDecoNpcSlotCount) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC]Slot No is not valid at %s : %d, %d",
        //     L"CResidence::SetDecoNpc",
        //     m_data != nullptr ? m_data->m_nResidenceID : 0,
        //     agitSlot)
        // L2_LOG_ERROR(Log,
        //     L"[DECONPC]Slot No is not valid at %s : %d, %d",
        //     L"CResidence::SetDecoNpc",
        //     m_data != nullptr ? m_data->m_nResidenceID : 0,
        //     agitSlot)
        L2_LOG_ERROR(
            Log, "Cannot register decoration NPC SID %u in residence %d, slot %d: %s (slot count %d).",
            static_cast<unsigned>(npcSid), m_data != nullptr ? m_data->m_nResidenceID : 0, agitSlot,
            m_data == nullptr ? "residence data is missing" : "slot is outside the configured range",
            m_data != nullptr ? m_data->m_nDecoNpcSlotCount : 0
        );
        return false;
    }
    if (m_data->m_aDecoNpc[agitSlot].nNpcSID != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[DECONPC]Deco Npc is already set at %s : %d, %d",
        //     L"CResidence::SetDecoNpc",
        //     m_data->m_nResidenceID,
        //     agitSlot)
        // L2_LOG_ERROR(Log,
        //     L"[DECONPC]Deco Npc is already set at %s : %d, %d",
        //     L"CResidence::SetDecoNpc",
        //     m_data->m_nResidenceID,
        //     agitSlot)
        L2_LOG_ERROR(
            Log, "Residence %d, slot %d: cannot register decoration NPC SID %u; existing SID %u is retained.",
            m_data->m_nResidenceID, agitSlot, static_cast<unsigned>(npcSid),
            static_cast<unsigned>(m_data->m_aDecoNpc[agitSlot].nNpcSID)
        );
        return false;
    }
    m_data->m_aDecoNpc[agitSlot].nNpcSID = npcSid;
    return true;
}

void CResidence::ResetDecoNpc(const int slot) {
    if (m_data == nullptr) {
        return;
    }

    // Noise: the invalid-slot branch now reports directly. An already empty slot needs no message.
    // const wchar_t* message = nullptr
    if (slot < 0 || slot >= m_data->m_nDecoNpcSlotCount) {
        // message = L"[DECONPC]Slot No is not valid at %s : %d, %d"
        L2_LOG_ERROR(
            Log, "Cannot reset decoration NPC for residence %d: slot %d is outside the configured range (slot count %d).",
            m_data->m_nResidenceID, slot, m_data->m_nDecoNpcSlotCount
        );
    } else if (m_data->m_aDecoNpc[slot].nNpcSID != 0) {
        m_data->m_aDecoNpc[slot].nNpcSID = 0;
        return;
    } else {
        // Noise: resetting an already empty decoration NPC slot is an idempotent no-op.
        // message = L"[DECONPC]Deco Npc is already removed at %s : %d, %d"
    }

    // Log.Add(
    //     LOG_ERROR,
    //     message,
    //     L"CResidence::ResetDecoNpc",
    //     m_data->m_nResidenceID,
    //     slot)
    // Noise: The invalid slot is reported in its branch above. Repeating a reset for an empty slot needs no error.
    // L2_LOG_ERROR(Log,
    //     message,
    //     L"CResidence::ResetDecoNpc",
    //     m_data->m_nResidenceID,
    //     slot)
}

bool CResidence::IsOwnerPledge(const int pledgeId) {
    L2_GUARD_FUNCTION();
    const int ownerPledgeId = static_cast<int>(m_data->m_nResidenceOwnerID);
    return ownerPledgeId != 0 && pledgeId == ownerPledgeId;
}

CPledge* CResidence::GetResidenceOwnerPledge() {
    L2_GUARD_FUNCTION();

    if (m_nResidenceOwnerPledgeServerId == 0) {
        return nullptr;
    }

    if (CThreadManager::GetThreadID() < CThreadManager::GetThreadCount()) {
        return CPledge::GetObjectW(static_cast<std::uint32_t>(m_nResidenceOwnerPledgeServerId));
    }

    const int worldId = m_data != nullptr ? m_data->m_nResidenceOwnerWorldId : 0;
    const int pledgeId = m_data != nullptr ? static_cast<int>(m_data->m_nResidenceOwnerID) : 0;
    CSPointer<CPledge> pledge = CPledge::FindPledge(worldId, pledgeId);
    return pledge.get();
}

void CResidence::SetPledge(CPledge* const pledge) {
    // Current L2Server 0x00a30978.
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        m_data->m_nResidenceOwnerID = 0;
        m_data->m_nResidenceOwnerWorldId = 0;
        m_data->m_nResidenceOwnerIndex = static_cast<std::uint32_t>(-1);
        m_data->m_ResidenceType = CastleType::CASTLE_NOTHING;
        m_data->m_nSuccessCampaign = -1;
        m_nResidenceOwnerPledgeServerId = 0;
    } else {
        m_data->m_nResidenceOwnerID = pledge->GetDbId();
        m_data->m_nResidenceOwnerWorldId = pledge->GetWorldId();
        m_data->m_nResidenceOwnerIndex = static_cast<std::uint32_t>(pledge->GetIndex());
        m_nResidenceOwnerPledgeServerId = static_cast<std::int32_t>(pledge->GetSmartID().Sid());
    }

    for (const int manorId : m_data->m_ManorIdList) {
        if (manorId > 0) {
            CManor* const manor = g_ManorDB.GetManor(manorId);
            if (manor != nullptr) {
                manor->SetPledge(pledge);
            }
        }
    }
}

void CResidence::AddManor(const int manorId) {
    // Current L2Server 0x00a30c80.
    L2_GUARD_FUNCTION();

    for (const int existingManorId : m_data->m_ManorIdList) {
        if (existingManorId == manorId) {
            // Log.Add(LOG_ERROR, L"try to add duplicated manor.")
            // L2_LOG_ERROR(Log, L"try to add duplicated manor.")
            L2_LOG_ERROR(
                Log, "Residence %d already has manor ID %d in its list; the duplicate binding is not added.",
                m_data != nullptr ? m_data->m_nResidenceID : 0, manorId
            );
            return;
        }
    }

    for (int index = 0; index < 5; ++index) {
        if (m_data->m_ManorIdList[index] != 0) {
            continue;
        }

        // The current code looks up the still-zero old lane before writing
        // manorId. Preserve the otherwise ineffective extended-castle path.
        if (IsCastle() && IsExtended()) {
            CManor* const manor = g_ManorDB.GetManor(m_data->m_ManorIdList[index]);
            if (manor != nullptr) {
                manor->m_dMaxChangeRate = 1.1;
            }
        }
        m_data->m_ManorIdList[index] = manorId;
        break;
    }
}

void CResidence::DelManor(const int manorId) {
    // Current L2Server 0x00a30dc4.
    L2_GUARD_FUNCTION();
    for (int index = 0; index < 5; ++index) {
        if (m_data->m_ManorIdList[index] == manorId) {
            m_data->m_ManorIdList[index] = 0;
            break;
        }
    }
}

bool CResidence::HaveManor(const int manorId) {
    // Current L2Server 0x00a30e8c.
    L2_GUARD_FUNCTION();
    for (int index = 0; index < 5; ++index) {
        if (m_data->m_ManorIdList[index] == manorId) {
            return true;
        }
    }
    return false;
}

CManor* CResidence::GetManor(const int manorId) {
    // Current L2Server 0x00a30f4c.
    L2_GUARD_FUNCTION();
    return HaveManor(manorId) ? g_ManorDB.GetManor(manorId) : nullptr;
}

void CResidence::SetMaxChangeRateToAllManor(const double rate) {
    // Current L2Server 0x00a31008.
    L2_GUARD_FUNCTION();
    for (const int manorId : m_data->m_ManorIdList) {
        if (manorId != 0) {
            CManor* const manor = g_ManorDB.GetManor(manorId);
            if (manor != nullptr) {
                manor->m_dMaxChangeRate = rate;
            }
        }
    }
}

void CResidence::SetNextCropInfo(
    const int npcIndex,
    const int userIndex,
    const int /*residenceId*/,
    const int manorId,
    const int cropId,
    const std::int64_t count,
    const std::int64_t price,
    const int type
) {
    // Current L2Server 0x00a348c4.
    L2_GUARD_FUNCTION();
    CSPointer<CCreature> npcCreature = CCreatureFactory::Inst().FindObjectSP(npcIndex);
    CSPointer<CCreature> userCreature = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (!npcCreature || !npcCreature->IsNPC() || !userCreature || !userCreature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid arguments in [SetNextCropInfo]")
        // L2_LOG_ERROR(Log,
        //     L"invalid arguments in [SetNextCropInfo]")
        L2_LOG_ERROR(
            Log,
            "Cannot update next crop settings for manor %d: NPC index %d or character index %d is missing or has the wrong "
            "object type.",
            manorId, npcIndex, userIndex
        );
        return;
    }

    CObject* const object = g_ObjectDB.GetObjectW(cropId, false);
    if (object == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]invalid crop(nCropId : %d, %I64d, %I64d)",
        //     "Residence.cpp",
        //     0x3df,
        //     cropId,
        //     price,
        //     count)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]invalid crop(nCropId : %d, %I64d, %I64d)",
        //     "Residence.cpp",
        //     0x3df,
        //     cropId,
        //     price,
        //     count)
        L2_LOG_ERROR(
            Log,
            "Cannot update next crop settings for manor %d, class %d: the item prototype was not found (count %lld, price "
            "%lld adena).",
            manorId, cropId, static_cast<long long>(count), static_cast<long long>(price)
        );
        userCreature->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::INVALID_CROP_PURCHASE_ITEM_INFORMATION);
        return;
    }
    if (!object->IsItem()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]invalid crop(nCropId : %d, %I64d, %I64d)",
        //     "Residence.cpp",
        //     0x3ef,
        //     cropId,
        //     price,
        //     count)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]invalid crop(nCropId : %d, %I64d, %I64d)",
        //     "Residence.cpp",
        //     0x3ef,
        //     cropId,
        //     price,
        //     count)
        L2_LOG_ERROR(
            Log,
            "Cannot update next crop settings for manor %d, class %d: the prototype is not an item (count %lld, price %lld "
            "adena).",
            manorId, cropId, static_cast<long long>(count), static_cast<long long>(price)
        );
        userCreature->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::INVALID_CROP_PURCHASE_ITEM_INFORMATION);
        return;
    }

    CItem* const item = static_cast<CItem*>(object);
    if (count != 0) {
        const std::int32_t defaultPrice = runtime::Int32FromBits(item->m_const->m_nDefaultPrice);
        const int minimumPrice = static_cast<int>(static_cast<double>(defaultPrice) * 0.6);
        const int maximumPrice = runtime::MultiplyInt32Wrapping(defaultPrice, 10);
        if (price < minimumPrice || price > maximumPrice) {
            // Log.Add(
            //     LOG_WARN,
            //     L"[%S][%d]invalid crop price(nCropId : %d, %I64d, %I64d)",
            //     "Residence.cpp",
            //     1000,
            //     cropId,
            //     price,
            //     count)
            // L2_LOG_WARN(Log,
            //     L"[%S][%d]invalid crop price(nCropId : %d, %I64d, %I64d)",
            //     "Residence.cpp",
            //     1000,
            //     cropId,
            //     price,
            //     count)
            L2_LOG_WARN(
                Log, "Rejected next crop settings for manor %d, class %d: price %lld adena is outside %d..%d (count %lld).",
                manorId, cropId, static_cast<long long>(price), minimumPrice, maximumPrice, static_cast<long long>(count)
            );
            userCreature->CastUser()->GetSocket()->SendSystemMessage(
                SystemMessageId::INVALID_CROP_PURCHASE_ITEM_INFORMATION
            );
            return;
        }
        if (type != 1 && type != 2) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]invalid procure type(nCropId : %d, %I64d, %I64d, %d)",
            //     "Residence.cpp",
            //     0x3f6,
            //     cropId,
            //     price,
            //     count,
            //     type)
            // L2_LOG_ERROR(Log,
            //     L"[%S][%d]invalid procure type(nCropId : %d, %I64d, %I64d, %d)",
            //     "Residence.cpp",
            //     0x3f6,
            //     cropId,
            //     price,
            //     count,
            //     type)
            L2_LOG_WARN(
                Log,
                "Rejected next crop settings for manor %d, class %d: reward type %d must be 1 or 2 (count %lld, price %lld "
                "adena).",
                manorId, cropId, type, static_cast<long long>(count), static_cast<long long>(price)
            );
            return;
        }
    }

    if (!HaveManor(manorId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid manor in [SetNextCropInfo] [%d]",
        //     manorId)
        // L2_LOG_ERROR(Log,
        //     L"invalid manor in [SetNextCropInfo] [%d]",
        //     manorId)
        L2_LOG_ERROR(
            Log, "Cannot update next crop settings for residence %d: manor %d is not assigned to this residence.",
            m_data != nullptr ? m_data->m_nResidenceID : 0, manorId
        );
        return;
    }
    CManor* const manor = GetManor(manorId);
    if (manor == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid manor in [SetNextCropInfo] [%d]",
        //     manorId)
        // L2_LOG_ERROR(Log,
        //     L"invalid manor in [SetNextCropInfo] [%d]",
        //     manorId)
        L2_LOG_ERROR(
            Log, "Cannot update next crop settings for residence %d: manor %d has no available manor object.",
            m_data != nullptr ? m_data->m_nResidenceID : 0, manorId
        );
        return;
    }
    if (manor->GetMaxCropCountById(cropId) < count) {
        // Log.Add(
        //     LOG_WARN,
        //     L"[%S][%d]invalid max crop count(nCropId : %d, %I64d, %I64d)",
        //     "Residence.cpp",
        //     0x402,
        //     cropId,
        //     price,
        //     count)
        // L2_LOG_WARN(Log,
        //     L"[%S][%d]invalid max crop count(nCropId : %d, %I64d, %I64d)",
        //     "Residence.cpp",
        //     0x402,
        //     cropId,
        //     price,
        //     count)
        L2_LOG_WARN(
            Log,
            "Rejected next crop settings for manor %d, class %d: count %lld exceeds the permitted maximum (price %lld "
            "adena).",
            manorId, cropId, static_cast<long long>(count), static_cast<long long>(price)
        );
        userCreature->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::INVALID_CROP_PURCHASE_ITEM_INFORMATION);
        return;
    }
    manor->SetNextProcureInfo(cropId, price, count, type);
}

void CResidence::SetNextSeedInfo(
    const int npcIndex,
    const int userIndex,
    const int /*residenceId*/,
    const int manorId,
    const int seedId,
    const std::int64_t count,
    const std::int64_t price
) {
    // Current L2Server 0x00a350dc.
    L2_GUARD_FUNCTION();
    CSPointer<CCreature> npcCreature = CCreatureFactory::Inst().FindObjectSP(npcIndex);
    CSPointer<CCreature> userCreature = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (!npcCreature || !npcCreature->IsNPC() || !userCreature || !userCreature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid arguments in [SetNextSeedInfo]")
        // L2_LOG_ERROR(Log,
        //     L"invalid arguments in [SetNextSeedInfo]")
        L2_LOG_ERROR(
            Log,
            "Cannot update next seed settings for manor %d: NPC index %d or character index %d is missing or has the wrong "
            "object type.",
            manorId, npcIndex, userIndex
        );
        return;
    }

    CObject* const object = g_ObjectDB.GetObjectW(seedId, false);
    if (object == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]invalid seed price(nSeedId : %d, %I64d, %I64d)",
        //     "Residence.cpp",
        //     0x42e,
        //     seedId,
        //     price,
        //     count)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]invalid seed price(nSeedId : %d, %I64d, %I64d)",
        //     "Residence.cpp",
        //     0x42e,
        //     seedId,
        //     price,
        //     count)
        L2_LOG_ERROR(
            Log,
            "Cannot update next seed settings for manor %d, class %d: the item prototype was not found (count %lld, price "
            "%lld adena).",
            manorId, seedId, static_cast<long long>(count), static_cast<long long>(price)
        );
        userCreature->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::INVALID_SEED_INFORMATION);
        return;
    }
    if (!object->IsItem()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]invalid seed price(nSeedId : %d, %I64d, %I64d)",
        //     "Residence.cpp",
        //     0x43e,
        //     seedId,
        //     price,
        //     count)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]invalid seed price(nSeedId : %d, %I64d, %I64d)",
        //     "Residence.cpp",
        //     0x43e,
        //     seedId,
        //     price,
        //     count)
        L2_LOG_ERROR(
            Log,
            "Cannot update next seed settings for manor %d, class %d: the prototype is not an item (count %lld, price %lld "
            "adena).",
            manorId, seedId, static_cast<long long>(count), static_cast<long long>(price)
        );
        userCreature->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::INVALID_SEED_INFORMATION);
        return;
    }

    CItem* const item = static_cast<CItem*>(object);
    if (count != 0) {
        const std::int32_t defaultPrice = runtime::Int32FromBits(item->m_const->m_nDefaultPrice);
        const int minimumPrice = static_cast<int>(static_cast<double>(defaultPrice) * 0.6);
        const int maximumPrice = runtime::MultiplyInt32Wrapping(defaultPrice, 10);
        if (price < minimumPrice || price > maximumPrice) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]invalid seed price(nSeedId : %d, %I64d, %I64d)",
            //     "Residence.cpp",
            //     0x437,
            //     seedId,
            //     price,
            //     count)
            // L2_LOG_ERROR(Log,
            //     L"[%S][%d]invalid seed price(nSeedId : %d, %I64d, %I64d)",
            //     "Residence.cpp",
            //     0x437,
            //     seedId,
            //     price,
            //     count)
            L2_LOG_WARN(
                Log, "Rejected next seed settings for manor %d, class %d: price %lld adena is outside %d..%d (count %lld).",
                manorId, seedId, static_cast<long long>(price), minimumPrice, maximumPrice, static_cast<long long>(count)
            );
            userCreature->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::INVALID_SEED_INFORMATION);
            return;
        }
    }

    if (!HaveManor(manorId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid manor in [SetNextSeedInfo] [%d]",
        //     manorId)
        // L2_LOG_ERROR(Log,
        //     L"invalid manor in [SetNextSeedInfo] [%d]",
        //     manorId)
        L2_LOG_ERROR(
            Log, "Cannot update next seed settings for residence %d: manor %d is not assigned to this residence.",
            m_data != nullptr ? m_data->m_nResidenceID : 0, manorId
        );
        return;
    }
    CManor* const manor = GetManor(manorId);
    if (manor == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid manor in [SetNextSeedInfo] [%d]",
        //     manorId)
        // L2_LOG_ERROR(Log,
        //     L"invalid manor in [SetNextSeedInfo] [%d]",
        //     manorId)
        L2_LOG_ERROR(
            Log, "Cannot update next seed settings for residence %d: manor %d has no available manor object.",
            m_data != nullptr ? m_data->m_nResidenceID : 0, manorId
        );
        return;
    }
    manor->SetNextSeedInfo(seedId, price, count);
}

void CResidence::ShowSellSeedList(CNPC& npc, User& user, const int /*residenceId*/, const int manorId) {
    // Current L2Server 0x00a38a48.
    L2_GUARD_FUNCTION();
    if (!HaveManor(manorId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid manor in [SetNextCropInfo] [%d]",
        //     manorId)
        // L2_LOG_ERROR(Log,
        //     L"invalid manor in [SetNextCropInfo] [%d]",
        //     manorId)
        L2_LOG_ERROR(
            Log, "Cannot show the seed sale list for residence %d: manor %d is not assigned to this residence.",
            m_data != nullptr ? m_data->m_nResidenceID : 0, manorId
        );
        return;
    }
    CManor* const manor = GetManor(manorId);
    if (manor == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid manor in [SetNextCropInfo] [%d]",
        //     manorId)
        // L2_LOG_ERROR(Log,
        //     L"invalid manor in [SetNextCropInfo] [%d]",
        //     manorId)
        L2_LOG_ERROR(
            Log, "Cannot show the seed sale list for residence %d: manor %d has no available manor object.",
            m_data != nullptr ? m_data->m_nResidenceID : 0, manorId
        );
        return;
    }

    CPacketHelperEx<20480> packet;
    std::int16_t itemCount = 0;
    for (int index = 0; index < 40; ++index) {
        if (manor->m_data->m_nSeedList[index] <= 0) {
            continue;
        }

        const int itemClassId = manor->m_data->m_nSeedIdList[index];
        const std::int64_t price = manor->m_data->m_nSeedPrice[index];
        const std::int64_t amount = manor->m_data->m_nSeedRemainCount[index];
        if (amount <= 0) {
            continue;
        }

        CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
        if (object == nullptr || reinterpret_cast<std::uintptr_t>(object) == UINT64_C(0xcdcdcdcdcdcdcdcd)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Undefined item(%d)",
            //     L"Residence.cpp",
            //     0x48c,
            //     itemClassId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Undefined item(%d)",
            //     L"Residence.cpp",
            //     0x48c,
            //     itemClassId)
            L2_LOG_ERROR(
                Log,
                "Manor %d seed sale list: skipping slot %d, class %d because the prototype is null or has the debug-fill "
                "pointer value; other entries remain eligible.",
                manorId, index, itemClassId
            );
            continue;
        }
        if (!object->IsItem()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Not a item(%d)",
            //     L"Residence.cpp",
            //     0x491,
            //     itemClassId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Not a item(%d)",
            //     L"Residence.cpp",
            //     0x491,
            //     itemClassId)
            L2_LOG_ERROR(
                Log,
                "Manor %d seed sale list: skipping slot %d, class %d because the prototype is not an item; other entries "
                "remain eligible.",
                manorId, index, itemClassId
            );
            continue;
        }

        CItemAssemblerAmount assembler(*static_cast<CItem*>(object), amount);
        const int assembledItem = assembler.AssembleItemInfo(packet);
        const int assembledPrice = packet.Assemble("Q", price);
        if (assembledItem + assembledPrice > 0) {
            ++itemCount;
        }
    }

    CUserSocket* const socket = user.GetSocket();
    const std::int64_t adena = user.m_inv.GetAdenaAmount();
    if (socket != nullptr) {
        TalkChannelCache::inst().mChannels[3].Cache(user.Id(), npc.Id(), 0);
        const int inventoryCount = user.m_inv.GetInventoryCount();
        socket->Send("cQddhb", 0xe9, adena, inventoryCount, manorId, itemCount, packet.GetLength(), packet.GetPacket());
    }
}

void CResidence::SetTaxRates(const int taxRate, const CastleType::Enum castleType) {
    // Current L2Server 0x00a31858.
    L2_GUARD_FUNCTION();
    m_data->m_nTaxRateToChange = taxRate;
    db.ReqWriteTax(this, 1, m_data->m_TaxInfoData[static_cast<int>(castleType)].m_nTaxRate, taxRate, IsCastle());
}

void CResidence::SetChildTaxRates(const int taxRate, const CastleType::Enum castleType) {
    // Current L2Server 0x00a31944.
    L2_GUARD_FUNCTION();
    m_data->m_nTaxForResidenceRateToChange = taxRate;
    db.ReqWriteTax(this, 2, m_data->m_TaxInfoData[static_cast<int>(castleType)].m_nTaxForResidenceRate, taxRate, IsCastle());
}

void CResidence::SetNPCPledge(CNPC* npc, int pledgeId) {
    // Current L2Server 0x00a33478.
    L2_GUARD_FUNCTION();
    if (npc == nullptr) {
        return;
    }

    npc->m_data->SetPledgeID(static_cast<std::uint32_t>(pledgeId));
    CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, pledgeId);
    if (pledge) {
        CAlliance* const alliance = pledge->GetAlliance();
        if (alliance != nullptr) {
            npc->m_data->SetAllianceID(alliance->m_data->m_nDbId);
        }
    }
    npc->ParamChanged(NPCT_NPC_PLEDGE);
}

void CResidence::SetAllNPCPledge(const int pledgeId) {
    // Current L2Server 0x00a340dc.
    L2_GUARD_FUNCTION();
    {
        L2_READ_GUARD(*this);
        for (const std::uint32_t npcServerId : m_AllOwningNPCServerIdList) {
            CCreature* const object = g_CreatureSmartIdManager.GetObjectW(npcServerId);
            CNPC* const npc = object != nullptr && object->IsNPC() ? object->CastNPC() : nullptr;
            SetNPCPledge(npc, pledgeId);
        }
    }
}

void CResidence::SendDB(const char* format, ...) const {
    L2_GUARD_FUNCTION();

    const int worldId = CThreadManager::GetThreadID() < 16 ? m_nWorldId : g_nWorldId;
    runtime::process::SetCurrentWorldId(worldId);
    CDB::CWorldSetter worldSetter;

    std::va_list arguments;
    va_start(arguments, format);
    if (worldId < 1) {
        SystemMail::sendNotSetThreadLocalWorldID(mailServer);
    } else {
        db.m_pDBSockets[::LegacyCrtRand() & 1]->SendV(format, arguments);
    }
    va_end(arguments);
}

void CResidence::WriteLock(const wchar_t* file, int line) {
    L2_GUARD_FUNCTION();
    g_ResidenceLock.WriteLock(file, line);
}

void CResidence::ReadLock(const wchar_t* file, int line) {
    // Current L2Server 0x00a306a0.
    L2_GUARD_FUNCTION();
    g_ResidenceLock.ReadLock(file, line, false);
}

void CResidence::ReadUnlock() {
    // Current L2Server 0x00a30750.
    L2_GUARD_FUNCTION();
    g_ResidenceLock.ReadUnlock(nullptr, 0);
}

void CResidence::WriteUnlock() {
    L2_GUARD_FUNCTION();
    g_ResidenceLock.WriteUnlock(nullptr, 0);
}

void CResidence::LoadGuard() {
    // Current L2Server 0x00a35e58 clears the two node maps without deleting
    // their pointer-valued payloads, then requests the residence DB rows.
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(*this);
        m_mapResidenceGuardData.clear();
        m_mapResidenceGuardDataCheck.clear();
    }
    db.RequestLoadGuardData(*this);
}

void CResidence::SetCampaignResult(bool success) {
    L2_GUARD_FUNCTION();

    {
        L2_WRITE_GUARD(*this);
        m_data->m_nSuccessCampaign = success ? 0 : 1;
    }
    db.RequestSetCastleCampaign(this, m_data->m_nSuccessCampaign);
}

void CResidence::ChangeResidenceState(const ResidenceState state) {
    // Current L2Server.exe 0x00a33edc, 511 bytes.
    L2_GUARD_FUNCTION();

    if (IsCastle() && GetResidenceOwnerPledge() != nullptr) {
        const CResidenceData* const data = m_data;
        const int residenceId = data != nullptr ? data->m_nResidenceID : 0;
        const int ownerPledgeId = data != nullptr ? static_cast<int>(data->m_nResidenceOwnerID) : 0;
        const ResidenceState previousState = data != nullptr ? data->m_ResidenceState : RS_MAX;

        const std::u16string masterName = std::u16string(GetResidenceOwnerPledge()->GetMasterName());
        const std::u16string pledgeName = std::u16string(GetResidenceOwnerPledge()->GetPledgeName());
        CLogFormatter formatter;
        formatter.WriteLog(
            0x7e5, 0, 0, 0, 0, 0, 0, 0, pledgeName.c_str(), u"", masterName.c_str(), residenceId, ownerPledgeId,
            static_cast<int>(previousState), static_cast<int>(state), 0, 0, 0, 0, 0, 0, u"", u"", u"", u"", 0
        );
    }

    if (m_data != nullptr) {
        m_data->m_ResidenceState = state;
    }
    db.RequestSaveResidenceStatus(this);
}

void CResidence::BroadcastSystemMessageW(const int messageId) {
    // Current L2Server 0x00a367bc.
    L2_GUARD_FUNCTION();
    const std::int16_t residenceId = m_data != nullptr ? static_cast<std::int16_t>(m_data->m_nResidenceID) : 0;
    g_ptlUserMap->BroadcastToNeighborRect(
        nullptr, 0x80000, m_OwnerRestartPointList[0], 0x4000, "chcch", 0x62, static_cast<std::int16_t>(messageId), 1, 5,
        residenceId
    );
}

void CResidence::BroadcastSystemMessageW(const char16_t* const text, const int messageId) {
    // Current L2Server 0x00a368d0.
    L2_GUARD_FUNCTION();
    const std::int16_t residenceId = m_data != nullptr ? static_cast<std::int16_t>(m_data->m_nResidenceID) : 0;
    g_ptlUserMap->BroadcastToNeighborRect(
        nullptr, 0x80000, m_OwnerRestartPointList[0], 0x4000, "chccSch", 0x62, static_cast<std::int16_t>(messageId), 2, 0,
        text, 5, residenceId
    );
}

void CResidence::BroadcastDominionMessage(const int messageType) {
    L2_GUARD_FUNCTION();
    class TLMFunc final : public CTLMFunc {
    public:
        TLMFunc(const int messageId, const int dominionId) noexcept : m_messageId(messageId), m_dominionId(dominionId) {}

        bool CanDo(CObject* object, double) override {
            if (object->IsPC() && !object->IsDeleting() && object->m_Deleted == 0) {
                object->CastUser()->GetSocket()->Send(
                    "chcch", 0x62, static_cast<std::int16_t>(m_messageId), 1, EST_CASTLEID,
                    static_cast<std::int16_t>(m_dominionId)
                );
            }
            return true;
        }

    private:
        std::int32_t m_messageId;
        std::int32_t m_dominionId;
    };


    int messageId = 0x967;
    if (messageType == 7) {
        messageId = 0x966;
    } else if (messageType == 0x19) {
        messageId = 0xac0;
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Unknown Dominion Broadcast Message. %d",
        //     0x967)
        // L2_LOG_ERROR(Log,
        //     L"Unknown Dominion Broadcast Message. %d",
        //     0x967)
        L2_LOG_ERROR(
            Log, "Residence %d: unknown dominion broadcast type %d; continuing with fallback system message %d.",
            m_data != nullptr ? m_data->m_nResidenceID : 0, messageType, messageId
        );
    }

    int dominionId = 0;
    if (IsDominion()) {
        dominionId = m_data != nullptr ? m_data->m_nResidenceID : 0;
    } else if (IsCastle() || IsFortress()) {
        dominionId = GetDominionSiegeID();
    }

    if (m_OwnerRestartPointList.empty()) {
        return;
    }
    CTLMFilterRect filter(0, m_OwnerRestartPointList[0], 16384.0);
    TLMFunc sender(messageId, dominionId);
    FOR_ALL_NEIGHBOR_USER_RECT(&filter, &sender);
}

CResidence* CResidenceDB::GetResidence(int residenceId) {
    L2_GUARD_FUNCTION();

    CResidence* const* const residenceSlot = [&]() -> CResidence* const* {
        L2_GUARD(m_ResidenceMapLock);
        const auto position = m_ResidenceIDMap.find(residenceId);
        if (position == m_ResidenceIDMap.end()) {
            return nullptr;
        }
        return &position->second;
    }();
    return residenceSlot != nullptr ? *residenceSlot : nullptr;
}

CResidence* CResidenceDB::GetResidence(const char16_t* residenceName) {
    // Current L2Server 0x00a35f38.
    L2_GUARD_FUNCTION();

    const auto position = [&]() {
        L2_GUARD(m_ResidenceMapLock);
        const auto position = m_ResidenceMap.find(residenceName);
        return position;
    }();

    if (position == m_ResidenceMap.end()) {
        const std::wstring hostName = residenceName != nullptr ? Utf16ToWide(residenceName) : std::wstring{};
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] undefined (or unregistered) Residence[%s]",
        //     __WFILE__,
        //     0x729,
        //     residenceName != nullptr
        //         ? hostName.c_str()
        //         : nullptr)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] undefined (or unregistered) Residence[%s]",
        //     __WFILE__,
        //     0x729,
        //     residenceName != nullptr
        //         ? hostName.c_str()
        //         : nullptr)
        L2_LOG_ERROR(
            Log, u"Residence '%ls' is not registered; reporting the call stack and returning no residence.",
            residenceName != nullptr ? hostName.c_str() : L"<null>"
        );
        runtime::process::ReportCallStack(2, false);
        return nullptr;
    }
    return position->second;
}

int CResidenceDB::GetCastlePledgeList(std::vector<int>& pledgeIds) {
    // Current L2Server 0x00a37b68.
    L2_GUARD_FUNCTION();

    const int count = [&]() -> int {
        L2_GUARD(m_ResidenceMapLock);
        int count = 0;
        for (const auto& entry : m_ResidenceIDMap) {
            CResidence* const residence = entry.second;
            if (residence == nullptr || !residence->IsCastle()) {
                continue;
            }
            CPledge* const pledge = residence->GetResidenceOwnerPledge();
            if (pledge != nullptr) {
                pledgeIds.push_back(runtime::Int32FromBits(static_cast<std::int64_t>(pledge->GetDbId())));
                ++count;
            }
        }
        return count;
    }();
    return count;
}

bool CResidence::IsInBattleField(const FVector& position, bool includeZ) {
    L2_GUARD_FUNCTION();

    for (CTerritory* const territory : m_lstBattleFieldTerritory) {
        if (territory->IsInside(position, includeZ)) {
            return true;
        }
    }
    return false;
}

bool CResidence::IsInResidence(const FVector& position, bool includeZ) {
    // Current L2Server 0x00a32758.
    L2_GUARD_FUNCTION();

    for (CTerritory* const territory : m_lstResidenceTerritory) {
        if (territory->IsInside(position, includeZ)) {
            return true;
        }
    }
    return false;
}

CResidence* CResidence::GetBattleFieldResidence(const FVector& position) {
    // Current L2Server 0x00a35b58.
    L2_GUARD_FUNCTION();

    if (!CWorld::Inst().GetGeo()->IsField(
            static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z),
            GEO_FIELD_BATTLE_RESIDENCE
        )) {
        return nullptr;
    }

    for (const auto& entry : g_ResidenceDB.m_ResidenceIDMap) {
        CResidence* const residence = entry.second;
        if (residence != nullptr && (residence->IsCastle() || residence->IsFortress()) &&
            residence->IsInBattleField(position, true)) {
            return residence;
        }
    }
    return nullptr;
}

void CResidence::ReserveExpelOfUsers(std::vector<User*>& users) {
    // Current L2Server 0x00a38284. The target assumes at
    // least one banish point and performs the modulo without an empty-list
    // guard.
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(g_ExpelTimer);
        const int userCount = static_cast<int>(users.size());
        for (int index = 0; index < userCount; ++index) {
            const std::uint32_t pointIndex =
                static_cast<std::uint32_t>(std::rand()) % static_cast<std::uint32_t>(m_BanishPointList.size());
            const FVector position = m_BanishPointList[pointIndex];
            User* const user = runtime::CheckedVectorAt(users, static_cast<std::size_t>(index));

            if (IsFortress() && user->m_bHasFortressFlag) {
                // The current body keeps this otherwise-unused virtual cast.
                (void)CastFortress();
                CFortress::HandleDeleteFlag(user, false);
            }
            g_ExpelTimer.ReserveExpelWithNoLock(user, position);
        }
    }
}

void CResidence::BanNonDominionUsers(CDominionSiegeEntry* const entry) {
    // Current L2Server 0x00a38638.
    L2_GUARD_FUNCTION();
    if (entry == nullptr) {
        return;
    }

    class TLMFIlter final : public CTLMFilterRect {
    public:
        TLMFIlter(const FVector& origin, CDominionSiegeEntry* const entry, CResidence* const residence) :
            CTLMFilterRect(0, origin, 16384.0), m_entry(entry), m_residence(residence) {}

        bool CanDo(CObject* const object) override {
            if (!CTLMFilterRect::CanDo(object) || !object->IsPC() || object->IsDeleting() || object->m_Deleted != 0) {
                return false;
            }

            User* const user = object->CastUser();
            if (m_entry->IsInSiegeUser(user->GetServerId())) {
                return false;
            }
            const std::uint32_t pledgeId = user->m_data->m_nPledgeID;
            if (pledgeId != 0 && m_entry->IsInSiegePledge(pledgeId)) {
                return false;
            }
            return m_residence->IsInResidence(user->GetPos(), false);
        }

    private:
        CDominionSiegeEntry* m_entry;
        CResidence* m_residence;
    };

    class TLMFunc final : public CTLMFunc {
    public:
        explicit TLMFunc(std::vector<User*>& users) : m_users(users) {}

        bool CanDo(CObject* const object, double) override {
            if (object->IsPC() && !object->IsDeleting() && object->m_Deleted == 0) {
                User* const user = object->CastUser();
                m_users.push_back(user);
                L2_ADD_REF(user, ObjectRefType::EventRetain);
            }
            return true;
        }

    private:
        std::vector<User*>& m_users;
    };

    std::vector<User*> users;
    TLMFIlter filter(*m_OwnerRestartPointList.begin(), entry, this);
    TLMFunc gather(users);
    FOR_ALL_NEIGHBOR_USER_RECT(&filter, &gather);
    ReserveExpelOfUsers(users);
}

void CResidence::SetFreezeField(const bool enabled) {
    // Current L2Server 0x00a32cdc reacquires the world/geo
    // owner for every territory.
    L2_GUARD_FUNCTION();
    for (CTerritory* const territory : m_lstBattleFieldTerritory) {
        CWorld::Inst().GetGeo()->SetField(territory, GEO_FIELD_FREEZE, enabled);
    }
}

bool CResidence::IsInBattleCampZone(const FVector& position) {
    // Current L2Server 0x00ba1114. Battle-camp territories always include
    // the Z coordinate. Unlike IsInBattleField this owner has no flag.
    L2_GUARD_FUNCTION();

    for (CTerritory* const territory : m_lstBattleCampTerritory) {
        if (territory->IsInside(position, true)) {
            return true;
        }
    }
    return false;
}

void CResidence::ShowTerritories(CCreature& creature, const TerritoryType::Enum type) {
    // Current L2Server 0x00a32528.
    L2_GUARD_FUNCTION();

    if (static_cast<std::uint32_t>(type) >= static_cast<std::uint32_t>(TerritoryType::Max)) {
        return;
    }
    if (type == TerritoryType::OwnerRestart) {
        m_OwnerRestartTerritory.ShowTerritoryByAdena(creature);
        return;
    }

    std::vector<CTerritory*>* territories = nullptr;
    if (type == TerritoryType::Residence) {
        territories = &m_lstResidenceTerritory;
    } else if (type == TerritoryType::BattleCamp) {
        territories = &m_lstBattleCampTerritory;
    } else if (type == TerritoryType::BattleField) {
        territories = &m_lstBattleFieldTerritory;
    }
    if (territories == nullptr) {
        return;
    }
    for (CTerritory* const territory : *territories) {
        territory->ShowTerritoryByAdena(creature);
    }
}

void CResidence::GoTerritory(CCreature& creature, const TerritoryType::Enum type) {
    // Current L2Server 0x00a32654.
    L2_GUARD_FUNCTION();

    if (static_cast<std::uint32_t>(type) >= static_cast<std::uint32_t>(TerritoryType::Max)) {
        return;
    }

    CTerritory* territory = nullptr;
    if (type == TerritoryType::OwnerRestart) {
        territory = &m_OwnerRestartTerritory;
    } else {
        std::vector<CTerritory*>* territories = nullptr;
        if (type == TerritoryType::Residence) {
            territories = &m_lstResidenceTerritory;
        } else if (type == TerritoryType::BattleCamp) {
            territories = &m_lstBattleCampTerritory;
        } else if (type == TerritoryType::BattleField) {
            territories = &m_lstBattleFieldTerritory;
        }
        if (territories != nullptr && !territories->empty()) {
            territory = (*territories)[0];
        }
    }
    if (territory != nullptr) {
        static_cast<void>(territory->GoFirstPoint(creature));
    }
}

bool CResidence::IsOffendingPledge(const int pledgeId, const int worldId) {
    // Current L2Server 0x00a308b4.
    L2_GUARD_FUNCTION();
    return m_data != nullptr && m_data->IsOffendingPledge(pledgeId, worldId);
}

CResidence* CResidenceDB::GetWhoseBattleFieldWithBattleCampZone(const FVector& position) {
    L2_GUARD_FUNCTION();

    for (CResidence* const residence : m_lstResidenceWithBattleCampZone) {
        if (residence->IsInBattleField(position, true)) {
            return residence;
        }
    }
    return nullptr;
}

bool CResidenceDB::CanUseSwoopCannonInDominionSiege(const FVector& position, bool includeZ) {
    // Current L2Server 0x00a35ccc.
    L2_GUARD_FUNCTION();

    {
        L2_GUARD(m_ResidenceMapLock);
        for (const auto& entry : m_ResidenceIDMap) {
            CResidence* const residence = entry.second;
            if (residence != nullptr && residence->IsUnderDominionSiege() && residence->IsInBattleField(position, true) &&
                !residence->IsInResidence(position, includeZ)) {
                return true;
            }
        }
    }
    return false;
}

CResidence* CResidenceDB::GetWhoseBattleCampZone(const FVector& position) {
    // Current L2Server 0x00ba1204.
    L2_GUARD_FUNCTION();

    for (CResidence* const residence : m_lstResidenceWithBattleCampZone) {
        if (residence->IsInBattleCampZone(position)) {
            return residence;
        }
    }
    return nullptr;
}

CDominion* CResidenceDB::GetDominion(int residenceId) {
    L2_GUARD_FUNCTION();

    CResidence* const residence = GetResidence(residenceId);
    return residence != nullptr && residence->IsDominion() ? residence->CastDominion() : nullptr;
}

void CResidence::RollbackChangeTaxIncome(const std::int64_t income) {
    // Current L2Server 0x00a30ac4. The original private source spelling is
    // unavailable. This is the real Residence.cpp rollback owner called by
    // User::CheckManipulateQueue after a failed MANI_TAX_BANK withdrawal.
    {
        L2_WRITE_GUARD(*this);

        const std::uint64_t nextBits = static_cast<std::uint64_t>(m_data->m_nTaxIncome) + static_cast<std::uint64_t>(income);
        std::int64_t nextIncome;
        static_assert(sizeof(nextIncome) == sizeof(nextBits));
        std::memcpy(&nextIncome, &nextBits, sizeof(nextIncome));
        if (nextIncome < 0) {
            m_data->m_nTaxIncome = 0;
        } else if (nextIncome < 1000000000000LL) {
            m_data->m_nTaxIncome = nextIncome;
        }
    }
}

void CResidence::RollbackDepositToSafe(const std::int64_t amount) {
    // Current L2Server 0x00a32970. This is the paired Residence.cpp rollback
    // after DepositToSafe succeeded but item manipulation failed.
    {
        L2_WRITE_GUARD(*this);

        const std::uint64_t nextBits = static_cast<std::uint64_t>(m_data->m_nTaxIncome) - static_cast<std::uint64_t>(amount);
        std::int64_t nextIncome;
        static_assert(sizeof(nextIncome) == sizeof(nextBits));
        std::memcpy(&nextIncome, &nextBits, sizeof(nextIncome));
        if (nextIncome < 0) {
            nextIncome = 0;
        } else if (nextIncome > 999999999999LL) {
            nextIncome = 999999999999LL;
        }
        m_data->m_nTaxIncome = nextIncome;
    }
}

bool CResidence::ChangeTaxIncome(const std::int64_t income) {
    // Current L2Server 0x00a329ec.
    L2_GUARD_FUNCTION();
    const bool result = [&]() -> bool {
        L2_WRITE_GUARD(*this);

        bool result = true;
        std::int64_t nextIncome = m_data->m_nTaxIncome - income;
        if (nextIncome < 0) {
            result = false;
            nextIncome = 0;
        } else if (nextIncome > 999999999999LL) {
            nextIncome = 999999999999LL;
        }
        m_data->m_nTaxIncome = nextIncome;

        return result;
    }();
    return result;
}

bool CResidence::DepositToSafe(const std::int64_t amount) {
    // Current L2Server 0x00a30b2c.
    const bool result = [&]() -> bool {
        L2_WRITE_GUARD(*this);

        const std::uint64_t nextBits = static_cast<std::uint64_t>(m_data->m_nTaxIncome) + static_cast<std::uint64_t>(amount);
        std::int64_t nextIncome;
        static_assert(sizeof(nextIncome) == sizeof(nextBits));
        std::memcpy(&nextIncome, &nextBits, sizeof(nextIncome));

        bool result = true;
        if (nextIncome < 0) {
            m_data->m_nTaxIncome = 0;
        } else if (nextIncome < 1000000000000LL) {
            m_data->m_nTaxIncome = nextIncome;
        } else {
            result = false;
        }

        return result;
    }();
    return result;
}

void CResidence::ResetVault(const int resetIncome) {
    // Current L2Server.exe 0x00a31a30.
    L2_GUARD_FUNCTION();
    if (resetIncome == 1) {
        m_data->m_nTaxIncome = 0;
        m_data->m_nTaxIncomeTemp = 0;
    }
    SaveIncome(IsCastle(), 0x0e);
}

void CResidence::OpenObserver(User* const user) {
    // Current L2Server 0x00a31b04.
    L2_GUARD_FUNCTION();
    if (m_pObserver != nullptr) {
        m_pObserver->Interact(user);
    }
}

void CResidence::SaveIncome(const bool isCastle, const int reason) {
    // Current L2Server 0x00a30bac.
    L2_GUARD_FUNCTION();
    db.RequestSaveCastleIncome(*this, m_data->m_nTaxIncome, m_data->m_nTaxIncomeTemp, isCastle ? 1 : 0, reason);
}

void CResidence::TributeToParentResidence(const std::uint32_t amount, const CastleType::Enum castleType, const bool isSell) {
    // Current L2Server 0x00a364e4.
    L2_GUARD_FUNCTION();
    std::uint64_t retained = amount;
    const std::size_t type = static_cast<std::size_t>(castleType);

    if (m_data->m_nAgitParentCastleId > 0) {
        CResidence* const parent = g_ResidenceDB.GetResidence(m_data->m_nAgitParentCastleId);
        if (parent != nullptr) {
            const std::uint32_t rate = isSell ? m_data->m_TaxInfoData[type].m_nTaxForResidenceRate
                                              : m_data->m_TaxInfoData[type].m_nBuyTaxForResidenceRate;
            const std::uint64_t tribute =
                static_cast<std::uint64_t>((static_cast<double>(rate) / 100.0) * static_cast<double>(amount));
            retained = static_cast<std::uint64_t>(amount) - tribute;
            parent->TributeToParentResidence(
                static_cast<std::uint32_t>(tribute), static_cast<CastleType::Enum>(parent->m_data->m_ResidenceType), isSell
            );

            CLogFormatter formatter;
            formatter.WriteLog(
                0x3b5, 0, 0, 0, 0, 0, 0, 0, u"", u"", u"", m_data != nullptr ? m_data->m_nResidenceID : 0,
                m_data->m_nAgitParentCastleId, m_data->m_TaxInfoData[type].m_nTaxForResidenceRate, amount, tribute,
                m_data->m_nTaxIncome, m_data->m_nTaxIncomeTemp, isSell, 0, 0, u"", u"", u"", u"", 0
            );
        }
    }

    {
        L2_WRITE_GUARD(*this);
        m_data->m_nTaxIncomeTemp += static_cast<std::int64_t>(retained);
    }
    SaveIncome(IsCastle(), 0x0d);
}

void CResidence::ReportShopTax(const std::int64_t amount, const CastleType::Enum castleType, const bool isSell) {
    // Current L2Server 0x00a36c34.
    L2_GUARD_FUNCTION();
    const std::size_t type = static_cast<std::size_t>(castleType);
    const std::uint32_t taxTaxRate =
        isSell ? m_data->m_TaxInfoData[type].m_nTaxTaxRate : m_data->m_TaxInfoData[type].m_nBuyTaxTaxRate;
    const double retainedRate = std::max(0.0, 100.0 - static_cast<double>(taxTaxRate));
    std::int64_t retained = static_cast<std::int64_t>((retainedRate / 100.0) * static_cast<double>(amount));
    std::uint64_t tribute = 0;

    if (m_data->m_nAgitParentCastleId > 0) {
        CResidence* const parent = g_ResidenceDB.GetResidence(m_data->m_nAgitParentCastleId);
        if (parent != nullptr) {
            const std::uint32_t rate = isSell ? m_data->m_TaxInfoData[type].m_nTaxForResidenceRate
                                              : m_data->m_TaxInfoData[type].m_nBuyTaxForResidenceRate;
            tribute = static_cast<std::uint64_t>((static_cast<double>(rate) / 100.0) * static_cast<double>(retained));
            retained -= static_cast<std::int64_t>(tribute);
            parent->TributeToParentResidence(
                static_cast<std::uint32_t>(tribute), static_cast<CastleType::Enum>(parent->m_data->m_ResidenceType), isSell
            );

            CLogFormatter formatter;
            formatter.WriteLog(
                0x3b5, 0, 0, 0, 0, 0, 0, 0, u"", u"", u"", m_data != nullptr ? m_data->m_nResidenceID : 0,
                m_data->m_nAgitParentCastleId, m_data->m_TaxInfoData[type].m_nTaxForResidenceRate, amount, tribute,
                m_data->m_nTaxIncome, m_data->m_nTaxIncomeTemp, isSell, 0, 0, u"", u"", u"", u"", 0
            );
        }
    }

    {
        L2_WRITE_GUARD(*this);
        m_data->m_nTaxIncomeTemp += retained;
    }

    CSPointer<CPledge> pledge = CPledge::FindPledge(m_nWorldId, m_data->m_nResidenceOwnerID);
    if (pledge) {
        const std::u16string pledgeName = std::u16string(pledge->GetPledgeName());
        const std::u16string masterName = std::u16string(pledge->GetMasterName());
        CLogFormatter formatter;
        formatter.WriteLog(
            0x3b6, 0, 0, 0, 0, 0, 0, 0, pledgeName.c_str(), masterName.c_str(), u"", m_data->m_nResidenceID,
            m_data->m_nAgitParentCastleId, tribute, retained, m_data->m_TaxInfoData[type].m_nTaxRate,
            m_data->m_nResidenceOwnerID, m_data->m_nTaxIncome, m_data->m_nTaxIncomeTemp, isSell, 0, u"", u"", u"", u"", 0
        );
    }
    SaveIncome(IsCastle(), 0x0c);
}

void CResidence::SendResidenceTaxIncome(const int mode) {
    // Current L2Server 0x00a32df0.
    L2_GUARD_FUNCTION();
    if (mode != 1) {
        return;
    }

    constexpr std::int64_t kMaxTaxIncome = 999999999999LL;
    const bool isCastle = IsCastle();
    if (!isCastle || m_data->m_ResidenceType == 2) {
        std::int64_t income = m_data->m_nTaxIncome + m_data->m_nTaxIncomeTemp;
        if (income < 0) {
            income = 0;
        } else if (income > kMaxTaxIncome) {
            income = kMaxTaxIncome;
        }
        m_data->m_nTaxIncome = income;

        if (isCastle) {
            CLogFormatter formatter;
            formatter.WriteLog(
                0x2333, 0, 0, 0, 0, 0, 0, 0, u"", u"", u"", m_data->m_nResidenceID, m_data->m_nAgitParentCastleId,
                m_data->m_nTaxIncomeTemp, m_data->m_TaxInfoData[2].m_nTaxRate, m_data->m_nResidenceOwnerID, 0, 0, 0, 0, 0,
                u"", u"", u"", u"", 0
            );
        }
    }

    m_data->m_nTaxIncomeTemp = 0;
    SaveIncome(IsCastle(), 0x0f);
}

void CResidenceDB::SendResidenceTaxIncome() {
    L2_GUARD_FUNCTION();

    for (const auto& entry : m_ResidenceIDMap) {
        CResidence* const residence = entry.second;
        if (residence == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d]Cannot find residence. "
            //     L"SetResicendeTaxIncome()",
            //     L"Residence.cpp",
            //     0x698)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d]Cannot find residence. "
            //     L"SetResicendeTaxIncome()",
            //     L"Residence.cpp",
            //     0x698)
            L2_LOG_ERROR(
                Log, "Cannot send tax income for residence registry ID %d: the object is null; this entry is skipped.",
                entry.first
            );
            continue;
        }
        residence->SendResidenceTaxIncome(1);
    }
}

int CResidenceDB::VerifyMaxRateOfCastle() {
    // Current L2Server 0x00a3584c.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_ResidenceMapLock);
        for (const auto& entry : m_ResidenceIDMap) {
            CResidence* const residence = entry.second;
            if (residence == nullptr || !residence->IsCastle()) {
                continue;
            }
            const int residenceType = residence->m_data->m_ResidenceType;
            TaxInfo& tax = residence->m_data->m_TaxInfoData[static_cast<std::size_t>(residenceType)];
            if (tax.m_nTaxRate > static_cast<std::uint32_t>(SSQSystem::inst().GetMaxTaxRateOfCastle())) {
                const std::uint32_t maximumRate = static_cast<std::uint32_t>(SSQSystem::inst().GetMaxTaxRateOfCastle());
                residence->m_data->m_nTaxRateToChange = static_cast<int>(maximumRate);
                tax.m_nTaxRate = maximumRate;
                db.ReqWriteTax(
                    residence, 1, static_cast<int>(tax.m_nTaxRate), residence->m_data->m_nTaxRateToChange,
                    residence->IsCastle()
                );
            }
        }
    }
    return 0;
}

bool CResidenceDB::IsSiegeAgit(int residenceId) {
    // Current L2Server 0x00888810. The original body is a direct ordered-set
    // lookup. Keep it separate from the residence-map lookup because callers
    // persist this bit to CacheD even when no CResidence object is loaded.
    L2_GUARD_FUNCTION();
    return m_setSiegeAgit.find(residenceId) != m_setSiegeAgit.end();
}

void CResidenceDB::SetNowTalkingMessenger(User* const user, CNPC* const npc) {
    // Current L2Server 0x00a37300.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_MessengerMapLock);
        m_mapUserToMessengerLastTalked[user->m_data->m_nId] = npc->m_data->m_nId;
    }
}

bool CResidenceDB::CheckTalkableDistanceToMessenger(User* const user, const int residenceId) {
    // Current L2Server 0x00a36330.
    L2_GUARD_FUNCTION();
    if (user->IsBuilder()) {
        return true;
    }

    int messengerId = 0;
    {
        L2_GUARD(m_MessengerMapLock);
        const auto position = m_mapUserToMessengerLastTalked.find(user->m_data->m_nId);
        if (position != m_mapUserToMessengerLastTalked.end()) {
            messengerId = position->second;
        }
    }

    if (messengerId == 0) {
        return false;
    }
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(messengerId));
    CNPC* const messenger = creature != nullptr ? creature->CastNPC() : nullptr;
    return messenger != nullptr && messenger->m_data->m_nResidenceID == residenceId && user->IsTalkableDistance(messenger);
}
