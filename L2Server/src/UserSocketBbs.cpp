#include "CommCli.h"
#include "Community.h"
#include "Protocol/src/SystemMessageIds.h"
#include "SSQ.h"
#include "Setting.h"
#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"
#include "User.h"
#include "UserSocket.h"

#include <array>
#include <cstddef>
#include <cstring>

namespace {

std::size_t TargetAsciiLength(const char* const value) { return std::strlen(value); }

bool TargetStartsWith(const SharedUtilsWideChar* const value, const char* const prefix) {
    const std::size_t length = TargetAsciiLength(prefix);
    for (std::size_t index = 0; index < length; ++index) {
        if (value[index] != static_cast<SharedUtilsWideChar>(static_cast<unsigned char>(prefix[index]))) {
            return false;
        }
    }
    return true;
}

void RequestBBSHome(User* const user) { commcli.RequestBBSInit(user, 0, 0, 0, 0, 1); }

} // namespace

// MSVC puts top-level const of pointer parameters into the decorated name.
// Keep the definition free of it so it matches the declaration elsewhere.
bool CommunityBBS(CSocket* socket, const SharedUtilsWideChar* command) {
    // Current L2Server 0x00c2e134.
    CUserSocket* const userSocket = static_cast<CUserSocket*>(socket);
    User* const user = userSocket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] No user... on Html CMD Packet (CommunityBBS)",
        //     L"usersocket_bbs.cpp",
        //     0x1d)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] No user... on Html CMD Packet (CommunityBBS)",
        //     L"usersocket_bbs.cpp",
        //     0x1d)
        L2_LOG_WARN(Log, "Community-board HTML command ignored: socket %p has no bound player.", static_cast<void*>(socket));
        return true;
    }

    constexpr char initPrefix[] = "_bbsinit";
    if (TargetStartsWith(command, initPrefix)) {
        int value = 0;
        ParseHTMLParam(command + TargetAsciiLength(initPrefix) + 1, const_cast<char*>("d"), &value);
        commcli.RequestBBSInit(user, value, 0, 0, 0, 1);
        return true;
    }

    constexpr char listPrefix[] = "_bbslist";
    if (TargetStartsWith(command, listPrefix)) {
        int boardId = 0;
        int page = 0;
        int flags = 0;
        ParseHTMLParam(command + TargetAsciiLength(listPrefix) + 1, const_cast<char*>("ddd"), &boardId, &page, &flags);
        commcli.RequestBBSList(user, boardId, page, flags);
        return true;
    }

    constexpr char dawnPrefix[] = "_bbsssqdawnlist";
    if (TargetStartsWith(command, dawnPrefix)) {
        int boardId = 0;
        int page = 0;
        int flags = 0;
        ParseHTMLParam(command + TargetAsciiLength(dawnPrefix) + 1, const_cast<char*>("ddd"), &boardId, &page, &flags);
        if (SSQSystem::inst().GetCurrentStatus() != SSQStatus::EVENT) {
            user->GetSocket()->SendSystemMessage(SystemMessageId::ONLY_AVAILABLE_DURING_COMPETITION_PERIOD);
            RequestBBSHome(user);
        } else if (user->GetSSQPart() != SSQPart::DAWN) {
            user->GetSocket()->SendSystemMessage(SystemMessageId::REQUIRES_LORDS_OF_DAWN_MEMBERSHIP);
            RequestBBSHome(user);
        } else {
            commcli.RequestBBSList(user, boardId, page, flags);
        }
        return true;
    }

    constexpr char twilightPrefix[] = "_bbsssqtwilightlist";
    if (TargetStartsWith(command, twilightPrefix)) {
        int boardId = 0;
        int page = 0;
        int flags = 0;
        ParseHTMLParam(command + TargetAsciiLength(twilightPrefix) + 1, const_cast<char*>("ddd"), &boardId, &page, &flags);
        if (SSQSystem::inst().GetCurrentStatus() != SSQStatus::EVENT) {
            user->GetSocket()->SendSystemMessage(SystemMessageId::ONLY_AVAILABLE_DURING_COMPETITION_PERIOD);
            RequestBBSHome(user);
        } else if (user->GetSSQPart() != SSQPart::TWILIGHT) {
            user->GetSocket()->SendSystemMessage(SystemMessageId::REQUIRES_REVOLUTIONARIES_OF_DUSK_MEMBERSHIP);
            RequestBBSHome(user);
        } else {
            commcli.RequestBBSList(user, boardId, page, flags);
        }
        return true;
    }

    constexpr char searchPrefix[] = "_bbssearch";
    if (TargetStartsWith(command, searchPrefix)) {
        int value1 = 0;
        int value2 = 0;
        int value3 = 0;
        int value4 = 0;
        std::array<SharedUtilsWideChar, 0x20> search{};
        ParseHTMLParam(
            command + TargetAsciiLength(searchPrefix) + 1, const_cast<char*>("ddddS"), &value1, &value2, &value3, &value4,
            0x40, search.data()
        );
        commcli.RequestBBSSearch(user, value1, value4, value2, value3, search.data());
        return true;
    }

    constexpr char readPrefix[] = "_bbsread";
    if (TargetStartsWith(command, readPrefix)) {
        if (userSocket->IsReadBBSBlocked()) {
            userSocket->SendSystemMessage(SystemMessageId::NO_READ_PERMISSION);
            RequestBBSHome(user);
            return true;
        }
        int boardId = 0;
        int articleId = 0;
        int boardType = 0;
        int listMode = 0;
        int listPage = 0;
        int searchType = 0;
        std::array<SharedUtilsWideChar, 0x15> search{};
        int searchStart = 1;
        int searchEnd = 1;
        ParseHTMLParam(
            command + TargetAsciiLength(readPrefix) + 1, const_cast<char*>("ddddddSdd"), &boardId, &articleId, &boardType,
            &listMode, &listPage, &searchType, 0x2a, search.data(), &searchStart, &searchEnd
        );
        commcli.RequestBBSRead(
            user, boardId, articleId, boardType, listMode, listPage, searchType, search.data(), searchStart, searchEnd
        );
        commcli.RequestBBSReadThread(
            user, boardId, articleId, boardType, listMode, listPage, searchType, search.data(), searchStart, searchEnd
        );
        commcli.RequestBBSReadComment(
            user, boardId, articleId, boardType, listMode, listPage, searchType, search.data(), searchStart, searchEnd
        );
        return true;
    }

    constexpr char errorReadPrefix[] = "_bbser1";
    if (TargetStartsWith(command, errorReadPrefix)) {
        int boardId = 0;
        int articleId = 0;
        int boardType = 0;
        int listMode = 0;
        int listPage = 0;
        int searchType = 0;
        std::array<SharedUtilsWideChar, 0x15> search{};
        int searchStart = 1;
        int searchEnd = 1;
        ParseHTMLParam(
            command + TargetAsciiLength(errorReadPrefix) + 1, const_cast<char*>("ddddddSdd"), &boardId, &articleId,
            &boardType, &listMode, &listPage, &searchType, 0x2a, search.data(), &searchStart, &searchEnd
        );
        userSocket->SendSystemMessage(SystemMessageId::NO_WRITE_PERMISSION);
        commcli.RequestBBSRead(
            user, boardId, articleId, boardType, listMode, listPage, searchType, search.data(), searchStart, searchEnd
        );
        commcli.RequestBBSReadThread(
            user, boardId, articleId, boardType, listMode, listPage, searchType, search.data(), searchStart, searchEnd
        );
        commcli.RequestBBSReadComment(
            user, boardId, articleId, boardType, listMode, listPage, searchType, search.data(), searchStart, searchEnd
        );
        return true;
    }

    constexpr char errorListPrefix[] = "_bbser2";
    if (TargetStartsWith(command, errorListPrefix)) {
        int boardId = 0;
        int page = 0;
        int flags = 0;
        ParseHTMLParam(command + TargetAsciiLength(errorListPrefix) + 1, const_cast<char*>("ddd"), &boardId, &page, &flags);
        userSocket->SendSystemMessage(SystemMessageId::NO_WRITE_PERMISSION);
        commcli.RequestBBSList(user, boardId, page, flags);
        return true;
    }

    constexpr char errorSearchPrefix[] = "_bbser3";
    if (TargetStartsWith(command, errorSearchPrefix)) {
        int value1 = 0;
        int value2 = 0;
        int value3 = 0;
        int value4 = 0;
        std::array<SharedUtilsWideChar, 0x20> search{};
        ParseHTMLParam(
            command + TargetAsciiLength(errorSearchPrefix) + 1, const_cast<char*>("ddddS"), &value1, &value2, &value3,
            &value4, 0x40, search.data()
        );
        userSocket->SendSystemMessage(SystemMessageId::NO_WRITE_PERMISSION);
        commcli.RequestBBSSearch(user, value1, value4, value2, value3, search.data());
        return true;
    }

    constexpr char writeFormPrefix[] = "_bbswriteform";
    if (TargetStartsWith(command, writeFormPrefix)) {
        if (userSocket->IsWriteBBSBlocked()) {
            userSocket->SendSystemMessage(SystemMessageId::NO_WRITE_PERMISSION);
            RequestBBSHome(user);
            return true;
        }
        int value1 = 0;
        int value2 = 0;
        ParseHTMLParam(command + TargetAsciiLength(writeFormPrefix) + 1, const_cast<char*>("dd"), &value1, &value2);
        commcli.RequestBBSWriteForm(user, value1, value2);
        return true;
    }

    constexpr char replyFormPrefix[] = "_bbsreplyform";
    if (TargetStartsWith(command, replyFormPrefix)) {
        if (userSocket->IsWriteBBSBlocked()) {
            userSocket->SendSystemMessage(SystemMessageId::NO_WRITE_PERMISSION);
            RequestBBSHome(user);
            return true;
        }
        int value1 = 0;
        int value2 = 0;
        int value3 = 0;
        ParseHTMLParam(
            command + TargetAsciiLength(replyFormPrefix) + 1, const_cast<char*>("ddd"), &value1, &value2, &value3
        );
        commcli.RequestBBSReply(user, value1, value3, value2);
        return true;
    }

    constexpr char modifyPrefix[] = "_bbsmodify";
    if (TargetStartsWith(command, modifyPrefix)) {
        if (userSocket->IsWriteBBSBlocked()) {
            userSocket->SendSystemMessage(SystemMessageId::NO_WRITE_PERMISSION);
            RequestBBSHome(user);
            return true;
        }
        int value1 = 0;
        int value2 = 0;
        int value3 = 0;
        ParseHTMLParam(command + TargetAsciiLength(modifyPrefix) + 1, const_cast<char*>("ddd"), &value1, &value2, &value3);
        commcli.RequestBBSModifyForm(user, value1, value3, value2);
        return true;
    }

    constexpr char deletePrefix[] = "_bbsdelete";
    if (TargetStartsWith(command, deletePrefix)) {
        int value1 = 0;
        int value2 = 0;
        int value3 = 0;
        int value4 = 0;
        int value5 = 0;
        ParseHTMLParam(
            command + TargetAsciiLength(deletePrefix) + 1, const_cast<char*>("ddddd"), &value1, &value2, &value3, &value4,
            &value5
        );
        commcli.RequestBBSDelete(user, value1, value5, value2, value3, value4);
        return true;
    }

    constexpr char homePrefix[] = "_bbshome";
    if (TargetStartsWith(command, homePrefix)) {
        RequestBBSHome(user);
        return true;
    }

    constexpr char locationPrefix[] = "_bbsloc";
    if (TargetStartsWith(command, locationPrefix)) {
        const FVector position = user->m_data->m_Pos;
        const RaceType race = user->m_data->m_Race;
        const int restartPoint = CSetting::inst().GetRestartPointSys(race, position, 1);
        if (restartPoint > 0) {
            commcli.RequestBBSList(user, -restartPoint, 1, 0);
        } else {
            RequestBBSHome(user);
        }
        return true;
    }

    constexpr char clanPrefix[] = "_bbsclan";
    if (TargetStartsWith(command, clanPrefix)) {
        commcli.RequestClanBBSInit(user);
        return true;
    }

    constexpr char addFavoritePrefix[] = "_bbsaddfav";
    if (TargetStartsWith(command, addFavoritePrefix)) {
        std::array<SharedUtilsWideChar, 0x32> string1{};
        std::array<SharedUtilsWideChar, 0x32> string2{};
        int value1 = 0;
        int value2 = 0;
        int value3 = 0;
        int value4 = 0;
        int value5 = 0;
        std::array<SharedUtilsWideChar, 0x15> string3{};
        ParseHTMLParam(
            command + TargetAsciiLength(addFavoritePrefix) + 1, const_cast<char*>("SSdddddS"), 100, string1.data(), 100,
            string2.data(), &value1, &value2, &value3, &value4, &value5, 0x2a, string3.data()
        );
        commcli.RequestBBSAddFavorite(
            user, string1.data(), string2.data(), value1, value2, value3, value4, value5, string3.data()
        );
        return true;
    }

    constexpr char getFavoritePrefix[] = "_bbsgetfav";
    if (TargetStartsWith(command, getFavoritePrefix)) {
        commcli.RequestBBSGetFavorite(user);
        return true;
    }

    constexpr char deleteFavoritePrefix[] = "_bbsdelfav";
    if (TargetStartsWith(command, deleteFavoritePrefix)) {
        int value = 0;
        ParseHTMLParam(command + TargetAsciiLength(deleteFavoritePrefix) + 1, const_cast<char*>("d"), &value);
        commcli.RequestBBSDelFavorite(user, value);
        return true;
    }

    constexpr char announcePrefix[] = "_bbsann";
    if (TargetStartsWith(command, announcePrefix)) {
        int value = 0;
        ParseHTMLParam(command + TargetAsciiLength(announcePrefix) + 1, const_cast<char*>("d"), &value);
        commcli.RequestBBSGetAnnounce(user, value);
        return true;
    }

    constexpr char memoPrefix[] = "_bbsmemo";
    if (TargetStartsWith(command, memoPrefix)) {
        SharedUtilsWideChar emptySearch[] = {0};
        commcli.RequestGetMemoList(user, 1, emptySearch);
        return true;
    }

    constexpr char linkPrefix[] = "_bbslink";
    if (TargetStartsWith(command, linkPrefix)) {
        CCommunity community(user->GetSocket(), 1);
        static_cast<void>(community.GetLinkPage());
    }
    return true;
}
