#pragma once

#include "PledgeInfo.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"

#include <cstddef>

struct XMLElement;

struct PledgeMemberCountInfo {
    bool LoadFromXML(const XMLElement* element);

    int total_count = 0;
    int elite_count = 0;
};


// Current PledgeFunctionDB allocates exactly 0x18 bytes and the constructor,
// parser and PledgeFunction consumer cover every byte.
struct PledgeFunctionInfo {
    PledgeFunctionInfo();

    bool LoadFromXML(const XMLElement* element);

    PledgeConditionInfo Condition;
    PledgeFunctionType::Value type;
    PledgeMemberCountInfo MemberCount;

private:
};


class PledgeFunctionDB : public ::Singleton<PledgeFunctionDB> {
public:
    class Builder {
    public:
        static void Reload(PledgeFunctionDB* database);
    };

    PledgeFunctionDB();
    ~PledgeFunctionDB() override;

    const std::vector<PledgeFunctionInfo*>& GetInfos() const;
    void Load();

private:
    std::vector<PledgeFunctionInfo*> Functions;
};
