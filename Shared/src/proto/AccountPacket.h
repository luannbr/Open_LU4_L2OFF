#pragma once

#include "Shared/src/StandardContainers.h"
#include "Shared/src/proto/Stream.h"

#include <cstddef>
#include <cstdint>
#include <list>

// The current image creates every protocol/accountpacket.h carrier
// under one-byte packing. TotalChecker2015 supplies the source spellings
// below only after the current L2Server.exe virtual codecs have fixed every
// field offset, width and wire operation independently.
#pragma pack(push, 1)

namespace protocol {

struct Foo {
    bool Encode(network::Stream* stream) const { return stream->Encode(&bar); }

    bool Decode(network::Stream* stream) { return stream->Decode(&bar); }

    int bar = 0;
};


struct PointInfo {
    bool Encode(network::Stream* stream) const {
        return stream->Encode(reinterpret_cast<const unsigned char*>(this), static_cast<int>(sizeof(*this)));
    }

    bool Decode(network::Stream* stream) {
        return stream->Decode(reinterpret_cast<unsigned char*>(this), static_cast<int>(sizeof(*this)));
    }

    char pointType = 0; // +0x00
    std::int64_t pointAmount = 0; // +0x01
};


struct FriendInfo {
    bool Encode(network::Stream* stream) const {
        return stream->Encode(&accountID) && stream->Encode(&name) && stream->Encode(&friendstatus) &&
            stream->Encode(&OnOffstatus);
    }

    bool Decode(network::Stream* stream) {
        return stream->Decode(&accountID) && stream->Decode(&name) && stream->Decode(&friendstatus) &&
            stream->Decode(&OnOffstatus);
    }

    std::int64_t accountID = 0; // +0x00
    std::u16string name; // +0x08
    std::int16_t friendstatus = 0; // +0x30
    std::int16_t OnOffstatus = 0; // +0x32
};

// The old donor has a 0x20-byte string and a 0x2c-byte FriendInfo. Current
// L2Server.exe 0x004211a8/0x0041f510 instead address the 0x28-byte product
// string and place the two status words at +0x30/+0x32.

struct ACS_TEST_MORPHEUS final : network::IPacket {
    ~ACS_TEST_MORPHEUS() override = default;

    bool Encode(network::Stream* stream) const override {
        const int count = static_cast<int>(garbageList.size());
        if (!stream->Encode(&packetId) ||
            !stream->Encode(reinterpret_cast<const unsigned char*>(greetings), static_cast<int>(sizeof(greetings))) ||
            !stream->Encode(&count)) {
            return false;
        }
        for (Foo value : garbageList) {
            if (!value.Encode(stream)) {
                return false;
            }
        }
        return true;
    }

    bool Decode(network::Stream* stream) override {
        int count = 0;
        if (!stream->Decode(&packetId) ||
            !stream->Decode(reinterpret_cast<unsigned char*>(greetings), static_cast<int>(sizeof(greetings))) ||
            !stream->Decode(&count)) {
            return false;
        }
        for (int index = 0; index < count; ++index) {
            Foo value;
            if (!value.Decode(stream)) {
                return false;
            }
            garbageList.push_back(value);
        }
        return true;
    }

    char packetId = 0; // +0x08
    // Current L2Server 0x004209cc copies the UTF-16 literal
    // "Hello, accountD" through a 24-code-unit safe-copy boundary. The
    // codec still transfers this field as the exact 48 raw bytes observed at
    // 0x0041f7cc/0x0041f8dc.
    char16_t greetings[24]{}; // +0x09
    std::list<Foo, std::allocator<Foo>> garbageList; // target +0x39
};

// Target MSVC list is 0x10 and makes this packet 0x49. Host STL list extent
// is an intentional cross-platform creation detail.

struct ASC_TEST_MORPHEUS_RETURN final : network::IPacket {
    ~ASC_TEST_MORPHEUS_RETURN() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&packetId) &&
            stream->Encode(reinterpret_cast<const unsigned char*>(greetings), static_cast<int>(sizeof(greetings)));
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&packetId) &&
            stream->Decode(reinterpret_cast<unsigned char*>(greetings), static_cast<int>(sizeof(greetings)));
    }

    char packetId = 0; // +0x08
    // The current return handler passes this 200-byte field to a wide %s.
    // Preserve its physical wire width while publishing the proved UTF-16
    // source type.
    char16_t greetings[100]{}; // +0x09
};


struct ACS_CONNECT final : network::IPacket {
    ~ACS_CONNECT() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&connectedServerNumber); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&connectedServerNumber); }

    int connectedServerNumber = 0; // +0x08
};


struct ACS_FRIEND_CREATE_REQUEST final : network::IPacket {
    ~ACS_FRIEND_CREATE_REQUEST() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&requesterAccountID) && stream->Encode(&wfriendName);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&requesterAccountID) && stream->Decode(&wfriendName);
    }

    std::int64_t requesterAccountID = 0; // +0x08
    std::u16string wfriendName; // +0x10
};


struct ACS_FRIEND_DELETE final : network::IPacket {
    ~ACS_FRIEND_DELETE() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&requesterAccountID) && stream->Encode(&friendAccountID);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&requesterAccountID) && stream->Decode(&friendAccountID);
    }

    std::int64_t requesterAccountID = 0; // +0x08
    std::int64_t friendAccountID = 0; // +0x10
};


struct ACS_FRIEND final : network::IPacket {
    ~ACS_FRIEND() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&accountID); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&accountID); }

    std::int64_t accountID = 0; // +0x08
};


struct ACS_POINT_UPDATE final : network::IPacket {
    ~ACS_POINT_UPDATE() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&accountID) && stream->Encode(&pointType) && stream->Encode(&pointAmount);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&accountID) && stream->Decode(&pointType) && stream->Decode(&pointAmount);
    }

    std::int64_t accountID = 0; // +0x08
    char pointType = 0; // +0x10
    std::int64_t pointAmount = 0; // +0x11
};


struct ACS_POINT final : network::IPacket {
    ~ACS_POINT() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&accountID); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&accountID); }

    std::int64_t accountID = 0; // +0x08
};


struct ACS_REGISTER_CHAT_NAME final : network::IPacket {
    ~ACS_REGISTER_CHAT_NAME() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&accountID) && stream->Encode(&chatName); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&accountID) && stream->Decode(&chatName); }

    std::int64_t accountID = 0; // +0x08
    std::u16string chatName; // +0x10
};


struct ACS_FRIEND_CREATE_ANSWER final : network::IPacket {
    ~ACS_FRIEND_CREATE_ANSWER() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&requesterAccountID) && stream->Encode(&answerAccountID) && stream->Encode(&resultCode);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&requesterAccountID) && stream->Decode(&answerAccountID) && stream->Decode(&resultCode);
    }

    std::int64_t requesterAccountID = 0; // +0x08
    std::int64_t answerAccountID = 0; // +0x10
    std::int16_t resultCode = 0; // +0x18
};


struct ACS_FRIEND_ONLINE final : network::IPacket {
    ~ACS_FRIEND_ONLINE() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&accountID) && stream->Encode(&friendaccountID);
    }

    bool Decode(network::Stream* stream) override { return stream->Decode(&accountID) && stream->Decode(&friendaccountID); }

    std::int64_t accountID = 0; // +0x08
    std::int64_t friendaccountID = 0; // +0x10
};


struct ACS_FRIEND_OFFLINE final : network::IPacket {
    ~ACS_FRIEND_OFFLINE() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&accountID); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&accountID); }

    std::int64_t accountID = 0; // +0x08
};


struct ACS_CHAT final : network::IPacket {
    ~ACS_CHAT() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&senderAccountID) && stream->Encode(&receiverAccountID) && stream->Encode(&param);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&senderAccountID) && stream->Decode(&receiverAccountID) && stream->Decode(&param);
    }

    std::int64_t senderAccountID = 0; // +0x08
    std::int64_t receiverAccountID = 0; // +0x10
    std::u16string param; // +0x18
};


struct ASC_CONNECT_RETURN final : network::IPacket {
    ~ASC_CONNECT_RETURN() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&connected); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&connected); }

    char connected = 0; // +0x08
};


struct ASC_ACCOUNT_CONNECTION_STATUS_UPDATE final : network::IPacket {
    ~ASC_ACCOUNT_CONNECTION_STATUS_UPDATE() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&accountID) && stream->Encode(&resultCode); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&accountID) && stream->Decode(&resultCode); }

    std::int64_t accountID = 0; // +0x08
    std::int16_t resultCode = 0; // +0x10
};


struct ASC_POINT_UPDATE final : network::IPacket {
    ~ASC_POINT_UPDATE() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&accountID) && stream->Encode(&resultCode); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&accountID) && stream->Decode(&resultCode); }

    std::int64_t accountID = 0; // +0x08
    std::int16_t resultCode = 0; // +0x10
};


struct ASC_LOAD_CLIENT_INI final : network::IPacket {
    ~ASC_LOAD_CLIENT_INI() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&accountID) && stream->Encode(&iniType) &&
            stream->Encode(reinterpret_cast<const unsigned char*>(iniContents), static_cast<int>(sizeof(iniContents)));
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&accountID) && stream->Decode(&iniType) &&
            stream->Decode(reinterpret_cast<unsigned char*>(iniContents), static_cast<int>(sizeof(iniContents)));
    }

    std::int64_t accountID = 0; // +0x08
    std::int16_t iniType = 0; // +0x10
    char iniContents[0x3c01]{}; // +0x12
};


struct ASC_FRIEND_CREATE final : network::IPacket {
    ~ASC_FRIEND_CREATE() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&requesterAccountID) && stream->Encode(&name) && stream->Encode(&resultCode);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&requesterAccountID) && stream->Decode(&name) && stream->Decode(&resultCode);
    }

    std::int64_t requesterAccountID = 0; // +0x08
    std::u16string name; // +0x10
    std::int16_t resultCode = 0; // +0x38
};


struct ASC_FRIEND_DELETE final : network::IPacket {
    ~ASC_FRIEND_DELETE() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&requesterAccountID) && stream->Encode(&friendAccountID) && stream->Encode(&name) &&
            stream->Encode(&resultCode);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&requesterAccountID) && stream->Decode(&friendAccountID) && stream->Decode(&name) &&
            stream->Decode(&resultCode);
    }

    std::int64_t requesterAccountID = 0; // +0x08
    std::int64_t friendAccountID = 0; // +0x10
    std::u16string name; // +0x18
    std::int16_t resultCode = 0; // +0x40
};


struct ASC_REGISTER_CHAT_NAME final : network::IPacket {
    ~ASC_REGISTER_CHAT_NAME() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&accountID) && stream->Encode(&chatName) && stream->Encode(&resultCode);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&accountID) && stream->Decode(&chatName) && stream->Decode(&resultCode);
    }

    std::int64_t accountID = 0; // +0x08
    std::u16string chatName; // +0x10
    std::int16_t resultCode = 0; // +0x38
};


struct ASC_FRIEND_CREATE_REQUEST final : network::IPacket {
    ~ASC_FRIEND_CREATE_REQUEST() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&answerAccountID) && stream->Encode(&requesterAccountID) && stream->Encode(&Name) &&
            stream->Encode(&status);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&answerAccountID) && stream->Decode(&requesterAccountID) && stream->Decode(&Name) &&
            stream->Decode(&status);
    }

    std::int64_t answerAccountID = 0; // +0x08
    std::int64_t requesterAccountID = 0; // +0x10
    std::u16string Name; // +0x18
    std::int16_t status = 0; // +0x40
};


struct ASC_FRIEND_STATUS_UPDATE final : network::IPacket {
    ~ASC_FRIEND_STATUS_UPDATE() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&accountID) && stream->Encode(&friendaccountID) && stream->Encode(&Name) &&
            stream->Encode(&onoffline);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&accountID) && stream->Decode(&friendaccountID) && stream->Decode(&Name) &&
            stream->Decode(&onoffline);
    }

    std::int64_t accountID = 0; // +0x08
    std::int64_t friendaccountID = 0; // +0x10
    std::u16string Name; // +0x18
    std::int16_t onoffline = 0; // +0x40
};


struct ASC_FRIEND_CREATE_ANSWER final : network::IPacket {
    ~ASC_FRIEND_CREATE_ANSWER() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&resultCode) && stream->Encode(&accountID) && stream->Encode(&friendaccountID) &&
            stream->Encode(&Name) && stream->Encode(&onoffline);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&resultCode) && stream->Decode(&accountID) && stream->Decode(&friendaccountID) &&
            stream->Decode(&Name) && stream->Decode(&onoffline);
    }

    std::int16_t resultCode = 0; // +0x08
    std::int64_t accountID = 0; // +0x0a
    std::int64_t friendaccountID = 0; // +0x12
    std::u16string Name; // +0x1a
    std::int16_t onoffline = 0; // +0x42
};


struct ASC_CHAT final : network::IPacket {
    ~ASC_CHAT() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&senderAccountID) && stream->Encode(&receiverAccountID) && stream->Encode(&param);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&senderAccountID) && stream->Decode(&receiverAccountID) && stream->Decode(&param);
    }

    std::int64_t senderAccountID = 0; // +0x08
    std::int64_t receiverAccountID = 0; // +0x10
    std::u16string param; // +0x18
};


struct ASC_POINT final : network::IPacket {
    ~ASC_POINT() override = default;

    bool Encode(network::Stream* stream) const override {
        const int count = static_cast<int>(pointInfoList.size());
        if (!stream->Encode(&accountID) || !stream->Encode(&count)) {
            return false;
        }
        for (PointInfo value : pointInfoList) {
            if (!value.Encode(stream)) {
                return false;
            }
        }
        return true;
    }

    bool Decode(network::Stream* stream) override {
        int count = 0;
        if (!stream->Decode(&accountID) || !stream->Decode(&count)) {
            return false;
        }
        for (int index = 0; index < count; ++index) {
            PointInfo value;
            if (!value.Decode(stream)) {
                return false;
            }
            pointInfoList.push_back(value);
        }
        return true;
    }

    std::int64_t accountID = 0; // +0x08
    std::list<PointInfo, std::allocator<PointInfo>> pointInfoList; // target +0x10
};

// Target MSVC list is 0x10 and makes this packet 0x20.

struct ASC_FRIEND final : network::IPacket {
    ~ASC_FRIEND() override = default;

    bool Encode(network::Stream* stream) const override {
        const int count = static_cast<int>(friendInfoList.size());
        if (!stream->Encode(&accountID) || !stream->Encode(&count)) {
            return false;
        }
        for (FriendInfo value : friendInfoList) {
            if (!value.Encode(stream)) {
                return false;
            }
        }
        return true;
    }

    bool Decode(network::Stream* stream) override {
        int count = 0;
        if (!stream->Decode(&accountID) || !stream->Decode(&count)) {
            return false;
        }
        for (int index = 0; index < count; ++index) {
            FriendInfo value;
            if (!value.Decode(stream)) {
                return false;
            }
            friendInfoList.push_back(value);
        }
        return true;
    }

    std::int64_t accountID = 0; // +0x08
    std::list<FriendInfo, std::allocator<FriendInfo>> friendInfoList; // target +0x10
};

// Target MSVC list is 0x10 and makes this packet 0x20.

} // namespace protocol

#pragma pack(pop)
