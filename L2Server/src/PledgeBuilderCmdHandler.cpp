#include "PledgeBuilderCmdHandler.h"

#include "DB.h"
#include "Item.h"
#include "ObjectDB.h"
#include "Pledge.h"
#include "PledgeContribution.h"
#include "PledgeContributionInfo.h"
#include "PledgeFunction.h"
#include "PledgeFunctionInfo.h"
#include "PledgeItem.h"
#include "PledgeItemInfo.h"
#include "PledgeLevel.h"
#include "PledgeLevelInfo.h"
#include "PledgeMastery.h"
#include "PledgeMasteryInfo.h"
#include "PledgeMission.h"
#include "PledgeRaid.h"
#include "PledgeSkill.h"
#include "PledgeSkillInfo.h"
#include "PledgeStuff.h"
#include "User.h"
#include "UserSocket.h"

#include "Protocol/src/L2Constants.h"
#include "Shared/src/Config.h"
#include "Shared/src/Log.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/ScopedLockGuards.h"
#include "Shared/src/SlotType.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <string_view>
#include <vector>

namespace {

using PledgeBuilderCmdHandler::ParamVector;

template <typename... Args>
std::u16string FormatMessage(const std::u16string_view format, const Args&... arguments) {
    std::array<char16_t, 1024> message{};
    FormatUtf16To(message.data(), message.size(), format, arguments...);
    return std::u16string(message.data());
}

void SendMessage(CUserSocket* const socket, const std::u16string& message) {
    if (socket != nullptr) {
        PledgeStuff::SendSystemMessage(socket, message);
    }
}

std::u16string GetParamAsString(
    const ParamVector& params, const std::size_t index, const char16_t* const defaultValue = u""
) {
    return index < params.size() ? runtime::CheckedVectorAt(params, index) : std::u16string(defaultValue);
}

int GetParamAsInt(const ParamVector& params, const std::size_t index, const int defaultValue = 0) {
    if (index >= params.size()) {
        return defaultValue;
    }
    return WindowsWtol(runtime::CheckedVectorAt(params, index));
}

std::int64_t GetParamAsInt64(const ParamVector& params, const std::size_t index, const std::int64_t defaultValue = 0) {
    if (index >= params.size()) {
        return defaultValue;
    }
    const std::wstring value = Utf16ToWide(runtime::CheckedVectorAt(params, index));
    return static_cast<std::int64_t>(std::wcstoll(value.c_str(), nullptr, 10));
}

bool GetParamAsBool(const ParamVector& params, const std::size_t index, const bool defaultValue = false) {
    if (index >= params.size()) {
        return defaultValue;
    }
    return runtime::StringView(runtime::CheckedVectorAt(params, index)) == std::u16string_view(u"true");
}

int GetParamAsClassId(const ParamVector& params, const int, const int defaultValue) {
    int classId = GetParamAsInt(params, 1, 0);
    if (classId == 0) {
        const std::u16string name = GetParamAsString(params, 1);
        classId = g_ObjectDB.GetClassIdFromName(name.c_str(), false);
    }
    return classId == -1 ? defaultValue : classId;
}

bool CheckWorld(CUserSocket* const socket, const User* const user, const char16_t* const command) {
    if (user != nullptr && user->m_data->GetWorldId() == g_nWorldId) {
        return true;
    }
    if (socket != nullptr) {
        socket->SendSystemMessage(u"SYS", FormatMessage(u"Raid User cannot do %s command", command));
    }
    return false;
}

bool AssembleItemForDeposit(CPacketHelperEx<20480>& packet, const CItem* const item, const std::int64_t amount) {
    if (item == nullptr || item->m_data == nullptr) {
        return false;
    }
    return packet.Assemble(
               "ddQddd", item->GetIndex(), item->m_data->m_nClassID, amount, item->m_data->GetEnchanted(),
               item->m_data->m_nDamaged, static_cast<int>(item->m_data->m_ItemConsumeType)
           ) > 0;
}


} // namespace

namespace PledgeBuilderCmdHandler {

HandlerMap g_PledgeBuilderCmdMap;

PledgeBuilderCmdType::PledgeBuilderCmdType(const int requiredLevel, const Handler handler) noexcept :
    nRequiredLevel(requiredLevel), pHandler(handler) {}

BindHandlerHelper::BindHandlerHelper(const char16_t* const command, const int requiredLevel, const Handler handler) {
    g_PledgeBuilderCmdMap.emplace(command, PledgeBuilderCmdType(requiredLevel, handler));
}

BindHandlerHelper ahelp(u"help", 3, help);
BindHandlerHelper ainfo(u"info", 3, info);
BindHandlerHelper alevel_reload(u"level_reload", 3, level_reload);
BindHandlerHelper alevel_up(u"level_up", 3, level_up);
BindHandlerHelper adeposit(u"deposit", 3, deposit);
BindHandlerHelper aadd_namevalue(u"add_namevalue", 3, add_namevalue);
BindHandlerHelper amastery_set(u"mastery_set", 3, mastery_set);
BindHandlerHelper amastery_reset(u"mastery_reset", 3, mastery_reset);
BindHandlerHelper amastery_reload(u"mastery_reload", 3, mastery_reload);
BindHandlerHelper areset_penalty(u"reset_penalty", 3, reset_penalty);
BindHandlerHelper acontrib_info(u"contrib_info", 3, contrib_info);
BindHandlerHelper acontrib_reload(u"contrib_reload", 3, contrib_reload);
BindHandlerHelper acontrib_add(u"contrib_add", 3, contrib_add);
BindHandlerHelper acontrib_add_random(u"contrib_add_random", 3, contrib_add_random);
BindHandlerHelper acontrib_reward(u"contrib_reward", 3, contrib_reward);
BindHandlerHelper acontrib_rank_decide(u"contrib_rank_decide", 3, contrib_rank_decide);
BindHandlerHelper acontrib_rank(u"contrib_rank", 3, contrib_rank);
BindHandlerHelper acontrib_reset(u"contrib_reset", 3, contrib_reset);
BindHandlerHelper afunction_reload(u"function_reload", 3, function_reload);
BindHandlerHelper afunction_set(u"function_set", 3, function_set);
BindHandlerHelper askill_reload(u"skill_reload", 3, skill_reload);
BindHandlerHelper askill_list(u"skill_list", 3, skill_list);
BindHandlerHelper askill_add(u"skill_add", 3, skill_add);
BindHandlerHelper askill_active(u"skill_active", 3, skill_active);
BindHandlerHelper askill_remove(u"skill_remove", 3, skill_remove);
BindHandlerHelper askill_reset(u"skill_reset", 3, skill_reset);
BindHandlerHelper aitem_reload(u"item_reload", 3, item_reload);
BindHandlerHelper aitem_list(u"item_list", 3, item_list);
BindHandlerHelper aitem_add(u"item_add", 3, item_add);
BindHandlerHelper aitem_remove(u"item_remove", 3, item_remove);
BindHandlerHelper aitem_reset(u"item_reset", 3, item_reset);
BindHandlerHelper araid_show(u"raid_show", 3, raid_show);
BindHandlerHelper araid_show_point(u"raid_show_point", 3, raid_show_point);
BindHandlerHelper araid_set_point(u"raid_set_point", 3, raid_set_point);
BindHandlerHelper araid_show_points(u"raid_show_points", 3, raid_show_points);
BindHandlerHelper araid_reset_restrict(u"raid_reset_restrict", 3, raid_reset_restrict);
BindHandlerHelper araid_show_ui(u"raid_show_ui", 3, raid_show_ui);
BindHandlerHelper araid_decide(u"raid_decide", 3, raid_decide);
BindHandlerHelper araid_reset_season(u"raid_reset_season", 1, raid_reset_season);
BindHandlerHelper amission_show(u"mission_show", 3, mission_show);
BindHandlerHelper amission_set(u"mission_set", 3, mission_set);
BindHandlerHelper amission_reward(u"mission_reward", 3, mission_reward);
BindHandlerHelper amission_reset_one_week(u"mission_reset_one_week", 3, mission_reset_one_week);

} // namespace PledgeBuilderCmdHandler

bool pledge_builder_handler(
    CUserSocket* const socket, User* const user, const PledgeBuilderCmdHandler::ParamVector& params
) {
    L2_GUARD_FUNCTION();
    const std::u16string command = params.empty() ? std::u16string(u"help") : runtime::CheckedVectorAt(params, 0);
    const auto position = PledgeBuilderCmdHandler::g_PledgeBuilderCmdMap.find(command);
    if (position == PledgeBuilderCmdHandler::g_PledgeBuilderCmdMap.end()) {
        SendMessage(socket, FormatMessage(u"[%s] is not a builder command.", command));
        return false;
    }
    if (user == nullptr || user->m_data == nullptr) {
        return false;
    }
    const PledgeBuilderCmdHandler::PledgeBuilderCmdType& entry = position->second;
    if (entry.nRequiredLevel != 0 && user->m_data->m_nBuilder > entry.nRequiredLevel) {
        return false;
    }
    return entry.pHandler != nullptr && entry.pHandler(socket, user, params);
}

namespace PledgeBuilderCmdHandler {

bool help(CUserSocket*, User* const user, const ParamVector&) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    for (const auto& entry : g_PledgeBuilderCmdMap) {
        PledgeStuff::SendSystemMessage(user, FormatMessage(u"%s [LEVEL:%d]", entry.first, entry.second.nRequiredLevel));
    }
    return false;
}

bool info(CUserSocket* const socket, User* const user, const ParamVector&) {
    L2_GUARD_FUNCTION();
    CPledge* const pledge = user != nullptr ? user->GetPledge() : nullptr;
    if (pledge == nullptr) {
        SendMessage(socket, u"Not in pledge");
        return false;
    }
    PledgeLevelStuff::Builder::SendPledgeLevelInfoTo(socket, pledge);
    PledgeMasteryStuff::Builder::SendPledgeMasteryInfoTo(socket, pledge);
    return false;
}

bool level_reload(CUserSocket* const socket, User*, const ParamVector&) {
    L2_GUARD_FUNCTION();
    PledgeLevelDB::Builder::Reload(&PledgeLevelDB::inst());
    SendMessage(socket, u"[pledge level_reload] accepted");
    return false;
}

bool level_up(CUserSocket* const socket, User* const user, const ParamVector&) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->GetPledge() == nullptr) {
        if (socket != nullptr) {
            socket->SendSystemMessage(u"SYS", u"Not in pledge");
        }
        return false;
    }
    PledgeLevelStuff::RequestPledgeLevelUp(user);
    SendMessage(socket, u"[pledge level_up] accepted");
    return false;
}

bool deposit(CUserSocket* const socket, User* const user, const ParamVector& params) {
    L2_GUARD_FUNCTION();
    CPledge* const pledge = user != nullptr ? user->GetPledge() : nullptr;
    if (pledge == nullptr) {
        SendMessage(socket, u"Not in pledge");
        return false;
    }
    if (params.size() < 2) {
        SendMessage(socket, u"//pledge deposit item_class|name [item_amount]");
        return false;
    }

    const int itemClassId = GetParamAsClassId(params, 1, -1);
    std::int64_t amount = GetParamAsInt64(params, 2, -1);
    L2_READ_GUARD(user->m_inv);
    CItem* const item = user->m_inv.GetFirstItemByClassID(itemClassId, false, false);
    if (item == nullptr) {
        SendMessage(socket, u"Item is not in inventory");
        return false;
    }
    if (!item->IsTrade()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeBuilderCmdHandler.cpp",
        //     0xe9)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeBuilderCmdHandler.cpp",
        //     0xe9)
        L2_LOG_WARN(Log, "Builder clan deposit rejected item %d: the item is not tradable.", itemClassId);
        return false;
    }
    const std::int64_t available = item->m_data->m_nCount;
    if (available == 0 || available < amount) {
        SendMessage(socket, u"No item.");
        return false;
    }
    if (amount < 0) {
        amount = available;
    }
    if (user->IsEquipedItem(item->GetIndex())) {
        SendMessage(socket, u"Equipped item.");
        return false;
    }
    int slot = 0;
    while (slot < ST_MAX && (ST2SBT[slot] & item->m_data->m_Slot) == 0) {
        ++slot;
    }
    if (slot < ST_MAX && user->m_data->m_nSlotIndex[slot] == item->GetIndex()) {
        SendMessage(socket, u"Equipped slot item.");
        return false;
    }

    CPacketHelperEx<20480> packet;
    int count = 0;
    if (amount <= available && AssembleItemForDeposit(packet, item, amount)) {
        ++count;
    }
    CItem* const adena = user->m_inv.GetFirstItemByClassID(57, false, false);
    if (adena == nullptr) {
        SendMessage(socket, u"No adena");
        return false;
    }
    if (AssembleItemForDeposit(packet, adena, 0)) {
        ++count;
    }
    db.RequestDepositItemEx(
        2, static_cast<int>(pledge->GetDbId()), user, count, user->GetServerId(), packet.GetLength(), packet.GetPacket()
    );
    SendMessage(socket, u"[pledge deposit] accepted");
    return false;
}

bool add_namevalue(CUserSocket* const socket, User* const user, const ParamVector& params) {
    L2_GUARD_FUNCTION();
    if (params.size() < 2) {
        SendMessage(socket, u"//pledge add_namevalue amount");
        return false;
    }
    CPledge* const pledge = user != nullptr ? user->GetPledge() : nullptr;
    if (pledge == nullptr) {
        SendMessage(socket, u"Not in pledge");
        return false;
    }
    const int amount = GetParamAsInt(params, 1);
    db.UpdatePledgeNameValue(
        // Current add_namevalue@0x0098dfc8 reads CObject::m_nId directly
        // from CPledge+0x18 at 0x0098e176. GetIndex() is the distinct
        // shared-data index at CPledge+0x28 and cannot be used as the
        // server-side reply correlation identity.
        pledge->Id(), pledge->GetDbId(), pledge->GetNameValue(), amount, g_nWorldId
    );
    PledgeContributionSystem::inst().AddPoint(user, pledge, amount);
    SendMessage(socket, u"[pledge add_namevalue] accepted");
    return false;
}

bool mastery_set(CUserSocket* const socket, User* const user, const ParamVector& params) {
    L2_GUARD_FUNCTION();
    if (params.size() < 2) {
        SendMessage(socket, u"//pledge mastery_set id");
        return false;
    }
    const int masteryId = GetParamAsInt(params, 1);
    if (!PledgeStuff::IsValidMasteryID(static_cast<PledgeMasteryID::Type>(masteryId))) {
        SendMessage(socket, u"Invalid mastery id");
        return false;
    }
    if (user == nullptr || user->m_data->m_nPledgeID == 0) {
        SendMessage(socket, u"Not in pledge");
        return false;
    }
    PledgeMasteryStuff::RequestSetPoint(user, static_cast<PledgeMasteryID::Type>(masteryId));
    SendMessage(socket, u"[pledge mastery_set] accepted");
    return false;
}

bool mastery_reset(CUserSocket* const socket, User* const user, const ParamVector&) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->m_data->m_nPledgeID == 0) {
        SendMessage(socket, u"Not in pledge");
        return false;
    }
    PledgeMasteryStuff::RequestResetPoint(user);
    SendMessage(socket, u"[pledge mastery_reset] accepted");
    return false;
}

bool mastery_reload(CUserSocket* const socket, User*, const ParamVector&) {
    L2_GUARD_FUNCTION();
    PledgeMasteryDB::Builder::Reload(&PledgeMasteryDB::inst());
    SendMessage(socket, u"[pledge mastery_reload] accepted");
    return false;
}

bool reset_penalty(CUserSocket* const socket, User* const user, const ParamVector&) {
    L2_GUARD_FUNCTION();
    CPledge* const pledge = user != nullptr ? user->GetPledge() : nullptr;
    if (pledge == nullptr) {
        SendMessage(socket, u"Not in pledge");
        return false;
    }
    pledge->SetMasteryResetTime(0);
    pledge->Save(0x15);
    SendMessage(socket, u"[pledge reset_penalty] accepted");
    return false;
}

bool contrib_info(CUserSocket* const socket, User* const user, const ParamVector& params) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->m_data->m_nPledgeID == 0) {
        SendMessage(socket, u"Not in pledge");
        return false;
    }
    if (params.size() < 2) {
        SendMessage(socket, u"//pledge contrib_info true|false");
        return false;
    }
    PledgeContributionSystem::Builder::RetrieveRankInfo(
        &PledgeContributionSystem::inst(), user, GetParamAsBool(params, 1, true)
    );
    SendMessage(socket, u"[pledge contrib_info] accepted");
    return false;
}

bool contrib_reload(CUserSocket* const socket, User*, const ParamVector&) {
    L2_GUARD_FUNCTION();
    PledgeContributionDB::Builder::Reload(&PledgeContributionDB::inst());
    SendMessage(socket, u"[pledge contrib_reload] accepted");
    return false;
}

bool contrib_add(CUserSocket* const socket, User* const user, const ParamVector& params) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->m_data->m_nPledgeID == 0) {
        SendMessage(socket, u"Not in pledge");
        return false;
    }
    if (params.size() < 2) {
        SendMessage(socket, u"//pledge contrib_add point");
        return false;
    }
    PledgeContributionSystem::Builder::AddPoint(
        &PledgeContributionSystem::inst(), static_cast<int>(user->GetDBID()), static_cast<int>(user->m_data->m_nPledgeID),
        GetParamAsInt(params, 1)
    );
    SendMessage(socket, u"[pledge contrib_add] accepted");
    return false;
}

bool contrib_add_random(CUserSocket* const socket, User* const user, const ParamVector& params) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->m_data->m_nPledgeID == 0) {
        SendMessage(socket, u"Not in pledge");
        return false;
    }
    if (params.size() < 3) {
        SendMessage(socket, u"//pledge contrib_add_random min max");
        return false;
    }
    const int minimum = GetParamAsInt(params, 1);
    const int requestedMaximum = GetParamAsInt(params, 2);
    const int maximum = requestedMaximum > minimum ? requestedMaximum : minimum;
    CPledge* const pledge = user->GetPledge();
    std::vector<int> memberIds;
    pledge->SelectMemberIDs(memberIds);
    for (const int memberId : memberIds) {
        const int point = static_cast<int>(
            ::genrand64_real2() * static_cast<double>(maximum - minimum + 1) + static_cast<double>(minimum)
        );
        PledgeContributionSystem::Builder::AddPoint(
            &PledgeContributionSystem::inst(), memberId, static_cast<int>(pledge->GetDbId()), point
        );
    }
    SendMessage(socket, u"[pledge contrib_add_random] accepted");
    return false;
}

bool contrib_reward(CUserSocket* const socket, User* const user, const ParamVector& params) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->m_data->m_nPledgeID == 0) {
        SendMessage(socket, u"Not in pledge");
        return false;
    }
    const std::u16string type = GetParamAsString(params, 1);
    if (runtime::StringView(type) == std::u16string_view(u"period")) {
        PledgeContributionSystem::inst().TakePeriodReward(user);
    } else if (runtime::StringView(type) == std::u16string_view(u"total")) {
        PledgeContributionSystem::inst().TakeTotalReward(user);
    } else {
        SendMessage(socket, u"//pledge contrib_reward period|total");
        return false;
    }
    const std::wstring hostType = Utf16ToWide(type);
    SendMessage(socket, FormatMessage(u"[pledge contrib_reward %s] accepted", hostType.c_str()));
    return false;
}

bool contrib_rank_decide(CUserSocket* const socket, User* const user, const ParamVector& params) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->m_data->m_nPledgeID == 0) {
        SendMessage(socket, u"Not in pledge");
        return false;
    }
    const std::int32_t delay = runtime::MultiplyInt32Wrapping(GetParamAsInt(params, 1), 1000);
    PledgeContributionSystem::Builder::DecideSeason(&PledgeContributionSystem::inst(), user, delay);
    SendMessage(socket, u"[pledge contrib_rank_decide] accepted");
    return false;
}

bool contrib_rank(CUserSocket* const socket, User* const user, const ParamVector&) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->m_data->m_nPledgeID == 0) {
        SendMessage(socket, u"Not in pledge");
        return false;
    }
    PledgeContributionSystem::Builder::ShowInfo(&PledgeContributionSystem::inst(), user);
    SendMessage(socket, u"[pledge contrib_rank] accepted");
    return false;
}

bool contrib_reset(CUserSocket* const socket, User* const user, const ParamVector&) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->m_data->m_nPledgeID == 0) {
        SendMessage(socket, u"Not in pledge");
        return false;
    }
    PledgeContributionSystem::Builder::ResetInfo(&PledgeContributionSystem::inst(), user);
    SendMessage(socket, u"[pledge contrib_reset] accepted");
    return false;
}

bool function_reload(CUserSocket* const socket, User* const user, const ParamVector&) {
    L2_GUARD_FUNCTION();
    PledgeFunctionDB::Builder::Reload(&PledgeFunctionDB::inst());
    CPledge* const pledge = user != nullptr ? user->GetPledge() : nullptr;
    if (pledge != nullptr) {
        pledge->GetFunction()->UpdateFunction(static_cast<int>(pledge->GetPledgeLevel()), pledge->GetMastery());
    }
    SendMessage(socket, u"[pledge function_reload] accepted");
    return false;
}

bool function_set(CUserSocket* const socket, User* const user, const ParamVector& params) {
    L2_GUARD_FUNCTION();
    CPledge* const pledge = user != nullptr ? user->GetPledge() : nullptr;
    if (pledge == nullptr) {
        SendMessage(socket, u"Not in pledge");
        return false;
    }
    const auto type = static_cast<PledgeFunctionType::Value>(GetParamAsInt(params, 1));
    if (GetParamAsBool(params, 2, false)) {
        PledgeFunction::Builder::Enable(pledge->GetFunction(), type);
    } else {
        PledgeFunction::Builder::Disable(pledge->GetFunction(), type);
    }
    SendMessage(socket, u"[pledge function_set] accepted");
    return false;
}

bool skill_reload(CUserSocket* const socket, User*, const ParamVector&) {
    L2_GUARD_FUNCTION();
    PledgeSkillDB::Builder::Reload(&PledgeSkillDB::inst());
    SendMessage(socket, u"[pledge skill_reload] accepted");
    return false;
}

bool skill_list(CUserSocket* const socket, User* const user, const ParamVector&) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->m_data->m_nPledgeID == 0) {
        SendMessage(socket, u"Not in pledge");
        return false;
    }
    PledgeSkillStuff::Builder::SendPledgeSkillListTo(user);
    SendMessage(socket, u"[pledge skill_list] accepted");
    return false;
}

bool skill_add(CUserSocket* const socket, User* const user, const ParamVector& params) {
    L2_GUARD_FUNCTION();
    if (!CheckWorld(socket, user, u"skill_add")) {
        return false;
    }
    if (user->m_data->m_nPledgeID == 0) {
        SendMessage(socket, u"Not in pledge");
        return false;
    }
    const int skillId = GetParamAsInt(params, 1);
    const int skillLevel = GetParamAsInt(params, 2, 1);
    const int expiryDelay = GetParamAsInt(params, 3);
    if (skillId == 0) {
        SendMessage(socket, u"//pledge skill_add skill_id skill_level [expiry_delay]");
        return false;
    }
    const std::int32_t expiryTime =
        expiryDelay == 0 ? 0 : runtime::AddInt32Wrapping(runtime::crt::Time32Now(), expiryDelay);
    CPledge* const pledge = user->GetPledge();
    if (pledge != nullptr) {
        pledge->AddPledgeSkillWithSaveAndNotify(skillId, skillLevel, expiryTime, true, false, false);
    }
    SendMessage(socket, u"[pledge skill_add] accepted");
    return false;
}

bool skill_active(CUserSocket* const socket, User* const user, const ParamVector& params) {
    L2_GUARD_FUNCTION();
    if (!CheckWorld(socket, user, u"skill_add")) {
        return false;
    }
    if (user->m_data->m_nPledgeID == 0) {
        SendMessage(socket, u"Not in pledge");
        return false;
    }
    const int skillId = GetParamAsInt(params, 1);
    const int skillLevel = GetParamAsInt(params, 2, 1);
    if (skillId == 0) {
        SendMessage(socket, u"//pledge skill_active skill_id skill_level");
        return false;
    }
    PledgeSkillStuff::RequestToAddPledgeSkill(user, skillId, skillLevel);
    SendMessage(socket, u"[pledge skill_active] accepted");
    return false;
}

bool skill_remove(CUserSocket* const socket, User* const user, const ParamVector& params) {
    L2_GUARD_FUNCTION();
    if (!CheckWorld(socket, user, u"skill_remove")) {
        return false;
    }
    if (user->m_data->m_nPledgeID == 0) {
        SendMessage(socket, u"Not in pledge");
        return false;
    }
    const int skillId = GetParamAsInt(params, 1);
    if (skillId == 0) {
        SendMessage(socket, u"//pledge skill_remove skill_id");
        return false;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge != nullptr && pledge->RemovePledgeSkill(skillId, true)) {
        db.RequestDelPledgeSkill(*pledge, static_cast<unsigned int>(skillId));
    }
    SendMessage(socket, u"[pledge skill_remove] accepted");
    return false;
}

bool skill_reset(CUserSocket* const socket, User* const user, const ParamVector& params) {
    L2_GUARD_FUNCTION();
    if (!CheckWorld(socket, user, u"skill_reset")) {
        return false;
    }
    if (user->m_data->m_nPledgeID == 0) {
        SendMessage(socket, u"Not in pledge");
        return false;
    }
    if (params.size() < 2) {
        SendMessage(socket, u"//pledge skill_reset all|mastery");
        return false;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge != nullptr) {
        pledge->RemovePledgeSkills(runtime::StringView(runtime::CheckedVectorAt(params, 1)) != std::u16string_view(u"all"));
    }
    SendMessage(socket, u"[pledge skill_reset] accepted");
    return false;
}

bool item_reload(CUserSocket* const socket, User* const user, const ParamVector&) {
    L2_GUARD_FUNCTION();
    PledgeItemDB::Builder::Reload(&PledgeItemDB::inst());
    CPledge* const pledge = user != nullptr ? user->GetPledge() : nullptr;
    if (pledge != nullptr) {
        PledgeItem::Builder::ReloadInfo(pledge->GetItem());
    }
    SendMessage(socket, u"[pledge item_reload] accepted");
    return false;
}

bool item_list(CUserSocket* const socket, User* const user, const ParamVector&) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->m_data->m_nPledgeID == 0) {
        SendMessage(socket, u"Not in pledge");
        return false;
    }
    PledgeItemStuff::Builder::SendPledgeItemListTo(user);
    SendMessage(socket, u"[pledge item_list] accepted");
    return false;
}

bool item_add(CUserSocket* const socket, User* const user, const ParamVector& params) {
    L2_GUARD_FUNCTION();
    if (!CheckWorld(socket, user, u"item_add")) {
        return false;
    }
    if (user->m_data->m_nPledgeID == 0) {
        SendMessage(socket, u"Not in pledge");
        return false;
    }
    const int itemClassId = GetParamAsClassId(params, 1, -1);
    if (itemClassId == -1) {
        SendMessage(socket, u"//pledge item_add class_id [expiry_delay]");
        return false;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge != nullptr) {
        PledgeItemStuff::AddPledgeItemAndSave(pledge, itemClassId, GetParamAsInt(params, 2));
    }
    SendMessage(socket, u"[pledge item_add] accepted");
    return false;
}

bool item_remove(CUserSocket* const socket, User* const user, const ParamVector& params) {
    L2_GUARD_FUNCTION();
    if (!CheckWorld(socket, user, u"item_add")) {
        return false;
    }
    if (user->m_data->m_nPledgeID == 0) {
        SendMessage(socket, u"Not in pledge");
        return false;
    }
    const int itemClassId = GetParamAsClassId(params, 1, -1);
    if (itemClassId == -1) {
        SendMessage(socket, u"//pledge item_remove class_id");
        return false;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge != nullptr && pledge->GetItem()->RemoveItem(itemClassId)) {
        PledgeItemStuff::RequestRemovePledgeItem(pledge, itemClassId);
        PledgeStuff::Write_LOG_PLEDGE_ITEM_REMOVE(pledge, itemClassId);
    }
    SendMessage(socket, u"[pledge item_remove] accepted");
    return false;
}

bool item_reset(CUserSocket* const socket, User* const user, const ParamVector& params) {
    L2_GUARD_FUNCTION();
    if (!CheckWorld(socket, user, u"item_reset")) {
        return false;
    }
    if (user->m_data->m_nPledgeID == 0) {
        SendMessage(socket, u"Not in pledge");
        return false;
    }
    if (params.size() < 2) {
        SendMessage(socket, u"//pledge item_reset all|mastery");
        return false;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge != nullptr) {
        PledgeItemStuff::RequestRemovePledgeItems(
            pledge, runtime::StringView(runtime::CheckedVectorAt(params, 1)) != std::u16string_view(u"all")
        );
        PledgeItemStuff::SendPledgeItemListTo(user);
    }
    return false;
}

bool raid_show(CUserSocket* const socket, User* const user, const ParamVector&) {
    L2_GUARD_FUNCTION();
    if (!CheckWorld(socket, user, u"raid_show")) {
        return false;
    }
    PledgeRaidSystem::inst().ShowInfo(user);
    SendMessage(socket, u"[pledge raid_show] accepted");
    return false;
}

bool raid_show_point(CUserSocket* const socket, User* const user, const ParamVector& params) {
    L2_GUARD_FUNCTION();
    if (!CheckWorld(socket, user, u"raid_show_point")) {
        return false;
    }
    const std::u16string pledgeName = GetParamAsString(params, 1);
    const std::wstring hostName = Utf16ToWide(pledgeName);
    const int season = GetParamAsInt(params, 2);
    PledgeRaidSystem::inst().ShowInfoWithPledgeName(user, pledgeName.c_str(), season);
    SendMessage(socket, FormatMessage(u"[pledge raid_show_point %s Season : %d] accepted", hostName.c_str(), season));
    return false;
}

bool raid_set_point(CUserSocket* const socket, User* const user, const ParamVector& params) {
    L2_GUARD_FUNCTION();
    if (!CheckWorld(socket, user, u"raid_set_point")) {
        return false;
    }
    const std::u16string pledgeName = GetParamAsString(params, 1);
    const std::int64_t point = GetParamAsInt64(params, 2);
    std::vector<SharedUtilsWideChar> lookupName(pledgeName.begin(), pledgeName.end());
    lookupName.push_back(0);
    CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, lookupName.data());
    if (pledge) {
        PledgeRaidSystem::inst().SetPoint(0, static_cast<int>(pledge->GetDbId()), 0, point);
    }
    const std::wstring hostName = Utf16ToWide(pledgeName);
    SendMessage(socket, FormatMessage(u"[pledge raid_set_point %s %I64d] accepted", hostName.c_str(), point));
    return false;
}

bool raid_show_points(CUserSocket* const socket, User* const user, const ParamVector& params) {
    L2_GUARD_FUNCTION();
    if (!CheckWorld(socket, user, u"raid_show_points")) {
        return false;
    }
    const int season = GetParamAsInt(params, 1);
    PledgeRaidSystem::inst().ShowInfoList(user, season);
    SendMessage(socket, FormatMessage(u"[pledge raid_show_points Season : %d] accepted", season));
    return false;
}

bool raid_reset_restrict(CUserSocket* const socket, User* const user, const ParamVector& params) {
    L2_GUARD_FUNCTION();
    if (!CheckWorld(socket, user, u"raid_reset_restrict")) {
        return false;
    }
    const std::u16string pledgeName = GetParamAsString(params, 1);
    const std::wstring hostName = Utf16ToWide(pledgeName);
    PledgeRaidSystem::inst().ResetInzoneRestrict(pledgeName.c_str());
    SendMessage(socket, FormatMessage(u"[pledge raid_reset_restrict Pledge : %s] accepted", hostName.c_str()));
    return false;
}

bool raid_show_ui(CUserSocket* const socket, User* const user, const ParamVector& params) {
    L2_GUARD_FUNCTION();
    if (!CheckWorld(socket, user, u"raid_show_ui")) {
        return false;
    }
    const bool currentSeason = GetParamAsBool(params, 1, false);
    PledgeRaidSystem::inst().SendAllToUser(user, currentSeason);
    SendMessage(socket, FormatMessage(u"[pledge raid_show_ui IsCurrentSeason : %d] accepted", currentSeason));
    return false;
}

bool raid_decide(CUserSocket* const socket, User* const user, const ParamVector& params) {
    L2_GUARD_FUNCTION();
    if (!CheckWorld(socket, user, u"raid_decide")) {
        return false;
    }
    const std::int64_t requestedDelay = GetParamAsInt64(params, 1);
    const std::int32_t delay = static_cast<std::int32_t>(requestedDelay);
    PledgeRaidSystem::inst().DecideSeason(user, delay);
    SendMessage(socket, FormatMessage(u"[pledge raid_decide %d] accepted", delay));
    return false;
}

bool raid_reset_season(CUserSocket* const socket, User* const user, const ParamVector&) {
    L2_GUARD_FUNCTION();
    if (!CheckWorld(socket, user, u"raid_reset_season")) {
        return false;
    }
    PledgeRaidSystem::inst().ResetSeasonBuilderCommand();
    SendMessage(socket, u"[pledge raid_reset_season] accepted");
    return false;
}

bool mission_show(CUserSocket* const socket, User* const user, const ParamVector& params) {
    L2_GUARD_FUNCTION();
    if (!CheckWorld(socket, user, u"mission_show")) {
        return false;
    }
    const int missionId = GetParamAsInt(params, 1);
    if (missionId == 0) {
        user->GetUserPledgeMission().BuilderCmdShowInfo(user);
    } else {
        user->GetUserPledgeMission().BuilderCmdShowInfoWithMissionId(user, missionId);
    }
    SendMessage(socket, u"[pledge mission_show] accepted");
    return false;
}

bool mission_set(CUserSocket* const socket, User* const user, const ParamVector& params) {
    L2_GUARD_FUNCTION();
    if (!CheckWorld(socket, user, u"mission_set")) {
        return false;
    }
    const int missionId = GetParamAsInt(params, 1);
    const int count = GetParamAsInt(params, 2);
    user->GetUserPledgeMission().BuilderCmdSetCount(user, missionId, count);
    SendMessage(socket, FormatMessage(u"[pledge mission_set [%d] [%d] accepted", missionId, count));
    return false;
}

bool mission_reward(CUserSocket* const socket, User* const user, const ParamVector& params) {
    L2_GUARD_FUNCTION();
    if (!CheckWorld(socket, user, u"mission_reward")) {
        return false;
    }
    const int missionId = GetParamAsInt(params, 1);
    user->GetUserPledgeMission().Reward(user, missionId);
    SendMessage(socket, FormatMessage(u"[pledge mission_reward [%d] accepted", missionId));
    return false;
}

bool mission_reset_one_week(CUserSocket* const socket, User* const user, const ParamVector&) {
    L2_GUARD_FUNCTION();
    if (!CheckWorld(socket, user, u"mission_reset_one_week")) {
        return false;
    }
    UserPledgeMission& mission = user->GetUserPledgeMission();
    mission.ResetRewardCountOfOneWeek(user);
    mission.SendRewardCountOfOneWeekToUser(user);
    SendMessage(socket, u"[pledge mission_reset_one_week accepted");
    return false;
}

} // namespace PledgeBuilderCmdHandler
