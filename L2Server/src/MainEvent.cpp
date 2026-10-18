#include "MainEvent.h"
#include "CConfig.h"
#include "DB.h"
#include "Object.h"
#include "Party.h"
#include "Protocol/src/SystemMessageIds.h"
#include "SSQ.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/DXPacket.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/TextRuntime.h"

#include <array>
#include <cstring>
#include <new>
#include <string>

namespace {

constexpr std::array<int, 5> kTimeAttackRoomPoint = {
    60,
    70,
    100,
    120,
    150,
};

constexpr std::array<double, 5> kTimeAttackFeeDistribution = {
    3.0,
    5.0,
    10.0,
    15.0,
    20.0,
};

} // namespace

TimeAttackInfo::TimeAttackInfo(int roomNumber, int recordType) {
    L2_GUARD_FUNCTION();

    mSharedInfo = ::SharedTimeAttackInfo::GetInfo(roomNumber, recordType);

    mSharedInfo->mRoomNo = roomNumber;
    mSharedInfo->mRecordType = recordType;
    mSharedInfo->mSSQRound = 0;
    mSharedInfo->mSSQPart = 0;
    mSharedInfo->mPoint = 0;
    mSharedInfo->mRecordTime = 0;
    mSharedInfo->mElapsedTime = 0;
    mSharedInfo->mMemberCount = 0;

    // Current 0x008192a4 does not clear mFee at +0x28.
    std::memset(mSharedInfo->mMemberNameList, 0, sizeof(mSharedInfo->mMemberNameList));
    std::memset(mSharedInfo->mMemberDBIDList, 0, sizeof(mSharedInfo->mMemberDBIDList));
    std::memset(mSharedInfo->mMemberRewardFlags, 0, sizeof(mSharedInfo->mMemberRewardFlags));
}

const unsigned char* TimeAttackInfo::Set(const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    packet = ::Disassemble(
        packet, "ddddQdddQ", &mSharedInfo->mRoomNo, &mSharedInfo->mRecordType, &mSharedInfo->mSSQRound,
        &mSharedInfo->mSSQPart, &mSharedInfo->mPoint, &mSharedInfo->mRecordTime, &mSharedInfo->mElapsedTime,
        &mSharedInfo->mMemberCount, &mSharedInfo->mFee
    );

    for (int index = 0; index < mSharedInfo->mMemberCount; ++index) {
        packet = ::Disassemble(packet, "S", 0x32, mSharedInfo->mMemberNameList[index]);
        packet = ::Disassemble(packet, "dd", &mSharedInfo->mMemberDBIDList[index], &mSharedInfo->mMemberRewardFlags[index]);
    }

    return packet;
}

TimeAttackBoard::TimeAttackBoard() {
    L2_GUARD_FUNCTION();

    for (int roomNumber = 1; roomNumber < 6; ++roomNumber) {
        for (int recordType = 0; recordType < 3; ++recordType) {
            TimeAttackInfo* const info = new (std::nothrow) TimeAttackInfo(roomNumber, recordType);
            mRecordMap[RecordKey(roomNumber, recordType)] = info;
        }
    }
}

TimeAttackBoard::~TimeAttackBoard() {
    L2_GUARD_FUNCTION();

    for (auto& record : mRecordMap) {
        delete record.second;
    }
    mRecordMap.clear();
}

TimeAttackBoard& TimeAttackBoard::inst() {
    static TimeAttackBoard instance;
    return instance;
}

TimeAttackInfo* TimeAttackBoard::GetInfo(const int roomNumber, const int recordType) const {
    L2_GUARD_FUNCTION();

    const auto position = mRecordMap.find(RecordKey(roomNumber, recordType));
    return position == mRecordMap.end() ? nullptr : position->second;
}

void TimeAttackBoard::OnStart() {
    // Current L2Server 0x00819538.
    L2_GUARD_FUNCTION();
    db.LoadTimeAttackInfo();
}

std::int64_t TimeAttackBoard::GetFee(const int roomNumber) {
    // Current L2Server 0x00819710.
    L2_GUARD_FUNCTION();
    SharedTimeAttackInfo* const info = SharedTimeAttackInfo::GetInfo(roomNumber, TimeAttackRecordType::TOP_OF_ROOM);
    return info != nullptr ? info->mFee : 0;
}

bool TimeAttackBoard::AddFee(const int roomNumber, const std::int64_t fee) {
    // Current L2Server 0x008199d8.
    L2_GUARD_FUNCTION();
    TimeAttackInfo* const info = GetInfo(roomNumber, TimeAttackRecordType::TOP_OF_ROOM);
    if (info == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] TimeAttackBoard::AddFee() : Invalid room, room no=[%d], room type=[%d]",
        //     L"MainEvent.cpp",
        //     0xeb,
        //     roomNumber,
        //     TimeAttackRecordType::TOP_OF_ROOM)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] TimeAttackBoard::AddFee() : Invalid room, room no=[%d], room type=[%d]",
        //     L"MainEvent.cpp",
        //     0xeb,
        //     roomNumber,
        //     TimeAttackRecordType::TOP_OF_ROOM)
        L2_LOG_ERROR(
            Log,
            "Cannot add time-attack fees for room %d: its top-of-room record is unavailable; no fee update is submitted.",
            roomNumber
        );
        return false;
    }

    constexpr std::uint64_t kMaximumFee = UINT64_C(999999999999);
    if (static_cast<std::uint64_t>(fee) - UINT64_C(1) >= kMaximumFee) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] TimeAttackBoard::AddFee() : Too large point=[%I64d]",
        //     L"MainEvent.cpp",
        //     0xfc,
        //     fee)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] TimeAttackBoard::AddFee() : Too large point=[%I64d]",
        //     L"MainEvent.cpp",
        //     0xfc,
        //     fee)
        L2_LOG_ERROR(
            Log,
            "Cannot add time-attack fees for room %d: amount %lld is outside 1..999999999999; no fee update is submitted.",
            roomNumber, static_cast<long long>(fee)
        );
        return false;
    }

    const std::int64_t result = [&] {
        L2_WRITE_GUARD(*info);
        const std::int64_t result = runtime::AddInt64Wrapping(info->mSharedInfo->mFee, fee);
        return result;
    }();
    if (static_cast<std::uint64_t>(result) - UINT64_C(1) >= kMaximumFee) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] TimeAttackBoard::AddFee() : Too large result=[%I64d], delta=[%I64d]]",
        //     L"MainEvent.cpp",
        //     0x108,
        //     result,
        //     fee)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] TimeAttackBoard::AddFee() : Too large result=[%I64d], delta=[%I64d]]",
        //     L"MainEvent.cpp",
        //     0x108,
        //     result,
        //     fee)
        L2_LOG_ERROR(
            Log,
            "Cannot add time-attack fees for room %d: calculated total %lld with added amount %lld is outside "
            "1..999999999999; no fee update is submitted.",
            roomNumber, static_cast<long long>(result), static_cast<long long>(fee)
        );
        return false;
    }

    DxAddTimeAttackFee packet(roomNumber, fee);
    db.RequestTPacketProcess(g_nWorldId, 0x13, static_cast<int>(sizeof(packet)), packet);
    return true;
}

bool TimeAttackBoard::GiveReward(User* const user, const int roomNumber, ItemManip& request) {
    // Current L2Server 0x00819bbc, original owner MainEvent.cpp.
    L2_GUARD_FUNCTION();

    if (user == nullptr) {
        return false;
    }

    const SSQPart::type partType = user->GetSSQPart();
    if (partType == SSQPart::NONE) {
        return false;
    }

    TimeAttackInfo* const info = GetInfo(roomNumber, static_cast<int>(partType));
    if (info == nullptr) {
        return false;
    }

    {
        L2_WRITE_GUARD(*info);
        if (info->mSharedInfo->IsMember(runtime::Int32FromBits(user->GetDBID())) == 0) {
            return false;
        }

        if (info->mSharedInfo->GetRewardFlag(runtime::Int32FromBits(user->GetDBID())) != 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Already rewarded user, dbid=[%d]",
            //     L"MainEvent.cpp",
            //     0x135,
            //     runtime::Int32FromBits(
            //         user->GetDBID()))
            // Noise: A nonzero reward flag rejects repeated or in-flight claims before item manipulation. Ordinary claim
            // validation needs no operator message. L2_LOG_ERROR(Log,
            //     L"[%s][%d] Already rewarded user, dbid=[%d]",
            //     L"MainEvent.cpp",
            //     0x135,
            //     runtime::Int32FromBits(
            //         user->GetDBID()))
            return false;
        }

        info->mSharedInfo->SetRewardFlag(runtime::Int32FromBits(user->GetDBID()), 1);
    }

    const std::int64_t currentAdena = user->m_inv.GetItemAmount(0x15c7, 0, true);
    if (!user->HandleItemManipulation(1, &request, nullptr, 0, nullptr, false, ItemGetReason::Etc)) {
        L2_WRITE_GUARD(*info);
        info->mSharedInfo->SetRewardFlag(runtime::Int32FromBits(user->GetDBID()), 0);
        return false;
    }

    if (request.mClassId == 0x15c7) {
        // Current 0x00819e7a evaluates three independent GetPos calls in
        // z/y/x order before publishing x/y/z to the audit record.
        const int z = runtime::Win64CvttDoubleToInt32(user->GetPos().z);
        const int y = runtime::Win64CvttDoubleToInt32(user->GetPos().y);
        const int x = runtime::Win64CvttDoubleToInt32(user->GetPos().x);
        const std::wstring accountName = ::Utf16ToWide(std::u16string_view(user->m_sAccountName.data()));

        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,,%I64d,%I64d,%I64d,,"
        //     L"%s,%s,,,",
        //     static_cast<std::int64_t>(0x13d),
        //     static_cast<std::int64_t>(
        //         user->GetDBID()),
        //     static_cast<std::int64_t>(
        //         user->GetAccountID()),
        //     static_cast<std::int64_t>(x),
        //     static_cast<std::int64_t>(y),
        //     static_cast<std::int64_t>(z),
        //     static_cast<std::int64_t>(
        //         user->m_data->m_Race),
        //     static_cast<std::int64_t>(
        //         user->m_data->m_Sex),
        //     static_cast<std::int64_t>(
        //         user->m_data->m_nClass),
        //     static_cast<std::int64_t>(
        //         user->m_data->m_nLevel),
        //     static_cast<std::int64_t>(partType),
        //     static_cast<std::int64_t>(roomNumber),
        //     request.mCount,
        //     runtime::AddInt64Wrapping(
        //         currentAdena,
        //         request.mCount),
        //     user->GetName(),
        //     accountName.c_str())
        Log.AddT(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
            u"%I64d,%I64d,%I64d,%I64d,%I64d,,%I64d,%I64d,%I64d,,"
            u"%s,%s,,,",
            static_cast<std::int64_t>(0x13d), static_cast<std::int64_t>(user->GetDBID()),
            static_cast<std::int64_t>(user->GetAccountID()), static_cast<std::int64_t>(x), static_cast<std::int64_t>(y),
            static_cast<std::int64_t>(z), static_cast<std::int64_t>(user->m_data->m_Race),
            static_cast<std::int64_t>(user->m_data->m_Sex), static_cast<std::int64_t>(user->m_data->m_nClass),
            static_cast<std::int64_t>(user->m_data->m_nLevel), static_cast<std::int64_t>(partType),
            static_cast<std::int64_t>(roomNumber), request.mCount, runtime::AddInt64Wrapping(currentAdena, request.mCount),
            user->GetName(), accountName.c_str()
        );
    }

    DxSetTimeAttackRewardFlag packet(runtime::Int32FromBits(user->GetDBID()), roomNumber, static_cast<int>(partType), 1);
    db.RequestTPacketProcess(user->m_data->m_nWorldId, 0x14, static_cast<int>(sizeof(packet)), packet);
    return true;
}

bool TimeAttackBoard::AddRecord(
    const int roomNumber,
    const SSQPart::type partType,
    const int partyServerId,
    const std::int64_t point,
    const int recordTime,
    const int elapsedTime
) {
    // Current L2Server 0x0081a7b4, original owner MainEvent.cpp.
    L2_GUARD_FUNCTION();

    if (static_cast<unsigned int>(partType) - static_cast<unsigned int>(SSQPart::TWILIGHT) >= 2U) {
        return false;
    }

    if (GetInfo(roomNumber, static_cast<int>(partType)) == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] TimeAttackBoard::AddRecord() : Invalid room, room no=[%d], room type=[%d]",
        //     L"MainEvent.cpp",
        //     0x92,
        //     roomNumber,
        //     static_cast<int>(partType))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] TimeAttackBoard::AddRecord() : Invalid room, room no=[%d], room type=[%d]",
        //     L"MainEvent.cpp",
        //     0x92,
        //     roomNumber,
        //     static_cast<int>(partType))
        L2_LOG_ERROR(
            Log, "Cannot add a time-attack record for room %d, SSQ faction %d: the room record is unavailable.", roomNumber,
            static_cast<int>(partType)
        );
        return false;
    }

    const SSQStatus::type currentStatus = SSQSystem::inst().GetCurrentStatus();
    if (currentStatus != SSQStatus::EVENT) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] TimeAttackBoard::AddRecord() : Current status is not EVENT, status=[%d]",
        //     L"MainEvent.cpp",
        //     0x9b,
        //     static_cast<int>(currentStatus))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] TimeAttackBoard::AddRecord() : Current status is not EVENT, status=[%d]",
        //     L"MainEvent.cpp",
        //     0x9b,
        //     static_cast<int>(currentStatus))
        L2_LOG_WARN(
            Log,
            "Cannot add a time-attack record for room %d, SSQ faction %d: current SSQ phase %d is not EVENT; no record is "
            "submitted.",
            roomNumber, static_cast<int>(partType), static_cast<int>(currentStatus)
        );
        return false;
    }

    constexpr std::uint64_t maximumPoint = UINT64_C(999999999999);
    if (static_cast<std::uint64_t>(point) - UINT64_C(1) >= maximumPoint) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] TimeAttackBoard::AddRecord() : Too large point=[%d]",
        //     L"MainEvent.cpp",
        //     0xa1,
        //     runtime::Int32FromBits(
        //         static_cast<std::uint32_t>(point)))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] TimeAttackBoard::AddRecord() : Too large point=[%d]",
        //     L"MainEvent.cpp",
        //     0xa1,
        //     runtime::Int32FromBits(
        //         static_cast<std::uint32_t>(point)))
        L2_LOG_ERROR(
            Log, "Cannot add a time-attack record for room %d, SSQ faction %d: score %lld is outside 1..999999999999.",
            roomNumber, static_cast<int>(partType), static_cast<long long>(point)
        );
        return false;
    }

    const std::int64_t result = [&] {
        L2_READ_GUARD(SSQSystem::inst());
        const std::int64_t collectedPoint = SSQSystem::inst().GetCollectedPoint(partType);
        const std::int64_t mainEventPoint = SSQSystem::inst().GetMainEventPoint(partType);
        const std::int64_t result =
            runtime::AddInt64Wrapping(runtime::AddInt64Wrapping(mainEventPoint, point), collectedPoint);
        return result;
    }();

    if (static_cast<std::uint64_t>(result) - UINT64_C(1) >= maximumPoint) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] TimeAttackBoard::AddRecord() : Too large result=[%d], delta=[%d]",
        //     L"MainEvent.cpp",
        //     0xb0,
        //     runtime::Int32FromBits(
        //         static_cast<std::uint32_t>(result)),
        //     runtime::Int32FromBits(
        //         static_cast<std::uint32_t>(point)))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] TimeAttackBoard::AddRecord() : Too large result=[%d], delta=[%d]",
        //     L"MainEvent.cpp",
        //     0xb0,
        //     runtime::Int32FromBits(
        //         static_cast<std::uint32_t>(result)),
        //     runtime::Int32FromBits(
        //         static_cast<std::uint32_t>(point)))
        L2_LOG_ERROR(
            Log,
            "Cannot add a time-attack record for room %d, SSQ faction %d: calculated total %lld with added score %lld is "
            "outside 1..999999999999.",
            roomNumber, static_cast<int>(partType), static_cast<long long>(result), static_cast<long long>(point)
        );
        return false;
    }

    CParty* const party = g_PartySmartIdManager.GetObjectW(static_cast<std::uint32_t>(partyServerId));
    if (party == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] TimeAttackBoard::AddRecord() : Invalid party, part sid[%d]",
        //     L"MainEvent.cpp",
        //     0xb8,
        //     partyServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] TimeAttackBoard::AddRecord() : Invalid party, part sid[%d]",
        //     L"MainEvent.cpp",
        //     0xb8,
        //     partyServerId)
        L2_LOG_WARN(
            Log,
            "Cannot add a time-attack record for room %d, SSQ faction %d: party SID %u is unavailable; no record is "
            "submitted.",
            roomNumber, static_cast<int>(partType), static_cast<unsigned int>(partyServerId)
        );
        return false;
    }

    std::vector<std::uint32_t> memberIds;
    party->GetAllMemberId(memberIds);

    DxAddTimeAttackRecord packet(roomNumber, partType, point, recordTime, elapsedTime);
    for (std::size_t index = 0; index < memberIds.size(); ++index) {
        CObject* const object = CObject::GetObjectW(runtime::CheckedVectorAt(memberIds, index));
        User* member = nullptr;
        if (object != nullptr && object->IsPC()) {
            member = object->CastUser();
        }
        if (member == nullptr) {
            continue;
        }
        if (member->GetSSQPart() != partType) {
            member->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_ADD_RECORD_NOT_MEMBER_OF_RELATED_GROUP);
            return false;
        }

        const unsigned int memberDatabaseId = member->GetDBID();
        const char16_t* const memberName = member->GetName();
        packet.AddMember(memberName, runtime::Int32FromBits(memberDatabaseId));
    }

    if (packet.GetMemberCount() > 0) {
        db.RequestTPacketProcess(g_nWorldId, 0xe, static_cast<int>(sizeof(packet)), packet);
        return true;
    }
    return false;
}

std::int64_t TimeAttackBoard::GetTotalPoint(const SSQPart::type partType) {
    L2_GUARD_FUNCTION();

    std::int64_t totalPoint = 0;
    for (int roomNumber = 1; roomNumber < 6; ++roomNumber) {
        TimeAttackInfo* const current = GetInfo(roomNumber, partType);

        int opponentType = TimeAttackRecordType::TOP_OF_ROOM;
        if (partType == SSQPart::TWILIGHT) {
            opponentType = TimeAttackRecordType::TOP_OF_DAWN;
        } else if (partType == SSQPart::DAWN) {
            opponentType = TimeAttackRecordType::TOP_OF_TWILIGHT;
        }

        TimeAttackInfo* const opponent = GetInfo(roomNumber, opponentType);
        if (current != nullptr && opponent != nullptr && opponent->mSharedInfo->mPoint < current->mSharedInfo->mPoint) {
            totalPoint += kTimeAttackRoomPoint[static_cast<std::size_t>(roomNumber - 1)];
        }
    }
    return totalPoint;
}

int TimeAttackBoard::MakeTimeAttackInfo(char* const output, const int outputSize) {
    // Current L2Server 0x0081a118, original owner MainEvent.cpp.
    L2_GUARD_FUNCTION();
    int length = ::Assemble(output, outputSize, "cc", 0, 5);
    if (length < 1) {
        return 0;
    }

    for (int roomNumber = 1; roomNumber < 6; ++roomNumber) {
        length = runtime::AddInt32Wrapping(
            length,
            ::Assemble(
                output + length, outputSize - length, "cd", roomNumber,
                kTimeAttackRoomPoint[static_cast<std::size_t>(roomNumber - 1)]
            )
        );

        TimeAttackInfo* const twilight = GetInfo(roomNumber, TOP_OF_TWILIGHT);
        TimeAttackInfo* const dawn = GetInfo(roomNumber, TOP_OF_DAWN);
        if (twilight == nullptr || dawn == nullptr) {
            return 0;
        }

        {
            L2_READ_GUARD(*twilight);
            const int twilightCount = twilight->mSharedInfo->mMemberCount;
            length = runtime::AddInt32Wrapping(
                length, ::Assemble(output + length, outputSize - length, "Qc", twilight->mSharedInfo->mPoint, twilightCount)
            );
            for (int index = 0; index < twilightCount; ++index) {
                const int safeIndex = index < 0 || index > 8 ? 0 : index;
                length = runtime::AddInt32Wrapping(
                    length,
                    ::Assemble(
                        output + length, outputSize - length, "S", twilight->mSharedInfo->mMemberNameList[safeIndex],
                        twilightCount
                    )
                );
            }
        }

        {
            L2_READ_GUARD(*dawn);
            const int dawnCount = dawn->mSharedInfo->mMemberCount;
            length = runtime::AddInt32Wrapping(
                length, ::Assemble(output + length, outputSize - length, "Qc", dawn->mSharedInfo->mPoint, dawnCount)
            );
            for (int index = 0; index < dawnCount; ++index) {
                const int safeIndex = index < 0 || index > 8 ? 0 : index;
                length = runtime::AddInt32Wrapping(
                    length,
                    ::Assemble(
                        output + length, outputSize - length, "S", dawn->mSharedInfo->mMemberNameList[safeIndex], dawnCount
                    )
                );
            }
        }
    }
    return length;
}

void TimeAttackBoard::DistributeFee() {
    // Current L2Server 0x0081a3d0.
    L2_GUARD_FUNCTION();
    const SSQPart::type winner = SSQSystem::inst().GetWinner();
    int opponent = TimeAttackRecordType::TOP_OF_ROOM;
    if (winner == SSQPart::TWILIGHT) {
        opponent = TimeAttackRecordType::TOP_OF_DAWN;
    } else if (winner == SSQPart::DAWN) {
        opponent = TimeAttackRecordType::TOP_OF_TWILIGHT;
    } else if (winner == SSQPart::NONE) {
        return;
    }

    double totalWeight = 0.0;
    double totalFee = 0.0;
    for (int roomNumber = 1; roomNumber < 6; ++roomNumber) {
        TimeAttackInfo* const winnerInfo = GetInfo(roomNumber, static_cast<int>(winner));
        TimeAttackInfo* const opponentInfo = GetInfo(roomNumber, opponent);
        if (winnerInfo == nullptr || opponentInfo == nullptr) {
            continue;
        }
        if (winnerInfo->mSharedInfo->mPoint > opponentInfo->mSharedInfo->mPoint) {
            totalWeight += kTimeAttackFeeDistribution[static_cast<std::size_t>(roomNumber - 1)];
        } else {
            totalFee += static_cast<double>(GetFee(roomNumber));
        }
    }

    if (totalWeight == 0.0 || totalFee == 0.0) {
        return;
    }
    for (int roomNumber = 1; roomNumber < 6; ++roomNumber) {
        TimeAttackInfo* const winnerInfo = GetInfo(roomNumber, static_cast<int>(winner));
        TimeAttackInfo* const opponentInfo = GetInfo(roomNumber, opponent);
        if (winnerInfo == nullptr || opponentInfo == nullptr) {
            continue;
        }
        if (winnerInfo->mSharedInfo->mPoint > opponentInfo->mSharedInfo->mPoint) {
            AddFee(
                roomNumber,
                static_cast<std::int64_t>(
                    totalFee * kTimeAttackFeeDistribution[static_cast<std::size_t>(roomNumber - 1)] / totalWeight
                )
            );
        } else {
            DxAddTimeAttackFee packet(roomNumber, 0);
            db.RequestTPacketProcess(g_nWorldId, 0x13, static_cast<int>(sizeof(packet)), packet);
        }
    }
}
