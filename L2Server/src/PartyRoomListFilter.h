#pragma once

#include "Shared/src/ThreadSafeLibrary.h"

#include <cstddef>
#include <cstdint>

class CPartyRoom;
class User;

enum LocationFilter : std::int32_t {
    NEAR_AREA = -2,
    ALL_AREA = -1,
    TALKING_ISLAND = 1,
    GLUDIN_GLUDIO = 2,
    DARK_ELF = 3,
    ELF = 4,
    DION_FLORAN = 5,
    GIRAN = 6,
    DMZ = 7,
    SCHUTTGART = 9,
    OREN = 10,
    HUNTER = 11,
    HEINESS = 12,
    ADEN = 13,
    RUNE = 14,
    GODARD = 15,
    ETC_AREA = 100,
};

enum LevelFilter : std::int32_t {
    NEAR_LEVEL = 0,
    ALL_LEVEL = 1,
};

class CPartyRoomListFilter {
public:
    static constexpr int MAX_LIST_COUNT = 15;

    CPartyRoomListFilter(
        CSPointer<User> user, int page, LocationFilter locationFilter, LevelFilter levelFilter, bool mpccRoomFilter
    );
    virtual ~CPartyRoomListFilter();

    CSPointer<User> GetUser();
    int GetPage() const;
    void SetPage(int page);
    LocationFilter GetLocationFilter() const;
    LevelFilter GetLevelFilter() const;
    bool IsInPage(int index) const;
    bool IsFilteredCondition(CSPointer<CPartyRoom> room);
    bool FilterByLocation(CSPointer<CPartyRoom> room);
    bool FilterByLevel(CSPointer<CPartyRoom> room);
    bool IsMPCCRoomFilter() const;

private:
    bool IsInLowerLimit(int index) const;
    bool IsInUpperLimit(int index) const;

    CSPointer<User> m_user;
    int m_page;
    LocationFilter m_locationFilter;
    LevelFilter m_levelFilter;
    bool m_bMPCCRoomFilter;
    [[maybe_unused]] std::byte m_alignmentPadding25[3];
};
