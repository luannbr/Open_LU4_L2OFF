#pragma once

#include "Shared/src/IOC.h"

#include <cstddef>
#include <cstdint>

class CUserSocket;
class User;

bool BuilderCmd_setIntervalAnnounce(CUserSocket* socket, User* user, char16_t* command);


class CAnnounceTimer final : public CIOObject {
public:
    CAnnounceTimer();
    ~CAnnounceTimer() override;

    L2_DECLARE_OBJECT_TYPE(CAnnounceTimer)

    static CAnnounceTimer* Inst();

    void Init();
    void ClearIntervalMsg();
    void SetIntervalMsg(int interval, int announceId, char16_t* message);

private:
    friend bool BuilderCmd_setIntervalAnnounce(CUserSocket* socket, User* user, char16_t* command);

    void ShowIntervalMsg(int intervalIndex);
    void IntervalAnnounceCheck();
    void TimerExpired(int id) override;

    bool bShowIntervalAnnounce; // +0x0018
    std::uint8_t m_padding0019[3]; // +0x0019
    std::uint32_t m_nLastCheckedMin; // +0x001c
    char16_t wszIntervalMsg[10][10][80]; // +0x0020
};
