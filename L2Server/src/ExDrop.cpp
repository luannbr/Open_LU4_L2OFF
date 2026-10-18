#include "ExDrop.h"
#include "MultiPartyCommandChannel.h"
#include "NPC.h"
#include "ParserExDrop.h"
#include "Party.h"
#include "Protocol/src/SystemMessageIds.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/CategoryDataDB.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Utils.h"

#include <ctime>

namespace {

std::tm* ExDropLocalTime(std::int32_t value) noexcept { return runtime::crt::LocalTime32(value); }

} // namespace

ExDropItemSystem::ExDropItemSystem() :
    m_category_pch_file(nullptr),
    m_bOnlyPrecompiledHeader(false),
    m_alignment31To34{},
    m_tmPrevResetTime(0),
    m_tmNextResetTime(0),
    m_alignment3cTo40{} {}

ExDropItemSystem::~ExDropItemSystem() = default;

void ExDropItemSystem::LoadExDropData() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\ex_drop.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/ex_drop.txt";
#endif

    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from Objects file failed",
        //     L"ExDrop.cpp",
        //     0x1c)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from Objects file failed",
        //     L"ExDrop.cpp",
        //     0x1c)
        L2_LOG_ERROR(Log, "Cannot read ex_drop.txt; extra-drop loading stops.");
        return;
    }

    CParserForExDrop parser;
    parser.m_bOnlyPrecompiledHeader = m_bOnlyPrecompiledHeader;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForCategory::create failed",
        //     L"ExDrop.cpp",
        //     0x26)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForCategory::create failed",
        //     L"ExDrop.cpp",
        //     0x26)
        L2_LOG_ERROR(Log, "Cannot initialize the ex_drop.txt parser; extra-drop loading stops.");
        return;
    }

    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing CategoryData file failed after line[%d]",
        //     L"ExDrop.cpp",
        //     0x31,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing CategoryData file failed after line[%d]",
        //     L"ExDrop.cpp",
        //     0x31,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log,
            "Cannot parse ex_drop.txt at script line %lld; already processed data is retained and reset-time initialization "
            "continues.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }

    InitLastResetTime();
}

void ExDropItemSystem::InitLastResetTime() {
    L2_GUARD_FUNCTION();

    const std::int32_t now = runtime::crt::Time32Now();
    const std::tm* const currentPointer = ExDropLocalTime(now);
    if (currentPointer == nullptr) {
        return;
    }
    const std::tm current = *currentPointer;
    std::tm reset = current;
    reset.tm_hour = 6;
    reset.tm_min = 30;
    const std::int32_t resetTime = runtime::crt::Mktime32(&reset);

    // Literal current comparison: 06:29 is already considered the current
    // reset window even though the constructed reset timestamp is 06:30.
    if (current.tm_hour < 6 || (current.tm_hour == 6 && current.tm_min < 29)) {
        m_tmNextResetTime = resetTime;
        m_tmPrevResetTime = resetTime - 86400;
    } else {
        m_tmPrevResetTime = resetTime;
        m_tmNextResetTime = resetTime + 86400;
    }
}

bool ExDropItemSystem::IsNeedInit(const std::int32_t time) const {
    L2_GUARD_FUNCTION();
    return time < m_tmPrevResetTime;
}

ExDropItemData* ExDropItemSystem::getExDrop(const int id) {
    const auto found = exDropDataMap.find(id);
    return found != exDropDataMap.end() ? found->second : nullptr;
}

void ExDropItemSystem::CheckAndDropItem(CNPC* npc, CCreature* creature) {
    // Current L2Server.exe 0x0072b380.
    L2_GUARD_FUNCTION();

    if (npc == nullptr || creature == nullptr || npc->m_vExDropItem.empty()) {
        return;
    }

    for (auto position = npc->m_vExDropItem.begin(); position != npc->m_vExDropItem.end(); ++position) {
        ExDropItemData* const dropData = *position;
        User* const owner = creature->GetUserOrMaster();
        std::vector<User*> users;
        if (owner == nullptr) {
            continue;
        }

        if (owner->GetMPCC() != nullptr) {
            owner->GetMPCC()->GetAllMember(users);
        } else if (owner->GetParty() != nullptr) {
            owner->GetParty()->GetAllMember(users);
        } else {
            users.push_back(owner);
        }

        for (User* const user : users) {
            if (!npc->IsInRange(user, 1500.0)) {
                continue;
            }
            const int level = user->Level();
            if (!dropData->IsInLevel(level)) {
                continue;
            }
            const int maximumCount = dropData->getMaxCount();
            const int currentCount = user->GetExDropCount(dropData->getCountId());
            if (currentCount >= maximumCount) {
                continue;
            }
            const int classId = user->GetClass();
            if (!dropData->IsInCategory(classId)) {
                continue;
            }

            const double baseProbability = dropData->getProb();
            const double additionalProbability = user->GetExDropAdditionalProb(dropData->getCountId());
            const double roll = ::genrand64_real1();
            if (roll * 100.0 + 0.0 <= additionalProbability + baseProbability) {
                bool acquired = false;
                const int carryingWeight = user->m_inv.GetCarryingWeight();
                const double carryingCapacity = user->CarryWeight();
                if (static_cast<double>(carryingWeight) / carryingCapacity < 0.99) {
                    {
                        InventoryParams parameters;
                        parameters.nItemClassID = dropData->getItemId();
                        parameters.nCount = dropData->getGiveCount();

                        {
                            L2_WRITE_GUARD(user->m_inv);
                            acquired = user->AddItemToInventory(parameters);
                            if (acquired) {
                                user->GetSocket()->Send(
                                    "chccdcd", 0x62, SystemMessageId::ITEM_OBTAINED_WITH_DAILY_LIMIT, 2, 3,
                                    dropData->getItemId(), 1, dropData->getMaxCount()
                                );
                            }
                        }
                    }

                    if (acquired) {
                        user->UpdateExDropCount(dropData, true);
                        break;
                    }
                }

                user->GetSocket()->Send(
                    "chccd", 0x62, SystemMessageId::CANNOT_RECEIVE_NAMED_ITEM_INVENTORY_WEIGHT_OR_QUANTITY_EXCEEDED, 1, 3,
                    dropData->getItemId()
                );
                continue;
            }

            user->UpdateExDropCount(dropData, false);
        }
    }
}

bool ExDropItemData::IsInCategory(int classId) {
    L2_GUARD_FUNCTION();

    for (const int categoryId : m_vCategory) {
        if (::CCategoryDataDB::Inst().IsInCategory(classId, categoryId)) {
            return true;
        }
    }
    return false;
}
