#include "UserMacro.h"

#include "CacheSocket.h"
#include "DBConn.h"

#include "L2Core/src/inc/Guard.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

template <std::size_t Capacity>
void CopyMacroText(std::array<char16_t, Capacity>& destination, const char16_t* source) {
    destination.fill(u'\0');
    if (!source)
        return;

    const std::u16string_view target(source);
    const std::size_t count = std::min(target.size(), Capacity - 1);
    std::copy_n(target.begin(), count, destination.begin());
}

} // namespace

BaseMacroStrategy::~BaseMacroStrategy() { m_socket = nullptr; }

DefaultMacroStrategy::DefaultMacroStrategy(
    int characterId, int userServerId, CSocket* socket, int responseType, int macroId
) noexcept :
    BaseMacroStrategy(characterId, userServerId, socket), m_responseType(responseType), m_macroId(macroId) {}

void DefaultMacroStrategy::ProcessFailure() {
    // Current Cached.exe 0x005c6d68.
    L2_GUARD_FUNCTION();
    m_socket->Send("cddcdc", 0xae, m_characterId, m_userServerId, m_responseType, m_macroId, 0);
}

bool DefaultMacroStrategy::Send(UserMacroIdSet& macroIds, UserMacroInfoMap& macroRows, int macroCount) {
    // Current Cached.exe 0x005c74ac. In particular, operation 0 clears the
    // ID set without replacing macroCount, operations 1/2 reduce both to the
    // selected ID, and operation 3 changes its outgoing type to 1.
    L2_GUARD_FUNCTION();

    int responseMacroCount = macroCount;
    if (m_responseType == 0) {
        macroIds.clear();
    } else if (m_responseType == 1 || m_responseType == 2) {
        macroIds.clear();
        macroIds.insert(m_macroId);
        responseMacroCount = static_cast<int>(macroIds.size());
    } else if (m_responseType == 3) {
        m_responseType = 1;
    }

    bool sent = false;
    int visited = 0;
    for (int macroId : macroIds) {
        if (visited >= 48)
            break;

        const auto range = macroRows.equal_range(macroId);
        const auto rowCount = static_cast<int>(std::distance(range.first, range.second));
        if (range.first != macroRows.end() && (m_responseType == 1 || m_macroId == macroId) &&
            range.first->second != nullptr) {
            const UserMacroInfoTableJoin& root = *range.first->second;

            CPacketHelperEx<20480> packet;
            if (packet.Assemble(
                    "cddcdcdSSSdc", 0xae, m_characterId, m_userServerId, m_responseType, m_macroId, responseMacroCount,
                    root.macroId, root.macroName.data(), root.macroTooltip.data(), root.macroIconName.data(),
                    root.macroIconType, rowCount
                ) != 0) {
                CPacketHelperEx<20480> infoPacket;
                for (auto row = range.first; row != range.second; ++row) {
                    if (!row->second)
                        continue;
                    const UserMacroInfo& info = row->second->macroInfo;
                    infoPacket.Assemble(
                        "ccdcS", info.macroOrder, info.macroInt1, info.macroInt2, info.macroInt3, info.macroString.data()
                    );
                }

                if (packet.Assemble("b", infoPacket.GetLength(), infoPacket.GetPacket()) != 0) {
                    m_socket->Send("b", packet.GetLength(), packet.GetPacket());
                    sent = true;
                }
            }
        }
        ++visited;
    }
    return sent;
}

CopyOtherMacroStrategy::CopyOtherMacroStrategy(
    int sourceCharacterId, int userServerId, CSocket* socket, const char16_t* sourceCharacterName, int destinationCharacterId
) noexcept :
    BaseMacroStrategy(sourceCharacterId, userServerId, socket),
    m_sourceCharacterName(sourceCharacterName),
    m_destinationCharacterId(destinationCharacterId) {}

void CopyOtherMacroStrategy::ProcessFailure() {
    // Current Cached.exe 0x005c6e3c. The shipping strategy deliberately
    // performs no failure reply. RequestLoadOtherUserMacro sends its own
    // name/result response after SendMacro returns.
    L2_GUARD_FUNCTION();
}

bool CopyOtherMacroStrategy::Send(UserMacroIdSet& macroIds, UserMacroInfoMap& macroRows, int macroCount [[maybe_unused]]) {
    // Current Cached.exe 0x005c7de8. It copies at most the destination's
    // remaining 48-slot capacity and returns true even when there are no
    // source rows or no free slots.
    L2_GUARD_FUNCTION();

    const int destinationMacroCount = UserMacroHandler::GetMacroCount(m_destinationCharacterId);
    int visited = 0;
    for (const int sourceMacroId : macroIds) {
        if (48 - destinationMacroCount <= visited)
            break;

        const auto range = macroRows.equal_range(sourceMacroId);
        if (range.first != macroRows.end() && range.first->second != nullptr) {
            const UserMacroInfoTableJoin& root = *range.first->second;
            int responseType = 0;
            int destinationMacroId = 0;
            const int createdMacroId = UserMacroHandler::CreateUserMacro(
                m_destinationCharacterId, FixedUtf16View(root.macroName), FixedUtf16View(root.macroTooltip),
                FixedUtf16View(root.macroIconName), root.macroIconType, responseType, destinationMacroId
            );

            for (auto row = range.first; row != range.second; ++row) {
                if (!row->second)
                    continue;
                UserMacroInfo copied = row->second->macroInfo;
                copied.macroId = createdMacroId;
                UserMacroHandler::CreateUserMacroInfo(copied);
            }
        }
        ++visited;
    }
    return true;
}

int UserMacroHandler::GetMacroCount(int characterId) {
    // Current Cached.exe 0x005c79d4.
    L2_GUARD_FUNCTION();

    int macroCount = 0;
    std::int64_t nullableIndicator = -1;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.BindNullable(&macroCount, &nullableIndicator);

    constexpr const wchar_t* statement = L"{Call lin_GetMacroCount (? )}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }
    connection.UnBind();
    return macroCount;
}

int UserMacroHandler::CreateUserMacro(
    int characterId,
    std::u16string_view macroName,
    std::u16string_view macroTooltip,
    std::u16string_view macroIconName,
    int macroIconType,
    int& responseType,
    int& macroId
) {
    // Current Cached.exe 0x005c7b38. The outer DBConn is acquired before the
    // nested count query, and the two output references are assigned whenever
    // the count permits an insert, even if Execute fails.
    L2_GUARD_FUNCTION();

    std::int64_t nullableIndicator = -1;
    DBConn connection(runtime::process::CurrentDatabaseId());
    int createdMacroId = 0;
    if (GetMacroCount(characterId) < 48) {
        connection.BindNullable(&createdMacroId, &nullableIndicator);
        constexpr const wchar_t* statement = L"{Call lin_CreateMacro (?, ?, ?, ?, ? )}";
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        const std::u16string name(macroName);
        const std::u16string tooltip(macroTooltip);
        const std::u16string iconName(macroIconName);
        connection.BindParameter(name.c_str());
        connection.BindParameter(tooltip.c_str());
        connection.BindParameter(iconName.c_str());
        connection.BindParameter(&macroIconType);
        if (connection.ExecutePrepared(statement)) {
            connection.Fetch();
        }
        responseType = 1;
        macroId = createdMacroId;
        connection.UnBind();
    }
    return createdMacroId;
}

void UserMacroHandler::CreateUserMacroInfo(const UserMacroInfo& macroInfo) {
    // Current Cached.exe 0x005c7cc8.
    L2_GUARD_FUNCTION();

    const std::u16string macroString(FixedUtf16View(macroInfo.macroString));
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_CreateMacroInfo (?, ?, ?, ?, ?, ?  )}";
    connection.Prepare(statement);
    connection.BindParameter(&macroInfo.macroId);
    connection.BindParameter(&macroInfo.macroOrder);
    connection.BindParameter(&macroInfo.macroInt1);
    connection.BindParameter(&macroInfo.macroInt2);
    connection.BindParameter(&macroInfo.macroInt3);
    connection.BindParameter(macroString.c_str());
    connection.ExecutePrepared(statement);
    connection.UnBind();
}

void UserMacroHandler::SendMacro(BaseMacroStrategy& strategy) {
    // Current Cached.exe 0x005c8064.
    L2_GUARD_FUNCTION();

    // SendMacro reads the BaseMacroStrategy character ID at target +0x8.
    const int characterId = strategy.m_characterId;
    const int macroCount = GetMacroCount(characterId);

    int macroId = 0;
    int selectedCharacterId = 0;
    std::array<char16_t, 64> macroName{};
    std::array<char16_t, 64> macroTooltip{};
    std::array<char16_t, 64> macroIconName{};
    int macroIconType = 0;
    int macroOrder = 0;
    int macroInt1 = 0;
    int macroInt2 = 0;
    int macroInt3 = 0;
    std::array<char16_t, 81> macroString{};

    DBConn connection(runtime::process::CurrentDatabaseId());
    std::int64_t nullableIndicator = -1;
    connection.BindNullable(&macroId, &nullableIndicator);
    connection.BindNullable(&selectedCharacterId, &nullableIndicator);
    connection.BindNullable(macroName.data(), 0x80, &nullableIndicator);
    connection.BindNullable(macroTooltip.data(), 0x80, &nullableIndicator);
    connection.BindNullable(macroIconName.data(), 0x80, &nullableIndicator);
    connection.BindNullable(&macroIconType, &nullableIndicator);
    connection.BindNullable(&macroOrder, &nullableIndicator);
    connection.BindNullable(&macroInt1, &nullableIndicator);
    connection.BindNullable(&macroInt2, &nullableIndicator);
    connection.BindNullable(&macroInt3, &nullableIndicator);
    connection.BindNullable(macroString.data(), 0xa2, &nullableIndicator);

    UserMacroIdSet macroIds;
    UserMacroInfoMap macroRows;
    std::vector<std::unique_ptr<UserMacroInfoTableJoin>> allocatedRows;

    constexpr const wchar_t* statement = L"{Call lin_GetMacro (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            auto row = std::make_unique<UserMacroInfoTableJoin>();
            row->macroId = macroId;
            row->macroIconType = macroIconType;
            CopyMacroText(row->macroName, macroName.data());
            CopyMacroText(row->macroTooltip, macroTooltip.data());
            CopyMacroText(row->macroIconName, macroIconName.data());
            row->macroInfo.macroId = macroId;
            row->macroInfo.macroOrder = macroOrder;
            row->macroInfo.macroInt1 = macroInt1;
            row->macroInfo.macroInt2 = macroInt2;
            row->macroInfo.macroInt3 = macroInt3;
            CopyMacroText(row->macroInfo.macroString, macroString.data());

            UserMacroInfoTableJoin* const rawRow = row.get();
            macroIds.insert(macroId);
            macroRows.emplace(macroId, rawRow);
            allocatedRows.push_back(std::move(row));
        }
    }

    const bool sent = strategy.Send(macroIds, macroRows, macroCount);

    // The current body deletes every 0x240-byte join object before invoking
    // ProcessFailure. The multimap nodes themselves remain until afterward.
    allocatedRows.clear();
    if (!sent || macroIds.empty()) {
        strategy.ProcessFailure();
    }
}
