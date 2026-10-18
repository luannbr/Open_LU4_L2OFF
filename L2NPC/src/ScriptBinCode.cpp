#include "ScriptBinCode.h"

#include "L2Core/src/inc/Guard.h"
#include "NPCEventHandler.h"
#include "Portable/Msvc2010Algorithms.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/Log.h"
#include "Shared/src/ThreadManager.h"
#include "TextRuntime.h"

ScriptCodeInformation g_ScriptCodeInfo;

CScriptBinCode::CScriptBinCode(int maxCode) {
    L2_GUARD_FUNCTION();

    m_nPC = 0;
    if (maxCode < 1) {
        maxCode = 0x20;
    }
    m_codes = new L2_OP[static_cast<std::size_t>(maxCode)]{};
    m_nMaxCode = maxCode;
    mIsValid = true;
}

CScriptBinCode::~CScriptBinCode() { delete[] m_codes; }

void CScriptBinCode::AddCode(OP_TYPE type) {
    L2_GUARD_FUNCTION();
    if (m_nPC >= m_nMaxCode) {
        mIsValid = false;
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Too large code can't add opcode, "
        //     L"type=[%d]",
        //     L"ScriptBinCode.cpp",
        //     0x25,
        //     static_cast<int>(type))
        // Log.Add(LOG_ERROR,
        //     "[AI load] Cannot append opcode %d at instruction %d: bytecode capacity is %d. Marking the handler
        //     incomplete.", static_cast<int>(type), m_nPC, m_nMaxCode)
        L2_LOG_ERROR(
            Log,
            "[AI load] Cannot append opcode %d at instruction %d: bytecode capacity is %d. Marking the handler incomplete.",
            static_cast<int>(type), m_nPC, m_nMaxCode
        );
        return;
    }
    m_codes[m_nPC].optr = static_cast<char>(type);
    ++m_nPC;
}

void CScriptBinCode::AddCode2(OP_TYPE type, std::int64_t value) {
    L2_GUARD_FUNCTION();
    if (m_nPC >= m_nMaxCode) {
        mIsValid = false;
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Too large code can't add opcode, "
        //     L"type=[%d]",
        //     L"ScriptBinCode.cpp",
        //     0x38,
        //     type)
        // Log.Add(LOG_ERROR,
        //     "[AI load] Cannot append opcode %d at instruction %d: bytecode capacity is %d. Marking the handler
        //     incomplete.", static_cast<int>(type), m_nPC, m_nMaxCode)
        L2_LOG_ERROR(
            Log,
            "[AI load] Cannot append opcode %d at instruction %d: bytecode capacity is %d. Marking the handler incomplete.",
            static_cast<int>(type), m_nPC, m_nMaxCode
        );
        return;
    }

    L2_OP& instruction = m_codes[m_nPC];
    instruction.optr = static_cast<char>(type);
    if (type == OP_BRANCH_FALSE || type == OP_JUMP || type == OP_BRANCH_TRUE) {
        instruction.data.codeLabel = static_cast<std::int32_t>(value);
        instruction.dataType = OPERAND_LABEL_UNRESOLVED;
    } else if (type == OP_PUSH_STRING) {
        instruction.data.stringLabel = static_cast<std::int32_t>(value);
        instruction.dataType = OPERAND_STRING_UNRESOLVED;
    } else {
        instruction.data.intData = value;
        instruction.dataType = OPERAND_INTEGER;
    }
    ++m_nPC;
}

void CScriptBinCode::AddCode2(OP_TYPE type, float value) {
    L2_GUARD_FUNCTION();
    if (m_nPC >= m_nMaxCode) {
        mIsValid = false;
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Too large code can't add opcode, "
        //     L"type=[%d]",
        //     L"ScriptBinCode.cpp",
        //     0x5c,
        //     type)
        // Log.Add(LOG_ERROR,
        //     "[AI load] Cannot append opcode %d at instruction %d: bytecode capacity is %d. Marking the handler
        //     incomplete.", static_cast<int>(type), m_nPC, m_nMaxCode)
        L2_LOG_ERROR(
            Log,
            "[AI load] Cannot append opcode %d at instruction %d: bytecode capacity is %d. Marking the handler incomplete.",
            static_cast<int>(type), m_nPC, m_nMaxCode
        );
        return;
    }

    L2_OP& instruction = m_codes[m_nPC];
    instruction.optr = static_cast<char>(type);
    instruction.dataType = OPERAND_FLOAT;
    instruction.data.floatData = value;
    ++m_nPC;
}

void CScriptBinCode::AddCode2(OP_TYPE type, char16_t* value) {
    L2_GUARD_FUNCTION();
    if (m_nPC >= m_nMaxCode) {
        mIsValid = false;
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Too large code can't add opcode, "
        //     L"type=[%d]",
        //     L"ScriptBinCode.cpp",
        //     0x70,
        //     type)
        // Log.Add(LOG_ERROR,
        //     "[AI load] Cannot append opcode %d at instruction %d: bytecode capacity is %d. Marking the handler
        //     incomplete.", static_cast<int>(type), m_nPC, m_nMaxCode)
        L2_LOG_ERROR(
            Log,
            "[AI load] Cannot append opcode %d at instruction %d: bytecode capacity is %d. Marking the handler incomplete.",
            static_cast<int>(type), m_nPC, m_nMaxCode
        );
        return;
    }

    L2_OP& instruction = m_codes[m_nPC];
    instruction.optr = static_cast<char>(type);
    instruction.dataType = OPERAND_STRING_POINTER;
    instruction.data.pszString = value;
    ++m_nPC;
}

void CScriptBinCode::DeclareLabel(int label) {
    L2_GUARD_FUNCTION();
    m_mapCodeLabelToRealAddress[label] = m_nPC;
}

void CScriptBinCode::DeclareStringLabel(int label, char16_t* value) {
    L2_GUARD_FUNCTION();
    m_mapStringLabelToReadAddress[label] = value;
}

int CScriptBinCode::GoPass2() {
    L2_GUARD_FUNCTION();
    for (int index = 0; index < m_nPC; ++index) {
        L2_OP& instruction = m_codes[index];
        if (instruction.optr == OP_JUMP || instruction.optr == OP_BRANCH_FALSE || instruction.optr == OP_BRANCH_TRUE) {
            if (instruction.dataType == OPERAND_LABEL_UNRESOLVED) {
                instruction.data.realAddress = m_mapCodeLabelToRealAddress[instruction.data.codeLabel];
                instruction.dataType = OPERAND_CODE_INDEX;
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] undefined bin code",
                //     L"ScriptBinCode.cpp",
                //     0x8e)
                // Log.Add(LOG_ERROR,
                //     "[AI load] Cannot resolve branch at instruction %d (opcode %u): operand tag %d is not an unresolved
                //     code label, leaving it unchanged.", index, static_cast<unsigned int>(instruction.optr),
                //     static_cast<int>(instruction.dataType))
                L2_LOG_ERROR(
                    Log,
                    "[AI load] Cannot resolve branch at instruction %d (opcode %u): operand tag %d is not an unresolved "
                    "code label; leaving it unchanged.",
                    index, static_cast<unsigned int>(instruction.optr), static_cast<int>(instruction.dataType)
                );
            }
        } else if (instruction.optr == OP_PUSH_STRING) {
            if (instruction.dataType == OPERAND_STRING_UNRESOLVED) {
                instruction.data.pszString = m_mapStringLabelToReadAddress[instruction.data.stringLabel];
                instruction.dataType = OPERAND_STRING_POINTER;
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] undefined bin code",
                //     L"ScriptBinCode.cpp",
                //     0x9a)
                // Log.Add(LOG_ERROR,
                //     "[AI load] Cannot resolve string at instruction %d: operand tag %d is not an unresolved string label.
                //     leaving it unchanged.", index, static_cast<int>(instruction.dataType))
                L2_LOG_ERROR(
                    Log,
                    "[AI load] Cannot resolve string at instruction %d: operand tag %d is not an unresolved string label; "
                    "leaving it unchanged.",
                    index, static_cast<int>(instruction.dataType)
                );
            }
        }
    }
    return 1;
}

void CScriptBinCode::SetVariableList(std::vector<std::u16string> variableList) {
    L2_GUARD_FUNCTION();
    m_vecVariableList = variableList;
}

std::vector<std::u16string>& CScriptBinCode::GetVariableList() {
    L2_GUARD_FUNCTION();
    return m_vecVariableList;
}

bool CompareCodeInfo::operator()(
    const ScriptCodeInformation::CodeInfo& left, const ScriptCodeInformation::CodeInfo& right
) const {
    return left.mCodeLine > right.mCodeLine;
}

void ScriptCodeInformation::AddInfo(CNPCEventHandler* handler, int eventId, CScriptBinCode* binCode) {
    L2_GUARD_FUNCTION();
    mTotalCodeLine_ = runtime::AddInt32Wrapping(mTotalCodeLine_, binCode->GetPC());
    mBinCodeSize_ += static_cast<double>(runtime::MultiplyInt32Wrapping(binCode->GetSize(), 16));
    mHandlerCount_ = runtime::AddInt32Wrapping(mHandlerCount_, 1);

    if (!binCode->IsValid()) {
        const char16_t* const className = handler->GetAIClassName();
        const CodeInfo info{std::u16string(std::u16string_view(className, std::char_traits<char16_t>::length(className))),
            eventId, binCode->GetPC()};
        mIncompleteCodeList_.push_back(info);
    } else if (binCode->GetPC() > 7000) {
        const char16_t* const className = handler->GetAIClassName();
        const CodeInfo info{std::u16string(std::u16string_view(className, std::char_traits<char16_t>::length(className))),
            eventId, binCode->GetPC()};
        mLargeCodeList_.push_back(info);
    }
}

void ScriptCodeInformation::Sort() {
    L2_GUARD_FUNCTION();
    portable_detail::Msvc2010Sort(mLargeCodeList_.begin(), mLargeCodeList_.end(), CompareCodeInfo{});
    portable_detail::Msvc2010Sort(mIncompleteCodeList_.begin(), mIncompleteCodeList_.end(), CompareCodeInfo{});
}

void ScriptCodeInformation::Dump() const {
    L2_GUARD_FUNCTION();
    // Noise: The following summary identifies the AI bytecode report. Decorative banners add no information.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"+----------------------------------+")
    // Noise: The following summary identifies the AI bytecode report. Decorative banners add no information.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"    AI Script Code Information    ")
    // Noise: The following summary identifies the AI bytecode report. Decorative banners add no information.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"+----------------------------------+")
    // Noise: The hardcoded macro value is not the measured per-handler capacity. Overflow errors report the actual capacity.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"  [+] #define MAX_CODE_LINE (%d)",
    //     26000)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"  [+] Handler Count = %d",
    //     mHandlerCount_)
    // Log.Add(LOG_NORMAL,
    //     "[AI load] Bytecode summary: %d handlers, %d instructions, %.3f MiB allocated. %zu large and %zu incomplete
    //     handlers.", mHandlerCount_, mTotalCodeLine_, mBinCodeSize_ / (1024.0 * 1024.0), mLargeCodeList_.size(),
    //     mIncompleteCodeList_.size())
    L2_LOG_INFO(
        Log,
        "[AI load] Bytecode summary: %d handlers, %d instructions, %.3f MiB allocated; %zu large and %zu incomplete "
        "handlers.",
        mHandlerCount_, mTotalCodeLine_, mBinCodeSize_ / (1024.0 * 1024.0), mLargeCodeList_.size(),
        mIncompleteCodeList_.size()
    );
    // Noise: This measurement is included in the combined bytecode summary.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"  [+] Total PC = %d ",
    //     mTotalCodeLine_)
    // Noise: This measurement is included in the combined bytecode summary.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"  [+] Total OP Code Size = %5.3f M",
    //     mBinCodeSize_ / (1024.0 * 1024.0))

    if (!mLargeCodeList_.empty()) {
        // Noise: Each large-handler row already names its classification and script.
        // Log.Add(
        //     LOG_NORMAL,
        //     L"  [+] Large code information")
        for (const CodeInfo& info : mLargeCodeList_) {
            const std::string className = Utf16ToUtf8(info.mClassName);
            // Log.Add(
            //     LOG_NORMAL,
            //     L"     [+] PC=[%5d], ai=[%s]",
            //     info.mCodeLine,
            //     className.c_str())
            // Log.Add(LOG_NORMAL,
            //     "[AI load] Large handler: script '%s', event %d, %d instructions (threshold 7000).",
            //     WideToUtf8(className).c_str(), info.mEventId, info.mCodeLine)
            L2_LOG_INFO(
                Log, "[AI load] Large handler: script '%s', event %d, %d instructions (threshold 7000).", className.c_str(),
                info.mEventId, info.mCodeLine
            );
        }
    }

    if (!mIncompleteCodeList_.empty()) {
        // Noise: Each incomplete-handler error already names its classification and script.
        // Log.Add(
        //     LOG_ERROR,
        //     L"  [+] Incomplete code information")
        for (const CodeInfo& info : mIncompleteCodeList_) {
            const std::string className = Utf16ToUtf8(info.mClassName);
            // Log.Add(
            //     LOG_ERROR,
            //     L"     [+] PC=[%5d], ai=[%s]",
            //     info.mCodeLine,
            //     className.c_str())
            // Log.Add(LOG_ERROR,
            //     "[AI load] Incomplete handler: script '%s', event %d, %d stored instructions.",
            //     WideToUtf8(className).c_str(), info.mEventId, info.mCodeLine)
            L2_LOG_ERROR(
                Log, "[AI load] Incomplete handler: script '%s', event %d, %d stored instructions.", className.c_str(),
                info.mEventId, info.mCodeLine
            );
        }
    }
    // Noise: Blank separator has no diagnostic content.
    // Log.Add(
    //     LOG_NORMAL,
    //     L" ")
}
