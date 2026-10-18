#pragma once

#include "Shared/src/Config.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"

#include <cstddef>


class ServerTypeHelper : public Singleton<ServerTypeHelper> {
public:
    using StringToType = ::std::map<std::u16string, L2ServerType::Value, runtime::CStringLess<std::u16string>>;

    ServerTypeHelper();
    ~ServerTypeHelper() override;

    L2ServerType::Value GetType(char16_t* type);

private:
    StringToType typeMap;
};
