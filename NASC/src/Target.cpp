#include "Target.h"

#include "Encoding.h"

#include <string>

namespace nasc {
namespace {

void AppendLine(std::string& output, std::string_view key, std::uint32_t value) {
    output.append(key);
    output.push_back(' ');
    output.append(std::to_string(value));
    output.append("\r\n");
}

} // namespace

std::u16string LegacyObjectHeader() {
    std::string header;
    AppendLine(header, "SizeofPointer", legacyWin64Target.pointerSize);
    AppendLine(header, "SharedFactoryVersion", legacyWin64Target.sharedFactoryVersion);
    AppendLine(header, "NPCHVersion", legacyWin64Target.npchVersion);
    AppendLine(header, "NASCVersion", legacyWin64Target.nascVersion);
    AppendLine(header, "NPCEventHVersion", legacyWin64Target.npcEventHVersion);
    header.append("Debug 0\r\n\r\n");
    return Utf8ToUtf16(header);
}

std::u16string LegacyVersionFile() {
    std::string text;
    AppendLine(text, "SharedFactoryVersion", legacyWin64Target.sharedFactoryVersion);
    AppendLine(text, "NPCHVersion", legacyWin64Target.npchVersion);
    AppendLine(text, "NASCVersion", legacyWin64Target.nascVersion);
    AppendLine(text, "NPCEventHVersion", legacyWin64Target.npcEventHVersion);
    text.append("Platform ");
    text.append(legacyWin64Target.platform);
    text.append("\r\n");
    return Utf8ToUtf16(text);
}

} // namespace nasc
