#pragma once

#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"

#include <cstdint>

struct XMLElement {
    XMLElement();
    ~XMLElement() = default;

    XMLElement* GetLastChild();
    bool Add(XMLElement* element);
    void ReleaseChild();

    std::uint32_t m_nDepth;
    std::u16string m_sName;
    std::map<std::u16string, std::u16string, runtime::CStringLess<std::u16string>> m_smapAttr;
    std::vector<XMLElement*> m_vChild;
};

class CXmlLoader : public ::Singleton<CXmlLoader> {
public:
    XMLElement* Load(const wchar_t* filename);
    XMLElement* LoadSecure(const wchar_t* filename);
};

namespace XMLUtil {

const std::u16string& GetAttributeValue(
    const XMLElement* element, const std::u16string& attributeName, const std::u16string& defaultValue
);

int GetAttributeValueAsInt(const XMLElement* element, const std::u16string& attributeName, int defaultValue);

std::int64_t GetAttributeValueAsInt64(
    const XMLElement* element, const std::u16string& attributeName, std::int64_t defaultValue
);

float GetAttributeValueAsFloat(const XMLElement* element, const std::u16string& attributeName, float defaultValue);

const XMLElement* FindChild(const XMLElement* element, const std::u16string& childName);

std::vector<XMLElement*> Select(const XMLElement* element, const std::u16string& selector);

} // namespace XMLUtil
