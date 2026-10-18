#pragma once

#include "Object.h"
#include "Packet.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include "L2Core/src/inc/XStd.h"
#include "Shared/src/ScopedLockGuards.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeObjectMap.h"

#include <cstddef>
#include <cstdint>


class CNPC;
class CDB;
class CDBSocket;
class CPledge;
class CPledgePenaltyViewer;
class RaidClientSocket;
class User;

bool ReplyDismissAlliance(CDBSocket* socket, const unsigned char* packet);

// Exact current L2Server alliance envelope. The factory record and lock are
// borrowed. The DBID-to-SID tree is the only owned member allocation.
class CAlliance : public CObject {
public:
    CAlliance(
        std::uint32_t databaseId,
        const wchar_t* name,
        std::uint32_t masterPledgeDatabaseId,
        std::int32_t worldId,
        std::uint32_t oustTime,
        std::uint32_t crestId
    );
    ~CAlliance() override;

    int GetIndex() const override;
    CObject* Copy() override;
    void SendDB(const char* format, ...) const override;

    static void InitAlliance();
    static void CreateAllianceByNpc(CNPC* npc, User* user, const char16_t* name);
    static CAlliance* GetObjectW(std::uint32_t smartId);

    bool CanAcceptNewMemberPledge();
    int GetMemberPledgeCount() const {
        int memberPledgeCount;
        {
            L2_GUARD(*m_lock);
            memberPledgeCount = static_cast<int>(m_mapPledge.size());
        }
        return memberPledgeCount;
    }
    void Save(std::uint32_t selector);
    CPledge* GetMasterPledge();
    bool IsRaidAlliance();
    void GetAllMemberPledge(std::vector<CPledge*>& pledges);
    void Say(User* user, const char16_t* message, SayPacketType type, bool unknownFlag, const char16_t* targetName);
    void SayByMobile(int firstValue, int secondValue, const char16_t* senderName, const char16_t* message, SayPacketType type);
    void SetAllianceCrestToMember(std::uint32_t crestId);
    void AddMemberPledge(CPledge* pledge);
    void DeleteMemberPledge(std::uint32_t pledgeDatabaseId);


protected:
    virtual FVector GetPos() const;

public:
    ::CYieldLock* m_lock;
    CSharedAllianceData* m_data;

protected:
    friend class CDB;
    friend class CPledgePenaltyViewer;
    friend class User;
    friend bool ReplyDismissAlliance(CDBSocket* socket, const unsigned char* packet);
    friend bool RaidRequestDismissAlliance(RaidClientSocket* socket, const unsigned char* packet);

    ::std::map<std::uint32_t, std::uint32_t> m_mapPledge;
    std::uint32_t m_nMasterPledgeServerId;
    std::int32_t m_nWorldId;
};

using CAllianceSmartIdManager = CSmartIdManager<CAlliance, 5000, 16>;
using CAllianceDbIdMap = CObjectMap<CAlliance, 1, 5, RWLock, std::CDualKey<int, int>>;
using CAllianceNameMap = CObjectByIntKeyStringMap<CAlliance>;

// Donor spellings. Current L2Server proves all three global
// specializations, their template parameters and their single instances.
extern CAllianceDbIdMap g_AllianceDbIdMap;
extern CAllianceNameMap g_AllianceNameMap;
extern CAllianceSmartIdManager g_AllianceSmartIdManager;

CAllianceSmartIdManager* AllianceSmartIdManager() noexcept;
