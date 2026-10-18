#pragma once

#include "CDes.h"

#include <cstdint>

class CGenerator {
public:
    CGenerator(char* generatorKey);
    ~CGenerator();

    bool GetRegisterNumber(
        char* registerNumber,
        std::uint32_t numberLength);

    static bool GetCardKeyValue(
        int cardNumber,
        int key,
        char* value,
        std::uint32_t valueLength);
    static int GetCardKeyValue(int cardNumber, int key);

private:
    CDes m_genDes;
};


extern char* GENERATE_KEY;
