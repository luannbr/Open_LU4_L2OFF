#include "WhiteListIPMgr.h"

#include "Shared/src/ScopedLockGuards.h"
#include "Shared/src/Runtime/OperatorLog.h"

#include "IPListMonitor.h"
#include "Log.h"
#include "Parser.h"
#include "Util.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <sstream>
#include <string>

WhiteListIPMgr whiteListIPMgr;

// L2Authd.exe 0x00491DD0, Src/whitelistipmgr.cpp:41-48.
WLIPInfo::WLIPInfo(unsigned char* from)
    : mIsRange(false),
      next(nullptr) {
    fromIP[0] = from[0];
    fromIP[1] = from[1];
    fromIP[2] = from[2];
    fromIP[3] = from[3];
}

// L2Authd.exe 0x00491E70, Src/whitelistipmgr.cpp:50-61.
WLIPInfo::WLIPInfo(
    unsigned char* from,
    unsigned char* to)
    : mIsRange(true),
      next(nullptr) {
    fromIP[0] = from[0];
    fromIP[1] = from[1];
    fromIP[2] = from[2];
    fromIP[3] = from[3];
    toIP[0] = to[0];
    toIP[1] = to[1];
    toIP[2] = to[2];
    toIP[3] = to[3];
}

// L2Authd.exe 0x00491F80, Src/whitelistipmgr.cpp:63-65.
WLIPInfo::~WLIPInfo() {
    delete next;
}

// L2Authd.exe 0x00491FD0, Src/whitelistipmgr.cpp:68-112.
void WLIPInfo::addItem(
    WLIPInfo** root,
    WLIPInfo* item,
    int* count) {
    if (item == nullptr) {
        return;
    }

    switch (compare(item)) {
    case WLIPINFO_COMPARERESULT_PARAM_IS_EXCLUSIVE_OVER_RANGE:
        if (next == nullptr) {
            next = item;
            ++*count;
        } else {
            next->addItem(&next, item, count);
        }
        break;

    case WLIPINFO_COMPARERESULT_PARAM_IS_EXCLUSIVE_UNDER_RANGE:
        item->next = this;
        *root = item;
        ++*count;
        break;

    case WLIPINFO_COMPARERESULT_EQUAL:
    case WLIPINFO_COMPARERESULT_PARAM_IS_SUBSET:
        delete item;
        break;

    case WLIPINFO_COMPARERESULT_PARAM_INTERSECTION_OVER_RANGE:
        item->setToFrom(fromIP);
        [[fallthrough]];

    case WLIPINFO_COMPARERESULT_PARAM_IS_SUPERSET:
        if (next == nullptr) {
            *root = item;
            delete this;
        } else {
            *root = next;
            next = nullptr;
            --*count;
            (*root)->addItem(root, item, count);
            delete this;
        }
        break;

    case WLIPINFO_COMPARERESULT_PARAM_INTERSECTION_UNDER_RANGE:
        item->getFromIp(fromIP);
        delete item;
        break;
    }
}

// L2Authd.exe 0x004921B0, Src/whitelistipmgr.cpp:115-163.
WLIPINFO_COMPARERESULT WLIPInfo::compare(WLIPInfo* item) {
    if (!mIsRange) {
        if (item->isRange()) {
            int result = item->compareWithFrom(fromIP);
            if (result <= 0) {
                result = item->compareWithTo(fromIP);
                if (result < 0) {
                    return
                        WLIPINFO_COMPARERESULT_PARAM_IS_EXCLUSIVE_UNDER_RANGE;
                }
                return WLIPINFO_COMPARERESULT_PARAM_IS_SUPERSET;
            }
            return
                WLIPINFO_COMPARERESULT_PARAM_IS_EXCLUSIVE_OVER_RANGE;
        }

        const int result = item->compareWithFrom(fromIP);
        if (result <= 0) {
            if (result < 0) {
                return
                    WLIPINFO_COMPARERESULT_PARAM_IS_EXCLUSIVE_UNDER_RANGE;
            }
            return WLIPINFO_COMPARERESULT_EQUAL;
        }
        return WLIPINFO_COMPARERESULT_PARAM_IS_EXCLUSIVE_OVER_RANGE;
    }

    if (!item->isRange()) {
        int result = item->compareWithFrom(fromIP);
        if (result < 0) {
            return
                WLIPINFO_COMPARERESULT_PARAM_IS_EXCLUSIVE_UNDER_RANGE;
        }
        result = item->compareWithFrom(toIP);
        if (result <= 0) {
            return WLIPINFO_COMPARERESULT_PARAM_IS_SUBSET;
        }
        return WLIPINFO_COMPARERESULT_PARAM_IS_EXCLUSIVE_OVER_RANGE;
    }

    int result = item->compareWithFrom(toIP);
    if (result > 0) {
        return WLIPINFO_COMPARERESULT_PARAM_IS_EXCLUSIVE_OVER_RANGE;
    }

    result = item->compareWithTo(fromIP);
    if (result < 0) {
        return WLIPINFO_COMPARERESULT_PARAM_IS_EXCLUSIVE_UNDER_RANGE;
    }

    result = item->compareWithFrom(fromIP);
    if (result <= 0) {
        if (result < 0) {
            result = item->compareWithTo(toIP);
            if (result < 0) {
                return
                    WLIPINFO_COMPARERESULT_PARAM_INTERSECTION_UNDER_RANGE;
            }
            return WLIPINFO_COMPARERESULT_PARAM_IS_SUPERSET;
        }

        result = item->compareWithTo(toIP);
        if (result <= 0) {
            if (result < 0) {
                return WLIPINFO_COMPARERESULT_PARAM_IS_SUBSET;
            }
            return WLIPINFO_COMPARERESULT_EQUAL;
        }
        return WLIPINFO_COMPARERESULT_PARAM_IS_SUPERSET;
    }

    result = item->compareWithTo(toIP);
    if (result <= 0) {
        return WLIPINFO_COMPARERESULT_PARAM_IS_SUBSET;
    }
    return WLIPINFO_COMPARERESULT_PARAM_INTERSECTION_OVER_RANGE;
}

// L2Authd.exe 0x004923C0, Src/whitelistipmgr.cpp:166-177.
int WLIPInfo::compare(unsigned char* address) {
    if (!mIsRange) {
        return compareWithFrom(address);
    }

    int result = compareWithFrom(address);
    if (result <= 0) {
        result = compareWithTo(address);
        if (result >= 0) {
            result = 0;
        }
    }
    return result;
}

// L2Authd.exe 0x00492420, Src/whitelistipmgr.cpp:180-182.
bool WLIPInfo::isRange() {
    return mIsRange;
}

// L2Authd.exe 0x00492430, Src/whitelistipmgr.cpp:185-191.
int WLIPInfo::compareWithFrom(unsigned char* address) {
    for (int index = 0; index < 4; ++index) {
        if (fromIP[index] < address[index]) {
            return -1;
        }
        if (fromIP[index] > address[index]) {
            return 1;
        }
    }
    return 0;
}

// L2Authd.exe 0x004924A0, Src/whitelistipmgr.cpp:194-200.
int WLIPInfo::compareWithTo(unsigned char* address) {
    for (int index = 0; index < 4; ++index) {
        if (toIP[index] < address[index]) {
            return -1;
        }
        if (toIP[index] > address[index]) {
            return 1;
        }
    }
    return 0;
}

// L2Authd.exe 0x00492510, Src/whitelistipmgr.cpp:203-208.
void WLIPInfo::setToFrom(unsigned char* address) {
    fromIP[0] = address[0];
    fromIP[1] = address[1];
    fromIP[2] = address[2];
    fromIP[3] = address[3];
}

// L2Authd.exe 0x004925A0, Src/whitelistipmgr.cpp:211-216.
void WLIPInfo::getFromIp(unsigned char* address) {
    address[0] = fromIP[0];
    address[1] = fromIP[1];
    address[2] = fromIP[2];
    address[3] = fromIP[3];
}

// L2Authd.exe 0x00492630, Src/whitelistipmgr.cpp:218-225.
void WLIPInfo::printLog() {
    if (mIsRange) {
        // Log.AddLog(
        //     LOG_DEBUG,
        //     "%d.%d.%d.%d - %d.%d.%d.%d",
        //     fromIP[0],
        //     fromIP[1],
        //     fromIP[2],
        //     fromIP[3],
        //     toIP[0],
        //     toIP[1],
        //     toIP[2],
        //     toIP[3])
        // Noise: The load summary already reports the file and entry count. Do not dump every allowlist entry.
        // Log.AddLog(
        //     LOG_DEBUG,
        //     "component=ip_allowlist event=entry_loaded kind=range first=%u.%u.%u.%u last=%u.%u.%u.%u",
        //     static_cast<unsigned int>(fromIP[0]), static_cast<unsigned int>(fromIP[1]), static_cast<unsigned int>(fromIP[2]), static_cast<unsigned int>(fromIP[3]), static_cast<unsigned int>(toIP[0]), static_cast<unsigned int>(toIP[1]), static_cast<unsigned int>(toIP[2]), static_cast<unsigned int>(toIP[3]))
    } else {
        // Log.AddLog(
        //     LOG_DEBUG,
        //     "%d.%d.%d.%d",
        //     fromIP[0],
        //     fromIP[1],
        //     fromIP[2],
        //     fromIP[3])
        // Noise: The load summary already reports the file and entry count. Do not dump every allowlist entry.
        // Log.AddLog(
        //     LOG_DEBUG,
        //     "component=ip_allowlist event=entry_loaded kind=address address=%u.%u.%u.%u",
        //     static_cast<unsigned int>(fromIP[0]), static_cast<unsigned int>(fromIP[1]), static_cast<unsigned int>(fromIP[2]), static_cast<unsigned int>(fromIP[3]))
    }
}

// L2Authd.exe 0x00492740, Src/whitelistipmgr.cpp:228-233.
WhiteListIPMgr::WhiteListIPMgr()
    : IPList(nullptr),
      IPCount(0),
      mListLock(eCriticalSection, 0),
      fileMonitor(nullptr) {
    mFileName[0] = '\0';
}

// L2Authd.exe 0x004927A0, Src/whitelistipmgr.cpp:236-246.
WhiteListIPMgr::~WhiteListIPMgr() {
    if (IPList != nullptr) {
        for (int index = 0; index < IPCount; ++index) {
            delete IPList[index];
        }
        delete[] IPList;
    }
    delete fileMonitor;
}

// L2Authd.exe 0x004928B0, Src/whitelistipmgr.cpp:249-485.
int WhiteListIPMgr::Load(const char* fileName) {
    try {
        if (fileName == nullptr) {
            fileName = mFileName;
            if (mFileName[0] == '\0') {
                // Log.AddLog(LOG_WARN,
                //     "component=ip_allowlist event=load_failed reason=no_configured_path result_code=1 action=retain_active_list")
                // Log.AddLog(LOG_WARN,
                //     "[ip-list] No allowlist path is configured. Keeping the active list (result code 1).")
                L2_LOG_WARN(Log,
                    "[ip-list] No allowlist path is configured. Keeping the active list (result code 1).");
                return 1;
            }
        } else {
            mFileName[0] = '\0';
        }

        int fileSize = 0;
        std::unique_ptr<char[]> fileData(
            static_cast<char*>(
                WhiteListIPMgr_LoadBinary(
                    fileName,
                    &fileSize,
                    false)));
        if (fileData == nullptr) {
            return 1;
        }

        if (mFileName[0] == '\0') {
            std::strcpy(mFileName, fileName);
        }
        if (fileMonitor == nullptr) {
            std::array<char, 260> monitorFile{};
            std::strncpy(
                monitorFile.data(),
                fileName,
                monitorFile.size());
            monitorFile[259] = '\0';

            std::array<wchar_t, 260> monitorFileWide{};
            [[maybe_unused]] const int convertedLength =
                AnsiToUnicode(
                    monitorFile.data(),
                    static_cast<int>(monitorFile.size()),
                    monitorFileWide.data(),
                    static_cast<int>(monitorFileWide.size()));
            fileMonitor =
                new CIpListMonitor(monitorFileWide.data());
            fileMonitor->StartMonitor(
                MonitorCallback,
                this);
        }

        if (fileSize == 0) {
            // Log.AddLog(LOG_WARN,
            //     "component=ip_allowlist event=load_skipped path=\"%s\" reason=empty_file result_code=3 action=retain_active_list",
            //     fileName)
            // Log.AddLog(LOG_WARN,
            //     "[ip-list] Allowlist \"%s\" is empty. Keeping the active list (result code 3).",
            //     fileName)
            L2_LOG_WARN(Log,
                "[ip-list] Allowlist \"%s\" is empty. Keeping the active list (result code 3).",
                fileName);
            return 3;
        }

        std::istringstream input(
            std::string(
                fileData.get(),
                static_cast<std::size_t>(fileSize)));
        input >> std::noskipws;

        enum State {
            eWaitFirstIP = 0,
            eWaitFirstIPDot = 1,
            eFirstIP = 2,
            eFirstIPDone = 3,
            eWaitSecondIP = 4,
            eWaitSecondIPDot = 5,
            eSecondIP = 6,
            eSecondIPDone = 7,
            eInComment = 8,
        };

        std::array<char, 260> tokenValue{};
        std::array<unsigned char, 4> firstAddress{};
        std::array<unsigned char, 4> secondAddress{};
        unsigned char firstIndex = 0;
        unsigned char secondIndex = 0;
        State state = eWaitFirstIP;
        int line = 1;
        int itemCount = 0;
        errorLine = 0;
        WLIPInfo* list = nullptr;

        const auto discardList = [&list] {
            delete list;
            list = nullptr;
        };
        const auto failFormat = [&] {
            errorLine = line;
            const char* expected = "an address entry";
            switch (state) {
            case eWaitFirstIP: expected = "an address, a ';' comment, or end of line"; break;
            case eWaitFirstIPDot:
            case eWaitSecondIPDot: expected = "'.' between address components"; break;
            case eFirstIP:
            case eWaitSecondIP:
            case eSecondIP: expected = "a numeric address component"; break;
            case eFirstIPDone: expected = "'-' followed by a range end, or end of line"; break;
            case eSecondIPDone: expected = "end of line after the address range"; break;
            case eInComment: expected = "end of comment"; break;
            }
            // Log.AddLog(LOG_ERROR,
            //     "component=ip_allowlist event=load_failed path=\"%s\" reason=invalid_format line=%d parser_state=%d result_code=2 action=retain_active_list",
            //     fileName, line, static_cast<int>(state))
            // Log.AddLog(LOG_ERROR,
            //     "[ip-list] Invalid allowlist \"%s\" at line %d (parser state %d). Keeping the active list (result code 2).",
            //     fileName, line, static_cast<int>(state))
            // L2_LOG_ERROR(Log,
            //     "[ip-list] Invalid allowlist \"%s\" at line %d (parser state %d). Keeping the active list (result code 2).",
            //     fileName, line, static_cast<int>(state))
            L2_LOG_ERROR(Log,
                "[ip-list] Invalid allowlist \"%s\" at line %d: expected %s (parser state %d). Keeping the active list (result code 2).",
                fileName, line, expected, static_cast<int>(state));
            discardList();
            return 2;
        };
        const auto appendPending = [&] {
            WLIPInfo* item = nullptr;
            if (state == eFirstIPDone) {
                item = new WLIPInfo(firstAddress.data());
            } else if (state == eSecondIPDone) {
                item = new WLIPInfo(
                    firstAddress.data(),
                    secondAddress.data());
            }

            if (list == nullptr) {
                list = item;
                ++itemCount;
            } else {
                list->addItem(
                    &list,
                    item,
                    &itemCount);
            }
            state = eWaitFirstIP;
            firstIndex = 0;
            secondIndex = 0;
            ++line;
        };

        for (;;) {
            const Token token =
                GetToken(
                    input,
                    tokenValue.data(),
                    false);

            if (token == eEOF) {
                if (state == eFirstIPDone ||
                    state == eSecondIPDone) {
                    appendPending();
                } else if (state != eInComment &&
                           state != eWaitFirstIP) {
                    return failFormat();
                }
                break;
            }

            if (token == eNewLine) {
                if (state == eFirstIPDone ||
                    state == eSecondIPDone) {
                    appendPending();
                } else {
                    if (state != eInComment &&
                        state != eWaitFirstIP) {
                        return failFormat();
                    }
                    state = eWaitFirstIP;
                    firstIndex = 0;
                    secondIndex = 0;
                    ++line;
                }
                continue;
            }

            if (state == eWaitFirstIP) {
                if (token == eSemiColon) {
                    state = eInComment;
                } else {
                    if (token != eNumber) {
                        return failFormat();
                    }
                    firstAddress[firstIndex] =
                        static_cast<unsigned char>(
                            std::atoi(tokenValue.data()));
                    ++firstIndex;
                    state = eWaitFirstIPDot;
                }
                continue;
            }

            if (state == eWaitFirstIPDot) {
                if (token != ePeriod) {
                    return failFormat();
                }
                state = eFirstIP;
                continue;
            }

            if (state == eFirstIP) {
                if (token != eNumber) {
                    return failFormat();
                }
                firstAddress[firstIndex] =
                    static_cast<unsigned char>(
                        std::atoi(tokenValue.data()));
                ++firstIndex;
                state = firstIndex == 4
                    ? eFirstIPDone
                    : eWaitFirstIPDot;
                continue;
            }

            if (state == eFirstIPDone) {
                if (token != eMinus) {
                    return failFormat();
                }
                state = eWaitSecondIP;
                continue;
            }

            if (state == eWaitSecondIP) {
                if (token != eNumber) {
                    return failFormat();
                }
                secondAddress[secondIndex] =
                    static_cast<unsigned char>(
                        std::atoi(tokenValue.data()));
                ++secondIndex;
                state = eWaitSecondIPDot;
                continue;
            }

            if (state == eWaitSecondIPDot) {
                if (token != ePeriod) {
                    return failFormat();
                }
                state = eSecondIP;
                continue;
            }

            if (state == eSecondIP) {
                if (token != eNumber) {
                    return failFormat();
                }
                secondAddress[secondIndex] =
                    static_cast<unsigned char>(
                        std::atoi(tokenValue.data()));
                ++secondIndex;
                state = secondIndex == 4
                    ? eSecondIPDone
                    : eWaitSecondIPDot;
                continue;
            }

            if (state != eInComment) {
                return failFormat();
            }
        }

        fileData.reset();
        if (itemCount == 0) {
            // Log.AddLog(LOG_WARN,
            //     "component=ip_allowlist event=load_skipped path=\"%s\" reason=no_address_entries result_code=3 action=retain_active_list",
            //     fileName)
            // Log.AddLog(LOG_WARN,
            //     "[ip-list] Allowlist \"%s\" contains no address entries. Keeping the active list (result code 3).",
            //     fileName)
            L2_LOG_WARN(Log,
                "[ip-list] Allowlist \"%s\" contains no address entries. Keeping the active list (result code 3).",
                fileName);
            return 3;
        }

        {
            L2_GUARD(mListLock);
            if (IPList != nullptr) {
                for (int index = 0; index < IPCount; ++index) {
                    delete IPList[index];
                }
                delete[] IPList;
            }

            IPCount = itemCount;
            IPList = new WLIPInfo*[static_cast<std::size_t>(IPCount)];
            WLIPInfo* item = list;
            int index = 0;
            while (item != nullptr) {
                IPList[index] = item;
                ++index;
                item = item->next;
            }
            for (index = 0; index < IPCount; ++index) {
                IPList[index]->next = nullptr;
            }

            // Log.AddLog(LOG_DEBUG, "=== WhiteListAdded ===")
            // Log.AddLog(
            //     LOG_NORMAL,
            //     "component=ip_allowlist event=list_loaded path=\"%s\" entries=%d action=replace_active_list effect=exempt_from_ip_connection_limit",
            //     fileName, IPCount)
            // Log.AddLog(
            //     LOG_NORMAL,
            //     "[ip-list] Loaded \"%s\" with %d entries and replaced the active allowlist. These addresses are exempt from the IP connection limit.",
            //     fileName, IPCount)
            L2_LOG_INFO(Log,
                "[ip-list] Loaded \"%s\" with %d entries and replaced the active allowlist. These addresses are exempt from the IP connection limit.",
                fileName, IPCount);
            for (index = 0; index < IPCount; ++index) {
                IPList[index]->printLog();
            }
        }
        return 0;
    } catch (...) {
        // Log.AddLog(LOG_ERROR,
        //     "component=ip_allowlist event=load_failed path=\"%.259s\" reason=exception result_code=1 active_list=unknown action=return_failure",
        //     fileName != nullptr ? fileName : "(unset)")
        // Log.AddLog(LOG_ERROR,
        //     "[ip-list] Loading \"%.259s\" threw an exception (result code 1). The active list may have changed.",
        //     fileName != nullptr ? fileName : "(unset)")
        // L2_LOG_ERROR(Log,
        //     "[ip-list] Loading \"%.259s\" threw an exception (result code 1). The active list may have changed.",
        //     fileName != nullptr ? fileName : "(unset)")
        // The exception may be allocation failure, including failure inside
        // the ordinary logger. Keep this final diagnostic allocation-free.
        const char* const path = fileName != nullptr ? fileName : "(unset)";
        std::size_t pathLength = 0;
        while (pathLength < 259 && path[pathLength] != '\0') ++pathLength;
        char message[512]{};
        std::snprintf(message, sizeof(message),
            "[ip-list] Loading \"%.259s\"%s threw an exception (result code 1). The active list may have changed.",
            path, path[pathLength] != '\0' ? " [path truncated]" : "");
        runtime::logging::Write(
            "authd", runtime::logging::Level::Error, message, __FILE__);
        // The selected function's terminal EH landing pad returns 1.
        return 1;
    }
}

// L2Authd.exe 0x004939D0, Src/whitelistipmgr.cpp:487-489.
int WhiteListIPMgr::Reload() {
    return Load(nullptr);
}

// L2Authd.exe 0x004939F0, Src/whitelistipmgr.cpp:492-494.
int WhiteListIPMgr::GetIPNum() {
    return IPCount;
}

// L2Authd.exe 0x00493A10, Src/whitelistipmgr.cpp:497-499.
int WhiteListIPMgr::GetErrorLine() {
    return errorLine;
}

// L2Authd.exe 0x00493A30, Src/whitelistipmgr.cpp:502-517.
bool WhiteListIPMgr::IsWhiteIP(
    unsigned char* address) {
    {
        L2_GUARD(mListLock);
        int first = 0;
        int last = IPCount;
        do {
            while (first < last) {
                const int middle = (first + last) / 2;
                const int result =
                    IPList[middle]->compare(address);
                if (result < 0) {
                    first = middle + 1;
                } else {
                    last = middle;
                    if (result == 0) {
                        return true;
                    }
                }
            }
            return false;
        } while (false);
    }
}

// L2Authd.exe 0x00493AE0, Src/whitelistipmgr.cpp:520-538.
unsigned int MonitorCallback(void* parameter) {
    auto* const manager =
        static_cast<WhiteListIPMgr*>(parameter);
    const int result = manager->Reload();
    if (result == 1) {
        // Noise: WhiteListIPMgr::Load and its file loader report the specific result for both initial load and monitor reload. Omit this duplicate.
        // Log.AddLog(
        //     LOG_WARN,
        //     "White List IP File does not exist or wrong path")
    } else if (result == 2) {
        // Noise: WhiteListIPMgr::Load and its file loader report the specific result for both initial load and monitor reload. Omit this duplicate.
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Invalid White List IP Format (Line:%d)",
        //     manager->GetErrorLine())
    } else if (result == 3) {
        // Noise: WhiteListIPMgr::Load and its file loader report the specific result for both initial load and monitor reload. Omit this duplicate.
        // Log.AddLog(
        //     LOG_WARN,
        //     "No White List IP Loaded")
    } else {
        // Noise: WhiteListIPMgr::Load and its file loader report the specific result for both initial load and monitor reload. Omit this duplicate.
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "%d of White List IP Loaded",
        //     manager->GetIPNum())
    }
    return 0;
}

// L2Authd.exe 0x00493B80, Src/whitelistipmgr.cpp:12-39.
void* WhiteListIPMgr_LoadBinary(
    const char* fileName,
    int* size,
    bool zeroTerminate) {
    std::FILE* const file =
        std::fopen(fileName, "rb");
    if (file == nullptr) {
        // Log.AddLog(LOG_ERROR,
        //     "component=ip_allowlist event=load_failed path=\"%s\" stage=open error_domain=errno code=%d action=retain_active_list",
        //     fileName, errno)
        // Log.AddLog(LOG_ERROR,
        //     "[ip-list] Could not open \"%s\" (errno %d). Keeping the active allowlist.",
        //     fileName, errno)
        L2_LOG_ERROR(Log,
            "[ip-list] Could not open \"%s\" (errno %d). Keeping the active allowlist.",
            fileName, errno);
        return nullptr;
    }

    if (std::fseek(file, 0, SEEK_END) != 0) {
        // Log.AddLog(LOG_ERROR,
        //     "component=ip_allowlist event=load_failed path=\"%s\" stage=seek_end error_domain=errno code=%d action=retain_active_list",
        //     fileName, errno)
        // Log.AddLog(LOG_ERROR,
        //     "[ip-list] Could not seek to the end of \"%s\" (errno %d). Keeping the active allowlist.",
        //     fileName, errno)
        L2_LOG_ERROR(Log,
            "[ip-list] Could not seek to the end of \"%s\" (errno %d). Keeping the active allowlist.",
            fileName, errno);
        std::fclose(file);
        return nullptr;
    }
    const long fileSize = std::ftell(file);
    if (fileSize < 0 ||
        std::fseek(file, 0, SEEK_SET) != 0) {
        // Log.AddLog(LOG_ERROR,
        //     "component=ip_allowlist event=load_failed path=\"%s\" stage=%s error_domain=errno code=%d action=retain_active_list",
        //     fileName, fileSize < 0 ? "tell_size" : "seek_start", errno)
        // Log.AddLog(LOG_ERROR,
        //     "[ip-list] Could not load \"%s\" while %s (errno %d). Keeping the active allowlist.",
        //     fileName, fileSize < 0 ? "reading the file size" : "seeking to the beginning", errno)
        L2_LOG_ERROR(Log,
            "[ip-list] Could not load \"%s\" while %s (errno %d). Keeping the active allowlist.",
            fileName, fileSize < 0 ? "reading the file size" : "seeking to the beginning", errno);
        std::fclose(file);
        return nullptr;
    }

    const std::size_t allocationSize =
        static_cast<std::size_t>(fileSize) +
        (zeroTerminate ? 1U : 0U);
    auto* const data = static_cast<unsigned char*>(
        ::operator new[](allocationSize, std::nothrow));
    if (data == nullptr) {
        // Log.AddLog(LOG_ERROR,
        //     "component=ip_allowlist event=load_failed path=\"%s\" reason=allocation_failed requested_bytes=%zu action=retain_active_list",
        //     fileName, allocationSize)
        // Log.AddLog(LOG_ERROR,
        //     "[ip-list] Could not allocate a buffer for \"%s\" (%zu bytes). Keeping the active allowlist.",
        //     fileName, allocationSize)
        // L2_LOG_ERROR(Log,
        //     "[ip-list] Could not allocate a buffer for \"%s\" (%zu bytes). Keeping the active allowlist.",
        //     fileName, allocationSize)
        // Avoid allocating a formatted record after buffer allocation failed.
        char message[1024]{};
        std::size_t pathPrefixLength = 0;
        while (pathPrefixLength < 700 && fileName[pathPrefixLength] != '\0') {
            ++pathPrefixLength;
        }
        const bool pathTruncated = fileName[pathPrefixLength] != '\0';
        std::snprintf(message, sizeof(message),
            "[ip-list] Cannot allocate %zu bytes for the allowlist. Keeping the active list. File: %.700s%s",
            allocationSize, fileName, pathTruncated ? " [path truncated]" : "");
        runtime::logging::Write(
            "authd", runtime::logging::Level::Error, message, __FILE__);
        std::fclose(file);
        return nullptr;
    }

    [[maybe_unused]] const std::size_t bytesRead =
        std::fread(
            data,
            1,
            static_cast<std::size_t>(fileSize),
            file);
    if (bytesRead != static_cast<std::size_t>(fileSize)) {
        // Log.AddLog(LOG_ERROR,
        //     "component=ip_allowlist event=file_read_incomplete path=\"%s\" expected_bytes=%ld initialized_bytes=%zu reported_bytes=%d reason=%s action=return_buffer_with_unread_tail",
        //     fileName, fileSize, bytesRead, static_cast<int>(fileSize),
        //     std::ferror(file) != 0 ? "read_error" : "unexpected_eof")
        // Log.AddLog(LOG_ERROR,
        //     "[ip-list] Incomplete read of \"%s\": expected %ld bytes, initialized %zu, reported %d (%s). The buffer still contains an unread tail.",
        //     fileName, fileSize, bytesRead, static_cast<int>(fileSize),
        //     std::ferror(file) != 0 ? "read error" : "unexpected end of file")
        // L2_LOG_ERROR(Log,
        //     "[ip-list] Incomplete read of \"%s\": expected %ld bytes, initialized %zu, reported %d (%s). The buffer still contains an unread tail.",
        //     fileName, fileSize, bytesRead, static_cast<int>(fileSize),
        //     std::ferror(file) != 0 ? "read error" : "unexpected end of file")
        L2_LOG_ERROR(Log,
            "[ip-list] Incomplete read of \"%s\": expected %ld bytes, read %zu, passing %d to the parser (%s). Parsing continues with an uninitialized tail; the active list may change.",
            fileName, fileSize, bytesRead, static_cast<int>(fileSize),
            std::ferror(file) != 0 ? "read error" : "unexpected end of file");
    }
    std::fclose(file);
    if (size != nullptr) {
        *size = static_cast<int>(fileSize);
    }
    if (zeroTerminate) {
        data[fileSize] = 0;
    }
    return data;
}
