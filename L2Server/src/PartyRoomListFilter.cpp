#include "PartyRoomListFilter.h"

#include "PartyRoom.h"
#include "User.h"

#include "Shared/src/BaseStructure.h"

#include <cmath>
#include <utility>

CPartyRoomListFilter::CPartyRoomListFilter(
    CSPointer<User> user, int page, LocationFilter locationFilter, LevelFilter levelFilter, bool mpccRoomFilter
) :
    m_user(std::move(user)),
    m_page(page),
    m_locationFilter(locationFilter),
    m_levelFilter(levelFilter),
    m_bMPCCRoomFilter(mpccRoomFilter),
    m_alignmentPadding25{} {}

CPartyRoomListFilter::~CPartyRoomListFilter() = default;

CSPointer<User> CPartyRoomListFilter::GetUser() { return m_user; }

int CPartyRoomListFilter::GetPage() const { return m_page; }

void CPartyRoomListFilter::SetPage(int page) { m_page = page; }

LocationFilter CPartyRoomListFilter::GetLocationFilter() const { return m_locationFilter; }

LevelFilter CPartyRoomListFilter::GetLevelFilter() const { return m_levelFilter; }

bool CPartyRoomListFilter::IsInLowerLimit(int index) const {
    const int page = m_page < 1 ? 1 : m_page;
    return page * MAX_LIST_COUNT - (MAX_LIST_COUNT - 1) <= index;
}

bool CPartyRoomListFilter::IsInUpperLimit(int index) const {
    const int page = m_page < 1 ? 1 : m_page;
    return index <= page * MAX_LIST_COUNT;
}

bool CPartyRoomListFilter::IsInPage(int index) const { return IsInLowerLimit(index) && IsInUpperLimit(index); }

bool CPartyRoomListFilter::FilterByLevel(CSPointer<CPartyRoom> room) {
    L2_GUARD_FUNCTION();
    if (m_levelFilter == NEAR_LEVEL) {
        return room && room->GetPartyRoomInfo().IsBetweenLevelLimit(GetUser());
    }
    return m_levelFilter == ALL_LEVEL;
}

bool CPartyRoomListFilter::FilterByLocation(CSPointer<CPartyRoom> room) {
    L2_GUARD_FUNCTION();
    if (!room) {
        return false;
    }

    if (m_locationFilter == NEAR_AREA) {
        const CSPointer<User> master = room->GetPartyRoomInfo().GetMasterUser();
        const CSPointer<User> user = GetUser();
        if (!master || !user) {
            return false;
        }
        const FVector masterPosition = master->GetPos();
        const FVector userPosition = user->GetPos();
        return std::sqrt(masterPosition.DistCube(userPosition)) <= 16384.0;
    }

    if (m_locationFilter == ALL_AREA) {
        return true;
    }

    const bool explicitArea = (TALKING_ISLAND <= m_locationFilter && m_locationFilter <= DMZ) ||
        (SCHUTTGART <= m_locationFilter && m_locationFilter <= GODARD) || m_locationFilter == ETC_AREA;
    return explicitArea && room->GetPartyRoomInfo().GetLocation() == static_cast<int>(m_locationFilter);
}

bool CPartyRoomListFilter::IsFilteredCondition(CSPointer<CPartyRoom> room) {
    L2_GUARD_FUNCTION();
    return room && FilterByLevel(room) && FilterByLocation(std::move(room));
}

bool CPartyRoomListFilter::IsMPCCRoomFilter() const { return m_bMPCCRoomFilter; }
