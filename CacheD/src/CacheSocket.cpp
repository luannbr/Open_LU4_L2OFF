#include "CacheSocket.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/Runtime/DirectLog.h"

#include "AllianceDb.h"
#include "BattleCampDb.h"
#include "CAccount.h"
#include "CAnnounce.h"
#include "CReadFile.h"
#include "Cached.h"
#include "CastleDb.h"
#include "CursedWeaponDB.h"
#include "DBConn.h"
#include "FriendsDb.h"
#include "HtmlCommandHandler.h"
#include "Item.h"
#include "ItemAssembler.h"
#include "ManorDb.h"
#include "MiniGameAgitDB.h"
#include "NewCharDB.h"
#include "NpcServer.h"
#include "Pet.h"
#include "PledgeBBS.h"
#include "PledgeContributionInCache.h"
#include "PledgeCrestDb.h"
#include "PledgeDb.h"
#include "ServerPacketEx.h"
#include "User.h"
#include "UserDb.h"
#include "UserMacro.h"
#include "WareHouse.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/DelimitedText.h"
#include "Shared/src/Runtime/NativeTcpSocket.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/TargetFormatting.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/DXPacket.h"
#include "Shared/src/GIP.h"
#include "Shared/src/L2Time.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/PledgeRanking.h"
#include "Shared/src/QueryPacket.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/SlotType.h"
#include "Shared/src/UtilsString.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/platform/Runtime.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <cwctype>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

// The current guard signature includes `static`. This handler is an original
// CacheSocket.cpp-local owner instead of public CacheSocket.h API.
static bool RequestManageMemo(CSocket* socket, const std::uint8_t* packet);

namespace {

constexpr int kCurrentCacheRevision = 373;
constexpr int kDefaultServerStopperKey = 0x04dd0e8f;

// Exact current CacheD instantiations. The shared template owns their
// function-local static instances, just as it does in L2NPC.
using ServerToCacheDPacketStat = CPacketStat<695, 234>;
using CacheDToServerPacketStat = CPacketStat<506, 198>;
std::atomic<bool> g_serverStopRequested{false};

std::string AddressText(::in_addr address) {
    std::array<char, INET_ADDRSTRLEN> text{};
    if (::inet_ntop(AF_INET, &address, text.data(), text.size())) {
        return text.data();
    }
    return "0.0.0.0";
}

bool IsConfiguredAdminAddress(const std::string& address) {
    // Current Cached.exe 0x00460364. The original identifier is absent.
    // this semantic name describes the source-local helper used only by
    // anonymous ServerStopper at 0x00460418.
    const auto& addresses = GetCacheDConfiguration().adminAddresses;
    return std::find(addresses.begin(), addresses.end(), address) != addresses.end();
}

void WriteDeleteSubJobAudit(const CUser& user, int deletedSubjobId) {
    // Current RequestDeleteSubJob 0x00479aa4 writes this sparse 26-field
    // route-6 record directly instead of going through CLogFormatter, so
    // the unused numeric positions remain empty instead of becoming zeroes.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x350);
    fields[1] = runtime::DecimalUtf16(user.GetId());
    fields[2] = runtime::DecimalUtf16(user.GetAccountID());
    fields[5] = runtime::DecimalUtf16(user.GetX());
    fields[6] = runtime::DecimalUtf16(user.GetY());
    fields[7] = runtime::DecimalUtf16(user.GetZ());
    fields[11] = runtime::DecimalUtf16(user.GetRace());
    fields[12] = runtime::DecimalUtf16(user.GetGender());
    fields[13] = runtime::DecimalUtf16(user.GetClass());
    fields[14] = runtime::DecimalUtf16(user.GetLevel());
    fields[20] = runtime::DecimalUtf16(deletedSubjobId);
    fields[21] = std::u16string(user.GetCharName());
    fields[22] = std::u16string(user.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteSaveCharacterLevelJumpLog(const CUser& user, int newLevel, int oldLevel, int timeToChange, int subjobId) {
    // Current SaveCharacter emits log 0x58d directly to the audit route,
    // separate from the ordinary 0x32a level-change formatter record.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x58d);
    fields[1] = runtime::DecimalUtf16(user.GetId());
    fields[2] = runtime::DecimalUtf16(user.GetAccountID());
    fields[5] = runtime::DecimalUtf16(user.GetX());
    fields[6] = runtime::DecimalUtf16(user.GetY());
    fields[7] = runtime::DecimalUtf16(user.GetZ());
    fields[11] = runtime::DecimalUtf16(user.GetRace());
    fields[12] = runtime::DecimalUtf16(user.GetGender());
    fields[13] = runtime::DecimalUtf16(user.GetClass());
    fields[14] = runtime::DecimalUtf16(newLevel);
    fields[15] = runtime::DecimalUtf16(oldLevel);
    fields[16] = runtime::DecimalUtf16(timeToChange);
    fields[17] = runtime::DecimalUtf16(user.GetBuilder());
    fields[18] = runtime::DecimalUtf16(subjobId);
    fields[21] = std::u16string(user.GetCharName());
    fields[22] = std::u16string(user.GetAccountName());
    runtime::process::WriteLogFormatterLine(8, runtime::JoinFields(fields, u','));
}

void WriteSaveCharacterClassChangeLog(
    const CUser& user, int newLevel, int oldClass, int timeToChange, int subjobId, int dualClassId
) {
    // Unlike the common CLogFormatter call above, the target writes this
    // 0x330 record through CLog::AddV on the ordinary log route.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x330);
    fields[1] = runtime::DecimalUtf16(user.GetId());
    fields[2] = runtime::DecimalUtf16(user.GetAccountID());
    fields[5] = runtime::DecimalUtf16(user.GetX());
    fields[6] = runtime::DecimalUtf16(user.GetY());
    fields[7] = runtime::DecimalUtf16(user.GetZ());
    fields[11] = runtime::DecimalUtf16(user.GetRace());
    fields[12] = runtime::DecimalUtf16(user.GetGender());
    fields[13] = runtime::DecimalUtf16(user.GetClass());
    fields[14] = runtime::DecimalUtf16(newLevel);
    fields[15] = runtime::DecimalUtf16(oldClass);
    fields[16] = runtime::DecimalUtf16(timeToChange);
    fields[17] = runtime::DecimalUtf16(user.GetBuilder());
    fields[18] = runtime::DecimalUtf16(subjobId);
    fields[19] = runtime::DecimalUtf16(dualClassId);
    fields[21] = std::u16string(user.GetCharName());
    fields[22] = std::u16string(user.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteCreatePetAudit(
    const CUser& user, int collarType, int petNpcClass, int collarItemId, std::int64_t experience, int enchant
) {
    // Current RequestCreatePet 0x0046e814 writes this exact route-6 record
    // through the unnamed vararg adapter at 0x0045e344.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x67);
    fields[1] = runtime::DecimalUtf16(user.GetId());
    fields[2] = runtime::DecimalUtf16(user.GetAccountID());
    fields[5] = runtime::DecimalUtf16(user.GetX());
    fields[6] = runtime::DecimalUtf16(user.GetY());
    fields[7] = runtime::DecimalUtf16(user.GetZ());
    fields[11] = runtime::DecimalUtf16(user.GetRace());
    fields[12] = runtime::DecimalUtf16(user.GetGender());
    fields[13] = runtime::DecimalUtf16(user.GetClass());
    fields[14] = runtime::DecimalUtf16(user.GetLevel());
    fields[15] = runtime::DecimalUtf16(collarType);
    fields[16] = runtime::DecimalUtf16(petNpcClass);
    fields[17] = runtime::DecimalUtf16(collarItemId);
    fields[18] = runtime::DecimalUtf16(experience);
    fields[19] = runtime::DecimalUtf16(enchant);
    fields[21] = std::u16string(user.GetCharName());
    fields[22] = std::u16string(user.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteDeletePetAudits(
    const CUser& user,
    int requestedCollarItemId,
    int actualItemId,
    int actualItemType,
    int actualEnchant,
    int deleteReason,
    int requestCollarEnchant,
    const char16_t* shapeShift,
    const char16_t* baseAttribute,
    const char16_t* variation,
    const char16_t* ensoul
) {
    const std::u16string shapeShiftText = std::u16string(shapeShift);
    const std::u16string baseAttributeText = std::u16string(baseAttribute);
    const std::u16string variationText = std::u16string(variation);
    const std::u16string ensoulText = std::u16string(ensoul);
    const std::u16string characterName = std::u16string(user.GetCharName());
    const std::u16string accountName = std::u16string(user.GetAccountName());

    // First current record: explicit sparse item-deletion audit 0x38b.
    std::array<std::u16string, 26> itemFields{};
    itemFields[0] = runtime::DecimalUtf16(0x38b);
    itemFields[1] = runtime::DecimalUtf16(user.GetId());
    itemFields[2] = runtime::DecimalUtf16(user.GetAccountID());
    itemFields[5] = runtime::DecimalUtf16(user.GetX());
    itemFields[6] = runtime::DecimalUtf16(user.GetY());
    itemFields[7] = runtime::DecimalUtf16(user.GetZ());
    itemFields[8] = shapeShiftText;
    itemFields[9] = baseAttributeText;
    itemFields[10] = variationText;
    itemFields[11] = runtime::DecimalUtf16(user.GetRace());
    itemFields[12] = runtime::DecimalUtf16(user.GetGender());
    itemFields[13] = runtime::DecimalUtf16(user.GetClass());
    itemFields[14] = runtime::DecimalUtf16(user.GetLevel());
    itemFields[17] = runtime::DecimalUtf16(actualEnchant);
    itemFields[18] = runtime::DecimalUtf16(actualItemType);
    itemFields[19] = runtime::DecimalUtf16(-1);
    itemFields[20] = runtime::DecimalUtf16(0);
    itemFields[21] = characterName;
    itemFields[22] = accountName;
    itemFields[25] = runtime::DecimalUtf16(actualItemId);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(itemFields, u','));

    // Second current record is the ordinary formatter path 0x2342. Only
    // ensoul text occupies its string triplet. Numeric 1..4 are actor state.
    CLogFormatter formatter;
    formatter.WriteLog(
        0x2342, user.GetId(), user.GetAccountID(), 0, 0, user.GetX(), user.GetY(), user.GetZ(), ensoulText.c_str(), nullptr,
        nullptr, user.GetRace(), user.GetGender(), user.GetClass(), user.GetLevel(), 0, 0, 0, 0, 0, 0, characterName.c_str(),
        accountName.c_str(), nullptr, nullptr, actualItemId
    );

    // Third current record is the pet-specific deletion reason 0x68.
    std::array<std::u16string, 26> petFields{};
    petFields[0] = runtime::DecimalUtf16(0x68);
    petFields[1] = runtime::DecimalUtf16(user.GetId());
    petFields[2] = runtime::DecimalUtf16(user.GetAccountID());
    petFields[5] = runtime::DecimalUtf16(user.GetX());
    petFields[6] = runtime::DecimalUtf16(user.GetY());
    petFields[7] = runtime::DecimalUtf16(user.GetZ());
    petFields[11] = runtime::DecimalUtf16(user.GetRace());
    petFields[12] = runtime::DecimalUtf16(user.GetGender());
    petFields[13] = runtime::DecimalUtf16(user.GetClass());
    petFields[14] = runtime::DecimalUtf16(user.GetLevel());
    petFields[15] = runtime::DecimalUtf16(requestedCollarItemId);
    petFields[16] = runtime::DecimalUtf16(deleteReason);
    petFields[18] = runtime::DecimalUtf16(requestCollarEnchant);
    petFields[21] = characterName;
    petFields[22] = accountName;
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(petFields, u','));
}

std::u16string PetNameField(const std::array<char16_t, 25>& name) {
    const auto end = std::find(name.begin(), name.end(), u'\0');
    return std::u16string(name.begin(), end);
}

void WriteWithdrawPetAudit(
    const CUser& user,
    const std::u16string& petName,
    int petId,
    int petNpcClass,
    std::int64_t experience,
    int requestedNpcClass,
    int meal,
    int alive
) {
    // Raw current call setup at 0x0047e813..0x0047e8c2 fixes route 6,
    // record 0x69 and all populated positions of this 26-field record.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x69);
    fields[1] = runtime::DecimalUtf16(user.GetId());
    fields[2] = runtime::DecimalUtf16(user.GetAccountID());
    fields[5] = runtime::DecimalUtf16(user.GetX());
    fields[6] = runtime::DecimalUtf16(user.GetY());
    fields[7] = runtime::DecimalUtf16(user.GetZ());
    fields[8] = petName;
    fields[11] = runtime::DecimalUtf16(user.GetRace());
    fields[12] = runtime::DecimalUtf16(user.GetGender());
    fields[13] = runtime::DecimalUtf16(user.GetClass());
    fields[14] = runtime::DecimalUtf16(user.GetLevel());
    fields[15] = runtime::DecimalUtf16(petId);
    fields[16] = runtime::DecimalUtf16(petNpcClass);
    fields[17] = runtime::DecimalUtf16(experience);
    fields[18] = runtime::DecimalUtf16(requestedNpcClass);
    fields[19] = runtime::DecimalUtf16(meal);
    fields[20] = runtime::DecimalUtf16(alive);
    fields[21] = std::u16string(user.GetCharName());
    fields[22] = std::u16string(user.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteChangePetNameAudit(
    const CUser& user, const wchar_t* newName, int petId, int petNpcClass, std::int64_t experience
) {
    // Current RequestChangePetName call 0x00471322 emits record 0x6b.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x6b);
    fields[1] = runtime::DecimalUtf16(user.GetId());
    fields[2] = runtime::DecimalUtf16(user.GetAccountID());
    fields[5] = runtime::DecimalUtf16(user.GetX());
    fields[6] = runtime::DecimalUtf16(user.GetY());
    fields[7] = runtime::DecimalUtf16(user.GetZ());
    fields[8] = WideToUtf16(newName);
    fields[11] = runtime::DecimalUtf16(user.GetRace());
    fields[12] = runtime::DecimalUtf16(user.GetGender());
    fields[13] = runtime::DecimalUtf16(user.GetClass());
    fields[14] = runtime::DecimalUtf16(user.GetLevel());
    fields[15] = runtime::DecimalUtf16(petId);
    fields[16] = runtime::DecimalUtf16(petNpcClass);
    fields[17] = runtime::DecimalUtf16(experience);
    fields[21] = std::u16string(user.GetCharName());
    fields[22] = std::u16string(user.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteDismissPetAudit(const CUser& user, int petId, int petLevel) {
    // Current RequestDismissPet call 0x0049138d emits record 0x6c. The pet
    // level occupies field 18, with fields 16 and 17 empty.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x6c);
    fields[1] = runtime::DecimalUtf16(user.GetId());
    fields[2] = runtime::DecimalUtf16(user.GetAccountID());
    fields[5] = runtime::DecimalUtf16(user.GetX());
    fields[6] = runtime::DecimalUtf16(user.GetY());
    fields[7] = runtime::DecimalUtf16(user.GetZ());
    fields[11] = runtime::DecimalUtf16(user.GetRace());
    fields[12] = runtime::DecimalUtf16(user.GetGender());
    fields[13] = runtime::DecimalUtf16(user.GetClass());
    fields[14] = runtime::DecimalUtf16(user.GetLevel());
    fields[15] = runtime::DecimalUtf16(petId);
    fields[18] = runtime::DecimalUtf16(petLevel);
    fields[21] = std::u16string(user.GetCharName());
    fields[22] = std::u16string(user.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WritePetUseItemAudit(const CUser& user, int petId, int npcClassId, int itemType, int itemId) {
    // Current RequestPetUseItem 0x0048139e emits this route-6 record only
    // after the mutation succeeds and the positive character ID resolves.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x71);
    fields[1] = runtime::DecimalUtf16(user.GetId());
    fields[2] = runtime::DecimalUtf16(user.GetAccountID());
    fields[5] = runtime::DecimalUtf16(user.GetX());
    fields[6] = runtime::DecimalUtf16(user.GetY());
    fields[7] = runtime::DecimalUtf16(user.GetZ());
    fields[11] = runtime::DecimalUtf16(user.GetRace());
    fields[12] = runtime::DecimalUtf16(user.GetGender());
    fields[13] = runtime::DecimalUtf16(user.GetClass());
    fields[14] = runtime::DecimalUtf16(user.GetLevel());
    fields[15] = runtime::DecimalUtf16(petId);
    fields[16] = runtime::DecimalUtf16(npcClassId);
    fields[18] = runtime::DecimalUtf16(itemType);
    fields[21] = std::u16string(user.GetCharName());
    fields[22] = std::u16string(user.GetAccountName());
    fields[25] = runtime::DecimalUtf16(itemId);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

std::u16string DottedAddressField(int first, int second, int third, int fourth) {
    std::u16string result = runtime::DecimalUtf16(first);
    result.push_back(u'.');
    result.append(runtime::DecimalUtf16(second));
    result.push_back(u'.');
    result.append(runtime::DecimalUtf16(third));
    result.push_back(u'.');
    result.append(runtime::DecimalUtf16(fourth));
    return result;
}

void WriteLogoutAddressMismatchAudit(
    const CUser& user,
    const std::array<std::uint8_t, 4>& previousAddress,
    int address0,
    int address1,
    int address2,
    int address3
) {
    // Current RequestCharacterLogout 0x00464b34 emits the route-8 record
    // through the unnamed Win64 vararg adapter at 0x0045d17c. Its call frame
    // fixes all populated fields, including old IP in field 8 and the packet
    // IP in field 9.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x57f);
    fields[1] = runtime::DecimalUtf16(user.GetId());
    fields[2] = runtime::DecimalUtf16(user.GetAccountID());
    fields[5] = runtime::DecimalUtf16(user.GetX());
    fields[6] = runtime::DecimalUtf16(user.GetY());
    fields[7] = runtime::DecimalUtf16(user.GetZ());
    fields[8] = DottedAddressField(previousAddress[0], previousAddress[1], previousAddress[2], previousAddress[3]);
    fields[9] = DottedAddressField(address0, address1, address2, address3);
    fields[21] = std::u16string(user.GetCharName());
    fields[22] = std::u16string(user.GetAccountName());
    runtime::process::WriteLogFormatterLine(8, runtime::JoinFields(fields, u','));
}

void WriteSetAgitOwnerAudit(const CUser& ruler, const char16_t* pledgeName, int pledgeId, int agitId) {
    // Current RequestSetAgitOwner 0x0048bcb8 emits this exact sparse record
    // through the unnamed Win64 vararg-widening adapter at 0x0045d2a8.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xe8);
    fields[1] = runtime::DecimalUtf16(ruler.GetId());
    fields[2] = runtime::DecimalUtf16(ruler.GetAccountID());
    fields[5] = runtime::DecimalUtf16(ruler.GetX());
    fields[6] = runtime::DecimalUtf16(ruler.GetY());
    fields[7] = runtime::DecimalUtf16(ruler.GetZ());
    fields[8] = pledgeName ? pledgeName : u"";
    fields[11] = runtime::DecimalUtf16(ruler.GetRace());
    fields[12] = runtime::DecimalUtf16(ruler.GetGender());
    fields[13] = runtime::DecimalUtf16(ruler.GetClass());
    fields[14] = runtime::DecimalUtf16(ruler.GetLevel());
    fields[15] = runtime::DecimalUtf16(pledgeId);
    fields[16] = runtime::DecimalUtf16(agitId);
    fields[21] = std::u16string(ruler.GetCharName());
    fields[22] = std::u16string(ruler.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteSetCastleOwnerAudit(const CUser& ruler, const char16_t* pledgeName, int pledgeId, int castleId, int castleType) {
    // Current CSocketHandler::OnRequestSetCastleOwner 0x005979d0 emits this
    // sparse route-6 record through the unnamed adapter at 0x0045d3c4.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xd5);
    fields[1] = runtime::DecimalUtf16(ruler.GetId());
    fields[2] = runtime::DecimalUtf16(ruler.GetAccountID());
    fields[5] = runtime::DecimalUtf16(ruler.GetX());
    fields[6] = runtime::DecimalUtf16(ruler.GetY());
    fields[7] = runtime::DecimalUtf16(ruler.GetZ());
    fields[8] = pledgeName ? pledgeName : u"";
    fields[11] = runtime::DecimalUtf16(ruler.GetRace());
    fields[12] = runtime::DecimalUtf16(ruler.GetGender());
    fields[13] = runtime::DecimalUtf16(ruler.GetClass());
    fields[14] = runtime::DecimalUtf16(ruler.GetLevel());
    fields[15] = runtime::DecimalUtf16(pledgeId);
    fields[16] = runtime::DecimalUtf16(castleId);
    fields[17] = runtime::DecimalUtf16(castleType);
    fields[21] = std::u16string(ruler.GetCharName());
    fields[22] = std::u16string(ruler.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteSetPledgeCrestAudit(const CUser& ruler, const char16_t* pledgeName, int pledgeId) {
    // Current RequestSetPledgeCrest 0x00465b64 emits this exact sparse
    // route-6 record through the unnamed adapter at 0x0045d604.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xd3);
    fields[1] = runtime::DecimalUtf16(ruler.GetId());
    fields[2] = runtime::DecimalUtf16(ruler.GetAccountID());
    fields[5] = runtime::DecimalUtf16(ruler.GetX());
    fields[6] = runtime::DecimalUtf16(ruler.GetY());
    fields[7] = runtime::DecimalUtf16(ruler.GetZ());
    fields[8] = pledgeName ? pledgeName : u"";
    fields[11] = runtime::DecimalUtf16(ruler.GetRace());
    fields[12] = runtime::DecimalUtf16(ruler.GetGender());
    fields[13] = runtime::DecimalUtf16(ruler.GetClass());
    fields[14] = runtime::DecimalUtf16(ruler.GetLevel());
    fields[15] = runtime::DecimalUtf16(pledgeId);
    fields[21] = std::u16string(ruler.GetCharName());
    fields[22] = std::u16string(ruler.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteSetUserNicknameAudit(
    const CUser& target, const CUser& master, const char16_t* nickname, const char16_t* pledgeName, int pledgeId
) {
    // Current RequestSetUserNickname 0x00465f64 emits this exact sparse
    // route-6 record through the unnamed adapter at 0x0045d70c/0x0045d860.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xce);
    fields[1] = runtime::DecimalUtf16(target.GetId());
    fields[2] = runtime::DecimalUtf16(target.GetAccountID());
    fields[3] = runtime::DecimalUtf16(master.GetId());
    fields[4] = runtime::DecimalUtf16(master.GetAccountID());
    fields[5] = runtime::DecimalUtf16(target.GetX());
    fields[6] = runtime::DecimalUtf16(target.GetY());
    fields[7] = runtime::DecimalUtf16(target.GetZ());
    fields[8] = nickname ? nickname : u"";
    fields[9] = pledgeName ? pledgeName : u"";
    fields[11] = runtime::DecimalUtf16(target.GetRace());
    fields[12] = runtime::DecimalUtf16(target.GetGender());
    fields[13] = runtime::DecimalUtf16(target.GetClass());
    fields[14] = runtime::DecimalUtf16(target.GetLevel());
    fields[15] = runtime::DecimalUtf16(pledgeId);
    fields[21] = std::u16string(target.GetCharName());
    fields[22] = std::u16string(target.GetAccountName());
    fields[23] = std::u16string(master.GetCharName());
    fields[24] = std::u16string(master.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteResetUserNicknameAudit(const CUser& target, const char16_t* pledgeName, int pledgeId) {
    // Current RequestResetUserNickname 0x00466b04 emits the same record
    // family as nickname assignment, but has no master identity and records
    // an empty nickname after the successful reset.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xce);
    fields[1] = runtime::DecimalUtf16(target.GetId());
    fields[2] = runtime::DecimalUtf16(target.GetAccountID());
    fields[5] = runtime::DecimalUtf16(target.GetX());
    fields[6] = runtime::DecimalUtf16(target.GetY());
    fields[7] = runtime::DecimalUtf16(target.GetZ());
    fields[9] = pledgeName ? pledgeName : u"";
    fields[11] = runtime::DecimalUtf16(target.GetRace());
    fields[12] = runtime::DecimalUtf16(target.GetGender());
    fields[13] = runtime::DecimalUtf16(target.GetClass());
    fields[14] = runtime::DecimalUtf16(target.GetLevel());
    fields[15] = runtime::DecimalUtf16(pledgeId);
    fields[21] = std::u16string(target.GetCharName());
    fields[22] = std::u16string(target.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteInstallBattleCampAudit(
    const CUser& ruler, const char16_t* pledgeName, int pledgeId, std::uint32_t battleCampId, int maxHp, int x, int y, int z
) {
    // Current RequestManageBattleCamp 0x004833e0 install branch emits this
    // exact sparse route-6 record through the adapter at 0x0045dc20.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xdb);
    fields[1] = runtime::DecimalUtf16(ruler.GetId());
    fields[2] = runtime::DecimalUtf16(ruler.GetAccountID());
    fields[5] = runtime::DecimalUtf16(ruler.GetX());
    fields[6] = runtime::DecimalUtf16(ruler.GetY());
    fields[7] = runtime::DecimalUtf16(ruler.GetZ());
    fields[8] = pledgeName ? pledgeName : u"";
    fields[11] = runtime::DecimalUtf16(ruler.GetRace());
    fields[12] = runtime::DecimalUtf16(ruler.GetGender());
    fields[13] = runtime::DecimalUtf16(ruler.GetClass());
    fields[14] = runtime::DecimalUtf16(ruler.GetLevel());
    fields[15] = runtime::DecimalUtf16(pledgeId);
    fields[16] = runtime::DecimalUtf16(battleCampId);
    fields[17] = runtime::DecimalUtf16(maxHp);
    fields[18] = runtime::DecimalUtf16(x);
    fields[19] = runtime::DecimalUtf16(y);
    fields[20] = runtime::DecimalUtf16(z);
    fields[21] = std::u16string(ruler.GetCharName());
    fields[22] = std::u16string(ruler.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteUninstallBattleCampAudit(const CUser& ruler, const char16_t* pledgeName, std::uint32_t pledgeId) {
    // Current RequestManageBattleCamp 0x004833e0 uninstall branch emits this
    // exact sparse route-6 record through the adapter at 0x0045d074.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xdd);
    fields[1] = runtime::DecimalUtf16(ruler.GetId());
    fields[2] = runtime::DecimalUtf16(ruler.GetAccountID());
    fields[5] = runtime::DecimalUtf16(ruler.GetX());
    fields[6] = runtime::DecimalUtf16(ruler.GetY());
    fields[7] = runtime::DecimalUtf16(ruler.GetZ());
    fields[8] = pledgeName ? pledgeName : u"";
    fields[11] = runtime::DecimalUtf16(ruler.GetRace());
    fields[12] = runtime::DecimalUtf16(ruler.GetGender());
    fields[13] = runtime::DecimalUtf16(ruler.GetClass());
    fields[14] = runtime::DecimalUtf16(ruler.GetLevel());
    fields[15] = runtime::DecimalUtf16(pledgeId);
    fields[21] = std::u16string(ruler.GetCharName());
    fields[22] = std::u16string(ruler.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteUninstallAllBattleCampAudit(std::uint32_t castleId, std::uint32_t battleCampId) {
    // Current RequestManageBattleCamp 0x004833e0 all-camps branch emits one
    // route-6 record per returned database id.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xdc);
    fields[11] = runtime::DecimalUtf16(castleId);
    fields[12] = runtime::DecimalUtf16(battleCampId);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WritePrepareCastleWarAudit(
    int recordId, const CUser& ruler, const char16_t* pledgeName, std::uint32_t pledgeId, std::uint32_t castleId
) {
    // Current RequestPrepareCastleWar 0x0048de6c emits route-6 records
    // 0xdf/0xe0/0xe1 for attacker registration, defender registration and
    // either unregister operation respectively.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(recordId);
    fields[1] = runtime::DecimalUtf16(ruler.GetId());
    fields[2] = runtime::DecimalUtf16(ruler.GetAccountID());
    fields[5] = runtime::DecimalUtf16(ruler.GetX());
    fields[6] = runtime::DecimalUtf16(ruler.GetY());
    fields[7] = runtime::DecimalUtf16(ruler.GetZ());
    fields[8] = pledgeName ? pledgeName : u"";
    fields[11] = runtime::DecimalUtf16(ruler.GetRace());
    fields[12] = runtime::DecimalUtf16(ruler.GetGender());
    fields[13] = runtime::DecimalUtf16(ruler.GetClass());
    fields[14] = runtime::DecimalUtf16(ruler.GetLevel());
    fields[15] = runtime::DecimalUtf16(pledgeId);
    fields[16] = runtime::DecimalUtf16(castleId);
    fields[21] = std::u16string(ruler.GetCharName());
    fields[22] = std::u16string(ruler.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteConfirmCastleDefenceAudit(
    const CUser& ruler,
    const char16_t* pledgeName,
    std::uint32_t loadedPledgeId,
    std::uint32_t requestedPledgeId,
    int operation
) {
    // Current RequestConfirmCastleDefence 0x0048e464 emits route-6 record
    // 0xe2 after the reply whenever both the pledge and its ruler resolve.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xe2);
    fields[1] = runtime::DecimalUtf16(ruler.GetId());
    fields[2] = runtime::DecimalUtf16(ruler.GetAccountID());
    fields[5] = runtime::DecimalUtf16(ruler.GetX());
    fields[6] = runtime::DecimalUtf16(ruler.GetY());
    fields[7] = runtime::DecimalUtf16(ruler.GetZ());
    fields[8] = pledgeName ? pledgeName : u"";
    fields[11] = runtime::DecimalUtf16(ruler.GetRace());
    fields[12] = runtime::DecimalUtf16(ruler.GetGender());
    fields[13] = runtime::DecimalUtf16(ruler.GetClass());
    fields[14] = runtime::DecimalUtf16(ruler.GetLevel());
    fields[15] = runtime::DecimalUtf16(loadedPledgeId);
    fields[16] = runtime::DecimalUtf16(requestedPledgeId);
    fields[17] = runtime::DecimalUtf16(operation);
    fields[21] = std::u16string(ruler.GetCharName());
    fields[22] = std::u16string(ruler.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteResetCastleSiegePledgeAudit(
    const CUser& ruler, const char16_t* pledgeName, std::uint32_t pledgeId, std::uint32_t castleId, int registrationType
) {
    // Current ResetCastleSiegePledge 0x0048e898 emits this route-6 record
    // after each successful lin_InsertCastleWar whose pledge and ruler load.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xe3);
    fields[1] = runtime::DecimalUtf16(ruler.GetId());
    fields[2] = runtime::DecimalUtf16(ruler.GetAccountID());
    fields[5] = runtime::DecimalUtf16(ruler.GetX());
    fields[6] = runtime::DecimalUtf16(ruler.GetY());
    fields[7] = runtime::DecimalUtf16(ruler.GetZ());
    fields[8] = pledgeName ? pledgeName : u"";
    fields[11] = runtime::DecimalUtf16(ruler.GetRace());
    fields[12] = runtime::DecimalUtf16(ruler.GetGender());
    fields[13] = runtime::DecimalUtf16(ruler.GetClass());
    fields[14] = runtime::DecimalUtf16(ruler.GetLevel());
    fields[15] = runtime::DecimalUtf16(pledgeId);
    fields[16] = runtime::DecimalUtf16(castleId);
    fields[17] = runtime::DecimalUtf16(registrationType);
    fields[21] = std::u16string(ruler.GetCharName());
    fields[22] = std::u16string(ruler.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteMissingResetCastleSiegePledgeAudit(
    std::uint32_t pledgeId, std::uint32_t castleId, int registrationType, int worldId
) {
    // The target's generic formatter branch records the packet identity even
    // when CPledge cannot be loaded. A loaded pledge with a missing ruler has
    // no matching record.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xe3);
    fields[15] = runtime::DecimalUtf16(pledgeId);
    fields[16] = runtime::DecimalUtf16(castleId);
    fields[17] = runtime::DecimalUtf16(registrationType);
    fields[18] = runtime::DecimalUtf16(worldId);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteSetNextCastleSiegeAudit(std::uint32_t castleId, std::uint32_t nextWarTime, bool success) {
    // Current RequestSetNextCastleSiege 0x00491644 writes this sparse
    // route-6 record directly through CLog::Add after sending the reply.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xed);
    fields[11] = runtime::DecimalUtf16(runtime::Int32FromBits(castleId));
    fields[12] = runtime::DecimalUtf16(runtime::Int32FromBits(nextWarTime));
    fields[13] = runtime::DecimalUtf16(success ? 1 : 0);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteEndSiegeWinnerAudit(
    const char16_t* pledgeName, const char16_t* allianceName, int allianceId, int residenceId, int pledgeId, int siegeValue
) {
    // Current RequestEndSiege 0x0049c010 emits route-6 record 0xf4 for the
    // winning pledge. This helper is semantic factoring of the direct
    // CLog::Add call. No separate source-level helper name survives.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xf4);
    fields[8] = pledgeName ? pledgeName : u"";
    fields[9] = allianceName ? allianceName : u"";
    fields[11] = runtime::DecimalUtf16(allianceId);
    fields[12] = runtime::DecimalUtf16(residenceId);
    fields[13] = runtime::DecimalUtf16(pledgeId);
    fields[14] = runtime::DecimalUtf16(siegeValue);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteEndSiegeAllianceMemberAudit(
    const char16_t* pledgeName, const char16_t* allianceName, int allianceId, int residenceId, int pledgeId
) {
    // The alliance traversal in current RequestEndSiege emits the adjacent
    // 0xf5 record for every member pledge that resolves in the request's
    // world. Its format has three numeric fields after the two names.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xf5);
    fields[8] = pledgeName ? pledgeName : u"";
    fields[9] = allianceName ? allianceName : u"";
    fields[11] = runtime::DecimalUtf16(allianceId);
    fields[12] = runtime::DecimalUtf16(residenceId);
    fields[13] = runtime::DecimalUtf16(pledgeId);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteCreateAgitAuctionAudit(
    const CUser& user,
    const char16_t* description,
    int auctionId,
    int agitId,
    std::int64_t minimumPrice,
    std::int64_t adenaAmount,
    int auctionDays,
    int auctionTime,
    bool adenaDeleted
) {
    // Current RequestCreateAgitAuction 0x00491834 writes this route-6 record
    // only when the character was resolved. The helper name is a portable
    // factoring of the target's direct CLog::Add call, not a claimed source
    // identifier.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xee);
    fields[1] = runtime::DecimalUtf16(user.GetId());
    fields[2] = runtime::DecimalUtf16(user.GetAccountID());
    fields[5] = runtime::DecimalUtf16(user.GetX());
    fields[6] = runtime::DecimalUtf16(user.GetY());
    fields[7] = runtime::DecimalUtf16(user.GetZ());
    fields[8] = description ? description : u"";
    fields[11] = runtime::DecimalUtf16(auctionId);
    fields[12] = runtime::DecimalUtf16(agitId);
    fields[13] = runtime::DecimalUtf16(minimumPrice);
    fields[14] = runtime::DecimalUtf16(adenaAmount);
    fields[15] = runtime::DecimalUtf16(auctionDays);
    fields[16] = runtime::DecimalUtf16(auctionTime);
    fields[17] = runtime::DecimalUtf16(adenaDeleted ? 1 : 0);
    fields[21] = std::u16string(user.GetCharName());
    fields[22] = std::u16string(user.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteCreateAgitBidAudit(
    const CUser& user, int auctionId, int agitId, int bidId, long long requestedPrice, int requestPledgeId, bool adenaDeleted
) {
    // Current RequestCreateAgitBid 0x00491ec4 writes record 0xef only after
    // resolving the character. This semantic factoring does not claim a
    // separate source helper in the target.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xef);
    fields[1] = runtime::DecimalUtf16(user.GetId());
    fields[2] = runtime::DecimalUtf16(user.GetAccountID());
    fields[5] = runtime::DecimalUtf16(user.GetX());
    fields[6] = runtime::DecimalUtf16(user.GetY());
    fields[7] = runtime::DecimalUtf16(user.GetZ());
    fields[11] = runtime::DecimalUtf16(auctionId);
    fields[12] = runtime::DecimalUtf16(agitId);
    fields[13] = runtime::DecimalUtf16(bidId);
    fields[14] = runtime::DecimalUtf16(requestedPrice);
    fields[15] = runtime::DecimalUtf16(requestPledgeId);
    fields[16] = runtime::DecimalUtf16(adenaDeleted ? 1 : 0);
    fields[21] = std::u16string(user.GetCharName());
    fields[22] = std::u16string(user.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteSetAgitAuctionAudit(
    const char16_t* pledgeName, int agitId, int auctionId, int attendId, long long maxPrice, int pledgeId, int tax
) {
    // Current RequestSetAgitAuction 0x004925e0 writes this sparse route-6
    // record unconditionally. This semantic factoring does not claim that a
    // separate helper existed in the target source.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xf0);
    fields[8] = pledgeName ? pledgeName : u"";
    fields[11] = runtime::DecimalUtf16(agitId);
    fields[12] = runtime::DecimalUtf16(auctionId);
    fields[13] = runtime::DecimalUtf16(attendId);
    fields[14] = runtime::DecimalUtf16(maxPrice);
    fields[15] = runtime::DecimalUtf16(pledgeId);
    fields[16] = runtime::DecimalUtf16(tax);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteCancelAgitAuctionAudit(int agitId, int auctionId) {
    // Current RequestCancelAgitAuction 0x004941c4 emits this sparse route-6
    // record unconditionally. The helper is portable semantic factoring.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xf1);
    fields[11] = runtime::DecimalUtf16(agitId);
    fields[12] = runtime::DecimalUtf16(auctionId);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteAgitCostAudit(
    const char16_t* pledgeName, int agitId, int pledgeId, std::int64_t price, bool success, int nextCost, int costFail
) {
    // Current RequestAgitCost 0x004945f8 emits record 0xf6 after its reply.
    // This helper name is not claimed as an original target identifier.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xf6);
    fields[8] = pledgeName ? pledgeName : u"";
    fields[11] = runtime::DecimalUtf16(agitId);
    fields[12] = runtime::DecimalUtf16(pledgeId);
    fields[13] = runtime::DecimalUtf16(price);
    fields[14] = runtime::DecimalUtf16(success ? 1 : 0);
    fields[15] = runtime::DecimalUtf16(nextCost);
    fields[16] = runtime::DecimalUtf16(costFail);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteAgitAutoSellAudit(int agitId, int auctionId, std::int64_t minimumPrice, int auctionDurationDays, int auctionTime) {
    // Current RequestAgitAutoSell 0x00494c80 emits record 0xf3 after its
    // reply. This helper name is portable semantic factoring only.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xf3);
    fields[11] = runtime::DecimalUtf16(agitId);
    fields[12] = runtime::DecimalUtf16(auctionId);
    fields[13] = runtime::DecimalUtf16(minimumPrice);
    fields[14] = runtime::DecimalUtf16(auctionDurationDays);
    fields[15] = runtime::DecimalUtf16(auctionTime);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteAgitBidCancelAudit(
    const char16_t* pledgeName, int agitId, int pledgeId, int tax, std::int64_t price, std::int64_t refund
) {
    // Current RequestAgitBidCancel 0x00494ed4 emits record 0xf2 after its
    // reply. Success is present on the reply wire but not in this log row.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xf2);
    fields[8] = pledgeName ? pledgeName : u"";
    fields[11] = runtime::DecimalUtf16(agitId);
    fields[12] = runtime::DecimalUtf16(pledgeId);
    fields[13] = runtime::DecimalUtf16(tax);
    fields[14] = runtime::DecimalUtf16(price);
    fields[15] = runtime::DecimalUtf16(refund);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteDoorHpChangedAudit(const char16_t* doorName, int hp) {
    // Current RequestSaveDoorStatus 0x004902f4 emits this sparse route-6
    // record only after lin_SaveDoorStatus succeeds.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xe5);
    fields[8] = doorName ? doorName : u"";
    fields[11] = runtime::DecimalUtf16(hp);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteSaveCastleIncomeAudit(int residenceId, std::int64_t shopIncome, std::int64_t shopIncomeTemp, int incomeReason) {
    // Current RequestSaveCastleIncome 0x004904e0 calls the generic formatter
    // at 0x004efa60. Raw stack setup at 0x00490604 fixes the intentionally
    // empty nEtcNum2 and places the SaveIncome reason in nEtcNum5.
    CLogFormatter formatter;
    formatter.WriteLog(
        0xea, 0, 0, 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, residenceId, 0, shopIncome, shopIncomeTemp, incomeReason, 0, 0,
        0, 0, 0, nullptr, nullptr, nullptr, nullptr, 0
    );
}

void WritePledgeContributionAudit(
    const char16_t* pledgeName, int residenceId, int pledgeId, int resetContribution, int contribution
) {
    // Current RequestSavePledgeContribution emits this sparse record through
    // CLog::Add on route 6 even when lin_SavePledgeContribution fails.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xe6);
    fields[8] = pledgeName ? pledgeName : u"";
    fields[11] = runtime::DecimalUtf16(residenceId);
    fields[12] = runtime::DecimalUtf16(pledgeId);
    fields[13] = runtime::DecimalUtf16(resetContribution);
    fields[14] = runtime::DecimalUtf16(contribution);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteContributionWinnerAudit(const char16_t* pledgeName, int residenceId, int winnerPledgeId, int resetContribution) {
    // Raw vararg setup at 0x0048d28c fixes the three numeric fields of the
    // current route-6 winner record.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xe7);
    fields[8] = pledgeName ? pledgeName : u"";
    fields[11] = runtime::DecimalUtf16(residenceId);
    fields[12] = runtime::DecimalUtf16(winnerPledgeId);
    fields[13] = runtime::DecimalUtf16(resetContribution);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

bool ExecuteSetCastleWar(
    DBConn& connection,
    std::uint32_t worldId,
    std::uint32_t pledgeId,
    std::uint32_t castleId,
    int registrationType,
    std::uint32_t proposeTime
) {
    // Semantic name for the unnamed current helper at 0x004844b0. Its exact
    // five bindings are uint32, uint32, uint32, int32 and uint32.
    constexpr auto statement = L"{Call lin_SetCastleWar (?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&worldId);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&castleId);
    connection.BindParameter(&registrationType);
    connection.BindParameter(&proposeTime);
    return connection.ExecutePrepared(statement);
}

bool ExecuteUpdateCastleWar(
    DBConn& connection, std::uint32_t castleId, std::uint32_t pledgeId, int registrationType, std::uint32_t worldId
) {
    // Semantic name for the unnamed current helper at 0x00484540. Its exact
    // four bindings are uint32, uint32, int32 and uint32.
    constexpr auto statement = L"{Call lin_UpdateCastleWar (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&castleId);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&registrationType);
    connection.BindParameter(&worldId);
    return connection.ExecutePrepared(statement);
}

bool ExecuteInsertCastleWar(
    DBConn& connection, std::uint32_t castleId, std::uint32_t pledgeId, int registrationType, int worldId
) {
    // Semantic name for the unnamed current helper at 0x004845bc. Raw call
    // setup in ResetCastleSiegePledge confirms all four stack/register values.
    constexpr auto statement = L"{Call lin_InsertCastleWar (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&castleId);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&registrationType);
    connection.BindParameter(&worldId);
    return connection.ExecutePrepared(statement);
}

bool ExecuteCreateAgitAuction(
    DBConn& connection, int agitId, const wchar_t* description, long long minimumPrice, int auctionTime, int auctionTax
) {
    // Semantic name for unnamed current helper 0x004847d4. The current
    // machine fixes the five bindings as int32, UTF-16, int64, int32, int32.
    constexpr auto statement = L"{Call lin_CreateAgitAuction (?, ?, ?, ?, ? )}";
    connection.Prepare(statement);
    connection.BindParameter(&agitId);
    connection.BindParameter(description);
    connection.BindParameter(&minimumPrice);
    connection.BindParameter(&auctionTime);
    connection.BindParameter(&auctionTax);
    return connection.ExecutePrepared(statement);
}

bool ExecuteInsertAgitAdena(DBConn& connection, int agitId, int pledgeId, int auctionId, int reason, long long adenaAmount) {
    // Semantic name for unnamed current helper 0x0048486c. No surviving
    // source identifier exists. Bind order and widths are machine-confirmed.
    constexpr auto statement = L"{Call lin_InsertAgitAdena (?, ?, ?, ?, ? )}";
    connection.Prepare(statement);
    connection.BindParameter(&agitId);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&auctionId);
    connection.BindParameter(&reason);
    connection.BindParameter(&adenaAmount);
    return connection.ExecutePrepared(statement);
}

bool ExecuteCreateAgitBid(
    DBConn& connection, int agitId, int auctionId, long long requestedPrice, int requestPledgeId, int attendTime
) {
    // Semantic name for unnamed current helper 0x004848fc. The exact target
    // binding order is int32, int32, int64, int32 and int32.
    constexpr auto statement = L"{Call lin_CreateAgitBid (?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&agitId);
    connection.BindParameter(&auctionId);
    connection.BindParameter(&requestedPrice);
    connection.BindParameter(&requestPledgeId);
    connection.BindParameter(&attendTime);
    return connection.ExecutePrepared(statement);
}

void WriteAquireSkillAudit(const CUser& user, int skillId, int skillLevel, int skillSubLevel) {
    // Current RequestAquireSkill 0x004687b0 emits this exact sparse route-6
    // record through the unnamed adapter at 0x0045de94.
    const std::uint32_t packedSkillLevel =
        (static_cast<std::uint32_t>(skillSubLevel) << 16) | (static_cast<std::uint32_t>(skillLevel) & 0xffffU);

    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x191);
    fields[1] = runtime::DecimalUtf16(user.GetId());
    fields[2] = runtime::DecimalUtf16(user.GetAccountID());
    fields[5] = runtime::DecimalUtf16(user.GetX());
    fields[6] = runtime::DecimalUtf16(user.GetY());
    fields[7] = runtime::DecimalUtf16(user.GetZ());
    fields[11] = runtime::DecimalUtf16(user.GetRace());
    fields[12] = runtime::DecimalUtf16(user.GetGender());
    fields[13] = runtime::DecimalUtf16(user.GetClass());
    fields[14] = runtime::DecimalUtf16(user.GetLevel());
    fields[15] = runtime::DecimalUtf16(skillId);
    fields[16] = runtime::DecimalUtf16(packedSkillLevel);
    fields[17] = runtime::DecimalUtf16(user.GetSP());
    fields[18] = runtime::DecimalUtf16(user.GetBuilder());
    fields[21] = std::u16string(user.GetCharName());
    fields[22] = std::u16string(user.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteDeleteSkillAudit(const CUser& user, int skillId, int previousSkillLevelSet) {
    // Current RequestDeleteSkill 0x0046aad8 and
    // RequestDeletePledgeSkill 0x0047a8e0 route this same complete 26-field
    // record through the generic formatter at 0x004efa60. Unlike the old C1
    // record, field 16 is builder and field 17 is the level/sublevel value
    // captured before deletion. SP is not present.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x192);
    fields[1] = runtime::DecimalUtf16(user.GetId());
    fields[2] = runtime::DecimalUtf16(user.GetAccountID());
    fields[5] = runtime::DecimalUtf16(user.GetX());
    fields[6] = runtime::DecimalUtf16(user.GetY());
    fields[7] = runtime::DecimalUtf16(user.GetZ());
    fields[11] = runtime::DecimalUtf16(user.GetRace());
    fields[12] = runtime::DecimalUtf16(user.GetGender());
    fields[13] = runtime::DecimalUtf16(user.GetClass());
    fields[14] = runtime::DecimalUtf16(user.GetLevel());
    fields[15] = runtime::DecimalUtf16(skillId);
    fields[16] = runtime::DecimalUtf16(user.GetBuilder());
    fields[17] = runtime::DecimalUtf16(previousSkillLevelSet);
    fields[21] = std::u16string(user.GetCharName());
    fields[22] = std::u16string(user.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteSetPledgeInfoAudit(
    const CUser& ruler,
    const char16_t* pledgeName,
    std::uint32_t pledgeId,
    std::uint32_t fieldSelector,
    std::uint32_t newValue,
    std::uint32_t skillLevel,
    std::uint32_t memberCount
) {
    // Current RequestSetPledgeInfo 0x004673d0 emits this exact sparse
    // route-6 record through the unnamed adapter at 0x0045d9b4.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xd7);
    fields[1] = runtime::DecimalUtf16(ruler.GetId());
    fields[2] = runtime::DecimalUtf16(ruler.GetAccountID());
    fields[5] = runtime::DecimalUtf16(ruler.GetX());
    fields[6] = runtime::DecimalUtf16(ruler.GetY());
    fields[7] = runtime::DecimalUtf16(ruler.GetZ());
    fields[8] = pledgeName ? pledgeName : u"";
    fields[11] = runtime::DecimalUtf16(ruler.GetRace());
    fields[12] = runtime::DecimalUtf16(ruler.GetGender());
    fields[13] = runtime::DecimalUtf16(ruler.GetClass());
    fields[14] = runtime::DecimalUtf16(ruler.GetLevel());
    fields[15] = runtime::DecimalUtf16(pledgeId);
    fields[16] = runtime::DecimalUtf16(fieldSelector);
    fields[17] = runtime::DecimalUtf16(newValue);
    fields[18] = runtime::DecimalUtf16(skillLevel);
    fields[19] = runtime::DecimalUtf16(memberCount);
    fields[21] = std::u16string(ruler.GetCharName());
    fields[22] = std::u16string(ruler.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteSetUserPledgeInfoAudit(
    const CUser& user,
    const char16_t* pledgeName,
    std::uint32_t pledgeId,
    std::uint32_t fieldSelector,
    std::uint32_t newValue
) {
    // Current RequestSetUserPledgeInfo 0x00467968 emits this exact sparse
    // route-6 record through the unnamed adapter at 0x0045dafc.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xd9);
    fields[1] = runtime::DecimalUtf16(user.GetId());
    fields[2] = runtime::DecimalUtf16(user.GetAccountID());
    fields[5] = runtime::DecimalUtf16(user.GetX());
    fields[6] = runtime::DecimalUtf16(user.GetY());
    fields[7] = runtime::DecimalUtf16(user.GetZ());
    fields[8] = pledgeName ? pledgeName : u"";
    fields[11] = runtime::DecimalUtf16(user.GetRace());
    fields[12] = runtime::DecimalUtf16(user.GetGender());
    fields[13] = runtime::DecimalUtf16(user.GetClass());
    fields[14] = runtime::DecimalUtf16(user.GetLevel());
    fields[15] = runtime::DecimalUtf16(pledgeId);
    fields[16] = runtime::DecimalUtf16(fieldSelector);
    fields[17] = runtime::DecimalUtf16(newValue);
    fields[21] = std::u16string(user.GetCharName());
    fields[22] = std::u16string(user.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteCreatePledgeAudit(const CUser& ruler, const char16_t* pledgeName, int pledgeId) {
    // Current RequestCreatePledgeByNpc 0x00462df4 emits this exact sparse
    // route-6 record through the unnamed adapter at 0x0045cd2c.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xc9);
    fields[1] = runtime::DecimalUtf16(ruler.GetId());
    fields[2] = runtime::DecimalUtf16(ruler.GetAccountID());
    fields[5] = runtime::DecimalUtf16(ruler.GetX());
    fields[6] = runtime::DecimalUtf16(ruler.GetY());
    fields[7] = runtime::DecimalUtf16(ruler.GetZ());
    fields[8] = pledgeName ? pledgeName : u"";
    fields[11] = runtime::DecimalUtf16(ruler.GetRace());
    fields[12] = runtime::DecimalUtf16(ruler.GetGender());
    fields[13] = runtime::DecimalUtf16(ruler.GetClass());
    fields[14] = runtime::DecimalUtf16(ruler.GetLevel());
    fields[15] = runtime::DecimalUtf16(pledgeId);
    fields[21] = std::u16string(ruler.GetCharName());
    fields[22] = std::u16string(ruler.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteAllianceAudit(
    int recordId,
    const CUser& ruler,
    const char16_t* allianceName,
    const char16_t* pledgeName,
    std::uint32_t allianceId,
    std::uint32_t pledgeId
) {
    // Current alliance handlers 0x0046cd34..0x0046e1bc all emit this same
    // 26-field route-6 shape through the formatter adapter at 0x0045e218
    // (Create uses its equivalent entry at 0x0045e0ec).
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(recordId);
    fields[1] = runtime::DecimalUtf16(ruler.GetId());
    fields[2] = runtime::DecimalUtf16(ruler.GetAccountID());
    fields[5] = runtime::DecimalUtf16(ruler.GetX());
    fields[6] = runtime::DecimalUtf16(ruler.GetY());
    fields[7] = runtime::DecimalUtf16(ruler.GetZ());
    fields[8] = allianceName ? allianceName : u"";
    fields[9] = pledgeName ? pledgeName : u"";
    fields[11] = runtime::DecimalUtf16(ruler.GetRace());
    fields[12] = runtime::DecimalUtf16(ruler.GetGender());
    fields[13] = runtime::DecimalUtf16(ruler.GetClass());
    fields[14] = runtime::DecimalUtf16(ruler.GetLevel());
    fields[15] = runtime::DecimalUtf16(allianceId);
    fields[16] = runtime::DecimalUtf16(pledgeId);
    fields[21] = std::u16string(ruler.GetCharName());
    fields[22] = std::u16string(ruler.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteDeletePledgeByTimerAudit(const CUser& ruler, const char16_t* pledgeName, int pledgeId) {
    // Current RequestDeletePledgeByTimer 0x0049d7e8 emits this exact sparse
    // route-6 record through the unnamed adapter at 0x0045d074.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xd8);
    fields[1] = runtime::DecimalUtf16(ruler.GetId());
    fields[2] = runtime::DecimalUtf16(ruler.GetAccountID());
    fields[5] = runtime::DecimalUtf16(ruler.GetX());
    fields[6] = runtime::DecimalUtf16(ruler.GetY());
    fields[7] = runtime::DecimalUtf16(ruler.GetZ());
    fields[8] = pledgeName ? pledgeName : u"";
    fields[11] = runtime::DecimalUtf16(ruler.GetRace());
    fields[12] = runtime::DecimalUtf16(ruler.GetGender());
    fields[13] = runtime::DecimalUtf16(ruler.GetClass());
    fields[14] = runtime::DecimalUtf16(ruler.GetLevel());
    fields[15] = runtime::DecimalUtf16(pledgeId);
    fields[21] = std::u16string(ruler.GetCharName());
    fields[22] = std::u16string(ruler.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteCastleTaxAudit(const CUser& ruler, const char16_t* pledgeName, int pledgeId, int residenceId, int taxRate) {
    // Current RequestWriteCastleTax 0x0048d780 emits this exact sparse
    // route-6 record through the unnamed adapter at 0x0045d3c4.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0xd6);
    fields[1] = runtime::DecimalUtf16(ruler.GetId());
    fields[2] = runtime::DecimalUtf16(ruler.GetAccountID());
    fields[5] = runtime::DecimalUtf16(ruler.GetX());
    fields[6] = runtime::DecimalUtf16(ruler.GetY());
    fields[7] = runtime::DecimalUtf16(ruler.GetZ());
    fields[8] = pledgeName ? pledgeName : u"";
    fields[11] = runtime::DecimalUtf16(ruler.GetRace());
    fields[12] = runtime::DecimalUtf16(ruler.GetGender());
    fields[13] = runtime::DecimalUtf16(ruler.GetClass());
    fields[14] = runtime::DecimalUtf16(ruler.GetLevel());
    fields[15] = runtime::DecimalUtf16(pledgeId);
    fields[16] = runtime::DecimalUtf16(residenceId);
    fields[17] = runtime::DecimalUtf16(taxRate);
    fields[21] = std::u16string(ruler.GetCharName());
    fields[22] = std::u16string(ruler.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

// Current helper 0x0045b0d0 has no guard string or recoverable source name.
// Its two wire shapes and argument order are fixed by the machine code.
void SendDropItemFailure(
    CSocket* socket,
    short residenceGuardDrop,
    int creatureObjectId,
    int x,
    int y,
    int z,
    short dropOnDeath,
    int guardAngle,
    int residenceItemId,
    int residenceId,
    short querySucceeded,
    int itemObjectId
) {
    if (residenceGuardDrop == 0) {
        socket->Send(
            "cddddddQddQhh", 0x0f, creatureObjectId, x, y, z, 0, itemObjectId, static_cast<long long>(0), 0, 0,
            static_cast<long long>(0), 0, static_cast<int>(dropOnDeath)
        );
        return;
    }

    socket->Send(
        "cddddddQddQhhdddh", 0x0f, creatureObjectId, x, y, z, 0, itemObjectId, static_cast<long long>(0), 0, 0,
        static_cast<long long>(0), static_cast<int>(residenceGuardDrop), static_cast<int>(dropOnDeath), guardAngle,
        residenceItemId, residenceId, static_cast<int>(querySucceeded)
    );
}

void WriteLargeAdenaDropAudit(
    std::int64_t actorId,
    std::int64_t accountId,
    int x,
    int y,
    int z,
    int race,
    int gender,
    int characterClass,
    int level,
    int enchant,
    int itemType,
    std::int64_t negativeDropAmount,
    std::int64_t remainingAmount,
    const std::u16string& characterName,
    const std::u16string& accountName,
    std::int64_t sourceItemId
) {
    // Current DropItemPacket emits log 0x589 directly through CLog::AddV,
    // leaving fields 3/4, 8..10, 15/16 and 23/24 blank.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x589);
    fields[1] = runtime::DecimalUtf16(actorId);
    fields[2] = runtime::DecimalUtf16(accountId);
    fields[5] = runtime::DecimalUtf16(x);
    fields[6] = runtime::DecimalUtf16(y);
    fields[7] = runtime::DecimalUtf16(z);
    fields[11] = runtime::DecimalUtf16(race);
    fields[12] = runtime::DecimalUtf16(gender);
    fields[13] = runtime::DecimalUtf16(characterClass);
    fields[14] = runtime::DecimalUtf16(level);
    fields[17] = runtime::DecimalUtf16(enchant);
    fields[18] = runtime::DecimalUtf16(itemType);
    fields[19] = runtime::DecimalUtf16(negativeDropAmount);
    fields[20] = runtime::DecimalUtf16(remainingAmount);
    fields[21] = characterName;
    fields[22] = accountName;
    fields[25] = runtime::DecimalUtf16(sourceItemId);
    runtime::process::WriteLogFormatterLine(8, runtime::JoinFields(fields, u','));
}

void WriteInvalidAdenaAudit(
    std::int64_t characterId,
    std::int64_t accountId,
    int x,
    int y,
    int z,
    int level,
    int characterClass,
    std::int64_t actualAdena,
    std::uint32_t actualAdenaItemId,
    std::int64_t packetAdena,
    std::uint32_t packetAdenaItemId,
    const std::u16string& characterName,
    const std::u16string& accountName
) {
    // Current SaveItemsPacket 0x0048adfc writes log 0x57d directly to route
    // 6. It is not a CLogFormatter method call in the target.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x57d);
    fields[1] = runtime::DecimalUtf16(characterId);
    fields[2] = runtime::DecimalUtf16(accountId);
    fields[5] = runtime::DecimalUtf16(x);
    fields[6] = runtime::DecimalUtf16(y);
    fields[7] = runtime::DecimalUtf16(z);
    fields[11] = runtime::DecimalUtf16(level);
    fields[12] = runtime::DecimalUtf16(characterClass);
    fields[13] = runtime::DecimalUtf16(actualAdena);
    fields[14] = runtime::DecimalUtf16(actualAdenaItemId);
    fields[15] = runtime::DecimalUtf16(packetAdena);
    fields[16] = runtime::DecimalUtf16(packetAdenaItemId);
    fields[21] = characterName;
    fields[22] = accountName;
    fields[25] = runtime::DecimalUtf16(actualAdenaItemId);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

bool EqualWideCaseInsensitive(const wchar_t* left, const wchar_t* right) {
    if (!left || !right)
        return left == right;
    while (*left != L'\0' && *right != L'\0') {
        if (std::towlower(*left) != std::towlower(*right)) {
            return false;
        }
        ++left;
        ++right;
    }
    return *left == *right;
}

std::int32_t CharacterSelectionTime32(const SYSTEMTIME& value) noexcept {
    // The first LoadCharacterPacket traversal zeroes a CRT tm, fills these
    // six fields and calls _mktime32. In particular tm_isdst remains zero.
    std::tm local{};
    local.tm_year = static_cast<int>(value.wYear) - 1900;
    local.tm_mon = static_cast<int>(value.wMonth) - 1;
    local.tm_mday = static_cast<int>(value.wDay);
    local.tm_hour = static_cast<int>(value.wHour);
    local.tm_min = static_cast<int>(value.wMinute);
    local.tm_sec = static_cast<int>(value.wSecond);
    const std::time_t converted = std::mktime(&local);
    if (converted == static_cast<std::time_t>(-1) || converted < 0 ||
        converted > static_cast<std::time_t>(std::numeric_limits<std::int32_t>::max())) {
        return -1;
    }
    return static_cast<std::int32_t>(converted);
}

std::int32_t TargetMktime32(
    int year, int month, int day, int hour, int minute, int second, bool databaseCalendarFields
) noexcept {
    std::tm local{};
    local.tm_year = databaseCalendarFields ? year - 1900 : year;
    local.tm_mon = databaseCalendarFields ? month - 1 : month;
    local.tm_mday = day;
    local.tm_hour = hour;
    local.tm_min = minute;
    local.tm_sec = second;
    // The successful target branch stores zero. The fallback
    // branch leaves this stack member indeterminate. Zero is the portable,
    // deterministic boundary while retaining that branch's raw year/month.
    local.tm_isdst = 0;
#if defined(_WIN32)
    return static_cast<std::int32_t>(::_mktime32(&local));
#else
    const std::time_t converted = std::mktime(&local);
    if (converted == static_cast<std::time_t>(-1) || converted < static_cast<std::time_t>(0) ||
        converted > static_cast<std::time_t>(std::numeric_limits<std::int32_t>::max())) {
        return -1;
    }
    return static_cast<std::int32_t>(converted);
#endif
}

std::uint64_t CharacterBirthdaySortKey(const SYSTEMTIME& value) noexcept {
    // Current Windows code converts SYSTEMTIME to FILETIME and compares the
    // resulting unsigned 64-bit value. This fixed-width lexicographic key
    // has the same ordering for valid calendar values on every host.
    return (static_cast<std::uint64_t>(value.wYear) << 48U) | (static_cast<std::uint64_t>(value.wMonth) << 44U) |
        (static_cast<std::uint64_t>(value.wDay) << 39U) | (static_cast<std::uint64_t>(value.wHour) << 34U) |
        (static_cast<std::uint64_t>(value.wMinute) << 28U) | (static_cast<std::uint64_t>(value.wSecond) << 22U) |
        static_cast<std::uint64_t>(value.wMilliseconds);
}

std::array<int, 60> EquipmentObjectIds(const UserEquipmentSlots& equipment) {
    return {
        equipment.underwear,
        equipment.rightEar,
        equipment.leftEar,
        equipment.neck,
        equipment.rightFinger,
        equipment.leftFinger,
        equipment.head,
        equipment.rightHand,
        equipment.leftHand,
        equipment.gloves,
        equipment.chest,
        equipment.legs,
        equipment.feet,
        equipment.back,
        equipment.bothHand,
        equipment.hair,
        equipment.hair2,
        equipment.rightBracelet,
        equipment.leftBracelet,
        equipment.agathionMain,
        equipment.agathionSub[0],
        equipment.agathionSub[1],
        equipment.agathionSub[2],
        equipment.agathionSub[3],
        equipment.deco[0], equipment.deco[1], equipment.deco[2],
        equipment.deco[3], equipment.deco[4], equipment.deco[5],
        equipment.waist, equipment.brooch,
        equipment.jewel[0], equipment.jewel[1], equipment.jewel[2],
        equipment.jewel[3], equipment.jewel[4], equipment.jewel[5],
        equipment.artifactBook,
        equipment.artifactA[0],
        equipment.artifactA[1],
        equipment.artifactA[2],
        equipment.artifactA[3],
        equipment.artifactA[4],
        equipment.artifactA[5],
        equipment.artifactA[6],
        equipment.artifactA[7],
        equipment.artifactA[8],
        equipment.artifactA[9],
        equipment.artifactA[10],
        equipment.artifactA[11],
        equipment.artifactB[0],
        equipment.artifactB[1],
        equipment.artifactB[2],
        equipment.artifactC[0],
        equipment.artifactC[1],
        equipment.artifactC[2],
        equipment.artifactD[0],
        equipment.artifactD[1],
        equipment.artifactD[2],
        // equipment.deco[0], equipment.deco[1], equipment.deco[2],
        // equipment.deco[3], equipment.deco[4], equipment.deco[5],
        // equipment.waist, equipment.brooch,
        // equipment.jewel[0], equipment.jewel[1], equipment.jewel[2],
        // equipment.jewel[3], equipment.jewel[4], equipment.jewel[5],
    };
}

struct FindCharactersToDeletePredicate {
    explicit FindCharactersToDeletePredicate(int accountId) : accountId(accountId) {}

    bool Do(CUser* user) {
        if (!user) {
            // Log.Add(
            //     LOG_ERROR, "[%s][%d][LoadCharacterPacket] invalid user account[%d]", "CacheSocket.cpp", 0x488,
            //     accountId
            // )
            // L2_LOG_ERROR(Log, "[%s][%d][LoadCharacterPacket] invalid user account[%d]", "CacheSocket.cpp", 0x488,
            //     accountId
            // )
            L2_LOG_ERROR(Log, "[characters] Cannot check scheduled deletions for account %d: null character entry. Character-list load marked failed.", accountId);
            hasError = true;
            return false;
        }

        const int elapsed = user->GetDeletedElapsedSecond();
        if (elapsed >= GetCharacterDeleteWaitSeconds()) {
            if (expiredCount < expiredCharacterIds.size()) {
                expiredCharacterIds[expiredCount++] = user->GetId();
            }
            return true;
        }

        const SYSTEMTIME& lastLogin = user->GetLastLoginTime();
        const SYSTEMTIME& selectedTime = lastLogin.wYear != 0 ? lastLogin : *user->GetBirthday();
        const std::int32_t timestamp = CharacterSelectionTime32(selectedTime);
        if (latestTimestamp < timestamp) {
            latestTimestamp = timestamp;
            latestCharacterId = user->GetId();
        }
        return true;
    }

    int accountId = 0;
    bool hasError = false;
    std::array<std::uint32_t, 7> expiredCharacterIds{};
    std::size_t expiredCount = 0;
    std::int32_t latestTimestamp = 0;
    std::uint32_t latestCharacterId = 0;
};

struct CharacterOrderRecord {
    int originalPosition = -1;
    std::uint64_t birthday = 0;
    int highestLevel = 0;
    std::int64_t highestExperience = 0;
};

struct CollectCharacterOrderPredicate {
    CollectCharacterOrderPredicate(int userSocketId, bool& hasError) : userSocketId(userSocketId), hasError(hasError) {}

    bool Do(CUser* user) {
        if (!user) {
            hasError = true;
            return false;
        }

        // The shipping loop observes the eighth user's highest level before
        // logging and resets its supported count to seven. Do the same
        // without reproducing the target's one-record stack overwrite.
        maximumHighestLevel = std::max(maximumHighestLevel, user->GetHighestLevel());
        if (count >= records.size()) {
            // Log.Add(LOG_ERROR, "max character amount over!!! [UserServerId : %d] ", userSocketId)
            // L2_LOG_ERROR(Log, "max character amount over!!! [UserServerId : %d] ", userSocketId)
            L2_LOG_ERROR(Log, "[characters] Cannot add another character to selection order for socket %d: capacity 7 reached.", userSocketId);
            return false;
        }

        CharacterOrderRecord& record = records[count];
        record.originalPosition = static_cast<int>(count);
        record.birthday = CharacterBirthdaySortKey(*user->GetBirthday());
        record.highestLevel = user->GetHighestLevel();
        record.highestExperience = user->GetHighestExperience();
        ++count;
        return true;
    }

    int userSocketId = 0;
    bool& hasError;
    std::array<CharacterOrderRecord, 7> records{};
    std::size_t count = 0;
    int maximumHighestLevel = 0;
};

void SortAndAssignFreeToPlayStates(CollectCharacterOrderPredicate& order, std::array<int, 7>& states) {
    // Current code emits a stable descending bubble sort over FILETIME.
    for (std::size_t pass = order.count; pass > 0; --pass) {
        for (std::size_t index = 1; index < order.count; ++index) {
            if (order.records[index - 1].birthday < order.records[index].birthday) {
                std::swap(order.records[index - 1], order.records[index]);
            }
        }
    }

    const CacheDConfiguration& configuration = GetCacheDConfiguration();
    if (order.maximumHighestLevel < configuration.deactivateMinLevel) {
        int activated = 0;
        for (std::size_t index = 0; index < order.count; ++index) {
            const int original = order.records[index].originalPosition;
            states[static_cast<std::size_t>(original)] = activated < configuration.activateCharacterAmount ? 1 : 2;
            ++activated;
        }
        return;
    }

    std::fill_n(states.begin(), order.count, 3);
}

constexpr std::size_t kRightHandSlot = 7;
constexpr std::size_t kLeftHandSlot = 8;
constexpr std::size_t kGlovesSlot = 9;
constexpr std::size_t kChestSlot = 10;
constexpr std::size_t kLegsSlot = 11;
constexpr std::size_t kFeetSlot = 12;
constexpr std::size_t kBothHandSlot = 14;
constexpr std::size_t kHairSlot = 15;
constexpr std::size_t kHair2Slot = 16;
constexpr std::size_t kHeadSlot = 6;

int ItemTypeOrZero(const CItemSP& item) noexcept { return item ? static_cast<int>(item->ItemType()) : 0; }

int AppearanceOrZero(const CItemSP& item) noexcept { return item ? item->AppearanceType() : 0; }

int EnchantOrZero(const CItemSP& item) noexcept { return item ? item->Enchant() : 0; }

int AssembleCharacterRecord(
    char* buffer,
    int capacity,
    CUser& user,
    const std::array<CItemSP, 60>& items,
    int freeToPlayState,
    std::uint32_t latestCharacterId
) {
    std::array<int, 60> itemTypes{};
    for (std::size_t index = 0; index < items.size(); ++index) {
        itemTypes[index] = ItemTypeOrZero(items[index]);
    }

    const std::array<int, 9> appearances{
        AppearanceOrZero(items[kRightHandSlot]),
        AppearanceOrZero(items[kLeftHandSlot]),
        AppearanceOrZero(items[kGlovesSlot]),
        AppearanceOrZero(items[kChestSlot]),
        AppearanceOrZero(items[kLegsSlot]),
        AppearanceOrZero(items[kFeetSlot]),
        AppearanceOrZero(items[kBothHandSlot]),
        AppearanceOrZero(items[kHairSlot]),
        AppearanceOrZero(items[kHair2Slot]),
    };
    const std::array<int, 5> armorEnchants{
        EnchantOrZero(items[kChestSlot]),
        EnchantOrZero(items[kLegsSlot]),
        EnchantOrZero(items[kHeadSlot]),
        EnchantOrZero(items[kGlovesSlot]),
        EnchantOrZero(items[kFeetSlot]),
    };

    const CItemSP& weapon = items[kRightHandSlot] ? items[kRightHandSlot] : items[kBothHandSlot];
    const int weaponEnchant = EnchantOrZero(weapon);
    const int variationOption1 = weapon ? weapon->OptionKey().variationOption1 : 0;
    const int variationOption2 = weapon ? weapon->OptionKey().variationOption2 : 0;

    const int deletedElapsed = user.GetDeletedElapsedSecond();
    std::uint32_t deletionAndBan = 0;
    if (deletedElapsed > -86400) {
        deletionAndBan = static_cast<std::uint32_t>(GetCharacterDeleteWaitSeconds() - deletedElapsed);
    }

    const int status = user.GetStatus();
    if (status == 2) {
        const std::string characterName = Utf16ToUtf8(user.GetCharName());
        // Log.Add(LOG_NORMAL, "eternally banned char [%s]", characterName.c_str())
        // Previous: L2_LOG_INFO(Log, "eternally banned char [%s]", characterName.c_str())
        L2_LOG_INFO(Log,
            "[character access] Character %u (%s) is permanently banned; setting the ban flag in the character-list reply.",
            user.GetId(), characterName.c_str());
        deletionAndBan |= 0x80000000U;
    } else if (status == 1) {
        const int banEnd = user.GetBanEnd();
        const std::int32_t logTime = runtime::crt::Time32Now();
        // Log.Add(LOG_NORMAL, "ban time status:%d, nBanTime:%d, time_l2(0):%d", 1, banEnd, logTime)
        // Previous: L2_LOG_INFO(Log, "ban time status:%d, nBanTime:%d, time_l2(0):%d", 1, banEnd, logTime)
        L2_LOG_INFO(Log,
            "[character access] Character %u has a temporary ban ending at %d; current server time is %d.",
            user.GetId(), banEnd, logTime);
        if (runtime::crt::Time32Now() < banEnd) {
            const std::string characterName = Utf16ToUtf8(user.GetCharName());
            // Log.Add(LOG_NORMAL, "banned, char [%s]", characterName.c_str())
            // Previous: L2_LOG_INFO(Log, "banned, char [%s]", characterName.c_str())
            L2_LOG_INFO(Log,
                "[character access] Character %u (%s) is still banned; setting the ban flag in the character-list reply.",
                user.GetId(), characterName.c_str());
            deletionAndBan |= 0x80000000U;
        } else {
            user.SetStatus(0);
            user.SetBanEnd(0);

            DBConn connection(runtime::process::CurrentDatabaseId());
            const std::uint32_t characterId = user.GetId();
            const int zero = 0;
            constexpr const wchar_t* statement = L"{Call lin_SetUserBan (?, ?, ?, ? )}";
            connection.Prepare(statement);
            connection.BindParameter(&characterId);
            connection.BindParameter(&zero);
            connection.BindParameter(&zero);
            connection.BindParameter(&zero);
            connection.ExecutePrepared(statement);
        }
    }

    user.CheckVitalityReset();
    const int vitality = user.GetVitality();
    const CVitalData vitalityData = user.GetVitalData();

    // The current instruction order reads +0x5f0 before IsHero(), while
    // IsHero reloads the seven nobless columns. Preserve that stale-first
    // selection quirk instead of silently returning the refreshed value.
    const int noblessType = user.GetNoblessType();
    const int hero = user.IsHero() ? 1 : 0;

    int alignment = user.GetAlign();
    if (alignment > 999999)
        alignment = 999999;

    const std::u16string characterName = std::u16string(user.GetCharName());
    const std::u16string accountName = std::u16string(user.GetAccountName());
    const std::u16string originName = std::u16string(user.GetNameOrigin());

    constexpr const char* format = "SdSddddddddddffQQddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
                                   "dddhhhhhdddffdddcdddSdddddddddc";
    return ::Assemble(
        buffer, capacity, format, characterName.c_str(), static_cast<int>(user.GetId()), accountName.c_str(),
        user.GetAccountID(), static_cast<int>(user.GetPledgeID()), user.GetBuilder(), user.GetGender(), user.GetRace(),
        user.GetMainClass(), user.GetWorld(), user.GetX(), user.GetY(), user.GetZ(), user.GetHP(), user.GetMP(),
        user.GetSP(), user.GetExp(), user.GetLevel(), alignment, static_cast<int>(user.GetPK()),
        static_cast<int>(user.GetDuel()), static_cast<int>(user.GetPKPardon()), itemTypes[0], itemTypes[1], itemTypes[2],
        itemTypes[3], itemTypes[4], itemTypes[5], itemTypes[6], itemTypes[7], itemTypes[8], itemTypes[9], itemTypes[10],
        itemTypes[11], itemTypes[12], itemTypes[13], itemTypes[14], itemTypes[15], itemTypes[16], itemTypes[17],
        itemTypes[18], itemTypes[19], itemTypes[20], itemTypes[21], itemTypes[22], itemTypes[23], itemTypes[24],
        itemTypes[25], itemTypes[26], itemTypes[27], itemTypes[28], itemTypes[29], itemTypes[30], itemTypes[31],
        itemTypes[32], itemTypes[33], itemTypes[34], itemTypes[35], itemTypes[36], itemTypes[37], itemTypes[38],
        itemTypes[39], itemTypes[40], itemTypes[41], itemTypes[42], itemTypes[43], itemTypes[44], itemTypes[45],
        itemTypes[46], itemTypes[47], itemTypes[48], itemTypes[49], itemTypes[50], itemTypes[51], itemTypes[52],
        itemTypes[53], itemTypes[54], itemTypes[55], itemTypes[56], itemTypes[57], itemTypes[58], itemTypes[59],
        appearances[0], appearances[1], appearances[2], appearances[3], appearances[4], appearances[5], appearances[6],
        appearances[7], appearances[8], armorEnchants[0], armorEnchants[1], armorEnchants[2], armorEnchants[3],
        // Current 0x0048963e..0x0048965e: +0x430, +0x42c, +0x428.
        armorEnchants[4], user.GetHairColorIndex(), user.GetHairShapeIndex(), user.GetFaceIndex(), user.GetMaxHP(),
        user.GetMaxMP(), static_cast<int>(deletionAndBan), user.GetClass(), user.GetId() == latestCharacterId ? 1 : 0,
        weaponEnchant, variationOption1, variationOption2, user.GetPreviousServer(), originName.c_str(), vitality,
        user.GetJobGroup(), static_cast<int>(vitalityData.restoreCount), freeToPlayState, user.GetFaceIndexEx(),
        user.GetHairShapeIndexEx(), user.GetHairColorIndexEx(), noblessType, hero, user.GetHairAccessoryFlag() ? 1 : 0
    );
}

struct FillAllCharactersInfo {
    FillAllCharactersInfo(
        int accountId, std::uint32_t latestCharacterId, const std::array<int, 7>& freeToPlayStates, bool& hasError
    ) :
        accountId(accountId), latestCharacterId(latestCharacterId), freeToPlayStates(freeToPlayStates), hasError(hasError) {}

    bool Do(CUser* user) {
        if (!user) {
            // Log.Add(
            //     LOG_ERROR, "[%s][%d][LoadCharacterPacket] invalid user account[%d]", "CacheSocket.cpp", 0x6e1,
            //     accountId
            // )
            // L2_LOG_ERROR(Log, "[%s][%d][LoadCharacterPacket] invalid user account[%d]", "CacheSocket.cpp", 0x6e1,
            //     accountId
            // )
            L2_LOG_ERROR(Log, "[characters] Cannot assemble selection data for account %d: null character entry. Character-list load marked failed.", accountId);
            hasError = true;
            return false;
        }

        CWareHouseSP inventory = user->GetWareHouse(WareHouseType_Inventory, true);
        if (!inventory) {
            // Log.Add(
            //     LOG_ERROR, "[%s][%d][LoadCharacterPacket] invalid warehouse account[%d]", "CacheSocket.cpp", 0x6e1,
            //     accountId
            // )
            // L2_LOG_ERROR(Log, "[%s][%d][LoadCharacterPacket] invalid warehouse account[%d]", "CacheSocket.cpp", 0x6e1,
            //     accountId
            // )
            L2_LOG_ERROR(Log, "[characters] Cannot assemble selection data for account %d: character %u inventory unavailable. Character-list load marked failed.",
                accountId, static_cast<unsigned int>(user->GetId()));
            hasError = true;
            return false;
        }

        std::optional<std::array<CItemSP, 60>> itemsStorage;
        {
            L2_READ_GUARD(*inventory.operator->());
            const std::array<int, 60> objectIds = EquipmentObjectIds(user->GetEquipment());
            auto& items = itemsStorage.emplace();
            for (std::size_t index = 0; index < items.size(); ++index) {
                items[index] = inventory->GetItem(objectIds[index]);
            }

            const int freeToPlayState = position < freeToPlayStates.size() ? freeToPlayStates[position] : 0;
            const int assembled = AssembleCharacterRecord(
                buffer.data() + length, static_cast<int>(buffer.size()) - length, *user, items, freeToPlayState,
                latestCharacterId
            );
            ++position;
            if (assembled > 0)
                length += assembled;
        }
        return true;
    }

    int accountId = 0;
    std::uint32_t latestCharacterId = 0;
    const std::array<int, 7>& freeToPlayStates;
    bool& hasError;
    std::array<char, 0x5000> buffer{};
    int length = 0;
    std::size_t position = 0;
};

// Semantic name: the original local functor name is not present in the PDB.
// Its current inline body is recovered from RequestPetiGetCharList 0x0048224c.
struct PetiCharacterListAssembler {
    bool Do(CUser* user) {
        if (!user)
            return false;

        const std::u16string characterName = std::u16string(user->GetCharName());
        const int written = ::Assemble(
            rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "Shd", characterName.c_str(),
            user->GetBuilder(), static_cast<int>(user->GetId())
        );
        if (written > 0) {
            rowBytes += written;
            ++rowCount;
        }
        return true;
    }

    std::array<char, 0x5000> rows{};
    int rowBytes = 0;
    int rowCount = 0;
};

bool ServerStopper(CSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    // The anonymous target helper reads CSocket+0x110 directly. Keep that
    // confirmed source-local access instead of adding a non-target getter.
    const auto* const socketBytes = reinterpret_cast<const std::uint8_t*>(socket);
    ::in_addr remote{};
    if (socket) {
        std::memcpy(&remote, socketBytes + 0x110, sizeof(remote));
    }
    const std::string remoteText = AddressText(remote);

    const CacheDConfiguration& config = GetCacheDConfiguration();
    if (!config.serverStopperEnabled) {
        // Log.Add(
        //     LOG_ERROR,
        //     "This server doesn't allow ServerStopper by setting, but "
        //     "someone attempted!! [IP:%s]",
        //     remoteText.c_str()
        // )
        // Log.Add(LOG_WARN,
        //     "[shutdown] Rejected request from %s: ServerStopper is disabled.",
        //     remoteText.c_str())
        L2_LOG_WARN(Log,
            "[shutdown] Rejected request from %s: ServerStopper is disabled.",
            remoteText.c_str());
        return false;
    }

    int attemptedKey = 0;
    ::Disassemble(packet, "d", &attemptedKey);
    if (!IsConfiguredAdminAddress(remoteText)) {
        // Log.Add(LOG_ERROR, "UnAuthenticated user attempted crash server!! [IP:%s]", remoteText.c_str())
        // Log.Add(LOG_WARN,
        //     "[shutdown] Rejected request from %s: the address is not in the administrative allowlist.",
        //     remoteText.c_str())
        L2_LOG_WARN(Log,
            "[shutdown] Rejected request from %s: the address is not in the administrative allowlist.",
            remoteText.c_str());
        return false;
    }

    const int expectedKey = config.serverStopperKey != 0 ? config.serverStopperKey : kDefaultServerStopperKey;
    if (attemptedKey != expectedKey) {
        // The rejection must not disclose either the configured or attempted shutdown key.
        // Log.Add(
        //     LOG_ERROR,
        //     "UnAuthenticated key used to crash server!! "
        //     "[Server Key:%d][Attempted Key:%d] [IP:%s]",
        //     expectedKey, attemptedKey, remoteText.c_str()
        // )
        // Log.Add(LOG_WARN,
        //     "[shutdown] Rejected request from %s: the shutdown key does not match.",
        //     remoteText.c_str())
        L2_LOG_WARN(Log,
            "[shutdown] Rejected request from %s: the shutdown key does not match.",
            remoteText.c_str());
        return false;
    }

    CSocket* const active = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (!active) {
        // Log.Add(LOG_NORMAL, "Server stopped by admin tool [IP:%s]", remoteText.c_str())
        // Log.Add(LOG_NORMAL,
        //     "[shutdown] Authorized request from %s: no active game-server connection. Requesting CacheD termination.",
        //     remoteText.c_str())
        L2_LOG_INFO(Log,
            "[shutdown] Authorized request from %s: no active game-server connection; requesting CacheD termination.",
            remoteText.c_str());
        // The Windows target enters its process crash/termination path here.
        // Cooperative termination is the platform transport for that same
        // authorized command.
        g_bTerminating = 1;
        return false;
    }

    if (!g_serverStopRequested.exchange(true, std::memory_order_acq_rel)) {
        // Log.Add(LOG_NORMAL, "Wait a moment. Now sending L2Server stop packet... [IP:%s]", remoteText.c_str())
        // Log.Add(LOG_NORMAL,
        //     "[shutdown] Authorized request from %s: sending a stop request to L2Server and waiting for disconnect cleanup.",
        //     remoteText.c_str())
        L2_LOG_INFO(Log,
            "[shutdown] Authorized request from %s: sending a stop request to L2Server and waiting for disconnect cleanup.",
            remoteText.c_str());
        active->Send("ch", 0xc6, 0x70);
    } else {
        // Noise: An accepted stop request is already pending. Repeated requests do not start another shutdown transition.
        // Log.Add(LOG_NORMAL, "Wait a moment. L2Server stop packet is sended. [IP:%s]", remoteText.c_str())
    }
    return false;
}

void AccumulateIncomingPacket(std::uint8_t protocol, const std::uint8_t* packet, int packetSize) {
    if (!runtime::process::IsServerToCacheDPacketStatMonitorEnabled()) {
        return;
    }

    std::size_t index = protocol;
    int measuredSize = packetSize;
    if (protocol >= 0xea) {
        if (packetSize < 3) {
            return;
        }
        const std::uint16_t extended = static_cast<std::uint16_t>(packet[1]) | (static_cast<std::uint16_t>(packet[2]) << 8U);
        index += extended;
        measuredSize -= 2;
    }
    if (index >= 695) {
        return;
    }
    ServerToCacheDPacketStat::Inst().Accumulate(static_cast<int>(index), measuredSize);
}

static bool RequestHtmlCommandHandler_RQ_MAIL_LIST_EX(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052d530.
    L2_GUARD_FUNCTION();
    ListMailHtmlCommandHandler handler(socket, packet);
    if (handler.DisassemblePacket() && handler.HandleInternal()) {
        handler.SendReply();
    }
    return false;
}

static bool RequestHtmlCommandHandler_RQ_READ_MAIL_EX(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052d668.
    L2_GUARD_FUNCTION();
    ReadMailHtmlCommandHandler handler(socket, packet);
    if (handler.DisassemblePacket() && handler.HandleInternal()) {
        handler.SendReply();
    }
    return false;
}

static bool RequestHtmlCommandHandler_RQ_SEND_MAIL_EX(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005939c4.
    L2_GUARD_FUNCTION();
    SendMailHtmlCommandHandler handler(socket, packet);
    if (handler.DisassemblePacket() && handler.HandleInternal()) {
        handler.SendReply();
    }
    return false;
}

static bool RequestHtmlCommandHandler_RQ_SAVE_MAIL_EX(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052d7a0.
    L2_GUARD_FUNCTION();
    SaveMailHtmlCommandHandler handler(socket, packet);
    if (handler.DisassemblePacket() && handler.HandleInternal()) {
        handler.SendReply();
    }
    return false;
}

static bool RequestHtmlCommandHandler_RQ_GET_TEMP_MAIL_EX(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052d8d8.
    L2_GUARD_FUNCTION();
    GetTempMailHtmlCommandHandler handler(socket, packet);
    if (handler.DisassemblePacket() && handler.HandleInternal()) {
        handler.SendReply();
    }
    return false;
}

static bool RequestHtmlCommandHandler_RQ_REPLY_MAIL_EX(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052da10.
    L2_GUARD_FUNCTION();
    ReplyMailHtmlCommandHandler handler(socket, packet);
    if (handler.DisassemblePacket() && handler.HandleInternal()) {
        handler.SendReply();
    }
    return false;
}

static bool RequestHtmlCommandHandler_RQ_FORWARD_MAIL_EX(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052db48.
    L2_GUARD_FUNCTION();
    ForwardMailHtmlCommandHandler handler(socket, packet);
    if (handler.DisassemblePacket() && handler.HandleInternal()) {
        handler.SendReply();
    }
    return false;
}

static bool RequestHtmlCommandHandler_RQ_MODIFY_TEMP_MAIL_EX(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052dc80.
    L2_GUARD_FUNCTION();
    ModifyTempMailHtmlCommandHandler handler(socket, packet);
    if (handler.DisassemblePacket() && handler.HandleInternal()) {
        handler.SendReply();
    }
    return false;
}

static bool RequestHtmlCommandHandler_RQ_MODIFY_SEND_TEMP_MAIL_EX(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00593af8.
    L2_GUARD_FUNCTION();
    ModifySendTempMailHtmlCommandHandler handler(socket, packet);
    if (handler.DisassemblePacket() && handler.HandleInternal()) {
        handler.SendReply();
    }
    return false;
}

static bool RequestHtmlCommandHandler_RQ_DELETE_MAIL_EX(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052ddac.
    L2_GUARD_FUNCTION();
    DeleteMailHtmlCommandHandler handler(socket, packet);
    if (handler.DisassemblePacket() && handler.HandleInternal()) {
        handler.SendReply();
    }
    return false;
}

static bool RequestHtmlCommandHandler_RQ_ARCHIVE_MAIL_EX(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052dee4.
    L2_GUARD_FUNCTION();
    ArchiveMailHtmlCommandHandler handler(socket, packet);
    if (handler.DisassemblePacket() && handler.HandleInternal()) {
        handler.SendReply();
    }
    return false;
}

static bool RequestHtmlCommandHandler_RQ_UNREAD_MAIL_COUNT_EX(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052e01c.
    L2_GUARD_FUNCTION();
    UnreadMailCountCommandHandler handler(socket, packet);
    if (handler.DisassemblePacket() && handler.HandleInternal()) {
        handler.SendReply();
    }
    return false;
}

static bool RequestHtmlCommandHandler_RQ_SEND_PLEDGE_MAIL_EX(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052e184.
    L2_GUARD_FUNCTION();
    SendPledgeMailHtmlCommandHandler handler(socket, packet);
    if (handler.DisassemblePacket() && handler.HandleInternal()) {
        handler.SendReply();
    }
    return false;
}

static bool RequestHtmlCommandHandler_RQ_DELETE_FRIEND_BATCH_EX(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0054a9e0.
    L2_GUARD_FUNCTION();
    DeleteFriendBatchHtmlCommandHandler handler(socket, packet);
    if (handler.DisassemblePacket() && handler.HandleInternal()) {
        handler.SendReply();
    }
    return false;
}

static bool RequestHtmlCommandHandler_RQ_DELETE_BLOCK_BATCH_EX(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x00593c2c.
    L2_GUARD_FUNCTION();
    DeleteBlockBatchHtmlCommandHandler handler(socket, packet);
    if (handler.DisassemblePacket() && handler.HandleInternal()) {
        handler.SendReply();
    }
    return false;
}

static bool RequestHtmlCommandHandler_RQ_GET_PLEDGE_ANNOUNCE_EX(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x005435ac.
    L2_GUARD_FUNCTION();
    GetPledgeAnnounceHtmlCommandHandler handler(socket, packet);
    if (handler.DisassemblePacket() && handler.HandleInternal()) {
        handler.SendReply();
    }
    return false;
}

static bool RequestHtmlCommandHandler_RQ_SET_PLEDGE_ANNOUNCE_EX(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052e2b0.
    L2_GUARD_FUNCTION();
    SetPledgeAnnounceHtmlCommandHandler handler(socket, packet);
    if (handler.DisassemblePacket() && handler.HandleInternal()) {
        handler.SendReply();
    }
    return false;
}

static bool RequestHtmlCommandHandler_RQ_SWITCH_PLEDGE_ANNOUNCE_SHOW_FLAG_EX(CSocket* socket, const unsigned char* packet) {
    // Current Cached.exe 0x0052e3dc.
    L2_GUARD_FUNCTION();
    SwitchPledgeAnnounceShowFlagHtmlCommandHandler handler(socket, packet);
    if (handler.DisassemblePacket() && handler.HandleInternal()) {
        handler.SendReply();
    }
    return false;
}

} // namespace

// Semantic carrier for the loop in current RequestPackageLoadTargetList
// 0x004826b8. The original local identifier is absent from the target guard
// material. Friendship keeps the shipping direct CUser+0x45c wYear check
// without adding a non-target public CUser accessor.
struct PackageTargetListAssembler {
    explicit PackageTargetListAssembler(int sourceCharacterId) : sourceCharacterId(sourceCharacterId) {}

    bool Do(CUser* user) {
        if (!user || static_cast<int>(user->GetId()) == sourceCharacterId || user->m_deletionTime.wYear != 0) {
            return true;
        }

        const std::u16string characterName = std::u16string(user->GetCharName());
        const int written = ::Assemble(
            rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dS", static_cast<int>(user->GetId()),
            characterName.c_str()
        );
        if (written > 0) {
            rowBytes += written;
            ++rowCount;
        }
        return true;
    }

    int sourceCharacterId = 0;
    std::array<char, 0x5000> rows{};
    int rowBytes = 0;
    int rowCount = 0;
};

int GetCharacterDeleteWaitSeconds() noexcept {
    // Current unnamed helper 0x0045b0a8. Zero selects the hard one-minute
    // development rule, -1 selects configured minutes, every other value is
    // interpreted as configured days.
    const CacheDConfiguration& configuration = GetCacheDConfiguration();
    if (configuration.waitingDateForDelete == 0)
        return 60;
    if (configuration.waitingDateForDelete == -1) {
        return configuration.waitingMinuteForDelete * 60;
    }
    return configuration.waitingDateForDelete * 86400;
}

CacheDCallbackMap gPacketTableFromServer{DummyPacket, u"RQ_PROTOCOL"};
CacheDExtendedCallbackMap gPacketTableFromServerEx{DummyServerExPacket, u"RQ_EX_PROTOCOL"};

namespace {

const bool IsRegistered_RQ_CACHE_VERSION = gPacketTableFromServer.Bind(0, CacheVersionPacket);
const bool IsRegistered_RQ_LOAD_CHARACTER = gPacketTableFromServer.Bind(1, LoadCharacterPacket);
const bool IsRegistered_RQ_CREATE_CHARACTER = gPacketTableFromServer.Bind(2, CreateCharacterPacket);
const bool IsRegistered_RQ_LOAD_SUMMONS = gPacketTableFromServer.Bind(3, RequestLoadSummons);
const bool IsRegistered_RQ_LOAD_ITEMS = gPacketTableFromServer.Bind(4, LoadItemsPacket);
const bool IsRegistered_RQ_CREATE_INIT_ITEMS = gPacketTableFromServer.Bind(5, CreateInitItems);
const bool IsRegistered_RQ_DROP_ITEM = gPacketTableFromServer.Bind(6, CSocket::DropItemPacket);
const bool IsRegistered_RQ_SAVE_CHARACTER = gPacketTableFromServer.Bind(7, SaveCharacterPacket);
const bool IsRegistered_RQ_SAVE_ITEMS = gPacketTableFromServer.Bind(8, SaveItemsPacket);
const bool IsRegistered_RQ_DELETE_CHARACTER = gPacketTableFromServer.Bind(9, DeleteCharacterPacket);
const bool IsRegistered_RQ_UPDATE_USER_PLEDGE = gPacketTableFromServer.Bind(10, RequestUpdateUserPledge);
const bool IsRegistered_RQ_DELETE_PLEDGE = gPacketTableFromServer.Bind(11, RequestDeletePledge);
const bool IsRegistered_RQ_LOAD_WAREHOUSE_ITEMS = gPacketTableFromServer.Bind(12, LoadWarehouseItemsPacket);
const bool IsRegistered_RQ_WITHDRAW_WAREHOUSE_ITEMS = gPacketTableFromServer.Bind(13, WithdrawWarehouseItemsPacket);
const bool IsRegistered_RQ_REQUEST_SHORTCUT_INFO = gPacketTableFromServer.Bind(14, RequestShortCutInfoPacket);
const bool IsRegistered_RQ_SET_SHORTCUT = gPacketTableFromServer.Bind(15, RequestSetShortCutPacket);
const bool IsRegistered_RQ_DEPOSIT_WAREHOUSE_ITEMS = gPacketTableFromServer.Bind(16, DepositWarehouseItemsPacket);
const bool IsRegistered_RQ_SELL_ITEMS = gPacketTableFromServer.Bind(17, SellItemsPacket);
const bool IsRegistered_RQ_BUY_ITEMS = gPacketTableFromServer.Bind(18, BuyItemsPacket);
const bool IsRegistered_RQ_USE_ITEM = gPacketTableFromServer.Bind(19, UseItemPacket);
const bool IsRegistered_RQ_LOAD_PLEDGE = gPacketTableFromServer.Bind(20, RequestLoadPledge);
const bool IsRegistered_RQ_CHARACTER_LOGIN = gPacketTableFromServer.Bind(21, RequestCharacterLogin);
const bool IsRegistered_RQ_CHARACTER_LOGOUT = gPacketTableFromServer.Bind(22, RequestCharacterLogout);
const bool IsRegistered_RQ_QUEST_SET = gPacketTableFromServer.Bind(23, RequestQuestSet);
const bool IsRegistered_RQ_SET_AGIT_OWNER = gPacketTableFromServer.Bind(24, RequestSetAgitOwner);
const bool IsRegistered_RQ_SET_CASTLE_OWNER = gPacketTableFromServer.Bind(25, RequestSetCastleOwner);
const bool IsRegistered_RQ_LOAD_CASTLES = gPacketTableFromServer.Bind(26, RequestLoadCastles);
const bool IsRegistered_RQ_LOAD_AGITS = gPacketTableFromServer.Bind(27, CSocket::RequestLoadAgits);
const bool IsRegistered_RQ_WRITE_CASTLE_TAX = gPacketTableFromServer.Bind(28, RequestWriteCastleTax);
const bool IsRegistered_RQ_DUMMY_29 = gPacketTableFromServer.Bind(29, DummyPacket);
const bool IsRegistered_RQ_MANIPULATE_ITEM = gPacketTableFromServer.Bind(30, RequestManipulateItem);
const bool IsRegistered_RQ_OUST_PLEDGE_MEMBER = gPacketTableFromServer.Bind(31, RequestOustPledgeMember);
const bool IsRegistered_RQ_TRADE = gPacketTableFromServer.Bind(32, RequestTrade);
const bool IsRegistered_RQ_SET_PLEDGE_CREST = gPacketTableFromServer.Bind(33, RequestSetPledgeCrest);
const bool IsRegistered_RQ_DUMMY_34 = gPacketTableFromServer.Bind(34, DummyPacket);
const bool IsRegistered_RQ_DUMMY_35 = gPacketTableFromServer.Bind(35, DummyPacket);
const bool IsRegistered_RQ_DUMMY_36 = gPacketTableFromServer.Bind(36, DummyPacket);
const bool IsRegistered_RQ_SET_USER_NICKNAME = gPacketTableFromServer.Bind(37, RequestSetUserNickname);
const bool IsRegistered_RQ_AQUIRE_SKILL = gPacketTableFromServer.Bind(38, RequestAquireSkill);
const bool IsRegistered_RQ_DESTROY_ITEM = gPacketTableFromServer.Bind(39, RequestDestroyItem);
const bool IsRegistered_RQ_ENCHANT_ITEM = gPacketTableFromServer.Bind(40, RequestEnchantItem);
const bool IsRegistered_RQ_ENCHANT_ITEM_FAIL = gPacketTableFromServer.Bind(41, RequestEnchantItemFail);
const bool IsRegistered_RQ_SET_ITEM_COUNT_LIMIT = gPacketTableFromServer.Bind(42, RequestSetItemCountLimit);
const bool IsRegistered_RQ_LOAD_SKILL = gPacketTableFromServer.Bind(43, RequestLoadSkill);
const bool IsRegistered_RQ_LOAD_FRIEND_LIST = gPacketTableFromServer.Bind(44, RequestLoadFriendList);
const bool IsRegistered_RQ_ESTABLISH_FRIENDSHIP = gPacketTableFromServer.Bind(45, RequestEstablishFriendship);
const bool IsRegistered_RQ_BREAK_FRIENDSHIP = gPacketTableFromServer.Bind(46, RequestBreakFriendship);
const bool IsRegistered_RQ_CHANGE_ITEM_DATA = gPacketTableFromServer.Bind(47, RequestChangeItemData);
const bool IsRegistered_RQ_SET_PUNISH = gPacketTableFromServer.Bind(48, SetPunishPacket);
const bool IsRegistered_RQ_GET_PUNISH = gPacketTableFromServer.Bind(49, GetPunishPacket);
const bool IsRegistered_RQ_CHANGE_POS = gPacketTableFromServer.Bind(50, ChangePosPacket);
const bool IsRegistered_RQ_GET_LOGIN_ANNOUNCE = gPacketTableFromServer.Bind(51, GetLoginAnnouncePacket);
const bool IsRegistered_RQ_SET_LOGIN_ANNOUNCE = gPacketTableFromServer.Bind(52, SetLoginAnnouncePacket);
const bool IsRegistered_RQ_DEL_LOGIN_ANNOUNCE = gPacketTableFromServer.Bind(53, DelLoginAnnouncePacket);
const bool IsRegistered_RQ_SET_BUILDER = gPacketTableFromServer.Bind(54, SetBuilderPacket);
const bool IsRegistered_RQ_LOAD_SELECTED_CHARACTER = gPacketTableFromServer.Bind(55, LoadSelectedCharacter);
const bool IsRegistered_RQ_DELETE_SKILL = gPacketTableFromServer.Bind(56, RequestDeleteSkill);
const bool IsRegistered_RQ_RESTORE_CHARACTER = gPacketTableFromServer.Bind(57, RequestRestoreCharacter);
const bool IsRegistered_RQ_SET_PLEDGE_INFO = gPacketTableFromServer.Bind(58, RequestSetPledgeInfo);
const bool IsRegistered_RQ_DUMMY_59 = gPacketTableFromServer.Bind(59, DummyPacket);
const bool IsRegistered_RQ_GET_PLEDGE_CREST = gPacketTableFromServer.Bind(60, RequestGetPledgeCrest);
const bool IsRegistered_RQ_DUMMY_61 = gPacketTableFromServer.Bind(61, DummyPacket);
const bool IsRegistered_RQ_DUMMY_62 = gPacketTableFromServer.Bind(62, DummyPacket);
const bool IsRegistered_RQ_CREATE_PLEDGE_BY_NPC = gPacketTableFromServer.Bind(63, RequestCreatePledgeByNpc);
const bool IsRegistered_RQ_DELETE_PLEDGE_BY_TIMER = gPacketTableFromServer.Bind(64, RequestDeletePledgeByTimer);
const bool IsRegistered_RQ_DUMMY_65 = gPacketTableFromServer.Bind(65, DummyPacket);
const bool IsRegistered_RQ_LOAD_DISMISS_RESERVED_PLEDGE = gPacketTableFromServer.Bind(66, RequestLoadDismissReservedPledge);
const bool IsRegistered_RQ_DUMMY_67 = gPacketTableFromServer.Bind(67, DummyPacket);
const bool IsRegistered_RQ_PETI_GET_CHAR_LIST = gPacketTableFromServer.Bind(68, RequestPetiGetCharList);
const bool IsRegistered_RQ_PETI_SAVE_MSG = gPacketTableFromServer.Bind(69, RequestPetiSaveMsg);
const bool IsRegistered_RQ_SET_USER_PLEDGE_INFO = gPacketTableFromServer.Bind(70, RequestSetUserPledgeInfo);
const bool IsRegistered_RQ_LOG_CHAR_INFO = gPacketTableFromServer.Bind(71, RequestLogCharInfo);
const bool IsRegistered_RQ_DUMMY_72 = gPacketTableFromServer.Bind(72, DummyPacket);
const bool IsRegistered_RQ_RESET_USER_NICKNAME = gPacketTableFromServer.Bind(73, RequestResetUserNickname);
const bool IsRegistered_RQ_MANAGE_BATTLE_CAMP = gPacketTableFromServer.Bind(74, RequestManageBattleCamp);
const bool IsRegistered_RQ_LOAD_BATTLE_CAMP = gPacketTableFromServer.Bind(75, RequestLoadBattleCamp);
const bool IsRegistered_RQ_PREPARE_CASTLE_WAR = gPacketTableFromServer.Bind(76, RequestPrepareCastleWar);
const bool IsRegistered_RQ_VIEW_SIEGE_LIST = gPacketTableFromServer.Bind(77, RequestViewSiegeList);
const bool IsRegistered_RQ_APPROVE_BATTLE = gPacketTableFromServer.Bind(78, RequestApproveBattle);
const bool IsRegistered_RQ_CONFIRM_CASTLE_DEFENCE = gPacketTableFromServer.Bind(79, RequestConfirmCastleDefence);
const bool IsRegistered_RQ_QUERY_CASTLE_SIEGE_FOR_REGISTER =
    gPacketTableFromServer.Bind(80, RequestQueryCastleSiegeForRegister);
const bool IsRegistered_RQ_QUERY_CASTLE_SIEGE_FOR_UNREGISTER =
    gPacketTableFromServer.Bind(81, RequestQueryCastleSiegeForUnregister);
const bool IsRegistered_RQ_QUERY_CASTLE_SIEGE_FOR_INFO = gPacketTableFromServer.Bind(82, RequestQueryCastleSiegeForInfo);
const bool IsRegistered_RQ_QUERY_CASTLE_SIEGE_FOR_DISMISS =
    gPacketTableFromServer.Bind(83, RequestQueryCastleSiegeForDismiss);
const bool IsRegistered_RQ_DELETE_PLEDGE_CREST = gPacketTableFromServer.Bind(84, RequestDelPledgeCrest);
const bool IsRegistered_RQ_MODIFY_PLEDGE_NAME = gPacketTableFromServer.Bind(85, RequestModPledgeName);
const bool IsRegistered_RQ_RESET_CASTLE_SIEGE_PLEDGE = gPacketTableFromServer.Bind(86, ResetCastleSiegePledge);
const bool IsRegistered_RQ_PARTY_TEL_BOOKMARK = gPacketTableFromServer.Bind(87, RequestPartyTelBookMarkPacket);
const bool IsRegistered_RQ_DUMMY_88 = gPacketTableFromServer.Bind(88, DummyPacket);
const bool IsRegistered_RQ_LOAD_DOOR = gPacketTableFromServer.Bind(89, RequestLoadDoor);
const bool IsRegistered_RQ_SAVE_CASTLE_STATUS = gPacketTableFromServer.Bind(90, RequestSaveCastleStatus);
const bool IsRegistered_RQ_SAVE_DOOR_STATUS = gPacketTableFromServer.Bind(91, RequestSaveDoorStatus);
const bool IsRegistered_RQ_LOAD_WAREHOUSE_ITEM_LIST = gPacketTableFromServer.Bind(92, RequestLoadWarehouseItemList);
const bool IsRegistered_RQ_CRYSTALLIZE_ITEM = gPacketTableFromServer.Bind(93, RequestCrystallizeItem);
const bool IsRegistered_RQ_SAVE_CASTLE_INCOME = gPacketTableFromServer.Bind(94, RequestSaveCastleIncome);
const bool IsRegistered_RQ_INSTALL_AGIT_DECO = gPacketTableFromServer.Bind(95, RequestInstallAgitDeco);
const bool IsRegistered_RQ_SAVE_PLEDGE_CONTRIBUTION = gPacketTableFromServer.Bind(96, RequestSavePledgeContribution);
const bool IsRegistered_RQ_GET_CONTRIBUTION_RELATED_PLEDGE =
    gPacketTableFromServer.Bind(97, RequestGetContributionRelatedPledge);
const bool IsRegistered_RQ_SET_AGIT_INFO = gPacketTableFromServer.Bind(98, RequestSetAgitInfo);
const bool IsRegistered_RQ_PRIVATE_STORE_COMMIT = gPacketTableFromServer.Bind(99, RequestPrivateStoreCommit);
const bool IsRegistered_RQ_SAVE_DIE_DROP_EXP = gPacketTableFromServer.Bind(100, RequestSaveDieDropExp);
const bool IsRegistered_RQ_RESET_CONTRIBUTION = gPacketTableFromServer.Bind(101, RequestResetContribution);
const bool IsRegistered_RQ_SAVE_OBJECT_HP = gPacketTableFromServer.Bind(102, RequestSaveObjectHp);
const bool IsRegistered_RQ_SAVE_CASTLE_SIEGE_ELAPSED_TIME =
    gPacketTableFromServer.Bind(103, RequestSaveCastleSiegeElapsedTime);
const bool IsRegistered_RQ_SAVE_SKILL_USE = gPacketTableFromServer.Bind(104, RequestSaveSkillUse);
const bool IsRegistered_RQ_LOAD_PLEDGE2 = gPacketTableFromServer.Bind(105, RequestLoadPledge2);
const bool IsRegistered_RQ_PING = gPacketTableFromServer.Bind(106, RequestPing);
const bool IsRegistered_RQ_PING_ONE_ADENA_DROP = gPacketTableFromServer.Bind(107, RequestPingOneAdenaDrop);
const bool IsRegistered_RQ_PING_NETWORK = gPacketTableFromServer.Bind(108, RequestPingNetwork);
const bool IsRegistered_RQ_SAVE_ABNORMAL_STATUS = gPacketTableFromServer.Bind(109, RequestSaveAbnormalStatus);
const bool IsRegistered_RQ_LOAD_ABNORMAL_STATUS = gPacketTableFromServer.Bind(110, RequestLoadAbnormalStatus);
const bool IsRegistered_RQ_CREATE_ALLIANCE_BY_NPC = gPacketTableFromServer.Bind(111, RequestCreateAllianceByNpc);
const bool IsRegistered_RQ_JOIN_ALLIANCE = gPacketTableFromServer.Bind(112, RequestJoinAlliance);
const bool IsRegistered_RQ_DISMISS_ALLIANCE = gPacketTableFromServer.Bind(113, RequestDismissAlliance);
const bool IsRegistered_RQ_OUST_ALLIANCE_MEMBER_PLEDGE = gPacketTableFromServer.Bind(114, RequestOustAllianceMemberPledge);
const bool IsRegistered_RQ_WITHDRAW_ALLIANCE = gPacketTableFromServer.Bind(115, RequestWithdrawAlliance);
const bool IsRegistered_RQ_CREATE_PET = gPacketTableFromServer.Bind(116, RequestCreatePet);
const bool IsRegistered_RQ_DELETE_PET = gPacketTableFromServer.Bind(117, RequestDeletePet);
const bool IsRegistered_RQ_EVOLVE_PET = gPacketTableFromServer.Bind(118, RequestEvolvePet);
const bool IsRegistered_RQ_WITHDRAW_PET = gPacketTableFromServer.Bind(119, RequestWithdrawPet);
const bool IsRegistered_RQ_LOAD_PET_ITEMS = gPacketTableFromServer.Bind(120, RequestLoadPetItems);
const bool IsRegistered_RQ_DEPOSIT_PET = gPacketTableFromServer.Bind(121, RequestDepositPet);
const bool IsRegistered_RQ_SAVE_PET = gPacketTableFromServer.Bind(122, RequestSavePet);
const bool IsRegistered_RQ_CHANGE_PET_NAME = gPacketTableFromServer.Bind(123, RequestChangePetName);
const bool IsRegistered_RQ_DISMISS_PET = gPacketTableFromServer.Bind(124, RequestDismissPet);
const bool IsRegistered_RQ_PET_GET_ITEM = gPacketTableFromServer.Bind(125, RequestPetGetItem);
const bool IsRegistered_RQ_PET_DROP_ITEM = gPacketTableFromServer.Bind(126, RequestPetDropItem);
const bool IsRegistered_RQ_GIVE_ITEM_TO_PET = gPacketTableFromServer.Bind(127, RequestGiveItemToPet);
const bool IsRegistered_RQ_GET_ITEM_FROM_PET = gPacketTableFromServer.Bind(128, RequestGetItemFromPet);
const bool IsRegistered_RQ_PET_USE_ITEM = gPacketTableFromServer.Bind(129, RequestPetUseItem);
const bool IsRegistered_RQ_SET_ALLIANCE_CREST = gPacketTableFromServer.Bind(130, RequestSetAllianceCrest);
const bool IsRegistered_RQ_GET_ALLIANCE_CREST = gPacketTableFromServer.Bind(131, RequestGetAllianceCrest);
const bool IsRegistered_RQ_UNUSED_132 = gPacketTableFromServer.Bind(132, DummyPacket);
const bool IsRegistered_RQ_LOAD_ALL_ALLIANCE = gPacketTableFromServer.Bind(133, RequestLoadAllAlliance);
const bool IsRegistered_RQ_SET_ALLIANCE_INFO = gPacketTableFromServer.Bind(134, RequestSetAllianceInfo);
const bool IsRegistered_RQ_GET_DB_RELATED_COUNTS = gPacketTableFromServer.Bind(135, RequestGetDbRelatedCounts);
const bool IsRegistered_RQ_LOAD_ALL_PLEDGE_WAR = gPacketTableFromServer.Bind(136, RequestLoadAllPledgeWar);
const bool IsRegistered_RQ_INCREASE_USER_PK_COUNT = gPacketTableFromServer.Bind(137, RequestIncreaseUserPKCount);
const bool IsRegistered_RQ_INCREASE_USER_DUEL_COUNT = gPacketTableFromServer.Bind(138, RequestIncreaseUserDuelCount);
const bool IsRegistered_RQ_RECIPE_MAKE_ITEM = gPacketTableFromServer.Bind(139, RequestRecipeMakeItem);
const bool IsRegistered_RQ_PICK_ITEM = gPacketTableFromServer.Bind(140, RequestPickItem);
const bool IsRegistered_RQ_PACKAGE_LOAD_TARGET_LIST = gPacketTableFromServer.Bind(141, RequestPackageLoadTargetList);
const bool IsRegistered_RQ_PACKAGE_SEND_TO_TARGET = gPacketTableFromServer.Bind(142, RequestPackageSendToTarget);
const bool IsRegistered_RQ_ADD_BLOCK_LIST = gPacketTableFromServer.Bind(143, RequestAddBlockList);
const bool IsRegistered_RQ_DELETE_BLOCK_LIST = gPacketTableFromServer.Bind(144, RequestDeleteBlockList);
const bool IsRegistered_RQ_LOAD_WAREHOUSE_ITEMS_EX = gPacketTableFromServer.Bind(145, RequestLoadWarehouseItemsEx);
const bool IsRegistered_RQ_WITHDRAW_WAREHOUSE_ITEMS_EX = gPacketTableFromServer.Bind(146, RequestWithdrawWarehouseItemsEx);
const bool IsRegistered_RQ_DEPOSIT_WAREHOUSE_ITEMS_EX = gPacketTableFromServer.Bind(147, RequestDepositWarehouseItemsEx);
const bool IsRegistered_RQ_SET_NEXT_CASTLE_SIEGE = gPacketTableFromServer.Bind(148, RequestSetNextCastleSiege);
const bool IsRegistered_RQ_CREATE_AGIT_AUCTION = gPacketTableFromServer.Bind(149, RequestCreateAgitAuction);
const bool IsRegistered_RQ_CREATE_AGIT_BID = gPacketTableFromServer.Bind(150, RequestCreateAgitBid);
const bool IsRegistered_RQ_SET_AGIT_AUCTION = gPacketTableFromServer.Bind(151, RequestSetAgitAuction);
const bool IsRegistered_RQ_LOAD_CASTLE_GUARD = gPacketTableFromServer.Bind(152, RequestLoadCastleGuard);
const bool IsRegistered_RQ_LOAD_AGIT_BID = gPacketTableFromServer.Bind(153, RequestLoadAgitBid);
const bool IsRegistered_RQ_LOAD_CONTROL_TOWER = gPacketTableFromServer.Bind(154, RequestLoadControlTower);
const bool IsRegistered_RQ_SAVE_CONTROL_TOWER = gPacketTableFromServer.Bind(155, RequestSaveControlTower);
const bool IsRegistered_RQ_SAVE_MERCENARY = gPacketTableFromServer.Bind(156, RequestSaveMercenary);
const bool IsRegistered_RQ_LOAD_MERCENARY = gPacketTableFromServer.Bind(157, RequestLoadMercenary);
const bool IsRegistered_RQ_DELETE_MERCENARY = gPacketTableFromServer.Bind(158, RequestDeleteMercenary);
const bool IsRegistered_RQ_UPDATE_MERCENARY = gPacketTableFromServer.Bind(159, RequestUpdateMercenary);
const bool IsRegistered_RQ_AGIT_PLEDGE_ADENA = gPacketTableFromServer.Bind(160, RequestAgitPledgeAdena);
const bool IsRegistered_RQ_CANCEL_AGIT_AUCTION = gPacketTableFromServer.Bind(161, RequestCancelAgitAuction);
const bool IsRegistered_RQ_AGIT_COST = gPacketTableFromServer.Bind(162, RequestAgitCost);
const bool IsRegistered_RQ_SHOW_INTERVAL_ANNOUNCE = gPacketTableFromServer.Bind(163, RequestShowIntervalAnnounce);
const bool IsRegistered_RQ_SET_INTERVAL_ANNOUNCE = gPacketTableFromServer.Bind(164, RequestSetIntervalAnnounce);
const bool IsRegistered_RQ_DEL_INTERVAL_ANNOUNCE = gPacketTableFromServer.Bind(165, RequestDelIntervalAnnounce);
const bool IsRegistered_RQ_DELETE_CASTLE_GUARD = gPacketTableFromServer.Bind(166, RequestDeleteCastleGuard);
const bool IsRegistered_RQ_AGIT_AUTO_SELL = gPacketTableFromServer.Bind(167, RequestAgitAutoSell);
const bool IsRegistered_RQ_AGIT_BID_CANCEL = gPacketTableFromServer.Bind(168, RequestAgitBidCancel);
const bool IsRegistered_RQ_LOAD_INTERVAL_ANNOUNCE = gPacketTableFromServer.Bind(169, RequestLoadIntervalAnnounce);
const bool IsRegistered_RQ_UNUSED_170 = gPacketTableFromServer.Bind(170, DummyPacket);
const bool IsRegistered_RQ_END_SIEGE = gPacketTableFromServer.Bind(171, RequestEndSiege);
const bool IsRegistered_RQ_PETI_SAVE_MSG_FORCE = gPacketTableFromServer.Bind(172, RequestPetiSaveMsgForce);
const bool IsRegistered_RQ_UNUSED_173 = gPacketTableFromServer.Bind(173, DummyPacket);
const bool IsRegistered_RQ_LOAD_LAST_RESIDENCE_TAX_UPDATE =
    gPacketTableFromServer.Bind(174, RequestLoadLastResidenceTaxUpdate);
const bool IsRegistered_RQ_SET_LAST_RESIDENCE_TAX_UPDATE =
    gPacketTableFromServer.Bind(175, RequestSetLastResidenceTaxUpdate);
const bool IsRegistered_RQ_MNG_COMMENT = gPacketTableFromServer.Bind(176, RequestMngComment);
const bool IsRegistered_RQ_LOAD_PLAY_TIME = gPacketTableFromServer.Bind(177, RequestLoadPlayTime);
const bool IsRegistered_RQ_BOOK_MARK = gPacketTableFromServer.Bind(178, RequestBookMark);
const bool IsRegistered_RQ_UNUSED_179 = gPacketTableFromServer.Bind(179, DummyPacket);
const bool IsRegistered_RQ_LOAD_TEAM_BATTLE_AGIT = gPacketTableFromServer.Bind(180, RequestLoadTeamBattleAgit);
const bool IsRegistered_RQ_NEW_TEAM_BATTLE_AGIT_PLEDGE = gPacketTableFromServer.Bind(181, RequestNewTeamBattleAgitPledge);
const bool IsRegistered_RQ_NEW_TEAM_BATTLE_AGIT_MEMBER = gPacketTableFromServer.Bind(182, RequestNewTeamBattleAgitMember);
const bool IsRegistered_RQ_TEAM_BATTLE_AGIT_STATUS = gPacketTableFromServer.Bind(183, RequestTeamBattleAgitStatus);
const bool IsRegistered_RQ_CHANGE_TIME_LIMIT = gPacketTableFromServer.Bind(184, RequestChangeTimeLimit);
const bool IsRegistered_RQ_SAVE_USED_TIME = gPacketTableFromServer.Bind(185, RequestSaveUsedTime);
const bool IsRegistered_RQ_UNREGISTER_TEAM_BATTLE_AGIT_PLEDGE =
    gPacketTableFromServer.Bind(186, RequestUnregisterTeamBattleAgitPledge);
const bool IsRegistered_RQ_UNREGISTER_TEAM_BATTLE_AGIT_MEMBER =
    gPacketTableFromServer.Bind(187, RequestUnregisterTeamBattleAgitMember);
const bool IsRegistered_RQ_MONRACE = gPacketTableFromServer.Bind(188, RequestMonrace);
const bool IsRegistered_RQ_LOAD_SIEGE_AGIT = gPacketTableFromServer.Bind(189, RequestLoadSiegeAgit);
const bool IsRegistered_RQ_REGISTER_SIEGE_AGIT_PLEDGE = gPacketTableFromServer.Bind(190, RequestRegisterSiegeAgitPledge);
const bool IsRegistered_RQ_UNREGISTER_SIEGE_AGIT_PLEDGE = gPacketTableFromServer.Bind(191, RequestUnregisterSiegeAgitPledge);
const bool IsRegistered_RQ_SET_TEAM_BATTLE_STATUS = gPacketTableFromServer.Bind(192, RequstSetTeamBattleStatus);
const bool IsRegistered_RQ_SET_TEAM_BATTLE_WINNER = gPacketTableFromServer.Bind(193, RequestSetTeamBattleWinner);
const bool IsRegistered_RQ_USE_RECIPE_ITEM = gPacketTableFromServer.Bind(194, RequestUseRecipeItem);
const bool IsRegistered_RQ_SAVE_RECIPE_BOOK_INFO = gPacketTableFromServer.Bind(195, RequestSaveRecipeBookInfo);
const bool IsRegistered_RQ_DELETE_RECIPE_BOOK_INFO = gPacketTableFromServer.Bind(196, RequestDeleteRecipeBookInfo);
const bool IsRegistered_RQ_LOAD_RECIPE_BOOK_INFO = gPacketTableFromServer.Bind(197, RequestLoadRecipeBookInfo);
const bool IsRegistered_RQ_RECIPE_SHOP_MAKE_ITEM = gPacketTableFromServer.Bind(198, RequestRecipeShopMakeItem);
const bool IsRegistered_RQ_UPDATE_SOCIALITY = gPacketTableFromServer.Bind(199, RequestUpdateSociality);
const bool IsRegistered_RQ_SET_TEAM_BATTLE_NPC_TYPE = gPacketTableFromServer.Bind(200, RequestSetTeamBattleNpcType);
const bool IsRegistered_RQ_RESET_SIEGE_AGIT_PLEDGE = gPacketTableFromServer.Bind(201, RequestResetSiegeAgitPledge);
const bool IsRegistered_RQ_RESET_TEAM_BATTLE_AGIT_PLEDGE =
    gPacketTableFromServer.Bind(202, RequestResetTeamBattleAgitPledge);
const bool IsRegistered_RQ_EQUIP_HENNA = gPacketTableFromServer.Bind(203, RequestEquipHenna);
const bool IsRegistered_RQ_UNEQUIP_HENNA = gPacketTableFromServer.Bind(204, RequestUnequipHenna);
const bool IsRegistered_RQ_MANAGE_MACRO = gPacketTableFromServer.Bind(205, RequestManageMacro);
const bool IsRegistered_RQ_MANAGE_MEMO = gPacketTableFromServer.Bind(206, RequestManageMemo);
const bool IsRegistered_RQ_MAN_PLEDGE_BBS = gPacketTableFromServer.Bind(207, RequestManPledgeBBS);
const bool IsRegistered_RQ_MANOR_BUY_SEED = gPacketTableFromServer.Bind(208, RequestManorBuySeed);
const bool IsRegistered_RQ_MANOR_SELL_CROP = gPacketTableFromServer.Bind(209, RequestManorSellCrop);
const bool IsRegistered_RQ_MANOR_MOVE_CROP = gPacketTableFromServer.Bind(210, RequestManorMoveCrop);
const bool IsRegistered_RQ_SAVE_MANOR_NEXT_SEED = gPacketTableFromServer.Bind(211, RequestSaveManorNextSeed);
const bool IsRegistered_RQ_SAVE_MANOR_NOW_SEED = gPacketTableFromServer.Bind(212, RequestSaveManorNowSeed);
const bool IsRegistered_RQ_SAVE_MANOR_INFO = gPacketTableFromServer.Bind(213, RequestSaveManorInfo);
const bool IsRegistered_RQ_LOAD_ALL_MANOR = gPacketTableFromServer.Bind(214, RequestLoadAllManor);
const bool IsRegistered_RQ_LOAD_EVENT_POINT = gPacketTableFromServer.Bind(215, RequestLoadEventPoint);
const bool IsRegistered_RQ_SAVE_EVENT_POINT = gPacketTableFromServer.Bind(216, RequestSaveEventPoint);
const bool IsRegistered_RQ_BBS_CLAN_ACTION = gPacketTableFromServer.Bind(217, RequestBBSClanAction);
const bool IsRegistered_RQ_MOD_ALLIANCE_NAME = gPacketTableFromServer.Bind(218, RequestModAllianceName);
const bool IsRegistered_RQ_UNUSED_219 = gPacketTableFromServer.Bind(219, DummyPacket);
const bool IsRegistered_RQ_CREATE_SUBJOB = gPacketTableFromServer.Bind(220, RequestCreateSubJob);
const bool IsRegistered_RQ_DELETE_SUBJOB = gPacketTableFromServer.Bind(221, RequestDeleteSubJob);
const bool IsRegistered_RQ_CHANGE_SUBJOB = gPacketTableFromServer.Bind(222, RequestChangeSubJob);
const bool IsRegistered_RQ_LOAD_SUBJOB = gPacketTableFromServer.Bind(223, RequestLoadSubJob);
const bool IsRegistered_RQ_EVENT_AMOUNT = gPacketTableFromServer.Bind(224, EventAmountPacket);
const bool IsRegistered_RQ_SAVE_CHARACTER_FOR_SUBJOB = gPacketTableFromServer.Bind(225, RequestSaveCharacterForSubJob);
const bool IsRegistered_RQ_RELOAD_SKILL = gPacketTableFromServer.Bind(226, RequestReloadSkill);
const bool IsRegistered_RQ_SAVE_ACQUIRED_SKILLS = gPacketTableFromServer.Bind(227, RequestSaveAcquiredSkills);
const bool IsRegistered_RQ_DX_PACKET = gPacketTableFromServer.Bind(228, RequestDxPacket);
const bool IsRegistered_RQ_QUERY_PACKET = gPacketTableFromServer.Bind(229, RequestQueryPacket);
const bool IsRegistered_RQ_SAVE_PUNISHMENT_HISTORY = gPacketTableFromServer.Bind(230, RequestSavePunishmentHistory);
const bool IsRegistered_RQ_LOAD_SUPEND_PLEDGE = gPacketTableFromServer.Bind(231, RequestLoadSupendPledge);
const bool IsRegistered_RQ_LOAD_MAIN_CLASS_SKILL = gPacketTableFromServer.Bind(232, RequestLoadMainClassSkill);
const bool IsRegistered_RQ_DELETE_PLEDGE_SKILL = gPacketTableFromServer.Bind(233, RequestDeletePledgeSkill);
const bool IsRegistered_RQ_EX_PACKET = gPacketTableFromServer.Bind(0xea, RequestExPacket);
const bool IsRegistered_RQ_EX_LOAD_SSQ_INFO = gPacketTableFromServerEx.Bind(0, LoadSevenSignQuestInfo);
const bool IsRegistered_RQ_EX_MAIL_LIST = gPacketTableFromServerEx.Bind(1, RequestHtmlCommandHandler_RQ_MAIL_LIST_EX);
const bool IsRegistered_RQ_EX_READ_MAIL = gPacketTableFromServerEx.Bind(2, RequestHtmlCommandHandler_RQ_READ_MAIL_EX);
const bool IsRegistered_RQ_EX_LOAD_TIME_ATTACK_INFO = gPacketTableFromServerEx.Bind(3, LoadTimeAttackInfo);
const bool IsRegistered_RQ_EX_SEND_MAIL = gPacketTableFromServerEx.Bind(4, RequestHtmlCommandHandler_RQ_SEND_MAIL_EX);
const bool IsRegistered_RQ_EX_SAVE_MAIL = gPacketTableFromServerEx.Bind(5, RequestHtmlCommandHandler_RQ_SAVE_MAIL_EX);
const bool IsRegistered_RQ_EX_SET_DAWN_ROUND_TO_PLEDGE = gPacketTableFromServerEx.Bind(6, SetDawnRoundToPledge);
const bool IsRegistered_RQ_EX_START_SSQ_EVENT_EX = gPacketTableFromServerEx.Bind(7, StartSevenSignEvent);
const bool IsRegistered_RQ_EX_INITIALIZE_TIME_ATTACK_DB = gPacketTableFromServerEx.Bind(8, InitializeTimeAttackDB);
const bool IsRegistered_RQ_EX_GET_TEMP_MAIL =
    gPacketTableFromServerEx.Bind(9, RequestHtmlCommandHandler_RQ_GET_TEMP_MAIL_EX);
const bool IsRegistered_RQ_EX_REPLY_MAIL = gPacketTableFromServerEx.Bind(10, RequestHtmlCommandHandler_RQ_REPLY_MAIL_EX);
const bool IsRegistered_RQ_EX_FORWARD_MAIL = gPacketTableFromServerEx.Bind(11, RequestHtmlCommandHandler_RQ_FORWARD_MAIL_EX);
const bool IsRegistered_RQ_EX_MODIFY_TEMP_MAIL =
    gPacketTableFromServerEx.Bind(12, RequestHtmlCommandHandler_RQ_MODIFY_TEMP_MAIL_EX);
const bool IsRegistered_RQ_EX_MODIFY_SEND_TEMP_MAIL =
    gPacketTableFromServerEx.Bind(13, RequestHtmlCommandHandler_RQ_MODIFY_SEND_TEMP_MAIL_EX);
const bool IsRegistered_RQ_EX_DELETE_MAIL = gPacketTableFromServerEx.Bind(14, RequestHtmlCommandHandler_RQ_DELETE_MAIL_EX);
const bool IsRegistered_RQ_EX_ARCHIVE_MAIL = gPacketTableFromServerEx.Bind(15, RequestHtmlCommandHandler_RQ_ARCHIVE_MAIL_EX);
const bool IsRegistered_RQ_EX_CHANGE_SSQ_DROP_GUARD_STATUS = gPacketTableFromServerEx.Bind(16, ChangeSSQDropGuardStatus);
const bool IsRegistered_RQ_EX_UNREAD_MAIL_COUNT =
    gPacketTableFromServerEx.Bind(17, RequestHtmlCommandHandler_RQ_UNREAD_MAIL_COUNT_EX);
const bool IsRegistered_RQ_EX_SEND_PLEDGE_MAIL =
    gPacketTableFromServerEx.Bind(18, RequestHtmlCommandHandler_RQ_SEND_PLEDGE_MAIL_EX);
const bool IsRegistered_RQ_EX_DELETE_FRIEND_BATCH =
    gPacketTableFromServerEx.Bind(19, RequestHtmlCommandHandler_RQ_DELETE_FRIEND_BATCH_EX);
const bool IsRegistered_RQ_EX_DELETE_BLOCK_BATCH =
    gPacketTableFromServerEx.Bind(20, RequestHtmlCommandHandler_RQ_DELETE_BLOCK_BATCH_EX);
const bool IsRegistered_RQ_EX_LOAD_MARKET_PRICE_LIST = gPacketTableFromServerEx.Bind(21, LoadMarketPriceList);
const bool IsRegistered_RQ_EX_RENEW_SUBJOB = gPacketTableFromServerEx.Bind(22, RenewSubJob);
const bool IsRegistered_RQ_EX_LOAD_ALL_NOBLESS = gPacketTableFromServerEx.Bind(23, LoadAllNobless);
const bool IsRegistered_RQ_EX_LOAD_NOBLESS = gPacketTableFromServerEx.Bind(24, LoadNobless);
const bool IsRegistered_RQ_EX_SET_PLEDGE_EMBLEM = gPacketTableFromServerEx.Bind(25, RequestSetPledgeEmblem);
const bool IsRegistered_RQ_EX_GET_PLEDGE_EMBLEM = gPacketTableFromServerEx.Bind(26, RequestGetPledgeEmblem);
const bool IsRegistered_RQ_EX_DEL_PLEDGE_EMBLEM = gPacketTableFromServerEx.Bind(27, RequestDelPledgeEmblem);
const bool IsRegistered_RQ_EX_GET_NOBLESS_TOP10 = gPacketTableFromServerEx.Bind(28, GetNoblessTop10);
const bool IsRegistered_RQ_EX_GET_NOBLESS_ACHIEVEMENTS = gPacketTableFromServerEx.Bind(29, GetNoblessAchievements);
const bool IsRegistered_RQ_EX_GET_PLEDGE_ANNOUNCE =
    gPacketTableFromServerEx.Bind(30, RequestHtmlCommandHandler_RQ_GET_PLEDGE_ANNOUNCE_EX);
const bool IsRegistered_RQ_EX_SET_PLEDGE_ANNOUNCE =
    gPacketTableFromServerEx.Bind(31, RequestHtmlCommandHandler_RQ_SET_PLEDGE_ANNOUNCE_EX);
const bool IsRegistered_RQ_EX_SWITCH_PLEDGE_ANNOUNCE_SHOW_FLAG =
    gPacketTableFromServerEx.Bind(32, RequestHtmlCommandHandler_RQ_SWITCH_PLEDGE_ANNOUNCE_SHOW_FLAG_EX);
const bool IsRegistered_RQ_EX_LOAD_PLEDGE_SIEGE_KILL_DEATH_COUNT =
    gPacketTableFromServerEx.Bind(33, RequestLoadPledgeSiegeKillDeathCount);
const bool IsRegistered_RQ_EX_LOAD_PCCAFE_POINT = gPacketTableFromServerEx.Bind(34, RequestLoadPCCafePoint);
const bool IsRegistered_RQ_EX_SAVE_PCCAFE_POINT = gPacketTableFromServerEx.Bind(35, RequestSavePCCafePoint);
const bool IsRegistered_RQ_EX_MANIPULATE_ITEM_WITH_POINT = gPacketTableFromServerEx.Bind(36, RequestManipulateItemWithPoint);
const bool IsRegistered_RQ_EX_NPC_PICK_ITEM = gPacketTableFromServerEx.Bind(37, RequestNPCPickItem);
const bool IsRegistered_RQ_EX_FISHING_EVENT_FINISH = gPacketTableFromServerEx.Bind(38, RequestFishingEventFinish);
const bool IsRegistered_RQ_EX_FISHING_EVENT_REGISTER = gPacketTableFromServerEx.Bind(39, RequestFishingEventRegister);
const bool IsRegistered_RQ_EX_FISHING_EVENT_SAVE_PRIZE = gPacketTableFromServerEx.Bind(40, RequestFishingEventSavePrize);
const bool IsRegistered_RQ_EX_UPDATE_BOSS_RECORD = gPacketTableFromServerEx.Bind(41, RequestUpdateBossRecord);
const bool IsRegistered_RQ_EX_LOAD_BOSS_RECORD = gPacketTableFromServerEx.Bind(42, RequestLoadBossRecord);
const bool IsRegistered_RQ_EX_CREATE_ACADEMY = gPacketTableFromServerEx.Bind(43, RequestCreateAcademy);
const bool IsRegistered_RQ_EX_LOAD_BOSS_RECORD_RANKING = gPacketTableFromServerEx.Bind(44, RequestLoadBossRecordRanking);
const bool IsRegistered_RQ_EX_LOAD_BOSS_RECORD_ROUND = gPacketTableFromServerEx.Bind(45, RequestLoadBossRecordRound);
const bool IsRegistered_RQ_EX_MOVE_TO_NEXT_BOSS_RECORD_ROUND =
    gPacketTableFromServerEx.Bind(46, RequestMoveToNextBossRecordRound);
const bool IsRegistered_RQ_EX_LOAD_MINIGAME_AGIT =
    gPacketTableFromServerEx.Bind(47, CMiniGameAgitDB::RequestLoadMiniGameAgit);
const bool IsRegistered_RQ_EX_SET_MINIGAME_AGIT_STATUS =
    gPacketTableFromServerEx.Bind(48, CMiniGameAgitDB::RequestSetMiniGameAgitStatus);
const bool IsRegistered_RQ_EX_ADD_MINIGAME_AGIT_PLEDGE =
    gPacketTableFromServerEx.Bind(49, CMiniGameAgitDB::RequestAddMiniGameAgitPledge);
const bool IsRegistered_RQ_EX_DEL_MINIGAME_AGIT_PLEDGE =
    gPacketTableFromServerEx.Bind(50, CMiniGameAgitDB::RequestDelMiniGameAgitPledge);
const bool IsRegistered_RQ_EX_ADD_MINIGAME_AGIT_MEMBER =
    gPacketTableFromServerEx.Bind(51, CMiniGameAgitDB::RequestAddMiniGameAgitMember);
const bool IsRegistered_RQ_EX_RESET_MINIGAME_AGIT =
    gPacketTableFromServerEx.Bind(52, CMiniGameAgitDB::RequestResetMiniGameAgit);
const bool IsRegistered_RQ_EX_UPDATE_PLEDGE_NAME_VALUE = gPacketTableFromServerEx.Bind(53, RequestUpdatePledgeNameValue);
const bool IsRegistered_RQ_EX_LOAD_SUB_PLEDGE = gPacketTableFromServerEx.Bind(54, RequestLoadSubPledge);
const bool IsRegistered_RQ_EX_LOAD_PLEDGE_SKILL = gPacketTableFromServerEx.Bind(55, RequestLoadPledgeSkill);
const bool IsRegistered_RQ_EX_LOAD_CURSED_WEAPON_LIST =
    gPacketTableFromServerEx.Bind(56, CCursedWeaponDB::RequestLoadCursedWeaponList);
const bool IsRegistered_RQ_EX_ADD_CURSED_WEAPON = gPacketTableFromServerEx.Bind(57, CCursedWeaponDB::RequestAddCursedWeapon);
const bool IsRegistered_RQ_EX_UPDATE_CURSED_WEAPON =
    gPacketTableFromServerEx.Bind(58, CCursedWeaponDB::RequestUpdateCursedWeapon);
const bool IsRegistered_RQ_EX_DEL_CURSED_WEAPON = gPacketTableFromServerEx.Bind(59, CCursedWeaponDB::RequestDelCursedWeapon);
const bool IsRegistered_RQ_EX_DEL_CURSED_WEAPON_ONLY_IN_DB =
    gPacketTableFromServerEx.Bind(60, CCursedWeaponDB::RequestDelCursedWeaponOnlyInDB);
const bool IsRegistered_RQ_EX_PICK_CURSED_WEAPON =
    gPacketTableFromServerEx.Bind(84, CCursedWeaponDB::RequestPickCursedWeapon);
const bool IsRegistered_RQ_EX_LOAD_PLEDGE_RANKING = gPacketTableFromServerEx.Bind(61, CPledgeRanking::SendRankingList);
const bool IsRegistered_RQ_EX_SET_PLEDGE_POWER_GRADE = gPacketTableFromServerEx.Bind(62, RequestSetPledgePowerGrade);
const bool IsRegistered_RQ_EX_CREATE_SUB_PLEDGE = gPacketTableFromServerEx.Bind(63, RequestCreateSubPledge);
const bool IsRegistered_RQ_EX_SAVE_PLEDGE_SKILL = gPacketTableFromServerEx.Bind(64, RequestSavePledgeSkill);
const bool IsRegistered_RQ_EX_SAVE_SUB_PLEDGE_SKILL = gPacketTableFromServerEx.Bind(65, RequestSaveSubPledgeSkill);
const bool IsRegistered_RQ_EX_DEL_PLEDGE_SKILL = gPacketTableFromServerEx.Bind(66, RequestDelPledgeSkill);
const bool IsRegistered_RQ_EX_DEL_SUB_PLEDGE_SKILL = gPacketTableFromServerEx.Bind(67, RequestDelSubPledgeSkill);
const bool IsRegistered_RQ_EX_SAVE_PLEDGE_POWER = gPacketTableFromServerEx.Bind(68, RequestSavePledgePower);
const bool IsRegistered_RQ_EX_UPDATE_SUB_PLEDGE_MASTER = gPacketTableFromServerEx.Bind(69, RequestUpdateSubPledgeMaster);
const bool IsRegistered_RQ_EX_UPGRADE_SUB_PLEDGE_MEMBER_COUNT =
    gPacketTableFromServerEx.Bind(70, RequestUpgradeSubPledgeMemberCount);
const bool IsRegistered_RQ_EX_INITIALIZE_BOSS_RECORD = gPacketTableFromServerEx.Bind(71, RequestInitializeBossRecord);
const bool IsRegistered_RQ_EX_GRADUATE_ACADEMY = gPacketTableFromServerEx.Bind(72, RequestGraduateAcademy);
const bool IsRegistered_RQ_EX_SET_ACADEMY_MASTER = gPacketTableFromServerEx.Bind(73, RequestSetAcademyMaster);
const bool IsRegistered_RQ_EX_PLEDGE_MASTER_TRANSFER = gPacketTableFromServerEx.Bind(74, RequestPledgeMasterTransfer);
const bool IsRegistered_RQ_EX_PLEDGE_MASTER_TRANSFER_CANCEL =
    gPacketTableFromServerEx.Bind(75, RequestPledgeMasterTransferCancel);
const bool IsRegistered_RQ_EX_UPDATE_PLEDGE_NAME_VALUE_BY_USER =
    gPacketTableFromServerEx.Bind(76, RequestUpdatePledgeNameValueByUser);
const bool IsRegistered_RQ_EX_PCCAFE_COUPON_CHECK = gPacketTableFromServerEx.Bind(77, RequestPCCafeCouponCheck);
const bool IsRegistered_RQ_EX_PCCAFE_COUPON_USED = gPacketTableFromServerEx.Bind(78, RequestPCCafeCouponUsed);
const bool IsRegistered_RQ_EX_RENAME_SUBPLEDGE = gPacketTableFromServerEx.Bind(79, RequestRenameSubpledge);
const bool IsRegistered_RQ_EX_LOAD_TRANSFERRED_PLEDGE_MASTER =
    gPacketTableFromServerEx.Bind(80, RequestLoadTransferredPledgeMaster);
const bool IsRegistered_RQ_EX_SAVE_CLANCOMM_TRANSFER_RESULT =
    gPacketTableFromServerEx.Bind(81, RequestSaveClancommTransferResult);
const bool IsRegistered_RQ_EX_SET_PREVIOUS_CASTLE_OWNER_ID =
    gPacketTableFromServerEx.Bind(82, RequestSetPreviousCastleOwnerID);
const bool IsRegistered_RQ_EX_ACADEMY_GRADUATE_LIST = gPacketTableFromServerEx.Bind(83, RequestAcademyGraduateList);
const bool IsRegistered_RQ_EX_REORGRANIZE_PLEDGE_MEMBER = gPacketTableFromServerEx.Bind(85, RequestReorgranizePledgeMember);
const bool IsRegistered_RQ_EX_OLYMPIAD_PENALTY_LOG = gPacketTableFromServerEx.Bind(86, RequestOlympiadPenaltyLog);
const bool IsRegistered_RQ_EX_LOAD_SHARED_REUSE_DELAYS_OF_ITEMS =
    gPacketTableFromServerEx.Bind(87, RequestLoadSharedReuseDelaysOfItems);
const bool IsRegistered_RQ_EX_SAVE_SHARED_REUSE_DELAY_OF_ITEM =
    gPacketTableFromServerEx.Bind(88, RequestSaveSharedReuseDelayOfItem);
const bool IsRegistered_RQ_EX_VARIATION_ITEM = gPacketTableFromServerEx.Bind(89, RequestVariationItem);
const bool IsRegistered_RQ_EX_CANCEL_VARIATION_ITEM = gPacketTableFromServerEx.Bind(90, RequestCancelVariationItem);
const bool IsRegistered_RQ_EX_UPDATE_ITEM_DURATION = gPacketTableFromServerEx.Bind(91, RequestUpdateItemDuration);
const bool IsRegistered_RQ_EX_SAVE_DUEL_RECORD = gPacketTableFromServerEx.Bind(92, RequestSaveDuelRecord);
const bool IsRegistered_RQ_EX_LOAD_DUEL_RECORD = gPacketTableFromServerEx.Bind(93, RequestLoadDuelRecord);
const bool IsRegistered_RQ_EX_LARGE_PACKET = gPacketTableFromServerEx.Bind(94, RequestLargePacket);
const bool IsRegistered_RQ_EX_GET_HERO_MATCH_RESULT = gPacketTableFromServerEx.Bind(95, RequestGetHeroMatchResult);
const bool IsRegistered_RQ_EX_SAVE_FIELD_CYCLE_INFO = gPacketTableFromServerEx.Bind(96, RequestSaveFieldCycleInfo);
const bool IsRegistered_RQ_EX_LOAD_FIELD_CYCLE_INFO = gPacketTableFromServerEx.Bind(97, RequestLoadFieldCycleInfo);
const bool IsRegistered_RQ_EX_SAVE_CHARACTER_SLOT_EX = gPacketTableFromServerEx.Bind(98, RequestSaveCharacterSlotEx);
const bool IsRegistered_RQ_EX_LOAD_ALL_FORTRESS = gPacketTableFromServerEx.Bind(99, RequestLoadAllFortress);
const bool IsRegistered_RQ_EX_LOAD_FORTRESS_SIEGE = gPacketTableFromServerEx.Bind(100, RequestLoadFortressSiege);
const bool IsRegistered_RQ_EX_LOAD_FORTRESS_SIEGE_REGISTRY =
    gPacketTableFromServerEx.Bind(101, RequestLoadFortressSiegeRegistry);
const bool IsRegistered_RQ_EX_UPDATE_FORTRESS_OWNER = gPacketTableFromServerEx.Bind(102, RequestUpdateFortressOwner);
const bool IsRegistered_RQ_EX_PAY_FORTRESS_RENT = gPacketTableFromServerEx.Bind(103, RequestPayFortressRent);
const bool IsRegistered_RQ_EX_SAVE_FORTRESS_STATE = gPacketTableFromServerEx.Bind(104, RequestSaveFortressState);
const bool IsRegistered_RQ_EX_SAVE_FORTRESS_SIEGE_STATE = gPacketTableFromServerEx.Bind(105, RequestSaveFortressSiegeState);
const bool IsRegistered_RQ_EX_SAVE_FORTRESS_CONTRACT_INFO =
    gPacketTableFromServerEx.Bind(106, RequestSaveFortressContractInfo);
const bool IsRegistered_RQ_EX_SAVE_FORTRESS_REWARD_INFO = gPacketTableFromServerEx.Bind(107, RequestSaveFortressRewardInfo);
const bool IsRegistered_RQ_EX_SAVE_FORTRESS_SIEGE_REGISTRY =
    gPacketTableFromServerEx.Bind(108, RequestSaveFortressSiegeRegistry);
const bool IsRegistered_RQ_EX_SAVE_FORTRESS_SIEGE_REGISTER_PLEDGE =
    gPacketTableFromServerEx.Bind(109, RequestSaveFortressSiegeRegisterPledge);
const bool IsRegistered_RQ_EX_LOAD_ALL_AUCTION = gPacketTableFromServerEx.Bind(110, RequestLoadAllAuction);
const bool IsRegistered_RQ_EX_LOAD_ALL_AUCTION_BID = gPacketTableFromServerEx.Bind(111, RequestLoadAllAuctionBid);
const bool IsRegistered_DUMMY_SERVER_EX_PACKET_112 = gPacketTableFromServerEx.Bind(112, DummyServerExPacket);
const bool IsRegistered_RQ_EX_CREATE_ITEM_AUCTION = gPacketTableFromServerEx.Bind(113, RequestCreateItemAuction);
const bool IsRegistered_RQ_EX_UPDATE_ITEM_AUCTION_INFO_FOR_L2ASSIST =
    gPacketTableFromServerEx.Bind(114, Request_RQ_EX_UPDATE_ITEM_AUCTION_INFO_FOR_L2ASSIST);
const bool IsRegistered_RQ_EX_SET_ITEM_AUCTION_END_TIME = gPacketTableFromServerEx.Bind(115, RequestSetItemAuctionEndTime);
const bool IsRegistered_RQ_EX_SET_ITEM_AUCTION_STATUS = gPacketTableFromServerEx.Bind(116, RequestSetItemAuctionStatus);
const bool IsRegistered_RQ_EX_SET_ITEM_AUCTION_INFO = gPacketTableFromServerEx.Bind(117, RequestSetItemAuctionInfo);
const bool IsRegistered_RQ_EX_MANIPULATE_WAREHOUSE_ITEM = gPacketTableFromServerEx.Bind(118, RequestManipulateWarehouseItem);
const bool IsRegistered_RQ_EX_CONVERT_ITEM = gPacketTableFromServerEx.Bind(119, RequestConvertItem);
const bool IsRegistered_RQ_EX_CHANGE_MERGED_NAME = gPacketTableFromServerEx.Bind(120, RequestChangeMergedName);
const bool IsRegistered_RQ_EX_SET_TUTORIAL = gPacketTableFromServerEx.Bind(121, RequestSetTutorial);
const bool IsRegistered_RQ_EX_TUTORIAL_LIST = gPacketTableFromServerEx.Bind(122, RequestTutorialList);
const bool IsRegistered_RQ_EX_LOAD_IN_ZONE_RESTRICTION = gPacketTableFromServerEx.Bind(123, RequestLoadInZoneRestriction);
const bool IsRegistered_RQ_EX_MARK_IN_ZONE_RESTRICTION = gPacketTableFromServerEx.Bind(124, RequestMarkInZoneRestriction);
const bool IsRegistered_RQ_EX_LOAD_UI = gPacketTableFromServerEx.Bind(125, RequestLoadUI);
const bool IsRegistered_RQ_EX_SAVE_UI = gPacketTableFromServerEx.Bind(126, RequestSaveUI);
const bool IsRegistered_RQ_EX_RESET_IN_ZONE_RESTRICTION = gPacketTableFromServerEx.Bind(127, RequestResetInZoneRestriction);
const bool IsRegistered_RQ_EX_SAVE_INVENTORY_SLOT_INDEX = gPacketTableFromServerEx.Bind(128, RequestSaveInventorySlotIndex);
const bool IsRegistered_RQ_EX_SET_CASTLE_SIEGE_DEFENCE_COUNT =
    gPacketTableFromServerEx.Bind(129, RequestSetCastleSiegeDefenceCount);
const bool IsRegistered_RQ_EX_SET_ASSOCIATED_IN_ZONE = gPacketTableFromServerEx.Bind(130, RequestSetAssociatedInZone);
const bool IsRegistered_RQ_EX_WAITING_TRANSFER_PLEDGE_MASTER =
    gPacketTableFromServerEx.Bind(131, RequestWaitingTransferPledgeMaster);
const bool IsRegistered_RQ_EX_DELETE_SKILL_LIST = gPacketTableFromServerEx.Bind(132, RequestDeleteSkillList);
const bool IsRegistered_RQ_EX_SAVE_USER_POINT = gPacketTableFromServerEx.Bind(133, RequestSaveUserPoint);
const bool IsRegistered_RQ_EX_LOAD_USER_POINT = gPacketTableFromServerEx.Bind(134, RequestLoadUserPoint);
const bool IsRegistered_RQ_EX_SAVE_PVP_POINT_RESTRAIN = gPacketTableFromServerEx.Bind(135, RequestSavePVPPointRestrain);
const bool IsRegistered_RQ_EX_LOAD_PVP_POINT_RESTRAIN = gPacketTableFromServerEx.Bind(136, RequestLoadPVPPointRestrain);
const bool IsRegistered_RQ_EX_SAVE_LAST_COLOR = gPacketTableFromServerEx.Bind(137, RequestSaveLastColor);
const bool IsRegistered_RQ_EX_LOAD_LAST_COLOR = gPacketTableFromServerEx.Bind(138, RequestLoadLastColor);
const bool IsRegistered_RQ_EX_SAVE_NR_MEMO = gPacketTableFromServerEx.Bind(139, RequestSaveNRMemo);
const bool IsRegistered_RQ_EX_LOAD_NR_MEMO = gPacketTableFromServerEx.Bind(140, RequestLoadNRMemo);
const bool IsRegistered_RQ_EX_DELETE_NR_MEMO = gPacketTableFromServerEx.Bind(141, RequestDeleteNRMemo);
const bool IsRegistered_RQ_EX_SAVE_PREMIUM_ITEM_FOR_CHAR = gPacketTableFromServerEx.Bind(142, RequestSavePremiumItemForChar);
const bool IsRegistered_RQ_EX_SAVE_PREMIUM_ITEM_FOR_SERVER =
    gPacketTableFromServerEx.Bind(143, RequestSavePremiumItemForServer);
const bool IsRegistered_RQ_EX_LOAD_PREMIUM_ITEM = gPacketTableFromServerEx.Bind(144, RequestLoadPremiumItem);
const bool IsRegistered_RQ_EX_USER_BOOKMARK_JOB = gPacketTableFromServerEx.Bind(145, RequestUserBookmarkJob);
const bool IsRegistered_RQ_EX_PERIODIC_ITEM_JOB = gPacketTableFromServerEx.Bind(146, RequestPeriodicItemJob);
const bool IsRegistered_RQ_EX_MANIPULATE_PREMIUM_ITEMS = gPacketTableFromServerEx.Bind(147, RequestManipulatePremiumItems);
const bool IsRegistered_RQ_EX_SET_DELETED_IN_IBSERVER = gPacketTableFromServerEx.Bind(148, RequestSetDeletedInIBServer);
const bool IsRegistered_RQ_EX_DELETE_PREMIUM_ITEM = gPacketTableFromServerEx.Bind(149, RequestDeletePremiumItem);
const bool IsRegistered_RQ_EX_SET_RIM_POINT = gPacketTableFromServerEx.Bind(150, RequestSetRimPoint);
const bool IsRegistered_RQ_EX_GET_RIM_POINT_RANK = gPacketTableFromServerEx.Bind(151, GetRimPointRank);
const bool IsRegistered_RQ_EX_SET_BOT_REPORT = gPacketTableFromServerEx.Bind(152, RequestSetBotReport);
const bool IsRegistered_RQ_EX_SAVE_BOT_REPORT = gPacketTableFromServerEx.Bind(153, RequestSaveBotReport);
const bool IsRegistered_RQ_EX_LOAD_BOT_REPORT = gPacketTableFromServerEx.Bind(154, RequestLoadBotReport);
const bool IsRegistered_RQ_EX_LOAD_BOT_REPORT_TOP_TEN = gPacketTableFromServerEx.Bind(155, RequestLoadBotReportTopTen);
const bool IsRegistered_RQ_EX_RESET_BOT_REPORT = gPacketTableFromServerEx.Bind(156, RequestResetBotReport);
const bool IsRegistered_RQ_EX_SET_BOT_ADMIN = gPacketTableFromServerEx.Bind(157, RequestSetBotAdmin);
const bool IsRegistered_RQ_EX_DECAY_BOT_REPORT = gPacketTableFromServerEx.Bind(158, RequestDecayBotReport);
const bool IsRegistered_RQ_EX_CHANGE_NICKNAME_COLOR = gPacketTableFromServerEx.Bind(159, RequestChangeNicknameColor);
const bool IsRegistered_RQ_EX_LOAD_PREMIUM_ITEM_FOR_BATCH =
    gPacketTableFromServerEx.Bind(160, RequestLoadPremiumItemForBatch);
const bool IsRegistered_RQ_EX_LOAD_ALL_DOMINION = gPacketTableFromServerEx.Bind(161, RequestLoadAllDominion);
const bool IsRegistered_RQ_EX_SAVE_DOMINION = gPacketTableFromServerEx.Bind(162, RequestSaveDominion);
const bool IsRegistered_RQ_EX_SAVE_DOMINION_REGISTRY = gPacketTableFromServerEx.Bind(163, RequestSaveDominionRegistry);
const bool IsRegistered_RQ_EX_LOAD_DOMINION_REGISTRY = gPacketTableFromServerEx.Bind(164, RequestLoadDominionRegistry);
const bool IsRegistered_RQ_EX_SAVE_OWNTHING = gPacketTableFromServerEx.Bind(165, RequestSaveOwnthing);
const bool IsRegistered_RQ_EX_LOAD_OWNTHING = gPacketTableFromServerEx.Bind(166, RequestLoadOwnthing);
const bool IsRegistered_RQ_EX_SAVE_DOMINION_SIEGE = gPacketTableFromServerEx.Bind(167, RequestSaveDominionSiege);
const bool IsRegistered_RQ_EX_LOAD_DOMINION_SIEGE = gPacketTableFromServerEx.Bind(168, RequestLoadDominionSiege);
const bool IsRegistered_RQ_EX_LOAD_BIRTH_INFO = gPacketTableFromServerEx.Bind(169, RequestLoadBirthInfo);
const bool IsRegistered_RQ_EX_SAVE_BIRTHDAY_GIFT = gPacketTableFromServerEx.Bind(170, RequestSaveBirthdayGift);
const bool IsRegistered_RQ_EX_GET_LAST_BIRTHDAY_GIFT_DATE =
    gPacketTableFromServerEx.Bind(171, RequestGetLastBirthdayGiftDate);
const bool IsRegistered_RQ_EX_LOAD_5_YEAR_INFO = gPacketTableFromServerEx.Bind(172, RequestLoad5YearInfo);
const bool IsRegistered_RQ_EX_SAVE_5_YEAR_GIFT = gPacketTableFromServerEx.Bind(173, RequestSave5YearGift);
const bool IsRegistered_RQ_EX_SAVE_DOMINION_RENAMED = gPacketTableFromServerEx.Bind(174, RequestSaveDominionRenamed);
const bool IsRegistered_RQ_EX_LOAD_DOMINION_RENAMED = gPacketTableFromServerEx.Bind(175, RequestLoadDominionRenamed);
const bool IsRegistered_RQ_EX_DELETE_ALL_DOMINION_RENAMED =
    gPacketTableFromServerEx.Bind(176, RequestDeleteAllDominionRenamed);
const bool IsRegistered_RQ_EX_SET_DAILY_QUEST = gPacketTableFromServerEx.Bind(177, RequestSetDailyQuest);
const bool IsRegistered_RQ_EX_LOAD_DAILY_QUEST = gPacketTableFromServerEx.Bind(178, RequestLoadDailyQuest);
const bool IsRegistered_RQ_EX_SET_BIRTHDAY = gPacketTableFromServerEx.Bind(179, RequestSetBirthday);
const bool IsRegistered_RQ_EX_AIR_SHIP_JOB = gPacketTableFromServerEx.Bind(180, RequestAirShipJob);
const bool IsRegistered_RQ_EX_CACHE_PING = gPacketTableFromServerEx.Bind(181, RequestCachePing);
const bool IsRegistered_RQ_EX_WRITE_POST = gPacketTableFromServerEx.Bind(182, RequestWritePost);
const bool IsRegistered_RQ_EX_SEND_POST = gPacketTableFromServerEx.Bind(183, RequestSendPost);
const bool IsRegistered_RQ_EX_RECEIVE_POST = gPacketTableFromServerEx.Bind(184, RequestReceivePost);
const bool IsRegistered_RQ_EX_REJECT_POST = gPacketTableFromServerEx.Bind(185, RequestRejectPost);
const bool IsRegistered_RQ_EX_CANCEL_POST = gPacketTableFromServerEx.Bind(186, RequestCancelPost);
const bool IsRegistered_RQ_EX_DELETE_POST = gPacketTableFromServerEx.Bind(187, RequestDeletePost);
const bool IsRegistered_RQ_EX_OPEN_POST = gPacketTableFromServerEx.Bind(188, RequestOpenPost);
const bool IsRegistered_RQ_EX_LOAD_POST_LIST = gPacketTableFromServerEx.Bind(189, RequestLoadPostList);
const bool IsRegistered_RQ_EX_LOAD_RECEIVED_POST_LIST = gPacketTableFromServerEx.Bind(190, RequestLoadReceivedPostList);
const bool IsRegistered_RQ_EX_LOAD_SENT_POST_LIST = gPacketTableFromServerEx.Bind(191, RequestLoadSentPostList);
const bool IsRegistered_RQ_EX_CACHE_TEST = gPacketTableFromServerEx.Bind(192, RQExHandler::RequestCacheTest);
const bool IsRegistered_RQ_EX_LOAD_BLOCKED_IP = gPacketTableFromServerEx.Bind(193, LoadBlockedIP);
const bool IsRegistered_RQ_EX_LOAD_PET_ITEM_COUNT = gPacketTableFromServerEx.Bind(194, LoadPetItemCount);
const bool IsRegistered_RQ_EX_RELOAD_CHARACTER = gPacketTableFromServerEx.Bind(195, ReloadCharacter);
const bool IsRegistered_RQ_EX_SUMMON_SAVE_ABNORMAL_STATUS =
    gPacketTableFromServerEx.Bind(196, RequestSummonSaveAbnormalStatusEx);
const bool IsRegistered_RQ_EX_SUMMON_LOAD_ABNORMAL_STATUS =
    gPacketTableFromServerEx.Bind(197, RequestSummonLoadAbnormalStatusEx);
const bool IsRegistered_RQ_EX_SAVE_SUMMON = gPacketTableFromServerEx.Bind(198, RequestSaveSummon);
const bool IsRegistered_RQ_EX_CHECK_RAID_WORLD = gPacketTableFromServerEx.Bind(199, RequestCheckRaidWorld);
const bool IsRegistered_RQ_EX_LOAD_ALLIANCE = gPacketTableFromServerEx.Bind(200, RequestLoadAlliance);
const bool IsRegistered_RQ_EX_SET_QUEST_NPC_LOG = gPacketTableFromServerEx.Bind(201, RequestSetQuestNPCLog);
const bool IsRegistered_RQ_EX_LOAD_INZONE_FLAG = gPacketTableFromServerEx.Bind(202, RequestLoadInZoneFlag);
const bool IsRegistered_RQ_EX_CREATE_INSTANT_ZONE = gPacketTableFromServerEx.Bind(203, RequestCreateInstantZone);
const bool IsRegistered_RQ_EX_SAVE_INSTANT_ZONE_FLAG = gPacketTableFromServerEx.Bind(204, RequestSaveInstantZoneFlag);
const bool IsRegistered_RQ_EX_LOAD_INZONE_MAX_DBID = gPacketTableFromServerEx.Bind(205, RequestLoadInzoneMaxDBID);
const bool IsRegistered_RQ_EX_SAVE_VOTE_SYSTEM = gPacketTableFromServerEx.Bind(206, RequestSaveVoteSystem);
const bool IsRegistered_RQ_EX_CLEAR_EVENT_TIME = gPacketTableFromServerEx.Bind(207, RequestClearEventTime);
const bool IsRegistered_RQ_EX_ADD_POST_FRIEND = gPacketTableFromServerEx.Bind(208, RequestAddPostFriend);
const bool IsRegistered_RQ_EX_LOAD_POST_FRIEND = gPacketTableFromServerEx.Bind(209, RequestLoadPostFriend);
const bool IsRegistered_RQ_EX_DELETE_POST_FRIEND = gPacketTableFromServerEx.Bind(210, RequestDeletePostFriend);
const bool IsRegistered_RQ_EX_LOAD_OLYMPIAD_MATCH_COUNT = gPacketTableFromServerEx.Bind(211, LoadOlympiadMatchCount);
const bool IsRegistered_RQ_EX_SAVE_OLYMPIAD_MATCH_COUNT = gPacketTableFromServerEx.Bind(212, SaveOlympiadMatchCount);
const bool IsRegistered_RQ_EX_GET_OLYMPIAD_SEASON_PLAYER_COUNT =
    gPacketTableFromServerEx.Bind(213, GetOlympiadSeasonPlayerCount);
const bool IsRegistered_RQ_EX_BR_GIVE_ITEM = gPacketTableFromServerEx.Bind(214, RequestBrGiveItem);
const bool IsRegistered_RQ_EX_BR_PLAYING_USER = gPacketTableFromServerEx.Bind(215, RequestBrPlayingUser);
const bool IsRegistered_RQ_EX_BR_PRODUCT_SOLD_COUNT = gPacketTableFromServerEx.Bind(216, RequestBrProductSoldCount);
const bool IsRegistered_RQ_EX_BR_RECENT_PRODUCT = gPacketTableFromServerEx.Bind(217, RequestBrRecentProduct);
const bool IsRegistered_RQ_EX_BR_INSERT_EVENT_RANKING = gPacketTableFromServerEx.Bind(218, RequestBrInsertEventRanking);
const bool IsRegistered_RQ_EX_BR_LOAD_EVENT_TOP_RANKERS = gPacketTableFromServerEx.Bind(219, RequestBrLoadEventTopRankers);
const bool IsRegistered_RQ_EX_BR_UPDATE_EVENT_RANKING = gPacketTableFromServerEx.Bind(220, RequestBrUpdateEventRanking);
const bool IsRegistered_RQ_EX_GET_EVENT_SCORE = gPacketTableFromServerEx.Bind(221, RequestGetEventScore);
const bool IsRegistered_RQ_EX_SET_EVENT_SCORE = gPacketTableFromServerEx.Bind(222, RequestSetEventScore);
const bool IsRegistered_RQ_EX_LOAD_MINIGAME_SCORES = gPacketTableFromServerEx.Bind(223, RequestLoadMinigameScores);
const bool IsRegistered_RQ_EX_INSERT_MINIGAME_SCORE = gPacketTableFromServerEx.Bind(224, RequestInsertMinigameScore);
const bool IsRegistered_RQ_EX_GET_LECTURE_MARK = gPacketTableFromServerEx.Bind(225, RequestGetLectureMark);
const bool IsRegistered_RQ_EX_SET_LECTURE_MARK = gPacketTableFromServerEx.Bind(226, RequestSetLectureMark);
const bool IsRegistered_RQ_EX_SET_CREATE_DATE = gPacketTableFromServerEx.Bind(227, RequestSetCreateDate);
const bool IsRegistered_RQ_EX_LOAD_DROP_ITEM_AMOUNT_PER_SERVER =
    gPacketTableFromServerEx.Bind(228, RequestLoadDropItemAmountPerServer);
const bool IsRegistered_RQ_EX_SAVE_DROP_ITEM_AMOUNT_PER_SERVER =
    gPacketTableFromServerEx.Bind(229, RequestSaveDropItemAmountPerServer);
const bool IsRegistered_RQ_EX_LOAD_ITEM_AGATHION_ENERGY = gPacketTableFromServerEx.Bind(230, RequestLoadItemAgathionEnergy);
const bool IsRegistered_RQ_EX_SAVE_ITEM_AGATHION_ENERGY = gPacketTableFromServerEx.Bind(231, RequestSaveItemAgathionEnergy);
const bool IsRegistered_RQ_EX_RESET_EVENT_DATA = gPacketTableFromServerEx.Bind(232, RequestResetEventData);
const bool IsRegistered_RQ_EX_SET_EVENT_DATA_PERIOD = gPacketTableFromServerEx.Bind(233, RequestSetEventDataPeriod);
const bool IsRegistered_RQ_EX_USER_RACE_SEX = gPacketTableFromServerEx.Bind(234, RequestUserRaceSex);
const bool IsRegistered_RQ_EX_LOAD_RESERVED_ANNOUNCE =
    gPacketTableFromServerEx.Bind(235, RQExHandler::RequestLoadReservedAnnounce);
const bool IsRegistered_RQ_EX_ADD_RESERVED_ANNOUNCE =
    gPacketTableFromServerEx.Bind(236, RQExHandler::RequestAddReservedAnnounce);
const bool IsRegistered_RQ_EX_DELETE_RESERVED_ANNOUNCE =
    gPacketTableFromServerEx.Bind(237, RQExHandler::RequestDeleteReservedAnnounce);
const bool IsRegistered_RQ_EX_LOAD_EVENT_DATA = gPacketTableFromServerEx.Bind(238, RQExHandler::RequestLoadEventData);
const bool IsRegistered_RQ_EX_RELOAD_EVENT_DATA = gPacketTableFromServerEx.Bind(239, RQExHandler::RequestReloadEventData);
const bool IsRegistered_RQ_EX_DUMP_EVENT_DATA = gPacketTableFromServerEx.Bind(240, RQExHandler::RequestDumpEventData);
const bool IsRegistered_RQ_EX_DELETE_SUMMON = gPacketTableFromServerEx.Bind(241, RequestDeleteSummon);
const bool IsRegistered_RQ_EX_ITEM_CHECK = gPacketTableFromServerEx.Bind(242, RequestItemCheck);
const bool IsRegistered_RQ_EX_DB_SECURITY_SETTING = gPacketTableFromServerEx.Bind(243, RequestDBSecuritySetting);
const bool IsRegistered_RQ_EX_SEND_BIRTHDAY_MAIL = gPacketTableFromServerEx.Bind(244, RequestSendBirthDayMail);
const bool IsRegistered_RQ_EX_DELETE_BIRTHDAY_MAIL = gPacketTableFromServerEx.Bind(245, RequestDeleteBirthDayMail);
const bool IsRegistered_RQ_EX_LOAD_GROUP_TOGGLE_STATE = gPacketTableFromServerEx.Bind(246, RequestLoadGroupToggleStateEx);
const bool IsRegistered_RQ_EX_SAVE_GROUP_TOGGLE_STATE = gPacketTableFromServerEx.Bind(247, RequestSaveGroupToggleStateEx);
const bool IsRegistered_RQ_EX_RESET_PROFILE = gPacketTableFromServerEx.Bind(248, RequestResetProfileEx);
const bool IsRegistered_RQ_EX_REPORT_PROFILE = gPacketTableFromServerEx.Bind(249, RequestReportProfileEx);
const bool IsRegistered_RQ_EX_SAVE_PLEDGE_WAR_POINT = gPacketTableFromServerEx.Bind(250, RequestSavePledgeWarPoint);
const bool IsRegistered_RQ_EX_DYNAMIC_QUEST = gPacketTableFromServerEx.Bind(251, RQExHandler::RequestDynamicQuest);
const bool IsRegistered_RQ_EX_LOAD_OTHER_USER_MACRO =
    gPacketTableFromServerEx.Bind(252, RQExHandler::RequestLoadOtherUserMacro);
const bool IsRegistered_RQ_EX_LOAD_OTHER_BUILDER_BOOKMARK =
    gPacketTableFromServerEx.Bind(253, RQExHandler::RequestLoadOtherBuilderBookmark);
const bool IsRegistered_RQ_EX_UPDATE_BLOCK_MEMO = gPacketTableFromServerEx.Bind(254, RQExHandler::RequestUpdateBlockMemo);
const bool IsRegistered_RQ_EX_UPDATE_FRIEND_MEMO = gPacketTableFromServerEx.Bind(255, RQExHandler::RequestUpdateFriendMemo);
const bool IsRegistered_RQ_EX_COMMISSION_TRADE_LOAD_INFO =
    gPacketTableFromServerEx.Bind(256, RQExHandler::RequestCommissionTradeLoadInfo);
const bool IsRegistered_RQ_EX_COMMISSION_TRADE_LOAD_ALL =
    gPacketTableFromServerEx.Bind(257, RQExHandler::RequestCommissionTradeLoadAll);
const bool IsRegistered_RQ_EX_COMMISSION_TRADE_REGISTER =
    gPacketTableFromServerEx.Bind(258, RQExHandler::RequestCommissionTradeRegister);
const bool IsRegistered_RQ_EX_COMMISSION_TRADE_DELETE =
    gPacketTableFromServerEx.Bind(259, RQExHandler::RequestCommissionTradeDelete);
const bool IsRegistered_RQ_EX_COMMISSION_TRADE_BUY_ITEM =
    gPacketTableFromServerEx.Bind(260, RQExHandler::RequestCommissionTradeBuyItem);
const bool IsRegistered_RQ_EX_COMMISSION_TRADE_REGISTERED_LIST =
    gPacketTableFromServerEx.Bind(261, RQExHandler::RequestCommissionTradeRegisteredList);
const bool IsRegistered_RQ_EX_COMMISSION_TRADE_SEARCH_LIST =
    gPacketTableFromServerEx.Bind(262, RQExHandler::RequestCommissionTradeSearchList);
const bool IsRegistered_RQ_EX_COMMISSION_TRADE_DELETE_ALL =
    gPacketTableFromServerEx.Bind(263, RQExHandler::RequestCommissionTradeDeleteAll);
const bool IsRegistered_RQ_EX_COMMISSION_TRADE_REG_TOTAL_COUNT =
    gPacketTableFromServerEx.Bind(264, RQExHandler::RequestCommissionTradeRegTotalCount);
const bool IsRegistered_RQ_EX_INSTANT_AGIT_JOB = gPacketTableFromServerEx.Bind(265, RQExHandler::RequestInstantAgitJob);
const bool IsRegistered_RQ_EX_SAVE_VITALITY = gPacketTableFromServerEx.Bind(266, RQExHandler::RequestSaveVitality);
const bool IsRegistered_RQ_EX_SAVE_AGATHION_SUMMON_TIME = gPacketTableFromServerEx.Bind(267, RequestSaveAgathionSummonTime);
const bool IsRegistered_RQ_EX_SAVE_AGATHION_DESPAWN_TIME =
    gPacketTableFromServerEx.Bind(268, RequestSaveAgathionDespawnTime);
const bool IsRegistered_RQ_EX_GET_AGATHION_ELAPSED_TIME = gPacketTableFromServerEx.Bind(269, RequestGetAgathionElapsedTime);
const bool IsRegistered_RQ_EX_UPDATE_AGATHION_ELAPSED_TIME =
    gPacketTableFromServerEx.Bind(270, RequestUpdateAgathionElapsedTime);
const bool IsRegistered_RQ_EX_INIT_AGATHION_ELAPSED_TIME =
    gPacketTableFromServerEx.Bind(271, RequestInitAgathionElapsedTime);
const bool IsRegistered_RQ_EX_CHECK_CHAR_NAME = gPacketTableFromServerEx.Bind(272, RQExHandler::RequestCheckCharName);
const bool IsRegistered_RQ_EX_SAVE_RETRIEVE_GOODS = gPacketTableFromServerEx.Bind(273, RequestSaveRetrieveGoods);
const bool IsRegistered_RQ_EX_SAVE_USER_JOB_GROUP = gPacketTableFromServerEx.Bind(274, RQExHandler::RequestSaveUserJobGroup);
const bool IsRegistered_RQ_EX_SAVE_DAILY_PCCAFE_POINT = gPacketTableFromServerEx.Bind(275, RequestSaveDailyPCCafePoint);
const bool IsRegistered_RQ_EX_SET_DAILY_RESET_TIME = gPacketTableFromServerEx.Bind(276, RequestSetDailyResetTime);
const bool IsRegistered_RQ_EX_CLEAR_ONE_EVENT_TIME = gPacketTableFromServerEx.Bind(277, RequestClearOneEventTime);
const bool IsRegistered_RQ_EX_LOAD_TIME_TO_GIVE_KARMA_POINT =
    gPacketTableFromServerEx.Bind(278, RequestLoadTimeToGiveKarmaPoint);
const bool IsRegistered_RQ_EX_SAVE_TIME_TO_GIVE_KARMA_POINT =
    gPacketTableFromServerEx.Bind(279, RequestSaveTimeToGiveKarmaPoint);
const bool IsRegistered_RQ_EX_SAVE_CHAR_EXTRA_INFO =
    gPacketTableFromServerEx.Bind(280, RQExHandler::RequestSaveCharExtraInfo);
const bool IsRegistered_RQ_EX_CHANGE_ATTRIBUTE_ITEM =
    gPacketTableFromServerEx.Bind(281, RQExHandler::RequestChangeAttributeItem);
const bool IsRegistered_RQ_EX_DELETE_AND_SAVE_SKILL_LIST =
    gPacketTableFromServerEx.Bind(282, RQExHandler::RequestDeleteAndSaveSkillList);
const bool IsRegistered_RQ_EX_CANCEL_DUAL_CLASS = gPacketTableFromServerEx.Bind(283, RQExHandler::RequestCancelDualClass);
const bool IsRegistered_RQ_EX_BR_CAN_BUY_SHOP_ITEM =
    gPacketTableFromServerEx.Bind(284, RQExHandler::RequestBrCanBuyShopItem);
const bool IsRegistered_RQ_EX_BR_SEND_SHOP_GIFT_MAIL =
    gPacketTableFromServerEx.Bind(285, RQExHandler::RequestBrSendShopGiftMail);
const bool IsRegistered_RQ_EX_GET_DAILY_COUNT = gPacketTableFromServerEx.Bind(286, RQExHandler::RequestGetDailyCount);
const bool IsRegistered_RQ_EX_SET_DAILY_COUNT = gPacketTableFromServerEx.Bind(287, RQExHandler::RequestSetDailyCount);
const bool IsRegistered_RQ_EX_CLEAR_DAILY_COUNT = gPacketTableFromServerEx.Bind(288, RQExHandler::RequestClearDailyCount);
const bool IsRegistered_RQ_EX_ADD_MENTEE = gPacketTableFromServerEx.Bind(289, RQExHandler::RequestAddMentee);
const bool IsRegistered_RQ_EX_LOAD_MENTEE_LIST = gPacketTableFromServerEx.Bind(290, RQExHandler::RequestLoadMenteeList);
const bool IsRegistered_RQ_EX_LOAD_MENTOR = gPacketTableFromServerEx.Bind(291, RQExHandler::RequestLoadMentor);
const bool IsRegistered_RQ_EX_UPDATE_MENTOR_STATUS =
    gPacketTableFromServerEx.Bind(292, RQExHandler::RequestUpdateMentorstatus);
const bool IsRegistered_RQ_EX_UPDATE_MENTEE_PLAYTIME =
    gPacketTableFromServerEx.Bind(293, RQExHandler::RequestUpdateMenteePlaytime);
const bool IsRegistered_RQ_EX_UPDATE_MENTOR_PLAYTIME =
    gPacketTableFromServerEx.Bind(294, RQExHandler::RequestUpdateMentorPlaytime);
const bool IsRegistered_RQ_EX_RESET_MENTOR_PENALTY =
    gPacketTableFromServerEx.Bind(295, RQExHandler::RequestResetMentorPenalty);
const bool IsRegistered_RQ_EX_SHAPE_SHIFT_ITEM = gPacketTableFromServerEx.Bind(296, RQExHandler::RequestShapeShiftItem);
const bool IsRegistered_RQ_EX_SHAPE_SHIFT_ITEM_FIXED =
    gPacketTableFromServerEx.Bind(297, RQExHandler::RequestShapeShiftItemFixed);
const bool IsRegistered_RQ_EX_RESTORE_SHAPE_SHIFT_ITEM =
    gPacketTableFromServerEx.Bind(298, RQExHandler::RequestRestoreShapeShiftItem);
const bool IsRegistered_RQ_EX_EXPIRE_SHAPE_SHIFT_ITEM =
    gPacketTableFromServerEx.Bind(299, RQExHandler::RequestExpireShapeShiftItem);
const bool IsRegistered_RQ_EX_SET_CASTLE_CAMPAIGN =
    gPacketTableFromServerEx.Bind(300, RQExHandler::RequestSetCastleCampaign);
const bool IsRegistered_RQ_EX_LOAD_PLEDGE_GAME = gPacketTableFromServerEx.Bind(301, RQExHandler::RequestLoadPledgeGame);
const bool IsRegistered_RQ_EX_START_PLEDGE_GAME = gPacketTableFromServerEx.Bind(302, RQExHandler::RequestStartPledgeGame);
const bool IsRegistered_RQ_EX_END_PLEDGE_GAME = gPacketTableFromServerEx.Bind(303, RQExHandler::RequestEndPledgeGame);
const bool IsRegistered_RQ_EX_UPDATE_PLEDGE_GAME_RESULT =
    gPacketTableFromServerEx.Bind(304, RQExHandler::RequestUpdatePledgeGameResult);
const bool IsRegistered_RQ_EX_SET_PLEDGE_GAME_GOT_ITEM =
    gPacketTableFromServerEx.Bind(305, RQExHandler::RequestSetPledgeGameGotItem);
const bool IsRegistered_RQ_EX_INCREASE_PLEDGE_GAME_SURRENDER_COUNT =
    gPacketTableFromServerEx.Bind(306, RQExHandler::RequestIncreasePledgeGameSurrenderCount);
const bool IsRegistered_RQ_EX_GET_PLEDGE_GAME_RECORD = gPacketTableFromServerEx.Bind(307, RQExHandler::GetPledgeGameRecord);
const bool IsRegistered_RQ_EX_SET_CASTLE_TYPE = gPacketTableFromServerEx.Bind(308, RQExHandler::RequestSetCastleType);
const bool IsRegistered_RQ_EX_DELETE_MACRO_ALL = gPacketTableFromServerEx.Bind(309, RQExHandler::RequestDeleteMacroAll);
const bool IsRegistered_RQ_EX_MPCC_TEL_BOOKMARK =
    gPacketTableFromServerEx.Bind(310, RQExHandler::RequestMPCCTelBookMarkPacket);
const bool IsRegistered_RQ_EX_LOAD_KALIE_EVENT =
    gPacketTableFromServerEx.Bind(311, RQExHandler::RequestLoadKalieEventPacket);
const bool IsRegistered_RQ_EX_LOAD_KALIE_EVENT_USER =
    gPacketTableFromServerEx.Bind(312, RQExHandler::RequestLoadKalieEventUserPacket);
const bool IsRegistered_RQ_EX_UPDATE_KALIE_EVENT =
    gPacketTableFromServerEx.Bind(313, RQExHandler::RequestUpdateKalieEventPacket);
const bool IsRegistered_RQ_EX_UPDATE_KALIE_EVENT_USER =
    gPacketTableFromServerEx.Bind(314, RQExHandler::RequestUpdateKalieEventUserPacket);
const bool IsRegistered_RQ_EX_SET_COMPETE_AGIT_STATUS = gPacketTableFromServerEx.Bind(315, RequestSetCompeteAgitStatus);
const bool IsRegistered_RQ_EX_SET_COMPETE_AGIT_WINNER = gPacketTableFromServerEx.Bind(316, RequestSetCompeteAgitWinner);
const bool IsRegistered_RQ_EX_RESET_COMPETE_AGIT = gPacketTableFromServerEx.Bind(317, RequestResetCompeteAgit);
const bool IsRegistered_RQ_EX_LOAD_COMPETE_AGIT = gPacketTableFromServerEx.Bind(318, RequestLoadCompeteAgit);
const bool IsRegistered_RQ_EX_LOAD_EXDROP_COUNT = gPacketTableFromServerEx.Bind(319, RQExHandler::RequestLoadExdropCount);
const bool IsRegistered_RQ_EX_UPDATE_EXDROP_COUNT =
    gPacketTableFromServerEx.Bind(320, RQExHandler::RequestUpdateExdropCount);
const bool IsRegistered_RQ_EX_RESET_EXDROP_COUNT = gPacketTableFromServerEx.Bind(321, RQExHandler::RequestResetExdropCount);
const bool IsRegistered_RQ_EX_SAVE_PLEDGE_WAR_POINT_PREV = gPacketTableFromServerEx.Bind(322, RequestSavePledgeWarPointPrev);
const bool IsRegistered_RQ_EX_UPDATE_PLEDGE_WAR_KILL_COUNT =
    gPacketTableFromServerEx.Bind(323, RequestUpdatePledgeWarKillCount);
const bool IsRegistered_RQ_EX_SET_PREVIOUS_AGIT_OWNER_ID = gPacketTableFromServerEx.Bind(324, RequestSetPreviousAgitOwnerID);
const bool IsRegistered_RQ_EX_UPDATE_PLEDGE_NAME_VALUE_BY_PLEDGE_WAR =
    gPacketTableFromServerEx.Bind(325, RequestUpdatePledgeNameValueByPledgeWar);
const bool IsRegistered_RQ_EX_BEAUTY_LOAD = gPacketTableFromServerEx.Bind(326, RQExHandler::RequestBeautyLoad);
const bool IsRegistered_RQ_EX_BUY_BEAUTY_ITEM = gPacketTableFromServerEx.Bind(327, RQExHandler::RequestBuyBeautyItem);
const bool IsRegistered_RQ_EX_RESET_BEAUTY_ITEM = gPacketTableFromServerEx.Bind(328, RQExHandler::RequestResetBeautyItem);
const bool IsRegistered_RQ_EX_RESET_BEAUTY_LIST = gPacketTableFromServerEx.Bind(329, RQExHandler::RequestResetBeautyList);
const bool IsRegistered_RQ_EX_LOAD_NPC_QUEST_LOG = gPacketTableFromServerEx.Bind(330, RQExHandler::RequestLoadNPCQuestLog);
const bool IsRegistered_RQ_EX_RESET_WAR_TARGETED = gPacketTableFromServerEx.Bind(331, RequestResetWarTargeted);
const bool IsRegistered_RQ_EX_LEAVE_WORLD_SAVE_CHAR = gPacketTableFromServerEx.Bind(332, RQExHandler::LeaveWorldSaveChar);
const bool IsRegistered_RQ_EX_LEAVE_WORLD_SAVE_SKILL_USE =
    gPacketTableFromServerEx.Bind(333, RQExHandler::LeaveWorldSaveSkillUse);
const bool IsRegistered_RQ_EX_LEAVE_WORLD_SAVE_ABNORMAL_STATUS =
    gPacketTableFromServerEx.Bind(334, RQExHandler::LeaveWorldSaveAbnormalStatus);
const bool IsRegistered_RQ_EX_LOAD_MENTEE_STATE = gPacketTableFromServerEx.Bind(335, RQExHandler::RequestLoadMenteeState);
const bool IsRegistered_RQ_EX_CANCEL_EQUIPMENT_ADDED_EFFECT_AND_DROP_ITEM =
    gPacketTableFromServerEx.Bind(336, RQExHandler::RequestCancelEquipmentAddedEffectAndDropItem);
const bool IsRegistered_RQ_EX_CHAR_LEVEL_UP = gPacketTableFromServerEx.Bind(337, RQExHandler::RequestCharLevelUp);
const bool IsRegistered_RQ_EX_BR_INTERESTED_PRODUCT =
    gPacketTableFromServerEx.Bind(338, RQExHandler::RequestBrInterestedProduct);
const bool IsRegistered_RQ_EX_BR_ADD_INTERESTED_PRODUCT =
    gPacketTableFromServerEx.Bind(339, RQExHandler::RequestBrAddInterestedProduct);
const bool IsRegistered_RQ_EX_BR_DELETE_INTERESTED_PRODUCT =
    gPacketTableFromServerEx.Bind(340, RQExHandler::RequestBrDeleteInterestedProduct);
const bool IsRegistered_RQ_EX_PERIODIC_SHAPE_SHIFTED_ITEM_JOB =
    gPacketTableFromServerEx.Bind(341, RQExHandler::RequestPeriodicShapeShiftedItemJob);
const bool IsRegistered_RQ_EX_PLEDGE_JOIN_SYSTEM_INIT =
    gPacketTableFromServerEx.Bind(342, RQExHandler::RequestPledgeJoinSystemInit);
const bool IsRegistered_RQ_EX_LOAD_PLEDGE_ON_PLEDGE_RECRUIT_BOARD =
    gPacketTableFromServerEx.Bind(343, RQExHandler::RequestLoadPledgeOnPledgeRecruitBoard);
const bool IsRegistered_RQ_EX_WRITE_PLEDGE_RECRUIT_BOARD =
    gPacketTableFromServerEx.Bind(344, RQExHandler::RequestWritePledgeRecruitBoard);
const bool IsRegistered_RQ_EX_MODIFY_PLEDGE_RECRUIT_BOARD =
    gPacketTableFromServerEx.Bind(345, RQExHandler::RequestModifyPledgeRecruitBoard);
const bool IsRegistered_RQ_EX_DELETE_PLEDGE_RECRUIT_BOARD =
    gPacketTableFromServerEx.Bind(346, RQExHandler::RequestDeletePledgeRecruitBoard);
const bool IsRegistered_RQ_EX_SEARCH_PLEDGE_RECRUIT_BOARD =
    gPacketTableFromServerEx.Bind(347, RQExHandler::RequestSearchPledgeRecruitBoard);
const bool IsRegistered_RQ_EX_MODIFY_PLEDGE_INFO_ON_BOARD =
    gPacketTableFromServerEx.Bind(348, RQExHandler::RequestModifyPledgeInfoOnBoard);
const bool IsRegistered_RQ_EX_PLEDGE_ON_PLEDGE_RECRUIT_BOARD =
    gPacketTableFromServerEx.Bind(349, RQExHandler::RequestPledgeOnPledgeRecruitBoard);
const bool IsRegistered_RQ_EX_APPLY_INFO_ON_PLEDGE_WAITING_LIST =
    gPacketTableFromServerEx.Bind(350, RQExHandler::RequestApplyInfoOnPledgeWaitingList);
const bool IsRegistered_RQ_EX_WRITE_PLEDGE_WAITING_USER_LIST =
    gPacketTableFromServerEx.Bind(351, RQExHandler::RequestWritePledgeWaitingUserList);
const bool IsRegistered_RQ_EX_DELETE_PLEDGE_WAITING_USER_LIST =
    gPacketTableFromServerEx.Bind(352, RQExHandler::RequestDeletePledgeWaitingUserList);
const bool IsRegistered_RQ_EX_PLEDGE_WAITING_USER_LIST =
    gPacketTableFromServerEx.Bind(353, RQExHandler::RequestPledgeWaitingUserList);
const bool IsRegistered_RQ_EX_USER_ON_PLEDGE_WAITING_USER_LIST =
    gPacketTableFromServerEx.Bind(354, RQExHandler::RequestUserOnPledgeWaitingUserList);
const bool IsRegistered_RQ_EX_WRITE_PLEDGE_DRAFT_USER_LIST =
    gPacketTableFromServerEx.Bind(355, RQExHandler::RequestWritePledgeDraftUserList);
const bool IsRegistered_RQ_EX_DELETE_PLEDGE_DRAFT_USER_LIST =
    gPacketTableFromServerEx.Bind(356, RQExHandler::RequestDeletePledgeDraftUserList);
const bool IsRegistered_RQ_EX_PLEDGE_DRAFT_USER_LIST =
    gPacketTableFromServerEx.Bind(357, RQExHandler::RequestPledgeDraftUserList);
const bool IsRegistered_RQ_EX_PLEDGE_RECRUIT_APPLY_INFO =
    gPacketTableFromServerEx.Bind(358, RQExHandler::RequestPledgeRecruitApplyInfo);
const bool IsRegistered_RQ_EX_PLEDGE_JOIN_SYSTEM_CODE =
    gPacketTableFromServerEx.Bind(359, RQExHandler::RequestPledgeJoinSystemCode);
const bool IsRegistered_RQ_EX_MODIFY_APPLIED_USER_INFO =
    gPacketTableFromServerEx.Bind(360, RQExHandler::RequestModifyAppliedUserInfo);
const bool IsRegistered_RQ_EX_UPDATE_MAX_LEVEL = gPacketTableFromServerEx.Bind(361, RQExHandler::RequestUpdateMaxLevel);
const bool IsRegistered_RQ_EX_SAVE_PETITION_EXTERNAL_NOTIFY =
    gPacketTableFromServerEx.Bind(362, RQExHandler::RequestSavePetitionExternalNotify);
const bool IsRegistered_RQ_EX_DELETE_PETITION_EXTERNAL_NOTIFY =
    gPacketTableFromServerEx.Bind(363, RQExHandler::RequestDeletePetitionExternalNotify);
const bool IsRegistered_RQ_EX_LOAD_PETITION_EXTERNAL_NOTIFY =
    gPacketTableFromServerEx.Bind(364, RQExHandler::RequestLoadPetitionExternalNotify);
const bool IsRegistered_RQ_EX_SET_DORMANT_TIME = gPacketTableFromServerEx.Bind(365, RQExHandler::RequestSetDormantTime);
const bool IsRegistered_RQ_EX_LOAD_ACCOUNT_MAX_LEVEL =
    gPacketTableFromServerEx.Bind(366, RQExHandler::RequestLoadAccountMaxLevel);
const bool IsRegistered_RQ_EX_RELOAD_USER_FLAG = gPacketTableFromServerEx.Bind(367, RQExHandler::RequestReloadUserFlag);
const bool IsRegistered_RQ_EX_REGISTER_XMAS_WISH = gPacketTableFromServerEx.Bind(368, RQExHandler::RequestRegisterXmasWish);
const bool IsRegistered_RQ_EX_CONFIRM_XMAS_WISH = gPacketTableFromServerEx.Bind(369, RQExHandler::RequestConfirmXmasWish);
const bool IsRegistered_RQ_EX_GET_DORMANT_GIFT_INFO =
    gPacketTableFromServerEx.Bind(370, RQExHandler::RequestGetDormantGiftInfo);
const bool IsRegistered_RQ_EX_NEW_MANIPULATE_ITEM =
    gPacketTableFromServerEx.Bind(371, RQExHandler::RequestNewManipulateItem);
const bool IsRegistered_RQ_EX_TRADE_DIVIDE_ADENA = gPacketTableFromServerEx.Bind(372, RQExHandler::TradeDivideAdena);
const bool IsRegistered_RQ_EX_BEAUTY_PURCHASE_LIST =
    gPacketTableFromServerEx.Bind(373, RQExHandler::RequestBeautyPurchaseList);
const bool IsRegistered_RQ_EX_UPDATE_HAIR_FLAG = gPacketTableFromServerEx.Bind(374, RQExHandler::RequestUpdateHairFlag);
const bool IsRegistered_RQ_EX_ACQUIRE_POTENTIAL_SKILL =
    gPacketTableFromServerEx.Bind(375, RQExHandler::RequestAcquirePotentialSkill);
const bool IsRegistered_RQ_EX_RESET_POTENTIAL_SKILL =
    gPacketTableFromServerEx.Bind(376, RQExHandler::RequestResetPotentialSkill);
const bool IsRegistered_RQ_EX_CHANGE_AP = gPacketTableFromServerEx.Bind(377, RQExHandler::Request_RQ_EX_CHANGE_AP);
const bool IsRegistered_RQ_EX_START_NEW_FISHING_CHAMPIONSHIP =
    gPacketTableFromServerEx.Bind(378, RQExHandler::RequestStartNewFIshingChampionship);
const bool IsRegistered_RQ_EX_SAVE_FISHING_CHAMPIONSHIP_RESULT =
    gPacketTableFromServerEx.Bind(379, RQExHandler::RequestSaveFIshingChampionshipResult);
const bool IsRegistered_RQ_EX_RESET_FISHING_CHAMPIONSHIP =
    gPacketTableFromServerEx.Bind(380, RQExHandler::RequestResetFIshingChampionship);
const bool IsRegistered_RQ_EX_GIVE_FISHING_CHAMPIONSHIP_REWARD =
    gPacketTableFromServerEx.Bind(381, RQExHandler::RequestGiveFIshingChampionshipReward);
const bool IsRegistered_RQ_EX_BR_START_TRAINING_ZONE =
    gPacketTableFromServerEx.Bind(382, RQExHandler::RequestBrStartTrainingZone);
const bool IsRegistered_RQ_EX_BR_END_TRAINING_ZONE =
    gPacketTableFromServerEx.Bind(383, RQExHandler::RequestBrEndTrainingZone);
const bool IsRegistered_RQ_EX_BR_REWARD_TRAINING_ZONE =
    gPacketTableFromServerEx.Bind(384, RQExHandler::RequestBrRewardTrainingZone);
const bool IsRegistered_RQ_EX_BR_LOAD_TRAINING_ZONE =
    gPacketTableFromServerEx.Bind(385, RQExHandler::RequestBrLoadTrainingZone);
const bool IsRegistered_RQ_EX_PERIODIC_PREMIUM_HENNA_JOB =
    gPacketTableFromServerEx.Bind(386, RQExHandler::RequestPeriodicPremiumHennaJob);
const bool IsRegistered_RQ_EX_BR_RESET_SHOP_LIMIT = gPacketTableFromServerEx.Bind(387, RQExHandler::RequestBrResetShopLimit);
const bool IsRegistered_RQ_EX_ACCOUNT_USER_STAMP = gPacketTableFromServerEx.Bind(388, RQExHandler::RequestAccountUserStamp);
const bool IsRegistered_RQ_EX_ACCOUNT_USER_STAMP_BUILDER =
    gPacketTableFromServerEx.Bind(389, RQExHandler::RequestAccountUserStampBuilder);
const bool IsRegistered_RQ_EX_DELETE_ACCOUNT_USER_STAMP_LOG_BUILDER =
    gPacketTableFromServerEx.Bind(390, RQExHandler::RequestDeleteAccountUserStampLogBuilder);
const bool IsRegistered_RQ_EX_LOAD_WORLD_CHAT_POINT =
    gPacketTableFromServerEx.Bind(391, RQExHandler::RequestLoadWorldChatPoint);
const bool IsRegistered_RQ_EX_INC_USED_WORLD_CHAT_POINT =
    gPacketTableFromServerEx.Bind(392, RQExHandler::RequestIncUsedWorldChatPoint);
const bool IsRegistered_RQ_EX_RESET_WORLD_CHAT_POINT =
    gPacketTableFromServerEx.Bind(393, RQExHandler::RequestResetWorldChatPoint);
const bool IsRegistered_RQ_EX_LOAD_WORLD_CHAT_TIME =
    gPacketTableFromServerEx.Bind(394, RQExHandler::RequestLoadWorldChatTime);
const bool IsRegistered_RQ_EX_LOAD_MATCHING_POINT =
    gPacketTableFromServerEx.Bind(395, RQExHandler::RequestLoadMatchingPoint);
const bool IsRegistered_RQ_EX_SAVE_MATCHING_POINT =
    gPacketTableFromServerEx.Bind(396, RQExHandler::RequestSaveMatchingPoint);
const bool IsRegistered_RQ_EX_CREATE_DUAL_JOB = gPacketTableFromServerEx.Bind(397, CreateDualJob);
const bool IsRegistered_RQ_EX_KAMAROKA_ROUND = gPacketTableFromServerEx.Bind(398, RQExHandler::RequestKamarokaRound);
const bool IsRegistered_RQ_EX_KAMAROKA_RANKING_LIST =
    gPacketTableFromServerEx.Bind(399, RQExHandler::RequestKamarokaRankingList);
const bool IsRegistered_RQ_EX_KAMAROKA_ADD_NEW_RECORD =
    gPacketTableFromServerEx.Bind(400, RQExHandler::RequestKamarokaAddNewRecord);
const bool IsRegistered_RQ_EX_KAMAROKA_PERSONAL_RANK_INFO =
    gPacketTableFromServerEx.Bind(401, RQExHandler::RequestKamarokaPersonalRankInfo);
const bool IsRegistered_RQ_EX_UPDATE_UI_COUNT = gPacketTableFromServerEx.Bind(402, RQExHandler::RequestUpdateUICount);
const bool IsRegistered_RQ_EX_LOAD_CLIENT_INI = gPacketTableFromServerEx.Bind(403, RQExHandler::RequestLoadClientINI);
const bool IsRegistered_RQ_EX_SAVE_CLIENT_INI = gPacketTableFromServerEx.Bind(404, RQExHandler::RequestSaveClientINI);
const bool IsRegistered_RQ_EX_LOAD_DROID_NPC_APPEARANCE =
    gPacketTableFromServerEx.Bind(405, RQExHandler::RequestLoadDroidNPCAppearance);
const bool IsRegistered_RQ_EX_LOAD_HONORS_SKILL = gPacketTableFromServerEx.Bind(406, RQExHandler::RequestLoadHonorsSkill);
const bool IsRegistered_RQ_EX_SET_HONORS_SKILL = gPacketTableFromServerEx.Bind(407, RQExHandler::RequestSetHonorsSkill);
const bool IsRegistered_RQ_EX_DELETE_HONORS_SKILL =
    gPacketTableFromServerEx.Bind(408, RQExHandler::RequestDeleteHonorsSkill);
const bool IsRegistered_RQ_EX_RESET_HONORS_SKILL = gPacketTableFromServerEx.Bind(409, RQExHandler::RequestResetHonorsSkill);
const bool IsRegistered_RQ_EX_UPDATE_WEDDING = gPacketTableFromServerEx.Bind(410, RQExHandler::RequestUpdateWedding);
const bool IsRegistered_RQ_EX_LOAD_WEDDING = gPacketTableFromServerEx.Bind(411, RQExHandler::RequestLoadWedding);
const bool IsRegistered_RQ_EX_LOAD_FRIEND_DETAIL_INFO =
    gPacketTableFromServerEx.Bind(412, RQExHandler::RequestLoadFriendDetailInfo);
const bool IsRegistered_RQ_EX_SIGNIN_PLEDGE = gPacketTableFromServerEx.Bind(413, RQExHandler::Request_RQ_EX_SIGNIN_PLEDGE);
const bool IsRegistered_RQ_EX_LOAD_FACTION_POINT = gPacketTableFromServerEx.Bind(414, RQExHandler::RequestLoadFactionPoint);
const bool IsRegistered_RQ_EX_UPDATE_FACTION_POINT =
    gPacketTableFromServerEx.Bind(415, RQExHandler::RequestUpdateFactionPoint);
const bool IsRegistered_RQ_EX_LOAD_BALTHUS_EVENT_USER_INFO =
    gPacketTableFromServerEx.Bind(416, RQExHandler::RequestLoadBalthusEventUserInfo);
const bool IsRegistered_RQ_EX_UPDATE_BALTHUS_EVENT_USER_INFO =
    gPacketTableFromServerEx.Bind(417, RQExHandler::RequestUpdateBalthusEventUserInfo);
const bool IsRegistered_RQ_EX_UPDATE_USER_INZONE_POINT =
    gPacketTableFromServerEx.Bind(418, RQExHandler::RequestUpdateUserInzonePoint);
const bool IsRegistered_RQ_EX_LOAD_USER_INZONE_POINT =
    gPacketTableFromServerEx.Bind(419, RQExHandler::RequestLoadUserInzonePoint);
const bool IsRegistered_RQ_EX_WOLYMPIAD_LOAD_ALL_USER =
    gPacketTableFromServerEx.Bind(420, RQExHandler::Request_RQ_EX_WOLYMPIAD_LOAD_ALL_USER);
const bool IsRegistered_RQ_EX_WOLYMPIAD_GET_TOP10 = gPacketTableFromServerEx.Bind(421, DummyServerExPacket);
const bool IsRegistered_RQ_EX_WOLYMPIAD_GET_MATCH_COUNT =
    gPacketTableFromServerEx.Bind(422, RQExHandler::Request_RQ_EX_WOLYMPIAD_GET_MATCH_COUNT);
const bool IsRegistered_RQ_EX_WOLYMPIAD_GET_ACHIEVEMENTS = gPacketTableFromServerEx.Bind(423, DummyServerExPacket);
const bool IsRegistered_RQ_EX_WOLYMPIAD_GET_MATCH_RESULT =
    gPacketTableFromServerEx.Bind(424, RQExHandler::Request_RQ_EX_WOLYMPIAD_GET_MATCH_RESULT);
const bool IsRegistered_RQ_EX_WOLYMPIAD_MATCHING_POINT_LOAD =
    gPacketTableFromServerEx.Bind(425, RQExHandler::Request_RQ_EX_WOLYMPIAD_MATCHING_POINT_LOAD);
const bool IsRegistered_RQ_EX_SAVE_CLASS_CATEGORY_DATA =
    gPacketTableFromServerEx.Bind(426, RQExHandler::RequestSaveCategoryData);
const bool IsRegistered_RQ_EX_UPDATE_ONE_DAY_REWARD_INFO =
    gPacketTableFromServerEx.Bind(427, RQExHandler::RequestUpdateOneDayRewardInfo);
const bool IsRegistered_RQ_EX_LOAD_AUTO_SOULSHOT_UI =
    gPacketTableFromServerEx.Bind(428, RQExHandler::RequestLoadAutoSoulShotUI);
const bool IsRegistered_RQ_EX_SAVE_AUTO_SOULSHOT_UI =
    gPacketTableFromServerEx.Bind(429, RQExHandler::RequestSaveAutoSoulShotUI);
const bool IsRegistered_RQ_EX_INIT_USER_PLEDGE_BONUS =
    gPacketTableFromServerEx.Bind(430, RQExHandler::RequestInitUserPledgeBonus);
const bool IsRegistered_RQ_EX_UPDATE_USER_PLEDGE_BONUS =
    gPacketTableFromServerEx.Bind(431, RQExHandler::RequestUpdateUserPledgeBonus);
const bool IsRegistered_RQ_EX_LOAD_USER_PLEDGE_BONUS =
    gPacketTableFromServerEx.Bind(432, RQExHandler::RequestLoadUserPledgeBonus);
const bool IsRegistered_RQ_EX_UPDATE_USER_PLEDGE_BONUS_REWARD =
    gPacketTableFromServerEx.Bind(433, RQExHandler::RequestUpdateUserPledgeBonusReward);
const bool IsRegistered_RQ_EX_UPDATE_USER_MONSTER_BOOK =
    gPacketTableFromServerEx.Bind(434, RQExHandler::RequestUpdateUserMonsterBook);
const bool IsRegistered_RQ_EX_LOAD_USER_MONSTER_BOOK =
    gPacketTableFromServerEx.Bind(435, RQExHandler::RequestLoadUserMonsterBook);
const bool IsRegistered_RQ_EX_UPGRADE_SYSTEM = gPacketTableFromServerEx.Bind(436, RQExHandler::RequestUpgradeSystem);
const bool IsRegistered_RQ_EX_LOAD_CARD_UPDOWN_RECORD =
    gPacketTableFromServerEx.Bind(437, RQExHandler::RequestLoadCardUpdownGameRecord);
const bool IsRegistered_RQ_EX_UPDATE_CARD_UPDOWN_RECORD =
    gPacketTableFromServerEx.Bind(438, RQExHandler::RequestUpdateCardUpdownGameRecord);
const bool IsRegistered_RQ_EX_PLEDGE_MANIPULATE_ITEMS =
    gPacketTableFromServerEx.Bind(439, RQExHandler::Request_RQ_EX_PLEDGE_MANIPULATE_ITEMS);
const bool IsRegistered_RQ_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO =
    gPacketTableFromServerEx.Bind(440, RQExHandler::Request_RQ_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO);
const bool IsRegistered_RQ_EX_PLEDGE_CONTRIBUTION_RANK =
    gPacketTableFromServerEx.Bind(441, RQExHandler::Request_RQ_EX_PLEDGE_CONTRIBUTION_RANK);
const bool IsRegistered_RQ_EX_PLEDGE_ITEM_LOAD =
    gPacketTableFromServerEx.Bind(442, RQExHandler::Request_RQ_EX_PLEDGE_ITEM_LOAD);
const bool IsRegistered_RQ_EX_PLEDGE_ITEM_SAVE =
    gPacketTableFromServerEx.Bind(443, RQExHandler::Request_RQ_EX_PLEDGE_ITEM_SAVE);
const bool IsRegistered_RQ_EX_PLEDGE_ITEM_DELETE =
    gPacketTableFromServerEx.Bind(444, RQExHandler::Request_RQ_EX_PLEDGE_ITEM_DELETE);
const bool IsRegistered_RQ_EX_PLEDGE_RAID_GET_INFO_LIST =
    gPacketTableFromServerEx.Bind(445, RQExHandler::Request_RQ_EX_PLEDGE_RAID_GET_INFO_LIST);
const bool IsRegistered_RQ_EX_PLEDGE_RAID_INFO_UPDATE_REWARD_REASON =
    gPacketTableFromServerEx.Bind(446, RQExHandler::Request_RQ_EX_PLEDGE_RAID_INFO_UPDATE_REWARD_REASON);
const bool IsRegistered_RQ_EX_PLEDGE_RAID_INZONE_LOAD =
    gPacketTableFromServerEx.Bind(447, RQExHandler::Request_RQ_EX_PLEDGE_RAID_INZONE_LOAD);
const bool IsRegistered_RQ_EX_PLEDGE_MISSION_LOAD =
    gPacketTableFromServerEx.Bind(448, RQExHandler::Request_RQ_EX_PLEDGE_MISSION_LOAD);
const bool IsRegistered_RQ_EX_PLEDGE_MISSION_ADD_COUNT =
    gPacketTableFromServerEx.Bind(449, RQExHandler::Request_RQ_EX_PLEDGE_MISSION_ADD_COUNT);
const bool IsRegistered_RQ_EX_PLEDGE_ANNOUNCE =
    gPacketTableFromServerEx.Bind(450, RQExHandler::Request_RQ_EX_PLEDGE_ANNOUNCE);
const bool IsRegistered_RQ_EX_PLEDGE_ANNOUNCE_SET =
    gPacketTableFromServerEx.Bind(451, RQExHandler::Request_RQ_EX_PLEDGE_ANNOUNCE_SET);
const bool IsRegistered_RQ_EX_SEQUENCE_JOB = gPacketTableFromServerEx.Bind(452, RQExHandler::Request_RQ_EX_SEQUENCE_JOB);
const bool IsRegistered_RQ_EX_BUILDER_COMMAND =
    gPacketTableFromServerEx.Bind(453, RQExHandler::Request_RQ_EX_BUILDER_COMMAND);
const bool IsRegistered_RQ_EX_LOCKED_ITEM = gPacketTableFromServerEx.Bind(454, RQExHandler::RequestLockedItem);
const bool IsRegistered_RQ_EX_SET_ITEM_REUSE_DELAY =
    gPacketTableFromServerEx.Bind(455, RQExHandler::Request_RQ_EX_SET_ITEM_REUSE_DELAY);
const bool IsRegistered_RQ_EX_PLEDGE_MANIPULATE_ITEMS_NEW =
    gPacketTableFromServerEx.Bind(456, RQExHandler::Request_RQ_EX_PLEDGE_MANIPULATE_ITEMS_NEW);
const bool IsRegistered_RQ_EX_LOAD_RESERVATION_ANNOUNCE =
    gPacketTableFromServerEx.Bind(457, RQExHandler::Request_RQ_EX_LOAD_RESERVATION_ANNOUNCE);
const bool IsRegistered_RQ_EX_DELETE_RESERVATION_ANNOUNCE =
    gPacketTableFromServerEx.Bind(458, RQExHandler::Request_RQ_EX_DELETE_RESERVATION_ANNOUNCE);
const bool IsRegistered_RQ_EX_SET_RESERVATION_ANNOUNCE =
    gPacketTableFromServerEx.Bind(459, RQExHandler::Request_RQ_EX_SET_RESERVATION_ANNOUNCE);
const bool IsRegistered_RQ_EX_SHOW_RESERVATION_ANNOUNCE =
    gPacketTableFromServerEx.Bind(460, RQExHandler::Request_RQ_EX_SHOW_RESERVATION_ANNOUNCE);

} // namespace

std::atomic<CSocket*> g_pActiveServerSocket{nullptr};
CSocketHandler g_socketHandler;
std::atomic<std::int32_t> g_nPendingLogoutCharacters{0};

void CompleteCacheDShutdownPersistence(const char* completionMessage) {
    // Log.Add(LOG_ERROR, "%s", completionMessage)
    // Log.Add(LOG_NORMAL, "%s", completionMessage)
    L2_LOG_INFO(Log, "%s", completionMessage);
    if (g_serverStopRequested.load(std::memory_order_acquire)) {
        // Current CacheD enters its deliberate crash path after the last two
        // counters reach zero. Cooperative process exit replaces only
        // that Windows crash/minidump transport.
        // Log.Add(LOG_NORMAL,
        //     "[shutdown] Requesting CacheD termination after the item-check callbacks.")
        L2_LOG_INFO(Log,
            "[shutdown] Requesting CacheD termination after the item-check callbacks.");
        g_bTerminating = 1;
    }
}

L2_DEFINE_OBJECT_CLASS_ID(CSocket)

L2_DEFINE_OBJECT_CLASS_NAME(CSocket, m_pClassName_CSocket)

L2_DEFINE_OBJECT_NEW(CSocket)

// Current allocator 0x0045b204 publishes 0x188 bytes. The complete CRT
// table makes 0x0069fed4 the eleventh MemoryObject class initializer.
L2_DEFINE_OBJECT_ALLOCATOR(CSocket, m_pClassName_CSocket)

L2_DEFINE_OBJECT_RELEASE(CSocket)

CSocket::CSocket(std::uint64_t socket, int bufferSize [[maybe_unused]]) :
    CacheDIOSocket(socket, 600),
    m_socket(socket),
    m_opaqueB0(0),
    m_threadCount(0),
    m_opaqueD0(0),
    m_opaqueD8(0),
    m_opaqueF0(0),
    m_taggedEvent(1),
    m_event(0),
    m_packetTable(gPacketTableFromServer.GetData()),
    m_address{},
    m_packetLock(4000),
    m_mode(SocketMode::SM_READ_LEN),
    m_packetLength(0),
    m_packetCount(0),
    m_oldPacketCount(0),
    m_key(0),
    m_createdTick(runtime::GetTickCount()) {
    L2_GUARD_FUNCTION();
}

CSocket::~CSocket() { L2_GUARD_FUNCTION(); }

CSocket* CSocket::Allocate(std::uint64_t socket) {
    L2_GUARD_FUNCTION();
    return new CSocket(socket, 0x5000);
}

void CSocket::SetAddress(::in_addr address) {
    L2_GUARD_FUNCTION();
    m_address = address;
}

void CSocket::OnCreate() {
    L2_GUARD_FUNCTION();
    CacheDIOSocket::OnCreate();
}

bool CSocket::CheckPendingWriteSafe(int pendingWrite) const {
    if (m_hSocket.load(std::memory_order_acquire) == UINT64_C(0xffffffffffffffff)) {
        return false;
    }
    const std::int64_t combined = static_cast<std::int64_t>(pendingWrite) + m_nPendingWrite.load(std::memory_order_relaxed);
    return combined < 0x1000001;
}

void CSocket::Send(const char* format, ...) {
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::LoadEnum32Relaxed(&m_mode) == SocketMode::SM_CLOSE) {
        return;
    }

    va_list arguments;
    va_start(arguments, format);
    SendV(format, arguments);
    va_end(arguments);
}

void CSocket::SendV(const char* format, va_list arguments) {
    L2_GUARD_FUNCTION();

    DefaultIOBuffer* const buffer = DefaultIOBuffer::Alloc();
    const int bodySize = ::VAssemble(reinterpret_cast<char*>(buffer->m_Buffer.data() + 3), 0x4ffd, format, arguments);
    if (bodySize == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "%d: assemble too large packet. format %s",
        //     runtime::LegacySocketImportAddressToken(),
        //     format)
        // Log.Add(LOG_ERROR,
        //     "[L2Server] Cannot assemble a reply on connection %llu to %s within the 20477-byte body buffer (format %s). Discarding this reply.",
        //     static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), AddressText(m_address).c_str(), format)
        L2_LOG_ERROR(Log,
            "[L2Server] Cannot assemble a reply on connection %llu to %s within the 20477-byte body buffer (format %s); discarding this reply.",
            static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), AddressText(m_address).c_str(), format);
        buffer->Release();
        return;
    }

    const int packetSize = bodySize + 3;
    buffer->m_Buffer[0] = static_cast<std::uint8_t>(packetSize);
    buffer->m_Buffer[1] = static_cast<std::uint8_t>(static_cast<unsigned>(packetSize) >> 8U);
    buffer->m_Buffer[2] = static_cast<std::uint8_t>(runtime::process::CurrentDatabaseId());

    std::uint16_t extendedProtocol = 0;
    if (buffer->m_Buffer[3] == 0xc6) {
        extendedProtocol =
            static_cast<std::uint16_t>(buffer->m_Buffer[4]) | (static_cast<std::uint16_t>(buffer->m_Buffer[5]) << 8U);
    }
    OnSending(buffer->m_Buffer[3], extendedProtocol, packetSize);

    {
        L2_GUARD(m_sendLock);
        buffer->m_dwSize = static_cast<std::uint32_t>(packetSize);
        L2_ADD_REF(this, ObjectRefType::IoSocketWrite);
        SyncWrite(buffer);
    }
}

void CSocket::OnSending(std::uint8_t protocol, std::uint16_t extendedProtocol, int packetSize) const {
    L2_GUARD_FUNCTION();
    if (!runtime::process::IsCacheDToServerPacketStatMonitorEnabled()) {
        return;
    }

    const std::size_t index = static_cast<std::size_t>(protocol) + extendedProtocol;
    if (index >= 506) {
        return;
    }
    CacheDToServerPacketStat::Inst().Accumulate(static_cast<int>(index), packetSize);
}

void CSocket::OnClose() {
    L2_GUARD_FUNCTION();
    const int error = GetSocketLastError();
    const std::string address = AddressText(m_address);
    // Log.Add(LOG_NORMAL, "server closed(%s) error: %d", address.c_str(), error)
    // Log.Add(LOG_NORMAL,
    //     "[L2Server] Connection from %s closed. Last socket error observed: %d. This saved value may not identify the cause.",
    //     address.c_str(), error)
    L2_LOG_INFO(Log,
        "[L2Server] Connection from %s closed. Last socket error observed: %d; this saved value may not identify the cause.",
        address.c_str(), error);
    runtime::raw_atomic::StoreEnum32Relaxed(&m_mode, SocketMode::SM_CLOSE);

    const std::int32_t closedSocket =
        runtime::raw_atomic::ExchangeI32(reinterpret_cast<volatile std::int32_t*>(&m_socket), 0);
    CSocket* expected = this;
    if (closedSocket != 0 &&
        g_pActiveServerSocket.compare_exchange_strong(
            expected, nullptr, std::memory_order_acq_rel, std::memory_order_acquire
        )) {
        const std::uint32_t serverBufferSize = GetReadBufSize();
        // Log.Add(
        //     LOG_ERROR,
        //     "server closed(%s) error: %d read buffer size: "
        //     "(server:%u npc:%d)",
        //     address.c_str(), error, serverBufferSize, static_cast<int>(g_NpcServer.GetReadBufSize())
        // )
        // Log.Add(LOG_WARN,
        //     "[L2Server] Active game-server connection from %s was removed. Starting disconnect cleanup (read buffers: game server %u bytes, NPC server %u bytes). Character logout and clan checks can continue asynchronously.",
        //     address.c_str(), serverBufferSize, g_NpcServer.GetReadBufSize())
        L2_LOG_WARN(Log,
            "[L2Server] Active game-server connection from %s was removed; starting disconnect cleanup (read buffers: game server %u bytes, NPC server %u bytes). Character logout and clan checks can continue asynchronously.",
            address.c_str(), serverBufferSize, g_NpcServer.GetReadBufSize());
        g_castleDb.ItemCheckOnAllCastle();
        g_pledgeDb.ItemCheckOnAllLoadedPledge();
        g_userDb.LogoutAllChars();
        // Noise: LogoutAllChars and clan item checks can only enqueue work here. This message does not prove data was saved. UserDb reports the logout scheduling count.
        // Log.Add(LOG_NORMAL, "Save cached data.")
    }
}

void CSocket::OnRead() {
    L2_GUARD_FUNCTION();
    const auto* peerBytes = reinterpret_cast<const std::uint8_t*>(&m_address.s_addr);
    if (runtime::raw_atomic::LoadEnum32Relaxed(&m_mode) == SocketMode::SM_CLOSE) {
        Close();
        // Noise: an already-closed socket receiving another read callback is not a new failure. The close cause is reported at its origin.
        // runtime::process::ReportNamedCallStack(L"SM_CLOSE", 2, false)
        return;
    }
    // if (m_nPendingWrite.load(std::memory_order_relaxed) >= 0x1000001) {
    //     Close()
    //     Log.Add(
    //         LOG_ERROR,
    //         "IOBufferPool - %d / %d, PendingWrite %d bytes "
    //         "(%d buffer count)",
    //         GetIOBufferPoolCount(), GetIOBufferPoolTotalCount(), g_nSocketPendingWrite.load(std::memory_order_relaxed),
    //         g_nSocketPendingWrite.load(std::memory_order_relaxed) / 0x5000
    //     )
    //     return.
    // }
    const int pendingWrite = m_nPendingWrite.load(std::memory_order_relaxed);
    if (pendingWrite >= 0x1000001) {
        // Log.Add(LOG_ERROR,
        //     "[L2Server] Closing connection %llu from %u.%u.%u.%u: pending writes reached %d bytes (limit 16777216). Global pending writes: %d bytes. Buffer pool: %d free of %d total.",
        //     static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]), pendingWrite, g_nSocketPendingWrite.load(std::memory_order_relaxed), GetIOBufferPoolCount(), GetIOBufferPoolTotalCount())
        L2_LOG_ERROR(Log,
            "[L2Server] Closing connection %llu from %u.%u.%u.%u: pending writes reached %d bytes (limit 16777216). Global pending writes: %d bytes; buffer pool: %d free of %d total.",
            static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]), pendingWrite, g_nSocketPendingWrite.load(std::memory_order_relaxed), GetIOBufferPoolCount(), GetIOBufferPoolTotalCount());
        Close();
        return;
    }

    const int readSize = static_cast<int>(m_pReadBuf->m_dwSize);
    int packetOffset = 0;
    while (packetOffset < readSize) {
        if (runtime::raw_atomic::LoadEnum32Relaxed(&m_mode) == SocketMode::SM_READ_LEN) {
            if (readSize - packetOffset < 3) {
                Read(static_cast<std::uint32_t>(readSize - packetOffset));
                return;
            }

            const std::uint8_t* const header = m_pReadBuf->m_Buffer.data() + packetOffset;
            m_packetLength =
                static_cast<int>(static_cast<std::uint16_t>(header[0]) | (static_cast<std::uint16_t>(header[1]) << 8U)) - 3;
            runtime::process::SetCurrentDatabaseId(header[2]);
            if (m_packetLength < 1 || m_packetLength > 0x4ffd) {
                // Log.Add(
                //     LOG_ERROR,
                //     "socket %d: bad packet size %d",
                //     runtime::LegacySocketImportAddressToken(),
                //     m_packetLength
                // )
                // Log.Add(LOG_WARN,
                //     "[L2Server] Closing connection %llu from %u.%u.%u.%u: invalid incoming body length %d bytes (accepted range 1..20477).",
                //     static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]), m_packetLength)
                L2_LOG_WARN(Log,
                    "[L2Server] Closing connection %llu from %u.%u.%u.%u: invalid incoming body length %d bytes (accepted range 1..20477).",
                    static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]), m_packetLength);
                runtime::raw_atomic::StoreEnum32Relaxed(&m_mode, SocketMode::SM_CLOSE);
                Close();
                return;
            }
            runtime::raw_atomic::StoreEnum32Relaxed(&m_mode, SocketMode::SM_READ_BODY);
            packetOffset += 3;
        }

        if (runtime::raw_atomic::LoadEnum32Relaxed(&m_mode) != SocketMode::SM_READ_BODY) {
            // Log.Add(LOG_ERROR,
            //     "[L2Server] Closing connection %llu from %u.%u.%u.%u: packet processing found an unexpected read state.",
            //     static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]))
            L2_LOG_ERROR(Log,
                "[L2Server] Closing connection %llu from %u.%u.%u.%u: packet processing found an unexpected read state.",
                static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]));
            Close();
            return;
        }
        if (readSize - packetOffset < m_packetLength) {
            Read(static_cast<std::uint32_t>(readSize - packetOffset));
            return;
        }

        ++m_packetCount;
        if (m_packetCount == 0) {
            m_createdTick = runtime::GetTickCount();
        }

        std::uint8_t* const packet = m_pReadBuf->m_Buffer.data() + packetOffset;
        Decrypt(packet, static_cast<std::int64_t>(m_key), m_packetLength);
        m_key += static_cast<std::uint64_t>(m_packetLength);

        if (m_packetTable == gPacketTableFromServer.GetData()) {
            if (m_packetLength < 2) {
                // Log.Add(
                //     LOG_ERROR,
                //     "error. Packet checksum is different. "
                //     "protocol[%d], len[%d], check[%d]",
                //     static_cast<int>(packet[0]), m_packetLength, -1
                // )
                // Log.Add(LOG_WARN,
                //     "[L2Server] Closing connection %llu from %u.%u.%u.%u: body length %d is too short for the two-byte length trailer (protocol %u).",
                //     static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]), m_packetLength, static_cast<unsigned>(packet[0]))
                L2_LOG_WARN(Log,
                    "[L2Server] Closing connection %llu from %u.%u.%u.%u: body length %d is too short for the two-byte length trailer (protocol %u).",
                    static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]), m_packetLength, static_cast<unsigned>(packet[0]));
                runtime::raw_atomic::StoreEnum32Relaxed(&m_mode, SocketMode::SM_CLOSE);
                Close();
                return;
            }
            const int encodedLength = static_cast<int>(
                static_cast<std::uint16_t>(packet[m_packetLength - 2]) |
                (static_cast<std::uint16_t>(packet[m_packetLength - 1]) << 8U)
            );
            const int checkedLength = encodedLength - 3;
            if (checkedLength != m_packetLength) {
                // Log.Add(
                //     LOG_ERROR,
                //     "error. Packet checksum is different. "
                //     "protocol[%d], len[%d], check[%d]",
                //     static_cast<int>(packet[0]), m_packetLength, checkedLength
                // )
                // Log.Add(LOG_WARN,
                //     "[L2Server] Closing connection %llu from %u.%u.%u.%u: body length %d differs from trailer length %d after header adjustment (protocol %u).",
                //     static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]), m_packetLength, checkedLength, static_cast<unsigned>(packet[0]))
                L2_LOG_WARN(Log,
                    "[L2Server] Closing connection %llu from %u.%u.%u.%u: body length %d differs from trailer length %d after header adjustment (protocol %u).",
                    static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]), m_packetLength, checkedLength, static_cast<unsigned>(packet[0]));
                runtime::raw_atomic::StoreEnum32Relaxed(&m_mode, SocketMode::SM_CLOSE);
                Close();
                return;
            }
        }

        const std::uint8_t protocol = packet[0];
        if (protocol >= 235) {
            // Log.Add(LOG_WARN,
            //     "[L2Server] Closing connection %llu from %u.%u.%u.%u: unknown protocol %u (accepted range 0..234).",
            //     static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]), static_cast<unsigned>(protocol))
            L2_LOG_WARN(Log,
                "[L2Server] Closing connection %llu from %u.%u.%u.%u: unknown protocol %u (accepted range 0..234).",
                static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]), static_cast<unsigned>(protocol));
            runtime::raw_atomic::StoreEnum32Relaxed(&m_mode, SocketMode::SM_CLOSE);
            Close();
            return;
        }

        auto* const callback = new CServerPacket;
        int databaseId = runtime::process::CurrentDatabaseId();
        if (databaseId == 0) {
            databaseId = GetCacheDConfiguration().worldId;
        }
        callback->m_databaseId = databaseId;
        callback->m_pSocket = this;
        callback->m_pBuf = m_pReadBuf;
        callback->m_pFunc = m_packetTable[protocol];
        callback->SetPacketType(protocol);

        // Noise: this per-packet wall-clock trace has no request context. RequestPingOneAdenaDrop and its reply keep the diagnostic timing.
        // if (protocol == 0x6b) {
        //     Log.Add(LOG_NORMAL, "ping adena drop read %lld", static_cast<long long>(std::time(nullptr)))
        // }

        L2_ADD_REF(this, ObjectRefType::IoRetain);
        m_pReadBuf->AddRef();
        AccumulateIncomingPacket(protocol, packet, m_packetLength);
        CAbstractPacket::g_nPendingPacket.fetch_add(1, std::memory_order_seq_cst);
        callback->PostObject(packetOffset);

        packetOffset += m_packetLength;
        runtime::raw_atomic::StoreEnum32Relaxed(&m_mode, SocketMode::SM_READ_LEN);
    }
    Read(0);
}

CServerPacket::CServerPacket() : m_databaseId(0) {}

void CServerPacket::OnIOCallback(int success, std::uint32_t transferred, PortableOverlapped* overlapped) {
    runtime::process::SetCurrentDatabaseId(m_databaseId);
    CacheDCPacketEx::OnIOCallback(success, transferred, overlapped);
}

void Decrypt(std::uint8_t* buffer [[maybe_unused]], std::int64_t key [[maybe_unused]], int length [[maybe_unused]]) {
    L2_GUARD_FUNCTION();
}

namespace ScriptFileUtil::CacheToServer {

int AssembleFileInfo(char* buffer, int bufferSize) {
    L2_GUARD_FUNCTION();
    static constexpr std::array<const wchar_t*, 5> files{
        L"..\\Script\\npc_pch.txt",
        L"..\\Script\\manual_pch.txt",
        L"..\\Script\\category_pch.txt",
        L"..\\Script\\categorydata.txt",
        L"..\\Script\\Itemdata.txt",
    };

    int assembled = 0;
    for (const wchar_t* file : files) {
        const ByHandleFileInformation info = GetFileInfo(file);
        const int written =
            ::Assemble(buffer + assembled, bufferSize - assembled, "dd", info.nFileSizeHigh, info.nFileSizeLow);
        assembled += written;
    }
    return assembled;
}

} // namespace ScriptFileUtil::CacheToServer

bool DummyPacket(CSocket* socket [[maybe_unused]], const std::uint8_t* packet [[maybe_unused]]) {
    L2_GUARD_FUNCTION();
    return false;
}

bool DummyServerExPacket(CSocket* socket [[maybe_unused]], const std::uint8_t* packet [[maybe_unused]]) {
    L2_GUARD_FUNCTION();
    // Noise: RequestExPacket now reports the decoded protocol and no-op outcome. This context-free ERROR duplicated that record and also marked intentional no-op registrations as failures.
    // Log.Add(LOG_ERROR, "DummyServerExPacket called in Server Packet Ex")
    return false;
}

bool RequestDxPacket(CSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    std::uint16_t protocol = 0;
    const std::uint8_t* const payload = ::Disassemble(packet, "h", &protocol);
    DxPacketCallback callback = DummyDXPacket;
    if (protocol < 116) {
        callback = g_DxPacketTable.GetCallback(protocol);
    }
    if (callback == DummyDXPacket) {
        // Log.Add(LOG_ERROR,
        //     "[database protocol] Ignoring DX request %u: %s (supported IDs 0..115).",
        //     static_cast<unsigned int>(protocol),
        //     protocol < 116 ? "no handler is registered" : "protocol ID is outside the supported range")
        L2_LOG_ERROR(Log,
            "[database protocol] Ignoring DX request %u: %s (supported IDs 0..115).",
            static_cast<unsigned int>(protocol),
            protocol < 116 ? "no handler is registered" : "protocol ID is outside the supported range");
    }
    return callback(socket, payload);
}

bool RequestQueryPacket(CSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    std::uint16_t protocol = 0;
    const std::uint8_t* const payload = ::Disassemble(packet, "h", &protocol);
    QueryPacketCallback callback = DummyQueryPacket;
    if (protocol < 4) {
        callback = g_QueryPacketTable.GetCallback(protocol);
    }
    if (callback == DummyQueryPacket) {
        // Log.Add(LOG_ERROR,
        //     "[database protocol] Ignoring query request %u: %s (supported IDs 0..3).",
        //     static_cast<unsigned int>(protocol),
        //     protocol < 4 ? "no handler is registered" : "protocol ID is outside the supported range")
        L2_LOG_ERROR(Log,
            "[database protocol] Ignoring query request %u: %s (supported IDs 0..3).",
            static_cast<unsigned int>(protocol),
            protocol < 4 ? "no handler is registered" : "protocol ID is outside the supported range");
    }
    return callback(socket, payload);
}

bool RequestExPacket(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0045fea4 unwraps one little-endian uint16 and
    // selects the 461-entry extended callback table.
    L2_GUARD_FUNCTION();
    std::uint16_t protocol = 0;
    const std::uint8_t* const payload = ::Disassemble(packet, "h", &protocol);
    CacheDPacketCallback callback = DummyServerExPacket;
    if (protocol < 461) {
        callback = gPacketTableFromServerEx.GetCallback(protocol);
    }
    if (callback == DummyServerExPacket) {
        // Log.Add(protocol < 461 ? LOG_WARN : LOG_ERROR,
        //     "[database protocol] Ignoring extended request %u: %s (supported IDs 0..460).",
        //     static_cast<unsigned int>(protocol),
        //     protocol < 461 ? "current handler performs no work" : "protocol ID is outside the supported range")
        L2_LOG(Log, protocol < 461 ? LOG_WARN : LOG_ERROR,
            "[database protocol] Ignoring extended request %u: %s (supported IDs 0..460).",
            static_cast<unsigned int>(protocol),
            protocol < 461 ? "current handler performs no work" : "protocol ID is outside the supported range");
    }
    return callback(socket, payload);
}

bool LoadCharacterPacket(CSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int accountId = 0;
    int userSocketId = 0;
    ::Disassemble(packet, "dd", &accountId, &userSocketId);

    CAccountSP account = CAccount::Load(static_cast<unsigned int>(accountId));
    if (!account) {
        // Log.Add(LOG_ERROR, "[%s][%d][LoadCharacterPacket] invalid account", "CacheSocket.cpp", 0x6eb)
        // L2_LOG_ERROR(Log, "[%s][%d][LoadCharacterPacket] invalid account", "CacheSocket.cpp", 0x6eb)
        L2_LOG_ERROR(Log, "[characters] Cannot load character list: account %d unavailable. Sending empty list to socket %d.", accountId, userSocketId);
        const SYSTEMTIME emptyTime{};
        socket->Send("cdddbd", 1, userSocketId, 0, 0, static_cast<int>(sizeof(emptyTime)), &emptyTime, 0);
        return false;
    }

    std::optional<FindCharactersToDeletePredicate> deletionStorage;
    std::optional<CollectCharacterOrderPredicate> orderStorage;
    std::optional<std::array<int, 7>> freeToPlayStatesStorage;
    std::optional<FillAllCharactersInfo> characterInfoStorage;
    {
        L2_READ_GUARD(*account.operator->());
        const int weekPlayTime = account->GetWeekPlayTime();
        const SYSTEMTIME weekPlayTimeLastChanged = account->GetWeekPlayTimeLastChanged();
        auto& characters = account->GetChars();

        auto& deletion = deletionStorage.emplace(account->GetId());
        characters.FOR_ALL(&deletion, L"CacheSocket.cpp", 0x488);

        for (std::size_t index = 0; index < deletion.expiredCharacterIds.size(); ++index) {
            const std::uint32_t characterId = deletion.expiredCharacterIds[index];
            if (characterId == 0)
                continue;

            const int deletedLevel = account->DeleteCharacter(characterId);
            if (deletedLevel < 1) {
                // Log.Add(LOG_ERROR, "Real Character delete is fail.")
                // L2_LOG_ERROR(Log, "Real Character delete is fail.")
                L2_LOG_ERROR(Log, "[characters] Permanent deletion failed for character %u. Continuing account character-list processing.",
                    static_cast<unsigned int>(characterId));
                continue;
            }

            FriendIdSet friendIds;
            g_friendsDb.GetFriendIdList(characterId, friendIds);
            g_friendsDb.DeleteFriends(characterId, friendIds);

            CPacketHelperEx<20480> friendPacket;
            for (const unsigned int friendCharacterId : friendIds) {
                friendPacket.Assemble("d", static_cast<int>(friendCharacterId));
            }
            socket->Send(
                "cddb", 0x29, static_cast<int>(characterId), friendPacket.GetAssembledCount(), friendPacket.GetLength(),
                friendPacket.GetPacket()
            );

            int highestCharacterId = 0;
            int highestLevel = 0;
            DBConn connection(runtime::process::CurrentDatabaseId());
            connection.Bind(&highestCharacterId);
            connection.Bind(&highestLevel);
            constexpr const wchar_t* statement = L"{Call lin_ReloadAccountHighestExpAndLevel (?)}";
            connection.Prepare(statement);
            const int loadedAccountId = account->GetId();
            connection.BindParameter(&loadedAccountId);
            if (connection.ExecutePrepared(statement)) {
                connection.Fetch();
            }

            socket->Send(
                "cddddddd", 6, 0, loadedAccountId, static_cast<int>(characterId), deletedLevel, highestCharacterId, highestLevel,
                0
            );
            // Log.Add(LOG_NORMAL, "character is really deleted. id[%d]", static_cast<int>(characterId))
            // L2_LOG_INFO(Log, "character is really deleted. id[%d]", static_cast<int>(characterId))
            L2_LOG_INFO(Log, "[characters] Character %u permanently deleted from account %d.",
                static_cast<unsigned int>(characterId), loadedAccountId);
        }

        auto& order = orderStorage.emplace(userSocketId, deletion.hasError);
        characters.FOR_ALL(&order, L"CacheSocket.cpp", 0x4ef);
        auto& freeToPlayStates = freeToPlayStatesStorage.emplace();
        SortAndAssignFreeToPlayStates(order, freeToPlayStates);

        auto& characterInfo = characterInfoStorage.emplace(account->GetId(), deletion.latestCharacterId, freeToPlayStates, deletion.hasError);
        characters.FOR_ALL(&characterInfo, L"CacheSocket.cpp", 0x6e1);

        const int characterCount = account->GetCharCount();
        if (deletion.hasError) {
            socket->Send(
                "cdddbd", 1, userSocketId, characterCount, weekPlayTime, static_cast<int>(sizeof(weekPlayTimeLastChanged)),
                &weekPlayTimeLastChanged, 0
            );
        } else {
            socket->Send(
                "cdddbdb", 1, userSocketId, characterCount, weekPlayTime, static_cast<int>(sizeof(weekPlayTimeLastChanged)),
                &weekPlayTimeLastChanged, characterInfo.length, characterInfo.length, characterInfo.buffer.data()
            );
        }
    }
    return false;
}

bool CreateCharacterPacket(CSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::array<char16_t, 24> targetCharacterName{};
    std::array<char16_t, 24> targetAccountName{};
    unsigned int accountId = 0;
    int gender = 0;
    int race = 0;
    int characterClass = 0;
    int worldId = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    double hp = 0.0;
    double mp = 0.0;
    int faceIndex = 0;
    int hairShapeIndex = 0;
    int hairColorIndex = 0;
    int userSocketId = 0;
    int address1 = 0;
    int address2 = 0;
    int address3 = 0;
    int address4 = 0;
    int level = 0;
    long long experience = 0;
    ::Disassemble(
        packet, "SSddddddddffdddddddddQ", 0x30, targetCharacterName.data(), 0x30, targetAccountName.data(), &accountId,
        &gender, &race, &characterClass, &worldId, &x, &y, &z, &hp, &mp, &faceIndex, &hairShapeIndex, &hairColorIndex,
        &userSocketId, &address1, &address2, &address3, &address4, &level, &experience
    );

    std::wstring characterName = FixedUtf16ToWide(targetCharacterName);
    std::wstring accountName = FixedUtf16ToWide(targetAccountName);
    if (!CUser::CheckCharacterName(characterName.c_str(), GetCacheDConfiguration().country, false)) {
        socket->Send("cdd", 4, 3, userSocketId);
        return false;
    }

    CAccountSP account = CAccount::Load(accountId);
    if (!account) {
        // Log.Add(
        //     LOG_ERROR, "can not create char. account is not valid[%ls][%u]", accountName.c_str(), accountId
        // )
        // L2_LOG_ERROR(Log, "can not create char. account is not valid[%ls][%u]", accountName.c_str(), accountId
        // )
        L2_LOG_ERROR(Log, "[characters] Creation failed: account %u unavailable. Sending failure code 3 to socket %d.", accountId, userSocketId);
        socket->Send("cdd", 4, 3, userSocketId);
        return false;
    }

    if (account->GetCharCount() >= 7) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[CacheSocket.cpp][805] can not create char. too many "
        //     "chars [%d] account name[%ls]",
        //     account->GetCharCount(), account->GetName()
        // )
        // L2_LOG_ERROR(Log,
        //     "[CacheSocket.cpp][805] can not create char. too many "
        //     "chars [%d] account name[%ls]",
        //     account->GetCharCount(), account->GetName()
        // )
        L2_LOG_WARN(Log, "[characters] Creation rejected for account %u: character count %d reached the limit of 7. Sending failure code 3 to socket %d.",
            accountId, account->GetCharCount(), userSocketId);
        socket->Send("cdd", 4, 3, userSocketId);
        return false;
    }

    const std::wstring storedAccountNameStorage = Utf16ToWide(account->GetName());
    const wchar_t* const storedAccountName = storedAccountNameStorage.c_str();
    if (storedAccountName[0] != L'\0' && !EqualWideCaseInsensitive(storedAccountName, accountName.c_str())) {
        // Log.Add(
        //     LOG_ERROR, "DIFFERENT ACCOUNT!!! db[%ls] in[%ls] id[%u]", storedAccountName, accountName.c_str(),
        //     accountId
        // )
        // L2_LOG_ERROR(Log, "DIFFERENT ACCOUNT!!! db[%ls] in[%ls] id[%u]", storedAccountName, accountName.c_str(),
        //     accountId
        // )
        L2_LOG_WARN(Log, "[characters] Account name mismatch during creation for account %u: stored '%s', requested '%s'. Continuing with the stored name.",
            accountId, WideToUtf8(storedAccountName).c_str(), WideToUtf8(accountName).c_str());
        accountName.assign(storedAccountName);
    }

    int builder = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&builder);
    constexpr const wchar_t* builderStatement = L"{Call lin_GetBuilderAccount (? )}";
    connection.Prepare(builderStatement);
    connection.BindParameter(accountName.c_str());
    if (!connection.ExecutePrepared(builderStatement) || !connection.Fetch()) {
        builder = 0;
    }

    std::array<wchar_t, 20> address{};
    std::swprintf(address.data(), address.size(), L"%d.%d.%d.%d", address1, address2, address3, address4);

    const int characterId = g_userDb.CreateUser(
        characterName.data(), accountName.data(), accountId, 0, builder, gender, race, characterClass, worldId, x, y, z, hp,
        mp, 0, experience, level, 0, 0, 0, 0, faceIndex, hairShapeIndex, hairColorIndex, address.data()
    );
    if (characterId == 0) {
        socket->Send("cdd", 4, 2, userSocketId);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 4, 2, userSocketId);
        return false;
    }

    account->SetCharacter(user.get());
    if (account->GetCharCount() == 1) {
        g_newCharDB.Insert(account->GetId(), static_cast<int>(user->GetId()));
    }

    socket->Send("cddd", 3, static_cast<int>(user->GetId()), characterClass, userSocketId);
    GIP._Add(
        L"CMD_CharacterInfoData(%d, %d, %d, '%s', %d, %d, %d, "
        L"%d, %d, %d, %d, %d",
        static_cast<int>(accountId), GetCacheDConfiguration().worldId, static_cast<int>(user->GetId()),
        characterName.c_str(), characterClass, race, level, gender, 0, 0, 0, static_cast<int>(std::time(nullptr))
    );
    return false;
}

bool RequestLoadSummons(CSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int userSocketId = 0;
    int characterId = 0;
    ::Disassemble(packet, "dd", &userSocketId, &characterId);

    int summonClassId = 0;
    int summonDatabaseId = 0;
    int summonLevel = 0;
    int originalRemainTime = 0;
    int remainTime = 0;
    int experienceRate = 0;
    double hp = 0.0;
    double mp = 0.0;
    int consumeItemId = 0;
    int consumeItemCount = 0;
    int isSummoned = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&summonClassId);
    connection.Bind(&summonDatabaseId);
    connection.Bind(&summonLevel);
    connection.Bind(&originalRemainTime);
    connection.Bind(&remainTime);
    connection.Bind(&experienceRate);
    connection.Bind(&hp);
    connection.Bind(&mp);
    connection.Bind(&consumeItemId);
    connection.Bind(&consumeItemCount);
    connection.BindNullable(&isSummoned, nullptr);

    constexpr const wchar_t* statement = L"{Call lin_LoadUserSummon (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(statement)) {
        return false;
    }

    CPacketHelperEx<20480> summons;
    while (connection.Fetch()) {
        if (summonClassId <= 0)
            continue;
        summons.Assemble(
            "ddddddffddd", summonClassId, summonDatabaseId, summonLevel, originalRemainTime, remainTime, experienceRate, hp,
            mp, consumeItemId, consumeItemCount, isSummoned
        );
        if (summons.GetAssembledCount() > 11) {
            // Log.Add(LOG_ERROR, "Too much summons for char_id[%d][%d]", characterId, summonDatabaseId)
            // L2_LOG_ERROR(Log, "Too much summons for char_id[%d][%d]", characterId, summonDatabaseId)
            L2_LOG_WARN(Log, "[summons] Character %d has more than 11 summon entries; current summon ID %d. Continuing to assemble the reply.",
                characterId, summonDatabaseId);
        }
    }

    socket->Send("cddb", 8, userSocketId, summons.GetAssembledCount(), summons.GetLength(), summons.GetPacket());
    return false;
}

bool LoadItemsPacket(CSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int characterId = 0;
    unsigned int userSocketId = 0;
    int reloadForRaid = 0;
    ::Disassemble(packet, "ddd", &characterId, &userSocketId, &reloadForRaid);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x148, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x148, characterId)
        L2_LOG_ERROR(Log, "[items] Cannot load inventory: character %d unavailable (raid reload %d). Sending empty list to socket %u.",
            characterId, reloadForRaid, userSocketId);
        socket->Send("cdd", 2, userSocketId, 0);
        return false;
    }

    user->LoadQuest(socket, characterId, userSocketId);
    user->LoadOneDayReward(socket, userSocketId);
    user->LoadItemReuseDelay(socket, userSocketId);

    CWareHouseSP warehouse = reloadForRaid == 0 ? user->GetWareHouse(0, true) : user->GetWareHouseForRaid(0);
    if (!warehouse) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find warehouse, owner id[%d]", "CacheSocket.cpp", 0x169, characterId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find warehouse, owner id[%d]", "CacheSocket.cpp", 0x169, characterId
        // )
        L2_LOG_ERROR(Log, "[items] Cannot load items for character %d: inventory unavailable (raid reload %d). Sending empty list to socket %u.",
            characterId, reloadForRaid, userSocketId);
        socket->Send("cdd", 2, userSocketId, 0);
        return false;
    }

    {
        L2_WRITE_GUARD(*warehouse.operator->());
        warehouse->SendList(socket, userSocketId);
    }
    return false;
}

bool CreateInitItems(CSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int characterId = 0;
    unsigned int userSocketId = 0;
    unsigned int itemGroupCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "ddd", &characterId, &userSocketId, &itemGroupCount);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x706, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x706, characterId)
        L2_LOG_ERROR(Log, "[items] Cannot create initial items for character %d: character unavailable. Sending completion reply without creating items (socket %u).",
            characterId, userSocketId);
        socket->Send("cd", 5, userSocketId);
        return false;
    }

    CWareHouseSP warehouse = user->GetWareHouse(0, true);
    if (!warehouse) {
        socket->Send("cd", 5, userSocketId);
        // Diagnostic addition: this branch returns the same completion reply without creating items.
        L2_LOG_ERROR(Log, "[items] Initial item creation skipped for character %d: inventory unavailable. Completion reply was requested for socket %u.",
            characterId, userSocketId);
        return false;
    }

    {
        L2_WRITE_GUARD(*warehouse.operator->());
        if (itemGroupCount != 0) {
            const ItemOptionKey defaultOptionKey;
            const BaseAttribute defaultBaseAttribute;

            for (unsigned int groupIndex = 0; groupIndex < itemGroupCount; ++groupIndex) {
                int itemType = 0;
                long long amount = 0;
                int consumeType = 0;
                cursor = ::Disassemble(cursor, "dQd", &itemType, &amount, &consumeType);

                auto createItem = [&](long long createdAmount) {
                    CItemSP item = warehouse->MakeNewItem(
                        static_cast<unsigned int>(characterId), static_cast<unsigned int>(itemType), createdAmount, 0, 0, 0, 0,
                        0, 0, defaultOptionKey, 0, defaultBaseAttribute, 0, 0
                    );
                    if (item) {
                        warehouse->PushItem(item.get());
                    }
                };

                if (consumeType == 2 || consumeType == 3) {
                    createItem(amount);
                } else {
                    for (int itemIndex = 0; static_cast<long long>(itemIndex) < amount; ++itemIndex) {
                        createItem(1);
                    }
                }
            }

            std::array<int, static_cast<std::size_t>(ST_MAX)> equippedItemIds{};
            // The current prologue is the source spelling equivalent of
            // `int slots[63] = {-1}.`: only the first element is -1 before the
            // fixed 0xfc-byte packet copy, while the remaining elements are zero.
            equippedItemIds.front() = -1;
            ::Disassemble(cursor, "b", static_cast<int>(sizeof(equippedItemIds)), equippedItemIds.data());

            for (std::size_t slotIndex = 0; slotIndex < equippedItemIds.size(); ++slotIndex) {
                const int itemType = equippedItemIds[slotIndex];
                if (itemType == -1)
                    continue;

                // Current Cached.exe 0x0047bc63 calls through CWareHouse's
                // first post-MemoryObject virtual slot (+0x20): GetItemByType.
                // L2Server sends initial equipment class IDs in this tail, not
                // the database IDs assigned moments earlier by MakeNewItem.
                CItemSP item = warehouse->GetItemByType(itemType, 0);
                if (!item) {
                    // Log.Add(LOG_ERROR, "[%s][%d] Cannot find item. [%d]", "CacheSocket.cpp", 0x741, itemType)
                    // L2_LOG_ERROR(Log, "[%s][%d] Cannot find item. [%d]", "CacheSocket.cpp", 0x741, itemType)
                    L2_LOG_ERROR(Log, "[items] Cannot equip initial item type %d for character %d in slot %u: item unavailable. Skipping this slot.",
                        itemType, characterId, static_cast<unsigned int>(slotIndex));
                    continue;
                }
                user->SetEquip(static_cast<SlotType>(slotIndex), static_cast<int>(item->ItemID()));
            }
            user->Save();
        }

        user->SetCheckSumOnDB();
        socket->Send("cd", 5, userSocketId);
        user->RegisterInitialShortcut();
    }
    return false;
}

bool CSocket::DropItemPacket(CSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int x = 0;
    int y = 0;
    int z = 0;
    int creatureObjectId = 0;
    int characterId = 0;
    int itemObjectId = 0;
    int encodedSlotType = 0;
    long long amountToDrop = 0;
    short residenceGuardDrop = 0;
    short dropOnDeath = 0;
    int guardAngle = 0;
    int residenceId = 0;
    int residenceItemId = 0;
    int guardNpcId = 0;
    int guardType = 0;
    int guardCanMove = 0;

    const std::uint8_t* cursor = ::Disassemble(
        packet, "dddddddQhh", &x, &y, &z, &creatureObjectId, &characterId, &itemObjectId, &encodedSlotType, &amountToDrop,
        &residenceGuardDrop, &dropOnDeath
    );
    if (residenceGuardDrop != 0) {
        ::Disassemble(cursor, "dddddd", &guardAngle, &residenceId, &residenceItemId, &guardNpcId, &guardType, &guardCanMove);
    }
    // The paired current L2Server populates this with (slotType + 1) % 0x3f.
    // Cached.exe parses it but does not consult it in opcode 6.

    auto sendFailure = [&]() {
        SendDropItemFailure(
            socket, residenceGuardDrop, creatureObjectId, x, y, z, dropOnDeath, guardAngle, residenceItemId, residenceId, 0,
            itemObjectId
        );
    };

    if (amountToDrop < -1) {
        // Log.Add(
        //     LOG_ERROR,
        //     "amount is below 0, char id[%d], item id [%d], "
        //     "amount[%lld]",
        //     characterId, itemObjectId, amountToDrop
        // )
        // L2_LOG_ERROR(Log,
        //     "amount is below 0, char id[%d], item id [%d], "
        //     "amount[%lld]",
        //     characterId, itemObjectId, amountToDrop
        // )
        L2_LOG_WARN(Log, "[items] Drop rejected for character %d, item %d: amount %lld is below -1. Sending failure reply.",
            characterId, itemObjectId, amountToDrop);
        sendFailure();
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x24c, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x24c, characterId)
        L2_LOG_ERROR(Log, "[items] Cannot drop item %d (amount %lld): character %d unavailable. Sending failure reply.",
            itemObjectId, amountToDrop, characterId);
        sendFailure();
        return false;
    }

    // The current handler snapshots these before acquiring the warehouse.
    const std::int64_t actorId = user->GetId();
    const std::int64_t accountId = user->GetAccountID();
    const std::u16string characterName = std::u16string(user->GetCharName());
    const std::u16string accountName = std::u16string(user->GetAccountName());
    const int race = user->GetRace();
    const int gender = user->GetGender();
    const int characterClass = user->GetClass();
    const int level = user->GetLevel();

    CWareHouseSP warehouse = user->GetWareHouse(0, true);
    if (!warehouse) {
        sendFailure();
        return false;
    }

    std::optional<CItemSP> sourceItemStorage;
    std::optional<CItemSP> droppedItemStorage;
    {
        L2_WRITE_GUARD(*warehouse.operator->());
        if (warehouse->m_lateSavingItemsPending) {
            warehouse->SaveLateSavingItems();
        }

        auto& sourceItem = sourceItemStorage.emplace(warehouse->GetItem(itemObjectId));
        if (!sourceItem) {
            sendFailure();
            return false;
        }

        const std::int64_t originalAmount = sourceItem->Amount();
        if (sourceItem->Amount() < amountToDrop) {
            if (amountToDrop - sourceItem->Amount() > 5) {
                // Log.Add(
                //     LOG_ERROR,
                //     "[%s][%d] DropItemPacket real[%lld] try[%lld] "
                //     "type[%d], drop all.",
                //     "CacheSocket.cpp", 0x275, static_cast<long long>(sourceItem->Amount()), amountToDrop,
                //     static_cast<int>(sourceItem->ItemType())
                // )
                // L2_LOG_ERROR(Log,
                //     "[%s][%d] DropItemPacket real[%lld] try[%lld] "
                //     "type[%d], drop all.",
                //     "CacheSocket.cpp", 0x275, static_cast<long long>(sourceItem->Amount()), amountToDrop,
                //     static_cast<int>(sourceItem->ItemType())
                // )
                L2_LOG_WARN(Log, "[items] Drop amount exceeds inventory for character %d, item %d (type %d): requested %lld, available %lld. Limiting drop to available amount.",
                    characterId, itemObjectId, static_cast<int>(sourceItem->ItemType()), amountToDrop,
                    static_cast<long long>(sourceItem->Amount()));
            }
            amountToDrop = sourceItem->Amount();
        }

        auto& droppedItem = droppedItemStorage.emplace(warehouse->DropItem(sourceItem.get(), amountToDrop));
        const std::int64_t absoluteDropAmount = amountToDrop < 0 ? -amountToDrop : amountToDrop;
        const std::int64_t negativeDropAmount = -absoluteDropAmount;
        const std::int64_t remainingAmount = originalAmount + negativeDropAmount;

        auto writeDropAudit = [&](CItem& auditItem) {
            std::array<char16_t, 256> shapeShiftAndVariation{};
            std::array<char16_t, 256> baseAttributeText{};
            std::array<char16_t, 256> ensoulText{};
            auditItem.MakeShapeShiftAndVariationLogInfo(shapeShiftAndVariation.data());
            BaseAttribute baseAttribute(auditItem.Attribute());
            baseAttribute.GetLogString(baseAttributeText.data());
            // auditItem.MakeEnsoulLogInfo(ensoulText.data())
            auditItem.MakeEnsoulLogInfo(ensoulText.data(), ensoulText.size());

            const std::u16string targetShapeShift = std::u16string(shapeShiftAndVariation.data());
            const std::u16string targetBaseAttribute = std::u16string(baseAttributeText.data());
            const std::u16string targetEnsoul = std::u16string(ensoulText.data());

            CLogFormatter formatter;
            formatter.WriteAuditLog(
                0x38c, actorId, accountId, 0, 0, x, y, z, targetShapeShift.c_str(), targetBaseAttribute.c_str(),
                targetEnsoul.c_str(), race, gender, characterClass, level, 0, 0, auditItem.Enchant(), auditItem.ItemType(),
                negativeDropAmount, remainingAmount, characterName.c_str(), accountName.c_str(), nullptr, nullptr,
                sourceItem->ItemID()
            );
        };

        if (!droppedItem) {
            sendFailure();
            writeDropAudit(*sourceItem.get());
            return false;
        }

        int querySucceeded = 0;
        if (residenceGuardDrop == 0) {
            socket->Send(
                "cddddddQddQhh", 0x0f, creatureObjectId, x, y, z, sourceItem->OwnerID(), sourceItem->ItemID(),
                static_cast<long long>(absoluteDropAmount), droppedItem->OwnerID(), droppedItem->ItemID(),
                static_cast<long long>(droppedItem->Amount()), 0, static_cast<int>(dropOnDeath)
            );
        } else {
            DBConn connection(runtime::process::CurrentDatabaseId());
            constexpr const wchar_t* statement = L"{Call lin_InsertIntoResidenceGuard "
                                                 L"(?, ?, ?, ?, ?, ?, ?, ?, ?)}";
            connection.Prepare(statement);
            connection.BindParameter(&residenceId);
            connection.BindParameter(&residenceItemId);
            connection.BindParameter(&guardNpcId);
            connection.BindParameter(&guardType);
            connection.BindParameter(&guardCanMove);
            connection.BindParameter(&x);
            connection.BindParameter(&y);
            connection.BindParameter(&z);
            connection.BindParameter(&guardAngle);
            querySucceeded = connection.ExecutePrepared(statement) ? 1 : 0;

            socket->Send(
                "cddddddQddQhhdddh", 0x0f, creatureObjectId, x, y, z, sourceItem->OwnerID(), sourceItem->ItemID(),
                static_cast<long long>(absoluteDropAmount), droppedItem->OwnerID(), droppedItem->ItemID(),
                static_cast<long long>(droppedItem->Amount()), static_cast<int>(residenceGuardDrop),
                static_cast<int>(dropOnDeath), guardAngle, residenceItemId, residenceId, querySucceeded
            );
        }

        writeDropAudit(*droppedItem.get());
        if (droppedItem->ItemType() == 57U &&
            static_cast<std::uint64_t>(absoluteDropAmount) >= GetCacheDConfiguration().adenaLogLimit) {
            WriteLargeAdenaDropAudit(
                actorId, accountId, x, y, z, race, gender, characterClass, level, droppedItem->Enchant(),
                static_cast<int>(droppedItem->ItemType()), negativeDropAmount, remainingAmount, characterName, accountName,
                sourceItem->ItemID()
            );
        }

    }
    return false;
}

bool CSocket::SaveCharacter(CSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int reserved = 0;
    int builder = 0;
    int characterClass = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    double hp = 0.0;
    double mp = 0.0;
    double maxHp = 0.0;
    double maxMp = 0.0;
    double cp = 0.0;
    double maxCp = 0.0;
    long long sp = 0;
    long long experience = 0;
    // The target initializes this one field to one before Disassemble.
    int level = 1;
    int alignment = 0;
    int pkCount = 0;
    int duelCount = 0;
    int pkPardon = 0;
    int hairColor = 0;
    int hairShape = 0;
    int face = 0;
    int inVehicle = 0;
    int subjobId = 0;
    std::array<int, 4> subjobClass{};
    int dualClassId = 0;
    int ssqDawnRound = 0;
    int abilityPoints = 0;

    ::Disassemble(
        packet, "dddddddffffffQQddddddddddddddddd", &characterId, &reserved, &builder, &characterClass, &x, &y, &z, &hp, &mp,
        &maxHp, &maxMp, &cp, &maxCp, &sp, &experience, &level, &alignment, &pkCount, &duelCount, &pkPardon, &hairColor,
        &hairShape, &face, &inVehicle, &subjobId, &subjobClass[0], &subjobClass[1], &subjobClass[2], &subjobClass[3],
        &dualClassId, &ssqDawnRound, &abilityPoints
    );

    if (hp < 0.0 || mp < 0.0 || maxHp < 0.0 || maxMp < 0.0 || sp < 0 || experience < 0 || level < 0 || pkCount < 0 ||
        abilityPoints < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] Character save error[%d]. "
        //     "[%d][%f][%f][%f][%f][%lld][%lld]"
        //     "[%d][%d][%d][%d][%d][%d]",
        //     "CacheSocket.cpp", 0x7a0, characterId, reserved, hp, mp, maxHp, maxMp, sp, experience, level, alignment, pkCount,
        //     duelCount, pkPardon, abilityPoints
        // )
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] Character save error[%d]. "
        //     "[%d][%f][%f][%f][%f][%lld][%lld]"
        //     "[%d][%d][%d][%d][%d][%d]",
        //     "CacheSocket.cpp", 0x7a0, characterId, reserved, hp, mp, maxHp, maxMp, sp, experience, level, alignment, pkCount,
        //     duelCount, pkPardon, abilityPoints
        // )
        L2_LOG_WARN(Log, "[characters] Save rejected for character %d: negative value in required nonnegative fields. HP %f/%f, MP %f/%f, SP %lld, experience %lld, level %d, PK count %d, ability points %d. Sending failure reply.",
            characterId, hp, maxHp, mp, maxMp, sp, experience, level, pkCount, abilityPoints);
        socket->Send("chdd", 0xc6, 0xeb, characterId, 0);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "Character save error. Cannot find user. id[%d]", characterId)
        // L2_LOG_ERROR(Log, "Character save error. Cannot find user. id[%d]", characterId)
        L2_LOG_ERROR(Log, "[characters] Cannot save character %d: character unavailable. No save reply is sent.", characterId);
        return false;
    }

    CWareHouseSP warehouse = user->GetWareHouse(0, true);
    if (warehouse) {
        if (warehouse->m_unsavedItemsPending) {
            {
                L2_WRITE_GUARD(*warehouse.operator->());
                warehouse->SaveUnsavedItems();
            }
        }
        if (warehouse->m_lateSavingItemsPending) {
            {
                L2_WRITE_GUARD(*warehouse.operator->());
                warehouse->SaveLateSavingItems();
            }
        }
    }

    int oldLevel;
    int oldClass;
    int oldSubjobId;
    {
        L2_WRITE_GUARD(*user.operator->());
        oldLevel = user->GetLevel();
        oldClass = user->GetClass();
        oldSubjobId = user->m_subjobId;
        if (user->GetClass() != characterClass) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[Character Save1] CharName[%ls] "
            //     "m_nClass[%d] / nClass[%d]",
            //     user->GetCharName(), user->GetClass(), characterClass
            // )
            // L2_LOG_ERROR(Log,
            //     "[Character Save1] CharName[%ls] "
            //     "m_nClass[%d] / nClass[%d]",
            //     user->GetCharName(), user->GetClass(), characterClass
            // )
            L2_LOG_ERROR(Log, "[characters] Class mismatch while saving character %d: cached class %d, requested class %d. Applying the requested character state.",
                characterId, user->GetClass(), characterClass);
        }

        user->SetChanged(
            user->GetPledgeID(), builder, user->GetGender(), user->GetRace(), characterClass, x, y, z, hp, mp, maxHp, maxMp, sp,
            experience, level, alignment, static_cast<std::uint32_t>(pkCount), static_cast<std::uint32_t>(duelCount),
            static_cast<std::uint32_t>(pkPardon), hairColor, hairShape, face, inVehicle, subjobId, subjobClass[0],
            subjobClass[1], subjobClass[2], subjobClass[3], dualClassId, cp, maxCp, ssqDawnRound, abilityPoints, true
        );
    }

    user->Save();
    socket->Send("chdd", 0xc6, 0xeb, characterId, 1);
    user->ReloadHighestExpAndLevel();

    if (oldLevel != level) {
        const int previousLog = user->GetUserLog(1, oldLevel);
        const std::uint32_t sessionUseTime = (runtime::GetTickCount() - user->m_loginTick) / 1000U;
        const std::uint32_t totalUseTime = user->m_useTime + sessionUseTime;
        const int timeToChange = runtime::Int32FromBits(totalUseTime - static_cast<std::uint32_t>(previousLog));

        if (oldClass == characterClass) {
            user->AddUserLog(1, oldLevel, level, totalUseTime, static_cast<int>(sessionUseTime));

            const std::u16string characterName = std::u16string(user->GetCharName());
            const std::u16string accountName = std::u16string(user->GetAccountName());
            CLogFormatter formatter;
            formatter.WriteLog(
                0x32a, user->GetId(), user->GetAccountID(), 0, 0, user->GetX(), user->GetY(), user->GetZ(), nullptr, nullptr,
                nullptr, user->GetRace(), user->GetGender(), user->GetClass(), level, oldLevel, timeToChange,
                user->GetBuilder(), user->m_subjobId, user->m_dualClassId, 0, characterName.c_str(), accountName.c_str(),
                nullptr, nullptr, 0
            );

            if (runtime::SubtractInt32Wrapping(level, oldLevel) >= 2 && oldLevel > 9) {
                WriteSaveCharacterLevelJumpLog(*user.get(), level, oldLevel, timeToChange, user->m_subjobId);
            }
        }
    }

    if (oldClass != characterClass && oldSubjobId == subjobId) {
        const int previousLog = user->GetUserLog(2, oldClass);
        const std::uint32_t sessionUseTime = (runtime::GetTickCount() - user->m_loginTick) / 1000U;
        const std::uint32_t totalUseTime = user->m_useTime + sessionUseTime;
        const int timeToChange = runtime::Int32FromBits(totalUseTime - static_cast<std::uint32_t>(previousLog));
        user->AddUserLog(2, oldClass, characterClass, totalUseTime, static_cast<int>(sessionUseTime));
        WriteSaveCharacterClassChangeLog(*user.get(), level, oldClass, timeToChange, user->m_subjobId, user->m_dualClassId);
    }

    return false;
}

bool CSocket::SaveSkillUse(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x00462bf4. Extended leave-world request 333 uses
    // this four-dword owner, not primary RequestSaveSkillUse's five-dword
    // carrier containing an explicit save reason.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int subjobId = 0;
    int number = 0;
    int allJobNumber = 0;
    const std::uint8_t* payload = ::Disassemble(packet, "dddd", &characterId, &subjobId, &number, &allJobNumber);

    if (characterId < 1) {
        // Log.Add(LOG_ERROR, L"Error occurred in Save skill use time[%d]", characterId)
        // L2_LOG_ERROR(Log, L"Error occurred in Save skill use time[%d]", characterId)
        L2_LOG_WARN(Log, "[skills] Leave-world cooldown save rejected: character ID %d must be positive (subclass %d).", characterId, subjobId);
        return false;
    }
    if (number < 1 && allJobNumber < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Error occurred in Save skill use time[%d], number[%d], "
        //     L"nAllJobNumber[%d]",
        //     characterId, number, allJobNumber
        // )
        // L2_LOG_ERROR(Log,
        //     L"Error occurred in Save skill use time[%d], number[%d], "
        //     L"nAllJobNumber[%d]",
        //     characterId, number, allJobNumber
        // )
        L2_LOG_WARN(Log, "[skills] Leave-world cooldown save skipped for character %d, subclass %d: both entry counts are nonpositive (subclass %d, all classes %d).",
            characterId, subjobId, number, allJobNumber);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, L"[%S][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x842, characterId)
        // L2_LOG_ERROR(Log, L"[%S][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x842, characterId)
        L2_LOG_ERROR(Log, "[skills] Cannot save leave-world cooldowns for character %d, subclass %d: character unavailable. Save skipped.", characterId, subjobId);
        return false;
    }

    user->SetSkillsLastUse(payload, ReuseDelaySaveReason::SR_NORMAL, subjobId, number, allJobNumber);
    return false;
}

bool SaveCharacterPacket(CSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    return CSocket::SaveCharacter(socket, packet);
}

bool SaveItemsPacket(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int requestedCharacterId = 0;
    std::uint32_t itemCount = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &requestedCharacterId, &itemCount);

    if (itemCount >= 201U) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] invalid item count [%d].", "CacheSocket.cpp", 0x8ed,
        //     runtime::Int32FromBits(itemCount)
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] invalid item count [%d].", "CacheSocket.cpp", 0x8ed,
        //     runtime::Int32FromBits(itemCount)
        // )
        L2_LOG_WARN(Log, "[items] Item audit rejected for character %d: received %u entries, maximum 200.",
            requestedCharacterId, static_cast<unsigned int>(itemCount));
        return false;
    }

    std::int64_t adenaSum = 0;
    std::int64_t defaultPriceSum = 0;
    std::int64_t itemSum = 0;
    std::uint32_t packetAdenaItemId = 0;

    std::int64_t characterId = 0;
    std::int64_t accountId = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    int level = 0;
    int characterClass = 0;
    bool sessionLogFlag = false;
    std::u16string characterName;
    std::u16string accountName;
    CUserSP user;

    if (itemCount != 0U) {
        for (std::uint32_t index = 0; index < itemCount; ++index) {
            CItemDisassemblerFromL2Server item;
            cursor = item.DisassembleItemInfoImpl(cursor);
            if (item.m_nItemType == 57U) {
                adenaSum = runtime::AddInt64Wrapping(adenaSum, item.m_nAmount);
                packetAdenaItemId = item.m_nItemID;
            }
            itemSum = runtime::AddInt64Wrapping(itemSum, item.m_nAmount);

            const CReadItemData* itemData = g_readFile.FindItem(static_cast<int>(item.m_nItemType));
            if (itemData) {
                defaultPriceSum =
                    runtime::AddInt64Wrapping(defaultPriceSum, runtime::MultiplyInt64Wrapping(itemData->m_defaultPrice, item.m_nAmount));
            }
        }

        user = g_userDb.GetUser(requestedCharacterId, true, false, runtime::process::CurrentDatabaseId());
        if (!user) {
            // Log.Add(LOG_ERROR, "[%s][%d]cannot find user.", "CacheSocket.cpp", 0x944)
            // L2_LOG_ERROR(Log, "[%s][%d]cannot find user.", "CacheSocket.cpp", 0x944)
            L2_LOG_ERROR(Log, "[items] Cannot audit %u items for character %d: character unavailable. Audit skipped.",
                static_cast<unsigned int>(itemCount), requestedCharacterId);
            return false;
        }

        characterId = user->GetId();
        accountId = user->GetAccountID();
        characterName = std::u16string(user->GetCharName());
        accountName = std::u16string(user->GetAccountName());
        x = user->GetX();
        y = user->GetY();
        z = user->GetZ();
        level = user->GetLevel();
        characterClass = user->GetClass();
        sessionLogFlag = user->m_sessionLogFlag;

        CWareHouseSP inventory = user->GetWareHouse(0, true);
        if (inventory) {
            CItemSP actualAdena = inventory->GetAdena();
            if (actualAdena && actualAdena->Amount() != adenaSum) {
                WriteInvalidAdenaAudit(
                    characterId, accountId, x, y, z, level, characterClass, actualAdena->Amount(), actualAdena->ItemID(),
                    adenaSum, packetAdenaItemId, characterName, accountName
                );
            }
        }
    }

    // The paired current L2Server sender fixes this exact order. Its +0xce0
    // virtual is PDB-confirmed User::GetParty. Identifiers absent from both
    // PDBs remain named by their proven sender origin instead of guessed.
    int partyMetricC4 = 0;
    int partyMetric98 = 0;
    int privateStoreTime = 0;
    int inventoryItemCount = 0;
    int userMetric920 = 0;
    int sharedMetric3258 = 0;
    int userMetric928 = 0;
    int userField23c = 0;
    int sharedMetric3268 = 0;
    int artifactInventoryCount = 0;
    int userMetric930 = 0;
    ::Disassemble(
        cursor, "ddddddddddd", &partyMetricC4, &partyMetric98, &privateStoreTime, &inventoryItemCount, &userMetric920,
        &sharedMetric3258, &userMetric928, &userField23c, &sharedMetric3268, &artifactInventoryCount, &userMetric930
    );

    int playDiff = 0;
    std::int64_t expDiff = 0;
    if (user) {
        user->CharAudit(adenaSum, defaultPriceSum, itemSum);
        playDiff = user->GetPlayDiff();
        expDiff = user->GetExpDiff();
    }

    CLogFormatter formatter;
    formatter.WriteLog(
        0x32d, characterId, accountId, 0, 0, x, y, z, nullptr, nullptr, nullptr, adenaSum, defaultPriceSum, itemSum, level,
        characterClass, playDiff, expDiff, partyMetricC4, partyMetric98, privateStoreTime, characterName.c_str(),
        accountName.c_str(), nullptr, nullptr, sessionLogFlag ? 1 : 0
    );
    formatter.WriteLog(
        0x1f7d, characterId, accountId, 0, 0, x, y, z, nullptr, nullptr, nullptr, inventoryItemCount, userMetric920,
        sharedMetric3258, userMetric928, userField23c, sharedMetric3268, artifactInventoryCount, userMetric930, 0, 0,
        characterName.c_str(), accountName.c_str(), nullptr, nullptr, 0
    );
    return false;
}

bool DeleteCharacterPacket(CSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int accountId = 0;
    unsigned int characterId = 0;
    int userSocketId = 0;
    ::Disassemble(packet, "ddd", &accountId, &characterId, &userSocketId);

    CUserSP user = g_userDb.GetUser(static_cast<int>(characterId), true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x41d, static_cast<int>(characterId)
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x41d, static_cast<int>(characterId)
        // )
        L2_LOG_ERROR(Log, "[characters] Deletion failed: character %u unavailable (account %d). Sending failure code 1 to socket %d.",
            characterId, accountId, userSocketId);
        socket->Send("cdd", 7, userSocketId, 1);
        return false;
    }

    const std::uint32_t pledgeId = user->GetPledgeID();
    if (pledgeId != 0U) {
        CPledgeSP pledge = g_pledgeDb.GetPledge(static_cast<int>(pledgeId), false, runtime::process::CurrentDatabaseId());
        if (pledge && pledge->rullerId() == user->GetId()) {
            socket->Send("cdd", 7, userSocketId, 3);
            return false;
        }
        socket->Send("cdd", 7, userSocketId, 2);
        return false;
    }

    DBConn gateConnection(runtime::process::CurrentDatabaseId());
    int blockingValue = 0;
    gateConnection.Bind(&blockingValue);
    constexpr const wchar_t* commissionStatement = L"{Call lin_RegTotalCountCommission(?)}";
    gateConnection.Prepare(commissionStatement);
    gateConnection.BindParameter(&characterId);
    if (gateConnection.ExecutePrepared(commissionStatement) && gateConnection.Fetch() && blockingValue != 0) {
        socket->Send("cdd", 7, userSocketId, 5);
        return false;
    }

    blockingValue = 0;
    gateConnection.UnBind();
    gateConnection.Bind(&blockingValue);
    constexpr const wchar_t* sentPostStatement = L"{Call lin_LoadSendPostBySenderId (?)}";
    gateConnection.Prepare(sentPostStatement);
    gateConnection.BindParameter(&characterId);
    if (gateConnection.ExecutePrepared(sentPostStatement) && gateConnection.Fetch() && blockingValue != 0) {
        socket->Send("cdd", 7, userSocketId, 8);
        return false;
    }

    blockingValue = 0;
    gateConnection.UnBind();
    gateConnection.Bind(&blockingValue);
    constexpr const wchar_t* menteeStatement = L"{Call lin_IsMentee (?)}";
    gateConnection.Prepare(menteeStatement);
    gateConnection.BindParameter(&characterId);
    if (gateConnection.ExecutePrepared(menteeStatement) && gateConnection.Fetch() && blockingValue != 0) {
        socket->Send("cdd", 7, userSocketId, 6);
        return false;
    }

    blockingValue = 0;
    gateConnection.UnBind();
    gateConnection.Bind(&blockingValue);
    constexpr const wchar_t* mentorStatement = L"{Call lin_IsMentor (?)}";
    gateConnection.Prepare(mentorStatement);
    gateConnection.BindParameter(&characterId);
    if (gateConnection.ExecutePrepared(mentorStatement) && gateConnection.Fetch() && blockingValue != 0) {
        socket->Send("cdd", 7, userSocketId, 6);
        return false;
    }

    CPledgeJoinDB& pledgeJoinDb = CPledgeJoinDB::Instance();
    const int worldId = GetCacheDConfiguration().worldId;
    if (pledgeJoinDb.IsWaitingUserRegistered(worldId, static_cast<int>(characterId))) {
        DBConn waitingDeletion(runtime::process::CurrentDatabaseId());
        int waitingPledgeId = 0;
        waitingDeletion.Bind(&waitingPledgeId);
        constexpr const wchar_t* waitingStatement = L"{Call lin_DeletePledgeWaitingUser (?,?)}";
        waitingDeletion.Prepare(waitingStatement);
        waitingDeletion.BindParameter(&worldId);
        waitingDeletion.BindParameter(&characterId);
        if (waitingDeletion.ExecutePrepared(waitingStatement)) {
            // Current 0x0049cca4 passes the earlier gate DBConn at rbp-0x20,
            // not waitingDeletion at rbp+0x440. The previous mentor row has
            // already been fetched, so this normally performs zero updates.
            while (gateConnection.Fetch()) {
                pledgeJoinDb.DecresePledgeWaitUserCount(waitingPledgeId);
            }
        }
    }

    if (pledgeJoinDb.IsDraftUserRegistered(worldId, static_cast<int>(characterId))) {
        DBConn draftDeletion(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* draftStatement = L"{Call lin_DeletePledgeDraftUser (?,?)}";
        draftDeletion.Prepare(draftStatement);
        draftDeletion.BindParameter(&worldId);
        draftDeletion.BindParameter(&characterId);
        draftDeletion.ExecutePrepared(draftStatement);
    }

    if (user->GetBuilder() < 1) {
        if (!user->SetDeleted()) {
            // Log.Add(
            //     LOG_ERROR, "[%s][%d]Cannot delete, id[%d]", "CacheSocket.cpp", 0x416, static_cast<int>(characterId)
            // )
            // L2_LOG_ERROR(Log, "[%s][%d]Cannot delete, id[%d]", "CacheSocket.cpp", 0x416, static_cast<int>(characterId)
            // )
            L2_LOG_ERROR(Log, "[characters] Cannot mark character %u for deletion (account %d). Sending failure code 1 to socket %d.",
                characterId, accountId, userSocketId);
            socket->Send("cdd", 7, userSocketId, 1);
            return false;
        }

        socket->Send("cddddddd", 6, userSocketId, accountId, static_cast<int>(user->GetId()), user->GetLevel(), -1, -1, 1);
        return false;
    }

    CAccountSP account = CAccount::Load(static_cast<unsigned int>(accountId));
    if (!account || account->DeleteCharacter(characterId) < 1) {
        // The current builder-character branch sends no failure packet here.
        return false;
    }

    FriendIdSet friendIds;
    g_friendsDb.GetFriendIdList(user->GetId(), friendIds);
    g_friendsDb.DeleteFriends(user->GetId(), friendIds);

    int highestCharacterId = 0;
    int highestLevel = 0;
    DBConn reloadConnection(runtime::process::CurrentDatabaseId());
    reloadConnection.Bind(&highestCharacterId);
    reloadConnection.Bind(&highestLevel);
    constexpr const wchar_t* reloadStatement = L"{Call lin_ReloadAccountHighestExpAndLevel (?)}";
    reloadConnection.Prepare(reloadStatement);
    const unsigned int accountIdBits = static_cast<unsigned int>(accountId);
    reloadConnection.BindParameter(&accountIdBits);
    if (reloadConnection.ExecutePrepared(reloadStatement)) {
        reloadConnection.Fetch();
    }

    socket->Send(
        "cddddddd", 6, userSocketId, accountId, static_cast<int>(user->GetId()), user->GetLevel(), highestCharacterId,
        highestLevel, 1
    );
    return false;
}

bool RequestUpdateUserPledge(CSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int userSocketId = 0;
    int pledgeSocketId = 0;
    int updateType = 0;
    int characterId = 0;
    int pledgeId = 0;
    PledgeType pledgeType = MASTER_PLEDGE;
    ::Disassemble(packet, "dddddd", &userSocketId, &pledgeSocketId, &updateType, &characterId, &pledgeId, &pledgeType);

    std::array<char16_t, 24> pledgeName{};
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (pledge) {
        L2_READ_GUARD(*pledge.operator->());
        std::copy_n(pledge->GetName(), pledgeName.size(), pledgeName.begin());
    }

    bool success = false;
    if (updateType == 1) {
        success = g_pledgeDb.JoinPledgeMember(
            static_cast<std::uint32_t>(pledgeId), static_cast<std::uint32_t>(characterId), pledgeType
        );
    } else if (updateType == 2) {
        success = g_pledgeDb.WithdrawPledgeMember(
            static_cast<std::uint32_t>(pledgeId), static_cast<std::uint32_t>(characterId), pledgeType
        );
    }

    socket->Send("cddddd", 0x2e, userSocketId, pledgeSocketId, static_cast<int>(pledgeType), updateType, success ? 1 : 0);
    if (!success)
        return false;

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        const std::u16string characterName = std::u16string(user->GetCharName());
        const std::u16string accountName = std::u16string(user->GetAccountName());
        CLogFormatter formatter;
        formatter.WriteLog(
            updateType == 1 ? 0xca : 0xcc, user->GetId(), user->GetAccountID(), 0, 0, user->GetX(), user->GetY(),
            user->GetZ(), pledgeName.data(), u"", u"", user->GetRace(), user->GetGender(), user->GetClass(),
            user->GetLevel(), static_cast<int>(pledgeType), pledgeId, 0, 0, 0, 0, characterName.c_str(), accountName.c_str(),
            u"", u"", 0
        );

        if (updateType == 1) {
            // GIP._Add(
            //     L"CMD_JoinClan(%d, %d, %d, %d, %d, '%s', %d", GetCacheDConfiguration().worldId, pledgeId,
            //     static_cast<std::int64_t>(std::time(nullptr)), user->GetAccountID(), user->GetId(), user->GetCharName(),
            //     static_cast<int>(pledgeType)
            // )
            GIP._Add(
                L"CMD_JoinClan(%d, %d, %d, %d, %d, '%s', %d", GetCacheDConfiguration().worldId, pledgeId,
                static_cast<std::int64_t>(std::time(nullptr)), user->GetAccountID(), user->GetId(), Utf16ToWide(user->GetCharName()).c_str(),
                static_cast<int>(pledgeType)
            );
        } else {
            // GIP._Add(
            //     L"CMD_WithdrawClan(%d, %d, %d, %d, %d, '%s', %d", GetCacheDConfiguration().worldId, pledgeId,
            //     static_cast<std::int64_t>(std::time(nullptr)), user->GetAccountID(), user->GetId(), user->GetCharName(), 1
            // )
            GIP._Add(
                L"CMD_WithdrawClan(%d, %d, %d, %d, %d, '%s', %d", GetCacheDConfiguration().worldId, pledgeId,
                static_cast<std::int64_t>(std::time(nullptr)), user->GetAccountID(), user->GetId(), Utf16ToWide(user->GetCharName()).c_str(), 1
            );
        }
    }

    if (updateType == 1) {
        PledgeContributionInCache::AddInfo(pledgeId, characterId);
    } else if (!PledgeContributionInCache::DeleteInfo(characterId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "PLEDGE[CacheSocket.cpp][%d]Failed delete contribution "
        //     "info: %d",
        //     0xa0d, characterId
        // )
        // L2_LOG_ERROR(Log,
        //     "PLEDGE[CacheSocket.cpp][%d]Failed delete contribution "
        //     "info: %d",
        //     0xa0d, characterId
        // )
        L2_LOG_ERROR(Log, "[clans] Contribution cleanup failed for character %d after withdrawal from clan %d. Membership success reply was already requested.",
            characterId, pledgeId);
    }
    return false;
}

bool RequestOustPledgeMember(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00465308.
    L2_GUARD_FUNCTION();

    int userSocketId = 0;
    int pledgeSocketId = 0;
    int pledgeId = 0;
    std::array<char16_t, 24> targetCharacterName{};
    ::Disassemble(packet, "dddS", &userSocketId, &pledgeSocketId, &pledgeId, 0x30, targetCharacterName.data());

    const std::wstring characterName = Utf16ToWide(std::u16string_view(targetCharacterName.data()));
    const std::uint32_t oustedMemberId =
        g_pledgeDb.OustPledgeMember(static_cast<std::uint32_t>(pledgeId), characterName.c_str());

    if (oustedMemberId != 0U) {
        CUserSP user = g_userDb.GetUser(static_cast<int>(oustedMemberId), true, false, runtime::process::CurrentDatabaseId());
        if (user) {
            const PledgeType oldPledgeType = user->GetPledgeType();
            user->SetPledge(0, MASTER_PLEDGE);
            // Current 0x004654e6 writes CUser+0x614 directly.
            user->m_gradeId = 0;

            std::array<char16_t, 24> pledgeName{};
            CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
            if (pledge) {
                L2_READ_GUARD(*pledge.operator->());
                std::copy_n(pledge->GetName(), pledgeName.size(), pledgeName.begin());
            }

            const std::u16string loadedCharacterName = std::u16string(user->GetCharName());
            const std::u16string accountName = std::u16string(user->GetAccountName());
            CLogFormatter formatter;
            formatter.WriteLog(
                0xcd, user->GetId(), user->GetAccountID(), 0, 0, user->GetX(), user->GetY(), user->GetZ(), pledgeName.data(),
                u"", u"", user->GetRace(), user->GetGender(), user->GetClass(), user->GetLevel(),
                static_cast<int>(oldPledgeType), pledgeId, 0, 0, 0, 0, loadedCharacterName.c_str(), accountName.c_str(), u"",
                u"", 0
            );

            // GIP._Add(
            //     L"CMD_WithdrawClan(%d, %d, %d, %d, %d, '%s', %d", GetCacheDConfiguration().worldId, pledgeId,
            //     static_cast<std::int64_t>(std::time(nullptr)), user->GetAccountID(), user->GetId(), user->GetCharName(), 0
            // )
            GIP._Add(
                L"CMD_WithdrawClan(%d, %d, %d, %d, %d, '%s', %d", GetCacheDConfiguration().worldId, pledgeId,
                static_cast<std::int64_t>(std::time(nullptr)), user->GetAccountID(), user->GetId(), Utf16ToWide(user->GetCharName()).c_str(), 0
            );

            socket->Send(
                "cddddS", 0x17, userSocketId, pledgeSocketId, static_cast<int>(user->GetPledgeType()),
                static_cast<int>(oustedMemberId), targetCharacterName.data()
            );
            if (!PledgeContributionInCache::DeleteInfo(static_cast<int>(oustedMemberId))) {
                // Log.Add(
                //     LOG_ERROR,
                //     "PLEDGE[CacheSocket.cpp][%d]Failed delete "
                //     "contribution info: %u",
                //     0xf8b, oustedMemberId
                // )
                // L2_LOG_ERROR(Log,
                //     "PLEDGE[CacheSocket.cpp][%d]Failed delete "
                //     "contribution info: %u",
                //     0xf8b, oustedMemberId
                // )
                L2_LOG_ERROR(Log, "[clans] Contribution cleanup failed for character %u after removal from clan %d. Member-removal reply was already requested.",
                    static_cast<unsigned int>(oustedMemberId), pledgeId);
            }
            return false;
        }

        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%u]", "CacheSocket.cpp", 0xf92, oustedMemberId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%u]", "CacheSocket.cpp", 0xf92, oustedMemberId)
        L2_LOG_ERROR(Log, "[clans] Character %u unavailable after removal from clan %d. Cached membership update skipped; replying with pledge type 0.",
            static_cast<unsigned int>(oustedMemberId), pledgeId);
    }

    socket->Send(
        "cddddS", 0x17, userSocketId, pledgeSocketId, 0, static_cast<int>(oustedMemberId), targetCharacterName.data()
    );
    return false;
}

bool RequestTrade(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00469a18.
    L2_GUARD_FUNCTION();

    int sourceCharacterId = 0;
    int targetCharacterId = 0;
    const std::uint8_t* tradePayload = ::Disassemble(packet, "dd", &sourceCharacterId, &targetCharacterId);

    // The shipping handler performs this read and discards its result before
    // passing the unchanged payload to CWareHouse::TradeItems.
    int discardedFirstDirectionCount = 0;
    ::Disassemble(tradePayload, "d", &discardedFirstDirectionCount);

    CUserSP sourceUser = g_userDb.GetUser(sourceCharacterId, true, false, runtime::process::CurrentDatabaseId());
    CUserSP targetUser = g_userDb.GetUser(targetCharacterId, true, false, runtime::process::CurrentDatabaseId());
    if (!sourceUser || !targetUser) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1670, sourceCharacterId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1670, sourceCharacterId
        // )
        L2_LOG_ERROR(Log, "[trade] Cannot exchange items: source character %d %s, target character %d %s. No reply is sent.",
            sourceCharacterId, sourceUser ? "available" : "unavailable", targetCharacterId, targetUser ? "available" : "unavailable");
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1671, targetCharacterId
        // )
        // Noise: the combined diagnostic identifies both participants without falsely reporting both missing.
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1671, targetCharacterId
        // )
        return false;
    }

    CWareHouseSP sourceInventory = sourceUser->GetWareHouse(WareHouseType_Inventory, true);
    CWareHouseSP targetInventory = targetUser->GetWareHouse(WareHouseType_Inventory, true);
    // if (!sourceInventory || !targetInventory)
    //     return false
    if (!sourceInventory || !targetInventory) {
        // Diagnostic addition: report the storage failure before abandoning the trade.
        L2_LOG_ERROR(Log, "[trade] Cannot exchange items: source character %d inventory %s, target character %d inventory %s. No reply is sent.",
            sourceCharacterId, sourceInventory ? "available" : "unavailable", targetCharacterId, targetInventory ? "available" : "unavailable");
        return false;
    }

    auto savePendingAmounts = [&]() {
        if (sourceInventory->m_lateSavingItemsPending) {
            sourceInventory->SaveLateSavingItems();
        }
        if (targetInventory->m_lateSavingItemsPending) {
            targetInventory->SaveLateSavingItems();
        }
    };
    auto executeTrade = [&]() {
        savePendingAmounts();
        sourceInventory->TradeItems(socket, tradePayload, targetInventory.get());
    };

    const std::uint64_t sourceLockAddress = sourceInventory->GetLockAddr();
    const std::uint64_t targetLockAddress = targetInventory->GetLockAddr();
    if (sourceLockAddress == targetLockAddress) {
        // Log.Add(LOG_ERROR, "Same warehouse double writelock?-_-,")
        // L2_LOG_ERROR(Log, "Same warehouse double writelock?-_-,")
        L2_LOG_WARN(Log, "[trade] Source character %d and target character %d inventories share a lock. Continuing exchange with one write lock.",
            sourceCharacterId, targetCharacterId);
        {
            L2_WRITE_GUARD(*sourceInventory.operator->());
            executeTrade();
        }
        return false;
    }

    if (sourceLockAddress < targetLockAddress) {
        {
            L2_WRITE_GUARD(*sourceInventory.operator->());
            {
                L2_WRITE_GUARD(*targetInventory.operator->());
                executeTrade();
            }
        }
        return false;
    }

    {
        L2_WRITE_GUARD(*targetInventory.operator->());
        {
            L2_WRITE_GUARD(*sourceInventory.operator->());
            executeTrade();
        }
    }
    return false;
}

bool RequestSetPledgeCrest(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00465b64.
    L2_GUARD_FUNCTION();

    int firstRequestId = 0;
    int secondRequestId = 0;
    int pledgeId = 0;
    int bitmapSize = 0;
    const std::uint8_t* const bitmap =
        ::Disassemble(packet, "dddd", &firstRequestId, &secondRequestId, &pledgeId, &bitmapSize);

    if (bitmapSize == 0) {
        socket->Send("cddd", 0x30, firstRequestId, secondRequestId, 0);
        return false;
    }

    const std::uint32_t newCrestId = g_pledgeCrestDb.CreatePledgeCrest(bitmapSize, bitmap);
    if (newCrestId != 0) {
        std::uint32_t rulerId = 0;
        std::array<char16_t, 24> pledgeName{};
        CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
        if (pledge) {
            L2_READ_GUARD(*pledge.operator->());
            std::copy_n(pledge->GetName(), pledgeName.size(), pledgeName.begin());
            rulerId = pledge->rullerId();
        }

        CUserSP ruler = g_userDb.GetUser(static_cast<int>(rulerId), true, false, runtime::process::CurrentDatabaseId());
        if (ruler) {
            WriteSetPledgeCrestAudit(*ruler.get(), pledgeName.data(), pledgeId);
        }
    }

    socket->Send("cddd", 0x30, firstRequestId, secondRequestId, static_cast<int>(newCrestId));
    return false;
}

bool RequestSetUserNickname(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00465f64.
    L2_GUARD_FUNCTION();

    int masterServerId = 0;
    int targetServerId = 0;
    std::uint32_t masterCharacterId = 0;
    std::uint32_t targetCharacterId = 0;
    std::array<char16_t, 24> nickname{};
    ::Disassemble(
        packet, "ddddS", &masterServerId, &targetServerId, &masterCharacterId, &targetCharacterId, 0x30, nickname.data()
    );

    std::wstring databaseNickname = FixedUtf16ToWide(nickname);
    if (!g_userDb.SetUserNickname(targetCharacterId, databaseNickname.data())) {
        socket->Send("cddSd", 0x34, masterServerId, targetServerId, nickname.data(), 0);
        return false;
    }

    // The current handler replies before materializing users or writing the
    // audit record. Missing cached objects therefore never change success.
    socket->Send("cddSd", 0x34, masterServerId, targetServerId, nickname.data(), 1);

    const int worldId = runtime::process::CurrentDatabaseId();
    CUserSP master = g_userDb.GetUser(static_cast<int>(masterCharacterId), true, false, worldId);
    CUserSP target = g_userDb.GetUser(static_cast<int>(targetCharacterId), true, false, worldId);
    if (!master || !target)
        return false;

    std::array<char16_t, 24> pledgeName{};
    int pledgeId = 0;
    CPledgeSP pledge = g_pledgeDb.GetPledge(static_cast<int>(target->GetPledgeID()), false, worldId);
    if (pledge) {
        std::copy_n(pledge->GetName(), pledgeName.size(), pledgeName.begin());
        pledgeId = static_cast<int>(pledge->GetId());
    }

    WriteSetUserNicknameAudit(*target.get(), *master.get(), nickname.data(), pledgeName.data(), pledgeId);
    return false;
}

bool RequestAquireSkill(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x004687b0.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int skillId = 0;
    int skillLevel = 0;
    int skillSubLevel = 0;
    int force = 0;
    int subjobId = -1;
    ::Disassemble(packet, "dddddd", &characterId, &skillId, &skillLevel, &skillSubLevel, &force, &subjobId);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1454, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1454, characterId)
        L2_LOG_ERROR(Log, "[skills] Cannot acquire skill %d (level %d, sublevel %d) for character %d, subclass %d: character unavailable. Update skipped.",
            skillId, skillLevel, skillSubLevel, characterId, subjobId);
        return false;
    }

    if (user->SetAquireSkill(skillId, skillLevel, skillSubLevel, force != 0, subjobId)) {
        WriteAquireSkillAudit(*user.get(), skillId, skillLevel, skillSubLevel);
    }
    return false;
}

bool RequestDestroyItem(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0047be5c.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int petId = 0;
    const std::uint8_t* const itemPacket = ::Disassemble(packet, "dd", &characterId, &petId);

    auto sendFailure = [&]() {
        int itemId = 0;
        int itemServerId = 0;
        int consumeType = 0;
        int userServerId = 0;
        std::int64_t amount = 0;
        ::Disassemble(itemPacket, "ddddQ", &itemId, &itemServerId, &consumeType, &userServerId, &amount);
        socket->Send("cddQ", 0x1a, userServerId, itemServerId, static_cast<long long>(-1));
    };

    const int worldId = runtime::process::CurrentDatabaseId();
    CUserSP user = g_userDb.GetUser(characterId, true, false, worldId);
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1475, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1475, characterId)
        L2_LOG_ERROR(Log, "[items] Destruction request failed: character %d unavailable (pet context %d). Sending failure reply.", characterId, petId);
        sendFailure();
        return false;
    }

    if (petId < 1) {
        CWareHouseSP inventory = user->GetWareHouse(WareHouseType_Inventory, true);
        if (!inventory) {
            // Log.Add(
            //     LOG_ERROR, "[%s][%d]Cannot find warehouse [0], id[%d]", "CacheSocket.cpp", 0x1493, characterId
            // )
            // L2_LOG_ERROR(Log, "[%s][%d]Cannot find warehouse [0], id[%d]", "CacheSocket.cpp", 0x1493, characterId
            // )
            L2_LOG_ERROR(Log, "[items] Destruction request failed for character %d: inventory unavailable. Sending failure reply.", characterId);
            sendFailure();
            return false;
        }

        {
            L2_WRITE_GUARD(*inventory.operator->());
            inventory->DestroyItem(socket, itemPacket, false);
        }
        return false;
    }

    CPetSP pet = g_petDb.GetPet(petId, worldId);
    if (!pet) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find pet, id[%d], userID[%d]", "CacheSocket.cpp", 0x147e, petId, characterId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find pet, id[%d], userID[%d]", "CacheSocket.cpp", 0x147e, petId, characterId
        // )
        L2_LOG_ERROR(Log, "[items] Destruction request failed for character %d: pet %d unavailable. Sending failure reply.", characterId, petId);
        sendFailure();
        return false;
    }

    CPetWarehouseSP petWarehouse = pet->GetWarehouse();
    if (!petWarehouse) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find pet warehouse, id[%d], userID[%d]", "CacheSocket.cpp", 0x1485, petId,
        //     characterId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find pet warehouse, id[%d], userID[%d]", "CacheSocket.cpp", 0x1485, petId,
        //     characterId
        // )
        L2_LOG_ERROR(Log, "[items] Destruction request failed for character %d: pet %d inventory unavailable. Sending failure reply.", characterId, petId);
        sendFailure();
        return false;
    }

    {
        L2_WRITE_GUARD(*petWarehouse.operator->());
        petWarehouse->DestroyItem(socket, itemPacket, true);
    }
    return false;
}

bool RequestEnchantItem(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00468d4c.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    const std::uint8_t* const enchantPacket = ::Disassemble(packet, "d", &characterId);

    // Both current failure branches decode only the first eight fields of
    // the forwarded payload. So field four is the support item's
    // database ID, not its L2Server runtime ID. This is the shipping wire
    // behavior at 0x00468f33/0x00469084.
    auto sendUnavailableReply = [&](bool includeLuckyByte) {
        int scrollItemId = 0;
        int scrollServerId = 0;
        int scrollConsumeType = 0;
        int supportItemId = 0;
        int supportServerId = 0;
        int supportConsumeType = 0;
        int targetItemId = 0;
        int targetServerId = 0;
        ::Disassemble(
            enchantPacket, "dddddddd", &scrollItemId, &scrollServerId, &scrollConsumeType, &supportItemId, &supportServerId,
            &supportConsumeType, &targetItemId, &targetServerId
        );

        if (includeLuckyByte) {
            socket->Send(
                "cdQdQdddc", 0x1b, scrollServerId, static_cast<long long>(-1), supportItemId, static_cast<long long>(-1),
                targetServerId, -1, 0, 0
            );
            return;
        }
        socket->Send(
            "cdQdQddd", 0x1b, scrollServerId, static_cast<long long>(-1), supportItemId, static_cast<long long>(-1),
            targetServerId, -1, 0, 0
        );
    };

    const int worldId = runtime::process::CurrentDatabaseId();
    CUserSP user = g_userDb.GetUser(characterId, true, false, worldId);
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]cannot find user. User id[%d]", "CacheSocket.cpp", 0x14c7, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]cannot find user. User id[%d]", "CacheSocket.cpp", 0x14c7, characterId)
        L2_LOG_ERROR(Log, "[items] Enchantment request failed: character %d unavailable. Sending failure reply.", characterId);
        sendUnavailableReply(false);
        return false;
    }

    CWareHouseSP inventory = user->GetWareHouse(WareHouseType_Inventory, true);
    if (!inventory) {
        // Log.Add(LOG_ERROR, "[%s][%d]cannot find warehouse.", "CacheSocket.cpp", 0x14ef)
        // L2_LOG_ERROR(Log, "[%s][%d]cannot find warehouse.", "CacheSocket.cpp", 0x14ef)
        L2_LOG_ERROR(Log, "[items] Enchantment request failed for character %d: inventory unavailable. Sending failure reply.", characterId);
        sendUnavailableReply(true);
        return false;
    }

    {
        L2_WRITE_GUARD(*inventory.operator->());
        inventory->EnchantItem(socket, enchantPacket);
    }
    return false;
}

bool RequestEnchantItemFail(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004691e8.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    const std::uint8_t* const enchantPacket = ::Disassemble(packet, "d", &characterId);

    auto sendUnavailableReply = [&]() {
        int scrollItemDatabaseId = 0;
        int scrollItemServerId = 0;
        int scrollConsumeType = 0;
        int supportItemDatabaseId = 0;
        int supportItemServerId = 0;
        int supportConsumeType = 0;
        int targetItemDatabaseId = 0;
        int targetItemServerId = 0;
        int targetConsumeType = 0;
        int userServerId = 0;
        int targetItemType = 0;
        int successProbability = 0;
        ::Disassemble(
            enchantPacket, "dddddddddddd", &scrollItemDatabaseId, &scrollItemServerId, &scrollConsumeType,
            &supportItemDatabaseId, &supportItemServerId, &supportConsumeType, &targetItemDatabaseId, &targetItemServerId,
            &targetConsumeType, &userServerId, &targetItemType, &successProbability
        );

        // Both current owner-missing calls put the support database ID in
        // the reply's support-runtime-ID slot. The decoder also stops after
        // the probability field and never reads the crystal pair.
        socket->Send(
            "cdQdQdddQ", 0x1c, scrollItemServerId, static_cast<long long>(-1), supportItemDatabaseId,
            static_cast<long long>(-1), targetItemServerId, userServerId, -1, static_cast<long long>(-1)
        );
    };

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1536, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1536, characterId)
        L2_LOG_ERROR(Log, "[items] Cannot process failed enchantment: character %d unavailable. Sending failure reply.", characterId);
        sendUnavailableReply();
        return false;
    }

    CWareHouseSP inventory = user->GetWareHouse(WareHouseType_Inventory, true);
    if (!inventory) {
        // Diagnostic addition: failure to process the outcome is distinct from the enchantment failure itself.
        L2_LOG_ERROR(Log, "[items] Cannot process failed enchantment for character %d: inventory unavailable. Sending failure reply.", characterId);
        sendUnavailableReply();
        return false;
    }

    {
        L2_WRITE_GUARD(*inventory.operator->());
        inventory->EnchantItemFail(socket, enchantPacket);
    }
    return false;
}

bool RequestSetItemCountLimit(CSocket* socket [[maybe_unused]], const std::uint8_t* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0045f688 is a distinct guard-only handler.
    L2_GUARD_FUNCTION();
    return false;
}

bool RequestLoadSkill(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00469618.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int userSocketId = 0;
    ::Disassemble(packet, "dd", &characterId, &userSocketId);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x15b1, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x15b1, characterId)
        L2_LOG_ERROR(Log, "[skills] Cannot load skills for character %d: character unavailable. Sending empty skill reply to socket %d.", characterId, userSocketId);
        socket->Send("cddddd", 0x18, userSocketId, characterId, 0, 0, 0);
        return false;
    }

    user->SendBlockList(socket, static_cast<unsigned int>(userSocketId));
    user->SendAquireSkill(socket, static_cast<unsigned int>(userSocketId));
    user->SendAcquiredPotentialSkill(
        socket, static_cast<unsigned int>(characterId), static_cast<unsigned int>(userSocketId)
    );
    if (user->GetSubjobId() != 0) {
        user->SendMainClassSkill(socket, static_cast<unsigned int>(userSocketId));
    }
    return false;
}

bool RequestLoadFriendList(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004698ac.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int userSocketId = 0;
    ::Disassemble(packet, "dd", &characterId, &userSocketId);

    CPacketHelperEx<20480> friendPacket;
    g_friendsDb.MakeFriendListPacket(static_cast<unsigned int>(characterId), friendPacket);
    socket->Send(
        "cddb", 0x1e, userSocketId, friendPacket.GetAssembledCount(), friendPacket.GetLength(), friendPacket.GetPacket()
    );
    return false;
}

bool RequestEstablishFriendship(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0045f708.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int userSocketId = 0;
    int friendCharacterId = 0;
    int friendUserSocketId = 0;
    ::Disassemble(packet, "dddd", &characterId, &userSocketId, &friendCharacterId, &friendUserSocketId);

    const bool established = g_friendsDb.EstablishFriendship(
        static_cast<unsigned int>(characterId), static_cast<unsigned int>(friendCharacterId)
    );
    socket->Send("cddd", 0x1f, static_cast<int>(established), userSocketId, friendUserSocketId);
    return false;
}

bool RequestBreakFriendship(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0045f81c.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int userSocketId = 0;
    int friendCharacterId = 0;
    ::Disassemble(packet, "ddd", &characterId, &userSocketId, &friendCharacterId);

    const bool broken =
        g_friendsDb.BreakFriendship(static_cast<unsigned int>(characterId), static_cast<unsigned int>(friendCharacterId));
    socket->Send("cddd", 0x20, static_cast<int>(broken), userSocketId, friendCharacterId);
    return false;
}

bool RequestChangeItemData(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0046a2d0.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    const std::uint8_t* const itemPacket = ::Disassemble(packet, "d", &characterId);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x16a5, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x16a5, characterId)
        L2_LOG_ERROR(Log, "[items] Cannot change item data for character %d: character unavailable. No reply is sent.", characterId);
        return false;
    }

    CWareHouseSP inventory = user->GetWareHouse(WareHouseType_Inventory, true);
    if (!inventory)
        return false;

    {
        L2_WRITE_GUARD(*inventory.operator->());
        inventory->ChangeItemData(socket, itemPacket);
    }
    return false;
}

bool SetPunishPacket(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x0048f420.
    L2_GUARD_FUNCTION();

    unsigned int characterId = 0;
    int punishId = 0;
    int punishOn = 0;
    int remain = 0;
    ::Disassemble(packet, "dddd", &characterId, &punishId, &punishOn, &remain);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetPunish (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&punishId);
    connection.BindParameter(&punishOn);
    connection.BindParameter(&remain);
    connection.ExecutePrepared(statement);
    return false;
}

bool GetPunishPacket(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0048f580.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x16d7, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x16d7, characterId)
        L2_LOG_ERROR(Log, "[characters] Cannot load punishments for character %d: character unavailable. No reply is sent.", characterId);
        return false;
    }

    int punishId = 0;
    int punishOn = 0;
    int remainGame = 0;
    int remainReal = 0;
    std::int64_t nullableIndicator = -1;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&punishId);
    connection.BindNullable(&punishOn, &nullableIndicator);
    connection.BindNullable(&remainGame, &nullableIndicator);
    connection.BindNullable(&remainReal, &nullableIndicator);

    constexpr const wchar_t* statement = L"{Call lin_GetPunish (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(statement)) {
        socket->Send("cdd", 0x2b, characterId, 0);
        return false;
    }

    std::array<char, 0x5000> response{};
    int responseSize = 0;
    int punishCount = 0;
    while (connection.Fetch()) {
        const int remaining = punishOn == 0 ? remainReal : remainGame;
        const int assembledSize = ::Assemble(
            response.data() + responseSize, static_cast<int>(response.size()) - responseSize, "ddd", punishId, punishOn,
            remaining
        );
        if (assembledSize > 0) {
            responseSize += assembledSize;
        }

        punishId = 0;
        punishOn = 0;
        remainGame = 0;
        remainReal = 0;
        ++punishCount;
    }

    socket->Send("cddb", 0x2b, characterId, punishCount, responseSize, response.data());
    return false;
}

bool ChangePosPacket(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x0046a54c.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 50> characterName{};
    std::array<char16_t, 50> lowerCharacterName{};
    int x = 0;
    int y = 0;
    int z = 0;
    ::Disassemble(packet, "Sddd", 0x64, characterName.data(), &x, &y, &z);
    Utils::CopyStrInLowerCase(characterName.data(), lowerCharacterName.data());

    std::string databaseName = Utf16ToUtf8(FixedUtf16View(lowerCharacterName));
    CUserSP user = g_userDb.GetUser(std::u16string(FixedUtf16View(lowerCharacterName)).c_str(), true);
    if (user && !user->ChangeLocationExtern(lowerCharacterName.data(), x, y, z, false)) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot change location user, char name[%ls]", "CacheSocket.cpp", 0x172e,
        //     databaseName.c_str()
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot change location user, char name[%ls]", "CacheSocket.cpp", 0x172e,
        //     databaseName.c_str()
        // )
        L2_LOG_ERROR(Log, "[characters] Location change failed for '%s'; requested position (%d, %d, %d).",
            databaseName.c_str(), x, y, z);
    }
    return false;
}

bool GetLoginAnnouncePacket(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0046a720.
    L2_GUARD_FUNCTION();

    unsigned int characterId = 0;
    ::Disassemble(packet, "d", &characterId);

    std::array<char, 0x5000> response{};
    int announceCount = 0;
    int responseSize = 0;
    {
        L2_GUARD(g_announce);
        g_announce.ReadAnnounce(false);
        announceCount = g_announce.GetLoginAnnoncesNumber();
        responseSize = g_announce.GetPacketSize();
        std::memcpy(response.data(), g_announce.GetLoginAnnoncesPacket(), static_cast<std::size_t>(responseSize));
    }

    socket->Send("cddb", 0x2c, characterId, announceCount, responseSize, response.data());

    CUserSP user = g_userDb.GetUser(static_cast<int>(characterId), true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        user->ShowPetiMsg(socket);
    }
    return false;
}

bool SetBuilderPacket(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x0046a910.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 50> characterName{};
    std::array<char16_t, 50> lowerCharacterName{};
    int builderLevel = 0;
    ::Disassemble(packet, "Sd", 0x64, characterName.data(), &builderLevel);
    Utils::CopyStrInLowerCase(characterName.data(), lowerCharacterName.data());

    const std::string lowerDatabaseName = Utf16ToUtf8(FixedUtf16View(lowerCharacterName));
    CUserSP user = g_userDb.GetUser(std::u16string(FixedUtf16View(lowerCharacterName)).c_str(), false);
    if (!user) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find user, char name[%ls]", "CacheSocket.cpp", 0x17c8,
        //     lowerDatabaseName.c_str()
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, char name[%ls]", "CacheSocket.cpp", 0x17c8,
        //     lowerDatabaseName.c_str()
        // )
        L2_LOG_ERROR(Log, "[characters] Cannot set builder level %d for '%s': character unavailable. Update skipped.",
            builderLevel, lowerDatabaseName.c_str());
        return false;
    }

    user->SetBuilderLevExtern(characterName.data(), builderLevel);
    return false;
}

bool LoadSelectedCharacter(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0047c3c8.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    unsigned int userSocketId = 0;
    std::uint16_t locationTypeValue = 0;
    int raidReload = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "ddhd", &characterId, &userSocketId, &locationTypeValue, &raidReload);
    if (locationTypeValue != 0) {
        ::Disassemble(cursor, "ddd", &x, &y, &z);
    }

    CUserSP user = raidReload == 0 ? g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId())
                                   : g_userDb.GetUserForRaid(characterId, false);
    if (user && user->GetReloadFlag()) {
        user = g_userDb.GetUserForRaid(characterId, false);
        if (user)
            user->SetReloadFlag(false);
    }

    if (user) {
        if (!user->ItemCheckBetweenMemoryAndDB()) {
            user->OnItemCheckFailed(2);
        }
        user->ReplyLoadSelectedChar(socket, userSocketId, static_cast<AddtionalLocationType>(locationTypeValue), x, y, z);
    }
    return false;
}

bool RequestDeleteSkill(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x0046aad8.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int skillId = 0;
    ::Disassemble(packet, "dd", &characterId, &skillId);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, char id[%d]", "CacheSocket.cpp", 0x184b, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, char id[%d]", "CacheSocket.cpp", 0x184b, characterId)
        L2_LOG_ERROR(Log, "[skills] Cannot delete skill %d from character %d: character unavailable. Deletion skipped.", skillId, characterId);
        return false;
    }

    const int previousSkillLevelSet = user->GetAcquireSkillLevelSet(skillId);
    user->DeleteAquireSkill(skillId);
    WriteDeleteSkillAudit(*user.get(), skillId, previousSkillLevelSet);
    return false;
}

bool RequestRestoreCharacter(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0048fc0c.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    const std::uint8_t* const loadCharacterPacket = ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, char id[%d]", "CacheSocket.cpp", 0x181d, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, char id[%d]", "CacheSocket.cpp", 0x181d, characterId)
        L2_LOG_ERROR(Log, "[characters] Cannot restore character %d: character unavailable. No load reply is sent.", characterId);
        return false;
    }

    if (user->m_deletionTime.wYear != 0) {
        user->RestoreDeleted();
    }
    LoadCharacterPacket(socket, loadCharacterPacket);
    // Current RequestRestoreCharacter 0x0048fc0c discards the delegated
    // result and executes `xor al, al` at 0x0048fd90.
    return false;
}

bool RequestSetPledgeInfo(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x004673d0.
    L2_GUARD_FUNCTION();

    std::uint32_t pledgeId = 0;
    std::uint32_t fieldSelector = 0;
    std::uint32_t newValue = 0;
    ::Disassemble(packet, "ddd", &pledgeId, &fieldSelector, &newValue);

    if (!g_pledgeDb.SetPledgeInfo(pledgeId, fieldSelector, runtime::Int32FromBits(newValue))) {
        return false;
    }

    CPledgeSP pledge = g_pledgeDb.GetPledge(runtime::Int32FromBits(pledgeId), false, runtime::process::CurrentDatabaseId());
    if (!pledge)
        return false;

    // The target clears a 24-unit UTF-16 buffer, then performs wcscpy_s
    // before loading the ruler.
    std::array<char16_t, 24> pledgeName{};
    const char16_t* const sourceName = pledge->GetName();
    const char16_t* const sourceEnd = std::find(sourceName, sourceName + pledgeName.size(), u'\0');
    if (sourceEnd != sourceName + pledgeName.size()) {
        std::copy(sourceName, sourceEnd, pledgeName.begin());
    }
    CUserSP ruler = g_userDb.GetUser(static_cast<int>(pledge->rullerId()), true, false, runtime::process::CurrentDatabaseId());
    if (!ruler)
        return false;

    WriteSetPledgeInfoAudit(
        *ruler.get(), pledgeName.data(), pledgeId, fieldSelector, newValue, static_cast<std::uint32_t>(pledge->skillLevel()),
        pledge->GetMemberCount()
    );
    return false;
}

bool RequestGetPledgeCrest(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00467ec0. The third request field was added after
    // C1 and is the pledge's server/database identifier: current L2Server
    // User::OnPledgeCrest passes the pledge +0x228 value and keys its crest
    // cache by the same (serverId, crestId) pair.
    L2_GUARD_FUNCTION();

    std::uint32_t userSocketId = 0;
    std::uint32_t crestId = 0;
    std::uint32_t serverId = 0;
    ::Disassemble(packet, "ddd", &userSocketId, &crestId, &serverId);

    CPledgeCrestSP pledgeCrest = g_pledgeCrestDb.GetPledgeCrest(crestId);
    if (!pledgeCrest) {
        socket->Send("cdddd", 0x36, userSocketId, crestId, serverId, 0);
        return false;
    }

    const int bitmapSize = pledgeCrest->bitmapSize();
    socket->Send("cddddb", 0x36, userSocketId, crestId, serverId, bitmapSize, bitmapSize, pledgeCrest->bitmap());
    return false;
}

bool RequestCreatePledgeByNpc(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00462df4.
    L2_GUARD_FUNCTION();

    std::uint32_t userSocketId = 0;
    std::uint32_t npcObjectId = 0;
    std::array<char16_t, 50> pledgeName{};
    int characterId = 0;
    ::Disassemble(packet, "ddSd", &userSocketId, &npcObjectId, 0x64, pledgeName.data(), &characterId);

    const int worldId = runtime::process::CurrentDatabaseId();
    int pledgeId = -1;
    CUserSP ruler = g_userDb.GetUser(characterId, true, false, worldId);
    if (ruler && ruler->GetPledgeID() == 0U && ruler->GetWorld() == worldId) {
        std::wstring databasePledgeName = FixedUtf16ToWide(pledgeName);
        pledgeId = g_pledgeDb.CreatePledge(databasePledgeName.data(), characterId);
        if (pledgeId > 0) {
            ruler->SetPledge(pledgeId, MASTER_PLEDGE);
            PledgeContributionInCache::AddInfo(pledgeId, characterId);
            WriteCreatePledgeAudit(*ruler.get(), pledgeName.data(), pledgeId);
        }
    }

    socket->Send("cdddSd", 0x39, userSocketId, npcObjectId, pledgeId, pledgeName.data(), worldId);
    return false;
}

bool RequestDeletePledgeByTimer(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0049d7e8.
    L2_GUARD_FUNCTION();

    int pledgeServerId = 0;
    int pledgeId = 0;
    ::Disassemble(packet, "dd", &pledgeServerId, &pledgeId);

    const int worldId = runtime::process::CurrentDatabaseId();
    std::uint32_t rulerId = 0;
    std::array<char16_t, 24> pledgeName{};
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, worldId);
    if (pledge) {
        L2_READ_GUARD(*pledge.operator->());
        rulerId = pledge->rullerId();
        std::copy_n(pledge->GetName(), pledgeName.size(), pledgeName.begin());
    }

    if (!g_pledgeDb.DeletePledge(static_cast<std::uint32_t>(pledgeId))) {
        socket->Send("cddd", 0x3a, pledgeServerId, pledgeId, 0);
        return false;
    }

    CUserSP ruler = g_userDb.GetUser(static_cast<int>(rulerId), true, false, worldId);
    if (ruler) {
        WriteDeletePledgeByTimerAudit(*ruler.get(), pledgeName.data(), pledgeId);
    }

    GIP._Add(
        L"CMD_CloseClan(%d, %d, %d", GetCacheDConfiguration().worldId, pledgeId,
        static_cast<std::int64_t>(std::time(nullptr))
    );
    CPledgeJoinDB::Instance().RequestPledgeDelete(pledgeId);
    if (!PledgeContributionInCache::DeleteAllInfo(pledgeId)) {
        // Log.Add(
        //     LOG_ERROR, "PLEDGE[%s][%d]Failed delete contribution info all in : %d", "CacheSocket.cpp", 0xa9b,
        //     pledgeId
        // )
        // L2_LOG_ERROR(Log, "PLEDGE[%s][%d]Failed delete contribution info all in : %d", "CacheSocket.cpp", 0xa9b,
        //     pledgeId
        // )
        L2_LOG_ERROR(Log, "[clans] Contribution cleanup failed after timer deletion of clan %d. Clan-deletion success reply will still be sent.", pledgeId);
    }

    socket->Send("cddd", 0x3a, pledgeServerId, pledgeId, 1);
    return false;
}

bool RequestLoadDismissReservedPledge(CSocket* socket, const std::uint8_t* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0048b63c. The request has no payload. Each reply
    // carries at most 1000 (pledge_id, dismiss_reserved_time) pairs. The
    // target's do/while sends an additional empty terminating reply whenever
    // the row count is an exact multiple of 1000, including zero rows.
    L2_GUARD_FUNCTION();

    unsigned int pledgeId = 0;
    unsigned int dismissReservedTime = 0;
    constexpr int status = 3;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&pledgeId);
    connection.Bind(&dismissReservedTime);

    constexpr const wchar_t* statement = L"{Call lin_LoadDismissReservedPledge (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&status);
    if (!connection.ExecutePrepared(statement)) {
        return false;
    }

    bool fullBatch = false;
    do {
        std::array<char, 0x5000> rows{};
        int rowCount = 0;
        int rowBytes = 0;
        while (rowCount != 1000) {
            if (!connection.Fetch())
                break;
            const int written = ::Assemble(
                rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dd", pledgeId, dismissReservedTime
            );
            rowBytes += written;
            ++rowCount;
        }
        fullBatch = rowCount == 1000;
        socket->Send("cdb", 0x3c, rowCount, rowBytes, rows.data());
    } while (fullBatch);

    return false;
}

bool RequestPetiGetCharList(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0048224c. Unlike the C1 donor, the current request
    // and reply identify the account by numeric ID. Each row is "Shd":
    // character name, builder level and character ID.
    L2_GUARD_FUNCTION();

    int accountId = 0;
    ::Disassemble(packet, "d", &accountId);

    CAccountSP account;
    if (accountId > 0) {
        account = CAccount::Load(static_cast<unsigned int>(accountId));
    }
    if (!account) {
        socket->Send("cdc", 0x3e, accountId, 0);
        return false;
    }

    std::optional<PetiCharacterListAssembler> charactersStorage;
    {
        L2_READ_GUARD(*account.operator->());
        auto& characters = charactersStorage.emplace();
        account->GetChars().FOR_ALL(&characters, L"CacheSocket.cpp", 0x188c);
        socket->Send("cdcb", 0x3e, accountId, characters.rowCount, characters.rowBytes, characters.rows.data());
    }
    return false;
}

bool RequestPetiSaveMsg(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x0048fda8. The socket is unused and
    // the ExecutePrepared result is ignored. This request is one-way.
    L2_GUARD_FUNCTION();

    unsigned int characterId = 0;
    std::array<char16_t, 1001> petitionMessage{};
    ::Disassemble(packet, "dS", &characterId, 0x7d2, petitionMessage.data());
    const std::u16string databaseMessage(FixedUtf16View(petitionMessage));

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetPetitionMsg (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(databaseMessage.c_str());
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestSetUserPledgeInfo(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x00467968. Persistence and in-memory mutation are
    // owned by CPledgeDb::SetUserPledgeInfo. This handler adds the current
    // loaded-user/pledge audit only after that operation succeeds.
    L2_GUARD_FUNCTION();

    std::uint32_t characterId = 0;
    std::uint32_t fieldSelector = 0;
    std::uint32_t newValue = 0;
    ::Disassemble(packet, "ddd", &characterId, &fieldSelector, &newValue);
    if (!g_pledgeDb.SetUserPledgeInfo(characterId, fieldSelector, newValue)) {
        return false;
    }

    const int worldId = runtime::process::CurrentDatabaseId();
    CUserSP user = g_userDb.GetUser(static_cast<int>(characterId), true, false, worldId);
    if (!user)
        return false;

    CPledgeSP pledge = g_pledgeDb.GetPledge(static_cast<int>(user->GetPledgeID()), false, worldId);
    if (!pledge)
        return false;

    std::array<char16_t, 24> pledgeName{};
    const char16_t* const sourceName = pledge->GetName();
    const char16_t* const sourceEnd = std::find(sourceName, sourceName + pledgeName.size(), u'\0');
    if (sourceEnd != sourceName + pledgeName.size()) {
        std::copy(sourceName, sourceEnd, pledgeName.begin());
    }
    WriteSetUserPledgeInfoAudit(*user.get(), pledgeName.data(), pledge->GetId(), fieldSelector, newValue);
    return false;
}

bool RequestLogCharInfo(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x0046b060. This one-way request writes the complete
    // 26-field SaveCharInfo record through CLogFormatter::WriteLog.
    L2_GUARD_FUNCTION();

    std::uint32_t characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    CUserSP user = g_userDb.GetUser(static_cast<int>(characterId), true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    // Preserve the target evaluation order: both calls can load database
    // state before the formatter snapshots the remaining in-memory fields.
    const std::u16string vitality = runtime::DecimalUtf16(user->GetVitality());
    const std::u16string userPoint = runtime::DecimalUtf16(user->LoadUserPoint(5));

    // Target CUser+0x628 is initialized to zero at 0x005a0efd and has no
    // writer anywhere in the current EXE. Its original member name is not
    // recoverable from the surviving PDB records. Retain the observable zero
    // without inventing a public field or semantic name.
    const std::u16string reservedUserState = runtime::DecimalUtf16(0);
    const std::u16string characterName = std::u16string(user->GetCharName());
    const std::u16string accountName = std::u16string(user->GetAccountName());

    CLogFormatter formatter;
    formatter.WriteLog(
        0x32c, user->GetId(), user->GetAccountID(), 0, 0, user->GetX(), user->GetY(), user->GetZ(),
        reservedUserState.c_str(), userPoint.c_str(), vitality.c_str(), runtime::Win64CvttDoubleToInt32(user->GetHP()),
        runtime::Win64CvttDoubleToInt32(user->GetMP()), user->GetSP(), user->GetExp(), user->GetLevel(), user->GetAlign(),
        user->GetPK(), user->GetDuel(), user->GetDualClassId(), user->GetClass(), characterName.c_str(), accountName.c_str(),
        nullptr, nullptr, user->GetSubjobId()
    );
    return false;
}

bool RequestResetUserNickname(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00466b04. The target passes an addressable empty
    // string to CUserDb::SetUserNickname, not a null pointer.
    L2_GUARD_FUNCTION();

    std::uint32_t characterId = 0;
    ::Disassemble(packet, "d", &characterId);
    std::array<wchar_t, 1> emptyNickname{};
    if (!g_userDb.SetUserNickname(characterId, emptyNickname.data())) {
        return false;
    }

    // The current reply precedes every cache lookup and audit operation.
    socket->Send("cd", 0x3b, characterId);

    const int worldId = runtime::process::CurrentDatabaseId();
    CUserSP user = g_userDb.GetUser(static_cast<int>(characterId), true, false, worldId);
    if (!user)
        return false;

    std::array<char16_t, 24> pledgeName{};
    int pledgeId = 0;
    CPledgeSP pledge = g_pledgeDb.GetPledge(static_cast<int>(user->GetPledgeID()), false, worldId);
    if (pledge) {
        const char16_t* const sourceName = pledge->GetName();
        const char16_t* const sourceEnd = std::find(sourceName, sourceName + pledgeName.size(), u'\0');
        if (sourceEnd != sourceName + pledgeName.size()) {
            std::copy(sourceName, sourceEnd, pledgeName.begin());
        }
        pledgeId = static_cast<int>(pledge->GetId());
    }

    WriteResetUserNicknameAudit(*user.get(), pledgeName.data(), pledgeId);
    return false;
}

bool RequestManageBattleCamp(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004833e0. The three operation layouts and all four
    // reply formats are confirmed by the paired L2Server image.
    L2_GUARD_FUNCTION();

    int operation = 0;
    const std::uint8_t* const payload = ::Disassemble(packet, "d", &operation);

    if (operation == 0) {
        std::uint32_t pledgeId = 0;
        std::uint32_t pledgeWorldId = 0;
        std::uint32_t castleId = 0;
        int maxHp = 0;
        int x = 0;
        int y = 0;
        int z = 0;
        int baseType = 0;
        ::Disassemble(payload, "dddddddd", &pledgeId, &pledgeWorldId, &castleId, &maxHp, &x, &y, &z, &baseType);

        const std::uint32_t battleCampId =
            g_battleCampDb.InstallBattleCamp(castleId, pledgeId, pledgeWorldId, maxHp, x, y, z, baseType);
        if (battleCampId == 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[CacheSocket.cpp][%d] MBC_INSTALL_BATTLE_CAMP[1] "
            //     "Invalid BattleCampDbId[%u] PledgeDBID[%u] "
            //     "World[%u] CastleID[%u]",
            //     0x1134, battleCampId, pledgeId, pledgeWorldId, castleId
            // )
            // L2_LOG_ERROR(Log,
            //     "[CacheSocket.cpp][%d] MBC_INSTALL_BATTLE_CAMP[1] "
            //     "Invalid BattleCampDbId[%u] PledgeDBID[%u] "
            //     "World[%u] CastleID[%u]",
            //     0x1134, battleCampId, pledgeId, pledgeWorldId, castleId
            // )
            L2_LOG_ERROR(Log, "[siege] Battle camp installation returned ID 0 for clan %u, world %u, castle %u (base type %d). Reply will carry camp ID 0.",
                static_cast<unsigned int>(pledgeId), static_cast<unsigned int>(pledgeWorldId), static_cast<unsigned int>(castleId), baseType);
        } else {
            CPledgeSP pledge = g_pledgeDb.GetPledge(static_cast<int>(pledgeId), false, static_cast<int>(pledgeWorldId));
            if (!pledge) {
                // Log.Add(
                //     LOG_ERROR,
                //     "[CacheSocket.cpp][%d] MBC_INSTALL_BATTLE_CAMP[2] "
                //     "Invalid BattleCampDbId[%u] PledgeDBID[%u] "
                //     "World[%u] CastleID[%u]",
                //     0x112e, battleCampId, pledgeId, pledgeWorldId, castleId
                // )
                // L2_LOG_ERROR(Log,
                //     "[CacheSocket.cpp][%d] MBC_INSTALL_BATTLE_CAMP[2] "
                //     "Invalid BattleCampDbId[%u] PledgeDBID[%u] "
                //     "World[%u] CastleID[%u]",
                //     0x112e, battleCampId, pledgeId, pledgeWorldId, castleId
                // )
                L2_LOG_ERROR(Log, "[siege] Cannot write installation audit for battle camp %u: clan %u unavailable in world %u (castle %u). Reply will still carry the returned camp ID.",
                    static_cast<unsigned int>(battleCampId), static_cast<unsigned int>(pledgeId), static_cast<unsigned int>(pledgeWorldId), static_cast<unsigned int>(castleId));
            } else {
                std::array<char16_t, 24> pledgeName{};
                const char16_t* const sourceName = pledge->GetName();
                const char16_t* const sourceEnd = std::find(sourceName, sourceName + pledgeName.size(), u'\0');
                if (sourceEnd != sourceName + pledgeName.size()) {
                    std::copy(sourceName, sourceEnd, pledgeName.begin());
                }
                const std::uint32_t loadedPledgeId = pledge->GetId();
                CUserSP ruler =
                    g_userDb.GetUser(static_cast<int>(pledge->rullerId()), true, false, static_cast<int>(pledgeWorldId));
                if (ruler) {
                    WriteInstallBattleCampAudit(
                        *ruler.get(), pledgeName.data(), static_cast<int>(loadedPledgeId), battleCampId, maxHp, x, y, z
                    );
                }
            }
        }

        socket->Send(
            "cdddddddddd", 0x40, operation, battleCampId, pledgeId, pledgeWorldId, castleId, maxHp, x, y, z, baseType
        );
        return false;
    }

    if (operation == 1) {
        std::uint32_t pledgeId = 0;
        std::uint32_t pledgeWorldId = 0;
        int baseType = 0;
        ::Disassemble(payload, "ddd", &pledgeId, &pledgeWorldId, &baseType);

        const std::uint32_t battleCampId = g_battleCampDb.UninstallBattleCamp(pledgeId, pledgeWorldId, baseType);
        CPledgeSP pledge = g_pledgeDb.GetPledge(static_cast<int>(pledgeId), false, static_cast<int>(pledgeWorldId));
        if (pledge) {
            std::array<char16_t, 24> pledgeName{};
            const char16_t* const sourceName = pledge->GetName();
            const char16_t* const sourceEnd = std::find(sourceName, sourceName + pledgeName.size(), u'\0');
            if (sourceEnd != sourceName + pledgeName.size()) {
                std::copy(sourceName, sourceEnd, pledgeName.begin());
            }
            CUserSP ruler =
                g_userDb.GetUser(static_cast<int>(pledge->rullerId()), true, false, static_cast<int>(pledgeWorldId));
            if (ruler) {
                WriteUninstallBattleCampAudit(*ruler.get(), pledgeName.data(), pledgeId);
            }
        }

        socket->Send("cddddd", 0x40, operation, pledgeId, pledgeWorldId, baseType, battleCampId);
        return false;
    }

    if (operation == 2) {
        std::uint32_t castleId = 0;
        ::Disassemble(payload, "d", &castleId);

        std::vector<std::uint32_t> battleCamps;
        g_battleCampDb.UninstallAllBattleCamp(castleId, &battleCamps);
        if (battleCamps.empty()) {
            socket->Send("cddd", 0x40, operation, castleId, 0);
            return false;
        }

        std::array<char, 0x5000> response{};
        int responseSize = 0;
        int battleCampCount = 0;
        for (const std::uint32_t battleCampId : battleCamps) {
            responseSize += ::Assemble(
                response.data() + responseSize, static_cast<int>(response.size()) - responseSize, "d", battleCampId
            );
            ++battleCampCount;
            WriteUninstallAllBattleCampAudit(castleId, battleCampId);
        }

        socket->Send("cdddb", 0x40, operation, castleId, battleCampCount, responseSize, response.data());
    }
    return false;
}

bool RequestLoadBattleCamp(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0048db7c. Unlike C1, the current request includes
    // the object type and each returned row carries that type as field nine.
    L2_GUARD_FUNCTION();

    std::uint32_t residenceId = 0;
    int type = 0;
    ::Disassemble(packet, "dd", &residenceId, &type);

    std::uint32_t battleCampId = 0;
    std::uint32_t ownerId = 0;
    std::uint32_t loadedResidenceId = 0;
    int maxHp = 0;
    int hp = 0;
    int x = 0;
    int y = 0;
    int z = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&battleCampId);
    connection.Bind(&ownerId);
    connection.Bind(&loadedResidenceId);
    connection.Bind(&maxHp);
    connection.Bind(&hp);
    connection.Bind(&x);
    connection.Bind(&y);
    connection.Bind(&z);

    const wchar_t* statement = nullptr;
    if (residenceId == 0) {
        statement = L"{Call lin_GetResidenceByType (?)}";
        connection.Prepare(statement);
        connection.BindParameter(&type);
    } else {
        statement = L"{Call lin_GetResidenceByIdType (?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&residenceId);
        connection.BindParameter(&type);
    }
    if (!connection.ExecutePrepared(statement))
        return false;

    std::array<char, 0x5000> response{};
    bool fullBatch = false;
    do {
        fullBatch = false;
        int battleCampCount = 0;
        int responseSize = 0;
        do {
            if (!connection.Fetch())
                break;
            responseSize += ::Assemble(
                response.data() + responseSize, static_cast<int>(response.size()) - responseSize, "ddddddddd", battleCampId,
                ownerId, loadedResidenceId, maxHp, hp, x, y, z, type
            );
            ++battleCampCount;
        } while (battleCampCount != 250);

        // This sends a final empty packet when the row count is
        // an exact multiple of 250, matching the target's outer do/while.
        fullBatch = battleCampCount == 250;
        socket->Send("cdb", 0x41, battleCampCount, responseSize, response.data());
    } while (fullBatch);
    return false;
}

bool RequestPrepareCastleWar(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0048de6c. The current wire reply omits the
    // request's world ID and uses opcode 0x42, unlike the C1 donor reply.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    std::uint32_t worldId = 0;
    int cacheContextId = 0;
    int operation = 0;
    std::uint32_t castleId = 0;
    std::uint32_t pledgeId = 0;
    std::uint32_t proposeTime = 0;
    ::Disassemble(
        packet, "ddddddd", &userServerId, &worldId, &cacheContextId, &operation, &castleId, &pledgeId, &proposeTime
    );

    bool success = false;
    DBConn connection(runtime::process::CurrentDatabaseId());
    if (operation == 1 || operation == 2) {
        const int registrationType = operation == 1 ? 1 : 3;
        success = ExecuteSetCastleWar(connection, worldId, pledgeId, castleId, registrationType, proposeTime);
    } else if (operation == 3 || operation == 4) {
        constexpr auto statement = L"{Call lin_DeleteCastleWar (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&worldId);
        connection.BindParameter(&pledgeId);
        connection.BindParameter(&castleId);
        success = connection.ExecutePrepared(statement);
    }

    if (success) {
        // This is castleId, not worldId. Current instruction
        // 0x0048e03a places packet field five in GetPledge's world argument.
        // the paired current sender proves that field is the castle ID.
        CPledgeSP pledge = g_pledgeDb.GetPledge(static_cast<int>(pledgeId), false, static_cast<int>(castleId));
        if (pledge) {
            std::array<char16_t, 24> pledgeName{};
            const char16_t* const sourceName = pledge->GetName();
            const char16_t* const sourceEnd = std::find(sourceName, sourceName + pledgeName.size(), u'\0');
            if (sourceEnd != sourceName + pledgeName.size()) {
                std::copy(sourceName, sourceEnd, pledgeName.begin());
            }

            CUserSP ruler = g_userDb.GetUser(static_cast<int>(pledge->rullerId()), true, false, pledge->GetWorldId());
            if (ruler) {
                int recordId = 0xe1;
                if (operation == 1) {
                    recordId = 0xdf;
                } else if (operation == 2) {
                    recordId = 0xe0;
                }
                WritePrepareCastleWarAudit(recordId, *ruler.get(), pledgeName.data(), pledge->GetId(), castleId);
            }
        }
    }

    socket->Send("cddddd", 0x42, userServerId, cacheContextId, operation, proposeTime, success ? 1 : 0);
    return false;
}

bool RequestViewSiegeList(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0045f420.
    L2_GUARD_FUNCTION();

    std::uint32_t userServerId = 0;
    std::uint32_t castleId = 0;
    std::array<char16_t, 256> targetReturnHtml{};
    ::Disassemble(packet, "ddS", &userServerId, &castleId, 0x200, targetReturnHtml.data());

    const std::wstring returnHtml = FixedUtf16ToWide(targetReturnHtml);
    g_castleDb.SendReplyToViewSiegeList(socket, userServerId, castleId, returnHtml.c_str());
    return false;
}

bool RequestApproveBattle(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0045f554.
    L2_GUARD_FUNCTION();

    std::uint32_t userServerId = 0;
    std::uint32_t castleId = 0;
    std::array<char16_t, 256> targetReturnHtml{};
    ::Disassemble(packet, "ddS", &userServerId, &castleId, 0x200, targetReturnHtml.data());

    const std::wstring returnHtml = FixedUtf16ToWide(targetReturnHtml);
    g_castleDb.SendReplyToApproveBattle(socket, userServerId, castleId, returnHtml.c_str());
    return false;
}

bool RequestConfirmCastleDefence(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0048e464. Compared with C1, the current request
    // keeps castleId in lane 3 and adds worldId as lane 6. The reply returns
    // the affected-row result immediately before that worldId.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    int cacheContextId = 0;
    std::uint32_t castleId = 0;
    std::uint32_t pledgeId = 0;
    int operation = 0;
    std::uint32_t worldId = 0;
    ::Disassemble(packet, "dddddd", &userServerId, &cacheContextId, &castleId, &pledgeId, &operation, &worldId);

    int affectedRows = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&affectedRows);
    bool executed = false;
    if (operation == 0) {
        constexpr auto statement = L"{Call lin_DeleteCastleWar (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&worldId);
        connection.BindParameter(&pledgeId);
        connection.BindParameter(&castleId);
        executed = connection.ExecutePrepared(statement);
    } else {
        constexpr int registrationType = 2;
        executed = ExecuteUpdateCastleWar(connection, castleId, pledgeId, registrationType, worldId);
    }
    if (executed) {
        connection.Fetch();
    }

    socket->Send("cdddddd", 0x45, userServerId, cacheContextId, pledgeId, operation, affectedRows, worldId);

    CPledgeSP pledge = g_pledgeDb.GetPledge(static_cast<int>(pledgeId), false, static_cast<int>(worldId));
    if (pledge) {
        std::array<char16_t, 24> pledgeName{};
        const char16_t* const sourceName = pledge->GetName();
        const char16_t* const sourceEnd = std::find(sourceName, sourceName + pledgeName.size(), u'\0');
        if (sourceEnd != sourceName + pledgeName.size()) {
            std::copy(sourceName, sourceEnd, pledgeName.begin());
        }

        CUserSP ruler = g_userDb.GetUser(static_cast<int>(pledge->rullerId()), true, false, pledge->GetWorldId());
        if (ruler) {
            WriteConfirmCastleDefenceAudit(*ruler.get(), pledgeName.data(), pledge->GetId(), pledgeId, operation);
        }
    }
    return false;
}

bool RequestQueryCastleSiegeForRegister(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00468034.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    int worldId = 0;
    int cacheContextId = 0;
    int castleId = 0;
    int allianceId = 0;
    std::uint32_t pledgeId = 0;
    int operation = 0;
    ::Disassemble(
        packet, "ddddddd", &userServerId, &worldId, &cacheContextId, &castleId, &allianceId, &pledgeId, &operation
    );

    CPacketHelperEx<20480> relatedSieges;
    if (g_castleDb.GetRelatedCastleSiege(relatedSieges, 0, pledgeId, worldId) < 0) {
        // Log.Add(
        //     LOG_ERROR, "[CacheSocket.cpp][%d] GetRelatedCastleSiege failed [%d]", 0x12eb,
        //     static_cast<int>(pledgeId)
        // )
        // L2_LOG_ERROR(Log, "[CacheSocket.cpp][%d] GetRelatedCastleSiege failed [%d]", 0x12eb,
        //     static_cast<int>(pledgeId)
        // )
        // L2_LOG_ERROR(Log, "[siege] Related-siege lookup failed during registration: clan %u, castle %d, world %d, operation %d, request context %d.",
        //     static_cast<unsigned int>(pledgeId), castleId, worldId, operation, cacheContextId)
        L2_LOG_ERROR(Log, "[siege] Related-siege lookup failed during registration: clan %u, castle %d, requested world %d, operation %d, request context %d.",
            static_cast<unsigned int>(pledgeId), castleId, worldId, operation, cacheContextId);
    }

    // allianceId is present in the current request but is not echoed by the
    // current 0x46 reply.
    socket->Send(
        "cddddddb", 0x46, userServerId, cacheContextId, castleId, static_cast<int>(pledgeId), operation,
        relatedSieges.GetAssembledCount(), relatedSieges.GetLength(), relatedSieges.GetPacket()
    );
    return false;
}

bool RequestQueryCastleSiegeForUnregister(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0046822c.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    int worldId = 0;
    int cacheContextId = 0;
    int castleId = 0;
    std::uint32_t pledgeId = 0;
    ::Disassemble(packet, "ddddd", &userServerId, &worldId, &cacheContextId, &castleId, &pledgeId);

    CPacketHelperEx<20480> relatedSieges;
    if (g_castleDb.GetRelatedCastleSiege(relatedSieges, 0, pledgeId, worldId) < 0) {
        // Log.Add(
        //     LOG_ERROR, "[CacheSocket.cpp][%d] GetRelatedCastleSiege failed [%d]", 0x1315,
        //     static_cast<int>(pledgeId)
        // )
        // L2_LOG_ERROR(Log, "[CacheSocket.cpp][%d] GetRelatedCastleSiege failed [%d]", 0x1315,
        //     static_cast<int>(pledgeId)
        // )
        // L2_LOG_ERROR(Log, "[siege] Related-siege lookup failed during unregistration: clan %u, castle %d, world %d, request context %d.",
        //     static_cast<unsigned int>(pledgeId), castleId, worldId, cacheContextId)
        L2_LOG_ERROR(Log, "[siege] Related-siege lookup failed during unregistration: clan %u, castle %d, requested world %d, request context %d.",
            static_cast<unsigned int>(pledgeId), castleId, worldId, cacheContextId);
    }

    socket->Send(
        "cdddddb", 0x47, userServerId, cacheContextId, castleId, static_cast<int>(pledgeId),
        relatedSieges.GetAssembledCount(), relatedSieges.GetLength(), relatedSieges.GetPacket()
    );
    return false;
}

bool RequestQueryCastleSiegeForInfo(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0046840c. The first four values are deliberately
    // kept as wire fields: neither the current CacheD owner nor the surviving
    // current PDB assigns semantic parameter names to this free handler.
    L2_GUARD_FUNCTION();

    int requestField1 = 0;
    int requestField2 = 0;
    int requestField3 = 0;
    int requestField4 = 0;
    std::uint32_t pledgeId = 0;
    ::Disassemble(packet, "ddddd", &requestField1, &requestField2, &requestField3, &requestField4, &pledgeId);

    CPacketHelperEx<20480> relatedSieges;
    if (g_castleDb.GetRelatedCastleSiege(relatedSieges, 0, pledgeId, 0) < 0) {
        // Log.Add(
        //     LOG_ERROR, "[CacheSocket.cpp][%d] GetRelatedCastleSiege failed [%d]", 0x1340,
        //     static_cast<int>(pledgeId)
        // )
        // L2_LOG_ERROR(Log, "[CacheSocket.cpp][%d] GetRelatedCastleSiege failed [%d]", 0x1340,
        //     static_cast<int>(pledgeId)
        // )
        L2_LOG_ERROR(Log, "[siege] Related-siege lookup failed for clan %u in the siege-info request.", static_cast<unsigned int>(pledgeId));
    }

    socket->Send(
        "cddddddb", 0x48, requestField1, requestField2, requestField3, requestField4, static_cast<int>(pledgeId),
        relatedSieges.GetAssembledCount(), relatedSieges.GetLength(), relatedSieges.GetPacket()
    );
    return false;
}

bool RequestQueryCastleSiegeForDismiss(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004685e4. Current L2Server's paired sender proves
    // that the third request field is the pledge identifier consumed here.
    L2_GUARD_FUNCTION();

    int requestField1 = 0;
    int requestField2 = 0;
    std::uint32_t pledgeId = 0;
    int requestField4 = 0;
    ::Disassemble(packet, "dddd", &requestField1, &requestField2, &pledgeId, &requestField4);

    CPacketHelperEx<20480> relatedSieges;
    if (g_castleDb.GetRelatedCastleSiege(relatedSieges, 0, pledgeId, 0) < 0) {
        // Log.Add(
        //     LOG_ERROR, "[CacheSocket.cpp][%d] GetRelatedCastleSiege failed [%d]", 0x136b,
        //     static_cast<int>(pledgeId)
        // )
        // L2_LOG_ERROR(Log, "[CacheSocket.cpp][%d] GetRelatedCastleSiege failed [%d]", 0x136b,
        //     static_cast<int>(pledgeId)
        // )
        L2_LOG_ERROR(Log, "[siege] Related-siege lookup failed for clan %u in the dismissal request.", static_cast<unsigned int>(pledgeId));
    }

    socket->Send(
        "cdddddb", 0x49, requestField1, requestField2, static_cast<int>(pledgeId), requestField4,
        relatedSieges.GetAssembledCount(), relatedSieges.GetLength(), relatedSieges.GetPacket()
    );
    return false;
}

bool RequestDelPledgeCrest(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0046b4b8.
    L2_GUARD_FUNCTION();

    std::uint32_t pledgeId = 0;
    ::Disassemble(packet, "d", &pledgeId);

    bool success = false;
    if (pledgeId != 0) {
        CPledgeSP pledge = g_pledgeDb.GetPledge(static_cast<int>(pledgeId), false, runtime::process::CurrentDatabaseId());
        if (pledge && g_pledgeCrestDb.DeleteCrest(pledge->crestId())) {
            success = pledge->DeleteCrest();
        }
    }

    socket->Send("cdd", 0x4a, static_cast<int>(pledgeId), success ? 2 : 0);
    return false;
}

bool RequestModPledgeName(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0045f934.
    L2_GUARD_FUNCTION();

    std::uint32_t pledgeId = 0;
    std::array<char16_t, 25> targetNewName{};
    ::Disassemble(packet, "dS", &pledgeId, 0x32, targetNewName.data());

    bool success = false;
    if (pledgeId != 0 && targetNewName[0] != u'\0') {
        std::wstring newName = FixedUtf16ToWide(targetNewName);
        success = g_pledgeDb.ModifyPledgeName(pledgeId, newName.data());
    }

    socket->Send("cddS", 0x4b, static_cast<int>(pledgeId), success ? 2 : 0, targetNewName.data());
    return false;
}

bool ResetCastleSiegePledge(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x0048e898. Current L2Server 0x00648a2c assembles
    // each entry as pledgeId, registrationType, worldId.
    L2_GUARD_FUNCTION();

    std::uint32_t castleId = 0;
    int pledgeCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "dd", &castleId, &pledgeCount);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr auto deleteStatement = L"{Call lin_DeleteCastleWarByCastleId (?)}";
    connection.Prepare(deleteStatement);
    connection.BindParameter(&castleId);
    connection.ExecutePrepared(deleteStatement);

    for (int index = 0; index < pledgeCount; ++index) {
        std::uint32_t pledgeId = 0;
        int registrationType = 0;
        int worldId = 0;
        connection.ResetParam();
        cursor = ::Disassemble(cursor, "ddd", &pledgeId, &registrationType, &worldId);
        if (!ExecuteInsertCastleWar(connection, castleId, pledgeId, registrationType, worldId)) {
            continue;
        }

        CPledgeSP pledge = g_pledgeDb.GetPledge(static_cast<int>(pledgeId), false, worldId);
        if (!pledge) {
            WriteMissingResetCastleSiegePledgeAudit(pledgeId, castleId, registrationType, worldId);
            continue;
        }

        std::array<char16_t, 24> pledgeName{};
        const char16_t* const sourceName = pledge->GetName();
        const char16_t* const sourceEnd = std::find(sourceName, sourceName + pledgeName.size(), u'\0');
        if (sourceEnd != sourceName + pledgeName.size()) {
            std::copy(sourceName, sourceEnd, pledgeName.begin());
        }

        CUserSP ruler = g_userDb.GetUser(static_cast<int>(pledge->rullerId()), true, false, pledge->GetWorldId());
        if (ruler) {
            WriteResetCastleSiegePledgeAudit(*ruler.get(), pledgeName.data(), pledge->GetId(), castleId, registrationType);
        }
    }
    return false;
}

bool RequestPartyTelBookMarkPacket(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0048f1f0.
    L2_GUARD_FUNCTION();

    std::uint32_t characterId = 0;
    int userServerId = 0;
    std::array<char16_t, 50> targetBookmarkName{};
    ::Disassemble(packet, "ddS", &characterId, &userServerId, 100, targetBookmarkName.data());
    if (userServerId == 0)
        return false;

    int worldId = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    int found = 0;
    const std::u16string bookmarkName(FixedUtf16View(targetBookmarkName));

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&worldId);
    connection.Bind(&x);
    connection.Bind(&y);
    connection.Bind(&z);
    constexpr auto statement = L"{Call lin_GetBookMark (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(bookmarkName.c_str());
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        found = 1;
    }

    socket->Send("cdddddd", 0x4c, userServerId, found, worldId, x, y, z);
    return false;
}

bool RequestLoadDoor(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0048ff14.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 50> targetDoorName{};
    int defaultHp = 0;
    ::Disassemble(packet, "Sd", 100, targetDoorName.data(), &defaultHp);
    const std::u16string doorName(FixedUtf16View(targetDoorName));

    int hp = 0;
    int maxHp = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&hp);
    connection.Bind(&maxHp);
    constexpr auto loadStatement = L"{Call lin_LoadDoor (?)}";
    connection.Prepare(loadStatement);
    connection.BindParameter(doorName.c_str());
    if (!connection.ExecutePrepared(loadStatement) || !connection.Fetch()) {
        connection.UnBind();
        constexpr auto createStatement = L"{Call lin_CreateDoorDataIfNotExist (?, ?)}";
        connection.Prepare(createStatement);
        connection.BindParameter(doorName.c_str());
        connection.BindParameter(&defaultHp);
        if (!connection.ExecutePrepared(createStatement)) {
            // Log.Add(
            //     LOG_ERROR, "Cannot Create Door into DB (%s, %d)", WideToUtf8(doorName).c_str(), defaultHp
            // )
            // L2_LOG_ERROR(Log, "Cannot Create Door into DB (%s, %d)", WideToUtf8(doorName).c_str(), defaultHp
            // )
            // L2_LOG_ERROR(Log, "[doors] Cannot create database record for door '%s': database operation failed. Reply will use default HP %d for current and maximum HP.",
            //     WideToUtf8(doorName).c_str(), defaultHp)
            L2_LOG_ERROR(Log, "[doors] Cannot create database record for door '%s': database operation failed. Reply will use default HP %d for current and maximum HP.",
                Utf16ToUtf8(doorName).c_str(), defaultHp);
        }
        hp = defaultHp;
        maxHp = defaultHp;
    }

    socket->Send("cSdd", 0x4e, targetDoorName.data(), hp, maxHp);
    return false;
}

bool RequestSaveCastleStatus(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x00490178.
    L2_GUARD_FUNCTION();

    std::uint32_t castleId = 0;
    int status = 0;
    ::Disassemble(packet, "dd", &castleId, &status);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr auto statement = L"{Call lin_SaveCastleStatus (?, ?)}";
    connection.Prepare(statement);
    // The shipping procedure order is status, then castle identifier.
    connection.BindParameter(&status);
    connection.BindParameter(&castleId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "Error occurred in Saving Castle Status")
        // L2_LOG_ERROR(Log, "Error occurred in Saving Castle Status")
        L2_LOG_ERROR(Log, "[castles] Cannot save status %d for castle %u: database operation failed.", status, static_cast<unsigned int>(castleId));
    }
    return false;
}

bool RequestSaveDoorStatus(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x004902f4.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 50> targetDoorName{};
    int hp = 0;
    int maxHp = 0;
    ::Disassemble(packet, "Sdd", 100, targetDoorName.data(), &hp, &maxHp);
    const std::u16string doorName(FixedUtf16View(targetDoorName));

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr auto statement = L"{Call lin_SaveDoorStatus (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&hp);
    connection.BindParameter(&maxHp);
    connection.BindParameter(doorName.c_str());
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "Error occurred in Saving Door Status")
        // L2_LOG_ERROR(Log, "Error occurred in Saving Door Status")
        // L2_LOG_ERROR(Log, "[doors] Cannot save door '%s' (HP %d, maximum HP %d): database operation failed.",
        //     WideToUtf8(doorName).c_str(), hp, maxHp)
        L2_LOG_ERROR(Log, "[doors] Cannot save door '%s' (HP %d, maximum HP %d): database operation failed.",
            Utf16ToUtf8(doorName).c_str(), hp, maxHp);
    } else {
        WriteDoorHpChangedAudit(targetDoorName.data(), hp);
    }
    return false;
}

bool RequestLoadWarehouseItemList(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0046b660.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int userSocketId = 0;
    int warehouseType = 0;
    ::Disassemble(packet, "ddd", &characterId, &userSocketId, &warehouseType);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdddd", 0x4f, characterId, userSocketId, warehouseType, 0);
        // Log.Add(LOG_ERROR, "cannot find user. user dbid [%d] ", characterId)
        // L2_LOG_ERROR(Log, "cannot find user. user dbid [%d] ", characterId)
        L2_LOG_ERROR(Log, "[warehouse] Cannot load item list for warehouse type %d: character %d unavailable. Empty-list reply requested for socket %d.",
            warehouseType, characterId, userSocketId);
        return false;
    }

    CWareHouseSP warehouse = user->GetWareHouse(warehouseType, true);
    if (!warehouse) {
        socket->Send("cdddd", 0x4f, characterId, userSocketId, warehouseType, 0);
        // Log.Add(LOG_ERROR, "cannot find warehouse[%d]. user dbid [%d] ", warehouseType, characterId)
        // L2_LOG_ERROR(Log, "cannot find warehouse[%d]. user dbid [%d] ", warehouseType, characterId)
        L2_LOG_ERROR(Log, "[warehouse] Cannot load item list for character %d: warehouse type %d unavailable. Empty-list reply requested for socket %d.",
            characterId, warehouseType, userSocketId);
        return false;
    }

    {
        L2_WRITE_GUARD(*warehouse.operator->());
        warehouse->SendItemList(socket, userSocketId);
    }
    return false;
}

bool RequestCrystallizeItem(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0047c71c.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int petId = 0;
    const std::uint8_t* const itemPacket = ::Disassemble(packet, "dd", &characterId, &petId);

    auto sendFailure = [&]() {
        int sourceItemId = 0;
        int itemServerId = 0;
        int userServerId = 0;
        int generatedItemCount = 0;
        const std::uint8_t* cursor =
            ::Disassemble(itemPacket, "dddd", &sourceItemId, &itemServerId, &userServerId, &generatedItemCount);
        for (int index = 0; index < generatedItemCount; ++index) {
            int generatedItemType = 0;
            std::int64_t generatedAmount = 0;
            cursor = ::Disassemble(cursor, "dQ", &generatedItemType, &generatedAmount);
        }
        socket->Send("cddQ", 0x50, userServerId, itemServerId, static_cast<long long>(-1));
    };

    const int worldId = runtime::process::CurrentDatabaseId();
    CUserSP user = g_userDb.GetUser(characterId, true, false, worldId);
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x19e7, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x19e7, characterId)
        L2_LOG_ERROR(Log, "[items] Crystallization failed: character %d unavailable (pet context %d). Sending failure reply.", characterId, petId);
        sendFailure();
        return false;
    }

    if (petId < 1) {
        CWareHouseSP inventory = user->GetWareHouse(WareHouseType_Inventory, true);
        if (!inventory) {
            // Log.Add(LOG_ERROR, "[%s][%d]Cannot find warehouse 0.", "CacheSocket.cpp", 0x1a05)
            // L2_LOG_ERROR(Log, "[%s][%d]Cannot find warehouse 0.", "CacheSocket.cpp", 0x1a05)
            L2_LOG_ERROR(Log, "[items] Crystallization failed for character %d: inventory unavailable. Sending failure reply.", characterId);
            sendFailure();
            return false;
        }

        {
            L2_WRITE_GUARD(*inventory.operator->());
            inventory->CrystallizeItem(socket, itemPacket, false);
        }
        return false;
    }

    CPetSP pet = g_petDb.GetPet(petId, worldId);
    if (!pet) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find pet, id[%d], userID[%d]", "CacheSocket.cpp", 0x19f0, petId, characterId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find pet, id[%d], userID[%d]", "CacheSocket.cpp", 0x19f0, petId, characterId
        // )
        L2_LOG_ERROR(Log, "[items] Crystallization failed for character %d: pet %d unavailable. Sending failure reply.", characterId, petId);
        sendFailure();
        return false;
    }

    CPetWarehouseSP petWarehouse = pet->GetWarehouse();
    if (!petWarehouse) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find pet warehouse, id[%d], userID[%d]", "CacheSocket.cpp", 0x19f7, petId,
        //     characterId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find pet warehouse, id[%d], userID[%d]", "CacheSocket.cpp", 0x19f7, petId,
        //     characterId
        // )
        L2_LOG_ERROR(Log, "[items] Crystallization failed for character %d: pet %d inventory unavailable. Sending failure reply.", characterId, petId);
        sendFailure();
        return false;
    }

    {
        L2_WRITE_GUARD(*petWarehouse.operator->());
        petWarehouse->CrystallizeItem(socket, itemPacket, true);
    }
    return false;
}

bool RequestSaveCastleIncome(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x004904e0.
    L2_GUARD_FUNCTION();

    int residenceId = 0;
    long long shopIncome = 0;
    long long shopIncomeTemp = 0;
    int isCastle = 0;
    int incomeReason = 0;
    ::Disassemble(packet, "dQQdd", &residenceId, &shopIncome, &shopIncomeTemp, &isCastle, &incomeReason);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveCastleIncome (?, ?, ?, ?, ?)}";
    const int cropIncome = 0;
    connection.Prepare(statement);
    connection.BindParameter(&cropIncome);
    connection.BindParameter(&shopIncome);
    connection.BindParameter(&residenceId);
    connection.BindParameter(&shopIncomeTemp);
    connection.BindParameter(&isCastle);
    // The current unnamed helper at 0x00484744 discards the SQL result.
    connection.ExecutePrepared(statement);

    WriteSaveCastleIncomeAudit(residenceId, shopIncome, shopIncomeTemp, incomeReason);
    return false;
}

bool RequestInstallAgitDeco(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0048c8d4.
    L2_GUARD_FUNCTION();

    int agitId = 0;
    int decoType = 0;
    int value = 0;
    int expire = 0;
    ::Disassemble(packet, "dddd", &agitId, &decoType, &value, &expire);

    DBConn connection(runtime::process::CurrentDatabaseId());
    const wchar_t* statement = nullptr;
    switch (decoType) {
    case 0:
        statement = L"{Call lin_UpdateAgitStove (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&value);
        connection.BindParameter(&expire);
        connection.BindParameter(&agitId);
        break;
    case 1:
        statement = L"{Call lin_UpdateAgitFlame (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&value);
        connection.BindParameter(&expire);
        connection.BindParameter(&agitId);
        break;
    case 2:
        statement = L"{Call lin_UpdateAgitHatcher (?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&value);
        connection.BindParameter(&agitId);
        break;
    case 3:
        statement = L"{Call lin_UpdateAgitTeleport (?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&value);
        connection.BindParameter(&agitId);
        break;
    default:
        break;
    }

    if (statement != nullptr && connection.ExecutePrepared(statement)) {
        socket->Send("cdddd", 0x51, agitId, decoType, value, expire);
        return false;
    }

    socket->Send("cdddd", 0x51, 0, 0, 0, 0);
    // Log.Add(
    //     LOG_ERROR, "Cannot Install Agit[%d] Deco(type:%d value:%d expire:%d)", agitId, decoType, value, expire
    // )
    // L2_LOG_ERROR(Log, "Cannot Install Agit[%d] Deco(type:%d value:%d expire:%d)", agitId, decoType, value, expire
    // )
    // L2_LOG_ERROR(Log, "[clan halls] Cannot install decoration for hall %d (type %d, value %d, expiry %d): %s. Failure reply sent.",
    //     agitId, decoType, value, expire, statement == nullptr ? "unsupported decoration type" : "database operation failed")
    L2_LOG_ERROR(Log, "[clan halls] Cannot install decoration for hall %d (type %d, value %d, expiry %d): %s. Failure reply attempted; delivery is not confirmed.",
        agitId, decoType, value, expire, statement == nullptr ? "unsupported decoration type" : "database operation failed");
    return false;
}

bool RequestSavePledgeContribution(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0048cef4.
    L2_GUARD_FUNCTION();

    int residenceId = 0;
    int registerOnly = 0;
    int resetContribution = 0;
    int contributionCount = 0;
    const std::uint8_t* cursor =
        ::Disassemble(packet, "dddd", &residenceId, &registerOnly, &resetContribution, &contributionCount);

    const int worldId = runtime::process::CurrentDatabaseId();
    DBConn connection(worldId);
    constexpr const wchar_t* saveStatement = L"{Call lin_SavePledgeContribution (?, ?, ?)}";
    for (int index = 0; index < contributionCount; ++index) {
        int pledgeId = 0;
        int contribution = 0;
        cursor = ::Disassemble(cursor, "dd", &pledgeId, &contribution);

        connection.Prepare(saveStatement);
        connection.BindParameter(&residenceId);
        connection.BindParameter(&pledgeId);
        connection.BindParameter(&contribution);
        if (!connection.ExecutePrepared(saveStatement)) {
            // Log.Add(
            //     LOG_ERROR, "Failed to Save Pledge Contribution PledgeId[%d], Contribute[%d], Residence[%d]",
            //     pledgeId, contribution, residenceId
            // )
            // L2_LOG_ERROR(Log, "Failed to Save Pledge Contribution PledgeId[%d], Contribute[%d], Residence[%d]",
            //     pledgeId, contribution, residenceId
            // )
            L2_LOG_ERROR(Log, "[clans] Database operation failed while saving contribution %d for clan %d, residence %d. Continuing contribution processing.",
                contribution, pledgeId, residenceId);
        }
        connection.ResetParam();

        CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, worldId);
        if (pledge) {
            WritePledgeContributionAudit(pledge->GetName(), residenceId, pledgeId, resetContribution, contribution);
        }
    }

    connection.UnBind();
    int winnerPledgeId = 0;
    connection.Bind(&winnerPledgeId);
    constexpr const wchar_t* winnerStatement = L"{Call lin_GetContributionWinnerPledge (?, ?)}";
    connection.Prepare(winnerStatement);
    connection.BindParameter(&residenceId);
    connection.BindParameter(&registerOnly);
    if (!connection.ExecutePrepared(winnerStatement)) {
        // Log.Add(LOG_ERROR, "Cannot Get Contribution Winner[%d]", residenceId)
        // L2_LOG_ERROR(Log, "Cannot Get Contribution Winner[%d]", residenceId)
        L2_LOG_ERROR(Log, "[clans] Cannot query contribution winner for residence %d (register-only %d): database operation failed.", residenceId, registerOnly);
    } else if (!connection.Fetch()) {
        // Log.Add(LOG_NORMAL, "No Contribution Winner Exists[%d]", residenceId)
        // L2_LOG_INFO(Log, "No Contribution Winner Exists[%d]", residenceId)
        L2_LOG_INFO(Log, "[clans] No contribution-winner row fetched for residence %d (register-only %d).", residenceId, registerOnly);
    } else {
        CPledgeSP pledge = g_pledgeDb.GetPledge(winnerPledgeId, false, worldId);
        if (pledge) {
            WriteContributionWinnerAudit(pledge->GetName(), residenceId, winnerPledgeId, resetContribution);
        }
    }

    socket->Send("cddd", 0x52, residenceId, resetContribution, winnerPledgeId);

    if (resetContribution != 0) {
        connection.UnBind();
        constexpr const wchar_t* deleteStatement = L"{Call lin_DeletePledgeContribution (?)}";
        connection.Prepare(deleteStatement);
        connection.BindParameter(&residenceId);
        if (!connection.ExecutePrepared(deleteStatement)) {
            // Log.Add(LOG_ERROR, "Failed to Delete Pledge Contribution[%d]", residenceId)
            // L2_LOG_ERROR(Log, "Failed to Delete Pledge Contribution[%d]", residenceId)
            L2_LOG_ERROR(Log, "[clans] Database operation failed while resetting contributions for residence %d. Winner reply was already sent (clan %d).",
                residenceId, winnerPledgeId);
        }
    }
    return false;
}

bool RequestGetContributionRelatedPledge(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0048d3f0.
    L2_GUARD_FUNCTION();

    int residenceId = 0;
    ::Disassemble(packet, "d", &residenceId);

    unsigned int pledgeId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&pledgeId);
    constexpr const wchar_t* statement = L"{Call lin_GetContributionRelatedPledge (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&residenceId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "Failed to Get Contribution Related Pledge[%d]", residenceId)
        // L2_LOG_ERROR(Log, "Failed to Get Contribution Related Pledge[%d]", residenceId)
        L2_LOG_ERROR(Log, "[clans] Cannot query contribution participants for residence %d: database operation failed. No reply is sent.", residenceId);
        return false;
    }

    std::array<char, 0x5000> response{};
    int responseSize = 0;
    int pledgeCount = 0;
    while (connection.Fetch()) {
        responseSize +=
            ::Assemble(response.data() + responseSize, static_cast<int>(response.size()) - responseSize, "d", pledgeId);
        ++pledgeCount;
    }

    if (pledgeCount == 0) {
        socket->Send("cdd", 0x53, 0, 0);
    } else {
        socket->Send("cddb", 0x53, residenceId, pledgeCount, responseSize, response.data());
    }
    return false;
}

bool RequestSetAgitInfo(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x0048cc04.
    L2_GUARD_FUNCTION();

    int agitId = 0;
    int infoType = 0;
    int value = 0;
    ::Disassemble(packet, "ddd", &agitId, &infoType, &value);

    DBConn connection(runtime::process::CurrentDatabaseId());
    const wchar_t* statement = nullptr;
    switch (infoType) {
    case 0:
        statement = L"{Call lin_UpdateAgitPledgeId (?, ?)}";
        break;
    case 1:
        statement = L"{Call lin_UpdateAgitAuctionPrice (?, ?)}";
        break;
    case 2:
        statement = L"{Call lin_UpdateAgitAuctionData (?, ?)}";
        break;
    case 3:
        statement = L"{Call lin_UpdateAgitStatus (?, ?)}";
        break;
    case 4:
        statement = L"{Call lin_UpdateAgitNextWarTime (?, ?)}";
        break;
    default:
        break;
    }

    bool success = false;
    if (statement != nullptr) {
        connection.Prepare(statement);
        connection.BindParameter(&value);
        connection.BindParameter(&agitId);
        success = connection.ExecutePrepared(statement);
    }
    if (!success) {
        // Log.Add(LOG_ERROR, "Cannot Set Agit[%d] Deco(type:%d value:%d)", agitId, infoType, value)
        // L2_LOG_ERROR(Log, "Cannot Set Agit[%d] Deco(type:%d value:%d)", agitId, infoType, value)
        L2_LOG_ERROR(Log, "[clan halls] Cannot update hall %d (info type %d, value %d): %s.",
            agitId, infoType, value, statement == nullptr ? "unsupported info type" : "database operation failed");
    }
    return false;
}

bool RequestPrivateStoreCommit(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0046b930.
    L2_GUARD_FUNCTION();

    int vendorCharacterId = 0;
    int purchaserCharacterId = 0;
    const std::uint8_t* const commitPayload = ::Disassemble(packet, "dd", &vendorCharacterId, &purchaserCharacterId);

    CUserSP vendorUser = g_userDb.GetUser(vendorCharacterId, true, false, runtime::process::CurrentDatabaseId());
    CUserSP purchaserUser = g_userDb.GetUser(purchaserCharacterId, true, false, runtime::process::CurrentDatabaseId());
    if (!vendorUser || !purchaserUser) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1a60, vendorCharacterId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1a60, vendorCharacterId
        // )
        L2_LOG_ERROR(Log, "[private store] Cannot commit trade: seller %d %s, buyer %d %s. Sending failure reply.",
            vendorCharacterId, vendorUser ? "available" : "unavailable", purchaserCharacterId, purchaserUser ? "available" : "unavailable");
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1a61, purchaserCharacterId
        // )
        // Noise: the combined diagnostic above identifies which participant is unavailable.
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1a61, purchaserCharacterId
        // )
        socket->Send("cdddd", 0x54, vendorCharacterId, purchaserCharacterId, 0, 0);
        return false;
    }

    CWareHouseSP vendorInventory = vendorUser->GetWareHouse(WareHouseType_Inventory, true);
    CWareHouseSP purchaserInventory = purchaserUser->GetWareHouse(WareHouseType_Inventory, true);
    if (!vendorInventory || !purchaserInventory) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find warehouse, user id[%d]", "CacheSocket.cpp", 0x1a6b, vendorCharacterId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find warehouse, user id[%d]", "CacheSocket.cpp", 0x1a6b, vendorCharacterId
        // )
        L2_LOG_ERROR(Log, "[private store] Cannot commit trade: seller %d inventory %s, buyer %d inventory %s. Sending failure reply.",
            vendorCharacterId, vendorInventory ? "available" : "unavailable", purchaserCharacterId, purchaserInventory ? "available" : "unavailable");
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find warehouse, user id[%d]", "CacheSocket.cpp", 0x1a6c,
        //     purchaserCharacterId
        // )
        // Noise: the combined diagnostic above identifies which inventory is unavailable.
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find warehouse, user id[%d]", "CacheSocket.cpp", 0x1a6c,
        //     purchaserCharacterId
        // )
        socket->Send("cdddd", 0x54, vendorCharacterId, purchaserCharacterId, 0, 0);
        return false;
    }

    auto executeCommit = [&]() {
        if (vendorInventory->m_lateSavingItemsPending) {
            vendorInventory->SaveLateSavingItems();
        }
        if (purchaserInventory->m_lateSavingItemsPending) {
            purchaserInventory->SaveLateSavingItems();
        }
        vendorInventory->PrivateStoreCommit(socket, commitPayload, purchaserInventory.get());
    };

    const std::uint64_t vendorLockAddress = vendorInventory->GetLockAddr();
    const std::uint64_t purchaserLockAddress = purchaserInventory->GetLockAddr();
    if (vendorLockAddress == purchaserLockAddress) {
        // Log.Add(LOG_ERROR, "Same warehouse double writelock?-_-,")
        // L2_LOG_ERROR(Log, "Same warehouse double writelock?-_-,")
        L2_LOG_WARN(Log, "[private store] Seller %d and buyer %d inventories share a lock. Continuing trade with one write lock.",
            vendorCharacterId, purchaserCharacterId);
        {
            L2_WRITE_GUARD(*vendorInventory.operator->());
            executeCommit();
        }
        return false;
    }

    if (vendorLockAddress < purchaserLockAddress) {
        {
            L2_WRITE_GUARD(*vendorInventory.operator->());
            {
                L2_WRITE_GUARD(*purchaserInventory.operator->());
                executeCommit();
            }
        }
        return false;
    }

    {
        L2_WRITE_GUARD(*purchaserInventory.operator->());
        {
            L2_WRITE_GUARD(*vendorInventory.operator->());
            executeCommit();
        }
    }
    return false;
}

bool RequestSaveDieDropExp(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x004906fc.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    std::int64_t dropExperience = 0;
    ::Disassemble(packet, "dQ", &characterId, &dropExperience);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1a92, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1a92, characterId)
        L2_LOG_ERROR(Log, "[characters] Cannot save death experience loss %lld for character %d: character unavailable. Save skipped.",
            static_cast<long long>(dropExperience), characterId);
        return false;
    }

    {
        L2_WRITE_GUARD(*user.operator->());
        if (user->m_dropExperience == 0 && dropExperience == 0) {
            user->m_dropExpSaveArmed = false;
        } else if (!user->m_dropExpSaveArmed) {
            user->m_dropExpSaveArmed = true;
        } else {
            user->SetDropExp(dropExperience);

            DBConn connection(runtime::process::CurrentDatabaseId());
            constexpr const wchar_t* statement = L"{Call lin_SaveDropExp (?, ?)}";
            connection.Prepare(statement);
            connection.BindParameter(&dropExperience);
            connection.BindParameter(&characterId);
            connection.ExecutePrepared(statement);
        }
    }
    return false;
}

bool RequestResetContribution(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x0048d62c.
    L2_GUARD_FUNCTION();

    int residenceId = 0;
    ::Disassemble(packet, "d", &residenceId);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeletePledgeContribution (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&residenceId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "Failed to Delete Pledge Contribution[%d]", residenceId)
        // L2_LOG_ERROR(Log, "Failed to Delete Pledge Contribution[%d]", residenceId)
        L2_LOG_ERROR(Log, "[clans] Database operation failed while deleting contribution data for residence %d.", residenceId);
    }
    return false;
}

bool RequestSaveObjectHp(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x004909e0.
    L2_GUARD_FUNCTION();

    int objectId = 0;
    int hp = 0;
    ::Disassemble(packet, "dd", &objectId, &hp);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveObjectHp (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&hp);
    connection.BindParameter(&objectId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "Error occurred in Save Object[%d] Hp[%d]", objectId, hp)
        // L2_LOG_ERROR(Log, "Error occurred in Save Object[%d] Hp[%d]", objectId, hp)
        L2_LOG_ERROR(Log, "[world] Database operation failed while saving HP %d for object %d.", hp, objectId);
    }
    return false;
}

bool RequestSaveCastleSiegeElapsedTime(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x00490b68.
    L2_GUARD_FUNCTION();

    int castleId = 0;
    int siegeElapsedTime = 0;
    ::Disassemble(packet, "dd", &castleId, &siegeElapsedTime);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveCastleSiegeElapsedTime (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&siegeElapsedTime);
    connection.BindParameter(&castleId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "Error occurred in Save Save[%d] Elapsed Time[%d]", castleId, siegeElapsedTime)
        // L2_LOG_ERROR(Log, "Error occurred in Save Save[%d] Elapsed Time[%d]", castleId, siegeElapsedTime)
        L2_LOG_ERROR(Log, "[siege] Database operation failed while saving elapsed time %d for castle %d.", siegeElapsedTime, castleId);
    }
    return false;
}

bool RequestSaveSkillUse(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x0046c280.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ReuseDelaySaveReason::Type saveReason = ReuseDelaySaveReason::SR_NORMAL;
    int subjobId = 0;
    int number = 0;
    int allJobNumber = 0;
    const std::uint8_t* payload =
        ::Disassemble(packet, "ddddd", &characterId, &saveReason, &subjobId, &number, &allJobNumber);

    if (characterId < 1) {
        // Log.Add(LOG_ERROR, "Error occurred in Save skill use time[%d]", characterId)
        // L2_LOG_ERROR(Log, "Error occurred in Save skill use time[%d]", characterId)
        L2_LOG_WARN(Log, "[skills] Cooldown save rejected: character ID %d must be positive (subclass %d).", characterId, subjobId);
        return false;
    }

    // Shipping instructions encode the original impossible conjunction
    // `saveReason < SR_NORMAL && SR_MAX < saveReason`. Therefore no int32
    // value reaches its SaveReason diagnostic.
    if (static_cast<int>(saveReason) < static_cast<int>(ReuseDelaySaveReason::SR_NORMAL) &&
        static_cast<int>(ReuseDelaySaveReason::SR_MAX) < static_cast<int>(saveReason)) {
        // Log.Add(
        //     LOG_ERROR, "Error occurred in Save skill use time[%d], SaveReason[%d]", characterId,
        //     static_cast<int>(saveReason)
        // )
        // Noise: the kept shipping conjunction cannot reach this diagnostic.
        // L2_LOG_ERROR(Log, "Error occurred in Save skill use time[%d], SaveReason[%d]", characterId,
        //     static_cast<int>(saveReason)
        // )
        return false;
    }
    if (number < 1 && allJobNumber < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Error occurred in Save skill use time[%d], number[%d], "
        //     "nAllJobNumber[%d]",
        //     characterId, number, allJobNumber
        // )
        // L2_LOG_ERROR(Log,
        //     "Error occurred in Save skill use time[%d], number[%d], "
        //     "nAllJobNumber[%d]",
        //     characterId, number, allJobNumber
        // )
        L2_LOG_WARN(Log, "[skills] Cooldown save skipped for character %d, subclass %d: both entry counts are nonpositive (subclass %d, all classes %d).",
            characterId, subjobId, number, allJobNumber);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1b11, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1b11, characterId)
        L2_LOG_ERROR(Log, "[skills] Cannot save cooldowns for character %d, subclass %d: character unavailable. Save skipped.", characterId, subjobId);
        return false;
    }

    user->SetSkillsLastUse(payload, saveReason, subjobId, number, allJobNumber);
    return false;
}

bool SetLoginAnnouncePacket(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x0048f8e8.
    L2_GUARD_FUNCTION();

    int announceId = 0;
    std::array<char16_t, 64> announceMessage{};
    ::Disassemble(packet, "dS", &announceId, 0x80, announceMessage.data());
    const std::u16string databaseMessage(FixedUtf16View(announceMessage));

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetLoginAnnounce (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&announceId);
    connection.BindParameter(databaseMessage.c_str());
    if (connection.ExecutePrepared(statement)) {
        {
            L2_GUARD(g_announce);
            g_announce.ReadAnnounce(true);
        }
    }
    return false;
}

bool DelLoginAnnouncePacket(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x0048faac.
    L2_GUARD_FUNCTION();

    int announceId = 0;
    ::Disassemble(packet, "d", &announceId);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DelLoginAnnounce (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&announceId);
    if (connection.ExecutePrepared(statement)) {
        {
            L2_GUARD(g_announce);
            g_announce.ReadAnnounce(true);
        }
    }
    return false;
}

bool RequestShowIntervalAnnounce(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0045bb5c.
    L2_GUARD_FUNCTION();
    g_announce.ShowIntervalAnnounce(socket, packet);
    return false;
}

bool RequestSetIntervalAnnounce(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0045bc0c.
    L2_GUARD_FUNCTION();
    g_announce.SetIntervalAnnounce(socket, packet);
    return false;
}

bool RequestDelIntervalAnnounce(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0045bcbc.
    L2_GUARD_FUNCTION();
    g_announce.DelIntervalAnnounce(socket, packet);
    return false;
}

bool RequestLoadIntervalAnnounce(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0045bd6c.
    L2_GUARD_FUNCTION();
    g_announce.LoadIntervalAnnounce(socket, packet);
    return false;
}

bool RequestDeletePledge(CSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int userSocketId = 0;
    int pledgeSocketId = 0;
    int pledgeId = 0;
    ::Disassemble(packet, "ddd", &userSocketId, &pledgeSocketId, &pledgeId);

    std::uint32_t rulerId = 0;
    std::array<char16_t, 24> pledgeName{};
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (pledge) {
        L2_READ_GUARD(*pledge.operator->());
        rulerId = pledge->rullerId();
        std::copy_n(pledge->GetName(), pledgeName.size(), pledgeName.begin());
    }

    const bool success = g_pledgeDb.DeletePledge(static_cast<std::uint32_t>(pledgeId));
    socket->Send("cddd", 0x2f, userSocketId, pledgeSocketId, success ? 1 : 0);
    if (!success)
        return false;

    CUserSP ruler = g_userDb.GetUser(static_cast<int>(rulerId), true, false, runtime::process::CurrentDatabaseId());
    if (ruler) {
        const std::u16string characterName = std::u16string(ruler->GetCharName());
        const std::u16string accountName = std::u16string(ruler->GetAccountName());
        CLogFormatter formatter;
        formatter.WriteLog(
            0xcb, ruler->GetId(), ruler->GetAccountID(), 0, 0, ruler->GetX(), ruler->GetY(), ruler->GetZ(),
            pledgeName.data(), u"", u"", ruler->GetRace(), ruler->GetGender(), ruler->GetClass(), ruler->GetLevel(),
            pledgeId, 0, 0, 0, 0, 0, characterName.c_str(), accountName.c_str(), u"", u"", 0
        );
    }

    CPledgeJoinDB::Instance().RequestPledgeDelete(pledgeId);
    if (!PledgeContributionInCache::DeleteAllInfo(pledgeId)) {
        // Log.Add(
        //     LOG_ERROR, "PLEDGE[%s][%d]Failed delete contribution info all in : %d", "CacheSocket.cpp", 0xa57,
        //     pledgeId
        // )
        // L2_LOG_ERROR(Log, "PLEDGE[%s][%d]Failed delete contribution info all in : %d", "CacheSocket.cpp", 0xa57,
        //     pledgeId
        // )
        L2_LOG_ERROR(Log, "[clans] Contribution cleanup failed for deleted clan %d. The clan-deletion success reply was already sent.", pledgeId);
    }
    return false;
}

bool LoadWarehouseItemsPacket(CSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int userSocketId = 0;
    int warehouseNumber = 0;
    int contextId = 0;
    int requestedIdent = 0;
    int memoryId = 0;
    int warehouseClass = 0;
    int itemListGetType = 0;
    ::Disassemble(
        packet, "dddddddd", &characterId, &userSocketId, &warehouseNumber, &contextId, &requestedIdent, &memoryId,
        &warehouseClass, &itemListGetType
    );

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Load warehouse item error. Cannot find user. "
        //     "id[%d], warehouse[%d]",
        //     characterId, warehouseNumber
        // )
        // L2_LOG_ERROR(Log,
        //     "Load warehouse item error. Cannot find user. "
        //     "id[%d], warehouse[%d]",
        //     characterId, warehouseNumber
        // )
        L2_LOG_ERROR(Log, "[warehouse] Cannot load item list for warehouse %d: character %d unavailable (request context %d). Sending failure reply.",
            warehouseNumber, characterId, contextId);
        // Shipping 0x0046178d is one integer shorter than the warehouse-null
        // failure below. The paired receiver still attempts nine reads.
        socket->Send(
            "cdddddddd", 9, userSocketId, warehouseNumber, warehouseClass, itemListGetType, 0, contextId, memoryId, 0
        );
        return false;
    }

    CWareHouseSP warehouse = user->GetWareHouse(warehouseNumber, true);
    if (!warehouse) {
        // Diagnostic addition: distinguish missing storage from the character lookup failure above.
        L2_LOG_ERROR(Log, "[warehouse] Cannot load item list for character %d: warehouse %d unavailable (request context %d). Sending failure reply.",
            characterId, warehouseNumber, contextId);
        socket->Send(
            "cddddddddd", 9, userSocketId, warehouseNumber, warehouseClass, itemListGetType, 0, contextId, memoryId, 0, 0
        );
        return false;
    }

    {
        L2_WRITE_GUARD(*warehouse.operator->());
        warehouse->SendDepositItemsList(
            socket, userSocketId, contextId, requestedIdent, memoryId, warehouseClass, itemListGetType
        );
    }
    return false;
}

bool WithdrawWarehouseItemsPacket(CSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int warehouseNumber = 0;
    const std::uint8_t* payload = ::Disassemble(packet, "dd", &characterId, &warehouseNumber);

    auto sendFailure = [&]() {
        int responseId = 0;
        ::Disassemble(payload, "d", &responseId);
        socket->Send("cdd", 10, responseId, -2);
    };

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Withdraw warehouse item error. Cannot find user. "
        //     "id[%d], warehouse[%d]",
        //     characterId, warehouseNumber
        // )
        // L2_LOG_ERROR(Log,
        //     "Withdraw warehouse item error. Cannot find user. "
        //     "id[%d], warehouse[%d]",
        //     characterId, warehouseNumber
        // )
        L2_LOG_ERROR(Log, "[warehouse] Withdrawal from warehouse %d failed: character %d unavailable. Sending failure reply (-2).",
            warehouseNumber, characterId);
        sendFailure();
        return false;
    }

    CWareHouseSP inventory = user->GetWareHouse(WareHouseType_Inventory, true);
    CWareHouseSP source = user->GetWareHouse(warehouseNumber, true);
    if (!inventory || !source) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Withdraw warehouse item error. Cannot find warehouse. "
        //     "id[%d], warehouse[%d]",
        //     characterId, warehouseNumber
        // )
        // L2_LOG_ERROR(Log,
        //     "Withdraw warehouse item error. Cannot find warehouse. "
        //     "id[%d], warehouse[%d]",
        //     characterId, warehouseNumber
        // )
        L2_LOG_ERROR(Log, "[warehouse] Withdrawal from warehouse %d failed for character %d: inventory %s, source warehouse %s. Sending failure reply (-2).",
            warehouseNumber, characterId, inventory ? "available" : "unavailable", source ? "available" : "unavailable");
        sendFailure();
        return false;
    }

    const std::uint64_t sourceLock = source->GetLockAddr();
    const std::uint64_t inventoryLock = inventory->GetLockAddr();
    if (sourceLock == inventoryLock) {
        // Log.Add(LOG_ERROR, "Same warehouse double writelock?-_-,")
        // L2_LOG_ERROR(Log, "Same warehouse double writelock?-_-,")
        L2_LOG_WARN(Log, "[warehouse] Withdrawal from warehouse %d for character %d uses the same lock as the inventory. Continuing with one write lock.",
            warehouseNumber, characterId);
        {
            L2_WRITE_GUARD(*source.operator->());
            source->Withdraw(socket, payload, inventory.get());
        }
        return false;
    }

    if (sourceLock < inventoryLock) {
        {
            L2_WRITE_GUARD(*source.operator->());
            {
                L2_WRITE_GUARD(*inventory.operator->());
                source->Withdraw(socket, payload, inventory.get());
            }
        }
        return false;
    }

    {
        L2_WRITE_GUARD(*inventory.operator->());
        {
            L2_WRITE_GUARD(*source.operator->());
            source->Withdraw(socket, payload, inventory.get());
        }
    }
    return false;
}

bool DepositWarehouseItemsPacket(CSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int warehouseNumber = 0;
    const std::uint8_t* payload = ::Disassemble(packet, "dd", &characterId, &warehouseNumber);

    auto sendFailure = [&]() {
        int responseId = 0;
        ::Disassemble(payload, "d", &responseId);
        socket->Send("cdd", 12, responseId, -2);
    };

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Deposit warehouse item error. Cannot find user. "
        //     "id[%d], warehouse[%d]",
        //     characterId, warehouseNumber
        // )
        // L2_LOG_ERROR(Log,
        //     "Deposit warehouse item error. Cannot find user. "
        //     "id[%d], warehouse[%d]",
        //     characterId, warehouseNumber
        // )
        L2_LOG_ERROR(Log, "[warehouse] Deposit into warehouse %d failed: character %d unavailable. Sending failure reply (-2).",
            warehouseNumber, characterId);
        sendFailure();
        return false;
    }

    CWareHouseSP inventory = user->GetWareHouse(WareHouseType_Inventory, true);
    CWareHouseSP target = user->GetWareHouse(warehouseNumber, true);
    if (!inventory || !target) {
        // The current handler retains this exact "user" wording even when
        // the failed lookup is one of the two warehouses.
        // Log.Add(
        //     LOG_ERROR,
        //     "Deposit warehouse item error. Cannot find user. "
        //     "id[%d], warehouse[%d]",
        //     characterId, warehouseNumber
        // )
        // L2_LOG_ERROR(Log,
        //     "Deposit warehouse item error. Cannot find user. "
        //     "id[%d], warehouse[%d]",
        //     characterId, warehouseNumber
        // )
        L2_LOG_ERROR(Log, "[warehouse] Deposit into warehouse %d failed for character %d: inventory %s, target warehouse %s. Sending failure reply (-2).",
            warehouseNumber, characterId, inventory ? "available" : "unavailable", target ? "available" : "unavailable");
        sendFailure();
        return false;
    }

    const std::uint64_t targetLock = target->GetLockAddr();
    const std::uint64_t inventoryLock = inventory->GetLockAddr();
    if (targetLock == inventoryLock) {
        // Log.Add(LOG_ERROR, "Same warehouse double writelock?-_-,")
        // L2_LOG_ERROR(Log, "Same warehouse double writelock?-_-,")
        L2_LOG_WARN(Log, "[warehouse] Deposit into warehouse %d for character %d uses the same lock as the inventory. Continuing with one write lock.",
            warehouseNumber, characterId);
        {
            L2_WRITE_GUARD(*target.operator->());
            target->Deposit(socket, payload, inventory.get());
        }
        return false;
    }

    if (targetLock < inventoryLock) {
        {
            L2_WRITE_GUARD(*target.operator->());
            {
                L2_WRITE_GUARD(*inventory.operator->());
                target->Deposit(socket, payload, inventory.get());
            }
        }
        return false;
    }

    {
        L2_WRITE_GUARD(*inventory.operator->());
        {
            L2_WRITE_GUARD(*target.operator->());
            target->Deposit(socket, payload, inventory.get());
        }
    }
    return false;
}

bool SellItemsPacket(CSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int responseId = 0;
    const std::uint8_t* payload = ::Disassemble(packet, "dd", &characterId, &responseId);

    auto sendFailure = [&]() {
        socket->Send("cddQdd", 13, responseId, 0, static_cast<long long>(0), static_cast<int>(BSR_INVALID_ITEMS), 0);
    };

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0xb77, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0xb77, characterId)
        L2_LOG_ERROR(Log, "[items] Sale request %d failed: character %d unavailable. Sending invalid-items reply.", responseId, characterId);
        sendFailure();
        return false;
    }

    CWareHouseSP inventory = user->GetWareHouse(WareHouseType_Inventory, true);
    if (!inventory) {
        sendFailure();
        return false;
    }

    {
        L2_WRITE_GUARD(*inventory.operator->());
        inventory->SellItems(socket, payload, static_cast<unsigned int>(responseId));
    }
    return false;
}

bool BuyItemsPacket(CSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int responseId = 0;
    const std::uint8_t* payload = ::Disassemble(packet, "dd", &characterId, &responseId);

    auto sendFailure = [&]() {
        socket->Send("cddQddd", 14, responseId, 0, static_cast<std::int64_t>(0), 0, static_cast<int>(BSR_INVALID_ITEMS), 0);
    };

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0xbd7, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0xbd7, characterId)
        L2_LOG_ERROR(Log, "[items] Purchase request %d failed: character %d unavailable. Sending invalid-items reply.", responseId, characterId);
        sendFailure();
        return false;
    }

    CWareHouseSP inventory = user->GetWareHouse(WareHouseType_Inventory, true);
    if (!inventory) {
        sendFailure();
        return false;
    }

    {
        L2_WRITE_GUARD(*inventory.operator->());
        inventory->BuyItems(socket, payload, static_cast<unsigned int>(responseId));
    }
    return false;
}

bool RequestLoadPledge(CSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int pledgeId = 0;
    std::uint8_t checkPendingWrite = 0;
    ::Disassemble(packet, "dc", &pledgeId, &checkPendingWrite);

    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        // Log.Add(LOG_ERROR, "RP_LOAD_PLEDGE[%d], cannot load pledge.", pledgeId)
        // L2_LOG_ERROR(Log, "RP_LOAD_PLEDGE[%d], cannot load pledge.", pledgeId)
        L2_LOG_ERROR(Log, "[clans] Cannot load clan %d. No load reply is sent.", pledgeId);
    } else {
        pledge->SendReplyToLoadPledge(socket, checkPendingWrite == 1);
    }
    return false;
}

bool RequestLoadPledge2(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00463cec. The handler has no
    // handler-owned failure log: GetPledge(name, false) reports a failed
    // load, and only a found pledge produces the normal load reply.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 24> targetPledgeName{};
    ::Disassemble(packet, "S", static_cast<int>(targetPledgeName.size() * sizeof(char16_t)), targetPledgeName.data());

    std::wstring pledgeName = FixedUtf16ToWide(targetPledgeName);
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeName.data(), false);
    if (pledge) {
        // The target canonicalizes its local 24-wide buffer from CPledge
        // before sending. The local copy has no later external use.
        std::copy_n(pledge->GetName(), targetPledgeName.size(), targetPledgeName.begin());
        pledge->SendReplyToLoadPledge(socket, false);
    }
    return false;
}

bool RequestPing(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0046c4bc. Paired current L2Server CDB::Ping is
    // 0x006496f0 and its PingTest reply handler is 0x006aba84.
    L2_GUARD_FUNCTION();

    int reserved = 0;
    int characterId = 0;
    int userSocketId = 0;
    int requestTick = 0;
    ::Disassemble(packet, "dddd", &reserved, &characterId, &userSocketId, &requestTick);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(
        //     LOG_ERROR, "[PING][%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1b37, characterId
        // )
        // L2_LOG_ERROR(Log, "[PING][%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1b37, characterId
        // )
        L2_LOG_ERROR(Log, "[L2Server ping] Cannot check persistence for character %d (request tick %d): character unavailable. Sending failure reply.",
            characterId, requestTick);
        socket->Send("cd", 0x55, 0);
        return false;
    }

    const std::uint32_t userSaveBegin = runtime::GetTickCount();
    user->Save();
    const std::uint32_t userSaveElapsed = runtime::GetTickCount() - userSaveBegin;

    CWareHouseSP inventory = user->GetWareHouse(WareHouseType_Inventory, true);
    if (!inventory) {
        // The current target passes characterId as an extra variadic value
        // even though its literal has no final conversion specifier.
        // Log.Add(LOG_ERROR, "[PING][%s][%d]Cannot find pWarehouse", "CacheSocket.cpp", 0x1b48, characterId)
        // Log.Add(LOG_ERROR,
        //     "[L2Server ping] Cannot check inventory persistence for character %d (request tick %d): inventory unavailable after the character-save attempt. Returning failure.",
        //     characterId, requestTick)
        L2_LOG_ERROR(Log,
            "[L2Server ping] Cannot check inventory persistence for character %d (request tick %d): inventory unavailable after the character-save attempt; returning failure.",
            characterId, requestTick);
        socket->Send("cd", 0x55, 0);
        return false;
    }

    std::uint32_t inventorySaveElapsed;
    {
        L2_WRITE_GUARD(*inventory.operator->());
        inventorySaveElapsed = inventory->PingTest();
    }

    socket->Send(
        "cdddddd", 0x55, userSocketId, requestTick, reserved, userSaveElapsed, inventorySaveElapsed,
        runtime::crt::Time32Now()
    );
    return false;
}

bool RequestPingOneAdenaDrop(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0047cc9c. The paired current L2Server sender is
    // CDB::PingOneAdenaDrop at 0x006497d4 and its reply owner is 0x006abd60.
    L2_GUARD_FUNCTION();

    const std::int32_t pingReceivedTime = runtime::crt::Time32Now();
    // Log.Add(LOG_NORMAL, "ping adena drop %d", pingReceivedTime)
    // Noise: the timing reply already carries this timestamp. Each probe needs no startup-style INFO.
    // L2_LOG_INFO(Log, "ping adena drop %d", pingReceivedTime)

    int correlationId = 0;
    int pingStartTime = 0;
    int dbSendTime = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    int userSocketId = 0;
    int characterId = 0;
    int itemObjectId = 0;
    int encodedSlotType = 0;
    int amountToDrop = 0;
    ::Disassemble(
        packet, "ddddddddddd", &correlationId, &pingStartTime, &dbSendTime, &x, &y, &z, &userSocketId, &characterId,
        &itemObjectId, &encodedSlotType, &amountToDrop
    );
    // The paired L2Server writes (slotType + 1) % 0x3f here. Cached.exe
    // parses the value but never consults it.

    auto sendFailure = [&]() {
        socket->Send("cdddddddddddddddd", 0x56, userSocketId, 0, 0, 0, 0, 0, x, y, z, 0, 0, 0, 0, 0, 0, correlationId);
    };

    if (amountToDrop < -1) {
        // Log.Add(
        //     LOG_ERROR, "amount is below 0, char id[%d], item id [%d], amount[%d]", characterId, itemObjectId,
        //     amountToDrop
        // )
        // L2_LOG_ERROR(Log, "amount is below 0, char id[%d], item id [%d], amount[%d]", characterId, itemObjectId,
        //     amountToDrop
        // )
        L2_LOG_WARN(Log, "[items] Adena drop probe %d rejected: amount %d is below -1 (character %d, item %d). Sending failure reply.",
            correlationId, amountToDrop, characterId, itemObjectId);
        sendFailure();
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1b86, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1b86, characterId)
        L2_LOG_ERROR(Log, "[items] Adena drop probe %d failed: character %d unavailable (item %d). Sending failure reply.",
            correlationId, characterId, itemObjectId);

        // The current handler's literal requests seventeen dwords but its
        // source-visible argument list initializes only sixteen. Its paired
        // L2Server consumes sixteen and ignores the trailing dword. Preserve
        // the wire width while making that undefined stack value deterministic.
        socket->Send("cddddddddddddddddd", 0x56, userSocketId, 0, 0, 0, 0, 0, x, y, z, 0, 0, 0, 0, 0, 0, correlationId, 0);
        return false;
    }

    // The current handler snapshots all audit identity and position fields
    // before it asks CUser for the inventory.
    const std::int64_t actorId = user->GetId();
    const std::int64_t accountId = user->GetAccountID();
    const std::u16string characterName = std::u16string(user->GetCharName());
    const std::u16string accountName = std::u16string(user->GetAccountName());
    const int auditX = user->GetX();
    const int auditY = user->GetY();
    const int auditZ = user->GetZ();
    const int race = user->GetRace();
    const int gender = user->GetGender();
    const int characterClass = user->GetClass();
    const int level = user->GetLevel();

    CWareHouseSP inventory = user->GetWareHouse(WareHouseType_Inventory, true);
    if (!inventory) {
        sendFailure();
        return false;
    }

    std::optional<CItemSP> sourceItemStorage;
    std::optional<CItemSP> droppedItemStorage;
    {
        L2_WRITE_GUARD(*inventory.operator->());
        auto& sourceItem = sourceItemStorage.emplace(inventory->GetItem(itemObjectId));
        if (!sourceItem) {
            sendFailure();
            return false;
        }

        const std::int64_t originalAmount = sourceItem->Amount();
        const std::int32_t beforeDropTime = runtime::crt::Time32Now();
        auto& droppedItem = droppedItemStorage.emplace(
            inventory->DropItem(sourceItem.get(), static_cast<std::int64_t>(amountToDrop)));

        // Current x64 code computes -(abs(amountToDrop)) in 32-bit arithmetic.
        // Values below -1 have already returned, so the signed operations here
        // cannot overflow.
        const int negativeDropAmount = amountToDrop > 0 ? -amountToDrop : amountToDrop;
        const int absoluteDropAmount = -negativeDropAmount;
        const std::int64_t remainingAmount = originalAmount + negativeDropAmount;

        auto writeDropAudit = [&](CItem& auditItem) {
            std::array<char16_t, 256> shapeShiftAndVariation{};
            std::array<char16_t, 256> baseAttributeText{};
            std::array<char16_t, 256> ensoulText{};
            auditItem.MakeShapeShiftAndVariationLogInfo(shapeShiftAndVariation.data());
            BaseAttribute baseAttribute(auditItem.Attribute());
            baseAttribute.GetLogString(baseAttributeText.data());
            // auditItem.MakeEnsoulLogInfo(ensoulText.data())
            auditItem.MakeEnsoulLogInfo(ensoulText.data(), ensoulText.size());

            const std::u16string targetShapeShift = std::u16string(shapeShiftAndVariation.data());
            const std::u16string targetBaseAttribute = std::u16string(baseAttributeText.data());
            const std::u16string targetEnsoul = std::u16string(ensoulText.data());

            CLogFormatter formatter;
            formatter.WriteAuditLog(
                0x38c, actorId, accountId, 0, 0, auditX, auditY, auditZ, targetShapeShift.c_str(), targetBaseAttribute.c_str(),
                targetEnsoul.c_str(), race, gender, characterClass, level, 0, 0, auditItem.Enchant(), auditItem.ItemType(),
                negativeDropAmount, remainingAmount, characterName.c_str(), accountName.c_str(), nullptr, nullptr,
                auditItem.ItemID()
            );
        };

        auto writeLargeAdenaAudit = [&](CItem& auditItem) {
            if (auditItem.ItemType() != 57U ||
                static_cast<std::uint64_t>(absoluteDropAmount) < GetCacheDConfiguration().adenaLogLimit) {
                return;
            }
            WriteLargeAdenaDropAudit(
                actorId, accountId, auditX, auditY, auditZ, race, gender, characterClass, level, auditItem.Enchant(),
                static_cast<int>(auditItem.ItemType()), negativeDropAmount, remainingAmount, characterName, accountName,
                auditItem.ItemID()
            );
        };

        if (!droppedItem) {
            sendFailure();
            writeDropAudit(*sourceItem.get());
            writeLargeAdenaAudit(*sourceItem.get());
            return false;
        }

        const std::int32_t afterDropTime = runtime::crt::Time32Now();
        socket->Send(
            "cdddddddddddddddd", 0x56, userSocketId, pingStartTime, dbSendTime, pingReceivedTime, beforeDropTime, afterDropTime,
            x, y, z, sourceItem->OwnerID(), sourceItem->ItemID(), absoluteDropAmount, droppedItem->OwnerID(),
            droppedItem->ItemID(), static_cast<int>(droppedItem->Amount()), correlationId
        );

        writeDropAudit(*droppedItem.get());
        writeLargeAdenaAudit(*droppedItem.get());
    }
    return false;
}

bool RequestPingNetwork(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0045fda4. The paired current L2Server sender and
    // reply owner are 0x0064996c and 0x006ac760 respectively.
    L2_GUARD_FUNCTION();

    int reserved = 0;
    int userSocketId = 0;
    int requestTick = 0;
    ::Disassemble(packet, "ddd", &reserved, &userSocketId, &requestTick);
    socket->Send("cddd", 0x57, userSocketId, requestTick, reserved);
    return false;
}

bool CSocket::SaveAbnormalStatus(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x0048a890. Both primary opcode 109 and extended
    // leave-world opcode 334 delegate to this CSocket owner.
    L2_GUARD_FUNCTION();

    unsigned int characterId = 0;
    unsigned int skillCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "dd", &characterId, &skillCount);

    CUserSP user = g_userDb.GetUser(runtime::Int32FromBits(characterId), true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x86b,
        //     runtime::Int32FromBits(characterId)
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x86b,
        //     runtime::Int32FromBits(characterId)
        // )
        L2_LOG_ERROR(Log, "[skills] Cannot save %u active effects: character %u unavailable. Save skipped.",
            skillCount, characterId);
        return false;
    }

    constexpr std::size_t abnormalStatusCount = 96;
    if (skillCount > abnormalStatusCount) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]overflow skill number[%d] char_id[%d]", "CacheSocket.cpp", 0x877,
        //     runtime::Int32FromBits(skillCount), runtime::Int32FromBits(characterId)
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]overflow skill number[%d] char_id[%d]", "CacheSocket.cpp", 0x877,
        //     runtime::Int32FromBits(skillCount), runtime::Int32FromBits(characterId)
        // )
        L2_LOG_WARN(Log, "[skills] Active-effect save rejected for character %u: received %u effects, capacity 96. Save skipped.",
            characterId, skillCount);
        return false;
    }

    std::array<AbnormalStatus, abnormalStatusCount> statuses;
    for (AbnormalStatus& status : statuses) {
        // The current constructor first writes skillId=-1, then the handler
        // zeroes the entire 0x600-byte local carrier.
        status.skillId = 0;
        status.skillLevel = 0;
        status.skillSubLevel = 0;
        status.skillDuration = 0;
        status.skillSelfCast = 0;
    }

    for (unsigned int index = 0; index < skillCount; ++index) {
        int skillId = 0;
        int skillLevel = 0;
        int skillSubLevel = 0;
        int skillDuration = 0;
        int skillSelfCast = 0;
        cursor = ::Disassemble(cursor, "ddddd", &skillId, &skillLevel, &skillSubLevel, &skillDuration, &skillSelfCast);
        if (skillId == 0)
            continue;

        AbnormalStatus& status = statuses[index];
        status.skillId = skillId;
        status.skillLevel = static_cast<std::int16_t>(skillLevel);
        status.skillSubLevel = static_cast<std::int16_t>(skillSubLevel);
        status.skillDuration = skillDuration;
        status.skillSelfCast = skillSelfCast;
    }

    // Shipping memcpy at 0x0048ab39 precedes CUser::WriteLock.
    user->m_abnormalStatus = statuses;
    std::optional<DBConn> connectionStorage;
    {
        L2_WRITE_GUARD(*user.operator->());

        auto& connection = connectionStorage.emplace(runtime::process::CurrentDatabaseId());
        if (statuses.front().skillId == 0) {
            constexpr const wchar_t* deleteStatement = L"{Call lin_DeleteNewActiveSkill (?)}";
            connection.Prepare(deleteStatement);
            connection.BindParameter(&characterId);
            connection.ExecutePrepared(deleteStatement);
        }

        constexpr const wchar_t* updateBatchStatement = L"{Call lin_UpdateNewActiveSkillBatch ("
                                                        L"?, ?, "
                                                        L"?, ?, ?, ?, ?, "
                                                        L"?, ?, ?, ?, ?, "
                                                        L"?, ?, ?, ?, ?, "
                                                        L"?, ?, ?, ?, ?, "
                                                        L"?, ?, ?, ?, ?, "
                                                        L"?, ?, ?, ?, ?, "
                                                        L"?, ?, ?, ?, ?, "
                                                        L"?, ?, ?, ?, ?)}";
        constexpr std::size_t statusesPerBatch = 8;
        constexpr std::size_t batchCount = abnormalStatusCount / statusesPerBatch;
        for (std::size_t batchIndex = 0; batchIndex < batchCount; ++batchIndex) {
            const std::size_t first = batchIndex * statusesPerBatch;
            if (statuses[first].skillId == 0)
                break;

            const int deleteExisting = batchIndex == 0 ? 1 : 0;
            connection.Prepare(updateBatchStatement);
            connection.BindParameter(&characterId);
            connection.BindParameter(&deleteExisting);
            for (std::size_t statusIndex = first; statusIndex < first + statusesPerBatch; ++statusIndex) {
                const AbnormalStatus& status = statuses[statusIndex];
                // Current helper 0x00483e9c binds the newly added sublevel last,
                // preserving the four-field donor ABI prefix.
                connection.BindParameter(&status.skillId);
                connection.BindParameter(&status.skillLevel);
                connection.BindParameter(&status.skillDuration);
                connection.BindParameter(&status.skillSelfCast);
                connection.BindParameter(&status.skillSubLevel);
            }
            connection.ExecutePrepared(updateBatchStatement);
            connection.ResetParam();
        }
    }
    return false;
}

bool RequestSaveAbnormalStatus(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00490cf0 is exactly this guarded delegation.
    L2_GUARD_FUNCTION();
    return CSocket::SaveAbnormalStatus(socket, packet);
}

bool RequestLoadAbnormalStatus(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0046c8a0. Paired current L2Server owners are
    // CDB::LoadInfectedSkills 0x00649b38 and ReplyLoadAbnormalStatus
    // 0x006c09f4.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int userSocketId = 0;
    ::Disassemble(packet, "dd", &characterId, &userSocketId);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1c3d, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1c3d, characterId)
        L2_LOG_ERROR(Log, "[skills] Cannot load active effects for character %d: character unavailable. Sending empty list to socket %d.",
            characterId, userSocketId);
        socket->Send("cdd", 0x58, userSocketId, 0);
        return false;
    }

    std::array<char, 0x5000> payload{};
    int payloadSize = 0;
    int assembledCount = 0;
    for (const AbnormalStatus& status : user->m_abnormalStatus) {
        if (status.skillId <= 0)
            continue;

        const int written = ::Assemble(
            payload.data() + payloadSize, 0x4f9c - payloadSize, "ddddd", status.skillId, static_cast<int>(status.skillLevel),
            static_cast<int>(status.skillSubLevel), status.skillDuration, status.skillSelfCast
        );
        if (written < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d] Buffer Overflow in Load abnormal status!!!. "
            //     "userdbid[%d], count[%d]",
            //     "CacheSocket.cpp", 0x1c59, characterId, assembledCount
            // )
            // L2_LOG_ERROR(Log,
            //     "[%s][%d] Buffer Overflow in Load abnormal status!!!. "
            //     "userdbid[%d], count[%d]",
            //     "CacheSocket.cpp", 0x1c59, characterId, assembledCount
            // )
            L2_LOG_ERROR(Log, "[skills] Cannot encode active effects for character %d after %d entries (%d bytes used). Sending empty list to socket %d.",
                characterId, assembledCount, payloadSize, userSocketId);
            socket->Send("cdd", 0x58, userSocketId, 0);
            return false;
        }
        payloadSize += written;
        ++assembledCount;
    }

    socket->Send("cddb", 0x58, userSocketId, assembledCount, payloadSize, payload.data());
    return false;
}

bool RequestCreateAllianceByNpc(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0046cd34. Paired current L2Server sender/reply
    // owners are 0x00649c14 and 0x006d3004.
    L2_GUARD_FUNCTION();

    std::uint32_t requestId = 0;
    std::uint32_t npcObjectId = 0;
    std::uint32_t requestContext = 0;
    std::array<char16_t, 50> allianceName{};
    std::uint32_t masterPledgeId = 0;
    ::Disassemble(packet, "dddSd", &requestId, &npcObjectId, &requestContext, 0x64, allianceName.data(), &masterPledgeId);

    const int worldId = runtime::process::CurrentDatabaseId();
    int allianceId = 0;
    CPledgeSP pledge = g_pledgeDb.GetPledge(static_cast<int>(masterPledgeId), false, worldId);
    if (pledge && pledge->allianceId() == 0U) {
        std::wstring databaseAllianceName = FixedUtf16ToWide(allianceName);
        allianceId = g_allianceDb.CreateAlliance(databaseAllianceName.data(), masterPledgeId, worldId);
        if (allianceId > 0) {
            std::array<char16_t, 24> pledgeName{};
            std::copy_n(pledge->GetName(), pledgeName.size(), pledgeName.begin());
            CUserSP ruler = g_userDb.GetUser(static_cast<int>(pledge->rullerId()), true, false, worldId);
            if (ruler) {
                WriteAllianceAudit(
                    0xf8, *ruler.get(), allianceName.data(), pledgeName.data(), static_cast<std::uint32_t>(allianceId),
                    masterPledgeId
                );
            }
        }
    }

    socket->Send("cddddS", 0x59, requestId, npcObjectId, requestContext, allianceId, allianceName.data());
    return false;
}

bool RequestJoinAlliance(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0046d124. Unlike the C1 donor, this target has no
    // sixth request field and performs no pre-join castle-siege SQL scans.
    L2_GUARD_FUNCTION();

    std::uint32_t requestId = 0;
    std::uint32_t npcObjectId = 0;
    std::uint32_t requestContext = 0;
    std::uint32_t allianceId = 0;
    std::uint32_t memberPledgeId = 0;
    ::Disassemble(packet, "ddddd", &requestId, &npcObjectId, &requestContext, &allianceId, &memberPledgeId);

    const bool joined = g_allianceDb.JoinAlliance(allianceId, memberPledgeId);
    socket->Send("cdddd", 0x5a, requestId, npcObjectId, requestContext, joined ? 0 : 2);

    if (joined) {
        const int worldId = runtime::process::CurrentDatabaseId();
        CPledgeSP pledge = g_pledgeDb.GetPledge(static_cast<int>(memberPledgeId), false, worldId);
        if (pledge) {
            std::array<char16_t, 24> pledgeName{};
            std::copy_n(pledge->GetName(), pledgeName.size(), pledgeName.begin());
            CUserSP ruler = g_userDb.GetUser(static_cast<int>(pledge->rullerId()), true, false, worldId);
            if (ruler) {
                CAllianceSP alliance = g_allianceDb.GetAlliance(allianceId, static_cast<std::uint32_t>(worldId));
                if (alliance) {
                    WriteAllianceAudit(0xf9, *ruler.get(), alliance->name(), pledgeName.data(), allianceId, memberPledgeId);
                }
            }
        }
    }
    return false;
}

bool RequestDismissAlliance(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0046d76c. The alliance name is kept before
    // CAllianceDb removes the object from its world/id map.
    L2_GUARD_FUNCTION();

    std::uint32_t requestId = 0;
    std::uint32_t npcObjectId = 0;
    std::uint32_t requestContext = 0;
    std::uint32_t dismissTime = 0;
    std::uint32_t allianceId = 0;
    std::uint32_t masterPledgeId = 0;
    ::Disassemble(packet, "dddddd", &requestId, &npcObjectId, &requestContext, &dismissTime, &allianceId, &masterPledgeId);

    const int worldId = runtime::process::CurrentDatabaseId();
    std::array<char16_t, 25> allianceName{};
    CAllianceSP alliance = g_allianceDb.GetAlliance(allianceId, static_cast<std::uint32_t>(worldId));
    if (alliance) {
        std::copy_n(alliance->name(), allianceName.size(), allianceName.begin());
    }

    const bool dismissed = g_allianceDb.DismissAlliance(allianceId, masterPledgeId, dismissTime);
    socket->Send("cddddd", 0x5b, requestId, npcObjectId, requestContext, dismissTime, dismissed ? 1 : 0);

    if (dismissed) {
        CPledgeSP pledge = g_pledgeDb.GetPledge(static_cast<int>(masterPledgeId), false, worldId);
        if (pledge) {
            std::array<char16_t, 24> pledgeName{};
            std::copy_n(pledge->GetName(), pledgeName.size(), pledgeName.begin());
            CUserSP ruler = g_userDb.GetUser(static_cast<int>(pledge->rullerId()), true, false, worldId);
            if (ruler) {
                WriteAllianceAudit(0xfa, *ruler.get(), allianceName.data(), pledgeName.data(), allianceId, masterPledgeId);
            }
        }
    }
    return false;
}

bool RequestOustAllianceMemberPledge(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0046db54.
    L2_GUARD_FUNCTION();

    std::uint32_t requestId = 0;
    std::uint32_t npcObjectId = 0;
    std::uint32_t requestContext = 0;
    std::uint32_t requestValue = 0;
    std::uint32_t oustTime = 0;
    std::uint32_t allianceId = 0;
    std::uint32_t memberPledgeId = 0;
    ::Disassemble(
        packet, "ddddddd", &requestId, &npcObjectId, &requestContext, &requestValue, &oustTime, &allianceId, &memberPledgeId
    );

    const bool ousted = g_allianceDb.OustMemberPledge(allianceId, memberPledgeId, oustTime);
    socket->Send("cdddddd", 0x5c, requestId, npcObjectId, requestContext, requestValue, oustTime, ousted ? 1 : 0);

    if (ousted) {
        const int worldId = runtime::process::CurrentDatabaseId();
        CPledgeSP pledge = g_pledgeDb.GetPledge(static_cast<int>(memberPledgeId), false, worldId);
        if (pledge) {
            std::array<char16_t, 24> pledgeName{};
            std::copy_n(pledge->GetName(), pledgeName.size(), pledgeName.begin());
            CUserSP ruler = g_userDb.GetUser(static_cast<int>(pledge->rullerId()), true, false, worldId);
            if (ruler) {
                CAllianceSP alliance = g_allianceDb.GetAlliance(allianceId, static_cast<std::uint32_t>(worldId));
                if (alliance) {
                    WriteAllianceAudit(0xfb, *ruler.get(), alliance->name(), pledgeName.data(), allianceId, memberPledgeId);
                }
            }
        }
    }
    return false;
}

bool RequestWithdrawAlliance(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0046e1bc.
    L2_GUARD_FUNCTION();

    std::uint32_t requestId = 0;
    std::uint32_t npcObjectId = 0;
    std::uint32_t requestContext = 0;
    std::uint32_t withdrawTime = 0;
    std::uint32_t allianceId = 0;
    std::uint32_t memberPledgeId = 0;
    ::Disassemble(packet, "dddddd", &requestId, &npcObjectId, &requestContext, &withdrawTime, &allianceId, &memberPledgeId);

    const bool withdrawn = g_allianceDb.Withdraw(allianceId, memberPledgeId, withdrawTime);
    socket->Send("cddddd", 0x5d, requestId, npcObjectId, requestContext, withdrawTime, withdrawn ? 1 : 0);

    if (withdrawn) {
        const int worldId = runtime::process::CurrentDatabaseId();
        CPledgeSP pledge = g_pledgeDb.GetPledge(static_cast<int>(memberPledgeId), false, worldId);
        if (pledge) {
            std::array<char16_t, 24> pledgeName{};
            std::copy_n(pledge->GetName(), pledgeName.size(), pledgeName.begin());
            CUserSP ruler = g_userDb.GetUser(static_cast<int>(pledge->rullerId()), true, false, worldId);
            if (ruler) {
                CAllianceSP alliance = g_allianceDb.GetAlliance(allianceId, static_cast<std::uint32_t>(worldId));
                if (alliance) {
                    WriteAllianceAudit(0xfc, *ruler.get(), alliance->name(), pledgeName.data(), allianceId, memberPledgeId);
                }
            }
        }
    }
    return false;
}

bool RequestCreatePet(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0046e814.
    L2_GUARD_FUNCTION();

    // Present in the shipping frame even though CWareHouse owns both SQL
    // writes performed by this request.
    DBConn connection(runtime::process::CurrentDatabaseId());

    int requestId = 0;
    int characterId = 0;
    int collarType = 0;
    int petNpcClass = 0;
    std::uint64_t experienceBits = 0;
    int enchant = 0;
    double hp = 0.0;
    double mp = 0.0;
    int meal = 0;
    ::Disassemble(
        packet, "ddddQdffd", &requestId, &characterId, &collarType, &petNpcClass, &experienceBits, &enchant, &hp, &mp, &meal
    );
    const std::int64_t experience = runtime::Int64FromBits(experienceBits);

    auto sendFailure = [&]() {
        if (requestId < 0)
            return;
        socket->Send("cdddd", 0x5e, requestId, 0, 0, 0);
    };

    const int worldId = runtime::process::CurrentDatabaseId();
    CUserSP user = g_userDb.GetUser(characterId, true, false, worldId);
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1de5, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1de5, characterId)
        L2_LOG_ERROR(Log, "[pets] Create request %d for character %d failed: character unavailable. %s", requestId, characterId,
            requestId >= 0 ? "Sending failure reply." : "No reply for negative request ID.");
        sendFailure();
        return false;
    }

    CWareHouseSP inventory = user->GetWareHouse(0, true);
    if (!inventory) {
        // Log.Add(LOG_ERROR, "[%s][%d] invalid warehouse, id[%d]", "CacheSocket.cpp", 0x1df1, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d] invalid warehouse, id[%d]", "CacheSocket.cpp", 0x1df1, characterId)
        L2_LOG_ERROR(Log, "[pets] Create request %d for character %d failed: inventory unavailable. %s", requestId, characterId,
            requestId >= 0 ? "Sending failure reply." : "No reply for negative request ID.");
        sendFailure();
        return false;
    }

    int itemId;
    bool created;
    {
        L2_WRITE_GUARD(*inventory.operator->());
        itemId = 0;
        created = inventory->CreatePet(collarType, petNpcClass, experience, enchant, hp, mp, meal, itemId, 0);
    }
    if (!created) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] warehouse->CreatePet Failed , id[%d]", "CacheSocket.cpp", 0x1dfe, characterId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] warehouse->CreatePet Failed , id[%d]", "CacheSocket.cpp", 0x1dfe, characterId
        // )
        L2_LOG_ERROR(Log, "[pets] Create request %d failed for character %d (collar type %d, NPC class %d). %s",
            requestId, characterId, collarType, petNpcClass,
            requestId >= 0 ? "Sending failure reply." : "No reply for negative request ID.");
        sendFailure();
        return false;
    }

    if (requestId >= 0) {
        socket->Send("cddddd", 0x5e, requestId, 1, collarType, enchant, itemId);
    }
    WriteCreatePetAudit(*user.get(), collarType, petNpcClass, itemId, experience, enchant);
    return false;
}

bool RequestDeletePet(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0046f084.
    L2_GUARD_FUNCTION();

    int requestId = 0;
    int characterId = 0;
    int collarItemId = 0;
    int deleteReason = 0;
    int requestCollarEnchant = 0;
    ::Disassemble(packet, "ddddd", &requestId, &characterId, &collarItemId, &deleteReason, &requestCollarEnchant);

    auto sendFailure = [&]() { socket->Send("cddd", 0x5f, requestId, 0, 0); };

    const int worldId = runtime::process::CurrentDatabaseId();
    CUserSP user = g_userDb.GetUser(characterId, true, false, worldId);
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1e29, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1e29, characterId)
        L2_LOG_ERROR(Log, "[pets] Delete request %d for pet %d failed: character %d unavailable. Sending failure reply.", requestId, collarItemId, characterId);
        sendFailure();
        return false;
    }

    CWareHouseSP inventory = user->GetWareHouse(0, true);
    if (!inventory) {
        // Log.Add(LOG_ERROR, "[%s][%d] invalid warehouse, id[%d]", "CacheSocket.cpp", 0x1e30, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d] invalid warehouse, id[%d]", "CacheSocket.cpp", 0x1e30, characterId)
        L2_LOG_ERROR(Log, "[pets] Delete request %d for pet %d failed: character %d inventory unavailable. Sending failure reply.", requestId, collarItemId, characterId);
        sendFailure();
        return false;
    }

    std::optional<CItemSP> collarStorage;
    std::optional<std::array<char16_t, 256>> shapeShiftStorage;
    std::optional<std::array<char16_t, 256>> baseAttributeTextStorage;
    std::optional<std::array<char16_t, 256>> ensoulTextStorage;
    std::optional<std::array<char16_t, 256>> variationTextStorage;
    std::optional<BaseAttribute> baseAttributeStorage;
    int actualEnchant;
    int actualItemType;
    int actualItemId;
    const int deleteResult = [&]() {
        L2_WRITE_GUARD(*inventory.operator->());
        auto& collar = collarStorage.emplace(inventory->GetItem(collarItemId));
        if (!collar) {
            return 1;
        }

        auto& shapeShift = shapeShiftStorage.emplace();
        auto& baseAttributeText = baseAttributeTextStorage.emplace();
        auto& ensoulText = ensoulTextStorage.emplace();
        auto& variationText = variationTextStorage.emplace();
        collar->MakeShapeShiftLogInfo(shapeShift.data());
        auto& baseAttribute = baseAttributeStorage.emplace(collar->Attribute());
        baseAttribute.GetLogString(baseAttributeText.data());
        // collar->MakeEnsoulLogInfo(ensoulText.data())
        collar->MakeEnsoulLogInfo(ensoulText.data(), ensoulText.size());
        FormatUtf16To(
            variationText.data(), variationText.size(), u"%d:%d:%d", collar->m_intensiveItemType,
            collar->OptionKey().variationOption1, collar->OptionKey().variationOption2
        );

        actualEnchant = collar->Enchant();
        actualItemType = static_cast<int>(collar->ItemType());
        actualItemId = static_cast<int>(collar->ItemID());
        inventory->PopItem(actualItemId);
        if (!collar->Delete(true, false)) {
            return 2;
        }
        return 0;
    }();
    if (deleteResult == 1) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] invalid pet collar dbid(%d), id[%d]", "CacheSocket.cpp", 0x1e39, collarItemId,
        //     characterId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] invalid pet collar dbid(%d), id[%d]", "CacheSocket.cpp", 0x1e39, collarItemId,
        //     characterId
        // )
        L2_LOG_ERROR(Log, "[pets] Delete request %d failed: collar %d missing from character %d inventory. Sending failure reply.", requestId, collarItemId, characterId);
        sendFailure();
        return false;
    }
    if (deleteResult == 2) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] RequestDeletePet Failed (delete pet collar failed) "
        //     "UserDBID(%d) PetCollarDBID(%d)",
        //     "CacheSocket.cpp", 0x1e5a, characterId, collarItemId
        // )
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] RequestDeletePet Failed (delete pet collar failed) "
        //     "UserDBID(%d) PetCollarDBID(%d)",
        //     "CacheSocket.cpp", 0x1e5a, characterId, collarItemId
        // )
        L2_LOG_ERROR(Log, "[pets] Delete request %d failed while deleting collar %d for character %d. Inventory removal was already called. Sending failure reply.", requestId, collarItemId, characterId);
        sendFailure();
        return false;
    }

    CPetSP pet = g_petDb.GetPet(collarItemId, worldId);
    if (!pet) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] RequestDeletePet failed. Cannot find pet, id[%d]", "CacheSocket.cpp", 0x1e62,
        //     collarItemId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] RequestDeletePet failed. Cannot find pet, id[%d]", "CacheSocket.cpp", 0x1e62,
        //     collarItemId
        // )
        L2_LOG_ERROR(Log, "[pets] Delete request %d cannot continue: pet %d unavailable after collar deletion (character %d). No reply is sent.", requestId, collarItemId, characterId);
        // Shipping exits this one post-collar failure without a response.
        return false;
    }
    if (!g_petDb.DeletePet(collarItemId, true, worldId)) {
        sendFailure();
        return false;
    }

    WriteDeletePetAudits(
        *user.get(), collarItemId, actualItemId, actualItemType, actualEnchant, deleteReason, requestCollarEnchant,
        shapeShiftStorage->data(), baseAttributeTextStorage->data(), variationTextStorage->data(), ensoulTextStorage->data()
    );
    socket->Send("cddd", 0x5f, requestId, 1, collarItemId);
    return false;
}

bool RequestEvolvePet(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0046fe48.
    L2_GUARD_FUNCTION();

    unsigned int requestId = 0;
    int characterId = 0;
    const std::uint8_t* evolvePacket = ::Disassemble(packet, "dd", &requestId, &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1eaa, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x1eaa, characterId)
        L2_LOG_ERROR(Log, "[pets] Evolution request %u for character %d failed: character unavailable. Sending failure reply.", requestId, characterId);
        socket->Send("cdddddd", 0x60, requestId, 0, 0, 0, 0, 0);
        return false;
    }

    user->OnRequestEvolvePet(socket, evolvePacket, requestId);
    return false;
}

bool RequestWithdrawPet(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0047ddf8.
    L2_GUARD_FUNCTION();

    int userSocketId = 0;
    int characterId = 0;
    int collarServerId = 0;
    int petId = 0;
    int requestedNpcClass = 0;
    int responseContext = 0;
    int reloadPet = 0;
    ::Disassemble(
        packet, "ddddddd", &userSocketId, &characterId, &collarServerId, &petId, &requestedNpcClass, &responseContext,
        &reloadPet
    );

    auto sendFailure = [&]() {
        socket->Send("cddddQ", 0x61, 0, userSocketId, collarServerId, 0, static_cast<long long>(0));
    };

    const int worldId = runtime::process::CurrentDatabaseId();
    CUserSP user = g_userDb.GetUser(characterId, true, false, worldId);
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] RequestWithdrawPet failed. invalid user[%d]. "
        //     "pet collar[%d]",
        //     "CacheSocket.cpp", 0x1ec4, characterId, petId
        // )
        // L2_LOG_ERROR(Log,
            // "[%s][%d] RequestWithdrawPet failed. invalid user[%d]. "
            // "pet collar[%d]",
            // "CacheSocket.cpp", 0x1ec4, characterId, petId
        // )
        L2_LOG_ERROR(Log, "[pets] Withdrawal of pet %d for character %d failed: character unavailable. Sending failure reply.", petId, characterId);
        sendFailure();
        return false;
    }

    CWareHouseSP inventory = user->GetWareHouse(0, true);
    if (!inventory) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] RequestWithdrawPet failed. invalid warehouse, "
        //     "user[%d], pet collar[%d]",
        //     "CacheSocket.cpp", 0x1ecc, characterId, petId
        // )
        // L2_LOG_ERROR(Log,
            // "[%s][%d] RequestWithdrawPet failed. invalid warehouse, "
            // "user[%d], pet collar[%d]",
            // "CacheSocket.cpp", 0x1ecc, characterId, petId
        // )
        L2_LOG_ERROR(Log, "[pets] Withdrawal of pet %d for character %d failed: inventory unavailable. Sending failure reply.", petId, characterId);
        sendFailure();
        return false;
    }

    std::optional<CItemSP> collarStorage;
    std::optional<CPetSP> petStorage;
    std::optional<CPetWarehouseSP> petWarehouseStorage;
    const int lookupResult = [&]() {
        L2_WRITE_GUARD(*inventory.operator->());
        auto& collar = collarStorage.emplace(inventory->GetItem(petId));
        if (!collar) {
            return 1;
        }

        auto& pet = petStorage.emplace(
            reloadPet == 0 ? g_petDb.GetPet(petId, worldId) : g_petDb.ReloadPet(petId, worldId));
        if (!pet) {
            return 2;
        }

        pet->m_ownerId = static_cast<std::uint32_t>(characterId);
        auto& petWarehouse = petWarehouseStorage.emplace(pet->GetWarehouse());
        petWarehouse->SetOwnerId(static_cast<std::uint32_t>(characterId));
        collar->SetUsable(false);
        pet->m_isSpawned = true;
        return 0;
    }();
    if (lookupResult == 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] RequestWithdrawPet failed. user[%d]. "
        //     "invalid pet collar[%d]",
        //     "CacheSocket.cpp", 0x1ed7, characterId, petId
        // )
        // L2_LOG_ERROR(Log,
            // "[%s][%d] RequestWithdrawPet failed. user[%d]. "
            // "invalid pet collar[%d]",
            // "CacheSocket.cpp", 0x1ed7, characterId, petId
        // )
        L2_LOG_ERROR(Log, "[pets] Withdrawal of pet %d for character %d failed: collar missing from inventory. Sending failure reply.", petId, characterId);
        sendFailure();
        return false;
    }
    if (lookupResult == 2) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] RequestWithdrawPet failed. user[%d] "
        //     "invalid pet [%d]",
        //     "CacheSocket.cpp", 0x1eeb, characterId, petId
        // )
        // L2_LOG_ERROR(Log,
            // "[%s][%d] RequestWithdrawPet failed. user[%d] "
            // "invalid pet [%d]",
            // "CacheSocket.cpp", 0x1eeb, characterId, petId
        // )
        L2_LOG_ERROR(Log, "[pets] Withdrawal of pet %d for character %d failed: pet record unavailable. Sending failure reply.", petId, characterId);
        sendFailure();
        return false;
    }
    auto& pet = *petStorage;

    const int alive = pet->m_wasDead == 0 ? 1 : 0;
    socket->Send(
        "cddddQQffdSddddQdd", 0x61, 1, userSocketId, collarServerId, static_cast<int>(pet->m_npcClassId),
        static_cast<long long>(pet->m_exp), static_cast<long long>(pet->m_spellPoint), static_cast<double>(pet->m_hp),
        static_cast<double>(pet->m_mp), pet->m_meal, pet->m_nickName.data(), pet->m_slot1, pet->m_slot2, pet->m_slot3,
        responseContext, static_cast<long long>(pet->m_downExp), alive, pet->m_deathCount
    );

    WriteWithdrawPetAudit(
        *user.get(), PetNameField(pet->m_nickName), petId, static_cast<int>(pet->m_npcClassId), pet->m_exp,
        requestedNpcClass, pet->m_meal, alive
    );
    return false;
}

bool RequestLoadPetItems(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0047e974.
    L2_GUARD_FUNCTION();

    int userSocketId = 0;
    int petId = 0;
    int ownerId = 0;
    int contextId = 0;
    int contextValue = 0;
    ::Disassemble(packet, "ddddd", &userSocketId, &petId, &ownerId, &contextId, &contextValue);

    CPetSP pet = g_petDb.GetPet(petId, runtime::process::CurrentDatabaseId());
    if (!pet) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] RequestLoadPetItems failed. invalid pet [%d]", "CacheSocket.cpp", 0x1f2d, petId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] RequestLoadPetItems failed. invalid pet [%d]", "CacheSocket.cpp", 0x1f2d, petId
        // )
        L2_LOG_ERROR(Log, "[pets] Cannot load items for pet %d, owner %d: pet unavailable. Sending failure reply.", petId, ownerId);
        // The target source passes only status and userSocketId to a five-d
        // format here, leaking three caller-stack slots. A portable build
        // cannot keep undefined host stack bytes safely. Zero is the
        // deterministic receiver-compatible value for those invalid fields.
        socket->Send("cddddd", 0x62, 0, userSocketId, 0, 0, 0);
        return false;
    }

    pet->LoadItems(ownerId);
    CPetWarehouseSP warehouse = pet->GetWarehouse();
    {
        L2_WRITE_GUARD(*warehouse.operator->());
        warehouse->SendPetItemList(socket, userSocketId, contextId, contextValue);
    }
    return false;
}

bool RequestDepositPet(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0047ec60.
    L2_GUARD_FUNCTION();

    int requestId = 0;
    int characterId = 0;
    int requestContext = 0;
    int petId = 0;
    int requestValue = 0;
    char requestByte = 0;
    ::Disassemble(packet, "dddddc", &requestId, &characterId, &requestContext, &petId, &requestValue, &requestByte);

    auto sendFailure = [&]() { socket->Send("cd", 0x63, 0); };

    const int worldId = runtime::process::CurrentDatabaseId();
    CUserSP user = g_userDb.GetUser(characterId, true, false, worldId);
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] RequestWithdrawPet failed. invalid user[%d]. "
        //     "pet collar[%d]",
        //     "CacheSocket.cpp", 0x1f4b, characterId, petId
        // )
        // L2_LOG_ERROR(Log,
            // "[%s][%d] RequestWithdrawPet failed. invalid user[%d]. "
            // "pet collar[%d]",
            // "CacheSocket.cpp", 0x1f4b, characterId, petId
        // )
        L2_LOG_ERROR(Log, "[pets] Deposit of pet %d for character %d failed: character unavailable. Sending failure reply.", petId, characterId);
        sendFailure();
        return false;
    }

    CWareHouseSP inventory = user->GetWareHouse(0, true);
    if (!inventory) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] RequestWithdrawPet failed. invalid warehouse, "
        //     "user[%d], pet collar[%d]",
        //     "CacheSocket.cpp", 0x1f54, characterId, petId
        // )
        // L2_LOG_ERROR(Log,
            // "[%s][%d] RequestWithdrawPet failed. invalid warehouse, "
            // "user[%d], pet collar[%d]",
            // "CacheSocket.cpp", 0x1f54, characterId, petId
        // )
        L2_LOG_ERROR(Log, "[pets] Deposit of pet %d for character %d failed: inventory unavailable. Sending failure reply.", petId, characterId);
        sendFailure();
        return false;
    }

    std::optional<CItemSP> collarStorage;
    std::optional<CPetSP> petStorage;
    const int lookupResult = [&]() {
        L2_WRITE_GUARD(*inventory.operator->());
        auto& collar = collarStorage.emplace(inventory->GetItem(petId));
        if (!collar) {
            return 1;
        }

        auto& pet = petStorage.emplace(g_petDb.GetPet(petId, worldId));
        if (!pet) {
            return 2;
        }

        pet->m_ownerId = 0;
        collar->SetUsable(true);
        pet->m_isSpawned = false;
        return 0;
    }();
    if (lookupResult == 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] RequestWithdrawPet failed. user[%d]. "
        //     "invalid pet collar[%d]",
        //     "CacheSocket.cpp", 0x1f5f, characterId, petId
        // )
        // L2_LOG_ERROR(Log,
            // "[%s][%d] RequestWithdrawPet failed. user[%d]. "
            // "invalid pet collar[%d]",
            // "CacheSocket.cpp", 0x1f5f, characterId, petId
        // )
        L2_LOG_ERROR(Log, "[pets] Deposit of pet %d for character %d failed: collar missing from inventory. Sending failure reply.", petId, characterId);
        sendFailure();
        return false;
    }
    if (lookupResult == 2) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] RequestWithdrawPet failed. user[%d] "
        //     "invalid pet [%d]",
        //     "CacheSocket.cpp", 0x1f6a, characterId, petId
        // )
        // L2_LOG_ERROR(Log,
            // "[%s][%d] RequestWithdrawPet failed. user[%d] "
            // "invalid pet [%d]",
            // "CacheSocket.cpp", 0x1f6a, characterId, petId
        // )
        L2_LOG_ERROR(Log, "[pets] Deposit of pet %d for character %d failed: pet record unavailable. Sending failure reply.", petId, characterId);
        sendFailure();
        return false;
    }
    auto& pet = *petStorage;

    CPetWarehouseSP petWarehouse = pet->GetWarehouse();
    if (petWarehouse) {
        {
            L2_WRITE_GUARD(*petWarehouse.operator->());
            petWarehouse->ClearItemWhenDespawn();
        }
    }

    socket->Send(
        "cddddddc", 0x63, 1, requestId, characterId, requestContext, petId, requestValue,
        static_cast<int>(static_cast<unsigned char>(requestByte))
    );
    return false;
}

bool RequestSavePet(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00470008.
    L2_GUARD_FUNCTION();

    int mode = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "d", &mode);
    const int worldId = runtime::process::CurrentDatabaseId();

    if (mode == 0) {
        int requestId = 0;
        int characterId = 0;
        int petId = 0;
        std::uint64_t experienceBits = 0;
        int enchant = 0;
        double hp = 0.0;
        double mp = 0.0;
        std::uint64_t spellPointBits = 0;
        int meal = 0;
        int slot1 = 0;
        int slot2 = 0;
        int slot3 = 0;
        std::uint64_t downExperienceBits = 0;
        int alive = 0;
        int deathCount = 0;
        int responseContext = 0;
        ::Disassemble(
            cursor, "dddQdffQddddQddd", &requestId, &characterId, &petId, &experienceBits, &enchant, &hp, &mp,
            &spellPointBits, &meal, &slot1, &slot2, &slot3, &downExperienceBits, &alive, &deathCount, &responseContext
        );

        auto sendResult = [&](int status) { socket->Send("chdddd", 0xc6, 0xea, mode, status, requestId, responseContext); };

        CPetSP pet = g_petDb.GetPet(petId, worldId);
        if (!pet) {
            sendResult(0);
            // Log.Add(
            //     LOG_ERROR, "[%s][%d] RequestSavePet failed. invalid pet[%d]", "CacheSocket.cpp", 0x1fb2, petId
            // )
            // L2_LOG_ERROR(Log, "[%s][%d] RequestSavePet failed. invalid pet[%d]", "CacheSocket.cpp", 0x1fb2, petId
            // )
            L2_LOG_ERROR(Log, "[pets] Save request %d failed: pet %d unavailable (character %d). Reply status 0.", requestId, petId, characterId);
            return false;
        }

        pet->m_exp = runtime::Int64FromBits(experienceBits);
        pet->m_hp = runtime::Win64CvttDoubleToInt32(hp);
        pet->m_mp = runtime::Win64CvttDoubleToInt32(mp);
        pet->m_spellPoint = runtime::Int64FromBits(spellPointBits);
        pet->m_meal = meal;
        pet->m_slot1 = slot1;
        pet->m_slot2 = slot2;
        pet->m_slot3 = slot3;
        pet->m_downExp = runtime::Int64FromBits(downExperienceBits);
        pet->m_wasDead = alive == 0 ? 1U : 0U;
        pet->m_deathCount = deathCount;
        pet->Save();

        CUserSP user = g_userDb.GetUser(characterId, true, false, worldId);
        if (!user) {
            sendResult(0);
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d] RequestSavePet failed. invalid user[%d]. "
            //     "pet collar[%d]",
            //     "CacheSocket.cpp", 0x1fc8, characterId, petId
            // )
            // L2_LOG_ERROR(Log,
                // "[%s][%d] RequestSavePet failed. invalid user[%d]. "
                // "pet collar[%d]",
                // "CacheSocket.cpp", 0x1fc8, characterId, petId
            // )
            L2_LOG_ERROR(Log, "[pets] Save request %d cannot update collar %d: character %d unavailable. Pet save was already called; reply status 0.", requestId, petId, characterId);
            return false;
        }

        CWareHouseSP inventory = user->GetWareHouse(0, true);
        if (!inventory) {
            sendResult(0);
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d] RequestSavePet failed. invalid warehouse, "
            //     "user[%d], pet collar[%d]",
            //     "CacheSocket.cpp", 0x1fd2, characterId, petId
            // )
            // L2_LOG_ERROR(Log,
                // "[%s][%d] RequestSavePet failed. invalid warehouse, "
                // "user[%d], pet collar[%d]",
                // "CacheSocket.cpp", 0x1fd2, characterId, petId
            // )
            L2_LOG_ERROR(Log, "[pets] Save request %d cannot update collar %d: character %d inventory unavailable. Pet save was already called; reply status 0.", requestId, petId, characterId);
            return false;
        }

        std::optional<CItemSP> collarStorage;
        const bool collarSaved = [&]() {
            L2_WRITE_GUARD(*inventory.operator->());
            auto& collar = collarStorage.emplace(inventory->GetItem(petId));
            if (!collar) {
                return false;
            }

            collar->SetEnchant(enchant);
            collar->Save(true);
            return true;
        }();
        if (!collarSaved) {
            sendResult(0);
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d] RequestSavePet failed. user[%d]. "
            //     "invalid pet collar[%d]",
            //     "CacheSocket.cpp", 0x1fdd, characterId, petId
            // )
            // L2_LOG_ERROR(Log,
                // "[%s][%d] RequestSavePet failed. user[%d]. "
                // "invalid pet collar[%d]",
                // "CacheSocket.cpp", 0x1fdd, characterId, petId
            // )
            L2_LOG_ERROR(Log, "[pets] Save request %d cannot update collar %d: item missing from character %d inventory. Pet save was already called; reply status 0.", requestId, petId, characterId);
            return false;
        }
        sendResult(1);
        return false;
    }

    if (mode == 1) {
        int petId = 0;
        int meal = 0;
        ::Disassemble(cursor, "dd", &petId, &meal);
        CPetSP pet = g_petDb.GetPet(petId, worldId);
        if (!pet) {
            // Log.Add(
            //     LOG_ERROR, "[%s][%d] RequestSavePet failed. invalid pet[%d]", "CacheSocket.cpp", 0x1fef, petId
            // )
            // L2_LOG_ERROR(Log, "[%s][%d] RequestSavePet failed. invalid pet[%d]", "CacheSocket.cpp", 0x1fef, petId
            // )
            L2_LOG_ERROR(Log, "[pets] Cannot save meal value %d for pet %d: pet unavailable. No reply is sent.", meal, petId);
            return false;
        }
        pet->m_meal = meal;
        pet->Save();
        return false;
    }

    if (mode == 2) {
        int characterId = 0;
        int petId = 0;
        ::Disassemble(cursor, "dd", &characterId, &petId);
        CUserSP user = g_userDb.GetUser(characterId, true, false, worldId);
        if (!user) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d] RequestSavePet failed. invalid user[%d]. "
            //     "pet collar[%d]",
            //     "CacheSocket.cpp", 0x1fff, characterId, petId
            // )
            // L2_LOG_ERROR(Log,
                // "[%s][%d] RequestSavePet failed. invalid user[%d]. "
                // "pet collar[%d]",
                // "CacheSocket.cpp", 0x1fff, characterId, petId
            // )
            L2_LOG_ERROR(Log, "[pets] Cannot mark collar %d as eroded: character %d unavailable. No reply is sent.", petId, characterId);
            return false;
        }

        CWareHouseSP inventory = user->GetWareHouse(0, true);
        if (!inventory) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d] RequestSavePet failed. invalid warehouse, "
            //     "user[%d], pet collar[%d]",
            //     "CacheSocket.cpp", 0x2008, characterId, petId
            // )
            // L2_LOG_ERROR(Log,
                // "[%s][%d] RequestSavePet failed. invalid warehouse, "
                // "user[%d], pet collar[%d]",
                // "CacheSocket.cpp", 0x2008, characterId, petId
            // )
            L2_LOG_ERROR(Log, "[pets] Cannot mark collar %d as eroded: character %d inventory unavailable. No reply is sent.", petId, characterId);
            return false;
        }

        std::optional<CItemSP> collarStorage;
        const bool collarSaved = [&]() {
            L2_WRITE_GUARD(*inventory.operator->());
            auto& collar = collarStorage.emplace(inventory->GetItem(petId));
            if (!collar) {
                return false;
            }
            collar->m_eroded = 1;
            collar->Save(true);
            return true;
        }();
        if (!collarSaved) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d] RequestSavePet failed. user[%d]. "
            //     "invalid pet collar[%d]",
            //     "CacheSocket.cpp", 0x2012, characterId, petId
            // )
            // L2_LOG_ERROR(Log,
                // "[%s][%d] RequestSavePet failed. user[%d]. "
                // "invalid pet collar[%d]",
                // "CacheSocket.cpp", 0x2012, characterId, petId
            // )
            L2_LOG_ERROR(Log, "[pets] Cannot mark collar %d as eroded: item missing from character %d inventory. No reply is sent.", petId, characterId);
            return false;
        }
    }
    return false;
}

bool RequestChangePetName(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00470df4.
    L2_GUARD_FUNCTION();

    int requestId = 0;
    int petId = 0;
    std::array<char16_t, 25> targetNewName{};
    int characterId = 0;
    ::Disassemble(packet, "ddSd", &requestId, &petId, 0x32, targetNewName.data(), &characterId);

    const int worldId = runtime::process::CurrentDatabaseId();
    CPetSP pet = g_petDb.GetPet(petId, worldId);
    if (!pet) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] RequestSavePet failed. invalid pet[%d]", "CacheSocket.cpp", 0x2032, petId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] RequestSavePet failed. invalid pet[%d]", "CacheSocket.cpp", 0x2032, petId
        // )
        L2_LOG_ERROR(Log, "[pets] Rename request %d failed: pet %d unavailable (character %d). No reply is sent.", requestId, petId, characterId);
        return false;
    }

    const std::wstring newName = FixedUtf16ToWide(targetNewName);
    int setNameResult = 0;
    if (!pet->SetName(newName.c_str(), setNameResult)) {
        socket->Send("cddd", 0x68, 0, requestId, setNameResult);
        return false;
    }

    socket->Send("cddS", 0x68, 1, requestId, targetNewName.data());
    if (characterId > 0) {
        CUserSP user = g_userDb.GetUser(characterId, true, false, worldId);
        if (user) {
            WriteChangePetNameAudit(*user.get(), newName.c_str(), petId, static_cast<int>(pet->m_npcClassId), pet->m_exp);
        }
    }
    return false;
}

bool RequestDismissPet(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x00490d98.
    L2_GUARD_FUNCTION();

    std::uint32_t characterId = 0;
    std::uint32_t petId = 0;
    ::Disassemble(packet, "dd", &characterId, &petId);

    const int worldId = runtime::process::CurrentDatabaseId();
    CUserSP user = g_userDb.GetUser(static_cast<int>(characterId), true, false, worldId);
    if (!user) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] RequestDismissPet failed. Cannot find user, id[%d]", "CacheSocket.cpp", 0x2063,
        //     static_cast<int>(characterId)
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] RequestDismissPet failed. Cannot find user, id[%d]", "CacheSocket.cpp", 0x2063,
        //     static_cast<int>(characterId)
        // )
        L2_LOG_ERROR(Log, "[pets] Cannot dismiss pet %u for character %u: character unavailable. Request skipped.",
            static_cast<unsigned int>(petId), static_cast<unsigned int>(characterId));
        return false;
    }

    CPetSP pet = g_petDb.GetPet(static_cast<int>(petId), worldId);
    if (!pet) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] RequestDismissPet failed. Cannot find pet, id[%d]", "CacheSocket.cpp", 0x206a,
        //     static_cast<int>(petId)
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] RequestDismissPet failed. Cannot find pet, id[%d]", "CacheSocket.cpp", 0x206a,
        //     static_cast<int>(petId)
        // )
        L2_LOG_ERROR(Log, "[pets] Cannot dismiss pet %u for character %u: pet unavailable. Request skipped.",
            static_cast<unsigned int>(petId), static_cast<unsigned int>(characterId));
        return false;
    }

    const int petLevel = pet->Level();
    DBConn connection(worldId);
    constexpr const wchar_t* statement = L"{Call lin_DeletePet (?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&petId);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "[%s][%d] RequestDismissPet failed", "CacheSocket.cpp", 0x2072)
        // L2_LOG_ERROR(Log, "[%s][%d] RequestDismissPet failed", "CacheSocket.cpp", 0x2072)
        L2_LOG_ERROR(Log, "[pets] Dismissal failed in the database for pet %u, character %u. No reply is sent.",
            static_cast<unsigned int>(petId), static_cast<unsigned int>(characterId));
        return false;
    }

    WriteDismissPetAudit(*user.get(), static_cast<int>(petId), petLevel);
    return false;
}

bool RequestPetGetItem(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0047f3b8.
    L2_GUARD_FUNCTION();

    int petId = 0;
    int responseContext = 0;
    int responseContext2 = 0;
    ::Disassemble(packet, "ddd", &petId, &responseContext, &responseContext2);

    CPetSP pet = g_petDb.GetPet(petId, runtime::process::CurrentDatabaseId());
    if (!pet) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] RequestPetGetItem failed. Cannot find pet, id[%d]", "CacheSocket.cpp", 0x208e,
        //     petId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] RequestPetGetItem failed. Cannot find pet, id[%d]", "CacheSocket.cpp", 0x208e,
        //     petId
        // )
        L2_LOG_ERROR(Log, "[pets] Cannot process item acquisition for pet %d: pet unavailable (request contexts %d, %d). No reply is sent.",
            petId, responseContext, responseContext2);
        return false;
    }

    CPetWarehouseSP warehouse = pet->GetWarehouse();
    {
        L2_WRITE_GUARD(*warehouse.operator->());
        if (!warehouse->PetGetItem(socket, packet)) {
            socket->Send("cddd", 0x66, 0, responseContext2, responseContext);
        }
    }
    return false;
}

bool RequestPetDropItem(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0047f654.
    L2_GUARD_FUNCTION();

    int userSocketId = 0;
    int petId = 0;
    int l2ServerItemId = 0;
    int cachedItemId = 0;
    int dropContext = 0;
    ::Disassemble(packet, "ddddd", &userSocketId, &petId, &l2ServerItemId, &cachedItemId, &dropContext);

    CPetSP pet = g_petDb.GetPet(petId, runtime::process::CurrentDatabaseId());
    if (!pet) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] RequestPetDropItem failed. Cannot find pet, id[%d]", "CacheSocket.cpp", 0x20ed,
        //     petId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] RequestPetDropItem failed. Cannot find pet, id[%d]", "CacheSocket.cpp", 0x20ed,
        //     petId
        // )
        L2_LOG_ERROR(Log, "[pets] Cannot drop item DB ID %d for pet %d: pet unavailable. Sending failure reply for server item %d.",
            cachedItemId, petId, l2ServerItemId);
        socket->Send("cddd", 0x67, 0, userSocketId, l2ServerItemId);
        return false;
    }

    CPetWarehouseSP warehouse = pet->GetWarehouse();
    std::optional<CItemSP> itemToDropStorage;
    bool dropped;
    {
        L2_WRITE_GUARD(*warehouse.operator->());
        auto& itemToDrop = itemToDropStorage.emplace(warehouse->GetItem(cachedItemId));
        dropped = itemToDrop && warehouse->PetDropItem(itemToDrop);
    }
    auto& itemToDrop = *itemToDropStorage;
    if (dropped) {
        socket->Send(
            "cdddQd", 0x67, 1, userSocketId, l2ServerItemId, static_cast<long long>(itemToDrop->Amount()), dropContext
        );
        return false;
    }

    socket->Send("cddd", 0x67, 0, userSocketId, l2ServerItemId);
    return false;
}

bool RequestGiveItemToPet(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0047facc.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    int characterId = 0;
    int petServerId = 0;
    int petId = 0;
    int itemId = 0;
    std::int64_t count = 0;
    int consumeTypeValue = 0;
    ::Disassemble(packet, "dddddQd", &userServerId, &characterId, &petServerId, &petId, &itemId, &count, &consumeTypeValue);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     "RequestGiveItemToPet failed. Cannot find user. "
        //     "user[%d], pet collar[%d]",
        //     characterId, petId
        // )
        // L2_LOG_ERROR(Log,
        //     "RequestGiveItemToPet failed. Cannot find user. "
        //     "user[%d], pet collar[%d]",
        //     characterId, petId
        // )
        L2_LOG_ERROR(Log, "[pets] Cannot give item %d to pet %d: character %d unavailable. Sending failure reply.", itemId, petId, characterId);
        // The first three shipping failure sites spell this as "cddb" but
        // supply only the blob length and no pointer. Paired L2Server reads
        // two dwords. Use the intended cddd shape instead of dereferencing
        // an unrelated variadic stack slot.
        socket->Send("cddd", 0x64, 0, userServerId, petServerId);
        return false;
    }

    CPetSP pet = g_petDb.GetPet(petId, runtime::process::CurrentDatabaseId());
    if (!pet) {
        // Log.Add(
        //     LOG_ERROR, "RequestGiveItemToPet failed. Cannot pet. user[%d], pet [%d]", characterId, petId
        // )
        // L2_LOG_ERROR(Log, "RequestGiveItemToPet failed. Cannot pet. user[%d], pet [%d]", characterId, petId
        // )
        L2_LOG_ERROR(Log, "[pets] Cannot give item %d from character %d: pet %d unavailable. Sending failure reply.", itemId, characterId, petId);
        socket->Send("cddd", 0x64, 0, userServerId, petServerId);
        return false;
    }

    CWareHouseSP userWarehouse = user->GetWareHouse(WareHouseType_Inventory, true);
    CPetWarehouseSP petWarehouse = pet->GetWarehouse();
    if (!userWarehouse || !petWarehouse) {
        // Log.Add(
        //     LOG_ERROR,
        //     "RequestGiveItemToPet failed. invalid warehouse. "
        //     "user[%d], pet collar[%d]",
        //     characterId, petId
        // )
        // L2_LOG_ERROR(Log,
        //     "RequestGiveItemToPet failed. invalid warehouse. "
        //     "user[%d], pet collar[%d]",
        //     characterId, petId
        // )
        L2_LOG_ERROR(Log, "[pets] Cannot give item %d to pet %d from character %d: character inventory %s, pet inventory %s. Sending failure reply.",
            itemId, petId, characterId, userWarehouse ? "available" : "unavailable", petWarehouse ? "available" : "unavailable");
        socket->Send("cddd", 0x64, 0, userServerId, petServerId);
        return false;
    }

    petWarehouse->SetPetId(static_cast<std::uint32_t>(petId));
    petWarehouse->SetNpcClassId(pet->NpcClassId());
    const auto consumeType = static_cast<ItemConsumeType>(consumeTypeValue);
    bool deposited = false;
    const std::uint64_t userLock = userWarehouse->GetLockAddr();
    const std::uint64_t petLock = petWarehouse->GetLockAddr();
    if (userLock == petLock) {
        // Log.Add(LOG_ERROR, "Same warehouse double writelock?-_-,")
        // L2_LOG_ERROR(Log, "Same warehouse double writelock?-_-,")
        L2_LOG_WARN(Log, "[pets] Character %d and pet %d inventories share a lock. Continuing item %d deposit with one write lock.", characterId, petId, itemId);
        {
            L2_WRITE_GUARD(*userWarehouse.operator->());
            deposited =
                petWarehouse->DepositToPet(socket, userServerId, petServerId, userWarehouse.get(), itemId, consumeType, count);
        }
    } else if (userLock < petLock) {
        {
            L2_WRITE_GUARD(*userWarehouse.operator->());
            {
                L2_WRITE_GUARD(*petWarehouse.operator->());
                deposited =
                    petWarehouse->DepositToPet(socket, userServerId, petServerId, userWarehouse.get(), itemId, consumeType, count);
            }
        }
    } else {
        {
            L2_WRITE_GUARD(*petWarehouse.operator->());
            {
                L2_WRITE_GUARD(*userWarehouse.operator->());
                deposited =
                    petWarehouse->DepositToPet(socket, userServerId, petServerId, userWarehouse.get(), itemId, consumeType, count);
            }
        }
    }

    if (!deposited) {
        socket->Send("cddd", 0x64, 0, userServerId, petServerId);
    }
    return false;
}

bool RequestGetItemFromPet(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00480410.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    int characterId = 0;
    int petServerId = 0;
    int petId = 0;
    int itemId = 0;
    std::int64_t count = 0;
    int consumeTypeValue = 0;
    ::Disassemble(packet, "dddddQd", &userServerId, &characterId, &petServerId, &petId, &itemId, &count, &consumeTypeValue);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     "RequestGetItemFromPet failed. Cannot find user. "
        //     "user[%d], pet collar[%d]",
        //     characterId, petId
        // )
        // L2_LOG_ERROR(Log,
        //     "RequestGetItemFromPet failed. Cannot find user. "
        //     "user[%d], pet collar[%d]",
        //     characterId, petId
        // )
        L2_LOG_ERROR(Log, "[pets] Cannot withdraw item %d from pet %d: character %d unavailable. Sending failure reply.", itemId, petId, characterId);
        socket->Send("cddd", 0x64, 0, userServerId, petServerId);
        return false;
    }

    CPetSP pet = g_petDb.GetPet(petId, runtime::process::CurrentDatabaseId());
    if (!pet) {
        // Log.Add(
        //     LOG_ERROR, "RequestGetItemFromPet failed. Cannot pet. user[%d], pet [%d]", characterId, petId
        // )
        // L2_LOG_ERROR(Log, "RequestGetItemFromPet failed. Cannot pet. user[%d], pet [%d]", characterId, petId
        // )
        L2_LOG_ERROR(Log, "[pets] Cannot withdraw item %d for character %d: pet %d unavailable. Sending failure reply.", itemId, characterId, petId);
        socket->Send("cddd", 0x64, 0, userServerId, petServerId);
        return false;
    }

    CWareHouseSP userWarehouse = user->GetWareHouse(WareHouseType_Inventory, true);
    CPetWarehouseSP petWarehouse = pet->GetWarehouse();
    if (!userWarehouse || !petWarehouse) {
        // Log.Add(
        //     LOG_ERROR,
        //     "RequestGetItemFromPet failed. invalid warehouse. "
        //     "user[%d], pet collar[%d]",
        //     characterId, petId
        // )
        // L2_LOG_ERROR(Log,
        //     "RequestGetItemFromPet failed. invalid warehouse. "
        //     "user[%d], pet collar[%d]",
        //     characterId, petId
        // )
        L2_LOG_ERROR(Log, "[pets] Cannot withdraw item %d from pet %d to character %d: character inventory %s, pet inventory %s. Sending failure reply.",
            itemId, petId, characterId, userWarehouse ? "available" : "unavailable", petWarehouse ? "available" : "unavailable");
        socket->Send("cddd", 0x64, 0, userServerId, petServerId);
        return false;
    }

    petWarehouse->SetPetId(static_cast<std::uint32_t>(petId));
    petWarehouse->SetNpcClassId(pet->NpcClassId());
    const auto consumeType = static_cast<ItemConsumeType>(consumeTypeValue);
    bool withdrawn = false;
    const std::uint64_t petLock = petWarehouse->GetLockAddr();
    const std::uint64_t userLock = userWarehouse->GetLockAddr();
    if (petLock == userLock) {
        // Log.Add(LOG_ERROR, "Same warehouse double writelock?-_-,")
        // L2_LOG_ERROR(Log, "Same warehouse double writelock?-_-,")
        L2_LOG_WARN(Log, "[pets] Pet %d and character %d inventories share a lock. Continuing item %d withdrawal with one write lock.", petId, characterId, itemId);
        {
            L2_WRITE_GUARD(*petWarehouse.operator->());
            withdrawn = petWarehouse->WithdrawFromPet(
                socket, userServerId, petServerId, userWarehouse.get(), itemId, consumeType, count
            );
        }
    } else if (petLock < userLock) {
        {
            L2_WRITE_GUARD(*petWarehouse.operator->());
            {
                L2_WRITE_GUARD(*userWarehouse.operator->());
                withdrawn = petWarehouse->WithdrawFromPet(
                    socket, userServerId, petServerId, userWarehouse.get(), itemId, consumeType, count
                );
            }
        }
    } else {
        {
            L2_WRITE_GUARD(*userWarehouse.operator->());
            {
                L2_WRITE_GUARD(*petWarehouse.operator->());
                withdrawn = petWarehouse->WithdrawFromPet(
                    socket, userServerId, petServerId, userWarehouse.get(), itemId, consumeType, count
                );
            }
        }
    }

    if (!withdrawn) {
        socket->Send("cddd", 0x64, 0, userServerId, petServerId);
    }
    return false;
}

bool RequestPetUseItem(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00480d50.
    L2_GUARD_FUNCTION();

    int petServerId = 0;
    int petId = 0;
    int itemId = 0;
    int consumeTypeValue = 0;
    int characterId = 0;
    ::Disassemble(packet, "ddddd", &petServerId, &petId, &itemId, &consumeTypeValue, &characterId);

    CPetSP pet = g_petDb.GetPet(petId, runtime::process::CurrentDatabaseId());
    if (!pet) {
        // Log.Add(LOG_ERROR, "RequestGetItemFromPet failed. Cannot pet. pet [%d]", petId)
        // L2_LOG_ERROR(Log, "RequestGetItemFromPet failed. Cannot pet. pet [%d]", petId)
        L2_LOG_ERROR(Log, "[pets] Cannot use item %d: pet %d unavailable. Sending failure reply.", itemId, petId);
        // Current failure sites spell the packet as cddd but physically
        // provide only petServerId after status. Preserve the target width
        // without serializing an unrelated stack dword.
        socket->Send("cddd", 0x69, 0, petServerId, 0);
        return false;
    }

    CPetWarehouseSP warehouse = pet->GetWarehouse();
    if (!warehouse) {
        // Log.Add(LOG_ERROR, "RequestPetUseItem failed. invalid warehouse. pet collar[%d]", petId)
        // L2_LOG_ERROR(Log, "RequestPetUseItem failed. invalid warehouse. pet collar[%d]", petId)
        L2_LOG_ERROR(Log, "[pets] Cannot use item %d for pet %d: inventory unavailable. Sending failure reply.", itemId, petId);
        socket->Send("cddd", 0x69, 0, petServerId, 0);
        return false;
    }

    CItemSP auditItem = warehouse->GetItem(itemId);
    const int itemType = auditItem ? static_cast<int>(auditItem->ItemType()) : 0;
    bool used;
    {
        L2_WRITE_GUARD(*warehouse.operator->());
        used = warehouse->PetUseItem(socket, petServerId, itemId, static_cast<ItemConsumeType>(consumeTypeValue));
    }
    if (!used) {
        socket->Send("cddd", 0x69, 0, petServerId, 0);
        return false;
    }

    if (characterId > 0) {
        CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
        if (user) {
            WritePetUseItemAudit(*user.get(), petId, static_cast<int>(pet->NpcClassId()), itemType, itemId);
        }
    }
    return false;
}

bool RequestSetAllianceCrest(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0045b288.
    L2_GUARD_FUNCTION();

    int userSocketId = 0;
    int allianceId = 0;
    int bitmapSize = 0;
    const std::uint8_t* const bitmap = ::Disassemble(packet, "ddd", &userSocketId, &allianceId, &bitmapSize);

    const std::uint32_t newCrestId = bitmapSize == 0 ? 0 : g_pledgeCrestDb.CreatePledgeCrest(bitmapSize, bitmap);
    socket->Send("cdd", 0x6a, userSocketId, static_cast<int>(newCrestId));
    return false;
}

bool RequestGetAllianceCrest(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004713f0.
    L2_GUARD_FUNCTION();

    std::uint32_t userSocketId = 0;
    std::uint32_t crestId = 0;
    std::uint32_t allianceId = 0;
    ::Disassemble(packet, "ddd", &userSocketId, &crestId, &allianceId);

    CPledgeCrestSP pledgeCrest = g_pledgeCrestDb.GetPledgeCrest(crestId);
    if (!pledgeCrest) {
        socket->Send("cdddd", 0x6b, userSocketId, crestId, allianceId, 0);
        return false;
    }

    const int bitmapSize = pledgeCrest->bitmapSize();
    socket->Send("cddddb", 0x6b, userSocketId, crestId, allianceId, bitmapSize, bitmapSize, pledgeCrest->bitmap());
    return false;
}

bool RequestLoadAllAlliance(CSocket* socket, const std::uint8_t* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0049bca8. The registered handler deliberately
    // ignores its payload and emits one reply for every kept alliance.
    L2_GUARD_FUNCTION();

    struct SendAlliance {
        explicit SendAlliance(CSocket* targetSocket) : socket(targetSocket) {}

        bool Do(CAlliance* alliance) {
            GuardHelper inCode(L"in_code", CThreadManager::GetThreadID());
            CPacketHelperEx<20480> members;
            int memberCount = 0;
            PledgeIdList memberPledgeIds;
            alliance->GetAllMemberPledgeId(memberPledgeIds);
            for (const std::uint32_t memberPledgeId : memberPledgeIds) {
                members.Assemble("d", static_cast<int>(memberPledgeId));
                ++memberCount;
            }

            if (memberCount == 0) {
                socket->Send(
                    "cdSddddd", 0x6d, alliance->id(), alliance->name(), alliance->masterPledgeId(), alliance->oustTime(),
                    alliance->crestId(), alliance->worldId(), 0
                );
            } else {
                socket->Send(
                    "cdSdddddb", 0x6d, alliance->id(), alliance->name(), alliance->masterPledgeId(), alliance->oustTime(),
                    alliance->crestId(), alliance->worldId(), memberCount, members.GetLength(), members.GetPacket()
                );
            }
            return true;
        }

        CSocket* socket;
    } send(socket);

    g_allianceDb.alliances().FOR_ALL(&send, L"CacheSocket.cpp", 0x222e);
    return false;
}

bool RequestSetAllianceInfo(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x0045b3b4.
    L2_GUARD_FUNCTION();

    std::uint32_t allianceId = 0;
    std::uint32_t updateFieldName = 0;
    std::uint32_t updateValue = 0;
    ::Disassemble(packet, "ddd", &allianceId, &updateFieldName, &updateValue);
    g_allianceDb.SetAllianceInfo(allianceId, updateFieldName, updateValue);
    return false;
}

bool RequestGetDbRelatedCounts(CSocket* socket, const std::uint8_t* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0045b47c. The fifth of seven SQL result columns
    // is fetched but not transmitted by the shipping function. The paired
    // current L2Server decoder and TotalChecker2015 PDB establish the six
    // transmitted meanings. No surviving source establishes column five's
    // name, so keep its omission explicit instead of assigning one.
    L2_GUARD_FUNCTION();

    int allianceCount = 0;
    int pledgeWarCount = 0;
    int dismissReservedCount = 0;
    int noblessCount = 0;
    int discardedResultColumn5 = 0;
    int worldOlympiadUserCount = 0;
    int matchCount = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&allianceCount);
    connection.Bind(&pledgeWarCount);
    connection.Bind(&dismissReservedCount);
    connection.Bind(&noblessCount);
    connection.Bind(&discardedResultColumn5);
    connection.Bind(&worldOlympiadUserCount);
    connection.Bind(&matchCount);

    constexpr const wchar_t* statement = L"{Call lin_GetDbRelatedCounts}";
    connection.Prepare(statement);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        // Shipping address 0x0045b5df also overwrites the discarded fifth
        // local with column seven before this virtual Send. It has no
        // observable effect, so the portable source keeps the effective
        // packet instead of a dead stack assignment.
        socket->Send(
            "cdddddd", 0x6f, allianceCount, pledgeWarCount, dismissReservedCount, noblessCount, matchCount,
            worldOlympiadUserCount
        );
    }
    return false;
}

bool RequestLoadAllPledgeWar(CSocket* socket, const std::uint8_t* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0045b664.
    L2_GUARD_FUNCTION();

    int challenger = 0;
    int challengee = 0;
    int declareTime = 0;
    int warPoint = 0;
    int previousWarPoint = 0;
    int deathCount = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&challenger);
    connection.Bind(&challengee);
    connection.Bind(&declareTime);
    connection.Bind(&warPoint);
    connection.Bind(&previousWarPoint);
    connection.Bind(&deathCount);

    constexpr const wchar_t* statement = L"{Call lin_GetWarDeclare}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement)) {
        return false;
    }

    bool fullBatch = false;
    do {
        fullBatch = false;
        int rowCount = 0;
        int payloadLength = 0;
        std::array<char, 0x2004> payload{};

        while (rowCount != 200) {
            if (!connection.Fetch())
                break;
            payloadLength += ::Assemble(
                payload.data() + payloadLength, static_cast<int>(payload.size()) - payloadLength, "dddddd", challenger,
                challengee, declareTime, warPoint, previousWarPoint, deathCount
            );
            ++rowCount;
        }

        if (rowCount == 200)
            fullBatch = true;
        socket->Send("cdb", 0x38, rowCount, payloadLength, payload.data());
    } while (fullBatch);

    return false;
}

bool RequestIncreaseUserPKCount(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x00471560.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] RequestIncreaseUserPKCount failed. invalid user[%d]", "CacheSocket.cpp", 0x2302,
        //     characterId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] RequestIncreaseUserPKCount failed. invalid user[%d]", "CacheSocket.cpp", 0x2302,
        //     characterId
        // )
        L2_LOG_ERROR(Log, "[character] Cannot increment PK count for character %d: character could not be loaded. Update skipped.", characterId);
        return false;
    }

    user->IncreasePKCount();
    user->Save();
    return false;
}

bool RequestIncreaseUserDuelCount(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x00471708.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    ::Disassemble(packet, "d", &characterId);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] RequestIncreaseUserDuelCount failed. invalid user[%d]", "CacheSocket.cpp", 0x2317,
        //     characterId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d] RequestIncreaseUserDuelCount failed. invalid user[%d]", "CacheSocket.cpp", 0x2317,
        //     characterId
        // )
        L2_LOG_ERROR(Log, "[character] Cannot increment duel count for character %d: character could not be loaded. Update skipped.", characterId);
        return false;
    }

    user->IncreaseDuelCount();
    user->Save();
    return false;
}

bool RequestRecipeMakeItem(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004718b0. This owner only resolves and locks the
    // inventory. The complete atomic item transaction remains in the target
    // CWareHouse::RecipeMakeItem owner at 0x0061b4e0.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    const std::uint8_t* warehousePacket = ::Disassemble(packet, "d", &characterId);

    auto sendFailure = [&]() {
        int responseValue1 = 0;
        int responseValue2 = 0;
        ::Disassemble(warehousePacket, "dd", &responseValue1, &responseValue2);
        socket->Send("cdddd", 0x70, responseValue1, responseValue2, 0, 0);
    };

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user", "CacheSocket.cpp", 0x233e)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user", "CacheSocket.cpp", 0x233e)
        L2_LOG_ERROR(Log, "[crafting] Cannot craft for character %d: character could not be loaded. Sending failure reply.", characterId);
        sendFailure();
        return false;
    }

    CWareHouseSP inventory = user->GetWareHouse(0, true);
    if (!inventory) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find inventory warehouse, char dbid[%d]", "CacheSocket.cpp", 0x2349,
        //     characterId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find inventory warehouse, char dbid[%d]", "CacheSocket.cpp", 0x2349,
        //     characterId
        // )
        L2_LOG_ERROR(Log, "[crafting] Cannot craft for character %d: inventory unavailable. Sending failure reply.", characterId);
        sendFailure();
        return false;
    }

    {
        L2_WRITE_GUARD(*inventory.operator->());
        if (inventory->m_lateSavingItemsPending) {
            inventory->SaveLateSavingItems();
        }
        inventory->RecipeMakeItem(socket, warehousePacket);
    }
    return false;
}

bool RequestPickItem(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00471c60. The handler decodes the complete packet
    // once for its two failure replies, then passes the post-character slice
    // and the separately captured audit context to CWareHouse::PickItem.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    const std::uint8_t* warehousePacket = ::Disassemble(packet, "d", &characterId);

    int userServerId = 0;
    int ignoredItemType = 0;
    int ignoredConsumeType = 0;
    int requestedItemId = 0;
    int itemServerId = 0;
    std::int64_t requestedAmount = 0;
    int requestContext = 0;
    short deleteResidenceGuard = 0;
    const std::uint8_t* cursor = ::Disassemble(
        warehousePacket, "dddddQdh", &userServerId, &ignoredItemType, &ignoredConsumeType, &requestedItemId, &itemServerId,
        &requestedAmount, &requestContext, &deleteResidenceGuard
    );

    int guardX = 0;
    int guardY = 0;
    int guardZ = 0;
    if (deleteResidenceGuard != 0) {
        ::Disassemble(cursor, "ddd", &guardX, &guardY, &guardZ);
    }

    auto sendFailure = [&]() {
        if (deleteResidenceGuard == 0) {
            socket->Send(
                "cdddQdQdh", 0x71, userServerId, requestedItemId, itemServerId, static_cast<long long>(requestedAmount), 0,
                static_cast<long long>(0), 2, 0
            );
            return;
        }

        socket->Send(
            "cdddQdQdhdddh", 0x71, userServerId, requestedItemId, itemServerId, static_cast<long long>(requestedAmount), 0,
            static_cast<long long>(0), 2, static_cast<int>(deleteResidenceGuard), guardX, guardY, guardZ, 0
        );
    };

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, dbid[%d]", "CacheSocket.cpp", 0x2390, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, dbid[%d]", "CacheSocket.cpp", 0x2390, characterId)
        L2_LOG_ERROR(Log, "[items] Pickup request %d for character %d failed: character unavailable (item %d, amount %lld). Sending failure reply.",
            requestContext, characterId, requestedItemId, static_cast<long long>(requestedAmount));
        sendFailure();
        return false;
    }

    CWareHouseSP inventory = user->GetWareHouse(0, true);
    if (!inventory) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find inventory warehouse, char dbid[%d]", "CacheSocket.cpp", 0x239b,
        //     characterId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find inventory warehouse, char dbid[%d]", "CacheSocket.cpp", 0x239b,
        //     characterId
        // )
        L2_LOG_ERROR(Log, "[items] Pickup request %d for character %d failed: inventory unavailable (item %d, amount %lld). Sending failure reply.",
            requestContext, characterId, requestedItemId, static_cast<long long>(requestedAmount));
        sendFailure();
        return false;
    }

    {
        L2_WRITE_GUARD(*inventory.operator->());
        if (inventory->m_lateSavingItemsPending) {
            inventory->SaveLateSavingItems();
        }
        inventory->PickItem(socket, warehousePacket, requestContext);
    }
    return false;
}

bool RequestPackageLoadTargetList(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004826b8. The paired current L2Server sender at
    // 0x0064b394 fixes the request as character DB ID followed by user SID.
    // ReplyPackageLoadTargetList 0x006aea30 consumes the echoed SID and dS
    // rows from opcode 0x78.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int userServerId = 0;
    ::Disassemble(packet, "dd", &characterId, &userServerId);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, dbid[%d]", "CacheSocket.cpp", 0x240e, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, dbid[%d]", "CacheSocket.cpp", 0x240e, characterId)
        L2_LOG_ERROR(Log, "[package] Cannot load recipient list for character %d: character could not be loaded. Returning an empty list.", characterId);
        socket->Send("cdd", 0x78, userServerId, 0);
        return false;
    }

    CAccountSP account = CAccount::Load(static_cast<unsigned int>(user->GetAccountID()));
    if (!account) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, dbid[%d]", "CacheSocket.cpp", 0x2442, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, dbid[%d]", "CacheSocket.cpp", 0x2442, characterId)
        L2_LOG_ERROR(Log, "[package] Cannot load recipient list for character %d: account could not be loaded. Returning an empty list.", characterId);
        socket->Send("cdd", 0x78, userServerId, 0);
        return false;
    }

    std::optional<PackageTargetListAssembler> targetsStorage;
    {
        L2_READ_GUARD(*account.operator->());
        auto& targets = targetsStorage.emplace(characterId);
        account->GetChars().FOR_ALL(&targets, L"CacheSocket.cpp", 0x243d);
    }
    auto& targets = *targetsStorage;

    if (targets.rowCount < 1) {
        socket->Send("cdd", 0x78, userServerId, 0);
    } else {
        socket->Send("cddb", 0x78, userServerId, targets.rowCount, targets.rowBytes, targets.rows.data());
    }
    return false;
}

bool RequestPackageSendToTarget(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00472144. The paired L2Server sender at
    // 0x0064b468 fixes the full request as cdddddb. This owner consumes the
    // first source-character ID, source SID and target-character ID before
    // passing the unchanged packet to CWareHouse::GetPackage 0x005f3b34.
    L2_GUARD_FUNCTION();

    int sourceCharacterId = 0;
    int sourceUserServerId = 0;
    int targetCharacterId = 0;
    ::Disassemble(packet, "ddd", &sourceCharacterId, &sourceUserServerId, &targetCharacterId);

    auto sendFailure = [&]() { socket->Send("cdd", 0x79, sourceUserServerId, -2); };

    CUserSP sourceUser = g_userDb.GetUser(sourceCharacterId, true, false, runtime::process::CurrentDatabaseId());
    if (!sourceUser) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find user, dbid[%d]", "CacheSocket.cpp", 0x2472, sourceCharacterId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, dbid[%d]", "CacheSocket.cpp", 0x2472, sourceCharacterId
        // )
        L2_LOG_ERROR(Log, "[package] Transfer from character %d to %d failed: sender unavailable. Reply code -2.", sourceCharacterId, targetCharacterId);
        sendFailure();
        return false;
    }

    const int sourceAccountId = sourceUser->GetAccountID();
    CUserSP targetUser = g_userDb.GetUser(targetCharacterId, true, false, runtime::process::CurrentDatabaseId());
    if (!targetUser) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find user, dbid[%d]", "CacheSocket.cpp", 0x247b, targetCharacterId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, dbid[%d]", "CacheSocket.cpp", 0x247b, targetCharacterId
        // )
        L2_LOG_ERROR(Log, "[package] Transfer from character %d to %d failed: recipient unavailable. Reply code -2.", sourceCharacterId, targetCharacterId);
        sendFailure();
        return false;
    }

    const int targetAccountId = targetUser->GetAccountID();
    if (targetAccountId != sourceAccountId || sourceCharacterId == targetCharacterId) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]Try hack ?, source[%d][%d], "
        //     "target[%d][%d].",
        //     "CacheSocket.cpp", 0x2483, sourceCharacterId, sourceAccountId, targetCharacterId, targetAccountId
        // )
        // L2_LOG_ERROR(Log,
        //     "[%s][%d]Try hack ?, source[%d][%d], "
        //     "target[%d][%d].",
        //     "CacheSocket.cpp", 0x2483, sourceCharacterId, sourceAccountId, targetCharacterId, targetAccountId
        // )
        L2_LOG_WARN(Log, "[package] Transfer rejected: sender character %d (account %d), recipient %d (account %d); %s. Reply code -2.",
            sourceCharacterId, sourceAccountId, targetCharacterId, targetAccountId,
            targetAccountId != sourceAccountId ? "accounts differ" : "sender and recipient are the same character");
        sendFailure();
        return false;
    }

    CWareHouseSP sourceInventory = sourceUser->GetWareHouse(WareHouseType_Inventory, true);
    CWareHouseSP targetWarehouse = targetUser->GetWareHouse(WareHouseType_CharWarehouse, true);
    if (!sourceInventory || !targetWarehouse) {
        // Log.Add(
        //     LOG_ERROR,
        //     "RequestPackageSendToTarget item error. Cannot find "
        //     "warehouse. sender id[%d], receiver id[%d]",
        //     sourceCharacterId, targetCharacterId
        // )
        // L2_LOG_ERROR(Log,
        //     "RequestPackageSendToTarget item error. Cannot find "
        //     "warehouse. sender id[%d], receiver id[%d]",
        //     sourceCharacterId, targetCharacterId
        // )
        L2_LOG_ERROR(Log, "[package] Transfer failed: sender %d inventory %s, recipient %d warehouse %s. Reply code -2.",
            sourceCharacterId, sourceInventory ? "available" : "unavailable",
            targetCharacterId, targetWarehouse ? "available" : "unavailable");
        sendFailure();
        return false;
    }

    auto executeRequest = [&]() { targetWarehouse->GetPackage(socket, packet, sourceInventory.get()); };

    const std::uint64_t sourceLockAddress = sourceInventory->GetLockAddr();
    const std::uint64_t targetLockAddress = targetWarehouse->GetLockAddr();
    if (sourceLockAddress == targetLockAddress) {
        // Log.Add(LOG_ERROR, "Same warehouse double writelock?-_-,")
        // L2_LOG_ERROR(Log, "Same warehouse double writelock?-_-,")
        L2_LOG_ERROR(Log, "[package] Transfer from character %d to %d encountered equal warehouse lock addresses. Processing continues with one lock.", sourceCharacterId, targetCharacterId);
        {
            L2_WRITE_GUARD(*targetWarehouse.operator->());
            executeRequest();
        }
        return false;
    }

    if (targetLockAddress < sourceLockAddress) {
        {
            L2_WRITE_GUARD(*targetWarehouse.operator->());
            {
                L2_WRITE_GUARD(*sourceInventory.operator->());
                executeRequest();
            }
        }
        return false;
    }

    {
        L2_WRITE_GUARD(*sourceInventory.operator->());
        {
            L2_WRITE_GUARD(*targetWarehouse.operator->());
            executeRequest();
        }
    }
    return false;
}

bool RequestAddBlockList(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0045b890. The paired L2Server sender at
    // 0x0064b574 fixes the request body as ddSd after opcode 0x8f.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int userServerId = 0;
    int finalFlag = 0;
    std::array<char16_t, 28> targetName{};
    ::Disassemble(packet, "ddSd", &characterId, &userServerId, 0x32, targetName.data(), &finalFlag);

    std::wstring blockedCharacterName = FixedUtf16ToWide(targetName);
    const int result = g_blockCharDb.AddBlockChar(static_cast<unsigned int>(characterId), blockedCharacterName.data());
    if (result < 1) {
        // The target normalizes every non-positive AddBlockChar result and
        // replaces the echoed name with an empty string.
        socket->Send("cddSd", 0x7a, userServerId, 0, u"", finalFlag);
    } else {
        socket->Send("cddSd", 0x7a, userServerId, result, targetName.data(), finalFlag);
    }
    return false;
}

bool RequestDeleteBlockList(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0045ba04. The paired L2Server sender at
    // 0x0064b664 fixes the request body as ddS after opcode 0x90.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int userServerId = 0;
    std::array<char16_t, 28> targetName{};
    ::Disassemble(packet, "ddS", &characterId, &userServerId, 0x32, targetName.data());

    std::wstring blockedCharacterName = FixedUtf16ToWide(targetName);
    const int result = g_blockCharDb.DeleteBlockChar(static_cast<unsigned int>(characterId), blockedCharacterName.data());

    // Both physical target branches send the same cddS packet. Keep the
    // signed result intact: -1 (unresolved name) is observable in L2Server.
    socket->Send("cddS", 0x7b, userServerId, result, targetName.data());
    return false;
}

bool RequestLoadWarehouseItemsEx(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004729b0. Paired current L2Server
    // CDB::RequestLoadWarehouseEx 0x0064b818 fixes cdddddd.
    L2_GUARD_FUNCTION();

    int warehouseClassType = 0;
    int warehouseId = 0;
    int characterId = 0;
    int userServerId = 0;
    int warehouseMemoryId = 0;
    int itemListGetType = 0;
    ::Disassemble(
        packet, "dddddd", &warehouseClassType, &warehouseId, &characterId, &userServerId, &warehouseMemoryId,
        &itemListGetType
    );

    const auto sendFailure = [&]() {
        socket->Send(
            "cddddddd", 0x7d, warehouseClassType, warehouseId, userServerId, warehouseMemoryId, itemListGetType, 0, 0
        );
    };

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]Cannot find user, nWarehouseClassType[%d], "
        //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
        //     "CacheSocket.cpp", 0x2519, warehouseClassType, warehouseId, characterId, userServerId
        // )
        // L2_LOG_ERROR(Log,
        //     "[%s][%d]Cannot find user, nWarehouseClassType[%d], "
        //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
        //     "CacheSocket.cpp", 0x2519, warehouseClassType, warehouseId, characterId, userServerId
        // )
        L2_LOG_ERROR(Log, "[warehouse] Cannot load deposit list for warehouse %d (class %d): character %d unavailable. Sending failure reply to user object %d.",
            warehouseId, warehouseClassType, characterId, userServerId);
        sendFailure();
        return false;
    }

    if (warehouseClassType == 1) {
        CCastleSP castle = g_castleDb.GetCastle(warehouseId);
        if (!castle) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d]Cannot find castle, nWarehouseClassType[%d], "
            //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
            //     "CacheSocket.cpp", 0x252b, warehouseClassType, warehouseId, characterId, userServerId
            // )
            // L2_LOG_ERROR(Log,
            //     "[%s][%d]Cannot find castle, nWarehouseClassType[%d], "
            //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
            //     "CacheSocket.cpp", 0x252b, warehouseClassType, warehouseId, characterId, userServerId
            // )
            L2_LOG_ERROR(Log, "[warehouse] Cannot load castle deposit list: castle %d unavailable (character %d, user object %d). Sending failure reply.",
                warehouseId, characterId, userServerId);
            sendFailure();
            return false;
        }

        CWareHouseSP warehouse = castle->GetWareHouse();
        {
            L2_WRITE_GUARD(*warehouse.operator->());
            warehouse->SendDepositItemsListEx(socket, packet);
        }
        return false;
    }

    if (warehouseClassType == 2) {
        CPledgeSP pledge = g_pledgeDb.GetPledge(warehouseId, false, runtime::process::CurrentDatabaseId());
        if (!pledge) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d]Cannot find pledge, nWarehouseClassType[%d], "
            //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
            //     "CacheSocket.cpp", 0x253d, warehouseClassType, warehouseId, characterId, userServerId
            // )
            // L2_LOG_ERROR(Log,
            //     "[%s][%d]Cannot find pledge, nWarehouseClassType[%d], "
            //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
            //     "CacheSocket.cpp", 0x253d, warehouseClassType, warehouseId, characterId, userServerId
            // )
            L2_LOG_ERROR(Log, "[warehouse] Cannot load clan deposit list: clan %d unavailable (character %d, user object %d). Sending failure reply.",
                warehouseId, characterId, userServerId);
            sendFailure();
            return false;
        }

        CWareHouseSP warehouse = pledge->GetWareHouse();
        {
            L2_WRITE_GUARD(*warehouse.operator->());
            warehouse->SendDepositItemsListEx(socket, packet);
        }
        return false;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     "[%s][%d]Unknown warehouse type, nWarehouseClassType[%d], "
    //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
    //     "CacheSocket.cpp", 0x2545, warehouseClassType, warehouseId, characterId, userServerId
    // )
    // L2_LOG_ERROR(Log,
    //     "[%s][%d]Unknown warehouse type, nWarehouseClassType[%d], "
    //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
    //     "CacheSocket.cpp", 0x2545, warehouseClassType, warehouseId, characterId, userServerId
    // )
    L2_LOG_WARN(Log, "[warehouse] Deposit-list request rejected: unsupported warehouse class %d (warehouse %d, character %d, user object %d). Supported classes: 1 castle, 2 clan. Sending failure reply.",
        warehouseClassType, warehouseId, characterId, userServerId);
    sendFailure();
    return false;
}

bool RequestWithdrawWarehouseItemsEx(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00473088. Paired current L2Server
    // CDB::RequestWithdrawItemEx 0x0064b918 fixes the seven-dword request
    // header followed by complete CItemAssemblerWareHouse records.
    L2_GUARD_FUNCTION();

    int warehouseClassType = 0;
    int warehouseId = 0;
    int characterId = 0;
    int userServerId = 0;
    int warehouseMemoryId = 0;
    int requestContext = 0;
    ::Disassemble(
        packet, "dddddd", &warehouseClassType, &warehouseId, &characterId, &userServerId, &warehouseMemoryId, &requestContext
    );

    const auto sendFailure = [&]() { socket->Send("cdddd", 0x7e, warehouseClassType, warehouseId, userServerId, 1); };

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]Cannot find user, nWarehouseType[%d], "
        //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
        //     "CacheSocket.cpp", 0x257d, warehouseClassType, warehouseId, characterId, userServerId
        // )
        // L2_LOG_ERROR(Log,
        //     "[%s][%d]Cannot find user, nWarehouseType[%d], "
        //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
        //     "CacheSocket.cpp", 0x257d, warehouseClassType, warehouseId, characterId, userServerId
        // )
        L2_LOG_ERROR(Log, "[warehouse] Withdrawal failed for character %d from warehouse %d (class %d): character unavailable. Sending failure reply to user object %d.",
            characterId, warehouseId, warehouseClassType, userServerId);
        sendFailure();
        return false;
    }

    if (warehouseClassType == 1) {
        CCastleSP castle = g_castleDb.GetCastle(warehouseId);
        if (!castle) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d]Cannot find castle, nWarehouseType[%d], "
            //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
            //     "CacheSocket.cpp", 0x25d0, warehouseClassType, warehouseId, characterId, userServerId
            // )
            // L2_LOG_ERROR(Log,
            //     "[%s][%d]Cannot find castle, nWarehouseType[%d], "
            //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
            //     "CacheSocket.cpp", 0x25d0, warehouseClassType, warehouseId, characterId, userServerId
            // )
            L2_LOG_ERROR(Log, "[warehouse] Withdrawal failed for character %d from warehouse %d (class %d): castle unavailable. Sending failure reply to user object %d.",
                characterId, warehouseId, warehouseClassType, userServerId);
            sendFailure();
            return false;
        }

        CWareHouseSP sourceWarehouse = castle->GetWareHouse();
        CWareHouseSP inventory = user->GetWareHouse(WareHouseType_Inventory, true);
        if (!inventory || !sourceWarehouse) {
            // The pledge wording is present in the current castle branch.
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d]Cannot find pledge, nWarehouseType[%d], "
            //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
            //     "CacheSocket.cpp", 0x25c8, warehouseClassType, warehouseId, characterId, userServerId
            // )
            // L2_LOG_ERROR(Log,
            //     "[%s][%d]Cannot find pledge, nWarehouseType[%d], "
            //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
            //     "CacheSocket.cpp", 0x25c8, warehouseClassType, warehouseId, characterId, userServerId
            // )
            L2_LOG_ERROR(Log, "[warehouse] Withdrawal failed for character %d from warehouse %d (class %d): inventory %s, source warehouse %s. Sending failure reply.",
                characterId, warehouseId, warehouseClassType, inventory ? "available" : "unavailable", sourceWarehouse ? "available" : "unavailable");
            sendFailure();
            return false;
        }

        const std::uint64_t sourceLock = sourceWarehouse->GetLockAddr();
        const std::uint64_t inventoryLock = inventory->GetLockAddr();
        if (sourceLock == inventoryLock) {
            // Log.Add(LOG_ERROR, "Same warehouse double writelock?-_-,")
            // L2_LOG_ERROR(Log, "Same warehouse double writelock?-_-,")
            L2_LOG_WARN(Log, "[warehouse] Withdrawal for character %d from warehouse %d (class %d) shares the inventory lock. Continuing with one write lock.",
                characterId, warehouseId, warehouseClassType);
            {
                L2_WRITE_GUARD(*sourceWarehouse.operator->());
                if (!inventory->WithdrawItemsFromBankEx(socket, packet, sourceWarehouse.get())) {
                    sendFailure();
                }
            }
            return false;
        }

        if (sourceLock < inventoryLock) {
            {
                L2_WRITE_GUARD(*sourceWarehouse.operator->());
                {
                    L2_WRITE_GUARD(*inventory.operator->());
                    if (!inventory->WithdrawItemsFromBankEx(socket, packet, sourceWarehouse.get())) {
                        sendFailure();
                    }
                }
            }
            return false;
        }

        {
            L2_WRITE_GUARD(*inventory.operator->());
            {
                L2_WRITE_GUARD(*sourceWarehouse.operator->());
                if (!inventory->WithdrawItemsFromBankEx(socket, packet, sourceWarehouse.get())) {
                    sendFailure();
                }
            }
        }
        return false;
    }

    if (warehouseClassType == 2) {
        CPledgeSP pledge = g_pledgeDb.GetPledge(warehouseId, false, runtime::process::CurrentDatabaseId());
        if (!pledge) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d]Cannot find pledge, nWarehouseType[%d], "
            //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
            //     "CacheSocket.cpp", 0x25a7, warehouseClassType, warehouseId, characterId, userServerId
            // )
            // L2_LOG_ERROR(Log,
            //     "[%s][%d]Cannot find pledge, nWarehouseType[%d], "
            //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
            //     "CacheSocket.cpp", 0x25a7, warehouseClassType, warehouseId, characterId, userServerId
            // )
            L2_LOG_ERROR(Log, "[warehouse] Withdrawal failed for character %d from warehouse %d (class %d): clan unavailable. Sending failure reply to user object %d.",
                characterId, warehouseId, warehouseClassType, userServerId);
            sendFailure();
            return false;
        }

        CWareHouseSP sourceWarehouse = pledge->GetWareHouse();
        CWareHouseSP inventory = user->GetWareHouse(WareHouseType_Inventory, true);
        if (!inventory || !sourceWarehouse) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d]Cannot find pledge, nWarehouseType[%d], "
            //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
            //     "CacheSocket.cpp", 0x259f, warehouseClassType, warehouseId, characterId, userServerId
            // )
            // L2_LOG_ERROR(Log,
            //     "[%s][%d]Cannot find pledge, nWarehouseType[%d], "
            //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
            //     "CacheSocket.cpp", 0x259f, warehouseClassType, warehouseId, characterId, userServerId
            // )
            L2_LOG_ERROR(Log, "[warehouse] Withdrawal failed for character %d from warehouse %d (class %d): inventory %s, source warehouse %s. Sending failure reply.",
                characterId, warehouseId, warehouseClassType, inventory ? "available" : "unavailable", sourceWarehouse ? "available" : "unavailable");
            sendFailure();
            return false;
        }

        const std::uint64_t sourceLock = sourceWarehouse->GetLockAddr();
        const std::uint64_t inventoryLock = inventory->GetLockAddr();
        if (sourceLock == inventoryLock) {
            // Log.Add(LOG_ERROR, "Same warehouse double writelock?-_-,")
            // L2_LOG_ERROR(Log, "Same warehouse double writelock?-_-,")
            L2_LOG_WARN(Log, "[warehouse] Withdrawal for character %d from warehouse %d (class %d) shares the inventory lock. Continuing with one write lock.",
                characterId, warehouseId, warehouseClassType);
            {
                L2_WRITE_GUARD(*sourceWarehouse.operator->());
                if (!inventory->WithdrawItemsFromBankEx(socket, packet, sourceWarehouse.get())) {
                    sendFailure();
                }
            }
            return false;
        }

        if (sourceLock < inventoryLock) {
            {
                L2_WRITE_GUARD(*sourceWarehouse.operator->());
                {
                    L2_WRITE_GUARD(*inventory.operator->());
                    if (!inventory->WithdrawItemsFromBankEx(socket, packet, sourceWarehouse.get())) {
                        sendFailure();
                    }
                }
            }
            return false;
        }

        {
            L2_WRITE_GUARD(*inventory.operator->());
            {
                L2_WRITE_GUARD(*sourceWarehouse.operator->());
                if (!inventory->WithdrawItemsFromBankEx(socket, packet, sourceWarehouse.get())) {
                    sendFailure();
                }
            }
        }
        return false;
    }

    if (warehouseClassType == 3) {
        // The current handler performs this second user lookup even though
        // the first one succeeded. Its failure path is the older opcode-10
        // warehouse error response and is therefore distinct.
        CUserSP character = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
        if (!character) {
            // Log.Add(
            //     LOG_ERROR,
            //     "Withdraw warehouse item error. Cannot find user. "
            //     "id[%d], warehouse[%d]",
            //     characterId, warehouseId
            // )
            // L2_LOG_ERROR(Log,
            //     "Withdraw warehouse item error. Cannot find user. "
            //     "id[%d], warehouse[%d]",
            //     characterId, warehouseId
            // )
            L2_LOG_ERROR(Log, "[warehouse] Personal withdrawal failed: character %d unavailable on repeated lookup (warehouse %d). Sending legacy withdrawal failure reply (-2).",
                characterId, warehouseId);
            int responseId = 0;
            ::Disassemble(packet, "d", &responseId);
            socket->Send("cdd", 10, responseId, -2);
            return false;
        }

        CWareHouseSP inventory = character->GetWareHouse(WareHouseType_Inventory, true);
        CWareHouseSP sourceWarehouse = character->GetWareHouse(WareHouseType_CharWarehouse, true);
        if (!inventory || !sourceWarehouse) {
            // The current character-warehouse branch keeps this typo.
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d]Cannot find pledge, nWarehouseType[%d], "
            //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
            //     "CacheSocket.cpp", 0x25f9, warehouseClassType, warehouseId, characterId, userServerId
            // )
            // L2_LOG_ERROR(Log,
            //     "[%s][%d]Cannot find pledge, nWarehouseType[%d], "
            //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
            //     "CacheSocket.cpp", 0x25f9, warehouseClassType, warehouseId, characterId, userServerId
            // )
            L2_LOG_ERROR(Log, "[warehouse] Withdrawal failed for character %d from warehouse %d (class %d): inventory %s, source warehouse %s. Sending failure reply.",
                characterId, warehouseId, warehouseClassType, inventory ? "available" : "unavailable", sourceWarehouse ? "available" : "unavailable");
            sendFailure();
            return false;
        }

        const std::uint64_t sourceLock = sourceWarehouse->GetLockAddr();
        const std::uint64_t inventoryLock = inventory->GetLockAddr();
        if (sourceLock == inventoryLock) {
            // Log.Add(LOG_ERROR, "Same warehouse double writelock?-_-,")
            // L2_LOG_ERROR(Log, "Same warehouse double writelock?-_-,")
            L2_LOG_WARN(Log, "[warehouse] Withdrawal for character %d from warehouse %d (class %d) shares the inventory lock. Continuing with one write lock.",
                characterId, warehouseId, warehouseClassType);
            {
                L2_WRITE_GUARD(*sourceWarehouse.operator->());
                if (!inventory->WithdrawItemsFromBankEx(socket, packet, sourceWarehouse.get())) {
                    sendFailure();
                }
            }
            return false;
        }

        if (sourceLock < inventoryLock) {
            {
                L2_WRITE_GUARD(*sourceWarehouse.operator->());
                {
                    L2_WRITE_GUARD(*inventory.operator->());
                    if (!inventory->WithdrawItemsFromBankEx(socket, packet, sourceWarehouse.get())) {
                        sendFailure();
                    }
                }
            }
            return false;
        }

        {
            L2_WRITE_GUARD(*inventory.operator->());
            {
                L2_WRITE_GUARD(*sourceWarehouse.operator->());
                if (!inventory->WithdrawItemsFromBankEx(socket, packet, sourceWarehouse.get())) {
                    sendFailure();
                }
            }
        }
        return false;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     "[%s][%d]Unknown warehouse type, nWarehouseType[%d], "
    //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
    //     "CacheSocket.cpp", 0x2602, warehouseClassType, warehouseId, characterId, userServerId
    // )
    // L2_LOG_ERROR(Log,
    //     "[%s][%d]Unknown warehouse type, nWarehouseType[%d], "
    //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
    //     "CacheSocket.cpp", 0x2602, warehouseClassType, warehouseId, characterId, userServerId
    // )
    L2_LOG_WARN(Log, "[warehouse] Withdrawal rejected: unsupported class %d (warehouse %d, character %d, user object %d). Supported classes: 1 castle, 2 clan, 3 personal. Sending failure reply.",
        warehouseClassType, warehouseId, characterId, userServerId);
    sendFailure();
    return false;
}

bool RequestDepositWarehouseItemsEx(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004745bc. Paired current L2Server
    // CDB::RequestDepositItemEx 0x0064ba3c fixes the six-dword header and
    // complete CItemAssemblerWareHouse payload consumed by the model.
    L2_GUARD_FUNCTION();

    int warehouseClassType = 0;
    int warehouseId = 0;
    int characterId = 0;
    int userServerId = 0;
    ::Disassemble(packet, "dddd", &warehouseClassType, &warehouseId, &characterId, &userServerId);

    const auto sendResult = [&](int result) {
        socket->Send("cddddd", 0x7f, warehouseClassType, warehouseId, userServerId, result, 0);
    };

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]Cannot find user, nWarehouseType[%d], "
        //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
        //     "CacheSocket.cpp", 0x263a, warehouseClassType, warehouseId, characterId, userServerId
        // )
        // L2_LOG_ERROR(Log,
        //     "[%s][%d]Cannot find user, nWarehouseType[%d], "
        //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
        //     "CacheSocket.cpp", 0x263a, warehouseClassType, warehouseId, characterId, userServerId
        // )
        L2_LOG_ERROR(Log, "[warehouse] Deposit failed for character %d into warehouse %d (class %d): character unavailable. Sending failure code 1 to user object %d.",
            characterId, warehouseId, warehouseClassType, userServerId);
        sendResult(1);
        return false;
    }

    if (warehouseClassType == 2) {
        CPledgeSP pledge = g_pledgeDb.GetPledge(warehouseId, false, runtime::process::CurrentDatabaseId());

        // The shipping handler performs this first inventory acquisition
        // before checking the pledge and acquires it again on the transfer
        // path. Retaining both calls keeps its reference/lifetime path.
        CWareHouseSP inventoryProbe = user->GetWareHouse(WareHouseType_Inventory, true);
        if (!pledge) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d]Cannot find pledge, nWarehouseType[%d], "
            //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
            //     "CacheSocket.cpp", 0x2653, warehouseClassType, warehouseId, characterId, userServerId
            // )
            // L2_LOG_ERROR(Log,
            //     "[%s][%d]Cannot find pledge, nWarehouseType[%d], "
            //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
            //     "CacheSocket.cpp", 0x2653, warehouseClassType, warehouseId, characterId, userServerId
            // )
            L2_LOG_ERROR(Log, "[warehouse] Deposit failed for character %d into warehouse %d (class %d): clan unavailable. Sending failure code 1 to user object %d.",
                characterId, warehouseId, warehouseClassType, userServerId);
            sendResult(1);
            return false;
        }

        CWareHouseSP destinationWarehouse = pledge->GetWareHouse();
        CWareHouseSP inventory = user->GetWareHouse(WareHouseType_Inventory, true);
        if (!destinationWarehouse || !inventory) {
            // Current target logs only. It does not synthesize a reply here.
            // Log.Add(LOG_ERROR, "Error, Invalid sp.")
            // L2_LOG_ERROR(Log, "Error, Invalid sp.")
            L2_LOG_ERROR(Log, "[warehouse] Deposit failed for character %d into warehouse %d (class %d): inventory %s, destination warehouse %s. No reply is sent.",
                characterId, warehouseId, warehouseClassType, inventory ? "available" : "unavailable", destinationWarehouse ? "available" : "unavailable");
            return false;
        }

        const std::uint64_t destinationLock = destinationWarehouse->GetLockAddr();
        const std::uint64_t inventoryLock = inventory->GetLockAddr();
        if (destinationLock == inventoryLock) {
            // Log.Add(LOG_ERROR, "Same warehouse double writelock?-_-,")
            // L2_LOG_ERROR(Log, "Same warehouse double writelock?-_-,")
            L2_LOG_WARN(Log, "[warehouse] Deposit for character %d into warehouse %d (class %d) shares the inventory lock. Continuing with one write lock.",
                characterId, warehouseId, warehouseClassType);
            {
                L2_WRITE_GUARD(*destinationWarehouse.operator->());
                const int result = inventory->DepositItemsToBankEx(socket, packet, destinationWarehouse.get());
                if (result > 0) {
                    sendResult(result);
                }
            }
            return false;
        }

        if (destinationLock < inventoryLock) {
            {
                L2_WRITE_GUARD(*destinationWarehouse.operator->());
                L2_WRITE_GUARD(*inventory.operator->());
                const int result = inventory->DepositItemsToBankEx(socket, packet, destinationWarehouse.get());
                if (result > 0) {
                    sendResult(result);
                }
            }
            return false;
        }

        {
            L2_WRITE_GUARD(*inventory.operator->());
            L2_WRITE_GUARD(*destinationWarehouse.operator->());
            const int result = inventory->DepositItemsToBankEx(socket, packet, destinationWarehouse.get());
            if (result > 0) {
                sendResult(result);
            }
        }
        return false;
    }

    if (warehouseClassType == 1) {
        CCastleSP castle = g_castleDb.GetCastle(warehouseId);
        if (!castle) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d]Cannot find castle, nWarehouseType[%d], "
            //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
            //     "CacheSocket.cpp", 0x266b, warehouseClassType, warehouseId, characterId, userServerId
            // )
            // L2_LOG_ERROR(Log,
            //     "[%s][%d]Cannot find castle, nWarehouseType[%d], "
            //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
            //     "CacheSocket.cpp", 0x266b, warehouseClassType, warehouseId, characterId, userServerId
            // )
            L2_LOG_ERROR(Log, "[warehouse] Deposit failed for character %d into warehouse %d (class %d): castle unavailable. Sending failure code 1 to user object %d.",
                characterId, warehouseId, warehouseClassType, userServerId);
            sendResult(1);
            return false;
        }

        CWareHouseSP destinationWarehouse = castle->GetWareHouse();
        CWareHouseSP inventory = user->GetWareHouse(WareHouseType_Inventory, true);
        if (!destinationWarehouse || !inventory) {
            // As in the pledge branch, current Cached.exe logs but sends no
            // result packet for this invalid-smart-pointer state.
            // Log.Add(LOG_ERROR, "Error, Invalid sp.")
            // L2_LOG_ERROR(Log, "Error, Invalid sp.")
            L2_LOG_ERROR(Log, "[warehouse] Deposit failed for character %d into warehouse %d (class %d): inventory %s, destination warehouse %s. No reply is sent.",
                characterId, warehouseId, warehouseClassType, inventory ? "available" : "unavailable", destinationWarehouse ? "available" : "unavailable");
            return false;
        }

        const std::uint64_t destinationLock = destinationWarehouse->GetLockAddr();
        const std::uint64_t inventoryLock = inventory->GetLockAddr();
        if (destinationLock == inventoryLock) {
            // Log.Add(LOG_ERROR, "Same warehouse double writelock?-_-,")
            // L2_LOG_ERROR(Log, "Same warehouse double writelock?-_-,")
            L2_LOG_WARN(Log, "[warehouse] Deposit for character %d into warehouse %d (class %d) shares the inventory lock. Continuing with one write lock.",
                characterId, warehouseId, warehouseClassType);
            {
                L2_WRITE_GUARD(*destinationWarehouse.operator->());
                const int result = inventory->DepositItemsToBankEx(socket, packet, destinationWarehouse.get());
                if (result != 0) {
                    sendResult(result);
                }
            }
            return false;
        }

        if (destinationLock < inventoryLock) {
            {
                L2_WRITE_GUARD(*destinationWarehouse.operator->());
                L2_WRITE_GUARD(*inventory.operator->());
                const int result = inventory->DepositItemsToBankEx(socket, packet, destinationWarehouse.get());
                if (result != 0) {
                    sendResult(result);
                }
            }
            return false;
        }

        {
            L2_WRITE_GUARD(*inventory.operator->());
            L2_WRITE_GUARD(*destinationWarehouse.operator->());
            const int result = inventory->DepositItemsToBankEx(socket, packet, destinationWarehouse.get());
            if (result != 0) {
                sendResult(result);
            }
        }
        return false;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     "[%s][%d]Unknown warehouse type, nWarehouseType[%d], "
    //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
    //     "CacheSocket.cpp", 0x2673, warehouseClassType, warehouseId, characterId, userServerId
    // )
    // L2_LOG_ERROR(Log,
    //     "[%s][%d]Unknown warehouse type, nWarehouseType[%d], "
    //     "nWarehouseId[%d], nCharDBID[%d], nUserServerId[%d]",
    //     "CacheSocket.cpp", 0x2673, warehouseClassType, warehouseId, characterId, userServerId
    // )
    L2_LOG_WARN(Log, "[warehouse] Deposit rejected: unsupported class %d (warehouse %d, character %d, user object %d). Supported classes: 1 castle, 2 clan. Sending failure code 1.",
        warehouseClassType, warehouseId, characterId, userServerId);
    sendResult(1);
    return false;
}

bool RequestSetNextCastleSiege(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00491644. Paired current L2Server
    // CDB::RequestSetNextCastleSiege 0x00668334 fixes the
    // routing and castle fields of this four-dword request.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    int cacheContextId = 0;
    std::uint32_t castleId = 0;
    std::uint32_t nextWarTime = 0;
    ::Disassemble(packet, "dddd", &userServerId, &cacheContextId, &castleId, &nextWarTime);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateCastleNextWar (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&nextWarTime);
    connection.BindParameter(&castleId);
    const bool success = connection.ExecutePrepared(statement);

    socket->Send(
        "cddddd", 0x80, userServerId, cacheContextId, runtime::Int32FromBits(castleId), runtime::Int32FromBits(nextWarTime),
        success ? 1 : 0
    );
    WriteSetNextCastleSiegeAudit(castleId, nextWarTime, success);
    return false;
}

bool RequestCreateAgitAuction(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00491834. Paired current L2Server
    // CDB::RequestCreateAgitAuction 0x0064c088 and
    // ReplyCreateAgitAuction 0x006af850 fix both packet
    // formats and the echoed request identity.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int requestContext = 0;
    int agitId = 0;
    std::array<char16_t, 256> description{};
    long long minimumPrice = 0;
    int auctionDays = 0;
    long long adenaAmount = 0;
    int auctionTax = 0;
    ::Disassemble(
        packet, "dddSQdQd", &characterId, &requestContext, &agitId, 0x200, description.data(), &minimumPrice, &auctionDays,
        &adenaAmount, &auctionTax
    );

    // The target performs each operation in a 32-bit register. Preserve the
    // two's-complement wrap and the signed, truncating division without
    // invoking C++ signed-overflow undefined behavior.
    const std::int32_t now = runtime::crt::Time32Now();
    const std::uint32_t unroundedBits = static_cast<std::uint32_t>(auctionDays) * 86400U + static_cast<std::uint32_t>(now);
    const std::int32_t unrounded = runtime::Int32FromBits(unroundedBits);
    const std::int32_t hour = unrounded / 3600;
    const int auctionTime = runtime::Int32FromBits((static_cast<std::uint32_t>(hour) + 1U) * 3600U);

    int auctionId = 0;
    bool adenaDeleted = false;
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        const int pledgeId = runtime::Int32FromBits(user->GetPledgeID());
        CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
        if (pledge) {
            CWareHouseSP warehouse = pledge->GetWareHouse();
            if (warehouse) {
                {
                    L2_WRITE_GUARD(*warehouse.operator->());
                    adenaDeleted = warehouse->DelAdena(adenaAmount);
                }

                if (adenaDeleted) {
                    DBConn connection(runtime::process::CurrentDatabaseId());
                    connection.Bind(&auctionId);
                    const std::wstring wideDescription = FixedUtf16ToWide(description);
                    if (ExecuteCreateAgitAuction(
                            connection, agitId, wideDescription.c_str(), minimumPrice, auctionTime, auctionTax
                        )) {
                        connection.Fetch();
                        connection.UnBind();
                        ExecuteInsertAgitAdena(connection, agitId, pledgeId, auctionId, 1, adenaAmount);
                    }
                }
            }
        }

        WriteCreateAgitAuctionAudit(
            *user.get(), description.data(), auctionId, agitId, minimumPrice, adenaAmount, auctionDays, auctionTime,
            adenaDeleted
        );
    }

    socket->Send(
        "cdddSQddd", 0x81, requestContext, auctionId, agitId, description.data(), minimumPrice, auctionDays, auctionTime,
        adenaDeleted ? 1 : 0
    );
    return false;
}

bool RequestCreateAgitBid(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00491ec4. Paired current L2Server
    // CDB::RequestCreateAgitBid 0x0064c19c and ReplyCreateAgitBid
    // 0x006afa60 fix both wire directions.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int requestContext = 0;
    int agitId = 0;
    int auctionId = 0;
    long long requestedPrice = 0;
    int requestPledgeId = 0;
    int attendTime = 0;
    ::Disassemble(
        packet, "ddddQdd", &characterId, &requestContext, &agitId, &auctionId, &requestedPrice, &requestPledgeId, &attendTime
    );

    long long amountToDebit = requestedPrice;
    int bidId = 0;
    long long previousPrice = 0;
    int status = 0;
    bool adenaDeleted = false;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&auctionId);
    connection.Bind(&bidId);
    connection.Bind(&previousPrice);
    const int queriedAuctionId = auctionId;
    const int queriedPledgeId = requestPledgeId;
    constexpr const wchar_t* getBidStatement = L"{Call lin_GetAgitBidOne (?, ? )}";
    connection.Prepare(getBidStatement);
    connection.BindParameter(&queriedAuctionId);
    connection.BindParameter(&queriedPledgeId);
    if (connection.ExecutePrepared(getBidStatement) && connection.Fetch() && previousPrice > 0) {
        if (requestedPrice > previousPrice) {
            amountToDebit = requestedPrice - previousPrice;
        } else {
            amountToDebit = 0;
            status = 1;
        }
    }
    connection.UnBind();

    if (amountToDebit > 0) {
        CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
        if (user) {
            const int actualPledgeId = runtime::Int32FromBits(user->GetPledgeID());
            CPledgeSP pledge = g_pledgeDb.GetPledge(actualPledgeId, false, runtime::process::CurrentDatabaseId());
            if (pledge) {
                CWareHouseSP warehouse = pledge->GetWareHouse();
                if (warehouse) {
                    {
                        L2_WRITE_GUARD(*warehouse.operator->());
                        adenaDeleted = warehouse->DelAdena(amountToDebit);
                    }

                    if (adenaDeleted) {
                        ExecuteInsertAgitAdena(connection, agitId, actualPledgeId, auctionId, 3, amountToDebit);
                        connection.UnBind();
                        connection.Bind(&auctionId);
                        connection.Bind(&bidId);
                        connection.Bind(&amountToDebit);
                        if (ExecuteCreateAgitBid(
                                connection, agitId, auctionId, requestedPrice, requestPledgeId, attendTime
                            )) {
                            connection.Fetch();
                        }
                    } else {
                        status |= 2;
                    }
                }
            }

            WriteCreateAgitBidAudit(*user.get(), auctionId, agitId, bidId, requestedPrice, requestPledgeId, adenaDeleted);
        }
    }

    socket->Send(
        "cddddQddd", 0x82, requestContext, bidId, agitId, auctionId, requestedPrice, requestPledgeId, adenaDeleted ? 1 : 0,
        status
    );
    return false;
}

bool RequestSetAgitAuction(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004925e0. Paired current L2Server
    // CDB::RequestSetAgitAuction 0x0064c2ac and ReplySetAgitAuction
    // 0x006a415c fix both wire directions.
    L2_GUARD_FUNCTION();

    int agitId = 0;
    int auctionId = 0;
    ::Disassemble(packet, "dd", &agitId, &auctionId);

    // Every operation in the target uses a 32-bit register. Its signed
    // remainder rounds (now + one week) to the nearest hour, with an exact
    // half-hour rounded down, and all additions keep two's-complement wrap.
    const std::int32_t now = runtime::crt::Time32Now();
    const std::uint32_t shiftedBits = static_cast<std::uint32_t>(now) + 604800U;
    const std::int32_t shifted = runtime::Int32FromBits(shiftedBits);
    const std::int32_t remainder = shifted % 3600;
    const std::int32_t roundedHour = remainder > 1800 ? 3600 : 0;
    const int nextCost =
        runtime::Int32FromBits(shiftedBits + static_cast<std::uint32_t>(roundedHour) - static_cast<std::uint32_t>(remainder));

    int attendId = 0;
    long long maxPrice = 0;
    int winnerPledgeId = 0;
    int tax = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&attendId);
    connection.Bind(&maxPrice);
    connection.Bind(&winnerPledgeId);
    connection.Bind(&tax);
    constexpr const wchar_t* setAuctionStatement = L"{Call lin_SetAgitAuction (?, ?, ? )}";
    connection.Prepare(setAuctionStatement);
    connection.BindParameter(&agitId);
    connection.BindParameter(&auctionId);
    connection.BindParameter(&nextCost);
    if (connection.ExecutePrepared(setAuctionStatement)) {
        connection.Fetch();

        if (winnerPledgeId > 0) {
            connection.UnBind();
            long long loggedAdena = 0;
            int ownerPledgeId = 0;
            connection.Bind(&loggedAdena);
            connection.Bind(&ownerPledgeId);
            const int adenaReason = 1;
            constexpr const wchar_t* getAdenaStatement = L"{Call lin_GetAgitAdena (?, ?, ? )}";
            connection.Prepare(getAdenaStatement);
            connection.BindParameter(&agitId);
            connection.BindParameter(&auctionId);
            connection.BindParameter(&adenaReason);
            if (connection.ExecutePrepared(getAdenaStatement)) {
                connection.Fetch();
            }

            if (loggedAdena > 0 && ownerPledgeId > 0) {
                CPledgeSP ownerPledge = g_pledgeDb.GetPledge(ownerPledgeId, false, runtime::process::CurrentDatabaseId());
                if (ownerPledge) {
                    CWareHouseSP ownerWarehouse = ownerPledge->GetWareHouse();
                    if (ownerWarehouse) {
                        {
                            L2_WRITE_GUARD(*ownerWarehouse.operator->());
                            ownerWarehouse->AddAdena(loggedAdena);
                        }
                    }
                }
            }

            // Current 0x0049298f tests maxPrice, not attendId. The older C1
            // source used attendId here, so it is not authoritative for this
            // branch in the current revision.
            if (maxPrice > 0) {
                connection.UnBind();
                int rowAttendId = 0;
                long long rowPrice = 0;
                int rowPledgeId = 0;
                int rowAttendTime = 0;
                std::int64_t indicator = -1;
                connection.BindNullable(&rowAttendId, &indicator);
                connection.BindNullable(&rowPrice, &indicator);
                connection.BindNullable(&rowPledgeId, &indicator);
                connection.BindNullable(&rowAttendTime, &indicator);
                constexpr const wchar_t* getBidStatement = L"{Call lin_GetAgitBid (?)}";
                connection.Prepare(getBidStatement);
                connection.BindParameter(&auctionId);
                if (connection.ExecutePrepared(getBidStatement)) {
                    while (connection.Fetch()) {
                        if (winnerPledgeId == rowPledgeId) {
                            const std::int32_t percentage = runtime::SubtractInt32Wrapping(100, tax);
                            const long long refund =
                                runtime::Win64CvttDoubleToInt64(
                                    (static_cast<double>(percentage) / 100.0) *
                                    static_cast<double>(rowPrice));
                            CPledgeSP ownerPledge =
                                g_pledgeDb.GetPledge(ownerPledgeId, false, runtime::process::CurrentDatabaseId());
                            if (ownerPledge) {
                                CWareHouseSP ownerWarehouse = ownerPledge->GetWareHouse();
                                if (ownerWarehouse) {
                                    {
                                        L2_WRITE_GUARD(*ownerWarehouse.operator->());
                                        ownerWarehouse->AddAdena(refund);
                                    }
                                }
                            }
                        } else {
                            CPledgeSP bidderPledge =
                                g_pledgeDb.GetPledge(rowPledgeId, false, runtime::process::CurrentDatabaseId());
                            if (bidderPledge) {
                                CWareHouseSP bidderWarehouse = bidderPledge->GetWareHouse();
                                if (bidderWarehouse) {
                                    {
                                        L2_WRITE_GUARD(*bidderWarehouse.operator->());
                                        bidderWarehouse->AddAdena(rowPrice);
                                    }
                                }
                            }
                        }

                        // The current loop resets only these two bound
                        // values before its next Fetch call.
                        rowPrice = 0;
                        rowPledgeId = 0;
                    }
                }
            }
        }
    }

    CPledgeSP winnerPledge = g_pledgeDb.GetPledge(winnerPledgeId, false, runtime::process::CurrentDatabaseId());
    WriteSetAgitAuctionAudit(
        winnerPledge ? winnerPledge->GetName() : u"", agitId, auctionId, attendId, maxPrice, winnerPledgeId, tax
    );

    socket->Send("cdddQdd", 0x83, agitId, auctionId, attendId, maxPrice, winnerPledgeId, nextCost);
    return false;
}

bool RequestLoadCastleGuard(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00492e50. Paired current L2Server
    // CDB::RequestLoadGuardData 0x00642cb4 and ReplyLoadCastleGuard
    // 0x006c2464 fix both wire directions.
    L2_GUARD_FUNCTION();

    int residenceId = 0;
    ::Disassemble(packet, "d", &residenceId);

    int itemId = 0;
    int npcId = 0;
    int guardType = 0;
    int canMove = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    int angle = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&itemId);
    connection.Bind(&npcId);
    connection.Bind(&guardType);
    connection.Bind(&canMove);
    connection.Bind(&x);
    connection.Bind(&y);
    connection.Bind(&z);
    connection.Bind(&angle);

    constexpr const wchar_t* statement = L"{Call lin_LoadResidenceGuard (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&residenceId);
    if (!connection.ExecutePrepared(statement)) {
        return false;
    }

    std::array<char, 0x5000> rows{};
    int rowBytes = 0;
    int rowCount = 0;
    while (connection.Fetch()) {
        // The target increments rowCount even when Assemble reports a full
        // buffer and returns zero. Keep the raw-buffer path instead of the
        // packet-helper successful-record counter for that exact anomaly.
        rowBytes += ::Assemble(
            rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dhhddddd", itemId, guardType, canMove, x, y,
            z, angle, npcId
        );
        ++rowCount;
    }

    if (rowCount != 0) {
        socket->Send("cddb", 0x84, residenceId, rowCount, rowBytes, rows.data());
    } else {
        socket->Send("cdd", 0x84, residenceId, 0);
    }
    return false;
}

bool RequestLoadAgitBid(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00493110. Paired current L2Server
    // CDB::RequestLoadAgitBid 0x0064c378 and ReplyLoadAgitBid 0x006afc1c
    // fix the request, row and reply codecs.
    L2_GUARD_FUNCTION();

    int requestContext = 0;
    int auctionId = 0;
    ::Disassemble(packet, "dd", &requestContext, &auctionId);

    int attendId = 0;
    long long attendPrice = 0;
    int attendPledgeId = 0;
    int attendTime = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&attendId);
    connection.Bind(&attendPrice);
    connection.Bind(&attendPledgeId);
    connection.Bind(&attendTime);
    constexpr const wchar_t* statement = L"{Call lin_GetAgitBid (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&auctionId);

    std::array<char, 0x5000> rows{};
    int rowBytes = 0;
    int rowCount = 0;
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            ++rowCount;
            const int written = ::Assemble(
                rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dQdd", attendId, attendPrice,
                attendPledgeId, attendTime
            );
            if (written > 0) {
                rowBytes += written;
            }

            // The current loop clears every bound value after
            // each assembled row, including the signed 64-bit price.
            attendId = 0;
            attendPrice = 0;
            attendPledgeId = 0;
            attendTime = 0;
        }
    }

    // Unlike opcode 152, this reply is unconditional after SQL failure and
    // carries the original request context as well as the auction ID.
    socket->Send("cdddb", 0x85, requestContext, auctionId, rowCount, rowBytes, rows.data());
    return false;
}

bool RequestLoadControlTower(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00493388. Current L2Server
    // CDB::RequestLoadAllControlTower 0x00666178 and
    // ReplyLoadControlTower 0x006d36cc fix the two wire directions.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 128> targetName{};
    int defaultResidenceId = 0;
    int defaultControlLevel = 0;
    int defaultHp = 0;
    int defaultStatus = 0;
    ::Disassemble(
        packet, "Sdddd", 0x100, targetName.data(), &defaultResidenceId, &defaultControlLevel, &defaultHp, &defaultStatus
    );
    const std::u16string databaseName(FixedUtf16View(targetName));

    int residenceId = 0;
    int controlLevel = 0;
    int hp = 0;
    int status = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&residenceId);
    connection.Bind(&controlLevel);
    connection.Bind(&hp);
    connection.Bind(&status);

    constexpr const wchar_t* loadStatement = L"{Call lin_LoadControlTowerByName (?)}";
    connection.Prepare(loadStatement);
    connection.BindParameter(databaseName.c_str());
    if (!connection.ExecutePrepared(loadStatement) || !connection.Fetch()) {
        connection.UnBind();
        constexpr const wchar_t* insertStatement = L"{Call lin_InsertIntoControlTower (?, ?, ?, ?, ?)}";
        connection.Prepare(insertStatement);
        connection.BindParameter(databaseName.c_str());
        connection.BindParameter(&defaultResidenceId);
        connection.BindParameter(&defaultControlLevel);
        connection.BindParameter(&defaultHp);
        connection.BindParameter(&defaultStatus);
        if (!connection.ExecutePrepared(insertStatement)) {
            // Log.Add(
            //     LOG_ERROR,
            //     "Error occurred in Execute "
            //     "lin_InsertIntoControlTower [%s]",
            //     WideToUtf8(databaseName).c_str()
            // )
            // L2_LOG_ERROR(Log,
            //     "Error occurred in Execute "
            //     "lin_InsertIntoControlTower [%s]",
            //     WideToUtf8(databaseName).c_str()
            // )
            // L2_LOG_ERROR(Log, "[siege] Cannot create control tower '%s': database operation failed. Reply uses defaults: residence %d, control level %d, HP %d, status %d.",
            //     WideToUtf8(databaseName).c_str(), defaultResidenceId, defaultControlLevel, defaultHp, defaultStatus)
            L2_LOG_ERROR(Log, "[siege] Cannot create control tower '%s': database operation failed. Reply uses defaults: residence %d, control level %d, HP %d, status %d.",
                Utf16ToUtf8(databaseName).c_str(), defaultResidenceId, defaultControlLevel, defaultHp, defaultStatus);
        }

        residenceId = defaultResidenceId;
        controlLevel = defaultControlLevel;
        hp = defaultHp;
        status = defaultStatus;
    }

    socket->Send("cSdddd", 0x86, targetName.data(), residenceId, controlLevel, hp, status);
    return false;
}

bool RequestSaveControlTower(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x00493644. Current L2Server
    // CDB::RequestSaveControlTowerStatus 0x0064c7cc sends Sddd.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 128> targetName{};
    int controlLevel = 0;
    int hp = 0;
    int status = 0;
    ::Disassemble(packet, "Sddd", 0x100, targetName.data(), &controlLevel, &hp, &status);
    const std::u16string databaseName(FixedUtf16View(targetName));

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateControlTower (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&controlLevel);
    connection.BindParameter(&hp);
    connection.BindParameter(&status);
    connection.BindParameter(databaseName.c_str());
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR, "Error occurred in Execute lin_UpdateControlTower [%s]", WideToUtf8(databaseName).c_str()
        // )
        // L2_LOG_ERROR(Log, "Error occurred in Execute lin_UpdateControlTower [%s]", WideToUtf8(databaseName).c_str()
        // )
        // L2_LOG_ERROR(Log, "[siege] Cannot save control tower '%s' (control level %d, HP %d, status %d): database operation failed.",
        //     WideToUtf8(databaseName).c_str(), controlLevel, hp, status)
        L2_LOG_ERROR(Log, "[siege] Cannot save control tower '%s' (control level %d, HP %d, status %d): database operation failed.",
            Utf16ToUtf8(databaseName).c_str(), controlLevel, hp, status);
    }
    return false;
}

bool RequestSaveMercenary(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0049382c. Current L2Server
    // CDB::RequestSaveMercenary 0x00642a04 fixes the nine-dword request.
    // the obsolete C1 tenth field and delete-all branch are not imported.
    L2_GUARD_FUNCTION();

    int requestContext = 0;
    int residenceId = 0;
    int npcId = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    int angle = 0;
    int hp = 0;
    int mp = 0;
    ::Disassemble(packet, "ddddddddd", &requestContext, &residenceId, &npcId, &x, &y, &z, &angle, &hp, &mp);

    int recordId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&recordId);
    constexpr const wchar_t* statement = L"{Call lin_InsertIntoMercenary (?, ?, ?, ?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&residenceId);
    connection.BindParameter(&npcId);
    connection.BindParameter(&x);
    connection.BindParameter(&y);
    connection.BindParameter(&z);
    connection.BindParameter(&angle);
    connection.BindParameter(&hp);
    connection.BindParameter(&mp);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }

    if (recordId == 0) {
        // Log.Add(LOG_ERROR, "Error Occurred in lin_InsertIntoMercenary Castle[%d]", residenceId)
        // L2_LOG_ERROR(Log, "Error Occurred in lin_InsertIntoMercenary Castle[%d]", residenceId)
        L2_LOG_ERROR(Log, "[siege] Mercenary creation produced no record ID for residence %d, NPC %d (request %d). Reply will carry ID 0.",
            residenceId, npcId, requestContext);
    }
    socket->Send("cdd", 0x88, requestContext, recordId);
    return false;
}

bool RequestLoadMercenary(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00493a30. Current L2Server
    // CDB::RequestLoadMercenary 0x00642b34 and ReplyLoadMercenary
    // 0x006b948c fix the request, row and reply codecs.
    L2_GUARD_FUNCTION();

    int residenceId = 0;
    ::Disassemble(packet, "d", &residenceId);

    int mercenaryId = 0;
    int npcId = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    int angle = 0;
    int hp = 0;
    int mp = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&mercenaryId);
    connection.Bind(&npcId);
    connection.Bind(&x);
    connection.Bind(&y);
    connection.Bind(&z);
    connection.Bind(&angle);
    connection.Bind(&hp);
    connection.Bind(&mp);
    constexpr const wchar_t* statement = L"{Call lin_LoadMercenary (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&residenceId);

    std::array<char, 0x5000> rows{};
    int rowBytes = 0;
    int rowCount = 0;
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            rowBytes += ::Assemble(
                rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dddddddd", mercenaryId, x, y, z, angle,
                npcId, hp, mp
            );
            ++rowCount;
        }
    }

    if (rowCount != 0) {
        socket->Send("cddb", 0x87, residenceId, rowCount, rowBytes, rows.data());
    } else {
        socket->Send("cdd", 0x87, residenceId, 0);
    }
    return false;
}

bool RequestDeleteMercenary(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x00493cf0. Paired current L2Server
    // CDB::RequestDeleteMercenary 0x00642bf4 sends the residence ID.
    L2_GUARD_FUNCTION();

    int residenceId = 0;
    ::Disassemble(packet, "d", &residenceId);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeleteMercenary (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&residenceId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "Error Occurred in Delete Mercenary Castle[%d]", residenceId)
        // L2_LOG_ERROR(Log, "Error Occurred in Delete Mercenary Castle[%d]", residenceId)
        L2_LOG_ERROR(Log, "[siege] Cannot delete mercenaries for residence %d: database operation failed.", residenceId);
    }
    return false;
}

bool RequestUpdateMercenary(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x00493e44. Paired current L2Server
    // CDB::RequestUpdateMercenary 0x00663ec4 sends ddddddd.
    L2_GUARD_FUNCTION();

    int mercenaryId = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    int angle = 0;
    int hp = 0;
    int mp = 0;
    ::Disassemble(packet, "ddddddd", &mercenaryId, &x, &y, &z, &angle, &hp, &mp);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateMercenary (?, ?, ?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&mercenaryId);
    connection.BindParameter(&x);
    connection.BindParameter(&y);
    connection.BindParameter(&z);
    connection.BindParameter(&angle);
    connection.BindParameter(&hp);
    connection.BindParameter(&mp);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "Error Occurred in Update Mercenary id[%d]", mercenaryId)
        // L2_LOG_ERROR(Log, "Error Occurred in Update Mercenary id[%d]", mercenaryId)
        L2_LOG_ERROR(Log, "[siege] Cannot update mercenary %d at (%d, %d, %d), angle %d, HP %d, MP %d: database operation failed.",
            mercenaryId, x, y, z, angle, hp, mp);
    }
    return false;
}

bool RequestAgitPledgeAdena(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00493fe8. Paired current L2Server
    // CDB::SendAgitPledgeAdena 0x0064bb54 sends the exact five-dword
    // request and ReplyAgitPledgeAdena 0x006afe18 consumes the reply.
    L2_GUARD_FUNCTION();

    int characterDbId = 0;
    int userServerId = 0;
    int agitId = 0;
    int pledgeId = 0;
    int command = 0;
    ::Disassemble(packet, "ddddd", &characterDbId, &userServerId, &agitId, &pledgeId, &command);
    // The shipping handler decodes but never reads the character DB ID.

    long long pledgeAdena = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&pledgeAdena);
    constexpr const wchar_t* statement = L"{Call lin_GetPledgeAdena (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledgeId);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }

    // Current behavior replies even when Execute or Fetch fails, retaining
    // the zero-initialized signed 64-bit amount in that case.
    socket->Send("cdddQd", 0x89, userServerId, agitId, pledgeId, pledgeAdena, command);
    return false;
}

bool RequestCancelAgitAuction(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004941c4. Current L2Server
    // CDB::RequestCancelAgitAuction 0x0064c444 and
    // ReplyCancelAgitAuction 0x006affb4 fix both wire directions.
    L2_GUARD_FUNCTION();

    int characterDbId = 0;
    int userServerId = 0;
    int agitId = 0;
    int requestContext = 0;
    ::Disassemble(packet, "dddd", &characterDbId, &userServerId, &agitId, &requestContext);

    const int lastCancel = runtime::crt::Time32Now();
    int auctionId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&auctionId);
    constexpr const wchar_t* cancelStatement = L"{Call lin_CancelAgitAuction (?, ? )}";
    connection.Prepare(cancelStatement);
    connection.BindParameter(&agitId);
    connection.BindParameter(&lastCancel);
    if (connection.ExecutePrepared(cancelStatement) && connection.Fetch()) {
        int attendId = 0;
        long long attendPrice = 0;
        int attendPledgeId = 0;
        int attendTime = 0;
        std::int64_t indicator = -1;
        connection.UnBind();
        connection.BindNullable(&attendId, &indicator);
        connection.BindNullable(&attendPrice, &indicator);
        connection.BindNullable(&attendPledgeId, &indicator);
        connection.BindNullable(&attendTime, &indicator);
        constexpr const wchar_t* bidsStatement = L"{Call lin_GetAgitBid (?)}";
        connection.Prepare(bidsStatement);
        connection.BindParameter(&auctionId);
        if (connection.ExecutePrepared(bidsStatement)) {
            while (connection.Fetch()) {
                CPledgeSP pledge = g_pledgeDb.GetPledge(attendPledgeId, false, runtime::process::CurrentDatabaseId());
                if (pledge) {
                    CWareHouseSP warehouse = pledge->GetWareHouse();
                    if (warehouse) {
                        {
                            L2_WRITE_GUARD(*warehouse.operator->());
                            warehouse->AddAdena(attendPrice);
                        }
                    }
                }

                // The current loop clears only these two values. Attend ID
                // and attend time keep their previous row.
                attendPrice = 0;
                attendPledgeId = 0;
            }
        }
    }

    WriteCancelAgitAuctionAudit(agitId, auctionId);
    socket->Send("cdddd", 0x8a, userServerId, agitId, requestContext, lastCancel);
    return false;
}

bool RequestAgitCost(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004945f8. Paired current L2Server
    // CDB::RequestAgitCost 0x0064c530 sends ddQdd and ReplyAgitCost
    // 0x006a42bc consumes the exact ddQddd reply payload.
    L2_GUARD_FUNCTION();

    int agitId = 0;
    int pledgeId = 0;
    long long price = 0;
    int costFail = 0;
    int referenceTime = 0;
    ::Disassemble(packet, "ddQdd", &agitId, &pledgeId, &price, &costFail, &referenceTime);

    bool success = false;
    int nextCost = 0;
    std::u16string pledgeName;

    if (costFail == -1) {
        const int shifted = runtime::AddInt32Wrapping(referenceTime, 604800);
        const int remainder = shifted % 3600;
        nextCost = runtime::AddInt32Wrapping(shifted, runtime::SubtractInt32Wrapping(remainder > 1800 ? 3600 : 0, remainder));
        costFail = 0;

        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_SetAgitNextCost (?, ?, ? )}";
        connection.Prepare(statement);
        connection.BindParameter(&agitId);
        connection.BindParameter(&nextCost);
        connection.BindParameter(&costFail);
        success = connection.ExecutePrepared(statement);
    } else if (agitId > 0 && pledgeId > 0 && price > 0) {
        int unroundedNextCost = 0;
        CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
        if (pledge) {
            CWareHouseSP warehouse = pledge->GetWareHouse();
            if (warehouse) {
                {
                    L2_WRITE_GUARD(*warehouse.operator->());
                    success = warehouse->DelAdena(price);
                }
            }
            pledgeName = pledge->GetName();
        }

        if (!pledge || !success) {
            unroundedNextCost = runtime::AddInt32Wrapping(referenceTime, 86400);
            costFail = runtime::AddInt32Wrapping(costFail, 1);
        } else {
            int boundedCostFail = costFail;
            if (boundedCostFail > 7) {
                boundedCostFail = 7;
            }
            const int daysUntilCost = runtime::SubtractInt32Wrapping(7, boundedCostFail);
            unroundedNextCost = runtime::AddInt32Wrapping(runtime::MultiplyInt32Wrapping(daysUntilCost, 86400), referenceTime);
            costFail = 0;
        }

        const int remainder = unroundedNextCost % 3600;
        nextCost = runtime::AddInt32Wrapping(unroundedNextCost, runtime::SubtractInt32Wrapping(remainder > 1800 ? 3600 : 0, remainder));

        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_SetAgitNextCost (?, ?, ? )}";
        connection.Prepare(statement);
        connection.BindParameter(&agitId);
        connection.BindParameter(&nextCost);
        connection.BindParameter(&costFail);
        connection.ExecutePrepared(statement);
    }

    socket->Send("cddQddd", 0x8b, agitId, pledgeId, price, success ? 1 : 0, nextCost, costFail);
    WriteAgitCostAudit(pledgeName.c_str(), agitId, pledgeId, price, success, nextCost, costFail);
    return false;
}

bool RequestDeleteCastleGuard(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x00494b2c. Paired current L2Server
    // CDB::RequestDeleteGuardData 0x00642d74 sends the residence ID.
    L2_GUARD_FUNCTION();

    int residenceId = 0;
    ::Disassemble(packet, "d", &residenceId);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeleteAllResidenceGuard (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&residenceId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "Error Occurred in Delete All CastleGuard id[%d]", residenceId)
        // L2_LOG_ERROR(Log, "Error Occurred in Delete All CastleGuard id[%d]", residenceId)
        L2_LOG_ERROR(Log, "[siege] Cannot delete all guards for residence %d: database operation failed.", residenceId);
    }
    return false;
}

bool RequestAgitAutoSell(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00494c80. Paired current L2Server
    // CDB::RequestAgitAutoSell 0x0064c618 and ReplyAgitAutoSell
    // 0x006a4464 fix both wire directions and the signed 64-bit price.
    L2_GUARD_FUNCTION();

    int agitId = 0;
    long long minimumPrice = 0;
    int auctionTax = 0;
    int auctionDurationDays = 0;
    int unusedRequestValue = 0;
    ::Disassemble(packet, "dQddd", &agitId, &minimumPrice, &auctionTax, &auctionDurationDays, &unusedRequestValue);
    // The shipping handler decodes but never reads the fifth payload value.

    const int unroundedAuctionTime =
        runtime::AddInt32Wrapping(runtime::MultiplyInt32Wrapping(auctionDurationDays, 86400), runtime::crt::Time32Now());
    const int auctionTime = runtime::MultiplyInt32Wrapping(runtime::AddInt32Wrapping(unroundedAuctionTime / 3600, 1), 3600);

    int auctionId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&auctionId);
    constexpr const wchar_t* statement = L"{Call lin_CreateAgitAuction (?, ?, ?, ?, ? )}";
    connection.Prepare(statement);
    connection.BindParameter(&agitId);
    connection.BindParameter(L"");
    connection.BindParameter(&minimumPrice);
    connection.BindParameter(&auctionTime);
    connection.BindParameter(&auctionTax);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }

    socket->Send("cddQdd", 0x8f, auctionId, agitId, minimumPrice, auctionDurationDays, auctionTime);
    WriteAgitAutoSellAudit(agitId, auctionId, minimumPrice, auctionDurationDays, auctionTime);
    return false;
}

bool RequestAgitBidCancel(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00494ed4. Paired current L2Server
    // CDB::RequestAgitBidCancelRequest 0x0064bf88 and ReplyAgitBidCancel
    // 0x006b0770 fix the six-dword request and bigint reply fields.
    L2_GUARD_FUNCTION();

    int characterDbId = 0;
    int userServerId = 0;
    int agitId = 0;
    int auctionId = 0;
    int pledgeId = 0;
    int tax = 0;
    ::Disassemble(packet, "dddddd", &characterDbId, &userServerId, &agitId, &auctionId, &pledgeId, &tax);
    // As in the shipping handler, identity field one is consumed but not read.

    std::uint32_t ownerPledgeId = 0;
    int hpStove = 0;
    int hpStoveExpire = 0;
    int mpFlame = 0;
    int mpFlameExpire = 0;
    int hatcher = 0;
    int status = 0;
    int nextWarTime = 0;
    int currentAuctionId = 0;
    int auctionTime = 0;
    long long lastPrice = 0;
    int lastCancel = 0;
    long long minimumPrice = 0;
    int teleportLevel = 0;
    int teleportExpire = 0;
    std::array<char16_t, 256> auctionDescription{};
    int nextCost = 0;
    int costFail = 0;
    int taxRate = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&ownerPledgeId);
    connection.Bind(&hpStove);
    connection.Bind(&hpStoveExpire);
    connection.Bind(&mpFlame);
    connection.Bind(&mpFlameExpire);
    connection.Bind(&hatcher);
    connection.Bind(&status);
    connection.Bind(&nextWarTime);
    connection.Bind(&currentAuctionId);
    connection.Bind(&auctionTime);
    connection.Bind(&lastPrice);
    connection.Bind(&lastCancel);
    connection.Bind(&minimumPrice);
    connection.Bind(&teleportLevel);
    connection.Bind(&teleportExpire);
    connection.Bind(auctionDescription.data(), 0x200);
    connection.Bind(&nextCost);
    connection.Bind(&costFail);
    connection.Bind(&taxRate);
    constexpr const wchar_t* loadStatement = L"{Call lin_LoadAgit (?)}";
    connection.Prepare(loadStatement);
    connection.BindParameter(&agitId);
    if (connection.ExecutePrepared(loadStatement)) {
        connection.Fetch();
    }

    long long price = 0;
    long long refund = 0;
    bool adenaAdded = false;
    std::u16string pledgeName;
    if (currentAuctionId > 0 && currentAuctionId == auctionId) {
        connection.UnBind();
        connection.Bind(&agitId);
        connection.Bind(&pledgeId);
        connection.Bind(&auctionId);
        connection.Bind(&price);
        constexpr const wchar_t* deleteStatement = L"{Call lin_DeleteAgitBid (?, ?)}";
        connection.Prepare(deleteStatement);
        connection.BindParameter(&agitId);
        connection.BindParameter(&pledgeId);
        if (connection.ExecutePrepared(deleteStatement) && connection.Fetch()) {
            connection.UnBind();
            const int refundPercentage = runtime::SubtractInt32Wrapping(100, tax);
            refund = runtime::Win64CvttDoubleToInt64(
                (static_cast<double>(refundPercentage) / 100.0) *
                static_cast<double>(price));

            CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
            if (pledge) {
                pledgeName = pledge->GetName();
                CWareHouseSP warehouse = pledge->GetWareHouse();
                if (warehouse) {
                    {
                        L2_WRITE_GUARD(*warehouse.operator->());
                        warehouse->AddAdena(refund);
                    }
                    // Current success means that a warehouse existed. The
                    // return value of AddAdena is intentionally ignored.
                    adenaAdded = true;
                }
            }
        }
    }

    socket->Send("cddddQQd", 0x90, userServerId, agitId, pledgeId, tax, price, refund, adenaAdded ? 1 : 0);
    WriteAgitBidCancelAudit(pledgeName.c_str(), agitId, pledgeId, tax, price, refund);
    return false;
}

bool RequestEndSiege(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x0049c010. Paired current L2Server
    // CDB::RequestEndSiege 0x0064c8b8 sends residence ID, winning pledge ID,
    // the siege value and the winning pledge's world ID in that order.
    L2_GUARD_FUNCTION();

    int residenceId = 0;
    int winnerPledgeId = 0;
    int siegeValue = 0;
    int worldId = 0;
    ::Disassemble(packet, "dddd", &residenceId, &winnerPledgeId, &siegeValue, &worldId);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeleteMasterRelatedCastleWar (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&winnerPledgeId);
    connection.BindParameter(&worldId);
    // The current handler ignores the execution result.
    connection.ExecutePrepared(statement);

    CPledgeSP pledge = g_pledgeDb.GetPledge(winnerPledgeId, false, worldId);
    if (!pledge)
        return false;

    const int allianceId = runtime::Int32FromBits(pledge->allianceId());
    CAllianceSP alliance = g_allianceDb.GetAlliance(pledge->allianceId(), worldId);
    if (!alliance) {
        WriteEndSiegeWinnerAudit(
            pledge->GetName(), u"", allianceId, residenceId, runtime::Int32FromBits(pledge->GetId()), siegeValue
        );
        return false;
    }

    PledgeIdList memberPledgeIds;
    alliance->GetAllMemberPledgeId(memberPledgeIds);
    for (const std::uint32_t memberPledgeId : memberPledgeIds) {
        CPledgeSP memberPledge = g_pledgeDb.GetPledge(runtime::Int32FromBits(memberPledgeId), false, worldId);
        if (!memberPledge)
            continue;
        WriteEndSiegeAllianceMemberAudit(
            memberPledge->GetName(), alliance->name(), allianceId, residenceId, runtime::Int32FromBits(memberPledge->GetId())
        );
    }

    WriteEndSiegeWinnerAudit(
        pledge->GetName(), alliance->name(), allianceId, residenceId, runtime::Int32FromBits(pledge->GetId()), siegeValue
    );
    return false;
}

bool RequestPetiSaveMsgForce(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004954fc and current L2Server
    // CDB::RequestSavePetitionMsgForceGetDBID 0x0064c99c fix both SdS input
    // and cdSdS reply, including the -1 lookup-failure sentinel.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 50> characterName{};
    int requestValue = 0;
    std::array<char16_t, 501> petitionMessage{};
    ::Disassemble(packet, "SdS", 100, characterName.data(), &requestValue, 0x3ea, petitionMessage.data());

    int characterId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&characterId);
    constexpr const wchar_t* statement = L"{Call lin_GetDBIDByCharName (?)}";
    connection.Prepare(statement);
    const std::u16string databaseCharacterName(FixedUtf16View(characterName));
    connection.BindParameter(databaseCharacterName.c_str());
    if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
        characterId = -1;
    }

    socket->Send("cdSdS", 0x93, characterId, characterName.data(), requestValue, petitionMessage.data());
    return false;
}

bool RequestLoadLastResidenceTaxUpdate(CSocket* socket, const std::uint8_t* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0045be1c has no request payload. It binds all 18
    // date components returned by lin_LoadLastTaxUpdate and replies with the
    // three signed _time32_t values consumed by current L2Server.
    L2_GUARD_FUNCTION();

    std::array<std::array<int, 6>, 3> components{{
        {{100, 1, 1, 0, 0, 0}},
        {{100, 1, 1, 0, 0, 0}},
        {{100, 1, 1, 0, 0, 0}},
    }};
    DBConn connection(runtime::process::CurrentDatabaseId());
    for (auto& date : components) {
        for (int& component : date) {
            connection.Bind(&component);
        }
    }

    constexpr const wchar_t* statement = L"{Call lin_LoadLastTaxUpdate}";
    connection.Prepare(statement);
    const bool loaded = connection.ExecutePrepared(statement) && connection.Fetch();

    std::array<std::int32_t, 3> timestamps{};
    for (std::size_t index = 0; index < components.size(); ++index) {
        const auto& value = components[index];
        // On failure the target passes the raw database-style
        // year/month defaults as tm_year/tm_mon instead of subtracting
        // 1900/1. Preserve that distinct branch.
        timestamps[index] = TargetMktime32(value[0], value[1], value[2], value[3], value[4], value[5], loaded);
    }

    socket->Send("cddd", 0x95, timestamps[0], timestamps[1], timestamps[2]);
    return false;
}

bool RequestSetLastResidenceTaxUpdate(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x004956d4 and current L2Server
    // CDB::RequestSaveLastTaxChange 0x0064cb28 use three signed time32 values.
    L2_GUARD_FUNCTION();

    std::array<std::int32_t, 3> timestamps{};
    ::Disassemble(packet, "ddd", &timestamps[0], &timestamps[1], &timestamps[2]);

    std::array<std::array<wchar_t, 20>, 3> databaseTimes{};
    bool converted = true;
    for (std::size_t index = 0; index < timestamps.size(); ++index) {
        std::tm local{};
        if (!runtime::crt::LocalTime32(
                timestamps[index],
                local)) {
            converted = false;
            break;
        }
        std::swprintf(
            databaseTimes[index].data(), databaseTimes[index].size(), L"%d-%d-%d %d:%d:%d", local.tm_year + 1900,
            local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec
        );
    }

    bool saved = false;
    if (converted) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_SetLastTaxUpdate (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(databaseTimes[0].data());
        connection.BindParameter(databaseTimes[1].data());
        connection.BindParameter(databaseTimes[2].data());
        saved = connection.ExecutePrepared(statement);
    }
    if (!saved) {
        // Log.Add(LOG_ERROR, "[%s][%d] cannot update last tax change date.", "CacheSocket.cpp", 0x2bba)
        // L2_LOG_ERROR(Log, "[%s][%d] cannot update last tax change date.", "CacheSocket.cpp", 0x2bba)
        L2_LOG_ERROR(Log, "[residence] Cannot save last tax-update times [%d, %d, %d]: %s. No reply is sent.",
            static_cast<int>(timestamps[0]), static_cast<int>(timestamps[1]), static_cast<int>(timestamps[2]),
            converted ? "database execution failed" : "local-time conversion failed; database update was not attempted");
    }
    return false;
}

bool RequestMngComment(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004959f0 and current L2Server
    // CDB::RequestMngComment 0x0064cc08 fix the complete request/reply ABI.
    L2_GUARD_FUNCTION();

    int writeComment = 0;
    int writerCharacterId = 0;
    int requestValue = 0;
    std::array<char16_t, 25> requestName{};
    int targetCharacterId = 0;
    std::array<char16_t, 256> comment{};
    ::Disassemble(
        packet, "dddSdS", &writeComment, &writerCharacterId, &requestValue, 0x32, requestName.data(), &targetCharacterId,
        0x200, comment.data()
    );
    // The third packet string is decoded by the current handler but never
    // read after the two CUser lookups.

    const int worldId = runtime::process::CurrentDatabaseId();
    CUserSP writer = g_userDb.GetUser(writerCharacterId, true, false, worldId);
    CUserSP target = g_userDb.GetUser(targetCharacterId, true, false, worldId);

    int commentCount = 0;
    int commentBytes = 0;
    std::array<char, 0x800> comments{};
    if (writer && target) {
        DBConn connection(worldId);
        if (writeComment == 1) {
            const std::u16string targetName(target->GetCharName());
            const std::u16string writerName(writer->GetCharName());
            const std::u16string databaseComment(FixedUtf16View(comment));
            const int loadedTargetId = static_cast<int>(target->GetId());
            constexpr const wchar_t* writeStatement = L"{Call lin_CommentWrite (?, ?, ?, ? )}";
            connection.Prepare(writeStatement);
            connection.BindParameter(targetName.c_str());
            connection.BindParameter(&loadedTargetId);
            connection.BindParameter(databaseComment.c_str());
            connection.BindParameter(writerName.c_str());
            connection.ExecutePrepared(writeStatement);
            connection.UnBind();
        }

        int commentId = 0;
        std::array<char16_t, 25> databaseCharacterName{};
        int databaseCharacterId = 0;
        std::array<char16_t, 256> databaseComment{};
        std::array<char16_t, 25> databaseWriterName{};
        std::int64_t nullableIndicator = -1;
        connection.BindNullable(&commentId, &nullableIndicator);
        connection.BindNullable(databaseCharacterName.data(), 0x32, &nullableIndicator);
        connection.BindNullable(&databaseCharacterId, &nullableIndicator);
        connection.BindNullable(databaseComment.data(), 0x200, &nullableIndicator);
        connection.BindNullable(databaseWriterName.data(), 0x32, &nullableIndicator);

        const int loadedTargetId = static_cast<int>(target->GetId());
        int page = 1;
        constexpr const wchar_t* searchStatement = L"{Call lin_CommentSearch (?, ? )}";
        connection.Prepare(searchStatement);
        connection.BindParameter(&loadedTargetId);
        connection.BindParameter(&page);
        if (connection.ExecutePrepared(searchStatement)) {
            while (connection.Fetch()) {
                if (commentCount > 3)
                    break;
                ++commentCount;
                const std::u16string packetComment = std::u16string(databaseComment.data());
                const std::u16string packetWriter = std::u16string(databaseWriterName.data());
                const int assembled = ::Assemble(
                    comments.data() + commentBytes, static_cast<int>(comments.size()) - commentBytes, "SS",
                    packetComment.c_str(), packetWriter.c_str()
                );
                if (assembled > 0) {
                    commentBytes += assembled;
                }
                databaseComment.fill(u'\0');
                databaseWriterName.fill(u'\0');
            }
        }
    }

    // A missing target reaches a null smart-pointer dereference in the
    // shipping exceptional path. Keep the normal reply transport alive with
    // an empty name instead of crashing this portable service.
    const std::u16string targetName = target ? std::u16string(target->GetCharName()) : std::u16string{};
    socket->Send(
        "cddddSb", 0x96, writeComment, requestValue, writerCharacterId, commentCount, targetName.c_str(), commentBytes,
        comments.data()
    );
    return false;
}

bool RequestLoadPlayTime(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00475494 and ReplyLoadDayPlayTime in current
    // L2Server fix the ddb response, including its raw 16-byte SYSTEMTIME.
    L2_GUARD_FUNCTION();

    unsigned int accountId = 0;
    ::Disassemble(packet, "d", &accountId);
    CAccountSP account = CAccount::Load(accountId);
    if (!account) {
        const SYSTEMTIME emptyTime{};
        socket->Send("cddb", 0x97, accountId, 0, static_cast<int>(sizeof(emptyTime)), &emptyTime);
        return false;
    }

    int weekPlayTime;
    SYSTEMTIME lastChanged;
    {
        L2_READ_GUARD(*account.operator->());
        weekPlayTime = account->GetWeekPlayTime();
        lastChanged = account->GetWeekPlayTimeLastChanged();
    }
    socket->Send("cddb", 0x97, accountId, weekPlayTime, static_cast<int>(sizeof(lastChanged)), &lastChanged);
    return false;
}

bool RequestBookMark(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00495fe8. The five command branches, paging
    // boundary and reply suppression rules below are all visible in the
    // shipping control flow.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int userServerId = 0;
    int command = 0;
    const std::uint8_t* commandPacket = ::Disassemble(packet, "ddd", &characterId, &userServerId, &command);

    if (command == 1) {
        int bookmarkId = 0;
        std::array<char16_t, 51> targetName{};
        int x = 0;
        int y = 0;
        int z = 0;
        ::Disassemble(commandPacket, "dSddd", &bookmarkId, 0x66, targetName.data(), &x, &y, &z);

        const int option = 1;
        const std::u16string name(FixedUtf16View(targetName));
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_ManBookMark (?, ?, ?, ?, ?, ?, ? )}";
        connection.Prepare(statement);
        connection.BindParameter(&option);
        connection.BindParameter(&characterId);
        connection.BindParameter(&bookmarkId);
        connection.BindParameter(name.c_str());
        connection.BindParameter(&x);
        connection.BindParameter(&y);
        connection.BindParameter(&z);
        connection.ExecutePrepared(statement);

        socket->Send("cddd", 0x98, characterId, userServerId, command);
        return false;
    }

    if (command == 2) {
        int page = 0;
        ::Disassemble(commandPacket, "d", &page);
        if (page < 1)
            page = 1;

        int bookmarkId = 0;
        std::array<char16_t, 51> name{};
        int x = 0;
        int y = 0;
        int z = 0;
        std::int64_t nullableIndicator = -1;
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.BindNullable(&bookmarkId, &nullableIndicator);
        connection.BindNullable(name.data(), 0x66, &nullableIndicator);
        connection.BindNullable(&x, &nullableIndicator);
        connection.BindNullable(&y, &nullableIndicator);
        connection.BindNullable(&z, &nullableIndicator);

        const int option = 2;
        const int emptyBookmarkId = 0;
        const int emptyCoordinate = 0;
        constexpr const wchar_t* emptyName = L"";
        constexpr const wchar_t* statement = L"{Call lin_ManBookMark (?, ?, ?, ?, ?, ?, ? )}";
        connection.Prepare(statement);
        connection.BindParameter(&option);
        connection.BindParameter(&characterId);
        connection.BindParameter(&emptyBookmarkId);
        connection.BindParameter(emptyName);
        connection.BindParameter(&emptyCoordinate);
        connection.BindParameter(&emptyCoordinate);
        connection.BindParameter(&emptyCoordinate);

        CPacketHelperEx<20480> bookmarks;
        int sourceIndex = 0;
        int bookmarkCount = 0;
        if (connection.ExecutePrepared(statement)) {
            while (bookmarkCount < 12 && connection.Fetch()) {
                const int pageBegin = (page - 1) * 12;
                const int pageEnd = page * 12;
                if (sourceIndex >= pageBegin && sourceIndex < pageEnd) {
                    bookmarks.Assemble("dSddd", bookmarkId, name.data(), x, y, z);
                    ++bookmarkCount;
                }
                ++sourceIndex;
            }
        }

        socket->Send(
            "cdddddb", 0x98, characterId, userServerId, command, page, bookmarkCount, bookmarks.GetLength(),
            bookmarks.GetPacket()
        );
        return false;
    }

    if (command == 3) {
        int bookmarkId = 0;
        ::Disassemble(commandPacket, "d", &bookmarkId);

        std::array<char16_t, 51> name{};
        int x = 0;
        int y = 0;
        int z = 0;
        std::int64_t nullableIndicator = -1;
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.BindNullable(name.data(), 0x66, &nullableIndicator);
        connection.BindNullable(&x, &nullableIndicator);
        connection.BindNullable(&y, &nullableIndicator);
        connection.BindNullable(&z, &nullableIndicator);

        const int option = 3;
        const int emptyCoordinate = 0;
        constexpr const wchar_t* emptyName = L"";
        constexpr const wchar_t* statement = L"{Call lin_ManBookMark (?, ?, ?, ?, ?, ?, ? )}";
        connection.Prepare(statement);
        connection.BindParameter(&option);
        connection.BindParameter(&characterId);
        connection.BindParameter(&bookmarkId);
        connection.BindParameter(emptyName);
        connection.BindParameter(&emptyCoordinate);
        connection.BindParameter(&emptyCoordinate);
        connection.BindParameter(&emptyCoordinate);
        if (connection.ExecutePrepared(statement) && connection.Fetch()) {
            socket->Send("cdddddd", 0x98, characterId, userServerId, command, x, y, z);
        }
        return false;
    }

    if (command == 4) {
        int bookmarkId = 0;
        ::Disassemble(commandPacket, "d", &bookmarkId);

        const int option = 4;
        const int emptyCoordinate = 0;
        constexpr const wchar_t* emptyName = L"";
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_ManBookMark (?, ?, ?, ?, ?, ?, ? )}";
        connection.Prepare(statement);
        connection.BindParameter(&option);
        connection.BindParameter(&characterId);
        connection.BindParameter(&bookmarkId);
        connection.BindParameter(emptyName);
        connection.BindParameter(&emptyCoordinate);
        connection.BindParameter(&emptyCoordinate);
        connection.BindParameter(&emptyCoordinate);
        connection.ExecutePrepared(statement);

        socket->Send("cddd", 0x98, characterId, userServerId, command);
        return false;
    }

    if (command == 5) {
        std::array<char16_t, 51> targetName{};
        ::Disassemble(commandPacket, "S", 0x66, targetName.data());

        int worldId = 0;
        int x = 0;
        int y = 0;
        int z = 0;
        const std::u16string name(FixedUtf16View(targetName));
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Bind(&worldId);
        connection.Bind(&x);
        connection.Bind(&y);
        connection.Bind(&z);
        constexpr const wchar_t* statement = L"{Call lin_GetBookMark (?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.BindParameter(name.c_str());
        if (connection.ExecutePrepared(statement) && connection.Fetch()) {
            // The fetched world id is not part of the current
            // command-5 reply.
            socket->Send("cdddddd", 0x98, characterId, userServerId, command, x, y, z);
        }
    }
    return false;
}

bool RequestLoadTeamBattleAgit(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00496760.
    L2_GUARD_FUNCTION();

    int agitId = 0;
    ::Disassemble(packet, "d", &agitId);

    int status = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&status);
    constexpr const wchar_t* statusStatement = L"{Call lin_LoadTeamBattleAgitStatus (?)}";
    connection.Prepare(statusStatement);
    connection.BindParameter(&agitId);
    if (!connection.ExecutePrepared(statusStatement) || !connection.Fetch()) {
        // Log.Add(LOG_ERROR, "Error in Getting team battle agit status[%d]", agitId)
        // L2_LOG_ERROR(Log, "Error in Getting team battle agit status[%d]", agitId)
        L2_LOG_ERROR(Log, "[team battle] Cannot load status for hall %d: query failed or no status row fetched. Sending reply without clans or members (status %d).", agitId, status);
        socket->Send("cdddd", 0x9a, agitId, status, 0, 0);
        return false;
    }

    connection.UnBind();
    int pledgeId = 0;
    int pledgeProposeTime = 0;
    int color = 0;
    int npcType = 0;
    connection.Bind(&pledgeId);
    connection.Bind(&pledgeProposeTime);
    connection.Bind(&color);
    connection.Bind(&npcType);
    constexpr const wchar_t* pledgeStatement = L"{Call lin_LoadTeamBattleAgitPledge (?)}";
    connection.Prepare(pledgeStatement);
    connection.BindParameter(&agitId);
    if (!connection.ExecutePrepared(pledgeStatement)) {
        // Log.Add(LOG_ERROR, "Error in Getting team battle agit related pledge[%d]", agitId)
        // L2_LOG_ERROR(Log, "Error in Getting team battle agit related pledge[%d]", agitId)
        L2_LOG_ERROR(Log, "[team battle] Cannot load clans for hall %d: database operation failed. Sending reply without clans or members (status %d).", agitId, status);
        socket->Send("cdddd", 0x9a, agitId, status, 0, 0);
        return false;
    }

    std::array<char, 0x800> pledgePacket{};
    int pledgeBytes = 0;
    int pledgeCount = 0;
    while (connection.Fetch()) {
        const int assembled = ::Assemble(
            pledgePacket.data() + pledgeBytes, static_cast<int>(pledgePacket.size()) - pledgeBytes, "dddd", pledgeId,
            pledgeProposeTime, color, npcType
        );
        pledgeBytes += assembled;
        ++pledgeCount;
    }

    connection.UnBind();
    int characterId = 0;
    int memberPledgeId = 0;
    int memberProposeTime = 0;
    connection.Bind(&characterId);
    connection.Bind(&memberPledgeId);
    connection.Bind(&memberProposeTime);
    constexpr const wchar_t* memberStatement = L"{Call lin_LoadTeamBattleAgitMember (?)}";
    connection.Prepare(memberStatement);
    connection.BindParameter(&agitId);
    if (!connection.ExecutePrepared(memberStatement)) {
        // Log.Add(LOG_ERROR, "Error in Getting team battle agit related member[%d]", agitId)
        // L2_LOG_ERROR(Log, "Error in Getting team battle agit related member[%d]", agitId)
        L2_LOG_ERROR(Log, "[team battle] Cannot load members for hall %d: database operation failed. Sending reply without clans or members (status %d); %d assembled clan entries will not be sent.", agitId, status, pledgeCount);
        socket->Send("cdddd", 0x9a, agitId, status, 0, 0);
        return false;
    }

    std::array<char, 0x800> memberPacket{};
    int memberBytes = 0;
    int memberCount = 0;
    while (connection.Fetch()) {
        const int assembled = ::Assemble(
            memberPacket.data() + memberBytes, static_cast<int>(memberPacket.size()) - memberBytes, "ddd", characterId,
            memberPledgeId, memberProposeTime
        );
        memberBytes += assembled;
        ++memberCount;
    }

    socket->Send(
        "cdddbdb", 0x9a, agitId, status, pledgeCount, pledgeBytes, pledgePacket.data(), memberCount, memberBytes,
        memberPacket.data()
    );
    return false;
}

bool RequestNewTeamBattleAgitPledge(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00496bec.
    L2_GUARD_FUNCTION();

    int requestContext = 0;
    int agitId = 0;
    int pledgeId = 0;
    int characterId = 0;
    int proposeTime = 0;
    int color = 0;
    int npcType = 0;
    ::Disassemble(packet, "ddddddd", &requestContext, &agitId, &pledgeId, &characterId, &proposeTime, &color, &npcType);

    int result = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    constexpr const wchar_t* statement = L"{Call lin_NewTeamBattleAgitPledge (?, ?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&agitId);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&characterId);
    connection.BindParameter(&proposeTime);
    connection.BindParameter(&color);
    connection.BindParameter(&npcType);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    } else {
        // Diagnostic addition: the existing reply keeps its initialized result 0.
        L2_LOG_ERROR(Log, "[team battle] Clan registration request %d failed in the database (hall %d, clan %d, character %d). Reply result remains 0.",
            requestContext, agitId, pledgeId, characterId);
    }

    socket->Send("cddddd", 0x9b, requestContext, result, proposeTime, color, npcType);
    return false;
}

bool RequestNewTeamBattleAgitMember(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00496dc8.
    L2_GUARD_FUNCTION();

    int requestContext = 0;
    int agitId = 0;
    int characterId = 0;
    int pledgeId = 0;
    int proposeTime = 0;
    ::Disassemble(packet, "ddddd", &requestContext, &agitId, &characterId, &pledgeId, &proposeTime);

    int result = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    constexpr const wchar_t* statement = L"{Call lin_NewTeamBattleAgitMember (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&agitId);
    connection.BindParameter(&characterId);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&proposeTime);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    } else {
        // Diagnostic addition: the existing reply keeps its initialized result 0.
        L2_LOG_ERROR(Log, "[team battle] Member registration request %d failed in the database (hall %d, character %d, clan %d). Reply result remains 0.",
            requestContext, agitId, characterId, pledgeId);
    }

    socket->Send("cddd", 0x9c, requestContext, result, proposeTime);
    return false;
}

bool RequestTeamBattleAgitStatus(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00496f78. This legacy registration is distinct
    // from current primary opcode 192 (RequstSetTeamBattleStatus).
    L2_GUARD_FUNCTION();

    int requestContext = 0;
    int agitId = 0;
    int status = 0;
    int eventValue = 0;
    ::Disassemble(packet, "dddd", &requestContext, &agitId, &status, &eventValue);

    const wchar_t* statement = nullptr;
    switch (status) {
    case 0:
        statement = L"{Call lin_FinishTeamBattle (?, ?, ?)}";
        break;
    case 1:
        statement = L"{Call lin_StartBattleRoyal (?, ?, ?)}";
        break;
    case 2:
        statement = L"{Call lin_StartFinal (?, ?, ?)}";
        break;
    default:
        // Log.Add(LOG_ERROR, "error in updating team battle agit status[%d:%d]", agitId, status)
        // L2_LOG_ERROR(Log, "error in updating team battle agit status[%d:%d]", agitId, status)
        L2_LOG_WARN(Log, "[team battle] Status request %d for hall %d rejected: unsupported status %d (expected 0, 1 or 2). No database update or reply.",
            requestContext, agitId, status);
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Prepare(statement);
    connection.BindParameter(&agitId);
    connection.BindParameter(&status);
    connection.BindParameter(&eventValue);
    if (connection.ExecutePrepared(statement)) {
        socket->Send("cddd", 0x9d, requestContext, status, eventValue);
    } else {
        // Diagnostic addition: this failure previously relied on the generic DB log.
        L2_LOG_ERROR(Log, "[team battle] Status request %d failed in the database (hall %d, status %d, event value %d). No reply is sent.",
            requestContext, agitId, status, eventValue);
    }
    return false;
}

bool RequestChangeTimeLimit(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004972f0.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 25> targetCharacterName{};
    int type = 0;
    int changeMinutes = 0;
    int requestContext = 0;
    ::Disassemble(packet, "Sddd", 0x32, targetCharacterName.data(), &type, &changeMinutes, &requestContext);
    if (changeMinutes < 0) {
        const std::string characterName = Utf16ToUtf8(FixedUtf16View(targetCharacterName));
        // Log.Add(
        //     LOG_ERROR, "RequestChangeTimeLimit have error, [%ls][%d][%d][%d]", characterName.c_str(), type,
        //     changeMinutes, requestContext
        // )
        // L2_LOG_ERROR(Log, "RequestChangeTimeLimit have error, [%ls][%d][%d][%d]", characterName.c_str(), type,
        //     changeMinutes, requestContext
        // )
        L2_LOG_WARN(Log, "[playtime] Time-limit request %d for character \"%s\" rejected: negative change %d minutes (type %d). No reply is sent.",
            requestContext, characterName.c_str(), changeMinutes, type);
        return false;
    }

    int accountId = 0;
    int usedSeconds = 0;
    const std::u16string characterName(FixedUtf16View(targetCharacterName));
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&accountId);
    connection.Bind(&usedSeconds);
    constexpr const wchar_t* statement = L"{Call lin_ChangeTimeData (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(characterName.c_str());
    connection.BindParameter(&type);
    connection.BindParameter(&changeMinutes);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
        socket->Send("cdddd", 0x9e, 0, type, usedSeconds, requestContext);
        return false;
    }

    CAccountSP account = CAccount::Load(static_cast<unsigned int>(accountId));
    if (!account) {
        // Log.Add(LOG_ERROR, "[%s][%d] Cannot find account. id [%d]", "CacheSocket.cpp", 0x2db4, accountId)
        // L2_LOG_ERROR(Log, "[%s][%d] Cannot find account. id [%d]", "CacheSocket.cpp", 0x2db4, accountId)
        L2_LOG_ERROR(Log, "[playtime] Time-limit request %d completed in the database, but account %d could not be loaded. Cached playtime is unchanged; the database result is still returned.",
            requestContext, accountId);
    } else {
        const runtime::SystemTime localTime = runtime::GetLocalTimeNow();
        const SYSTEMTIME targetLocalTime{localTime.wYear, localTime.wMonth, localTime.wDayOfWeek, localTime.wDay,
            localTime.wHour, localTime.wMinute, localTime.wSecond, localTime.wMilliseconds};
        // The current image writes the field directly. C1's recovered
        // Account.h identifies the inlined source owner as SetWeekPlayTime.
        account->SetWeekPlayTime(usedSeconds);
        account->SetLastSaveTIme(&targetLocalTime);
    }

    socket->Send("cdddd", 0x9e, accountId, type, usedSeconds, requestContext);
    return false;
}

bool RequestSaveUsedTime(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x00475710. This is the same CAccount production
    // owner used by RequestLoadPlayTime and character logout.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 25> accountName{};
    int weekPlayTime = 0;
    unsigned int accountId = 0;
    SYSTEMTIME lastSaveTime{};
    ::Disassemble(
        packet, "Sddb", 0x32, accountName.data(), &weekPlayTime, &accountId, static_cast<int>(sizeof(lastSaveTime)),
        &lastSaveTime
    );

    if (weekPlayTime < 0) {
        const std::string logAccountName = Utf16ToUtf8(FixedUtf16View(accountName));
        // Log.Add(
        //     LOG_ERROR, "RequestSaveUsedTime has error, [%ls][%d]", logAccountName.c_str(), weekPlayTime
        // )
        // L2_LOG_ERROR(Log, "RequestSaveUsedTime has error, [%ls][%d]", logAccountName.c_str(), weekPlayTime
        // )
        L2_LOG_WARN(Log, "[playtime] Cannot save negative weekly playtime %d for account %u (\"%s\"). Request skipped.",
            weekPlayTime, accountId, logAccountName.c_str());
        return false;
    }

    CAccountSP account = CAccount::Load(accountId);
    if (!account) {
        // Log.Add(LOG_ERROR, "[%s][%d] Cannot find account. id [%u]", "CacheSocket.cpp", 0x2de2, accountId)
        // L2_LOG_ERROR(Log, "[%s][%d] Cannot find account. id [%u]", "CacheSocket.cpp", 0x2de2, accountId)
        L2_LOG_ERROR(Log, "[playtime] Cannot save weekly playtime %d for account %u: account could not be loaded. Request skipped.",
            weekPlayTime, accountId);
        return false;
    }
    account->SaveWeekPlayTime(weekPlayTime, &lastSaveTime);
    return false;
}

bool RequestUnregisterTeamBattleAgitPledge(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00497680.
    L2_GUARD_FUNCTION();

    int requestContext = 0;
    int agitId = 0;
    int pledgeId = 0;
    ::Disassemble(packet, "ddd", &requestContext, &agitId, &pledgeId);

    int result = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    constexpr const wchar_t* statement = L"{Call lin_UnregisterTeamBattleAgitPledge (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&agitId);
    connection.BindParameter(&pledgeId);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }

    socket->Send("cdd", 0x9f, requestContext, result);
    return false;
}

bool RequestUnregisterTeamBattleAgitMember(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00497838.
    L2_GUARD_FUNCTION();

    int requestContext = 0;
    int agitId = 0;
    int characterId = 0;
    int pledgeId = 0;
    ::Disassemble(packet, "dddd", &requestContext, &agitId, &characterId, &pledgeId);

    int result = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    constexpr const wchar_t* statement = L"{Call lin_UnregisterTeamBattleAgitMember (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&agitId);
    connection.BindParameter(&characterId);
    connection.BindParameter(&pledgeId);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }

    socket->Send("cdd", 0xa0, requestContext, result);
    return false;
}

bool RequestMonrace(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00497a10. Current L2Server
    // CDB::Request*MonRace 0x0064dbb4..0x0064e458 fixes all nine live
    // commands, while command 3 and every other value do
    // nothing. The spelling/capitalization is the surviving PDB identifier.
    L2_GUARD_FUNCTION();

    int command = 0;
    const std::uint8_t* const commandPacket = ::Disassemble(packet, "d", &command);

    if (command == 1) {
        int runnerId = 0;
        int initialWin = 0;
        int runCount = 0;
        int winCount = 0;
        ::Disassemble(commandPacket, "dddd", &runnerId, &initialWin, &runCount, &winCount);

        int resultInitialWin = 0;
        int resultRunCount = 0;
        int resultWinCount = 0;
        std::int64_t nullableIndicator = -1;
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.BindNullable(&resultInitialWin, &nullableIndicator);
        connection.BindNullable(&resultRunCount, &nullableIndicator);
        connection.BindNullable(&resultWinCount, &nullableIndicator);
        constexpr const wchar_t* statement = L"{Call lin_CreateMonRaceMon (?, ?, ?, ? )}";
        connection.Prepare(statement);
        connection.BindParameter(&runnerId);
        connection.BindParameter(&initialWin);
        connection.BindParameter(&runCount);
        connection.BindParameter(&winCount);
        if (connection.ExecutePrepared(statement)) {
            connection.Fetch();
        }
        socket->Send("cddddd", 0xa1, command, runnerId, resultInitialWin, resultRunCount, resultWinCount);
        return false;
    }

    if (command == 2) {
        std::array<int, 8> lanes{};
        int taxRate = 0;
        ::Disassemble(
            commandPacket, "ddddddddd", &lanes[0], &lanes[1], &lanes[2], &lanes[3], &lanes[4], &lanes[5], &lanes[6],
            &lanes[7], &taxRate
        );

        int raceId = 0;
        std::int64_t nullableIndicator = -1;
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.BindNullable(&raceId, &nullableIndicator);
        constexpr const wchar_t* statement = L"{Call lin_CreateMonRace (?, ?, ?, ?, ?, ?, ?, ?, ? )}";
        connection.Prepare(statement);
        for (const int& lane : lanes) {
            connection.BindParameter(&lane);
        }
        connection.BindParameter(&taxRate);
        if (connection.ExecutePrepared(statement)) {
            connection.Fetch();
        }
        g_MonRaceTaxRate = taxRate;
        socket->Send("cdd", 0xa1, command, raceId);
        return false;
    }

    if (command == 4) {
        int raceId = 0;
        ::Disassemble(commandPacket, "d", &raceId);

        std::array<char, 0x1000> rows{};
        int rowBytes = 0;
        int firstRowCount = 0;
        int firstBetTotal = 0;
        int firstBet = 0;
        int betAmount = 0;
        std::int64_t nullableIndicator = -1;
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.BindNullable(&firstBet, &nullableIndicator);
        connection.BindNullable(&betAmount, &nullableIndicator);

        int betType = 1;
        constexpr const wchar_t* statement = L"{Call lin_GetMonRaceBet (?, ? )}";
        connection.Prepare(statement);
        connection.BindParameter(&betType);
        connection.BindParameter(&raceId);
        if (connection.ExecutePrepared(statement)) {
            while (connection.Fetch()) {
                firstBetTotal = runtime::AddInt32Wrapping(firstBetTotal, betAmount);
                const int assembled =
                    ::Assemble(rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dd", firstBet, betAmount);
                if (assembled > 0)
                    rowBytes += assembled;
                firstRowCount = runtime::AddInt32Wrapping(firstRowCount, 1);
            }
        }

        connection.UnBind();
        int secondBet = 0;
        int secondBetTotal = 0;
        int secondRowCount = 0;
        connection.BindNullable(&firstBet, &nullableIndicator);
        connection.BindNullable(&secondBet, &nullableIndicator);
        connection.BindNullable(&betAmount, &nullableIndicator);
        betType = 2;
        connection.Prepare(statement);
        connection.BindParameter(&betType);
        connection.BindParameter(&raceId);
        if (connection.ExecutePrepared(statement)) {
            while (connection.Fetch()) {
                if (secondBet < firstBet) {
                    std::swap(firstBet, secondBet);
                }
                secondBetTotal = runtime::AddInt32Wrapping(secondBetTotal, betAmount);
                const int assembled = ::Assemble(
                    rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "ddd", firstBet, secondBet, betAmount
                );
                if (assembled > 0)
                    rowBytes += assembled;
                secondRowCount = runtime::AddInt32Wrapping(secondRowCount, 1);
            }
        }

        socket->Send(
            "cdddddb", 0xa1, command, firstRowCount, firstBetTotal, secondRowCount, secondBetTotal, rowBytes, rows.data()
        );
        return false;
    }

    if (command == 5) {
        int raceId = 0;
        std::array<double, 10> runValues{};
        int winner1 = 0;
        int winner2 = 0;
        double winRate1 = 0.0;
        double winRate2 = 0.0;
        ::Disassemble(
            commandPacket, "dffffffffffddff", &raceId, &runValues[0], &runValues[1], &runValues[2], &runValues[3],
            &runValues[4], &runValues[5], &runValues[6], &runValues[7], &runValues[8], &runValues[9], &winner1, &winner2,
            &winRate1, &winRate2
        );

        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* updateRaceStatement = L"{Call lin_UpdateMonRace (?, ?, ?)}";
        connection.Prepare(updateRaceStatement);
        connection.BindParameter(&raceId);
        connection.BindParameter(&winRate1);
        connection.BindParameter(&winRate2);
        connection.ExecutePrepared(updateRaceStatement);

        connection.UnBind();
        constexpr const wchar_t* updateInfoStatement = L"{Call lin_UpdateMonRaceInfo (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ? )}";
        connection.Prepare(updateInfoStatement);
        connection.BindParameter(&raceId);
        for (std::size_t index = 0; index < 8; ++index) {
            connection.BindParameter(&runValues[index]);
        }
        connection.BindParameter(&winner1);
        connection.BindParameter(&winner2);
        connection.ExecutePrepared(updateInfoStatement);
        // Current Cached.exe decodes these two doubles but never reads them.

        connection.UnBind();
        int taxSum = 0;
        std::int64_t nullableIndicator = -1;
        connection.BindNullable(&taxSum, &nullableIndicator);
        constexpr const wchar_t* taxStatement = L"{Call lin_GetMonRaceTaxSum (?  )}";
        connection.Prepare(taxStatement);
        connection.BindParameter(&raceId);
        if (connection.ExecutePrepared(taxStatement)) {
            connection.Fetch();
        }
        socket->Send("cddd", 0xa1, command, raceId, taxSum);
        return false;
    }

    if (command == 6) {
        int runnerId = 0;
        int winRate = 0;
        int runCount = 0;
        int winCount = 0;
        ::Disassemble(commandPacket, "dddd", &runnerId, &winRate, &runCount, &winCount);

        int resultWinRate = 0;
        int resultRunCount = 0;
        int resultWinCount = 0;
        std::int64_t nullableIndicator = -1;
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.BindNullable(&resultWinRate, &nullableIndicator);
        connection.BindNullable(&resultRunCount, &nullableIndicator);
        connection.BindNullable(&resultWinCount, &nullableIndicator);
        constexpr const wchar_t* statement = L"{Call lin_UpdateMonRaceMon (?, ?, ?, ? )}";
        connection.Prepare(statement);
        connection.BindParameter(&runnerId);
        connection.BindParameter(&winRate);
        connection.BindParameter(&runCount);
        connection.BindParameter(&winCount);
        if (connection.ExecutePrepared(statement)) {
            connection.Fetch();
        }
        socket->Send("cddddd", 0xa1, command, runnerId, resultWinRate, resultRunCount, resultWinCount);
        return false;
    }

    if (command == 7) {
        int characterId = 0;
        int userSocketId = 0;
        int page = 1;
        ::Disassemble(commandPacket, "ddd", &characterId, &userSocketId, &page);

        CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
        CWareHouseSP warehouse = user ? user->GetWareHouse(0, true) : CWareHouseSP{};
        if (warehouse) {
            {
                L2_WRITE_GUARD(*warehouse.operator->());
                warehouse->SendMonRaceTicketList(socket, command, characterId, userSocketId, page);
            }
        } else {
            socket->Send("cddddd", 0xa1, command, characterId, userSocketId, 0, page);
        }
        return false;
    }

    if (command == 8) {
        int characterId = 0;
        int userSocketId = 0;
        int itemId = 0;
        ::Disassemble(commandPacket, "ddd", &characterId, &userSocketId, &itemId);

        int ticketId = 0;
        int raceId = 0;
        int betType = 0;
        int bet1 = 0;
        int bet2 = 0;
        int bet3 = 0;
        int betMoney = 0;
        double winRate1 = 0.0;
        double winRate2 = 0.0;
        int winner1 = 0;
        int winner2 = 0;
        int raceEnd = 0;

        CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
        CWareHouseSP warehouse = user ? user->GetWareHouse(0, true) : CWareHouseSP{};
        bool hasTicket = false;
        if (warehouse) {
            {
                L2_WRITE_GUARD(*warehouse.operator->());
                hasTicket = warehouse->CheckMonRaceTicket(static_cast<unsigned int>(itemId));
            }
        }
        if (hasTicket) {
            std::int64_t nullableIndicator = -1;
            DBConn connection(runtime::process::CurrentDatabaseId());
            connection.BindNullable(&ticketId, &nullableIndicator);
            connection.BindNullable(&raceId, &nullableIndicator);
            connection.BindNullable(&betType, &nullableIndicator);
            connection.BindNullable(&bet1, &nullableIndicator);
            connection.BindNullable(&bet2, &nullableIndicator);
            connection.BindNullable(&bet3, &nullableIndicator);
            connection.BindNullable(&betMoney, &nullableIndicator);
            connection.BindNullable(&winRate1, &nullableIndicator);
            connection.BindNullable(&winRate2, &nullableIndicator);
            connection.BindNullable(&winner1, &nullableIndicator);
            connection.BindNullable(&winner2, &nullableIndicator);
            connection.BindNullable(&raceEnd, &nullableIndicator);
            constexpr const wchar_t* statement = L"{Call lin_GetMonRaceTicket (?  )}";
            connection.Prepare(statement);
            connection.BindParameter(&itemId);
            if (connection.ExecutePrepared(statement)) {
                connection.Fetch();
            }
        }

        socket->Send(
            "cdddddddddddffddd", 0xa1, command, characterId, userSocketId, itemId, ticketId, raceId, betType, bet1, bet2,
            bet3, betMoney, winRate1, winRate2, winner1, winner2, raceEnd
        );
        return false;
    }

    if (command == 9) {
        int characterId = 0;
        int userSocketId = 0;
        int itemId = 0;
        int currentRaceId = 0;
        int allowCurrentRace = 0;
        ::Disassemble(commandPacket, "ddddd", &characterId, &userSocketId, &itemId, &currentRaceId, &allowCurrentRace);

        int success = 0;
        int adenaItemId = 0;
        std::int64_t reward = 0;
        CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
        CWareHouseSP warehouse = user ? user->GetWareHouse(0, true) : CWareHouseSP{};
        if (warehouse) {
            {
                L2_WRITE_GUARD(*warehouse.operator->());
                reward = warehouse->DelMonRaceTicket(static_cast<unsigned int>(itemId), currentRaceId, allowCurrentRace);
                if (reward > 0) {
                    success = 1;
                    CItemSP adena = warehouse->GetAdena();
                    if (adena) {
                        adenaItemId = static_cast<int>(adena->ItemID());
                    }
                }
            }
        }
        socket->Send(
            "cddddddQ", 0xa1, command, characterId, userSocketId, success, itemId, adenaItemId,
            static_cast<long long>(reward)
        );
        return false;
    }

    if (command == 10) {
        int characterId = 0;
        int userSocketId = 0;
        int page = 1;
        ::Disassemble(commandPacket, "ddd", &characterId, &userSocketId, &page);

        std::array<char, 0x2000> rows{};
        int rowBytes = 0;
        int rowCount = 0;
        CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
        if (user) {
            int raceId = 0;
            std::array<int, 8> lanes{};
            int winner1 = 0;
            int winner2 = 0;
            double winRate1 = 0.0;
            double winRate2 = 0.0;
            int raceEnd = 0;
            std::int64_t nullableIndicator = -1;
            DBConn connection(runtime::process::CurrentDatabaseId());
            connection.BindNullable(&raceId, &nullableIndicator);
            for (int& lane : lanes) {
                connection.BindNullable(&lane, &nullableIndicator);
            }
            connection.BindNullable(&winner1, &nullableIndicator);
            connection.BindNullable(&winner2, &nullableIndicator);
            connection.BindNullable(&winRate1, &nullableIndicator);
            connection.BindNullable(&winRate2, &nullableIndicator);
            connection.BindNullable(&raceEnd, &nullableIndicator);

            const int lineCount = 7;
            constexpr const wchar_t* statement = L"{Call lin_GetMonRaceResult (?, ?  )}";
            connection.Prepare(statement);
            connection.BindParameter(&page);
            connection.BindParameter(&lineCount);
            if (connection.ExecutePrepared(statement)) {
                if (page < 1)
                    page = 1;
                while (connection.Fetch()) {
                    const int assembled = ::Assemble(
                        rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dddddddddddffd", raceId, lanes[0],
                        lanes[1], lanes[2], lanes[3], lanes[4], lanes[5], lanes[6], lanes[7], winner1, winner2, winRate1,
                        winRate2, raceEnd
                    );
                    if (assembled > 0) {
                        rowBytes += assembled;
                        rowCount = runtime::AddInt32Wrapping(rowCount, 1);
                    }
                }
            }
        }
        socket->Send("cdddddb", 0xa1, command, characterId, userSocketId, page, rowCount, rowBytes, rows.data());
        return false;
    }

    return false;
}

bool RequestLoadSiegeAgit(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00498e74. The paired current L2Server writer
    // CDB::RequestLoadSiegeAgit at 0x00668a34 and reply consumer at
    // 0x006c384c fix the context/agit identities and row ABI.
    L2_GUARD_FUNCTION();

    int requestContext = 0;
    unsigned int agitId = 0;
    ::Disassemble(packet, "dd", &requestContext, &agitId);

    int pledgeId = 0;
    int proposeTime = 0;
    int status = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&pledgeId);
    connection.Bind(&proposeTime);
    connection.Bind(&status);
    constexpr const wchar_t* statement = L"{Call lin_LoadSiegeAgitPledge (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&agitId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "Error in Getting siege agit related pledge[%d]", static_cast<int>(agitId))
        // L2_LOG_ERROR(Log, "Error in Getting siege agit related pledge[%d]", static_cast<int>(agitId))
        L2_LOG_ERROR(Log, "[siege] Cannot load clans for hall %u: database operation failed. Sending empty list for request %d.", agitId, requestContext);
        socket->Send("cdd", 0xa2, requestContext, 0);
        return false;
    }

    std::array<char, 0x400> rows{};
    int rowBytes = 0;
    int rowCount = 0;
    while (connection.Fetch()) {
        rowBytes += ::Assemble(
            rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "ddd", pledgeId, proposeTime, status
        );
        ++rowCount;
    }
    socket->Send("cddb", 0xa2, requestContext, rowCount, rowBytes, rows.data());
    return false;
}

bool RequestRegisterSiegeAgitPledge(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004990d0. Paired current producer
    // CDB::RequestRegisterSiegeAgitPledge is 0x00668b2c.
    L2_GUARD_FUNCTION();

    int requestContext = 0;
    int agitId = 0;
    int pledgeId = 0;
    int proposeTime = 0;
    ::Disassemble(packet, "dddd", &requestContext, &agitId, &pledgeId, &proposeTime);

    int result = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    constexpr const wchar_t* statement = L"{Call lin_RegisterSiegeAgitPledge (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&agitId);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&proposeTime);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }
    socket->Send("cddd", 0xa3, requestContext, result, proposeTime);
    return false;
}

bool RequestUnregisterSiegeAgitPledge(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004992b0. Paired current producer and consumer are
    // 0x00668c90 and 0x006c1964 respectively.
    L2_GUARD_FUNCTION();

    int requestContext = 0;
    int agitId = 0;
    int pledgeId = 0;
    ::Disassemble(packet, "ddd", &requestContext, &agitId, &pledgeId);

    int result = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    constexpr const wchar_t* statement = L"{Call lin_UnregisterSiegeAgitPledge (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&agitId);
    connection.BindParameter(&pledgeId);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }
    socket->Send("cdd", 0xa4, requestContext, result);
    return false;
}

bool RequstSetTeamBattleStatus(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00499468. The missing 'e' in Requst is the exact
    // shipping guard spelling. This primary opcode is distinct from the
    // kept legacy RequestTeamBattleAgitStatus registration at ID 183.
    L2_GUARD_FUNCTION();

    int requestContext = 0;
    int agitId = 0;
    int status = 0;
    ::Disassemble(packet, "ddd", &requestContext, &agitId, &status);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetTeamBattleStatus (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&agitId);
    connection.BindParameter(&status);
    const int result = connection.ExecutePrepared(statement) ? 1 : 0;
    socket->Send("cddd", 0xa5, requestContext, result, status);
    return false;
}

bool RequestSetTeamBattleWinner(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00499608. Stack-only Windows x64 varargs at the
    // Disassemble, four-input SQL helper and Send sites fix every field below.
    L2_GUARD_FUNCTION();

    int requestContext = 0;
    int agitId = 0;
    int winnerPledgeId = 0;
    int masterPledgeId = 0;
    int isFinal = 0;
    ::Disassemble(packet, "ddddd", &requestContext, &agitId, &winnerPledgeId, &masterPledgeId, &isFinal);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetTeamBattleWinner (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&agitId);
    connection.BindParameter(&winnerPledgeId);
    connection.BindParameter(&masterPledgeId);
    connection.BindParameter(&isFinal);
    const int result = connection.ExecutePrepared(statement) ? 1 : 0;
    socket->Send("cdddd", 0xa6, requestContext, winnerPledgeId, result, isFinal);
    return false;
}

bool RequestUseRecipeItem(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004758d4. Paired current producer/consumer are
    // CDB::UseRecipeItem 0x0064d6d0 and ReplyUseRecipeItem 0x006b1b18.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    const std::uint8_t* const warehousePacket = ::Disassemble(packet, "d", &characterId);

    auto sendMissingOwner = [&]() {
        int ignoredItemId = 0;
        int userServerId = 0;
        ::Disassemble(warehousePacket, "dd", &ignoredItemId, &userServerId);
        socket->Send("cdd", 0xa7, userServerId, 0);
    };

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x304c, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x304c, characterId)
        L2_LOG_ERROR(Log, "[recipes] Cannot use recipe item for character %d: character could not be loaded. Sending failure reply.", characterId);
        sendMissingOwner();
        return false;
    }

    CWareHouseSP inventory = user->GetWareHouse(0, true);
    if (!inventory) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find warehouse [0], id[%d]", "CacheSocket.cpp", 0x3054, characterId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find warehouse [0], id[%d]", "CacheSocket.cpp", 0x3054, characterId
        // )
        L2_LOG_ERROR(Log, "[recipes] Cannot use recipe item for character %d: inventory unavailable. Sending failure reply.", characterId);
        sendMissingOwner();
        return false;
    }

    {
        L2_WRITE_GUARD(*inventory.operator->());
        inventory->UseRecipeItem(socket, warehousePacket);
    }
    return false;
}

bool RequestSaveRecipeBookInfo(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00475c34.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int recipeId = 0;
    int userServerId = 0;
    ::Disassemble(packet, "ddd", &characterId, &recipeId, &userServerId);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x307a, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x307a, characterId)
        L2_LOG_ERROR(Log, "[recipes] Cannot save recipe %d for character %d: character could not be loaded. Sending legacy 0xa7 failure reply.", recipeId, characterId);
        // The shipping missing-user path uses opcode 0xa7, not this
        // request's ordinary 0xa8 reply.
        socket->Send("cdd", 0xa7, userServerId, 0);
        return false;
    }

    user->SaveRecipeBookInfo(recipeId, userServerId, socket);
    return false;
}

bool RequestDeleteRecipeBookInfo(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00475dec.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int recipeId = 0;
    int userServerId = 0;
    ::Disassemble(packet, "ddd", &characterId, &recipeId, &userServerId);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x309b, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x309b, characterId)
        L2_LOG_ERROR(Log, "[recipes] Cannot delete recipe %d for character %d: character could not be loaded. Sending legacy 0xa7 failure reply.", recipeId, characterId);
        // This is the same target anomaly as save: opcode 0xa7 on a missing
        // owner instead of the ordinary 0xa9 reply.
        socket->Send("cdd", 0xa7, userServerId, 0);
        return false;
    }

    user->DeleteRecipeBookInfo(recipeId, userServerId, socket);
    return false;
}

bool RequestLoadRecipeBookInfo(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00475fa4. CUser::SendRecipeBookInfo owns the
    // ordered list codec.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int userServerId = 0;
    ::Disassemble(packet, "dd", &characterId, &userServerId);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x30ba, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x30ba, characterId)
        L2_LOG_ERROR(Log, "[recipes] Cannot load recipe book for character %d: character could not be loaded. Sending reply with zero entries.", characterId);
        socket->Send("cdd", 0xaa, userServerId, 0);
        return false;
    }

    user->SendRecipeBookInfo(userServerId, socket);
    return false;
}

bool RequestRecipeShopMakeItem(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0047614c. The customer inventory owns the
    // cross-warehouse transaction. This wrapper fixes the target's lock
    // ordering and its short failure reply.
    L2_GUARD_FUNCTION();

    int makerCharacterId = 0;
    int makerServerId = 0;
    int customerCharacterId = 0;
    int customerServerId = 0;
    int responseValue1 = 0;
    int responseValue2 = 0;
    ::Disassemble(
        packet, "dddddd", &makerCharacterId, &makerServerId, &customerCharacterId, &customerServerId, &responseValue1,
        &responseValue2
    );

    auto sendFailure = [&]() {
        socket->Send("cddddd", 0xab, makerServerId, customerServerId, responseValue1, responseValue2, 0);
    };

    CUserSP maker = g_userDb.GetUser(makerCharacterId, true, false, runtime::process::CurrentDatabaseId());
    if (!maker) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user", "CacheSocket.cpp", 0x30ff)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user", "CacheSocket.cpp", 0x30ff)
        L2_LOG_ERROR(Log, "[crafting] Recipe-shop request failed: maker character %d unavailable (customer %d). Sending failure reply.", makerCharacterId, customerCharacterId);
        sendFailure();
        return false;
    }

    CUserSP customer = g_userDb.GetUser(customerCharacterId, true, false, runtime::process::CurrentDatabaseId());
    if (!customer) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user", "CacheSocket.cpp", 0x3107)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user", "CacheSocket.cpp", 0x3107)
        L2_LOG_ERROR(Log, "[crafting] Recipe-shop request failed: customer character %d unavailable (maker %d). Sending failure reply.", customerCharacterId, makerCharacterId);
        sendFailure();
        return false;
    }

    CWareHouseSP makerInventory = maker->GetWareHouse(0, true);
    CWareHouseSP customerInventory = customer->GetWareHouse(0, true);
    if (!makerInventory || !customerInventory) {
        // Log.Add(LOG_ERROR, "Error, Invalid sp.")
        // L2_LOG_ERROR(Log, "Error, Invalid sp.")
        L2_LOG_ERROR(Log, "[crafting] Recipe-shop request cannot proceed: maker %d inventory %s, customer %d inventory %s. No reply is sent.",
            makerCharacterId, makerInventory ? "available" : "unavailable",
            customerCharacterId, customerInventory ? "available" : "unavailable");
        return false;
    }

    CWareHouse* const makerWarehouse = makerInventory.get();
    CWareHouse* const customerWarehouse = customerInventory.get();
    CWareHouse* firstLock = makerWarehouse;
    CWareHouse* secondLock = customerWarehouse;
    const std::uint64_t makerLock = makerWarehouse->GetLockAddr();
    const std::uint64_t customerLock = customerWarehouse->GetLockAddr();
    const bool sameLock = makerLock == customerLock;
    if (sameLock) {
        // Log.Add(LOG_ERROR, "Same warehouse double writelock?-_-,")
        // L2_LOG_ERROR(Log, "Same warehouse double writelock?-_-,")
        L2_LOG_ERROR(Log, "[crafting] Recipe-shop request for maker %d and customer %d encountered equal warehouse lock addresses. Processing continues with one lock.", makerCharacterId, customerCharacterId);
        secondLock = nullptr;
    } else if (customerLock < makerLock) {
        firstLock = customerWarehouse;
        secondLock = makerWarehouse;
    }

    {
        L2_WRITE_GUARD(*firstLock);
        auto executeRequest = [&]() {
            if (makerWarehouse->m_lateSavingItemsPending) {
                makerWarehouse->SaveLateSavingItems();
            }
            if (customerWarehouse->m_lateSavingItemsPending) {
                customerWarehouse->SaveLateSavingItems();
            }

            const int result = customerWarehouse->RecipeShopMakeItem(socket, packet, makerWarehouse);
            if (result < 1) {
                sendFailure();
            }
        };
        if (secondLock) {
            L2_WRITE_GUARD(*secondLock);
            executeRequest();
        } else {
            executeRequest();
        }
    }
    return false;
}

bool RequestUpdateSociality(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x00476b10. The literal at 0x006d68a8 is "dddb".
    // the final counted carrier is the target's raw 16-byte SYSTEMTIME.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int sociality = 0;
    int usedSuffrageCount = 0;
    SYSTEMTIME lastCheck{};
    ::Disassemble(
        packet, "dddb", &characterId, &sociality, &usedSuffrageCount, static_cast<int>(sizeof(lastCheck)), &lastCheck
    );

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, [%d]", "CacheSocket.cpp", 0x3138, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, [%d]", "CacheSocket.cpp", 0x3138, characterId)
        L2_LOG_ERROR(Log, "[sociality] Cannot update character %d: character could not be loaded. Request skipped.", characterId);
        return false;
    }

    user->SetSocialityAll(sociality, usedSuffrageCount, &lastCheck);
    return false;
}

bool RequestSetTeamBattleNpcType(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x0049979c. Paired current
    // CDB::RequestSetTeamBattleNpcType is 0x0064ce88.
    L2_GUARD_FUNCTION();

    int agitId = 0;
    int pledgeId = 0;
    int npcType = 0;
    ::Disassemble(packet, "ddd", &agitId, &pledgeId, &npcType);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetTeamBattleNpcType (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&agitId);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&npcType);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "error occurred in set npc type : team battle agit[%d] "
        //     "pledge [%d] npc type[%d]]",
        //     agitId, pledgeId, npcType
        // )
        // L2_LOG_ERROR(Log,
        //     "error occurred in set npc type : team battle agit[%d] "
        //     "pledge [%d] npc type[%d]]",
        //     agitId, pledgeId, npcType
        // )
        L2_LOG_ERROR(Log, "[team battle] Cannot set NPC type %d for clan %d in hall %d: database operation failed.", npcType, pledgeId, agitId);
    }
    return false;
}

bool RequestResetSiegeAgitPledge(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x0048ed58. Delete success is ignored. Every
    // replacement insert owns a fresh result binding and conditionally
    // fetches exactly once.
    L2_GUARD_FUNCTION();

    unsigned int agitId = 0;
    int pledgeCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "dd", &agitId, &pledgeCount);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* deleteStatement = L"{Call lin_DeleteSiegeAgitPledge (?)}";
    connection.Prepare(deleteStatement);
    connection.BindParameter(&agitId);
    connection.ExecutePrepared(deleteStatement);

    for (int index = 0; index < pledgeCount; ++index) {
        unsigned int pledgeId = 0;
        unsigned int proposeTime = 0;
        cursor = ::Disassemble(cursor, "dd", &pledgeId, &proposeTime);

        connection.UnBind();
        int result = 0;
        connection.Bind(&result);
        constexpr const wchar_t* insertStatement = L"{Call lin_RegisterSiegeAgitPledge (?, ?, ?)}";
        connection.Prepare(insertStatement);
        connection.BindParameter(&agitId);
        connection.BindParameter(&pledgeId);
        connection.BindParameter(&proposeTime);
        if (connection.ExecutePrepared(insertStatement)) {
            connection.Fetch();
        }
    }
    return false;
}

bool RequestResetTeamBattleAgitPledge(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x0048ef6c. This is the target's non-transactional
    // delete-then-reinsert sequence. All Execute results are ignored.
    L2_GUARD_FUNCTION();

    unsigned int agitId = 0;
    int pledgeCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "dd", &agitId, &pledgeCount);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* deletePledges = L"{Call lin_DeleteTeamBattleAgitPledge (?)}";
    connection.Prepare(deletePledges);
    connection.BindParameter(&agitId);
    connection.ExecutePrepared(deletePledges);

    for (int index = 0; index < pledgeCount; ++index) {
        unsigned int pledgeId = 0;
        unsigned int proposeTime = 0;
        unsigned int color = 0;
        unsigned int npcType = 0;
        cursor = ::Disassemble(cursor, "dddd", &pledgeId, &proposeTime, &color, &npcType);

        connection.ResetParam();
        constexpr const wchar_t* insertPledge = L"{Call lin_InsertIntoTeamBattleAgitPledge "
                                                L"(?, ?, ?, ?, ?)}";
        connection.Prepare(insertPledge);
        connection.BindParameter(&agitId);
        connection.BindParameter(&pledgeId);
        connection.BindParameter(&proposeTime);
        connection.BindParameter(&color);
        connection.BindParameter(&npcType);
        connection.ExecutePrepared(insertPledge);
    }

    connection.ResetParam();
    constexpr const wchar_t* deleteMembers = L"{Call lin_DeleteTeamBattleAgitMember (?)}";
    connection.Prepare(deleteMembers);
    connection.BindParameter(&agitId);
    connection.ExecutePrepared(deleteMembers);

    int memberCount = 0;
    cursor = ::Disassemble(cursor, "d", &memberCount);
    for (int index = 0; index < memberCount; ++index) {
        unsigned int characterId = 0;
        unsigned int pledgeId = 0;
        unsigned int proposeTime = 0;
        cursor = ::Disassemble(cursor, "ddd", &characterId, &pledgeId, &proposeTime);

        connection.ResetParam();
        constexpr const wchar_t* insertMember = L"{Call lin_InsertIntoTeamBattleAgitMember (?, ?, ?, ?)}";
        connection.Prepare(insertMember);
        connection.BindParameter(&agitId);
        connection.BindParameter(&characterId);
        connection.BindParameter(&pledgeId);
        connection.BindParameter(&proposeTime);
        connection.ExecutePrepared(insertMember);
    }
    return false;
}

bool RequestEquipHenna(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00476ce8. The owner receives the original packet
    // after this three-value lookup prefix has selected the user inventory.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int userServerId = 0;
    int hennaId = 0;
    ::Disassemble(packet, "ddd", &characterId, &userServerId, &hennaId);

    auto sendFailure = [&]() { socket->Send("cddd", 0xac, userServerId, hennaId, 0); };

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        sendFailure();
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, [%d]", "CacheSocket.cpp", 0x317e, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, [%d]", "CacheSocket.cpp", 0x317e, characterId)
        L2_LOG_ERROR(Log, "[henna] Cannot equip henna %d for character %d: character could not be loaded. Reply result 0.", hennaId, characterId);
        return false;
    }

    CWareHouseSP inventory = user->GetWareHouse(0, true);
    if (!inventory) {
        sendFailure();
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find warehouse, [%d]", "CacheSocket.cpp", 0x318f, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find warehouse, [%d]", "CacheSocket.cpp", 0x318f, characterId)
        L2_LOG_ERROR(Log, "[henna] Cannot equip henna %d for character %d: inventory unavailable. Reply result 0.", hennaId, characterId);
        return false;
    }

    {
        L2_WRITE_GUARD(*inventory.operator->());
        if (!inventory->EquipHenna(socket, packet)) {
            sendFailure();
        }
    }
    return false;
}

bool RequestUnequipHenna(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00477034.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int userServerId = 0;
    int hennaId = 0;
    ::Disassemble(packet, "ddd", &characterId, &userServerId, &hennaId);

    auto sendFailure = [&]() { socket->Send("cddd", 0xad, userServerId, hennaId, 0); };

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        sendFailure();
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, [%d]", "CacheSocket.cpp", 0x31bc, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, [%d]", "CacheSocket.cpp", 0x31bc, characterId)
        L2_LOG_ERROR(Log, "[henna] Cannot unequip henna %d for character %d: character could not be loaded. Reply result 0.", hennaId, characterId);
        return false;
    }

    CWareHouseSP inventory = user->GetWareHouse(0, true);
    if (!inventory) {
        sendFailure();
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find warehouse, [%d]", "CacheSocket.cpp", 0x31cd, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find warehouse, [%d]", "CacheSocket.cpp", 0x31cd, characterId)
        L2_LOG_ERROR(Log, "[henna] Cannot unequip henna %d for character %d: inventory unavailable. Reply result 0.", hennaId, characterId);
        return false;
    }

    {
        L2_WRITE_GUARD(*inventory.operator->());
        if (!inventory->UnequipHenna(socket, packet)) {
            sendFailure();
        }
    }
    return false;
}

bool RequestManageMacro(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00499950. The paired current CDB request bodies
    // at 0x0064ea6c..0x0064ed30 fix operations 1=create, 2=update,
    // 3=delete and 4=get-all.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int userServerId = 0;
    int operation = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "ddd", &characterId, &userServerId, &operation);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    bool operationAccepted = false;
    int responseType = 0;
    int responseMacroId = 0;

    if (operation == 1) {
        int requestedMacroId = 0;
        std::array<char16_t, 64> macroName{};
        std::array<char16_t, 64> macroTooltip{};
        std::array<char16_t, 64> macroIconName{};
        int macroIconType = 0;
        std::uint8_t infoCount = 0;
        cursor = ::Disassemble(
            cursor, "dSSSdc", &requestedMacroId, 0x80, macroName.data(), 0x80, macroTooltip.data(), 0x80,
            macroIconName.data(), &macroIconType, reinterpret_cast<char*>(&infoCount)
        );

        const std::u16string databaseMacroName(FixedUtf16View(macroName));
        const std::u16string databaseMacroTooltip(FixedUtf16View(macroTooltip));
        const std::u16string databaseMacroIconName(FixedUtf16View(macroIconName));
        const int createdMacroId = UserMacroHandler::CreateUserMacro(
            characterId, databaseMacroName.c_str(), databaseMacroTooltip.c_str(), databaseMacroIconName.c_str(),
            macroIconType, responseType, responseMacroId
        );
        if (createdMacroId < 1)
            return false;

        for (int index = 0; index < static_cast<int>(infoCount); ++index) {
            UserMacroInfo macroInfo{};
            macroInfo.macroId = createdMacroId;
            std::uint8_t macroOrder = 0;
            std::uint8_t macroInt1 = 0;
            std::uint8_t macroInt3 = 0;
            cursor = ::Disassemble(
                cursor, "ccdcS", reinterpret_cast<char*>(&macroOrder), reinterpret_cast<char*>(&macroInt1),
                &macroInfo.macroInt2, reinterpret_cast<char*>(&macroInt3), 0xa2, macroInfo.macroString.data()
            );
            macroInfo.macroOrder = macroOrder;
            macroInfo.macroInt1 = macroInt1;
            macroInfo.macroInt3 = macroInt3;
            UserMacroHandler::CreateUserMacroInfo(macroInfo);
        }
        operationAccepted = true;
    } else if (operation == 2) {
        int macroId = 0;
        std::array<char16_t, 64> macroName{};
        std::array<char16_t, 64> macroTooltip{};
        std::array<char16_t, 64> macroIconName{};
        int macroIconType = 0;
        std::uint8_t infoCount = 0;
        cursor = ::Disassemble(
            cursor, "dSSSdc", &macroId, 0x80, macroName.data(), 0x80, macroTooltip.data(), 0x80, macroIconName.data(),
            &macroIconType, reinterpret_cast<char*>(&infoCount)
        );

        const std::u16string databaseMacroName(FixedUtf16View(macroName));
        const std::u16string databaseMacroTooltip(FixedUtf16View(macroTooltip));
        const std::u16string databaseMacroIconName(FixedUtf16View(macroIconName));

        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* setStatement = L"{Call lin_SetMacro (?, ?, ?, ?, ? )}";
        connection.Prepare(setStatement);
        connection.BindParameter(&macroId);
        connection.BindParameter(databaseMacroName.c_str());
        connection.BindParameter(databaseMacroTooltip.c_str());
        connection.BindParameter(databaseMacroIconName.c_str());
        connection.BindParameter(&macroIconType);
        connection.ExecutePrepared(setStatement);
        responseType = 2;
        responseMacroId = macroId;
        connection.UnBind();

        for (int index = 0; index < static_cast<int>(infoCount); ++index) {
            std::uint8_t macroOrder = 0;
            std::uint8_t macroInt1 = 0;
            int macroInt2 = 0;
            std::uint8_t macroInt3 = 0;
            std::array<char16_t, 81> macroString{};
            cursor = ::Disassemble(
                cursor, "ccdcS", reinterpret_cast<char*>(&macroOrder), reinterpret_cast<char*>(&macroInt1), &macroInt2,
                reinterpret_cast<char*>(&macroInt3), 0xa2, macroString.data()
            );
            const std::u16string databaseMacroString(FixedUtf16View(macroString));

            constexpr const wchar_t* infoStatement = L"{Call lin_CreateMacroInfo (?, ?, ?, ?, ?, ?  )}";
            connection.Prepare(infoStatement);
            const int order = macroOrder;
            const int int1 = macroInt1;
            const int int3 = macroInt3;
            connection.BindParameter(&macroId);
            connection.BindParameter(&order);
            connection.BindParameter(&int1);
            connection.BindParameter(&macroInt2);
            connection.BindParameter(&int3);
            connection.BindParameter(databaseMacroString.c_str());
            connection.ExecutePrepared(infoStatement);
            connection.UnBind();
        }
    } else if (operation == 3) {
        int macroId = 0;
        ::Disassemble(cursor, "d", &macroId);
        if (macroId > 0) {
            user->DeleteUserMacro(macroId);
            operationAccepted = true;
        }
        responseType = 0;
        responseMacroId = macroId;
        if (!operationAccepted)
            return false;
    } else if (operation == 4) {
        responseType = 3;
        responseMacroId = 0;
    } else {
        return false;
    }

    DefaultMacroStrategy strategy(characterId, userServerId, socket, responseType, responseMacroId);
    UserMacroHandler::SendMacro(strategy);
    return false;
}

static bool RequestManageMemo(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00499f3c. The paired current L2Server request
    // owners at 0x005abce0..0x005ac350 and ReplyManMemo at 0x006b36ac
    // fix the six operations and every request/reply field.
    L2_GUARD_FUNCTION();

    constexpr int kCommunityDatabaseId = -1;
    constexpr int kMemoReplyOpcode = 0xaf;

    int characterId = 0;
    int userServerId = 0;
    int operation = 0;
    int worldId = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "dddd", &characterId, &userServerId, &operation, &worldId);

    auto sendFailure = [&]() { socket->Send("cddd", kMemoReplyOpcode, characterId, userServerId, -1); };

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        sendFailure();
        return false;
    }

    switch (operation) {
    case 1: {
        int page = 0;
        std::array<char16_t, 256> search{};
        ::Disassemble(cursor, "dS", &page, 0x200, search.data());
        if (page < 1)
            page = 1;

        int memoId = 0;
        int memoWorldId = 0;
        int insertTime = 0;
        int authorCharacterId = 0;
        std::array<char16_t, 64> authorCharacterName{};
        int authorAccountId = 0;
        std::array<char16_t, 64> authorAccountName{};
        std::array<char16_t, 128> memoTitle{};
        int memoCount = 0;
        std::int64_t nullableIndicator = -1;

        DBConn connection(kCommunityDatabaseId);
        connection.BindNullable(&memoId, &nullableIndicator);
        connection.BindNullable(&memoWorldId, &nullableIndicator);
        connection.BindNullable(&insertTime, &nullableIndicator);
        connection.BindNullable(&authorCharacterId, &nullableIndicator);
        connection.BindNullable(authorCharacterName.data(), 0x80, &nullableIndicator);
        connection.BindNullable(&authorAccountId, &nullableIndicator);
        connection.BindNullable(authorAccountName.data(), 0x80, &nullableIndicator);
        connection.BindNullable(memoTitle.data(), 0x100, &nullableIndicator);
        connection.BindNullable(&memoCount, &nullableIndicator);

        const int accountId = user->GetAccountID();
        const std::u16string databaseSearch(FixedUtf16View(search));
        constexpr const wchar_t* statement = L"{Call lin_GetMemoList (?, ? )}";
        connection.Prepare(statement);
        connection.BindParameter(&accountId);
        connection.BindParameter(databaseSearch.c_str());

        int resultRow = 0;
        int selectedCount = 0;
        int payloadLength = 0;
        std::array<char, 0x2000> payload{};
        const int pageEnd = runtime::MultiplyInt32Wrapping(page, 12);
        const int pageBegin = runtime::AddInt32Wrapping(pageEnd, -12);
        if (connection.ExecutePrepared(statement)) {
            while (connection.Fetch()) {
                if (pageBegin <= resultRow && resultRow < pageEnd) {
                    const int rowLength = ::Assemble(
                        payload.data() + payloadLength, static_cast<int>(payload.size()) - payloadLength, "ddddSdSS", memoId,
                        memoWorldId, insertTime, authorCharacterId, authorCharacterName.data(), authorAccountId,
                        authorAccountName.data(), memoTitle.data()
                    );
                    if (rowLength > 0) {
                        payloadLength += rowLength;
                        ++selectedCount;
                    }
                }
                resultRow = runtime::AddInt32Wrapping(resultRow, 1);
            }
        }

        // Shipping code reports count/12 + 1 instead of
        // a ceiling division, including zero and exact multiples of 12.
        socket->Send(
            "cdddddSdb", kMemoReplyOpcode, characterId, userServerId, operation, memoCount / 12 + 1, page, search.data(),
            selectedCount, payloadLength, payload.data()
        );
        return false;
    }

    case 2: {
        int memoId = 0;
        int page = 0;
        std::array<char16_t, 256> search{};
        ::Disassemble(cursor, "ddS", &memoId, &page, 0x200, search.data());

        int memoWorldId = 0;
        int insertTime = 0;
        int authorCharacterId = 0;
        std::array<char16_t, 64> authorCharacterName{};
        int authorAccountId = 0;
        std::array<char16_t, 64> authorAccountName{};
        std::array<char16_t, 129> memoTitle{};
        std::array<char16_t, 4001> memoText{};
        std::int64_t nullableIndicator = -1;

        bool sentReply = false;
        {
            DBConn connection(kCommunityDatabaseId);
            connection.BindNullable(&memoWorldId, &nullableIndicator);
            connection.BindNullable(&insertTime, &nullableIndicator);
            connection.BindNullable(&authorCharacterId, &nullableIndicator);
            connection.BindNullable(authorCharacterName.data(), 0x80, &nullableIndicator);
            connection.BindNullable(&authorAccountId, &nullableIndicator);
            connection.BindNullable(authorAccountName.data(), 0x80, &nullableIndicator);
            connection.BindNullable(memoTitle.data(), 0x102, &nullableIndicator);
            connection.BindNullable(memoText.data(), 0x1f42, &nullableIndicator);

            constexpr const wchar_t* statement = L"{Call lin_GetMemo (? )}";
            connection.Prepare(statement);
            connection.BindParameter(&memoId);
            if (connection.ExecutePrepared(statement) && connection.Fetch()) {
                socket->Send(
                    "cddddSddddSdSSS", kMemoReplyOpcode, characterId, userServerId, operation, page, search.data(), memoId,
                    memoWorldId, insertTime, authorCharacterId, authorCharacterName.data(), authorAccountId,
                    authorAccountName.data(), memoTitle.data(), memoText.data()
                );
                sentReply = true;
            }
        }
        if (!sentReply)
            sendFailure();
        return false;
    }

    case 3: {
        int authorCharacterId = 0;
        std::array<char16_t, 64> authorCharacterName{};
        int authorAccountId = 0;
        std::array<char16_t, 64> authorAccountName{};
        std::array<char16_t, 129> memoTitle{};
        std::array<char16_t, 4001> memoText{};
        ::Disassemble(
            cursor, "dSdSSS", &authorCharacterId, 0x80, authorCharacterName.data(), &authorAccountId, 0x80,
            authorAccountName.data(), 0x102, memoTitle.data(), 0x1f42, memoText.data()
        );

        const std::u16string databaseCharacterName(FixedUtf16View(authorCharacterName));
        const std::u16string databaseAccountName(FixedUtf16View(authorAccountName));
        const std::u16string databaseTitle(FixedUtf16View(memoTitle));
        const std::u16string databaseText(FixedUtf16View(memoText));

        int createdMemoId = 0;
        std::int64_t nullableIndicator = -1;
        DBConn connection(kCommunityDatabaseId);
        connection.BindNullable(&createdMemoId, &nullableIndicator);
        constexpr const wchar_t* statement = L"{Call lin_CreateMemo (?, ?, ?, ?, ?, ?, ? )}";
        connection.Prepare(statement);
        connection.BindParameter(&worldId);
        connection.BindParameter(&authorCharacterId);
        connection.BindParameter(databaseCharacterName.c_str());
        connection.BindParameter(&authorAccountId);
        connection.BindParameter(databaseAccountName.c_str());
        connection.BindParameter(databaseTitle.c_str());
        connection.BindParameter(databaseText.c_str());
        connection.ExecutePrepared(statement);

        // The target's helper at 0x00484b80 never calls DBConn::Fetch,
        // so the bound result remains its initialized zero in this path.
        socket->Send("cdddd", kMemoReplyOpcode, characterId, userServerId, operation, createdMemoId);
        return false;
    }

    case 4: {
        int memoId = 0;
        ::Disassemble(cursor, "d", &memoId);

        DBConn connection(kCommunityDatabaseId);
        constexpr const wchar_t* statement = L"{Call lin_DeleteMemo (?, ? )}";
        const int deleteMemo = 1;
        connection.Prepare(statement);
        connection.BindParameter(&memoId);
        connection.BindParameter(&deleteMemo);
        connection.ExecutePrepared(statement);
        socket->Send("cddd", kMemoReplyOpcode, characterId, userServerId, operation);
        return false;
    }

    case 5: {
        int memoId = 0;
        ::Disassemble(cursor, "d", &memoId);

        int memoWorldId = 0;
        int insertTime = 0;
        int authorCharacterId = 0;
        std::array<char16_t, 64> authorCharacterName{};
        int authorAccountId = 0;
        std::array<char16_t, 64> authorAccountName{};
        std::array<char16_t, 129> memoTitle{};
        std::array<char16_t, 4001> memoText{};
        std::int64_t nullableIndicator = -1;

        bool sentReply = false;
        {
            DBConn connection(kCommunityDatabaseId);
            connection.BindNullable(&memoWorldId, &nullableIndicator);
            connection.BindNullable(&insertTime, &nullableIndicator);
            connection.BindNullable(&authorCharacterId, &nullableIndicator);
            connection.BindNullable(authorCharacterName.data(), 0x80, &nullableIndicator);
            connection.BindNullable(&authorAccountId, &nullableIndicator);
            connection.BindNullable(authorAccountName.data(), 0x80, &nullableIndicator);
            connection.BindNullable(memoTitle.data(), 0x102, &nullableIndicator);
            connection.BindNullable(memoText.data(), 0x1f42, &nullableIndicator);

            constexpr const wchar_t* statement = L"{Call lin_GetMemo (? )}";
            connection.Prepare(statement);
            connection.BindParameter(&memoId);
            if (connection.ExecutePrepared(statement) && connection.Fetch()) {
                socket->Send(
                    "cdddddddSdSSS", kMemoReplyOpcode, characterId, userServerId, operation, memoId, memoWorldId, insertTime,
                    authorCharacterId, authorCharacterName.data(), authorAccountId, authorAccountName.data(),
                    memoTitle.data(), memoText.data()
                );
                sentReply = true;
            }
        }
        if (!sentReply)
            sendFailure();
        return false;
    }

    case 6: {
        int memoId = 0;
        std::array<char16_t, 129> memoTitle{};
        std::array<char16_t, 4001> memoText{};
        ::Disassemble(cursor, "dSS", &memoId, 0x102, memoTitle.data(), 0x1f42, memoText.data());

        const std::u16string databaseTitle(FixedUtf16View(memoTitle));
        const std::u16string databaseText(FixedUtf16View(memoText));
        DBConn connection(kCommunityDatabaseId);
        constexpr const wchar_t* statement = L"{Call lin_UpdateMemo (?,  ?, ? )}";
        connection.Prepare(statement);
        connection.BindParameter(&memoId);
        connection.BindParameter(databaseTitle.c_str());
        connection.BindParameter(databaseText.c_str());
        connection.ExecutePrepared(statement);
        socket->Send("cddd", kMemoReplyOpcode, characterId, userServerId, operation);
        return false;
    }

    default:
        sendFailure();
        return false;
    }
}

bool RequestManPledgeBBS(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00477380. The paired current L2Server
    // CDB::RequestClanBBSView and ReplyManPledgeBBS fix the
    // root `ddd`, option-2 `dd` trailer and reply field order.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int userServerId = 0;
    int option = 0;
    const std::uint8_t* const cursor = ::Disassemble(packet, "ddd", &characterId, &userServerId, &option);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        CPledgeBBS pledgeBbs;
        pledgeBbs.DoPledgeBBS(characterId, userServerId, option, cursor, socket);
    }
    return false;
}

bool RequestManorBuySeed(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004774e8. The paired current L2Server sender
    // fixes the root as character ID, server ID, manor ID, residence ID,
    // total price and item count. CWareHouse::BuySeed re-reads the same root.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int userServerId = 0;
    int manorId = 0;
    int residenceId = 0;
    std::int64_t totalPrice = 0;
    int itemCount = 0;
    ::Disassemble(packet, "ddddQd", &characterId, &userServerId, &manorId, &residenceId, &totalPrice, &itemCount);

    auto sendFailure = [&](int result) {
        socket->Send(
            "cdddddQd", 0xb1, result, characterId, userServerId, manorId, residenceId, static_cast<long long>(totalPrice),
            itemCount
        );
    };

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x336d, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x336d, characterId)
        L2_LOG_ERROR(Log, "[manor] Seed purchase for character %d at manor %d failed: character unavailable. Reply code 1.", characterId, manorId);
        sendFailure(1);
        return false;
    }

    CManorSP manor = g_manorDb.GetManor(manorId);
    if (!manor) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find manor, id[%d]", "CacheSocket.cpp", 0x3375, manorId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find manor, id[%d]", "CacheSocket.cpp", 0x3375, manorId)
        L2_LOG_ERROR(Log, "[manor] Seed purchase for character %d failed: manor %d unavailable. Reply code 2.", characterId, manorId);
        sendFailure(2);
        return false;
    }

    CWareHouseSP userWarehouse = user->GetWareHouse(0, true);
    if (!userWarehouse) {
        // Diagnostic addition: this failure previously returned only a packet code.
        L2_LOG_ERROR(Log, "[manor] Seed purchase for character %d at manor %d failed: inventory unavailable. Reply code 3.", characterId, manorId);
        sendFailure(3);
        return false;
    }
    CWareHouseSP manorWarehouse = manor->GetWareHouse();
    if (!manorWarehouse) {
        // Diagnostic addition: this failure previously returned only a packet code.
        L2_LOG_ERROR(Log, "[manor] Seed purchase for character %d at manor %d failed: manor warehouse unavailable. Reply code 4.", characterId, manorId);
        sendFailure(4);
        return false;
    }

    CWareHouse* const userWarehouseRaw = userWarehouse.get();
    CWareHouse* const manorWarehouseRaw = manorWarehouse.get();
    CWareHouse* firstLock = userWarehouseRaw;
    CWareHouse* secondLock = manorWarehouseRaw;
    const std::uint64_t userLockAddress = userWarehouseRaw->GetLockAddr();
    const std::uint64_t manorLockAddress = manorWarehouseRaw->GetLockAddr();
    if (userLockAddress == manorLockAddress) {
        // Log.Add(LOG_ERROR, "Same warehouse double writelock?-_-,")
        // L2_LOG_ERROR(Log, "Same warehouse double writelock?-_-,")
        L2_LOG_ERROR(Log, "[manor] Seed purchase for character %d at manor %d encountered equal warehouse lock addresses. Processing continues with one lock.", characterId, manorId);
        secondLock = nullptr;
    } else if (manorLockAddress < userLockAddress) {
        firstLock = manorWarehouseRaw;
        secondLock = userWarehouseRaw;
    }

    {
        L2_WRITE_GUARD(*firstLock);
        auto executeRequest = [&]() {
            userWarehouseRaw->BuySeed(socket, packet);
        };
        if (secondLock) {
            L2_WRITE_GUARD(*secondLock);
            executeRequest();
        } else {
            executeRequest();
        }
    }
    return false;
}

bool RequestManorSellCrop(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00477e28. The paired current L2Server
    // CDB::RequestProcureCrop at 0x0064f124 fixes all five scalar roles and
    // the trailing row buffer. Its ReplyManorSellCrop at 0x006b4394 fixes
    // the response identities independently.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int userServerId = 0;
    int manorId = 0;
    int residenceId = 0;
    int itemCount = 0;
    ::Disassemble(packet, "ddddd", &characterId, &userServerId, &manorId, &residenceId, &itemCount);

    auto sendFailure = [&](int result) {
        socket->Send("cdddddd", 0xb2, result, characterId, userServerId, manorId, residenceId, itemCount);
    };

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x33ab, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0x33ab, characterId)
        L2_LOG_ERROR(Log, "[manor] Crop sale for character %d at manor %d failed: character unavailable. Reply code 1.", characterId, manorId);
        sendFailure(1);
        return false;
    }

    CManorSP manor = g_manorDb.GetManor(manorId);
    if (!manor) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find manor, id[%d]", "CacheSocket.cpp", 0x33b3, manorId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find manor, id[%d]", "CacheSocket.cpp", 0x33b3, manorId)
        L2_LOG_ERROR(Log, "[manor] Crop sale for character %d failed: manor %d unavailable. Reply code 2.", characterId, manorId);
        sendFailure(2);
        return false;
    }

    CWareHouseSP userWarehouse = user->GetWareHouse(0, true);
    if (!userWarehouse) {
        // Diagnostic addition: this failure previously returned only a packet code.
        L2_LOG_ERROR(Log, "[manor] Crop sale for character %d at manor %d failed: inventory unavailable. Reply code 3.", characterId, manorId);
        sendFailure(3);
        return false;
    }
    CWareHouseSP manorWarehouse = manor->GetWareHouse();
    if (!manorWarehouse) {
        // Diagnostic addition: this failure previously returned only a packet code.
        L2_LOG_ERROR(Log, "[manor] Crop sale for character %d at manor %d failed: manor warehouse unavailable. Reply code 4.", characterId, manorId);
        sendFailure(4);
        return false;
    }

    CWareHouse* const userWarehouseRaw = userWarehouse.get();
    CWareHouse* const manorWarehouseRaw = manorWarehouse.get();
    CWareHouse* firstLock = userWarehouseRaw;
    CWareHouse* secondLock = manorWarehouseRaw;
    const std::uint64_t userLockAddress = userWarehouseRaw->GetLockAddr();
    const std::uint64_t manorLockAddress = manorWarehouseRaw->GetLockAddr();
    if (userLockAddress == manorLockAddress) {
        // Log.Add(LOG_ERROR, "Same warehouse double writelock?-_-,")
        // L2_LOG_ERROR(Log, "Same warehouse double writelock?-_-,")
        L2_LOG_ERROR(Log, "[manor] Crop sale for character %d at manor %d encountered equal warehouse lock addresses. Processing continues with one lock.", characterId, manorId);
        secondLock = nullptr;
    } else if (manorLockAddress < userLockAddress) {
        firstLock = manorWarehouseRaw;
        secondLock = userWarehouseRaw;
    }

    {
        L2_WRITE_GUARD(*firstLock);
        auto executeRequest = [&]() {
            userWarehouseRaw->SellCrop(socket, packet);
        };
        if (secondLock) {
            L2_WRITE_GUARD(*secondLock);
            executeRequest();
        } else {
            executeRequest();
        }
    }
    return false;
}

bool RequestManorMoveCrop(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00478738. The paired current L2Server sender
    // 0x0064f250 and its sole production caller CManor::TransferManorData
    // 0x00821d7c fix the root as manor, residence, pledge and row count.
    L2_GUARD_FUNCTION();

    int manorId = 0;
    int residenceId = 0;
    int pledgeId = 0;
    int itemCount = 0;
    ::Disassemble(packet, "dddd", &manorId, &residenceId, &pledgeId, &itemCount);

    CManorSP manor = g_manorDb.GetManor(manorId);
    if (!manor) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find manor, id[%d]", "CacheSocket.cpp", 0x33e7, manorId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find manor, id[%d]", "CacheSocket.cpp", 0x33e7, manorId)
        L2_LOG_ERROR(Log, "[manor] Cannot transfer crops from manor %d to clan %d: manor unavailable. No reply is sent.", manorId, pledgeId);
        return false;
    }

    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find pledge, id[%d]", "CacheSocket.cpp", 0x33ee, pledgeId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find pledge, id[%d]", "CacheSocket.cpp", 0x33ee, pledgeId)
        L2_LOG_ERROR(Log, "[manor] Cannot transfer crops from manor %d to clan %d: clan unavailable. No reply is sent.", manorId, pledgeId);
        return false;
    }

    CWareHouseSP pledgeWarehouse = pledge->GetWareHouse();
    if (!pledgeWarehouse) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find pledge warehouse, id[%d]", "CacheSocket.cpp", 0x33f5, pledgeId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find pledge warehouse, id[%d]", "CacheSocket.cpp", 0x33f5, pledgeId
        // )
        L2_LOG_ERROR(Log, "[manor] Cannot transfer crops from manor %d to clan %d: clan warehouse unavailable. No reply is sent.", manorId, pledgeId);
        return false;
    }

    pledgeWarehouse->AddProcuredCrop(socket, packet);
    return false;
}

bool RequestSaveManorNextSeed(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00478a84. The paired current L2Server
    // CManor::SaveNextSeedInfo sender fixes the second root value as the
    // residence ID. The CManor member consumes the complete packet itself.
    L2_GUARD_FUNCTION();

    int manorId = 0;
    int residenceId = 0;
    ::Disassemble(packet, "dd", &manorId, &residenceId);

    CManorSP manor = g_manorDb.GetManor(manorId);
    if (!manor) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find manor, id[%d]", "CacheSocket.cpp", 0x341f, manorId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find manor, id[%d]", "CacheSocket.cpp", 0x341f, manorId)
        L2_LOG_ERROR(Log, "[manor] Cannot save next seed settings for manor %d, residence %d: manor unavailable. Request skipped.", manorId, residenceId);
        return false;
    }

    CWareHouseSP warehouse = manor->GetWareHouse();
    if (!warehouse) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find manor warehouse, id[%d]", "CacheSocket.cpp", 0x3426, manorId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find manor warehouse, id[%d]", "CacheSocket.cpp", 0x3426, manorId
        // )
        L2_LOG_ERROR(Log, "[manor] Cannot save next seed settings for manor %d, residence %d: manor warehouse unavailable. Request skipped.", manorId, residenceId);
        return false;
    }

    {
        L2_WRITE_GUARD(*warehouse.operator->());
        manor->SetNextManorSeed(socket, packet);
    }
    return false;
}

bool RequestSaveManorNowSeed(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00478d30. Paired current producer:
    // L2Server CManor::SaveNowSeedInfo.
    L2_GUARD_FUNCTION();

    int manorId = 0;
    int residenceId = 0;
    ::Disassemble(packet, "dd", &manorId, &residenceId);

    CManorSP manor = g_manorDb.GetManor(manorId);
    if (!manor) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find manor, id[%d]", "CacheSocket.cpp", 0x3457, manorId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find manor, id[%d]", "CacheSocket.cpp", 0x3457, manorId)
        L2_LOG_ERROR(Log, "[manor] Cannot save current seed settings for manor %d, residence %d: manor unavailable. Request skipped.", manorId, residenceId);
        return false;
    }

    CWareHouseSP warehouse = manor->GetWareHouse();
    if (!warehouse) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find manor warehouse, id[%d]", "CacheSocket.cpp", 0x345e, manorId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find manor warehouse, id[%d]", "CacheSocket.cpp", 0x345e, manorId
        // )
        L2_LOG_ERROR(Log, "[manor] Cannot save current seed settings for manor %d, residence %d: manor warehouse unavailable. Request skipped.", manorId, residenceId);
        return false;
    }

    {
        L2_WRITE_GUARD(*warehouse.operator->());
        manor->SetNowManorSeed(socket, packet);
    }
    return false;
}

bool RequestSaveManorInfo(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00478fdc. Paired current producer:
    // L2Server CManor::SaveNowManorInfo.
    L2_GUARD_FUNCTION();

    int manorId = 0;
    int residenceId = 0;
    ::Disassemble(packet, "dd", &manorId, &residenceId);

    CManorSP manor = g_manorDb.GetManor(manorId);
    if (!manor) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find manor, id[%d]", "CacheSocket.cpp", 0x347f, manorId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find manor, id[%d]", "CacheSocket.cpp", 0x347f, manorId)
        L2_LOG_ERROR(Log, "[manor] Cannot save current settings for manor %d, residence %d: manor unavailable. Request skipped.", manorId, residenceId);
        return false;
    }

    CWareHouseSP warehouse = manor->GetWareHouse();
    if (!warehouse) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find manor warehouse, id[%d]", "CacheSocket.cpp", 0x3486, manorId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find manor warehouse, id[%d]", "CacheSocket.cpp", 0x3486, manorId
        // )
        L2_LOG_ERROR(Log, "[manor] Cannot save current settings for manor %d, residence %d: manor warehouse unavailable. Request skipped.", manorId, residenceId);
        return false;
    }

    {
        L2_WRITE_GUARD(*warehouse.operator->());
        manor->SetNowManorInfo(socket, packet);
    }
    return false;
}

bool RequestLoadAllManor(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00479288. The paired current
    // CManorDB::LoadFromDB assembles the counted manor-ID buffer.
    L2_GUARD_FUNCTION();

    std::uint32_t manorCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "d", &manorCount);
    for (std::uint32_t index = 0; index < manorCount; ++index) {
        int manorId = 0;
        cursor = ::Disassemble(cursor, "d", &manorId);
        CManorSP manor = g_manorDb.GetManor(manorId);
        if (manor)
            manor->SendManorInfoAndData(socket);
    }
    return false;
}

bool RequestLoadEventPoint(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0049ac4c. The complete counted request loop,
    // nullable double result, SQL call and opcode 0xb4 reply are fixed by the
    // current machine image. No compatible C1 handler exists at this opcode.
    L2_GUARD_FUNCTION();

    std::array<char, 0x5000> response{};
    std::uint32_t groupCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "d", &groupCount);
    int responseSize = ::Assemble(response.data(), static_cast<int>(response.size()), "d", groupCount);

    DBConn connection(runtime::process::CurrentDatabaseId());
    std::int64_t nullableIndicator = -1;
    for (std::uint32_t index = 0; index < groupCount; ++index) {
        unsigned int groupId = 0;
        double groupPoint = 0.0;
        cursor = ::Disassemble(cursor, "d", &groupId);

        connection.BindNullable(&groupPoint, &nullableIndicator);
        constexpr const wchar_t* statement = L"{Call lin_GetGroupPoint (? )}";
        connection.Prepare(statement);
        connection.BindParameter(&groupId);
        if (connection.ExecutePrepared(statement)) {
            connection.Fetch();
        }

        responseSize += ::Assemble(
            response.data() + responseSize, static_cast<int>(response.size()) - responseSize, "df", groupId, groupPoint
        );
        connection.UnBind();
    }

    socket->Send("cb", 0xb4, responseSize, response.data());
    return false;
}

bool RequestSaveEventPoint(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x0049ae9c. This is append-only: the
    // shipping lin_CreateGroupPoint call runs once for every positive-count
    // row and produces no reply.
    L2_GUARD_FUNCTION();

    int groupCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "d", &groupCount);
    DBConn connection(runtime::process::CurrentDatabaseId());
    if (groupCount > 0) {
        for (int index = 0; index < groupCount; ++index) {
            unsigned int groupId = 0;
            double groupPoint = 0.0;
            cursor = ::Disassemble(cursor, "df", &groupId, &groupPoint);

            constexpr const wchar_t* statement = L"{Call lin_CreateGroupPoint (?, ? )}";
            connection.Prepare(statement);
            connection.BindParameter(&groupId);
            connection.BindParameter(&groupPoint);
            connection.ExecutePrepared(statement);
            connection.UnBind();
        }
    }
    return false;
}

bool RequestBBSClanAction(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004793bc. Both request variants and the two
    // opcode-0xb5 reply layouts are fixed by the paired current
    // L2Server CDB senders and ReplyClanBBSList consumer.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int userServerId = 0;
    int requestContext = 0;
    int action = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "dddd", &characterId, &userServerId, &requestContext, &action);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return false;

    if (action == 1) {
        int currentPage = 0;
        int totalPages = 0;
        int filter = 0;
        std::array<char16_t, 0x80> searchText{};
        int rowCount = 0;
        cursor = ::Disassemble(cursor, "dddSd", &currentPage, &totalPages, &filter, 0x100, searchText.data(), &rowCount);

        int validRowCount = 0;
        int rowBytes = 0;
        std::array<char, 0x1c00> rows{};
        int rowKey = 0;
        int pledgeId = 0;
        std::array<char16_t, 25> pledgeName{};
        std::array<char16_t, 25> rulerName{};
        while (rowCount > 0) {
            --rowCount;
            cursor = ::Disassemble(cursor, "ddSS", &rowKey, &pledgeId, 0x32, pledgeName.data(), 0x32, rulerName.data());

            CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, true, runtime::process::CurrentDatabaseId());
            if (pledge) {
                const int assembled = ::Assemble(
                    rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "ddddSS", rowKey, pledgeId,
                    pledge->skillLevel(), static_cast<int>(pledge->GetMemberCount()), pledgeName.data(), rulerName.data()
                );
                if (assembled != 0)
                    rowBytes += assembled;
                ++validRowCount;
            }
        }

        socket->Send(
            "cdddddddSdb", 0xb5, characterId, userServerId, requestContext, action, currentPage, totalPages, filter,
            searchText.data(), validRowCount, rowBytes, rows.data()
        );
    } else if (action == 2) {
        int adminRequest = 0;
        int pledgeId = 0;
        std::array<char16_t, 0x200> adminText{};
        int adminValue = 0;
        int trailingBytes = 0;
        const std::uint8_t* const trailingData =
            ::Disassemble(cursor, "ddSdd", &adminRequest, &pledgeId, 0x400, adminText.data(), &adminValue, &trailingBytes);

        int skillLevel = 0;
        int memberCount = 0;
        int allianceId = 0;
        std::array<char16_t, 24> pledgeName{};
        std::array<char16_t, 24> rulerName{};
        std::array<char16_t, 25> allianceName{};

        CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, true, runtime::process::CurrentDatabaseId());
        if (pledge) {
            skillLevel = pledge->skillLevel();
            memberCount = static_cast<int>(pledge->GetMemberCount());
            std::copy_n(pledge->GetName(), pledgeName.size(), pledgeName.begin());
            // The shipping function performs this copy even though its final
            // packet uses the current user's name instead of rulerName.
            std::copy_n(pledge->rullerName(), rulerName.size(), rulerName.begin());
            allianceId = static_cast<int>(pledge->allianceId());
            if (allianceId != 0) {
                CAllianceSP alliance = g_allianceDb.GetAlliance(
                    static_cast<std::uint32_t>(allianceId), static_cast<std::uint32_t>(runtime::process::CurrentDatabaseId())
                );
                if (alliance) {
                    std::copy_n(alliance->name(), allianceName.size(), allianceName.begin());
                }
            }
        }

        const std::u16string characterName = std::u16string(user->GetCharName());
        socket->Send(
            "cddddddSdSdSSdddb", 0xb5, characterId, userServerId, requestContext, action, adminRequest, pledgeId,
            pledgeName.data(), characterId, characterName.c_str(), allianceId, allianceName.data(), adminText.data(),
            skillLevel, memberCount, adminValue, trailingBytes, trailingData
        );
    }
    return false;
}

bool RequestModAllianceName(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0045fa98. The paired current L2Server
    // CDB::RequestModifyAllianceName and ReplyModAllianceNamePacket fix the
    // dS request, result value 2 and complete opcode-0xb6 cddS reply.
    L2_GUARD_FUNCTION();

    std::uint32_t allianceId = 0;
    std::array<char16_t, 25> targetNewName{};
    ::Disassemble(packet, "dS", &allianceId, 0x32, targetNewName.data());

    bool success = false;
    if (allianceId != 0 && targetNewName[0] != u'\0') {
        std::wstring newName = FixedUtf16ToWide(targetNewName);
        success = g_allianceDb.ModifyAllianceName(allianceId, newName.data());
    }

    socket->Send("cddS", 0xb6, static_cast<int>(allianceId), success ? 2 : 0, targetNewName.data());
    return false;
}

bool RequestCreateSubJob(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0049b058. The request literal and both reply
    // layouts are current-image constants. The paired current L2Server
    // senders/consumer fix their field order.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    int characterId = 0;
    int mode = 0;
    int requestContext1 = 0;
    int requestContext2 = 0;
    int newSubjobId = 0;
    int newClass = 0;
    int oldSubjobId = 0;
    double hp = 0.0;
    double mp = 0.0;
    std::int64_t sp = 0;
    std::int64_t experience = 0;
    int level = 0;
    int henna1 = 0;
    int henna2 = 0;
    int henna3 = 0;
    int abilityPoints = 0;
    ::Disassemble(
        packet, "ddddddddffQQddddd", &userServerId, &characterId, &mode, &requestContext1, &requestContext2, &newSubjobId,
        &newClass, &oldSubjobId, &hp, &mp, &sp, &experience, &level, &henna1, &henna2, &henna3, &abilityPoints
    );

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdddddd", 0xb8, userServerId, mode, requestContext1, requestContext2, 0, newClass);
        return false;
    }

    const CVitalData oldVitality = user->GetVitalData();
    const int oldClass = user->GetClass();

    // Current helper 0x00433684 uses SQL_INTEGER for the two identifiers and
    // SQL_C_SLONG -> SQL_DOUBLE for the four vitality values.
    DBConn vitalityConnection(runtime::process::CurrentDatabaseId());
    const long oldVitalPoint = oldVitality.vitalPoint;
    const long oldRestoreCount = oldVitality.restoreCount;
    const long oldResetDateVP = oldVitality.latestResetDate_VP;
    const long oldResetDateRC = oldVitality.latestResetDate_RC;
    constexpr const wchar_t* vitalityStatement = L"{Call lin_SaveCharVitality (?, ?, ?, ?, ?, ?)}";
    vitalityConnection.Prepare(vitalityStatement);
    vitalityConnection.BindParameter(&characterId);
    vitalityConnection.BindParameter(&oldSubjobId);
    vitalityConnection.BindParameter(&oldVitalPoint);
    vitalityConnection.BindParameter(&oldRestoreCount);
    vitalityConnection.BindParameter(&oldResetDateVP);
    vitalityConnection.BindParameter(&oldResetDateRC);
    vitalityConnection.ExecutePrepared(vitalityStatement);

    const int result = user->CreateSubJob(
        newSubjobId, newClass, oldSubjobId, hp, mp, sp, experience, level, henna1, henna2, henna3, abilityPoints
    );
    user->LoadUserVitality();
    const CVitalData newVitality = user->GetVitalData();

    socket->Send(
        "cdddddddddd", 0xb8, userServerId, mode, requestContext1, requestContext2, result, newClass,
        static_cast<int>(newVitality.vitalPoint), static_cast<int>(newVitality.restoreCount),
        static_cast<int>(newVitality.latestResetDate_VP), static_cast<int>(newVitality.latestResetDate_RC)
    );

    const std::u16string characterName = std::u16string(user->GetCharName());
    const std::u16string accountName = std::u16string(user->GetAccountName());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x1fa5, user->GetId(), user->GetAccountID(), 0, 0, user->GetX(), user->GetY(), user->GetZ(), nullptr, nullptr,
        nullptr, oldSubjobId, oldClass, newSubjobId, newClass, oldVitality.vitalPoint, newVitality.vitalPoint,
        oldVitality.restoreCount, newVitality.restoreCount, 0, 0, characterName.c_str(), accountName.c_str(), nullptr,
        nullptr, 0
    );
    return false;
}

bool RequestDeleteSubJob(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00479aa4. Paired current L2Server sender
    // 0x00650408 and ReplyDeleteSubJob 0x006b4f9c fix all three fields and
    // the exact cddd reply.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    int characterId = 0;
    int subjobId = 0;
    ::Disassemble(packet, "ddd", &userServerId, &characterId, &subjobId);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cddd", 0xb9, userServerId, subjobId, 0);
        return false;
    }

    const bool deleted = user->DeleteSubJob(subjobId);
    socket->Send("cddd", 0xb9, userServerId, subjobId, deleted ? 1 : 0);
    if (deleted) {
        WriteDeleteSubJobAudit(*user.get(), subjobId);
    }
    return false;
}

bool RequestChangeSubJob(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0049dc60. Both current paired senders use the
    // complete 20-field payload. Mode/context fields are echoed unchanged.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    int characterId = 0;
    int mode = 0;
    int requestContext1 = 0;
    int requestContext2 = 0;
    int newSubjobId = 0;
    int oldSubjobId = 0;
    double hp = 0.0;
    double mp = 0.0;
    std::int64_t sp = 0;
    std::int64_t experience = 0;
    int level = 0;
    int henna1 = 0;
    int henna2 = 0;
    int henna3 = 0;
    int oldVitalPoint = 0;
    int oldRestoreCount = 0;
    int oldResetDateVP = 0;
    int oldResetDateRC = 0;
    int abilityPoints = 0;
    ::Disassemble(
        packet, "dddddddffQQddddddddd", &userServerId, &characterId, &mode, &requestContext1, &requestContext2, &newSubjobId,
        &oldSubjobId, &hp, &mp, &sp, &experience, &level, &henna1, &henna2, &henna3, &oldVitalPoint, &oldRestoreCount,
        &oldResetDateVP, &oldResetDateRC, &abilityPoints
    );

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send(
            "cddddddffQQdddddd", 0xba, userServerId, mode, requestContext1, requestContext2, newSubjobId, 0, hp, mp, sp,
            experience, abilityPoints, level, henna1, henna2, henna3, 0
        );
        return false;
    }

    // Current helper 0x00433724 binds all six values as ordinary integers.
    // this differs from CreateSubJob helper 0x00433684.
    DBConn vitalityConnection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* vitalityStatement = L"{Call lin_SaveCharVitality (?, ?, ?, ?, ?, ?)}";
    vitalityConnection.Prepare(vitalityStatement);
    vitalityConnection.BindParameter(&characterId);
    vitalityConnection.BindParameter(&oldSubjobId);
    vitalityConnection.BindParameter(&oldVitalPoint);
    vitalityConnection.BindParameter(&oldRestoreCount);
    vitalityConnection.BindParameter(&oldResetDateVP);
    vitalityConnection.BindParameter(&oldResetDateRC);
    vitalityConnection.ExecutePrepared(vitalityStatement);

    // The target only writes this output on a fetched transition row. Its
    // failure reply otherwise exposes an indeterminate stack scalar. Zero is
    // the deterministic portable value used instead of reproducing a leak.
    int oldClass = 0;
    const bool changed = user->ChangeSubJob(
        newSubjobId, oldSubjobId, &hp, &mp, &sp, &experience, &level, &henna1, &henna2, &henna3, oldClass, abilityPoints
    );

    user->LoadUserVitality();
    const CVitalData newVitality = user->GetVitalData();

    int potentialPoint1 = 0;
    int potentialPoint2 = 0;
    int potentialPoint3 = 0;
    if (user->GetDualClassId() == newSubjobId || newSubjobId == 0) {
        DBConn potentialConnection(runtime::process::CurrentDatabaseId());
        potentialConnection.Bind(&potentialPoint1);
        potentialConnection.Bind(&potentialPoint2);
        potentialConnection.Bind(&potentialPoint3);
        constexpr const wchar_t* potentialStatement = L"{Call lin_GetAcquirePotentialPoint (?, ?)}";
        potentialConnection.Prepare(potentialStatement);
        potentialConnection.BindParameter(&characterId);
        potentialConnection.BindParameter(&newSubjobId);
        if (!potentialConnection.ExecutePrepared(potentialStatement) || !potentialConnection.Fetch()) {
            // Log.Add(
            //     LOG_ERROR,
            //     "qsGetAcquirePotentialPoint error "
            //     "(RequestChangeSubJob)"
            // )
            // L2_LOG_ERROR(Log,
            //     "qsGetAcquirePotentialPoint error "
            //     "(RequestChangeSubJob)"
            // )
            L2_LOG_ERROR(Log, "[subclass] Cannot load potential points for character %d, subclass %d: query failed or no row fetched. Subclass-change result remains %d; reply will use point values %d, %d, %d.",
                characterId, newSubjobId, changed ? 1 : 0, potentialPoint1, potentialPoint2, potentialPoint3);
        }
    }

    socket->Send(
        "cddddddffQQdddddddddddddd", 0xba, userServerId, mode, requestContext1, requestContext2, newSubjobId,
        changed ? 1 : 0, hp, mp, sp, experience, abilityPoints, level, henna1, henna2, henna3, oldSubjobId, oldClass,
        static_cast<int>(newVitality.vitalPoint), static_cast<int>(newVitality.restoreCount),
        static_cast<int>(newVitality.latestResetDate_VP), static_cast<int>(newVitality.latestResetDate_RC), potentialPoint1,
        potentialPoint2, potentialPoint3
    );

    const std::u16string characterName = std::u16string(user->GetCharName());
    const std::u16string accountName = std::u16string(user->GetAccountName());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x1fa5, user->GetId(), user->GetAccountID(), 0, 0, user->GetX(), user->GetY(), user->GetZ(), nullptr, nullptr,
        nullptr, oldSubjobId, oldClass, newSubjobId, user->GetClass(), oldVitalPoint, oldRestoreCount,
        newVitality.vitalPoint, newVitality.restoreCount, 0, 0, characterName.c_str(), accountName.c_str(), nullptr, nullptr,
        0
    );

    CPledgeJoinDB::Instance().ModifyUserInfoOnPledgeJoinDB(characterId, level, user->GetClass());
    return false;
}

bool RequestLoadSubJob(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00479fa0. The common handler reads the seven-field
    // NPC request shape. Paired ordinary RequestLoadSubJob sends the same
    // prefix without context4, exactly as in the shipping pair.
    L2_GUARD_FUNCTION();

    int userServerId = 0;
    int characterId = 0;
    int mode = 0;
    int requestContext1 = 0;
    int requestContext2 = 0;
    int requestContext3 = 0;
    int requestContext4 = 0;
    ::Disassemble(
        packet, "ddddddd", &userServerId, &characterId, &mode, &requestContext1, &requestContext2, &requestContext3,
        &requestContext4
    );

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send(
            "cddddddd", 0xbb, userServerId, mode, requestContext1, requestContext2, requestContext3, requestContext4, 0
        );
        return false;
    }

    std::array<char, 0x5000> payload{};
    int payloadLength = static_cast<int>(payload.size());
    const int count = user->GetSubJobListBuffer(payload.data(), &payloadLength);
    if (count == 0) {
        socket->Send(
            "cddddddd", 0xbb, userServerId, mode, requestContext1, requestContext2, requestContext3, requestContext4, 0
        );
        return false;
    }

    socket->Send(
        "cdddddddb", 0xbb, userServerId, mode, requestContext1, requestContext2, requestContext3, requestContext4, count,
        payloadLength, payload.data()
    );
    return false;
}

bool EventAmountPacket(CSocket* socket [[maybe_unused]], const std::uint8_t* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0045c220 has only the guard bookkeeping and a
    // false return. It reads neither object and changes no event state.
    L2_GUARD_FUNCTION();
    return false;
}

bool RequestSaveCharacterForSubJob(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x0047a258 and paired current L2Server sender
    // 0x00650f18 fix the complete 17-value payload after the opcode.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int subjobId = 0;
    int characterClass = 0;
    int dualClassId = 0;
    double hp = 0.0;
    double mp = 0.0;
    double maxHp = 0.0;
    double maxMp = 0.0;
    double cp = 0.0;
    double maxCp = 0.0;
    std::int64_t sp = 0;
    std::int64_t experience = 0;
    int level = 0;
    int henna1 = 0;
    int henna2 = 0;
    int henna3 = 0;
    int abilityPoints = 0;
    ::Disassemble(
        packet, "ddddffffffQQddddd", &characterId, &subjobId, &characterClass, &dualClassId, &hp, &mp, &maxHp, &maxMp, &cp,
        &maxCp, &sp, &experience, &level, &henna1, &henna2, &henna3, &abilityPoints
    );

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "cannot find user[%d] (save character for subjob)", characterId)
        // L2_LOG_ERROR(Log, "cannot find user[%d] (save character for subjob)", characterId)
        L2_LOG_ERROR(Log, "[subclass] Cannot save subclass %d for character %d: character could not be loaded. Request skipped.", subjobId, characterId);
        return false;
    }

    if (!user->SaveForSubJob(
            subjobId, characterClass, dualClassId, hp, mp, maxHp, maxMp, cp, maxCp, sp, experience, level, henna1, henna2,
            henna3, abilityPoints
        )) {
        // Log.Add(LOG_ERROR, "cannot save user info[%d] for subjob", characterId)
        // L2_LOG_ERROR(Log, "cannot save user info[%d] for subjob", characterId)
        L2_LOG_ERROR(Log, "[subclass] Saving subclass %d for character %d failed (class %d, dual-class ID %d). No reply is sent.", subjobId, characterId, characterClass, dualClassId);
    }
    return false;
}

bool RequestReloadSkill(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0047a514. Paired current sender 0x006510d8 fixes
    // {user socket ID, character database ID, subjob ID}.
    L2_GUARD_FUNCTION();

    int userSocketId = 0;
    int characterId = 0;
    int subjobId = 0;
    ::Disassemble(packet, "ddd", &userSocketId, &characterId, &subjobId);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "cannot find user[%d] (reload skill)", characterId)
        // L2_LOG_ERROR(Log, "cannot find user[%d] (reload skill)", characterId)
        // L2_LOG_ERROR(Log,
        //     "[skills] Cannot reload main-class skills for character %d: character could not be loaded. No skill reply is sent.",
        //     characterId)
        L2_LOG_ERROR(Log,
            "[skills] Cannot reload skills for character %d, subclass %d: character could not be loaded. No skill reply is sent.",
            characterId, subjobId);
        return false;
    }

    if (!user->LoadAquireSkill(subjobId)) {
        // The shipping diagnostic passes the character ID despite its skill
        // placeholder. Retain that observable argument.
        // Log.Add(LOG_ERROR, "cannot load skill[%d] (reload skill)", characterId)
        // L2_LOG_ERROR(Log, "cannot load skill[%d] (reload skill)", characterId)
        L2_LOG_ERROR(Log, "[skills] Loading skills for character %d, subclass %d failed. No skill reply is sent.", characterId, subjobId);
        return false;
    }

    user->SendReloadSkill(socket, static_cast<unsigned int>(userSocketId), subjobId, false, true);
    user->SendAcquiredPotentialSkill(
        socket, static_cast<unsigned int>(characterId), static_cast<unsigned int>(userSocketId)
    );
    return false;
}

bool RequestSaveAcquiredSkills(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x0047ae68. Disassemble returns the first byte after
    // the three-dword header. The remaining count records are raw `ddd` rows.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int subjobId = 0;
    int count = 0;
    const std::uint8_t* rows = ::Disassemble(packet, "ddd", &characterId, &subjobId, &count);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "cannot find user[%d] (save acquires skills)", characterId)
        // L2_LOG_ERROR(Log, "cannot find user[%d] (save acquires skills)", characterId)
        L2_LOG_ERROR(Log, "[skills] Cannot save acquired skills for character %d, subclass %d (requested count %d): character could not be loaded. Request skipped.", characterId, subjobId, count);
        return false;
    }

    user->ResetAcquiresSkillsFromBuffer(subjobId, count, rows);
    return false;
}

bool RequestSavePunishmentHistory(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x0049b778. Paired current CDB sender 0x00645730.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int logType = 0;
    std::array<char16_t, 25> targetCharacterName{};
    std::array<char16_t, 15> targetAccountName{};
    ::Disassemble(packet, "ddSS", &characterId, &logType, 0x32, targetCharacterName.data(), 0x1e, targetAccountName.data());

    const std::u16string characterName(FixedUtf16View(targetCharacterName));
    const std::u16string accountName(FixedUtf16View(targetAccountName));

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_AddPunishmentHistory (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(characterName.c_str());
    connection.BindParameter(&characterId);
    connection.BindParameter(&logType);
    connection.BindParameter(accountName.c_str());
    connection.ExecutePrepared(statement);
    return false;
}

bool RequestLoadSupendPledge(CSocket* socket, const std::uint8_t* packet [[maybe_unused]]) {
    // `Supend` is the spelling kept by the current PDB. Cached.exe
    // 0x0049141c reads no request payload and returns opcode 0xc4 in batches
    // of at most 200 `dddd` rows.
    L2_GUARD_FUNCTION();

    int winnerPledgeId = 0;
    int loserPledgeId = 0;
    int warEndTime = 0;
    int warEndType = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&winnerPledgeId);
    connection.Bind(&loserPledgeId);
    connection.Bind(&warEndTime);
    connection.Bind(&warEndType);

    const int now = runtime::crt::Time32Now();
    constexpr const wchar_t* statement = L"{Call lin_GetSuspendPledges (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&now);
    if (!connection.ExecutePrepared(statement)) {
        return false;
    }

    bool fullBatch = false;
    do {
        fullBatch = false;
        int rowCount = 0;
        int payloadLength = 0;
        std::array<char, 0x2004> payload{};
        while (rowCount != 200) {
            if (!connection.Fetch())
                break;
            payloadLength += ::Assemble(
                payload.data() + payloadLength, static_cast<int>(payload.size()) - payloadLength, "dddd", winnerPledgeId,
                loserPledgeId, warEndTime, warEndType
            );
            ++rowCount;
        }
        if (rowCount == 200)
            fullBatch = true;
        socket->Send("cdb", 0xc4, rowCount, payloadLength, payload.data());
    } while (fullBatch);
    return false;
}

bool RequestLoadMainClassSkill(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0047a774. Paired current sender 0x00650a6c.
    L2_GUARD_FUNCTION();

    int userSocketId = 0;
    int characterId = 0;
    ::Disassemble(packet, "dd", &userSocketId, &characterId);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "cannot find user[%d] (reload skill)", characterId)
        // L2_LOG_ERROR(Log, "cannot find user[%d] (reload skill)", characterId)
        L2_LOG_ERROR(Log, "[skills] Cannot load main-class skills for character %d: character could not be loaded. No skill reply is sent.", characterId);
        return false;
    }

    user->SendMainClassSkill(socket, static_cast<unsigned int>(userSocketId));
    return false;
}

bool RequestDeletePledgeSkill(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    // Current Cached.exe 0x0047a8e0. Paired current sender 0x006458e8.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int skillId = 0;
    ::Disassemble(packet, "dd", &characterId, &skillId);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, char id[%d]", "CacheSocket.cpp", 0x3744, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, char id[%d]", "CacheSocket.cpp", 0x3744, characterId)
        L2_LOG_ERROR(Log,
            "[skills] Cannot delete clan skill %d for character %d: character could not be loaded. Request skipped.",
            skillId, characterId);
        return false;
    }

    const int previousSkillLevelSet = user->GetAcquireSkillLevelSet(skillId);
    user->DeletePledgeSkill(skillId);
    WriteDeleteSkillAudit(*user.get(), skillId, previousSkillLevelSet);
    return false;
}

bool RequestCharacterLogin(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int sessionLogFlag = 0;
    int sessionLogSource = 0;
    int address0 = 0;
    int address1 = 0;
    int address2 = 0;
    int address3 = 0;
    std::uint64_t macAddress = 0;
    ::Disassemble(
        packet, "dddddddQ", &characterId, &sessionLogFlag, &sessionLogSource, &address0, &address1, &address2, &address3,
        &macAddress
    );
    if (std::getenv("L2_QUEST_TRACE") != nullptr) {
        // std::fprintf(
        //     stderr,
        //     "[QUEST_TRACE] CacheD::RequestCharacterLogin character=%d "
        //     "world=%d\n",
        //     characterId,
        //     runtime::process::CurrentDatabaseId())
        L2_LOG_DIRECT(stderr, runtime::logging::Level::Info,
            "[quest trace] Character login requested for DB ID %d in database %d.",
            characterId, runtime::process::CurrentDatabaseId());
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0xc11, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0xc11, characterId)
        L2_LOG_ERROR(Log,
            "[session] Cannot process login for character %d: character could not be loaded. Login bookkeeping skipped.",
            characterId);
        return false;
    }

    user->SetAddr(address0, address1, address2, address3);
    // Current RequestCharacterLogin writes these three fields directly
    // before calling CUser::WriteLogin. No portable session carrier exists.
    user->m_sessionMacAddress = macAddress;
    user->m_sessionLogFlag = sessionLogFlag == 1;
    user->m_sessionLogSource = sessionLogSource;
    user->WriteLogin(characterId);
    if (std::getenv("L2_QUEST_TRACE") != nullptr) {
        // std::fprintf(
        //     stderr,
        //     "[QUEST_TRACE] CacheD::RequestCharacterLogin completed "
        //     "character=%d logged_in=%d\n",
        //     characterId,
        //     user->m_loggedIn ? 1 : 0)
        L2_LOG_DIRECT(stderr, runtime::logging::Level::Info,
            "[quest trace] Login processing returned for character DB ID %d; logged-in flag: %s.",
            characterId, user->m_loggedIn ? "yes" : "no");
    }
    return false;
}

bool RequestCharacterLogout(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int weekPlayTime = 0;
    int logoutReason = 0;
    SYSTEMTIME lastSaveTime{};
    int address0 = 0;
    int address1 = 0;
    int address2 = 0;
    int address3 = 0;
    std::uint64_t macAddress = 0;
    ::Disassemble(
        packet, "dddbddddQ", &characterId, &weekPlayTime, &logoutReason, static_cast<int>(sizeof(lastSaveTime)),
        &lastSaveTime, &address0, &address1, &address2, &address3, &macAddress
    );

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find user, id[%d][%d]", "CacheSocket.cpp", 0xc66, characterId, logoutReason
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d][%d]", "CacheSocket.cpp", 0xc66, characterId, logoutReason
        // )
        L2_LOG_ERROR(Log,
            "[session] Cannot process logout for character %d (reason %d): character could not be loaded. Logout persistence and item checks skipped.",
            characterId, logoutReason);
        return false;
    }

    CWareHouseSP inventory = user->GetWareHouse(0, true);
    if (inventory) {
        if (inventory->m_unsavedItemsPending) {
            {
                L2_WRITE_GUARD(*inventory.operator->());
                inventory->SaveUnsavedItems();
            }
        }
        if (inventory->m_lateSavingItemsPending) {
            {
                L2_WRITE_GUARD(*inventory.operator->());
                inventory->SaveLateSavingItems();
            }
        }
    }

    // The current handler reads the logged-in byte directly. It compares the
    // four packet integers with the kept byte values before changing only
    // the raw MAC carrier and entering CUser::WriteLogout.
    if (user->m_loggedIn) {
        if (address0 != static_cast<int>(user->m_sessionAddress[0]) ||
            address1 != static_cast<int>(user->m_sessionAddress[1]) ||
            address2 != static_cast<int>(user->m_sessionAddress[2]) ||
            address3 != static_cast<int>(user->m_sessionAddress[3])) {
            WriteLogoutAddressMismatchAudit(*user.get(), user->m_sessionAddress, address0, address1, address2, address3);
        }
        user->m_sessionMacAddress = macAddress;
        user->WriteLogout(characterId, logoutReason);
    }

    CAccountSP account = CAccount::Load(static_cast<unsigned int>(user->GetAccountID()));
    if (account && weekPlayTime >= 0) {
        account->SaveWeekPlayTime(weekPlayTime, &lastSaveTime);
    }

    if (!user->ItemCheckBetweenMemoryAndDB()) {
        user->OnItemCheckFailed(3);
    } else {
        user->SetCheckSumOnDB();
    }
    return false;
}

bool RequestQuestSet(CSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int characterId = 0;
    const std::uint8_t* questPacket = ::Disassemble(packet, "d", &characterId);
    if (std::getenv("L2_QUEST_TRACE") != nullptr) {
        // std::fprintf(
        //     stderr,
        //     "[QUEST_TRACE] CacheD::RequestQuestSet character=%d\n",
        //     characterId)
        L2_LOG_DIRECT(stderr, runtime::logging::Level::Info,
            "[quest trace] Quest update requested for character DB ID %d.",
            characterId);
    }
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0xc83, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0xc83, characterId)
        L2_LOG_ERROR(Log,
            "[quests] Cannot update quests for character %d: character could not be loaded. No completion reply is sent.",
            characterId);
        return false;
    }

    user->SetQuest(questPacket, characterId);
    if (std::getenv("L2_QUEST_TRACE") != nullptr) {
        // std::fprintf(
        //     stderr,
        //     "[QUEST_TRACE] CacheD::RequestQuestSet reply character=%d\n",
        //     characterId)
        L2_LOG_DIRECT(stderr, runtime::logging::Level::Info,
            "[quest trace] Quest update processing returned for character DB ID %d; preparing the reply.",
            characterId);
    }
    socket->Send("chd", 0xc6, 0xfd, characterId);
    return false;
}

bool RequestSetAgitOwner(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int agitId = 0;
    int pledgeId = 0;
    ::Disassemble(packet, "dd", &agitId, &pledgeId);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetAgitOwner2 (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&agitId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "Agit owner setting fail.Agit id=[%d], pledge id = [%d]", agitId, pledgeId)
        // L2_LOG_ERROR(Log, "Agit owner setting fail.Agit id=[%d], pledge id = [%d]", agitId, pledgeId)
        L2_LOG_ERROR(Log,
            "[residence] Clan-hall owner update failed for hall %d and clan %d. Cached clan ownership is unchanged.",
            agitId, pledgeId);
        return false;
    }

    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (pledge) {
        pledge->setAgitId(static_cast<std::uint32_t>(agitId));

        // The target copies the fixed 24-wide-character pledge name before
        // resolving the ruler. CPledge stores the same target UTF-16 units.
        std::array<char16_t, 24> pledgeName{};
        std::copy_n(pledge->GetName(), pledgeName.size(), pledgeName.begin());

        CUserSP ruler =
            g_userDb.GetUser(static_cast<int>(pledge->rullerId()), true, false, runtime::process::CurrentDatabaseId());
        if (ruler) {
            WriteSetAgitOwnerAudit(*ruler.get(), pledgeName.data(), pledgeId, agitId);
        }
    }

    // Log.Add(LOG_NORMAL, "Agit owner setting success. Agit id=[%d], pledge id = [%d].", agitId, pledgeId)
    // L2_LOG_INFO(Log, "Agit owner setting success. Agit id=[%d], pledge id = [%d].", agitId, pledgeId)
    L2_LOG_INFO(Log,
        "[residence] Clan-hall owner update procedure completed for hall %d and clan %d.",
        agitId, pledgeId);
    return false;
}

bool CSocketHandler::OnRequestSetCastleOwner(
    int pledgeWorldId,
    int previousPledgeWorldId,
    int castleId,
    int pledgeId,
    int previousPledgeId,
    int castleType,
    CSocket* socket
) {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetCastleOwner2 (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledgeWorldId);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&castleId);
    connection.BindParameter(&castleType);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR, "Castle owner setting fail.Castle id=[%d], pledge id = [%d]", castleId, pledgeId
        // )
        // L2_LOG_ERROR(Log, "Castle owner setting fail.Castle id=[%d], pledge id = [%d]", castleId, pledgeId
        // )
        L2_LOG_ERROR(Log,
            "[residence] Castle owner update failed for castle %d and clan %d in world %d (castle type %d). Cached clan ownership is unchanged.",
            castleId, pledgeId, pledgeWorldId, castleType);
        return true;
    }

    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, pledgeWorldId);
    if (pledge) {
        pledge->setCastleId(static_cast<std::uint32_t>(castleId));

        std::array<char16_t, 24> pledgeName{};
        std::copy_n(pledge->GetName(), pledgeName.size(), pledgeName.begin());
        CUserSP ruler = g_userDb.GetUser(static_cast<int>(pledge->rullerId()), true, false, pledge->GetWorldId());
        if (ruler) {
            WriteSetCastleOwnerAudit(*ruler.get(), pledgeName.data(), pledgeId, castleId, castleType);
        }
    }

    // Log.Add(
    //     LOG_NORMAL,
    //     "Castle owner setting success. Castle id=[%d], "
    //     "pledgeid=[%d], pledgeWorldId=[%d]",
    //     castleId, pledgeId, pledgeWorldId
    // )
    // L2_LOG_INFO(Log,
    //     "Castle owner setting success. Castle id=[%d], "
    //     "pledgeid=[%d], pledgeWorldId=[%d]",
    //     castleId, pledgeId, pledgeWorldId
    // )
    L2_LOG_INFO(Log,
        "[residence] Castle owner update procedure completed for castle %d and clan %d in world %d (castle type %d).",
        castleId, pledgeId, pledgeWorldId, castleType);

    if (pledgeId != previousPledgeId && pledgeWorldId == previousPledgeWorldId) {
        // The current path uses request TLS for this old-pledge
        // lookup, instead of the decoded pledgeWorldId used above.
        CPledgeSP previousPledge = g_pledgeDb.GetPledge(previousPledgeId, false, runtime::process::CurrentDatabaseId());
        if (previousPledge && previousPledge->SetCastleSiegeDefenceCount(0)) {
            socket->Send("chdd", 0xc6, 0x54, previousPledgeId, 0);
        }
    }
    return true;
}

bool RequestSetCastleOwner(CSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int castleId = 0;
    int pledgeId = 0;
    int previousPledgeId = 0;
    int castleType = 0;
    int pledgeWorldId = 0;
    int previousPledgeWorldId = 0;
    ::Disassemble(
        packet, "dddddd", &castleId, &pledgeId, &previousPledgeId, &castleType, &pledgeWorldId, &previousPledgeWorldId
    );
    if (pledgeWorldId < 1) {
        pledgeWorldId = GetCacheDConfiguration().worldId;
    }

    g_socketHandler.OnRequestSetCastleOwner(
        pledgeWorldId, previousPledgeWorldId, castleId, pledgeId, previousPledgeId, castleType, socket
    );
    // Current RequestSetCastleOwner 0x0045f1c0 executes `xor al, al`
    // immediately after the delegated call. The delegate's true return is
    // therefore not a CPacketEx close request.
    return false;
}

bool RequestLoadCastles(CSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int castleCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "d", &castleCount);
    for (int index = 0; index < castleCount; ++index) {
        std::uint32_t castleId = 0;
        std::array<char16_t, 256> targetCastleName{};
        cursor = ::Disassemble(cursor, "dS", &castleId, 0x200, targetCastleName.data());

        // Disassemble keeps the target's two-byte WCHAR packet ABI. The
        // recovered method signature remains host wchar_t at this boundary.
        const std::wstring castleName = Utf16ToWide(std::u16string_view(targetCastleName.data()));
        g_castleDb.SendReplyToLoadCastle(socket, castleId, castleName.c_str());
    }
    return false;
}

bool CSocket::RequestLoadAgits(CSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    constexpr int kLoadAgitReply = 0x15;
    constexpr int kPayloadCapacity = 0x2004;

    int agitCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "d", &agitCount);
    for (int index = 0; index < agitCount; ++index) {
        std::uint32_t agitId = 0;
        std::array<char16_t, 256> targetAgitName{};
        cursor = ::Disassemble(cursor, "dS", &agitId, 0x200, targetAgitName.data());

        const std::u16string agitName(std::u16string_view(targetAgitName.data()));

        std::uint32_t pledgeId = 0;
        int hpStove = 0;
        int hpStoveExpire = 0;
        int mpFlame = 0;
        int mpFlameExpire = 0;
        int hatcher = 0;
        int status = 0;
        int nextWarTime = 0;
        int auctionId = 0;
        int auctionTime = 0;
        long long lastPrice = 0;
        int lastCancel = 0;
        long long minimumPrice = 0;
        int teleportLevel = 0;
        int teleportExpire = 0;
        std::array<char16_t, 256> auctionDescription{};
        int nextCost = 0;
        int costFail = 0;
        int taxRate = 0;
        int taxRateToChange = 0;
        int taxChildRate = 0;
        int taxChildRateToChange = 0;
        long long shopIncome = 0;
        long long shopIncomeTemp = 0;

        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Bind(&pledgeId);
        connection.Bind(&hpStove);
        connection.Bind(&hpStoveExpire);
        connection.Bind(&mpFlame);
        connection.Bind(&mpFlameExpire);
        connection.Bind(&hatcher);
        connection.Bind(&status);
        connection.Bind(&nextWarTime);
        connection.Bind(&auctionId);
        connection.Bind(&auctionTime);
        connection.Bind(&lastPrice);
        connection.Bind(&lastCancel);
        connection.Bind(&minimumPrice);
        connection.Bind(&teleportLevel);
        connection.Bind(&teleportExpire);
        connection.Bind(auctionDescription.data(), 0x200);
        connection.Bind(&nextCost);
        connection.Bind(&costFail);
        connection.Bind(&taxRate);
        connection.Bind(&taxRateToChange);
        connection.Bind(&taxChildRate);
        connection.Bind(&taxChildRateToChange);
        connection.Bind(&shopIncome);
        connection.Bind(&shopIncomeTemp);

        constexpr const wchar_t* loadStatement = L"{Call lin_LoadAgit (?)}";
        connection.Prepare(loadStatement);
        connection.BindParameter(&agitId);
        const bool loaded = connection.ExecutePrepared(loadStatement) && connection.Fetch();
        if (!loaded) {
            connection.UnBind();
            constexpr const wchar_t* createStatement = L"{Call lin_CreateAgitIfNotExist (?, ?)}";
            connection.Prepare(createStatement);
            connection.BindParameter(&agitId);
            connection.BindParameter(agitName.c_str());
            if (!connection.ExecutePrepared(createStatement)) {
                // Log.Add(
                //     LOG_ERROR, "Cannot Create Agit %s[%d]", WideToUtf8(agitName).c_str(), static_cast<int>(agitId)
                // )
                // L2_LOG_ERROR(Log, "Cannot Create Agit %s[%d]", WideToUtf8(agitName).c_str(), static_cast<int>(agitId)
                // )
                // L2_LOG_ERROR(Log, "[residence] Could not create missing clan hall %d (\"%s\") after loading failed. Reply uses default values, loading continues.",
                //     static_cast<int>(agitId), WideToUtf8(agitName).c_str())
                L2_LOG_ERROR(Log, "[residence] Could not create missing clan hall %d (\"%s\") after loading failed. Reply uses default values; loading continues.",
                    static_cast<int>(agitId), Utf16ToUtf8(agitName).c_str());
            }

            socket->Send(
                "cdddddddddddQdQddSddddddQQdd", kLoadAgitReply, static_cast<std::int32_t>(agitId), 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, static_cast<std::int64_t>(0), 0, static_cast<std::int64_t>(0), 0, 0, u"", 0, 0, 0, 0, 0, 0,
                static_cast<std::int64_t>(0), static_cast<std::int64_t>(0), 0, 0
            );
            continue;
        }

        connection.UnBind();
        int decoType = 0;
        int decoId = 0;
        int decoLevel = 0;
        int decoExpire = 0;
        int decoActive = 0;
        std::int64_t nullableIndicator = -1;
        connection.Bind(&decoType);
        connection.Bind(&decoId);
        connection.BindNullable(&decoLevel, &nullableIndicator);
        connection.BindNullable(&decoExpire, &nullableIndicator);
        connection.BindNullable(&decoActive, &nullableIndicator);

        std::array<char, kPayloadCapacity> payload{};
        int payloadLength = 0;
        int decoCount = 0;
        constexpr const wchar_t* decoStatement = L"{Call lin_LoadAgitDeco (?)}";
        connection.Prepare(decoStatement);
        connection.BindParameter(&agitId);
        if (connection.ExecutePrepared(decoStatement)) {
            while (connection.Fetch()) {
                if (decoActive == 1) {
                    payloadLength += ::Assemble(
                        payload.data() + payloadLength, static_cast<int>(payload.size()) - payloadLength, "dddd", decoType,
                        decoId, decoLevel, decoExpire
                    );
                    ++decoCount;
                }
            }
        }

        int npcCount = 0;
        payloadLength += AppendAgitDecoNpcs(
            payload.data() + payloadLength, static_cast<int>(payload.size()) - payloadLength, agitId, npcCount
        );

        if (decoCount < 1 && npcCount < 1) {
            socket->Send(
                "cdddddddddddQdQddSddddddQQdd", kLoadAgitReply, static_cast<std::int32_t>(agitId),
                static_cast<std::int32_t>(pledgeId), hpStove, hpStoveExpire, mpFlame, mpFlameExpire, hatcher, status,
                nextWarTime, auctionId, auctionTime, static_cast<std::int64_t>(lastPrice), lastCancel,
                static_cast<std::int64_t>(minimumPrice), teleportLevel, teleportExpire, auctionDescription.data(),
                nextCost, costFail, taxRate, taxRateToChange, taxChildRate, taxChildRateToChange,
                static_cast<std::int64_t>(shopIncome), static_cast<std::int64_t>(shopIncomeTemp), decoCount, npcCount
            );
        } else {
            socket->Send(
                "cdddddddddddQdQddSddddddQQddb", kLoadAgitReply, static_cast<std::int32_t>(agitId),
                static_cast<std::int32_t>(pledgeId), hpStove, hpStoveExpire, mpFlame, mpFlameExpire, hatcher, status,
                nextWarTime, auctionId, auctionTime, static_cast<std::int64_t>(lastPrice), lastCancel,
                static_cast<std::int64_t>(minimumPrice), teleportLevel, teleportExpire, auctionDescription.data(),
                nextCost, costFail, taxRate, taxRateToChange, taxChildRate, taxChildRateToChange,
                static_cast<std::int64_t>(shopIncome), static_cast<std::int64_t>(shopIncomeTemp), decoCount, npcCount,
                payloadLength, payload.data()
            );
        }
    }
    return false;
}

bool RequestWriteCastleTax(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int isCastle = 0;
    int residenceId = 0;
    int pledgeId = 0;
    int taxType = 0;
    int taxRate = 0;
    int toChange = 0;
    ::Disassemble(packet, "dddddd", &isCastle, &residenceId, &pledgeId, &taxType, &taxRate, &toChange);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_WriteCastleTax (?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&isCastle);
    connection.BindParameter(&taxType);
    connection.BindParameter(&taxRate);
    connection.BindParameter(&toChange);
    connection.BindParameter(&residenceId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Try Castle[%d] tax set [%d]%%, "
        //     "tochange rate[%d]%%. but fail.",
        //     residenceId, taxRate, toChange
        // )
        // L2_LOG_ERROR(Log,
        //     "Try Castle[%d] tax set [%d]%%, "
        //     "tochange rate[%d]%%. but fail.",
        //     residenceId, taxRate, toChange
        // )
        L2_LOG_ERROR(Log,
            "[residence] Tax update failed for residence %d (castle flag %d, tax type %d, rate %d%%, pending rate %d%%). No reply is sent.",
            residenceId, isCastle, taxType, taxRate, toChange);
        return false;
    }

    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (pledge) {
        std::array<char16_t, 24> pledgeName{};
        std::copy_n(pledge->GetName(), pledgeName.size(), pledgeName.begin());

        CUserSP ruler =
            g_userDb.GetUser(static_cast<int>(pledge->rullerId()), true, false, runtime::process::CurrentDatabaseId());
        if (ruler) {
            WriteCastleTaxAudit(*ruler.get(), pledgeName.data(), pledgeId, residenceId, taxRate);
        }
    }

    // Log.Add(
    //     LOG_NORMAL,
    //     "Castle[%d] tax setted [%d]%%, "
    //     "tochange rate[%d]%%.",
    //     residenceId, taxRate, toChange
    // )
    // L2_LOG_INFO(Log,
    //     "Castle[%d] tax setted [%d]%%, "
    //     "tochange rate[%d]%%.",
    //     residenceId, taxRate, toChange
    // )
    L2_LOG_INFO(Log,
        "[residence] Tax update procedure completed for residence %d (castle flag %d, tax type %d, rate %d%%, pending rate %d%%).",
        residenceId, isCastle, taxType, taxRate, toChange);
    return false;
}

bool UseItemPacket(CSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int itemId = 0;
    int consumeType = 0;
    std::int64_t requestedAmount = 0;
    int characterId = 0;
    ::Disassemble(packet, "ddQd", &itemId, &consumeType, &requestedAmount, &characterId);

    auto sendFailure = [&]() {
        int decodedItemId = 0;
        int decodedConsumeType = 0;
        std::int64_t decodedAmount = 0;
        int decodedCharacterId = 0;
        int itemServerId = 0;
        int userServerId = 0;
        int responseFlag = 0;
        ::Disassemble(
            packet, "ddQdddd", &decodedItemId, &decodedConsumeType, &decodedAmount, &decodedCharacterId, &itemServerId,
            &userServerId, &responseFlag
        );
        socket->Send("cddQd", 16, userServerId, itemServerId, static_cast<std::int64_t>(-1), 0);
    };

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0xba2, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0xba2, characterId)
        L2_LOG_ERROR(Log,
            "[items] Cannot use item %d for character %d: character could not be loaded. Sending failure reply.",
            itemId, characterId);
        sendFailure();
        return false;
    }

    CWareHouseSP inventory = user->GetWareHouse(WareHouseType_Inventory, true);
    if (!inventory) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find warehouse [0], id[%d]", "CacheSocket.cpp", 0xbb3, characterId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find warehouse [0], id[%d]", "CacheSocket.cpp", 0xbb3, characterId
        // )
        L2_LOG_ERROR(Log,
            "[items] Cannot use item %d for character %d: inventory is unavailable. Sending failure reply.",
            itemId, characterId);
        sendFailure();
        return false;
    }

    {
        L2_WRITE_GUARD(*inventory.operator->());
        inventory->UseItem(socket, packet);
    }
    return false;
}

bool RequestManipulateItem(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00482f0c.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    const std::uint8_t* payload = ::Disassemble(packet, "d", &characterId);

    auto sendFailure = [&]() {
        int responseId = 0;
        int responseId2 = 0;
        ::Disassemble(payload, "dd", &responseId, &responseId2);
        socket->Send("cdddd", 0x16, responseId, responseId2, 0, 0);
    };

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0xf22, characterId)
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find user, id[%d]", "CacheSocket.cpp", 0xf22, characterId)
        L2_LOG_ERROR(Log,
            "[items] Cannot process item changes for character %d: character could not be loaded. Sending failure reply.",
            characterId);
        sendFailure();
        return false;
    }

    CWareHouseSP inventory = user->GetWareHouse(WareHouseType_Inventory, true);
    if (!inventory) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find warehouse [0], id[%d]", "CacheSocket.cpp", 0xf2e, characterId
        // )
        // L2_LOG_ERROR(Log, "[%s][%d]Cannot find warehouse [0], id[%d]", "CacheSocket.cpp", 0xf2e, characterId
        // )
        L2_LOG_ERROR(Log,
            "[items] Cannot process item changes for character %d: inventory is unavailable. Sending failure reply.",
            characterId);
        sendFailure();
        return false;
    }

    int responseId = 0;
    int responseId2 = 0;
    std::vector<EtcMani*> etcManis;
    CPacketHelperEx<20480> itemPacket;
    CPacketHelperEx<20480> etcManiPacket;
    unsigned int changedItemCount = 0;
    std::uint64_t processedEtcManiCount = 0;

    {
        L2_WRITE_GUARD(*inventory.operator->());
        const bool success = inventory->ManipulateItems(
            socket, payload, responseId, responseId2, etcManis, itemPacket, etcManiPacket, changedItemCount,
            processedEtcManiCount
        );
        if (success) {
            socket->Send(
                "cddddbb", 0x16, responseId, responseId2, static_cast<int>(processedEtcManiCount),
                static_cast<int>(changedItemCount), etcManiPacket.GetLength(), etcManiPacket.GetPacket(), itemPacket.GetLength(),
                itemPacket.GetPacket()
            );
        } else {
            socket->Send("cdddd", 0x16, responseId, responseId2, 0, 0);
        }
    }
    return false;
}

bool RequestShortCutInfoPacket(CSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int userSocketId = 0;
    int characterId = 0;
    int subjobId = 0;
    ::Disassemble(packet, "ddd", &userSocketId, &characterId, &subjobId);

    int slotNumber = 0;
    int packedShortcutType = 0;
    int shortcutId = 0;
    std::array<char16_t, 0x100> shortcutMacro{};
    std::array<char, 0x5000> assembledShortcuts{};
    int shortcutCount = 0;
    int assembledSize = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&slotNumber);
    connection.Bind(&packedShortcutType);
    connection.Bind(&shortcutId);
    connection.Bind(shortcutMacro.data(), 0x200);

    constexpr const wchar_t* statement = L"{Call lin_GetShortCut (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&subjobId);
    connection.ExecutePrepared(statement);

    while (connection.Fetch()) {
        const std::uint32_t typeBits = static_cast<std::uint32_t>(packedShortcutType);
        const int type = static_cast<int>(typeBits & 0xffffU);
        const int typeParameter = static_cast<int>(typeBits >> 16U);
        const int macroId = WindowsWtol(Utf16ToWide(shortcutMacro.data()));
        const int written = ::Assemble(
            assembledShortcuts.data() + assembledSize, static_cast<int>(assembledShortcuts.size()) - assembledSize, "ddddd",
            slotNumber, type, shortcutId, typeParameter, macroId
        );
        if (written > 0) {
            assembledSize += written;
        }
        // Count each fetched row, even when it does not fit in the fixed buffer.
        ++shortcutCount;
    }

    socket->Send("cddb", 0x0b, userSocketId, shortcutCount, assembledSize, assembledShortcuts.data());
    return false;
}

bool RequestSetShortCutPacket(CSocket* socket [[maybe_unused]], const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int subjobId = 0;
    int slotNumber = 0;
    int shortcutType = 0;
    int shortcutId = 0;
    int shortcutTypeParameter = 0;
    int shortcutMacro = 0;
    ::Disassemble(
        packet, "ddddddd", &characterId, &subjobId, &slotNumber, &shortcutType, &shortcutId, &shortcutTypeParameter,
        &shortcutMacro
    );

    DBConn connection(runtime::process::CurrentDatabaseId());

    const std::uint32_t packedTypeBits =
        static_cast<std::uint32_t>(shortcutType) | (static_cast<std::uint32_t>(shortcutTypeParameter) << 16U);
    const int packedShortcutType = runtime::Int32FromBits(packedTypeBits);

    std::array<wchar_t, 0x1e> shortcutMacroText{};
    std::swprintf(shortcutMacroText.data(), shortcutMacroText.size(), L"%d", shortcutMacro);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user) {
        {
            L2_WRITE_GUARD(*user.operator->());
            constexpr const wchar_t* statement = L"{Call lin_SetShortCut (?, ?, ?, ?, ?, ?)}";
            connection.Prepare(statement);
            connection.BindParameter(&characterId);
            connection.BindParameter(&subjobId);
            connection.BindParameter(&slotNumber);
            connection.BindParameter(&packedShortcutType);
            connection.BindParameter(&shortcutId);
            connection.BindParameter(shortcutMacroText.data());
            connection.ExecutePrepared(statement);
        }
    }

    return false;
}

bool RequestBuilderAccountList(CSocket* socket, const std::uint8_t* packet [[maybe_unused]]) {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    std::array<char, 0x20> accountName{};
    std::array<char, 0x5000> assembledAccounts{};
    int accountCount = 0;
    int assembledSize = 0;
    connection.Bind(accountName.data(), 0x14);
    constexpr const wchar_t* statement = L"{Call lin_SelectTop200BuilderAccount}";
    connection.Prepare(statement);
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            const int written = ::Assemble(
                assembledAccounts.data() + assembledSize, static_cast<int>(assembledAccounts.size()) - assembledSize, "s",
                accountName.data()
            );
            if (written > 0) {
                ++accountCount;
                assembledSize += written;
            }
            if (accountCount >= 0xc9) {
                break;
            }
        }
    }

    socket->Send("cdb", 0x4d, accountCount, assembledSize, assembledAccounts.data());
    return false;
}

bool CacheVersionPacket(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004606b4.
    L2_GUARD_FUNCTION();

    int serverRevision = 0;
    const std::uint8_t* const payload = ::Disassemble(packet, "d", &serverRevision);
    if (serverRevision == -1) {
        return ServerStopper(socket, payload);
    }

    std::array<char, 0x400> fileInfo{};
    const int fileInfoSize =
        ScriptFileUtil::CacheToServer::AssembleFileInfo(fileInfo.data(), static_cast<int>(fileInfo.size()));
    // The unnamed current helper at 0x0045f144 (123 bytes, SHA-256
    // 15902b341ad2e190cb5b37569395911a02d0cb1f416772960d8736f0451cd804)
    // performs exactly this revision/pending-logout result selection.
    int result = 1;
    if (serverRevision == kCurrentCacheRevision) {
        result = g_nPendingLogoutCharacters.load(std::memory_order_acquire) > 0 ? 2 : 0;
    }
    socket->Send("cddb", 0, result, g_readFile.GetItemCount(), fileInfoSize, fileInfo.data());

    if (result == 0) {
        g_nPendingLogoutCharacters.store(0, std::memory_order_release);
        g_pActiveServerSocket.store(socket, std::memory_order_release);
        RequestBuilderAccountList(socket, nullptr);
        // Log.Add(
        //     LOG_ERROR,
        //     "[NO_ERROR] Cache Version is succeeded : "
        //     "[RESULT:%d] protocol [SERVER:%d] [CACHED:%d]",
        //     result, serverRevision, kCurrentCacheRevision
        // )
        // Log.Add(LOG_NORMAL,
        //     "[L2Server] Protocol revision %d accepted. This connection is now registered as the active game server (required revision %d).",
        //     serverRevision, kCurrentCacheRevision)
        L2_LOG_INFO(Log,
            "[L2Server] Protocol revision %d accepted; this connection is now registered as the active game server (required revision %d).",
            serverRevision, kCurrentCacheRevision);
        return false;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     "Cache Version failed : [RESULT:%d] protocol "
    //     "[SERVER:%d] [CACHED:%d]",
    //     result, serverRevision, kCurrentCacheRevision
    // )
    // Log.Add(LOG_WARN,
    //     "[L2Server] Connection rejected: %s (peer revision %d, required %d, result %d). Closing the connection.",
    //     result == 2 ? "character logout from the previous connection is still pending" : "protocol revision mismatch", serverRevision, kCurrentCacheRevision, result)
    L2_LOG_WARN(Log,
        "[L2Server] Connection rejected: %s (peer revision %d, required %d, result %d); closing the connection.",
        result == 2 ? "character logout from the previous connection is still pending" : "protocol revision mismatch", serverRevision, kCurrentCacheRevision, result);
    return true;
}
