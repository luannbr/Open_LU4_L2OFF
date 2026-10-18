#pragma once

#include "Shared/src/SharedFactory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace l2server {

enum class SharedFactoryKind : std::uint8_t {
    Creature,
    Item,
    StaticObject,
    Pledge,
    Party,
    Alliance,
    GeneralObject,
    AirShip,
    Shuttle,
};

struct SharedFactoryDescriptor final {
    SharedFactoryKind kind;
    const CSharedDefine* define;
    std::size_t recordStride;
    std::size_t constantStride;
    bool eager;
    std::uint8_t publicationOrder;
};

const std::array<SharedFactoryDescriptor, 9>& SharedFactoryDescriptors() noexcept;

// The current root publishes eight factories after CWorld/geodata in this
// exact order and leaves Shuttle lazy. This product composition performs no
// listener/readiness publication. Later startup gates still own that action.
class L2ServerSharedFactories final {
public:
    static bool InitializeEager(std::string* error = nullptr);
    static bool InitializeShuttle(std::string* error = nullptr);
    static bool EagerInitialized() noexcept;
    static bool ShuttleInitialized() noexcept;
    static void Shutdown() noexcept;
};

} // namespace l2server
