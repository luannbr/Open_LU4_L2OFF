#pragma once

#include "Shared/src/MemorySub.h"

#include "Shared/src/BaseStructure.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>


class CFHTML;
class CDX_ATOMICJOB_BASE;

namespace network {
struct IPacket;
}

namespace AtomicType {

// AtomicType::Enum and its values are recovered from the exact 2015 donor
// PDB. Enum entries survive even when a later executable no longer contains
// the matching concrete class: current L2NPC.exe replaced the old
// DeleteDepositedSSQItemAndGiveRewards and ManipulatePVPPointWithItem jobs
// with AtomicDeleteDepositedSSQItem and AtomicUpdatePVPPoint, while its
// CreateAcademy/CreateSubPledge script natives are explicit deprecated stubs.
enum Enum : std::int32_t {
    None = 0,
    CreatePet = 1,
    ParamChange = 2,
    BuyLottoTicket = 3,
    EvolvePet = 4,
    EvolvePetWithSameExp = 5,
    DestroyPet = 6,
    ChangePetName = 7,
    ResetPetName = 8,
    AMemoControl = 9,
    ANRMemoControl = 10,
    ShowQuestPage = 11,
    ShowPage = 12,
    PledgeLevelUp = 13,
    SetResidenceDeco = 14,
    ResetResidenceDeco = 15,
    SetFortressFacility = 16,
    ResetFortressFacility = 17,
    StartObserver = 18,
    ShowQuestFHTML = 19,
    ShowFHTML = 20,
    DropItem = 21,
    DropItem2 = 22,
    RideWyvern = 23,
    EventRide = 24,
    AddSSQMember = 25,
    DeleteDepositedSSQItem = 26,
    DepositSSQItemEx = 27,
    AddTimeAttackRecord = 28,
    TeleportParty = 29,
    AddTimer = 30,
    TeleportTo = 31,
    SetTicketBuyCount = 32,
    AddTimeAttackFee = 33,
    GiveTimeAttackReward = 34,
    SetNobless = 35,
    SetHero = 36,
    CastBuff = 37,
    CastBuffWithMpConsumption = 38,
    AquirePledgeSkill = 39,
    CreateAcademy = 40,
    CreateSubPledge = 41,
    CouponUsed = 42,
    PledgeUnion = 43,
    CallPC = 44,
    CreateMPCC = 45,
    UpdatePCCafePoint = 46,
    DeleteAcquireSkill = 47,
    DeleteAcquireSkills = 48,
    UpdatePVPPoint = 49,
    ManipulatePVPPointWithItem = 50,
    SummonAirShip = 51,
    BuyAirShip = 52,
    GetOnAirShip = 53,
    ItemDelete = 54,
    QuestAccept = 55,
    QuestFinish = 56,
    ChangeTalkTarget = 57,
    SetSkill = 58,
    ChangeClass = 59,
    ChangeClassAndSetSkill = 60,
    RewardSkillEnchant = 61,
    ChangeDailyQuestFlag = 62,
    NpcSetSkillAll = 63,
    UpdateUserPoint = 64,
    ShowCardCollectSystem = 65,
    AlchemyResult = 66,
    ItemJobCheck = 67,
    SaveQuestInfo = 68,
    SkillEnchantNormal = 69,
    SkillEnchantSafe = 70,
    SkillEnchantForce = 71,
    SkillEnchantUntrain = 72,
    SkillEnchantRouteChange = 73,
    CastBuffForEventMaker = 74,
    AddWorldChatBonusPoint = 75,
    JobList = 76,
    PetDBJob = 77,
    AddFactionPoint = 78,
    SetFactionPoint = 79,
    OpenDecoNpc = 80,
    MonsterBookReward = 81,
    LambdaJob = 82,
    LambdaJobDx = 83,
    LambdaJobPacket = 84,
    AcceptUserQuest = 85,
};

} // namespace AtomicType

// Shared/AtomicJob.h enum recovered from the matching NASC64_2015 PDB.
enum ParameterType : std::int32_t {
    PARAM_EXP = 0,
    PARAM_SP = 1,
    PARAM_INT = 2,
    PARAM_STR = 3,
    PARAM_CON = 4,
    PARAM_MEN = 5,
    PARAM_DEX = 6,
    PARAM_WIT = 7,
    PARAM_LUC = 8,
    PARAM_CHA = 9,
    PARAM_LEVEL = 10,
    PARAM_PKCOUNT = 11,
};

namespace NPCContentUIEvent {

enum NPCContentUIType : std::int32_t {
    NCUT_MONSTER_ARENA = 0,
    NCUT_MAX = 1,
};

} // namespace NPCContentUIEvent

// Shared/AtomicJob.h polymorphic request root. AtomicJobCompositor owns these
// through Destroy() and calls Assemble() through virtual slot +0x20 in the
// current executable.
class AtomicJob : public MemoryObject {
public:
    AtomicJob() = default;
    ~AtomicJob() override = default;

    L2_DECLARE_OBJECT_TYPE(AtomicJob)

    virtual int Assemble(char* buffer, int bufferSize);
    virtual const unsigned char* Disassemble(const unsigned char* packet);
    virtual bool Do() = 0;
    virtual void Destroy();
    virtual void Dump();
    virtual bool CanDo();

    std::int32_t tmTime;
    AtomicType::Enum eType;
    int nLine;

private:
    // The Win64 base extent is 0x28 even though the last named member ends at
    // +0x24. Itanium-derived layouts otherwise reuse those four tail bytes,
    // moving every AtomicJob subclass field before its shipping offset.
    std::uint32_t m_targetBaseExtentPadding;
};


class AtomicCreateMPCC final : public AtomicJob {
public:
    AtomicCreateMPCC(std::uint32_t channelOwnerSid, std::uint32_t targetUserSid);
    ~AtomicCreateMPCC() override = default;

    bool Do() override;

private:
    std::uint32_t m_channelOwnerSid;
    std::uint32_t m_targetUserSid;
};


// Current L2Server's call-PC completion job. The item-manipulation reply
// calls Do(), which resolves the recalled user and verifies that the
// original recaller SID still owns the pending recall transaction.
class CAtomicCallPC : public AtomicJob {
public:
    CAtomicCallPC(std::uint32_t recalledUserSid, std::int32_t recallerSid);
    ~CAtomicCallPC() override = default;

    bool Do() override;

private:
    std::uint32_t m_recalledUserSid; // +0x28
    std::int32_t m_recallerSid; // +0x2c
};

static_assert(AtomicType::CallPC == 0x2c, "CAtomicCallPC must retain current AtomicType 0x2c");


// Current L2Server's Monster Book item-manipulation completion job. The
// current RTTI/vftable at 0x011fa558 proves the complete AtomicJob surface.
// only Do() is specialized, while the remaining virtuals stay inherited.
class AtomicMonsterBookReward final : public AtomicJob {
public:
    AtomicMonsterBookReward(std::int32_t userSid, std::int32_t cardId, std::int32_t cardLevel, std::int32_t factionIndex);
    ~AtomicMonsterBookReward() override = default;

    bool Do() override;

private:
    std::int32_t m_userSid; // +0x28
    std::int32_t m_cardId; // +0x2c
    std::int32_t m_cardLevel; // +0x30
    std::int32_t m_factionIndex; // +0x34
};


// Current L2Server's asynchronous alchemy completion owner. The current
// RTTI/vftable at 0x012146b8 proves the complete ten-slot AtomicJob surface.
// the three result methods below are its only overrides beyond the deleting
// destructor, while Assemble/Disassemble/Destroy remain inherited.
class AtomicAlchemyResult final : public AtomicJob {
public:
    struct ItemRollyRewardType {
        char nType; // +0x00
        std::array<std::byte, 0x03> m_alignment01To04; // +0x01..+0x03
        std::int32_t nItemClassId; // +0x04
        std::int64_t nItemAmount; // +0x08
    };

    struct ConvRewardInfo {
        std::int32_t nSuccess;
        std::int32_t nFail;
    };

    AtomicAlchemyResult(std::uint32_t userSid, char alchemyType, std::int32_t usedSkillId);
    ~AtomicAlchemyResult() override = default;

    bool Do() override;
    void Dump() override;
    bool CanDo() override;

    void AddRollyRewardInfo(char type, std::int32_t itemClassId, std::int64_t amount);
    void SetConvRewardInfo(std::int32_t success, std::int32_t fail);

    // User::TryRollyCube publishes these two result lanes directly before
    // handing the object to the item-manipulation queue in the current image.
    std::uint32_t m_nUserSID; // +0x28
    std::int32_t m_nEffectSkillId; // +0x2c
    char m_nAlchemyType; // +0x30
    std::array<std::byte, 0x03> m_alignment31To34; // +0x31..+0x33
    std::int32_t m_nUsedSkillId; // +0x34
    char m_nSuccessInfo; // +0x38
    std::array<std::byte, 0x07> m_alignment39To40; // +0x39..+0x3f
    std::vector<ItemRollyRewardType> m_vRollyRewardList; // +0x40..+0x57
    // The current constructor and deleting destructor leave this eight-byte
    // lane untouched. Its extent is current-proved, but no surviving access
    // binds it to a source type or name.
    std::array<std::byte, 0x08> m_opaque58To60; // +0x58..+0x5f
    ConvRewardInfo m_ConvInfo; // +0x60..+0x67
};


static_assert(AtomicType::MonsterBookReward == 0x51);
static_assert(AtomicType::AlchemyResult == 0x42);


// The process-lifetime pet DB rendezvous job constructed by user_summon.cpp.
// It carries no state beyond AtomicJob. The current object exists only to
// provide a stable ObjectCache callback identity for the four-step pet
// deposit/despawn handshake.
class AtomicPetDBJob final : public AtomicJob {
public:
    AtomicPetDBJob();

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;
    bool CanDo() override;
};


// Exact storage owner is user_summon.cpp. Both that owner and DB.cpp consume
// the same process-lifetime pointer (current address 0x20d8f2e0).
extern AtomicPetDBJob* g_AtomicPetDBJob;

// Current L2Server calls the user_summon.cpp dynamic initializer at
// 0x00b17c38 from the startup sequence instead of before WinMain. This
// explicit entry keeps that observable lifetime in portable builds.
void InitializeAtomicPetDBJob();

// Shared/AtomicJob.h:3418-3543. These three abstract carriers are the
// shipping callback bridge used by ordinary DB replies, DX packets and the
// typed network packet layer respectively. Their one result/pointer field
// starts at +0x28 and the target rounds every base to 0x30 bytes.
class AtomicLambdaJob : public AtomicJob {
public:
    AtomicLambdaJob();
    ~AtomicLambdaJob() override;

    int m_nResult; // +0x28
};

class AtomicLambdaJobDx : public AtomicJob {
public:
    AtomicLambdaJobDx();
    ~AtomicLambdaJobDx() override;

    CDX_ATOMICJOB_BASE* m_pDx; // +0x28
};

class AtomicLambdaJobPacket : public AtomicJob {
public:
    AtomicLambdaJobPacket();
    ~AtomicLambdaJobPacket() override;

    network::IPacket* m_pPacket; // +0x28
};


template <typename Function>
class AtomicLambdaJobImplement final : public AtomicLambdaJob {
public:
    explicit AtomicLambdaJobImplement(Function function) : AtomicLambdaJob(), m_function(std::move(function)) {}

    bool Do() override {
        std::invoke(m_function, m_nResult);
        return true;
    }

private:
    Function m_function;
};

template <typename Function>
class AtomicLambdaJobDxImplement final : public AtomicLambdaJobDx {
public:
    explicit AtomicLambdaJobDxImplement(Function function) : AtomicLambdaJobDx(), m_function(std::move(function)) {}

    bool Do() override {
        std::invoke(m_function, this);
        return true;
    }

private:
    Function m_function;
};

template <typename Function>
class AtomicLambdaJobPacketImplement final : public AtomicLambdaJobPacket {
public:
    explicit AtomicLambdaJobPacketImplement(Function function) : AtomicLambdaJobPacket(), m_function(std::move(function)) {}

    bool Do() override {
        std::invoke(m_function, this);
        return true;
    }

private:
    Function m_function;
};

template <typename Function>
AtomicLambdaJob* MakeLambdaJob(Function&& function) {
    using StoredFunction = std::decay_t<Function>;
    return new AtomicLambdaJobImplement<StoredFunction>(std::forward<Function>(function));
}

template <typename Function>
AtomicLambdaJobDx* MakeLambdaJobDx(Function&& function) {
    using StoredFunction = std::decay_t<Function>;
    return new AtomicLambdaJobDxImplement<StoredFunction>(std::forward<Function>(function));
}

template <typename Function>
AtomicLambdaJobPacket* MakeLambdaJobPacket(Function&& function) {
    using StoredFunction = std::decay_t<Function>;
    return new AtomicLambdaJobPacketImplement<StoredFunction>(std::forward<Function>(function));
}

class AtomicCreatePet : public AtomicJob {
public:
    AtomicCreatePet();
    AtomicCreatePet(unsigned int creatureSID, int petCollarClassId, int petNpcClassId, int petLevel);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;

private:
    unsigned int mCreatureSID;
    int mPetCollarClassId;
    int mPetNpcClassId;
    int mPetLevel;
};


class AtomicBuyLottoTicket : public AtomicJob {
public:
    AtomicBuyLottoTicket();
    AtomicBuyLottoTicket(unsigned int creatureSID, int roundNumber, int numberFlag);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;

private:
    unsigned int CreatureSID;
    int RoundNumber;
    int NumberFlag;
};


class AtomicEvolvePet : public AtomicJob {
public:
    AtomicEvolvePet();
    AtomicEvolvePet(
        unsigned int creatureSID,
        int originalPetCollarDbId,
        int originalPetNpcClassId,
        int petCollarClassId,
        int petNpcClassId,
        int petLevel
    );

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;

private:
    unsigned int mCreatureSID;
    int mOriginalPetCollarDbId;
    int mOriginalPetNpcClassId;
    int mPetCollarClassId;
    int mPetNpcClassId;
    int mPetLevel;
};


class AtomicEvolvePetWithSameExp : public AtomicJob {
public:
    AtomicEvolvePetWithSameExp();
    AtomicEvolvePetWithSameExp(unsigned int creatureSID, int newPetIndex);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;

private:
    unsigned int m_creatureSID;
    int m_newPetIndex;
};


class AtomicDestroyPet : public AtomicJob {
public:
    AtomicDestroyPet();
    AtomicDestroyPet(unsigned int creatureSID, int petCollarDbId, int level);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;

private:
    unsigned int mCreatureSID;
    int mPetCollarDbId;
    int mLevel;
};


class AtomicChangePetName : public AtomicJob {
public:
    AtomicChangePetName();
    AtomicChangePetName(unsigned int creatureSID, int petCollarDbId, const char16_t* newName);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;

private:
    unsigned int mCreatureSID;
    int mPetCollarDbId;
    std::array<char16_t, 24> mNewName;
};


class AtomicResetPetName : public AtomicJob {
public:
    AtomicResetPetName();
    AtomicResetPetName(unsigned int creatureSID, int petCollarDbId);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;

private:
    unsigned int mCreatureSID;
    int mPetCollarDbId;
};


class MemoControl : public AtomicJob {
public:
    enum TYPE : std::int32_t {
        SET_MEMO = 0,
        SET_STATE = 1,
        SET_JOURNAL = 2,
        SET_FJOURNAL = 3,
        RESET_FJOURNAL = 4,
        REMOVE = 5,
        SET_ONETIME_QUEST_FLAG = 6,
        UNKOWN = 7,
    };

    MemoControl();
    MemoControl(unsigned int creatureSID, TYPE controlType, int questId, int argument1, int argument2);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;

private:
    unsigned int mCreatureSID;
    TYPE mControlType;
    int mQuestId;
    int mArg1;
    int mArg2;
};


class NRMemoControl : public AtomicJob {
public:
    enum TYPE : std::int32_t {
        SET_MEMO = 0,
        SET_STATE = 1,
        SET_JOURNAL = 2,
        SET_FJOURNAL = 3,
        RESET_FJOURNAL = 4,
        REMOVE = 5,
        SET_ONETIME_QUEST_FLAG = 6,
        UNKOWN = 7,
    };

    NRMemoControl();
    NRMemoControl(unsigned int creatureSID, TYPE controlType, int questId, int argument1, int argument2);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;

private:
    unsigned int mCreatureSID;
    TYPE mControlType;
    int mQuestId;
    int mArg1;
    int mArg2;
};


class AtomicShowQuestPage : public AtomicJob {
public:
    AtomicShowQuestPage();
    AtomicShowQuestPage(unsigned int creatureSID, unsigned int npcIndex, int questId, const char16_t* fileName);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;

private:
    unsigned int mCreatureSID;
    unsigned int mNpcIndex;
    int mQuestId;
    std::array<char16_t, 261> mFileName;
};


class AtomicShowPage : public AtomicJob {
public:
    AtomicShowPage();
    AtomicShowPage(unsigned int creatureSID, unsigned int npcIndex, const char16_t* fileName, int eventId);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;

private:
    unsigned int mCreatureSID;
    unsigned int mNpcIndex;
    int m_EventID;
    std::array<char16_t, 261> mFileName;
};


class AtomicPledgeLevelUp : public AtomicJob {
public:
    AtomicPledgeLevelUp();
    AtomicPledgeLevelUp(unsigned int creatureSID, int pledgeId, int pledgeLevel);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;

private:
    unsigned int mCreatureSID;
    int mPledgeId;
    int mPledgeLevel;
};


class AtomicSetResidenceDeco : public AtomicJob {
public:
    AtomicSetResidenceDeco();
    AtomicSetResidenceDeco(unsigned int npcIndex, unsigned int userIndex, int agitId, int decoType, int decoLevel);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;

private:
    unsigned int mNpcIndex;
    unsigned int mUserIndex;
    int mAgitId;
    int mDecoType;
    int mDecoLevel;
};


class AtomicResetResidenceDeco : public AtomicJob {
public:
    AtomicResetResidenceDeco();
    AtomicResetResidenceDeco(unsigned int npcIndex, unsigned int userIndex, int agitId, int decoType);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;

private:
    unsigned int mNpcIndex;
    unsigned int mUserIndex;
    int mAgitId;
    int mDecoType;
};


// This current-only L2NPC class is absent from the adjacent older PDB. Its
// original shared-header field names survive in the matching NASC64_2015 PDB.
class Atomic_AJ_OPEN_DECO_NPC : public AtomicJob {
public:
    Atomic_AJ_OPEN_DECO_NPC();
    Atomic_AJ_OPEN_DECO_NPC(unsigned int npcIndex, unsigned int userId, int residenceId);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;

private:
    unsigned int m_nNpcIndex;
    unsigned int m_nUserIndex;
    int m_nAgitCID;
};


class AtomicSetFortressFacility : public AtomicJob {
public:
    AtomicSetFortressFacility();
    AtomicSetFortressFacility(unsigned int npcId, unsigned int userId, int fortressId, int facilityType, int facilityLevel);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;

private:
    unsigned int m_nNpcId;
    unsigned int m_nUserId;
    int m_nFortressId;
    int m_nFacilityType;
    int m_nFacilityLevel;
};


class AtomicResetFortressFacility : public AtomicJob {
public:
    AtomicResetFortressFacility();
    AtomicResetFortressFacility(unsigned int npcId, unsigned int userId, int fortressId);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;

private:
    unsigned int m_nNpcId;
    unsigned int m_nUserId;
    int m_nFortressId;
};


class AtomicStartObserver : public AtomicJob {
public:
    AtomicStartObserver();
    AtomicStartObserver(
        unsigned int npcIndex, unsigned int userIndex, int x, int y, int z, int yaw, int pitch, int residenceId
    );

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;

private:
    unsigned int mNpcIndex;
    unsigned int mUserIndex;
    int mX;
    int mY;
    int mZ;
    int mYaw;
    int mPitch;
    int mResidenceId;
};


class AtomicShowQuestFHTML : public AtomicJob {
public:
    AtomicShowQuestFHTML();
    AtomicShowQuestFHTML(
        unsigned int creatureSID, unsigned int creatureIndex, unsigned int npcSID, CFHTML* fhtml, int questId
    );
    ~AtomicShowQuestFHTML() override;

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;

private:
    unsigned int mCreatureSID;
    unsigned int mCreatureIndex;
    unsigned int mNpcSID;
    CFHTML* mpFHTML;
    int mQuestID;
    std::u16string mFHTMLDataString;
    std::u16string mFHTMLName;
};


class AtomicShowFHTML : public AtomicJob {
public:
    AtomicShowFHTML();
    AtomicShowFHTML(unsigned int creatureSID, unsigned int creatureIndex, unsigned int npcSID, CFHTML* fhtml);
    ~AtomicShowFHTML() override;

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;

private:
    unsigned int mCreatureSID;
    unsigned int mCreatureIndex;
    unsigned int mNpcSID;
    CFHTML* mpFHTML;
    std::u16string mFHTMLDataString;
    std::u16string mFHTMLName;
};


// AtomicDeleteAcquireSkill was added after the adjacent donor PDB. Its exact
// current field names survive in the matching NASC64_2015 PDB.
class AtomicDeleteAcquireSkill : public AtomicJob {
public:
    AtomicDeleteAcquireSkill();
    AtomicDeleteAcquireSkill(int npcSID, int userSID, int acquireType);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    bool CanDo() override;

private:
    int m_NpcSID;
    int m_UserSID;
    int m_SkillID;
};


class AtomicDeleteAcquireSkills : public AtomicJob {
public:
    AtomicDeleteAcquireSkills();
    AtomicDeleteAcquireSkills(int npcSID, int userSID, int acquireType);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;
    bool CanDo() override;

private:
    int m_nNpcSID;
    int m_nUserSID;
    int m_nAcquireType;
};


// Current L2Server's PCCafe coupon commit job. The constructor is inlined
// into User::RepliedPCCafeCouponResult in the shipping image. Do remains in
// its original Shared/AtomicJob.cpp owner.
#if defined(L2_BUILD_L2SERVER)
class CAtomicCouponUsed : public AtomicJob {
public:
    CAtomicCouponUsed(
        int worldId,
        int userId,
        int userDatabaseId,
        int couponPart1,
        int couponPart2,
        int couponPart3,
        int couponPart4,
        int couponItemClassId,
        std::uint32_t accountId
    );

    bool Do() override;

private:
    std::int32_t m_nWorldId; // +0x28
    std::int32_t m_nUserId; // +0x2c
    std::int32_t m_nUserDatabaseId; // +0x30
    std::int32_t m_nCouponPart1; // +0x34
    std::int32_t m_nCouponPart2; // +0x38
    std::int32_t m_nCouponPart3; // +0x3c
    std::int32_t m_nCouponPart4; // +0x40
    std::int32_t m_nCouponItemClassId; // +0x44
    std::uint32_t m_nAccountId; // +0x48
};

#endif

class AtomicUpdatePCCafePoint : public AtomicJob {
public:
    AtomicUpdatePCCafePoint();
    AtomicUpdatePCCafePoint(int userSID, int delta);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;
    bool CanDo() override;

private:
    int m_userSID;
    int m_delta;
};


class AtomicUpdatePVPPoint : public AtomicJob {
public:
    AtomicUpdatePVPPoint();
    AtomicUpdatePVPPoint(int userSID, int delta, int type);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;
    bool CanDo() override;

private:
    int m_userSID;
    int m_delta;
    int m_nType;
};


class AtomicManipulatePVPPointWithItem : public AtomicJob {
public:
    AtomicManipulatePVPPointWithItem();
    AtomicManipulatePVPPointWithItem(int userSID, int delta, int itemClassId, int enchanted, std::int64_t count);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;
    bool CanDo() override;

private:
    int m_userSID;
    int m_delta;
    int m_nItemClassID;
    int m_nEnchanted;
    std::int64_t m_nCount;
};


class AtomicUpdateUserPoint : public AtomicJob {
public:
    AtomicUpdateUserPoint();
    AtomicUpdateUserPoint(int talkerIndex, int type, int point);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;
    bool CanDo() override;

private:
    int m_TalkerIndex;
    int m_Type;
    int m_point;
};


class AtomicSetNobless : public AtomicJob {
public:
    AtomicSetNobless();
    AtomicSetNobless(unsigned int npcSid, unsigned int userSid, int noblessType);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;

private:
    unsigned int mNpcSid;
    unsigned int mUserSid;
    int mNoblessType;
};


class AtomicSetHero : public AtomicJob {
public:
    AtomicSetHero();
    AtomicSetHero(unsigned int npcSid, unsigned int userSid, int heroType);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;

private:
    unsigned int mNpcSid;
    unsigned int mUserSid;
    int mHeroType;
};


class AtomicCastBuff : public AtomicJob {
public:
    AtomicCastBuff();
    AtomicCastBuff(unsigned int npcSid, unsigned int userSid, std::int64_t skillNameId);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;

private:
    bool CheckCancelCondition(CCreature* creature);

    unsigned int npcSid;
    unsigned int userSid;
    std::int64_t skillNameId;
    int skillType;
    int actionMoveType;
};


class AtomicCastBuffWithMpConsumption : public AtomicJob {
public:
    AtomicCastBuffWithMpConsumption();
    AtomicCastBuffWithMpConsumption(unsigned int npcSid, unsigned int userSid, std::int64_t skillNameId);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;

private:
    bool CheckCancelCondition(CCreature* creature);

    unsigned int npcSid;
    unsigned int userSid;
    std::int64_t skillNameId;
    int skillType;
    int actionMoveType;
};


class AtomicCastBuffForEventMaker : public AtomicJob {
public:
    AtomicCastBuffForEventMaker();
    AtomicCastBuffForEventMaker(unsigned int npcSid, unsigned int userSid);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;

private:
    bool CheckCancelCondition(CCreature* creature);

    unsigned int npcSid;
    unsigned int userSid;
};


class AtomicRideWyvern : public AtomicJob {
public:
    AtomicRideWyvern();
    AtomicRideWyvern(unsigned int userSID, int wyvernClassID);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;

private:
    unsigned int m_UserSID;
    int m_WyvernClassID;
};


class AtomicEventRide : public AtomicJob {
public:
    AtomicEventRide();
    AtomicEventRide(unsigned int userSID, int petClassID, int duration, int cafePoint);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;
    bool CanDo() override;

private:
    unsigned int m_userSID;
    int m_petClassID;
    int m_duration;
    int m_cafePoint;
};


class AtomicSummonAirShip : public AtomicJob {
public:
    AtomicSummonAirShip();
    AtomicSummonAirShip(int userSID, int airportId, int platformId);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;

private:
    int m_UserSID;
    int m_AirportId;
    int m_PlatformId;
};


class AtomicBuyAirShip : public AtomicJob {
public:
    AtomicBuyAirShip();
    AtomicBuyAirShip(int userSID, int typeId, int npcSID);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;

private:
    int m_UserSID;
    int m_TypeId;
    int m_NpcSID;
};


class AtomicGetOnAirShip : public AtomicJob {
public:
    AtomicGetOnAirShip();
    AtomicGetOnAirShip(int userSID, int airShipSID);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;
    bool CanDo() override;

private:
    int m_UserSID;
    int m_AirShipSID;
};


class AtomicChangeTalkTarget : public AtomicJob {
public:
    AtomicChangeTalkTarget();
    AtomicChangeTalkTarget(int userSID, int npcSID);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;
    bool CanDo() override;

private:
    int m_userSID;
    int m_npcSID;
};


class AtomicRewardSkillEnchant : public AtomicJob {
public:
    AtomicRewardSkillEnchant();
    AtomicRewardSkillEnchant(int userSID, int itemId);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;
    bool CanDo() override;

private:
    int m_userSID;
    int m_nItemId;
};


class AtomicAddTimer : public AtomicJob {
public:
    AtomicAddTimer();
    AtomicAddTimer(int npcSID, int npcIndex, int timerId, int milliSec);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;

private:
    int mNpcSID;
    int mNpcIndex;
    int mTimerId;
    int mMilliSec;
};


class AtomicSetTicketBuyCount : public AtomicJob {
public:
    AtomicSetTicketBuyCount();
    AtomicSetTicketBuyCount(int userSID, int count);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;

private:
    int mUserSID;
    int mCount;
};


class AtomicAddWorldChatBonusPoint : public AtomicJob {
public:
    AtomicAddWorldChatBonusPoint();
    AtomicAddWorldChatBonusPoint(int userSID, int pointType, int point);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;
    bool CanDo() override;

private:
    int m_userSID;
    int m_nPointType;
    int m_nPoint;
};


class AtomicAcceptUserQuest : public AtomicJob {
public:
    AtomicAcceptUserQuest();
    AtomicAcceptUserQuest(unsigned int creatureSID, int questId);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;

private:
    unsigned int mCreatureSID;
    int mQuestId;
};


class AtomicShowCardCollectSystem : public AtomicJob {
public:
    AtomicShowCardCollectSystem();
    explicit AtomicShowCardCollectSystem(int userSID);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;
    bool CanDo() override;

private:
    int m_userSID;
};


class AtomicAddTimeAttackRecord : public AtomicJob {
public:
    AtomicAddTimeAttackRecord();
    AtomicAddTimeAttackRecord(int roomNo, int partType, int partySID, std::int64_t point, int eventTime, int elapsedTime);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;

private:
    int mRoomNo;
    int mPartType;
    int mPartySID;
    std::int64_t mPoint;
    int mEventTime;
    int mElapsedTime;
};


class AtomicAddTimeAttackFee : public AtomicJob {
public:
    AtomicAddTimeAttackFee();
    AtomicAddTimeAttackFee(int roomNO, std::int64_t fee);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;

private:
    int mRoomNO;
    std::int64_t mFee;
};


class AtomicGiveTimeAttackReward : public AtomicJob {
public:
    AtomicGiveTimeAttackReward();
    AtomicGiveTimeAttackReward(int userSID, int roomNO, int questID, int itemClassID, std::int64_t count);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;

private:
    int mUserSID;
    int mRoomNO;
    int mQuestID;
    int mItemClassID;
    std::int64_t mCount;
};


class AtomicAddSSQMember : public AtomicJob {
public:
    AtomicAddSSQMember();
    AtomicAddSSQMember(
        int npcSID, int userSID, int ssqPart, int sealSelectionNo, int position, std::int64_t initialPoint, int joinType
    );

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;

private:
    int mNpcSID;
    int mUserSID;
    int mSSQPart;
    int mSealSelectionNo;
    int mPosition;
    std::int64_t mInitialPoint;
    int mJoinType;
};


class AtomicDeleteDepositedSSQItem : public AtomicJob {
public:
    AtomicDeleteDepositedSSQItem();
    AtomicDeleteDepositedSSQItem(int userSID, int ssqPart, int itemType, std::int64_t count);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;

private:
    int mUserSID;
    int mSSQPart;
    int mItemType;
    std::int64_t mCount;
};


class AtomicDepositSSQItemEx : public AtomicJob {
public:
    AtomicDepositSSQItemEx();
    AtomicDepositSSQItemEx(
        int userSID,
        int ssqPart,
        std::int64_t firstCount,
        std::int64_t secondCount,
        std::int64_t thirdCount,
        std::int64_t point
    );

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;

private:
    int mUserSID;
    int mSSQPart;
    std::int64_t mCountList[3];
    std::int64_t mPoint;
};


class AtomicParamChange : public AtomicJob {
public:
    AtomicParamChange();
    AtomicParamChange(int relatedQuestID, unsigned int creatureSID, ParameterType paramType, std::int64_t delta);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;

private:
    int mRelatedQuestID;
    unsigned int mCreatureSID;
    ParameterType mParamType;
    std::int64_t mDelta;
};


class AtomicTeleportParty : public AtomicJob {
public:
    AtomicTeleportParty();
    AtomicTeleportParty(int partySID, int x, int y, int z, int distanceFromLeader, int privateID, unsigned int inZoneID);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;

private:
    int mPartySID;
    int mX;
    int mY;
    int mZ;
    int mDistanceFromLeader;
    int mPrivateID;
    unsigned int mInZoneID;
};


class AtomicTeleportTo : public AtomicJob {
public:
    AtomicTeleportTo();
    AtomicTeleportTo(int creatureSID, int x, int y, int z, unsigned int inZoneID);
    AtomicTeleportTo(int creatureSID, int x, int y, int z, int direction, unsigned int inZoneID);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;

private:
    int m_CreatureSID;
    int m_X;
    int m_Y;
    int m_Z;
    int m_Direction;
    unsigned int m_InZoneID;
};


class AtomicAddFactionPoint : public AtomicJob {
public:
    AtomicAddFactionPoint();
    AtomicAddFactionPoint(int userSID, int factionType, std::int64_t factionPoint, int reason);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;
    bool CanDo() override;

private:
    int m_nUserSID;
    int m_nFactionType;
    std::int64_t m_nFactionPoint;
    int m_nReason;
};


class AtomicSetFactionPoint : public AtomicJob {
public:
    AtomicSetFactionPoint();
    AtomicSetFactionPoint(int userSID, int factionType, std::int64_t factionPoint, int reason);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;
    bool CanDo() override;

private:
    int m_nUserSID;
    int m_nFactionType;
    std::int64_t m_nFactionPoint;
    int m_nReason;
};


class AtomicContentUIEvent : public AtomicJob {
public:
    struct ContentMonsterArena {
        enum PointOpCode : std::int32_t {
            POINT_CLEAR = 0,
            POINT_ADD = 1,
        };

        int nInzoneTypeId;
        int nStageProgress;
        int nCurrentPoint;
        PointOpCode pointOpCode;
        bool bSendUI;
    };

    AtomicContentUIEvent() = default;

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;
    bool CanDo() override;

    void SetMonsterArenaInfo(
        int userSID,
        int inzoneTypeId,
        int stageProgress,
        int currentPoint,
        ContentMonsterArena::PointOpCode pointOpCode,
        bool sendUI
    );

private:
    NPCContentUIEvent::NPCContentUIType m_eventType;
    int m_nUserSID;
    ContentMonsterArena m_monArena;
};


class AtomicDropItem : public AtomicJob {
public:
    AtomicDropItem();
    AtomicDropItem(std::uint32_t npcSID, int itemClassId, std::int64_t count, FVector& position);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;

private:
    std::uint32_t mNpcSID;
    int mItemClassId;
    std::int64_t mItemCount;
    FVector mvPos;
};


class AtomicDropItem2 : public AtomicJob {
public:
    AtomicDropItem2();
    AtomicDropItem2(std::uint32_t npcSID, int itemClassId, std::int64_t count, FVector& position, int contributorId);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;

private:
    std::uint32_t mNpcSID;
    int mItemClassId;
    std::int64_t mItemCount;
    FVector mvPos;
    std::uint32_t mContributorID;
};


class AtomicNpcSetSkillAll : public AtomicJob {
public:
    AtomicNpcSetSkillAll();
    AtomicNpcSetSkillAll(int talkerIndex, int type, int classId);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;
    bool CanDo() override;

private:
    int m_TalkerIndex;
    int m_Type;
    int m_Class;
};


class AtomicSetSkill : public AtomicJob {
public:
    AtomicSetSkill();
    AtomicSetSkill(int creatureSID, std::int64_t skillNameID);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;
    bool CanDo() override;

private:
    int m_CreatureSID;
    std::int64_t m_SkillNameID;
};


class AtomicChangeClass : public AtomicJob {
public:
    AtomicChangeClass();
    AtomicChangeClass(int userSID, int classId, int npcId, int targetMaxLevel);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;
    bool CanDo() override;

private:
    int m_UserSID;
    int m_Class;
    int m_NpcID;
    int m_nTargetMaxLevel;
};


class AtomicChangeClassAndSetSkill : public AtomicJob {
public:
    AtomicChangeClassAndSetSkill();
    AtomicChangeClassAndSetSkill(int userSID, int classId, int npcId);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;
    bool CanDo() override;

private:
    int m_userSID;
    int m_nClass;
    int m_nNpcId;
};


class AtomicChangeDailyQuestFlag : public AtomicJob {
public:
    AtomicChangeDailyQuestFlag();
    AtomicChangeDailyQuestFlag(int userSID, int questId);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Do() override;
    void Dump() override;
    bool CanDo() override;

private:
    int m_userSID;
    int m_questId;
};


// Recovered Shared/AtomicJob.h value kept by m_vAtomicLogList. Windows
// `long` is represented as 32 bits so the fields keep their
// original value ranges on LP64 hosts.
struct AtomicLog : MemoryObject {
    AtomicLog() = default;
    AtomicLog(std::int32_t time, AtomicType::Enum type, int line);
    ~AtomicLog() override = default;

    // No AtomicLog allocation/release call site survives in the current
    // image, so its OBJECT_TYPE source line cannot be read back from machine
    // code. The PDB still fixes the allocation extent at 0x28. Only the
    // unavailable diagnostic line remains zero below.
    L2_DECLARE_OBJECT_TYPE(AtomicLog)

    std::int32_t m_tmTime;
    AtomicType::Enum m_eType;
    int m_nLine;
};


// Current L2Server.exe root 0x00d7449c constructs this exact specialization
// at 0x020f50e0. BuilderCmd::AtomicLog and the physical AtomicJob cache
// Push/Pop paths consume the same storage. It remains separate
// from every creature smart-ID manager and lookup path.
using AtomicLogObjectMap = CObjectMap<AtomicLog, 256, 5, CIOCriticalSection, std::int64_t>;


extern AtomicLogObjectMap m_vAtomicLogList;

template <typename TObject>
struct ObjectCacheTraits;

template <>
struct ObjectCacheTraits<AtomicJob> {
    static void OnPush(int slot, AtomicJob* job);
    static void OnPop(int slot);
};
