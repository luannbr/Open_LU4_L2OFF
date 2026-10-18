#pragma once

#include "Shared/src/IOC.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

namespace protocol {

struct PkReservationAnncounceData;
class RP_EX_LOAD_RESERVATION_ANNOUNCE;

} // namespace protocol


class CReservationAnnounceTimer final : public CIOObject, public Singleton<CReservationAnnounceTimer> {
public:
    CReservationAnnounceTimer();
    ~CReservationAnnounceTimer() override;

    void Init();
    void LoadAnnounce(const protocol::RP_EX_LOAD_RESERVATION_ANNOUNCE& reply);

private:
    struct ReservationMsg {
        int announceNumber; // +0x00
        std::int32_t startDate; // +0x04
        std::int32_t endDate; // +0x08
        int period; // +0x0c
        int location; // +0x10
        std::list<std::u16string> Msgs; // +0x18
        std::int32_t sentDate; // +0x30
    };

    void TimerExpired(int id) override;
    void _Update();
    ReservationMsg _ConvertToReservationMsg(const protocol::PkReservationAnncounceData& data);
    void _DelReservationAnnounce(std::int32_t now);
    bool _CheckTime(const ReservationMsg& message, std::int32_t now);
    void _BroadcastAnnounce(ReservationMsg message);
    std::int32_t SetLastSentDate(std::int32_t startDate, std::int32_t now, int period);

    std::vector<ReservationMsg> resMsg; // +0x20
    CYieldLock m_lock; // +0x40
};
