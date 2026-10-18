#pragma once

#include <cstddef>
#include <cstdint>


class CDBSocketOp {
public:
    explicit CDBSocketOp(int type = 0);
    virtual ~CDBSocketOp();

    void SendHome(const std::uint8_t* packet);
    void PledgeLev(const std::uint8_t* packet);
    void SetSkillAll(const std::uint8_t* packet);
    void ManIntAnn(const std::uint8_t* packet);
    void ModWeekPlay(const std::uint8_t* packet);
    void ChangeCharName(const std::uint8_t* packet);
    void GmReg(const std::uint8_t* packet);
    void CreateSubjob(const std::uint8_t* packet);
    void SetInstantAnnounce(const std::uint8_t* packet);
    void ModDepositedSSQItem(const std::uint8_t* packet);
    void CreatePet(const std::uint8_t* packet);
    void OustPledgeMember(const std::uint8_t* packet);
    void DelPledgeAnnounce(const std::uint8_t* packet);
    void DelPledgeEmblem(const std::uint8_t* packet);
    void SetNobless(const std::uint8_t* packet);
    void SetHero(const std::uint8_t* packet);
    void AddPledgeSkill(const std::uint8_t* packet);
    void ModPledgeSkill(const std::uint8_t* packet);
    void DelPledgeSkill(const std::uint8_t* packet);
    void LoadNobless(const std::uint8_t* packet);
    void AddSubPledgeSkill(const std::uint8_t* packet);
    void DelSubPledgeSKill(const std::uint8_t* packet);
    void ModSubPledgeSkill(const std::uint8_t* packet);
    void BroadcastInstantAnnounce(const std::uint8_t* packet);
    void SetPetitionDisableTime(const std::uint8_t* packet);
    void SpawnEventNpc(const std::uint8_t* packet);
    void CancelCommission(const std::uint8_t* packet);
    void BroadcastScreenAnnounce(const std::uint8_t* packet);
    void IBroadcastScreenAnnounce(const std::uint8_t* packet);
    void TakeGMAnnounce(const std::uint8_t* packet);

private:
    int m_nType; // +0x08
};
