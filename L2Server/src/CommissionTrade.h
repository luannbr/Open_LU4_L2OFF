#pragma once

#include "Protocol/src/L2Constants.h"

#include "TradeItem.h"

#include "Shared/src/IOC.h"
#include "Shared/src/IOCs.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/TargetWideChar.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CItem;
class User;
enum CrystalType : std::int32_t;

namespace Commission {

enum Enum : std::int32_t {
    Invalid = -1,
    None = 0,
    OnehandSword = 1,
    OnehandMagicSword = 2,
    Dagger = 3,
    Rapier = 4,
    TwohandSword = 5,
    TwohandAncientSword = 6,
    DualSword = 7,
    DualDagger = 8,
    OnehandBlunt = 9,
    OnehandMagicBlunt = 10,
    TwohandBlunt = 11,
    TwohandMagicBlunt = 12,
    DualBlunt = 13,
    Bow = 14,
    CrossBow = 15,
    Fist = 16,
    Spear = 17,
    EtcWeapon = 18,
    Helmet = 19,
    Upbody = 20,
    Downbody = 21,
    Onepiece = 22,
    Gloves = 23,
    Shoes = 24,
    Shield = 25,
    Sigil = 26,
    Underwear = 27,
    Cloak = 28,
    Ring = 29,
    Earring = 30,
    Necklace = 31,
    Belt = 32,
    Bracelet = 33,
    HairAccessary = 34,
    Potion = 35,
    WeaponEnchantScroll = 36,
    ArmorEnchantScroll = 37,
    EtcScroll = 38,
    SoulShot = 39,
    SpritShot = 40,
    EtcGoods = 41,
    PetEquipItem = 42,
    PetGoods = 43,
    Crystal = 44,
    Recipe = 45,
    MainMaterial = 46,
    WeaponVariationStone = 47,
    SoulCrystal = 48,
    AttributeStone = 49,
    WeaponEnchantAuxStone = 50,
    ArmorEnchantAuxStone = 51,
    SkillBook = 52,
    Jemstone = 53,
    Pouch = 54,
    Pin = 55,
    MagicRuneClip = 56,
    MagicDeco = 57,
    Dye = 58,
    EtcItem = 59,
    ShowNPC = 60,
    Talisman = 61,
    Agathion = 62,
    Jewel = 63,
    Artifact = 64,
    TypeMAX = 65,
};

} // namespace Commission

namespace CommissionExpired {

enum Enum : std::int32_t {
    EpInvalid = -1,
    EpOne = 0,
    EpThree = 1,
    EpFive = 2,
    EpSeven = 3,
    EpFifteen = 4,
    EpThirty = 5,
    EpMAX = 6,
};

} // namespace CommissionExpired

namespace CommissionDiscountInfo {

enum Enum : std::int32_t {
    CDI_INVALID = -1,
    CDI_DEFAULT = 0,
    CDI_30PERCENT_DISCOUNT = 1,
    CDI_NO_COMMISSION = 2,
    CDI_MAX = 3,
};

} // namespace CommissionDiscountInfo

namespace CommissionOwner {

enum Enum : std::int32_t {
    CoInvalid = -1,
    CoTimer = 0,
    CoUser = 1,
    CoMAX = 2,
};

} // namespace CommissionOwner

namespace CommissionFirst {

enum Enum : std::int32_t {
    Weapon = 0,
    Armor = 1,
    Accessary = 2,
    Goods = 3,
    Pet = 4,
    Etc = 5,
    FirstMAX = 6,
};

} // namespace CommissionFirst

namespace CommissionStatus {

// enum Enum : std::int32_t {
//     CsInvalidValue = -4,
//     CsSearchDelay = -3,
//     CsRegisteredFail = -2,
//     CsSearchFail = -1,
//     CsFail = 0,
//     CsOk = 1,
//     CsRegisteredOk = 2,
//     CsSearchOk = 3,
//     CsStatusMAX = 4,
// }

} // namespace CommissionStatus

namespace CommissionItemClassId {

// enum Enum : std::int32_t {
//     CICI_NONE = 0,
//     CICI_SEVENDAY_30PERCENT_DISCOUNT = 22351,
//     CICI_SEVENDAY_NO_COMMISSION = 22352,
//     CICI_FIFTEENDAY = 22353,
//     CICI_THIRTYDAY = 22354,
// }

} // namespace CommissionItemClassId


#define L2_DECLARE_COMMISSION_RECORD(ClassToken)                                                                            \
    class ClassToken final : public MemoryObject {                                                                          \
    public:                                                                                                                 \
        ClassToken(                                                                                                         \
            std::int64_t commissionId,                                                                                      \
            Commission::Enum type,                                                                                          \
            CommissionExpired::Enum expiredType,                                                                            \
            std::int32_t userDatabaseId,                                                                                    \
            std::int64_t itemPrice,                                                                                         \
            std::int32_t expiredDate,                                                                                       \
            CCommissionItem item,                                                                                           \
            const char16_t* userName                                                                                        \
        );                                                                                                                  \
        ~ClassToken() override;                                                                                             \
                                                                                                                            \
        L2_DECLARE_OBJECT_TYPE(ClassToken)                                                                                  \
                                                                                                                            \
        std::int64_t m_nCommissionID; /* +0x18 */                                                                           \
        Commission::Enum m_eType; /* +0x20 */                                                                               \
        CommissionExpired::Enum m_epType; /* +0x24 */                                                                       \
        std::int32_t m_nUserDBID; /* +0x28 */                                                                               \
        std::array<std::byte, 0x04> m_padding2c; /* +0x2c */                                                                \
        std::int64_t m_ItemPrice; /* +0x30 */                                                                               \
        std::int32_t m_tmExpiredDate; /* +0x38 */                                                                           \
        std::array<std::byte, 0x04> m_padding3c; /* +0x3c */                                                                \
        CCommissionItem* m_pCommissionItem; /* +0x40 */                                                                     \
        char16_t m_sUserName[25]; /* +0x48 */                                                                               \
        std::array<std::byte, 0x06> m_targetTailPadding; /* +0x7a */                                                        \
    };

L2_DECLARE_COMMISSION_RECORD(CommissionTradeWeapon)
L2_DECLARE_COMMISSION_RECORD(CommissionTradeArmor)
L2_DECLARE_COMMISSION_RECORD(CommissionTradeAccessary)
L2_DECLARE_COMMISSION_RECORD(CommissionTradeGoods)
L2_DECLARE_COMMISSION_RECORD(CommissionTradePet)
L2_DECLARE_COMMISSION_RECORD(CommissionTradeEtc)

#undef L2_DECLARE_COMMISSION_RECORD


class ExpiredCommissionData final : public MemoryObject {
public:
    ExpiredCommissionData(
        std::int64_t commissionId,
        std::int32_t userDatabaseId,
        std::int32_t itemId,
        std::int32_t expiredDate,
        Commission::Enum commissionType
    );
    ~ExpiredCommissionData() override;

    L2_DECLARE_OBJECT_TYPE(ExpiredCommissionData)

    std::int64_t m_nCommissionID; // +0x18
    std::int32_t m_nUserDBID; // +0x20
    std::int32_t m_nItemID; // +0x24
    std::int32_t m_tmExpiredDate; // +0x28
    Commission::Enum m_eType; // +0x2c
};


class CExpiredCommission final : public CObjectMapFunc {
public:
    bool Do(MemoryObject* object) override;
};


class CCommissionTradeManager final : public CIOObject {
public:
    using WeaponMap = CObjectMap<CommissionTradeWeapon, 32, 5, CIOCriticalSection, std::int64_t>;
    using ArmorMap = CObjectMap<CommissionTradeArmor, 16, 5, CIOCriticalSection, std::int64_t>;
    using AccessaryMap = CObjectMap<CommissionTradeAccessary, 8, 5, CIOCriticalSection, std::int64_t>;
    using GoodsMap = CObjectMap<CommissionTradeGoods, 8, 5, CIOCriticalSection, std::int64_t>;
    using PetMap = CObjectMap<CommissionTradePet, 2, 5, CIOCriticalSection, std::int64_t>;
    using EtcMap = CObjectMap<CommissionTradeEtc, 16, 5, CIOCriticalSection, std::int64_t>;
    using ExpiredMap = CObjectMap<ExpiredCommissionData, 1024, 5, CIOCriticalSection, std::int64_t>;

    CCommissionTradeManager();
    ~CCommissionTradeManager() override;

    L2_DECLARE_OBJECT_TYPE(CCommissionTradeManager)

    static CCommissionTradeManager& Inst();

    void Init();
    void SetCommissionStart();
    bool CheckCommissionCommon(User* user);
    bool CheckCommissionItem(CItem* item);
    bool CheckCommissionRegister(User* user, CItem* item);
    void RequestCommissionTradeSearch(
        int requestId,
        int depth,
        Commission::Enum commissionType,
        int itemGeneral,
        CrystalType itemGrade,
        SharedUtilsWideChar* itemName
    );
    bool CheckCommissionJob(Commission::Enum type);
    int SetCommissionJob(Commission::Enum type);
    void DoneCommissionJob(Commission::Enum type);
    bool CheckCommissionType(Commission::Enum type);
    void CheckExpiredCommissionItem();
    void TimerExpired(int id) override;
    bool CheckCommissionInven(User* user, std::int64_t commissionId);
    bool CheckCommissionDelete(User* user, std::int32_t itemId, std::int64_t commissionId, CommissionOwner::Enum owner);
    bool CheckCommissionBuy(std::int64_t commissionId);
    void RequestCommissionTradeRegTotalCount(std::int32_t characterId, std::int32_t itemServerId);
    void RequestCommissionTradeRegister(
        CommissionOwner::Enum owner,
        std::int32_t characterId,
        std::int32_t itemId,
        SharedUtilsWideChar* itemName,
        std::int64_t itemPrice,
        std::int64_t itemAmount,
        int itemConsumeType,
        CommissionExpired::Enum expiredType,
        Commission::Enum commissionType,
        CommissionDiscountInfo::Enum discountType,
        CommissionItemClassId::Enum premiumItemClassId,
        int enchanted
    );
    std::int32_t GetCommissionItemDBID(std::int64_t commissionId);
    std::int64_t GetCommissionPrice(std::int64_t commissionId);
    bool CheckMyCommissionItem(std::int64_t commissionId, std::int32_t characterId);
    bool CheckItemRestrictInCommmision(std::int64_t commissionId);
    std::int64_t GetCommissionAmount(std::int64_t commissionId);
    void RequestCommissionTradeBuyItem(
        std::int32_t characterId, std::int64_t commissionId, Commission::Enum commissionType, CommissionOwner::Enum owner
    );
    void RequestCommissionTradeRegisteredList(std::int32_t characterId);
    void RequestCommissionTradeDelete(
        CommissionOwner::Enum owner,
        std::int64_t commissionId,
        std::int32_t characterId,
        std::int32_t itemId,
        Commission::Enum commissionType
    );
    int ExpiredUserCommission(std::int64_t commissionId);
    void RequestCommissionTradeDeleteAll(int characterId);
    void RegisterExpiredCommissionData(
        std::int64_t commissionId,
        std::int32_t userDatabaseId,
        std::int32_t itemId,
        CommissionExpired::Enum expiredType,
        std::int32_t expiredDate,
        Commission::Enum commissionType
    );
    void RegisterCommissionItem(
        std::int64_t commissionId,
        std::int32_t userDatabaseId,
        const char16_t* itemName,
        std::int64_t itemPrice,
        std::int64_t itemAmount,
        std::int32_t expiredDate,
        CCommissionItem item,
        Commission::Enum commissionType,
        CommissionExpired::Enum expiredType,
        const char16_t* userName,
        CommissionFirst::Enum firstType
    );
    void DeleteCommissionItem(std::int64_t commissionId, CommissionFirst::Enum firstType);
    void DeleteExpiredCommissionData(std::int64_t commissionId, CommissionExpired::Enum expiredType);
    void RequestCommissionTradeInfo(User* user, CItem* item);
    void RequestCommissionTradeBuyInfo(std::int32_t characterId, std::int64_t commissionId, Commission::Enum commissionType);
    void RequestCommissionTradeItemList(User* user);
    void CommissionList(CommissionStatus::Enum status, int characterId, int count, const unsigned char*& packet);

    volatile std::int32_t m_nCommissionLoadCount; // +0x18
    std::uint8_t m_reserved1c; // +0x1c
    bool m_bLimit; // +0x1d
    std::array<std::byte, 0x02> m_padding1e; // +0x1e
    WeaponMap m_mapCommissionTradeWeapon; // +0x20
    ArmorMap m_mapCommissionTradeArmor; // +0x928
    AccessaryMap m_mapCommissionTradeAccessary; // +0xdb0
    GoodsMap m_mapCommissionTradeGoods; // +0xff8
    PetMap m_mapCommissionTradePet; // +0x1240
    EtcMap m_mapCommissionTradeEtc; // +0x12d8
    RWLock m_Lock[65]; // +0x1760
    ExpiredMap m_mapExpiredCommissionOne; // +0x2598
    ExpiredMap m_mapExpiredCommissionThree; // +0x145a0
    ExpiredMap m_mapExpiredCommissionFive; // +0x265a8
    ExpiredMap m_mapExpiredCommissionSeven; // +0x385b0
    ExpiredMap m_mapExpiredCommissionFifteen; // +0x4a5b8
    ExpiredMap m_mapExpiredCommissionThirty; // +0x5c5c0
    volatile std::int32_t m_nCommissionJob[65]; // +0x6e5c8
    std::array<std::byte, 0x04> m_targetTailPadding; // +0x6e6cc
};
