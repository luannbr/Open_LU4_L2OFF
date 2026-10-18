#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Exact Win64 carriers embedded by the current ggsrvlib26_x64_MT.lib.
// L2Server needs the physical CCSAuth2 owner because GameGuardAgent embeds it
// at +0x38 and User::LeaveWorldForRaid calls Close directly.
struct _GG_AUTH_DATA final {
    std::uint32_t dwIndex;
    std::uint32_t dwValue1;
    std::uint32_t dwValue2;
    std::uint32_t dwValue3;
};

struct _GG_VERSION final {
    std::uint32_t dwGGVer;
    std::uint16_t wYear;
    std::uint16_t wMonth;
    std::uint16_t wDay;
    std::uint16_t wNum;
};

class CCSAuth2;

struct _GG_AUTH_PROTOCOL final {
    using GetAuthQueryFn = std::uint32_t (*)(std::uint32_t*, _GG_VERSION*, CCSAuth2*, std::uint32_t, std::uint32_t);
    using CheckAuthAnswerFn = std::uint32_t (*)(
        std::uint32_t*, _GG_VERSION*, _GG_AUTH_DATA*, _GG_AUTH_DATA*, std::uint32_t, std::uint32_t*
    );
    using CryptFn = std::uint32_t (*)(_GG_AUTH_DATA*);
    using UpdateTimerFn = std::uint32_t (*)();
    using LoopAuthFn = std::uint32_t (*)(
        std::uint8_t*, std::uint32_t*, std::uint8_t*, std::uint32_t*, _GG_AUTH_DATA*, std::uint32_t
    );
    using GetInformationFn = std::uint32_t (*)(std::uint32_t, _GG_AUTH_DATA*);
    using CheckValidAnswerFn = std::uint32_t (*)(_GG_AUTH_DATA*, std::int32_t*);

    void* hModule; // +0x00
    std::uint32_t dwVersion; // +0x08
    std::uint32_t dwReferCount; // +0x0c
    std::uint32_t dwOption; // +0x10
    std::int32_t nLastTime; // +0x14
    std::int32_t nFirstTime; // +0x18
    std::int32_t bDisuse; // +0x1c
    GetAuthQueryFn PrtcGetAuthQuery; // +0x20
    CheckAuthAnswerFn PrtcCheckAuthAnswer; // +0x28
    CryptFn PrtcEncryptQuery; // +0x30
    CryptFn PrtcDecryptAnswer; // +0x38
    UpdateTimerFn PrtcUpdateTimer; // +0x40
    LoopAuthFn PrtcLoopAuth; // +0x48
    GetInformationFn PrtcGetInformation; // +0x50
    CheckValidAnswerFn PrtcCheckValidAnswer; // +0x58
    _GG_AUTH_PROTOCOL* pNext; // +0x60
    _GG_AUTH_PROTOCOL* pBefore; // +0x68
};


class CCSAuth2 final {
public:
    CCSAuth2();
    ~CCSAuth2();

    void Close();
    std::uint32_t GetAuthQuery();
    std::uint32_t CheckAuthAnswer();

    // The vendor header exposes the physical state. The current PDB uses the
    // same field names and the executable fixes every offset below.
    _GG_AUTH_DATA m_AuthQuery; // +0x000
    _GG_AUTH_DATA m_AuthAnswer; // +0x010
    std::uint32_t m_dwUniqValue1; // +0x020
    std::uint32_t m_dwUniqValue2; // +0x024
    CCSAuth2* m_pNext; // +0x028
    bool m_bAuth; // +0x030
    std::array<std::byte, 0x07> m_alignment031; // +0x031
    _GG_AUTH_PROTOCOL* m_pProtocol; // +0x038
    std::uint32_t m_bPrtcRef; // +0x040
    std::uint32_t m_dwUserFlag; // +0x044
    _GG_VERSION m_GGVer; // +0x048
    _GG_AUTH_DATA m_AuthQueryTmp; // +0x054
    bool m_bAllowOldVersion; // +0x064
    std::array<std::byte, 0x03> m_alignment065; // +0x065
    std::uint32_t m_nSequenceNum; // +0x068
    std::uint32_t m_dwServerKey; // +0x06c
    std::uint32_t m_dwLastValue4; // +0x070
    std::uint8_t m_byLastLoop1; // +0x074
    std::uint8_t m_byLastLoop2; // +0x075
    std::array<std::byte, 0x02> m_alignment076; // +0x076
    std::uint32_t m_dwLoop1AuthArray[256]; // +0x078
    std::uint32_t m_dwLoop2AuthArray[256]; // +0x478
};


// ggsrvlib26_x64_MT.lib export used by Shared/Exception.cpp.
extern "C" std::uint32_t InitGameguardAuth(
    const char* path, std::uint32_t maximumActiveUsers, std::uint32_t useUpdateTimer, std::uint32_t logOptions
);
extern "C" void CleanupGameguardAuth();
