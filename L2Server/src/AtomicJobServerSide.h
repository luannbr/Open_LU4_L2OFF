#pragma once

#include "Shared/src/AtomicJob.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"

#include <cstddef>
#include <cstdint>

class CQuestBase;
enum EtcSkillAcquireType : std::int32_t;

enum AJ_JOBID_TYPE : std::int32_t {};

using AtomicJobFactory = GenericFactory<AtomicJob, AJ_JOBID_TYPE, AtomicJob* (*)()>;


// L2Server's process-side atomic transaction envelope. The current image
// uses the 0x20-byte xvector carrier here, instead of the 0x18-byte donor
// std::vector layout.
class AtomicJobList final : public AtomicJob {
public:
    AtomicJobList(int reserveCount, int userId);
    ~AtomicJobList() override = default;

    static AtomicJobList* CreateAtomicJobList(const unsigned char* packet, int userId, bool addItemJobCheck);

    bool Do() override;
    void Destroy() override;
    void Dump() override;
    bool CanDo() override;

    void AddAtomicJob(AtomicJob* job);

private:
    [[maybe_unused]] std::int32_t m_UserId; // +0x28
    [[maybe_unused]] std::byte m_alignment2cTo30[4]{};
    std::vector<AtomicJob*> mJobVector; // +0x30
};

class CAtomicItemJobCheck final : public AtomicJob {
public:
    explicit CAtomicItemJobCheck(int userSID);
    ~CAtomicItemJobCheck() override = default;

    bool Do() override;
    bool CanDo() override;

private:
    std::int32_t m_UserSID; // +0x28
};

class CAtomicQuestAccept final : public AtomicJob {
public:
    CAtomicQuestAccept(int userSID, CQuestBase* quest);
    ~CAtomicQuestAccept() override = default;

    bool Do() override;

private:
    std::int32_t m_UserSID; // +0x28
    [[maybe_unused]] std::byte m_alignment2cTo30[4]{};
    CQuestBase* m_pQuest; // +0x30
};

class CAtomicQuestFinish final : public AtomicJob {
public:
    CAtomicQuestFinish(int userSID, CQuestBase* quest);
    ~CAtomicQuestFinish() override = default;

    bool Do() override;

private:
    std::int32_t m_UserSID; // +0x28
    [[maybe_unused]] std::byte m_alignment2cTo30[4]{};
    CQuestBase* m_pQuest; // +0x30
};

class CAtomicSaveQuestInfo final : public AtomicJob {
public:
    explicit CAtomicSaveQuestInfo(int userSID);
    ~CAtomicSaveQuestInfo() override = default;

    bool Do() override;

private:
    std::int32_t m_UserSID; // +0x28
};

class CAtomicSkillEnchantNormal final : public AtomicJob {
public:
    CAtomicSkillEnchantNormal(int userSid, int skillId, int skillLevel, int skillSubLevel) :
        m_nUserSid(userSid), m_nSkillID(skillId), m_nSkillLevel(skillLevel), m_nSkillSubLevel(skillSubLevel) {
        tmTime = runtime::crt::Time32Now();
        eType = AtomicType::SkillEnchantNormal;
        nLine = 0x31;
    }

    bool Do() override;

private:
    std::int32_t m_nUserSid; // +0x28
    std::int32_t m_nSkillID; // +0x2c
    std::int32_t m_nSkillLevel; // +0x30
    std::int32_t m_nSkillSubLevel; // +0x34
};

class CAtomicSkillEnchantSafe final : public AtomicJob {
public:
    CAtomicSkillEnchantSafe(int userSid, int skillId, int skillLevel, int skillSubLevel) :
        m_nUserSid(userSid), m_nSkillID(skillId), m_nSkillLevel(skillLevel), m_nSkillSubLevel(skillSubLevel) {
        tmTime = runtime::crt::Time32Now();
        eType = AtomicType::SkillEnchantSafe;
        nLine = 0x4b;
    }

    bool Do() override;

private:
    std::int32_t m_nUserSid; // +0x28
    std::int32_t m_nSkillID; // +0x2c
    std::int32_t m_nSkillLevel; // +0x30
    std::int32_t m_nSkillSubLevel; // +0x34
};

class AtomicSkillEnchantForce final : public AtomicJob {
public:
    AtomicSkillEnchantForce(int userSid, int skillId, int skillLevel, int skillSubLevel) :
        m_UserSid(userSid), m_SkillID(skillId), m_SkillLev(skillLevel), m_SkillSubLevel(skillSubLevel) {
        tmTime = runtime::crt::Time32Now();
        eType = AtomicType::SkillEnchantForce;
        nLine = 0x65;
    }

    bool Do() override;

private:
    std::int32_t m_UserSid; // +0x28
    std::int32_t m_SkillID; // +0x2c
    std::int32_t m_SkillLev; // +0x30
    std::int32_t m_SkillSubLevel; // +0x34
};

class CAtomicSkillEnchantRouteChange final : public AtomicJob {
public:
    CAtomicSkillEnchantRouteChange(int userSid, int skillId, int skillLevel, int skillSubLevel) :
        m_nUserSid(userSid), m_nSkillID(skillId), m_nSkillLev(skillLevel), m_nSkillSubLev(skillSubLevel) {
        tmTime = runtime::crt::Time32Now();
        eType = AtomicType::SkillEnchantRouteChange;
        nLine = 0x97;
    }

    bool Do() override;

private:
    std::int32_t m_nUserSid; // +0x28
    std::int32_t m_nSkillID; // +0x2c
    std::int32_t m_nSkillLev; // +0x30
    std::int32_t m_nSkillSubLev; // +0x34
};

class AtomicItemDelete final : public AtomicJob {
public:
    AtomicItemDelete();
    AtomicItemDelete(int userSID, int skillId, int level, int subLevel, EtcSkillAcquireType acquireType);

    bool Do() override;

private:
    std::int32_t m_UserSID; // +0x28
    std::int32_t m_SkillID; // +0x2c
    std::int32_t m_Level; // +0x30
    std::int32_t m_SubLevel; // +0x34
    EtcSkillAcquireType m_AcquireType; // +0x38
};
