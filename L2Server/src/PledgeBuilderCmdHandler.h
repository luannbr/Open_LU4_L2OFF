#pragma once

#include "Shared/src/StandardContainers.h"
#include "Shared/src/Utils.h"

#include <cstddef>
#include <cstdint>

class CUserSocket;
class User;

namespace PledgeBuilderCmdHandler {

using ParamVector = std::vector<std::u16string>;
using Handler = bool (*)(CUserSocket*, User*, const ParamVector&);

struct PledgeBuilderCmdType {
    PledgeBuilderCmdType() = default;
    PledgeBuilderCmdType(int requiredLevel, Handler handler) noexcept;

    int nRequiredLevel{};
    std::uint32_t m_targetPadding04{};
    Handler pHandler{};
};

using HandlerMap = ::std::map<std::u16string, PledgeBuilderCmdType, runtime::CStringLess<std::u16string>>;

extern HandlerMap g_PledgeBuilderCmdMap;

class BindHandlerHelper {
public:
    BindHandlerHelper(const char16_t* command, int requiredLevel, Handler handler);
};

bool help(CUserSocket*, User*, const ParamVector&);
bool info(CUserSocket*, User*, const ParamVector&);
bool level_reload(CUserSocket*, User*, const ParamVector&);
bool level_up(CUserSocket*, User*, const ParamVector&);
bool deposit(CUserSocket*, User*, const ParamVector&);
bool add_namevalue(CUserSocket*, User*, const ParamVector&);
bool mastery_set(CUserSocket*, User*, const ParamVector&);
bool mastery_reset(CUserSocket*, User*, const ParamVector&);
bool mastery_reload(CUserSocket*, User*, const ParamVector&);
bool reset_penalty(CUserSocket*, User*, const ParamVector&);
bool contrib_info(CUserSocket*, User*, const ParamVector&);
bool contrib_reload(CUserSocket*, User*, const ParamVector&);
bool contrib_add(CUserSocket*, User*, const ParamVector&);
bool contrib_add_random(CUserSocket*, User*, const ParamVector&);
bool contrib_reward(CUserSocket*, User*, const ParamVector&);
bool contrib_rank_decide(CUserSocket*, User*, const ParamVector&);
bool contrib_rank(CUserSocket*, User*, const ParamVector&);
bool contrib_reset(CUserSocket*, User*, const ParamVector&);
bool function_reload(CUserSocket*, User*, const ParamVector&);
bool function_set(CUserSocket*, User*, const ParamVector&);
bool skill_reload(CUserSocket*, User*, const ParamVector&);
bool skill_list(CUserSocket*, User*, const ParamVector&);
bool skill_add(CUserSocket*, User*, const ParamVector&);
bool skill_active(CUserSocket*, User*, const ParamVector&);
bool skill_remove(CUserSocket*, User*, const ParamVector&);
bool skill_reset(CUserSocket*, User*, const ParamVector&);
bool item_reload(CUserSocket*, User*, const ParamVector&);
bool item_list(CUserSocket*, User*, const ParamVector&);
bool item_add(CUserSocket*, User*, const ParamVector&);
bool item_remove(CUserSocket*, User*, const ParamVector&);
bool item_reset(CUserSocket*, User*, const ParamVector&);
bool raid_show(CUserSocket*, User*, const ParamVector&);
bool raid_show_point(CUserSocket*, User*, const ParamVector&);
bool raid_set_point(CUserSocket*, User*, const ParamVector&);
bool raid_show_points(CUserSocket*, User*, const ParamVector&);
bool raid_reset_restrict(CUserSocket*, User*, const ParamVector&);
bool raid_show_ui(CUserSocket*, User*, const ParamVector&);
bool raid_decide(CUserSocket*, User*, const ParamVector&);
bool raid_reset_season(CUserSocket*, User*, const ParamVector&);
bool mission_show(CUserSocket*, User*, const ParamVector&);
bool mission_set(CUserSocket*, User*, const ParamVector&);
bool mission_reward(CUserSocket*, User*, const ParamVector&);
bool mission_reset_one_week(CUserSocket*, User*, const ParamVector&);

} // namespace PledgeBuilderCmdHandler

bool pledge_builder_handler(CUserSocket* socket, User* user, const PledgeBuilderCmdHandler::ParamVector& params);
