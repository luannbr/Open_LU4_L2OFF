#pragma once

#include "CGeneralObject.h"
#include "Creature.h"

#include "Shared/src/BaseStructure.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/YieldLock.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>

class CCreature;
class CDuel;
class CDuelPlayerBroadcastOperation;
class CDuelPlayerOperation;
class CDuelPlayerInfo;
class CIndividualDuelPlayerInfo;
class CIndividualDuel;
class CInstantZone;
class CPartyDuelPlayerInfo;
class CPartyDuel;
class CParty;
class CSocket;
class User;

// The spellings are donor-selected source vocabulary. Every numeric value is
// produced or consumed by the current L2Server image.
enum DuelStartErrorType : std::int32_t {
    DSET_SUCCESS = 0,
    DSET_ETC_ERROR = 0x794,
    DSET_IN_STORE_MODE = 0x7e1,
    DSET_FISHING = 0x7e2,
    DSET_NOT_ENOUGH_HP_MP = 0x7e3,
    DSET_IN_BATTLE_ZONE = 0x7e4,
    DSET_IN_NO_RESTART_ZONE = 0x7e4,
    DSET_IN_PEACE_ZONE = 0x7e4,
    DSET_IN_SSQ_ZONE = 0x7e4,
    DSET_IN_WATER = 0x7e4,
    DSET_IN_COMBAT_MODE = 0x7e5,
    DSET_IN_DUEL = 0x7e6,
    DSET_CHAO_VIOLET = 0x7e7,
    DSET_OLYMPIAD_REGISTERED = 0x7e8,
    DSET_IN_AGIT_WAR = 0x7e9,
    DSET_IN_CASTLE_WAR = 0x7ea,
    DSET_ON_VEHICLE = 0x7eb,
    DSET_TRANSFORMED = 0x87e,
    DSET_TELEPORT = 0x8f1,
    DSET_FAKE_DEATH = 0x10fa,
};

enum DuelTeamType : std::int32_t {
    DTT_TEAM_A = 0,
    DTT_TEAM_B = 1,
    DTT_TEAM_MAX = 2,
};

enum DuelType : std::int32_t {
    DT_INDIVIDUAL = 0,
    DT_PARTY = 1,
    DT_MAX = 2,
};


class CDuelBuilderCmdHandler {
public:
    static bool Handle(CSocket* socket, User* user, char16_t* command);

protected:
    using DuelBuilderCmdFunc = bool (*)(CSocket*, User*, char16_t*);

    struct stDuelBuilderCmdMap {
        const char16_t* pszCmdString;
        DuelBuilderCmdFunc func;
    };


    static stDuelBuilderCmdMap m_IndividualDuelCmdMap[2];
    static stDuelBuilderCmdMap m_PartyDuelCmdMap[1];

    static bool BuilderCmd_IndividualDuelStart(CSocket* socket, User* user, char16_t* command);
    static bool BuilderCmd_IndividualDuelCancel(CSocket* socket, User* user, char16_t* command);
    static bool BuilderCmd_PartyDuelStart(CSocket* socket, User* user, char16_t* command);
};

// Current operation objects are stack-only one-vptr visitors. None of the
// four abstract roots has a virtual destructor: the current concrete vtables
// contain only Operate, or Operate followed by OperateInternal.
class CDuelPlayerOperation {
public:
    virtual void Operate(User* user, CDuelPlayerInfo* playerInfo) = 0;
};

class CIndividualDuelPlayerOperation : public CDuelPlayerOperation {
public:
    void Operate(User* user, CDuelPlayerInfo* playerInfo) override;

protected:
    virtual void OperateInternal(User* user, CIndividualDuelPlayerInfo* playerInfo) = 0;
};

class CPartyDuelPlayerOperation : public CDuelPlayerOperation {
public:
    void Operate(User* user, CDuelPlayerInfo* playerInfo) override;

protected:
    virtual void OperateInternal(User* user, CPartyDuelPlayerInfo* playerInfo) = 0;
};

class CDuelPlayerBroadcastOperation {
public:
    virtual void Operate(User* receiver, User* subject, CDuelPlayerInfo* playerInfo) = 0;
};


// Current L2Server creates the base owner at 0x78 bytes, the individual
// subtype at the same extent and the party subtype at 0x98 bytes. The five
// virtuals below are their complete current vtable surfaces.
class CDuelPlayerInfo {
public:

    CDuelPlayerInfo() :
        m_nSID(0),
        m_dOriginalHP(0.0),
        m_dOriginalMP(0.0),
        m_dOriginalCP(0.0) {}

    virtual ~CDuelPlayerInfo() {
        if (!m_vSummonInfo.empty()) {
            for (CDuelPlayerInfo* summonInfo : m_vSummonInfo) {
                delete summonInfo;
            }
        }
        m_vSummonInfo.clear();
    }

    void SetSID(int sid) { m_nSID = sid; }

    int GetSID() { return m_nSID; }

    void SetOriginalStatus(double hp, double mp, double cp) {
        m_dOriginalHP = hp;
        m_dOriginalMP = mp;
        m_dOriginalCP = cp;
    }

    void GetOriginalStatus(double& hp, double& mp, double& cp) {
        hp = m_dOriginalHP;
        mp = m_dOriginalMP;
        cp = m_dOriginalCP;
    }

    void SetOrgInfectedSkills(std::vector<InfectedSkillType>& infectedSkills, CYieldLock& lock);
    void GetOrgInfectedSkills(CCreature* creature);

    virtual bool IsIndividualDuelPlayerInfo() { return false; }

    virtual bool IsPartyDuelPlayerInfo() { return false; }

    virtual CIndividualDuelPlayerInfo* CastIndividualDuelPlayerInfo() { return nullptr; }

    virtual CPartyDuelPlayerInfo* CastPartyDuelPlayerInfo() { return nullptr; }


protected:
    int m_nSID; // +0x08
    double m_dOriginalHP; // +0x10
    double m_dOriginalMP; // +0x18
    double m_dOriginalCP; // +0x20
    std::vector<InfectedSkillType> m_vOrgInfectedSkills; // +0x28

public:
    CYieldLock m_SummonLock; // +0x48
    std::vector<CDuelPlayerInfo*> m_vSummonInfo; // +0x58
};

class CIndividualDuelPlayerInfo : public CDuelPlayerInfo {
public:

    CIndividualDuelPlayerInfo() = default;

    bool IsIndividualDuelPlayerInfo() override { return true; }

    CIndividualDuelPlayerInfo* CastIndividualDuelPlayerInfo() override { return this; }
};

class CPartyDuelPlayerInfo : public CDuelPlayerInfo {
public:

    CPartyDuelPlayerInfo() : m_bIsAlive(true), m_nEnterPos(0) {}

    void SetDead() { m_bIsAlive = false; }

    void SetAlive() { m_bIsAlive = true; }

    bool IsAlive() { return m_bIsAlive; }

    void SetEnterPos(const int enterPos);

    int GetEnterPos() { return m_nEnterPos; }

    void SetReturnPos(FVector& returnPos);

    const FVector& GetReturnPos() { return m_fvReturnPos; }

    bool IsPartyDuelPlayerInfo() override { return true; }

    CPartyDuelPlayerInfo* CastPartyDuelPlayerInfo() override { return this; }


private:
    bool m_bIsAlive; // +0x78
    [[maybe_unused]] std::uint8_t gap_0079[3]; // +0x79..+0x7b
    int m_nEnterPos; // +0x7c
    FVector m_fvReturnPos; // +0x80
};


class Duel_SetDuelInfo final : public CDuelPlayerOperation {
public:
    Duel_SetDuelInfo(int duelSid, DuelTeamType team) : m_nDuelSid(duelSid), m_eDuelTeam(team) {}

    void Operate(User* user, CDuelPlayerInfo* playerInfo) override;

private:
    int m_nDuelSid; // +0x08
    DuelTeamType m_eDuelTeam; // +0x0c
};

class Duel_SetParamChanged final : public CDuelPlayerOperation {
public:
    void Operate(User* user, CDuelPlayerInfo* playerInfo) override;
};

class Duel_SetRelationChanged final : public CDuelPlayerOperation {
public:
    void Operate(User* user, CDuelPlayerInfo* playerInfo) override;
};

class Duel_ResetDuelInfo final : public CDuelPlayerOperation {
public:
    void Operate(User* user, CDuelPlayerInfo* playerInfo) override;
};

class Duel_Send final : public CDuelPlayerOperation {
public:
    Duel_Send(const char* format, std::va_list& arguments) : m_sFormat(format), m_arguments(arguments) {}

    void Operate(User* user, CDuelPlayerInfo* playerInfo) override;

private:
    const char* m_sFormat; // +0x08
    std::va_list& m_arguments; // +0x10
};

class Duel_PlayMusic final : public CDuelPlayerOperation {
public:
    explicit Duel_PlayMusic(const wchar_t* musicName) : m_sMusicName(musicName) {}

    void Operate(User* user, CDuelPlayerInfo* playerInfo) override;

private:
    const wchar_t* m_sMusicName; // +0x08
};

class Duel_SocialAction final : public CDuelPlayerOperation {
public:
    explicit Duel_SocialAction(int socialActionType) : m_nSocialActionType(socialActionType) {}

    void Operate(User* user, CDuelPlayerInfo* playerInfo) override;

private:
    int m_nSocialActionType; // +0x08
};

class Duel_StopAction final : public CDuelPlayerOperation {
public:
    void Operate(User* user, CDuelPlayerInfo* playerInfo) override;
};

class Duel_SetFullStatus final : public CDuelPlayerOperation {
public:
    void Operate(User* user, CDuelPlayerInfo* playerInfo) override;
};

class Duel_RestoreStatus final : public CDuelPlayerOperation {
public:
    void Operate(User* user, CDuelPlayerInfo* playerInfo) override;
};

class Duel_RestoreInfectedSkills final : public CDuelPlayerOperation {
public:
    void Operate(User* user, CDuelPlayerInfo* playerInfo) override;
};

class Duel_BroadcastSummonHPMPInfo final : public CDuelPlayerBroadcastOperation {
public:
    void Operate(User* receiver, User* subject, CDuelPlayerInfo* playerInfo) override;
};

class Duel_BroadcastHideSummonHPMPInfo final : public CDuelPlayerBroadcastOperation {
public:
    void Operate(User* receiver, User* subject, CDuelPlayerInfo* playerInfo) override;
};

class PartyDuel_EnterDuelZone final : public CPartyDuelPlayerOperation {
public:
    explicit PartyDuel_EnterDuelZone(CInstantZone* instantZone) : m_pInstantZone(instantZone) {}

private:
    void OperateInternal(User* user, CPartyDuelPlayerInfo* playerInfo) override;

    CInstantZone* m_pInstantZone;
};

class PartyDuel_LeaveDuelZone final : public CPartyDuelPlayerOperation {
public:
    explicit PartyDuel_LeaveDuelZone(CInstantZone* duelZone) : m_pDuelZone(duelZone) {}

private:
    void OperateInternal(User* user, CPartyDuelPlayerInfo* playerInfo) override;

    CInstantZone* m_pDuelZone;
};

class IndividualDuel_Draw final : public CIndividualDuelPlayerOperation {
private:
    void OperateInternal(User* user, CIndividualDuelPlayerInfo* playerInfo) override;
};

class IndividualDuel_Win final : public CIndividualDuelPlayerOperation {
private:
    void OperateInternal(User* user, CIndividualDuelPlayerInfo* playerInfo) override;
};

class IndividualDuel_Lose final : public CIndividualDuelPlayerOperation {
private:
    void OperateInternal(User* user, CIndividualDuelPlayerInfo* playerInfo) override;
};

class PartyDuel_SetAlive final : public CPartyDuelPlayerOperation {
private:
    void OperateInternal(User* user, CPartyDuelPlayerInfo* playerInfo) override;
};

class PartyDuel_Draw final : public CPartyDuelPlayerOperation {
private:
    void OperateInternal(User* user, CPartyDuelPlayerInfo* playerInfo) override;
};

class PartyDuel_Win final : public CPartyDuelPlayerOperation {
private:
    void OperateInternal(User* user, CPartyDuelPlayerInfo* playerInfo) override;
};

class PartyDuel_Lose final : public CPartyDuelPlayerOperation {
private:
    void OperateInternal(User* user, CPartyDuelPlayerInfo* playerInfo) override;
};


// A single process-wide manager owns the two product strategies. Current
// allocation extents are 0x18 for this manager and 0x38 for either strategy.
// the latter grew from the donor layout with the current xmap/CYieldLock
// carriers.
class CDuelManagementUnit {
public:
    using DuelMapType = std::map<int, CDuel*>;

    CDuelManagementUnit() = default;

    virtual bool CheckDuelStartCondition(User* requestor, User* target, bool sendMessage) = 0;
    virtual void StartDuel(User* requestor, User* target) = 0;
    virtual void EndDuel(CDuel* duel) = 0;
    virtual void CancelDuel(User* user, int reason) = 0;
    virtual ~CDuelManagementUnit() = default;

    // No current compiled bodies or callers prove these four donor
    // declarations as standalone methods.
    void TriggerDuelTimer();
    int GetDuelCountInMap();
    void OnCloseUp();
    void Clear();


protected:
    void HandleDuelStartConditionResult(User* requestor, DuelStartErrorType error, User* target);

    DuelMapType m_DuelMap; // +0x08
    CYieldLock m_DuelMapLock; // +0x28
};

class CIndividualDuelManagement : public CDuelManagementUnit {
private:
    bool CheckDuelStartCondition(User* requestor, User* target, bool sendMessage) override;
    void StartDuel(User* requestor, User* target) override;
    void EndDuel(CDuel* duel) override;

    void CancelDuel(User* user [[maybe_unused]], int reason [[maybe_unused]]) override {}
};

class CPartyDuelManagement : public CDuelManagementUnit {
private:
    bool CheckDuelStartCondition(User* requestor, User* target, bool sendMessage) override;
    void StartDuel(User* requestor, User* target) override;
    void EndDuel(CDuel* duel) override;

    void CancelDuel(User* user [[maybe_unused]], int reason [[maybe_unused]]) override {}
};

class CDuelManager : public Singleton<CDuelManager> {
public:

    CDuelManager();
    ~CDuelManager() override;

    bool CheckDuelStartCondition(User* requestor, User* target, DuelType duelType);
    bool StartDuel(User* requestor, User* target, DuelType duelType);
    void EndDuel(CDuel* duel);
    void CancelDuel(User* user, int reason);
    void DuelPlayerDie(User* user, CCreature* attacker);
    void DuelPlayerLeaveWorld(User* user);
    void UpdateDuelPlayerStatus(User* user);
    static DuelTeamType GetOpponentTeam(DuelTeamType team);
    void TriggerDuelTimer();
    void OnCloseUp();
    int GetDuelCountInMap();
    void Clear();


private:
    CDuelManagementUnit* m_pDuelManagement[DT_MAX]; // +0x08
};


// Current L2Server's abstract duel owner. The inherited vtable prefix is
// CGeneralObject. Slots 154..177 below are the complete CDuel-specific tail.
class CDuel : public CGeneralObject {
public:

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    CDuel();
    CDuel(const CDuel& other);
    ~CDuel() override;

    void Start();
    void Cancel(User* user, int reason);
    void End();
    void SendDuelResult();
    void PlayerDie(User* user, CCreature* attacker);
    void PlayerLeaveWorld(User* user);
    void PlayerStatusUpdate(User* user);
    void StartTimer();
    void TimerExpired(int timerId) override;

    static void ClearSummonStatus(CCreature* creature);

    int GetDuelStatus() const noexcept { return m_nStatus; }

    static CDuel* GetObjectW(std::uint32_t smartId);

    bool IsDuel() override { return true; }

    CDuel* CastDuel() override { return this; }

    // Slots 154..157.
    virtual bool IsPartyDuel() const { return false; }

    virtual bool IsIndividualDuel() const { return false; }

    virtual CPartyDuel* CastPartyDuel() { return nullptr; }

    virtual CIndividualDuel* CastIndividualDuel() { return nullptr; }

protected:
    // Slots 158..162.
    virtual void OnReady() = 0;
    virtual void OnStart() = 0;
    virtual void OnCancel(User* user, int reason) = 0;
    virtual void OnPreEnd() = 0;
    virtual void OnEnd() = 0;

public:
    // Slot 163 is public in the original class contract.
    virtual void OnCloseUp() = 0;

protected:
    // Slots 164..177.
    virtual void OnSendDuelResult() = 0;
    virtual void OnPlayerDie(User* user, CCreature* attacker) = 0;
    virtual void OnPlayerLeaveWorld(User* user) = 0;
    virtual void SendToAllPlayers(const char* format, ...) = 0;
    virtual void SendToTeamPlayers(DuelTeamType team, const char* format, ...) = 0;
    virtual void SaveDuelRecord() = 0;
    virtual DuelType GetDuelType() const = 0;
    virtual void ForAllPlayer(CDuelPlayerOperation& operation) = 0;
    virtual void ForTeamPlayer(DuelTeamType team, CDuelPlayerOperation& operation) = 0;
    virtual void BroadcastForAllPlayer(CDuelPlayerBroadcastOperation& operation) = 0;
    virtual int GetWaitingTime() const = 0;
    virtual int GetFightTime() const = 0;
    virtual int GetPreEndTime() const = 0;
    virtual int GetEndTime() const = 0;

    void OnPlayerStatusUpdate(User* user);
    void SetDuelStatus(int status);
    void SetWinner(DuelTeamType team);

public:
    DuelTeamType GetWinner() const;
    DuelTeamType GetLoser();


protected:
    friend class CDuelManager;

    int m_nStatus; // +0x50
    std::uint32_t m_tDuelTime; // +0x54
    bool m_bEndDuel; // +0x58
    std::uint8_t gap_0059[3]; // +0x59..+0x5b
    DuelTeamType m_eWinnerTeam; // +0x5c
};


class CIndividualDuel : public CDuel {
public:

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    CIndividualDuel(User* first, User* second);
    ~CIndividualDuel() override;
    void InitUser(User* user, DuelTeamType team);

    bool IsIndividualDuel() const override { return true; }

    CIndividualDuel* CastIndividualDuel() override { return this; }

    DuelType GetDuelType() const override { return DT_INDIVIDUAL; }

    int GetWaitingTime() const override { return 5000; }

    int GetFightTime() const override { return 120000; }

    int GetPreEndTime() const override { return 3000; }

    int GetEndTime() const override { return 3000; }


private:
    friend class CDuelUserInfo;

    void OnReady() override;
    void OnStart() override;
    void OnCancel(User* user, int reason) override;
    void OnPreEnd() override;
    void OnEnd() override;
    void OnCloseUp() override;
    void OnSendDuelResult() override;
    void OnPlayerDie(User* user, CCreature* attacker) override;
    void OnPlayerLeaveWorld(User* user) override;
    void SendToAllPlayers(const char* format, ...) override;
    void SendToTeamPlayers(DuelTeamType team, const char* format, ...) override;
    void SaveDuelRecord() override;
    void SetEnemyRelationAll();
    void ForAllPlayer(CDuelPlayerOperation& operation) override;
    void ForTeamPlayer(DuelTeamType team, CDuelPlayerOperation& operation) override;
    void BroadcastForAllPlayer(CDuelPlayerBroadcastOperation& operation) override;
    void RestoreStatus();
    User* GetPlayer(DuelTeamType team);

    int m_nPlayerId[DT_MAX]; // +0x60
    CIndividualDuelPlayerInfo* m_pPlayerInfo[DT_MAX]; // +0x68
    int m_eEndMode; // +0x78
    std::uint8_t gap_007c[4]; // +0x7c..+0x7f
};

class CPartyDuel : public CDuel {
public:
    using PlayerInfoMap = std::map<unsigned int, CPartyDuelPlayerInfo*>;

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    CPartyDuel(CParty* first, CParty* second);
    ~CPartyDuel() override;

    bool IsPartyDuel() const override { return true; }

    CPartyDuel* CastPartyDuel() override { return this; }

    DuelType GetDuelType() const override { return DT_PARTY; }

    int GetWaitingTime() const override { return 30000; }

    int GetFightTime() const override { return 300000; }

    int GetPreEndTime() const override { return 3000; }

    int GetEndTime() const override { return 10000; }


private:
    void OnReady() override;
    void OnStart() override;
    void OnCancel(User* user, int reason) override;
    void OnPreEnd() override;
    void OnEnd() override;
    void OnCloseUp() override;
    void OnSendDuelResult() override;
    void OnPlayerDie(User* user, CCreature* attacker) override;
    void OnPlayerLeaveWorld(User* user) override;
    void SendToAllPlayers(const char* format, ...) override;
    void SendToTeamPlayers(DuelTeamType team, const char* format, ...) override;
    void SaveDuelRecord() override;
    bool IsTeamAllDead(DuelTeamType team);
    void EnterDuelZone();
    void LeaveDuelZone();
    void SetFence(bool enabled);
    void SetDuelZoneId(int zoneId);
    int GetDuelZoneId();
    void ForAllPlayer(CDuelPlayerOperation& operation) override;
    void ForTeamPlayer(DuelTeamType team, CDuelPlayerOperation& operation) override;
    void ForPlayer(User* user, DuelTeamType team, CDuelPlayerOperation& operation);
    void BroadcastForAllPlayer(CDuelPlayerBroadcastOperation& operation) override;

    PlayerInfoMap m_mPlayerInfoMap[DT_MAX]; // +0x60
    CYieldLock m_PlayerInfoMapLock; // +0xa0
    int m_eEndMode; // +0xb0
    int m_nDuelZoneId; // +0xb4
};
