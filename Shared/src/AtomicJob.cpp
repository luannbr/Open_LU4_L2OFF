#include "Shared/src/AtomicJob.h"
#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Exception.h"
#include "Shared/src/FHtml.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TextRuntime.h"

#if defined(L2_BUILD_L2SERVER)
#include "Protocol/src/L2Constants.h"
#include "Shared/src/UserConst.h"
#include "Shared/src/platform/Runtime.h"
#include "../../L2Server/src/SSQ.h"
#include "AgitDecoDb.h"
#include "AirShip.h"
#include "AtomicJobServerSide.h"
#include "CConfig.h"
#include "CContributeData.h"
#include "CWorld.h"
#include "Creature.h"
#include "CreatureController.h"
#include "DB.h"
#include "EventMaker.h"
#include "HTMLCache.h"
#include "InstantAgit.h"
#include "Item.h"
#include "L2Packet.h"
#include "MainEvent.h"
#include "MultiPartyCommandChannel.h"
#include "MultiPartyCommandChannelManager.h"
#include "NPC.h"
#include "NewAction.h"
#include "NpcServer.h"
#include "ObjectDB.h"
#include "OlympiadServiceProxy.h"
#include "Party.h"
#include "Pet.h"
#include "PledgeGameSystem.h"
#include "QuestHelper.h"
#include "Residence.h"
#include "Shared/src/DXPacket.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/OlympiadDefine.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/proto/DBPacket.h"
#include "Skill.h"
#include "SkillAcquire.h"
#include "SkillEnchant.h"
#include "ThreadLocalWorldMap.h"
#include "ToDoList.h"
#include "User.h"
#include "UserPacketEx.h"
#include "UserSocket.h"

#include <optional>
#endif

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cwchar>
#include <string>


namespace {

void CopyAtomicPageName(std::array<char16_t, 261>& destination, const char16_t* source) {
    if (!source) {
        destination[0] = u'\0';
        myInvalidParameterHandler(L"(L\"Invalid string pointer\" && 0)", L"wcsncpy_s", nullptr, 0, 0);
        return;
    }

    std::size_t index = 0;
    while (index < 260 && source[index] != u'\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = u'\0';
}


void CopyAtomicPetName(std::array<char16_t, 24>& destination, const char16_t* source) {
    std::size_t index = 0;
    if (source != nullptr) {
        while (index < destination.size() - 1 && source[index] != u'\0') {
            destination[index] = source[index];
            ++index;
        }
    }
    destination[index] = u'\0';
}

} // namespace

#if defined(L2_BUILD_L2SERVER)
FVector GetRandomPosition(FVector& position) {
    // Current L2Server.exe 0x004884e4. TotalChecker2015 supplies the
    // original global name/signature. The current body fixes the behavior.
    position.z = static_cast<double>(CWorld::Inst().GetGeo()->FindGround(
        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        runtime::Win64CvttDoubleToInt32(position.z)
    ));

    const double angle =
        static_cast<double>(runtime::Win64CvttDoubleToInt32(::genrand64_real2() * 360.0 + 0.0)) * 3.14 / 360.0;
    const double length = static_cast<double>(runtime::Win64CvttDoubleToInt32(::genrand64_real2() * 16.0 + 30.0));
    const double yOffset = std::sin(angle) * length;
    const double xOffset = std::cos(angle) * length;
    return position + FVector{xOffset, yOffset, 0.0};
}
#endif

int AtomicJob::Assemble(char* buffer, int bufferSize) { return 0; }

const unsigned char* AtomicJob::Disassemble(const unsigned char* packet) { return packet; }

void AtomicJob::Destroy() {
    L2_GUARD_FUNCTION();
    L2_RELEASE(this, ObjectRefType::CreateDelete);
}

void AtomicJob::Dump() { L2_GUARD_FUNCTION(); }

bool AtomicJob::CanDo() { return true; }

#if defined(L2_BUILD_L2SERVER)

AtomicCreateMPCC::AtomicCreateMPCC(const std::uint32_t channelOwnerSid, const std::uint32_t targetUserSid) :
    m_channelOwnerSid(channelOwnerSid), m_targetUserSid(targetUserSid) {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::CreateMPCC;
    nLine = 0x950;
}

bool AtomicCreateMPCC::Do() {
    // Current L2Server 0x0048ba90.
    L2_GUARD_FUNCTION();
    CCreature* const ownerObject = g_CreatureSmartIdManager.GetObjectW(m_channelOwnerSid);
    User* const channelOwner = ownerObject && ownerObject->IsPC() ? ownerObject->CastUser() : nullptr;
    CCreature* const targetObject = g_CreatureSmartIdManager.GetObjectW(m_targetUserSid);
    User* const targetUser = targetObject && targetObject->IsPC() ? targetObject->CastUser() : nullptr;
    if (channelOwner == nullptr || targetUser == nullptr) {
        return false;
    }

    CMultiPartyCommandChannelManager manager;
    manager.Open(CSPointer<User>(channelOwner, ObjectRefType::FunctionChain), MCT_ITEM);
    manager.Join(
        CSPointer<User>(channelOwner, ObjectRefType::FunctionChain),
        CSPointer<User>(targetUser, ObjectRefType::FunctionChain)
    );
    return true;
}

CAtomicCallPC::CAtomicCallPC(const std::uint32_t recalledUserSid, const std::int32_t recallerSid) :
    m_recalledUserSid(recalledUserSid), m_recallerSid(recallerSid) {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::CallPC;
    nLine = 0x936;
}

bool CAtomicCallPC::Do() {
    // Current L2Server 0x0048b9ac.
    L2_GUARD_FUNCTION();
    CCreature* const object = g_CreatureSmartIdManager.GetObjectW(m_recalledUserSid);
    User* const recalledUser = object && object->IsPC() ? object->CastUser() : nullptr;
    if (recalledUser != nullptr) {
        recalledUser->CallPCVerified(m_recallerSid);
    }
    return true;
}

bool AtomicMonsterBookReward::Do() {
    // Current L2Server 0x00493a60, original owner Shared/AtomicJob.cpp.
    L2_GUARD_FUNCTION();
    CCreature* const object = g_CreatureSmartIdManager.GetObjectW(m_userSid);
    User* const user = object != nullptr && object->IsCreature() ? object->CastUser() : nullptr;
    if (user == nullptr) {
        return false;
    }

    CSPointer<User> userReference(user, ObjectRefType::FunctionChain);
    MonsterBook::Script::Card* const card = MonsterBook::Script::inst().GetCard(m_cardId);
    if (card == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MonsterBook][AtomicJob] invalid card script "
        //     L"userId[%d] cardId[%d]",
        //     user->GetDBID(),
        //     m_cardId)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Monster-book reward skipped for character DB ID %u: card %d has no script.",
        //     user->GetDBID(),
        //     m_cardId)
        L2_LOG_ERROR(
            Log, u"[jobs] Monster-book reward skipped for character DB ID %u: card %d has no script.", user->GetDBID(),
            m_cardId
        );
        return true;
    }

    const auto clearPendingFlags = [user]() {
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        runtime::raw_atomic::ExchangeI32(&user->m_nChangeSubjobFlag, 0);
    };

    if (m_cardLevel > 3) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MonsterBook][AtomicJob] invalid card level "
        //     L"userId[%d] cardId[%d] lev[%d]",
        //     user->GetDBID(),
        //     m_cardId,
        //     m_cardLevel)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Monster-book reward rejected for character DB ID %u: card %d has invalid reward level %d (maximum
        //     3).", user->GetDBID(), m_cardId, m_cardLevel)
        L2_LOG_ERROR(
            Log,
            u"[jobs] Monster-book reward rejected for character DB ID %u: card %d has invalid reward level %d (maximum 3).",
            user->GetDBID(), m_cardId, m_cardLevel
        );
        clearPendingFlags();
        return false;
    }

    const std::size_t level = static_cast<std::size_t>(m_cardLevel);
    const std::int64_t factionPoint = card->m_reward.m_nRewardFP[level];
    const std::int64_t experience = card->m_reward.m_nRewardEXP[level];
    const std::int64_t skillPoint = card->m_reward.m_nRewardSP[level];
    bool invalidFactionIndex = false;
    {
        L2_GUARD(*user->m_lock);
        if (factionPoint > 0) {
            if (static_cast<std::uint32_t>(m_factionIndex) > 15U) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[MonsterBook][AtomicMonsterBookReward] faction "
                //     L"index has problem! user[%d] faction id[%d]",
                //     user->GetDBID(),
                //     m_factionIndex)
                // Log.Add(
                //     LOG_ERROR,
                //     L"[jobs] Monster-book faction reward rejected for character DB ID %u: faction index %d is outside
                //     0-15.", user->GetDBID(), m_factionIndex)
                L2_LOG_ERROR(
                    Log,
                    u"[jobs] Monster-book faction reward rejected for character DB ID %u: faction index %d is outside 0-15.",
                    user->GetDBID(), m_factionIndex
                );
                invalidFactionIndex = true;
            } else {
                db.RequestUpdateFactionPoint(
                    user, m_factionIndex + 1, factionPoint, protocol::Faction::MODIFY, protocol::Faction::MONSTER_BOOK
                );
            }
        }

        if (!invalidFactionIndex && experience > 0) {
            user->ExpInc(experience, false);
            user->SendSystemMessage_Q(0x2d, experience);
        }
        if (!invalidFactionIndex && skillPoint > 0) {
            user->SPInc(skillPoint);
            user->SendSystemMessage_Q(0x14b, skillPoint);
        }
    }

    if (invalidFactionIndex) {
        clearPendingFlags();
        return false;
    }

    CLogFormatter formatter;
    formatter.WriteLog1_1(
        0x1fe2, user, nullptr, nullptr, nullptr, nullptr, m_cardId, factionPoint, experience, skillPoint, 0, 0, 0
    );
    for (const ItemAmountData& item : card->m_reward.m_vItem[level]) {
        formatter.WriteLog1_1(
            0x2358, user, nullptr, nullptr, nullptr, nullptr, m_cardId, item.m_nItemClassId, item.m_nItemAmount, 0, 0, 0, 0
        );
    }

    user->OnMonsterBookRewardFinish(m_cardId, m_factionIndex);
    user->UpdateMonsterBookToDB();
    clearPendingFlags();
    return true;
}

AtomicAlchemyResult::AtomicAlchemyResult(
    const std::uint32_t userSid, const char alchemyType, const std::int32_t usedSkillId
) :
    m_nUserSID(userSid),
    m_nEffectSkillId(0),
    m_nAlchemyType(alchemyType),
    m_nUsedSkillId(usedSkillId),
    m_nSuccessInfo(0) {
    // Current L2Server 0x00b4fa54, original inline AtomicJob.h owner.
    // The target leaves +0x58 and m_ConvInfo untouched here.
    // conversion producers publish m_ConvInfo before enqueueing.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::AlchemyResult;
    nLine = 0xc39;
}

void AtomicAlchemyResult::AddRollyRewardInfo(const char type, const std::int32_t itemClassId, const std::int64_t amount) {
    // Current L2Server 0x004924a0.
    L2_GUARD_FUNCTION();
    ItemRollyRewardType reward;
    reward.nType = type;
    reward.nItemClassId = itemClassId;
    reward.nItemAmount = amount;
    m_vRollyRewardList.push_back(reward);
}

void AtomicAlchemyResult::SetConvRewardInfo(const std::int32_t success, const std::int32_t fail) {
    // Current L2Server 0x00486f54.
    L2_GUARD_FUNCTION();
    m_ConvInfo.nSuccess = success;
    m_ConvInfo.nFail = fail;
}

void AtomicAlchemyResult::Dump() {
    // Current L2Server 0x00486eb0.
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"AtomicAlchemyResult fail!!!!")
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for alchemy result: character SID %u, type %d, used skill %d.",
    //     static_cast<unsigned int>(m_nUserSID),
    //     static_cast<int>(m_nAlchemyType),
    //     m_nUsedSkillId)
    L2_LOG_INFO(
        Log, u"[jobs] Parameters for alchemy result: character SID %u, type %d, used skill %d.",
        static_cast<unsigned int>(m_nUserSID), static_cast<int>(m_nAlchemyType), m_nUsedSkillId
    );
}

bool AtomicAlchemyResult::CanDo() {
    // Current L2Server 0x0048daac.
    L2_GUARD_FUNCTION();
    CCreature* const object = g_CreatureSmartIdManager.GetObjectW(m_nUserSID);
    User* const user = object != nullptr && object->IsPC() ? object->CastUser() : nullptr;
    return user != nullptr;
}

bool AtomicAlchemyResult::Do() {
    // Current L2Server 0x00494790, original owner Shared/AtomicJob.cpp.
    L2_GUARD_FUNCTION();
    CCreature* const object = g_CreatureSmartIdManager.GetObjectW(m_nUserSID);
    User* const user = object != nullptr && object->IsPC() ? object->CastUser() : nullptr;
    if (user == nullptr) {
        return true;
    }

    const int alchemyType = static_cast<int>(m_nAlchemyType);
    L2_GUARD(user->m_AlchemySkillsLock[alchemyType]);
    auto& skills = user->m_AlchemySkills[alchemyType];
    const auto found = skills.find(m_nUsedSkillId);
    if (found == skills.end()) {
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        return false;
    }

    CSkillInfo skill(*found->second.pSkillInfo);
    user->SetSkillReuseDelay(skill);
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);

    if (m_nEffectSkillId != 0) {
        SkillUseInfo use;
        use.m_skillID = m_nEffectSkillId;
        use.m_skillLevel = 1;
        use.m_skillSubLevel = 0;
        use.m_castRange = 0;
        use.m_type = 0;
        use.m_param1 = 0;
        int first = 0;
        int second = 500;
        int third = 0;
        int fourth = 1500;
        int fifth = 0;
        int sixth = 0;
        DefaultIOBuffer* const packet =
            user->MakePacketForMagicSkillUse(first, user, use, second, third, fourth, fifth, sixth, nullptr);
        g_ptlUserMap->BroadcastToNeighborDist(user, 0xf000, user->GetPos(), user->GetBroadcastDist(0x1000), packet);
        packet->Release();
    }

    // if (m_nAlchemyType == 1) {
    if (m_nAlchemyType == AlchemyConst::AT_CONV) {
        protocol::S_AlchemyConversion response{};
        response.nSuccessInfo = m_nSuccessInfo;
        response.nSuccessCount = m_ConvInfo.nSuccess;
        response.nFailCount = m_ConvInfo.nFail;
        user->GetSocket()->Send(S_EX, S_EX_REQUEST_ALCHEMY_CONVERSION, response);
        // } else if (m_nAlchemyType == 0) {
    } else if (m_nAlchemyType == AlchemyConst::AT_ROLLY) {
        protocol::S_AlchemyTryMixCube response{};
        response.nSuccessInfo = m_nSuccessInfo;
        for (const ItemRollyRewardType& reward : m_vRollyRewardList) {
            protocol::ItemRollyResultInfo result{};
            result.nType = reward.nType;
            result.nItemClassId = reward.nItemClassId;
            result.nAmount = reward.nItemAmount;
            response.resultList.push_back(result);
        }
        user->GetSocket()->Send(S_EX, S_EX_TRY_MIX_CUBE, response);
    }
    return true;
}

#endif

AtomicPetDBJob::AtomicPetDBJob() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::PetDBJob;
    nLine = 0xcaa;
}

int AtomicPetDBJob::Assemble(char*, int) { return 0; }

const unsigned char* AtomicPetDBJob::Disassemble(const unsigned char*) { return nullptr; }

bool AtomicPetDBJob::Do() { return true; }

void AtomicPetDBJob::Dump() {}

bool AtomicPetDBJob::CanDo() { return true; }

AtomicLambdaJob::AtomicLambdaJob() : m_nResult(0) {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::LambdaJob;
    nLine = 0xd5a;
}

AtomicLambdaJob::~AtomicLambdaJob() = default;

AtomicLambdaJobDx::AtomicLambdaJobDx() : m_pDx(nullptr) {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::LambdaJobDx;
    nLine = 0xd88;
}

AtomicLambdaJobDx::~AtomicLambdaJobDx() = default;

AtomicLambdaJobPacket::AtomicLambdaJobPacket() : m_pPacket(nullptr) {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::LambdaJobPacket;
    nLine = 0xdb5;
}

AtomicLambdaJobPacket::~AtomicLambdaJobPacket() = default;

AtomicCreatePet::AtomicCreatePet() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::CreatePet;
    nLine = 0x86;
}

AtomicCreatePet::AtomicCreatePet(unsigned int creatureSID, int petCollarClassId, int petNpcClassId, int petLevel) :
    mCreatureSID(creatureSID), mPetCollarClassId(petCollarClassId), mPetNpcClassId(petNpcClassId), mPetLevel(petLevel) {
    // Current EXE constructor sequence in CNPC::CreatePet at 0x004a0238.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::CreatePet;
    nLine = 0x86;
}

int AtomicCreatePet::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0040ebf8.
    return ::Assemble(
        buffer, bufferSize, "hdddd", 1, static_cast<int>(mCreatureSID), mPetCollarClassId, mPetNpcClassId, mPetLevel
    );
}

const unsigned char* AtomicCreatePet::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0040ecd8.
    return ::Disassemble(packet, "dddd", &mCreatureSID, &mPetCollarClassId, &mPetNpcClassId, &mPetLevel);
}

bool AtomicCreatePet::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x004886c8.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(mCreatureSID);
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user != nullptr) {
        return CPet::CreatePet(user, mPetCollarClassId, mPetNpcClassId, mPetLevel);
    }
    return false;
#else
    // Current L2NPC.exe 0x0040ed9c.
    return false;
#endif
}

void AtomicCreatePet::Dump() {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0040ee1c.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AtomicCreatePet::Dump() => "
    //     L"PetCollarClassId=[%d], PetNpcClassId=[%d], PetLevel=[%d]",
    //     mPetCollarClassId,
    //     mPetNpcClassId,
    //     mPetLevel)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for pet creation: collar template %d, NPC template %d, level %d.",
    //     mPetCollarClassId,
    //     mPetNpcClassId,
    //     mPetLevel)
    L2_LOG_INFO(
        Log, u"[jobs] Parameters for pet creation: collar template %d, NPC template %d, level %d.", mPetCollarClassId,
        mPetNpcClassId, mPetLevel
    );
}

AtomicBuyLottoTicket::AtomicBuyLottoTicket() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::BuyLottoTicket;
    nLine = 0xfa;
}

AtomicBuyLottoTicket::AtomicBuyLottoTicket(unsigned int creatureSID, int roundNumber, int numberFlag) :
    CreatureSID(creatureSID), RoundNumber(roundNumber), NumberFlag(numberFlag) {
    // Current EXE's inline constructor sequence in CNPC::Lotto_BuyTicket at
    // 0x004a1528.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::BuyLottoTicket;
    nLine = 0xfa;
}

int AtomicBuyLottoTicket::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0040f1b8.
    return ::Assemble(buffer, bufferSize, "hddd", 4, static_cast<int>(CreatureSID), RoundNumber, NumberFlag);
}

const unsigned char* AtomicBuyLottoTicket::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0040f290.
    return ::Disassemble(packet, "ddd", &CreatureSID, &RoundNumber, &NumberFlag);
}

bool AtomicBuyLottoTicket::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x004800d0. L2Server executes this virtual slot
    // through AtomicJobList::Do. The L2NPC body below is an unused transport
    // stub in that product.
    return true;
#else
    // Current L2NPC.exe 0x0040f34c.
    return false;
#endif
}

AtomicEvolvePet::AtomicEvolvePet() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::EvolvePet;
    nLine = 0x12f;
}

AtomicEvolvePet::AtomicEvolvePet(
    unsigned int creatureSID,
    int originalPetCollarDbId,
    int originalPetNpcClassId,
    int petCollarClassId,
    int petNpcClassId,
    int petLevel
) :
    mCreatureSID(creatureSID),
    mOriginalPetCollarDbId(originalPetCollarDbId),
    mOriginalPetNpcClassId(originalPetNpcClassId),
    mPetCollarClassId(petCollarClassId),
    mPetNpcClassId(petNpcClassId),
    mPetLevel(petLevel) {
    // Current EXE constructor sequence in CNPC::EvolvePet at 0x004cbae0.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::EvolvePet;
    nLine = 0x12f;
}

int AtomicEvolvePet::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0040f3cc.
    return ::Assemble(
        buffer, bufferSize, "hdddddd", 2, static_cast<int>(mCreatureSID), mOriginalPetCollarDbId, mOriginalPetNpcClassId,
        mPetCollarClassId, mPetNpcClassId, mPetLevel
    );
}

const unsigned char* AtomicEvolvePet::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0040f4b8.
    return ::Disassemble(
        packet, "dddddd", &mCreatureSID, &mOriginalPetCollarDbId, &mOriginalPetNpcClassId, &mPetCollarClassId,
        &mPetNpcClassId, &mPetLevel
    );
}

bool AtomicEvolvePet::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x00488988.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(mCreatureSID);
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user == nullptr) {
        return false;
    }

    CItem* const item = user->m_inv.GetByDBID(mOriginalPetCollarDbId);
    if (item == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] EvolvePet failed. Invalid pet collar DBID[%d]",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x150,
        //     mOriginalPetCollarDbId)
        // Log.Add(
        //     LOG_WARN,
        //     L"[jobs] Pet evolution skipped: collar DB ID %d was not found.",
        //     mOriginalPetCollarDbId)
        L2_LOG_WARN(Log, u"[jobs] Pet evolution skipped: collar DB ID %d was not found.", mOriginalPetCollarDbId);
        return false;
    }
    if (!item->IsPetCollarItem()) {
        return false;
    }

    CEtcItem* const petCollar = item->CastEtcItem();
    CObject* const petObject = g_ObjectDB.GetObjectW(mPetNpcClassId, false);
    if (petObject == nullptr || !petObject->IsPet()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] EvolvePet failed. Invalid npc class[%d]",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x163,
        //     mPetNpcClassId)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Pet evolution rejected: NPC template %d is missing or is not a pet.",
        //     mPetNpcClassId)
        L2_LOG_ERROR(Log, u"[jobs] Pet evolution rejected: NPC template %d is missing or is not a pet.", mPetNpcClassId);
        return false;
    }

    CPet* const newPet = petObject->CastPet();
    if (!petCollar->m_bPetCollarTradable) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] EvolvePet failed. Pet is summoned. Pet collar DBID[%d]",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x16f,
        //     mOriginalPetCollarDbId)
        // Log.Add(
        //     LOG_WARN,
        //     L"[jobs] Pet evolution rejected: pet with collar DB ID %d is still summoned.",
        //     mOriginalPetCollarDbId)
        L2_LOG_WARN(
            Log, u"[jobs] Pet evolution rejected: pet with collar DB ID %d is still summoned.", mOriginalPetCollarDbId
        );
        return false;
    }

    int newLevel = mPetLevel;
    if (newLevel < 1) {
        newLevel = newPet->m_data->m_nLevel;
    }
    PetDataSheet* const petDataSheet = PetDataSheetTable::Inst().GetPetDataSheetByIndex(newPet->GetPetIndex());
    const std::int64_t newExperience = petDataSheet->GetRecord(newLevel)->mExp;
    const int maximumMeal = petDataSheet->GetRecord(newLevel)->mMaxMeal;
    PetDataSheet* const oldNpcDataSheet = PetDataSheetTable::Inst().FindDataSheet(mOriginalPetNpcClassId);
    PetDataSheet* const newNpcDataSheet = PetDataSheetTable::Inst().FindDataSheet(mPetNpcClassId);
    if (oldNpcDataSheet == nullptr || newNpcDataSheet == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] EvolvePet failed. Invalid Pet Npc Class Id, Old[%d], New[%d]",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x185,
        //     mOriginalPetNpcClassId,
        //     mPetNpcClassId)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Pet evolution rejected: missing pet data for old NPC template %d or new NPC template %d.",
        //     mOriginalPetNpcClassId,
        //     mPetNpcClassId)
        L2_LOG_ERROR(
            Log, u"[jobs] Pet evolution rejected: missing pet data for old NPC template %d or new NPC template %d.",
            mOriginalPetNpcClassId, mPetNpcClassId
        );
        return false;
    }

    const int oldLevel = petCollar->m_data->GetEnchanted();
    std::int64_t oldExperienceBegin = 0;
    std::int64_t oldExperienceEnd = 0;
    if (static_cast<std::uint32_t>(oldLevel) - 1U < 120U) {
        oldExperienceBegin = oldNpcDataSheet->mRecords[oldLevel].mExp;
        oldExperienceEnd = runtime::SubtractInt64Wrapping(oldNpcDataSheet->mRecords[oldLevel + 1].mExp, 1);
    }

    std::int64_t newExperienceBegin = 0;
    std::int64_t newExperienceEnd = 0;
    if (static_cast<std::uint32_t>(newLevel) - 1U < 120U) {
        newExperienceBegin = newNpcDataSheet->mRecords[newLevel].mExp;
        newExperienceEnd = runtime::SubtractInt64Wrapping(newNpcDataSheet->mRecords[newLevel + 1].mExp, 1);
    }

    db.EvolvePet(
        user, petCollar, mPetCollarClassId, mPetNpcClassId, newExperience, newLevel, newPet->m_data->m_nLevel, 0.0, 0.0,
        maximumMeal, oldExperienceBegin, oldExperienceEnd, newExperienceBegin, newExperienceEnd
    );
    return true;
#else
    // Current L2NPC.exe 0x0040f660.
    return false;
#endif
}

void AtomicEvolvePet::Dump() {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0040f590.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AtomicEvolvePet::Dump() => "
    //     L"mOriginalPetCollarDbId=[%d], "
    //     L"mOriginalPetNpcClassId=[%d],"
    //     L"mPetCollarClassId=[%d], mPetNpcClassId=[%d], "
    //     L"mPetLevel=[%d]",
    //     mOriginalPetCollarDbId,
    //     mOriginalPetNpcClassId,
    //     mPetCollarClassId,
    //     mPetNpcClassId,
    //     mPetLevel)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for pet evolution: old collar DB ID %d, old NPC template %d, new collar template %d, new NPC
    //     template %d, level %d.", mOriginalPetCollarDbId, mOriginalPetNpcClassId, mPetCollarClassId, mPetNpcClassId,
    //     mPetLevel)
    L2_LOG_INFO(
        Log,
        u"[jobs] Parameters for pet evolution: old collar DB ID %d, old NPC template %d, new collar template %d, new NPC "
        u"template %d, level %d.",
        mOriginalPetCollarDbId, mOriginalPetNpcClassId, mPetCollarClassId, mPetNpcClassId, mPetLevel
    );
}

AtomicEvolvePetWithSameExp::AtomicEvolvePetWithSameExp() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::EvolvePetWithSameExp;
    nLine = 0x16e;
}

AtomicEvolvePetWithSameExp::AtomicEvolvePetWithSameExp(unsigned int creatureSID, int newPetIndex) :
    m_creatureSID(creatureSID), m_newPetIndex(newPetIndex) {
    // Current EXE constructor sequence in CNPC::EvolvePetWithSameExp at
    // 0x004cbe48.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::EvolvePetWithSameExp;
    nLine = 0x16e;
}

int AtomicEvolvePetWithSameExp::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0040f6e0.
    return ::Assemble(buffer, bufferSize, "hdd", 0x24, static_cast<int>(m_creatureSID), m_newPetIndex);
}

const unsigned char* AtomicEvolvePetWithSameExp::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0040f7b0.
    return ::Disassemble(packet, "dd", &m_creatureSID, &m_newPetIndex);
}

bool AtomicEvolvePetWithSameExp::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x00488d40.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(m_creatureSID);
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user == nullptr || !user->HavePet()) {
        return false;
    }

    CPet* pet = user->GetPet();
    const int petCollarDatabaseId = static_cast<int>(pet->GetDBID());
    CItem* const item = user->m_inv.GetByDBID(petCollarDatabaseId);
    if (item == nullptr || !item->IsPetCollarItem()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] EvolvePetWithSameExp failed. Invalid pet collar DBID[%d]",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x1cc,
        //     petCollarDatabaseId)
        // Log.Add(
        //     LOG_WARN,
        //     L"[jobs] Pet evolution with retained experience skipped: collar DB ID %d was not found.",
        //     petCollarDatabaseId)
        L2_LOG_WARN(
            Log, u"[jobs] Pet evolution with retained experience skipped: collar DB ID %d was not found.",
            petCollarDatabaseId
        );
        return false;
    }

    CEtcItem* const petCollar = item->CastEtcItem();
    pet = user->GetPet();
    if (petCollar->m_bPetCollarTradable || pet == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] EvolvePetWithSameExp failed. Pet is not summoned. Pet collar DBID[%d]",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x1d6,
        //     petCollarDatabaseId)
        // Log.Add(
        //     LOG_WARN,
        //     L"[jobs] Pet evolution with retained experience rejected: pet with collar DB ID %d is not summoned.",
        //     petCollarDatabaseId)
        L2_LOG_WARN(
            Log, u"[jobs] Pet evolution with retained experience rejected: pet with collar DB ID %d is not summoned.",
            petCollarDatabaseId
        );
        return false;
    }

    const int itemClassId = PetDataSheetTable::Inst().ItemClassIdFromIndex(m_newPetIndex);
    const int npcClassId = PetDataSheetTable::Inst().NpcClassIdFromIndex(m_newPetIndex);
    CObject* const petObject = g_ObjectDB.GetObjectW(npcClassId, false);
    if (petObject == nullptr || !petObject->IsPet()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] EvolvePetWithSameExp failed. Invalid npc class[%d]",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x1e2,
        //     npcClassId)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Pet evolution with retained experience rejected: NPC template %d is missing or is not a pet.",
        //     npcClassId)
        L2_LOG_ERROR(
            Log, u"[jobs] Pet evolution with retained experience rejected: NPC template %d is missing or is not a pet.",
            npcClassId
        );
        return false;
    }

    const std::int64_t experience = pet->m_data->m_nExp;
    const int newLevel = CPet::GetLevel(m_newPetIndex, experience);
    PetDataSheet* const petDataSheet = PetDataSheetTable::Inst().GetPetDataSheetByIndex(m_newPetIndex);
    const int maximumMeal = petDataSheet->GetRecord(newLevel)->mMaxMeal;
    const int oldLevel = pet->Level();
    db.EvolvePetWithSameExp(user, petCollar, itemClassId, npcClassId, experience, newLevel, oldLevel, maximumMeal);
    return true;
#else
    // Current L2NPC.exe 0x0040f918.
    return false;
#endif
}

void AtomicEvolvePetWithSameExp::Dump() {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0040f864.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AtomicEvolvePetWithSameExp::Dump() => "
    //     L"m_newPetIndex=[%d]",
    //     m_newPetIndex)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for pet evolution with retained experience: new pet index %d.",
    //     m_newPetIndex)
    L2_LOG_INFO(Log, u"[jobs] Parameters for pet evolution with retained experience: new pet index %d.", m_newPetIndex);
}

AtomicDestroyPet::AtomicDestroyPet() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::DestroyPet;
    nLine = 0x1a5;
}

AtomicDestroyPet::AtomicDestroyPet(unsigned int creatureSID, int petCollarDbId, int level) :
    mCreatureSID(creatureSID), mPetCollarDbId(petCollarDbId), mLevel(level) {
    // Current EXE constructor sequence in CNPC::DestroyPet at 0x004cbcac.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::DestroyPet;
    nLine = 0x1a5;
}

int AtomicDestroyPet::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0040f998.
    return ::Assemble(buffer, bufferSize, "hddd", 3, static_cast<int>(mCreatureSID), mPetCollarDbId, mLevel);
}

const unsigned char* AtomicDestroyPet::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0040fa70.
    return ::Disassemble(packet, "ddd", &mCreatureSID, &mPetCollarDbId, &mLevel);
}

bool AtomicDestroyPet::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048901c.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(mCreatureSID);
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user == nullptr) {
        return false;
    }

    CItem* const item = user->m_inv.GetByDBID(mPetCollarDbId);
    if (item == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] DestroyPet failed. Invalid pet collar DBID[%d]",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x229,
        //     mPetCollarDbId)
        // Log.Add(
        //     LOG_WARN,
        //     L"[jobs] Pet destruction skipped: collar DB ID %d was not found.",
        //     mPetCollarDbId)
        L2_LOG_WARN(Log, u"[jobs] Pet destruction skipped: collar DB ID %d was not found.", mPetCollarDbId);
        return false;
    }
    if (!item->IsEtcItem() || item->m_const->m_EtcitemType != 7) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] DestroyPet failed. Is not pet collar. Pet collar DBID[%d]",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x233,
        //     mPetCollarDbId)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Pet destruction rejected: item DB ID %d is not a pet collar.",
        //     mPetCollarDbId)
        L2_LOG_ERROR(Log, u"[jobs] Pet destruction rejected: item DB ID %d is not a pet collar.", mPetCollarDbId);
        return false;
    }

    CEtcItem* const petCollar = item->CastEtcItem();
    if (!petCollar->m_bPetCollarTradable) {
        if (mLevel != -99) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] DestroyPet failed. Pet is summoned. Pet collar DBID[%d]",
            //     L"..\\Shared\\AtomicJob.cpp",
            //     0x247,
            //     mPetCollarDbId)
            // Log.Add(
            //     LOG_WARN,
            //     L"[jobs] Pet destruction rejected: pet with collar DB ID %d is still summoned.",
            //     mPetCollarDbId)
            L2_LOG_WARN(
                Log, u"[jobs] Pet destruction rejected: pet with collar DB ID %d is still summoned.", mPetCollarDbId
            );
            return false;
        }
        user->DespawnPet(true, true, DESPAWN_PET_NONE, true);
    } else {
        const int userServerId = static_cast<int>(user->Id());
        const int worldId = user->m_data->m_nWorldId;
        const int petCollarDatabaseId = static_cast<int>(item->GetDBID());
        const int userDatabaseId = static_cast<int>(user->GetDBID());
        db.DeletePet(worldId, userServerId, userDatabaseId, petCollarDatabaseId, 1, mLevel);
    }
    return true;
#else
    // Current L2NPC.exe 0x0040fbe0.
    return false;
#endif
}

void AtomicDestroyPet::Dump() {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0040fb2c retains the "Destory" typo.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AtomicDestoryPet::Dump() => mPetCollarDbId=[%d]",
    //     mPetCollarDbId)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for pet destruction: collar DB ID %d.",
    //     mPetCollarDbId)
    L2_LOG_INFO(Log, u"[jobs] Parameters for pet destruction: collar DB ID %d.", mPetCollarDbId);
}

MemoControl::MemoControl() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::AMemoControl;
    nLine = 0x261;
}

MemoControl::MemoControl(unsigned int creatureSID, TYPE controlType, int questId, int argument1, int argument2) :
    mCreatureSID(creatureSID), mControlType(controlType), mQuestId(questId), mArg1(argument1), mArg2(argument2) {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::AMemoControl;
    nLine = 0x261;
}

int MemoControl::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0040fc60.
    return ::Assemble(
        buffer, bufferSize, "hddddd", 5, static_cast<int>(mCreatureSID), static_cast<int>(mControlType), mQuestId, mArg1,
        mArg2
    );
}

const unsigned char* MemoControl::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0040fd44.
    return ::Disassemble(packet, "ddddd", &mCreatureSID, &mControlType, &mQuestId, &mArg1, &mArg2);
}

bool MemoControl::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x004897e8.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(mCreatureSID);
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user == nullptr) {
        return false;
    }

    {
        L2_GUARD(*user->m_lock);
        switch (mControlType) {
            case SET_MEMO:
                user->m_data->SetMemo(mQuestId);
                break;
            case SET_STATE:
                if (user->m_data->SetMemoState(mQuestId, mArg1, mArg2)) {
                    const int state0 = user->m_data->GetMemoState(mQuestId, 0);
                    const int state1 = user->m_data->GetMemoState(mQuestId, 1);
                    const int state2 = user->m_data->GetMemoState(mQuestId, 2);
                    const int state3 = user->m_data->GetMemoState(mQuestId, 3);
                    CLogFormatter formatter;
                    formatter.WriteLog2(0x130, user, nullptr, mQuestId, state0, state1, state2, state3, 0, 0);
                }
                break;
            case SET_JOURNAL:
                user->m_data->SetJournal(mQuestId, mArg1);
                break;
            case SET_FJOURNAL:
                user->m_data->SetFlagJournal(mQuestId, mArg1);
                break;
            case RESET_FJOURNAL:
                user->m_data->ResetFlagJournal(mQuestId, mArg1);
                break;
            case REMOVE:
                if (user->m_data->RemoveMemo(mQuestId)) {
                    user->PledgeMissionCheckAndAddCount(PledgeMission::PMGT_QUEST, mQuestId, 1);
                    if ((mQuestId < 0 || mQuestId > 0xfe) && (mQuestId < 0x280f || mQuestId > 0x2f0f)) {
                        user->PledgeMissionCheckAndAddCount(PledgeMission::PMGT_QUEST_ANY, 0, 1);
                    }
                }
                if (g_ToDoListManager.IsDailyMissionQuest(mQuestId)) {
                    g_ToDoListManager.OnDailyMissionCheckAndUpdate(user, E_DAILY_MISSION_TYPE_QUESTCOUNT);
                }
                break;
            case SET_ONETIME_QUEST_FLAG: {
                user->m_data->SetOneTimeFlag(mQuestId, mArg1 != 0);
                CLogFormatter formatter;
                formatter.WriteLog2(0x132, user, nullptr, mQuestId, mArg1, 0, 0, 0, 0, 0);
                for (std::int16_t& questFlag : user->m_data->m_AtomicQuestFlag) {
                    if (questFlag == mQuestId) {
                        questFlag = 0;
                    }
                }
                break;
            }
            default:
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] MemoControl Failed. Undefined control type[%d]",
                //     L"..\\Shared\\AtomicJob.cpp",
                //     0x3a6,
                //     static_cast<int>(mControlType))
                // Log.Add(
                //     LOG_ERROR,
                //     L"[jobs] Quest memo control type %d is unsupported. No memo change made, quest-list response still
                //     follows.", static_cast<int>(mControlType))
                L2_LOG_ERROR(
                    Log,
                    u"[jobs] Quest memo control type %d is unsupported; no memo change made, quest-list response still "
                    u"follows.",
                    static_cast<int>(mControlType)
                );
                break;
        }
    }
    user->SendQuestList(nullptr);
    return true;
#else
    // Current L2NPC.exe 0x0040fedc.
    return false;
#endif
}

void MemoControl::Dump() {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_NORMAL,
    //     L"MemoControl::Dump() => "
    //     L"mControlType=[%d], mQuestId=[%d], "
    //     L"mArg1=[%d], mArg2=[%d]",
    //     static_cast<int>(mControlType),
    //     mQuestId,
    //     mArg1,
    //     mArg2)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for quest memo control: operation %d, quest %d, arguments %d and %d.",
    //     static_cast<int>(mControlType),
    //     mQuestId,
    //     mArg1,
    //     mArg2)
    L2_LOG_INFO(
        Log, u"[jobs] Parameters for quest memo control: operation %d, quest %d, arguments %d and %d.",
        static_cast<int>(mControlType), mQuestId, mArg1, mArg2
    );
}

NRMemoControl::NRMemoControl() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ANRMemoControl;
    nLine = 0x2b7;
}

NRMemoControl::NRMemoControl(unsigned int creatureSID, TYPE controlType, int questId, int argument1, int argument2) :
    mCreatureSID(creatureSID), mControlType(controlType), mQuestId(questId), mArg1(argument1), mArg2(argument2) {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ANRMemoControl;
    nLine = 0x2b7;
}

int NRMemoControl::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0040ff5c.
    return ::Assemble(
        buffer, bufferSize, "hddddd", 0x2b, static_cast<int>(mCreatureSID), static_cast<int>(mControlType), mQuestId, mArg1,
        mArg2
    );
}

const unsigned char* NRMemoControl::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00410040.
    return ::Disassemble(packet, "ddddd", &mCreatureSID, &mControlType, &mQuestId, &mArg1, &mArg2);
}

bool NRMemoControl::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x00489b90.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(mCreatureSID);
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user == nullptr) {
        return false;
    }

    {
        L2_GUARD(*user->m_lock);
        switch (mControlType) {
            case SET_MEMO:
                user->m_data->SetNRMemo(mQuestId);
                break;
            case SET_STATE:
                if (user->m_data->SetNRMemoState(mQuestId, mArg1, mArg2)) {
                    const int state0 = user->m_data->GetMemoState(mQuestId, 0);
                    const int state1 = user->m_data->GetMemoState(mQuestId, 1);
                    const int state2 = user->m_data->GetMemoState(mQuestId, 2);
                    const int state3 = user->m_data->GetMemoState(mQuestId, 3);
                    CLogFormatter formatter;
                    formatter.WriteLog2(0x130, user, nullptr, mQuestId, state0, state1, state2, state3, 0, 0);
                }
                break;
            case SET_JOURNAL:
                user->m_data->SetNRJournal(mQuestId, mArg1);
                break;
            case SET_FJOURNAL:
                user->m_data->SetNRFlagJournal(mQuestId, mArg1);
                break;
            case RESET_FJOURNAL:
                user->m_data->ResetNRFlagJournal(mQuestId, mArg1);
                break;
            case REMOVE:
                if (user->m_data->RemoveNRMemo(mQuestId)) {
                    db.RequestDeleteNRMemo(*user, mQuestId);
                }
                break;
            case SET_ONETIME_QUEST_FLAG: {
                user->m_data->SetOneTimeFlag(mQuestId, mArg1 != 0);
                CLogFormatter formatter;
                formatter.WriteLog2(0x132, user, nullptr, mQuestId, mArg1, 0, 0, 0, 0, 0);
                break;
            }
            default:
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] NRMemoControl Failed. Undefined control type[%d]",
                //     L"..\\Shared\\AtomicJob.cpp",
                //     0x41e,
                //     static_cast<int>(mControlType))
                // Log.Add(
                //     LOG_ERROR,
                //     L"[jobs] NR quest memo control type %d is unsupported. No memo change made, quest-list response still
                //     follows.", static_cast<int>(mControlType))
                L2_LOG_ERROR(
                    Log,
                    u"[jobs] NR quest memo control type %d is unsupported; no memo change made, quest-list response still "
                    u"follows.",
                    static_cast<int>(mControlType)
                );
                break;
        }
    }
    user->SendQuestList(nullptr);
    return true;
#else
    // Current L2NPC.exe 0x004101d8.
    return false;
#endif
}

void NRMemoControl::Dump() {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_NORMAL,
    //     L"NRMemoControl::Dump() => "
    //     L"mControlType=[%d], mQuestId=[%d], "
    //     L"mArg1=[%d], mArg2=[%d]",
    //     static_cast<int>(mControlType),
    //     mQuestId,
    //     mArg1,
    //     mArg2)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for NR quest memo control: operation %d, quest %d, arguments %d and %d.",
    //     static_cast<int>(mControlType),
    //     mQuestId,
    //     mArg1,
    //     mArg2)
    L2_LOG_INFO(
        Log, u"[jobs] Parameters for NR quest memo control: operation %d, quest %d, arguments %d and %d.",
        static_cast<int>(mControlType), mQuestId, mArg1, mArg2
    );
}

AtomicShowQuestPage::AtomicShowQuestPage() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ShowQuestPage;
    nLine = 0x2f1;
}

AtomicShowQuestPage::AtomicShowQuestPage(
    unsigned int creatureSID, unsigned int npcIndex, int questId, const char16_t* fileName
) :
    mCreatureSID(creatureSID), mNpcIndex(npcIndex), mQuestId(questId) {
    // Current EXE constructor helper 0x00489070 copies at most 0x104 UTF-16
    // code units into the fixed 0x105-code-unit carrier.
    CopyAtomicPageName(mFileName, fileName);
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ShowQuestPage;
    nLine = 0x2f1;
}

int AtomicShowQuestPage::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00410258.
    return ::Assemble(
        buffer, bufferSize, "hdddS", 0x2c, static_cast<int>(mCreatureSID), static_cast<int>(mNpcIndex), mQuestId,
        mFileName.data()
    );
}

const unsigned char* AtomicShowQuestPage::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00410338.
    return ::Disassemble(packet, "dddS", &mCreatureSID, &mNpcIndex, &mQuestId, 0x20a, mFileName.data());
}

bool AtomicShowQuestPage::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048ffc0.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(mCreatureSID);
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"pUser is NULL mCreatureSID[%d]",
        //     static_cast<int>(mCreatureSID))
        // Log.Add(
        //     LOG_WARN,
        //     L"[jobs] Quest HTML skipped: character SID %u is unavailable.",
        //     static_cast<unsigned int>(mCreatureSID))
        L2_LOG_WARN(
            Log, u"[jobs] Quest HTML skipped: character SID %u is unavailable.", static_cast<unsigned int>(mCreatureSID)
        );
        return false;
    }

    const char16_t* const targetFileName = mFileName.data();
    if (targetFileName == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] html file name is null.",
        //     "..\\Shared\\AtomicJob.cpp",
        //     0x457)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Quest HTML filename is null, loading continues through the existing path.")
        L2_LOG_ERROR(Log, u"[jobs] Quest HTML filename is null; loading continues through the existing path.");
    }
    const std::wstring fileName = target_utf16::ToHostCodeUnits(targetFileName);
    std::u16string html;
    if (!g_HTMLCache.GetHTMLFile(targetFileName, html, user->m_data->m_CountryCode)) {
        return false;
    }

    CCreature* const talkTarget = user->m_spTalkTarget.GetCreature();
    if (talkTarget == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"pTalkTargetNPC is NULL / UserName[%s]",
        //     user->GetName())
        // Log.Add(
        //     LOG_WARN,
        //     L"[jobs] Quest HTML skipped for %ls: no NPC talk target.",
        //     user->GetName())
        // L2_LOG_WARN(Log,
        //     L"[jobs] Quest HTML skipped for %ls: no NPC talk target.",
        //     user->GetName())
        L2_LOG_WARN(Log, u"[jobs] Quest HTML skipped for %ls: no NPC talk target.", user->GetName());
        return false;
    }
    if (talkTarget->m_data->index != static_cast<int>(mNpcIndex)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"pTalkTargetNPC Index not Equal "
        //     L"pTalkTargetNPC->m_data->index[%d]/"
        //     L"mNpcIndex[%d]/UserName[%s]",
        //     talkTarget->m_data->index,
        //     static_cast<int>(mNpcIndex),
        //     user->GetName())
        // Log.Add(
        //     LOG_WARN,
        //     L"[jobs] Quest HTML skipped for %ls: current NPC index %d differs from requested index %d.",
        //     user->GetName(),
        //     talkTarget->m_data->index,
        //     static_cast<int>(mNpcIndex))
        // L2_LOG_WARN(Log,
        //     L"[jobs] Quest HTML skipped for %ls: current NPC index %d differs from requested index %d.",
        //     user->GetName(),
        //     talkTarget->m_data->index,
        //     static_cast<int>(mNpcIndex))
        L2_LOG_WARN(
            Log, u"[jobs] Quest HTML skipped for %ls: current NPC index %d differs from requested index %d.",
            user->GetName(), talkTarget->m_data->index, static_cast<int>(mNpcIndex)
        );
        return false;
    }
    if (targetFileName == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] html file name is null. "
        //     L"npc[%s], UserName[%s]",
        //     "..\\Shared\\AtomicJob.cpp",
        //     0x46f,
        //     talkTarget->GetName(),
        //     user->GetName())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Quest HTML filename is null for NPC %ls and character %ls. Display continues through the existing
        //     path.", talkTarget->GetName(), user->GetName())
        // L2_LOG_ERROR(Log,
        //     L"[jobs] Quest HTML filename is null for NPC %ls and character %ls. Display continues through the existing
        //     path.", talkTarget->GetName(), user->GetName())
        L2_LOG_ERROR(
            Log,
            u"[jobs] Quest HTML filename is null for NPC %ls and character %ls; display continues through the existing "
            u"path.",
            talkTarget->GetName(), user->GetName()
        );
    }

    if (std::getenv("L2_QUEST_TRACE") != nullptr) {
        // std::fprintf(
        //     stderr,
        //     "[QUEST_TRACE] ShowQuestPage quest=%d file_units=%zu "
        //     "html_units=%zu host_units=%zu first=0x%04x\n",
        //     mQuestId,
        //     fileName.size(),
        //     html.size(),
        //     hostHtml.size(),
        //     html.empty()
        //         ? 0U
        //         : static_cast<unsigned int>(html[0]))
        // Noise: the first HTML code unit adds no useful context to the display trace.
        // Trace verbosity is controlled by L2_QUEST_TRACE. LOG_DEBUG is suppressed in L2Server.
        // Log.Add(LOG_NORMAL, L"[quest] Preparing HTML for quest %d: filename %zu host units, HTML %zu UTF-16 units,
        // converted HTML %zu host units.", mQuestId, fileName.size(), html.size(), hostHtml.size()), L2_LOG_INFO(Log,
        // L"[quest] Preparing HTML for quest %d: filename %zu host units, HTML %zu UTF-16 units, converted HTML %zu host
        // units.", mQuestId, fileName.size(), html.size(), hostHtml.size())
        L2_LOG_INFO(
            Log, u"[quest] Preparing HTML for quest %d: filename %zu UTF-16 units, HTML %zu UTF-16 units.", mQuestId,
            std::char_traits<char16_t>::length(mFileName.data()), html.size()
        );
        // Noise: the common logger owns record output. A separate stderr flush is extra.
        // std::fflush(stderr)
    }
    user->ShowQuestHTML(mFileName.data(), html.c_str(), mQuestId);
    if (user->IsBuilder()) {
        g_QuestHelper.checkAndOpenQuest(QuestHelper::TalkSelectedEvent, talkTarget->GetName(), fileName.c_str());
        g_QuestHelper.openHTML(const_cast<wchar_t*>(fileName.c_str()), 0);
    }
    return true;
#else
    // Current L2NPC.exe 0x00410408.
    return false;
#endif
}

AtomicShowPage::AtomicShowPage() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ShowPage;
    nLine = 0x327;
}

AtomicShowPage::AtomicShowPage(unsigned int creatureSID, unsigned int npcIndex, const char16_t* fileName, int eventId) :
    mCreatureSID(creatureSID), mNpcIndex(npcIndex), m_EventID(eventId) {
    // Current EXE constructor helper 0x004890fc.
    CopyAtomicPageName(mFileName, fileName);
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ShowPage;
    nLine = 0x327;
}

int AtomicShowPage::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00410488.
    return ::Assemble(
        buffer, bufferSize, "hddSd", 6, static_cast<int>(mCreatureSID), static_cast<int>(mNpcIndex), mFileName.data(),
        m_EventID
    );
}

const unsigned char* AtomicShowPage::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00410568.
    return ::Disassemble(packet, "ddSd", &mCreatureSID, &mNpcIndex, 0x20a, mFileName.data(), &m_EventID);
}

bool AtomicShowPage::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x00490340.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(mCreatureSID);
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"pUser is NULL mCreatureSID[%d]",
        //     static_cast<int>(mCreatureSID))
        // Log.Add(
        //     LOG_WARN,
        //     L"[jobs] HTML page skipped: character SID %u is unavailable.",
        //     static_cast<unsigned int>(mCreatureSID))
        L2_LOG_WARN(
            Log, u"[jobs] HTML page skipped: character SID %u is unavailable.", static_cast<unsigned int>(mCreatureSID)
        );
        return false;
    }

    const char16_t* const targetFileName = mFileName.data();
    if (targetFileName == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] html file name is null",
        //     "..\\Shared\\AtomicJob.cpp",
        //     0x4ae)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] HTML filename is null, loading continues through the existing path.")
        L2_LOG_ERROR(Log, u"[jobs] HTML filename is null; loading continues through the existing path.");
    }
    const std::wstring fileName = target_utf16::ToHostCodeUnits(targetFileName);
    std::u16string html;
    if (!g_HTMLCache.GetHTMLFile(targetFileName, html, user->m_data->m_CountryCode)) {
        return false;
    }

    CCreature* const talkTarget = user->m_spTalkTarget.GetCreature();
    if (talkTarget == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"pTalkTargetNPC is NULL UserName[%s]",
        //     user->GetName())
        // Log.Add(
        //     LOG_WARN,
        //     L"[jobs] HTML page skipped for %ls: no NPC talk target.",
        //     user->GetName())
        // L2_LOG_WARN(Log,
        //     L"[jobs] HTML page skipped for %ls: no NPC talk target.",
        //     user->GetName())
        L2_LOG_WARN(Log, u"[jobs] HTML page skipped for %ls: no NPC talk target.", user->GetName());
        return false;
    }
    if (talkTarget->m_data->index != static_cast<int>(mNpcIndex)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"pTalkTargetNPC Index not Equal "
        //     L"pTalkTargetNPC->m_data->index[%d]/"
        //     L"mNpcIndex[%d]/UserName[%s]",
        //     talkTarget->m_data->index,
        //     static_cast<int>(mNpcIndex),
        //     user->GetName())
        // Log.Add(
        //     LOG_WARN,
        //     L"[jobs] HTML page skipped for %ls: current NPC index %d differs from requested index %d.",
        //     user->GetName(),
        //     talkTarget->m_data->index,
        //     static_cast<int>(mNpcIndex))
        // L2_LOG_WARN(Log,
        //     L"[jobs] HTML page skipped for %ls: current NPC index %d differs from requested index %d.",
        //     user->GetName(),
        //     talkTarget->m_data->index,
        //     static_cast<int>(mNpcIndex))
        L2_LOG_WARN(
            Log, u"[jobs] HTML page skipped for %ls: current NPC index %d differs from requested index %d.", user->GetName(),
            talkTarget->m_data->index, static_cast<int>(mNpcIndex)
        );
        return false;
    }

    user->ShowHTML(mFileName.data(), html.c_str(), 0, static_cast<HTML::DisplayType>(m_EventID != 3), true);
    if (user->IsBuilder()) {
        g_QuestHelper.checkAndOpenQuest(QuestHelper::TalkSelectedEvent, talkTarget->GetName(), fileName.c_str());
        g_QuestHelper.openHTML(const_cast<wchar_t*>(fileName.c_str()), 0);
    }
    return true;
#else
    // Current L2NPC.exe 0x00410638.
    return false;
#endif
}

AtomicPledgeLevelUp::AtomicPledgeLevelUp() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::PledgeLevelUp;
    nLine = 0x35b;
}

AtomicPledgeLevelUp::AtomicPledgeLevelUp(unsigned int creatureSID, int pledgeId, int pledgeLevel) :
    mCreatureSID(creatureSID), mPledgeId(pledgeId), mPledgeLevel(pledgeLevel) {
    // Current EXE constructor sequence in CNPC::PledgeLevelUp at 0x0049fff4.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::PledgeLevelUp;
    nLine = 0x35b;
}

int AtomicPledgeLevelUp::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004106b8.
    return ::Assemble(buffer, bufferSize, "hddd", 0x0a, static_cast<int>(mCreatureSID), mPledgeId, mPledgeLevel);
}

const unsigned char* AtomicPledgeLevelUp::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00410790.
    return ::Disassemble(packet, "ddd", &mCreatureSID, &mPledgeId, &mPledgeLevel);
}

bool AtomicPledgeLevelUp::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x00489ea0.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(mCreatureSID);
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user == nullptr || user->m_data->m_nPledgeID == 0) {
        return true;
    }

    // The current body performs this second CastUser call even though the
    // first result is already typed as User.
    user = user->CastUser();
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr || pledge->GetDbId() != static_cast<std::uint32_t>(mPledgeId)) {
        user->GetSocket()->SendSystemMessage(0x113);
        return true;
    }

    pledge->SetPledgeLevel(static_cast<std::uint32_t>(mPledgeLevel), true);
    pledge->UpdateInfoWindowPacket();
    if (mPledgeLevel == 4) {
        {
            L2_GUARD(*user->m_lock);
            user->AcquireSkill(0xf7, 1, 0, false, true);
            user->AcquireSkill(0x3bb3, 1, 0, false, true);
            user->AcquireSkill(0x3bb4, 1, 0, false, true);
            user->AcquireSkill(0x3bb5, 1, 0, false, true);
        }
        user->ValidateChanged(VB_MAX);
    }
    if (mPledgeLevel == 5) {
        {
            L2_GUARD(*user->m_lock);
            user->AcquireSkill(0x4a5a, 1, 0, false, true);
            user->AcquireSkill(0x4a5b, 1, 0, false, true);
        }
        user->ValidateChanged(VB_MAX);
    }
    user->GetSocket()->SendSystemMessage(0x112);
    return true;
#else
    // Current L2NPC.exe 0x0041084c.
    return false;
#endif
}

AtomicSetResidenceDeco::AtomicSetResidenceDeco() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::SetResidenceDeco;
    nLine = 0x384;
}

AtomicSetResidenceDeco::AtomicSetResidenceDeco(
    unsigned int npcIndex, unsigned int userIndex, int agitId, int decoType, int decoLevel
) :
    mNpcIndex(npcIndex), mUserIndex(userIndex), mAgitId(agitId), mDecoType(decoType), mDecoLevel(decoLevel) {
    // Current EXE constructor sequence in CNPC::Agit_SetDeco at 0x0048b610.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::SetResidenceDeco;
    nLine = 0x384;
}

int AtomicSetResidenceDeco::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004108cc.
    return ::Assemble(
        buffer, bufferSize, "hddddd", 0x0f, static_cast<int>(mNpcIndex), static_cast<int>(mUserIndex), mAgitId, mDecoType,
        mDecoLevel
    );
}

const unsigned char* AtomicSetResidenceDeco::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004109b0.
    return ::Disassemble(packet, "ddddd", &mNpcIndex, &mUserIndex, &mAgitId, &mDecoType, &mDecoLevel);
}

bool AtomicSetResidenceDeco::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x00493e48.
    CResidenceDeco* const deco = CResidenceDecoDb::Inst().FindResidenceDeco(mDecoType, mDecoLevel);
    if (deco == nullptr) {
        g_npcserver.Send("cddd", 0x46, mNpcIndex, mUserIndex, 0);
        return true;
    }

    const std::int32_t now = runtime::crt::Time32Now();
    std::tm* const localTime = runtime::crt::LocalTime32(now);
    const std::int32_t periodDays = runtime::Int32FromBits(deco->m_lstCost.front().first);
    const std::int32_t expiration = runtime::AddInt32Wrapping(
        runtime::SubtractInt32Wrapping(
            runtime::MultiplyInt32Wrapping(
                runtime::AddInt32Wrapping(
                    runtime::SubtractInt32Wrapping(
                        runtime::MultiplyInt32Wrapping(periodDays, 1440), localTime->tm_min
                    ),
                    60
                ),
                60
            ),
            localTime->tm_sec
        ),
        now
    );
    CSetResidenceDeco packet(
        static_cast<int>(mNpcIndex), static_cast<int>(mUserIndex), mAgitId, deco->m_nType, deco->m_nDecoId,
        deco->m_sDecoName, deco->m_nLevel, expiration, 0
    );
    db.RequestTPacketProcess(g_nWorldId, 1, static_cast<int>(sizeof(packet)), packet);
    return true;
#else
    // Current EXE 0x00410a80.
    return false;
#endif
}

AtomicResetResidenceDeco::AtomicResetResidenceDeco() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ResetResidenceDeco;
    nLine = 0x3a8;
}

AtomicResetResidenceDeco::AtomicResetResidenceDeco(unsigned int npcIndex, unsigned int userIndex, int agitId, int decoType) :
    mNpcIndex(npcIndex), mUserIndex(userIndex), mAgitId(agitId), mDecoType(decoType) {
    // Current EXE constructor sequence in CNPC::Agit_ResetDeco at 0x0048b744.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ResetResidenceDeco;
    nLine = 0x3a8;
}

int AtomicResetResidenceDeco::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00410b00.
    return ::Assemble(
        buffer, bufferSize, "hdddd", 0x10, static_cast<int>(mNpcIndex), static_cast<int>(mUserIndex), mAgitId, mDecoType
    );
}

const unsigned char* AtomicResetResidenceDeco::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00410be0.
    return ::Disassemble(packet, "dddd", &mNpcIndex, &mUserIndex, &mAgitId, &mDecoType);
}

bool AtomicResetResidenceDeco::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x00488198 creates the exact 0x14-byte DX
    // packet on the stack and sends protocol 2 for the live process world.
    ::CResetResidenceDeco packet(static_cast<int>(mNpcIndex), static_cast<int>(mUserIndex), mAgitId, mDecoType, 0);
    ::db.RequestTPacketProcess(::g_nWorldId, 2, static_cast<int>(sizeof(packet)), packet);
    return true;
#else
    // Current L2NPC.exe 0x00410ca4.
    return false;
#endif
}

Atomic_AJ_OPEN_DECO_NPC::Atomic_AJ_OPEN_DECO_NPC() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::OpenDecoNpc;
    nLine = 0x5a6;
}

Atomic_AJ_OPEN_DECO_NPC::Atomic_AJ_OPEN_DECO_NPC(unsigned int npcIndex, unsigned int userId, int residenceId) :
    m_nNpcIndex(npcIndex), m_nUserIndex(userId), m_nAgitCID(residenceId) {
    // Current EXE constructor 0x00410d24.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::OpenDecoNpc;
    nLine = 0x5a6;
}

int Atomic_AJ_OPEN_DECO_NPC::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00410d8c.
    return ::Assemble(
        buffer, bufferSize, "hddd", 0x3f, static_cast<int>(m_nNpcIndex), static_cast<int>(m_nUserIndex), m_nAgitCID
    );
}

const unsigned char* Atomic_AJ_OPEN_DECO_NPC::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00410e64.
    return ::Disassemble(packet, "ddd", &m_nNpcIndex, &m_nUserIndex, &m_nAgitCID);
}

bool Atomic_AJ_OPEN_DECO_NPC::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x00493104.
    CCreature* const userCreature = g_CreatureSmartIdManager.GetObjectW(m_nUserIndex);
    User* const rawUser = userCreature != nullptr && userCreature->IsPC() ? userCreature->CastUser() : nullptr;
    CSPointer<User> user(rawUser, ObjectRefType::FunctionChain);
    if (!user || user->GetSocket() == nullptr) {
        return false;
    }

    CCreatureFactory& creatureFactory = CCreatureFactory::Inst();
    const int npcIndex = static_cast<int>(m_nNpcIndex);
    CCreature* npcCreature = nullptr;
    if (npcIndex >= 0 && npcIndex < creatureFactory.GetSize()) {
        npcCreature =
            g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(creatureFactory.GetIdByIndex(npcIndex)));
    }
    CNPC* const rawNpc = npcCreature != nullptr ? npcCreature->CastNPC() : nullptr;
    CSPointer<CNPC> npc(rawNpc, ObjectRefType::FunctionChain);
    if (!npc) {
        return false;
    }

    if (InstantAgitUtil::IsInInstantAgit(user)) {
        CInstantAgit* const agit = CInstantAgitManager::inst().GetInstantAgit(static_cast<int>(user->m_data->m_nPledgeID));
        if (agit == nullptr) {
            user->GetSocket()->SendSystemMessage(u"SYS", u"invalid agit");
            return false;
        }
        agit->SendDecoNpcInfo(user);
    } else {
        CResidence* const residence = g_ResidenceDB.GetResidence(m_nAgitCID);
        if (residence == nullptr) {
            user->GetSocket()->SendSystemMessage(u"SYS", u"invalid agit");
            return false;
        }
        if (!ResidenceUtil::HasRightToSetDecoNpc(residence, user)) {
            user->GetSocket()->SendSystemMessage(u"SYS", u"no right");
            return false;
        }

        protocol::S_EX_DECO_NPC_INFO packet{};
        packet.nAgitCID = m_nAgitCID;
        const std::int32_t currentTime = runtime::crt::Time32Now();
        for (int slot = 0; slot < residence->m_data->m_nDecoNpcSlotCount; ++slot) {
            const DecoNpc& decoNpc = residence->m_data->m_aDecoNpc[slot];
            protocol::DecoNpcInfo info{};
            info.cSlot = static_cast<char>(slot);
            info.nDecoCID = decoNpc.nCID;
            info.nExpire = decoNpc.nCID == 0 ? 0 : runtime::SubtractInt32Wrapping(decoNpc.tExpire, currentTime);
            packet.vDecoNpcs.push_back(info);
        }
        packet.nResidenceGrade = static_cast<char>(residence->m_data->m_nResidenceGrade);
        for (int index = 0; index < residence->m_data->m_nResidenceDomainCount; ++index) {
            packet.vResidenceDomains.push_back(static_cast<char>(residence->m_data->m_aResidenceDomains[index]));
        }
        user->GetSocket()->Send(S_EX, S_EX_DECO_NPC_INFO, packet);
    }

    TalkChannelCache::inst().mChannels[21].Cache(user->m_nId, npc->m_nId, 0);
    return true;
#else
    // Current EXE 0x00410f20.
    return false;
#endif
}

AtomicSetFortressFacility::AtomicSetFortressFacility() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::SetFortressFacility;
    nLine = 0x3e3;
}

AtomicSetFortressFacility::AtomicSetFortressFacility(
    unsigned int npcId, unsigned int userId, int fortressId, int facilityType, int facilityLevel
) :
    m_nNpcId(npcId),
    m_nUserId(userId),
    m_nFortressId(fortressId),
    m_nFacilityType(facilityType),
    m_nFacilityLevel(facilityLevel) {
    // Current EXE constructor sequence in CNPC::Fortress_SetFacility at
    // 0x0049f094.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::SetFortressFacility;
    nLine = 0x3e3;
}

int AtomicSetFortressFacility::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00410fa0.
    return ::Assemble(
        buffer, bufferSize, "hddddd", 0x11, static_cast<int>(m_nNpcId), static_cast<int>(m_nUserId), m_nFortressId,
        m_nFacilityType, m_nFacilityLevel
    );
}

const unsigned char* AtomicSetFortressFacility::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00411084.
    return ::Disassemble(packet, "ddddd", &m_nNpcId, &m_nUserId, &m_nFortressId, &m_nFacilityType, &m_nFacilityLevel);
}

bool AtomicSetFortressFacility::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x004882e4.
    CResidence* const residence = g_ResidenceDB.GetResidence(m_nFortressId);
    if (residence != nullptr && residence->IsFortress() && residence->CastFortress() != nullptr) {
        CDxSetFortressFacility packet(
            static_cast<int>(m_nNpcId), static_cast<int>(m_nUserId), m_nFortressId, m_nFacilityType, m_nFacilityLevel
        );
        db.RequestTPacketProcess(g_nWorldId, 0x32, static_cast<int>(sizeof(packet)), packet);
    }
    return true;
#else
    // Current EXE 0x00411154.
    return false;
#endif
}

AtomicResetFortressFacility::AtomicResetFortressFacility() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ResetFortressFacility;
    nLine = 0x406;
}

AtomicResetFortressFacility::AtomicResetFortressFacility(unsigned int npcId, unsigned int userId, int fortressId) :
    m_nNpcId(npcId), m_nUserId(userId), m_nFortressId(fortressId) {
    // Current EXE constructor sequence in CNPC::Fortress_ResetFacility at
    // 0x0049f228.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ResetFortressFacility;
    nLine = 0x406;
}

int AtomicResetFortressFacility::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004111d4.
    return ::Assemble(
        buffer, bufferSize, "hddd", 0x12, static_cast<int>(m_nNpcId), static_cast<int>(m_nUserId), m_nFortressId
    );
}

const unsigned char* AtomicResetFortressFacility::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004112ac.
    return ::Disassemble(packet, "ddd", &m_nNpcId, &m_nUserId, &m_nFortressId);
}

bool AtomicResetFortressFacility::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048840c creates the exact 0x0c-byte DX
    // packet on the stack and sends protocol 0x33 for the live world.
    ::CDxResetFortressFacility packet(static_cast<int>(m_nNpcId), static_cast<int>(m_nUserId), m_nFortressId);
    ::db.RequestTPacketProcess(::g_nWorldId, 0x33, static_cast<int>(sizeof(packet)), packet);
    return true;
#else
    // Current L2NPC.exe 0x00411368.
    return false;
#endif
}

AtomicStartObserver::AtomicStartObserver() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::StartObserver;
    nLine = 0x429;
}

AtomicStartObserver::AtomicStartObserver(
    unsigned int npcIndex, unsigned int userIndex, int x, int y, int z, int yaw, int pitch, int residenceId
) :
    mNpcIndex(npcIndex), mUserIndex(userIndex), mX(x), mY(y), mZ(z), mYaw(yaw), mPitch(pitch), mResidenceId(residenceId) {
    // Current EXE constructor sequence in CNPC::Agit_StartObserver at
    // 0x0048b868.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::StartObserver;
    nLine = 0x429;
}

int AtomicStartObserver::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004113e8.
    return ::Assemble(
        buffer, bufferSize, "hdddddddd", 0x14, static_cast<int>(mNpcIndex), static_cast<int>(mUserIndex), mX, mY, mZ, mYaw,
        mPitch, mResidenceId
    );
}

const unsigned char* AtomicStartObserver::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004114e4.
    return ::Disassemble(packet, "dddddddd", &mNpcIndex, &mUserIndex, &mX, &mY, &mZ, &mYaw, &mPitch, &mResidenceId);
}

bool AtomicStartObserver::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048e894.
    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(static_cast<int>(mUserIndex));
    if (!creature || !creature->IsPC() || creature->m_data->m_bIsAlive == 0) {
        return true;
    }

    User* const user = creature->CastUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [User *pUser = "
        //     L"pCreature->CastUser()] is nullptr",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x6af)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Cannot start observer mode: creature index %u is marked as a player but has no User object.",
        //     static_cast<unsigned int>(mUserIndex))
        L2_LOG_ERROR(
            Log, u"[jobs] Cannot start observer mode: creature index %u is marked as a player but has no User object.",
            static_cast<unsigned int>(mUserIndex)
        );
        return false;
    }

    if (user->HaveSummon() || user->HavePet()) {
        user->GetSocket()->SendSystemMessage(0x30e);
        return true;
    }
    if (user->IsRiding()) {
        user->GetSocket()->SendSystemMessage(0x30e);
        return true;
    }
    if (user->IsOlympiadObserver()) {
        user->GetSocket()->SendSystemMessage(0xb8c);
        return true;
    }
    if (g_PledgeGameSystem.IsRegisteredUser(user)) {
        user->GetSocket()->SendSystemMessage(0x69d);
        return true;
    }

    if (mResidenceId > 0) {
        CResidence* const residence = g_ResidenceDB.GetResidence(mResidenceId);
        if (residence != nullptr && residence->IsCastle() && !residence->IsUnderSiege()) {
            user->GetSocket()->SendSystemMessage(0x30c);
            return false;
        }
    }

    user->StopMove();
    user->ObserverStart(mX, mY, mZ, mYaw, mPitch, 0xe10);
    return true;
#else
    // Current L2NPC.exe 0x004115d0 returns false after the guard body.
    return false;
#endif
}

AtomicShowQuestFHTML::AtomicShowQuestFHTML() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ShowQuestFHTML;
    nLine = 0x6f0;
}

AtomicShowQuestFHTML::AtomicShowQuestFHTML(
    unsigned int creatureSID, unsigned int creatureIndex, unsigned int npcSID, CFHTML* fhtml, int questId
) :
    mCreatureSID(creatureSID), mCreatureIndex(creatureIndex), mNpcSID(npcSID), mpFHTML(fhtml), mQuestID(questId) {
    // Current EXE constructor helper 0x0041838c.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ShowQuestFHTML;
    nLine = 0x6f0;
    L2_ADD_REF(mpFHTML, ObjectRefType::Anonymous);
}

AtomicShowQuestFHTML::~AtomicShowQuestFHTML() {
    // Current EXE destructor helper 0x00418450.
    if (mpFHTML) {
        L2_RELEASE(mpFHTML, ObjectRefType::Anonymous);
    }
}

int AtomicShowQuestFHTML::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00411650 uses a 0x4fa0-byte stack allocation and passes
    // the exact 0x4f9b limit to CFHTML::MakeStream.
    std::array<char, 0x4fa0> stream;
    char* const end = mpFHTML->MakeStream(stream.data(), 0x4f9b);
    return ::Assemble(
        buffer, bufferSize, "hddddb", 0x2d, static_cast<int>(mCreatureSID), static_cast<int>(mCreatureIndex),
        static_cast<int>(mNpcSID), mQuestID, static_cast<int>(end - stream.data()), stream.data()
    );
}

const unsigned char* AtomicShowQuestFHTML::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x004910c4. The standalone page-name decode
    // does not advance packet. MakeHTMLFromStream decodes the
    // same name again before consuming its parameter count.
    packet = ::Disassemble(packet, "dddd", &mCreatureSID, &mCreatureIndex, &mNpcSID, &mQuestID);

    if (std::getenv("L2_QUEST_TRACE") != nullptr) {
        // std::fprintf(
        //     stderr,
        //     "[QUEST_TRACE] AtomicShowQuestFHTML::Disassemble "
        //     "sid=%u index=%u npc=%u quest=%d "
        //     "stream=%02x%02x%02x%02x%02x%02x%02x%02x\n",
        //     mCreatureSID,
        //     mCreatureIndex,
        //     mNpcSID,
        //     mQuestID,
        //     packet[0],
        //     packet[1],
        //     packet[2],
        //     packet[3],
        //     packet[4],
        //     packet[5],
        //     packet[6],
        //     packet[7])
        // Noise: unchecked raw packet bytes are not useful operator context. Retain decoded identifiers.
        // Log.Add(LOG_NORMAL, L"[quest] Decoded formatted-HTML job header: character SID %u, creature index %u, NPC SID %u,
        // quest %d.", mCreatureSID, mCreatureIndex, mNpcSID, mQuestID)
        L2_LOG_INFO(
            Log, u"[quest] Decoded formatted-HTML job header: character SID %u, creature index %u, NPC SID %u, quest %d.",
            mCreatureSID, mCreatureIndex, mNpcSID, mQuestID
        );
        // Noise: the common logger owns record output. A separate stderr flush is extra.
        // std::fflush(stderr)
    }

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(static_cast<int>(mCreatureIndex));
    if (creature && creature->IsPC() && creature->Id() == mCreatureSID) {
        User* const actualUser = creature->CastUser();
        if (actualUser != nullptr) {
            std::array<char16_t, 264> pageName;
            ::Disassemble(packet, "S", 0x208, pageName.data());
            mFHTMLName.assign(std::u16string_view(pageName.data()));
            mFHTMLDataString = CFHTML::MakeHTMLFromStream(packet, actualUser->m_data->m_CountryCode);
            if (std::getenv("L2_QUEST_TRACE") != nullptr) {
                // std::fprintf(
                //     stderr,
                //     "[QUEST_TRACE] AtomicShowQuestFHTML::Disassemble "
                //     "decoded_name_units=%zu decoded_html_units=%zu "
                //     "country=%u\n",
                //     mFHTMLName.size(),
                //     mFHTMLDataString.size(),
                //     static_cast<unsigned int>(
                //         actualUser->m_data->m_CountryCode))
                // Log.Add(LOG_NORMAL, L"[quest] Decoded formatted HTML: filename %zu UTF-16 units, HTML %zu UTF-16 units,
                // country code %u.", mFHTMLName.size(), mFHTMLDataString.size(), static_cast<unsigned
                // int>(actualUser->m_data->m_CountryCode))
                L2_LOG_INFO(
                    Log,
                    u"[quest] Decoded formatted HTML: filename %zu UTF-16 units, HTML %zu UTF-16 units, country code %u.",
                    mFHTMLName.size(), mFHTMLDataString.size(), static_cast<unsigned int>(actualUser->m_data->m_CountryCode)
                );
                // Noise: the common logger owns record output. A separate stderr flush is extra.
                // std::fflush(stderr)
            }
        }
    }
#else
    // Current L2NPC.exe 0x00411774 does not deserialize this outbound-only
    // job.
#endif
    return packet;
}

bool AtomicShowQuestFHTML::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x00490684.
    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(static_cast<int>(mCreatureIndex));
    if (!creature || !creature->IsPC() || creature->Id() != mCreatureSID) {
        return false;
    }

    User* const user = creature->CastUser();
    const std::wstring fileName = target_utf16::ToHostCodeUnits(mFHTMLName.c_str());
    if (std::getenv("L2_QUEST_TRACE") != nullptr) {
        // std::fprintf(
        //     stderr,
        //     "[QUEST_TRACE] AtomicShowQuestFHTML::Do "
        //     "quest=%d name_units=%zu html_units=%zu\n",
        //     mQuestID,
        //     mFHTMLName.size(),
        //     mFHTMLDataString.size())
        // Log.Add(LOG_NORMAL, L"[quest] Preparing formatted HTML for quest %d: filename %zu UTF-16 units, HTML %zu UTF-16
        // units.", mQuestID, mFHTMLName.size(), mFHTMLDataString.size())
        L2_LOG_INFO(
            Log, u"[quest] Preparing formatted HTML for quest %d: filename %zu UTF-16 units, HTML %zu UTF-16 units.",
            mQuestID, mFHTMLName.size(), mFHTMLDataString.size()
        );
        // Noise: the common logger owns record output. A separate stderr flush is extra.
        // std::fflush(stderr)
    }
    user->ShowQuestHTML(mFHTMLName.c_str(), mFHTMLDataString.c_str(), mQuestID);

    if (user->IsBuilder()) {
        CCreature* const talkTarget = user->m_spTalkTarget.GetCreature();
        if (talkTarget != nullptr) {
            g_QuestHelper.checkAndOpenQuest(QuestHelper::TalkSelectedEvent, talkTarget->GetName(), fileName.c_str());
            g_QuestHelper.openHTML(const_cast<wchar_t*>(fileName.c_str()), 0);
        }
    }
    return true;
#else
    // Current L2NPC.exe 0x00411800.
    return false;
#endif
}

AtomicShowFHTML::AtomicShowFHTML() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ShowFHTML;
    nLine = 0x758;
}

AtomicShowFHTML::AtomicShowFHTML(unsigned int creatureSID, unsigned int creatureIndex, unsigned int npcSID, CFHTML* fhtml) :
    mCreatureSID(creatureSID), mCreatureIndex(creatureIndex), mNpcSID(npcSID), mpFHTML(fhtml) {
    // Current EXE constructor helper 0x00418520.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ShowFHTML;
    nLine = 0x758;
    L2_ADD_REF(mpFHTML, ObjectRefType::Anonymous);
}

AtomicShowFHTML::~AtomicShowFHTML() {
    // Current EXE destructor helper 0x004185dc.
    if (mpFHTML) {
        L2_RELEASE(mpFHTML, ObjectRefType::Anonymous);
    }
}

int AtomicShowFHTML::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00411880.
    std::array<char, 0x4fa0> stream;
    char* const end = mpFHTML->MakeStream(stream.data(), 0x4f9b);
    return ::Assemble(
        buffer, bufferSize, "hdddb", 7, static_cast<int>(mCreatureSID), static_cast<int>(mCreatureIndex),
        static_cast<int>(mNpcSID), static_cast<int>(end - stream.data()), stream.data()
    );
}

const unsigned char* AtomicShowFHTML::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x00491320 keeps the same non-advancing
    // page-name decode as AtomicShowQuestFHTML.
    packet = ::Disassemble(packet, "ddd", &mCreatureSID, &mCreatureIndex, &mNpcSID);

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(static_cast<int>(mCreatureIndex));
    if (creature && creature->IsPC() && creature->Id() == mCreatureSID) {
        User* const actualUser = creature->CastUser();
        if (actualUser != nullptr) {
            std::array<char16_t, 264> pageName;
            ::Disassemble(packet, "S", 0x208, pageName.data());
            mFHTMLName.assign(std::u16string_view(pageName.data()));
            mFHTMLDataString = CFHTML::MakeHTMLFromStream(packet, actualUser->m_data->m_CountryCode);
        }
    }
#else
    // Current L2NPC.exe 0x0041199c does not deserialize this outbound-only
    // job.
#endif
    return packet;
}

bool AtomicShowFHTML::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048ec84.
    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(static_cast<int>(mCreatureIndex));
    if (!creature || !creature->IsPC() || creature->Id() != mCreatureSID) {
        return false;
    }

    User* const user = creature->CastUser();
    user->ShowHTML(mFHTMLName.c_str(), mFHTMLDataString.c_str(), 0, HTML::LAUNCH, true);
    return true;
#else
    // Current L2NPC.exe 0x00411a28.
    return false;
#endif
}

AtomicDeleteAcquireSkill::AtomicDeleteAcquireSkill() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::DeleteAcquireSkill;
    nLine = 0x992;
}

AtomicDeleteAcquireSkill::AtomicDeleteAcquireSkill(int npcSID, int userSID, int acquireType) :
    m_NpcSID(npcSID), m_UserSID(userSID), m_SkillID(acquireType) {
    // Current EXE constructor sequence in CNPC::DeleteAcquireSkill at
    // 0x004a2734.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::DeleteAcquireSkill;
    nLine = 0x992;
}

int AtomicDeleteAcquireSkill::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004147d4.
    return ::Assemble(buffer, bufferSize, "hddd", 0x27, m_NpcSID, m_UserSID, m_SkillID);
}

const unsigned char* AtomicDeleteAcquireSkill::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004148ac.
    return ::Disassemble(packet, "ddd", &m_NpcSID, &m_UserSID, &m_SkillID);
}

bool AtomicDeleteAcquireSkill::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048be64.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_UserSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    return user != nullptr && user->DeleteAcquireSkill(m_SkillID);
#else
    // Current L2NPC.exe 0x00414968.
    return false;
#endif
}

bool AtomicDeleteAcquireSkill::CanDo() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048bf48.
    CCreature* const userObject = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_UserSID));
    User* user = nullptr;
    if (userObject != nullptr && userObject->IsPC()) {
        user = userObject->CastUser();
    }

    CCreature* const npcObject = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_NpcSID));
    CNPC* npc = nullptr;
    if (npcObject != nullptr && npcObject->IsNPC()) {
        npc = npcObject->CastNPC();
    }

    if (user == nullptr || npc == nullptr) {
        return false;
    }

    const FVector userPosition = user->m_data->m_Pos;
    const FVector npcPosition = npc->GetPos();
    const double deltaX = userPosition.x - npcPosition.x;
    const double deltaY = userPosition.y - npcPosition.y;
    const double deltaZ = userPosition.z - npcPosition.z;
    return deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ < 62500.0;
#else
    // Current L2NPC.exe 0x004149e8.
    return true;
#endif
}

AtomicDeleteAcquireSkills::AtomicDeleteAcquireSkills() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::DeleteAcquireSkills;
    nLine = 0x9b2;
}

AtomicDeleteAcquireSkills::AtomicDeleteAcquireSkills(int npcSID, int userSID, int acquireType) :
    m_nNpcSID(npcSID), m_nUserSID(userSID), m_nAcquireType(acquireType) {
    // Current EXE constructor sequence in CNPC::DeleteAcquireSkills at
    // 0x004a28cc.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::DeleteAcquireSkills;
    nLine = 0x9b2;
}

int AtomicDeleteAcquireSkills::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00414a68.
    return ::Assemble(buffer, bufferSize, "hddd", 0x28, m_nNpcSID, m_nUserSID, m_nAcquireType);
}

const unsigned char* AtomicDeleteAcquireSkills::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00414b40.
    return ::Disassemble(packet, "ddd", &m_nNpcSID, &m_nUserSID, &m_nAcquireType);
}

bool AtomicDeleteAcquireSkills::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048c0d4.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nUserSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    return user != nullptr && user->DeleteAcquireSkills(static_cast<EtcSkillAcquireType>(m_nAcquireType));
#else
    // Current L2NPC.exe 0x00414bfc.
    return false;
#endif
}

void AtomicDeleteAcquireSkills::Dump() {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00414cfc.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AtomicDeleteAcquireSkills::Dump() => "
    //     L"m_nNpcSID=[%d], m_nUserSID=[%d], "
    //     L"m_nAcquireType=[%d]",
    //     m_nNpcSID,
    //     m_nUserSID,
    //     m_nAcquireType)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for acquired-skill deletion: NPC SID %d, character SID %d, acquire type %d.",
    //     m_nNpcSID,
    //     m_nUserSID,
    //     m_nAcquireType)
    L2_LOG_INFO(
        Log, u"[jobs] Parameters for acquired-skill deletion: NPC SID %d, character SID %d, acquire type %d.", m_nNpcSID,
        m_nUserSID, m_nAcquireType
    );
}

bool AtomicDeleteAcquireSkills::CanDo() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048c1b8.
    CCreature* const userObject = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nUserSID));
    User* user = nullptr;
    if (userObject != nullptr && userObject->IsPC()) {
        user = userObject->CastUser();
    }

    CCreature* const npcObject = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nNpcSID));
    CNPC* npc = nullptr;
    if (npcObject != nullptr && npcObject->IsNPC()) {
        npc = npcObject->CastNPC();
    }

    if (user == nullptr || npc == nullptr) {
        return false;
    }

    const FVector userPosition = user->m_data->m_Pos;
    const FVector npcPosition = npc->GetPos();
    const double deltaX = userPosition.x - npcPosition.x;
    const double deltaY = userPosition.y - npcPosition.y;
    const double deltaZ = userPosition.z - npcPosition.z;
    return deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ < 62500.0;
#else
    // Current L2NPC.exe 0x00414c7c.
    return true;
#endif
}

#if defined(L2_BUILD_L2SERVER)
CAtomicCouponUsed::CAtomicCouponUsed(
    const int worldId,
    const int userId,
    const int userDatabaseId,
    const int couponPart1,
    const int couponPart2,
    const int couponPart3,
    const int couponPart4,
    const int couponItemClassId,
    const std::uint32_t accountId
) :
    m_nWorldId(worldId),
    m_nUserId(userId),
    m_nUserDatabaseId(userDatabaseId),
    m_nCouponPart1(couponPart1),
    m_nCouponPart2(couponPart2),
    m_nCouponPart3(couponPart3),
    m_nCouponPart4(couponPart4),
    m_nCouponItemClassId(couponItemClassId),
    m_nAccountId(accountId) {
    // Current inline constructor sequence at 0x00b6c57f.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::CouponUsed;
    nLine = 0x8fa;
}

bool CAtomicCouponUsed::Do() {
    // Current L2Server.exe 0x0048b864.
    L2_GUARD_FUNCTION();
    db.RequestPCCafeCouponUsed(m_nWorldId, m_nAccountId, m_nCouponPart1, m_nCouponPart2, m_nCouponPart3, m_nCouponPart4);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nUserId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user != nullptr) {
        user->GetSocket()->Send("chccd", 0x62, 0x761, 1, 3, m_nCouponItemClassId);
    }
    return true;
}
#endif

AtomicUpdatePCCafePoint::AtomicUpdatePCCafePoint() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::UpdatePCCafePoint;
    nLine = 0x970;
}

AtomicUpdatePCCafePoint::AtomicUpdatePCCafePoint(int userSID, int delta) : m_userSID(userSID), m_delta(delta) {
    // Current EXE constructor sequence in CNPC::UpdatePCCafePoint2 at
    // 0x0048ec24.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::UpdatePCCafePoint;
    nLine = 0x970;
}

int AtomicUpdatePCCafePoint::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00414494.
    return ::Assemble(buffer, bufferSize, "hdd", 0x25, m_userSID, m_delta);
}

const unsigned char* AtomicUpdatePCCafePoint::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00414564.
    return ::Disassemble(packet, "dd", &m_userSID, &m_delta);
}

bool AtomicUpdatePCCafePoint::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048bc64.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_userSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    user->UpdatePoint(m_delta, PT_PCCAFE_POINT, m_delta < 0 ? DJWP_DECREASE : DJWP_ADD, 0);
    return true;
#else
    // Current L2NPC.exe 0x00414618.
    return false;
#endif
}

void AtomicUpdatePCCafePoint::Dump() {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00414718.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AtomicUpdatePCCafePoint::Dump() => "
    //     L"m_userSID=[%d], m_delta=[%d]",
    //     m_userSID,
    //     m_delta)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for PC-cafe point update: character SID %d, delta %d.",
    //     m_userSID,
    //     m_delta)
    L2_LOG_INFO(Log, u"[jobs] Parameters for PC-cafe point update: character SID %d, delta %d.", m_userSID, m_delta);
}

bool AtomicUpdatePCCafePoint::CanDo() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048bd58.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_userSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user == nullptr || !user->m_data->m_bCanUsePCCafePoint) {
        return false;
    }
    return user->CheckPointAddable(1, m_delta);
#else
    // Pinned L2NPC.exe 0x00414698.
    return true;
#endif
}

AtomicUpdatePVPPoint::AtomicUpdatePVPPoint() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::UpdatePVPPoint;
    nLine = 0x9d3;
}

AtomicUpdatePVPPoint::AtomicUpdatePVPPoint(int userSID, int delta, int type) :
    m_userSID(userSID), m_delta(delta), m_nType(type) {
    // Current EXE constructor sequence in CNPC::UpdatePVPPoint at 0x0048ed5c.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::UpdatePVPPoint;
    nLine = 0x9d3;
}

int AtomicUpdatePVPPoint::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00414dbc.
    return ::Assemble(buffer, bufferSize, "hddd", 0x29, m_userSID, m_delta, m_nType);
}

const unsigned char* AtomicUpdatePVPPoint::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00414e94.
    return ::Disassemble(packet, "ddd", &m_userSID, &m_delta, &m_nType);
}

bool AtomicUpdatePVPPoint::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048c344.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_userSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    user->UpdatePoint(m_delta, PT_PVP_POINT, m_delta < 0 ? DJWP_DECREASE : DJWP_ADD, 0);
    return true;
#else
    // Current L2NPC.exe 0x00414f50.
    return false;
#endif
}

void AtomicUpdatePVPPoint::Dump() {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00415050 keeps the older two-field text.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AtomicUpdatePVPPoint::Dump() => "
    //     L"m_userSID=[%d], m_delta=[%d]",
    //     m_userSID,
    //     m_delta)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for PVP point update: character SID %d, delta %d.",
    //     m_userSID,
    //     m_delta)
    L2_LOG_INFO(Log, u"[jobs] Parameters for PVP point update: character SID %d, delta %d.", m_userSID, m_delta);
}

bool AtomicUpdatePVPPoint::CanDo() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048c438.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_userSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    return user != nullptr && user->CheckPointAddable(5, m_delta);
#else
    // Pinned L2NPC.exe 0x00414fd0.
    return true;
#endif
}

AtomicUpdateUserPoint::AtomicUpdateUserPoint() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::UpdateUserPoint;
    nLine = 0xbe0;
}

AtomicUpdateUserPoint::AtomicUpdateUserPoint(int talkerIndex, int type, int point) :
    m_TalkerIndex(talkerIndex), m_Type(type), m_point(point) {
    // Current EXE constructor sequence in CNPC::UpdateRaidPoint at 0x0048eea4.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::UpdateUserPoint;
    nLine = 0xbe0;
}

int AtomicUpdateUserPoint::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00417038 reads only the low byte of m_Type.
    return ::Assemble(
        buffer, bufferSize, "hdcd", 0x39, m_TalkerIndex, static_cast<int>(static_cast<signed char>(m_Type)), m_point
    );
}

const unsigned char* AtomicUpdateUserPoint::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00417110 writes the wire selector into m_Type's low byte.
    return ::Disassemble(packet, "dcd", &m_TalkerIndex, reinterpret_cast<char*>(&m_Type), &m_point);
}

bool AtomicUpdateUserPoint::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048d6bc.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_TalkerIndex));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    user->UpdatePoint(m_point, static_cast<UserPointType>(m_Type), m_point < 0 ? DJWP_DECREASE : DJWP_ADD, 0);
    return true;
#else
    // Current L2NPC.exe 0x004171cc.
    return false;
#endif
}

void AtomicUpdateUserPoint::Dump() {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0041724c has no body beyond instrumentation.
}

bool AtomicUpdateUserPoint::CanDo() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048d7b0.
    if (m_Type == 8 || m_Type == 1 || m_Type == 5) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_TalkerIndex));
        User* user = nullptr;
        if (creature != nullptr && creature->IsPC()) {
            user = creature->CastUser();
        }
        if (user != nullptr && (m_Type != 1 || user->m_data->m_bCanUsePCCafePoint)) {
            return user->CheckPointAddable(m_Type, m_point);
        }
        return false;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"only raid point, pccafe point and pvp point can be update")
    // Log.Add(
    //     LOG_ERROR,
    //     L"[jobs] Point update rejected: type %d is unsupported. Only raid, PC-cafe and PVP points are accepted.",
    //     m_Type)
    L2_LOG_ERROR(
        Log, u"[jobs] Point update rejected: type %d is unsupported; only raid, PC-cafe and PVP points are accepted.", m_Type
    );
    return false;
#else
    // Pinned L2NPC.exe 0x004172c8.
    return true;
#endif
}

AtomicSetNobless::AtomicSetNobless() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::SetNobless;
    nLine = 0x7c2;
}

AtomicSetNobless::AtomicSetNobless(unsigned int npcSid, unsigned int userSid, int noblessType) :
    mNpcSid(npcSid), mUserSid(userSid), mNoblessType(noblessType) {
    // Current EXE constructor sequence in CNPC::SetNobless at 0x0048e5c0.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::SetNobless;
    nLine = 0x7c2;
}

int AtomicSetNobless::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00413a40.
    return ::Assemble(buffer, bufferSize, "hddd", 0x1b, static_cast<int>(mNpcSid), static_cast<int>(mUserSid), mNoblessType);
}

const unsigned char* AtomicSetNobless::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00413b18.
    return ::Disassemble(packet, "ddd", &mNpcSid, &mUserSid, &mNoblessType);
}

bool AtomicSetNobless::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048b504.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(mUserSid);
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] AtomicSetNobless Failed. User sid[%d] is null",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0xb85,
        //     static_cast<int>(mUserSid))
        // Log.Add(
        //     LOG_WARN,
        //     L"[jobs] Noblesse update skipped: character SID %u is unavailable.",
        //     static_cast<unsigned int>(mUserSid))
        L2_LOG_WARN(
            Log, u"[jobs] Noblesse update skipped: character SID %u is unavailable.", static_cast<unsigned int>(mUserSid)
        );
        return false;
    }

    const int defaultOlympiadPoint =
        OlympiadDefine::GetClassIndex(user->m_data->m_nClass) != -1 ? 1000 : 0;
    DxSaveNoblessType request(
        static_cast<int>(user->GetDBID()), mNoblessType, defaultOlympiadPoint, static_cast<int>(mNpcSid),
        static_cast<int>(mUserSid)
    );
    db.RequestTPacketProcess(user->m_data->m_nWorldId, 0x1b, static_cast<int>(sizeof(request)), request);
    return true;
#else
    // Current L2NPC.exe 0x00413bd4.
    return false;
#endif
}

AtomicSetHero::AtomicSetHero() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::SetHero;
    nLine = 0x7e2;
}

AtomicSetHero::AtomicSetHero(unsigned int npcSid, unsigned int userSid, int heroType) :
    mNpcSid(npcSid), mUserSid(userSid), mHeroType(heroType) {
    // Current EXE constructor sequences in CNPC::SetHero and SetLegendary at
    // 0x0048e72c and 0x0048e830.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::SetHero;
    nLine = 0x7e2;
}

int AtomicSetHero::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00413c54.
    return ::Assemble(buffer, bufferSize, "hddd", 0x1c, static_cast<int>(mNpcSid), static_cast<int>(mUserSid), mHeroType);
}

const unsigned char* AtomicSetHero::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00413d2c.
    return ::Disassemble(packet, "ddd", &mNpcSid, &mUserSid, &mHeroType);
}

bool AtomicSetHero::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048b6d0.
    if (WOlympiadServiceConfig::inst().IsHost()) {
        return false;
    }

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(mUserSid);
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]AtomicSetHero Failed. "
        //     L"User is null: %d",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0xbc0,
        //     static_cast<int>(mUserSid))
        // Log.Add(
        //     LOG_WARN,
        //     L"[jobs] Hero update skipped: character SID %u is unavailable.",
        //     static_cast<unsigned int>(mUserSid))
        L2_LOG_WARN(
            Log, u"[jobs] Hero update skipped: character SID %u is unavailable.", static_cast<unsigned int>(mUserSid)
        );
        return false;
    }

    const bool accepted = WOlympiadServiceProxy::inst().SetHeroType(
        CSPointer<User>(user, ObjectRefType::FunctionChain), mHeroType, mNpcSid, false
    );
    if (!accepted) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0xbc6)
        // Log.Add(
        //     LOG_WARN,
        //     L"[jobs] Olympiad service did not accept hero type %d for character SID %u (NPC SID %u).",
        //     static_cast<int>(mHeroType),
        //     static_cast<unsigned int>(mUserSid),
        //     static_cast<unsigned int>(mNpcSid))
        L2_LOG_WARN(
            Log, u"[jobs] Olympiad service did not accept hero type %d for character SID %u (NPC SID %u).",
            static_cast<int>(mHeroType), static_cast<unsigned int>(mUserSid), static_cast<unsigned int>(mNpcSid)
        );
    }
    return accepted;
#else
    // Current L2NPC.exe 0x00413de8.
    return false;
#endif
}

AtomicCastBuff::AtomicCastBuff() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::CastBuff;
    nLine = 0x802;
}

AtomicCastBuff::AtomicCastBuff(unsigned int npcSidValue, unsigned int userSidValue, std::int64_t skillNameIdValue) :
    npcSid(npcSidValue), userSid(userSidValue), skillNameId(skillNameIdValue), skillType(0), actionMoveType(0) {
    // Current EXE constructor helper 0x00489314. The matching shared PDB
    // confirms that skillType and actionMoveType are both initialized to zero.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::CastBuff;
    nLine = 0x802;
}

int AtomicCastBuff::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00413e68 serializes only the three wire fields.
    return ::Assemble(
        buffer, bufferSize, "hddQ", 0x1d, static_cast<int>(npcSid), static_cast<int>(userSid),
        static_cast<long long>(skillNameId)
    );
}

const unsigned char* AtomicCastBuff::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00413f40.
    return ::Disassemble(packet, "ddQ", &npcSid, &userSid, &skillNameId);
}

bool AtomicCastBuff::CheckCancelCondition(CCreature* creature) {
#if defined(L2_BUILD_L2SERVER)
    return creature == nullptr || creature->m_data->m_bCanAct == 0 || creature->m_data->GetHP() <= 0.0 ||
        !creature->m_data->m_bIsActive || creature->m_data->m_bIsAlive == 0;
#else
    static_cast<void>(creature);
    return true;
#endif
}

bool AtomicCastBuff::Do() {
    L2_GUARD_FUNCTION();
    // Current L2Server 0x00491c30.
#if defined(L2_BUILD_L2SERVER)
    // if ((userSid & 0xf8000000U) != 0x48000000U) {
    if ((userSid & CLASS_MASK) != (SCLS_CREATURE << CLASS_SHIFT)) {
        return false;
    }

    CCreature* const npcObject = g_CreatureSmartIdManager.GetObjectW(npcSid);
    CNPC* const npc = npcObject != nullptr && npcObject->IsNPC() ? npcObject->CastNPC() : nullptr;
    CCreature* const target = g_CreatureSmartIdManager.GetObjectW(userSid);
    if (CheckCancelCondition(npc) || CheckCancelCondition(target) || (!target->IsPC() && !target->IsSummonOrPet())) {
        return false;
    }

    const FVector npcPosition = npc->GetPos();
    const FVector targetPosition = target->GetPos();
    const double deltaX = npcPosition.x - targetPosition.x;
    const double deltaY = npcPosition.y - targetPosition.y;
    const double deltaZ = npcPosition.z - targetPosition.z;
    const double distance = std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
    if (!(distance <= 6000.0)) {
        return false;
    }

    const std::uint64_t packedSkill = static_cast<std::uint64_t>(skillNameId);
    const int skillId = static_cast<int>(packedSkill >> 32);
    if (!(distance <= 6000.0)) {
        return false;
    }
    const std::int32_t heightDifference = runtime::AbsoluteInt32Wrapping(
        runtime::SubtractInt32Wrapping(
            runtime::Win64CvttDoubleToInt32(npcPosition.z), runtime::Win64CvttDoubleToInt32(targetPosition.z)
        )
    );
    if (skillId != 0x11a3 && skillId != 0x1077 && heightDifference >= 0x1f5) {
        return false;
    }

    const CSkillKey key(
        skillId, static_cast<int>(static_cast<std::uint16_t>(packedSkill >> 16)),
        static_cast<int>(static_cast<std::uint16_t>(packedSkill))
    );
    const CSkillInfo* const skill = g_SkillDB.GetSkillInfo(key, npc);
    if (skill == nullptr) {
        return false;
    }

    CSkillAction2 action;
    SkillUseInfo useInfo;
    useInfo.m_skillID = skill->m_nSkillId;
    useInfo.m_skillLevel = skill->m_nLevel;
    useInfo.m_skillSubLevel = skill->m_nSubLevel;
    useInfo.m_castRange = skill->m_nCastRange;
    useInfo.m_type = 0;
    useInfo.m_param1 = 0;

    CWorldObject* const targetWorld = target->CastWorldObject();
    npc->GetController()->ActionBroadcastSkillUse(
        *targetWorld, const_cast<CSkillInfo*>(skill), 0, 0, &action, nullptr, useInfo
    );
    skill->ActivateSkill(npc, targetWorld, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
    target->ValidateParameters(VB_SKILLMOD, false);
    return true;
#else
    return false;
#endif
}

AtomicCastBuffWithMpConsumption::AtomicCastBuffWithMpConsumption() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::CastBuffWithMpConsumption;
    nLine = 0x826;
}

AtomicCastBuffWithMpConsumption::AtomicCastBuffWithMpConsumption(
    unsigned int npcSidValue, unsigned int userSidValue, std::int64_t skillNameIdValue
) :
    npcSid(npcSidValue), userSid(userSidValue), skillNameId(skillNameIdValue), skillType(0), actionMoveType(0) {
    // Current EXE constructor helper 0x00489388. Matching shared PDB fields
    // skillType and actionMoveType are initialized to zero.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::CastBuffWithMpConsumption;
    nLine = 0x826;
}

int AtomicCastBuffWithMpConsumption::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0041407c.
    return ::Assemble(
        buffer, bufferSize, "hddQ", 0x23, static_cast<int>(npcSid), static_cast<int>(userSid),
        static_cast<long long>(skillNameId)
    );
}

const unsigned char* AtomicCastBuffWithMpConsumption::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00414154.
    return ::Disassemble(packet, "ddQ", &npcSid, &userSid, &skillNameId);
}

bool AtomicCastBuffWithMpConsumption::CheckCancelCondition(CCreature* creature) {
#if defined(L2_BUILD_L2SERVER)
    return creature == nullptr || creature->m_data->m_bCanAct == 0 || creature->m_data->GetHP() <= 0.0 ||
        !creature->m_data->m_bIsActive || creature->m_data->m_bIsAlive == 0;
#else
    static_cast<void>(creature);
    return true;
#endif
}

bool AtomicCastBuffWithMpConsumption::Do() {
    L2_GUARD_FUNCTION();
    // Current L2Server 0x00491f54.
#if defined(L2_BUILD_L2SERVER)
    // if ((userSid & 0xf8000000U) != 0x48000000U) {
    if ((userSid & CLASS_MASK) != (SCLS_CREATURE << CLASS_SHIFT)) {
        return false;
    }

    CCreature* const npcObject = g_CreatureSmartIdManager.GetObjectW(npcSid);
    CNPC* const npc = npcObject != nullptr && npcObject->IsNPC() ? npcObject->CastNPC() : nullptr;
    CCreature* const userObject = g_CreatureSmartIdManager.GetObjectW(userSid);
    User* const user = userObject != nullptr && userObject->IsPC() ? userObject->CastUser() : nullptr;
    if (CheckCancelCondition(npc) || CheckCancelCondition(user)) {
        return false;
    }

    const FVector npcPosition = npc->GetPos();
    const FVector userPosition = user->GetPos();
    const double deltaX = npcPosition.x - userPosition.x;
    const double deltaY = npcPosition.y - userPosition.y;
    const double deltaZ = npcPosition.z - userPosition.z;
    const double distance = std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
    const std::int32_t heightDifference = runtime::AbsoluteInt32Wrapping(
        runtime::SubtractInt32Wrapping(
            runtime::Win64CvttDoubleToInt32(npcPosition.z), runtime::Win64CvttDoubleToInt32(userPosition.z)
        )
    );
    if (heightDifference >= 0x1f5) {
        return false;
    }

    const std::uint64_t packedSkill = static_cast<std::uint64_t>(skillNameId);
    const CSkillKey key(
        static_cast<int>(packedSkill >> 32), static_cast<int>(static_cast<std::uint16_t>(packedSkill >> 16)),
        static_cast<int>(static_cast<std::uint16_t>(packedSkill))
    );
    const CSkillInfo* const skill = g_SkillDB.GetSkillInfo(key, npc);
    if (skill == nullptr) {
        return false;
    }

    const double mpConsumption = static_cast<double>(skill->m_nMpConsume2) + static_cast<double>(skill->m_nMpConsume1);
    const double currentMp = npc->m_data->m_dMP;
    const double baseMp = npc->m_data->m_dBaseMP;
    const double availableMp = currentMp < baseMp || currentMp == baseMp ? currentMp : baseMp;
    if (mpConsumption <= availableMp) {
        npc->ConsumeMP(mpConsumption);

        CSkillAction2 action;
        SkillUseInfo useInfo;
        CWorldObject* const targetWorld = user->CastWorldObject();
        npc->GetController()->ActionBroadcastSkillUse(
            *targetWorld, const_cast<CSkillInfo*>(skill), 0, 0, &action, nullptr, useInfo
        );
        skill->ActivateSkill(npc, targetWorld, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
        user->ValidateParameters(VB_SKILLMOD, false);

        const FVector logPosition = npc->GetPos();
        std::array<char16_t, 64> firstNpcClass{};
        std::array<char16_t, 64> secondNpcClass{};
        FormatUtf16To(firstNpcClass.data(), firstNpcClass.size(), u"%d", npc->GetNPCId());
        FormatUtf16To(secondNpcClass.data(), secondNpcClass.size(), u"%d", npc->GetNPCId());
        const int firstLoggedNpcId = npc->GetNPCId();
        const int secondLoggedNpcId = npc->GetNPCId();
        const std::wstring accountName = target_utf16::ToHostCodeUnits(user->m_sAccountName.data());
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,"
        //     L"%s,%s,%s,%s,",
        //     std::int64_t{0x193},
        //     static_cast<std::int64_t>(secondLoggedNpcId),
        //     static_cast<std::int64_t>(firstLoggedNpcId),
        //     static_cast<std::int64_t>(user->GetDBID()),
        //     static_cast<std::int64_t>(user->GetAccountID()),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(logPosition.x)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(logPosition.y)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(logPosition.z)),
        //     static_cast<std::int64_t>(user->m_data->m_Race),
        //     static_cast<std::int64_t>(user->m_data->m_Sex),
        //     static_cast<std::int64_t>(user->m_data->m_nClass),
        //     static_cast<std::int64_t>(user->m_data->m_nLevel),
        //     static_cast<std::int64_t>(skill->m_nSkillId),
        //     static_cast<std::int64_t>(
        //         static_cast<std::int32_t>(
        //             (static_cast<std::uint32_t>(skill->m_nSubLevel) << 16) |
        //             (static_cast<std::uint32_t>(skill->m_nLevel) & 0xffffU))),
        //     std::int64_t{0},
        //     std::int64_t{0},
        //     firstNpcClass.data(),
        //     secondNpcClass.data(),
        //     user->GetName(),
        //     accountName.c_str())
        Log.AddT(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,"
            u"%s,%s,%s,%s,",
            std::int64_t{0x193}, static_cast<std::int64_t>(secondLoggedNpcId), static_cast<std::int64_t>(firstLoggedNpcId),
            static_cast<std::int64_t>(user->GetDBID()), static_cast<std::int64_t>(user->GetAccountID()),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(logPosition.x)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(logPosition.y)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(logPosition.z)),
            static_cast<std::int64_t>(user->m_data->m_Race), static_cast<std::int64_t>(user->m_data->m_Sex),
            static_cast<std::int64_t>(user->m_data->m_nClass), static_cast<std::int64_t>(user->m_data->m_nLevel),
            static_cast<std::int64_t>(skill->m_nSkillId),
            static_cast<std::int64_t>(static_cast<std::int32_t>(
                (static_cast<std::uint32_t>(skill->m_nSubLevel) << 16) |
                (static_cast<std::uint32_t>(skill->m_nLevel) & 0xffffU)
            )),
            std::int64_t{0}, std::int64_t{0}, firstNpcClass.data(), secondNpcClass.data(), user->GetName(),
            accountName.c_str()
        );
        return true;
    }

    // Original: u"아지트 관리인의 MP가 부족합니다. MP를 회복할 때까지 기다려주세요."
    user->GetSocket()->SendSystemMessage(
        u"SYS", u"The clan hall manager does not have enough MP. Please wait until the MP recovers."
    );
    return false;
#else
    return false;
#endif
}

AtomicCastBuffForEventMaker::AtomicCastBuffForEventMaker() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::CastBuffForEventMaker;
    nLine = 0x848;
}

AtomicCastBuffForEventMaker::AtomicCastBuffForEventMaker(unsigned int npcSidValue, unsigned int userSidValue) :
    npcSid(npcSidValue), userSid(userSidValue) {
    // Current EXE constructor sequence in CNPC::CastBuffForEventMaker at
    // 0x0049a914.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::CastBuffForEventMaker;
    nLine = 0x848;
}

int AtomicCastBuffForEventMaker::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Original name is absent from the adjacent stale PDB but kept in
    // the current EXE call-stack string at 0x00414290.
    return ::Assemble(buffer, bufferSize, "hdd", 0x3b, static_cast<int>(npcSid), static_cast<int>(userSid));
}

const unsigned char* AtomicCastBuffForEventMaker::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00414360.
    return ::Disassemble(packet, "dd", &npcSid, &userSid);
}

bool AtomicCastBuffForEventMaker::CheckCancelCondition(CCreature* creature) {
#if defined(L2_BUILD_L2SERVER)
    return creature == nullptr || creature->m_data->m_bCanAct == 0 || creature->m_data->GetHP() <= 0.0 ||
        !creature->m_data->m_bIsActive || creature->m_data->m_bIsAlive == 0;
#else
    static_cast<void>(creature);
    return true;
#endif
}

bool AtomicCastBuffForEventMaker::Do() {
    L2_GUARD_FUNCTION();
    // Current L2Server 0x00492664. The target rejects non-creature smart
    // ids before either borrowed lookup and keeps the exact 6000-unit
    // three-dimensional range gate.
#if defined(L2_BUILD_L2SERVER)
    // if ((userSid & 0xf8000000U) != 0x48000000U) {
    if ((userSid & CLASS_MASK) != (SCLS_CREATURE << CLASS_SHIFT)) {
        return false;
    }

    CCreature* const npcObject = g_CreatureSmartIdManager.GetObjectW(npcSid);
    CNPC* const npc = npcObject != nullptr && npcObject->IsNPC() ? npcObject->CastNPC() : nullptr;
    CCreature* const target = g_CreatureSmartIdManager.GetObjectW(userSid);

    if (CheckCancelCondition(npc) || CheckCancelCondition(target) || (!target->IsPC() && !target->IsSummonOrPet())) {
        return false;
    }

    const FVector npcPosition = npc->GetPos();
    const FVector targetPosition = target->GetPos();
    const double distance = std::sqrt(
        (npcPosition.x - targetPosition.x) * (npcPosition.x - targetPosition.x) +
        (npcPosition.y - targetPosition.y) * (npcPosition.y - targetPosition.y) +
        (npcPosition.z - targetPosition.z) * (npcPosition.z - targetPosition.z)
    );
    if (distance > 6000.0) {
        return false;
    }
    return CEventMakerManager::inst().CastBuffSkillsToUser(npc, target);
#else
    return false;
#endif
}

AtomicRideWyvern::AtomicRideWyvern() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::RideWyvern;
    nLine = 0x539;
}

AtomicRideWyvern::AtomicRideWyvern(unsigned int userSID, int wyvernClassID) :
    m_UserSID(userSID), m_WyvernClassID(wyvernClassID) {
    // Current EXE constructor sequence in CNPC::RideWyvern at 0x0048cca4.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::RideWyvern;
    nLine = 0x539;
}

int AtomicRideWyvern::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00412028.
    return ::Assemble(buffer, bufferSize, "hdd", 9, static_cast<int>(m_UserSID), m_WyvernClassID);
}

const unsigned char* AtomicRideWyvern::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048290c.
    return ::Disassemble(packet, "dd", &m_UserSID, &m_WyvernClassID);
#else
    // Current EXE 0x004120f8 leaves the object unchanged.
    return packet;
#endif
}

bool AtomicRideWyvern::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048ab14.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(m_UserSID);
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    return user != nullptr && user->RideWyvern(m_WyvernClassID);
#else
    // Current L2NPC.exe 0x00412240.
    return false;
#endif
}

void AtomicRideWyvern::Dump() {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00412184.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AtomicRideWyvern::Dump() => "
    //     L"m_UserSID=[%d], m_WyvernClassID=[%d]",
    //     static_cast<int>(m_UserSID),
    //     m_WyvernClassID)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for wyvern ride: character SID %d, wyvern template %d.",
    //     static_cast<int>(m_UserSID),
    //     m_WyvernClassID)
    L2_LOG_INFO(
        Log, u"[jobs] Parameters for wyvern ride: character SID %d, wyvern template %d.", static_cast<int>(m_UserSID),
        m_WyvernClassID
    );
}

AtomicEventRide::AtomicEventRide() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::EventRide;
    nLine = 0x563;
}

AtomicEventRide::AtomicEventRide(unsigned int userSID, int petClassID, int duration, int cafePoint) :
    m_userSID(userSID), m_petClassID(petClassID), m_duration(duration), m_cafePoint(cafePoint) {
    // Current EXE constructor sequence in CNPC::RideForEvent at 0x0048cd98.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::EventRide;
    nLine = 0x563;
}

int AtomicEventRide::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004122c0.
    return ::Assemble(buffer, bufferSize, "hdddd", 0x26, static_cast<int>(m_userSID), m_petClassID, m_duration, m_cafePoint);
}

const unsigned char* AtomicEventRide::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x00482b5c.
    return ::Disassemble(packet, "dddd", &m_userSID, &m_petClassID, &m_duration, &m_cafePoint);
#else
    // Current L2NPC.exe 0x004123a0 leaves the object unchanged.
    return packet;
#endif
}

bool AtomicEventRide::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048abf8.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(m_userSID);
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    return user != nullptr && user->RideForEvent(m_petClassID, m_duration, m_cafePoint);
#else
    // Current L2NPC.exe 0x004124f4.
    return false;
#endif
}

void AtomicEventRide::Dump() {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0041242c keeps the AtomicRideWyvern label.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AtomicRideWyvern::Dump() => "
    //     L"m_userSID=[%d], m_petClassID=[%d], "
    //     L"m_duration=[%d], m_cafePoint=[%d]",
    //     static_cast<int>(m_userSID),
    //     m_petClassID,
    //     m_duration,
    //     m_cafePoint)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for event ride: character SID %d, pet template %d, duration %d minutes, event log ID %d.",
    //     static_cast<int>(m_userSID),
    //     m_petClassID,
    //     m_duration,
    //     m_cafePoint)
    L2_LOG_INFO(
        Log, u"[jobs] Parameters for event ride: character SID %d, pet template %d, duration %d minutes, event log ID %d.",
        static_cast<int>(m_userSID), m_petClassID, m_duration, m_cafePoint
    );
}

bool AtomicEventRide::CanDo() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048acec.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(m_userSID);
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    return user != nullptr && user->CanRideForEvent(m_petClassID);
#else
    // Current L2NPC.exe 0x00412574.
    return true;
#endif
}

AtomicSummonAirShip::AtomicSummonAirShip() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::SummonAirShip;
    nLine = 0xa1d;
}

AtomicSummonAirShip::AtomicSummonAirShip(int userSID, int airportId, int platformId) :
    m_UserSID(userSID), m_AirportId(airportId), m_PlatformId(platformId) {
    // Current EXE constructor sequence in CNPC::SummonAirShip at 0x00490090.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::SummonAirShip;
    nLine = 0xa1d;
}

int AtomicSummonAirShip::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0041510c.
    return ::Assemble(buffer, bufferSize, "hddd", 0x2e, m_UserSID, m_AirportId, m_PlatformId);
}

const unsigned char* AtomicSummonAirShip::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004151e4.
    return ::Disassemble(packet, "ddd", &m_UserSID, &m_AirportId, &m_PlatformId);
}

bool AtomicSummonAirShip::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x00492be8.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_UserSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user == nullptr) {
        return false;
    }
    return CAirShipManager::Inst().SummonFreeAirShip(user, ASTE_PLEDGE, m_AirportId, m_PlatformId);
#else
    // Current L2NPC.exe 0x004152a0.
    return false;
#endif
}

void AtomicSummonAirShip::Dump() {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00415320.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AtomicSummonAirShip::Dump() => "
    //     L"m_UserSID=[%d], m_AirportId=[%d], m_PlatformId=[%d]",
    //     m_UserSID,
    //     m_AirportId,
    //     m_PlatformId)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for airship summon: character SID %d, airport %d, platform %d.",
    //     m_UserSID,
    //     m_AirportId,
    //     m_PlatformId)
    L2_LOG_INFO(
        Log, u"[jobs] Parameters for airship summon: character SID %d, airport %d, platform %d.", m_UserSID, m_AirportId,
        m_PlatformId
    );
}

AtomicBuyAirShip::AtomicBuyAirShip() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::BuyAirShip;
    nLine = 0xa40;
}

AtomicBuyAirShip::AtomicBuyAirShip(int userSID, int typeId, int npcSID) :
    m_UserSID(userSID), m_TypeId(typeId), m_NpcSID(npcSID) {
    // Current EXE constructor sequence in CNPC::BuyAirShip at 0x0049019c.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::BuyAirShip;
    nLine = 0xa40;
}

int AtomicBuyAirShip::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004153e0.
    return ::Assemble(buffer, bufferSize, "hddd", 0x2f, m_UserSID, m_TypeId, m_NpcSID);
}

const unsigned char* AtomicBuyAirShip::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004154b8.
    return ::Disassemble(packet, "ddd", &m_UserSID, &m_TypeId, &m_NpcSID);
}

bool AtomicBuyAirShip::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x00492d10.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_UserSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user == nullptr) {
        return false;
    }
    return CAirShipManager::Inst().BuyFreeAirShip(user, ASTE_PLEDGE, m_TypeId, static_cast<std::uint32_t>(m_NpcSID));
#else
    // Current L2NPC.exe 0x00415574.
    return false;
#endif
}

void AtomicBuyAirShip::Dump() {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004155f4 keeps the AtomicSummonAirShip label.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AtomicSummonAirShip::Dump() => "
    //     L"m_UserSID=[%d], m_TypeId=[%d], m_NpcSID=[%d]",
    //     m_UserSID,
    //     m_TypeId,
    //     m_NpcSID)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for airship purchase: character SID %d, type %d, NPC SID %d.",
    //     m_UserSID,
    //     m_TypeId,
    //     m_NpcSID)
    L2_LOG_INFO(
        Log, u"[jobs] Parameters for airship purchase: character SID %d, type %d, NPC SID %d.", m_UserSID, m_TypeId, m_NpcSID
    );
}

AtomicGetOnAirShip::AtomicGetOnAirShip() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::GetOnAirShip;
    nLine = 0xa63;
}

AtomicGetOnAirShip::AtomicGetOnAirShip(int userSID, int airShipSID) : m_UserSID(userSID), m_AirShipSID(airShipSID) {
    // Current EXE constructor sequence in CNPC::GetOnAirShip at 0x004b75c8.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::GetOnAirShip;
    nLine = 0xa63;
}

int AtomicGetOnAirShip::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004156b4.
    return ::Assemble(buffer, bufferSize, "hdd", 0x30, m_UserSID, m_AirShipSID);
}

const unsigned char* AtomicGetOnAirShip::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00415784.
    return ::Disassemble(packet, "dd", &m_UserSID, &m_AirShipSID);
}

bool AtomicGetOnAirShip::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server 0x00492e38.
    CCreature* const candidate = g_CreatureSmartIdManager.GetObjectW(m_UserSID);
    User* const user = candidate != nullptr && candidate->IsPC() ? candidate->CastUser() : nullptr;
    if (user == nullptr) {
        return false;
    }
    return CAirShipManager::Inst().GetOnAirShip(user, static_cast<std::uint32_t>(m_AirShipSID));
#else
    // Current L2NPC 0x00415838.
    return false;
#endif
}

void AtomicGetOnAirShip::Dump() {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00415938.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AtomicGetOnAirShip::Dump() => "
    //     L"m_UserSID=[%d], m_AirShipSID=[%d]",
    //     m_UserSID,
    //     m_AirShipSID)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for airship boarding: character SID %d, airship SID %d.",
    //     m_UserSID,
    //     m_AirShipSID)
    L2_LOG_INFO(Log, u"[jobs] Parameters for airship boarding: character SID %d, airship SID %d.", m_UserSID, m_AirShipSID);
}

bool AtomicGetOnAirShip::CanDo() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server 0x00492f54.
    CCreature* const candidate = g_CreatureSmartIdManager.GetObjectW(m_UserSID);
    User* const user = candidate != nullptr && candidate->IsPC() ? candidate->CastUser() : nullptr;
    if (user == nullptr) {
        return false;
    }
    return CAirShipManager::Inst().CanGetOnAirShip(user, static_cast<std::uint32_t>(m_AirShipSID));
#else
    // Current L2NPC 0x004158b8.
    return true;
#endif
}

AtomicChangeTalkTarget::AtomicChangeTalkTarget() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ChangeTalkTarget;
    nLine = 0xae2;
}

AtomicChangeTalkTarget::AtomicChangeTalkTarget(int userSID, int npcSID) : m_userSID(userSID), m_npcSID(npcSID) {
    // Current EXE constructor sequence in CNPC::ChangeUserTalkTarget at
    // 0x004a2c80.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ChangeTalkTarget;
    nLine = 0xae2;
}

int AtomicChangeTalkTarget::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004159f4.
    return ::Assemble(buffer, bufferSize, "hdd", 0x32, m_userSID, m_npcSID);
}

const unsigned char* AtomicChangeTalkTarget::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00415ac4.
    return ::Disassemble(packet, "dd", &m_userSID, &m_npcSID);
}

bool AtomicChangeTalkTarget::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048cb7c.
    CCreature* const userCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_userSID));
    User* user = nullptr;
    if (userCreature != nullptr && userCreature->IsPC()) {
        user = userCreature->CastUser();
    }

    CCreature* const npcCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_npcSID));
    CNPC* npc = nullptr;
    if (npcCreature != nullptr && npcCreature->IsNPC()) {
        npc = npcCreature->CastNPC();
    }
    user->ChangeTarget(npc, CTR_DEFAULT);
    user->ChangeTalkTarget(npc);
    return true;
#else
    // Current L2NPC.exe 0x00415b78.
    return false;
#endif
}

void AtomicChangeTalkTarget::Dump() {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00415c78.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AtomicChangeTalkTarget::Dump() => "
    //     L"m_userSID=[%d], m_npcSID=[%d]",
    //     m_userSID,
    //     m_npcSID)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for talk-target change: character SID %d, NPC SID %d.",
    //     m_userSID,
    //     m_npcSID)
    L2_LOG_INFO(Log, u"[jobs] Parameters for talk-target change: character SID %d, NPC SID %d.", m_userSID, m_npcSID);
}

bool AtomicChangeTalkTarget::CanDo() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048ccb0.
    CCreature* const userCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_userSID));
    User* user = nullptr;
    if (userCreature != nullptr && userCreature->IsPC()) {
        user = userCreature->CastUser();
    }

    CCreature* const npcCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_npcSID));
    CNPC* npc = nullptr;
    if (npcCreature != nullptr && npcCreature->IsNPC()) {
        npc = npcCreature->CastNPC();
    }
    return user != nullptr && npc != nullptr;
#else
    // Pinned L2NPC.exe 0x00415bf8.
    return true;
#endif
}

AtomicRewardSkillEnchant::AtomicRewardSkillEnchant() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::RewardSkillEnchant;
    nLine = 0xb71;
}

AtomicRewardSkillEnchant::AtomicRewardSkillEnchant(int userSID, int itemId) : m_userSID(userSID), m_nItemId(itemId) {
    // Current EXE constructor sequence in CNPC::GiveRewardBySkillEnchant at
    // 0x004a3a14.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::RewardSkillEnchant;
    nLine = 0xb71;
}

int AtomicRewardSkillEnchant::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004166e8.
    return ::Assemble(buffer, bufferSize, "hdd", 0x34, m_userSID, m_nItemId);
}

const unsigned char* AtomicRewardSkillEnchant::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004167b8.
    return ::Disassemble(packet, "dd", &m_userSID, &m_nItemId);
}

bool AtomicRewardSkillEnchant::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048f3bc.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_userSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }

    std::int32_t rewardSp = 0;
    std::int32_t rewardItemCount = 0;
    {
        L2_GUARD(user->m_AcquiredSkillsSect);
        for (const auto& entry : user->m_AcquiredSkills) {
            const AcquiredSkillInfo& skill = entry.second;
            if (skill.m_nLevel <= 100) {
                continue;
            }
            rewardSp = runtime::AddInt32Wrapping(
                rewardSp, g_SkillEnchantDB.GetRewardSP(entry.first, skill.m_nLevel, user->GetClass())
            );
            rewardItemCount = runtime::AddInt32Wrapping(
                rewardItemCount, g_SkillEnchantDB.GetRewardItemCount(entry.first, skill.m_nLevel, user->Level())
            );
        }
    }

    if (rewardSp < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"reward sp invalid! [%s][%d]",
        //     user->GetName(),
        //     rewardSp)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Invalid skill-enchant SP reward for %ls: %d. Replacing it with zero and continuing the reward
        //     operation.", user->GetName(), rewardSp)
        // L2_LOG_ERROR(Log,
        //     L"[jobs] Invalid skill-enchant SP reward for %ls: %d. Replacing it with zero and continuing the reward
        //     operation.", user->GetName(), rewardSp)
        L2_LOG_ERROR(
            Log,
            u"[jobs] Invalid skill-enchant SP reward for %ls: %d; replacing it with zero and continuing the reward "
            u"operation.",
            user->GetName(), rewardSp
        );
        rewardSp = 0;
    }
    if (rewardItemCount < 0 || rewardItemCount > 8000) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"reward item count invalid! [%s][%d]",
        //     user->GetName(),
        //     rewardItemCount)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Invalid skill-enchant item reward for %ls: count %d is outside 0-8000. Replacing it with zero and
        //     continuing.", user->GetName(), rewardItemCount)
        // L2_LOG_ERROR(Log,
        //     L"[jobs] Invalid skill-enchant item reward for %ls: count %d is outside 0-8000. Replacing it with zero and
        //     continuing.", user->GetName(), rewardItemCount)
        L2_LOG_ERROR(
            Log,
            u"[jobs] Invalid skill-enchant item reward for %ls: count %d is outside 0-8000; replacing it with zero and "
            u"continuing.",
            user->GetName(), rewardItemCount
        );
        rewardItemCount = 0;
    }

    const std::int64_t newSp = user->SPInc(rewardSp);
    user->GetSocket()->Send("chccQ", 0x62, 0x14b, 1, 6, newSp);

    CObject* const rewardObject = g_ObjectDB.GetObjectW(m_nItemId, false);
    if (rewardObject != nullptr && rewardObject->IsItem()) {
        {
            L2_WRITE_GUARD(user->m_inv);
            const ItemOptionKey optionKey{};
            const BaseAttribute attribute;
            static_cast<void>(user->AddItemToInventory(
                m_nItemId, static_cast<std::int64_t>(rewardItemCount), ItemGetReason::GetFromNpc, 0, 0, 0, optionKey, 0,
                attribute
            ));
        }
    }

    user->m_data->SetNPCGiveItemJobDone();
    return true;
#else
    // Pinned L2NPC.exe 0x0041686c.
    return false;
#endif
}

void AtomicRewardSkillEnchant::Dump() {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0041696c.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AtomicRewardSkillEnchant::Dump() => "
    //     L"m_userSID=[%d], m_nItemId=[%d]",
    //     m_userSID,
    //     m_nItemId)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for skill-enchant reward: character SID %d, item template %d.",
    //     m_userSID,
    //     m_nItemId)
    L2_LOG_INFO(
        Log, u"[jobs] Parameters for skill-enchant reward: character SID %d, item template %d.", m_userSID, m_nItemId
    );
}

bool AtomicRewardSkillEnchant::CanDo() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048d400.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_userSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    return user != nullptr;
#else
    // Pinned L2NPC.exe 0x004168ec.
    return true;
#endif
}

AtomicAddTimer::AtomicAddTimer() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::AddTimer;
    nLine = 0x6c6;
}

AtomicAddTimer::AtomicAddTimer(int npcSID, int npcIndex, int timerId, int milliSec) :
    mNpcSID(npcSID), mNpcIndex(npcIndex), mTimerId(timerId), mMilliSec(milliSec) {
    // Current EXE constructor sequence in CNPC::AtomicAddTimerEx at
    // 0x0048a698.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::AddTimer;
    nLine = 0x6c6;
}

int AtomicAddTimer::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004131d8.
    return ::Assemble(buffer, bufferSize, "hdddd", 0x13, mNpcSID, mNpcIndex, mTimerId, mMilliSec);
}

const unsigned char* AtomicAddTimer::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004132b8.
    return ::Disassemble(packet, "dddd", &mNpcSID, &mNpcIndex, &mTimerId, &mMilliSec);
}

bool AtomicAddTimer::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x004838b8.
    g_npcserver.Send("cdddd", 0x48, mNpcSID, mNpcIndex, mTimerId, mMilliSec);
    return true;
#else
    // Pinned L2NPC.exe 0x0041337c.
    return false;
#endif
}

AtomicSetTicketBuyCount::AtomicSetTicketBuyCount() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::SetTicketBuyCount;
    nLine = 0x73a;
}

AtomicSetTicketBuyCount::AtomicSetTicketBuyCount(int userSID, int count) : mUserSID(userSID), mCount(count) {
    // Current EXE constructor sequence in CNPC::SetTicketBuyCount at
    // 0x0048d5e0.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::SetTicketBuyCount;
    nLine = 0x73a;
}

int AtomicSetTicketBuyCount::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004133fc.
    return ::Assemble(buffer, bufferSize, "hdd", 0x15, mUserSID, mCount);
}

const unsigned char* AtomicSetTicketBuyCount::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004134cc.
    return ::Disassemble(packet, "dd", &mUserSID, &mCount);
}

bool AtomicSetTicketBuyCount::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048b29c.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(mUserSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user == nullptr) {
        return false;
    }

    DxSetTicketBuyCount request(static_cast<int>(user->GetDBID()), mUserSID, mCount);
    db.RequestTPacketProcess(user->m_data->m_nWorldId, 9, static_cast<int>(sizeof(request)), request);
    return true;
#else
    // Current L2NPC.exe 0x00413580.
    return false;
#endif
}

AtomicAddWorldChatBonusPoint::AtomicAddWorldChatBonusPoint() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::AddWorldChatBonusPoint;
    nLine = 0xc81;
}

AtomicAddWorldChatBonusPoint::AtomicAddWorldChatBonusPoint(int userSID, int pointType, int point) :
    m_userSID(userSID), m_nPointType(pointType), m_nPoint(point) {
    // Current EXE constructor sequence in CNPC::AddWorldChatBonusPoint at
    // 0x004a3b74.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::AddWorldChatBonusPoint;
    nLine = 0xc81;
}

int AtomicAddWorldChatBonusPoint::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00417348.
    return ::Assemble(buffer, bufferSize, "hddd", 0x3c, m_userSID, m_nPointType, m_nPoint);
}

const unsigned char* AtomicAddWorldChatBonusPoint::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00417420.
    return ::Disassemble(packet, "ddd", &m_userSID, &m_nPointType, &m_nPoint);
}

bool AtomicAddWorldChatBonusPoint::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048db8c checks the point-type field at +0x2c.
    // the point amount at +0x30 is not consumed here.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_userSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    return user != nullptr && m_nPointType == 1;
#else
    // Pinned L2NPC.exe 0x004174dc.
    return false;
#endif
}

void AtomicAddWorldChatBonusPoint::Dump() {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0041755c calls the second field m_nType in the log even
    // though its matching PDB member name is m_nPointType.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AtomicAddWorldChatBonusPoint::Dump() => "
    //     L"m_userSID[%d] m_nType[%d], m_nPoint[%d]",
    //     m_userSID,
    //     m_nPointType,
    //     m_nPoint)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for world-chat bonus: character SID %d, point type %d, points %d.",
    //     m_userSID,
    //     m_nPointType,
    //     m_nPoint)
    L2_LOG_INFO(
        Log, u"[jobs] Parameters for world-chat bonus: character SID %d, point type %d, points %d.", m_userSID, m_nPointType,
        m_nPoint
    );
}

bool AtomicAddWorldChatBonusPoint::CanDo() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048dc78 repeats Do's validation exactly.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_userSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    return user != nullptr && m_nPointType == 1;
#else
    // Pinned L2NPC.exe 0x0041761c.
    return true;
#endif
}

AtomicAcceptUserQuest::AtomicAcceptUserQuest() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::AcceptUserQuest;
    nLine = 0xde6;
}

AtomicAcceptUserQuest::AtomicAcceptUserQuest(unsigned int creatureSID, int questId) :
    mCreatureSID(creatureSID), mQuestId(questId) {
    // Current EXE constructor sequence in CNPC::AcceptUserQuest at
    // 0x004a436c.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::AcceptUserQuest;
    nLine = 0xde6;
}

int AtomicAcceptUserQuest::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00418048.
    return ::Assemble(buffer, bufferSize, "hdd", 0x41, static_cast<int>(mCreatureSID), mQuestId);
}

const unsigned char* AtomicAcceptUserQuest::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00418118.
    return ::Disassemble(packet, "dd", &mCreatureSID, &mQuestId);
}

bool AtomicAcceptUserQuest::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048e3d0.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(mCreatureSID);
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user != nullptr) {
        user->AcceptQuest(mQuestId);
    }
    return user != nullptr;
#else
    // Current L2NPC.exe 0x004181cc.
    return false;
#endif
}

AtomicAddTimeAttackRecord::AtomicAddTimeAttackRecord() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::AddTimeAttackRecord;
    nLine = 0x650;
}

AtomicAddTimeAttackRecord::AtomicAddTimeAttackRecord(
    int roomNo, int partType, int partySID, std::int64_t point, int eventTime, int elapsedTime
) :
    mRoomNo(roomNo),
    mPartType(partType),
    mPartySID(partySID),
    mPoint(point),
    mEventTime(eventTime),
    mElapsedTime(elapsedTime) {
    // Current EXE constructor sequence in CNPC::AddTimeAttackRecord at
    // 0x0048d700.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::AddTimeAttackRecord;
    nLine = 0x650;
}

int AtomicAddTimeAttackRecord::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00412d38.
    return ::Assemble(
        buffer, bufferSize, "hdddQdd", 0x0d, mRoomNo, mPartType, mPartySID, static_cast<long long>(mPoint), mEventTime,
        mElapsedTime
    );
}

const unsigned char* AtomicAddTimeAttackRecord::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00412e28.
    return ::Disassemble(packet, "dddQdd", &mRoomNo, &mPartType, &mPartySID, &mPoint, &mEventTime, &mElapsedTime);
}

bool AtomicAddTimeAttackRecord::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x00483474.
    return TimeAttackBoard::inst().AddRecord(
        mRoomNo, static_cast<SSQPart::type>(mPartType), mPartySID, mPoint, mEventTime, mElapsedTime
    );
#else
    // Current L2NPC.exe 0x00412f00.
    return false;
#endif
}

AtomicAddTimeAttackFee::AtomicAddTimeAttackFee() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::AddTimeAttackFee;
    nLine = 0x762;
}

AtomicAddTimeAttackFee::AtomicAddTimeAttackFee(int roomNO, std::int64_t fee) : mRoomNO(roomNO), mFee(fee) {
    // Current EXE constructor sequence in CNPC::AddTimeAttackFee at
    // 0x0048d830.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::AddTimeAttackFee;
    nLine = 0x762;
}

int AtomicAddTimeAttackFee::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00413600.
    return ::Assemble(buffer, bufferSize, "hdQ", 0x17, mRoomNO, static_cast<long long>(mFee));
}

const unsigned char* AtomicAddTimeAttackFee::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004136d4.
    return ::Disassemble(packet, "dQ", &mRoomNO, &mFee);
}

bool AtomicAddTimeAttackFee::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x00483e58.
    return TimeAttackBoard::inst().AddFee(mRoomNO, mFee);
#else
    // Current L2NPC.exe 0x00413788 is guard-only and returns false.
    return false;
#endif
}

AtomicGiveTimeAttackReward::AtomicGiveTimeAttackReward() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::GiveTimeAttackReward;
    nLine = 0x78f;
}

AtomicGiveTimeAttackReward::AtomicGiveTimeAttackReward(
    int userSID, int roomNO, int questID, int itemClassID, std::int64_t count
) :
    mUserSID(userSID), mRoomNO(roomNO), mQuestID(questID), mItemClassID(itemClassID), mCount(count) {
    // Current EXE constructor sequence in CNPC::GiveTimeAttackReward at
    // 0x0048d938.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::GiveTimeAttackReward;
    nLine = 0x78f;
}

int AtomicGiveTimeAttackReward::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00413808.
    return ::Assemble(
        buffer, bufferSize, "hddddQ", 0x18, mUserSID, mRoomNO, mQuestID, mItemClassID, static_cast<long long>(mCount)
    );
}

const unsigned char* AtomicGiveTimeAttackReward::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004138f0.
    return ::Disassemble(packet, "ddddQ", &mUserSID, &mRoomNO, &mQuestID, &mItemClassID, &mCount);
}

bool AtomicGiveTimeAttackReward::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048b3d0.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(mUserSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user == nullptr) {
        return false;
    }

    ItemManip request(mQuestID, mItemClassID, mCount, 0, 0, 0);
    return TimeAttackBoard::inst().GiveReward(user, mRoomNO, request);
#else
    // Current L2NPC.exe 0x004139c0.
    return false;
#endif
}

AtomicAddSSQMember::AtomicAddSSQMember() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::AddSSQMember;
    nLine = 0x590;
}

AtomicAddSSQMember::AtomicAddSSQMember(
    int npcSID, int userSID, int ssqPart, int sealSelectionNo, int position, std::int64_t initialPoint, int joinType
) :
    mNpcSID(npcSID),
    mUserSID(userSID),
    mSSQPart(ssqPart),
    mSealSelectionNo(sealSelectionNo),
    mPosition(position),
    mInitialPoint(initialPoint),
    mJoinType(joinType) {
    // Current EXE constructor sequence in CNPC::AddSSQMember at 0x0048cea4.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::AddSSQMember;
    nLine = 0x590;
}

int AtomicAddSSQMember::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004125f4.
    return ::Assemble(
        buffer, bufferSize, "hdddddQd", 0x0b, mNpcSID, mUserSID, mSSQPart, mSealSelectionNo, mPosition,
        static_cast<long long>(mInitialPoint), mJoinType
    );
}

const unsigned char* AtomicAddSSQMember::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004126e8.
    return ::Disassemble(
        packet, "dddddQd", &mNpcSID, &mUserSID, &mSSQPart, &mSealSelectionNo, &mPosition, &mInitialPoint, &mJoinType
    );
}

bool AtomicAddSSQMember::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048add8.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(mUserSID));
    if (creature == nullptr || !creature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] AtomicAddSSQMember::Do(), Invalid user[%d]",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x96a,
        //     mUserSID)
        // Log.Add(
        //     LOG_WARN,
        //     L"[jobs] Seven Signs registration skipped: character SID %d is unavailable.",
        //     mUserSID)
        L2_LOG_WARN(Log, u"[jobs] Seven Signs registration skipped: character SID %d is unavailable.", mUserSID);
        return false;
    }

    User* const user = creature->CastUser();
    return SSQSystem::inst().AddMember(
        user, static_cast<SSQPart::type>(mSSQPart), static_cast<SSQSeal::type>(mSealSelectionNo),
        static_cast<SSQPosition::type>(mPosition), mInitialPoint, mJoinType
    );
#else
    // Current L2NPC.exe 0x004127cc.
    return false;
#endif
}

void AtomicAddSSQMember::Dump() {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0041284c has an empty body.
}

AtomicDeleteDepositedSSQItem::AtomicDeleteDepositedSSQItem() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::DeleteDepositedSSQItem;
    nLine = 0x5d0;
}

AtomicDeleteDepositedSSQItem::AtomicDeleteDepositedSSQItem(int userSID, int ssqPart, int itemType, std::int64_t count) :
    mUserSID(userSID), mSSQPart(ssqPart), mItemType(itemType), mCount(count) {
    // Current EXE constructor helper 0x0048921c.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::DeleteDepositedSSQItem;
    nLine = 0x5d0;
}

int AtomicDeleteDepositedSSQItem::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004128c8.
    return ::Assemble(buffer, bufferSize, "hdddQ", 0x0c, mUserSID, mSSQPart, mItemType, static_cast<long long>(mCount));
}

const unsigned char* AtomicDeleteDepositedSSQItem::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004129a8.
    return ::Disassemble(packet, "dddQ", &mUserSID, &mSSQPart, &mItemType, &mCount);
}

bool AtomicDeleteDepositedSSQItem::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048af14.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(mUserSID));
    if (creature == nullptr || !creature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] AtomicDepositSSQItem::Do(), Invalid user[%d]",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x9b6,
        //     mUserSID)
        // Log.Add(
        //     LOG_WARN,
        //     L"[jobs] Seven Signs deposited-item deletion skipped: character SID %d is unavailable.",
        //     mUserSID)
        L2_LOG_WARN(Log, u"[jobs] Seven Signs deposited-item deletion skipped: character SID %d is unavailable.", mUserSID);
        return false;
    }

    User* const user = creature->CastUser();
    return SSQSystem::inst().DeleteDepositedItem(
        user, static_cast<SSQPart::type>(mSSQPart), static_cast<SSQItem::type>(mItemType), mCount
    );
#else
    // Current L2NPC.exe 0x00412a6c.
    return false;
#endif
}

AtomicDepositSSQItemEx::AtomicDepositSSQItemEx() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::DepositSSQItemEx;
    nLine = 0x60c;
}

AtomicDepositSSQItemEx::AtomicDepositSSQItemEx(
    int userSID, int ssqPart, std::int64_t firstCount, std::int64_t secondCount, std::int64_t thirdCount, std::int64_t point
) :
    mUserSID(userSID), mSSQPart(ssqPart), mCountList{firstCount, secondCount, thirdCount}, mPoint(point) {
    // Current EXE constructor helper 0x00489290.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::DepositSSQItemEx;
    nLine = 0x60c;
}

int AtomicDepositSSQItemEx::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00412aec.
    return ::Assemble(
        buffer, bufferSize, "hddQQQQ", 0x19, mUserSID, mSSQPart, static_cast<long long>(mCountList[0]),
        static_cast<long long>(mCountList[1]), static_cast<long long>(mCountList[2]), static_cast<long long>(mPoint)
    );
}

const unsigned char* AtomicDepositSSQItemEx::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00412be0.
    return ::Disassemble(packet, "ddQQQQ", &mUserSID, &mSSQPart, &mCountList[0], &mCountList[1], &mCountList[2], &mPoint);
}

bool AtomicDepositSSQItemEx::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048b048.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(mUserSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] AtomicDepositSSQItem::Do(), Invalid user[%d]",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x9f8,
        //     mUserSID)
        // Log.Add(
        //     LOG_WARN,
        //     L"[jobs] Seven Signs item deposit skipped: character SID %d is unavailable.",
        //     mUserSID)
        L2_LOG_WARN(Log, u"[jobs] Seven Signs item deposit skipped: character SID %d is unavailable.", mUserSID);
        return false;
    }

    return SSQSystem::inst().DepositItemEx(user, static_cast<SSQPart::type>(mSSQPart), mCountList, mPoint);
#else
    // Current L2NPC.exe 0x00412cb8.
    return false;
#endif
}

AtomicParamChange::AtomicParamChange() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ParamChange;
    nLine = 0xc2;
}

AtomicParamChange::AtomicParamChange(
    int relatedQuestID, unsigned int creatureSID, ParameterType paramType, std::int64_t delta
) :
    mRelatedQuestID(relatedQuestID), mCreatureSID(creatureSID), mParamType(paramType), mDelta(delta) {
    // Current EXE inline constructor helper 0x00488ffc.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ParamChange;
    nLine = 0xc2;
}

int AtomicParamChange::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0040eedc.
    return ::Assemble(
        buffer, bufferSize, "hdddQ", 0, mRelatedQuestID, static_cast<int>(mCreatureSID), static_cast<int>(mParamType),
        static_cast<long long>(mDelta)
    );
}

const unsigned char* AtomicParamChange::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0040efb8.
    return ::Disassemble(packet, "dddQ", &mRelatedQuestID, &mCreatureSID, &mParamType, &mDelta);
}

bool AtomicParamChange::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x004887bc.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(mCreatureSID);
    if (creature == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] AtomicParamChange::Do(), cannot find creature",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0xd6)
        // Log.Add(
        //     LOG_WARN,
        //     L"[jobs] Parameter change skipped: creature SID %u was not found.",
        //     static_cast<unsigned int>(mCreatureSID))
        L2_LOG_WARN(
            Log, u"[jobs] Parameter change skipped: creature SID %u was not found.", static_cast<unsigned int>(mCreatureSID)
        );
        return false;
    }

    if (mDelta > 0 && (mParamType == PARAM_EXP || mParamType == PARAM_SP)) {
        mDelta = runtime::Win64CvttDoubleToInt64(static_cast<double>(mDelta) * g_dAIGiveEXPRate);
    }
    creature->IncrementParam(mParamType, mDelta);
    if (creature->IsPC()) {
        db.SendSaveCharacterInfo(creature->CastUser(), false, true);
        creature->CastUser()->SendUserInfo(nullptr, nullptr);

        CLogFormatter formatter;
        formatter.WriteLog1_1(
            0x149, creature, nullptr, nullptr, nullptr, nullptr, static_cast<std::int64_t>(mParamType), mDelta, 0, 0, 0, 0, 0
        );
    }
    return true;
#else
    // Current L2NPC.exe 0x0040f07c.
    return false;
#endif
}

void AtomicParamChange::Dump() {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0040f0fc uses %d for the 64-bit delta. On Win64 that
    // displays the low 32 bits, which the explicit cast reproduces without
    // passing a mismatched variadic argument.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AtomicParamChange::Dump() => "
    //     L"ParamType=[%d], Delta=[%d]",
    //     static_cast<int>(mParamType),
    //     static_cast<int>(mDelta))
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for parameter change: type %d, delta %lld.",
    //     static_cast<int>(mParamType),
    //     static_cast<long long>(mDelta))
    L2_LOG_INFO(
        Log, u"[jobs] Parameters for parameter change: type %d, delta %lld.", static_cast<int>(mParamType),
        static_cast<long long>(mDelta)
    );
}

AtomicTeleportParty::AtomicTeleportParty() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::TeleportParty;
    nLine = 0x686;
}

AtomicTeleportParty::AtomicTeleportParty(
    int partySID, int x, int y, int z, int distanceFromLeader, int privateID, unsigned int inZoneID
) :
    mPartySID(partySID),
    mX(x),
    mY(y),
    mZ(z),
    mDistanceFromLeader(distanceFromLeader),
    mPrivateID(privateID),
    mInZoneID(inZoneID) {
    // Current EXE constructor sequence in CNPC::TeleportParty at 0x0048dad0.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::TeleportParty;
    nLine = 0x686;
}

int AtomicTeleportParty::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00412f80.
    return ::Assemble(
        buffer, bufferSize, "hddddddd", 0x0e, mPartySID, mX, mY, mZ, mDistanceFromLeader, mPrivateID,
        static_cast<int>(mInZoneID)
    );
}

const unsigned char* AtomicTeleportParty::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00413074.
    return ::Disassemble(packet, "ddddddd", &mPartySID, &mX, &mY, &mZ, &mDistanceFromLeader, &mPrivateID, &mInZoneID);
}

bool AtomicTeleportParty::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x004908f8.
    CParty* const party = g_PartySmartIdManager.GetObjectW(static_cast<std::uint32_t>(mPartySID));
    if (party == nullptr) {
        return false;
    }

    User* const master = party->GetMaster();
    if (master != nullptr) {
        const std::uint32_t masterSid = master->m_nId;
        const FVector masterPosition = master->GetPos();

        std::vector<std::uint32_t> memberSids;
        party->GetAllMemberId(memberSids);
        for (const std::uint32_t memberSid : memberSids) {
            CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(memberSid);
            User* user = nullptr;
            if (creature != nullptr && creature->IsPC()) {
                user = creature->CastUser();
            }
            if (user == nullptr) {
                continue;
            }

            if (mDistanceFromLeader != 0 && user->m_nId != masterSid) {
                const FVector memberPosition = user->GetPos();
                const double deltaX = masterPosition.x - memberPosition.x;
                const double deltaY = masterPosition.y - memberPosition.y;
                const double deltaZ = masterPosition.z - memberPosition.z;
                const double distance = std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
                if (static_cast<double>(mDistanceFromLeader) <= distance) {
                    continue;
                }
            }

            if (user->GetInZoneID() == mInZoneID) {
                user->SetPrivateID(mPrivateID);
                user->TeleportToLocation(
                    mX, mY, mZ, mInZoneID, 0x10000, TeleportSourceType::AtomicTeleportParty, nullptr, 0
                );
            }
        }
    }
    return true;
#else
    // Current L2NPC.exe 0x00413158.
    return false;
#endif
}

AtomicAddFactionPoint::AtomicAddFactionPoint() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::AddFactionPoint;
    nLine = 0xcc2;
}

AtomicAddFactionPoint::AtomicAddFactionPoint(int userSID, int factionType, std::int64_t factionPoint, int reason) :
    m_nUserSID(userSID), m_nFactionType(factionType), m_nFactionPoint(factionPoint), m_nReason(reason) {
    // Current EXE constructor sequence in CNPC::AddFactionPoint at
    // 0x00490710.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::AddFactionPoint;
    nLine = 0xcc2;
}

int AtomicAddFactionPoint::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0041769c.
    return ::Assemble(
        buffer, bufferSize, "hddQd", 0x3d, m_nUserSID, m_nFactionType, static_cast<long long>(m_nFactionPoint), m_nReason
    );
}

const unsigned char* AtomicAddFactionPoint::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x0041777c.
    return ::Disassemble(packet, "ddQd", &m_nUserSID, &m_nFactionType, &m_nFactionPoint, &m_nReason);
}

bool AtomicAddFactionPoint::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048dd64.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nUserSID));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        return false;
    }

    const std::uint32_t factionIndexBits = static_cast<std::uint32_t>(m_nFactionType) - 1U;
    if (factionIndexBits >= 16U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] AtomicAddFactionPoint::Do(), Invalid "
        //     L"m_nFactionType(%d)!!!, UserName[%s]",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x15e4,
        //     m_nFactionType,
        //     user->GetName())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Faction point addition rejected: faction ID %d is outside 1-16 for %ls.",
        //     m_nFactionType,
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     L"[jobs] Faction point addition rejected: faction ID %d is outside 1-16 for %ls.",
        //     m_nFactionType,
        //     user->GetName())
        L2_LOG_ERROR(
            Log, u"[jobs] Faction point addition rejected: faction ID %d is outside 1-16 for %ls.", m_nFactionType,
            user->GetName()
        );
        return false;
    }

    const int factionIndex = static_cast<int>(factionIndexBits);
    if (FactionMaxLevel[factionIndex] < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] AtomicAddFactionPoint::Do(), Invalid "
        //     L"m_nFactionType(%d)!!!, UserName[%s]",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x15ea,
        //     m_nFactionType,
        //     user->GetName())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Faction point addition rejected: faction ID %d has no configured levels for %ls.",
        //     m_nFactionType,
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     L"[jobs] Faction point addition rejected: faction ID %d has no configured levels for %ls.",
        //     m_nFactionType,
        //     user->GetName())
        L2_LOG_ERROR(
            Log, u"[jobs] Faction point addition rejected: faction ID %d has no configured levels for %ls.", m_nFactionType,
            user->GetName()
        );
        return false;
    }

    if (m_nReason <= 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] AtomicAddFactionPoint::Do(), Invalid "
        //     L"m_nReason(%d)!!!, UserName[%s]",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x15f0,
        //     m_nReason,
        //     user->GetName())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Faction point addition rejected: reason %d must be positive for %ls.",
        //     m_nReason,
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     L"[jobs] Faction point addition rejected: reason %d must be positive for %ls.",
        //     m_nReason,
        //     user->GetName())
        L2_LOG_ERROR(
            Log, u"[jobs] Faction point addition rejected: reason %d must be positive for %ls.", m_nReason, user->GetName()
        );
        return false;
    }

    const std::int64_t maximumPoint =
        runtime::SubtractInt64Wrapping(FactionByLevel[factionIndex][FactionMaxLevel[factionIndex] - 1], 1);
    const std::int64_t addedPoint =
        runtime::AddInt64Wrapping(user->m_data->m_nFactionPoint[factionIndex], m_nFactionPoint);
    const bool atOrAboveMaximum = addedPoint >= maximumPoint;
    db.RequestUpdateFactionPoint(
        user, m_nFactionType, atOrAboveMaximum ? maximumPoint : m_nFactionPoint,
        atOrAboveMaximum ? protocol::Faction::SET : protocol::Faction::MODIFY, static_cast<protocol::Faction::Reason>(m_nReason)
    );
    return true;
#else
    // Current L2NPC.exe 0x00417840.
    return false;
#endif
}

void AtomicAddFactionPoint::Dump() {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00417940.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AtomicAddFactionPoint::Dump() => "
    //     L"m_nUserSID=[%d], m_nFactionType=[%d], "
    //     L"m_nFactionPoint=[%I64d], m_nReason=[%d]",
    //     m_nUserSID,
    //     m_nFactionType,
    //     static_cast<long long>(
    //         m_nFactionPoint),
    //     m_nReason)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for faction point addition: character SID %d, faction ID %d, delta %lld, reason %d.",
    //     m_nUserSID,
    //     m_nFactionType,
    //     static_cast<long long>(
    //             m_nFactionPoint),
    //     m_nReason)
    L2_LOG_INFO(
        Log, u"[jobs] Parameters for faction point addition: character SID %d, faction ID %d, delta %lld, reason %d.",
        m_nUserSID, m_nFactionType, static_cast<long long>(m_nFactionPoint), m_nReason
    );
}

bool AtomicAddFactionPoint::CanDo() {
    L2_GUARD_FUNCTION();
    // Current EXE 0x004178c0.
    return true;
}

AtomicSetFactionPoint::AtomicSetFactionPoint() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::SetFactionPoint;
    nLine = 0xce7;
}

AtomicSetFactionPoint::AtomicSetFactionPoint(int userSID, int factionType, std::int64_t factionPoint, int reason) :
    m_nUserSID(userSID), m_nFactionType(factionType), m_nFactionPoint(factionPoint), m_nReason(reason) {
    // Current EXE constructor sequence in CNPC::SetFactionPoint at
    // 0x00490904.
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::SetFactionPoint;
    nLine = 0xce7;
}

int AtomicSetFactionPoint::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00417a0c.
    return ::Assemble(
        buffer, bufferSize, "hddQd", 0x3e, m_nUserSID, m_nFactionType, static_cast<long long>(m_nFactionPoint), m_nReason
    );
}

const unsigned char* AtomicSetFactionPoint::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00417aec.
    return ::Disassemble(packet, "ddQd", &m_nUserSID, &m_nFactionType, &m_nFactionPoint, &m_nReason);
}

bool AtomicSetFactionPoint::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048df98.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nUserSID));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        return false;
    }

    const std::uint32_t factionIndexBits = static_cast<std::uint32_t>(m_nFactionType) - 1U;
    if (factionIndexBits >= 16U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] AtomicSetFactionPoint::Do(), Invalid "
        //     L"m_nFactionType(%d)!!!, UserName[%s]",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x164d,
        //     m_nFactionType,
        //     user->GetName())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Faction point assignment rejected: faction ID %d is outside 1-16 for %ls.",
        //     m_nFactionType,
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     L"[jobs] Faction point assignment rejected: faction ID %d is outside 1-16 for %ls.",
        //     m_nFactionType,
        //     user->GetName())
        L2_LOG_ERROR(
            Log, u"[jobs] Faction point assignment rejected: faction ID %d is outside 1-16 for %ls.", m_nFactionType,
            user->GetName()
        );
        return false;
    }

    const int factionIndex = static_cast<int>(factionIndexBits);
    if (FactionMaxLevel[factionIndex] < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] AtomicSetFactionPoint::Do(), Invalid "
        //     L"m_nFactionType(%d)!!!, UserName[%s]",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x1653,
        //     m_nFactionType,
        //     user->GetName())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Faction point assignment rejected: faction ID %d has no configured levels for %ls.",
        //     m_nFactionType,
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     L"[jobs] Faction point assignment rejected: faction ID %d has no configured levels for %ls.",
        //     m_nFactionType,
        //     user->GetName())
        L2_LOG_ERROR(
            Log, u"[jobs] Faction point assignment rejected: faction ID %d has no configured levels for %ls.",
            m_nFactionType, user->GetName()
        );
        return false;
    }

    if (m_nReason <= 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] AtomicSetFactionPoint::Do(), Invalid "
        //     L"m_nReason(%d)!!!, UserName[%s]",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x1659,
        //     m_nReason,
        //     user->GetName())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Faction point assignment rejected: reason %d must be positive for %ls.",
        //     m_nReason,
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     L"[jobs] Faction point assignment rejected: reason %d must be positive for %ls.",
        //     m_nReason,
        //     user->GetName())
        L2_LOG_ERROR(
            Log, u"[jobs] Faction point assignment rejected: reason %d must be positive for %ls.", m_nReason, user->GetName()
        );
        return false;
    }

    const std::int64_t maximumPoint =
        runtime::SubtractInt64Wrapping(FactionByLevel[factionIndex][FactionMaxLevel[factionIndex] - 1], 1);
    if (m_nFactionPoint > maximumPoint) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] AtomicSetFactionPoint::Do(), Overflow "
        //     L"FactionPoint[%I64d]",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x1662,
        //     static_cast<long long>(m_nFactionPoint))
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Faction point assignment rejected: requested %lld exceeds maximum %lld for faction ID %d.",
        //     static_cast<long long>(m_nFactionPoint),
        //     static_cast<long long>(maximumPoint),
        //     m_nFactionType)
        L2_LOG_ERROR(
            Log, u"[jobs] Faction point assignment rejected: requested %lld exceeds maximum %lld for faction ID %d.",
            static_cast<long long>(m_nFactionPoint), static_cast<long long>(maximumPoint), m_nFactionType
        );
        return false;
    }

    db.RequestUpdateFactionPoint(
        user, m_nFactionType, m_nFactionPoint, protocol::Faction::SET, static_cast<protocol::Faction::Reason>(m_nReason)
    );
    return true;
#else
    // Current L2NPC.exe 0x00417bb0.
    return false;
#endif
}

void AtomicSetFactionPoint::Dump() {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00417cb0.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AtomicSetFactionPoint::Dump() => "
    //     L"m_nUserSID=[%d], m_nFactionType=[%d], "
    //     L"m_nFactionPoint=[%I64d], m_nReason=[%d]",
    //     m_nUserSID,
    //     m_nFactionType,
    //     static_cast<long long>(
    //         m_nFactionPoint),
    //     m_nReason)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for faction point assignment: character SID %d, faction ID %d, points %lld, reason %d.",
    //     m_nUserSID,
    //     m_nFactionType,
    //     static_cast<long long>(
    //             m_nFactionPoint),
    //     m_nReason)
    L2_LOG_INFO(
        Log, u"[jobs] Parameters for faction point assignment: character SID %d, faction ID %d, points %lld, reason %d.",
        m_nUserSID, m_nFactionType, static_cast<long long>(m_nFactionPoint), m_nReason
    );
}

bool AtomicSetFactionPoint::CanDo() {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00417c30.
    return true;
}

int AtomicContentUIEvent::Assemble(char* buffer, int bufferSize) {
    // Current EXE 0x0040e8e4.
    if (m_eventType != NPCContentUIEvent::NCUT_MONSTER_ARENA) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]AtomicContentUIEvent invalid type",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x169a)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Cannot encode content-UI event: unsupported type %d. No event bytes produced.",
        //     static_cast<int>(m_eventType))
        L2_LOG_ERROR(
            Log, u"[jobs] Cannot encode content-UI event: unsupported type %d; no event bytes produced.",
            static_cast<int>(m_eventType)
        );
        return 0;
    }

    return ::Assemble(
        buffer, bufferSize, "hddddddc", 0x40, static_cast<int>(m_eventType), m_nUserSID, m_monArena.nInzoneTypeId,
        m_monArena.nStageProgress, m_monArena.nCurrentPoint, static_cast<int>(m_monArena.pointOpCode),
        m_monArena.bSendUI ? 1 : 0
    );
}

const unsigned char* AtomicContentUIEvent::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00417d7c checks a temporary event type and deliberately
    // does not copy it into m_eventType.
    int eventType = 0;
    packet = ::Disassemble(packet, "d", &eventType);
    if (eventType != NPCContentUIEvent::NCUT_MONSTER_ARENA) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]AtomicContentUIEvent invalid type",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x16b3)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Cannot decode content-UI event fields: unsupported type %d. Only the type field was consumed.",
        //     eventType)
        L2_LOG_ERROR(
            Log, u"[jobs] Cannot decode content-UI event fields: unsupported type %d; only the type field was consumed.",
            eventType
        );
        return packet;
    }

    char sendUI = 0;
    packet = ::Disassemble(
        packet, "dddddc", &m_nUserSID, &m_monArena.nInzoneTypeId, &m_monArena.nStageProgress, &m_monArena.nCurrentPoint,
        &m_monArena.pointOpCode, &sendUI
    );
    m_monArena.bSendUI = sendUI > 0;
    return packet;
}

bool AtomicContentUIEvent::Do() {
    // This is the folded false-return implementation confirmed by the
    // matching NASC64_2015 PDB.
    return false;
}

void AtomicContentUIEvent::Dump() {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00417f90.
    // Log.Add(
    //     LOG_ERROR,
    //     L"AtomicContentUIEvent::Dump() type[%d]",
    //     static_cast<int>(m_eventType))
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for content-UI event: type %d.",
    //     static_cast<int>(m_eventType))
    L2_LOG_INFO(Log, u"[jobs] Parameters for content-UI event: type %d.", static_cast<int>(m_eventType));
}

bool AtomicContentUIEvent::CanDo() {
    // This is the folded true-return implementation confirmed by the
    // matching NASC64_2015 PDB.
    return true;
}

void AtomicContentUIEvent::SetMonsterArenaInfo(
    int userSID,
    int inzoneTypeId,
    int stageProgress,
    int currentPoint,
    ContentMonsterArena::PointOpCode pointOpCode,
    bool sendUI
) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00417ec0.
    m_eventType = NPCContentUIEvent::NCUT_MONSTER_ARENA;
    m_monArena.nCurrentPoint = currentPoint;
    m_monArena.pointOpCode = pointOpCode;
    m_nUserSID = userSID;
    m_monArena.nInzoneTypeId = inzoneTypeId;
    m_monArena.bSendUI = sendUI;
    m_monArena.nStageProgress = stageProgress;
}

AtomicDropItem::AtomicDropItem() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::DropItem;
    nLine = 0x4c1;
}

AtomicDropItem::AtomicDropItem(std::uint32_t npcSID, int itemClassId, std::int64_t count, FVector& position) :
    mNpcSID(npcSID), mItemClassId(itemClassId), mItemCount(count), mvPos(position) {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::DropItem;
    nLine = 0x4c1;
}

int AtomicDropItem::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00411aa8.
    return ::Assemble(
        buffer, bufferSize, "hdQdddd", 8, static_cast<int>(mNpcSID), static_cast<long long>(mItemCount), mItemClassId,
        static_cast<int>(mvPos.x), static_cast<int>(mvPos.y), static_cast<int>(mvPos.z)
    );
}

const unsigned char* AtomicDropItem::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00482378.
    int x;
    int y;
    int z;
    const unsigned char* const next = ::Disassemble(packet, "dQdddd", &mNpcSID, &mItemCount, &mItemClassId, &x, &y, &z);
    const FVector position(x, y, z);
    mvPos = position;
    return next;
}

bool AtomicDropItem::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048a2e0.
    CCreature* const dropper = g_CreatureSmartIdManager.GetObjectW(mNpcSID);
    CObject* const itemObject = g_ObjectDB.GetObjectW(mItemClassId, false);
    if (itemObject == nullptr || !itemObject->IsItem()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] AtomicDropItem, Item[%d] is not exist.",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x7ef,
        //     mItemClassId)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Item drop rejected: item template %d is missing or is not an item.",
        //     mItemClassId)
        L2_LOG_ERROR(Log, u"[jobs] Item drop rejected: item template %d is missing or is not an item.", mItemClassId);
        return false;
    }

    FVector position{};
    if (dropper == nullptr) {
        position = mvPos;
    } else {
        position = dropper->GetPos();
    }

    CItem* const itemTemplate = static_cast<CItem*>(itemObject);
    if (CSharedItemData::IsStackable(itemTemplate->m_data->m_ItemConsumeType)) {
        CItem* const item = g_ObjectDB.CreateItem(mItemClassId);
        if (item == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] CreateItem Failed. item class[%d] nCount[%I64d]",
            //     L"..\\Shared\\AtomicJob.cpp",
            //     0x814,
            //     mItemClassId,
            //     static_cast<long long>(mItemCount))
            // Log.Add(
            //     LOG_ERROR,
            //     L"[jobs] Item drop could not create stackable item template %d (requested count %lld). No item dropped.",
            //     mItemClassId,
            //     static_cast<long long>(mItemCount))
            L2_LOG_ERROR(
                Log,
                u"[jobs] Item drop could not create stackable item template %d (requested count %lld); no item dropped.",
                mItemClassId, static_cast<long long>(mItemCount)
            );
        } else {
            if (!item->SetAmount(mItemCount)) {
                // The current image supplies this 64-bit lane to %d.
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] item amount is invalid[%d][%I64d]",
                //     "..\\Shared\\AtomicJob.cpp",
                //     0x807,
                //     static_cast<long long>(item->m_data->m_nCount),
                //     static_cast<long long>(mItemCount))
                // Log.Add(
                //     LOG_ERROR,
                //     L"[jobs] Item drop could not set the stack amount: current %lld, requested %lld. Continuing with the
                //     current amount.", static_cast<long long>(item->m_data->m_nCount), static_cast<long long>(mItemCount))
                L2_LOG_ERROR(
                    Log,
                    u"[jobs] Item drop could not set the stack amount: current %lld, requested %lld; continuing with the "
                    u"current amount.",
                    static_cast<long long>(item->m_data->m_nCount), static_cast<long long>(mItemCount)
                );
            }
            {
                L2_GUARD(*item->m_lock);
                if (dropper == nullptr || !dropper->IsBoss()) {
                    item->m_nDroppedTick = runtime::GetTickCount();
                } else {
                    item->m_nDroppedTick = runtime::GetTickCount() + 300000U;
                }
                item->m_bDroppedByAI = true;
            }
            item->AddDropPeriodTimer();
            static_cast<void>(CWorld::Inst().PutItem(item, GetRandomPosition(position), dropper));
        }
    } else {
        for (int index = 0; static_cast<std::int64_t>(index) < mItemCount; ++index) {
            CItem* const item = g_ObjectDB.CreateItem(mItemClassId);
            if (item == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] CreateItem Failed. item class[%d] nCount[%I64d]",
                //     L"..\\Shared\\AtomicJob.cpp",
                //     0x82a,
                //     mItemClassId,
                //     static_cast<long long>(mItemCount))
                // Log.Add(
                //     LOG_ERROR,
                //     L"[jobs] Item drop could not create item template %d (requested total %lld). Skipping this item and
                //     continuing the loop.", mItemClassId, static_cast<long long>(mItemCount))
                L2_LOG_ERROR(
                    Log,
                    u"[jobs] Item drop could not create item template %d (requested total %lld); skipping this item and "
                    u"continuing the loop.",
                    mItemClassId, static_cast<long long>(mItemCount)
                );
                continue;
            }

            {
                L2_GUARD(*item->m_lock);
                if (dropper == nullptr || !dropper->IsBoss()) {
                    item->m_nDroppedTick = runtime::GetTickCount();
                } else {
                    item->m_nDroppedTick = runtime::GetTickCount() + 300000U;
                }
                item->m_bDroppedByAI = true;
            }
            item->AddDropPeriodTimer();
            static_cast<void>(CWorld::Inst().PutItem(item, GetRandomPosition(position), dropper));
        }
    }
    return true;
#else
    // Current L2NPC.exe 0x00411ce4.
    return false;
#endif
}

void AtomicDropItem::Dump() {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AtomicDropItem::Dump() => "
    //     L"mItemCount=[%I64d], mItemClassId=[%d]",
    //     static_cast<long long>(mItemCount),
    //     mItemClassId)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for item drop: count %lld, item template %d.",
    //     static_cast<long long>(mItemCount),
    //     mItemClassId)
    L2_LOG_INFO(
        Log, u"[jobs] Parameters for item drop: count %lld, item template %d.", static_cast<long long>(mItemCount),
        mItemClassId
    );
}

AtomicDropItem2::AtomicDropItem2() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::DropItem2;
    nLine = 0x4f8;
}

AtomicDropItem2::AtomicDropItem2(
    std::uint32_t npcSID, int itemClassId, std::int64_t count, FVector& position, int contributorId
) :
    mNpcSID(npcSID),
    mItemClassId(itemClassId),
    mItemCount(count),
    mvPos(position),
    mContributorID(static_cast<std::uint32_t>(contributorId)) {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::DropItem2;
    nLine = 0x4f8;
}

int AtomicDropItem2::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00411d64.
    return ::Assemble(
        buffer, bufferSize, "hdQddddd", 0x1a, static_cast<int>(mNpcSID), static_cast<long long>(mItemCount), mItemClassId,
        static_cast<int>(mvPos.x), static_cast<int>(mvPos.y), static_cast<int>(mvPos.z), static_cast<int>(mContributorID)
    );
}

const unsigned char* AtomicDropItem2::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00482654.
    int x;
    int y;
    int z;
    const unsigned char* const next =
        ::Disassemble(packet, "dQddddd", &mNpcSID, &mItemCount, &mItemClassId, &x, &y, &z, &mContributorID);
    const FVector position(x, y, z);
    mvPos = position;
    return next;
}

bool AtomicDropItem2::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048a68c.
    CCreature* const dropper = g_CreatureSmartIdManager.GetObjectW(mNpcSID);
    CObject* const itemObject = g_ObjectDB.GetObjectW(mItemClassId, false);
    if (itemObject == nullptr || !itemObject->IsItem()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] AtomicDropItem, Item[%d] is not exist.",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x86e,
        //     mItemClassId)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Contributor-aware item drop rejected: item template %d is missing or is not an item.",
        //     mItemClassId)
        L2_LOG_ERROR(
            Log, u"[jobs] Contributor-aware item drop rejected: item template %d is missing or is not an item.", mItemClassId
        );
        return false;
    }

    FVector position{};
    if (dropper == nullptr) {
        position = mvPos;
    } else {
        position = dropper->GetPos();
    }

    CCreature* const contributorCreature = g_CreatureSmartIdManager.GetObjectW(mContributorID);
    User* contributor = nullptr;
    if (contributorCreature != nullptr && contributorCreature->IsPC()) {
        contributor = contributorCreature->CastUser();
    }

    CItem* const itemTemplate = static_cast<CItem*>(itemObject);
    if (CSharedItemData::IsStackable(itemTemplate->m_data->m_ItemConsumeType)) {
        CItem* const item = g_ObjectDB.CreateItem(mItemClassId);
        if (item == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] CreateItem Failed. item class[%d] nCount[%I64d]",
            //     L"..\\Shared\\AtomicJob.cpp",
            //     0x89a,
            //     mItemClassId,
            //     static_cast<long long>(mItemCount))
            // Log.Add(
            //     LOG_ERROR,
            //     L"[jobs] Contributor-aware item drop could not create stackable item template %d (requested count %lld)
            //     no item dropped.", mItemClassId, static_cast<long long>(mItemCount))
            L2_LOG_ERROR(
                Log,
                u"[jobs] Contributor-aware item drop could not create stackable item template %d (requested count %lld); no "
                u"item dropped.",
                mItemClassId, static_cast<long long>(mItemCount)
            );
        } else {
            if (!item->SetAmount(mItemCount)) {
                // The current image supplies this 64-bit lane to %d.
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] item amount is invalid[%d][%I64d]",
                //     "..\\Shared\\AtomicJob.cpp",
                //     0x888,
                //     static_cast<long long>(item->m_data->m_nCount),
                //     static_cast<long long>(mItemCount))
                // Log.Add(
                //     LOG_ERROR,
                //     L"[jobs] Contributor-aware item drop could not set the stack amount: current %lld, requested %lld.
                //     continuing with the current amount.", static_cast<long long>(item->m_data->m_nCount), static_cast<long
                //     long>(mItemCount))
                L2_LOG_ERROR(
                    Log,
                    u"[jobs] Contributor-aware item drop could not set the stack amount: current %lld, requested %lld; "
                    u"continuing with the current amount.",
                    static_cast<long long>(item->m_data->m_nCount), static_cast<long long>(mItemCount)
                );
            }
            if (contributor != nullptr && dropper != nullptr) {
                CContributeDataForEvent* const contribution = new CContributeDataForEvent(contributor);
                contribution->AddTimer(20000, 0);
                {
                    L2_GUARD(*item->m_lock);
                    item->m_ContributeDataID = contribution->GetSmartID();
                    if (!dropper->IsBoss()) {
                        item->m_nDroppedTick = runtime::GetTickCount();
                    } else {
                        item->m_nDroppedTick = runtime::GetTickCount() + 300000U;
                    }
                    item->m_bDroppedByAI = true;
                }
                item->AddDropPeriodTimer();
            }
            static_cast<void>(CWorld::Inst().PutItem(item, GetRandomPosition(position), dropper));
        }
    } else {
        for (std::int64_t index = 0; index < mItemCount; ++index) {
            CItem* const item = g_ObjectDB.CreateItem(mItemClassId);
            if (item == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] CreateItem Failed. item class[%d] nCount[%I64d]",
                //     L"..\\Shared\\AtomicJob.cpp",
                //     0x8b6,
                //     mItemClassId,
                //     static_cast<long long>(mItemCount))
                // Log.Add(
                //     LOG_ERROR,
                //     L"[jobs] Contributor-aware item drop could not create item template %d (requested total %lld)
                //     skipping this item and continuing the loop.", mItemClassId, static_cast<long long>(mItemCount))
                L2_LOG_ERROR(
                    Log,
                    u"[jobs] Contributor-aware item drop could not create item template %d (requested total %lld); skipping "
                    u"this item and continuing the loop.",
                    mItemClassId, static_cast<long long>(mItemCount)
                );
                continue;
            }

            if (contributor != nullptr && dropper != nullptr) {
                CContributeDataForEvent* const contribution = new CContributeDataForEvent(contributor);
                contribution->AddTimer(20000, 0);
                {
                    L2_GUARD(*item->m_lock);
                    item->m_ContributeDataID = contribution->GetSmartID();
                    if (!dropper->IsBoss()) {
                        item->m_nDroppedTick = runtime::GetTickCount();
                    } else {
                        item->m_nDroppedTick = runtime::GetTickCount() + 300000U;
                    }
                    item->m_bDroppedByAI = true;
                }
                item->AddDropPeriodTimer();
            }
            static_cast<void>(CWorld::Inst().PutItem(item, GetRandomPosition(position), dropper));
        }
    }
    return true;
#else
    // Current L2NPC.exe 0x00411fa8.
    return false;
#endif
}

void AtomicDropItem2::Dump() {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AtomicDropItem2::Dump() => "
    //     L"mItemCount=[%I64d], mItemClassId=[%d]",
    //     static_cast<long long>(mItemCount),
    //     mItemClassId)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for contributor-aware item drop: count %lld, item template %d.",
    //     static_cast<long long>(mItemCount),
    //     mItemClassId)
    L2_LOG_INFO(
        Log, u"[jobs] Parameters for contributor-aware item drop: count %lld, item template %d.",
        static_cast<long long>(mItemCount), mItemClassId
    );
}

AtomicNpcSetSkillAll::AtomicNpcSetSkillAll() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::NpcSetSkillAll;
    nLine = 0xbb9;
}

AtomicNpcSetSkillAll::AtomicNpcSetSkillAll(int talkerIndex, int type, int classId) :
    m_TalkerIndex(talkerIndex), m_Type(type), m_Class(classId) {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::NpcSetSkillAll;
    nLine = 0xbb9;
}

int AtomicNpcSetSkillAll::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00416d28.
    return ::Assemble(buffer, bufferSize, "hddd", 0x38, m_TalkerIndex, m_Type, m_Class);
}

const unsigned char* AtomicNpcSetSkillAll::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "ddd", &m_TalkerIndex, &m_Type, &m_Class);
}

bool AtomicNpcSetSkillAll::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server 0x0048f6fc.
    CCreatureFactory& creatures = CCreatureFactory::Inst();
    CCreature* creature = nullptr;
    if (m_TalkerIndex >= 0 && m_TalkerIndex < creatures.GetSize()) {
        creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(creatures.GetIdByIndex(m_TalkerIndex)));
    }
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    CPacketHelperEx<20480> deletePacket;
    int deleteCount = 0;
    int acquireType = 0;
    if (m_Type == 2) {
        acquireType = m_Class;
        if (m_Class < 0 || m_Class >= static_cast<int>(ESAT_MAX)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"invalid etc setskillall! talker %d: class %d",
            //     m_TalkerIndex,
            //     m_Class)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[jobs] NPC skill update rejected: NPC index %d requested invalid acquire type %d.",
            //     m_TalkerIndex,
            //     m_Class)
            L2_LOG_ERROR(
                Log, u"[jobs] NPC skill update rejected: NPC index %d requested invalid acquire type %d.", m_TalkerIndex,
                m_Class
            );
            return false;
        }

        User* const user = creature->CastUser();
        if (user != nullptr) {
            {
                L2_GUARD(creature->m_AcquiredSkillsSect);
                {
                    L2_GUARD(user->m_AcquiredHonorsSkillsLock);
                    // Noise: preliminary acquired-skill count and invariant zero delete count do not report an outcome.
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"AtomicNpcSetSkillAll [%d,%d]",
                    //     static_cast<int>(
                    //         user->m_AcquiredSkills.size()),
                    //     deleteCount)
                    for (const auto& entry : user->m_AcquiredSkills) {
                        const AcquiredSkillInfo& acquired = entry.second;
                        const CSkillInfo* const skill =
                            g_SkillDB.GetSkillInfo(CSkillKey(acquired.m_nId, acquired.m_nLevel, acquired.m_nSubLevel), user);
                        if (!skill->m_bSaveToDB && acquired.m_bSaveToDB) {
                            deletePacket.Assemble("d", acquired.m_nId);
                            ++deleteCount;
                        }
                    }
                    user->_DeleteAllAcquireSkills(true);
                    user->_AcquireAllHonorsSkill();
                    user->_AcquireSkills(g_SkillAcquireDB.GetAcquirableRaceSkills(user->m_data->m_Race), true, false);
                }
            }
        }
    } else {
        if (m_Type != 1) {
            if (m_Type != 0) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"invalid setskillall type! talker %d: type %d",
                //     m_TalkerIndex,
                //     m_Type)
                // Log.Add(
                //     LOG_ERROR,
                //     L"[jobs] NPC skill update rejected: NPC index %d requested unsupported mode %d.",
                //     m_TalkerIndex,
                //     m_Type)
                L2_LOG_ERROR(
                    Log, u"[jobs] NPC skill update rejected: NPC index %d requested unsupported mode %d.", m_TalkerIndex,
                    m_Type
                );
                return false;
            }
            m_Class = creature->m_data->m_nClass;
        }
        if (m_Class < 0 || m_Class >= 0xc0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"invalid class setskillall! talker %d: class %d",
            //     m_TalkerIndex,
            //     m_Class)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[jobs] NPC skill update rejected: NPC index %d requested class %d outside 0-191.",
            //     m_TalkerIndex,
            //     m_Class)
            L2_LOG_ERROR(
                Log, u"[jobs] NPC skill update rejected: NPC index %d requested class %d outside 0-191.", m_TalkerIndex,
                m_Class
            );
            return false;
        }
    }

    const auto& skills =
        g_SkillAcquireDB.GetAcquirableSkills(static_cast<EtcSkillAcquireType>(acquireType), static_cast<ClassType>(m_Class));
    std::optional<CPacketHelperEx<20480>> savePacketStorage;
    int saveCount = 0;
    {
        L2_GUARD(*creature->m_lock);
        creature->DisableSetItemSkill();
        creature->DisableNewSetItemSkill();
        const auto& pledgeSkills = g_SkillAcquireDB.GetAcquirableEtcSkills(ESAT_PLEDGE);
        const auto& subPledgeSkills = g_SkillAcquireDB.GetAcquirableEtcSkills(ESAT_SUB_PLEDGE);
        auto& savePacket = savePacketStorage.emplace();
        {
            L2_GUARD(creature->m_AcquiredSkillsSect);
            creature->_AcquireSkills(skills, false, false);
            if (m_Type == 2) {
                for (const SkillAcquireInfo* const skill : skills) {
                    if (skill != nullptr && !skill->bAutoGet) {
                        savePacket.Assemble("dddd", skill->nSkillID, skill->nSkillLevel, 0, 1);
                        ++saveCount;
                    }
                }
            } else {
                for (const auto& entry : creature->m_AcquiredSkills) {
                    const AcquiredSkillInfo& acquired = entry.second;
                    bool pledgeSkill = false;
                    for (const SkillAcquireInfo* const skill : pledgeSkills) {
                        if (skill->nSkillID == acquired.m_nId) {
                            pledgeSkill = true;
                        }
                    }
                    for (const SkillAcquireInfo* const skill : subPledgeSkills) {
                        if (skill->nSkillID == acquired.m_nId) {
                            pledgeSkill = true;
                        }
                    }
                    if (acquired.m_bSaveToDB && !pledgeSkill) {
                        savePacket.Assemble("dddd", acquired.m_nId, acquired.m_nLevel, acquired.m_nSubLevel, 1);
                        ++saveCount;
                    }
                }
            }

            if (creature->IsPC()) {
                creature->CastUser()->_OnRuneItemsChange();
            }
        }
        creature->ValidateSetItem(ST_MAX);
        creature->ValidateNewSetItem();
        creature->ValidateShapeShiftedSetItem();
    }

    if (!creature->IsPC()) {
        creature->ValidateChanged(VB_MAX);
        creature->SendMyInfo();
    } else {
        User* const user = creature->CastUser();
        if (user != nullptr) {
            user->ValidateChanged(VB_MAX);
            user->ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_BYNPC});
        }
    }
    db.RequestDeleteAndSaveSkillList(creature->CastUser(), deleteCount, &deletePacket, saveCount, &*savePacketStorage);
    return true;
#else
    return false;
#endif
}

bool AtomicNpcSetSkillAll::CanDo() {
    L2_GUARD_FUNCTION();
    return true;
}

void AtomicNpcSetSkillAll::Dump() { L2_GUARD_FUNCTION(); }

AtomicSetSkill::AtomicSetSkill() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::SetSkill;
    nLine = 0xb06;
}

AtomicSetSkill::AtomicSetSkill(int creatureSID, std::int64_t skillNameID) :
    m_CreatureSID(creatureSID), m_SkillNameID(skillNameID) {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::SetSkill;
    nLine = 0xb06;
}

int AtomicSetSkill::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00415d34.
    return ::Assemble(buffer, bufferSize, "hdQ", 0x35, m_CreatureSID, static_cast<long long>(m_SkillNameID));
}

const unsigned char* AtomicSetSkill::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00415e08.
    return ::Disassemble(packet, "dQ", &m_CreatureSID, &m_SkillNameID);
}

bool AtomicSetSkill::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048cdcc.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_CreatureSID));
    if (creature == nullptr) {
        return false;
    }

    const std::uint64_t packedSkill = static_cast<std::uint64_t>(m_SkillNameID);
    const int skillId = static_cast<int>(packedSkill >> 32);
    const int skillLevel = static_cast<int>(static_cast<std::uint16_t>(packedSkill >> 16));
    const int skillSubLevel = static_cast<int>(static_cast<std::uint16_t>(packedSkill));

    {
        L2_GUARD(*creature->m_lock);
        if (!creature->AcquireSkill(skillId, skillLevel, skillSubLevel, false, true)) {
            return false;
        }
    }

    if (!creature->IsPC()) {
        creature->ValidateChanged(VB_MAX);
    } else {
        creature->CastUser()->ValidateChanged(VB_MAX);
    }
    if (creature->IsPC()) {
        creature->CastUser()->SendAcquiredSkillToDB(skillId, skillLevel, skillSubLevel, true);
    }
    return true;
#else
    // Current L2NPC.exe 0x00415ebc.
    return false;
#endif
}

void AtomicSetSkill::Dump() { L2_GUARD_FUNCTION(); }

bool AtomicSetSkill::CanDo() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048cf64 accepts any creature
    // object and does not apply the IsPC/CastUser gate used by nearby jobs.
    return g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_CreatureSID)) != nullptr;
#else
    return true;
#endif
}

AtomicChangeClass::AtomicChangeClass() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ChangeClass;
    nLine = 0xb2a;
}

AtomicChangeClass::AtomicChangeClass(int userSID, int classId, int npcId, int targetMaxLevel) :
    m_UserSID(userSID), m_Class(classId), m_NpcID(npcId), m_nTargetMaxLevel(targetMaxLevel) {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ChangeClass;
    nLine = 0xb2a;
}

int AtomicChangeClass::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00416038.
    return ::Assemble(buffer, bufferSize, "hdddd", 0x36, m_UserSID, m_Class, m_NpcID, m_nTargetMaxLevel);
}

const unsigned char* AtomicChangeClass::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00416118.
    return ::Disassemble(packet, "dddd", &m_UserSID, &m_Class, &m_NpcID, &m_nTargetMaxLevel);
}

bool AtomicChangeClass::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server 0x0048d018.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_UserSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user == nullptr) {
        return false;
    }

    if (runtime::raw_atomic::CompareExchangeI32(&user->m_lSaveCharacter, 1, 0) != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to execute "
        //     L"pUser->SetSaveCharacterState(), used by "
        //     L"another thread, user_id(%d)",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x1199,
        //     static_cast<int>(user->m_nId))
        // Log.Add(
        //     LOG_WARN,
        //     L"[jobs] Class change skipped for character SID %u: another operation owns the character-save state.",
        //     static_cast<unsigned int>(user->m_nId))
        L2_LOG_WARN(
            Log, u"[jobs] Class change skipped for character SID %u: another operation owns the character-save state.",
            static_cast<unsigned int>(user->m_nId)
        );
        return false;
    }

    user->ChangeClass(static_cast<ClassType>(m_Class), m_NpcID);
    DxSetClass request(
        static_cast<int>(user->GetDBID()), user->m_data->m_nSubJobId, static_cast<int>(user->m_data->m_nClass)
    );
    db.RequestTPacketProcess(user->CastUser()->m_data->m_nWorldId, 0x23, static_cast<int>(sizeof(request)), request);

    int previousLevel;
    {
        L2_GUARD(*user->m_lock);
        previousLevel = user->m_data->m_nLevel;
    }

    bool saveCharacter = m_nTargetMaxLevel < 1;
    if (!saveCharacter) {
        user->SetLevel(static_cast<unsigned int>(m_nTargetMaxLevel));
        saveCharacter = previousLevel == m_nTargetMaxLevel;
    }
    if (saveCharacter) {
        db.SendSaveCharacterInfo(user->CastUser(), false, true);
    }
    return true;
#else
    return true;
#endif
}

void AtomicChangeClass::Dump() {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AtomicChangeClass::Dump() => "
    //     L"m_userSID=[%d], m_nClass=[%d]",
    //     m_UserSID,
    //     m_Class)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for class change: character SID %d, class %d.",
    //     m_UserSID,
    //     m_Class)
    L2_LOG_INFO(Log, u"[jobs] Parameters for class change: character SID %d, class %d.", m_UserSID, m_Class);
}

bool AtomicChangeClass::CanDo() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048d240.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_UserSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    return user != nullptr;
#else
    return true;
#endif
}

AtomicChangeClassAndSetSkill::AtomicChangeClassAndSetSkill() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ChangeClassAndSetSkill;
    nLine = 0xb4c;
}

AtomicChangeClassAndSetSkill::AtomicChangeClassAndSetSkill(int userSID, int classId, int npcId) :
    m_userSID(userSID), m_nClass(classId), m_nNpcId(npcId) {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ChangeClassAndSetSkill;
    nLine = 0xb4c;
}

int AtomicChangeClassAndSetSkill::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00416398.
    return ::Assemble(buffer, bufferSize, "hddd", 0x33, m_userSID, m_nClass, m_nNpcId);
}

const unsigned char* AtomicChangeClassAndSetSkill::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00416470.
    return ::Disassemble(packet, "ddd", &m_userSID, &m_nClass, &m_nNpcId);
}

bool AtomicChangeClassAndSetSkill::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server 0x0048ee54.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_userSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user == nullptr) {
        return false;
    }
    if (runtime::raw_atomic::CompareExchangeI32(&user->m_lSaveCharacter, 1, 0) != 0) {
        return false;
    }

    user->ChangeClass(static_cast<ClassType>(m_nClass), m_nNpcId);

    CPacketHelperEx<20480> deletePacket;
    int deleteCount = 0;
    {
        L2_GUARD(user->m_AcquiredSkillsSect);
        {
            L2_GUARD(user->m_AcquiredHonorsSkillsLock);
            for (const auto& entry : user->m_AcquiredSkills) {
                const AcquiredSkillInfo& acquired = entry.second;
                const CSkillInfo* const skill =
                    g_SkillDB.GetSkillInfo(CSkillKey(acquired.m_nId, acquired.m_nLevel, acquired.m_nSubLevel), user);
                if (!skill->m_bSaveToDB && acquired.m_bSaveToDB) {
                    deletePacket.Assemble("d", acquired.m_nId);
                    ++deleteCount;
                }
            }
            user->_DeleteAllAcquireSkills(true);
            user->_AcquireSkills(g_SkillAcquireDB.GetAcquirableRaceSkills(user->m_data->m_Race), true, false);
            user->_AcquireAllHonorsSkill();
        }
    }

    CPacketHelperEx<20480> savePacket;
    int saveCount = 0;
    const auto& skills = g_SkillAcquireDB.GetAcquirableSkills(user->m_data->m_nClass);
    {
        L2_GUARD(*user->m_lock);
        user->DisableSetItemSkill();
        user->DisableNewSetItemSkill();
        {
            L2_GUARD(user->m_AcquiredSkillsSect);
            {
                L2_GUARD(user->m_AcquiredHonorsSkillsLock);
                user->_AcquireSkills(skills, false, false);
                user->_OnRuneItemsChange();
                for (const SkillAcquireInfo* const skill : skills) {
                    if (skill != nullptr && skill->nGetLevel <= user->m_data->m_nLevel && !skill->bAutoGet) {
                        savePacket.Assemble("dddd", skill->nSkillID, skill->nSkillLevel, 0, 1);
                        ++saveCount;
                    }
                }
            }
        }
        user->ValidateSetItem(ST_MAX);
        user->ValidateNewSetItem();
        user->ValidateShapeShiftedSetItem();
    }

    user->ValidateChanged(VB_MAX);
    user->AcquireSubjobCertSkills();
    user->GetSocket()->Send("chccd", 0x62, 0x4f5, 1, 0xf, m_nClass);
    user->BroadcastSocialAction(0x84a, 0x800, 0);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR] AtomicJob DeleteAndSaveSkillList called. "
    //     L"%d/%d - %s",
    //     deleteCount,
    //     saveCount,
    //     user->GetName())
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Class and skills changed in memory. Requesting persistence of %d skill deletions and %d saves for %ls.",
    //     deleteCount,
    //     saveCount,
    //     user->GetName())
    // L2_LOG_INFO(Log,
    //     L"[jobs] Class and skills changed in memory. Requesting persistence of %d skill deletions and %d saves for %ls.",
    //     deleteCount,
    //     saveCount,
    //     user->GetName())
    L2_LOG_INFO(
        Log,
        u"[jobs] Class and skills changed in memory; requesting persistence of %d skill deletions and %d saves for %ls.",
        deleteCount, saveCount, user->GetName()
    );
    db.RequestDeleteAndSaveSkillList(user, deleteCount, &deletePacket, saveCount, &savePacket);
    user->SendETCBuffStatusInfo();
    db.SendSaveCharacterInfo(user, false, true);
    user->ValidateHenna();
    user->ValidateChanged(VB_SKILLLIST);
    return true;
#else
    return false;
#endif
}

void AtomicChangeClassAndSetSkill::Dump() {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AtomicChangeClassAndSetSkill::Dump() => "
    //     L"m_userSID=[%d], m_nClass=[%d]",
    //     m_userSID,
    //     m_nClass)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for class and skill change: character SID %d, class %d.",
    //     m_userSID,
    //     m_nClass)
    L2_LOG_INFO(Log, u"[jobs] Parameters for class and skill change: character SID %d, class %d.", m_userSID, m_nClass);
}

bool AtomicChangeClassAndSetSkill::CanDo() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048d320.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_userSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    return user != nullptr;
#else
    return true;
#endif
}

AtomicChangeDailyQuestFlag::AtomicChangeDailyQuestFlag() {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ChangeDailyQuestFlag;
    nLine = 0xb95;
}

AtomicChangeDailyQuestFlag::AtomicChangeDailyQuestFlag(int userSID, int questId) : m_userSID(userSID), m_questId(questId) {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ChangeDailyQuestFlag;
    nLine = 0xb95;
}

int AtomicChangeDailyQuestFlag::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00416a28.
    return ::Assemble(buffer, bufferSize, "hdd", 0x37, m_userSID, m_questId);
}

const unsigned char* AtomicChangeDailyQuestFlag::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current EXE 0x00416af8.
    return ::Disassemble(packet, "dd", &m_userSID, &m_questId);
}

bool AtomicChangeDailyQuestFlag::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048d4e0.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_userSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user != nullptr) {
        user->SetDailyQuest(m_questId, true);
        user->SendCompletedDailyQuest(m_questId);
    }
    return user != nullptr;
#else
    return false;
#endif
}

void AtomicChangeDailyQuestFlag::Dump() { L2_GUARD_FUNCTION(); }

bool AtomicChangeDailyQuestFlag::CanDo() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x0048d5dc.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_userSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    return user != nullptr;
#else
    return true;
#endif
}

AtomicChangePetName::AtomicChangePetName() : mCreatureSID(0), mPetCollarDbId(0) {
    mNewName[0] = u'\0';
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ChangePetName;
    nLine = 0x1ec;
}

AtomicChangePetName::AtomicChangePetName(unsigned int creatureSID, int petCollarDbId, const char16_t* newName) :
    mCreatureSID(creatureSID), mPetCollarDbId(petCollarDbId) {
    CopyAtomicPetName(mNewName, newName);
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ChangePetName;
    nLine = 0x1ec;
}

int AtomicChangePetName::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    return ::Assemble(buffer, bufferSize, "hddS", 0x1f, static_cast<int>(mCreatureSID), mPetCollarDbId, mNewName.data());
}

const unsigned char* AtomicChangePetName::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "ddS", &mCreatureSID, &mPetCollarDbId, 0x30, mNewName.data());
}

bool AtomicChangePetName::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(mCreatureSID);
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user == nullptr) {
        return false;
    }

    CItem* const item = user->m_inv.GetByDBID(mPetCollarDbId);
    if (item == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ChangePetName failed. Invalid pet collar DBID[%d]",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x28a,
        //     mPetCollarDbId)
        // Log.Add(
        //     LOG_WARN,
        //     L"[jobs] Pet rename skipped: collar DB ID %d was not found.",
        //     mPetCollarDbId)
        L2_LOG_WARN(Log, u"[jobs] Pet rename skipped: collar DB ID %d was not found.", mPetCollarDbId);
        return false;
    }
    if (!item->IsEtcItem() || item->m_const->m_EtcitemType != 7) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ChangePetName failed. Is not pet collar. Pet collar DBID[%d]",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x294,
        //     mPetCollarDbId)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Pet rename rejected: item DB ID %d is not a pet collar.",
        //     mPetCollarDbId)
        L2_LOG_ERROR(Log, u"[jobs] Pet rename rejected: item DB ID %d is not a pet collar.", mPetCollarDbId);
        return false;
    }

    CEtcItem* const etcItem = item->CastEtcItem();
    if (!etcItem->m_bPetCollarTradable) {
        CPet* const pet = user->GetPet();
        if (pet != nullptr) {
            pet->ChangePetName(mNewName.data());
            return true;
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ChangePetName failed. don't have pet",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x2a9)
        // Log.Add(
        //     LOG_WARN,
        //     L"[jobs] Pet rename skipped: no summoned pet object for collar DB ID %d.",
        //     mPetCollarDbId)
        L2_LOG_WARN(Log, u"[jobs] Pet rename skipped: no summoned pet object for collar DB ID %d.", mPetCollarDbId);
        return false;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] ChangePetName failed. Pet is not summoned. Pet collar DBID[%d]",
    //     L"..\\Shared\\AtomicJob.cpp",
    //     0x2a1,
    //     mPetCollarDbId)
    // Log.Add(
    //     LOG_WARN,
    //     L"[jobs] Pet rename rejected: pet with collar DB ID %d is not summoned.",
    //     mPetCollarDbId)
    L2_LOG_WARN(Log, u"[jobs] Pet rename rejected: pet with collar DB ID %d is not summoned.", mPetCollarDbId);
    return false;
#else
    return false;
#endif
}

void AtomicChangePetName::Dump() {
    L2_GUARD_FUNCTION();
    const std::wstring hostName = target_utf16::ToHostCodeUnits(mNewName.data());
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AtomicChangePetName::Dump() => mCreatureSID=[%d], mPetCollarDbId=[%d], mNewName=[%s]",
    //     mCreatureSID,
    //     mPetCollarDbId,
    //     hostName.c_str())
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for pet rename: creature SID %u, collar DB ID %d, new name %ls.",
    //     mCreatureSID,
    //     mPetCollarDbId,
    //     hostName.c_str())
    L2_LOG_INFO(
        Log, u"[jobs] Parameters for pet rename: creature SID %u, collar DB ID %d, new name %ls.", mCreatureSID,
        mPetCollarDbId, hostName.c_str()
    );
}

AtomicResetPetName::AtomicResetPetName() : mCreatureSID(0), mPetCollarDbId(0) {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ResetPetName;
    nLine = 0x221;
}

AtomicResetPetName::AtomicResetPetName(unsigned int creatureSID, int petCollarDbId) :
    mCreatureSID(creatureSID), mPetCollarDbId(petCollarDbId) {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ResetPetName;
    nLine = 0x221;
}

int AtomicResetPetName::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    return ::Assemble(buffer, bufferSize, "hdd", 0x20, static_cast<int>(mCreatureSID), mPetCollarDbId);
}

const unsigned char* AtomicResetPetName::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "dd", &mCreatureSID, &mPetCollarDbId);
}

bool AtomicResetPetName::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(mCreatureSID);
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user == nullptr) {
        return false;
    }

    CItem* const item = user->m_inv.GetByDBID(mPetCollarDbId);
    if (item == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ChangePetName failed. Invalid pet collar DBID[%d]",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x2e7,
        //     mPetCollarDbId)
        // Log.Add(
        //     LOG_WARN,
        //     L"[jobs] Pet name reset skipped: collar DB ID %d was not found.",
        //     mPetCollarDbId)
        L2_LOG_WARN(Log, u"[jobs] Pet name reset skipped: collar DB ID %d was not found.", mPetCollarDbId);
        return false;
    }
    if (!item->IsEtcItem() || item->m_const->m_EtcitemType != 7) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ChangePetName failed. Is not pet collar. Pet collar DBID[%d]",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x2f1,
        //     mPetCollarDbId)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Pet name reset rejected: item DB ID %d is not a pet collar.",
        //     mPetCollarDbId)
        L2_LOG_ERROR(Log, u"[jobs] Pet name reset rejected: item DB ID %d is not a pet collar.", mPetCollarDbId);
        return false;
    }

    CEtcItem* const etcItem = item->CastEtcItem();
    if (!etcItem->m_bPetCollarTradable) {
        CPet* const pet = user->GetPet();
        if (pet != nullptr) {
            pet->ChangePetName(u"");
            return true;
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ChangePetName failed. don't have pet",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x306)
        // Log.Add(
        //     LOG_WARN,
        //     L"[jobs] Pet name reset skipped: no summoned pet object for collar DB ID %d.",
        //     mPetCollarDbId)
        L2_LOG_WARN(Log, u"[jobs] Pet name reset skipped: no summoned pet object for collar DB ID %d.", mPetCollarDbId);
        return false;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] ChangePetName failed. Pet is not summoned. Pet collar DBID[%d]",
    //     L"..\\Shared\\AtomicJob.cpp",
    //     0x2fe,
    //     mPetCollarDbId)
    // Log.Add(
    //     LOG_WARN,
    //     L"[jobs] Pet name reset rejected: pet with collar DB ID %d is not summoned.",
    //     mPetCollarDbId)
    L2_LOG_WARN(Log, u"[jobs] Pet name reset rejected: pet with collar DB ID %d is not summoned.", mPetCollarDbId);
    return false;
#else
    return false;
#endif
}

void AtomicResetPetName::Dump() {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AtomicResetPetName::Dump() => mCreatureSID=[%d], mPetCollarDbId=[%d]",
    //     mCreatureSID,
    //     mPetCollarDbId)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for pet name reset: creature SID %u, collar DB ID %d.",
    //     mCreatureSID,
    //     mPetCollarDbId)
    L2_LOG_INFO(
        Log, u"[jobs] Parameters for pet name reset: creature SID %u, collar DB ID %d.", mCreatureSID, mPetCollarDbId
    );
}

AtomicTeleportTo::AtomicTeleportTo() : m_CreatureSID(0), m_X(0), m_Y(0), m_Z(0), m_Direction(0), m_InZoneID(0) {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::TeleportTo;
    nLine = 0x715;
}

AtomicTeleportTo::AtomicTeleportTo(int creatureSID, int x, int y, int z, unsigned int inZoneID) :
    AtomicTeleportTo(creatureSID, x, y, z, 0x10000, inZoneID) {}

AtomicTeleportTo::AtomicTeleportTo(int creatureSID, int x, int y, int z, int direction, unsigned int inZoneID) :
    m_CreatureSID(creatureSID), m_X(x), m_Y(y), m_Z(z), m_Direction(direction), m_InZoneID(inZoneID) {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::TeleportTo;
    nLine = 0x6f8;
}

int AtomicTeleportTo::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    return ::Assemble(buffer, bufferSize, "hdddddd", 0x16, m_CreatureSID, m_X, m_Y, m_Z, m_Direction, m_InZoneID);
}

const unsigned char* AtomicTeleportTo::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "dddddd", &m_CreatureSID, &m_X, &m_Y, &m_Z, &m_Direction, &m_InZoneID);
}

bool AtomicTeleportTo::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_CreatureSID));
    if (creature == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] AtomicTeleportTo::Do() failed. Invalid creature sid=[%d]",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0xad6,
        //     m_CreatureSID)
        // Log.Add(
        //     LOG_WARN,
        //     L"[jobs] Teleport skipped: creature SID %d was not found.",
        //     m_CreatureSID)
        L2_LOG_WARN(Log, u"[jobs] Teleport skipped: creature SID %d was not found.", m_CreatureSID);
        return false;
    }
    return creature->TeleportToLocation(
        m_X, m_Y, m_Z, m_InZoneID, m_Direction, TeleportSourceType::AtomicTeleportTo, nullptr, 0
    );
#else
    return false;
#endif
}

AtomicManipulatePVPPointWithItem::AtomicManipulatePVPPointWithItem() :
    m_userSID(0), m_delta(0), m_nItemClassID(0), m_nEnchanted(0), m_nCount(0) {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ManipulatePVPPointWithItem;
    nLine = 0xa00;
}

AtomicManipulatePVPPointWithItem::AtomicManipulatePVPPointWithItem(
    int userSID, int delta, int itemClassId, int enchanted, std::int64_t count
) :
    m_userSID(userSID), m_delta(delta), m_nItemClassID(itemClassId), m_nEnchanted(enchanted), m_nCount(count) {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ManipulatePVPPointWithItem;
    nLine = 0xa00;
}

int AtomicManipulatePVPPointWithItem::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    return ::Assemble(
        buffer, bufferSize, "hddddQ", 0x2a, m_userSID, m_delta, m_nItemClassID, m_nEnchanted,
        static_cast<long long>(m_nCount)
    );
}

const unsigned char* AtomicManipulatePVPPointWithItem::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "ddddQ", &m_userSID, &m_delta, &m_nItemClassID, &m_nEnchanted, &m_nCount);
}

bool AtomicManipulatePVPPointWithItem::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_userSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (m_delta < 1) {
        user->ManipulatePVPPointWithItem(m_delta, 3, m_nItemClassID, m_nCount, m_nEnchanted);
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"pvp point must be decreased!")
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] PVP point/item operation rejected: delta %d must be zero or negative.",
        //     m_delta)
        L2_LOG_ERROR(Log, u"[jobs] PVP point/item operation rejected: delta %d must be zero or negative.", m_delta);
    }
    return m_delta < 1;
#else
    return false;
#endif
}

void AtomicManipulatePVPPointWithItem::Dump() {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AtomicManipulatePVPPointWithItem::Dump() => m_userSID=[%d], m_delta=[%d], m_nItemClassID=[%d],
    //     m_nEnchanted=[%d], m_nCount=[%d]", m_userSID, m_delta, m_nItemClassID, m_nEnchanted, m_nCount)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for PVP point/item operation: character SID %d, point delta %d, item template %d, enchant %d,
    //     count %lld.", m_userSID, m_delta, m_nItemClassID, m_nEnchanted, static_cast<long long>(m_nCount))
    L2_LOG_INFO(
        Log,
        u"[jobs] Parameters for PVP point/item operation: character SID %d, point delta %d, item template %d, enchant %d, "
        u"count %lld.",
        m_userSID, m_delta, m_nItemClassID, m_nEnchanted, static_cast<long long>(m_nCount)
    );
}

bool AtomicManipulatePVPPointWithItem::CanDo() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_userSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    return user != nullptr && user->CheckPointAddable(5, -m_delta);
#else
    return true;
#endif
}

AtomicShowCardCollectSystem::AtomicShowCardCollectSystem() : m_userSID(0) {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ShowCardCollectSystem;
    nLine = 0xc14;
}

AtomicShowCardCollectSystem::AtomicShowCardCollectSystem(int userSID) : m_userSID(userSID) {
    tmTime = runtime::crt::Time32Now();
    eType = AtomicType::ShowCardCollectSystem;
    nLine = 0xc0a;
}

int AtomicShowCardCollectSystem::Assemble(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    return ::Assemble(buffer, bufferSize, "hd", 0x3a, m_userSID);
}

const unsigned char* AtomicShowCardCollectSystem::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "d", &m_userSID);
}

bool AtomicShowCardCollectSystem::Do() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_userSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user != nullptr) {
        user->OnShowCardCollectSystem();
    }
#endif
    return true;
}

void AtomicShowCardCollectSystem::Dump() {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_NORMAL,
    //     L"AtomicShowCardCollectSystem::Dump() => m_userSID[%d]",
    //     m_userSID)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[jobs] Parameters for card collection display: character SID %d.",
    //     m_userSID)
    L2_LOG_INFO(Log, u"[jobs] Parameters for card collection display: character SID %d.", m_userSID);
}

bool AtomicShowCardCollectSystem::CanDo() {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_L2SERVER)
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_userSID));
    return creature != nullptr && creature->IsPC() && creature->CastUser() != nullptr;
#else
    return true;
#endif
}

#if defined(L2_BUILD_L2SERVER)
bool AtomicItemDelete::Do() {
    // Current L2Server 0x0048c74c. Physical body owner Shared/AtomicJob.cpp.
    L2_GUARD_FUNCTION();
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_UserSID));
    User* user = nullptr;
    if (creature != nullptr && creature->IsPC()) {
        user = creature->CastUser();
    }
    if (user == nullptr) {
        return false;
    }

    SkillAcquireInfo* const acquireInfo = g_SkillAcquireDB.GetSkillAcquireInfo(
        m_AcquireType, user->m_data->m_nClass, m_SkillID, m_Level
    );
    if (acquireInfo == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] SkillAcquireInfo is null",
        //     L"..\\Shared\\AtomicJob.cpp",
        //     0x107d)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Skill purchase rejected: no acquire data for skill %d level %d, acquire type %d.",
        //     m_SkillID,
        //     m_Level,
        //     static_cast<int>(m_AcquireType))
        L2_LOG_ERROR(
            Log, u"[jobs] Skill purchase rejected: no acquire data for skill %d level %d, acquire type %d.", m_SkillID,
            m_Level, static_cast<int>(m_AcquireType)
        );
        return false;
    }

    {
        L2_GUARD(user->m_ModifySkillLock);
        user->SetSP(user->m_data->m_nSP - acquireInfo->nLevelUP_SP);
        const bool acquired = user->AcquireSkill(m_SkillID, m_Level, m_SubLevel, false, true);
        if (acquired && !acquireInfo->bAutoGet) {
            user->SendAcquiredSkillToDB(m_SkillID, m_Level, m_SubLevel, false);
        }
    }

    user->ValidateChanged(VB_MAX);
    user->ParamChanged(PCB_USER);
    user->SetMaxMagicLevel();
    const int packedLevel =
        static_cast<int>((static_cast<std::uint32_t>(m_SubLevel) << 16U) | static_cast<std::uint16_t>(m_Level));
    user->GetSocket()->Send("chccdd", 0x62, 0x115, 1, 4, m_SkillID, packedLevel);
    user->SendSkillAcquireList(m_AcquireType, false);
    user->SendSkillList(nullptr, true, m_SkillID);
    return false;
}
#endif

AtomicLog::AtomicLog(std::int32_t time, AtomicType::Enum type, int line) : m_tmTime(time), m_eType(type), m_nLine(line) {}

AtomicLogObjectMap m_vAtomicLogList;

void ObjectCacheTraits<AtomicJob>::OnPush(const int slot, AtomicJob* job) {
    AtomicLog* const value = new AtomicLog(job->tmTime, job->eType, job->nLine);
    if (job->eType == AtomicType::None || job->nLine == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"You must set eType[%d], nLine[%d]",
        //     static_cast<int>(job->eType),
        //     job->nLine)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Atomic-job cache metadata is incomplete: type %d, source line %d. Tracking insertion still follows.",
        //     static_cast<int>(job->eType),
        //     job->nLine)
        L2_LOG_ERROR(
            Log,
            u"[jobs] Atomic-job cache metadata is incomplete: type %d, source line %d; tracking insertion still follows.",
            static_cast<int>(job->eType), job->nLine
        );
    }

    if (!m_vAtomicLogList.Insert(static_cast<std::int64_t>(slot), value, const_cast<char*>(L2_SOURCE_FILE), __LINE__)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"mOut is duplicate mOut[%d]",
        //     slot)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Cannot track atomic-job cache slot %d: a tracking entry already exists.",
        //     slot)
        L2_LOG_ERROR(Log, u"[jobs] Cannot track atomic-job cache slot %d: a tracking entry already exists.", slot);
        const CSPointer<AtomicLog> old = m_vAtomicLogList.FindObject(static_cast<std::int64_t>(slot));
        if (old) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"AtomicValue Time[%d] / Type[%d] / Line[%d]",
            //     old->m_tmTime,
            //     static_cast<int>(old->m_eType),
            //     old->m_nLine)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[jobs] Existing entry for atomic-job cache slot %d: creation time %d (Unix seconds), type %d, source
            //     line %d.", slot, old->m_tmTime, static_cast<int>(old->m_eType), old->m_nLine)
            L2_LOG_ERROR(
                Log,
                u"[jobs] Existing entry for atomic-job cache slot %d: creation time %d (Unix seconds), type %d, source line "
                u"%d.",
                slot, old->m_tmTime, static_cast<int>(old->m_eType), old->m_nLine
            );
        }
    }
    L2_RELEASE(value, ObjectRefType::CreateDelete);
}

void ObjectCacheTraits<AtomicJob>::OnPop(const int slot) {
    if (!m_vAtomicLogList.DeleteObject(static_cast<std::int64_t>(slot))) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"mIn is not Fount mIn[%d]",
        //     slot)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[jobs] Cannot remove atomic-job tracking entry: cache slot %d was not found.",
        //     slot)
        L2_LOG_ERROR(Log, u"[jobs] Cannot remove atomic-job tracking entry: cache slot %d was not found.", slot);
    }
}
