#include "Pet.h"
#include "Area.h"
#include "CConfig.h"
#include "CContributeData.h"
#include "CWorld.h"
#include "CursedWeapon.h"
#include "DB.h"
#include "Item.h"
#include "ItemAssembler.h"
#include "NpcServer.h"
#include "ObjectDB.h"
#include "PVPMatch.h"
#include "Party.h"
#include "PetDataSheet.h"
#include "Portable/ProcessRuntime.h"
#include "Protocol/src/NPCProtocol.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/CategoryDataDB.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/SystemMail.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/Utils.h"
#include "Shared/src/platform/Runtime.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserSocket.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <optional>
#include <string>

namespace {


double ClampSummonSpeed(double value, double maximum) noexcept {
    double clamped = value;
    if (maximum <= clamped) {
        clamped = maximum;
    }
    if (!(0.0 <= clamped)) {
        clamped = 0.0;
    }
    return clamped;
}

constexpr std::array<double, 12> kPetArmorEnchantBonus{
    0.0,
    1.0,
    1.0,
    1.0,
    1.0,
    1.0,
    1.0,
    1.0,
    1.0,
    1.0,
    1.0,
    0.0,
};

void CopyPetNameToTarget(const char16_t* source, char16_t* destination, std::size_t capacity) {
    if (destination == nullptr || capacity == 0) {
        return;
    }
    std::fill(destination, destination + capacity, u'\0');
    if (source == nullptr) {
        return;
    }
    const std::u16string_view converted(source);
    const std::size_t count = std::min(converted.size(), capacity - 1);
    std::copy_n(converted.begin(), count, destination);
}


void CopyPetAuditWide25(std::array<wchar_t, 25>& destination, const char16_t* const source) {
    destination.fill(L'\0');
    if (source == nullptr) {
        runtime::process::HandleInvalidParameter(L"wcscpy_s");
        return;
    }

    std::size_t length = 0;
    while (length < destination.size() && source[length] != u'\0') {
        ++length;
    }
    if (length >= destination.size()) {
        runtime::process::HandleInvalidParameter(L"wcscpy_s");
        return;
    }
    const std::wstring host = Utf16ToWide(std::u16string_view(source, length));
    std::copy(host.begin(), host.end(), destination.begin());
}

} // namespace

CPetManager g_PetManager;

std::int32_t CSummon::s_MultiSummonCategoryID = -1;
std::int32_t CSummon::s_ControlCategoryID = -1;
std::int32_t CSummon::s_MultiSummonTwoPointCategoryID = -1;
std::int32_t CPet::s_StriderCategoryID = -1;
std::int32_t CPet::s_WolfCategoryID = -1;
std::int32_t CPet::s_WyvernCategoryID = -1;

CSummon::CSummon() :
    m_MasterWorldID(0),
    m_MasterUserDBID(0),
    m_MasterUserSID(0),
    m_InvalidMasterTick(runtime::GetTickCount()),
    m_LifeTimeTick(0),
    m_nConsumeItemClassID(0),
    m_nConsumeItemCount(0),
    m_nConsumedCount(0),
    m_petIndex(0),
    m_nOrgDuration(0),
    m_betrayOwnMaster(false),
    m_bMultiSummon(false),
    m_Controlable(true),
    m_nSummonPoint(0),
    m_nSpelledInfoChanged(0),
    m_nIndexForMaster(0),
    m_SummonDBSaveTick(0) {
    L2_GUARD_FUNCTION();
}

CSummon::CSummon(const CSummon& other) :
    CNPC(other),
    m_MasterUserDBID(0),
    m_MasterUserSID(0),
    m_InvalidMasterTick(runtime::GetTickCount()),
    m_LifeTimeTick(0),
    m_petIndex(other.m_petIndex),
    m_betrayOwnMaster(other.m_betrayOwnMaster),
    m_bMultiSummon(other.m_bMultiSummon),
    m_Controlable(other.m_Controlable),
    m_nSummonPoint(other.m_nSummonPoint),
    m_nIndexForMaster(0),
    m_SummonDBSaveTick(0) {
    L2_GUARD_FUNCTION();
    CheckMultiSummonCategory();
    CheckControlableCategory();
    CheckMultiSummonPoint();
}

CSummon::~CSummon() = default;

CPet::CPet() :
    m_bZolaHungry(false),
    m_MealTick(runtime::GetTickCount()),
    m_DBSaveTick(0),
    m_bFirstTime(true),
    m_SysMsgTick(0),
    m_nSlots{},
    m_ZolaHungryTerm(0),
    m_rideType(RIDE_NONE),
    m_bRestartDead(false),
    m_bItemDroppedByHungry(false),
    m_nSaveToDBTick(0),
    m_pPetSheet(nullptr),
    m_pPetData(nullptr) {
    m_pPetSheet = PetDataSheetTable::Inst().GetPetDataSheetByIndex(m_petIndex);
    m_pPetData = m_pPetSheet->GetRecord(m_data->m_nLevel);
}

CPet::CPet(const CPet& other) :
    CSummon(other),
    m_bZolaHungry(false),
    m_MealTick(runtime::GetTickCount()),
    m_DBSaveTick(0),
    m_bFirstTime(true),
    m_SysMsgTick(0),
    m_nSlots{other.m_nSlots[0], other.m_nSlots[1], other.m_nSlots[2]},
    m_ZolaHungryTerm(0),
    m_nSaveToDBTick(0),
    m_pPetSheet(nullptr),
    m_pPetData(nullptr) {
    m_pPetSheet = PetDataSheetTable::Inst().GetPetDataSheetByIndex(m_petIndex);
    m_pPetData = m_pPetSheet->GetRecord(m_data->m_nLevel);
    CheckCategory();
}

CPet::~CPet() = default;

CPetManager::CPetManager() {
    // Current L2Server 0x0095f92c.
    L2_GUARD_FUNCTION();
    PreCalc();
}

void CPetManager::PreCalc() {
    // Current L2Server 0x0095bc1c. The target initializes levels 0..120.
    // the final [121] cells keep their zero-initialized process value.
    L2_GUARD_FUNCTION();
    for (int level = 0; level <= 120; ++level) {
        m_petExpPenalty[0][level] = level < 6 ? 1.0 : std::pow(1.1, static_cast<double>(std::min(level - 5, 20)));
        m_petExpPenalty[1][level] = level < 11 ? 1.0 : std::pow(1.05, static_cast<double>(std::min(level - 10, 20)));
    }
}

double CPetManager::MasterExpRatio(int baseRatio, int masterLevel, int summonLevel) {
    // Current L2Server 0x00958a34.
    L2_GUARD_FUNCTION();
    if (baseRatio == 100) {
        return 0.0;
    }

    const int levelDifference = masterLevel - summonLevel;
    const double adjustedRatio = levelDifference > 0 ? static_cast<double>(baseRatio) / m_petExpPenalty[0][levelDifference]
                                                     : static_cast<double>(baseRatio) * m_petExpPenalty[1][-levelDifference];
    const double result = 100.0 - adjustedRatio;
    if (!(result > 0.0)) {
        return 0.0;
    }
    return result >= 100.0 ? 100.0 : result;
}

double CPetManager::SummonExpRatio(int baseRatio, int masterLevel, int summonLevel) {
    // Current L2Server 0x00958b4c.
    L2_GUARD_FUNCTION();
    if (baseRatio == 100) {
        return 100.0;
    }

    const int levelDifference = masterLevel - summonLevel;
    const double result = levelDifference > 0 ? static_cast<double>(baseRatio) / m_petExpPenalty[0][levelDifference]
                                              : static_cast<double>(baseRatio) * m_petExpPenalty[1][-levelDifference];
    if (!(result > 0.0)) {
        return 0.0;
    }
    return result >= 100.0 ? 100.0 : result;
}

double CPetManager::GetExpPenalty(int petIndex, int level) {
    // Current L2Server 0x00957c28. The subtraction and conversion are kept
    // in the same order as the target's cvtsi2sd path.
    L2_GUARD_FUNCTION();
    double percentage = 10.0 - static_cast<double>(level - 1) * 0.125;
    if (percentage <= 2.0) {
        percentage = 2.0;
    }
    PetDataSheet* const sheet = PetDataSheetTable::Inst().GetPetDataSheetByIndex(petIndex);
    const std::int64_t current = sheet->GetRecord(level)->mExp;
    const std::int64_t previous = sheet->GetRecord(level - 1)->mExp;
    return 0.0 - static_cast<double>(current - previous) * (percentage / 100.0);
}

bool CPetManager::IsSummonedPetCollar(std::int32_t itemClassId) {
    // Current L2Server 0x0095de24.
    L2_GUARD_FUNCTION();
    const bool found = [&] {
        L2_GUARD(m_Sect);
        return m_PetCollarDB.find(itemClassId) != m_PetCollarDB.end();
    }();
    return found;
}

bool CPetManager::LinkPet(User* user, CPet* pet) {
    // Current L2Server 0x0095bdec.
    L2_GUARD_FUNCTION();
    pet->SetMaster(user);
    user->m_data->m_pPet = pet;
    user->m_PetSID = pet->m_nId;
    pet->m_data->m_SummonType = NST_PET;
    pet->SetInZoneID(user->GetInZoneID());

    CItem* const collar = user->m_inv.GetByDBID(pet->GetDBID());
    if (collar != nullptr && collar->IsEtcItem()) {
        collar->CastEtcItem()->m_bPetCollarTradable = false;
    }
    return true;
}

bool CPetManager::CreatePet(User* user, CPet* pet) {
    // Current L2Server 0x0095f5f8.
    L2_GUARD_FUNCTION();

    const std::uint32_t userDatabaseId = user->GetDBID();
    const std::uint32_t userServerId = user->Id();
    const std::int32_t collarDatabaseId = pet == nullptr ? 0 : static_cast<std::int32_t>(pet->m_CollarDBID);

    {
        L2_GUARD(m_Sect);
        const auto existing = m_UserPetDataDB.find(userDatabaseId);
        if (existing != m_UserPetDataDB.end()) {
            CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(existing->second.PetSID);
            if (pet == nullptr && creature != nullptr && creature->IsPet() &&
                creature->CastPet()->m_MasterUserDBID == userDatabaseId) {
                LinkPet(user, creature->CastPet());
                return false;
            }
            _DeletePet(userDatabaseId, userServerId);
        }

        if (pet != nullptr) {
            m_UserPetDataDB[userDatabaseId] = PetStruct{static_cast<std::uint32_t>(collarDatabaseId), pet->Id()};
            m_PetCollarDB.insert(collarDatabaseId);
            LinkPet(user, pet);
        }
    }
    return pet != nullptr;
}

void CPetManager::DeletePet(std::uint32_t userDatabaseId, std::uint32_t userServerId) {
    // Current L2Server 0x0095f850.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Sect);
        _DeletePet(userDatabaseId, userServerId);
    }
}

void CPetManager::_DeletePet(const std::uint32_t userDatabaseId, const std::uint32_t userServerId) {
    // Current L2Server 0x0095ec08. The caller owns m_Sect.
    L2_GUARD_FUNCTION();
    const auto position = m_UserPetDataDB.find(userDatabaseId);
    if (position == m_UserPetDataDB.end()) {
        return;
    }

    const std::int32_t collarDatabaseId = static_cast<std::int32_t>(position->second.PetDBID);
    CCreature* const pet = g_CreatureSmartIdManager.GetObjectW(position->second.PetSID);
    if (pet != nullptr) {
        pet->Delete();
    }

    CCreature* const userCreature = g_CreatureSmartIdManager.GetObjectW(userServerId);
    if (userCreature != nullptr) {
        userCreature->m_data->m_pPet.id = -1;
        userCreature->m_data->m_pPet.index = -1;
    }

    m_UserPetDataDB.erase(position);
    m_PetCollarDB.erase(collarDatabaseId);
}

void CPetManager::DeleteAll() {
    // Current L2Server 0x0095ed60.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Sect);

        auto position = m_UserPetDataDB.begin();
        while (position != m_UserPetDataDB.end()) {
            // The target increments its tree iterator after _DeletePet may erase
            // the current node. Capture the already-established successor so the
            // portable host tree keeps the same traversal without dereferencing
            // a freed implementation node.
            const auto current = position++;
            CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(current->second.PetSID);
            if (creature == nullptr || !creature->IsPet()) {
                continue;
            }

            User* const master = creature->CastPet()->GetMaster();
            if (master == nullptr) {
                _DeletePet(creature->CastPet()->m_MasterUserDBID, creature->CastPet()->m_MasterUserSID);
            } else {
                master->DespawnPet(true, true, DESPAWN_PET_NONE, false);
            }
        }

        // Log.Add(
        //     LOG_NORMAL,
        //     "Count of removed pet=[%d]",
        //     m_UserPetDataDB.size())
        // L2_LOG_INFO(Log,
        //     "Count of removed pet=[%d]",
        //     m_UserPetDataDB.size())
        L2_LOG_INFO(Log, "Pet cleanup pass finished; %zu owner records remain.", m_UserPetDataDB.size());
        m_PetCollarDB.clear();
    }
}

void CSummon::InitSummon() {
    L2_GUARD_FUNCTION();
    s_ControlCategoryID = static_cast<std::int32_t>(g_PrecompiledHeaderDB.Search(u"uncontrollable_summon"));
    s_MultiSummonTwoPointCategoryID = static_cast<std::int32_t>(g_PrecompiledHeaderDB.Search(u"2point_multi_summon"));
}

void CSummon::CheckControlableCategory() {
    L2_GUARD_FUNCTION();
    if (s_ControlCategoryID != -1) {
        m_Controlable = !CCategoryDataDB::Inst().IsInCategory(m_data->m_nClassID, s_ControlCategoryID);
    }
}

void CSummon::CheckMultiSummonCategory() {
    L2_GUARD_FUNCTION();
    if (s_MultiSummonCategoryID != -1) {
        m_bMultiSummon = CCategoryDataDB::Inst().IsInCategory(m_data->m_nClassID, s_MultiSummonCategoryID);
    }
}

int CSummon::CheckMultiSummonPoint(std::int32_t npcClassId) {
    L2_GUARD_FUNCTION();
    if (s_MultiSummonTwoPointCategoryID == -1) {
        return 0;
    }
    return CCategoryDataDB::Inst().IsInCategory(npcClassId, s_MultiSummonTwoPointCategoryID) ? 2 : 0;
}

void CSummon::CheckMultiSummonPoint() {
    L2_GUARD_FUNCTION();
    m_nSummonPoint = CheckMultiSummonPoint(m_data->m_nClassID);
}

void CSummon::DeleteInfectedSkills(AtomicJob* atomicJob) {
    L2_GUARD_FUNCTION();
    User* const master = GetMaster();
    if (master == nullptr) {
        return;
    }
    const int summonIndex = IsPet() ? static_cast<int>(GetDBID()) : master->GetSummonIndex(static_cast<int>(Id()));
    db.SaveSummonInfectedSkills(*master, m_data->m_nClassID, summonIndex, atomicJob, 0, 0, nullptr);
}

void CSummon::_DeleteInfectedSkills() {
    L2_GUARD_FUNCTION();
    User* const master = GetMaster();
    if (master == nullptr) {
        return;
    }
    const int summonIndex = IsPet() ? static_cast<int>(GetDBID()) : master->_GetSummonIndex(static_cast<int>(Id()));
    db.SaveSummonInfectedSkills(*master, m_data->m_nClassID, summonIndex, nullptr, 0, 0, nullptr);
}

int CSummon::SaveInfectedSkills(AtomicJob* atomicJob) {
    L2_GUARD_FUNCTION();
    std::vector<InfectedSkillType> infectedSkills;
    GetInfectedSkillsToDBSave(infectedSkills);
    CPacketHelperEx<20480> packet;
    PackingInfectedSkillsToDBSave(infectedSkills, packet);

    User* const master = GetMaster();
    if (master == nullptr) {
        return -1;
    }
    const int summonIndex = IsPet() ? static_cast<int>(GetDBID()) : master->GetSummonIndex(static_cast<int>(Id()));
    const int skillCount = static_cast<int>(infectedSkills.size());
    db.SaveSummonInfectedSkills(
        *master, m_data->m_nClassID, summonIndex, atomicJob, skillCount, packet.GetLength(), packet.GetPacket()
    );
    return skillCount;
}

int CSummon::_SaveInfectedSkills() {
    L2_GUARD_FUNCTION();
    std::vector<InfectedSkillType> infectedSkills;
    GetInfectedSkillsToDBSave(infectedSkills);
    CPacketHelperEx<20480> packet;
    PackingInfectedSkillsToDBSave(infectedSkills, packet);

    User* const master = GetMaster();
    if (master == nullptr) {
        return -1;
    }
    const int summonIndex = IsPet() ? static_cast<int>(GetDBID()) : master->_GetSummonIndex(static_cast<int>(Id()));
    const int skillCount = static_cast<int>(infectedSkills.size());
    db.SaveSummonInfectedSkills(
        *master, m_data->m_nClassID, summonIndex, nullptr, skillCount, packet.GetLength(), packet.GetPacket()
    );
    return skillCount;
}

void CPet::InitPet() {
    L2_GUARD_FUNCTION();

    s_StriderCategoryID = static_cast<std::int32_t>(g_PrecompiledHeaderDB.Search(u"strider"));
    if (s_StriderCategoryID < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d cannot find strider category",
        //     L"pet.cpp",
        //     0x641)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d cannot find strider category",
        //     L"pet.cpp",
        //     0x641)
        L2_LOG_ERROR(
            Log, "Pet initialization: category 'strider' is missing from the precompiled header; initialization continues."
        );
    }

    s_WolfCategoryID = static_cast<std::int32_t>(g_PrecompiledHeaderDB.Search(u"wolf_group"));
    if (s_WolfCategoryID < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d cannot find wolf category",
        //     L"pet.cpp",
        //     0x648)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d cannot find wolf category",
        //     L"pet.cpp",
        //     0x648)
        L2_LOG_ERROR(
            Log,
            "Pet initialization: category 'wolf_group' is missing from the precompiled header; initialization continues."
        );
    }

    s_WyvernCategoryID = static_cast<std::int32_t>(g_PrecompiledHeaderDB.Search(u"wyvern_group"));
    if (s_WyvernCategoryID < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d cannot find wyvern category",
        //     L"pet.cpp",
        //     0x64f)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d cannot find wyvern category",
        //     L"pet.cpp",
        //     0x64f)
        L2_LOG_ERROR(
            Log,
            "Pet initialization: category 'wyvern_group' is missing from the precompiled header; initialization continues."
        );
    }
}

void CPet::CheckCategory() {
    L2_GUARD_FUNCTION();

    if (CCategoryDataDB::Inst().IsInCategory(m_data->m_nClassID, s_StriderCategoryID)) {
        m_rideType = RIDE_STRIDER;
    } else if (CCategoryDataDB::Inst().IsInCategory(m_data->m_nClassID, s_WolfCategoryID)) {
        m_rideType = RIDE_WOLF;
    } else if (CCategoryDataDB::Inst().IsInCategory(m_data->m_nClassID, s_WyvernCategoryID)) {
        m_rideType = RIDE_WYVERN;
    }
}

void CPet::SetPetIndex(std::int32_t petIndex) {
    m_petIndex = petIndex;
    m_pPetSheet = PetDataSheetTable::Inst().GetPetDataSheetByIndex(m_petIndex);
    m_pPetData = m_pPetSheet->GetRecord(m_data->m_nLevel);
}

bool CPet::GetPetData(const int npcClassId, int& level, std::int64_t& experience, int& meal) {
    // Current L2Server 0x00956010.
    L2_GUARD_FUNCTION();
    CObject* const object = g_ObjectDB.GetObjectW(npcClassId, false);
    if (object != nullptr && object->IsPet()) {
        CPet* const pet = object->CastPet();
        if (level < 1) {
            level = pet->m_data->m_nLevel;
        }
        PetDataSheet* const sheet = PetDataSheetTable::Inst().GetPetDataSheetByIndex(pet->m_petIndex);
        PetDataSheet::Record* const record = sheet->GetRecord(level);
        experience = record->mExp;
        meal = record->mMaxMeal;
        return true;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] CPet::CreatePet failed. invalid npc class[%d]",
    //     L"pet.cpp",
    //     0x7e3,
    //     npcClassId)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] CPet::CreatePet failed. invalid npc class[%d]",
    //     L"pet.cpp",
    //     0x7e3,
    //     npcClassId)
    L2_LOG_ERROR(Log, "Cannot initialize pet level and experience: NPC class %d is missing or is not a pet.", npcClassId);
    return false;
}

bool CPet::CreatePet(User* const user, const int itemClassId, const int npcClassId, int level) {
    // Current L2Server 0x0095a524.
    L2_GUARD_FUNCTION();
    std::int64_t experience = 0;
    int meal = 0;
    if (!GetPetData(npcClassId, level, experience, meal)) {
        return false;
    }

    db.CreatePet(*user, itemClassId, npcClassId, experience, level, 0.0, 0.0, meal);
    return true;
}

bool CPet::CreatePetForAdmin(const int userDatabaseId, const int itemClassId, const int npcClassId, int level) {
    // Current L2Server 0x0095a644.
    L2_GUARD_FUNCTION();
    std::int64_t experience = 0;
    int meal = 0;
    if (!GetPetData(npcClassId, level, experience, meal)) {
        return false;
    }

    db.CreatePetForAdmin(userDatabaseId, itemClassId, npcClassId, experience, level, 0.0, 0.0, meal);
    return true;
}

bool CPet::DestroyPet(User* const user, CEtcItem* const petCollar) {
    L2_GUARD_FUNCTION();

    if (user != nullptr && petCollar != nullptr) {
        if (!petCollar->IsPetCollarItem()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] DestroyPet failed. Is not pet collar. Pet collar DBID[%d]",
            //     L"pet.cpp",
            //     0x802,
            //     petCollar->GetDBID())
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] DestroyPet failed. Is not pet collar. Pet collar DBID[%d]",
            //     L"pet.cpp",
            //     0x802,
            //     petCollar->GetDBID())
            L2_LOG_ERROR(
                Log, "Cannot destroy pet for character %u: item %u is not a pet collar.",
                static_cast<unsigned>(user->GetDBID()), static_cast<unsigned>(petCollar->GetDBID())
            );
        } else if (!petCollar->m_bPetCollarTradable) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] DestroyPet failed. Pet is summoned. Pet collar DBID[%d]",
            //     L"pet.cpp",
            //     0x80b,
            //     petCollar->GetDBID())
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] DestroyPet failed. Pet is summoned. Pet collar DBID[%d]",
            //     L"pet.cpp",
            //     0x80b,
            //     petCollar->GetDBID())
            L2_LOG_WARN(
                Log, "Cannot destroy pet for character %u: collar %u is marked non-tradable.",
                static_cast<unsigned>(user->GetDBID()), static_cast<unsigned>(petCollar->GetDBID())
            );
        } else {
            db.DeletePet(
                user->m_data->GetWorldId(), static_cast<int>(user->Id()), static_cast<int>(user->GetDBID()),
                static_cast<int>(petCollar->GetDBID()), 3, petCollar->m_data->GetEnchanted()
            );
        }
    }

    return false;
}

void CPet::ResetParameter() {
    L2_GUARD_FUNCTION();

    m_data->m_nOrgParam[2] = runtime::Win64CvttDoubleToInt32(m_pPetData->mdOrgPAttack);
    m_data->m_nOrgParam[0] = runtime::Win64CvttDoubleToInt32(m_pPetData->mdOrgPDefend);
    m_data->m_nOrgParam[3] = runtime::Win64CvttDoubleToInt32(m_pPetData->mdOrgMAttack);
    m_data->m_nOrgParam[1] = runtime::Win64CvttDoubleToInt32(m_pPetData->mdOrgMDefend);
    m_data->m_dOrgHP = m_pPetData->mdOrgHP;
    m_data->m_dOrgMP = m_pPetData->mdOrgMP;
    m_data->m_dOrgHPRegen = m_pPetData->mdOrgHPRegen;
    m_data->m_dOrgMPRegen = m_pPetData->mdOrgMPRegen;
}

void CPet::InitObject() {
    L2_GUARD_FUNCTION();

    const char16_t* const name = GetName();
#if WCHAR_MAX <= 0xffff
    const char16_t* const targetName = reinterpret_cast<const char16_t*>(name);
#else
    const std::u16string targetNameStorage = std::u16string(name);
    const char16_t* const targetName = targetNameStorage.c_str();
#endif

    PetDataSheetTable& table = PetDataSheetTable::Inst();
    PetDataSheet* sheet = table.FindDataSheet(targetName);
    if (sheet == nullptr) {
        sheet = table.GetPetDataSheetByIndex(0);
    }
    SetPetIndex(sheet->Index());
    ResetParameter();
}

bool CSummon::IsAlive() {
    // Current L2Server 0x00954ad8.
    L2_GUARD_FUNCTION();
    if (!m_data->m_bIsActive || m_data->m_bIsAlive == 0) {
        return false;
    }

    User* const master = GetMaster();
    if (master != nullptr && master->IsOlympiadPlayer()) {
        return master->IsAliveInOlympiad();
    }
    return true;
}

bool CSummon::IsSummon() const { return true; }

bool CSummon::IsSummonOrPet() const { return true; }

bool CSummon::IsNewSummon() const {
    // Current L2Server 0x0095566c.
    return m_data->m_dHP == 0.0 && m_data->m_dMP == 0.0;
}

bool CSummon::IsPCOrSummon() const { return true; }

bool CSummon::IsPCOrPCRelatedNPC() const { return true; }

CSummon* CSummon::CastSummon() { return this; }

CObject* CSummon::Copy() {
    L2_GUARD_FUNCTION();
    CSummon* const summon = new CSummon(*this);
    summon->OnCreate();
    return summon;
}

User* CSummon::GetUserOrMaster() {
    L2_GUARD_FUNCTION();
    return GetMaster();
}

User* CSummon::GetMaster() const {
    L2_GUARD_FUNCTION();
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(m_MasterUserSID);
    if (creature && creature->IsPC()) {
        return creature->CastUser();
    }
    return nullptr;
}

void CSummon::SetMaster(User* master) {
    L2_GUARD_FUNCTION();
    m_MasterWorldID = master->m_data->GetWorldId();
    m_MasterUserDBID = master->GetDBID();
    m_MasterUserSID = master->m_nId;
    CNPC::SetMaster(master);
}

bool CSummon::CanBeSeenBy(CObject& object) {
    // Current L2Server 0x00954104.
    L2_GUARD_FUNCTION();
    if (IsVisible()) {
        return true;
    }
    User* const master = GetMaster();
    return master != nullptr && master->CanBeSeenBy(object);
}

bool CSummon::IsRealEnemyTo(CCreature* creature) {
    // Current L2Server 0x00954530.
    L2_GUARD_FUNCTION();
    if (User* const master = GetMaster()) {
        return master->IsRealEnemyTo(creature);
    }
    return IsGuilty() || m_data->m_nInBattleField < 0 || creature->m_AreaMod.m_bIsInBattleField ||
        (GetMaster() != nullptr && GetMaster()->m_EventMatchUser.m_byEventMatchTeamId != 0);
}

bool CSummon::IsEnemyTo(CCreature* creature) {
    // Current L2Server 0x00954644.
    L2_GUARD_FUNCTION();
    if (User* const master = GetMaster()) {
        return master->IsEnemyTo(creature);
    }
    return GetGuilty() != 0 || m_data->m_nInBattleField < 0 || creature->m_AreaMod.m_bIsInBattleField ||
        (GetMaster() != nullptr && GetMaster()->m_EventMatchUser.m_byEventMatchTeamId != 0);
}

bool CSummon::IsMakeAttackerGuilty(CCreature* attacker, bool flag) {
    // Current L2Server 0x00954758.
    L2_GUARD_FUNCTION();
    if (User* const master = GetMaster()) {
        return master->IsMakeAttackerGuilty(attacker, flag);
    }
    if (GetGuilty() == 0 && m_data->m_nInBattleField >= 0 && !attacker->m_AreaMod.m_bIsInBattleField &&
        (GetMaster() == nullptr || GetMaster()->m_EventMatchUser.m_byEventMatchTeamId == 0)) {
        flag = !flag;
    }
    return flag;
}

bool CSummon::IsMakeAttackerChao(CCreature* attacker) {
    // Current L2Server 0x0095488c.
    L2_GUARD_FUNCTION();
    if (User* const master = GetMaster()) {
        return master->IsMakeAttackerChao(attacker);
    }
    return GetGuilty() == 0 && m_data->m_nInBattleField >= 0 && !attacker->m_AreaMod.m_bIsInBattleField &&
        (GetMaster() == nullptr || GetMaster()->m_EventMatchUser.m_byEventMatchTeamId == 0);
}

void CSummon::OnCreated() {
    // Current L2Server 0x009554d4.
    L2_GUARD_FUNCTION();
    User* const master = GetMaster();
    if (master == nullptr) {
        return;
    }
    const int summonIndex = IsPet() ? static_cast<int>(GetDBID()) : m_nIndexForMaster;
    db.RequestLoadSummonAbnormalStatus(*master, m_data->m_nClassID, summonIndex);
}

void CSummon::OnEnterWorld(FVector&) {
    // Current L2Server 0x00957e14.
    L2_GUARD_FUNCTION();
    User* const master = GetMaster();
    if (master != nullptr && master->m_PledgeGameUser.IsWaiting()) {
        SetVisible(false);
    }
    ValidateParameters(VB_SKILLMOD, false);
}

CCreature* CSummon::GetDelegate() {
    // Current L2Server 0x00954a28.
    L2_GUARD_FUNCTION();
    User* const master = GetMaster();
    return master != nullptr ? static_cast<CCreature*>(master) : static_cast<CCreature*>(this);
}

void CSummon::SetSpelledInfoChanged() {
    // Current L2Server 0x0095e1ac.
    runtime::raw_atomic::ExchangeI32(&m_nSpelledInfoChanged, 1);
}

void CSummon::SendSpelledInfoChanged() {
    // Current L2Server 0x00954cc0.
    L2_GUARD_FUNCTION();
    User* const master = GetMaster();
    if (master == nullptr) {
        return;
    }

    std::array<char, 0x4000> packet;
    int packetSize = static_cast<int>(packet.size());
    const int count = PrepareSpelledInfoChangedBuffer(packet.data(), packetSize);
    master->GetSocket()->Send("cdddb", 0xf4, IsPet() ? 1 : 2, static_cast<int>(Id()), count, packetSize, packet.data());
}

void CSummon::TimerExpired(int id) {
    // Current L2Server 0x009597cc.
    L2_GUARD_FUNCTION();
    if (m_bDeleting || m_Deleted != 0) {
        return;
    }

    CNPC::TimerExpired(id);
    const std::uint32_t now = runtime::GetTickCount();
    const bool pet = IsPet();
    if (!pet && m_LifeTimeTick + 5000U < now) {
        m_LifeTimeTick = now;
        if (User* const master = GetMaster()) {
            master->GetSocket()->Send("cdd", 0xd1, GetMaxLife(), GetCurrentLife());
        }
    }

    if (runtime::raw_atomic::ExchangeI32(&m_nSpelledInfoChanged, 0) != 0) {
        SendSpelledInfoChanged();
    }

    User* const master = GetMaster();
    if (master != nullptr && master->IsAlive() && IsAlive()) {
        const FVector ownPosition = GetPos();
        const FVector masterPosition = master->GetPos();
        const double deltaX = ownPosition.x - masterPosition.x;
        const double deltaY = ownPosition.y - masterPosition.y;
        const double deltaZ = ownPosition.z - masterPosition.z;
        const double distance = std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
        if (distance >= 5000.0 && master->m_data->m_bIsActive && m_data->m_bIsActive) {
            StopMove();
            TeleportToLocation(
                static_cast<int>(masterPosition.x), static_cast<int>(masterPosition.y), static_cast<int>(masterPosition.z),
                master->GetInZoneID(), 0x10000, static_cast<TeleportSourceType::Enum>(0x3d), nullptr, 0
            );
        }
    }

    const std::int32_t saveTick = m_SummonDBSaveTick++;
    if (saveTick > 500) {
        if (master != nullptr) {
            db.RequestSaveSummon(*master);
        }
        m_SummonDBSaveTick = 0;
    }
}

std::int64_t CSummon::ExpInc(std::int64_t, bool) {
    // Current L2Server 0x009549a8.
    L2_GUARD_FUNCTION();
    return 0;
}

bool CSummon::CheckConsumeItemCondition() {
    L2_GUARD_FUNCTION();
    const std::int32_t interval = m_data->m_Race == RT_SIEGE_WEAPON ? 60000 : 240000;
    const std::int32_t remainingCount = runtime::SubtractInt32Wrapping(
        m_nOrgDuration / interval, runtime::raw_atomic::LoadI32Relaxed(&m_nConsumedCount)
    );
    if (remainingCount <= 0) {
        return false;
    }
    return static_cast<std::uint32_t>(GetCurrentLife()) < static_cast<std::int64_t>(interval) * remainingCount;
}

void CSummon::ConsumeItem() {
    // Current L2Server 0x009556a0.
    L2_GUARD_FUNCTION();

    if (m_nConsumeItemCount != 0) {
        User* const master = GetMaster();
        if (master != nullptr) {
            bool consumed = false;
            if (m_nConsumeItemClassID >= 0) {
                consumed = master->DeleteItemInInventory(m_nConsumeItemClassID, m_nConsumeItemCount, nullptr, false, false);
            }
            if (consumed) {
                master->GetSocket()->Send(
                    "chccdcQ", 0x62, SystemMessageId::SERVITOR_MAINTENANCE_ITEM_COST, 2, 3, m_nConsumeItemClassID, 6,
                    m_nConsumeItemCount
                );
            } else {
                master->SendSystemMessage(SystemMessageId::SERVITOR_DISAPPEARED_MAINTENANCE_ITEMS_MISSING);
                Delete();
            }
        }
    }
    runtime::raw_atomic::IncrementI32(&m_nConsumedCount);
}

void CSummon::UpdateSummonByPassive() {
    L2_GUARD_FUNCTION();
    if (GetMaster() != nullptr) {
        ValidatePHit();
        ParamChanged(NPCT_VALIDATE);
    }
}

void CSummon::UpdateSummonItemCountByRemainTime() {
    L2_GUARD_FUNCTION();
    const std::int32_t interval = m_data->m_Race == RT_SIEGE_WEAPON ? 60000 : 240000;
    const std::uint64_t elapsed = static_cast<std::uint32_t>(m_nOrgDuration) - static_cast<std::uint32_t>(GetCurrentLife());
    std::int32_t consumed = 0;
    if (static_cast<std::int64_t>(elapsed) < 0) {
        const std::int32_t loggedDuration = m_nOrgDuration;
        const std::int32_t currentLife = GetCurrentLife();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]UpdateSummonItemCountByRemainTime %d > %d over time",
        //     __WFILE__,
        //     __LINE__,
        //     currentLife,
        //     loggedDuration)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d]UpdateSummonItemCountByRemainTime %d > %d over time",
        //     __WFILE__,
        //     __LINE__,
        //     currentLife,
        //     loggedDuration)
        L2_LOG_ERROR(
            Log,
            "Summon object %u has invalid elapsed lifetime %llu (remaining %d, original %d); using original duration to "
            "calculate consumed items.",
            static_cast<unsigned>(m_nId), static_cast<unsigned long long>(elapsed), currentLife, loggedDuration
        );

        CSharedCreatureData* const data = m_data;
        const std::int32_t fallbackDuration = m_nOrgDuration;
        const std::int32_t fallbackInterval = data->m_Race == RT_SIEGE_WEAPON ? 60000 : 240000;
        consumed = static_cast<std::int16_t>(fallbackDuration / fallbackInterval);
    } else {
        consumed =
            runtime::Win64CvttDoubleToInt32(std::ceil(static_cast<double>(elapsed) / static_cast<double>(interval)));
    }
    runtime::raw_atomic::ExchangeI32(&m_nConsumedCount, consumed);
}

void CSummon::ValidateSummonAttackSpeed() {
    L2_GUARD_FUNCTION();
    CSharedCreatureData* const data = m_data;
    data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(
        data->m_dParam[VALIDATE::PATTACKSPEED], ClampSummonSpeed(data->m_dParam[VALIDATE::PATTACKSPEED], 1500.0), 0x3b
    );
    data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(
        data->m_dParam[VALIDATE::MATTACKSPEED], ClampSummonSpeed(data->m_dParam[VALIDATE::MATTACKSPEED], 6.006006006006006),
        0x3b
    );
    data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(
        data->m_dParam[VALIDATE::PSKILLSPEED], ClampSummonSpeed(data->m_dParam[VALIDATE::PSKILLSPEED], 6.006006006006006),
        0x3b
    );
}

void CSummon::SendSummonDelete() {
    L2_GUARD_FUNCTION();

    User* const master = GetMaster();
    if (master == nullptr) {
        return;
    }
    if (!master->HaveSummon() && !master->HavePet()) {
        master->TurnOffSummonSoulShot();
    }
    master->GetSocket()->Send("cdd", 0xb7, static_cast<int>(m_data->m_SummonType), static_cast<int>(Id()));
}

void CSummon::SendSummonInfo(bool first, bool second) {
    L2_GUARD_FUNCTION();

    User* const master = GetMaster();
    if (master == nullptr) {
        return;
    }

    CUserSocket* const socket = master->GetSocket();
    std::uint8_t teamEffect = 0;
    if (master->IsEventMatch()) {
        CSPointer<CEventMatch> eventMatch = master->m_EventMatchUser.GetEventMatch();
        if (eventMatch) {
            const EventMatchStatus status = eventMatch->GetStatus();
            if (status == EMStatus_PLAYING || status == EMStatus_PAUSE) {
                teamEffect = master->m_EventMatchUser.m_byEventMatchTeamId;
            }
        }
    }

    if (master->IsInDuel() && IsAlive()) {
        teamEffect = master->m_DuelUserInfo.GetTeamForEffect(master);
    }

    User* const partyMaster = GetMaster();
    if (partyMaster != nullptr) {
        CParty* const party = partyMaster->GetParty();
        if (party != nullptr) {
            teamEffect = static_cast<std::uint8_t>(party->m_data->GetPVPMatchTeamIndex());
        }
    }

    const FVector position(m_data->m_Pos);
    OrgSpeeds speeds{};
    GetOrgSpeeds(speeds);

    const double attackModifier = GetAttackModifier();
    const double moveModifier = GetMoveModifier();
    const double collisionRadius = m_data->m_dCollisionRadius;
    const double collisionHeight = m_data->m_dCollisionHeight;
    const int magicAttackSpeed = runtime::Win64CvttDoubleToInt32(m_data->m_dParam[VALIDATE::MATTACKSPEED] * 333.0);
    const int physicalSkillSpeed = runtime::Win64CvttDoubleToInt32(m_data->m_dParam[VALIDATE::PSKILLSPEED] * 333.0);

    CItemFactory& items = CItemFactory::Inst();
    const int chestIndex = m_data->m_nSlotIndex[ST_CHEST];
    int chestClassId = chestIndex == -1 ? -1 : items.GetData(chestIndex)->m_nClassID;

    const int rightHandIndex = m_data->m_nSlotIndex[ST_RHAND];
    int rightHandClassId =
        rightHandIndex == -1 ? m_pClassConst->m_nDefaultRHandItemClassID : items.GetData(rightHandIndex)->m_nClassID;

    const int leftHandIndex = m_data->m_nSlotIndex[ST_LHAND];
    int leftHandClassId =
        leftHandIndex == -1 ? m_pClassConst->m_nDefaultLHandItemClassID : items.GetData(leftHandIndex)->m_nClassID;

    const char guilty = GetGuilty();
    const int criminalRate = CalcCriminalRate();

    bool ridingPet = false;
    if (IsPet()) {
        ridingPet = CastPet()->GetRideType() > RIDE_NONE;
    }

    PetDataSheet* const dataSheet = PetDataSheetTable::Inst().GetPetDataSheetByIndex(m_petIndex);
    std::int64_t currentExperience = m_data->m_nExp;
    const std::int64_t levelExperience = dataSheet->GetRecord(m_data->m_nLevel)->mExp;
    const std::int64_t nextLevelExperience = dataSheet->GetRecord(m_data->m_nLevel + 1)->mExp;
    const int currentLife = GetCurrentLife();
    const int maximumLife = GetMaxLife();
    if (IsPet() && CastPet()->IsSyncLevel()) {
        currentExperience = nextLevelExperience;
    }

    int currentSummonPoint = 0;
    int maximumSummonPoint = 0;
    if (!IsPet() && GetMaster() != nullptr) {
        currentSummonPoint = GetMaster()->GetSummonPoint();
        maximumSummonPoint = GetMaster()->GetMaxSummonPoint();
    }

    CPacketHelperEx<20480> abnormalVisualEffects;
    MakeAbnormalVisualEffectPacket(abnormalVisualEffects);

    std::uint8_t state = IsEnemyTo(master) ? 0x01U : 0x00U;
    if (m_data->m_StopMode != 0) {
        state |= 0x02U;
    }
    if (m_data->m_MoveMode != 0) {
        state |= 0x04U;
    }
    if (m_data->m_bIsInCombatMode) {
        state |= 0x08U;
    }
    if (m_data->m_bIsAlive == 0) {
        state |= 0x10U;
    }
    if (ridingPet) {
        state |= 0x20U;
    }

    const int carryingWeight = m_inv.GetCarryingWeight();
    const int abnormalEffectCount = abnormalVisualEffects.GetAssembledCount();
    const int morphId = GetMorphID();
    const int spiritShotCount = GetSpiritShotCount();
    const int soulShotCount = GetSoulShotCount();
    const int magicCritical = CriticalRateForInfo(VALIDATE::MCRITICAL);
    const int physicalCritical = CriticalRateForInfo(VALIDATE::PCRITICAL);

    if (leftHandClassId == -1) {
        leftHandClassId = 0;
    }
    if (chestClassId == -1) {
        chestClassId = 0;
    }
    if (rightHandClassId == -1) {
        rightHandClassId = 0;
    }

    const int summonInfoType = first ? 1 : (second ? 2 : 0);
    const int classId = GetClassId();
    socket->Send(
        "ccddddddddhhhhhhhhffffdddcdSdScdddddddQcQQQdddddddddddddddccccddcchbc", 0xb2,
        static_cast<int>(static_cast<std::int8_t>(m_data->m_SummonType)), static_cast<int>(m_nId), classId,
        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        runtime::Win64CvttDoubleToInt32(position.z), m_data->GetDirByInt() & 0xffff, magicAttackSpeed, physicalSkillSpeed,
        static_cast<int>(static_cast<std::int16_t>(speeds.m_nOrgSpeed0)),
        static_cast<int>(static_cast<std::int16_t>(speeds.m_nOrgSpeed1)),
        static_cast<int>(static_cast<std::int16_t>(speeds.m_nOrgSpeed2)),
        static_cast<int>(static_cast<std::int16_t>(speeds.m_nOrgSpeed3)),
        static_cast<int>(static_cast<std::int16_t>(speeds.m_nOrgSpeed4)),
        static_cast<int>(static_cast<std::int16_t>(speeds.m_nOrgSpeed5)),
        static_cast<int>(static_cast<std::int16_t>(speeds.m_nOrgSpeed6)),
        static_cast<int>(static_cast<std::int16_t>(speeds.m_nOrgSpeed7)), moveModifier, attackModifier, collisionRadius,
        collisionHeight, rightHandClassId, chestClassId, leftHandClassId, summonInfoType, m_NickNameIndex, m_sMasterName,
        m_MasterNameIndex, m_data->Name(), static_cast<int>(guilty), criminalRate, currentLife, maximumLife,
        runtime::Win64CvttDoubleToInt32(m_data->GetHP()), runtime::Win64CvttDoubleToInt32(m_data->m_dBaseHP),
        runtime::Win64CvttDoubleToInt32(m_data->GetMP()), runtime::Win64CvttDoubleToInt32(m_data->m_dBaseMP),
        m_data->m_nSP, static_cast<int>(static_cast<std::int8_t>(m_data->m_nLevel)), currentExperience, levelExperience,
        nextLevelExperience, carryingWeight, runtime::Win64CvttDoubleToInt32(m_data->m_dCarryWeight),
        runtime::Win64CvttDoubleToInt32(m_data->m_dParam[VALIDATE::PATTACK]),
        runtime::Win64CvttDoubleToInt32(m_data->m_dParam[VALIDATE::PDEFEND]),
        runtime::Win64CvttDoubleToInt32(m_data->m_dParam[VALIDATE::PHIT]),
        runtime::Win64CvttDoubleToInt32(m_data->m_dParam[VALIDATE::PAVOID]), physicalCritical,
        runtime::Win64CvttDoubleToInt32(m_data->m_dParam[VALIDATE::MATTACK]),
        runtime::Win64CvttDoubleToInt32(m_data->m_dParam[VALIDATE::MDEFEND]),
        runtime::Win64CvttDoubleToInt32(m_data->m_dParam[VALIDATE::MHIT]),
        runtime::Win64CvttDoubleToInt32(m_data->m_dParam[VALIDATE::MAVOID]), magicCritical,
        runtime::Win64CvttDoubleToInt32(m_data->m_dParam[VALIDATE::SPEED]),
        runtime::Win64CvttDoubleToInt32(m_data->m_dParam[VALIDATE::PATTACKSPEED]), magicAttackSpeed,
        static_cast<int>(static_cast<std::int8_t>(m_data->m_Environment)),
        static_cast<int>(static_cast<std::int8_t>(teamEffect)), static_cast<int>(static_cast<std::int8_t>(soulShotCount)),
        static_cast<int>(static_cast<std::int8_t>(spiritShotCount)), m_data->m_EvolutionId, morphId,
        static_cast<int>(static_cast<std::int8_t>(currentSummonPoint)),
        static_cast<int>(static_cast<std::int8_t>(maximumSummonPoint)), abnormalEffectCount,
        abnormalVisualEffects.GetLength(), abnormalVisualEffects.GetPacket(),
        static_cast<int>(static_cast<std::int8_t>(state))
    );

    if (!first && IsPet()) {
        CastPet()->SendPetItemList(false);
    }
}

void CSummon::SendSummonStatusToMaster(const int reason) {
    // Current L2Server 0x00959b98.
    L2_GUARD_FUNCTION();

    User* const master = GetMaster();
    if (master == nullptr) {
        return;
    }

    const FVector position = m_data->GetPos();
    PetDataSheet* const dataSheet = PetDataSheetTable::Inst().GetPetDataSheetByIndex(m_petIndex);
    std::int64_t currentExperience = m_data->m_nExp;
    const std::int64_t levelExperience = dataSheet->GetRecord(m_data->m_nLevel)->mExp;
    const std::int64_t nextLevelExperience = dataSheet->GetRecord(m_data->m_nLevel + 1)->mExp;
    if (IsPet() && CastPet()->IsSyncLevel()) {
        currentExperience = nextLevelExperience;
    }

    const int currentLife = GetCurrentLife();
    const int maximumLife = GetMaxLife();
    master->GetSocket()->Send(
        "cdddddSdddddddQQQd", 0xb6, static_cast<int>(m_data->m_SummonType), runtime::Int32FromBits(m_nId),
        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        runtime::Win64CvttDoubleToInt32(position.z), m_sNickName, currentLife, maximumLife,
        runtime::Win64CvttDoubleToInt32(m_data->m_dHP), runtime::Win64CvttDoubleToInt32(m_data->m_dBaseHP),
        runtime::Win64CvttDoubleToInt32(m_data->m_dMP), runtime::Win64CvttDoubleToInt32(m_data->m_dBaseMP),
        m_data->m_nLevel, currentExperience, levelExperience, nextLevelExperience, reason
    );

    if (CParty* const party = master->GetParty()) {
        party->UpdatePetWindowPacket(this);
    }
}

void CSummon::AddSummonStatBonus(CCreatureSkillMod::SummonStatBonusMod mod) {
    L2_GUARD_FUNCTION();
    User* const master = GetMaster();
    if (master == nullptr) {
        return;
    }

    const double bonus = master->m_SkillMod.m_ModSummonBonus[mod];
    if (0.0 < bonus) {
        double* target = nullptr;
        double candidate = 0.0;
        int updateBit = 0x3b;

        switch (mod) {
            case CCreatureSkillMod::SBMN_PAttack:
                target = &m_data->m_dParam[VALIDATE::PATTACK];
                candidate = bonus * master->m_data->m_dParam[VALIDATE::PATTACK] / 100.0 + *target;
                break;
            case CCreatureSkillMod::SBMN_PDefend:
                target = &m_data->m_dParam[VALIDATE::PDEFEND];
                candidate = bonus * master->m_data->m_dParam[VALIDATE::PDEFEND] / 100.0 + *target;
                break;
            case CCreatureSkillMod::SBMN_MAttack:
                target = &m_data->m_dParam[VALIDATE::MATTACK];
                candidate = bonus * master->m_data->m_dParam[VALIDATE::MATTACK] / 100.0 + *target;
                break;
            case CCreatureSkillMod::SBMN_MDefend:
                target = &m_data->m_dParam[VALIDATE::MDEFEND];
                candidate = bonus * master->m_data->m_dParam[VALIDATE::MDEFEND] / 100.0 + *target;
                break;
            case CCreatureSkillMod::SBMN_HP:
                target = &m_data->m_dOrgHP;
                candidate = bonus * master->m_data->m_dOrgHP / 100.0 + *target;
                updateBit = 8;
                break;
            case CCreatureSkillMod::SBMN_MP:
                target = &m_data->m_dOrgMP;
                candidate = bonus * master->m_data->m_dOrgMP / 100.0 + *target;
                updateBit = 9;
                break;
            case CCreatureSkillMod::SBMN_PUseSpeed:
                target = &m_data->m_dParam[VALIDATE::PSKILLSPEED];
                candidate = bonus * master->m_data->m_dParam[VALIDATE::PSKILLSPEED] / 100.0 + *target;
                break;
            case CCreatureSkillMod::SBMN_PAttackSpeed:
                target = &m_data->m_dParam[VALIDATE::PATTACKSPEED];
                candidate = bonus * master->m_data->m_dParam[VALIDATE::PATTACKSPEED] / 100.0 + *target;
                break;
            case CCreatureSkillMod::SBMN_MUseSpeed:
                target = &m_data->m_dParam[VALIDATE::MATTACKSPEED];
                candidate = bonus * master->m_data->m_dParam[VALIDATE::MATTACKSPEED] / 100.0 + *target;
                break;
            case CCreatureSkillMod::SBMN_Speed: {
                target = &m_data->m_dParam[VALIDATE::SPEED];
                const double value = master->m_data->m_dParam[VALIDATE::SPEED] * bonus / 100.0 + *target;
                candidate = ClampSummonSpeed(value, IsBoss() ? 500.0 : 350.0);
                break;
            }
            case CCreatureSkillMod::SBMN_CriticalProb:
                target = &m_data->m_dParam[VALIDATE::PCRITICAL];
                candidate = bonus * master->m_data->m_dParam[VALIDATE::PCRITICAL] / 100.0 + *target;
                break;
            default:
                break;
        }

        if (target != nullptr) {
            m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(*target, candidate, updateBit);
        }
    }
    ValidateSummonAttackSpeed();
}

void CSummon::AddSummonPassiveBonus(CCreatureSkillMod::SummonStatBonusMod mod) {
    // Current L2Server 0x00955a88.
    L2_GUARD_FUNCTION();
    User* const master = GetMaster();
    if (master == nullptr || mod != CCreatureSkillMod::SBMN_PHit) {
        return;
    }
    double& physicalHit = m_data->m_dParam[VALIDATE::PHIT];
    m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(
        physicalHit,
        master->m_SkillMod.m_Mods[CCreatureSkillMod::HitSummon].m_Per * physicalHit +
            master->m_SkillMod.m_Mods[CCreatureSkillMod::HitSummon].m_Diff,
        0x3b
    );
}

bool CSummon::ValidatePHit() {
    // Current L2Server 0x00957360.
    L2_GUARD_FUNCTION();
    CCreature::ValidatePHit();
    AddSummonPassiveBonus(CCreatureSkillMod::SBMN_PHit);
    return true;
}

bool CSummon::ValidatePAttack() {
    L2_GUARD_FUNCTION();
    CCreature::ValidatePAttack();
    AddSummonStatBonus(CCreatureSkillMod::SBMN_PAttack);
    return true;
}

bool CSummon::ValidateMAttack() {
    L2_GUARD_FUNCTION();
    CCreature::ValidateMAttack();
    AddSummonStatBonus(CCreatureSkillMod::SBMN_MAttack);
    return true;
}

bool CSummon::ValidatePDefend() {
    L2_GUARD_FUNCTION();
    CCreature::ValidatePDefend();
    AddSummonStatBonus(CCreatureSkillMod::SBMN_PDefend);
    return true;
}

bool CSummon::ValidateMDefend() {
    L2_GUARD_FUNCTION();
    CCreature::ValidateMDefend();
    AddSummonStatBonus(CCreatureSkillMod::SBMN_MDefend);
    return true;
}

bool CSummon::ValidateAttackSpeed() {
    L2_GUARD_FUNCTION();
    CCreature::ValidateAttackSpeed();
    AddSummonStatBonus(CCreatureSkillMod::SBMN_PAttackSpeed);
    return true;
}

bool CSummon::ValidateMUseSpeed() {
    L2_GUARD_FUNCTION();
    CCreature::ValidateMUseSpeed();
    AddSummonStatBonus(CCreatureSkillMod::SBMN_MUseSpeed);
    return true;
}

bool CSummon::ValidatePUseSpeed() {
    L2_GUARD_FUNCTION();
    CCreature::ValidatePUseSpeed();
    AddSummonStatBonus(CCreatureSkillMod::SBMN_PUseSpeed);
    return true;
}

bool CSummon::ValidatePCriticalProb() {
    L2_GUARD_FUNCTION();
    CCreature::ValidatePCriticalProb();
    AddSummonStatBonus(CCreatureSkillMod::SBMN_CriticalProb);
    return true;
}

bool CSummon::ValidateBaseMP() {
    L2_GUARD_FUNCTION();
    CCreature::ValidateBaseMP();
    AddSummonStatBonus(CCreatureSkillMod::SBMN_MP);
    return true;
}

bool CSummon::ValidateBaseHP() {
    L2_GUARD_FUNCTION();
    CCreature::ValidateBaseHP();
    AddSummonStatBonus(CCreatureSkillMod::SBMN_HP);
    return true;
}

bool CSummon::Morph(int classId) {
    // Current L2Server 0x00957174.
    L2_GUARD_FUNCTION();
    return CNPC::Morph(classId);
}

void CSummon::UnMorph() {
    // Current L2Server 0x00957218.
    L2_GUARD_FUNCTION();
    CNPC::UnMorph();
}

void CSummon::ChangeMorph() {
    // Current L2Server 0x009572bc.
    L2_GUARD_FUNCTION();
    CNPC::ChangeMorph();
}

bool CSummon::ValidateSpeed() {
    // Current L2Server 0x0095adf4.
    L2_GUARD_FUNCTION();
    CNPC::ValidateSpeed();
    AddSummonStatBonus(CCreatureSkillMod::SBMN_Speed);
    ValidateGradePenalty(VALIDATE::SPEED);
    return true;
}

void CSummon::CheckSummonBattleMode() {
    // Current L2Server 0x00954f38.
    L2_GUARD_FUNCTION();
    User* const master = GetMaster();
    if (master == nullptr) {
        return;
    }
    if (!master->m_data->m_bIsInCombatMode) {
        if (m_data->m_bIsInCombatMode) {
            master->SetInCombatMode(true);
            master->BroadcastCombatModeStart();
        }
    } else if (!m_data->m_bIsInCombatMode) {
        master->SetInCombatMode(false);
        master->BroadcastCombatModeFinish();
    }
}

void CSummon::CheckSummonBattleTimeStamp() {
    // Current L2Server 0x00955054.
    L2_GUARD_FUNCTION();
    User* const master = GetMaster();
    if (master == nullptr) {
        return;
    }
    if (!master->m_data->m_bIsInCombatMode) {
        master->BroadcastCombatModeStart();
        master->SetInCombatMode(true);
    }
    CIOObject::SetTimeStamp(&master->m_nCombatModeTimeStamp);
}

void CSummon::SendSystemMessage(unsigned int messageId) {
    // Current L2Server 0x009541d8.
    L2_GUARD_FUNCTION();
    if (User* const master = GetMaster()) {
        master->SendSystemMessage(messageId);
    }
}

void CSummon::SendSystemMessage_d(unsigned int messageId, int value) {
    // Current L2Server 0x00954294.
    L2_GUARD_FUNCTION();
    if (User* const master = GetMaster()) {
        master->SendSystemMessage_d(messageId, value);
    }
}

void CSummon::SendSystemMessage_Q(unsigned int messageId, std::int64_t value) {
    // Current L2Server 0x00954364.
    L2_GUARD_FUNCTION();
    if (User* const master = GetMaster()) {
        master->SendSystemMessage_Q(messageId, value);
    }
}

void CSummon::SendSystemMessageWithSelf(unsigned int messageId) {
    // Current L2Server 0x00954434.
    L2_GUARD_FUNCTION();
    User* const master = GetMaster();
    if (master == nullptr) {
        return;
    }
    master->GetSocket()->Send("chccd", 0x62, messageId, 1, 2, GetClassId());
}

void CSummon::LeaveWorld(bool) {
    // Current L2Server 0x009555c8 ignores the caller's save flag.
    L2_GUARD_FUNCTION();
    CNPC::LeaveWorld(true);
}

bool CSummon::Die(CCreature* killer, int reason) {
    // Current L2Server 0x0095f058. The two Cratae-Cube branches are
    // kept separate: the target distinguishes a direct PC
    // killer from a summon whose master is the PC participant.
    L2_GUARD_FUNCTION();
    if (killer != nullptr && killer->IsPCOrPCRelatedNPC() && !AreaBasedRule::CastBadSkill(this)) {
        m_data->SetHP(1.0);
        return false;
    }

    User* const master = GetMaster();
    if (killer != nullptr && master != nullptr) {
        std::int32_t killerDatabaseId = 0;
        std::int32_t killerAccountId = 0;
        std::array<wchar_t, 25> killerName{};
        std::array<wchar_t, 25> killerAccountName{};
        if (killer->IsPC()) {
            killerDatabaseId = runtime::Int32FromBits(killer->GetDBID());
            killerAccountId = runtime::Int32FromBits(killer->CastUser()->GetAccountID());
            CopyPetAuditWide25(killerName, killer->GetName());
            CopyPetAuditWide25(killerAccountName, killer->CastUser()->m_sAccountName.data());
        } else if (killer->IsSummonOrPet()) {
            User* const killerUser = killer->CastSummon()->GetMaster();
            if (killerUser != nullptr) {
                killerDatabaseId = runtime::Int32FromBits(killerUser->GetDBID());
                killerAccountId = runtime::Int32FromBits(killerUser->GetAccountID());
                CopyPetAuditWide25(killerName, killerUser->GetName());
                CopyPetAuditWide25(killerAccountName, killerUser->m_sAccountName.data());
                if (!IsPet() && !killer->IsPet() && CPVPMatchSystem::inst().IsCrataeCubeUser(*killerUser)) {
                    CPVPMatch* const match = CPVPMatchSystem::inst().GetMatchByUser(*master);
                    if (match != nullptr) {
                        g_npcserver.Send(
                            "cdddd", 0x7c, static_cast<int>(match->GetManagerNpcId()), static_cast<int>(master->Id()),
                            static_cast<int>(killerUser->Id()), 1
                        );
                    }
                }
            }
        }

        if (killer->IsPCOrSummon()) {
            const double positionZ = GetPos().z;
            const double positionY = GetPos().y;
            const double positionX = GetPos().x;
            const char16_t* const masterName = master->GetName();
            const std::int32_t masterAccountId = runtime::Int32FromBits(master->GetAccountID());
            const std::int32_t masterDatabaseId = runtime::Int32FromBits(master->GetDBID());
            const std::wstring masterAccountName = Utf16ToWide(master->m_sAccountName.data());
            // Log.Add(
            //     LOG_BOTH,
            //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d"
            //     L",,,,,,,,,%I64d,,,,,%s,%s,%s,%s,",
            //     static_cast<std::int64_t>(0x463),
            //     static_cast<std::int64_t>(killerDatabaseId),
            //     static_cast<std::int64_t>(killerAccountId),
            //     static_cast<std::int64_t>(masterDatabaseId),
            //     static_cast<std::int64_t>(masterAccountId),
            //     static_cast<std::int64_t>(
            //         runtime::Win64CvttDoubleToInt32(positionX)),
            //     static_cast<std::int64_t>(
            //         runtime::Win64CvttDoubleToInt32(positionY)),
            //     static_cast<std::int64_t>(
            //         runtime::Win64CvttDoubleToInt32(positionZ)),
            //     static_cast<std::int64_t>(1),
            //     killerName.data(),
            //     killerAccountName.data(),
            //     masterName,
            //     masterAccountName.c_str())
            Log.Add(
                LOG_BOTH,
                u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d"
                u",,,,,,,,,%I64d,,,,,%s,%s,%s,%s,",
                static_cast<std::int64_t>(0x463), static_cast<std::int64_t>(killerDatabaseId),
                static_cast<std::int64_t>(killerAccountId), static_cast<std::int64_t>(masterDatabaseId),
                static_cast<std::int64_t>(masterAccountId),
                static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(positionX)),
                static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(positionY)),
                static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(positionZ)), static_cast<std::int64_t>(1),
                killerName.data(), killerAccountName.data(), masterName, masterAccountName.c_str()
            );
        }
        master->ValidateChanged(std::int64_t{VB_SKILLMOD});
    }

    if (!IsPet() && killer != nullptr && master != nullptr && killer->IsPC()) {
        User* const killerUser = killer->CastUser();
        if (CPVPMatchSystem::inst().IsCrataeCubeUser(*killerUser)) {
            CPVPMatch* const match = CPVPMatchSystem::inst().GetMatchByUser(*master);
            if (match != nullptr) {
                g_npcserver.Send(
                    "cdddd", 0x7c, static_cast<int>(match->GetManagerNpcId()), static_cast<int>(master->Id()),
                    static_cast<int>(killer->Id()), 1
                );
            }
        }
    }

    if (!CNPC::Die(killer, reason)) {
        return false;
    }
    SendSystemMessage(SystemMessageId::SERVITOR_DIED);
    return true;
}

void CSummon::OnCorpseDecayed() {
    // Current L2Server 0x00959a94.
    L2_GUARD_FUNCTION();
    if (m_data->m_bDecayComplete) {
        User* const master = GetMaster();
        if (master != nullptr) {
            DeleteInfectedSkills(nullptr);
            const int summonIndex = master->GetSummonIndex(static_cast<int>(Id()));
            if (static_cast<unsigned int>(summonIndex) < 10U) {
                master->UnlinkSummon(this);
                db.RequestDeleteSummon(*master, summonIndex, nullptr);
            }
        }
    }
    CNPC::Delete();
}

void CSummon::SendResurrectionConfirmDlg() {
    // Current L2Server 0x00957cd8.
    L2_GUARD_FUNCTION();
    User* const master = GetMaster();
    if (master == nullptr) {
        return;
    }
    master->GetSocket()->Send(
        "cdddSdQdddd", 0xf3, 0x5e6, 3, 0xc, m_data->m_sResurrectSpellerName, 6, m_data->m_nResurrectExp, 1,
        m_data->m_nResurrectRatio, 0, m_data->m_dwResurrectTime
    );
}

bool CSummon::CanForceAttack() const {
    // Current L2Server 0x00955138.
    L2_GUARD_FUNCTION();
    User* const master = GetMaster();
    return master == nullptr || master->CanForceAttack();
}

CParty* CSummon::GetParty() {
    // Current L2Server 0x009551f4.
    L2_GUARD_FUNCTION();
    User* const master = GetMaster();
    return master == nullptr ? nullptr : master->GetParty();
}

double CSummon::GetCriticalDamagePer() {
    // Current L2Server 0x0095a440.
    User* const master = GetMaster();
    const double own = m_SkillMod.m_Mods[CCreatureSkillMod::CriticalDamage].m_Per;
    if (master == nullptr) {
        return own;
    }
    const double factor = std::max(
        ((master->m_SkillMod.m_Mods[CCreatureSkillMod::CriticalDamage].m_Per - 1.0) *
            master->m_SkillMod.m_ModSummonBonus[CCreatureSkillMod::SBMN_CriticalDamage]) /
                100.0 +
            1.0,
        1.0
    );
    return own * factor;
}

double CSummon::GetCriticalDamageDiff() {
    // Current L2Server 0x0095a4b8.
    User* const master = GetMaster();
    const double own = m_SkillMod.m_Mods[CCreatureSkillMod::CriticalDamage].m_Diff;
    if (master == nullptr) {
        return own;
    }
    const double factor = std::max(master->m_SkillMod.m_ModSummonBonus[CCreatureSkillMod::SBMN_CriticalDamage] / 100.0, 0.0);
    return master->m_SkillMod.m_Mods[CCreatureSkillMod::CriticalDamage].m_Diff * factor + own;
}

void CSummon::EnterWorld(bool unknown, int first, int second, unsigned int third) {
    // Current L2Server 0x0095c78c.
    L2_GUARD_FUNCTION();
    CheckAbnormalVisualEffect();
    CNPC::EnterWorld(unknown, first, second, third);
    User* const master = GetMaster();
    if (master != nullptr) {
        db.RequestSaveSummon(*master);
        if (master->m_bAbnormalStatusChanged) {
            master->ValidateChanged(std::int64_t{VB_ITEMSKILL});
        }
    }
    if (!IsPet()) {
        UpdateSummonItemCountByRemainTime();
    }
    SendMyInfo();
}

int CSummon::GetSoulShotCount() {
    L2_GUARD_FUNCTION();
    return m_data->m_nSoulShotCount;
}

int CSummon::GetSpiritShotCount() {
    L2_GUARD_FUNCTION();
    return m_data->m_nSpiritShotCount;
}

int CSummon::GetCurrentLife() {
    const std::uint32_t maximum = m_data->m_dwSummonLifeTime;
    const std::uint32_t remaining = m_data->m_dwSummonTime - runtime::GetTickCount() + maximum;
    return static_cast<int>(std::min(remaining, maximum));
}

int CSummon::GetMaxLife() { return m_nOrgDuration; }

bool CPet::IsPet() { return true; }

bool CPet::IsSummon() const { return false; }

CPet* CPet::CastPet() { return this; }

void CPet::SetDBID(unsigned int dbId) { m_CollarDBID = dbId; }

bool CPet::ChangePetName(const char16_t* name) {
    // Current L2Server 0x00955cc4.
    L2_GUARD_FUNCTION();
    if (m_sNickName[0] != u'\0') {
        return false;
    }
    if (!User::CheckCharacterNameDual(name, g_nCountryCode, true)) {
        SendSystemMessage(SystemMessageId::INVALID_CHARACTERS_IN_PET_NAME);
        return false;
    }
    if (!User::CheckCharacterName(name, g_nCountryCode, true)) {
        SendSystemMessage(SystemMessageId::INVALID_CHARACTERS_IN_PET_NAME);
        return false;
    }
    db.ChangePetName(this, name);
    return true;
}

void CPet::PetNameChanged(const wchar_t* name) {
    // Current L2Server 0x00955de0.
    L2_GUARD_FUNCTION();
    SetNickName(WideToUtf16(name).c_str());
    ParamChanged(NPCT_PETNAME_CHANGED);
    User* const master = GetMaster();
    if (master != nullptr) {
        master->PetNameChanged(static_cast<int>(GetDBID()));
        db.SavePetNameChanged(*this);
    }
}

CObject* CPet::Copy() {
    L2_GUARD_FUNCTION();
    CPet* const pet = new CPet(*this);
    pet->OnCreate();
    return pet;
}

void CPet::SendDB(const char* format, ...) const {
    // Current L2Server 0x00958664.
    L2_GUARD_FUNCTION();
    const int worldId = CThreadManager::GetThreadID() < 16
        // Current instruction 0x009586ed reads CPet +0x3d78,
        // m_MasterWorldID. Reading shared creature m_nWorldId here drops
        // pet database traffic when that shared lane has not been published.
        // ? m_data->m_nWorldId
        ? m_MasterWorldID
        : g_nWorldId;
    runtime::process::SetCurrentWorldId(worldId);
    CDB::CWorldSetter worldSetter;

    va_list arguments;
    va_start(arguments, format);
    if (runtime::process::CurrentWorldId() < 1) {
        SystemMail::sendNotSetThreadLocalWorldID(mailServer);
    } else {
        db.m_pDBSockets[::LegacyCrtRand() & 1]->SendV(format, arguments);
    }
    va_end(arguments);
}

double CPet::EquipedArmorsValidate(VALIDATE::Enum type) const {
    // Current L2Server 0x0095c9e8.
    L2_GUARD_FUNCTION();
    CItemFactory& items = CItemFactory::Inst();
    CSharedItemData* const chest = items.GetData(m_data->m_nSlotIndex[ST_CHEST]);
    CSharedItemData* const underwear = items.GetData(m_data->m_nSlotIndex[ST_UNDERWEAR]);

    const auto valueFor = [type](CSharedItemData* item, ItemType requiredType) {
        if (item == nullptr || item->m_Type != requiredType) {
            return 0.0;
        }
        CSharedItemConstant* const constant = item->GetConstant();
        if (constant == nullptr) {
            return 0.0;
        }
        const double base = constant->m_dValidate[static_cast<int>(type)];
        double result = base;
        const int crystal = static_cast<int>(constant->GetCrystalType());
        if (crystal > 0 && crystal < static_cast<int>(kPetArmorEnchantBonus.size()) && base > 0.0) {
            result += static_cast<double>(item->GetEnchanted()) * kPetArmorEnchantBonus[crystal];
        }
        return result;
    };

    return valueFor(chest, IT_ARMOR) + valueFor(underwear, IT_ACCESSARY);
}

std::int64_t CPet::SetExp(std::int64_t experience) {
    // Current L2Server 0x0095b204.
    L2_GUARD_FUNCTION();
    if (IsSyncLevel()) {
        OnValidateLevel();
        return 0;
    }

    const std::int64_t maximum = m_pPetSheet->GetRecord(121)->mExp - 1;
    const std::int64_t next = std::clamp<std::int64_t>(experience, 0, maximum);
    if (m_data->m_nExp != next) {
        m_data->m_NeedUpdatePacketBitSet.SetInt64Value_IfModifed(m_data->m_nExp, next, 6);
        m_data->m_NeedUpdatePacketBitSet.SetBit(7);
    }
    ValidateChanged(std::int64_t{VB_LEVEL});
    return m_data->m_nExp;
}

std::int64_t CPet::ExpInc(std::int64_t experience, bool applyBonus) {
    // Current L2Server 0x00957ef0 passes false to the base.
    L2_GUARD_FUNCTION();
    if (!applyBonus && m_data->m_bPetActionBlocked && experience > 0) {
        experience = 0;
    }
    return CCreature::ExpInc(experience, false);
}

std::int64_t CPet::SPInc(std::int64_t amount) {
    // Current L2Server 0x00957fcc.
    L2_GUARD_FUNCTION();
    if (m_data->m_bPetActionBlocked && amount > 0) {
        amount = 0;
    }
    return CCreature::SPInc(amount);
}

void CPet::ExpDown(std::int64_t experience) {
    // Current L2Server 0x0095808c.
    L2_GUARD_FUNCTION();
    const PetDataSheet::Record* const record = m_pPetSheet->GetRecord(m_data->m_nLevel);
    if (m_data->m_nExp + experience < record->mExp) {
        experience = record->mExp - m_data->m_nExp;
        m_nDownExp = experience;
    }
    if (experience == 0) {
        m_nDownExp = 0;
    } else {
        ExpInc(experience, false);
    }
}

void CPet::OnDieDropItem(bool reducedRate) {
    // Current L2Server 0x0095cc14.
    L2_GUARD_FUNCTION();
    if (g_nNotDropItemOnUserDie != 0) {
        return;
    }
    User* const master = GetMaster();
    if (master == nullptr || master->m_data->m_nKarma >= 0 ||
        master->m_data->m_nPKCount - master->m_data->m_nPKPardonCount < g_nPKPenaltyCount) {
        return;
    }

    double dropRate = (-static_cast<double>(m_data->m_nKarma) * 3.0) / 2000.0 - 3.0;
    dropRate = std::clamp(dropRate, 0.0, 50.0);
    double equippedDropRate = dropRate / 3.0;
    if (reducedRate) {
        dropRate *= 0.25;
        equippedDropRate *= 0.25;
    }

    std::array<int, 200> candidates{};
    int candidateCount = 0;
    {
        L2_WRITE_GUARD(m_inv);
        CSharedItemData* itemData = m_inv.GetFirst();
        int maximumDropCount = g_nMaxDropItemLimit;
        const int inventoryCount = m_inv.GetTotalCount();
        if (inventoryCount < maximumDropCount) {
            maximumDropCount = inventoryCount;
        }

        int depth = 0;
        while (itemData != nullptr && candidateCount < 200) {
            CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemData->index);
            if (!item) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"Cannot Get item at file[%S], line[%d]",
                //     "pet.cpp",
                //     0xbea)
                // L2_LOG_ERROR(Log,
                //     L"Cannot Get item at file[%S], line[%d]",
                //     "pet.cpp",
                //     0xbea)
                L2_LOG_ERROR(
                    Log, "Pet object %u: death-drop candidate item index %d is unavailable; skipping this entry.",
                    static_cast<unsigned>(m_nId), itemData->index
                );
                itemData = itemData->Next();
                continue;
            }

            CSharedItemConstant* const constant = item->m_const;
            const bool excluded = item->IsQuestItem() ||
                (!item->IsTrade() && constant != nullptr && constant->m_nAttribution >= 0) ||
                (!item->IsDrop() && constant != nullptr && constant->m_nAttribution >= 0) ||
                item->m_data->m_Type == IT_ASSET || (constant != nullptr && constant->m_EtcitemType == 8) ||
                item->IsDuration() || (constant != nullptr && constant->m_bIsPremium) ||
                item->IsPeriodicShapeShiftedItem(master);
            if (!excluded) {
                double rate = dropRate;
                if (item->IsAsset()) {
                    rate = 0.0;
                } else if (item->IsWeapon() && IsEquipedItem(item->m_data->index)) {
                    rate = equippedDropRate;
                }
                if (::genrand64_real1() * 100.0 < rate) {
                    candidates[static_cast<std::size_t>(candidateCount++)] = itemData->index;
                }
            }

            itemData = itemData->Next();
            ++depth;
            if (depth > 0xca) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"depth > %d at file[%s], line[%d]",
                //     0xca,
                //     L"pet.cpp",
                //     0xc35)
                // L2_LOG_ERROR(Log,
                //     L"depth > %d at file[%s], line[%d]",
                //     0xca,
                //     L"pet.cpp",
                //     0xc35)
                L2_LOG_ERROR(
                    Log,
                    "Pet object %u: death-drop inventory traversal reached depth %d above 202; stopping candidate "
                    "selection.",
                    static_cast<unsigned>(m_nId), depth
                );
                break;
            }
        }

        if (candidateCount > 0 && maximumDropCount > 0) {
            CContributeDataForOne* const contribution = new CContributeDataForOne(master);
            contribution->AddTimer(20000, 0);
            const CSmartID contributionId = contribution->GetSmartID();

            const auto dropOne = [&](int itemIndex) {
                CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemIndex);
                if (!item) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"Cannot Get item at file[%S], line[%d]",
                    //     "pet.cpp",
                    //     0xc4a)
                    // L2_LOG_ERROR(Log,
                    //     L"Cannot Get item at file[%S], line[%d]",
                    //     "pet.cpp",
                    //     0xc4a)
                    L2_LOG_ERROR(
                        Log, "Pet object %u cannot drop selected item index %d: item lookup failed.",
                        static_cast<unsigned>(m_nId), itemIndex
                    );
                    return false;
                }

                FVector position = GetPos();
                position.z = static_cast<double>(CWorld::Inst().GetGeo()->FindGround(
                    static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z)
                ));
                constexpr double kCircle = 6.28318530717958647692;
                const double angle = static_cast<double>(static_cast<int>(::genrand64_real2() * 360.0)) * kCircle / 360.0;
                const double distance = static_cast<double>(static_cast<int>(::genrand64_real2() * 16.0 + 30.0));
                position.x += static_cast<double>(static_cast<int>(std::cos(angle) * distance));
                position.y += static_cast<double>(static_cast<int>(std::sin(angle) * distance));

                {
                    L2_GUARD(*item->m_lock);
                    item->m_ContributeDataID = contributionId;
                    item->m_nDroppedTick = runtime::GetTickCount();
                }
                DropItem(item.get(), item->m_data->m_nCount, position, false);
                return true;
            };

            if (candidateCount <= maximumDropCount) {
                for (int index = 0; index < candidateCount; ++index) {
                    dropOne(candidates[static_cast<std::size_t>(index)]);
                }
            } else {
                int dropped = 0;
                int attempts = 0;
                while (dropped < maximumDropCount && candidateCount > 0) {
                    if (attempts++ > 0xca) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"depth>%d at file[%s], line[%d]",
                        //     0xca,
                        //     L"pet.cpp",
                        //     0xc6c)
                        // L2_LOG_ERROR(Log,
                        //     L"depth>%d at file[%s], line[%d]",
                        //     0xca,
                        //     L"pet.cpp",
                        //     0xc6c)
                        L2_LOG_ERROR(
                            Log,
                            "Pet object %u: death-drop selection stopped at attempt counter %d; %d drop requests issued, "
                            "limit %d, %d candidates remain.",
                            static_cast<unsigned>(m_nId), attempts, dropped, maximumDropCount, candidateCount
                        );
                        break;
                    }
                    const int selected = static_cast<int>(::genrand64_real2() * static_cast<double>(candidateCount));
                    const int itemIndex = candidates[static_cast<std::size_t>(selected)];
                    if (itemIndex > 0 && dropOne(itemIndex)) {
                        ++dropped;
                        for (int index = selected; index + 1 < candidateCount; ++index) {
                            candidates[static_cast<std::size_t>(index)] = candidates[static_cast<std::size_t>(index + 1)];
                        }
                        --candidateCount;
                    }
                }
            }
        }
    }
}

void CPet::TimerExpired(int id) {
    // Current L2Server 0x0095f9fc.
    L2_GUARD_FUNCTION();
    if (m_bDeleting || m_Deleted != 0) {
        return;
    }

    User* const master = GetMaster();
    const std::uint32_t now = runtime::GetTickCount();
    if (m_data->m_bIsAlive != 0 && m_MealTick + 10000U < now) {
        m_MealTick = now;
        EatFood(-(m_data->m_bIsInCombatMode ? m_pPetData->mConsumeMealInBattle : m_pPetData->mConsumeMealInNormal));

        const int hungryMeal = runtime::MultiplyInt32Wrapping(m_data->m_MaxMeal, m_pPetData->m_nHungryLimit) / 100;
        for (const int foodClassId : m_pPetData->m_nFoodClassId) {
            if (foodClassId == 0) {
                break;
            }
            if (hungryMeal <= 0 || m_data->m_Meal >= hungryMeal) {
                continue;
            }
            CItem* const food = m_inv.GetFirstItemByClassID(foodClassId, false, false);
            if (food == nullptr) {
                continue;
            }
            UseItem(*food, 0, false, 0);
            if (master != nullptr) {
                master->GetSocket()->Send("chccd", 0x62, SystemMessageId::HUNGRY_PET_CONSUMED_FOOD, 1, 3, foodClassId);
            }
            break;
        }
    }

    if (m_data->m_bIsAlive == 0) {
        if (m_bZolaHungry) {
            if (m_ZolaHungryTerm == 0) {
                m_ZolaHungryTerm = now - m_ZolaHungryStartTick;
            }
            m_ZolaHungryStartTick = now - m_ZolaHungryTerm;
        }
    } else {
        m_ZolaHungryTerm = 0;
        const int maximumMeal = std::max(m_data->m_MaxMeal, 1);
        const int mealPercent =
            static_cast<int>(static_cast<double>(m_data->m_Meal) / static_cast<double>(maximumMeal) * 100.0);

        if (m_data->m_bPetActionBlocked) {
            int state = 0;
            if (mealPercent < 1) {
                if (m_DBSaveTick == (m_DBSaveTick / 15U) * 15U) {
                    SendSystemMessage(SystemMessageId::HUNGRY_PET_IGNORES_ORDERS);
                }
                state = 100;
                State(state);
            } else {
                m_data->m_bPetActionBlocked = false;
                state = 101;
                State(state);
            }
            if (g_ptlUserMap != nullptr) {
                g_ptlUserMap->BroadcastToNeighborDist(
                    this, 0x5000, GetPos(), 0xc00, "chdd", 0xfe, 0xbf, m_data->m_nId, state
                );
            }
        }

        if (m_bZolaHungry && mealPercent > 10) {
            m_bZolaHungry = false;
            SendSystemMessage(SystemMessageId::PET_ATE_BUT_REMAINS_HUNGRY);
        }
        if (mealPercent < 1) {
            if (m_pPetData->m_nHungryLimit != 1 && !m_data->m_bPetActionBlocked) {
                m_data->m_bPetActionBlocked = true;
            }
            if (m_SysMsgTick + 10000U < now) {
                m_SysMsgTick = now;
                if (m_pPetData->m_nHungryLimit == 1) {
                    SendSystemMessage(SystemMessageId::PET_DEPARTURE_TIME_APPROACHING);
                }
            }
            if (!m_bZolaHungry) {
                m_bZolaHungry = true;
                m_ZolaHungryStartTick = now;
            }
        } else if (mealPercent < 11 && m_pPetData->m_nHungryLimit != 1 && m_DBSaveTick == (m_DBSaveTick / 45U) * 45U) {
            SendSystemMessage(SystemMessageId::PET_CANNOT_BE_CONTROLLED_WITH_EMPTY_HUNGER_GAUGE);
        }

        // The duplicated hungry-limit comparison is literal current code:
        // non-premium pets set m_bStarved but are not deleted here.
        if (m_bZolaHungry && now - m_ZolaHungryStartTick > 300000U &&
            (m_pPetData->m_nHungryLimit == 1 || (m_bStarved = true, m_pPetData->m_nHungryLimit == 1))) {
            SendSystemMessage(SystemMessageId::PET_LEAVING_TIME_EXPIRED);
            g_npcserver.Send("cddd", 0x8f, static_cast<int>(Id()), m_pPetData->m_nHungryLimit == 1 ? 1 : 0, 0);
            db.DeletePet(
                m_MasterWorldID, static_cast<int>(m_MasterUserSID), static_cast<int>(m_MasterUserDBID), GetDBID(), 0, Level()
            );
            g_PetManager.DeletePet(m_MasterUserDBID, m_MasterUserSID);
            return;
        }
    }

    CSummon::TimerExpired(id);
    ++m_DBSaveTick;
    runtime::raw_atomic::AddI32(&m_nSaveToDBTick, 1);
    if (runtime::raw_atomic::LoadI32Relaxed(&m_nSaveToDBTick) >= 45 && m_data->m_bIsActive && master != nullptr &&
        !master->IsPetSummoning()) {
        runtime::raw_atomic::ExchangeI32(&m_nSaveToDBTick, 0);
        db.SavePetInfo(this, nullptr);
    }

    if (master != nullptr) {
        if (m_data->m_bIsAlive == 0) {
            return;
        }
        m_InvalidMasterTick = now;
        const FVector ownPosition = GetPos();
        const FVector masterPosition = master->GetPos();
        const double deltaX = ownPosition.x - masterPosition.x;
        const double deltaY = ownPosition.y - masterPosition.y;
        const double deltaZ = ownPosition.z - masterPosition.z;
        const double distance = std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
        if (distance >= 5000.0 && master->m_data->m_bIsAlive != 0 && m_data->m_bIsActive) {
            StopMove();
            TeleportToLocation(
                static_cast<int>(masterPosition.x), static_cast<int>(masterPosition.y), static_cast<int>(masterPosition.z),
                master->GetInZoneID(), 0x10000, static_cast<TeleportSourceType::Enum>(0x3d), nullptr, 0
            );
        }
        return;
    }

    if (m_data->m_bIsAlive != 0 && now - m_InvalidMasterTick > 60000U) {
        g_PetManager.DeletePet(m_MasterUserDBID, m_MasterUserSID);
    }
}

int CPet::GetInventoryLimit() { return 12; }

int CPet::GetQuestInventoryLimit() { return 0; }

int CPet::GetItemCount() {
    // Current L2Server 0x0095dcb0.
    L2_GUARD_FUNCTION();

    int itemCount = 0;
    {
        L2_READ_GUARD(m_inv);
        CItemFactory& itemFactory = CItemFactory::Inst();
        CSharedItemData* itemData = m_inv.GetFirst();
        while (itemData != nullptr) {
            CSPointer<CItem> item = itemFactory.FindObjectSP(itemData->index);
            if (item) {
                ++itemCount;
            }
            itemData = itemFactory.GetNext(itemData);
        }
    }
    return itemCount;
}

void CPet::AutoEquip() {
    // Current L2Server 0x00958420.
    L2_GUARD_FUNCTION();
    for (const std::int32_t itemDatabaseId : m_nSlots) {
        CItem* const object = m_inv.GetByDBID(itemDatabaseId);
        if (object != nullptr && object->IsItem()) {
            EquipItem(object->CastItem(), ST_MAX);
        }
    }
}

RideType CPet::GetRideType() { return m_rideType; }

int CPet::GetLevel(const int petIndex, const std::int64_t experience) {
    // Current L2Server 0x00956334.
    L2_GUARD_FUNCTION();
    int level = 120;
    PetDataSheet* const dataSheet = PetDataSheetTable::Inst().GetPetDataSheetByIndex(petIndex);
    do {
        if (PetDataSheet::Record* const record = dataSheet->GetRecord(level); record->mExp <= experience) {
            break;
        }
        --level;
    } while (level > 1);
    return level;
}

int CPet::GetMaxMeal() const {
    L2_GUARD_FUNCTION();
    return m_pPetData->mMaxMeal;
}

bool CPet::IsSyncLevel() const {
    L2_GUARD_FUNCTION();
    return m_pPetSheet->IsSyncLevel();
}

void CPet::SendPetItemList(bool changedItemsOnly) {
    L2_GUARD_FUNCTION();

    CPacketHelperEx<20480> packet;
    int itemCount = 0;
    User* const master = GetMaster();
    if (master == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] SendPetItemList failed. cannot find master[%d]",
        //     L"pet.cpp",
        //     0xe25,
        //     static_cast<int>(m_MasterUserDBID))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] SendPetItemList failed. cannot find master[%d]",
        //     L"pet.cpp",
        //     0xe25,
        //     static_cast<int>(m_MasterUserDBID))
        L2_LOG_ERROR(
            Log,
            "Cannot send inventory for pet object %u: master character %u is unavailable; changed-item tracking is cleared.",
            static_cast<unsigned>(m_nId), static_cast<unsigned>(m_MasterUserDBID)
        );
        m_inv.ReleaseMapChangedItems();
        return;
    }

    CUserSocket* const socket = master->GetSocket();
    std::optional<std::map<int, int>> changedItemsStorage;
    {
        L2_WRITE_GUARD(m_inv.lockMapChangedItems);
        changedItemsStorage.emplace(m_inv.MapChangedItems);
    }
    const auto& changedItems = *changedItemsStorage;

    {
        L2_READ_GUARD(m_inv);
        CItemFactory& itemFactory = CItemFactory::Inst();
        int depth = 0;
        if (!changedItemsOnly) {
            CSharedItemData* itemData = m_inv.GetFirst();
            while (itemData != nullptr) {
                ++depth;
                if (depth > 0xca) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"depth > %d at file[%s], line[%d]",
                    //     0xca,
                    //     L"pet.cpp",
                    //     0xe6a)
                    // L2_LOG_ERROR(Log,
                    //     L"depth > %d at file[%s], line[%d]",
                    //     0xca,
                    //     L"pet.cpp",
                    //     0xe6a)
                    L2_LOG_ERROR(
                        Log,
                        "Pet object %u: full inventory traversal exceeded 202 entries at depth %d; sending the assembled "
                        "prefix.",
                        static_cast<unsigned>(m_nId), depth
                    );
                    break;
                }

                CSPointer<CItem> item = itemFactory.FindObjectSP(itemData->index);
                if (!item) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] FindObject Failed",
                    //     L"pet.cpp",
                    //     0xe72)
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d] FindObject Failed",
                    //     L"pet.cpp",
                    //     0xe72)
                    L2_LOG_ERROR(
                        Log, "Pet object %u: full inventory item index %d is unavailable; skipping this entry.",
                        static_cast<unsigned>(m_nId), itemData->index
                    );
                    itemData = itemFactory.GetNext(itemData);
                    continue;
                }

                std::optional<CItemAssembler> assemblerStorage;
                const int assembled = [&] {
                    L2_GUARD(*item->m_lock);
                    ++itemCount;
                    SlotType candidateSlot = ST_UNDERWEAR;
                    while (candidateSlot < ST_MAX && (ST2SBT[candidateSlot] & item->m_data->m_Slot) == 0) {
                        candidateSlot = static_cast<SlotType>(static_cast<int>(candidateSlot) + 1);
                    }
                    const SlotType equippedSlot = IsEquiped(item->m_data->index, candidateSlot);
                    auto& assembler = assemblerStorage.emplace(*master, *item.get(), equippedSlot != ST_MAX);
                    return assembler.AssembleItemInfo(packet);
                }();
                if (assembled < 1) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"Assembe Error at file[%s], line[%d], "
                    //     L"assemble result = [%d], total length = [%d]",
                    //     L"pet.cpp",
                    //     0xe8d,
                    //     assembled,
                    //     packet.GetLength())
                    // L2_LOG_ERROR(Log,
                    //     L"Assembe Error at file[%s], line[%d], "
                    //     L"assemble result = [%d], total length = [%d]",
                    //     L"pet.cpp",
                    //     0xe8d,
                    //     assembled,
                    //     packet.GetLength())
                    L2_LOG_ERROR(
                        Log,
                        "Pet object %u: full inventory item assembly returned %d, packet length %d; processing continues "
                        "without reducing the declared item count.",
                        static_cast<unsigned>(m_nId), assembled, packet.GetLength()
                    );
                }
                itemData = itemFactory.GetNext(itemData);
            }
        } else {
            for (const auto& changedItem : changedItems) {
                ++depth;
                if (depth > 0xca) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"depth > %d at file[%s], line[%d]",
                    //     0xca,
                    //     L"pet.cpp",
                    //     0xe3a)
                    // L2_LOG_ERROR(Log,
                    //     L"depth > %d at file[%s], line[%d]",
                    //     0xca,
                    //     L"pet.cpp",
                    //     0xe3a)
                    L2_LOG_ERROR(
                        Log,
                        "Pet object %u: changed inventory traversal exceeded 202 entries at depth %d; sending the assembled "
                        "prefix.",
                        static_cast<unsigned>(m_nId), depth
                    );
                    break;
                }

                CSPointer<CItem> item = itemFactory.FindObjectSP(changedItem.first);
                if (!item) {
                    continue;
                }

                std::optional<CItemAssembler> assemblerStorage;
                const int assembled = [&] {
                    L2_GUARD(*item->m_lock);
                    packet.Assemble("h", static_cast<int>(static_cast<std::int16_t>(changedItem.second)));
                    SlotType candidateSlot = ST_UNDERWEAR;
                    while (candidateSlot < ST_MAX && (ST2SBT[candidateSlot] & item->m_data->m_Slot) == 0) {
                        candidateSlot = static_cast<SlotType>(static_cast<int>(candidateSlot) + 1);
                    }
                    const SlotType equippedSlot = IsEquiped(item->m_data->index, candidateSlot);
                    auto& assembler = assemblerStorage.emplace(*master, *item.get(), equippedSlot != ST_MAX);
                    return assembler.AssembleItemInfo(packet);
                }();
                if (assembled < 1) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"Assembe Error at file[%s], line[%d], "
                    //     L"assemble result = [%d], total length = [%d]",
                    //     L"pet.cpp",
                    //     0xe5a,
                    //     assembled,
                    //     packet.GetLength())
                    // L2_LOG_ERROR(Log,
                    //     L"Assembe Error at file[%s], line[%d], "
                    //     L"assemble result = [%d], total length = [%d]",
                    //     L"pet.cpp",
                    //     0xe5a,
                    //     assembled,
                    //     packet.GetLength())
                    L2_LOG_ERROR(
                        Log,
                        "Pet object %u: changed inventory item assembly returned %d, packet length %d; processing and item "
                        "counting continue.",
                        static_cast<unsigned>(m_nId), assembled, packet.GetLength()
                    );
                }
                ++itemCount;
            }
        }
    }
    m_inv.ReleaseMapChangedItems();
    if (!changedItemsOnly) {
        socket->Send(
            "chb", 0xb3, static_cast<int>(static_cast<std::int16_t>(itemCount)), packet.GetLength(), packet.GetPacket()
        );
    } else if (packet.GetLength() == 0) {
        socket->Send("ch", 0xb4, 0);
    } else {
        socket->Send(
            "chb", 0xb4, itemCount, static_cast<int>(static_cast<std::int16_t>(packet.GetLength())), packet.GetPacket()
        );
    }

    m_inv.ValidateCarringWeight();
    ParamChanged(NPCT_PETITEMLIST);
}

void CPet::SetLevel(int level) {
    L2_GUARD_FUNCTION();
    if (!IsSyncLevel()) {
        SetExp(m_pPetSheet->GetRecord(level)->mExp);
    } else {
        OnValidateLevel();
    }
}

bool CPet::ValidateLevel() {
    // Current L2Server 0x0095b420.
    L2_GUARD_FUNCTION();
    if (IsSyncLevel()) {
        return false;
    }

    const std::int64_t experience = m_data->m_nExp;
    int level = std::clamp(m_data->m_nLevel, 1, 120);
    bool changed = false;

    PetDataSheet::Record* record = m_pPetSheet->GetRecord(level);
    int nextLevel = level + 1;
    PetDataSheet::Record* nextRecord = m_pPetSheet->GetRecord(nextLevel);

    if (level > 1 && experience < record->mExp) {
        int destinationLevel = level;
        do {
            --destinationLevel;
            if (destinationLevel < 2) {
                break;
            }
            record = m_pPetSheet->GetRecord(destinationLevel);
        } while (experience < record->mExp);

        if (destinationLevel != level) {
            LevelDown(static_cast<unsigned int>(level - destinationLevel));
            changed = true;
        }
    }

    if (nextRecord->mExp <= experience) {
        while (nextLevel < 121) {
            nextRecord = m_pPetSheet->GetRecord(nextLevel);
            if (experience < nextRecord->mExp) {
                break;
            }
            ++nextLevel;
        }
        if (nextLevel - level != 1) {
            LevelUp(static_cast<unsigned int>(nextLevel - level - 1));
            changed = true;
        }
    }

    if (!changed) {
        OnValidateLevel();
    }
    return true;
}

bool CPet::ValidateSyncLevel() {
    // Current L2Server 0x0095b5c0.
    L2_GUARD_FUNCTION();
    if (!IsSyncLevel() || m_data->m_nDBID == 0) {
        return false;
    }

    User* const master = GetMaster();
    if (master == nullptr) {
        return false;
    }

    const int masterLevel = std::clamp(master->m_data->m_nLevel, 1, 120);
    const int petLevel = m_data->m_nLevel;
    if (petLevel == masterLevel) {
        OnValidateLevel();
    } else if (petLevel < masterLevel) {
        LevelUp(static_cast<unsigned int>(masterLevel - petLevel));
    } else {
        LevelDown(static_cast<unsigned int>(petLevel - masterLevel));
    }
    return true;
}

void CPet::OnValidateLevel() {
    // Current L2Server 0x0095b6f4.
    L2_GUARD_FUNCTION();
    const int level = m_data->m_nLevel;
    PetDataSheet::Record* const record = m_pPetSheet->GetRecord(level);
    m_data->m_nMasterExpRatio = record->mGetExpType;

    if (m_pPetData == nullptr || m_pPetData->mLevel != level) {
        m_pPetData = m_pPetSheet->GetRecord(level);
        ValidateChanged(std::int64_t{VB_MAX});
        SendSummonStatusToMaster(5);
        if (User* const master = GetMaster()) {
            master->PetLevelChanged(static_cast<int>(GetDBID()), level);
        }
        runtime::raw_atomic::AddI32(&m_nSaveToDBTick, 0x2d);
    } else {
        ValidateChanged(std::int64_t{VB_HPMPCP});
    }

    m_data->m_EvolutionId = m_pPetSheet->CheckEvolutionID(level);
}

bool CPet::ValidateOrgHP() {
    L2_GUARD_FUNCTION();
    m_data->m_dOrgHP = m_pPetData->mdOrgHP;
    ValidateBaseHP();
    if (m_data->m_dBaseHP <= m_data->m_dHP && m_data->m_dHP != m_data->m_dBaseHP) {
        m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dHP, m_data->m_dBaseHP, 11);
    }
    return true;
}

bool CPet::ValidateOrgMP() {
    L2_GUARD_FUNCTION();
    m_data->m_dOrgMP = m_pPetData->mdOrgMP;
    ValidateBaseMP();
    if (m_data->m_dBaseMP <= m_data->m_dMP && m_data->m_dMP != m_data->m_dBaseMP) {
        m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dMP, m_data->m_dBaseMP, 12);
    }
    return true;
}

bool CPet::ValidatePAttack() {
    L2_GUARD_FUNCTION();
    CCreature::ValidatePAttack();
    return true;
}

bool CPet::ValidateMAttack() {
    L2_GUARD_FUNCTION();
    CCreature::ValidateMAttack();
    return true;
}

bool CPet::ValidatePDefend() {
    L2_GUARD_FUNCTION();
    CCreature::ValidatePDefend();
    return true;
}

bool CPet::ValidateMDefend() {
    L2_GUARD_FUNCTION();
    CCreature::ValidateMDefend();
    return true;
}

bool CPet::ValidateAttackSpeed() {
    L2_GUARD_FUNCTION();
    CCreature::ValidateAttackSpeed();
    if (Hungry()) {
        double* const attackSpeed = &m_data->m_dParam[VALIDATE::PATTACKSPEED];
        m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(*attackSpeed, *attackSpeed * 0.5, 0x3b);
    }
    return true;
}

bool CPet::ValidateMUseSpeed() {
    L2_GUARD_FUNCTION();
    CCreature::ValidateMUseSpeed();
    return true;
}

bool CPet::ValidatePUseSpeed() {
    L2_GUARD_FUNCTION();
    CCreature::ValidatePUseSpeed();
    return true;
}

bool CPet::ValidatePCriticalProb() {
    L2_GUARD_FUNCTION();
    CCreature::ValidatePCriticalProb();
    return true;
}

bool CPet::ValidateOrgHPRegen() {
    L2_GUARD_FUNCTION();
    m_data->m_dOrgHPRegen = m_pPetData->mdOrgHPRegen;
    CCreature::ValidateHPRegen();
    return true;
}

bool CPet::ValidateOrgMPRegen() {
    L2_GUARD_FUNCTION();
    m_data->m_dOrgMPRegen = m_pPetData->mdOrgMPRegen;
    CCreature::ValidateMPRegen();
    return true;
}

bool CPet::ValidateOrgPAttack() {
    // Current L2Server 0x00956554.
    L2_GUARD_FUNCTION();
    const int weapon = runtime::Win64CvttDoubleToInt32(EquipedWeaponValidate(VALIDATE::PATTACK));
    const int armor = runtime::Win64CvttDoubleToInt32(EquipedArmorsValidate(VALIDATE::PATTACK));
    m_data->m_nOrgParam[VALIDATE::PATTACK] =
        runtime::Win64CvttDoubleToInt32(m_pPetData->mdOrgPAttack + static_cast<double>(weapon + armor));
    ValidatePAttack();
    return true;
}

bool CPet::ValidateOrgPDefend() {
    // Current L2Server 0x00956784.
    L2_GUARD_FUNCTION();
    const int weapon = runtime::Win64CvttDoubleToInt32(EquipedWeaponValidate(VALIDATE::PDEFEND));
    const int armor = runtime::Win64CvttDoubleToInt32(EquipedArmorsValidate(VALIDATE::PDEFEND));
    m_data->m_nOrgParam[VALIDATE::PDEFEND] =
        runtime::Win64CvttDoubleToInt32(m_pPetData->mdOrgPDefend + static_cast<double>(weapon + armor));
    ValidatePDefend();
    return true;
}

bool CPet::ValidateOrgMAttack() {
    // Current L2Server 0x0095666c.
    L2_GUARD_FUNCTION();
    const int weapon = runtime::Win64CvttDoubleToInt32(EquipedWeaponValidate(VALIDATE::MATTACK));
    const int armor = runtime::Win64CvttDoubleToInt32(EquipedArmorsValidate(VALIDATE::MATTACK));
    m_data->m_nOrgParam[VALIDATE::MATTACK] =
        runtime::Win64CvttDoubleToInt32(m_pPetData->mdOrgMAttack + static_cast<double>(weapon + armor));
    ValidateMAttack();
    return true;
}

bool CPet::ValidateOrgMDefend() {
    // Current L2Server 0x00956898.
    L2_GUARD_FUNCTION();
    const int weapon = runtime::Win64CvttDoubleToInt32(EquipedWeaponValidate(VALIDATE::MDEFEND));
    const int armor = runtime::Win64CvttDoubleToInt32(EquipedArmorsValidate(VALIDATE::MDEFEND));
    m_data->m_nOrgParam[VALIDATE::MDEFEND] =
        runtime::Win64CvttDoubleToInt32(m_pPetData->mdOrgMDefend + static_cast<double>(weapon + armor));
    ValidateMDefend();
    return true;
}

bool CPet::Morph(int classId) {
    // Current L2Server 0x0095e2fc.
    L2_GUARD_FUNCTION();
    return CSummon::Morph(classId);
}

void CPet::UnMorph() {
    // Current L2Server 0x0095e2f4.
    L2_GUARD_FUNCTION();
    CSummon::UnMorph();
}

void CPet::ChangeMorph() {
    // Current L2Server 0x0095e2ec.
    L2_GUARD_FUNCTION();
    CSummon::ChangeMorph();
}

bool CPet::ValidateSpeed() {
    // Current L2Server 0x0095b980.
    L2_GUARD_FUNCTION();
    double speed = (GetMoveModifier() * static_cast<double>(m_data->m_nOrgParam[VALIDATE::SPEED]) * m_dGMSpeed *
                           m_SkillMod.m_Mods[CCreatureSkillMod::Speed].m_Per * m_AreaMod.m_dSpeedModByArea +
                       m_SkillMod.m_Mods[CCreatureSkillMod::Speed].m_Diff) *
        m_dSpeedModByWeight;
    if (Hungry()) {
        speed *= 0.5;
    }
    m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dParam[VALIDATE::SPEED], speed, 0x3b);

    if (speed < 0.0 || speed > 1200.0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Validate Speed dSpeed[%f]",
        //     speed)
        // L2_LOG_ERROR(Log,
        //     L"Validate Speed dSpeed[%f]",
        //     speed)
        L2_LOG_ERROR(
            Log,
            "Pet object %u: calculated speed %.17g is outside 0..1200; value was already stored, validation fails before "
            "clamping.",
            static_cast<unsigned>(m_nId), speed
        );
        runtime::process::ReportCallStack(LOG_ERROR, false);
        return false;
    }
    ValidateGradePenalty(VALIDATE::SPEED);
    m_data->m_dParam[VALIDATE::SPEED] = std::clamp(m_data->m_dParam[VALIDATE::SPEED], 1.0, 350.0);
    return true;
}

bool CPet::ValidateLevelUP() {
    // Current L2Server 0x009576f0.
    L2_GUARD_FUNCTION();
    CCreature::ValidateLevelUP();
    User* const master = GetMaster();
    CParty* const party = master == nullptr ? nullptr : master->GetParty();
    if (party != nullptr) {
        party->UpdatePetWindowPacket(CastSummon());
    }
    BroadcastHPMP(0, 0);
    return false;
}

int CPet::EquipItem(CItem* item, ::SlotType slotType) {
    // Current L2Server 0x0095bb54.
    L2_GUARD_FUNCTION();
    const int result = CNPC::EquipItem(item, slotType);
    runtime::raw_atomic::AddI32(&m_nSaveToDBTick, 0x2d);
    return result;
}

void CPet::UnequipWeapon(bool force) {
    // Current L2Server 0x00958258.
    L2_GUARD_FUNCTION();
    CSharedItemData* const weapon = GetEquipedWeapon();
    if (weapon == nullptr) {
        return;
    }
    if (force) {
        m_SkillMod.m_nBlockedWeaponIdx = weapon->index;
    }
    SlotType slot = ST_UNDERWEAR;
    while (slot < ST_MAX && (ST2SBT[static_cast<int>(slot)] & weapon->m_Slot) == 0) {
        slot = static_cast<SlotType>(static_cast<int>(slot) + 1);
    }
    UnequipItem(slot, false);
}

void CPet::RestoreUnequipedWeapon() {
    // Current L2Server 0x0095db5c.
    L2_GUARD_FUNCTION();
    const int itemIndex = m_SkillMod.m_nBlockedWeaponIdx;
    if (itemIndex == -1) {
        return;
    }
    CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemIndex);
    if (item && m_inv.Have(item->m_data->index)) {
        EquipItem(item.get(), ST_MAX);
    }
    m_SkillMod.m_nBlockedWeaponIdx = -1;
}

bool CPet::DropItem(CItem* item, std::int64_t count, FVector& position, bool unknown) {
    // Current L2Server 0x0095789c.
    L2_GUARD_FUNCTION();
    if (item == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [CItem* pItem] is nullptr",
        //     L"pet.cpp",
        //     0xb5e)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [CItem* pItem] is nullptr",
        //     L"pet.cpp",
        //     0xb5e)
        L2_LOG_ERROR(
            Log, "Pet object %u cannot drop %lld items: item pointer is null.", static_cast<unsigned>(m_nId),
            static_cast<long long>(count)
        );
        return false;
    }
    if (!CanDropItem(item, count, position, unknown)) {
        return false;
    }

    SlotType slot = ST_UNDERWEAR;
    while (slot < ST_MAX && (ST2SBT[static_cast<int>(slot)] & item->m_data->m_Slot) == 0) {
        slot = static_cast<SlotType>(static_cast<int>(slot) + 1);
    }
    slot = IsEquiped(item->m_data->index, slot);
    if (slot != ST_MAX && slot != static_cast<SlotType>(-1)) {
        UnequipItem(slot, false);
    }

    item->m_data->SetPos(position);
    m_inv.Pop(item->m_data->index, false);
    User* const master = GetMaster();
    if (master != nullptr) {
        CLogFormatter formatter{};
        formatter.WriteLog2_1(0x6e, master, this, GetDBID(), 0, 0, item->m_data->m_nClassID, count, 0, item->GetDBID());
    }
    db.RequestPetDropItem(master, this, item, GetInZoneID(), count);
    return true;
}

bool CPet::GetItem(CItem* item, bool unknown) {
    // Current L2Server 0x00961ac8.
    L2_GUARD_FUNCTION();
    CCursedWeaponMgr& manager = CCursedWeaponMgr::inst();
    if (item != nullptr && manager.FindConst(item->m_data->m_nClassID) != nullptr) {
        return false;
    }
    return CCreature::GetItem(item, unknown);
}

bool CPet::AddItemToInventory2(CItem* item) {
    // Current L2Server 0x00955ef0.
    L2_GUARD_FUNCTION();
    if (item == nullptr) {
        return false;
    }
    User* const master = GetMaster();
    db.PetGetItem(master == nullptr ? 0U : master->Id(), *this, item);
    return true;
}

bool CPet::CanDoAction_china() {
    // Current L2Server 0x009588a8.
    L2_GUARD_FUNCTION();
    User* const master = GetMaster();
    return master == nullptr || master->CanDoAction_china();
}

void CPet::AnnouncePickItem(CItem* item) {
    // Current L2Server 0x0095e39c.
    L2_GUARD_FUNCTION();
    if (item == nullptr) {
        return;
    }
    User* const master = GetMaster();
    if (master == nullptr) {
        return;
    }

    const int itemClassId = item->m_nClassId;
    const std::int64_t count = item->m_data->m_nCount;
    const int enchanted = item->m_data->GetEnchanted();
    CParty* const party = master->GetParty();

    const auto sendAdena = [&](User* recipient) {
        if (recipient == nullptr || !recipient->IsPC()) {
            return;
        }
        if (recipient->Id() == master->Id()) {
            recipient->GetSocket()->Send("chccQ", 0x62, SystemMessageId::PET_PICKED_UP_ADENA, 1, 6, count);
        } else {
            recipient->GetSocket()->Send(
                "chccScQ", 0x62, SystemMessageId::CHARACTER_PET_OBTAINED_ADENA, 2, 0x0c, master->GetName(), 6, count
            );
        }
    };

    const auto sendItem = [&](User* recipient) {
        if (recipient == nullptr || !recipient->IsPC()) {
            return;
        }
        const bool self = recipient->Id() == master->Id();
        if (self) {
            if (count < 2) {
                if (enchanted < 1) {
                    recipient->GetSocket()->Send("chccd", 0x62, SystemMessageId::PET_PICKED_UP_ITEM, 1, 3, itemClassId);
                } else {
                    recipient->GetSocket()->Send(
                        "chccccd", 0x62, SystemMessageId::PET_PICKED_UP_ENCHANTED_ITEM, 2, 0x13,
                        static_cast<int>(static_cast<std::int8_t>(enchanted)), 3, itemClassId
                    );
                }
            } else {
                recipient->GetSocket()->Send(
                    "chccdcQ", 0x62, SystemMessageId::PET_PICKED_UP_MULTIPLE_ITEMS, 2, 3, itemClassId, 6, count
                );
            }
            return;
        }

        if (count < 2) {
            if (enchanted < 1) {
                recipient->GetSocket()->Send(
                    "chccScd", 0x62, SystemMessageId::CHARACTER_PET_OBTAINED_ITEM, 2, 0x0c, master->GetName(), 3, itemClassId
                );
            } else {
                recipient->GetSocket()->Send(
                    "chccScccd", 0x62, SystemMessageId::CHARACTER_PET_OBTAINED_ENCHANTED_ITEM, 3, 0x0c, master->GetName(),
                    0x13, static_cast<int>(static_cast<std::int8_t>(enchanted)), 3, itemClassId
                );
            }
        } else {
            recipient->GetSocket()->Send(
                "chccScdcQ", 0x62, SystemMessageId::CHARACTER_PET_OBTAINED_MULTIPLE_ITEMS, 3, 0x0c, master->GetName(), 3,
                itemClassId, 6, count
            );
        }
    };

    if (party != nullptr) {
        std::vector<User*> members;
        party->GetAllMember(members);
        for (User* const member : members) {
            if (itemClassId == 0x39) {
                sendAdena(member);
            } else {
                sendItem(member);
            }
        }
    } else if (itemClassId == 0x39) {
        sendAdena(master);
    } else {
        sendItem(master);
    }

    if (item->IsWeapon() || item->IsArmor() || item->IsAccessary()) {
        const FVector position = GetPos();
        if (enchanted < 1) {
            g_ptlUserMap->BroadcastToNeighborDist(
                this, 0x5000, position, 0x5dc, "chccScd", 0x62, 0x5ff, 2, 0x0c, master->GetName(), 3, itemClassId
            );
        } else {
            g_ptlUserMap->BroadcastToNeighborDist(
                this, 0x5000, position, 0x5dc, "chccScccd", 0x62, 0x600, 3, 0x0c, master->GetName(), 0x13,
                static_cast<int>(static_cast<std::int8_t>(enchanted)), 3, itemClassId
            );
        }
    }
}

bool CPet::Die(CCreature* killer, int reason) {
    // Current L2Server 0x00961bb0.
    L2_GUARD_FUNCTION();
    if (!mTrLock.Begin()) {
        return false;
    }

    int resurrectionSpecial;
    bool resurrectionZoneMatches;
    std::optional<std::vector<int>> resurrectionZonesStorage;
    {
        L2_GUARD(mTrLock, (scoped_lock_detail::AdoptOwnerMethod<&TransactionLock::End>{}));
        resurrectionSpecial = m_SkillDurationMod.m_nResurrectionSpecial;
        const auto& resurrectionZones = resurrectionZonesStorage.emplace(m_SkillDurationMod.m_vResurrectionInstantZoneIds);
        resurrectionZoneMatches = resurrectionZones.empty() ||
            std::find(resurrectionZones.begin(), resurrectionZones.end(), static_cast<int>(m_data->m_nInstantZoneTypeID)) !=
                resurrectionZones.end();

        if (!CCreature::Die(killer, reason)) {
            return false;
        }
    }

    g_npcserver.Send("cdSd", 5, static_cast<int>(Id()), u"", 0);
    if (m_bIsGMKill) {
        return false;
    }
    if (m_nDeathCount.fetch_add(1, std::memory_order_seq_cst) + 1 > 1) {
        return false;
    }
    if (killer != nullptr && killer->IsPCOrPCRelatedNPC() && !AreaBasedRule::CastBadSkill(this)) {
        m_data->SetHP(1.0);
        return false;
    }

    User* const master = GetMaster();
    User* const killerOwner = killer == nullptr ? nullptr : killer->GetUserOrMaster();
    const bool skipDropAndPenalty = killerOwner != nullptr && killerOwner->m_nCursedWeaponClassId > 0;
    bool mayDropItem = killer != nullptr && !skipDropAndPenalty;
    if (mayDropItem) {
        mayDropItem = !(((!killer->IsNPC() || killer->IsSummonOrPet()) && m_data->m_nKarma >= 0) || IsInDuel());
    }
    if (!skipDropAndPenalty) {
        bool cursedWeaponParticipant = master != nullptr && master->m_nCursedWeaponClassId > 0;
        if (!cursedWeaponParticipant && killer != nullptr) {
            if (killer->IsPC()) {
                User* const killerUser = killer->CastUser();
                cursedWeaponParticipant = killerUser != nullptr && killerUser->m_nCursedWeaponClassId > 0;
            }
            if (killer->IsSummonOrPet() && killer->CastSummon()->GetMaster() != nullptr) {
                User* const killerMaster = killer->CastSummon()->GetMaster();
                cursedWeaponParticipant = killerMaster != nullptr && killerMaster->m_nCursedWeaponClassId > 0;
            }
        }
        if (cursedWeaponParticipant) {
            mayDropItem = false;
        }
    }
    if (mayDropItem) {
        OnDieDropItem(
            m_AreaMod.m_bIsInBattleField || (master != nullptr && master->m_EventMatchUser.m_byEventMatchTeamId != 0)
        );
    }

    bool penaltyApplied = false;
    if (!skipDropAndPenalty && AreaBasedRule::CastBadSkill(this) && !IsInDuel() &&
        (g_bDiePenalty || master == nullptr || !master->IsPC() || master->IsEinhasadeTransformed() ||
            master->m_data->m_nKarma < 0) &&
        m_pPetData->m_bIsPenalty) {
        std::int64_t penalty = static_cast<std::int64_t>(CPetManager::GetExpPenalty(m_petIndex, m_data->m_nLevel));

        User* const killerUser = killer != nullptr && killer->IsPC() ? killer->CastUser() : nullptr;
        if (killerUser != nullptr && master != nullptr && master->CheckDeclareWarBoth(killerUser)) {
            if (!m_AreaMod.m_bIsInPvPZone) {
                penalty /= 4;
                penaltyApplied = true;
            }
        } else {
            const bool inEvent = master != nullptr && master->m_EventMatchUser.m_byEventMatchTeamId != 0;
            const bool inOwnSiege = master != nullptr && master->IsInMySiegeBattleField() != 0;
            penaltyApplied = (!m_AreaMod.m_bIsInBattleField && !inEvent) || (!m_AreaMod.m_bIsInPvPZone && !inOwnSiege);
        }

        if (penaltyApplied) {
            m_nDownExp = penalty;
            ExpDown(penalty);
            runtime::raw_atomic::AddI32(&m_nSaveToDBTick, 0x2d);
            if (penalty != 0) {
                SendSummonStatusToMaster(5);
            }
        }
    }
    if (!penaltyApplied) {
        m_nDownExp = 0;
    }

    SendSystemMessage(SystemMessageId::PET_DIED_RESURRECT_BEFORE_CORPSE_AND_SUMMONING_ITEM_EXPIRE);

    if (master != nullptr) {
        if (killer != nullptr) {
            User* const killerUser = killer->GetUserOrMaster();
            if (killerUser != nullptr) {
                const std::int32_t killerDatabaseId = runtime::Int32FromBits(killerUser->GetDBID());
                const std::int32_t killerAccountId = runtime::Int32FromBits(killerUser->GetAccountID());
                std::array<wchar_t, 25> killerName{};
                std::array<wchar_t, 25> killerAccountName{};
                CopyPetAuditWide25(killerName, killerUser->GetName());
                CopyPetAuditWide25(killerAccountName, killerUser->m_sAccountName.data());

                const double positionZ = GetPos().z;
                const double positionY = GetPos().y;
                const double positionX = GetPos().x;
                const char16_t* const masterName = master->GetName();
                const std::int32_t masterAccountId = runtime::Int32FromBits(master->GetAccountID());
                const std::int32_t masterDatabaseId = runtime::Int32FromBits(master->GetDBID());
                const std::wstring masterAccountName = Utf16ToWide(master->m_sAccountName.data());
                // Log.Add(
                //     LOG_BOTH,
                //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d"
                //     L",,,,,,,,,%I64d,,,,,%s,%s,%s,%s,",
                //     static_cast<std::int64_t>(0x463),
                //     static_cast<std::int64_t>(killerDatabaseId),
                //     static_cast<std::int64_t>(killerAccountId),
                //     static_cast<std::int64_t>(masterDatabaseId),
                //     static_cast<std::int64_t>(masterAccountId),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(positionX)),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(positionY)),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(positionZ)),
                //     static_cast<std::int64_t>(1),
                //     killerName.data(),
                //     killerAccountName.data(),
                //     masterName,
                //     masterAccountName.c_str())
                Log.Add(
                    LOG_BOTH,
                    u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d"
                    u",,,,,,,,,%I64d,,,,,%s,%s,%s,%s,",
                    static_cast<std::int64_t>(0x463), static_cast<std::int64_t>(killerDatabaseId),
                    static_cast<std::int64_t>(killerAccountId), static_cast<std::int64_t>(masterDatabaseId),
                    static_cast<std::int64_t>(masterAccountId),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(positionX)),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(positionY)),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(positionZ)), static_cast<std::int64_t>(1),
                    killerName.data(), killerAccountName.data(), masterName, masterAccountName.c_str()
                );
            }
        }
        if (m_data->m_bCanResurrection == 0 && resurrectionSpecial > 0 && resurrectionZoneMatches) {
            m_SkillDurationMod.m_vResurrectionInstantZoneIds = *resurrectionZonesStorage;
            runtime::raw_atomic::ExchangeI32(&m_data->m_bCanResurrection, 1);
            CopyPetNameToTarget(
                master->GetName(), m_data->m_sResurrectSpellerName,
                sizeof(m_data->m_sResurrectSpellerName) / sizeof(m_data->m_sResurrectSpellerName[0])
            );
            m_data->m_nResurrectExp = GetResurrectExp(master, 200.0);
            m_data->m_dwResurrectTime = runtime::GetTickCount();
            SendResurrectionConfirmDlg();
        } else if (resurrectionSpecial > 0) {
            SendSystemMessage(SystemMessageId::PET_RESURRECTION_INVALIDATED_OWNER_RESURRECTING);
        }
    }
    return true;
}

void CPet::OnCorpseDecayed() {
    // Current L2Server 0x0096012c.
    L2_GUARD_FUNCTION();
    if (!m_data->m_bDecayComplete) {
        return;
    }
    SendSystemMessage(SystemMessageId::PET_CORPSE_DISAPPEARED_AFTER_TIME_LIMIT);
    db.DeletePet(
        m_MasterWorldID, static_cast<int>(m_MasterUserSID), static_cast<int>(m_MasterUserDBID), Level(), 1,
        static_cast<int>(GetDBID())
    );
    g_PetManager.DeletePet(m_MasterUserDBID, m_MasterUserSID);
}

void CPet::OnNpcSocketClose() {
    // Current L2Server 0x00958964.
    L2_GUARD_FUNCTION();
    User* const master = GetMaster();
    if (master != nullptr) {
        master->ResetPet(GetDBID());
    }
    Delete();
}

bool CPet::Hungry() {
    L2_GUARD_FUNCTION();
    const std::int32_t thresholdProduct = runtime::MultiplyInt32Wrapping(m_pPetData->m_nHungryLimit, m_data->m_MaxMeal);
    return static_cast<double>(thresholdProduct) / 100.0 > static_cast<double>(m_data->m_Meal);
}

bool CPet::IsPremiumPet() const { return m_pPetData->m_nHungryLimit == 1; }

void CPet::EatFood(int delta) {
    // Current L2Server 0x0095a764.
    L2_GUARD_FUNCTION();

    const int added = runtime::AddInt32Wrapping(m_data->m_Meal, delta);
    const int limited = m_data->m_MaxMeal <= added ? m_data->m_MaxMeal : added;
    m_data->m_Meal = limited < 0 ? 0 : limited;
    m_bFirstTime = false;
    ValidateOrgSpeed();
    ValidateAttackSpeedAndTime();
    ParamChanged(std::int64_t{NPCT_PET_EAT_FOOD});
    SendSummonStatusToMaster(0);
}

void CPet::ValidateMeal() {
    L2_GUARD_FUNCTION();
    m_data->m_MaxMeal = m_pPetData->mMaxMeal;
    if (m_data->m_MaxMeal < m_data->m_Meal) {
        m_data->m_Meal = m_data->m_MaxMeal;
    }
}

int CPet::GetSoulShotCount() {
    L2_GUARD_FUNCTION();
    return m_pPetData->m_nSoulShotCount;
}

int CPet::GetSpiritShotCount() {
    L2_GUARD_FUNCTION();
    return m_pPetData->m_nSpiritShotCount;
}

bool CPet::Usable(CItem* const item) {
    // Current L2Server 0x0095859c. Pet item use is narrower
    // than User item use: the item must be NPC-enabled and pass the same
    // parser-owned condition chain against the pet itself.
    L2_GUARD_FUNCTION();
    return item != nullptr && item->m_const->m_bForNpc && item->CheckItemUseCondition(this);
}

bool CPet::ValidateBaseMP() {
    L2_GUARD_FUNCTION();
    CCreature::ValidateBaseMP();
    return true;
}

bool CPet::ValidateBaseHP() {
    L2_GUARD_FUNCTION();
    CCreature::ValidateBaseHP();
    return true;
}

int CPet::GetCurrentLife() { return m_data->m_Meal; }

int CPet::GetMaxLife() { return m_data->m_MaxMeal; }

void CPet::OnLevelChanged(int) {
    L2_GUARD_FUNCTION();
    OnValidateLevel();
}

void CCommonRidingInfo::Init() {
    L2_GUARD_FUNCTION();
    m_striderDBID = 0;
    m_striderClassID = 0;
    m_striderPetIndex = 0;
    m_striderLevel = 0;
    m_orgCollisionRadius = 0.0;
    m_orgCollisionHeight = 0.0;
    m_attackSpeedOnRide = 0.0;
    m_maxHPOnRide = 0.0;
    m_maxMPOnRide = 0.0;
    m_rideType = RIDE_NONE;
}

const PetDataSheet::Record& CCommonRidingInfo::Record() {
    L2_GUARD_FUNCTION();
    PetDataSheet* const dataSheet = PetDataSheetTable::Inst().GetPetDataSheetByIndex(m_striderPetIndex);
    return *dataSheet->GetRecord(m_striderLevel);
}

const PetDataSheet::Record& CCommonRidingInfo::Record(int level) {
    L2_GUARD_FUNCTION();
    if (m_striderLevel < level) {
        level = m_striderLevel;
    }
    PetDataSheet* const dataSheet = PetDataSheetTable::Inst().GetPetDataSheetByIndex(m_striderPetIndex);
    return *dataSheet->GetRecord(level);
}

void CCommonRidingInfo::RideStrider(CPet* pet) {
    L2_GUARD_FUNCTION();
    m_striderDBID = static_cast<std::int32_t>(pet->GetDBID());
    m_striderClassID = pet->m_data->m_nClassID;
    m_striderPetIndex = pet->GetPetIndex();
    m_striderLevel = pet->m_data->m_nLevel;
    m_rideType = pet->GetRideType();
}

void CCommonRidingInfo::RideWyvern(CPet* pet) {
    L2_GUARD_FUNCTION();
    m_striderClassID = pet->m_data->m_nClassID;
    m_striderPetIndex = pet->GetPetIndex();
    m_rideType = pet->GetRideType();
}

void CCommonRidingInfo::EventRide(CPet* pet, int level) {
    L2_GUARD_FUNCTION();
    m_striderDBID = 0;
    m_striderClassID = pet->m_data->m_nClassID;
    m_striderPetIndex = pet->GetPetIndex();
    m_striderLevel = level;
    m_rideType = pet->GetRideType();
}

void CCommonRidingInfo::OnRide(const PetDataSheet::Record& record, double collisionRadius, double collisionHeight) {
    L2_GUARD_FUNCTION();
    m_attackSpeedOnRide = record.mdAttackSpeedOnRide;
    m_maxHPOnRide = record.mdMaxHPOnRide;
    m_maxMPOnRide = record.mdMaxMPOnRide;
    m_orgCollisionRadius = collisionRadius;
    m_orgCollisionHeight = collisionHeight;
}

void CNormalRidingInfo::Ride(CPet* pet, int meal) {
    L2_GUARD_FUNCTION();
    m_striderMeal = meal;
    m_striderMaxMeal = pet->GetMaxMeal();
}

void CNormalRidingInfo::OnRide(const PetDataSheet::Record& record) {
    L2_GUARD_FUNCTION();
    m_consumeStriderMeal_OnCombat = record.mConsumeMealInBattleOnRide;
    m_consumeStriderMeal_OnNormal = record.mConsumeMealInNormalOnRide;
}

int CNormalRidingInfo::Meal() const { return m_striderMeal; }

int CNormalRidingInfo::MaxMeal() const { return m_striderMaxMeal; }

bool CNormalRidingInfo::IsMealEmpty() { return m_striderMeal < 1; }

bool CNormalRidingInfo::IsHungry(int hungryLimit) {
    return m_striderMeal < runtime::MultiplyInt32Wrapping(m_striderMaxMeal, hungryLimit) / 100;
}

int CNormalRidingInfo::ConsumeAmount(bool inCombat) {
    return inCombat ? m_consumeStriderMeal_OnCombat : m_consumeStriderMeal_OnNormal;
}

bool CNormalRidingInfo::IsMealTime() {
    L2_GUARD_FUNCTION();
    const std::uint32_t now = runtime::GetTickCount();
    if (now - static_cast<std::uint32_t>(m_striderMealTick) < 10001U) {
        return false;
    }
    m_striderMealTick = runtime::Int32FromBits(runtime::GetTickCount());
    return true;
}

bool CNormalRidingInfo::Eat(int amount) {
    L2_GUARD_FUNCTION();
    const std::int32_t oldMeal = m_striderMeal;
    const std::int32_t added = runtime::AddInt32Wrapping(oldMeal, amount);
    std::int32_t nextMeal = added < m_striderMaxMeal ? added : m_striderMaxMeal;
    if (nextMeal < 0) {
        nextMeal = 0;
    }
    m_striderMeal = nextMeal;
    return oldMeal != nextMeal;
}

bool CNormalRidingInfo::IsEventRiding() { return false; }

int CNormalRidingInfo::TimeLeft() { return 0; }

void CNormalRidingInfo::Init() {
    L2_GUARD_FUNCTION();
    m_striderMealTick = 0;
    m_striderMeal = 0;
    m_striderMaxMeal = 0;
    m_consumeStriderMeal_OnCombat = 0;
    m_consumeStriderMeal_OnNormal = 0;
}

void CEventRidingInfo::Ride(CPet*, int durationMinutes) {
    L2_GUARD_FUNCTION();
    m_duration = runtime::MultiplyInt32Wrapping(durationMinutes, 60);
    m_startTime = runtime::crt::Time32Now();
}

void CEventRidingInfo::OnRide(const PetDataSheet::Record&) {}

int CEventRidingInfo::Meal() const { return 0; }

int CEventRidingInfo::MaxMeal() const { return 0; }

bool CEventRidingInfo::IsMealEmpty() { return false; }

bool CEventRidingInfo::IsHungry(int) { return false; }

int CEventRidingInfo::ConsumeAmount(bool) { return 0; }

bool CEventRidingInfo::IsMealTime() { return false; }

bool CEventRidingInfo::Eat(int) { return false; }

bool CEventRidingInfo::IsEventRiding() { return true; }

int CEventRidingInfo::TimeLeft() {
    L2_GUARD_FUNCTION();
    return runtime::SubtractInt32Wrapping(
        runtime::AddInt32Wrapping(m_startTime, m_duration), runtime::crt::Time32Now()
    );
}

void CEventRidingInfo::Init() {
    L2_GUARD_FUNCTION();
    m_duration = 0;
    m_startTime = 0;
}

bool CUserRidingInfo::IsSame(PetCheckCond condition, int value) {
    L2_GUARD_FUNCTION();
    switch (condition) {
        case COND_COLLAR:
            return m_commonInfo.m_striderDBID == value;
        case COND_CLASS:
            return m_commonInfo.m_striderClassID == value;
        default:
            return false;
    }
}

void CUserRidingInfo::MakeLog(int type, User* user) {
    L2_GUARD_FUNCTION();

    CLogFormatter formatter{};
    formatter.WriteLog2_1(
        type, user, nullptr, m_commonInfo.m_striderDBID, m_commonInfo.m_striderClassID,
        static_cast<int>(m_commonInfo.m_rideType) - 1, 0, 0, 0, 0
    );
}

void CUserRidingInfo::Init() {
    L2_GUARD_FUNCTION();
    m_commonInfo.Init();
    m_normalInfo.Init();
    m_eventInfo.Init();
    runtime::raw_atomic::ExchangePointer(
        reinterpret_cast<CAbstractRidingInfo* volatile*>(&m_pRidingInfo), static_cast<CAbstractRidingInfo*>(&m_normalInfo)
    );
}

bool CUserRidingInfo::RideStrider(CPet* pet, double collisionRadius, double collisionHeight) {
    L2_GUARD_FUNCTION();
    m_commonInfo.RideStrider(pet);
    m_normalInfo.Ride(pet, pet->m_data->m_Meal);
    const PetDataSheet::Record& record = m_commonInfo.Record();
    const bool hungry = m_pRidingInfo->IsHungry(record.m_nHungryLimit);
    if (hungry) {
        Init();
        return false;
    }

    m_commonInfo.OnRide(record, collisionRadius, collisionHeight);
    m_normalInfo.OnRide(record);
    runtime::raw_atomic::ExchangePointer(
        reinterpret_cast<CAbstractRidingInfo* volatile*>(&m_pRidingInfo), static_cast<CAbstractRidingInfo*>(&m_normalInfo)
    );
    return true;
}

bool CUserRidingInfo::RideWyvern(CPet* pet, double collisionRadius, double collisionHeight) {
    L2_GUARD_FUNCTION();
    m_commonInfo.RideWyvern(pet);
    m_normalInfo.Ride(pet, pet->GetMaxMeal());
    const PetDataSheet::Record& record = m_commonInfo.Record();
    m_commonInfo.OnRide(record, collisionRadius, collisionHeight);
    m_normalInfo.OnRide(record);
    runtime::raw_atomic::ExchangePointer(
        reinterpret_cast<CAbstractRidingInfo* volatile*>(&m_pRidingInfo), static_cast<CAbstractRidingInfo*>(&m_normalInfo)
    );
    return true;
}

bool CUserRidingInfo::RideForEvent(
    CPet* pet, int level, double collisionRadius, double collisionHeight, int durationMinutes
) {
    L2_GUARD_FUNCTION();
    m_commonInfo.EventRide(pet, level);
    const PetDataSheet::Record& record = m_commonInfo.Record();
    m_commonInfo.OnRide(record, collisionRadius, collisionHeight);
    m_eventInfo.Ride(pet, durationMinutes);
    m_eventInfo.OnRide(record);
    runtime::raw_atomic::ExchangePointer(
        reinterpret_cast<CAbstractRidingInfo* volatile*>(&m_pRidingInfo), static_cast<CAbstractRidingInfo*>(&m_eventInfo)
    );
    return true;
}

void CUserRidingInfo::MakeEventLog(User* user, int eventId) {
    // Current L2Server 0x0095c444.
    L2_GUARD_FUNCTION();

    const double positionZ = user->GetPos().z;
    const double positionY = user->GetPos().y;
    const double positionX = user->GetPos().x;
    const char16_t* const userName = user->GetName();
    const int accountId = user->GetAccountID();
    const int databaseId = user->GetDBID();
    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,,,,%I64d,,,%s,%s,,,",
    //     static_cast<std::int64_t>(0x37c),
    //     static_cast<std::int64_t>(databaseId),
    //     static_cast<std::int64_t>(accountId),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(positionX)),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(positionY)),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(positionZ)),
    //     static_cast<std::int64_t>(user->m_data->m_Race),
    //     static_cast<std::int64_t>(user->m_data->m_Sex),
    //     static_cast<std::int64_t>(user->m_data->m_nClass),
    //     static_cast<std::int64_t>(user->m_data->m_nLevel),
    //     static_cast<std::int64_t>(eventId),
    //     userName,
    //     user->m_sAccountName.data())
    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,,,,%I64d,,,%s,%s,,,",
    //     static_cast<std::int64_t>(0x37c),
    //     static_cast<std::int64_t>(databaseId),
    //     static_cast<std::int64_t>(accountId),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(positionX)),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(positionY)),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(positionZ)),
    //     static_cast<std::int64_t>(user->m_data->m_Race),
    //     static_cast<std::int64_t>(user->m_data->m_Sex),
    //     static_cast<std::int64_t>(user->m_data->m_nClass),
    //     static_cast<std::int64_t>(user->m_data->m_nLevel),
    //     static_cast<std::int64_t>(eventId),
    //     Utf16ToWide(userName).c_str(),
    //     user->m_sAccountName.data())
    Log.Add(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,,,,%I64d,,,%s,%s,,,",
        static_cast<std::int64_t>(0x37c), static_cast<std::int64_t>(databaseId), static_cast<std::int64_t>(accountId),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(positionX)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(positionY)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(positionZ)),
        static_cast<std::int64_t>(user->m_data->m_Race), static_cast<std::int64_t>(user->m_data->m_Sex),
        static_cast<std::int64_t>(user->m_data->m_nClass), static_cast<std::int64_t>(user->m_data->m_nLevel),
        static_cast<std::int64_t>(eventId), userName, user->m_sAccountName.data()
    );
}

void CUserRidingInfo::OnEnterWorld() {
    L2_GUARD_FUNCTION();
    Init();
}

void CPet::OnMasterLeaveWorld() {
    // Current L2Server 0x00957590.
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::ExchangeI32(&m_data->m_bCanResurrection, 0);
    m_data->m_nResurrectExp = 0;
    m_data->m_nResurrectRatio = 0;
    m_data->m_dwResurrectTime = runtime::GetTickCount();
}
