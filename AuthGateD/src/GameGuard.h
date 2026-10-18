#pragma once

#include <cstdint>

// Exact GameGuard wire carriers imported from AuthGateD.pdb. The selected
// executable statically links ggsrvlib25. Its provider implementation is kept
// behind this original CCSAuth2 owner so disabled GameGuard follows the same
// client path on every host.
struct _GG_AUTH_DATA {
    std::uint32_t dwIndex = 0;
    std::uint32_t dwValue1 = 0;
    std::uint32_t dwValue2 = 0;
    std::uint32_t dwValue3 = 0;
};


struct _GG_VERSION {
    std::uint32_t dwGGVer = 0;
    std::uint16_t wYear = 0;
    std::uint16_t wMonth = 0;
    std::uint16_t wDay = 0;
    std::uint16_t wNum = 0;
};


// ggsrvlib25.lib did not contribute a named PDB record for this private
// carrier, but every field and offset below is fixed by the compiled accesses
// at AuthGateD.exe 0x00418480..0x0041985f. On the original x86 target its
// size is 0x38. Callable PE providers are owned by a platform sidecar on
// non-Windows hosts. No host-only state is added to CCSAuth2.
struct _GG_AUTH_PROTOCOL {
    void* hModule = nullptr;
    std::uint32_t dwVersion = 0;
    std::uint32_t dwReferCount = 0;
    std::uint32_t dwOption = 0;
    std::int32_t nLastTime = 0;
    std::int32_t nFirstTime = 0;
    std::int32_t bDisuse = 0;
    void* PrtcGetAuthQuery = nullptr;
    void* PrtcCheckAuthAnswer = nullptr;
    void* PrtcEncryptQuery = nullptr;
    void* PrtcDecryptAnswer = nullptr;
    void* PrtcUpdateTimer = nullptr;
    _GG_AUTH_PROTOCOL* pNext = nullptr;
    _GG_AUTH_PROTOCOL* pBefore = nullptr;
};


struct _GG_UPREPORT {
    std::uint32_t dwBefore = 0;
    std::uint32_t dwNext = 0;
    std::int32_t nType = 0;
};


class CCSAuth2 {
public:
    CCSAuth2();
    ~CCSAuth2();

    void Init();
    void Close();
    std::uint32_t GetAuthQuery();
    std::uint32_t CheckAuthAnswer();

    _GG_AUTH_PROTOCOL* m_pProtocol = nullptr;
    std::uint32_t m_bPrtcRef = 0;
    std::uint32_t m_dwUserFlag = 0;
    _GG_VERSION m_GGVer;
    _GG_AUTH_DATA m_AuthQueryTmp;
    std::int32_t m_bNewProtocol = 0;
    std::int32_t m_bActive = 0;
    _GG_AUTH_DATA m_AuthQuery;
    _GG_AUTH_DATA m_AuthAnswer;
};

std::uint32_t InitGameguardAuth(
    const char* path,
    int numActive,
    bool useUpdateTimer,
    std::uint32_t useLog);
void CleanupGameguardAuth();

void GGAuthUpdateCallback(const _GG_UPREPORT* report);
void NpLog(int type, const char* message);
void GGEncryptPacket(_GG_AUTH_DATA* packet);
void GGDecryptPacket(_GG_AUTH_DATA* packet);
