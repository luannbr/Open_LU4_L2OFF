#pragma once

#include "Shared/src/TextRuntime.h"

#include <cstdint>

class CUser;
class CItem;
class CCreature;
class CWorldObject;


class CSharedCreatureData;
class CNPC;


// Stateless formatter recovered from shared/LogFormatter.cpp. The target
// passed UTF-16 wchar_t text. Here all text arguments are UTF-16.
class CLogFormatter {
public:
    void WriteLog(
        int nLogId,
        std::int64_t nActorDbId,
        std::int64_t nActorAccountId,
        std::int64_t nTargetDbId,
        std::int64_t nTargetAccountId,
        std::int64_t x,
        std::int64_t y,
        std::int64_t z,
        const char16_t* strEtcStr1_200,
        const char16_t* strEtcStr2_50,
        const char16_t* strEtcStr3_50,
        std::int64_t nEtcNum1,
        std::int64_t nEtcNum2,
        std::int64_t nEtcNum3,
        std::int64_t nEtcNum4,
        std::int64_t nEtcNum5,
        std::int64_t nEtcNum6,
        std::int64_t nEtcNum7,
        std::int64_t nEtcNum8,
        std::int64_t nEtcNum9,
        std::int64_t nEtcNum10,
        const char16_t* strActorName_32,
        const char16_t* strActorAccountName_32,
        const char16_t* strTargetName_32,
        const char16_t* strTargetAccountName_32,
        std::int64_t nItemId
    );

    void WriteAuditLog(
        int nLogId,
        std::int64_t nActorDbId,
        std::int64_t nActorAccountId,
        std::int64_t nTargetDbId,
        std::int64_t nTargetAccountId,
        std::int64_t x,
        std::int64_t y,
        std::int64_t z,
        const char16_t* strEtcStr1_200,
        const char16_t* strEtcStr2_50,
        const char16_t* strEtcStr3_50,
        std::int64_t nEtcNum1,
        std::int64_t nEtcNum2,
        std::int64_t nEtcNum3,
        std::int64_t nEtcNum4,
        std::int64_t nEtcNum5,
        std::int64_t nEtcNum6,
        std::int64_t nEtcNum7,
        std::int64_t nEtcNum8,
        std::int64_t nEtcNum9,
        std::int64_t nEtcNum10,
        const char16_t* strActorName_32,
        const char16_t* strActorAccountName_32,
        const char16_t* strTargetName_32,
        const char16_t* strTargetAccountName_32,
        std::int64_t nItemId
    );

#if defined(L2_BUILD_L2SERVER)
    void WriteChatLog(
        int nLogId,
        std::int64_t nActorDbId,
        std::int64_t nTargetDbId,
        std::int64_t x,
        std::int64_t y,
        std::int64_t z,
        Utf16Text message,
        const char16_t* actorName,
        const char16_t* targetName
    );

    void WriteNCGuardLog(
        int nLogId,
        std::int64_t nActorDbId,
        std::int64_t nActorAccountId,
        std::int64_t nTargetDbId,
        std::int64_t nTargetAccountId,
        std::int64_t x,
        std::int64_t y,
        std::int64_t z,
        const char16_t* strEtcStr1_200,
        const char16_t* strEtcStr2_50,
        const char16_t* strEtcStr3_50,
        std::int64_t nEtcNum1,
        std::int64_t nEtcNum2,
        std::int64_t nEtcNum3,
        std::int64_t nEtcNum4,
        std::int64_t nEtcNum5,
        std::int64_t nEtcNum6,
        std::int64_t nEtcNum7,
        std::int64_t nEtcNum8,
        std::int64_t nEtcNum9,
        std::int64_t nEtcNum10,
        std::int64_t nEtcNum11,
        std::int64_t nEtcNum12,
        std::int64_t nEtcNum13,
        std::int64_t nEtcNum14,
        std::int64_t nEtcNum15,
        const char16_t* strActorName_32,
        const char16_t* strActorAccountName_32,
        const char16_t* strTargetName_32,
        const char16_t* strTargetAccountName_32,
        std::int64_t nItemId
    );

    void WriteNCGuardLog1(
        int nLogId,
        CCreature* actor,
        CWorldObject* target,
        Utf16Text strEtcStr1_200,
        Utf16Text strEtcStr2_50,
        Utf16Text strEtcStr3_50,
        std::int64_t nEtcNum1,
        std::int64_t nEtcNum2,
        std::int64_t nEtcNum3,
        std::int64_t nEtcNum4,
        std::int64_t nEtcNum5,
        std::int64_t nEtcNum6,
        std::int64_t nEtcNum7,
        std::int64_t nEtcNum8,
        std::int64_t nEtcNum9,
        std::int64_t nEtcNum10,
        std::int64_t nEtcNum11,
        std::int64_t nEtcNum12,
        std::int64_t nEtcNum13,
        std::int64_t nEtcNum14,
        std::int64_t nEtcNum15
    );

    void WriteLog1(
        int nLogId,
        CCreature* actor,
        CWorldObject* target,
        Utf16Text strEtcStr1_200,
        Utf16Text strEtcStr2_50,
        Utf16Text strEtcStr3_50,
        std::int64_t nEtcNum5,
        std::int64_t nEtcNum6,
        std::int64_t nEtcNum7,
        std::int64_t nEtcNum8,
        std::int64_t nEtcNum9,
        std::int64_t nEtcNum10,
        std::int64_t nItemId
    );

    void WriteAuditLog1(
        int nLogId,
        CCreature* actor,
        CWorldObject* target,
        Utf16Text strEtcStr1_200,
        Utf16Text strEtcStr2_50,
        Utf16Text strEtcStr3_50,
        std::int64_t nEtcNum5,
        std::int64_t nEtcNum6,
        std::int64_t nEtcNum7,
        std::int64_t nEtcNum8,
        std::int64_t nEtcNum9,
        std::int64_t nEtcNum10,
        std::int64_t nItemId
    );

    void WriteLog1_1(
        int nLogId,
        CCreature* actor,
        CWorldObject* target,
        Utf16Text strEtcStr1_200,
        Utf16Text strEtcStr2_50,
        Utf16Text strEtcStr3_50,
        std::int64_t nEtcNum5,
        std::int64_t nEtcNum6,
        std::int64_t nEtcNum7,
        std::int64_t nEtcNum8,
        std::int64_t nEtcNum9,
        std::int64_t nEtcNum10,
        std::int64_t nItemId
    );

    void WriteLog2(
        int nLogId,
        CCreature* actor,
        CWorldObject* target,
        std::int64_t nEtcNum5,
        std::int64_t nEtcNum6,
        std::int64_t nEtcNum7,
        std::int64_t nEtcNum8,
        std::int64_t nEtcNum9,
        std::int64_t nEtcNum10,
        std::int64_t nItemId
    );

    void WriteLog2_1(
        int nLogId,
        CCreature* actor,
        CWorldObject* target,
        std::int64_t nEtcNum5,
        std::int64_t nEtcNum6,
        std::int64_t nEtcNum7,
        std::int64_t nEtcNum8,
        std::int64_t nEtcNum9,
        std::int64_t nEtcNum10,
        std::int64_t nItemId
    );

    void WriteLog4(
        int nLogId,
        CCreature* actor,
        CWorldObject* target,
        Utf16Text strEtcStr1_200,
        Utf16Text strEtcStr2_50,
        Utf16Text strEtcStr3_50,
        std::int64_t nEtcNum1,
        std::int64_t nEtcNum2,
        std::int64_t nEtcNum3,
        std::int64_t nEtcNum4,
        std::int64_t nEtcNum5,
        std::int64_t nEtcNum6,
        std::int64_t nEtcNum7,
        std::int64_t nEtcNum8,
        std::int64_t nEtcNum9,
        std::int64_t nEtcNum10,
        std::int64_t nItemId
    );
#endif

    void WriteLogCache3(
        int nLogId,
        CUser* user,
        std::int64_t nEtcNum5,
        std::int64_t nEtcNum6,
        std::int64_t nEtcNum7,
        std::int64_t nEtcNum8,
        std::int64_t nEtcNum9,
        std::int64_t nEtcNum10
    );

    void WriteLogCache1(
        int nLogId,
        CUser* user,
        CItem* item,
        Utf16Text strEtcStr1_200,
        Utf16Text strEtcStr2_50,
        Utf16Text strEtcStr3_50,
        std::int64_t nEtcNum5,
        std::int64_t nEtcNum6,
        std::int64_t nEtcNum7,
        std::int64_t nEtcNum8,
        std::int64_t nEtcNum9,
        std::int64_t nEtcNum10
    );

    void WriteLogCache2(
        int nLogId,
        CUser* user,
        CItem* item,
        std::int64_t nEtcNum5,
        std::int64_t nEtcNum6,
        std::int64_t nEtcNum7,
        std::int64_t nEtcNum8,
        std::int64_t nEtcNum9,
        std::int64_t nEtcNum10
    );

    void WriteLogNpc1(
        int nLogId,
        CNPC& actor,
        CSharedCreatureData* pTalker,
        char16_t* strEtcStr2_50,
        char16_t* strEtcStr3_50,
        std::int64_t nEtcNum5,
        std::int64_t nEtcNum6,
        std::int64_t nEtcNum7,
        std::int64_t nEtcNum8,
        std::int64_t nEtcNum9,
        std::int64_t nEtcNum10,
        std::int64_t nItemId
    );

    void WriteLogNpc2(
        int nLogId,
        CNPC& actor,
        CSharedCreatureData* pTalker,
        char16_t* strEtcStr2_50,
        char16_t* strEtcStr3_50,
        std::int64_t nEtcNum5,
        std::int64_t nEtcNum6,
        std::int64_t nEtcNum7,
        std::int64_t nEtcNum8,
        std::int64_t nEtcNum9,
        std::int64_t nEtcNum10,
        std::int64_t nItemId
    );

    // Present in the current L2NPC.exe and in the matching 2015 shared
    // implementation, although the older L2NPC donor PDB exposes only
    // WriteLogNpc1/WriteLogNpc2.
    void WriteLogNpc3(
        int nLogId,
        CNPC& actor,
        CSharedCreatureData* pTalker,
        char16_t* strEtcStr2_50,
        char16_t* strEtcStr3_50,
        std::int64_t nEtcNum5,
        std::int64_t nEtcNum6,
        std::int64_t nEtcNum7,
        std::int64_t nEtcNum8,
        std::int64_t nEtcNum9,
        std::int64_t nEtcNum10,
        std::int64_t nItemId
    );
};
