#pragma once

#include "ParseHtml.h"

#include <cstddef>
#include <cstdint>

class CUserSocket;

// Current community.cpp owner. The derived socket member intentionally
// hides CParseHTML::m_mySocket: both physical pointer lanes exist in the
// target and are initialized by the constructor.
class CCommunity final : public CParseHTML {
public:
    CCommunity(CUserSocket* socket, int communityType);
    ~CCommunity() override;

    int GetLinkPage();
    const std::uint8_t* ReadTree(const std::uint8_t* packet, std::u16string& tree, int& treeValue);
    int ReadArticle(int boardId, int boardType, SharedUtilsWideChar* search, const std::uint8_t* packet);
    const std::uint8_t* WriteFormFrame(
        int boardId, int articleId, int boardType, int mode, SharedUtilsWideChar* search, const std::uint8_t* packet
    );
    int WriteForm(int boardId, int boardType, SharedUtilsWideChar* search, const std::uint8_t* packet);
    int WriteReplyForm(int boardId, int boardType, SharedUtilsWideChar* search, const std::uint8_t* packet);
    int WriteModifyForm(int boardId, int boardType, SharedUtilsWideChar* search, const std::uint8_t* packet);
    int GetAnnounce(const std::uint8_t* packet);
    int GetBoardList(const std::uint8_t* packet);
    int GetArticleList(
        int boardId,
        int boardType,
        SharedUtilsWideChar* search,
        int listMode,
        int listPage,
        int searchType,
        const std::uint8_t* packet,
        int searchStart,
        int searchEnd,
        SharedUtilsWideChar* extraSearch,
        int extraValue1,
        int extraValue2
    );
    int GetArticleComment(
        int boardId,
        int articleId,
        int boardType,
        int listMode,
        int listPage,
        int searchType,
        SharedUtilsWideChar* search,
        int searchStart,
        int searchEnd,
        int commentPage,
        int commentCount,
        const std::uint8_t* packet,
        int mode
    );
    int GetArticleThread(int boardId, int articleId, int boardType, const std::uint8_t* packet);
    int GetSearch(
        int boardId,
        int boardType,
        int searchType,
        SharedUtilsWideChar* search,
        SharedUtilsWideChar* extraSearch,
        int listMode,
        int listPage,
        int searchStart,
        const std::uint8_t* packet,
        int searchEnd
    );
    int GetFavorite(int value, const std::uint8_t* packet);

private:
    [[maybe_unused]] std::int32_t m_nCommunityType; // +0xa0
    [[maybe_unused]] std::int32_t m_nBoard; // +0xa4
    CUserSocket* m_mySocket; // +0xa8
};
