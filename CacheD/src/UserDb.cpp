#include "Shared/src/Log.h"

#include "UserDb.h"

#include "Cached.h"
#include "IOCPMessageCached.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/DBConn.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/Runtime/TargetFormatting.h"
#include "Shared/src/UtilsString.h"
#include "Shared/src/TextRuntime.h"
#include "Runtime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace {

constexpr std::size_t kTargetNameCharacters = 0x18;

std::mutex g_peakUserTimeLock;

struct CUserProperty {
    std::array<char16_t, kTargetNameCharacters> characterName{};
    std::uint32_t characterId = 0;
    std::array<char16_t, kTargetNameCharacters> accountName{};
    int accountId = 0;
    std::uint32_t pledgeId = 0;
    int builder = 0;
    int gender = 0;
    int race = 0;
    int characterClass = 0;
    int worldId = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    int inVehicle = 0;
    double hp = 0.0;
    double mp = 0.0;
    double maxHp = 0.0;
    double maxMp = 0.0;
    double cp = 0.0;
    double maxCp = 0.0;
    long long sp = 0;
    long long experience = 0;
    int level = 0;
    long long highestExperience = 0;
    int highestLevel = 0;
    int alignment = 0;
    std::uint32_t pkCount = 0;
    std::uint32_t duelCount = 0;
    std::uint32_t pkPardon = 0;
    UserEquipmentSlots equipment;
    UserDeletionTime deletionTime;
    std::array<std::uint8_t, 0x100> questFlags{};
    int face = 0;
    int hairShape = 0;
    int hairColor = 0;
    std::array<char16_t, kTargetNameCharacters> nickname{};
    std::array<std::uint8_t, 0x20> powerFlags{};
    std::uint32_t pledgeDismissTime = 0;
    std::uint32_t pledgeOustedTime = 0;
    std::uint32_t pledgeWithdrawTime = 0;
    std::uint32_t legacySurrenderWarId = 0;
    std::uint32_t useTime = 0;
    long long dropExperience = 0;
    int status = 0;
    int banEnd = 0;
    int subjobId = 0;
    std::array<int, 4> subjobClass{};
    int dualClassId = 0;
    int ssqDawnRound = 0;
    int characterNameColor = 0;
    int nicknameColor = 0;
    int pledgeType = 0;
    int gradeId = 0;
    int academyPledgeId = 0;
    int serviceFlag = 0;
    std::array<std::uint8_t, 0x80> tutorialFlags{};
    std::uint32_t associatedInzone = 0;
    int bookmarkSlot = 0;
    int associatedInzoneUid = 0;
    int jobGroup = -1;
    int abilityPoints = 0;

    // The current helper reuses +0x6b0 for nickname and dual
    // class nullable indicators.
    std::int64_t sharedNullableIndicator = -1;
};

std::u16string BoundString(const std::array<char16_t, kTargetNameCharacters>& value) {
    return std::u16string(FixedUtf16View(value));
}

void BindEquipment(DBConn& connection, UserEquipmentSlots& slots) {
    // migrations 018/019 keep their deployed PostgreSQL
    // artifact-before-deco SQL order. Bind by name into the corrected wire-
    // ordered carrier. CUser::SaveSlot applies the inverse projection.
    connection.Bind(&slots.underwear);
    connection.Bind(&slots.rightEar);
    connection.Bind(&slots.leftEar);
    connection.Bind(&slots.neck);
    connection.Bind(&slots.rightFinger);
    connection.Bind(&slots.leftFinger);
    connection.Bind(&slots.head);
    connection.Bind(&slots.rightHand);
    connection.Bind(&slots.leftHand);
    connection.Bind(&slots.gloves);
    connection.Bind(&slots.chest);
    connection.Bind(&slots.legs);
    connection.Bind(&slots.feet);
    connection.Bind(&slots.back);
    connection.Bind(&slots.bothHand);
    connection.Bind(&slots.hair);
    connection.Bind(&slots.hair2);
    connection.Bind(&slots.rightBracelet);
    connection.Bind(&slots.leftBracelet);
    connection.Bind(&slots.agathionMain);
    for (int& value : slots.agathionSub)
        connection.Bind(&value);
    connection.Bind(&slots.artifactBook);
    for (int& value : slots.artifactA)
        connection.Bind(&value);
    for (int& value : slots.artifactB)
        connection.Bind(&value);
    for (int& value : slots.artifactC)
        connection.Bind(&value);
    for (int& value : slots.artifactD)
        connection.Bind(&value);
    for (int& value : slots.deco)
        connection.Bind(&value);
    connection.Bind(&slots.waist);
    connection.Bind(&slots.brooch);
    for (int& value : slots.jewel)
        connection.Bind(&value);
}

void WriteCreatedCharacterLog(
    int characterId,
    unsigned int accountId,
    const wchar_t* characterName,
    const wchar_t* address,
    int race,
    int gender,
    int characterClass,
    const wchar_t* accountName
) {
    // Current helper 0x005bf5dc is an unnamed Win64 vararg adapter. The
    // literal at CUserDb::CreateUser 0x005c2fd0 fixes this 26-field record,
    // including numeric zeroes at 14..19 and the blank field 20.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x326);
    fields[1] = runtime::DecimalUtf16(characterId);
    fields[2] = runtime::DecimalUtf16(accountId);
    fields[8] = WideToUtf16(characterName);
    fields[9] = WideToUtf16(address);
    fields[11] = runtime::DecimalUtf16(race);
    fields[12] = runtime::DecimalUtf16(gender);
    fields[13] = runtime::DecimalUtf16(characterClass);
    for (std::size_t index = 14; index <= 19; ++index) {
        fields[index] = u"0";
    }
    fields[21] = WideToUtf16(characterName);
    fields[22] = WideToUtf16(accountName);

    std::u16string record;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (index != 0)
            record.push_back(u',');
        record.append(fields[index]);
    }
    runtime::process::WriteLogFormatterLine(6, record);
}

// Original identifier unavailable. Current helper 0x005c2a9c owns the exact
// 24-parameter lin_CreateChar binding used by CUserDb::CreateUser.
bool ExecuteCreateCharacterStatement(
    DBConn& connection,
    wchar_t* characterName,
    wchar_t* accountName,
    unsigned int accountId,
    unsigned int pledgeId,
    int builder,
    int gender,
    int race,
    int characterClass,
    int worldId,
    int x,
    int y,
    int z,
    double hp,
    double mp,
    std::int64_t sp,
    std::int64_t experience,
    int level,
    int alignment,
    int pkCount,
    unsigned int duelCount,
    unsigned int pkPardon,
    int faceIndex,
    int hairShapeIndex,
    int hairColorIndex
) {
    constexpr const wchar_t* statement = L"{Call lin_CreateChar "
                                         L"(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(characterName);
    connection.BindParameter(accountName);
    connection.BindParameter(&accountId);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&builder);
    connection.BindParameter(&gender);
    connection.BindParameter(&race);
    connection.BindParameter(&characterClass);
    connection.BindParameter(&worldId);
    connection.BindParameter(&x);
    connection.BindParameter(&y);
    connection.BindParameter(&z);
    connection.BindParameter(&hp);
    connection.BindParameter(&mp);
    connection.BindParameter(&sp);
    connection.BindParameter(&experience);
    connection.BindParameter(&level);
    connection.BindParameter(&alignment);
    connection.BindParameter(&pkCount);
    connection.BindParameter(&duelCount);
    connection.BindParameter(&pkPardon);
    connection.BindParameter(&faceIndex);
    connection.BindParameter(&hairShapeIndex);
    connection.BindParameter(&hairColorIndex);
    return connection.ExecutePrepared(statement);
}

// Original identifiers unavailable. Current helpers 0x005c2c30/0x005c2e00
// own the exact 28-parameter lin_CreateCharWithSubjob binding used by the
// second CUserDb::CreateUser overload.
bool ExecuteCreateCharacterWithSubjobStatement(
    DBConn& connection,
    wchar_t* characterName,
    wchar_t* accountName,
    unsigned int accountId,
    unsigned int pledgeId,
    int builder,
    int gender,
    int race,
    int characterClass,
    int worldId,
    int x,
    int y,
    int z,
    double hp,
    double mp,
    std::int64_t sp,
    std::int64_t experience,
    int level,
    int alignment,
    int pkCount,
    unsigned int duelCount,
    unsigned int pkPardon,
    int faceIndex,
    int hairShapeIndex,
    int hairColorIndex,
    int subjobId,
    int mainClass,
    int dualClassId,
    double cp
) {
    constexpr const wchar_t* statement = L"{Call lin_CreateCharWithSubjob "
                                         L"(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(characterName);
    connection.BindParameter(accountName);
    connection.BindParameter(&accountId);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&builder);
    connection.BindParameter(&gender);
    connection.BindParameter(&race);
    connection.BindParameter(&characterClass);
    connection.BindParameter(&worldId);
    connection.BindParameter(&x);
    connection.BindParameter(&y);
    connection.BindParameter(&z);
    connection.BindParameter(&hp);
    connection.BindParameter(&mp);
    connection.BindParameter(&sp);
    connection.BindParameter(&experience);
    connection.BindParameter(&level);
    connection.BindParameter(&alignment);
    connection.BindParameter(&pkCount);
    connection.BindParameter(&duelCount);
    connection.BindParameter(&pkPardon);
    connection.BindParameter(&faceIndex);
    connection.BindParameter(&hairShapeIndex);
    connection.BindParameter(&hairColorIndex);
    connection.BindParameter(&subjobId);
    connection.BindParameter(&mainClass);
    connection.BindParameter(&dualClassId);
    connection.BindParameter(&cp);
    return connection.ExecutePrepared(statement);
}

} // namespace

std::atomic<std::int32_t> CUserDb::s_usersOnline{0};
std::atomic<std::int32_t> CUserDb::s_maxUsersOnline{0};
runtime::SystemTime CUserDb::s_maxOnlineTime{};

class CUserDbHelper final : public DBConn {
public:
    // Current Cached.exe 0x005bf788. The explicit world argument is passed
    // by both shipping call sites but is not read by the constructor: DBConn
    // takes the request TLS database selector, while the result carrier starts
    // with the configured world. A successful fetch later replaces the
    // latter through result column 10.
    explicit CUserDbHelper(int worldId [[maybe_unused]]) : DBConn(runtime::process::CurrentDatabaseId()) {
        std::copy_n(L"test", 5, m_property.characterName.begin());
        std::copy_n(L"test", 5, m_property.accountName.begin());
        m_property.worldId = runtime::process::ConfiguredDatabaseId();
    }

    void BindUserProperty() {
        L2_GUARD_FUNCTION();
        UnBind();

        Bind(m_property.characterName.data(), 0x30);
        Bind(&m_property.characterId);
        Bind(m_property.accountName.data(), 0x30);
        Bind(&m_property.accountId);
        Bind(&m_property.pledgeId);
        Bind(&m_property.builder);
        Bind(&m_property.gender);
        Bind(&m_property.race);
        Bind(&m_property.characterClass);
        Bind(&m_property.worldId);
        Bind(&m_property.x);
        Bind(&m_property.y);
        Bind(&m_property.z);
        Bind(&m_property.inVehicle);
        Bind(&m_property.hp);
        Bind(&m_property.mp);
        Bind(&m_property.maxHp);
        Bind(&m_property.maxMp);
        Bind(&m_property.cp);
        Bind(&m_property.maxCp);
        Bind(&m_property.sp);
        Bind(&m_property.experience);
        Bind(&m_property.level);
        Bind(&m_property.highestExperience);
        Bind(&m_property.highestLevel);
        Bind(&m_property.alignment);
        Bind(&m_property.pkCount);
        Bind(&m_property.duelCount);
        Bind(&m_property.pkPardon);
        BindEquipment(*this, m_property.equipment);
        Bind(&m_property.deletionTime.year);
        Bind(&m_property.deletionTime.month);
        Bind(&m_property.deletionTime.day);
        Bind(&m_property.deletionTime.hour);
        Bind(&m_property.deletionTime.minute);
        Bind(&m_property.deletionTime.second);
        BindBinary(reinterpret_cast<char*>(m_property.questFlags.data()), 0x100);
        Bind(&m_property.face);
        Bind(&m_property.hairShape);
        Bind(&m_property.hairColor);
        BindNullable(m_property.nickname.data(), 0x30, &m_property.sharedNullableIndicator);
        BindBinary(reinterpret_cast<char*>(m_property.powerFlags.data()), 0x20);
        Bind(&m_property.pledgeDismissTime);
        Bind(&m_property.pledgeOustedTime);
        Bind(&m_property.pledgeWithdrawTime);
        Bind(&m_property.legacySurrenderWarId);
        Bind(&m_property.useTime);
        Bind(&m_property.dropExperience);
        Bind(&m_property.status);
        Bind(&m_property.banEnd);
        Bind(&m_property.subjobId);
        for (int& value : m_property.subjobClass)
            Bind(&value);
        BindNullable(&m_property.dualClassId, &m_property.sharedNullableIndicator);
        Bind(&m_property.ssqDawnRound);
        Bind(&m_property.characterNameColor);
        Bind(&m_property.nicknameColor);
        Bind(&m_property.pledgeType);
        Bind(&m_property.gradeId);
        Bind(&m_property.academyPledgeId);
        Bind(&m_property.serviceFlag);
        BindBinary(reinterpret_cast<char*>(m_property.tutorialFlags.data()), 0x80);
        Bind(&m_property.associatedInzone);
        Bind(&m_property.bookmarkSlot);
        Bind(&m_property.associatedInzoneUid);
        Bind(&m_property.jobGroup);
        Bind(&m_property.abilityPoints);
    }

    UserInitialState MakeInitialState() const {
        UserInitialState state;
        state.characterName = BoundString(m_property.characterName);
        state.accountName = BoundString(m_property.accountName);
        state.characterId = m_property.characterId;
        state.accountId = m_property.accountId;
        state.pledgeId = m_property.pledgeId;
        state.builder = m_property.builder;
        state.gender = m_property.gender;
        state.race = m_property.race;
        state.characterClass = m_property.characterClass;
        state.worldId = m_property.worldId;
        state.x = m_property.x;
        state.y = m_property.y;
        state.z = m_property.z;
        state.inVehicle = m_property.inVehicle != 0;
        state.hp = m_property.hp;
        state.mp = m_property.mp;
        state.maxHp = m_property.maxHp;
        state.maxMp = m_property.maxMp;
        state.cp = m_property.cp;
        state.maxCp = m_property.maxCp;
        state.sp = m_property.sp;
        state.experience = m_property.experience;
        state.level = m_property.level;
        state.highestExperience = m_property.highestExperience;
        state.highestLevel = m_property.highestLevel;
        state.alignment = m_property.alignment;
        state.pkCount = m_property.pkCount;
        state.duelCount = m_property.duelCount;
        state.pkPardon = m_property.pkPardon;
        state.abilityPoints = m_property.abilityPoints;
        state.equipment = m_property.equipment;
        state.deletionTime = m_property.deletionTime;
        state.face = m_property.face;
        state.hairShape = m_property.hairShape;
        state.hairColor = m_property.hairColor;
        state.nickname = BoundString(m_property.nickname);
        state.powerFlags = m_property.powerFlags;
        state.pledgeDismissTime = m_property.pledgeDismissTime;
        state.pledgeOustedTime = m_property.pledgeOustedTime;
        state.pledgeWithdrawTime = m_property.pledgeWithdrawTime;
        state.ignoredSurrenderWarId = m_property.legacySurrenderWarId;
        state.useTime = m_property.useTime;
        state.subjobId = m_property.subjobId;
        state.subjobClass = m_property.subjobClass;
        state.dualClassId = m_property.dualClassId;
        state.ssqDawnRound = m_property.ssqDawnRound;
        state.characterNameColor = m_property.characterNameColor;
        state.nicknameColor = m_property.nicknameColor;
        state.pledgeType = m_property.pledgeType;
        state.gradeId = m_property.gradeId;
        state.academyPledgeId = m_property.academyPledgeId;
        return state;
    }

    CUser* MakeNewUserObject() {
        L2_GUARD_FUNCTION();
        return new CUser(MakeInitialState());
    }

    bool LoadUserByCharId(std::uint32_t characterId) {
        L2_GUARD_FUNCTION();
        constexpr const wchar_t* query = L"{Call lin_GetUserDataByCharId (?)}";
        Prepare(query);
        BindParameter(&characterId);
        return ExecutePrepared(query);
    }

    bool LoadUserByCharName(const char16_t* characterName) {
        // Current Cached.exe 0x005c3ea8.
        L2_GUARD_FUNCTION();
        constexpr const wchar_t* query = L"{Call lin_GetUserDataByCharName (?)}";
        Prepare(query);
        BindParameter(characterName);
        return ExecutePrepared(query);
    }

    int GetCountUserByCharId(std::uint32_t characterId) {
        L2_GUARD_FUNCTION();
        int count = 0;
        Bind(&count);
        constexpr const wchar_t* query = L"{Call lin_GetUserDataCountByCharId (?)}";
        Prepare(query);
        BindParameter(&characterId);
        if (!ExecutePrepared(query) || !Fetch()) {
            return -1;
        }
        return count;
    }

    int GetCountUserByCharName(const char16_t* characterName) {
        L2_GUARD_FUNCTION();
        int count = 0;
        Bind(&count);
        constexpr const wchar_t* query = L"{Call lin_GetUserDataCountByCharName (?)}";
        Prepare(query);
        BindParameter(characterName);
        if (!ExecutePrepared(query) || !Fetch()) {
            return -1;
        }
        return count;
    }

    bool Fetch() {
        L2_GUARD_FUNCTION();
        return DBConn::Fetch();
    }

    void LoadUserInfo(CUser* pUserIn) {
        L2_GUARD_FUNCTION();

        // Current 0x005c10a0 performs these direct member writes in exactly
        // this order. The grouped carrier is a host-layout choice only.
        pUserIn->m_loadedInfo.status = m_property.status;
        pUserIn->m_loadedInfo.banEnd = m_property.banEnd;
        pUserIn->m_loadedInfo.serviceFlag = m_property.serviceFlag;
        pUserIn->m_loadedInfo.questFlags.SetFlagAll(reinterpret_cast<char*>(m_property.questFlags.data()));

        pUserIn->LoadAquireSkill(pUserIn->m_subjobId);

        pUserIn->m_loadedInfo.tutorialFlags.SetFlagAll(reinterpret_cast<char*>(m_property.tutorialFlags.data()));
        pUserIn->m_loadedInfo.associatedInzone = m_property.associatedInzone;
        pUserIn->m_loadedInfo.bookmarkSlot = m_property.bookmarkSlot;

        pUserIn->LoadVoteSystem();

        pUserIn->m_loadedInfo.associatedInzoneUid = m_property.associatedInzoneUid;
        pUserIn->m_loadedInfo.jobGroup = m_property.jobGroup;

        pUserIn->LoadTrainingZoneInfo();
        pUserIn->LoadUserPledgeBonus();
    }

    void ReloadUserInfo(CUser* user, CUser::INCLUDE_WAREHOUSE includeWarehouse) {
        L2_GUARD_FUNCTION();
        user->SetAtConstructor(includeWarehouse, MakeInitialState());
    }

    void LoadUserEtc(CUser* pUserIn, bool) {
        L2_GUARD_FUNCTION();
        // Current 0x005c11e8 tests only the CUser pointer. The trailing bool
        // is present in the guard signature but is not read by the shipping
        // body, so both observed call-site values follow this same path.
        if (!pUserIn)
            return;

        LoadUserInfo(pUserIn);
        pUserIn->LoadActiveSkillFromDB();
        pUserIn->SetDropExp(m_property.dropExperience);
        pUserIn->LoadLastLogout();
        pUserIn->LoadTimeToGiveKarmaPoint();
        pUserIn->LoadQuestFromDB();
        pUserIn->LoadRecipeInfo();
        pUserIn->LoadHennaInfo();
        pUserIn->LoadSSQInfo();
        pUserIn->LoadMergedInfo();
        pUserIn->LoadBeautyData();
        pUserIn->LoadMatchingPoint();
        pUserIn->LoadUserVitality();
    }

    CUserSP AddToUserDb(CUser* pUserIn) {
        L2_GUARD_FUNCTION();
        CUserSP retained(pUserIn, ObjectRefType::FunctionChain);
        g_userDb.Add(pUserIn);
        L2_RELEASE(pUserIn, ObjectRefType::CreateDelete);
        return retained;
    }

    CUserSP LoadUserFromDb(std::uint32_t characterId) {
        L2_GUARD_FUNCTION();
        if (GetCountUserByCharId(characterId) <= 0) {
            return {};
        }

        BindUserProperty();
        if (!LoadUserByCharId(characterId)) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[UserDb.cpp][1060] LoadUserByCharId Fail "
            //     "char Id[%u], world[%d]",
            //     characterId, GetDatabaseId()
            // )
            L2_LOG_ERROR(Log,
                "[characters] Cannot load character %u from database %d: database execution failed. No character object created.",
                characterId, GetDatabaseId());
            return {};
        }
        if (!Fetch() || m_property.accountId == -1) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[UserDb.cpp][1055] Fetch Fail(or DeleteUer) "
            //     "char Id[%u], world[%d]",
            //     characterId, GetDatabaseId()
            // )
            L2_LOG_ERROR(Log,
                "[characters] Cannot load character %u from database %d: fetch failed or returned account ID -1. "
                "No character object created.",
                characterId, GetDatabaseId());
            return {};
        }

        CUser* const user = MakeNewUserObject();
        CUserSP retained = AddToUserDb(user);
        if (retained) {
            LoadUserEtc(user, false);
        }
        return retained;
    }

    CUserSP LoadUserFromDb(const char16_t* characterName) {
        // Current Cached.exe 0x005c59d4. Unlike the ID overload, this path
        // rechecks the loaded ID map after fetching so a name miss cannot
        // publish a duplicate object.
        L2_GUARD_FUNCTION();
        if (GetCountUserByCharName(characterName) <= 0) {
            return {};
        }

        BindUserProperty();
        if (!LoadUserByCharName(characterName) || !Fetch() || m_property.accountId == -1) {
            return {};
        }

        CUserSP retained = g_userDb.FindResidentUser(static_cast<int>(m_property.characterId));
        if (retained)
            return retained;

        CUser* const user = MakeNewUserObject();
        retained = AddToUserDb(user);
        if (retained) {
            LoadUserEtc(user, false);
        }
        return retained;
    }

    CUserSP ReloadUserFromDb(CUser* user, CUser::INCLUDE_WAREHOUSE includeWarehouse) {
        L2_GUARD_FUNCTION();
        if (!user)
            return {};

        const std::uint32_t characterId = user->GetId();
        if (GetCountUserByCharId(characterId) <= 0)
            return {};

        BindUserProperty();
        if (!LoadUserByCharId(characterId) || !Fetch() || m_property.accountId == -1) {
            return {};
        }

        ReloadUserInfo(user, includeWarehouse);
        LoadUserEtc(user, true);
        return CUserSP(user, ObjectRefType::FunctionChain);
    }

private:
    CUserProperty m_property;
};

CUserDb g_userDb;

CUserDb::CUserDb() = default;

CUserDb::~CUserDb() = default;

int CUserDb::CreateUser(
    wchar_t* characterName,
    wchar_t* accountName,
    unsigned int accountId,
    unsigned int pledgeId,
    int builder,
    int gender,
    int race,
    int characterClass,
    int worldId,
    int x,
    int y,
    int z,
    double hp,
    double mp,
    std::int64_t sp,
    std::int64_t experience,
    int level,
    int alignment,
    int pkCount,
    unsigned int duelCount,
    unsigned int pkPardon,
    int faceIndex,
    int hairShapeIndex,
    int hairColorIndex,
    wchar_t* address
) {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    int characterId = 0;
    connection.Bind(&characterId);

    constexpr const wchar_t* reservationStatement = L"{Call lin_CheckReserved (?,?,?)}";
    connection.Prepare(reservationStatement);
    connection.BindParameter(characterName);
    connection.BindParameter(accountName);
    connection.BindParameter(&accountId);
    if (!connection.ExecutePrepared(reservationStatement)) {
        return 0;
    }

    connection.ResetParam();
    if (ExecuteCreateCharacterStatement(
            connection, characterName, accountName, accountId, pledgeId, builder, gender, race, characterClass, worldId, x,
            y, z, hp, mp, sp, experience, level, alignment, pkCount, duelCount, pkPardon, faceIndex, hairShapeIndex,
            hairColorIndex
        ) &&
        connection.Fetch() && characterId != 0) {
        WriteCreatedCharacterLog(characterId, accountId, characterName, address, race, gender, characterClass, accountName);
    }

    connection.UnBind();
    int zero = 0;
    constexpr const wchar_t* beautyStatement = L"{Call lin_UpdateUserBeauty(?,?,?,?)}";
    connection.Prepare(beautyStatement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&zero);
    connection.BindParameter(&zero);
    connection.BindParameter(&zero);
    if (!connection.ExecutePrepared(beautyStatement)) {
        // Log.Add(
        //     LOG_ERROR, "[UserDb.cpp][325] Character beauty save error[%ls : %d].", characterName, characterId
        // )
        // L2_LOG_ERROR(Log, "[UserDb.cpp][325] Character beauty save error[%ls : %d].", characterName, characterId
        // )
        // Previous: L2_LOG_ERROR(Log, "Cannot initialize appearance for character '%ls' (ID %d), character creation continues to vote initialization.", characterName, characterId)
        L2_LOG_ERROR(Log,
            "[appearance] Cannot initialize appearance for character '%ls' (ID %d); character creation continues to vote initialization.",
            characterName, characterId);
    }

    connection.ResetParam();
    wchar_t initialVoteTime[] = L"1970-1-1 00:00:01";
    constexpr const wchar_t* voteStatement = L"{Call lin_SaveVoteSystem (?, ?, ?, ?, ?, ?, ?)}";
    connection.Prepare(voteStatement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&zero);
    connection.BindParameter(&zero);
    connection.BindParameter(&zero);
    connection.BindParameter(&zero);
    connection.BindParameter(&zero);
    connection.BindParameter(initialVoteTime);
    if (!connection.ExecutePrepared(voteStatement)) {
        // Log.Add(
        //     LOG_ERROR, "[UserDb.cpp][334] Vote system save error[%ls : (%ls)].", characterName, initialVoteTime
        // )
        // L2_LOG_ERROR(Log, "[UserDb.cpp][334] Vote system save error[%ls : (%ls)].", characterName, initialVoteTime
        // )
        // Previous: L2_LOG_ERROR(Log, "Cannot initialize voting data for character '%ls' (ID %d, initial vote time '%ls'), creation still returns this character ID.", characterName, characterId, initialVoteTime)
        L2_LOG_ERROR(Log,
            "[voting] Cannot initialize data for character '%ls' (ID %d, initial vote time '%ls'); creation still returns this character ID.",
            characterName, characterId, initialVoteTime);
    }
    return characterId;
}

int CUserDb::CreateUser(
    wchar_t* characterName,
    wchar_t* accountName,
    unsigned int accountId,
    unsigned int pledgeId,
    int builder,
    int gender,
    int race,
    int characterClass,
    int worldId,
    int x,
    int y,
    int z,
    double hp,
    double mp,
    std::int64_t sp,
    std::int64_t experience,
    int level,
    int alignment,
    int pkCount,
    unsigned int duelCount,
    unsigned int pkPardon,
    int faceIndex,
    int hairShapeIndex,
    int hairColorIndex,
    int subjobId,
    int mainClass,
    int dualClassId,
    double cp
) {
    // Current Cached.exe 0x005c3410. This is a distinct shipping overload,
    // not a portable extension of the ordinary character-creation path.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    int characterId = 0;
    connection.Bind(&characterId);

    constexpr const wchar_t* reservationStatement = L"{Call lin_CheckReserved (?,?,?)}";
    connection.Prepare(reservationStatement);
    connection.BindParameter(characterName);
    connection.BindParameter(accountName);
    connection.BindParameter(&accountId);
    if (!connection.ExecutePrepared(reservationStatement)) {
        return 0;
    }

    connection.ResetParam();
    if (ExecuteCreateCharacterWithSubjobStatement(
            connection, characterName, accountName, accountId, pledgeId, builder, gender, race, characterClass, worldId, x,
            y, z, hp, mp, sp, experience, level, alignment, pkCount, duelCount, pkPardon, faceIndex, hairShapeIndex,
            hairColorIndex, subjobId, mainClass, dualClassId, cp
        ) &&
        connection.Fetch() && characterId != 0) {
        WriteCreatedCharacterLog(characterId, accountId, characterName, L"", race, gender, characterClass, accountName);
    }

    connection.UnBind();
    int zero = 0;
    constexpr const wchar_t* beautyStatement = L"{Call lin_UpdateUserBeauty(?,?,?,?)}";
    connection.Prepare(beautyStatement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&zero);
    connection.BindParameter(&zero);
    connection.BindParameter(&zero);
    if (!connection.ExecutePrepared(beautyStatement)) {
        // Log.Add(
        //     LOG_ERROR, "[UserDb.cpp][392] Character beauty save error[%ls: %d]", characterName, characterId
        // )
        // L2_LOG_ERROR(Log, "[UserDb.cpp][392] Character beauty save error[%ls: %d]", characterName, characterId
        // )
        // Previous: L2_LOG_ERROR(Log, "Cannot initialize appearance for character '%ls' (ID %d, subjob %d), character creation continues to vote initialization.", characterName, characterId, subjobId)
        L2_LOG_ERROR(Log,
            "[appearance] Cannot initialize appearance for character '%ls' (ID %d, subjob %d); character creation continues to vote initialization.",
            characterName, characterId, subjobId);
    }

    connection.ResetParam();
    wchar_t initialVoteTime[] = L"1970-1-1 00:00:01";
    constexpr const wchar_t* voteStatement = L"{Call lin_SaveVoteSystem (?, ?, ?, ?, ?, ?, ?)}";
    connection.Prepare(voteStatement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&zero);
    connection.BindParameter(&zero);
    connection.BindParameter(&zero);
    connection.BindParameter(&zero);
    connection.BindParameter(&zero);
    connection.BindParameter(initialVoteTime);
    if (!connection.ExecutePrepared(voteStatement)) {
        // Log.Add(
        //     LOG_ERROR, "[UserDb.cpp][401] Vote system save error[%ls: (%ls)].", characterName, initialVoteTime
        // )
        // L2_LOG_ERROR(Log, "[UserDb.cpp][401] Vote system save error[%ls: (%ls)].", characterName, initialVoteTime
        // )
        // Previous: L2_LOG_ERROR(Log, "Cannot initialize voting data for character '%ls' (ID %d, subjob %d, initial vote time '%ls'), creation still returns this character ID.", characterName, characterId, subjobId, initialVoteTime)
        L2_LOG_ERROR(Log,
            "[voting] Cannot initialize data for character '%ls' (ID %d, subjob %d, initial vote time '%ls'); creation still returns this character ID.",
            characterName, characterId, subjobId, initialVoteTime);
    }
    return characterId;
}

void CUserDb::SetLoggedInIndex(CUser* user, bool loggedIn) {
    if (!user)
        return;
    user->SetLoggedIn(loggedIn);
    if (user->GetWorld() == GetCacheDConfiguration().worldId) {
        const int characterId = static_cast<int>(user->GetId());
        if (loggedIn) {
            m_loggedInUsers.Insert(characterId, user);
        } else {
            m_loggedInUsers.DeleteObject(characterId);
        }
    }
    if (!loggedIn)
        user->m_sessionLogFlag = false;
}

void CUserDb::Add(CUser* user) {
    L2_GUARD_FUNCTION();
    if (!user)
        return;

    // Current Cached.exe 0x005c53c0 reads TLS +0x10 for both world-partitioned
    // indexes. The persisted CUser world is independent object data and is
    // not used as the active request/map route here.
    const int worldId = runtime::process::CurrentDatabaseId();
    m_usersByWorldAndId.Insert(std::CDualKey<int, int>(worldId, static_cast<int>(user->GetId())), user);
    m_usersByNameByWorld.Insert(worldId, user->GetCharNameLow(), user);
    m_usersByAccountId.Insert(user->GetAccountID(), user);

    const int current = s_usersOnline.fetch_add(1, std::memory_order_seq_cst) + 1;
    int previousPeak = s_maxUsersOnline.load(std::memory_order_relaxed);
    while (current > previousPeak) {
        if (s_maxUsersOnline.compare_exchange_weak(
                previousPeak, current, std::memory_order_seq_cst, std::memory_order_relaxed
            )) {
            std::lock_guard<std::mutex> timeGuard(g_peakUserTimeLock);
            s_maxOnlineTime = runtime::GetLocalTimeNow();
            break;
        }
    }
}

CUserSP CUserDb::LoadFromDb(std::uint32_t characterId, int worldId) {
    L2_GUARD_FUNCTION();
    CUserDbHelper helper(worldId);
    return helper.LoadUserFromDb(characterId);
}

CUserSP CUserDb::LoadFromDb(const char16_t* characterName) {
    // Current Cached.exe 0x005c5e90, 259 bytes, SHA-256
    // aa275029696fc48f48610c6766e0429882ea1c77ebdf68ce697bca440ca202d6.
    L2_GUARD_FUNCTION();
    CUserDbHelper helper(GetCacheDConfiguration().worldId);
    return helper.LoadUserFromDb(characterName);
}

CUserSP CUserDb::ReLoadFromDb(CUser* user, CUser::INCLUDE_WAREHOUSE includeWarehouse) {
    L2_GUARD_FUNCTION();
    CUserDbHelper helper(runtime::process::CurrentDatabaseId());
    return helper.ReloadUserFromDb(user, includeWarehouse);
}

bool CUserDb::RefreshChar(CUser* user) {
    // Current Cached.exe 0x005c47dc. A non-null input is success regardless
    // of the reload result. The temporary smart reference is released on
    // return exactly as in the shipping owner.
    L2_GUARD_FUNCTION();
    if (!user)
        return false;
    CUserSP refreshed = ReLoadFromDb(user, static_cast<CUser::INCLUDE_WAREHOUSE>(1));
    return true;
}

CUserSP CUserDb::GetUser(int characterId, bool loadIfMissing, bool secondErrorVariant, int worldId) {
    L2_GUARD_FUNCTION();
    if (characterId < 0) {
        runtime::process::ReportNamedCallStack(L"GetUser", 2, false);
    }

    CUserSP user = m_usersByWorldAndId.FindObject(std::CDualKey<int, int>(worldId, characterId));
    if (user || !loadIfMissing) {
        return user;
    }

    user = LoadFromDb(static_cast<std::uint32_t>(characterId), worldId);
    if (!user) {
        [[maybe_unused]] const int sourceLine = secondErrorVariant ? 0x98 : 0x93;
        // Log.Add(
        //     LOG_ERROR,
        //     "[UserDb.cpp][%d] undefined (or unregistered) "
        //     "char(%d) Id[%d], world[%d]",
        //     sourceLine, secondErrorVariant ? 2 : 1, characterId, worldId
        // )
        // Noise: omit shipping source coordinates. Retain lookup variant and IDs.
        // L2_LOG_ERROR(Log,
        //     "[UserDb.cpp][%d] undefined (or unregistered) "
        //     "char(%d) Id[%d], world[%d]",
        //     sourceLine, secondErrorVariant ? 2 : 1, characterId, worldId)
        L2_LOG_ERROR(Log,
            "[characters] Could not load character %d in world %d (lookup variant %d). No character object was returned.",
            characterId, worldId, secondErrorVariant ? 2 : 1);
        runtime::process::ReportNamedCallStack(L"GetUser", 2, false);
    }
    return user;
}

CUserSP CUserDb::FindResidentUser(int characterId) {
    // Original identifier unavailable. Current Cached.exe 0x005127ec packs
    // TLS +0x10 as CDualKey::m_PrimaryKey and characterId as
    // m_SecondaryKey, then tail-calls the exact 0x00512594 map lookup. It
    // performs no guard, validation, log or database load.
    return m_usersByWorldAndId.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
}

CUserSP CUserDb::GetUserForRaid(int characterId, bool secondErrorVariant) {
    L2_GUARD_FUNCTION();
    if (characterId < 0) {
        runtime::process::ReportNamedCallStack(L"GetUser", 2, false);
    }

    const int worldId = runtime::process::CurrentDatabaseId();
    CUserSP user = m_usersByWorldAndId.FindObject(std::CDualKey<int, int>(worldId, characterId));
    if (user) {
        return ReLoadFromDb(user.get(), static_cast<CUser::INCLUDE_WAREHOUSE>(0));
    }

    user = LoadFromDb(static_cast<std::uint32_t>(characterId), worldId);
    if (!user) {
        [[maybe_unused]] const int sourceLine = secondErrorVariant ? 0xc2 : 0xbd;
        // Log.Add(
        //     LOG_ERROR,
        //     "[UserDb.cpp][%d] undefined (or unregistered) "
        //     "char(%d) Id[%d], world[%d]",
        //     sourceLine, secondErrorVariant ? 2 : 1, characterId, worldId
        // )
        // Noise: omit shipping source coordinates. Retain lookup variant and IDs.
        // L2_LOG_ERROR(Log,
        //     "[UserDb.cpp][%d] undefined (or unregistered) "
        //     "char(%d) Id[%d], world[%d]",
        //     sourceLine, secondErrorVariant ? 2 : 1, characterId, worldId)
        L2_LOG_ERROR(Log,
            "[characters] Could not load character %d in world %d (lookup variant %d). No character object was returned.",
            characterId, worldId, secondErrorVariant ? 2 : 1);
        runtime::process::ReportNamedCallStack(L"GetUser", 2, false);
    }
    return user;
}

CUserSP CUserDb::GetUser(const char16_t* characterName, bool suppressLoadError) {
    // Current Cached.exe 0x005c6674.
    L2_GUARD_FUNCTION();

    std::u16string lowerName(characterName);
    Utils::CopyStrInLowerCase(lowerName.c_str(), lowerName.data());
    CUserSP user = m_usersByNameByWorld.FindObject(runtime::process::CurrentDatabaseId(), lowerName);
    if (!user) {
        user = LoadFromDb(characterName);
    }
    if (!user && !suppressLoadError) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[UserDb.cpp][%d] undefined (or unregistered) "
        //     "char Name[%ls], world[%d]",
        //     0xe9, characterName, runtime::process::CurrentDatabaseId()
        // )
        // Noise: the macro supplies the current basename. Omit shipping coordinates.
        // L2_LOG_ERROR(Log,
        //     "[UserDb.cpp][%d] undefined (or unregistered) "
        //     "char Name[%ls], world[%d]",
        //     0xe9, characterName, runtime::process::CurrentDatabaseId())
        // L2_LOG_ERROR(Log,
        //     "[characters] Could not load character '%ls' in world %d. No character object was returned.",
        //     characterName, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[characters] Could not load character '%s' in world %d. No character object was returned.",
            Utf16ToUtf8(characterName).c_str(), runtime::process::CurrentDatabaseId());
        runtime::process::ReportNamedCallStack(L"GetUser", 2, false);
    }
    return user;
}

bool CUserDb::SimpleLoad(
    int characterId,
    char16_t* characterName,
    int characterNameBufferBytes,
    char16_t* nickname,
    int nicknameBufferBytes,
    int* level,
    int* characterClass,
    int* gender,
    int* race,
    int* gradeId,
    int* pledgeBonus
) {
    L2_GUARD_FUNCTION();

    std::int64_t nicknameIndicator = -1;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(characterName, characterNameBufferBytes);
    connection.BindNullable(nickname, nicknameBufferBytes, &nicknameIndicator);
    connection.Bind(level);
    connection.Bind(characterClass);
    connection.Bind(gender);
    connection.Bind(race);
    connection.Bind(gradeId);

    constexpr const wchar_t* simpleUserStatement = L"{Call lin_GetSimpleUserData (?)}";
    connection.Prepare(simpleUserStatement);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(simpleUserStatement) || !connection.Fetch()) {
        return false;
    }

    // Current 0x005c3a67 drops the first result bindings and requires the
    // second query to Execute+Fetch before reporting a successful simple
    // load. Activity zero is the exact value staged by the machine code.
    connection.UnBind();
    connection.Bind(pledgeBonus);
    const int activity = 0;
    constexpr const wchar_t* pledgeBonusStatement = L"{Call lin_LoadUserPledgeBonus(?, ?)}";
    connection.Prepare(pledgeBonusStatement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&activity);
    return connection.ExecutePrepared(pledgeBonusStatement) && connection.Fetch();
}

bool CUserDb::CopyChar(int sourceCharacterId, int builder, wchar_t* newCharacterName, wchar_t* accountName, int accountId) {
    // Exact current Cached.exe 0x005c3b40. lin_CheckReserved is deliberately
    // executed without fetching its bound result. Only lin_CopyChar's result
    // row controls the return value.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    int createdCharacterId = 0;
    connection.Bind(&createdCharacterId);

    constexpr const wchar_t* reservationStatement = L"{Call lin_CheckReserved (?,?,?)}";
    connection.Prepare(reservationStatement);
    connection.BindParameter(newCharacterName);
    connection.BindParameter(accountName);
    connection.BindParameter(&accountId);
    if (!connection.ExecutePrepared(reservationStatement)) {
        return false;
    }

    connection.ResetParam();
    constexpr const wchar_t* copyStatement = L"{Call lin_CopyChar (?, ?, ?, ?, ? )}";
    connection.Prepare(copyStatement);
    connection.BindParameter(&sourceCharacterId);
    connection.BindParameter(newCharacterName);
    connection.BindParameter(&accountId);
    connection.BindParameter(accountName);
    connection.BindParameter(&builder);
    if (!connection.ExecutePrepared(copyStatement)) {
        return false;
    }
    return connection.Fetch();
}

CUserSP CUserDb::GetUserByAccountId(int accountId) {
    L2_GUARD_FUNCTION();
    return m_usersByAccountId.FindObject(accountId);
}

bool CUserDb::SetUserNickname(unsigned int characterId, wchar_t* nickname) {
    // Current Cached.exe 0x005c68e8.
    L2_GUARD_FUNCTION();

    int returnCode = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&returnCode);

    constexpr const wchar_t* statement = L"{Call lin_SetUserNickname (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(nickname);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }
    if (returnCode == 0)
        return false;

    CUserSP user = GetUser(static_cast<int>(characterId), true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        // The target copies directly to the fixed 24-WCHAR field at
        // CUser+0xc80 and does not take a separate CUser data lock.
        user->m_nickname = WideToUtf16(nickname);
    }
    return true;
}

bool CUserDb::SetPowerGrade(int characterId, int gradeId) {
    L2_GUARD_FUNCTION();

    CUserSP user = GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetPledgePowerGrade (?, ?)}";
    connection.Prepare(statement);
    // Current 0x005c6ac8 binds grade first and character ID second.
    connection.BindParameter(&gradeId);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(statement))
        return false;

    user->m_gradeId = gradeId;
    return true;
}

bool CUserDb::DeleteChar(CUser* user, bool deleteFromDatabase) {
    L2_GUARD_FUNCTION();
    if (!user)
        return false;

    const bool result = !deleteFromDatabase || user->Delete();

    // Current 0x005c4e2c removes all three publication indexes and updates
    // the process count regardless of the database deletion result.
    // Current 0x005c4e9d/0x005c4f08 key both world-index removals with the
    // active request database ID from TLS +0x10.
    const int worldId = runtime::process::CurrentDatabaseId();
    m_usersByWorldAndId.DeleteObject(std::CDualKey<int, int>(worldId, static_cast<int>(user->GetId())));
    m_usersByNameByWorld.DeleteObject(worldId, user->GetCharNameLow());
    m_usersByAccountId.DeleteObject(user->GetAccountID());
    s_usersOnline.fetch_sub(1, std::memory_order_seq_cst);
    return result;
}

bool CUserDb::RefreshMaps(const char16_t* oldCharacterName, const char16_t* newCharacterName) {
    // Current Cached.exe 0x005c5558. The caller supplies both names in the
    // already-lowercased form used by the world-partitioned name index.
    L2_GUARD_FUNCTION();

    const int worldId = runtime::process::CurrentDatabaseId();
    CUserSP user = m_usersByNameByWorld.FindObject(worldId, oldCharacterName);
    if (user) {
        m_usersByNameByWorld.DeleteObject(worldId, oldCharacterName);
        m_usersByNameByWorld.Insert(worldId, newCharacterName, user.get());
    }
    return true;
}

void CUserDb::LoadCommissionUser() {
    L2_GUARD_FUNCTION();

    const int worldId = runtime::process::CurrentDatabaseId();
    int characterId = 0;
    DBConn connection(worldId);
    connection.Bind(&characterId);
    constexpr const wchar_t* statement = L"{Call lin_LoadCommissionUser}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement))
        return;

    while (connection.Fetch()) {
        CUserSP user = m_usersByWorldAndId.FindObject(std::CDualKey<int, int>(worldId, characterId));
        if (!user) {
            user = LoadFromDb(static_cast<std::uint32_t>(characterId), worldId);
            if (!user) {
                // Log.Add(
                //     LOG_ERROR,
                //     "[UserDb.cpp][%d] commission undefined (or "
                //     "unregistered) char Id[%d], world[%d]",
                //     0x62, characterId, worldId
                // )
                // Noise: omit the shipping line number. Retain commission context.
                // L2_LOG_ERROR(Log,
                //     "[UserDb.cpp][%d] commission undefined (or "
                //     "unregistered) char Id[%d], world[%d]",
                //     0x62, characterId, worldId)
                L2_LOG_ERROR(Log,
                    "[commission] Could not load character %d in world %d. No character object was returned.",
                    characterId, worldId);
            }
        }
    }
}

void CUserDb::PrintAllAccountData() {
    // Current Cached.exe 0x005c18c8 iterates all 0x4000 buckets of the
    // world/character map and calls CUser::PrintCharData on every object.
    L2_GUARD_FUNCTION();
    struct PrintCharacter {
        bool Do(CUser* user) {
            user->PrintCharData();
            return true;
        }
    } printCharacter;
    m_usersByWorldAndId.FOR_ALL(&printCharacter, L"UserDb.cpp", 0x215);
}

void CUserDb::PrintAllAccountWarehouseData() {
    // Current Cached.exe 0x005c1ae8 is the parallel full-map walk for the
    // two ordinary per-character warehouses.
    L2_GUARD_FUNCTION();
    struct PrintWarehouses {
        bool Do(CUser* user) {
            user->PrintCharWarehouseData();
            return true;
        }
    } printWarehouses;
    m_usersByWorldAndId.FOR_ALL(&printWarehouses, L"UserDb.cpp", 0x222);
}

bool CUserDb::LogoutAllChars() {
    L2_GUARD_FUNCTION();

    const std::int32_t loggedInUsers = m_loggedInUsers.GetSize();
    std::int32_t expected = 0;
    const bool firstInvocation = g_nPendingLogoutCharacters.compare_exchange_strong(
        expected, loggedInUsers, std::memory_order_seq_cst, std::memory_order_seq_cst
    );
    if (firstInvocation) {
        // Log.Add(LOG_ERROR, "[NO_ERROR] Logout All Characters : %d", loggedInUsers)
        // Log.Add(LOG_NORMAL, "component=shutdown event=character_logout_scheduled characters=%d", loggedInUsers)
        // Log.Add(LOG_NORMAL, "[shutdown] Scheduling logout for %d characters.", loggedInUsers)
        // Log.Add(LOG_NORMAL,
        //     "[disconnect cleanup] Queuing logout callbacks for %d logged-in characters.",
        //     loggedInUsers)
        L2_LOG_INFO(Log,
            "[disconnect cleanup] Queuing logout callbacks for %d logged-in characters.",
            loggedInUsers);
        struct PostLogout {
            bool Do(CUser* user) {
                (new CLogOutCharMessage(user))->Post(0);
                return true;
            }
        } post;
        m_loggedInUsers.FOR_ALL(&post, L"UserDb.cpp", 0x202);
    }
    CUser::s_loggedUsers.store(0, std::memory_order_seq_cst);
    return true;
}

void CUserDb::ItemCheckOnAllCharLoaded() {
    L2_GUARD_FUNCTION();

    const std::int32_t loadedUsers = m_usersByWorldAndId.GetSize();
    std::int32_t expected = -1;
    const bool firstInvocation = g_nPendingCharacterItemChecks.compare_exchange_strong(
        expected, loadedUsers, std::memory_order_seq_cst, std::memory_order_seq_cst
    );
    if (!firstInvocation)
        return;

    // Log.Add(LOG_NORMAL,
    //     "[disconnect cleanup] Queuing item-check callbacks for %d loaded characters.",
    //     static_cast<int>(loadedUsers))
    L2_LOG_INFO(Log,
        "[disconnect cleanup] Queuing item-check callbacks for %d loaded characters.",
        static_cast<int>(loadedUsers));
    struct PostItemCheck {
        bool Do(CUser* user) {
            (new CItemCheckOnCharLoadedMessage(user))->Post(0);
            return true;
        }
    } post;
    m_usersByWorldAndId.FOR_ALL(&post, L"UserDb.cpp", 0x27e);
}

void CUserDb::SetReloadMatchingPointAll() {
    L2_GUARD_FUNCTION();

    struct CSetReloadMatchingPoint : CObjectMapFunc {
        bool Do(MemoryObject* object) override {
            auto* const user = static_cast<CUser*>(object);
            if (user != nullptr) {
                user->m_matchingPointChanged = true;
            }
            return true;
        }
    } setReloadMatchingPoint;
    m_usersByWorldAndId.FOR_ALL(setReloadMatchingPoint);
}

void CUserDb::SetEventDataPeriod(int eventDataPeriod) {
    // Current Cached.exe 0x005bfa80.
    L2_GUARD_FUNCTION();
    m_eventDataPeriod = eventDataPeriod;
}
