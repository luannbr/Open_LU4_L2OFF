#pragma once

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

class CParserForNPCSpawn;

namespace ConditionTargetType {

enum Enum : std::int32_t {
    PC = 0,
    PARTY_ONE = 1,
    PARTY_ALL = 2,
    MPCC_ALL = 3,
    MAX = 4,
};

} // namespace ConditionTargetType

namespace ExecutionType {

enum Enum : std::int32_t {
    ALWAYS = 0,
    RANDOM1 = 1,
    MAX = 2,
};

} // namespace ExecutionType

// Current TotalChecker.cpp event-handler carrier. The older TotalChecker
// PDB keeps the source names and access. Current L2Server.exe independently
// fixes the widened 0xe8 layout through the map-node destructor at 0x00aee6a4.
class QuestHandlerData {
public:
    QuestHandlerData();

    std::u16string GetAutoGenCodeForHandler() const;
    std::u16string GetInventoryCheckCode() const;

    std::int32_t QuestID; // +0x00
    std::byte m_alignment004To008[4];
    std::u16string QuestName; // +0x08
    std::u16string NPCName; // +0x30
    std::u16string Condition; // +0x58
    std::int32_t Choice; // +0x80
    std::byte m_alignment084To088[4];
    std::u16string Handler; // +0x88
    std::int32_t ConditionLine; // +0xb0
    std::int32_t HandlerLine; // +0xb4
    std::set<std::u16string> VarSet; // +0xb8
    ConditionTargetType::Enum mCondTarget; // +0xd8
    ExecutionType::Enum mExecutionType; // +0xdc
    bool checkCondition; // +0xe0
    std::byte m_alignment0e1To0e4[3];
    std::int32_t ConditionRange; // +0xe4
};

using QuestHandlerDataVector = std::vector<QuestHandlerData>;
using QuestHandlerEventDB = std::map<std::u16string, QuestHandlerDataVector>;


extern QuestHandlerEventDB g_AttackedEventDB;
extern QuestHandlerEventDB g_CreatedEventDB;
extern QuestHandlerEventDB g_MenuSelectedEventDB;
extern QuestHandlerEventDB g_MyDyingEventDB;
extern QuestHandlerEventDB g_QuestAcceptedEventDB;
extern QuestHandlerEventDB g_ScriptEventEventDB;
extern QuestHandlerEventDB g_SpelledEventDB;
extern QuestHandlerEventDB g_TalkSelectedEventDB;
extern QuestHandlerEventDB g_TalkedEventDB;
extern QuestHandlerEventDB g_TimerFiredEventDB;
extern QuestHandlerEventDB g_TimerFiredExEventDB;

// Non-polymorphic AI skeleton records consumed by the L2Server npcpos
// checker. Current constructors fix the 0x50-byte record and 0x20-byte
// table images of the older TotalChecker donor containers.
class AIClassInfo {
public:
    AIClassInfo();

    void AddParameter(const char16_t* name);
    bool HaveParameter(const char16_t* name) const;

    std::u16string mName; // +0x00
    AIClassInfo* mpSuper; // +0x28
    std::set<std::u16string> mParamSet; // +0x30
};


class AIClassInfoTable {
public:
    static AIClassInfoTable* inst();

    AIClassInfo* GetClassInfoW(const char16_t* name) const;
    void AddClassInfo(AIClassInfo* info);

private:
    std::map<std::u16string, AIClassInfo*, runtime::CStringLess<std::u16string>> mAIClassMap;
};


// Current L2Server CNpcPosChecker singleton. Its FILE pointer is non-owning
// and is used only by the maker-event-list mode of the generated parser.
class CNpcPosChecker {
public:
    static CNpcPosChecker* Inst();

    bool LoadData(char16_t* text, int codeUnitCount);
    void Uniputs(const char16_t* text);

    void SetOnlyMakerEventList(bool enabled) { m_bOnlyMakerEventList = enabled; }
    void SetMakerEventListFile(std::FILE* stream) { m_maker_event_list_file = stream; }

private:
    bool m_bOnlyMakerEventList = false; // +0x00
    [[maybe_unused]] std::byte m_targetPadding[7]{}; // +0x01
    std::FILE* m_maker_event_list_file = nullptr; // +0x08
};
