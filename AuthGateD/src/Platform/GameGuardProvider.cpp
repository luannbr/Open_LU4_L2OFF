#include "Platform/GameGuardProvider.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <condition_variable>
#include <ctime>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#if defined(_WIN32)
#  include <Windows.h>
#else
#  include <dlfcn.h>
#endif

#if defined(AUTHGATED_HAS_UNICORN)
#  include <unicorn/unicorn.h>
#  include <unicorn/x86.h>
#endif

namespace authgated::platform {
namespace {

constexpr std::uint32_t kLoadFailure = 2;
constexpr std::uint32_t kMissingExport = 3;

void* OpenNativeModule(const char* path) noexcept {
#if defined(_WIN32)
    return ::LoadLibraryA(path);
#else
    return ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

void CloseNativeModule(void* module) noexcept {
    if (!module) return;
#if defined(_WIN32)
    ::FreeLibrary(static_cast<HMODULE>(module));
#else
    ::dlclose(module);
#endif
}

void* FindNativeSymbol(void* module, const char* name) noexcept {
#if defined(_WIN32)
    return reinterpret_cast<void*>(
        ::GetProcAddress(static_cast<HMODULE>(module), name));
#else
    return ::dlsym(module, name);
#endif
}

template <typename Function>
Function FunctionFromSymbol(void* symbol) noexcept {
    static_assert(std::is_pointer_v<Function>);
    Function result = nullptr;
    static_assert(sizeof(result) == sizeof(symbol));
    std::memcpy(&result, &symbol, sizeof(result));
    return result;
}

class NativeGameGuardProvider final : public GameGuardProvider {
public:
    using GetVersionFn = std::uint32_t (*)(int);
    using AuthFn = std::uint32_t (*)(
        std::uint32_t,
        _GG_VERSION*,
        std::uint32_t*,
        _GG_AUTH_DATA*,
        _GG_AUTH_DATA*);
    using CryptFn = std::uint32_t (*)(_GG_AUTH_DATA*);
    using UpdateFn = std::uint32_t (*)();

    explicit NativeGameGuardProvider(void* module) noexcept
        : m_module(module) {}

    ~NativeGameGuardProvider() override {
        CloseNativeModule(m_module);
    }

    bool Resolve() noexcept {
        m_getVersion = FunctionFromSymbol<GetVersionFn>(
            FindNativeSymbol(m_module, "PrtcGetVersion"));
        m_getQuery = FunctionFromSymbol<AuthFn>(
            FindNativeSymbol(m_module, "PrtcGetAuthQuery"));
        m_checkAnswer = FunctionFromSymbol<AuthFn>(
            FindNativeSymbol(m_module, "PrtcCheckAuthAnswer"));
        m_encryptQuery = FunctionFromSymbol<CryptFn>(
            FindNativeSymbol(m_module, "PrtcEncryptQuery"));
        m_decryptAnswer = FunctionFromSymbol<CryptFn>(
            FindNativeSymbol(m_module, "PrtcDecryptAnswer"));
        m_updateTimer = FunctionFromSymbol<UpdateFn>(
            FindNativeSymbol(m_module, "PrtcUpdateTimer"));
        return m_getVersion && m_getQuery && m_checkAnswer &&
            m_encryptQuery && m_decryptAnswer && m_updateTimer;
    }

    std::uint32_t GetVersion(bool useUpdateTimer) override {
        return m_getVersion(useUpdateTimer ? 1 : 0);
    }

    std::uint32_t GetAuthQuery(
        std::uint32_t option,
        _GG_VERSION* version,
        std::uint32_t* userFlag,
        _GG_AUTH_DATA* query,
        _GG_AUTH_DATA* answer) override {
        return m_getQuery(option, version, userFlag, query, answer);
    }

    std::uint32_t CheckAuthAnswer(
        std::uint32_t option,
        _GG_VERSION* version,
        std::uint32_t* userFlag,
        _GG_AUTH_DATA* query,
        _GG_AUTH_DATA* answer) override {
        return m_checkAnswer(option, version, userFlag, query, answer);
    }

    std::uint32_t EncryptQuery(_GG_AUTH_DATA* query) override {
        return m_encryptQuery(query);
    }

    std::uint32_t DecryptAnswer(_GG_AUTH_DATA* answer) override {
        return m_decryptAnswer(answer);
    }

    std::uint32_t UpdateTimer() override {
        return m_updateTimer();
    }

private:
    void* m_module = nullptr;
    GetVersionFn m_getVersion = nullptr;
    AuthFn m_getQuery = nullptr;
    AuthFn m_checkAnswer = nullptr;
    CryptFn m_encryptQuery = nullptr;
    CryptFn m_decryptAnswer = nullptr;
    UpdateFn m_updateTimer = nullptr;
};

#if defined(AUTHGATED_HAS_UNICORN)

constexpr std::uint64_t kImageBase = UINT64_C(0x10000000);
constexpr std::uint64_t kGetQuery = UINT64_C(0x1000ea90);
constexpr std::uint64_t kCheckAnswer = UINT64_C(0x1000ebb0);
constexpr std::uint64_t kRandomWord = UINT64_C(0x100037a0);
constexpr std::uint64_t kTimeSlot = UINT64_C(0x1001bf04);
constexpr std::uint64_t kStackBase = UINT64_C(0x20000000);
constexpr std::uint64_t kStackSize = UINT64_C(0x00010000);
constexpr std::uint64_t kScratchBase = UINT64_C(0x21000000);
constexpr std::uint64_t kScratchSize = UINT64_C(0x00010000);
constexpr std::uint64_t kReturnAddress = UINT64_C(0x22000000);

std::uint16_t ReadU16(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset) {
    if (offset + 2U > bytes.size()) return 0;
    return static_cast<std::uint16_t>(bytes[offset]) |
        static_cast<std::uint16_t>(bytes[offset + 1U] << 8U);
}

std::uint32_t ReadU32(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset) {
    if (offset + 4U > bytes.size()) return 0;
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
        (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

std::uint64_t PageSize(std::uint64_t value) noexcept {
    return (value + UINT64_C(0xfff)) & ~UINT64_C(0xfff);
}

struct ProviderRandomContext {
    std::uint64_t identity = 0;
};

std::atomic<std::uint64_t> g_nextProviderIdentity{1};
struct ProviderRandomSeed {
    std::uint64_t identity = 0;
    std::uint32_t seed = 1;
};
thread_local std::array<ProviderRandomSeed, 100>
    g_providerRandomSeeds{};

std::uint32_t ProviderRandomWord(std::uint64_t identity) noexcept {
    ProviderRandomSeed* selected = nullptr;
    for (ProviderRandomSeed& entry : g_providerRandomSeeds) {
        if (entry.identity == identity) {
            selected = &entry;
            break;
        }
        if (!selected && entry.identity == 0) selected = &entry;
    }
    if (!selected) {
        selected = &g_providerRandomSeeds[
            static_cast<std::size_t>(
                identity % g_providerRandomSeeds.size())];
        selected->identity = 0;
    }
    if (selected->identity == 0) {
        selected->identity = identity;
        selected->seed = 1;
    }
    std::uint32_t value = 0;
    for (std::uint32_t shift = 0; shift < 32U; shift += 4U) {
        selected->seed =
            selected->seed * UINT32_C(214013) +
            UINT32_C(2531011);
        const std::uint32_t random =
            (selected->seed >> 16U) & UINT32_C(0x7fff);
        value |= (random & 0x0fU) << shift;
    }
    return value;
}

void RandomWordHook(
    uc_engine* engine,
    [[maybe_unused]] std::uint64_t address,
    [[maybe_unused]] std::uint32_t size,
    void* userData) {
    std::uint32_t stack = 0;
    std::uint32_t returnAddress = 0;
    uc_reg_read(engine, UC_X86_REG_ESP, &stack);
    uc_mem_read(engine, stack, &returnAddress, sizeof(returnAddress));
    const auto* context =
        static_cast<const ProviderRandomContext*>(userData);
    const std::uint32_t value =
        ProviderRandomWord(context->identity);
    stack += sizeof(returnAddress);
    uc_reg_write(engine, UC_X86_REG_EAX, &value);
    uc_reg_write(engine, UC_X86_REG_ESP, &stack);
    uc_reg_write(engine, UC_X86_REG_EIP, &returnAddress);
}

class EmulatedPeGameGuardProvider final : public GameGuardProvider {
public:
    EmulatedPeGameGuardProvider() noexcept
        : m_randomContext{
              g_nextProviderIdentity.fetch_add(
                  1,
                  std::memory_order_relaxed)} {}

    ~EmulatedPeGameGuardProvider() override {
        {
            std::lock_guard<std::mutex> guard(m_timerMutex);
            m_stopTimer = true;
        }
        m_timerCondition.notify_all();
        if (m_timer.joinable()) m_timer.join();
        if (m_engine) uc_close(m_engine);
    }

    bool Load(const char* path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) return false;
        const std::vector<std::uint8_t> bytes{
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
        if (bytes.size() < 0x100U ||
            ReadU16(bytes, 0) != 0x5a4dU) {
            return false;
        }
        const std::size_t pe = ReadU32(bytes, 0x3c);
        if (pe + 0x18U > bytes.size() ||
            ReadU32(bytes, pe) != UINT32_C(0x00004550)) {
            return false;
        }
        const std::uint16_t sectionCount = ReadU16(bytes, pe + 6U);
        const std::uint16_t optionalSize = ReadU16(bytes, pe + 20U);
        const std::size_t optional = pe + 24U;
        if (optional + optionalSize > bytes.size() ||
            ReadU16(bytes, optional) != 0x10bU ||
            ReadU32(bytes, optional + 28U) != kImageBase) {
            return false;
        }
        const std::uint32_t imageSize = ReadU32(bytes, optional + 56U);
        const std::uint32_t headerSize = ReadU32(bytes, optional + 60U);
        if (imageSize == 0 || headerSize > bytes.size()) return false;

        if (uc_open(UC_ARCH_X86, UC_MODE_32, &m_engine) != UC_ERR_OK) {
            m_engine = nullptr;
            return false;
        }
        if (uc_mem_map(
                m_engine,
                kImageBase,
                PageSize(imageSize),
                UC_PROT_ALL) != UC_ERR_OK ||
            uc_mem_write(
                m_engine,
                kImageBase,
                bytes.data(),
                headerSize) != UC_ERR_OK) {
            return false;
        }

        const std::size_t sections = optional + optionalSize;
        for (std::uint16_t index = 0; index < sectionCount; ++index) {
            const std::size_t section = sections + index * 40U;
            if (section + 40U > bytes.size()) return false;
            const std::uint32_t virtualAddress =
                ReadU32(bytes, section + 12U);
            const std::uint32_t rawSize =
                ReadU32(bytes, section + 16U);
            const std::uint32_t rawOffset =
                ReadU32(bytes, section + 20U);
            if (rawSize == 0) continue;
            if (static_cast<std::size_t>(rawOffset) + rawSize >
                bytes.size()) {
                return false;
            }
            if (uc_mem_write(
                    m_engine,
                    kImageBase + virtualAddress,
                    bytes.data() + rawOffset,
                    rawSize) != UC_ERR_OK) {
                return false;
            }
        }
        if (uc_mem_map(
                m_engine,
                kStackBase,
                kStackSize,
                UC_PROT_ALL) != UC_ERR_OK ||
            uc_mem_map(
                m_engine,
                kScratchBase,
                kScratchSize,
                UC_PROT_ALL) != UC_ERR_OK) {
            return false;
        }
        if (uc_hook_add(
                m_engine,
                &m_randomHook,
                UC_HOOK_CODE,
                reinterpret_cast<void*>(RandomWordHook),
                &m_randomContext,
                kRandomWord,
                kRandomWord) != UC_ERR_OK) {
            return false;
        }
        UpdateTimeSlot();
        return true;
    }

    std::uint32_t GetVersion(bool useUpdateTimer) override {
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            UpdateTimeSlot();
        }
        // PrtcGetVersion(0) creates a timer-queue callback with a 300000 ms
        // period. A nonzero argument means the caller owns UpdateTimer.
        if (!useUpdateTimer && !StartTimer()) return 0;
        return UINT32_C(0x00010060);
    }

    std::uint32_t GetAuthQuery(
        std::uint32_t option,
        _GG_VERSION* version,
        std::uint32_t* userFlag,
        _GG_AUTH_DATA* query,
        _GG_AUTH_DATA* answer) override {
        std::lock_guard<std::mutex> guard(m_mutex);
        return RunAuthFunction(
            kGetQuery,
            option,
            version,
            userFlag,
            query,
            answer);
    }

    std::uint32_t CheckAuthAnswer(
        std::uint32_t option,
        _GG_VERSION* version,
        std::uint32_t* userFlag,
        _GG_AUTH_DATA* query,
        _GG_AUTH_DATA* answer) override {
        std::lock_guard<std::mutex> guard(m_mutex);
        return RunAuthFunction(
            kCheckAnswer,
            option,
            version,
            userFlag,
            query,
            answer);
    }

    std::uint32_t EncryptQuery(
        [[maybe_unused]] _GG_AUTH_DATA* query) override {
        return 0;
    }

    std::uint32_t DecryptAnswer(
        [[maybe_unused]] _GG_AUTH_DATA* answer) override {
        return 0;
    }

    std::uint32_t UpdateTimer() override {
        std::lock_guard<std::mutex> guard(m_mutex);
        UpdateTimeSlot();
        return 0;
    }

private:
    bool StartTimer() {
        std::lock_guard<std::mutex> guard(m_timerMutex);
        if (m_timer.joinable()) return true;
        try {
            m_timer = std::thread([this] {
                std::unique_lock<std::mutex> timerGuard(m_timerMutex);
                while (!m_timerCondition.wait_for(
                    timerGuard,
                    std::chrono::milliseconds(300000),
                    [this] { return m_stopTimer; })) {
                    timerGuard.unlock();
                    {
                        std::lock_guard<std::mutex> engineGuard(m_mutex);
                        UpdateTimeSlot();
                    }
                    timerGuard.lock();
                }
            });
        } catch (...) {
            return false;
        }
        return true;
    }

    void UpdateTimeSlot() {
        const std::time_t now = std::time(nullptr);
        std::tm utc{};
#if defined(_WIN32)
        gmtime_s(&utc, &now);
#else
        gmtime_r(&now, &utc);
#endif
        const std::uint32_t month =
            static_cast<std::uint32_t>(utc.tm_mon + 1);
        const std::uint32_t day =
            static_cast<std::uint32_t>(utc.tm_mday);
        const std::uint32_t hour =
            static_cast<std::uint32_t>(utc.tm_hour);
        const std::uint32_t value = day == 31U
            ? month * UINT32_C(0x708) + hour - UINT32_C(0x450)
            : (month * UINT32_C(0x4b) + day) * 24U +
                hour - UINT32_C(0x720);
        const std::uint16_t slot = static_cast<std::uint16_t>(
            (value / 3U) % 500U);
        uc_mem_write(m_engine, kTimeSlot, &slot, sizeof(slot));
    }

    std::uint32_t RunAuthFunction(
        std::uint64_t function,
        std::uint32_t option,
        _GG_VERSION* version,
        std::uint32_t* userFlag,
        _GG_AUTH_DATA* query,
        _GG_AUTH_DATA* answer) {
        constexpr std::uint32_t versionAddress =
            static_cast<std::uint32_t>(kScratchBase + 0x000);
        constexpr std::uint32_t flagAddress =
            static_cast<std::uint32_t>(kScratchBase + 0x020);
        constexpr std::uint32_t queryAddress =
            static_cast<std::uint32_t>(kScratchBase + 0x040);
        constexpr std::uint32_t answerAddress =
            static_cast<std::uint32_t>(kScratchBase + 0x060);
        uc_mem_write(m_engine, versionAddress, version, sizeof(*version));
        uc_mem_write(m_engine, flagAddress, userFlag, sizeof(*userFlag));
        uc_mem_write(m_engine, queryAddress, query, sizeof(*query));
        uc_mem_write(m_engine, answerAddress, answer, sizeof(*answer));

        const std::array<std::uint32_t, 6> frame{
            static_cast<std::uint32_t>(kReturnAddress),
            option,
            versionAddress,
            flagAddress,
            queryAddress,
            answerAddress};
        std::uint32_t stack = static_cast<std::uint32_t>(
            kStackBase + kStackSize - frame.size() * sizeof(std::uint32_t));
        uc_mem_write(m_engine, stack, frame.data(), sizeof(frame));

        constexpr std::array<int, 8> registers{
            UC_X86_REG_EAX,
            UC_X86_REG_EBX,
            UC_X86_REG_ECX,
            UC_X86_REG_EDX,
            UC_X86_REG_ESI,
            UC_X86_REG_EDI,
            UC_X86_REG_EBP,
            UC_X86_REG_EFLAGS};
        const std::uint32_t zero = 0;
        for (const int reg : registers) {
            uc_reg_write(m_engine, reg, &zero);
        }
        const std::uint32_t initialFlags = 2;
        uc_reg_write(m_engine, UC_X86_REG_EFLAGS, &initialFlags);
        uc_reg_write(m_engine, UC_X86_REG_ESP, &stack);
        const uc_err execution = uc_emu_start(
            m_engine,
            function,
            kReturnAddress,
            0,
            1000000);
        if (execution != UC_ERR_OK) return UINT32_C(0xffffffff);

        std::uint32_t result = 0;
        uc_reg_read(m_engine, UC_X86_REG_EAX, &result);
        uc_mem_read(m_engine, flagAddress, userFlag, sizeof(*userFlag));
        uc_mem_read(m_engine, queryAddress, query, sizeof(*query));
        uc_mem_read(m_engine, answerAddress, answer, sizeof(*answer));
        return result;
    }

    uc_engine* m_engine = nullptr;
    uc_hook m_randomHook = 0;
    ProviderRandomContext m_randomContext;
    std::mutex m_mutex;
    std::mutex m_timerMutex;
    std::condition_variable m_timerCondition;
    std::thread m_timer;
    bool m_stopTimer = false;
};

#endif

} // namespace

std::unique_ptr<GameGuardProvider> LoadGameGuardProvider(
    const char* path,
    std::uint32_t& error) {
    error = kLoadFailure;
    if (void* module = OpenNativeModule(path)) {
        auto provider =
            std::make_unique<NativeGameGuardProvider>(module);
        if (provider->Resolve()) {
            error = 0;
            return provider;
        }
        error = kMissingExport;
        return {};
    }

#if defined(AUTHGATED_HAS_UNICORN)
    auto provider = std::make_unique<EmulatedPeGameGuardProvider>();
    if (provider->Load(path)) {
        error = 0;
        return provider;
    }
#endif
    return {};
}

} // namespace authgated::platform
