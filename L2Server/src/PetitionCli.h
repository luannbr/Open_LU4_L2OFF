#pragma once

#include "Shared/src/MemorySub.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>
#include <string>

class CPetitionSocket;
class CSocket;
class IPacketHelper;
class User;

struct PetitionCategory final : public MemoryObject {
    L2_DECLARE_OBJECT_TYPE(PetitionCategory)

    ~PetitionCategory() override = default;

    std::uint8_t nID; // +0x018
    std::uint8_t nParentID; // +0x019
    char16_t wsName[512]; // +0x01a
    char16_t wsDescription[512]; // +0x41a
    std::uint8_t alignment81ATo820[6]; // +0x81a
};

struct PetitionFAQ final : public MemoryObject {
    L2_DECLARE_OBJECT_TYPE(PetitionFAQ)

    ~PetitionFAQ() override = default;

    std::int32_t nMessageID; // +0x018
    std::uint8_t nCategoryID; // +0x01c
    std::uint8_t alignment1D; // +0x01d
    char16_t wsTitle[1000]; // +0x01e
    char16_t wsContents[6000]; // +0x7ee
    std::uint8_t alignment36CETo36D0[2]; // +0x36ce
};

struct PetitionCategoryAttribute final : public MemoryObject {
    L2_DECLARE_OBJECT_TYPE(PetitionCategoryAttribute)

    ~PetitionCategoryAttribute() override = default;

    std::int32_t nID; // +0x018
    char16_t wsName[512]; // +0x01c
    std::int32_t nType; // +0x41c
    std::int16_t sValueMaxLength; // +0x420
    std::uint8_t alignment422To428[6]; // +0x422
};

struct PetitionCheckedOut final {
    std::int32_t petitionID; // +0x0000
    std::int32_t nGM_dbid; // +0x0004
    std::int32_t nClient_dbid; // +0x0008
    std::uint8_t alignment0CTo10[4];
    std::vector<int> vPetiChatServerIdList; // +0x0010
    // The current callbacks only read or clear the initially empty mapPetition.
    // Its value fields are unknown because no current caller fills them.
    // Keep these bytes as storage and do not read fields through guessed offsets.
    std::array<std::byte, 0x1008> unresolvedPayload28To1030; // +0x0028
    bool bIsChat; // +0x1030
    bool bIsPetitionChat; // +0x1031
};

using PetitionCategoryMap = CObjectMap<PetitionCategory, 16, 5, CIOCriticalSection, int>;
using PetitionFAQMap = CObjectMap<PetitionFAQ, 16, 5, CIOCriticalSection, int>;


class CGMListMgr final {
public:
    CGMListMgr() = default;
    ~CGMListMgr() = default;

    static CGMListMgr& inst();

    bool Exist(const char16_t* name);
    bool Insert(const char16_t* name);
    bool Delete(const char16_t* name);
    void SendGMList(CSocket* socket);

private:
    std::vector<std::u16string> m_GMCharList; // +0x00
    RWLock m_lock; // +0x20
};


class CPetitionCli final {
public:
    CPetitionCli();
    ~CPetitionCli();

    bool Init();
    bool Connect();
    void Free();
    void ConnectToPetitionD();
    bool CreateThread();
    bool ReplyCharList(const unsigned char* packet);
    bool ReqCancelPetition(User* user, User* target);
    bool IsDisableTime();
    bool RequestFeedback(int length, char* buffer);
    bool ReqSubmitPetition(User* user, char16_t* text, int categoryId, User* target);
    bool ReqShowPetition(User* user);
    int GetDepthOne(IPacketHelper& packet);
    bool ReqShowStepTwo(User* user, std::uint8_t categoryId);
    int GetDepthTwo(IPacketHelper& packet, std::uint8_t categoryId);
    bool ReqShowStepThree(User* user, std::uint8_t categoryId);
    int GetDepthThree(IPacketHelper& packet, std::uint8_t categoryId);
    void ShowConTents(User* user, int messageId);
    void ShowMessage(User* user, wchar_t* message);
    void SetDisableTime(int first, int second);
    int GetMyPetition(int databaseId);
    void ResetCheckedOutList();
    bool NotifyUserChat2(User* user, const char16_t* message);
    bool NotifyGmChat(User* user, const char16_t* message);

    std::int32_t disabletime1; // +0x000
    std::int32_t disabletime2; // +0x004
    PetitionCategoryMap vCategoryList; // +0x008
    PetitionFAQMap vFAQList; // +0x490
    RWLock m_Petitionlock; // +0x918

private:
    friend class CPetitionSocket;

    bool enabled; // +0x950
    bool sendable; // +0x951
    bool supressed; // +0x952
    std::uint8_t alignment953To958[5];
    RWLock lock; // +0x958
    std::map<int, PetitionCheckedOut*> mapPetition; // +0x990
    CYieldLock mLock; // +0x9b0
    std::uint32_t m_nConnectThread; // +0x9c0
    std::uint32_t alignment9C4; // +0x9c4
    void* m_hConnectThread; // +0x9c8
    CPetitionSocket* m_pPetitionSocket; // +0x9d0
};


extern CPetitionCli petitionCli;
