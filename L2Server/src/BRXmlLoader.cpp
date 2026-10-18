#include "Shared/src/Log.h"
#include "Shared/src/Runtime/ResourceDirectories.h"

#include "XmlLoader.h"

#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace {

std::string HostSecureXmlPath(const wchar_t* filename) {
    std::string path = filename ? ::WideToUtf8(std::wstring_view(filename)) : std::string();
#if !defined(_WIN32)
    std::replace(path.begin(), path.end(), '\\', '/');
#endif
    return runtime::ResolveResourcePath(path).string();
}

std::wstring HostWideSecureXmlPath(const wchar_t* filename) {
    std::wstring path = filename ? std::wstring(filename) : std::wstring();
#if !defined(_WIN32)
    std::replace(path.begin(), path.end(), L'\\', L'/');
#endif
    return runtime::ResolveResourcePath(path).wstring();
}

} // namespace

XMLElement* CXmlLoader::LoadSecure(const wchar_t* filename) {
    L2_GUARD_FUNCTION();

    constexpr const wchar_t* kTemporaryPath = L"..\\Script\\temp.xml";
    const std::wstring hostInputPath = HostWideSecureXmlPath(filename);
    ::CSecureStream input(hostInputPath.c_str(), true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] LoadSecure %s file failed",
        //     L"br_XmlLoader.cpp",
        //     0x13,
        //     filename)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] LoadSecure %s file failed",
        //     L"br_XmlLoader.cpp",
        //     0x13,
        //     filename)
        L2_LOG_ERROR(Log, "Cannot read secure XML input '%s'; no XML tree is returned.", WideToUtf8(hostInputPath).c_str());
        return nullptr;
    }

    const std::string hostTemporaryPath = HostSecureXmlPath(kTemporaryPath);
    std::FILE* const stream = std::fopen(hostTemporaryPath.c_str(), "wb");
    if (stream) {
        const std::int64_t byteCount =
            static_cast<std::int64_t>(input.GetRawStringLen()) * static_cast<std::int64_t>(sizeof(char16_t));
        std::fwrite(input.GetRawString(), 1, static_cast<std::size_t>(byteCount), stream);
        std::fclose(stream);

        XMLElement* const root = Load(kTemporaryPath);
        std::remove(hostTemporaryPath.c_str());
        if (root) {
            return root;
        }
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] LoadSecure %s file failed",
    //     L"br_XmlLoader.cpp",
    //     0x2a,
    //     filename)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] LoadSecure %s file failed",
    //     L"br_XmlLoader.cpp",
    //     0x2a,
    //     filename)
    L2_LOG_ERROR(
        Log,
        "Cannot load secure XML '%s' through temporary file '%s'; temporary-file creation or XML loading failed and no tree "
        "is returned.",
        WideToUtf8(hostInputPath).c_str(), hostTemporaryPath.c_str()
    );
    return nullptr;
}
