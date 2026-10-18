#pragma once

#include "Protocol/src/L2Constants.h"

#include "Shared/src/IOC.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <array>
#include <cstddef>
#include <cstdint>

class User;

struct CBeautyData {
    std::int32_t m_nUpperID{}; // +0x00
    std::int32_t m_nID{}; // +0x04
    std::int64_t m_nAdena{}; // +0x08
    std::int64_t m_nResetAdena{}; // +0x10
    std::int64_t m_nCoin{}; // +0x18
    std::int32_t m_nCount{}; // +0x20
    std::int32_t m_nEnable{}; // +0x24
};


struct BeautyFace final : MemoryObject {
    BeautyFace() = default;
    explicit BeautyFace(const CBeautyData& data);
    ~BeautyFace() override = default;

    L2_DECLARE_OBJECT_TYPE_FULL_OUT_OF_LINE(BeautyFace)

    CBeautyData m_Data; // +0x18
};

struct BeautyHair final : MemoryObject {
    BeautyHair() = default;
    explicit BeautyHair(const CBeautyData& data);
    ~BeautyHair() override = default;

    L2_DECLARE_OBJECT_TYPE_FULL_OUT_OF_LINE(BeautyHair)

    CBeautyData m_Data; // +0x18
};

struct BeautyColor final : MemoryObject {
    BeautyColor() = default;
    explicit BeautyColor(const CBeautyData& data);
    ~BeautyColor() override = default;

    L2_DECLARE_OBJECT_TYPE_FULL_OUT_OF_LINE(BeautyColor)

    CBeautyData m_Data; // +0x18
};


class CBeautyManager final : public CIOObject {
public:
    using BeautyKey = std::CDualKey<std::int32_t, std::int32_t>;
    using FaceMap = CObjectMap<BeautyFace, 8, 5, CIOCriticalSection, BeautyKey>;
    using HairMap = CObjectMap<BeautyHair, 8, 5, CIOCriticalSection, BeautyKey>;
    using ColorMap = CObjectMap<BeautyColor, 8, 5, CIOCriticalSection, BeautyKey>;

    CBeautyManager();
    ~CBeautyManager() override;

    static CBeautyManager* Inst();
    L2_DECLARE_OBJECT_TYPE_FULL_OUT_OF_LINE(CBeautyManager)

    void Init();
    void LoadData(const unsigned char* packet);
    void IncrementBeautyItem(Beauty::BeautyState state, int secondary, int primary);
    void GetData(const CBeautyData& data, bool includePrice);
    void GetDataList(int userSid, Beauty::BeautyState state);
    void GetItemData(Beauty::BeautyState state);
    void SendAllDataList(User* user);
    void RequestBeautyPurchaseList(User& user, char requestFlag);
    bool ResetBeautyItem(
        Beauty::BeautyState state,
        int primary,
        int secondary,
        int userDatabaseId,
        CPacketHelperEx<20480>& packet,
        std::int64_t& adenaPrice,
        std::int64_t& beautyCoinPrice,
        int builder
    );
    bool BuyBeautyItem(
        Beauty::BeautyState state,
        int primary,
        int secondary,
        int userDatabaseId,
        CPacketHelperEx<20480>& packet,
        std::int64_t& adenaPrice,
        std::int64_t& beautyCoinPrice,
        int builder
    );
    bool SetBeautyItemLimit(Beauty::BeautyState state, int id, int upperId, int limit);

private:
    FaceMap m_vFaceList; // +0x0018
    HairMap m_vHairList; // +0x0260
    ColorMap m_vColorList; // +0x04a8
    volatile std::int32_t m_bInit; // +0x06f0
    [[maybe_unused]] std::array<std::byte, 0x04> m_alignment06f4To06f8{}; // +0x06f4
    CPacketHelperEx<20480> m_packetHelper; // +0x06f8
    std::array<RWLock, 3> m_lock; // +0x5708
};
