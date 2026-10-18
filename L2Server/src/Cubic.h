#pragma once

#include "Object.h"
#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

class CSkillInfo;
class CCreature;
class User;
class CCubic;
class CAgathion;

enum SpriteTargetType : std::int32_t {
    STT_NULL = 0,
    STT_TARGET = 1,
    STT_BY_SKILL = 2,
    STT_HEAL = 3,
    STT_MASTER = 4,
    STT_MAX = 5,
};

enum SpriteSkillOp : std::int32_t {
    SSO_NULL = 0,
    SSO_OVER = 1,
    SSO_BELOW = 2,
    SSO_DEBUFF = 3,
    SSO_MAX = 4,
};

enum CubicTimeSkillType : std::int32_t {
    CTST_PERIOD = 0,
    CTST_DAILY = 1,
    CTST_MAX = 2,
};

enum SpriteType : std::int32_t {
    SPRITE_CUBIC = 0,
    SPRITE_AGATHION = 1,
};


// Target-side activation state used twice by CubicActivationInfo. The
// primitive fields have no default member initializers: the
// original compiler-generated constructors only construct m_TargetSID, while
// Init supplies the operational defaults observed in the current image.
struct ActivateInfo2 {
    void Init() noexcept;

    bool m_NeedCheckTarget; // +0x00
    CSmartID m_TargetSID; // +0x04
    CSkillInfo* m_pSkillInfo; // +0x08
    int m_SkillIndex; // +0x10
    int m_CountDown; // +0x14
};


// Complete current CubicActivationInfo value. Win64 `long` is represented
// as int32_t so the 0x60 target envelope survives on LP64 hosts.
struct CubicActivationInfo {
    CubicActivationInfo(std::int32_t summonedTime, bool bySelfCasting);
    CubicActivationInfo(const CubicActivationInfo& other);
    CubicActivationInfo& operator=(const CubicActivationInfo& other) = default;

    void Init(std::int32_t summonedTime, bool bySelfCasting);
    bool IsOnDailyTimeSkillResetTime(std::int32_t currentTime);
    void InitLastResetTime(std::int32_t currentTime, int resetHour, int resetMinute, bool usePreviousDay);

    std::int32_t summonedTime; // +0x00
    std::int32_t lastHitTime; // +0x04
    int hitCount; // +0x08
    bool bySelfCasting; // +0x0c
    bool m_bTimeSkill; // +0x0d
    ActivateInfo2 m_Normal; // +0x10
    ActivateInfo2 m_TimeSkill; // +0x28
    std::int32_t m_TimeSkillLaunched[5]; // +0x40
    bool m_bResetDailySkills[5]; // +0x54
    std::int32_t m_tmLastResetTime; // +0x5c
};


struct CubicOpCond {
    SpriteSkillOp op; // +0x00
    int hpRatio; // +0x04
    int hpAbs; // +0x08
};


struct CubicSkillInfo {
    int m_ActivateProb; // +0x00
    CSkillInfo* m_pSkillInfo; // +0x08
    int m_HitProb; // +0x10
    bool m_bAttackStaticObject; // +0x14
    SpriteTargetType m_TargetType; // +0x18
    CubicOpCond m_OpCond; // +0x1c
    int m_CycleSec; // +0x28
    CubicTimeSkillType m_eTimeSkillType; // +0x2c
};


// Current L2Server RTTI and both derived vtables establish this four-slot
// source hierarchy. The cast hooks are the slots consumed by the current
// sprite lookup paths. There is no virtual destructor in this family.
class CSprite {
public:
    CSprite(int classId, int level, int slot);

    virtual CSprite* CastSprite();
    virtual CAgathion* CastAgathion();
    virtual CCubic* CastCubic();

    int m_classID; // +0x08
    int m_level; // +0x0c
    int m_slot; // +0x10
    std::array<std::byte, 4> m_alignment14To18; // +0x14..+0x17
};

class CCubic : public CSprite {
public:
    CCubic(int classId, int level, int slot, int duration, int delay, int maxCount, double power);

    CCubic* CastCubic() override;
    virtual void SetNPCClassID(int npcClassId);

    bool PushTimeSkill(const CubicSkillInfo& skillInfo);
    int TimeSkillDelayShow(User& user, CubicActivationInfo& activationInfo);
    void TimeSkillDelayDel(User& user, CubicActivationInfo& activationInfo, const int elapsedTime);
    void TimeSkillDelayReset(User& user, CubicActivationInfo& activationInfo);
    void SetElapsedTimeToDailyTimeSkill(User* user, int elapsedTime, CubicActivationInfo* activationInfo);
    void RemoveDailyTimeSkillDelay(User* user);
    bool SetEnergy(int energy);
    bool SetMaxEnergy(int maxEnergy);
    void AddItemId(int itemClassId);

    int m_duration; // +0x18
    int m_delay; // +0x1c
    int m_maxCount; // +0x20
    double m_power; // +0x28
    int m_maxActivateProb; // +0x30
    bool m_UseUp; // +0x34
    std::array<std::byte, 3> m_alignment35To38; // +0x35..+0x37
    SpriteTargetType m_targetType; // +0x38
    CubicOpCond m_opCond; // +0x3c
    double m_targetTypeOption[4]; // +0x48
    CubicSkillInfo* m_skillList[3]; // +0x68
    int m_CubicTimeSkillCount; // +0x80
    CubicSkillInfo m_CubicTimeSkills[5]; // +0x88
    std::vector<int> m_nItemClassIds; // +0x178
};

class CAgathion : public CCubic {
public:
    CAgathion(int classId, int level, int slot, int duration, int delay, int maxCount, double power);

    CAgathion* CastAgathion() override;
    void SetNPCClassID(int npcClassId) override;

    int m_npcClassID; // +0x198
};

class SpriteDB {
public:
    using SummonSkillType = std::pair<int, int>;
    using SpriteKeyType = std::pair<SpriteType, SummonSkillType>;

    static SpriteDB& Inst();

    CSprite* GetInfo(SpriteKeyType key);
    CSprite* GetInfo(SpriteType type, int classId, int level) {
        return GetInfo(SpriteKeyType{type, SummonSkillType{classId, level}});
    }
    void Add(SpriteType type, CSprite* sprite, int classId, int level);
    bool Load();
    bool LoadData(char16_t* data, int dataLength);

private:
    std::map<SpriteKeyType, CSprite*> m_spriteMap;
};


class CCubicHandler {
public:
    static bool CheckOpCond(CObject* object, const double& currentHp, const double& maximumHp, CubicOpCond& condition);
    static bool CanUseSkill(CSkillInfo* skillInfo, CCreature* creature, CWorldObject* target);
    static bool TryToUseSkill(
        CCreature* creature,
        CWorldObject* target,
        CCubic* cubic,
        CubicActivationInfo& activationInfo,
        ActivateInfo2& activateInfo,
        CubicSkillInfo* cubicSkillInfo
    );
    static CubicSkillInfo* ChooseSkill(
        CCreature& creature, CCubic& cubic, CubicActivationInfo& activationInfo, ActivateInfo2& activateInfo
    );
    static bool ActionToCurrentTarget(
        CCreature* creature, CCubic* cubic, CubicActivationInfo& activationInfo, ActivateInfo2& activateInfo
    );
    static bool ActionToMaster(
        CCreature* creature, CCubic* cubic, CubicActivationInfo& activationInfo, ActivateInfo2& activateInfo
    );
    static bool ActionHeal(
        CCreature* creature,
        CCubic* cubic,
        CubicActivationInfo& activationInfo,
        ActivateInfo2& activateInfo,
        CubicSkillInfo* cubicSkillInfo
    );
    static bool ActionToTargetBySkill(
        CCreature* creature, CCubic* cubic, CubicActivationInfo& activationInfo, ActivateInfo2& activateInfo
    );
    static bool ActivateCubicSkill(
        CCreature* creature, CubicActivationInfo& activationInfo, ActivateInfo2& activateInfo, CCubic* cubic
    );
    static bool ReadyToUseSkill(
        CCreature* creature, CCubic* cubic, CubicActivationInfo& activationInfo, ActivateInfo2& activateInfo
    );
    static bool OnTick(CCreature& creature, CCubic& cubic, CubicActivationInfo& activationInfo, ActivateInfo2& activateInfo);
    static bool ResetLaunchedTime(CCubic& cubic, CubicActivationInfo& activationInfo, std::int32_t launchedTime);
    static bool IsExpired(CCubic* cubic, CubicActivationInfo& activationInfo);
    static bool CheckCubicSkillDelay(
        CCreature& creature, CCubic& cubic, CSkillInfo& skillInfo, int skillIndex, CubicActivationInfo& activationInfo
    );
};
