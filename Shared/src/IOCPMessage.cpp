#include "Shared/src/IOCPMessage.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/IOThreadRuntime.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Log.h"

#include <cstring>


void CIOCPMessage::OnIOCallback(int success, std::uint32_t transferred, PortableOverlapped* overlapped) {
    // Current L2NPC.exe 0x00457124. IOCPMessage.cpp lines 33-40.
    L2_GUARD_FUNCTION();

    std::int32_t argument = 0;
    static_assert(sizeof(argument) == sizeof(transferred));
    std::memcpy(&argument, &transferred, sizeof(argument));
    OnReceive(argument);
    L2_RELEASE(this, ObjectRefType::CreateDelete);
}

void CIOCPMessage::Post(int transferred) {
    // Current L2NPC.exe 0x00457028. IOCPMessage.cpp lines 9-29.
    L2_GUARD_FUNCTION();
    std::uint32_t transferredBits = 0;
    static_assert(sizeof(transferredBits) == sizeof(transferred));
    std::memcpy(&transferredBits, &transferred, sizeof(transferredBits));

    if (!IOThreadRuntime::PostCompletion(this, transferredBits, nullptr)) {
#if defined(L2_BUILD_L2SERVER)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] PostQueuedCompletionStatus failed.",
        //     L"..\\Shared\\IOCPMessage.cpp",
        //     0x16)
        // Log.Add(
        //     LOG_ERROR,
        //     "[network] IO completion queue did not accept message argument %d. Its receive callback is not queued and its
        //     reference is released.", transferred)
        L2_LOG_ERROR(
            Log,
            "[network] IO completion queue did not accept message argument %d; its receive callback is not queued and its "
            "reference is released.",
            transferred
        );
#else
        runtime::process::ReportIOCompletionPostFailure();
#endif
        L2_RELEASE(this, ObjectRefType::CreateDelete);
    }
}
