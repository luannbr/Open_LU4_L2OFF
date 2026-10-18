#pragma once

#include "Shared/src/BrBuyProductResultType.h"
#include "Shared/src/IOC.h"
#include "Shared/src/Obj.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>

class CPremiumServiceSocket;

using CPremiumCallback = bool (*)(CPremiumServiceSocket*, const std::uint8_t*);
using CPremiumCallbackMap = FixedSizeCallbackMap<46, CPremiumCallback>;

extern CPremiumCallbackMap g_PremiumPacketTable;


class CPremiumServiceSocket : public CIOSocket {
public:
    explicit CPremiumServiceSocket(std::uint64_t socket);
    ~CPremiumServiceSocket() override;

    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    bool CheckPendingWrite() override;
    void Send(const char* format, ...) override;
    void SendV(const char* format, va_list arguments) override;
    void OnClose() override;
    void OnRead() override;

protected:
    std::uint64_t m_socket; // +0xb0..+0xb7
    std::int32_t m_threadCount; // +0xb8..+0xbb
    std::array<std::byte, 0x04> m_alignmentBcToC0; // +0xbc..+0xbf
    CPremiumCallback* m_packetTable; // +0xc0..+0xc7
    SocketMode m_mode; // +0xc8..+0xcb
    std::int32_t m_packetLength; // +0xcc..+0xcf

private:
    L2_DECLARE_OBJECT_REGISTRATION(CPremiumServiceSocket)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CPremiumServiceSocket)
};

using CPremiumPacketBase = CPacketEx<DefaultIOBuffer, CPremiumServiceSocket, CPremiumCallback>;

class CPremiumPacket final : public CPremiumPacketBase {
public:
    CPremiumPacket() = default;
    ~CPremiumPacket() override = default;
};


class CBillingJob : public MemoryObject {
public:
    CBillingJob(int userId, std::int16_t command);
    ~CBillingJob() override;

    static void* operator new(std::size_t requestedSize);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    std::int32_t transactionId; // +0x018..+0x01b
    std::int32_t userId; // +0x01c..+0x01f
    std::int16_t command; // +0x020..+0x021
    std::array<std::byte, 0x06> m_alignment22To28; // +0x022..+0x027
    std::int64_t warehouseNo; // +0x028..+0x02f
    std::int32_t productId; // +0x030..+0x033
    std::int32_t productAmount; // +0x034..+0x037
    std::uint8_t extensionSize; // +0x038
    std::array<char, 0x100> extension; // +0x039..+0x138
    std::array<std::byte, 0x07> m_alignment139To140; // +0x139..+0x13f
    std::int64_t pointDiff; // +0x140..+0x147
    std::int32_t option; // +0x148..+0x14b
    bool bGift; // +0x14c
    std::byte m_alignment14dTo14e; // +0x14d
    std::array<char16_t, 25> sReceiverName; // +0x14e..+0x17f
    std::array<char16_t, 61> sTitle; // +0x180..+0x1f9
    std::array<char16_t, 1001> sContent; // +0x1fa..+0x9cb
    std::array<std::byte, 0x04> m_tailPadding9ccTo9d0; // +0x9cc..+0x9cf

private:
    L2_DECLARE_OBJECT_REGISTRATION(CBillingJob)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CBillingJob)
};


using CBillingJobSP = CSPointer<CBillingJob>;
using CBillingJobMap = CObjectMap<CBillingJob, 1, 5, RWLock, int>;

class CPremiumService {
public:
    CPremiumService();
    ~CPremiumService();

    void ConnectToPremiumAgent();
    void RequestCheckVersion();
    void RequestGetPremiumItems();
    bool Connect();
    bool Init();
    void CreateThread();
    void Load();
    std::int32_t CreateTransactionId();
    CBillingJobSP PopJob(int transactionId);
    bool PushJob(CBillingJob* billingJob);
    void RequestAddGamePoint(int userId, std::int64_t point);
    BrBuyProductResultType SendBuyItemReq(
        int userId, int productId, int productAmount, bool gift, wchar_t* receiverName, wchar_t* title, wchar_t* content
    );
    void RequestDeleteItem(std::int64_t warehouseNo);
    void RequestDeleteItem(int itemCount, const std::uint8_t* packet);
    void RequestGetGamePoint(int userId, int option);
    void RequestSubGamePoint(int userId, std::int64_t point);
    void SendRequestDeleteItem(int sequence);
    void SetDeletedInIBServer(int sequence);

private:
    friend class CPremiumServiceSocket;
    friend bool IBVersionPacket(CPremiumServiceSocket*, const std::uint8_t*);

    using DeleteItemList = std::vector<std::int64_t>;
    using DeleteRequestMap = std::map<std::int32_t, DeleteItemList>;

    std::uint32_t m_nConnectThread; // +0x00..+0x03
    [[maybe_unused]] std::array<std::byte, 0x04> m_alignment04To08; // +0x04..+0x07
    void* m_hConnectThread; // +0x08..+0x0f
    bool m_bConnected; // +0x10
    bool m_bSendable; // +0x11
    [[maybe_unused]] std::array<std::byte, 0x06> m_alignment12To18; // +0x12..+0x17
    CPremiumServiceSocket* m_pPremiumSvcSocket; // +0x18..+0x1f
    CYieldLock m_lock; // +0x20..+0x2f
    std::uint32_t m_nLogTick; // +0x30..+0x33
    std::int32_t m_nReqDeleteSequence; // +0x34..+0x37
    DeleteRequestMap m_mapReqDelete; // +0x38..+0x57
    CBillingJobMap m_Jobs; // +0x58..+0xb7
    std::int32_t m_nTransactionId; // +0xb8..+0xbb
    [[maybe_unused]] std::array<std::byte, 0x04> m_tailPaddingBcToC0; // +0xbc..+0xbf
};


extern CPremiumService g_PremiumSvc;

// Exact current CPremiumItem physical envelope and lifetime surface.
class CPremiumItem : public MemoryObject {
public:
    CPremiumItem(std::int64_t warehouseNo, int itemClassId, std::int64_t itemAmount, bool isGift, wchar_t* buyerCharName);
    ~CPremiumItem() override;

    static void* operator new(std::size_t requestedSize);

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    std::int64_t m_nWarehouseNo; // +0x18..+0x1f
    std::int32_t m_nItemClassId; // +0x20..+0x23
    std::array<std::byte, 0x04> m_alignment24To28; // +0x24..+0x27
    std::int64_t m_nItemAmount; // +0x28..+0x2f
    bool m_bIsGift; // +0x30
    std::byte m_alignment31To32; // +0x31
    std::array<char16_t, 24> m_sBuyerCharName; // +0x32..+0x61
    std::array<std::byte, 0x06> m_tailPadding62To68; // +0x62..+0x67

private:
    L2_DECLARE_OBJECT_REGISTRATION(CPremiumItem)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CPremiumItem)
};


BrBuyProductResultType GetBrBuyResultFromBilling(int billingResult);
std::int16_t GetPointCategory();
std::int16_t GetPointId();
bool CheckPointType(int category, int pointId);

bool DummyPremiumPacket(CPremiumServiceSocket* socket, const std::uint8_t* packet);
bool IBBuyGoodsForAccountAckPacket(CPremiumServiceSocket* socket, const std::uint8_t* packet);
bool IBBuyGoodsForCharacterAckPacket(CPremiumServiceSocket* socket, const std::uint8_t* packet);
bool IBGiftGoodsToAccountAckPacket(CPremiumServiceSocket* socket, const std::uint8_t* packet);
bool IBGiftGoodsToCharacterAckPacket(CPremiumServiceSocket* socket, const std::uint8_t* packet);
bool IBGetItemsForAccountAckPacket(CPremiumServiceSocket* socket, const std::uint8_t* packet);
bool IBGetItemsForCharacterAckPacket(CPremiumServiceSocket* socket, const std::uint8_t* packet);
bool IBVersionPacket(CPremiumServiceSocket* socket, const std::uint8_t* packet);
bool IBNewItemDeliveredToAccountNotiPacket(CPremiumServiceSocket* socket, const std::uint8_t* packet);
bool IBNewItemDeliveredToCharacterNotiPacket(CPremiumServiceSocket* socket, const std::uint8_t* packet);
bool IBGetItemsForServerAckPacket(CPremiumServiceSocket* socket, const std::uint8_t* packet);
bool IBDeleteItemsAckPacket(CPremiumServiceSocket* socket, const std::uint8_t* packet);
bool IBBuyItemAckPacket(CPremiumServiceSocket* socket, const std::uint8_t* packet);
bool IBGetPointAckPacket(CPremiumServiceSocket* socket, const std::uint8_t* packet);
bool IBAddPointAckPacket(CPremiumServiceSocket* socket, const std::uint8_t* packet);
bool IBSubPointAckPacket(CPremiumServiceSocket* socket, const std::uint8_t* packet);
