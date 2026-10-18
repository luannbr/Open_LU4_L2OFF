#include "PetitionSocket.h"

#include "CConfig.h"
#include "DB.h"
#include "PetitionCli.h"
#include "User.h"

#include "Shared/src/Log.h"
#include "Shared/src/MD5.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <WinSock2.h>
#else
#include <sys/socket.h>
#endif

namespace {

int TargetUtf16Length(const char16_t* const value) noexcept {
    int length = 0;
    while (value[length] != u'\0') {
        ++length;
    }
    return length;
}

constexpr std::array<PetitionCallback, kPetitionPacketTableSize> MakePetitionPacketTable() {
    std::array<PetitionCallback, kPetitionPacketTableSize> result{};
    for (PetitionCallback& callback : result) {
        callback = &ToSendPacketError;
    }
    result[101 - kPetitionPacketBaseOpcode] = &PetitionServerVersion;
    result[102 - kPetitionPacketBaseOpcode] = &ToSendPacketError;
    result[103 - kPetitionPacketBaseOpcode] = &PetitionAcceptWorld;
    result[250 - kPetitionPacketBaseOpcode] = &ExternalNotify;
    return result;
}

} // namespace

std::array<PetitionCallback, kPetitionPacketTableSize> petitionPacketTable = MakePetitionPacketTable();

bool PetitionDummyPacket(CPetitionSocket*, const std::uint8_t*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"error, Max Packet number is arrived.")
    // L2_LOG_ERROR(Log,
    //     L"error, Max Packet number is arrived.")
    L2_LOG_WARN(Log, "Petition packet reached the fallback handler; the packet is ignored.");
    return false;
}

bool ToSendPacketError(CPetitionSocket*, const std::uint8_t*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"error, To send packet is arrived.")
    // L2_LOG_ERROR(Log,
    //     L"error, To send packet is arrived.")
    L2_LOG_WARN(Log, "Petition packet reached the outbound/error handler; no response is processed.");
    return false;
}

bool PetitionServerVersion(CPetitionSocket* const socket, const std::uint8_t* const packet) {
    // Current L2Server 0x00968584.
    L2_GUARD_FUNCTION();

    std::int32_t challengeValue = 0;
    std::array<unsigned char, 8> challenge{};
    ::Disassemble(packet, "db", &challengeValue, static_cast<int>(challenge.size()), challenge.data());

    std::array<unsigned char, 0x5000> output{};
    std::int32_t heartBeat = 10000;
    const std::int16_t worldId = static_cast<std::int16_t>(g_nWorldId);
    const int worldNameLength = TargetUtf16Length(g_PetitionSetting.wsWorldServerName);
    ::Assemble(
        reinterpret_cast<char*>(output.data()), static_cast<int>(output.size()), "cdcchbc", 0x89, heartBeat, 0x20, worldId,
        worldNameLength, worldNameLength * 2, g_PetitionSetting.wsWorldServerName, 1
    );

    std::array<unsigned char, 16> digest{};
    MD5 md5;
    md5.Update(output.data() + 1, 0x1b);
    md5.Update(challenge.data(), static_cast<unsigned int>(challenge.size()));
    md5.Update(reinterpret_cast<unsigned char*>(&challengeValue), sizeof(challengeValue));
    md5.Update(reinterpret_cast<unsigned char*>(&heartBeat), sizeof(heartBeat));
    md5.Final(digest.data());
    std::memcpy(output.data() + 0x1c, digest.data(), digest.size());
    socket->Send("b", 0x2c, output.data());
    return false;
}

bool PetitionAcceptWorld(CPetitionSocket*, const std::uint8_t* const packet) {
    // Current L2Server 0x00968790.
    L2_GUARD_FUNCTION();
    std::uint8_t rejected = 0;
    ::Disassemble(packet, "c", &rejected);
    if (rejected != 0) {
        petitionCli.Free();
    }
    return false;
}

CPetitionSocket::CPetitionSocket(const std::uint64_t nativeSocket, PetitionCallback* const callbacks) :
    CIOSocket(nativeSocket, 600),
    sock(nativeSocket),
    threadCount(0),
    alignmentBC(0),
    packetTable(callbacks),
    mode(SM_READ_LEN),
    packetLen(0) {
    // Current L2Server 0x00968ddc.
    L2_GUARD_FUNCTION();
    petitionCli.vCategoryList.DeleteAllObjects();
    petitionCli.vFAQList.DeleteAllObjects();
}

CPetitionSocket::~CPetitionSocket() {
    // Current L2Server 0x0096900c.
    petitionCli.vCategoryList.DeleteAllObjects();
    petitionCli.vFAQList.DeleteAllObjects();
    packetTable = nullptr;
}

bool ExternalNotify(CPetitionSocket* const socket, const std::uint8_t* const packet) {
    // Current L2Server 0x00969068.
    L2_GUARD_FUNCTION();

    std::uint8_t eventType = 0;
    std::uint32_t petitionId = 0;
    std::uint32_t eventValue = 0;
    std::uint16_t nameLength = 0;
    char16_t characterName[25];
    characterName[0] = u'\0';
    std::memset(characterName + 1, 0, 0x30);
    std::memset(characterName, 0, 0x32);

    const std::uint8_t* cursor = ::Disassemble(packet, "cddh", &eventType, &petitionId, &eventValue, &nameLength);
    if (static_cast<std::uint16_t>(nameLength - 1U) < 24U) {
        std::int32_t worldId = 0;
        ::Disassemble(cursor, "bd", static_cast<int>(nameLength) * 2, characterName, &worldId);
        if (eventType == 1) {
            const std::u16string characterNameText = SanitizeUtf16(FixedUtf16View(characterName));
            std::int32_t characterDatabaseId = 0;
            CSPointer<User> user = GetUserFromNameMap(worldId, characterNameText);
            if (user) {
                characterDatabaseId = static_cast<std::int32_t>(user->GetDBID());
            }
            db.RequestSavePetitionExternalNotify(characterNameText.c_str(), petitionId, characterDatabaseId);
            socket->Send("cdd", 0xfb, petitionId, 0);
        } else if (eventType != 2) {
            socket->Send("cdd", 0xfb, petitionId, 0x1e);
        }
    } else {
        socket->Send("cdd", 0xfb, petitionId, 0x1e);
        // Log.Add(
        //     LOG_ERROR,
        //     L"PETITION[%s][%d]ExternalNotify failed: %c, %u, %u, %d",
        //     L"PetitionSocket.cpp",
        //     0x4e1,
        //     static_cast<int>(eventType),
        //     petitionId,
        //     eventValue,
        //     static_cast<int>(nameLength))
        // L2_LOG_ERROR(Log,
        //     L"PETITION[%s][%d]ExternalNotify failed: %c, %u, %u, %d",
        //     L"PetitionSocket.cpp",
        //     0x4e1,
        //     static_cast<int>(eventType),
        //     petitionId,
        //     eventValue,
        //     static_cast<int>(nameLength))
        L2_LOG_ERROR(
            Log,
            "Petition external notification rejected: name length %u is outside 1 through 24 UTF-16 code units (event %u, "
            "petition %u, value %u); error reply 30 was requested.",
            static_cast<unsigned>(nameLength), static_cast<unsigned>(eventType), static_cast<unsigned>(petitionId),
            static_cast<unsigned>(eventValue)
        );
    }
    return false;
}

void CPetitionSocket::Send(const char* const format, ...) {
    // Current L2Server 0x009692f8.
    L2_GUARD_FUNCTION();
    if (mode == SM_CLOSE) {
        return;
    }

    DefaultIOBuffer* const buffer = DefaultIOBuffer::Alloc();
    std::va_list arguments;
    va_start(arguments, format);
    const int bodySize = ::VAssemble(reinterpret_cast<char*>(buffer->m_Buffer.data() + 2), 0x4ffe, format, arguments);
    va_end(arguments);

    int frameSize = 0;
    if (bodySize == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"%d: assemble too large packet. format %s",
        //     LegacySocketSymbolForLog(),
        //     format)
        // L2_LOG_ERROR(Log,
        //     L"%d: assemble too large packet. format %s",
        //     LegacySocketSymbolForLog(),
        //     format)
        L2_LOG_ERROR(
            Log,
            "Petition socket 0x%llx could not assemble the outgoing packet (format %s); the zero-length buffer is still "
            "passed to Write.",
            static_cast<unsigned long long>(sock), format != nullptr ? format : "<null>"
        );
    } else {
        frameSize = bodySize + 2;
        const std::uint16_t encodedBodySize = static_cast<std::uint16_t>(bodySize - 1);
        buffer->m_Buffer[0] = static_cast<std::uint8_t>(encodedBodySize);
        buffer->m_Buffer[1] = static_cast<std::uint8_t>(encodedBodySize >> 8U);
    }
    buffer->m_dwSize = static_cast<std::uint32_t>(frameSize);
    Write(buffer);
}

void CPetitionSocket::OnRead() {
    // Current L2Server 0x00968848.
    L2_GUARD_FUNCTION();
    if (mode == SM_CLOSE) {
        Close();
        return;
    }

    const std::int32_t readSize = static_cast<std::int32_t>(m_pReadBuf->m_dwSize);
    std::int32_t packetOffset = 0;
    while (packetOffset < readSize) {
        if (mode == SM_READ_LEN) {
            if (readSize < packetOffset + 2) {
                Read(readSize - packetOffset);
                return;
            }
            const std::uint8_t* const header = m_pReadBuf->m_Buffer.data() + packetOffset;
            packetLen = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(header[1]) * 256U + 1U + static_cast<std::uint32_t>(header[0])
            );
            if (static_cast<std::uint32_t>(packetLen - 1) > 0x4fffU) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"socket %d: bad packet size %d",
                //     LegacySocketSymbolForLog(),
                //     packetLen)
                // L2_LOG_ERROR(Log,
                //     L"socket %d: bad packet size %d",
                //     LegacySocketSymbolForLog(),
                //     packetLen)
                L2_LOG_ERROR(
                    Log,
                    "Petition socket 0x%llx received invalid body length %d; expected 1 through 20480 bytes. Closing the "
                    "connection.",
                    static_cast<unsigned long long>(sock), packetLen
                );
                mode = SM_CLOSE;
                Close();
                return;
            }
            mode = SM_READ_BODY;
            packetOffset += 2;
        } else if (mode != SM_READ_BODY) {
            Close();
            return;
        }

        if (readSize < packetOffset + packetLen) {
            Read(readSize - packetOffset);
            return;
        }
        const std::uint8_t opcode = m_pReadBuf->m_Buffer[static_cast<std::size_t>(packetOffset)];
        if (opcode > 0xfa) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[petition socket]unknown protocol %d",
            //     static_cast<int>(opcode))
            // L2_LOG_ERROR(Log,
            //     L"[petition socket]unknown protocol %d",
            //     static_cast<int>(opcode))
            L2_LOG_ERROR(
                Log, "Petition socket 0x%llx received opcode 0x%02x above 0xfa; closing the connection.",
                static_cast<unsigned long long>(sock), static_cast<unsigned>(opcode)
            );
            mode = SM_CLOSE;
            Close();
            return;
        }

        CPetitionPacket* const posted = new CPetitionPacket;
        posted->m_pSocket = this;
        m_pReadBuf->AddRef();
        posted->m_pBuf = m_pReadBuf;
        posted->m_pFunc = packetTable[static_cast<int>(opcode) - static_cast<int>(kPetitionPacketBaseOpcode)];
        L2_ADD_REF(this, ObjectRefType::IoRetain);
        CAbstractPacket::g_nPendingPacket.fetch_add(1, std::memory_order_seq_cst);
        posted->PostObject(packetOffset);
        packetOffset += packetLen;
        mode = SM_READ_LEN;
    }
    Read(0);
}

void CPetitionSocket::OnClose() {
    // Current L2Server 0x00969440 clears the private sendable byte but
    // leaves CPetitionCli's raw socket pointer untouched.
    L2_GUARD_FUNCTION();
    petitionCli.sendable = false;
    petitionCli.ResetCheckedOutList();
    // Log.Add(
    //     LOG_WARN,
    //     L"CPetitionSocket Close %x(%x)",
    //     static_cast<unsigned int>(sock),
    //     static_cast<unsigned int>(
    //         reinterpret_cast<std::uintptr_t>(this)))
    // L2_LOG_WARN(Log,
    //     L"CPetitionSocket Close %x(%x)",
    //     static_cast<unsigned int>(sock),
    //     static_cast<unsigned int>(
    //         reinterpret_cast<std::uintptr_t>(this)))
    L2_LOG_WARN(
        Log,
        "Petition socket 0x%llx closed; the client is unavailable, checked-out petitions were cleared, and category/FAQ "
        "cleanup follows.",
        static_cast<unsigned long long>(sock)
    );
    petitionCli.vCategoryList.DeleteAllObjects();
    petitionCli.vFAQList.DeleteAllObjects();
}
