#pragma once

#include "Platform/WindowsTypes.h"

class CDes;
class CMailReport;
class CServiceManager;
struct HINSTANCE__;
struct HWND__;

// L2Authd.pdb type 0x85FC.
enum Result : int {
    Continue = 0,
    OkStop = 1,
    ErrorStop = 2,
};

// L2Authd.pdb type 0x865B, sizeof one. Winsock startup/cleanup are the only
// target side effects. Non-Windows hosts need no matching global socket
// subsystem initialization.
struct CWsaInitializer {
    CWsaInitializer();
    ~CWsaInitializer();
};

using EncPwdType = void (*)(char* password);

// Original process globals from Src/L2AuthD.cpp. The selected PDB records
// these at .data offsets 48432..48464 and 94884/94888.
extern HWND__* mainWnd;
extern HWND__* logWnd;
extern HWND__* reporterWnd;
extern HWND__* buttonWnd;
extern HWND__* reloadButtonWnd;
extern HINSTANCE__* g_instance;
extern HWND__* ShutdownWnd;
extern bool globalTeminateEvent;
extern bool g_bServiceStop;
extern EncPwdType EncPwd;
extern CServiceManager g_serviceManager;
extern CMailReport reportMail;
extern CDes DesObject;
extern int g_updateKey;
extern int g_updateKey2;

int service_main(int argumentCount, char** arguments);
