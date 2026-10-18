#pragma once

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>

namespace protocol {
class RQ_EX_UPDATE_ITEM_AUCTION_INFO_FOR_L2ASSIST;
}

namespace network {

// Header-defined protocol stream used by the shipping typed-packet classes.
// TotalChecker2015.pdb keeps the original l2server/stream.h owner and the
// {vptr, buffer, cursor, int32 size} layout. Cached.exe 0x00531e8c constructs
// the same object over a 0x4000-byte request view before virtual Decode.
class Stream {
public:
    Stream(unsigned char* buffer, int bufferSize) noexcept :
        m_pBuffer(buffer), m_pCursor(buffer), m_bufferSize(bufferSize) {}

    virtual ~Stream() = default;

    unsigned char* GetBuffer() const noexcept { return m_pBuffer; }

    int GetSize() const noexcept { return m_bufferSize; }

    unsigned char* GetCursor() const noexcept { return m_pCursor; }

    void SetCursor(unsigned char* cursor) noexcept { m_pCursor = cursor; }

    bool Encode(const unsigned char* value, int size) {
        if (!value || size < 0 || !CanAccess(static_cast<std::size_t>(size))) {
            return false;
        }
        std::memcpy(m_pCursor, value, static_cast<std::size_t>(size));
        m_pCursor += size;
        return true;
    }

    bool Decode(unsigned char* value, int size) {
        if (!value || size < 0 || !CanAccess(static_cast<std::size_t>(size))) {
            return false;
        }
        std::memcpy(value, m_pCursor, static_cast<std::size_t>(size));
        m_pCursor += size;
        return true;
    }

    // Exact l2server/stream.h overload instantiated by the current typed
    // packet classes. Strings carry a signed 16-bit UTF-16 code-unit count
    // followed by that many code units, without a terminator.
    bool Encode(const std::u16string* value) {
        // Current Cached.exe specialization 0x00543100. The current typed
        // encoder inventory accounts for both memcpy sites and every direct
        // packet consumer of this overload.
        // Current L2Server.exe 0x0041da74 narrows the target string's
        // 64-bit size to its low signed 16 bits before doing any range
        // decision. A negative narrowed count is therefore written to the
        // stream and only then rejected. There is no preflight INT16_MAX
        // guard in the shipping overload.
        const std::uint16_t lowCharacterCount = static_cast<std::uint16_t>(value->size());
        std::int16_t characterCount = 0;
        std::memcpy(&characterCount, &lowCharacterCount, sizeof(characterCount));
        if (!Encode(&characterCount)) {
            return false;
        }
        if (characterCount == 0) {
            return true;
        }
        if (characterCount < 0) {
            return false;
        }
        return Encode(reinterpret_cast<const unsigned char*>(value->data()), static_cast<int>(characterCount) * 2);
    }

    bool Decode(std::u16string* value) {
        // Current Cached.exe specialization 0x00543f5c. The current typed
        // decoder inventory fixes the two-byte count, payload advance and all
        // six direct packet consumers.
        std::int16_t characterCount = 0;
        if (!Decode(&characterCount) || characterCount < 0) {
            return false;
        }
        std::u16string decoded(static_cast<std::size_t>(characterCount), u'\0');
        if (characterCount != 0 &&
            !Decode(reinterpret_cast<unsigned char*>(decoded.data()), static_cast<int>(characterCount) * 2)) {
            return false;
        }
        value->assign(decoded);
        return true;
    }

    // Current CS_ClientINI uses the narrow
    // basic_string<char, ..., xallocator<char>> overload: a signed 16-bit
    // byte count followed by exactly that many bytes, without a terminator.
    bool Encode(const std::string* value) {
        if (!value || value->size() > static_cast<std::size_t>(std::numeric_limits<std::int16_t>::max()) ||
            !CanAccess(sizeof(std::int16_t) + value->size())) {
            return false;
        }
        const std::int16_t byteCount = static_cast<std::int16_t>(value->size());
        return Encode(&byteCount) &&
            (byteCount == 0 || Encode(reinterpret_cast<const unsigned char*>(value->data()), byteCount));
    }

    bool Decode(std::string* value) {
        std::int16_t byteCount = 0;
        if (!value || !Decode(&byteCount) || byteCount < 0) {
            return false;
        }
        std::string decoded(static_cast<std::size_t>(byteCount), '\0');
        if (byteCount != 0 && !Decode(reinterpret_cast<unsigned char*>(decoded.data()), byteCount)) {
            return false;
        }
        value->assign(decoded);
        return true;
    }

    template <class Value>
    bool Encode(const Value* value) {
        static_assert(std::is_integral_v<Value> || std::is_enum_v<Value>, "shipping fixed-width stream values are integral");
        if (!value || !CanAccess(sizeof(Value))) {
            return false;
        }
        std::memcpy(m_pCursor, value, sizeof(Value));
        m_pCursor += sizeof(Value);
        return true;
    }

    template <class Value>
    bool Decode(Value* value) {
        static_assert(std::is_integral_v<Value> || std::is_enum_v<Value>, "shipping fixed-width stream values are integral");
        if (!value || !CanAccess(sizeof(Value))) {
            return false;
        }
        std::memcpy(value, m_pCursor, sizeof(Value));
        m_pCursor += sizeof(Value);
        return true;
    }

private:
    bool CanAccess(std::size_t size) const noexcept {
        if (!m_pBuffer || !m_pCursor || m_bufferSize < 0 || m_pCursor < m_pBuffer) {
            return false;
        }
        const std::size_t used = static_cast<std::size_t>(m_pCursor - m_pBuffer);
        const std::size_t capacity = static_cast<std::size_t>(m_bufferSize);
        return used <= capacity && size <= capacity - used;
    }

    unsigned char* m_pBuffer = nullptr; // +0x08
    unsigned char* m_pCursor = nullptr; // +0x10
    int m_bufferSize = 0; // +0x18
};


// Exact three-slot typed-packet interface: deleting destructor, Encode and
// Decode. Shared/ioc.h needs only a forward declaration.
struct IPacket {
    virtual ~IPacket() = default;
    virtual bool Encode(Stream* stream) const = 0;
    virtual bool Decode(Stream* stream) = 0;
};


} // namespace network
