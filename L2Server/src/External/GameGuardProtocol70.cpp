#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#define GGAUTH70_EXPORT extern "C" __declspec(dllexport)
#else
#define GGAUTH70_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {

struct AuthData final {
    std::uint32_t index;
    std::uint32_t value1;
    std::uint32_t value2;
    std::uint32_t value3;
};

struct Version final {
    std::uint32_t gameGuardVersion;
    std::uint16_t year;
    std::uint16_t month;
    std::uint16_t day;
    std::uint16_t number;
};


#include "GameGuardProtocol70Tables.inc"

constexpr std::uint32_t kA018 = 0xa85b70c3U;
constexpr std::uint32_t kA028 = 0x37ec7c64U;
constexpr std::uint32_t kA038 = 0xa6cc2510U;
constexpr std::uint32_t kA044 = 0x9d40909bU;
constexpr std::uint32_t kA068 = 0xe450789fU;
constexpr std::uint32_t kA074 = 0x1a339554U;
constexpr std::uint32_t kA080 = 0x54a31038U;
constexpr std::uint32_t kA0b0 = 0x6ea0e0b2U;
constexpr std::uint32_t kA0cc = 0xdf774d66U;
constexpr std::uint32_t kA104 = 0xffdc99f8U;
constexpr std::uint32_t kA11c = 0x1bd031e9U;
constexpr std::uint32_t kA148 = 0x6a01e805U;
constexpr std::uint32_t kA14c = 0x1f000059U;
constexpr std::uint32_t kB284 = 0x6a1ce802U;
constexpr std::uint32_t kB288 = 0xffffff59U;
constexpr std::uint32_t kB290 = 0x0085c075U;
constexpr std::uint32_t kB298 = 0x6a1be8b3U;

// Current 0x18001c640 and 0x18001c660 are process globals instead of
// function locals. Relaxed lock-free carriers keep the observable
// cross-call overwrite contract without introducing a C++ data race.
std::atomic<std::uint32_t> g_algorithmScratch{0};
std::atomic<std::uint16_t> g_crcBitCount{0};
std::atomic<std::uint16_t> g_utcSlot{0};
thread_local std::uint32_t g_localScratch = 0;
thread_local std::uint32_t g_holdRand = 1;
const char* g_gameMonPath = nullptr;

std::uint32_t Scratch() noexcept {
    // Every target algorithm publishes its register result to 0x18001c640,
    // then continues with that register instead of reloading the global.
    return g_localScratch;
}

void SetScratch(const std::uint32_t value) noexcept {
    g_localScratch = value;
    g_algorithmScratch.store(value, std::memory_order_relaxed);
}

std::uint32_t AlgorithmConstant(const Version& version, const std::size_t column, const std::uint32_t salt) noexcept {
    const std::uint32_t row =
        (static_cast<std::uint32_t>(version.number) * static_cast<std::uint32_t>(version.day) + salt) % 10U;
    return kAlgorithmConstants[row][column];
}

std::uint32_t ConstantAnswer(const std::uint32_t first, const std::uint32_t second) noexcept {
    return ((kA0b0 + first) ^ kB288) + kA11c + second + kA074;
}

void ComputeExpected(const AuthData& query, AuthData& expected, const Version& version) noexcept {
    expected = {};
    switch (static_cast<std::uint8_t>(query.index >> 24U)) {
        case 1: {
            std::uint32_t value2 = query.value1 ^ kA080;
            const std::uint32_t value1 = query.value2 ^ 0xfbb6615fU;
            const std::uint32_t value3 = value2 + 0x4e7ee885U;
            value2 = (kA044 ^ query.value3 ^ (value1 + 0x96ee06b7U) ^ value2) + AlgorithmConstant(version, 95, 12);
            SetScratch((value1 + 0x81902f89U) ^ value2);
            expected.value3 = (Scratch() ^ value3) + kB284 + value2 + value3;
            break;
        }
        case 2:
            expected.value3 = ConstantAnswer(0x4ff1ee4bU, 0xa11f94e0U);
            break;
        case 3:
            SetScratch(AlgorithmConstant(version, 70, 87) + (query.value2 ^ (kA018 + 0xd7fe3fd2U)) + kA068);
            expected.value3 = (Scratch() ^ 0x13b034f4U) + ((query.value3 + 0x3e42dadcU) ^ kA148) + kA018 + 0xd7fe3fd2U;
            break;
        case 4:
            expected.value3 = ConstantAnswer(0xe274fad8U, 0x304ecff3U);
            break;
        case 5:
            expected.value3 = ConstantAnswer(0xdddd7978U, 0xebfdf1a1U);
            break;
        case 6: {
            const std::uint32_t value = query.value1 - 0x43a1a2a8U;
            SetScratch(kA0b0 + value);
            expected.value3 = AlgorithmConstant(version, 62, 79) + (kA14c ^ (query.value2 - 0x3da226ebU + kA038)) +
                Scratch() + 0x5d42865fU + value;
            break;
        }
        case 7: {
            const std::uint32_t base = kA018 ^ 0xde696108U;
            SetScratch(AlgorithmConstant(version, 44, 61) - query.value2 + kA068 + base);
            expected.value3 = (Scratch() ^ 0x255a87f8U) + ((query.value3 + 0x3e0c4101U) ^ kA148) + base;
            break;
        }
        case 8: {
            std::uint32_t value2 = query.value1 ^ kA080;
            const std::uint32_t value1 = query.value2 ^ 0x08d032a1U;
            const std::uint32_t value3 = value2 + 0x0bf03c27U;
            value2 = (kA044 ^ (value1 + 0x2780b443U) ^ value2 ^ query.value3) + AlgorithmConstant(version, 40, 57);
            SetScratch((value1 + 0xaf87ca92U) ^ value2);
            expected.value3 = (Scratch() ^ value3) + kB284 + value2 + value3;
            break;
        }
        case 9: {
            const std::uint32_t value = query.value1 + 0x3cfccba7U;
            SetScratch(kA0b0 + value);
            expected.value3 = AlgorithmConstant(version, 27, 44) + (kA14c ^ (query.value2 - 0x46131d38U + kA038)) +
                Scratch() + 0x7ec4dfb4U + value;
            break;
        }
        case 10: {
            const std::uint32_t value1 = query.value3 ^ 0x64c341c9U;
            const std::uint32_t value2 = query.value1 ^ kA104 ^ 0xebfeb8a4U;
            SetScratch(value1 + 0x1af440fbU + value2);
            expected.value3 =
                AlgorithmConstant(version, 61, 78) + (Scratch() ^ 0x64c341c9U) + kB298 + value1 + 0x44eU + value2;
            break;
        }
        case 11:
            SetScratch(AlgorithmConstant(version, 90, 7) + (query.value2 ^ (kA018 + 0x636f53baU)) + kA068);
            expected.value3 = (Scratch() ^ 0x95e8d587U) + (query.value3 ^ kA148 ^ 0x23bffe0bU) + kA018 + 0x636f53baU;
            break;
        case 12: {
            std::uint32_t value2 = query.value2 - 0x319b7bb4U + kA0cc;
            const std::uint32_t value1 = (query.value3 ^ 0xe6fd42f5U) + 0x36eefdc1U;
            SetScratch(kA028 + value1);
            value2 ^= kB290;
            expected.value3 = (kA038 ^ value2) + value2 +
                ((AlgorithmConstant(version, 63, 80) + value1) ^ ConstantAnswer(0x37d1701dU, 0x560b19d0U));
            break;
        }
        case 13:
            SetScratch(AlgorithmConstant(version, 60, 77) - query.value2 + kA068 + kA018 - 0x48ecd2bbU);
            expected.value3 = (Scratch() ^ 0x9bcbc35aU) + ((query.value3 + 0x9ab3db46U) ^ kA148) + kA018 - 0x48ecd2bbU;
            break;
        case 14: {
            const std::uint32_t value = query.value1 - 0x700d5ebdU;
            SetScratch(kA0b0 + value);
            expected.value3 = AlgorithmConstant(version, 85, 2) + (kA14c ^ (query.value2 - 0x42a05d2aU + kA038)) +
                Scratch() + 0x51e81829U + value;
            break;
        }
        case 15: {
            std::uint32_t value2 = query.value1 ^ kA080;
            const std::uint32_t value1 = (query.value2 ^ 0x7d0c4db3U) + 0x112eb835U;
            const std::uint32_t value3 = value2 + 0x87848204U;
            value2 = (kA044 ^ (value2 - value1) ^ query.value3) + AlgorithmConstant(version, 61, 78);
            SetScratch(value2 ^ value1 ^ 0x015d20b4U);
            expected.value3 = (Scratch() ^ value3) + kB284 + value2 + value3;
            break;
        }
        case 16: {
            const std::uint32_t value = query.value1 - 0x0bdc759fU;
            SetScratch(kA0b0 + value);
            expected.value3 = AlgorithmConstant(version, 53, 70) + (kA14c ^ (query.value2 + 0x2f38ab56U + kA038)) +
                Scratch() - 0x4c9c643bU + value;
            break;
        }
        case 17: {
            const std::uint32_t value = query.value1 - 0x4c123474U;
            SetScratch(kA0b0 + value);
            expected.value3 = AlgorithmConstant(version, 11, 28) + (kA14c ^ (query.value2 - 0x773e22e2U + kA038)) +
                Scratch() - 0x0da9f064U + value;
            break;
        }
        case 18: {
            const std::uint32_t value = query.value1 - 0x0ac254f3U;
            SetScratch(kA0b0 + value);
            expected.value3 = AlgorithmConstant(version, 76, 93) + (kA14c ^ (query.value2 - 0x20ab09f9U + kA038)) +
                Scratch() - 0x13ff019aU + value;
            break;
        }
        case 19: {
            const std::uint32_t value2 = kA080 + query.value1;
            SetScratch(query.value2 + 0x64d9ce5fU);
            const std::uint32_t value1 = ((value2 - Scratch()) ^ query.value3) + AlgorithmConstant(version, 8, 25) + kA044;
            SetScratch(value1 + 0x4acf92f6U + Scratch());
            expected.value3 = kB284 + (value2 - 0x7888fff0U) * 2U + Scratch() + value1;
            break;
        }
        case 20: {
            std::uint32_t value2 = query.value1 ^ kA080;
            const std::uint32_t value1 = query.value2 ^ 0x0772dc4bU;
            const std::uint32_t value3 = value2 ^ 0x8987dbf7U;
            value2 = (kA044 ^ query.value3 ^ (value1 + 0x91aefaa9U) ^ value2) + AlgorithmConstant(version, 22, 39);
            SetScratch((value1 + 0x7adfeb8dU) ^ value2);
            expected.value3 = (Scratch() ^ value3) + kB284 + value2 + value3;
            break;
        }
        case 21: {
            const std::uint32_t value1 = (query.value3 + 0xcd209782U) ^ 0x0a81da4cU;
            std::uint32_t value2 = (query.value2 ^ 0x6cbda5b0U) + kA0cc;
            SetScratch(kA028 + value1);
            value2 ^= kB290;
            expected.value3 = (kA038 ^ value2) + value2 +
                ((AlgorithmConstant(version, 57, 74) + value1) ^ ConstantAnswer(0xdddd7978U, 0xebfdf1a1U));
            break;
        }
        case 22: {
            const std::uint32_t base = kA018 ^ 0x6a27ab7dU;
            SetScratch(AlgorithmConstant(version, 30, 47) + query.value2 + kA068 + base);
            expected.value3 = Scratch() + ((query.value3 + 0x17634783U) ^ kA148) - 0x55392b4eU + base;
            break;
        }
        case 23: {
            const std::uint32_t value1 = query.value2 + 0xb2212174U;
            const std::uint32_t value3 = kA080 + query.value1;
            const std::uint32_t value2 = (query.value3 ^ value1 ^ value3) + AlgorithmConstant(version, 91, 8) + kA044;
            SetScratch(value2 - 0x1718f435U + value1);
            expected.value3 = kB284 + (value3 + 0x9132b72fU) * 2U + Scratch() + value2;
            break;
        }
        case 24: {
            const std::uint32_t value2 = query.value2 + 0x22289b2aU;
            const std::uint32_t value1 =
                (query.value3 ^ value2 ^ (query.value1 - kA080)) + (AlgorithmConstant(version, 59, 76) - kA044);
            SetScratch((value2 ^ 0x33c8c315U) - value1);
            expected.value3 = kB284 + Scratch() + value1;
            break;
        }
        case 25: {
            const std::uint32_t base = kA018 ^ 0x370d3d46U;
            SetScratch(AlgorithmConstant(version, 2, 19) - query.value2 + kA068 + base);
            expected.value3 = Scratch() + (query.value3 ^ kA148 ^ 0x2600dde6U) - 0x3d4473cfU + base;
            break;
        }
        case 26: {
            const std::uint32_t value = query.value1 ^ 0xe8f62d1fU;
            SetScratch(kA0b0 + value);
            expected.value3 = (Scratch() ^ 0xf36e1273U) + value + AlgorithmConstant(version, 42, 59) +
                (kA14c ^ ((query.value2 ^ 0x03b08b74U) + kA038));
            break;
        }
        case 27: {
            const std::uint32_t value2 = query.value1 - kA104;
            const std::uint32_t value1 = query.value3 ^ 0x008f251aU;
            SetScratch(value1 + 0xb8b9718cU + value2);
            expected.value3 =
                AlgorithmConstant(version, 93, 10) + (Scratch() ^ 0x008f251aU) + kB298 + value1 + 0x07a2066dU + value2;
            break;
        }
        case 28:
            SetScratch(AlgorithmConstant(version, 29, 46) + (query.value2 ^ (kA018 + 0x7cabc29eU)) + kA068);
            expected.value3 = Scratch() + ((query.value3 + 0x77d72a5aU) ^ kA148) + 0x02a3a40eU + kA018 + 0x7cabc29eU;
            break;
        case 29: {
            const std::uint32_t value2 = kA080 + query.value1;
            const std::uint32_t value3 = query.value2 + 0x3c1e8fcbU;
            const std::uint32_t value1 = ((value3 + value2) ^ query.value3) + AlgorithmConstant(version, 94, 11) + kA044;
            SetScratch((value3 ^ 0x269e8a65U) + value1);
            expected.value3 = kB284 + (value2 - 0x65fcbae6U) * 2U + Scratch() + value1;
            break;
        }
        case 30: {
            const std::uint32_t value1 = query.value2 - 0x26425c57U;
            const std::uint32_t value2 =
                (((query.value1 - kA080) - value1) ^ query.value3) + (AlgorithmConstant(version, 20, 37) - kA044);
            SetScratch(value1 - value2 - 0x0faa0cc4U);
            expected.value3 = kB284 + Scratch() + value2;
            break;
        }
        case 31: {
            const std::uint32_t value = query.value1 - kA104;
            SetScratch(query.value3 + 0x675a28faU + value);
            expected.value3 = AlgorithmConstant(version, 69, 86) + kB298 + Scratch() + query.value3 + 0x32a11e62U + value;
            break;
        }
        case 32: {
            const std::uint32_t value = query.value1 + 0x4905f430U;
            SetScratch(kA0b0 + value);
            expected.value3 = AlgorithmConstant(version, 74, 91) + (kA14c ^ (query.value2 + 0x0e645533U + kA038)) +
                Scratch() + 0x5c57eebcU + value;
            break;
        }
        case 33: {
            const std::uint32_t value1 = query.value3 ^ 0x48db880dU;
            const std::uint32_t value2 = query.value1 ^ kA104 ^ 0xbd3482d4U;
            SetScratch(value2 - value1 + 0x2d3e495cU);
            expected.value3 =
                AlgorithmConstant(version, 1, 18) + (Scratch() ^ 0x48db880dU) + kB298 + value1 + 0x44eU + value2;
            break;
        }
        case 34: {
            std::uint32_t value2 = query.value2 - 0x6c875ae1U + kA0cc;
            const std::uint32_t value1 = query.value3 ^ 0x98872066U;
            SetScratch(kA028 + value1);
            value2 ^= kB290;
            expected.value3 = (kA038 ^ value2) + value2 +
                ((AlgorithmConstant(version, 0, 17) + value1) ^ ConstantAnswer(0x37d1701dU, 0x560b19d0U));
            break;
        }
        case 35: {
            std::uint32_t value2 = query.value1 ^ kA080;
            const std::uint32_t value1 = (query.value2 ^ 0x95742b6aU) + 0x22aad4d6U;
            const std::uint32_t value3 = value2 + 0xad920996U;
            value2 = (kA044 ^ (value1 + value2) ^ query.value3) + AlgorithmConstant(version, 74, 91);
            SetScratch(value2 ^ value1 ^ 0x3554d6a1U);
            expected.value3 = (Scratch() ^ value3) + kB284 + value2 + value3;
            break;
        }
        case 36: {
            const std::uint32_t value = query.value1 + 0x34ff0159U;
            SetScratch(kA0b0 + value);
            expected.value3 = AlgorithmConstant(version, 13, 30) + (kA14c ^ (query.value2 + 0x11b2eefaU + kA038)) +
                Scratch() + 0x32f8d58fU + value;
            break;
        }
        case 37: {
            const std::uint32_t value2 = query.value1 - kA104;
            const std::uint32_t value1 = query.value3 ^ 0xe9b03451U;
            SetScratch(value2 - value1 + 0x6dd9ad46U);
            expected.value3 =
                AlgorithmConstant(version, 40, 57) + (Scratch() ^ 0xe9b03451U) + kB298 + value1 - 0x7fa9572dU + value2;
            break;
        }
        case 38: {
            const std::uint32_t value2 = query.value2 - 0x026925f3U;
            const std::uint32_t value1 =
                ((value2 + (query.value1 - kA080)) ^ query.value3) + (AlgorithmConstant(version, 61, 78) - kA044);
            SetScratch(value2 - value1 - 0x6228b8dcU);
            expected.value3 = kB284 + Scratch() + value1;
            break;
        }
        case 39:
            expected.value3 = ConstantAnswer(0x17bdbefaU, 0x11bb52dfU);
            break;
        case 40:
            expected.value3 = ConstantAnswer(0xd34bde55U, 0xa7292d81U);
            break;
        case 41: {
            const std::uint32_t value = query.value1 - 0x4df71ddeU;
            SetScratch(kA0b0 + value);
            expected.value3 = AlgorithmConstant(version, 4, 21) + (kA14c ^ (query.value2 + 0x711e9f67U + kA038)) +
                Scratch() + 0x20febf6cU + value;
            break;
        }
        case 42:
            SetScratch(AlgorithmConstant(version, 63, 80) + (query.value2 ^ (kA018 + 0xcdc2c7b0U)) + kA068);
            expected.value3 = Scratch() + ((query.value3 + 0x78763bb2U) ^ kA148) - 0x3ee2ef2aU + kA018 + 0xcdc2c7b0U;
            break;
        case 43: {
            const std::uint32_t value1 = query.value3 - 0x0a7fcdc1U;
            const std::uint32_t value2 = query.value1 ^ kA104 ^ 0x13214bf2U;
            SetScratch(value2 - value1 - 0x4e75b6cfU);
            expected.value3 = AlgorithmConstant(version, 76, 93) + kB298 + Scratch() + value1 - 0x0a7fc973U + value2;
            break;
        }
        case 44:
            expected.value3 = ConstantAnswer(0x37d1701dU, 0x560b19d0U);
            break;
        case 45: {
            std::uint32_t value2 = query.value2 - 0x71e2a3c7U + kA0cc;
            const std::uint32_t value1 = query.value3 - 0x679ba432U;
            SetScratch(kA028 + value1);
            value2 ^= kB290;
            expected.value3 = (kA038 ^ value2) + value2 +
                ((AlgorithmConstant(version, 59, 76) + value1) ^ ConstantAnswer(0xdddd7978U, 0xebfdf1a1U));
            break;
        }
        case 46: {
            const std::uint32_t value1 = query.value3 ^ 0x38f682dcU;
            const std::uint32_t value2 = query.value1 - kA104 + 0xa0a3519cU;
            SetScratch((value1 ^ value2) + 0x32c5ecedU);
            expected.value3 =
                AlgorithmConstant(version, 72, 89) + (Scratch() ^ 0x38f682dcU) + kB298 + value1 + 0x44eU + value2;
            break;
        }
        case 47: {
            const std::uint32_t value1 = (query.value3 + 0xf53da505U) ^ 0xd3eba94bU;
            std::uint32_t value2 = (query.value2 ^ 0xcd7e1e49U) + kA0cc;
            SetScratch(kA028 + value1);
            value2 ^= kB290;
            expected.value3 = (kA038 ^ value2) + value2 +
                ((AlgorithmConstant(version, 32, 49) + value1) ^ ConstantAnswer(0x17bdbefaU, 0x11bb52dfU));
            break;
        }
        case 48: {
            const std::uint32_t value1 = query.value2 + 0x6d610501U;
            const std::uint32_t value2 =
                (((query.value1 - kA080) - value1) ^ query.value3) + (AlgorithmConstant(version, 57, 74) - kA044);
            SetScratch(value1 - value2 + 0x2ccfdaf0U);
            expected.value3 = kB284 + Scratch() + value2;
            break;
        }
        case 49: {
            const std::uint32_t value1 = query.value1 - 0x388046b8U + kA104;
            const std::uint32_t value2 = query.value3 ^ 0xf795d1aaU;
            SetScratch(value2 + 0x222afb30U + value1);
            expected.value3 =
                AlgorithmConstant(version, 50, 67) + (Scratch() ^ 0xf795d1aaU) + kB298 + value2 + 0x44eU + value1;
            break;
        }
        case 50: {
            const std::uint32_t base = kA018 ^ 0x435bb3e7U;
            SetScratch(AlgorithmConstant(version, 79, 96) + query.value2 + kA068 + base);
            expected.value3 = (Scratch() ^ 0x852d1885U) + ((query.value3 + 0x08680b83U) ^ kA148) + base;
            break;
        }
        default:
            break;
    }
}

std::uint16_t CrcStep(const std::uint16_t input, const std::uint16_t polynomial, std::uint16_t crc) noexcept {
    std::uint16_t source = static_cast<std::uint16_t>(input << 8U);
    std::uint16_t remaining = 8;
    do {
        const std::uint16_t mixed = static_cast<std::uint16_t>(source ^ crc);
        crc = static_cast<std::uint16_t>(crc << 1U);
        if ((mixed & 0x8000U) != 0U) {
            crc = static_cast<std::uint16_t>(crc ^ polynomial);
        }
        source = static_cast<std::uint16_t>(source << 1U);
        remaining = static_cast<std::uint16_t>(remaining - 1U);
        g_crcBitCount.store(remaining, std::memory_order_relaxed);
    } while (remaining != 0U);
    return crc;
}

std::uint16_t Crc16(const std::uint8_t* data, const int length, const std::uint16_t polynomial) noexcept {
    std::uint16_t crc = 0;
    for (int index = 0; index < length; ++index) {
        crc = CrcStep(data[index], polynomial, crc);
    }
    return static_cast<std::uint16_t>(~crc);
}

std::uint32_t TargetRand() noexcept {
    g_holdRand = g_holdRand * 0x343fdU + 0x269ec3U;
    return (g_holdRand >> 16U) & 0x7fffU;
}

void TargetSrand(const std::uint32_t seed) noexcept { g_holdRand = seed; }

std::uint32_t Random32() noexcept {
    std::uint32_t result = 0;
    for (unsigned shift = 0; shift < 32; shift += 4) {
        result |= (TargetRand() & 0x0fU) << shift;
    }
    return result;
}

void UpdateUtcSlot() noexcept {
    std::uint32_t month = 0;
    std::uint32_t day = 0;
    std::uint32_t hour = 0;
#if defined(_WIN32)
    SYSTEMTIME current{};
    ::GetSystemTime(&current);
    month = current.wMonth;
    day = current.wDay;
    hour = current.wHour;
#else
    const std::time_t now = std::time(nullptr);
    std::tm current{};
    if (::gmtime_r(&now, &current) != nullptr) {
        month = static_cast<std::uint32_t>(current.tm_mon + 1);
        day = static_cast<std::uint32_t>(current.tm_mday);
        hour = static_cast<std::uint32_t>(current.tm_hour);
    }
#endif
    const std::uint32_t value =
        day == 31U ? (month - 1U) * 0x708U + 0x2b8U + hour : hour + ((month - 1U) * 0x4bU + day - 1U) * 0x18U;
    g_utcSlot.store(
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(value) - static_cast<std::uint16_t>(value / 30U) * 30U),
        std::memory_order_relaxed
    );
}

#if defined(_WIN32)
void CALLBACK ProtocolTimerCallback(HWND, UINT, UINT_PTR, DWORD) noexcept { UpdateUtcSlot(); }

bool StartProtocolTimer() noexcept { return ::SetTimer(nullptr, 0, 300000, &ProtocolTimerCallback) != 0; }
#else
class ProtocolTimer final {
public:
    ~ProtocolTimer() {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    bool Start() noexcept {
        std::lock_guard<std::mutex> guard(mutex_);
        if (thread_.joinable()) {
            return true;
        }
        try {
            thread_ = std::thread([this] {
                std::unique_lock<std::mutex> lock(mutex_);
                while (!wake_.wait_for(lock, std::chrono::milliseconds(300000), [this] { return stop_; })) {
                    lock.unlock();
                    UpdateUtcSlot();
                    lock.lock();
                }
            });
        } catch (...) {
            return false;
        }
        return true;
    }

private:
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread thread_;
    bool stop_ = false;
};

ProtocolTimer g_protocolTimer;

bool StartProtocolTimer() noexcept { return g_protocolTimer.Start(); }
#endif

std::uint32_t NextLoop1(const std::uint32_t seed) noexcept {
    std::uint32_t result = seed * 0x33d0U + (seed / 0x1c271U) * 0xa4d57508U;
    if (result == 0U || (result & 0x80000000U) != 0U) {
        result += 0x7fffffffU;
    }
    if (result == 0U) {
        result = 0x7fffffffU;
    }
    return result;
}

std::uint32_t NextLoop2(const std::uint32_t seed) noexcept {
    std::uint32_t result = seed * 0x2c08U + (seed / 0x1f39dU) * 0xaa115dddU;
    if (result == 0U || (result & 0x80000000U) != 0U) {
        result += 0x7fffffffU;
    }
    if (result == 0U) {
        result = 0x7fffffffU;
    }
    return result;
}

} // namespace

GGAUTH70_EXPORT std::uint32_t PrtcGetVersion(const std::uint32_t useUpdateTimer) {
    if (useUpdateTimer == 0U && !StartProtocolTimer()) {
        return 0;
    }
    UpdateUtcSlot();
    TargetSrand(static_cast<std::uint32_t>(std::time(nullptr)));
    g_gameMonPath = "c:\\game\\gameguard\\gamemon.des";
    return 0x46U;
}

GGAUTH70_EXPORT std::uint32_t PrtcUpdateTimer() {
    UpdateUtcSlot();
    return 0;
}

GGAUTH70_EXPORT std::uint32_t PrtcGetAuthQuery(
    std::uint32_t* const flags, Version*, void* const authOwner, const std::uint32_t sequence, const std::uint32_t serverKey
) {
    if ((*flags & 2U) != 0U) {
        return 0x0bU;
    }

    const std::uint32_t oldFlags = *flags;
    *flags = oldFlags | 2U;
    if ((oldFlags & 1U) == 0U) {
        *flags = oldFlags | 3U;
    }

    auto& query = *static_cast<AuthData*>(authOwner);
    auto* const bytes = reinterpret_cast<std::uint8_t*>(&query);
    bytes[0] = 0x1aU;
    bytes[2] = 0x46U;
    if (sequence == 0U) {
        bytes[1] = 0;
        query.value1 = serverKey;
    } else {
        bytes[1] = 1;
        query.value1 = Random32();
    }

    bytes[3] = kUtcSlotAlgorithms[g_utcSlot.load(std::memory_order_relaxed)];
    if (((*flags >> 9U) & 1U) == 0U) {
        if (*flags > 0x50000U && TargetRand() % 5U == 2U) {
            *flags |= 0x300U;
            bytes[3] = kEscalatedAlgorithms[TargetRand() % kEscalatedAlgorithms.size()];
        } else {
            *flags += 0x10000U;
        }
    }

    query.value2 = kQueryMasks[bytes[3]] ^ query.value1;
    query.value3 = Random32() & 0xffffU;
    query.value3 |= static_cast<std::uint32_t>(Crc16(bytes, 14, 0x1021U)) << 16U;
    return 0;
}

GGAUTH70_EXPORT std::uint32_t PrtcCheckAuthAnswer(
    std::uint32_t* const flags,
    Version* const version,
    AuthData* const query,
    AuthData* const answer,
    std::uint32_t,
    std::uint32_t* const serverKey
) {
    AuthData expected{};
    ComputeExpected(*query, expected, *version);
    std::uint16_t expectedChecksum = static_cast<std::uint16_t>(expected.value3 ^ *serverKey);

    const auto* const bytes = reinterpret_cast<const std::uint8_t*>(answer);
    if (bytes[2] != 0x46U) {
        return 0x0cU;
    }
    if (bytes[1] == 0U) {
        if (answer->value1 != version->gameGuardVersion) {
            return 0x0eU;
        }
        const std::size_t adjustment =
            (static_cast<std::uint32_t>(version->year) + static_cast<std::uint32_t>(version->month) +
                static_cast<std::uint32_t>(version->day) + static_cast<std::uint32_t>(version->number)) %
            kAnswerAdjustments.size();
        expectedChecksum = static_cast<std::uint16_t>(
            expectedChecksum + 0x8219U + static_cast<std::uint16_t>(kAnswerAdjustments[adjustment])
        );
    }
    if (static_cast<std::uint16_t>(answer->value3) != expectedChecksum) {
        return 0x0dU;
    }

    *serverKey = answer->value1;
    if ((*flags & 0x300U) == 0x300U) {
        *flags = (*flags & 0xfffffeffU) | 0x200U;
        *flags &= 0x0000ffffU;
    }
    return 0;
}

GGAUTH70_EXPORT std::uint32_t PrtcCheckValidAnswer(AuthData* const answer, std::int32_t* const diagnostic) {
    *diagnostic = 0;
    const auto* const bytes = reinterpret_cast<const std::uint8_t*>(answer);
    if (bytes[0] != 0x1aU) {
        return 0x12U;
    }

    const std::int32_t difference = static_cast<std::int32_t>(static_cast<std::uint16_t>(answer->value3 >> 16U)) -
        static_cast<std::int32_t>(Crc16(bytes, 14, 0x1021U));
    const std::uint8_t check = static_cast<std::uint8_t>(Crc16(bytes + 4, 4, 0x1021U));
    if (difference != 0) {
        if (difference == static_cast<std::int32_t>(bytes[3]) - static_cast<std::int32_t>(check)) {
            *diagnostic = difference;
            return 0x15U;
        }
        return 0x13U;
    }
    return bytes[3] == check ? 0U : 0x14U;
}

GGAUTH70_EXPORT std::uint32_t PrtcEncryptQuery(AuthData* const query) {
    const std::uint32_t old0 = query->index;
    const std::uint32_t old3 = query->value3;
    query->index = query->value1;
    query->value3 = query->value2;
    query->value1 = old3 ^ old0;
    query->value2 = old3;

    auto* const bytes = reinterpret_cast<std::uint8_t*>(query);
    for (std::size_t blockSize = 1; blockSize < sizeof(AuthData); blockSize *= 2) {
        for (std::size_t block = 0; block < sizeof(AuthData); block += blockSize * 2) {
            for (std::size_t index = 0; index < blockSize; ++index) {
                const std::size_t left = block + index;
                const std::size_t right = left + blockSize;
                const std::uint8_t difference = static_cast<std::uint8_t>(bytes[left] ^ bytes[right]);
                std::uint8_t mixed = static_cast<std::uint8_t>(difference * 2U);
                if ((difference & 0x80U) != 0U) {
                    mixed ^= 0xa9U;
                }
                bytes[left] ^= mixed;
                bytes[right] ^= mixed;
            }
        }
    }
    return 0;
}

GGAUTH70_EXPORT std::uint32_t PrtcDecryptAnswer(AuthData* const answer) {
    const std::uint32_t old0 = answer->index;
    const std::uint32_t old1 = answer->value1;
    const std::uint32_t old2 = answer->value2;
    answer->value1 = old0;
    answer->index = old0 ^ old1;
    answer->value2 = answer->value3 ^ old0;
    answer->value3 = old0 ^ old1 ^ old2;
    return 0;
}

GGAUTH70_EXPORT std::uint32_t PrtcGetInformation(const std::uint32_t selector, const AuthData* const data) {
    const auto* const bytes = reinterpret_cast<const std::uint8_t*>(data);
    switch (selector) {
        case 0:
            return bytes[1];
        case 1:
            return bytes[2];
        case 2:
        case 3:
            return bytes[3];
        case 4:
            return data->value1;
        case 5:
            return bytes[8];
        case 6:
            return bytes[9];
        case 7:
            return static_cast<std::uint16_t>(data->value2 >> 16U);
        case 8:
            return data->value2;
        case 9:
            return static_cast<std::uint16_t>(data->value3);
        case 10:
            return static_cast<std::uint16_t>(data->value3 >> 16U);
        default:
            return 0;
    }
}

GGAUTH70_EXPORT std::uint32_t PrtcLoopAuth(
    std::uint8_t* const loop1Index,
    std::uint32_t* const loop1,
    std::uint8_t* const loop2Index,
    std::uint32_t* const loop2,
    const AuthData* const data,
    const std::int32_t mode
) {
    const std::uint8_t target1 = static_cast<std::uint8_t>(data->value2);
    const std::uint8_t target2 = static_cast<std::uint8_t>(data->value2 >> 8U);
    if (mode == 0 || (mode == 1 && (target1 == 0U || target2 == 0U))) {
        return 0;
    }

    const std::uint8_t current1 = *loop1Index;
    if (target1 == current1 || loop1[current1] == 0U) {
        return 0x10U;
    }
    std::uint32_t seed = loop1[current1];
    std::uint32_t result1 = loop1[target1];
    if (result1 == 0U) {
        for (std::uint32_t cursor = static_cast<std::uint32_t>(current1) + 1U; cursor <= static_cast<std::uint32_t>(target1);
            ++cursor) {
            const std::size_t index = cursor == 0x100U ? 0U : cursor;
            if (loop1[index] == 0U) {
                loop1[index] = NextLoop1(seed);
            }
            seed = loop1[index];
        }
        result1 = loop1[target1];
    }
    *loop1Index = target1;

    const std::uint8_t current2 = *loop2Index;
    if (target2 == current2 || loop2[current2] == 0U) {
        return 0x10U;
    }
    seed = loop2[current2];
    std::uint32_t result2 = loop2[target2];
    if (result2 == 0U) {
        for (std::uint32_t cursor = static_cast<std::uint32_t>(current2) + 1U; cursor <= static_cast<std::uint32_t>(target2);
            ++cursor) {
            const std::size_t index = cursor == 0x100U ? 0U : cursor;
            if (loop2[index] == 0U) {
                loop2[index] = NextLoop2(seed);
            }
            seed = loop2[index];
        }
        result2 = loop2[target2];
    }
    *loop2Index = target2;

    const std::uint16_t expected =
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(result2) + static_cast<std::uint16_t>(result1));
    return static_cast<std::uint16_t>(data->value2 >> 16U) == expected ? 0U : 0x11U;
}
