#pragma once

#include "AuthType.h"

#include <cstdint>
#include <cstring>

struct HINSTANCE__;
class CFileLog;

// Original owner: Src/ifatigue.h:22-42. L2Authd.pdb type 0x5DB8, target
// size 0x120. This is the exact carrier passed to the optional external
// fatigue module. Only native pointers follow the portable host ABI.
class FModule {
public:
    // L2Authd.exe 0x00447410, Src/ifatigue.h:32-42.
    FModule()
        : mhIOThreadIOCP(nullptr),
          mpPacketLog(nullptr),
          mCompanyCode(1),
          // The selected constructor performs a zero-length memset at +0x0C,
          // so mPAIP remains uninitialized here as well.
          mPAPort(0),
          mPAReconnectInterval(30),
          mGameId(0) {
        std::memset(mLogDirectory, 0, sizeof(mLogDirectory));
    }

    void* mhIOThreadIOCP;
    CFileLog* mpPacketLog;
    int mCompanyCode;
    in_addr mPAIP;
    int mPAPort;
    int mPAReconnectInterval;
    int mGameId;
    char mLogDirectory[260];
};

// Original owner: Src/ifatigue.h. L2Authd.pdb type 0x8664, target size
// 0x04. The DLL handle is opaque in the portable baseline.
// the external module ABI remains owned by Src/ifatigue.cpp.
class IFatigue {
public:
    int InitializeDll();
    int FinalizeDll();
    int FatigueLogin(int uid, LoginUser* user);
    int FatigueLogOut(int uid, const LoginUser& user);

    static int FatigueCallback(
        int cmd,
        int uid,
        int result,
        char* ptid,
        int onTime,
        int offTime);

private:
    HINSTANCE__* mDllHandle;
};

extern IFatigue fatigueMgr;
