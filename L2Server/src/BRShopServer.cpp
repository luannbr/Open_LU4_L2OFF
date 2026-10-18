#include "BRShopServer.h"
#include "Shared/src/MemorySub.h"

#include "CConfig.h"
#include "DB.h"
#include "Item.h"
#include "ObjectDB.h"
#include "ParserProduct.h"

#include "Shared/src/Log.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/Socket.h"
#include "Shared/src/Utils.h"

#include <array>
#include <cstdint>
#include <optional>

namespace {


struct ProductItemInfo {
    std::int32_t itemId;
    std::int32_t amount;
    std::int32_t weight;
    std::int32_t tradable;
};


} // namespace

// Portable registration: the runtime ID is no longer a shipping number.
// static_assert(
//     CShopServer::kMemoryClassId ==
//     L2ServerMemory::kCShopServerClassId)

CShopServer g_ShopServer;

CShopServer::CShopServer() : m_products(nullptr), m_loaded(false), m_newProduct(false) {}

CShopServer::~CShopServer() {
    delete m_products;
    m_products = nullptr;
}

L2_DEFINE_OBJECT_RELEASE(CShopServer)

void CShopServer::WriteLock(const wchar_t* file, int line) {
    L2_GUARD_FUNCTION();
    m_lock.WriteLock(file, line);
}

void CShopServer::WriteUnlock() {
    L2_GUARD_FUNCTION();
    m_lock.WriteUnlock();
}

void CShopServer::ReadLock(const wchar_t* file, int line) {
    L2_GUARD_FUNCTION();
    m_lock.ReadLock(file, line);
}

void CShopServer::ReadUnlock() {
    L2_GUARD_FUNCTION();
    m_lock.ReadUnlock();
}

bool CShopServer::Init() {
    L2_GUARD_FUNCTION();

    CPacketHelperEx<20480> packet;
    int productCount = 0;

    FOR_ALL_OBJECT_IN_OBJECT_MAP(*m_products, product, {
        if (product == nullptr) {
            continue;
        }
        if (product->m_stock > 0) {
            packet.Assemble("d", product->m_productId);
            ++productCount;
        }
        if (!m_newProduct && product->m_itemPanelType == 3 && product->m_buyable) {
            m_newProduct = true;
        }
    });

    if (db.m_pDBSockets[::LegacyCrtRand() & 1] != nullptr && productCount > 0) {
        db.RequestProductSoldCount(productCount, packet);
    }
    return true;
}

void CShopServer::SendProductList(CSocket* socket, std::int64_t value1, std::int64_t value2) {
    L2_GUARD_FUNCTION();

    CPacketHelperEx<20480> packet;
    if (g_PremiumSetting.cashshopPause) {
        socket->Send("chQQcdb", 0xfe, 0xd7, std::int64_t{0}, std::int64_t{0}, 0, -1, 0, packet.GetPacket());
        return;
    }

    FOR_ALL_OBJECT_IN_OBJECT_MAP(*m_products, product, {
        if (product == nullptr || product->CheckBuyableCondition(-1) != BR_BUY_SUCCESS) {
            continue;
        }

        // Current 0x004cd8f0 neither initializes this stack array nor
        // clears it between products. Missing item/object lanes keep
        // indeterminate or preceding-product bytes in the packet.
        std::array<ProductItemInfo, 8> itemInfo;
        const int itemCount = product->GetItemCount();
        for (int index = 0; index < itemCount; ++index) {
            CProduct::ItemData* const productItem = product->GetItemData(index);
            if (productItem == nullptr) {
                continue;
            }

            CObject* const object = g_ObjectDB.GetObjectW(productItem->typeId, false);
            if (object == nullptr || !object->IsItem()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] GetObject Fail",
                //     "br_ShopServer.cpp",
                //     0x154)
                // L2_LOG_ERROR(Log,
                //     L"[%S][%d] GetObject Fail",
                //     "br_ShopServer.cpp",
                //     0x154)
                L2_LOG_ERROR(
                    Log,
                    "Product %d item slot %d references missing or non-item prototype %d; the slot is not filled and "
                    "product-list assembly continues.",
                    product->m_productId, index, productItem->typeId
                );
                continue;
            }

            CItem* const item = object->CastItem();
            ProductItemInfo& info = itemInfo[static_cast<std::size_t>(index)];
            info.itemId = productItem->typeId;
            info.amount = productItem->amount;
            info.weight = item->m_const->m_enumDefaultAction == ITEM_ACTION_SOULSHOT ? 0 : item->m_const->m_nWeight;
            info.tradable = item->IsTrade() ? 1 : 0;
        }

        const int assembled = packet.Assemble(
            "dccdcdddcccccddcccddddcb", product->m_productId,
            static_cast<int>(static_cast<std::int8_t>(product->m_category)),
            static_cast<int>(static_cast<std::int8_t>(product->m_paymentType)), product->m_price,
            static_cast<int>(static_cast<std::int8_t>(product->m_itemPanelType)), product->m_tab,
            product->GetSaleStartTime(), product->GetSaleEndTime(), static_cast<int>(product->m_dayOfWeek),
            static_cast<int>(product->m_startTime.hour), static_cast<int>(product->m_startTime.minute),
            static_cast<int>(product->m_endTime.hour), static_cast<int>(product->m_endTime.minute), product->m_soldCount,
            product->m_stock, static_cast<int>(static_cast<std::int8_t>(product->m_salePercent)),
            static_cast<int>(static_cast<std::int8_t>(product->m_minLevel)),
            static_cast<int>(static_cast<std::int8_t>(product->m_maxLevel)), product->GetMinBirthday(),
            product->GetMaxBirthday(), product->m_limitPeriod, product->m_limitCount,
            static_cast<int>(static_cast<std::int8_t>(itemCount)), itemCount * static_cast<int>(sizeof(ProductItemInfo)),
            itemInfo.data()
        );
        if (assembled < 1) {
            // Log.Add(
            //         LOG_ERROR,
            //         L"[%S][%d]SendProductList. socket buffer overflow. "
            //         L"Len[%d] Count[%d]",
            //         "br_ShopServer.cpp",
            //         0x154,
            //         packet.GetLength(),
            //     packet.GetAssembledCount())
            // L2_LOG_ERROR(Log,
            //         L"[%S][%d]SendProductList. socket buffer overflow. "
            //         L"Len[%d] Count[%d]",
            //         "br_ShopServer.cpp",
            //         0x154,
            //         packet.GetLength(),
            //     packet.GetAssembledCount())
            L2_LOG_ERROR(
                Log,
                "Product-list assembly failed at product %d (buffer %d bytes, %d assembled records); sending the "
                "accumulated list.",
                product->m_productId, packet.GetLength(), packet.GetAssembledCount()
            );
            break;
        }
        if (packet.GetLength() > 20000) {
            // Log.Add(
            //         LOG_ERROR,
            //         L"[%S][%d]SendProductList. socket over 20000. "
            //         L"Len[%d] Count[%d] LastID[%d]",
            //         "br_ShopServer.cpp",
            //         0x154,
            //         packet.GetLength(),
            //     packet.GetAssembledCount(),
            //     product->m_productId)
            // L2_LOG_ERROR(Log,
            //         L"[%S][%d]SendProductList. socket over 20000. "
            //         L"Len[%d] Count[%d] LastID[%d]",
            //         "br_ShopServer.cpp",
            //         0x154,
            //         packet.GetLength(),
            //     packet.GetAssembledCount(),
            //     product->m_productId)
            L2_LOG_WARN(
                Log,
                "Product list exceeded 20000 bytes after product %d (buffer %d bytes, %d assembled records); stopping "
                "accumulation and sending the list.",
                product->m_productId, packet.GetLength(), packet.GetAssembledCount()
            );
            break;
        }
    });

    socket->Send(
        "chQQcdb", 0xfe, 0xd7, value1, value2, 0, packet.GetAssembledCount(), packet.GetLength(), packet.GetPacket()
    );
}

CProductSP CShopServer::GetProduct(int productId) {
    L2_GUARD_FUNCTION();
    if (m_products == nullptr) {
        return {};
    }
    return m_products->FindObject(productId);
}

bool CShopServer::PushProduct(CProduct* product) {
    L2_GUARD_FUNCTION();
    if (m_products == nullptr || product == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Push null product",
        //     L"br_ShopServer.cpp",
        //     0xca)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Push null product",
        //     L"br_ShopServer.cpp",
        //     0xca)
        L2_LOG_ERROR(Log, "Cannot register shop product: the product map or product pointer is null; nothing is inserted.");
        return false;
    }
    m_products->Insert(product->m_productId, product);
    return true;
}

bool CShopServer::Load() {
    L2_GUARD_FUNCTION();

    m_loaded = false;
    {
        L2_WRITE_GUARD(*this);
        delete m_products;
        m_products = new ProductMap;
    }

    std::optional<CSecureStream> inputStorage;
    std::optional<CParserForProduct> parserStorage;
    {
        L2_READ_GUARD(*this);
#if defined(_WIN32)
        constexpr const wchar_t* kProductData = L"..\\Script\\ProductData.txt";
#else
        // Linux file names are case sensitive. The data file is productdata.txt.
        constexpr const wchar_t* kProductData = L"../script/productdata.txt";
#endif
        auto& input = inputStorage.emplace(kProductData, true);
        if (input.fail()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] Read from ProductData.txt file failed",
            //     L"br_ShopServer.cpp",
            //     0x9d)
            // L2_LOG_ERROR(Log,
            //     L"[%S][%d] Read from ProductData.txt file failed",
            //     L"br_ShopServer.cpp",
            //     0x9d)
            L2_LOG_ERROR(
                Log,
                "Cannot read ProductData.txt; the previous product map was already replaced and the shop remains unloaded."
            );
            return false;
        }

        auto& parser = parserStorage.emplace();
        if (!parser.create()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] ProductParser.create failed",
            //     L"br_ShopServer.cpp",
            //     0xa6)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] ProductParser.create failed",
            //     L"br_ShopServer.cpp",
            //     0xa6)
            L2_LOG_ERROR(
                Log,
                "Cannot create the ProductData parser; the previous product map was already replaced and the shop remains "
                "unloaded."
            );
            return false;
        }

        parser.m_lexer.yybegin(4);
        parser.m_lexer.yyin = input.GetStream();
        const int parseResult = parser.yyparse();
        if (parseResult != 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Parsing ProductData file failed after line[%d]",
            //     L"br_ShopServer.cpp",
            //     0xb4,
            //     parser.m_lexer.line)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Parsing ProductData file failed after line[%d]",
            //     L"br_ShopServer.cpp",
            //     0xb4,
            //     parser.m_lexer.line)
            L2_LOG_ERROR(
                Log,
                "ProductData parsing failed with result %d at lexer line %d; the shop remains unloaded and any products "
                "already parsed remain in the replacement map.",
                parseResult, parser.m_lexer.line
            );
            return false;
        }
    }
    m_loaded = true;
    return true;
}
