#include "MultiPartyCommandChannel.h"
#include "CContributeData.h"
#include "CWorld.h"
#include "CWorldObject.h"
#include "NPC.h"
#include "Party.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/AtomicJob.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace {


constexpr std::size_t kSharedPartyCapacity = 50;


class CheckPointOverlap final {
public:
    CheckPointOverlap(FVector& point, int distance) noexcept : m_point(point), m_distance(distance) {}

    bool operator()(FVector& point) {
        L2_GUARD_FUNCTION();
        const double deltaY = point.y - m_point.y;
        const double deltaX = point.x - m_point.x;
        return deltaX * deltaX + deltaY * deltaY <
            static_cast<double>(runtime::MultiplyInt32Wrapping(m_distance, m_distance));
    }

private:
    FVector& m_point;
    int m_distance;
};


bool CanGetExpSp(User* user, CCreature* creature, int radius, bool ignoreZone) {
    const FVector userPosition = user->GetPos();
    const FVector creaturePosition = creature->GetPos();
    const double distanceSquared = (creaturePosition.x - userPosition.x) * (creaturePosition.x - userPosition.x) +
        (creaturePosition.y - userPosition.y) * (creaturePosition.y - userPosition.y) +
        (creaturePosition.z - userPosition.z) * (creaturePosition.z - userPosition.z);
    const double radiusSquared = static_cast<double>(runtime::MultiplyInt32Wrapping(radius, radius));
    if (radiusSquared <= distanceSquared) {
        return false;
    }
    return creature->GetInZoneID() == user->GetInZoneID() || ignoreZone;
}

const wchar_t* AccountNameForRoutingLog(const User& user, std::array<wchar_t, 16>& storage) noexcept {
#if defined(_WIN32)
    static_cast<void>(storage);
    return reinterpret_cast<const wchar_t*>(user.m_sAccountName.data());
#else
    for (std::size_t index = 0;
        index + 1 < storage.size() && index < user.m_sAccountName.size() && user.m_sAccountName[index] != u'\0'; ++index) {
        storage[index] = static_cast<wchar_t>(user.m_sAccountName[index]);
    }
    return storage.data();
#endif
}

void LogMasterPartyRouting(CMultiPartyCommandChannel& channel, CNPC& npc, User& master, int routing) {
    std::array<wchar_t, 16> accountNameStorage{};
    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,,,,,,,%I64d,%I64d,%I64d,,,,,,,,%s,%s,,,",
    //     static_cast<std::int64_t>(0x36d),
    //     static_cast<std::int64_t>(master.GetDBID()),
    //     static_cast<std::int64_t>(master.GetAccountID()),
    //     static_cast<std::int64_t>(channel.GetSmartID().Sid()),
    //     static_cast<std::int64_t>(npc.GetSmartID().Sid()),
    //     static_cast<std::int64_t>(routing),
    //     master.GetName(),
    //     AccountNameForRoutingLog(master, accountNameStorage))
    Log.Add(
        LOG_BOTH, u"%I64d,%I64d,%I64d,,,,,,,,,%I64d,%I64d,%I64d,,,,,,,,%s,%s,,,", static_cast<std::int64_t>(0x36d),
        static_cast<std::int64_t>(master.GetDBID()), static_cast<std::int64_t>(master.GetAccountID()),
        static_cast<std::int64_t>(channel.GetSmartID().Sid()), static_cast<std::int64_t>(npc.GetSmartID().Sid()),
        static_cast<std::int64_t>(routing), master.GetName(), AccountNameForRoutingLog(master, accountNameStorage)
    );
}

} // namespace

CMultiPartyCommandChannel::CMultiPartyCommandChannel(std::uint32_t masterPartySid) {
    m_partySidList.push_back(masterPartySid);
    m_isTeleporting = false;

    m_data->m_masterPartySid = masterPartySid;
    m_data->m_partyCount = 1;

    m_routingType = static_cast<MPCCRoutingType>(0);
    m_routingNpcId = 0;

    std::memset(m_data->m_partySidList, 0, sizeof(m_data->m_partySidList));
    _UpdateSharedPartySidList();

    runtime::raw_atomic::ExchangeI32(&m_divide, 0);
    CGeneralObject::OnCreate();
}

CMultiPartyCommandChannel::~CMultiPartyCommandChannel() = default;

L2_DEFINE_OBJECT_CLASS_ID(CMultiPartyCommandChannel)

L2_DEFINE_OBJECT_CLASS_NAME(CMultiPartyCommandChannel, m_pClassName)

L2_DEFINE_OBJECT_SINGLE_ALLOCATOR(CMultiPartyCommandChannel, __GetClassName())

L2_DEFINE_OBJECT_TYPED_RELEASE(CMultiPartyCommandChannel)

int CMultiPartyCommandChannel::GetIndex() const { return static_cast<int>(m_nId & 0x000fffffU); }

bool CMultiPartyCommandChannel::IsMPCC() { return true; }

CMultiPartyCommandChannel* CMultiPartyCommandChannel::CastMPCC() { return this; }

void CMultiPartyCommandChannel::SetMaxPartyCanJoin(int maximumPartyCount) {
    L2_GUARD_FUNCTION();
    if (static_cast<std::uint32_t>(maximumPartyCount) - 2U < 0x31U) {
        s_maxPartyCanJoin = maximumPartyCount;
    }
}

void CMultiPartyCommandChannel::CalcMPCCTeleportPointCount(int memberCount, int& pointCount, int& pointSize) {
    L2_GUARD_FUNCTION();
    if (pointSize < memberCount) {
        const int quotient = memberCount / pointSize;
        pointCount = quotient + 1;
        if (memberCount % pointSize < 10 && pointCount > 1) {
            pointCount = quotient;
            pointSize = memberCount / quotient;
        }
    } else {
        pointCount = 1;
    }
}

int CMultiPartyCommandChannel::GetMembersForTeleport(std::vector<User*>& users, int range) {
    // Current L2Server 0x0084eb7c.
    L2_GUARD_FUNCTION();

    if (GetMaster() == nullptr) {
        return 0;
    }

    const FVector masterPosition = GetMaster()->GetPos();
    std::vector<CParty*> parties;
    GetAllParty(parties);
    users.push_back(GetMaster());

    const double rangeSquared = static_cast<double>(runtime::MultiplyInt32Wrapping(range, range));
    for (CParty* const party : parties) {
        if (party == nullptr) {
            continue;
        }

        std::vector<User*> members;
        party->GetAllMember(members);
        for (User* const member : members) {
            if (member == nullptr || IsMaster(static_cast<int>(member->GetServerId()))) {
                continue;
            }

            const FVector position = member->GetPos();
            const double deltaX = position.x - masterPosition.x;
            const double deltaY = position.y - masterPosition.y;
            const double deltaZ = position.z - masterPosition.z;
            if (deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ < rangeSquared) {
                users.push_back(member);
            }
        }
    }
    return static_cast<int>(users.size());
}

void CMultiPartyCommandChannel::CalcMPCCTeleportPointList(
    std::vector<FVector>& points, const FVector& center, int pointCount, int minimumDistance, int maximumDistance
) {
    L2_GUARD_FUNCTION();

    points.clear();
    std::int32_t sectorStart = 0;
    const std::int32_t distanceSpan =
        runtime::AbsoluteInt32Wrapping(runtime::SubtractInt32Wrapping(maximumDistance, minimumDistance));
    if (pointCount <= 0) {
        return;
    }

    const std::int32_t sectorSize = 0x10000 / pointCount;
    const double sectorWidth = static_cast<double>(runtime::AddInt32Wrapping(sectorSize, 1));
    const double distanceWidth = static_cast<double>(runtime::AddInt32Wrapping(distanceSpan, 1));
    const FVector rotationAxis{0.0, 0.0, 1.0};

    for (int pointIndex = 0; pointIndex < pointCount; ++pointIndex) {
        FVector candidate{};
        int attemptCount = 0;
        std::vector<FVector>::iterator overlap;
        do {
            const double angleRandom = ::genrand64_real2();
            const double distanceRandom = ::genrand64_real2();
            const std::int32_t angle =
                runtime::AddInt32Wrapping(runtime::Win64CvttDoubleToInt32(angleRandom * sectorWidth), sectorStart);
            const std::int32_t distance = runtime::AddInt32Wrapping(
                runtime::Win64CvttDoubleToInt32(distanceRandom * distanceWidth), minimumDistance
            );

            const FVector radialVector{
                static_cast<double>(distance),
                0.0,
                0.0,
            };
            const FVector rotated = FVector::RotateAngleAxis(&radialVector, angle, &rotationAxis);
            candidate = FVector{
                rotated.x + center.x,
                rotated.y + center.y,
                rotated.z + center.z,
            };

            CheckPointOverlap predicate(candidate, 200);
            overlap = std::find_if(points.begin(), points.end(), predicate);
            ++attemptCount;
        } while (overlap != points.end() && attemptCount < 100);

        points.push_back(candidate);
        sectorStart = runtime::AddInt32Wrapping(sectorStart, sectorSize);
    }
}

int CMultiPartyCommandChannel::TeleportMPCCToLocation(
    int x,
    int y,
    int z,
    int range,
    int minimumDistance,
    int maximumDistance,
    int itemClassId,
    std::int64_t itemCount,
    int questId,
    int inZoneId
) {
    // Current L2Server 0x00850440.
    L2_GUARD_FUNCTION();

    m_isTeleporting = true;
    int teleportedCount = 0;
    int pointCount = 0;
    int pointSize = 50;
    const FVector center{
        static_cast<double>(x),
        static_cast<double>(y),
        static_cast<double>(z),
    };

    std::vector<User*> users;
    const int memberCount = GetMembersForTeleport(users, range);
    CalcMPCCTeleportPointCount(memberCount, pointCount, pointSize);

    std::vector<FVector> points;
    CalcMPCCTeleportPointList(points, center, pointCount, minimumDistance, maximumDistance);

    User* const master = GetMaster();
    if (master == nullptr) {
        m_isTeleporting = false;
        return 0;
    }
    const FVector masterPosition = master->GetPos();
    const double rangeSquared = static_cast<double>(runtime::MultiplyInt32Wrapping(range, range));

    auto queueItemTeleport = [this, itemClassId, itemCount, questId, inZoneId](
                                 User* user, int destinationX, int destinationY, int destinationZ, int,
                                 [[maybe_unused]] int insertLine, int
                             ) -> int {
        ItemManip request(questId, itemClassId, runtime::NegateInt64Wrapping(itemCount), 0, 0, 0, true);
        AtomicTeleportTo* const job = new AtomicTeleportTo(
            static_cast<int>(user->Id()), destinationX, destinationY, destinationZ, static_cast<unsigned int>(inZoneId)
        );

        L2_GUARD(user->m_ManipulateQueueLock);
        CPacketHelperEx<20480> packet;

        int masterSid = 0;
        if (IsMaster(static_cast<int>(user->GetServerId()))) {
            masterSid = static_cast<int>(user->GetServerId());
        }
        User::etcMsg message =
            user->SaveEtcMsg(false, false, itemClassId, masterSid, itemCount, std::u16string(), std::u16string(), 0);
        User::ManipulateQueue queue = user->SaveManipulateQueue(
            ManipulateType::MANI_MPCC_TELEPORT, 0, 1, &request, job, 0, packet, false, ItemGetReason::Etc, message
        );

        const User::ManiItemResultType result = user->_SetManiItemJob(queue);
        if (result == User::MRT_NOJOB) {
            if (user->_HandleItemManipulation(queue)) {
                return 1;
            }
            user->GetSocket()->Send(
                "chccdcQ", 0x62, SystemMessageId::NAMED_ITEM_QUANTITY_REQUIRED, 2, 3, itemClassId, 6, itemCount
            );
            job->Destroy();
            return IsMaster(static_cast<int>(user->GetServerId())) ? -1 : 0;
        }
        if (result == User::MRT_JOBMAP) {
            job->Destroy();
            user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
            return 0;
        }
        if (result == User::MRT_JOBNOTMAP) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
            //     L"MultiPartyCommandChannel.cpp",
            //     insertLine)
            // Noise: The item-cost teleport is queued behind an active operation. Normal queue waiting is not an error.
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
            //     L"MultiPartyCommandChannel.cpp",
            //     insertLine)
            return 1;
        }

        job->Destroy();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"MultiPartyCommandChannel.cpp",
        //     errorLine)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"MultiPartyCommandChannel.cpp",
        //     errorLine)
        L2_LOG_ERROR(
            Log,
            "Command-channel teleport item operation returned unexpected queue result %d (item %d, cost %lld); its job was "
            "destroyed.",
            static_cast<int>(result), itemClassId, static_cast<long long>(itemCount)
        );
        return 0;
    };

    int teleportedAtPoint = 0;
    FVector teleportPoint{};
    auto point = points.begin();
    auto user = users.begin();
    for (; user != users.end() && point != points.end(); ++user) {
        User* const target = *user;
        teleportPoint = *point;
        if (target != nullptr) {
            const FVector position = target->GetPos();
            const double deltaX = position.x - masterPosition.x;
            const double deltaY = position.y - masterPosition.y;
            if (deltaX * deltaX + deltaY * deltaY < rangeSquared) {
                const int destinationX = runtime::AddInt32Wrapping(
                    runtime::Win64CvttDoubleToInt32(teleportPoint.x),
                    runtime::Win64CvttDoubleToInt32(::genrand64_real2() * 61.0)
                );
                const int destinationY = runtime::AddInt32Wrapping(
                    runtime::Win64CvttDoubleToInt32(teleportPoint.y),
                    runtime::Win64CvttDoubleToInt32(::genrand64_real2() * 61.0)
                );
                const int destinationZ = CWorld::Inst().GetGeo()->FindGround(
                    destinationX, destinationY,
                    runtime::AddInt32Wrapping(runtime::Win64CvttDoubleToInt32(teleportPoint.z), 200)
                );

                int result = 0;
                if (itemClassId < 1 || itemCount < 1) {
                    if (target->TeleportToLocation(
                            destinationX, destinationY, destinationZ, static_cast<unsigned int>(inZoneId), 0x10000,
                            TeleportSourceType::TeleportMPCCToLocation1, nullptr, 0
                        )) {
                        result = 1;
                    } else if (IsMaster(static_cast<int>(target->GetServerId()))) {
                        m_isTeleporting = false;
                        return 0;
                    }
                } else {
                    result = queueItemTeleport(target, destinationX, destinationY, destinationZ, 0x14d, 0x180, 0x186);
                    if (result < 0) {
                        m_isTeleporting = false;
                        return 0;
                    }
                }

                if (result > 0) {
                    ++teleportedAtPoint;
                    ++teleportedCount;
                }
            }
        }

        if (teleportedAtPoint == pointSize) {
            ++point;
            teleportedAtPoint = 0;
        }
    }

    // Current target reuses the last copied point after the generated point
    // list is exhausted. Its direct, no-item teleport in this branch does
    // not contribute to the returned success count.
    for (; user != users.end(); ++user) {
        User* const target = *user;
        if (target == nullptr) {
            continue;
        }

        const FVector position = target->GetPos();
        const double deltaX = position.x - masterPosition.x;
        const double deltaY = position.y - masterPosition.y;
        if (deltaX * deltaX + deltaY * deltaY >= rangeSquared) {
            continue;
        }

        const int destinationX = runtime::Win64CvttDoubleToInt32(teleportPoint.x);
        const int destinationY = runtime::Win64CvttDoubleToInt32(teleportPoint.y);
        const int destinationZ = runtime::Win64CvttDoubleToInt32(teleportPoint.z);
        if (itemClassId < 1 || itemCount < 1) {
            static_cast<void>(target->TeleportToLocation(
                destinationX, destinationY, destinationZ, static_cast<unsigned int>(inZoneId), 0x10000,
                TeleportSourceType::TeleportMPCCToLocation2, nullptr, 0
            ));
            continue;
        }

        const int result = queueItemTeleport(target, destinationX, destinationY, destinationZ, 0x1b4, 0x1e8, 0x1ee);
        if (result < 0) {
            m_isTeleporting = false;
            return 0;
        }
        if (result > 0) {
            ++teleportedCount;
        }
    }

    m_isTeleporting = false;
    return teleportedCount;
}

void CMultiPartyCommandChannel::SetRouting(MPCCRoutingType routingType, int npcId) {
    L2_GUARD_FUNCTION();
    m_routingType = routingType;
    m_routingNpcId = npcId;
}

int CMultiPartyCommandChannel::GetRouting() {
    L2_GUARD_FUNCTION();
    return static_cast<int>(m_routingType);
}

CNPC* CMultiPartyCommandChannel::GetRoutingNpc() {
    L2_GUARD_FUNCTION();

    CCreature* const object = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_routingNpcId));
    if (!object || !object->IsNPC()) {
        return nullptr;
    }
    return object->CastNPC();
}

void CMultiPartyCommandChannel::SetMasterPartyRouting(CNPC* npc) {
    // Current L2Server 0x0084e304.
    L2_GUARD_FUNCTION();
    if (npc == nullptr) {
        return;
    }

    const std::uint32_t channelSid = GetSmartID().Sid();
    const std::uint32_t routingSid = npc->m_pContribute->GetMPCCRouting();
    if (routingSid == 0) {
        npc->m_pContribute->SetMPCCRouting(channelSid);
        SetRouting(MRT_MASTER_PARTY_FIRST, static_cast<int>(npc->GetSmartID().Sid()));

        SendMPCCRoutingInfo routingInfo(1);
        CMultiPartyVisitor routingVisitor(routingInfo);
        Accept(routingVisitor);

        User* const master = GetMaster();
        if (master != nullptr) {
            BroadcastSystemMsg_S message(
                SystemMessageId::COMMAND_CHANNEL_MASTER_PARTY_GRANTED_LOOT_RIGHTS, 0x0c, master->GetName()
            );
            CMultiPartyVisitor messageVisitor(message);
            Accept(messageVisitor);
            LogMasterPartyRouting(*this, *npc, *master, 1);
        }
        return;
    }

    if (routingSid != channelSid) {
        BroadcastSystemMsg message(SystemMessageId::COMMAND_CHANNEL_WITH_LOOT_RIGHTS_ALREADY_EXISTS);
        CMultiPartyVisitor visitor(message);
        Accept(visitor);
    }
}

void CMultiPartyCommandChannel::ResetMasterPartyRouting() {
    // Current L2Server 0x0084e558.
    L2_GUARD_FUNCTION();
    CNPC* const npc = GetRoutingNpc();
    if (npc != nullptr) {
        npc->m_pContribute->SetMPCCRouting(0);
        User* const master = GetMaster();
        if (master != nullptr) {
            LogMasterPartyRouting(*this, *npc, *master, 0);
        }
    }

    SetRouting(MRT_NORMAL, 0);
    SendMPCCRoutingInfo routingInfo(0);
    CMultiPartyVisitor visitor(routingInfo);
    Accept(visitor);
}

int CMultiPartyCommandChannel::GetPartyCount() { return static_cast<int>(m_partySidList.size()); }

bool CMultiPartyCommandChannel::IsAboutToClose() { return m_partySidList.size() < 2; }

int CMultiPartyCommandChannel::GetMemberCount() {
    L2_GUARD_FUNCTION();

    int memberCount = 0;
    L2_GUARD(m_mpccLock);
    CPartySmartIdManager* const manager = PartySmartIdManager();
    for (const std::uint32_t sid : m_partySidList) {
        if (CParty* const party = manager->GetObjectW(sid)) {
            memberCount += party->m_nMemberCount;
        }
    }
    return memberCount;
}

int CMultiPartyCommandChannel::GetMaxLevelOfAllMember() {
    L2_GUARD_FUNCTION();

    int maximumLevel = 0;
    std::vector<CParty*> parties;
    GetAllParty(parties);
    for (CParty* const party : parties) {
        if (!party) {
            continue;
        }
        std::vector<User*> users;
        party->GetAllMember(users);
        for (User* const user : users) {
            if (maximumLevel < user->Level()) {
                // The current body repeats the virtual query on assignment.
                maximumLevel = user->Level();
            }
        }
    }
    return maximumLevel;
}

int CMultiPartyCommandChannel::GetMinLevelOfAllMember(
    bool checkDistance, int radius, FVector position, bool checkFilter, int filterArgument
) {
    L2_GUARD_FUNCTION();

    int minimumLevel = 120;
    std::vector<CParty*> parties;
    GetAllParty(parties);
    for (CParty* const party : parties) {
        if (!party) {
            continue;
        }
        std::vector<User*> users;
        party->GetAllMember(users);
        for (User* const user : users) {
            const int candidateLevel = user->Level();
            if (candidateLevel >= minimumLevel) {
                continue;
            }
            if (checkFilter && !user->IsInInZone(static_cast<unsigned int>(filterArgument))) {
                continue;
            }
            if (checkDistance) {
                const FVector userPosition = user->GetPos();
                const double distanceSquared = (userPosition.x - position.x) * (userPosition.x - position.x) +
                    (userPosition.y - position.y) * (userPosition.y - position.y) +
                    (userPosition.z - position.z) * (userPosition.z - position.z);
                const double radiusSquared = static_cast<double>(runtime::MultiplyInt32Wrapping(radius, radius));
                if (radiusSquared <= distanceSquared) {
                    continue;
                }
            }
            // The current body repeats the virtual query on assignment.
            minimumLevel = user->Level();
        }
    }
    return minimumLevel;
}

void CMultiPartyCommandChannel::GetAllMemberWhoCanGetExpSp(std::vector<User*>& users, CCreature* creature) {
    L2_GUARD_FUNCTION();

    users.clear();
    std::vector<CParty*> parties;
    GetAllParty(parties);
    for (CParty* const party : parties) {
        if (!party) {
            continue;
        }
        std::vector<User*> partyUsers;
        party->GetAllMember(partyUsers);
        for (User* const user : partyUsers) {
            if (!user) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[CMultiPartyCommandChannel::"
                //     L"GetAllMemberWhoCanGetExpSp] pUser is NULL")
                // L2_LOG_ERROR(Log,
                //     L"[CMultiPartyCommandChannel::"
                //     L"GetAllMemberWhoCanGetExpSp] pUser is NULL")
                L2_LOG_WARN(
                    Log,
                    "Cannot consider a command-channel member for experience and SP: the user is unavailable; candidate "
                    "scanning continues."
                );
                continue;
            }
            if (CanGetExpSp(user, creature, 0xaf0, false)) {
                users.push_back(user);
            }
        }
    }
}

void CMultiPartyCommandChannel::Accept(IMultiPartyVisitor& visitor) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_mpccLock);
    for (const std::uint32_t sid : m_partySidList) {
        visitor.Work(sid);
    }
}

std::vector<std::uint32_t>::iterator CMultiPartyCommandChannel::_Find(CParty* party) {
    L2_GUARD_FUNCTION();
    const std::uint32_t sid = party->GetSmartID().Sid();
    return std::find(m_partySidList.begin(), m_partySidList.end(), sid);
}

bool CMultiPartyCommandChannel::Contains(CParty* party) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_mpccLock);
    return _Find(party) != m_partySidList.end();
}

void CMultiPartyCommandChannel::GetAllParty(std::vector<CParty*>& parties) {
    L2_GUARD_FUNCTION();

    parties.reserve(50);
    parties.resize(0);

    L2_GUARD(m_mpccLock);
    CPartySmartIdManager* const manager = PartySmartIdManager();
    for (const std::uint32_t sid : m_partySidList) {
        if (CParty* const party = manager->GetObjectW(sid)) {
            parties.push_back(party);
        }
    }
}

void CMultiPartyCommandChannel::GetAllMember(std::vector<User*>& users) {
    L2_GUARD_FUNCTION();

    std::vector<CParty*> parties;
    GetAllParty(parties);
    for (CParty* const party : parties) {
        if (!party) {
            continue;
        }
        std::vector<User*> partyUsers;
        party->GetAllMember(partyUsers);
        for (User* const user : partyUsers) {
            users.push_back(user);
        }
    }
}

void CMultiPartyCommandChannel::ShowPartyMembersInfo(User* user, CParty* party) {
    // Current L2Server 0x0084e9b0.
    L2_GUARD_FUNCTION();
    if (user == nullptr || party == nullptr) {
        return;
    }

    std::vector<User*> members;
    party->GetAllMember(members);

    std::array<char, 0x400> packet;
    int packetLength = ::Assemble(packet.data(), static_cast<int>(packet.size()), "d", static_cast<int>(members.size()));
    if (packetLength <= 0) {
        packetLength = 0;
    }

    for (User* const member : members) {
        if (member == nullptr) {
            continue;
        }

        const std::uint32_t memberId = member->m_nId;
        CSharedCreatureData* const memberData = member->m_data;
        const char16_t* const memberNameSource = member->GetName();
        const int memberClass = static_cast<int>(memberData->m_nClass);
        const std::u16string memberName = std::u16string(memberNameSource);
        const int assembled = ::Assemble(
            packet.data() + packetLength, static_cast<int>(packet.size()) - packetLength, "Sdd", memberName.c_str(),
            runtime::Int32FromBits(memberId), memberClass
        );
        if (assembled > 0) {
            packetLength += assembled;
        }
    }

    user->GetSocket()->Send("chb", 0xfe, 0x4c, packetLength, packet.data());
}

void CMultiPartyCommandChannel::CancelTradeAllMember() {
    L2_GUARD_FUNCTION();
    std::vector<CParty*> parties;
    GetAllParty(parties);
    for (CParty* const party : parties) {
        if (party) {
            party->CancelTradeAllMember();
        }
    }
}

CParty* CMultiPartyCommandChannel::GetMasterParty() { return PartySmartIdManager()->GetObjectW(m_data->m_masterPartySid); }

bool CMultiPartyCommandChannel::AddParty(CParty* party) {
    L2_GUARD_FUNCTION();
    if (Contains(party)) {
        return false;
    }

    L2_GUARD(m_mpccLock);
    if (m_partySidList.size() < static_cast<std::size_t>(s_maxPartyCanJoin)) {
        m_partySidList.push_back(party->GetSmartID().Sid());
        _UpdateSharedPartySidList();
        SendCancelPacket(0x1037, static_cast<DivideType::Enum>(2));
    }
    return true;
}

bool CMultiPartyCommandChannel::RemoveParty(CParty* party) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_mpccLock);
    const auto found = _Find(party);
    if (found == m_partySidList.end()) {
        return false;
    }
    m_partySidList.erase(found);
    _UpdateSharedPartySidList();
    SendCancelPacket(0x1037, static_cast<DivideType::Enum>(2));
    return true;
}

User* CMultiPartyCommandChannel::GetMaster() {
    L2_GUARD_FUNCTION();
    CParty* const masterParty = GetMasterParty();
    return masterParty ? masterParty->GetMaster() : nullptr;
}

bool CMultiPartyCommandChannel::IsMaster(int sid) {
    L2_GUARD_FUNCTION();
    User* const master = GetMaster();
    return master && master->GetSmartID().Sid() == static_cast<std::uint32_t>(sid);
}

bool CMultiPartyCommandChannel::IsMember(int sid) {
    L2_GUARD_FUNCTION();
    User* const user = CSmartID(static_cast<std::uint32_t>(sid)).GetUser();
    CMultiPartyCommandChannel* const channel = user->GetMPCC();
    return channel && channel->m_nId == m_nId;
}

void CMultiPartyCommandChannel::DivideAdenaStart() {
    // Current L2Server 0x0084f734.
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::ExchangeI32(&m_divide, 1);

    std::vector<CParty*> parties;
    GetAllParty(parties);
    for (CParty* const party : parties) {
        if (party == nullptr) {
            continue;
        }
        std::vector<User*> users;
        party->GetAllMember(users);
        for (User* const user : users) {
            if (IsMaster(static_cast<int>(user->m_nId))) {
                user->GetSocket()->Send("ch", 0xfe, 0x15b);
            }
            user->SendSystemMessage(SystemMessageId::ADENA_DISTRIBUTION_STARTED);
        }
    }
}

void CMultiPartyCommandChannel::SendCancelPacket(int messageId, DivideType::Enum divideType) {
    L2_GUARD_FUNCTION();

    const std::int32_t previous = runtime::raw_atomic::ExchangeI32(&m_divide, 0);
    if (previous == 0) {
        return;
    }

    User* const master = GetMaster();
    if (!master) {
        return;
    }

    std::vector<CParty*> parties;
    GetAllParty(parties);
    for (CParty* const party : parties) {
        if (!party) {
            continue;
        }
        std::vector<User*> users;
        party->GetAllMember(users);
        for (User* const user : users) {
            if (IsMaster(static_cast<int>(user->m_nId))) {
                master->GetSocket()->Send("chc", 0xfe, 0x15c, static_cast<int>(divideType));
            }
            user->SendSystemMessage(messageId);
        }
    }
}

void CMultiPartyCommandChannel::DivideAdenaCancel(DivideType::Enum divideType) {
    L2_GUARD_FUNCTION();
    SendCancelPacket(0x1037, divideType);
}

void CMultiPartyCommandChannel::DivideAdena(std::int64_t totalAmount) {
    // Current L2Server 0x0084ff60.
    L2_GUARD_FUNCTION();

    const int memberCount = GetMemberCount();
    User* const master = GetMaster();
    if (master == nullptr) {
        return;
    }

    CancelTradeAllMember();
    const std::u16string masterName = std::u16string(master->GetName());
    const std::int64_t amountPerMember = totalAmount / memberCount;
    if (amountPerMember < 1) {
        SendCancelPacket(0x103d, DivideType::DIT_ADENA_LACK);
        return;
    }

    std::vector<CParty*> parties;
    GetAllParty(parties);
    constexpr std::int64_t kMaximumAdena = 999999999999LL;
    for (CParty* const party : parties) {
        if (party == nullptr) {
            continue;
        }
        std::vector<User*> users;
        party->GetAllMember(users);
        for (User* const user : users) {
            if (user->m_nId != master->m_nId &&
                runtime::AddInt64Wrapping(user->m_inv.GetAdenaAmount(), amountPerMember) > kMaximumAdena) {
                SendCancelPacket(0x104b, DivideType::DIT_ADENA_LACK);
                master->GetSocket()->SendSystemMessage_S(
                    SystemMessageId::CANNOT_DISTRIBUTE_ADENA_RECIPIENT_LIMIT_EXCEEDED, user->GetName()
                );
                return;
            }
        }
    }

    runtime::raw_atomic::ExchangeI32(&m_divide, 0);
    for (CParty* const party : parties) {
        if (party == nullptr) {
            continue;
        }
        std::vector<User*> users;
        party->GetAllMember(users);
        for (User* const user : users) {
            if (user->m_nId == master->m_nId) {
                continue;
            }
            if (!master->TradeDivideAdena(user, memberCount, amountPerMember, true)) {
                user->SendSystemMessage(SystemMessageId::ADENA_DISTRIBUTION_NOT_RECEIVED);
                master->GetSocket()->SendSystemMessage_S(
                    SystemMessageId::ADENA_NOT_DISTRIBUTED_TO_CHARACTER, user->GetName()
                );
            }
        }
    }

    master->GetSocket()->Send("chccdQQS", 0xfe, 0x15d, 1, 1, memberCount, amountPerMember, totalAmount, masterName.c_str());
    CLogFormatter formatter;
    formatter.WriteLog1(
        0x233a, master, nullptr, nullptr, nullptr, nullptr, 2, totalAmount, memberCount, amountPerMember, 0, 0, 0
    );
}

void CMultiPartyCommandChannel::_UpdateSharedPartySidList() {
    L2_GUARD_FUNCTION();

    const std::size_t partyCount = m_partySidList.size();
    const std::uint32_t* const partySids = m_partySidList.data();
    for (std::size_t index = 0; index < kSharedPartyCapacity; ++index) {
        m_data->m_partySidList[index] = index < partyCount ? partySids[index] : 0;
    }
    m_data->m_partyCount = static_cast<std::uint32_t>(partyCount);
}
