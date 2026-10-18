#include "ClanBbs.h"
#include "CommCli.h"
#include "DB.h"
#include "Pledge.h"
#include "PledgeFunction.h"
#include "Protocol/src/SystemMessageIds.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cwchar>
#include <string>
#include <string_view>

namespace {

std::size_t TargetWideLength(const wchar_t* const value) { return std::wcslen(value); }

bool TargetStartsWith(const SharedUtilsWideChar* const value, const wchar_t* const prefix) {
    const std::size_t length = TargetWideLength(prefix);
    for (std::size_t index = 0; index < length; ++index) {
        if (value[index] != static_cast<SharedUtilsWideChar>(prefix[index])) {
            return false;
        }
    }
    return true;
}


std::u16string ToTargetWide(const int value) {
    const std::string digits = std::to_string(value);
    return std::u16string(digits.begin(), digits.end());
}

void AppendTarget(std::u16string& destination, const std::u16string& value) { destination.append(runtime::StringView(value)); }

template <class... Args>
void AppendFormatted(std::u16string& destination, std::u16string_view format, const Args&... arguments) {
    // The original formatted into a 0x400-unit buffer.
    std::u16string text = FormatUtf16(format, arguments...);
    if (text.size() >= 0x400) {
        text.resize(0x3ff);
    }
    destination += text;
}

std::array<char16_t, 0x81> ShortTitle(const std::array<char16_t, 0x81>& title) {
    std::array<char16_t, 0x81> result{};
    const std::size_t length = std::char_traits<char16_t>::length(title.data());
    if (length < 0x1f) {
        std::copy_n(title.data(), std::min(length + 1, result.size()), result.data());
    } else {
        std::copy_n(title.data(), 0x1d, result.data());
        result[0x1d] = u'.';
        result[0x1e] = u'.';
        result[0x1f] = u'.';
        result[0x20] = u'\0';
    }
    return result;
}

} // namespace

int CheckClanOwner(User* const user) {
    // Current L2Server 0x0059d970. This free owner differs
    // from CCommCli::CheckClanOwner in its master/dbid failure messages.
    L2_GUARD_FUNCTION();
    int pledgeId = 0;
    CPledge* const firstPledge = user->GetPledge();
    if (firstPledge != nullptr) {
        CPledge* const functionPledge = user->GetPledge();
        if (!functionPledge->GetFunction()->IsEnable(PledgeFunctionType::COMMUNITY)) {
            user->GetSocket()->SendSystemMessage(SystemMessageId::CLAN_COMMUNITY_REQUIRES_HIGHER_CLAN_LEVEL);
            commcli.RequestClanBBSInit(user);
            return 0;
        }

        CPledge* const masterPledge = user->GetPledge();
        const int masterDatabaseId = static_cast<int>(masterPledge->GetMasterDbId());
        if (static_cast<int>(user->GetDBID()) != masterDatabaseId) {
            user->GetSocket()->SendSystemMessage(SystemMessageId::ONLY_CLAN_LEADER_CAN_ACT);
            commcli.RequestClanBBSInit(user);
            return 0;
        }

        CPledge* const idPledge = user->GetPledge();
        pledgeId = static_cast<int>(idPledge->GetDbId());
        if (pledgeId != 0) {
            return pledgeId;
        }
    }

    user->GetSocket()->SendSystemMessage(SystemMessageId::NOT_IN_CLAN);
    commcli.RequestClanBBSInit(user);
    return pledgeId;
}

bool ClanBBS(CSocket* const socket, const SharedUtilsWideChar* const command) {
    // Current L2Server 0x0059db04.
    L2_GUARD_FUNCTION();
    User* const user = static_cast<CUserSocket*>(socket)->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] No user... on Html CMD Packet (CommunityBBS)",
        //     L"clanbbs.cpp",
        //     0x47)
        // Noise: An HTML command can arrive without an attached user during connection cleanup. The existing early return
        // safely ignores it. L2_LOG_ERROR(Log,
        //     L"[%s][%d] No user... on Html CMD Packet (CommunityBBS)",
        //     L"clanbbs.cpp",
        //     0x47)
        return true;
    }

    constexpr wchar_t listPrefix[] = L"_clbbslist";
    if (TargetStartsWith(command, listPrefix)) {
        int page = 0;
        int value = 0;
        std::array<SharedUtilsWideChar, 64> search{};
        ParseHTMLParam(
            command + TargetWideLength(listPrefix) + 1, const_cast<char*>("ddS"), &page, &value, 0x80, search.data()
        );
        commcli.RequestClanBBSList(user, page, value, search.data());
        return true;
    }

    constexpr wchar_t clanPrefix[] = L"_clbbsclan";
    if (TargetStartsWith(command, clanPrefix)) {
        int pledgeId = 0;
        ParseHTMLParam(command + TargetWideLength(clanPrefix) + 1, const_cast<char*>("d"), &pledgeId);
        if (pledgeId == 0) {
            CPledge* const firstPledge = user->GetPledge();
            if (firstPledge != nullptr) {
                CPledge* const idPledge = user->GetPledge();
                pledgeId = static_cast<int>(idPledge->GetDbId());
            }
            if (pledgeId == 0) {
                user->GetSocket()->SendSystemMessage(SystemMessageId::NOT_IN_CLAN);
                static constexpr SharedUtilsWideChar empty[] = {0};
                commcli.RequestClanBBSList(user, 1, 0, const_cast<SharedUtilsWideChar*>(empty));
                return true;
            }
        }
        commcli.RequestClanBBSView(user, pledgeId);
        return true;
    }

    constexpr wchar_t adminPrefix[] = L"_clbbsadmi";
    if (TargetStartsWith(command, adminPrefix)) {
        const int pledgeId = CheckClanOwner(user);
        if (pledgeId != 0) {
            commcli.RequestClanBBSAdmin(user, pledgeId);
        }
        return true;
    }

    constexpr wchar_t permissionPrefix[] = L"_clbbsper";
    if (TargetStartsWith(command, permissionPrefix)) {
        const int ownerPledgeId = CheckClanOwner(user);
        if (ownerPledgeId != 0) {
            int setting1 = 0;
            int setting2 = 0;
            int setting3 = 0;
            int setting4 = 0;
            ParseHTMLParam(
                command + TargetWideLength(permissionPrefix) + 1, const_cast<char*>("dddd"), &setting1, &setting2, &setting3,
                &setting4
            );
            CPledge* const pledge = user->GetPledge();
            commcli.RequestClanBBSSetPer(user, static_cast<int>(pledge->GetDbId()), setting1, setting2, setting3, setting4);
        }
    }
    return true;
}

CClanBBSHTML::CClanBBSHTML(CUserSocket* const socket) : CParseHTML(socket) {
    // Current L2Server 0x0059de40.
    L2_GUARD_FUNCTION();
}

CClanBBSHTML::~CClanBBSHTML() {
    // Current L2Server 0x0059dedc.
    L2_GUARD_FUNCTION();
}

void CClanBBSHTML::ClanBBSList(const std::uint8_t* packet) {
    // Current L2Server 0x0059df78.
    L2_GUARD_FUNCTION();
    std::int32_t worldId = 0;
    std::int32_t value1 = 0;
    std::int32_t value2 = 0;
    std::int32_t value3 = 0;
    std::array<char16_t, 0x80> name{};
    std::int32_t value4 = 0;
    std::int32_t dataSize = 0;
    packet = ::Disassemble(packet, "ddddSdd", &worldId, &value1, &value2, &value3, 0x100, name.data(), &value4, &dataSize);
    User* const user = m_mySocket->GetUser();
    if (user != nullptr) {
        db.RequestBBSClanList(user, worldId, value1, value2, value3, name.data(), value4, dataSize, packet);
    }
}

void CClanBBSHTML::ClanBBSList2(const std::uint8_t* packet) {
    // Current L2Server 0x0059e574.
    L2_GUARD_FUNCTION();
    std::int32_t page = 0;
    std::int32_t totalPages = 0;
    std::int32_t searchType = 0;
    std::array<char16_t, 0x80> search{};
    std::int32_t clanCount = 0;
    packet = ::Disassemble(packet, "dddSd", &page, &totalPages, &searchType, 0x100, search.data(), &clanCount);

    if (m_mySocket == nullptr) {
        return;
    }
    User* const user = m_mySocket->GetUser();
    if (user == nullptr) {
        return;
    }

    GetBodyFromFile(u"bbs_clanlist.htm", user->m_data->m_CountryCode);
    std::u16string list;
    while (clanCount > 0) {
        std::int32_t ignored = 0;
        std::int32_t pledgeId = 0;
        std::int32_t skillLevel = 0;
        std::int32_t memberCount = 0;
        std::array<char16_t, 0x40> clanName{};
        std::array<char16_t, 0x40> ownerName{};
        packet = ::Disassemble(
            packet, "ddddSS", &ignored, &pledgeId, &skillLevel, &memberCount, 0x80, clanName.data(), 0x80, ownerName.data()
        );

        StrStrMapType parameters;
        AddFormattedStringLocalMap(parameters, u"action_clanhome", u"bypass %s_%d", u"_clbbsclan", pledgeId);
        AddStringLocalMap(parameters, u"clan_name", SanitizeUtf16(clanName.data()));
        AddStringLocalMap(parameters, u"clan_owner", SanitizeUtf16(ownerName.data()));
        AddFormattedStringLocalMap(parameters, u"skill_level", u"%d", skillLevel);
        AddFormattedStringLocalMap(parameters, u"member_count", u"%d", memberCount);
        AppendTarget(list, MakeHTMLFromStrTemplet(1, parameters));
        --clanCount;
    }
    AddStringMap(u"CLAN_LIST", SanitizeUtf16(std::u16string(list)));

    const std::wstring hostSearch = Utf16ToWide(search.data());
    if (page > 1) {
        AddFormattedStringMap(
            u"ACTION_GO_LEFT", u"bypass %s_%d_%d_%s", u"_clbbslist", page - 1, searchType, hostSearch.c_str()
        );
    }

    list.clear();
    int pageNumber = page - 10 < 2 ? 1 : page - 10;
    while (pageNumber < page) {
        StrStrMapType parameters;
        AddFormattedStringLocalMap(
            parameters, u"action_link", u"bypass %s_%d_%d_%s", u"_clbbslist", pageNumber, searchType, hostSearch.c_str()
        );
        AddFormattedStringLocalMap(parameters, u"action_name", u"%d", pageNumber);
        AppendTarget(list, MakeHTMLFromStrTemplet(2, parameters));
        ++pageNumber;
    }
    AddStringMap(u"GO_LIST", SanitizeUtf16(std::u16string(list)));
    AddFormattedStringMap(u"NPAGE", u"%d", page);

    list.clear();
    pageNumber = page + 1;
    const int lastPage = std::min(totalPages, page + 10);
    while (pageNumber <= lastPage) {
        StrStrMapType parameters;
        AddFormattedStringLocalMap(
            parameters, u"action_link", u"bypass %s_%d_%d_%s", u"_clbbslist", pageNumber, searchType, hostSearch.c_str()
        );
        AddFormattedStringLocalMap(parameters, u"action_name", u"%d", pageNumber);
        AppendTarget(list, MakeHTMLFromStrTemplet(3, parameters));
        ++pageNumber;
    }
    AddStringMap(u"GO_LIST2", SanitizeUtf16(std::u16string(list)));

    const int rightPage = page + 1 <= totalPages ? page + 1 : totalPages;
    AddFormattedStringMap(
        u"ACTION_GO_RIGHT", u"bypass %s_%d_%d_%s", u"_clbbslist", rightPage, searchType, hostSearch.c_str()
    );
    AddFormattedStringMap(u"PLEDGE_ID", u"%d", user->m_data->m_nPledgeID);

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr || pledge->GetFunction() == nullptr ||
        !pledge->GetFunction()->IsEnable(PledgeFunctionType::COMMUNITY)) {
        AddFormattedStringMap(u"MY_PLEDGE_NAME", u" : %s", u"");
    } else {
        AddFormattedStringMap(u"MY_PLEDGE_NAME", u" : %s", pledge->GetPledgeName());
    }
    AddFormattedStringMap(u"BBS_C_WRITE_SEARCH", u"%d", 5);
    static_cast<void>(SendHTML(MakeHTMLFromStrTemplet(0, m_mapParam), 1, 0, 0, 0, 0, 0, L"", 1));
}

void CClanBBSHTML::ClanBBSView(const std::uint8_t* packet) {
    // Current L2Server 0x0059e124.
    L2_GUARD_FUNCTION();
    std::int32_t value1 = 0;
    std::int32_t pledgeId = 0;
    std::array<char16_t, 0x40> string1{};
    std::int32_t value3 = 0;
    std::array<char16_t, 0x40> string2{};
    std::int32_t value4 = 0;
    std::array<char16_t, 0x40> string3{};
    std::int32_t value5 = 0;
    std::int32_t value6 = 0;
    ::Disassemble(
        packet, "ddSdSdSdd", &value1, &pledgeId, 0x80, string1.data(), &value3, 0x80, string2.data(), &value4, 0x80,
        string3.data(), &value5, &value6
    );
    if (m_mySocket == nullptr) {
        return;
    }
    User* const user = m_mySocket->GetUser();
    if (user == nullptr) {
        return;
    }
    if (pledgeId < 1) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::NOT_IN_CLAN);
        commcli.RequestClanBBSInit(user);
    } else if (value5 < 2) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CLAN_COMMUNITY_REQUIRES_HIGHER_CLAN_LEVEL);
        commcli.RequestClanBBSInit(user);
    } else {
        commcli.RequestClanBBSView2(
            user, value1, pledgeId, string1.data(), value3, string2.data(), value4, string3.data(), value5, value6
        );
    }
}

void CClanBBSHTML::ClanBBSAdmin(const std::uint8_t* packet) {
    // Current L2Server 0x0059e398.
    L2_GUARD_FUNCTION();
    std::int32_t value1 = 0;
    std::int32_t value2 = 0;
    std::array<char16_t, 0x200> name{};
    std::int32_t value3 = 0;
    std::int32_t dataSize = 0;
    packet = ::Disassemble(packet, "ddSdd", &value1, &value2, 0x400, name.data(), &value3, &dataSize);
    User* const user = m_mySocket->GetUser();
    if (user != nullptr) {
        db.RequestBBSClanAdmin(user, value1, value2, name.data(), value3, dataSize, packet);
    }
}

void CClanBBSHTML::ClanBBSAdmin2(const std::uint8_t* packet) {
    // Current L2Server 0x0059ffac.
    L2_GUARD_FUNCTION();
    std::int32_t boardId = 0;
    std::int32_t pledgeId = 0;
    std::array<char16_t, 0x40> clanName{};
    std::int32_t ignored1 = 0;
    std::array<char16_t, 0x40> ownerName{};
    std::int32_t ignored2 = 0;
    std::array<char16_t, 0x40> allianceName{};
    std::array<char16_t, 0x200> clanIntro{};
    std::int32_t skillLevel = 0;
    std::int32_t memberCount = 0;
    std::int32_t permissionCount = 0;
    packet = ::Disassemble(
        packet, "ddSdSdSSddd", &boardId, &pledgeId, 0x80, clanName.data(), &ignored1, 0x80, ownerName.data(), &ignored2,
        0x80, allianceName.data(), 0x400, clanIntro.data(), &skillLevel, &memberCount, &permissionCount
    );

    User* const user = m_mySocket->GetUser();
    if (user == nullptr) {
        return;
    }

    GetBodyFromFile(u"bbs_clanadmin.htm", user->m_data->m_CountryCode);
    AddFormattedStringMap(u"MULTI_1", u"1001%c", 8);
    AddFormattedStringMap(u"PLEDGE_ID", u"%d", user->m_data->m_nPledgeID);
    AddStringMap(u"CLAN_NAME", SanitizeUtf16(clanName.data()));
    AddFormattedStringMap(u"SKILL_LEVEL", u"%d", skillLevel);
    AddFormattedStringMap(u"CLAN_MEMBERS", u"%d", memberCount);
    AddStringMap(u"OWNER_NAME", SanitizeUtf16(ownerName.data()));
    AddStringMap(u"ALLIANCE_NAME", SanitizeUtf16(allianceName.data()));
    AddFormattedStringMap(u"ACTION_ANN", u"bypass %s_%d_%d_%d", u"_bbslist", boardId, 1, 1);
    AddFormattedStringMap(u"ACTION_FREE", u"bypass %s_%d_%d_%d", u"_bbslist", boardId, 1, 2);
    AddFormattedStringMap(u"nBoardId", u"%d", -2);
    AddFormattedStringMap(u"BBS_SEND_MODE", u"%d", 4);

    CPledge* const pledge = user->GetPledge();
    if (pledge != nullptr && static_cast<int>(pledge->GetDbId()) == pledgeId && pledge->GetPledgeLevel() > 1) {
        StrStrMapType parameters;
        AddFormattedStringLocalMap(parameters, u"pledge_id", u"%d", pledgeId);
        AddStringMap(u"CLAN_ADMINLINK", SanitizeUtf16(MakeHTMLFromStrTemplet(3, parameters)));
    }

    std::u16string permissionList;
    int nextPermission = 0;
    int nextPermission2 = 0;
    int permissionPledgeId = boardId;
    while (permissionCount > 0) {
        --permissionCount;
        std::int32_t ignored = 0;
        std::int32_t boardCode = 0;
        std::array<char16_t, 0x33> boardName{};
        std::array<char16_t, 0x65> ignoredString{};
        std::int32_t permission = 0;
        std::int32_t permission2 = 0;
        packet = ::Disassemble(
            packet, "dddSSdd", &ignored, &permissionPledgeId, &boardCode, 0x66, boardName.data(), 0xca, ignoredString.data(),
            &permission, &permission2
        );

        StrStrMapType parameters;
        if (permission == 0) {
            nextPermission = 3;
            AddFormattedStringLocalMap(parameters, u"cur_per", u"&$810;");
            AddFormattedStringLocalMap(parameters, u"next_per", u"&$806;");
        } else if (permission == 3) {
            nextPermission = 31;
            AddFormattedStringLocalMap(parameters, u"cur_per", u"&$806;");
            AddFormattedStringLocalMap(parameters, u"next_per", u"&$807;");
        } else if (permission == 31) {
            nextPermission = 0;
            AddFormattedStringLocalMap(parameters, u"cur_per", u"&$807;");
            AddFormattedStringLocalMap(parameters, u"next_per", u"&$810;");
        }

        if (permission2 == 0) {
            nextPermission2 = 3;
            AddFormattedStringLocalMap(parameters, u"cur_per2", u"&$810;");
            AddFormattedStringLocalMap(parameters, u"next_per2", u"&$806;");
        } else if (permission2 == 3) {
            nextPermission2 = 31;
            AddFormattedStringLocalMap(parameters, u"cur_per2", u"&$806;");
            AddFormattedStringLocalMap(parameters, u"next_per2", u"&$807;");
        } else if (permission2 == 31) {
            nextPermission2 = 0;
            AddFormattedStringLocalMap(parameters, u"cur_per2", u"&$807;");
            AddFormattedStringLocalMap(parameters, u"next_per2", u"&$810;");
        }

        AddStringLocalMap(parameters, u"board_name", SanitizeUtf16(boardName.data()));
        AddFormattedStringLocalMap(
            parameters, u"action_next_per", u"bypass %s_%d_%d_1_%d", u"_clbbsper", permissionPledgeId, boardCode,
            nextPermission
        );
        AddFormattedStringLocalMap(
            parameters, u"action_next_per2", u"bypass %s_%d_%d_2_%d", u"_clbbsper", permissionPledgeId, boardCode,
            nextPermission2
        );
        AppendTarget(permissionList, MakeHTMLFromStrTemplet(1, parameters));
    }
    --permissionCount;
    AddStringMap(u"per_list", SanitizeUtf16(std::u16string(permissionList)));
    static_cast<void>(SendHTML(MakeHTMLFromStrTemplet(0, m_mapParam), 9, pledgeId, permissionPledgeId, 0, 0, 0, L"", 2));

    const std::wstring hostIntro = Utf16ToWide(clanIntro.data());
    std::u16string multiPacket;
    AppendFormatted(multiPacket, u"1002%c", 8);
    AppendFormatted(multiPacket, u"%d %c", 0, 8);
    AppendFormatted(multiPacket, u"%d %c", 0, 8);
    AppendFormatted(multiPacket, u"%d %c", 0, 8);
    AppendFormatted(multiPacket, u"%d %c", 0, 8);
    AppendFormatted(multiPacket, u"%d %c", 0, 8);
    AppendFormatted(multiPacket, u"%d %c", 0, 8);
    AppendFormatted(multiPacket, u"%s %c", u"", 8);
    AppendFormatted(multiPacket, u"%d %c", 8, 8);
    AppendFormatted(multiPacket, u"%s %c", u"", 8);
    AppendFormatted(multiPacket, u"%d %c", 8, 8);
    AppendFormatted(multiPacket, u"%s %c", u"", 8);
    AppendFormatted(multiPacket, u"%s %c", u"", 8);
    AppendFormatted(multiPacket, u"%s %c", hostIntro.c_str(), 8);
    AppendFormatted(multiPacket, u"%s %c", u"", 8);
    AppendFormatted(multiPacket, u"%s %c", u"", 8);
    AppendFormatted(multiPacket, u"%d %c", 0, 8);
    AppendFormatted(multiPacket, u"%d %c", 0, 8);
    AppendFormatted(multiPacket, u"%s %c", u"", 8);
    static_cast<void>(SendHTML(std::u16string(multiPacket), 9, pledgeId, permissionPledgeId, 0, 0, 0, L"", 2));
}

void CClanBBSHTML::ClanBBSView2(const std::uint8_t* packet) {
    // Current L2Server 0x0059efe0.
    L2_GUARD_FUNCTION();
    std::int32_t ignored1 = 0;
    std::int32_t pledgeId = 0;
    std::array<char16_t, 0x40> clanName{};
    std::int32_t ignored2 = 0;
    std::array<char16_t, 0x40> ownerName{};
    std::int32_t ignored3 = 0;
    std::array<char16_t, 0x40> allianceName{};
    std::int32_t skillLevel = 0;
    std::int32_t memberCount = 0;
    std::int32_t boardId = 0;
    std::array<char16_t, 0x200> clanIntro{};
    std::int32_t announcementCount = 0;
    std::int32_t threadCount = 0;
    packet = ::Disassemble(
        packet, "ddSdSdSdddSdd", &ignored1, &pledgeId, 0x80, clanName.data(), &ignored2, 0x80, ownerName.data(), &ignored3,
        0x80, allianceName.data(), &skillLevel, &memberCount, &boardId, 0x400, clanIntro.data(), &announcementCount,
        &threadCount
    );

    User* const user = m_mySocket->GetUser();
    if (user == nullptr) {
        return;
    }
    GetBodyFromFile(u"bbs_clan.htm", user->m_data->m_CountryCode);
    AddFormattedStringMap(u"PLEDGE_ID", u"%d", pledgeId);
    const std::u16string convertedIntro(ConvertStr(clanIntro.data(), 4));
    AddStringMap(u"CLAN_INTRO", convertedIntro);
    AddStringMap(u"CLAN_NAME", SanitizeUtf16(clanName.data()));
    AddFormattedStringMap(u"SKILL_LEVEL", u"%d", skillLevel);
    AddFormattedStringMap(u"CLAN_MEMBERS", u"%d", memberCount);
    AddStringMap(u"OWNER_NAME", SanitizeUtf16(ownerName.data()));
    AddStringMap(u"ALLIANCE_NAME", SanitizeUtf16(allianceName.data()));
    AddFormattedStringMap(u"ACTION_ANN", u"bypass %s_%d_%d_%d", u"_bbslist", boardId, 1, 1);
    AddFormattedStringMap(u"ACTION_FREE", u"bypass %s_%d_%d_%d", u"_bbslist", boardId, 1, 2);

    CPledge* const pledge = user->GetPledge();
    if (pledge != nullptr && static_cast<int>(pledge->GetDbId()) == pledgeId && pledge->GetPledgeLevel() > 1) {
        StrStrMapType parameters;
        const std::u16string html(MakeHTMLFromStrTemplet(3, parameters));
        AddStringMap(u"CLAN_ADMINLINK", html);
    }
    if (user->IsPledgeMaster()) {
        StrStrMapType parameters;
        AddFormattedStringLocalMap(parameters, u"pledge_id", u"%d", pledgeId);
        const std::u16string mail(MakeHTMLFromStrTemplet(4, parameters));
        AddStringMap(u"CLAN_MAILLINK", mail);
        parameters.clear();
        AddFormattedStringLocalMap(parameters, u"pledge_id", u"%d", pledgeId);
        const std::u16string announce(MakeHTMLFromStrTemplet(5, parameters));
        AddStringMap(u"CLAN_ANNOUNCELINK", announce);
    }

    std::u16string list;
    const auto appendEntry = [&](const bool announcement) {
        StrStrMapType parameters;
        std::int32_t ignoredValues[10]{};
        std::int32_t articleId = 0;
        std::array<char16_t, 0x40> writer{};
        std::array<char16_t, 0x40> ignoredString{};
        std::array<char16_t, 0x81> title{};
        time_l2_t date = 0;
        std::int32_t readCount = 0;
        std::int32_t replyCount = 0;
        std::array<char16_t, 0x40> label{};
        packet = ::Disassemble(
            packet, "ddddddSdSdSdddddS", &ignoredValues[0], &ignoredValues[1], &articleId, &ignoredValues[2],
            &ignoredValues[3], &ignoredValues[4], 0x80, writer.data(), &ignoredValues[5], 0x80, ignoredString.data(),
            &ignoredValues[6], 0x102, title.data(), &date, &ignoredValues[7], &readCount, &ignoredValues[8], &replyCount,
            0x80, label.data()
        );

        std::array<char16_t, 0x81> shortTitle = ShortTitle(title);
        const std::wstring convertedTitle = Utf16ToWide(ConvertStr(shortTitle.data(), 4));
        const std::wstring hostLabel = Utf16ToWide(label.data());
        if (announcement) {
            AddFormattedStringLocalMap(
                parameters, u"CLICK_LINK", u"<a action=\"bypass %s_%d_%d_%d_%d_%d_%d__1_1\">[%s]%s</a>", u"_bbsread",
                boardId, articleId, 0, 1, 1, 1, hostLabel.c_str(), convertedTitle.c_str()
            );
            const std::u16string hostDate(MakeFormatedDate(date, 1));
            AddStringLocalMap(parameters, u"POST_DATE", hostDate);
            AppendTarget(list, MakeHTMLFromStrTemplet(1, parameters));
            return;
        }

        // Current target inserts articleId under the aligned empty-string
        // key immediately preceding the literal "[%d]".
        parameters[std::u16string(u"")] = ToTargetWide(articleId);
        std::array<char16_t, 0x0c> replySuffix{};
        if (replyCount > 0) {
            FormatUtf16To(replySuffix.data(), 0x0b, u"[%d]", replyCount);
        }
        AddFormattedStringLocalMap(
            parameters, u"title", u"<a action=\"bypass %s_%d_%d_%d_%d_%d_%d__1_1\">[%s]%s</a> %s", u"_bbsread", boardId,
            articleId, 0, 1, 1, 2, hostLabel.c_str(), convertedTitle.c_str(), replySuffix.data()
        );
        AddStringLocalMap(parameters, u"writer", SanitizeUtf16(writer.data()));
        const std::u16string hostDate(MakeFormatedDate(date, 1));
        AddStringLocalMap(parameters, u"post_date", hostDate);
        AddFormattedStringLocalMap(parameters, u"read_count", u"%d", readCount);
        AppendTarget(list, MakeHTMLFromStrTemplet(2, parameters));
    };

    while (announcementCount > 0) {
        appendEntry(true);
        --announcementCount;
    }
    AddStringMap(u"ANN_LIST", SanitizeUtf16(std::u16string(list)));
    list.clear();
    while (threadCount > 0) {
        appendEntry(false);
        --threadCount;
    }
    AddStringMap(u"THREAD_LIST", SanitizeUtf16(std::u16string(list)));
    static_cast<void>(SendHTML(MakeHTMLFromStrTemplet(0, m_mapParam), 9, 0, 0, 0, 0, 0, L"", 1));
}
