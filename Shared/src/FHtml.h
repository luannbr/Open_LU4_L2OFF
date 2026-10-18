#pragma once

#include "Shared/src/MemorySub.h"

#include "Shared/src/Obj.h"
#include "Shared/src/ScriptAccessible.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>


// Shared/fhtml.h. The current L2NPC.exe constructor at 0x004302b0 confirms
// MemoryObject at +0, ScriptAccessibleEx at +0x18, four 0x20-byte maps at
// +0x20..+0x9f, and the 0x28-byte HTML file name at +0xa0.
class CFHTML : public MemoryObject, public ScriptAccessibleEx {
public:
    using StrStrMapType = std::map<std::u16string, std::u16string, runtime::CStringLess<std::u16string>>;

    CFHTML();
    CFHTML(const CFHTML&) = default;

    L2_DECLARE_MEMORY_ALLOCATION(CFHTML)

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    void SetHTMLFileName(const char16_t* pszFileName);
    void Set(const char16_t* pszVarName, int nIntData);
    void Set(const char16_t* pszVarName, std::int64_t nInt64Data);
    void Set(const char16_t* pszVarName, float dFloatData);
    void Set(const char16_t* pszVarName, const char16_t* pszStrData);

    char* MakeStream(char* pBuffer, int nBufferSize);

    static std::u16string MakeHTMLFromStream(const unsigned char*& packet, std::uint8_t countryCode);

    static std::u16string MakeHTMLFromStrMap(
        const char16_t* fileName, const StrStrMapType& parameters, std::uint8_t countryCode
    );

    ScriptAccessibleEx* GetScriptAccessibleEx() const override;

private:
    ~CFHTML() override;

    using StrStrMapItrType = StrStrMapType::iterator;

    std::map<std::u16string, int, runtime::CStringLess<std::u16string>> m_mapIntData; // +0x20
    std::map<std::u16string, std::int64_t, runtime::CStringLess<std::u16string>> m_mapInt64Data; // +0x40
    std::map<std::u16string, float, runtime::CStringLess<std::u16string>> m_mapFloatData; // +0x60
    StrStrMapType m_mapStrData; // +0x80
    std::u16string m_sHTMLFileName; // +0xa0
};
