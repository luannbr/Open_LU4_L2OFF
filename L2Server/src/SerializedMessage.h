#pragma once

#include "Skill.h"

#include "Shared/src/IOC.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

class CCreature;
class CObject;
class CWorldObject;

class CSerializedMessage : public ::CIOObject {
public:
    ~CSerializedMessage() override = default;

    virtual void OnHandle(CCreature* creature) = 0;
    virtual void Clear() = 0;
};

// Original SerializedMessage.h helper used by current lambda-backed jobs.
// The callable is stored by value immediately after the 0x18-byte CIOObject
// envelope. An empty callable therefore keeps the target 0x20 extent.
template <class Function>
class CSerializedLambdaMessage final : public CSerializedMessage {
public:
    explicit CSerializedLambdaMessage(Function function) : m_Function(std::move(function)) {}

    void OnHandle(CCreature* creature) override { m_Function(creature); }

    void Clear() override {}

private:
    Function m_Function;
};

template <class Function>
CSerializedMessage* MakeLambdaMessage(Function&& function) {
    using StoredFunction = std::decay_t<Function>;
    return new CSerializedLambdaMessage<StoredFunction>(std::forward<Function>(function));
}

class CSerializedLostTargetMessage : public CSerializedMessage {
public:
    CSerializedLostTargetMessage(CCreature* creature, CObject* target);

    void OnHandle(CCreature* creature) override;
    void Clear() override;

private:
    CCreature* m_pCreature;
    CObject* m_pTarget;
};

class CSerializedDamageMessage : public CSerializedMessage {
public:
    CSerializedDamageMessage(CCreature* attacker, CCreature* victim, int damage);

    void OnHandle(CCreature* creature) override;
    void Clear() override;

private:
    CCreature* m_pAttacker;
    CCreature* m_pVictim;
    int m_nDamage;
};

class CSerializedLookNeighborMessage : public CSerializedMessage {
public:
    CSerializedLookNeighborMessage(CCreature* actor, bool reset, const FVector& previousLookPosition);

    void OnHandle(CCreature* creature) override;
    void Clear() override;

protected:
    CCreature* m_pActor;
    bool m_bReset;
    FVector m_PrevLookPos;
};

class CSerializedChangeTargetMessage : public CSerializedMessage {
public:
    CSerializedChangeTargetMessage(CCreature* actor);

    void OnHandle(CCreature* creature) override;
    void Clear() override;

private:
    CCreature* m_pActor;
};

class CSerializedCreatureResetVisiblity : public CSerializedMessage {
public:
    CSerializedCreatureResetVisiblity(CCreature* creature);

    void OnHandle(CCreature* creature) override;
    void Clear() override;

private:
    CCreature* m_pCreature;
};

class CSerializedPumpEndMessage : public CSerializedMessage {
public:
    CSerializedPumpEndMessage(
        CCreature* actor, CCreature* target, const CSkillInfo& skillInfo, SkillPumpEndType pumpEndType
    );

    void OnHandle(CCreature* creature) override;
    void Clear() override;

private:
    CCreature* m_pActor;
    CCreature* m_pTarget;
    const CSkillInfo& m_SkillInfo;
    const SkillPumpEndType m_PumpEndType;
};

class CSerializedTriggerSkillMessage : public CSerializedMessage {
public:
    CSerializedTriggerSkillMessage(
        CCreature* actor, CWorldObject* target, const CSkillInfo& skillInfo, SkillTargetTypeEnum targetType
    );

    void OnHandle(CCreature* creature) override;
    void Clear() override;

private:
    CCreature* m_pActor;
    CWorldObject* m_pTarget;
    const CSkillInfo& m_SkillInfo;
    SkillTargetTypeEnum m_TargetType;
};

class CSerializedAuraSkillMessage : public CSerializedMessage {
public:
    CSerializedAuraSkillMessage(CCreature* actor, const CSkillInfo& skillInfo, SkillTargetTypeEnum targetType);

    void OnHandle(CCreature* creature) override;
    void Clear() override;

private:
    CCreature* m_pActor;
    const CSkillInfo& m_SkillInfo;
    SkillTargetTypeEnum m_TargetType;
};

class CSerializedActivateSkillEffectsMessage : public CSerializedMessage {
public:
    CSerializedActivateSkillEffectsMessage(
        CCreature* actor, CWorldObject* target, const CSkillInfo& skillInfo, CSkillInfo::EffectActivateTiming timing
    );

    void OnHandle(CCreature* creature) override;
    void Clear() override;

private:
    CCreature* m_pActor;
    CWorldObject* m_pTarget;
    const CSkillInfo& m_SkillInfo;
    const CSkillInfo::EffectActivateTiming m_Timing;
};

class CSerializedAcquirePassiveBonusSkill : public CSerializedMessage {
public:
    CSerializedAcquirePassiveBonusSkill(
        CCreature* creature, const CSkillInfo& skillInfo, CSkillKey requireSkillKey, CSkillKey acquireSkillKey
    );

    void OnHandle(CCreature* creature) override;
    void Clear() override;

private:
    CCreature* m_pCreature;
    [[maybe_unused]] const CSkillInfo& m_SkillInfo;
    CSkillKey m_RequireSkillKey;
    CSkillKey m_AcquireSkillKey;
};

class CSerializedDeletePassiveBonusSkill : public CSerializedMessage {
public:
    CSerializedDeletePassiveBonusSkill(CCreature* creature, const CSkillInfo& skillInfo, CSkillKey acquireSkillKey);

    void OnHandle(CCreature* creature) override;
    void Clear() override;

private:
    CCreature* m_pCreature;
    [[maybe_unused]] const CSkillInfo& m_SkillInfo;
    CSkillKey m_AcquireSkillKey;
};

class CSerializedOnInfectedSkillByMessage : public CSerializedMessage {
public:
    CSerializedOnInfectedSkillByMessage(
        CCreature* actor,
        CCreature* target,
        const CSkillInfo& skillInfo,
        SkillPumpStartType pumpStartType,
        bool needSendVisualEffect
    );

    void OnHandle(CCreature* creature) override;
    void Clear() override;

private:
    CCreature* m_pActor;
    CCreature* m_pTarget;
    const CSkillInfo& m_SkillInfo;
    const SkillPumpStartType m_PumpStartType;
    bool m_bNeedSendVisualEffect;
};

class CSerializedDominionChannelSetMessage : public CSerializedMessage {
public:
    CSerializedDominionChannelSetMessage(CCreature* actor, bool set);

    void OnHandle(CCreature* creature) override;
    void Clear() override;

private:
    CCreature* m_pActor;
    bool m_bSet;
};

class CSerializedRenameByDominionSiegeMessage : public CSerializedMessage {
public:
    CSerializedRenameByDominionSiegeMessage(CCreature* actor, bool renamed);

    void OnHandle(CCreature* creature) override;
    void Clear() override;

protected:
    CCreature* m_pActor;
    bool m_bRenamed;
};

class CSerializedDominionEventMessage : public CSerializedMessage {
public:
    CSerializedDominionEventMessage(CCreature* actor, int eventType, int npcId, int dominionId, bool start);

    void OnHandle(CCreature* creature) override;
    void Clear() override;

protected:
    CCreature* m_pActor;
    int m_nEventType;
    int m_nNpcID;
    int m_nDominionID;
    bool m_bStart;
};

class CSerializedDominionScriptEventMessage : public CSerializedMessage {
public:
    CSerializedDominionScriptEventMessage(CCreature* actor, int arg1, int arg2, int arg3);

    void OnHandle(CCreature* creature) override;
    void Clear() override;

private:
    CCreature* m_pActor;
    int m_nArg1;
    int m_nArg2;
    int m_nArg3;
};

class CSerializedSendVoiceEventMessage : public CSerializedMessage {
public:
    CSerializedSendVoiceEventMessage(CCreature* actor, const unsigned char* packet);

    void OnHandle(CCreature* creature) override;
    void Clear() override;

private:
    CCreature* m_pActor;
    const unsigned char* packet;
};

class CSerializedSendUSMEventMessage : public CSerializedMessage {
public:
    CSerializedSendUSMEventMessage(CCreature* actor, int eventId);

    void OnHandle(CCreature* creature) override;
    void Clear() override;

private:
    CCreature* m_pActor;
    int m_nEventId;
};

class CSerializedSendUIEventMessage : public CSerializedMessage {
public:
    CSerializedSendUIEventMessage(CCreature* actor, const unsigned char* packet);

    void OnHandle(CCreature* creature) override;
    void Clear() override;

private:
    CCreature* m_pActor;
    const unsigned char* packet;
};

class CSerializedTakeOffItems : public CSerializedMessage {
public:
    CSerializedTakeOffItems(CCreature* actor, bool isByLogin, bool isByContainerChange);

    void OnHandle(CCreature* creature) override;
    void Clear() override;

private:
    CCreature* m_pActor;
    bool m_isByLogin;
    bool m_isByContainerChange;
};
