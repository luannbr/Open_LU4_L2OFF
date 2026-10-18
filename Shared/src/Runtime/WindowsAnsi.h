#pragma once

#include <string>
#include <string_view>

namespace portable {

// Cross-host equivalent of WideCharToMultiByte(CP_ACP, 0, ...). Windows
// delegates to the operating-system ACP exactly. POSIX uses the Windows code
// page selected by L2NPC_WINDOWS_ACP (949 by default for the recovered Korean
// deployment baseline). The setting names the first recovered consumer. The
// conversion itself is the common Shared CP_ACP boundary used by every server.
int WideCharToWindowsAnsi(const char16_t* source, int sourceLength, char* destination, int destinationCount);

int WideCharToWindowsAnsi(const wchar_t* source, int sourceLength, char* destination, int destinationCount);

// Cross-host equivalent of MultiByteToWideChar(CP_ACP, 0, ...). The
// char16_t overload keeps the target's two-byte wchar_t carrier on LP64
// hosts. The wchar_t overload is the native Win32 spelling.
int WindowsAnsiToWideChar(const char* source, int sourceLength, char16_t* destination, int destinationCount);

int WindowsAnsiToWideChar(const char* source, int sourceLength, wchar_t* destination, int destinationCount);

std::string WindowsAnsiFromUtf16(std::u16string_view source);

std::string WindowsAnsiFromWide(std::wstring_view source);

std::u16string Utf16FromWindowsAnsi(const char* source);

std::u16string Utf16FromWindowsAnsi(const char* source, std::u16string_view nullValue);

} // namespace portable
