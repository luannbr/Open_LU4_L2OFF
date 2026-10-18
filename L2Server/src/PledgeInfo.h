#pragma once

#include "Protocol/src/L2Constants.h"
#include "Shared/src/PledgeMasteryID.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

struct XMLElement;
enum UserPointType : std::int32_t;

struct PledgeCostInfo {
    PledgeCostInfo();

    bool LoadFromXML(const XMLElement* element);

    std::int64_t item_amount;
    int item_class;
    int name_value;
    std::int64_t adena;

private:
};


struct PledgeUserCostInfo {
    PledgeUserCostInfo();

    bool LoadFromXML(const XMLElement* element);
    bool HasItemCost() const;
    bool HasPointCost() const;

    std::int64_t adena;
    std::int64_t point_amount;
    std::int64_t item_amount;
    UserPointType point_type;
    int item_class;

private:
};


namespace PledgeFunctionType {

// Current L2Server.exe 0x00999ac4 compares these names in this order and
// returns the adjacent 0..14 values. The 15-bit PledgeFunction carrier fixes
// the exclusive upper bound independently.
// enum Value : std::int32_t {
//     NONE = 0,
//     MEMBER_COUNT = 1,
//     PLEDGE_WAR = 2,
//     PLEDGE_GAME = 3,
//     ALLIANCE = 4,
//     AUCTION_AGIT = 5,
//     COMMUNITY = 6,
//     CLAN_ADVENT = 7,
//     MINIGAME = 8,
//     CREST = 9,
//     NICKNAME = 10,
//     CASTLE_SIEGE = 11,
//     AGIT_SIEGE = 12,
//     FORTRESS_SIEGE = 13,
//     WAREHOUSE = 14,
//     MAX_COUNT = 15,
// }

} // namespace PledgeFunctionType


namespace PledgeAddType {

enum Value : std::int32_t {
    AUTO = 0,
    MANUAL = 1,
    SYSTEM = 2,
};

} // namespace PledgeAddType

static_assert(PledgeAddType::AUTO == 0);
static_assert(PledgeAddType::MANUAL == 1);
static_assert(PledgeAddType::SYSTEM == 2);

// Exact current condition value consumed by PledgeFunction and the pledge
// XML databases. Current code fixes all three dword fields and the 0x0c
// extent. Donor debug data supplies only the source member spellings.
struct PledgeConditionInfo {
    PledgeConditionInfo();

    bool LoadFromXML(const XMLElement* element);
    bool CheckLevel(int pledgeLevel) const;
    bool CheckMastery(int masteryPoint) const;
    bool HasMasteryCondition() const;

    int level;
    PledgeMasteryID::Type mastery_id;
    int point;

private:
};


namespace PledgeStuff {

PledgeMasteryID::Type ConvertToPledgeMasteryID(const std::u16string& value);
PledgeFunctionType::Value ConvertToPledgeFunctionType(const std::u16string& value);
PledgeAddType::Value ConvertToAddType(const std::u16string& value, PledgeAddType::Value defaultValue);
SocialClass::Type ConvertToSocialClassType(const std::u16string& value);
UserPointType ConvertToUserPointType(const std::u16string& value, UserPointType defaultValue);

PledgeMasteryID::Type GetAttributeValueAsMasterID(
    const XMLElement* element, const std::u16string& attributeName, PledgeMasteryID::Type defaultValue
);
PledgeFunctionType::Value GetAttributeValueAsFunctionType(
    const XMLElement* element, const std::u16string& attributeName, PledgeFunctionType::Value defaultValue
);
PledgeAddType::Value GetAttributeValueAsAddType(
    const XMLElement* element, const std::u16string& attributeName, PledgeAddType::Value defaultValue
);
SocialClass::Type GetAttributeValueAsSocialClassType(
    const XMLElement* element, const std::u16string& attributeName, SocialClass::Type defaultValue
);
UserPointType GetAttributeValueAsUserPointType(
    const XMLElement* element, const std::u16string& attributeName, UserPointType defaultValue
);

} // namespace PledgeStuff
