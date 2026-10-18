#include "GameGuardServerLibCompat.h"

// Current L2Server.exe 0x013c4f00/0x013c4f50 contains the same 4168
// main P/S bytes as this already recovered table (SHA-256
// 1b2102001d189daaf6da08d5655426cfb6f48aab720f6ee488ca754c16ff01f3).
#include "../../../AuthGateD/src/BlowfishTables.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iterator>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

constexpr std::uint32_t kErrorAllocation = 1;
constexpr std::uint32_t kErrorModule = 2;
constexpr std::uint32_t kErrorExport = 3;
constexpr std::uint32_t kErrorInactive = 4;
constexpr std::uint32_t kErrorConfig = 5;
constexpr std::uint32_t kErrorProtocolMismatch = 0x0c;
constexpr std::uint32_t kErrorInvalidVersion = 0x0e;
constexpr std::uint32_t kErrorDuplicateClient = 0x0f;
constexpr std::uint32_t kInitialGameGuardVersion = 2003010101U;
constexpr std::uint32_t kLimitedGameGuardVersion = 2100010101U;
constexpr std::uint32_t kVersionRange = 97000000U;
constexpr std::uint32_t kCandidateExpirySeconds = 3600U;
constexpr std::size_t kCandidateCount = 5;
constexpr std::size_t kClientMapWidth = 200;
constexpr std::size_t kClientMapBuckets = 100;

constexpr std::uint32_t kUserCheckVersion = 0x01;
constexpr std::uint32_t kUserHasAnswer = 0x02;
constexpr std::uint32_t kUserNewGameGuard = 0x04;
constexpr std::uint32_t kUserNewProtocol = 0x08;
constexpr std::uint32_t kUserInitialized = 0x20;

struct BlowfishState final {
    std::uint32_t p[18];
    std::uint32_t s[4][256];
};


struct VersionCandidate final {
    std::uint32_t version;
    std::uint32_t count;
    std::uint32_t lastSeen;
    std::uint32_t firstSeen;
};


struct ClientIdentity final {
    std::uint32_t uniqueValue1;
    std::uint32_t uniqueValue2;
    std::uint32_t serverKey;
};


// Current L2Server.exe 0x38816450. The embedded vendor diagnostic names this
// physical aggregate ggGlobal. CRT slot 15516 clears its complete 0x1e0 bytes.
struct GameGuardGlobalState final {
    std::uint32_t protocolCount; // +0x000
    std::uint32_t maximumActiveUsers; // +0x004
    _GG_AUTH_PROTOCOL* pProtocolHead; // +0x008
    std::array<char, 0x104> protocolDirectory; // +0x010
    std::uint32_t currentGameGuardVersion; // +0x114
    std::array<VersionCandidate, kCandidateCount> gameGuardCandidates; // +0x118
    std::uint32_t limitedGameGuardVersion; // +0x168
    bool gameGuardPromotionStarted; // +0x16c
    std::array<std::byte, 0x03> alignment16dTo170;
    std::array<VersionCandidate, kCandidateCount> protocolCandidates; // +0x170
    bool protocolPromotionStarted; // +0x1c0
    std::array<std::byte, 0x03> alignment1c1To1c4;
    std::uint32_t promotionPercent; // +0x1c4
    std::uint32_t promotionMinutes; // +0x1c8
    std::uint32_t gameGuardPromotionStart; // +0x1cc
    std::uint32_t protocolPromotionStart; // +0x1d0
    std::uint32_t useUpdateTimer; // +0x1d4
    std::uint32_t logOptions; // +0x1d8
    bool gameGuardAuthEnabled; // +0x1dc
    bool gameGuardAuthInitialized; // +0x1dd
    std::array<std::byte, 0x02> alignment1deTo1e0;
};


GameGuardGlobalState ggGlobal{};

BlowfishState blowfish{};
std::array<ClientIdentity, kClientMapWidth * kClientMapBuckets> clientIdentities{};

// Current critical sections 0x38816228, 0x38816200 and 0x388161d8.
std::optional<std::mutex> gameGuardVersionLock;
std::optional<std::mutex> protocolVersionLock;
std::optional<std::mutex> clientIdentityLock;

std::uint32_t Epoch32() noexcept { return static_cast<std::uint32_t>(std::time(nullptr)); }

std::uint64_t AbsoluteDifference(std::uint32_t left, std::uint32_t right) noexcept {
    const std::int64_t difference = static_cast<std::int64_t>(left) - static_cast<std::int64_t>(right);
    return static_cast<std::uint64_t>(difference < 0 ? -difference : difference);
}

void* OpenProtocolModule(const char* path) noexcept {
#if defined(_WIN32)
    return reinterpret_cast<void*>(::LoadLibraryA(path));
#else
    // LoadLibraryA resolves a bare protocol leaf from the process directory
    // and accepts the target's backslash separator. dlopen does neither.
    // Replace only that OS transport while retaining the exact configured
    // protocol filename and dynamic-module boundary.
    std::string nativePath = path == nullptr ? "" : path;
    std::replace(nativePath.begin(), nativePath.end(), '\\', '/');
    if (nativePath.find('/') == std::string::npos) {
        nativePath.insert(0, "./");
    }
    return ::dlopen(nativePath.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void CloseProtocolModule(void* module) noexcept {
    if (module == nullptr) {
        return;
    }
#if defined(_WIN32)
    ::FreeLibrary(reinterpret_cast<HMODULE>(module));
#else
    ::dlclose(module);
#endif
}

void* FindProtocolSymbol(void* module, const char* name) noexcept {
#if defined(_WIN32)
    const FARPROC symbol = ::GetProcAddress(reinterpret_cast<HMODULE>(module), name);
    void* result = nullptr;
    static_assert(sizeof(result) == sizeof(symbol));
    std::memcpy(&result, &symbol, sizeof(result));
    return result;
#else
    return ::dlsym(module, name);
#endif
}

template <typename Function>
Function ProtocolFunction(void* module, const char* name) noexcept {
    static_assert(std::is_pointer_v<Function>);
    void* symbol = FindProtocolSymbol(module, name);
    Function result = nullptr;
    static_assert(sizeof(result) == sizeof(symbol));
    std::memcpy(&result, &symbol, sizeof(result));
    return result;
}

std::uint32_t BlowfishF(const BlowfishState& state, std::uint32_t value) noexcept {
    return ((state.s[0][value >> 24U] + state.s[1][(value >> 16U) & 0xffU]) ^ state.s[2][(value >> 8U) & 0xffU]) +
        state.s[3][value & 0xffU];
}

void BlowfishEncipher(BlowfishState& state, std::uint32_t& left, std::uint32_t& right) noexcept {
    std::uint32_t leftValue = left ^ state.p[0];
    std::uint32_t rightValue = right;
    for (int round = 1; round < 17; round += 2) {
        rightValue ^= BlowfishF(state, leftValue) ^ state.p[round];
        leftValue ^= BlowfishF(state, rightValue) ^ state.p[round + 1];
    }
    right = leftValue;
    left = rightValue ^ state.p[17];
}

void BlowfishDecipher(BlowfishState& state, std::uint32_t& left, std::uint32_t& right) noexcept {
    std::uint32_t leftValue = left ^ state.p[17];
    std::uint32_t rightValue = right;
    for (int round = 16; round > 1; round -= 2) {
        rightValue ^= BlowfishF(state, leftValue) ^ state.p[round];
        leftValue ^= BlowfishF(state, rightValue) ^ state.p[round - 1];
    }
    left = rightValue ^ state.p[0];
    right = leftValue;
}

void InitializeBlowfish(const unsigned char* key, int keyLength) noexcept {
    std::copy(std::begin(BF_P_INIT), std::end(BF_P_INIT), blowfish.p);
    std::copy(std::begin(BF_S_INIT), std::end(BF_S_INIT), &blowfish.s[0][0]);

    int keyPosition = 0;
    for (std::uint32_t& entry : blowfish.p) {
        std::uint32_t keyValue = 0;
        for (int byte = 0; byte < 4; ++byte) {
            keyValue = (keyValue << 8U) | key[keyPosition];
            ++keyPosition;
            if (keyPosition >= keyLength) {
                keyPosition = 0;
            }
        }
        entry ^= keyValue;
    }

    std::uint32_t left = 0;
    std::uint32_t right = 0;
    for (int index = 0; index < 18; index += 2) {
        BlowfishEncipher(blowfish, left, right);
        blowfish.p[index] = left;
        blowfish.p[index + 1] = right;
    }
    for (auto& box : blowfish.s) {
        for (int index = 0; index < 256; index += 2) {
            BlowfishEncipher(blowfish, left, right);
            box[index] = left;
            box[index + 1] = right;
        }
    }
}

void EncryptPacket(_GG_AUTH_DATA& packet) noexcept {
    packet.dwIndex ^= UINT32_C(0xda93ab6f);
    packet.dwValue1 ^= UINT32_C(0xed3ae7db);
    BlowfishEncipher(blowfish, packet.dwIndex, packet.dwValue2);
    packet.dwValue3 ^= UINT32_C(0x6fd4ee8e);
    packet.dwValue2 ^= UINT32_C(0xe2f3d164);
    BlowfishEncipher(blowfish, packet.dwValue1, packet.dwValue3);
}

void DecryptPacket(_GG_AUTH_DATA& packet) noexcept {
    BlowfishDecipher(blowfish, packet.dwValue1, packet.dwValue3);
    packet.dwValue2 ^= UINT32_C(0xcc0c559f);
    packet.dwValue3 ^= UINT32_C(0x1ac6186c);
    BlowfishDecipher(blowfish, packet.dwIndex, packet.dwValue2);
    packet.dwIndex ^= UINT32_C(0xd253fc2e);
    packet.dwValue1 ^= UINT32_C(0x630fc331);
}

bool IsValidVersion(const _GG_VERSION& version) noexcept {
    return version.dwGGVer != 0U && version.wYear > 2002U && version.wYear < 2101U && version.wMonth != 0U &&
        version.wMonth < 13U && version.wDay != 0U && version.wDay < 32U && version.wNum != 0U && version.wNum < 100U;
}

bool IsValidPackedVersion(std::uint32_t version) noexcept {
    if (version == 0U) {
        return false;
    }
    const std::uint32_t year = version / 1000000U;
    const std::uint32_t month = (version / 10000U) % 100U;
    const std::uint32_t day = (version / 100U) % 100U;
    return year > 2002U && year < 2101U && month != 0U && month < 13U && day != 0U && day < 32U;
}

std::uint32_t GenerateServerKey() noexcept {
    std::uint32_t value = 0;
    for (int shift = 0; shift < 32; shift += 4) {
        value |= (static_cast<std::uint32_t>(std::rand()) & 0x0fU) << shift;
    }
    return value;
}

bool UpdateClientIdentity(
    bool add, std::uint32_t uniqueValue1, std::uint32_t uniqueValue2, std::uint32_t serverKey
) noexcept {
    if (uniqueValue1 == 0U || uniqueValue1 == UINT32_MAX) {
        return false;
    }

    const std::size_t bucket = uniqueValue1 % kClientMapBuckets;
    const std::size_t firstPosition = uniqueValue2 % kClientMapWidth;
    std::size_t tombstone = std::numeric_limits<std::size_t>::max();

    for (std::size_t distance = 0; distance < kClientMapWidth; ++distance) {
        const std::size_t position = (firstPosition + distance) % kClientMapWidth;
        const std::size_t index = bucket * kClientMapWidth + position;
        ClientIdentity& entry = clientIdentities[index];

        if (add) {
            if (entry.uniqueValue1 == uniqueValue1 && entry.uniqueValue2 == uniqueValue2) {
                return entry.serverKey == serverKey;
            }
            if (entry.uniqueValue1 == 0U) {
                const std::size_t selected = tombstone == std::numeric_limits<std::size_t>::max() ? index : tombstone;
                clientIdentities[selected] = {uniqueValue1, uniqueValue2, serverKey};
                return true;
            }
            if (entry.uniqueValue1 == UINT32_MAX && tombstone == std::numeric_limits<std::size_t>::max()) {
                tombstone = index;
            }
        } else {
            if (entry.uniqueValue1 == uniqueValue1 && entry.uniqueValue2 == uniqueValue2 && entry.serverKey == serverKey) {
                entry.uniqueValue1 = UINT32_MAX;
                return true;
            }
            if (entry.uniqueValue1 == 0U) {
                return true;
            }
        }
    }

    if (add && tombstone != std::numeric_limits<std::size_t>::max()) {
        clientIdentities[tombstone] = {uniqueValue1, uniqueValue2, serverKey};
    }
    return true;
}

std::size_t SelectReplacementCandidate(
    const std::array<VersionCandidate, kCandidateCount>& candidates, std::uint32_t now
) noexcept {
    std::size_t selected = std::numeric_limits<std::size_t>::max();
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (AbsoluteDifference(candidates[index].lastSeen, now) <= kCandidateExpirySeconds) {
            continue;
        }
        const std::size_t baseline = selected == std::numeric_limits<std::size_t>::max() ? 0 : selected;
        if (candidates[baseline].count > candidates[index].count) {
            selected = index;
        }
    }
    if (selected != std::numeric_limits<std::size_t>::max()) {
        return selected;
    }

    for (std::size_t index = 0; index < candidates.size(); ++index) {
        const std::size_t baseline = selected == std::numeric_limits<std::size_t>::max() ? 0 : selected;
        if (candidates[baseline].lastSeen > candidates[index].lastSeen) {
            selected = index;
        }
    }
    return selected == std::numeric_limits<std::size_t>::max() ? 0 : selected;
}

std::optional<std::uint32_t> CountVersionCandidate(
    std::array<VersionCandidate, kCandidateCount>& candidates,
    std::uint32_t version,
    std::uint32_t currentVersion,
    bool& promotionStarted,
    std::uint32_t& promotionStart
) noexcept {
    if (version > currentVersion && !promotionStarted) {
        promotionStarted = true;
        promotionStart = Epoch32();
    }

    const std::uint32_t now = Epoch32();
    for (VersionCandidate& candidate : candidates) {
        if (candidate.version != version || candidate.version == 0U ||
            AbsoluteDifference(candidate.lastSeen, now) >= kCandidateExpirySeconds) {
            continue;
        }

        ++candidate.count;
        candidate.lastSeen = now;
        if (candidate.count < ggGlobal.maximumActiveUsers ||
            static_cast<std::uint64_t>(now) - promotionStart <=
                static_cast<std::uint64_t>(ggGlobal.promotionMinutes) * 60U ||
            candidate.version <= currentVersion) {
            return std::nullopt;
        }

        std::uint32_t totalCount = 0;
        for (const VersionCandidate& item : candidates) {
            totalCount += item.count;
        }
        std::optional<std::uint32_t> acceptedVersion;
        for (const VersionCandidate& item : candidates) {
            if (item.version <= currentVersion || (ggGlobal.promotionPercent * totalCount) / 100U >= item.count) {
                continue;
            }
            if (!acceptedVersion || *acceptedVersion < item.version) {
                acceptedVersion = item.version;
            }
        }
        candidates.fill({});
        promotionStarted = false;
        return acceptedVersion;
    }

    for (VersionCandidate& candidate : candidates) {
        if (candidate.version != 0U) {
            continue;
        }
        candidate = {version, 1U, now, now};
        return std::nullopt;
    }

    const std::size_t replacement = SelectReplacementCandidate(candidates, now);
    candidates[replacement] = {version, 1U, now, now};
    return std::nullopt;
}

std::uint32_t UpdateGameGuardVersion(std::uint32_t version) noexcept {
    if (version == 0U || version - kInitialGameGuardVersion > kVersionRange) {
        return 0;
    }
    if (version > ggGlobal.limitedGameGuardVersion) {
        return 0;
    }
    if (version < ggGlobal.currentGameGuardVersion) {
        return kErrorProtocolMismatch;
    }

    const std::optional<std::uint32_t> acceptedVersion = CountVersionCandidate(
        ggGlobal.gameGuardCandidates, version, ggGlobal.currentGameGuardVersion, ggGlobal.gameGuardPromotionStarted,
        ggGlobal.gameGuardPromotionStart
    );
    if (acceptedVersion) {
        ggGlobal.currentGameGuardVersion = *acceptedVersion;
    }
    return 0;
}

std::uint32_t UpdateProtocolVersion(_GG_AUTH_PROTOCOL* protocol) noexcept {
    if (protocol == nullptr) {
        return 0;
    }
    const std::uint32_t version = protocol->dwVersion;
    if (version <= 0x1004fU || version >= 0x20001U) {
        return 0;
    }
    if (ggGlobal.pProtocolHead == nullptr || version < ggGlobal.pProtocolHead->dwVersion) {
        return kErrorProtocolMismatch;
    }

    const std::optional<std::uint32_t> acceptedVersion = CountVersionCandidate(
        ggGlobal.protocolCandidates, version, ggGlobal.pProtocolHead->dwVersion, ggGlobal.protocolPromotionStarted,
        ggGlobal.protocolPromotionStart
    );
    if (!acceptedVersion) {
        return 0;
    }

    _GG_AUTH_PROTOCOL* selected = protocol;
    if (selected->dwVersion != *acceptedVersion) {
        selected = ggGlobal.pProtocolHead;
        while (selected != nullptr && selected->dwVersion != *acceptedVersion) {
            selected = selected->pNext;
        }
        if (selected == nullptr) {
            return kErrorProtocolMismatch;
        }
    }

    if (selected->pNext == nullptr) {
        selected->pBefore->pNext = nullptr;
    } else {
        selected->pBefore->pNext = selected->pNext;
        selected->pNext->pBefore = selected->pBefore;
    }
    ggGlobal.pProtocolHead->pBefore = selected;
    ggGlobal.pProtocolHead->bDisuse = 1;
    selected->pBefore = nullptr;
    selected->pNext = ggGlobal.pProtocolHead;
    ggGlobal.pProtocolHead = selected;
    return 0;
}

std::string GameGuardPath(const char* leaf) {
    return std::string(ggGlobal.protocolDirectory.data()) + leaf;
}

std::uint32_t LoadAuthProtocol(const std::string& path, bool defaultProtocol) noexcept {
    using GetVersionFn = std::uint32_t (*)(std::uint32_t);

    auto* protocol = static_cast<_GG_AUTH_PROTOCOL*>(std::calloc(1, sizeof(_GG_AUTH_PROTOCOL)));
    if (protocol == nullptr) {
        return kErrorAllocation;
    }

    protocol->hModule = OpenProtocolModule(path.c_str());
    if (protocol->hModule == nullptr) {
        std::free(protocol);
        return kErrorModule;
    }

    const GetVersionFn getVersion = ProtocolFunction<GetVersionFn>(protocol->hModule, "PrtcGetVersion");
    protocol->PrtcGetAuthQuery = ProtocolFunction<_GG_AUTH_PROTOCOL::GetAuthQueryFn>(protocol->hModule, "PrtcGetAuthQuery");
    protocol->PrtcCheckAuthAnswer =
        ProtocolFunction<_GG_AUTH_PROTOCOL::CheckAuthAnswerFn>(protocol->hModule, "PrtcCheckAuthAnswer");
    protocol->PrtcEncryptQuery = ProtocolFunction<_GG_AUTH_PROTOCOL::CryptFn>(protocol->hModule, "PrtcEncryptQuery");
    protocol->PrtcDecryptAnswer = ProtocolFunction<_GG_AUTH_PROTOCOL::CryptFn>(protocol->hModule, "PrtcDecryptAnswer");
    protocol->PrtcUpdateTimer = ProtocolFunction<_GG_AUTH_PROTOCOL::UpdateTimerFn>(protocol->hModule, "PrtcUpdateTimer");
    protocol->PrtcLoopAuth = ProtocolFunction<_GG_AUTH_PROTOCOL::LoopAuthFn>(protocol->hModule, "PrtcLoopAuth");
    protocol->PrtcGetInformation =
        ProtocolFunction<_GG_AUTH_PROTOCOL::GetInformationFn>(protocol->hModule, "PrtcGetInformation");
    protocol->PrtcCheckValidAnswer =
        ProtocolFunction<_GG_AUTH_PROTOCOL::CheckValidAnswerFn>(protocol->hModule, "PrtcCheckValidAnswer");

    if (getVersion == nullptr || protocol->PrtcGetAuthQuery == nullptr || protocol->PrtcCheckAuthAnswer == nullptr ||
        protocol->PrtcEncryptQuery == nullptr || protocol->PrtcDecryptAnswer == nullptr ||
        protocol->PrtcUpdateTimer == nullptr || protocol->PrtcLoopAuth == nullptr ||
        protocol->PrtcGetInformation == nullptr || protocol->PrtcCheckValidAnswer == nullptr) {
        CloseProtocolModule(protocol->hModule);
        std::free(protocol);
        return kErrorExport;
    }

    // Current 0x00ce38a6 loads ggGlobal.useUpdateTimer into ECX before the
    // indirect call. Protocol 70 tests that argument to decide whether it
    // must install its own five-minute updater.
    protocol->dwVersion = getVersion(ggGlobal.useUpdateTimer);
    protocol->nLastTime = static_cast<std::int32_t>(Epoch32());
    if (defaultProtocol) {
        protocol->dwOption |= 3U;
        ggGlobal.pProtocolHead = protocol;
    } else {
        _GG_AUTH_PROTOCOL* tail = ggGlobal.pProtocolHead;
        while (tail != nullptr && tail->pNext != nullptr) {
            tail = tail->pNext;
        }
        if (tail != nullptr) {
            tail->pNext = protocol;
            protocol->pBefore = tail;
        }
    }

    {
        std::lock_guard<std::mutex> guard(*protocolVersionLock);
        ++ggGlobal.protocolCount;
    }
    return 0;
}

} // namespace

extern "C" std::uint32_t InitGameguardAuth(
    const char* path, std::uint32_t maximumUsers, std::uint32_t updateTimer, std::uint32_t options
) {
    std::array<int, 100> protocolVersions{};
    protocolVersions[0] = -1;

    ggGlobal.protocolCount = 0;
    ggGlobal.pProtocolHead = nullptr;
    ggGlobal.protocolDirectory[0] = '\0';
    ggGlobal.gameGuardPromotionStarted = false;
    ggGlobal.protocolPromotionStarted = false;
    ggGlobal.gameGuardPromotionStart = 0;
    ggGlobal.protocolPromotionStart = 0;
    ggGlobal.currentGameGuardVersion = kInitialGameGuardVersion;
    ggGlobal.limitedGameGuardVersion = kLimitedGameGuardVersion;
    ggGlobal.promotionPercent = 50;
    ggGlobal.promotionMinutes = 30;
    ggGlobal.gameGuardAuthEnabled = true;
    ggGlobal.maximumActiveUsers = maximumUsers;
    ggGlobal.useUpdateTimer = updateTimer;
    ggGlobal.logOptions = options;
    ggGlobal.gameGuardCandidates.fill({});
    ggGlobal.protocolCandidates.fill({});

    if (path != nullptr && path[0] != '\0') {
        const std::size_t length = std::strlen(path);
        // Source bug: the configured path was copied without a bound.
        auto& directory = ggGlobal.protocolDirectory;
        if (path[length - 1] == '\\') {
            std::snprintf(directory.data(), directory.size(), "%s", path);
        } else {
            std::snprintf(directory.data(), directory.size(), "%s%c", path, '\\');
        }
    }

    // The current initializer clears only the occupied marker in each
    // 12-byte open-addressed entry. The other two lanes remain ignored
    // until that marker is populated again.
    for (ClientIdentity& identity : clientIdentities) {
        identity.uniqueValue1 = 0;
    }

    gameGuardVersionLock.emplace();
    protocolVersionLock.emplace();
    clientIdentityLock.emplace();
    ggGlobal.gameGuardAuthInitialized = true;

    const std::string configPath = GameGuardPath("csauth2.cfg");
    FILE* config = std::fopen(configPath.c_str(), "r");
    if (config == nullptr) {
        CleanupGameguardAuth();
        return kErrorConfig;
    }

    std::fseek(config, 0, SEEK_SET);
    int protocolVersionCount = 0;
    do {
        const int result = std::fscanf(config, "%d", &protocolVersions[static_cast<std::size_t>(protocolVersionCount)]);
        if (result == EOF) {
            break;
        }
        ++protocolVersionCount;
    } while (protocolVersionCount < static_cast<int>(protocolVersions.size()));

    if (protocolVersionCount < 1) {
        CleanupGameguardAuth();
        std::fclose(config);
        return kErrorConfig;
    }

    for (int index = 0; index < protocolVersionCount; ++index) {
        char leaf[64]{};
        std::snprintf(leaf, sizeof(leaf), "ggauth%d.dll", protocolVersions[static_cast<std::size_t>(index)]);
        const std::uint32_t result = LoadAuthProtocol(GameGuardPath(leaf), index == 0);
        if (result != 0U) {
            CleanupGameguardAuth();
            std::fclose(config);
            return result;
        }
    }
    std::fclose(config);

    const std::string versionPath = GameGuardPath("csver.cfg");
    FILE* versionConfig = std::fopen(versionPath.c_str(), "r");
    if (versionConfig != nullptr) {
        std::fseek(versionConfig, 0, SEEK_SET);
        int versionLimit = 0;
        if (std::fscanf(versionConfig, "%d", &versionLimit) != EOF && versionLimit != 0 &&
            static_cast<std::uint32_t>(versionLimit) - kInitialGameGuardVersion <= kVersionRange) {
            ggGlobal.limitedGameGuardVersion = static_cast<std::uint32_t>(versionLimit);
        }
        std::fclose(versionConfig);
    }

    static constexpr unsigned char kBlowfishKey[] = "y].ZdaXlMls";
    InitializeBlowfish(kBlowfishKey, 0x0b);
    return 0;
}

extern "C" void CleanupGameguardAuth() {
#if !defined(_WIN32)
    // The target leaves this flag set after deleting its critical sections.
    // Native mutexes cannot be used after optional::reset, including when
    // startup continues after a missing config or failed protocol load.
    ggGlobal.gameGuardAuthInitialized = false;
#endif
    _GG_AUTH_PROTOCOL* protocol = ggGlobal.pProtocolHead;
    while (protocol != nullptr) {
        ggGlobal.pProtocolHead = protocol->pNext;
        CloseProtocolModule(protocol->hModule);
        std::free(protocol);
        protocol = ggGlobal.pProtocolHead;
    }

    gameGuardVersionLock.reset();
    protocolVersionLock.reset();
    clientIdentityLock.reset();
}

CCSAuth2::CCSAuth2() {
#if !defined(_WIN32)
    // Keep an object constructed while the native library is unavailable
    // inactive even if library initialization is retried later.
    m_dwUserFlag = 0;
#endif
    if (!ggGlobal.gameGuardAuthInitialized) {
        return;
    }

    m_pProtocol = ggGlobal.pProtocolHead;
    m_AuthQuery = {};
    m_AuthAnswer = {};
    m_AuthQueryTmp = {};
    m_dwUserFlag = 0;
    m_bAuth = true;
    m_bPrtcRef = 0;
    m_GGVer = {};

    // Present in the current constructor after m_dwUserFlag has just been
    // cleared. So this removal branch is dead.
    if ((m_dwUserFlag & kUserInitialized) != 0U) {
        std::lock_guard<std::mutex> guard(*clientIdentityLock);
        UpdateClientIdentity(false, m_dwUniqValue1, m_dwUniqValue2, m_dwServerKey);
    }

    m_dwUserFlag |= kUserInitialized;
    m_bAllowOldVersion = false;
    m_nSequenceNum = UINT32_MAX;
    m_dwServerKey = GenerateServerKey();
    m_dwLastValue4 = 0;
    m_byLastLoop1 = 0;
    m_byLastLoop2 = 0;
    m_dwUniqValue1 = 0;
    m_dwUniqValue2 = 0;
    std::memset(m_dwLoop1AuthArray, 0, sizeof(m_dwLoop1AuthArray));
    std::memset(m_dwLoop2AuthArray, 0, sizeof(m_dwLoop2AuthArray));
    m_pNext = nullptr;
    m_dwLoop1AuthArray[0] = m_dwUniqValue1;
    m_dwLoop2AuthArray[0] = m_dwServerKey;
}

CCSAuth2::~CCSAuth2() { Close(); }

void CCSAuth2::Close() {
    if (!ggGlobal.gameGuardAuthInitialized || (m_dwUserFlag & kUserInitialized) == 0U) {
        return;
    }

    {
        std::lock_guard<std::mutex> guard(*clientIdentityLock);
        UpdateClientIdentity(false, m_dwUniqValue1, m_dwUniqValue2, m_dwServerKey);
    }

    // Current 0x00ce53e0 only observes the new-protocol/refcount condition.
    // it performs no decrement in either branch.
    m_dwUserFlag &= ~kUserInitialized;
    m_bPrtcRef = 0;
}

std::uint32_t CCSAuth2::GetAuthQuery() {
#if !defined(_WIN32)
    if (!ggGlobal.gameGuardAuthInitialized) {
        return kErrorInactive;
    }
#endif
    if ((m_dwUserFlag & kUserInitialized) == 0U) {
        return kErrorInactive;
    }
    if (m_pProtocol == nullptr) {
        return kErrorInactive;
    }
    if (!m_bAuth || !ggGlobal.gameGuardAuthEnabled) {
        return 0;
    }

    if (m_bPrtcRef == 0U) {
        ++m_pProtocol->dwReferCount;
        if ((ggGlobal.pProtocolHead->dwOption & 2U) == 0U &&
            ggGlobal.maximumActiveUsers < ggGlobal.pProtocolHead->dwReferCount) {
            ggGlobal.pProtocolHead->dwOption |= 2U;
        }
        m_bPrtcRef = 1;
    }

    if ((m_dwUserFlag & kUserCheckVersion) != 0U && !IsValidVersion(m_GGVer)) {
        return kErrorInvalidVersion;
    }

    ++m_nSequenceNum;
    std::uint32_t result = m_pProtocol->PrtcGetAuthQuery(&m_dwUserFlag, &m_GGVer, this, m_nSequenceNum, m_dwServerKey);
    if (result == 0U) {
        m_AuthQueryTmp = m_AuthQuery;
        result = m_pProtocol->PrtcEncryptQuery(&m_AuthQuery);
        EncryptPacket(m_AuthQuery);
    }
    return result;
}

std::uint32_t CCSAuth2::CheckAuthAnswer() {
#if !defined(_WIN32)
    if (!ggGlobal.gameGuardAuthInitialized) {
        return kErrorInactive;
    }
#endif
    std::int32_t validAnswer = 0;
    if ((m_dwUserFlag & kUserInitialized) == 0U) {
        return kErrorInactive;
    }
    if ((m_dwUserFlag & kUserHasAnswer) == 0U) {
        return 0;
    }

    m_dwUserFlag &= ~kUserHasAnswer;
    if (m_pProtocol == nullptr) {
        return kErrorInactive;
    }
    if (!m_bAuth || !ggGlobal.gameGuardAuthEnabled) {
        return 0;
    }

    _GG_AUTH_DATA answer = m_AuthAnswer;
    DecryptPacket(answer);
    m_pProtocol->PrtcDecryptAnswer(&answer);

    std::uint32_t result = m_pProtocol->PrtcCheckValidAnswer(&answer, &validAnswer);
    if (result != 0U) {
        return result;
    }

    const std::uint32_t protocolChecker = m_pProtocol->PrtcGetInformation(1U, &answer);
    const std::uint32_t authenticationStep = m_pProtocol->PrtcGetInformation(0U, &answer);
    const std::uint32_t answerValue = m_pProtocol->PrtcGetInformation(4U, &answer);

    if (m_pProtocol->dwVersion < protocolChecker) {
        if (authenticationStep != 0U) {
            return kErrorProtocolMismatch;
        }

        m_dwUserFlag &= ~kUserNewProtocol;
        _GG_AUTH_PROTOCOL* protocol = ggGlobal.pProtocolHead;
        while (protocol != nullptr) {
            if (protocol->dwVersion == protocolChecker && protocol->bDisuse == 0) {
                --m_pProtocol->dwReferCount;
                m_dwUserFlag |= kUserNewProtocol;
                m_pProtocol = protocol;
                break;
            }
            protocol = protocol->pNext;
        }
        if ((m_dwUserFlag & kUserNewProtocol) == 0U) {
            return kErrorProtocolMismatch;
        }
    } else if (protocolChecker < m_pProtocol->dwVersion) {
        return kErrorProtocolMismatch;
    } else if (authenticationStep != 0U) {
        // On follow-up authentication packets the current code keeps the
        // version structure established by the first packet.
        goto validate_version_structure;
    }

    if (!IsValidPackedVersion(answerValue)) {
        return kErrorInvalidVersion;
    }
    if (!m_bAllowOldVersion && answerValue < ggGlobal.currentGameGuardVersion) {
        return kErrorInvalidVersion;
    }
    if (ggGlobal.limitedGameGuardVersion < answerValue) {
        return kErrorInvalidVersion;
    }

    m_GGVer.dwGGVer = answerValue;
    m_GGVer.wYear = static_cast<std::uint16_t>(answerValue / 1000000U);
    m_GGVer.wMonth = static_cast<std::uint16_t>((answerValue / 10000U) % 100U);
    m_GGVer.wDay = static_cast<std::uint16_t>((answerValue / 100U) % 100U);
    m_GGVer.wNum = static_cast<std::uint16_t>(answerValue % 100U);

validate_version_structure:
    if (!IsValidVersion(m_GGVer)) {
        return kErrorInvalidVersion;
    }

    std::uint32_t callbackLastValue = m_dwLastValue4;
    result = m_pProtocol->PrtcCheckAuthAnswer(
        &m_dwUserFlag, &m_GGVer, &m_AuthQueryTmp, &answer, m_nSequenceNum, &callbackLastValue
    );

    // Current 0x00ce6060 ignores callbackLastValue and copies the fourth
    // decrypted answer lane instead.
    m_dwLastValue4 = answer.dwValue3;
    if (result != 0U) {
        return result;
    }

    if (authenticationStep == 1U) {
        m_dwUniqValue1 = answerValue;
        m_dwLoop1AuthArray[0] = answerValue;
    } else if (authenticationStep == 2U) {
        m_dwUniqValue2 = answerValue;
        std::lock_guard<std::mutex> guard(*clientIdentityLock);
        if (!UpdateClientIdentity(true, m_dwUniqValue1, m_dwUniqValue2, m_dwServerKey)) {
            return kErrorDuplicateClient;
        }
    }

    const std::uint8_t previousLoop1 = m_byLastLoop1;
    const std::uint8_t previousLoop2 = m_byLastLoop2;
    result = m_pProtocol->PrtcLoopAuth(
        &m_byLastLoop1, m_dwLoop1AuthArray, &m_byLastLoop2, m_dwLoop2AuthArray, &answer, m_nSequenceNum
    );
    if (result != 0U) {
        const std::uint32_t loop1 = m_pProtocol->PrtcGetInformation(5U, &answer);
        const std::uint32_t loop2 = m_pProtocol->PrtcGetInformation(6U, &answer);
        const std::uint32_t auth = m_pProtocol->PrtcGetInformation(7U, &answer);
        (void)previousLoop1;
        (void)previousLoop2;
        (void)loop1;
        (void)loop2;
        (void)auth;
        return result;
    }

    if (m_bAllowOldVersion) {
        return 0;
    }
    if (authenticationStep != 0U) {
        if ((m_dwUserFlag & (kUserNewGameGuard | kUserNewProtocol)) == 0U) {
            return 0;
        }
        if ((m_dwUserFlag & kUserNewGameGuard) != 0U) {
            m_dwUserFlag &= ~kUserNewGameGuard;
            std::lock_guard<std::mutex> guard(*gameGuardVersionLock);
            UpdateGameGuardVersion(m_GGVer.dwGGVer);
        }
        if ((m_dwUserFlag & kUserNewProtocol) != 0U) {
            m_dwUserFlag &= ~kUserNewProtocol;
            std::lock_guard<std::mutex> guard(*protocolVersionLock);
            UpdateProtocolVersion(m_pProtocol);
        }
        return 0;
    }

    if (!ggGlobal.gameGuardPromotionStarted && m_GGVer.dwGGVer <= ggGlobal.currentGameGuardVersion) {
        return 0;
    }
    m_dwUserFlag |= kUserNewGameGuard;
    return 0;
}
