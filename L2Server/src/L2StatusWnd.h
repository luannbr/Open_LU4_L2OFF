#pragma once

#include "Shared/src/StatusWnd.h"

#include <cstdint>

// Current L2Server RTTI fixes this complete derived owner with no fields
// beyond the 0x2058-byte CStatusWnd base. The process-global instance is
// defined by L2Server.obj.
class CL2StatusWnd final : public CStatusWnd {
public:
    CL2StatusWnd();

    void OnRefresh() override;

private:
    void DisplayCommon();
};

extern CL2StatusWnd g_L2StatusWnd;


// L2StatusWnd.obj owns the three process-wide CPU readings published by
// Shared/perfmon.cpp. Win64 long is represented as 32 bits on
// portable hosts.
extern std::int32_t g_CPUUsage;
extern std::int32_t g_L2ServerUsage;
extern std::int32_t g_L2NPCUsage;
