#pragma once

#include "StaticObject.h"

#include "Shared/src/ItemOptionKey.h"
#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>


class CUserSocket;
class User;
class CParserForDoor;
class CSignBoardDb;

// Exact current observer-remains owner. The target stores its fixed name as
// 26 UTF-16 code units. Using char16_t keeps the Win64 0x34-byte array on
// hosts whose wchar_t is four bytes.
class CObserverRemains : public CStaticObject {
public:
    CObserverRemains();
    ~CObserverRemains() override;

    L2_DECLARE_OBJECT_REGISTRATION(CObserverRemains)

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()
    bool IsObserverRemains() override;
    CObserverRemains* CastObserverRemains() override;

    void RemoveRemains();
    void SendObserverRemainsInfo(CUserSocket* socket);
    void SetUser(User* user);


    char16_t sName[26];
    std::int32_t nRace;
    std::int32_t nSex;
    std::int32_t nClass;
    std::int32_t nFace;
    std::int32_t nHairShape;
    std::int32_t nHairColor;
    std::int32_t nNowClass;
    std::int32_t nSlotItemClassID[63];
    ItemOptionKey optionKey[63];
    std::int32_t nSlotItemShapeShiftClassID[63];
    std::int32_t nPledgeId;
    std::int32_t nPledgeCrestId;
    std::int32_t nPledgeSId;
    std::int32_t nAllianceId;
    std::int32_t nAllianceCrestId;
    std::array<std::byte, 0x04> m_alignment093cTo0940;
    double dCollisionRadius;
    double dCollisionHeight;
    std::int32_t nCriminalRate;
    std::array<std::byte, 0x04> m_alignment0954To0958;

private:
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CObserverRemains)
};

// Donor-selected source vocabulary over exact current values. The current
// parser and Interact body establish the complete 0..3 domain.
enum SignBoardActionTypeEnum : std::int32_t {
    SBACT_NONE = 0,
    SBACT_HTML = 1,
    SBACT_TOWNMAP = 2,
    SBACT_OBSERVER = 3,
};

// Current parser stores and current CSignBoard consumers establish every
// width and offset. Member spellings are donor-selected replacement names.
struct ObserverPos final {
    std::int32_t nNameID;
    std::int32_t x;
    std::int32_t y;
    std::int32_t z;
    std::int32_t yaw;
    std::int32_t pitch;
    std::int64_t price;
    std::int32_t nResidenceId;
    std::int32_t nGroup;
};

// Exact current L2Server signboard envelope. Both strings are borrowed from
// parser-owned storage. The two standard vectors own only their element storage.
class CSignBoard : public CStaticObject {
    friend class CParserForDoor;
    friend class CSignBoardDb;

public:
    CSignBoard();
    ~CSignBoard() override;

    L2_DECLARE_OBJECT_REGISTRATION(CSignBoard)

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    bool IsSignBoard() override;
    CSignBoard* CastSignBoard() override;
    bool IsAlive() override;
    void DecideDistance(CMoveToTargetShareData* data, CSmartID creatureId) override;
    void Interact(CCreature* creature) override;

    void SendSignBoardInfo(CUserSocket* socket);
    void SendObserverListHTML(User* user, int group);
    void AddObserverPos(ObserverPos observerPos);

    // TotalChecker's same-source field list marks this complete data group
    // public. Current HtmlCmdObserver directly indexes m_vObserverPosList,
    // confirming that access boundary without changing layout.
    // Target wchar_t is UTF-16. Keep these borrowed script pointers at their
    // actual target width on hosts where wchar_t is four bytes.
    const char16_t* m_szHTML;
    const char16_t* m_szTextureName;
    std::int32_t m_nMapX;
    std::int32_t m_nMapY;
    std::vector<std::int32_t> m_vObserverGroup;
    std::vector<ObserverPos> m_vObserverPosList;
    SignBoardActionTypeEnum m_eAct;
    std::int32_t m_nDisplayNPCID;

private:
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CSignBoard)
};


class CSignBoardDb {
public:
    using SignBoardMap = std::map<std::u16string, CSignBoard*, runtime::CStringLess<std::u16string>>;

    CSignBoardDb() = default;
    ~CSignBoardDb() = default;

    CSignBoard* Find(const char16_t* name);
    void Add(const char16_t* name, CSignBoard* signBoard);

private:
    SignBoardMap m_SignBoardNameMap;
};


extern CSignBoardDb g_SignBoardDb;
