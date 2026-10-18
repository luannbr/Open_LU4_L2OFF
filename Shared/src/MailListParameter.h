#pragma once

#include <array>
#include <cstddef>
#include <cstdint>


// Shared/MailListParameter.{h,cpp} is the physical owner kept by the
// current L2Server and CacheD project manifests. Both current images expose
// the same 0x3c object and packet contract.
class CMailListParameter {
public:
    CMailListParameter();
    ~CMailListParameter() = default;

    void SetMailBoxType(int mailBoxType) noexcept { m_mailBoxType = mailBoxType; }
    void SetPage(int page) noexcept { m_page = page; }
    void SetRowsPerPage(int rowsPerPage) noexcept { m_rowsPerPage = rowsPerPage; }
    void SetSearchTarget(int searchTarget) noexcept { m_searchTarget = searchTarget; }
    void SetKeyword(const char16_t* keyword);

    int GetMailBoxType() noexcept { return m_mailBoxType; }
    int GetPage() noexcept { return m_page; }
    int GetRowsPerPage() noexcept;
    int GetSearchTarget() noexcept { return m_searchTarget; }
    char16_t* GetKeyword() noexcept { return m_keyword.data(); }
    wchar_t* GetMailBoxName() noexcept;

    int AssemblePacket(char* buffer, int bufferLength);
    const unsigned char* DisassemblePacket(const unsigned char* packet);

private:
    std::int32_t m_mailBoxType; // +0x00
    std::int32_t m_page; // +0x04
    std::int32_t m_rowsPerPage; // +0x08
    std::int32_t m_searchTarget; // +0x0c
    std::array<char16_t, 21> m_keyword; // +0x10
};
