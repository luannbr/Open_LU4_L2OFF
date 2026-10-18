#include "BotChecker.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/Utils.h"
#include "Shared/src/platform/Runtime.h"
#include "User.h"
#include "UserSocket.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <string>

namespace {

char kBotCheckerSourceFile[] = "BotChecker.cpp";
char16_t kSystemMessageContext[] = u"SYS";

} // namespace

static_assert(
    //     L2ServerMemory::kCBotCheckInfoClassId == 69)
    static_cast<std::int32_t>(ShippingMemoryClassId::CBotCheckInfo) == 69
);
static_assert(
    //     L2ServerMemory::kCBotCheckerClassId == 70)
    static_cast<std::int32_t>(ShippingMemoryClassId::CBotChecker) == 70
);
static_assert(
    //     L2ServerMemory::kBotBlockDataClassId == 71)
    static_cast<std::int32_t>(ShippingMemoryClassId::BotBlockData) == 71
);

BotBlockData::BotBlockData(std::uint32_t userDatabaseId, std::uint32_t bannedTick) :
    m_UserDBID(userDatabaseId), m_BannedTick(bannedTick) {}

L2_DEFINE_OBJECT_CLASS_ID(BotBlockData)

L2_DEFINE_OBJECT_CLASS_NAME(BotBlockData, m_pClassName_BotBlockData)

L2_DEFINE_OBJECT_ALLOCATOR(BotBlockData, m_pClassName_BotBlockData)

L2_DEFINE_OBJECT_NEW(BotBlockData)

L2_DEFINE_OBJECT_RELEASE(BotBlockData)

CBotBlockManager::CBotBlockManager() : m_BanPeriodMSec(30000) {}

CBotBlockManager::~CBotBlockManager() = default;

int CBotBlockManager::Clear() { return m_BlockList.DeleteAllObjects(nullptr, -1); }

bool CBotBlockManager::IsBotUser(std::uint32_t userDatabaseId) {
    L2_GUARD_FUNCTION();
    const int key = static_cast<int>(userDatabaseId);
    CSPointer<BotBlockData> data = m_BlockList.FindObject(key);
    if (!data) {
        return false;
    }

    const std::uint32_t now = runtime::GetTickCount();
    if (m_BanPeriodMSec < now - data->m_BannedTick) {
        m_BlockList.DeleteObject(key, kBotCheckerSourceFile, 0x21b);
        return false;
    }
    return true;
}

bool CBotBlockManager::RemoveBotUser(std::uint32_t userDatabaseId) {
    return m_BlockList.DeleteObject(static_cast<int>(userDatabaseId), kBotCheckerSourceFile, 0x228);
}

bool CBotBlockManager::AddAsBotUser(std::uint32_t userDatabaseId) {
    L2_GUARD_FUNCTION();
    BotBlockData* const data = new BotBlockData(userDatabaseId, runtime::GetTickCount());

    const bool inserted = m_BlockList.Insert(static_cast<int>(userDatabaseId), data, kBotCheckerSourceFile, 0x206);
    L2_RELEASE(data, ObjectRefType::Anonymous);
    return inserted;
}

L2_DEFINE_OBJECT_CLASS_ID(CBotCheckInfo)

L2_DEFINE_OBJECT_CLASS_NAME(CBotCheckInfo, m_pClassName_CBotCheckInfo)

L2_DEFINE_OBJECT_ALLOCATOR(CBotCheckInfo, m_pClassName_CBotCheckInfo)

L2_DEFINE_OBJECT_NEW(CBotCheckInfo)

L2_DEFINE_OBJECT_RELEASE(CBotCheckInfo)

CBotCheckInfo::CBotCheckInfo() : m_nTickCount(0), m_nBotUserID(0) {}

// Copy only check data. Each object keeps its own reference state.
CBotCheckInfo::CBotCheckInfo(const CBotCheckInfo& other) :
    MemoryObject(), m_nTickCount(other.m_nTickCount), m_nBotUserID(other.m_nBotUserID) {}

CBotCheckInfo::CBotCheckInfo(CBotCheckInfo&& other) : CBotCheckInfo(other) {}

CBotCheckInfo& CBotCheckInfo::operator=(const CBotCheckInfo& other) noexcept {
    m_nTickCount = other.m_nTickCount;
    m_nBotUserID = other.m_nBotUserID;
    return *this;
}

CBotCheckInfo& CBotCheckInfo::operator=(CBotCheckInfo&& other) noexcept {
    return operator=(other);
}

void CBotCheckInfo::Init() {
    m_nTickCount = 0;
    m_nBotUserID = 0;
}

void CBotCheckInfo::SetData(int tickCount, int botUserId) {
    m_nTickCount = tickCount;
    m_nBotUserID = botUserId;
}

int CBotCheckInfo::GetTickCount() { return m_nTickCount; }

void CBotCheckInfo::DecTickCount() { --m_nTickCount; }

int CBotCheckInfo::GetBotUserID() { return m_nBotUserID; }

L2_DEFINE_OBJECT_CLASS_ID(CBotChecker)

L2_DEFINE_OBJECT_CLASS_NAME(CBotChecker, m_pClassName_CBotChecker)

L2_DEFINE_OBJECT_ALLOCATOR(CBotChecker, m_pClassName_CBotChecker)

L2_DEFINE_OBJECT_NEW(CBotChecker)

L2_DEFINE_OBJECT_RELEASE(CBotChecker)

CBotChecker::CBotChecker() : m_pOwner(nullptr) {}

bool CBotChecker::CheckImpl_1(User* user) {
    L2_GUARD_FUNCTION();
    if (m_pOwner == nullptr || user == nullptr) {
        return false;
    }

    const int random = static_cast<int>(::genrand64_real2() * 32768.0 + 0.0);
    const int ownerId = static_cast<int>(m_pOwner->Id());
    user->GetSocket()->Send(
        "cdddddddddddhbhddddd", 0x48, 0, ownerId, ownerId, 0x4c1, 3, random, 0, random * -0xae1 + -0xd268, 0, 0, 0, 1, 0, 0,
        0, 0, 0, 0, 0, 0
    );
    return true;
}

bool CBotChecker::CheckImpl_3(User* user) {
    L2_GUARD_FUNCTION();
    if (m_pOwner == nullptr || user == nullptr) {
        return false;
    }
    static_cast<void>(::genrand64_real2());
    return true;
}

bool CBotChecker::IsCanCheck() {
    L2_GUARD_FUNCTION();
    if (m_pOwner == nullptr) {
        return false;
    }

    L2_GUARD(m_lock);
    if (m_vInfo.size() != 10) {
        return true;
    }
    m_pOwner->SendSystemMessage(SystemMessageId::PREVIOUS_BOT_CHECK_STILL_IN_PROGRESS);
    return false;
}

void CBotChecker::ShowBotUserList() {
    L2_GUARD_FUNCTION();
    m_pOwner->SendSystemMessage(kSystemMessageContext, u"===== BOT User List =====");

    for (const std::u16string& name : m_vBotList) {
        std::array<char16_t, 128> message{};
        FormatUtf16To(message.data(), message.size(), u"* [%ls]", name);
        m_pOwner->SendSystemMessage(kSystemMessageContext, message.data());
    }

    m_pOwner->SendSystemMessage(kSystemMessageContext, u"=========================");
}

void CBotChecker::ShowBotUserCheckList() {
    L2_GUARD_FUNCTION();
    m_pOwner->SendSystemMessage(kSystemMessageContext, u"===== BOT User Check List =========");

    for (CBotCheckInfo& info : m_vInfo) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(info.GetBotUserID()));
        User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
        if (user == nullptr) {
            continue;
        }

        const int tickCount = info.GetTickCount();
        const int minuteValue = runtime::Win64CvttDoubleToInt32(static_cast<double>(tickCount) * 0.666) / 60;
        const int secondValue = runtime::Win64CvttDoubleToInt32(static_cast<double>(tickCount) * 0.666) % 60;
        std::array<char16_t, 128> message{};
        FormatUtf16To(
            message.data(), message.size(), u"* [%ls] - %d min %02d sec remained.", user->GetName(), minuteValue, secondValue
        );
        m_pOwner->SendSystemMessage(kSystemMessageContext, message.data());
    }

    m_pOwner->SendSystemMessage(kSystemMessageContext, u"===================================");
}

void CBotChecker::Init(User* owner) {
    L2_GUARD_FUNCTION();
    m_pOwner = owner;
    m_vInfo.clear();
    DeleteBotUserList();
}

void CBotChecker::ReleaseOwner(User* owner [[maybe_unused]]) {
    L2_GUARD_FUNCTION();
    DeleteBotUserList();
}

void CBotChecker::SetData(int botUserId, int tickCount) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    CBotCheckInfo info;
    info.SetData(tickCount, botUserId);
    m_vInfo.push_back(info);
}

bool CBotChecker::BeginCheck(int checkType, User* user, int tickCount) {
    L2_GUARD_FUNCTION();
    if (!IsCanCheck() || user == nullptr || tickCount < 0) {
        return false;
    }

    if (static_cast<unsigned int>(checkType) > 3U) {
        m_pOwner->SendSystemMessage(kSystemMessageContext, u"//check_bot [1-3] [time(min)]");
        return false;
    }

    int effectiveCheckType = checkType;
    if (checkType != 0) {
        if (::g_nCountryCode == 3 || ::g_nCountryCode == 4) {
            effectiveCheckType = 2;
        } else if (::g_nCountryCode == 1 || ::g_nCountryCode == 2 || ::g_nCountryCode == 5 || ::g_nCountryCode == 6) {
            effectiveCheckType = 3;
        }
    }

    if (effectiveCheckType == 0) {
        ShowBotUserList();
        ShowBotUserCheckList();
        return true;
    }

    bool started = false;
    if (effectiveCheckType == 1) {
        started = CheckImpl_1(user);
    } else if (effectiveCheckType == 3) {
        started = CheckImpl_3(user);
    }

    if (started) {
        const std::wstring ownerName = Utf16ToWide(m_pOwner->GetName());
        const std::wstring userName = Utf16ToWide(user->GetName());
        const std::wstring ownerAccountName =
            Utf16ToWide(FixedUtf16View(m_pOwner->m_sAccountName), Utf16WidePolicy::PreserveInvalid);
        const std::wstring userAccountName =
            Utf16ToWide(FixedUtf16View(user->m_sAccountName), Utf16WidePolicy::PreserveInvalid);
        Log.Add(
            LOG_AUDIT,
            u"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,,"
            u"%I64d,%I64d,,,,,,,,,%s,%s,%s,%s,",
            std::int64_t{0x59a}, static_cast<std::int64_t>(m_pOwner->GetDBID()),
            static_cast<std::int64_t>(m_pOwner->GetAccountID()), static_cast<std::int64_t>(user->GetDBID()),
            static_cast<std::int64_t>(user->GetAccountID()), static_cast<std::int64_t>(tickCount),
            static_cast<std::int64_t>(effectiveCheckType), ownerName.c_str(), ownerAccountName.c_str(), userName.c_str(),
            userAccountName.c_str()
        );
        user->m_bBeingCheckedIfBot = true;
        SetData(static_cast<int>(user->Id()), tickCount);
    }
    return started;
}

void CBotChecker::AddBotUser(const char16_t* name) {
    L2_GUARD_FUNCTION();
    m_vBotList.emplace_back(name);
}

void CBotChecker::OnTimerExpired() {
    L2_GUARD_FUNCTION();
    for (auto iterator = m_vInfo.begin(); iterator != m_vInfo.end();) {
        const int tickCount = iterator->GetTickCount();

        if (tickCount == (tickCount / 90) * 90) {
            CCreature* const creature =
                g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(iterator->GetBotUserID()));
            User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
            if (user != nullptr && user->IsPC() && user->m_data->m_bIsActive) {
                const std::u16string userName = std::u16string(user->GetName());
                const int remaining = static_cast<std::int8_t>(tickCount / 90);
                m_pOwner->GetSocket()->Send(
                    "chccccS", 0x62, SystemMessageId::BOT_CHECK_COMPLETES_IN_MINUTES, 2, 0x14, remaining, 0x0c,
                    userName.c_str()
                );
            }
        }

        if (tickCount == 1) {
            CCreature* const creature =
                g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(iterator->GetBotUserID()));
            User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
            if (user != nullptr && user->IsPC() && user->m_data->m_bIsActive) {
                const std::u16string packetUserName = std::u16string(user->GetName());
                m_pOwner->GetSocket()->Send(
                    "chccS", 0x62, SystemMessageId::INVESTIGATED_USER_IDENTIFIED_AS_BOT, 1, 0x0c, packetUserName.c_str()
                );
                AddBotUser(user->GetName());
                // Log.Add(
                //     LOG_ERROR,
                //     L"%s character is bot!!!!",
                //     user->GetName())
                // L2_LOG_ERROR(Log,
                //     L"%s character is bot!!!!",
                //     user->GetName())
                // L2_LOG_WARN(Log,
                //     "Bot-check timer identified character '%s' (DB ID %u) as a bot. The owner notification and bot-list
                //     update were requested.", WideToUtf8(std::wstring_view(user->GetName())).c_str(),
                //     static_cast<unsigned>(user->GetDBID()))
                L2_LOG_WARN(
                    Log,
                    "Bot-check timer identified character '%s' (DB ID %u) as a bot; the owner notification and bot-list "
                    "update were requested.",
                    Utf16ToUtf8(user->GetName()).c_str(), static_cast<unsigned>(user->GetDBID())
                );

                const FVector position = user->GetPos();
                const std::u16string ipAddress = user->GetSocket()->GetIPAddressString();
                const std::u16string ipAddress16 = std::u16string(ipAddress.c_str());
                const std::u16string auditUserName = std::u16string(user->GetName());
                const int userLevel = user->Level();
                const std::uint32_t userAccountId = user->GetAccountID();
                const std::uint32_t userDatabaseId = user->GetDBID();
                CLogFormatter audit;
                audit.WriteAuditLog(
                    0x585, userDatabaseId, userAccountId, 0, 0, runtime::Win64CvttDoubleToInt32(position.x),
                    runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z),
                    ipAddress16.c_str(), u"", u"", 99, 0, 0, 0, 2, 0, 0, 0, 0, userLevel, auditUserName.c_str(),
                    user->m_sAccountName.data(), u"", u"", 0
                );
                user->m_bBeingCheckedIfBot = false;
            } else {
                m_pOwner->SendSystemMessage(SystemMessageId::INVESTIGATED_USER_APPEARS_NOT_TO_BE_BOT);
            }
        }

        iterator->DecTickCount();
        if (iterator->GetTickCount() == 0) {
            iterator = m_vInfo.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void CBotChecker::DeleteBotUserList() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    m_vBotList.clear();
}
