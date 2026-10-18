#pragma once

#include "Shared/src/MemorySub.h"

#include "Shared/src/BaseAttribute.h"
#include "Shared/src/Obj.h"
#include "Shared/src/PledgeMasteryID.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"

#include <array>
#include <cstddef>
#include <cstdint>

class IPacketHelper;
class CAuction;
class User;
template <std::size_t Capacity>
class CPacketHelperEx;

// shared/EtcMani.h value kept by the matching PDB and by the current
// ItemUtil machine code. Enchant is part of the in-memory 0x18-byte carrier
// but the legacy pledge-item wire list serializes only dQ.
struct CreateItemStruct {
    CreateItemStruct() : ItemID(0), Count(0), Enchant(0) {}

    CreateItemStruct(int itemId, std::int64_t count, int enchant) : ItemID(itemId), Count(count), Enchant(enchant) {}

    int ItemID; // +0x00
    std::int64_t Count; // +0x08
    int Enchant; // +0x10
};


namespace ItemUtil {

void AssembleItemList(const std::vector<CreateItemStruct>& items, CPacketHelperEx<20480>& packet);
const unsigned char* DisassembleItemList(const unsigned char* packet, int count, std::vector<CreateItemStruct>& items);

} // namespace ItemUtil

namespace L2EtcMani {

const unsigned char* ParseEtcMani(int count, const unsigned char* packet);

} // namespace L2EtcMani


// Matching PDB global enum used by CETC_MANI_POINT_UPDATE.
enum UserPointType : std::int32_t {
    PT_NONE = 0,
    PT_PCCAFE_POINT = 1,
    PT_PLEDGE_POINT = 2,
    PT_HOME_PCCAFE_COUNT = 3,
    PT_HOME_DAILY_MAX_SET_POINT = 4,
    PT_PVP_POINT = 5,
    PT_VITALITY_POINT = 6,
    PT_FIELDCYCLE_POINT = 7,
    PT_RAID_POINT = 8,
    PT_BLOCK_PENALTY_POINT = 9,
    PT_MAX = 10,
};

// Shared/EtcMani.h enum recovered from the matching NASC64_2015 PDB.
// The numeric values are serialized as the leading packet subtype.
enum ETC_MANI_TYPE : std::int32_t {
    ETC_MANI_UPDATE_PLEDGE_NAMEVALUE = 0,
    ETC_MANI_UPDATE_USER_EVENT_TIME = 1,
    ETC_MANI_UPDATE_USER_ITEM_BASE_ATTRIBUTE = 2,
    ETC_MANI_ROLLBACK_USER_ITEM_BASE_ATTRIBUTE = 3,
    ETC_MANI_CREATE_ITEM_AUCTION_BID = 4,
    ETC_MANI_WITHDRAW_ITEM_AUCTION_BID = 5,
    ETC_MANI_GIVE_ITEM_BY_CASTLE_SIEGE_COUNT = 6,
    ETC_MANI_GIVE_ITEM_ON_ACCOUNT = 7,
    ETC_MANI_UPDATE_USER_ITEM_COMBINATION = 8,
    ETC_MANI_ENSOUL = 9,
    ETC_MANI_MULTISELL = 10,
    ETC_MANI_SUCCESSFUL_ITEM_AUCTION_BID = 11,
    ETC_MANI_CARD_UPDOWN_GAME = 12,
    ETC_MANI_REMOVE_INZONE_BINDING_BY_EVENT = 13,
    ETC_MANI_PLEDGE_MASTERY_SET = 14,
    ETC_MANI_PLEDGE_MASTERY_RESET = 15,
    ETC_MANI_POINT_UPDATE = 16,
    ETC_MANI_PLEDGE_MISSION_REWARD = 17,
    ETC_MANI_PLEDGE_CONTRIBUTION_REWARD = 18,
    ETC_MANI_USE_COUNT = 19,
    ETC_MANI_ENCHANT_ARTIFACT = 20,
    ETC_MANI_MAX = 21,
};

// Exact Shared/EtcMani.h polymorphic root. The method order and 0x20 layout
// come from the matching NASC64_2015 PDB. The active L2NPC executable confirms
// the same char-buffer Assemble slot and Destroy ownership path.
class EtcMani : public MemoryObject {
public:
    EtcMani();
    ~EtcMani() override = default;

    L2_DECLARE_OBJECT_TYPE(EtcMani)

    virtual int Assemble(IPacketHelper& packet);
    virtual int Assemble(char* buffer, int bufferSize);
    virtual const unsigned char* Disassemble(const unsigned char* packet);
    virtual bool Rollback() = 0;
    virtual bool DoAtServer() = 0;
    virtual void DumpError();
    virtual void Destroy();

    bool DoAtCache();
    bool IsSuccess();

private:
    virtual bool DoAtCacheInternal() = 0;

    bool m_isSuccess = false;

    // The Win64 base ends at +0x20. Without explicit tail storage, Itanium
    // ABIs can place the first derived member directly after m_isSuccess.
    std::array<std::uint8_t, 7> m_targetBaseExtentPadding{};
};


class EtcManiUpdatePledgeNameValue final : public EtcMani {
public:
    EtcManiUpdatePledgeNameValue();
    EtcManiUpdatePledgeNameValue(int worldId, int userSid, unsigned int pledgeDbId, int delta);

    int Assemble(IPacketHelper& packet) override;
    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Rollback() override;
    bool DoAtServer() override;
    void DumpError() override;

private:
    bool DoAtCacheInternal() override;

    int m_worldId = 0;
    int m_userSid = 0;
    unsigned int m_pledgeDbId = 0;
    int m_delta = 0;
};


class EtcManiUpdateUserEventTime final : public EtcMani {
public:
    EtcManiUpdateUserEventTime();

    int Assemble(IPacketHelper& packet) override;
    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Rollback() override;
    bool DoAtServer() override;
    void DumpError() override;

private:
    friend class User;

    bool DoAtCacheInternal() override;

    int m_userDBID = 0;
    int m_timeGap = 0;
    int m_eventTime = 0;
    int m_oldEventTime = 0;
    int m_eventType = 0;
    int m_eventVariable = 0;
    int m_nDelItemClassID = 0;
    std::int64_t m_nDelItemAmount = 0;
    int m_nAddItemClassID = 0;
    std::int64_t m_nAddItemAmount = 0;
    int m_nAddItemEnchant = 0;
    int m_nRet = 0;
};


class EtcManiUpdateUserItemBaseAttribute final : public EtcMani {
public:
    EtcManiUpdateUserItemBaseAttribute();
    EtcManiUpdateUserItemBaseAttribute(
        int userSid, int userDBID, int targetItemDBID, int intensiveItemClassId, int enchantType, BaseAttribute attribute
    );

    int Assemble(IPacketHelper& packet) override;
    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Rollback() override;
    bool DoAtServer() override;
    void DumpError() override;

private:
    bool DoAtCacheInternal() override;

    unsigned int m_nUserSid = 0;
    int m_nUserDBID = 0;
    int m_nTargetItemDBID = 0;
    int m_nIntensiveItemClassId = 0;
    int m_nEnchantType = 0;
    BaseAttribute m_Attribute;
    BaseAttribute m_oldAttribute;
};


class EtcManiRollbackUserItemBaseAttribute final : public EtcMani {
public:
    EtcManiRollbackUserItemBaseAttribute();
    EtcManiRollbackUserItemBaseAttribute(unsigned int userSid, int userDbId, int targetItemDbId, int removeAttribute);

    int Assemble(IPacketHelper& packet) override;
    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Rollback() override;
    bool DoAtServer() override;
    void DumpError() override;

private:
    bool DoAtCacheInternal() override;

    unsigned int m_nUserSid = 0;
    int m_nUserDBID = 0;
    int m_nTargetItemDBID = 0;
    int m_nResult = 0;
    int m_nRemoveAttribute = 0;
    BaseAttribute m_oldAttribute;
};


class EtcManiCreateItemAuctionBid final : public EtcMani {
public:
    EtcManiCreateItemAuctionBid();

    int Assemble(IPacketHelper& packet) override;
    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Rollback() override;
    bool DoAtServer() override;
    void DumpError() override;

private:
    friend class CAuction;

    bool DoAtCacheInternal() override;

    unsigned int m_nUserSid = 0;
    unsigned int m_nUserDBID = 0;
    int m_nAuctionID = 0;
    int m_nBidID = 0;
    std::int64_t m_nBiddingPrice = 0;
    int m_nTransactionID = 0;
};


class EtcManiWithdrawItemAuctionBid final : public EtcMani {
public:
    EtcManiWithdrawItemAuctionBid();

    int Assemble(IPacketHelper& packet) override;
    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Rollback() override;
    bool DoAtServer() override;
    void DumpError() override;

private:
    friend class CAuction;

    bool DoAtCacheInternal() override;

    int m_nBidID = 0;
    unsigned int m_nUserDBID = 0;
    int m_nAuctionID = 0;
    std::int64_t m_nAdenaAmount = 0;
    bool m_bLog = false;
};


class EtcManiGiveItemByCastleSiegeCount final : public EtcMani {
public:
    EtcManiGiveItemByCastleSiegeCount();
    EtcManiGiveItemByCastleSiegeCount(int userSid, int userDbId, int pledgeDbId, int castleSiegeCount, int npcId) :
        m_UserSid(static_cast<unsigned int>(userSid)),
        m_UserDBID(userDbId),
        m_PledgeDBID(pledgeDbId),
        m_CastleSiegeCount(castleSiegeCount),
        m_NpcId(npcId) {}

    int Assemble(IPacketHelper& packet) override;
    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Rollback() override;
    bool DoAtServer() override;
    void DumpError() override;

private:
    bool DoAtCacheInternal() override;

    unsigned int m_UserSid = 0;
    int m_UserDBID = 0;
    int m_PledgeDBID = 0;
    int m_CastleSiegeCount = 0;
    int m_NpcId = 0;
};


class EtcManiGiveItemOnAccount final : public EtcMani {
public:
    EtcManiGiveItemOnAccount();
    EtcManiGiveItemOnAccount(
        int userDbId,
        int condition,
        int deleteItemClassId,
        std::int64_t deleteItemAmount,
        int addItemCount,
        const int* addItemClassIds,
        const std::int64_t* addItemAmounts,
        const int* addItemEnchants,
        int type,
        unsigned int accountId
    );
    EtcManiGiveItemOnAccount(
        int userDbId,
        int condition,
        int deleteItemClassId,
        std::int64_t deleteItemAmount,
        int addItemClassId,
        std::int64_t addItemAmount,
        int addItemEnchant,
        int type,
        unsigned int accountId,
        int npcId,
        const wchar_t* eventName,
        int eventStartTime
    );

    int Assemble(IPacketHelper& packet) override;
    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Rollback() override;
    bool DoAtServer() override;
    void DumpError() override;

private:
    bool DoAtCacheInternal() override;
    bool ProcessByAI();
    bool ProcessAttendanceEvent();

    int m_userDBID = 0;
    int m_nCond = 0;
    unsigned int m_nAccountID = 0;
    int m_nDelItemClassID = 0;
    std::int64_t m_nDelItemAmount = 0;
    int m_nAddItemCount = 0;
    std::array<int, 10> m_AddItemClassID{};
    std::array<int, 10> m_AddItemEnchant{};
    std::array<std::int64_t, 10> m_AddItemAmount{};
    int m_nType = 0;
    int m_nRet = 0;
    int m_nNpcId = 0;
    std::array<char16_t, 50> m_sEventName{};
    int m_nEventStartTime = 0;
};


class EtcManiUpdateUserItemCombination final : public EtcMani {
public:
    EtcManiUpdateUserItemCombination();

    int Assemble(IPacketHelper& packet) override;
    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Rollback() override;
    bool DoAtServer() override;
    void DumpError() override;

private:
    friend class User;

    bool DoAtCacheInternal() override;

    unsigned int m_nUserSid = 0;
    int m_nUserDBID = 0;
    bool m_isSuccess = false;
    int m_nItemID = 0;
    int m_nDel1ItemID = 0;
    int m_nDel2ItemID = 0;
};


class EtcManiEnsoul final : public EtcMani {
public:
    EtcManiEnsoul();
    EtcManiEnsoul(int targetItemDbId, int userDbId, int userSid, std::map<int, int> ensoulInfo[3]);
    EtcManiEnsoul(int targetItemDbId, int userDbId, int userSid, std::map<int, int> ensoulInfo[3], bool extraction);
    // Current implicit scalar deleting destructor 0x004c3670 destroys both
    // arrays of three EnsoulInfo xmaps before the EtcMani/MemoryObject bases.

    int Assemble(IPacketHelper& packet) override;
    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Rollback() override;
    bool DoAtServer() override;
    void DumpError() override;

private:
    bool DoAtCacheInternal() override;

    std::array<std::map<int, int>, 3> m_mEnsoulInfo;
    std::array<std::map<int, int>, 3> m_mOldEnsoulInfo;
    int m_nUserSid;
    int m_nUserDBID;
    int m_nTargetItemDBID;
    bool m_bIsExtractionJob;
};


class EtcManiMultisellResult final : public EtcMani {
public:
    EtcManiMultisellResult();

    int Assemble(IPacketHelper& packet) override;
    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Rollback() override;
    bool DoAtServer() override;
    void DumpError() override;

private:
    friend class User;

    bool DoAtCacheInternal() override;

    int m_nUserDBID = 0;
    int m_nUserSID = 0;
    int m_ePointType = 0;
    int m_nPoint = 0;
    int m_nGroupId = 0;
    bool m_bSuccess = false;
};


class EtcManiSuccessfulItemAuctionBid final : public EtcMani {
public:
    EtcManiSuccessfulItemAuctionBid();

    int Assemble(IPacketHelper& packet) override;
    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Rollback() override;
    bool DoAtServer() override;
    void DumpError() override;

private:
    friend class CAuction;

    bool DoAtCacheInternal() override;

    int m_nBidID = 0;
    unsigned int m_nUserDBID = 0;
    int m_nAuctionID = 0;
    std::int64_t m_nAdenaAmount = 0;
    int m_nItemClassId = 0;
    int m_nItemCount = 0;
    int m_nAuctionNPCId = 0;
};


class EtcManiCardUpDownGame final : public EtcMani {
public:
    EtcManiCardUpDownGame();

    int Assemble(IPacketHelper& packet) override;
    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Rollback() override;
    bool DoAtServer() override;
    void DumpError() override;

private:
    friend class User;

    bool DoAtCacheInternal() override;

    int m_nRollbackGameState = 0;
    int m_nGameId = 0;
    int m_nReqType = 0;
    int m_nResult = 0;
    int m_nUserSid = 0;
    int m_nUserDBID = 0;
};


class EtcManiRemoveInzoneBindingByEvent final : public EtcMani {
public:
    EtcManiRemoveInzoneBindingByEvent();

    int Assemble(IPacketHelper& packet) override;
    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Rollback() override;
    bool DoAtServer() override;
    void DumpError() override;

private:
    friend class User;

    bool DoAtCacheInternal() override;

    int m_nUserSid = 0;
    int m_nResult = 0;
    int m_nInzoneTypeID = 0;
    int m_isGroupRestriction = 0;
};


class CETC_MANI_PLEDGE_MASTERY_SET final : public EtcMani {
public:
    CETC_MANI_PLEDGE_MASTERY_SET();
    CETC_MANI_PLEDGE_MASTERY_SET(int worldId, int pledgeId, PledgeMasteryID::Type masteryId, int point, unsigned int userId);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Rollback() override;
    bool DoAtServer() override;
    void DumpError() override;

private:
    bool DoAtCacheInternal() override;

    int m_idWorld;
    int m_idPledge;
    PledgeMasteryID::Type m_idMastery;
    int m_nPoint;
    unsigned int m_idUser;
    int m_nPrevPoint;
};


class CETC_MANI_PLEDGE_MASTERY_RESET final : public EtcMani {
public:
    CETC_MANI_PLEDGE_MASTERY_RESET();
    CETC_MANI_PLEDGE_MASTERY_RESET(int worldId, int pledgeId, unsigned int userId);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Rollback() override;
    bool DoAtServer() override;
    void DumpError() override;

private:
    bool DoAtCacheInternal() override;

    int m_idWorld;
    int m_idPledge;
    unsigned int m_idUser;
    int m_anPrevPoint[PledgeMasteryID::MAX_COUNT];
};


class CETC_MANI_POINT_UPDATE final : public EtcMani {
public:
    CETC_MANI_POINT_UPDATE();
    CETC_MANI_POINT_UPDATE(unsigned int userSid, int userDbId, UserPointType pointType, int pointAmount);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Rollback() override;
    bool DoAtServer() override;
    void DumpError() override;

private:
    bool DoAtCacheInternal() override;

    unsigned int m_sidUser;
    int m_dbidUser;
    UserPointType m_pointType;
    int m_pointAmount;
};


class CETC_MANI_PLEDGE_MISSION_REWARD final : public EtcMani {
public:
    CETC_MANI_PLEDGE_MISSION_REWARD();
    CETC_MANI_PLEDGE_MISSION_REWARD(int userDbId, int missionId, int count, std::int32_t saveRewardTime);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Rollback() override;
    bool DoAtServer() override;
    void DumpError() override;

private:
    bool DoAtCacheInternal() override;

    int nUserDbId_;
    int nMissionId_;
    int nCount_;
    std::int32_t tSaveRewardTime_;
    std::int32_t tRewardTime_;
};


class CETC_MANI_PLEDGE_CONTRIBUTION_REWARD final : public EtcMani {
public:
    CETC_MANI_PLEDGE_CONTRIBUTION_REWARD();
    CETC_MANI_PLEDGE_CONTRIBUTION_REWARD(unsigned int userSid, int userDbId, int pledgeId, int rewardStep, int minPoint);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Rollback() override;
    bool DoAtServer() override;
    void DumpError() override;

private:
    bool DoAtCacheInternal() override;

    unsigned int m_sidUser;
    int m_dbidUser;
    int m_idPledge;
    int m_nRewardStep;
    int m_nMinPoint;
};


class CETC_MANI_USE_COUNT final : public EtcMani {
public:
    CETC_MANI_USE_COUNT();
    CETC_MANI_USE_COUNT(unsigned int userSid, int userDbId, unsigned int itemId, short useCount, short maxUseCount);

    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Rollback() override;
    bool DoAtServer() override;
    void DumpError() override;

private:
    bool DoAtCacheInternal() override;

    unsigned int m_sidUser;
    int m_dbidUser;
    unsigned int m_itemId;
    short m_useCount;
    short m_maxUseCount;
};


class EtcManiEnchantArtifact final : public EtcMani {
public:
    EtcManiEnchantArtifact();

    int Assemble(IPacketHelper& packet) override;
    int Assemble(char* buffer, int bufferSize) override;
    const unsigned char* Disassemble(const unsigned char* packet) override;
    bool Rollback() override;
    bool DoAtServer() override;
    void DumpError() override;

    void Set(
        int userSid,
        int userDbId,
        bool enchantSuccess,
        int targetItemDbId,
        int targetNewEnchant,
        int materialItemDbId1,
        int materialItemDbId2,
        int materialItemDbId3
    );

private:
    bool DoAtCacheInternal() override;

    int m_nUserSID;
    int m_nUserDBID;
    bool m_bEnchantSuccess;
    int m_nTargetItemDBID;
    int m_nTargetNewEnchant;
    std::array<int, 3> m_nMaterialItemDBID;
    int m_nTargetOrgEnchant;
};


using EtcManiCreator = EtcMani* (*)();
using EtcManiFactory = GenericFactory<EtcMani, ETC_MANI_TYPE, EtcManiCreator>;


EtcMani* Create_EtcManiUpdatePledgeNameValue();
EtcMani* Create_EtcManiUpdateUserEventTime();
EtcMani* Create_EtcManiUpdateUserItemBaseAttribute();
EtcMani* Create_EtcManiRollbackUserItemBaseAttribute();
EtcMani* Create_EtcManiCreateItemAuctionBid();
EtcMani* Create_EtcManiWithdrawItemAuctionBid();
EtcMani* Create_EtcManiGiveItemByCastleSiegeCount();
EtcMani* Create_EtcManiGiveItemOnAccount();
EtcMani* Create_EtcManiUpdateUserItemCombination();
EtcMani* Create_EtcManiEnsoul();
EtcMani* Create_EtcManiMultisellResult();
EtcMani* Create_EtcManiSuccessfulItemAuctionBid();
EtcMani* Create_EtcManiCardUpDownGame();
EtcMani* Create_EtcManiRemoveInzoneBindingByEvent();
EtcMani* Create_CETC_MANI_PLEDGE_MASTERY_SET();
EtcMani* Create_CETC_MANI_PLEDGE_MASTERY_RESET();
EtcMani* Create_CETC_MANI_POINT_UPDATE();
EtcMani* Create_CETC_MANI_PLEDGE_MISSION_REWARD();
EtcMani* Create_CETC_MANI_PLEDGE_CONTRIBUTION_REWARD();
EtcMani* Create_CETC_MANI_USE_COUNT();
EtcMani* Create_EtcManiEnchantArtifact();

namespace EtcManiUtil {

void AssembleEtcManis(const std::vector<EtcMani*>& etcManis, CPacketHelperEx<20480>& packet);
void Destroy(std::vector<EtcMani*>& etcManis);
bool DoAtCache(const std::vector<EtcMani*>& etcManis, CPacketHelperEx<20480>& resultPacket);
void Rollback(const std::vector<EtcMani*>& etcManis);
const unsigned char* DisassembleEtcManis(const unsigned char* packet, int count, std::vector<EtcMani*>& etcManis);

} // namespace EtcManiUtil
