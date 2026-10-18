#include "NCGuard.h"

#include "SecureINIFileReader.h"
#include "User.h"
#include "UserSocket.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/ScopedLockGuards.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>

namespace {

constexpr std::u16string_view kPenaltyPatternPath = u".\\NCGuard\\penalty_pattern.ini";

std::uint32_t ByteSwap32(const std::uint32_t value) noexcept {
    return ((value & 0x000000ffU) << 24U) | ((value & 0x0000ff00U) << 8U) | ((value & 0x00ff0000U) >> 8U) |
        ((value & 0xff000000U) >> 24U);
}

std::uint32_t ParseHex32(const char16_t* text) noexcept {
    if (text == nullptr) {
        return 0;
    }

    while (*text == u' ' || *text == u'\t' || *text == u'\r' || *text == u'\n') {
        ++text;
    }

    bool negative = false;
    if (*text == u'+' || *text == u'-') {
        negative = *text == u'-';
        ++text;
    }
    if (text[0] == u'0' && (text[1] == u'x' || text[1] == u'X')) {
        text += 2;
    }

    std::uint32_t value = 0;
    while (*text != u'\0') {
        unsigned digit = 0;
        if (*text >= u'0' && *text <= u'9') {
            digit = static_cast<unsigned>(*text - u'0');
        } else if (*text >= u'a' && *text <= u'f') {
            digit = static_cast<unsigned>(*text - u'a') + 10U;
        } else if (*text >= u'A' && *text <= u'F') {
            digit = static_cast<unsigned>(*text - u'A') + 10U;
        } else {
            break;
        }
        value = value * 16U + digit;
        ++text;
    }
    return negative ? 0U - value : value;
}

std::array<std::uint32_t, 4> HashWords(const std::array<std::uint8_t, 16>& hash) noexcept {
    std::array<std::uint32_t, 4> result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        const std::size_t offset = index * 4U;
        result[index] = static_cast<std::uint32_t>(hash[offset]) | (static_cast<std::uint32_t>(hash[offset + 1U]) << 8U) |
            (static_cast<std::uint32_t>(hash[offset + 2U]) << 16U) | (static_cast<std::uint32_t>(hash[offset + 3U]) << 24U);
    }
    return result;
}

std::u16string FormatHashWordsLower(const std::array<std::uint32_t, 4>& words) {
    std::array<char16_t, 100> buffer{};
    FormatUtf16To(buffer.data(), buffer.size(), u"ImageHash[%x %x %x %x]", words[0], words[1], words[2], words[3]);
    return buffer.data();
}

std::wstring FormatHashBytesUpper(const std::array<std::uint8_t, 16>& hash) {
    static constexpr wchar_t kHex[] = L"0123456789ABCDEF";
    std::wstring result;
    result.reserve(35);
    for (std::size_t index = 0; index < hash.size(); ++index) {
        if (index != 0U && index % 4U == 0U) {
            result.push_back(L' ');
        }
        result.push_back(kHex[hash[index] >> 4U]);
        result.push_back(kHex[hash[index] & 0x0fU]);
    }
    return result;
}

std::array<char16_t, 33> FormatPenaltyPatternHash(const NCGuardPenaltyPattern& pattern) {
    // Current writes four unpadded base-16 strings at fixed eight-WCHAR
    // boundaries in one pre-zeroed 33-WCHAR buffer. A short lane therefore
    // leaves an early terminator instead of being padded to eight digits.
    std::array<char16_t, 33> result{};
    for (std::size_t index = 0; index < pattern.fileHash.size(); ++index) {
        const std::size_t offset = index * 8U;
        FormatUtf16To(result.data() + offset, result.size() - offset, u"%x", ByteSwap32(pattern.fileHash[index]));
    }
    return result;
}

std::wstring JoinPatchHashes(const _NCG_PD_PATCH_INFO& patch, const std::array<std::uint8_t, 16>* moduleHash) {
    std::wstring result = FormatHashBytesUpper(patch.InitialData);
    result.push_back(L' ');
    result.append(FormatHashBytesUpper(patch.PatchedData));
    result.push_back(L' ');
    result.append(FormatHashBytesUpper(patch.DetouredDllHash));
    if (moduleHash != nullptr) {
        result.push_back(L' ');
        result.append(FormatHashBytesUpper(*moduleHash));
    }
    return result;
}

std::u16string UserIpAddress(User& user) { return user.GetSocket()->GetIPAddressString(); }

std::int32_t EventTypeFromEntry(const NCGuardQueueEntry& entry) noexcept {
    std::int32_t eventType = 0;
    std::memcpy(&eventType, entry.eventTypeLane.data(), sizeof(eventType));
    return eventType;
}

void SetEventType(NCGuardQueueEntry& entry, const NCG_EVENT_TYPE eventType) noexcept {
    const std::int32_t value = static_cast<std::int32_t>(eventType);
    std::memcpy(entry.eventTypeLane.data(), &value, sizeof(value));
}

void* CopyVendorPayload(const void* data, const std::uint32_t size) {
    void* const copy = std::malloc(size);
    if (size != 0U) {
        // The target performs the same unchecked allocation/copy pair.
        std::memcpy(copy, data, size);
    }
    return copy;
}

} // namespace

CNCGuardManager::CNCGuardManager() :
    m_loaded(false),
    m_use(false),
    m_version{} {
    L2_GUARD_FUNCTION();
    l2portable_detail::InitializeCriticalSection(m_logCriticalSection);
    l2portable_detail::InitializeCriticalSection(m_messageCriticalSection);
}

CNCGuardManager::~CNCGuardManager() = default;

void CNCGuardManager::Init() {
    // Current 0x0085aee0 ignores the timer insertion result.
    L2_GUARD_FUNCTION();
    AddTimer(1000, 0);
}

void CNCGuardManager::PushLogQueue(const NCGuardQueueEntry& entry) {
    L2_GUARD(m_logCriticalSection);
    m_logQueue.push_back(entry);
}

void CNCGuardManager::PushMessageQueue(const NCGuardQueueEntry& entry) {
    L2_GUARD(m_messageCriticalSection);
    m_messageQueue.push_back(entry);
}

void CNCGuardManager::ProcessMessageQueue() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_messageCriticalSection);
    while (!m_messageQueue.empty()) {
        const NCGuardQueueEntry entry = m_messageQueue.front();
        if (entry.uniqueId != 0U) {
            CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(entry.uniqueId));
            if (user) {
                user->GetSocket()->Send("chdb", 0xfe, 0x12e, entry.size, entry.size, entry.data);
            }
        }
        std::free(entry.data);
        m_messageQueue.pop_front();
    }
}

void CNCGuardManager::ProcessLogQueue() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_logCriticalSection);
    while (!m_logQueue.empty()) {
        const NCGuardQueueEntry entry = m_logQueue.front();
        if (entry.uniqueId != 0U) {
            WriteNCGuardLog(entry.uniqueId, static_cast<NCG_EVENT_TYPE>(EventTypeFromEntry(entry)), entry.data);
        }
        std::free(entry.data);
        m_logQueue.pop_front();
    }
}

void CNCGuardManager::TimerExpired(int) {
    L2_GUARD_FUNCTION();
    ProcessLogQueue();
    ProcessMessageQueue();
    AddTimer(1000, 0);
}

bool CNCGuardManager::LoadPenaltyPattern() {
    L2_GUARD_FUNCTION();
    if (!l2server::g_SecureINIFileDB.Load(kPenaltyPatternPath)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Load Penalty Pattern file failed %s",
        //     L"NCGuard.cpp",
        //     0x5f,
        //     L".\\NCGuard\\penalty_pattern.ini")
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Load Penalty Pattern file failed %s",
        //     L"NCGuard.cpp",
        //     0x5f,
        //     L".\\NCGuard\\penalty_pattern.ini")
        L2_LOG_ERROR(
            Log, "Cannot load NCGuard penalty patterns from '%s'; the current pattern list and loaded flag are unchanged.",
            Utf16ToUtf8(kPenaltyPatternPath).c_str()
        );
        return false;
    }

    std::array<char16_t, 1024> buffer{};
    l2server::g_SecureINIFileDB.GetString(u"HEADER", u"VERSION", u"99999999", buffer.data(), 0x3ff, kPenaltyPatternPath);
    std::fill(m_version.begin(), m_version.end(), u'\0');
    const auto versionEnd = std::find(buffer.begin(), buffer.end(), u'\0');
    const std::size_t versionLength =
        std::min<std::size_t>(static_cast<std::size_t>(versionEnd - buffer.begin()), m_version.size() - 1U);
    std::copy_n(buffer.begin(), versionLength, m_version.begin());

    m_use = l2server::g_SecureINIFileDB.GetInt(u"HEADER", u"USE", 0, kPenaltyPatternPath) == 1;
    const std::int32_t count = l2server::g_SecureINIFileDB.GetInt(u"HEADER", u"COUNT", 0, kPenaltyPatternPath);
    m_penaltyPatterns.reserve(static_cast<std::size_t>(count));

    for (std::int32_t index = 0; index < count; ++index) {
        const std::string sectionSuffix = std::to_string(index);
        std::u16string section = u"PATTERN";
        section.append(sectionSuffix.begin(), sectionSuffix.end());

        NCGuardPenaltyPattern pattern{};
        pattern.ncgId =
            static_cast<std::uint32_t>(l2server::g_SecureINIFileDB.GetInt(section, u"NCGID", 0, kPenaltyPatternPath));
        pattern.mode = l2server::g_SecureINIFileDB.GetInt(section, u"mode", 0, kPenaltyPatternPath);

        for (std::size_t hashIndex = 0; hashIndex < pattern.fileHash.size(); ++hashIndex) {
            const std::string keySuffix = std::to_string(hashIndex + 1U);
            std::u16string key = u"FileHash";
            key.append(keySuffix.begin(), keySuffix.end());
            buffer.fill(u'\0');
            l2server::g_SecureINIFileDB.GetString(section, key, u"00000000", buffer.data(), 0x3ff, kPenaltyPatternPath);
            pattern.fileHash[hashIndex] = ByteSwap32(ParseHex32(buffer.data()));
        }
        m_penaltyPatterns.push_back(pattern);
    }

    m_loaded = true;
    return true;
}

void CNCGuardManager::WriteNCGuardLog(const std::uint32_t uniqueId, const NCG_EVENT_TYPE eventType, void* const data) {
    L2_GUARD_FUNCTION();
    CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(uniqueId));
    if (!user) {
        return;
    }

    GuardHelper switchGuard(L"WriteNCGuardLog switch case", CThreadManager::GetThreadID());
    CLogFormatter formatter;
    switch (eventType) {
        case ReportProcessDetectionEvent: {
            GuardHelper eventGuard(L"ReportProcessDetectionEvent", CThreadManager::GetThreadID());
            const auto& report = *static_cast<const _NCG_REPORT_PROCESS_DETECTION*>(data);
            const auto moduleHash = HashWords(report.DetectedModuleHash);
            const auto processHash = HashWords(report.DetectedProcessHash);

            int matchedIndex = -1;
            std::array<char16_t, 33> patternHash{};
            for (std::size_t index = 0; index < m_penaltyPatterns.size(); ++index) {
                const NCGuardPenaltyPattern& pattern = m_penaltyPatterns[index];
                if (pattern.ncgId != report.PatternId) {
                    continue;
                }

                bool detected = false;
                if (pattern.mode == 0) {
                    detected = true;
                } else if (pattern.mode == 1) {
                    detected = pattern.fileHash == moduleHash;
                } else if (pattern.mode == 2) {
                    detected = pattern.fileHash == processHash;
                } else {
                    const std::string nickname = Utf16ToUtf8(std::u16string_view(user->m_sNickName));
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] NCGuard Error : Wrong pattern mode %s(%d)",
                    //     L"NCGuard.cpp",
                    //     0x165,
                    //     nickname.c_str(),
                    //     user->GetDBID())
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d] NCGuard Error : Wrong pattern mode %s(%d)",
                    //     L"NCGuard.cpp",
                    //     0x165,
                    //     nickname.c_str(),
                    //     user->GetDBID())
                    L2_LOG_ERROR(
                        Log,
                        "NCGuard pattern %u has unsupported mode %d for character '%s' (DB ID %u); this pattern is skipped "
                        "and matching continues.",
                        static_cast<unsigned>(pattern.ncgId), pattern.mode, nickname.c_str(),
                        static_cast<unsigned>(user->GetDBID())
                    );
                }

                if (!detected) {
                    continue;
                }

                const std::string nickname = Utf16ToUtf8(std::u16string_view(user->m_sNickName));
                // Log.Add(
                //     LOG_ERROR,
                //     L"[NCGuard] BOT Detected : %s(%d)",
                //     nickname.c_str(),
                //     user->GetDBID())
                // L2_LOG_ERROR(Log,
                //     L"[NCGuard] BOT Detected : %s(%d)",
                //     nickname.c_str(),
                //     user->GetDBID())
                L2_LOG_WARN(
                    Log,
                    "NCGuard matched penalty pattern %u (mode %d) for character '%s' (DB ID %u); marking the character as "
                    "detected.",
                    static_cast<unsigned>(pattern.ncgId), pattern.mode, nickname.c_str(),
                    static_cast<unsigned>(user->GetDBID())
                );
                matchedIndex = static_cast<int>(index);
                user->SetNCGuardBotDetected(true);
                patternHash = FormatPenaltyPatternHash(pattern);
                break;
            }

            // Current source indexes the vector with -1 on the unmatched path.
            // Keep the same unchecked source expression. The shipping allocator's
            // preceding lane is so observable in audit log 0x5a7.
            const std::int32_t auditMode = m_penaltyPatterns[static_cast<std::size_t>(matchedIndex)].mode;
            const std::u16string modulePath = Utf16Text(FixedUtf16View(report.DetectedModulePath)).String();
            const std::u16string processPath = Utf16Text(FixedUtf16View(report.DetectedProcessPath)).String();
            formatter.WriteLog1(
                0x5a7, user.get(), nullptr, modulePath.data(), patternHash.data(), processPath.data(), report.PatternId,
                report.ProcessId, auditMode, report.DetectedModuleSize, report.DetectedProcessSize, report.IsForeWnd,
                matchedIndex
            );

            const std::u16string ipAddress = UserIpAddress(*user.get());
            formatter.WriteNCGuardLog1(
                0x39a, user.get(), nullptr, modulePath.c_str(), processPath.c_str(), ipAddress, report.DetectedAddress,
                matchedIndex, report.PatternId, report.ProcessId, report.IsForeWnd, report.DetectedModuleSize,
                report.DetectedProcessSize, moduleHash[0], moduleHash[1], moduleHash[2], moduleHash[3], processHash[0],
                processHash[1], processHash[2], processHash[3]
            );
            break;
        }

        case ReportBotUsingDetectionEvent: {
            GuardHelper eventGuard(L"ReportBotUsingDetectionEvent", CThreadManager::GetThreadID());
            const auto& report = *static_cast<const _NCG_REPORT_BOT_USING_DETECTION*>(data);
            const std::u16string imagePath = Utf16Text(FixedUtf16View(report.ImagePath)).String();
            const std::u16string hash = FormatHashWordsLower(HashWords(report.ImageHash));
            const std::u16string ipAddress = UserIpAddress(*user.get());
            formatter.WriteNCGuardLog1(
                0x39c, user.get(), nullptr, imagePath.c_str(), hash, ipAddress, 0, 0, report.PatternId, report.ProcessId,
                report.DetectedAddress, report.FirstUsedTime, report.LastUsedTime, 0, 0, 0, 0, 0, 0, 0, 0
            );
            break;
        }

        case ReportCICheckEvent: {
            GuardHelper eventGuard(L"ReportCICheckEvent", CThreadManager::GetThreadID());
            const auto& report = *static_cast<const _NCG_REPORT_CI_PATTERN*>(data);
            // Current converts this fifth component-name DWORD in base 30 and
            // then discards the text before publishing the numeric lanes.
            std::array<std::uint32_t, 5> componentWords{};
            std::memcpy(componentWords.data(), report.componentName.data(), sizeof(componentWords));
            std::array<wchar_t, 30> unusedBase30Text{};
            std::uint32_t unusedValue = componentWords[4];
            wchar_t* cursor = unusedBase30Text.data() + unusedBase30Text.size() - 1;
            do {
                const std::uint32_t digit = unusedValue % 30U;
                *--cursor = static_cast<wchar_t>(digit < 10U ? L'0' + digit : L'a' + digit - 10U);
                unusedValue /= 30U;
            } while (unusedValue != 0U);
            (void)unusedBase30Text;

            formatter.WriteNCGuardLog1(
                0x39f, user.get(), nullptr, nullptr, nullptr, nullptr, 0, report.validationResult, report.clientID,
                report.flowID, report.componentID, componentWords[0], componentWords[1], componentWords[2],
                componentWords[3], componentWords[4], 0, 0, 0, 0, 0
            );
            break;
        }

        case ReportPatchDetectionEvent: {
            const auto& report = *static_cast<const _NCG_REPORT_PATCH_DETECTION*>(data);
            const std::u16string modulePath = Utf16Text(FixedUtf16View(report.ModulePath)).String();
            const std::u16string ipAddress = UserIpAddress(*user.get());
            const std::uint32_t count = std::min<std::uint32_t>(report.PatchInfoCount, 4U);
            for (std::uint32_t index = 0; index < count; ++index) {
                const _NCG_PD_PATCH_INFO& patch = report.PatchInfo[index];
                std::wstring hashes = JoinPatchHashes(patch, &report.ModuleHash);
                formatter.WriteNCGuardLog1(
                    0x3a2, user.get(), nullptr, modulePath.c_str(), WideToUtf16(hashes.c_str()), ipAddress, 0, 0,
                    report.ModuleBase, report.PatchInfoCount, patch.DetouredDllSize, patch.PatchedRva, 0, 0, 0, 0, 0, 0, 0,
                    0, 0
                );
            }
            break;
        }

        case ReportExtraPatchInfoEvent: {
            const auto& report = *static_cast<const _NCG_REPORT_EXTRA_PATCH_INFO*>(data);
            const std::u16string ipAddress = UserIpAddress(*user.get());
            const std::uint32_t count = std::min<std::uint32_t>(report.ExtraInfoCount, 4U);
            for (std::uint32_t index = 0; index < count; ++index) {
                const _NCG_PD_PATCH_INFO& patch = report.PatchInfo[index];
                const std::u16string detouredPath = Utf16Text(FixedUtf16View(patch.DetouredDllPath)).String();
                std::wstring hashes = JoinPatchHashes(patch, nullptr);
                formatter.WriteNCGuardLog1(
                    0x3a2, user.get(), nullptr, detouredPath.c_str(), WideToUtf16(hashes.c_str()), ipAddress, 0, 0,
                    report.ModuleBase, 0, patch.DetouredDllSize, patch.PatchedRva, patch.PatchedLength, 0, 0, 0, 0, 0, 0, 0,
                    0
                );
            }
            break;
        }

        case ReportNCGIntegrationLogEvent: {
            const auto& report = *static_cast<const _NCG_INTEGRATION_LOG*>(data);
            const FVector position = user->GetPos();
            const std::u16string ipAddress = UserIpAddress(*user.get());
            const std::u16string targetIp = ipAddress;
            formatter.WriteNCGuardLog(
                0x3a3, user->GetDBID(), user->GetAccountID(), 0, 0, runtime::Win64CvttDoubleToInt32(position.x),
                runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z),
                report.str260_WCHAR1.data(), report.str260_WCHAR2.data(), targetIp.c_str(), report.int64_ConnectionTime, 0,
                report.int64_logID, report.int64_Guid1, report.int64_Guid2, report.data[0], report.data[1], report.data[2],
                report.data[3], report.data[4], report.data[5], report.data[6], report.data[7], report.data[8],
                report.data[9], report.str260_WCHAR3.data(), report.str260_WCHAR5.data(), report.str260_WCHAR6.data(),
                report.str260_WCHAR7.data(), 0
            );
            break;
        }

        default:
            break;
    }
}

int NCGuardSendHandler(const std::uint64_t uniqueId, void* const data, const std::uint32_t size) {
    if (uniqueId == 0U) {
        return -1;
    }

    NCGuardQueueEntry entry;
    entry.uniqueId = static_cast<std::uint32_t>(uniqueId);
    entry.data = CopyVendorPayload(data, size);
    entry.size = size;
    CNCGuardManager::inst().PushMessageQueue(entry);
    return 0;
}

int NCGuardEventHandler(
    const std::uint64_t uniqueId,
    const NCG_EVENT_TYPE eventType,
    void* const data,
    const std::uint32_t size,
    const std::uint32_t flags
) {
    if ((flags & 1U) != 0U) {
        NCGuardQueueEntry entry;
        entry.uniqueId = static_cast<std::uint32_t>(uniqueId);
        SetEventType(entry, eventType);
        entry.data = CopyVendorPayload(data, size);
        entry.size = size;
        CNCGuardManager::inst().PushLogQueue(entry);
    }
    return 0;
}
