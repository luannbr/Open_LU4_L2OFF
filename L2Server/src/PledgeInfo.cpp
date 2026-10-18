#include "PledgeInfo.h"

#include "PledgeStuff.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/EtcMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/TextRuntime.h"
#include "XmlLoader.h"

#include <string>

PledgeCostInfo::PledgeCostInfo() : item_amount(0), item_class(-1), name_value(0), adena(0) {}

PledgeUserCostInfo::PledgeUserCostInfo() : adena(0), point_amount(0), item_amount(0), point_type(PT_NONE), item_class(-1) {}

bool PledgeUserCostInfo::HasItemCost() const { return item_class > 0 && item_amount > 0; }

bool PledgeUserCostInfo::HasPointCost() const { return point_type != PT_NONE && point_amount > 0; }

bool PledgeCostInfo::LoadFromXML(const XMLElement* element) {
    L2_GUARD_FUNCTION();

    name_value = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"name_value"), 0);
    item_class = PledgeStuff::GetAttributeAsClassID(element, std::u16string(u"item_class"));
    item_amount = XMLUtil::GetAttributeValueAsInt64(element, std::u16string(u"item_amount"), 0);
    adena = XMLUtil::GetAttributeValueAsInt64(element, std::u16string(u"adena"), 0);
    return true;
}

bool PledgeUserCostInfo::LoadFromXML(const XMLElement* element) {
    L2_GUARD_FUNCTION();

    adena = static_cast<std::int64_t>(XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"adena"), 0));
    item_class = PledgeStuff::GetAttributeAsClassID(element, std::u16string(u"item_class"));
    item_amount = static_cast<std::int64_t>(XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"item_amount"), 0));
    point_type = PledgeStuff::GetAttributeValueAsUserPointType(element, std::u16string(u"point_type"), PT_NONE);
    point_amount = static_cast<std::int64_t>(XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"point_amount"), 0));
    return true;
}

PledgeConditionInfo::PledgeConditionInfo() : level(0), mastery_id(PledgeMasteryID::NONE), point(0) {}

bool PledgeConditionInfo::LoadFromXML(const XMLElement* element) {
    L2_GUARD_FUNCTION();

    level = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"level"), 0);
    mastery_id = PledgeStuff::GetAttributeValueAsMasterID(element, std::u16string(u"mastery_id"), PledgeMasteryID::NONE);
    point = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"point"), 0);
    return true;
}

bool PledgeConditionInfo::CheckLevel(int pledgeLevel) const {
    if (level < 1) {
        return true;
    }
    return level <= pledgeLevel;
}

bool PledgeConditionInfo::HasMasteryCondition() const {
    const std::uint32_t normalized = static_cast<std::uint32_t>(mastery_id) - 1U;
    return normalized < 16U && point > 0;
}

bool PledgeConditionInfo::CheckMastery(int masteryPoint) const {
    if (!HasMasteryCondition()) {
        return true;
    }
    return point <= masteryPoint;
}

namespace PledgeStuff {

PledgeMasteryID::Type ConvertToPledgeMasteryID(const std::u16string& value) {
    static const std::u16string none(u"NONE");
    static const std::u16string pledgeLevel1(u"PLEDGE_LV_1");
    static const std::u16string pledgeLevel2(u"PLEDGE_LV_2");
    static const std::u16string pledgeLevel3(u"PLEDGE_LV_3");
    static const std::u16string pledgeLevel4(u"PLEDGE_LV_4");
    static const std::u16string pledgeLevel5(u"PLEDGE_LV_5");
    static const std::u16string pledgeLevel6(u"PLEDGE_LV_6");
    static const std::u16string combatLevel1(u"COMBAT_LV_1");
    static const std::u16string combatLevel2(u"COMBAT_LV_2");
    static const std::u16string combatLevel3(u"COMBAT_LV_3");
    static const std::u16string combatLevel4(u"COMBAT_LV_4");
    static const std::u16string combatLevel5(u"COMBAT_LV_5");
    static const std::u16string warLevel1(u"WAR_LV_1");
    static const std::u16string warLevel2(u"WAR_LV_2");
    static const std::u16string warLevel3(u"WAR_LV_3");
    static const std::u16string warLevel4(u"WAR_LV_4");
    static const std::u16string warLevel5(u"WAR_LV_5");

    if (value == none) {
        return PledgeMasteryID::NONE;
    }
    if (value == pledgeLevel1) {
        return PledgeMasteryID::PLEDGE_LV_1;
    }
    if (value == pledgeLevel2) {
        return PledgeMasteryID::PLEDGE_LV_2;
    }
    if (value == pledgeLevel3) {
        return PledgeMasteryID::PLEDGE_LV_3;
    }
    if (value == pledgeLevel4) {
        return PledgeMasteryID::PLEDGE_LV_4;
    }
    if (value == pledgeLevel5) {
        return PledgeMasteryID::PLEDGE_LV_5;
    }
    if (value == pledgeLevel6) {
        return PledgeMasteryID::PLEDGE_LV_6;
    }
    if (value == combatLevel1) {
        return PledgeMasteryID::COMBAT_LV_1;
    }
    if (value == combatLevel2) {
        return PledgeMasteryID::COMBAT_LV_2;
    }
    if (value == combatLevel3) {
        return PledgeMasteryID::COMBAT_LV_3;
    }
    if (value == combatLevel4) {
        return PledgeMasteryID::COMBAT_LV_4;
    }
    if (value == combatLevel5) {
        return PledgeMasteryID::COMBAT_LV_5;
    }
    if (value == warLevel1) {
        return PledgeMasteryID::WAR_LV_1;
    }
    if (value == warLevel2) {
        return PledgeMasteryID::WAR_LV_2;
    }
    if (value == warLevel3) {
        return PledgeMasteryID::WAR_LV_3;
    }
    if (value == warLevel4) {
        return PledgeMasteryID::WAR_LV_4;
    }
    if (value == warLevel5) {
        return PledgeMasteryID::WAR_LV_5;
    }

    const std::string hostValue = Utf16ToUtf8(value);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[PLEDGE][%s][%d]Invalid PledgeMasterID::%s",
    //     L"PledgeInfo.cpp",
    //     0x96,
    //     hostValue.c_str())
    // L2_LOG_ERROR(Log,
    //     L"[PLEDGE][%s][%d]Invalid PledgeMasterID::%s",
    //     L"PledgeInfo.cpp",
    //     0x96,
    //     hostValue.c_str())
    L2_LOG_ERROR(Log, "Unknown clan mastery value '%s'; reporting the stack and using NONE.", hostValue.c_str());
    runtime::process::ReportCallStack(2, false);
    return PledgeMasteryID::NONE;
}

PledgeFunctionType::Value ConvertToPledgeFunctionType(const std::u16string& value) {
    L2_GUARD_FUNCTION();

    static const std::u16string none(u"NONE");
    static const std::u16string memberCount(u"MEMBER_COUNT");
    static const std::u16string pledgeWar(u"PLEDGE_WAR");
    static const std::u16string pledgeGame(u"PLEDGE_GAME");
    static const std::u16string alliance(u"ALLIANCE");
    static const std::u16string auctionAgit(u"AUCTION_AGIT");
    static const std::u16string community(u"COMMUNITY");
    static const std::u16string clanAdvent(u"CLAN_ADVENT");
    static const std::u16string minigame(u"MINIGAME");
    static const std::u16string crest(u"CREST");
    static const std::u16string nickname(u"NICKNAME");
    static const std::u16string castleSiege(u"CASTLE_SIEGE");
    static const std::u16string agitSiege(u"AGIT_SIEGE");
    static const std::u16string fortressSiege(u"FORTRESS_SIEGE");
    static const std::u16string warehouse(u"WAREHOUSE");

    if (value == none) {
        return PledgeFunctionType::NONE;
    }
    if (value == memberCount) {
        return PledgeFunctionType::MEMBER_COUNT;
    }
    if (value == pledgeWar) {
        return PledgeFunctionType::PLEDGE_WAR;
    }
    if (value == pledgeGame) {
        return PledgeFunctionType::PLEDGE_GAME;
    }
    if (value == alliance) {
        return PledgeFunctionType::ALLIANCE;
    }
    if (value == auctionAgit) {
        return PledgeFunctionType::AUCTION_AGIT;
    }
    if (value == community) {
        return PledgeFunctionType::COMMUNITY;
    }
    if (value == clanAdvent) {
        return PledgeFunctionType::CLAN_ADVENT;
    }
    if (value == minigame) {
        return PledgeFunctionType::MINIGAME;
    }
    if (value == crest) {
        return PledgeFunctionType::CREST;
    }
    if (value == nickname) {
        return PledgeFunctionType::NICKNAME;
    }
    if (value == castleSiege) {
        return PledgeFunctionType::CASTLE_SIEGE;
    }
    if (value == agitSiege) {
        return PledgeFunctionType::AGIT_SIEGE;
    }
    if (value == fortressSiege) {
        return PledgeFunctionType::FORTRESS_SIEGE;
    }
    if (value == warehouse) {
        return PledgeFunctionType::WAREHOUSE;
    }

    const std::string hostValue = Utf16ToUtf8(value);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[PLEDGE][%s][%d]Invalid PledgeFunctionType::%s",
    //     L"PledgeInfo.cpp",
    //     0xc2,
    //     hostValue.c_str())
    // L2_LOG_ERROR(Log,
    //     L"[PLEDGE][%s][%d]Invalid PledgeFunctionType::%s",
    //     L"PledgeInfo.cpp",
    //     0xc2,
    //     hostValue.c_str())
    L2_LOG_ERROR(
        Log, "Unknown clan function value '%s'; reporting the stack and using NONE.", hostValue.c_str()
    );
    runtime::process::ReportCallStack(2, false);
    return PledgeFunctionType::NONE;
}

PledgeAddType::Value ConvertToAddType(const std::u16string& value, PledgeAddType::Value defaultValue) {
    L2_GUARD_FUNCTION();

    static const std::u16string automatic(u"AUTO");
    static const std::u16string manual(u"MANUAL");
    static const std::u16string system(u"SYSTEM");

    if (value == automatic) {
        return PledgeAddType::AUTO;
    }
    if (value == manual) {
        return PledgeAddType::MANUAL;
    }
    if (value == system) {
        return PledgeAddType::SYSTEM;
    }

    if (!value.empty()) {
        const std::string hostValue = Utf16ToUtf8(value);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Invalid SocialClass::%s",
        //     L"PledgeInfo.cpp",
        //     0x110,
        //     hostValue.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]Invalid SocialClass::%s",
        //     L"PledgeInfo.cpp",
        //     0x110,
        //     hostValue.c_str())
        L2_LOG_ERROR(
            Log, "Unknown clan-item add type '%s'; reporting the stack and using default type %d.",
            hostValue.c_str(), static_cast<int>(defaultValue)
        );
        runtime::process::ReportCallStack(2, false);
    }
    return defaultValue;
}

SocialClass::Type ConvertToSocialClassType(const std::u16string& value) {
    L2_GUARD_FUNCTION();

    static const std::u16string wanderer(u"WANDERER");
    static const std::u16string squire(u"SQUIRE");
    static const std::u16string mercenary(u"MERCENARY");
    static const std::u16string knight(u"KNIGHT");
    static const std::u16string banneret(u"BANNERET");
    static const std::u16string baron(u"BARON");
    static const std::u16string count(u"COUNT");
    static const std::u16string duke(u"DUKE");
    static const std::u16string king(u"KING");
    static const std::u16string emperor(u"EMPEROR");

    if (value == wanderer) {
        return SocialClass::WANDERER;
    }
    if (value == squire) {
        return SocialClass::SQUIRE;
    }
    if (value == mercenary) {
        return SocialClass::MERCENARY;
    }
    if (value == knight) {
        return SocialClass::KNIGHT;
    }
    if (value == banneret) {
        return SocialClass::BANNERET;
    }
    if (value == baron) {
        return SocialClass::BARON;
    }
    if (value == count) {
        return SocialClass::COUNT;
    }
    if (value == duke) {
        return SocialClass::DUKE;
    }
    if (value == king) {
        return SocialClass::KING;
    }
    if (value == emperor) {
        return SocialClass::EMPEROR;
    }

    const std::string hostValue = Utf16ToUtf8(value);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[PLEDGE][%s][%d]Invalid SocialClass::%s",
    //     L"PledgeInfo.cpp",
    //     0xeb,
    //     hostValue.c_str())
    // L2_LOG_ERROR(Log,
    //     L"[PLEDGE][%s][%d]Invalid SocialClass::%s",
    //     L"PledgeInfo.cpp",
    //     0xeb,
    //     hostValue.c_str())
    L2_LOG_ERROR(
        Log, "Unknown social class value '%s'; reporting the stack and using WANDERER.", hostValue.c_str()
    );
    runtime::process::ReportCallStack(2, false);
    return SocialClass::WANDERER;
}

UserPointType ConvertToUserPointType(const std::u16string& value, UserPointType defaultValue) {
    L2_GUARD_FUNCTION();

    static const std::u16string pcCafe(u"PT_PCCAFE_POINT");
    static const std::u16string pvp(u"PT_PVP_POINT");
    static const std::u16string raid(u"PT_RAID_POINT");

    if (value == pcCafe) {
        return PT_PCCAFE_POINT;
    }
    if (value == pvp) {
        return PT_PVP_POINT;
    }
    if (value == raid) {
        return PT_RAID_POINT;
    }

    if (!value.empty()) {
        const std::string hostValue = Utf16ToUtf8(value);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Invalid UserPointType::%s",
        //     L"PledgeInfo.cpp",
        //     0x135,
        //     hostValue.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]Invalid UserPointType::%s",
        //     L"PledgeInfo.cpp",
        //     0x135,
        //     hostValue.c_str())
        L2_LOG_ERROR(
            Log, "Unknown user point type '%s'; reporting the stack and using default type %d.",
            hostValue.c_str(), static_cast<int>(defaultValue)
        );
        runtime::process::ReportCallStack(2, false);
    }
    return defaultValue;
}

PledgeMasteryID::Type GetAttributeValueAsMasterID(
    const XMLElement* element, const std::u16string& attributeName, PledgeMasteryID::Type defaultValue [[maybe_unused]]
) {
    L2_GUARD_FUNCTION();

    const std::u16string none(u"NONE");
    return ConvertToPledgeMasteryID(XMLUtil::GetAttributeValue(element, attributeName, none));
}

PledgeFunctionType::Value GetAttributeValueAsFunctionType(
    const XMLElement* element, const std::u16string& attributeName, PledgeFunctionType::Value defaultValue [[maybe_unused]]
) {
    L2_GUARD_FUNCTION();

    const std::u16string none(u"NONE");
    return ConvertToPledgeFunctionType(XMLUtil::GetAttributeValue(element, attributeName, none));
}

PledgeAddType::Value GetAttributeValueAsAddType(
    const XMLElement* element, const std::u16string& attributeName, PledgeAddType::Value defaultValue
) {
    L2_GUARD_FUNCTION();

    const std::u16string empty;
    return ConvertToAddType(XMLUtil::GetAttributeValue(element, attributeName, empty), defaultValue);
}

SocialClass::Type GetAttributeValueAsSocialClassType(
    const XMLElement* element, const std::u16string& attributeName, SocialClass::Type defaultValue [[maybe_unused]]
) {
    L2_GUARD_FUNCTION();

    const std::u16string wanderer(u"WANDERER");
    return ConvertToSocialClassType(XMLUtil::GetAttributeValue(element, attributeName, wanderer));
}

UserPointType GetAttributeValueAsUserPointType(
    const XMLElement* element, const std::u16string& attributeName, UserPointType defaultValue
) {
    L2_GUARD_FUNCTION();

    const std::u16string empty;
    return ConvertToUserPointType(XMLUtil::GetAttributeValue(element, attributeName, empty), defaultValue);
}

} // namespace PledgeStuff
