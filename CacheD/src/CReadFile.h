#pragma once

#include "CReadItemData.h"
#include "Cached.h"
#include "Shared/src/CriticalSectionStorage.h"
#include "Shared/src/ItemConsumeType.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>


// Current owner and spelling are confirmed by Cached.exe guard literals. The
// complete physical layout below is fixed by constructor,
// lookup, insertion, traversal and destructor instructions in current
// Cached.exe 0x00524978..0x005282d2.
class CReadFile {
public:
    CReadFile();
    ~CReadFile();

    CReadFile(const CReadFile&) = delete;
    CReadFile& operator=(const CReadFile&) = delete;

    // Current overloads at 0x005256a0 and 0x00526efc.
    bool ParseItemData(std::wistream& input);
    bool ParseItemData();

    // Current overloads at 0x00525e00 and 0x00527020.
    bool ParseExpData(std::wistream& input);
    bool ParseExpData();

    // C1 keeps the original names of the first two anonymous current
    // bodies. Current 0x00524978/0x005249d0 fixes the widened price value.
    const CReadItemData* FindItem(int itemType) const;
    std::int64_t GetDefaultPrice(int itemType) const;

    // Current CacheVersionPacket 0x004607a0 reads the low dword of the
    // shipping xmap size at g_readFile+0x38. The original inline identifier
    // does not survive, so this semantic name exposes that exact production
    // value through std::map::size(), independently of the host map layout.
    int GetItemCount() const noexcept { return static_cast<int>(m_items.size()); }

    // Original names do not survive for 0x00524a30 and 0x00524c4c. These
    // semantic names describe their exact set-membership bodies.
    bool IsPremiumItem(int itemType) const;
    bool IsPetCollar(int itemType) const;

    int GetPeriodItemPeriod(int itemType) const;
    int GetDurationItemDuration(int itemType) const;

    // Current Cached.exe contains two non-equivalent definitions carrying
    // the same CReadFile::GetConsumeType(int) source identity. This is the
    // CacheD-local 0/1/2/3/4 mapping at 0x00524ca0.
    int GetConsumeType(int itemType);

    // Semantic disambiguation for the second shipping definition at
    // 0x00524e00. Its original guard is still kept by the body. It
    // returns the Shared/packet ItemConsumeType values -1/0/1/2/3.
    ItemConsumeType GetItemConsumeType(int itemType);
    CrystalType GetCrystalType(const wchar_t* value);

    // Current method at 0x00526448.
    void MakeItemDataTable();

    // Current overloads at 0x00527288 and 0x00527f54.
    bool ParseDBJobData(std::wistream& input);
    bool ParseDBJobData();

    // Current method at 0x00526760.
    void ExecuteDBJob();

private:

    using Items = std::map<int, CReadItemData*>;
    using ItemTypes = std::set<int>;
    using ItemValues = std::map<int, int>;

    // The record's original type name is absent from the surviving symbols.
    // Its value members and their use are recovered from 0x00526d78,
    // 0x00526e8c, 0x00527288 and 0x00526760.
    struct DBJobEntry {
        std::set<int> itemList;
        std::set<int> buffList;
        std::set<int> questList;
        std::int32_t bookingTime = -1;
        CacheDServerType serverType = CacheDServerType::Live;
        bool deleteUiDate = false;
        int deleteUiDateValue = 0;
    };

    l2portable_detail::CriticalSectionStorage m_lock; // target +0x00
    Items m_items; // target +0x28
    ItemTypes m_premiumItems; // target +0x48
    ItemValues m_periodItems; // target +0x68
    ItemValues m_durationItems; // target +0x88
    ItemTypes m_petCollars; // target +0xa8
    std::list<DBJobEntry> m_dbJobs; // target +0xc8
};



// Current global object is DAT_02a1e350. The older C1 source independently
// confirms the original source-level name g_readFile.
extern CReadFile g_readFile;

// The current zero-filled region is exactly [0x02a1df80,0x02a1e350):
// 122 int64 entries. Its original current data symbol is absent. The same
// owner in the supporting HF image identifies the source-level name.
extern std::int64_t g_ExpTable[122];
