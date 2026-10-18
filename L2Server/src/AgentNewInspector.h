#pragma once

#include "Shared/src/MemorySub.h"

#include "Agent.h"
#include "Portable/ProcessRuntime.h"
#include "QuestionAnswer.h"

#include <cstddef>
#include <cstdint>

class NewInspectorAgent final : public Agent {
public:
    L2_DECLARE_OBJECT_REGISTRATION(NewInspectorAgent)

    ~NewInspectorAgent() override;

    L2_DECLARE_OBJECT_SINGLE_ALLOCATOR_LITERAL(NewInspectorAgent)

    L2_DECLARE_OBJECT_RELEASE(NewInspectorAgent)

    bool QueryFirstImpl(PacketList::iterator current) override;
    bool ResponseFirstImpl(const unsigned char** packet) override;
    bool QuerySecondImpl() override;
    bool TimeCheckImpl() override;
    bool ResponseSecond(const unsigned char** packet) override;

private:
    friend class User;

    explicit NewInspectorAgent(User* user);

    Query MakeRandomQuery();
    void QueryCommon(CPacketHelperEx<20480>& packet);
    bool ExecutePolicy(std::uint32_t errorCode);
    bool ResponseCommonImpl(const unsigned char** packet, bool firstResponse);

    volatile std::int32_t m_ErrorCount; // +0x28
    volatile std::int32_t m_SequenceNo; // +0x2c
    volatile std::int32_t m_SendCount; // +0x30
    volatile std::int32_t m_RecvCount; // +0x34
    std::uint32_t m_lastRequestTick; // +0x38
    std::uint32_t m_lastResponseTick; // +0x3c
    QuestionAnswer m_QA; // +0x40..+0x5a7
};
