#pragma once

#include "Shared/src/BaseStructure.h"
#include "Shared/src/ScopedLockGuards.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

class User;

class IPacketHelper;

namespace l2geo {


// SuperPoint.h. The current image allocates this object with 0x98 bytes.
// CSuperPointManager::Load fixes all member offsets of the
// older adjacent PDB's larger xvector/xmap carriers.
class CSuperPoint {
public:
    struct SuperPointNode {
        int Index = 0;
        FVector Pos;
    };

    enum class SPMoveType : int {
        FollowRail = 0,
        Random = 1,
        FollowRailRestart = 2,
        FollowRailRestartTeleport = 3,
    };

    using SuperPointNodes = std::vector<SuperPointNode*>;
    using SuperPointNodeMap = std::map<int, SuperPointNodes*>;

    CSuperPoint();

    SuperPointNodes* GetPath(int startIndex, int endIndex) const;
    int PushPath(int delay, const FVector& location);
    int GetNodeCount() const;
    int GetDelay(int index) const;
    FVector GetLoc(int index) const;
    int GetClosestIndex(int x, int y, int z) const;
    void ShowSuperPointNodeToBuilder(int builderId, int startIndex, int endIndex, IPacketHelper& helper, int& count) const;
    bool ShowSuperPointToBuilder(User& builder) const;

    // PDB type 0x1504 supplies the source member names/types. The current
    // constructor at 0x0053c0ac supplies the reduced 2018 offsets below.
    SPMoveType m_MoveType = SPMoveType::FollowRail;
    std::u16string m_Name;
    int m_SPNodeCount = 0;
    std::vector<int> m_Delays;
    std::vector<FVector> m_Locs;
    SuperPointNodeMap m_Paths;
};

class CSuperPointManager {
public:
    using SuperPointNameMap = std::map<std::u16string, CSuperPoint*, runtime::CStringLess<std::u16string>>;

    CSuperPoint* Find(const char16_t* name);
    bool Add(const char16_t* name, CSuperPoint* superPoint);
    bool Remove(const char16_t* name);
    void RemoveAll();
    int Count() const;
    void Load();

private:
    SuperPointNameMap m_SuperPointNameMap;
};

extern CSuperPointManager g_SuperPointManager;

// SuperPoint.cpp stores per-NPC overrides from superpointinfo.txt separately
// from the binary CSuperPoint graph. The target zero-initializes all fields.
struct SuperPointNodeInfo {
    int Delay = 0;
    int SocialNumber = 0;
    int FStringIndex = 0;
    int m_nNodeID = 0;
};

class CSuperPointInfoOfNPC {
public:
    explicit CSuperPointInfoOfNPC(int npcClassId);

    int GetDelay(int nodeIndex);
    int GetSocialNumber(int nodeIndex);
    int GetFStringIndex(int nodeIndex);
    int GetMoveType();
    void SetNPCClassID(int npcClassId);
    void SetDelay(int nodeIndex, int delay);
    void SetSocialNumber(int nodeIndex, int socialNumber);
    void SetFStringIndex(int nodeIndex, int fstringIndex);
    void SetMoveType(int moveType);

private:
    friend struct scoped_lock_detail::OwnerAccess;

    void Enter(const wchar_t* file, int line);
    void Leave(const wchar_t* file, int line);
    SuperPointNodeInfo* GetNodeInfo(int nodeIndex, bool readOnly);

    int npc_class_id = 0;
    int nMoveType = 0;
    std::map<int, SuperPointNodeInfo*> NodeList;
    CYieldLock m_lock;
};

class CSuperPointInfo {
public:
    CSuperPointInfo();

    void SetSuperpointName(const char16_t* name);
    std::u16string GetSuperpointName() const;
    void SetDelay(int npcClassId, int nodeIndex, int delay);
    void SetSocialNumber(int npcClassId, int nodeIndex, int socialNumber);
    void SetFStringIndex(int npcClassId, int nodeIndex, int fstringIndex);
    void SetMoveType(int npcClassId, int moveType);
    int GetDelay(int npcClassId, int nodeIndex);
    int GetSocialNumber(int npcClassId, int nodeIndex);
    int GetFStringIndex(int npcClassId, int nodeIndex);
    int GetMoveType(int npcClassId);

private:
    friend struct scoped_lock_detail::OwnerAccess;

    void Enter(const wchar_t* file, int line);
    void Leave(const wchar_t* file, int line);
    CSuperPointInfoOfNPC* GetNPCInfo(int npcClassId, bool readOnly);

    std::u16string SuperpointName;
    std::map<int, CSuperPointInfoOfNPC*> NPCList;
    CYieldLock m_lock;
};

class CSuperPointInfoManager {
public:
    static CSuperPointInfoManager& Inst();

    CSuperPointInfo* GetSuperPointInfo(std::u16string superPointName);
    bool AddSuperPointInfo(CSuperPointInfo* info);
    int GetFStringIndex(std::u16string superPointName, int npcClassId, int nodeIndex);
    int GetSocialNumber(std::u16string superPointName, int npcClassId, int nodeIndex);
    int GetDelay(std::u16string superPointName, int npcClassId, int nodeIndex);
    int GetMoveType(std::u16string superPointName, int npcClassId);
    void Load();

private:
    friend struct scoped_lock_detail::OwnerAccess;

    void Enter(const wchar_t* file, int line);
    void Leave(const wchar_t* file, int line);

    std::map<std::u16string, CSuperPointInfo*, runtime::CStringLess<std::u16string>> InfoList;
    CYieldLock m_lock;
};


} // namespace l2geo
