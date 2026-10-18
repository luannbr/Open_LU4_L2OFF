#pragma once

#include "Shared/src/IOC.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Obj.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>

class CCaptchaSocket;

namespace CAPTCHA {

enum ANSWER_CONFIRM_RESULT : std::int32_t {
    ACR_FAIL = 0,
    ACR_SUCCESS = 1,
    ACR_INVALID = 2,
};

enum JOB_TYPE : std::int32_t {
    JT_REQUEST = 0,
    JT_REFRESH = 1,
};

} // namespace CAPTCHA


class CCaptchaJob final : public MemoryObject {
public:
    CCaptchaJob(std::uint32_t accountId, CAPTCHA::JOB_TYPE jobType);
    ~CCaptchaJob() override = default;

    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    int ConvertAnswerToInt(const char16_t* answer);
    void SetAnswer(const char16_t* answer);

    std::int64_t m_nTransactionId; // +0x18
    std::uint32_t m_nAccountId; // +0x20
    std::array<char16_t, 7> m_answer; // +0x24
    std::int32_t m_nAnswer; // +0x34
    CAPTCHA::JOB_TYPE m_jobType; // +0x38
    std::array<std::byte, 4> m_alignment03cTo040{};

private:
    L2_DECLARE_OBJECT_REGISTRATION(CCaptchaJob)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CCaptchaJob)
};

using CCaptchaCallback = bool (*)(CCaptchaSocket*, const std::uint8_t*);
using CCaptchaCallbackMap = FixedSizeCallbackMap<1003, CCaptchaCallback>;

extern CCaptchaCallbackMap g_CaptchaPacketTable;

bool DummyCaptchaPacket(CCaptchaSocket* socket, const std::uint8_t* packet);
bool CRReplyCheckKeepAlive(CCaptchaSocket* socket, const std::uint8_t* packet);
bool CRReplyCheckVersion(CCaptchaSocket* socket, const std::uint8_t* packet);
bool CRReplyCaptchaImage(CCaptchaSocket* socket, const std::uint8_t* packet);

class CCaptchaSocket final : public CIOSocket {
public:
    explicit CCaptchaSocket(std::uint64_t socket);
    ~CCaptchaSocket() override;

    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    bool CheckPendingWrite() override;
    void Send(const char* format, ...) override;
    void SendV(const char* format, va_list arguments) override;
    void OnClose() override;
    void OnRead() override;

protected:
    std::uint64_t m_socket; // +0xb0
    std::int32_t m_threadCount; // +0xb8
    std::array<std::byte, 4> m_alignment0bcTo0c0{};
    CCaptchaCallback* m_packetTable; // +0xc0
    SocketMode m_mode; // +0xc8
    std::int32_t m_packetLength; // +0xcc

private:
    // Current CRT slots 00ddce38/00ddce40 assign the socket/job IDs in
    // ascending order. The socket is class 11 and the job is class 12.
    L2_DECLARE_OBJECT_REGISTRATION(CCaptchaSocket)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CCaptchaSocket)
};

using CCaptchaPacketBase = CPacketEx<DefaultIOBuffer, CCaptchaSocket, CCaptchaCallback>;

class CCaptchaPacket final : public CCaptchaPacketBase {
public:
    CCaptchaPacket() = default;
    ~CCaptchaPacket() override = default;

protected:
    void OnIOCallback(int success, std::uint32_t transferred, PortableOverlapped* overlapped) override;
};

class CCaptchaManager final {
public:
    using JobMap = CObjectMap<CCaptchaJob, 1, 5, RWLock, int>;

    CCaptchaManager() = default;
    ~CCaptchaManager();

    void ConnectToCaptchaServer();
    void RequestCheckVersion();
    bool Connect();
    bool Init();
    void CreateThread();
    std::int64_t CreateTransactionId();
    bool PushCaptchaJob(CCaptchaJob* job);
    CSPointer<CCaptchaJob> PopCaptchaJob(std::int64_t transactionId);
    void RequestCaptchaImage(std::uint32_t accountId);
    void RefreshCaptchaImage(std::uint32_t accountId, std::int64_t transactionId);
    CAPTCHA::ANSWER_CONFIRM_RESULT ConfirmAnswer(std::int64_t transactionId, int answer);
    bool SetAnswerAndSendImageToClient(
        std::int64_t transactionId, const char16_t* answer, std::int16_t imageSize, const std::uint8_t* image
    );

private:
    friend class CCaptchaSocket;
    friend bool CRReplyCheckVersion(CCaptchaSocket*, const std::uint8_t*);

    JobMap m_mapJobs; // +0x00..+0x5f
    std::uint32_t m_nConnectThread = 0; // +0x60
    // Current initializer 00d6ea80 leaves both padding lanes untouched.
    [[maybe_unused]] std::array<std::byte, 4> m_alignment064To068;
    void* m_hConnectThread = nullptr; // +0x68
    bool m_bConnected = false; // +0x70
    bool m_bSendable = false; // +0x71
    std::array<std::byte, 6> m_alignment072To078;
    CCaptchaSocket* m_pCaptchaSocket = nullptr; // +0x78
    std::uint32_t m_nLogTick = 0; // +0x80
    volatile std::int32_t m_nTransactionId = 0; // +0x84
};


extern CCaptchaManager g_CaptchaManager;
