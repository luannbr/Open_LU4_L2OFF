#pragma once

#include "Shared/src/StandardContainers.h"

#include <cstdint>

// Original owner: Protocol/L2Mobile/L2MobileNoti.h. The complete outer
// declaration owns one invalid-length constant and the original nested type
// structure. Only the thirty-three current-machine-closed builders are defined
// here. The remaining nested owners stay incomplete.
class MakeBufferL2MobileNoti {
public:
    static const int INVALID_SEND_BUFFER_LEN = 0;

    class L2M_PING_FROM_L2SERVER;
    class ML2_PONG_FROM_MOBILESERVER;
    class L2M_TESTENUM_FROM_L2SERVER;
    class L2M_TEST_PACKET;
    class L2M_CHAR_LOG_IN;
    class L2M_CHAR_LOG_OUT;
    class L2M_PC_KILL_PC;
    class L2M_CREATE_NPC;
    class L2M_DESPAWN_NPC;
    class L2M_DIE_BOSS_NPC;
    class L2M_PRIVATESTORE_OPEN;
    class L2M_PRIVATESTORE_BUY;
    class L2M_PRIVATESTORE_CLOSE;
    class L2M_WORLD_CHAT;
    class L2M_FISHING_ITEM;
    class L2M_TOWER_DESTRUCT;
    class L2M_DOOR_DESTRUCT;
    class L2M_SIEGE_START;
    class L2M_SIEGE_END;
    class L2M_SIEGE_SCORE;
    class L2M_BOSS_REGEN;
    class L2M_CHANAGE_PLEDGE_ANNOUNCE;
    class L2M_PLEDGE_WAR_DECLARE;
    class L2M_PARTY_MATCHING_LIST;
    class L2M_USER_PING;
    class L2M_ENTER_OLYMPIAD;
    class L2M_ENTER_PLEDGEGAME;
    class L2M_ENTER_CASTLE;
    class L2M_ENTER_FORTRESS;
    class L2M_ENTER_AGIT;
    class L2M_ENTER_RAID_SERVER;
    class L2M_GET_RAID_POINT;
    class L2M_COMMISSION_TRADE_BUY;
    class L2M_PRIVATESTORE_BUY_BUY;
    class L2M_GIVE_POST_RESULT;
    class L2M_CONNECT_SERVER;
    class L2M_DISCONNECT_SERVER;
    class L2M_CONNECTED_SERVER_LIST;
    class ML2_Test;
    class ML2_GIVE_POST;
    class L2M_SAY_PLEDGE;
    class ML2_SAY_PLEDGE;
    class L2M_SAY_ALLIANCE;
    class ML2_SAY_ALLIANCE;
};

class MakeBufferL2MobileNoti::L2M_CREATE_NPC {
public:
    static int MakeSendBuffer(unsigned char* buff, int maxBuffLength, int worldId, int npcId);
};

class MakeBufferL2MobileNoti::L2M_DESPAWN_NPC {
public:
    static int MakeSendBuffer(unsigned char* buff, int maxBuffLength, int worldId, int npcId);
};

class MakeBufferL2MobileNoti::L2M_DIE_BOSS_NPC {
public:
    static int MakeSendBuffer(unsigned char* buff, int maxBuffLength, int worldId, int npcId);
};

class MakeBufferL2MobileNoti::L2M_TOWER_DESTRUCT {
public:
    static int MakeSendBuffer(
        unsigned char* buff,
        int maxBuffLength,
        int worldId,
        std::int64_t x,
        std::int64_t y,
        std::int64_t z,
        int residenceId,
        int towerId
    );
};

class MakeBufferL2MobileNoti::L2M_DOOR_DESTRUCT {
public:
    static int MakeSendBuffer(
        unsigned char* buff, int maxBuffLength, int worldId, int residenceId, int doorType, int damageGrade
    );
};

class MakeBufferL2MobileNoti::L2M_SIEGE_START {
public:
    static int MakeSendBuffer(unsigned char* buff, int maxBuffLength, int worldId, int residenceId);
};

class MakeBufferL2MobileNoti::L2M_SIEGE_END {
public:
    static int MakeSendBuffer(unsigned char* buff, int maxBuffLength, int worldId, int residenceId);
};

class MakeBufferL2MobileNoti::L2M_SIEGE_SCORE {
public:
    static int MakeSendBuffer(
        unsigned char* buff,
        int maxBuffLength,
        int worldId,
        int residenceId,
        std::int64_t attackerKillScore,
        std::int64_t defenderKillScore
    );
};

class MakeBufferL2MobileNoti::L2M_BOSS_REGEN {
public:
    static int MakeSendBuffer(unsigned char* buff, int maxBuffLength, int worldId, int npcId, std::int64_t spawnTime);
};

class MakeBufferL2MobileNoti::L2M_PLEDGE_WAR_DECLARE {
public:
    static int MakeSendBuffer(
        unsigned char* buff,
        int maxBuffLength,
        int declareWorldId,
        int declarePledgeId,
        int declaredWorldId,
        int declaredPledgeId,
        int type
    );
};

class MakeBufferL2MobileNoti::L2M_COMMISSION_TRADE_BUY {
public:
    static int MakeSendBuffer(unsigned char* buff, int maxBuffLength, int worldId, int commissionId);
};

class MakeBufferL2MobileNoti::L2M_GIVE_POST_RESULT {
public:
    static int MakeSendBuffer(
        unsigned char* buff,
        int maxBuffLength,
        int presentId,
        int worldId,
        int charDBId,
        int itemTemplateId,
        std::int64_t itemAmount,
        bool isSuccess
    );
};

class MakeBufferL2MobileNoti::L2M_CHAR_LOG_IN {
public:
    static int MakeSendBuffer(
        unsigned char* buff,
        int maxBuffLength,
        int worldId,
        int accountId,
        int charId,
        int pledgeId,
        const std::u16string charName,
        std::int64_t exp,
        int lev,
        bool isPCCafeUser,
        int type
    );
};

class MakeBufferL2MobileNoti::L2M_CHAR_LOG_OUT {
public:
    static int MakeSendBuffer(
        unsigned char* buff,
        int maxBuffLength,
        int worldId,
        int aaccountId,
        int charId,
        int pledgeId,
        const std::u16string charName,
        std::int64_t exp,
        int lev,
        int charPlayTime
    );
};

class MakeBufferL2MobileNoti::L2M_PC_KILL_PC {
public:
    static int MakeSendBuffer(
        unsigned char* buff,
        int maxBuffLength,
        int dieUserWorldId,
        int dieUserDBId,
        const std::u16string dieUserName,
        int dieUserPledgeId,
        int killerWorldId,
        int killerDBId,
        const std::u16string killerName,
        int killerPledgeId,
        int type
    );
};

class MakeBufferL2MobileNoti::L2M_PRIVATESTORE_CLOSE {
public:
    static int MakeSendBuffer(
        unsigned char* buff,
        int maxBuffLength,
        int worldId,
        int accountId,
        int charId,
        int pledgeId,
        const std::u16string charName
    );
};

class MakeBufferL2MobileNoti::L2M_WORLD_CHAT {
public:
    static int MakeSendBuffer(
        unsigned char* buff,
        int maxBuffLength,
        int worldId,
        int accountId,
        int charId,
        int pledgeId,
        const std::u16string charName,
        const std::u16string contents
    );
};

class MakeBufferL2MobileNoti::L2M_FISHING_ITEM {
public:
    static int MakeSendBuffer(
        unsigned char* buff,
        int maxBuffLength,
        int worldId,
        int accountId,
        int charId,
        int pledgeId,
        const std::u16string charName,
        int itemId,
        std::int64_t exp,
        std::int64_t sp
    );
};

class MakeBufferL2MobileNoti::L2M_CHANAGE_PLEDGE_ANNOUNCE {
public:
    static int MakeSendBuffer(
        unsigned char* buff, int maxBuffLength, int worldId, int pledgeId, const std::u16string contents
    );
};

class MakeBufferL2MobileNoti::L2M_ENTER_OLYMPIAD {
public:
    static int MakeSendBuffer(
        unsigned char* buff,
        int maxBuffLength,
        int worldId,
        int accountId,
        int charId,
        int pledgeId,
        const std::u16string charName
    );
};

class MakeBufferL2MobileNoti::L2M_ENTER_PLEDGEGAME {
public:
    static int MakeSendBuffer(
        unsigned char* buff,
        int maxBuffLength,
        int worldId,
        int accountId,
        int charId,
        int pledgeId,
        const std::u16string charName
    );
};

class MakeBufferL2MobileNoti::L2M_ENTER_CASTLE {
public:
    static int MakeSendBuffer(
        unsigned char* buff,
        int maxBuffLength,
        int worldId,
        int accountId,
        int charId,
        int pledgeId,
        const std::u16string charName
    );
};

class MakeBufferL2MobileNoti::L2M_ENTER_FORTRESS {
public:
    static int MakeSendBuffer(
        unsigned char* buff,
        int maxBuffLength,
        int worldId,
        int accountId,
        int charId,
        int pledgeId,
        const std::u16string charName
    );
};

class MakeBufferL2MobileNoti::L2M_ENTER_AGIT {
public:
    static int MakeSendBuffer(
        unsigned char* buff,
        int maxBuffLength,
        int worldId,
        int accountId,
        int charId,
        int pledgeId,
        const std::u16string charName
    );
};

class MakeBufferL2MobileNoti::L2M_ENTER_RAID_SERVER {
public:
    static int MakeSendBuffer(
        unsigned char* buff,
        int maxBuffLength,
        int worldId,
        int accountId,
        int charId,
        int pledgeId,
        const std::u16string charName
    );
};

class MakeBufferL2MobileNoti::L2M_GET_RAID_POINT {
public:
    static int MakeSendBuffer(
        unsigned char* buff,
        int maxBuffLength,
        int worldId,
        int accountId,
        int charId,
        int pledgeId,
        const std::u16string charName,
        int npcClassId,
        int point,
        int totalPoint
    );
};

class MakeBufferL2MobileNoti::L2M_SAY_PLEDGE {
public:
    static int MakeSendBuffer(
        unsigned char* buff,
        int maxBuffLength,
        int worldId,
        int pledgeId,
        int charId,
        const std::u16string charName,
        const std::u16string chatMsg
    );
};

class MakeBufferL2MobileNoti::L2M_SAY_ALLIANCE {
public:
    static int MakeSendBuffer(
        unsigned char* buff,
        int maxBuffLength,
        int worldId,
        int allianceId,
        int pledgeId,
        int charId,
        const std::u16string charName,
        const std::u16string chatMsg
    );
};

class MakeBufferL2MobileNoti::L2M_PRIVATESTORE_OPEN {
public:
    struct ItemListTuple {
        int itemId;
        int ItemDbId;
        std::int64_t price;
        std::int64_t count;

        ItemListTuple(int itemId, int ItemDbId, std::int64_t price, std::int64_t count) :
            itemId(itemId), ItemDbId(ItemDbId), price(price), count(count) {}
    };

    static int MakeSendBuffer(
        unsigned char* buff,
        int maxBuffLength,
        int worldId,
        int accountId,
        int charId,
        int pledgeId,
        const std::u16string charName,
        std::int64_t x,
        std::int64_t y,
        std::int64_t z,
        int itemCount,
        int privateStoreType,
        std::vector<ItemListTuple> ItemList
    );
};

class MakeBufferL2MobileNoti::L2M_PRIVATESTORE_BUY {
public:
    struct BuyItemListTuple {
        int ItemId;
        int ItemDbId;
        std::int64_t Price;
        std::int64_t count;

        BuyItemListTuple(int ItemId, int ItemDbId, std::int64_t Price, std::int64_t count) :
            ItemId(ItemId), ItemDbId(ItemDbId), Price(Price), count(count) {}
    };

    struct RemainItemListTuple {
        int ItemId;
        int ItemDbId;
        std::int64_t Price;
        std::int64_t count;

        RemainItemListTuple(int ItemId, int ItemDbId, std::int64_t Price, std::int64_t count) :
            ItemId(ItemId), ItemDbId(ItemDbId), Price(Price), count(count) {}
    };

    static int MakeSendBuffer(
        unsigned char* buff,
        int maxBuffLength,
        int sellerworldId,
        int sellerAccountId,
        int sellerCharId,
        int sellerPledgeId,
        const std::u16string sellerCharName,
        int buyerworldId,
        int buyerAccountId,
        int buyerCharId,
        int buyerPledgeId,
        const std::u16string buyerCharName,
        int BuyItemCount,
        std::vector<BuyItemListTuple> BuyItemList,
        int privateStoreType,
        int RemainItemCount,
        std::vector<RemainItemListTuple> RemainItemList
    );
};

class MakeBufferL2MobileNoti::L2M_USER_PING {
public:
    struct ItemListTuple {
        int itemId;
        int itemDbId;
        std::int64_t price;
        std::int64_t count;

        ItemListTuple(int itemId, int itemDbId, std::int64_t price, std::int64_t count) :
            itemId(itemId), itemDbId(itemDbId), price(price), count(count) {}
    };

    static int MakeSendBuffer(
        unsigned char* buff,
        int maxBuffLength,
        int worldId,
        int accountId,
        int charId,
        int pledgeId,
        const std::u16string charName,
        std::int64_t exp,
        int lev,
        bool bPcCafe,
        int premiumType,
        int posX,
        int posY,
        int posZ,
        std::int64_t getExp,
        std::int64_t getSp,
        std::int64_t getAdena,
        int privateStoreType,
        std::vector<ItemListTuple> ItemList
    );
};

class MakeBufferL2MobileNoti::L2M_PRIVATESTORE_BUY_BUY {
public:
    struct SellItemListTuple {
        int ItemId;
        int ItemDbId;
        std::int64_t Price;
        std::int64_t count;

        SellItemListTuple(int ItemId, int ItemDbId, std::int64_t Price, std::int64_t count) :
            ItemId(ItemId), ItemDbId(ItemDbId), Price(Price), count(count) {}
    };

    struct RemainBuyItemListTuple {
        int ItemId;
        int ItemDbId;
        std::int64_t Price;
        std::int64_t count;

        RemainBuyItemListTuple(int ItemId, int ItemDbId, std::int64_t Price, std::int64_t count) :
            ItemId(ItemId), ItemDbId(ItemDbId), Price(Price), count(count) {}
    };

    static int MakeSendBuffer(
        unsigned char* buff,
        int maxBuffLength,
        int buyerworldId,
        int buyerAccountId,
        int buyerCharId,
        int buyerPledgeId,
        const std::u16string buyerCharName,
        int sellerworldId,
        int sellerAccountId,
        int sellerCharId,
        int sellerPledgeId,
        const std::u16string sellerCharName,
        int SellItemCount,
        std::vector<SellItemListTuple> SellItemList,
        int RemainBuyItemCount,
        std::vector<RemainBuyItemListTuple> RemainBuyItemList
    );
};

class MakeBufferL2MobileNoti::L2M_PARTY_MATCHING_LIST {
public:
    struct partyRoomInfoTuple {
        std::u16string charName;
        int lowerLev;
        int upperLev;
        int maxMember;
        std::u16string title;
        int currentMember;

        partyRoomInfoTuple(std::u16string, int, int, int, std::u16string, int);
    };

    static int MakeSendBuffer(
        unsigned char* buff,
        int maxBuffLength,
        int worldId,
        int listCount,
        std::vector<partyRoomInfoTuple> partyRoomInfo,
        bool firstPacket
    );
};

class ReadBufferL2MobileNoti {
public:
    struct ML2_GIVE_POST {
        int presentId;
        int worldId;
        int charDBId;
        int itemTemplateId;
        std::int64_t itemAmount;
        std::u16string mailTitle;
        std::u16string mailBody;
    };

    struct ML2_SAY_PLEDGE {
        int worldId;
        int pledgeId;
        int charId;
        std::u16string charName;
        std::u16string chatMsg;
    };

    struct ML2_SAY_ALLIANCE {
        int worldId;
        int allianceId;
        int pledgeId;
        int charId;
        std::u16string charName;
        std::u16string chatMsg;
    };

    static bool Read_ML2_GIVE_POST(ML2_GIVE_POST& packet, unsigned char* buff, int offset, int packetBodyLen);
    static bool Read_ML2_SAY_PLEDGE(ML2_SAY_PLEDGE& packet, unsigned char* buff, int offset, int packetBodyLen);
    static bool Read_ML2_SAY_ALLIANCE(ML2_SAY_ALLIANCE& packet, unsigned char* buff, int offset, int packetBodyLen);
};
