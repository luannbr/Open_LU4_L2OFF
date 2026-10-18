#pragma once

#include "ParseHtml.h"

#include <cstddef>
#include <cstdint>

class CSocket;
class User;

int CheckClanOwner(User* user);
bool ClanBBS(CSocket* socket, const SharedUtilsWideChar* command);

class CClanBBSHTML final : public CParseHTML {
public:
    explicit CClanBBSHTML(CUserSocket* socket);
    ~CClanBBSHTML() override;

    void ClanBBSList(const std::uint8_t* packet);
    void ClanBBSList2(const std::uint8_t* packet);
    void ClanBBSView(const std::uint8_t* packet);
    void ClanBBSAdmin(const std::uint8_t* packet);
    void ClanBBSAdmin2(const std::uint8_t* packet);
    void ClanBBSView2(const std::uint8_t* packet);
};
