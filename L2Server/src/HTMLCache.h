#pragma once

#include "Protocol/src/L2Constants.h"
#include "Shared/src/IOC.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cstddef>
#include <cstdint>

enum E_TODOLIST_TYPE : std::int32_t;

struct HTMLCmdData {
    struct HTMLParamData {
        std::u16string m_ParamName; // +0x00..+0x27
        std::u16string m_ParamValue; // +0x28..+0x4f
        bool m_bClientSide = false; // +0x50
        bool m_bParamValueOnly = false; // +0x51
        std::array<std::byte, 0x06> m_alignment52To58{}; // +0x52..+0x57
    };

    bool Parse(const char16_t* command, int length);
    int Compare(const HTMLCmdData& other) const;

    std::u16string m_Cmd; // +0x00..+0x27
    std::vector<HTMLParamData> m_Params; // +0x28..+0x47
};

struct HTMLCmdDataToDoList {
    struct HTMLParamDataToDoList {
        std::u16string m_ParamName; // +0x00..+0x27
        std::u16string m_ParamValue; // +0x28..+0x4f
        bool m_bClientSide = false; // +0x50
        bool m_bParamValueOnly = false; // +0x51
        std::array<std::byte, 0x06> m_alignment52To58{}; // +0x52..+0x57
    };

    bool Parse(const char16_t* command, int length);
    const char16_t* GetStrValue(const char16_t* name) const;

    std::u16string m_Cmd; // +0x00..+0x27
    std::vector<HTMLParamDataToDoList> m_Params; // +0x28..+0x47
};

class CHTMLCache : public CIOObject {
public:
    using CacheMap = std::map<std::u16string, std::u16string>;

    CHTMLCache();
    ~CHTMLCache() override;

    int OpenFile();
    bool ReadHTMLFile(const wchar_t* fileName, std::u16string& output, const wchar_t* folderName);
    bool GetHTMLFileContent(const char16_t* fileName, std::u16string& output, const wchar_t* folderName);
    bool RemoveCachingHTMLFile(const char16_t* fileName);
    void ClearCaching();
    int IsValidLink(std::vector<HTMLCmdData>& expectedLinks, std::u16string_view command);
    void SetToDoListNameFromHTML(std::u16string& output, char* html, int length);
    void GetToDoListNameFromHTML(std::u16string& output, const std::u16string& fileName, const wchar_t* folderName);

    void OnIOCallback(int success, std::uint32_t transferred, PortableOverlapped* overlapped) override;

private:
    friend class CHTMLCacheManager;

    const wchar_t* m_pszFolder; // +0x0018
    [[maybe_unused]] std::array<std::byte, 0x250> m_FindData; // +0x0020..+0x026f
    void* m_hFind; // +0x0270
    void* m_hFile; // +0x0278
    [[maybe_unused]] void* m_hIOCP; // +0x0280
    std::array<std::byte, 0x20> m_overlappedRead; // +0x0288..+0x02a7
    std::uint32_t m_dwBuffer; // +0x02a8
    std::uint32_t m_dwFileSize; // +0x02ac
    CacheMap m_mapCache; // +0x02b0..+0x02cf
    CacheMap m_mapTNameCache; // +0x02d0..+0x02ef
    CYieldLock m_Lock; // +0x02f0..+0x02ff
    std::array<std::byte, 0x4000> m_Buffer; // +0x0300..+0x42ff
};

class CHTMLCacheManager {
public:
    CHTMLCacheManager();
    ~CHTMLCacheManager();

    const wchar_t* GetFolderName(int language);
    void MakeToDoListHtmlFileName(std::u16string& output, E_TODOLIST_TYPE& type, const std::u16string& name);
    bool GetHTMLFile(const char16_t* fileName, std::u16string& output, int language);
    bool RemoveCachingHTMLFile(const char16_t* fileName, int language);
    void ClearCaching();
    int IsValidLink(std::vector<HTMLCmdData>& expectedLinks, std::u16string_view command, int language);
    void Load();
    void GetToDoListNameFromHTML(std::u16string& output, E_TODOLIST_TYPE& type, const std::u16string& name, int language);

private:
    CHTMLCache m_DefaultCache; // +0x0000..+0x42ff
    std::map<std::int32_t, CHTMLCache*> m_MultiLangCache; // +0x4300..+0x431f
};

extern CHTMLCacheManager g_HTMLCache;
