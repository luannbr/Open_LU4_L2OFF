#include "ReservationAnnounceTimer.h"
#include "Shared/src/TextRuntime.h"

#include "DB.h"
#include "UserBroadcaster.h"

#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/Utils.h"
#include "Shared/src/proto/DBPacket.h"

#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>

namespace {



std::int32_t CurrentTime64Low32() noexcept {
    const std::int64_t current = static_cast<std::int64_t>(std::time(nullptr));
    return runtime::Int32FromBits(static_cast<std::uint32_t>(static_cast<std::uint64_t>(current)));
}


} // namespace

CReservationAnnounceTimer::CReservationAnnounceTimer() {}

CReservationAnnounceTimer::~CReservationAnnounceTimer() = default;

void CReservationAnnounceTimer::Init() {
    // Current L2Server 0x00a2e5e0.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    AddTimer(60000, 0);
    db.LoadReservationAnnounce();
}

bool CReservationAnnounceTimer::_CheckTime(const ReservationMsg& message, const std::int32_t now) {
    // Current L2Server 0x00a2e6bc. Both comparisons are signed and the
    // period product/addition keep the target's 32-bit wrap.
    L2_GUARD_FUNCTION();
    std::int32_t target = message.startDate;
    if (message.sentDate != 0) {
        target = runtime::AddInt32Wrapping(message.sentDate, runtime::MultiplyInt32Wrapping(message.period, 0xe10));
        if (message.sentDate < now && now < target) {
            return false;
        }
    }
    return target < now;
}

void CReservationAnnounceTimer::_BroadcastAnnounce(ReservationMsg message) {
    // Current L2Server 0x00a2e78c.
    L2_GUARD_FUNCTION();
    if (message.location == 0) {
        for (const std::u16string& text : message.Msgs) {
            const std::wstring hostText = target_utf16::ToHostCodeUnits(text.c_str());
            CUserBroadcaster::inst().BroadcastToAllUserScreenAnnounceEX(hostText.c_str());
            CLogFormatter formatter;
            formatter.WriteLog(
                0x221, 0, 0, 0, 0, 0, 0, 0, text.c_str(), nullptr, nullptr, message.location, message.announceNumber,
                message.startDate, message.endDate, message.period, 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr, 0
            );
        }
    } else if (message.location == 1) {
        for (const std::u16string& text : message.Msgs) {
            CUserBroadcaster::inst().BroadcastToAllUserAnnounce(text.c_str());
            CLogFormatter formatter;
            formatter.WriteLog(
                0x221, 0, 0, 0, 0, 0, 0, 0, text.c_str(), nullptr, nullptr, message.location, message.announceNumber,
                message.startDate, message.endDate, message.period, 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr, 0
            );
        }
    }
}

CReservationAnnounceTimer::ReservationMsg CReservationAnnounceTimer::_ConvertToReservationMsg(
    const protocol::PkReservationAnncounceData& data
) {
    // Current L2Server 0x00a2eb14.
    L2_GUARD_FUNCTION();
    ReservationMsg result;
    result.announceNumber = data.announceId;
    result.Msgs = data.Msgs;
    result.startDate = data.StartDate;
    result.endDate = data.endDate;
    result.period = data.period;
    result.location = data.location;
    result.sentDate = SetLastSentDate(result.startDate, CurrentTime64Low32(), data.period);
    return result;
}

void CReservationAnnounceTimer::_DelReservationAnnounce(const std::int32_t now) {
    // Current L2Server 0x00a2f08c.
    L2_GUARD_FUNCTION();
    for (auto iterator = resMsg.begin(); iterator != resMsg.end();) {
        if (iterator->endDate < now) {
            db.DelReservationAnnounce(iterator->announceNumber);
            iterator = resMsg.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void CReservationAnnounceTimer::_Update() {
    // Current L2Server 0x00a2f160.
    L2_GUARD_FUNCTION();
    if (resMsg.empty()) {
        return;
    }

    const std::int32_t now = CurrentTime64Low32();
    for (ReservationMsg& message : resMsg) {
        if (_CheckTime(message, now)) {
            _BroadcastAnnounce(message);
            message.sentDate = SetLastSentDate(message.startDate, now, message.period);
        }
    }
    _DelReservationAnnounce(now);
}

void CReservationAnnounceTimer::TimerExpired(const int id) {
    // Current L2Server 0x00a2f330.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    if (id == 0) {
        GuardAddTimer addTimer(this, 60000, 0);
        _Update();
    }
}

void CReservationAnnounceTimer::LoadAnnounce(const protocol::RP_EX_LOAD_RESERVATION_ANNOUNCE& reply) {
    // Current L2Server 0x00a2f430.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    resMsg.clear();
    for (const protocol::PkReservationAnncounceData& data : reply.data) {
        const ReservationMsg converted = _ConvertToReservationMsg(data);
        resMsg.push_back(converted);
    }
}

std::int32_t CReservationAnnounceTimer::SetLastSentDate(
    const std::int32_t startDate, const std::int32_t now, const int period
) {
    // The original private helper is inlined into current 0x00a2eb14 and
    // 0x00a2f160. A zero/non-progressing period keeps the
    // target's non-terminating loop when startDate is already in the past.
    std::int32_t sentDate = 0;
    if (startDate < now) {
        sentDate = startDate;
        const std::int32_t periodSeconds = runtime::MultiplyInt32Wrapping(period, 0xe10);
        do {
            sentDate = runtime::AddInt32Wrapping(sentDate, periodSeconds);
        } while (sentDate < now);
        sentDate = runtime::SubtractInt32Wrapping(sentDate, periodSeconds);
    }
    return sentDate;
}
