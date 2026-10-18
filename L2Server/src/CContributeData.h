#pragma once

#include "CGeneralObject.h"

#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>


class CCreature;
class CMultiPartyCommandChannel;
class CNPC;
class CParty;
class User;

// Exact current L2Server base contribution envelope. The four tree domains
// own their nodes and sentinels. The embedded lock and scalar state are
// local values. The lifecycle and slot-154 pickup policy are published as
// one owner-complete base-class slice.
class CContributeData : public CGeneralObject {
public:
    using PartyContributeMap = std::map<CParty*, double>;
    using MPCCContributeMap = std::map<CMultiPartyCommandChannel*, double>;
    using SidContributeMap = std::map<std::uint32_t, double>;

    enum ExpPointType : std::int32_t {
        EXP_TYPE = 0,
        SP_TYPE = 1,
    };

    CContributeData();
    ~CContributeData() override;

    void TimerExpired(int id) override;
    bool IsContributeData() override;
    CContributeData* CastContributeData() override;

    virtual bool PickableIn15Sec(CCreature* creature);

    bool PickableIn5Sec(CCreature* creature);

    void SetSubContributeUserCountForBuilder(int count);
    void SetMPCCRouting(std::uint32_t partySid);
    std::uint32_t GetMPCCRouting();
    int AddUltimateSkillPoint(CCreature* contributor, CCreature* target);
    void GetBonusItemByLuckyStat(User* user, CCreature* victim, std::int64_t experience);
    double CalcExpBonus(double value, double multiplier, CCreature* creature);
    double CalcSpBonus(double value, double multiplier, CCreature* creature);
    double CalcSubJobExpAdjustment(double value, double multiplier, User* user);
    double CalcSubJobSpAdjustment(double value, double multiplier, User* user);
    void AccumulateContribute(CCreature* contributor, double contribution);
    void AccumulateSubContribute(CCreature* contributor, int contribution);
    void AccumulateEventContribute(CCreature* contributor, int contribution);
    std::uint32_t GetGreatEventContributorID();
    std::int32_t GetPersonalGreatContributorLevel() const noexcept { return m_PersonalGreatContributorLevel; }
    void Clear();
    void SetMPCCMasterPartyRouting();
    double GetContributeInfos(std::map<std::uint32_t, double>& contributes);
    void GetSubContributeInfos(std::map<std::uint32_t, std::int32_t>& contributes);
    void ArrangePartyContribute(std::map<CParty*, double>& contributes, CParty* party, double contribution);
    void ArrangeMPCCContribute(
        std::map<CMultiPartyCommandChannel*, double>& contributes, CMultiPartyCommandChannel* channel, double contribution
    );
    double GetContributesPremiumDropBonus();
    double GetContributesPremiumVitalItemDropBonus();
    int GetContributesDropRateByRune(int runeType);
    int GetContributesDropAmountByRune(int runeType);
    static double GetExpPenalty(CCreature* creature, int targetLevel);
    bool CheckSubContirbutorForDamages(User* user, double contribution);
    bool CheckSubContirbutorForSkills(User* user, int contribution);
    void AdjustChinaFatigueSystem(
        CCreature* creature,
        std::int64_t& experience,
        std::int64_t& sp,
        int& raidPoint,
        std::int64_t& petExperience,
        std::int64_t& petSp
    );
    void AdjustChinaFatigueSystem(CCreature* creature, std::int64_t& experience, std::int64_t& sp);
    void AdjustChinaFatigueSystem(CCreature* creature, double& first, double& second, double& third);
    void DistributeSubExpSp(CCreature* creature);
    std::int64_t GetAdjustedExp(
        double value,
        double multiplier,
        User* user,
        CCreature* creature,
        double flatBonus,
        ExpPointType type,
        bool forceSummon
    );
    void CalculateExpSpBonus(
        std::int64_t& expBonus,
        std::int64_t& spBonus,
        std::int64_t& pcCafeExpBonus,
        std::int64_t& pcCafeSpBonus,
        double& eventRate,
        double baseExp,
        double baseSp,
        User* user,
        CCreature* creature,
        std::uint32_t forceSummon
    );
    void DistributeToOneContributor(
        MPCCContributeMap& mpccContributes,
        PartyContributeMap& partyContributes,
        double& greatestContribution,
        int contributorSid,
        double contribution,
        CCreature* victim,
        double totalContribution,
        double baseExp,
        double baseSp,
        double raidPoint,
        CCreature* killer,
        int damagedSeconds,
        int contributionCount,
        std::uint32_t tick
    );
    void DistributeMPCC(
        CCreature* victim,
        MPCCContributeMap& mpccContributes,
        SidContributeMap& directContributes,
        double& greatestContribution,
        double totalContribution,
        double baseExp,
        double baseSp,
        double raidPoint,
        std::uint32_t tick,
        int damagedSeconds,
        int contributionCount
    );
    void Distribute(CCreature* victim, CCreature* killer);


protected:
    // Current CNPC::DropItemAdena reads the selected contributor directly
    // from +0xc8 before resolving its User owner.
    friend class CNPC;

    ::CYieldLock m_Sect;
    std::map<std::uint32_t, double> m_Contributes;
    std::map<std::uint32_t, std::int32_t> m_SubContributes;
    std::uint32_t m_GreatContributor;
    std::byte padding_a4[4];
    std::set<std::uint32_t> m_Contributors;
    std::uint32_t m_PersonalGreatContributor;
    std::int32_t m_PersonalGreatContributorLevel;
    std::int32_t m_PersonalGreatContribute;
    std::uint32_t m_nMPCCRouting;
    std::map<std::uint32_t, double> m_EventContributes;
    double m_dTotalDamage;
    std::int32_t m_nSubCountributeUserCount;
    std::byte padding_104[4];
};


// These three current policies add no state except the UserDie pledge dword.
// Their User-dependent constructors and slot-154 policy bodies keep the
// current master/party/pledge identity rules.
class CContributeDataForOne : public CContributeData {
public:
    explicit CContributeDataForOne(User* user);
    ~CContributeDataForOne() override;
};

class CContributeDataForEvent : public CContributeData {
public:
    explicit CContributeDataForEvent(User* user);
    ~CContributeDataForEvent() override;

    bool PickableIn15Sec(CCreature* creature) override;
};

class CContributeDataForUserDie : public CContributeData {
public:
    explicit CContributeDataForUserDie(User* user);
    ~CContributeDataForUserDie() override;

    bool PickableIn15Sec(CCreature* creature) override;

protected:
    std::uint32_t m_nPledgeID;
    std::byte padding_10c[4];
};
