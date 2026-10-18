#include "PathToAwakeningNotice.h"
#include "ParserPathToAwakening.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/MemorySub.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"

namespace {


// Current ICF endpoint 0x0046c3fc returns false without emitting a message.
bool IgnoreInvalidRewardCount(CCreature*, const ItemManip*) { return false; }

} // namespace

static_assert(
    //     L2ServerMemory::kCPathToAwakeningNoticeClassId == 211)
    static_cast<std::int32_t>(ShippingMemoryClassId::CPathToAwakeningNotice) == 211
);

CNotice::~CNotice() = default;

CNotice_LevelCond::CNotice_LevelCond() : CNotice(PathToAwakening::PTA_LEVEL) {}

CNotice_LevelCond::~CNotice_LevelCond() = default;

bool CNotice_LevelCond::Exists(const int level) const {
    L2_GUARD_FUNCTION();
    return m_mapReward.find(level) != m_mapReward.end();
}

void CNotice_LevelCond::AddLevelCond(const int level, const Reward reward) {
    L2_GUARD_FUNCTION();
    if (!Exists(level)) {
        m_mapReward.emplace(level, reward);
    }
}

bool CNotice_LevelCond::CheckAwakening(User* const user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->GetSocket() == nullptr) {
        return false;
    }

    const int level = user->Level();
    const auto found = m_mapReward.find(level);
    if (found == m_mapReward.end()) {
        return false;
    }

    const int itemClassId = found->second.first;
    const std::int64_t itemCount = found->second.second;
    if (itemClassId != -1 && user->m_data->m_nSubJobId == 0) {
        ItemManip request(0, itemClassId, itemCount, 0, 0, 0, true);
        request.SetPolicyOnInvalidCount(&IgnoreInvalidRewardCount);

        L2_GUARD(user->m_ManipulateQueueLock);

        CPacketHelperEx<20480> packet;
        User::etcMsg message = user->SaveEtcMsg(false, false, 0, 0, 0, std::u16string(), std::u16string(), 0);
        User::ManipulateQueue queue = user->SaveManipulateQueue(
            ManipulateType::MANI_UNDEFINED_ITEM, 0, 1, &request, nullptr, 0, packet, false, ItemGetReason::Etc, message
        );

        const User::ManiItemResultType result = user->_SetManiItemJob(queue);
        if (result == User::MRT_NOJOB) {
            if (!user->_HandleItemManipulation(queue)) {
                user->SendSystemMessage(SystemMessageId::ITEM_CONVERSION_FAILED);
            }
        } else if (result == User::MRT_JOBMAP) {
            user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
            return false;
        } else if (result == User::MRT_JOBNOTMAP) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
            //     L"PathToAwakeningNotice.cpp",
            //     0xaa)
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
            //     L"PathToAwakeningNotice.cpp",
            //     0xaa)
            // Noise: The reward was queued normally. This is not a failure or proof of item delivery.
        } else {
            user->SendSystemMessage(SystemMessageId::ITEM_CONVERSION_FAILED);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
            //     L"PathToAwakeningNotice.cpp",
            //     0xb0)
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
            //     L"PathToAwakeningNotice.cpp",
            //     0xb0)
            L2_LOG_ERROR(
                Log,
                "Path to Awakening reward rejected for character %u at level %d: item %d, amount %lld, queue result %d.",
                static_cast<unsigned>(user->GetDBID()), level, itemClassId, static_cast<long long>(itemCount),
                static_cast<int>(result)
            );
            return false;
        }
    }

    user->GetSocket()->Send("chdd", 0xfe, 0x151, GetType(), user->Level());
    return true;
}

CPathToAwakeningNotice::CPathToAwakeningNotice() {}

CPathToAwakeningNotice::~CPathToAwakeningNotice() = default;

CPathToAwakeningNotice* CPathToAwakeningNotice::Inst() {
    static CPathToAwakeningNotice instance;
    return &instance;
}

L2_DEFINE_OBJECT_CLASS_ID(CPathToAwakeningNotice)

L2_DEFINE_OBJECT_CLASS_NAME(CPathToAwakeningNotice, m_pClassName)

L2_DEFINE_OBJECT_NEW(CPathToAwakeningNotice)

L2_DEFINE_OBJECT_ALLOCATOR(CPathToAwakeningNotice, __GetClassName())

L2_DEFINE_OBJECT_RELEASE(CPathToAwakeningNotice)

CNotice* CPathToAwakeningNotice::GetNotice(const PathToAwakening::AwakeningType type) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    const auto found = m_mapNotice.find(type);
    return found != m_mapNotice.end() ? found->second : nullptr;
}

void CPathToAwakeningNotice::AddAwakeningNotice(const PathToAwakening::AwakeningType type, CNotice* const notice) {
    L2_GUARD_FUNCTION();
    m_mapNotice.emplace(type, notice);
}

void CPathToAwakeningNotice::LoadAwakeningNotice() {
    L2_GUARD_FUNCTION();
    m_mapNotice.clear();

#if defined(_WIN32)
    constexpr const wchar_t* kDataPath = L"..\\Script\\pathtoawakening.txt";
#else
    constexpr const wchar_t* kDataPath = L"../script/pathtoawakening.txt";
#endif

    CSecureStream input(kDataPath, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from PathToAwakening file failed",
        //     L"PathToAwakeningNotice.cpp",
        //     0x31)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from PathToAwakening file failed",
        //     L"PathToAwakeningNotice.cpp",
        //     0x31)
        L2_LOG_ERROR(Log, "Cannot read pathtoawakening.txt; awakening notices remain empty.");
        return;
    }

    CParserForPathToAwakening parser;
    if (!parser.Create()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForPathToAwakening.create failed",
        //     L"PathToAwakeningNotice.cpp",
        //     0x39)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForPathToAwakening.create failed",
        //     L"PathToAwakeningNotice.cpp",
        //     0x39)
        L2_LOG_ERROR(Log, "Cannot initialize pathtoawakening.txt parser; awakening notices remain empty.");
        return;
    }

    parser.m_lexer.yybegin(4);
    parser.m_lexer.yyin = input.GetStream();
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing PathToAwakening file failed after line[%d]",
        //     L"PathToAwakeningNotice.cpp",
        //     0x45,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing PathToAwakening file failed after line[%d]",
        //     L"PathToAwakeningNotice.cpp",
        //     0x45,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "Cannot parse pathtoawakening.txt at script line %lld; notices loaded before the error are retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
}
