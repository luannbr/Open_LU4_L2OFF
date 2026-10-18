#pragma once

// use the main protocol constants directly.
#include "Protocol/src/L2Constants.h"

#include "Shared/src/BaseStructure.h"
#include "Shared/src/ReuseDelaySaveReason.h"
#include "Shared/src/SSQ.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/Utils.h"
#include "Shared/src/Vitality.h"
#include "Shared/src/Obj.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

class CWareHouse;
class CSocket;
class CAdminSocket;
class CUser;
bool CheckPCBangCharPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool CheckFreeUserPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool MoveItemProc(
    CAdminSocket* socket,
    int replyOpcode,
    int sourceCharacterId,
    int itemId,
    std::int64_t amount,
    int destinationCharacterId,
    int destinationWarehouse,
    const wchar_t* auditText
);
bool DelMacroPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool AddMacroPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool AddMacroInfoPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool ModifyDepositedSSQItemPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool SetOnetimeQuestPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool SetQuestPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool DelQuestPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool SetQuest2Packet(CAdminSocket* socket, const std::uint8_t* packet);
bool DelRecipePacket(CAdminSocket* socket, const std::uint8_t* packet);
bool GmRegPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool MoveCharacterPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool RestoreCharPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool PledgeOustPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestUserRaceSex(CSocket* socket, const unsigned char* packet);
bool RequestItemCheck(CSocket* socket, const unsigned char* packet);
class CPledgeDb;
class CUserDb;
class CUserDbHelper;
class CPledgeMemberListPacketAssembler;
class DxSetTicketBuyCount;
class DxAddSevenSignQuestMember;
class DxDeleteDepositedSevenSignQuestItem;
class DxDepositSevenSignQuestItemEx;
class DxChangeSSQUserPoint;
class DxDismissSevenSignMember;
struct PackageTargetListAssembler;
enum AddtionalLocationType : int;
enum PledgeType : int;
enum SlotType : std::int32_t;
namespace UserBookmarkJobHandler {
void OnAddSlot(int characterId, int slotCount);
}
namespace RQExHandler {
bool RequestSaveUserJobGroup(CSocket* socket, const unsigned char* packet);
bool RequestBuyBeautyItem(CSocket* socket, const unsigned char* packet);
bool RequestResetBeautyItem(CSocket* socket, const unsigned char* packet);
bool RequestUpdateHairFlag(CSocket* socket, const unsigned char* packet);
bool RequestAcquirePotentialSkill(CSocket* socket, const unsigned char* packet);
bool RequestResetPotentialSkill(CSocket* socket, const unsigned char* packet);
bool RequestBrStartTrainingZone(CSocket* socket, const unsigned char* packet);
bool RequestBrEndTrainingZone(CSocket* socket, const unsigned char* packet);
bool RequestBrLoadTrainingZone(CSocket* socket, const unsigned char* packet);
bool RequestLoadDroidNPCAppearance(CSocket* socket, const unsigned char* packet);
} // namespace RQExHandler
namespace AQHandler {
bool RequestAddDualClass(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestResetInzoneRestrict(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestAddRecipeListPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestAddShortcutListPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestAddActiveSkill(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestCheckRaidUser(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestResetAbilityPoint(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestResetWorldOlympiadPoint(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestModifyRaidPoint(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestModifyWorldchatPoint(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestAddAbilitySkill(CAdminSocket* socket, const std::uint8_t* packet);
bool RequestAddAbilityPoint(CAdminSocket* socket, const std::uint8_t* packet);
} // namespace AQHandler
namespace PeriodicPremiumHennaJobHandler {
void OnLoadInfo(CSocket* socket, int characterId, int userSocketId);
}
using CWareHouseSP = CSPointer<CWareHouse>;
using CUserSP = CSPointer<class CUser>;

// Equipment fields use packet order. Composite slots are not stored here.
struct UserEquipmentSlots {
    int underwear = 0;
    int rightEar = 0;
    int leftEar = 0;
    int neck = 0;
    int rightFinger = 0;
    int leftFinger = 0;
    int head = 0;
    int rightHand = 0;
    int leftHand = 0;
    int gloves = 0;
    int chest = 0;
    int legs = 0;
    int feet = 0;
    int back = 0;
    int bothHand = 0;
    int hair = 0;
    int hair2 = 0;
    int rightBracelet = 0;
    int leftBracelet = 0;
    int agathionMain = 0;
    std::array<int, 4> agathionSub{};
    std::array<int, 6> deco{};
    int waist = 0;
    int brooch = 0;
    std::array<int, 6> jewel{};
    int artifactBook = 0;
    std::array<int, 12> artifactA{};
    std::array<int, 3> artifactB{};
    std::array<int, 3> artifactC{};
    std::array<int, 3> artifactD{};
    // std::array<int, 6> deco{}
    // int waist = 0
    // int brooch = 0
    // std::array<int, 6> jewel{}
};


// Current CUser::Set/LoadActiveSkillFromDB store 96 consecutive 0x10-byte
// records at target offset +0x660. The fifth result column was added after
// the surviving donor SQL/PDB. Current machine code stores it in the second
// 16-bit member and therefore makes the current layout authoritative.
struct AbnormalStatus {
    int skillId = -1;
    std::int16_t skillLevel = 0;
    std::int16_t skillSubLevel = 0;
    int skillDuration = 0;
    int skillSelfCast = 0;
};


struct UserDeletionTime {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
};

// Current lin_LoadSSQUserInfo result state. The database state has five
// int32 values, one int64 point, two three-element int64 count families, and
// the final int32 ticket count at CUser +0x24ec..+0x2538. The selected-user
// wire section omits joinTime.
struct UserSSQInfo {
    int roundNumber = 0;
    int joinTime = 0;
    int part = 0;
    int position = 0;
    int sealSelectionNumber = 0;
    std::int64_t point = 0;
    std::array<std::int64_t, 3> twilightItemCount{};
    std::array<std::int64_t, 3> dawnItemCount{};
    int ticketBuyCount = 0;
};

// Current lin_GetNoblessById result order and CUser +0x5f0..+0x608.
// The public query methods reload this block on every call,
// matching Cached.exe 0x005b1768 instead of treating it as a cache.
struct UserNoblessInfo {
    int noblessType = 0;
    // int heroType = 0.
    int heroType = HERO_NONE;
    int winCount = 0;
    int previousPoint = 0;
    int olympiadPoint = 0;
    int matchCount = 0;
    int olympiadWinCount = 0;
};

// Semantic replacement for the target's 109-argument public constructor.
// Every member is selected by MakeNewUserObject 0x005c036c. Fields applied by
// the later LoadUserInfo gate are absent.
struct UserInitialState {
    std::u16string characterName;
    std::u16string accountName;
    std::uint32_t characterId = 0;
    std::int32_t accountId = -1;

    std::uint32_t pledgeId = 0;
    int builder = 0;
    int gender = 0;
    int race = 0;
    int characterClass = 0;
    int worldId = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    bool inVehicle = false;

    double hp = 0.0;
    double mp = 0.0;
    double maxHp = 0.0;
    double maxMp = 0.0;
    double cp = 0.0;
    double maxCp = 0.0;
    std::int64_t sp = 0;
    std::int64_t experience = 0;
    int level = 0;
    std::int64_t highestExperience = 0;
    int highestLevel = 0;
    int alignment = 0;
    std::uint32_t pkCount = 0;
    std::uint32_t duelCount = 0;
    std::uint32_t pkPardon = 0;
    int abilityPoints = 0;

    UserEquipmentSlots equipment;
    UserDeletionTime deletionTime;
    int face = 0;
    int hairShape = 0;
    int hairColor = 0;
    std::u16string nickname;
    std::array<std::uint8_t, 0x20> powerFlags{};

    std::uint32_t pledgeDismissTime = 0;
    std::uint32_t pledgeOustedTime = 0;
    std::uint32_t pledgeWithdrawTime = 0;

    // Column 105 is still bound and passed by the current EXE, but public
    // constructor 0x005be7c4 does not forward it to SetAtConstructor.
    std::uint32_t ignoredSurrenderWarId = 0;
    std::uint32_t useTime = 0;

    int subjobId = 0;
    std::array<int, 4> subjobClass{};
    int dualClassId = 0;
    int ssqDawnRound = 0;
    int characterNameColor = 0;
    int nicknameColor = 0;
    int pledgeType = 0;
    int gradeId = 0;
    int academyPledgeId = 0;
};

struct UserLoadedInfo {
    int status = 0;
    int banEnd = 0;
    int serviceFlag = 0;
    // Current CUser+0x18 is the exact Shared
    // CBitStorage<2048,int,64> one-time quest bitmap.
    CBitStorage<2048, std::int32_t, 64> questFlags;
    // Current User.obj instantiates CBitStorage<1024,int,32> for the exact
    // 0x80-byte tutorial bitmap at target +0x118.
    CBitStorage<1024, std::int32_t, 32> tutorialFlags;
    std::uint32_t associatedInzone = 0;
    int associatedInzoneUid = 0;
    int bookmarkSlot = 0;
    int jobGroup = -1;
};

using UserSkillState = std::pair<int, int>;
using UserSkillMap = std::map<int, UserSkillState>;

// Shipping owner: CacheD/User.{h,cpp}. Host-native strings and grouped state
// replace only the private MSVC object layout. Identity, warehouse ownership,
// lock points and database-visible state follow current Cached.exe.
class CUser : public MemoryObject {
public:
    // The current PDB keeps the nested type but not its enumerator
    // identifiers. Machine-code call sites establish the observed values.
    enum INCLUDE_WAREHOUSE : int;

    // Current DAT_0083d5d0. The older C1 owner keeps the original static
    // identifier, while current WriteLogin/WriteLogout fix its
    // update and logging behavior.
    static std::atomic<int> s_loggedUsers;

    explicit CUser(const UserInitialState& state);
    ~CUser() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    static bool CheckCharacterName(const wchar_t* characterName, int country, bool allowLongName);

    std::uint32_t GetId() const noexcept;
    std::int32_t GetAccountID() const noexcept;
    int GetWorld() const noexcept;
    const char16_t* GetCharName() const noexcept;
    const char16_t* GetCharNameLow() const noexcept;
    const char16_t* GetAccountName() const noexcept;
    int GetSubjobId() const noexcept;
    int GetDualClassId() const noexcept;
    std::uint32_t GetPledgeID() const noexcept;
    int GetAllianceID();
    PledgeType GetPledgeType() const noexcept;
    int GetBuilder() const noexcept;
    int GetLevel() const noexcept;
    static int GetLevelByExp(std::int64_t experience);
    int GetGender() const noexcept;
    int GetRace() const noexcept;
    int GetClass() const noexcept;
    int GetGradeId() const noexcept;
    int GetX() const noexcept;
    int GetY() const noexcept;
    int GetZ() const noexcept;
    double GetHP() const noexcept;
    double GetMP() const noexcept;
    double GetMaxHP() const noexcept;
    double GetMaxMP() const noexcept;
    double GetCP() const noexcept;
    double GetMaxCP() const noexcept;
    std::int64_t GetSP() const noexcept;
    void SetSP(std::int64_t sp) noexcept;
    int GetAP() const noexcept;
    std::int64_t GetExp() const noexcept;
    std::int64_t GetHighestExperience() const noexcept;
    int GetHighestLevel() const noexcept;
    int GetAlign() const noexcept;
    std::uint32_t GetPK() const noexcept;
    std::uint32_t GetDuel() const noexcept;
    std::uint32_t GetPKPardon() const noexcept;
    // The current tiny body is unsymbolized. This semantic name is kept
    // with its address in User.cpp instead of pretending PDB name evidence.
    bool IsCurrentSSQRound() const;
    int GetMainClass() const noexcept;
    const std::array<int, 4>& GetSubjobClasses() const noexcept;
    const UserEquipmentSlots& GetEquipment() const noexcept;
    int GetFaceIndex() const noexcept;
    int GetHairShapeIndex() const noexcept;
    int GetHairColorIndex() const noexcept;
    int GetFaceIndexEx() const noexcept;
    int GetHairShapeIndexEx() const noexcept;
    int GetHairColorIndexEx() const noexcept;
    bool GetHairAccessoryFlag() const noexcept;
    int GetPreviousServer() const noexcept;
    const char16_t* GetNameOrigin() const noexcept;
    int GetJobGroup() const noexcept;
    int GetStatus() const noexcept;
    void SetStatus(int status) noexcept;
    int GetBanEnd() const noexcept;
    void SetBanEnd(int banEnd) noexcept;
    int GetServiceFlag() const noexcept;
    int GetNoblessType() const noexcept;
    int GetHeroType() const noexcept;
    int GetDeletedElapsedSecond() const;
    std::int32_t GetAccountLastLogoutTime();
    const SYSTEMTIME& GetLastLogoutTime() const noexcept;
    const SYSTEMTIME& GetLastLoginTime() const noexcept;
    SYSTEMTIME* GetBirthday() noexcept;
    const SYSTEMTIME* GetBirthday() const noexcept;
    bool IsLoggedIn() const noexcept;
    void PrintCharData();
    void PrintCharWarehouseData();
    bool Delete();
    bool SetDeleted();
    bool RestoreDeleted();

    void LoadUserNobless();
    bool IsNobless();
    bool IsNoblessOrHonors();
    bool IsHero();

    int GetEventData(int eventId);
    void SetEventData(int eventId, int eventTime);

    void ReadLock(const wchar_t* file, int line);
    void ReadUnlock();
    void WriteLock(const wchar_t* file, int line);
    void WriteUnlock();
    void SetAddr(int first, int second, int third, int fourth);
    void SetPos(int x, int y, int z);
    std::u16string GetMacAddressString() const;
    void SetPledge(int pledgeId, PledgeType pledgeType);
    void SetLoggedIn(bool loggedIn);
    void WriteLogin(int characterId);
    void WriteLogout(int characterId, int logoutReason);
    void SetEquip(SlotType slotType, int itemId);
    void SetChanged(
        std::uint32_t pledgeId,
        int builder,
        int gender,
        int race,
        int characterClass,
        int x,
        int y,
        int z,
        double hp,
        double mp,
        double maxHp,
        double maxMp,
        std::int64_t sp,
        std::int64_t experience,
        int level,
        int alignment,
        std::uint32_t pkCount,
        std::uint32_t duelCount,
        std::uint32_t pkPardon,
        int hairColor,
        int hairShape,
        int face,
        int inVehicle,
        int subjobId,
        int subjobClass0,
        int subjobClass1,
        int subjobClass2,
        int subjobClass3,
        int dualClassId,
        double cp,
        double maxCp,
        int ssqDawnRound,
        int abilityPoints,
        bool alreadyLocked
    );
    bool Save();
    bool SaveAP(int abilityPoints);
    bool SaveSlot();
    void SaveSummonInfo(
        int summonClassId,
        int summonDbId,
        int summonLevel,
        int summonOriginalRemainTime,
        int summonRemainTime,
        int summonExperienceRate,
        double summonHp,
        double summonMp,
        int summonConsumeItemId,
        int summonConsumeItemCount,
        int summonIsSummoned
    );
    bool SaveSSQDawnRound(int dawnRound);
    void ReloadHighestExpAndLevel();
    bool AddUserLog(int logId, int logFrom, int logTo, std::uint32_t useTime, int sessionUseTime);
    bool AddUserLog2(int logId, int logFrom, int logTo, std::uint32_t useTime, int subjobId);
    bool ModChar2Extern(int gender, int race, int characterClass, int face, int hairShape, int hairColor, int jobGroup);
    bool ModCharSubJobClass(int characterClass, int subjobId);
    bool ModCharPledgeExtern(wchar_t* nickname);
    bool ModCharExtern(
        std::int64_t sp,
        std::int64_t experience,
        int alignment,
        int pkCount,
        std::uint32_t pkPardon,
        std::uint32_t duelCount,
        int abilityPoints
    );
    bool ModCharSubJobAbilityAbsolutely(std::int64_t sp, std::int64_t experience, int subjobId, int abilityPoints);
    bool ModChar2Extern(
        int gender, int race, int characterClass, int face, int hairShape, int hairColor, int jobGroup, int subjobId
    );
    bool ModChar3Extern(
        std::int64_t spDelta,
        std::int64_t experienceDelta,
        int alignmentDelta,
        int pkCountDelta,
        int pkPardonDelta,
        int duelCountDelta,
        int abilityPointDelta
    );
    bool ModCharSubJobAbility(std::int64_t spDelta, std::int64_t experienceDelta, int subjobId, int abilityPointDelta);
    bool ModCharExtern(
        std::int64_t sp,
        std::int64_t experience,
        int alignment,
        int pkCount,
        std::uint32_t pkPardon,
        std::uint32_t duelCount,
        int subjobId,
        int abilityPoints
    );
    bool ModChar3Extern(
        std::int64_t spDelta,
        std::int64_t experienceDelta,
        int alignmentDelta,
        int pkCountDelta,
        int pkPardonDelta,
        int duelCountDelta,
        int subjobId,
        int abilityPointDelta
    );
    int CreateSubJob(
        int newSubjobId,
        int newClass,
        int oldSubjobId,
        double hp,
        double mp,
        std::int64_t sp,
        std::int64_t experience,
        int level,
        int henna1,
        int henna2,
        int henna3,
        int abilityPoints
    );
    int CreateDualJob(
        int newSubjobId,
        int newClass,
        int oldSubjobId,
        double hp,
        double mp,
        std::int64_t sp,
        std::int64_t experience,
        int level,
        int henna1,
        int henna2,
        int henna3,
        int abilityPoints
    );
    int CancelDualClass();
    bool RenewSubJob(
        int subjobId,
        int subjobType,
        int newClass,
        int oldSubjobId,
        double hp,
        double mp,
        std::int64_t sp,
        std::int64_t experience,
        int level,
        int henna1,
        int henna2,
        int henna3,
        int abilityPoints
    );
    bool DeleteSubJob(int subjobId);
    bool ChangeSubJob(
        int newSubjobId,
        int oldSubjobId,
        double* hp,
        double* mp,
        std::int64_t* sp,
        std::int64_t* experience,
        int* level,
        int* henna1,
        int* henna2,
        int* henna3,
        int& oldClass,
        int& abilityPoints
    );
    bool ChangeSubJob(int subjobId);
    int GetSubJobListBuffer(char* buffer, int* bufferSize);
    bool AddSubJob(
        int newClass,
        double hp,
        double mp,
        std::int64_t sp,
        std::int64_t experience,
        int level,
        int subjobId,
        int henna1,
        int henna2,
        int henna3
    );
    bool SaveForSubJob(
        int subjobId,
        int characterClass,
        int dualClassId,
        double hp,
        double mp,
        double maxHp,
        double maxMp,
        double cp,
        double maxCp,
        std::int64_t sp,
        std::int64_t experience,
        int level,
        int henna1,
        int henna2,
        int henna3,
        int abilityPoints
    );
    int GetUserLog(int logId, int logTo);
    void RegisterInitialShortcut();
    void SaveShortcut(int subjobId, int slot, int type, int id, wchar_t* macro);

    bool OnRequestEvolvePet(CSocket* socket, const std::uint8_t* packet, unsigned int requestId);
    int LoadPetItemCount();

    void SetCheckSumOnDB();
    bool ItemcheckOnDB();
    bool ItemCheckBetweenMemoryAndDB();
    void SetLoadTime();
    void SetUnloadTime();
    void ItemCheckOnLoad();
    void ItemCheckOnUnload();

    CWareHouseSP GetWareHouse(int warehouseType, bool loadItems = true);
    CWareHouseSP GetWareHouseForRaid(int warehouseType);
    bool IsLoadedWareHouse(int warehouseType);
    int GetItemWare(int itemId);
    int DelItem(int itemId);
    bool SetSeizedItemOwner(int itemId, int newOwnerId, CWareHouse* destinationWarehouse);
    bool SetSeizedItemOwner(int itemId, std::int64_t amount, int newOwnerId, CWareHouseSP destinationWarehouse);
    bool GetReloadFlag();
    void SetReloadFlag(bool reloadFlag);
    void SaveTutorial();
    void SendTutorial(CSocket* socket, int userSocketId);
    void SetTutorial(int tutorialId);
    bool SetSelectedQuest(int questId, int questState);
    bool SetSelectedQuest(int questId, int questState, int questState2, int questState3, int questState4);
    bool DeleteSelectedQuest(int questId);
    void SetOneTimeQuest(int questId, bool enabled);
    bool DeleteSelectedNRQuest(int questId);
    void SaveOneTimeQuest();
    void SaveMemo();
    void SetQuest(const std::uint8_t* packet, int characterId);
    void LoadQuest(CSocket* socket, int characterId, unsigned int userSocketId);
    void LoadOneDayReward(CSocket* socket, unsigned int userSocketId);
    void LoadItemReuseDelay(CSocket* socket, unsigned int userSocketId);
    bool SetItemReuseDelay(int reuseKey, int reuseEndTime);
    bool CharAudit(std::int64_t adenaSum, std::int64_t defaultPriceSum, std::int64_t itemSum);
    int GetPlayDiff();
    std::int64_t GetExpDiff();
    void IncreasePKCount();
    void ResetPKCountWhenCursedPCDied(int originalPk);
    void IncreaseDuelCount();
    void LoadActiveSkillFromDB();
    void LoadBeautyData();
    bool SetDailyQuest(int questId, long completeDate);
    bool SetBirthday(SYSTEMTIME* birthday);
    bool SaveBirthdayGift(SYSTEMTIME* birthday);
    bool LoadBirthInfo(CSocket* socket);
    bool LoadLastBirthdayGiftDate(CSocket* socket);
    bool Load5YearInfo(CSocket* socket);
    bool Save5YearGift(SYSTEMTIME* birthday);
    bool SaveDuelRecord(
        int individualWin, int individualLose, int individualDraw, int partyWin, int partyLose, int partyDraw
    );
    bool LoadDuelRecord(CSocket* socket);
    void LoadMergedInfo();
    void UpdateBossRecord(int bossId, int record);
    void LoadBossRecord(CSocket* socket);
    int LoadPCCafePoint();
    bool SavePCCafePoint(int point);
    bool SaveHomePCCafeCount(int point);
    bool ResetHomePCCafeCount();
    int LoadHomePCCafeCount();
    void SaveAbilitySkill(int subjobId, int potentialType, int skillId, int skillLevel);
    void SaveAbilityPoint(int subjobId, int typeOnePoint, int typeTwoPoint, int typeThreePoint);
    int LoadUserPoint(int pointType);
    bool SaveUserPoint(int pointType, int point);
    bool SavePVPPointRestrain(int giveCount, int giveTime);
    bool LoadPVPPointRestrain(CSocket* socket);
    bool SaveLastColor(int colorIndex, int savedTime);
    bool LoadLastColor(CSocket* socket);
    void LoadBotReport();
    void ResetBotReport();
    void SetBotReport(int reportPoint);
    void SaveCaptchaConfirm(int confirmTimes, int confirmSeconds, int delaySeconds);
    void SaveBotReport(CSocket* socket);
    int GetBotReport();
    int LoadBotReportTime();
    void DecayBotReport(CSocket* socket);
    int LoadTimeToGiveKarmaPoint();
    void UpdateTimeToGiveKarmaPoint(int timeToGiveKarmaPoint);
    bool ResetInzoneOnDB(int inzoneId);
    int LoadWorldChatPoint(int maximumCount);
    bool UpdateWorldChatPoint(int point);
    void IncWorldChatPoint(int increment);
    bool LoadMatchingPoint();
    bool SetMatchingPoint(MatchingPoint::MATCHINGPOINT_TYPE type, std::int64_t point, std::int64_t& previousPoint);
    bool SaveMatchingPoint();
    std::int64_t GetMatchingPoint(MatchingPoint::MATCHINGPOINT_TYPE type);
    bool LoadHonorsSkill();
    bool SetHonorsSkill(int skillId, int skillLevel);
    bool ResetHonorsSkill();
    bool DeleteHonorsSkill(int skillId);
    int GetHonorsSkillLevel(int skillId);
    bool SendHonorsSkillList(CSocket* socket);
    bool LoadHennaInfo();
    bool SaveHennaInfo();
    bool AddHenna(int hennaId);
    bool AddSubJobHenna(int hennaId, int subjobId);
    bool DelHenna(int hennaId);
    bool DelSubJobHenna(int hennaId, int subjobId);
    bool AddHenna(int hennaId, int subjobId);
    bool DelHenna(int hennaId, int subjobId);
    bool SaveHennaPremiumInfo(int startTime, int period);
    bool AddHennaPremium(int hennaId, int startTime, int period);
    bool DelHennaPremium(int hennaId);
    void DeleteUserMacro(int macroId);
    void AddUserMacro(
        int characterId, wchar_t* macroName, wchar_t* macroTooltip, wchar_t* macroIconName, int macroIconType, int* macroId
    );
    void AddUserMacroInfo(int macroId, int macroOrder, int macroInt1, int macroInt2, int macroInt3, wchar_t* macroText);
    void SetSSQPoint(std::int64_t point);
    bool SetDepositedSSQItemCount(SSQPart::type part, SSQItem::type itemType, std::int64_t count);
    bool SaveSSQInfo();
    bool LoadSSQInfo();
    int AssembleSSQInfo(char* buffer, int bufferSize);
    bool SendRecipeBookInfo(int userServerId, CSocket* socket);
    bool SaveRecipeBookInfo(int recipeId);
    bool SaveRecipeBookInfo(int recipeId, int userServerId, CSocket* socket);
    bool DeleteRecipeBookInfo(int recipeId);
    bool DeleteRecipeBookInfo(int recipeId, int userServerId, CSocket* socket);
    bool LoadRecipeInfo();
    bool ChangeLocationExtern(const char16_t* characterName, int x, int y, int z, bool save);
    bool SetBuilderLevExtern(const char16_t* characterName, int builderLevel);
    bool ChangeCharacternameExtern(const char16_t* characterName);
    bool LoadAquireSkill(int nSubJobId);
    void SendReloadSkill(CSocket* socket, unsigned int userSocketId, int nSubJobId, bool trailingFlag, bool byteFlag);
    void ResetAcquiresSkillsFromBuffer(int nSubJobId, int count, const std::uint8_t* packet);
    void SetSkillsLastUse(
        const std::uint8_t* packet, ReuseDelaySaveReason::Type saveReason, int nSubJobId, int number, int nAllJobNumber
    );
    bool CheckMaxLevel(int nSubJobId, int level);
    bool UpdateMaxLevel(int nSubJobId, int level, bool forceUpdate);
    void SendMainClassSkill(CSocket* socket, unsigned int userSocketId);
    void SendBlockList(CSocket* socket, unsigned int userSocketId);
    void SendAcquiredPotentialSkill(CSocket* socket, unsigned int characterId, unsigned int userSocketId);
    bool SetAcquirePotentialSkill(int skillId, int skillLevel, int dualClass, int potentialType, int& acquiredPointDelta);
    bool ResetPotentialSkill(int userSocketId, CSocket* socket);
    void SendAquireSkill(CSocket* socket, unsigned int userSocketId);
    void ShowPetiMsg(CSocket* socket);
    bool SetAquireSkill(int nSkillId, int nSkillLevel, int nSkillSubLevel, bool bForce, int nSubJobId);
    bool SetSubJobAcquireSkill(int nSubJobId, int nSkillId, int nSkillLevel, int nSkillSubLevel);
    int GetAcquireSkillLevelSet(int nSkillId);
    bool DeleteAquireSkill(int nSkillId);
    bool DeleteSubJobSkill(int nSubJobId, int nSkillId);
    // wchar_t* in the Win64 PDB. Char16_t keeps that UTF-16 packet ABI on
    // hosts where wchar_t is 32 bits.
    void DeleteAcquireSkillList(char16_t* skillList);
    bool DeletePledgeSkill(int nSkillId);
    void LoadLastLogout();
    void LoadUserVitality();
    void LoadQuestFromDB();
    void SetQuestNPCLog(const unsigned char* packet, int characterId);
    void LoadQuestNPCLog(CSocket* socket, int characterId, unsigned int requestContext);
    void SaveQuestNPCLog(int memoIndex, int questId, UserNPCLogType userLogType, int npcId, int count);
    void LoadVoteSystem();
    bool SaveVoteSystem(
        int bonusTimeLimit, int playTime, int voteCount, int bonusCount, int playTimeCount, SYSTEMTIME* lastInitTime
    );
    void LoadTrainingZoneInfo();
    void LoadUserPledgeBonus();
    void LoadSociality();
    void SetSociality(int sociality);
    bool SetSocialityWithLock(int sociality);
    void SetSocialityAll(int sociality, int usedSuffrageCount, SYSTEMTIME* lastCheck);
    SYSTEMTIME* GetSocialityLastCheck();
    int GetSociality();
    int GetUsedSulffrageCount();
    bool ReplyLoadSelectedChar(
        CSocket* socket, unsigned int userSocketId, AddtionalLocationType locationType, int x, int y, int z
    );
    void SetDropExp(std::int64_t dropExperience);
    void SetAP(int abilityPoints);
    void CheckVitalityReset();
    CVitalData GetVitalData();
    void UpdateVitality(CVitalData& data);
    void UpdateVitalityByAdmin(CVitalData& data, int charDbId, int subjobId);
    int GetVitality();
    void AssembleVitality(IPacketHelper& packet);

private:
    // Current CheckPCBangCharPacket reads the target byte at +0x2490
    // directly. Friendship exposes that same kept member without adding
    // a portable-only CUser accessor.
    friend bool CheckPCBangCharPacket(CAdminSocket* socket, const std::uint8_t* packet);
    // CheckFreeUserPacket reads the shipping logged-in byte at +0x24a4 and
    // session-log source at +0x2494 directly. Keep that owner access instead
    // of publishing an accessor absent from the current target surface.
    friend bool CheckFreeUserPacket(CAdminSocket* socket, const std::uint8_t* packet);
    // Current MoveItemProc reads the shipping logged-in byte at +0x24a4
    // directly. Friendship keeps that source relationship without adding
    // a non-target accessor.
    friend bool MoveItemProc(
        CAdminSocket* socket,
        int replyOpcode,
        int sourceCharacterId,
        int itemId,
        std::int64_t amount,
        int destinationCharacterId,
        int destinationWarehouse,
        const wchar_t* auditText
    );
    // Current AQ 9/10/11/57/84/92/115 read the shipping logged-in byte at
    // +0x24a4 directly. Friendship keeps that owner access without
    // exposing a non-target accessor.
    friend bool SetOnetimeQuestPacket(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool SetQuestPacket(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool DelQuestPacket(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool SetQuest2Packet(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool DelMacroPacket(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool AddMacroPacket(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool AddMacroInfoPacket(CAdminSocket* socket, const std::uint8_t* packet);
    // Current AQ 87 checks and updates the resident SSQ block at
    // +0x24e8..+0x2538 directly. Friendship keeps that owner access
    // without adding getters absent from the current target surface.
    friend bool ModifyDepositedSSQItemPacket(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool DelRecipePacket(CAdminSocket* socket, const std::uint8_t* packet);
    // Current CreateSubjobPacket reads the ordinary Henna IDs and ability
    // points directly before invoking CUser::CreateSubJob. The premium-color
    // handler writes the resident nickname-color field after SQL succeeds.
    // Friendships keep those shipping owner accesses without adding
    // portable-only CUser getters or setters.
    friend bool CreateSubjobPacket(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool ModUserColorPremiumPacket(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool SetColorPacket(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool DisableAddedServicePacket(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool GmRegPacket(CAdminSocket* socket, const std::uint8_t* packet);
    // Current AQ 31/35 write the embedded account ID/name directly, and AQ
    // 36 clears the direct power-grade field after SetPledge. Friendships
    // keep those source relationships without publishing setters absent
    // from the current target surface.
    friend bool MoveCharacterPacket(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool RestoreCharPacket(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool RestoreChar2Packet(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool PledgeOustPacket(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool AQHandler::RequestResetInzoneRestrict(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool AQHandler::RequestAddRecipeListPacket(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool AQHandler::RequestAddShortcutListPacket(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool AQHandler::RequestAddActiveSkill(CAdminSocket* socket, const std::uint8_t* packet);
    // Current RequestAddDualClass reads the three ordinary Henna IDs and
    // ability-point field directly before invoking CreateDualJob or
    // RenewSubJob. Friendship keeps those production member reads without
    // publishing accessors absent from the target interface.
    friend bool AQHandler::RequestAddDualClass(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool AQHandler::RequestCheckRaidUser(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool AQHandler::RequestResetAbilityPoint(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool AQHandler::RequestResetWorldOlympiadPoint(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool AQHandler::RequestModifyRaidPoint(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool AQHandler::RequestModifyWorldchatPoint(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool AQHandler::RequestAddAbilitySkill(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool AQHandler::RequestAddAbilityPoint(CAdminSocket* socket, const std::uint8_t* packet);
    friend class CSocket;
    friend class CWareHouse;
    friend class CFriends;
    friend class CPledgeDb;
    // Current Cached.exe CLogFormatter::WriteLogCache1/3 read the CUser
    // storage directly. Keep that recovered owner access without publishing
    // getters or portable carrier structures absent from the target API.
    friend class CLogFormatter;
    friend class CPledgeMemberListPacketAssembler;
    friend class CUserDb;
    friend class CUserDbHelper;
    friend class DxSetTicketBuyCount;
    friend class DxAddSevenSignQuestMember;
    friend class DxDeleteDepositedSevenSignQuestItem;
    friend class DxDepositSevenSignQuestItemEx;
    friend class DxChangeSSQUserPoint;
    friend class DxDismissSevenSignMember;
    friend struct PackageTargetListAssembler;
    friend bool RQExHandler::RequestBuyBeautyItem(CSocket* socket, const unsigned char* packet);
    friend bool RQExHandler::RequestResetBeautyItem(CSocket* socket, const unsigned char* packet);
    friend bool RQExHandler::RequestUpdateHairFlag(CSocket* socket, const unsigned char* packet);
    friend bool RQExHandler::RequestAcquirePotentialSkill(CSocket* socket, const unsigned char* packet);
    friend bool RQExHandler::RequestResetPotentialSkill(CSocket* socket, const unsigned char* packet);
    friend bool RQExHandler::RequestBrStartTrainingZone(CSocket* socket, const unsigned char* packet);
    friend bool RQExHandler::RequestBrEndTrainingZone(CSocket* socket, const unsigned char* packet);
    friend bool RQExHandler::RequestBrLoadTrainingZone(CSocket* socket, const unsigned char* packet);
    friend bool RQExHandler::RequestLoadDroidNPCAppearance(CSocket* socket, const unsigned char* packet);
    friend void PeriodicPremiumHennaJobHandler::OnLoadInfo(CSocket* socket, int characterId, int userSocketId);
    friend bool SaveItemsPacket(CSocket* socket, const std::uint8_t* packet);
    friend bool RequestCharacterLogin(CSocket* socket, const std::uint8_t* packet);
    friend bool RequestCharacterLogout(CSocket* socket, const std::uint8_t* packet);
    friend bool RequestSetAssociatedInZone(CSocket* socket, const std::uint8_t* packet);
    friend bool LoadSelectedCharacter(CSocket* socket, const std::uint8_t* packet);
    friend bool RequestRestoreCharacter(CSocket* socket, const std::uint8_t* packet);
    friend bool RequestOustPledgeMember(CSocket* socket, const std::uint8_t* packet);
    friend bool RequestSaveDieDropExp(CSocket* socket, const std::uint8_t* packet);
    friend bool RequestSaveSkillUse(CSocket* socket, const std::uint8_t* packet);
    friend bool RequestLoadAbnormalStatus(CSocket* socket, const std::uint8_t* packet);
    friend bool RequestSaveCharacterSlotEx(CSocket* socket, const std::uint8_t* packet);
    friend bool RequestChangeMergedName(CSocket* socket, const std::uint8_t* packet);
    friend bool RequestLoadBotReport(CSocket* socket, const std::uint8_t* packet);
    friend bool RequestChangeNicknameColor(CSocket* socket, const std::uint8_t* packet);
    friend bool RequestUserRaceSex(CSocket* socket, const unsigned char* packet);
    friend bool RequestItemCheck(CSocket* socket, const unsigned char* packet);
    friend void UserBookmarkJobHandler::OnAddSlot(int characterId, int slotCount);
    friend bool RQExHandler::RequestSaveUserJobGroup(CSocket* socket, const unsigned char* packet);
    bool _LoadAcquiredPotentialSkill(int nSubJobId);
    void _InsertOrUpdateLastUse(int nSubJobId, int nSkillId, int nEndTime, int delay);
    bool _SetSkillLastUse(ReuseDelaySaveReason::Type saveReason, int nSubJobId, int nSkillId, int nEndTime, int delay);
    void _PackingReuseDelay(int nSubJobId, char* buffer, int bufferSize, unsigned int& length, int& count);
    bool SetSkillLock(int nSkillId, int nSkillLock, bool bToSave);
    void SetCreateDate(int year, int month, int day, int hour, int minute, int second);
    void SetLastLogin(int year, int month, int day, int hour, int minute, int second);
    void SetLastLogout(int year, int month, int day, int hour, int minute, int second);
    void InitQuestMemo();
    int CalcKarma(int newPkCount, int pkPardon);
    void SetLastInitTime(const wchar_t* lastInitTime);
    void InitializeSSQInfo();
    void InitAtConstructor(const UserInitialState& state);
    void SetAtConstructor(INCLUDE_WAREHOUSE includeWarehouse, const UserInitialState& state);
    void Set(const UserInitialState& state);
    void SetSlot(const UserEquipmentSlots& equipment);
    void OnItemCheckFailed(int checkFailType);

    L2_DECLARE_OBJECT_REGISTRATION(CUser)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CUser)

    RWLock m_dataLock;
    CObjectStorage<CWareHouse> m_inventory;
    CObjectStorage<CWareHouse> m_warehouse;
    CObjectStorage<CWareHouse> m_manor;
    CObjectStorage<CWareHouse> m_mail;

    std::u16string m_characterNameLow;
    std::u16string m_characterName;
    std::u16string m_accountName;
    std::uint32_t m_characterId = 0;
    std::int32_t m_accountId = -1;

    std::uint32_t m_pledgeId = 0;
    int m_builder = 0;
    int m_gender = 0;
    int m_race = 0;
    int m_characterClass = 0;
    int m_worldId = 0;
    int m_x = 0;
    int m_y = 0;
    int m_z = 0;
    bool m_inVehicle = false;

    double m_hp = 0.0;
    double m_mp = 0.0;
    double m_maxHp = 0.0;
    double m_maxMp = 0.0;
    double m_cp = 0.0;
    double m_maxCp = 0.0;
    std::int64_t m_sp = 0;
    std::int64_t m_experience = 0;
    int m_level = 0;
    std::int64_t m_highestExperience = 0;
    int m_highestLevel = 0;
    int m_alignment = 0;
    std::uint32_t m_pkCount = 0;
    std::uint32_t m_duelCount = 0;
    std::uint32_t m_pkPardon = 0;
    std::int64_t m_dropExperience = 0;
    int m_abilityPoints = 0;

    UserEquipmentSlots m_equipment;
    // Current target +0x45c is a complete 16-byte SYSTEMTIME. In
    // particular, wDay is at +0x462. A six-element packed array would put it
    // at the wrong offset and change deletion scheduling.
    SYSTEMTIME m_deletionTime{};
    int m_face = 0;
    int m_hairShape = 0;
    int m_hairColor = 0;
    int m_sociality = 0;
    int m_usedSuffrageCount = 0;
    SYSTEMTIME m_socialityLastCheck{};
    std::u16string m_nickname;
    std::array<std::uint8_t, 0x20> m_powerFlags{};
    std::uint32_t m_pledgeDismissTime = 0;
    std::uint32_t m_pledgeOustedTime = 0;
    std::uint32_t m_pledgeWithdrawTime = 0;
    // Current target +0x24c0. Host placement is not an ABI
    // claim. SaveCharacter reads its low 32-bit wrapping meaning directly.
    std::uint32_t m_useTime = 0;

    // Current target user-log/login/shutdown members at
    // +0x2498/+0x249c/+0x24a4 and security state +0x25c8. AddUserLog and
    // WriteLogin refresh +0x2498. Elapsed play time uses the distinct +0x198
    // baseline below. Host placement is not an ABI claim.
    std::uint32_t m_userLogTick = 0;
    std::uint32_t m_loginTick = 0;
    // Current target +0x24a0 is exchanged atomically by
    // ReplyLoadSelectedChar of both logging ticks.
    std::atomic<std::uint32_t> m_loadSelectedTick{0};
    std::atomic<bool> m_loggedIn{false};
    bool m_itemCheckPending = true;

    // SaveItemsPacket and WriteLogout read the target byte at +0x2490.
    bool m_sessionLogFlag = false;
    int m_sessionLogSource = 0;
    // Current CharAudit proves target +0x24a5/+0x24a8/+0x24b0/+0x24b8.
    // The older CacheD owner keeps the original identifiers.
    // current arithmetic widens the three sums to int64.
    bool m_auditNeeded = false;
    std::int64_t m_adenaSum = 0;
    std::int64_t m_defaultPriceSum = 0;
    std::int64_t m_itemSum = 0;
    int m_sessionLogValue1 = 0;
    int m_sessionLogValue2 = 0;
    std::array<std::uint8_t, 4> m_sessionAddress{};
    // RequestCharacterLogin writes the exact wire `Q` at target +0x24e0.
    // GetMacAddressString reads only its low six bytes, high byte first.
    std::uint64_t m_sessionMacAddress = 0;

    int m_subjobId = 0;
    std::array<int, 4> m_subjobClass{};
    int m_dualClassId = 0;
    int m_ssqDawnRound = 0;
    int m_characterNameColor = 0xffffff;
    int m_nicknameColor = 0xecf9a2;
    int m_pledgeType = 0;
    int m_academyPledgeId = 0;
    int m_gradeId = 0;

    // Current target +0x618..+0x624. The damaged current PDB does not expose
    // the four original member identifiers. Current CacheD SQL accesses and
    // paired L2Server captcha methods establish these semantic roles. The
    // target uses locked xchg/inc operations for every individual field.
    std::atomic<std::int32_t> m_botReportPoint{-1};
    std::atomic<std::int32_t> m_captchaConfirmTimes{0};
    std::atomic<std::int32_t> m_captchaConfirmSec{0};
    std::atomic<std::int32_t> m_captchaDelaySec{0};

    std::array<AbnormalStatus, 96> m_abnormalStatus{};
    // Current target stores these three adjacent 16-byte SYSTEMTIME values at
    // +0x630, +0x640 and +0x650. Host layout is not asserted.
    SYSTEMTIME m_lastLogoutTime{};
    SYSTEMTIME m_lastLoginTime{};
    SYSTEMTIME m_createDate{};
    int m_hairColorIndexEx = 0;
    int m_hairShapeIndexEx = 0;
    int m_faceIndexEx = 0;
    bool m_hairAccessoryFlag = false;
    std::array<int, 3> m_henna{};
    // Current target +0xc6c. EquipHenna/UnequipHenna access this fourth,
    // premium slot directly beside the three ordinary Henna IDs.
    int m_hennaPremiumId = 0;
    UserNoblessInfo m_noblessInfo;
    UserSSQInfo m_ssqInfo;
    bool m_ssqInfoLoaded = false;
    // Current target +0x470 serializes every acquired-skill, potential-skill,
    // skill-lock and reuse-delay map. CYieldLock is recursive:
    // LoadAquireSkill enters it before calling SetSkillLock, which enters the
    // same lock again in the shipping body.
    CYieldLock m_skillLock;
    // Target +0x498: skill ID -> { level, sublevel }. The second value is a
    // current-EXE addition absent from the older PDB/SQL donor.
    UserSkillMap m_acquiredSkills;
    // Target +0x580: skill ID -> persistent lock state.
    std::map<int, int> m_skillLocks;
    // Current target +0x5a0/+0x5c8. Every read and mutation of the honors
    // skill map is serialized by this distinct CYieldLock. It is not the
    // acquired-skill lock at +0x470.
    CYieldLock m_honorsSkillLock;
    std::map<int, int> m_acquiredHonorsSkills;
    // Target +0x4d8 and five maps at +0x4e0 with 0x20-byte target stride.
    bool m_skillReuseDelayLoaded = false;
    std::array<UserSkillMap, 5> m_skillReuseBySubjob;
    // Target +0x25e0: acquired potential skill ID -> { level, 0 }.
    UserSkillMap m_acquiredPotentialSkills;
    // Current target +0x25a8: event ID -> Unix time32 value. GetEventData
    // populates this map only after a successful Execute+Fetch. SetEventData
    // updates it before issuing the persistence call.
    std::map<int, int> m_eventData;
    // Current target +0x25d0. InitAtConstructor installs the raw pointer and
    // CUser::~CUser 0x005bf2b0 does not release it. Retain that observable
    // lifetime instead of introducing a portable-only smart owner.
    CVitality* m_vitality = nullptr;
    // Current target +0x25d8, exposed by exact GetReloadFlag/SetReloadFlag
    // guard signatures and used to select normal versus raid item reloads.
    bool m_reloadFlag = false;
    // Current RequestSaveDieDropExp reads/writes the adjacent target byte at
    // +0x25d9. The original field name is unavailable. Its machine-proven
    // role is to arm persistence before a later drop-exp packet saves it.
    bool m_dropExpSaveArmed = false;
    // Current target member +0x4b8 is
    // std::map<int,int,...>: recipe ID -> owning character ID.
    std::map<int, int> m_recipeBook;
    // Current CUser owns exactly 41 MemoStruct records at target +0xcdc.
    std::array<MemoStruct, 41> m_questMemos{};

    int m_previousServer = 0;
    std::array<char16_t, 24> m_nameOrigin{};

    std::int64_t m_pledgeGamePoint = 0;
    std::int64_t m_olympiadPoint = 0;
    bool m_matchingPointChanged = false;

    // Column and member order are fixed by lin_LoadVoteSystem and current
    // writes at target +0x2540..+0x2563.
    int m_voteCount = 0;
    int m_bonusTimeLimit = 0;
    int m_bonusCount = 0;
    int m_playTime = 0;
    int m_playTimeCount = 0;
    SYSTEMTIME m_lastVoteInitTime{};
    int m_trainingZoneEndTime = 0;
    // Current target member +0x262c. The available PDB predates this field.
    // the semantic name follows its sole load/update protocol family.
    int m_userPledgeBonus = 0;

    UserLoadedInfo m_loadedInfo;
    // Current target +0x198, set by AddUserLog but not AddUserLog2.
    std::uint32_t m_lastUpdateTick = 0;
    // Current target +0x1a0. GetExpDiff compares it with m_experience at
    // +0x300 and then advances the baseline under m_dataLock.
    std::int64_t m_previousExperience = 0;
    // Original identifier is absent. Current +0x24d8 is raised by the
    // successful sub/dual-job transition paths so their baseline reset does
    // not appear as earned experience in the next SaveItems audit.
    bool m_skipNextExpDiff = false;
};
