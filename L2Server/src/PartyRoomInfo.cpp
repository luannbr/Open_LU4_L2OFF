#include "PartyRoomInfo.h"

#include "Cmpccroom.h"
#include "MultiPartyCommandChannel.h"
#include "MultiPartyCommandChannelRegistry.h"
#include "Party.h"
#include "PartyMatchingRequest.h"
#include "PartyRoomMember.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/TemplateHelper.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

RWLock CPartyRoomInfo::s_lock;
std::int32_t CPartyRoomInfo::s_nextRoomId = 1;

CPartyRoomHistoryManager::CPartyRoomHistoryManager() :
    m_nPushPos(0), m_roomHistory{} {}

CPartyRoomHistoryManager::~CPartyRoomHistoryManager() = default;

void CPartyRoomHistoryManager::Init() {
    m_nPushPos = 0;
    std::memset(m_roomHistory, 0, sizeof(m_roomHistory));
}

bool CPartyRoomHistoryManager::IsHistoryFull() const { return m_roomHistory[m_nPushPos].m_nLength > 0; }

bool CPartyRoomHistoryManager::AssembleData(CPartyRoomInfo& roomInfo, PartyRoomHistoryData& history) {
    L2_GUARD_FUNCTION();
    const CSPointer<User> master = roomInfo.GetMasterUser();
    if (!roomInfo.GetParty() || !master || master->IsDeleting() || master->m_Deleted != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]invalid cond",
        //     L"PartyRoomInfo.cpp",
        //     0x1be)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d]invalid cond",
        //     L"PartyRoomInfo.cpp",
        //     0x1be)
        L2_LOG_WARN(
            Log,
            "Cannot encode history for party room %d: the party is missing or its leader is unavailable or being deleted.",
            roomInfo.GetId()
        );
        return false;
    }

    const std::u16string masterName = std::u16string(master->GetName());
    history.m_nLength = ::Assemble(
        history.m_buf, static_cast<int>(sizeof(history.m_buf)), "SS", roomInfo.GetTitle().c_str(), masterName.c_str()
    );
    return true;
}

void CPartyRoomHistoryManager::AddHistory(CPartyRoomInfo& roomInfo) {
    L2_GUARD_FUNCTION();
    const CSPointer<User> master = roomInfo.GetMasterUser();
    if (!roomInfo.GetParty() || !master || master->IsDeleting() || master->m_Deleted != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]invalid cond",
        //     L"PartyRoomInfo.cpp",
        //     0x1a4)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d]invalid cond",
        //     L"PartyRoomInfo.cpp",
        //     0x1a4)
        L2_LOG_WARN(
            Log,
            "Cannot append history for party room %d: the party is missing or its leader is unavailable or being deleted.",
            roomInfo.GetId()
        );
        return;
    }

    L2_WRITE_GUARD(m_rwLock);
    if (AssembleData(roomInfo, m_roomHistory[m_nPushPos])) {
        m_nPushPos = (m_nPushPos + 1) % 101;
    }
}

void CPartyRoomHistoryManager::SendHistory(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return;
    }

    CPacketHelperEx<20480> packet;
    int historyCount = 0;
    {
        L2_READ_GUARD(m_rwLock);
        if (!IsHistoryFull()) {
            for (int index = 0; index < m_nPushPos; ++index) {
                PartyRoomHistoryData& history = m_roomHistory[index];
                packet.Assemble("b", history.m_nLength, history.m_buf);
            }
            historyCount = m_nPushPos;
        } else {
            for (int index = m_nPushPos + 1; index < 101; ++index) {
                PartyRoomHistoryData& history = m_roomHistory[index];
                packet.Assemble("b", history.m_nLength, history.m_buf);
            }
            for (int index = 0; index < m_nPushPos; ++index) {
                PartyRoomHistoryData& history = m_roomHistory[index];
                packet.Assemble("b", history.m_nLength, history.m_buf);
            }
            historyCount = 100;
        }
    }

    CUserSocket* const socket = user->GetSocket();
    if (socket) {
        socket->Send("chdb", 0xfe, 0x1a5, historyCount, packet.GetLength(), packet.GetPacket());
    }
}

void CPartyRoomHistoryManager::DeleteAll() {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_rwLock);
    for (PartyRoomHistoryData& history : m_roomHistory) {
        history.m_nLength = 0;
    }
    m_nPushPos = 0;
}

CPartyRoomInfo::CPartyRoomInfo() :
    m_isMpCCRoomInfo(false),
    m_alignmentPadding09{},
    m_id(0),
    m_lowerLevelLimit(0),
    m_upperLevelLimit(0),
    m_maxMemberLimit(0),
    m_partyRoutingType(0) {}

CPartyRoomInfo::CPartyRoomInfo(
    int id,
    CSPointer<User> user,
    int lowerLevelLimit,
    int upperLevelLimit,
    int maxMemberLimit,
    int partyRoutingType,
    const char16_t* title
) :
    CPartyRoomInfo() {
    Init(id, std::move(user), lowerLevelLimit, upperLevelLimit, maxMemberLimit, partyRoutingType, title);
}

CPartyRoomInfo::CPartyRoomInfo(const CPartyRoomInfo& other) : CPartyRoomInfo() { *this = other; }

CPartyRoomInfo::~CPartyRoomInfo() = default;

int CPartyRoomInfo::GetId() const { return m_id; }

CSPointer<CRoomMember> CPartyRoomInfo::GetMaster() const { return m_master; }

CSPointer<User> CPartyRoomInfo::GetMasterUser() const {
    const CSPointer<CRoomMember> master = GetMaster();
    return master ? master->GetUser() : CSPointer<User>();
}

int CPartyRoomInfo::GetLowerLevelLimit() const { return m_lowerLevelLimit; }

int CPartyRoomInfo::GetUpperLevelLimit() const { return m_upperLevelLimit; }

int CPartyRoomInfo::GetMaxMemberLimit() const { return m_maxMemberLimit; }

std::u16string& CPartyRoomInfo::GetTitle() { return m_title; }

unsigned int CPartyRoomInfo::GetPartyRoutingType() const {
    CParty* const party = GetParty();
    return party ? static_cast<unsigned int>(party->GetRoutingType()) : static_cast<unsigned int>(m_partyRoutingType);
}

int CPartyRoomInfo::GetLocation() const {
    L2_GUARD_FUNCTION();
    const CSPointer<User> master = GetMasterUser();
    return master ? master->GetBBSLocation() : 0;
}

CParty* CPartyRoomInfo::GetParty() const {
    const CSPointer<User> master = GetMasterUser();
    return master ? master->GetParty() : nullptr;
}

void CPartyRoomInfo::SetMaster(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return;
    }

    m_master = IsCMPCCRoomInfo() ? CRoomMemberFactory::MakeMPCCMaster(std::move(user))
                                 : CRoomMemberFactory::MakePartyRoomMaster(std::move(user));
}

int CPartyRoomInfo::GetNextRoomId() {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(s_lock);
    return s_nextRoomId++;
}

void CPartyRoomInfo::SetNewId() {
    L2_GUARD_FUNCTION();
    m_id = GetNextRoomId();
}

bool CPartyRoomInfo::IsMaster(CSPointer<User> user) const { return GetMasterUser().get() == user.get(); }

bool CPartyRoomInfo::IsPartyMember(CSPointer<User> user) const {
    L2_GUARD_FUNCTION();
    CParty* const party = GetParty();
    return party && user && party->IsMember(user->GetSmartID().Sid());
}

bool CPartyRoomInfo::IsBetweenLevelLimit(CSPointer<User> user) const {
    L2_GUARD_FUNCTION();
    if (!user || !user->m_data) {
        return false;
    }
    const int level = user->m_data->m_nLevel;
    return m_lowerLevelLimit <= level && level <= m_upperLevelLimit;
}

bool CPartyRoomInfo::IsCorrectLevelLimit() const { return m_lowerLevelLimit <= m_upperLevelLimit; }

bool CPartyRoomInfo::IsForCreatingNewPartyRoom() const { return m_id == 0; }

CMPCCRoomInfo* CPartyRoomInfo::CastCMPCCRoomInfo() { return nullptr; }

bool CPartyRoomInfo::IsCMPCCRoomInfo() { return false; }

bool CPartyRoomInfo::CreateRoom(CPartyMatchingRequest& request) {
    L2_GUARD_FUNCTION();
    CSPointer<CPartyRoom> room(new CPartyRoom(*this), ObjectRefType::FunctionChain);
    if (!CPartyRoomRegistry::inst().Register(room)) {
        return false;
    }
    request.ResponseOnCreateRoom(std::move(room));
    return true;
}

void CPartyRoomInfo::Init(
    int id,
    CSPointer<User> user,
    int lowerLevelLimit,
    int upperLevelLimit,
    int maxMemberLimit,
    int partyRoutingType,
    const char16_t* title
) {
    L2_GUARD_FUNCTION();
    m_id = id;
    SetMaster(std::move(user));
    SetLowerLevelLimit(lowerLevelLimit);
    SetUpperLevelLimit(upperLevelLimit);
    SetMaxMemberLimit(maxMemberLimit);
    m_partyRoutingType = partyRoutingType;
    m_title.assign(title != nullptr ? title : u"");
}

void CPartyRoomInfo::SetMaxMemberLimit(int maxMemberLimit) {
    L2_GUARD_FUNCTION();
    m_maxMemberLimit = std::min(maxMemberLimit, MAX_MEMBER_LIMIT);
}

void CPartyRoomInfo::SetLowerLevelLimit(int lowerLevelLimit) { m_lowerLevelLimit = std::max(lowerLevelLimit, 1); }

void CPartyRoomInfo::SetUpperLevelLimit(int upperLevelLimit) { m_upperLevelLimit = std::min(upperLevelLimit, 120); }

CPartyRoomInfo& CPartyRoomInfo::operator=(const CPartyRoomInfo& other) {
    L2_GUARD_FUNCTION();
    if (this == &other) {
        return *this;
    }

    m_id = other.m_id;
    SetMaster(other.GetMasterUser());
    SetLowerLevelLimit(other.m_lowerLevelLimit);
    SetUpperLevelLimit(other.m_upperLevelLimit);
    SetMaxMemberLimit(other.m_maxMemberLimit);
    m_partyRoutingType = other.m_partyRoutingType;
    m_title = other.m_title;
    m_isMpCCRoomInfo = other.m_isMpCCRoomInfo;
    return *this;
}

CMPCCRoomInfo::CMPCCRoomInfo() { m_isMpCCRoomInfo = true; }

CMPCCRoomInfo::CMPCCRoomInfo(
    int id,
    CSPointer<User> user,
    int lowerLevelLimit,
    int upperLevelLimit,
    int maxMemberLimit,
    int partyRoutingType,
    const char16_t* title
) :
    CPartyRoomInfo(id, std::move(user), lowerLevelLimit, upperLevelLimit, maxMemberLimit, partyRoutingType, title) {
    SetMaxMemberLimit(maxMemberLimit);
    m_isMpCCRoomInfo = true;
}

CMPCCRoomInfo::CMPCCRoomInfo(const CMPCCRoomInfo& other) : CMPCCRoomInfo() { *this = other; }

CMPCCRoomInfo::~CMPCCRoomInfo() = default;

CSPointer<CMultiPartyCommandChannel> CMPCCRoomInfo::GetMPCC() const {
    L2_GUARD_FUNCTION();
    const CSPointer<User> master = GetMasterUser();
    if (!master) {
        return {};
    }
    return CMultiPartyCommandChannelRegistry::FindByMasterParty(master->GetParty());
}

bool CMPCCRoomInfo::IsMPCCPartyMaster(CSPointer<User> user) const {
    const CSPointer<CMultiPartyCommandChannel> channel = GetMPCC();
    return channel && user && channel->GetMaster() == user.get();
}

bool CMPCCRoomInfo::IsMPCCPartyMember(CSPointer<User> user) const {
    L2_GUARD_FUNCTION();
    const CSPointer<CMultiPartyCommandChannel> channel = GetMPCC();
    return channel && user && channel->IsMember(static_cast<int>(user->GetSmartID().Sid()));
}

bool CMPCCRoomInfo::IsPartyMember(CSPointer<User> user) const {
    L2_GUARD_FUNCTION();
    return IsMPCCPartyMember(std::move(user));
}

CMPCCRoomInfo* CMPCCRoomInfo::CastCMPCCRoomInfo() { return this; }

bool CMPCCRoomInfo::IsCMPCCRoomInfo() { return true; }

bool CMPCCRoomInfo::CreateRoom(CPartyMatchingRequest& request) {
    L2_GUARD_FUNCTION();
    CSPointer<CPartyRoom> room(new CMPCCRoom(*this), ObjectRefType::FunctionChain);
    if (!CMPCCRoomRegistry::inst().Register(room)) {
        return false;
    }
    request.ResponseOnCreateRoom(std::move(room));
    return true;
}

CMPCCRoomInfo& CMPCCRoomInfo::operator=(const CMPCCRoomInfo& other) {
    CPartyRoomInfo::operator=(other);
    m_isMpCCRoomInfo = true;
    return *this;
}

void CMPCCRoomInfo::SetMaxMemberLimit(int maxMemberLimit) {
    L2_GUARD_FUNCTION();
    m_maxMemberLimit = std::min(maxMemberLimit, MAX_MEMBER_LIMIT);
}
