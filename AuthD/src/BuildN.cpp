#include "BuildN.h"

#include "Platform/WindowsTypes.h"

#include <string>
#include <vector>

// L2Authd.exe 0x00519000. The value is owned by BuildN.obj in the selected
// PDB and is also sent to peer servers by the shipping executable.
int buildNumber = 2017012601;

// L2Authd.exe 0x0041D500, Src/buildn.cpp:162-164.
int GetBuildNumber() {
    return buildNumber;
}

// L2Authd.exe 0x0041D510, Src/buildn.cpp:167-210.
const char* GetProductVersion() {
    static std::string productVersion;
    if (!productVersion.empty()) {
        return productVersion.c_str();
    }

#if defined(_WIN32)
    static const std::string appVersionLanguage("0000");
    static const std::string appVersionLocale("04b0");
    static const std::string appVersionString =
        "\\StringFileInfo\\" + appVersionLanguage + appVersionLocale +
        "\\ProductVersion";

    char modulePath[MAX_PATH]{};
    if (GetModuleFileNameA(nullptr, modulePath, MAX_PATH) == 0) {
        return "";
    }

    DWORD ignoredHandle = 0;
    const DWORD versionInfoSize =
        GetFileVersionInfoSizeA(modulePath, &ignoredHandle);
    if (versionInfoSize == 0) {
        return "";
    }

    std::vector<unsigned char> versionInfo(versionInfoSize);
    if (!GetFileVersionInfoA(
            modulePath,
            0,
            versionInfoSize,
            versionInfo.data())) {
        return "";
    }

    char* value = nullptr;
    UINT valueLength = 0;
    if (!VerQueryValueA(
            versionInfo.data(),
            appVersionString.c_str(),
            reinterpret_cast<void**>(&value),
            &valueLength)) {
        return "";
    }

    productVersion = value;
#else
    // The target asks the Win32 version resource of its own module. Mach-O
    // and ELF have no equivalent resource, so the portable transport embeds
    // the exact ProductVersion extracted from the selected L2Authd.exe:
    // StringFileInfo/000004b0/ProductVersion = 1.1.74-RELEASE.
    productVersion = "1.1.74-RELEASE";
#endif

    return productVersion.c_str();
}
