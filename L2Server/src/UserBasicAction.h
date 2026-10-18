#pragma once

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class User;

enum ServerTriggeredUserAction : std::int32_t {
    STUA_SIT_STAND = 0,
    STUA_WALK_RUN = 1,
    STUA_ATTACK = 2,
    STUA_TRADE = 3,
    STUA_CLIENT_ACTION = 4,
    STUA_SOCIAL_ACTION = 5,
    STUA_PRIVATE_STORE = 6,
    STUA_PRIVATE_BUY = 7,
    STUA_MAKE_ITEM = 8,
    STUA_RIDE = 9,
    STUA_MAKE_ITEM2 = 10,
    STUA_PACKAGE_PRIVATE_STORE = 11,
    STUA_PET_ACTION = 12,
    STUA_SUMMON_ACTION = 13,
    STUA_PET_DEPOSIT = 14,
    STUA_SUMMON_DESPAWN = 15,
    STUA_TELEPORT_BOOKMARK = 16,
    STUA_BOT_REPORT = 17,
    STUA_AIRSHIP_ACTION = 18,
    STUA_COUPLE_ACTION = 19,
    STUA_SET_TACTICAL_SIGN = 20,
    STUA_TARGET_TACTICAL_SIGN = 21,
    STUA_INSTANTZONE = 22,
    STUA_MAX = 23,
};


class CBasicActionHandler {
public:
    virtual ~CBasicActionHandler() = default;

    bool Handle(User* user, int actionId, bool controlPressed, bool shiftPressed, int option, bool allowedRaidUser);

protected:
    virtual bool HandleInternal(User* user, int actionId, bool controlPressed, bool shiftPressed, int option) = 0;
};


class CBasicActionHandlerWrapper {
public:
    CBasicActionHandlerWrapper(CBasicActionHandler* handler, int option, bool allowedRaidUser) noexcept;

    bool Handle(User* user, int actionId, bool controlPressed, bool shiftPressed);

    CBasicActionHandler* m_handler; // +0x00
    int m_option; // +0x08
    bool m_allowedRaidUser; // +0x0c
    std::byte m_padding0d[3]{}; // +0x0d
};


#define DECLARE_BASIC_ACTION_HANDLER(name)                                                                                  \
    class CBasicActionHandler_##name final : public CBasicActionHandler {                                                   \
    protected:                                                                                                              \
        bool HandleInternal(User* user, int actionId, bool controlPressed, bool shiftPressed, int option) override;         \
    }

DECLARE_BASIC_ACTION_HANDLER(SIT_STAND);
DECLARE_BASIC_ACTION_HANDLER(WALK_RUN);
DECLARE_BASIC_ACTION_HANDLER(ATTACK);
DECLARE_BASIC_ACTION_HANDLER(TRADE);
DECLARE_BASIC_ACTION_HANDLER(CLIENT_ACTION);
DECLARE_BASIC_ACTION_HANDLER(SOCIAL_ACTION);
DECLARE_BASIC_ACTION_HANDLER(PRIVATE_STORE);
DECLARE_BASIC_ACTION_HANDLER(PRIVATE_BUY);
DECLARE_BASIC_ACTION_HANDLER(MAKE_ITEM);
DECLARE_BASIC_ACTION_HANDLER(RIDE);
DECLARE_BASIC_ACTION_HANDLER(MAKE_ITEM2);
DECLARE_BASIC_ACTION_HANDLER(PACKAGE_PRIVATE_STORE);
DECLARE_BASIC_ACTION_HANDLER(PET_ACTION);
DECLARE_BASIC_ACTION_HANDLER(SUMMON_ACTION);
DECLARE_BASIC_ACTION_HANDLER(PET_DEPOSIT);
DECLARE_BASIC_ACTION_HANDLER(SUMMON_DESPAWN);
DECLARE_BASIC_ACTION_HANDLER(TELEPORT_BOOKMARK);
DECLARE_BASIC_ACTION_HANDLER(BOT_REPORT);
DECLARE_BASIC_ACTION_HANDLER(AIRSHIP_ACTION);
DECLARE_BASIC_ACTION_HANDLER(COUPLE_ACTION);
DECLARE_BASIC_ACTION_HANDLER(SET_TACTICAL_SIGN);
DECLARE_BASIC_ACTION_HANDLER(TARGET_TACTICAL_SIGN);
DECLARE_BASIC_ACTION_HANDLER(INSTANTZONE);

#undef DECLARE_BASIC_ACTION_HANDLER

class CUserBasicAction {
public:
    using ActionMap = std::map<int, CBasicActionHandlerWrapper*>;

    CUserBasicAction();
    ~CUserBasicAction();

    CBasicActionHandler* GetHandler(const char16_t* handlerName);
    void SendList(User* user);
    bool HandleServerTriggeredAction(User* user, ServerTriggeredUserAction action, bool controlPressed, bool shiftPressed);
    bool HandleAction(User* user, int actionId, bool controlPressed, bool shiftPressed);
    bool IsValid(int actionId) const;
    bool Add(int actionId, const char16_t* handlerName, int option, bool allowedRaidUser);
    bool Add(int actionId);
    void MakeList();
    void Load();

private:
    ActionMap m_actionMap; // +0x0000
    std::uint8_t m_buf[0x4f9c]; // +0x0020
    std::uint32_t m_bufLen; // +0x4fbc
    int m_actionCount; // +0x4fc0
    [[maybe_unused]] std::uint32_t m_padding4fc4{}; // +0x4fc4
};


extern CUserBasicAction g_UserBasicAction;
