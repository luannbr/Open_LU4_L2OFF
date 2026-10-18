#include "ParserArmorEnchantBonus.h"
#include "ParserWeaponEnchantEx.h"

#include "Item.h"

#include "Shared/src/Log.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/Utils.h"

#include <cstring>

extern int g_nEnchantedArmorBonus[][20];
extern double g_dEnchantWeaponExBonus[CRT_MAX][AT_MAX][2][VALIDATE::EXTRA_MAX];

void LoadArmorEnchantBonus() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\ArmorEnchantBonusData.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/ArmorEnchantBonusData.txt";
#endif

    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from %s file failed",
        //     L"Item_EnchantBonus.cpp",
        //     0x17,
        //     kFileName)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from %s file failed",
        //     L"Item_EnchantBonus.cpp",
        //     0x17,
        //     kFileName)
        L2_LOG_ERROR(Log, "Cannot read ArmorEnchantBonusData.txt; armor enchant bonuses are left unchanged.");
        return;
    }

    CParserForArmorEnchantBonus parser;
    if (!parser.Create()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForArmorEnchantBonus::create failed",
        //     L"Item_EnchantBonus.cpp",
        //     0x1f)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForArmorEnchantBonus::create failed",
        //     L"Item_EnchantBonus.cpp",
        //     0x1f)
        L2_LOG_ERROR(
            Log, "Cannot initialize the ArmorEnchantBonusData.txt parser; armor enchant bonuses are left unchanged."
        );
        return;
    }

    parser.m_lexer.yyin = input.GetStream();
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing ArmorEnchantBonus file failed after line[%d]",
        //     L"Item_EnchantBonus.cpp",
        //     0x2a,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing ArmorEnchantBonus file failed after line[%d]",
        //     L"Item_EnchantBonus.cpp",
        //     0x2a,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log,
            "Cannot parse ArmorEnchantBonusData.txt at script line %lld; assigned bonuses are retained and zero entries "
            "still inherit the preceding level.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }

    for (int grade = 0; grade < 11; ++grade) {
        for (int level = 1; level < 20; ++level) {
            if (g_nEnchantedArmorBonus[grade][level] == 0) {
                g_nEnchantedArmorBonus[grade][level] = g_nEnchantedArmorBonus[grade][level - 1];
            }
        }
    }
}

void LoadWeaponEnchantExBonus() {
    L2_GUARD_FUNCTION();

    std::memset(g_dEnchantWeaponExBonus, 0, sizeof(g_dEnchantWeaponExBonus));

#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\WeaponEnchantValidateEx.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/WeaponEnchantValidateEx.txt";
#endif

    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from %s file failed",
        //     L"Item_EnchantBonus.cpp",
        //     0x4b,
        //     kFileName)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from %s file failed",
        //     L"Item_EnchantBonus.cpp",
        //     0x4b,
        //     kFileName)
        L2_LOG_ERROR(Log, "Cannot read WeaponEnchantValidateEx.txt; weapon enchant extra bonuses remain cleared.");
        return;
    }

    CParserForWeaponEnchantExtraBonus parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForArmorEnchantBonus::create failed",
        //     L"Item_EnchantBonus.cpp",
        //     0x53)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForArmorEnchantBonus::create failed",
        //     L"Item_EnchantBonus.cpp",
        //     0x53)
        L2_LOG_ERROR(
            Log, "Cannot initialize the WeaponEnchantValidateEx.txt parser; weapon enchant extra bonuses remain cleared."
        );
        return;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing WeaponEnchantExtraBonus file failed after line[%d]",
        //     L"Item_EnchantBonus.cpp",
        //     0x5e,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing WeaponEnchantExtraBonus file failed after line[%d]",
        //     L"Item_EnchantBonus.cpp",
        //     0x5e,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log,
            "Cannot parse WeaponEnchantValidateEx.txt at script line %lld; already assigned weapon enchant extra bonuses "
            "are retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
}
