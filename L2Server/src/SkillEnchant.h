#pragma once

#include "L2Core/src/inc/XStd.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Obj.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Skill.h"
#include "SkillReuseDelay.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

enum SkillEnchantCommandType : std::int32_t {
    SECT_NORMAL = 0,
    SECT_SAFE = 1,
    SECT_UNTRAIN = 2,
    SECT_ROUTE_CHANGE = 3,
    SECT_FORCE = 4,
    SECT_MAX = 5,
};


class AtomicJob;
class CNPC;
class CUserSocket;
class User;

struct SkillEnchantInfo {
    SkillEnchantInfo();

    std::int32_t nSkillID; // +0x000
    std::int32_t nMinSkillLevel; // +0x004
    std::int32_t nMaxSkillLevel; // +0x008
    std::int32_t nSkillSubLevel; // +0x00c
    std::int32_t nNeedSkillSubLevel; // +0x010
    std::int32_t nRouteId; // +0x014
    std::int32_t nEnchantId; // +0x018
    std::int64_t nSp[SECT_MAX]; // +0x020
    double dProb[SECT_MAX]; // +0x048
    std::map<std::int32_t, std::int32_t> RequiredItem[SECT_MAX]; // +0x070
};


class SkillEnchantOperator : public MemoryObject {
public:
    SkillEnchantOperator();
    ~SkillEnchantOperator() override = default;

    L2_DECLARE_OBJECT_TYPE(SkillEnchantOperator)

    virtual void SendEnchantSkillInfo(User* user, CUserSocket* socket, int skillId, int skillLevelAndSubLevel, bool detail);
    virtual bool Operate(User* user, int skillId, int skillLevelAndSubLevel) = 0;
    virtual bool OperateAtomicJob(User* user, int skillId, int skillLevel, int skillSubLevel) = 0;
    virtual void SendAcquiredSkillList(User* user);
    virtual void SendAcquirableSkillList(User* user, std::int64_t skillNameId);
    virtual void SendAcquirableSkillDetail(User* user, std::int64_t skillNameId);
    virtual bool CheckRequestEnchantSkill(
        User* user, int skillId, int skillLevel, int skillSubLevel, SkillEnchantInfo*& info
    );

    static SkillEnchantCommandType GetSkillEnchantTypeByCmd(const char16_t* command);
    static void BuilderEnchantSkillReset(User* user, CUserSocket* socket);

    bool CheckNpcShowEnchantSkillList(int npcIndex, int userSid, int userIndex, User*& user, CNPC*& npc);

protected:
    bool CheckRequestSkill(User* user, int skillId, int skillLevel, int skillSubLevel);
    bool CheckEnchantSkill(User* user, std::int64_t skillNameId, SkillEnchantInfo*& info);
    bool CheckLearnable(CUserSocket* socket, const SkillEnchantInfo* info, std::int64_t sp, int level);
    bool DoOperation(User* user, int skillId, int skillLevel, int skillSubLevel, AtomicJob* job);
    std::int64_t GetRequiredSP(const SkillEnchantInfo* info) const;
    double GetEnchantProb(const SkillEnchantInfo* info) const;
    std::int64_t GetRequiredAdena(SkillEnchantInfo* info) const;
    int CalcSubLevelDown(int skillSubLevel) const;
    bool IsEnchantSuccess(const SkillEnchantInfo* info) const;

    SkillEnchantCommandType m_eType; // +0x18
};

class SkillEnchantNormal final : public SkillEnchantOperator {
public:
    SkillEnchantNormal();
    ~SkillEnchantNormal() override = default;

    L2_DECLARE_OBJECT_TYPE(SkillEnchantNormal)

    bool Operate(User*, int, int) override;
    bool OperateAtomicJob(User*, int, int, int) override;
    void SendAcquiredSkillList(User*) override;
};

class SkillEnchantSafe : public SkillEnchantOperator {
public:
    SkillEnchantSafe();
    ~SkillEnchantSafe() override = default;

    L2_DECLARE_OBJECT_TYPE(SkillEnchantSafe)

    bool Operate(User*, int, int) override;
    bool OperateAtomicJob(User*, int, int, int) override;
    void SendAcquiredSkillList(User*) override;
};

class SkillEnchantUntrain final : public SkillEnchantOperator {
public:
    SkillEnchantUntrain();
    ~SkillEnchantUntrain() override = default;

    L2_DECLARE_OBJECT_TYPE(SkillEnchantUntrain)

    bool Operate(User*, int, int) override;
    bool OperateAtomicJob(User*, int, int, int) override;
    void SendAcquiredSkillList(User*) override;
    bool CheckRequestEnchantSkill(User*, int, int, int, SkillEnchantInfo*&) override;
};

class SkillEnchantRouteChange final : public SkillEnchantOperator {
public:
    SkillEnchantRouteChange();
    ~SkillEnchantRouteChange() override = default;

    L2_DECLARE_OBJECT_TYPE(SkillEnchantRouteChange)

    bool Operate(User*, int, int) override;
    bool OperateAtomicJob(User*, int, int, int) override;
    void SendAcquiredSkillList(User*) override;
    bool CheckRequestEnchantSkill(User*, int, int, int, SkillEnchantInfo*&) override;
};

class SkillEnchantForce final : public SkillEnchantSafe {
public:
    SkillEnchantForce();
    ~SkillEnchantForce() override = default;

    L2_DECLARE_OBJECT_TYPE(SkillEnchantForce)

    bool Operate(User*, int, int) override;
    bool OperateAtomicJob(User*, int, int, int) override;
};

using SkillEnchantFactory = GenericFactory<SkillEnchantOperator, SkillEnchantCommandType, SkillEnchantOperator* (*)()>;


class CSkillEnchantDB {
public:
    using EnchantKey = std::CTriKey<std::int32_t, std::int32_t, std::int32_t>;
    using SkillKey = std::CDualKey<std::int32_t, std::int32_t>;
    using EnchantMap = std::map<EnchantKey, SkillEnchantInfo*>;
    using SkillMap = std::map<SkillKey, SkillEnchantInfo*>;
    using SkillMultiMap = std::multimap<SkillKey, SkillEnchantInfo*>;
    using AcquiredSkillMap = std::map<std::int32_t, AcquiredSkillInfo>;
    using SkillKeyVector = std::vector<CSkillKey>;
    using SkillEnchantInfoVector = std::vector<SkillEnchantInfo*>;

    CSkillEnchantDB();
    ~CSkillEnchantDB();

    void Load();
    void Add(SkillEnchantInfo* info);
    void PostProcess();
    void SetSkillNeedLevel();
    void MakeMap();
    int GetRewardSP(int skillId, int skillLevel, int classId);
    int GetRewardItemCount(int skillId, int skillLevel, int userLevel);
    SkillEnchantInfo* GetSkillEnchantInfo(int skillId, int normalLevel, int subLevel);
    SkillEnchantInfo* GetNextLevelEnchantInfo(int skillId, int normalLevel, int enchantId);
    bool IsEnchantableSkill(int skillId, int normalLevel, int subLevel);
    void GetEnchantableSkills(const AcquiredSkillMap& acquiredSkills, SkillKeyVector& result);
    void GetOtherRouteSkills(SkillEnchantInfoVector& result, int skillId, SkillEnchantInfo* current);

    EnchantMap m_SkillEnchantMap; // +0x00
    SkillMap m_mapSkillID2EnchantSkill; // +0x20
    SkillMultiMap m_mmapSkillID2OtherEnchantSkill; // +0x40
    SkillMultiMap m_mmapNeedSkill2EnchantInfo; // +0x60
};


extern CSkillEnchantDB g_SkillEnchantDB;
