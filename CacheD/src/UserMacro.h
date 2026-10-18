#pragma once

#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CSocket;

// Current Cached.exe 0x005c6ce8 constructs this exact 0xb8-byte target
// record. char16_t is used deliberately: shipping wchar_t is UTF-16 even on
// portable hosts where wchar_t has four bytes.
struct UserMacroInfo {
    int macroId = 0;
    int macroOrder = 0;
    int macroInt1 = 0;
    int macroInt2 = 0;
    int macroInt3 = 0;
    std::array<char16_t, 81> macroString{};
};


// Result-row carrier used by UserMacroHandler::SendMacro. The current body
// stores macro_icontype at +0x4. The selected char_id result column is bound
// separately and is not kept in this join object.
struct UserMacroInfoTableJoin {
    int macroId = 0;
    int macroIconType = 0;
    std::array<char16_t, 64> macroName{};
    std::array<char16_t, 64> macroTooltip{};
    std::array<char16_t, 64> macroIconName{};
    UserMacroInfo macroInfo{};
};


using UserMacroIdSet = std::set<int>;
using UserMacroInfoMap = std::multimap<int, UserMacroInfoTableJoin*>;

// The current vtables contain Send at +0, ProcessFailure at +8 and the
// deleting destructor at +0x10. The destructor's store to target +0x10
// proves that the server-user id and socket are base fields,
// shared by both concrete strategies.
struct BaseMacroStrategy {
protected:
    explicit BaseMacroStrategy(int characterId, int userServerId, CSocket* socket) noexcept :
        m_characterId(characterId), m_userServerId(userServerId), m_socket(socket) {}

    int m_characterId;
    int m_userServerId;
    CSocket* m_socket;

    friend class UserMacroHandler;

public:
    virtual bool Send(UserMacroIdSet& macroIds, UserMacroInfoMap& macroRows, int macroCount) = 0;
    virtual void ProcessFailure() = 0;
    // Current complete destructor 0x0045b098 clears the borrowed socket.
    // the Base/Default/CopyOther deleting slots fold to 0x0045f01c.
    virtual ~BaseMacroStrategy();
};


class DefaultMacroStrategy final : public BaseMacroStrategy {
public:
    DefaultMacroStrategy(int characterId, int userServerId, CSocket* socket, int responseType, int macroId) noexcept;

    bool Send(UserMacroIdSet& macroIds, UserMacroInfoMap& macroRows, int macroCount) override;
    void ProcessFailure() override;

private:
    int m_responseType;
    int m_macroId;
};


// Current UserMacro.obj owns this second concrete strategy. The target
// RequestLoadOtherUserMacro stack object is 0x28 bytes: the Base strategy
// source-character id is followed by the requesting server-user id, socket,
// requested name and destination-character id in exactly this order.
class CopyOtherMacroStrategy final : public BaseMacroStrategy {
public:
    CopyOtherMacroStrategy(
        int sourceCharacterId,
        int userServerId,
        CSocket* socket,
        const char16_t* sourceCharacterName,
        int destinationCharacterId
    ) noexcept;

    bool Send(UserMacroIdSet& macroIds, UserMacroInfoMap& macroRows, int macroCount) override;
    void ProcessFailure() override;

private:
    [[maybe_unused]] const char16_t* m_sourceCharacterName;
    int m_destinationCharacterId;
};


class UserMacroHandler {
public:
    static int GetMacroCount(int characterId);
    static int CreateUserMacro(
        int characterId,
        std::u16string_view macroName,
        std::u16string_view macroTooltip,
        std::u16string_view macroIconName,
        int macroIconType,
        int& responseType,
        int& macroId
    );
    static void CreateUserMacroInfo(const UserMacroInfo& macroInfo);
    static void SendMacro(BaseMacroStrategy& strategy);
};
