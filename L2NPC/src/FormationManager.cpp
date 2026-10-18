#include "L2NPC/src/FormationManager.h"
#include "Shared/src/Runtime/ResourceDirectories.h"
#include "Shared/src/Runtime/Utf8.h"
#include "Shared/src/TemplateHelper.h"

#include "L2Core/src/inc/Guard.h"
#include "L2NPC/src/NPC.h"
#include "L2NPC/src/NPCEventFactory.h"
#include "L2NPC/src/NpcEvent.h"
#include "L2NPC/src/ParserFormation.h"
#include "L2NPC/src/Portable/TargetByteWideInput.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/Log.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <istream>
#include <new>
#include <optional>

CFormationManager g_FormationManager;

CFormationInst::CFormationInst(int uniqueId, FormationConstData* formationData) {
    L2_GUARD_FUNCTION();

    m_nUniqueID = uniqueId;
    m_pFormationData = formationData;
    m_bNextPosSet = false;
    m_bNextPosReserved = false;
    m_bSentEvent = false;
}

CFormationInst::~CFormationInst() { m_pFormationData = nullptr; }

bool CFormationInst::InsertNPC(CNPC* npc, int index, bool leader) {
    L2_GUARD_FUNCTION();

    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"No NPC in FM::InsertNPC! [%d]",
        //     index)
        // Log.Add(LOG_ERROR,
        //     "[formation] Cannot fill slot %d of instance %d: NPC is missing.",
        //     index, m_nUniqueID)
        L2_LOG_ERROR(Log, "[formation] Cannot fill slot %d of instance %d: NPC is missing.", index, m_nUniqueID);
        return false;
    }
    if (index < 0 || index >= m_pFormationData->size || index >= 30) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Index overflow in FM::InsertNPC! [%d]",
        //     index)
        // Log.Add(LOG_ERROR,
        //     "[formation] Cannot insert NPC %u into instance %d: slot %d must be non-negative, below the template size and
        //     below 30.", static_cast<unsigned int>(npc->Id()), m_nUniqueID, index)
        L2_LOG_ERROR(
            Log,
            "[formation] Cannot insert NPC %u into instance %d: slot %d must be non-negative, below the template size and "
            "below 30.",
            static_cast<unsigned int>(npc->Id()), m_nUniqueID, index
        );
        return false;
    }

    NPCFormation& member = m_sNPCFormation[index];
    if (member.npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"NPC already exists in FM::InsertNPC! [%d]",
        //     index)
        // Log.Add(LOG_WARN,
        //     "[formation] Cannot insert NPC %u into instance %d: slot %d is already occupied. Keeping the current member.",
        //     static_cast<unsigned int>(npc->Id()), m_nUniqueID, index)
        L2_LOG_WARN(
            Log,
            "[formation] Cannot insert NPC %u into instance %d: slot %d is already occupied; keeping the current member.",
            static_cast<unsigned int>(npc->Id()), m_nUniqueID, index
        );
        return false;
    }

    // The current executable publishes the CNPC back-pointer before taking
    // the formation lock.
    npc->SetFormationInst(this);
    {
        L2_GUARD(m_Lock);
        member.npc = npc;
        member.type = FPT_ARRIVED_GOAL;
        member.leader = leader;
    }
    return true;
}

bool CFormationInst::CheckAllArrived(FormationProcessType type) {
    L2_GUARD_FUNCTION();

    bool result;
    {
        L2_GUARD(m_Lock);
        result = true;
        const int size = m_pFormationData->size;
        for (int index = 0; index < size; ++index) {
            const NPCFormation& member = m_sNPCFormation[index];
            if (member.npc && member.type != type) {
                result = false;
                break;
            }
        }
    }
    return result;
}

int CFormationInst::WhereAmI(CNPC* npc) {
    int result;
    {
        L2_GUARD(m_Lock);
        result = -1;
        const int size = m_pFormationData->size;
        for (int index = 0; index < size; ++index) {
            const NPCFormation& member = m_sNPCFormation[index];
            if (member.npc == npc) {
                result = member.type;
                break;
            }
        }
    }
    return result;
}

FVector CFormationInst::GetNextWait(CNPC* npc) {
    std::optional<FVector> resultStorage;
    {
        L2_GUARD(m_Lock);
        auto& result = resultStorage.emplace();
        const int size = m_pFormationData->size;
        for (int index = 0; index < size; ++index) {
            const NPCFormation& member = m_sNPCFormation[index];
            if (member.npc == npc) {
                result = member.wait;
                break;
            }
        }
    }
    return *resultStorage;
}

FVector CFormationInst::GetNextGoal(CNPC* npc) {
    std::optional<FVector> resultStorage;
    {
        L2_GUARD(m_Lock);
        auto& result = resultStorage.emplace();
        const int size = m_pFormationData->size;
        for (int index = 0; index < size; ++index) {
            const NPCFormation& member = m_sNPCFormation[index];
            if (member.npc == npc) {
                result = member.goal;
                break;
            }
        }
    }
    return *resultStorage;
}

void CFormationInst::NotifyArrived(CNPC* npc, FormationProcessType type) {
    L2_GUARD_FUNCTION();

    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"No NPC in FM::NotifyArrived!")
        // Log.Add(LOG_ERROR,
        //     "[formation] Cannot report arrival state %d in instance %d: NPC is missing. Member states are unchanged.",
        //     static_cast<int>(type), m_nUniqueID)
        L2_LOG_ERROR(
            Log, "[formation] Cannot report arrival state %d in instance %d: NPC is missing; member states are unchanged.",
            static_cast<int>(type), m_nUniqueID
        );
        return;
    }

    m_bSentEvent = false;
    {
        L2_GUARD(m_Lock);
        for (int index = 0; index < m_pFormationData->size; ++index) {
            NPCFormation& member = m_sNPCFormation[index];
            if (member.npc == npc) {
                member.type = type;
            }
        }
    }
}

void CFormationInst::GoAll(FormationProcessType type) {
    L2_GUARD_FUNCTION();

    {
        L2_GUARD(m_Lock);
        for (int index = 0; index < m_pFormationData->size; ++index) {
            NPCFormation& member = m_sNPCFormation[index];
            if (member.npc) {
                member.type = type;
            }
        }
    }
    m_bNextPosSet = false;
    m_bNextPosReserved = false;
}

void CFormationInst::ReserveNextPos(FVector goal, FVector wait) {
    L2_GUARD_FUNCTION();

    if (m_bNextPosReserved) {
        return;
    }

    {
        L2_GUARD(m_Lock);
        const double directionX = runtime::SubtractInt32Wrapping(
            runtime::Win64CvttDoubleToInt32(wait.x), runtime::Win64CvttDoubleToInt32(goal.x)
        );
        const double directionY = runtime::SubtractInt32Wrapping(
            runtime::Win64CvttDoubleToInt32(wait.y), runtime::Win64CvttDoubleToInt32(goal.y)
        );
        const double directionLength = std::sqrt(directionX * directionX + directionY * directionY);

        double directionNormalX = 0.0;
        double directionNormalY = 0.0;
        if (directionLength > 0.1 || directionLength < -0.1) {
            directionNormalX = directionY / directionLength;
            directionNormalY = directionX / directionLength;
        }

        for (int index = 0; index < m_pFormationData->size; ++index) {
            const FVector& offset = m_pFormationData->pos_var[index];
            const double offsetX = runtime::Win64CvttDoubleToInt32(offset.x);
            const double offsetY = runtime::Win64CvttDoubleToInt32(offset.y);
            const double offsetLength = std::sqrt(offsetX * offsetX + offsetY * offsetY);

            double offsetNormalX = 0.0;
            double offsetNormalY = 0.0;
            if (offsetLength > 0.1 || offsetLength < -0.1) {
                offsetNormalX = offsetY / offsetLength;
                offsetNormalY = offsetX / offsetLength;
            }

            const int rotatedX = runtime::Win64CvttDoubleToInt32(
                (offsetNormalY * directionNormalY - offsetNormalX * directionNormalX) * offsetLength
            );
            const int rotatedY = runtime::Win64CvttDoubleToInt32(
                (offsetNormalX * directionNormalY + offsetNormalY * directionNormalX) * offsetLength
            );
            const int offsetZ = runtime::Win64CvttDoubleToInt32(offset.z);

            NPCFormation& member = m_sNPCFormation[index];
            member.goal_reserved = FVector{
                static_cast<double>(runtime::AddInt32Wrapping(runtime::Win64CvttDoubleToInt32(goal.x), rotatedX)),
                static_cast<double>(runtime::AddInt32Wrapping(runtime::Win64CvttDoubleToInt32(goal.y), rotatedY)),
                static_cast<double>(runtime::AddInt32Wrapping(runtime::Win64CvttDoubleToInt32(goal.z), offsetZ))};
            member.wait_reserved = FVector{
                static_cast<double>(runtime::AddInt32Wrapping(runtime::Win64CvttDoubleToInt32(wait.x), rotatedX)),
                static_cast<double>(runtime::AddInt32Wrapping(runtime::Win64CvttDoubleToInt32(wait.y), rotatedY)),
                static_cast<double>(runtime::AddInt32Wrapping(runtime::Win64CvttDoubleToInt32(wait.z), offsetZ))};
        }
    }
    m_bNextPosReserved = true;
}

void CFormationInst::SetNextPos() {
    L2_GUARD_FUNCTION();

    if (m_bNextPosSet) {
        return;
    }

    {
        L2_GUARD(m_Lock);
        for (int index = 0; index < m_pFormationData->size; ++index) {
            NPCFormation& member = m_sNPCFormation[index];
            member.goal = member.goal_reserved;
            member.wait = member.wait_reserved;
        }
    }
    m_bNextPosSet = true;
}

bool CFormationInst::AllNPCLoaded() {
    L2_GUARD_FUNCTION();

    bool result;
    {
        L2_GUARD(m_Lock);
        result = true;
        const int size = m_pFormationData->size;
        for (int index = 0; index < size; ++index) {
            // The target evaluates every slot. This is a bitwise
            // boolean accumulation instead of a short-circuiting &&.
            result &= m_sNPCFormation[index].npc != nullptr;
        }
    }
    return result;
}

void CFormationInst::SendNodeArrivedEventToAll() {
    L2_GUARD_FUNCTION();

    if (m_bSentEvent) {
        return;
    }

    {
        L2_GUARD(m_Lock);
        for (int index = 0; index < m_pFormationData->size; ++index) {
            NPCFormation& member = m_sNPCFormation[index];
            if (!member.npc) {
                continue;
            }

            CNPCEvent* event = CNPCEventFactory::CreateNodeArrivedEvent(
                member.npc, m_nUniqueID, m_pFormationData->id, index, member.leader ? 1 : 0, 1
            );
            if (!event) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] "
                //     L"CreateNodeArrivedEvent failed",
                //     L"FormationManager.cpp",
                //     0x161)
                // Log.Add(LOG_ERROR,
                //     "[formation] Cannot create the arrival event for NPC %u, instance %d, template %d, slot %d. Skipping
                //     this member and continuing the event broadcast.", static_cast<unsigned int>(member.npc->Id()),
                //     m_nUniqueID, m_pFormationData->id, index)
                L2_LOG_ERROR(
                    Log,
                    "[formation] Cannot create the arrival event for NPC %u, instance %d, template %d, slot %d; skipping "
                    "this member and continuing the event broadcast.",
                    static_cast<unsigned int>(member.npc->Id()), m_nUniqueID, m_pFormationData->id, index
                );
                continue;
            }

            member.npc->HandleEvent(event);
            L2_RELEASE(event, ObjectRefType::CreateDelete);
        }
        m_bSentEvent = true;
    }
}

CFormationManager::CFormationManager() {
    L2_GUARD_FUNCTION();

    m_lstFormationConstData.clear();
    m_lstFormationInst.clear();
}

CFormationInst* CFormationManager::GetFormationInst(int uniqueId) {
    L2_GUARD_FUNCTION();

    CFormationInst* result;
    {
        L2_GUARD(m_InstLock);
        result = nullptr;
        for (FormationInstList::iterator it = m_lstFormationInst.begin(); it != m_lstFormationInst.end(); ++it) {
            CFormationInst* const instance = *it;
            if (instance && instance->GetUniqueID() == uniqueId) {
                result = instance;
                break;
            }
        }
    }
    return result;
}

void CFormationManager::InsertFormationConstData(FormationConstData data) {
    L2_GUARD_FUNCTION();

    m_lstFormationConstData.push_back(data);
}

CFormationInst* CFormationManager::MakeNewFormationInst(int uniqueId, int formationId) {
    L2_GUARD_FUNCTION();

    CFormationInst* result = nullptr;
    for (FormationConstDataList::iterator it = m_lstFormationConstData.begin(); it != m_lstFormationConstData.end(); ++it) {
        FormationConstData& data = *it;
        if (data.id != formationId) {
            continue;
        }

        {
            L2_GUARD(m_InstLock);
            CFormationInst* instance = new CFormationInst(uniqueId, &data);
            if (!instance) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"cannot create formation inst %d, "
                //     L"unique id %d!",
                //     formationId,
                //     uniqueId)
                // Log.Add(LOG_ERROR,
                //     "[formation] Allocation returned no formation instance %d for template %d. Continuing the template
                //     scan.", uniqueId, formationId)
                L2_LOG_ERROR(
                    Log,
                    "[formation] Allocation returned no formation instance %d for template %d; continuing the template "
                    "scan.",
                    uniqueId, formationId
                );
            } else {
                m_lstFormationInst.push_back(instance);
                result = instance;
            }
        }
    }

    if (!result) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid Formation %d, unique id %d",
        //     formationId,
        //     uniqueId)
        // Log.Add(LOG_ERROR,
        //     "[formation] Cannot obtain instance %d from template %d. Returning no formation instance.",
        //     uniqueId, formationId)
        L2_LOG_ERROR(
            Log, "[formation] Cannot obtain instance %d from template %d; returning no formation instance.", uniqueId,
            formationId
        );
    }
    return result;
}

bool CFormationManager::InsertNPCToFormation(CNPC* npc, int uniqueId, int formationId, int formationIndex, bool leader) {
    L2_GUARD_FUNCTION();

    CFormationInst* instance = GetFormationInst(uniqueId);
    if (!instance) {
        instance = MakeNewFormationInst(uniqueId, formationId);
    }
    return instance && instance->InsertNPC(npc, formationIndex, leader);
}

void CFormationManager::Load() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const char* nativePath = "..\\Script\\formationinfo.txt";
    // std::wifstream file(
    //     runtime::ResolveResourcePath(nativePath))
    const auto resolvedPath = runtime::ResolveResourcePath(nativePath);
    std::wifstream file(resolvedPath);
    const bool fileOpened = static_cast<bool>(file);
#else
    constexpr const char* nativePath = "../script/FormationInfo.txt";
    // std::ifstream byteFile(
    //     runtime::ResolveResourcePath(nativePath))
    const auto resolvedPath = runtime::ResolveResourcePath(nativePath);
    std::ifstream byteFile(resolvedPath);
    l2npc::portable::TargetByteWideInputBuffer fileBuffer(byteFile);
    std::wistream file(&fileBuffer);
    const bool fileOpened = static_cast<bool>(byteFile);
#endif
    // constexpr const wchar_t* targetPath =
    //     L"..\\Script\\formationinfo.txt".
    [[maybe_unused]] constexpr const wchar_t* targetPath = L"..\\Script\\formationinfo.txt";

    if (!fileOpened) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] Read from %s file failed",
        //     L"FormationManager.cpp",
        //     0x1df,
        //     targetPath)
        // Log.Add(LOG_ERROR,
        //     "[formation] Cannot open definitions at '%s'. Loading skipped.",
        //     resolvedPath.u8string().c_str())
        L2_LOG_ERROR(
            Log, "[formation] Cannot open definitions at '%s'; loading skipped.",
            runtime::Utf8Bytes(resolvedPath.u8string()).c_str()
        );
        return;
    }

    CParserForFormation settingParser;
    if (settingParser.create() == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] settingParser.create failed",
        //     L"FormationManager.cpp",
        //     0x1e6)
        // Log.Add(LOG_ERROR,
        //     "[formation] Cannot initialize the parser for '%s'. Loading skipped.",
        //     resolvedPath.u8string().c_str())
        L2_LOG_ERROR(
            Log, "[formation] Cannot initialize the parser for '%s'; loading skipped.",
            runtime::Utf8Bytes(resolvedPath.u8string()).c_str()
        );
        return;
    }

    settingParser.m_lexer.yyin = &file;
    if (settingParser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing %s file failed "
        //     L"after line[%d]",
        //     L"FormationManager.cpp",
        //     0x1f0,
        //     targetPath,
        //     settingParser.m_lexer.line)
        // Log.Add(LOG_ERROR,
        //     "[formation] Cannot parse definitions at '%s' near line %d.",
        //     resolvedPath.u8string().c_str(), settingParser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     "[formation] Cannot parse definitions at '%s' near line %d.",
        //     resolvedPath.u8string().c_str(), settingParser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "[formation] Cannot parse definitions at '%s' near line %lld.",
            runtime::Utf8Bytes(resolvedPath.u8string()).c_str(), static_cast<long long>(settingParser.m_lexer.line) + 1
        );
    } else if (settingParser.yyerrorcount != 0) {
        L2_LOG_WARN(
            Log,
            "[formation] Parser accepted definitions from '%s' after reporting %d errors. Check the preceding parser "
            "diagnostics.",
            runtime::Utf8Bytes(resolvedPath.u8string()).c_str(), settingParser.yyerrorcount
        );
    }
}
