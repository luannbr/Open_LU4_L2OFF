#include "OlympiadStuff.h"
#include "Shared/src/MemorySub.h"

#include "Auth.h"
#include "CConfig.h"
#include "DB.h"
#include "InstantZone.h"
#include "Item.h"
#include "L2Packet.h"
#include "Object.h"
#include "OlympiadMatch.h"
#include "OlympiadService.h"
#include "OlympiadServiceProxy.h"
#include "OlympiadSystem.h"
#include "Party.h"
#include "Pet.h"
#include "Pledge.h"
#include "Relay.h"
#include "SerializedMessage.h"
#include "Setting.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "User.h"
#include "UserSocket.h"
#include "XmlLoader.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"
#include "Shared/src/net/Serialize.h"
#include "Shared/src/proto/OlympiadPacket.h"
#include "Shared/src/proto/Stream.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#if defined(_WIN32)
#include <WinSock2.h>
#else
#include <arpa/inet.h>
#endif

OlympiadConnectVerifier::OlympiadConnectVerifier(CUserSocket* const socket) : m_pUserSock(socket) {
    // Current L2Server 0x009108a4.
    L2_ADD_REF(m_pUserSock, ObjectRefType::VerifierRetain);
    AddTimer(30000, 0);
    ReportOrt("OlympiadStuff.cpp", 0x2d2);
}

void OlympiadConnectVerifier::TimerExpired(int) {
    // Current L2Server 0x00910930.
    L2_GUARD_FUNCTION();
    L2_RELEASE(m_pUserSock, ObjectRefType::VerifierRetain);
}

WOlympiadUser::WOlympiadUser(User* const user) :
    m_Self(user),
    m_GameRule(GRT_MAX),
    m_FieldId(0),
    m_Damage(0),
    m_IsAlive(true),
    m_nStartPosIndex(0),
    m_pInstantZone(nullptr) {
    // Current L2Server 0x009109ec. The class allocator clears all 0x78
    // bytes first. The constructor leaves m_IsObserver and the
    // alignment lanes untouched after that clear.
}

// Current L2Server 0x00911940. Allocator registration number 205 is
// fixed by the current CRT initializer at 0x00da561c.
L2_DEFINE_OBJECT_RELEASE(WOlympiadUser)

WOlympiadMatch* WOlympiadUser::GetField() {
    // Current L2Server 0x00911db0.
    L2_GUARD_FUNCTION();
    return WOlympiadService::inst().GetField(m_FieldId);
}

WOlympiadUserStateMachine::UserState WOlympiadUser::GetStatus() const {
    // Current L2Server ICF body 0x00a49a2c.
    return m_StatusMachine.GetStatus();
}

bool WOlympiadUser::CanAttack() {
    // Current L2Server 0x00912350.
    const WOlympiadUserStateMachine::UserState status = m_StatusMachine.GetStatus();
    if (status != WOlympiadUserStateMachine::OLYMPIAD_PLAYER1 && status != WOlympiadUserStateMachine::OLYMPIAD_PLAYER2) {
        return true;
    }
    WOlympiadMatch* const field = GetField();
    return field == nullptr || field->CanAttack();
}

namespace {

WOlympiadServiceConfig::Server ToServer(const XMLElement* element) {
    // Current L2Server 0x00914d8c assumes a non-null element.
    L2_GUARD_FUNCTION();
    WOlympiadServiceConfig::Server result{};

    result.idWorld = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"WorldID"), 0);

    std::array<char, 16> address{};
    const std::u16string internalAddress =
        XMLUtil::GetAttributeValue(element, std::u16string(u"InternalIP"), std::u16string(u""));
    UnicodeToAnsi(const_cast<char16_t*>(internalAddress.c_str()), 16, address.data(), 16);
    result.nInternalAddress = ::inet_addr(address.data());

    const std::u16string externalAddress =
        XMLUtil::GetAttributeValue(element, std::u16string(u"ExternalIP"), std::u16string(u""));
    UnicodeToAnsi(const_cast<char16_t*>(externalAddress.c_str()), 16, address.data(), 16);
    result.nExternalAddress = ::inet_addr(address.data());

    result.nPort = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"Port"), 0);
    return result;
}

} // namespace

bool WOlympiadStuff::DecodePacket(network::IPacket& packet, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    if (!source) {
        return false;
    }

    network::Stream stream(const_cast<std::uint8_t*>(source), 0x4000);
    return packet.Decode(&stream);
}

void WOlympiadStuff::Convert(const olympiad::PkOlympiadAuthInfo* source, stAccountAuthInfo* destination) {
    L2_GUARD_FUNCTION();
    if (!source || !destination) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadStuff.cpp",
        //     source ? 0x6c2 : 0x6c1,
        //     L"WO_ASSERT_NOT_NULL")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadStuff.cpp",
        //     source ? 0x6c2 : 0x6c1,
        //     L"WO_ASSERT_NOT_NULL")
        L2_LOG_ERROR(
            Log,
            "Cannot convert Olympiad authentication packet to account data: source available %s, destination available %s.",
            source ? "yes" : "no", destination ? "yes" : "no"
        );
        return;
    }

    destination->nTimeLeft = source->nTimeLeft;
    destination->nStat = source->nUserStat;
    destination->nWarnFlag = source->nWarnFlag;
    destination->nLoginFlag = source->nLoginFlag;
    destination->nAge = source->nAge;
    destination->cChargeMethod = source->cChargeMethod;
    destination->nReservations = source->nReservations;
    destination->nRestTime_china = source->nRestTime_china;
    destination->nPlayTime_china = source->nPlayTime_china;
    std::memcpy(destination->sPCCafeCode, source->sPCCafeCode, sizeof(destination->sPCCafeCode));
    destination->sPCCafeCode[sizeof(destination->sPCCafeCode) - 1] = '\0';
    destination->client_addr.s_addr = static_cast<std::uint32_t>(source->nClientAddress);
}

void WOlympiadStuff::Convert(const stAccountAuthInfo* source, olympiad::PkOlympiadAuthInfo* destination) {
    L2_GUARD_FUNCTION();
    if (!source || !destination) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadStuff.cpp",
        //     source ? 0x6d8 : 0x6d7,
        //     L"WO_ASSERT_NOT_NULL")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadStuff.cpp",
        //     source ? 0x6d8 : 0x6d7,
        //     L"WO_ASSERT_NOT_NULL")
        L2_LOG_ERROR(
            Log,
            "Cannot convert account authentication data to Olympiad packet: source available %s, destination available %s.",
            source ? "yes" : "no", destination ? "yes" : "no"
        );
        return;
    }

    destination->nTimeLeft = source->nTimeLeft;
    destination->nUserStat = source->nStat;
    destination->nWarnFlag = source->nWarnFlag;
    destination->nLoginFlag = source->nLoginFlag;
    destination->nAge = source->nAge;
    destination->cChargeMethod = source->cChargeMethod;
    destination->nReservations = source->nReservations;
    destination->nRestTime_china = source->nRestTime_china;
    destination->nPlayTime_china = source->nPlayTime_china;
    std::memcpy(destination->sPCCafeCode, source->sPCCafeCode, sizeof(destination->sPCCafeCode));
    destination->sPCCafeCode[sizeof(destination->sPCCafeCode) - 1] = '\0';
    destination->nClientAddress = static_cast<int>(source->client_addr.s_addr);
}

void WOlympiadStuff::ConvertNoblessInfo(
    const SharedNoblessInfo* const source, olympiad::OSN_NOBLESS_INFO* const destination
) {
    // Current L2Server 0x0091141c.
    destination->nUserDBID = source->nCharDbId;
    destination->nHeroType = source->nHeroType;
    destination->nWinCount = source->nWinCount;
    destination->nPreviousPoint = source->nPreviousPoint;
    destination->nOlympiadPoint = source->nOlympiadPoint;
    destination->nMatchCount = source->nMatchCount;
    destination->nOlympiadWinCount = source->nOlympiadWinCount;
    destination->nOlympiadLoseCount = source->nOlympiadLoseCount;
    destination->nOlympiadTradePoint = source->nOlympiadTradePoint;
}

User* WOlympiadStuff::FindUserBySID(const std::uint32_t idUser) {
    // Current L2Server 0x00913818.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(idUser);
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr || user->IsDeleting() || user->m_Deleted != 0) {
        return nullptr;
    }
    return user;
}

CSPointer<User> WOlympiadStuff::FindUserByKey(std::CDualKey<int, int> key) {
    L2_GUARD_FUNCTION();

    CSPointer<User> user = g_UserDBIDMap.FindObject(key);
    if (!user || user->IsDeleting() || user->m_Deleted != 0) {
        return {};
    }
    return user;
}

bool WOlympiadStuff::IsAllowedClass(ClassType classType) {
    L2_GUARD_FUNCTION();
    for (int index = 0; index < OlympiadDefine::HERO_CLASS_COUNT; ++index) {
        if (OlympiadDefine::GetHeroClassType(index) == classType) {
            return true;
        }
    }
    return false;
}

bool WOlympiadStuff::IsAllowedUser(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadStuff.cpp",
        //     0x73b,
        //     L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadStuff.cpp",
        //     0x73b,
        //     L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_WARN(Log, "Cannot check Olympiad eligibility: user is missing.");
        return false;
    }
    const RaceType race = user->m_data->m_Race;
    if (race != static_cast<RaceType>(1) && race != static_cast<RaceType>(2)) {
        return false;
    }
    return IsAllowedClass(user->m_data->m_nClass) && user->m_data->m_nLevel >= 85;
}

bool WOlympiadStuff::HasEnoughSpaceInInventory(CSPointer<User> user) {
    // Current L2Server 0x009135cc.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadStuff.cpp",
        //     0x8e4,
        //     L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadStuff.cpp",
        //     0x8e4,
        //     L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_WARN(Log, "Cannot check inventory capacity for Olympiad: user is missing.");
        return false;
    }
    const double weightLimit = user->GetWeightLimit(69000.0);
    return static_cast<double>(user->m_data->m_nWeightPoint) <= weightLimit * 0.800000011920929 &&
        user->m_inv.GetTotalCount() <= static_cast<int>(static_cast<double>(user->GetInventoryLimit()) * 0.800000011920929);
}

bool WOlympiadStuff::IsTalkableDistance(CSPointer<User> user, const int npcIndex) {
    // Current L2Server 0x00914894.
    L2_GUARD_FUNCTION();
    CSPointer<CCreature> npc = CCreatureFactory::Inst().FindObjectSP(npcIndex);
    if (!npc || !npc->IsNPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]invalid npc index: %d",
        //     L"OlympiadStuff.cpp",
        //     0x6f1,
        //     npcIndex)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]invalid npc index: %d",
        //     L"OlympiadStuff.cpp",
        //     0x6f1,
        //     npcIndex)
        L2_LOG_WARN(
            Log, "Cannot check Olympiad NPC interaction distance: object ID %u is missing or is not an NPC.",
            static_cast<unsigned int>(npcIndex)
        );
        return false;
    }
    return user->IsTalkableDistance(npc.get());
}

bool WOlympiadStuff::SendPacketTo(std::uint32_t serverId, S_EX_PROTOCOL protocol, const network::IPacket& packet) {
    L2_GUARD_FUNCTION();

    User* const user = FindUserBySID(serverId);
    if (!user) {
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    if (!socket) {
        return false;
    }

    socket->Send(S_EX, protocol, const_cast<network::IPacket&>(packet));
    return true;
}

void WOlympiadStuff::Write_LOG_ADD_OLYMPIAD_ENTRY_InPeer(CSPointer<User> user, const GameRuleType gameRule) {
    // Current L2Server 0x00912a3c.
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadStuff.cpp",
        //     0x7dc,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadStuff.cpp",
        //     0x7dc,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_ERROR(Log, "Cannot write Olympiad entry record for rule %d: user is missing.", static_cast<int>(gameRule));
        return;
    }

    const FVector position = user->GetPos();
    const std::wstring accountName =
        Utf16ToWide(std::u16string_view(user->m_sAccountName.data(), user->m_sAccountName.size()));
#if defined(_WIN32)
    Log.Add(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,"
        u"%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
        0x35c, static_cast<int>(user->GetDBID()), static_cast<int>(user->GetAccountID()), static_cast<int>(position.x),
        static_cast<int>(position.y), static_cast<int>(position.z), static_cast<int>(user->m_data->m_Race),
        static_cast<int>(user->m_data->m_Sex), static_cast<int>(user->m_data->m_nClass), user->m_data->m_nLevel,
        static_cast<int>(gameRule), user->GetName(), accountName.c_str()
    );
#else
    // Portable: %I64d becomes %lld. Retain signed values with matching varargs types.
    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,"
    //     L"%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
    //     0x35c,
    //     static_cast<int>(user->GetDBID()),
    //     static_cast<int>(user->GetAccountID()),
    //     static_cast<int>(position.x),
    //     static_cast<int>(position.y),
    //     static_cast<int>(position.z),
    //     static_cast<int>(user->m_data->m_Race),
    //     static_cast<int>(user->m_data->m_Sex),
    //     static_cast<int>(user->m_data->m_nClass),
    //     user->m_data->m_nLevel,
    //     static_cast<int>(gameRule),
    //     user->GetName(),
    //     accountName.c_str())
    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,"
    //     L"%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
    //     static_cast<long long>(0x35c),
    //     static_cast<long long>(static_cast<int>(user->GetDBID())),
    //     static_cast<long long>(static_cast<int>(user->GetAccountID())),
    //     static_cast<long long>(static_cast<int>(position.x)),
    //     static_cast<long long>(static_cast<int>(position.y)),
    //     static_cast<long long>(static_cast<int>(position.z)),
    //     static_cast<long long>(static_cast<int>(user->m_data->m_Race)),
    //     static_cast<long long>(static_cast<int>(user->m_data->m_Sex)),
    //     static_cast<long long>(static_cast<int>(user->m_data->m_nClass)),
    //     static_cast<long long>(user->m_data->m_nLevel),
    //     static_cast<long long>(static_cast<int>(gameRule)),
    //     user->GetName(),
    //     accountName.c_str())
    Log.Add(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,"
        u"%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
        static_cast<long long>(0x35c), static_cast<long long>(static_cast<int>(user->GetDBID())),
        static_cast<long long>(static_cast<int>(user->GetAccountID())), static_cast<long long>(static_cast<int>(position.x)),
        static_cast<long long>(static_cast<int>(position.y)), static_cast<long long>(static_cast<int>(position.z)),
        static_cast<long long>(static_cast<int>(user->m_data->m_Race)),
        static_cast<long long>(static_cast<int>(user->m_data->m_Sex)),
        static_cast<long long>(static_cast<int>(user->m_data->m_nClass)), static_cast<long long>(user->m_data->m_nLevel),
        static_cast<long long>(static_cast<int>(gameRule)), user->GetName(), accountName.c_str()
    );
#endif
}

void WOlympiadStuff::Write_LOG_REMOVE_OLYMPIAD_ENTRY_InPeer(CSPointer<User> user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadStuff.cpp",
        //     0x814,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadStuff.cpp",
        //     0x814,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_ERROR(Log, "Cannot write Olympiad entry removal record: user is missing.");
        return;
    }

    const FVector position = user->GetPos();
    const std::wstring accountName =
        Utf16ToWide(std::u16string_view(user->m_sAccountName.data(), user->m_sAccountName.size()));
#if defined(_WIN32)
    Log.Add(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,"
        u"%I64d,%I64d,%I64d,,,,,,%s,%s,,,%I64d",
        0x35d, static_cast<int>(user->GetDBID()), static_cast<int>(user->GetAccountID()), static_cast<int>(position.x),
        static_cast<int>(position.y), static_cast<int>(position.z), static_cast<int>(user->m_data->m_Race),
        static_cast<int>(user->m_data->m_Sex), static_cast<int>(user->m_data->m_nClass), user->m_data->m_nLevel, 3,
        user->GetName(), accountName.c_str(), 0
    );
#else
    // Portable: %I64d becomes %lld. Retain signed values with matching varargs types.
    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,"
    //     L"%I64d,%I64d,%I64d,,,,,,%s,%s,,,%I64d",
    //     0x35d,
    //     static_cast<int>(user->GetDBID()),
    //     static_cast<int>(user->GetAccountID()),
    //     static_cast<int>(position.x),
    //     static_cast<int>(position.y),
    //     static_cast<int>(position.z),
    //     static_cast<int>(user->m_data->m_Race),
    //     static_cast<int>(user->m_data->m_Sex),
    //     static_cast<int>(user->m_data->m_nClass),
    //     user->m_data->m_nLevel,
    //     3,
    //     user->GetName(),
    //     accountName.c_str(),
    //     0)
    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,"
    //     L"%I64d,%I64d,%I64d,,,,,,%s,%s,,,%I64d",
    //     static_cast<long long>(0x35d),
    //     static_cast<long long>(static_cast<int>(user->GetDBID())),
    //     static_cast<long long>(static_cast<int>(user->GetAccountID())),
    //     static_cast<long long>(static_cast<int>(position.x)),
    //     static_cast<long long>(static_cast<int>(position.y)),
    //     static_cast<long long>(static_cast<int>(position.z)),
    //     static_cast<long long>(static_cast<int>(user->m_data->m_Race)),
    //     static_cast<long long>(static_cast<int>(user->m_data->m_Sex)),
    //     static_cast<long long>(static_cast<int>(user->m_data->m_nClass)),
    //     static_cast<long long>(user->m_data->m_nLevel),
    //     static_cast<long long>(3),
    //     user->GetName(),
    //     accountName.c_str(),
    //     static_cast<long long>(0))
    Log.Add(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,"
        u"%I64d,%I64d,%I64d,,,,,,%s,%s,,,%I64d",
        static_cast<long long>(0x35d), static_cast<long long>(static_cast<int>(user->GetDBID())),
        static_cast<long long>(static_cast<int>(user->GetAccountID())), static_cast<long long>(static_cast<int>(position.x)),
        static_cast<long long>(static_cast<int>(position.y)), static_cast<long long>(static_cast<int>(position.z)),
        static_cast<long long>(static_cast<int>(user->m_data->m_Race)),
        static_cast<long long>(static_cast<int>(user->m_data->m_Sex)),
        static_cast<long long>(static_cast<int>(user->m_data->m_nClass)), static_cast<long long>(user->m_data->m_nLevel),
        static_cast<long long>(3), user->GetName(), accountName.c_str(), static_cast<long long>(0)
    );
#endif
}

void WOlympiadStuff::Write_LOG_SET_HERO_InPeer(CSPointer<User> user, const int winCount) {
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadStuff.cpp",
        //     0x84f,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadStuff.cpp",
        //     0x84f,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_ERROR(Log, "Cannot write Olympiad hero record with win count %d: user is missing.", winCount);
        return;
    }

    const FVector position = user->GetPos();
    const std::wstring accountName =
        Utf16ToWide(std::u16string_view(user->m_sAccountName.data(), user->m_sAccountName.size()));
#if defined(_WIN32)
    Log.Add(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,"
        u"%I64d,%I64d,,%I64d,,,,,%s,%s,,,",
        0x35e, static_cast<int>(user->GetDBID()), static_cast<int>(user->GetAccountID()), static_cast<int>(position.x),
        static_cast<int>(position.y), static_cast<int>(position.z), static_cast<int>(user->m_data->m_Race),
        static_cast<int>(user->m_data->m_Sex), static_cast<int>(user->m_data->m_nClass), user->m_data->m_nLevel, winCount,
        user->GetName(), accountName.c_str()
    );
#else
    // Portable: %I64d becomes %lld. Retain signed values with matching varargs types.
    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,"
    //     L"%I64d,%I64d,,%I64d,,,,,%s,%s,,,",
    //     0x35e,
    //     static_cast<int>(user->GetDBID()),
    //     static_cast<int>(user->GetAccountID()),
    //     static_cast<int>(position.x),
    //     static_cast<int>(position.y),
    //     static_cast<int>(position.z),
    //     static_cast<int>(user->m_data->m_Race),
    //     static_cast<int>(user->m_data->m_Sex),
    //     static_cast<int>(user->m_data->m_nClass),
    //     user->m_data->m_nLevel,
    //     winCount,
    //     user->GetName(),
    //     accountName.c_str())
    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,"
    //     L"%I64d,%I64d,,%I64d,,,,,%s,%s,,,",
    //     static_cast<long long>(0x35e),
    //     static_cast<long long>(static_cast<int>(user->GetDBID())),
    //     static_cast<long long>(static_cast<int>(user->GetAccountID())),
    //     static_cast<long long>(static_cast<int>(position.x)),
    //     static_cast<long long>(static_cast<int>(position.y)),
    //     static_cast<long long>(static_cast<int>(position.z)),
    //     static_cast<long long>(static_cast<int>(user->m_data->m_Race)),
    //     static_cast<long long>(static_cast<int>(user->m_data->m_Sex)),
    //     static_cast<long long>(static_cast<int>(user->m_data->m_nClass)),
    //     static_cast<long long>(user->m_data->m_nLevel),
    //     static_cast<long long>(winCount),
    //     user->GetName(),
    //     accountName.c_str())
    Log.Add(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,"
        u"%I64d,%I64d,,%I64d,,,,,%s,%s,,,",
        static_cast<long long>(0x35e), static_cast<long long>(static_cast<int>(user->GetDBID())),
        static_cast<long long>(static_cast<int>(user->GetAccountID())), static_cast<long long>(static_cast<int>(position.x)),
        static_cast<long long>(static_cast<int>(position.y)), static_cast<long long>(static_cast<int>(position.z)),
        static_cast<long long>(static_cast<int>(user->m_data->m_Race)),
        static_cast<long long>(static_cast<int>(user->m_data->m_Sex)),
        static_cast<long long>(static_cast<int>(user->m_data->m_nClass)), static_cast<long long>(user->m_data->m_nLevel),
        static_cast<long long>(winCount), user->GetName(), accountName.c_str()
    );
#endif
}

void WOlympiadStuff::Write_LOG_WOLYMPIAD_REMOVE_ENTRY_InHost(
    const int worldId, const int userDbId, const olympiad::PkApplication& application
) {
    // Current L2Server 0x0091282c.
    L2_GUARD_FUNCTION();
    CLogFormatter formatter{};
    formatter.WriteLog(
        0x1fd8, userDbId, application.nAccountID, 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, application.nClassType,
        application.nLevel, 0, 0, 0, 0, 0, 0, 0, worldId, application.sCharName.c_str(), application.sAccountName.c_str(),
        nullptr, nullptr, 0
    );
}

void WOlympiadStuff::Write_LOG_WOLYMPIAD_ADD_ENTRY_InHost(
    const int worldId, const int userDbId, const olympiad::PkApplication& application
) {
    // Current L2Server 0x009126f0.
    L2_GUARD_FUNCTION();
    CLogFormatter formatter{};
    formatter.WriteLog(
        0x1fd7, userDbId, application.nAccountID, 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, application.nClassType,
        application.nLevel, 0, 0, 0, 0, 0, 0, 0, worldId, application.sCharName.c_str(), application.sAccountName.c_str(),
        nullptr, nullptr, 0
    );
}

void WOlympiadStuff::Write_LOG_WOLYMPIAD_SET_HERO_InHost(
    const int worldId, const int userDbId, const char16_t* const charName, const int classType, const int winCount
) {
    // Current L2Server 0x00912f04.
    L2_GUARD_FUNCTION();
    if (charName == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadStuff.cpp",
        //     0x869,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadStuff.cpp",
        //     0x869,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_ERROR(
            Log, "Cannot write Olympiad hero record for world %d, character DB ID %d: character name is missing.", worldId,
            userDbId
        );
        return;
    }
    CLogFormatter formatter{};
    formatter.WriteLog(
        0x1fd9, userDbId, 0, 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, classType, 0, winCount, 0, 0, 0, 0, 0, 0, worldId,
        charName, nullptr, nullptr, nullptr, 0
    );
}

void WOlympiadStuff::Write_LOG_WOLYMPIAD_SET_NOBLESS_InHost(
    const int worldId, const int userDbId, const char16_t* const charName, const int classType, const int heroClassType
) {
    // Current L2Server 0x00913010.
    L2_GUARD_FUNCTION();
    if (charName == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadStuff.cpp",
        //     0x93b,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadStuff.cpp",
        //     0x93b,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_ERROR(
            Log, "Cannot write Olympiad nobless record for world %d, character DB ID %d: character name is missing.",
            worldId, userDbId
        );
        return;
    }
    CLogFormatter formatter{};
    formatter.WriteLog(
        0x1fda, userDbId, 0, 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, classType, 0, heroClassType, 0, 0, 0, 0, 0, 0,
        worldId, charName, nullptr, nullptr, nullptr, 0
    );
}

void WOlympiadStuff::Write_LOG_WOLYMPIAD_ENTER_INZONE_InHost(
    CSPointer<User> user,
    const int instantZoneTypeId,
    const int instantZoneId,
    const int fieldId,
    const LogOlympiadEnterType enterType,
    const int matchCount,
    const int gameRule
) {
    // Current L2Server 0x00912f6c.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadStuff.cpp",
        //     0x894,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadStuff.cpp",
        //     0x894,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_ERROR(
            Log, "Cannot write Olympiad zone entry record for field %d, zone type %d, zone ID %d: user is missing.", fieldId,
            instantZoneTypeId, instantZoneId
        );
        return;
    }

    const FVector position = user->GetPos();
    CLogFormatter formatter{};
    formatter.WriteLog(
        0x1fdb, user->GetDBID(), user->GetAccountID(), 0, 0, static_cast<int>(position.x), static_cast<int>(position.y),
        static_cast<int>(position.z), nullptr, nullptr, nullptr, static_cast<int>(user->m_data->m_Sex),
        static_cast<int>(user->m_data->m_Race), instantZoneTypeId, instantZoneId, fieldId, static_cast<int>(enterType),
        matchCount, gameRule, 0, user->m_data->m_nWorldId, user->m_data->Name(), user->m_sAccountName.data(), nullptr,
        nullptr, 0
    );
}

void WOlympiadStuff::Write_LOG_WOLYMPIAD_EXIT_INZONE_InHost(
    CSPointer<User> user,
    const int instantZoneTypeId,
    const int instantZoneId,
    const int fieldId,
    const LogOlympiadEnterType enterType,
    const int matchCount
) {
    // Current L2Server 0x009132a0.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadStuff.cpp",
        //     0x8bf,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadStuff.cpp",
        //     0x8bf,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_ERROR(
            Log, "Cannot write Olympiad zone exit record for field %d, zone type %d, zone ID %d: user is missing.", fieldId,
            instantZoneTypeId, instantZoneId
        );
        return;
    }

    const FVector position = user->GetPos();
    CLogFormatter formatter{};
    formatter.WriteLog(
        0x1fdc, user->GetDBID(), user->GetAccountID(), 0, 0, static_cast<int>(position.x), static_cast<int>(position.y),
        static_cast<int>(position.z), nullptr, nullptr, nullptr, static_cast<int>(user->m_data->m_Sex),
        static_cast<int>(user->m_data->m_Race), instantZoneTypeId, instantZoneId, fieldId, static_cast<int>(enterType),
        matchCount, 0, 0, user->m_data->m_nWorldId, user->m_data->Name(), user->m_sAccountName.data(), nullptr, nullptr, 0
    );
}

WOlympiadServiceConfig::WOlympiadServiceConfig() : m_Server{} {}

WOlympiadServiceConfig::~WOlympiadServiceConfig() = default;

void WOlympiadServiceConfig::Load(const std::u16string& filename) {
    L2_GUARD_FUNCTION();

    const std::wstring hostFilename = Utf16ToWide(runtime::StringView(filename));
    XMLElement* const root = Singleton<CXmlLoader>::inst().Load(hostFilename.c_str());
    if (!root) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d] Failed to load : %s",
        //     L"OlympiadStuff.cpp",
        //     0x5d,
        //     hostFilename.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d] Failed to load : %s",
        //     L"OlympiadStuff.cpp",
        //     0x5d,
        //     hostFilename.c_str())
        L2_LOG_ERROR(
            Log, "Cannot load Olympiad service configuration from %s; settings are unchanged.",
            WideToUtf8(hostFilename).c_str()
        );
        return;
    }

    const XMLElement* const server = XMLUtil::FindChild(root, std::u16string(u"Server"));
    if (server) {
        m_Server = ToServer(server);
    }

    const XMLElement* const allow = XMLUtil::FindChild(root, std::u16string(u"Allow"));
    if (allow) {
        for (XMLElement* child : allow->m_vChild) {
            const int worldId = XMLUtil::GetAttributeValueAsInt(child, std::u16string(u"WorldID"), 0);
            const int quota = XMLUtil::GetAttributeValueAsInt(child, std::u16string(u"Quota"), 0);
            m_ClientQuotaMap[worldId] = quota;
        }
    }

    root->ReleaseChild();
    delete root;
}

bool WOlympiadServiceConfig::IsAllowedPeer(int worldId) const {
    L2_GUARD_FUNCTION();
    return m_ClientQuotaMap.count(worldId) != 0U;
}

bool WOlympiadServiceConfig::IsActive() const {
    // Current L2Server 0x0091027c.
    return m_Server.idWorld != 0;
}

bool WOlympiadServiceConfig::IsHost() const { return g_nWorldId == m_Server.idWorld; }

bool WOlympiadServiceConfig::IsPeer() const {
    return g_nWorldId != m_Server.idWorld && m_ClientQuotaMap.count(g_nWorldId) != 0U;
}

bool WOlympiadServiceConfig::IsReturningFromHost(int worldId) const { return m_Server.idWorld == worldId; }

WOlympiadDefaultSetting::WOlympiadDefaultSetting() :
    m_GiveSkills{},
    m_ResetSkillInfo{},
    m_BonusItemData{},
    m_actionCount(0),
    m_buf{},
    m_bufLen(0),
    m_StartTime(1200),
    m_EndTime(1440),
    m_Denominator{3, 3, 3},
    m_Weight{100, 100, 100},
    m_MatchLimit{25, 25, 25, 70},
    SELECTION_MIN_COUNT{0, 20, 10} {
    L2_GUARD_FUNCTION();
}

WOlympiadDefaultSetting::~WOlympiadDefaultSetting() = default;

void WOlympiadDefaultSetting::AddForbiddenWeapon(const int itemClassId) { m_ForbiddenWeapons.insert(itemClassId); }

void WOlympiadDefaultSetting::AddForbiddenItem(const int itemClassId) { m_ForbiddenItems.insert(itemClassId); }

void WOlympiadDefaultSetting::AddGiveSkill(
    const ClassType classType, const GameRuleType gameRule, const std::int64_t skillKey
) {
    const int heroClass = OlympiadDefine::GetHeroClassIndex(classType);
    const std::uint64_t bits = static_cast<std::uint64_t>(skillKey);
    m_GiveSkills[heroClass * 3 + static_cast<int>(gameRule)].push_back(CSkillKey(
        static_cast<int>(bits >> 32), static_cast<int>(static_cast<std::uint16_t>(bits >> 16)),
        static_cast<int>(static_cast<std::uint16_t>(bits))
    ));
}

void WOlympiadDefaultSetting::AddNotResetSkill(const std::vector<std::int64_t>* const skillKeys) {
    // Current L2Server 0x00915a20 appends only the high dword of every
    // packed key and sorts the complete accumulated list afterwards.
    L2_GUARD_FUNCTION();
    for (const std::int64_t packedSkill : *skillKeys) {
        m_ResetSkillInfo.notResetSkillIdList.push_back(static_cast<int>(static_cast<std::uint64_t>(packedSkill) >> 32));
    }
    std::sort(m_ResetSkillInfo.notResetSkillIdList.begin(), m_ResetSkillInfo.notResetSkillIdList.end());
}

void WOlympiadDefaultSetting::SetAdditionalRewardCount(const int winner, const int loser) {
    m_AdditionalRewardCount = {winner, loser};
}

void WOlympiadDefaultSetting::SetBonusItemData(
    const GameRuleType gameRule,
    const int winnerItemClassId,
    const int winnerItemCount,
    const int loserItemClassId,
    const int loserItemCount
) {
    BonusItemData& data = m_BonusItemData[static_cast<int>(gameRule)];
    // Preserve the target's store order.
    data.itemClassIdForLoser = loserItemClassId;
    data.itemCountForLoser = loserItemCount;
    data.itemClassId = winnerItemClassId;
    data.itemCount = winnerItemCount;
}

void WOlympiadDefaultSetting::SetDaysSetting(const int dayOfWeek) {
    m_vMatchDayList.push_back(static_cast<L2DateTime::DayOfWeek>(dayOfWeek));
}

void WOlympiadDefaultSetting::SetDenominator(const GameRuleType gameRule, const int denominator) {
    m_Denominator[static_cast<int>(gameRule)] = denominator;
}

void WOlympiadDefaultSetting::SetMatchLimit(const GameRuleType gameRule, const int limit) {
    m_MatchLimit[static_cast<int>(gameRule)] = limit;
}

void WOlympiadDefaultSetting::SetResetSkillLimit(const int reuseDelayLimit) {
    m_ResetSkillInfo.reuseDelayLimit = reuseDelayLimit;
}

void WOlympiadDefaultSetting::SetRewardCount(const std::vector<int>* const rewardCounts) { m_vRewardCounts = *rewardCounts; }

void WOlympiadDefaultSetting::SetWeight(const GameRuleType gameRule, const int weight) {
    m_Weight[static_cast<int>(gameRule)] = weight;
}

bool WOlympiadDefaultSetting::IsAllowedLevel(const int level) const { return level > 84; }

bool WOlympiadDefaultSetting::IsAllowedItem(int itemClassId) { return m_ForbiddenItems.count(itemClassId) == 0U; }

WOlympiadDefaultSetting::BonusItemData* WOlympiadDefaultSetting::GetBonusItemData(const GameRuleType gameRule) {
    // Current L2Server 0x00910444.
    return &m_BonusItemData[static_cast<int>(gameRule)];
}

int WOlympiadDefaultSetting::GetPenaltyPoint(const GameRuleType, const int point) const {
    // Current L2Server 0x00910458.
    L2_GUARD_FUNCTION();
    const int penalty = static_cast<int>(std::ceil(point * 0.05));
    return point - penalty > 9 ? penalty : 0;
}

int WOlympiadDefaultSetting::GetTransferingPoint(
    const GameRuleType gameRule, const int winnerPoint, const int loserPoint
) const {
    // Current L2Server 0x0091048c. Integer division precedes the two
    // floating-point divisions in the shipping body.
    L2_GUARD_FUNCTION();
    static constexpr int kMaxPlayerCount[3]{3, 1, 1};
    const int index = static_cast<int>(gameRule);
    double denominator = static_cast<double>(m_Denominator[index]);
    if (denominator == 0.0) {
        denominator = 3.0;
    }
    const double winner =
        std::ceil(static_cast<double>((m_Weight[index] * winnerPoint) / 100 / kMaxPlayerCount[index]) / denominator);
    const double loser =
        std::ceil(static_cast<double>((m_Weight[index] * loserPoint) / 100 / kMaxPlayerCount[index]) / denominator);
    return std::min(10, std::min(static_cast<int>(winner), static_cast<int>(loser)));
}

int WOlympiadDefaultSetting::GetRewardCount(const int index) const {
    // Current L2Server 0x00911ce8 uses a one-based public index.
    L2_GUARD_FUNCTION();
    const int offset = index - 1;
    if (offset < 0 || static_cast<std::size_t>(offset) >= m_vRewardCounts.size()) {
        return 0;
    }
    return runtime::CheckedVectorAt(m_vRewardCounts, static_cast<std::size_t>(offset));
}

int WOlympiadDefaultSetting::GetAdditionalRewardCount(const bool winner) const {
    // Current L2Server 0x0091068c.
    L2_GUARD_FUNCTION();
    return winner ? m_AdditionalRewardCount.m_PrimaryKey : m_AdditionalRewardCount.m_SecondaryKey;
}

void WOlympiadDefaultSetting::GiveSkillToPlayer(User* const user, const GameRuleType gameRule) {
    // Current L2Server 0x00913a50. Each hero class owns three contiguous
    // rule vectors in the 108-element publication table.
    L2_GUARD_FUNCTION();
    const int heroClass = OlympiadDefine::GetHeroClassIndex(user->m_data->m_nClass);
    if (heroClass < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]index out of bound, class: %d, %d",
        //     L"OlympiadStuff.cpp",
        //     0x155,
        //     -1,
        //     user->m_data->m_nClass)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]index out of bound, class: %d, %d",
        //     L"OlympiadStuff.cpp",
        //     0x155,
        //     -1,
        //     user->m_data->m_nClass)
        L2_LOG_ERROR(Log, "Cannot give Olympiad skills: class %d has no hero class index.", user->m_data->m_nClass);
        return;
    }
    int activatedCount = 0;
    const auto& skills = m_GiveSkills[heroClass * 3 + static_cast<int>(gameRule)];
    for (const CSkillKey& key : skills) {
        const CSkillInfo* const skill = g_SkillDB.GetSkillInfo(key, user);
        if (skill == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]no skill info: %d, %d",
            //     L"OlympiadStuff.cpp",
            //     0x16c,
            //     key.m_nSkillId,
            //     static_cast<int>(key.m_nLevel))
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]no skill info: %d, %d",
            //     L"OlympiadStuff.cpp",
            //     0x16c,
            //     key.m_nSkillId,
            //     static_cast<int>(key.m_nLevel))
            L2_LOG_ERROR(
                Log, "Cannot give Olympiad skill %d at level %d: skill information is missing.", key.m_nSkillId,
                static_cast<int>(key.m_nLevel)
            );
        } else {
            skill->ActivateSkill(user, user, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
            if (activatedCount++ > 200) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[WORLD_OLYMPIAD][%s][%d]out of loop > 200",
                //     L"OlympiadStuff.cpp",
                //     0x174)
                // L2_LOG_ERROR(Log,
                //     L"[WORLD_OLYMPIAD][%s][%d]out of loop > 200",
                //     L"OlympiadStuff.cpp",
                //     0x174)
                L2_LOG_ERROR(
                    Log, "Stopped giving Olympiad skills after %d activation calls: loop limit exceeded.", activatedCount
                );
                break;
            }
        }
    }
}

int WOlympiadDefaultSetting::GetRewardItem() const { return 0xb210; }

int WOlympiadDefaultSetting::GetStartTime() const {
    // Current L2Server 0x00910558.
    return m_StartTime;
}

int WOlympiadDefaultSetting::GetEndTime() const {
    // Current L2Server 0x00910560.
    return m_EndTime;
}

bool WOlympiadDefaultSetting::IsOlympiadStarted(
    const L2DateTime::DayOfWeek dayOfWeek, const int hour, const int minute
) const {
    // Current L2Server 0x00913968.
    L2_GUARD_FUNCTION();
    if (std::find(m_vMatchDayList.begin(), m_vMatchDayList.end(), dayOfWeek) == m_vMatchDayList.end()) {
        return false;
    }
    return L2DateTimeUtil::IsBetweenInMinute(hour * 60 + minute, m_StartTime, m_EndTime);
}

int WOlympiadDefaultSetting::GetMatchLimit(const GameRuleType gameRule) const {
    // Current L2Server 0x00910584. The caller owns rule-range validation,
    // exactly as in the target's single indexed load.
    return m_MatchLimit[static_cast<int>(gameRule)];
}

int WOlympiadDefaultSetting::GetMatchMinCount(const GameRuleType gameRule) const {
    // Current L2Server 0x00910590. As with GetMatchLimit, the caller owns
    // validation of the recovered enum index.
    return SELECTION_MIN_COUNT[static_cast<int>(gameRule)];
}

int WOlympiadDefaultSetting::GetOlympiadGrade(const int rank, const int playerCount) const {
    // Current L2Server 0x009105b0.
    L2_GUARD_FUNCTION();
    if (rank < 1) {
        return 0;
    }
    static constexpr int kDivisors[4]{100, 10, 4, 0};
    for (int index = 0; index < 4; ++index) {
        if (kDivisors[index] == 0) {
            continue;
        }
        const int threshold = std::max(1, playerCount / kDivisors[index]);
        if (rank <= threshold) {
            return index + 1;
        }
    }
    return 5;
}

void WOlympiadDefaultSetting::SetTimeSetting(const int hour, const int minute) {
    // Current L2Server 0x00910538.
    m_StartTime = hour * 60 + minute;
}

void WOlympiadDefaultSetting::SetEndTime(const int hour, const int minute) {
    // Current L2Server 0x00910548.
    m_EndTime = hour * 60 + minute;
}

void WOlympiadDefaultSetting::SetMatchMinCount(const GameRuleType gameRule, const int count) {
    // Current L2Server 0x0091059c.
    SELECTION_MIN_COUNT[static_cast<int>(gameRule)] = count;
}

void WOlympiadDefaultSetting::ResetMatchDay(const std::vector<L2DateTime::DayOfWeek>& days) {
    // Current L2Server 0x00915974.
    L2_GUARD_FUNCTION();
    m_vMatchDayList = days;
}

bool WOlympiadDefaultSetting::IsMatchDayListEmpty() const {
    // Current L2Server 0x00911cbc.
    return m_vMatchDayList.empty();
}

void WOlympiadDefaultSetting::InitMatchDay() {
    // Current L2Server 0x00916074. The current body clears even though its
    // only startup caller first verifies that the vector is empty.
    m_vMatchDayList.clear();
    if (!g_bTestEventServer) {
        m_vMatchDayList.push_back(L2DateTime::FRI);
        m_vMatchDayList.push_back(L2DateTime::SAT);
    } else {
        m_vMatchDayList.push_back(L2DateTime::THU);
        m_vMatchDayList.push_back(L2DateTime::FRI);
    }
}

bool WOlympiadDefaultSetting::IsValidAction(const int actionId) const {
    L2_GUARD_FUNCTION();
    static constexpr std::array<int, 13> kInvalidActions{3, 7, 8, 9, 10, 11, 28, 37, 50, 51, 56, 60, 61};
    return std::find(kInvalidActions.begin(), kInvalidActions.end(), actionId) == kInvalidActions.end();
}

void WOlympiadDefaultSetting::AddAction(const int actionId) {
    L2_GUARD_FUNCTION();
    if (!IsValidAction(actionId)) {
        return;
    }

    ++m_actionCount;
    const int assembled =
        ::Assemble(reinterpret_cast<char*>(m_buf) + m_bufLen, static_cast<int>(sizeof(m_buf) - m_bufLen), "d", actionId);
    if (assembled > 0) {
        m_bufLen += static_cast<std::uint32_t>(assembled);
    }
}

void WOlympiadDefaultSetting::SendActionList(User* const user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->GetSocket() == nullptr) {
        return;
    }
    user->GetSocket()->Send("chdb", 0xfe, 0x60, m_actionCount, static_cast<int>(m_bufLen), m_buf);
}

void WOlympiadDefaultSetting::ResetSkillReuseDelay(User* const user) {
    // Current L2Server 0x00915040.
    L2_GUARD_FUNCTION();
    // Recovered source added a defensive boundary absent at 0x00915040:
    // if (user == nullptr) { return. }

    user->OlympiadResetSkillReuseDelay(m_ResetSkillInfo.reuseDelayLimit, m_ResetSkillInfo.notResetSkillIdList);
    user->SendCoolTime();

    {
        FOR_ALL_SUMMON_IN_SUMMON_LIST(
            user, summon,
            summon->OlympiadResetSkillReuseDelay(m_ResetSkillInfo.reuseDelayLimit, m_ResetSkillInfo.notResetSkillIdList);
        );
    }
}

namespace {

constexpr bool kOlympiadStateTransitions[WOlympiadUserStateMachine::OLYMPIAD_MAX][WOlympiadUserStateMachine::OLYMPIAD_MAX] =
    {
        {true, false, false, false, true, false, true},
        {true, false, false, false, false, false, false},
        {true, false, false, false, false, false, false},
        {true, false, false, false, false, false, true},
        {true, false, false, false, false, true, false},
        {true, true, true, false, false, false, false},
        {true, false, false, true, false, false, false},
};

} // namespace

bool WOlympiadUser::IsAlive() { return m_IsAlive; }

bool WOlympiadUser::IsEnemyTo(WOlympiadUser* target) {
    // Current L2Server.exe 0x00912384.
    L2_GUARD_FUNCTION();

    if (target == nullptr || target->m_FieldId != m_FieldId) {
        return false;
    }
    const WOlympiadUserStateMachine::UserState ownStatus = m_StatusMachine.GetStatus();
    const WOlympiadUserStateMachine::UserState targetStatus = target->m_StatusMachine.GetStatus();
    const bool ownPlayer =
        ownStatus == WOlympiadUserStateMachine::OLYMPIAD_PLAYER1 || ownStatus == WOlympiadUserStateMachine::OLYMPIAD_PLAYER2;
    const bool targetPlayer = targetStatus == WOlympiadUserStateMachine::OLYMPIAD_PLAYER1 ||
        targetStatus == WOlympiadUserStateMachine::OLYMPIAD_PLAYER2;
    if (!ownPlayer || !targetPlayer || ownStatus == targetStatus) {
        return false;
    }
    WOlympiadMatch* const field = GetField();
    return field->CanAttack();
}

FVector* WOlympiadUser::GetReturnPos() { return &m_ReturnPos; }

int WOlympiadUser::GetTeamId() {
    L2_GUARD_FUNCTION();
    return 0;
}

void WOlympiadUser::SetEnterInZone(CInstantZone* instantZone, int startPositionIndex) {
    L2_GUARD_FUNCTION();
    m_pInstantZone = instantZone;
    m_nStartPosIndex = startPositionIndex;
}

bool WOlympiadUser::CanRegister() {
    L2_GUARD_FUNCTION();
    return m_StatusMachine.CanChangeState(WOlympiadUserStateMachine::OLYMPIAD_ENTRY_REGISTER);
}

bool WOlympiadUser::RegisterOlympiadEntry() {
    L2_GUARD_FUNCTION();
    if (!m_StatusMachine.ChangeState(WOlympiadUserStateMachine::OLYMPIAD_ENTRY_REGISTER)) {
        return false;
    }

    m_Self->m_data->m_PVPMatchType = PVPMatchType::OLYMPIAD;
    m_Self->m_data->m_PVPMatchPlayerState = PVPMatchPlayerState::WAITING;
    return true;
}

bool WOlympiadUser::WaitingOlympiadPlaying() {
    // Current L2Server 0x00910dd8.
    L2_GUARD_FUNCTION();
    const WOlympiadUserStateMachine::UserState oldStatus = m_StatusMachine.GetStatus();
    if (!m_StatusMachine.ChangeState(WOlympiadUserStateMachine::OLYMPIAD_PLAY_WAITING)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]To change status is failed: %s, %d, %d, %d",
        //     L"OlympiadStuff.cpp",
        //     0x4ca,
        //     m_Self->GetName(),
        //     static_cast<int>(oldStatus),
        //     static_cast<int>(m_Self->m_data->m_PVPMatchType),
        //     static_cast<int>(m_Self->m_data->m_PVPMatchPlayerState))
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]To change status is failed: %s, %d, %d, %d",
        //     L"OlympiadStuff.cpp",
        //     0x4ca,
        //     m_Self->GetName(),
        //     static_cast<int>(oldStatus),
        //     static_cast<int>(m_Self->m_data->m_PVPMatchType),
        //     static_cast<int>(m_Self->m_data->m_PVPMatchPlayerState))
        // L2_LOG_ERROR(Log,
        //     "Cannot move Olympiad player %s into play waiting: previous state %d, PvP match type %d, player state %d.",
        //     WideToUtf8(m_Self->GetName()).c_str(), static_cast<int>(oldStatus),
        //     static_cast<int>(m_Self->m_data->m_PVPMatchType), static_cast<int>(m_Self->m_data->m_PVPMatchPlayerState))
        L2_LOG_ERROR(
            Log, "Cannot move Olympiad player %s into play waiting: previous state %d, PvP match type %d, player state %d.",
            Utf16ToUtf8(m_Self->GetName()).c_str(), static_cast<int>(oldStatus),
            static_cast<int>(m_Self->m_data->m_PVPMatchType), static_cast<int>(m_Self->m_data->m_PVPMatchPlayerState)
        );
        return false;
    }
    m_Self->m_data->m_PVPMatchType = PVPMatchType::OLYMPIAD;
    m_Self->m_data->m_PVPMatchPlayerState = PVPMatchPlayerState::WAITING;
    return true;
}

void WOlympiadUser::ReadyForMatch(CInstantZone* const instantZone, const int startPositionIndex) {
    // Current L2Server 0x00913c6c.
    L2_GUARD_FUNCTION();
    if (instantZone == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadStuff.cpp",
        //     0x430,
        //     L"WO_ASSERT_NOT_NULL")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadStuff.cpp",
        //     0x430,
        //     L"WO_ASSERT_NOT_NULL")
        L2_LOG_ERROR(Log, "Cannot prepare Olympiad match: instant zone is missing.");
        return;
    }

    if (m_ReturnPos.IsZero()) {
        const FVector currentPosition = m_Self->GetPos();
        FVector returnPosition = Singleton<CSetting>::inst().GetRestartPoint(
            m_Self->m_data->m_Race, currentPosition, m_Self->m_data->m_nSubJobId < 0, m_Self->m_data->m_nResidenceID
        );
        if (returnPosition.IsZero()) {
            returnPosition = m_Self->GetPos();
        }
        SetReturnPos(returnPosition, false);
    }

    m_Self->TryDismounting(true, false);
    SetEnterInZone(instantZone, startPositionIndex);
    m_Self->DespawnPet(true, true, DESPAWN_PET_OLYMPIAD_PLAYER, false);
    if (m_Self->m_bDominionRenamed) {
        m_Self->RenameByDominionSiege(false, true);
    }
    if (m_Self->IsPC()) {
        m_Self->ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
        m_Self->PushSerializedMessage(MakeLambdaMessage([](CCreature* const creature) {
            creature->ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
        }));
    }
}

bool WOlympiadUser::SetPlayer(
    const std::uint8_t team,
    const int fieldId,
    CInstantZone* const instantZone,
    const GameRuleType gameRule,
    const int startPositionIndex
) {
    // Current L2Server 0x00913e88.
    L2_GUARD_FUNCTION();
    if (static_cast<std::uint32_t>(fieldId) >= 850U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadStuff.cpp",
        //     0x46e,
        //     L"WO_ASSERT_BETWEEN_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadStuff.cpp",
        //     0x46e,
        //     L"WO_ASSERT_BETWEEN_RETURN")
        L2_LOG_ERROR(Log, "Cannot assign Olympiad player: field ID %d is outside 0..849.", fieldId);
        return false;
    }
    if (instantZone == nullptr || instantZone->IsDeletingObject() || instantZone->m_Deleted != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]No InZone: %d",
        //     L"OlympiadStuff.cpp",
        //     0x474,
        //     fieldId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]No InZone: %d",
        //     L"OlympiadStuff.cpp",
        //     0x474,
        //     fieldId)
        L2_LOG_ERROR(Log, "Cannot assign Olympiad player to field %d: instant zone is missing or being deleted.", fieldId);
        return false;
    }

    const auto state = static_cast<WOlympiadUserStateMachine::UserState>(static_cast<int>(team != 1U) + 1);
    if (!m_StatusMachine.ChangeState(state)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]ChageState failed: %d, %d",
        //     L"OlympiadStuff.cpp",
        //     0x47f,
        //     static_cast<int>(m_StatusMachine.GetStatus()),
        //     static_cast<int>(state))
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]ChageState failed: %d, %d",
        //     L"OlympiadStuff.cpp",
        //     0x47f,
        //     static_cast<int>(m_StatusMachine.GetStatus()),
        //     static_cast<int>(state))
        L2_LOG_ERROR(
            Log, "Cannot assign Olympiad player to field %d: transition from state %d to %d failed.", fieldId,
            static_cast<int>(m_StatusMachine.GetStatus()), static_cast<int>(state)
        );
        return false;
    }

    if (gameRule != GRT_TEAM) {
        LeaveParty();
    }
    m_Self->m_data->m_PVPMatchType = PVPMatchType::OLYMPIAD;
    m_Self->m_data->m_PVPMatchPlayerState = PVPMatchPlayerState::COMBAT;
    m_GameRule = gameRule;
    m_FieldId = fieldId;
    m_Self->GetSocket()->Send("chc", 0xfe, 0x7d, static_cast<int>(state));
    ReadyForMatch(instantZone, startPositionIndex);
    return true;
}

bool WOlympiadUser::WaitingOlympiadObserving() {
    L2_GUARD_FUNCTION();
    return m_StatusMachine.ChangeState(WOlympiadUserStateMachine::OLYMPIAD_OB_WAITING);
}

void WOlympiadUser::LeaveParty() {
    // Current L2Server 0x00910ad0.
    L2_GUARD_FUNCTION();
    CParty* const party = m_Self->GetParty();
    if (party == nullptr) {
        return;
    }
    if (m_Self->IsPartyMaster()) {
        if (party->GetMemberCount() < 3) {
            party->Dismiss(false, false);
            return;
        }
        User* const candidate = party->GetMasterCandidate();
        if (candidate == nullptr) {
            return;
        }
        party->HandOverMaster(candidate);
    } else if (party->GetMemberCount() < 3) {
        party->Dismiss(false, false);
        return;
    }
    party->Withdraw(m_Self, false);
}

bool WOlympiadUser::SetObserver(const int fieldId, CInstantZone* const instantZone) {
    // Current L2Server 0x00911e5c.
    L2_GUARD_FUNCTION();
    if (static_cast<std::uint32_t>(fieldId) >= 850U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Wrong field id: %d",
        //     L"OlympiadStuff.cpp",
        //     0x3d3,
        //     fieldId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Wrong field id: %d",
        //     L"OlympiadStuff.cpp",
        //     0x3d3,
        //     fieldId)
        L2_LOG_ERROR(Log, "Cannot assign Olympiad observer: field ID %d is outside 0..849.", fieldId);
        return false;
    }
    if (instantZone == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]No Olympiad Inzone: %d",
        //     L"OlympiadStuff.cpp",
        //     0x3d9,
        //     fieldId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]No Olympiad Inzone: %d",
        //     L"OlympiadStuff.cpp",
        //     0x3d9,
        //     fieldId)
        L2_LOG_ERROR(Log, "Cannot assign Olympiad observer to field %d: instant zone is missing.", fieldId);
        return false;
    }
    if (!m_StatusMachine.ChangeState(WOlympiadUserStateMachine::OLYMPIAD_OBSERVER)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]failed to ChangeState: %d",
        //     L"OlympiadStuff.cpp",
        //     0x3df,
        //     static_cast<int>(m_StatusMachine.GetStatus()))
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]failed to ChangeState: %d",
        //     L"OlympiadStuff.cpp",
        //     0x3df,
        //     static_cast<int>(m_StatusMachine.GetStatus()))
        L2_LOG_ERROR(
            Log, "Cannot assign Olympiad observer to field %d: transition from state %d to observer failed.", fieldId,
            static_cast<int>(m_StatusMachine.GetStatus())
        );
        return false;
    }

    m_IsObserver = true;
    LeaveParty();
    m_FieldId = fieldId;
    FVector position = m_Self->GetPos();
    if (SetReturnPos(position, false)) {
        db.SendSaveCharacterInfo(m_Self, false, true);
    }
    m_Self->DespawnAllSummon(true, false);
    m_Self->TryDismounting(true, false);
    SetEnterInZone(instantZone, 6);
    m_Self->DespawnPet(true, true, DESPAWN_PET_OLYMPIAD_OBSERVER, false);
    return true;
}

void WOlympiadUser::ObserverHide(const bool hide) {
    // Current L2Server 0x009140ac.
    L2_GUARD_FUNCTION();
    m_Self->m_bCameraMode = hide;
    m_Self->ValidateOrgSpeed();

    if (hide) {
        if (!m_Self->m_data->m_bGMInvisible) {
            L2_GUARD(*m_Self->m_lock);
            m_Self->m_data->m_bGMInvisible = true;
        }
        m_Self->DoNothing();
        const bool entered = g_InstantZoneMgr.EnterInstantZone(
            m_Self, m_pInstantZone, IZET_INDIVIDUAL, m_nStartPosIndex, false, 0, TeleportSourceType::InstantAgit
        );
        CSPointer<User> user(m_Self, ObjectRefType::FunctionChain);
        if (!entered) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]Observing OlympiadZone Failed: %s, %d",
            //     L"OlympiadStuff.cpp",
            //     0x5d7,
            //     m_Self->GetName(),
            //     m_Self->m_nInstantZoneTypeID)
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]Observing OlympiadZone Failed: %s, %d",
            //     L"OlympiadStuff.cpp",
            //     0x5d7,
            //     m_Self->GetName(),
            //     m_Self->m_nInstantZoneTypeID)
            // L2_LOG_ERROR(Log,
            //     "Olympiad observer %s could not enter field %d. Current zone type %d.",
            //     WideToUtf8(m_Self->GetName()).c_str(), m_FieldId, m_Self->m_nInstantZoneTypeID)
            L2_LOG_ERROR(
                Log, "Olympiad observer %s could not enter field %d; current zone type %d.",
                Utf16ToUtf8(m_Self->GetName()).c_str(), m_FieldId, m_Self->m_nInstantZoneTypeID
            );
            return;
        }

        WOlympiadStuff::Write_LOG_WOLYMPIAD_ENTER_INZONE_InHost(
            user, m_pInstantZone->m_nTypeId, static_cast<int>(m_pInstantZone->m_nId), m_FieldId, LOET_OBSERVER, 0, 0
        );
        if (CUserSocket* const socket = m_Self->GetSocket()) {
            socket->Send("chc", 0xfe, 0x7d, 3);
        }
        if (WOlympiadMatch* const field = GetField()) {
            field->RequestPlayersInfo(m_Self->GetServerId());
        }
        return;
    }

    if (m_Self->m_data->m_bGMInvisible && !m_Self->IsBuilder()) {
        L2_GUARD(*m_Self->m_lock);
        m_Self->m_data->m_bGMInvisible = false;
    }
    m_Self->DoNothing();
    if (!m_Self->IsInRealWorld()) {
        CInstantZone* const zone = m_Self->GetInstantZone();
        if (zone == nullptr) {
            m_Self->TeleportToLocation(
                static_cast<int>(m_ReturnPos.x), static_cast<int>(m_ReturnPos.y), static_cast<int>(m_ReturnPos.z), 0, 0,
                TeleportSourceType::GoPoint, nullptr, 0
            );
        } else {
            zone->LeaveZone(m_Self, false, 0, 0, 0, 0);
            WOlympiadStuff::Write_LOG_WOLYMPIAD_EXIT_INZONE_InHost(
                CSPointer<User>(m_Self, ObjectRefType::FunctionChain), zone->m_nTypeId, static_cast<int>(zone->m_nId),
                m_FieldId, LOET_OBSERVER, 0
            );
        }
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Olympiad observer is not in inzone: %d, %d",
        //     L"OlympiadStuff.cpp",
        //     0x5ee,
        //     static_cast<int>(m_Self->GetDBID()),
        //     m_Self->m_data->m_nWorldId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Olympiad observer is not in inzone: %d, %d",
        //     L"OlympiadStuff.cpp",
        //     0x5ee,
        //     static_cast<int>(m_Self->GetDBID()),
        //     m_Self->m_data->m_nWorldId)
        L2_LOG_WARN(
            Log, "Olympiad observer DB ID %u in world %d is already in the real world during observer exit.",
            static_cast<unsigned>(m_Self->GetDBID()), m_Self->m_data->m_nWorldId
        );
    }
    if (CUserSocket* const socket = m_Self->GetSocket()) {
        socket->Send("chc", 0xfe, 0x7d, 0);
    }
    runtime::raw_atomic::CompareExchangeI32(&m_Self->m_nRelationChanged, 1, 0);
}

void WOlympiadUser::OnDespawnPetByObserver() {
    // Current L2Server 0x009147f0.
    L2_GUARD_FUNCTION();
    ObserverHide(true);
}

void WOlympiadUser::OnDespawnPetInPeer() {
    // Current L2Server 0x00915b28.
    L2_GUARD_FUNCTION();
    m_Self->LeaveWorldForRaid();
    m_Self->m_RaidInfo.m_OriginPos = m_Self->GetPos();

    CUserSocket* const socket = m_Self->GetSocket();
    socket->AddRaidStatus(URS_ACCEPTED_REQUEST);
    new OlympiadConnectVerifier(socket);

    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR]"
    //     L"[WORLD_OLYMPIAD][PACKET]"
    //     L"S_EX_RAID_RESERVE_RESULT sent: %d, 1",
    //     L"OlympiadStuff.cpp",
    //     0x3a4,
    //     m_Self->GetDBID())
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR]"
    //     L"[WORLD_OLYMPIAD][PACKET]"
    //     L"S_EX_RAID_RESERVE_RESULT sent: %d, 1",
    //     L"OlympiadStuff.cpp",
    //     0x3a4,
    //     m_Self->GetDBID())
    L2_LOG_INFO(
        Log, "Sending accepted Olympiad transfer reservation to character DB ID %u.",
        static_cast<unsigned>(m_Self->GetDBID())
    );

    socket->Send(
        "chddddd", 0xfe, 0xb7, 1, WOlympiadServiceConfig::inst().m_Server.nExternalAddress, g_nWorldId, m_Self->GetDBID(),
        m_Self->GetAccountID()
    );
    db.RequestReloadUserFlagOn(*m_Self);
}

void WOlympiadUser::UnequipForbiddenWeapon() {
    // Current L2Server 0x00916114.
    L2_GUARD_FUNCTION();

    std::array<CItem*, 64> forbiddenItems{};
    std::size_t forbiddenItemCount = 0;
    {
        L2_READ_GUARD(m_Self->m_inv);
        for (const std::int32_t itemIndex : m_Self->m_data->m_nSlotIndex) {
            CItem* const item = m_Self->m_inv.GetByIndex(itemIndex);
            if (item != nullptr && !WOlympiadDefaultSetting::inst().IsAllowedItem(item->m_nClassId)) {
                forbiddenItems[forbiddenItemCount++] = item;
            }
        }
    }

    for (std::size_t index = 0; index < forbiddenItemCount; ++index) {
        m_Self->UnequipItem(forbiddenItems[index], false);
    }
}

void WOlympiadUser::OnDespawnPetByPlayer() {
    // Current L2Server 0x0091674c.
    L2_GUARD_FUNCTION();

    m_Self->CancelTransform(0, false, SPET_DISPEL);
    m_Self->DispelByBuffProtectionFlag(2);
    {
        L2_GUARD(*m_Self->m_lock);
        m_Self->m_data->SetCP(m_Self->m_data->m_dBaseCP);
        m_Self->m_data->SetHP(m_Self->m_data->m_dBaseHP);
        m_Self->m_data->SetMP(m_Self->m_data->m_dBaseMP);
        m_Self->m_data->UltimateSkillGauge(500);
    }
    db.SendSaveCharacterInfo(m_Self, false, true);

    if (!g_bUseAutoSoulShotUI) {
        m_Self->GetSocket()->Send("chddd", 0xfe, 0x0c, 0, 0, -1);
    }

    UnequipForbiddenWeapon();
    m_Self->OnRuneItemsChange();

    {
        FOR_ALL_SUMMON_IN_SUMMON_LIST(
            m_Self, summon,
            if (static_cast<std::int32_t>(summon->m_data->m_Race) == 0x18) {
                m_Self->DespawnSummon(summon, true, false);
                continue;
            }

            summon->DispelByBuffProtectionFlag(2);
            L2_GUARD(*summon->m_lock); summon->m_data->SetHP(summon->m_data->m_dBaseHP);
            summon->m_data->SetMP(summon->m_data->m_dBaseMP);
        );
    }

    m_Self->EraseCubicNotSelfCast();
    WOlympiadDefaultSetting::inst().ResetSkillReuseDelay(m_Self);
    m_Damage = 0;
    m_Self->DoNothing();
    m_Self->m_nTimeActivatePledgeSkill = 0;
    WOlympiadDefaultSetting::inst().SendActionList(m_Self);

    const bool entered = g_InstantZoneMgr.EnterInstantZone(
        m_Self, m_pInstantZone, IZET_INDIVIDUAL, m_nStartPosIndex, false, 0, TeleportSourceType::InstantAgit
    );
    if (!entered) {
        return;
    }

    const std::CDualKey<int, int> key(m_Self->m_data->m_nWorldId, static_cast<int>(m_Self->GetDBID()));
    const SharedNoblessInfo* const info = WOlympiadUsers::inst().GetNoblessInfo(key);
    const int matchCount = info == nullptr ? 0 : info->nMatchCount;
    const int season = WOlympiadService::inst().GetSeason();
    const WOlympiadCondition::Value step = WOlympiadService::inst().GetStep();
    const int matchCountByStep = WOlympiadUsers::inst().GetMatchCountByStep(key, season, static_cast<int>(step), m_GameRule);
    WOlympiadStuff::Write_LOG_WOLYMPIAD_ENTER_INZONE_InHost(
        CSPointer<User>(m_Self, ObjectRefType::FunctionChain), m_pInstantZone->m_nTypeId,
        static_cast<int>(m_pInstantZone->m_nId), m_FieldId, LOET_PLAYER, matchCount, matchCountByStep
    );
    RelayClient::Inst()->SendEnterOlympiad(m_Self);
}

void WOlympiadUser::EscapeFromOlympiad() {
    // Current L2Server 0x00916c74.
    L2_GUARD_FUNCTION();
    const WOlympiadUserStateMachine::UserState status = m_StatusMachine.GetStatus();
    if (status != WOlympiadUserStateMachine::OLYMPIAD_OBSERVER && status != WOlympiadUserStateMachine::OLYMPIAD_PLAYER1 &&
        status != WOlympiadUserStateMachine::OLYMPIAD_PLAYER2) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Status : %d",
        //     L"OlympiadStuff.cpp",
        //     0x545,
        //     static_cast<int>(status))
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Status : %d",
        //     L"OlympiadStuff.cpp",
        //     0x545,
        //     static_cast<int>(status))
        L2_LOG_ERROR(Log, "Cannot leave Olympiad field: state %d is neither player nor observer.", static_cast<int>(status));
        runtime::process::ReportCallStack(2, false);
        return;
    }

    m_Self->DespawnAllSummon(true, false);
    m_Self->TryDismounting(true, false);
    if (status == WOlympiadUserStateMachine::OLYMPIAD_OBSERVER) {
        ObserverHide(false);
        if (!OlympiadStatusReset()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]Status: %d",
            //     L"OlympiadStuff.cpp",
            //     0x556,
            //     static_cast<int>(m_StatusMachine.GetStatus()))
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]Status: %d",
            //     L"OlympiadStuff.cpp",
            //     0x556,
            //     static_cast<int>(m_StatusMachine.GetStatus()))
            L2_LOG_ERROR(
                Log, "Could not reset Olympiad observer state after exit; current state %d.",
                static_cast<int>(m_StatusMachine.GetStatus())
            );
        }
        return;
    }

    if (CUserSocket* const socket = m_Self->GetSocket()) {
        socket->Send("chc", 0xfe, 0x7d, 0);
    }
    {
        L2_GUARD(*m_Self->m_lock);
        m_Self->m_data->UltimateSkillGauge(0);
        m_Self->m_data->SetCP(m_Self->m_data->m_dBaseCP);
        m_Self->m_data->SetHP(m_Self->m_data->m_dBaseHP);
        m_Self->m_data->SetMP(m_Self->m_data->m_dBaseMP);
    }

    {
        FOR_ALL_SUMMON_IN_SUMMON_LIST(
            m_Self, summon, summon->DispelByBuffProtectionFlag(2); L2_GUARD(*summon->m_lock);
            summon->m_data->SetHP(summon->m_data->m_dBaseHP); summon->m_data->SetMP(summon->m_data->m_dBaseMP);
        );
    }

    if (!m_Self->IsInRealWorld()) {
        CInstantZone* const zone = m_Self->GetInstantZone();
        if (zone != nullptr) {
            zone->LeaveZone(m_Self, false, 0, 0, 0, 0);
            const std::CDualKey<int, int> key(m_Self->m_data->m_nWorldId, static_cast<int>(m_Self->GetDBID()));
            const SharedNoblessInfo* const info = WOlympiadUsers::inst().GetNoblessInfo(key);
            WOlympiadStuff::Write_LOG_WOLYMPIAD_EXIT_INZONE_InHost(
                CSPointer<User>(m_Self, ObjectRefType::FunctionChain), zone->m_nTypeId, static_cast<int>(zone->m_nId),
                m_FieldId, LOET_PLAYER, info == nullptr ? 0 : info->nMatchCount
            );
        }
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Olympiad user is not in inzone: %d",
        //     L"OlympiadStuff.cpp",
        //     0x56f,
        //     static_cast<int>(m_StatusMachine.GetStatus()))
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Olympiad user is not in inzone: %d",
        //     L"OlympiadStuff.cpp",
        //     0x56f,
        //     static_cast<int>(m_StatusMachine.GetStatus()))
        L2_LOG_WARN(
            Log, "Olympiad player is already in the real world during field exit; current state %d.",
            static_cast<int>(m_StatusMachine.GetStatus())
        );
    }

    if (!OlympiadStatusReset()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Status: %d",
        //     L"OlympiadStuff.cpp",
        //     0x585,
        //     static_cast<int>(m_StatusMachine.GetStatus()))
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Status: %d",
        //     L"OlympiadStuff.cpp",
        //     0x585,
        //     static_cast<int>(m_StatusMachine.GetStatus()))
        L2_LOG_ERROR(
            Log, "Could not reset Olympiad player state after exit; current state %d.",
            static_cast<int>(m_StatusMachine.GetStatus())
        );
    }
    if (CPledge* const pledge = m_Self->GetPledge()) {
        pledge->AddPledgeSkillsTo(m_Self, true);
    }
    if (static_cast<int>(m_Self->m_data->m_nClass) == 0 && m_Self->IsHaveMentee() && m_Self->m_Mentor.m_nCanGetBuff > 0) {
        m_Self->m_Mentor.m_nInfectedSkillByMentee = 0;
    }
    m_Self->SendBasicActionList();
    m_Self->OnRuneItemsChange();
}

void WOlympiadUser::SetAlive(const bool alive) {
    // Current L2Server 0x0091217c.
    L2_GUARD_FUNCTION();
    if (alive) {
        WOlympiadMatch* const field = GetField();
        if (field == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]invalid field id: %d",
            //     L"OlympiadStuff.cpp",
            //     0x514,
            //     m_FieldId)
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]invalid field id: %d",
            //     L"OlympiadStuff.cpp",
            //     0x514,
            //     m_FieldId)
            L2_LOG_ERROR(
                Log, "Cannot broadcast Olympiad revival: field %d is unavailable; local alive state will still be updated.",
                m_FieldId
            );
        } else {
            field->Broadcast("cd", 1, static_cast<int>(m_Self->m_nId));
        }
    }
    m_IsAlive = alive;
}

void WOlympiadUser::ChangeObserverField(const int fieldId) {
    // Current L2Server 0x0091227c.
    L2_GUARD_FUNCTION();
    if (m_StatusMachine.GetStatus() != WOlympiadUserStateMachine::OLYMPIAD_OBSERVER) {
        return;
    }
    WOlympiadMatch* const field = WOlympiadService::inst().GetField(fieldId);
    if (field != nullptr && fieldId != m_FieldId) {
        field->AddObserver(m_Self->GetServerId());
    }
}

bool WOlympiadUser::SetReturnPos(FVector& position, bool force) {
    L2_GUARD_FUNCTION();
    if (!m_ReturnPos.IsZero() && !force) {
        return false;
    }
    m_ReturnPos = position;
    return true;
}

void WOlympiadUser::AddDamage(int damage) { m_Damage += damage; }

int WOlympiadUser::GetDamage() const {
    // Current L2Server 0x004b841c.
    return m_Damage;
}

void WOlympiadUser::ResetDamage() { m_Damage = 0; }

bool WOlympiadUser::ValidInventory() {
    // Current L2Server 0x009144ec.
    L2_GUARD_FUNCTION();
    return WOlympiadStuff::HasEnoughSpaceInInventory(CSPointer<User>(m_Self, ObjectRefType::FunctionChain));
}

bool WOlympiadUser::OlympiadStatusReset() {
    L2_GUARD_FUNCTION();

    m_ReturnPos.x = 0.0;
    m_ReturnPos.y = 0.0;
    m_ReturnPos.z = 0.0;
    m_GameRule = GRT_MAX;
    m_FieldId = 0;
    m_Damage = 0;
    m_IsAlive = true;
    m_nStartPosIndex = 0;
    m_pInstantZone = nullptr;

    const bool changed = m_StatusMachine.ChangeState(WOlympiadUserStateMachine::OLYMPIAD_NOT_USER);
    if (changed) {
        m_Self->m_data->m_PVPMatchType = PVPMatchType::NONE;
        m_Self->m_data->m_PVPMatchPlayerState = PVPMatchPlayerState::NONE;
    }
    return changed;
}

bool WOlympiadUser::IsPlayer() const {
    const WOlympiadUserStateMachine::UserState status = m_StatusMachine.GetStatus();
    return status == WOlympiadUserStateMachine::OLYMPIAD_PLAYER1 || status == WOlympiadUserStateMachine::OLYMPIAD_PLAYER2;
}

bool WOlympiadUser::IsObserver() const {
    return m_StatusMachine.GetStatus() == WOlympiadUserStateMachine::OLYMPIAD_OBSERVER;
}

bool WOlympiadUser::IsRegistered() const {
    return m_StatusMachine.GetStatus() == WOlympiadUserStateMachine::OLYMPIAD_ENTRY_REGISTER;
}

bool WOlympiadUser::IsPlaying() const {
    const WOlympiadUserStateMachine::UserState status = m_StatusMachine.GetStatus();
    return status == WOlympiadUserStateMachine::OLYMPIAD_PLAYER1 || status == WOlympiadUserStateMachine::OLYMPIAD_PLAYER2 ||
        status == WOlympiadUserStateMachine::OLYMPIAD_PLAY_WAITING;
}

bool WOlympiadUserStateMachine::CanChangeState(UserState state) {
    L2_GUARD_FUNCTION();

    return kOlympiadStateTransitions[m_Status][state];
}

bool WOlympiadUserStateMachine::ChangeState(UserState state) {
    L2_GUARD_FUNCTION();

    const bool canChange = kOlympiadStateTransitions[m_Status][state];
    if (canChange) {
        m_Status = state;
    }
    return canChange;
}

void WOlympiadUser::OnLeaveWorld() {
    // Current L2Server 0x00915d08.
    L2_GUARD_FUNCTION();
    WOlympiadServiceConfig& config = WOlympiadServiceConfig::inst();
    if (g_nWorldId == config.m_Server.idWorld) {
        const CSPointer<WOlympiadMemberInfo> member = WOlympiadService::inst().FindMember(
            std::CDualKey<int, int>(m_Self->m_data->m_nWorldId, static_cast<int>(m_Self->GetDBID()))
        );
        if (member && !member->IsPlayer()) {
            WOlympiadService::inst().UnregisterEntry(
                std::CDualKey<int, int>(m_Self->m_data->m_nWorldId, static_cast<int>(m_Self->GetDBID()))
            );
        }
        return;
    }

    WOlympiadServiceProxy& proxy = WOlympiadServiceProxy::inst();
    if (!proxy.IsMember(static_cast<int>(m_Self->GetDBID()))) {
        return;
    }

    CSPointer<User> user(m_Self, ObjectRefType::FunctionChain);
    if (m_Self->m_RaidInfo.m_nRaidWorldId == 0) {
        proxy.StopMatchMaking(user, true);
    } else {
        proxy.UnregisterMember(user);
    }
}
