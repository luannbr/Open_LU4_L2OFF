#include "PledgeCrestDb.h"

#include "CacheSocket.h"
#include "FileServerManager.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/DBConn.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/Log.h"
#include "Shared/src/TextRuntime.h"
#include "Runtime.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <utility>

namespace {

char kPledgeCrestDbSource[] = "PledgeCrestDb.cpp";

CPledgeCrestKey CurrentPledgeCrestKey(std::uint32_t crestId) {
    return CPledgeCrestKey(runtime::process::CurrentDatabaseId(), static_cast<int>(crestId));
}

} // namespace

CPledgeCrestMap g_pledgeCrests;
CPledgeCrestDb g_pledgeCrestDb;
CPledgeEmblemDb g_pledgeEmblemDb;

std::uint32_t CPledgeCrestDb::CreatePledgeCrest(int bitmapSize, const std::uint8_t* bitmap) {
    L2_GUARD_FUNCTION();

    // Exact unsigned range test in current 0x0050efa4: (size - 1) < 0xc00.
    if (static_cast<std::uint32_t>(bitmapSize) - 1U >= static_cast<std::uint32_t>(CPledgeCrest::MAX_BITMAP_SIZE)) {
        return 0;
    }

    std::uint32_t crestId = 0;
    std::array<wchar_t, 0x400> query{};
    std::swprintf(query.data(), query.size(), L"{Call lin_SetPledgeCrest (%d, ?)}", bitmapSize);

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&crestId);
    const std::int64_t bitmapLength = bitmapSize;
    connection.BindParameterBinary(bitmap, static_cast<std::size_t>(bitmapSize), &bitmapLength);
    connection.Prepare(query.data());
    if (!connection.ExecutePrepared(query.data()) || !connection.Fetch() || crestId == 0) {
        return 0;
    }

    CPledgeCrest* const pledgeCrest = new CPledgeCrest(crestId, bitmapSize, bitmap);
    g_pledgeCrests.Insert(CurrentPledgeCrestKey(crestId), pledgeCrest, nullptr, -1);
    L2_RELEASE(pledgeCrest, ObjectRefType::CreateDelete);
    return crestId;
}

CPledgeCrestSP CPledgeCrestDb::LoadPledgeCrest(std::uint32_t crestId) {
    L2_GUARD_FUNCTION();

    int bitmapSize = 0;
    std::array<std::uint8_t, CPledgeCrest::MAX_BITMAP_SIZE> bitmap{};
    std::int64_t bitmapLength = -1;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&bitmapSize);
    connection.BindBinaryNullable(bitmap.data(), static_cast<int>(bitmap.size()), &bitmapLength);
    constexpr const wchar_t* statement = L"{Call lin_LoadPledgeCrest (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&crestId);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        CPledgeCrest* const pledgeCrest = new CPledgeCrest(crestId, bitmapSize, bitmap.data());
        g_pledgeCrests.Insert(CurrentPledgeCrestKey(crestId), pledgeCrest, nullptr, -1);
        L2_RELEASE(pledgeCrest, ObjectRefType::CreateDelete);
        return g_pledgeCrests.FindObject(CurrentPledgeCrestKey(crestId));
    }

    // Log.Add(LOG_ERROR, "LoadPledgeCrest Error crest_id[%u]", crestId)
    // Log.Add(LOG_ERROR,
    //     "[clan crest] No crest row was loaded for ID %u in database %d. No cached crest returned.",
    //     crestId, runtime::process::CurrentDatabaseId())
    L2_LOG_ERROR(Log,
        "[clan crest] No crest row was loaded for ID %u in database %d; no cached crest returned.",
        crestId, runtime::process::CurrentDatabaseId());
    runtime::process::ReportNamedCallStack(L"LoadPledgeCrest", 2, false);
    return {};
}

CPledgeCrestSP CPledgeCrestDb::GetPledgeCrest(std::uint32_t crestId) {
    L2_GUARD_FUNCTION();
    CPledgeCrestSP pledgeCrest = g_pledgeCrests.FindObject(CurrentPledgeCrestKey(crestId));
    if (!pledgeCrest) {
        pledgeCrest = LoadPledgeCrest(crestId);
    }
    return pledgeCrest;
}

bool CPledgeCrestDb::DeleteCrest(std::uint32_t crestId) {
    L2_GUARD_FUNCTION();
    CPledgeCrestSP pledgeCrest = GetPledgeCrest(crestId);
    if (!pledgeCrest)
        return false;

    g_pledgeCrests.DeleteObject(CurrentPledgeCrestKey(crestId), kPledgeCrestDbSource, 0xa3);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DelPledgeCrest (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&crestId);
    return connection.ExecutePrepared(statement);
}

void CPledgeEmblemDb::SendPledgeEmblem(
    int userSocketId, int pledgeEmblemId, const char16_t* downloadUrl, int worldId, int pledgeSocketId
) {
    L2_GUARD_FUNCTION();

    CSocket* const activeSocket = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (!activeSocket)
        return;
    activeSocket->Send(
        "chddddS",
        0xc6,
        0x17,
        userSocketId,
        pledgeEmblemId,
        worldId,
        pledgeSocketId,
        downloadUrl);
}

bool CPledgeEmblemDb::CreatePledgeEmblem(int userSocketId, int pledgeId, int pledgeSocketId) {
    L2_GUARD_FUNCTION();

    const auto found = m_pledgeEmblems.find(std::make_pair(userSocketId, pledgeId));
    if (found == m_pledgeEmblems.end()) {
        return false;
    }

    std::array<char, 0x10080> emblem{};
    std::size_t totalSize = 0;
    for (const auto& entry : found->second) {
        const PledgeEmblemPiece& piece = entry.second;
        if (piece.size < 0 || static_cast<std::size_t>(piece.size) > piece.data.size() ||
            static_cast<std::size_t>(piece.size) > emblem.size() - totalSize) {
            // Shipping code performs unchecked cumulative stack copies and
            // validates only afterwards. Reject only malformed input at the
            // portable memory-safety boundary. Every valid target path is
            // byte-for-byte identical below.
            return false;
        }
        std::memcpy(emblem.data() + totalSize, piece.data.data(), static_cast<std::size_t>(piece.size));
        totalSize += static_cast<std::size_t>(piece.size);
    }

    // Exact current unsigned test at 0x0050e8d4:
    // (totalSize - 1U) < 0x10080.
    if (static_cast<std::uint32_t>(totalSize) - 1U >= 0x10080U) {
        return false;
    }
    return g_fileServerManager.RequestRegistEmblem(
        emblem.data(), static_cast<int>(totalSize), userSocketId, pledgeId, pledgeSocketId
    );
}

void CPledgeEmblemDb::LoadPledgeEmblem(int userSocketId, int pledgeEmblemId, int worldId, int pledgeSocketId) {
    L2_GUARD_FUNCTION();

    std::array<char16_t, 0x400> downloadUrl{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(downloadUrl.data(), 0x800);
    constexpr const wchar_t* statement = L"{Call lin_LoadPledgeNewEmblem (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledgeEmblemId);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        if (g_pActiveServerSocket.load(std::memory_order_acquire)) {
            SendPledgeEmblem(userSocketId, pledgeEmblemId, downloadUrl.data(), worldId, pledgeSocketId);
        }
        return;
    }

    // Log.AddT(LOG_ERROR, L"qsLoadPledgeNewEmblem error pledge_emblem_id[%d]", pledgeEmblemId)
    // Log.Add(LOG_ERROR,
    //     "[clan emblem] No emblem row was loaded for ID %d in database %d. Download reply not sent to user socket %d (world %d, clan socket %d).",
    //     pledgeEmblemId, runtime::process::CurrentDatabaseId(), userSocketId, worldId, pledgeSocketId)
    L2_LOG_ERROR(Log,
        "[clan emblem] No emblem row was loaded for ID %d in database %d; download reply not sent to user socket %d (world %d, clan socket %d).",
        pledgeEmblemId, runtime::process::CurrentDatabaseId(), userSocketId, worldId, pledgeSocketId);
}

int CPledgeEmblemDb::AddPledgeEmblemPiece(
    int userSocketId, int pledgeId, int pledgeSocketId, int pieceIndex, int pieceSize, const unsigned char* pieceData
) {
    L2_GUARD_FUNCTION();
    L2_GUARD(static_cast<CIOCriticalSection&>(*this));

    const PledgeEmblemKey key = std::make_pair(userSocketId, pledgeId);
    if (pieceIndex == 0) {
        m_pledgeEmblems.erase(key);
    }

    if (pieceSize < 0 || static_cast<std::size_t>(pieceSize) > 0x3800U || (pieceSize != 0 && pieceData == nullptr)) {
        return 0;
    }

    PledgeEmblemPiece piece{};
    piece.size = pieceSize;
    if (pieceSize != 0) {
        std::memcpy(piece.data.data(), pieceData, static_cast<std::size_t>(pieceSize));
    }

    auto aggregate = m_pledgeEmblems.find(key);
    if (aggregate == m_pledgeEmblems.end()) {
        PledgeEmblemPieces pieces;
        pieces.emplace(pieceIndex, piece);
        aggregate = m_pledgeEmblems.emplace(key, std::move(pieces)).first;
    } else {
        // Current std::map::insert path at 0x0050e244 keeps the first value
        // for a duplicate index and destroys the newly allocated node.
        aggregate->second.emplace(pieceIndex, piece);
    }

    bool complete = true;
    for (int requiredIndex = 0; requiredIndex < 5; ++requiredIndex) {
        if (aggregate->second.find(requiredIndex) == aggregate->second.end()) {
            complete = false;
            break;
        }
    }
    if (!complete)
        return 0;

    if (!CreatePledgeEmblem(userSocketId, pledgeId, pledgeSocketId)) {
        // Log.AddT(LOG_ERROR, L"CreatePledgeEmblem failed [%d]", pledgeId)
        // Log.Add(LOG_ERROR,
        //     "[clan emblem] Cannot submit emblem registration for clan %d (user socket %d, clan socket %d). Collected pieces discarded.",
        //     pledgeId, userSocketId, pledgeSocketId)
        L2_LOG_ERROR(Log,
            "[clan emblem] Cannot submit emblem registration for clan %d (user socket %d, clan socket %d); collected pieces discarded.",
            pledgeId, userSocketId, pledgeSocketId);
    }
    m_pledgeEmblems.erase(key);
    return 0;
}
