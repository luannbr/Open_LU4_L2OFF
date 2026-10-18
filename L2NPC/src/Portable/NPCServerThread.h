#pragma once

#include <cstdint>
#include <functional>

namespace l2npc::portable {

using NPCServerThreadStartSink = std::function<void*()>;

class NPCServerThreadBinding {
public:
    NPCServerThreadBinding() = default;
    explicit NPCServerThreadBinding(NPCServerThreadStartSink startSink);
    ~NPCServerThreadBinding();

    NPCServerThreadBinding(const NPCServerThreadBinding&) = delete;
    NPCServerThreadBinding& operator=(const NPCServerThreadBinding&) = delete;

    NPCServerThreadBinding(NPCServerThreadBinding&& other) noexcept;
    NPCServerThreadBinding& operator=(NPCServerThreadBinding&& other) noexcept;

    void Reset(NPCServerThreadStartSink startSink = {});

private:
    std::uint64_t generation_ = 0;
};

void* StartNPCServerThread();

} // namespace l2npc::portable
