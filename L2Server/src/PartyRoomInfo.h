#pragma once

#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

class CMultiPartyCommandChannel;
class CMPCCRoomInfo;
class CParty;
class CPartyMatchingRequest;
class CPartyRoomInfo;
class CRoomMember;
class User;

class PartyRoomHistoryData {
public:
    PartyRoomHistoryData() : m_nLength(0) {}

    int m_nLength;
    char m_buf[124];
};

class CPartyRoomHistoryManager final : public Singleton<CPartyRoomHistoryManager> {
public:
    CPartyRoomHistoryManager();
    ~CPartyRoomHistoryManager() override;

    void Init();
    void AddHistory(CPartyRoomInfo& roomInfo);
    void SendHistory(CSPointer<User> user);
    void DeleteAll();


private:
    bool AssembleData(CPartyRoomInfo& roomInfo, PartyRoomHistoryData& history);
    bool IsHistoryFull() const;

    RWLock m_rwLock;
    std::int32_t m_nPushPos;
    PartyRoomHistoryData m_roomHistory[101];
};

// Current L2Server CPartyRoomInfo envelope. The target xallocator string is
// 0x28 bytes, making both the base and CMPCC specialization 0x58 bytes.
class CPartyRoomInfo {
public:
    static constexpr int MAX_MEMBER_LIMIT = 12;

    CPartyRoomInfo();
    CPartyRoomInfo(
        int id,
        CSPointer<User> user,
        int lowerLevelLimit,
        int upperLevelLimit,
        int maxMemberLimit,
        int partyRoutingType,
        const char16_t* title
    );
    CPartyRoomInfo(const CPartyRoomInfo& other);
    virtual ~CPartyRoomInfo();

    int GetId() const;
    CSPointer<CRoomMember> GetMaster() const;
    CSPointer<User> GetMasterUser() const;
    int GetLowerLevelLimit() const;
    int GetUpperLevelLimit() const;
    int GetMaxMemberLimit() const;
    std::u16string& GetTitle();
    unsigned int GetPartyRoutingType() const;
    int GetLocation() const;
    CParty* GetParty() const;

    void SetMaster(CSPointer<User> user);
    void SetNewId();

    bool IsMaster(CSPointer<User> user) const;
    virtual bool IsPartyMember(CSPointer<User> user) const;
    bool IsBetweenLevelLimit(CSPointer<User> user) const;
    bool IsCorrectLevelLimit() const;
    bool IsForCreatingNewPartyRoom() const;

    virtual CMPCCRoomInfo* CastCMPCCRoomInfo();
    virtual bool IsCMPCCRoomInfo();
    virtual bool CreateRoom(CPartyMatchingRequest& request);

    CPartyRoomInfo& operator=(const CPartyRoomInfo& other);

    bool m_isMpCCRoomInfo;

protected:
    void Init(
        int id,
        CSPointer<User> user,
        int lowerLevelLimit,
        int upperLevelLimit,
        int maxMemberLimit,
        int partyRoutingType,
        const char16_t* title
    );
    virtual void SetMaxMemberLimit(int maxMemberLimit);
    void SetLowerLevelLimit(int lowerLevelLimit);
    void SetUpperLevelLimit(int upperLevelLimit);

    static int GetNextRoomId();

    std::byte m_alignmentPadding09[3];
    std::int32_t m_id;
    CSPointer<CRoomMember> m_master;
    std::int32_t m_lowerLevelLimit;
    std::int32_t m_upperLevelLimit;
    std::int32_t m_maxMemberLimit;
    std::int32_t m_partyRoutingType;
    std::u16string m_title;

private:
    static RWLock s_lock;
    static std::int32_t s_nextRoomId;
};

class CMPCCRoomInfo final : public CPartyRoomInfo {
public:
    static constexpr int MAX_MEMBER_LIMIT = 50;

    CMPCCRoomInfo();
    CMPCCRoomInfo(
        int id,
        CSPointer<User> user,
        int lowerLevelLimit,
        int upperLevelLimit,
        int maxMemberLimit,
        int partyRoutingType,
        const char16_t* title
    );
    CMPCCRoomInfo(const CMPCCRoomInfo& other);
    ~CMPCCRoomInfo() override;

    CSPointer<CMultiPartyCommandChannel> GetMPCC() const;
    bool IsMPCCPartyMaster(CSPointer<User> user) const;
    bool IsMPCCPartyMember(CSPointer<User> user) const;

    bool IsPartyMember(CSPointer<User> user) const override;
    CMPCCRoomInfo* CastCMPCCRoomInfo() override;
    bool IsCMPCCRoomInfo() override;
    bool CreateRoom(CPartyMatchingRequest& request) override;
    CMPCCRoomInfo& operator=(const CMPCCRoomInfo& other);

protected:
    void SetMaxMemberLimit(int maxMemberLimit) override;
};

static_assert(std::is_trivially_copyable_v<PartyRoomHistoryData>);
static_assert(std::is_trivially_destructible_v<PartyRoomHistoryData>);
