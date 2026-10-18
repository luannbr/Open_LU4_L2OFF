#include "Shared/src/Runtime/SharedFactoryMapping.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace {
using LifetimeMap = std::unordered_map<const void*, std::shared_ptr<void>>;

std::mutex& MappingMutex() {
    static auto* const mutex = new std::mutex;
    return *mutex;
}

LifetimeMap& MappingLifetimes() {
    // The registry shell must survive factory atexit destructors. Individual
    // mappings are still released by the existing product Detach operation.
    static auto* const lifetimes = new LifetimeMap;
    return *lifetimes;
}
} // namespace

// Previous product registry shells. Now shared above.
// namespace l2server_factory_detail {
// namespace {
//
// using LifetimeMap = std::unordered_map<const void*, std::shared_ptr<void>>
//
// std::mutex& MappingMutex() {
//     static auto* const mutex = new std::mutex
//     return *mutex
// }
//
// LifetimeMap& MappingLifetimes() {
//     // Factory singletons are destroyed during process exit. Leak only the
//     // registry shell so its lifetime cannot race those atexit destructors.
//     // each mapping lifetime itself is released by DetachMappedViews.
//     static auto* const lifetimes = new LifetimeMap.
//     return *lifetimes
// }
//
// } // namespace
//

// using LifetimeMap =
//     std::unordered_map<const void*, std::shared_ptr<void>>
//
// std::mutex& MappingMutex() {
//     // The exact factory is a function-static process singleton. Keep the
//     // host registry alive through its atexit destructor regardless of which
//     // one was first touched.
//     static auto* mutex = new std::mutex
//     return *mutex
// }
//
// LifetimeMap& MappingLifetimes() {
//     static auto* lifetimes = new LifetimeMap.
//     return *lifetimes
// }
//

#if defined(L2_BUILD_L2SERVER)
#include "Shared/src/Runtime/SharedMemoryRegion.h"

namespace l2server_factory_detail {
bool AdoptMappingLifetime(const void* factory, std::shared_ptr<void> lifetime, std::string* error) {
    if (!factory || !lifetime) {
        if (error) {
            *error = "shared factory mapping lifetime is unavailable";
        }
        return false;
    }

    std::lock_guard<std::mutex> guard(MappingMutex());
    const auto [position, inserted] = MappingLifetimes().emplace(factory, std::move(lifetime));
    if (!inserted && error) {
        *error = "shared factory mapping lifetime is already registered";
    }
    return inserted;
}

void ReleaseMappingLifetime(const void* factory) noexcept {
    std::shared_ptr<void> lifetime;
    {
        std::lock_guard<std::mutex> guard(MappingMutex());
        auto& lifetimes = MappingLifetimes();
        const auto found = lifetimes.find(factory);
        if (found == lifetimes.end()) {
            return;
        }
        lifetime = std::move(found->second);
        lifetimes.erase(found);
    }
}

bool HasMappingLifetime(const void* factory) noexcept {
    std::lock_guard<std::mutex> guard(MappingMutex());
    return MappingLifetimes().find(factory) != MappingLifetimes().end();
}


} // namespace l2server_factory_detail

namespace {

struct FactoryMappingLifetime final {
    SharedMemoryRegion data;
    SharedMemoryRegion constants;
    SharedMemoryRegion index;
};

} // namespace

bool L2ServerSharedFactoryMappingPublisher::CreateMappings(
    const CSharedDefine& define,
    std::size_t dataBytes,
    std::size_t constantBytes,
    std::size_t indexBytes,
    L2ServerSharedFactoryMapping& output,
    std::string* error
) {
    output = {};
    auto lifetime = std::make_shared<FactoryMappingLifetime>();
    if (!lifetime->data.CreateNamed(define.DataMMFName, dataBytes, SharedMemoryCreateMode::LiteralRequestedExtent, error)) {
        return false;
    }
    if (define.max_class > 0 &&
        !lifetime->constants.CreateNamed(
            define.ConstantMMFName, constantBytes, SharedMemoryCreateMode::LiteralRequestedExtent, error
        )) {
        return false;
    }
    if (!lifetime->index.CreateNamed(
            define.IndexMMFName, indexBytes, SharedMemoryCreateMode::LiteralRequestedExtent, error
        )) {
        return false;
    }

    output.dataHandle = lifetime->data.NativeHandle();
    output.data = lifetime->data.Data();
    output.dataBytes = dataBytes;
    if (define.max_class > 0) {
        output.constantHandle = lifetime->constants.NativeHandle();
        output.constants = lifetime->constants.Data();
        output.constantBytes = constantBytes;
    }
    output.indexHandle = lifetime->index.NativeHandle();
    output.index = static_cast<std::int32_t*>(lifetime->index.Data());
    output.indexBytes = indexBytes;
    output.lifetime = std::move(lifetime);
    return true;
}

#else
#include "Shared/src/Runtime/ErrorText.h"

namespace runtime {
namespace {

using AllianceFactory = CSharedFactory<CAlliance, CSharedAllianceData, CSharedAllianceConstant, &SharedDefine_Alliance>;

} // namespace

bool SharedFactoryMappingAccess::Attach(
    AllianceFactory& factory,
    void* dataBase,
    std::size_t,
    void* indexBase,
    std::size_t,
    std::shared_ptr<void> lifetime,
    std::string* error
) {
    if (!lifetime) {
        SetErrorText(error, "alliance shared mapping lifetime is unavailable");
        return false;
    }
    const bool complete = dataBase && indexBase;

    std::shared_ptr<void> previous;
    {
        std::lock_guard<std::mutex> guard(MappingMutex());
        auto& lifetimes = MappingLifetimes();
        const auto found = lifetimes.find(&factory);
        if (found != lifetimes.end()) {
            previous = std::move(found->second);
            found->second = std::move(lifetime);
        } else {
            lifetimes.emplace(&factory, std::move(lifetime));
        }

        factory.m_hfm_data = nullptr;
        factory.m_data = static_cast<CSharedAllianceData*>(dataBase);
        factory.m_hfm_const = nullptr;
        factory.m_const = nullptr;
        factory.m_const_num = 0;
        factory.m_hfm_index = nullptr;
        factory.m_index = static_cast<std::int32_t*>(indexBase);
    }
    if (!complete) {
        SetErrorText(error, "alliance shared data/index mapping is unavailable");
    }
    return complete;
}

void SharedFactoryMappingAccess::Detach(AllianceFactory& factory) {
    std::shared_ptr<void> lifetime;
    {
        std::lock_guard<std::mutex> guard(MappingMutex());
        auto& lifetimes = MappingLifetimes();
        const auto found = lifetimes.find(&factory);
        if (found != lifetimes.end()) {
            lifetime = std::move(found->second);
            lifetimes.erase(found);
        }

        factory.m_hfm_index = nullptr;
        factory.m_index = nullptr;
        factory.m_hfm_const = nullptr;
        factory.m_const = nullptr;
        factory.m_const_num = 0;
        factory.m_hfm_data = nullptr;
        factory.m_data = nullptr;
    }
}

bool SharedFactoryMappingAccess::HasView(const AllianceFactory& factory) {
    std::lock_guard<std::mutex> guard(MappingMutex());
    const auto& lifetimes = MappingLifetimes();
    return factory.m_data && factory.m_index && lifetimes.find(&factory) != lifetimes.end();
}

bool SharedFactoryMappingAccess::Attach(
    CStaticObjectFactory& factory,
    void* dataBase,
    std::size_t,
    void* indexBase,
    std::size_t,
    std::shared_ptr<void> lifetime,
    std::string* error
) {
    if (!lifetime) {
        SetErrorText(error, "static-object shared mapping lifetime is unavailable");
        return false;
    }
    const bool complete = dataBase && indexBase;

    std::shared_ptr<void> previous;
    {
        std::lock_guard<std::mutex> guard(MappingMutex());
        auto& lifetimes = MappingLifetimes();
        const auto found = lifetimes.find(&factory);
        if (found != lifetimes.end()) {
            previous = std::move(found->second);
            found->second = std::move(lifetime);
        } else {
            lifetimes.emplace(&factory, std::move(lifetime));
        }

        factory.m_hfm_data = nullptr;
        factory.m_data = static_cast<CSharedStaticObjectData*>(dataBase);
        factory.m_hfm_const = nullptr;
        factory.m_const = nullptr;
        factory.m_const_num = 0;
        factory.m_hfm_index = nullptr;
        factory.m_index = static_cast<std::int32_t*>(indexBase);
    }
    if (!complete) {
        SetErrorText(error, "static-object shared data/index mapping is unavailable");
    }
    return complete;
}

void SharedFactoryMappingAccess::Detach(CStaticObjectFactory& factory) {
    std::shared_ptr<void> lifetime;
    {
        std::lock_guard<std::mutex> guard(MappingMutex());
        auto& lifetimes = MappingLifetimes();
        const auto found = lifetimes.find(&factory);
        if (found != lifetimes.end()) {
            lifetime = std::move(found->second);
            lifetimes.erase(found);
        }

        factory.m_hfm_index = nullptr;
        factory.m_index = nullptr;
        factory.m_hfm_const = nullptr;
        factory.m_const = nullptr;
        factory.m_const_num = 0;
        factory.m_hfm_data = nullptr;
        factory.m_data = nullptr;
    }
}

bool SharedFactoryMappingAccess::HasView(const CStaticObjectFactory& factory) {
    std::lock_guard<std::mutex> guard(MappingMutex());
    const auto& lifetimes = MappingLifetimes();
    return factory.m_data && factory.m_index && lifetimes.find(&factory) != lifetimes.end();
}

bool SharedFactoryMappingAccess::Attach(
    CAirShipFactory& factory,
    void* dataBase,
    std::size_t,
    void* constantBase,
    std::size_t,
    void* indexBase,
    std::size_t,
    std::shared_ptr<void> lifetime,
    std::string* error
) {
    if (!lifetime) {
        SetErrorText(error, "airship shared mapping lifetime is unavailable");
        return false;
    }
    const bool complete = dataBase && constantBase && indexBase;

    std::shared_ptr<void> previous;
    {
        std::lock_guard<std::mutex> guard(MappingMutex());
        auto& lifetimes = MappingLifetimes();
        const auto found = lifetimes.find(&factory);
        if (found != lifetimes.end()) {
            previous = std::move(found->second);
            found->second = std::move(lifetime);
        } else {
            lifetimes.emplace(&factory, std::move(lifetime));
        }

        factory.m_hfm_data = nullptr;
        factory.m_data = static_cast<CSharedAirShipData*>(dataBase);
        factory.m_hfm_const = nullptr;
        factory.m_const = static_cast<CSharedAirShipConstant*>(constantBase);
        // The consumer constructor at 0x00458084 maps the complete
        // Constant object but leaves m_const_num at its zero-initialized
        // value. The field is producer-side bookkeeping. GetConstant uses
        // SharedDefine_AirShip.max_class directly.
        factory.m_const_num = 0;
        factory.m_hfm_index = nullptr;
        factory.m_index = static_cast<std::int32_t*>(indexBase);
    }
    if (!complete) {
        SetErrorText(error, "airship shared data/constant/index mapping is unavailable");
    }
    return complete;
}

void SharedFactoryMappingAccess::Detach(CAirShipFactory& factory) {
    std::shared_ptr<void> lifetime;
    {
        std::lock_guard<std::mutex> guard(MappingMutex());
        auto& lifetimes = MappingLifetimes();
        const auto found = lifetimes.find(&factory);
        if (found != lifetimes.end()) {
            lifetime = std::move(found->second);
            lifetimes.erase(found);
        }

        factory.m_hfm_index = nullptr;
        factory.m_index = nullptr;
        factory.m_hfm_const = nullptr;
        factory.m_const = nullptr;
        factory.m_const_num = 0;
        factory.m_hfm_data = nullptr;
        factory.m_data = nullptr;
    }
}

bool SharedFactoryMappingAccess::HasView(const CAirShipFactory& factory) {
    std::lock_guard<std::mutex> guard(MappingMutex());
    const auto& lifetimes = MappingLifetimes();
    return factory.m_data && factory.m_const && factory.m_index && lifetimes.find(&factory) != lifetimes.end();
}

bool SharedFactoryMappingAccess::Attach(
    CCreatureFactory& factory,
    void* dataBase,
    std::size_t,
    void* indexBase,
    std::size_t,
    std::shared_ptr<void> lifetime,
    std::string* error
) {
    if (!lifetime) {
        SetErrorText(error, "creature shared mapping lifetime is unavailable");
        return false;
    }
    const bool complete = dataBase && indexBase;

    std::shared_ptr<void> previous;
    {
        std::lock_guard<std::mutex> guard(MappingMutex());
        auto& lifetimes = MappingLifetimes();
        const auto found = lifetimes.find(&factory);
        if (found != lifetimes.end()) {
            previous = std::move(found->second);
            found->second = std::move(lifetime);
        } else {
            lifetimes.emplace(&factory, std::move(lifetime));
        }

        factory.m_hfm_data = nullptr;
        factory.m_data = static_cast<CSharedCreatureData*>(dataBase);
        factory.m_hfm_const = nullptr;
        factory.m_const = nullptr;
        factory.m_const_num = 0;
        factory.m_hfm_index = nullptr;
        factory.m_index = static_cast<std::int32_t*>(indexBase);
    }
    if (!complete) {
        SetErrorText(error, "creature shared data/index mapping is unavailable");
    }
    return complete;
}

void SharedFactoryMappingAccess::Detach(CCreatureFactory& factory) {
    std::shared_ptr<void> lifetime;
    {
        std::lock_guard<std::mutex> guard(MappingMutex());
        auto& lifetimes = MappingLifetimes();
        const auto found = lifetimes.find(&factory);
        if (found != lifetimes.end()) {
            lifetime = std::move(found->second);
            lifetimes.erase(found);
        }

        factory.m_hfm_index = nullptr;
        factory.m_index = nullptr;
        factory.m_hfm_const = nullptr;
        factory.m_const = nullptr;
        factory.m_const_num = 0;
        factory.m_hfm_data = nullptr;
        factory.m_data = nullptr;
    }
}

bool SharedFactoryMappingAccess::HasView(const CCreatureFactory& factory) {
    std::lock_guard<std::mutex> guard(MappingMutex());
    const auto& lifetimes = MappingLifetimes();
    return factory.m_data && factory.m_index && lifetimes.find(&factory) != lifetimes.end();
}

bool SharedFactoryMappingAccess::Attach(
    CPartyFactory& factory,
    void* dataBase,
    std::size_t,
    void* indexBase,
    std::size_t,
    std::shared_ptr<void> lifetime,
    std::string* error
) {
    if (!lifetime) {
        SetErrorText(error, "party shared mapping lifetime is unavailable");
        return false;
    }
    const bool complete = dataBase && indexBase;

    std::shared_ptr<void> previous;
    {
        std::lock_guard<std::mutex> guard(MappingMutex());
        auto& lifetimes = MappingLifetimes();
        const auto found = lifetimes.find(&factory);
        if (found != lifetimes.end()) {
            previous = std::move(found->second);
            found->second = std::move(lifetime);
        } else {
            lifetimes.emplace(&factory, std::move(lifetime));
        }

        factory.m_hfm_data = nullptr;
        factory.m_data = static_cast<CSharedPartyData*>(dataBase);
        factory.m_hfm_const = nullptr;
        factory.m_const = nullptr;
        factory.m_const_num = 0;
        factory.m_hfm_index = nullptr;
        factory.m_index = static_cast<std::int32_t*>(indexBase);
    }
    if (!complete) {
        SetErrorText(error, "party shared data/index mapping is unavailable");
    }
    return complete;
}

void SharedFactoryMappingAccess::Detach(CPartyFactory& factory) {
    std::shared_ptr<void> lifetime;
    {
        std::lock_guard<std::mutex> guard(MappingMutex());
        auto& lifetimes = MappingLifetimes();
        const auto found = lifetimes.find(&factory);
        if (found != lifetimes.end()) {
            lifetime = std::move(found->second);
            lifetimes.erase(found);
        }

        factory.m_hfm_index = nullptr;
        factory.m_index = nullptr;
        factory.m_hfm_const = nullptr;
        factory.m_const = nullptr;
        factory.m_const_num = 0;
        factory.m_hfm_data = nullptr;
        factory.m_data = nullptr;
    }
}

bool SharedFactoryMappingAccess::HasView(const CPartyFactory& factory) {
    std::lock_guard<std::mutex> guard(MappingMutex());
    const auto& lifetimes = MappingLifetimes();
    return factory.m_data && factory.m_index && lifetimes.find(&factory) != lifetimes.end();
}

bool SharedFactoryMappingAccess::Attach(
    CPledgeFactory& factory,
    void* dataBase,
    std::size_t,
    void* indexBase,
    std::size_t,
    std::shared_ptr<void> lifetime,
    std::string* error
) {
    if (!lifetime) {
        SetErrorText(error, "pledge shared mapping lifetime is unavailable");
        return false;
    }
    const bool complete = dataBase && indexBase;

    std::shared_ptr<void> previous;
    {
        std::lock_guard<std::mutex> guard(MappingMutex());
        auto& lifetimes = MappingLifetimes();
        const auto found = lifetimes.find(&factory);
        if (found != lifetimes.end()) {
            previous = std::move(found->second);
            found->second = std::move(lifetime);
        } else {
            lifetimes.emplace(&factory, std::move(lifetime));
        }

        factory.m_hfm_data = nullptr;
        factory.m_data = static_cast<CSharedPledgeData*>(dataBase);
        factory.m_hfm_const = nullptr;
        factory.m_const = nullptr;
        factory.m_const_num = 0;
        factory.m_hfm_index = nullptr;
        factory.m_index = static_cast<std::int32_t*>(indexBase);
    }
    if (!complete) {
        SetErrorText(error, "pledge shared data/index mapping is unavailable");
    }
    return complete;
}

void SharedFactoryMappingAccess::Detach(CPledgeFactory& factory) {
    std::shared_ptr<void> lifetime;
    {
        std::lock_guard<std::mutex> guard(MappingMutex());
        auto& lifetimes = MappingLifetimes();
        const auto found = lifetimes.find(&factory);
        if (found != lifetimes.end()) {
            lifetime = std::move(found->second);
            lifetimes.erase(found);
        }

        factory.m_hfm_index = nullptr;
        factory.m_index = nullptr;
        factory.m_hfm_const = nullptr;
        factory.m_const = nullptr;
        factory.m_const_num = 0;
        factory.m_hfm_data = nullptr;
        factory.m_data = nullptr;
    }
}

bool SharedFactoryMappingAccess::HasView(const CPledgeFactory& factory) {
    std::lock_guard<std::mutex> guard(MappingMutex());
    const auto& lifetimes = MappingLifetimes();
    return factory.m_data && factory.m_index && lifetimes.find(&factory) != lifetimes.end();
}

bool SharedFactoryMappingAccess::Attach(
    CGeneralObjectFactory& factory,
    void* dataBase,
    std::size_t,
    void* indexBase,
    std::size_t,
    std::shared_ptr<void> lifetime,
    std::string* error
) {
    if (!lifetime) {
        SetErrorText(error, "general-object shared mapping lifetime is unavailable");
        return false;
    }
    const bool complete = dataBase && indexBase;

    std::shared_ptr<void> previous;
    {
        std::lock_guard<std::mutex> guard(MappingMutex());
        auto& lifetimes = MappingLifetimes();
        const auto found = lifetimes.find(&factory);
        if (found != lifetimes.end()) {
            previous = std::move(found->second);
            found->second = std::move(lifetime);
        } else {
            lifetimes.emplace(&factory, std::move(lifetime));
        }

        factory.m_hfm_data = nullptr;
        factory.m_data = static_cast<CSharedGeneralObjectData*>(dataBase);
        factory.m_hfm_const = nullptr;
        factory.m_const = nullptr;
        factory.m_const_num = 0;
        factory.m_hfm_index = nullptr;
        factory.m_index = static_cast<std::int32_t*>(indexBase);
    }
    if (!complete) {
        SetErrorText(error, "general-object shared data/index mapping is unavailable");
    }
    return complete;
}

void SharedFactoryMappingAccess::Detach(CGeneralObjectFactory& factory) {
    std::shared_ptr<void> lifetime;
    {
        std::lock_guard<std::mutex> guard(MappingMutex());
        auto& lifetimes = MappingLifetimes();
        const auto found = lifetimes.find(&factory);
        if (found != lifetimes.end()) {
            lifetime = std::move(found->second);
            lifetimes.erase(found);
        }

        factory.m_hfm_index = nullptr;
        factory.m_index = nullptr;
        factory.m_hfm_const = nullptr;
        factory.m_const = nullptr;
        factory.m_const_num = 0;
        factory.m_hfm_data = nullptr;
        factory.m_data = nullptr;
    }
}

bool SharedFactoryMappingAccess::HasView(const CGeneralObjectFactory& factory) {
    std::lock_guard<std::mutex> guard(MappingMutex());
    const auto& lifetimes = MappingLifetimes();
    return factory.m_data && factory.m_index && lifetimes.find(&factory) != lifetimes.end();
}

bool SharedFactoryMappingAccess::Attach(
    CItemFactory& factory,
    void* dataBase,
    std::size_t,
    void* constantBase,
    std::size_t,
    void* indexBase,
    std::size_t,
    std::shared_ptr<void> lifetime,
    std::string* error
) {
    if (!lifetime) {
        SetErrorText(error, "item shared mapping lifetime is unavailable");
        return false;
    }
    const bool complete = dataBase && constantBase && indexBase;

    std::shared_ptr<void> previous;
    {
        std::lock_guard<std::mutex> guard(MappingMutex());
        auto& lifetimes = MappingLifetimes();
        const auto found = lifetimes.find(&factory);
        if (found != lifetimes.end()) {
            previous = std::move(found->second);
            found->second = std::move(lifetime);
        } else {
            lifetimes.emplace(&factory, std::move(lifetime));
        }

        factory.m_hfm_data = nullptr;
        factory.m_data = static_cast<CSharedItemData*>(dataBase);
        factory.m_hfm_const = nullptr;
        factory.m_const = static_cast<CSharedItemConstant*>(constantBase);
        // Current L2NPC.exe 0x004543a8 likewise maps ItemConstant without
        // populating m_const_num. Keep the consumer image identical. The
        // bounds check is against SharedDefine_Item.max_class.
        factory.m_const_num = 0;
        factory.m_hfm_index = nullptr;
        factory.m_index = static_cast<std::int32_t*>(indexBase);
    }
    if (!complete) {
        SetErrorText(error, "item shared data/constant/index mapping is unavailable");
    }
    return complete;
}

void SharedFactoryMappingAccess::Detach(CItemFactory& factory) {
    std::shared_ptr<void> lifetime;
    {
        std::lock_guard<std::mutex> guard(MappingMutex());
        auto& lifetimes = MappingLifetimes();
        const auto found = lifetimes.find(&factory);
        if (found != lifetimes.end()) {
            lifetime = std::move(found->second);
            lifetimes.erase(found);
        }

        factory.m_hfm_index = nullptr;
        factory.m_index = nullptr;
        factory.m_hfm_const = nullptr;
        factory.m_const = nullptr;
        factory.m_const_num = 0;
        factory.m_hfm_data = nullptr;
        factory.m_data = nullptr;
    }
}

bool SharedFactoryMappingAccess::HasView(const CItemFactory& factory) {
    std::lock_guard<std::mutex> guard(MappingMutex());
    const auto& lifetimes = MappingLifetimes();
    return factory.m_data && factory.m_const && factory.m_index && lifetimes.find(&factory) != lifetimes.end();
}

} // namespace runtime

#endif
