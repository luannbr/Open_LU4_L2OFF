#include "PledgeAnnounceDBHandlerImpl.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/DBConn.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/TextRuntime.h"

#include <array>
#include <string>

CPledgeAnnounceSP CPledgeAnnounceDBHandlerImpl::Load(unsigned int pledgeDbId) {
    // Current Cached.exe 0x0050a240.
    L2_GUARD_FUNCTION();

    int showFlag = 0;
    std::array<char16_t, 3001> content{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&showFlag);
    connection.Bind(content.data(), 0x1772);
    constexpr const wchar_t* statement = L"{Call lin_LoadPledgeAnnounce (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledgeDbId);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
        return {};
    }

    CPledgeAnnounce* const loaded = new CPledgeAnnounce(pledgeDbId, showFlag == 1, content.data());
    CPledgeAnnounceSP result(loaded, ObjectRefType::FunctionChain);
    L2_RELEASE(loaded, ObjectRefType::CreateDelete);
    return result;
}

bool CPledgeAnnounceDBHandlerImpl::Set(CPledgeAnnounceSP pledgeAnnounce) {
    // Current Cached.exe 0x0050a504.
    L2_GUARD_FUNCTION();

    const int pledgeDbId = pledgeAnnounce->GetId();
    const int showFlag = pledgeAnnounce->DoesShow() ? 1 : 0;
    const std::u16string content(runtime::StringView(pledgeAnnounce->GetContent()));
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetPledgeAnnounce (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledgeDbId);
    connection.BindParameter(&showFlag);
    connection.BindParameter(content.c_str());
    return connection.ExecutePrepared(statement);
}

bool CPledgeAnnounceDBHandlerImpl::SwitchFlag(unsigned int pledgeDbId, bool flag) {
    // Current Cached.exe 0x0050a728.
    L2_GUARD_FUNCTION();

    const int showFlag = flag ? 1 : 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SwitchPledgeAnnounceShowFlag (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledgeDbId);
    connection.BindParameter(&showFlag);
    return connection.ExecutePrepared(statement);
}
