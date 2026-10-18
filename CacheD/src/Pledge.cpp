#include "Shared/src/Log.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/TemplateHelper.h"

#include "Pledge.h"

#include "CacheSocket.h"
#include "Cached.h"
#include "User.h"
#include "UserDb.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/TargetFormatting.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/DBConn.h"
#include "Shared/src/PledgeRanking.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/TextRuntime.h"
#include "Runtime.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <new>
#include <optional>
#include <string>

namespace {

constexpr char16_t kEmptyTargetString[] = u"";

int ClampNameValueDelta(int currentValue, int requestedDelta) {
    // Current helper 0x00501f10 has no surviving original name. It clamps
    // the delta so the stored signed 32-bit pledge value cannot overflow.
    const std::int64_t candidate = static_cast<std::int64_t>(currentValue) + requestedDelta;
    if (candidate < std::numeric_limits<int>::max() && candidate > std::numeric_limits<int>::min()) {
        return requestedDelta;
    }
    const int boundary =
        candidate >= std::numeric_limits<int>::max() ? std::numeric_limits<int>::max() : std::numeric_limits<int>::min();
    return boundary - currentValue;
}

void WritePledgeItemCheckAudit(
    const char16_t* pledgeName,
    std::int64_t characterId,
    std::int64_t accountId,
    int penaltyLevel,
    int checkFailType,
    const char16_t* characterName,
    const char16_t* accountName
) {
    // Direct current call at 0x00502ab0 uses this exact 26-field route-8
    // record. It is not a CLogFormatter class call in the shipping image.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x5a4);
    fields[1] = runtime::DecimalUtf16(characterId);
    fields[2] = runtime::DecimalUtf16(accountId);
    if (pledgeName)
        fields[8] = pledgeName;
    fields[11] = runtime::DecimalUtf16(2);
    fields[12] = runtime::DecimalUtf16(penaltyLevel);
    fields[13] = runtime::DecimalUtf16(0);
    fields[14] = runtime::DecimalUtf16(checkFailType);
    fields[21] = characterName;
    fields[22] = accountName;

    std::u16string record;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (index != 0)
            record.push_back(u',');
        record.append(fields[index]);
    }
    runtime::process::WriteLogFormatterLine(8, record);
}

// Current global constructor 0x00505790 creates this exact xmap value shape
// and inserts eight separately allocated, fieldless assembler objects. No
// source identifier survives for the owning global/class, so the recovery
// name remains local and semantic.
class RecoveredPledgeMemberAssemblerMap {
public:
    RecoveredPledgeMemberAssemblerMap() {
        m_assemblers.emplace(MASTER_PLEDGE, new CPledgeMemberPacketAssembler);
        m_assemblers.emplace(ACADEMY, new CAcademyMemberPacketAssembler);
        m_assemblers.emplace(SUB_1, new CPledgeMemberPacketAssembler);
        m_assemblers.emplace(SUB_2, new CPledgeMemberPacketAssembler);
        m_assemblers.emplace(SUB_1_1, new CPledgeMemberPacketAssembler);
        m_assemblers.emplace(SUB_1_2, new CPledgeMemberPacketAssembler);
        m_assemblers.emplace(SUB_2_1, new CPledgeMemberPacketAssembler);
        m_assemblers.emplace(SUB_2_2, new CPledgeMemberPacketAssembler);
    }

    ~RecoveredPledgeMemberAssemblerMap() {
        // The current destructor at 0x005056d8 destroys the raw assembler
        // allocations before releasing the xmap tree. Their destructors are
        // trivial, but use the concrete types here to keep portable C++
        // lifetime rules well-defined.
        for (const auto& entry : m_assemblers) {
            if (entry.first == ACADEMY) {
                delete static_cast<CAcademyMemberPacketAssembler*>(entry.second);
            } else {
                delete static_cast<CPledgeMemberPacketAssembler*>(entry.second);
            }
        }
    }

    CPledgeMemberListPacketAssembler& Get(PledgeType pledgeType) {
        const auto found = m_assemblers.find(pledgeType);
        if (found == m_assemblers.end())
            return m_nullAssembler;
        return *found->second;
    }

private:
    std::map<PledgeType, CPledgeMemberListPacketAssembler*> m_assemblers;
    CNullAssembler m_nullAssembler;
};

RecoveredPledgeMemberAssemblerMap g_pledgeMemberAssemblers;

} // namespace

int CPledgeMemberListPacket::GetMemberCount() const noexcept { return m_memberCount; }

void CPledgeMemberListPacket::IncrementMemberCount() noexcept { ++m_memberCount; }

bool CPledgeMemberListPacketAssembler::AssembleTemplate(CPledgeMemberListPacket& packet, PledgeMemberInfo& member) {
    L2_GUARD_FUNCTION();

    if (AssembleInternal(packet, member) < 1) {
        const std::string memberName = member.characterName ? Utf16ToUtf8(member.characterName) : std::string();
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] Buffer Overflow in LoadPledge. member[%s]!!", "Pledge.cpp", 0x78,
        //     memberName.c_str()
        // )
        // Log.Add(LOG_ERROR,
        //     "[clan] Cannot assemble member %u ('%s') in the clan member list. List assembly stopped.",
        //     member.memberId, memberName.c_str())
        L2_LOG_ERROR(Log,
            "[clan] Cannot assemble member %u ('%s') in the clan member list; list assembly stopped.",
            member.memberId, memberName.c_str());
        return false;
    }

    packet.IncrementMemberCount();
    return true;
}

bool CPledgeMemberListPacketAssembler::SimpleLoadUserAndAssemble(
    CPledge* pledge, CPledgeMemberListPacket& packet, std::uint32_t memberId
) {
    L2_GUARD_FUNCTION();

    // The current caller reserves 50 WCHARs for each value but deliberately
    // binds only 0x32 target bytes (24 UTF-16 code units plus the terminator).
    std::array<char16_t, 50> characterName{};
    std::array<char16_t, 50> nickname{};
    int level = 0;
    int characterClass = 0;
    int gender = 0;
    int race = 0;
    int gradeId = 0;
    int pledgeBonus = 0;
    if (!g_userDb.SimpleLoad(
            static_cast<int>(memberId), characterName.data(), 0x32, nickname.data(), 0x32, &level, &characterClass, &gender,
            &race, &gradeId, &pledgeBonus
        )) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%u]", "Pledge.cpp", 0x94, memberId)
        // Log.Add(LOG_ERROR,
        //     "[clan] Character %u could not be loaded for the member list. Skipping this member.",
        //     memberId)
        L2_LOG_ERROR(Log,
            "[clan] Character %u could not be loaded for the member list; skipping this member.",
            memberId);
        // A missing database row is skipped in the shipping loop. It is not
        // treated as a packet overflow and therefore does not abort the list.
        return true;
    }

    const std::u16string targetCharacterName = std::u16string(characterName.data());
    const std::u16string targetNickname = std::u16string(nickname.data());
    PledgeMemberInfo member;
    member.pledge = pledge;
    member.memberId = memberId;
    member.characterName = targetCharacterName.c_str();
    member.nickname = targetNickname.c_str();
    member.level = level;
    member.characterClass = characterClass;
    member.gender = gender;
    member.race = race;
    member.gradeId = gradeId;
    member.pledgeBonus = pledgeBonus;
    return AssembleTemplate(packet, member);
}

bool CPledgeMemberListPacketAssembler::Assemble(
    CPledge* pledge, CPledgeMemberListPacket& packet, std::set<std::uint32_t>& members
) {
    L2_GUARD_FUNCTION();

    for (const std::uint32_t memberId : members) {
        CUserSP user = g_userDb.GetUser(static_cast<int>(memberId), false, false, pledge->GetWorldId());
        if (!user) {
            if (!SimpleLoadUserAndAssemble(pledge, packet, memberId)) {
                return false;
            }
            continue;
        }

        // Current unnamed body 0x0050282c is the compiled
        // PledgeMemberInfo(CPledge*, CSPointer<CUser>) construction path. It
        // consumes a by-value smart-pointer copy and publishes these exact
        // ten fields in carrier order. On Unix, CUser text is held as
        // 32-bit-wchar host state, so the two UTF-16 backing strings must
        // remain in this caller until AssembleTemplate returns. The target
        // can point directly into its native 16-bit-wchar CUser instead.
        const std::u16string characterName = std::u16string(user->GetCharName());
        const std::u16string nickname = user->m_nickname;
        PledgeMemberInfo member;
        member.pledge = pledge;
        member.memberId = memberId;
        member.characterName = characterName.c_str();
        member.nickname = nickname.c_str();
        member.level = user->GetLevel();
        member.characterClass = user->GetClass();
        member.gender = user->GetGender();
        member.race = user->GetRace();
        member.gradeId = user->GetGradeId();
        member.pledgeBonus = user->m_userPledgeBonus;
        if (!AssembleTemplate(packet, member)) {
            return false;
        }
    }
    return true;
}

int CPledgeMemberPacketAssembler::AssembleInternal(CPledgeMemberListPacket& packet, PledgeMemberInfo& member) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "dSSdddddd", static_cast<int>(member.memberId), member.characterName, member.nickname, member.level,
        member.characterClass, member.gender, member.race, member.gradeId, member.pledgeBonus
    );
}

int CAcademyMemberPacketAssembler::AssembleInternal(CPledgeMemberListPacket& packet, PledgeMemberInfo& member) {
    L2_GUARD_FUNCTION();

    int joinLevel = 0x27;
    std::uint32_t academyMasterId = 0;
    CAcademyMember* const academyMember = member.pledge->GetAcademyMember(member.memberId);
    if (!academyMember) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find academy user, id[%u]", "Pledge.cpp", 0xbd, member.memberId)
        // Log.Add(LOG_ERROR,
        //     "[clan] Academy information is missing for character %u. Reply uses join level %d and academy master %u.",
        //     member.memberId, joinLevel, academyMasterId)
        L2_LOG_ERROR(Log,
            "[clan] Academy information is missing for character %u; reply uses join level %d and academy master %u.",
            member.memberId, joinLevel, academyMasterId);
    } else {
        joinLevel = academyMember->JoinLevel();
        academyMasterId = academyMember->AcademyMasterId();
    }

    return packet.Assemble(
        "dSSdddddddd", static_cast<int>(member.memberId), member.characterName, member.nickname, member.level,
        member.characterClass, member.gender, member.race, member.gradeId, member.pledgeBonus, joinLevel,
        static_cast<int>(academyMasterId)
    );
}

int CNullAssembler::AssembleInternal(
    CPledgeMemberListPacket& packet [[maybe_unused]], PledgeMemberInfo& member [[maybe_unused]]
) {
    // Current CNullAssembler vtable points at the shared three-byte return-0
    // helper at 0x0063a3c8.
    return 0;
}

CAcademyMember::CAcademyMember(std::uint32_t memberId, int joinLevel, std::uint32_t academyMasterId) noexcept :
    m_memberId(memberId), m_joinLevel(joinLevel), m_academyMasterId(academyMasterId) {}

std::uint32_t CAcademyMember::MemberId() const noexcept { return m_memberId; }

int CAcademyMember::JoinLevel() const noexcept { return m_joinLevel; }

std::uint32_t CAcademyMember::AcademyMasterId() const noexcept { return m_academyMasterId; }

void CAcademyMember::SetAcademyMasterId(std::uint32_t academyMasterId) noexcept { m_academyMasterId = academyMasterId; }

RecoveredSubPledgeInfo::RecoveredSubPledgeInfo(
    PledgeType type, std::uint32_t subPledgeMasterId, const wchar_t* subPledgeName, int subPledgeMemberLimit
) :
    pledgeType(type), masterId(subPledgeMasterId), memberLimit(subPledgeMemberLimit) {
    const std::u16string converted = WideToUtf16(subPledgeName);
    name.assign(converted);
}

L2_DEFINE_OBJECT_CLASS_ID(CPledge)

L2_DEFINE_OBJECT_CLASS_NAME(CPledge, m_pClassName_CPledge)

L2_DEFINE_OBJECT_NEW(CPledge)

// Current allocator 0x00510ba4 publishes 0x2e0 bytes. Complete CRT
// initializer order makes 0x006a35a0 class ID 25.
L2_DEFINE_OBJECT_ALLOCATOR(CPledge, m_pClassName_CPledge)

L2_DEFINE_OBJECT_RELEASE(CPledge)

CPledge::CPledge(
    std::uint32_t pledgeId,
    const char16_t* pledgeName,
    std::uint32_t rulerId,
    const char16_t* rulerName,
    std::uint32_t allianceId,
    std::uint32_t challengeTime,
    std::uint32_t nowWarId,
    std::uint32_t nameValue,
    std::uint32_t oustTime,
    std::uint32_t skillLevel,
    std::uint32_t privateFlag,
    int status,
    int rank,
    std::uint32_t castleId,
    std::uint32_t agitId,
    std::uint32_t fortressId,
    int rootNameValue,
    std::uint32_t /* legacyUnused */,
    std::uint32_t crestId,
    std::uint32_t isGuilty,
    std::uint32_t dismissReservedTime,
    std::uint32_t allianceOustedTime,
    std::uint32_t allianceWithdrawTime,
    std::uint32_t allianceDismissTime,
    std::uint32_t newEmblemId,
    std::uint32_t castleSiegeDefenceCount,
    std::uint32_t masteryResetTime,
    int worldId
) :
    m_pledgeId(pledgeId),
    m_name{},
    m_rulerId(rulerId),
    m_rulerName{},
    m_allianceId(allianceId),
    m_challengeTime(challengeTime),
    m_nowWarId(nowWarId),
    m_nameValue(nameValue),
    m_oustTime(oustTime),
    m_skillLevel(skillLevel),
    m_privateFlag(privateFlag),
    m_status(status),
    m_rank(rank),
    m_castleId(castleId),
    m_agitId(agitId),
    m_fortressId(fortressId),
    m_rootNameValue(rootNameValue),
    m_crestId(crestId),
    m_newEmblemId(newEmblemId),
    m_isGuilty(isGuilty),
    m_dismissReservedTime(dismissReservedTime),
    m_allianceOustedTime(allianceOustedTime),
    m_allianceWithdrawTime(allianceWithdrawTime),
    m_allianceDismissTime(allianceDismissTime),
    m_worldId(worldId),
    m_masteryResetTime(masteryResetTime),
    m_targetUnknownD8(0),
    m_targetUnknownE0(false),
    m_academyName{},
    m_graduateAcademyCount(0),
    m_academyExists(false),
    m_pledgeTypeMask(0),
    m_powers{},
    m_mergedPledgeId(0),
    m_mergedPledgeName{},
    m_pledgeBonus{},
    m_castleSiegeDefenceCount(castleSiegeDefenceCount),
    m_mastery(nullptr),
    m_items(nullptr) {
    L2_GUARD_FUNCTION();
    AssignFixedName(m_name, pledgeName, L"pledgeName length < 24");
    AssignFixedName(m_rulerName, rulerName, L"rulerName length < 24");
    InitializeWareHouse();
    m_mastery = new (std::nothrow) PledgeMasteryInCache(static_cast<std::int32_t>(m_pledgeId));
    m_items = new (std::nothrow) PledgeItemInCache(static_cast<std::int32_t>(m_pledgeId));
}

CPledge::CPledge(
    std::uint32_t pledgeId, const char16_t* pledgeName, std::uint32_t rulerId, const char16_t* rulerName, int worldId
) :
    m_pledgeId(pledgeId),
    m_name{},
    m_rulerId(rulerId),
    m_rulerName{},
    m_allianceId(0),
    m_challengeTime(0),
    m_nowWarId(0),
    m_nameValue(0),
    m_oustTime(0),
    m_skillLevel(0),
    m_privateFlag(0),
    m_status(0),
    m_rank(0),
    m_castleId(0),
    m_agitId(0),
    m_fortressId(0),
    m_rootNameValue(0),
    m_crestId(0),
    m_newEmblemId(0),
    m_isGuilty(0),
    m_dismissReservedTime(0),
    m_allianceOustedTime(0),
    m_allianceWithdrawTime(0),
    m_allianceDismissTime(0),
    m_worldId(worldId),
    m_masteryResetTime(0),
    m_targetUnknownD8(0),
    m_targetUnknownE0(false),
    m_academyName{},
    m_graduateAcademyCount(0),
    m_academyExists(false),
    m_pledgeTypeMask(0),
    m_powers{},
    m_mergedPledgeId(0),
    m_mergedPledgeName{},
    m_pledgeBonus{},
    m_castleSiegeDefenceCount(0),
    m_mastery(nullptr),
    m_items(nullptr) {
    L2_GUARD_FUNCTION();
    AssignFixedName(m_name, pledgeName, L"pledgeName length < 24");
    AssignFixedName(m_rulerName, rulerName, L"rulerName length < 24");
    InitializeWareHouse();
    m_mastery = new (std::nothrow) PledgeMasteryInCache(static_cast<std::int32_t>(m_pledgeId));
    m_items = new (std::nothrow) PledgeItemInCache(static_cast<std::int32_t>(m_pledgeId));
}

CPledge::~CPledge() {
    L2_GUARD_FUNCTION();
    delete m_items;
    m_items = nullptr;
    delete m_mastery;
    m_mastery = nullptr;
    for (auto& entry : m_academyMembers) {
        delete entry.second;
    }
    for (auto& entry : m_subPledges) {
        delete entry.second;
    }
}

void CPledge::AssignFixedName(char16_t (&destination)[24], const char16_t* source, const wchar_t* parameterName) {
    std::fill(std::begin(destination), std::end(destination), u'\0');
    if (!source) {
        runtime::process::HandleInvalidParameter(L"source != nullptr");
        return;
    }
    const std::u16string_view converted(source);
    if (converted.size() >= std::size(destination)) {
        runtime::process::HandleInvalidParameter(parameterName);
        return;
    }
    std::copy(converted.begin(), converted.end(), std::begin(destination));
}

void CPledge::InitializeWareHouse() {
    CWareHouse* const warehouse = new CWareHouse;
    warehouse->SetOwnerId(m_pledgeId);
    warehouse->Init(WareHouseType_Pledge, true);
    m_warehouse.SetObject(warehouse);
    ItemCheckOnLoad();
    L2_RELEASE(warehouse, ObjectRefType::CreateDelete);
}

std::uint32_t CPledge::GetId() const noexcept { return m_pledgeId; }

std::uint32_t CPledge::rullerId() const noexcept {
    // The misspelling is the original C1 source accessor. Current
    // DeleteCharacterPacket inlines the same read from CPledge +0x4c.
    return m_rulerId;
}

const char16_t* CPledge::rullerName() const noexcept {
    // The original accessor spelling survives in C1. Current
    // CPledgeBBS::GetClanInfo/GetClanAdmin inline the read from +0x50.
    return m_rulerName;
}

std::uint32_t CPledge::allianceId() const noexcept {
    // Current RequestCreateAllianceByNpc inlines this read from +0x80. The
    // surviving C1 owner keeps the original accessor spelling.
    return m_allianceId;
}

void CPledge::setAllianceId(std::uint32_t allianceId) noexcept {
    // Current CAllianceDb operations inline this store to CPledge+0x80.
    m_allianceId = allianceId;
}

void CPledge::setAllianceOustedTime(std::uint32_t allianceOustedTime) noexcept {
    // Current CAllianceDb::OustMemberPledge stores at CPledge+0xc4.
    m_allianceOustedTime = allianceOustedTime;
}

void CPledge::setAllianceWithdrawTime(std::uint32_t allianceWithdrawTime) noexcept {
    // Current CAllianceDb::Withdraw stores at CPledge+0xc8.
    m_allianceWithdrawTime = allianceWithdrawTime;
}

void CPledge::setAllianceDismissTime(std::uint32_t allianceDismissTime) noexcept {
    // Current CAllianceDb::DismissAlliance stores at CPledge+0xcc.
    m_allianceDismissTime = allianceDismissTime;
}

std::uint32_t CPledge::crestId() const noexcept {
    // Current RequestDelPledgeCrest 0x0046b4b8 reads CPledge +0xb4. The
    // surviving C1 declaration supplies the source spelling for this inline
    // accessor. The current object layout confirms its owner.
    return m_crestId;
}

int CPledge::skillLevel() const noexcept {
    // RequestSetPledgeInfo 0x004673d0 inlines the read from CPledge +0x94.
    // The original accessor spelling survives in the C1 source owner.
    return static_cast<int>(m_skillLevel);
}

std::uint32_t CPledge::castleId() const noexcept {
    // GetPledge3Packet 0x0042465c inlines this read from CPledge +0xa4.
    // The matching C1 owner keeps the original accessor spelling.
    return m_castleId;
}

std::uint32_t CPledge::agitId() const noexcept {
    // GetPledge3Packet 0x0042465c inlines this read from CPledge +0xa8.
    // The matching C1 owner keeps the original accessor spelling.
    return m_agitId;
}

void CPledge::setCastleId(std::uint32_t castleId) noexcept {
    // CSocketHandler::OnRequestSetCastleOwner 0x005979d0 inlines this store
    // to CPledge +0xa4. The matching C1 owner keeps this spelling.
    m_castleId = castleId;
}

void CPledge::setAgitId(std::uint32_t agitId) noexcept {
    // RequestSetAgitOwner 0x0048bcb8 inlines this store to CPledge +0xa8.
    // The surviving C1 owner keeps the original inline accessor spelling.
    m_agitId = agitId;
}

int CPledge::GetWorldId() const noexcept { return m_worldId; }

const char16_t* CPledge::GetName() const noexcept { return m_name; }

bool CPledge::AssembleMemberPacket(CPledgeMemberListPacket& packet, PledgeType pledgeType) {
    L2_GUARD_FUNCTION();

    // Shipping CPledge::AssembleMemberPacket uses xmap::operator[] here, so
    // an otherwise unknown pledge type also creates an empty member set
    // before CNullAssembler returns zero.
    return g_pledgeMemberAssemblers.Get(pledgeType).Assemble(this, packet, m_members[pledgeType]);
}

void CPledge::SendReplyToLoadPledge(CSocket* socket, bool checkPendingWrite) {
    L2_GUARD_FUNCTION();

    bool assembledSuccessfully = true;
    std::optional<CPledgeMemberListPacket> masterMembersStorage;
    std::optional<CPledgeMemberListPacket> academyMembersStorage;
    std::optional<std::array<char, 0x5000>> powersStorage;
    std::optional<std::array<char, 0x5000>> payloadStorage;
    int payloadLength;
    {
        L2_READ_GUARD(m_pledgeLock);

        auto& masterMembers = masterMembersStorage.emplace();
        if (!AssembleMemberPacket(masterMembers, MASTER_PLEDGE)) {
            assembledSuccessfully = false;
            const std::string pledgeName = Utf16ToUtf8(m_name);
            // Log.Add(
            //     LOG_ERROR,
            //     "fail to assemble master member packet. "
            //     "pledge[%s] member count[%d]",
            //     pledgeName.c_str(), masterMembers.GetMemberCount()
            // )
            // Log.Add(LOG_ERROR,
            //     "[clan] Cannot assemble the main member list for clan '%s' (ID %u). %d members assembled, clan reply not sent.",
            //     pledgeName.c_str(), m_pledgeId, masterMembers.GetMemberCount())
            L2_LOG_ERROR(Log,
                "[clan] Cannot assemble the main member list for clan '%s' (ID %u); %d members assembled, clan reply not sent.",
                pledgeName.c_str(), m_pledgeId, masterMembers.GetMemberCount());
        }

        auto& academyMembers = academyMembersStorage.emplace();
        if (!AssembleMemberPacket(academyMembers, ACADEMY)) {
            assembledSuccessfully = false;
            const std::string pledgeName = Utf16ToUtf8(m_name);
            // Log.Add(
            //     LOG_ERROR,
            //     "fail to assemble academy member packet. "
            //     "pledge[%s] member count[%d]",
            //     pledgeName.c_str(), academyMembers.GetMemberCount()
            // )
            // Log.Add(LOG_ERROR,
            //     "[clan] Cannot assemble the academy member list for clan '%s' (ID %u). %d members assembled, clan reply not sent.",
            //     pledgeName.c_str(), m_pledgeId, academyMembers.GetMemberCount())
            L2_LOG_ERROR(Log,
                "[clan] Cannot assemble the academy member list for clan '%s' (ID %u); %d members assembled, clan reply not sent.",
                pledgeName.c_str(), m_pledgeId, academyMembers.GetMemberCount());
        }

        auto& powers = powersStorage.emplace();
        int powersLength = 0;
        for (int gradeId = 1; gradeId <= 9; ++gradeId) {
            powersLength += ::Assemble(
                powers.data() + powersLength, static_cast<int>(powers.size()) - powersLength, "dd", gradeId,
                static_cast<int>(m_powers[static_cast<std::size_t>(gradeId - 1)])
            );
        }

        auto& payload = payloadStorage.emplace();
        payloadLength = ::Assemble(
            payload.data(), static_cast<int>(payload.size()), "SddSdbdSbddddddddddddddddddddddddddbdSdddddddd", m_name,
            static_cast<int>(m_pledgeId), static_cast<int>(m_rulerId), m_rulerName, masterMembers.GetMemberCount(),
            masterMembers.GetLength(), masterMembers.GetPacket(), academyMembers.GetMemberCount(), m_academyName,
            academyMembers.GetLength(), academyMembers.GetPacket(),
            // Current Cached.exe emits the direct mask getter at 0x00503210.
            // keep the same field read inline instead of inventing public API.
            static_cast<int>(m_pledgeTypeMask), static_cast<int>(m_pledgeSkills.size()), 0, static_cast<int>(m_nowWarId),
            m_rootNameValue, static_cast<int>(m_challengeTime), static_cast<int>(m_oustTime), static_cast<int>(m_allianceId),
            runtime::Int32FromBits(static_cast<std::uint32_t>(m_targetUnknownD8)), static_cast<int>(m_castleId),
            static_cast<int>(m_agitId), static_cast<int>(m_fortressId), m_rank, m_status, static_cast<int>(m_privateFlag),
            static_cast<int>(m_crestId), static_cast<int>(m_isGuilty), static_cast<int>(m_dismissReservedTime),
            static_cast<int>(m_skillLevel), static_cast<int>(m_allianceOustedTime), static_cast<int>(m_allianceWithdrawTime),
            static_cast<int>(m_allianceDismissTime), static_cast<int>(m_newEmblemId),
            static_cast<int>(m_castleSiegeDefenceCount), static_cast<int>(m_masteryResetTime), 9, powersLength, powers.data(),
            static_cast<int>(m_mergedPledgeId), m_mergedPledgeName, m_pledgeBonus[0].m_nCurrentBonusPoint,
            m_pledgeBonus[0].m_nMaxBonusPoint, m_pledgeBonus[0].m_nLastSaveTime, m_pledgeBonus[0].m_nTodayRewardLev,
            m_pledgeBonus[1].m_nCurrentBonusPoint, m_pledgeBonus[1].m_nMaxBonusPoint, m_pledgeBonus[1].m_nLastSaveTime,
            m_pledgeBonus[1].m_nTodayRewardLev
        );

        if (payloadLength == 0) {
            assembledSuccessfully = false;
            const std::string pledgeName = Utf16ToUtf8(m_name);
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d] Buffer Overflow in LoadPledge. "
            //     "pledge[%s], membercount[%u]",
            //     "Pledge.cpp", 0x342, pledgeName.c_str(), GetMemberCount()
            // )
            // Log.Add(LOG_ERROR,
            //     "[clan] Cannot assemble the load reply for clan '%s' (ID %u, %u members) in the %zu-byte buffer. Reply not sent.",
            //     pledgeName.c_str(), m_pledgeId, GetMemberCount(), payload.size())
            L2_LOG_ERROR(Log,
                "[clan] Cannot assemble the load reply for clan '%s' (ID %u, %u members) in the %zu-byte buffer; reply not sent.",
                pledgeName.c_str(), m_pledgeId, GetMemberCount(), payload.size());
        }
    }

    if (!assembledSuccessfully)
        return;
    if (checkPendingWrite && !socket->CheckPendingWriteSafe(0x80000)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "SendReplyToLoadPledge. pledge_id[%u] "
        //     "pending_write[%lld] packet_len[%d]",
        //     m_pledgeId, static_cast<long long>(socket->m_nPendingWrite.load(std::memory_order_relaxed)), payloadLength
        // )
        // Log.Add(LOG_ERROR,
        //     "[clan] Load reply for clan %u not sent: socket is closed or write queue has insufficient headroom. Pending bytes %lld, reply bytes %d.",
        //     m_pledgeId, static_cast<long long>(socket->m_nPendingWrite.load(std::memory_order_relaxed)), payloadLength)
        L2_LOG_ERROR(Log,
            "[clan] Load reply for clan %u not sent: socket is closed or write queue has insufficient headroom; pending bytes %lld, reply bytes %d.",
            m_pledgeId, static_cast<long long>(socket->m_nPendingWrite.load(std::memory_order_relaxed)), payloadLength);
        return;
    }
    socket->Send("cb", 0x11, payloadLength, payloadStorage->data());
}

void CPledge::SendReplyToLoadSubPledge(CSocket* socket, unsigned long pledgeTypeMask) {
    // Current Cached.exe 0x00508da8. The target examines only the low eight
    // bits and keeps an unusual equality boundary before each row.
    L2_GUARD_FUNCTION();

    std::array<char, 0x5000> payload{};
    int payloadLength = 0;
    int subPledgeCount = 0;

    std::optional<decltype(m_subPledges.find(MASTER_PLEDGE))> foundStorage;
    std::optional<CPledgeMemberListPacket> membersStorage;
    std::optional<std::string> pledgeNameStorage;
    std::optional<std::string> subPledgeNameStorage;
    {
        L2_READ_GUARD(m_pledgeLock);
        for (unsigned int index = 0; index < 8; ++index) {
            if ((pledgeTypeMask & (1UL << index)) == 0) {
                continue;
            }

            const PledgeType pledgeType = g_PledgeTypeValidator.GetPledgeTypeByIndex(index);
            const auto& found = foundStorage.emplace(m_subPledges.find(pledgeType));
            if (found == m_subPledges.end() || found->second == nullptr) {
                foundStorage.reset();
                continue;
            }

            RecoveredSubPledgeInfo* const subPledge = found->second;
            auto& members = membersStorage.emplace();
            AssembleMemberPacket(members, pledgeType);

            // Shipping instructions at 0x00508fcd-0x00508fd8 compare for exact
            // equality, instead of <= or >=. Keep that observable boundary.
            if (0x4f38 - members.GetLength() == payloadLength) {
                membersStorage.reset();
                foundStorage.reset();
                break;
            }

            const int assembled = ::Assemble(
                payload.data() + payloadLength, static_cast<int>(payload.size()) - payloadLength, "dSdddb",
                static_cast<int>(subPledge->pledgeType), subPledge->name.c_str(), static_cast<int>(subPledge->masterId),
                subPledge->memberLimit, members.GetMemberCount(), members.GetLength(), members.GetPacket()
            );
            if (assembled < 1) {
                const auto& pledgeName = pledgeNameStorage.emplace(Utf16ToUtf8(m_name));
                const auto& subPledgeName = subPledgeNameStorage.emplace(Utf16ToUtf8(subPledge->name.c_str()));
                // Log.Add(
                //     LOG_ERROR,
                //     "[%s][%d] Buffer Overflow in LoadSubPledge. "
                //     "pledge[%s], subPledgeName[%s], membercount[%d]",
                //     "Pledge.cpp", 0x395, pledgeName.c_str(), subPledgeName.c_str(), members.GetMemberCount()
                // )
                // Log.Add(LOG_ERROR,
                //     "[clan] Cannot assemble subgroup '%s' of clan '%s' (ID %u, %d members) in the %zu-byte buffer. Subgroup reply not sent.",
                //     subPledgeName.c_str(), pledgeName.c_str(), m_pledgeId, members.GetMemberCount(), payload.size())
                L2_LOG_ERROR(Log,
                    "[clan] Cannot assemble subgroup '%s' of clan '%s' (ID %u, %d members) in the %zu-byte buffer; subgroup reply not sent.",
                    subPledgeName.c_str(), pledgeName.c_str(), m_pledgeId, members.GetMemberCount(), payload.size());
                return;
            }

            payloadLength += assembled;
            ++subPledgeCount;
            membersStorage.reset();
            foundStorage.reset();
        }
    }

    socket->Send("chddb", 0xc6, 0x2d, static_cast<int>(m_pledgeId), subPledgeCount, payloadLength, payload.data());
}

void CPledge::SendReplyToLoadSkill(CSocket* socket) {
    // Current Cached.exe 0x00503bb8. The third reply dword is the count of a
    // later sub-pledge-skill section. This build always transmits zero.
    L2_GUARD_FUNCTION();

    std::array<char, 0x5000> payload{};
    int payloadLength = 0;
    int skillCount = 0;

    std::optional<std::string> pledgeNameStorage;
    {
        L2_READ_GUARD(m_pledgeLock);
        for (const auto& entry : m_pledgeSkills) {
            ++skillCount;
            const int assembled = ::Assemble(
                payload.data() + payloadLength, static_cast<int>(payload.size()) - payloadLength, "ddd",
                static_cast<int>(entry.first), entry.second.level, entry.second.expirationTime
            );
            if (assembled < 1) {
                const auto& pledgeName = pledgeNameStorage.emplace(Utf16ToUtf8(m_name));
                // Log.Add(
                //     LOG_ERROR,
                //     "[%s][%d] Buffer Overflow in Load Pledge Skill. "
                //     "pledge[%s], skillcount[%d]",
                //     "Pledge.cpp", 0x3c9, pledgeName.c_str(), static_cast<int>(m_pledgeSkills.size())
                // )
                // Log.Add(LOG_ERROR,
                //     "[clan] Cannot assemble skills for clan '%s' (ID %u, %zu skills) in the %zu-byte buffer. Skill reply not sent.",
                //     pledgeName.c_str(), m_pledgeId, m_pledgeSkills.size(), payload.size())
                L2_LOG_ERROR(Log,
                    "[clan] Cannot assemble skills for clan '%s' (ID %u, %zu skills) in the %zu-byte buffer; skill reply not sent.",
                    pledgeName.c_str(), m_pledgeId, m_pledgeSkills.size(), payload.size());
                return;
            }
            payloadLength += assembled;
        }
    }

    socket->Send("chdddb", 0xc6, 0x2e, static_cast<int>(m_pledgeId), skillCount, 0, payloadLength, payload.data());
}

void CPledge::SetPowerGrade(int gradeId, std::uint32_t power) {
    L2_GUARD_FUNCTION();
    if (static_cast<unsigned int>(gradeId) - 1U < m_powers.size()) {
        m_powers[static_cast<std::size_t>(gradeId - 1)] = power;
        return;
    }
    // Log.Add(LOG_ERROR, "Invalide Pledge Power Grade Id(%d)", gradeId)
    // Log.Add(LOG_ERROR,
    //     "[clan] Cannot set permissions for clan %u: rank %d is outside 1..%zu. Cached permissions unchanged.",
    //     m_pledgeId, gradeId, m_powers.size())
    L2_LOG_ERROR(Log,
        "[clan] Cannot set permissions for clan %u: rank %d is outside 1..%zu; cached permissions unchanged.",
        m_pledgeId, gradeId, m_powers.size());
}

void CPledge::ReadLock(const wchar_t* file, int line) {
    // CPledge's source-level ReadLock survives in the C1 owner. Current
    // CacheD inlines the same operation at RequestUpdateUserPledge 0x4634df:
    // the RWLock at CPledge+0x120 is entered before the fixed name copy.
    m_pledgeLock.ReadLock(file, line);
}

void CPledge::ReadUnlock() { m_pledgeLock.ReadUnlock(); }

std::uint32_t CPledge::GetPowerByGradeId(int gradeId) const {
    L2_GUARD_FUNCTION();
    if (static_cast<unsigned int>(gradeId) - 1U < m_powers.size()) {
        return m_powers[static_cast<std::size_t>(gradeId - 1)];
    }
    return 0;
}

int CPledge::GetAcademyGraduatedRewardByJoinLevel(int joinLevel, int /* currentLevel */, int pledgeClass) {
    L2_GUARD_FUNCTION();
    if (pledgeClass != 6)
        return 0;

    const int levelDistance = std::min(40, 85 - joinLevel);
    const int rawReward = (levelDistance * 5 + 50) * 4;
    const int clampedReward = std::max(220, std::min(1000, rawReward));
    return static_cast<int>(static_cast<double>(clampedReward) * GetCacheDConfiguration().pledgeNameValueRate);
}

int CPledge::GetAcademyGraduatedReward(std::uint32_t memberId, int pledgeClass) {
    L2_GUARD_FUNCTION();
    int reward = 0;
    std::optional<decltype(m_academyMembers.find(memberId))> academyStorage;
    {
        L2_READ_GUARD(m_memberLock);
        const auto& academy = academyStorage.emplace(m_academyMembers.find(memberId));
        if (academy != m_academyMembers.end()) {
            CUserSP user = g_userDb.GetUser(static_cast<int>(memberId), true, false, runtime::process::CurrentDatabaseId());
            if (user) {
                reward = GetAcademyGraduatedRewardByJoinLevel(academy->second->JoinLevel(), user->GetLevel(), pledgeClass);
            }
        }
    }
    return reward;
}

void CPledge::SetPledgeBonus(
    int bonusIndex, int currentBonusPoint, int maxBonusPoint, int lastSaveTime, int todayRewardLevel
) {
    L2_GUARD_FUNCTION();
    if (static_cast<unsigned int>(bonusIndex) >= 2U)
        return;
    PledgeBonusInfo& bonus = m_pledgeBonus[bonusIndex];
    bonus.m_nCurrentBonusPoint = currentBonusPoint;
    bonus.m_nMaxBonusPoint = maxBonusPoint;
    bonus.m_nLastSaveTime = lastSaveTime;
    if (todayRewardLevel >= 0) {
        bonus.m_nTodayRewardLev = todayRewardLevel;
    }
}

CWareHouseSP CPledge::GetWareHouse() {
    L2_GUARD_FUNCTION();
    return m_warehouse.GetObjectW();
}

void CPledge::SetCheckSumOnDB() {
    L2_GUARD_FUNCTION();
    CWareHouseSP warehouse = GetWareHouse();
    warehouse->SetCheckSumOnDB();
}

int CPledge::_GetMemberCount() const {
    L2_GUARD_FUNCTION();
    int count = 0;
    for (const auto& entry : m_members) {
        count += static_cast<int>(entry.second.size());
    }
    return count;
}

std::uint32_t CPledge::GetMemberCount() const {
    L2_GUARD_FUNCTION();
    int count;
    {
        L2_READ_GUARD(m_memberLock);
        count = _GetMemberCount();
    }
    return static_cast<std::uint32_t>(count);
}

bool CPledge::IsSubPledgeMaster(std::uint32_t memberId) const {
    L2_GUARD_FUNCTION();
    {
        L2_READ_GUARD(m_memberLock);
        for (const auto& entry : m_subPledges) {
            if (entry.first != ACADEMY && entry.first != MASTER_PLEDGE && entry.second->masterId == memberId) {
                return true;
            }
        }
    }
    return false;
}

PledgeType CPledge::GetSubMasterPledgeType(std::uint32_t memberId) const {
    L2_GUARD_FUNCTION();
    PledgeType result = MASTER_PLEDGE;
    {
        L2_READ_GUARD(m_memberLock);
        for (const auto& entry : m_subPledges) {
            if (entry.first != ACADEMY && entry.first != MASTER_PLEDGE && entry.second->masterId == memberId) {
                result = entry.second->pledgeType;
                break;
            }
        }
    }
    return result;
}

CAcademyMember* CPledge::GetAcademyMember(std::uint32_t memberId) const {
    L2_GUARD_FUNCTION();
    const auto found = m_academyMembers.find(memberId);
    return found == m_academyMembers.end() ? nullptr : found->second;
}

bool CPledge::UpdateSubPledgeMaster(PledgeType pledgeType, std::uint32_t masterId) {
    L2_GUARD_FUNCTION();
    std::optional<decltype(m_subPledges.find(pledgeType))> foundStorage;
    {
        L2_WRITE_GUARD(m_memberLock);
        const auto& found = foundStorage.emplace(m_subPledges.find(pledgeType));
        if (found == m_subPledges.end()) {
            return false;
        }
        found->second->masterId = masterId;
    }
    return true;
}

bool CPledge::SetAcademyMaster(std::uint32_t memberId, std::uint32_t academyMasterId) {
    L2_GUARD_FUNCTION();
    std::optional<decltype(m_academyMembers.find(memberId))> foundStorage;
    {
        L2_WRITE_GUARD(m_memberLock);
        const auto& found = foundStorage.emplace(m_academyMembers.find(memberId));
        if (found == m_academyMembers.end()) {
            return false;
        }
        found->second->SetAcademyMasterId(academyMasterId);
    }
    return true;
}

CSPointer<CUser> CPledge::GetSubpledgeMaster(PledgeType pledgeType) const {
    L2_GUARD_FUNCTION();
    std::optional<CUserSP> masterStorage;
    std::optional<decltype(m_subPledges.find(pledgeType))> foundStorage;
    {
        L2_READ_GUARD(m_memberLock);
        auto& master = masterStorage.emplace();
        const auto& found = foundStorage.emplace(m_subPledges.find(pledgeType));
        if (found != m_subPledges.end() && found->second->masterId != 0) {
            master =
                g_userDb.GetUser(static_cast<int>(found->second->masterId), true, false, runtime::process::CurrentDatabaseId());
        }
    }
    return std::move(*masterStorage);
}

const char16_t* CPledge::GetSubpledgeName(PledgeType pledgeType) const {
    L2_GUARD_FUNCTION();
    std::optional<decltype(m_subPledges.find(pledgeType))> foundStorage;
    const char16_t* result;
    {
        L2_READ_GUARD(m_memberLock);
        const auto& found = foundStorage.emplace(m_subPledges.find(pledgeType));
        result = kEmptyTargetString;
        if (found != m_subPledges.end()) {
            result = found->second->name.c_str();
        }
        // The shipping function likewise returns the xwstring buffer after
        // dropping the member lock.
    }
    return result;
}

bool CPledge::SetSubpledgeName(PledgeType pledgeType, const wchar_t* name) {
    L2_GUARD_FUNCTION();
    std::optional<decltype(m_subPledges.find(pledgeType))> foundStorage;
    std::optional<std::u16string> convertedStorage;
    {
        L2_WRITE_GUARD(m_memberLock);
        const auto& found = foundStorage.emplace(m_subPledges.find(pledgeType));
        if (found == m_subPledges.end()) {
            return false;
        }
        const auto& converted = convertedStorage.emplace(WideToUtf16(name));
        found->second->name.assign(converted);
    }
    return true;
}

void CPledge::AddAcademyMember(std::uint32_t memberId, int joinLevel, std::uint32_t academyMasterId) {
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(m_memberLock);
        if (m_academyMembers.find(memberId) == m_academyMembers.end()) {
            CAcademyMember* const member = new (std::nothrow) CAcademyMember(memberId, joinLevel, academyMasterId);
            m_academyMembers.emplace(memberId, member);
        }
    }
}

void CPledge::SetPledgeTypePresent(PledgeType pledgeType) {
    // Current Cached.exe 0x00502a64. The original private identifier is not
    // kept. This semantic name describes its exact bitset operation.
    const unsigned int index = g_PledgeTypeValidator.GetIndex(pledgeType);
    if (index >= 8U) {
        runtime::process::HandleInvalidParameter(L"CPledge pledge-type bit index < 8");
        return;
    }
    m_pledgeTypeMask |= (1U << index);
}

void CPledge::AddSubPledge(PledgeType pledgeType, std::uint32_t masterId, const wchar_t* name, int memberLimit) {
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(m_memberLock);
        if (m_subPledges.find(pledgeType) == m_subPledges.end()) {
            RecoveredSubPledgeInfo* const info =
                new (std::nothrow) RecoveredSubPledgeInfo(pledgeType, masterId, name, memberLimit);
            m_subPledges.emplace(pledgeType, info);
            SetPledgeTypePresent(pledgeType);
        }
    }
}

void CPledge::_AddPledgeSkillWithoutSql(std::uint32_t skillId, int level, std::int32_t expirationTime, bool forceUpdate) {
    L2_GUARD_FUNCTION();
    const auto found = m_pledgeSkills.find(skillId);
    if (found == m_pledgeSkills.end()) {
        m_pledgeSkills.emplace(skillId, PledgeSkillValue{level, expirationTime});
    } else if (forceUpdate || found->second.level < level) {
        found->second.level = level;
        found->second.expirationTime = expirationTime;
    }
}

void CPledge::AddMember(std::uint32_t memberId, PledgeType pledgeType) {
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(m_memberLock);
        m_members[pledgeType].insert(memberId);
    }
}

void CPledge::SetDawnRoundToMember(int dawnRound) {
    L2_GUARD_FUNCTION();
    {
        L2_READ_GUARD(m_memberLock);
        for (const auto& pledgeMembers : m_members) {
            for (const std::uint32_t memberId : pledgeMembers.second) {
                CUserSP user = g_userDb.GetUser(static_cast<int>(memberId), true, false, runtime::process::CurrentDatabaseId());
                if (!user)
                    continue;
                {
                    L2_WRITE_GUARD(*user.operator->());
                    user->SaveSSQDawnRound(dawnRound);
                }
            }
        }
    }
}

void CPledge::RemoveMember(std::uint32_t memberId, PledgeType pledgeType) {
    L2_GUARD_FUNCTION();
    std::optional<decltype(m_members.find(pledgeType))> memberSetStorage;
    {
        L2_WRITE_GUARD(m_memberLock);

        const auto& memberSet = memberSetStorage.emplace(m_members.find(pledgeType));
        if (memberSet != m_members.end()) {
            memberSet->second.erase(memberId);
        }

        if (pledgeType == ACADEMY) {
            const auto academy = m_academyMembers.find(memberId);
            if (academy != m_academyMembers.end()) {
                DBConn connection(runtime::process::CurrentDatabaseId());
                constexpr const wchar_t* statement = L"{Call lin_DeleteAcademyMember (?, ?)}";
                connection.Prepare(statement);
                connection.BindParameter(&memberId);
                connection.BindParameter(&m_pledgeId);
                if (connection.ExecutePrepared(statement)) {
                    delete academy->second;
                    m_academyMembers.erase(academy);
                }
            }
        } else {
            for (auto& entry : m_academyMembers) {
                CAcademyMember* const academy = entry.second;
                if (academy->AcademyMasterId() != memberId)
                    continue;

                DBConn connection(runtime::process::CurrentDatabaseId());
                constexpr const wchar_t* statement = L"{Call lin_ResetAcademyMaster (?, ?)}";
                connection.Prepare(statement);
                connection.BindParameter(&memberId);
                connection.BindParameter(&m_pledgeId);
                if (connection.ExecutePrepared(statement)) {
                    academy->SetAcademyMasterId(0);
                    break;
                }
            }
        }
    }
}

void CPledge::ReorganizeMember(std::uint32_t memberId, PledgeType oldPledgeType, PledgeType newPledgeType) {
    L2_GUARD_FUNCTION();
    std::optional<decltype(m_members.find(oldPledgeType))> oldMembersStorage;
    {
        L2_WRITE_GUARD(m_memberLock);
        const auto& oldMembers = oldMembersStorage.emplace(m_members.find(oldPledgeType));
        if (oldMembers != m_members.end()) {
            oldMembers->second.erase(memberId);
        }
        m_members[newPledgeType].insert(memberId);
    }
}

bool CPledge::GraduateAcademyMember(std::uint32_t memberId) {
    L2_GUARD_FUNCTION();
    std::optional<decltype(m_members.find(ACADEMY))> academySetStorage;
    std::optional<decltype(m_academyMembers.find(memberId))> academyStorage;
    {
        L2_WRITE_GUARD(m_memberLock);

        const auto& academySet = academySetStorage.emplace(m_members.find(ACADEMY));
        if (academySet != m_members.end()) {
            academySet->second.erase(memberId);
        }
        const auto& academy = academyStorage.emplace(m_academyMembers.find(memberId));
        if (academy != m_academyMembers.end()) {
            DBConn connection(runtime::process::CurrentDatabaseId());
            constexpr const wchar_t* statement = L"{Call lin_GraduateAcademyMember (?, ?)}";
            connection.Prepare(statement);
            connection.BindParameter(&memberId);
            connection.BindParameter(&m_pledgeId);
            if (connection.ExecutePrepared(statement)) {
                delete academy->second;
                m_academyMembers.erase(academy);
            }
        }
    }
    return true;
}

bool CPledge::DeleteCrest() {
    L2_GUARD_FUNCTION();
    // Current 0x00505f88 clears the in-memory value before SQL, including on
    // an execution failure.
    m_crestId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_InitPledgeCrest (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_pledgeId);
    return connection.ExecutePrepared(statement);
}

bool CPledge::DeleteEmblem() {
    L2_GUARD_FUNCTION();
    // Current 0x005060ac has the same deliberate memory-first ordering.
    m_newEmblemId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_InitPledgeNewEmblem (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_pledgeId);
    return connection.ExecutePrepared(statement);
}

bool CPledge::CreateAcademy(const wchar_t* academyName) {
    L2_GUARD_FUNCTION();
    if (m_academyExists)
        return false;

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_CreateAcademy (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_pledgeId);
    connection.BindParameter(academyName);
    if (!connection.ExecutePrepared(statement))
        return false;

    AssignFixedName(m_academyName, academyName != nullptr ? WideToUtf16(academyName).c_str() : nullptr, L"academyName length < 24");
    m_academyExists = true;
    return true;
}

bool CPledge::CreateSubPledge(PledgeType pledgeType, const wchar_t* name, std::uint32_t masterId) {
    L2_GUARD_FUNCTION();
    if (m_subPledges.find(pledgeType) != m_subPledges.end()) {
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_CreateSubPledge (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_pledgeId);
    connection.BindParameter(&pledgeType);
    connection.BindParameter(name);
    connection.BindParameter(&masterId);
    if (!connection.ExecutePrepared(statement))
        return false;

    RecoveredSubPledgeInfo* const info = new (std::nothrow) RecoveredSubPledgeInfo(pledgeType, masterId, name, 0);
    m_subPledges.emplace(pledgeType, info);
    SetPledgeTypePresent(pledgeType);
    return true;
}

bool CPledge::AddPledgeSkill(std::uint32_t skillId, int level, std::int32_t expirationTime, bool forceUpdate) {
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_AcquirePledgeSkill (?, ?, ?, ?)}";
    // Current helper 0x005055cc binds Win64 `long` through the dedicated
    // SQL_C_SLONG/SQL_DOUBLE overload. Keep the C++ value int32-wide while
    // preserving that exact ODBC/PostgreSQL call ABI.
    const long expirationParameter = static_cast<long>(expirationTime);
    connection.Prepare(statement);
    connection.BindParameter(&m_pledgeId);
    connection.BindParameter(&skillId);
    connection.BindParameter(&level);
    connection.BindParameter(&expirationParameter);
    if (!connection.ExecutePrepared(statement))
        return false;

    {
        L2_WRITE_GUARD(m_pledgeLock);
        _AddPledgeSkillWithoutSql(skillId, level, expirationTime, forceUpdate);
    }
    return true;
}

bool CPledge::DelPledgeSkill(std::uint32_t skillId) {
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeletePledgeSkill (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_pledgeId);
    connection.BindParameter(&skillId);
    if (!connection.ExecutePrepared(statement))
        return false;

    std::optional<decltype(m_pledgeSkills.find(skillId))> foundStorage;
    {
        L2_WRITE_GUARD(m_pledgeLock);
        const auto& found = foundStorage.emplace(m_pledgeSkills.find(skillId));
        if (found != m_pledgeSkills.end()) {
            m_pledgeSkills.erase(found);
        }
    }
    return true;
}

bool CPledge::AddPledgeNameValueLog(std::uint32_t pledgeId, int logId, int oldValue, int newValue, int delta) {
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_AddPledgeNameValueLog (?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&logId);
    connection.BindParameter(&oldValue);
    connection.BindParameter(&newValue);
    connection.BindParameter(&delta);
    return connection.ExecutePrepared(statement);
}

bool CPledge::UpdateNameValue(int requestedDelta, int& updatedValue, int logId) {
    L2_GUARD_FUNCTION();
    const int previousValue = m_rootNameValue;
    const int appliedDelta = ClampNameValueDelta(previousValue, requestedDelta);

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&m_rootNameValue);
    constexpr const wchar_t* statement = L"{Call lin_UpdatePledgeNameValue (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_pledgeId);
    connection.BindParameter(&appliedDelta);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        updatedValue = m_rootNameValue;
        CPledgeRanking::Update(m_pledgeId, m_rootNameValue);
        AddPledgeNameValueLog(m_pledgeId, logId, previousValue, m_rootNameValue, appliedDelta);
        return true;
    }
    updatedValue = m_rootNameValue;
    return false;
}

bool CPledge::CanApplyNameValueDelta(int requestedDelta) const noexcept {
    // Current Cached.exe helper 0x00501f3c has no surviving symbol. It is
    // the exact non-negative gate used by EtcMani and point manipulation:
    // both the stored value and the signed 32-bit sum must remain >= 0.
    if (m_rootNameValue < 0)
        return false;
    const std::uint32_t wrappedSum =
        static_cast<std::uint32_t>(m_rootNameValue) + static_cast<std::uint32_t>(requestedDelta);
    return (wrappedSum & 0x80000000U) == 0;
}

int CPledge::rootNameValue() const noexcept {
    // Current Cached.exe reads CPledge +0xb0 directly at 0x004c05fc. The
    // surviving C1 CPledge source supplies this original accessor name.
    return m_rootNameValue;
}

bool CPledge::UpdateNameValue(int requestedDelta, int logId) {
    L2_GUARD_FUNCTION();
    bool result;
    {
        L2_WRITE_GUARD(m_nameValueLock);
        int updatedValue = 0;
        result = UpdateNameValue(requestedDelta, updatedValue, logId);
    }
    return result;
}

void CPledge::LoadMergedInfo() {
    L2_GUARD_FUNCTION();
    std::uint32_t mergedPledgeId = 0;
    std::array<char16_t, 24> mergedPledgeName{};
    std::int64_t nullableIndicator = -1;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.BindNullable(&mergedPledgeId, &nullableIndicator);
    connection.BindNullable(mergedPledgeName.data(), 0x30, &nullableIndicator);
    int loadFlag = 1;
    constexpr const wchar_t* statement = L"{Call lin_LoadMergedInfo (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_pledgeId);
    connection.BindParameter(&loadFlag);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch())
        return;

    m_mergedPledgeId = mergedPledgeId;
    AssignFixedName(m_mergedPledgeName, mergedPledgeName.data(), L"mergedPledgeName length < 24");
}

bool CPledge::SetCastleSiegeDefenceCount(int count) {
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetPledgeCastleSiegeDefenceCount (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_pledgeId);
    connection.BindParameter(&count);
    if (!connection.ExecutePrepared(statement))
        return false;
    m_castleSiegeDefenceCount = static_cast<std::uint32_t>(count);
    return true;
}

void CPledge::SetLoadTime() {
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetPledgeLoadTime (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_pledgeId);
    connection.ExecutePrepared(statement);
}

void CPledge::SetUnloadTime() {
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetPledgeUnloadTime (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_pledgeId);
    connection.ExecutePrepared(statement);
}

void CPledge::OnItemCheckFailed(int checkFailType) {
    L2_GUARD_FUNCTION();
    if (!g_bUseDBSecurity)
        return;

    CUserSP ruler = g_userDb.GetUser(static_cast<int>(m_rulerId), true, false, runtime::process::CurrentDatabaseId());
    const std::int64_t characterId = ruler ? static_cast<std::int64_t>(ruler->GetId()) : 0;
    const std::int64_t accountId = ruler ? static_cast<std::int64_t>(ruler->GetAccountID()) : 0;
    WritePledgeItemCheckAudit(
        m_name, characterId, accountId, g_nDBSecurityPenaltyLevel, checkFailType, ruler ? ruler->GetCharName() : u"",
        ruler ? ruler->GetAccountName() : u""
    );
}

void CPledge::ItemCheckOnLoad() {
    L2_GUARD_FUNCTION();
    if (!g_bUseDBSecurity)
        return;
    SetLoadTime();
    CWareHouseSP warehouse = GetWareHouse();
    if (!warehouse->ItemcheckOnDB()) {
        OnItemCheckFailed(0);
    }
}

void CPledge::ItemCheckOnUnload() {
    L2_GUARD_FUNCTION();
    if (!g_bUseDBSecurity)
        return;
    CWareHouseSP warehouse = GetWareHouse();
    if (!warehouse->ItemCheckBetweenMemoryAndDB()) {
        OnItemCheckFailed(1);
    } else {
        SetCheckSumOnDB();
    }
    SetUnloadTime();
}

void CPledge::LoadPledgeBonus() {
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&m_pledgeBonus[0].m_nCurrentBonusPoint);
    connection.Bind(&m_pledgeBonus[0].m_nMaxBonusPoint);
    connection.Bind(&m_pledgeBonus[0].m_nLastSaveTime);
    connection.Bind(&m_pledgeBonus[0].m_nTodayRewardLev);

    int bonusIndex = 0;
    constexpr const wchar_t* statement = L"{Call lin_LoadPledgeBonus(?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_pledgeId);
    connection.BindParameter(&bonusIndex);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }

    connection.UnBind();
    connection.Bind(&m_pledgeBonus[1].m_nCurrentBonusPoint);
    connection.Bind(&m_pledgeBonus[1].m_nMaxBonusPoint);
    connection.Bind(&m_pledgeBonus[1].m_nLastSaveTime);
    connection.Bind(&m_pledgeBonus[1].m_nTodayRewardLev);
    bonusIndex = 1;
    connection.Prepare(statement);
    connection.BindParameter(&m_pledgeId);
    connection.BindParameter(&bonusIndex);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }
}
