#include "Shared/src/Log.h"
#include "Shared/src/Runtime/ResourceDirectories.h"

#include "XmlLoader.h"

#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <libxml/xmlerror.h>
#include <libxml/xmlreader.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <system_error>

namespace {

std::int64_t WindowsWtoi64(std::u16string_view value) {
    const std::string narrow = ::Utf16ToUtf8(value);
    return static_cast<std::int64_t>(std::strtoll(narrow.c_str(), nullptr, 10));
}

double WindowsWtof(std::u16string_view value) {
    const std::string narrow = ::Utf16ToUtf8(value);
    return std::strtod(narrow.c_str(), nullptr);
}

std::string HostXmlPath(const wchar_t* filename) {
    std::string path = filename ? ::WideToUtf8(std::wstring_view(filename)) : std::string();
#if !defined(_WIN32)
    std::replace(path.begin(), path.end(), '\\', '/');
#endif
    return runtime::ResolveResourcePath(path).string();
}

struct XmlTextReaderDeleter {
    void operator()(xmlTextReader* reader) const noexcept {
        if (reader) {
            xmlFreeTextReader(reader);
        }
    }
};

using XmlTextReaderPtr = std::unique_ptr<xmlTextReader, XmlTextReaderDeleter>;

std::string XmlErrorMessage(const xmlError* error) {
    std::string message = error && error->message ? error->message : "";
    for (char& character : message) {
        if (character == '\r' || character == '\n' || character == '\t') {
            character = ' ';
        }
    }
    const auto last = message.find_last_not_of(' ');
    if (last == std::string::npos) {
        return "libxml did not report a reason";
    }
    message.resize(last + 1);
    return message;
}

// void LogXmlTransportError(
//     int line,
//     const char* message,
//     int error) {
//     // Log.Add(
//     //     LOG_ERROR,
//     //     "[%s][%d][XmlLoader] %s, error is %08.8x",
//     //     "XmlLoader.cpp",
//     //     line,
//     //     message,
//     //     static_cast<unsigned int>(error))
//     // L2_LOG_ERROR(Log,
//     //     "[%s][%d][XmlLoader] %s, error is %08.8x",
//     //     "XmlLoader.cpp",
//     //     line,
//     //     message,
//     //     static_cast<unsigned int>(error))
//     L2_LOG_ERROR(Log,
//         "Cannot obtain XML node data: %s (helper result %d). No next element is returned.",
//         message != nullptr ? message : "<no detail>", error)
// }
// Diagnostic-only reader context: these queries do not advance the XML cursor.
void LogXmlTransportError(xmlTextReader* reader, const char* operation, int result, const char* outcome) {
    // Copy the last error before asking the reader for contextual metadata.
    const xmlError* const error = xmlGetLastError();
    const int errorCode = error ? error->code : 0;
    const std::string reason = XmlErrorMessage(error);
    const std::string errorFile = error && error->file ? error->file : "";
    const xmlChar* const baseUri = xmlTextReaderConstBaseUri(reader);
    const std::string uri = baseUri ? reinterpret_cast<const char*>(baseUri) : (errorFile.empty() ? "<unknown>" : errorFile);
    const int nodeType = xmlTextReaderNodeType(reader);
    const char* const nodeKind =
        nodeType == XML_READER_TYPE_ATTRIBUTE ? "attribute" : (nodeType == XML_READER_TYPE_ELEMENT ? "element" : "node");
    const int parserLine = xmlTextReaderGetParserLineNumber(reader);
    const int parserColumn = xmlTextReaderGetParserColumnNumber(reader);
    L2_LOG_ERROR(
        Log, "XML %s at base URI '%s' (%s, parser position %d:%d; result %d, last libxml error %d: %s). %s", operation,
        uri.c_str(), nodeKind, parserLine, parserColumn, result, errorCode, reason.c_str(), outcome
    );
}

bool InitXmlReader(XmlTextReaderPtr& reader, const wchar_t* filename) {
    L2_GUARD_FUNCTION();

    const std::string path = HostXmlPath(filename);
    constexpr int parserOptions = XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING;
    // Diagnostic improvement: report the OS/library cause instead of a
    // synthetic HRESULT, without reusing an earlier load's libxml error.
    xmlResetLastError();
    errno = 0;
    reader.reset(xmlReaderForFile(path.c_str(), nullptr, parserOptions));
    const int openError = errno;
    if (!reader) {
        if (openError != 0) {
            const std::string reason = std::generic_category().message(openError);
            // L2_LOG_ERROR(Log,
            //     "[XML] Cannot open file \"%s\": %s (errno %d).",
            //     path.c_str(), reason.c_str(), openError)
            L2_LOG_ERROR(
                Log, "Cannot create XML reader for '%s': %s (errno %d); no XML tree is returned.", path.c_str(),
                reason.c_str(), openError
            );
        } else {
            const xmlError* const error = xmlGetLastError();
            const std::string reason = XmlErrorMessage(error);
            // L2_LOG_ERROR(Log,
            //     "[XML] Cannot create reader for file \"%s\": %s (libxml error %d).",
            //     path.c_str(), reason.c_str(), error ? error->code : 0)
            L2_LOG_ERROR(
                Log, "Cannot create XML reader for '%s': %s (libxml error %d); no XML tree is returned.", path.c_str(),
                reason.c_str(), error ? error->code : 0
            );
        }
        return false;
    }

    // Current XmlLite sets XmlReaderProperty_DtdProcessing to Prohibit.
    // The streaming adapter additionally rejects the document-type node in
    // GetNextXmlElement. Keep all libxml DTD/entity expansion disabled here.
    xmlTextReaderSetParserProp(reader.get(), XML_PARSER_LOADDTD, 0);
    xmlTextReaderSetParserProp(reader.get(), XML_PARSER_DEFAULTATTRS, 0);
    xmlTextReaderSetParserProp(reader.get(), XML_PARSER_SUBST_ENTITIES, 0);
    xmlTextReaderSetParserProp(reader.get(), XML_PARSER_VALIDATE, 0);
    return true;
}

bool GetXmlLocalName(xmlTextReader* reader, const xmlChar** localName) {
    L2_GUARD_FUNCTION();

    *localName = xmlTextReaderConstLocalName(reader);
    if (!*localName) {
        // LogXmlTransportError(
        //     0xe9,
        //     "Error getting local name",
        //     -1)
        LogXmlTransportError(
            reader, "local-name lookup failed", -1, "The current element is discarded; any earlier tree is retained."
        );
        return false;
    }
    return true;
}

bool GetXmlValue(xmlTextReader* reader, const xmlChar** value) {
    L2_GUARD_FUNCTION();

    *value = xmlTextReaderConstValue(reader);
    if (!*value) {
        // LogXmlTransportError(
        //     0xfa,
        //     "Error getting value",
        //     -1)
        LogXmlTransportError(
            reader, "value lookup failed", -1, "The current element is discarded; any earlier tree is retained."
        );
        return false;
    }
    return true;
}

bool GetXmlAttributes(
    xmlTextReader* reader, std::map<std::u16string, std::u16string, runtime::CStringLess<std::u16string>>& attributes
) {
    L2_GUARD_FUNCTION();

    int moved = xmlTextReaderMoveToFirstAttribute(reader);
    while (moved == 1) {
        if (xmlTextReaderIsDefault(reader) == 0) {
            const xmlChar* localName = nullptr;
            const xmlChar* value = nullptr;
            if (!GetXmlLocalName(reader, &localName) || !GetXmlValue(reader, &value)) {
                return false;
            }

            const std::u16string targetName = ::Utf8ToUtf16(reinterpret_cast<const char*>(localName));
            const std::u16string targetValue = ::Utf8ToUtf16(reinterpret_cast<const char*>(value));
            attributes.emplace(std::u16string(targetName), std::u16string(targetValue));
        }
        moved = xmlTextReaderMoveToNextAttribute(reader);
    }

    if (moved < 0) {
        LogXmlTransportError(
            reader, "attribute enumeration failed", moved, "Attributes collected so far are retained; loading continues."
        );
    }

    // XmlLite's Read advances from its attribute cursor. libxml requires the
    // equivalent explicit return to the owning element before the next Read.
    // xmlTextReaderMoveToElement(reader)
    const int elementMoveResult = xmlTextReaderMoveToElement(reader);
    if (elementMoveResult < 0) {
        LogXmlTransportError(
            reader, "return to the owning element failed", elementMoveResult,
            "The current attribute set is still accepted; loading continues."
        );
    }
    return true;
}

XMLElement* GetNextXmlElement(xmlTextReader* reader) {
    L2_GUARD_FUNCTION();

    int readResult = xmlTextReaderRead(reader);
    while (readResult == 1) {
        const int nodeType = xmlTextReaderNodeType(reader);
        if (nodeType == XML_READER_TYPE_DOCUMENT_TYPE) {
            // Current XmlLite returns a failed Read when DTD processing is
            // prohibited. Load treats that exactly like the end of stream.
            LogXmlTransportError(
                reader, "document type is prohibited", nodeType, "Scanning stops; any earlier tree is retained."
            );
            return nullptr;
        }
        if (nodeType == XML_READER_TYPE_ELEMENT) {
            std::unique_ptr<XMLElement> element(new XMLElement());

            const int depth = xmlTextReaderDepth(reader);
            if (depth < 0) {
                // Log.Add(
                //     LOG_ERROR,
                //     "[%s][%d][XmlLoader] Error getting element depth",
                //     "XmlLoader.cpp",
                //     0xbc)
                // L2_LOG_ERROR(Log,
                //     "[%s][%d][XmlLoader] Error getting element depth",
                //     "XmlLoader.cpp",
                //     0xbc)
                // L2_LOG_ERROR(Log,
                //     "Cannot obtain XML element depth (reader result %d). No next element is returned.",
                //     depth)
                LogXmlTransportError(
                    reader, "element-depth lookup failed", depth,
                    "The current element is discarded; any earlier tree is retained."
                );
                return nullptr;
            }
            element->m_nDepth = static_cast<std::uint32_t>(depth);

            const xmlChar* localName = nullptr;
            if (!GetXmlLocalName(reader, &localName)) {
                // Log.Add(
                //     LOG_ERROR,
                //     "[%s][%d][XmlLoader] Error getting element name",
                //     "XmlLoader.cpp",
                //     0xc3)
                // Noise: GetXmlLocalName already reports the failed operation. Repeating the same failure at this caller
                // adds no context. L2_LOG_ERROR(Log,
                //     "[%s][%d][XmlLoader] Error getting element name",
                //     "XmlLoader.cpp",
                //     0xc3)
                return nullptr;
            }
            element->m_sName.assign(::Utf8ToUtf16(reinterpret_cast<const char*>(localName)));

            if (!GetXmlAttributes(reader, element->m_smapAttr)) {
                // Log.Add(
                //     LOG_ERROR,
                //     "[%s][%d][XmlLoader] Error getting attributes",
                //     "XmlLoader.cpp",
                //     0xcb)
                // Noise: GetXmlAttributes propagates an already reported local-name/value failure. Repeating it here adds no
                // context. L2_LOG_ERROR(Log,
                //     "[%s][%d][XmlLoader] Error getting attributes",
                //     "XmlLoader.cpp",
                //     0xcb)
                return nullptr;
            }
            return element.release();
        }
        readResult = xmlTextReaderRead(reader);
    }
    // Normal EOF remains quiet. A failed read previously returned through the
    // same path, leaving callers unable to distinguish it from complete input.
    if (readResult < 0) {
        LogXmlTransportError(reader, "read failed", readResult, "Scanning stops; any earlier tree is retained.");
    }
    return nullptr;
}

XMLElement* GetRootXmlElement(XmlTextReaderPtr& reader, const wchar_t* filename) {
    L2_GUARD_FUNCTION();

    if (!InitXmlReader(reader, filename)) {
        return nullptr;
    }
    XMLElement* const root = GetNextXmlElement(reader.get());
    if (!root || root->m_nDepth != 0) {
        // The current root-depth rejection does not retire a non-null node.
        return nullptr;
    }
    return root;
}

} // namespace

XMLElement::XMLElement() : m_nDepth(0x20) {}

XMLElement* XMLElement::GetLastChild() {
    L2_GUARD_FUNCTION();

    const std::int32_t childCount = static_cast<std::int32_t>(m_vChild.size());
    if (childCount < 1) {
        return nullptr;
    }
    return runtime::CheckedVectorAt(m_vChild, static_cast<std::size_t>(childCount - 1));
}

bool XMLElement::Add(XMLElement* element) {
    L2_GUARD_FUNCTION();

    std::uint32_t currentDepth = m_nDepth;
    if (element->m_nDepth < currentDepth) {
        return false;
    }

    XMLElement* owner = this;
    while (currentDepth < element->m_nDepth - 1U) {
        owner = owner->GetLastChild();
        ++currentDepth;
    }
    owner->m_vChild.push_back(element);
    return true;
}

void XMLElement::ReleaseChild() {
    L2_GUARD_FUNCTION();

    for (XMLElement* child : m_vChild) {
        if (child->m_vChild.empty()) {
            delete child;
        } else {
            child->ReleaseChild();
        }
    }
    m_vChild.clear();
}

namespace XMLUtil {

const std::u16string& GetAttributeValue(
    const XMLElement* element, const std::u16string& attributeName, const std::u16string& defaultValue
) {
    L2_GUARD_FUNCTION();

    if (element) {
        const auto found = element->m_smapAttr.find(attributeName);
        if (found != element->m_smapAttr.end()) {
            return found->second;
        }
    }
    return defaultValue;
}

int GetAttributeValueAsInt(const XMLElement* element, const std::u16string& attributeName, int defaultValue) {
    L2_GUARD_FUNCTION();

    if (element) {
        const auto found = element->m_smapAttr.find(attributeName);
        if (found != element->m_smapAttr.end()) {
            return ::WindowsWtol(::Utf16ToUtf8(runtime::StringView(found->second)));
        }
    }
    return defaultValue;
}

std::int64_t GetAttributeValueAsInt64(
    const XMLElement* element, const std::u16string& attributeName, std::int64_t defaultValue
) {
    L2_GUARD_FUNCTION();

    if (element) {
        const auto found = element->m_smapAttr.find(attributeName);
        if (found != element->m_smapAttr.end()) {
            return WindowsWtoi64(runtime::StringView(found->second));
        }
    }
    return defaultValue;
}

float GetAttributeValueAsFloat(const XMLElement* element, const std::u16string& attributeName, float defaultValue) {
    L2_GUARD_FUNCTION();

    if (element) {
        const auto found = element->m_smapAttr.find(attributeName);
        if (found != element->m_smapAttr.end()) {
            return static_cast<float>(WindowsWtof(runtime::StringView(found->second)));
        }
    }
    return defaultValue;
}

const XMLElement* FindChild(const XMLElement* element, const std::u16string& childName) {
    L2_GUARD_FUNCTION();

    for (XMLElement* child : element->m_vChild) {
        if (child->m_sName == childName) {
            return child;
        }
    }
    return nullptr;
}

std::vector<XMLElement*> Select(const XMLElement* element, const std::u16string& selector) {
    L2_GUARD_FUNCTION();

    const std::u16string_view selectorView = runtime::StringView(selector);
    const std::size_t slash = selectorView.find(u'/');
    if (slash == std::u16string_view::npos) {
        if (selector.empty() || element->m_vChild.empty()) {
            return element->m_vChild;
        }

        std::vector<XMLElement*> selected;
        selected.reserve(element->m_vChild.size());
        std::int32_t index = static_cast<std::int32_t>(element->m_vChild.size()) - 1;
        while (index >= 0) {
            XMLElement* const child = runtime::CheckedVectorAt(element->m_vChild, static_cast<std::size_t>(index));
            if (child->m_sName == selector) {
                selected.push_back(child);
            }
            --index;
        }
        return selected;
    }

    const std::u16string head(selectorView.substr(0, slash));
    const XMLElement* const child = FindChild(element, head);
    if (!child) {
        return {};
    }

    const std::u16string tail(selectorView.substr(slash + 1));
    return Select(child, tail);
}

} // namespace XMLUtil

XMLElement* CXmlLoader::Load(const wchar_t* filename) {
    L2_GUARD_FUNCTION();

    XmlTextReaderPtr reader;
    XMLElement* const root = GetRootXmlElement(reader, filename);
    if (!root) {
        // Opening failures already include the filename and cause. They do
        // not imply a missing root element in a successfully opened file.
        if (!reader) {
            return nullptr;
        }
        const std::string path = HostXmlPath(filename);
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d][XmlLoader] fail to find root element for xml file[%s]",
        //     "XmlLoader.cpp",
        //     0x5f,
        //     path.c_str())
        const xmlError* const error = xmlGetLastError();
        if (error && error->code != XML_ERR_OK) {
            const std::string reason = XmlErrorMessage(error);
            // L2_LOG_ERROR(Log,
            //     "[XML] Cannot read root element from file \"%s\": %s "
            //     "(line %d, column %d. Libxml error %d).",
            //     path.c_str(), reason.c_str(), error->line, error->int2, error->code)
            L2_LOG_ERROR(
                Log, "Cannot read XML root from '%s': %s (line %d, column %d; libxml error %d); no tree is returned.",
                path.c_str(), reason.c_str(), error->line, error->int2, error->code
            );
        } else {
            // L2_LOG_ERROR(Log,
            //     "[XML] File \"%s\" opened, but no XML root element could be read.",
            //     path.c_str())
            L2_LOG_ERROR(
                Log, "XML reader opened '%s', but no valid root element was obtained; no tree is returned.", path.c_str()
            );
        }
        return nullptr;
    }

    while (XMLElement* const element = GetNextXmlElement(reader.get())) {
        root->Add(element);
    }
    return root;
}
