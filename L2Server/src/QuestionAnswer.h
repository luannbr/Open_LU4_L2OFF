#pragma once

#include "L2Core/src/inc/AES.h"

#include <cstddef>
#include <type_traits>

struct Query {
    Query() : textureNo(0), x(0), y(0) {}

    Query(const Query& other) : textureNo(other.textureNo), x(other.x), y(other.y) {}

    Query& operator=(const Query& other) {
        if (this != &other) {
            textureNo = other.textureNo;
            x = other.x;
            y = other.y;
        }
        return *this;
    }

    unsigned textureNo; // +0x00
    unsigned x; // +0x04
    unsigned y; // +0x08
};

struct RGBA {
    unsigned r; // +0x00
    unsigned g; // +0x04
    unsigned b; // +0x08
    unsigned a; // +0x0c
};

enum ObfuscationMethodType {
    METHOD_1 = 1,
    METHOD_2 = 2,
    METHOD_3 = 3,
    METHOD_4 = 4,
    METHOD_5 = 5,
    METHOD_6 = 6,
    METHOD_7 = 7,
    METHOD_8 = 8,
    METHOD_9 = 9,
    METHOD_10 = 10,
    METHOD_11 = 11,
    METHOD_12 = 12,
    METHOD_13 = 13,
    METHOD_14 = 14,
    METHOD_15 = 3,
    METHOD_16 = 7,
};

class NewInspectorAgent;

class QuestionAnswer {
    friend class NewInspectorAgent;

private:
    Query m_Query1; // +0x000
    Query m_Query2; // +0x00c
    RGBA m_Answer; // +0x018
    unsigned m_obfuscationBit; // +0x028
    unsigned m_obfuscationCounter; // +0x02c
    AES m_AES; // +0x030

public:
    static const unsigned SR_ARRAY_SIZE = 16;

    unsigned SendReceiveValues[SR_ARRAY_SIZE]; // +0x4a8
    char rawSendReceiveValues[64]; // +0x4e8
    char rawResultSendReceiveValues[64]; // +0x528

    QuestionAnswer();

    void MakeRequestQuery(const Query& query1, const Query& query2, const RGBA& answer);
    void ResponseResultQuery();

    bool ValidateResult() {
        return SendReceiveValues[0] == m_Answer.r && SendReceiveValues[1] == m_Answer.g &&
            SendReceiveValues[2] == m_Answer.b && SendReceiveValues[3] == m_Answer.a;
    }

private:
    unsigned GetObfuscationBitSlotIndex(unsigned counter);
    ObfuscationMethodType GetObfuscationMethodByRequest(unsigned arrayNo);
    ObfuscationMethodType GetObfuscationMethodByResponse(unsigned response);
    void XORByInitialVector();
    void Obfuscate();
    void Deobfuscate();
    int MakeObfuscationArrayNo();
    void SetRequest(const Query& query1, const Query& query2);
    void SetAnswer(const RGBA& answer);
    void Decrypt();
    void Encrypt();
};


static_assert(std::is_trivially_copyable_v<RGBA>);
