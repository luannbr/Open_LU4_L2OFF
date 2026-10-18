// User.h  -  recovered User-side classes.
//
// PDB/source evidence places User, ReverseHateManager and
// ReverseHateNPCList in l2npc/user.cpp.
#pragma once

#include "Shared/src/MemorySub.h"

#include "L2NPC/src/Creature.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>


class CSharedCreatureData;
class CNPC;

// User-side event fanout recovered from l2npc/user.cpp. The User object
// owns the behavior while all relationships it reads reside in its
// CSharedCreatureData record.
class User : public CCreature {
public:
    User();
    ~User() override;

    L2_DECLARE_OBJECT_TYPE(User)

    static User* GetObjectW(int objectId);
    bool Set(unsigned int objectId, unsigned int sharedIndex);
    bool IsPC() const override;
    User* CastUser() override;

    int GetCookie(char16_t* key);
    void SetCookie(char16_t* key, int value);
    int CreateBingoBoard(int size);
    int GetBingoBoardSize();
    int GetNumberFromBingoBoard(int index);
    int SelectBingoNumber(int number);
    int GetBingoSelectCount();
    int IsSelectedBingoNumber(int number);
    int GetMatchedBingoLineCount();
    void ClearBingoBoard();
    bool AddReverseHate(std::uint32_t npcObjectId, int hate) override;
    void ResetReverseHate();
    void ClearReverseHate();
    void SeeCreature(CCreature* creature) override;
    void TimerExpired(int timerId) override;
    void Spelled(CSharedCreatureData* speller, int skillId, int skillLevel, int skillSubLevel) override;

    void HandleMasterAttackEvent(CCreature* target, int damage, int skillId, int skillLevel, int skillSubLevel);
    void HandleMasterAttackedEvent(CCreature* attacker, int damage, int skillId, int skillLevel, int skillSubLevel);

private:
    friend class CSharedCreatureData;
    friend class CNPC;

    // Shipping User::m_lock is the first User member at exact +0x28.
    // CSharedCreatureData::CheckAndSetOneTimeUserFlag holds it while
    // inspecting/updating m_AtomicQuestFlag[5].
    CYieldLock m_lock;

    // Current User+0x38. The target's MSVC std::map owner is 0x20 bytes
    // portable code owns a standard map whose host extent may differ.
    std::map<std::uint32_t, int> m_ReverseHateMap;
    // Current User+0x58.
    CIOCriticalSection m_ReverseHateSect;

    // Current User+0x80 in the shipping layout. The portable member is a
    // standard map. Its host extent and offset are implementation-dependent.
    std::map<std::u16string, int, std::less<>> m_CookieMap;

public:
    // Packet handlers in the shipping source access this field directly.
    // It is target User+0xa0 (donor-PDB User+0x80).
    std::atomic<int> m_nEventHandling;

private:
    // Current User+0xa8 and +0xc8. Shipping xvector has a fourth,
    // allocator-storage word, so each owner occupies 0x20 bytes.
    std::vector<int> m_BingoBoard;
    std::vector<int> m_SelectedBingoNumbers;
    int m_nBingoBoardSize; // +0xe8
    int m_nBingoSelectCount; // +0xec
    // User::User initializes mHTMLCookie to -1 at current +0xf0.
    int mHTMLCookie;
};


class ReverseHateNPCList : public MemoryObject {
public:
    explicit ReverseHateNPCList(std::uint32_t userObjectId);

    L2_DECLARE_OBJECT_TYPE(ReverseHateNPCList)

    void AddNPC(std::uint32_t npcObjectId);
    void DeleteNPC(std::uint32_t npcObjectId);
    void DeleteAll();

private:
    std::vector<std::uint32_t> m_NPCList;
    std::set<std::uint32_t> m_NPCSet;
    int m_nUserID = 0;
    CIOCriticalSection m_lock;
};


class ReverseHateManager : public MemoryObject {
public:
    ReverseHateManager();

    L2_DECLARE_OBJECT_TYPE(ReverseHateManager)

    static ReverseHateManager& inst();
    void Init();
    void AddHate(std::uint32_t userObjectId, std::uint32_t npcObjectId);
    void DeleteHate(std::uint32_t userObjectId, std::uint32_t npcObjectId);
    void ResetHate(std::uint32_t userObjectId);

private:
    std::map<std::uint32_t, ReverseHateNPCList*> m_ReverseHateMap;
    CIOCriticalSection m_lock;
};
