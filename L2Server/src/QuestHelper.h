#pragma once

#include "Shared/src/Obj.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace QuestHelper {

enum Event : std::int32_t {
    TalkSelectedEvent = 0,
    QuestAccpetedEvent = 1,
    MenuSelectedEvent = 2,
    None = 3,
};

enum FileLocation : std::int32_t {
    FileLocationQuest = 0,
    FileLocationUserQuest = 1,
};

} // namespace QuestHelper

class CParserForQuestHelperQuest;
class CParserForQuestHelperUserQuest;
class CQuestHelper;

struct CQuestHelperUtil {
    static bool findAndSubString(std::u16string& orgStr, wchar_t* startStr, wchar_t* endStr, std::u16string* outStr);
};

struct IParser {
    virtual void parse(CQuestHelper& helper) = 0;
};

struct CParserBase : IParser {
    CParserBase(QuestHelper::Event type, QuestHelper::FileLocation fileLocation);

    QuestHelper::Event m_eType; // +0x08
    QuestHelper::FileLocation m_eFileLocation; // +0x0c
};

struct CTalkSelectedParser : CParserBase {
    explicit CTalkSelectedParser(QuestHelper::FileLocation fileLocation);
};

struct CQuestAccpetedParser : CParserBase {
    explicit CQuestAccpetedParser(QuestHelper::FileLocation fileLocation);
};

struct CMenuSelectParser : CParserBase {
    explicit CMenuSelectParser(QuestHelper::FileLocation fileLocation);
};

struct CParserQuest {
    explicit CParserQuest(CParserForQuestHelperQuest* parser);

    CParserForQuestHelperQuest* pParser; // +0x00
};

struct CParserUserQuest {
    explicit CParserUserQuest(CParserForQuestHelperUserQuest* parser);

    CParserForQuestHelperUserQuest* pParser; // +0x00
};

struct CQuestTalkSelectedParser final : CTalkSelectedParser, CParserQuest {
    explicit CQuestTalkSelectedParser(CParserForQuestHelperQuest* parser);
    void parse(CQuestHelper& helper) override;
};

struct CQuestQuestAccpetedParser final : CQuestAccpetedParser, CParserQuest {
    explicit CQuestQuestAccpetedParser(CParserForQuestHelperQuest* parser);
    void parse(CQuestHelper& helper) override;
};

struct CQuestMenuSelectParser final : CMenuSelectParser, CParserQuest {
    explicit CQuestMenuSelectParser(CParserForQuestHelperQuest* parser);
    void parse(CQuestHelper& helper) override;
};

struct CUserQuestTalkSelectedParser final : CTalkSelectedParser, CParserUserQuest {
    explicit CUserQuestTalkSelectedParser(CParserForQuestHelperUserQuest* parser);
    void parse(CQuestHelper& helper) override;
};

struct CUserQuestMenuSelectParser final : CMenuSelectParser, CParserUserQuest {
    explicit CUserQuestMenuSelectParser(CParserForQuestHelperUserQuest* parser);
    void parse(CQuestHelper& helper) override;
};

struct CQuestHelperEventDataBase {
    std::int32_t m_nQuestID = 0; // +0x00
    QuestHelper::Event m_eType = QuestHelper::None; // +0x04
    QuestHelper::FileLocation m_eFileLocation = QuestHelper::FileLocationQuest; // +0x08
    std::array<std::byte, 0x04> m_alignment0cTo10;
    std::int64_t m_nLine = 0; // +0x10
};

struct CQuestHelperTalkSelectedEventData : CQuestHelperEventDataBase {
    std::int32_t m_nChoice = 0; // +0x18
    std::array<std::byte, 0x04> m_alignment1cTo20;
    std::u16string m_strScriptFileName; // +0x20
};

struct CQuestHelperQuestAccpetedEventData : CQuestHelperEventDataBase {};

struct CQuestHelperMenuSelectedEventData : CQuestHelperEventDataBase {
    std::int64_t m_nAsk = 0; // +0x18
    std::int64_t m_nReply = 0; // +0x20
};

class CQuestHelper final : public MemoryObject {
public:
    using TalkEventList = std::vector<CQuestHelperTalkSelectedEventData>;
    using QuestAcceptedEventList = std::vector<CQuestHelperQuestAccpetedEventData>;
    using MenuEventList = std::vector<CQuestHelperMenuSelectedEventData>;
    using TalkEventMap = std::map<std::u16string, TalkEventList>;
    using QuestAcceptedEventMap = std::map<std::u16string, QuestAcceptedEventList>;
    using MenuEventMap = std::map<std::u16string, MenuEventList>;

    L2_DECLARE_OBJECT_REGISTRATION(CQuestHelper)

    CQuestHelper();
    ~CQuestHelper() override = default;

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    void checkAndOpenQuest(QuestHelper::Event event, const char16_t* npcName, ...);

    bool load();
    void reload();
    void insert(std::u16string npcName, CQuestHelperEventDataBase* eventData);
    void openHTML(wchar_t* fileName, std::int64_t line);

    bool isUseQuestOpen() { return m_bUseQuestOpen; }

    void setUseQuestOpen(bool useQuestOpen) { m_bUseQuestOpen = useQuestOpen; }

    bool isUseHTMLOpen() { return m_bUseHTMLOpen; }

    void setUseHTMLOpen(bool useHTMLOpen) { m_bUseHTMLOpen = useHTMLOpen; }

    bool isLoaded() { return m_nLoaded != 0; }

private:
    void openNotePadPlusPlus(const CQuestHelperEventDataBase* eventData, std::int64_t line);
    void openNotePadPlusPlus(const wchar_t* fileName, std::int64_t line);

    TalkEventMap m_mTalkSelectedEventData; // +0x18
    QuestAcceptedEventMap m_mQuestAccpetedEventData; // +0x38
    MenuEventMap m_mMenuSelectedEventData; // +0x58
    bool m_bUseQuestOpen; // +0x78
    bool m_bUseHTMLOpen; // +0x79
    [[maybe_unused]] std::array<std::byte, 0x02> m_alignment7aTo7c;
    volatile std::int32_t m_nLoaded; // +0x7c
    CYieldLock m_lock; // +0x80
};


extern CQuestHelper g_QuestHelper;
