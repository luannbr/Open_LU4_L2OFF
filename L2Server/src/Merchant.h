#pragma once

#include "NPC.h"

#include "Shared/src/BaseStructure.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>


// Exact current merchant.obj owner. Current parser_npc construction fixes
// the 0x3dc0 allocation, while the current vtable differs from CNPC in only
// the six overrides declared here. The target xvectors keep their 0x20
// physical envelopes and borrow the GoodStruct records owned by parser_npc.
class CMerchant : public CNPC {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CMerchant)

    CMerchant();
    CMerchant(const CMerchant& other);
    ~CMerchant() override;

    static void* operator new(std::size_t requestedSize);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void ReportOrt(const char* callerFile, int callerLine) override;
    CObject* Copy() override;
    bool IsMerchant() override;
    CMerchant* CastMerchant() override;

    void AddSellingGood(GoodStruct* good);
    void AddBuyingGood(GoodStruct* good);


private:
    bool m_bIsSeller; // +0x3d78
    [[maybe_unused]] std::byte m_alignment3d79To3d80[0x07];
    std::vector<GoodStruct*> m_SellingGoods; // +0x3d80
    std::vector<GoodStruct*> m_BuyingGoods; // +0x3da0
};
