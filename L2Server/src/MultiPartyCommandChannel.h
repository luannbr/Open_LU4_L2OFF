#pragma once

#include "CGeneralObject.h"
#include "MultiPartyVisitor.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

class CCreature;
class CNPC;
class CParty;
class CUserSocket;
class User;

enum MPCCCreateType : std::int32_t {
    MCT_NORMAL_SKILL = 0,
    MCT_ITEM = 1,
    MCT_MAX = 2,
};

enum MPCCRoutingType : std::int32_t {
    MRT_NORMAL = 0,
    MRT_MASTER_PARTY_FIRST = 1,
    MRT_MAX = 2,
};


namespace DivideType {
enum Enum : std::int32_t;
}

// Exact current L2Server envelope at 01065e18. Field spellings are
// replacement vocabulary. Every offset and ownership boundary below is
// fixed by the current image.
class CMultiPartyCommandChannel : public CGeneralObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CMultiPartyCommandChannel)

    explicit CMultiPartyCommandChannel(std::uint32_t masterPartySid);
    ~CMultiPartyCommandChannel() override;

    CMultiPartyCommandChannel(const CMultiPartyCommandChannel&) = delete;
    CMultiPartyCommandChannel& operator=(const CMultiPartyCommandChannel&) = delete;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    int GetIndex() const override;
    bool IsMPCC() override;
    CMultiPartyCommandChannel* CastMPCC() override;

    static void SetMaxPartyCanJoin(int maximumPartyCount);
    int GetMembersForTeleport(std::vector<User*>& users, int range);
    void CalcMPCCTeleportPointCount(int memberCount, int& pointCount, int& pointSize);
    void CalcMPCCTeleportPointList(
        std::vector<FVector>& points, const FVector& center, int pointCount, int minimumDistance, int maximumDistance
    );
    int TeleportMPCCToLocation(
        int x,
        int y,
        int z,
        int range,
        int minimumDistance,
        int maximumDistance,
        int itemClassId,
        std::int64_t itemCount,
        int questId,
        int inZoneId
    );
    void SetRouting(MPCCRoutingType routingType, int npcId);
    int GetRouting();
    CNPC* GetRoutingNpc();
    void SetMasterPartyRouting(CNPC* npc);
    void ResetMasterPartyRouting();
    int GetPartyCount();
    bool IsAboutToClose();
    int GetMemberCount();
    int GetMaxLevelOfAllMember();
    int GetMinLevelOfAllMember(bool checkDistance, int radius, FVector position, bool checkFilter, int filterArgument);
    void GetAllMemberWhoCanGetExpSp(std::vector<User*>& users, CCreature* creature);
    void Accept(IMultiPartyVisitor& visitor);
    bool Contains(CParty* party);
    void GetAllParty(std::vector<CParty*>& parties);
    void GetAllMember(std::vector<User*>& users);
    void ShowPartyMembersInfo(User* user, CParty* party);
    void CancelTradeAllMember();
    CParty* GetMasterParty();
    bool AddParty(CParty* party);
    bool RemoveParty(CParty* party);
    User* GetMaster();
    bool IsMaster(int sid);
    bool IsMember(int sid);
    void DivideAdenaStart();
    void SendCancelPacket(int messageId, DivideType::Enum divideType);
    void DivideAdenaCancel(DivideType::Enum divideType);
    void DivideAdena(std::int64_t totalAmount);

private:
    friend class CMultiPartyCommandChannelManager;
    friend class CUserSocket;
    friend class User;

    std::vector<std::uint32_t>::iterator _Find(CParty* party);
    void _UpdateSharedPartySidList();

    ::CYieldLock m_mpccLock;
    volatile std::int32_t m_divide;
    [[maybe_unused]] std::byte padding_64[4];
    std::vector<std::uint32_t> m_partySidList;

public:
    bool m_isTeleporting;

private:
    [[maybe_unused]] std::byte padding_89[3];
    MPCCRoutingType m_routingType;
    std::int32_t m_routingNpcId;
    [[maybe_unused]] std::byte padding_94[4];

    inline static const wchar_t* m_pClassName = L"CMultiPartyCommandChannel";
    inline static std::int32_t s_maxPartyCanJoin = 50;
};
