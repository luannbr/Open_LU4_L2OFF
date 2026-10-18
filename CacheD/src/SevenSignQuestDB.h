#pragma once

#include "Shared/src/SSQ.h"
#include "Shared/src/RWLock.h"

#include <cstddef>
#include <cstdint>

class CAdminSocket;
bool ModifyDepositedSSQItemPacket(CAdminSocket* socket, const std::uint8_t* packet);
class DxChangeSSQPeriod;
class DxStartSSQAccount;
class DxStartSSQSealEffect;
class DxChangeSSQStatus;
class DxSetSevenSignQuestResult;
class DxCloseSevenSignQuest;
class DxAddSevenSignQuestMember;
class DxChangeSealSelectionCount;
class DxChangeSSQUserPoint;
class DxStartQuickCycle;
class DxChangeMainEventPoint;
class DxChangeSealOwner;
class DxChangeSSQWinner;
class DxDismissSevenSignMember;
class DxChangeCycleMode;
class DxDepositSevenSignQuestItemEx;

// Shipping owner: CacheD/SevenSignQuestDB.{h,cpp}. The lazy singleton at
// 0x02a2ddc0 occupies exactly 0x140 bytes. Every field below is addressed by
// current Cached.exe code and mirrors the SharedSSQInfo wire payload.
// Current external handlers also call the offset-zero RWLock base directly.
// C++ access control is not encoded in machine code. Public inheritance is the
// minimal source reconstruction and does not alter this ABI or layout.
class SevenSignQuestDB : public RWLock {
public:
    SevenSignQuestDB();
    ~SevenSignQuestDB();

    SevenSignQuestDB(const SevenSignQuestDB&) = delete;
    SevenSignQuestDB& operator=(const SevenSignQuestDB&) = delete;

    // Current accessor 0x00596840 is an unsymbolized lazy static. The
    // source-level identifier is unavailable. Instance describes the exact
    // lifetime and ownership instead of introducing a second global.
    static SevenSignQuestDB& Instance();

    void SetEventStartTime(time_l2_t time);
    bool SetEventEndTime(time_l2_t time);
    void SetSealEffectTime(time_l2_t time);
    bool SetSealEffectEndTime(time_l2_t time);
    bool SetCurrentStatus(SSQStatus::type status);
    bool SetSealOwner(int sealNumber, SSQPart::type part);
    bool SetMainEventPoint(SSQPart::type part, std::int64_t point);
    bool SetCollectedPoint(SSQPart::type part, std::int64_t point);
    bool SetDropGuardStatus(bool canDropGuard);

    int AssembleAll(char* outputBuffer, int bufferSize);
    bool SaveSystemInfo();
    bool SaveJoinInfo(SSQPart::type part);
    bool LoadJoinInfo(int roundNumber);

    // The guard literal says CreateRound() although current machine code
    // consumes all sixteen persisted values. The explicit signature below
    // follows the actual ABI and lin_CreateSSQRound contract.
    bool CreateRound(
        int roundNumber,
        SSQStatus::type status,
        SSQPart::type winner,
        time_l2_t eventStartTime,
        time_l2_t eventEndTime,
        time_l2_t sealEffectTime,
        time_l2_t sealEffectEndTime,
        SSQPart::type seal1,
        SSQPart::type seal2,
        SSQPart::type seal3,
        SSQPart::type seal4,
        SSQPart::type seal5,
        SSQPart::type seal6,
        SSQPart::type seal7,
        time_l2_t castleSnapshotTime,
        bool canDropGuard
    );

    bool StartEvent(int roundNumber);
    bool Load();

    // Inlined at current startup 0x00459f95: the shipping code reads +0x3c
    // immediately after Load and passes it to TimeAttackDB::Load.
    int GetRoundNumber() const noexcept { return m_roundNumber; }

private:
    // Current AQ 87 reads the selected side's collected-point slot directly
    // while holding this owner lock. Friendship keeps that relationship
    // without publishing a non-target accessor.
    friend bool ModifyDepositedSSQItemPacket(CAdminSocket* socket, const std::uint8_t* packet);
    friend class DxChangeSSQPeriod;
    friend class DxStartSSQAccount;
    friend class DxStartSSQSealEffect;
    friend class DxChangeSSQStatus;
    friend class DxSetSevenSignQuestResult;
    friend class DxCloseSevenSignQuest;
    friend class DxAddSevenSignQuestMember;
    friend class DxChangeSealSelectionCount;
    friend class DxChangeSSQUserPoint;
    friend class DxStartQuickCycle;
    friend class DxChangeMainEventPoint;
    friend class DxChangeSealOwner;
    friend class DxChangeSSQWinner;
    friend class DxDismissSevenSignMember;
    friend class DxChangeCycleMode;
    friend class DxDepositSevenSignQuestItemEx;

    bool m_loaded; // +0x038
    int m_roundNumber; // +0x03c
    SSQStatus::type m_currentStatus; // +0x040
    SSQPart::type m_winner; // +0x044
    SSQPart::type m_previousWinner; // +0x048
    SSQPart::type m_sealOwner[8]; // +0x04c
    time_l2_t m_eventStartTime; // +0x06c
    time_l2_t m_eventEndTime; // +0x070
    time_l2_t m_sealEffectTime; // +0x074
    time_l2_t m_sealEffectEndTime; // +0x078
    std::int64_t m_point[3]; // +0x080
    std::int64_t m_collectedPoint[3]; // +0x098
    std::int64_t m_mainEventPoint[3]; // +0x0b0
    int m_memberCount[3]; // +0x0c8
    int m_sealSelectionCount[3][8]; // +0x0d4
    time_l2_t m_castleSnapshotTime; // +0x134
    bool m_canDropGuard; // +0x138
};
