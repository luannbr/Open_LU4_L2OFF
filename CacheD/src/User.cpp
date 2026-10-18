#include "User.h"
#include "Shared/src/MemorySub.h"

#include "BossRecordDB.h"
#include "CAccount.h"
#include "CReadFile.h"
#include "CacheSocket.h"
#include "Cached.h"
#include "Item.h"
#include "Pet.h"
#include "Pledge.h"
#include "PledgeDb.h"
#include "SevenSignQuestDB.h"
#include "UserDb.h"
#include "WareHouse.h"
#include "WareHouseType.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/DelimitedText.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/DBConn.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/QsCheck.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/SlotType.h"
#include "Shared/src/StringTokenizer.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/UtilsString.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/TextRuntime.h"
#include "Runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <optional>
#include <string_view>

namespace {


std::u16string TargetLowercase(std::u16string value) {
    Utils::CopyStrInLowerCase(value.c_str(), value.data());
    return value;
}

void InstallWarehouse(CObjectStorage<CWareHouse>& destination, std::uint32_t ownerId, WareHouseType type) {
    CWareHouse* warehouse = new CWareHouse;
    warehouse->SetOwnerId(ownerId);
    warehouse->Init(type, true);
    destination.SetObject(warehouse);
    L2_RELEASE(warehouse, ObjectRefType::CreateDelete);
}

std::u16string UserAddressText(const std::array<std::uint8_t, 4>& address) {
    char text[16]{};
    std::snprintf(
        text, sizeof(text), "%u.%u.%u.%u", static_cast<unsigned>(address[0]), static_cast<unsigned>(address[1]),
        static_cast<unsigned>(address[2]), static_cast<unsigned>(address[3])
    );
    return Utf8ToUtf16(text);
}

std::u16string UserMacAddressText(std::uint64_t address) {
    char text[18]{};
    std::snprintf(
        text, sizeof(text), "%02X-%02X-%02X-%02X-%02X-%02X", static_cast<unsigned>((address >> 40U) & 0xffU),
        static_cast<unsigned>((address >> 32U) & 0xffU), static_cast<unsigned>((address >> 24U) & 0xffU),
        static_cast<unsigned>((address >> 16U) & 0xffU), static_cast<unsigned>((address >> 8U) & 0xffU),
        static_cast<unsigned>(address & 0xffU)
    );
    return Utf8ToUtf16(text);
}

std::u16string QuestAuditDecimal(std::int64_t value) {
    const std::string source = std::to_string(value);
    return std::u16string(source.begin(), source.end());
}

void WriteSubJobTransitionAudit(const CUser& user, int route, int logId, int oldClass, int oldLevel, int result) {
    // The original helper identifier is unavailable. Current adapter
    // 0x0045e5e8 receives this exact sparse format from CreateSubJob
    // 0x005af9c8, first on route 6/log 0x34f and then route 8/log 0x591.
    std::array<std::u16string, 26> fields{};
    fields[0] = QuestAuditDecimal(logId);
    fields[1] = QuestAuditDecimal(user.GetId());
    fields[2] = QuestAuditDecimal(user.GetAccountID());
    fields[5] = QuestAuditDecimal(user.GetX());
    fields[6] = QuestAuditDecimal(user.GetY());
    fields[7] = QuestAuditDecimal(user.GetZ());
    fields[11] = QuestAuditDecimal(user.GetRace());
    fields[12] = QuestAuditDecimal(user.GetGender());
    fields[13] = QuestAuditDecimal(oldClass);
    fields[14] = QuestAuditDecimal(oldLevel);
    fields[15] = QuestAuditDecimal(user.GetClass());
    fields[16] = QuestAuditDecimal(user.GetLevel());
    fields[20] = QuestAuditDecimal(result);
    fields[21] = std::u16string(user.GetCharName());
    fields[22] = std::u16string(user.GetAccountName());
    runtime::process::WriteLogFormatterLine(route, runtime::JoinFields(fields, u','));
}

void WriteCharacterRenameAudit(const CUser& user, const char16_t* newCharacterName, const std::u16string& oldCharacterNameLow) {
    // Current CUser::ChangeCharacternameExtern 0x005acad8 calls CLog::Add
    // directly with this sparse 26-field route-6 record. In particular, the
    // final item field is empty instead of numeric zero.
    std::array<std::u16string, 26> fields{};
    fields[0] = QuestAuditDecimal(0x328);
    fields[1] = QuestAuditDecimal(user.GetId());
    fields[2] = QuestAuditDecimal(user.GetAccountID());
    fields[5] = QuestAuditDecimal(user.GetX());
    fields[6] = QuestAuditDecimal(user.GetY());
    fields[7] = QuestAuditDecimal(user.GetZ());
    fields[8] = newCharacterName;
    fields[11] = QuestAuditDecimal(user.GetRace());
    fields[12] = QuestAuditDecimal(user.GetGender());
    fields[13] = QuestAuditDecimal(user.GetClass());
    fields[14] = QuestAuditDecimal(user.GetLevel());
    fields[21] = oldCharacterNameLow;
    fields[22] = std::u16string(user.GetAccountName());
    runtime::process::WriteLogFormatterLine(LOG_BOTH, runtime::JoinFields(fields, u','));
}

void WriteRenewSubJobAudit(
    const CUser& user, int subjobId, int oldClass, int oldLevel, int subjobType, int newClass, int newLevel
) {
    // Current CUser::RenewSubJob 0x005b07fc forwards this exact sparse
    // LogFormatter record through helper 0x0059dbbc on route 6.
    std::array<std::u16string, 26> fields{};
    fields[0] = QuestAuditDecimal(0x359);
    fields[1] = QuestAuditDecimal(user.GetId());
    fields[2] = QuestAuditDecimal(user.GetAccountID());
    fields[5] = QuestAuditDecimal(user.GetX());
    fields[6] = QuestAuditDecimal(user.GetY());
    fields[7] = QuestAuditDecimal(user.GetZ());
    fields[11] = QuestAuditDecimal(subjobId);
    fields[12] = QuestAuditDecimal(oldClass);
    fields[13] = QuestAuditDecimal(oldLevel);
    fields[14] = QuestAuditDecimal(subjobType);
    fields[15] = QuestAuditDecimal(newClass);
    fields[16] = QuestAuditDecimal(newLevel);
    fields[21] = std::u16string(user.GetCharName());
    fields[22] = std::u16string(user.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteQuestMemoAudit(const CUser& user, int indexMarker, int questId, int journal) {
    // Original helper identifier is unavailable. Current adapter 0x004d4fa8
    // widens the integer arguments before forwarding this exact sparse
    // 26-field record to CLog::Add on route 6.
    std::array<std::u16string, 26> fields{};
    fields[0] = QuestAuditDecimal(0x32e);
    fields[1] = QuestAuditDecimal(user.GetId());
    fields[2] = QuestAuditDecimal(user.GetAccountID());
    fields[5] = QuestAuditDecimal(user.GetX());
    fields[6] = QuestAuditDecimal(user.GetY());
    fields[7] = QuestAuditDecimal(user.GetZ());
    fields[11] = QuestAuditDecimal(indexMarker);
    fields[12] = QuestAuditDecimal(questId);
    fields[13] = QuestAuditDecimal(journal);
    fields[21] = std::u16string(user.GetCharName());
    fields[22] = std::u16string(user.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteEvolvePetAudit(
    const CUser& user, int oldPetNpcClass, int oldCollarId, int oldLevel, int preserveName, int newPetNpcClass, int newLevel
) {
    // Current CUser::OnRequestEvolvePet 0x005a3660 writes this sparse
    // 26-field record through the unnamed route-6 adapter at 0x0059dcc8.
    std::array<std::u16string, 26> fields{};
    fields[0] = QuestAuditDecimal(0x79);
    fields[1] = QuestAuditDecimal(user.GetId());
    fields[2] = QuestAuditDecimal(user.GetAccountID());
    fields[5] = QuestAuditDecimal(user.GetX());
    fields[6] = QuestAuditDecimal(user.GetY());
    fields[7] = QuestAuditDecimal(user.GetZ());
    fields[11] = QuestAuditDecimal(user.GetRace());
    fields[12] = QuestAuditDecimal(user.GetGender());
    fields[13] = QuestAuditDecimal(user.GetClass());
    fields[14] = QuestAuditDecimal(user.GetLevel());
    fields[15] = QuestAuditDecimal(oldPetNpcClass);
    fields[16] = QuestAuditDecimal(oldCollarId);
    fields[17] = QuestAuditDecimal(oldLevel);
    fields[18] = QuestAuditDecimal(preserveName);
    fields[19] = QuestAuditDecimal(newPetNpcClass);
    fields[20] = QuestAuditDecimal(newLevel);
    fields[21] = std::u16string(user.GetCharName());
    fields[22] = std::u16string(user.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteBotReportDecayAudit(const CUser& user, int remainingReport, int decayedReport) {
    // Current CUser::DecayBotReport 0x005bd838 calls the unnamed route-6
    // 26-field adapter at 0x004efa60 after the 0xc6/0x62 reply. Raw call
    // setup 0x005bda93..0x005bdb83 fixes every populated field and also
    // proves the nonstandard class/race/gender ordering below.
    std::array<std::u16string, 26> fields{};
    constexpr std::array<std::size_t, 19> numericFields{0, 1, 2, 3, 4, 5, 6, 7, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 25};
    for (const std::size_t index : numericFields) {
        fields[index] = u"0";
    }
    fields[0] = QuestAuditDecimal(0x1f6d);
    fields[1] = QuestAuditDecimal(user.GetId());
    fields[2] = QuestAuditDecimal(user.GetAccountID());
    fields[5] = QuestAuditDecimal(user.GetX());
    fields[6] = QuestAuditDecimal(user.GetY());
    fields[7] = QuestAuditDecimal(user.GetZ());
    fields[11] = QuestAuditDecimal(user.GetClass());
    fields[12] = QuestAuditDecimal(user.GetRace());
    fields[13] = QuestAuditDecimal(user.GetGender());
    fields[14] = QuestAuditDecimal(user.GetLevel());
    fields[15] = QuestAuditDecimal(remainingReport);
    fields[16] = QuestAuditDecimal(decayedReport);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

// Original identifier is unavailable. Current helper 0x00450820 returns a
// standard padded Base64 std::string, CUser::SetQuest calls it for four
// consecutive 64-byte flag chunks.
std::u16string EncodeQuestFlagChunkBase64(const std::uint8_t* source, std::size_t length) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                       "abcdefghijklmnopqrstuvwxyz"
                                       "0123456789+/";

    std::u16string encoded;
    encoded.reserve(((length + 2U) / 3U) * 4U);
    for (std::size_t index = 0; index < length; index += 3U) {
        const unsigned first = source[index];
        const unsigned second = index + 1U < length ? source[index + 1U] : 0U;
        const unsigned third = index + 2U < length ? source[index + 2U] : 0U;
        encoded.push_back(static_cast<char16_t>(alphabet[first >> 2U]));
        encoded.push_back(static_cast<char16_t>(alphabet[((first & 0x03U) << 4U) | (second >> 4U)]));
        encoded.push_back(
            static_cast<char16_t>(index + 1U < length ? alphabet[((second & 0x0fU) << 2U) | (third >> 6U)] : '=')
        );
        encoded.push_back(static_cast<char16_t>(index + 2U < length ? alphabet[third & 0x3fU] : '='));
    }
    return encoded;
}

void WriteQuestFlagAudit(const CUser& user, int logId, int chunkIndex, const std::uint8_t* chunk) {
    // Original helper identifier is unavailable. Current adapter 0x0059d9ec
    // forwards this exact sparse record to CLog::Add on route 6.
    std::array<std::u16string, 26> fields{};
    fields[0] = QuestAuditDecimal(logId);
    fields[1] = QuestAuditDecimal(user.GetId());
    fields[2] = QuestAuditDecimal(user.GetAccountID());
    fields[5] = QuestAuditDecimal(user.GetX());
    fields[6] = QuestAuditDecimal(user.GetY());
    fields[7] = QuestAuditDecimal(user.GetZ());
    fields[8] = EncodeQuestFlagChunkBase64(chunk, 0x40);
    fields[11] = QuestAuditDecimal(chunkIndex);
    fields[21] = std::u16string(user.GetCharName());
    fields[22] = std::u16string(user.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteUserItemCheckAudit(const CUser& user, int penaltyLevel, int checkFailType) {
    CLogFormatter formatter;
    const std::u16string characterName = std::u16string(user.GetCharName());
    const std::u16string accountName = std::u16string(user.GetAccountName());
    formatter.WriteAuditLog(
        0x5a4, user.GetId(), user.GetAccountID(), 0, 0, 0, 0, 0, u"", u"", u"", 0, penaltyLevel, 0, checkFailType, 0, 0, 0,
        0, 0, 0, characterName.c_str(), accountName.c_str(), u"", u"", 0
    );
}

void WritePotentialSkillAudit(const CUser& user, int skillId, int skillLevel) {
    // Current CUser::SetAcquirePotentialSkill 0x005aa5c4 emits record 0x1fb4
    // for a newly inserted skill and for a strict level increase. The
    // formatter argument frame fixes this exact sparse layout.
    const std::u16string characterName = std::u16string(user.GetCharName());
    const std::u16string accountName = std::u16string(user.GetAccountName());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x1fb4, user.GetId(), user.GetAccountID(), 0, 0, user.GetX(), user.GetY(), user.GetZ(), nullptr, nullptr, nullptr,
        user.GetRace(), user.GetGender(), user.GetClass(), user.GetLevel(), user.GetSubjobId(), skillId, skillLevel, 0, 0, 0,
        characterName.c_str(), accountName.c_str(), nullptr, nullptr, 0
    );
}

} // namespace

std::atomic<int> CUser::s_loggedUsers{0};

L2_DEFINE_OBJECT_CLASS_ID(CUser)

L2_DEFINE_OBJECT_CLASS_NAME(CUser, m_pClassName_CUser)

L2_DEFINE_OBJECT_NEW(CUser)

// Native-container size differs from the shipping class-size metadata.
void* CUser::operator new(std::size_t requestedSize, bool objectAllocation) {
    // Current allocator 0x005bf548 publishes target size 0x2630. CRT
    // initializer 0x006a8180 assigns class ID 29.
    return runtime::process::AllocateMemoryObject(
        requestedSize, objectAllocation, L2_MEMORY_CLASS_ID(CUser), m_pClassName_CUser, "CUser", 0x2630
    );
}

L2_DEFINE_OBJECT_RELEASE(CUser)

bool CUser::CheckCharacterName(const wchar_t* characterName, int country, bool allowLongName) {
    L2_GUARD_FUNCTION();

    // Current 0x0059cf48 operates on Windows UTF-16 code units. All admitted
    // non-ASCII ranges are in the BMP, so a 32-bit host wchar_t above 0xffff
    // represents target surrogate units that the shipping table rejects.
    if (!characterName)
        return false;
    std::size_t targetLength = 0;
    for (const wchar_t* cursor = characterName; *cursor != L'\0'; ++cursor) {
        if (static_cast<std::uint32_t>(*cursor) > 0xffffU) {
            return false;
        }
        ++targetLength;
    }

    const int maximumWeight = allowLongName || country == 5 ? 24 : 16;
    if (targetLength == 0 || targetLength > static_cast<std::size_t>(maximumWeight)) {
        return false;
    }

    int weight = 0;
    for (const wchar_t* cursor = characterName; *cursor != L'\0'; ++cursor) {
        if (weight > maximumWeight)
            return false;

        const std::uint16_t character = static_cast<std::uint16_t>(*cursor);
        if ((character >= u'0' && character <= u'9') || (character >= u'a' && character <= u'z') ||
            (character >= u'A' && character <= u'Z')) {
            ++weight;
            continue;
        }

        bool accepted = false;
        int characterWeight = 0;
        switch (country) {
        case 0:
            accepted = character >= 0xac00U && character <= 0xd7a3U;
            characterWeight = 2;
            break;
        case 2:
        case 3:
        case 4:
            accepted = (character >= 0x3041U && character <= 0x3096U) || (character >= 0x30a1U && character <= 0x30fcU) ||
                (character >= 0x4e00U && character <= 0x9fa5U);
            characterWeight = 2;
            break;
        case 5:
            accepted = (character >= 0x0e01U && character <= 0x0e3aU) || (character >= 0x0e3fU && character <= 0x0e5bU);
            characterWeight = 2;
            break;
        case 8:
            accepted = character >= 0x0410U && character <= 0x044fU;
            characterWeight = 1;
            break;
        case 10:
        case 11:
        case 13:
            accepted = character >= 0x0100U && character <= 0x017fU;
            characterWeight = 1;
            break;
        case 12:
            switch (character) {
            case 0x0104U:
            case 0x0105U:
            case 0x0106U:
            case 0x0107U:
            case 0x0118U:
            case 0x0119U:
            case 0x0141U:
            case 0x0142U:
            case 0x0143U:
            case 0x0144U:
            case 0x00d3U:
            case 0x00f3U:
            case 0x015aU:
            case 0x015bU:
            case 0x0179U:
            case 0x017aU:
            case 0x017bU:
            case 0x017cU:
                accepted = true;
                break;
            default:
                break;
            }
            characterWeight = 1;
            break;
        default:
            break;
        }
        if (!accepted)
            return false;
        weight += characterWeight;
    }
    return weight <= maximumWeight;
}

int CUser::GetEventData(int eventId) {
    // Current Cached.exe 0x005b59a8.
    L2_GUARD_FUNCTION();

    const auto cached = m_eventData.find(eventId);
    if (cached != m_eventData.end()) {
        return cached->second;
    }

    int eventTime = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&eventTime);
    constexpr const wchar_t* statement = L"{Call lin_BrLoadUserEventData (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&eventId);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        m_eventData[eventId] = eventTime;
        return eventTime;
    }
    return 0;
}

void CUser::SetEventData(int eventId, int eventTime) {
    // Current Cached.exe 0x005b5b9c.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    m_eventData[eventId] = eventTime;
    constexpr const wchar_t* statement = L"{Call lin_BrUpdateUserEventData (?, ?, ?) }";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&eventId);
    connection.BindParameter(&eventTime);
    connection.ExecutePrepared(statement);
}

CUser::CUser(const UserInitialState& state) {
    L2_GUARD_FUNCTION();
    InitAtConstructor(state);
    SetAtConstructor(static_cast<INCLUDE_WAREHOUSE>(0), state);
}

CUser::~CUser() = default;

void CUser::InitAtConstructor(const UserInitialState& state) {
    L2_GUARD_FUNCTION();
    m_characterNameLow = TargetLowercase(state.characterName);
    m_characterName = state.characterName;
    m_accountName = state.accountName;
    m_characterId = state.characterId;
    m_accountId = state.accountId;
    InitializeSSQInfo();
    m_loadedInfo.jobGroup = -1;
    for (AbnormalStatus& status : m_abnormalStatus) {
        status.skillId = -1;
    }
    // Current InitAtConstructor 0x005be5bc allocates CVitality(0x50) with
    // the character DB ID and world ID, then stores the raw pointer at
    // CUser +0x25d0.
    m_vitality = new CVitality(static_cast<int>(m_characterId), state.worldId);
}

void CUser::SetAtConstructor(INCLUDE_WAREHOUSE includeWarehouse, const UserInitialState& state) {
    L2_GUARD_FUNCTION();

    Set(state);
    SetSlot(state.equipment);

    m_deletionTime.wYear = static_cast<std::uint16_t>(state.deletionTime.year);
    m_deletionTime.wMonth = static_cast<std::uint16_t>(state.deletionTime.month);
    m_deletionTime.wDay = static_cast<std::uint16_t>(state.deletionTime.day);
    m_deletionTime.wHour = static_cast<std::uint16_t>(state.deletionTime.hour);
    m_deletionTime.wMinute = static_cast<std::uint16_t>(state.deletionTime.minute);
    m_deletionTime.wSecond = static_cast<std::uint16_t>(state.deletionTime.second);
    m_nickname = state.nickname;
    m_powerFlags = state.powerFlags;
    m_pledgeDismissTime = state.pledgeDismissTime;
    m_pledgeOustedTime = state.pledgeOustedTime;
    m_pledgeWithdrawTime = state.pledgeWithdrawTime;

    // Cached.exe receives the adjacent legacy surrender value but never
    // reads it in constructor 0x005be7c4.

    if (static_cast<int>(includeWarehouse) == 0) {
        InstallWarehouse(m_inventory, m_characterId, WareHouseType_Inventory);
        InstallWarehouse(m_warehouse, m_characterId, WareHouseType_CharWarehouse);
        InstallWarehouse(m_manor, m_characterId, WareHouseType_Manor);
        InstallWarehouse(m_mail, m_characterId, WareHouseType_Mail);
        ItemCheckOnLoad();
    }

    m_useTime = state.useTime;
    m_lastUpdateTick = runtime::GetTickCount();
    m_previousExperience = state.experience;
    // Target SetAtConstructor clears CUser+0x2600..+0x2627 and the adjacent
    // dirty byte. The recovered portable members in that span are the two
    // current matching-point values and their change flag.
    m_pledgeGamePoint = 0;
    m_olympiadPoint = 0;
    m_matchingPointChanged = false;
}

void CUser::SetPos(int x, int y, int z) {
    // Current Cached.exe 0x0059de14.
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(m_dataLock);
        m_x = x;
        m_y = y;
        m_z = z;
    }
}

void CUser::Set(const UserInitialState& state) {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_dataLock);
    m_pledgeId = state.pledgeId;
    m_builder = state.builder;
    m_gender = state.gender;
    m_race = state.race;
    m_characterClass = state.characterClass;
    m_worldId = state.worldId;
    m_x = state.x;
    m_y = state.y;
    m_z = state.z;
    m_inVehicle = state.inVehicle;
    m_hp = state.hp;
    m_mp = state.mp;
    m_maxHp = state.maxHp;
    m_maxMp = state.maxMp;
    m_cp = state.cp;
    m_maxCp = state.maxCp;
    m_sp = state.sp;
    m_experience = state.experience;
    m_level = state.level;
    m_highestExperience = state.highestExperience;
    m_highestLevel = state.highestLevel;
    m_alignment = state.alignment;
    m_pkCount = state.pkCount;
    m_duelCount = state.duelCount;
    m_pkPardon = state.pkPardon;
    // Current Set 0x005a11fd..0x005a1220 stores SQL appearance lanes
    // 3/2/1 at CUser +0x428/+0x42c/+0x430. State keeps SQL column order.
    // the recovered member names must not erase the target permutation.
    m_face = state.hairColor;
    m_hairShape = state.hairShape;
    m_hairColor = state.face;
    m_subjobId = state.subjobId;
    m_subjobClass = state.subjobClass;
    m_dualClassId = state.dualClassId;
    m_ssqDawnRound = state.ssqDawnRound;
    m_characterNameColor = state.characterNameColor;
    m_nicknameColor = state.nicknameColor;
    m_pledgeType = state.pledgeType;
    m_gradeId = state.gradeId;
    m_academyPledgeId = state.academyPledgeId;
    SetAP(state.abilityPoints);
}

void CUser::SetChanged(
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
) {
    L2_GUARD_FUNCTION();

    const auto applyChanges = [&] {
        m_pledgeId = pledgeId;
        m_builder = builder;
        m_gender = gender;
        m_subjobId = subjobId;
        m_subjobClass = {
            subjobClass0,
            subjobClass1,
            subjobClass2,
            subjobClass3,
        };
        m_dualClassId = dualClassId;
        m_race = race;
        m_characterClass = characterClass;
        m_x = x;
        m_y = y;
        m_z = z;
        m_inVehicle = inVehicle != 0;
        m_hp = hp;
        m_mp = mp;
        m_maxHp = maxHp;
        m_maxMp = maxMp;
        m_cp = cp;
        m_maxCp = maxCp;
        m_sp = sp;
        m_experience = experience;
        SetAP(abilityPoints);
        m_level = level;
        m_alignment = alignment;
        m_pkCount = pkCount;
        m_duelCount = duelCount;
        m_pkPardon = pkPardon;
        m_face = face;
        m_hairShape = hairShape;
        m_hairColor = hairColor;
        m_ssqDawnRound = ssqDawnRound;
    };
    if (!alreadyLocked) {
        L2_WRITE_GUARD(m_dataLock);
        applyChanges();
    } else {
        applyChanges();
    }
}

void CUser::SetSlot(const UserEquipmentSlots& equipment) {
    L2_GUARD_FUNCTION();
    m_equipment = equipment;
}

std::uint32_t CUser::GetId() const noexcept {
    // Current Cached.exe 0x0059cd10 reads dword CUser+0x260.
    return m_characterId;
}

std::int32_t CUser::GetAccountID() const noexcept {
    // Current Cached.exe 0x0059cd30 reads dword CUser+0x294.
    return m_accountId;
}

int CUser::GetWorld() const noexcept {
    // Current Cached.exe 0x0059cd60 reads dword CUser+0x2ac.
    return m_worldId;
}

const char16_t* CUser::GetCharName() const noexcept {
    // Current Cached.exe 0x0059cd20 returns CUser+0x230.
    return m_characterName.c_str();
}

const char16_t* CUser::GetCharNameLow() const noexcept {
    // Current Cached.exe 0x0059cd18 returns CUser+0x200.
    return m_characterNameLow.c_str();
}

const char16_t* CUser::GetAccountName() const noexcept {
    // Current Cached.exe 0x0059cd28 returns CUser+0x264.
    return m_accountName.c_str();
}

int CUser::GetSubjobId() const noexcept { return m_subjobId; }

int CUser::GetDualClassId() const noexcept {
    // Semantic portable accessor for the direct CUser+0x24d4 read in current
    // RequestLogCharInfo 0x0046b060. No separate out-of-line target symbol
    // survives for this access.
    return m_dualClassId;
}

std::uint32_t CUser::GetPledgeID() const noexcept {
    // Current inline accessor 0x0059cd38. The name is kept by C1.
    return m_pledgeId;
}

int CUser::GetAllianceID() {
    // Current Cached.exe 0x005a1544. The source identifier survives in the
    // carved PDB. The target resolves the resident pledge in the active
    // world and returns zero when either the pledge ID or object is absent.
    if (m_pledgeId == 0)
        return 0;
    CPledgeSP pledge = g_pledgeDb.GetPledge(static_cast<int>(m_pledgeId), false, runtime::process::CurrentDatabaseId());
    return pledge ? static_cast<int>(pledge->allianceId()) : 0;
}

PledgeType CUser::GetPledgeType() const noexcept {
    // Exact PDB identifier. Current inline accessor 0x0059cf38 returns
    // CUser +0x60c.
    return static_cast<PledgeType>(m_pledgeType);
}

int CUser::GetBuilder() const noexcept {
    // Current inline accessor 0x0059cd40. The name is kept by C1.
    return m_builder;
}

int CUser::GetLevel() const noexcept {
    // Current inline accessor 0x0059ce44 returns CUser +0x308.
    return m_level;
}

int CUser::GetLevelByExp(std::int64_t experience) {
    // Current Cached.exe 0x0059ef60 scans the exact 122-entry table owned by
    // CReadFile. Preserve its unusual zero/exhausted-table result of one.
    L2_GUARD_FUNCTION();

    int thresholdIndex = 0;
    int level = 1;
    if (experience != 0) {
        const std::int64_t* threshold = &g_ExpTable[0];
        do {
            level = thresholdIndex;
            if (experience < *threshold)
                break;
            ++threshold;
            ++thresholdIndex;
            level = 1;
        } while (threshold < &g_ExpTable[122]);
    }
    return level;
}

int CUser::GetGender() const noexcept {
    // Current inline accessor 0x0059cd48.
    return m_gender;
}

int CUser::GetRace() const noexcept {
    // Current inline accessor 0x0059cd50.
    return m_race;
}

int CUser::GetClass() const noexcept {
    // Current inline accessor 0x0059cd58.
    return m_characterClass;
}

int CUser::GetGradeId() const noexcept {
    // Exact PDB identifier. Current inline accessor 0x0059cf40 returns
    // CUser +0x614.
    return m_gradeId;
}

int CUser::GetX() const noexcept {
    // Current inline accessor 0x0059cd68.
    return m_x;
}

int CUser::GetY() const noexcept {
    // Current inline accessor 0x0059cd70.
    return m_y;
}

int CUser::GetZ() const noexcept {
    // Current inline accessor 0x0059cd78.
    return m_z;
}

double CUser::GetHP() const noexcept {
    // Current Cached.exe 0x0059cd80 reads double CUser+0x2c0.
    return m_hp;
}

double CUser::GetMP() const noexcept {
    // Current Cached.exe 0x0059cd8c reads double CUser+0x2c8.
    return m_mp;
}

double CUser::GetMaxHP() const noexcept {
    // Current 0x0059cd98 returns max HP when positive, then HP, then 1.0.
    if (m_maxHp > 0.0)
        return m_maxHp;
    return m_hp > 0.0 ? m_hp : 1.0;
}

double CUser::GetMaxMP() const noexcept {
    // Current 0x0059cdc4 uses the same fallback rule for MP.
    if (m_maxMp > 0.0)
        return m_maxMp;
    return m_mp > 0.0 ? m_mp : 1.0;
}

double CUser::GetCP() const noexcept { return m_cp; }

double CUser::GetMaxCP() const noexcept {
    // Current Cached.exe 0x0059cdf0 contains this observable shipping quirk:
    // a positive max-CP field selects max-MP, otherwise CP and finally 1.0.
    if (m_maxCp > 0.0)
        return m_maxMp;
    return m_cp > 0.0 ? m_cp : 1.0;
}

std::int64_t CUser::GetSP() const noexcept {
    // Current Cached.exe 0x0059ce24 reads qword CUser+0x2f0.
    return m_sp;
}

void CUser::SetSP(std::int64_t sp) noexcept {
    // Current Cached.exe 0x0059ce2c writes qword CUser+0x2f0. The current
    // image has no surviving symbol, so SetSP is the semantic symmetric name.
    m_sp = sp;
}

int CUser::GetAP() const noexcept {
    // Current Cached.exe 0x0059ce34 reads dword CUser+0x2f8. The current
    // image has no surviving symbol, so GetAP names the confirmed field role.
    return m_abilityPoints;
}

std::int64_t CUser::GetExp() const noexcept {
    // Current Cached.exe 0x0059ce3c reads qword CUser+0x300.
    return m_experience;
}

std::int64_t CUser::GetHighestExperience() const noexcept {
    // Current optimized accessor 0x0059ce4c returns CUser +0x310.
    return m_highestExperience;
}

int CUser::GetHighestLevel() const noexcept {
    // Current optimized accessor 0x0059ce54 returns CUser +0x318.
    return m_highestLevel;
}

int CUser::GetAlign() const noexcept {
    // Current Cached.exe 0x0059ce5c reads dword CUser+0x328.
    return m_alignment;
}

std::uint32_t CUser::GetPK() const noexcept {
    // Current Cached.exe 0x0059ce64 reads dword CUser+0x32c.
    return m_pkCount;
}

std::uint32_t CUser::GetDuel() const noexcept {
    // Current Cached.exe 0x0059ce6c reads dword CUser+0x330.
    return m_duelCount;
}

std::uint32_t CUser::GetPKPardon() const noexcept {
    // Current Cached.exe 0x0059ce74 reads dword CUser+0x334.
    return m_pkPardon;
}

bool CUser::IsCurrentSSQRound() const {
    // Current Cached.exe 0x0059ce7c compares CUser+0x24ec with
    // SevenSignQuestDB::Instance()+0x3c. Its source identifier is absent.
    return m_ssqInfo.roundNumber == SevenSignQuestDB::Instance().GetRoundNumber();
}

int CUser::GetMainClass() const noexcept {
    // LoadCharacterPacket reads target CUser +0xc70, class-array slot zero.
    // No distinct current symbol survives for this optimized accessor.
    return m_subjobClass[0];
}

const std::array<int, 4>& CUser::GetSubjobClasses() const noexcept {
    // GetCharactersPacket reads the four adjacent current target fields at
    // CUser +0xc70..+0xc7c directly. No distinct out-of-line symbol survives
    // for that source-level access.
    return m_subjobClass;
}

const UserEquipmentSlots& CUser::GetEquipment() const noexcept { return m_equipment; }

int CUser::GetFaceIndex() const noexcept { return m_face; }

int CUser::GetHairShapeIndex() const noexcept { return m_hairShape; }

int CUser::GetHairColorIndex() const noexcept { return m_hairColor; }

int CUser::GetFaceIndexEx() const noexcept { return m_faceIndexEx; }

int CUser::GetHairShapeIndexEx() const noexcept { return m_hairShapeIndexEx; }

int CUser::GetHairColorIndexEx() const noexcept { return m_hairColorIndexEx; }

bool CUser::GetHairAccessoryFlag() const noexcept { return m_hairAccessoryFlag; }

int CUser::GetPreviousServer() const noexcept { return m_previousServer; }

const char16_t* CUser::GetNameOrigin() const noexcept { return m_nameOrigin.data(); }

int CUser::GetJobGroup() const noexcept { return m_loadedInfo.jobGroup; }

int CUser::GetStatus() const noexcept { return m_loadedInfo.status; }

void CUser::SetStatus(int status) noexcept { m_loadedInfo.status = status; }

int CUser::GetBanEnd() const noexcept { return m_loadedInfo.banEnd; }

void CUser::SetBanEnd(int banEnd) noexcept { m_loadedInfo.banEnd = banEnd; }

int CUser::GetServiceFlag() const noexcept {
    // Current GetCharactersPacket directly reads CUser +0x24cc.
    return m_loadedInfo.serviceFlag;
}

int CUser::GetNoblessType() const noexcept { return m_noblessInfo.noblessType; }

int CUser::GetHeroType() const noexcept {
    // Current GetCharactersPacket directly reads CUser +0x5f4 after
    // LoadUserNobless refreshes the complete seven-column state.
    return m_noblessInfo.heroType;
}

int CUser::GetDeletedElapsedSecond() const {
    L2_GUARD_FUNCTION();
    if (m_deletionTime.wYear == 0)
        return -86400;

    const std::int32_t now = runtime::crt::Time32Now();
    // The target passes a zero-initialized tm, including tm_isdst == 0.
    const std::int32_t deletion =
        runtime::crt::SystemTimeToTime32(
            m_deletionTime);
    const double elapsed = std::difftime(static_cast<std::time_t>(now), static_cast<std::time_t>(deletion));
    return static_cast<int>(std::min(elapsed, static_cast<double>(GetCharacterDeleteWaitSeconds())));
}

std::int32_t CUser::GetAccountLastLogoutTime() {
    L2_GUARD_FUNCTION();
    CAccountSP account = CAccount::Load(static_cast<unsigned int>(m_accountId));
    if (!account) {
        // Log.Add(LOG_ERROR, "[User.cpp][9215] invalid account id[%d]", m_accountId)
        // Log.Add(LOG_ERROR,
        //     "[character] Cannot load account %d for character %u. Last logout time is unavailable.",
        //     m_accountId, m_characterId)
        L2_LOG_ERROR(Log,
            "[character] Cannot load account %d for character %u; last logout time is unavailable.",
            m_accountId, m_characterId);
        return -1;
    }
    return account->m_lastLogoutTime;
}

const SYSTEMTIME& CUser::GetLastLogoutTime() const noexcept {
    // Current GetCharactersPacket reads target CUser +0x630 directly after
    // invoking LoadLastLogout.
    return m_lastLogoutTime;
}

const SYSTEMTIME& CUser::GetLastLoginTime() const noexcept {
    // LoadCharacterPacket reads current target CUser +0x640 directly.
    return m_lastLoginTime;
}

SYSTEMTIME* CUser::GetBirthday() noexcept {
    L2_GUARD_FUNCTION();
    return &m_createDate;
}

const SYSTEMTIME* CUser::GetBirthday() const noexcept { return &m_createDate; }

bool CUser::IsLoggedIn() const noexcept { return m_loggedIn.load(std::memory_order_acquire); }

void CUser::PrintCharData() {
    // Current Cached.exe 0x0059ff40. GetMaxHP/GetMaxMP keep the target's
    // positive-max/current-value/1.0 fallback before the diagnostic record.
    L2_GUARD_FUNCTION();

    const std::string accountName = Utf16ToUtf8(m_accountName);
    const std::string characterName = Utf16ToUtf8(m_characterName);
    // Log.Add(
    //     LOG_NORMAL,
    //     "[PrintCharData] %s, %s, %d, %d, %d, %d, %d, %d, "
    //     "%d, %d, %d, %d, %d, %d, %f, %f, %f, %f, "
    //     "%lld, %lld, %d, %d, %d, %d, %d, %d, %d, %d",
    //     accountName.c_str(), characterName.c_str(), m_accountId, static_cast<int>(m_characterId),
    //     static_cast<int>(m_pledgeId), m_builder, m_gender, m_race, m_characterClass, m_worldId, m_x, m_y, m_z,
    //     m_inVehicle ? 1 : 0, m_hp, m_mp, GetMaxHP(), GetMaxMP(), static_cast<long long>(m_sp),
    //     static_cast<long long>(m_experience), m_level, m_alignment, static_cast<int>(m_pkCount),
    //     static_cast<int>(m_duelCount), static_cast<int>(m_pkPardon), m_hairColor, m_hairShape, m_face
    // )
    // Log.Add(LOG_NORMAL,
    //     "[character] Account %s (%d), character %s (%u): clan %u, builder %d, gender %d, race %d, class %d, world %d. Position (%d, %d, %d), in vehicle %d. HP %.6f, MP %.6f, max HP %.6f, max MP %.6f. SP %lld, experience %lld, level %d, alignment %d. PK %u, duels %u, pardons %u. Hair color %d, hair shape %d, face %d.",
    //     accountName.c_str(), m_accountId, characterName.c_str(), m_characterId,
    //     m_pledgeId, m_builder, m_gender, m_race, m_characterClass, m_worldId, m_x, m_y, m_z,
    //     m_inVehicle ? 1 : 0, m_hp, m_mp, GetMaxHP(), GetMaxMP(),
    //     static_cast<long long>(m_sp), static_cast<long long>(m_experience), m_level, m_alignment,
    //     m_pkCount, m_duelCount, m_pkPardon, m_hairColor, m_hairShape, m_face)
    L2_LOG_INFO(Log,
        "[character] Account %s (%d), character %s (%u): clan %u, builder %d, gender %d, race %d, class %d, world %d; position (%d, %d, %d), in vehicle %d; HP %.6f, MP %.6f, max HP %.6f, max MP %.6f; SP %lld, experience %lld, level %d, alignment %d; PK %u, duels %u, pardons %u; hair color %d, hair shape %d, face %d.",
        accountName.c_str(), m_accountId, characterName.c_str(), m_characterId,
        m_pledgeId, m_builder, m_gender, m_race, m_characterClass, m_worldId, m_x, m_y, m_z,
        m_inVehicle ? 1 : 0, m_hp, m_mp, GetMaxHP(), GetMaxMP(),
        static_cast<long long>(m_sp), static_cast<long long>(m_experience), m_level, m_alignment,
        m_pkCount, m_duelCount, m_pkPardon, m_hairColor, m_hairShape, m_face);
}

void CUser::PrintCharWarehouseData() {
    // Current Cached.exe 0x005a34cc creates warehouse 0 and then 1.
    // Its two later calls target the shared three-byte nullsub at
    // 0x0047b248, so no additional portable operation is introduced here.
    L2_GUARD_FUNCTION();
    CWareHouseSP inventory = GetWareHouse(0, true);
    CWareHouseSP warehouse = GetWareHouse(1, true);
}

void CUser::LoadUserNobless() {
    L2_GUARD_FUNCTION();

    UserNoblessInfo loaded{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&loaded.noblessType);
    connection.Bind(&loaded.heroType);
    connection.Bind(&loaded.winCount);
    connection.Bind(&loaded.previousPoint);
    connection.Bind(&loaded.olympiadPoint);
    connection.Bind(&loaded.matchCount);
    connection.Bind(&loaded.olympiadWinCount);

    constexpr const wchar_t* query = L"{Call lin_GetNoblessById (?)}";
    connection.Prepare(query);
    connection.BindParameter(&m_characterId);
    if (connection.ExecutePrepared(query)) {
        connection.Fetch();
    }

    // The target overwrites all seven members even on Execute/Fetch failure.
    m_noblessInfo = loaded;
}

bool CUser::IsNobless() {
    L2_GUARD_FUNCTION();
    LoadUserNobless();
    return m_noblessInfo.noblessType == 1;
}

bool CUser::IsNoblessOrHonors() {
    L2_GUARD_FUNCTION();
    LoadUserNobless();
    return m_noblessInfo.noblessType == 1 || m_noblessInfo.noblessType == 2;
}

bool CUser::IsHero() {
    L2_GUARD_FUNCTION();
    LoadUserNobless();
    // return m_noblessInfo.heroType == 2.
    return m_noblessInfo.heroType == HERO_ACTIVE;
}

bool CUser::Delete() {
    L2_GUARD_FUNCTION();

    std::uint32_t itemId = 0;
    DBConn deletion(runtime::process::CurrentDatabaseId());
    deletion.Bind(&itemId);
    constexpr const wchar_t* deleteQuery = L"{Call lin_DeleteChar (?, 1)}";
    deletion.Prepare(deleteQuery);
    deletion.BindParameter(&m_characterId);
    if (!deletion.ExecutePrepared(deleteQuery))
        return false;

    const std::u16string characterName = std::u16string(m_characterName.c_str());
    const std::u16string accountName = std::u16string(m_accountName.c_str());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x33e, m_characterId, m_accountId, 0, 0, m_x, m_y, m_z, u"", u"", u"", m_race, m_gender, m_characterClass, m_level,
        0, 0, 0, 0, 0, 0, characterName.c_str(), accountName.c_str(), u"", u"", 0
    );

    while (deletion.Fetch()) {
        CItemSP item = CItem::Load(itemId);
        if (item && item->Delete(true, false)) {
            item->CopyByDeleteChar();
        }
    }

    CPledgeSP pledge = g_pledgeDb.GetPledge(static_cast<int>(m_pledgeId), false, runtime::process::CurrentDatabaseId());
    if (pledge) {
        pledge->RemoveMember(m_characterId, static_cast<PledgeType>(m_pledgeType));
    }
    m_pledgeId = 0;

    DBConn blockList(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* clearBlockList = L"{Call lin_ClearDeletedCharBlockList (?)}";
    blockList.Prepare(clearBlockList);
    blockList.BindParameter(&m_characterId);
    blockList.ExecutePrepared(clearBlockList);
    return true;
}

bool CUser::SetDeleted() {
    L2_GUARD_FUNCTION();

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&year);
    connection.Bind(&month);
    connection.Bind(&day);
    connection.Bind(&hour);
    connection.Bind(&minute);
    connection.Bind(&second);
    constexpr const wchar_t* statement = L"{Call lin_SetCharacterDelete (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    if (!connection.ExecutePrepared(statement))
        return false;

    // The current body writes log 0x327 after Execute and before Fetch. A
    // missing result row therefore still produces the deletion-attempt log.
    const std::u16string characterName = std::u16string(m_characterName.c_str());
    const std::u16string accountName = std::u16string(m_accountName.c_str());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x327, m_characterId, m_accountId, 0, 0, 0, 0, 0, u"", u"", u"", m_race, m_gender, m_characterClass, m_level, 0, 0,
        0, 0, 0, 0, characterName.c_str(), accountName.c_str(), u"", u"", 0
    );

    if (!connection.Fetch())
        return false;

    // Current +0x45c is SYSTEMTIME. The procedure returns every component
    // except day-of-week and milliseconds, which keep their old values.
    m_deletionTime.wYear = static_cast<std::uint16_t>(year);
    m_deletionTime.wMonth = static_cast<std::uint16_t>(month);
    m_deletionTime.wDay = static_cast<std::uint16_t>(day);
    m_deletionTime.wHour = static_cast<std::uint16_t>(hour);
    m_deletionTime.wMinute = static_cast<std::uint16_t>(minute);
    m_deletionTime.wSecond = static_cast<std::uint16_t>(second);
    return true;
}

bool CUser::RestoreDeleted() {
    // Current Cached.exe 0x005ad844.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetCharacterDeleteRestore (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    const bool restored = connection.ExecutePrepared(statement);
    if (!restored)
        return false;

    // The target clears year/month and day/hour/minute/second. Its two stores
    // leave day-of-week and milliseconds unchanged.
    m_deletionTime.wYear = 0;
    m_deletionTime.wMonth = 0;
    m_deletionTime.wDay = 0;
    m_deletionTime.wHour = 0;
    m_deletionTime.wMinute = 0;
    m_deletionTime.wSecond = 0;

    const std::u16string characterName = std::u16string(m_characterName.c_str());
    const std::u16string accountName = std::u16string(m_accountName.c_str());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x33d, m_characterId, m_accountId, 0, 0, m_x, m_y, m_z, u"", u"", u"", m_race, m_gender, m_characterClass, m_level,
        0, 0, 0, 0, 0, 0, characterName.c_str(), accountName.c_str(), u"", u"", 0
    );
    return true;
}

void CUser::ReadLock(const wchar_t* file, int line) {
    // Current Cached.exe 0x0059f014 forwards to RWLock at CUser +0x1a8.
    L2_GUARD_FUNCTION();
    m_dataLock.ReadLock(file, line);
}

void CUser::ReadUnlock() {
    // Current Cached.exe 0x0059f0d0 forwards to the same RWLock owner.
    L2_GUARD_FUNCTION();
    m_dataLock.ReadUnlock();
}

void CUser::WriteLock(const wchar_t* file, int line) {
    // Current Cached.exe 0x0059f178 forwards both caller arguments to the
    // embedded RWLock at CUser +0x1a8.
    L2_GUARD_FUNCTION();
    m_dataLock.WriteLock(file, line);
}

void CUser::WriteUnlock() {
    L2_GUARD_FUNCTION();
    m_dataLock.WriteUnlock();
}

void CUser::SetAddr(int first, int second, int third, int fourth) {
    L2_GUARD_FUNCTION();
    // Current 0x005a0348 stores only the low byte of each int argument.
    m_sessionAddress[0] = static_cast<std::uint8_t>(first);
    m_sessionAddress[1] = static_cast<std::uint8_t>(second);
    m_sessionAddress[2] = static_cast<std::uint8_t>(third);
    m_sessionAddress[3] = static_cast<std::uint8_t>(fourth);
}

std::u16string CUser::GetMacAddressString() const {
    L2_GUARD_FUNCTION();
    return UserMacAddressText(m_sessionMacAddress);
}

void CUser::SetPledge(int pledgeId, PledgeType pledgeType) {
    L2_GUARD_FUNCTION();

    // Current 0x005a7d44 publishes both fields before attempting SQL and does
    // not roll either value back if persistence fails.
    {
        L2_WRITE_GUARD(m_dataLock);
        m_pledgeId = static_cast<std::uint32_t>(pledgeId);
        m_pledgeType = static_cast<int>(pledgeType);
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveCharacterPledge (?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_pledgeId);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&pledgeType);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Error, Cannot save user pledge id.[%u]user "
        //     "[%u]pledge [%d] pledge type",
        //     m_characterId, m_pledgeId, static_cast<int>(pledgeType)
        // )
        // Log.Add(LOG_ERROR,
        //     "[character] Cannot persist clan %u (type %d) for character %u. The clan remains changed in memory.",
        //     m_pledgeId, static_cast<int>(pledgeType), m_characterId)
        L2_LOG_ERROR(Log,
            "[character] Cannot persist clan %u (type %d) for character %u; the clan remains changed in memory.",
            m_pledgeId, static_cast<int>(pledgeType), m_characterId);
    }
}

bool CUser::GetReloadFlag() {
    // Current Cached.exe 0x0059d668 reads the byte at CUser +0x25d8.
    L2_GUARD_FUNCTION();
    return m_reloadFlag;
}

void CUser::SetReloadFlag(bool reloadFlag) {
    L2_GUARD_FUNCTION();
    m_reloadFlag = reloadFlag;
}

void CUser::SetLoggedIn(bool loggedIn) {
    L2_GUARD_FUNCTION();
    m_loggedIn.store(loggedIn, std::memory_order_release);
}

void CUser::WriteLogin(int characterId) {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    if (std::getenv("L2_QUEST_TRACE") != nullptr) {
        // std::fprintf(
        //     stderr,
        //     "[QUEST_TRACE] CUser::WriteLogin character=%d object_id=%u "
        //     "world=%d was_logged_in=%d db_valid=%d\n",
        //     characterId,
        //     m_characterId,
        //     runtime::process::CurrentDatabaseId(),
        //     m_loggedIn ? 1 : 0,
        //     connection.IsValid() ? 1 : 0)
        // Log.Add(LOG_NORMAL,
        //     "[quest trace] Login write requested for character %d (cached character %u) in database %d. Already logged in: %s. DB connection valid: %s.",
        //     characterId, m_characterId, runtime::process::CurrentDatabaseId(),
        //     m_loggedIn ? "yes" : "no", connection.IsValid() ? "yes" : "no")
        L2_LOG_INFO(Log,
            "[quest trace] Login write requested for character %d (cached character %u) in database %d; already logged in: %s; DB connection valid: %s.",
            characterId, m_characterId, runtime::process::CurrentDatabaseId(),
            m_loggedIn ? "yes" : "no", connection.IsValid() ? "yes" : "no");
    }
    if (m_loggedIn) {
        // Log.Add(
        //     LOG_ERROR, "Invalid WriteLogin %ls(%d), %ls(%d)", m_characterName.c_str(), characterId,
        //     m_accountName.c_str(), m_accountId
        // )
        // Log.Add(LOG_WARN,
        //     L"[character] Ignoring duplicate login write for %ls (%d), account %ls (%d): already logged in.",
        //     m_characterName.c_str(), characterId, m_accountName.c_str(), m_accountId)
        // L2_LOG_WARN(Log,
        //     L"[character] Ignoring duplicate login write for %ls (%d), account %ls (%d): already logged in.",
        //     m_characterName.c_str(), characterId, m_accountName.c_str(), m_accountId)
        L2_LOG_WARN(
            Log, u"[character] Ignoring duplicate login write for %ls (%d), account %ls (%d): already logged in.",
            m_characterName.c_str(), characterId, m_accountName.c_str(), m_accountId
        );
        return;
    }

    constexpr const wchar_t* statement = L"{Call lin_CharLogin (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    const bool loginPersisted = connection.ExecutePrepared(statement);
    if (std::getenv("L2_QUEST_TRACE") != nullptr) {
        // std::fprintf(
        //     stderr,
        //     "[QUEST_TRACE] CUser::WriteLogin SQL character=%d success=%d\n",
        //     characterId,
        //     loginPersisted ? 1 : 0)
        // Log.Add(LOG_NORMAL,
        //     "[quest trace] Login database write for character %d %s.",
        //     characterId, loginPersisted ? "succeeded" : "failed")
        L2_LOG_INFO(Log,
            "[quest trace] Login database write for character %d %s.",
            characterId, loginPersisted ? "succeeded" : "failed");
    }
    // if (!loginPersisted)
    //     return.
    if (!loginPersisted) {
        // Log.Add(LOG_ERROR,
        //     "[character] Cannot persist login for character %d (cached character %u), account %d. Login index was not updated.",
        //     characterId, m_characterId, m_accountId)
        L2_LOG_ERROR(Log,
            "[character] Cannot persist login for character %d (cached character %u), account %d; login index was not updated.",
            characterId, m_characterId, m_accountId);
        return;
    }

    // These are three separate GetTickCount calls in current 0x005a8288.
    m_userLogTick = runtime::GetTickCount();
    m_loginTick = runtime::GetTickCount();
    m_lastUpdateTick = runtime::GetTickCount();
    g_userDb.SetLoggedInIndex(this, true);
    const int loggedUsers = s_loggedUsers.fetch_add(1, std::memory_order_seq_cst) + 1;

    const std::u16string address = UserAddressText(m_sessionAddress);
    const std::u16string macAddress = GetMacAddressString();
    const std::u16string characterName = std::u16string(m_characterName.c_str());
    const std::u16string accountName = std::u16string(m_accountName.c_str());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x323, m_characterId, m_accountId, 0, 0, m_x, m_y, m_z, address.c_str(), macAddress.c_str(), u"", m_race, m_gender,
        m_characterClass, m_level, loggedUsers, m_adenaSum, m_adenaSum + m_defaultPriceSum, m_sessionLogSource, 0, 0,
        characterName.c_str(), accountName.c_str(), u"", u"", m_sessionLogFlag ? 1 : 0
    );

    const runtime::SystemTime localTime = runtime::GetLocalTimeNow();
    SetLastLogin(localTime.wYear, localTime.wMonth, localTime.wDay, localTime.wHour, localTime.wMinute, localTime.wSecond);
}

void CUser::WriteLogout(int characterId, int logoutReason) {
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    if (!m_loggedIn) {
        // Log.Add(
        //     LOG_ERROR, "Invalid WriteLogout %ls(%d), %ls(%d), (%d)", m_characterName.c_str(), characterId,
        //     m_accountName.c_str(), m_accountId, logoutReason
        // )
        // Log.Add(LOG_WARN,
        //     L"[character] Ignoring logout write for %ls (%d), account %ls (%d): not logged in (reason %d).",
        //     m_characterName.c_str(), characterId, m_accountName.c_str(), m_accountId, logoutReason)
        // L2_LOG_WARN(Log,
        //     L"[character] Ignoring logout write for %ls (%d), account %ls (%d): not logged in (reason %d).",
        //     m_characterName.c_str(), characterId, m_accountName.c_str(), m_accountId, logoutReason)
        L2_LOG_WARN(
            Log, u"[character] Ignoring logout write for %ls (%d), account %ls (%d): not logged in (reason %d).",
            m_characterName.c_str(), characterId, m_accountName.c_str(), m_accountId, logoutReason
        );
        return;
    }

    const std::uint32_t now = runtime::GetTickCount();
    const std::uint32_t usedSeconds = (now - m_loginTick) / 1000U;
    m_useTime += usedSeconds;

    constexpr const wchar_t* statement = L"{Call lin_CharLogout (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&usedSeconds);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR, "Fail leaveworld %ls(%d), %ls(%d), (%d)", m_characterName.c_str(), characterId,
        //     m_accountName.c_str(), m_accountId, logoutReason
        // )
        // Log.Add(LOG_ERROR,
        //     L"[character] Cannot persist logout for %ls (%d), account %ls (%d), reason %d. The logged-in index remains set.",
        //     m_characterName.c_str(), characterId, m_accountName.c_str(), m_accountId, logoutReason)
        // L2_LOG_ERROR(Log,
        //     L"[character] Cannot persist logout for %ls (%d), account %ls (%d), reason %d. The logged-in index remains set.",
        //     m_characterName.c_str(), characterId, m_accountName.c_str(), m_accountId, logoutReason)
        L2_LOG_ERROR(
            Log,
            u"[character] Cannot persist logout for %ls (%d), account %ls (%d), reason %d; the logged-in index remains set.",
            m_characterName.c_str(), characterId, m_accountName.c_str(), m_accountId, logoutReason
        );
    } else {
        const std::u16string address = UserAddressText(m_sessionAddress);
        const std::u16string macAddress = GetMacAddressString();
        const std::u16string characterName = std::u16string(m_characterName.c_str());
        const std::u16string accountName = std::u16string(m_accountName.c_str());
        CLogFormatter formatter;
        formatter.WriteLog(
            0x325, m_characterId, m_accountId, 0, 0, m_x, m_y, m_z, address.c_str(), macAddress.c_str(), u"", m_race,
            m_gender, m_characterClass, m_level, usedSeconds, m_adenaSum, m_adenaSum + m_defaultPriceSum, m_sessionLogValue1,
            m_sessionLogValue2, m_sessionLogSource, characterName.c_str(), accountName.c_str(), u"", u"",
            m_sessionLogFlag ? 1 : 0
        );
        g_userDb.SetLoggedInIndex(this, false);
        s_loggedUsers.fetch_sub(1, std::memory_order_seq_cst);
    }

    const runtime::SystemTime localTime = runtime::GetLocalTimeNow();
    SetLastLogout(localTime.wYear, localTime.wMonth, localTime.wDay, localTime.wHour, localTime.wMinute, localTime.wSecond);
}

void CUser::SetEquip(SlotType slotType, int itemId) {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_dataLock);

    switch (slotType) {
    case ST_UNDERWEAR:
        m_equipment.underwear = itemId;
        break;
    case ST_REAR:
        m_equipment.rightEar = itemId;
        break;
    case ST_LEAR:
        m_equipment.leftEar = itemId;
        break;
    case ST_NECK:
        m_equipment.neck = itemId;
        break;
    case ST_RFINGER:
        m_equipment.rightFinger = itemId;
        break;
    case ST_LFINGER:
        m_equipment.leftFinger = itemId;
        break;
    case ST_HEAD:
        m_equipment.head = itemId;
        break;
    case ST_RHAND:
        m_equipment.rightHand = itemId;
        break;
    case ST_LHAND:
        m_equipment.leftHand = itemId;
        break;
    case ST_GLOVES:
        m_equipment.gloves = itemId;
        break;
    case ST_CHEST:
        m_equipment.chest = itemId;
        break;
    case ST_LEGS:
        m_equipment.legs = itemId;
        break;
    case ST_FEET:
        m_equipment.feet = itemId;
        break;
    case ST_BACK:
        m_equipment.back = itemId;
        break;
    case ST_RLHAND:
        m_equipment.bothHand = itemId;
        break;
    case ST_ONEPIECE:
        m_equipment.chest = itemId;
        m_equipment.legs = itemId;
        break;
    case ST_HAIR:
        m_equipment.hair = itemId;
        break;
    case ST_ALLDRESS:
        m_equipment.chest = itemId;
        m_equipment.legs = itemId;
        m_equipment.feet = itemId;
        m_equipment.gloves = itemId;
        break;
    case ST_HAIR2:
        m_equipment.hair2 = itemId;
        break;
    case ST_HAIRALL:
        m_equipment.hair = itemId;
        m_equipment.hair2 = itemId;
        break;
    case ST_RBRACELET:
        m_equipment.rightBracelet = itemId;
        break;
    case ST_LBRACELET:
        m_equipment.leftBracelet = itemId;
        break;
    case ST_AGATHION_MAIN:
        m_equipment.agathionMain = itemId;
        break;
    case ST_AGATHION_SUB1:
    case ST_AGATHION_SUB2:
    case ST_AGATHION_SUB3:
    case ST_AGATHION_SUB4:
        m_equipment.agathionSub[static_cast<std::size_t>(slotType - ST_AGATHION_SUB1)] = itemId;
        break;
    case ST_DECO1:
    case ST_DECO2:
    case ST_DECO3:
    case ST_DECO4:
    case ST_DECO5:
    case ST_DECO6:
        m_equipment.deco[static_cast<std::size_t>(slotType - ST_DECO1)] = itemId;
        break;
    case ST_WAIST:
        m_equipment.waist = itemId;
        break;
    case ST_BROOCH:
        m_equipment.brooch = itemId;
        break;
    case ST_JEWEL1:
    case ST_JEWEL2:
    case ST_JEWEL3:
    case ST_JEWEL4:
    case ST_JEWEL5:
    case ST_JEWEL6:
        m_equipment.jewel[static_cast<std::size_t>(slotType - ST_JEWEL1)] = itemId;
        break;
    case ST_ARTIFACTBOOK:
        m_equipment.artifactBook = itemId;
        break;
    case ST_ARTIFACT_A1:
    case ST_ARTIFACT_A2:
    case ST_ARTIFACT_A3:
    case ST_ARTIFACT_A4:
    case ST_ARTIFACT_A5:
    case ST_ARTIFACT_A6:
    case ST_ARTIFACT_A7:
    case ST_ARTIFACT_A8:
    case ST_ARTIFACT_A9:
    case ST_ARTIFACT_A10:
    case ST_ARTIFACT_A11:
    case ST_ARTIFACT_A12:
        m_equipment.artifactA[static_cast<std::size_t>(slotType - ST_ARTIFACT_A1)] = itemId;
        break;
    case ST_ARTIFACT_B1:
    case ST_ARTIFACT_B2:
    case ST_ARTIFACT_B3:
        m_equipment.artifactB[static_cast<std::size_t>(slotType - ST_ARTIFACT_B1)] = itemId;
        break;
    case ST_ARTIFACT_C1:
    case ST_ARTIFACT_C2:
    case ST_ARTIFACT_C3:
        m_equipment.artifactC[static_cast<std::size_t>(slotType - ST_ARTIFACT_C1)] = itemId;
        break;
    case ST_ARTIFACT_D1:
    case ST_ARTIFACT_D2:
    case ST_ARTIFACT_D3:
        m_equipment.artifactD[static_cast<std::size_t>(slotType - ST_ARTIFACT_D1)] = itemId;
        break;
    default:
        // Log.Add(LOG_ERROR, "[%s][%d] unknown slot, [%d]", "User.cpp", 0x7ec, static_cast<int>(slotType))
        // Log.Add(LOG_WARN,
        //     "[character] Ignoring equipment slot %d for character %u (item %d): unknown slot.",
        //     static_cast<int>(slotType), m_characterId, itemId)
        L2_LOG_WARN(Log,
            "[character] Ignoring equipment slot %d for character %u (item %d): unknown slot.",
            static_cast<int>(slotType), m_characterId, itemId);
        break;
    }
}

bool CUser::Save() {
    L2_GUARD_FUNCTION();
    bool databaseStateSaved;
    {
        L2_WRITE_GUARD(m_dataLock);

        databaseStateSaved = false;
        {
            DBConn connection(runtime::process::CurrentDatabaseId());
            const int inVehicle = m_inVehicle ? 1 : 0;
            constexpr const wchar_t* saveCharacter = L"{Call lin_SaveCharacter (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,"
                                                     L"?,?,?,?,?,?,?,?,?,?,?,?)}";
            connection.Prepare(saveCharacter);
            connection.BindParameter(&m_subjobId);
            connection.BindParameter(&m_dualClassId);
            connection.BindParameter(&m_pledgeId);
            connection.BindParameter(&m_characterClass);
            connection.BindParameter(&m_x);
            connection.BindParameter(&m_y);
            connection.BindParameter(&m_z);
            connection.BindParameter(&inVehicle);
            connection.BindParameter(&m_hp);
            connection.BindParameter(&m_mp);
            connection.BindParameter(&m_maxHp);
            connection.BindParameter(&m_maxMp);
            connection.BindParameter(&m_sp);
            connection.BindParameter(&m_experience);
            connection.BindParameter(&m_level);
            connection.BindParameter(&m_alignment);
            connection.BindParameter(&m_pkCount);
            connection.BindParameter(&m_duelCount);
            connection.BindParameter(&m_pkPardon);
            // Current Save 0x005bb3d3..0x005bb3f3 passes +0x430/+0x42c/+0x428
            // to lin_SaveCharacter parameters 20/21/22, inverse to Set.
            connection.BindParameter(&m_hairColor);
            connection.BindParameter(&m_hairShape);
            connection.BindParameter(&m_face);
            connection.BindParameter(&m_ssqDawnRound);
            connection.BindParameter(&m_characterId);
            for (const int& subjobClass : m_subjobClass) {
                connection.BindParameter(&subjobClass);
            }
            connection.BindParameter(&m_cp);
            connection.BindParameter(&m_maxCp);

            if (!connection.ExecutePrepared(saveCharacter)) {
                // Log.Add(
                //     LOG_ERROR, "[%s][%d] Character save error[%ls : %d]", "User.cpp", 0x806, m_characterName.c_str(),
                //     static_cast<int>(m_characterId)
                // )
                // Log.Add(LOG_ERROR,
                //     L"[character] Cannot save base state for character %u (%ls).",
                //     m_characterId, m_characterName.c_str())
                // L2_LOG_ERROR(Log,
                //     L"[character] Cannot save base state for character %u (%ls).",
                //     m_characterId, m_characterName.c_str())
                L2_LOG_ERROR(
                    Log, u"[character] Cannot save base state for character %u (%ls).", m_characterId,
                    m_characterName.c_str()
                );
            } else {
                connection.ResetParam();
                constexpr const wchar_t* updateBeauty = L"{Call lin_UpdateUserBeauty(?,?,?,?)}";
                connection.Prepare(updateBeauty);
                connection.BindParameter(&m_characterId);
                connection.BindParameter(&m_faceIndexEx);
                connection.BindParameter(&m_hairShapeIndexEx);
                connection.BindParameter(&m_hairColorIndexEx);
                if (!connection.ExecutePrepared(updateBeauty)) {
                    // Log.Add(
                    //     LOG_ERROR, "[%s][%d] Character beauty save error[%ls : %d]", "User.cpp", 0x80e,
                    //     m_characterName.c_str(), static_cast<int>(m_characterId)
                    // )
                    // Log.Add(LOG_ERROR,
                    //     L"[character] Base state saved, but cannot save extended appearance for character %u (%ls).",
                    //     m_characterId, m_characterName.c_str())
                    // L2_LOG_ERROR(Log,
                    //     L"[character] Base state saved, but cannot save extended appearance for character %u (%ls).",
                    //     m_characterId, m_characterName.c_str())
                    L2_LOG_ERROR(
                        Log, u"[character] Base state saved, but cannot save extended appearance for character %u (%ls).",
                        m_characterId, m_characterName.c_str()
                    );
                } else {
                    databaseStateSaved = true;
                }
            }
        }
    }
    return databaseStateSaved && SaveSlot();
}

bool CUser::SaveAP(int abilityPoints) {
    // Current Cached.exe 0x005a7b80.
    L2_GUARD_FUNCTION();

    std::optional<DBConn> connectionStorage;
    bool saved;
    {
        L2_WRITE_GUARD(m_dataLock);
        auto& connection = connectionStorage.emplace(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_SaveCharacterAP (?,?)}";
        connection.Prepare(statement);
        connection.BindParameter(&m_characterId);
        connection.BindParameter(&abilityPoints);
        saved = connection.ExecutePrepared(statement);
        if (saved) {
            SetAP(abilityPoints);
        } else {
            // Log.Add(LOG_ERROR, "CharacterAP save error[%ls:%u]", m_characterName.c_str(), m_characterId)
            // Log.Add(LOG_ERROR,
            //     L"[character] Cannot save %d ability points for character %u (%ls).",
            //     abilityPoints, m_characterId, m_characterName.c_str())
            // L2_LOG_ERROR(Log,
            //     L"[character] Cannot save %d ability points for character %u (%ls).",
            //     abilityPoints, m_characterId, m_characterName.c_str())
            L2_LOG_ERROR(
                Log, u"[character] Cannot save %d ability points for character %u (%ls).", abilityPoints, m_characterId,
                m_characterName.c_str()
            );
        }
    }
    return saved;
}

bool CUser::SaveSlot() {
    L2_GUARD_FUNCTION();

    // The SQL parameter order differs from the packet order.
    // Match BindEquipment in UserDb.cpp when saving these fields.
    const std::array<int, 60> slots{
        m_equipment.underwear, m_equipment.rightEar, m_equipment.leftEar,
        m_equipment.neck, m_equipment.rightFinger, m_equipment.leftFinger,
        m_equipment.head, m_equipment.rightHand, m_equipment.leftHand,
        m_equipment.gloves, m_equipment.chest, m_equipment.legs,
        m_equipment.feet, m_equipment.back, m_equipment.bothHand,
        m_equipment.hair, m_equipment.hair2,
        m_equipment.rightBracelet, m_equipment.leftBracelet,
        m_equipment.agathionMain,
        m_equipment.agathionSub[0], m_equipment.agathionSub[1],
        m_equipment.agathionSub[2], m_equipment.agathionSub[3],
        m_equipment.artifactBook,
        m_equipment.artifactA[0], m_equipment.artifactA[1],
        m_equipment.artifactA[2], m_equipment.artifactA[3],
        m_equipment.artifactA[4], m_equipment.artifactA[5],
        m_equipment.artifactA[6], m_equipment.artifactA[7],
        m_equipment.artifactA[8], m_equipment.artifactA[9],
        m_equipment.artifactA[10], m_equipment.artifactA[11],
        m_equipment.artifactB[0], m_equipment.artifactB[1], m_equipment.artifactB[2],
        m_equipment.artifactC[0], m_equipment.artifactC[1], m_equipment.artifactC[2],
        m_equipment.artifactD[0], m_equipment.artifactD[1], m_equipment.artifactD[2],
        m_equipment.deco[0], m_equipment.deco[1], m_equipment.deco[2],
        m_equipment.deco[3], m_equipment.deco[4], m_equipment.deco[5],
        m_equipment.waist, m_equipment.brooch,
        m_equipment.jewel[0], m_equipment.jewel[1], m_equipment.jewel[2],
        m_equipment.jewel[3], m_equipment.jewel[4], m_equipment.jewel[5],
    };

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveCharacterSlot "
                                         L"(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,"
                                         L"?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    for (const int& slot : slots) {
        connection.BindParameter(&slot);
    }
    if (connection.ExecutePrepared(statement))
        return true;

    // Log.Add(LOG_ERROR, "Character Slot save error[%ls:%u]", m_characterName.c_str(), m_characterId)
    // Log.Add(LOG_ERROR,
    //     L"[character] Cannot save equipment slots for character %u (%ls).",
    //     m_characterId, m_characterName.c_str())
    // L2_LOG_ERROR(Log,
    //     L"[character] Cannot save equipment slots for character %u (%ls).",
    //     m_characterId, m_characterName.c_str())
    L2_LOG_ERROR(
        Log, u"[character] Cannot save equipment slots for character %u (%ls).", m_characterId, m_characterName.c_str()
    );
    return false;
}

void CUser::SaveSummonInfo(
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
) {
    // Current Cached.exe 0x005a7f0c.
    L2_GUARD_FUNCTION();

    std::optional<DBConn> connectionStorage;
    {
        L2_WRITE_GUARD(m_dataLock);
        auto& connection = connectionStorage.emplace(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_UpdateUserSummon (?,?,?,?,?,?,?,?,?,?,?,?)}";
        connection.Prepare(statement);
        connection.BindParameter(&m_characterId);
        connection.BindParameter(&summonClassId);
        connection.BindParameter(&summonDbId);
        connection.BindParameter(&summonLevel);
        connection.BindParameter(&summonOriginalRemainTime);
        connection.BindParameter(&summonRemainTime);
        connection.BindParameter(&summonExperienceRate);
        connection.BindParameter(&summonHp);
        connection.BindParameter(&summonMp);
        connection.BindParameter(&summonConsumeItemId);
        connection.BindParameter(&summonConsumeItemCount);
        connection.BindParameter(&summonIsSummoned);
        if (!connection.ExecutePrepared(statement)) {
            // Log.Add(
            //     LOG_ERROR,
            //     "Error, Cannot save user summon. user[%d] "
            //     "[%d, %d, %d, %d, %d]",
            //     static_cast<int>(m_characterId), summonClassId, summonDbId, summonLevel, summonOriginalRemainTime,
            //     summonRemainTime
            // )
            // Log.Add(LOG_ERROR,
            //     "[summon] Cannot save summon %d (class %d, level %d) for character %u. Original remaining time %d, current remaining time %d.",
            //     summonDbId, summonClassId, summonLevel, m_characterId, summonOriginalRemainTime, summonRemainTime)
            L2_LOG_ERROR(Log,
                "[summon] Cannot save summon %d (class %d, level %d) for character %u; original remaining time %d, current remaining time %d.",
                summonDbId, summonClassId, summonLevel, m_characterId, summonOriginalRemainTime, summonRemainTime);
        }
    }
}

void CUser::ReloadHighestExpAndLevel() {
    L2_GUARD_FUNCTION();

    long long highestExperience = 0;
    int highestLevel = 0;
    {
        L2_WRITE_GUARD(m_dataLock);
        {
            DBConn connection(runtime::process::CurrentDatabaseId());
            connection.Bind(&highestExperience);
            connection.Bind(&highestLevel);
            const int characterId = static_cast<int>(m_characterId);
            constexpr const wchar_t* statement = L"{Call lin_ReloadUserHighestExpAndLevel (?)}";
            connection.Prepare(statement);
            connection.BindParameter(&characterId);
            if (connection.ExecutePrepared(statement)) {
                connection.Fetch();
                m_highestExperience = highestExperience;
                m_highestLevel = highestLevel;
            }
        }
    }
}

bool CUser::AddUserLog(int logId, int logFrom, int logTo, std::uint32_t useTime, int sessionUseTime [[maybe_unused]]) {
    L2_GUARD_FUNCTION();

    m_userLogTick = runtime::GetTickCount();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_AddUserLog (?,?,?,?,?,-1)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&logId);
    connection.BindParameter(&logFrom);
    connection.BindParameter(&logTo);
    connection.BindParameter(&useTime);
    return connection.ExecutePrepared(statement);
}

bool CUser::AddUserLog2(int logId, int logFrom, int logTo, std::uint32_t useTime, int subjobId) {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_AddUserLog (?,?,?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&logId);
    connection.BindParameter(&logFrom);
    connection.BindParameter(&logTo);
    connection.BindParameter(&useTime);
    connection.BindParameter(&subjobId);
    return connection.ExecutePrepared(statement);
}

bool CUser::ModChar2Extern(int gender, int race, int characterClass, int face, int hairShape, int hairColor, int jobGroup) {
    // Current Cached.exe 0x005acda0. The first successful procedure updates
    // the six resident appearance/class fields before lin_SaveUserJobGroup.
    // a failure of that second call leaves those changes live.
    L2_GUARD_FUNCTION();

    bool result;
    {
        L2_WRITE_GUARD(*this);
        result = false;
        {
            DBConn connection(runtime::process::CurrentDatabaseId());
            const int characterId = static_cast<int>(m_characterId);
            constexpr const wchar_t* modifyStatement = L"{Call lin_ModChar2 (?, ?, ?, ?, ?, ?, ?)}";
            connection.Prepare(modifyStatement);
            connection.BindParameter(&gender);
            connection.BindParameter(&race);
            connection.BindParameter(&characterClass);
            connection.BindParameter(&face);
            connection.BindParameter(&hairShape);
            connection.BindParameter(&hairColor);
            connection.BindParameter(&characterId);
            if (!connection.ExecutePrepared(modifyStatement)) {
                // Log.AddT(
                //     LOG_ERROR,
                //     L"ModChar2Extern is failed, char_id[%d] gender[%d] "
                //     L"race[%d] class[%d] face[%d] hairshape[%d] "
                //     L"haircolor[%d] jobgroup[%d]",
                //     characterId, gender, race, characterClass, face, hairShape, hairColor, jobGroup
                // )
                // Log.Add(LOG_ERROR,
                //     "[character] Cannot modify character %u: gender %d, race %d, class %d, face %d, hair shape %d, hair color %d, requested job group %d.",
                //     m_characterId, gender, race, characterClass, face, hairShape, hairColor, jobGroup)
                L2_LOG_ERROR(Log,
                    "[character] Cannot modify character %u: gender %d, race %d, class %d, face %d, hair shape %d, hair color %d, requested job group %d.",
                    m_characterId, gender, race, characterClass, face, hairShape, hairColor, jobGroup);
            } else {
                m_gender = gender;
                m_race = race;
                m_characterClass = characterClass;
                // Current ModChar2Extern 0x005aceda..0x005acef2 keeps the
                // same SQL-to-object appearance permutation as Set.
                m_face = hairColor;
                m_hairShape = hairShape;
                m_hairColor = face;

                connection.ResetParam();
                constexpr const wchar_t* jobGroupStatement = L"{Call lin_SaveUserJobGroup (?, ?)}";
                connection.Prepare(jobGroupStatement);
                connection.BindParameter(&characterId);
                connection.BindParameter(&jobGroup);
                if (!connection.ExecutePrepared(jobGroupStatement)) {
                    // Log.AddT(
                    //     LOG_ERROR,
                    //     L"ModChar2Extern SetJobGroup is failed "
                    //     L"char_id[%d] jobgroup[%d]",
                    //     characterId, jobGroup
                    // )
                    // Log.Add(LOG_ERROR,
                    //     "[character] Appearance/class changes applied for character %u, but saving job group %d failed.",
                    //     m_characterId, jobGroup)
                    L2_LOG_ERROR(Log,
                        "[character] Appearance/class changes applied for character %u, but saving job group %d failed.",
                        m_characterId, jobGroup);
                } else {
                    m_loadedInfo.jobGroup = jobGroup;
                    result = true;
                }
            }
        }
    }
    return result;
}

bool CUser::ModCharSubJobClass(int characterClass, int subjobId) {
    // Current Cached.exe 0x005ad040. The current machine fixes the procedure
    // order as character ID, subjob ID, then class despite the C++ argument
    // order being class followed by subjob ID.
    L2_GUARD_FUNCTION();

    std::optional<DBConn> connectionStorage;
    int characterId;
    bool result;
    {
        L2_WRITE_GUARD(*this);
        auto& connection = connectionStorage.emplace(runtime::process::CurrentDatabaseId());
        characterId = static_cast<int>(m_characterId);
        constexpr const wchar_t* statement = L"{Call lin_ModSubJobClass (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.BindParameter(&subjobId);
        connection.BindParameter(&characterClass);
        result = connection.ExecutePrepared(statement);
    }
    return result;
}

bool CUser::ModCharPledgeExtern(wchar_t* nickname) {
    // Current Cached.exe 0x005ad1bc. The historical method name is kept.
    // its actual database owner is lin_SetUserNickname. Resident state is
    // changed before SQL and is not restored when execution fails.
    L2_GUARD_FUNCTION();

    int unusedResult;
    int characterId;
    std::optional<DBConn> connectionStorage;
    bool result;
    {
        L2_WRITE_GUARD(*this);
        if (nickname == nullptr) {
            m_nickname.clear();
        } else {
            std::u16string targetNickname = WideToUtf16(nickname);
            if (targetNickname.size() > 23) {
                targetNickname.resize(23);
            }
            m_nickname = targetNickname;
        }

        unusedResult = 0;
        characterId = static_cast<int>(m_characterId);
        auto& connection = connectionStorage.emplace(runtime::process::CurrentDatabaseId());
        connection.Bind(&unusedResult);
        constexpr const wchar_t* statement = L"{Call lin_SetUserNickname (?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.BindParameter(nickname);
        result = connection.ExecutePrepared(statement);
    }
    return result;
}

bool CUser::ModCharExtern(
    std::int64_t sp,
    std::int64_t experience,
    int alignment,
    int pkCount,
    std::uint32_t pkPardon,
    std::uint32_t duelCount,
    int abilityPoints
) {
    // Current Cached.exe 0x005bc4c8. State is changed before SQL and remains
    // changed when lin_ModChar fails. The write lock spans both operations.
    L2_GUARD_FUNCTION();

    int previousLevel;
    int characterId;
    std::optional<DBConn> connectionStorage;
    bool result;
    {
        L2_WRITE_GUARD(*this);
        previousLevel = GetLevelByExp(m_experience);
        m_level = GetLevelByExp(experience);
        m_sp = sp;
        m_experience = experience;
        m_dropExperience = experience;
        m_alignment = alignment;
        m_pkCount = static_cast<std::uint32_t>(pkCount);
        m_pkPardon = pkPardon;
        m_duelCount = duelCount;
        SetAP(abilityPoints);

        characterId = static_cast<int>(m_characterId);
        auto& connection = connectionStorage.emplace(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_ModChar (?, ?, ?, ?, ?, ?, ?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&sp);
        connection.BindParameter(&experience);
        connection.BindParameter(&m_level);
        connection.BindParameter(&alignment);
        connection.BindParameter(&pkCount);
        connection.BindParameter(&pkPardon);
        connection.BindParameter(&duelCount);
        connection.BindParameter(&characterId);
        connection.BindParameter(&abilityPoints);
        result = connection.ExecutePrepared(statement);
    }

    if (result) {
        const int currentLevel = GetLevelByExp(m_experience);
        if (previousLevel != currentLevel) {
            AddUserLog2(3, previousLevel, currentLevel, 0, m_subjobId);
        }
    }
    return result;
}

bool CUser::ModCharSubJobAbilityAbsolutely(std::int64_t sp, std::int64_t experience, int subjobId, int abilityPoints) {
    // Current Cached.exe 0x005bc704. A failed/missing old-EXP query suppresses
    // only the level audit. The absolute mutation is still attempted.
    L2_GUARD_FUNCTION();

    std::int64_t previousExperience = 0;
    int previousLevel = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&previousExperience);
    constexpr const wchar_t* loadStatement = L"{Call lin_GetExpBySubJob (?, ?)}";
    connection.Prepare(loadStatement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&subjobId);
    if (connection.ExecutePrepared(loadStatement) && connection.Fetch()) {
        previousLevel = GetLevelByExp(previousExperience);
    }
    connection.UnBind();

    int characterId;
    int level;
    bool result;
    {
        L2_WRITE_GUARD(*this);
        characterId = static_cast<int>(m_characterId);
        level = GetLevelByExp(experience);
        constexpr const wchar_t* modifyStatement = L"{Call lin_ModSubJobAbilityAbsolutely (?, ?, ?, ?, ?, ?)}";
        connection.Prepare(modifyStatement);
        connection.BindParameter(&characterId);
        connection.BindParameter(&subjobId);
        connection.BindParameter(&sp);
        connection.BindParameter(&experience);
        connection.BindParameter(&level);
        connection.BindParameter(&abilityPoints);
        result = connection.ExecutePrepared(modifyStatement);
    }

    if (result && previousLevel > 0 && previousLevel != GetLevelByExp(experience)) {
        // Shipping 0x005bc8c7 reads resident main-job EXP here instead of
        // logging the newly computed nonresident-subjob level.
        AddUserLog2(3, previousLevel, GetLevelByExp(m_experience), 0, subjobId);
    }
    return result;
}

bool CUser::ModChar2Extern(
    int gender, int race, int characterClass, int face, int hairShape, int hairColor, int jobGroup, int subjobId
) {
    // Current Cached.exe 0x005bc92c. The selected subjob-class cache is
    // written before either SQL call, and neither failure path rolls it back.
    L2_GUARD_FUNCTION();

    if (static_cast<unsigned int>(subjobId) < m_subjobClass.size()) {
        m_subjobClass[static_cast<std::size_t>(subjobId)] = characterClass;
    }

    const int residentClass = m_subjobId == subjobId ? characterClass : m_characterClass;
    if (!ModChar2Extern(gender, race, residentClass, face, hairShape, hairColor, jobGroup)) {
        return false;
    }
    return ModCharSubJobClass(characterClass, subjobId);
}

bool CUser::ModChar3Extern(
    std::int64_t spDelta,
    std::int64_t experienceDelta,
    int alignmentDelta,
    int pkCountDelta,
    int pkPardonDelta,
    int duelCountDelta,
    int abilityPointDelta
) {
    // Current Cached.exe 0x005bca6c. Unlike the absolute owner, SQL executes
    // before resident state changes and the target unlocks before applying
    // the successful wrapping additions.
    L2_GUARD_FUNCTION();

    int previousLevel;
    std::int64_t prospectiveExperience;
    int prospectiveLevel;
    int characterId;
    std::optional<DBConn> connectionStorage;
    bool result;
    {
        L2_WRITE_GUARD(*this);
        previousLevel = GetLevelByExp(m_experience);
        prospectiveExperience = runtime::AddInt64Wrapping(m_experience, experienceDelta);
        prospectiveLevel = GetLevelByExp(prospectiveExperience);
        characterId = static_cast<int>(m_characterId);

        auto& connection = connectionStorage.emplace(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_ModChar3 (?, ?, ?, ?, ?, ?, ?, ?, ? )}";
        connection.Prepare(statement);
        connection.BindParameter(&spDelta);
        connection.BindParameter(&experienceDelta);
        connection.BindParameter(&prospectiveLevel);
        connection.BindParameter(&alignmentDelta);
        connection.BindParameter(&pkCountDelta);
        connection.BindParameter(&pkPardonDelta);
        connection.BindParameter(&duelCountDelta);
        connection.BindParameter(&characterId);
        connection.BindParameter(&abilityPointDelta);
        result = connection.ExecutePrepared(statement);
    }

    if (result) {
        m_sp = runtime::AddInt64Wrapping(m_sp, spDelta);
        m_experience = runtime::AddInt64Wrapping(m_experience, experienceDelta);
        m_dropExperience = runtime::AddInt64Wrapping(m_dropExperience, experienceDelta);
        m_alignment = runtime::AddInt32Wrapping(m_alignment, alignmentDelta);
        m_pkCount += static_cast<std::uint32_t>(pkCountDelta);
        m_pkPardon += static_cast<std::uint32_t>(pkPardonDelta);
        m_duelCount += static_cast<std::uint32_t>(duelCountDelta);
        m_level = GetLevelByExp(m_experience);
        m_abilityPoints = runtime::AddInt32Wrapping(m_abilityPoints, abilityPointDelta);

        const int currentLevel = GetLevelByExp(m_experience);
        if (previousLevel != currentLevel) {
            AddUserLog2(3, previousLevel, currentLevel, 0, m_subjobId);
        }
    }
    return result;
}

bool CUser::ModCharSubJobAbility(std::int64_t spDelta, std::int64_t experienceDelta, int subjobId, int abilityPointDelta) {
    // Current Cached.exe 0x005bcca8 requires an old-EXP row before the delta
    // mutation and keeps nonresident state solely in user_subjob.
    L2_GUARD_FUNCTION();

    std::int64_t previousExperience = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&previousExperience);
    constexpr const wchar_t* loadStatement = L"{Call lin_GetExpBySubJob (?, ?)}";
    connection.Prepare(loadStatement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&subjobId);
    if (!connection.ExecutePrepared(loadStatement) || !connection.Fetch()) {
        return false;
    }
    connection.UnBind();

    std::int64_t prospectiveExperience;
    int prospectiveLevel;
    int characterId;
    bool result;
    {
        L2_WRITE_GUARD(*this);
        prospectiveExperience = runtime::AddInt64Wrapping(previousExperience, experienceDelta);
        prospectiveLevel = GetLevelByExp(prospectiveExperience);
        characterId = static_cast<int>(m_characterId);
        constexpr const wchar_t* modifyStatement = L"{Call lin_ModSubJobAbility (?, ?, ?, ?, ?, ?)}";
        connection.Prepare(modifyStatement);
        connection.BindParameter(&characterId);
        connection.BindParameter(&subjobId);
        connection.BindParameter(&spDelta);
        connection.BindParameter(&experienceDelta);
        connection.BindParameter(&prospectiveLevel);
        connection.BindParameter(&abilityPointDelta);
        result = connection.ExecutePrepared(modifyStatement);
    }

    if (result) {
        const int currentLevel = GetLevelByExp(prospectiveExperience);
        const int previousLevel = GetLevelByExp(previousExperience);
        if (previousLevel != currentLevel) {
            AddUserLog2(3, previousLevel, currentLevel, 0, subjobId);
        }
    }
    return result;
}

bool CUser::ModCharExtern(
    std::int64_t sp,
    std::int64_t experience,
    int alignment,
    int pkCount,
    std::uint32_t pkPardon,
    std::uint32_t duelCount,
    int subjobId,
    int abilityPoints
) {
    // Current Cached.exe 0x005bdc84 calls the current and nonresident
    // subjob paths without introducing a second state carrier.
    L2_GUARD_FUNCTION();

    if (m_subjobId == subjobId) {
        return ModCharExtern(sp, experience, alignment, pkCount, pkPardon, duelCount, abilityPoints);
    }
    if (!ModCharExtern(m_sp, m_experience, alignment, pkCount, pkPardon, duelCount, m_abilityPoints)) {
        return false;
    }
    return ModCharSubJobAbilityAbsolutely(sp, experience, subjobId, abilityPoints);
}

bool CUser::ModChar3Extern(
    std::int64_t spDelta,
    std::int64_t experienceDelta,
    int alignmentDelta,
    int pkCountDelta,
    int pkPardonDelta,
    int duelCountDelta,
    int subjobId,
    int abilityPointDelta
) {
    // Current Cached.exe 0x005bdde0 applies the shared deltas through the
    // current-job owner before touching a different subjob.
    L2_GUARD_FUNCTION();

    if (m_subjobId == subjobId) {
        return ModChar3Extern(
            spDelta, experienceDelta, alignmentDelta, pkCountDelta, pkPardonDelta, duelCountDelta, abilityPointDelta
        );
    }
    if (!ModChar3Extern(0, 0, alignmentDelta, pkCountDelta, pkPardonDelta, duelCountDelta, 0)) {
        return false;
    }
    return ModCharSubJobAbility(spDelta, experienceDelta, subjobId, abilityPointDelta);
}

int CUser::CreateSubJob(
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
) {
    // Current Cached.exe 0x005af9c8. The PDB fixes the complete signature.
    // current machine code fixes the SQL ABI, both audit records and every
    // in-memory mutation below.
    L2_GUARD_FUNCTION();

    int result;
    std::optional<DBConn> connectionStorage;
    int characterId;
    int newLevel;
    long long newExperience;
    long long savedSp;
    long long savedExperience;
    {
        L2_WRITE_GUARD(*this);
        result = 0;
        auto& connection = connectionStorage.emplace(runtime::process::CurrentDatabaseId());
        connection.Bind(&result);

        characterId = static_cast<int>(m_characterId);
        newLevel = 40;
        newExperience = static_cast<long long>(g_ExpTable[39]);
        savedSp = static_cast<long long>(sp);
        savedExperience = static_cast<long long>(experience);
        constexpr const wchar_t* statement = L"{Call lin_CreateSubJob (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.BindParameter(&newSubjobId);
        connection.BindParameter(&newClass);
        connection.BindParameter(&newLevel);
        connection.BindParameter(&newExperience);
        connection.BindParameter(&oldSubjobId);
        connection.BindParameter(&hp);
        connection.BindParameter(&mp);
        connection.BindParameter(&savedSp);
        connection.BindParameter(&savedExperience);
        connection.BindParameter(&level);
        connection.BindParameter(&henna1);
        connection.BindParameter(&henna2);
        connection.BindParameter(&henna3);
        connection.BindParameter(&abilityPoints);

        if (connection.ExecutePrepared(statement) && connection.Fetch()) {
            // The target enters this block on a fetched result row, including a
            // procedure result of zero. The result value is not a branch gate.
            m_skipNextExpDiff = true;
            const int oldCharacterClass = m_characterClass;
            const int oldCharacterLevel = m_level;
            m_characterClass = newClass;
            m_subjobClass[static_cast<std::size_t>(newSubjobId)] = newClass;
            m_subjobId = result;
            m_sp = 0;
            SetAP(0);
            m_experience = g_ExpTable[39];
            m_level = newLevel;
            // Current writes at CUser+0xc60/+0xc68 clear the two matching-point
            // values. Henna is reset by lin_CreateSubJob, not by this cache body.
            m_pledgeGamePoint = 0;
            m_olympiadPoint = 0;

            WriteSubJobTransitionAudit(*this, 6, 0x34f, oldCharacterClass, oldCharacterLevel, result);
            WriteSubJobTransitionAudit(*this, 8, 0x591, oldCharacterClass, oldCharacterLevel, result);
            AddUserLog2(2, oldCharacterClass, m_characterClass, 0, result);

            {
                L2_GUARD(m_skillLock);
                m_acquiredSkills.clear();
                m_acquiredPotentialSkills.clear();
            }

            // Shipping memset at 0x005afda7 clears the complete 0x600-byte
            // active-abnormal block, including skillId's usual -1 sentinel.
            m_abnormalStatus.fill(AbnormalStatus{0, 0, 0, 0, 0});
        }
    }
    return result;
}

int CUser::CreateDualJob(
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
) {
    // Current Cached.exe 0x005afdfc. The PDB fixes the complete signature.
    // the current machine code fixes the level-85 initialization, both
    // transition records and the complete cache mutation below.
    L2_GUARD_FUNCTION();

    int result;
    std::optional<DBConn> connectionStorage;
    int characterId;
    int newLevel;
    long long newExperience;
    long long savedSp;
    long long savedExperience;
    {
        L2_WRITE_GUARD(*this);
        result = 0;
        auto& connection = connectionStorage.emplace(runtime::process::CurrentDatabaseId());
        connection.Bind(&result);

        characterId = static_cast<int>(m_characterId);
        newLevel = 85;
        newExperience = static_cast<long long>(g_ExpTable[84]);
        savedSp = static_cast<long long>(sp);
        savedExperience = static_cast<long long>(experience);
        constexpr const wchar_t* statement = L"{Call lin_CreateDualJob (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.BindParameter(&newSubjobId);
        connection.BindParameter(&newClass);
        connection.BindParameter(&newLevel);
        connection.BindParameter(&newExperience);
        connection.BindParameter(&oldSubjobId);
        connection.BindParameter(&hp);
        connection.BindParameter(&mp);
        connection.BindParameter(&savedSp);
        connection.BindParameter(&savedExperience);
        connection.BindParameter(&level);
        connection.BindParameter(&henna1);
        connection.BindParameter(&henna2);
        connection.BindParameter(&henna3);
        connection.BindParameter(&abilityPoints);

        if (connection.ExecutePrepared(statement) && connection.Fetch()) {
            // As in CreateSubJob, fetching a row enters this block even if the
            // procedure returned zero. The result itself is not a branch gate.
            m_skipNextExpDiff = true;
            const int oldCharacterClass = m_characterClass;
            const int oldCharacterLevel = m_level;
            m_characterClass = newClass;
            m_subjobClass[static_cast<std::size_t>(newSubjobId)] = newClass;
            m_subjobId = result;
            m_sp = 0;
            SetAP(0);
            m_experience = g_ExpTable[84];
            m_level = newLevel;
            m_pledgeGamePoint = 0;
            m_olympiadPoint = 0;

            WriteSubJobTransitionAudit(*this, 6, 0x34f, oldCharacterClass, oldCharacterLevel, result);
            WriteSubJobTransitionAudit(*this, 8, 0x591, oldCharacterClass, oldCharacterLevel, result);
            AddUserLog2(2, oldCharacterClass, m_characterClass, 0, result);

            {
                L2_GUARD(m_skillLock);
                m_acquiredSkills.clear();
                m_acquiredPotentialSkills.clear();
            }

            m_abnormalStatus.fill(AbnormalStatus{0, 0, 0, 0, 0});
        }
    }
    return result;
}

int CUser::CancelDualClass() {
    // Current Cached.exe 0x005b0610. The procedure result gates the sole
    // cache mutation. The method itself returns the live dual-class ID after
    // that branch, not the procedure result.
    L2_GUARD_FUNCTION();

    int result;
    std::optional<DBConn> connectionStorage;
    int characterId;
    {
        L2_WRITE_GUARD(*this);
        result = 0;
        auto& connection = connectionStorage.emplace(runtime::process::CurrentDatabaseId());
        connection.Bind(&result);
        characterId = static_cast<int>(m_characterId);
        constexpr const wchar_t* statement = L"{Call lin_CancelDualClass (?)}";
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        if (connection.ExecutePrepared(statement)) {
            connection.Fetch();
        }

        if (result < 1) {
            // Log.AddT(
            //     LOG_ERROR, L"[%s][%d] fail to cancel dualclass. user[%s] class now[%d]", L"User.cpp", 0x1ab7, GetCharName(),
            //     m_characterClass
            // )
            // Log.Add(LOG_ERROR,
            //     L"[subclass] Cannot cancel dual class for character %u (%ls), current class %d: database result %d. Dual class retained.",
            //     m_characterId, GetCharName(), m_characterClass, result)
            // L2_LOG_ERROR(Log,
            //     L"[subclass] Cannot cancel dual class for character %u (%ls), current class %d: database result %d. Dual class retained.",
            //     m_characterId, GetCharName(), m_characterClass, result)
            L2_LOG_ERROR(
                Log,
                u"[subclass] Cannot cancel dual class for character %u (%ls), current class %d: database result %d; dual "
                u"class retained.",
                m_characterId, GetCharName(), m_characterClass, result
            );
        } else {
            const int previousDualClassId = m_dualClassId;
            m_dualClassId = -1;
            AddUserLog2(6, m_characterClass, m_characterClass, 0, previousDualClassId);
        }
    }
    return m_dualClassId;
}

bool CUser::RenewSubJob(
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
) {
    // Current Cached.exe 0x005b07fc. The current helper at 0x005a6514
    // fixes all sixteen SQL input types. The compatible 287, Classic 3.0 and
    // Ertheia procedures supply the parameter meanings.
    L2_GUARD_FUNCTION();

    int result;
    std::optional<DBConn> connectionStorage;
    int newLevel;
    long long newExperience;
    int characterId;
    long long savedSp;
    long long savedExperience;
    {
        L2_WRITE_GUARD(*this);
        result = 0;
        auto& connection = connectionStorage.emplace(runtime::process::CurrentDatabaseId());
        connection.Bind(&result);

        newLevel = subjobType == 1 ? m_level : 40;
        newExperience =
            subjobType == 1 ? static_cast<long long>(m_experience) : static_cast<long long>(g_ExpTable[39]);
        if (newLevel > 120) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[User.cpp][0x1ae0] newLevel[%d] is bigger than "
            //     "MAX_LEVEL[%d]",
            //     newLevel, 120
            // )
            // Log.Add(LOG_WARN,
            //     "[subclass] Renewal for character %u requests level %d, above the maximum 120. The request will still be submitted.",
            //     m_characterId, newLevel)
            L2_LOG_WARN(Log,
                "[subclass] Renewal for character %u requests level %d, above the maximum 120; the request will still be submitted.",
                m_characterId, newLevel);
        }

        characterId = static_cast<int>(m_characterId);
        savedSp = static_cast<long long>(sp);
        savedExperience = static_cast<long long>(experience);
        constexpr const wchar_t* statement = L"{Call lin_RenewSubJob (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.BindParameter(&subjobId);
        connection.BindParameter(&subjobType);
        connection.BindParameter(&newClass);
        connection.BindParameter(&newLevel);
        connection.BindParameter(&newExperience);
        connection.BindParameter(&oldSubjobId);
        connection.BindParameter(&hp);
        connection.BindParameter(&mp);
        connection.BindParameter(&savedSp);
        connection.BindParameter(&savedExperience);
        connection.BindParameter(&level);
        connection.BindParameter(&henna1);
        connection.BindParameter(&henna2);
        connection.BindParameter(&henna3);
        connection.BindParameter(&abilityPoints);
        if (connection.ExecutePrepared(statement)) {
            connection.Fetch();
        }

        if (result < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[User.cpp][0x1b1e] fail to renew subjob. user[%ls] "
            //     "subjob type[%d] subjob id[%d] class now:new [%d:%d]",
            //     GetCharName(), subjobType, subjobId, m_characterClass, newClass
            // )
            // Log.Add(LOG_ERROR,
            //     L"[subclass] Cannot renew subjob %d (type %d) for character %u (%ls), class %d to %d: database result %d.",
            //     subjobId, subjobType, m_characterId, GetCharName(), m_characterClass, newClass, result)
            // L2_LOG_ERROR(Log,
            //     L"[subclass] Cannot renew subjob %d (type %d) for character %u (%ls), class %d to %d: database result %d.",
            //     subjobId, subjobType, m_characterId, GetCharName(), m_characterClass, newClass, result)
            L2_LOG_ERROR(
                Log,
                u"[subclass] Cannot renew subjob %d (type %d) for character %u (%ls), class %d to %d: database result %d.",
                subjobId, subjobType, m_characterId, GetCharName(), m_characterClass, newClass, result
            );
        } else {
            if (subjobType == 1) {
                m_dualClassId = subjobId;
                AddUserLog2(5, m_characterClass, newClass, 0, subjobId);
            } else {
                m_dualClassId = -1;
                m_henna.fill(0);
                m_hennaPremiumId = 0;
                m_experience = newExperience;
                m_level = newLevel;
                m_sp = 0;
                SetAP(0);
                {
                    L2_GUARD(m_skillLock);
                    m_acquiredSkills.clear();
                    m_acquiredPotentialSkills.clear();
                }
                m_abnormalStatus.fill(AbnormalStatus{0, 0, 0, 0, 0});
            }

            m_skipNextExpDiff = true;
            const int oldClass = m_subjobClass[static_cast<std::size_t>(subjobId)];
            m_characterClass = newClass;
            m_subjobClass[static_cast<std::size_t>(subjobId)] = newClass;
            m_subjobId = subjobId;
            WriteRenewSubJobAudit(*this, subjobId, oldClass, level, subjobType, newClass, newLevel);
        }
    }
    return result != 0;
}

bool CUser::DeleteSubJob(int subjobId) {
    // Current Cached.exe 0x005b0c08. The owner does not mutate the CUser
    // cache. Paired L2Server ReplyDeleteSubJob performs its own successful
    // class-slot update after receiving the procedure result.
    L2_GUARD_FUNCTION();

    int result;
    std::optional<DBConn> connectionStorage;
    int characterId;
    {
        L2_WRITE_GUARD(*this);
        result = 0;
        auto& connection = connectionStorage.emplace(runtime::process::CurrentDatabaseId());
        connection.Bind(&result);

        characterId = static_cast<int>(m_characterId);
        constexpr const wchar_t* statement = L"{Call lin_DeleteSubJob (?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.BindParameter(&subjobId);
        if (connection.ExecutePrepared(statement)) {
            connection.Fetch();
        }
    }
    return result != 0;
}

bool CUser::ChangeSubJob(
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
) {
    // Current Cached.exe 0x005b0d94. All output pointers are also initialized
    // with the active class values supplied by L2Server. The target copies
    // those inputs before binding the same destinations as nine result
    // columns.
    L2_GUARD_FUNCTION();

    double savedHp;
    double savedMp;
    long long savedSp;
    long long savedExperience;
    int savedLevel;
    int savedHenna1;
    int savedHenna2;
    int savedHenna3;
    int savedAbilityPoints;
    bool changed;
    std::optional<DBConn> connectionStorage;
    long long outputSp;
    long long outputExperience;
    int characterId;
    {
        L2_WRITE_GUARD(*this);
        savedHp = *hp;
        savedMp = *mp;
        savedSp = static_cast<long long>(*sp);
        savedExperience = static_cast<long long>(*experience);
        savedLevel = *level;
        savedHenna1 = *henna1;
        savedHenna2 = *henna2;
        savedHenna3 = *henna3;
        savedAbilityPoints = abilityPoints;

        changed = false;
        auto& connection = connectionStorage.emplace(runtime::process::CurrentDatabaseId());
        outputSp = savedSp;
        outputExperience = savedExperience;
        connection.Bind(hp);
        connection.Bind(mp);
        connection.Bind(&outputSp);
        connection.Bind(&outputExperience);
        connection.Bind(level);
        connection.Bind(henna1);
        connection.Bind(henna2);
        connection.Bind(henna3);
        connection.Bind(&abilityPoints);

        characterId = static_cast<int>(m_characterId);
        constexpr const wchar_t* statement = L"{Call lin_ChangeSubJob (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.BindParameter(&newSubjobId);
        connection.BindParameter(&oldSubjobId);
        connection.BindParameter(&savedHp);
        connection.BindParameter(&savedMp);
        connection.BindParameter(&savedSp);
        connection.BindParameter(&savedExperience);
        connection.BindParameter(&savedLevel);
        connection.BindParameter(&savedHenna1);
        connection.BindParameter(&savedHenna2);
        connection.BindParameter(&savedHenna3);
        connection.BindParameter(&savedAbilityPoints);

        if (connection.ExecutePrepared(statement) && connection.Fetch()) {
            *sp = static_cast<std::int64_t>(outputSp);
            *experience = static_cast<std::int64_t>(outputExperience);
            m_skipNextExpDiff = true;
            const int oldLevel = m_level;
            const std::int64_t oldExperience = m_experience;

            m_hp = *hp;
            m_mp = *mp;
            m_sp = *sp;
            m_experience = *experience;
            m_level = *level;
            m_henna[0] = *henna1;
            m_henna[1] = *henna2;
            m_henna[2] = *henna3;
            m_subjobId = newSubjobId;
            oldClass = m_characterClass;
            m_characterClass = m_subjobClass[static_cast<std::size_t>(newSubjobId)];
            SetAP(abilityPoints);

            const std::u16string characterName = std::u16string(GetCharName());
            const std::u16string accountName = std::u16string(GetAccountName());
            CLogFormatter formatter;
            formatter.WriteLog(
                0x34e, GetId(), GetAccountID(), 0, 0, GetX(), GetY(), GetZ(), nullptr, nullptr, nullptr, GetRace(), GetGender(),
                m_characterClass, m_level, oldClass, oldLevel, oldExperience, 0, m_dualClassId, newSubjobId,
                characterName.c_str(), accountName.c_str(), nullptr, nullptr, 0
            );

            {
                L2_GUARD(m_skillLock);
                m_acquiredSkills.clear();
                m_acquiredPotentialSkills.clear();
            }
            m_abnormalStatus.fill(AbnormalStatus{0, 0, 0, 0, 0});
            changed = true;
        }
    }
    return changed;
}

bool CUser::ChangeSubJob(int subjobId) {
    // Current Cached.exe 0x005b1268. This is the admin/offline overload: the
    // stored procedure persists the current class and selects the requested
    // class, then CUserDb reloads the same published object from the database.
    L2_GUARD_FUNCTION();

    if (subjobId == m_subjobId)
        return true;

    std::optional<DBConn> connectionStorage;
    int characterId;
    bool changed;
    {
        L2_WRITE_GUARD(*this);
        auto& connection = connectionStorage.emplace(runtime::process::CurrentDatabaseId());
        characterId = static_cast<int>(m_characterId);
        constexpr const wchar_t* statement = L"{Call lin_ChangeSubJobBySubJobId (?, ? )}";
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.BindParameter(&subjobId);
        changed = connection.ExecutePrepared(statement);
    }

    if (changed) {
        g_userDb.RefreshChar(this);
    }
    return changed;
}

int CUser::GetSubJobListBuffer(char* buffer, int* bufferSize) {
    // Current Cached.exe 0x005b13dc. Each result row is the target `dd`
    // pair {subjob_id, effective_level}. The caller supplies and then receives
    // the raw byte capacity/length through the same int pointer.
    L2_GUARD_FUNCTION();

    int subjobId = 0;
    int effectiveLevel = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&subjobId);
    connection.Bind(&effectiveLevel);

    const int characterId = static_cast<int>(m_characterId);
    constexpr const wchar_t* statement = L"{Call lin_GetSubJobLevel (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(statement)) {
        *bufferSize = 0;
        return 0;
    }

    const int capacity = *bufferSize;
    *bufferSize = 0;
    int count = 0;
    while (connection.Fetch()) {
        const int assembled = ::Assemble(buffer + *bufferSize, capacity - *bufferSize, "dd", subjobId, effectiveLevel);
        *bufferSize += assembled;
        ++count;
    }
    return count;
}

bool CUser::AddSubJob(
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
) {
    // Current Cached.exe 0x005b1594. This legacy/admin overload inserts a
    // supplied subjob snapshot without changing the active class.
    L2_GUARD_FUNCTION();

    int result = 0;
    std::optional<DBConn> connectionStorage;
    int characterId;
    long long savedSp;
    long long savedExperience;
    {
        L2_WRITE_GUARD(*this);
        auto& connection = connectionStorage.emplace(runtime::process::CurrentDatabaseId());
        connection.Bind(&result);

        characterId = static_cast<int>(m_characterId);
        savedSp = static_cast<long long>(sp);
        savedExperience = static_cast<long long>(experience);
        constexpr const wchar_t* statement = L"{Call lin_AddSubJob (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.BindParameter(&newClass);
        connection.BindParameter(&hp);
        connection.BindParameter(&mp);
        connection.BindParameter(&savedSp);
        connection.BindParameter(&savedExperience);
        connection.BindParameter(&level);
        connection.BindParameter(&subjobId);
        connection.BindParameter(&henna1);
        connection.BindParameter(&henna2);
        connection.BindParameter(&henna3);
        if (connection.ExecutePrepared(statement)) {
            connection.Fetch();
            m_subjobClass[static_cast<std::size_t>(subjobId)] = newClass;
        }
    }
    return result > 0;
}

bool CUser::SaveForSubJob(
    int subjobId,
    int characterClass,
    int dualClassId,
    double hp,
    double mp,
    double maxHp,
    double maxMp,
    double cp [[maybe_unused]],
    double maxCp [[maybe_unused]],
    std::int64_t sp,
    std::int64_t experience,
    int level,
    int henna1,
    int henna2,
    int henna3,
    int abilityPoints
) {
    // Current Cached.exe 0x005bb638. The original body accepts CP/max-CP but
    // does not store either value. Only HP/MP and their two maxima are copied
    // before the ordinary Save and SaveHennaInfo owners run.
    L2_GUARD_FUNCTION();

    m_subjobId = subjobId;
    m_dualClassId = dualClassId;
    m_characterClass = characterClass;
    m_subjobClass[static_cast<std::size_t>(subjobId)] = characterClass;
    m_hp = hp;
    m_mp = mp;
    m_maxHp = maxHp;
    m_maxMp = maxMp;
    m_sp = sp;
    m_experience = experience;
    SetAP(abilityPoints);
    m_level = level;
    m_henna[0] = henna1;
    m_henna[1] = henna2;
    m_henna[2] = henna3;

    const bool userSaved = Save();
    const bool hennaSaved = SaveHennaInfo();
    return userSaved && hennaSaved;
}

int CUser::GetUserLog(int logId, int logTo) {
    L2_GUARD_FUNCTION();

    int useTime = 0;
    std::int64_t nullableIndicator = -1;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.BindNullable(&useTime, &nullableIndicator);
    constexpr const wchar_t* statement = L"{Call lin_GetUserLogTime (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&logId);
    connection.BindParameter(&logTo);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }
    return useTime;
}

void CUser::RegisterInitialShortcut() {
    L2_GUARD_FUNCTION();

    CWareHouseSP warehouse = GetWareHouse(0, true);
    if (!warehouse) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find warehouse, owner id[%d]", "User.cpp", 0x1d22,
        //     static_cast<int>(m_characterId)
        // )
        // Log.Add(LOG_ERROR,
        //     "[character] Cannot register initial shortcuts for character %u: inventory unavailable.",
        //     m_characterId)
        L2_LOG_ERROR(Log,
            "[character] Cannot register initial shortcuts for character %u: inventory unavailable.",
            m_characterId);
        return;
    }

    struct InitialShortcut {
        int slot;
        int type;
        int id;
        const wchar_t* macro;
    };
    static constexpr std::array<InitialShortcut, 11> initialShortcuts{{
        {0, 3, 2, L""},
        {3, 3, 5, L""},
        {4, 3, 4, L""},
        {10, 3, 0, L""},
        {120, 2, 911, L"1"},
        {123, 2, 884, L"1"},
        {124, 2, 885, L"1"},
        {132, 3, 67, L""},
        {132, 3, 68, L""},
        {132, 3, 69, L""},
        {132, 3, 70, L""},
    }};

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetShortCut (?, ?, ?, ?, ?, ?)}";
    const int characterId = static_cast<int>(m_characterId);
    auto saveShortcut = [&](const InitialShortcut& shortcut) {
        const int subjobId = m_subjobId;
        connection.ResetParam();
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.BindParameter(&subjobId);
        connection.BindParameter(&shortcut.slot);
        connection.BindParameter(&shortcut.type);
        connection.BindParameter(&shortcut.id);
        connection.BindParameter(shortcut.macro);
        connection.ExecutePrepared(statement);
    };

    for (const InitialShortcut& shortcut : initialShortcuts) {
        saveShortcut(shortcut);
    }

    if (m_characterClass == 10 || m_characterClass == 25 || m_characterClass == 38) {
        saveShortcut({1, 2, 1177, L""});
        saveShortcut({9, 2, 1216, L""});
    } else if (m_characterClass == 49) {
        saveShortcut({1, 2, 1001, L""});
    } else if (m_characterClass == 183) {
        saveShortcut({1, 2, 30001, L""});
    }
}

void CUser::SaveShortcut(int subjobId, int slot, int type, int id, wchar_t* macro) {
    // Current Cached.exe 0x005b2808. The helper called by the target binds
    // the character ID as unsigned, followed by the five source arguments.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetShortCut (?, ?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&subjobId);
    connection.BindParameter(&slot);
    connection.BindParameter(&type);
    connection.BindParameter(&id);
    connection.BindParameter(macro);
    connection.ExecutePrepared(statement);
}

bool CUser::OnRequestEvolvePet(CSocket* socket, const std::uint8_t* packet, unsigned int requestId) {
    // Current Cached.exe 0x005a3660.
    L2_GUARD_FUNCTION();

    // The shipping body acquires a connection at entry even though none of
    // its own statements use it. Nested CPet/CWareHouse calls use separate
    // connections. Retain that observable pool lifetime.
    DBConn connection(runtime::process::CurrentDatabaseId());

    char sameExperience = 0;
    int oldCollarId = 0;
    int newCollarType = 0;
    int newPetNpcClass = 0;
    std::uint64_t baseExperienceBits = 0;
    int newLevel = 0;
    int oldLevel = 0;
    double hp = 0.0;
    double mp = 0.0;
    int meal = 0;
    const std::uint8_t* cursor = ::Disassemble(
        packet, "cdddQddffd", &sameExperience, &oldCollarId, &newCollarType, &newPetNpcClass, &baseExperienceBits, &newLevel,
        &oldLevel, &hp, &mp, &meal
    );

    auto sendFailure = [&]() { socket->Send("cdddddd", 0x60, requestId, 0, 0, 0, 0, 0); };

    const int worldId = runtime::process::CurrentDatabaseId();
    CWareHouseSP inventory = GetWareHouse(0, true);
    if (!inventory) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] invalid warehouse, id[%d]", "User.cpp", 0x20dc, static_cast<int>(m_characterId)
        // )
        // Log.Add(LOG_ERROR,
        //     "[pet] Evolution rejected for character %u: inventory unavailable.",
        //     m_characterId)
        L2_LOG_ERROR(Log,
            "[pet] Evolution rejected for character %u: inventory unavailable.",
            m_characterId);
        sendFailure();
        return false;
    }

    std::optional<CItemSP> oldCollarStorage;
    std::optional<CPetSP> oldPetStorage;
    std::optional<std::wstring> oldPetNameStorage;
    std::optional<CPetSP> newPetStorage;
    const bool evolutionSucceeded = [&] {
        L2_WRITE_GUARD(*inventory.operator->());
        auto& oldCollar = oldCollarStorage.emplace(inventory->GetItem(oldCollarId));
        if (!oldCollar) {
            // Log.Add(
            //     LOG_ERROR, "[%s][%d] invalid pet collar dbid(%d), id[%d]", "User.cpp", 0x20e6, oldCollarId,
            //     static_cast<int>(m_characterId)
            // )
            // Log.Add(LOG_ERROR,
            //     "[pet] Evolution rejected for character %u: collar item %d is not in the inventory.",
            //     m_characterId, oldCollarId)
            L2_LOG_ERROR(Log,
                "[pet] Evolution rejected for character %u: collar item %d is not in the inventory.",
                m_characterId, oldCollarId);
            return false;
        }

        auto& oldPet = oldPetStorage.emplace(g_petDb.GetPet(oldCollarId, worldId));
        if (!oldPet) {
            // Log.Add(
            //     LOG_ERROR, "[%s][%d] RequestEvolvePet failed. invalid pet[%d]", "User.cpp", 0x20ed, oldCollarId
            // )
            // Log.Add(LOG_ERROR,
            //     "[pet] Evolution rejected for character %u: pet record for collar %d is unavailable.",
            //     m_characterId, oldCollarId)
            L2_LOG_ERROR(Log,
                "[pet] Evolution rejected for character %u: pet record for collar %d is unavailable.",
                m_characterId, oldCollarId);
            oldCollar.Release();
            return false;
        }

        const int oldPetNpcClass = static_cast<int>(oldPet->m_npcClassId);
        const std::int64_t oldPetSpellPoint = oldPet->m_spellPoint;
        std::int64_t newExperience = runtime::Int64FromBits(baseExperienceBits);
        if (sameExperience == 0) {
            std::uint64_t oldLevelMinimumBits = 0;
            std::uint64_t oldLevelMaximumBits = 0;
            std::uint64_t newLevelMinimumBits = 0;
            std::uint64_t newLevelMaximumBits = 0;
            ::Disassemble(
                cursor, "QQQQ", &oldLevelMinimumBits, &oldLevelMaximumBits, &newLevelMinimumBits, &newLevelMaximumBits
            );

            const std::int64_t oldLevelMinimum = runtime::Int64FromBits(oldLevelMinimumBits);
            const std::int64_t oldLevelMaximum = runtime::Int64FromBits(oldLevelMaximumBits);
            const std::int64_t newLevelMinimum = runtime::Int64FromBits(newLevelMinimumBits);
            const std::int64_t newLevelMaximum = runtime::Int64FromBits(newLevelMaximumBits);
            const std::int64_t oldNumerator = runtime::SubtractInt64Wrapping(oldPet->m_exp, oldLevelMinimum);
            const std::int64_t oldDenominator = runtime::SubtractInt64Wrapping(oldLevelMaximum, oldLevelMinimum);
            const std::int64_t newRange = runtime::SubtractInt64Wrapping(newLevelMaximum, newLevelMinimum);

            // Volatile double intermediates prohibit contraction into an FMA.
            // current 0x005a3a1f..0x005a3a52 uses separate DIVSD/MULSD/ADDSD.
            volatile double ratio = static_cast<double>(oldNumerator) / static_cast<double>(oldDenominator);
            volatile double scaled = static_cast<double>(newRange) * ratio;
            volatile double evolved = scaled + static_cast<double>(newExperience);
            // Current OnRequestEvolvePet ends this calculation with
            // CVTTSD2SI r64.
            newExperience =
                runtime::Win64CvttDoubleToInt64(evolved);
        }

        int preserveName = 0;
        auto& oldPetName = oldPetNameStorage.emplace();
        if (oldCollar->Eroded() == 1) {
            preserveName = 1;
            oldPetName = FixedUtf16ToWide(oldPet->m_nickName);
        }

        int newItemId = 0;
        if (!inventory->CreatePet(
                newCollarType, newPetNpcClass, newExperience, newLevel, hp, mp, meal, newItemId, preserveName
            )) {
            // Log.Add(
            //     LOG_ERROR, "[%s][%d] warehouse->CreatePet Failed , id[%d]", "User.cpp", 0x2110,
            //     static_cast<int>(m_characterId)
            // )
            // Log.Add(LOG_ERROR,
            //     "[pet] Evolution rejected for character %u: cannot create the new collar type %d / NPC class %d.",
            //     m_characterId, newCollarType, newPetNpcClass)
            L2_LOG_ERROR(Log,
                "[pet] Evolution rejected for character %u: cannot create the new collar type %d / NPC class %d.",
                m_characterId, newCollarType, newPetNpcClass);
            oldPet.Release();
            oldCollar.Release();
            return false;
        }

        auto& newPet = newPetStorage.emplace(g_petDb.GetPet(newItemId, worldId));
        if (!newPet) {
            oldPet.Release();
            oldCollar.Release();
            return false;
        }

        newPet->m_spellPoint = oldPetSpellPoint;
        newPet->Save();
        inventory->PopItem(oldCollarId);
        if (!oldCollar->Delete(true, false)) {
            // Log.Add(LOG_ERROR, "[%s][%d] item delete fail[%d]", "User.cpp", 0x2122, oldCollarId)
            // Log.Add(LOG_ERROR,
            //     "[pet] Evolution for character %u created collar %d, but deleting old collar %d failed. Evolution cleanup continues.",
            //     m_characterId, newItemId, oldCollarId)
            L2_LOG_ERROR(Log,
                "[pet] Evolution for character %u created collar %d, but deleting old collar %d failed; evolution cleanup continues.",
                m_characterId, newItemId, oldCollarId);
        }
        g_petDb.DeletePet(oldCollarId, false, worldId);
        if (preserveName != 0) {
            int setNameResult = 0;
            newPet->SetName(oldPetName.c_str(), setNameResult);
        }

        socket->Send(
            "cddcddddddQQ", 0x60, requestId, 1, static_cast<int>(sameExperience), oldCollarId, newCollarType, newItemId,
            // L2Server reads these values as the new pet class and meal.
            newLevel, newPetNpcClass, meal, static_cast<long long>(oldPetSpellPoint), static_cast<long long>(newExperience)
        );
        WriteEvolvePetAudit(*this, oldPetNpcClass, oldCollarId, oldLevel, preserveName, newPetNpcClass, newLevel);

        newPet.Release();
        oldPet.Release();
        oldCollar.Release();
        return true;
    }();
    if (!evolutionSucceeded) {
        sendFailure();
    }
    return evolutionSucceeded;
}

void CUser::SetCheckSumOnDB() {
    L2_GUARD_FUNCTION();
    if (!g_bUseDBSecurity || m_itemCheckPending) {
        return;
    }
    CWareHouseSP inventory = GetWareHouse(0, true);
    if (inventory)
        inventory->SetCheckSumOnDB();
    CWareHouseSP warehouse = GetWareHouse(1, true);
    if (warehouse)
        warehouse->SetCheckSumOnDB();
}

bool CUser::ItemcheckOnDB() {
    L2_GUARD_FUNCTION();
    if (!g_bUseDBSecurity || !m_itemCheckPending) {
        return true;
    }
    CWareHouseSP inventory = GetWareHouse(0, true);
    CWareHouseSP warehouse = GetWareHouse(1, true);
    if (!inventory || !warehouse)
        return false;
    if (inventory->ItemcheckOnDB() && warehouse->ItemcheckOnDB()) {
        m_itemCheckPending = false;
        return true;
    }
    m_itemCheckPending = true;
    return false;
}

bool CUser::ItemCheckBetweenMemoryAndDB() {
    L2_GUARD_FUNCTION();
    if (!g_bUseDBSecurity)
        return true;
    CWareHouseSP inventory = GetWareHouse(0, true);
    CWareHouseSP warehouse = GetWareHouse(1, true);
    return inventory && warehouse && inventory->ItemCheckBetweenMemoryAndDB() && warehouse->ItemCheckBetweenMemoryAndDB();
}

void CUser::SetLoadTime() {
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetUserLoadTime (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.ExecutePrepared(statement);
}

void CUser::SetUnloadTime() {
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetUserUnloadTime (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.ExecutePrepared(statement);
}

void CUser::OnItemCheckFailed(int checkFailType) {
    L2_GUARD_FUNCTION();
    if (!g_bUseDBSecurity)
        return;
    WriteUserItemCheckAudit(*this, g_nDBSecurityPenaltyLevel, checkFailType);
    CSocket* socket = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (socket) {
        socket->Send("chdddd", 0xc6, 0x9f, static_cast<int>(m_characterId), m_accountId, 0, g_nDBSecurityPenaltyLevel);
    }
}

void CUser::ItemCheckOnLoad() {
    L2_GUARD_FUNCTION();
    if (!g_bUseDBSecurity)
        return;
    SetLoadTime();
    if (!ItemcheckOnDB())
        OnItemCheckFailed(0);
}

void CUser::ItemCheckOnUnload() {
    L2_GUARD_FUNCTION();
    if (!g_bUseDBSecurity)
        return;
    if (!ItemCheckBetweenMemoryAndDB()) {
        OnItemCheckFailed(1);
    } else {
        SetCheckSumOnDB();
    }
    SetUnloadTime();
}

int CUser::LoadPetItemCount() {
    // Current Cached.exe 0x005b5730.
    L2_GUARD_FUNCTION();

    std::uint32_t itemId = 0;
    std::uint32_t itemType = 0;
    std::int64_t amount = 0;
    int enchant = 0;
    int eroded = 0;
    int bless = 0;
    int ident = 0;
    int wished = 0;
    int variationOption1 = 0;
    int variationOption2 = 0;
    int appearanceType = 0;
    short attackAttributeType = 0;
    short attackAttributeValue = 0;
    std::array<short, 6> defendAttributeValues{};
    short hLocked = 0;
    short useCount = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&itemId);
    connection.Bind(&itemType);
    connection.Bind(&amount);
    connection.Bind(&enchant);
    connection.Bind(&eroded);
    connection.Bind(&bless);
    connection.Bind(&ident);
    connection.Bind(&wished);
    connection.Bind(&variationOption1);
    connection.Bind(&variationOption2);
    connection.Bind(&appearanceType);
    connection.Bind(&attackAttributeType);
    connection.Bind(&attackAttributeValue);
    for (short& defendAttributeValue : defendAttributeValues) {
        connection.Bind(&defendAttributeValue);
    }
    connection.Bind(&hLocked);
    connection.Bind(&useCount);

    constexpr const wchar_t* statement = L"{Call lin_LoadPetItems (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);

    int itemCount = 0;
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            if (amount < 0) {
                // Log.Add(
                //     LOG_ERROR,
                //     "[%s][%d]item count[%d] of pet is invalid. "
                //     "pet[%d] item[%d]",
                //     "User.cpp", 0x22e2, runtime::Int32FromBits(static_cast<std::uint32_t>(amount)),
                //     static_cast<int>(m_characterId), static_cast<int>(itemId)
                // )
                // Log.Add(LOG_ERROR,
                //     "[pet] Skipping item %u with negative amount %lld while counting items for owner %u.",
                //     static_cast<unsigned>(itemId), static_cast<long long>(amount), m_characterId)
                L2_LOG_ERROR(Log,
                    "[pet] Skipping item %u with negative amount %lld while counting items for owner %u.",
                    static_cast<unsigned>(itemId), static_cast<long long>(amount), m_characterId);
            } else {
                ++itemCount;
            }
        }
    }
    return itemCount;
}

bool CUser::SaveSSQDawnRound(int dawnRound) {
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetUserSSQDawnRound (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&dawnRound);
    // Shipping 0x005b1e68 ignores the SQL result, updates memory even after
    // an execution failure and returns true unconditionally.
    connection.ExecutePrepared(statement);
    m_ssqDawnRound = dawnRound;
    return true;
}

CWareHouseSP CUser::GetWareHouse(int warehouseType, bool loadItems) {
    L2_GUARD_FUNCTION();

    if (GetReloadFlag() && (warehouseType == 0 || warehouseType == 1 || warehouseType == 4 || warehouseType == 6)) {
        // Log.Add(
        //     LOG_ERROR, "[User.cpp][CUser::GetWareHouse] dbid[%u] warehouse[%d]", m_characterId, warehouseType
        // )
        // Log.Add(LOG_ERROR,
        //     "[warehouse] Character %u requested warehouse %d while its reload flag is set. Lookup continues.",
        //     m_characterId, warehouseType)
        L2_LOG_ERROR(Log,
            "[warehouse] Character %u requested warehouse %d while its reload flag is set; lookup continues.",
            m_characterId, warehouseType);
        runtime::process::ReportNamedCallStack(L"CUser::GetWareHouse", 2, false);
    }

    CWareHouseSP warehouse;
    switch (static_cast<WareHouseType>(warehouseType)) {
    case WareHouseType_Inventory:
        warehouse = m_inventory.GetObjectW();
        break;
    case WareHouseType_CharWarehouse:
        warehouse = m_warehouse.GetObjectW();
        break;
    case WareHouseType_Manor:
        warehouse = m_manor.GetObjectW();
        break;
    case WareHouseType_Mail:
        warehouse = m_mail.GetObjectW();
        break;
    default:
        // Current 0x005a2f20 falls back to inventory for an
        // unknown warehouse number.
        warehouse = m_inventory.GetObjectW();
        break;
    }
    if (warehouse && loadItems && !warehouse->ItemsLoaded()) {
        warehouse->LoadItems();
    }
    return warehouse;
}

int CUser::GetItemWare(int itemId) {
    // Exact current Cached.exe 0x005a16e4: the user object itself is not
    // consulted after entering the method. Missing cache entries map to the
    // inventory value zero, just as in the shipping body.
    L2_GUARD_FUNCTION();
    CItemSP item = CItem::Load(static_cast<unsigned int>(itemId));
    return item ? item->Warehouse() : 0;
}

int CUser::DelItem(int itemId) {
    // Exact current Cached.exe 0x005a17c8. The owner object is not consulted:
    // this legacy path detaches a cached item directly, persists that state,
    // and then runs the ordinary non-hard, non-premium deletion owner.
    L2_GUARD_FUNCTION();

    CItemSP item = CItem::Load(static_cast<unsigned int>(itemId));
    if (!item)
        return 0;

    item->setOwnerID(0);
    item->Save(true);
    item->Delete(false, false);
    return 1;
}

bool CUser::SetSeizedItemOwner(int itemId, int newOwnerId [[maybe_unused]], CWareHouse* destinationWarehouse) {
    // Exact current whole-item overload at Cached.exe 0x005a1948. The
    // source-level newOwnerId parameter is not read by this revision. The
    // destination warehouse's owner field is authoritative.
    L2_GUARD_FUNCTION();

    if (GetItemWare(itemId) != 1001)
        return false;

    CItemSP item = CItem::Load(static_cast<unsigned int>(itemId));
    if (!item || item->OwnerID() != m_characterId) {
        return false;
    }
    if (item->OwnerID() == destinationWarehouse->GetOwnerId()) {
        return false;
    }

    {
        L2_WRITE_GUARD(*destinationWarehouse);
        const int consumeType = g_readFile.GetConsumeType(static_cast<int>(item->ItemType()));

        // The target sets the result before dispatching on consume type. An
        // unsupported type therefore still reports success after unlocking.
        bool transferWholeItem = false;
        if (consumeType == 1 || consumeType == 3) {
            transferWholeItem = true;
        } else if (consumeType == 2 || consumeType == 4) {
            CItemSP destinationItem = destinationWarehouse->GetItemByType(static_cast<int>(item->ItemType()), 0);
            if (!destinationItem) {
                transferWholeItem = true;
            } else {
                destinationItem->SetAmount(runtime::AddInt64Wrapping(destinationItem->Amount(), item->Amount()));
                destinationItem->Save(true);
                item->Delete(true, true);
            }
        }

        if (transferWholeItem) {
            item->setOwnerID(destinationWarehouse->GetOwnerId());
            item->SetWarehouse(static_cast<int>(destinationWarehouse->GetWarehouseNum()));
            item->Save(true);
            destinationWarehouse->PushItem(item.get());
        }
    }
    return true;
}

bool CUser::SetSeizedItemOwner(int itemId, std::int64_t amount, int newOwnerId, CWareHouseSP destinationWarehouse) {
    // Exact amount-aware current overload at Cached.exe 0x005a1dfc. The
    // source is a commission item (warehouse 1001). Normal/charge items move
    // as one object, while asset/stackable items merge or split by amount.
    L2_GUARD_FUNCTION();

    if (GetItemWare(itemId) != 1001)
        return false;

    CItemSP sourceItem = CItem::Load(static_cast<unsigned int>(itemId));
    if (!sourceItem || sourceItem->Amount() < amount)
        return false;
    if (sourceItem->OwnerID() != m_characterId)
        return false;

    CWareHouse* const destination = destinationWarehouse.get();
    if (sourceItem->OwnerID() == destination->GetOwnerId()) {
        return false;
    }

    bool result;
    {
        L2_WRITE_GUARD(*destination);
        const int consumeType = g_readFile.GetConsumeType(static_cast<int>(sourceItem->ItemType()));

        // The target sets its result before the consume-type switch. Therefore
        // an unsupported consume type still returns true after releasing the
        // destination lock. Retain that observable behavior.
        result = true;
        bool transferCreated = false;

        if (consumeType == 1 || consumeType == 3) {
            if (amount != 1) {
                result = false;
            } else {
                sourceItem->setOwnerID(destination->GetOwnerId());
                sourceItem->SetWarehouse(static_cast<int>(destination->GetWarehouseNum()));
                sourceItem->Save(true);
                destination->PushItem(sourceItem.get());
            }
        } else if (consumeType == 2 || consumeType == 4) {
            CItemSP destinationItem = destination->GetItemByType(static_cast<int>(sourceItem->ItemType()), 0);
            if (destinationItem) {
                destinationItem->SetAmount(runtime::AddInt64Wrapping(destinationItem->Amount(), amount));
                destinationItem->Save(true);
                transferCreated = true;
            } else {
                CItemSP newItem = destination->MakeNewItem(
                    static_cast<unsigned int>(newOwnerId), sourceItem->ItemType(), amount, sourceItem->Enchant(),
                    sourceItem->Eroded(), sourceItem->Bless(), sourceItem->Ident(), sourceItem->Wished(),
                    static_cast<int>(destination->GetWarehouseNum()), sourceItem->OptionKey(), sourceItem->m_intensiveItemType,
                    sourceItem->Attribute(), sourceItem->HLocked(), sourceItem->UseCount()
                );
                if (newItem) {
                    newItem->Save(true);
                    destination->PushItem(newItem.get());
                    transferCreated = true;
                    if (g_readFile.IsPremiumItem(static_cast<int>(newItem->ItemType()))) {
                        newItem->LogPremiumRelated(
                            static_cast<PremiumItemLogType>(0), 0, amount, static_cast<PremiumItemLogReason>(2)
                        );
                    }
                }
            }

            if (transferCreated) {
                if (sourceItem->Amount() > amount) {
                    sourceItem->SetAmount(runtime::SubtractInt64Wrapping(sourceItem->Amount(), amount));
                    sourceItem->Save(true);
                } else {
                    sourceItem->Delete(true, true);
                }
            }
        }

    }
    return result;
}

bool CUser::IsLoadedWareHouse(int warehouseType) {
    // Current Cached.exe 0x005a3364. The target starts with inventory and
    // replaces only the character warehouse or manor for values 1/4.
    // every other value tests inventory.
    L2_GUARD_FUNCTION();

    CWareHouseSP warehouse = m_inventory.GetObjectW();
    if (warehouseType == WareHouseType_CharWarehouse) {
        warehouse = m_warehouse.GetObjectW();
    } else if (warehouseType == WareHouseType_Manor) {
        warehouse = m_manor.GetObjectW();
    }
    return warehouse && warehouse->ItemsLoaded();
}

CWareHouseSP CUser::GetWareHouseForRaid(int warehouseType) {
    // Current Cached.exe 0x005a3134.
    L2_GUARD_FUNCTION();

    if (!GetReloadFlag() && (warehouseType == 0 || warehouseType == 1 || warehouseType == 4 || warehouseType == 6)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[User.cpp][CUser::GetWareHouseForRaid] dbid[%d] "
        //     "warehouse[%d]",
        //     static_cast<int>(m_characterId), warehouseType
        // )
        // Log.Add(LOG_ERROR,
        //     "[warehouse] Raid reload requested for character %u, warehouse %d, while its reload flag is clear. Reload continues.",
        //     m_characterId, warehouseType)
        L2_LOG_ERROR(Log,
            "[warehouse] Raid reload requested for character %u, warehouse %d, while its reload flag is clear; reload continues.",
            m_characterId, warehouseType);
        runtime::process::ReportNamedCallStack(L"CUser::GetWareHouseForRaid", 2, false);
    }

    CWareHouseSP warehouse;
    switch (static_cast<WareHouseType>(warehouseType)) {
    case WareHouseType_Inventory:
        warehouse = m_inventory.GetObjectW();
        break;
    case WareHouseType_CharWarehouse:
        warehouse = m_warehouse.GetObjectW();
        break;
    case WareHouseType_Manor:
        warehouse = m_manor.GetObjectW();
        break;
    case WareHouseType_Mail:
        warehouse = m_mail.GetObjectW();
        break;
    default:
        warehouse = m_inventory.GetObjectW();
        break;
    }

    if (warehouse) {
        warehouse->DeleteAllItem(false);
        warehouse->LoadItems();
    }
    return warehouse;
}

void CUser::SaveTutorial() {
    // Current Cached.exe 0x005a0874. The shipping body takes an unlocked
    // 0x80-byte snapshot, binds it before the character ID and reports the
    // statement text when the direct ODBC execution fails.
    L2_GUARD_FUNCTION();

    std::array<char, 0x80> tutorialFlags{};
    m_loadedInfo.tutorialFlags.GetFlagAll(tutorialFlags.data(), tutorialFlags.size());

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateTutorial (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameterBinary(tutorialFlags.data(), tutorialFlags.size());
    const int characterId = static_cast<int>(m_characterId);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(LOG_ERROR, L"%s", statement)
        // Log.Add(LOG_ERROR,
        //     L"[quest] Cannot save tutorial flags for character %u. SQL: %ls",
        //     m_characterId, statement)
        // Log.Add(LOG_ERROR,
        //     L"[quest] Cannot save tutorial flags for character %u. SQL: \"%ls\"",
        //     m_characterId, statement)
        L2_LOG_ERROR(Log, u"[quest] Cannot save tutorial flags for character %u. SQL: \"%ls\"", m_characterId, statement);
    }
}

void CUser::SendTutorial(CSocket* socket, int userSocketId) {
    // Current Cached.exe 0x005a2a74. The apparently extra packet-helper
    // assembly is present in the shipping body. The reply itself sends the
    // same raw 0x80-byte snapshot.
    L2_GUARD_FUNCTION();

    std::array<char, 0x80> tutorialFlags{};
    if (!socket)
        return;

    std::optional<CPacketHelperEx<20480>> packetStorage;
    {
        L2_READ_GUARD(m_dataLock);
        m_loadedInfo.tutorialFlags.GetFlagAll(tutorialFlags.data(), tutorialFlags.size());
        auto& packet = packetStorage.emplace();
        packet.Assemble("b", static_cast<int>(tutorialFlags.size()), tutorialFlags.data());
    }

    socket->Send("chdb", 0xc6, 0x51, userSocketId, static_cast<int>(tutorialFlags.size()), tutorialFlags.data());
}

void CUser::SetTutorial(int tutorialId) {
    // Current Cached.exe 0x005a2c20 and the exact Shared
    // CBitStorage<1024,int,32>::SetFlag instantiation at 0x005a0bd4.
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(*this);
        m_loadedInfo.tutorialFlags.SetFlag(tutorialId, true);
    }
    SaveTutorial();
}

bool CUser::SetSelectedQuest(int questId, int questState) {
    // Current Cached.exe 0x0059f68c.
    L2_GUARD_FUNCTION();

    MemoStruct* selected;
    {
        L2_WRITE_GUARD(*this);
        selected = nullptr;
        for (MemoStruct& memo : m_questMemos) {
            if (memo.nQuestID == questId) {
                selected = &memo;
                break;
            }
        }
        if (selected == nullptr) {
            for (MemoStruct& memo : m_questMemos) {
                if (memo.nQuestID == 0) {
                    memo.nQuestID = questId;
                    selected = &memo;
                    break;
                }
            }
        }
        if (selected != nullptr) {
            selected->bChanged = true;
            selected->nState = questState;
        }
    }
    return selected != nullptr;
}

bool CUser::SetSelectedQuest(int questId, int questState, int questState2, int questState3, int questState4) {
    // Current Cached.exe 0x0059f7c8.
    L2_GUARD_FUNCTION();

    MemoStruct* selected;
    {
        L2_WRITE_GUARD(*this);
        selected = nullptr;
        for (MemoStruct& memo : m_questMemos) {
            if (memo.nQuestID == questId) {
                selected = &memo;
                break;
            }
        }
        if (selected == nullptr) {
            for (MemoStruct& memo : m_questMemos) {
                if (memo.nQuestID == 0) {
                    memo.nQuestID = questId;
                    selected = &memo;
                    break;
                }
            }
        }
        if (selected != nullptr) {
            selected->nState = questState;
            selected->nState2 = questState2;
            selected->nState3 = questState3;
            selected->nState4 = questState4;
            selected->bChanged = true;
        }
    }
    return selected != nullptr;
}

bool CUser::DeleteSelectedQuest(int questId) {
    // Current Cached.exe 0x0059f930.
    L2_GUARD_FUNCTION();

    bool deleted;
    {
        L2_WRITE_GUARD(*this);
        deleted = false;
        for (MemoStruct& memo : m_questMemos) {
            if (memo.nQuestID != questId)
                continue;
            memo.nJournal = 1;
            memo.nState = 0;
            memo.nState2 = 0;
            memo.nState3 = 0;
            memo.nState4 = 0;
            memo.bChanged = false;
            memo.bDeleted = true;
            deleted = true;
            break;
        }
    }
    return deleted;
}

void CUser::SetOneTimeQuest(int questId, bool enabled) {
    // Current Cached.exe 0x005a15d0.
    L2_GUARD_FUNCTION();

    if (questId >= 0x280f) {
        if (questId > 0x2f0f) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"current onetime quest id(%d) is over "
            //     L"onetimequest_end_id(%d)!!",
            //     questId, 0x2f0f
            // )
            // Log.Add(LOG_WARN,
            //     "[quest] Rejecting one-time quest %d for character %u: the highest supported ID is %d.",
            //     questId, m_characterId, 0x2f0f)
            L2_LOG_WARN(Log,
                "[quest] Rejecting one-time quest %d for character %u: the highest supported ID is %d.",
                questId, m_characterId, 0x2f0f);
            return;
        }
        questId -= 10000;
    }

    {
        L2_WRITE_GUARD(*this);
        m_loadedInfo.questFlags.SetFlag(questId, enabled);
    }
}

bool CUser::DeleteSelectedNRQuest(int questId) {
    // Current Cached.exe 0x005ade34.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{call lin_DeleteUserQuest (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&questId);
    const bool success = connection.ExecutePrepared(statement);
    if (!success) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Fail to delete NRMemo. "
        //     L"char[%d] Quest ID[%d]",
        //     L"User.cpp", 0x162a, m_characterId, questId
        // )
        // Log.Add(LOG_ERROR,
        //     "[quest] Cannot delete quest %d for character %u.",
        //     questId, m_characterId)
        L2_LOG_ERROR(Log,
            "[quest] Cannot delete quest %d for character %u.",
            questId, m_characterId);
    }
    return success;
}

void CUser::SaveOneTimeQuest() {
    L2_GUARD_FUNCTION();

    // Current 0x0059e86c takes this unlocked snapshot before entering the
    // ODBC path. SetQuest calls it only after releasing m_dataLock.
    std::array<std::uint8_t, 0x100> questFlags{};
    m_loadedInfo.questFlags.GetFlagAll(reinterpret_cast<char*>(questFlags.data()), questFlags.size());
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateOneTimeQuest (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameterBinary(questFlags.data(), questFlags.size());
    connection.BindParameter(&m_characterId);
    std::uint32_t durationMilliseconds = 0;
    const bool success = connection.ExecutePreparedDirect(statement, durationMilliseconds);
    if (durationMilliseconds > 1000U) {
        g_qsCheck.AddSlowQs(statement, durationMilliseconds);
    }
    if (durationMilliseconds > static_cast<std::uint32_t>(GetCacheDConfiguration().sqlExecLimitMilliseconds)) {
        // Log.AddT(LOG_ERROR, L"sql execution time [%d]ms, sql[%s]", durationMilliseconds, statement)
        // Log.Add(LOG_WARN,
        //     L"[quest] One-time quest write for character %u took %u ms, exceeding the configured limit of %u ms. SQL: %ls",
        //     m_characterId, static_cast<unsigned>(durationMilliseconds),
        //     static_cast<unsigned>(GetCacheDConfiguration().sqlExecLimitMilliseconds), statement)
        // Log.Add(LOG_WARN,
        //     L"[quest] One-time quest write for character %u took %u ms, exceeding the configured limit of %u ms. SQL: \"%ls\"",
        //     m_characterId, static_cast<unsigned>(durationMilliseconds),
        //     static_cast<unsigned>(GetCacheDConfiguration().sqlExecLimitMilliseconds), statement)
        L2_LOG_WARN(
            Log,
            u"[quest] One-time quest write for character %u took %u ms, exceeding the configured limit of %u ms. SQL: "
            u"\"%ls\"",
            m_characterId, static_cast<unsigned>(durationMilliseconds),
            static_cast<unsigned>(GetCacheDConfiguration().sqlExecLimitMilliseconds), statement
        );
    }
    if (!success) {
        // Log.Add(LOG_ERROR, statement)
        // Log.Add(LOG_ERROR,
        //     L"[quest] Cannot save one-time quest flags for character %u. SQL: %ls",
        //     m_characterId, statement)
        // Log.Add(LOG_ERROR,
        //     L"[quest] Cannot save one-time quest flags for character %u. SQL: \"%ls\"",
        //     m_characterId, statement)
        L2_LOG_ERROR(
            Log, u"[quest] Cannot save one-time quest flags for character %u. SQL: \"%ls\"", m_characterId, statement
        );
    }
}

void CUser::SaveMemo() {
    L2_GUARD_FUNCTION();

    std::optional<DBConn> connectionStorage;
    {
        L2_WRITE_GUARD(m_dataLock);
        auto& connection = connectionStorage.emplace(runtime::process::CurrentDatabaseId());
        for (std::size_t index = 0; index < m_questMemos.size(); ++index) {
            MemoStruct& memo = m_questMemos[index];
            if (memo.nQuestID <= 0)
                continue;

            if (!memo.bDeleted) {
                if (memo.bChanged) {
                    constexpr const wchar_t* updateStatement = L"{Call lin_UpdateUserQuest "
                                                               L"(?, ?, ?, ?, ?, ?, ?, 0)}";
                    connection.Prepare(updateStatement);
                    connection.BindParameter(&m_characterId);
                    connection.BindParameter(&memo.nQuestID);
                    connection.BindParameter(&memo.nJournal);
                    connection.BindParameter(&memo.nState);
                    connection.BindParameter(&memo.nState2);
                    connection.BindParameter(&memo.nState3);
                    connection.BindParameter(&memo.nState4);
                    const bool updated =
                        connection.ExecutePrepared(updateStatement);
                    if (updated) {
                        memo.bChanged = false;
                    } else {
                        // Log.Add(LOG_ERROR,
                        //     "[quest] Cannot save quest %d in memo slot %zu for character %u. The memo remains marked for saving.",
                        //     memo.nQuestID, index, m_characterId)
                        L2_LOG_ERROR(Log,
                            "[quest] Cannot save quest %d in memo slot %zu for character %u; the memo remains marked for saving.",
                            memo.nQuestID, index, m_characterId);
                    }
                    if (std::getenv("L2_QUEST_TRACE") != nullptr) {
                        // std::fprintf(
                        //     stderr,
                        //     "[QUEST_TRACE] CacheD::SaveMemo character=%u "
                        //     "slot=%zu quest=%d journal=%d state=%d,%d,%d,%d "
                        //     "updated=%d\n",
                        //     m_characterId,
                        //     index,
                        //     memo.nQuestID,
                        //     memo.nJournal,
                        //     memo.nState,
                        //     memo.nState2,
                        //     memo.nState3,
                        //     memo.nState4,
                        //     updated ? 1 : 0)
                        // Log.Add(LOG_NORMAL,
                        //     "[quest trace] Character %u, memo slot %zu, quest %d: journal %d, state (%d, %d, %d, %d). Database update %s.",
                        //     m_characterId, index, memo.nQuestID, memo.nJournal, memo.nState, memo.nState2, memo.nState3, memo.nState4, updated ? "succeeded" : "failed")
                        L2_LOG_INFO(Log,
                            "[quest trace] Character %u, memo slot %zu, quest %d: journal %d, state (%d, %d, %d, %d); database update %s.",
                            m_characterId, index, memo.nQuestID, memo.nJournal, memo.nState, memo.nState2, memo.nState3, memo.nState4, updated ? "succeeded" : "failed");
                    }
                }
            } else {
                constexpr const wchar_t* deleteStatement = L"{Call lin_DeleteUserQuest (?, ?)}";
                connection.Prepare(deleteStatement);
                connection.BindParameter(&m_characterId);
                connection.BindParameter(&memo.nQuestID);
                if (connection.ExecutePrepared(deleteStatement)) {
                    // Noise: Successful per-memo deletion is routine persistence. Failures keep quest/character context below.
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "[NO_ERROR] QuestClearLog UserID[%d], "
                    //     "UserName[%ls], Index[%d], QuestID[%d]",
                    //     static_cast<std::int32_t>(m_characterId), m_characterName.c_str(), static_cast<int>(index), memo.nQuestID
                    // )
                    memo.nQuestID = 0;
                    for (UserNPCLog& npcLog : memo.NPCLog) {
                        npcLog.userLogType = USER_NPC_LOG_MAX;
                        npcLog.nNPCID = 0;
                        base_structure_detail::InterlockedStore(&npcLog.nCount, 0);
                    }
                    // Journal/state and bChanged survive. Current
                    // 0x005a8ad4 clears only the ID, ten NPC logs and this flag.
                    memo.bDeleted = false;
                } else {
                    // Log.Add(LOG_ERROR,
                    //     "[quest] Cannot delete quest %d in memo slot %zu for character %u. The memo remains marked for deletion.",
                    //     memo.nQuestID, index, m_characterId)
                    L2_LOG_ERROR(Log,
                        "[quest] Cannot delete quest %d in memo slot %zu for character %u; the memo remains marked for deletion.",
                        memo.nQuestID, index, m_characterId);
                }
            }
            connection.UnBind();
        }
    }
}

void CUser::SetQuest(const std::uint8_t* packet, int characterId) {
    L2_GUARD_FUNCTION();

    if (!m_loggedIn) {
        // Log.Add(LOG_ERROR, "SetQuest for logout user, id[%d]", characterId)
        // Log.Add(LOG_WARN,
        //     "[quest] Ignoring quest update for character %d: not logged in.",
        //     characterId)
        L2_LOG_WARN(Log,
            "[quest] Ignoring quest update for character %d: not logged in.",
            characterId);
        return;
    }

    bool logOldValues = true;
    const std::uint8_t* scan = packet;
    for (std::size_t index = 0; index < m_questMemos.size(); ++index) {
        int questId = 0;
        int journal = 1;
        int state = 0;
        int state2 = 0;
        int state3 = 0;
        int state4 = 0;
        scan = ::Disassemble(scan, "dddddd", &questId, &journal, &state, &state2, &state3, &state4);
        // The current prepass tests quest ID and the first state field, not
        // journal. Only the 41-record all-clear form logs prior memo values.
        if (questId != 0 || state != 0) {
            logOldValues = false;
            break;
        }
    }

    bool anyMemoChanged = false;
    const std::uint8_t* cursor = packet;
    std::array<std::uint8_t, 0x100> oldQuestFlags;
    std::array<std::uint8_t, 0x100> newQuestFlags;
    {
        L2_WRITE_GUARD(m_dataLock);
        for (std::size_t index = 0; index < m_questMemos.size(); ++index) {
            MemoStruct& memo = m_questMemos[index];
            const int oldQuestId = memo.nQuestID;
            const int oldJournal = memo.nJournal;
            const int oldState = memo.nState;
            const int oldState2 = memo.nState2;
            const int oldState3 = memo.nState3;
            const int oldState4 = memo.nState4;

            int questId = 0;
            int journal = 1;
            int state = 0;
            int state2 = 0;
            int state3 = 0;
            int state4 = 0;
            cursor = ::Disassemble(cursor, "dddddd", &questId, &journal, &state, &state2, &state3, &state4);

            if (oldQuestId == questId && oldJournal == journal && oldState == state && oldState2 == state2 &&
                oldState3 == state3 && oldState4 == state4) {
                continue;
            }

            if (logOldValues) {
                WriteQuestMemoAudit(*this, static_cast<int>(index) + 1000000, oldQuestId, oldJournal);
            }

            if (oldQuestId == questId || questId != 0) {
                if (oldQuestId != questId)
                    memo.nQuestID = questId;
                memo.bChanged = true;
                memo.nJournal = journal;
                memo.nState = state;
                memo.nState2 = state2;
                memo.nState3 = state3;
                memo.nState4 = state4;
            } else {
                // Current deletion keeps the old values until SaveMemo has
                // successfully deleted their database rows.
                memo.bDeleted = true;
            }
            anyMemoChanged = true;
            WriteQuestMemoAudit(*this, static_cast<int>(index), memo.nQuestID, memo.nJournal);
        }

        oldQuestFlags = {};
        m_loadedInfo.questFlags.GetFlagAll(reinterpret_cast<char*>(oldQuestFlags.data()), oldQuestFlags.size());
        m_loadedInfo.questFlags.SetFlagAll(reinterpret_cast<char*>(const_cast<std::uint8_t*>(cursor)));
        newQuestFlags = {};
        m_loadedInfo.questFlags.GetFlagAll(reinterpret_cast<char*>(newQuestFlags.data()), newQuestFlags.size());
    }

    if (oldQuestFlags != newQuestFlags) {
        const std::array<std::uint8_t, 0x100> emptyFlags{};
        if (newQuestFlags == emptyFlags) {
            for (int chunk = 0; chunk < 4; ++chunk) {
                WriteQuestFlagAudit(*this, 0x1f70, chunk, oldQuestFlags.data() + chunk * 0x40);
            }
        }
        for (int chunk = 0; chunk < 4; ++chunk) {
            WriteQuestFlagAudit(*this, 0x36a, chunk, newQuestFlags.data() + chunk * 0x40);
        }
        SaveOneTimeQuest();
    }
    if (std::getenv("L2_QUEST_TRACE") != nullptr) {
        // std::fprintf(
        //     stderr,
        //     "[QUEST_TRACE] CacheD::SetQuest character=%d changed=%d\n",
        //     characterId,
        //     anyMemoChanged ? 1 : 0)
        // Log.Add(LOG_NORMAL,
        //     "[quest trace] Character %d: quest memo changes %s.",
        //     characterId, anyMemoChanged ? "will be saved" : "not detected")
        L2_LOG_INFO(Log,
            "[quest trace] Character %d: quest memo changes %s.",
            characterId, anyMemoChanged ? "will be saved" : "not detected");
    }
    if (anyMemoChanged)
        SaveMemo();
}

void CUser::LoadQuest(CSocket* socket, int characterId [[maybe_unused]], unsigned int userSocketId) {
    L2_GUARD_FUNCTION();
    if (!socket)
        return;

    std::array<char, 20480> assembledQuest{};
    int assembledLength = 0;

    std::array<std::uint8_t, 0x100> questFlags;
    int flagsResult;
    {
        L2_READ_GUARD(m_dataLock);
        for (const MemoStruct& memo : m_questMemos) {
            const int result = ::Assemble(
                assembledQuest.data() + assembledLength, static_cast<int>(assembledQuest.size()) - assembledLength, "dddddd",
                memo.nQuestID, memo.nJournal, memo.nState, memo.nState2, memo.nState3, memo.nState4
            );
            if (result > 0)
                assembledLength += result;
        }
        questFlags = {};
        m_loadedInfo.questFlags.GetFlagAll(reinterpret_cast<char*>(questFlags.data()), questFlags.size());
        flagsResult = ::Assemble(
            assembledQuest.data() + assembledLength, static_cast<int>(assembledQuest.size()) - assembledLength, "b",
            static_cast<int>(questFlags.size()), questFlags.data()
        );
        if (flagsResult > 0)
            assembledLength += flagsResult;
    }

    socket->Send("cdb", 0x13, userSocketId, assembledLength, assembledQuest.data());
}

void CUser::LoadOneDayReward(CSocket* socket, unsigned int userSocketId) {
    L2_GUARD_FUNCTION();
    if (!socket)
        return;

    int dailyMissionDbId = 0;
    int updateCheckTime = 0;
    int accumulateCount = 0;
    int stateType = 0;
    int timeRewarded = 0;
    CPacketHelperEx<8192> rewards;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&dailyMissionDbId);
    connection.Bind(&updateCheckTime);
    connection.Bind(&accumulateCount);
    connection.Bind(&stateType);
    connection.Bind(&timeRewarded);

    constexpr const wchar_t* statement = L"{Call lin_LoadOneDayReward(?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    const bool executed = connection.ExecutePrepared(statement);

    int rewardCount = 0;
    if (executed) {
        while (connection.Fetch()) {
            rewards.Assemble("ddddd", dailyMissionDbId, updateCheckTime, accumulateCount, stateType, timeRewarded);
            ++rewardCount;
        }
    }

    socket->Send(
        "chddbdb", 0xc6, 0x118, userSocketId, executed ? 1 : 0, static_cast<int>(sizeof(m_createDate)), &m_createDate,
        rewardCount, rewards.GetLength(), rewards.GetPacket()
    );
}

void CUser::LoadItemReuseDelay(CSocket* socket, unsigned int userSocketId) {
    L2_GUARD_FUNCTION();
    if (!socket)
        return;

    int reuseKey = 0;
    int reuseEndTime = 0;
    CPacketHelperEx<8192> reuseDelays;
    const std::int32_t currentTime = runtime::crt::Time32Now();

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&reuseKey);
    connection.Bind(&reuseEndTime);

    constexpr const wchar_t* statement = L"{Call lin_GetItemReuseDelay(?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&currentTime);

    int reuseDelayCount = 0;
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            reuseDelays.Assemble("dd", reuseKey, reuseEndTime);
            ++reuseDelayCount;
        }
    }

    if (reuseDelayCount > 0) {
        socket->Send("chddb", 0xc6, 0x12f, userSocketId, reuseDelayCount, reuseDelays.GetLength(), reuseDelays.GetPacket());
    } else {
        socket->Send("chdd", 0xc6, 0x12f, userSocketId, 0);
    }
}

bool CUser::SetItemReuseDelay(int reuseKey, int reuseEndTime) {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetItemReuseDelay(?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&reuseKey);
    connection.BindParameter(&reuseEndTime);
    return connection.ExecutePrepared(statement);
}

bool CUser::CharAudit(std::int64_t adenaSum, std::int64_t defaultPriceSum, std::int64_t itemSum) {
    L2_GUARD_FUNCTION();

    const std::int64_t previousAdenaSum = m_adenaSum;
    const std::int64_t previousDefaultPriceSum = m_defaultPriceSum;
    if (!m_auditNeeded) {
        m_auditNeeded = true;
    } else {
        const std::int64_t priceSumDifference = previousDefaultPriceSum < defaultPriceSum
            ? defaultPriceSum - previousDefaultPriceSum
            : previousDefaultPriceSum - defaultPriceSum;
        const std::int64_t adenaSumDifference =
            previousAdenaSum < adenaSum ? adenaSum - previousAdenaSum : previousAdenaSum - adenaSum;

        const std::int64_t minimumPriceSum = std::min(previousDefaultPriceSum, defaultPriceSum);
        const int priceChangeRatio =
            minimumPriceSum < 1 ? 100 : static_cast<int>(priceSumDifference / minimumPriceSum) * 100;
        const std::int64_t minimumAdenaSum = std::min(previousAdenaSum, adenaSum);
        const int adenaChangeRatio =
            minimumAdenaSum < 1 ? 100 : static_cast<int>(adenaSumDifference / minimumAdenaSum) * 100;

        const CacheDConfiguration& configuration = GetCacheDConfiguration();
        if (priceSumDifference >= static_cast<std::int64_t>(configuration.defaultPriceLimit) ||
            (defaultPriceSum >= static_cast<std::int64_t>(configuration.defaultPriceCheck) &&
                priceChangeRatio >= configuration.priceChangeRatioLimit) ||
            adenaSumDifference >= static_cast<std::int64_t>(configuration.adenaLimit) ||
            (adenaSum >= configuration.adenaCheck && adenaChangeRatio >= configuration.adenaChangeRatioLimit)) {
            const std::u16string characterName = std::u16string(m_characterName.c_str());
            const std::u16string accountName = std::u16string(m_accountName.c_str());
            CLogFormatter formatter;
            formatter.WriteAuditLog(
                0x579, m_characterId, m_accountId, 0, 0, m_x, m_y, m_z, u"", u"", u"", adenaSum, defaultPriceSum, itemSum,
                previousAdenaSum, previousDefaultPriceSum, m_itemSum, 0, 0, 0, 0, characterName.c_str(), accountName.c_str(),
                u"", u"", 0
            );
        }
    }

    m_itemSum = itemSum;
    m_defaultPriceSum = defaultPriceSum;
    m_adenaSum = adenaSum;
    return true;
}

int CUser::GetPlayDiff() {
    L2_GUARD_FUNCTION();
    const std::uint32_t now = runtime::GetTickCount();
    const std::uint32_t elapsed = now - m_lastUpdateTick;
    m_lastUpdateTick = now;
    return static_cast<int>(elapsed / 1000U);
}

std::int64_t CUser::GetExpDiff() {
    L2_GUARD_FUNCTION();

    std::int64_t difference;
    {
        L2_WRITE_GUARD(*this);
        difference = 0;
        if (!m_skipNextExpDiff) {
            difference = runtime::SubtractInt64Wrapping(
                m_experience,
                m_previousExperience);
        }
        m_previousExperience = m_experience;
        m_skipNextExpDiff = false;
    }
    return difference;
}

int CUser::CalcKarma(int newPkCount, int pkPardon) {
    // Current Cached.exe 0x0059fae0. This 2018 body is not the old C1
    // capped floating-point formula: it also consumes the live alignment.
    L2_GUARD_FUNCTION();

    const std::int32_t difference =
        runtime::SubtractInt32Wrapping(static_cast<std::int32_t>(newPkCount), static_cast<std::int32_t>(pkPardon));
    const std::int32_t karmaIndex = difference >= 0 ? difference : 0;

    if (karmaIndex < 2 && m_alignment > 0) {
        return runtime::SubtractInt32Wrapping(0, m_alignment);
    }
    if (karmaIndex < 4) {
        return runtime::MultiplyInt32Wrapping(runtime::SubtractInt32Wrapping(-1, karmaIndex), 0x168);
    }
    return runtime::MultiplyInt32Wrapping(karmaIndex, -0x2d0);
}

void CUser::IncreasePKCount() {
    // Current Cached.exe 0x0059fbbc.
    L2_GUARD_FUNCTION();

    ++m_pkCount;
    if (m_alignment < 0x7fffb1df) {
        const int karma = CalcKarma(runtime::Int32FromBits(m_pkCount), runtime::Int32FromBits(m_pkPardon));
        m_alignment = runtime::AddInt32Wrapping(m_alignment, karma);
    }
}

void CUser::ResetPKCountWhenCursedPCDied(int originalPk) {
    // Current Cached.exe 0x0059fc74. The caller owns the CUser write lock.
    L2_GUARD_FUNCTION();
    m_alignment = 0;
    m_pkCount = static_cast<std::uint32_t>(originalPk);
}

void CUser::IncreaseDuelCount() {
    // Current Cached.exe 0x0059fd1c.
    L2_GUARD_FUNCTION();
    ++m_duelCount;
}

void CUser::LoadActiveSkillFromDB() {
    L2_GUARD_FUNCTION();

    std::array<AbnormalStatus, 96> loaded{};
    for (AbnormalStatus& status : loaded) {
        status.skillId = -1;
    }

    int skillId = 0;
    int skillLevel = 0;
    int skillSubLevel = 0;
    int skillDuration = 0;
    int skillSelfCast = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&skillId);
    connection.Bind(&skillLevel);
    connection.Bind(&skillSubLevel);
    connection.Bind(&skillDuration);
    connection.Bind(&skillSelfCast);

    constexpr const wchar_t* query = L"{Call lin_LoadUserActiveSkill (?)}";
    connection.Prepare(query);
    connection.BindParameter(&m_characterId);
    if (connection.ExecutePrepared(query)) {
        std::size_t index = 0;
        while (connection.Fetch()) {
            // Shipping code assumes the database invariant of at most 96
            // rows and otherwise writes past its 0x600-byte local array.
            // Keep all valid-row behavior while containing malformed data.
            if (index < loaded.size()) {
                loaded[index].skillId = skillId;
                loaded[index].skillLevel = static_cast<std::int16_t>(skillLevel);
                loaded[index].skillSubLevel = static_cast<std::int16_t>(skillSubLevel);
                loaded[index].skillDuration = skillDuration;
                loaded[index].skillSelfCast = skillSelfCast;
            }
            ++index;
        }
    }
    m_abnormalStatus = loaded;
}

void CUser::LoadBeautyData() {
    L2_GUARD_FUNCTION();

    int faceIndexEx = 0;
    int hairShapeIndexEx = 0;
    int hairColorIndexEx = 0;
    int hairAccessoryFlag = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&faceIndexEx);
    connection.Bind(&hairShapeIndexEx);
    connection.Bind(&hairColorIndexEx);
    connection.Bind(&hairAccessoryFlag);

    constexpr const wchar_t* query = L"{Call lin_LoadUserBeauty(?)}";
    connection.Prepare(query);
    connection.BindParameter(&m_characterId);
    if (connection.ExecutePrepared(query) && connection.Fetch()) {
        m_faceIndexEx = faceIndexEx;
        m_hairShapeIndexEx = hairShapeIndexEx;
        m_hairColorIndexEx = hairColorIndexEx;
        m_hairAccessoryFlag = hairAccessoryFlag == 1;
    }
}

bool CUser::SetDailyQuest(int questId, long completeDate) {
    // Current Cached.exe 0x005b3de0.
    L2_GUARD_FUNCTION();

    bool result;
    {
        L2_WRITE_GUARD(m_dataLock);
        result = false;
        {
            DBConn connection(runtime::process::CurrentDatabaseId());
            constexpr const wchar_t* statement = L"{Call lin_SetDailyQuest (?, ?, ?)}";
            connection.Prepare(statement);
            connection.BindParameter(&m_characterId);
            connection.BindParameter(&questId);
            // The shipping long helper carries a 32-bit value through
            // SQL_C_SLONG/SQL_DOUBLE.
            connection.BindParameter(&completeDate);
            result = connection.ExecutePrepared(statement);
            if (!result) {
                // Log.AddT(LOG_ERROR, L"set daily quest error [%s : %d]", m_characterName.c_str(), questId)
                // Log.Add(LOG_ERROR,
                //     L"[quest] Cannot save daily quest %d for character %u (%ls).",
                //     questId, m_characterId, m_characterName.c_str())
                // L2_LOG_ERROR(Log,
                //     L"[quest] Cannot save daily quest %d for character %u (%ls).",
                //     questId, m_characterId, m_characterName.c_str())
                L2_LOG_ERROR(
                    Log, u"[quest] Cannot save daily quest %d for character %u (%ls).", questId, m_characterId,
                    m_characterName.c_str()
                );
            }
        }
    }
    return result;
}

bool CUser::SetBirthday(SYSTEMTIME* birthday) {
    // Current Cached.exe 0x005b3fac.
    L2_GUARD_FUNCTION();

    if (birthday->wYear == 0) {
        // Log.AddT(
        //     LOG_ERROR, L"invalid date time. [%s : %d/%d/%d]", m_characterName.c_str(), static_cast<int>(birthday->wYear),
        //     static_cast<int>(birthday->wMonth), static_cast<int>(birthday->wDay)
        // )
        // Log.Add(LOG_WARN,
        //     L"[character] Rejecting birthday date for character %u (%ls): %u-%u-%u has a zero year.",
        //     m_characterId, m_characterName.c_str(), static_cast<unsigned>(birthday->wYear), static_cast<unsigned>(birthday->wMonth), static_cast<unsigned>(birthday->wDay))
        // L2_LOG_WARN(Log,
        //     L"[character] Rejecting birthday date for character %u (%ls): %u-%u-%u has a zero year.",
        //     m_characterId, m_characterName.c_str(), static_cast<unsigned>(birthday->wYear), static_cast<unsigned>(birthday->wMonth), static_cast<unsigned>(birthday->wDay))
        L2_LOG_WARN(
            Log, u"[character] Rejecting birthday date for character %u (%ls): %u-%u-%u has a zero year.", m_characterId,
            m_characterName.c_str(), static_cast<unsigned>(birthday->wYear), static_cast<unsigned>(birthday->wMonth),
            static_cast<unsigned>(birthday->wDay)
        );
        return false;
    }

    std::array<wchar_t, 20> birthdayText{};
    std::swprintf(
        birthdayText.data(), birthdayText.size(), L"%d-%d-%d %d:%d:%d", static_cast<int>(birthday->wYear),
        static_cast<int>(birthday->wMonth), static_cast<int>(birthday->wDay), static_cast<int>(birthday->wHour),
        static_cast<int>(birthday->wMinute), static_cast<int>(birthday->wSecond)
    );

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetBirthday (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(birthdayText.data());
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR, L"set birthday record error [%s : %d/%d/%d]", m_characterName.c_str(),
        //     static_cast<int>(birthday->wYear), static_cast<int>(birthday->wMonth), static_cast<int>(birthday->wDay)
        // )
        // Log.Add(LOG_ERROR,
        //     L"[character] Cannot save birthday record for character %u (%ls), date %u-%u-%u.",
        //     m_characterId, m_characterName.c_str(), static_cast<unsigned>(birthday->wYear), static_cast<unsigned>(birthday->wMonth), static_cast<unsigned>(birthday->wDay))
        // L2_LOG_ERROR(Log,
        //     L"[character] Cannot save birthday record for character %u (%ls), date %u-%u-%u.",
        //     m_characterId, m_characterName.c_str(), static_cast<unsigned>(birthday->wYear), static_cast<unsigned>(birthday->wMonth), static_cast<unsigned>(birthday->wDay))
        L2_LOG_ERROR(
            Log, u"[character] Cannot save birthday record for character %u (%ls), date %u-%u-%u.", m_characterId,
            m_characterName.c_str(), static_cast<unsigned>(birthday->wYear), static_cast<unsigned>(birthday->wMonth),
            static_cast<unsigned>(birthday->wDay)
        );
        return false;
    }

    SetCreateDate(birthday->wYear, birthday->wMonth, birthday->wDay, birthday->wHour, birthday->wMinute, birthday->wSecond);
    return true;
}

bool CUser::SaveBirthdayGift(SYSTEMTIME* birthday) {
    // Current Cached.exe 0x005b4204.
    L2_GUARD_FUNCTION();

    if (birthday->wYear == 0) {
        // Log.AddT(
        //     LOG_ERROR, L"invalid date time. [%s : %d/%d/%d]", m_characterName.c_str(), static_cast<int>(birthday->wYear),
        //     static_cast<int>(birthday->wMonth), static_cast<int>(birthday->wDay)
        // )
        // Log.Add(LOG_WARN,
        //     L"[character] Rejecting birthday gift date for character %u (%ls): %u-%u-%u has a zero year.",
        //     m_characterId, m_characterName.c_str(), static_cast<unsigned>(birthday->wYear), static_cast<unsigned>(birthday->wMonth), static_cast<unsigned>(birthday->wDay))
        // L2_LOG_WARN(Log,
        //     L"[character] Rejecting birthday gift date for character %u (%ls): %u-%u-%u has a zero year.",
        //     m_characterId, m_characterName.c_str(), static_cast<unsigned>(birthday->wYear), static_cast<unsigned>(birthday->wMonth), static_cast<unsigned>(birthday->wDay))
        L2_LOG_WARN(
            Log, u"[character] Rejecting birthday gift date for character %u (%ls): %u-%u-%u has a zero year.",
            m_characterId, m_characterName.c_str(), static_cast<unsigned>(birthday->wYear),
            static_cast<unsigned>(birthday->wMonth), static_cast<unsigned>(birthday->wDay)
        );
        return false;
    }

    std::array<wchar_t, 20> birthdayText{};
    std::swprintf(
        birthdayText.data(), birthdayText.size(), L"%d-%d-%d %d:%d:%d", static_cast<int>(birthday->wYear),
        static_cast<int>(birthday->wMonth), static_cast<int>(birthday->wDay), static_cast<int>(birthday->wHour),
        static_cast<int>(birthday->wMinute), static_cast<int>(birthday->wSecond)
    );

    const int eventId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveBirthdayGift (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(birthdayText.data());
    connection.BindParameter(&eventId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR, L"save birthday gift record error [%s : %d/%d/%d]", m_characterName.c_str(),
        //     static_cast<int>(birthday->wYear), static_cast<int>(birthday->wMonth), static_cast<int>(birthday->wDay)
        // )
        // Log.Add(LOG_ERROR,
        //     L"[character] Cannot save birthday gift record for character %u (%ls), date %u-%u-%u.",
        //     m_characterId, m_characterName.c_str(), static_cast<unsigned>(birthday->wYear), static_cast<unsigned>(birthday->wMonth), static_cast<unsigned>(birthday->wDay))
        // L2_LOG_ERROR(Log,
        //     L"[character] Cannot save birthday gift record for character %u (%ls), date %u-%u-%u.",
        //     m_characterId, m_characterName.c_str(), static_cast<unsigned>(birthday->wYear), static_cast<unsigned>(birthday->wMonth), static_cast<unsigned>(birthday->wDay))
        L2_LOG_ERROR(
            Log, u"[character] Cannot save birthday gift record for character %u (%ls), date %u-%u-%u.", m_characterId,
            m_characterName.c_str(), static_cast<unsigned>(birthday->wYear), static_cast<unsigned>(birthday->wMonth),
            static_cast<unsigned>(birthday->wDay)
        );
        return false;
    }
    return true;
}

bool CUser::LoadBirthInfo(CSocket* socket) {
    // Current Cached.exe 0x005b4444.
    L2_GUARD_FUNCTION();

    int giftCount = 0;
    if (m_createDate.wYear != 0) {
        const int eventId = 0;
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Bind(&giftCount);
        constexpr const wchar_t* statement = L"{Call lin_GetBirthdayGiftCount (?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&m_characterId);
        connection.BindParameter(&eventId);
        if (connection.ExecutePrepared(statement)) {
            connection.Fetch();
        }
    }

    socket->Send("chdd", 0xc6, 0x69, static_cast<int>(m_characterId), giftCount);
    return true;
}

bool CUser::LoadLastBirthdayGiftDate(CSocket* socket) {
    // Current Cached.exe 0x005b45ec.
    L2_GUARD_FUNCTION();

    int lastGiftYear = 0;
    int lastGiftMonth = 0;
    int lastGiftDay = 0;
    if (m_createDate.wYear != 0) {
        const int eventId = 0;
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Bind(&lastGiftYear);
        connection.Bind(&lastGiftMonth);
        connection.Bind(&lastGiftDay);
        constexpr const wchar_t* statement = L"{Call lin_GetLastBirthdayDate (?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&m_characterId);
        connection.BindParameter(&eventId);
        if (connection.ExecutePrepared(statement)) {
            connection.Fetch();
        }
    }

    socket->Send(
        "chddddb", 0xc6, 0x6a, static_cast<int>(m_characterId), lastGiftYear, lastGiftMonth, lastGiftDay, 0x10, &m_createDate
    );
    return true;
}

bool CUser::Load5YearInfo(CSocket* socket) {
    // Current Cached.exe 0x005b47d8.
    L2_GUARD_FUNCTION();

    int giftCount = 0;
    if (m_createDate.wYear != 0) {
        const int eventId = 1;
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Bind(&giftCount);
        constexpr const wchar_t* statement = L"{Call lin_GetBirthdayGiftCount (?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&m_characterId);
        connection.BindParameter(&eventId);
        if (connection.ExecutePrepared(statement)) {
            connection.Fetch();
        }
    }

    socket->Send("chddb", 0xc6, 0x6b, static_cast<int>(m_characterId), giftCount, 0x10, &m_createDate);
    return true;
}

bool CUser::Save5YearGift(SYSTEMTIME* birthday) {
    // Current Cached.exe 0x005b4994.
    L2_GUARD_FUNCTION();

    if (birthday->wYear == 0) {
        // Log.AddT(
        //     LOG_ERROR, L"invalid date time. [%s : %d/%d/%d]", m_characterName.c_str(), static_cast<int>(birthday->wYear),
        //     static_cast<int>(birthday->wMonth), static_cast<int>(birthday->wDay)
        // )
        // Log.Add(LOG_WARN,
        //     L"[character] Rejecting five-year anniversary gift date for character %u (%ls): %u-%u-%u has a zero year.",
        //     m_characterId, m_characterName.c_str(), static_cast<unsigned>(birthday->wYear), static_cast<unsigned>(birthday->wMonth), static_cast<unsigned>(birthday->wDay))
        // L2_LOG_WARN(Log,
        //     L"[character] Rejecting five-year anniversary gift date for character %u (%ls): %u-%u-%u has a zero year.",
        //     m_characterId, m_characterName.c_str(), static_cast<unsigned>(birthday->wYear), static_cast<unsigned>(birthday->wMonth), static_cast<unsigned>(birthday->wDay))
        L2_LOG_WARN(
            Log, u"[character] Rejecting five-year anniversary gift date for character %u (%ls): %u-%u-%u has a zero year.",
            m_characterId, m_characterName.c_str(), static_cast<unsigned>(birthday->wYear),
            static_cast<unsigned>(birthday->wMonth), static_cast<unsigned>(birthday->wDay)
        );
        return false;
    }

    std::array<wchar_t, 20> birthdayText{};
    std::swprintf(
        birthdayText.data(), birthdayText.size(), L"%d-%d-%d %d:%d:%d", static_cast<int>(birthday->wYear),
        static_cast<int>(birthday->wMonth), static_cast<int>(birthday->wDay), static_cast<int>(birthday->wHour),
        static_cast<int>(birthday->wMinute), static_cast<int>(birthday->wSecond)
    );

    const int eventId = 1;
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveBirthdayGift (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(birthdayText.data());
    connection.BindParameter(&eventId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR, L"save birthday gift record error [%s : %d/%d/%d]", m_characterName.c_str(),
        //     static_cast<int>(birthday->wYear), static_cast<int>(birthday->wMonth), static_cast<int>(birthday->wDay)
        // )
        // Log.Add(LOG_ERROR,
        //     L"[character] Cannot save five-year anniversary gift record for character %u (%ls), date %u-%u-%u.",
        //     m_characterId, m_characterName.c_str(), static_cast<unsigned>(birthday->wYear), static_cast<unsigned>(birthday->wMonth), static_cast<unsigned>(birthday->wDay))
        // L2_LOG_ERROR(Log,
        //     L"[character] Cannot save five-year anniversary gift record for character %u (%ls), date %u-%u-%u.",
        //     m_characterId, m_characterName.c_str(), static_cast<unsigned>(birthday->wYear), static_cast<unsigned>(birthday->wMonth), static_cast<unsigned>(birthday->wDay))
        L2_LOG_ERROR(
            Log, u"[character] Cannot save five-year anniversary gift record for character %u (%ls), date %u-%u-%u.",
            m_characterId, m_characterName.c_str(), static_cast<unsigned>(birthday->wYear),
            static_cast<unsigned>(birthday->wMonth), static_cast<unsigned>(birthday->wDay)
        );
        return false;
    }
    return true;
}

bool CUser::SaveDuelRecord(
    int individualWin, int individualLose, int individualDraw, int partyWin, int partyLose, int partyDraw
) {
    // Current Cached.exe 0x005b4bdc.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveDuelRecord (?, ?, ?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&individualWin);
    connection.BindParameter(&individualLose);
    connection.BindParameter(&individualDraw);
    connection.BindParameter(&partyWin);
    connection.BindParameter(&partyLose);
    connection.BindParameter(&partyDraw);
    const bool result = connection.ExecutePrepared(statement);
    if (!result) {
        // Log.Add(LOG_ERROR, L"duel record save error. [%s]", m_characterName.c_str())
        // Log.Add(LOG_ERROR,
        //     L"[character] Cannot save duel results for character %u (%ls).",
        //     m_characterId, m_characterName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[character] Cannot save duel results for character %u (%ls).",
        //     m_characterId, m_characterName.c_str())
        L2_LOG_ERROR(
            Log, u"[character] Cannot save duel results for character %u (%ls).", m_characterId, m_characterName.c_str()
        );
    }
    return result;
}

bool CUser::LoadDuelRecord(CSocket* socket) {
    // Current Cached.exe 0x005b4d48.
    L2_GUARD_FUNCTION();

    int individualWin = 0;
    int individualLose = 0;
    int individualDraw = 0;
    int partyWin = 0;
    int partyLose = 0;
    int partyDraw = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&individualWin);
    connection.Bind(&individualLose);
    connection.Bind(&individualDraw);
    connection.Bind(&partyWin);
    connection.Bind(&partyLose);
    connection.Bind(&partyDraw);

    constexpr const wchar_t* statement = L"{Call lin_LoadDuelRecord (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
        socket->Send(
            "chddddddd", 0xc6, 0x42, static_cast<int>(m_characterId), individualWin, individualLose, individualDraw,
            partyWin, partyLose, partyDraw
        );
    }
    return true;
}

void CUser::LoadMergedInfo() {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&m_previousServer);
    connection.Bind(m_nameOrigin.data(), 0x30);

    const int mergedObjectType = 0;
    constexpr const wchar_t* query = L"{Call lin_LoadMergedInfo (?, ?)}";
    connection.Prepare(query);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&mergedObjectType);
    if (connection.ExecutePrepared(query)) {
        connection.Fetch();
    }
}

void CUser::UpdateBossRecord(int bossId, int record) {
    // Current Cached.exe 0x005b0230 / User.cpp:6763.
    L2_GUARD_FUNCTION();

    std::optional<DBConn> connectionStorage;
    {
        L2_WRITE_GUARD(*this);
        auto& connection = connectionStorage.emplace(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_UpdateBossRecord (?,?,?)}";
        connection.Prepare(statement);
        connection.BindParameter(&m_characterId);
        connection.BindParameter(&bossId);
        connection.BindParameter(&record);
        if (!connection.ExecutePrepared(statement)) {
            // Log.Add(LOG_ERROR, "updating boss record failed.")
            // Log.Add(LOG_ERROR,
            //     "[character] Cannot save boss %d record %d for character %u.",
            //     bossId, record, m_characterId)
            L2_LOG_ERROR(Log,
                "[character] Cannot save boss %d record %d for character %u.",
                bossId, record, m_characterId);
        }
    }
}

void CUser::LoadBossRecord(CSocket* socket) {
    // Current Cached.exe 0x005b03c4 / User.cpp:6793.
    L2_GUARD_FUNCTION();
    if (!socket)
        return;

    int npcClassId = 0;
    int point = 0;
    int accumulatedPoint = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&npcClassId);
    connection.Bind(&point);
    connection.Bind(&accumulatedPoint);

    std::optional<CPacketHelperEx<20480>> recordsStorage;
    int count;
    {
        L2_READ_GUARD(*this);
        constexpr const wchar_t* statement = L"{Call lin_LoadBossRecord (?)}";
        connection.Prepare(statement);
        connection.BindParameter(&m_characterId);

        auto& records = recordsStorage.emplace();
        count = 0;
        if (connection.ExecutePrepared(statement)) {
            while (count < 500 && connection.Fetch()) {
                if (records.Assemble("ddd", npcClassId, point, accumulatedPoint) > 0) {
                    ++count;
                }
            }
        }
    }

    if (count > 0) {
        auto& records = *recordsStorage;
        socket->Send(
            "chdddb", 0xc6, 0x23, static_cast<int>(m_characterId), GetBossRecordDB().GetRound(), count, records.GetLength(),
            records.GetPacket()
        );
    }
}

void CUser::LoadBotReport() {
    // Current Cached.exe 0x005b50c0.
    L2_GUARD_FUNCTION();
    if (m_botReportPoint.load() != -1)
        return;

    int reportPoint = 0;
    int confirmTimes = 0;
    int confirmSeconds = 0;
    int delaySeconds = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&reportPoint);
    connection.Bind(&confirmTimes);
    connection.Bind(&confirmSeconds);
    connection.Bind(&delaySeconds);

    constexpr const wchar_t* statement = L"{Call lin_LoadBotReport (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        if (reportPoint < 0) {
            // Log.AddT(LOG_ERROR, L"Invalid bot report point for user %s (%d)", m_characterName.c_str(), reportPoint)
            // Log.Add(LOG_WARN,
            //     L"[bot reports] Loaded negative report points %d for character %u (%ls). Using zero in memory.",
            //     reportPoint, m_characterId, m_characterName.c_str())
            // L2_LOG_WARN(Log,
            //     L"[bot reports] Loaded negative report points %d for character %u (%ls). Using zero in memory.",
            //     reportPoint, m_characterId, m_characterName.c_str())
            L2_LOG_WARN(
                Log, u"[bot reports] Loaded negative report points %d for character %u (%ls); using zero in memory.",
                reportPoint, m_characterId, m_characterName.c_str()
            );
            reportPoint = 0;
        }
        m_botReportPoint.store(reportPoint);
        m_captchaConfirmTimes.store(confirmTimes);
        m_captchaConfirmSec.store(confirmSeconds);
        m_captchaDelaySec.store(delaySeconds);
    }
}

void CUser::ResetBotReport() {
    // Current Cached.exe 0x005b52ac.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_ResetBotReport (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(LOG_ERROR, L"[%s][%d] Fail to reset bot report. char[%d]", L"User.cpp", 0x21bd, m_characterId)
        // Log.Add(LOG_ERROR,
        //     "[bot reports] Cannot reset report points for character %u. Cached points retained.",
        //     m_characterId)
        L2_LOG_ERROR(Log,
            "[bot reports] Cannot reset report points for character %u; cached points retained.",
            m_characterId);
        return;
    }
    m_botReportPoint.store(0);
}

void CUser::SetBotReport(int reportPoint) {
    // Current Cached.exe 0x005b5404 holds CUser::m_dataLock across the
    // clamp, time acquisition and complete SQL operation.
    L2_GUARD_FUNCTION();
    std::int32_t now;
    long reportedDate;
    std::optional<DBConn> connectionStorage;
    {
        L2_WRITE_GUARD(m_dataLock);
        if (reportPoint < 0)
            reportPoint = 0;
        now = runtime::crt::Time32Now();
        // Current DBConn helper 0x0043238c binds _time32_t through the target's
        // SQL_C_SLONG/SQL_DOUBLE path.
        reportedDate = static_cast<long>(now);
        auto& connection = connectionStorage.emplace(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_SetBotReport (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&m_characterId);
        connection.BindParameter(&reportPoint);
        connection.BindParameter(&reportedDate);
        if (!connection.ExecutePrepared(statement)) {
            // Log.AddT(LOG_ERROR, L"[%s][%d] Fail to set bot report. char[%d]", L"User.cpp", 0x21d8, m_characterId)
            // Log.Add(LOG_ERROR,
            //     "[bot reports] Cannot set %d report points for character %u. Cached points retained.",
            //     reportPoint, m_characterId)
            L2_LOG_ERROR(Log,
                "[bot reports] Cannot set %d report points for character %u; cached points retained.",
                reportPoint, m_characterId);
        } else {
            m_botReportPoint.store(reportPoint);
        }
    }
}

void CUser::SaveCaptchaConfirm(int confirmTimes, int confirmSeconds, int delaySeconds) {
    // Current Cached.exe 0x005b55d8.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveBotReportConfirm (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&confirmTimes);
    connection.BindParameter(&confirmSeconds);
    connection.BindParameter(&delaySeconds);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR, L"[%s][%d] Fail to save captcha confirm data. char[%d]", L"User.cpp", 0x2228, m_characterId
        // )
        // Log.Add(LOG_ERROR,
        //     "[bot reports] Cannot save captcha confirmation for character %u: confirmations %d, confirmation time %d seconds, delay %d seconds.",
        //     m_characterId, confirmTimes, confirmSeconds, delaySeconds)
        L2_LOG_ERROR(Log,
            "[bot reports] Cannot save captcha confirmation for character %u: confirmations %d, confirmation time %d seconds, delay %d seconds.",
            m_characterId, confirmTimes, confirmSeconds, delaySeconds);
        return;
    }
    m_captchaConfirmTimes.store(confirmTimes);
    m_captchaConfirmSec.store(confirmSeconds);
    m_captchaDelaySec.store(delaySeconds);
}

int CUser::LoadPCCafePoint() {
    // Current Cached.exe 0x005b293c. Unlike per-character user points this
    // value belongs to the account, so the target does not take CUser's
    // write lock around the query.
    L2_GUARD_FUNCTION();

    int point = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&point);

    constexpr const wchar_t* query = L"{Call lin_LoadAccountPCCafePoint (?)}";
    connection.Prepare(query);
    connection.BindParameter(&m_accountId);
    if (connection.ExecutePrepared(query)) {
        connection.Fetch();
    }
    return point;
}

bool CUser::SavePCCafePoint(int point) {
    // Current Cached.exe 0x005b2a94.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* query = L"{Call lin_SaveAccountPCCafePoint (?, ?)}";
    connection.Prepare(query);
    connection.BindParameter(&m_accountId);
    connection.BindParameter(&point);
    const bool saved = connection.ExecutePrepared(query);
    if (!saved) {
        // Log.Add(LOG_ERROR, "pccafepoint save error. [%ls]", m_accountName.c_str())
        // Log.Add(LOG_ERROR,
        //     L"[PC cafe] Cannot save %d points for account %d (%ls).",
        //     point, m_accountId, m_accountName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[PC cafe] Cannot save %d points for account %d (%ls).",
        //     point, m_accountId, m_accountName.c_str())
        L2_LOG_ERROR(
            Log, u"[PC cafe] Cannot save %d points for account %d (%ls).", point, m_accountId, m_accountName.c_str()
        );
    }
    return saved;
}

bool CUser::SaveHomePCCafeCount(int point) {
    // Current Cached.exe 0x005b2c0c.
    L2_GUARD_FUNCTION();

    const long now = static_cast<long>(runtime::crt::Time32Now());
    const int savedDay = GetYYYYMMDDFromTime(now);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveAccountHomePCCafePoint (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_accountId);
    connection.BindParameter(&point);
    connection.BindParameter(&savedDay);
    const bool saved = connection.ExecutePrepared(statement);
    if (!saved) {
        // Log.Add(LOG_ERROR, "HomePCCafeCount save error. [%ls]", m_accountName.c_str())
        // Log.Add(LOG_ERROR,
        //     L"[PC cafe] Cannot save home count %d for account %d (%ls), date %d.",
        //     point, m_accountId, m_accountName.c_str(), savedDay)
        // L2_LOG_ERROR(Log,
        //     L"[PC cafe] Cannot save home count %d for account %d (%ls), date %d.",
        //     point, m_accountId, m_accountName.c_str(), savedDay)
        L2_LOG_ERROR(
            Log, u"[PC cafe] Cannot save home count %d for account %d (%ls), date %d.", point, m_accountId,
            m_accountName.c_str(), savedDay
        );
    }
    return saved;
}

bool CUser::ResetHomePCCafeCount() {
    // Current Cached.exe 0x005b2da8.
    L2_GUARD_FUNCTION();

    const int point = 0;
    const long now = static_cast<long>(runtime::crt::Time32Now());
    const int savedDay = GetYYYYMMDDFromTime(now);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveAccountHomePCCafePoint (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_accountId);
    connection.BindParameter(&point);
    connection.BindParameter(&savedDay);
    const bool saved = connection.ExecutePrepared(statement);
    if (!saved) {
        // Log.Add(LOG_ERROR, "pchomecafepoint reset error. [%ls]", m_accountName.c_str())
        // Log.Add(LOG_ERROR,
        //     L"[PC cafe] Cannot reset home count for account %d (%ls), date %d.",
        //     m_accountId, m_accountName.c_str(), savedDay)
        // L2_LOG_ERROR(Log,
        //     L"[PC cafe] Cannot reset home count for account %d (%ls), date %d.",
        //     m_accountId, m_accountName.c_str(), savedDay)
        L2_LOG_ERROR(
            Log, u"[PC cafe] Cannot reset home count for account %d (%ls), date %d.", m_accountId, m_accountName.c_str(),
            savedDay
        );
    }
    return saved;
}

int CUser::LoadHomePCCafeCount() {
    // Current Cached.exe 0x005bd2d0.
    L2_GUARD_FUNCTION();

    int point = 0;
    int savedDay = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&point);
    connection.Bind(&savedDay);
    constexpr const wchar_t* statement = L"{Call lin_LoadAccountHomePCCafePoint (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_accountId);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }

    const long now = static_cast<long>(runtime::crt::Time32Now());
    if (savedDay < GetYYYYMMDDFromTime(now)) {
        ResetHomePCCafeCount();
        return -1;
    }
    return point;
}

void CUser::SaveBotReport(CSocket* socket) {
    // Current Cached.exe 0x005bd464.
    L2_GUARD_FUNCTION();
    LoadBotReport();

    const std::int32_t now = runtime::crt::Time32Now();
    const long reportedDate = static_cast<long>(now);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveBotReport (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&reportedDate);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(LOG_ERROR, L"[%s][%d] Fail to save bot report. char[%d]", L"User.cpp", 0x2158, m_characterId)
        // Log.Add(LOG_ERROR,
        //     "[bot reports] Cannot record a new report for character %u. Cached points were not incremented.",
        //     m_characterId)
        L2_LOG_ERROR(Log,
            "[bot reports] Cannot record a new report for character %u; cached points were not incremented.",
            m_characterId);
        return;
    }

    m_botReportPoint.fetch_add(1);
    socket->Send("chdd", 0xc6, 0x62, static_cast<int>(m_characterId), m_botReportPoint.load());
}

int CUser::GetBotReport() {
    // Current Cached.exe 0x005bd618.
    L2_GUARD_FUNCTION();
    LoadBotReport();
    return m_botReportPoint.load();
}

int CUser::LoadBotReportTime() {
    // Current Cached.exe 0x005bd6c0.
    L2_GUARD_FUNCTION();

    int reportedDate = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.BindNullable(&reportedDate, nullptr);
    constexpr const wchar_t* statement = L"{Call lin_LoadBotReportTime (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }
    if (reportedDate < 1) {
        SetBotReport(m_botReportPoint.load());
        reportedDate = runtime::crt::Time32Now();
    }
    return reportedDate;
}

void CUser::DecayBotReport(CSocket* socket) {
    // Current Cached.exe 0x005bd838.
    L2_GUARD_FUNCTION();
    LoadBotReport();
    const int savedTime = LoadBotReportTime();
    const int now = runtime::crt::Time32Now();
    if (now < savedTime) {
        // Log.AddT(
        //     LOG_ERROR, L"[%s][%d] saved of future. something wrong. char[%d]", L"User.cpp", 0x21f1, m_characterId
        // )
        // Log.Add(LOG_WARN,
        //     "[bot reports] Cannot decay points for character %u: stored report time %d is after current time %d (Unix seconds).",
        //     m_characterId, savedTime, now)
        L2_LOG_WARN(Log,
            "[bot reports] Cannot decay points for character %u: stored report time %d is after current time %d (Unix seconds).",
            m_characterId, savedTime, now);
        return;
    }

    const int elapsed = runtime::SubtractInt32Wrapping(now, savedTime);
    const int elapsedDays = elapsed / 0x15180;
    int decayPoint = runtime::MultiplyInt32Wrapping(elapsedDays, 5);
    const int reportPoint = m_botReportPoint.load();
    if (reportPoint <= decayPoint) {
        decayPoint = reportPoint;
    }
    if (decayPoint <= 0)
        return;

    const int newReportedDate = runtime::AddInt32Wrapping(runtime::MultiplyInt32Wrapping(elapsedDays, 0x15180), savedTime);
    if (now < newReportedDate) {
        // Log.AddT(
        //     LOG_ERROR, L"[%s][%d] save to future. something wrong. char[%d]", L"User.cpp", 0x2205, m_characterId
        // )
        // Log.Add(LOG_WARN,
        //     "[bot reports] Cannot decay points for character %u: calculated report time %d is after current time %d (Unix seconds).",
        //     m_characterId, newReportedDate, now)
        L2_LOG_WARN(Log,
            "[bot reports] Cannot decay points for character %u: calculated report time %d is after current time %d (Unix seconds).",
            m_characterId, newReportedDate, now);
        return;
    }

    const long databaseReportedDate = static_cast<long>(newReportedDate);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DecayBotReport (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&decayPoint);
    connection.BindParameter(&databaseReportedDate);
    if (!connection.ExecutePrepared(statement)) {
        // Log.AddT(
        //     LOG_ERROR, L"[%s][%d] Fail to decay bot report. char[%d]/point[%d]", L"User.cpp", 0x220c, m_characterId,
        //     decayPoint
        // )
        // Log.Add(LOG_ERROR,
        //     "[bot reports] Cannot persist decay of %d report points for character %u. Cached points retained.",
        //     decayPoint, m_characterId)
        L2_LOG_ERROR(Log,
            "[bot reports] Cannot persist decay of %d report points for character %u; cached points retained.",
            decayPoint, m_characterId);
        return;
    }

    // The target performs a load/subtract followed by locked
    // exchange, instead of a fetch-sub operation.
    const int remainingReport = runtime::SubtractInt32Wrapping(m_botReportPoint.load(), decayPoint);
    m_botReportPoint.exchange(remainingReport);
    socket->Send("chdd", 0xc6, 0x62, static_cast<int>(m_characterId), m_botReportPoint.load());
    WriteBotReportDecayAudit(*this, m_botReportPoint.load(), decayPoint);
}

void CUser::SaveAbilitySkill(int subjobId, int potentialType, int skillId, int skillLevel) {
    // Current Cached.exe 0x005b2194.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetAcquirePotentialSkill (?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&subjobId);
    connection.BindParameter(&potentialType);
    connection.BindParameter(&skillId);
    connection.BindParameter(&skillLevel);
    connection.ExecutePrepared(statement);
}

void CUser::SaveAbilityPoint(int subjobId, int typeOnePoint, int typeTwoPoint, int typeThreePoint) {
    // Current Cached.exe 0x005b22b0.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetPotentialPoint (?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&subjobId);
    connection.BindParameter(&typeOnePoint);
    connection.BindParameter(&typeTwoPoint);
    connection.BindParameter(&typeThreePoint);
    connection.ExecutePrepared(statement);
}

int CUser::LoadUserPoint(int pointType) {
    // Current Cached.exe 0x005b3104 / User.cpp:7719-7727.
    L2_GUARD_FUNCTION();

    int point = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&point);

    {
        L2_WRITE_GUARD(*this);
        constexpr const wchar_t* query = L"{Call lin_LoadUserPoint (?, ?)}";
        connection.Prepare(query);
        connection.BindParameter(&m_characterId);
        connection.BindParameter(&pointType);
        if (connection.ExecutePrepared(query)) {
            connection.Fetch();
        }
    }
    return point;
}

bool CUser::SaveUserPoint(int pointType, int point) {
    // Current Cached.exe 0x005b2f40 / User.cpp:7695-7703.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    bool saved;
    {
        L2_WRITE_GUARD(*this);
        constexpr const wchar_t* query = L"{Call lin_SaveUserPoint (?, ?, ?)}";
        connection.Prepare(query);
        connection.BindParameter(&m_characterId);
        connection.BindParameter(&pointType);
        connection.BindParameter(&point);
        saved = connection.ExecutePrepared(query);
        if (!saved) {
            // Log.Add(
            //     LOG_ERROR, "User point save error. [%ls : %d (%d)]", m_characterName.c_str(), pointType, point
            // )
            // Log.Add(LOG_ERROR,
            //     L"[character] Cannot save point type %d, value %d, for character %u (%ls).",
            //     pointType, point, m_characterId, m_characterName.c_str())
            // L2_LOG_ERROR(Log,
            //     L"[character] Cannot save point type %d, value %d, for character %u (%ls).",
            //     pointType, point, m_characterId, m_characterName.c_str())
            L2_LOG_ERROR(
                Log, u"[character] Cannot save point type %d, value %d, for character %u (%ls).", pointType, point,
                m_characterId, m_characterName.c_str()
            );
        }
    }
    return saved;
}

bool CUser::SavePVPPointRestrain(int giveCount, int giveTime) {
    // Current Cached.exe 0x005b3770.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SavePVPPointRestrain(?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&giveCount);
    connection.BindParameter(&giveTime);
    const bool saved = connection.ExecutePrepared(statement);
    if (!saved) {
        // Log.Add(
        //     LOG_ERROR, "pvp point restrain save error. [%ls : %d (%d)]", m_characterName.c_str(), giveCount,
        //     giveTime
        // )
        // Log.Add(LOG_ERROR,
        //     L"[character] Cannot save PvP point restrictions for character %u (%ls): give count %d, give time %d.",
        //     m_characterId, m_characterName.c_str(), giveCount, giveTime)
        // L2_LOG_ERROR(Log,
        //     L"[character] Cannot save PvP point restrictions for character %u (%ls): give count %d, give time %d.",
        //     m_characterId, m_characterName.c_str(), giveCount, giveTime)
        L2_LOG_ERROR(
            Log, u"[character] Cannot save PvP point restrictions for character %u (%ls): give count %d, give time %d.",
            m_characterId, m_characterName.c_str(), giveCount, giveTime
        );
    }
    return saved;
}

bool CUser::LoadPVPPointRestrain(CSocket* socket) {
    // Current Cached.exe 0x005b3908. The shipping function reports success
    // even when SQL execution fails. Only the reply is conditional.
    L2_GUARD_FUNCTION();

    int giveCount = 0;
    int giveTime = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&giveCount);
    connection.Bind(&giveTime);
    constexpr const wchar_t* statement = L"{Call lin_LoadPVPPointRestrain(?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
        socket->Send("chddd", 0xc6, 0x58, m_characterId, giveCount, giveTime);
    }
    return true;
}

bool CUser::SaveLastColor(int colorIndex, int savedTime) {
    // Current Cached.exe 0x005b3aa8.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{call lin_SaveLastColor (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&colorIndex);
    connection.BindParameter(&savedTime);
    const bool saved = connection.ExecutePrepared(statement);
    if (!saved) {
        // Log.Add(
        //     LOG_ERROR, "last color save error. [%ls : %d (%d)]", m_characterName.c_str(), colorIndex, savedTime
        // )
        // Log.Add(LOG_ERROR,
        //     L"[character] Cannot save color %d for character %u (%ls), saved time %d.",
        //     colorIndex, m_characterId, m_characterName.c_str(), savedTime)
        // L2_LOG_ERROR(Log,
        //     L"[character] Cannot save color %d for character %u (%ls), saved time %d.",
        //     colorIndex, m_characterId, m_characterName.c_str(), savedTime)
        L2_LOG_ERROR(
            Log, u"[character] Cannot save color %d for character %u (%ls), saved time %d.", colorIndex, m_characterId,
            m_characterName.c_str(), savedTime
        );
    }
    return saved;
}

bool CUser::LoadLastColor(CSocket* socket) {
    // Current Cached.exe 0x005b3c40. As in LoadPVPPointRestrain, the bool
    // return is always true and SQL success controls only packet emission.
    L2_GUARD_FUNCTION();

    int colorIndex = 0;
    int savedTime = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&colorIndex);
    connection.Bind(&savedTime);
    constexpr const wchar_t* statement = L"{call lin_LoadLastColor (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
        socket->Send("chddd", 0xc6, 0x59, m_characterId, colorIndex, savedTime);
    }
    return true;
}

int CUser::LoadTimeToGiveKarmaPoint() {
    L2_GUARD_FUNCTION();

    int result = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);

    constexpr const wchar_t* query = L"{Call lin_LoadTimeToGiveKarmaPoint (?)}";
    connection.Prepare(query);
    connection.BindParameter(&m_characterId);
    if (connection.ExecutePrepared(query) && connection.Fetch()) {
        return result;
    }
    return 0;
}

void CUser::UpdateTimeToGiveKarmaPoint(int timeToGiveKarmaPoint) {
    // Current Cached.exe 0x005b623c.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateTimeToGiveKarmaPoint (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&timeToGiveKarmaPoint);
    connection.ExecutePrepared(statement);
}

bool CUser::ResetInzoneOnDB(int inzoneId) {
    // Current Cached.exe 0x005b6370.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_ResetInZoneRestriction (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&inzoneId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] Fail to reset inzone restriction. "
        //     "char[%u] inzoneid[%d]",
        //     "User.cpp", 0x2460, m_characterId, inzoneId
        // )
        // Log.Add(LOG_ERROR,
        //     "[instance] Cannot reset restrictions for character %u, instance %d.",
        //     m_characterId, inzoneId)
        L2_LOG_ERROR(Log,
            "[instance] Cannot reset restrictions for character %u, instance %d.",
            m_characterId, inzoneId);
        return false;
    }

    // The shipping instructions compare both adjacent fields with zero and
    // write the same eight zero bytes only when both comparisons succeed.
    if (m_loadedInfo.associatedInzone == 0 && m_loadedInfo.associatedInzoneUid == 0) {
        m_loadedInfo.associatedInzone = 0;
        m_loadedInfo.associatedInzoneUid = 0;
    }
    return true;
}

int CUser::LoadWorldChatPoint(int maximumCount) {
    // Current Cached.exe 0x005b6da0. The local 06:30 calculation is kept
    // even though its result has no observable use in the shipping body.
    L2_GUARD_FUNCTION();

    const std::time_t now = std::time(nullptr);
    std::tm localNow{};
#if defined(_WIN32)
    ::localtime_s(&localNow, &now);
#else
    ::localtime_r(&now, &localNow);
#endif
    localNow.tm_hour = 6;
    localNow.tm_min = 30;
    localNow.tm_sec = 0;
    std::mktime(&localNow);

    int point = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&point);
    constexpr const wchar_t* statement = L"{Call lin_LoadWorldChat(?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&maximumCount);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        return point;
    }

    // Log.Add(LOG_ERROR, "qsLoadWorldChatPoint error")
    // Log.Add(LOG_ERROR,
    //     "[world chat] Cannot load points for character %u (maximum %d): query failed or returned no row. Returning -1.",
    //     m_characterId, maximumCount)
    L2_LOG_ERROR(Log,
        "[world chat] Cannot load points for character %u (maximum %d): query failed or returned no row; returning -1.",
        m_characterId, maximumCount);
    return -1;
}

bool CUser::UpdateWorldChatPoint(int point) {
    // Current Cached.exe 0x005b6f6c.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_ModifyWorldChat(?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&point);
    const bool updated = connection.ExecutePrepared(statement);
    if (!updated) {
        // Log.Add(LOG_ERROR, "qsModifyWorldChatPoint error")
        // Log.Add(LOG_ERROR,
        //     "[world chat] Cannot save %d points for character %u.",
        //     point, m_characterId)
        L2_LOG_ERROR(Log,
            "[world chat] Cannot save %d points for character %u.",
            point, m_characterId);
    }
    return updated;
}

void CUser::IncWorldChatPoint(int increment) {
    // Current Cached.exe 0x005b70e0.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_IncWorldChat(?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&increment);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "qsIncWorldChatPoint error")
        // Log.Add(LOG_ERROR,
        //     "[world chat] Cannot increment points by %d for character %u.",
        //     increment, m_characterId)
        L2_LOG_ERROR(Log,
            "[world chat] Cannot increment points by %d for character %u.",
            increment, m_characterId);
    }
}

bool CUser::LoadMatchingPoint() {
    L2_GUARD_FUNCTION();

    long long pledgeGamePoint = 0;
    long long olympiadPoint = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&pledgeGamePoint);
    connection.Bind(&olympiadPoint);

    constexpr const wchar_t* query = L"{Call lin_LoadMatchingPoint (?)}";
    connection.Prepare(query);
    connection.BindParameter(&m_characterId);
    if (!connection.ExecutePrepared(query) || !connection.Fetch()) {
        return false;
    }

    m_pledgeGamePoint = pledgeGamePoint;
    m_olympiadPoint = olympiadPoint;
    m_matchingPointChanged = false;
    return true;
}

bool CUser::SetMatchingPoint(MatchingPoint::MATCHINGPOINT_TYPE type, std::int64_t point, std::int64_t& previousPoint) {
    // Current Cached.exe 0x0059d798. The unsigned range test deliberately
    // rejects both negative enum values and MPT_MAX.
    L2_GUARD_FUNCTION();
    if (static_cast<std::uint32_t>(type) >= static_cast<std::uint32_t>(MatchingPoint::MPT_MAX)) {
        // Log.Add(LOG_ERROR, "[User.cpp][%d] %s invalid argument nType", 0x259a, "CUser::SetMatchingPoint")
        // Log.Add(LOG_WARN,
        //     "[character] Rejecting matching-point type %d for character %u: outside the supported range.",
        //     static_cast<int>(type), m_characterId)
        L2_LOG_WARN(Log,
            "[character] Rejecting matching-point type %d for character %u: outside the supported range.",
            static_cast<int>(type), m_characterId);
        return false;
    }

    std::int64_t& stored = type == MatchingPoint::MPT_PLEDGEGAME ? m_pledgeGamePoint : m_olympiadPoint;
    if (stored == point)
        return false;
    previousPoint = stored;
    stored = point;
    return true;
}

bool CUser::SaveMatchingPoint() {
    // Current Cached.exe 0x005b73cc ignores the database result and always
    // returns true after issuing the exact three-parameter write.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveMatchingPoint (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&m_pledgeGamePoint);
    connection.BindParameter(&m_olympiadPoint);
    connection.ExecutePrepared(statement);
    return true;
}

std::int64_t CUser::GetMatchingPoint(MatchingPoint::MATCHINGPOINT_TYPE type) {
    // Current Cached.exe 0x005bdbc4 reloads lazily when UserDb marks the
    // two-value state dirty. A failed reload leaves and returns
    // the previous in-memory value.
    L2_GUARD_FUNCTION();
    if (static_cast<std::uint32_t>(type) >= static_cast<std::uint32_t>(MatchingPoint::MPT_MAX)) {
        return 0;
    }
    if (m_matchingPointChanged) {
        LoadMatchingPoint();
    }
    return type == MatchingPoint::MPT_PLEDGEGAME ? m_pledgeGamePoint : m_olympiadPoint;
}

bool CUser::LoadHennaInfo() {
    L2_GUARD_FUNCTION();

    int henna1 = 0;
    int henna2 = 0;
    int henna3 = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&henna1);
    connection.Bind(&henna2);
    connection.Bind(&henna3);

    constexpr const wchar_t* query = L"{Call lin_LoadHenna (?)}";
    connection.Prepare(query);
    connection.BindParameter(&m_characterId);
    if (connection.ExecutePrepared(query) && connection.Fetch()) {
        m_henna = {henna1, henna2, henna3};
    }

    // Current 0x005aeda0 reports success even when Execute or Fetch fails.
    return true;
}

bool CUser::SaveHennaInfo() {
    // Current Cached.exe 0x005aef3c. The target reports
    // success of the database Execute result.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveHenna (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&m_henna[0]);
    connection.BindParameter(&m_henna[1]);
    connection.BindParameter(&m_henna[2]);
    connection.ExecutePrepared(statement);
    return true;
}

bool CUser::AddHenna(int hennaId) {
    // Current Cached.exe 0x005af050 fills the first empty ordinary slot.
    L2_GUARD_FUNCTION();

    for (int& equippedHenna : m_henna) {
        if (equippedHenna == 0) {
            equippedHenna = hennaId;
            SaveHennaInfo();
            return true;
        }
    }
    return false;
}

bool CUser::AddSubJobHenna(int hennaId, int subjobId) {
    // Current Cached.exe 0x005af12c. The target binds character ID,
    // subjob ID, then Henna ID and returns ExecutePrepared's result.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_AddSubJobHenna (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&subjobId);
    connection.BindParameter(&hennaId);
    return connection.ExecutePrepared(statement);
}

bool CUser::DelHenna(int hennaId) {
    // Current Cached.exe 0x005af284 clears the first matching ordinary slot.
    L2_GUARD_FUNCTION();

    for (int& equippedHenna : m_henna) {
        if (equippedHenna == hennaId) {
            equippedHenna = 0;
            SaveHennaInfo();
            return true;
        }
    }
    return false;
}

bool CUser::DelSubJobHenna(int hennaId, int subjobId) {
    // Current Cached.exe 0x005af360. Its bind order is the same as the add
    // owner and the SQL execution result is returned unchanged.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DelSubJobHenna (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&subjobId);
    connection.BindParameter(&hennaId);
    return connection.ExecutePrepared(statement);
}

bool CUser::AddHenna(int hennaId, int subjobId) {
    // Current Cached.exe 0x005bcfc4. The wrapper leaves the selected
    // callee's AL result intact across its epilogue.
    L2_GUARD_FUNCTION();
    if (m_subjobId == subjobId) {
        return AddHenna(hennaId);
    }
    return AddSubJobHenna(hennaId, subjobId);
}

bool CUser::DelHenna(int hennaId, int subjobId) {
    // Current Cached.exe 0x005bd08c. As in the add wrapper, the active
    // subjob mutates the resident three-slot carrier. Another subjob goes
    // directly through its SQL owner.
    L2_GUARD_FUNCTION();
    if (m_subjobId == subjobId) {
        return DelHenna(hennaId);
    }
    return DelSubJobHenna(hennaId, subjobId);
}

bool CUser::SaveHennaPremiumInfo(int startTime, int period) {
    // Current Cached.exe 0x005af4b8. Unlike SaveHennaInfo, this method
    // returns the actual ExecutePrepared result.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveHennaPremium (?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&m_hennaPremiumId);
    connection.BindParameter(&startTime);
    connection.BindParameter(&period);
    connection.BindParameter(&m_subjobId);
    return connection.ExecutePrepared(statement);
}

bool CUser::AddHennaPremium(int hennaId, int startTime, int period) {
    // Current Cached.exe 0x005bd154 leaves SaveHennaPremiumInfo's AL result
    // intact across its epilogue, so a persistence failure is observable by
    // CWareHouse::EquipHenna.
    L2_GUARD_FUNCTION();
    m_hennaPremiumId = hennaId;
    return SaveHennaPremiumInfo(startTime, period);
}

bool CUser::DelHennaPremium(int hennaId) {
    // Return true when the slot matches, even if saving the change fails.
    L2_GUARD_FUNCTION();
    if (m_hennaPremiumId != hennaId)
        return false;

    m_hennaPremiumId = 0;
    SaveHennaPremiumInfo(0, 0);
    return true;
}

void CUser::DeleteUserMacro(int macroId) {
    // Current Cached.exe 0x005af5f4. This owner has no CUser
    // lock and ignores the database execution result.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeleteMacro (? )}";
    connection.Prepare(statement);
    connection.BindParameter(&macroId);
    connection.ExecutePrepared(statement);
}

void CUser::AddUserMacro(
    int characterId, wchar_t* macroName, wchar_t* macroTooltip, wchar_t* macroIconName, int macroIconType, int* macroId
) {
    // Current Cached.exe 0x005af70c, exact PDB surface
    // CUser::AddUserMacro(int,wchar_t *,wchar_t *,wchar_t *,int,int *).
    L2_GUARD_FUNCTION();

    std::optional<DBConn> connectionStorage;
    {
        L2_WRITE_GUARD(*this);
        auto& connection = connectionStorage.emplace(runtime::process::CurrentDatabaseId());
        connection.Bind(macroId);
        constexpr const wchar_t* statement = L"{Call lin_CreateMacro (?, ?, ?, ?, ? )}";
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.BindParameter(macroName);
        connection.BindParameter(macroTooltip);
        connection.BindParameter(macroIconName);
        connection.BindParameter(&macroIconType);
        connection.ExecutePrepared(statement);
        connection.Fetch();
    }
}

void CUser::AddUserMacroInfo(int macroId, int macroOrder, int macroInt1, int macroInt2, int macroInt3, wchar_t* macroText) {
    // Current Cached.exe 0x005af874, exact PDB surface
    // CUser::AddUserMacroInfo(int,int,int,int,int,wchar_t *).
    L2_GUARD_FUNCTION();

    std::optional<DBConn> connectionStorage;
    {
        L2_WRITE_GUARD(*this);
        auto& connection = connectionStorage.emplace(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_CreateMacroInfo (?, ?, ?, ?, ?, ?  )}";
        connection.Prepare(statement);
        connection.BindParameter(&macroId);
        connection.BindParameter(&macroOrder);
        connection.BindParameter(&macroInt1);
        connection.BindParameter(&macroInt2);
        connection.BindParameter(&macroInt3);
        connection.BindParameter(macroText);
        connection.ExecutePrepared(statement);
    }
}

void CUser::SetSSQPoint(std::int64_t point) {
    L2_GUARD_FUNCTION();
    m_ssqInfo.point = point;
}

void CUser::InitializeSSQInfo() {
    // Current Cached.exe 0x005a04e0 initializes the resident block at
    // +0x24e8..+0x2538. In particular, roundNumber starts at -1. The
    // adjacent join-time field is zeroed but is not part of the selected-
    // character SSQ packet assembled by 0x005a05b4.
    L2_GUARD_FUNCTION();
    m_ssqInfo.roundNumber = -1;
    m_ssqInfoLoaded = false;
    m_ssqInfo.point = 0;
    m_ssqInfo.part = 0;
    m_ssqInfo.position = 0;
    m_ssqInfo.sealSelectionNumber = 0;
    m_ssqInfo.twilightItemCount.fill(0);
    m_ssqInfo.dawnItemCount.fill(0);
    m_ssqInfo.ticketBuyCount = 0;
    m_ssqInfo.joinTime = 0;
}

bool CUser::SetDepositedSSQItemCount(SSQPart::type part, SSQItem::type itemType, std::int64_t count) {
    L2_GUARD_FUNCTION();

    const std::size_t index = static_cast<std::size_t>(itemType);
    if (part == SSQPart::TWILIGHT) {
        m_ssqInfo.twilightItemCount.data()[index] = count;
        return true;
    }
    if (part == SSQPart::DAWN) {
        m_ssqInfo.dawnItemCount.data()[index] = count;
        return true;
    }
    return false;
}

bool CUser::SaveSSQInfo() {
    L2_GUARD_FUNCTION();

    const unsigned int characterId = m_characterId;
    const int roundNumber = m_ssqInfo.roundNumber;
    // Current helper binds this int32 value as SQL_C_SLONG/SQL_DOUBLE.
    const long joinTime = static_cast<long>(m_ssqInfo.joinTime);
    const int part = m_ssqInfo.part;
    const int position = m_ssqInfo.position;
    const int sealSelectionNumber = m_ssqInfo.sealSelectionNumber;
    const long long point = static_cast<long long>(m_ssqInfo.point);
    const long long twilightA = static_cast<long long>(m_ssqInfo.twilightItemCount[0]);
    const long long twilightB = static_cast<long long>(m_ssqInfo.twilightItemCount[1]);
    const long long twilightC = static_cast<long long>(m_ssqInfo.twilightItemCount[2]);
    const long long dawnA = static_cast<long long>(m_ssqInfo.dawnItemCount[0]);
    const long long dawnB = static_cast<long long>(m_ssqInfo.dawnItemCount[1]);
    const long long dawnC = static_cast<long long>(m_ssqInfo.dawnItemCount[2]);
    const int ticketBuyCount = m_ssqInfo.ticketBuyCount;

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveSSQUserInfo (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
                                         L"?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&roundNumber);
    connection.BindParameter(&joinTime);
    connection.BindParameter(&part);
    connection.BindParameter(&position);
    connection.BindParameter(&sealSelectionNumber);
    connection.BindParameter(&point);
    connection.BindParameter(&twilightA);
    connection.BindParameter(&twilightB);
    connection.BindParameter(&twilightC);
    connection.BindParameter(&dawnA);
    connection.BindParameter(&dawnB);
    connection.BindParameter(&dawnC);
    connection.BindParameter(&ticketBuyCount);
    const bool saved = connection.ExecutePrepared(statement);
    if (!saved) {
        const std::string characterName = Utf16ToUtf8(m_characterName);
        // Log.Add(
        //     LOG_ERROR,
        //     "[User.cpp][%d]Cannot save ssq user info. "
        //     "char_id[%u], name[%s], Round[%d]",
        //     0x1cad, characterId, characterName.c_str(), roundNumber
        // )
        // Log.Add(LOG_ERROR,
        //     "[Seven Signs] Cannot save round %d for character %u (%s).",
        //     roundNumber, characterId, characterName.c_str())
        // Previous: L2_LOG_ERROR(Log,
        //     "[Seven Signs] Cannot save round %d for character %u (%s).",
        //     roundNumber, characterId, characterName.c_str())
        L2_LOG_ERROR(Log,
            "[seven signs] Cannot save round %d for character %u (%s).",
            roundNumber, characterId, characterName.c_str());
    }
    return saved;
}

bool CUser::LoadSSQInfo() {
    L2_GUARD_FUNCTION();

    int roundNumber = 0;
    int joinTime = 0;
    int part = 0;
    int position = 0;
    int sealSelectionNumber = 0;
    long long point = 0;
    std::array<long long, 3> twilightItemCount{};
    std::array<long long, 3> dawnItemCount{};
    int ticketBuyCount = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&roundNumber);
    connection.Bind(&joinTime);
    connection.Bind(&part);
    connection.Bind(&position);
    connection.Bind(&sealSelectionNumber);
    connection.Bind(&point);
    for (long long& count : twilightItemCount) {
        connection.Bind(&count);
    }
    for (long long& count : dawnItemCount) {
        connection.Bind(&count);
    }
    connection.Bind(&ticketBuyCount);

    const int characterId = static_cast<int>(m_characterId);
    constexpr const wchar_t* query = L"{Call lin_LoadSSQUserInfo (?)}";
    connection.Prepare(query);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(query) || !connection.Fetch()) {
        m_ssqInfoLoaded = false;
        return false;
    }

    m_ssqInfo.roundNumber = roundNumber;
    m_ssqInfo.joinTime = joinTime;
    m_ssqInfo.part = part;
    m_ssqInfo.position = position;
    m_ssqInfo.sealSelectionNumber = sealSelectionNumber;
    m_ssqInfo.point = point;
    for (std::size_t index = 0; index < twilightItemCount.size(); ++index) {
        m_ssqInfo.twilightItemCount[index] = twilightItemCount[index];
        m_ssqInfo.dawnItemCount[index] = dawnItemCount[index];
    }
    m_ssqInfo.ticketBuyCount = ticketBuyCount;
    m_ssqInfoLoaded = true;
    return true;
}

int CUser::AssembleSSQInfo(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    // Current Cached.exe 0x005a05b4 skips the adjacent persisted joinTime
    // member and emits exactly four leading dwords before the int64 fields.
    return ::Assemble(
        buffer, bufferSize - 100, "ddddQQQQQQQd", m_ssqInfo.roundNumber, m_ssqInfo.part, m_ssqInfo.position,
        m_ssqInfo.sealSelectionNumber, m_ssqInfo.point, m_ssqInfo.twilightItemCount[0], m_ssqInfo.twilightItemCount[1],
        m_ssqInfo.twilightItemCount[2], m_ssqInfo.dawnItemCount[0], m_ssqInfo.dawnItemCount[1], m_ssqInfo.dawnItemCount[2],
        m_ssqInfo.ticketBuyCount
    );
}

void CUser::LoadSociality() {
    L2_GUARD_FUNCTION();

    int sociality = 0;
    int usedSuffrageCount = 0;
    std::array<char16_t, 25> lastCheck{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&sociality);
    connection.Bind(&usedSuffrageCount);
    connection.Bind(lastCheck.data(), 0x32);

    constexpr const wchar_t* statement = L"{Call lin_LoadSociality (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        int year = 0;
        int month = 0;
        int day = 0;
        int hour = 0;
        int minute = 0;
        int second = 0;
        m_sociality = sociality;
        m_usedSuffrageCount = usedSuffrageCount;
        std::swscanf(Utf16ToWide(lastCheck.data()).c_str(), L"%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second);
        m_socialityLastCheck = {
            static_cast<std::uint16_t>(year),
            static_cast<std::uint16_t>(month),
            0,
            static_cast<std::uint16_t>(day),
            static_cast<std::uint16_t>(hour),
            static_cast<std::uint16_t>(minute),
            static_cast<std::uint16_t>(second),
            0,
        };
    } else {
        // Log.Add(LOG_ERROR, "sociality load error. [%ls]", m_characterName.c_str())
        // Log.Add(LOG_ERROR,
        //     L"[character] Cannot load sociality: query failed or returned no row for character %u (%ls).",
        //     m_characterId, m_characterName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[character] Cannot load sociality: query failed or returned no row for character %u (%ls).",
        //     m_characterId, m_characterName.c_str())
        L2_LOG_ERROR(
            Log, u"[character] Cannot load sociality: query failed or returned no row for character %u (%ls).",
            m_characterId, m_characterName.c_str()
        );
    }

    if (m_socialityLastCheck.wYear != 0)
        return;

    const runtime::SystemTime localTime = runtime::GetLocalTimeNow();
    m_socialityLastCheck = {
        localTime.wYear,
        localTime.wMonth,
        localTime.wDayOfWeek,
        localTime.wDay,
        localTime.wHour,
        localTime.wMinute,
        localTime.wSecond,
        localTime.wMilliseconds,
    };
    m_sociality = 0;
    m_usedSuffrageCount = 0;

    std::array<wchar_t, 20> resetTime{};
    std::swprintf(
        resetTime.data(), resetTime.size(), L"%d-%d-%d %d:%d:%d", static_cast<int>(m_socialityLastCheck.wYear),
        static_cast<int>(m_socialityLastCheck.wMonth), static_cast<int>(m_socialityLastCheck.wDay),
        static_cast<int>(m_socialityLastCheck.wHour), static_cast<int>(m_socialityLastCheck.wMinute),
        static_cast<int>(m_socialityLastCheck.wSecond)
    );

    DBConn resetConnection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* resetStatement = L"{Call lin_ResetSociality (?, ?, ?, ?)}";
    resetConnection.Prepare(resetStatement);
    resetConnection.BindParameter(&m_characterId);
    resetConnection.BindParameter(&m_sociality);
    resetConnection.BindParameter(&m_usedSuffrageCount);
    resetConnection.BindParameter(resetTime.data());
    resetConnection.ExecutePrepared(resetStatement);
}

void CUser::SetSociality(int sociality) {
    // Current Cached.exe 0x005aea14. The memory value is kept even when
    // persistence fails. The shipping method only emits the error log.
    L2_GUARD_FUNCTION();

    if (m_socialityLastCheck.wYear == 0) {
        LoadSociality();
    }
    m_sociality = sociality;

    DBConn connection(runtime::process::CurrentDatabaseId());
    const int characterId = static_cast<int>(m_characterId);
    constexpr const wchar_t* statement = L"{Call lin_updateSociality (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&sociality);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "sociality save error. [%ls]", m_characterName.c_str())
        // Log.Add(LOG_ERROR,
        //     L"[character] Cannot persist sociality %d for character %u (%ls). The value remains changed in memory.",
        //     sociality, m_characterId, m_characterName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[character] Cannot persist sociality %d for character %u (%ls). The value remains changed in memory.",
        //     sociality, m_characterId, m_characterName.c_str())
        L2_LOG_ERROR(
            Log, u"[character] Cannot persist sociality %d for character %u (%ls); the value remains changed in memory.",
            sociality, m_characterId, m_characterName.c_str()
        );
    }
}

bool CUser::SetSocialityWithLock(int sociality) {
    // Current Cached.exe 0x005bcf00. The wrapper has no failure branch of its
    // own and returns true after releasing the CUser write lock.
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(*this);
        SetSociality(sociality);
    }
    return true;
}

void CUser::SetSocialityAll(int sociality, int usedSuffrageCount, SYSTEMTIME* lastCheck) {
    // Current Cached.exe 0x005aeb80. The timestamp is copied before the
    // database call and the ExecutePrepared result is ignored.
    L2_GUARD_FUNCTION();

    if (m_socialityLastCheck.wYear == 0) {
        LoadSociality();
    }

    m_sociality = sociality;
    m_usedSuffrageCount = usedSuffrageCount;
    std::memcpy(&m_socialityLastCheck, lastCheck, sizeof(m_socialityLastCheck));

    std::array<wchar_t, 20> lastCheckText{};
    std::swprintf(
        lastCheckText.data(), lastCheckText.size(), L"%d-%d-%d %d:%d:%d", static_cast<int>(m_socialityLastCheck.wYear),
        static_cast<int>(m_socialityLastCheck.wMonth), static_cast<int>(m_socialityLastCheck.wDay),
        static_cast<int>(m_socialityLastCheck.wHour), static_cast<int>(m_socialityLastCheck.wMinute),
        static_cast<int>(m_socialityLastCheck.wSecond)
    );

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_ResetSociality (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&m_sociality);
    connection.BindParameter(&m_usedSuffrageCount);
    connection.BindParameter(lastCheckText.data());
    connection.ExecutePrepared(statement);
}

SYSTEMTIME* CUser::GetSocialityLastCheck() {
    L2_GUARD_FUNCTION();
    if (m_socialityLastCheck.wYear == 0)
        LoadSociality();
    return &m_socialityLastCheck;
}

int CUser::GetSociality() {
    L2_GUARD_FUNCTION();
    if (m_socialityLastCheck.wYear == 0)
        LoadSociality();
    return m_sociality;
}

int CUser::GetUsedSulffrageCount() {
    L2_GUARD_FUNCTION();
    if (m_socialityLastCheck.wYear == 0)
        LoadSociality();
    return m_usedSuffrageCount;
}

bool CUser::SendHonorsSkillList(CSocket* socket) {
    // Current Cached.exe 0x005a4858. The empty map produces no packet, and a
    // non-null socket is considered success even when the list is empty.
    L2_GUARD_FUNCTION();
    if (!socket)
        return false;

    CPacketHelperEx<20480> skills;
    int skillCount = 0;
    {
        L2_GUARD(m_honorsSkillLock);
        for (const auto& [skillId, skillLevel] : m_acquiredHonorsSkills) {
            skills.Assemble("dd", skillId, skillLevel);
            ++skillCount;
        }
    }

    if (skillCount > 0) {
        socket->Send("chddb", 0xc6, 0x108, m_characterId, skillCount, skills.GetLength(), skills.GetPacket());
    }
    return true;
}

int CUser::GetHonorsSkillLevel(int skillId) {
    // Current Cached.exe 0x005a4df0.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_honorsSkillLock);
    const auto skill = m_acquiredHonorsSkills.find(skillId);
    return skill == m_acquiredHonorsSkills.end() ? 0 : skill->second;
}

bool CUser::SendRecipeBookInfo(int userServerId, CSocket* socket) {
    // Current Cached.exe 0x005a4c4c. Entries are sent in the target
    // std::map key order and only successful four-byte assemblies contribute
    // to the advertised row count.
    L2_GUARD_FUNCTION();

    CPacketHelperEx<20480> recipes;
    int recipeCount = 0;

    {
        L2_WRITE_GUARD(*this);
        for (const auto& [recipeId, ownerId] : m_recipeBook) {
            if (recipes.Assemble("d", recipeId) > 0) {
                ++recipeCount;
            }
        }
    }

    socket->Send("cddb", 0xaa, userServerId, recipeCount, recipes.GetLength(), recipes.GetPacket());
    return true;
}

bool CUser::SaveRecipeBookInfo(int recipeId) {
    // Current Cached.exe 0x005ac1f0. Memory is updated before the SQL call.
    // a database failure does not roll the map insertion back.
    L2_GUARD_FUNCTION();

    bool result = true;
    {
        L2_WRITE_GUARD(*this);
        if (recipeId > 0) {
            if (m_recipeBook.find(recipeId) == m_recipeBook.end()) {
                m_recipeBook.emplace(recipeId, static_cast<int>(m_characterId));

                DBConn connection(runtime::process::CurrentDatabaseId());
                constexpr const wchar_t* statement = L"{Call lin_SaveRecipeInfo (?, ?)}";
                connection.Prepare(statement);
                connection.BindParameter(&m_characterId);
                connection.BindParameter(&recipeId);
                result = connection.ExecutePrepared(statement);
            } else {
                result = false;
                // Log.Add(
                //     LOG_ERROR, "[%s][%d] try to insert duplicated recipe info [%d][%d]", "User.cpp", 0x11e6,
                //     static_cast<int>(m_characterId), recipeId
                // )
                // Log.Add(LOG_WARN,
                //     "[recipe] Cannot add recipe %d for character %u: already present in the cached recipe book.",
                //     recipeId, m_characterId)
                L2_LOG_WARN(Log,
                    "[recipe] Cannot add recipe %d for character %u: already present in the cached recipe book.",
                    recipeId, m_characterId);
            }
        }
    }
    return result;
}

bool CUser::SaveRecipeBookInfo(int recipeId, int userServerId, CSocket* socket) {
    // Current wrapper 0x005bc2c8 echoes the recipe ID on success and -1 on
    // failure. The return value remains the core persistence result.
    L2_GUARD_FUNCTION();

    const bool result = SaveRecipeBookInfo(recipeId);
    socket->Send("cdd", 0xa8, userServerId, result ? recipeId : -1);
    return result;
}

bool CUser::DeleteRecipeBookInfo(int recipeId) {
    // Current Cached.exe 0x005ac41c. As in the target, removal precedes SQL
    // execution and is not rolled back when ExecutePrepared fails.
    L2_GUARD_FUNCTION();

    bool result = true;
    {
        L2_WRITE_GUARD(*this);
        if (recipeId > 0) {
            const auto recipe = m_recipeBook.find(recipeId);
            if (recipe == m_recipeBook.end()) {
                result = false;
                // Log.Add(
                //     LOG_ERROR, "[%s][%d] Cannot find recipe info. [%d][%d]", "User.cpp", 0x1212,
                //     static_cast<int>(m_characterId), recipeId
                // )
                // Log.Add(LOG_WARN,
                //     "[recipe] Cannot delete recipe %d for character %u: absent from the cached recipe book.",
                //     recipeId, m_characterId)
                L2_LOG_WARN(Log,
                    "[recipe] Cannot delete recipe %d for character %u: absent from the cached recipe book.",
                    recipeId, m_characterId);
            } else {
                m_recipeBook.erase(recipe);

                DBConn connection(runtime::process::CurrentDatabaseId());
                constexpr const wchar_t* statement = L"{Call lin_DelRecipeInfo (?, ?)}";
                connection.Prepare(statement);
                connection.BindParameter(&m_characterId);
                connection.BindParameter(&recipeId);
                result = connection.ExecutePrepared(statement);
            }
        }
    }
    return result;
}

bool CUser::DeleteRecipeBookInfo(int recipeId, int userServerId, CSocket* socket) {
    // Current wrapper 0x005bc3c8 has the same reply mapping as save, with
    // opcode 0xa9.
    L2_GUARD_FUNCTION();

    const bool result = DeleteRecipeBookInfo(recipeId);
    socket->Send("cdd", 0xa9, userServerId, result ? recipeId : -1);
    return result;
}

bool CUser::LoadRecipeInfo() {
    L2_GUARD_FUNCTION();

    int recipeId = 0;
    std::optional<DBConn> connectionStorage;
    {
        L2_WRITE_GUARD(m_dataLock);
        auto& connection = connectionStorage.emplace(runtime::process::CurrentDatabaseId());
        connection.Bind(&recipeId);

        constexpr const wchar_t* query = L"{Call lin_LoadRecipeInfo (?)}";
        connection.Prepare(query);
        connection.BindParameter(&m_characterId);
        if (connection.ExecutePrepared(query)) {
            m_recipeBook.clear();
            while (connection.Fetch()) {
                m_recipeBook.emplace(recipeId, static_cast<int>(m_characterId));
            }
        }
    }

    // Current 0x005ac630 returns true even when Execute fails.
    return true;
}

bool CUser::ChangeLocationExtern(const char16_t* characterName, int x, int y, int z, bool save [[maybe_unused]]) {
    // Current Cached.exe 0x005ac7ec. The final source parameter survives in
    // the guard signature but is not read anywhere in the current body.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&characterId);

    constexpr const wchar_t* statement = L"{Call lin_ChangeCharacterLocation (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(characterName);
    connection.BindParameter(&x);
    connection.BindParameter(&y);
    connection.BindParameter(&z);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch() || characterId <= 0) {
        return false;
    }

    SetPos(x, y, z);
    return true;
}

bool CUser::SetBuilderLevExtern(const char16_t* characterName, int builderLevel) {
    // Current Cached.exe 0x005ac944.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&characterId);

    constexpr const wchar_t* statement = L"{Call lin_SetBuilderCharacter (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(characterName);
    connection.BindParameter(&builderLevel);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch() || characterId <= 0) {
        return false;
    }

    {
        L2_WRITE_GUARD(m_dataLock);
        m_builder = builderLevel;
    }
    return true;
}

bool CUser::ChangeCharacternameExtern(const char16_t* characterName) {
    // Current Cached.exe 0x005acad8.
    L2_GUARD_FUNCTION();

    int result = 0;
    std::u16string oldCharacterNameLow = m_characterNameLow;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);

    constexpr const wchar_t* statement = L"{Call lin_ChangeCharacterName2 (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(oldCharacterNameLow.c_str());
    connection.BindParameter(characterName);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch() || result <= 0) {
        return false;
    }

    {
        L2_WRITE_GUARD(m_dataLock);
        m_characterNameLow = TargetLowercase(std::u16string(characterName));
        m_characterName = characterName;
    }

    g_userDb.RefreshMaps(oldCharacterNameLow.data(), m_characterNameLow.data());
    WriteCharacterRenameAudit(*this, characterName, oldCharacterNameLow);
    return true;
}

bool CUser::SetSkillLock(int nSkillId, int nSkillLock, bool bToSave) {
    L2_GUARD_FUNCTION();

    if (nSkillId <= 0)
        return false;

    L2_GUARD(m_skillLock);
    m_skillLocks.insert_or_assign(nSkillId, nSkillLock);

    if (bToSave) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* query = L"{Call lin_SetSkillLock (?, ?, ?, ?)}";
        connection.Prepare(query);
        connection.BindParameter(&m_characterId);
        connection.BindParameter(&m_subjobId);
        connection.BindParameter(&nSkillId);
        connection.BindParameter(&nSkillLock);
        connection.ExecutePrepared(query);
    }
    return true;
}

int CUser::GetAcquireSkillLevelSet(int nSkillId) {
    // Current Cached.exe 0x005a5260.
    L2_GUARD_FUNCTION();

    L2_GUARD(m_skillLock);
    const auto acquired = m_acquiredSkills.find(nSkillId);
    if (acquired == m_acquiredSkills.end())
        return 0;

    return static_cast<int>(
        (static_cast<std::uint32_t>(acquired->second.second) << 16) |
        (static_cast<std::uint32_t>(acquired->second.first) & 0xffffU)
    );
}

void CUser::_InsertOrUpdateLastUse(int nSubJobId, int nSkillId, int nEndTime, int delay) {
    L2_GUARD_FUNCTION();

    if (static_cast<unsigned int>(nSubJobId) >= m_skillReuseBySubjob.size()) {
        // Log.Add(LOG_ERROR, "[User.cpp][4075]Invalid Subjob Id, char dbid[%u]", m_characterId)
        // Log.Add(LOG_ERROR,
        //     "[skills] Cannot store skill cooldown for character %u: invalid subjob %d (valid range 0-4).",
        //     m_characterId, nSubJobId)
        L2_LOG_ERROR(Log,
            "[skills] Cannot store skill cooldown for character %u: invalid subjob %d (valid range 0-4).",
            m_characterId, nSubJobId);
        // Current Cached.exe 0x005a5846 reports the CacheD guard stack before
        // returning from the invalid-subjob branch.
        runtime::process::ReportCallStack(LOG_ERROR, false);
        return;
    }

    m_skillReuseBySubjob[nSubJobId].insert_or_assign(nSkillId, UserSkillState{nEndTime, delay});
}

bool CUser::_SetSkillLastUse(ReuseDelaySaveReason::Type saveReason, int nSubJobId, int nSkillId, int nEndTime, int delay) {
    // Current Cached.exe 0x005ab4b4.
    L2_GUARD_FUNCTION();

    if (nSkillId <= 0)
        return false;

    int effectiveSubjobId = nSubJobId;
    if (nSubJobId == -1 || nSubJobId == m_subjobId) {
        effectiveSubjobId = m_subjobId;
    } else if (nSubJobId != 4 && saveReason != ReuseDelaySaveReason::SR_DUMP) {
        // Log.Add(
        //     LOG_ERROR,
        //     "SubJobId (%d) is different from m_nSubJobId (%d), "
        //     "saveReason(%d)",
        //     nSubJobId, m_subjobId, static_cast<int>(saveReason)
        // )
        // Log.Add(LOG_ERROR,
        //     "[skills] Cannot save cooldown for character %u, skill %d: requested subjob %d differs from current subjob %d (save reason %d).",
        //     m_characterId, nSkillId, nSubJobId, m_subjobId, static_cast<int>(saveReason))
        L2_LOG_ERROR(Log,
            "[skills] Cannot save cooldown for character %u, skill %d: requested subjob %d differs from current subjob %d (save reason %d).",
            m_characterId, nSkillId, nSubJobId, m_subjobId, static_cast<int>(saveReason));
        runtime::process::ReportCallStack(LOG_ERROR, false);
        return false;
    }

    _InsertOrUpdateLastUse(effectiveSubjobId, nSkillId, nEndTime, delay);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetSkillCoolTime (?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    // The target persists the packet value, including -1, while using the
    // resolved current subjob only for its in-memory map.
    connection.BindParameter(&nSubJobId);
    connection.BindParameter(&nSkillId);
    connection.BindParameter(&nEndTime);
    connection.BindParameter(&delay);
    connection.ExecutePrepared(statement);
    return true;
}

void CUser::SetSkillsLastUse(
    const std::uint8_t* packet, ReuseDelaySaveReason::Type saveReason, int nSubJobId, int number, int nAllJobNumber
) {
    // Current Cached.exe 0x005ab648.
    L2_GUARD_FUNCTION();

    L2_GUARD(m_skillLock);

    int skillId = 0;
    int toEndTime = 0;
    int delay = 0;
    for (int index = 0; index < number; ++index) {
        packet = ::Disassemble(packet, "ddd", &skillId, &toEndTime, &delay);
        _SetSkillLastUse(saveReason, nSubJobId, skillId, toEndTime, delay);
    }
    for (int index = 0; index < nAllJobNumber; ++index) {
        packet = ::Disassemble(packet, "ddd", &skillId, &toEndTime, &delay);
        _SetSkillLastUse(saveReason, 4, skillId, toEndTime, delay);
    }
}

bool CUser::CheckMaxLevel(int nSubJobId, int level) {
    // Current Cached.exe 0x005b6518. The single fetched value is compared to
    // the requested level only after both ExecutePrepared and Fetch succeed.
    L2_GUARD_FUNCTION();

    int maximumLevel = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&maximumLevel);
    constexpr const wchar_t* statement = L"{Call lin_CheckCharMaxLevel (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&nSubJobId);
    return connection.ExecutePrepared(statement) && connection.Fetch() && maximumLevel < level;
}

bool CUser::UpdateMaxLevel(int nSubJobId, int level, bool forceUpdate) {
    // Current Cached.exe 0x005b669c and its exact four-parameter helper at
    // 0x004846c8.
    L2_GUARD_FUNCTION();

    const int forceUpdateValue = forceUpdate ? 1 : 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateCharMaxLevel (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&nSubJobId);
    connection.BindParameter(&level);
    connection.BindParameter(&forceUpdateValue);
    return connection.ExecutePrepared(statement);
}

bool CUser::_LoadAcquiredPotentialSkill(int nSubJobId) {
    L2_GUARD_FUNCTION();

    if (static_cast<unsigned int>(nSubJobId) >= m_skillReuseBySubjob.size()) {
        // Log.Add(LOG_ERROR, "nSubJobId %d in LoadAcquiredPotentialSkill is not valid", nSubJobId)
        // Log.Add(LOG_ERROR,
        //     "[skills] Cannot load potential skills for character %u: invalid subjob %d (valid range 0-4).",
        //     m_characterId, nSubJobId)
        L2_LOG_ERROR(Log,
            "[skills] Cannot load potential skills for character %u: invalid subjob %d (valid range 0-4).",
            m_characterId, nSubJobId);
        return false;
    }

    int nSkillId = 0;
    int nSkillLevel = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&nSkillId);
    connection.Bind(&nSkillLevel);

    constexpr const wchar_t* query = L"{Call lin_GetAcquirePotentialSkill (?, ?)}";
    connection.Prepare(query);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&nSubJobId);
    if (connection.ExecutePrepared(query)) {
        m_acquiredPotentialSkills.clear();
        while (connection.Fetch()) {
            const UserSkillState loaded{nSkillLevel, 0};

            const auto acquired = m_acquiredSkills.emplace(nSkillId, loaded);
            if (!acquired.second) {
                if (acquired.first->second.first < nSkillLevel) {
                    acquired.first->second.first = nSkillLevel;
                } else {
                    // The current target passes no values for its two `%d`
                    // conversions. Preserve the diagnostic text without
                    // reproducing undefined variadic reads.
                    // Noise: Potential-skill loading keeps an equal or higher acquired level. This merge is not an error.
                    // Log.Add(LOG_ERROR, "%s", "CUser::LoadAcquirePotentialSkill-2. %d < %d")
                }
            }

            const auto potential = m_acquiredPotentialSkills.emplace(nSkillId, loaded);
            if (!potential.second) {
                if (potential.first->second.first < nSkillLevel) {
                    potential.first->second.first = nSkillLevel;
                } else {
                    // Noise: Duplicate potential-skill rows keep the equal or higher level. Per-row merge chatter is unnecessary.
                    // Log.Add(LOG_ERROR, "%s", "CUser::LoadAcquirePotentialSkill-1. %d < %d")
                }
            }

            nSkillId = 0;
            nSkillLevel = 0;
        }
    }

    // Current 0x005b67d0 reports success for every valid subjob even when
    // ExecutePrepared fails.
    return true;
}

void CUser::SendMainClassSkill(CSocket* socket, unsigned int userSocketId) {
    // Current Cached.exe 0x005aa144.
    L2_GUARD_FUNCTION();

    std::array<char, 0x5000> payload{};
    unsigned int payloadLength = 0;
    int acquiredCount = 0;
    int skillId = 0;
    int skillLevel = 0;
    int skillSubLevel = 0;
    int skillLock = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    {
        L2_GUARD(m_skillLock);
        connection.Bind(&skillId);
        connection.Bind(&skillLevel);
        connection.Bind(&skillSubLevel);
        connection.Bind(&skillLock);

        constexpr const wchar_t* statement = L"{Call lin_GetAquireSkill (?, ?)}";
        connection.Prepare(statement);
        const int mainClassSubjobId = 0;
        connection.BindParameter(&m_characterId);
        connection.BindParameter(&mainClassSubjobId);
        if (connection.ExecutePrepared(statement)) {
            while (connection.Fetch()) {
                const int written = ::Assemble(
                    payload.data() + payloadLength, static_cast<int>(payload.size()) - static_cast<int>(payloadLength),
                    "ddd", skillId, skillLevel, skillSubLevel
                );
                payloadLength += static_cast<unsigned int>(written);
                ++acquiredCount;
            }
        }

        // The fourth lin_GetAquireSkill result is bound by the target but is
        // absent from the main-class packet.
        socket->Send("cddb", 0xc5, userSocketId, acquiredCount, static_cast<int>(payloadLength), payload.data());
    }
}

bool CUser::SetAquireSkill(int nSkillId, int nSkillLevel, int nSkillSubLevel, bool bForce, int nSubJobId) {
    // Current Cached.exe 0x005aa378.
    L2_GUARD_FUNCTION();

    bool persist = false;
    {
        L2_WRITE_GUARD(*this);
        if (nSubJobId != -1 && nSubJobId != m_subjobId) {
            return false;
        }
        if (nSkillId <= 0 || nSkillLevel <= 0 || nSkillSubLevel < 0) {
            return false;
        }

        {
            L2_GUARD(m_skillLock);
            const auto acquired = m_acquiredSkills.emplace(nSkillId, UserSkillState{nSkillLevel, nSkillSubLevel});
            if (acquired.second) {
                persist = true;
            } else if (bForce) {
                acquired.first->second.first = nSkillLevel;
                acquired.first->second.second = nSkillSubLevel;
                persist = true;
            } else if (acquired.first->second.first < nSkillLevel) {
                // The shipping body keeps the old in-memory
                // sublevel here, while persisting the requested sublevel below.
                acquired.first->second.first = nSkillLevel;
                persist = true;
            } else if (acquired.first->second.first == nSkillLevel) {
                acquired.first->second.second = nSkillSubLevel;
                persist = true;
            }

            if (persist) {
                DBConn connection(runtime::process::CurrentDatabaseId());
                constexpr const wchar_t* query = L"{Call lin_SetAquireSkill (?, ?, ?, ?, ?)}";
                connection.Prepare(query);
                connection.BindParameter(&m_characterId);
                connection.BindParameter(&m_subjobId);
                connection.BindParameter(&nSkillId);
                connection.BindParameter(&nSkillLevel);
                connection.BindParameter(&nSkillSubLevel);
                connection.ExecutePrepared(query);
            }
        }
    }
    return persist;
}

bool CUser::SetSubJobAcquireSkill(int nSubJobId, int nSkillId, int nSkillLevel, int nSkillSubLevel) {
    // Current Cached.exe 0x005ad358. Unlike the resident owner, this method
    // is SQL-only and takes no CUser or acquired-skill lock.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetSubJobAcquireSkill (?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&nSubJobId);
    connection.BindParameter(&nSkillId);
    connection.BindParameter(&nSkillLevel);
    connection.BindParameter(&nSkillSubLevel);
    return connection.ExecutePrepared(statement);
}

bool CUser::DeleteSubJobSkill(int nSubJobId, int nSkillId) {
    // Current Cached.exe 0x005ad478. The nonresident path does not touch the
    // resident acquired-skill map.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeleteSubJobSkill (?, ?, ? )}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&nSubJobId);
    connection.BindParameter(&nSkillId);
    return connection.ExecutePrepared(statement);
}

bool CUser::LoadHonorsSkill() {
    // Current Cached.exe 0x005aaaac. A failed Execute leaves the current map
    // untouched. A successful Execute clears it before the first Fetch.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    L2_GUARD(m_honorsSkillLock);

    int skillId = 0;
    int skillLevel = 0;
    connection.Bind(&skillId);
    connection.Bind(&skillLevel);
    constexpr const wchar_t* statement = L"{Call lin_GetHonorsSkill(?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    if (connection.ExecutePrepared(statement)) {
        m_acquiredHonorsSkills.clear();
        while (connection.Fetch()) {
            const auto inserted = m_acquiredHonorsSkills.emplace(skillId, skillLevel);
            if (!inserted.second) {
                // Log.Add(
                //     LOG_ERROR, "m_AcquiredHonorsSkills.insert Fail ID : %d Lev : %d", skillId, skillLevel
                // )
                // Log.Add(LOG_WARN,
                //     "[skills] Duplicate honors skill %d for character %u: incoming level %d ignored. Cached level %d retained.",
                //     skillId, m_characterId, skillLevel, inserted.first->second)
                L2_LOG_WARN(Log,
                    "[skills] Duplicate honors skill %d for character %u: incoming level %d ignored; cached level %d retained.",
                    skillId, m_characterId, skillLevel, inserted.first->second);
            }
        }
    }
    return true;
}

bool CUser::SetHonorsSkill(int skillId, int skillLevel) {
    // Current Cached.exe 0x005aacdc updates memory before issuing SQL and
    // reports success regardless of the database execution result.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_honorsSkillLock);

    m_acquiredHonorsSkills[skillId] = skillLevel;

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetHonorsSkill(?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&skillId);
    connection.BindParameter(&skillLevel);
    connection.ExecutePrepared(statement);
    return true;
}

bool CUser::ResetHonorsSkill() {
    // Current Cached.exe 0x005aaec4 clears memory first and always returns
    // true, including when lin_ResetHonorsSkill fails.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_honorsSkillLock);

    m_acquiredHonorsSkills.clear();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_ResetHonorsSkill(?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.ExecutePrepared(statement);
    return true;
}

bool CUser::DeleteHonorsSkill(int skillId) {
    // Current Cached.exe 0x005ab028 erases the resident entry only after the
    // delete procedure succeeds.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_honorsSkillLock);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DelHonorsSkill(?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&skillId);
    const bool deleted = connection.ExecutePrepared(statement);
    if (!deleted) {
        // Log.Add(LOG_ERROR, "m_AcquiredHonorsSkills.delete Fail ID : %d", skillId)
        // Log.Add(LOG_ERROR,
        //     "[skills] Cannot delete honors skill %d for character %u. Cached entry retained.",
        //     skillId, m_characterId)
        L2_LOG_ERROR(Log,
            "[skills] Cannot delete honors skill %d for character %u; cached entry retained.",
            skillId, m_characterId);
        return false;
    }

    m_acquiredHonorsSkills.erase(skillId);
    return true;
}

bool CUser::DeleteAquireSkill(int nSkillId) {
    // Current Cached.exe 0x005abe80.
    L2_GUARD_FUNCTION();

    {
        L2_WRITE_GUARD(*this);
        if (nSkillId > 0) {
            {
                L2_GUARD(m_skillLock);
                m_acquiredSkills.erase(nSkillId);
            }

            DBConn connection(runtime::process::CurrentDatabaseId());
            constexpr const wchar_t* statement = L"{Call lin_DelAquireSkill (?, ?, ?)}";
            connection.Prepare(statement);
            connection.BindParameter(&m_characterId);
            connection.BindParameter(&m_subjobId);
            connection.BindParameter(&nSkillId);
            connection.ExecutePrepared(statement);
        }
    }
    return nSkillId > 0;
}

void CUser::DeleteAcquireSkillList(char16_t* skillList) {
    // Current Cached.exe 0x005a5868 / User.cpp:4476-4485.
    L2_GUARD_FUNCTION();

    const std::u16string source{skillList == nullptr ? u"" : skillList};
    const std::u16string delimiter{u","};
    StringTokenizer tokenizer(source, delimiter);

    {
        L2_GUARD(m_skillLock);
        for (int index = 0; index < tokenizer.CountTokens(); ++index) {
            const std::u16string token = tokenizer.NextToken();
            const long skillId = WindowsWtol(Utf16ToUtf8(runtime::StringView(token)));
            if (skillId > 0) {
                m_acquiredSkills.erase(static_cast<int>(skillId));
            }
        }
    }
}

bool CUser::DeletePledgeSkill(int nSkillId) {
    // Current Cached.exe 0x005ac044. Despite the kept PDB name, this
    // removes the pledge-derived entry from this user's acquired-skill map
    // and from user_skill. It does not mutate the pledge_skill relation.
    L2_GUARD_FUNCTION();

    {
        L2_WRITE_GUARD(*this);
        if (nSkillId > 0) {
            {
                L2_GUARD(m_skillLock);
                m_acquiredSkills.erase(nSkillId);
            }

            DBConn connection(runtime::process::CurrentDatabaseId());
            constexpr const wchar_t* statement = L"{Call lin_DelPledgeSkill (?, ?)}";
            connection.Prepare(statement);
            connection.BindParameter(&m_characterId);
            connection.BindParameter(&nSkillId);
            connection.ExecutePrepared(statement);
        }
    }
    return nSkillId > 0;
}

bool CUser::SetAcquirePotentialSkill(
    int skillId, int skillLevel, int dualClass, int potentialType, int& acquiredPointDelta
) {
    // Current Cached.exe 0x005aa5c4.
    L2_GUARD_FUNCTION();

    if (skillId <= 0 || skillLevel <= 0)
        return true;
    if ((dualClass == 0 && m_subjobId != 0) || (dualClass != 0 && m_subjobId == 0)) {
        // Log.Add(LOG_ERROR, "User is not DualClass Now")
        // Log.Add(LOG_WARN,
        //     "[skills] Rejecting potential skill %d for character %u: dual-class flag %d is incompatible with current subjob %d.",
        //     skillId, m_characterId, dualClass, m_subjobId)
        L2_LOG_WARN(Log,
            "[skills] Rejecting potential skill %d for character %u: dual-class flag %d is incompatible with current subjob %d.",
            skillId, m_characterId, dualClass, m_subjobId);
        return false;
    }

    {
        L2_GUARD(m_skillLock);

        const auto acquired = m_acquiredSkills.emplace(skillId, UserSkillState{skillLevel, 0});
        if (!acquired.second) {
            acquired.first->second.first = skillLevel;
        }

        const auto potential = m_acquiredPotentialSkills.emplace(skillId, UserSkillState{skillLevel, 0});
        if (!potential.second) {
            const int previousLevel = potential.first->second.first;
            if (previousLevel < skillLevel) {
                WritePotentialSkillAudit(*this, skillId, skillLevel);
                acquiredPointDelta = runtime::AddInt32Wrapping(acquiredPointDelta, runtime::SubtractInt32Wrapping(skillLevel, previousLevel));
            }
            potential.first->second.first = skillLevel;
        } else {
            WritePotentialSkillAudit(*this, skillId, skillLevel);
            acquiredPointDelta = runtime::AddInt32Wrapping(acquiredPointDelta, skillLevel);
        }
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetAcquirePotentialSkill (?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&m_subjobId);
    connection.BindParameter(&potentialType);
    connection.BindParameter(&skillId);
    connection.BindParameter(&skillLevel);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "qsSetAcquirePotentialSkill fail")
        // Log.Add(LOG_ERROR,
        //     "[skills] Cannot persist potential skill %d, level %d, type %d for character %u, subjob %d. Skill changes remain in memory.",
        //     skillId, skillLevel, potentialType, m_characterId, m_subjobId)
        L2_LOG_ERROR(Log,
            "[skills] Cannot persist potential skill %d, level %d, type %d for character %u, subjob %d; skill changes remain in memory.",
            skillId, skillLevel, potentialType, m_characterId, m_subjobId);
        return false;
    }
    return true;
}

bool CUser::ResetPotentialSkill(int userSocketId, CSocket* socket) {
    // Current Cached.exe 0x005bbd70.
    L2_GUARD_FUNCTION();
    if (!socket)
        return false;

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_ResetAcquirePotentialSkill (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&m_subjobId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d][ResetPotentialSkill]"
        //     "Fail to Reset PotentialDB, id[%u]",
        //     "User.cpp", 0xea2, m_characterId
        // )
        // Log.Add(LOG_ERROR,
        //     "[skills] Cannot reset potential skills for character %u, subjob %d.",
        //     m_characterId, m_subjobId)
        L2_LOG_ERROR(Log,
            "[skills] Cannot reset potential skills for character %u, subjob %d.",
            m_characterId, m_subjobId);
        return false;
    }

    if (!LoadAquireSkill(m_subjobId)) {
        // Log.Add(LOG_ERROR, "cannot load skill[%u] (reload skill)", m_characterId)
        // Log.Add(LOG_ERROR,
        //     "[skills] Potential skills reset in the database for character %u, but reload for subjob %d failed.",
        //     m_characterId, m_subjobId)
        L2_LOG_ERROR(Log,
            "[skills] Potential skills reset in the database for character %u, but reload for subjob %d failed.",
            m_characterId, m_subjobId);
        return false;
    }
    SendReloadSkill(socket, static_cast<unsigned int>(userSocketId), m_subjobId, true, false);
    return true;
}

bool CUser::LoadAquireSkill(int nSubJobId) {
    L2_GUARD_FUNCTION();

    if (static_cast<unsigned int>(nSubJobId) >= m_skillReuseBySubjob.size()) {
        // Log.Add(LOG_ERROR, "nSubJobId %d in LoadAquireSkill is not valid", nSubJobId)
        // Log.Add(LOG_ERROR,
        //     "[skills] Cannot load acquired skills for character %u: invalid subjob %d (valid range 0-4).",
        //     m_characterId, nSubJobId)
        L2_LOG_ERROR(Log,
            "[skills] Cannot load acquired skills for character %u: invalid subjob %d (valid range 0-4).",
            m_characterId, nSubJobId);
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    int nSkillId = 0;
    int nSkillLevel = 0;
    int nSkillSubLevel = 0;
    int nSkillLock = 0;

    L2_GUARD(m_skillLock);
    connection.Bind(&nSkillId);
    connection.Bind(&nSkillLevel);
    connection.Bind(&nSkillSubLevel);
    connection.Bind(&nSkillLock);

    constexpr const wchar_t* acquireQuery = L"{Call lin_GetAquireSkill (?, ?)}";
    connection.Prepare(acquireQuery);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&nSubJobId);
    if (connection.ExecutePrepared(acquireQuery)) {
        m_acquiredSkills.clear();
        while (connection.Fetch()) {
            const auto loaded = m_acquiredSkills.emplace(nSkillId, UserSkillState{nSkillLevel, nSkillSubLevel});
            // Duplicate rows update only the level. The target deliberately
            // keeps the sublevel stored by the first row for that skill ID.
            if (!loaded.second && loaded.first->second.first < nSkillLevel) {
                loaded.first->second.first = nSkillLevel;
            }
            SetSkillLock(nSkillId, nSkillLock, false);

            nSkillId = 0;
            nSkillLevel = 0;
            nSkillSubLevel = 0;
            nSkillLock = 0;
        }
    }

    // The shipping method ignores this valid-subjob result and proceeds to
    // the one-time reuse-delay load on both query-success and query-failure.
    _LoadAcquiredPotentialSkill(nSubJobId);

    if (!m_skillReuseDelayLoaded) {
        m_skillReuseDelayLoaded = true;
        for (UserSkillMap& reuseMap : m_skillReuseBySubjob) {
            reuseMap.clear();
        }

        const int currentTime = runtime::crt::Time32Now();
        int reuseSkillId = 0;
        int reuseSubjobId = 0;
        int toEndTime = 0;
        int delay = 0;
        DBConn reuseConnection(runtime::process::CurrentDatabaseId());
        reuseConnection.Bind(&reuseSkillId);
        reuseConnection.Bind(&reuseSubjobId);
        reuseConnection.Bind(&toEndTime);
        reuseConnection.Bind(&delay);

        constexpr const wchar_t* reuseQuery = L"{Call lin_GetSkillReuseDelay (?, ?)}";
        reuseConnection.Prepare(reuseQuery);
        reuseConnection.BindParameter(&m_characterId);
        reuseConnection.BindParameter(&currentTime);
        if (reuseConnection.ExecutePrepared(reuseQuery)) {
            while (reuseConnection.Fetch()) {
                _InsertOrUpdateLastUse(reuseSubjobId, reuseSkillId, toEndTime, delay);
            }
        }
    }

    return true;
}

void CUser::_PackingReuseDelay(int nSubJobId, char* buffer, int bufferSize, unsigned int& length, int& count) {
    // Current Cached.exe 0x005a4a38.
    L2_GUARD_FUNCTION();

    const int now = runtime::crt::Time32Now();
    if (static_cast<unsigned int>(nSubJobId) >= m_skillReuseBySubjob.size()) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Invalid Subjob Id, char dbid[%d]", "User.cpp", 0x109e,
        //     static_cast<int>(m_characterId)
        // )
        // Log.Add(LOG_ERROR,
        //     "[skills] Cannot assemble skill cooldowns for character %u: invalid subjob %d (valid range 0-4).",
        //     m_characterId, nSubJobId)
        L2_LOG_ERROR(Log,
            "[skills] Cannot assemble skill cooldowns for character %u: invalid subjob %d (valid range 0-4).",
            m_characterId, nSubJobId);
        runtime::process::ReportCallStack(LOG_ERROR, false);
        return;
    }

    UserSkillMap& reuseMap = m_skillReuseBySubjob[static_cast<std::size_t>(nSubJobId)];
    for (auto entry = reuseMap.begin(); entry != reuseMap.end();) {
        if (now < entry->second.first) {
            const int written = ::Assemble(
                buffer + length, bufferSize - static_cast<int>(length), "ddd", entry->first, entry->second.first,
                entry->second.second
            );
            length += static_cast<unsigned int>(written);
            ++count;
            ++entry;
        } else {
            entry = reuseMap.erase(entry);
        }
    }
}

void CUser::SendReloadSkill(CSocket* socket, unsigned int userSocketId, int nSubJobId, bool trailingFlag, bool byteFlag) {
    // Current Cached.exe 0x005a4f28. The two bool identifiers do not survive.
    // their exact positions are the reply byte and final dword respectively.
    L2_GUARD_FUNCTION();

    std::array<char, 0x5000> acquiredPayload{};
    std::array<char, 0x5000> reusePayload{};
    unsigned int acquiredLength = 0;
    unsigned int reuseLength = 0;
    int acquiredCount = 0;
    int currentSubjobReuseCount = 0;
    int sharedReuseCount = 0;

    {
        L2_GUARD(m_skillLock);
        for (const auto& acquired : m_acquiredSkills) {
            int skillLock = 0;
            const auto lock = m_skillLocks.find(acquired.first);
            if (lock != m_skillLocks.end()) {
                skillLock = lock->second;
            }

            const int written = ::Assemble(
                acquiredPayload.data() + acquiredLength,
                static_cast<int>(acquiredPayload.size()) - static_cast<int>(acquiredLength), "dddd", acquired.first,
                acquired.second.first, acquired.second.second, skillLock
            );
            acquiredLength += static_cast<unsigned int>(written);
            ++acquiredCount;
        }

        _PackingReuseDelay(
            nSubJobId, reusePayload.data(), static_cast<int>(reusePayload.size()), reuseLength, currentSubjobReuseCount
        );
        _PackingReuseDelay(4, reusePayload.data(), static_cast<int>(reusePayload.size()), reuseLength, sharedReuseCount);
    }

    if (acquiredCount < 1 && currentSubjobReuseCount < 1 && sharedReuseCount < 1) {
        socket->Send("cdcdddd", 0xbd, userSocketId, byteFlag ? 1 : 0, 0, 0, 0, trailingFlag ? 1 : 0);
        return;
    }

    socket->Send(
        "cdcddddbb", 0xbd, userSocketId, byteFlag ? 1 : 0, acquiredCount, currentSubjobReuseCount, sharedReuseCount,
        trailingFlag ? 1 : 0, static_cast<int>(acquiredLength), acquiredPayload.data(), static_cast<int>(reuseLength),
        reusePayload.data()
    );
}

void CUser::ResetAcquiresSkillsFromBuffer(int nSubJobId, int count, const std::uint8_t* packet) {
    // Current Cached.exe 0x005ab200. Despite the original Reset name, the
    // shipping owner does not clear the map: every row inserts or overwrites
    // one skill and immediately persists it.
    L2_GUARD_FUNCTION();

    {
        L2_WRITE_GUARD(*this);
        if (static_cast<unsigned int>(nSubJobId) < 5U) {
            m_subjobId = nSubJobId;
        } else {
            // Log.Add(LOG_ERROR, "Try to set invalid subjob id (%d)", nSubJobId)
            // Log.Add(LOG_ERROR,
            //     "[skills] Invalid subjob %d while importing skills for character %u. Current subjob %d retained, row processing continues.",
            //     nSubJobId, m_characterId, m_subjobId)
            L2_LOG_ERROR(Log,
                "[skills] Invalid subjob %d while importing skills for character %u; current subjob %d retained, row processing continues.",
                nSubJobId, m_characterId, m_subjobId);
            runtime::process::ReportCallStack(LOG_ERROR, false);
        }

        {
            L2_GUARD(m_skillLock);
            for (int index = 0; index < count; ++index) {
                int skillId = 0;
                int skillLevel = 0;
                int skillSubLevel = 0;
                packet = ::Disassemble(packet, "ddd", &skillId, &skillLevel, &skillSubLevel);

                m_acquiredSkills.insert_or_assign(skillId, UserSkillState{skillLevel, skillSubLevel});

                DBConn connection(runtime::process::CurrentDatabaseId());
                constexpr const wchar_t* statement = L"{Call lin_SetAquireSkill (?, ?, ?, ?, ?)}";
                connection.Prepare(statement);
                connection.BindParameter(&m_characterId);
                connection.BindParameter(&nSubJobId);
                connection.BindParameter(&skillId);
                connection.BindParameter(&skillLevel);
                connection.BindParameter(&skillSubLevel);
                connection.ExecutePrepared(statement);
            }
        }
    }
}

void CUser::SendAquireSkill(CSocket* socket, unsigned int userSocketId) {
    // Current Cached.exe 0x005bbf68.
    L2_GUARD_FUNCTION();

    bool acquireListEmpty = false;
    {
        L2_GUARD(m_skillLock);
        acquireListEmpty = m_acquiredSkills.empty();
    }
    if (acquireListEmpty) {
        LoadAquireSkill(m_subjobId);
    }

    std::array<char, 0x5000> payload{};
    unsigned int payloadLength = 0;
    int acquiredCount = 0;
    int currentSubjobReuseCount = 0;
    int sharedReuseCount = 0;
    {
        L2_GUARD(m_skillLock);
        for (const auto& acquired : m_acquiredSkills) {
            int skillLock = 0;
            const auto lock = m_skillLocks.find(acquired.first);
            if (lock != m_skillLocks.end()) {
                skillLock = lock->second;
            }

            const int written = ::Assemble(
                payload.data() + payloadLength, static_cast<int>(payload.size()) - static_cast<int>(payloadLength), "dddd",
                acquired.first, acquired.second.first, acquired.second.second, skillLock
            );
            payloadLength += static_cast<unsigned int>(written);
            ++acquiredCount;
        }

        _PackingReuseDelay(
            m_subjobId, payload.data(), static_cast<int>(payload.size()), payloadLength, currentSubjobReuseCount
        );
        _PackingReuseDelay(4, payload.data(), static_cast<int>(payload.size()), payloadLength, sharedReuseCount);
    }

    if (acquiredCount < 1 && currentSubjobReuseCount < 1 && sharedReuseCount < 1) {
        socket->Send("cddddd", 0x18, userSocketId, static_cast<int>(m_characterId), 0, 0, 0);
        return;
    }

    socket->Send(
        "cdddddb", 0x18, userSocketId, static_cast<int>(m_characterId), acquiredCount, currentSubjobReuseCount,
        sharedReuseCount, static_cast<int>(payloadLength), payload.data()
    );
}

void CUser::SendBlockList(CSocket* socket, unsigned int userSocketId) {
    // Current Cached.exe 0x005ab9f0.
    L2_GUARD_FUNCTION();

    CPacketHelperEx<20480> blockPacket;
    int blockCharacterId = 0;
    std::array<char16_t, 51> memo{};
    std::int64_t memoIndicator = -1;
    int fetchedCount = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&blockCharacterId);
    connection.BindNullable(memo.data(), 0x66, &memoIndicator);
    constexpr const wchar_t* statement = L"{Call lin_LoadBlockList (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            CUserSP blockedUser = g_userDb.GetUser(blockCharacterId, true, false, runtime::process::CurrentDatabaseId());
            if (blockedUser) {
                const std::u16string blockedName = std::u16string(blockedUser->GetCharName());
                const std::u16string targetMemo = Utils::UnescapeSqlApostrophes(memo.data());
                const int written = blockPacket.Assemble("dSS", blockCharacterId, blockedName.c_str(), targetMemo.c_str());
                if (written == 0) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "[%s][%d] My BlockList is overflow, "
                    //     "char id[%d] count[%d]",
                    //     "User.cpp", 0x1122, static_cast<int>(m_characterId), blockPacket.GetAssembledCount()
                    // )
                    // Log.Add(LOG_ERROR,
                    //     "[block list] Cannot append blocked character %d for character %u after %d entries. Stopping the read and sending the assembled prefix.",
                    //     blockCharacterId, m_characterId, blockPacket.GetAssembledCount())
                    L2_LOG_ERROR(Log,
                        "[block list] Cannot append blocked character %d for character %u after %d entries; stopping the read and sending the assembled prefix.",
                        blockCharacterId, m_characterId, blockPacket.GetAssembledCount());
                    break;
                }

                if (blockPacket.GetAssembledCount() > 0x7f) {
                    socket->Send(
                        "cddb", 0x7c, userSocketId, blockPacket.GetAssembledCount(), blockPacket.GetLength(),
                        blockPacket.GetPacket()
                    );
                    blockPacket.Reset();
                }
            }
            ++fetchedCount;
        }

        if (blockPacket.GetAssembledCount() > 0) {
            socket->Send(
                "cddb", 0x7c, userSocketId, blockPacket.GetAssembledCount(), blockPacket.GetLength(), blockPacket.GetPacket()
            );
        }
        if (fetchedCount > 0x200) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d] My BlockList is Invalid, "
            //     "char id[%d] count[%d]",
            //     "User.cpp", 0x1137, static_cast<int>(m_characterId), fetchedCount
            // )
            // Log.Add(LOG_WARN,
            //     "[block list] Character %u has %d fetched entries, exceeding the expected limit 512. Available entries have already been sent.",
            //     m_characterId, fetchedCount)
            L2_LOG_WARN(Log,
                "[block list] Character %u has %d fetched entries, exceeding the expected limit 512; available entries have already been sent.",
                m_characterId, fetchedCount);
        }
    }
}

void CUser::SendAcquiredPotentialSkill(
    CSocket* socket, unsigned int characterId [[maybe_unused]], unsigned int userSocketId
) {
    // Current Cached.exe 0x005b6a98. The characterId argument is present in
    // the exact guard signature/call ABI but is not read by the body.
    L2_GUARD_FUNCTION();

    std::array<char, 0x5000> payload{};
    unsigned int payloadLength = 0;
    int acquiredCount = 0;
    {
        L2_GUARD(m_skillLock);
        for (const auto& acquired : m_acquiredPotentialSkills) {
            const int written = ::Assemble(
                payload.data() + payloadLength, static_cast<int>(payload.size()) - static_cast<int>(payloadLength), "dd",
                acquired.first, acquired.second.first
            );
            payloadLength += static_cast<unsigned int>(written);
            ++acquiredCount;
        }
    }

    int pointType1 = 0;
    int pointType2 = 0;
    int pointType3 = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&pointType1);
    connection.Bind(&pointType2);
    connection.Bind(&pointType3);
    constexpr const wchar_t* statement = L"{Call lin_GetAcquirePotentialPoint (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&m_subjobId);
    const bool pointsLoaded = connection.ExecutePrepared(statement) && connection.Fetch();
    if (pointsLoaded && acquiredCount > 0) {
        socket->Send(
            "chdddddb", 0xc6, 0xf4, userSocketId, pointType1, pointType2, pointType3, acquiredCount,
            static_cast<int>(payloadLength), payload.data()
        );
        return;
    }

    socket->Send("chddddd", 0xc6, 0xf4, userSocketId, pointType1, pointType2, pointType3, 0);
}

void CUser::SetCreateDate(int year, int month, int day, int hour, int minute, int second) {
    L2_GUARD_FUNCTION();
    if (year < 0)
        return;

    m_createDate.wYear = static_cast<std::uint16_t>(year);
    m_createDate.wMonth = static_cast<std::uint16_t>(month);
    m_createDate.wDay = static_cast<std::uint16_t>(day);
    m_createDate.wHour = static_cast<std::uint16_t>(hour);
    m_createDate.wMinute = static_cast<std::uint16_t>(minute);
    m_createDate.wSecond = static_cast<std::uint16_t>(second);
}

void CUser::SetLastLogin(int year, int month, int day, int hour, int minute, int second) {
    L2_GUARD_FUNCTION();
    if (year < 0)
        return;

    m_lastLoginTime.wYear = static_cast<std::uint16_t>(year);
    m_lastLoginTime.wMonth = static_cast<std::uint16_t>(month);
    m_lastLoginTime.wDay = static_cast<std::uint16_t>(day);
    m_lastLoginTime.wHour = static_cast<std::uint16_t>(hour);
    m_lastLoginTime.wMinute = static_cast<std::uint16_t>(minute);
    m_lastLoginTime.wSecond = static_cast<std::uint16_t>(second);
}

void CUser::SetLastLogout(int year, int month, int day, int hour, int minute, int second) {
    L2_GUARD_FUNCTION();
    if (year < 0)
        return;

    m_lastLogoutTime.wYear = static_cast<std::uint16_t>(year);
    m_lastLogoutTime.wMonth = static_cast<std::uint16_t>(month);
    m_lastLogoutTime.wDay = static_cast<std::uint16_t>(day);
    m_lastLogoutTime.wHour = static_cast<std::uint16_t>(hour);
    m_lastLogoutTime.wMinute = static_cast<std::uint16_t>(minute);
    m_lastLogoutTime.wSecond = static_cast<std::uint16_t>(second);

    // Current CUser::SetLastLogout 0x005a5574 constructs the map key in this
    // exact order: world ID in the low/primary half, account ID in the
    // high/secondary half.
    CAccountSP account = CAccount::Find(CAccountKey{m_worldId, m_accountId});
    if (account) {
        account->SetLastLogoutTime(m_lastLogoutTime);
    }
}

void CUser::ShowPetiMsg(CSocket* socket) {
    // Current Cached.exe 0x005adfa8.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 1001> petitionMessage{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(petitionMessage.data(), 0x7d2);

    constexpr const wchar_t* statement = L"{Call lin_GetPetitionMsg (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        socket->Send("cdS", 0x3f, m_characterId, petitionMessage.data());
    }
}

void CUser::LoadLastLogout() {
    L2_GUARD_FUNCTION();

    int lastLogoutYear = 0;
    int lastLogoutMonth = 0;
    int lastLogoutDay = 0;
    int lastLogoutHour = 0;
    int lastLogoutMinute = 0;
    int lastLogoutSecond = 0;
    int lastLoginYear = 0;
    int lastLoginMonth = 0;
    int lastLoginDay = 0;
    int lastLoginHour = 0;
    int lastLoginMinute = 0;
    int lastLoginSecond = 0;
    int createYear = 0;
    int createMonth = 0;
    int createDay = 0;
    int createHour = 0;
    int createMinute = 0;
    int createSecond = 0;
    std::int64_t nullableIndicator = -1;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.BindNullable(&lastLogoutYear, &nullableIndicator);
    connection.BindNullable(&lastLogoutMonth, &nullableIndicator);
    connection.BindNullable(&lastLogoutDay, &nullableIndicator);
    connection.BindNullable(&lastLogoutHour, &nullableIndicator);
    connection.BindNullable(&lastLogoutMinute, &nullableIndicator);
    connection.BindNullable(&lastLogoutSecond, &nullableIndicator);
    connection.BindNullable(&lastLoginYear, &nullableIndicator);
    connection.BindNullable(&lastLoginMonth, &nullableIndicator);
    connection.BindNullable(&lastLoginDay, &nullableIndicator);
    connection.BindNullable(&lastLoginHour, &nullableIndicator);
    connection.BindNullable(&lastLoginMinute, &nullableIndicator);
    connection.BindNullable(&lastLoginSecond, &nullableIndicator);
    connection.BindNullable(&createYear, &nullableIndicator);
    connection.BindNullable(&createMonth, &nullableIndicator);
    connection.BindNullable(&createDay, &nullableIndicator);
    connection.BindNullable(&createHour, &nullableIndicator);
    connection.BindNullable(&createMinute, &nullableIndicator);
    connection.BindNullable(&createSecond, &nullableIndicator);

    constexpr const wchar_t* query = L"{Call lin_LoadLastLogout (?)}";
    connection.Prepare(query);
    connection.BindParameter(&m_characterId);
    if (!connection.ExecutePrepared(query) || !connection.Fetch()) {
        return;
    }

    if (lastLoginYear >= 0) {
        SetLastLogin(lastLoginYear, lastLoginMonth, lastLoginDay, lastLoginHour, lastLoginMinute, lastLoginSecond);
    }
    if (lastLogoutYear >= 0) {
        SetLastLogout(lastLogoutYear, lastLogoutMonth, lastLogoutDay, lastLogoutHour, lastLogoutMinute, lastLogoutSecond);
    }
    if (createYear >= 0) {
        SetCreateDate(createYear, createMonth, createDay, createHour, createMinute, createSecond);
    }
}

void CUser::CheckVitalityReset() {
    L2_GUARD_FUNCTION();
    if (m_vitality->CheckReset()) {
        m_vitality->WriteVitalityLog(this, CVitality::VitalityReset);
    }
}

CVitalData CUser::GetVitalData() {
    L2_GUARD_FUNCTION();
    return m_vitality->m_Data;
}

void CUser::UpdateVitality(CVitalData& data) {
    // Current Cached.exe 0x0059d3bc forwards to CVitality::Update through
    // the CVitality pointer at CUser +0x25d0.
    L2_GUARD_FUNCTION();
    m_vitality->Update(data);
}

void CUser::UpdateVitalityByAdmin(CVitalData& data, int charDbId, int subjobId) {
    L2_GUARD_FUNCTION();
    m_vitality->UpdateByAdmin(data, charDbId, subjobId);
}

int CUser::GetVitality() {
    L2_GUARD_FUNCTION();
    return m_vitality->m_Data.vitalPoint;
}

void CUser::AssembleVitality(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    m_vitality->AssembleForLoading(packet);
}

void CUser::LoadUserVitality() {
    L2_GUARD_FUNCTION();
    int vitalPoint = 0;
    int restoreCount = 0;
    int latestResetDateVP = 0;
    int latestResetDateRC = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&vitalPoint);
    connection.Bind(&restoreCount);
    connection.Bind(&latestResetDateVP);
    connection.Bind(&latestResetDateRC);

    constexpr const wchar_t* query = L"{Call lin_LoadCharVitality (?, ?)}";
    connection.Prepare(query);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&m_subjobId);
    if (!connection.ExecutePrepared(query) || !connection.Fetch()) {
        // The current body clears all four outputs on either
        // execute or fetch failure before calling CVitality::Init.
        vitalPoint = 0;
        restoreCount = 0;
        latestResetDateVP = 0;
        latestResetDateRC = 0;
    }

    m_vitality->Init(vitalPoint, restoreCount, latestResetDateVP, latestResetDateRC);
    CheckVitalityReset();
}

void CUser::InitQuestMemo() {
    L2_GUARD_FUNCTION();

    // MemoStruct's default state is the exact state written by current
    // 0x0059ee68: journal 1, empty quest/state flags, and ten NPC-log slots
    // whose type is USER_NPC_LOG_MAX and whose id/count are zero.
    for (MemoStruct& memo : m_questMemos) {
        memo = MemoStruct{};
    }
}

void CUser::LoadQuestFromDB() {
    L2_GUARD_FUNCTION();

    InitQuestMemo();

    {
        int questId = 0;
        int journal = 0;
        int state = 0;
        int state2 = 0;
        int state3 = 0;
        int state4 = 0;

        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Bind(&questId);
        connection.Bind(&journal);
        connection.Bind(&state);
        connection.Bind(&state2);
        connection.Bind(&state3);
        connection.Bind(&state4);

        constexpr const wchar_t* query = L"{Call lin_SelectUserQuest (?, 0, 0)}";
        connection.Prepare(query);
        connection.BindParameter(&m_characterId);
        if (connection.ExecutePrepared(query)) {
            std::size_t index = 0;
            while (connection.Fetch()) {
                if (index >= m_questMemos.size()) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "[User.cpp][3086] [%u] character has too many "
                    //     "unregistered quest.",
                    //     m_characterId
                    // )
                    // Log.Add(LOG_ERROR,
                    //     "[quest] Character %u has more than %zu quest memos. Quest %d and subsequent rows are not loaded.",
                    //     m_characterId, m_questMemos.size(), questId)
                    L2_LOG_ERROR(Log,
                        "[quest] Character %u has more than %zu quest memos; quest %d and subsequent rows are not loaded.",
                        m_characterId, m_questMemos.size(), questId);
                    break;
                }

                MemoStruct& memo = m_questMemos[index++];
                memo.nQuestID = questId;
                memo.nJournal = journal;
                memo.nState = state;
                memo.nState2 = state2;
                memo.nState3 = state3;
                memo.nState4 = state4;
            }
        }
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    for (MemoStruct& memo : m_questMemos) {
        if (memo.nQuestID <= 0)
            continue;

        int questId = 0;
        int npcId = 0;
        int logType = 0;
        int count = 0;
        connection.Bind(&questId);
        connection.Bind(&npcId);
        connection.Bind(&logType);
        connection.Bind(&count);

        const int selectedQuestId = memo.nQuestID;
        const int allNpcLogs = -1;
        constexpr const wchar_t* query = L"{call lin_SelectUserNPCLog (?,?,?)}";
        connection.Prepare(query);
        connection.BindParameter(&m_characterId);
        connection.BindParameter(&selectedQuestId);
        connection.BindParameter(&allNpcLogs);
        if (connection.ExecutePrepared(query)) {
            while (connection.Fetch()) {
                memo.UpdateNPCLog(static_cast<UserNPCLogType>(logType), npcId, count);
            }
        }
        connection.UnBind();
    }
}

void CUser::SetQuestNPCLog(const unsigned char* packet, int characterId [[maybe_unused]]) {
    // Current Cached.exe 0x005bb7e0. The second argument survives in the PDB
    // signature and is supplied by RequestSetQuestNPCLog, but the shipping
    // body never reads it.
    L2_GUARD_FUNCTION();

    int recordCount = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &recordCount);
    {
        L2_WRITE_GUARD(m_dataLock);
        for (int record = 0; record < recordCount; ++record) {
            int memoIndex = 0;
            int questId = 0;
            int logType = 0;
            int npcId = 0;
            int count = 0;
            cursor = ::Disassemble(cursor, "ddddd", &memoIndex, &questId, &logType, &npcId, &count);
            if (m_questMemos[static_cast<std::size_t>(memoIndex)].nQuestID == questId) {
                SaveQuestNPCLog(memoIndex, questId, static_cast<UserNPCLogType>(logType), npcId, count);
            }
        }
    }
}

void CUser::LoadQuestNPCLog(CSocket* socket, int characterId [[maybe_unused]], unsigned int requestContext) {
    // Current Cached.exe 0x0059eca4. The character ID survives in the PDB
    // signature but is not read by the shipping body. The third argument is
    // the request value returned to L2Server.
    L2_GUARD_FUNCTION();
    if (!socket)
        return;

    std::array<char, 0x5000> records{};
    int recordBytes = 0;
    int recordCount = 0;

    {
        L2_READ_GUARD(m_dataLock);
        for (std::size_t memoIndex = 0; memoIndex < m_questMemos.size(); ++memoIndex) {
            const MemoStruct& memo = m_questMemos[memoIndex];
            if (memo.nQuestID <= 0)
                continue;

            for (const UserNPCLog& log : memo.NPCLog) {
                if (log.nNPCID == 0)
                    continue;

                const int assembled = ::Assemble(
                    records.data() + recordBytes, static_cast<int>(records.size()) - recordBytes, "ddddd",
                    static_cast<int>(memoIndex), memo.nQuestID, static_cast<int>(log.userLogType), log.nNPCID, log.nCount
                );
                if (assembled > 0) {
                    recordBytes += assembled;
                }
                // Cached.exe advances the logical count even when its fixed
                // packet buffer can no longer accept the assembled row.
                ++recordCount;
            }
        }
    }

    if (recordCount > 0) {
        socket->Send("chddb", 0xc6, 0x81, static_cast<int>(requestContext), recordCount, recordBytes, records.data());
    }
}

void CUser::SaveQuestNPCLog(int memoIndex, int questId, UserNPCLogType userLogType, int npcId, int count) {
    // Current Cached.exe 0x005a8d2c. The -1 branch flushes all 41-by-10
    // slots, including currently empty NPCLog records, and uses the target's
    // SQL_C_SLONG/SQL_DOUBLE helper for the fifth parameter.
    L2_GUARD_FUNCTION();

    constexpr const wchar_t* statement = L"{call lin_UpdateUserNPCLog (?, ?, ?, ?, ?)}";
    DBConn connection(runtime::process::CurrentDatabaseId());
    if (memoIndex != -1) {
        MemoStruct& memo = m_questMemos[static_cast<std::size_t>(memoIndex)];
        if (memo.nQuestID != questId)
            return;

        const int logType = static_cast<int>(userLogType);
        connection.Prepare(statement);
        connection.BindParameter(&m_characterId);
        connection.BindParameter(&questId);
        connection.BindParameter(&npcId);
        connection.BindParameter(&logType);
        connection.BindParameter(&count);
        if (connection.ExecutePrepared(statement)) {
            memo.UpdateNPCLog(userLogType, npcId, count);
        }
        return;
    }

    for (MemoStruct& memo : m_questMemos) {
        if (memo.nQuestID <= 0)
            continue;
        for (UserNPCLog& log : memo.NPCLog) {
            const int savedQuestId = memo.nQuestID;
            const int savedNpcId = log.nNPCID;
            const int savedLogType = static_cast<int>(log.userLogType);
            const long savedCount = static_cast<long>(log.nCount);
            connection.Prepare(statement);
            connection.BindParameter(&m_characterId);
            connection.BindParameter(&savedQuestId);
            connection.BindParameter(&savedNpcId);
            connection.BindParameter(&savedLogType);
            connection.BindParameter(&savedCount);
            if (connection.ExecutePrepared(statement)) {
                memo.UpdateNPCLog(log.userLogType, log.nNPCID, log.nCount);
            }
        }
    }
}

void CUser::SetLastInitTime(const wchar_t* lastInitTime) {
    L2_GUARD_FUNCTION();

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    std::swscanf(lastInitTime, L"%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second);
    m_lastVoteInitTime = {
        static_cast<std::uint16_t>(year),
        static_cast<std::uint16_t>(month),
        0,
        static_cast<std::uint16_t>(day),
        static_cast<std::uint16_t>(hour),
        static_cast<std::uint16_t>(minute),
        static_cast<std::uint16_t>(second),
        0,
    };
}

void CUser::LoadVoteSystem() {
    L2_GUARD_FUNCTION();

    int bonusTimeLimit = 0;
    int playTime = 0;
    int voteCount = 0;
    int bonusCount = 0;
    int playTimeCount = 0;
    std::array<char16_t, 25> lastInitTime{};
    constexpr char16_t initialTime[] = u"0-0-0 0:0:0";
    std::copy(std::begin(initialTime), std::end(initialTime), lastInitTime.begin());

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&bonusTimeLimit);
    connection.Bind(&playTime);
    connection.Bind(&voteCount);
    connection.Bind(&bonusCount);
    connection.Bind(&playTimeCount);
    connection.Bind(lastInitTime.data(), 0x32);

    {
        L2_WRITE_GUARD(m_dataLock);
        constexpr const wchar_t* query = L"{Call lin_LoadVoteSystem (?)}";
        connection.Prepare(query);
        connection.BindParameter(&m_characterId);
        if (connection.ExecutePrepared(query) && !connection.Fetch()) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[User.cpp][7757] Fetch Query Failed "
            //     "(qsLoadVoteSystem)."
            // )
            // Log.Add(LOG_ERROR,
            //     "[voting] No vote-system row fetched for character %u after query execution. Cached values retained.",
            //     m_characterId)
            L2_LOG_ERROR(Log,
                "[voting] No vote-system row fetched for character %u after query execution; cached values retained.",
                m_characterId);
            return;
        }

        // Shipping behavior is asymmetric: Execute failure stores
        // the initialized zeros, while Execute success followed by Fetch failure
        // returns above without modifying the existing state.
        m_bonusTimeLimit = bonusTimeLimit;
        m_playTime = playTime;
        m_voteCount = voteCount;
        m_bonusCount = bonusCount;
        m_playTimeCount = playTimeCount;
        SetLastInitTime(Utf16ToWide(lastInitTime.data()).c_str());
    }
}

bool CUser::SaveVoteSystem(
    int bonusTimeLimit, int playTime, int voteCount, int bonusCount, int playTimeCount, SYSTEMTIME* lastInitTime
) {
    // Current Cached.exe 0x005b3524.
    L2_GUARD_FUNCTION();

    std::array<wchar_t, 20> lastInitTimeText{};
    std::swprintf(
        lastInitTimeText.data(), lastInitTimeText.size(), L"%d-%d-%d %d:%d:%d", static_cast<int>(lastInitTime->wYear),
        static_cast<int>(lastInitTime->wMonth), static_cast<int>(lastInitTime->wDay), static_cast<int>(lastInitTime->wHour),
        static_cast<int>(lastInitTime->wMinute), static_cast<int>(lastInitTime->wSecond)
    );

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveVoteSystem (?, ?, ?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&bonusTimeLimit);
    connection.BindParameter(&playTime);
    connection.BindParameter(&voteCount);
    connection.BindParameter(&bonusCount);
    connection.BindParameter(&playTimeCount);
    connection.BindParameter(lastInitTimeText.data());
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] Vote system save error. "
        //     "[%ls : %d, %d, %d, %d, %d, (%ls)]",
        //     "User.cpp", 0x1e73, m_characterName.c_str(), bonusTimeLimit, playTime, voteCount, bonusCount, playTimeCount,
        //     lastInitTimeText.data()
        // )
        // Log.Add(LOG_ERROR,
        //     L"[voting] Cannot save state for character %u (%ls): bonus time limit %d, play time %d, votes %d, bonuses %d, play-time count %d, last initialization %ls.",
        //     m_characterId, m_characterName.c_str(), bonusTimeLimit, playTime, voteCount, bonusCount, playTimeCount, lastInitTimeText.data())
        // L2_LOG_ERROR(Log,
        //     L"[voting] Cannot save state for character %u (%ls): bonus time limit %d, play time %d, votes %d, bonuses %d, play-time count %d, last initialization %ls.",
        //     m_characterId, m_characterName.c_str(), bonusTimeLimit, playTime, voteCount, bonusCount, playTimeCount, lastInitTimeText.data())
        L2_LOG_ERROR(
            Log,
            u"[voting] Cannot save state for character %u (%ls): bonus time limit %d, play time %d, votes %d, bonuses %d, "
            u"play-time count %d, last initialization %ls.",
            m_characterId, m_characterName.c_str(), bonusTimeLimit, playTime, voteCount, bonusCount, playTimeCount,
            lastInitTimeText.data()
        );
        return false;
    }

    m_bonusTimeLimit = bonusTimeLimit;
    m_playTime = playTime;
    m_voteCount = voteCount;
    m_bonusCount = bonusCount;
    m_playTimeCount = playTimeCount;
    SetLastInitTime(lastInitTimeText.data());
    return true;
}

void CUser::LoadTrainingZoneInfo() {
    L2_GUARD_FUNCTION();

    int endTime = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&endTime);

    constexpr const wchar_t* query = L"{Call lin_LoadTrainingZoneEndTime(?)}";
    connection.Prepare(query);
    connection.BindParameter(&m_characterId);
    if (connection.ExecutePrepared(query) && connection.Fetch()) {
        m_trainingZoneEndTime = endTime;
    }
}

void CUser::LoadUserPledgeBonus() {
    L2_GUARD_FUNCTION();

    int userPledgeBonus = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&userPledgeBonus);

    const int activity = 0;
    constexpr const wchar_t* query = L"{Call lin_LoadUserPledgeBonus(?, ?)}";
    connection.Prepare(query);
    connection.BindParameter(&m_characterId);
    connection.BindParameter(&activity);
    if (connection.ExecutePrepared(query) && connection.Fetch()) {
        m_userPledgeBonus = userPledgeBonus;
    }
}

void CUser::SetDropExp(std::int64_t dropExperience) {
    L2_GUARD_FUNCTION();
    m_dropExperience = dropExperience;
}

void CUser::SetAP(int abilityPoints) {
    L2_GUARD_FUNCTION();
    m_abilityPoints = abilityPoints;
}

bool CUser::ReplyLoadSelectedChar(
    CSocket* socket, unsigned int userSocketId, AddtionalLocationType locationType, int x, int y, int z
) {
    // Current Cached.exe 0x005b7e18. The large single Assemble call and its
    // three independent overflow decisions are part of the wire contract.
    L2_GUARD_FUNCTION();

    const std::uint32_t nowTick = runtime::GetTickCount();
    const std::uint32_t previousTick = m_loadSelectedTick.exchange(nowTick, std::memory_order_seq_cst);
    if (nowTick - previousTick < 3000U) {
        // Log.Add(LOG_ERROR, "[User.cpp][835]Too fast Load character, char id[%u]", m_characterId)
        // Log.Add(LOG_WARN,
        //     "[character] Selection rejected for character %u: only %u ms since the previous request. Minimum interval is 3000 ms.",
        //     m_characterId, static_cast<unsigned>(nowTick - previousTick))
        L2_LOG_WARN(Log,
            "[character] Selection rejected for character %u: only %u ms since the previous request; minimum interval is 3000 ms.",
            m_characterId, static_cast<unsigned>(nowTick - previousTick));
        return false;
    }

    const int deletedElapsed = GetDeletedElapsedSecond();
    if (deletedElapsed >= -86399) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[User.cpp][843] BOT??? choose a character to wait for "
        //     "deleting, char id[%u], elapsed sec[%d]",
        //     m_characterId, deletedElapsed
        // )
        // Log.Add(LOG_WARN,
        //     "[character] Selection rejected for character %u: deletion wait check failed (elapsed %d seconds).",
        //     m_characterId, deletedElapsed)
        L2_LOG_WARN(Log,
            "[character] Selection rejected for character %u: deletion wait check failed (elapsed %d seconds).",
            m_characterId, deletedElapsed);
        return false;
    }

    const int location = static_cast<int>(locationType);
    if (location == 2 || (location == 1 && m_builder > 0)) {
        SetPos(x, y, z);
    } else if (m_trainingZoneEndTime > 0 && m_trainingZoneEndTime <= runtime::crt::Time32Now()) {
        int trainingX = 0;
        int trainingY = 0;
        int trainingZ = 0;
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Bind(&trainingX);
        connection.Bind(&trainingY);
        connection.Bind(&trainingZ);
        constexpr const wchar_t* statement = L"{Call lin_LoadTrainingZonePos(?)}";
        connection.Prepare(statement);
        connection.BindParameter(&m_characterId);
        if (connection.ExecutePrepared(statement) && connection.Fetch()) {
            SetPos(trainingX, trainingY, trainingZ);
        }
    }

    if (m_loadedInfo.status == 2) {
        // Log.Add(
        //     LOG_ERROR, "eternally banned char, name[%ls], char id[%u]", m_characterName.c_str(), m_characterId
        // )
        // Log.Add(LOG_WARN,
        //     L"[character] Selection rejected for character %u (%ls): permanently banned.",
        //     m_characterId, m_characterName.c_str())
        // L2_LOG_WARN(Log,
        //     L"[character] Selection rejected for character %u (%ls): permanently banned.",
        //     m_characterId, m_characterName.c_str())
        L2_LOG_WARN(
            Log, u"[character] Selection rejected for character %u (%ls): permanently banned.", m_characterId,
            m_characterName.c_str()
        );
        return false;
    }
    if (m_loadedInfo.status == 1) {
        const std::int32_t loggedTime = runtime::crt::Time32Now();
        // Noise: The ban decision below reports rejection or expiry. The preliminary timestamp row duplicates that context.
        // Log.Add(
        //     LOG_NORMAL, "ban time status:%d, nBanTime:%d, time_l2(0):%d", 1, m_loadedInfo.banEnd, loggedTime
        // )
        if (runtime::crt::Time32Now() < m_loadedInfo.banEnd) {
            // Log.Add(
            //     LOG_ERROR, "banned char, name[%ls], char id[%u]", m_characterName.c_str(), m_characterId
            // )
            // Log.Add(LOG_WARN,
            //     L"[character] Selection rejected for character %u (%ls): temporary ban ends at %d (Unix seconds. Checked at %d).",
            //     m_characterId, m_characterName.c_str(), m_loadedInfo.banEnd, loggedTime)
            // L2_LOG_WARN(Log,
            //     L"[character] Selection rejected for character %u (%ls): temporary ban ends at %d (Unix seconds. Checked at %d).",
            //     m_characterId, m_characterName.c_str(), m_loadedInfo.banEnd, loggedTime)
            L2_LOG_WARN(
                Log,
                u"[character] Selection rejected for character %u (%ls): temporary ban ends at %d (Unix seconds; checked at "
                u"%d).",
                m_characterId, m_characterName.c_str(), m_loadedInfo.banEnd, loggedTime
            );
            return false;
        }

        // Log.Add(LOG_NORMAL,
        //     "[character] Temporary ban expired for character %u (end %d, checked at %d Unix seconds). Clearing cached ban and requesting a database update.",
        //     m_characterId, m_loadedInfo.banEnd, loggedTime)
        L2_LOG_INFO(Log,
            "[character] Temporary ban expired for character %u (end %d, checked at %d Unix seconds); clearing cached ban and requesting a database update.",
            m_characterId, m_loadedInfo.banEnd, loggedTime);
        m_loadedInfo.status = 0;
        m_loadedInfo.banEnd = 0;
        DBConn connection(runtime::process::CurrentDatabaseId());
        const int zero = 0;
        constexpr const wchar_t* statement = L"{Call lin_SetUserBan (?, ?, ?, ? )}";
        connection.Prepare(statement);
        connection.BindParameter(&m_characterId);
        connection.BindParameter(&zero);
        connection.BindParameter(&zero);
        connection.BindParameter(&zero);
        connection.ExecutePrepared(statement);
    }

    CWareHouseSP inventory = GetWareHouse(0, true);
    if (!inventory)
        return false;

    std::array<int, 60> objectIds{};
    std::memcpy(objectIds.data(), &m_equipment, sizeof(m_equipment));
    std::array<CItemSP, 60> items{};
    {
        L2_READ_GUARD(*inventory.operator->());
        for (std::size_t index = 0; index < items.size(); ++index) {
            items[index] = inventory->GetItem(objectIds[index]);
        }
    }

    std::array<int, 60> itemTypes{};
    for (std::size_t index = 0; index < items.size(); ++index) {
        itemTypes[index] = items[index] ? static_cast<int>(items[index]->ItemType()) : 0;
    }

    SYSTEMTIME lastLogout = m_lastLogoutTime;
    std::uint64_t lastLogoutFirstHalf = 0;
    std::memcpy(&lastLogoutFirstHalf, &lastLogout, sizeof(lastLogoutFirstHalf));
    if (lastLogoutFirstHalf == 0) {
        lastLogout = {};
    }

    const std::int32_t accountLastLogoutTime = GetAccountLastLogoutTime();
    CPacketHelperEx<20480> vitality;
    AssembleVitality(vitality);
    const runtime::SystemTime localTime = runtime::GetLocalTimeNow();

    SYSTEMTIME* const socialityLastCheck = GetSocialityLastCheck();
    const int usedSuffrageCount = GetUsedSulffrageCount();
    const int sociality = GetSociality();

    const std::u16string characterName = std::u16string(m_characterName.c_str());
    const std::u16string accountName = std::u16string(m_accountName.c_str());
    const std::u16string nickname = std::u16string(m_nickname.c_str());

    std::array<char, 0x5000> response{};
    constexpr const char* format = "SdSdddddddddddffQQdddddddddddddddddddddddddddddddddddddddddd"
                                   "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
                                   "ddddddddddddddddddddddddddffSQddddddddbdddddddddffdbdddddddd"
                                   "ddddddddddddddbdddddcd";
    int responseLength = 0;
    const int assembled = ::Assemble(
        response.data(), static_cast<int>(response.size()), format, characterName.c_str(), static_cast<int>(m_characterId),
        accountName.c_str(), m_accountId, static_cast<int>(m_pledgeId), m_builder, m_gender, m_race, m_characterClass,
        m_worldId, m_x, m_y, m_z, m_inVehicle ? 1 : 0, m_hp, m_mp, m_sp, m_experience, m_level, m_alignment,
        static_cast<int>(m_pkCount), static_cast<int>(m_duelCount), static_cast<int>(m_pkPardon), objectIds[0], objectIds[1],
        objectIds[2], objectIds[3], objectIds[4], objectIds[5], objectIds[6], objectIds[7], objectIds[8], objectIds[9],
        objectIds[10], objectIds[11], objectIds[12], objectIds[13], objectIds[14], objectIds[15], objectIds[16],
        objectIds[17], objectIds[18], objectIds[19], objectIds[20], objectIds[21], objectIds[22], objectIds[23],
        objectIds[24], objectIds[25], objectIds[26], objectIds[27], objectIds[28], objectIds[29], objectIds[30],
        objectIds[31], objectIds[32], objectIds[33], objectIds[34], objectIds[35], objectIds[36], objectIds[37],
        objectIds[38], objectIds[39], objectIds[40], objectIds[41], objectIds[42], objectIds[43], objectIds[44],
        objectIds[45], objectIds[46], objectIds[47], objectIds[48], objectIds[49], objectIds[50], objectIds[51],
        objectIds[52], objectIds[53], objectIds[54], objectIds[55], objectIds[56], objectIds[57], objectIds[58],
        objectIds[59], itemTypes[0], itemTypes[1], itemTypes[2], itemTypes[3], itemTypes[4], itemTypes[5], itemTypes[6],
        itemTypes[7], itemTypes[8], itemTypes[9], itemTypes[10], itemTypes[11], itemTypes[12], itemTypes[13], itemTypes[14],
        itemTypes[15], itemTypes[16], itemTypes[17], itemTypes[18], itemTypes[19], itemTypes[20], itemTypes[21],
        itemTypes[22], itemTypes[23], itemTypes[24], itemTypes[25], itemTypes[26], itemTypes[27], itemTypes[28],
        itemTypes[29], itemTypes[30], itemTypes[31], itemTypes[32], itemTypes[33], itemTypes[34], itemTypes[35],
        itemTypes[36], itemTypes[37], itemTypes[38], itemTypes[39], itemTypes[40], itemTypes[41], itemTypes[42],
        itemTypes[43], itemTypes[44], itemTypes[45], itemTypes[46], itemTypes[47], itemTypes[48], itemTypes[49],
        itemTypes[50], itemTypes[51], itemTypes[52], itemTypes[53], itemTypes[54], itemTypes[55], itemTypes[56],
        itemTypes[57], itemTypes[58], itemTypes[59],
        // Current Cached.exe CUser::ReplyLoadSelectedChar prepares these
        // three arguments at 0x005ba068..0x005ba088 from target CUser
        // +0x430, +0x42c and +0x428. The portable members matching
        // those physical owners are hair color, hair shape and face. Keep
        // this non-semantic wire order: CSocket::SaveCharacter consumes the
        // same positions and CUser::SetChanged maps them back to their
        // semantic members.
        m_hairColor, m_hairShape, m_face, GetMaxHP(), GetMaxMP(),
        nickname.c_str(), m_dropExperience, static_cast<int>(lastLogout.wYear), static_cast<int>(lastLogout.wMonth),
        static_cast<int>(lastLogout.wDay), static_cast<int>(lastLogout.wHour), static_cast<int>(lastLogout.wMinute),
        static_cast<int>(lastLogout.wSecond), sociality, usedSuffrageCount, static_cast<int>(sizeof(*socialityLastCheck)),
        socialityLastCheck, m_henna[0], m_henna[1], m_henna[2], m_subjobId, m_subjobClass[0], m_subjobClass[1],
        m_subjobClass[2], m_subjobClass[3], m_dualClassId, m_cp, GetMaxCP(), m_ssqDawnRound,
        static_cast<int>(m_powerFlags.size()), m_powerFlags.data(), static_cast<int>(m_pledgeDismissTime),
        static_cast<int>(m_pledgeOustedTime), static_cast<int>(m_pledgeWithdrawTime), m_characterNameColor, m_nicknameColor,
        m_pledgeType, m_gradeId, m_academyPledgeId, static_cast<int>(m_loadedInfo.associatedInzone),
        static_cast<int>(localTime.wYear), static_cast<int>(localTime.wMonth), static_cast<int>(localTime.wDay),
        static_cast<int>(localTime.wHour), static_cast<int>(localTime.wMinute), static_cast<int>(localTime.wSecond),
        m_loadedInfo.associatedInzoneUid, m_loadedInfo.bookmarkSlot, m_bonusCount, m_playTimeCount, m_voteCount, m_playTime,
        m_bonusTimeLimit, static_cast<int>(sizeof(m_lastVoteInitTime)), &m_lastVoteInitTime, accountLastLogoutTime,
        m_loadedInfo.jobGroup, m_faceIndexEx, m_hairShapeIndexEx, m_hairColorIndexEx, m_hairAccessoryFlag ? 1 : 0,
        m_abilityPoints
    );
    if (assembled < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[User.cpp][1346] Buffer overflow in "
        //     "LoadSelectedCharacter char id[%u]",
        //     m_characterId
        // )
        // Log.Add(LOG_ERROR,
        //     "[character] Cannot assemble base selection data for character %u: result %d, buffer capacity %zu bytes. Reply construction continues.",
        //     m_characterId, assembled, response.size())
        L2_LOG_ERROR(Log,
            "[character] Cannot assemble base selection data for character %u: result %d, buffer capacity %zu bytes; reply construction continues.",
            m_characterId, assembled, response.size());
    } else {
        responseLength = assembled;
    }

    const int vitalitySize = ::Assemble(
        response.data() + responseLength, static_cast<int>(response.size()) - responseLength, "b", vitality.GetLength(),
        vitality.GetPacket()
    );
    if (vitalitySize < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[User.cpp][1357] Buffer overflow in "
        //     "LoadSelectedCharacter char id[%u]",
        //     m_characterId
        // )
        // Log.Add(LOG_ERROR,
        //     "[character] Cannot append vitality data to the selection reply for character %u: result %d, remaining capacity %d bytes. Reply construction continues.",
        //     m_characterId, vitalitySize, static_cast<int>(response.size()) - responseLength)
        L2_LOG_ERROR(Log,
            "[character] Cannot append vitality data to the selection reply for character %u: result %d, remaining capacity %d bytes; reply construction continues.",
            m_characterId, vitalitySize, static_cast<int>(response.size()) - responseLength);
    } else {
        responseLength += vitalitySize;
    }

    const int ssqSize =
        AssembleSSQInfo(response.data() + responseLength, static_cast<int>(response.size()) - responseLength);
    if (ssqSize < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[User.cpp][1368] Buffer overflow in "
        //     "LoadSelectedCharacter char id[%u]",
        //     m_characterId
        // )
        // Log.Add(LOG_ERROR,
        //     "[character] Cannot append Seven Signs data to the selection reply for character %u: result %d, remaining capacity %d bytes. The assembled prefix will still be sent.",
        //     m_characterId, ssqSize, static_cast<int>(response.size()) - responseLength)
        L2_LOG_ERROR(Log,
            "[character] Cannot append Seven Signs data to the selection reply for character %u: result %d, remaining capacity %d bytes; the assembled prefix will still be sent.",
            m_characterId, ssqSize, static_cast<int>(response.size()) - responseLength);
    } else {
        responseLength += ssqSize;
    }

    socket->Send("cdb", 0x2d, static_cast<int>(userSocketId), responseLength, response.data());
    return true;
}
