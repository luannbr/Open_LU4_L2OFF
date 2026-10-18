#pragma once

#include "AuthType.h"
#include "Lock.h"

#include <cstdint>
#include <map>

class CClientSession;
class CIOSocket;
class GradeSocket;

// Original owner: Src/gradeservermgr.h. L2Authd.pdb type 0x4C76,
// target size 0x16A4.
struct GradeTrans {
    static GradeTrans* Alloc();
    void AddRef();
    void ReleaseRef();

    CClientSession* pClientSession;
    PreLoginUser preLoginUser;
    char mOrigCmd;
    std::int32_t opTime;
    std::int32_t mRefCount;
};

// Original owner: Src/gradeservermgr.h. L2Authd.pdb type 0x5D3B,
// target size 0x3C. The native map keeps the portable host ABI. All actual
// PDB-recorded members and the complete compiled method surface remain owned
// here.
class GradeServerMgr {
public:
    using GradeTransMap = std::map<int, GradeTrans*>;

    GradeServerMgr();
    virtual ~GradeServerMgr();

    void Initialize();
    void GradeServerTimerRoutine();
    bool AddGradeTrans(int tid, GradeTrans* gradeTrans);
    bool DelGradeTrans(int tid, GradeTrans** gradeTrans);
    char RequestGrade(
        CClientSession* pSession,
        PreLoginUser& prelogin,
        char origCmd);
    bool RecvGrade(
        CIOSocket* pIOSocket,
        const unsigned char* packet);

    GradeSocket* mpGradeSocket;
    bool mGradeServerConnected;
    void* mhGradeServerTimer;

    static GradeServerMgr* Inst() {
        return sInstance;
    }

private:
    GradeTransMap mGradeTransMap;
    CLock mTransMapLock;
    std::int32_t mTransId;

    static GradeServerMgr* sInstance;
};
