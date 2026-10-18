#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

enum _NCG_EVENT_TYPE : std::int32_t {
    ReportDebugEvent = 0,
    ReportClientErrorEvent = 1,
    ReportServerErrorEvent = 2,
    ReportProcessDetectionEvent = 3,
    ReportUsbDeviceDetectionEvent = 4,
    ReportBotUsingDetectionEvent = 5,
    ReportDebuggingDetectionEvent = 6,
    ReportGameguardDetectionEvent = 7,
    ReportHealthCheckEvent = 8,
    ReportCICheckEvent = 9,
    ReportPatchDetectionEvent = 10,
    ReportExtraPatchInfoEvent = 11,
    ReportCIPatternEvent = 12,
    ReportClientStatusEvent = 13,
    RequestCurUserIPEvent = 14,
    ReportMalwareFileDetectionEvent = 15,
    ReportMalwareAPIDetectionEvent = 16,
    ReportNCGIntegrationLogEvent = 17,
    ReportNCGIntegrationMobileLogEvent = 18,
};

typedef _NCG_EVENT_TYPE NCG_EVENT_TYPE;


enum _PSEUDO_PACKET : std::int32_t {
    NormalDetectedPacket = 0,
    WrongDetectedPacket = 1,
    NormalPDPacket = 2,
    WrongPDPacket = 3,
};


// Exact vendor report carriers consumed by current L2Server.exe
// CNCGuardManager::WriteNCGuardLog@0x0085b6c8. The matching
// ncguardserver64.pdb fixes every field, offset, and extent below. Windows
// WCHAR remains an explicit 16-bit code unit on portable hosts.
using NCGWChar = char16_t;

struct _NCG_REPORT_PROCESS_DETECTION {
    std::uint32_t PatternId;
    std::uint32_t ProcessId;
    std::uint64_t DetectedAddress;
    std::array<NCGWChar, 260> DetectedModulePath;
    std::array<std::uint8_t, 16> DetectedModuleHash;
    std::uint32_t DetectedModuleSize;
    std::array<NCGWChar, 260> DetectedProcessPath;
    std::array<std::uint8_t, 16> DetectedProcessHash;
    std::uint32_t DetectedProcessSize;
    std::uint16_t IsForeWnd;
};

struct _NCG_REPORT_BOT_USING_DETECTION {
    std::uint32_t PatternId;
    std::uint32_t ProcessId;
    std::uint64_t DetectedAddress;
    std::array<NCGWChar, 260> ImagePath;
    std::array<std::uint8_t, 16> ImageHash;
    std::int64_t FirstUsedTime;
    std::int64_t LastUsedTime;
};

struct _NCG_PD_PATCH_INFO {
    std::uint32_t PatchedRva;
    std::uint32_t PatchedLength;
    std::uint32_t DetouredDllSize;
    std::array<std::uint8_t, 16> InitialData;
    std::array<std::uint8_t, 16> PatchedData;
    std::array<NCGWChar, 260> DetouredDllPath;
    std::array<std::uint8_t, 16> DetouredDllHash;
};

struct _NCG_REPORT_PATCH_DETECTION {
    std::uint64_t ModuleBase;
    std::array<NCGWChar, 260> ModulePath;
    std::uint32_t ModuleSize;
    std::array<std::uint8_t, 16> ModuleHash;
    std::uint32_t InitialHash;
    std::uint32_t PatchedHash;
    std::uint32_t PatchInfoCount;
    std::array<_NCG_PD_PATCH_INFO, 4> PatchInfo;
};

struct _NCG_REPORT_EXTRA_PATCH_INFO {
    std::uint32_t ExtraInfoCount;
    std::uint32_t alignment0004To0008;
    std::uint64_t ModuleBase;
    std::array<_NCG_PD_PATCH_INFO, 4> PatchInfo;
};

struct _NCG_REPORT_CI_PATTERN {
    std::uint32_t packetType;
    std::uint32_t packetID;
    std::uint32_t clientID;
    std::uint32_t validationResult;
    std::uint32_t patternID;
    std::uint32_t flowID;
    std::uint32_t componentID;
    std::array<NCGWChar, 130> componentName;
    std::array<NCGWChar, 130> componentDesc;
    std::array<NCGWChar, 130> resvString1;
    std::array<NCGWChar, 130> resvString2;
    std::array<NCGWChar, 130> resvString3;
    std::array<NCGWChar, 130> resvString4;
    std::uint64_t resvLongLong1;
    std::uint64_t resvLongLong2;
    std::uint64_t resvLongLong3;
    std::uint64_t resvLongLong4;
    std::uint32_t resvLong1;
    std::uint32_t resvLong2;
    std::uint32_t resvLong3;
    std::uint32_t resvLong4;
};

struct _NCG_INTEGRATION_LOG {
    std::uint64_t int64_logID;
    std::uint64_t int64_Reserved;
    std::uint64_t int64_Time;
    std::uint64_t int64_AccountID;
    std::uint64_t int64_CharID;
    std::uint64_t int64_Guid1;
    std::uint64_t int64_Guid2;
    std::uint64_t int64_ConnectionTime;
    std::array<std::uint64_t, 15> data;
    std::array<NCGWChar, 260> str260_WCHAR1;
    std::array<NCGWChar, 260> str260_WCHAR2;
    std::array<NCGWChar, 260> str260_WCHAR3;
    std::array<NCGWChar, 260> str260_WCHAR4;
    std::array<NCGWChar, 260> str260_WCHAR5;
    std::array<NCGWChar, 260> str260_WCHAR6;
    std::array<NCGWChar, 260> str260_WCHAR7;
    std::array<NCGWChar, 260> str260_WCHAR8;
    std::array<NCGWChar, 120> str120_WCHAR9;
    std::array<NCGWChar, 120> str120_WCHAR10;
};
