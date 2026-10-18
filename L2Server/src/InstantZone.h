#pragma once

#include "CGeneralObject.h"

#include "L2Core/src/inc/XStd.h"
#include "Shared/src/IOC.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>


class CArea;
class CDBSocket;
class CDoor;
class CEventController;
class CUserSocket;
class CInstantAgitManager;
class CInstantZone;
class CInstantZoneCluster;
class CInstantZoneGroup;
class CInstantZoneReleaseVerifier;
class CParserForInstantZone;
class CParty;
class CInstantZoneEntranceCondition_check_level;
class CInstantZoneEntranceCondition_check_quest;
class CInstantZoneEntranceCondition_check_pledge;
class CInstantZoneEntranceCondition_check_party_chair;
class CInstantZoneEntranceCondition_check_exclusive_point_inzone;
class CItem;
class CNPC;
class NpcSocket;
class CPlaneCollision;
class CPledgeGameField;
class PledgeRaidSystem;
class CPrivateContainer;
class CStaticObject;
class CTLMFilter;
class User;
class WOlympiadMatch;
class WOlympiadService;

bool BuilderCmd_InZone(CUserSocket*, User*, char16_t*);

class CInstantZoneNpcAIEventHandler {
public:
    static void Receive(const unsigned char* packet);
};

bool NpcCastleGateOpenClose2(NpcSocket* socket, const unsigned char* packet);
bool ReplyLoadInzoneMaxDBID(CDBSocket* socket, const unsigned char* packet);

template <std::size_t BufferSize>
class CPacketHelperEx;

enum InZoneBindingType : std::int32_t {
    IZBT_BUILDER = 0,
    IZBT_CONTENTS = 1,
    IZBT_TIME = 2,
    IZBT_MAX = 3,
};

enum InZoneEnterReasonType : std::int32_t {
    IZERT_FAILED = -1,
    IZERT_NORMAL = 0,
    IZERT_TIME_RESET = 1,
    IZERT_GROUP_EXTRA1 = 2,
    IZERT_BIND_EXPIRED = 3,
    IZERT_GROUP_EXTRA2 = 4,
};

enum InZoneEnterType : std::int32_t {
    IZET_INDIVIDUAL = 0,
    IZET_PARTY = 1,
    IZET_MPCC = 2,
    IZET_MAX = 3,
};

namespace InstantZoneLogHelper {
void Log(CInstantZone* zone, User* user, int logId, InZoneEnterType enterType, int timeLimit, int reason);
}

enum InstantZoneBindingLevel : std::int32_t {
    IZBL_NOT_BINDED = 0,
    IZBL_BINDED = 1,
    IZBL_EXTRA_ENTRANCE = 2,
    IZBL_ABS_BINDED = 3,
};

enum InstantZoneDeleteInfo : std::int32_t {
    IZDI_DURATION_EXPIRED = 0,
    IZDI_CONTENTS_FINISHED = 1,
    IZDI_NO_USER = 2,
    IZDI_NPCSERVER_DOWN = 3,
    IZDI_DUEL_FINISHED = 4,
    IZDI_BY_BUILDER = 5,
    IZDI_ENTER_FAILED = 6,
    IZDI_TEST = 7,
    IZDI_INSUFFICIENT_CONTAINER = 8,
    IZDI_MAX = 9,
};

enum InstantZoneStatus : std::int32_t {
    IZS_INIT = 0,
    IZS_READY = 1,
    IZS_DOING = 2,
    IZS_CONTENT_FINISH = 3,
};

// The spellings are donor-selected source vocabulary. Every numeric value is
// produced or consumed by the current L2Server image.
enum CreateFailReason : std::int32_t {
    CFR_INTERNAL_ERROR = 0,
    CFR_LIMIT_ZONE_COUNT = 0x838,
    CFR_LIMIT_MULTIFUL_ZONE = 0x839,
    CFR_NPC_SERVER_DOWN = 0x897,
    CFR_LIMIT_DESTROYED_ZONE = 0x8bb,
};


enum EndPositionType : std::int32_t {
    EPT_DESIGNATORY = 0,
    EPT_ORIGIN = 1,
    EPT_ORIGIN_RESTART = 2,
    EPT_RANDOM = 3,
    EPT_MAX = 4,
};


enum StartPositionType : std::int32_t {
    SPT_DESIGNATORY = 0,
    SPT_RANDOM = 1,
    SPT_MAX = 2,
};

enum InZoneType : std::int32_t {
    IZT_NORMAL = 0,
    IZT_OLYMPIAD = 1,
    IZT_INSTANT_AGIT = 2,
    IZT_PLEDGEGAME = 3,
    IZT_PLEDGE_RAID = 4,
    IZT_MAX = 5,
};


enum EnterFailReason : std::int32_t {
    EFR_EXCLUSIVE_POINT_INZONE_FAIL = 0,
    EFR_INTERNAL_ERROR = 0,
    EFR_LIMIT_DISTANCE = 0x830,
    EFR_LIMIT_LEVEL = 0x831,
    EFR_LIMIT_QUEST = 0x832,
    EFR_LIMIT_ITEM = 0x833,
    EFR_LIMIT_USE_COUNT = 0x834,
    EFR_LIMIT_PARTY = 0x835,
    EFR_LIMIT_PARTY_MEMBER_COUNT = 0x836,
    EFR_LIMIT_MPCC = 0x837,
    EFR_LIMIT_MULTIFUL_ZONE = 0x839,
    EFR_NOT_PARTY_MASTER = 0x889,
    EFR_MAKER_NOT_LOADED = 0x897,
    EFR_LIMIT_PCCAFE_POINT = 0x914,
    EFR_LIMIT_MPCC_MEMBER_COUNT = 0xacc,
    EFR_NOT_MPCC_MASTER = 0xacd,
    EFR_LIMIT_MIN_MEMBER_COUNT = 0xae9,
    EFR_LIMIT_CREATE_COUNT = 0xc4c,
    EFR_ALREADY_TELEPORT = 0x1001,
    EFR_PARTY_CHAIR_FAIL = 0x1129,
};


struct InstantZoneEnterFailInfo {
    InstantZoneEnterFailInfo() {
        reason = static_cast<EnterFailReason>(0);
        intParam = 0;
    }

    EnterFailReason reason;
    char16_t efUser[24];
    std::int32_t intParam;
};


// Current parser-owned polymorphic condition envelope. The target has one
// virtual slot (CanEnter), followed by the re-entry policy byte and the
// failure reason consumed by CInstantZone::CheckEnterConditionCommon.
class CInstantZoneEntranceCondition {
public:
    // The current default constructor is trivial. In
    // particular, parser action 0x2d constructs check_pledge by writing only
    // its vftable. The two scalar base fields keep allocator state.
    CInstantZoneEntranceCondition() = default;

    explicit CInstantZoneEntranceCondition(EnterFailReason failReason) :
        bCheckReEnter(false), m_padding009{}, reason(failReason) {}

    EnterFailReason GetFailReason() const noexcept { return reason; }

    virtual bool CanEnter(CObject* target, const CInstantZone* instantZone, bool reenter, int bindingId) = 0;

protected:
    bool bCheckReEnter; // +0x08
    std::byte m_padding009[3]; // +0x09..+0x0b
    EnterFailReason reason; // +0x0c
};


class CInstantZoneEntranceCondition_check_level final : public CInstantZoneEntranceCondition {
public:
    explicit CInstantZoneEntranceCondition_check_level(bool checkReEnter) :
        CInstantZoneEntranceCondition(EFR_LIMIT_LEVEL), nMinLevel(0), nMaxLevel(0) {
        bCheckReEnter = checkReEnter;
    }

    bool CanEnter(CObject* target, const CInstantZone* instantZone, bool reenter, int bindingId) override;

    std::int32_t nMinLevel; // +0x10
    std::int32_t nMaxLevel; // +0x14
};

class CInstantZoneEntranceCondition_check_quest final : public CInstantZoneEntranceCondition {
public:
    explicit CInstantZoneEntranceCondition_check_quest(bool checkReEnter) :
        CInstantZoneEntranceCondition(EFR_LIMIT_QUEST), nQuestID(0) {
        bCheckReEnter = checkReEnter;
    }

    bool CanEnter(CObject* target, const CInstantZone* instantZone, bool reenter, int bindingId) override;

    std::int32_t nQuestID; // +0x10
};

class CInstantZoneEntranceCondition_check_pledge final : public CInstantZoneEntranceCondition {
public:
    CInstantZoneEntranceCondition_check_pledge() = default;

    bool CanEnter(CObject* target, const CInstantZone* instantZone, bool reenter, int bindingId) override;
};

class CInstantZoneEntranceCondition_check_party_chair final : public CInstantZoneEntranceCondition {
public:
    explicit CInstantZoneEntranceCondition_check_party_chair(bool checkReEnter) :
        CInstantZoneEntranceCondition(EFR_PARTY_CHAIR_FAIL) {
        bCheckReEnter = checkReEnter;
    }

    bool CanEnter(CObject* target, const CInstantZone* instantZone, bool reenter, int bindingId) override;

    std::vector<int> m_vChairGroupList; // +0x10
};

class CInstantZoneEntranceCondition_check_exclusive_point_inzone final : public CInstantZoneEntranceCondition {
public:
    enum IterationCycle : std::int32_t {
        Weekly = 0,
    };

    enum IterationDay : std::int32_t {
        Sun = 0,
        Mon = 1,
        Tue = 2,
        Wed = 3,
        Thu = 4,
        Fri = 5,
        Sat = 6,
    };

    explicit CInstantZoneEntranceCondition_check_exclusive_point_inzone(bool checkReEnter);

    int CalcIterationTime();
    bool CanEnter(CObject* target, const CInstantZone* instantZone, bool reenter, int bindingId) override;

    std::vector<int> m_vExclusiveList; // +0x10
    IterationCycle m_itCycle; // +0x30
    IterationDay m_itDayOfWeek; // +0x34
    std::int32_t m_itHour; // +0x38
    std::int32_t m_itMin; // +0x3c
    CYieldLock m_lockCycleTime; // +0x40
    std::int32_t m_nCycleTime; // +0x50
    std::byte m_padding054[4]; // +0x54..+0x57
};


struct EntranceInfo {
    std::int32_t count;
    std::int32_t maxCount;
};


class InfoForEntered final : public MemoryObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(InfoForEntered)

    struct LogInfo {
        std::int32_t accountId;
        std::byte padding004[4];
        std::u16string charName;
        std::u16string accountName;
    };

    InfoForEntered(
        bool associated,
        InZoneEnterReasonType reason,
        const EntranceInfo& myEntrance,
        const EntranceInfo& groupEntrance,
        User* user
    );
    ~InfoForEntered() override = default;

    static void* operator new(std::size_t requestedSize);
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

private:
    friend class CInstantZone;

    bool associated;
    bool binded;
    [[maybe_unused]] std::byte padding01a[2];
    InZoneEnterReasonType eReason;
    std::int32_t usedTime;
    EntranceInfo myEntranceInfo;
    EntranceInfo groupEntranceInfo;
    LogInfo logInfo;
};

class InfoForExisting final : public MemoryObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(InfoForExisting)

    InfoForExisting();
    ~InfoForExisting() override = default;

    static void* operator new(std::size_t requestedSize);
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

private:
    friend class CInstantZone;
    std::int32_t useTime;
    [[maybe_unused]] std::byte padding01c[4];
};


struct InstantZoneKey {
    std::int32_t typeId;
    std::int32_t clusterId;

    friend bool operator<(const InstantZoneKey& left, const InstantZoneKey& right) noexcept {
        return left.typeId < right.typeId || (left.typeId == right.typeId && left.clusterId < right.clusterId);
    }
};


class alignas(8) CInstantZoneConst {
public:

    // The target stores pointers to UTF-16 lexer allocations. wchar_t is
    // 16-bit on Windows but 32-bit on the portable host, so spelling this as
    // char16_t keeps both the current pointee contract and the 0x20
    // xvector envelope.
    using NameList = std::vector<char16_t*>;
    using EntranceConditionList = std::vector<CInstantZoneEntranceCondition*>;
    using ItemList = std::vector<std::pair<int, int>>;
    using VectorListType = std::vector<FVector>;

    CInstantZoneConst();
    CInstantZoneConst(const CInstantZoneConst& other);
    ~CInstantZoneConst();

    bool IsRemovingBuff() const { return m_removeBuff > 0; }

    bool CheckEndPosType(EndPositionType type, bool disorder);
    int GetLatesetResetBindingDay(int weekDay, int minuteOfDay);
    int GetNextResetBindingDay(int weekDay, int minuteOfDay);
    FVector GetStartPos(int index) const;
    FVector GetEndPos(User* user, const FVector& origin, bool disorder) const;

    int GetMaxUser() const noexcept { return m_nMaxUser; }
    int GetMinUser() const noexcept { return m_nMinUser; }
    int GetDuration() const noexcept { return m_nDuration; }
    int GetTimeLimit() const noexcept { return m_nTimeLimit; }
    int GetDeleteNoUserTime() const noexcept { return m_nDeleteNoUser; }
    bool IsSpawnNpc() const noexcept { return m_bSpawnNpc; }
    bool IsAssociative() const noexcept { return m_bIsAssociative; }
    bool IsToDoListCheck() const noexcept { return m_bToDoListCheck; }
    int GetBindingTiming() const noexcept { return m_bindingTiming; }
    int GetResetBindingTime() const noexcept { return m_resetBindingTime; }
    int GetPCCafePoint() const noexcept { return m_pccafePoint; }
    int GetClusterId() const noexcept { return m_clusterId; }
    CInstantZoneCluster* GetCluster() const noexcept { return m_instantZoneCluster; }
    CInstantZoneGroup* GetGroup() const noexcept { return m_instantZoneGroup; }
    unsigned int GetMaxParty() const noexcept { return m_MaxParty; }
    void SetMinUser(int value) noexcept { m_nMinUser = value; }
    void SetMaxUser(int value) noexcept { m_nMaxUser = value; }
    InZoneType GetZoneType() const noexcept { return m_InZoneType; }
    int GetLevelMax() const noexcept { return m_LevelMax; }
    int GetLevelMin() const noexcept { return m_LevelMin; }
    bool EntranceConditonEmpty() const noexcept { return m_vEntranceCond.empty(); }
    int GetToDayPriority() const noexcept { return m_ToDayPriority; }
    void SetToDayPriority(int value) noexcept { m_ToDayPriority = value; }
    void SetRemoveBuff(int value) noexcept { m_removeBuff = value; }

private:
    friend bool BuilderCmd_InZone(CUserSocket*, User*, char16_t*);
    friend class CInstantZone;
    friend class CInstantZoneCluster;
    friend class CInstantZoneDB;
    friend class CInstantZoneGroup;
    friend class CInstantZoneManager;
    friend class CInstantZoneNpcAIEventHandler;
    friend class CParserForInstantZone;
    friend class PledgeRaidSystem;

    NameList m_vDoorNameList; // +0x000
    NameList m_vAreaNameList; // +0x020
    EntranceConditionList m_vEntranceCond; // +0x040
    ItemList m_RequiredItemList; // +0x060
    ItemList m_DeletedItemList; // +0x080
    InZoneType m_InZoneType; // +0x0a0
    std::int32_t m_nTypeId; // +0x0a4
    std::int32_t m_nMaxUser; // +0x0a8
    std::int32_t m_nMinUser; // +0x0ac
    std::int32_t m_nDuration; // +0x0b0
    std::int32_t m_nTimeLimit; // +0x0b4
    std::int32_t m_nDeleteNoUser; // +0x0b8
    bool m_bSpawnNpc; // +0x0bc
    bool m_bIsAssociative; // +0x0bd
    bool m_bToDoListCheck; // +0x0be
    std::byte m_padding0bf; // +0x0bf
    std::int32_t m_ToDayPriority; // +0x0c0
    StartPositionType m_eStartPosType; // +0x0c4
    VectorListType m_vStartPos; // +0x0c8
    EndPositionType m_eOrderEndPosType; // +0x0e8
    EndPositionType m_eDisorderEndPosType; // +0x0ec
    VectorListType m_vOrderEndPos; // +0x0f0
    VectorListType m_vDisorderEndPos; // +0x110
    std::int32_t m_bindingTiming; // +0x130
    std::int32_t m_resetBindingTime; // +0x134
    std::int32_t m_resetBindingDay; // +0x138
    std::int32_t m_pccafePoint; // +0x13c
    std::int32_t m_removeBuff; // +0x140
    std::byte m_padding144[4]; // +0x144
    CInstantZoneGroup* m_instantZoneGroup; // +0x148
    std::int32_t m_clusterId; // +0x150
    std::byte m_padding154[4]; // +0x154
    CInstantZoneCluster* m_instantZoneCluster; // +0x158
    std::uint32_t m_MaxParty; // +0x160
    std::int32_t m_LevelMin; // +0x164
    std::int32_t m_LevelMax; // +0x168
    std::byte m_padding16c[4]; // +0x16c
};


class CInstantZone : public CGeneralObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CInstantZone)

    using CollisionCheckMap = ::std::map<CPlaneCollision*, bool>;
    using NameToCollisionMap = ::std::map<::std::u16string, CollisionCheckMap>;
    using AreaOnOffMap = ::std::map<CArea*, bool>;
    using AreaIndexMap = ::std::map<CArea*, std::int32_t>;
    using EventControllerMap = ::std::map<CArea*, CEventController*>;
    using ObjectPositionMap = ::std::map<std::uint32_t, ::FVector>;
    using DoorMap = ::std::map<::std::u16string, CDoor*, runtime::CStringLess<::std::u16string>>;
    using StaticObjectList = ::std::vector<CStaticObject*>;
    using UserKey = std::CDualKey<std::int32_t, std::int32_t>;
    using EnteredUserMap = ::std::map<UserKey, InfoForEntered*>;
    using ExistingUserMap = ::std::map<UserKey, InfoForExisting*>;

    // Current CInstantZone::EnterZoneWithCheck@0x007bae34 constructs two
    // adjacent target xvectors (0x20 bytes each): the related users and the
    // per-user reason selected by CheckEnterCondition.
    struct MemberListInfo {
        std::vector<User*> memberList;
        std::vector<InZoneEnterReasonType> pTypeList;
    };

    CInstantZone();
    CInstantZone(const CInstantZone& other);
    ~CInstantZone() override;

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()
    void TimerExpired(int id) override;
    CObject* Copy() override;
    bool IsInZone() override;
    CInstantZone* CastInZone() override;
    void OnCreate() override;

    virtual void Delete(InstantZoneDeleteInfo info);
    virtual int GetInstantZoneTypeID();

    bool CanBeCreated(CreateFailReason& reason);
    bool IsAssociatedUser(User* user);
    void Activate();
    void OnRelease();
    bool EnterZone(CItem* item);
    bool LeaveZone(CItem* item);
    bool EnterZone(CNPC* npc, bool setInZoneId);
    bool LeaveZone(CNPC* npc);
    bool EnterZone(
        User* user,
        FVector currentPosition,
        FVector destination,
        InZoneEnterType enterType,
        InZoneEnterReasonType enterReason,
        TeleportSourceType::Enum source
    );
    bool LeaveZone(User* user, bool sendMessage, int x, int y, int z, int heading);
    void EnterZone(CStaticObject& object);
    void InitNPC();
    void GetAllActiveUser(std::vector<User*>& users);
    void ExpelAllUser(bool sendMessage);
    int GetUserCount();
    int GetTimeLimitLeft(User* user, int currentTime, int bindingTime);
    FVector GetSavingPos(User* user);
    int GetInstantZoneDoorSize();
    CDoor* GetDoor(const char16_t* name);
    void SetCollisionEnabled(::std::u16string name, bool enabled);
    bool GetCollisionCheck(CPlaneCollision* collision);
    bool GetAreaIsOn(CArea* area);
    bool SetAreaOnOff(CArea* area, bool on);
    bool SetBannedTerritoryOnOff(CArea* area, int index, bool on);
    void SetAllDoorOpenClose(bool open);
    void DeleteNPCNotRelatedToPCFromCreatureMap();
    void DeleteNPCFromCreatureMap();
    InstantZoneKey GetKey() const noexcept {
        return InstantZoneKey{m_nTypeId, m_const == nullptr ? 0 : m_const->m_clusterId};
    }
    InZoneEnterReasonType GetEnterReason(User* user);
    bool GetRetPos(User& user, bool disorder, FVector& position);
    bool EnterZoneWithCheck(User* user, InZoneEnterType enterType, InstantZoneEnterFailInfo& failInfo, bool force);
    bool EnterZoneWithNoCheck(
        User* user,
        FVector currentPosition,
        FVector destination,
        InZoneEnterType enterType,
        InZoneEnterReasonType enterReason
    );
    bool LeaveZoneWithoutTeleport(User* user, FVector& returnPosition, bool disorder);
    void SetBinding(User* user, int bindingTime, InZoneBindingType bindingType);
    void SetBindingAllUser(InZoneBindingType bindingType);
    void SetBindingAllUserInPledge();
    void SetAssociate(User* user);
    void SetInzoneFlag(int flagId, int value);
    CInstantZoneConst* GetConst() { return m_const; }
    CPrivateContainer* GetContainer() { return m_pContainer; }

private:
    friend bool BuilderCmd_InZone(CUserSocket*, User*, char16_t*);
    friend class CInstantAgitManager;
    friend class CPledgeGameField;
    friend class CPledgeGameSystem;
    friend class CInstantZoneCluster;
    friend class CInstantZoneDB;
    friend class CInstantZoneGroup;
    friend class CInstantZoneManager;
    friend class CInstantZoneNpcAIEventHandler;
    friend class CParserForInstantZone;
    friend class CInstantZoneReleaseVerifier;
    friend class CInstantZoneEntranceCondition_check_exclusive_point_inzone;
    friend class WOlympiadUser;
    friend class WOlympiadMatch;
    friend class WOlympiadService;
    friend class CObject;
    friend class CCreature;
    friend class PledgeRaidSystem;
    friend class User;
    friend class CTLMFilter;
    friend bool NpcCastleGateOpenClose2(NpcSocket* socket, const unsigned char* packet);
    InZoneEnterReasonType CheckEnterConditionCommon(User* user, InstantZoneEnterFailInfo& failInfo, int bindingId);
    bool CheckEnterCondition(
        User* user, InZoneEnterType enterType, MemberListInfo& members, InstantZoneEnterFailInfo& failInfo, bool force
    );
    bool _GetRetPos(User* user, bool disorder, FVector& position);
    bool _DeleteUserFromMap(User& user);
    void _SetBinding(UserKey key, InfoForEntered* info, int bindingTime, InZoneBindingType bindingType);
    void BindUserOnTimerExpired();
    bool ExpelByNPCDownOnTimer();
    void SendRemainTimeMessage(int messageId, int minutes);
    CArea* GetAreaByIndex(int index);
    void InitArea();
    void InitDoor();
    void _ClearInstantZoneUserMap();
    void _ClearAllUserMap();
    void _DeleteNPCNotRelatedToPCFromCreatureMap();
    void _DeleteNPCFromCreatureMap();
    void _ClearInstantZoneCreatureMap();
    void _ClearInstantZoneItemMap();
    void _ClearInstantZoneDoorMap();
    void _ClearInstantZoneStaticObject();
    void _ClearAreaEventControllerMap();

    NameToCollisionMap m_mapNameToCollisionMap;
    AreaOnOffMap m_mapAreaOnOffFlag;
    AreaIndexMap m_mapAreaIndex;
    EventControllerMap m_mapEventControllerByArea;
    std::int32_t m_nTypeId;
    std::int32_t m_nContentFinishTick;
    InstantZoneStatus m_eStatus;
    ObjectPositionMap m_mapInstantZoneCreature;
    ObjectPositionMap m_mapInstantZoneItem;
    DoorMap m_mapInstantZoneDoor;
    StaticObjectList m_InstantZoneStaticObjectList;
    std::array<std::byte, 0x08> m_paddingAfterStaticObjectList;
    EnteredUserMap m_EnteredUserMap;
    ExistingUserMap m_ExistingUserMap;
    ::CYieldLock m_InstantZoneLock;
    std::int32_t m_nTickCount;
    std::int32_t m_nExpelTickByNPCDown;
    std::int32_t m_nDeleteNoUserTick;
    std::int32_t MAX_ZONE_COUNT;
    volatile std::int32_t m_nCurrentZoneCount;
    volatile std::int32_t m_nInstantZoneDeleteCount;
    CInstantZoneConst* m_const;
    std::int32_t m_extraDuration;
    std::int32_t m_Expiration;
    std::int32_t m_nInzoneDBId;
    CPrivateContainer* m_pContainer;

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CInstantZone)
};

class CInstantZoneReleaseVerifier : public CIOObject {
public:
    explicit CInstantZoneReleaseVerifier(CInstantZone* instantZone);
    ~CInstantZoneReleaseVerifier() override = default;

    void TimerExpired(int id) override;


private:
    CInstantZone* m_pInZone;
};


class CInstantZoneGroup {
public:
    using ZoneMap = std::map<int, CInstantZone*>;
    using TypeIdSet = std::set<int>;

    CInstantZoneGroup(int groupId, int maxEntrance);
    CInstantZoneGroup(const CInstantZoneGroup&) = default;
    ~CInstantZoneGroup() = default;

    int GetGroupId() const noexcept { return m_groupId; }
    void AddZone(int typeId);
    int GetMaxEntrance() const noexcept { return m_maxEntrance; }
    int GetExtraEntrace() const noexcept { return m_extraEntrance; }
    int GetTimeLimit() const noexcept { return m_timeLimit; }
    int GetLatesetResetBindingDay(int weekDay, int minuteOfDay);
    int GetNextResetBindingDay(int weekDay, int minuteOfDay);
    int GetResetBindingTime() const noexcept { return m_resetBindingTime; }
    int GetLatestBindingTime();
    InstantZoneBindingLevel IsAlreadyBoundUser(User* user, std::int32_t currentTime);
    InstantZoneBindingLevel _IsAlreadyBoundUser(User* user, std::int32_t currentTime);
    void GetInstantZoneGroupTypeIdList(TypeIdSet& typeIds);

private:
    friend class CInstantZoneDB;
    friend class CParserForInstantZone;

    std::int32_t m_groupId; // +0x00
    std::int32_t m_maxEntrance; // +0x04
    std::int32_t m_extraEntrance; // +0x08
    std::byte m_padding00c[4]; // +0x0c
    ZoneMap m_groupMap; // +0x10
    std::int32_t m_timeLimit; // +0x30
    std::int32_t m_resetBindingTime; // +0x34
    std::int32_t m_resetBindingDay; // +0x38
    std::byte m_padding03c[4]; // +0x3c
};


class CInstantZoneCluster {
public:
    using InstantZoneDensityType = std::pair<CInstantZone*, int>;
    using ClusterDensityMapType = std::map<int, InstantZoneDensityType>;

    explicit CInstantZoneCluster(int zoneTypeId);
    CInstantZoneCluster(const CInstantZoneCluster&) = default;
    ~CInstantZoneCluster() = default;

    int GetZoneTypeId() const noexcept { return m_zoneTypeId; }
    void AddZone(int clusterId, CInstantZone* zone);
    void SetGroup(CInstantZoneGroup* group);
    CInstantZone* GetZone(int clusterId);
    CInstantZone* GetLowestDensityZone();
    void AddDensity(int clusterId, int density);

private:
    friend class CInstantZoneDB;
    friend class CParserForInstantZone;

    std::int32_t m_zoneTypeId; // +0x00
    std::byte m_padding004[4]; // +0x04
    ClusterDensityMapType m_clusteredMap; // +0x08
};


class CInstantZoneDB {
public:
    using ZoneMap = std::map<int, CInstantZone*>;
    using NameMap = std::map<std::u16string, int>;
    using ToDoListMap = std::map<std::u16string, int>;
    using GroupMap = std::map<int, CInstantZoneGroup*>;
    using ClusterMap = std::map<int, CInstantZoneCluster*>;
    using TypeIdSet = std::set<int>;

    CInstantZoneDB();
    CInstantZoneDB(const CInstantZoneDB&) = default;
    ~CInstantZoneDB();

    void Load();
    void PostLoad();
    CInstantZone* GetInstantZone(int typeId, int clusterId);
    CInstantZone* GetInstantZone(const wchar_t* name);
    CInstantZone* GetInstantZone(int typeId);
    CInstantZone* GetPledgeRaidInstantZone();
    CInstantZone* GetTypicalInstantZone(int typeId);
    int GetInstantZoneTypeId(const wchar_t* name);
    CInstantZoneGroup* GetInstantZoneGroup(int groupId);
    CInstantZoneCluster* GetCluster(int zoneTypeId);
    bool CheckAreaEnterInzone(int typeId);
    void SendToToList(User* user, bool entering);
    void MakeAssembleNameAndLevel(char* buffer, int bufferSize, unsigned int& length, short& count, User* user);
    void GetInstantZoneGroupTypeIdList(int groupId, TypeIdSet& typeIds);
    std::size_t GetInstantZoneToDoListMapCount() const noexcept { return m_InstantZoneToDoListMap.size(); }
    void ReportInstantZoneStats(void* argument);
    void ReportInstantZoneStats();

private:
    friend class CParserForInstantZone;

    void AddZone(CInstantZone* zone, const wchar_t* name);
    void AddCluster(CInstantZoneCluster* cluster);
    void AddGroup(CInstantZoneGroup* group);

    ZoneMap m_mapInstantZones; // +0x00
    NameMap m_mapInstantZoneStrMap; // +0x20
    ToDoListMap m_InstantZoneToDoListMap; // +0x40
    GroupMap m_mapInstantZoneGroup; // +0x60
    ClusterMap m_mapInstantZoneCluster; // +0x80
};

extern CInstantZoneDB g_InstantZoneDB;


// Current L2Server.exe 0x007bb098 / vftable 0x0100bd18. The current
// xvector carrier is 0x20 bytes, which shifts the ten target-wchar HTML
// buffers by eight bytes from the TotalChecker donor and makes the complete
// current object 0x14d0 instead of 0x14c8.
class CPremiumInstantRankingSystem : public CIOObject {
public:
    enum KamarokaStatus : std::int32_t {
        KS_NONE = 0,
        KS_RAID = 1,
        KS_GIVE_REWARD = 2,
        KS_END = 3,
    };

    struct PremiumInstanceRankInfo {
        std::int32_t nPartyId;
        std::int32_t nRank;
        std::int32_t nScore;
        std::int32_t m_alignment00c;
        std::u16string wsMemberList;
    };

    static constexpr int TEN_MINUTES = 600;

    ~CPremiumInstantRankingSystem() override;

    static CPremiumInstantRankingSystem* m_RankingSystem;
    static CPremiumInstantRankingSystem& Inst();

    void Init();
    void LoadFromDB();
    void TimerExpired(int id) override;
    bool IsRewardDay();
    bool IsRaidDay();
    void RequestRegisterNewRecord(int score, User* user);
    void RequestRegisterNewRecord(int score, CParty* party);
    void RequestUserRankingInfo(User* user);
    void ReplyRankingList(const unsigned char* packet);
    void ReplyAddNewRecord(const unsigned char* packet);
    void LineUpRanking();
    void ShowRankingList(User* user, int page);
    void ShowUserRankingInfo(User* user, const unsigned char* packet);
    void InitStatus();
    void SetRound(int round);
    void AddNewRecord(int partyId, int rank, int score, const char16_t* memberList);
    int GetRankListIndex(int rank);
    int GetRank(int partyId);
    void GetRankString(int rank, std::u16string& score, std::u16string& party);
    int GetRound() const noexcept { return m_nRound; }
    static bool SortRankByScore(PremiumInstanceRankInfo left, PremiumInstanceRankInfo right);
    bool IsRaidTime(unsigned short day);
    bool IsRaidTime();
    bool IsRewardTime();
    void SetStatusRaid();
    void SetStatusGiveReward();
    void ClearRankingList();
    void AddNextRound();
    void CheckKamarokaCycle();
    void SetKamarokaCycle(int cycle[7]);

private:
    CPremiumInstantRankingSystem();

    KamarokaStatus m_nStatus; // +0x18
    std::int32_t m_nMaxRank; // +0x1c
    RWLock mRankingLock; // +0x20
    std::int32_t m_nRound; // +0x58
    [[maybe_unused]] std::int32_t m_alignment05c; // +0x5c
    std::vector<PremiumInstanceRankInfo> m_vRankList; // +0x60
    char16_t m_pszHTMLFileName[10][260]; // +0x80
};


class CInstantZoneManager : public CIOObject {
public:
    using PartyMap = std::map<InstantZoneKey, std::uint32_t>;
    using InzoneFlagKey = std::CDualKey<std::int32_t, std::int32_t>;
    using FlagMap = std::map<int, int>;
    using InzoneFlagMap = std::map<InzoneFlagKey, FlagMap>;
    using DeleteInzoneList = std::vector<int>;

    CInstantZoneManager();
    ~CInstantZoneManager() override;
    void TimerExpired(int id) override;

    void LoadInzoneMaxDBID();
    void SetInzoneDBIndex(CInstantZone* zone);
    bool BeInstantZoneCreate(User* user);
    void FinishInstantZone(CInstantZone* zone);
    void AddExtraDuration(CInstantZone* zone, int duration);
    void LoadInzoneFlag(int userDbId, int zoneTypeId);
    void InitInZoneFlag(int userDbId, int& count, CPacketHelperEx<20480>& packet);
    void BuilderViewFlag(int userDbId, User* builder);
    void GetRimRank();
    void DeleteInstantZone(CInstantZone& zone, InstantZoneDeleteInfo info);
    void SendErrMessageForGettingZone(std::vector<User*> users, CreateFailReason reason);
    void SendErrMessageForEnteringZone(std::vector<User*> users, InstantZoneEnterFailInfo& info);
    void DeleteInzoneAgit();
    CInstantZone* GetInstantZoneTemplate(int typeId);
    bool SetMaxZoneCount(int typeId, int maxZoneCount);
    CInstantZone* CreateInstantZone(int typeId, CreateFailReason& reason);
    CInstantZone* CreateInstantZone(wchar_t* name, CreateFailReason& reason);
    CInstantZone* GetInstantZoneToEnter(User* user, InZoneEnterType enterType, int typeId, CreateFailReason& reason);
    void AddInzonePartyInfo(int partyId, int clusterId);
    bool ChangeInstantZone(User* user, CInstantZone* zone, InZoneEnterType enterType, int reason, bool force);
    void LeaveInstantZone(User* user, CInstantZone* zone, int arg1, int arg2, int arg3);
    void SetInstantZoneBinding(CInstantZone* zone, InZoneBindingType bindingType);
    bool EnterInstantZone(
        User* user,
        CInstantZone* zone,
        InZoneEnterType enterType,
        int reason,
        bool force,
        int bindingId,
        TeleportSourceType::Enum source
    );
    void SetDeleteInzoneAgit(int zoneSid);
    void GetRelatedMember(User* user, InZoneEnterType enterType, std::vector<User*>& members);
    void DeleteInstantZoneByBuilder(CInstantZone& zone);
    void SetFlagList(int userDbId, int typeId, int flagId, int value, bool save);
    void SetInstantZoneFlag(int userDbId, int typeId, int count, const unsigned char* packet);
    void SetValidFlagList(User* user, CInstantZone* zone);
    bool EnterInstantZone(User* user, InZoneEnterType enterType, int typeId, int& result, int reason, bool force);

private:
    friend bool ReplyLoadInzoneMaxDBID(CDBSocket* socket, const unsigned char* packet);

    CYieldLock m_Lock; // +0x18
    PartyMap m_PartyMap; // +0x28
    InzoneFlagMap m_InzoneFlagMap; // +0x48
    DeleteInzoneList m_nDeleteInzoneList; // +0x68
    volatile std::int32_t m_nMaxInzoneDBID; // +0x88
    std::byte m_padding08c[4]; // +0x8c
};

extern CInstantZoneManager g_InstantZoneMgr;
