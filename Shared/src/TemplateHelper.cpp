#include "Shared/src/TemplateHelper.h"

#include "Shared/src/Log.h"
#include "Shared/src/TextRuntime.h"

#include <string>

namespace template_helper_detail {

void ReportMissingCallbackRegistration(const char16_t* tableName, int id) {
    // std::wstring hostName
    // if (tableName) {
    //     for (const char16_t* current = tableName.
    //          *current != u'\0'.
    //          ++current) {
    //         hostName.push_back(
    //             static_cast<wchar_t>(*current))
    //     }
    // }
    const std::string hostName = Utf16ToUtf8(tableName ? std::u16string_view(tableName) : std::u16string_view(u"unnamed"));

    // Current L2NPC.exe 0x004ff4ec / 0x004ff55c.
    // Log.Add(
    //     LOG_ERROR,
    //     L"   >>> %s (%3d) handler is not registered!!!! <<<",
    //     hostName.c_str(),
    //     id)
    // Log.Add(
    //     LOG_ERROR,
    //     "[callbacks] No handler registered for slot %d in table \"%s\". Registration validation failed.",
    //     id, hostName.c_str())
    L2_LOG_ERROR(
        Log, "[callbacks] No handler registered for slot %d in table \"%s\". Registration validation failed.", id,
        hostName.c_str()
    );
}

void ReportCallbackAddressRange(
    const int minimumIndex, const std::uint64_t minimumAddress, const int maximumIndex, const std::uint64_t maximumAddress
) {
    // Current L2Server.exe 0x007f6744.
    // Noise: Startup callback address extrema do not report readiness or a fault.
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR] PacketAddr [%d][0x%016I64X] ~ [%d][0x%016I64X]",
    //     minimumIndex,
    //     minimumAddress,
    //     maximumIndex,
    //     maximumAddress)
}

} // namespace template_helper_detail
