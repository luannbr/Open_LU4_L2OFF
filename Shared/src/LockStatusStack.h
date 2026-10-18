#pragma once

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Exception.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"

#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <iterator>

namespace {

// The matching PDB records both as iocs.obj-local data. They originate in
// this header's anonymous namespace and are so replicated in each
// object that emits an inline lock-stack method.
[[maybe_unused]] const std::uint8_t DEFAULT_LOCK = 0;
[[maybe_unused]] const std::uint8_t LOCK_PROOF_REGION = 1;

} // namespace

// Exact global class from Shared/lockStatusStack.h. The shipping process owns
// one 0xea608-byte stack for each logical server thread.
class CLockStatusStack {
public:
    struct NodeInfo {
        const wchar_t* desc = nullptr;
        std::uint64_t addr = 0;
        std::uint32_t line = 0;
        std::uint8_t lockType = DEFAULT_LOCK;
        std::uint8_t entered = 0;

        NodeInfo() = default;

        bool CheckMismatchLockStack(std::uint64_t unlockAddress) const { return addr != unlockAddress; }

        bool CheckMismatchLockType(std::uint8_t unlockType) const { return lockType != unlockType; }

        // Current L2NPC.exe 0x004571ec. Original header lines 117-129.
        void DoCrashMismatch(const wchar_t* inDesc, std::uint64_t inAddr, std::uint32_t inLine) const {
            GuardHelper __stack_var__(L"lock stack mismatch crash", CThreadManager::GetThreadID());

            // const wchar_t* lockDescription =
            //     L"null file name".
            // if (desc != nullptr) {
            //     lockDescription = desc
            // }
            // const wchar_t* actualUnlockDescription =
            //     L"null file name".
            // if (inDesc != nullptr) {
            //     actualUnlockDescription = inDesc
            // }
            // constexpr const wchar_t* format =
            //     L"lock mismatch!!! lock[%s][%d][0x%x], "
            //     L"unlock[%s][%d][0x%x]".
            // Log.Add(
            //     LOG_LOCAL,
            //     format,
            //     lockDescription,
            //     static_cast<int>(line),
            //     static_cast<unsigned>(addr),
            //     actualUnlockDescription,
            //     static_cast<int>(inLine),
            //     static_cast<unsigned>(
            //         inAddr))
            // // The target observes the stored lock description again after
            // // the LOG_LOCAL call instead of retaining the first value.
            // lockDescription = L"null file name"
            // if (desc != nullptr) {
            //     lockDescription = desc
            // }
            // Log.Add(
            //     LOG_ERROR,
            //     format,
            //     lockDescription,
            //     static_cast<int>(line),
            //     static_cast<unsigned>(addr),
            //     actualUnlockDescription,
            //     static_cast<int>(inLine),
            //     static_cast<unsigned>(
            //         inAddr))
            // Noise: one ERROR record keeps the mandatory local copy.
            // the duplicated description snapshots above served only the old log calls.
            // runtime::log::WriteErrorWithLocalCopy(Log,
            //     "[locks] %s. Terminating the server. Recorded lock: \"%s\", line %u, address 0x%016llx, type %u. Unlock:
            //     \"%s\", line %u, address 0x%016llx.", addr != inAddr ? "Unlock order mismatch" : "Lock type mismatch",
            //     WideToUtf8(desc ? desc : L"unknown lock site").c_str(),
            //     static_cast<unsigned>(line), static_cast<unsigned long long>(addr),
            //     static_cast<unsigned>(lockType),
            //     WideToUtf8(inDesc ? inDesc : L"unknown unlock site").c_str(),
            //     static_cast<unsigned>(inLine), static_cast<unsigned long long>(inAddr))
            L2_LOG_ERROR_WITH_LOCAL_COPY(
                Log,
                "[locks] %s; terminating the server. Recorded lock: \"%s\", line %u, address 0x%016llx, type %u. Unlock: "
                "\"%s\", line %u, address 0x%016llx.",
                addr != inAddr ? "Unlock order mismatch" : "Lock type mismatch",
                WideToUtf8(desc ? desc : L"unknown lock site").c_str(), static_cast<unsigned>(line),
                static_cast<unsigned long long>(addr), static_cast<unsigned>(lockType),
                WideToUtf8(inDesc ? inDesc : L"unknown unlock site").c_str(), static_cast<unsigned>(inLine),
                static_cast<unsigned long long>(inAddr)
            );
            // ReportCallStack(LOG_LOCAL, false)
            // ReportCallStack(LOG_ERROR, false)
            // Noise: capture the guard stack once and copy each ERROR record locally.
            runtime::log::ReportCallStackWithLocalCopy();

            ForceCrash(CrashInfo::LOCK_MISMATCH);
        }
    };

    static constexpr std::int32_t kMaximumDepth = 40000;

    CLockStatusStack() = default;

    // Current L2NPC.exe 0x004576bc / Cached.exe 0x004e7f54. Original header
    // lines 143-169.
    void Lock(
        std::uint32_t ebp [[maybe_unused]],
        std::uint32_t eip [[maybe_unused]],
        const wchar_t* pszDesc,
        int line,
        std::uint64_t addr,
        std::uint8_t lockType
    ) {
        if (IsStackOverflow()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Lock stack is small?? -_-,,, kuooo")
            // Log.Add(LOG_ERROR,
            //     "[locks] Cannot safely record another lock: stack depth %d has reached capacity %d (address 0x%016llx,
            //     type %u). The acquisition path continues.", mt_nStackTop, kMaximumDepth, static_cast<unsigned long
            //     long>(addr), static_cast<unsigned>(lockType))
            L2_LOG_ERROR(
                Log,
                "[locks] Cannot safely record another lock: stack depth %d has reached capacity %d (address 0x%016llx, type "
                "%u). The acquisition path continues.",
                mt_nStackTop, kMaximumDepth, static_cast<unsigned long long>(addr), static_cast<unsigned>(lockType)
            );
        }
        IsInLockProofRegion(pszDesc, addr, static_cast<std::uint32_t>(line), true);

        NodeInfo& node = mt_Stack[mt_nStackTop];
        node.desc = pszDesc;
        node.addr = addr;
        node.line = static_cast<std::uint32_t>(line);
        node.lockType = lockType;
        node.entered = 0;
        ++mt_nStackTop;
    }

    void LockEntered(const wchar_t*, int, std::uint64_t, std::uint8_t) {
        if (IsStackOverflow()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Lock stack is small?? -_-,,, kuooo")
            // Log.Add(LOG_ERROR,
            //     "[locks] Lock entered with stack depth %d at or above capacity %d. Continuing to mark the last entry.",
            //     mt_nStackTop, kMaximumDepth)
            L2_LOG_ERROR(
                Log, "[locks] Lock entered with stack depth %d at or above capacity %d; continuing to mark the last entry.",
                mt_nStackTop, kMaximumDepth
            );
        }
        mt_Stack[mt_nStackTop - 1].entered = 1;
    }

    void StartLockProofRegion(const wchar_t* pszDesc, int line, std::uint64_t addr) {
        Lock(0, 0, pszDesc, line, addr, LOCK_PROOF_REGION);
        LockEntered(pszDesc, line, addr, LOCK_PROOF_REGION);
    }

    // Current L2NPC.exe 0x0045775c / Cached.exe 0x004e7ff4. Original header
    // lines 207-237.
    void Unlock(
        std::uint32_t ebp [[maybe_unused]],
        std::uint32_t eip [[maybe_unused]],
        const wchar_t* pszDesc,
        std::uint32_t line,
        std::uint64_t addr,
        std::uint8_t lockType
    ) {
        if (IsEmpty()) {
            DoCrashEmpty(pszDesc, addr, line);
        }
        IsInLockProofRegion(pszDesc, addr, line, false);

        --mt_nStackTop;
        NodeInfo& node = mt_Stack[mt_nStackTop];
        if (node.CheckMismatchLockStack(addr)) {
            node.DoCrashMismatch(pszDesc, addr, line);
        }
        if (node.CheckMismatchLockType(lockType)) {
            node.DoCrashMismatch(pszDesc, addr, line);
        }
        ClearNodeInfo(node);
    }

    void EndLockProofRegion(const wchar_t* pszDesc, std::uint32_t line, std::uint64_t addr) {
        if (IsEmpty()) {
            DoCrashEmpty(pszDesc, addr, line);
        }

        --mt_nStackTop;
        NodeInfo& node = mt_Stack[mt_nStackTop];
        if (node.CheckMismatchLockStack(addr)) {
            node.DoCrashMismatch(pszDesc, addr, line);
        }
        if (node.CheckMismatchLockType(LOCK_PROOF_REGION)) {
            node.DoCrashMismatch(pszDesc, addr, line);
        }
        ClearNodeInfo(node);
    }

    // Current L2NPC.exe 0x004573a0. Original header lines 258-267.
    void DoCrashEmpty(const wchar_t* inDesc, std::uint64_t inAddr, std::uint32_t inLine) const {
        GuardHelper __stack_var__(L"lock is empty", CThreadManager::GetThreadID());

        // constexpr const wchar_t* format =
        //     L"[%s][%d][%d] NO WAY!!! trying to unlock "
        //     L"the unlocked lock -_-... Kuooo mansei".
        // Log.Add(
        //     LOG_LOCAL,
        //     format,
        //     inDesc,
        //     static_cast<int>(inAddr),
        //     static_cast<int>(inLine))
        // Log.Add(
        //     LOG_ERROR,
        //     format,
        //     inDesc,
        //     static_cast<int>(inAddr),
        //     static_cast<int>(inLine))
        // Noise: the local-file copy and ERROR record share one console entry.
        // runtime::log::WriteErrorWithLocalCopy(Log,
        //     "[locks] Unlock requested with an empty lock stack. Terminating the server. Caller: \"%s\", line %u, address
        //     0x%016llx.", WideToUtf8(inDesc ? inDesc : L"unknown unlock site").c_str(), static_cast<unsigned>(inLine),
        //     static_cast<unsigned long long>(inAddr))
        L2_LOG_ERROR_WITH_LOCAL_COPY(
            Log,
            "[locks] Unlock requested with an empty lock stack; terminating the server. Caller: \"%s\", line %u, address "
            "0x%016llx.",
            WideToUtf8(inDesc ? inDesc : L"unknown unlock site").c_str(), static_cast<unsigned>(inLine),
            static_cast<unsigned long long>(inAddr)
        );
        ReportCallStack(LOG_ERROR, false);

        ForceCrash(CrashInfo::TRYING_TO_UNLOCK_THE_UNLOCKED_LOCK);
    }

    void ClearNodeInfo(NodeInfo& node) {
        node.desc = nullptr;
        node.line = 0;
        node.addr = 0;
        node.lockType = DEFAULT_LOCK;
    }

    int IsEmpty() const { return mt_nStackTop == 0; }

    int IsStackOverflow() const { return mt_nStackTop >= kMaximumDepth; }

    // Current L2NPC.exe 0x004574f8 and current Cached.exe unnamed body
    // 0x004e7ce8. Original header lines 294-308.
    bool IsInLockProofRegion(const wchar_t* pszDesc, std::uint64_t addr, std::uint32_t line, bool bLockLog) const {
        // Current L2NPC.exe uses a signed `mt_nStackTop > 0` test here.
        // This differs from `!IsEmpty()` if a fatal empty-unlock path ever
        // returns and leaves the stack depth negative.
        if (mt_nStackTop <= 0) {
            return false;
        }
        const NodeInfo& node = mt_Stack[mt_nStackTop - 1];
        if (node.lockType != LOCK_PROOF_REGION) {
            return false;
        }

        const wchar_t* lockProofDescription = L"null lockproof region name";
        if (node.desc != nullptr) {
            lockProofDescription = node.desc;
        }

        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][%d] Used %s in lock-proof "
        //     L"region![%s][%d]",
        //     pszDesc,
        //     static_cast<int>(addr),
        //     static_cast<int>(line),
        //     bLockLog ? L"lock" : L"unlock",
        //     lockProofDescription,
        //     static_cast<int>(node.line))
        // Log.Add(LOG_ERROR,
        //     "[locks] %s attempted in lock-proof region \"%s\" (line %u). Caller: \"%s\", line %u, address 0x%016llx. The
        //     operation continues.", bLockLog ? "Lock" : "Unlock", WideToUtf8(lockProofDescription).c_str(),
        //     static_cast<unsigned>(node.line),
        //     WideToUtf8(pszDesc ? pszDesc : L"unknown lock site").c_str(),
        //     static_cast<unsigned>(line), static_cast<unsigned long long>(addr))
        L2_LOG_ERROR(
            Log,
            "[locks] %s attempted in lock-proof region \"%s\" (line %u). Caller: \"%s\", line %u, address 0x%016llx. The "
            "operation continues.",
            bLockLog ? "Lock" : "Unlock", WideToUtf8(lockProofDescription).c_str(), static_cast<unsigned>(node.line),
            WideToUtf8(pszDesc ? pszDesc : L"unknown lock site").c_str(), static_cast<unsigned>(line),
            static_cast<unsigned long long>(addr)
        );
        return true;
    }

    NodeInfo mt_Stack[kMaximumDepth];
    std::int32_t mt_nStackTop = 0;
};


// The physical definition is process-sized: L2NPC has 12 slots, while the
// current CacheD creation has fixed roles through index 21.
extern CLockStatusStack g_LockStack[];
