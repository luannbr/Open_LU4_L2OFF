#pragma once

#include "Shared/src/StandardContainers.h"
#include "Shared/src/IOCs.h"

#include <cstddef>
#include <cstdint>

using EventDataContents = std::map<int, std::u16string>;

// Shipping owner and source names are kept by current guard literals:
// CacheD/EventDataHelper.{h,cpp}. The target prefix is CIOCriticalSection,
// followed by time32, two xallocator wstrings and the 0x20-byte ordered map.
class CEventDataHelper : public CIOCriticalSection {
public:
    CEventDataHelper();
    ~CEventDataHelper();

    void Load();
    bool Save(int index, int total, std::u16string eventName, std::u16string comment, std::u16string contents);

private:

    void SaveComplete();
    void Initialize(const std::u16string& eventName, const std::u16string& comment);

    std::int32_t m_startTime = 0; // +0x28
    std::u16string m_eventName; // +0x30
    std::u16string m_comment; // +0x58
    EventDataContents m_contents; // +0x80
};



inline CEventDataHelper& CurrentEventDataHelper() {
    // All current call sites inline access to the same DAT_007f2950 object,
    // guarded by one lazy-static byte and destroyed at process exit. An
    // external inline function gives every translation unit that same single
    // function-local object without introducing a second event-data owner.
    static CEventDataHelper eventDataHelper;
    return eventDataHelper;
}
