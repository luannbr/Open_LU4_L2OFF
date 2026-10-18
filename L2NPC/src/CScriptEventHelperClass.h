#pragma once

#include "Shared/src/IOCPMessage.h"


// GlobalObject.cpp's concrete IOCP payload for SCRIPT_EVENT. Only the target
// object ID and three copied integer arguments cross the async boundary.
class CScriptEventHelperClass final : public CIOCPMessage {
public:
    CScriptEventHelperClass(int npcWorldId, int eventType, int argument2, int argument3) noexcept;

    void OnReceive(int transferred) override;

private:
    int m_npcWorldId = 0;
    int m_eventType = 0;
    int m_arg2 = 0;
    int m_arg3 = 0;
};
