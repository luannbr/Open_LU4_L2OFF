#include "Castle.h"
#include "Shared/src/MemorySub.h"

#include "Cached.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/DBConn.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/Runtime/TargetFormatting.h"
#include "Shared/src/TextRuntime.h"

#include <array>
#include <string>

namespace {

constexpr char16_t kEmptyTargetString[] = u"";

void WriteCastleItemCheckAudit(int castleId, int penaltyLevel, int checkFailType) {
    // Current CCastle::OnItemCheckFailed at 0x0049e998 emits this exact
    // 26-field route-8 record directly through CLog::AddV.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x5a4);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[8] = kEmptyTargetString;
    fields[11] = runtime::DecimalUtf16(3);
    fields[12] = runtime::DecimalUtf16(penaltyLevel);
    fields[13] = runtime::DecimalUtf16(castleId);
    fields[14] = runtime::DecimalUtf16(checkFailType);
    fields[21] = kEmptyTargetString;
    fields[22] = kEmptyTargetString;

    std::u16string record;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (index != 0)
            record.push_back(u',');
        record.append(fields[index]);
    }
    runtime::process::WriteLogFormatterLine(8, record);
}

} // namespace

L2_DEFINE_OBJECT_CLASS_ID(CCastle)

L2_DEFINE_OBJECT_CLASS_NAME(CCastle, m_pClassName_CCastle)

L2_DEFINE_OBJECT_NEW(CCastle)

// Current allocator 0x0049e6c8 publishes 0x60 bytes. The complete CRT
// initializer order makes 0x0069ff24 class ID 12.
L2_DEFINE_OBJECT_ALLOCATOR(CCastle, m_pClassName_CCastle)

L2_DEFINE_OBJECT_RELEASE(CCastle)

CCastle::CCastle(int castleId) : m_castleId(castleId) {
    // Cached.exe 0x004a0e68 writes the ID at +0x58 but leaves the
    // alignment word at +0x5c untouched.
    L2_GUARD_FUNCTION();

    CWareHouse* const warehouse = new CWareHouse;
    warehouse->SetOwnerId(static_cast<std::uint32_t>(m_castleId));
    warehouse->Init(WareHouseType_Castle, true);
    m_warehouse.SetObject(warehouse);
    ItemCheckOnLoad();
    L2_RELEASE(warehouse, ObjectRefType::CreateDelete);
}

CCastle::~CCastle() { L2_GUARD_FUNCTION(); }

CWareHouseSP CCastle::GetWareHouse() {
    L2_GUARD_FUNCTION();
    return m_warehouse.GetObjectW();
}

void CCastle::SetCheckSumOnDB() {
    L2_GUARD_FUNCTION();
    CWareHouseSP warehouse = GetWareHouse();
    warehouse->SetCheckSumOnDB();
}

void CCastle::SetLoadTime() {
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetCastleLoadTime (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_castleId);
    connection.ExecutePrepared(statement);
}

void CCastle::SetUnloadTime() {
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetCastleUnloadTime (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_castleId);
    connection.ExecutePrepared(statement);
}

void CCastle::OnItemCheckFailed(int checkFailType) {
    L2_GUARD_FUNCTION();
    if (!g_bUseDBSecurity)
        return;
    WriteCastleItemCheckAudit(m_castleId, g_nDBSecurityPenaltyLevel, checkFailType);
}

void CCastle::ItemCheckOnLoad() {
    L2_GUARD_FUNCTION();
    if (!g_bUseDBSecurity)
        return;
    SetLoadTime();
    CWareHouseSP warehouse = GetWareHouse();
    if (!warehouse->ItemcheckOnDB()) {
        OnItemCheckFailed(0);
    }
}

void CCastle::ItemCheckOnUnload() {
    L2_GUARD_FUNCTION();
    if (!g_bUseDBSecurity)
        return;
    SetCheckSumOnDB();
    SetUnloadTime();
}

int CCastle::GetId() const noexcept { return m_castleId; }
