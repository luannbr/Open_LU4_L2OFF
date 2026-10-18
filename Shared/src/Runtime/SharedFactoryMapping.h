#pragma once

#if defined(L2_BUILD_L2SERVER)

#include "Shared/src/SharedFactory.h"

#include <cstddef>
#include <string>

// Create server mappings through SharedMemoryRegion.
// Keep their lifetime outside the factory record.
class L2ServerSharedFactoryMappingPublisher final {
public:
    static bool CreateMappings(
        const CSharedDefine& define,
        std::size_t dataBytes,
        std::size_t constantBytes,
        std::size_t indexBytes,
        L2ServerSharedFactoryMapping& output,
        std::string* error = nullptr
    );

    template <typename TFactory>
    static bool InitializeDefault(TFactory& factory, std::string* error = nullptr) {
        L2ServerSharedFactoryMapping mapping;
        if (!CreateMappings(
                TFactory::Definition(), TFactory::RequiredDataBytes(), TFactory::RequiredConstantBytes(),
                TFactory::RequiredIndexBytes(), mapping, error
            )) {
            return false;
        }
        return factory.InitializeMappedViews(std::move(mapping), error);
    }
};

#else

#include "Shared/src/SharedFactory.h"

#include <cstddef>
#include <memory>
#include <string>

namespace runtime {

// Keep OS mapping resources outside the 0x60-byte factory record.
// The record stores Windows HANDLEs, which cannot hold POSIX resources.
class SharedFactoryMappingAccess {
public:
    static bool Attach(
        CSharedFactory<CAlliance, CSharedAllianceData, CSharedAllianceConstant, &SharedDefine_Alliance>& factory,
        void* dataBase,
        std::size_t dataBytes,
        void* indexBase,
        std::size_t indexBytes,
        std::shared_ptr<void> lifetime,
        std::string* error = nullptr
    );

    static void Detach(
        CSharedFactory<CAlliance, CSharedAllianceData, CSharedAllianceConstant, &SharedDefine_Alliance>& factory
    );

    static bool HasView(
        const CSharedFactory<CAlliance, CSharedAllianceData, CSharedAllianceConstant, &SharedDefine_Alliance>& factory
    );

    static bool Attach(
        CStaticObjectFactory& factory,
        void* dataBase,
        std::size_t dataBytes,
        void* indexBase,
        std::size_t indexBytes,
        std::shared_ptr<void> lifetime,
        std::string* error = nullptr
    );

    static void Detach(CStaticObjectFactory& factory);

    static bool HasView(const CStaticObjectFactory& factory);

    static bool Attach(
        CAirShipFactory& factory,
        void* dataBase,
        std::size_t dataBytes,
        void* constantBase,
        std::size_t constantBytes,
        void* indexBase,
        std::size_t indexBytes,
        std::shared_ptr<void> lifetime,
        std::string* error = nullptr
    );

    static void Detach(CAirShipFactory& factory);

    static bool HasView(const CAirShipFactory& factory);

    static bool Attach(
        CCreatureFactory& factory,
        void* dataBase,
        std::size_t dataBytes,
        void* indexBase,
        std::size_t indexBytes,
        std::shared_ptr<void> lifetime,
        std::string* error = nullptr
    );

    static void Detach(CCreatureFactory& factory);

    static bool HasView(const CCreatureFactory& factory);

    static bool Attach(
        CPartyFactory& factory,
        void* dataBase,
        std::size_t dataBytes,
        void* indexBase,
        std::size_t indexBytes,
        std::shared_ptr<void> lifetime,
        std::string* error = nullptr
    );

    static void Detach(CPartyFactory& factory);

    static bool HasView(const CPartyFactory& factory);

    static bool Attach(
        CPledgeFactory& factory,
        void* dataBase,
        std::size_t dataBytes,
        void* indexBase,
        std::size_t indexBytes,
        std::shared_ptr<void> lifetime,
        std::string* error = nullptr
    );

    static void Detach(CPledgeFactory& factory);

    static bool HasView(const CPledgeFactory& factory);

    static bool Attach(
        CGeneralObjectFactory& factory,
        void* dataBase,
        std::size_t dataBytes,
        void* indexBase,
        std::size_t indexBytes,
        std::shared_ptr<void> lifetime,
        std::string* error = nullptr
    );

    static void Detach(CGeneralObjectFactory& factory);

    static bool HasView(const CGeneralObjectFactory& factory);

    static bool Attach(
        CItemFactory& factory,
        void* dataBase,
        std::size_t dataBytes,
        void* constantBase,
        std::size_t constantBytes,
        void* indexBase,
        std::size_t indexBytes,
        std::shared_ptr<void> lifetime,
        std::string* error = nullptr
    );

    static void Detach(CItemFactory& factory);

    static bool HasView(const CItemFactory& factory);
};

// Called by the recovered factory constructor. The implementation uses the
// host mapping adapter but keeps the target's default mapping names.
bool OpenDefaultAllianceSharedFactory(
    CSharedFactory<CAlliance, CSharedAllianceData, CSharedAllianceConstant, &SharedDefine_Alliance>& factory
);

bool OpenDefaultStaticObjectSharedFactory(CStaticObjectFactory& factory);

bool OpenDefaultAirShipSharedFactory(CAirShipFactory& factory);

bool OpenDefaultCreatureSharedFactory(CCreatureFactory& factory);

bool OpenDefaultPartySharedFactory(CPartyFactory& factory);

bool OpenDefaultPledgeSharedFactory(CPledgeFactory& factory);

bool OpenDefaultGeneralObjectSharedFactory(CGeneralObjectFactory& factory);

bool OpenDefaultItemSharedFactory(CItemFactory& factory);

} // namespace runtime

#endif
