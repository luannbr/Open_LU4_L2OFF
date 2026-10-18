#pragma once

#include "Platform/Odbc.h"
#include "Platform/WindowsTypes.h"
#include "Types.h"

#include <cstdint>
#include <string_view>

enum class LocalAccountCreateResult {
    Created,
    AlreadyExists,
    InvalidName,
    InvalidPassword,
    DatabaseError,
    Unsupported,
};

// Original owner: Src/account.h. L2Authd.pdb type 0x62EE, sizeof 0x8F0.
// The public data sequence is kept unchanged. Several legacy handlers
// check block_flag, login_flag and uid immediately after Load/LoadAccount.
class CAccount {
public:
    CAccount();

    static LocalAccountCreateResult CreateLocalAccount(
        std::string_view accountName,
        std::string_view password);
    char Load(const char* accountName);
    char LoadPassword(
        const char* accountName,
        char* password,
        char* encryptionType,
        [[maybe_unused]] int newEncryptionType);
    char LoadEtc();
    char CheckPassword(
        const char* accountName,
        char* password,
        int newEncrypt,
        bool autoCreate = false);
    void Encrypt(
        int encryptionType,
        const char* accountName,
        const char* password,
        char* encryptedPassword);
    bool VerifyUnderAgeTimeLimit(int portalUid);
    char CheckBlockStatus();
    bool CheckAge();
    bool CheckGMUser(in_addr address);
    void SetPremiumLevel(int premiumLevel);
    bool CheckFatigueData();
    void SetFreeServer();
    char LoadAccount(const char* accountName);
    int MakeBlockInfo(char* output);

    std::int32_t block_flag;
    std::int32_t block_flag2;
    std::int32_t warn_flag;
    std::int32_t pay_stat;
    std::int32_t uid;
    std::int32_t login_flag;
    std::int32_t subscription_flag;
    std::int32_t remainTime;
    std::int32_t quotaTime;
    char age;
    char gender;
    char account[15];
    char lastworld;
    std::int32_t service_flag;
    std::int32_t card_number;
    std::uint32_t externalAuthAdditionalInfo;
    char forbidden_servers[16];
    std::int32_t active_flag;
    char auth_flag;
    std::int32_t fcid;
    char user_data[2048];
    std::int16_t user_data_len;
    char packageVersionInfo;
    TIMESTAMP_STRUCT block_end_date;
    unsigned char byteChildPlayTimeInfo[24];
    std::uint16_t user_level;
    std::int32_t lastLogoutTime;
    std::int32_t activateTime;
    char activateInformation[8];
    SecurityServiceFlag securityFlag;
    SecurityUsage securityUsage;
};
