#pragma once

#include "Residence.h"

#include "Shared/src/Obj.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

enum DominionSiegeStateType : std::int32_t {
    DSST_FINISHED = 0,
    DSST_CASTLE_SIEGE = 1,
    DSST_REGISTRATION = 2,
    DSST_STANDBY = 3,
    DSST_DOMINION_SIEGE = 4,
};


class CDominion;
class CCastle;
class CDBSocket;
class CFortress;
class COwnThing;
class CPledge;
class NpcSocket;
class User;
enum SayPacketType : std::int32_t;
enum eSMsgType : std::int32_t;

// Current 0x78 CDominionSiegeEntry prefix. The two vectors and their shared
// lock are enough for the exact membership queries used by the dominion
// and restart paths. No lifecycle/vtable is compiled by this declaration.
class CDominionSiegeEntry : public MemoryObject {
public:
    explicit CDominionSiegeEntry(CDominion* dominion);
    ~CDominionSiegeEntry() override = default;

    bool IsEmpty();
    bool IsInSiegePledge(std::uint32_t pledgeId);
    bool IsInSiegeUser(std::uint32_t userId);
    bool AddPledge(std::uint32_t pledgeId);
    bool AddUser(std::uint32_t userId);
    bool RemovePledge(std::uint32_t pledgeId);
    bool RemoveUser(std::uint32_t userId);
    void SetSiegeRole(bool underSiege);
    void Say(User* user, const char16_t* message, SayPacketType type);
    void SendDominionChannelSet(bool enabled);
    void CancelAllRenamed();
    void SendDominionMessage(int messageId, int argument, eSMsgType argumentType, const char16_t* text, bool showEffect);
    void SendDominionMessage2(int messageId, int argument);
    void SendDominionEvent(bool start);
    void SendDominionScriptEvent(int eventType, int npcId, int dominionId);
    void UpdateWindowPacket();
    void Clear();
    int GetPledgeListSize() { return static_cast<int>(m_vPledgeList.size()); }
    int GetUserListSize() { return static_cast<int>(m_vUserList.size()); }
    std::vector<std::uint32_t> GetPledgeList() { return m_vPledgeList; }
    std::vector<std::uint32_t> GetUserList() { return m_vUserList; }

private:
    void SendDominionEventToPledge(bool start);
    void SendDominionEventToUser(bool start);
    void SendDominionScriptEventToPledge(int eventType, int npcId, int dominionId);
    void SendDominionScriptEventToUser(int eventType, int npcId, int dominionId);

    friend class CDominionManager;
    friend bool NpcGetRegistrySize(NpcSocket*, const unsigned char*);

    CDominion* m_pDominion; // +0x18
    std::int32_t m_nDominionID; // +0x20
    std::byte m_alignment24To28[0x04];
    std::vector<std::uint32_t> m_vPledgeList; // +0x28
    std::vector<std::uint32_t> m_vUserList; // +0x48
    CYieldLock m_Lock; // +0x68
};


// Current CDominion allocation extent is 0x6c0. Its full concrete vtable is
// restored with the Dominion owner. This physical declaration already makes
// the +0x520 siege-entry route type-safe without raw-offset access.
class CDominion : public CResidence {
public:
    using OwnThingList = std::vector<std::pair<COwnThing*, FVector>>;

    CDominion();
    ~CDominion() override;

    L2_DECLARE_OBJECT_TYPE(CDominion)

    bool IsExtended() const override;
    bool IsDominion() const override;
    CDominion* CastDominion() const override;
    bool IsUnderSiege() const override;
    void StartTimer() override;
    void StartSiege() override;
    void EndSiege() override;
    void PassSiege() override;
    void ChangeOwner(CPledge* pledge, int reason, bool announce) override;
    void PauseSiege() override;
    void ResumeSiege() override;
    void ExpelNonPledgeMember() override;

    wchar_t* GetPickupOwnThingName();
    wchar_t* GetHideNameItemName();
    void SetPickupOwnThingName(wchar_t* name);
    void SetHideNameItemName(wchar_t* name);
    void SetMainCastle(CCastle* castle);
    CCastle* GetCastle() { return m_pMainCastle; }
    void AddPos(FVector position);
    FVector ReservePos();
    void FreePos(FVector position);
    FVector _GetPos(COwnThing* ownThing);
    FVector GetPos(COwnThing* ownThing);
    void RemoveOwnthing(COwnThing* ownThing);
    void AddOwnThing(COwnThing* ownThing);
    OwnThingList GetOwnThingList() const { return m_OwnThingList; }
    CDominionSiegeEntry* GetSiegeEntry() { return m_pSiegeEntry; }
    COwnThing* GetOwnThing() { return m_pMainOwnThing; }
    CYieldLock& GetOwnThingLock() { return m_OwnThingLock; }

    bool IsPledgeInSiege(int pledgeId);
    bool IsUserInSiege(int userId);
    bool RegisterPledge(int pledgeId, bool save);
    bool RegisterUser(int userId, bool save);
    bool UnregisterPledge(int pledgeId, bool save);
    bool UnregisterUser(int userId, bool save);
    void ReleaseEntry();
    void StandBySiege();
    void MakeEntry();
    void CancelAllRenamed();
    void Say(User* user, const char16_t* message, SayPacketType type);
    void SetSiegeRole(bool underSiege);
    void RequestBroadcastMessage(int messageType);
    void StartRegistration();
    void ChangeDominionState(int state);
    void ChangeDominionStatus(int status);
    void DisableDominionExtends(CSPointer<CPledge> pledge);
    void BanNonDominionMember(bool expel);
    void AddAllResidenceSkill();
    void RemoveAllResidenceSkill(CSPointer<CPledge> pledge);
    int EnableDominionExtends();
    int ExtendDominion();
    int CancelExtendDominion();

private:
    friend bool ReplyLoadOwnthing(CDBSocket*, const unsigned char*);
    friend bool NpcGetRegistrySize(NpcSocket*, const unsigned char*);
    friend class CDominionManager;
    friend class CParserForResidence;
    friend class COwnThing;
    friend class CObjectDB;
    friend class CSkillEffect_i_register_siege_golem;

    CCastle* m_pMainCastle; // +0x490
    COwnThing* m_pMainOwnThing; // +0x498
    OwnThingList m_OwnThingList; // +0x4a0
    std::vector<std::pair<FVector, bool>> m_vPosList; // +0x4c0
    CYieldLock m_OwnThingLock; // +0x4e0
    CYieldLock m_PosLock; // +0x4f0
    std::set<CFortress*> m_sDomainFortress; // +0x500
    CDominionSiegeEntry* m_pSiegeEntry; // +0x520
    bool m_bPaused; // +0x528
    std::byte m_alignment529To52A[0x01];
    char16_t m_sPickupOwnThingName[100]; // +0x52a
    char16_t m_sHideNameItemName[100]; // +0x5f2
    std::byte m_alignment6BATo6C0[0x06];
};


// Current Dominion.cpp process-local singleton. Unlike the shared
// Singleton<T> family this owner is a function-static object, so its first
// physical member follows CIOObject directly at +0x18.
class CDominionManager : public CIOObject {
public:
    using DominionList = std::vector<CDominion*>;

    L2_DECLARE_OBJECT_REGISTRATION(CDominionManager)

    static CDominionManager* Inst();

    CDominionManager();
    ~CDominionManager() override;

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()
    void TimerExpired(int id) override;

    void Clear();
    void SetSiegeEndTime(int siegeEndTime, bool save);
    bool IsInReadyToDominionSiege();
    void SetSiegeTime();
    void SetSiegeTimeByBuilder();
    void SetNextSiegeTime(int nextSiegeTime, bool save);
    void ChangeState(DominionSiegeStateType state, bool save);
    int AdjustNextSiegeTime();
    void StandBySiege();
    void PauseSiege();
    void StartSiege();
    void ResumeSiege();
    void EndSiege();
    void ProcessExtraEndJob();
    void SiegeTimerExpired(int id);
    int CheckStartDominionRegistration(int reason, bool force);
    void StartRegistration();

    void Add(CDominion* dominion);
    void AddStart(CDominion* dominion);
    int GetUserDominionSiegeID(int userId);
    int GetPledgeDominionSiegeID(int pledgeId);
    DominionList GetDominionList() const;
    DominionSiegeStateType GetState() { return m_nState; }
    int GetNextSiegeTime() { return m_tNextSiegeTime; }
    int GetSiegeEndTimeInSiege() const noexcept { return runtime::AddInt32Wrapping(m_tSiegeStartTime, m_tSiegeTime); }

    void SendDominionChannelSet(bool enabled);
    void SendDominionMessageToMembers(int messageType);
    void SendDominionMessage(int messageId, int argument, eSMsgType argumentType, const char16_t* text, bool showEffect);
    void SendDominionScriptEvent(int eventType, int npcId, int dominionId);
    void BroadcastDominionMessageToAll(int messageType);
    void BroadcastDominionMessage(int messageType);
    void AnnounceMessage(int messageType, int targetType);
    void SendDominionRegistry(int userId, int dominionId);

private:
    friend bool ReplyLoadDominionSiege(CDBSocket*, const unsigned char*);
    friend class User;
    friend class COwnThing;
    friend class CDominion;
    friend class CCastle;
    friend class CFortress;

    DominionList m_lstStart; // +0x18
    DominionList m_lstDominion; // +0x38
    CYieldLock m_StartLock; // +0x58
    CYieldLock m_DominionLock; // +0x68
    std::int32_t m_tPreviousSiegeEnd; // +0x78
    std::int32_t m_tNextSiegeTime; // +0x7c
    bool m_bNextSiegeTimeSet; // +0x80
    std::byte m_alignment81To84[0x03];
    std::int32_t m_nErrorCount; // +0x84
    bool m_bPaused; // +0x88
    std::byte m_alignment89To8C[0x03];
    std::int32_t m_tSiegeStartTime; // +0x8c
    std::int32_t m_tSiegeElapsedTime; // +0x90
    std::int32_t m_nSaveTick; // +0x94
    std::int32_t m_tSiegeTime; // +0x98
    std::int32_t m_tPausedTime; // +0x9c
    std::int32_t m_nAnnouncedStatus; // +0xa0
    bool m_bServerDownResume; // +0xa4
    std::byte m_alignmentA5ToA8[0x03];
    DominionSiegeStateType m_nState; // +0xa8
    bool m_bSentDominionChannelSet; // +0xac
    bool m_bCanceledRenamed; // +0xad
    std::byte m_alignmentAEToB0[0x02];
    std::int32_t m_nLoadedOwnThing; // +0xb0
    std::int32_t m_nScriptEventSaveCount; // +0xb4
    std::int32_t m_nScriptEventSaveTick; // +0xb8
    std::byte m_alignmentBCToC0[0x04];
};
