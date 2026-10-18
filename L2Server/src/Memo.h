#pragma once

#include "ParseHtml.h"

#include <cstddef>
#include <cstdint>

class CSocket;

bool MemoHTML(CSocket* socket, const SharedUtilsWideChar* command);

class CMemoHTML final : public CParseHTML {
public:
    explicit CMemoHTML(CUserSocket* socket);
    ~CMemoHTML() override;

    void MemoRead(const std::uint8_t* packet);
    void MemoWriteForm(int mode, const std::uint8_t* packet);
    void MemoModifyForm(int mode, const std::uint8_t* packet);
    void MemoList(const std::uint8_t* packet);
};
