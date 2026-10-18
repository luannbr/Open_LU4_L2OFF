#include "CrataeCube.h"
#include "CursedWeapon.h"
#include "DB.h"
#include "NPC.h"
#include "NpcServer.h"
#include "Object.h"
#include "Protocol/src/SystemMessageIds.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/platform/Runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

namespace {


CSPointer<User> FindUser(std::uint32_t userId) {
    const std::CDualKey<int, int> key(g_nWorldId, static_cast<int>(userId));
    return g_UserDBIDMap.FindObject(key);
}

User* FindUserByObjectId(std::uint32_t userId) {
    CObject* const object = CObject::GetObjectW(userId);
    return object != nullptr && object->IsPC() ? object->CastUser() : nullptr;
}

} // namespace

const int CCrataeCube::MAX_USER_COUNT = 25;
const int CCrataeCube::PLAYING_DISTANCE = 15000;
const int CCrataeCube::PLAYING_TIME = 1200000;
const int CCrataeCube::WAITING_TIME = 180000;

CCCRecordBoardData::CCCRecordBoardData(std::int32_t userId, const char16_t* userName) :
    m_nUserId(0),
    m_targetPadding4(0),
    m_nKillCount(0),
    m_nKillPoint(0),
    m_nRank(0),
    m_dwRegTime(0),
    m_dwDieTime(0),
    m_targetPadding44(0) {
    Init(static_cast<std::uint32_t>(userId), userName);
}

CCCRecordBoardData::~CCCRecordBoardData() = default;

void CCCRecordBoardData::Init(std::uint32_t userId, const char16_t* userName) {
    m_nUserId = userId;
    m_strUserName = userName ? userName : u"";
    m_nKillCount = 0;
    m_nKillPoint = 0;
    m_nRank = 0;
    m_dwRegTime = 0;
    m_dwDieTime = 0;
}

void CCCRecordBoardData::SetUserDieTime(std::uint32_t dieTime) { m_dwDieTime = dieTime; }

std::uint32_t CCCRecordBoardData::GetUserDieTime() const { return m_dwDieTime; }

void CCCRecordBoardData::IncKillCount() { ++m_nKillCount; }
void CCCRecordBoardData::IncKillPoint(int point) { m_nKillPoint += point; }
void CCCRecordBoardData::SetRank(int rank) { m_nRank = rank; }

bool CCCRecordBoard::GreaterKillPoints::operator()(const CCCRecordBoardData* left, const CCCRecordBoardData* right) const {
    if (left->m_nKillPoint != right->m_nKillPoint) {
        return left->m_nKillPoint > right->m_nKillPoint;
    }
    return left->m_dwRegTime < right->m_dwRegTime;
}

CCCRecordBoard::CCCRecordBoard() : m_nTotalKillPoint(0), m_targetPadding14(0) {}

CCCRecordBoard::~CCCRecordBoard() { Init(); }

void CCCRecordBoard::Init() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    for (CCCRecordBoardData* record : m_vecUserRecordBoardData) {
        delete record;
    }
    m_vecUserRecordBoardData.clear();
}

void CCCRecordBoard::AddNewRecordData(User* user) {
    L2_GUARD_FUNCTION();
    CCCRecordBoardData* const record = new CCCRecordBoardData(user->m_data->m_nDBID, user->GetName());
    record->m_dwRegTime = runtime::GetTickCount();
    {
        L2_GUARD(m_Lock);
        m_vecUserRecordBoardData.push_back(record);
    }
}

bool CCCRecordBoard::DeleteRecordData(User* user) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x005d11c8 lets a null user match a
    // stale record whose DBID no longer resolves.
    L2_GUARD(m_Lock);
    for (auto it = m_vecUserRecordBoardData.begin(); it != m_vecUserRecordBoardData.end(); ++it) {
        // Release the lookup reference before erasing the record and unlocking.
        const bool matchesUser = FindUser((*it)->m_nUserId).get() == user;
        if (matchesUser) {
            CCCRecordBoardData* const record = *it;
            m_vecUserRecordBoardData.erase(it);
            delete record;
            return true;
        }
    }

    return false;
}

int CCCRecordBoard::AddKillPoint(User* user, int point) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x005d05d4.
    if (user != nullptr) {
        CCCRecordBoardData* const record = GetRecordData(user);
        if (record != nullptr) {
            record->IncKillCount();
            record->IncKillPoint(point);
            return record->m_nKillPoint;
        }
    }
    return 0;
}

CCCRecordBoardData* CCCRecordBoard::GetRecordData(User* user) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x005d02ac compares the record DBID with the
    // shared creature DBID, not with the temporary object SID.
    if (user == nullptr) {
        return nullptr;
    }
    L2_GUARD(m_Lock);
    for (CCCRecordBoardData* record : m_vecUserRecordBoardData) {
        if (record->m_nUserId == static_cast<std::uint32_t>(user->m_data->m_nDBID)) {
            return record;
        }
    }

    return nullptr;
}

void CCCRecordBoard::Summation() {
    m_nTotalKillPoint = 0;
    for (const CCCRecordBoardData* record : m_vecUserRecordBoardData) {
        m_nTotalKillPoint += record->m_nKillPoint;
    }
}

void CCCRecordBoard::SendCurrentRecordForSystemMessage(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->GetSocket() == nullptr) {
        return;
    }
    char16_t line[256];
    constexpr const char16_t* kRecordFormat = u"%s - kill point : %d";
    user->SendSystemMessage(u"SYS", u"===== Current Record =====");
    {
        L2_GUARD(m_Lock);
        _SortRecordBoardData(m_vecUserRecordBoardData);
        for (const CCCRecordBoardData* record : m_vecUserRecordBoardData) {
            FormatUtf16To(line, std::size(line), kRecordFormat, record->m_strUserName, record->m_nKillPoint);
            user->SendSystemMessage(u"SYS", line);
        }
    }
}

void CCCRecordBoard::SendCurrentRecordForUI(User* user, int) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x005d03c4 keeps the lookup and socket send under
    // the record-board lock and does not null-check the socket.
    if (user == nullptr) {
        return;
    }
    L2_GUARD(m_Lock);
    for (CCCRecordBoardData* record : m_vecUserRecordBoardData) {
        if (record != nullptr && record->m_nUserId == static_cast<std::uint32_t>(user->m_data->m_nDBID)) {
            user->GetSocket()->Send("chd", 0xfe, 0x8b, record->m_nRank);

            return;
        }
    }
}

int CCCRecordBoard::MakeCurrentRecordPacket(char* packet, int packetSize, int type) {
    L2_GUARD_FUNCTION();
    int length;
    {
        L2_GUARD(m_Lock);
        length = ::Assemble(packet, packetSize, "d", type);
        length += ::Assemble(packet + length, packetSize - length, "d", static_cast<int>(m_vecUserRecordBoardData.size()));
        _SortRecordBoardData(m_vecUserRecordBoardData);
        for (const CCCRecordBoardData* record : m_vecUserRecordBoardData) {
            length +=
                ::Assemble(packet + length, packetSize - length, "Sd", record->m_strUserName.c_str(), record->m_nKillPoint);
        }
    }
    return length;
}

CCCRecordBoard::CCRecordBoardDataVec CCCRecordBoard::GetRecordBoardDataSorted() {
    CCRecordBoardDataVec records = m_vecUserRecordBoardData;
    SortRecordBoardData(records);
    return records;
}

void CCCRecordBoard::SortRecordBoardData(CCRecordBoardDataVec& records) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        _SortRecordBoardData(records);
    }
}

void CCCRecordBoard::_SortRecordBoardData(CCRecordBoardDataVec& records) {
    L2_GUARD_FUNCTION();
    std::sort(records.begin(), records.end(), GreaterKillPoints());
    int rank = 0;
    for (CCCRecordBoardData* record : m_vecUserRecordBoardData) {
        record->SetRank(++rank);
    }
}

CCrataeCube::CCrataeCube(PVP_MATCH_TYPE type) :
    CPVPMatch2(type),
    m_nStatus(2),
    m_nResurrectionNpcId(0) {}

CCrataeCube::~CCrataeCube() = default;

bool CCrataeCube::IsCC() const { return true; }
CCrataeCube* CCrataeCube::CastCC() { return this; }
PVPMatchType::Enum CCrataeCube::GetPVPMatchType() const { return PVPMatchType::CRATAE_CUBE; }

bool CCrataeCube::CheckRegisterLevel(int level) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x005cf5a4.
    switch (m_eType) {
        case PVPMT_UGC_UL:
            return true;
        case PVPMT_COC_70:
            return level >= 70 && level <= 75;
        case PVPMT_COC_76:
            return level >= 76 && level <= 79;
        case PVPMT_COC_80:
            return level >= 80;
        default:
            return false;
    }
}

int CCrataeCube::CheckRegisterUser(std::uint32_t userId) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x005d44a4.
    if (m_nStatus == 0) {
        return PVPMR_USER_PVPMATCH_READY;
    }
    CSPointer<User> user = FindUser(userId);
    if (!user) {
        return PVPMR_INVALID_USER;
    }
    static_cast<void>(CCursedWeaponMgr::inst());
    if (user->m_nCursedWeaponClassId > 0) {
        return PVPMR_USER_CURSED;
    }
    if (user->m_bEinhasadeTransformed || user->m_data->m_nKarma < 0) {
        return PVPMR_USER_CHAO;
    }
    // Current L2Server.exe 0x005d4603 loads CPVPMatch2 +0x238 directly.
    // That field is m_nGateManNpcId: registration is validated beside the
    // entrance NPC that submitted the request, not the arena manager.
    CObject* const managerObject = CObject::GetObjectW(GetGateManNpcId());
    CNPC* const manager = managerObject != nullptr && managerObject->IsNPC() ? managerObject->CastNPC() : nullptr;
    if (manager == nullptr) {
        return PVPMR_INVALID_MATCH;
    }
    if (IsRegisteredUser(userId)) {
        return PVPMR_ALREADY_REGISTERED;
    }
    if (user->User::IsInPVPMatch(PVPMatchType::NONE, PVPMatchPlayerState::NONE)) {
        return PVPMR_ALREADY_REGISTERED_ANOTHER;
    }
    if (!CheckRegisterLevel(user->m_data->m_nLevel)) {
        return PVPMR_INVALID_USER_LEVEL;
    }
    FVector managerPosition = manager->GetPos();
    FVector userPosition = user->GetPos();
    if (!CheckDistance(managerPosition, userPosition, PLAYING_DISTANCE / 10)) {
        return PVPMR_INVALID_USER_DIST;
    }
    return user->m_data->m_nPartyId == 0 ? PVPMR_SUCCESS : PVPMR_USER_IN_PARTY;
}

int CCrataeCube::RegisterUser(std::uint32_t userId) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x005d138c.
    const int result = CheckRegisterUser(userId);
    if (result != PVPMR_SUCCESS) {
        return result;
    }
    CSPointer<User> user = FindUser(userId);
    if (!user) {
        return result;
    }
    {
        L2_GUARD(m_Lock);
        if (m_applicantListByUserId.size() > 24) {
            return PVPMR_FULL;
        }
        m_applicantListByUserId[userId] = static_cast<int>(user->Id());
    }
    user->m_data->m_PVPMatchType = PVPMatchType::CRATAE_CUBE;
    user->m_data->m_PVPMatchPlayerState = PVPMatchPlayerState::WAITING;

    const int positionZ = runtime::Win64CvttDoubleToInt32(user->GetPos().z);
    const int positionY = runtime::Win64CvttDoubleToInt32(user->GetPos().y);
    const int positionX = runtime::Win64CvttDoubleToInt32(user->GetPos().x);
    const char16_t* const userName = user->GetName();
    const std::uint32_t accountId = user->GetAccountID();
    const std::uint32_t databaseId = user->GetDBID();
    // Portable: the wide accounting formatter requires a host-wide account name.
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
    //     0x1f4e,
    //     databaseId,
    //     accountId,
    //     positionX,
    //     positionY,
    //     positionZ,
    //     user->m_data->m_nClass,
    //     user->m_data->m_Race,
    //     user->m_data->m_Sex,
    //     user->m_data->m_nLevel,
    //     static_cast<int>(m_eType),
    //     userName,
    //     user->m_sAccountName.data())
#if defined(_WIN32)
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
        0x1f4e, databaseId, accountId, positionX, positionY, positionZ, user->m_data->m_nClass, user->m_data->m_Race,
        user->m_data->m_Sex, user->m_data->m_nLevel, static_cast<int>(m_eType), userName,
        std::u16string_view(user->m_sAccountName.data(), user->m_sAccountName.size())
    );
#else
    // Portable: %I64d becomes %lld. Pass matching 64-bit values to varargs.
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
    //     static_cast<long long>(0x1f4e),
    //     static_cast<long long>(databaseId),
    //     static_cast<long long>(accountId),
    //     static_cast<long long>(positionX),
    //     static_cast<long long>(positionY),
    //     static_cast<long long>(positionZ),
    //     static_cast<long long>(user->m_data->m_nClass),
    //     static_cast<long long>(user->m_data->m_Race),
    //     static_cast<long long>(user->m_data->m_Sex),
    //     static_cast<long long>(user->m_data->m_nLevel),
    //     static_cast<long long>(static_cast<int>(m_eType)),
    //     userName,
    //     Utf16ToWide(std::u16string_view(
    //         user->m_sAccountName.data(),
    //         user->m_sAccountName.size())).c_str())
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
        static_cast<long long>(0x1f4e), static_cast<long long>(databaseId), static_cast<long long>(accountId),
        static_cast<long long>(positionX), static_cast<long long>(positionY), static_cast<long long>(positionZ),
        static_cast<long long>(user->m_data->m_nClass), static_cast<long long>(user->m_data->m_Race),
        static_cast<long long>(user->m_data->m_Sex), static_cast<long long>(user->m_data->m_nLevel),
        static_cast<long long>(static_cast<int>(m_eType)), userName,
        std::u16string_view(user->m_sAccountName.data(), user->m_sAccountName.size())
    );
#endif
    return PVPMR_SUCCESS;
}

bool CCrataeCube::_UnregisterUser(UserMapById::iterator position, int) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x005d1664 assumes a valid iterator.
    CSPointer<User> user = FindUser(static_cast<std::uint32_t>(position->first));
    if (!user || !user->IsInPVPMatch(PVPMatchType::CRATAE_CUBE, PVPMatchPlayerState::NONE)) {
        return false;
    }
    m_applicantListByUserId.erase(position);
    user->m_data->m_PVPMatchType = PVPMatchType::NONE;
    user->m_data->m_PVPMatchPlayerState = PVPMatchPlayerState::NONE;
    user->GetSocket()->Send("ch", 0xfe, 0x8c);

    const int positionZ = runtime::Win64CvttDoubleToInt32(user->GetPos().z);
    const int positionY = runtime::Win64CvttDoubleToInt32(user->GetPos().y);
    const int positionX = runtime::Win64CvttDoubleToInt32(user->GetPos().x);
    const char16_t* const userName = user->GetName();
    const std::uint32_t accountId = user->GetAccountID();
    const std::uint32_t databaseId = user->GetDBID();
    // Portable: the wide accounting formatter requires a host-wide account name.
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
    //     0x1f4f,
    //     databaseId,
    //     accountId,
    //     positionX,
    //     positionY,
    //     positionZ,
    //     user->m_data->m_nClass,
    //     user->m_data->m_Race,
    //     user->m_data->m_Sex,
    //     user->m_data->m_nLevel,
    //     static_cast<int>(m_eType),
    //     0,
    //     userName,
    //     user->m_sAccountName.data())
#if defined(_WIN32)
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
        0x1f4f, databaseId, accountId, positionX, positionY, positionZ, user->m_data->m_nClass, user->m_data->m_Race,
        user->m_data->m_Sex, user->m_data->m_nLevel, static_cast<int>(m_eType), 0, userName,
        std::u16string_view(user->m_sAccountName.data(), user->m_sAccountName.size())
    );
#else
    // Portable: %I64d becomes %lld. Pass matching 64-bit values to varargs.
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
    //     static_cast<long long>(0x1f4f),
    //     static_cast<long long>(databaseId),
    //     static_cast<long long>(accountId),
    //     static_cast<long long>(positionX),
    //     static_cast<long long>(positionY),
    //     static_cast<long long>(positionZ),
    //     static_cast<long long>(user->m_data->m_nClass),
    //     static_cast<long long>(user->m_data->m_Race),
    //     static_cast<long long>(user->m_data->m_Sex),
    //     static_cast<long long>(user->m_data->m_nLevel),
    //     static_cast<long long>(static_cast<int>(m_eType)),
    //     static_cast<long long>(0),
    //     userName,
    //     Utf16ToWide(std::u16string_view(
    //         user->m_sAccountName.data(),
    //         user->m_sAccountName.size())).c_str())
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
        static_cast<long long>(0x1f4f), static_cast<long long>(databaseId), static_cast<long long>(accountId),
        static_cast<long long>(positionX), static_cast<long long>(positionY), static_cast<long long>(positionZ),
        static_cast<long long>(user->m_data->m_nClass), static_cast<long long>(user->m_data->m_Race),
        static_cast<long long>(user->m_data->m_Sex), static_cast<long long>(user->m_data->m_nLevel),
        static_cast<long long>(static_cast<int>(m_eType)), static_cast<long long>(0), userName,
        std::u16string_view(user->m_sAccountName.data(), user->m_sAccountName.size())
    );
#endif
    return true;
}

bool CCrataeCube::UnregisterUser(std::uint32_t userId, int reason) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x005d18f0 duplicates the map operation so the
    // lock is released before state publication, socket send and audit log.
    CSPointer<User> user = FindUser(userId);
    if (!user || !user->IsInPVPMatch(PVPMatchType::CRATAE_CUBE, PVPMatchPlayerState::NONE)) {
        return false;
    }

    bool removed = false;
    int killPoint = 0;
    {
        L2_GUARD(m_Lock);
        auto applicant = m_applicantListByUserId.find(static_cast<int>(userId));
        if (applicant != m_applicantListByUserId.end()) {
            m_applicantListByUserId.erase(applicant);
            removed = true;
        } else {
            CCCRecordBoardData* const record = m_RecordBoard.GetRecordData(user.get());
            if (record != nullptr) {
                killPoint = record->m_nKillPoint;
                m_RecordBoard.DeleteRecordData(user.get());
            }
            auto participant = m_participantListByUserId.find(static_cast<int>(userId));
            if (participant != m_participantListByUserId.end()) {
                m_participantListByUserId.erase(participant);
                removed = true;
            }
        }
    }

    user->m_data->m_PVPMatchType = PVPMatchType::NONE;
    user->m_data->m_PVPMatchPlayerState = PVPMatchPlayerState::NONE;
    user->GetSocket()->Send("ch", 0xfe, 0x8c);

    const int positionZ = runtime::Win64CvttDoubleToInt32(user->GetPos().z);
    const int positionY = runtime::Win64CvttDoubleToInt32(user->GetPos().y);
    const int positionX = runtime::Win64CvttDoubleToInt32(user->GetPos().x);
    const char16_t* const userName = user->GetName();
    const std::uint32_t accountId = user->GetAccountID();
    const std::uint32_t databaseId = user->GetDBID();
    // Portable: the wide accounting formatter requires a host-wide account name.
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
    //     0x1f4f,
    //     databaseId,
    //     accountId,
    //     positionX,
    //     positionY,
    //     positionZ,
    //     user->m_data->m_nClass,
    //     user->m_data->m_Race,
    //     user->m_data->m_Sex,
    //     user->m_data->m_nLevel,
    //     static_cast<int>(m_eType),
    //     killPoint,
    //     userName,
    //     user->m_sAccountName.data())
#if defined(_WIN32)
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
        0x1f4f, databaseId, accountId, positionX, positionY, positionZ, user->m_data->m_nClass, user->m_data->m_Race,
        user->m_data->m_Sex, user->m_data->m_nLevel, static_cast<int>(m_eType), killPoint, userName,
        std::u16string_view(user->m_sAccountName.data(), user->m_sAccountName.size())
    );
#else
    // Portable: %I64d becomes %lld. Pass matching 64-bit values to varargs.
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
    //     static_cast<long long>(0x1f4f),
    //     static_cast<long long>(databaseId),
    //     static_cast<long long>(accountId),
    //     static_cast<long long>(positionX),
    //     static_cast<long long>(positionY),
    //     static_cast<long long>(positionZ),
    //     static_cast<long long>(user->m_data->m_nClass),
    //     static_cast<long long>(user->m_data->m_Race),
    //     static_cast<long long>(user->m_data->m_Sex),
    //     static_cast<long long>(user->m_data->m_nLevel),
    //     static_cast<long long>(static_cast<int>(m_eType)),
    //     static_cast<long long>(killPoint),
    //     userName,
    //     Utf16ToWide(std::u16string_view(
    //         user->m_sAccountName.data(),
    //         user->m_sAccountName.size())).c_str())
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
        static_cast<long long>(0x1f4f), static_cast<long long>(databaseId), static_cast<long long>(accountId),
        static_cast<long long>(positionX), static_cast<long long>(positionY), static_cast<long long>(positionZ),
        static_cast<long long>(user->m_data->m_nClass), static_cast<long long>(user->m_data->m_Race),
        static_cast<long long>(user->m_data->m_Sex), static_cast<long long>(user->m_data->m_nLevel),
        static_cast<long long>(static_cast<int>(m_eType)), static_cast<long long>(killPoint), userName,
        std::u16string_view(user->m_sAccountName.data(), user->m_sAccountName.size())
    );
#endif
    (void)reason;
    return removed;
}

bool CCrataeCube::_UnregisterUserById(std::uint32_t userId, int reason) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x005d1c98.
    CSPointer<User> user = FindUser(userId);
    if (!user || !user->IsInPVPMatch(PVPMatchType::CRATAE_CUBE, PVPMatchPlayerState::NONE)) {
        return false;
    }
    bool removed = false;
    int killPoint = 0;
    auto applicant = m_applicantListByUserId.find(static_cast<int>(userId));
    if (applicant != m_applicantListByUserId.end()) {
        m_applicantListByUserId.erase(applicant);
        removed = true;
    } else {
        CCCRecordBoardData* const record = m_RecordBoard.GetRecordData(user.get());
        if (record != nullptr) {
            killPoint = record->m_nKillPoint;
            m_RecordBoard.DeleteRecordData(user.get());
        }
        auto participant = m_participantListByUserId.find(static_cast<int>(userId));
        if (participant != m_participantListByUserId.end()) {
            m_participantListByUserId.erase(participant);
            removed = true;
        }
    }
    user->m_data->m_PVPMatchType = PVPMatchType::NONE;
    user->m_data->m_PVPMatchPlayerState = PVPMatchPlayerState::NONE;
    user->GetSocket()->Send("ch", 0xfe, 0x8c);

    const int positionZ = runtime::Win64CvttDoubleToInt32(user->GetPos().z);
    const int positionY = runtime::Win64CvttDoubleToInt32(user->GetPos().y);
    const int positionX = runtime::Win64CvttDoubleToInt32(user->GetPos().x);
    const char16_t* const userName = user->GetName();
    const std::uint32_t accountId = user->GetAccountID();
    const std::uint32_t databaseId = user->GetDBID();
    // Portable: the wide accounting formatter requires a host-wide account name.
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
    //     0x1f4f,
    //     databaseId,
    //     accountId,
    //     positionX,
    //     positionY,
    //     positionZ,
    //     user->m_data->m_nClass,
    //     user->m_data->m_Race,
    //     user->m_data->m_Sex,
    //     user->m_data->m_nLevel,
    //     static_cast<int>(m_eType),
    //     killPoint,
    //     userName,
    //     user->m_sAccountName.data())
#if defined(_WIN32)
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
        0x1f4f, databaseId, accountId, positionX, positionY, positionZ, user->m_data->m_nClass, user->m_data->m_Race,
        user->m_data->m_Sex, user->m_data->m_nLevel, static_cast<int>(m_eType), killPoint, userName,
        std::u16string_view(user->m_sAccountName.data(), user->m_sAccountName.size())
    );
#else
    // Portable: %I64d becomes %lld. Pass matching 64-bit values to varargs.
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
    //     static_cast<long long>(0x1f4f),
    //     static_cast<long long>(databaseId),
    //     static_cast<long long>(accountId),
    //     static_cast<long long>(positionX),
    //     static_cast<long long>(positionY),
    //     static_cast<long long>(positionZ),
    //     static_cast<long long>(user->m_data->m_nClass),
    //     static_cast<long long>(user->m_data->m_Race),
    //     static_cast<long long>(user->m_data->m_Sex),
    //     static_cast<long long>(user->m_data->m_nLevel),
    //     static_cast<long long>(static_cast<int>(m_eType)),
    //     static_cast<long long>(killPoint),
    //     userName,
    //     Utf16ToWide(std::u16string_view(
    //         user->m_sAccountName.data(),
    //         user->m_sAccountName.size())).c_str())
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
        static_cast<long long>(0x1f4f), static_cast<long long>(databaseId), static_cast<long long>(accountId),
        static_cast<long long>(positionX), static_cast<long long>(positionY), static_cast<long long>(positionZ),
        static_cast<long long>(user->m_data->m_nClass), static_cast<long long>(user->m_data->m_Race),
        static_cast<long long>(user->m_data->m_Sex), static_cast<long long>(user->m_data->m_nLevel),
        static_cast<long long>(static_cast<int>(m_eType)), static_cast<long long>(killPoint), userName,
        std::u16string_view(user->m_sAccountName.data(), user->m_sAccountName.size())
    );
#endif
    (void)reason;
    return removed;
}

bool CCrataeCube::_IsRegisteredUser(std::uint32_t userId) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x005d2408.
    CSPointer<User> user = FindUser(userId);
    if (!user) {
        return false;
    }
    auto applicant = m_applicantListByUserId.find(static_cast<int>(userId));
    if (applicant != m_applicantListByUserId.end()) {
        if (applicant->second == static_cast<int>(user->Id())) {
            return true;
        }
        _UnregisterUserById(userId, 2);
        return false;
    }
    auto participant = m_participantListByUserId.find(static_cast<int>(userId));
    if (participant == m_participantListByUserId.end()) {
        return false;
    }
    if (participant->second == static_cast<int>(user->Id())) {
        return true;
    }
    _UnregisterUserById(userId, 2);
    return false;
}

bool CCrataeCube::IsRegisteredUser(std::uint32_t userId) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x005d25e8 performs the DBID lookup before taking
    // the match lock. Each return releases the match lock before the user reference.
    CSPointer<User> user = FindUser(userId);
    if (!user) {
        return false;
    }
    L2_GUARD(m_Lock);
    auto applicant = m_applicantListByUserId.find(static_cast<int>(userId));
    if (applicant != m_applicantListByUserId.end()) {
        if (applicant->second != static_cast<int>(user->Id())) {
            _UnregisterUserById(userId, 2);
            return false;
        }

        return true;
    }

    auto participant = m_participantListByUserId.find(static_cast<int>(userId));
    if (participant == m_participantListByUserId.end()) {
        return false;
    }
    if (participant->second != static_cast<int>(user->Id())) {
        _UnregisterUserById(userId, 2);
        return false;
    }

    return true;
}

bool CCrataeCube::IsCrataeCubeUser(User& user) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x005d0fec reads the DBID once before
    // the lock and again for the map lookup.
    const std::uint32_t databaseId = user.GetDBID();
    L2_GUARD(m_Lock);
    const auto position = m_participantListByUserId.find(static_cast<int>(user.GetDBID()));
    if (position == m_participantListByUserId.end()) {
        return false;
    }
    if (position->second != static_cast<int>(user.Id())) {
        _UnregisterUserById(databaseId, 2);
        return false;
    }

    return true;
}

void CCrataeCube::RefreshUser(User* user) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x005cfc1c enters and immediately
    // leaves the creature lock before restoring CP, HP and MP from the base
    // values, in that non-main order.
    if (user != nullptr) {
        {
            L2_GUARD(*user->m_lock);
        }
        user->m_data->SetCP(user->m_data->m_dBaseCP);
        user->m_data->SetHP(user->m_data->m_dBaseHP);
        user->m_data->SetMP(user->m_data->m_dBaseMP);
    }
}

void CCrataeCube::ResurrectUser(User* user, FVector position) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->IsAlive()) {
        return;
    }
    user->TeleportToLocation(
        static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z),
        static_cast<std::uint32_t>(user->GetInZoneID()), 0x10000, TeleportSourceType::CrataeCubeResurrect, nullptr, 0
    );
    RefreshUser(user);
    db.SendSaveCharacterInfo(user, true, true);
    user->CheckAbnormalVisualEffect();
    user->m_data->SetAlive(true);
    user->m_lSaveCharacter = 0;
    db.SaveDieDropExp(*user, 0);
    if (user->GetSocket() != nullptr) {
        user->GetSocket()->Send("cd", 1, static_cast<int>(user->Id()));
    }
}

bool CCrataeCube::RegisterResurrectionNPC(std::uint32_t npcId) {
    L2_GUARD_FUNCTION();
    m_nResurrectionNpcId = npcId;
    return true;
}

bool CCrataeCube::CanResurrectedByMatch(User*) {
    L2_GUARD_FUNCTION();
    CObject* const object = CObject::GetObjectW(m_nResurrectionNpcId);
    CNPC* const npc = object != nullptr && object->IsNPC() ? object->CastNPC() : nullptr;
    return npc != nullptr && npc->IsAlive();
}

void CCrataeCube::_RegisterUserToRecordBoard(User* user) {
    L2_GUARD_FUNCTION();
    if (user != nullptr && _IsRegisteredUser(user->GetDBID())) {
        RefreshUser(user);
        m_RecordBoard.AddNewRecordData(user);
    }
}

void CCrataeCube::RegisterAllUsersToRecordBoard() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    for (auto it = m_applicantListByUserId.begin(); it != m_applicantListByUserId.end();) {
        const int userId = it->first;
        const int userSid = it->second;
        CObject* const userObject = CObject::GetObjectW(static_cast<std::uint32_t>(userSid));
        User* const user = userObject != nullptr && userObject->IsPC() ? userObject->CastUser() : nullptr;
        bool valid = user != nullptr;
        if (valid) {
            CObject* const managerObject = CObject::GetObjectW(GetManagerNpcId());
            CNPC* const manager = managerObject != nullptr && managerObject->IsNPC() ? managerObject->CastNPC() : nullptr;
            if (manager != nullptr) {
                FVector managerPosition = manager->GetPos();
                FVector userPosition = user->GetPos();
                valid = CheckDistance(managerPosition, userPosition, PLAYING_DISTANCE);
            }
            valid = valid && user->IsAlive();
        }
        if (!valid) {
            auto erasePosition = it++;
            _UnregisterUser(erasePosition, user ? 1 : 2);
            continue;
        }
        m_participantListByUserId[userId] = userSid;
        _RegisterUserToRecordBoard(user);
        user->m_data->m_PVPMatchType = PVPMatchType::CRATAE_CUBE;
        user->m_data->m_PVPMatchPlayerState = PVPMatchPlayerState::COMBAT;
        g_npcserver.Send("cdd", 0x7e, GetManagerNpcId(), static_cast<int>(user->Id()));
        ++it;
    }
    m_applicantListByUserId.clear();
}

bool CCrataeCube::ReadyMatch() {
    L2_GUARD_FUNCTION();
    if (m_nStatus != 2) {
        return false;
    }
    m_nStatus = 0;
    m_bGivePVPPoint = false;
    m_RecordBoard.Init();
    RegisterAllUsersToRecordBoard();
    m_dwStartTime = runtime::GetTickCount();
    m_dwEndTime = m_dwStartTime + PLAYING_TIME + WAITING_TIME;
    return true;
}

bool CCrataeCube::StartMatch() {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x005d06a4.
    if (m_nStatus != 0) {
        return false;
    }
    m_nStatus = 1;
    g_npcserver.Send("cd", 0x7f, GetManagerNpcId());
    {
        L2_GUARD(m_Lock);
        for (const auto& entry : m_participantListByUserId) {
            CObject* const object = CObject::GetObjectW(static_cast<std::uint32_t>(entry.second));
            User* const user = object != nullptr && object->IsPC() ? object->CastUser() : nullptr;
            if (user) {
                user->SendSystemMessage(SystemMessageId::MATCH_STARTED);
                SendCurrentRecordForUI(user, 0);

                const int positionZ = runtime::Win64CvttDoubleToInt32(user->GetPos().z);
                const int positionY = runtime::Win64CvttDoubleToInt32(user->GetPos().y);
                const int positionX = runtime::Win64CvttDoubleToInt32(user->GetPos().x);
                const char16_t* const userName = user->GetName();
                const std::uint32_t accountId = user->GetAccountID();
                const std::uint32_t databaseId = user->GetDBID();
                // Portable: the wide accounting formatter requires a host-wide account name.
                // Log.AddT(
                //     LOG_BOTH,
                //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                //     L"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
                //     0x1f50,
                //     databaseId,
                //     accountId,
                //     positionX,
                //     positionY,
                //     positionZ,
                //     user->m_data->m_nClass,
                //     user->m_data->m_Race,
                //     user->m_data->m_Sex,
                //     user->m_data->m_nLevel,
                //     static_cast<int>(m_eType),
                //     userName,
                //     user->m_sAccountName.data())
#if defined(_WIN32)
                Log.AddT(
                    LOG_BOTH,
                    u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                    u"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
                    0x1f50, databaseId, accountId, positionX, positionY, positionZ, user->m_data->m_nClass,
                    user->m_data->m_Race, user->m_data->m_Sex, user->m_data->m_nLevel, static_cast<int>(m_eType), userName,
                    std::u16string_view(user->m_sAccountName.data(), user->m_sAccountName.size())
                );
#else
                // Portable: %I64d becomes %lld. Pass matching 64-bit values to varargs.
                // Log.AddT(
                //     LOG_BOTH,
                //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                //     L"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
                //     static_cast<long long>(0x1f50),
                //     static_cast<long long>(databaseId),
                //     static_cast<long long>(accountId),
                //     static_cast<long long>(positionX),
                //     static_cast<long long>(positionY),
                //     static_cast<long long>(positionZ),
                //     static_cast<long long>(user->m_data->m_nClass),
                //     static_cast<long long>(user->m_data->m_Race),
                //     static_cast<long long>(user->m_data->m_Sex),
                //     static_cast<long long>(user->m_data->m_nLevel),
                //     static_cast<long long>(static_cast<int>(m_eType)),
                //     userName,
                //     Utf16ToWide(std::u16string_view(
                //         user->m_sAccountName.data(),
                //         user->m_sAccountName.size())).c_str())
                Log.AddT(
                    LOG_BOTH,
                    u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                    u"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
                    static_cast<long long>(0x1f50), static_cast<long long>(databaseId), static_cast<long long>(accountId),
                    static_cast<long long>(positionX), static_cast<long long>(positionY), static_cast<long long>(positionZ),
                    static_cast<long long>(user->m_data->m_nClass), static_cast<long long>(user->m_data->m_Race),
                    static_cast<long long>(user->m_data->m_Sex), static_cast<long long>(user->m_data->m_nLevel),
                    static_cast<long long>(static_cast<int>(m_eType)), userName,
                    std::u16string_view(user->m_sAccountName.data(), user->m_sAccountName.size())
                );
#endif
            }
        }
    }
    m_dwEndTime = runtime::GetTickCount() + PLAYING_TIME;
    return true;
}

void CCrataeCube::TriggerEndMatch() {
    L2_GUARD_FUNCTION();
    if (m_nStatus == 1) {
        m_dwEndTime = runtime::GetTickCount() + 11000;
    }
}

void CCrataeCube::EndMatchByTimeOut() {
    L2_GUARD_FUNCTION();
    TriggerEndMatch();
}

void CCrataeCube::EndMatch(int) {
    L2_GUARD_FUNCTION();
    if (m_nStatus != 1) {
        return;
    }
    m_nStatus = 2;
    char packet[1024]{};
    const int length = m_RecordBoard.MakeCurrentRecordPacket(packet, static_cast<int>(sizeof(packet)), 2);
    for (const auto& entry : m_participantListByUserId) {
        User* const user = FindUserByObjectId(static_cast<std::uint32_t>(entry.second));
        if (!user) {
            continue;
        }
        if (length > 0 && user->GetSocket() != nullptr) {
            user->GetSocket()->Send("chb", 0xfe, 0x8a, length, packet);
        }
        user->m_data->m_PVPMatchType = PVPMatchType::NONE;
        user->m_data->m_PVPMatchPlayerState = PVPMatchPlayerState::NONE;
    }
    PostEndMatch();
}

void CCrataeCube::OnNpcServerClosed() {
    L2_GUARD_FUNCTION();
    CPVPMatch2::OnNpcServerClosed();
    m_nStatus = 2;
}

void CCrataeCube::UserDie(User* user, User* enemy) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    if (enemy != nullptr) {
        g_npcserver.Send("cdddd", 0x7c, GetManagerNpcId(), static_cast<int>(user->Id()), static_cast<int>(enemy->Id()), 0);
        if (user->GetSocket() != nullptr) {
            user->GetSocket()->SendSystemMessage_S(SystemMessageId::KILLED_BY_CHARACTER_ATTACK, enemy->GetName());
        }
        if (enemy->GetSocket() != nullptr) {
            enemy->GetSocket()->SendSystemMessage_S(SystemMessageId::KILLED_CHARACTER_BY_ATTACK, user->GetName());
        }
    }
    CCCRecordBoardData* const record = GetRecordData(user);
    if (record != nullptr) {
        record->SetUserDieTime(runtime::GetTickCount());
    }
}

void CCrataeCube::AddKillPoint(User* user, int point) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x005d09ac.
    if (user == nullptr) {
        return;
    }
    const int killPoint = m_RecordBoard.AddKillPoint(user, point);
    SendCurrentRecordForUI(user, 1);
    SendCurrentRecordForUIToAllUser(1);

    const int positionZ = runtime::Win64CvttDoubleToInt32(user->GetPos().z);
    const int positionY = runtime::Win64CvttDoubleToInt32(user->GetPos().y);
    const int positionX = runtime::Win64CvttDoubleToInt32(user->GetPos().x);
    const char16_t* const userName = user->GetName();
    const std::uint32_t accountId = user->GetAccountID();
    const std::uint32_t databaseId = user->GetDBID();
    // Portable: the wide accounting formatter requires a host-wide account name.
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,%s,%s,,,",
    //     0x1f51,
    //     databaseId,
    //     accountId,
    //     positionX,
    //     positionY,
    //     positionZ,
    //     user->m_data->m_nClass,
    //     user->m_data->m_Race,
    //     user->m_data->m_Sex,
    //     user->m_data->m_nLevel,
    //     static_cast<int>(m_eType),
    //     killPoint,
    //     point,
    //     userName,
    //     user->m_sAccountName.data())
#if defined(_WIN32)
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,%s,%s,,,",
        0x1f51, databaseId, accountId, positionX, positionY, positionZ, user->m_data->m_nClass, user->m_data->m_Race,
        user->m_data->m_Sex, user->m_data->m_nLevel, static_cast<int>(m_eType), killPoint, point, userName,
        std::u16string_view(user->m_sAccountName.data(), user->m_sAccountName.size())
    );
#else
    // Portable: %I64d becomes %lld. Pass matching 64-bit values to varargs.
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,%s,%s,,,",
    //     static_cast<long long>(0x1f51),
    //     static_cast<long long>(databaseId),
    //     static_cast<long long>(accountId),
    //     static_cast<long long>(positionX),
    //     static_cast<long long>(positionY),
    //     static_cast<long long>(positionZ),
    //     static_cast<long long>(user->m_data->m_nClass),
    //     static_cast<long long>(user->m_data->m_Race),
    //     static_cast<long long>(user->m_data->m_Sex),
    //     static_cast<long long>(user->m_data->m_nLevel),
    //     static_cast<long long>(static_cast<int>(m_eType)),
    //     static_cast<long long>(killPoint),
    //     static_cast<long long>(point),
    //     userName,
    //     Utf16ToWide(std::u16string_view(
    //         user->m_sAccountName.data(),
    //         user->m_sAccountName.size())).c_str())
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,%s,%s,,,",
        static_cast<long long>(0x1f51), static_cast<long long>(databaseId), static_cast<long long>(accountId),
        static_cast<long long>(positionX), static_cast<long long>(positionY), static_cast<long long>(positionZ),
        static_cast<long long>(user->m_data->m_nClass), static_cast<long long>(user->m_data->m_Race),
        static_cast<long long>(user->m_data->m_Sex), static_cast<long long>(user->m_data->m_nLevel),
        static_cast<long long>(static_cast<int>(m_eType)), static_cast<long long>(killPoint), static_cast<long long>(point),
        userName, std::u16string_view(user->m_sAccountName.data(), user->m_sAccountName.size())
    );
#endif
}

CCCRecordBoardData* CCrataeCube::GetRecordData(User* user) { return m_RecordBoard.GetRecordData(user); }

void CCrataeCube::SendCurrentRecordForSystemMessage(User* user) { m_RecordBoard.SendCurrentRecordForSystemMessage(user); }

void CCrataeCube::SendCurrentRecordForUI(User* user, int type) { m_RecordBoard.SendCurrentRecordForUI(user, type); }

void CCrataeCube::SendCurrentRecordForUIToAllUser(int type) {
    L2_GUARD_FUNCTION();
    char packet[1024]{};
    const int length = m_RecordBoard.MakeCurrentRecordPacket(packet, static_cast<int>(sizeof(packet)), type);
    if (length <= 0) {
        return;
    }
    L2_GUARD(m_Lock);
    for (const auto& entry : m_participantListByUserId) {
        User* const user = FindUserByObjectId(static_cast<std::uint32_t>(entry.second));
        if (user && (type == 0 || type == 2 || user->m_bReceiveUserPVPMatchRecord) && user->GetSocket() != nullptr) {
            user->GetSocket()->Send("chb", 0xfe, 0x8a, length, packet);
        }
    }
}

void CCrataeCube::SendCurrentRecordForUIToOneUser(std::uint32_t userId, int type) {
    L2_GUARD_FUNCTION();
    char packet[1024]{};
    const int length = m_RecordBoard.MakeCurrentRecordPacket(packet, static_cast<int>(sizeof(packet)), type);
    if (length <= 0) {
        return;
    }
    L2_GUARD(m_Lock);
    const auto position = m_participantListByUserId.find(static_cast<int>(userId));
    if (position != m_participantListByUserId.end()) {
        User* const user = FindUserByObjectId(static_cast<std::uint32_t>(position->second));
        if (user && (type == 0 || type == 2 || user->m_bReceiveUserPVPMatchRecord) && user->GetSocket() != nullptr) {
            user->GetSocket()->Send("chb", 0xfe, 0x8a, length, packet);
        }
    }
}

int CCrataeCube::GetRankUserPVPMatch(std::uint32_t userId) {
    L2_GUARD_FUNCTION();
    CSPointer<User> user = FindUser(userId);
    CCCRecordBoardData* const record = user ? m_RecordBoard.GetRecordData(user.get()) : nullptr;
    return record != nullptr ? record->m_nRank : 0;
}

void CCrataeCube::IterationJob(bool combat) {
    L2_GUARD_FUNCTION();
    const std::uint32_t now = runtime::GetTickCount();
    // The NPC IDs stay 0 until L2NPC spawns these NPCs. CObject::GetObjectW
    // prints a call stack for ID 0 on every timer tick, so skip the lookup.
    CObject* const resurrectionObject =
        m_nResurrectionNpcId != 0 ? CObject::GetObjectW(m_nResurrectionNpcId) : nullptr;
    CNPC* const resurrectionNpc =
        resurrectionObject != nullptr && resurrectionObject->IsNPC() ? resurrectionObject->CastNPC() : nullptr;
    const std::uint32_t managerNpcId = GetManagerNpcId();
    CObject* const managerObject = managerNpcId != 0 ? CObject::GetObjectW(managerNpcId) : nullptr;
    CNPC* const managerNpc = managerObject != nullptr && managerObject->IsNPC() ? managerObject->CastNPC() : nullptr;
    L2_GUARD(m_Lock);
    for (auto it = m_participantListByUserId.begin(); it != m_participantListByUserId.end();) {
        const std::uint32_t userId = static_cast<std::uint32_t>(it->first);
        CObject* const userObject = CObject::GetObjectW(static_cast<std::uint32_t>(it->second));
        User* const user = userObject != nullptr && userObject->IsPC() ? userObject->CastUser() : nullptr;
        ++it;
        if (!user) {
            _UnregisterUserById(userId, 2);
            continue;
        }
        if (managerNpc != nullptr) {
            FVector managerPosition = managerNpc->GetPos();
            FVector userPosition = user->GetPos();
            if (!CheckDistance(managerPosition, userPosition, PLAYING_DISTANCE)) {
                _UnregisterUserById(userId, 1);
                continue;
            }
        }
        if (!combat || resurrectionNpc == nullptr || user->IsAlive()) {
            continue;
        }
        CCCRecordBoardData* const record = m_RecordBoard.GetRecordData(user);
        if (record == nullptr || record->GetUserDieTime() == 0) {
            continue;
        }
        const std::uint32_t elapsed = (now - record->GetUserDieTime()) / 1000;
        if (elapsed < 10) {
            if (user->GetSocket() != nullptr) {
                user->GetSocket()->Send(
                    "chcch", 0x62, SystemMessageId::WAITING_ROOM_RESURRECTION_IN_SECONDS, 1, 0x15,
                    static_cast<int>(10 - elapsed)
                );
            }
        } else {
            const double angle = ::genrand64_real2() * 6.283185307;
            FVector position = resurrectionNpc->GetPos();
            position.x += std::cos(angle) * 150.0;
            position.y += std::sin(angle) * 150.0;
            ResurrectUser(user, position);
            record->SetUserDieTime(0);
        }
    }
}

void CCrataeCube::TimerExpired(int) {
    L2_GUARD_FUNCTION();
    const std::uint32_t now = runtime::GetTickCount();
    const std::uint32_t seconds = (m_dwEndTime - now) / 1000;
    if (m_nStatus == 0) {
        IterationJob(false);
        if (seconds < 1201) {
            StartMatch();
        } else if (seconds <= 1230) {
            for (CCCRecordBoardData* record : m_RecordBoard.m_vecUserRecordBoardData) {
                CSPointer<User> user = FindUser(record->m_nUserId);
                if (user) {
                    user->SendSystemMessage_d(SystemMessageId::MATCH_STARTS_IN_SECONDS, static_cast<int>(seconds - 1200));
                }
            }
        }
    } else if (m_nStatus == 1) {
        IterationJob(seconds > 30);
        if (m_dwEndTime < now) {
            EndMatch(0);
        } else if (seconds <= 10) {
            for (CCCRecordBoardData* record : m_RecordBoard.m_vecUserRecordBoardData) {
                CSPointer<User> user = FindUser(record->m_nUserId);
                if (user) {
                    user->SendSystemMessage_d(
                        SystemMessageId::CRATAE_CUBE_GAME_END_SECONDS_REMAINING, static_cast<int>(seconds)
                    );
                }
            }
        }
    } else if (m_nStatus == 2) {
        std::time_t current = std::time(nullptr);
        const std::tm* const local = std::localtime(&current);
        if (local != nullptr && (local->tm_min == 27 || local->tm_min == 57)) {
            ReadyMatch();
        }
    }
    AddTimer(1000, 0);
}

void CCrataeCube::PostEndMatch() {
    L2_GUARD_FUNCTION();
    m_RecordBoard.SortRecordBoardData(m_RecordBoard.m_vecUserRecordBoardData);
    std::array<std::int32_t, 25> userIds{};
    int count = 0;
    {
        L2_GUARD(m_Lock);
        for (CCCRecordBoardData* record : m_RecordBoard.m_vecUserRecordBoardData) {
            CSPointer<User> user = FindUser(record->m_nUserId);
            if (!user) {
                continue;
            }
            user->SendSystemMessage(SystemMessageId::MATCH_ENDED);
            if (user->IsAlive()) {
                RefreshUser(user.get());
            } else {
                ResurrectUser(user.get(), user->GetPos());
            }
            if (count < static_cast<int>(userIds.size())) {
                userIds[count++] = static_cast<std::int32_t>(record->m_nUserId);
            }

            const int positionZ = runtime::Win64CvttDoubleToInt32(user->GetPos().z);
            const int positionY = runtime::Win64CvttDoubleToInt32(user->GetPos().y);
            const int positionX = runtime::Win64CvttDoubleToInt32(user->GetPos().x);
            const char16_t* const userName = user->GetName();
            const std::uint32_t accountId = user->GetAccountID();
            const std::uint32_t databaseId = user->GetDBID();
            // Portable: the wide accounting formatter requires a host-wide account name.
            // Log.AddT(
            //     LOG_BOTH,
            //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
            //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,%s,%s,,,",
            //     0x1f52,
            //     databaseId,
            //     accountId,
            //     positionX,
            //     positionY,
            //     positionZ,
            //     user->m_data->m_nClass,
            //     user->m_data->m_Race,
            //     user->m_data->m_Sex,
            //     user->m_data->m_nLevel,
            //     static_cast<int>(m_eType),
            //     record->m_nRank,
            //     count,
            //     userName,
            //     user->m_sAccountName.data())
#if defined(_WIN32)
            Log.AddT(
                LOG_BOTH,
                u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,%s,%s,,,",
                0x1f52, databaseId, accountId, positionX, positionY, positionZ, user->m_data->m_nClass, user->m_data->m_Race,
                user->m_data->m_Sex, user->m_data->m_nLevel, static_cast<int>(m_eType), record->m_nRank, count, userName,
                std::u16string_view(user->m_sAccountName.data(), user->m_sAccountName.size())
            );
#else
            // Portable: %I64d becomes %lld. Pass matching 64-bit values to varargs.
            // Log.AddT(
            //     LOG_BOTH,
            //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
            //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,%s,%s,,,",
            //     static_cast<long long>(0x1f52),
            //     static_cast<long long>(databaseId),
            //     static_cast<long long>(accountId),
            //     static_cast<long long>(positionX),
            //     static_cast<long long>(positionY),
            //     static_cast<long long>(positionZ),
            //     static_cast<long long>(user->m_data->m_nClass),
            //     static_cast<long long>(user->m_data->m_Race),
            //     static_cast<long long>(user->m_data->m_Sex),
            //     static_cast<long long>(user->m_data->m_nLevel),
            //     static_cast<long long>(static_cast<int>(m_eType)),
            //     static_cast<long long>(record->m_nRank),
            //     static_cast<long long>(count),
            //     userName,
            //     Utf16ToWide(std::u16string_view(
            //         user->m_sAccountName.data(),
            //         user->m_sAccountName.size())).c_str())
            Log.AddT(
                LOG_BOTH,
                u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,%s,%s,,,",
                static_cast<long long>(0x1f52), static_cast<long long>(databaseId), static_cast<long long>(accountId),
                static_cast<long long>(positionX), static_cast<long long>(positionY), static_cast<long long>(positionZ),
                static_cast<long long>(user->m_data->m_nClass), static_cast<long long>(user->m_data->m_Race),
                static_cast<long long>(user->m_data->m_Sex), static_cast<long long>(user->m_data->m_nLevel),
                static_cast<long long>(static_cast<int>(m_eType)), static_cast<long long>(record->m_nRank),
                static_cast<long long>(count), userName,
                std::u16string_view(user->m_sAccountName.data(), user->m_sAccountName.size())
            );
#endif
        }
        m_participantListByUserId.clear();
    }
    g_npcserver.Send("cdddddd", 0x81, GetManagerNpcId(), userIds[0], userIds[1], userIds[2], userIds[3], userIds[4]);
    CPVPMatch2::PostEndMatch();
}

void CCrataeCube::SendEndMatchPacket() { g_npcserver.Send("cd", 0x81, GetManagerNpcId()); }
