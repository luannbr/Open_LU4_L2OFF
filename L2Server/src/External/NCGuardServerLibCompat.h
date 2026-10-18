#pragma once

#include "NCGuard/include/NCGuard.h"

#include <cstdint>

// ncguardserverlib64.lib export. The vendor PDB fixes the source return type
// as unsigned long. Preserve its Windows-width carrier on LP64 hosts.
#if defined(_WIN32)
using NCGuardServerResult = unsigned long;
#else
using NCGuardServerResult = std::uint32_t;
#endif

using NCGuardSendCallback = int (*)(std::uint64_t uniqueId, void* data, std::uint32_t size);
using NCGuardEventCallback = int (*)(
    std::uint64_t uniqueId, NCG_EVENT_TYPE eventType, void* data, std::uint32_t size, std::uint32_t flags
);

extern "C" NCGuardServerResult InitializeNCGuardServerW(
    const wchar_t* dllDirectory,
    const wchar_t* configurationDirectory,
    NCGuardSendCallback sendCallback,
    NCGuardEventCallback eventCallback
);
extern "C" NCGuardServerResult StartNCGuardServer();
extern "C" NCGuardServerResult StopNCGuardServer(std::uint32_t parameter);
extern "C" NCGuardServerResult GetDllVersionW(const wchar_t* path, wchar_t* versionText, NCGuardServerResult* packedVersion);
extern "C" NCGuardServerResult MakeReceivePseudoPacket(std::uint64_t uniqueCharacterId, _PSEUDO_PACKET purpose);

extern "C" NCGuardServerResult OnReceivedNCGuardClientMsg(
    std::uint32_t accountId, const std::uint8_t* data, std::uint32_t size
);
extern "C" NCGuardServerResult CreateNCGuardUserInfo(std::uint64_t characterDatabaseId);
extern "C" NCGuardServerResult DeleteNCGuardUserInfo(std::uint64_t characterDatabaseId);
extern "C" NCGuardServerResult TerminateNCGuardServer();
