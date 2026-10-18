#pragma once

#include "Shared/src/IOC.h"
#include "Shared/src/Obj.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/Systems.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>

class User;
class CGoodsInventorySocket;

enum GoodsInventoryJobResult : std::int32_t {
    GIJR_S_OK = 0,
    GIJR_SA_SERVER_DISCONNECTED = 1,
    GIJR_INVALID_JOB = 2,
};


struct ItemIncludedInNewGoods {
    int m_nItemPrimaryId;
    int m_nItemClassId;
};


struct UserItemIncludedInNewGoods {
    int m_nItemPrimaryId;
    int m_nItemClassId;
    int m_nItemAmount;
    ::FileTime m_nDeliveryDate;
};


struct InquiryItemInGoods {
    int classId;
    int quantity;
};


struct InquiryPremiumItemAckPacketInfo {
    char isNewGoods;
    std::array<std::byte, 7> m_alignment01To08;
    std::int64_t warehouseId;
    char isGift;
    std::byte m_alignment11To12;
    char16_t senderName[51];
    std::int64_t itemClassId;
    std::int64_t amount;
};


struct InquiryGoodsAckPacketInfo {
    char isNewGoods;
    std::array<std::byte, 7> m_alignment001To008;
    std::int64_t goodsUid;
    int goodsIconId;
    char16_t goodsName[41];
    char16_t goodsDesc[151];
    std::array<std::byte, 4> m_alignment194To198;
    std::int64_t refundExpirationDate;
    char refundable;
    char isGift;
    char16_t senderName[51];
    char16_t sendMessage[81];
    std::int16_t includedItemCount;
    std::array<std::byte, 4> m_alignment2acTo2b0;
    std::list<InquiryItemInGoods> includedItems;
};


class CUserNewGoods {
public:
    CUserNewGoods();

    void Init(std::int64_t goodsUid, int goodsPrimaryId);
    void AddIncludededItem(int itemPrimaryId, int itemClassId, int itemAmount, FileTime deliveryDate);

    std::int64_t m_nGoodsUid;
    int m_nGoodsPrimaryId;
    int m_nIncludedItemCount;
    std::map<int, UserItemIncludedInNewGoods> m_mapIncludedItem;
};


using CGoodsInventoryCallback = bool (*)(CGoodsInventorySocket*, const std::uint8_t*);
using CGoodsInventoryCallbackMap = FixedSizeCallbackMap<46, CGoodsInventoryCallback>;

extern CGoodsInventoryCallbackMap g_GoodsInventoryPacketTable;


class CGoodsInventorySocket : public CIOSocket {
public:
    explicit CGoodsInventorySocket(std::uint64_t socket);
    ~CGoodsInventorySocket() override;

    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    bool CheckPendingWrite() override;
    void Send(const char* format, ...) override;
    void SendV(const char* format, va_list arguments) override;
    void OnClose() override;
    void OnRead() override;

protected:
    std::uint64_t m_socket;
    std::int32_t m_threadCount;
    std::array<std::byte, 4> m_alignmentBcToC0;
    CGoodsInventoryCallback* m_packetTable;
    SocketMode m_mode;
    std::int32_t m_packetLength;

private:
    L2_DECLARE_OBJECT_REGISTRATION(CGoodsInventorySocket)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CGoodsInventorySocket)
};

using CGoodsInventoryPacketBase = CPacketEx<DefaultIOBuffer, CGoodsInventorySocket, CGoodsInventoryCallback>;

class CGoodsInventoryPacket final : public CGoodsInventoryPacketBase {
public:
    CGoodsInventoryPacket() = default;
    ~CGoodsInventoryPacket() override = default;
};


class CGoodsInventoryJob : public MemoryObject {
public:
    CGoodsInventoryJob(int userId, std::int16_t command);
    CGoodsInventoryJob(int userId, std::int16_t command, std::int64_t goodsUid);
    CGoodsInventoryJob(int userId, std::int16_t command, bool isUserAction);
    ~CGoodsInventoryJob() override;

    static void* operator new(std::size_t requestedSize);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    int m_nTransactionId;
    int m_nUserId;
    std::int16_t m_nCommand;
    bool m_bIsUserAction;
    std::array<std::byte, 5> m_alignment23To28;
    std::int64_t m_nGoodsUid;

private:
    L2_DECLARE_OBJECT_REGISTRATION(CGoodsInventoryJob)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CGoodsInventoryJob)
};


using CGoodsInventoryJobSP = CSPointer<CGoodsInventoryJob>;
using CGoodsInventoryJobMap = CObjectMap<CGoodsInventoryJob, 1, 5, RWLock, int>;

class CNewGoods : public MemoryObject {
public:
    CNewGoods(int goodsPrimaryId, wchar_t* goodsName, wchar_t* goodsDesc, int goodsIconId, char* restrictionCondition);
    ~CNewGoods() override;

    static void* operator new(std::size_t requestedSize);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    void SetGoodsIconId(int goodsIconId);
    void SetGoodsName(wchar_t* goodsName);
    void SetGoodsDesc(wchar_t* goodsDesc);
    void SetGoodsRestrictionCondition(char* restrictionCondition);
    void AddIncludededItem(int itemPrimaryId, int itemClassId);
    void ChangeIncludedItem(int itemPrimaryId, int itemClassId);
    int GetIncludedItemClassId(int itemPrimaryId);

private:
    friend class CGoodsInventory;
    friend void ContainValidGoods(
        int transactionId,
        User* user,
        std::int16_t& goodsCount,
        std::list<InquiryGoodsAckPacketInfo>& goods,
        const std::uint8_t* packet
    );

    [[maybe_unused]] int m_nGoodsPrimaryId;
    char16_t m_sGoodsName[41];
    char16_t m_sGoodsDesc[151];
    int m_nIncludedItemCount;
    int m_nGoodsIconId;
    char m_GoodsRestrictionCondition[32];
    [[maybe_unused]] std::array<std::byte, 4> m_alignment1c4To1c8;
    std::map<int, ItemIncludedInNewGoods> m_mapIncludedItem;

    L2_DECLARE_OBJECT_REGISTRATION(CNewGoods)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CNewGoods)
};


class CGoodsInventory {
public:
    CGoodsInventory();
    ~CGoodsInventory();

    void ConnectToGoodsInventoryAgent();
    void RequestCheckVersion();
    void RequestAllGoodsInfo();
    void SendInquiryResultErrorNoti(int transactionId, int result, std::int64_t goodsUid);
    int CreateTransactionId();
    bool IsDeliveryDateExpired(FileTime deliveryDate);
    std::int64_t GetRemainTimeUntilExpired(FileTime deliveryDate);
    bool Connect();
    bool Init();
    void CreateThread();

    void AllGoodsInfoClear();
    CNewGoods* FindGoodsInfo(int goodsPrimaryId);
    ItemIncludedInNewGoods* FindIncludedItemInGoodsInfo(int goodsPrimaryId, int itemPrimaryId);
    void AddNewGoodsInfo(
        int goodsPrimaryId,
        int itemPrimaryId,
        int itemClassId,
        wchar_t* goodsName,
        wchar_t* goodsDesc,
        int goodsIconId,
        char* restrictionCondition
    );
    bool ChangeGoodsInfo(
        int goodsPrimaryId,
        int itemPrimaryId,
        int itemClassId,
        wchar_t* goodsName,
        wchar_t* goodsDesc,
        int goodsIconId,
        char* restrictionCondition
    );

    void RequestOneGoodsInfo(int userId, int accountId, int goodsPrimaryId);
    GoodsInventoryJobResult RequestInquiryGoodsInventory(
        int userId, int accountId, int characterDatabaseId, bool isUserAction
    );
    GoodsInventoryJobResult RequestRetrieveGoodsInventory(
        int userId, int accountId, int characterDatabaseId, std::int64_t goodsUid
    );
    GoodsInventoryJobResult RequestInquiryExistGoods(int userId, int accountId, int characterDatabaseId);

    bool PushGoodsInventoryJob(CGoodsInventoryJob* job);
    CGoodsInventoryJobSP PopGoodsInventoryJob(int transactionId);

private:
    friend class CGoodsInventorySocket;
    friend class User;
    friend bool IBVersionPacket(CGoodsInventorySocket*, const std::uint8_t*);

    CNewGoods* _FindGoodsInfo(int goodsPrimaryId);

    std::uint32_t m_nConnectThread;
    std::array<std::byte, 4> m_alignment04To08;
    void* m_hConnectThread;
    bool m_bConnected;
    bool m_bSendable;
    std::array<std::byte, 6> m_alignment12To18;
    CGoodsInventorySocket* m_pGoodsInventorySocket;
    CYieldLock m_lock;
    std::uint32_t m_nLogTick;
    std::array<std::byte, 4> m_alignment34To38;
    std::map<int, CNewGoods*> m_mapNewGoodsInfo;
    CGoodsInventoryJobMap m_GoodsInventoryJobs;
    int m_nTransactionId;
    std::array<std::byte, 4> m_tailPaddingBcToC0;
};


extern CGoodsInventory g_GoodsInventory;

bool DummyGoodsInventoryPacket(CGoodsInventorySocket* socket, const std::uint8_t* packet);
bool IBVersionPacket(CGoodsInventorySocket* socket, const std::uint8_t* packet);
bool IBInquiryAllGoodsInfoAckPacket(CGoodsInventorySocket* socket, const std::uint8_t* packet);
bool IBInquiryOneGoodsInfoAckPacket(CGoodsInventorySocket* socket, const std::uint8_t* packet);
bool IBGoodsInventoryChangedNoti(CGoodsInventorySocket* socket, const std::uint8_t* packet);
bool IBInquiryGoodsInventoryAckPacket(CGoodsInventorySocket* socket, const std::uint8_t* packet);
bool IBRetrieveGoodsFromInventory(CGoodsInventorySocket* socket, const std::uint8_t* packet);
bool IBInquiryExistGoodsAck(CGoodsInventorySocket* socket, const std::uint8_t* packet);
bool IBGoodsInfoChangedNoti(CGoodsInventorySocket* socket, const std::uint8_t* packet);

void _ContainValidPremiumItem(User* user, int itemCount, std::list<InquiryPremiumItemAckPacketInfo>& premiumItems);
void SendInquiryGoodsInventoryErrMsgToClient(User* user, int result, bool isUserAction);
void SendRetrieveGoodsErrMsgToClient(User* user, int result);
void ContainValidGoods(
    int transactionId,
    User* user,
    std::int16_t& goodsCount,
    std::list<InquiryGoodsAckPacketInfo>& goods,
    const std::uint8_t* packet
);
