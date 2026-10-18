#pragma once

#include "Shared/src/EtcMani.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/PledgeMasteryID.h"
#include "Shared/src/PledgeType.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

struct XMLElement;
class AtomicJob;
class AtomicLambdaJob;
class CDX_ATOMICJOB_BASE;
class CObject;
class CPledge;
class CUserSocket;
class User;
struct PledgeCostInfo;
template <std::size_t Capacity>
class CPacketHelperEx;

enum S_EX_PROTOCOL : std::int32_t;

namespace network {
struct IPacket;
}

namespace PledgeStuff {
namespace ActionStep {

enum Value : std::int32_t {
    Try = 0,
    Success = 1,
};

} // namespace ActionStep

int GetAttributeAsClassID(const XMLElement* element, const std::u16string& attributeName);

// Current PledgeStuff.cpp 0x009e16d4 owner. Both target vectors are the
// four-word NCSoft/MSVC xvector carrier, yielding the exact 0x50-byte helper.
class PledgeWarehouseManipHelper final {
public:
    PledgeWarehouseManipHelper(User* user, CPledge* pledge);
    ~PledgeWarehouseManipHelper();

    void AddItem(int itemId, std::int64_t count);
    void AppendEtcMani(EtcMani* etcMani);
    void Do(AtomicLambdaJob* job);

private:
    User* m_pUser; // +0x00
    CPledge* m_pPledge; // +0x08
    std::vector<CreateItemStruct> m_items; // +0x10
    std::vector<EtcMani*> m_etcManis; // +0x30
};

class PledgeCostHelper final {
public:
    PledgeCostHelper(User* user, CPledge* pledge);
    ~PledgeCostHelper();

    void SetCost(const PledgeCostInfo& cost);
    void AppendEtcMani(EtcMani* etcMani);
    void Do(AtomicLambdaJob* job);

private:
    void NeedItem(int itemClassId, std::int64_t amount);
    void SetNameValue(int nameValue);

    User* m_pUser; // +0x00
    CPledge* m_pPledge; // +0x08
    int m_nNameValue; // +0x10
    [[maybe_unused]] std::uint32_t m_padding14To18{};
    std::vector<CreateItemStruct> m_items; // +0x18
    std::vector<EtcMani*> m_etcManis; // +0x38
};

class ItemManipHelper final {
public:
    ItemManipHelper(User* user, ItemGetReason::Enum reason);
    ~ItemManipHelper();

    void PushManiplutor(int itemClassId, std::int64_t amount);
    void PushPointEtcMani(UserPointType pointType, std::int64_t amount);
    void PushEtcMani(EtcMani* etcMani);
    void Do(AtomicLambdaJob* job);

private:
    User* m_pUser; // +0x00
    ItemGetReason::Enum m_eReason; // +0x08
    [[maybe_unused]] std::uint32_t m_padding0cTo10{};
    std::vector<CreateItemStruct> m_items; // +0x10
    std::vector<EtcMani*> m_etcManis; // +0x30
};

bool IsValidPledgeType(PledgeType pledgeType);
bool IsValidMasteryID(PledgeMasteryID::Type masteryId) noexcept;
bool IsValidItem(const CObject* object);
bool GiveItem(User* user, int itemClassId, std::int64_t amount);
bool GiveNameValue(User* user, int amount);
std::int32_t CalculateExpiryTime(int seconds);
void AddPrivilegeByLevelAndMastery(CPledge* pledge, bool synchronizeRaid);
void RemovePrivilegeByMastery(CPledge* pledge);
void UpdateNameValueOf(CPledge* pledge, int amount);

// Current L2Server 0x009e1040. The original private source spelling is
// unavailable. This descriptive spelling names the proved cross-TU callable
// boundary used by ReplyCreatePledgeByNpc.
void SetDefaultPledgePower(CPledge* pledge, int grade);

void DestroyJob(AtomicJob* job);
void DoJob(AtomicJob* job, int result, bool strict);
void DoJobDx(AtomicJob* job, CDX_ATOMICJOB_BASE* packet, bool strict);
void DoJobPacket(AtomicJob* job, network::IPacket* packet, bool strict);

void SendPacket(const User* user, S_EX_PROTOCOL extendedProtocol, network::IPacket& packet);
void SendPacket(const User* user, const CPacketHelperEx<20480>& packet);

int Assemble_S_EX_PLEDGE_SHOW_INFO_UPDATE(char* buffer, int bufferSize, CPledge* pledge);
void Send_S_EX_PLEDGE_SHOW_INFO_UPDATE(User* user, CPledge* pledge);
void SendSystemMessage(CUserSocket* socket, const std::u16string& message);
void SendSystemMessage(User* user, const std::u16string& message);
void SendSystemMessage_MasteryID(CPledge* pledge, int messageId, PledgeMasteryID::Type masteryId);
void SendSystemMessage_Skill(CPledge* pledge, int messageId, int skillId, int skillLevel, int skillSubLevel);
void SendSystemMessage_Item(CPledge* pledge, int messageId, int itemClassId);
void SendSystemMessage_UsernameAndItem(CPledge* pledge, int messageId, const char16_t* username, int itemClassId);

void Write_LOG_PLEDGE_CONTRIB_ADD(const User* user, const CPledge* pledge, int point);
void Write_LOG_PLEDGE_CONTRIB_PERIOD_CHANGE(int season, std::int32_t nextSeasonTime);
void Write_LOG_PLEDGE_CONTRIB_REWARD(
    const User* user, const CPledge* pledge, int step, int point, int minimumPoint, ActionStep::Value action
);
void Write_LOG_PLEDGE_ITEM_ADD(const CPledge* pledge, int itemClassId, int expiryTimeSeconds);
void Write_LOG_PLEDGE_ITEM_ACTIVATE(
    const User* user, const CPledge* pledge, int itemClassId, int expiryTimeSeconds, ActionStep::Value action
);
void Write_LOG_PLEDGE_ITEM_BUY(const User* user, const CPledge* pledge, int itemClassId, ActionStep::Value action);
void Write_LOG_PLEDGE_ITEM_REMOVE(const CPledge* pledge, int itemClassId);
void Write_LOG_PLEDGE_LEVEL_UP(const User* user, const CPledge* pledge, int level, ActionStep::Value action);
void Write_LOG_PLEDGE_MASTERY_RESET(const User* user, const CPledge* pledge, ActionStep::Value action);
void Write_LOG_PLEDGE_MASTERY_SET(
    const User* user, const CPledge* pledge, PledgeMasteryID::Type masteryId, int point, ActionStep::Value action
);
void Write_LOG_PLEDGE_SKILL_ACTIVATE(
    const User* user,
    const CPledge* pledge,
    int skillId,
    int skillLevel,
    int remainSecond,
    int expirySecond,
    ActionStep::Value action
);
void Write_LOG_PLEDGE_SKILL_ADD(const CPledge* pledge, int skillId, int skillLevel, int expirySecond);
void Write_LOG_PLEDGE_SKILL_REMOVE(const CPledge* pledge, int skillId);

} // namespace PledgeStuff
