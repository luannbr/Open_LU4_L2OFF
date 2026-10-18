#pragma once

#include <cstdint>
#include <utility>

// Current L2Server 00c167cc creates this WELL512-derived shuffler.
// TotalChecker2015 supplies the original class/member spellings and the
// l2packetshuffle.h ownership. Every operation below is fixed independently
// by the current instructions.
class PacketShuffler {
public:
    explicit PacketShuffler(const unsigned int randomSeed) { _Initialize(randomSeed); }

    template <typename PacketSequence>
    void ShufflePacket(PacketSequence& packets, unsigned int packetCount) {
        for (unsigned int index = 1; index < packetCount; ++index) {
            const unsigned int randomIndex = GetRandomUint32Mod(index + 1U);
            using std::swap;
            swap(packets[index], packets[randomIndex]);
        }
    }

private:
    static constexpr unsigned int DIFF = 0x33U;

    void _Initialize(unsigned int randomSeed) noexcept {
        m_StateIndex = 0;
        for (std::uint32_t& state : m_State) {
            state = randomSeed;
            randomSeed = randomSeed * 2U + DIFF;
        }
    }

    void _WELLRNG512() noexcept {
        const std::uint32_t oldIndex = m_StateIndex;
        const std::uint32_t oldState = m_State[oldIndex];
        const std::uint32_t stateMinusThree = m_State[(oldIndex - 3U) & 0x0fU];
        const std::uint32_t firstMix = ((oldState * 2U ^ stateMinusThree) << 15U) ^ stateMinusThree ^ oldState;

        std::uint32_t secondMix = m_State[(oldIndex - 7U) & 0x0fU];
        secondMix ^= secondMix >> 11U;

        const std::uint32_t combined = firstMix ^ secondMix;
        m_State[oldIndex] = combined;

        m_StateIndex = (oldIndex - 1U) & 0x0fU;
        const std::uint32_t destination = m_State[m_StateIndex];
        m_State[m_StateIndex] = ((((secondMix << 10U) ^ firstMix) << 16U ^ destination) << 2U) ^
            ((combined & 0xfed22169U) << 5U) ^ combined ^ firstMix ^ destination;
    }

    unsigned int GetRandomUint32Mod(unsigned int modulo) noexcept {
        _WELLRNG512();
        return m_State[m_StateIndex] % modulo;
    }

    std::uint32_t m_State[16];
    std::uint32_t m_StateIndex;
};
