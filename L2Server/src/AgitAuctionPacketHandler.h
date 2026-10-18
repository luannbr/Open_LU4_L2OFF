#pragma once

#include <cstddef>
#include <cstdint>

class CUserSocket;
class CWorldAgit;

// Current L2Server's concrete C_EX_AGITAUCTION_CMD owner. The vtable has
// only the virtual destructor pair. The socket is the sole data member.
class CAgitAuctionPacketHandler {
public:
    explicit CAgitAuctionPacketHandler(CUserSocket* userSocket);
    virtual ~CAgitAuctionPacketHandler();

    bool Process(int command, const std::uint8_t* packet);

    void AgitAuctionInitInfo();
    void AgitAuctionBidStep1(int residenceId);
    void AgitAuctionBidStep2(int residenceId, std::int64_t price);
    void AgitAuctionBidStep3(int residenceId, std::int64_t price);
    void AgitAuctionReBidStep1(int residenceId);
    void AgitAuctionBidCancelConfirm(int residenceId);
    void AgitAuctionBidCancelProc(int residenceId);
    void AgitDetailInfo(int residenceId);
    void AgitAuctionList(int page);
    void MyAgitList();
    void AgitAuctionRegisterStep1();
    void AgitAuctionRegisterStep2(int residenceId, std::int64_t price, int periodDays);
    void AgitAuctionRegisterStep3(int residenceId, std::int64_t price, int periodDays);
    void AgitAuctionRegisterCancelConfirm();
    void AgitAuctionRegisterCancelProc();

private:
    int GetAgitType(int residenceId);
    CWorldAgit* GetPledgeWorldAgit(int flags, bool report);

    CUserSocket* m_pUserSocket; // +0x08
};
