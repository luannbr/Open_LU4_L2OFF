#pragma once

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

class CRecipeInfo;
class User;

template <std::size_t Capacity>
class CPacketHelperEx;

struct RecipeOfferingItemStruct;

struct RecipeItemStruct {
    RecipeItemStruct() : ItemClassId(0), Count(0) {}

    RecipeItemStruct(int itemClassId, std::int64_t count) : ItemClassId(itemClassId), Count(count) {}

    int ItemClassId;
    std::int64_t Count;
};

struct RecipeProductItemStruct : RecipeItemStruct {
    RecipeProductItemStruct() : Prob(0) {}

    RecipeProductItemStruct(int itemClassId, std::int64_t count, int probability) :
        RecipeItemStruct(itemClassId, count), Prob(probability) {}

    int Prob;
};

class CRecipeInfo {
public:
    CRecipeInfo();
    ~CRecipeInfo() = default;

    const RecipeItemStruct* GetMeterialItem(int itemClassId) const;
    bool MakeProduct(User* user, std::vector<RecipeOfferingItemStruct*> offeringItems, bool npcFee) const;
    bool RecipeShopMake(
        User* maker, User* customer, std::int64_t makingFee, std::vector<RecipeOfferingItemStruct*> offeringItems
    );
    void AnnounceOfferingRecipeMakeItem(User* user);

    int m_nId; // +0x00
    int m_nLevel; // +0x04
    int m_nMpConsume; // +0x08
    int m_nSuccessRate; // +0x0c
    int m_nItemId; // +0x10
    int m_nCommonRecipe; // +0x14
    bool m_bCreateCritical; // +0x18
    bool m_bUseOffering; // +0x19
    std::int64_t m_nOfferingDP; // +0x20
    bool m_bAnnounce; // +0x28
    std::vector<RecipeItemStruct*> m_vMaterial; // +0x30
    std::vector<RecipeItemStruct*> m_vCatalyst; // +0x50
    std::vector<RecipeProductItemStruct*> m_vProduct; // +0x70
    std::vector<RecipeItemStruct*> m_vNPCFee; // +0x90

private:
    bool IsEnoughItem(User* user, int itemClassId, int count, int& remaining) const;
    RecipeProductItemStruct* GetProduct(bool critical) const;
    bool MakeProductPacket(CPacketHelperEx<20480>& packet, int multiplier) const;
    bool CheckMaterial(User* user) const;
    bool CheckCatalyst(User* user) const;
    bool CheckNPCFee(User* user) const;
    bool CheckProduct(User* user) const;
    bool CheckCondition(User* user, bool npcFee) const;
    bool CheckCondition(User* customer, User* maker) const;
    bool MakeMaterialPacket(CPacketHelperEx<20480>& packet, User* user) const;
    bool MakeGiveBackPacket(CPacketHelperEx<20480>& packet, User* user) const;
    bool MakeNPCFeePacket(CPacketHelperEx<20480>& packet, User* user) const;
    int CalOfferingSuccessRate(User* user, std::vector<RecipeOfferingItemStruct*> offeringItems) const;
    const RecipeOfferingItemStruct* GetOfferingItem(
        User* user, std::vector<RecipeOfferingItemStruct*> offeringItems, int itemClassId
    ) const;
    bool CheckOffering(User* user, std::vector<RecipeOfferingItemStruct*> offeringItems) const;
    bool MakeOfferingPacket(
        CPacketHelperEx<20480>& packet, User* user, std::vector<RecipeOfferingItemStruct*> offeringItems
    ) const;
};

struct CRecipeInfoData {
    CRecipeInfoData() : m_pRecipeInfo(nullptr), m_nMakingFee(0) {}

    CRecipeInfoData(CRecipeInfo* recipeInfo, std::int64_t makingFee) : m_pRecipeInfo(recipeInfo), m_nMakingFee(makingFee) {}

    CRecipeInfo* m_pRecipeInfo; // +0x00
    std::int64_t m_nMakingFee; // +0x08
};


class CRecipeDB {
public:
    CRecipeDB() = default;
    ~CRecipeDB();

    void Add(CRecipeInfo* recipeInfo);
    bool Remove(int recipeId) { return m_mapRecipeInfo.erase(recipeId) != 0; }
    int GetRecipeCount() const { return static_cast<int>(m_mapRecipeInfo.size()); }
    void Load();
    CRecipeInfo* find(int recipeId);

private:
    using RecipeBookMap = std::map<int, CRecipeInfo*>;

    RecipeBookMap m_mapRecipeInfo;
};

extern CRecipeDB g_RecipeDB;

struct RecipeOfferingItemStruct {
    RecipeOfferingItemStruct(int itemServerId, std::int64_t count) : ItemServerId(itemServerId), Count(count) {}

    int ItemServerId;
    std::int64_t Count;
};

static_assert(std::is_trivially_copyable_v<RecipeOfferingItemStruct>);
static_assert(std::is_trivially_destructible_v<RecipeOfferingItemStruct>);
