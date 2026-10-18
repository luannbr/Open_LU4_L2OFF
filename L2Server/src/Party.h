#pragma once

#include "MultiPartyVisitor.h"
#include "Object.h"
#include "Packet.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadSafeObjectMap.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>


class CCreature;
class CDB;
class CItem;
class CMultiPartyCommandChannel;
class CSummon;
class User;

enum ItemRoutingType : std::int32_t {
    IRT_PICKER_FIRST = 0,
    IRT_RANDOMLY = 1,
    IRT_RANDOMLY_INCLUDING_SPOIL = 2,
    IRT_ORDERLY = 3,
    IRT_ORDERLY_INCLUDING_SPOIL = 4,
    IRT_MASTER_FIRST = 5,
};


namespace DivideType {
enum Enum : std::int32_t {
    DIT_DEFAULT = 0,
    DIT_ADENA_CHANGE = 1,
    DIT_MEMBER_CHANGE = 2,
    DIT_ADENA_LACK = 3,
    DIT_MASTER_CHANGE = 4,
    DIT_MAX = 5,
};
}


namespace TeleportSourceType {
enum Enum : std::int32_t;
}

// Exact current L2Server Party envelope. The factory record and stripe are
// borrowed. The embedded RWLock and DBID-to-acceptance tree are owned.
class CParty : public CObject {
public:
    // Current RTTI fixes this nested owner as Singleton<CParty::Stat>, and
    // the current singleton accessor allocates exactly 0x10 bytes. Current
    // call sites bind the two 32-bit counters at +0x08 and +0x0c. The names
    // are selected from the TotalChecker2015 donor PDB.
    class Stat : public ::Singleton<Stat> {
    public:
        void Init() {
            m_nTotalPartyCount = 0;
            m_nTotalPartyMemberCount = 0;
        }

        void IncrementTotalPartyCount() { runtime::raw_atomic::IncrementI32(&m_nTotalPartyCount); }

        void DecrementTotalPartyCount() { runtime::raw_atomic::DecrementI32(&m_nTotalPartyCount); }

        void IncrementTotalPartyMemberCount() { runtime::raw_atomic::IncrementI32(&m_nTotalPartyMemberCount); }

        void DecrementTotalPartyMemberCount() { runtime::raw_atomic::DecrementI32(&m_nTotalPartyMemberCount); }

        int GetTotalPartyCount() { return m_nTotalPartyCount; }

        int GetTotalPartyMemberCount() { return m_nTotalPartyMemberCount; }

    private:
        volatile std::int32_t m_nTotalPartyCount;
        volatile std::int32_t m_nTotalPartyMemberCount;
    };

    explicit CParty(ItemRoutingType routingType);
    ~CParty() override;

    CParty(const CParty&) = delete;
    CParty& operator=(const CParty&) = delete;

    L2_DECLARE_OBJECT_REGISTRATION(CParty)

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static CParty* TestCreateParty(User* requester, User* replier, ItemRoutingType routingType);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void TimerExpired(int id) override;
    int GetIndex() const override;
    CObject* Copy() override;

    ItemRoutingType ReservedItemLootingType();
    bool IsLootingTypeModifyInProgress();
    bool RouteItem(CItem* item, User* user);
    void SetMaster(User* user);
    void SetRoutingType(ItemRoutingType routingType);
    ItemRoutingType GetRoutingType() const;
    bool InsertMemberSid(std::uint32_t sid, bool rejectDuplicate);
    void DeleteMemberSid(std::uint32_t sid);
    int GetTacticalSignId(CCreature& creature);
    bool RouteAdena(CItem* item, User* user, std::vector<User*>& members);
    bool CheckRouteAdenaCond(CItem* item, User* user, std::vector<User*>& members, bool rangeMode);

    User* GetMaster();
    void CancelTradeAllMember();
    User* GetItemWinnerOrderly(User* referenceUser, std::uint32_t itemId, int amount);
    User* GetItemWinnerRandomly(User* referenceUser, std::uint32_t itemId, int amount);
    CMultiPartyCommandChannel* GetMPCC();
    User* GetMember(std::uint32_t sid);
    CCreature* GetTacticalSignedCreature(int signId);
    void SendTacticalSignOnJoinParty(User& user);
    void SendTacticalSignOnLeaveParty(User& user);
    bool IsMember(std::uint32_t sid);
    void GetAllMemberWhoCanGetExpSp(std::vector<User*>& users, CCreature* creature);
    void GetAllMember(std::vector<User*>& users);
    int GetMemberCount() const noexcept { return m_nMemberCount; }
    int GetLevSum() const noexcept { return m_nLevSum; }
    std::u16string GetAllMemberName();
    int GetMinLevelOfAllMember(bool checkDistance, int radius, FVector position, bool checkFilter, int filterArgument);
    void GetAllMemberId(std::vector<std::uint32_t>& sids);

    void OnSetTacticalSign(CCreature& creature, int signId);
    void OnJoinParty(User& user);
    void OnLeaveParty(User& user);
    void BroadcastOnSetTacticalSign(CCreature& setter, CCreature& creature, int signId);
    void OnResetTacticalSign(CCreature& creature);
    bool SetTacticalSign(CCreature& setter, CCreature& creature, int signId);

    bool CheckAcceptStatus();
    void CompleteModifyLootingType();
    void AcceptModifyLootingType(User* user);
    bool StartModifyLootingType(ItemRoutingType routingType);

    void MemberPositionPacket();
    void DivideAdenaStart();
    void SendCancelPacket(int messageId, DivideType::Enum divideType);
    void DivideAdenaCancel(DivideType::Enum divideType);
    void DivideAdena(std::int64_t totalAmount);
    void AnnounceSweepItem(User* user, std::uint32_t itemId, int amount);
    bool SweepItem(int itemId, int amount, User* user);
    bool AddMember(User* user);
    bool DeleteMember(std::uint32_t sid);
    void DismissImpl(bool retainNonMasterContext, bool writeDismissLogs);
    bool DismissByWithdraw(User* user, bool retainNonMasterContext);
    bool Dismiss(bool retainNonMasterContext, bool writeDismissLogs);
    void AddPetWindowPacket(CSummon* summon);
    void UpdatePetWindowPacket(CSummon* summon);
    void ClosePetWindowPacket(CSummon* summon);
    void OpenAllWindowPacket(User* recipient);
    void OpenAllWindowPacket();
    void AddWindowPacket(User* member);
    void UpdateWindowPacket(User* member, int unusedFirst, int relationType, int relationValue, int unusedLast);
    void CloseAllWindowPacket();
    bool CheckRouteItem(CItem* item, User* user);
    User* CheckRouteItemCond(CItem* item, User* user);
    bool Route(CItem* item, User* user, bool keepAdenaItem);
    void TeleportToLocation(
        int x, int y, int z, int randomRange, std::uint32_t parameter, TeleportSourceType::Enum source, CCreature* creature
    );
    void ValidateLevel();
    User* GetMasterCandidate();
    void OnReplaceMaster(User* oldMaster);
    bool HandOverMaster(User* newMaster);
    std::uint32_t Oust(User& user, wchar_t* name);
    void Withdraw(User* user, bool sendPacket);
    void OnPacketOust(User& user, wchar_t* name);
    void SendAllSpelledInfoChanged();
    void Accept(CPartyMemberVisitor& visitor);
    void BroadcastToAllParty(const char* format, ...);
    void BroadcastToAllPartyV(const char* format, va_list arguments);
    void BroadcastSystemMsgToAllParty(std::uint32_t messageId, eSMsgType messageType, const char16_t* text);
    void StartScenePlay(int sceneId, int param1, int param2);
    bool IsAllPartyMemberSamePledge();
    bool IsAnyOneIsInAirShip();
    bool ResetTacticalSign(CCreature& creature);
    void OnResetTacticalSign(int creatureSid);


protected:
    virtual FVector GetPos() const;

public:
    volatile std::int32_t m_Divide;
    volatile std::int32_t m_Dismissed;
    CSharedPartyData* m_data;
    ::CYieldLock* m_lock;

protected:
    RWLock m_PartyLootingLock;
    std::int32_t m_nTimerCount;
    std::int32_t m_LevelGap;
    std::int32_t m_nLevSum;
    std::int32_t m_nMaxLev;
    std::int32_t m_nMinLev;
    std::uint32_t m_aOrderedMember[7];
    std::int32_t m_nNextItemWinnerIndex;
    std::int32_t m_nMemberCount;
    ItemRoutingType m_ItemRoutingType;
    bool m_IsLootingTypeModifyInProgress;
    [[maybe_unused]] std::byte padding_cd[3];
    ::std::map<std::uint32_t, bool> m_LootingTypeModifyStatus;
    std::int32_t m_LootingChangeTimeout;
    ItemRoutingType m_ReservedItemLootingType;

private:
    friend class CDB;
    friend class CMultiPartyCommandChannel;

    inline static const wchar_t* m_pClassName = L"CParty";
};

using CPartySmartIdManager = CSmartIdManager<CParty, 10003, 16>;
using CPartyObjectMap = CObjectMap<CParty, 1, 5, RWLock, int>;

extern CPartySmartIdManager g_PartySmartIdManager;
extern CPartyObjectMap g_PartyMap;

CPartySmartIdManager* PartySmartIdManager() noexcept;

// Current constructor/destructor address the map at +0x08 and RWLock at
// +0x28. The next global starts 0x60 bytes after g_PartyMap.
