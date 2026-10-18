#include "ServerType.h"

ServerTypeHelper::ServerTypeHelper() {
    typeMap.emplace(std::u16string(u"dev"), L2ServerType::DEV);
    typeMap.emplace(std::u16string(u"live"), L2ServerType::LIVE);
    typeMap.emplace(std::u16string(u"test"), L2ServerType::TEST);
    typeMap.emplace(std::u16string(u"movie"), L2ServerType::MOVIE);
    typeMap.emplace(std::u16string(u"broadcast"), L2ServerType::BROADCAST);
    typeMap.emplace(std::u16string(u"worldserver"), L2ServerType::WORLDSERVER);
}

ServerTypeHelper::~ServerTypeHelper() = default;

L2ServerType::Value ServerTypeHelper::GetType(char16_t* const type) {
    const StringToType::iterator found = typeMap.find(type);
    if (found == typeMap.end()) {
        return L2ServerType::LIVE;
    }
    return found->second;
}
