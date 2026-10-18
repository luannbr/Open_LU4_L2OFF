#include "CursedWeapon.h"
#include "CConfig.h"
#include "CWorld.h"
#include "DB.h"
#include "DBSocket.h"
#include "Inventory.h"
#include "Item.h"
#include "NPC.h"
#include "ObjectDB.h"
#include "ParserCursedWeaponData.h"
#include "Pet.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/UserConst.h"
#include "Shared/src/Utils.h"
#include "Shared/src/net/Serialize.h"
#include "User.h"
#include "UserBroadcaster.h"
#include "UserSocket.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <string>
#include <string_view>

namespace {

constexpr std::array<int, 10> kMinuteAlarms{50, 40, 30, 20, 10, 5, 4, 3, 2, 1};

int TargetInt(double value) noexcept { return runtime::Win64CvttDoubleToInt32(value); }

constexpr std::int32_t ArithmeticShiftRight15(const std::int32_t value) noexcept {
    const std::uint32_t bits = static_cast<std::uint32_t>(value);
    const std::uint32_t signFill = (bits & UINT32_C(0x80000000)) != 0 ? UINT32_C(0xfffe0000) : UINT32_C(0);
    return runtime::Int32FromBits((bits >> 15U) | signFill);
}

int RemainingMinutes(const CCursedWeapon& weapon) noexcept {
    const int remaining = weapon.m_tExpiredDate - runtime::crt::Time32Now();
    return remaining / 60;
}

void SendBuilderMessage(CUserSocket* socket, const char16_t* message) { socket->SendSystemMessage(u"SYS", message); }

void SendRemainingTime(User* user, const CCursedWeapon& weapon) {
    if (!user || !user->GetSocket()) {
        return;
    }
    user->GetSocket()->Send(
        "chccdch", 0x62, SystemMessageId::NAMED_ITEM_USAGE_MINUTES_REMAINING, 2, 3, weapon.m_nItemClassId, 0x15,
        static_cast<int>(static_cast<std::int16_t>(RemainingMinutes(weapon)))
    );
}

void SendSinceLastKill(User* user, const CCursedWeapon& weapon) {
    if (!user || !user->GetSocket()) {
        return;
    }
    const int elapsedMinutes = (runtime::crt::Time32Now() - weapon.m_tLastKillDate) / 60;
    const int hours = elapsedMinutes / 60;
    const int minutes = elapsedMinutes % 60;
    user->GetSocket()->Send(
        "chccdchcc", 0x62, SystemMessageId::TIME_SINCE_NAMED_OBJECT_LAST_KILL, 3, 3, weapon.m_nItemClassId, 0x15,
        static_cast<int>(static_cast<std::int16_t>(hours)), 0x14, static_cast<int>(static_cast<std::int8_t>(minutes))
    );
}

void BroadcastLocation(int messageId, int messageParameter, const FVector& position, int itemClassId) {
    CUserBroadcaster::inst().BroadcastToAllUser(
        "chccdddcd", 0x62, messageId, 2, messageParameter, TargetInt(position.x), TargetInt(position.y),
        TargetInt(position.z), 3, itemClassId
    );
}

[[maybe_unused]] void NotifySummons(User* owner) {
    if (!owner) {
        return;
    }
    std::vector<CSummon*> summons;
    owner->GetAllSummonAndPet(summons);
    for (CSummon* summon : summons) {
        if (summon) {
            static_cast<CCreature*>(summon)->ParamChanged();
        }
    }
}

char16_t* TakeCursedWeaponBuilderToken(char16_t*& command) noexcept {
    // UTF-16 implementation of current GetLeftString2@00c3d570. It accepts
    // quoted tokens, splits only on ASCII space, mutates the input in place,
    // and has no null-input gate.
    while (*command == u' ') {
        ++command;
    }

    char16_t* token = nullptr;
    char16_t delimiter = u' ';
    if (*command != u'\0') {
        if (*command == u'"') {
            ++command;
            if (*command == u'\0') {
                return nullptr;
            }
            delimiter = u'"';
        }

        token = command;
        while (*command != u'\0' && *command != delimiter) {
            ++command;
        }
        if (*command != u'\0') {
            *command = u'\0';
            ++command;
        }
    }
    return token;
}

} // namespace

void CCursedWeaponConstInfo::Add(const CursedWeaponInfo* info) {
    L2_GUARD_FUNCTION();
    if (info) {
        m_mapCursedWeapon.emplace(info->nItemClassId, info);
    }
}

const CursedWeaponInfo* CCursedWeaponConstInfo::Find(int itemClassId) const {
    L2_GUARD_FUNCTION();
    const auto found = m_mapCursedWeapon.find(itemClassId);
    return found == m_mapCursedWeapon.end() ? nullptr : found->second;
}

void CCursedWeaponConstInfo::BuilderCmd_SettingView(CUserSocket* socket) {
    L2_GUARD_FUNCTION();
    char16_t message[2044];
    for (const auto& entry : m_mapCursedWeapon) {
        const CursedWeaponInfo* const info = entry.second;
        if (!info) {
            continue;
        }
        const char16_t* const name = g_ObjectDB.GetName(info->nItemClassId);
        if (!name) {
            continue;
        }
        FormatUtf16To(message, std::size(message), u"--- CursedWeapon[%ls, %d]", name, info->nItemClassId);
        SendBuilderMessage(socket, message);
        FormatUtf16To(message, std::size(message), u"   create_rate_per_10min: [%d]", info->nCreateRatePer10Min);
        SendBuilderMessage(socket, message);
        FormatUtf16To(message, std::size(message), u"   create_rate_per_npc: [%d]", info->nCreateRatePerNPC);
        SendBuilderMessage(socket, message);
        FormatUtf16To(message, std::size(message), u"   drop_rate_ondying: [%d]", info->nDropRateOnDying);
        SendBuilderMessage(socket, message);
        FormatUtf16To(message, std::size(message), u"   life_time: [%d]", info->nLifeTime);
        SendBuilderMessage(socket, message);
        FormatUtf16To(message, std::size(message), u"   expire_by_nonpk: [%d]", info->nExpireByNonpk);
        SendBuilderMessage(socket, message);
        FormatUtf16To(message, std::size(message), u"   expire_by_drop: [%d]", info->nExpireByDrop);
        SendBuilderMessage(socket, message);
        FormatUtf16To(message, std::size(message), u"   transform_id: [%d]", info->transformID);
        SendBuilderMessage(socket, message);
    }
}

int CCursedWeaponConstInfo::AssembleCursedWeaponList(char* buffer, int bufferSize, int* count) const {
    L2_GUARD_FUNCTION();
    if (!buffer || !count) {
        return 0;
    }
    int position = 0;
    *count = 0;
    for (const auto& entry : m_mapCursedWeapon) {
        if (bufferSize - position < 4) {
            break;
        }
        const int written = Assemble(buffer + position, bufferSize - position, "d", entry.first);
        if (written > 0) {
            position += written;
            ++*count;
        }
    }
    return position;
}

void CCursedWeaponConstInfo::SendCursedWeaponList(CUserSocket* socket) const {
    L2_GUARD_FUNCTION();
    if (!socket) {
        return;
    }
    std::array<char, 0x5000> buffer{};
    int count = 0;
    const int length = AssembleCursedWeaponList(buffer.data(), static_cast<int>(buffer.size()), &count);
    if (length < 1 || count < 1) {
        socket->Send("chd", 0xfe, 0x47, 0);
    } else {
        socket->Send("chdb", 0xfe, 0x47, count, length, buffer.data());
    }
}

bool CCursedWeaponGenerator::TestGenEnable() {
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::ExchangeI32(&m_GeneratorLock, 1) != 0) {
        return false;
    }

    bool enabled = false;
    if (runtime::raw_atomic::LoadI32Relaxed(&m_nExist) == 0) {
        const int now = runtime::crt::Time32Now();
        if (now - m_tLastCheck > 599) {
            m_tLastCheck = now;
            m_b10Min = static_cast<int>(genrand64_real2() * 1001.0) < m_pConstInfo->nCreateRatePer10Min;
        }
        if (m_b10Min && static_cast<int>(genrand64_real2() * 1001.0) < m_pConstInfo->nCreateRatePerNPC) {
            m_b10Min = false;
            runtime::raw_atomic::StoreI32(&m_nExist, 1);
            enabled = true;
        }
    }
    runtime::raw_atomic::StoreI32(&m_GeneratorLock, 0);
    return enabled;
}

int CCursedWeapon::CalcLastAlarmSec() const {
    L2_GUARD_FUNCTION();
    const int remaining = m_tExpiredDate - runtime::crt::Time32Now();
    int result = (remaining / 3600) * 3600;
    if (remaining != result) {
        result += 3600;
    }
    if (result == 3600) {
        for (auto it = kMinuteAlarms.rbegin(); it != kMinuteAlarms.rend(); ++it) {
            if (remaining <= *it * 60) {
                result = *it * 60;
                break;
            }
        }
    }
    return result;
}

void CCursedWeapon::ChangeOwner(User* oldOwner, User* newOwner) {
    L2_GUARD_FUNCTION();
    if (newOwner) {
        m_nCharId = static_cast<int>(newOwner->GetDBID());
        newOwner->m_nCursedWeaponClassId = m_nItemClassId;
        newOwner->ParamChanged(PCB_USER);
    } else {
        m_nCharId = 0;
    }
    if (oldOwner) {
        oldOwner->m_nCursedWeaponClassId = 0;
        oldOwner->ParamChanged(PCB_USER);
    }
}

int CCursedWeapon::GetLevel() const {
    L2_GUARD_FUNCTION();
    return std::min(10, 1 - static_cast<int>(m_dKillPoint / -60.0));
}

std::int32_t CCursedWeapon::RecalcExpiredDateAfterDrop() const {
    L2_GUARD_FUNCTION();
    const int remainder = m_tExpiredDate == 0 ? m_pConstInfo->nLifeTime : m_tExpiredDate - m_tDroppedDate;
    return runtime::crt::Time32Now() + remainder;
}

void CCursedWeapon::Drop(User* owner) {
    L2_GUARD_FUNCTION();
    db.RequestDelCursedWeapon(m_nItemDbId);
    m_bDrop = true;
    m_tDroppedDate = runtime::crt::Time32Now();
    ChangeOwner(owner, nullptr);
}

void CCursedWeapon::Destroy(User* owner) {
    L2_GUARD_FUNCTION();
    db.RequestDelCursedWeapon(m_nItemDbId);
    ChangeOwner(owner, nullptr);
}

void CCursedWeapon::KillPC(User* killer, User* victim) {
    L2_GUARD_FUNCTION();
    if (!killer || !victim) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [User *pCursedUser(%x) || User *pDeadUser(%x)] is nullptr",
        //     kSourceFile,
        //     0x195,
        //     killer,
        //     victim)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [User *pCursedUser(%x) || User *pDeadUser(%x)] is nullptr",
        //     kSourceFile,
        //     0x195,
        //     killer,
        //     victim)
        L2_LOG_ERROR(
            Log, "Cannot update cursed weapon class %d after a kill: killer %s, victim %s.", m_nItemClassId,
            killer ? "present" : "missing", victim ? "present" : "missing"
        );
        return;
    }

    const int levelDifference = std::clamp(killer->m_data->m_nLevel - victim->m_data->m_nLevel, 0, 9);
    const int oldLevel = GetLevel();
    const double point =
        std::sqrt(static_cast<double>(10 - levelDifference)) / std::pow(1.2, static_cast<double>(levelDifference));
    m_tLastKillDate = runtime::crt::Time32Now();
    m_dKillPoint += point;
    ++m_nTotalPK;
    if (m_nTotalPK < 72 && m_tExpiredDate - runtime::crt::Time32Now() > 3600) {
        m_tExpiredDate -= 3600;
    }
    db.RequestUpdateCursedWeapon(m_nItemDbId, m_dKillPoint, m_tLastKillDate, m_tExpiredDate, m_nCharId, m_nTotalPK);
    if (oldLevel != GetLevel()) {
        killer->SocialAction(0x4e31, 0x800, false);
        killer->ValidateChanged(VB_WEAPONARMOR | VB_ITEMSKILL | VB_SKILLLIST | VB_HPMPCP | VB_SIM | VB_SPEED);
    }
}

CItem* CCursedWeapon::GetItem() const {
    L2_GUARD_FUNCTION();
    return ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(m_nItemSId));
}

User* CCursedWeapon::GetOwner() const {
    L2_GUARD_FUNCTION();
    CSPointer<User> owner = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, m_nCharId));
    return owner.get();
}

CW_DELETE_REASON CCursedWeapon::TimerJob(int) {
    L2_GUARD_FUNCTION();
    const int now = runtime::crt::Time32Now();
    if (m_bDrop) {
        if (m_tDroppedDate + m_pConstInfo->nExpireByDrop < now) {
            return DR_EXPIRE_AFTER_DROP;
        }
        if (m_tExpiredDate != 0 && m_tExpiredDate < now) {
            return DR_END_LIFE_TIME;
        }
        if (now - m_tLastKillDate > m_pConstInfo->nExpireByNonpk) {
            return DR_NOT_KILL_PC;
        }
        return DR_NOT_DELETE;
    }

    if (m_tExpiredDate != 0 && m_tExpiredDate < now) {
        return DR_END_LIFE_TIME;
    }
    if (now - m_tLastKillDate > m_pConstInfo->nExpireByNonpk) {
        return DR_NOT_KILL_PC;
    }

    User* const owner = GetOwner();
    if (!owner) {
        return DR_NOT_DELETE;
    }
    const int remaining = m_tExpiredDate - now;
    int hourAlarm = remaining / 3600;
    if (remaining != hourAlarm * 3600) {
        ++hourAlarm;
    }
    hourAlarm *= 3600;
    if (hourAlarm >= 3600 && remaining <= hourAlarm && hourAlarm < m_nLastAlarmSec) {
        m_nLastAlarmSec = hourAlarm;
        owner->GetSocket()->Send(
            "chccdch", 0x62, SystemMessageId::NAMED_ITEM_USAGE_HOURS_REMAINING, 2, 3, m_nItemClassId, 0x15, hourAlarm / 3600
        );
        SendSinceLastKill(owner, *this);
        return DR_NOT_DELETE;
    }
    for (int minutes : kMinuteAlarms) {
        const int threshold = minutes * 60;
        if (remaining <= threshold && threshold < m_nLastAlarmSec) {
            m_nLastAlarmSec = threshold;
            owner->GetSocket()->Send(
                "chccdcc", 0x62, SystemMessageId::NAMED_ITEM_USAGE_MINUTES_REMAINING, 2, 3, m_nItemClassId, 0x14,
                static_cast<int>(static_cast<std::int8_t>(minutes))
            );
            SendSinceLastKill(owner, *this);
            break;
        }
    }
    return DR_NOT_DELETE;
}

void CCursedWeapon::PickUp(CItem* item, User* owner) {
    L2_GUARD_FUNCTION();
    if (!item || !owner) {
        return;
    }
    m_bDrop = false;
    m_tExpiredDate = RecalcExpiredDateAfterDrop();
    m_tDroppedDate = 0;
    m_nLastAlarmSec = CalcLastAlarmSec();
    m_nItemSId = static_cast<int>(item->m_nId);
    m_nItemDbId = static_cast<int>(item->m_data->m_nDBID);
    m_nOriginalPK = owner->m_data->m_nPKCount;
    m_dKillPoint = 0.0;
    owner->EquipItem(item, static_cast<SlotType>(0x3f));
    ChangeOwner(nullptr, owner);
    owner->Transform(m_pConstInfo->transformID, true);
    FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(owner, summon, static_cast<CCreature*>(summon)->ParamChanged(););
    db.RequestAddCursedWeapon(
        m_nItemDbId, m_tCreateDate, m_tLastKillDate, m_tExpiredDate, m_dKillPoint, m_nCharId, m_nItemClassId, m_nOriginalPK,
        m_nTotalPK
    );
    db.SendSaveCharacterInfo(owner, false, true);
}

void CCursedWeapon::EnterWorld(CItem* item, User* owner) {
    L2_GUARD_FUNCTION();
    if (!item || !owner) {
        return;
    }
    m_nItemSId = static_cast<int>(item->m_nId);
    m_nLastAlarmSec = CalcLastAlarmSec();
    CSharedItemData* const equipped = owner->GetEquipedWeapon();
    if (!equipped || equipped->m_nDBID != static_cast<int>(item->GetDBID())) {
        owner->EquipItem(item, static_cast<SlotType>(0x3f));
    }
    ChangeOwner(nullptr, owner);
    owner->Transform(m_pConstInfo->transformID, false);
    FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(owner, summon, static_cast<CCreature*>(summon)->ParamChanged(););
}

void CCursedWeapon::BuilderCmd_View(CUserSocket* socket) const {
    L2_GUARD_FUNCTION();
    const char16_t* const itemName = g_ObjectDB.GetName(m_nItemClassId);
    if (!itemName) {
        return;
    }
    char16_t message[2044];
    FormatUtf16To(message, std::size(message), u"--- CursedWeapon[%ls, %d]", itemName, m_nItemClassId);
    SendBuilderMessage(socket, message);
    const std::tm* const createTime = runtime::crt::LocalTime32(m_tCreateDate);
    FormatUtf16To(
        message, std::size(message), u"   create date: [%04d-%02d-%02d %02d:%02d:%02d]", createTime->tm_year + 1900,
        createTime->tm_mon + 1, createTime->tm_mday, createTime->tm_hour, createTime->tm_min, createTime->tm_sec
    );
    SendBuilderMessage(socket, message);
    if (m_tExpiredDate != 0) {
        const std::tm* const expiredTime = runtime::crt::LocalTime32(m_tExpiredDate);
        FormatUtf16To(
            message, std::size(message), u"   expired date: [%04d-%02d-%02d %02d:%02d:%02d]", expiredTime->tm_year + 1900,
            expiredTime->tm_mon + 1, expiredTime->tm_mday, expiredTime->tm_hour, expiredTime->tm_min, expiredTime->tm_sec
        );
        SendBuilderMessage(socket, message);
    }
    const std::tm* const lastKillTime = runtime::crt::LocalTime32(m_tLastKillDate);
    FormatUtf16To(
        message, std::size(message), u"   last kill date: [%04d-%02d-%02d %02d:%02d:%02d]", lastKillTime->tm_year + 1900,
        lastKillTime->tm_mon + 1, lastKillTime->tm_mday, lastKillTime->tm_hour, lastKillTime->tm_min, lastKillTime->tm_sec
    );
    SendBuilderMessage(socket, message);
    FormatUtf16To(message, std::size(message), u"   kill point: [%d]", static_cast<int>(m_dKillPoint));
    SendBuilderMessage(socket, message);
    FormatUtf16To(message, std::size(message), u"   level: [%d]", GetLevel());
    SendBuilderMessage(socket, message);
    FormatUtf16To(message, std::size(message), u"   total pk count: [%d]", m_nTotalPK);
    SendBuilderMessage(socket, message);
    if (m_bDrop) {
        const std::tm* const dropTime = runtime::crt::LocalTime32(m_tDroppedDate);
        FormatUtf16To(
            message, std::size(message), u"   drop date: [%04d-%02d-%02d %02d:%02d:%02d]", dropTime->tm_year + 1900,
            dropTime->tm_mon + 1, dropTime->tm_mday, dropTime->tm_hour, dropTime->tm_min, dropTime->tm_sec
        );
        SendBuilderMessage(socket, message);
    }
    if (User* const owner = GetOwner()) {
        const auto* const ownerData = owner->m_data;
        const char16_t* const ownerName = owner->GetName();
        const int ownerPkCount = ownerData->m_nPKCount;
        FormatUtf16To(message, std::size(message), u"   owner: [%ls] pk[%d]", ownerName, ownerPkCount);
        SendBuilderMessage(socket, message);
        const int z = TargetInt(owner->GetPos().z);
        const int y = TargetInt(owner->GetPos().y);
        const int x = TargetInt(owner->GetPos().x);
        FormatUtf16To(message, std::size(message), u"   pos: [%d,%d,%d]", x, y, z);
        SendBuilderMessage(socket, message);
    } else if (CItem* const item = GetItem()) {
        const int z = TargetInt(item->GetPos().z);
        const int y = TargetInt(item->GetPos().y);
        const int x = TargetInt(item->GetPos().x);
        FormatUtf16To(message, std::size(message), u"   pos: [%d,%d,%d]", x, y, z);
        SendBuilderMessage(socket, message);
    }
}

void CCursedWeapon::BuildCmd_SetKillPoint(int killPoint) {
    L2_GUARD_FUNCTION();
    const int oldLevel = GetLevel();
    m_dKillPoint = static_cast<double>(killPoint);
    const int newLevel = GetLevel();
    db.RequestUpdateCursedWeapon(m_nItemDbId, m_dKillPoint, m_tLastKillDate, m_tExpiredDate, m_nCharId, m_nTotalPK);
    if (User* const owner = GetOwner()) {
        owner->ValidateChanged(VB_WEAPONARMOR | VB_ITEMSKILL | VB_SKILLLIST | VB_HPMPCP | VB_SIM | VB_SPEED);
        if (oldLevel != newLevel) {
            owner->SocialAction(0x4e31, 0x800, false);
        }
    }
}

void CCursedWeapon::ReplyPickCursedWeaponFromDB(CItem* item, User* owner, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    if (!item || !packet) {
        return;
    }
    int originalPk = 0;
    int characterId = 0;
    if (owner) {
        Disassemble(packet, "d", &originalPk);
    } else {
        Disassemble(packet, "dd", &characterId, &originalPk);
    }
    m_nOriginalPK = originalPk;
    m_bDrop = false;
    m_tExpiredDate = RecalcExpiredDateAfterDrop();
    m_tDroppedDate = 0;
    m_nLastAlarmSec = CalcLastAlarmSec();
    m_nItemSId = static_cast<int>(item->m_nId);
    m_nItemDbId = static_cast<int>(item->m_data->m_nDBID);
    m_dKillPoint = 0.0;
    if (owner) {
        owner->ProcessMyParty(false);
        owner->EquipItem(item, static_cast<SlotType>(0x3f));
        ChangeOwner(nullptr, owner);
        owner->Transform(m_pConstInfo->transformID, true);
        FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(owner, summon, static_cast<CCreature*>(summon)->ParamChanged(););
        db.SendSaveCharacterInfo(owner, false, true);
    } else {
        m_nCharId = characterId;
    }
    db.RequestAddCursedWeapon(
        m_nItemDbId, m_tCreateDate, m_tLastKillDate, m_tExpiredDate, m_dKillPoint, m_nCharId, m_nItemClassId, m_nOriginalPK,
        m_nTotalPK
    );
}

void CCursedWeapon::PickAnotherCursedWeapon(User* owner, CItem* item) {
    L2_GUARD_FUNCTION();
    if (!owner || !item) {
        return;
    }
    const int level = GetLevel();
    if (level < 10) {
        m_dKillPoint = static_cast<double>(level * 60);
        db.RequestUpdateCursedWeapon(m_nItemDbId, m_dKillPoint, m_tLastKillDate, m_tExpiredDate, m_nCharId, m_nTotalPK);
        owner->SocialAction(0x4e31, 0x800, false);
        owner->ValidateChanged(VB_WEAPONARMOR | VB_ITEMSKILL | VB_SKILLLIST | VB_HPMPCP | VB_SIM | VB_SPEED);
    }
    CCursedWeaponMgr::inst().DeleteOriginalCursedWeapon(item);
}

CCursedWeaponMgr::CCursedWeaponMgr() :
    m_ConstInfo(),
    m_nTimerCount(0),
    m_padding94To98{} {}

CCursedWeaponMgr::~CCursedWeaponMgr() = default;

void CCursedWeaponMgr::Init() {
    L2_GUARD_FUNCTION();
    db.RequestLoadCursedWeaponList();
}

void CCursedWeaponMgr::LoadFromScript() {
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\cursedweapondata.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/cursedweapondata.txt";
#endif
    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from %s file failed",
        //     kSourceFile,
        //     0x2d7,
        //     kFileName)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from %s file failed",
        //     kSourceFile,
        //     0x2d7,
        //     kFileName)
        L2_LOG_ERROR(Log, "Cannot read cursedweapondata.txt; cursed weapon loading stops.");
        return;
    }
    CParserForCursedWeapon parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForCursedWeapon::create failed",
        //     kSourceFile,
        //     0x2e0)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForCursedWeapon::create failed",
        //     kSourceFile,
        //     0x2e0)
        L2_LOG_ERROR(Log, "Cannot initialize cursedweapondata.txt parser; cursed weapon loading stops.");
        return;
    }
    parser.m_lexer.yyin = input.GetStream();
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing %s file failed after line[%d]",
        //     kSourceFile,
        //     0x2ec,
        //     kFileName,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing %s file failed after line[%d]",
        //     kSourceFile,
        //     0x2ec,
        //     kFileName,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "Cannot parse cursedweapondata.txt at script line %lld; entries registered before the error are retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
}

void CCursedWeaponMgr::Add(CursedWeaponInfo* info) {
    L2_GUARD_FUNCTION();
    if (!info) {
        return;
    }
    m_ConstInfo.Add(info);
    auto* const generator = new CCursedWeaponGenerator{info, 0, 0, false, {}, 0};
    m_mapGenerator.emplace(info->nItemClassId, generator);
}

CCursedWeaponGenerator* CCursedWeaponMgr::GetGenerator(int itemClassId) {
    L2_GUARD_FUNCTION();
    const auto found = m_mapGenerator.find(itemClassId);
    return found == m_mapGenerator.end() ? nullptr : found->second;
}

CCursedWeapon* CCursedWeaponMgr::GetCursedWeapon(int itemClassId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EntrySect);
    const auto found = m_mapCursedItem.find(itemClassId);
    return found == m_mapCursedItem.end() ? nullptr : found->second;
}

int CCursedWeaponMgr::GetWeaponLevel(int itemClassId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EntrySect);
    const auto found = m_mapCursedItem.find(itemClassId);
    return found == m_mapCursedItem.end() || !found->second ? 1 : found->second->GetLevel();
}

int CCursedWeaponMgr::GetOriginalPKCount(int itemClassId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EntrySect);
    const auto found = m_mapCursedItem.find(itemClassId);
    return found == m_mapCursedItem.end() || !found->second ? -1 : found->second->m_nOriginalPK;
}

bool CCursedWeaponMgr::IsCursedUser(User* user) { return user != nullptr && user->m_nCursedWeaponClassId > 0; }

void CCursedWeaponMgr::KillPC(User* killer, User* victim) {
    L2_GUARD_FUNCTION();
    if (!killer || killer->m_nCursedWeaponClassId <= 0) {
        return;
    }
    L2_GUARD(m_EntrySect);
    const auto found = m_mapCursedItem.find(killer->m_nCursedWeaponClassId);
    if (found != m_mapCursedItem.end() && found->second) {
        found->second->KillPC(killer, victim);
    }
}

bool CCursedWeaponMgr::BuilderMake(CNPC* npc, int itemClassId) {
    L2_GUARD_FUNCTION();
    if (g_bIsNonPKServer) {
        return false;
    }
    CCursedWeaponGenerator* const generator = GetGenerator(itemClassId);
    if (!generator || runtime::raw_atomic::ExchangeI32(&generator->m_GeneratorLock, 1) != 0) {
        return false;
    }
    if (runtime::raw_atomic::LoadI32Relaxed(&generator->m_nExist) != 0) {
        runtime::raw_atomic::StoreI32(&generator->m_GeneratorLock, 0);
        return false;
    }
    runtime::raw_atomic::StoreI32(&generator->m_nExist, 1);
    runtime::raw_atomic::StoreI32(&generator->m_GeneratorLock, 0);
    if (m_mapCursedItem.find(itemClassId) != m_mapCursedItem.end()) {
        // Log.Add(LOG_ERROR, L"Invalid State of cursed weapon")
        // L2_LOG_ERROR(Log, L"Invalid State of cursed weapon")
        L2_LOG_ERROR(
            Log, "Cannot create cursed weapon class %d: weapon is already registered; generator existence flag remains set.",
            itemClassId
        );
        return false;
    }
    const ItemOptionKey optionKey{};
    const BaseAttribute attribute;
    if (npc->AddItemToInventory(itemClassId, 1, ItemGetReason::Etc, 0, 0, 0, optionKey, 0, attribute)) {
        return true;
    }
    runtime::raw_atomic::StoreI32(&generator->m_nExist, 0);
    return false;
}

void CCursedWeaponMgr::RandomMake(CNPC* npc) {
    L2_GUARD_FUNCTION();
    if (g_bIsNonPKServer) {
        return;
    }
    if (npc) {
        if (npc->m_bIsGMKill) {
            return;
        }
        const FVector position = npc->GetPos();
        const int mapX = (runtime::AddInt32Wrapping(TargetInt(position.x), 0x50000) >> 15) + 10;
        if (mapX < 0x0f || !npc->IsInRealWorld()) {
            return;
        }
    }
    for (const auto& entry : m_mapGenerator) {
        CCursedWeaponGenerator* const generator = entry.second;
        if (!generator || !generator->TestGenEnable()) {
            continue;
        }
        if (m_mapCursedItem.find(entry.first) == m_mapCursedItem.end() && npc) {
            const ItemOptionKey optionKey{};
            const BaseAttribute attribute;
            if (!npc->AddItemToInventory(entry.first, 1, ItemGetReason::Etc, 0, 0, 0, optionKey, 0, attribute)) {
                runtime::raw_atomic::StoreI32(&generator->m_nExist, 0);
            }
        }
        break;
    }
}

bool CCursedWeaponMgr::AddWeapon(
    int itemDbId,
    int createDate,
    int lastKillDate,
    int expiredDate,
    double killPoint,
    int charId,
    const CursedWeaponInfo* info,
    int originalPk,
    int totalPk
) {
    L2_GUARD_FUNCTION();
    if (!info) {
        return false;
    }
    auto* const weapon = new CCursedWeapon{info->nItemClassId, createDate, lastKillDate, expiredDate, killPoint, info,
        itemDbId, charId, originalPk, totalPk, 0, 0, 0, false, {}};
    weapon->m_nLastAlarmSec = weapon->CalcLastAlarmSec();
    const auto inserted = m_mapCursedItem.emplace(weapon->m_nItemClassId, weapon);
    return inserted.second;
}

void CCursedWeaponMgr::LoadFromDB(
    int itemDbId,
    int createDate,
    int lastKillDate,
    int expiredDate,
    double killPoint,
    int charId,
    int itemClassId,
    int originalPk,
    int totalPk
) {
    L2_GUARD_FUNCTION();
    const CursedWeaponInfo* const info = m_ConstInfo.Find(itemClassId);
    if (!info) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] item class id(%d) not cursed weapon",
        //     kSourceFile,
        //     0x325,
        //     itemClassId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] item class id(%d) not cursed weapon",
        //     kSourceFile,
        //     0x325,
        //     itemClassId)
        L2_LOG_ERROR(
            Log, "Cannot load cursed weapon item %d (class %d) from DB: class is absent from cursedweapondata.txt.",
            itemDbId, itemClassId
        );
        return;
    }
    AddWeapon(itemDbId, createDate, lastKillDate, expiredDate, killPoint, charId, info, originalPk, totalPk);
    if (CCursedWeaponGenerator* const generator = GetGenerator(itemClassId)) {
        runtime::raw_atomic::StoreI32(&generator->m_nExist, 1);
    }
}

void CCursedWeaponMgr::TimerExpired(int) {
    L2_GUARD_FUNCTION();
    m_nTimerCount = runtime::AddInt32Wrapping(m_nTimerCount, 1);
    {
        L2_GUARD(m_EntrySect);
        for (auto position = m_mapCursedItem.begin(); position != m_mapCursedItem.end();) {
            CCursedWeapon* const weapon = position->second;
            if (!weapon) {
                ++position;
                continue;
            }
            const CW_DELETE_REASON reason = weapon->TimerJob(m_nTimerCount);
            if (reason == DR_NOT_DELETE) {
                ++position;
                continue;
            }

            if (!weapon->GetItem()) {
                Log.Add(
                    LOG_BOTH, u"%I64d,,,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,,,,,,,,,,,,,", std::int64_t{0x3ce},
                    std::int64_t{0}, std::int64_t{0}, std::int64_t{0}, static_cast<std::int64_t>(weapon->m_nItemClassId),
                    static_cast<std::int64_t>(reason)
                );
                position = m_mapCursedItem.erase(position);
                db.RequestDelCursedWeaponOnlyInDB(weapon->m_nItemDbId, weapon->m_nOriginalPK);
                if (CCursedWeaponGenerator* const generator = GetGenerator(weapon->m_pConstInfo->nItemClassId)) {
                    runtime::raw_atomic::StoreI32(&generator->m_nExist, 0);
                }
                delete weapon;
                continue;
            }

            if (User* const owner = weapon->GetOwner()) {
                owner->SendSystemMessage(SystemMessageId::NO_CLAN_APPLICANTS);
                if (reason == DR_NOT_KILL_PC) {
                    owner->GetSocket()->Send(
                        "chccd", 0x62, SystemMessageId::NAMED_OBJECT_EXPIRED_AFTER_DAY_WITHOUT_KILL, 1, 3,
                        weapon->m_nItemClassId
                    );
                }
                ++position;
                continue;
            }

            position = m_mapCursedItem.erase(position);
            OnDeleted(weapon->GetItem(), nullptr, reason);
            delete weapon;
        }
    }
    AddTimer(1000, 0);
}

bool CCursedWeaponMgr::DestroyWeapon(CItem* item, User* owner) {
    L2_GUARD_FUNCTION();
    if (!item || !item->m_data) {
        return false;
    }
    bool destroyed = false;
    {
        L2_GUARD(m_EntrySect);
        const int itemClassId = item->m_data->m_nClassID;
        const auto position = m_mapCursedItem.find(itemClassId);
        if (position == m_mapCursedItem.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] cursed weapon(%d) not exists",
            //     kSourceFile,
            //     0x43d,
            //     itemClassId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] cursed weapon(%d) not exists",
            //     kSourceFile,
            //     0x43d,
            //     itemClassId)
            L2_LOG_ERROR(
                Log, "Cannot find cursed weapon class %d during destruction; DB item destruction will still be requested.",
                itemClassId
            );
        } else {
            CCursedWeapon* const weapon = position->second;
            if (!weapon) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] cursed weapon(%d) invalid",
                //     kSourceFile,
                //     0x437,
                //     itemClassId)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] cursed weapon(%d) invalid",
                //     kSourceFile,
                //     0x437,
                //     itemClassId)
                L2_LOG_ERROR(
                    Log,
                    "Cursed weapon class %d has a null registry entry during destruction; removing the entry and requesting "
                    "DB item destruction.",
                    itemClassId
                );
            } else {
                weapon->Destroy(owner);
                delete weapon;
                destroyed = true;
            }
            m_mapCursedItem.erase(position);
        }
    }
    db.RequestDestroyItem(item, 1, owner, 0);
    return destroyed;
}

bool CCursedWeaponMgr::AddWeapon(CItem* item, User* owner) {
    L2_GUARD_FUNCTION();
    if (!item || !item->m_data) {
        return false;
    }
    L2_GUARD(m_EntrySect);
    const int itemClassId = item->m_data->m_nClassID;
    const auto position = m_mapCursedItem.find(itemClassId);
    if (position == m_mapCursedItem.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] dropped cursed weapon(%d) not exists",
        //     kSourceFile,
        //     0x3f9,
        //     itemClassId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] dropped cursed weapon(%d) not exists",
        //     kSourceFile,
        //     0x3f9,
        //     itemClassId)
        L2_LOG_ERROR(Log, "Cannot pick up cursed weapon class %d: weapon is not registered.", itemClassId);
        return false;
    }
    CCursedWeapon* const weapon = position->second;
    if (!weapon) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] dropped cursed weapon(%d) invalid",
        //     kSourceFile,
        //     0x3f4,
        //     itemClassId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] dropped cursed weapon(%d) invalid",
        //     kSourceFile,
        //     0x3f4,
        //     itemClassId)
        L2_LOG_ERROR(Log, "Cannot pick up cursed weapon class %d: registry entry is null.", itemClassId);
        return false;
    }
    weapon->PickUp(item, owner);
    return true;
}

void CCursedWeaponMgr::OnPickUp(User* owner, CItem* item) {
    L2_GUARD_FUNCTION();
    if (!owner || !item || !item->m_data) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [User *pUser(%x) || CItem *pItem(%x)] is nullptr",
        //     kSourceFile,
        //     0x491,
        //     owner,
        //     item)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [User *pUser(%x) || CItem *pItem(%x)] is nullptr",
        //     kSourceFile,
        //     0x491,
        //     owner,
        //     item)
        L2_LOG_ERROR(
            Log, "Cannot process cursed weapon pickup: owner %s, item %s, item data %s.", owner ? "present" : "missing",
            item ? "present" : "missing", item && item->m_data ? "present" : "missing"
        );
        return;
    }
    if (!m_ConstInfo.Find(item->m_data->m_nClassID) || !AddWeapon(item, owner)) {
        return;
    }
    const FVector position = owner->GetPos();
    BroadcastLocation(0x718, 7, position, item->m_data->m_nClassID);
    owner->ProcessMyParty(false);
    L2_GUARD(m_EntrySect);
    const auto found = m_mapCursedItem.find(item->m_data->m_nClassID);
    if (found != m_mapCursedItem.end() && found->second) {
        SendRemainingTime(owner, *found->second);
    }
}

void CCursedWeaponMgr::OnEnterWorld(User* owner) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EntrySect);
    if (!owner) {
        return;
    }
    for (const auto& entry : m_mapCursedItem) {
        CCursedWeapon* const weapon = entry.second;
        if (!weapon || weapon->m_nCharId != static_cast<int>(owner->GetDBID())) {
            continue;
        }
        CItem* const item = owner->m_inv.GetByDBID(weapon->m_nItemDbId);
        if (!item) {
            return;
        }
        weapon->EnterWorld(item, owner);
        BroadcastLocation(0x719, 7, owner->GetPos(), weapon->m_nItemClassId);
        SendRemainingTime(owner, *weapon);
        SendSinceLastKill(owner, *weapon);
        return;
    }
}

void CCursedWeaponMgr::OnDeleted(CItem* item, User* owner, CW_DELETE_REASON reason) {
    L2_GUARD_FUNCTION();
    if (!item || !item->m_data || !m_ConstInfo.Find(item->m_data->m_nClassID)) {
        return;
    }
    const int itemClassId = item->m_data->m_nClassID;
    if (reason == DR_DYING) {
        DestroyWeapon(item, owner);
    } else if (reason == DR_EXPIRE_AFTER_DROP || (reason >= DR_END_LIFE_TIME && reason <= DR_BY_BUILDER)) {
        item->m_data->m_bIsActive = false;
        CWorld::Inst().PickAndDeleteItem(item);
    }
    CUserBroadcaster::inst().BroadcastToAllUser("chccd", 0x62, 0x71a, 1, 3, itemClassId);
    if (CCursedWeaponGenerator* const generator = GetGenerator(itemClassId)) {
        runtime::raw_atomic::StoreI32(&generator->m_nExist, 0);
    }
}

bool CCursedWeaponMgr::DropWeapon(CItem* item, User* owner) {
    L2_GUARD_FUNCTION();
    if (!item || !item->m_data) {
        return false;
    }
    L2_GUARD(m_EntrySect);
    const int itemClassId = item->m_data->m_nClassID;
    const auto found = m_mapCursedItem.find(itemClassId);
    if (found != m_mapCursedItem.end()) {
        if (found->second && owner) {
            found->second->Drop(owner);
            return true;
        }
        return false;
    }

    const CursedWeaponInfo* const info = m_ConstInfo.Find(itemClassId);
    if (!info) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] cursed weapon(%d) not defined",
        //     kSourceFile,
        //     0x41d,
        //     itemClassId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] cursed weapon(%d) not defined",
        //     kSourceFile,
        //     0x41d,
        //     itemClassId)
        L2_LOG_ERROR(
            Log, "Cannot register dropped cursed weapon class %d: class is absent from cursedweapondata.txt.", itemClassId
        );
        return false;
    }
    const int now = runtime::crt::Time32Now();
    auto* const weapon = new CCursedWeapon{itemClassId, now, now, 0, 0.0, info, 0, 0, 0, 0,
        // Current DropWeapon@0x00640a0c copies CItem+0x18: the encoded
        // CObject SmartID consumed by CCursedWeapon::GetItem, not the shared
        // object index returned by CItem::GetIndex().
        static_cast<int>(item->m_nId), now, 0, true, {}};
    return m_mapCursedItem.emplace(itemClassId, weapon).second;
}

void CCursedWeaponMgr::OnDropByNPC(CItem* item) {
    L2_GUARD_FUNCTION();
    if (!item) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [CItem *pItem] is nullptr",
        //     kSourceFile,
        //     0x44c)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [CItem *pItem] is nullptr",
        //     kSourceFile,
        //     0x44c)
        L2_LOG_ERROR(Log, "Cannot process NPC cursed weapon drop: item is missing.");
        return;
    }
    if (!m_ConstInfo.Find(item->m_data->m_nClassID)) {
        return;
    }
    const std::int32_t mapCell = runtime::AddInt32Wrapping(
        ArithmeticShiftRight15(runtime::AddInt32Wrapping(TargetInt(item->GetPos().x), 0x50000)), 10
    );
    if (mapCell < 15) {
        return;
    }

    const int firstZ = TargetInt(item->GetPos().z);
    const int firstY = TargetInt(item->GetPos().y);
    const int firstX = TargetInt(item->GetPos().x);
    CUserBroadcaster::inst().BroadcastToAllUser(
        "chccdddcd", 0x62, 0x717, 2, 7, firstX, firstY, firstZ, 3, item->m_data->m_nClassID
    );
    CUserBroadcaster::inst().BroadcastToAllUser("chd", 0xfe, 0x42, 10);

    const int secondZ = TargetInt(item->GetPos().z);
    const int secondY = TargetInt(item->GetPos().y);
    const int secondX = TargetInt(item->GetPos().x);
    CUserBroadcaster::inst().BroadcastToAllUser("cdddddd", 0xd3, secondX, secondY, secondZ, 0x14, 3, 1);

    const int auditZ = TargetInt(item->GetPos().z);
    const int auditY = TargetInt(item->GetPos().y);
    const int auditX = TargetInt(item->GetPos().x);
    Log.Add(
        LOG_BOTH, u"%I64d,,,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,,,,,,,,,,,,,", std::int64_t{0x3ce},
        static_cast<std::int64_t>(auditX), static_cast<std::int64_t>(auditY), static_cast<std::int64_t>(auditZ),
        static_cast<std::int64_t>(item->m_data->m_nClassID), std::int64_t{0}
    );
    DropWeapon(item, nullptr);
}

bool CCursedWeaponMgr::OnDropByUser(User* owner, CItem* item) {
    L2_GUARD_FUNCTION();
    if (!owner || owner->m_nCursedWeaponClassId <= 0 || !item || !item->m_data) {
        return true;
    }
    L2_GUARD(m_EntrySect);
    const auto found = m_mapCursedItem.find(item->m_data->m_nClassID);
    if (found == m_mapCursedItem.end() || !found->second) {
        return true;
    }
    CCursedWeapon* const weapon = found->second;
    const int now = runtime::crt::Time32Now();
    const FVector position = owner->GetPos();
    const bool expired = weapon->m_tExpiredDate != 0 && weapon->m_tExpiredDate < now;
    const bool dropFailed = static_cast<int>(genrand64_real2() * 1001.0) >= weapon->m_pConstInfo->nDropRateOnDying;
    const bool invalidPosition = !CWorld::Inst().IsValidPosition(TargetInt(position.x), TargetInt(position.y));
    if (expired || dropFailed || invalidPosition) {
        OnDeleted(item, owner, DR_DYING);
        return false;
    }
    BroadcastLocation(0x717, 7, position, item->m_data->m_nClassID);
    DropWeapon(item, owner);
    return true;
}

void CCursedWeaponMgr::DeleteOriginalCursedWeapon(CItem* item) {
    L2_GUARD_FUNCTION();
    if (!item || !item->m_data) {
        return;
    }
    {
        L2_GUARD(m_EntrySect);
        const auto found = m_mapCursedItem.find(item->m_data->m_nClassID);
        if (found != m_mapCursedItem.end() && found->second) {
            CCursedWeapon* const weapon = found->second;
            m_mapCursedItem.erase(found);
            delete weapon;
        }
    }
    OnDeleted(item, nullptr, DR_PICKED_BY_ANOTHER_CURSED_USER);
    item->Delete();
}

void CCursedWeaponMgr::ReplyPickCursedWeaponFromDB(CItem* item, User* owner, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    if (!item || !item->m_data || !m_ConstInfo.Find(item->m_data->m_nClassID)) {
        return;
    }
    if (CCursedWeapon* const weapon = GetCursedWeapon(item->m_data->m_nClassID)) {
        weapon->ReplyPickCursedWeaponFromDB(item, owner, packet);
    }
}

void CCursedWeaponMgr::BuilderCmd_SettingView(CUserSocket* socket) {
    L2_GUARD_FUNCTION();
    m_ConstInfo.BuilderCmd_SettingView(socket);
}

void CCursedWeaponMgr::SendCursedWeaponList(CUserSocket* socket) {
    L2_GUARD_FUNCTION();
    m_ConstInfo.SendCursedWeaponList(socket);
}

void CCursedWeaponMgr::BuilderCmd_Delete(User*, int itemClassId) {
    L2_GUARD_FUNCTION();
    int ownerId = 0;
    {
        L2_GUARD(m_EntrySect);
        const auto found = m_mapCursedItem.find(itemClassId);
        if (found == m_mapCursedItem.end() || !found->second) {
            return;
        }
        CCursedWeapon* const weapon = found->second;
        CItem* const item = weapon->GetItem();
        if (!item) {
            m_mapCursedItem.erase(found);
            db.RequestDelCursedWeaponOnlyInDB(weapon->m_nItemDbId, weapon->m_nOriginalPK);
            if (CCursedWeaponGenerator* const generator = GetGenerator(weapon->m_nItemClassId)) {
                runtime::raw_atomic::StoreI32(&generator->m_nExist, 0);
            }
            delete weapon;
        } else if (weapon->GetOwner()) {
            ownerId = weapon->m_nCharId;
        } else {
            m_mapCursedItem.erase(found);
            OnDeleted(item, nullptr, DR_BY_BUILDER);
            delete weapon;
        }
    }
    if (ownerId != 0) {
        CSPointer<User> owner = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, ownerId));
        if (owner) {
            owner->SendSystemMessage(SystemMessageId::NO_CLAN_APPLICANTS);
        }
    }
}

void CCursedWeaponMgr::BuilderCmd_View(CUserSocket* socket) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EntrySect);
    for (const auto& entry : m_mapCursedItem) {
        if (entry.second) {
            entry.second->BuilderCmd_View(socket);
        }
    }
}

void CCursedWeaponMgr::BuilderCmd_SetKillPoint(int itemClassId, int killPoint) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EntrySect);
    const auto found = m_mapCursedItem.find(itemClassId);
    if (found != m_mapCursedItem.end() && found->second) {
        found->second->BuildCmd_SetKillPoint(killPoint);
    }
}

void CCursedWeaponMgr::ShowCursedWeaponStatus(User* owner, int itemClassId) {
    L2_GUARD_FUNCTION();
    if (!owner || !owner->GetSocket()) {
        return;
    }
    L2_GUARD(m_EntrySect);
    const auto found = m_mapCursedItem.find(itemClassId);
    if (found == m_mapCursedItem.end() || !found->second) {
        owner->GetSocket()->Send("chccd", 0x62, SystemMessageId::NAMED_OBJECT_SEALED, 1, 3, itemClassId);
        return;
    }
    CCursedWeapon* const weapon = found->second;
    if (!weapon->m_bDrop) {
        User* const cursedOwner = weapon->GetOwner();
        if (!cursedOwner) {
            owner->GetSocket()->Send("chccd", 0x62, SystemMessageId::NAMED_OBJECT_DORMANT, 1, 3, itemClassId);
            return;
        }
        const FVector position = cursedOwner->GetPos();
        owner->GetSocket()->Send(
            "chccddddd", 0x62, SystemMessageId::EVIL_PRESENCE_FROM_PERSON_IN_NAMED_REGION, 2, 7, TargetInt(position.x),
            TargetInt(position.y), TargetInt(position.z), 3, itemClassId
        );
        return;
    }
    CItem* const item = weapon->GetItem();
    if (!item) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] droped item(%d) is null",
        //     kSourceFile,
        //     0x59d,
        //     itemClassId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] droped item(%d) is null",
        //     kSourceFile,
        //     0x59d,
        //     itemClassId)
        L2_LOG_ERROR(Log, "Cannot send location of dropped cursed weapon class %d: item lookup returned null.", itemClassId);
        return;
    }
    const FVector position = item->GetPos();
    owner->GetSocket()->Send(
        "chccdddcd", 0x62, SystemMessageId::EVIL_PRESENCE_IN_NAMED_REGION, 2, 7, TargetInt(position.x),
        TargetInt(position.y), TargetInt(position.z), 3, itemClassId
    );
}

int CCursedWeaponMgr::AssembleExistingCursedWeaponLocation(char* buffer, int bufferSize, int* count) {
    L2_GUARD_FUNCTION();
    if (!buffer || !count) {
        return 0;
    }
    int position = 0;
    *count = 0;
    L2_GUARD(m_EntrySect);
    for (const auto& entry : m_mapCursedItem) {
        if (bufferSize - position < 20 || !entry.second) {
            break;
        }
        const CCursedWeapon& weapon = *entry.second;
        FVector objectPosition{};
        if (weapon.m_bDrop) {
            CItem* const item = weapon.GetItem();
            if (!item) {
                continue;
            }
            objectPosition = item->GetPos();
        } else {
            User* const owner = weapon.GetOwner();
            if (!owner) {
                continue;
            }
            objectPosition = owner->GetPos();
        }
        const int written = Assemble(
            buffer + position, bufferSize - position, "ddddd", weapon.m_nItemClassId, weapon.m_bDrop ? 0 : 1,
            TargetInt(objectPosition.x), TargetInt(objectPosition.y), TargetInt(objectPosition.z)
        );
        if (written > 0) {
            position += written;
            ++*count;
        }
    }
    return position;
}

void CCursedWeaponMgr::SendExistingCursedWeapon(CUserSocket* socket) {
    L2_GUARD_FUNCTION();
    if (!socket) {
        return;
    }
    std::array<char, 0x5000> buffer{};
    int count = 0;
    const int length = AssembleExistingCursedWeaponLocation(buffer.data(), static_cast<int>(buffer.size()), &count);
    if (length < 1 || count < 1) {
        socket->Send("chd", 0xfe, 0x48, 0);
    } else {
        socket->Send("chdb", 0xfe, 0x48, count, length, buffer.data());
    }
}

bool CCursedWeaponDBHandler::ReplyLoadCursedWeaponList(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int count;
    packet = Disassemble(packet, "d", &count);
    if (count > 300) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] nCursedWeaponCount > %d",
        //     kSourceFile,
        //     0x63b,
        //     300)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] nCursedWeaponCount > %d",
        //     kSourceFile,
        //     0x63b,
        //     300)
        L2_LOG_ERROR(
            Log, "Cursed weapon DB list contains %d entries, exceeding limit 300; reply rejected before loading entries.",
            count
        );
        return false;
    }
    for (int index = 0; index < count; ++index) {
        int itemDbId;
        int createDate;
        int lastKillDate;
        int expiredDate;
        double killPoint;
        int charId;
        int itemClassId;
        int originalPk;
        int totalPk;
        packet = Disassemble(
            packet, "ddddfdddd", &itemDbId, &createDate, &lastKillDate, &expiredDate, &killPoint, &charId, &itemClassId,
            &originalPk, &totalPk
        );
        CCursedWeaponMgr::inst().LoadFromDB(
            itemDbId, createDate, lastKillDate, expiredDate, killPoint, charId, itemClassId, originalPk, totalPk
        );
    }
    CCursedWeaponMgr::inst().AddTimer(1000, 0);
    return false;
}

bool CCursedWeaponBuilderCmd::Delete(CUserSocket* socket, User* user, char16_t* command) {
    // Current L2Server 0x006419f4.
    L2_GUARD_FUNCTION();
    char16_t* const token = TakeCursedWeaponBuilderToken(command);
    if (token == nullptr) {
        socket->SendSystemMessage(u"SYS", u"//cursed_weapon_delete [item_class_id]");
    } else {
        CCursedWeaponMgr::inst().BuilderCmd_Delete(user, WindowsWtol(token));
        socket->SendSystemMessage(u"SYS", u"Builder Command 'cursed_weapon_delete' accepted...");
    }
    return false;
}

bool CCursedWeaponBuilderCmd::View(CUserSocket* socket, User*, char16_t*) {
    // Current L2Server 0x00641b14.
    L2_GUARD_FUNCTION();
    CCursedWeaponMgr::inst().BuilderCmd_View(socket);
    return false;
}

bool CCursedWeaponBuilderCmd::SettingView(CUserSocket* socket, User*, char16_t*) {
    // Current L2Server 0x00641bc0.
    L2_GUARD_FUNCTION();
    CCursedWeaponMgr::inst().BuilderCmd_SettingView(socket);
    return false;
}

bool CCursedWeaponBuilderCmd::GiveCursedWeapon(CUserSocket* socket, User* user, char16_t* command) {
    // Current L2Server 0x00641c6c.
    L2_GUARD_FUNCTION();
    if (*command == L'\0') {
        socket->SendSystemMessage(u"SYS", u"//give_cursed_weapon [Object id|Object name]");
        return false;
    }

    char16_t* const token = TakeCursedWeaponBuilderToken(command);
    int itemClassId = WindowsWtol(token);
    if (itemClassId == 0) {
        const std::u16string targetName = std::u16string(token);
        itemClassId = g_ObjectDB.GetClassIdFromName(targetName.c_str(), false);
    }
    if (!CCursedWeaponMgr::inst().GetGenerator(itemClassId)) {
        socket->SendSystemMessage(u"SYS", u"please use //giveitem");
        return false;
    }

    CCreature* const target = user->m_spTarget.GetCreature();
    if (!target) {
        socket->SendSystemMessage(u"SYS", u"target null");
        return false;
    }
    if (target->IsSummonOrPet()) {
        socket->SendSystemMessage(u"SYS", u"target is summoned creature");
        return false;
    }
    if (!target->IsNPC()) {
        socket->SendSystemMessage(u"SYS", u"target must be a npc");
        return false;
    }

    const FVector position = target->GetPos();
    if (!CWorld::Inst().IsValidPosition(TargetInt(position.x), TargetInt(position.y))) {
        socket->SendSystemMessage(u"SYS", u"invalid position");
        return false;
    }

    CNPC* const npc = target->CastNPC();
    const bool made = CCursedWeaponMgr::inst().BuilderMake(npc, itemClassId);
    CUserSocket* const resultSocket = user->GetSocket();
    const char16_t* const resultMessage = made ? u"giving cursed weapon succeeded" : u"cursed weapon already exist";
    resultSocket->SendSystemMessage(u"SYS", resultMessage);
    return false;
}

bool CCursedWeaponBuilderCmd::KillPoint(CUserSocket* socket, User*, char16_t* command) {
    // Current L2Server 0x00641ee4.
    L2_GUARD_FUNCTION();
    char16_t* const itemClassToken = TakeCursedWeaponBuilderToken(command);
    if (itemClassToken != nullptr) {
        const int itemClassId = WindowsWtol(itemClassToken);
        char16_t* const killPointToken = TakeCursedWeaponBuilderToken(command);
        if (killPointToken != nullptr) {
            const int killPoint = WindowsWtol(killPointToken);
            CCursedWeaponMgr::inst().BuilderCmd_SetKillPoint(itemClassId, killPoint);
            socket->SendSystemMessage(u"SYS", u"Builder Command 'cursed_weapon_killpoint' accepted...");
            return false;
        }
    }

    socket->SendSystemMessage(u"SYS", u"//cursed_weapon_killpoint [item_class_id] [kill_point]");
    return false;
}
