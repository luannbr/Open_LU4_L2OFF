#pragma once

#include "Agit.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

class CDBSocket;
class CPledge;
class CUserSocket;
class User;

namespace BuilderCmd {
bool CompeteAgit(CUserSocket*, User*, char16_t*);
}

namespace AgitSiegeStatus {

enum status : std::int32_t {
    START = 0,
    RESTART = 1,
    CODE_MAX = 2,
};

} // namespace AgitSiegeStatus


namespace CompeteAgitResult {

enum Reason : std::int32_t {
    SUCCESS = 0,
    DUPLICATED_AGIT = 1,
    NO_PLEDGE = 2,
    INVALID_PLEDGE_LEVEL = 3,
    CODE_MAX = 4,
};

} // namespace CompeteAgitResult


// Exact current status carrier embedded at CCompeteAgit+0x498. Win64
// `unsigned long` is represented by the fixed 32-bit elapsed-time lanes.
class CCompeteAgitStatus final {
public:
    enum state : std::int32_t {
        NOTHING = -1,
        PREPARE = 0,
        PROCESS = 1,
        BOSSKILL = 2,
        CLEAN_UP = 3,
        STATUS_MAX = 4,
    };

    bool UpdateState();
    void SetBosKill();

private:
    friend bool BuilderCmd::CompeteAgit(CUserSocket*, User*, char16_t*);
    friend class CCompeteAgit;
    friend class CCompeteAgitDBHandler;

    state CalcState() const;
    std::int32_t m_SecProgress; // +0x00, shipped spelling
    state m_State; // +0x04
    std::uint32_t m_msecElapsed; // +0x08
    std::uint32_t m_msecLastUpdate; // +0x0c
    bool m_bPause; // +0x10
    std::byte m_alignment11To14[3]; // +0x11..+0x13
};


// Current concrete competition-agit owner. Its current vtable has 57
// entries and differs from CWorldAgit only in Release/destruction,
// TimerExpired, StartTimer, EndSiege and the three competition query/casts.
class CCompeteAgit final : public CWorldAgit {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CCompeteAgit)

    CCompeteAgit();
    ~CCompeteAgit() override;

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()
    void TimerExpired(int id) override;

    bool IsCompetitionAgit() const override;
    bool IsBattleTypeAgit() const override;
    CCompeteAgit* CastCompeteAgit() const override;

    static CCompeteAgit* GetCompeteAgit(int residenceId);
    void LoadFromDB(int elapsedMilliseconds);
    void StartTimer() override;
    void PrepareSiege();
    void StartAgitSiege(AgitSiegeStatus::status status);
    void EndSiege() override;
    void ClearGame();
    void HandleState();
    std::int32_t GetNextSiegeTime();
    CompeteAgitResult::Reason CanBeOwner(CPledge* pledge);
    bool OnSetWinner(CPledge* pledge, bool accepted);
    void SetPreviouseOwnerFromDB(unsigned int pledgeId);
    void ShowInfoByBuilderCmd(CUserSocket* socket);

private:
    friend bool BuilderCmd::CompeteAgit(CUserSocket*, User*, char16_t*);
    friend class CCompeteAgitDBHandler;

    CCompeteAgitStatus m_StateHelper; // +0x498
    std::uint32_t m_nPreviousOwnerDBID; // +0x4ac
    std::uint32_t m_nWinnerPledgeDbId; // +0x4b0
    bool m_bAcceptOwner; // +0x4b4
    std::byte m_alignment4b5To4b8[3]; // +0x4b5..+0x4b7

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CCompeteAgit)
};


class CCompeteAgitDBHandler final {
public:
    static bool ReplyResetCompeteAgit(CDBSocket* socket, const unsigned char* packet);
    static bool ReplyLoadCompeteAgit(CDBSocket* socket, const unsigned char* packet);
};
