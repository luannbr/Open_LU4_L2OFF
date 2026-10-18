#include "AtomicJobServerSide.h"
#include "Shared/src/TextRuntime.h"

#include "DB.h"
#include "QuestBase.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "SkillAcquire.h"
#include "SkillEnchant.h"
#include "User.h"

#include "Shared/src/IOC.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Time32.h"

#include <cstdio>
#include <cstdlib>

namespace {

#define L2_REGISTER_ATOMIC_JOB(JobId, ConcreteType, Name)                                                                   \
    AtomicJob* CreateAtomicJob_##Name() { return new ConcreteType(); }                                                      \
    const bool isReg_AtomicJob_##Name =                                                                                     \
        Singleton<AtomicJobFactory>::inst().Register(static_cast<AJ_JOBID_TYPE>(JobId), &CreateAtomicJob_##Name)

L2_REGISTER_ATOMIC_JOB(0x00, AtomicParamChange, ParamChange);
L2_REGISTER_ATOMIC_JOB(0x01, AtomicCreatePet, CreatePet);
L2_REGISTER_ATOMIC_JOB(0x02, AtomicEvolvePet, EvolvePet);
L2_REGISTER_ATOMIC_JOB(0x03, AtomicDestroyPet, DestroyPet);
L2_REGISTER_ATOMIC_JOB(0x04, AtomicBuyLottoTicket, BuyLottoTicket);
L2_REGISTER_ATOMIC_JOB(0x05, MemoControl, MemoControl);
L2_REGISTER_ATOMIC_JOB(0x06, AtomicShowPage, ShowPage);
L2_REGISTER_ATOMIC_JOB(0x07, AtomicShowFHTML, ShowFHTML);
L2_REGISTER_ATOMIC_JOB(0x08, AtomicDropItem, DropItem);
L2_REGISTER_ATOMIC_JOB(0x09, AtomicRideWyvern, RideWyvern);
L2_REGISTER_ATOMIC_JOB(0x0a, AtomicPledgeLevelUp, PledgeLevelUp);
L2_REGISTER_ATOMIC_JOB(0x0b, AtomicAddSSQMember, AddSSQMember);
L2_REGISTER_ATOMIC_JOB(0x0c, AtomicDeleteDepositedSSQItem, DeleteDepositedSSQItem);
L2_REGISTER_ATOMIC_JOB(0x0d, AtomicAddTimeAttackRecord, AddTimeAttackRecord);
L2_REGISTER_ATOMIC_JOB(0x0e, AtomicTeleportParty, TeleportParty);
L2_REGISTER_ATOMIC_JOB(0x0f, AtomicSetResidenceDeco, SetResidenceDeco);
L2_REGISTER_ATOMIC_JOB(0x10, AtomicResetResidenceDeco, ResetResidenceDeco);
L2_REGISTER_ATOMIC_JOB(0x11, AtomicSetFortressFacility, SetFortressFacility);
L2_REGISTER_ATOMIC_JOB(0x12, AtomicResetFortressFacility, ResetFortressFacility);
L2_REGISTER_ATOMIC_JOB(0x13, AtomicAddTimer, AddTimer);
L2_REGISTER_ATOMIC_JOB(0x14, AtomicStartObserver, StartObserver);
L2_REGISTER_ATOMIC_JOB(0x15, AtomicSetTicketBuyCount, SetTicketBuyCount);
L2_REGISTER_ATOMIC_JOB(0x16, AtomicTeleportTo, TeleportTo);
L2_REGISTER_ATOMIC_JOB(0x17, AtomicAddTimeAttackFee, AddTimeAttackFee);
L2_REGISTER_ATOMIC_JOB(0x18, AtomicGiveTimeAttackReward, GiveTimeAttackReward);
L2_REGISTER_ATOMIC_JOB(0x19, AtomicDepositSSQItemEx, DepositSSQItemEx);
L2_REGISTER_ATOMIC_JOB(0x1a, AtomicDropItem2, DropItem2);
L2_REGISTER_ATOMIC_JOB(0x1b, AtomicSetNobless, SetNobless);
L2_REGISTER_ATOMIC_JOB(0x1c, AtomicSetHero, SetHero);
L2_REGISTER_ATOMIC_JOB(0x1d, AtomicCastBuff, CastBuff);
L2_REGISTER_ATOMIC_JOB(0x1f, AtomicChangePetName, ChangePetName);
L2_REGISTER_ATOMIC_JOB(0x20, AtomicResetPetName, ResetPetName);
L2_REGISTER_ATOMIC_JOB(0x23, AtomicCastBuffWithMpConsumption, CastBuffWithMpConsumption);
L2_REGISTER_ATOMIC_JOB(0x24, AtomicEvolvePetWithSameExp, EvolvePetWithSameExp);
L2_REGISTER_ATOMIC_JOB(0x25, AtomicUpdatePCCafePoint, UpdatePCCafePoint);
L2_REGISTER_ATOMIC_JOB(0x26, AtomicEventRide, EventRide);
L2_REGISTER_ATOMIC_JOB(0x27, AtomicDeleteAcquireSkill, DeleteAcquireSkill);
L2_REGISTER_ATOMIC_JOB(0x28, AtomicDeleteAcquireSkills, DeleteAcquireSkills);
L2_REGISTER_ATOMIC_JOB(0x29, AtomicUpdatePVPPoint, UpdatePVPPoint);
L2_REGISTER_ATOMIC_JOB(0x2a, AtomicManipulatePVPPointWithItem, ManipulatePVPPointWithItem);
L2_REGISTER_ATOMIC_JOB(0x2b, NRMemoControl, NRMemoControl);
L2_REGISTER_ATOMIC_JOB(0x2c, AtomicShowQuestPage, ShowQuestPage);
L2_REGISTER_ATOMIC_JOB(0x2d, AtomicShowQuestFHTML, ShowQuestFHTML);
L2_REGISTER_ATOMIC_JOB(0x2e, AtomicSummonAirShip, SummonAirShip);
L2_REGISTER_ATOMIC_JOB(0x2f, AtomicBuyAirShip, BuyAirShip);
L2_REGISTER_ATOMIC_JOB(0x30, AtomicGetOnAirShip, GetOnAirShip);
L2_REGISTER_ATOMIC_JOB(0x31, AtomicItemDelete, ItemDelete);
L2_REGISTER_ATOMIC_JOB(0x32, AtomicChangeTalkTarget, ChangeTalkTarget);
L2_REGISTER_ATOMIC_JOB(0x33, AtomicChangeClassAndSetSkill, ChangeClassAndSetSkill);
L2_REGISTER_ATOMIC_JOB(0x34, AtomicRewardSkillEnchant, RewardSkillEnchant);
L2_REGISTER_ATOMIC_JOB(0x35, AtomicSetSkill, SetSkill);
L2_REGISTER_ATOMIC_JOB(0x36, AtomicChangeClass, ChangeClass);
L2_REGISTER_ATOMIC_JOB(0x37, AtomicChangeDailyQuestFlag, ChangeDailyQuestFlag);
L2_REGISTER_ATOMIC_JOB(0x38, AtomicNpcSetSkillAll, NpcSetSkillAll);
L2_REGISTER_ATOMIC_JOB(0x39, AtomicUpdateUserPoint, UpdateUserPoint);
L2_REGISTER_ATOMIC_JOB(0x3a, AtomicShowCardCollectSystem, ShowCardCollectSystem);
L2_REGISTER_ATOMIC_JOB(0x3b, AtomicCastBuffForEventMaker, CastBuffForEventMaker);
L2_REGISTER_ATOMIC_JOB(0x3c, AtomicAddWorldChatBonusPoint, AddWorldChatBonusPoint);
L2_REGISTER_ATOMIC_JOB(0x3d, AtomicAddFactionPoint, AddFactionPoint);
L2_REGISTER_ATOMIC_JOB(0x3e, AtomicSetFactionPoint, SetFactionPoint);
L2_REGISTER_ATOMIC_JOB(0x3f, Atomic_AJ_OPEN_DECO_NPC, OpenDecoNpc);
L2_REGISTER_ATOMIC_JOB(0x40, AtomicContentUIEvent, ContentUIEvent);
L2_REGISTER_ATOMIC_JOB(0x41, AtomicAcceptUserQuest, AcceptUserQuest);

#undef L2_REGISTER_ATOMIC_JOB

} // namespace

AtomicJobList::AtomicJobList(int reserveCount, int userId) : m_UserId(userId) {
    if (reserveCount > 0) {
        mJobVector.reserve(static_cast<std::size_t>(reserveCount));
    }
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::JobList;
    nLine = 0x12f;
}

AtomicJobList* AtomicJobList::CreateAtomicJobList(const unsigned char* packet, int userId, bool addItemJobCheck) {
    // Current L2Server 0x004977e0, original owner
    // AtomicJobServerSide.cpp.
    L2_GUARD_FUNCTION();
    std::int32_t jobCount = 0;
    packet = ::Disassemble(packet, "d", &jobCount);
    if (jobCount < 1 && !addItemJobCheck) {
        return nullptr;
    }

    AtomicJobList* const list = new AtomicJobList(jobCount, userId);
    if (addItemJobCheck) {
        list->AddAtomicJob(new CAtomicItemJobCheck(userId));
    }

    bool needsSaveQuestInfo = false;
    for (std::int32_t index = 0; index < jobCount; ++index) {
        std::int16_t jobId = 0;
        packet = ::Disassemble(packet, "h", &jobId);
        if (jobId == 0x05 || jobId == 0x2b) {
            needsSaveQuestInfo = true;
        }

        AtomicJob* const job = Singleton<AtomicJobFactory>::inst().CreateObject(static_cast<AJ_JOBID_TYPE>(jobId));
        if (job == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] CreateAtomicJobList() failed, invalid job id=[%d]",
            //     L"AtomicJobServerSide.cpp",
            //     0x19d,
            //     static_cast<int>(jobId))
            // Log.Add(
            //     LOG_ERROR,
            //     L"[jobs] Cannot create job list: unsupported job ID %d at index %d of %d for character SID %u. Discarding
            //     the list.", static_cast<int>(jobId), index, jobCount, static_cast<unsigned int>(userId))
            L2_LOG_ERROR(
                Log,
                u"[jobs] Cannot create job list: unsupported job ID %d at index %d of %d for character SID %u; discarding "
                u"the list.",
                static_cast<int>(jobId), index, jobCount, static_cast<unsigned int>(userId)
            );
            list->Destroy();
            return nullptr;
        }
        packet = job->Disassemble(packet);
        list->AddAtomicJob(job);
    }

    if (needsSaveQuestInfo) {
        list->AddAtomicJob(new CAtomicSaveQuestInfo(userId));
    }
    if (std::getenv("L2_QUEST_TRACE") != nullptr) {
        // std::fprintf(
        //     stderr,
        //     "[QUEST_TRACE] AtomicJobList::Create user_sid=%d jobs=%d "
        //     "save_quest=%d\n",
        //     userId,
        //     jobCount,
        //     needsSaveQuestInfo ? 1 : 0)
        // Trace verbosity is controlled by L2_QUEST_TRACE. LOG_DEBUG is suppressed in L2Server.
        // Log.Add(LOG_NORMAL, L"[quest] Created list for character SID %u from %d supplied jobs, quest-save helper
        // scheduled: %ls.", static_cast<unsigned int>(userId), jobCount, needsSaveQuestInfo ? L"yes" : L"no")
        L2_LOG_INFO(
            Log, u"[quest] Created list for character SID %u from %d supplied jobs; quest-save helper scheduled: %ls.",
            static_cast<unsigned int>(userId), jobCount, needsSaveQuestInfo ? L"yes" : L"no"
        );
    }
    return list;
}

AtomicItemDelete::AtomicItemDelete() : m_UserSID(0), m_SkillID(0), m_Level(0), m_SubLevel(0), m_AcquireType(ESAT_NORMAL) {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ItemDelete;
    nLine = 0xa99;
}

AtomicItemDelete::AtomicItemDelete(int userSID, int skillId, int level, int subLevel, EtcSkillAcquireType acquireType) :
    m_UserSID(userSID), m_SkillID(skillId), m_Level(level), m_SubLevel(subLevel), m_AcquireType(acquireType) {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ItemDelete;
    nLine = 0xa90;
}

bool CAtomicSkillEnchantNormal::Do() {
    // Current L2Server 0x004973b4.
    L2_GUARD_FUNCTION();
    SkillEnchantOperator* const enchant = Singleton<SkillEnchantFactory>::inst().CreateObject(SECT_NORMAL);
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nUserSid));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    return enchant->OperateAtomicJob(user, m_nSkillID, m_nSkillLevel, m_nSkillSubLevel);
}

bool CAtomicSkillEnchantSafe::Do() {
    // Current L2Server 0x004974bc.
    L2_GUARD_FUNCTION();
    SkillEnchantOperator* const enchant = Singleton<SkillEnchantFactory>::inst().CreateObject(SECT_SAFE);
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nUserSid));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    return enchant->OperateAtomicJob(user, m_nSkillID, m_nSkillLevel, m_nSkillSubLevel);
}

bool CAtomicSkillEnchantRouteChange::Do() {
    // Current L2Server 0x004975c8.
    L2_GUARD_FUNCTION();
    SkillEnchantOperator* const enchant = Singleton<SkillEnchantFactory>::inst().CreateObject(SECT_ROUTE_CHANGE);
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nUserSid));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    return enchant->OperateAtomicJob(user, m_nSkillID, m_nSkillLev, m_nSkillSubLev);
}

bool AtomicSkillEnchantForce::Do() {
    // Current L2Server 0x004976d4.
    L2_GUARD_FUNCTION();
    SkillEnchantOperator* const enchant = Singleton<SkillEnchantFactory>::inst().CreateObject(SECT_FORCE);
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_UserSid));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    return enchant->OperateAtomicJob(user, m_SkillID, m_SkillLev, m_SkillSubLevel);
}

bool AtomicJobList::Do() {
    L2_GUARD_FUNCTION();
    for (AtomicJob* const job : mJobVector) {
        if (job != nullptr) {
            job->Do();
        }
    }
    return true;
}

void AtomicJobList::Destroy() {
    L2_GUARD_FUNCTION();
    for (AtomicJob* const job : mJobVector) {
        if (job != nullptr) {
            job->Destroy();
        }
    }
    mJobVector.clear();
    AtomicJob::Destroy();
}

void AtomicJobList::Dump() {
    L2_GUARD_FUNCTION();
    for (AtomicJob* const job : mJobVector) {
        if (job != nullptr) {
            job->Dump();
        }
    }
}

bool AtomicJobList::CanDo() {
    L2_GUARD_FUNCTION();
    for (AtomicJob* const job : mJobVector) {
        if (job != nullptr && !job->CanDo()) {
            return false;
        }
    }
    return true;
}

void AtomicJobList::AddAtomicJob(AtomicJob* job) { mJobVector.push_back(job); }

CAtomicItemJobCheck::CAtomicItemJobCheck(int userSID) : m_UserSID(userSID) {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ItemJobCheck;
    nLine = 0x11;
}

bool CAtomicItemJobCheck::Do() {
    L2_GUARD_FUNCTION();
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_UserSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user != nullptr) {
        user->m_nAtomicItemJob = 0;
    }
    return user != nullptr;
}

bool CAtomicItemJobCheck::CanDo() {
    L2_GUARD_FUNCTION();
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_UserSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user == nullptr || CIOObject::IsInTime(user->m_nAtomicItemJob, 60)) {
        return false;
    }

    if (user->m_nAtomicItemJob != 0) {
        const std::int32_t now = runtime::crt::Time32Now();
        const std::int32_t lag = now - user->m_nAtomicItemJob;
        if (lag > 1000) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Super Lag(%d-%d=%d) on "
            //     L"CAtomicItemJobCheck. user(%s)",
            //     L"AtomicJobServerSide.cpp",
            //     0x3d,
            //     now,
            //     user->m_nAtomicItemJob,
            //     lag,
            //     user->GetName())
            // Log.Add(
            //     LOG_WARN,
            //     L"[jobs] Item-operation gate for %ls still has an old timestamp: elapsed %d seconds (now %d, stored %d)
            //     allowing the next operation and refreshing the gate.", user->GetName(), lag, now, user->m_nAtomicItemJob)
            // L2_LOG_WARN(Log,
            //     L"[jobs] Item-operation gate for %ls still has an old timestamp: elapsed %d seconds (now %d, stored %d)
            //     allowing the next operation and refreshing the gate.", user->GetName(), lag, now, user->m_nAtomicItemJob)
            L2_LOG_WARN(
                Log,
                u"[jobs] Item-operation gate for %ls still has an old timestamp: elapsed %d seconds (now %d, stored %d); "
                u"allowing the next operation and refreshing the gate.",
                user->GetName(), lag, now, user->m_nAtomicItemJob
            );
        }
    }
    CIOObject::SetTimeStamp(const_cast<std::int32_t*>(&user->m_nAtomicItemJob));
    return true;
}

CAtomicQuestAccept::CAtomicQuestAccept(int userSID, CQuestBase* quest) : m_UserSID(userSID), m_pQuest(quest) {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::QuestAccept;
    nLine = 0xab6;
}

bool CAtomicQuestAccept::Do() {
    L2_GUARD_FUNCTION();
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_UserSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user != nullptr && m_pQuest != nullptr) {
        m_pQuest->DoAcceptOperation(*user);
    }
    return true;
}

CAtomicQuestFinish::CAtomicQuestFinish(int userSID, CQuestBase* quest) : m_UserSID(userSID), m_pQuest(quest) {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::QuestFinish;
    nLine = 0xacd;
}

bool CAtomicQuestFinish::Do() {
    L2_GUARD_FUNCTION();
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_UserSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user != nullptr && m_pQuest != nullptr) {
        m_pQuest->DoFinishOperation(*user);
    }
    return true;
}

CAtomicSaveQuestInfo::CAtomicSaveQuestInfo(int userSID) : m_UserSID(userSID) {
    // Current L2Server.exe 0x00494ddc.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::SaveQuestInfo;
    nLine = 0x55;
}

bool CAtomicSaveQuestInfo::Do() {
    // Current L2Server.exe 0x00496dec.
    L2_GUARD_FUNCTION();
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_UserSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (std::getenv("L2_QUEST_TRACE") != nullptr) {
        // std::fprintf(
        //     stderr,
        //     "[QUEST_TRACE] CAtomicSaveQuestInfo::Do user_sid=%d "
        //     "creature=%p user=%p\n",
        //     m_UserSID,
        //     static_cast<void*>(creature),
        //     static_cast<void*>(user))
        // Log.Add(LOG_NORMAL, L"[quest] Quest save for character SID %u: %ls.", static_cast<unsigned int>(m_UserSID), user
        // != nullptr ? L"requesting persistence" : L"skipped, character unavailable")
        L2_LOG_INFO(
            Log, u"[quest] Quest save for character SID %u: %ls.", static_cast<unsigned int>(m_UserSID),
            user != nullptr ? L"requesting persistence" : L"skipped; character unavailable"
        );
    }
    if (user != nullptr) {
        db.SaveQuestInfo(user);
    }
    return user != nullptr;
}
