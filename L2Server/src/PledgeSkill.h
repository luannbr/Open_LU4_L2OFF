#pragma once

#include "Protocol/src/L2Constants.h"
#include "Shared/src/Obj.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/Utils.h"

#include <cstddef>
#include <cstdint>

class User;
class CPledge;
class PledgeMastery;
struct PledgeSkillInfo;

template <std::size_t Capacity>
class CPacketHelperEx;


class PledgeSkill : public MemoryObject {
public:
    struct SkillNode final {
        int nLevel;
        std::int32_t tExpiryTime;
    };

    PledgeSkill();
    ~PledgeSkill() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    void RemoveSkillsFrom(User* user) const;
    void ApplySkillsTo(User* user, bool nameValueNegative, bool sendAcquireMessage, bool replaceSkill, bool refresh) const;
    void ApplySkillsTo(
        const std::set<std::uint32_t>& memberIds,
        bool nameValueNegative,
        bool sendAcquireMessage,
        bool replaceSkill,
        bool refresh
    ) const;
    bool CanAddSkill(int skillId, int skillLevel) const;
    bool AddSkill(int skillId, int skillLevel, std::int32_t expiryTime);
    bool HasSkill(int skillId) const;
    int GetRemainSecond(int skillId, std::int32_t now) const;
    bool RemoveSkill(int skillId);
    void GetSkillIDs(std::vector<int>& skillIds, bool masteryOnly) const;
    void SelectExpiredSkillIDs(std::int32_t now, std::vector<int>& skillIds) const;
    void Assemble(CPacketHelperEx<20480>& packet, int& skillCount) const;

    template <typename Function>
    void ForEach(Function function) const {
        L2_READ_GUARD(m_skillLock);
        for (const auto& entry : m_Skills) {
            function(entry.first, entry.second.nLevel);
        }
    }

    JobTransactionLock& GetTransactionLock();

private:
    friend class CPledge;

    L2_DECLARE_OBJECT_REGISTRATION(PledgeSkill)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(PledgeSkill)

    void _ApplySkillsTo(User* user, bool nameValueNegative, bool sendAcquireMessage, bool replaceSkill, bool refresh) const;
    bool _CanAddSkill(int skillId, int skillLevel) const;
    int _GetSkillLevel(int skillId) const;

    mutable RWLock m_skillLock;
    std::map<int, SkillNode> m_Skills;
    JobTransactionLock m_TransactionLock;
    std::uint32_t m_alignment7cTo80{};
};


namespace PledgeSkillStuff {

bool IsPledgeSkill(int skillId, int skillLevel);
bool CanAddSkill(const User* user, const PledgeSkillInfo* skillInfo);
bool HasRequirement(const PledgeSkillInfo* skillInfo, int pledgeLevel, const PledgeMastery* mastery);
PledgeActiveStatus::Value CheckSkillStatus(
    const PledgeSkill* skills, int pledgeLevel, const PledgeMastery* mastery, const PledgeSkillInfo* skillInfo
);
namespace Builder {
void SendPledgeSkillListTo(User* user);
}
void AddPledgeSkillsByLevelAndMastery(CPledge* pledge, bool synchronizeRaid);
void RequestToAddPledgeSkill(User* user, int skillId, int skillLevel);
void SendPledgeSkillInfoTo(User* user, int skillId, int skillLevel);

} // namespace PledgeSkillStuff
