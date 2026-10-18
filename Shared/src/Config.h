#pragma once

#include "Shared/src/net/Config.h"

#include <cstdint>

// shared/config.h in the Windows targets. wchar_t is a two-byte UTF-16 code
// unit there, so the explicit char16_t array keeps the PDB's 0x800-byte
// BinaryInfo layout on every host.
struct BinaryInfo {
    char16_t updateVersion[1024]{};
};


namespace L2ServerType {

// Current L2Server.exe ServerTypeHelper creates the six strings and
// values below at 0x00a432dc. TotalChecker2015 supplies the historical
// enumerator spellings only. Current L2NPC.exe uses the same
// numeric transport.
enum Value : std::int32_t {
    DEV = 0,
    LIVE = 1,
    TEST = 2,
    MOVIE = 3,
    BROADCAST = 4,
    WORLDSERVER = 5,
};

} // namespace L2ServerType


// Each server executable defines these process globals in its own config
// owner. Shared/Exception.cpp and Shared/SystemMail.cpp consume them.
extern bool g_bExceptionMakeMiniDump;
extern BinaryInfo g_BinaryInfo;
extern int g_nServerId;
extern int g_nWorldId;

// Each process that has the current L2Server-type configuration contract
// owns its own instance. L2Server's initialized value is LIVE. L2NPC's
// zero-filled instance starts as DEV and is populated by its config owner.
extern L2ServerType::Value g_L2ServerType;
