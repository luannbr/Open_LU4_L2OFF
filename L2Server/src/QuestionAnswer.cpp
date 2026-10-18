#include "QuestionAnswer.h"

#include "L2Core/src/inc/Guard.h"

#include <cstdlib>
#include <cstring>
#include <utility>

namespace {

const ObfuscationMethodType obfuscationMap[4][4] = {
    {METHOD_5, METHOD_14, METHOD_10, METHOD_11},
    {METHOD_8, METHOD_13, METHOD_1, METHOD_7},
    {METHOD_7, METHOD_2, METHOD_12, METHOD_9},
    {METHOD_3, METHOD_6, METHOD_15, METHOD_4},
};

const unsigned obfuscationSlot[8] = {
    3,
    1,
    7,
    6,
    2,
    0,
    5,
    8,
};

const int swapMask[4] = {
    1,
    2,
    4,
    8,
};

const unsigned obfuscationBitMask[4] = {
    0x1000,
    0x0400,
    0x0020,
    0x0008,
};

const char* key = "flslwl2rkWkddlwl";
const char* AES_CHAIN = "mr.andersonwelcometotherealworld";

} // namespace

QuestionAnswer::QuestionAnswer() : m_obfuscationCounter(0) { m_AES.MakeKey(key, AES_CHAIN, 16, 32); }

unsigned QuestionAnswer::GetObfuscationBitSlotIndex(unsigned counter) { return obfuscationSlot[counter & 7]; }

ObfuscationMethodType QuestionAnswer::GetObfuscationMethodByRequest(unsigned arrayNo) {
    return obfuscationMap[m_obfuscationCounter & 3][arrayNo];
}

ObfuscationMethodType QuestionAnswer::GetObfuscationMethodByResponse(unsigned response) {
    unsigned arrayNo = 0;
    for (unsigned index = 0; index < 4; ++index) {
        if ((obfuscationBitMask[index] & response) != 0) {
            arrayNo = index;
            break;
        }
    }
    return obfuscationMap[m_obfuscationCounter & 3][arrayNo];
}

int QuestionAnswer::MakeObfuscationArrayNo() {
    const unsigned randomBits = static_cast<unsigned>(std::rand());
    const unsigned randomArray = static_cast<unsigned>(std::rand());
    m_obfuscationBit = obfuscationBitMask[randomArray & 3] | (randomBits & 0xffffebd7U);
    return static_cast<int>(randomArray & 3);
}

void QuestionAnswer::XORByInitialVector() {
    for (unsigned index = 0; index < SR_ARRAY_SIZE; ++index) {
        if (index != 9) {
            SendReceiveValues[index] ^= SendReceiveValues[9];
        }
    }
}

void QuestionAnswer::SetRequest(const Query& query1, const Query& query2) {
    L2_GUARD_FUNCTION();

    std::memset(SendReceiveValues, 0, sizeof(SendReceiveValues));
    std::memset(rawSendReceiveValues, 0, sizeof(rawSendReceiveValues));
    std::memset(rawResultSendReceiveValues, 0, sizeof(rawResultSendReceiveValues));
    m_Query1 = query1;
    m_Query2 = query2;
}

void QuestionAnswer::SetAnswer(const RGBA& answer) { m_Answer = answer; }

void QuestionAnswer::Obfuscate() {
    L2_GUARD_FUNCTION();

    const unsigned bitSlot = GetObfuscationBitSlotIndex(m_obfuscationCounter);
    const unsigned arrayNo = static_cast<unsigned>(MakeObfuscationArrayNo());
    const ObfuscationMethodType method = GetObfuscationMethodByRequest(arrayNo);

    SendReceiveValues[0] = m_Query1.x;
    SendReceiveValues[1] = m_Query1.y;
    SendReceiveValues[2] = m_Query2.x;
    SendReceiveValues[3] = m_Query2.y;
    SendReceiveValues[4] = m_obfuscationBit;
    SendReceiveValues[5] = static_cast<unsigned>(std::rand()) & 0xff;
    SendReceiveValues[6] = static_cast<unsigned>(std::rand()) & 0xff;
    SendReceiveValues[7] = static_cast<unsigned>(std::rand()) & 0xff;
    SendReceiveValues[8] = static_cast<unsigned>(std::rand()) & 0xff;
    SendReceiveValues[9] = static_cast<unsigned>(std::rand());
    SendReceiveValues[10] = m_Query1.textureNo;
    SendReceiveValues[11] = m_Query2.textureNo;
    SendReceiveValues[12] = static_cast<unsigned>(std::rand());
    SendReceiveValues[13] = static_cast<unsigned>(std::rand() % 15);
    SendReceiveValues[14] = static_cast<unsigned>(std::rand() % 15);
    SendReceiveValues[15] = static_cast<unsigned>(std::rand());

    for (unsigned index = 0; index < 4; ++index) {
        if ((swapMask[index] & method) != 0) {
            std::swap(SendReceiveValues[index], SendReceiveValues[index + 5]);
            if (index < 2) {
                std::swap(SendReceiveValues[index + 10], SendReceiveValues[index + 13]);
            }
        }
    }

    std::swap(SendReceiveValues[4], SendReceiveValues[bitSlot]);
}

void QuestionAnswer::Deobfuscate() {
    L2_GUARD_FUNCTION();

    const unsigned bitSlot = GetObfuscationBitSlotIndex(m_obfuscationCounter);
    std::swap(SendReceiveValues[4], SendReceiveValues[bitSlot]);

    const ObfuscationMethodType method = GetObfuscationMethodByResponse(SendReceiveValues[4]);
    for (unsigned index = 0; index < 4; ++index) {
        if ((swapMask[index] & method) != 0) {
            std::swap(SendReceiveValues[index], SendReceiveValues[index + 5]);
            if (index < 2) {
                std::swap(SendReceiveValues[index + 10], SendReceiveValues[index + 13]);
            }
        }
    }
}

void QuestionAnswer::Decrypt() {
    L2_GUARD_FUNCTION();

    m_AES.Decrypt(rawSendReceiveValues, rawResultSendReceiveValues, sizeof(rawSendReceiveValues), AES::CBC);
    std::memcpy(SendReceiveValues, rawResultSendReceiveValues, sizeof(SendReceiveValues));
}

void QuestionAnswer::Encrypt() {
    L2_GUARD_FUNCTION();

    std::memcpy(rawSendReceiveValues, SendReceiveValues, sizeof(SendReceiveValues));
    m_AES.Encrypt(rawSendReceiveValues, rawResultSendReceiveValues, sizeof(rawSendReceiveValues), AES::CBC);
}

void QuestionAnswer::MakeRequestQuery(const Query& query1, const Query& query2, const RGBA& answer) {
    L2_GUARD_FUNCTION();

    SetRequest(query1, query2);
    SetAnswer(answer);
    Obfuscate();
    XORByInitialVector();
    Encrypt();
    ++m_obfuscationCounter;
}

void QuestionAnswer::ResponseResultQuery() {
    L2_GUARD_FUNCTION();

    Decrypt();
    XORByInitialVector();
    Deobfuscate();
    ++m_obfuscationCounter;
}
