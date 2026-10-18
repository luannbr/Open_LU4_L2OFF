#include "Shared/src/TextRuntime.h"
#include "Shared/src/Log.h"
#include "Shared/src/MemorySub.h"

#include "ManorDb.h"

#include "CacheSocket.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/DBConn.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/IOCPacket.h"
#include "Runtime.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <cwchar>

namespace {


bool SaveManorSeedRecord(int manorId, int index, const CManorSeedData& data) {
    long long seedList = data.m_nSeedList;
    long long seedPrice = data.m_nSeedPrice;
    long long seedSetCount = data.m_nSeedSetCount;
    long long seedRemainCount = data.m_nSeedRemainCount;
    long long cropList = data.m_nProcuringCropList;
    long long cropSetCount = data.m_nProcuringCropSetCount;
    long long cropCount = data.m_nProcuringCropCount;
    long long cropPrice = data.m_nProcuringCropPrice;
    long long cropType = data.m_nProcuringCropType;
    long long procuredCropList = data.m_nProcuredCropList;

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveManorSeed (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&manorId);
    connection.BindParameter(&index);
    connection.BindParameter(&seedList);
    connection.BindParameter(&seedPrice);
    connection.BindParameter(&seedSetCount);
    connection.BindParameter(&seedRemainCount);
    connection.BindParameter(&cropList);
    connection.BindParameter(&cropSetCount);
    connection.BindParameter(&cropCount);
    connection.BindParameter(&cropPrice);
    connection.BindParameter(&cropType);
    connection.BindParameter(&procuredCropList);
    return connection.ExecutePrepared(statement);
}

bool SaveNextManorSeedRecord(int manorId, int index, const CManorSeedDataN& data) {
    long long seedList = data.m_nSeedList_n;
    long long seedPrice = data.m_nSeedPrice_n;
    long long seedSetCount = data.m_nSeedSetCount_n;
    long long cropList = data.m_nProcuringCropList_n;
    long long cropCount = data.m_nProcuringCropCount_n;
    long long cropPrice = data.m_nProcuringCropPrice_n;
    long long cropType = data.m_nProcuringCropType_n;

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveManorSeed_N (?, ?, ?, ?, ?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&manorId);
    connection.BindParameter(&index);
    connection.BindParameter(&seedList);
    connection.BindParameter(&seedPrice);
    connection.BindParameter(&seedSetCount);
    connection.BindParameter(&cropList);
    connection.BindParameter(&cropCount);
    connection.BindParameter(&cropPrice);
    connection.BindParameter(&cropType);
    return connection.ExecutePrepared(statement);
}

} // namespace

CManorDb g_manorDb;

L2_DEFINE_OBJECT_CLASS_ID(CManor)

L2_DEFINE_OBJECT_CLASS_NAME(CManor, m_pClassName_CManor)

L2_DEFINE_OBJECT_NEW(CManor)

// Current allocator 0x004f339c publishes 0x1850 bytes. CRT initializer
// 0x006a3164 is the twentieth assignment of the shared class counter.
L2_DEFINE_OBJECT_ALLOCATOR(CManor, m_pClassName_CManor)

L2_DEFINE_OBJECT_RELEASE(CManor)

CManor::CManor(int manorId) :
    m_nManorId(manorId),
    m_manorSeedData{},
    m_manorSeedDataN{},
    m_seedToChangeData{},
    m_cropToChangeData{},
    m_nResidenceID(0),
    m_nSeedSellBank(0),
    m_nProcureBuyRemainBank(0),
    m_lastChanged{},
    m_byChangeState(0) {
    // Cached.exe 0x004f5928 initializes every semantic member above but
    // does not write the alignment regions +0x5c, +0x1824 or +0x1849.
    L2_GUARD_FUNCTION();

    CWareHouse* const warehouse = new CWareHouse;
    warehouse->SetOwnerId(static_cast<std::uint32_t>(m_nManorId));
    warehouse->Init(WareHouseType_Manor, true);
    m_warehouse.SetObject(warehouse);
    L2_RELEASE(warehouse, ObjectRefType::CreateDelete);
}

CManor::~CManor() { L2_GUARD_FUNCTION(); }

bool CManor::SetAdena(int seedSellBank, int procureBuyRemainBank) {
    L2_GUARD_FUNCTION();
    m_nSeedSellBank = seedSellBank;
    m_nProcureBuyRemainBank = procureBuyRemainBank;
    return true;
}

void CManor::SendManorInfoAndData(CSocket* socket) {
    L2_GUARD_FUNCTION();
    if (m_nResidenceID > 0) {
        socket->Send(
            "cddQQcbbb", 0xb3, m_nManorId, m_nResidenceID, static_cast<long long>(m_nSeedSellBank),
            static_cast<long long>(m_nProcureBuyRemainBank), static_cast<int>(m_byChangeState),
            static_cast<int>(sizeof(m_lastChanged)), &m_lastChanged, static_cast<int>(sizeof(m_manorSeedData)),
            m_manorSeedData.data(), static_cast<int>(sizeof(m_manorSeedDataN)), m_manorSeedDataN.data()
        );
    }
}

bool CManor::SetManorSeedToChangeData(int itemType, std::int64_t amount) {
    L2_GUARD_FUNCTION();
    for (std::size_t index = 0; index < m_manorSeedData.size(); ++index) {
        CManorSeedData& data = m_manorSeedData[index];
        if (data.m_nSeedList != itemType)
            continue;
        const std::int64_t pending = m_seedToChangeData[index];
        if (amount <= data.m_nSeedRemainCount - pending) {
            m_seedToChangeData[index] = pending + amount;
            return true;
        }
        return false;
    }
    return false;
}

bool CManor::SetManorCropToChangeData(int itemType, std::int64_t amount) {
    L2_GUARD_FUNCTION();
    for (std::size_t index = 0; index < m_manorSeedData.size(); ++index) {
        CManorSeedData& data = m_manorSeedData[index];
        if (data.m_nProcuringCropList != itemType)
            continue;
        const std::int64_t pending = m_cropToChangeData[index];
        if (amount <= data.m_nProcuringCropCount - pending) {
            m_cropToChangeData[index] = pending + amount;
            return true;
        }
        return false;
    }
    return false;
}

void CManor::ResetManorSeedToChangeData() {
    // Current body 0x004f3a08.
    L2_GUARD_FUNCTION();
    m_seedToChangeData.fill(0);
}

void CManor::ResetManorCropToChangeData() {
    // Current body 0x004f3aa4 has the same surviving guard literal as the
    // seed reset, but clears the distinct +0x16e0 block.
    L2_GUARD_FUNCTION();
    m_cropToChangeData.fill(0);
}

bool CManor::ApplyManorSeedToChangeData() {
    // Current bool body 0x004f5344. Original overload identity is absent.
    L2_GUARD_FUNCTION();
    for (std::size_t index = 0; index < m_manorSeedData.size(); ++index) {
        const std::int64_t pending = m_seedToChangeData[index];
        if (pending <= 0)
            continue;
        CManorSeedData& data = m_manorSeedData[index];
        if (data.m_nSeedRemainCount - pending < 0)
            return false;
        data.m_nSeedRemainCount -= pending;
        SaveManorSeedRecord(m_nManorId, static_cast<int>(index), data);
    }
    return true;
}

bool CManor::ApplyManorCropToChangeData() {
    // Current bool body 0x004f5518. Its guard literal collides with 0x4f5344.
    L2_GUARD_FUNCTION();
    for (std::size_t index = 0; index < m_manorSeedData.size(); ++index) {
        const std::int64_t pending = m_cropToChangeData[index];
        if (pending <= 0)
            continue;
        CManorSeedData& data = m_manorSeedData[index];
        if (data.m_nProcuringCropCount - pending < 0)
            return false;
        data.m_nProcuringCropCount -= pending;
        data.m_nProcuredCropList += pending;
        SaveManorSeedRecord(m_nManorId, static_cast<int>(index), data);
    }
    return true;
}

CWareHouseSP CManor::GetWareHouse() {
    L2_GUARD_FUNCTION();
    return m_warehouse.GetObjectW();
}

bool CManor::LoadData() {
    L2_GUARD_FUNCTION();
    if (m_nResidenceID < 0)
        return false;

    int index = 0;
    int seedList = 0;
    long long seedPrice = 0;
    long long seedSetCount = 0;
    int cropList = 0;
    long long cropCount = 0;
    long long cropPrice = 0;
    int cropType = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&index);
    connection.Bind(&seedList);
    connection.Bind(&seedPrice);
    connection.Bind(&seedSetCount);
    connection.Bind(&cropList);
    connection.Bind(&cropCount);
    connection.Bind(&cropPrice);
    connection.Bind(&cropType);

    constexpr const wchar_t* nextStatement = L"{Call lin_LoadManorSeed_N (?)}";
    connection.Prepare(nextStatement);
    connection.BindParameter(&m_nManorId);
    if (!connection.ExecutePrepared(nextStatement)) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot load manor data_n, [%d]", "ManorDb.cpp", 0x71, m_nManorId)
        // Log.Add(LOG_ERROR,
        //     "[Manor] Cannot load next-period seed and crop data for manor %d. Continuing with the current-period load.",
        //     m_nManorId)
        L2_LOG_ERROR(Log,
            "[Manor] Cannot load next-period seed and crop data for manor %d; continuing with the current-period load.",
            m_nManorId);
    } else {
        while (connection.Fetch()) {
            // The target trusts SQL indexes. Index 40 still lands inside
            // CManor (at +0x15a0), so keep that exact layout effect.
            // wider corruptions are excluded at the portable memory boundary.
            if (index < 0 || index > 40)
                continue;
            CManorSeedDataN data{};
            data.m_nSeedList_n = seedList;
            data.m_nSeedPrice_n = seedPrice;
            data.m_nSeedSetCount_n = seedSetCount;
            data.m_nProcuringCropList_n = cropList;
            data.m_nProcuringCropCount_n = cropCount;
            data.m_nProcuringCropPrice_n = cropPrice;
            data.m_nProcuringCropType_n = cropType;
            WriteNextDataTargetIndex(index, data);
        }
    }

    connection.UnBind();
    long long seedRemainCount = 0;
    long long cropSetCount = 0;
    long long cropRemainCount = 0;
    long long procuredCropList = 0;
    connection.Bind(&index);
    connection.Bind(&seedList);
    connection.Bind(&seedPrice);
    connection.Bind(&seedSetCount);
    connection.Bind(&seedRemainCount);
    connection.Bind(&cropList);
    // Current SQL result order is crop price, set count, remain count.
    connection.Bind(&cropPrice);
    connection.Bind(&cropSetCount);
    connection.Bind(&cropRemainCount);
    connection.Bind(&cropType);
    connection.Bind(&procuredCropList);

    constexpr const wchar_t* currentStatement = L"{Call lin_LoadManorSeed (?)}";
    connection.Prepare(currentStatement);
    connection.BindParameter(&m_nManorId);
    if (!connection.ExecutePrepared(currentStatement)) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot load manor data, [%d]", "ManorDb.cpp", 0x97, m_nManorId)
        // Log.Add(LOG_ERROR,
        //     "[Manor] Cannot load current-period seed and crop data for manor %d. Load failed.",
        //     m_nManorId)
        L2_LOG_ERROR(Log,
            "[Manor] Cannot load current-period seed and crop data for manor %d; load failed.",
            m_nManorId);
        return false;
    }

    while (connection.Fetch()) {
        // Valid production rows are 0..39. Current Cached.exe performs no
        // check. Rejecting wider DB corruption is the portable memory edge.
        if (index < 0 || index >= 40)
            continue;
        CManorSeedData& data = m_manorSeedData[static_cast<std::size_t>(index)];
        data.m_nSeedList = seedList;
        data.m_nSeedPrice = seedPrice;
        data.m_nSeedSetCount = seedSetCount;
        data.m_nSeedRemainCount = seedRemainCount;
        data.m_nProcuringCropList = cropList;
        data.m_nProcuringCropSetCount = cropSetCount;
        data.m_nProcuringCropCount = cropRemainCount;
        data.m_nProcuringCropPrice = cropPrice;
        data.m_nProcuringCropType = cropType;
        data.m_nProcuredCropList = procuredCropList;
    }
    return true;
}

bool CManor::LoadInfo() {
    L2_GUARD_FUNCTION();

    int residenceId = -1;
    long long seedSellBank = 0;
    long long procureBuyRemainBank = 0;
    int changeState = 0;
    std::array<char16_t, 28> lastChanged{};

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&residenceId);
    connection.Bind(&seedSellBank);
    connection.Bind(&procureBuyRemainBank);
    connection.Bind(&changeState);
    connection.Bind(lastChanged.data(), 0x32);
    constexpr const wchar_t* statement = L"{Call lin_LoadManorInfo (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nManorId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot load manor info, [%d]", "ManorDb.cpp", 0xcb, m_nManorId)
        // Log.Add(LOG_ERROR,
        //     "[Manor] Information query failed for manor %d. Cached information not updated.",
        //     m_nManorId)
        L2_LOG_ERROR(Log,
            "[Manor] Information query failed for manor %d; cached information not updated.",
            m_nManorId);
        return false;
    }
    if (!connection.Fetch()) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot load manor info, [%d]", "ManorDb.cpp", 0xc5, m_nManorId)
        // Log.Add(LOG_ERROR,
        //     "[Manor] No information row was fetched for manor %d. Cached information not updated.",
        //     m_nManorId)
        L2_LOG_ERROR(Log,
            "[Manor] No information row was fetched for manor %d; cached information not updated.",
            m_nManorId);
        return false;
    }

    m_nResidenceID = residenceId;
    m_nSeedSellBank = seedSellBank;
    m_nProcureBuyRemainBank = procureBuyRemainBank;
    m_byChangeState = static_cast<std::uint8_t>(changeState);

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    std::swscanf(Utf16ToWide(lastChanged.data()).c_str(), L"%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second);
    m_lastChanged.wYear = static_cast<std::uint16_t>(year);
    m_lastChanged.wMonth = static_cast<std::uint16_t>(month);
    m_lastChanged.wDay = static_cast<std::uint16_t>(day);
    m_lastChanged.wHour = static_cast<std::uint16_t>(hour);
    m_lastChanged.wMinute = static_cast<std::uint16_t>(minute);
    m_lastChanged.wSecond = static_cast<std::uint16_t>(second);
    return true;
}

bool CManor::SaveInfo() {
    L2_GUARD_FUNCTION();

    std::array<wchar_t, 20> lastChanged{};
    std::swprintf(
        lastChanged.data(), lastChanged.size(), L"%d-%d-%d %d:%d:%d", static_cast<int>(m_lastChanged.wYear),
        static_cast<int>(m_lastChanged.wMonth), static_cast<int>(m_lastChanged.wDay), static_cast<int>(m_lastChanged.wHour),
        static_cast<int>(m_lastChanged.wMinute), static_cast<int>(m_lastChanged.wSecond)
    );
    short changeState = m_byChangeState;
    long long seedSellBank = m_nSeedSellBank;
    long long procureBuyRemainBank = m_nProcureBuyRemainBank;

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveManorInfo (?, ?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_nManorId);
    connection.BindParameter(&m_nResidenceID);
    connection.BindParameter(&seedSellBank);
    connection.BindParameter(&procureBuyRemainBank);
    connection.BindParameter(&changeState);
    connection.BindParameter(lastChanged.data());
    const bool saved = connection.ExecutePrepared(statement);
    if (!saved) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot Save manor info, [%d]-[%d][%d][%d]", "ManorDb.cpp", 0xe5, m_nManorId,
        //     m_nResidenceID, static_cast<int>(m_nSeedSellBank), static_cast<int>(m_nProcureBuyRemainBank)
        // )
        // Log.Add(LOG_ERROR,
        //     "[Manor] Cannot save manor %d, residence %d: seed sales bank %lld, remaining crop purchase bank %lld.",
        //     m_nManorId, m_nResidenceID, seedSellBank, procureBuyRemainBank)
        L2_LOG_ERROR(Log,
            "[Manor] Cannot save manor %d, residence %d: seed sales bank %lld, remaining crop purchase bank %lld.",
            m_nManorId, m_nResidenceID, seedSellBank, procureBuyRemainBank);
    }
    return saved;
}

void CManor::SetNowManorInfo(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current member body 0x004f4ad8 keeps a guard literal without the
    // CManor qualifier. RCX and every +offset access establish its owner.
    L2_GUARD_FUNCTION();

    int manorId = 0;
    int residenceId = 0;
    int seedSellBank = 0;
    int procureBuyRemainBank = 0;
    std::uint8_t changeState = 0;
    SYSTEMTIME lastChanged{};
    ::Disassemble(
        packet, "ddddcb", &manorId, &residenceId, &seedSellBank, &procureBuyRemainBank, &changeState,
        static_cast<int>(sizeof(lastChanged)), &lastChanged
    );
    if (manorId != m_nManorId) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Manor id is mismatch. [%d]-[%d]", "ManorDb.cpp", 0x110, m_nManorId, manorId
        // )
        // Log.Add(LOG_ERROR,
        //     "[Manor] Rejected current information update: expected manor %d, received %d. No update applied.",
        //     m_nManorId, manorId)
        L2_LOG_ERROR(Log,
            "[Manor] Rejected current information update: expected manor %d, received %d; no update applied.",
            m_nManorId, manorId);
        return;
    }

    m_nResidenceID = residenceId;
    SetAdena(seedSellBank, procureBuyRemainBank);
    m_byChangeState = changeState;
    m_lastChanged = lastChanged;
    SaveInfo();
}

void CManor::SetNowManorSeed(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current member body 0x004f4c78.
    L2_GUARD_FUNCTION();

    int manorId = 0;
    int residenceId = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "dd", &manorId, &residenceId);
    // The paired current L2Server sender proves this is the residence ID.
    // The shipping member validates only its manor ID and otherwise ignores it.
    if (manorId != m_nManorId) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Manor id is mismatch. [%d]-[%d]", "ManorDb.cpp", 0x140, m_nManorId, manorId
        // )
        // Log.Add(LOG_ERROR,
        //     "[Manor] Rejected current seed update: expected manor %d, received %d. No update applied.",
        //     m_nManorId, manorId)
        L2_LOG_ERROR(Log,
            "[Manor] Rejected current seed update: expected manor %d, received %d; no update applied.",
            m_nManorId, manorId);
        return;
    }

    int index = 0;
    std::array<int, 10> values{};
    ::Disassemble(
        cursor, "ddddddddddd", &index, &values[0], &values[1], &values[2], &values[3], &values[4], &values[5], &values[6],
        &values[7], &values[8], &values[9]
    );
    if (index < 0 || index >= 40) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Manor index is over limit. [%d]-[%d]", "ManorDb.cpp", 0x148, m_nManorId, index
        // )
        // Log.Add(LOG_ERROR,
        //     "[Manor] Rejected current seed update for manor %d: row index %d is outside 0..39. No update applied.",
        //     m_nManorId, index)
        L2_LOG_ERROR(Log,
            "[Manor] Rejected current seed update for manor %d: row index %d is outside 0..39; no update applied.",
            m_nManorId, index);
        return;
    }

    CManorSeedData& data = m_manorSeedData[static_cast<std::size_t>(index)];
    data.m_nSeedList = values[0];
    data.m_nSeedPrice = values[1];
    data.m_nSeedSetCount = values[2];
    data.m_nSeedRemainCount = values[3];
    data.m_nProcuringCropList = values[4];
    data.m_nProcuringCropSetCount = values[5];
    data.m_nProcuringCropCount = values[6];
    data.m_nProcuringCropPrice = values[7];
    data.m_nProcuringCropType = values[8];
    data.m_nProcuredCropList = values[9];
    SaveManorSeedRecord(m_nManorId, index, data);
}

void CManor::SetNextManorSeed(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current member body 0x004f5020.
    L2_GUARD_FUNCTION();

    int manorId = 0;
    int residenceId = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "dd", &manorId, &residenceId);
    // The paired current L2Server sender proves this is the residence ID.
    // The shipping member validates only its manor ID and otherwise ignores it.
    if (manorId != m_nManorId) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Manor id is mismatch. [%d]-[%d]", "ManorDb.cpp", 0x186, m_nManorId, manorId
        // )
        // Log.Add(LOG_ERROR,
        //     "[Manor] Rejected next-period seed update: expected manor %d, received %d. No update applied.",
        //     m_nManorId, manorId)
        L2_LOG_ERROR(Log,
            "[Manor] Rejected next-period seed update: expected manor %d, received %d; no update applied.",
            m_nManorId, manorId);
        return;
    }

    int index = 0;
    std::array<int, 7> values{};
    ::Disassemble(
        cursor, "dddddddd", &index, &values[0], &values[1], &values[2], &values[3], &values[4], &values[5], &values[6]
    );
    if (index < 0 || index >= 41) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Manor index is over limit. [%d]-[%d]", "ManorDb.cpp", 0x18e, m_nManorId, index
        // )
        // Log.Add(LOG_ERROR,
        //     "[Manor] Rejected next-period seed update for manor %d: row index %d is outside 0..40. No update applied.",
        //     m_nManorId, index)
        L2_LOG_ERROR(Log,
            "[Manor] Rejected next-period seed update for manor %d: row index %d is outside 0..40; no update applied.",
            m_nManorId, index);
        return;
    }

    CManorSeedDataN data{};
    data.m_nSeedList_n = values[0];
    data.m_nSeedPrice_n = values[1];
    data.m_nSeedSetCount_n = values[2];
    data.m_nProcuringCropList_n = values[3];
    data.m_nProcuringCropCount_n = values[4];
    data.m_nProcuringCropPrice_n = values[5];
    data.m_nProcuringCropType_n = values[6];
    // The target accepts index 40 and writes this 0x38-byte row over the
    // first seven pending-seed entries. The raw contiguous write keeps
    // that shipping off-by-one without leaving CManor's allocated extent.
    WriteNextDataTargetIndex(index, data);
    SaveNextManorSeedRecord(m_nManorId, index, ReadNextDataTargetIndex(index));
}

int CManor::GetId() const noexcept { return m_nManorId; }

CManorSeedDataN CManor::ReadNextDataTargetIndex(int index) const {
    CManorSeedDataN result{};
    const auto* const base = reinterpret_cast<const std::uint8_t*>(m_manorSeedDataN.data());
    std::memcpy(&result, base + static_cast<std::size_t>(index) * sizeof(result), sizeof(result));
    return result;
}

void CManor::WriteNextDataTargetIndex(int index, const CManorSeedDataN& data) {
    auto* const base = reinterpret_cast<std::uint8_t*>(m_manorSeedDataN.data());
    std::memcpy(base + static_cast<std::size_t>(index) * sizeof(data), &data, sizeof(data));
}

CManorDb::CManorDb() = default;
CManorDb::~CManorDb() = default;

void CManorDb::Add(CManor* manor) {
    L2_GUARD_FUNCTION();
    if (manor) {
        m_manors.Insert(manor->GetId(), manor);
    }
}

CManorSP CManorDb::LoadFromDb(int manorId) {
    L2_GUARD_FUNCTION();
    CManor* const manor = new CManor(manorId);
    manor->LoadInfo();
    manor->LoadData();
    CManorSP result(manor, ObjectRefType::FunctionChain);
    Add(result.get());
    // Current 0x004f5afc, unlike CCastleDb::LoadFromDb, does not release the
    // initial CreateDelete reference. Manor objects are therefore kept
    // for process lifetime even when the map is destroyed.
    return result;
}

CManorSP CManorDb::GetManor(int manorId) {
    L2_GUARD_FUNCTION();
    CManorSP manor = m_manors.FindObject(manorId);
    if (!manor)
        manor = LoadFromDb(manorId);
    if (!manor) {
        // Log.Add(LOG_ERROR, "[ManorDb.cpp][%d] undefined (or unregistered) manor Id[%d]", 0x2a0, manorId)
        // Log.Add(LOG_ERROR,
        //     "[Manor] Manor %d remains unavailable after cache lookup and database load. Reporting call stack.",
        //     manorId)
        L2_LOG_ERROR(Log,
            "[Manor] Manor %d remains unavailable after cache lookup and database load; reporting call stack.",
            manorId);
        runtime::process::ReportCallStack(2, false);
    }
    return manor;
}
