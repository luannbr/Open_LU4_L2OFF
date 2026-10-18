#include "Memo.h"

#include "CConfig.h"
#include "CommCli.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>

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

template <std::size_t Capacity>
void ReplaceMemoSearchApostrophes(std::array<SharedUtilsWideChar, Capacity>& search) {
    int index = 0;
    while (static_cast<std::size_t>(index) < std::char_traits<SharedUtilsWideChar>::length(search.data())) {
        if (search[static_cast<std::size_t>(index)] == static_cast<SharedUtilsWideChar>(0x27)) {
            search[static_cast<std::size_t>(index)] = static_cast<SharedUtilsWideChar>(0x7f);
        }
        ++index;
    }
}


template <class... Args>
void AppendFormatted(std::u16string& destination, std::u16string_view format, const Args&... arguments) {
    // The original formatted into a 0x400-unit buffer.
    std::u16string text = FormatUtf16(format, arguments...);
    if (text.size() >= 0x400) {
        text.resize(0x3ff);
    }
    destination += text;
}

void AppendTarget(std::u16string& destination, const std::u16string& value) { destination.append(runtime::StringView(value)); }

template <std::size_t Capacity>
void AppendAtSign(std::array<char16_t, Capacity>& value) {
    const std::size_t length = std::char_traits<char16_t>::length(value.data());
    if (length + 1 < value.size()) {
        value[length] = u'@';
        value[length + 1] = u'\0';
    } else {
        value[0] = u'\0';
    }
}

} // namespace

bool MemoHTML(CSocket* const socket, const SharedUtilsWideChar* const command) {
    // Current L2Server 0x0082fac0.
    L2_GUARD_FUNCTION();
    User* const user = static_cast<CUserSocket*>(socket)->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] No user... on Html CMD Packet (MemoHTML)",
        //     L"memo.cpp",
        //     0x16)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] No user... on Html CMD Packet (MemoHTML)",
        //     L"memo.cpp",
        //     0x16)
        L2_LOG_WARN(Log, "Memo HTML command ignored: socket %p has no bound player.", static_cast<void*>(socket));
        return true;
    }

    constexpr char listPrefix[] = "_mmlist";
    if (TargetStartsWith(command, listPrefix)) {
        int page = 0;
        std::array<SharedUtilsWideChar, 0x100> search{};
        ParseHTMLParam(command + TargetAsciiLength(listPrefix) + 1, const_cast<char*>("dS"), &page, 0x200, search.data());
        ReplaceMemoSearchApostrophes(search);
        commcli.RequestGetMemoList(user, page, search.data());
    } else {
        constexpr char readPrefix[] = "_mmread";
        if (TargetStartsWith(command, readPrefix)) {
            int memoId = 0;
            int page = 0;
            std::array<SharedUtilsWideChar, 0x100> search{};
            ParseHTMLParam(
                command + TargetAsciiLength(readPrefix) + 1, const_cast<char*>("ddS"), &memoId, &page, 0x200, search.data()
            );
            ReplaceMemoSearchApostrophes(search);
            commcli.RequestGetMemo(user, memoId, page, search.data());
        } else {
            constexpr char createPrefix[] = "_mmcrea";
            if (TargetStartsWith(command, createPrefix)) {
                CMemoHTML memo(static_cast<CUserSocket*>(socket));
                memo.MemoWriteForm(4, nullptr);
            } else {
                constexpr char modifyPrefix[] = "_mmmodi";
                if (TargetStartsWith(command, modifyPrefix)) {
                    int memoId = 0;
                    ParseHTMLParam(command + TargetAsciiLength(modifyPrefix) + 1, const_cast<char*>("d"), &memoId);
                    commcli.RequestUpdateMemoForm(user, memoId);
                } else {
                    constexpr char deletePrefix[] = "_mmdele";
                    if (TargetStartsWith(command, deletePrefix)) {
                        int memoId = 0;
                        ParseHTMLParam(command + TargetAsciiLength(deletePrefix) + 1, const_cast<char*>("d"), &memoId);
                        commcli.RequestDeleteMemo(user, memoId);
                    }
                }
            }
        }
    }
    return true;
}

CMemoHTML::CMemoHTML(CUserSocket* const socket) : CParseHTML(socket) {
    // Current L2Server 0x0082e838.
    L2_GUARD_FUNCTION();
}

CMemoHTML::~CMemoHTML() {
    // Current L2Server 0x0082e8d4.
    L2_GUARD_FUNCTION();
}

void CMemoHTML::MemoRead(const std::uint8_t* packet) {
    // Current L2Server 0x0082e9a0.
    L2_GUARD_FUNCTION();
    User* const user = m_mySocket->GetUser();
    if (user == nullptr) {
        return;
    }

    std::int32_t page = 0;
    std::int32_t memoId = 0;
    std::int32_t worldId = 0;
    time_l2_t date = 0;
    std::int32_t ignoredValue1 = 0;
    std::int32_t ignoredValue2 = 0;
    std::array<char16_t, 0x100> search{};
    std::array<char16_t, 0x40> author{};
    std::array<char16_t, 0x40> ignoredString{};
    std::array<char16_t, 0x81> title{};
    std::array<char16_t, 0xfa1> text{};
    ::Disassemble(
        packet, "dSddddSdSSS", &page, 0x200, search.data(), &memoId, &worldId, &date, &ignoredValue1, 0x80, author.data(),
        &ignoredValue2, 0x80, ignoredString.data(), 0x102, title.data(), 0x1f42, text.data()
    );

    GetBodyFromFile(u"memo_read.htm", user->m_data->m_CountryCode);
    if (worldId != g_nWorldId) {
        AppendAtSign(author);
    }

    AddFormattedStringMap(u"memoid", u"%d", memoId);
    const std::u16string convertedTitle = ConvertStr(title.data(), 4);
    const std::u16string hostTitle(convertedTitle);
    AddStringMap(u"memotitle", hostTitle);

    const std::u16string convertedText = ConvertStr(text.data(), 4);
    const std::u16string hostText(convertedText);
    AddStringMap(u"memotext", hostText);

    const std::wstring hostAuthor = Utf16ToWide(author.data());
    const std::wstring hostWorldName = Utf16ToWide(GetWorldName(worldId));
    AddFormattedStringMap(u"char_name", u"%s @ %s", hostAuthor.c_str(), hostWorldName.c_str());

    const std::u16string hostDate(MakeFormatedDate(date, 2));
    AddStringMap(u"sDate", hostDate);
    const std::u16string hostSearchUtf16 = SanitizeUtf16(search.data());
    const std::wstring hostSearch = Utf16ToWide(hostSearchUtf16);
    AddStringMap(u"strsearch", hostSearchUtf16);
    AddFormattedStringMap(
        u"GOTO_LIST",
        u"<td FIXWIDTH=15></td><td><button value = \"&$422;\" action=\"bypass %s_%d_%s\" "
        u"back=\"l2ui_ct1.button.button_df_small_down\" width=70 height=25 fore=\"l2ui_ct1.button.button_df_small\" ></td>",
        u"_mmlist", page, hostSearch.c_str()
    );
    AddFormattedStringMap(
        u"MODIFY_BUTTON",
        u"<td FIXWIDTH=300></td><td><button value = \"&$424;\" action=\"bypass %s_%d\" "
        u"back=\"l2ui_ct1.button.button_df_small_down\" width=70 height=25 fore=\"l2ui_ct1.button.button_df_small\" ></td>",
        u"_mmmodi", memoId
    );
    AddFormattedStringMap(
        u"DELETE_BUTTON",
        u"<td><button value = \"&$425;\" action=\"bypass %s_%d\" back=\"l2ui_ct1.button.button_df_small_down\" width=70 "
        u"height=25 fore=\"l2ui_ct1.button.button_df_small\" ></td>",
        u"_mmdele", memoId
    );
    AddFormattedStringMap(
        u"NEW_BUTTON",
        u"<td><button value = \"&$421;\" action=\"bypass %s\" back=\"l2ui_ct1.button.button_df_small_down\" width=70 "
        u"height=25 fore=\"l2ui_ct1.button.button_df_small\" ></td>",
        u"_mmcrea"
    );

    static_cast<void>(SendHTML(MakeHTMLFromStrTemplet(0, m_mapParam), 9, -1, memoId, 0, 0, 0, L"", 1));
}

void CMemoHTML::MemoWriteForm(const int mode, const std::uint8_t* packet) {
    // Current L2Server 0x0082ef2c.
    L2_GUARD_FUNCTION();
    User* const user = m_mySocket->GetUser();
    if (user == nullptr) {
        return;
    }

    std::int32_t memoId = 0;
    std::int32_t value2 = 0;
    time_l2_t date = 0;
    std::int32_t value4 = 0;
    std::int32_t value6 = 0;
    std::array<char16_t, 0x40> author{};
    std::array<char16_t, 0x40> string2{};
    std::array<char16_t, 0x81> title{};
    std::array<char16_t, 0xfa1> text{};

    GetBodyFromFile(u"memo_form.htm", user->m_data->m_CountryCode);
    AddFormattedStringMap(u"MULTI_1", u"1001%c", 8);
    if (mode == 4) {
        AddFormattedStringMap(u"BBS_C_WRITE_WRITE", u"%d", 1);
    } else if (mode == 6) {
        AddFormattedStringMap(u"BBS_C_WRITE_WRITE", u"%d", 3);
        ::Disassemble(
            packet, "ddddSdSSS", &memoId, &value2, &date, &value4, 0x80, author.data(), &value6, 0x80, string2.data(), 0x102,
            title.data(), 0x1f42, text.data()
        );
    }

    AddStringMap(u"nBoardId", u"-1_-1");
    AddFormattedStringMap(u"memoid", u"%d", memoId);
    static_cast<void>(SendHTML(MakeHTMLFromStrTemplet(0, m_mapParam), 9, -1, memoId, 0, 0, 0, L"", 2));

    const std::wstring hostAuthor = Utf16ToWide(author.data());
    const std::wstring hostString2 = Utf16ToWide(string2.data());
    std::u16string multiPacket;
    AppendFormatted(multiPacket, u"1002%c", 8);
    AppendFormatted(multiPacket, u"%d %c", 0, 8);
    AppendFormatted(multiPacket, u"%d %c", 0, 8);
    AppendFormatted(multiPacket, u"%d %c", memoId, 8);
    AppendFormatted(multiPacket, u"%d %c", 0, 8);
    AppendFormatted(multiPacket, u"%d %c", 0, 8);
    AppendFormatted(multiPacket, u"%d %c", value4, 8);
    AppendFormatted(multiPacket, u"%s %c", hostAuthor.c_str(), 8);
    AppendFormatted(multiPacket, u"%d %c", value6, 8);
    AppendFormatted(multiPacket, u"%s %c", hostString2.c_str(), 8);
    AppendFormatted(multiPacket, u"%d %c", value2, 8);
    AppendTarget(multiPacket, ConvertStr(title.data(), 3));
    AppendFormatted(multiPacket, u" %c", 8);
    AppendTarget(multiPacket, ConvertStr(title.data(), 3));
    AppendFormatted(multiPacket, u" %c", 8);
    if (mode == 6) {
        AppendTarget(multiPacket, ConvertStr(text.data(), 3));
    }
    AppendFormatted(multiPacket, u" %c", 8);

    const std::wstring hostDate = Utf16ToWide(MakeFormatedDate(date, 2));
    AppendFormatted(multiPacket, u"%s %c", hostDate.c_str(), 8);
    AppendFormatted(multiPacket, u"%s %c", hostDate.c_str(), 8);
    AppendFormatted(multiPacket, u"%d %c", 0, 8);
    AppendFormatted(multiPacket, u"%d %c", 0, 8);
    AppendFormatted(multiPacket, u"%s %c", u"", 8);

    if (mode == 4 || mode == 6) {
        static_cast<void>(SendHTML(std::u16string(multiPacket), 9, -1, memoId, 0, 0, 0, L"", 2));
    }
}

void CMemoHTML::MemoModifyForm(int, const std::uint8_t* const packet) {
    // Current L2Server 0x0082fa00 ignores the first argument.
    L2_GUARD_FUNCTION();
    if (m_mySocket->GetUser() != nullptr) {
        MemoWriteForm(6, packet);
    }
}

void CMemoHTML::MemoList(const std::uint8_t* packet) {
    // Current L2Server 0x0082fe84.
    L2_GUARD_FUNCTION();
    User* const user = m_mySocket->GetUser();
    if (user == nullptr) {
        return;
    }

    GetBodyFromFile(u"memo_list.htm", user->m_data->m_CountryCode);

    std::int32_t pageCount = 0;
    std::int32_t page = 0;
    std::int32_t recordCount = 0;
    std::array<char16_t, 0x100> search{};
    packet = ::Disassemble(packet, "ddSd", &pageCount, &page, 0x200, search.data(), &recordCount);
    if (page < 1) {
        page = 1;
    }

    const std::wstring hostSearch = Utf16ToWide(search.data());
    std::u16string listHtml;
    std::array<char16_t, 0x40> author{};
    std::array<char16_t, 0x40> ignoredString{};
    std::array<char16_t, 0x81> subject{};
    while (recordCount > 0) {
        StrStrMapType parameters;
        std::int32_t memoId = 0;
        std::int32_t worldId = 0;
        time_l2_t date = 0;
        std::int32_t ignoredValue1 = 0;
        std::int32_t ignoredValue2 = 0;
        packet = ::Disassemble(
            packet, "ddddSdSS", &memoId, &worldId, &date, &ignoredValue1, 0x80, author.data(), &ignoredValue2, 0x80,
            ignoredString.data(), 0x102, subject.data()
        );

        AddFormattedStringLocalMap(parameters, u"world_no", u"%d", worldId);
        AddFormattedStringLocalMap(parameters, u"memo_id", u"%d", memoId);
        AddFormattedStringLocalMap(
            parameters, u"action", u"bypass %s_%d_%d_%s", u"_mmread", memoId, page, hostSearch.c_str()
        );

        const std::u16string hostSubject(ConvertStr(subject.data(), 4));
        AddStringLocalMap(parameters, u"subject", hostSubject);
        const std::wstring hostAuthor = Utf16ToWide(author.data());
        const std::wstring hostWorldName = Utf16ToWide(GetWorldName(worldId));
        AddFormattedStringLocalMap(parameters, u"char_name", u"%s<br1>@%s", hostAuthor.c_str(), hostWorldName.c_str());
        const std::u16string hostDate(MakeFormatedDate(date, 1));
        AddStringLocalMap(parameters, u"post", hostDate);
        listHtml += MakeHTMLFromStrTemplet(1, parameters);

        --recordCount;
        author.fill(u'\0');
        subject.fill(u'\0');
    }

    const std::u16string hostList(listHtml);
    AddStringMap(u"MEMO_LIST", hostList);
    if (page > 1) {
        AddFormattedStringMap(u"ACTION_GO_LEFT", u"bypass %s_%d_%s", u"_mmlist", page - 1, hostSearch.c_str());
    }

    listHtml.assign(u"");
    for (int index = std::max(page - 10, 1); index < page; ++index) {
        StrStrMapType parameters;
        AddFormattedStringLocalMap(parameters, u"action_link", u"bypass %s_%d_%s", u"_mmlist", index, hostSearch.c_str());
        AddFormattedStringLocalMap(parameters, u"action_name", u"%d", index);
        listHtml += MakeHTMLFromStrTemplet(4, parameters);
    }
    const std::u16string hostPreviousPages(listHtml);
    AddStringMap(u"GO_LIST", hostPreviousPages);

    listHtml.assign(u"");
    AddFormattedStringMap(u"NPAGE", u"%d", page);
    for (int index = page + 1; index <= std::min(page + 10, pageCount); ++index) {
        StrStrMapType parameters;
        AddFormattedStringLocalMap(parameters, u"action_link", u"bypass %s_%d_%s", u"_mmlist", index, hostSearch.c_str());
        AddFormattedStringLocalMap(parameters, u"action_name", u"%d", index);
        listHtml += MakeHTMLFromStrTemplet(5, parameters);
    }
    const std::u16string hostNextPages(listHtml);
    AddStringMap(u"GO_LIST2", hostNextPages);

    listHtml.assign(u"");
    AddFormattedStringMap(u"ACTION_GO_RIGHT", u"bypass %s_%d_%s", u"_mmlist", page + 1, hostSearch.c_str());
    AddFormattedStringMap(u"ACTION_WRITEBUTTON", u"bypass %s", u"_mmcrea");
    AddFormattedStringMap(u"BBS_C_WRITE_SEARCH", u"%d", 5);
    static_cast<void>(SendHTML(MakeHTMLFromStrTemplet(0, m_mapParam), 9, 0, 0, 0, 0, 0, L"", 1));
}
