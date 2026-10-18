#include "Community.h"

#include "CConfig.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Config.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cwchar>
#include <string>
#include <string_view>

namespace {

void AppendTarget(std::u16string& destination, const std::u16string& value) { destination.append(runtime::StringView(value)); }

template <class... Args>
void AppendFormatted(std::u16string& destination, std::u16string_view format, const Args&... arguments) {
    // The original formatted into a 0x5000-unit buffer.
    std::u16string text = FormatUtf16(format, arguments...);
    if (text.size() >= 0x5000) {
        text.resize(0x4fff);
    }
    destination += text;
}

template <class... Args>
void AppendReadTreeFormatted(std::u16string& destination, std::u16string_view format, const Args&... arguments) {
    // The original formatted into a 0x100-unit buffer.
    std::u16string text = FormatUtf16(format, arguments...);
    if (text.size() >= 0x100) {
        text.resize(0xff);
    }
    destination += text;
}

template <std::size_t Capacity>
std::array<char16_t, Capacity> TruncateSubject(const char16_t* const prefix, const char16_t* const subject) {
    std::array<char16_t, Capacity> full{};
    const std::u16string cleanPrefix = SanitizeUtf16(prefix);
    const std::u16string cleanSubject = SanitizeUtf16(subject);
    const std::u16string combined = cleanPrefix.empty() ? cleanSubject : u"[" + cleanPrefix + u"]" + cleanSubject;
    const std::size_t sourceLength = combined.size();
    if (sourceLength < 0x1a) {
        std::copy_n(combined.data(), std::min(sourceLength, Capacity - 1), full.data());
    } else {
        std::copy_n(combined.data(), std::min<std::size_t>(0x18, Capacity - 1), full.data());
        if (Capacity > 0x1b) {
            full[0x18] = u'.';
            full[0x19] = u'.';
            full[0x1a] = u'.';
        }
    }
    return full;
}

} // namespace

CCommunity::CCommunity(CUserSocket* const socket, const int communityType) :
    CParseHTML(socket), m_nCommunityType(communityType), m_nBoard(0), m_mySocket(socket) {
    // Current L2Server 0x005be09c.
    L2_GUARD_FUNCTION();
}

CCommunity::~CCommunity() = default;

int CCommunity::GetLinkPage() {
    // Current L2Server 0x005be160.
    L2_GUARD_FUNCTION();
    User* const user = m_mySocket != nullptr ? m_mySocket->GetUser() : nullptr;
    if (user == nullptr) {
        return 0;
    }
    GetBodyFromFile(u"bbs_link.htm", user->m_data->m_CountryCode);
    AddStringMap(u"icon_name", u"l2ui.ActionWnd.bbs_Webfolder");
    static_cast<void>(SendHTML(MakeHTMLFromStrTemplet(0, m_mapParam), 0xb, 0, 0, 0, 0, 0, L"", 2));
    return 1;
}

const std::uint8_t* CCommunity::ReadTree(const std::uint8_t* packet, std::u16string& tree, int& treeValue) {
    // Current L2Server 0x005be3c4.
    L2_GUARD_FUNCTION();
    std::int32_t count = 0;
    packet = ::Disassemble(packet, "d", &count);

    std::u16string prefix;
    std::u16string links;
    for (int index = 0; index < count; ++index) {
        std::int32_t ignored = 0;
        std::int32_t boardId = 0;
        std::int32_t boardType = 0;
        std::int32_t parentId = 0;
        std::int32_t decodedTreeValue = 0;
        std::array<char16_t, 50> name{};
        packet =
            ::Disassemble(packet, "ddSddd", &ignored, &boardId, 100, name.data(), &boardType, &parentId, &decodedTreeValue);
        treeValue = decodedTreeValue;
        const std::wstring hostName = Utf16ToWide(name.data());

        if (parentId > 0) {
            if (index == 0) {
                prefix += u"<br1><br1><table border=0 cellspacing=0 cellpadding=0><tr><td FIXWIDTH=15>&nbsp;</td><td "
                          u"width=745 height=30 align=left>";
                AppendReadTreeFormatted(prefix, u"<a action=\"bypass %s_0\"> HOME </a>", u"_bbsinit");
            }
            AppendReadTreeFormatted(prefix, u"&nbsp;&gt; <a action=\"bypass %s_1_0_\"> &$809; </a>", u"_clbbslist");
            AppendReadTreeFormatted(
                prefix, u"&nbsp;&gt; <a action=\"bypass %s_%d\"> &$802; </a>", u"_clbbsclan", decodedTreeValue
            );
        } else if (index == 0) {
            prefix += u"<br1><br1><table border=0 cellspacing=0 cellpadding=0><tr><td FIXWIDTH=15>&nbsp;</td><td width=745 "
                      u"height=30 align=left>";
            AppendReadTreeFormatted(prefix, u"<a action=\"bypass %s_0\"> HOME </a>", u"_bbsinit");
        }

        if (boardType == 4 || boardType == 16 || boardType == 32) {
            AppendReadTreeFormatted(
                links, u"&nbsp;&gt; <a action=\"bypass %s_%d_1_%d\"> %s </a>", u"_bbslist", boardId, parentId,
                hostName.c_str()
            );
        } else {
            AppendReadTreeFormatted(
                links, u"&nbsp;&gt; <a action=\"bypass %s_%d_1\"> %s </a>", u"_bbsinit", boardId, hostName.c_str()
            );
        }
    }
    prefix += links;
    if (prefix.empty()) {
        prefix += u"<br1><table border=0 cellspacing=0 cellpadding=0><tr><td width=50></td><td align=center width=695 "
                  u"height=30 align=left><img src=\"l2ui.bbs_lineage2\" width=128 height=16 ></td></tr></table>";
    } else {
        prefix += u"</td></tr></table>";
    }
    tree = std::u16string(prefix);
    return packet;
}

int CCommunity::ReadArticle(
    const int boardId, const int boardType, SharedUtilsWideChar* const search, const std::uint8_t* packet
) {
    // Current L2Server 0x005be9f4.
    L2_GUARD_FUNCTION();
    User* const user = m_mySocket != nullptr ? m_mySocket->GetUser() : nullptr;
    if (user == nullptr) {
        return 1;
    }

    GetBodyFromFile(u"bbs_readarticle.htm", user->m_data->m_CountryCode);
    AddFormattedStringMap(u"MULTI_1", u"1001%c", 8);

    std::u16string tree;
    int treePledgeId = 0;
    packet = ReadTree(packet, tree, treePledgeId);
    AddStringMap(u"TREE", SanitizeUtf16(tree));

    std::int32_t ignored1 = 0;
    std::int32_t ignored2 = 0;
    std::int32_t articleId = 0;
    std::int32_t ignored4 = 0;
    std::int32_t ignored5 = 0;
    std::int32_t writerServerId = 0;
    std::array<char16_t, 0x32> writerName{};
    std::int32_t ignored7 = 0;
    std::array<char16_t, 0x32> secondaryName{};
    std::int32_t writerWorldId = 0;
    std::array<char16_t, 0x81> title{};
    std::array<char16_t, 0xbb9> content{};
    time_l2_t postedAt = 0;
    time_l2_t modifiedAt = 0;
    std::int32_t readCount = 0;
    std::int32_t ignored16 = 0;
    std::array<char16_t, 0x32> category{};
    std::int32_t permissions = 0;
    std::array<char16_t, 0x15> titlePrefix{};
    std::int32_t displayArticleId = 0;
    std::int32_t listMode = 0;
    std::int32_t listPage = 0;
    std::int32_t searchType = 0;
    std::array<char16_t, 0x15> packetSearch{};
    std::int32_t searchStart = 0;
    std::int32_t searchEnd = 0;
    ::Disassemble(
        packet, "ddddddSdSdSSddddSdSddddSdd", &ignored1, &ignored2, &articleId, &ignored4, &ignored5, &writerServerId, 0x64,
        writerName.data(), &ignored7, 0x64, secondaryName.data(), &writerWorldId, 0x102, title.data(), 0x1772,
        content.data(), &postedAt, &modifiedAt, &readCount, &ignored16, 0x64, category.data(), &permissions, 0x2a,
        titlePrefix.data(), &displayArticleId, &listMode, &listPage, &searchType, 0x2a, packetSearch.data(), &searchStart,
        &searchEnd
    );

    if (treePledgeId > 0 && boardType > 0 && treePledgeId == static_cast<int>(user->m_data->m_nPledgeID) &&
        user->IsPledgeMaster()) {
        permissions = 0x3f;
    }
    if (writerServerId == static_cast<int>(user->GetServerId()) && writerWorldId == g_nWorldId) {
        permissions = 0x7f;
    }

    if (displayArticleId <= 0) {
        displayArticleId = articleId;
    }
    AddFormattedStringMap(u"article_id", u"%d", displayArticleId);
    AddFormattedStringMap(u"list_page", u"%d", listPage);
    AddFormattedStringMap(u"board_id", u"%d", boardId);
    const std::u16string convertedTitle = SanitizeUtf16(ConvertStr(title.data(), 4));
    if (titlePrefix[0] == u'\0') {
        AddStringMap(u"title", convertedTitle);
    } else {
        AddFormattedStringMap(u"title", u"[%s] %s", titlePrefix.data(), convertedTitle);
    }
    AddStringMap(u"char_name", SanitizeUtf16(writerName.data()));
    if (writerWorldId != g_nWorldId) {
        AddFormattedStringMap(u"server_name", u"@%s", GetWorldName(writerWorldId));
    }
    AddStringMap(u"sDate", SanitizeUtf16(MakeFormatedDate(postedAt, 2)));
    AddFormattedStringMap(u"read_count", u"%d", readCount);
    if ((permissions & 0x10) != 0 && user->m_data->m_nLevel >= g_nBBSWriteLevel) {
        AddFormattedStringMap(
            u"WRITE_COMMENT_FORM",
            u"<table border=0><tr><td FIXWIDTH=35>&$423;</td><td><edit var = \"Comment\" width=412 "
            u"height=15></td><td><button value=\"&$140;\" action=\"Write %d %d_%d %d Comment '' ''\" "
            u"back=\"l2ui_ct1.button.button_df_small_down\" width=70 height=25 "
            u"fore=\"l2ui_ct1.button.button_df_small\"></td></tr></table>",
            4, boardId, boardType, articleId
        );
    }
    AddFormattedStringMap(u"board_type", u"%d", boardType);
    if (listMode == 1) {
        AddFormattedStringMap(
            u"GOTO_LIST",
            u"<button value = \"&$422;\" action=\"bypass %s_%d_%d_%d\" back=\"l2ui_ct1.button.button_df_small_down\" "
            u"width=70 height=25 fore=\"l2ui_ct1.button.button_df_small\" > ",
            u"_bbslist", boardId, listPage, boardType
        );
    } else if (listMode == 2) {
        AddFormattedStringMap(
            u"GOTO_LIST",
            u"<button value = \"&$422;\" action=\"bypass %s_%d_%d_%d_%d_%s\" back=\"l2ui_ct1.button.button_df_small_down\" "
            u"width=70 height=25 fore=\"l2ui_ct1.button.button_df_small\" > ",
            u"_bbssearch", boardId, listPage, searchType, boardType, packetSearch.data()
        );
    }

    if ((permissions & 0x40) == 0) {
        AddStringMap(u"MODIFY_BUTTON", u"<td FIXWIDTH=350></td><td></td>");
    } else {
        AddFormattedStringMap(
            u"MODIFY_BUTTON",
            u"<td FIXWIDTH=300></td><td><button value = \"&$424;\" action=\"bypass %s_%d_%d_%d\" "
            u"back=\"l2ui_ct1.button.button_df_small_down\" width=70 height=25 fore=\"l2ui_ct1.button.button_df_small\" "
            u"></td>",
            u"_bbsmodify", boardId, articleId, boardType
        );
    }
    if ((permissions & 0x20) == 0) {
        AddStringMap(u"DELETE_BUTTON", u"<td FIXWIDTH=50></td>");
    } else {
        AddFormattedStringMap(
            u"DELETE_BUTTON",
            u"<td><button value = \"&$425;\" action=\"bypass %s_%d_%d_0_1_%d\" "
            u"back=\"l2ui_ct1.button.button_df_small_down\" width=70 height=25 fore=\"l2ui_ct1.button.button_df_small\" "
            u"></td>",
            u"_bbsdelete", boardId, articleId, boardType
        );
    }
    if ((permissions & 8) == 0 || user->m_data->m_nLevel < g_nBBSWriteLevel) {
        AddFormattedStringMap(
            u"REPLY_BUTTON",
            u"<td><button value = \"&$426;\" action=\"bypass %s_%d_%d_%d_%d_%d_%d_%s_%d_%d\" "
            u"back=\"l2ui_ct1.button.button_df_small_down\" width=70 height=25 fore=\"l2ui_ct1.button.button_df_small\" "
            u"></td>",
            u"_bbser1", boardId, articleId, searchType, listMode, listPage, boardType, packetSearch.data(), searchStart,
            searchEnd
        );
    } else {
        AddFormattedStringMap(
            u"REPLY_BUTTON",
            u"<td><button value = \"&$426;\" action=\"bypass %s_%d_%d_%d\" back=\"l2ui_ct1.button.button_df_small_down\" "
            u"width=70 height=25 fore=\"l2ui_ct1.button.button_df_small\" ></td>",
            u"_bbsreplyform", boardId, articleId, boardType
        );
    }
    if ((permissions & 4) == 0 || user->m_data->m_nLevel < g_nBBSWriteLevel) {
        AddFormattedStringMap(
            u"NEW_BUTTON",
            u"<td><button value = \"&$421;\" action=\"bypass %s_%d_%d_%d_%d_%d_%d_%s_%d_%d\" "
            u"back=\"l2ui_ct1.button.button_df_small_down\" width=70 height=25 fore=\"l2ui_ct1.button.button_df_small\" "
            u"></td>",
            u"_bbser1", boardId, articleId, searchType, listMode, listPage, boardType, packetSearch.data(), searchStart,
            searchEnd
        );
    } else {
        AddFormattedStringMap(
            u"NEW_BUTTON",
            u"<td><button value = \"&$421;\" action=\"bypass %s_%d_%d\" back=\"l2ui_ct1.button.button_df_small_down\" "
            u"width=70 height=25 fore=\"l2ui_ct1.button.button_df_small\" ></td>",
            u"_bbswriteform", boardId, boardType
        );
    }

    const int htmlType = boardType > 0 ? 9 : 3;
    const std::wstring hostSearch = Utf16ToWide(search, Utf16WidePolicy::PreserveInvalidOnWindows);
    static_cast<void>(SendHTML(
        MakeHTMLFromStrTemplet(0, m_mapParam), htmlType, boardId, articleId, searchType, listMode, listPage,
        hostSearch.c_str(), 2
    ));

    std::u16string multi;
    AppendFormatted(multi, u"2%c", 8);
    AppendFormatted(multi, u"%d %c", ignored1, 8);
    AppendFormatted(multi, u"%d %c", ignored2, 8);
    AppendFormatted(multi, u"%d %c", articleId, 8);
    AppendFormatted(multi, u"%d %c", ignored4, 8);
    AppendFormatted(multi, u"%d %c", ignored5, 8);
    AppendFormatted(multi, u"%d %c", writerServerId, 8);
    AppendFormatted(multi, u"%s %c", writerName.data(), 8);
    AppendFormatted(multi, u"%d %c", ignored7, 8);
    AppendFormatted(multi, u"%s %c", secondaryName.data(), 8);
    AppendFormatted(multi, u"%d %c", writerWorldId, 8);
    AppendTarget(multi, ConvertStr(title.data(), 4));
    AppendFormatted(multi, u" %c", 8);
    AppendTarget(multi, ConvertStr(content.data(), 4));
    AppendFormatted(multi, u" %c", 8);
    AppendFormatted(multi, u"%s %c", MakeFormatedDate(postedAt, 2), 8);
    AppendFormatted(multi, u"%s %c", MakeFormatedDate(modifiedAt, 2), 8);
    AppendFormatted(multi, u"%d %c", readCount, 8);
    AppendFormatted(multi, u"%d %c", ignored16, 8);
    AppendFormatted(multi, u"%s %c", category.data(), 8);
    static_cast<void>(
        SendHTML(std::u16string(multi), htmlType, boardId, articleId, searchType, listMode, listPage, hostSearch.c_str(), 2)
    );
    return 1;
}

const std::uint8_t* CCommunity::WriteFormFrame(
    const int boardId,
    const int articleId,
    const int boardType,
    const int mode,
    SharedUtilsWideChar* const search,
    const std::uint8_t* packet
) {
    // Current L2Server 0x005bfb60.
    L2_GUARD_FUNCTION();
    User* const user = m_mySocket != nullptr ? m_mySocket->GetUser() : nullptr;
    if (user == nullptr) {
        return packet;
    }

    GetBodyFromFile(u"bbs_writeform.htm", user->m_data->m_CountryCode);
    AddFormattedStringMap(u"MULTI_1", u"1001%c", 8);

    std::u16string tree;
    int treeValue = 0;
    packet = ReadTree(packet, tree, treeValue);
    AddStringMap(u"TREE", SanitizeUtf16(tree));

    std::int32_t comboCount = 0;
    packet = ::Disassemble(packet, "d", &comboCount);
    std::wstring combo;
    for (int index = 0; index < comboCount; ++index) {
        if (combo.size() > 0x1ea) {
            break;
        }
        std::array<char16_t, 0x14> entry{};
        packet = ::Disassemble(packet, "S", 0x28, entry.data());
        if (index > 0) {
            combo.push_back(L';');
        }
        combo += Utf16ToWide(entry.data());
    }
    AddStringMap(u"sCombo", WideToUtf16(combo.c_str()));

    std::int32_t value1 = 0;
    std::int32_t value2 = 0;
    std::int32_t packetArticleId = 0;
    std::int32_t value4 = 0;
    std::int32_t value5 = 0;
    std::int32_t value6 = 0;
    std::array<char16_t, 0x32> author{};
    std::int32_t value8 = 0;
    std::array<char16_t, 0x32> secondary{};
    std::int32_t value10 = 0;
    std::array<char16_t, 0x80> title{};
    std::array<char16_t, 0xbb9> content{};
    time_l2_t postedAt = 0;
    time_l2_t modifiedAt = 0;
    std::int32_t value15 = 0;
    std::int32_t value16 = 0;
    std::array<char16_t, 0x32> category{};
    std::array<char16_t, 0x15> titlePrefix{};

    if (mode == 4) {
        AddFormattedStringMap(u"BBS_C_WRITE_WRITE", u"%d", 1);
    } else if (mode == 5) {
        AddFormattedStringMap(u"BBS_C_WRITE_WRITE", u"%d", 2);
        packet = ::Disassemble(
            packet, "ddddddSdSdSSddddS", &value1, &value2, &packetArticleId, &value4, &value5, &value6, 0x64, author.data(),
            &value8, 0x64, secondary.data(), &value10, 0x100, title.data(), 0x1772, content.data(), &postedAt, &modifiedAt,
            &value15, &value16, 0x64, category.data()
        );
    } else if (mode == 6) {
        AddFormattedStringMap(u"BBS_C_WRITE_WRITE", u"%d", 3);
        packet = ::Disassemble(
            packet, "ddddddSdSdSSddddSS", &value1, &value2, &packetArticleId, &value4, &value5, &value6, 0x64, author.data(),
            &value8, 0x64, secondary.data(), &value10, 0x100, title.data(), 0x1772, content.data(), &postedAt, &modifiedAt,
            &value15, &value16, 0x64, category.data(), 0x2a, titlePrefix.data()
        );
    }

    AddFormattedStringMap(u"nBoardId", u"%d_%d", boardId, boardType);
    AddFormattedStringMap(u"article_id", u"%d", packetArticleId);
    AddFormattedStringMap(u"board_id", u"%d", boardId);
    AddFormattedStringMap(u"board_type", u"%d", boardType);

    const int htmlType = boardType > 0 ? 9 : mode;
    const std::wstring hostSearch = Utf16ToWide(search, Utf16WidePolicy::PreserveInvalidOnWindows);
    static_cast<void>(SendHTML(MakeHTMLFromStrTemplet(0, m_mapParam), htmlType, boardId, articleId, 0, 0, 0, L"", 2));

    std::u16string multi;
    AppendFormatted(multi, u"1002%c", 8);
    AppendFormatted(multi, u"%d %c", value1, 8);
    AppendFormatted(multi, u"%d %c", value2, 8);
    AppendFormatted(multi, u"%d %c", packetArticleId, 8);
    AppendFormatted(multi, u"%d %c", value4, 8);
    AppendFormatted(multi, u"%d %c", value5, 8);
    AppendFormatted(multi, u"%d %c", value6, 8);
    AppendFormatted(multi, u"%s %c", author.data(), 8);
    AppendFormatted(multi, u"%d %c", value8, 8);
    AppendFormatted(multi, u"%s %c", secondary.data(), 8);
    AppendFormatted(multi, u"%d %c", value10, 8);
    AppendTarget(multi, ConvertStr(title.data(), 3));
    AppendFormatted(multi, u" %c", 8);
    if (mode == 5) {
        AppendTarget(multi, std::u16string(u"RE:"));
    }
    AppendTarget(multi, ConvertStr(title.data(), 3));
    AppendFormatted(multi, u" %c", 8);
    if (mode == 6) {
        AppendTarget(multi, ConvertStr(content.data(), 3));
    }
    AppendFormatted(multi, u" %c", 8);
    AppendFormatted(multi, u"%s %c", MakeFormatedDate(postedAt, 2), 8);
    AppendFormatted(multi, u"%s %c", MakeFormatedDate(modifiedAt, 2), 8);
    AppendFormatted(multi, u"%d %c", value15, 8);
    AppendFormatted(multi, u"%d %c", value16, 8);
    AppendFormatted(multi, u"%s %c", category.data(), 8);

    if (mode == 4 || mode == 5 || mode == 6) {
        static_cast<void>(SendHTML(
            std::u16string(multi), htmlType, boardId, mode == 4 ? articleId : packetArticleId, 0, 0, 0, hostSearch.c_str(), 2
        ));
    }
    return packet;
}

int CCommunity::WriteForm(
    const int boardId, const int boardType, SharedUtilsWideChar* const search, const std::uint8_t* const packet
) {
    // Current L2Server 0x005c0bfc.
    L2_GUARD_FUNCTION();
    if (m_mySocket != nullptr && m_mySocket->GetUser() != nullptr) {
        static_cast<void>(WriteFormFrame(boardId, 0, boardType, 4, search, packet));
    }
    return 1;
}

int CCommunity::WriteReplyForm(
    const int boardId, const int boardType, SharedUtilsWideChar* const search, const std::uint8_t* const packet
) {
    // Current L2Server 0x005c0cec.
    L2_GUARD_FUNCTION();
    if (m_mySocket != nullptr && m_mySocket->GetUser() != nullptr) {
        static_cast<void>(WriteFormFrame(boardId, 0, boardType, 5, search, packet));
    }
    return 1;
}

int CCommunity::WriteModifyForm(
    const int boardId, const int boardType, SharedUtilsWideChar* const search, const std::uint8_t* const packet
) {
    // Current L2Server 0x005c0ddc.
    L2_GUARD_FUNCTION();
    if (m_mySocket != nullptr && m_mySocket->GetUser() != nullptr) {
        static_cast<void>(WriteFormFrame(boardId, 0, boardType, 6, search, packet));
    }
    return 0;
}

int CCommunity::GetAnnounce(const std::uint8_t* packet) {
    // Current L2Server 0x005c0ec8.
    L2_GUARD_FUNCTION();
    User* const user = m_mySocket != nullptr ? m_mySocket->GetUser() : nullptr;
    if (user == nullptr) {
        return 0;
    }

    GetBodyFromFile(u"bbs_getannounce.htm", user->m_data->m_CountryCode);
    std::u16string tree;
    int treeValue = 0;
    packet = ReadTree(packet, tree, treeValue);
    AddStringMap(u"TREE", SanitizeUtf16(tree));

    std::int32_t boardId = 0;
    std::array<char16_t, 0x32> boardName{};
    std::int32_t boardType = 0;
    time_l2_t postedAt = 0;
    std::array<char16_t, 0x81> title{};
    std::array<char16_t, 0xbb9> content{};
    ::Disassemble(
        packet, "dSddSS", &boardId, 0x64, boardName.data(), &boardType, &postedAt, 0x102, title.data(), 0x1772,
        content.data()
    );

    AddStringMap(u"title", SanitizeUtf16(ConvertStr(title.data(), 4)));
    AddStringMap(u"content", SanitizeUtf16(ConvertStr(content.data(), 4)));
    AddStringMap(u"sDate", SanitizeUtf16(MakeFormatedDate(postedAt, 2)));
    AddFormattedStringMap(u"BOARD_ID", u"%d", boardId);
    AddStringMap(u"BOARD_NAME", SanitizeUtf16(boardName.data()));
    static_cast<void>(SendHTML(MakeHTMLFromStrTemplet(0, m_mapParam), 8, boardType, 0, 0, 0, 0, L"", 1));
    return 1;
}

int CCommunity::GetBoardList(const std::uint8_t* packet) {
    // Current L2Server 0x005c139c.
    L2_GUARD_FUNCTION();
    User* const user = m_mySocket != nullptr ? m_mySocket->GetUser() : nullptr;
    if (user == nullptr) {
        return 1;
    }

    std::u16string tree;
    int treeValue = 0;
    packet = ReadTree(packet, tree, treeValue);
    GetBodyFromFile(u"bbs_boardlist.htm", user->m_data->m_CountryCode);
    AddStringMap(u"TREE", SanitizeUtf16(tree));

    std::int32_t count = 0;
    packet = ::Disassemble(packet, "d", &count);
    std::u16string rows;
    for (int index = 0; index < count; ++index) {
        std::int32_t recordId = 0;
        std::int32_t boardId = 0;
        std::array<char16_t, 0x32> administrator{};
        std::int32_t ignored1 = 0;
        std::int32_t ignored2 = 0;
        std::int32_t ignored3 = 0;
        std::array<char16_t, 0x32> boardName{};
        std::array<char16_t, 0x32> description{};
        std::int32_t boardType = 0;
        std::int32_t flags = 0;
        std::int32_t articleCount = 0;
        std::int32_t ignored4 = 0;
        std::int32_t displayCount = 0;
        std::int32_t showOverride = 0;
        packet = ::Disassemble(
            packet, "ddSdddSSdddddd", &recordId, &boardId, 0x64, administrator.data(), &ignored1, &ignored2, &ignored3, 0x64,
            boardName.data(), 0x64, description.data(), &boardType, &flags, &articleCount, &ignored4, &displayCount,
            &showOverride
        );

        bool skip = false;
        if (g_L2ServerType == L2ServerType::DEV) {
            skip = boardId == 0x4bc;
        } else if (g_L2ServerType == L2ServerType::TEST) {
            skip = boardId == 0x62;
        } else if (g_L2ServerType == L2ServerType::LIVE) {
            skip = boardId == 0x583;
        }
        if (skip || (!g_bShowSharedWritableBBS && showOverride == 0 && (flags & 4) != 0)) {
            continue;
        }

        articleCount = std::max(articleCount, 0);
        displayCount = std::max(displayCount, 0);
        StrStrMapType parameters;
        if (boardType == 4 || boardType == 0x10 || boardType == 0x20) {
            AddStringLocalMap(parameters, u"icon_name", u"l2ui.bbs_board");
            const char16_t* action = u"_bbslist_%d_1";
            if (boardType == 0x10) {
                action = u"_bbsssqdawnlist_%d_1";
            } else if (boardType == 0x20) {
                action = u"_bbsssqtwilightlist_%d_1";
            }
            AddFormattedStringLocalMap(parameters, u"action_click", action, boardId);
            AddFormattedStringLocalMap(parameters, u"action_name", u"%s (%d)", boardName.data(), displayCount);
        } else {
            AddStringLocalMap(parameters, u"icon_name", u"l2ui.bbs_folder");
            if (boardType == 1) {
                AddStringLocalMap(parameters, u"action_click", u"_bbsgetfav");
            } else if (boardType == 3) {
                AddFormattedStringLocalMap(parameters, u"action_click", u"%s_%d_%d_%s", u"_clbbslist", 1, 0, u"");
            } else if (boardType == -1) {
                AddStringLocalMap(parameters, u"action_click", u"_announcecastlelist");
                user->m_nCommLocalCommunityBid = recordId;
            } else {
                AddFormattedStringLocalMap(parameters, u"action_click", u"_bbsinit_%d", boardId);
            }
            AddStringLocalMap(parameters, u"action_name", SanitizeUtf16(boardName.data()));
        }
        AddStringLocalMap(parameters, u"description", SanitizeUtf16(description.data()));
        AddFormattedStringLocalMap(parameters, u"articles", u"%d", articleCount);
        AddStringLocalMap(parameters, u"admin", SanitizeUtf16(administrator.data()));
        AppendTarget(rows, MakeHTMLFromStrTemplet(1, parameters));
    }

    AddStringMap(u"BOARD_LIST", SanitizeUtf16(std::u16string(rows)));
    static_cast<void>(SendHTML(MakeHTMLFromStrTemplet(0, m_mapParam), 1, 0, 0, 0, 0, 0, L"", 1));
    return 1;
}

int CCommunity::GetArticleList(
    const int boardId,
    const int boardType,
    SharedUtilsWideChar* const search,
    const int listPage,
    const int listMode,
    const int totalPages,
    const std::uint8_t* packet,
    const int permissions,
    const int searchType,
    SharedUtilsWideChar* const extraSearch,
    const int searchStart,
    const int searchEnd
) {
    // Current L2Server 0x005c1b38.
    L2_GUARD_FUNCTION();
    User* const user = m_mySocket != nullptr ? m_mySocket->GetUser() : nullptr;
    if (user == nullptr) {
        return 0;
    }

    std::u16string tree;
    int treePledgeId = 0;
    packet = ReadTree(packet, tree, treePledgeId);
    int effectivePermissions = permissions;
    if (treePledgeId > 0 && boardType > 0 && treePledgeId == static_cast<int>(user->m_data->m_nPledgeID) &&
        user->IsPledgeMaster()) {
        effectivePermissions = 0x7f;
    }

    GetBodyFromFile(u"bbs_articlelist.htm", user->m_data->m_CountryCode);
    AddStringMap(u"TREE", SanitizeUtf16(tree));

    std::u16string rows;
    if (searchType == -1) {
        std::int32_t announcementCount = 0;
        packet = ::Disassemble(packet, "d", &announcementCount);
        std::u16string announcements;
        for (int index = 0; index < announcementCount; ++index) {
            std::int32_t announcementId = 0;
            time_l2_t postedAt = 0;
            std::array<char16_t, 0x81> title{};
            packet = ::Disassemble(packet, "ddS", &announcementId, &postedAt, 0x102, title.data());
            StrStrMapType parameters;
            AddFormattedStringLocalMap(
                parameters, u"CLICK_LINK", u"<a action=\"bypass %s_%d\">%s</a>", u"_bbsann", announcementId, title.data()
            );
            AddStringLocalMap(parameters, u"POST_DATE", SanitizeUtf16(MakeFormatedDate(postedAt, 1)));
            AppendTarget(announcements, MakeHTMLFromStrTemplet(1, parameters));
        }
        AddStringMap(u"ANN_LIST", SanitizeUtf16(std::u16string(announcements)));
    }

    std::int32_t articleCount = 0;
    packet = ::Disassemble(packet, "d", &articleCount);
    for (int index = 0; index < articleCount; ++index) {
        std::int32_t value1 = 0;
        std::int32_t value2 = 0;
        std::int32_t articleId = 0;
        std::int32_t depth = 0;
        std::int32_t parentId = 0;
        std::int32_t writerServerId = 0;
        std::array<char16_t, 0x32> writerName{};
        std::int32_t writerWorldId = 0;
        std::array<char16_t, 0x32> secondaryName{};
        std::int32_t titleKind = 0;
        std::array<char16_t, 0x81> title{};
        time_l2_t postedAt = 0;
        std::int32_t readCount = 0;
        std::int32_t ignored14 = 0;
        std::int32_t ignored15 = 0;
        std::int32_t replyCount = 0;
        std::array<char16_t, 0x15> prefix{};
        std::int32_t overrideArticleId = 0;
        packet = ::Disassemble(
            packet, "ddddddSdSdSdddddSd", &value1, &value2, &articleId, &depth, &parentId, &writerServerId, 0x64,
            writerName.data(), &writerWorldId, 0x64, secondaryName.data(), &titleKind, 0x102, title.data(), &postedAt,
            &readCount, &ignored14, &ignored15, &replyCount, 0x2a, prefix.data(), &overrideArticleId
        );

        if (articleId < 1) {
            StrStrMapType emptyParameters;
            AppendTarget(rows, MakeHTMLFromStrTemplet(3, emptyParameters));
            continue;
        }
        auto subject = TruncateSubject<0x5000>(prefix.data(), title.data());
        StrStrMapType parameters;
        const int displayedId = overrideArticleId > 0 ? overrideArticleId : articleId;
        AddFormattedStringLocalMap(parameters, u"article_id", u"%d", displayedId);
        AddFormattedStringLocalMap(parameters, u"step", u"%d", depth * 0x14);
        AddStringLocalMap(
            parameters, u"step_icon",
            WideToUtf16(depth > 0 ? L"<img src=\"l2ui.bbs_reply\" width=\"12\" height=\"12\" > " : L"")
        );
        const std::wstring hostExtraSearch = Utf16ToWide(extraSearch, Utf16WidePolicy::PreserveInvalidOnWindows);
        if (searchType == -1) {
            AddFormattedStringLocalMap(
                parameters, u"action", u"bypass %s_%d_%d_%d_%d_%d_%d__%d_%d", u"_bbsread", boardId, articleId, 0, 1,
                listPage, boardType, searchStart, searchEnd
            );
        } else {
            AddFormattedStringLocalMap(
                parameters, u"action", u"bypass %s_%d_%d_%d_%d_%d_%d_%s_%d_%d", u"_bbsread", boardId, articleId, searchType,
                2, listPage, boardType, hostExtraSearch.c_str(), searchStart, searchEnd
            );
        }
        const std::u16string convertedSubject = SanitizeUtf16(ConvertStr(subject.data(), 4));
        if (replyCount < 1) {
            AddStringLocalMap(parameters, u"subject", convertedSubject);
        } else {
            AddFormattedStringLocalMap(parameters, u"subject", u"%s [%d]", convertedSubject, replyCount);
        }
        AddStringLocalMap(parameters, u"char_name", SanitizeUtf16(writerName.data()));
        if (writerWorldId != g_nWorldId) {
            AddFormattedStringLocalMap(parameters, u"server_name", u"@%s", GetWorldName(writerWorldId));
        }
        AddStringLocalMap(parameters, u"post", SanitizeUtf16(MakeFormatedDate(postedAt, 1)));
        AddFormattedStringLocalMap(parameters, u"read_count", u"%d", readCount);
        AppendTarget(rows, MakeHTMLFromStrTemplet(2, parameters));
    }

    AddStringMap(u"ARTICLE_LIST", SanitizeUtf16(std::u16string(rows)));
    AddFormattedStringMap(u"board_type", u"%d", boardType);

    const std::wstring hostExtraSearch = Utf16ToWide(extraSearch, Utf16WidePolicy::PreserveInvalidOnWindows);
    if (listPage > 1) {
        if (searchType == -1) {
            AddFormattedStringMap(u"ACTION_GO_LEFT", u"bypass _bbslist_%d_%d_%d", boardId, listPage - 1, boardType);
        } else {
            AddFormattedStringMap(
                u"ACTION_GO_LEFT", u"bypass %s_%d_%d_%d_%d_%s", u"_bbssearch", boardId, listPage - 1, searchType, boardType,
                hostExtraSearch.c_str()
            );
        }
    }

    std::u16string leftPages;
    const int firstLeft = std::max(1, listPage - 10);
    for (int page = firstLeft; page < listPage; ++page) {
        StrStrMapType parameters;
        if (searchType == -1) {
            AddFormattedStringLocalMap(
                parameters, u"action_link", u"bypass %s_%d_%d_%d", u"_bbslist", boardId, page, boardType
            );
        } else {
            AddFormattedStringLocalMap(
                parameters, u"action_link", u"bypass %s_%d_%d_%d_%d_%s", u"_bbssearch", boardId, page, searchType, boardType,
                hostExtraSearch.c_str()
            );
        }
        AddFormattedStringLocalMap(parameters, u"action_name", u"%d", page);
        AppendTarget(leftPages, MakeHTMLFromStrTemplet(4, parameters));
    }
    AddStringMap(u"GO_LIST", SanitizeUtf16(std::u16string(leftPages)));
    AddFormattedStringMap(u"NPAGE", u"%d", listPage);

    std::u16string rightPages;
    const int lastRight = std::min(totalPages, listPage + 10);
    for (int page = listPage + 1; page <= lastRight; ++page) {
        StrStrMapType parameters;
        if (searchType == -1) {
            AddFormattedStringLocalMap(
                parameters, u"action_link", u"bypass %s_%d_%d_%d", u"_bbslist", boardId, page, boardType
            );
        } else {
            AddFormattedStringLocalMap(
                parameters, u"action_link", u"bypass %s_%d_%d_%d_%d_%s", u"_bbssearch", boardId, page, searchType, boardType,
                hostExtraSearch.c_str()
            );
        }
        AddFormattedStringLocalMap(parameters, u"action_name", u"%d", page);
        AppendTarget(rightPages, MakeHTMLFromStrTemplet(5, parameters));
    }
    AddStringMap(u"GO_LIST2", SanitizeUtf16(std::u16string(rightPages)));
    if (listPage < totalPages) {
        if (searchType == -1) {
            AddFormattedStringMap(u"ACTION_GO_RIGHT", u"bypass %s_%d_%d_%d", u"_bbslist", boardId, listPage + 1, boardType);
        } else {
            AddFormattedStringMap(
                u"ACTION_GO_RIGHT", u"bypass %s_%d_%d_%d_%d_%s", u"_bbssearch", boardId, listPage + 1, searchType, boardType,
                hostExtraSearch.c_str()
            );
        }
    }

    if ((effectivePermissions & 4) == 0 || user->m_data->m_nLevel < g_nBBSWriteLevel) {
        if (searchType == -1) {
            AddFormattedStringMap(u"ACTION_WRITEBUTTON", u"bypass %s_%d_%d_%d", u"_bbser2", boardId, listPage, boardType);
        } else {
            AddFormattedStringMap(
                u"ACTION_WRITEBUTTON", u"bypass %s_%d_%d_%d_%d_%s", u"_bbser3", boardId, listPage, searchType, boardType,
                hostExtraSearch.c_str()
            );
        }
    } else {
        AddFormattedStringMap(u"ACTION_WRITEBUTTON", u"bypass %s_%d_%d", u"_bbswriteform", boardId, boardType);
    }
    AddFormattedStringMap(u"BBS_C_WRITE_SEARCH", u"%d", 5);
    if (boardType < 1) {
        AddFormattedStringMap(u"nBoardId", u"%d", boardId);
    } else {
        AddFormattedStringMap(u"nBoardId", u"%d_%d", boardId, boardType);
    }
    AddFormattedStringMap(u"board_id", u"%d", boardId);
    static_cast<void>(SendHTML(
        MakeHTMLFromStrTemplet(0, m_mapParam), boardType > 0 ? 9 : 2, boardId, listPage, searchType, listMode, totalPages,
        Utf16ToWide(search, Utf16WidePolicy::PreserveInvalidOnWindows).c_str(), 1
    ));
    return 1;
}

int CCommunity::GetSearch(
    const int boardId,
    const int boardType,
    const int searchType,
    SharedUtilsWideChar* const search,
    SharedUtilsWideChar* const extraSearch,
    const int listMode,
    const int listPage,
    const int searchStart,
    const std::uint8_t* const packet,
    const int searchEnd
) {
    // Current L2Server 0x005c4948.
    L2_GUARD_FUNCTION();
    if (m_mySocket != nullptr && m_mySocket->GetUser() != nullptr) {
        static_cast<void>(GetArticleList(
            boardId, boardType, extraSearch, listMode, listPage, searchStart, packet, searchEnd, searchType, search, 1, 1
        ));
    }
    return 1;
}

int CCommunity::GetFavorite(const int count, const std::uint8_t* packet) {
    // Current L2Server 0x005c4a80.
    L2_GUARD_FUNCTION();
    User* const user = m_mySocket != nullptr ? m_mySocket->GetUser() : nullptr;
    if (user == nullptr) {
        return 1;
    }

    GetBodyFromFile(u"bbs_getfavorite.htm", user->m_data->m_CountryCode);
    std::u16string rows;
    for (int index = 0; index < 0xe; ++index) {
        if (index < count) {
            std::int32_t value1 = 0;
            std::int32_t favoriteId = 0;
            time_l2_t date = 0;
            std::array<char16_t, 0x32> label{};
            std::array<char16_t, 0x32> url{};
            std::int32_t argument1 = 0;
            std::int32_t argument2 = 0;
            std::int32_t argument3 = 0;
            std::int32_t argument4 = 0;
            std::int32_t argument5 = 0;
            std::array<char16_t, 0x15> stringArgument{};
            std::array<char16_t, 0x33> lastArgument{};
            std::array<char16_t, 0x65> description{};
            packet = ::Disassemble(
                packet, "dddSSdddddSSS", &value1, &favoriteId, &date, 0x64, label.data(), 0x64, url.data(), &argument1,
                &argument2, &argument3, &argument4, &argument5, 0x2a, stringArgument.data(), 0x66, lastArgument.data(), 0xca,
                description.data()
            );
            if (favoriteId > 0) {
                StrStrMapType parameters;
                AddFormattedStringLocalMap(parameters, u"fav_id", u"%d", favoriteId);
                AddStringLocalMap(parameters, u"url_string", SanitizeUtf16(url.data()));
                AddFormattedStringLocalMap(parameters, u"arg1", u"%d", argument1);
                AddFormattedStringLocalMap(parameters, u"arg2", u"%d", argument2);
                AddFormattedStringLocalMap(parameters, u"arg3", u"%d", argument3);
                AddFormattedStringLocalMap(parameters, u"arg4", u"%d", argument4);
                AddFormattedStringLocalMap(parameters, u"arg5", u"%d", argument5);
                AddStringLocalMap(parameters, u"sArg", SanitizeUtf16(stringArgument.data()));
                AddStringLocalMap(
                    parameters, u"arg_last", SanitizeUtf16(lastArgument[0] != u'\0' ? lastArgument.data() : label.data())
                );
                AddStringLocalMap(parameters, u"BBS_DELFAV", u"_bbsdelfav");
                AddStringLocalMap(parameters, u"sDate", SanitizeUtf16(MakeFormatedDate(date, 2)));
                if (lastArgument[0] != u'\0') {
                    AddFormattedStringLocalMap(
                        parameters, u"description", u"<tr><td></td><td align=center>%s</td><td></td><td></td><td></td></tr>",
                        description.data()
                    );
                }
                AppendTarget(rows, MakeHTMLFromStrTemplet(1, parameters));
                continue;
            }
        }
        StrStrMapType parameters;
        AppendTarget(rows, MakeHTMLFromStrTemplet(2, parameters));
    }

    AddStringMap(u"FAV_LIST", SanitizeUtf16(std::u16string(rows)));
    static_cast<void>(SendHTML(MakeHTMLFromStrTemplet(0, m_mapParam), 7, 0, 0, 0, 0, 0, L"", 1));
    return 1;
}

int CCommunity::GetArticleComment(
    const int boardId,
    const int articleId,
    const int boardType,
    const int listMode,
    const int listPage,
    const int searchType,
    SharedUtilsWideChar* const search,
    const int searchStart,
    const int permissions,
    const int commentPage,
    const int commentCount,
    const std::uint8_t* packet,
    const int entryCount
) {
    // Current L2Server 0x005c2e88.
    L2_GUARD_FUNCTION();
    User* const user = m_mySocket != nullptr ? m_mySocket->GetUser() : nullptr;
    if (user == nullptr) {
        return 1;
    }

    GetBodyFromFile(u"bbs_readcomment.htm", user->m_data->m_CountryCode);
    AddFormattedStringMap(u"MULTI_3", u"3%c", 8);
    std::u16string rows;
    std::u16string multi;
    AppendFormatted(multi, u"3%c", 8);
    for (int index = 0; index < entryCount; ++index) {
        std::array<char16_t, 0x32> writer{};
        std::array<char16_t, 0x100> comment{};
        time_l2_t postedAt = 0;
        std::int32_t commentId = 0;
        std::int32_t writerServerId = 0;
        std::int32_t writerWorldId = 0;
        packet = ::Disassemble(
            packet, "SSdddd", 0x64, writer.data(), 0x200, comment.data(), &postedAt, &commentId, &writerServerId,
            &writerWorldId
        );
        int effectivePermissions = permissions;
        if (writerServerId == static_cast<int>(user->GetServerId()) && writerWorldId == g_nWorldId) {
            effectivePermissions = 0x7f;
        }

        StrStrMapType parameters;
        AddStringLocalMap(parameters, u"char_name", SanitizeUtf16(writer.data()));
        if (writerWorldId != g_nWorldId) {
            AddFormattedStringLocalMap(parameters, u"server_name", u"@%s", GetWorldName(writerWorldId));
        }
        AddStringLocalMap(parameters, u"comment", SanitizeUtf16(ConvertStr(comment.data(), 4)));
        AddStringLocalMap(parameters, u"sDate2", SanitizeUtf16(MakeFormatedDate(postedAt, 5)));
        AddStringLocalMap(parameters, u"sTime", SanitizeUtf16(MakeFormatedDate(postedAt, 4)));
        if ((effectivePermissions & 0x20) != 0) {
            AddFormattedStringLocalMap(
                parameters, u"DELETE_BUTTON",
                u"<button action=\"bypass %s_%d_%d_%d_3_%d\" back=\"l2ui.bbs_delete_down\" width=12 height=12 "
                u"fore=\"l2ui.bbs_delete\" ></button>",
                u"_bbsdelete", boardId, articleId, commentId, boardType
            );
        }
        AppendTarget(rows, MakeHTMLFromStrTemplet(1, parameters));
    }

    const int totalCommentPages = commentCount == 1 ? 1 : (commentCount - 1) / 6 + 1;
    StrStrMapType navigation;
    AppendTarget(rows, MakeHTMLFromStrTemplet(2, navigation));
    const std::wstring hostSearch = Utf16ToWide(search, Utf16WidePolicy::PreserveInvalidOnWindows);
    if (commentPage < 2) {
        navigation.clear();
        AppendTarget(rows, MakeHTMLFromStrTemplet(7, navigation));
    } else {
        navigation.clear();
        AddFormattedStringLocalMap(
            navigation, u"action_link", u"bypass %s_%d_%d_%d_%d_%d_%d_%s_%d_%d", u"_bbsread", boardId, articleId, boardType,
            listMode, listPage, searchType, hostSearch.c_str(), commentPage - 1, searchStart
        );
        AppendTarget(rows, MakeHTMLFromStrTemplet(3, navigation));
    }
    navigation.clear();
    AddFormattedStringLocalMap(navigation, u"current_page", u"%d", commentPage);
    AddFormattedStringLocalMap(navigation, u"tot_page", u"%d", totalCommentPages);
    AppendTarget(rows, MakeHTMLFromStrTemplet(4, navigation));
    navigation.clear();
    if (commentPage < totalCommentPages) {
        AddFormattedStringLocalMap(
            navigation, u"action_link", u"bypass %s_%d_%d_%d_%d_%d_%d_%s_%d_%d", u"_bbsread", boardId, articleId, boardType,
            listMode, listPage, searchType, hostSearch.c_str(), commentPage + 1, searchStart
        );
        AppendTarget(rows, MakeHTMLFromStrTemplet(5, navigation));
    } else {
        AppendTarget(rows, MakeHTMLFromStrTemplet(8, navigation));
    }
    navigation.clear();
    AppendTarget(rows, MakeHTMLFromStrTemplet(6, navigation));

    AddStringMap(u"COMMENT_LIST", SanitizeUtf16(std::u16string(rows)));
    static_cast<void>(SendHTML(
        MakeHTMLFromStrTemplet(0, m_mapParam), boardType > 0 ? 9 : 3, boardId, articleId, listMode, listPage, searchType,
        hostSearch.c_str(), 2
    ));
    return 1;
}

int CCommunity::GetArticleThread(const int boardId, const int articleId, const int boardType, const std::uint8_t* packet) {
    // Current L2Server 0x005c39d0.
    L2_GUARD_FUNCTION();
    User* const user = m_mySocket != nullptr ? m_mySocket->GetUser() : nullptr;
    if (user == nullptr) {
        return 1;
    }

    std::int32_t searchType = 0;
    std::int32_t listMode = 0;
    std::int32_t listPage = 0;
    std::array<char16_t, 0x15> search{};
    std::int32_t searchStart = 0;
    std::int32_t page = 0;
    std::int32_t totalCount = 0;
    std::int32_t entryCount = 0;
    packet = ::Disassemble(
        packet, "dddSdddd", &searchType, &listMode, &listPage, 0x2a, search.data(), &searchStart, &page, &totalCount,
        &entryCount
    );

    GetBodyFromFile(u"bbs_getarticlethread.htm", user->m_data->m_CountryCode);
    AddFormattedStringMap(u"MULTI_4", u"4%c", 8);
    std::u16string rows;
    for (int index = 0; index < entryCount; ++index) {
        std::int32_t value1 = 0;
        std::int32_t value2 = 0;
        std::int32_t rowArticleId = 0;
        std::int32_t depth = 0;
        std::int32_t parentId = 0;
        std::int32_t writerServerId = 0;
        std::array<char16_t, 0x32> writer{};
        std::int32_t writerWorldId = 0;
        std::array<char16_t, 0x32> secondary{};
        std::int32_t titleKind = 0;
        std::array<char16_t, 0x81> title{};
        time_l2_t postedAt = 0;
        std::int32_t readCount = 0;
        std::int32_t ignored14 = 0;
        std::int32_t ignored15 = 0;
        std::int32_t replyCount = 0;
        std::array<char16_t, 0x15> prefix{};
        std::int32_t overrideArticleId = 0;
        packet = ::Disassemble(
            packet, "ddddddSdSdSdddddSd", &value1, &value2, &rowArticleId, &depth, &parentId, &writerServerId, 0x64,
            writer.data(), &writerWorldId, 0x64, secondary.data(), &titleKind, 0x102, title.data(), &postedAt, &readCount,
            &ignored14, &ignored15, &replyCount, 0x2a, prefix.data(), &overrideArticleId
        );

        auto subject = TruncateSubject<0x97>(prefix.data(), title.data());
        StrStrMapType parameters;
        AddFormattedStringLocalMap(
            parameters, u"article_id", u"%d", overrideArticleId > 0 ? overrideArticleId : rowArticleId
        );
        AddFormattedStringLocalMap(parameters, u"step", u"%d", depth * 0x14);
        if (depth > 0) {
            AddStringLocalMap(parameters, u"step_icon", u"<img src=\"l2ui.bbs_reply\" width=\"12\" height=\"12\" >");
        }
        AddStringLocalMap(parameters, u"char_name", SanitizeUtf16(writer.data()));
        if (writerWorldId != g_nWorldId) {
            AddFormattedStringLocalMap(parameters, u"server_name", u"@%s", GetWorldName(writerWorldId));
        }
        AddStringLocalMap(parameters, u"sDate", SanitizeUtf16(MakeFormatedDate(postedAt, 1)));
        AddFormattedStringLocalMap(parameters, u"read_count", u"%d", readCount);
        const int width = 0x13b - depth * 0x14;
        AddFormattedStringLocalMap(parameters, u"threadwidth", u"%d", width);
        const std::u16string convertedSubject(ConvertStr(subject.data(), 4));
        if (rowArticleId == articleId) {
            AddStringLocalMap(parameters, u"threadsubject", convertedSubject);
        } else {
            AddFormattedStringLocalMap(
                parameters, u"threadaction", u"bypass %s_%d_%d_%d_%d_%d_%d_%s_%d_%d", u"_bbsread", boardId, rowArticleId,
                searchType, listMode, listPage, boardType, search.data(), searchStart, page
            );
            AddStringLocalMap(parameters, u"threadsubject", convertedSubject);
        }
        if (replyCount > 0) {
            AddFormattedStringLocalMap(parameters, u"threadcnt", u"[%d]", replyCount);
        } else {
            AddStringLocalMap(parameters, u"threadcnt", u"");
        }
        AppendTarget(rows, MakeHTMLFromStrTemplet(rowArticleId == articleId ? 1 : 2, parameters));
    }

    const int totalPages = totalCount == 1 ? 1 : (totalCount - 1) / 5 + 1;
    StrStrMapType navigation;
    AppendTarget(rows, MakeHTMLFromStrTemplet(3, navigation));
    const std::wstring hostSearch = Utf16ToWide(search.data());
    if (page < 2) {
        AppendTarget(rows, MakeHTMLFromStrTemplet(8, navigation));
    } else {
        navigation.clear();
        AddFormattedStringLocalMap(
            navigation, u"action_link", u"bypass %s_%d_%d_%d_%d_%d_%d_%s_%d_%d", u"_bbsread", boardId, articleId, searchType,
            listMode, listPage, boardType, hostSearch.c_str(), searchStart, page - 1
        );
        AppendTarget(rows, MakeHTMLFromStrTemplet(4, navigation));
    }
    navigation.clear();
    AddFormattedStringLocalMap(navigation, u"current_page", u"%d", page);
    AddFormattedStringLocalMap(navigation, u"tot_page", u"%d", totalPages);
    AppendTarget(rows, MakeHTMLFromStrTemplet(5, navigation));
    navigation.clear();
    if (page < totalPages) {
        AddFormattedStringLocalMap(
            navigation, u"action_link", u"bypass %s_%d_%d_%d_%d_%d_%d_%s_%d_%d", u"_bbsread", boardId, articleId, searchType,
            listMode, listPage, boardType, hostSearch.c_str(), searchStart, page + 1
        );
        AppendTarget(rows, MakeHTMLFromStrTemplet(6, navigation));
    } else {
        AppendTarget(rows, MakeHTMLFromStrTemplet(9, navigation));
    }
    navigation.clear();
    AppendTarget(rows, MakeHTMLFromStrTemplet(7, navigation));
    AddStringMap(u"THREAD_LIST", SanitizeUtf16(std::u16string(rows)));
    static_cast<void>(
        SendHTML(MakeHTMLFromStrTemplet(0, m_mapParam), boardType > 0 ? 9 : 3, boardId, articleId, 0, 0, 0, L"", 2)
    );
    return 1;
}
