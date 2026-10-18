#pragma once

#include <cstdint>

namespace runtime {
class Config;
}

namespace l2npc::portable {

// GetPrivateProfileString/GetPrivateProfileInt are the only platform
// dependency inside NPCServer::Load. This binding supplies the already-open
// portable INI view while NPCServer itself keeps all target field writes.
class NPCServerConfigurationBinding {
public:
    NPCServerConfigurationBinding() = default;
    explicit NPCServerConfigurationBinding(const runtime::Config* configuration);
    ~NPCServerConfigurationBinding();

    NPCServerConfigurationBinding(const NPCServerConfigurationBinding&) = delete;
    NPCServerConfigurationBinding& operator=(const NPCServerConfigurationBinding&) = delete;

    NPCServerConfigurationBinding(NPCServerConfigurationBinding&& other) noexcept;
    NPCServerConfigurationBinding& operator=(NPCServerConfigurationBinding&& other) noexcept;

    void Reset(const runtime::Config* configuration = nullptr);

private:
    std::uint64_t generation_ = 0;
};

const runtime::Config* GetNPCServerConfiguration();

} // namespace l2npc::portable
