#pragma once

#include "Shared/src/PledgeMasteryID.h"
#include "Shared/src/PledgeType.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CPledge;
class CCastle;
class CResidence;
class CAlliance;
class CHtmlGenerator;
class User;
enum eSMsgType : std::int32_t;
enum SayPacketType : std::int32_t;


namespace PledgeMission {
enum GoalType : std::int32_t;
}

template <std::size_t Capacity>
class CPacketHelperEx;

// Current PledgeMemberService.cpp polymorphic dispatcher. The service is a
// stack-owned command: the only state in the base is the continuation byte at
// +0x08, and its two command slots are complete at +0x08/+0x10.
class CPledgeMemberService {
public:
    CPledgeMemberService();
    virtual ~CPledgeMemberService();

    void Serve(User* user);
    bool CanServe() const noexcept { return m_canServe; }
    void ServeForRaid(CPledge& pledge);

protected:
    void StopServe() noexcept { m_canServe = false; }

private:
    virtual void ServeInternal(User* user) = 0;
    virtual void ServeInternalForRaid(CPledge& pledge) = 0;

    bool m_canServe;
    // Current constructor writes only m_canServe. These tail bytes remain
    // stack-indeterminate in every concrete service object.
    [[maybe_unused]] std::byte m_alignment09To10[7];
};

// Current PledgeMemberService.cpp command published by
// CPledge::SetCastleUnderSiege. Both nine-dword arrays are copied into each
// local User and into the raid-side command without normalization.
class SetCastleSiege final : public CPledgeMemberService {
public:
    SetCastleSiege(const std::int32_t (&castleIds)[9], const std::int32_t (&castleStatuses)[9]) noexcept;
    ~SetCastleSiege() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    std::int32_t m_castleIds[9]; // +0x10
    std::int32_t m_castleStatuses[9]; // +0x34
};


class SetFortressSiege final : public CPledgeMemberService {
public:
    SetFortressSiege(int defenceFortressId, int offenceFortressId) noexcept;
    ~SetFortressSiege() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    int m_defenceFortressId; // +0x10
    int m_offenceFortressId; // +0x14
};


// Current PledgeMemberService.cpp command published by
// CPledge::SetAlliance. The alliance object is borrowed for the duration of
// the synchronous member fan-out. The trailing bytes after the target bool
// remain uninitialized just as in the current stack-owned command.
class SetAlliance final : public CPledgeMemberService {
public:
    SetAlliance(CAlliance* alliance, bool masterPledge) noexcept;
    ~SetAlliance() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    CAlliance* m_alliance; // +0x10
    bool m_isMasterPledges; // +0x18
    [[maybe_unused]] std::byte m_padding19To20[7];
};


// Current CPledge combat-state query. The command stops the outer member
// traversal after the first user, pet, or summon found in combat. Its raid
// slot is the target's shared folded no-op.
class CombatModeChecker final : public CPledgeMemberService {
public:
    CombatModeChecker() : m_isInCombatMode(false) {}
    ~CombatModeChecker() override = default;

    bool IsInCombatMode() { return m_isInCombatMode; }

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    void ItsInCombatMode() {
        StopServe();
        m_isInCombatMode = true;
    }

    bool m_isInCombatMode; // +0x10
};


// Current PledgeMemberService.cpp command used whenever a pledge's siege
// role changes. It carries no payload beyond the complete 0x10-byte base.
class SetRelationChanged final : public CPledgeMemberService {
public:
    SetRelationChanged() = default;
    ~SetRelationChanged() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;
};


// Current pledge-war fan-out command. It has no payload beyond the complete
// CPledgeMemberService base and owns both its local and raid command slots.
class AffectedByWar final : public CPledgeMemberService {
public:
    AffectedByWar() = default;
    ~AffectedByWar() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;
};


class ArrivedMail final : public CPledgeMemberService {
public:
    ArrivedMail() = default;
    ~ArrivedMail() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;
};


class PMS_SyncPledgeType final : public CPledgeMemberService {
public:
    PMS_SyncPledgeType(std::uint32_t characterDatabaseId, PledgeType pledgeType) noexcept;
    ~PMS_SyncPledgeType() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    std::uint32_t m_characterDatabaseId; // +0x10
    PledgeType m_pledgeType; // +0x14
};

// Current raid-synchronization command used after a pledge skill is added or
// removed. Local members have already been updated by the caller, so the
// local ServeInternal slot is the folded no-op. Only the raid path relays it.
class PMS_SyncPledgeSkill final : public CPledgeMemberService {
public:
    PMS_SyncPledgeSkill(int skillId, int skillLevel) noexcept;
    ~PMS_SyncPledgeSkill() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    int m_skillId; // +0x10
    int m_skillLevel; // +0x14
};

// Current PledgeMemberService.cpp local/raid notification sent after a
// pledge skill has been inserted into the pledge's skill map.
class CSendPledgeSkillAdd final : public CPledgeMemberService {
public:
    CSendPledgeSkillAdd(int skillId, int skillLevel) noexcept;
    ~CSendPledgeSkillAdd() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    int m_skillId; // +0x10
    int m_skillLevel; // +0x14
};

class PMS_SyncPledgeLevel final : public CPledgeMemberService {
public:
    explicit PMS_SyncPledgeLevel(int level) noexcept;
    ~PMS_SyncPledgeLevel() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    int m_level; // +0x10
    [[maybe_unused]] std::uint32_t m_alignment14To18{};
};

class PMS_SyncPledgeMastery final : public CPledgeMemberService {
public:
    PMS_SyncPledgeMastery(PledgeMasteryID::Type masteryId, int point) noexcept;
    ~PMS_SyncPledgeMastery() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    PledgeMasteryID::Type m_id; // +0x10
    int m_nPoint; // +0x14
};

class SetCrest final : public CPledgeMemberService {
public:
    explicit SetCrest(const std::uint32_t crestId) noexcept : m_crestId(crestId) {}
    ~SetCrest() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    std::uint32_t m_crestId; // +0x10
    [[maybe_unused]] std::byte m_alignment14To18[4];
};

class SetEmblem final : public CPledgeMemberService {
public:
    explicit SetEmblem(const std::uint32_t emblemId) noexcept : m_emblemId(emblemId) {}
    ~SetEmblem() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    std::uint32_t m_emblemId; // +0x10
    [[maybe_unused]] std::byte m_alignment14To18[4];
};

class SetAllianceCrest final : public CPledgeMemberService {
public:
    explicit SetAllianceCrest(const std::uint32_t allianceCrestId) noexcept :
        m_allianceCrestId(allianceCrestId) {}
    ~SetAllianceCrest() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    std::uint32_t m_allianceCrestId; // +0x10
    [[maybe_unused]] std::byte m_alignment14To18[4];
};


// Current PledgeMemberService.cpp command used by
// CPledge::UpdatePledgeInfoPacket. The 64-bit mask occupies the sole
// command lane at +0x10. Values outside the target's 44-bit parameter mask
// are ignored by the local service slot.
class SetParamChanged final : public CPledgeMemberService {
public:
    explicit SetParamChanged(std::int64_t flag) noexcept : m_flag(flag) {}
    ~SetParamChanged() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    std::int64_t m_flag; // +0x10
};

// Current PledgeMemberService.cpp command used when every stored pledge
// mission timestamp is reset. The concrete owner has no payload beyond the
// complete three-slot CPledgeMemberService surface.
class ResetPledgeActivityMark final : public CPledgeMemberService {
public:
    ResetPledgeActivityMark() = default;
    ~ResetPledgeActivityMark() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;
};


class UpdateMemberList final : public CPledgeMemberService {
public:
    UpdateMemberList(User* updatedMember, bool online) noexcept;
    ~UpdateMemberList() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    User* m_updatedMember; // +0x10
    bool m_isOnline; // +0x18
    [[maybe_unused]] std::byte m_padding19To20[7]{};
};

class AddWindowPacket final : public CPledgeMemberService {
public:
    explicit AddWindowPacket(User* member) noexcept;
    ~AddWindowPacket() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    User* m_member; // +0x10
};

class SetPledgeMember final : public CPledgeMemberService {
public:
    explicit SetPledgeMember(CPledge* pledge) noexcept : m_pledge(pledge) {}
    ~SetPledgeMember() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    CPledge* m_pledge; // +0x10
};

class OpenAllWindowPacket final : public CPledgeMemberService {
public:
    explicit OpenAllWindowPacket(CPledge* pledge) noexcept : m_pledge(pledge) {}
    ~OpenAllWindowPacket() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    CPledge* m_pledge; // +0x10
};

class CSendPledgeOnlineMemberCount final : public CPledgeMemberService {
public:
    explicit CSendPledgeOnlineMemberCount(int onlineCount) noexcept : m_onlineCount(onlineCount) {}
    ~CSendPledgeOnlineMemberCount() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    int m_onlineCount;
};

class CSendPledgeSkillList final : public CPledgeMemberService {
public:
    CSendPledgeSkillList(int pledgeSkillCount, int subPledgeSkillCount, CPacketHelperEx<20480>* packet) noexcept :
        m_pledgeSkillCount(pledgeSkillCount),
        m_subPledgeSkillCount(subPledgeSkillCount),
        m_packet(packet) {}
    ~CSendPledgeSkillList() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    int m_pledgeSkillCount;
    int m_subPledgeSkillCount;
    CPacketHelperEx<20480>* m_packet;
};

// PledgeMemberService.h current template used by CPledge::SystemMessage(int).
// The checker base is empty for NoCheck<User>, so the three payload dwords
// begin immediately after CPledgeMemberService at +0x10.
template <typename TObject>
class NoCheck {};

template <typename TObject>
class BattleFieldChecker {
public:
    explicit BattleFieldChecker(TObject* object) noexcept : m_t(object) {}

    bool Check(User* user) const;

    TObject* m_t;
};

template <typename TObject>
class SameMemberCheck {
public:
    explicit SameMemberCheck(TObject* object) noexcept : m_t(object) {}

    bool Check(User* user) const;

    TObject* m_t;
};

template <template <typename> class TChecker, typename TObject>
class SystemMessage final : public CPledgeMemberService, public TChecker<TObject> {
public:
    explicit SystemMessage(int messageId) noexcept;
    SystemMessage(int messageId, TObject* object, int skillId, int skillLevel) noexcept;
    ~SystemMessage() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    int m_messageId; // +0x10
    int m_skillId; // +0x14
    int m_skillLevel; // +0x18
    std::uint32_t m_alignment1cTo20;
};

// Current PledgeMemberService.h item-parameter system-message command.
// NoCheck<User> is an empty secondary base, so the two payload dwords occupy
// +0x10/+0x14 and the complete stack-owned object is 0x18 bytes.
template <template <typename> class TChecker, typename TObject>
class SystemMessage_Item final : public CPledgeMemberService, public TChecker<TObject> {
public:
    SystemMessage_Item(int messageId, int itemClassId, TObject* object) noexcept;
    SystemMessage_Item(int messageId, int itemClassId) noexcept;
    ~SystemMessage_Item() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    int m_msgNo; // +0x10
    int m_idItem; // +0x14
};

// Current PledgeMemberService.cpp string-parameter system-message command.
// NoCheck<User> is an empty secondary base. The target payload therefore
// occupies +0x10..+0x24 and the stack-owned concrete object is 0x28 bytes.
template <template <typename> class TChecker, typename TObject>
class SystemMessage_S final : public CPledgeMemberService, public TChecker<TObject> {
public:
    SystemMessage_S(int messageId, eSMsgType argumentType, const char16_t* text, TObject* object);
    SystemMessage_S(int messageId, eSMsgType argumentType, const char16_t* text);
    ~SystemMessage_S() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    int m_msgNo; // +0x10
    std::u16string m_str; // +0x18
    const eSMsgType m_eType; // +0x20
};

template <template <typename> class TChecker, typename TObject>
class SystemMessage_d final : public CPledgeMemberService, public TChecker<TObject> {
public:
    SystemMessage_d(int messageId, int parameter) noexcept;
    ~SystemMessage_d() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    int m_msgNo; // +0x10
    int m_nParam; // +0x14
};

template <template <typename> class TChecker, typename TObject>
class SystemMessage_S_d final : public CPledgeMemberService, public TChecker<TObject> {
public:
    SystemMessage_S_d(int messageId, eSMsgType argumentType, const char16_t* text, int parameter);
    ~SystemMessage_S_d() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    int m_msgNo; // +0x10
    std::u16string m_str; // +0x18
    int m_nParam; // +0x20
    eSMsgType m_eType; // +0x24
};

template <template <typename> class TChecker, typename TObject>
class SystemMessage_S_S_d final : public CPledgeMemberService, public TChecker<TObject> {
public:
    SystemMessage_S_S_d(
        int messageId,
        eSMsgType firstArgumentType,
        const char16_t* firstText,
        eSMsgType secondArgumentType,
        const char16_t* secondText,
        int parameter
    );
    ~SystemMessage_S_S_d() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    int m_msgNo; // +0x10
    std::u16string m_firstText; // +0x18
    std::u16string m_secondText; // +0x20
    int m_nParam; // +0x28
    eSMsgType m_firstType; // +0x2c
    eSMsgType m_secondType; // +0x30
};

template <template <typename> class TChecker, typename TObject>
class SystemMessage_d_d final : public CPledgeMemberService, public TChecker<TObject> {
public:
    SystemMessage_d_d(int messageId, int firstParameter, int secondParameter) noexcept;
    ~SystemMessage_d_d() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    int m_msgNo; // +0x10
    int m_firstParameter; // +0x14
    int m_secondParameter; // +0x18
    std::uint32_t m_padding1cTo20{};
};

class CloseWindowPacket final : public CPledgeMemberService {
public:
    explicit CloseWindowPacket(const char16_t* memberName);
    ~CloseWindowPacket() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    std::u16string m_memberName;
};

class DismissPledge final : public CPledgeMemberService {
public:
    explicit DismissPledge(const char16_t* pledgeName) noexcept : m_pledgeName(pledgeName) {}
    ~DismissPledge() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    const char16_t* m_pledgeName; // +0x10
};


class CloseAllWindowPacket final : public CPledgeMemberService {
public:
    CloseAllWindowPacket() = default;
    ~CloseAllWindowPacket() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;
};


class GenericService final : public CPledgeMemberService {
public:
    explicit GenericService(CPacketHelperEx<20480>* packet) noexcept;
    ~GenericService() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    CPacketHelperEx<20480>* m_packet;
};

class ViewPledgeMemberInfo final : public CPledgeMemberService {
public:
    explicit ViewPledgeMemberInfo(CPacketHelperEx<20480>* packet) noexcept;
    ~ViewPledgeMemberInfo() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    CPacketHelperEx<20480>* m_packet; // +0x10
};

// Current PledgeMemberService.cpp command used after a pledge power grade is
// changed. The current call sites fix the payload writes even
// though the constructor itself is inlined: power at +0x10, grade at +0x14.
class UpdatePledgePower final : public CPledgeMemberService {
public:
    UpdatePledgePower(int gradeId, std::uint32_t pledgePowerBits) noexcept;
    ~UpdatePledgePower() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    std::uint32_t pledgePowerBits; // +0x10
    int gradeId; // +0x14
};


class SubPledgeUpdated final : public CPledgeMemberService {
public:
    explicit SubPledgeUpdated(CPacketHelperEx<20480>* packet) noexcept;
    ~SubPledgeUpdated() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    CPacketHelperEx<20480>* m_packet;
};

class UpdatePledgeInfoWindow final : public CPledgeMemberService {
public:
    explicit UpdatePledgeInfoWindow(CPledge* pledge);
    ~UpdatePledgeInfoWindow() override = default;

private:
    void SetAllianceInfo();
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    CPledge* m_pledge; // +0x10
    std::uint32_t m_allianceId; // +0x18
    [[maybe_unused]] std::uint32_t m_alignment1cTo20{};
    std::u16string m_allianceName; // +0x20
    std::uint32_t m_allianceCrestId; // +0x48
    int m_IsNotInPeace; // +0x4c
};

// Current stack-owned command compiled by CPledge::ProcessBonusPoint. The
// three dwords at +0x10 are the complete command payload.
class UpdatePledgeBonusPoint final : public CPledgeMemberService {
public:
    UpdatePledgeBonusPoint(int missionType, int point, int rewardLevel) noexcept :
        m_missionType(missionType),
        m_point(point),
        m_rewardLevel(rewardLevel),
        m_alignment1cTo20{} {}
    ~UpdatePledgeBonusPoint() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    int m_missionType; // +0x10
    int m_point; // +0x14
    int m_rewardLevel; // +0x18
    [[maybe_unused]] std::byte m_alignment1cTo20[4]{};
};

// Current PledgeMemberService.cpp stack command used by
// CPledge::SetPledgeRaidInzoneBinding. The time lane is target time32/long.
class PMS_PledgeRaidInzoneBinding final : public CPledgeMemberService {
public:
    PMS_PledgeRaidInzoneBinding(std::int32_t bindingTime, bool force) noexcept;
    ~PMS_PledgeRaidInzoneBinding() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    std::int32_t tTime_; // +0x10
    bool bForce_; // +0x14
    [[maybe_unused]] std::byte m_padding15To18[3]{};
};

class SetDominionSiege final : public CPledgeMemberService {
public:
    SetDominionSiege(int dominionSiegeId, int dominionSiegeStatus) noexcept;
    ~SetDominionSiege() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    int m_nDominionSiegeId; // +0x10
    int m_nDominionSiegeStatus; // +0x14
};

class CancelRenamed final : public CPledgeMemberService {
public:
    CancelRenamed() = default;
    ~CancelRenamed() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;
};

class UnequipCastleItem final : public CPledgeMemberService {
public:
    UnequipCastleItem() = default;
    ~UnequipCastleItem() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;
};

class PMS_PledgeMissionCheckAndAddCountInCastleSiege final : public CPledgeMemberService {
public:
    PMS_PledgeMissionCheckAndAddCountInCastleSiege(
        PledgeMission::GoalType goalType, int firstValue, int secondValue, int count
    ) noexcept;
    ~PMS_PledgeMissionCheckAndAddCountInCastleSiege() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    PledgeMission::GoalType m_goalType; // +0x10
    int m_firstValue; // +0x14
    int m_secondValue; // +0x18
    int m_count; // +0x1c
};

class PlayMusic final : public CPledgeMemberService {
public:
    PlayMusic() = default;
    ~PlayMusic() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;
};

// Current PledgeMemberService.cpp pledge/alliance chat command. All pointer
// members are borrowed for the synchronous CPledge fan-out. The three bytes
// after the relay flag keep their target stack contents.
class Say final : public CPledgeMemberService {
public:
    Say(User* teller, const char16_t* raidName, const char16_t* message, SayPacketType type, bool needToRelay) noexcept;
    ~Say() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    User* m_teller; // +0x10
    const char16_t* m_sRaidName; // +0x18
    const char16_t* m_msg; // +0x20
    SayPacketType m_type; // +0x28
    bool m_bNeedToRelay; // +0x2c
    [[maybe_unused]] std::byte m_padding2dTo30[3];
};


// Current PledgeMemberService.cpp command used for relay-originated raid
// pledge/alliance fan-out. All strings are borrowed for the synchronous
// member traversal, and the raid command slot is the shared folded no-op.
class SayForRaid final : public CPledgeMemberService {
public:
    SayForRaid(int tellerServerId, int tellerDatabaseId, const char16_t* tellerName, const char16_t* message, SayPacketType type) noexcept;
    ~SayForRaid() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    int m_nTellerServerID; // +0x10
    int m_nTellerDBID; // +0x14
    const char16_t* m_sTellerName; // +0x18
    const char16_t* m_msg; // +0x20
    SayPacketType m_type; // +0x28
    [[maybe_unused]] std::uint32_t m_padding2cTo30;
};


// Current PledgeMemberService.cpp command used by relay-originated pledge
// and alliance chat. Both strings are borrowed for the synchronous fan-out.
// unlike Say, this command has a folded no-op raid slot.
class SayByMobile final : public CPledgeMemberService {
public:
    SayByMobile(
        int serverId, int characterDatabaseId, const char16_t* characterName, const char16_t* message, SayPacketType type
    ) noexcept;
    ~SayByMobile() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    int m_serverId; // +0x10
    int m_characterDatabaseId; // +0x14
    const char16_t* m_characterName; // +0x18
    const char16_t* m_message; // +0x20
    SayPacketType m_type; // +0x28
    [[maybe_unused]] std::uint32_t m_padding2cTo30;
};


// Current PledgeMemberService.cpp command used while building
// war_report.htm. The HTML generator is borrowed during synchronous pledge
// fan-out. The raid slot is the current shared folded no-op.
class WarReport final : public CPledgeMemberService {
public:
    explicit WarReport(CHtmlGenerator* html) noexcept : m_html(html) {}
    ~WarReport() override = default;

private:
    void ServeInternal(User* user) override;
    void ServeInternalForRaid(CPledge& pledge) override;

    CHtmlGenerator* m_html; // +0x10
};
