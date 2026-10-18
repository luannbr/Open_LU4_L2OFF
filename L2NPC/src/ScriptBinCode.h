#pragma once

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>


// CScriptEngine::RunNPCScript calls these byte values directly.
enum OP_TYPE {
    OP_PUSH_EVENT = 0x00,
    OP_ADD = 0x01,
    OP_SUB = 0x02,
    OP_MUL = 0x03,
    OP_DIV = 0x04,
    OP_MOD = 0x05,
    OP_FETCH_I = 0x06,
    OP_FETCH_I4 = 0x07,
    OP_FETCH_F = 0x08,
    OP_FETCH_D = 0x09,
    OP_EQUAL = 0x0a,
    OP_NOT_EQUAL = 0x0b,
    OP_LESS = 0x0c,
    OP_LESS_EQUAL = 0x0d,
    OP_GREATER_EQUAL = 0x0e,
    OP_GREATER = 0x0f,
    OP_NEGATE = 0x10,
    OP_NOT = 0x11,
    OP_AND = 0x12,
    OP_OR = 0x13,
    OP_ASSIGN8 = 0x14,
    OP_ASSIGN4 = 0x15,
    OP_EXIT_HANDLER = 0x16,
    OP_CALL_SUPER = 0x17,
    OP_BIT_AND = 0x18,
    OP_BIT_OR = 0x19,
    OP_BIT_XOR = 0x1a,
    OP_BRANCH_FALSE = 0x1b,
    OP_BRANCH_TRUE = 0x1c,
    OP_PUSH_CONST = 0x1d,
    OP_PUSH_STRING = 0x1e,
    OP_PUSH_PARAMETER = 0x1f,
    OP_PUSH_PROPERTY = 0x20,
    OP_PUSH_REG_SP = 0x21,
    OP_SHIFT_SP = 0x22,
    OP_FUNC_CALL = 0x23,
    OP_JUMP = 0x24,
    OP_ADD_STRING = 0x25
};

enum OperandTag : std::uint8_t {
    OPERAND_INTEGER = 0,
    OPERAND_FLOAT = 1,
    OPERAND_LABEL_UNRESOLVED = 2,
    OPERAND_STRING_UNRESOLVED = 3,
    OPERAND_CODE_INDEX = 4,
    OPERAND_STRING_POINTER = 5,
};

// PDB type 0x60e2 and current machine-code stride both fix this record at
// 0x10 bytes. Windows wchar_t is represented by char16_t in the portable
// build so the string-pointer member keeps the target's UTF-16 contract.
struct L2_OP {
    char optr;
    char dataType;
    std::int16_t __padding__;
    union {
        std::int64_t intData;
        float floatData;
        int codeLabel;
        int stringLabel;
        int realAddress;
        char16_t* pszString;
    } data;
};

class CScriptBinCode {
public:
    // The target replaces 32 records when maxCode is less than one.
    explicit CScriptBinCode(int maxCode = 0);
    ~CScriptBinCode();

    void AddCode(OP_TYPE type);
    void AddCode2(OP_TYPE type, std::int64_t value);
    void AddCode2(OP_TYPE type, float value);
    void AddCode2(OP_TYPE type, char16_t* value);
    void DeclareLabel(int label);
    void DeclareStringLabel(int label, char16_t* value);
    int GoPass2();

    int GetSize() const { return m_nMaxCode; }
    int GetPC() const { return m_nPC; }
    bool IsValid() const { return mIsValid; }

    void SetVariableList(std::vector<std::u16string> variableList);
    std::vector<std::u16string>& GetVariableList();

private:
    friend class CScriptEngine;

    L2_OP* m_codes;
    int m_nPC;
    int m_nMaxCode;
    bool mIsValid;
    std::map<int, int> m_mapCodeLabelToRealAddress;
    std::map<int, char16_t*> m_mapStringLabelToReadAddress;
    std::vector<std::u16string> m_vecVariableList;
};


class CNPCEventHandler;

class ScriptCodeInformation {
public:
    struct CodeInfo {
        std::u16string mClassName;
        int mEventId = 0;
        int mCodeLine = 0;
    };

    void AddInfo(CNPCEventHandler* handler, int eventId, CScriptBinCode* binCode);
    void Sort();
    void Dump() const;

private:
    int mTotalCodeLine_ = 0;
    double mBinCodeSize_ = 0.0;
    int mHandlerCount_ = 0;
    std::vector<CodeInfo> mLargeCodeList_;
    std::vector<CodeInfo> mIncompleteCodeList_;
};

// The matching NASC64_2015 PDB keeps this source comparator name. The
// current L2NPC image inlines the same descending mCodeLine comparison into
// its two compiled sort-template instances.
struct CompareCodeInfo {
    bool operator()(const ScriptCodeInformation::CodeInfo& left, const ScriptCodeInformation::CodeInfo& right) const;
};


extern ScriptCodeInformation g_ScriptCodeInfo;
