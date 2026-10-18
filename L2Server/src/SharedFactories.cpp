#include "SharedFactories.h"

#include "Shared/src/Runtime/SharedFactoryMapping.h"

#include "Shared/src/Log.h"

#include <mutex>

namespace l2server {
namespace {

std::mutex& CompositionMutex() {
    static auto* const mutex = new std::mutex;
    return *mutex;
}

bool AllEagerInitialized() noexcept {
    return CCreatureFactory::Inst().IsInitialized() && CItemFactory::Inst().IsInitialized() &&
        CStaticObjectFactory::Inst().IsInitialized() && CPledgeFactory::Inst().IsInitialized() &&
        CPartyFactory::Inst().IsInitialized() && CAllianceFactory::Inst().IsInitialized() &&
        CGeneralObjectFactory::Inst().IsInitialized() && CAirShipFactory::Inst().IsInitialized();
}

void ShutdownUnlocked() noexcept {
    CShuttleFactory::Inst().DetachMappedViews();
    CAirShipFactory::Inst().DetachMappedViews();
    CGeneralObjectFactory::Inst().DetachMappedViews();
    CAllianceFactory::Inst().DetachMappedViews();
    CPartyFactory::Inst().DetachMappedViews();
    CPledgeFactory::Inst().DetachMappedViews();
    CStaticObjectFactory::Inst().DetachMappedViews();
    CItemFactory::Inst().DetachMappedViews();
    CCreatureFactory::Inst().DetachMappedViews();
}

template <typename TFactory>
bool InitializeFactory(std::string* error) {
    return L2ServerSharedFactoryMappingPublisher::InitializeDefault(TFactory::Inst(), error);
}

void LogFactoryMemory(
    // const wchar_t* format,
    const char* factoryName,
    std::int32_t maximumRecords,
    std::int32_t recordBytes
) {
    constexpr double kBytesToMegabytes = 1.0 / (1024.0 * 1024.0);
    // Log.Add(
    //     LOG_NORMAL,
    //     format,
    //     static_cast<double>(
    //         runtime::MultiplyInt32Wrapping(
    //             maximumRecords,
    //             recordBytes)) *
    //         kBytesToMegabytes)
    // L2_LOG_INFO(Log,
    //     format,
    //     static_cast<double>(
    //         runtime::MultiplyInt32Wrapping(
    //             maximumRecords,
    //             recordBytes)) *
    //         kBytesToMegabytes)
    L2_LOG_INFO(
        Log, "Shared factory %s initialized: capacity %d, record stride %d bytes, estimated record storage %.1f MiB.",
        factoryName, maximumRecords, recordBytes,
        static_cast<double>(static_cast<std::int64_t>(maximumRecords) * recordBytes) * kBytesToMegabytes
    );
}

} // namespace

const std::array<SharedFactoryDescriptor, 9>& SharedFactoryDescriptors() noexcept {
    static constexpr std::array<SharedFactoryDescriptor, 9> descriptors{{
        {SharedFactoryKind::Creature, &SharedDefine_Creature, 0x3ed8, 0x01, true, 1},
        {SharedFactoryKind::Item, &SharedDefine_Item, 0xa0, 0x1e0, true, 2},
        {SharedFactoryKind::StaticObject, &SharedDefine_StaticObject, 0x90, 0x04, true, 3},
        {SharedFactoryKind::Pledge, &SharedDefine_Pledge, 0x800, 0x04, true, 4},
        {SharedFactoryKind::Party, &SharedDefine_Party, 0xb0, 0x04, true, 5},
        {SharedFactoryKind::Alliance, &SharedDefine_Alliance, 0x118, 0x04, true, 6},
        {SharedFactoryKind::GeneralObject, &SharedDefine_GeneralObject, 0x118, 0x04, true, 7},
        {SharedFactoryKind::AirShip, &SharedDefine_AirShip, 0x58, 0x01, true, 8},
        {SharedFactoryKind::Shuttle, &SharedDefine_Shuttle, 0x48, 0x01, false, 0},
    }};
    return descriptors;
}

bool L2ServerSharedFactories::InitializeEager(std::string* error) {
    std::lock_guard<std::mutex> guard(CompositionMutex());
    if (AllEagerInitialized()) {
        return true;
    }

    // A partial prior attempt cannot be a valid current startup state. Drop
    // all portable lifetimes and retry from the exact first publisher.
    ShutdownUnlocked();
    // Current InitializeSharedFactory@0x00800394 constructs and reports each
    // eager factory in this interleaved order. The four-byte object-id lane
    // is included in every displayed record extent.
    if (!InitializeFactory<CCreatureFactory>(error)) {
        ShutdownUnlocked();
        return false;
    }
    LogFactoryMemory(
        // L"CCreatureFactory            [%05.1f]MB",
        "CCreatureFactory", SharedDefine_Creature.max_data, 0x3edc
    );

    if (!InitializeFactory<CItemFactory>(error)) {
        ShutdownUnlocked();
        return false;
    }
    LogFactoryMemory(
        // L"CItemFactory                [%05.1f]MB",
        "CItemFactory", SharedDefine_Item.max_data, 0xa4
    );

    if (!InitializeFactory<CStaticObjectFactory>(error)) {
        ShutdownUnlocked();
        return false;
    }
    LogFactoryMemory(
        // L"CStaticObjectFactory        [%05.1f]MB",
        "CStaticObjectFactory", SharedDefine_StaticObject.max_data, 0x94
    );

    if (!InitializeFactory<CPledgeFactory>(error)) {
        ShutdownUnlocked();
        return false;
    }
    LogFactoryMemory(
        // L"CPledgeFactory              [%05.1f]MB",
        "CPledgeFactory", SharedDefine_Pledge.max_data, 0x804
    );

    if (!InitializeFactory<CPartyFactory>(error)) {
        ShutdownUnlocked();
        return false;
    }
    LogFactoryMemory(
        // L"CPartyFactory               [%05.1f]MB",
        "CPartyFactory", SharedDefine_Party.max_data, 0xb4
    );

    if (!InitializeFactory<CAllianceFactory>(error)) {
        ShutdownUnlocked();
        return false;
    }
    LogFactoryMemory(
        // L"CAllianceFactory            [%05.1f]MB",
        "CAllianceFactory", SharedDefine_Alliance.max_data, 0x11c
    );

    if (!InitializeFactory<CGeneralObjectFactory>(error)) {
        ShutdownUnlocked();
        return false;
    }
    LogFactoryMemory(
        // L"CGeneralObjectFactory       [%05.1f]MB",
        "CGeneralObjectFactory", SharedDefine_GeneralObject.max_data, 0x11c
    );

    if (!InitializeFactory<CAirShipFactory>(error)) {
        ShutdownUnlocked();
        return false;
    }
    LogFactoryMemory(
        // L"CAirShipFactory             [%05.1f]MB",
        "CAirShipFactory", SharedDefine_AirShip.max_data, 0x5c
    );
    return true;
}

bool L2ServerSharedFactories::InitializeShuttle(std::string* error) {
    std::lock_guard<std::mutex> guard(CompositionMutex());
    CShuttleFactory& factory = CShuttleFactory::Inst();
    if (factory.IsInitialized()) {
        return true;
    }
    return InitializeFactory<CShuttleFactory>(error);
}

bool L2ServerSharedFactories::EagerInitialized() noexcept {
    std::lock_guard<std::mutex> guard(CompositionMutex());
    return AllEagerInitialized();
}

bool L2ServerSharedFactories::ShuttleInitialized() noexcept {
    std::lock_guard<std::mutex> guard(CompositionMutex());
    return CShuttleFactory::Inst().IsInitialized();
}

void L2ServerSharedFactories::Shutdown() noexcept {
    std::lock_guard<std::mutex> guard(CompositionMutex());
    ShutdownUnlocked();
}

} // namespace l2server
