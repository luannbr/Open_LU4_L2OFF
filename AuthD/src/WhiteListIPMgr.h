#pragma once

#include "Lock.h"

#include <cstdint>

class CIpListMonitor;

// Original owner: Src/whitelistipmgr.h. L2Authd.pdb type 0x72D4.
enum WLIPINFO_COMPARERESULT : std::int32_t {
    WLIPINFO_COMPARERESULT_PARAM_IS_EXCLUSIVE_OVER_RANGE = 1,
    WLIPINFO_COMPARERESULT_PARAM_IS_EXCLUSIVE_UNDER_RANGE = 2,
    WLIPINFO_COMPARERESULT_EQUAL = 3,
    WLIPINFO_COMPARERESULT_PARAM_IS_SUPERSET = 4,
    WLIPINFO_COMPARERESULT_PARAM_IS_SUBSET = 5,
    WLIPINFO_COMPARERESULT_PARAM_INTERSECTION_OVER_RANGE = 6,
    WLIPINFO_COMPARERESULT_PARAM_INTERSECTION_UNDER_RANGE = 7,
};

// Original owner: Src/whitelistipmgr.h. L2Authd.pdb type 0x72F5,
// field list 0x72F4, target size 0x10.
class WLIPInfo {
public:
    explicit WLIPInfo(unsigned char* from);
    WLIPInfo(unsigned char* from, unsigned char* to);
    ~WLIPInfo();

    void addItem(
        WLIPInfo** root,
        WLIPInfo* item,
        int* count);
    WLIPINFO_COMPARERESULT compare(WLIPInfo* item);
    int compare(unsigned char* address);
    bool isRange();
    int compareWithFrom(unsigned char* address);
    int compareWithTo(unsigned char* address);
    void setToFrom(unsigned char* address);
    void getFromIp(unsigned char* address);
    void printLog();

private:
    bool mIsRange;
    unsigned char fromIP[4];
    unsigned char toIP[4];

public:
    WLIPInfo* next;
};

// Original owner: Src/whitelistipmgr.h. L2Authd.pdb type 0x72EE,
// field list 0x72ED, target size 0x134. The selected build does not expose a
// separately compiled header unit, so this declaration records the complete
// public method surface and field order used by its consumers.
class WhiteListIPMgr {
public:
    WhiteListIPMgr();
    ~WhiteListIPMgr();

    int Load(const char* fileName);
    int Reload();
    int GetIPNum();
    int GetErrorLine();
    bool IsWhiteIP(unsigned char* address);

private:
    char mFileName[260];
    WLIPInfo** IPList;
    std::int32_t IPCount;
    std::int32_t errorLine;
    CLock mListLock;
    CIpListMonitor* fileMonitor;
};

extern WhiteListIPMgr whiteListIPMgr;

unsigned int MonitorCallback(void* parameter);
void* WhiteListIPMgr_LoadBinary(
    const char* fileName,
    int* size,
    bool zeroTerminate);
