#include "Portable/Lu4Protocol.h"

#include "Portable/ClientProtocolRuntime.h"
#include "L2Server/src/Creature.h"
#include "L2Server/src/NPC.h"
#include "L2Server/src/ParserExpData.h"
#include "L2Server/src/UserSocket.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/LittleEndian.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Socket.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string>

namespace lu4 {
namespace {

// LU4 client opcodes.
constexpr std::uint8_t kClientVersion = 0x00;
constexpr std::uint8_t kClientAuthLogin = 0x01;
constexpr std::uint8_t kClientCharacterSelect = 0x04;
constexpr std::uint8_t kClientEnterWorld = 0x0b;
constexpr std::uint8_t kClientMove = 0x0c;
constexpr std::uint8_t kClientValidatePosition = 0x0d;
constexpr std::uint8_t kClientNetPing = 0x03;
constexpr std::uint8_t kClientAction = 0x0f;
constexpr std::uint8_t kClientNewCharacter = 0x05;
constexpr std::uint8_t kClientCharacterCreate = 0x07;
constexpr std::uint8_t kClientCharacterList = 0x09;
constexpr std::uint8_t kClientAcquireSkillInfo = 0x73;
constexpr std::uint8_t kClientAcquireSkill = 0x7c;
constexpr std::uint8_t kClientSay = 0x0e;
constexpr std::uint8_t kClientAttack = 0x10;
constexpr std::uint8_t kClientShortcutRegister = 0x12;
constexpr std::uint8_t kClientShortcutDelete = 0x13;
constexpr std::uint8_t kClientItemList = 0x17;
constexpr std::uint8_t kClientUseItem = 0x19;
constexpr std::uint8_t kClientDropItem = 0x1d;
constexpr std::uint8_t kClientMagicSkillUse = 0x39;
constexpr std::uint8_t kClientActionUse = 0x56;
constexpr std::uint8_t kClientRestartPoint = 0x7d;
constexpr std::uint8_t kClientTargetCancel = 0x11;
constexpr std::uint8_t kClientLinkHtml = 0x22;
constexpr std::uint8_t kClientBypass = 0x23;

// This server's client opcodes (UserSocket.cpp packet table).
constexpr std::uint8_t kNativeVersion = 0x0e;
constexpr std::uint8_t kNativeLogin = 0x2b;
constexpr std::uint8_t kNativeCharacterSelect = 0x12;
constexpr std::uint8_t kNativeEnterWorld = 0x11;
constexpr std::uint8_t kNativeMove = 0x0f;
constexpr std::uint8_t kNativeValidatePosition = 0x59;
constexpr std::uint8_t kNativeAction = 0x1f;
constexpr std::uint8_t kNativeNewCharacter = 0x13;
constexpr std::uint8_t kNativeCharacterCreate = 0x0c;
constexpr std::uint8_t kNativeClientEx = 0xd0;
constexpr std::uint8_t kNativeSayRequest = 0x49;
constexpr std::uint8_t kNativeBuilderCommand = 0x74;
constexpr std::uint8_t kNativeAttackRequest = 0x01;
constexpr std::uint8_t kNativeShortcutRegister = 0x3d;
constexpr std::uint8_t kNativeShortcutDelete = 0x3f;
constexpr std::uint8_t kNativeItemListRequest = 0x14;
constexpr std::uint8_t kNativeUseItem = 0x19;
constexpr std::uint8_t kNativeDropItem = 0x17;
constexpr std::uint8_t kNativeNetPing = 0xb1;
constexpr std::uint8_t kNativeTargetCancel = 0x48;

// This server's packets to the client.
constexpr std::uint8_t kNativeCharacterList = 0x09;
constexpr std::uint8_t kNativeLoginResult = 0x0a;
constexpr std::uint8_t kNativeCharacterSelected = 0x0b;
constexpr std::uint8_t kNativeStatusUpdate = 0x18;
constexpr std::uint8_t kNativeKeyPacket = 0x2e;
constexpr std::uint8_t kNativeMoveToLocation = 0x2f;
constexpr std::uint8_t kNativeStopMove = 0x47;
constexpr std::uint8_t kNativeMoveToPawn = 0x72;
constexpr std::uint8_t kNativeValidateLocation = 0x79;
constexpr std::uint8_t kNativeNpcHtml = 0x19;
constexpr std::uint8_t kNativeTargetSelected = 0x23;
constexpr std::uint8_t kNativeTargetUnselected = 0x24;
constexpr std::uint8_t kNativeMyTargetSelected = 0xb9;
constexpr std::uint8_t kNativeNetPingRequest = 0xd9;
constexpr std::uint8_t kNativeDie = 0x00;
constexpr std::uint8_t kNativeRevive = 0x01;
constexpr std::uint8_t kNativeAutoAttackStart = 0x25;
constexpr std::uint8_t kNativeAutoAttackStop = 0x26;
constexpr std::uint8_t kNativeSocialAction = 0x27;
constexpr std::uint8_t kNativeChangeMoveType = 0x28;
constexpr std::uint8_t kNativeChangeWaitType = 0x29;
constexpr std::uint8_t kNativeAttack = 0x33;
constexpr std::uint8_t kNativeSay = 0x4a;
constexpr std::uint8_t kNativeSystemMessage = 0x62;
constexpr std::uint8_t kNativeItemList = 0x11;
constexpr std::uint8_t kNativeAbnormalStatus = 0x85;
constexpr std::uint8_t kNativeAskJoinParty = 0x39;
constexpr std::uint8_t kNativeJoinParty = 0x3a;
constexpr std::uint8_t kNativePartyAll = 0x4e;
constexpr std::uint8_t kNativePartyAdd = 0x4f;
constexpr std::uint8_t kNativePartyDeleteAll = 0x50;
constexpr std::uint8_t kNativePartyDelete = 0x51;
constexpr std::uint8_t kNativePartyUpdate = 0x52;
constexpr std::uint8_t kNativePartyMemberPosition = 0xba;
constexpr std::uint8_t kNativePartySpelled = 0xf4;
constexpr std::uint8_t kNativeNewCharacterSuccess = 0x0d;
constexpr std::uint8_t kNativeCreateSuccess = 0x0f;
constexpr std::uint8_t kNativeCreateFail = 0x10;
constexpr std::uint8_t kNativeAcquireSkillList = 0x90;
constexpr int kNativeExAcquireSkillInfo = 0xfc;
constexpr std::uint8_t kNativeCharInfo = 0x31;
constexpr std::uint8_t kNativeInventoryUpdate = 0x21;
constexpr std::uint8_t kNativeActionFail = 0x1f;
constexpr std::uint8_t kNativeShortcutRegisterReply = 0x44;
constexpr std::uint8_t kNativeShortcutInit = 0x45;
constexpr std::uint8_t kNativeMagicSkillUse = 0x48;
constexpr std::uint8_t kNativeMagicSkillLaunched = 0x54;
constexpr std::uint8_t kNativeSkillList = 0x5f;
constexpr std::uint8_t kNativeSetupGauge = 0x6b;
constexpr std::uint8_t kNativeSkillCoolTime = 0xc7;
constexpr int kNativeExEquipSlots = 0x156;
constexpr int kNativeExBasicActionList = 0x60;
constexpr std::uint8_t kNativeExPacket = 0xfe;

constexpr std::size_t kInputPadding = 64;

void PutInt32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void PutInt64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    PutInt32(out, static_cast<std::uint32_t>(value));
    PutInt32(out, static_cast<std::uint32_t>(value >> 32));
}

void PutFloat(std::vector<std::uint8_t>& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    PutInt32(out, bits);
}

void PutZeros(std::vector<std::uint8_t>& out, std::size_t count) { out.insert(out.end(), count, 0); }

// Reads a server packet body; any read past the end marks it failed.
class Reader {
public:
    Reader(const std::uint8_t* data, int length, int position) : m_data(data), m_length(length), m_position(position) {}

    bool Ok() const { return m_ok; }
    int Position() const { return m_position; }
    void Seek(int position) { m_position = position; }

    std::int32_t Int32() {
        if (!Need(4)) {
            return 0;
        }
        const auto value = static_cast<std::int32_t>(runtime::LoadLittleEndian32(m_data + m_position));
        m_position += 4;
        return value;
    }
    std::int64_t Int64() {
        const auto low = static_cast<std::uint32_t>(Int32());
        const auto high = static_cast<std::uint32_t>(Int32());
        return static_cast<std::int64_t>(static_cast<std::uint64_t>(high) << 32 | low);
    }
    double Double() {
        const auto bits = static_cast<std::uint64_t>(Int64());
        double value = 0;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    // UTF-16 text with its terminator, as raw bytes.
    std::vector<std::uint8_t> Text() {
        const int start = m_position;
        while (Need(2) && (m_data[m_position] != 0 || m_data[m_position + 1] != 0)) {
            m_position += 2;
        }
        m_position += 2;
        if (!m_ok || m_position > m_length) {
            m_ok = false;
            return {0, 0};
        }
        return {m_data + start, m_data + m_position};
    }

private:
    bool Need(int count) {
        if (m_position + count > m_length) {
            m_ok = false;
        }
        return m_ok;
    }

    const std::uint8_t* m_data;
    int m_length;
    int m_position;
    bool m_ok = true;
};

std::string Hex(const std::uint8_t* data, int length) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string text;
    for (int index = 0; index < length; ++index) {
        text.push_back(kDigits[data[index] >> 4]);
        text.push_back(kDigits[data[index] & 15]);
    }
    return text;
}

// Untranslated packets, in full, for writing their translation:
// lu4_packets.log beside the server.
void Dump(const char* direction, const std::uint8_t* data, int length) {
    static std::mutex lock;
    const std::lock_guard guard(lock);
    std::ofstream file("lu4_packets.log", std::ios::app);
    file << direction << ' ' << length << ' ' << Hex(data, length) << '\n';
}

// Logs the first occurrence of each untranslated opcode in a direction.
bool FirstTime(int key) {
    static std::mutex lock;
    static std::set<int> seen;
    const std::lock_guard guard(lock);
    return seen.insert(key).second;
}

DefaultIOBuffer* MakeBuffer(const std::vector<std::uint8_t>& body) {
    DefaultIOBuffer* const buffer = DefaultIOBuffer::Alloc();
    const std::size_t size = body.size() + 2;
    buffer->m_Buffer[0] = static_cast<std::uint8_t>(size);
    buffer->m_Buffer[1] = static_cast<std::uint8_t>(size >> 8);
    std::copy(body.begin(), body.end(), buffer->m_Buffer.begin() + 2);
    buffer->m_dwSize = static_cast<std::uint32_t>(size);
    return buffer;
}

// c 0x00, d 62025, d uid, d 20  ->  c 0x0e, d 162, file key, d crc 0
bool TranslateVersion(std::vector<std::uint8_t>& out) {
    out.push_back(kNativeVersion);
    PutInt32(out, static_cast<std::uint32_t>(runtime::client_protocol::kCurrentRevision));
    out.insert(out.end(), g_nClientFileDecryptionKey, g_nClientFileDecryptionKey + g_nClientFileDecryptionKeyLen);
    PutInt32(out, 0);
    return true;
}

// c 0x01, S account, d uid, d playKey, d uid, d loginKey (the LoginOk and
// PlayOk values) -> c 0x2b, S account, d uid, d playKey, d loginKey, d game,
// d country, Q mac, d pptp. This server checks playKey itself and loginKey
// through AuthD.
bool TranslateAuthLogin(const std::uint8_t* packet, int length, std::vector<std::uint8_t>& out) {
    int cursor = 1;
    while (cursor + 1 < length && (packet[cursor] != 0 || packet[cursor + 1] != 0)) {
        cursor += 2;
    }
    cursor += 2;
    if (cursor + 16 > length) {
        return false;
    }
    out.push_back(kNativeLogin);
    out.insert(out.end(), packet + 1, packet + cursor);
    const std::uint32_t uid = runtime::LoadLittleEndian32(packet + cursor);
    const std::uint32_t playKey = runtime::LoadLittleEndian32(packet + cursor + 4);
    const std::uint32_t loginKey = runtime::LoadLittleEndian32(packet + cursor + 12);
    PutInt32(out, uid);
    PutInt32(out, playKey);
    PutInt32(out, loginKey);
    PutInt32(out, 0);
    PutInt32(out, 0);
    PutInt32(out, 0);
    PutInt32(out, 0);
    PutInt32(out, 0);
    return true;
}

// Native c 0x2e, c ok, Q key, ... -> LU4 c 0x2e, c 0, key[8], c 0
bool TranslateKeyPacket(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 10) {
        return false;
    }
    out = {kNativeKeyPacket, 0};
    out.insert(out.end(), body + 2, body + 10);
    out.push_back(0);
    return true;
}

// Native c 0x0a, d result (-1 accepted), d reason -> LU4 accepted is d 0, d 0.
bool TranslateLoginResult(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 9 || runtime::LoadLittleEndian32(body + 1) != 0xffffffffU) {
        return false;
    }
    out = {kNativeLoginResult, 0, 0, 0, 0, 0, 0, 0, 0};
    return true;
}

float ExpFraction(std::int64_t exp, int level) {
    if (level < 1 || level >= 121) {
        return 0;
    }
    const std::int64_t start = ExpByLevel[level - 1];
    const std::int64_t span = ExpByLevel[level] - start;
    return span > 0 ? static_cast<float>(static_cast<double>(exp - start) / static_cast<double>(span)) : 0.0F;
}

// Hair style, hair color and face as LU4 takes them. Its character creation
// offers hair 0-4 (male) or 0-6 (female), color 0-3 and face 0-2; a character
// made with this server's own client can have more (face 6 seen), and the LU4
// model then fails to load (T-pose). Values out of range become 0.
void PutLooks(std::vector<std::uint8_t>& out, int sex, std::int32_t hairStyle, std::int32_t hairColor, std::int32_t face) {
    auto clamp = [](std::int32_t value, std::int32_t last) { return value >= 0 && value <= last ? value : 0; };
    PutInt32(out, static_cast<std::uint32_t>(clamp(hairStyle, sex != 0 ? 6 : 4)));
    PutInt32(out, static_cast<std::uint32_t>(clamp(hairColor, 3)));
    PutInt32(out, static_cast<std::uint32_t>(clamp(face, 2)));
}

// Native character list (revision 162): c 0x09, d count, d slots, 7 bytes, then
// per character S name, d objectId, S account and a 522-byte block:
//   +12 sex, +16 race, +20 base class, +28 x, +32 y, +36 z, +40 hp (double),
//   +48 mp (double), +56 sp (Q), +64 exp (Q), +80 level, +132 paperdoll
//   (under, ears, neck, fingers, then head .. hair2 at +156), +418 face,
//   +422 hair style, +426 hair color, +430 max hp (double), +438 max mp (double),
//   +450 class, +454 last used.
// LU4 (official capture, D:\LU4\docs\LU4-CHARSELECT-WIRE.md): c 0x09, d 7,
// d count, then per character d objectId, S name, c race, c sex, a 139-byte
// block (d class twice, d hair style, color, face, ...), d x, d y, d z. The
// class ids are this server's.
constexpr int kNativeCharacterBlock = 522;

bool TranslateCharacterList(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    Reader header(body, length, 1);
    const int count = header.Int32();
    const int slots = header.Int32();
    if (!header.Ok() || count < 0 || count > 16) {
        return false;
    }
    out = {kNativeCharacterList};
    PutInt32(out, static_cast<std::uint32_t>(slots > 0 ? slots : 7));
    PutInt32(out, static_cast<std::uint32_t>(count));
    Reader reader(body, length, 16);
    for (int index = 0; index < count; ++index) {
        const std::vector<std::uint8_t> name = reader.Text();
        const std::int32_t objectId = reader.Int32();
        reader.Text(); // account
        const int block = reader.Position();
        if (!reader.Ok() || block + kNativeCharacterBlock > length) {
            return false;
        }
        Reader fields(body, length, block);
        auto at32 = [&](int offset) {
            fields.Seek(block + offset);
            return fields.Int32();
        };
        auto at64 = [&](int offset) {
            fields.Seek(block + offset);
            return fields.Int64();
        };
        auto atDouble = [&](int offset) {
            fields.Seek(block + offset);
            return fields.Double();
        };
        const int level = at32(80);
        const std::int64_t exp = at64(64);

        PutInt32(out, static_cast<std::uint32_t>(objectId));
        out.insert(out.end(), name.begin(), name.end());
        out.push_back(static_cast<std::uint8_t>(at32(16))); // race
        out.push_back(static_cast<std::uint8_t>(at32(12))); // sex
        // 139-byte block.
        PutInt32(out, static_cast<std::uint32_t>(at32(450)));
        PutInt32(out, static_cast<std::uint32_t>(at32(450)));
        PutLooks(out, at32(12), at32(422), at32(426), at32(418));
        PutInt32(out, static_cast<std::uint32_t>(atDouble(40)));
        PutInt32(out, static_cast<std::uint32_t>(atDouble(430)));
        PutInt32(out, static_cast<std::uint32_t>(atDouble(48)));
        PutInt32(out, static_cast<std::uint32_t>(atDouble(438)));
        PutInt64(out, static_cast<std::uint64_t>(at64(56)));
        PutInt64(out, static_cast<std::uint64_t>(exp));
        PutFloat(out, ExpFraction(exp, level));
        PutZeros(out, 8);
        PutInt32(out, static_cast<std::uint32_t>(level));
        for (int slot = 0; slot < 11; ++slot) { // head .. hair2
            PutInt32(out, static_cast<std::uint32_t>(at32(156 + slot * 4)));
        }
        PutZeros(out, 12);
        out.insert(out.end(), {0, static_cast<std::uint8_t>(at32(454) != 0 ? 1 : 0), 0, 0});
        PutZeros(out, 4 + 3 + 4);
        PutInt32(out, static_cast<std::uint32_t>(at32(28)));
        PutInt32(out, static_cast<std::uint32_t>(at32(32)));
        PutInt32(out, static_cast<std::uint32_t>(at32(36)));
        if (!fields.Ok()) {
            return false;
        }
        reader.Seek(block + kNativeCharacterBlock);
    }
    return true;
}

// Native c 0x0b, S name, d objectId, S title, d session, d clan, d builder,
// d sex, d race, d class, d active, d x, d y, d z ... -> LU4 (capture): c 0x0b,
// c 0, d x, d y, d z, d 0, d objectId.
bool TranslateCharacterSelected(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    Reader reader(body, length, 1);
    reader.Text();
    const std::int32_t objectId = reader.Int32();
    reader.Text();
    for (int index = 0; index < 7; ++index) {
        reader.Int32();
    }
    const std::int32_t x = reader.Int32();
    const std::int32_t y = reader.Int32();
    const std::int32_t z = reader.Int32();
    if (!reader.Ok()) {
        return false;
    }
    out = {kNativeCharacterSelected, 0};
    PutInt32(out, static_cast<std::uint32_t>(x));
    PutInt32(out, static_cast<std::uint32_t>(y));
    PutInt32(out, static_cast<std::uint32_t>(z));
    PutInt32(out, 0);
    PutInt32(out, static_cast<std::uint32_t>(objectId));
    return true;
}

// Native user info: c 0x32, d objectId, d size, h component count, the mask
// (a bit per component, high bit first), then the components it marks:
// d relation, then each component with its own h size first:
//    1 basic: h name length, name, c gm, c race, c sex, d base class, d class,
//      c level
//    2 h STR DEX CON INT WIT MEN LUC CHA       3 d max hp, mp, cp
//    4 d hp, mp, cp, Q sp, Q exp               6 d face, hair style, color
//    8 h attack range, d pAtk, pAtkSpd, pDef, evasion, accuracy, critical,
//      mAtk, castSpd, physical cast speed, mEvasion, mDef, mAccuracy, mCritical
//   10 d x, y, z    11 h speeds x8    12 double move and attack speed rates
//   13 double collision radius, height    15 h title length, title, ...
//   20 d name color, title color
// An update carries only what changed, so each connection keeps the last copy
// of every component.
//
// LU4 (D:\LU4\docs\LU4-CHARINFO-WIRE.md): c 0x32, d objectId, 4 bytes, S name,
// S title, then 305 bytes, here the official capture's with these fields set:
//   +0 name color, +4 title color, +8 c gm (0 in the capture), +9 c race,
//   +10 c sex, +11 base class, +15 class, +19 level, +23 hair style, color,
//   face, +35 h STR..MEN, +47 hp, max hp, mp, max mp, cp, max cp, +71 Q sp,
//   +79 Q exp, +87 float exp fraction, +95 h attack range, +97 d x13 combat
//   stats, +165 x y z,
//   +181 h speeds x8, +197 float move rate, attack speed rate, collision
//   radius, height.
// Combat stats order, matched against the capture's equipment changes: attack
// speed, pAtk, attack speed, pDef, evasion, accuracy, critical, mAtk, cast
// speed, mDef, mEvasion, mAccuracy, last one unknown (mCritical here).
constexpr std::uint8_t kNativeUserInfo = 0x32;
constexpr int kComponentCount = 32;

constexpr char kUserInfoTail[] =
    "ffffff00a2f9ec0000000000000000000000000600000004000000020000000200000028001e002b0015000b001900dd"
    "000000dd0000004a0000004a0000005800000058000000e802000000000000001f000000000000ad22b93e0100000028"
    "00a001000006000000a00100004f00000026000000260000005800000002000000d50000003100000026000000260000"
    "0020000000feff00000000000000000000000000003ee9feff7cf20300e0f3ffff000000007300500032003200000000"
    "0000000000cdcc8c3f861bc03f000010410000b841000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000001400000000000000000000000000000000000000000000000164000000000000"
    "0000000000000000004d290000dca90100";

struct UserState {
    std::int32_t objectId = 0;
    std::vector<std::uint8_t> components[kComponentCount];
    std::vector<std::uint8_t> loggedCombat;
    std::vector<std::uint8_t> loggedSocial;
};

std::mutex g_usersLock;
std::map<const CSocket*, UserState> g_users;

class TailWriter {
public:
    explicit TailWriter(std::vector<std::uint8_t>& tail) : m_tail(tail) {}
    void Byte(int offset, std::uint32_t value) { m_tail[offset] = static_cast<std::uint8_t>(value); }
    void Int16(int offset, std::uint32_t value) {
        Byte(offset, value);
        Byte(offset + 1, value >> 8);
    }
    void Int32(int offset, std::uint32_t value) {
        Int16(offset, value);
        Int16(offset + 2, value >> 16);
    }
    void Int64(int offset, std::uint64_t value) {
        Int32(offset, static_cast<std::uint32_t>(value));
        Int32(offset + 4, static_cast<std::uint32_t>(value >> 32));
    }
    void Float(int offset, float value) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        Int32(offset, bits);
    }

private:
    std::vector<std::uint8_t>& m_tail;
};

std::vector<std::uint8_t> UserInfoTail() {
    auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    std::vector<std::uint8_t> tail;
    for (std::size_t index = 0; index + 1 < sizeof(kUserInfoTail); index += 2) {
        tail.push_back(static_cast<std::uint8_t>(digit(kUserInfoTail[index]) << 4 | digit(kUserInfoTail[index + 1])));
    }
    return tail;
}

// Little-endian field of a stored component; bytes past its end read as 0.
std::uint64_t Field(const std::vector<std::uint8_t>& block, int offset, int bytes) {
    std::uint64_t value = 0;
    for (int index = bytes - 1; index >= 0; --index) {
        const int at = offset + index;
        value = value << 8 | (at >= 0 && at < static_cast<int>(block.size()) ? block[at] : 0);
    }
    return value;
}

// This server keeps reputation (negative for a player killer); LU4 takes the
// Interlude karma, positive for a player killer.
std::uint32_t Lu4Karma(std::int32_t reputation) { return reputation < 0 ? static_cast<std::uint32_t>(-reputation) : 0U; }

double DoubleField(const std::vector<std::uint8_t>& block, int offset) {
    const std::uint64_t bits = Field(block, offset, 8);
    double value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool TranslateUserInfo(const CSocket* socket, const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 11) {
        return false;
    }
    const auto objectId = static_cast<std::int32_t>(runtime::LoadLittleEndian32(body + 1));
    const int count = std::min<int>(body[9] | body[10] << 8, kComponentCount);
    int position = 11 + (count + 7) / 8;
    if (position > length) {
        return false;
    }
    L2_LOG_INFO(Log, "[lu4] User info for %d: mask %s", static_cast<int>(runtime::LoadLittleEndian32(body + 1)),
                Hex(body + 11, (count + 7) / 8).c_str());
    const std::lock_guard guard(g_usersLock);
    UserState& state = g_users[socket];
    if (state.objectId != objectId) {
        state = UserState{};
        state.objectId = objectId;
    }
    for (int component = 0; component < count; ++component) {
        if ((body[11 + component / 8] & 0x80 >> component % 8) == 0) {
            continue;
        }
        const int size = component == 0 ? 4 : (position + 2 <= length ? body[position] | body[position + 1] << 8 : 0);
        if (size < 2 || position + size > length) {
            return false;
        }
        state.components[component].assign(body + position, body + position + size);
        position += size;
    }
    const std::vector<std::uint8_t>& basic = state.components[1];
    if (basic.size() < 4) {
        return false;
    }
    const int afterName = 4 + static_cast<int>(Field(basic, 2, 2)) * 2;
    if (afterName + 12 > static_cast<int>(basic.size())) {
        return false;
    }
    const auto level = static_cast<int>(Field(basic, afterName + 11, 1));

    std::vector<std::uint8_t> tail = UserInfoTail();
    TailWriter writer(tail);
    writer.Byte(9, static_cast<std::uint32_t>(Field(basic, afterName + 1, 1)));
    writer.Byte(10, static_cast<std::uint32_t>(Field(basic, afterName + 2, 1)));
    writer.Int32(11, static_cast<std::uint32_t>(Field(basic, afterName + 3, 4)));
    writer.Int32(15, static_cast<std::uint32_t>(Field(basic, afterName + 7, 4)));
    writer.Int32(19, static_cast<std::uint32_t>(level));
    if (const auto& stats = state.components[2]; !stats.empty()) {
        for (int index = 0; index < 6; ++index) {
            writer.Int16(35 + index * 2, static_cast<std::uint32_t>(Field(stats, 2 + index * 2, 2)));
        }
    }
    const auto& maximum = state.components[3];
    const auto& current = state.components[4];
    if (!maximum.empty() && !current.empty()) {
        for (int index = 0; index < 3; ++index) {
            writer.Int32(47 + index * 8, static_cast<std::uint32_t>(Field(current, 2 + index * 4, 4)));
            writer.Int32(51 + index * 8, static_cast<std::uint32_t>(Field(maximum, 2 + index * 4, 4)));
        }
        const auto exp = static_cast<std::int64_t>(Field(current, 22, 8));
        writer.Int64(71, Field(current, 14, 8));
        writer.Int64(79, static_cast<std::uint64_t>(exp));
        writer.Float(87, ExpFraction(exp, level));
    }
    if (const auto& looks = state.components[6]; !looks.empty()) { // native face, hair style, hair color
        std::vector<std::uint8_t> clamped;
        PutLooks(clamped, static_cast<int>(Field(basic, afterName + 2, 1)), static_cast<std::int32_t>(Field(looks, 6, 4)),
                 static_cast<std::int32_t>(Field(looks, 10, 4)), static_cast<std::int32_t>(Field(looks, 2, 4)));
        std::copy(clamped.begin(), clamped.end(), tail.begin() + 23);
    }
    // The first user info goes out before the stats are computed (all zero);
    // the capture's values stand in until a later one carries them.
    if (const auto& combat = state.components[8]; combat.size() >= 56 && Field(combat, 8, 4) != 0) {
        // Native order after h attack range: pAtk, pAtkSpd, pDef, evasion, accuracy, critical, mAtk,
        // castSpd, an unrelated speed, mEvasion, mDef, mAccuracy, mCritical (User.cpp MakeUserInfo).
        static constexpr int kOrder[13] = {1, 0, 1, 2, 3, 4, 5, 6, 7, 10, 9, 11, 12};
        // +95 is the attack range: 20 bare-handed, 40 with a weapon in the capture.
        writer.Int16(95, static_cast<std::uint32_t>(Field(combat, 2, 2)));
        for (int index = 0; index < 13; ++index) {
            writer.Int32(97 + index * 4, static_cast<std::uint32_t>(Field(combat, 4 + kOrder[index] * 4, 4)));
        }
    }
    if (const auto& place = state.components[10]; !place.empty()) {
        for (int index = 0; index < 3; ++index) {
            writer.Int32(165 + index * 4, static_cast<std::uint32_t>(Field(place, 2 + index * 4, 4)));
        }
    }
    if (const auto& speeds = state.components[11]; !speeds.empty() && Field(speeds, 2, 2) != 0) {
        for (int index = 0; index < 8; ++index) {
            writer.Int16(181 + index * 2, static_cast<std::uint32_t>(Field(speeds, 2 + index * 2, 2)));
        }
    }
    if (const auto& rates = state.components[12]; !rates.empty()) {
        const double moveRate = DoubleField(rates, 2);
        const double attackRate = DoubleField(rates, 10);
        if (moveRate > 0 && moveRate < 100) {
            writer.Float(197, static_cast<float>(moveRate));
        }
        if (attackRate > 0 && attackRate < 100) {
            writer.Float(201, static_cast<float>(attackRate));
        }
    }
    if (const auto& collision = state.components[13]; !collision.empty()) {
        writer.Float(205, static_cast<float>(DoubleField(collision, 2)));
        writer.Float(209, static_cast<float>(DoubleField(collision, 10)));
    }
    if (const auto& colors = state.components[20]; !colors.empty()) {
        writer.Int32(0, static_cast<std::uint32_t>(Field(colors, 2, 4)));
        writer.Int32(4, static_cast<std::uint32_t>(Field(colors, 6, 4)));
    }
    // Native 16 social (L2Packet.h UserInfo_Social): c pvp flag, d reputation,
    // c noble, c hero, c social class, d pk, d pvp, h recommendations left,
    // h have. The client's reader (rva 0x4c175a0) has the same run at +240.
    if (const auto& social = state.components[16]; social.size() >= 22) {
        writer.Byte(240, static_cast<std::uint32_t>(Field(social, 2, 1)));
        writer.Int32(241, Lu4Karma(static_cast<std::int32_t>(Field(social, 3, 4))));
        writer.Byte(245, Field(social, 7, 1) != 0 ? 1U : 0U);
        writer.Byte(246, Field(social, 8, 1) != 0 ? 1U : 0U);
        writer.Byte(247, static_cast<std::uint32_t>(Field(social, 9, 1)));
        writer.Int32(248, static_cast<std::uint32_t>(Field(social, 10, 4)));
        writer.Int32(252, static_cast<std::uint32_t>(Field(social, 14, 4)));
        writer.Int16(256, static_cast<std::uint32_t>(Field(social, 18, 2)));
        writer.Int16(258, static_cast<std::uint32_t>(Field(social, 20, 2)));
    }

    if (const auto& combat = state.components[8]; combat.size() >= 56 && combat != state.loggedCombat) {
        state.loggedCombat = combat;
        std::string values;
        for (int index = 0; index < 13; ++index) {
            values += std::to_string(static_cast<std::int32_t>(Field(combat, 4 + index * 4, 4))) + ' ';
        }
        L2_LOG_INFO(Log, "[lu4] User info stats for %d: range %d, native %s; rates %s; LU4 tail+91..+213 %s", objectId,
                    static_cast<int>(Field(combat, 2, 2)), values.c_str(), Hex(state.components[12].data(),
                    static_cast<int>(state.components[12].size())).c_str(), Hex(tail.data() + 91, 122).c_str());
    }
    if (const auto& social = state.components[16]; social != state.loggedSocial) {
        state.loggedSocial = social;
        L2_LOG_INFO(Log, "[lu4] User info social for %d: native %s; LU4 tail+240..+259 %s", objectId,
                    Hex(social.data(), static_cast<int>(social.size())).c_str(), Hex(tail.data() + 240, 20).c_str());
    }
    out = {kNativeUserInfo};
    PutInt32(out, static_cast<std::uint32_t>(objectId));
    PutZeros(out, 4);
    out.insert(out.end(), basic.begin() + 4, basic.begin() + afterName);
    PutZeros(out, 2);
    if (const auto& clan = state.components[15]; clan.size() >= 4) {
        const int titleEnd = std::min<int>(4 + static_cast<int>(Field(clan, 2, 2)) * 2, static_cast<int>(clan.size()));
        out.insert(out.end(), clan.begin() + 4, clan.begin() + titleEnd);
    }
    PutZeros(out, 2);
    out.insert(out.end(), tail.begin(), tail.end());
    return true;
}

// Native c 0x18, d objectId, d caster, c visible, c count, count x (c id,
// d value) -> LU4 (Interlude form) c 0x18, d objectId, d count, count x (d id,
// d value). The ids are the same.
// Ids 26 (pvp flag) and 27 (karma) are Interlude's too; karma is converted
// as in the user info.
constexpr std::uint8_t kStatusPvpFlag = 26;
constexpr std::uint8_t kStatusKarma = 27;

bool TranslateStatusUpdate(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 11 || 11 + body[10] * 5 > length) {
        return false;
    }
    const int count = body[10];
    out = {body[0]};
    out.insert(out.end(), body + 1, body + 5);
    PutInt32(out, static_cast<std::uint32_t>(count));
    for (int index = 0; index < count; ++index) {
        const std::uint8_t* const entry = body + 11 + index * 5;
        PutInt32(out, entry[0]);
        if (entry[0] == kStatusKarma) {
            PutInt32(out, Lu4Karma(static_cast<std::int32_t>(runtime::LoadLittleEndian32(entry + 1))));
        } else {
            out.insert(out.end(), entry + 1, entry + 5);
        }
        if (entry[0] == kStatusPvpFlag || entry[0] == kStatusKarma) {
            L2_LOG_INFO(Log, "[lu4] Status update for %d: id %d value %d", static_cast<int>(runtime::LoadLittleEndian32(body + 1)),
                        entry[0], static_cast<int>(runtime::LoadLittleEndian32(entry + 1)));
        }
    }
    return true;
}

// Native NPC info (Protocol/src/L2NPCProtocol.cpp, NPCInfoPacket::Encode):
// c 0x0c, d objectId, c send type, h component count (37), the mask (a bit
// per component, high bit first), c private size, private components
// (1 c enemy, 2 d relation, 30 S master name), h common size, then the common
// components in NPCInfoPacket::EncodeCommon order:
//    0 d class (npc id + 1000000)  4 d x, y, z  5 d heading  6 d vehicle
//    7 d cast speeds x2  8 float move and attack speed rates
//    9 d right hand, chest, left hand  10 c stop  11 c move (1 run)
//   14 c environment  15 c team  16 d enchant  17 d move type  18 d
//   19 d  22 d state  23 d morph  24 d hp  25 d mp  26 d max hp  27 d max mp
//   28 c  29 d d  3 S title  31 d  32 d  33 c  34 d
//   35 d pledge, crest, emblem, alliance, alliance crest
// and, when any of 12, 13, 20, 21 is marked, one byte with 0x01 in combat,
// 0x02 dead, 0x04 targetable, 0x08 name shown. An update carries only what
// changed, so each connection keeps the last state of every NPC it sees.
//
// LU4 (Lucera NpcInfo.writeLu4, 59 official packets; 569 more checked here):
// c 0x0c, d objectId, c spawn animation, c enemy, d 0, d -1, d -1, d class,
// d x, y, z, heading, d 0, d mAtkSpd, d pAtkSpd, h speeds x8, float move rate,
// attack rate, collision radius, height, d right hand, chest, left hand,
// c alive, c running, c 0, c 0, S name, S title, d pvp flag, d karma,
// d abnormal, d hp, max hp, mp, max mp, d pledge, crest, alliance, alliance
// crest, d state (the native byte: 12 seen, 13 in combat, 15 dead), c team.
// Speeds and collision come from the NPC itself; this server's packet leaves
// them to the client's data.
constexpr std::uint8_t kNativeNpcInfo = 0x0c;
constexpr std::uint8_t kNativeNpcState = 0xc1;
constexpr std::uint8_t kNativeDeleteObject = 0x08;
constexpr std::uint8_t kNativeSpawnItem = 0x05;
constexpr std::uint8_t kNativeItemDropped = 0x16;
constexpr std::uint8_t kNativeItemPickedUp = 0x17;
constexpr std::uint8_t kNativeConfirmDialog = 0xf3;
constexpr std::uint8_t kNativeNpcSay = 0x30;
constexpr std::uint8_t kNativeRelationChanged = 0xce;

struct NpcState {
    std::int32_t classId = 0;
    std::int32_t position[3] = {};
    std::int32_t heading = 0;
    std::int32_t castSpeed[2] = {};
    float rates[2] = {1.0F, 1.0F};
    std::int32_t hands[3] = {};
    std::uint8_t enemy = 0;
    std::uint8_t moveMode = 0;
    std::uint8_t team = 0;
    std::uint8_t state = 0x0c;
    std::int32_t hp = 0;
    std::int32_t maxHp = 0;
    std::int32_t pledge[5] = {};
    std::vector<std::uint8_t> title{0, 0};
    bool shown = false;
};

std::mutex g_npcsLock;
std::map<const CSocket*, std::map<std::int32_t, NpcState>> g_npcs;

void ForgetNpcs(const CSocket* socket) {
    const std::lock_guard guard(g_npcsLock);
    g_npcs.erase(socket);
}

bool ParseNpcInfo(const std::uint8_t* body, int length, NpcState& npc) {
    if (length < 8) {
        return false;
    }
    const int count = body[6] | body[7] << 8;
    const std::uint8_t* const mask = body + 8;
    Reader reader(body, length, 8 + (count + 7) / 8);
    auto marked = [&](int component) { return component < count && (mask[component / 8] & 0x80 >> component % 8) != 0; };
    auto byte = [&]() {
        const int at = reader.Position();
        if (at >= length) {
            reader.Seek(length + 1);
            return std::uint8_t{0};
        }
        reader.Seek(at + 1);
        return body[at];
    };
    auto text = [&]() { return reader.Text(); };
    auto readFloat = [&]() {
        const auto bits = static_cast<std::uint32_t>(reader.Int32());
        float value = 0;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    };

    const int privateSize = byte();
    const int privateEnd = reader.Position() + privateSize;
    if (marked(1)) {
        npc.enemy = byte();
    }
    reader.Seek(privateEnd);
    const int commonSize = reader.Position() + 2 <= length ? body[reader.Position()] | body[reader.Position() + 1] << 8 : 0;
    reader.Seek(reader.Position() + 2);
    const int commonEnd = reader.Position() + commonSize;
    if (marked(0)) {
        npc.classId = reader.Int32();
    }
    if (marked(4)) {
        for (std::int32_t& value : npc.position) {
            value = reader.Int32();
        }
    }
    if (marked(5)) {
        npc.heading = reader.Int32();
    }
    if (marked(6)) {
        reader.Int32();
    }
    if (marked(7)) {
        npc.castSpeed[0] = reader.Int32();
        npc.castSpeed[1] = reader.Int32();
    }
    if (marked(8)) {
        npc.rates[0] = readFloat();
        npc.rates[1] = readFloat();
    }
    if (marked(9)) {
        for (std::int32_t& value : npc.hands) {
            value = reader.Int32();
        }
    }
    if (marked(10)) {
        byte();
    }
    if (marked(11)) {
        npc.moveMode = byte();
    }
    if (marked(14)) {
        byte();
    }
    if (marked(15)) {
        npc.team = byte();
    }
    for (const int component : {16, 17, 18, 19, 22, 23}) {
        if (marked(component)) {
            reader.Int32();
        }
    }
    if (marked(24)) {
        npc.hp = reader.Int32();
    }
    if (marked(25)) {
        reader.Int32();
    }
    if (marked(26)) {
        npc.maxHp = reader.Int32();
    }
    if (marked(27)) {
        reader.Int32();
    }
    if (marked(28)) {
        byte();
    }
    if (marked(29)) {
        reader.Int32();
        reader.Int32();
    }
    if (marked(3)) {
        npc.title = text();
    }
    for (const int component : {31, 32}) {
        if (marked(component)) {
            reader.Int32();
        }
    }
    if (marked(33)) {
        byte();
    }
    if (marked(34)) {
        reader.Int32();
    }
    if (marked(35)) {
        for (std::int32_t& value : npc.pledge) {
            value = reader.Int32();
        }
    }
    if (marked(12) || marked(13) || marked(20) || marked(21)) {
        const std::uint8_t flags = byte();
        static constexpr std::pair<int, std::uint8_t> kFlags[] = {{12, 0x01}, {13, 0x02}, {20, 0x04}, {21, 0x08}};
        for (const auto& [component, flag] : kFlags) {
            if (marked(component)) {
                npc.state = static_cast<std::uint8_t>((npc.state & ~flag) | (flags & flag));
            }
        }
    }
    return reader.Ok() && reader.Position() <= commonEnd;
}

void BuildNpcInfo(std::int32_t objectId, NpcState& npc, std::vector<std::uint8_t>& out) {
    OrgSpeeds speeds{};
    float collision[2] = {10.0F, 20.0F};
    std::int32_t mp[2] = {};
    if (CSPointer<CCreature> creature = CCreature::FindObjectSP(static_cast<std::uint32_t>(objectId));
        creature && creature->IsNPC()) {
        CNPC* const native = creature->CastNPC();
        native->GetOrgSpeeds(speeds);
        collision[0] = static_cast<float>(native->GetCollisionRadius());
        collision[1] = static_cast<float>(native->GetCollisionHeight());
        mp[0] = runtime::Win64CvttDoubleToInt32(native->m_data->GetMP());
        mp[1] = runtime::Win64CvttDoubleToInt32(native->BaseMP());
    }
    const bool dead = (npc.state & 0x02) != 0;
    out = {kNativeNpcInfo};
    PutInt32(out, static_cast<std::uint32_t>(objectId));
    out.push_back(npc.shown ? 1 : 0);
    out.push_back(npc.enemy != 0 ? 1 : 0);
    PutInt32(out, 0);
    PutInt32(out, 0xffffffffU);
    PutInt32(out, 0xffffffffU);
    PutInt32(out, static_cast<std::uint32_t>(npc.classId));
    for (const std::int32_t value : npc.position) {
        PutInt32(out, static_cast<std::uint32_t>(value));
    }
    PutInt32(out, static_cast<std::uint32_t>(npc.heading));
    PutInt32(out, 0);
    PutInt32(out, static_cast<std::uint32_t>(npc.castSpeed[0] >= 10 ? npc.castSpeed[0] : 278));
    PutInt32(out, static_cast<std::uint32_t>(npc.castSpeed[1] >= 10 ? npc.castSpeed[1] : 333));
    for (const std::int32_t value : {speeds.m_nOrgSpeed0, speeds.m_nOrgSpeed1, speeds.m_nOrgSpeed2, speeds.m_nOrgSpeed3,
                                     speeds.m_nOrgSpeed4, speeds.m_nOrgSpeed5, speeds.m_nOrgSpeed6, speeds.m_nOrgSpeed7}) {
        out.push_back(static_cast<std::uint8_t>(value));
        out.push_back(static_cast<std::uint8_t>(value >> 8));
    }
    PutFloat(out, npc.rates[0] > 0 ? npc.rates[0] : 1.0F);
    PutFloat(out, npc.rates[1] > 0 ? npc.rates[1] : 1.0F);
    PutFloat(out, collision[0]);
    PutFloat(out, collision[1]);
    PutInt32(out, static_cast<std::uint32_t>(npc.hands[0]));
    PutInt32(out, static_cast<std::uint32_t>(npc.hands[1]));
    PutInt32(out, static_cast<std::uint32_t>(npc.hands[2]));
    out.push_back(dead ? 0 : 1);
    out.push_back(npc.moveMode == 1 ? 1 : 0);
    out.push_back(0);
    out.push_back(0);
    PutZeros(out, 2); // name: the client's own
    out.insert(out.end(), npc.title.begin(), npc.title.end());
    PutInt32(out, 0);
    PutInt32(out, 0);
    PutInt32(out, 0);
    PutInt32(out, static_cast<std::uint32_t>(npc.hp));
    PutInt32(out, static_cast<std::uint32_t>(npc.maxHp));
    PutInt32(out, static_cast<std::uint32_t>(mp[0]));
    PutInt32(out, static_cast<std::uint32_t>(mp[1]));
    PutInt32(out, static_cast<std::uint32_t>(npc.pledge[0]));
    PutInt32(out, static_cast<std::uint32_t>(npc.pledge[1]));
    PutInt32(out, static_cast<std::uint32_t>(npc.pledge[3]));
    PutInt32(out, static_cast<std::uint32_t>(npc.pledge[4]));
    PutInt32(out, npc.state);
    out.push_back(npc.team);
    npc.shown = true;
}

bool TranslateNpcInfo(const CSocket* socket, const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 8) {
        return false;
    }
    const auto objectId = static_cast<std::int32_t>(runtime::LoadLittleEndian32(body + 1));
    NpcState npc;
    {
        const std::lock_guard guard(g_npcsLock);
        auto& known = g_npcs[socket];
        if (const auto found = known.find(objectId); found != known.end()) {
            npc = found->second;
        }
    }
    if (!ParseNpcInfo(body, length, npc) || npc.classId == 0) {
        return false;
    }
    BuildNpcInfo(objectId, npc, out);
    const std::lock_guard guard(g_npcsLock);
    g_npcs[socket][objectId] = npc;
    return true;
}

// Native c 0xc1, d objectId, c state (0x01 dead, 0x02 in combat, 0x04 running)
// has no LU4 form; the NPC is described again.
bool TranslateNpcState(const CSocket* socket, const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 6) {
        return false;
    }
    const auto objectId = static_cast<std::int32_t>(runtime::LoadLittleEndian32(body + 1));
    NpcState npc;
    {
        const std::lock_guard guard(g_npcsLock);
        auto& known = g_npcs[socket];
        const auto found = known.find(objectId);
        if (found == known.end()) {
            return false;
        }
        npc = found->second;
    }
    const std::uint8_t state = body[5];
    npc.state = static_cast<std::uint8_t>((npc.state & 0x0c) | (state & 0x01 ? 0x02 : 0) | (state & 0x02 ? 0x01 : 0));
    npc.moveMode = (state & 0x04) != 0 ? 1 : 0;
    BuildNpcInfo(objectId, npc, out);
    const std::lock_guard guard(g_npcsLock);
    g_npcs[socket][objectId] = npc;
    return true;
}

// Native c 0x08, d objectId, c flag -> LU4 c 0x08, d objectId, d 0.
bool TranslateDeleteObject(const CSocket* socket, const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 5) {
        return false;
    }
    {
        const std::lock_guard guard(g_npcsLock);
        if (const auto found = g_npcs.find(socket); found != g_npcs.end()) {
            found->second.erase(static_cast<std::int32_t>(runtime::LoadLittleEndian32(body + 1)));
        }
    }
    out.assign(body, body + 5);
    PutInt32(out, 0);
    return true;
}

// Items on the ground; the LU4 readers are rva 0x4c16770 and 0x4c0b1a0.
// Native c 0x05, d objectId, d item, d xyz, d stackable, Q count, d heading,
// c enchant, c variation, c ensoul -> LU4 the same up to the count, d 0.
bool TranslateSpawnItem(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 33) {
        return false;
    }
    out.assign(body, body + 33);
    PutInt32(out, 0);
    return true;
}

// Native c 0x16, d dropper, d objectId, d item, d xyz, c stackable, Q count,
// c heading flag, [d heading], c enchant, c variation, c ensoul -> LU4
// c 0x16, the dwords, d stackable, q count, d 1.
bool TranslateDropItem(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 35) {
        return false;
    }
    out.assign(body, body + 25);
    PutInt32(out, body[25]);
    out.insert(out.end(), body + 26, body + 34);
    PutInt32(out, 1);
    return true;
}

// Native c 0xb9, d 1, d target, h level difference, d reason -> LU4 c 0xb9,
// d target, h level difference (the name colour), d 0, c 0. The native
// deselection, c 0xb9, d 0, has no LU4 form; TargetUnselected (0x24) follows it.
bool TranslateMyTargetSelected(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length != 15 || runtime::LoadLittleEndian32(body + 1) != 1) {
        return false;
    }
    out = {body[0]};
    out.insert(out.end(), body + 5, body + 11);
    PutInt32(out, 0);
    out.push_back(0);
    return true;
}

bool EqualsIgnoreCase(const std::u16string& text, std::size_t at, const char* word) {
    for (std::size_t index = 0; word[index] != 0; ++index) {
        if (at + index >= text.size()) {
            return false;
        }
        char16_t c = text[at + index];
        if (c >= u'A' && c <= u'Z') {
            c = static_cast<char16_t>(c - u'A' + u'a');
        }
        if (c != static_cast<char16_t>(word[index])) {
            return false;
        }
    }
    return true;
}

bool EndsTagName(const std::u16string& text, std::size_t at) {
    return at < text.size() && (text[at] == u'>' || text[at] == u' ' || text[at] == u'\t' || text[at] == u'\r' ||
                                text[at] == u'\n' || text[at] == u'/');
}

// The LU4 dialog is a Chromium page that runs actions only on <button>:
// <a ...>text</a> becomes <button align=left icon="normal" ...>text</button>,
// <br1> becomes <br>, and the document gets <html><body> when it has none
// (Lucera NpcHtmlMessage.lu4Traduzir; 33 official dialogs use only <button>).
std::u16string Lu4Html(const std::u16string& html) {
    std::u16string out;
    out.reserve(html.size() + html.size() / 4);
    for (std::size_t at = 0; at < html.size();) {
        if (html[at] == u'<') {
            if (EqualsIgnoreCase(html, at, "<a") && EndsTagName(html, at + 2)) {
                out += u"<button align=left icon=\"normal\"";
                at += 2;
                continue;
            }
            if (EqualsIgnoreCase(html, at, "</a") && EndsTagName(html, at + 3)) {
                out += u"</button";
                at += 3;
                continue;
            }
            if (EqualsIgnoreCase(html, at, "<br1") && EndsTagName(html, at + 4)) {
                out += u"<br";
                at += 4;
                continue;
            }
        }
        out.push_back(html[at++]);
    }
    bool hasHtml = false;
    for (std::size_t at = 0; at + 5 <= out.size() && !hasHtml; ++at) {
        hasHtml = EqualsIgnoreCase(out, at, "<html");
    }
    return hasHtml ? out : u"<html><body>" + out + u"</body></html>";
}

// Native c 0x19, d npc, S html, d item, d display type -> LU4 c 0x19, d npc,
// S html (LU4 dialect), d 0.
bool TranslateNpcHtml(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    Reader reader(body, length, 5);
    const std::vector<std::uint8_t> text = reader.Text();
    if (!reader.Ok()) {
        return false;
    }
    std::u16string html;
    for (std::size_t index = 0; index + 3 < text.size(); index += 2) {
        html.push_back(static_cast<char16_t>(text[index] | text[index + 1] << 8));
    }
    out.assign(body, body + 5);
    for (const char16_t c : Lu4Html(html)) {
        out.push_back(static_cast<std::uint8_t>(c));
        out.push_back(static_cast<std::uint8_t>(c >> 8));
    }
    PutZeros(out, 2);
    PutInt32(out, 0);
    return true;
}

// Native c 0x00, d objectId, d village, clan hall, castle, siege camp, 0,
// fixed, fortress, revival time, limited fixed, c, d, d count, items -> LU4
// (Lucera Die) c 0x00, d objectId, d x10, c 0: the first nine choices kept.
bool TranslateDie(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 41) {
        return false;
    }
    out.assign(body, body + 41);
    PutInt32(out, 0);
    out.push_back(0);
    return true;
}

// Native c 0x27, d objectId, d action, d 0 -> LU4 without the last dword.
bool TranslateSocialAction(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 9) {
        return false;
    }
    out.assign(body, body + 9);
    return true;
}

// Native c 0x4a, d objectId, d type, S name, d npc string, S text, ... -> LU4
// the same, ending after the text.
bool TranslateSay(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    Reader reader(body, length, 9);
    reader.Text();
    reader.Int32();
    reader.Text();
    if (!reader.Ok()) {
        return false;
    }
    out.assign(body, body + reader.Position());
    return true;
}

// The LU4 client has no reader for the native NPC say (c 0x30, d objectId,
// d type, d npc, d npc string, S text or S x5 [d x5]); its chat reader (rva
// 0x4c0a770) takes c 0x4a, d objectId, d type, S name, d npc string, S text
// or S x5. With the NPC types 22 (say) and 23 (shout) it shows the NpcString
// but never a name; as 0 and 1 with an NpcString it showed an empty line. So
// the line goes as an ordinary chat line (0 or 1, npc string -1) with the
// name and the text built here from lu4_npcname.txt and lu4_npcstring.txt
// beside the server (tools/client/lu4_npc_tables.py writes them); a string
// missing there keeps the NPC type and its id.
std::map<std::int32_t, std::u16string> LoadNpcTable(const char* path) {
    std::map<std::int32_t, std::u16string> loaded;
    std::ifstream file(path, std::ios::binary);
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos) {
            continue;
        }
        std::string value;
        for (std::size_t index = tab + 1; index < line.size(); ++index) {
            if (line[index] == '\\' && index + 1 < line.size()) {
                const char escaped = line[++index];
                value.push_back(escaped == 'n' ? '\n' : escaped == 't' ? '\t' : escaped);
            } else {
                value.push_back(line[index]);
            }
        }
        loaded[std::atoi(line.c_str())] = Utf8ToUtf16(value);
    }
    L2_LOG_INFO(Log, "[lu4] Loaded %d entries from %s.", static_cast<int>(loaded.size()), path);
    return loaded;
}

std::u16string FromUtf16Bytes(const std::vector<std::uint8_t>& text) {
    std::u16string value;
    for (std::size_t index = 0; index + 3 < text.size(); index += 2) {
        value.push_back(static_cast<char16_t>(text[index] | text[index + 1] << 8));
    }
    return value;
}

void PutUtf16(std::vector<std::uint8_t>& out, const std::u16string& text) {
    for (const char16_t c : text) {
        out.push_back(static_cast<std::uint8_t>(c));
        out.push_back(static_cast<std::uint8_t>(c >> 8));
    }
    PutZeros(out, 2);
}

// The NpcString with $s1..$s5 replaced; the strings' own "\n" (a backslash
// and an n) becomes a space, as a chat line has one line.
std::u16string NpcStringText(const std::u16string& format, const std::u16string (&params)[5]) {
    std::u16string text;
    for (std::size_t index = 0; index < format.size(); ++index) {
        const char16_t c = format[index];
        if (c == u'$' && index + 2 < format.size() && (format[index + 1] == u's' || format[index + 1] == u'S') &&
            format[index + 2] >= u'1' && format[index + 2] <= u'5') {
            text += params[format[index + 2] - u'1'];
            index += 2;
        } else if (c == u'\\' && index + 1 < format.size() && format[index + 1] == u'n') {
            text.push_back(u' ');
            ++index;
        } else {
            text.push_back(c == u'\n' ? u' ' : c);
        }
    }
    return text;
}

bool TranslateNpcSay(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    static const std::map<std::int32_t, std::u16string> names = LoadNpcTable("lu4_npcname.txt");
    static const std::map<std::int32_t, std::u16string> strings = LoadNpcTable("lu4_npcstring.txt");
    Reader reader(body, length, 13);
    const std::int32_t message = reader.Int32();
    const int start = reader.Position();
    std::u16string params[5];
    for (int index = message == -1 ? 4 : 0; index < 5; ++index) {
        params[index] = FromUtf16Bytes(reader.Text());
    }
    if (!reader.Ok()) {
        return false;
    }
    const auto type = static_cast<std::int32_t>(runtime::LoadLittleEndian32(body + 5));
    const auto name = names.find(static_cast<std::int32_t>(runtime::LoadLittleEndian32(body + 9)) - 1000000);
    const auto format = message == -1 ? strings.end() : strings.find(message);
    out = {kNativeSay};
    out.insert(out.end(), body + 1, body + 5);
    if (message != -1 && format == strings.end()) {
        out.insert(out.end(), body + 5, body + 9);
        PutUtf16(out, name != names.end() ? name->second : std::u16string());
        PutInt32(out, static_cast<std::uint32_t>(message));
        out.insert(out.end(), body + start, body + reader.Position());
        return true;
    }
    PutInt32(out, type == 22 ? 0U : type == 23 ? 1U : static_cast<std::uint32_t>(type));
    PutUtf16(out, name != names.end() ? name->second : std::u16string());
    PutInt32(out, 0xffffffffU);
    PutUtf16(out, message == -1 ? params[4] : NpcStringText(format->second, params));
    return true;
}

// Native c 0xce, c 2, d objectId, d relation, c attackable, d karma, c pvp
// flag; c 4, h count, count x the same; c 1, d objectId (hidden in a pledge
// game) -> LU4 (the client's reader, rva 0x4c15a30, official capture) c 0xce,
// d count, count x (d objectId, d relation, d attackable, d karma, d pvp flag).
// The relation bits up to the siege ones are the Interlude ones; the clan war
// bits become its 0x8000 (mutual) and 0x10000 (one-sided).
std::uint32_t Lu4Relation(std::uint32_t relation) {
    constexpr std::uint32_t kWarOnYou = 0x4000;
    constexpr std::uint32_t kWarOnMe = 0x8000;
    std::uint32_t result = relation & 0x1fffU;
    if ((relation & (kWarOnYou | kWarOnMe)) == (kWarOnYou | kWarOnMe)) {
        result |= 0x8000U;
    } else if ((relation & (kWarOnYou | kWarOnMe)) != 0) {
        result |= 0x10000U;
    }
    return result;
}

bool TranslateRelationChanged(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 2) {
        return false;
    }
    int count = 1;
    int position = 2;
    if (body[1] == 4) {
        if (length < 4) {
            return false;
        }
        count = body[2] | body[3] << 8;
        position = 4;
    } else if (body[1] == 1) {
        if (length < 6) {
            return false;
        }
        out = {body[0]};
        PutInt32(out, 1);
        out.insert(out.end(), body + 2, body + 6);
        PutZeros(out, 16);
        return true;
    } else if (body[1] != 2) {
        return false;
    }
    if (position + count * 14 > length) {
        return false;
    }
    out = {body[0]};
    PutInt32(out, static_cast<std::uint32_t>(count));
    for (int index = 0; index < count; ++index, position += 14) {
        out.insert(out.end(), body + position, body + position + 4);
        PutInt32(out, Lu4Relation(runtime::LoadLittleEndian32(body + position + 4)));
        PutInt32(out, body[position + 8]);
        PutInt32(out, Lu4Karma(static_cast<std::int32_t>(runtime::LoadLittleEndian32(body + position + 9))));
        PutInt32(out, body[position + 13]);
    }
    return true;
}

// Native c 0x62, h message, c count, count x (c type, value) -> LU4
// (Interlude form, official capture) c 0x62, d message, d count, count x
// (d type, value). The type numbers are the same; the values widen to the
// LU4 sizes: 0 text and 12 player S; 1 number, 2 npc, 3 item, 11 door, 14,
// 17, 22 d; 4 skill d id, d level (low half); 5, 10, 13, 15, 21, 23 h -> d;
// 6 Q; 7 zone d x3; 8 item d -> d, h 0, h 0; 9 element c -> d; 16 damage
// d x3; 19, 20, 24, 25 c.
bool TranslateSystemMessage(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 4) {
        return false;
    }
    const int count = body[3];
    out = {body[0]};
    PutInt32(out, static_cast<std::uint32_t>(body[1] | body[2] << 8));
    PutInt32(out, static_cast<std::uint32_t>(count));
    int position = 4;
    auto need = [&](int bytes) { return position + bytes <= length; };
    auto take = [&](int bytes) {
        std::uint32_t value = 0;
        for (int index = bytes - 1; index >= 0; --index) {
            value = value << 8 | body[position + index];
        }
        position += bytes;
        return value;
    };
    for (int index = 0; index < count; ++index) {
        if (!need(1)) {
            return false;
        }
        const int type = body[position++];
        PutInt32(out, static_cast<std::uint32_t>(type));
        switch (type) {
        case 0:
        case 12: {
            Reader reader(body, length, position);
            const std::vector<std::uint8_t> text = reader.Text();
            if (!reader.Ok()) {
                return false;
            }
            out.insert(out.end(), text.begin(), text.end());
            position = reader.Position();
            break;
        }
        case 1: case 2: case 3: case 11: case 14: case 17: case 22:
            if (!need(4)) {
                return false;
            }
            PutInt32(out, take(4));
            break;
        case 4:
            if (!need(8)) {
                return false;
            }
            PutInt32(out, take(4));
            PutInt32(out, take(4) & 0xffff);
            break;
        case 5: case 10: case 13: case 15: case 21: case 23:
            if (!need(2)) {
                return false;
            }
            PutInt32(out, take(2));
            break;
        case 6:
            if (!need(8)) {
                return false;
            }
            out.insert(out.end(), body + position, body + position + 8);
            position += 8;
            break;
        case 7: case 16:
            if (!need(12)) {
                return false;
            }
            out.insert(out.end(), body + position, body + position + 12);
            position += 12;
            break;
        case 8:
            if (!need(4)) {
                return false;
            }
            PutInt32(out, take(4));
            PutInt32(out, 0);
            break;
        case 9:
            if (!need(1)) {
                return false;
            }
            PutInt32(out, take(1));
            break;
        case 19: case 20: case 24: case 25:
            if (!need(1)) {
                return false;
            }
            out.push_back(static_cast<std::uint8_t>(take(1)));
            break;
        default:
            return false;
        }
    }
    return true;
}

// Inventory. A native item (ItemAssembler.cpp, CItemAssemblerSendToCLNT) is
// c flags, d objectId, d item, c slot (255 equipped), Q count, c type2,
// c blessed, h equipped, Q body part, c enchant, c damaged, d mana, d period,
// c usable, h locked, then by flag: 0x01 d d variation, 0x02 h x8
// attributes, 0x04 d x3 enchant options, 0x08 d appearance, 0x10 c n, d x n,
// c n, d x n soul crystals, 0x20 h uses, 0x40 d reuse.
// LU4 (Lucera ItemList.writeLu4, official capture) has 32 bytes per item:
// h 0, d objectId, d item, c slot, d count, d 0, c type2, c equipped,
// d body part, 7 bytes 0.
struct NativeItem {
    std::int32_t objectId = 0;
    std::int32_t itemId = 0;
    std::int64_t count = 0;
    std::uint8_t slot = 0;
    std::uint8_t type2 = 0;
    std::uint8_t equipped = 0;
    std::uint32_t bodyPart = 0;
    std::uint8_t enchant = 0;
};

bool ReadItem(const std::uint8_t* body, int length, int& position, NativeItem& item) {
    if (position + 43 > length) {
        return false;
    }
    const std::uint8_t flags = body[position];
    item.slot = body[position + 9];
    Reader reader(body, length, position + 1);
    item.objectId = reader.Int32();
    item.itemId = reader.Int32();
    reader.Seek(reader.Position() + 1);
    item.count = reader.Int64();
    item.type2 = body[reader.Position()];
    item.equipped = body[reader.Position() + 2] | body[reader.Position() + 3];
    reader.Seek(reader.Position() + 4);
    item.bodyPart = static_cast<std::uint32_t>(reader.Int64());
    item.enchant = reader.Position() < length ? body[reader.Position()] : 0;
    position = reader.Position() + 1 + 1 + 4 + 4 + 1 + 2;
    static constexpr std::pair<std::uint8_t, int> kFixed[] = {{0x01, 8}, {0x02, 16}, {0x04, 12}, {0x08, 4}};
    for (const auto& [flag, size] : kFixed) {
        if ((flags & flag) != 0) {
            position += size;
        }
    }
    if ((flags & 0x10) != 0) {
        for (int list = 0; list < 2; ++list) {
            if (position >= length) {
                return false;
            }
            position += 1 + body[position] * 4;
        }
    }
    if ((flags & 0x20) != 0) {
        position += 2;
    }
    if ((flags & 0x40) != 0) {
        position += 4;
    }
    return position <= length;
}

void PutItem(std::vector<std::uint8_t>& out, const NativeItem& item) {
    PutZeros(out, 2);
    PutInt32(out, static_cast<std::uint32_t>(item.objectId));
    PutInt32(out, static_cast<std::uint32_t>(item.itemId));
    out.push_back(item.equipped != 0 ? 0xff : item.slot);
    PutInt32(out, static_cast<std::uint32_t>(std::min<std::int64_t>(item.count, 0x7fffffff)));
    PutInt32(out, 0);
    out.push_back(item.type2);
    out.push_back(item.equipped != 0 ? 1 : 0);
    PutInt32(out, item.bodyPart);
    PutZeros(out, 7);
}

struct InventoryState {
    int expected = -1;
    bool openWindow = false;
    std::vector<NativeItem> items;
    std::int32_t equipment[60][2] = {};
};

std::mutex g_inventoriesLock;
std::map<const CSocket*, InventoryState> g_inventories;
// Each socket's adena item, from the item lists and updates: the LU4 client
// has no adena count packet, so a new count goes to it as an update of this.
std::map<const CSocket*, NativeItem> g_adena;
constexpr std::int32_t kAdenaItem = 57;

void ForgetInventory(const CSocket* socket) {
    const std::lock_guard guard(g_inventoriesLock);
    g_inventories.erase(socket);
    g_adena.erase(socket);
}

void BuildItemList(InventoryState& state, std::vector<std::uint8_t>& out) {
    out = {0x11, static_cast<std::uint8_t>(state.openWindow ? 1 : 0)};
    PutInt32(out, static_cast<std::uint32_t>(state.items.size()));
    for (const NativeItem& item : state.items) {
        PutItem(out, item);
    }
    state.items.clear();
    state.expected = -1;
    state.openWindow = false;
}

// Native: c 0x11, c 1, h block, h 0, d total; then c 0x11, c 2, d total,
// d count, count items, as often as the list needs. LU4 takes one list:
// c 0x11, c open window, d count, items. An empty out drops the packet.
bool TranslateItemList(const CSocket* socket, const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 10) {
        return false;
    }
    const std::lock_guard guard(g_inventoriesLock);
    InventoryState& state = g_inventories[socket];
    out.clear();
    if (body[1] == 1) {
        state.items.clear();
        state.expected = static_cast<int>(runtime::LoadLittleEndian32(body + length - 4));
        if (state.expected <= 0) {
            BuildItemList(state, out);
        }
        return true;
    }
    const int count = static_cast<int>(runtime::LoadLittleEndian32(body + 6));
    int position = 10;
    for (int index = 0; index < count; ++index) {
        NativeItem item;
        if (!ReadItem(body, length, position, item)) {
            return false;
        }
        if (item.itemId == kAdenaItem) {
            g_adena[socket] = item;
        }
        state.items.push_back(item);
    }
    if (state.expected >= 0 && static_cast<int>(state.items.size()) >= state.expected) {
        BuildItemList(state, out);
    }
    return true;
}

// Native c 0x21, c 0, d total, d count, count x (h change, item) -> LU4
// c 0x21, d count, count x (h change, the 32-byte item).
bool TranslateInventoryUpdate(const CSocket* socket, const std::uint8_t* body, int length,
                              std::vector<std::uint8_t>& out) {
    if (length < 10) {
        return false;
    }
    const int count = static_cast<int>(runtime::LoadLittleEndian32(body + 6));
    out = {0x21};
    PutInt32(out, static_cast<std::uint32_t>(count));
    int position = 10;
    for (int index = 0; index < count; ++index) {
        if (position + 2 > length) {
            return false;
        }
        const std::uint8_t change[2] = {body[position], body[position + 1]};
        position += 2;
        NativeItem item;
        if (!ReadItem(body, length, position, item)) {
            return false;
        }
        if (item.itemId == kAdenaItem) {
            const std::lock_guard guard(g_inventoriesLock);
            if (change[0] == 3) { // removed
                g_adena.erase(socket);
            } else {
                g_adena[socket] = item;
            }
        }
        out.insert(out.end(), change, change + 2);
        PutItem(out, item);
    }
    return true;
}

// Native Ex 0x13e (ExAdenaInvenCount): Q adena, h inventory count. The LU4
// client has no reader for it (merchant sales change the adena only through
// it), so it becomes an update of the adena item; dropped while that is unknown.
bool TranslateAdenaCount(const CSocket* socket, const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 11) {
        return false;
    }
    out.clear();
    const std::lock_guard guard(g_inventoriesLock);
    const auto adena = g_adena.find(socket);
    if (adena == g_adena.end()) {
        return true;
    }
    adena->second.count = static_cast<std::int64_t>(runtime::LoadLittleEndian64(body + 3));
    out = {0x21};
    PutInt32(out, 1);
    out.push_back(2); // modified
    out.push_back(0);
    PutItem(out, adena->second);
    return true;
}

// Native c 0xfe, h 0x156, d objectId, h slot count (60), 8 mask bytes, then
// per marked slot h 22, d objectId, d item, d option x2, d appearance. Slot
// numbers are the paperdoll's (7 right hand ... 16 hair 2). LU4 (Lucera
// Lu4ExEquipUpdate): c 0xfe, h 0x156, c 10, per slot c slot, d objectId,
// d item, 8 bytes 0, for slots 7-14, 16 and 18.
bool TranslateEquipSlots(const CSocket* socket, const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 17) {
        return false;
    }
    const int count = std::min(body[7] | body[8] << 8, 60);
    const std::uint8_t* const mask = body + 9;
    int position = 9 + (count + 7) / 8;
    const std::lock_guard guard(g_inventoriesLock);
    InventoryState& state = g_inventories[socket];
    for (int slot = 0; slot < count; ++slot) {
        if ((mask[slot / 8] & 0x80 >> slot % 8) == 0) {
            continue;
        }
        if (position + 2 > length) {
            return false;
        }
        const int size = body[position] | body[position + 1] << 8;
        if (size < 10 || position + size > length) {
            return false;
        }
        state.equipment[slot][0] = static_cast<std::int32_t>(runtime::LoadLittleEndian32(body + position + 2));
        state.equipment[slot][1] = static_cast<std::int32_t>(runtime::LoadLittleEndian32(body + position + 6));
        position += size;
    }
    static constexpr std::uint8_t kSlots[] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 18};
    out = {0xfe, 0x56, 0x01, static_cast<std::uint8_t>(std::size(kSlots))};
    for (const std::uint8_t slot : kSlots) {
        out.push_back(slot);
        PutInt32(out, static_cast<std::uint32_t>(state.equipment[slot][0]));
        PutInt32(out, static_cast<std::uint32_t>(state.equipment[slot][1]));
        PutZeros(out, 8);
    }
    return true;
}

// Skills. Native c 0x5f, d count, count x (d passive, d level, d skill,
// d reuse group, c disabled, c enchanted), d learned -> LU4 (Lucera
// SkillList.writeLu4, official capture) c 0x5f, d count, count x (h skill,
// h level, c passive, d reuse group, h 0).
bool TranslateSkillList(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 5) {
        return false;
    }
    const int count = static_cast<int>(runtime::LoadLittleEndian32(body + 1));
    if (count < 0 || 5 + count * 18 > length) {
        return false;
    }
    out = {0x5f};
    PutInt32(out, static_cast<std::uint32_t>(count));
    for (int index = 0; index < count; ++index) {
        const std::uint8_t* const skill = body + 5 + index * 18;
        out.insert(out.end(), {skill[8], skill[9], skill[4], skill[5], skill[0]});
        out.insert(out.end(), skill + 12, skill + 16);
        PutZeros(out, 2);
    }
    return true;
}

// Native c 0x48, d type, d caster, d target, d skill, d level (sub-level in the
// high half), d hit time, d reuse group, d reuse, d x, y, z, h n, h x n,
// h 0, d target x, y, z, d type, d parameter -> LU4 (official capture, 53
// bytes with no list) without type, reuse group and the last two dwords.
bool TranslateMagicSkillUse(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 65) {
        return false;
    }
    out = {body[0]};
    out.insert(out.end(), body + 5, body + 17);
    out.insert(out.end(), {body[17], body[18], 0, 0});
    out.insert(out.end(), body + 21, body + 25);
    out.insert(out.end(), body + 29, body + length - 8);
    return true;
}

// Native c 0x54, d type, d caster, d skill, d level, d count, d targets ->
// LU4 (official capture, 21 bytes for one target) without the type.
bool TranslateMagicSkillLaunched(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 21) {
        return false;
    }
    out = {body[0]};
    out.insert(out.end(), body + 5, body + 13);
    out.insert(out.end(), {body[13], body[14], 0, 0});
    out.insert(out.end(), body + 17, body + length);
    return true;
}

// Shortcuts. Native entries: d type, d slot, then item (1) d objectId,
// d category, d reuse group, d reuse remaining, d reuse, d option x2,
// d appearance; skill (2) d skill, d level, d reuse group, c locked,
// d category (registration adds d d); others d id, d category. LU4 (Lucera
// ShortCutPacket, official capture): h type, h slot, then item d objectId,
// d reuse group, d reuse remaining, d reuse; skill h skill, h level, d -1,
// c 0; others d id.
bool PutShortcut(const std::uint8_t* body, int length, int& position, bool registration, std::vector<std::uint8_t>& out) {
    if (position + 12 > length) {
        return false;
    }
    const auto type = runtime::LoadLittleEndian32(body + position);
    const std::uint8_t* const entry = body + position;
    out.insert(out.end(), {entry[0], entry[1], entry[4], entry[5]});
    if (type == 1) {
        if (position + 40 > length) {
            return false;
        }
        out.insert(out.end(), entry + 8, entry + 12);
        out.insert(out.end(), entry + 16, entry + 28);
        position += 40;
    } else if (type == 2) {
        const int size = registration ? 33 : 25;
        if (position + size > length) {
            return false;
        }
        out.insert(out.end(), {entry[8], entry[9], entry[12], entry[13]});
        PutInt32(out, 0xffffffffU);
        out.push_back(0);
        position += size;
    } else {
        if (position + 16 > length) {
            return false;
        }
        out.insert(out.end(), entry + 8, entry + 12);
        position += 16;
    }
    return true;
}

bool TranslateShortcutInit(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 5) {
        return false;
    }
    const int count = static_cast<int>(runtime::LoadLittleEndian32(body + 1));
    out = {body[0]};
    PutInt32(out, static_cast<std::uint32_t>(count));
    int position = 5;
    for (int index = 0; index < count; ++index) {
        if (!PutShortcut(body, length, position, false, out)) {
            return false;
        }
    }
    return true;
}

bool TranslateShortcutRegister(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    int position = 1;
    out = {body[0]};
    return PutShortcut(body, length, position, true, out);
}

// Other players. Native (UserBroadcast.cpp, User::SendCharInfo_internal):
// c 0x31, c broadcast, d x, y, z, d vehicle, d objectId, S name, h race,
// c sex, d base class, d x12 paperdoll (under, head, right hand, left hand,
// gloves, chest, legs, feet, back, both hands, hair, hair 2), d x6
// variations, c, d x9 appearances, c pvp flag, d reputation, d cast speed, d skill speed,
// h x8 speeds, double move rate, attack rate, collision radius, height,
// d face, hair style, hair color, S title, d x4 pledge, c standing, c running,
// c in combat, c dead, c x3, h cubics, h x cubics, c, c, h, d x3, c, c, d,
// c x3, d x3 fishing, d name color, d heading, c, h, d title color, ...
// LU4 (10 official packets; Lucera never sent it): c 0x31, c, d objectId,
// S name, S title, then 229 bytes:
//   +0 name color, +4 title color, +8 c race, +9 c sex, +10 base class,
//   +14 class (the native mount/class lane after the cubics), +18 hair style, color, face, +32 attack speed, +36 cast speed,
//   +40 x, y, z, +56 h x8 speeds, +72 float move rate, attack rate,
//   collision radius, height, +111 c pvp flag, +112 d karma,
//   +125 d x11 paperdoll (head .. hair 2),
//   +196 c standing, +197 c running, +198 c in combat, +199 c dead,
//   +210 heading, +224 c 1 (in all samples); the rest is 0 in the samples.
bool TranslateCharInfo(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    Reader reader(body, length, 2);
    for (int index = 0; index < 4; ++index) {
        reader.Int32();
    }
    const std::int32_t objectId = reader.Int32();
    const std::vector<std::uint8_t> name = reader.Text();
    const int afterName = reader.Position();
    if (!reader.Ok() || afterName + 3 > length) {
        return false;
    }
    const int race = body[afterName];
    const int sex = body[afterName + 2];
    reader.Seek(afterName + 3);
    const std::int32_t baseClass = reader.Int32();
    std::int32_t paperdoll[12] = {};
    for (std::int32_t& item : paperdoll) {
        item = reader.Int32();
    }
    reader.Seek(reader.Position() + 6 * 4 + 1 + 9 * 4);
    const int guilty = reader.Position();
    reader.Seek(guilty + 1);
    const std::int32_t reputation = reader.Int32();
    const std::int32_t castSpeed = reader.Int32();
    const std::int32_t attackSpeed = reader.Int32();
    const int speeds = reader.Position();
    reader.Seek(speeds + 16);
    double rates[4] = {};
    for (double& rate : rates) {
        rate = reader.Double();
    }
    const std::int32_t face = reader.Int32();
    const std::int32_t hairStyle = reader.Int32();
    const std::int32_t hairColor = reader.Int32();
    const std::vector<std::uint8_t> title = reader.Text();
    reader.Seek(reader.Position() + 16);
    const int flags = reader.Position();
    reader.Seek(flags + 7);
    if (!reader.Ok() || flags + 9 > length) {
        return false;
    }
    const int cubics = body[flags + 7] | body[flags + 8] << 8;
    // c, c, h, d mount, d class, d, c, c, d, c x3, d x3 fishing
    const int afterCubics = flags + 9 + cubics * 2;
    reader.Seek(afterCubics + 1 + 1 + 2 + 4);
    const std::int32_t classId = reader.Int32();
    reader.Seek(afterCubics + 1 + 1 + 2 + 12 + 1 + 1 + 4 + 3 + 12);
    const std::int32_t nameColor = reader.Int32();
    const std::int32_t heading = reader.Int32();
    reader.Seek(reader.Position() + 1 + 2);
    const std::int32_t titleColor = reader.Int32();
    if (!reader.Ok()) {
        return false;
    }

    out = {body[0], body[1]};
    PutInt32(out, static_cast<std::uint32_t>(objectId));
    out.insert(out.end(), name.begin(), name.end());
    out.insert(out.end(), title.begin(), title.end());
    const std::size_t start = out.size();
    PutInt32(out, static_cast<std::uint32_t>(nameColor));
    PutInt32(out, static_cast<std::uint32_t>(titleColor));
    out.push_back(static_cast<std::uint8_t>(race));
    out.push_back(static_cast<std::uint8_t>(sex));
    PutInt32(out, static_cast<std::uint32_t>(baseClass));
    PutInt32(out, static_cast<std::uint32_t>(classId));
    PutLooks(out, sex, hairStyle, hairColor, face);
    PutZeros(out, 2);
    // A character's first broadcast goes out before its stats exist: speeds 0,
    // attack rate infinite. The client gets ordinary values until the next one.
    PutInt32(out, static_cast<std::uint32_t>(attackSpeed > 0 ? attackSpeed : 333));
    PutInt32(out, static_cast<std::uint32_t>(castSpeed > 0 ? castSpeed : 333));
    out.insert(out.end(), body + 2, body + 14); // x, y, z
    PutInt32(out, 0);
    out.insert(out.end(), body + speeds, body + speeds + 16);
    for (int index = 0; index < 4; ++index) {
        const double rate = rates[index];
        PutFloat(out, rate > 0 && rate < 1000 ? static_cast<float>(rate) : (index < 2 ? 1.0F : 10.0F));
    }
    // +111 c pvp flag, +112 d karma: the client fills the same structure as
    // from the UserInfo tail +240/+241 (a CharInfo without them shows white).
    PutZeros(out, start + 111 - out.size());
    out.push_back(body[guilty]);
    PutInt32(out, Lu4Karma(reputation));
    PutZeros(out, start + 125 - out.size());
    for (int slot = 1; slot < 12; ++slot) {
        PutInt32(out, static_cast<std::uint32_t>(paperdoll[slot]));
    }
    // +193 c, +194 c private store type, +195 c: the client keeps the store
    // type as CharacterStates.KioskType; that it is +194, the one after the
    // mount as in the Interlude CharInfo, was not checked against its reader.
    PutZeros(out, start + 194 - out.size());
    out.push_back(body[flags + 6]);
    PutZeros(out, start + 196 - out.size());
    // Native stop mode: 0 sitting, 1 standing, 2 private store (sitting).
    out.insert(out.end(), {static_cast<std::uint8_t>(body[flags] == 1 ? 1 : 0), body[flags + 1], body[flags + 2], body[flags + 3]});
    PutZeros(out, start + 210 - out.size());
    PutInt32(out, static_cast<std::uint32_t>(heading));
    PutZeros(out, start + 224 - out.size());
    out.push_back(1);
    PutZeros(out, start + 229 - out.size());
    return true;
}

// Character creation. Native c 0x0d, d count, count x (d race, d class,
// d x18: STR, DEX, CON, INT, WIT, MEN as maximum, recommended, minimum) ->
// LU4 (Lucera NewCharacterSuccess.writeLu4) c 0x0d, d 0, d count, count x
// (c race, d class, d STR, DEX, CON, INT, WIT, MEN).
bool TranslateNewCharacter(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 5) {
        return false;
    }
    const int count = static_cast<int>(runtime::LoadLittleEndian32(body + 1));
    if (count < 0 || 5 + count * 80 > length) {
        return false;
    }
    // The official list has races 0-5 only (Kamael included); this server's also
    // offers Ertheia (race 6), which LU4 does not have.
    int kept = 0;
    for (int index = 0; index < count; ++index) {
        kept += body[5 + index * 80] <= 5 ? 1 : 0;
    }
    out = {body[0]};
    PutInt32(out, 0);
    PutInt32(out, static_cast<std::uint32_t>(kept));
    for (int index = 0; index < count; ++index) {
        const std::uint8_t* const entry = body + 5 + index * 80;
        if (entry[0] > 5) {
            continue;
        }
        out.push_back(entry[0]);
        out.insert(out.end(), entry + 4, entry + 8);
        for (int stat = 0; stat < 6; ++stat) {
            out.insert(out.end(), entry + 8 + stat * 12 + 4, entry + 8 + stat * 12 + 8);
        }
    }
    return true;
}

// Native creation results c 0x0f, d 1 and c 0x10, d reason -> LU4 c 0x7d,
// d 0 when created, else the reason (never 0).
bool TranslateCreateResult(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 5) {
        return false;
    }
    const std::uint32_t reason = body[0] == 0x0f ? 0 : runtime::LoadLittleEndian32(body + 1);
    out = {0x7d};
    PutInt32(out, body[0] == 0x0f ? 0 : (reason == 0 ? 3 : reason));
    return true;
}

// Skill learning. Native c 0x90, h count, count x (d skill, d level, Q sp,
// c level needed, c dual class level, c n, n x (d item, Q count), c m, m x
// (d skill, d level)) -> LU4 (Lucera Lu4AcquireSkillList, official capture)
// c 0x90, d 0, d count, count x (h skill, h level, d sp, d 0, c level needed,
// c new, c 1, d item, d count, d 0 or c 0, c 0).
bool TranslateAcquireSkillList(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 3) {
        return false;
    }
    const int count = body[1] | body[2] << 8;
    out = {body[0]};
    PutInt32(out, 0);
    PutInt32(out, static_cast<std::uint32_t>(count));
    int position = 3;
    for (int index = 0; index < count; ++index) {
        if (position + 19 > length) {
            return false;
        }
        const std::uint8_t* const entry = body + position;
        const int items = entry[18];
        position += 19;
        if (position + items * 12 + 1 > length) {
            return false;
        }
        const std::uint8_t* const item = body + position;
        position += items * 12;
        position += 1 + body[position] * 8;
        out.insert(out.end(), {entry[0], entry[1], entry[4], entry[5]});
        out.insert(out.end(), entry + 8, entry + 12);
        PutInt32(out, 0);
        out.push_back(entry[16]);
        out.push_back(entry[4] == 1 && entry[5] == 0 ? 1 : 0);
        if (items > 0) {
            out.push_back(1);
            out.insert(out.end(), item, item + 4);
            out.insert(out.end(), item + 4, item + 8);
            PutInt32(out, 0);
        } else {
            out.push_back(0);
        }
        out.push_back(0);
    }
    return position <= length;
}

// Native c 0xfe, h 0xfc, d skill, d level, Q sp, h level needed, h dual class
// level, d n, items, ... -> LU4 (Lucera AcquireSkillInfo, 25 bytes) c 0xfe,
// h 0xfc, h skill, h level, d sp, d 0, d level needed, d 0, h 0.
bool TranslateAcquireSkillInfo(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 23) {
        return false;
    }
    out = {body[0], body[1], body[2], body[3], body[4], body[7], body[8]};
    out.insert(out.end(), body + 11, body + 15);
    PutInt32(out, 0);
    out.insert(out.end(), {body[19], body[20], 0, 0});
    PutInt32(out, 0);
    PutZeros(out, 2);
    return true;
}

// Buffs. Native c 0x85, h count, count x (d skill, d level (sub-level in the
// high half), d caster, h seconds left; 0x7fff and d seconds when longer) ->
// LU4 (official capture, 12 bytes each) c 0x85, h count, count x (d skill,
// h level, d caster, h seconds left).
bool TranslateAbnormalStatus(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 3) {
        return false;
    }
    const int count = body[1] | body[2] << 8;
    out = {body[0], body[1], body[2]};
    int position = 3;
    for (int index = 0; index < count; ++index) {
        if (position + 14 > length) {
            return false;
        }
        const std::uint8_t* const entry = body + position;
        out.insert(out.end(), entry, entry + 4);
        out.insert(out.end(), {entry[4], entry[5]});
        out.insert(out.end(), entry + 8, entry + 12);
        out.insert(out.end(), {entry[12], entry[13]});
        position += 14 + (entry[12] == 0xff && entry[13] == 0x7f ? 4 : 0);
    }
    return position <= length;
}

// Party. The client's readers (lu4-win64-shipping.exe, the S2C table at rva
// 0x7ea1c40) take this server's windows with additions. Native:
//   c 0x4e, d leader, c routing, c count, count x (d member, S name, d cp,
//     max cp, hp, max hp, mp, max mp, vitality, c level, h class, c sex,
//     h race, d n, n x summon (d, d, c, S, d x4, c))
//   c 0x4f, d leader, d routing, d member, S name, d x7, c level, h class,
//     c sex, h race
//   c 0x52, d member, h mask, then the changed values: 0x01 d cp, 0x02 max cp,
//     0x04 hp, 0x08 max hp, 0x10 mp, 0x20 max mp, 0x40 c level, 0x80 h class,
//     0x200 d vitality
//   c 0xf4, d type, d object, d count, count x (d skill, d level, d caster,
//     h seconds left; 0x7fff and d seconds when longer)
// LU4 reads 4 more bytes after each member's race (before the summons in
// 0x4e, at the end in 0x4f); 0x52 is unmasked, d member, d cp, max cp, hp,
// max hp, mp, max mp, c level, h class, d vitality, then the same 4 bytes;
// 0xf4 entries are d skill, h level, d seconds left. 0x39, 0x3a, 0x50, 0x51
// and 0xba are this server's.
struct PartyMember {
    std::int32_t vitals[6] = {};
    std::int32_t vitality = 0;
    std::uint8_t level = 0;
    std::uint16_t classId = 0;
};

std::mutex g_partiesLock;
std::map<const CSocket*, std::map<std::int32_t, PartyMember>> g_parties;

void ForgetParty(const CSocket* socket) {
    const std::lock_guard guard(g_partiesLock);
    g_parties.erase(socket);
}

// Reads d member, S name, d x7, c level, h class, c sex, h race.
bool ReadPartyMember(Reader& reader, const std::uint8_t* body, int length, std::int32_t& memberId,
                     PartyMember& member) {
    memberId = reader.Int32();
    reader.Text();
    for (std::int32_t& value : member.vitals) {
        value = reader.Int32();
    }
    member.vitality = reader.Int32();
    const int at = reader.Position();
    reader.Seek(at + 6);
    if (!reader.Ok() || reader.Position() > length) {
        return false;
    }
    member.level = body[at];
    member.classId = static_cast<std::uint16_t>(body[at + 1] | body[at + 2] << 8);
    return true;
}

bool TranslatePartyAll(const CSocket* socket, const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 7) {
        return false;
    }
    const int count = body[6];
    out.assign(body, body + 7);
    Reader reader(body, length, 7);
    std::map<std::int32_t, PartyMember> members;
    for (int index = 0; index < count; ++index) {
        const int start = reader.Position();
        std::int32_t memberId = 0;
        PartyMember member;
        if (!ReadPartyMember(reader, body, length, memberId, member)) {
            return false;
        }
        out.insert(out.end(), body + start, body + reader.Position());
        PutInt32(out, 0);
        const int summonsStart = reader.Position();
        const int summons = reader.Int32();
        for (int summon = 0; summon < summons && reader.Ok(); ++summon) {
            reader.Seek(reader.Position() + 9);
            reader.Text();
            reader.Seek(reader.Position() + 17);
        }
        if (!reader.Ok() || reader.Position() > length) {
            return false;
        }
        out.insert(out.end(), body + summonsStart, body + reader.Position());
        members[memberId] = member;
    }
    const std::lock_guard guard(g_partiesLock);
    g_parties[socket] = members;
    return true;
}

bool TranslatePartyAdd(const CSocket* socket, const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    Reader reader(body, length, 9);
    std::int32_t memberId = 0;
    PartyMember member;
    if (length < 9 || !ReadPartyMember(reader, body, length, memberId, member)) {
        return false;
    }
    out.assign(body, body + reader.Position());
    PutInt32(out, 0);
    const std::lock_guard guard(g_partiesLock);
    g_parties[socket][memberId] = member;
    return true;
}

bool TranslatePartyUpdate(const CSocket* socket, const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 7) {
        return false;
    }
    const auto memberId = static_cast<std::int32_t>(runtime::LoadLittleEndian32(body + 1));
    const int mask = body[5] | body[6] << 8;
    const std::lock_guard guard(g_partiesLock);
    auto& members = g_parties[socket];
    const auto found = members.find(memberId);
    if (found == members.end()) {
        return false;
    }
    PartyMember& member = found->second;
    Reader reader(body, length, 7);
    for (int index = 0; index < 6; ++index) {
        if ((mask & 1 << index) != 0) {
            member.vitals[index] = reader.Int32();
        }
    }
    if ((mask & 0x40) != 0) {
        const int at = reader.Position();
        reader.Seek(at + 1);
        member.level = at < length ? body[at] : member.level;
    }
    if ((mask & 0x80) != 0) {
        const int at = reader.Position();
        reader.Seek(at + 2);
        member.classId = at + 1 < length ? static_cast<std::uint16_t>(body[at] | body[at + 1] << 8) : member.classId;
    }
    // 0x100 has no known size; vitality follows it, so it is read only without it.
    if ((mask & 0x300) == 0x200) {
        member.vitality = reader.Int32();
    }
    if (!reader.Ok()) {
        return false;
    }
    out = {body[0]};
    PutInt32(out, static_cast<std::uint32_t>(memberId));
    for (const std::int32_t value : member.vitals) {
        PutInt32(out, static_cast<std::uint32_t>(value));
    }
    out.push_back(member.level);
    out.insert(out.end(), {static_cast<std::uint8_t>(member.classId), static_cast<std::uint8_t>(member.classId >> 8)});
    PutInt32(out, static_cast<std::uint32_t>(member.vitality));
    PutInt32(out, 0);
    return true;
}

bool TranslatePartySpelled(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 13) {
        return false;
    }
    const auto count = static_cast<std::int32_t>(runtime::LoadLittleEndian32(body + 9));
    out.assign(body, body + 13);
    int position = 13;
    for (std::int32_t index = 0; index < count; ++index) {
        if (position + 14 > length) {
            return false;
        }
        const std::uint8_t* const entry = body + position;
        out.insert(out.end(), entry, entry + 4);
        out.insert(out.end(), {entry[4], entry[5]});
        auto seconds = static_cast<std::int32_t>(static_cast<std::int16_t>(entry[12] | entry[13] << 8));
        position += 14;
        if (seconds == 0x7fff) {
            if (position + 4 > length) {
                return false;
            }
            seconds = static_cast<std::int32_t>(runtime::LoadLittleEndian32(body + position));
            position += 4;
        }
        PutInt32(out, static_cast<std::uint32_t>(seconds));
    }
    return true;
}

// Trade. This server sends each item packet as a package, a header
// (c op, c 1, ...) and data packets (c op, c type, d total, d count, items):
//   0x14 start: header d partner, c relation, [c level unless relation has
//     0x10], d item count; data type 0, the receiver's tradable items
//   0x1a own add, 0x1b other add: header d length; data type 2, items
//   0x81 update: header d length; data type 2, count x (h change, item)
// LU4 (the client's readers, rva 0x4c17280, 0x4c17110, 0x4c16ff0, 0x4c17470)
// takes one packet each, h counts and its own item records:
//   0x14: d partner, h count, count x (d object, d item, d 0, q count,
//     h type2, h custom1, h custom2, d body part, h enchant, h 0, d x3,
//     h attack attribute (0xfffe none), h power, q defenses, d, d, h)
//   0x1a, 0x1b: h count, count x (h type1, d object, d item, q count,
//     h type2, h custom1, d body part, h enchant, h custom2, h 0, h attack
//     attribute, h power, q defenses, d, d, h)
//   0x81: h count, count x (h change, h type1, d object, d item, q count,
//     h type2, h custom1, d body part, h enchant, h custom2, h 0, h attack
//     attribute, h power, q defenses, d)
// The requests, answers and confirmations (0x70, 0x53, 0x82, 0x1c) are the
// server's own.
struct TradeState {
    int expected = -1;
    std::int32_t partner = 0;
    std::vector<NativeItem> items;
};

std::mutex g_tradesLock;
std::map<const CSocket*, TradeState> g_trades;

void ForgetTrade(const CSocket* socket) {
    const std::lock_guard guard(g_tradesLock);
    g_trades.erase(socket);
}

void PutInt16(std::vector<std::uint8_t>& out, int value) {
    out.insert(out.end(), {static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value >> 8)});
}

void PutTradeCount(std::vector<std::uint8_t>& out, std::int64_t count) {
    PutInt32(out, static_cast<std::uint32_t>(count));
    PutInt32(out, static_cast<std::uint32_t>(static_cast<std::uint64_t>(count) >> 32));
}

// The record tail shared by 0x1a, 0x1b and 0x81, from h type2 on.
void PutTradeItemTail(std::vector<std::uint8_t>& out, const NativeItem& item) {
    PutInt16(out, item.type2);
    PutInt16(out, 0);
    PutInt32(out, item.bodyPart);
    PutInt16(out, item.enchant);
    PutInt16(out, 0);
    PutInt16(out, 0);
    PutInt16(out, 0xfffe);
    PutInt16(out, 0);
    PutZeros(out, 8);
}

// LU4's item record in the trade window and the stores, 68 bytes. The
// third dword is the item's key in the client's lists (the merchant's
// SellRefundList keeps a map by it), so each record of a list gets its own.
void PutItemRecord(std::vector<std::uint8_t>& out, const NativeItem& item, int key) {
    PutInt32(out, static_cast<std::uint32_t>(item.objectId));
    PutInt32(out, static_cast<std::uint32_t>(item.itemId));
    PutInt32(out, static_cast<std::uint32_t>(key));
    PutTradeCount(out, item.count);
    PutInt16(out, item.type2);
    PutInt16(out, 0);
    PutInt16(out, 0);
    PutInt32(out, item.bodyPart);
    PutInt16(out, item.enchant);
    PutInt16(out, 0);
    PutZeros(out, 12);
    PutInt16(out, 0xfffe);
    PutInt16(out, 0);
    PutZeros(out, 8 + 4 + 4 + 2);
}

bool BuildTradeStart(TradeState& state, std::vector<std::uint8_t>& out) {
    out = {0x14};
    PutInt32(out, static_cast<std::uint32_t>(state.partner));
    PutInt16(out, static_cast<int>(state.items.size()));
    for (std::size_t index = 0; index < state.items.size(); ++index) {
        PutItemRecord(out, state.items[index], static_cast<int>(index));
    }
    state = {};
    return true;
}

bool TranslateTradeStart(const CSocket* socket, const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 2) {
        return false;
    }
    const std::lock_guard guard(g_tradesLock);
    TradeState& state = g_trades[socket];
    if (body[1] == 1) {
        if (length < 11) {
            return false;
        }
        state = {};
        state.partner = static_cast<std::int32_t>(runtime::LoadLittleEndian32(body + 2));
        state.expected = static_cast<int>(runtime::LoadLittleEndian32(body + length - 4));
        if (state.expected <= 0) {
            return BuildTradeStart(state, out);
        }
        out.clear();
        return true;
    }
    out.clear();
    if (state.expected < 0 || length < 10) {
        return true; // an empty package after a list already sent
    }
    const int count = static_cast<int>(runtime::LoadLittleEndian32(body + 6));
    int position = 10;
    for (int index = 0; index < count; ++index) {
        NativeItem item;
        if (!ReadItem(body, length, position, item)) {
            return false;
        }
        state.items.push_back(item);
    }
    if (static_cast<int>(state.items.size()) >= state.expected) {
        return BuildTradeStart(state, out);
    }
    return true;
}

// 0x1a, 0x1b and 0x81 data packets; their headers are dropped.
bool TranslateTradeItems(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    out.clear();
    if (length < 2 || body[1] == 1) {
        return length >= 2;
    }
    if (length < 10) {
        return false;
    }
    const bool update = body[0] == 0x81;
    const int count = static_cast<int>(runtime::LoadLittleEndian32(body + 6));
    out = {body[0]};
    PutInt16(out, count);
    int position = 10;
    for (int index = 0; index < count; ++index) {
        int change = 0;
        if (update) {
            if (position + 2 > length) {
                return false;
            }
            change = body[position] | body[position + 1] << 8;
            position += 2;
        }
        NativeItem item;
        if (!ReadItem(body, length, position, item)) {
            return false;
        }
        if (update) {
            PutInt16(out, change);
        }
        PutInt16(out, 0);
        PutInt32(out, static_cast<std::uint32_t>(item.objectId));
        PutInt32(out, static_cast<std::uint32_t>(item.itemId));
        PutTradeCount(out, item.count);
        PutTradeItemTail(out, item);
        PutInt32(out, 0);
        if (!update) {
            PutInt32(out, 0);
            PutInt16(out, 0);
        }
    }
    return true;
}

// Private stores. This server sends the owner's management lists (0xa0
// sell, 0xbd buy) as packages: a header (c op, c 1, d owner, [d package
// sale], Q adena, d n, n x stored entries, d inventory count) and data
// packets (c op, c 2, d total, d count, count x (item, Q market price)).
// Stored sell entries are (item, Q price, Q market price), stored buy
// entries (item, d order, Q price, Q market price, Q count). A visitor gets
// one packet: 0xa1 (d owner, d package sale, Q adena, d inventory count,
// d n, n x sell entries) or 0xbe (d owner, Q adena, d inventory count, d n,
// n x buy entries). Titles are 0xa2 and 0xbf (d owner, S title), the
// package sale's ex 0x81.
// LU4 (the client's readers, rva 0x4c14e80, 0x4c14790, 0x4c14c60,
// 0x4c14560, 0x4c15210, 0x4c14b50) takes one packet each, with the trade
// record (PutItemRecord) for every item:
//   0xa0: d owner, d package sale, q adena, d n, n x (record, q market
//     price), d m, m x (record, q price, q market price)
//   0xbd: d owner, q adena, d n, n x (record, q market price, q market
//     price), d m, m x (record, q price, q market price, q count)
//   0xa1: d owner, d package sale, q adena, d n, n x (record, q price,
//     q market price)
//   0xbe: d owner, q adena, d n, n x (record, d order, q price, q market
//     price, q count)
//   0xa2, 0xbf: d owner, S title, d 0
struct StoreEntry {
    NativeItem item;
    std::int32_t order = 0;
    std::int64_t price = 0;
    std::int64_t marketPrice = 0;
    std::int64_t count = 0;
};

struct StoreListState {
    int expected = -1;
    std::vector<std::uint8_t> head;
    std::vector<StoreEntry> stored;
    std::vector<StoreEntry> inventory;
};

std::mutex g_storesLock;
std::map<std::pair<const CSocket*, int>, StoreListState> g_storeLists;
// The orders of the last buy store each client looked at, to answer its
// sales (0x9f) by order whatever object the client names.
std::map<const CSocket*, std::vector<StoreEntry>> g_buyOrders;

void ForgetStores(const CSocket* socket) {
    const std::lock_guard guard(g_storesLock);
    g_storeLists.erase({socket, 0xa0});
    g_storeLists.erase({socket, 0xbd});
    g_buyOrders.erase(socket);
}

bool ReadStoreEntry(const std::uint8_t* body, int length, int& position, bool buy, bool stored, StoreEntry& entry) {
    if (!ReadItem(body, length, position, entry.item)) {
        return false;
    }
    Reader reader(body, length, position);
    if (stored && buy) {
        entry.order = reader.Int32();
    }
    if (stored) {
        entry.price = reader.Int64();
    }
    entry.marketPrice = reader.Int64();
    if (stored && buy) {
        entry.count = reader.Int64();
    }
    position = reader.Position();
    return reader.Ok();
}

void BuildManageList(int opcode, StoreListState& state, std::vector<std::uint8_t>& out) {
    out = {static_cast<std::uint8_t>(opcode)};
    out.insert(out.end(), state.head.begin(), state.head.end());
    int key = 0;
    PutInt32(out, static_cast<std::uint32_t>(state.inventory.size()));
    for (const StoreEntry& entry : state.inventory) {
        PutItemRecord(out, entry.item, key++);
        PutTradeCount(out, entry.marketPrice);
        if (opcode == 0xbd) {
            PutTradeCount(out, entry.marketPrice);
        }
    }
    PutInt32(out, static_cast<std::uint32_t>(state.stored.size()));
    for (const StoreEntry& entry : state.stored) {
        PutItemRecord(out, entry.item, key++);
        PutTradeCount(out, entry.price);
        PutTradeCount(out, entry.marketPrice);
        if (opcode == 0xbd) {
            PutTradeCount(out, entry.count);
        }
    }
    state = {};
}

bool TranslateStoreManageList(const CSocket* socket, const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 2) {
        return false;
    }
    const int opcode = body[0];
    const bool buy = opcode == 0xbd;
    const std::lock_guard guard(g_storesLock);
    StoreListState& state = g_storeLists[{socket, opcode}];
    out.clear();
    if (body[1] == 1) {
        state = {};
        const int headLength = buy ? 12 : 16; // d owner, [d package sale], Q adena
        if (length < 2 + headLength + 4) {
            return false;
        }
        state.head.assign(body + 2, body + 2 + headLength);
        int position = 2 + headLength;
        const int count = static_cast<int>(runtime::LoadLittleEndian32(body + position));
        position += 4;
        for (int index = 0; index < count; ++index) {
            StoreEntry entry;
            if (!ReadStoreEntry(body, length, position, buy, true, entry)) {
                return false;
            }
            state.stored.push_back(entry);
        }
        if (position + 4 > length) {
            return false;
        }
        state.expected = static_cast<int>(runtime::LoadLittleEndian32(body + position));
        if (state.expected <= 0) {
            BuildManageList(opcode, state, out);
        }
        return true;
    }
    if (state.expected < 0 || length < 10) {
        return true; // an empty package after a list already sent
    }
    const int count = static_cast<int>(runtime::LoadLittleEndian32(body + 6));
    int position = 10;
    for (int index = 0; index < count; ++index) {
        StoreEntry entry;
        if (!ReadStoreEntry(body, length, position, buy, false, entry)) {
            return false;
        }
        state.inventory.push_back(entry);
    }
    if (static_cast<int>(state.inventory.size()) >= state.expected) {
        BuildManageList(opcode, state, out);
    }
    return true;
}

bool TranslateStoreList(const CSocket* socket, const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    const bool buy = body[0] == 0xbe;
    const int headLength = buy ? 12 : 16; // d owner, [d package sale], Q adena
    if (length < 1 + headLength + 8) {
        return false;
    }
    int position = 1 + headLength + 4; // the visitor's inventory count is not sent
    const int count = static_cast<int>(runtime::LoadLittleEndian32(body + position));
    position += 4;
    std::vector<StoreEntry> entries;
    for (int index = 0; index < count; ++index) {
        StoreEntry entry;
        if (!ReadStoreEntry(body, length, position, buy, true, entry)) {
            return false;
        }
        entries.push_back(entry);
    }
    out.assign(body, body + 1 + headLength);
    PutInt32(out, static_cast<std::uint32_t>(entries.size()));
    int key = 0;
    for (const StoreEntry& entry : entries) {
        PutItemRecord(out, entry.item, key++);
        if (buy) {
            PutInt32(out, static_cast<std::uint32_t>(entry.order));
        }
        PutTradeCount(out, entry.price);
        PutTradeCount(out, entry.marketPrice);
        if (buy) {
            PutTradeCount(out, entry.count);
        }
    }
    if (buy) {
        const std::lock_guard guard(g_storesLock);
        g_buyOrders[socket] = std::move(entries);
    }
    return true;
}

// 0xa2, 0xbf and ex 0x81 (the package sale's title, shown as a sale's).
bool TranslateStoreTitle(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    const int start = body[0] == kNativeExPacket ? 3 : 1;
    if (length < start + 4) {
        return false;
    }
    out = {static_cast<std::uint8_t>(start == 3 ? 0xa2 : body[0])};
    out.insert(out.end(), body + start, body + length);
    PutInt32(out, 0);
    return true;
}

// Client 0x9a, setting a buy store: d n, n x (d item, h enchant, h damage,
// q count, q price, h attack attribute, h power, h x6 defenses) -> native
// d n, n x (d item, h, h, Q, Q, d x2 variation, h x8 attributes, d shape
// shift, c x2 ensoul counts).
bool TranslateStoreBuySet(const std::uint8_t* packet, int length, std::vector<std::uint8_t>& out) {
    if (length < 5) {
        return false;
    }
    const int count = static_cast<int>(runtime::LoadLittleEndian32(packet + 1));
    if (count < 0 || 5 + count * 40 > length) {
        return false;
    }
    out.assign(packet, packet + 5);
    for (int index = 0; index < count; ++index) {
        const std::uint8_t* const entry = packet + 5 + index * 40;
        out.insert(out.end(), entry, entry + 24);
        PutZeros(out, 8);
        out.insert(out.end(), entry + 24, entry + 40);
        PutZeros(out, 6);
    }
    return true;
}

// Client 0x9f, selling to a buy store: d owner, d n, n x (d order, d item,
// h enchant, h damage, q count, q price) -> native the same entries plus
// d x2 variation, d shape shift, c x2 ensoul counts. An order the store did
// not list is looked up by item.
bool TranslateStoreSell(const CSocket* socket, const std::uint8_t* packet, int length, std::vector<std::uint8_t>& out) {
    if (length < 9) {
        return false;
    }
    const int count = static_cast<int>(runtime::LoadLittleEndian32(packet + 5));
    if (count < 0 || 9 + count * 28 > length) {
        return false;
    }
    std::vector<StoreEntry> orders;
    {
        const std::lock_guard guard(g_storesLock);
        const auto found = g_buyOrders.find(socket);
        if (found != g_buyOrders.end()) {
            orders = found->second;
        }
    }
    out.assign(packet, packet + 9);
    for (int index = 0; index < count; ++index) {
        const std::uint8_t* const entry = packet + 9 + index * 28;
        std::int32_t order = static_cast<std::int32_t>(runtime::LoadLittleEndian32(entry));
        const std::int32_t itemId = static_cast<std::int32_t>(runtime::LoadLittleEndian32(entry + 4));
        const bool listed = std::any_of(orders.begin(), orders.end(), [&](const StoreEntry& stored) {
            return stored.order == order;
        });
        if (!listed) {
            for (const StoreEntry& stored : orders) {
                if (stored.item.itemId == itemId) {
                    order = stored.order;
                    break;
                }
            }
        }
        PutInt32(out, static_cast<std::uint32_t>(order));
        out.insert(out.end(), entry + 4, entry + 28);
        PutZeros(out, 14);
    }
    return true;
}

// Merchants. Native ex 0xb8: d 0, Q adena, d list, d inventory count,
// h n, n x (item, Q price) for buying; d 1, d inventory count, h n, n x
// (item, Q price), h m, m x (item, d index, Q price) refunds, c refresh for
// selling; d 2, c reason when it fails. LU4 (the client's reader, rva
// 0x4c0b960) numbers it ex 0xb7 and has no inventory counts: d 0, q adena,
// d list, h n, n x (record, q price); d 1, h n, n x (record, q price), h m,
// m x (record, d index, q price), h refresh. The failure has no LU4 form.
bool TranslateBuySellList(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 7) {
        return false;
    }
    const std::uint32_t type = runtime::LoadLittleEndian32(body + 3);
    if (type > 1) {
        out.clear();
        return true;
    }
    out = {kNativeExPacket, 0xb7, 0x00};
    int position = 0;
    if (type == 0) {
        if (length < 25) {
            return false;
        }
        out.insert(out.end(), body + 3, body + 19); // d 0, Q adena, d list
        position = 23;
    } else {
        if (length < 13) {
            return false;
        }
        out.insert(out.end(), body + 3, body + 7);
        position = 11;
    }
    int key = 0;
    const auto putItems = [&](bool refunds) {
        if (position + 2 > length) {
            return false;
        }
        const int count = body[position] | body[position + 1] << 8;
        position += 2;
        PutInt16(out, count);
        for (int index = 0; index < count; ++index) {
            NativeItem item;
            if (!ReadItem(body, length, position, item)) {
                return false;
            }
            Reader reader(body, length, position);
            const std::int32_t refund = refunds ? reader.Int32() : 0;
            const std::int64_t price = reader.Int64();
            position = reader.Position();
            if (!reader.Ok()) {
                return false;
            }
            PutItemRecord(out, item, key++);
            if (refunds) {
                PutInt32(out, static_cast<std::uint32_t>(refund));
            }
            PutTradeCount(out, price);
        }
        return true;
    };
    if (!putItems(false)) {
        return false;
    }
    if (type == 1) {
        if (!putItems(true)) {
            return false;
        }
        PutInt16(out, position < length ? body[position] : 0);
    }
    return true;
}

// Multisell. Native c 0xd0, c last, d list, c show type, d page, d finished,
// d page size, d n, c type, d type set, n x (d entry, c stackable,
// h enchant, d x2 variation, h x8 attributes, ensoul, h products,
// h ingredients, products x (d item, Q, h type2, Q count, h enchant,
// d chance, d x2 variation, h x8 attributes, ensoul), ingredients x (d item,
// h type2, Q count, h enchant, d x2 variation, h x8 attributes, ensoul)),
// ensoul being c n, n x d, c m, m x d.
// LU4 (the client's reader, rva 0x4c11e70): c 0xd0, d list, d page,
// d finished, d page size, d n, n x (d entry, c stackable, h enchant,
// d variation, h x8 attributes, h products, products, h ingredients,
// ingredients), every item d item, q 0, h type2, q count, h enchant,
// d variation, h x8 attributes.
bool SkipEnsoul(const std::uint8_t* body, int length, int& position) {
    for (int list = 0; list < 2; ++list) {
        if (position >= length) {
            return false;
        }
        position += 1 + body[position] * 4;
    }
    return position <= length;
}

bool TranslateMultiSellItem(const std::uint8_t* body, int length, int& position, bool product,
                            std::vector<std::uint8_t>& out) {
    Reader reader(body, length, position);
    const std::int32_t itemId = reader.Int32();
    if (product) {
        reader.Int64();
    }
    const int type2Position = reader.Position();
    reader.Seek(type2Position + 2);
    const std::int64_t count = reader.Int64();
    const int enchantPosition = reader.Position();
    reader.Seek(enchantPosition + 2 + (product ? 4 : 0));
    const std::int32_t variation = reader.Int32();
    reader.Int32();
    const int attributes = reader.Position();
    reader.Seek(attributes + 16);
    position = reader.Position();
    if (!reader.Ok() || !SkipEnsoul(body, length, position)) {
        return false;
    }
    PutInt32(out, static_cast<std::uint32_t>(itemId));
    PutZeros(out, 8);
    out.insert(out.end(), body + type2Position, body + type2Position + 2);
    PutTradeCount(out, count);
    out.insert(out.end(), body + enchantPosition, body + enchantPosition + 2);
    PutInt32(out, static_cast<std::uint32_t>(variation));
    out.insert(out.end(), body + attributes, body + attributes + 16);
    return true;
}

bool TranslateMultiSellList(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length < 28) {
        return false;
    }
    const int count = static_cast<int>(runtime::LoadLittleEndian32(body + 19));
    out = {body[0]};
    out.insert(out.end(), body + 2, body + 6); // list
    out.insert(out.end(), body + 7, body + 23); // page, finished, page size, n
    int position = 28;
    for (int index = 0; index < count; ++index) {
        if (position + 31 > length) {
            return false;
        }
        const int head = position;
        out.insert(out.end(), body + head, body + head + 7); // entry, stackable, enchant
        out.insert(out.end(), body + head + 7, body + head + 11); // the first variation
        out.insert(out.end(), body + head + 15, body + head + 31); // attributes
        position = head + 31;
        if (!SkipEnsoul(body, length, position) || position + 4 > length) {
            return false;
        }
        const int products = body[position] | body[position + 1] << 8;
        const int ingredients = body[position + 2] | body[position + 3] << 8;
        position += 4;
        PutInt16(out, products);
        for (int product = 0; product < products; ++product) {
            if (!TranslateMultiSellItem(body, length, position, true, out)) {
                return false;
            }
        }
        PutInt16(out, ingredients);
        for (int ingredient = 0; ingredient < ingredients; ++ingredient) {
            if (!TranslateMultiSellItem(body, length, position, false, out)) {
                return false;
            }
        }
    }
    return true;
}

// Movement (D:\LU4\docs\LU4-MOVIMENTO.md and the Lucera port): the
// Interlude packets under other opcodes, except where noted.
// Native c 0x2f, d objectId, d destination xyz, d origin xyz -> LU4 the same
// opcode with origin first.
bool TranslateMoveToLocation(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length != 29) {
        return false;
    }
    out.assign(body, body + 5);
    out.insert(out.end(), body + 17, body + 29);
    out.insert(out.end(), body + 5, body + 17);
    return true;
}

// Native c 0x79, d objectId, d x, d y, d z, d heading, c -1 -> LU4 the same
// with a dword, 0, in place of the last byte (its meaning is unknown).
bool TranslateValidateLocation(const std::uint8_t* body, int length, std::vector<std::uint8_t>& out) {
    if (length != 22) {
        return false;
    }
    out.assign(body, body + 21);
    PutInt32(out, 0);
    return true;
}

// Packets LU4 takes as this server sends them: StopMove (c 0x47, d objectId,
// d x, y, z, heading) and MoveToPawn (c 0x72, d objectId, d target, d distance,
// d xyz, d target xyz).
bool PassThrough(const std::uint8_t* body, int length, int expected, std::vector<std::uint8_t>& out) {
    if (length != expected) {
        return false;
    }
    out.assign(body, body + length);
    return true;
}

DefaultIOBuffer* FilterOutbound(CSocket* socket, DefaultIOBuffer* buffer) {
    if (!runtime::client_protocol::HasLu4Clients()) {
        return buffer;
    }
    const CUserSocket* const user = socket->CastUserSocket();
    if (user == nullptr || !runtime::client_protocol::UsesLu4Protocol(user)) {
        return buffer;
    }
    const std::uint8_t* const raw = buffer->m_Buffer.data();
    const int size = raw[0] | raw[1] << 8;
    if (size < 3 || static_cast<std::uint32_t>(size) != buffer->m_dwSize) {
        L2_LOG_WARN(Log, "[lu4] Server buffer of %u bytes is not one packet (header %d); dropped.",
                    static_cast<unsigned>(buffer->m_dwSize), size);
        return nullptr;
    }
    const std::uint8_t* const body = raw + 2;
    const int length = size - 2;
    std::vector<std::uint8_t> out;
    bool translated = false;
    switch (body[0]) {
    case kNativeKeyPacket:
        translated = TranslateKeyPacket(body, length, out);
        break;
    case kNativeLoginResult:
        translated = TranslateLoginResult(body, length, out);
        break;
    case kNativeCharacterList:
        translated = TranslateCharacterList(body, length, out);
        break;
    case kNativeCharacterSelected:
        translated = TranslateCharacterSelected(body, length, out);
        break;
    case kNativeUserInfo:
        translated = TranslateUserInfo(socket, body, length, out);
        break;
    case kNativeStatusUpdate:
        translated = TranslateStatusUpdate(body, length, out);
        break;
    case kNativeMoveToLocation:
        translated = TranslateMoveToLocation(body, length, out);
        break;
    case kNativeNpcInfo:
        translated = TranslateNpcInfo(socket, body, length, out);
        break;
    case kNativeNpcState:
        translated = TranslateNpcState(socket, body, length, out);
        break;
    case kNativeDeleteObject:
        translated = TranslateDeleteObject(socket, body, length, out);
        break;
    case kNativeSpawnItem:
        translated = TranslateSpawnItem(body, length, out);
        break;
    case kNativeItemDropped:
        translated = TranslateDropItem(body, length, out);
        break;
    case kNativeItemPickedUp: // d picker, d objectId, d xyz (the builder's own copy has one d more)
        translated = length >= 21 && PassThrough(body, 21, 21, out);
        break;
    case kNativeConfirmDialog: // d message, d n, n x (d type, value), d time, d requester:
        // the client's reader (rva 0x4c0aba0) takes the native body. Message 0
        // (sent on every revive) asks no question; it is consumed.
        if (length >= 17 && runtime::LoadLittleEndian32(body + 1) == 0) {
            out.clear();
            translated = true;
        } else {
            translated = length >= 17 && PassThrough(body, length, length, out);
        }
        break;
    case kNativeValidateLocation:
        translated = TranslateValidateLocation(body, length, out);
        break;
    case kNativeStopMove:
        translated = PassThrough(body, length, 21, out);
        break;
    case kNativeMoveToPawn:
        translated = PassThrough(body, length, 37, out);
        break;
    case kNativeMyTargetSelected:
        translated = TranslateMyTargetSelected(body, length, out);
        break;
    case kNativeTargetSelected: // d objectId, d target, d x, y, z, d 0
        translated = PassThrough(body, length, 25, out);
        break;
    case kNativeTargetUnselected: // d objectId, d x, y, z, d reason
        translated = PassThrough(body, length, 21, out);
        break;
    case kNativeNpcHtml:
        translated = TranslateNpcHtml(body, length, out);
        break;
    case kNativeDie:
        translated = TranslateDie(body, length, out);
        break;
    case kNativeRevive: // d objectId
    case kNativeAutoAttackStart:
    case kNativeAutoAttackStop:
        translated = PassThrough(body, length, 5, out);
        break;
    case kNativeSocialAction:
        translated = TranslateSocialAction(body, length, out);
        break;
    case kNativeChangeMoveType: // d objectId, d running, d 0
        translated = PassThrough(body, length, 13, out);
        break;
    case kNativeChangeWaitType: // d objectId, d type, d x, y, z
        translated = PassThrough(body, length, 21, out);
        break;
    case kNativeAttack: // the LU4 body (Lucera Attack.writeLu4), 51 bytes for one hit
        translated = length >= 51 && PassThrough(body, length, length, out);
        break;
    case kNativeSay:
        translated = TranslateSay(body, length, out);
        break;
    case kNativeNpcSay:
        translated = TranslateNpcSay(body, length, out);
        break;
    case kNativeRelationChanged:
        translated = TranslateRelationChanged(body, length, out);
        break;
    case kNativeSystemMessage:
        translated = TranslateSystemMessage(body, length, out);
        break;
    case kNativeNetPingRequest: // d ping id; unanswered pings disconnect the client
        translated = PassThrough(body, length, 5, out);
        break;
    case kNativeItemList:
        translated = TranslateItemList(socket, body, length, out);
        break;
    case kNativeAbnormalStatus:
        translated = TranslateAbnormalStatus(body, length, out);
        break;
    case kNativeAskJoinParty: // S inviter, d routing
    case kNativeJoinParty: // d answer, d routing when accepted
    case kNativePartyDeleteAll: // no body
    case kNativePartyDelete: // d member, S name
    case kNativePartyMemberPosition: // d count, count x (d member, d x, y, z)
        translated = PassThrough(body, length, length, out);
        break;
    case kNativePartySpelled:
        translated = TranslatePartySpelled(body, length, out);
        break;
    // Trade: the request (d requester), both confirmations and the end
    // (d result) are this server's; the item packets are converted.
    case 0x70:
    case 0x53:
    case 0x82:
    case 0x1c:
        translated = PassThrough(body, length, length, out);
        break;
    case 0x14:
        translated = TranslateTradeStart(socket, body, length, out);
        break;
    case 0x1a:
    case 0x1b:
    case 0x81:
        translated = TranslateTradeItems(body, length, out);
        break;
    case 0xa0:
    case 0xbd:
        translated = TranslateStoreManageList(socket, body, length, out);
        break;
    case 0xa1:
    case 0xbe:
        translated = TranslateStoreList(socket, body, length, out);
        break;
    case 0xa2:
    case 0xbf:
        translated = TranslateStoreTitle(body, length, out);
        break;
    case 0xd0:
        translated = TranslateMultiSellList(body, length, out);
        break;
    case 0x22: // teleport: d objectId, d x, y, z, d 0, d heading, d 0 -> LU4 (Lucera
        // TeleportToLocation, official capture) without the last dword. The server
        // waits for the client's C_APPEARING after it, so dropping it froze the
        // character (//hide off, gatekeepers, returning to a village).
        if (length >= 25) {
            out.assign(body, body + 25);
            translated = true;
        }
        break;
    case 0x71: // restart answer, d 1 accepted (the LU4 one, 5 bytes, has no "bye" text)
    case 0x84: // logged out, no body
        translated = PassThrough(body, length, length, out);
        break;
    case kNativePartyAll:
        translated = TranslatePartyAll(socket, body, length, out);
        break;
    case kNativePartyAdd:
        translated = TranslatePartyAdd(socket, body, length, out);
        break;
    case kNativePartyUpdate:
        translated = TranslatePartyUpdate(socket, body, length, out);
        break;
    case kNativeNewCharacterSuccess:
        translated = TranslateNewCharacter(body, length, out);
        break;
    case kNativeCreateSuccess:
    case kNativeCreateFail:
        translated = TranslateCreateResult(body, length, out);
        break;
    case kNativeAcquireSkillList:
        translated = TranslateAcquireSkillList(body, length, out);
        break;
    case kNativeCharInfo:
        translated = TranslateCharInfo(body, length, out);
        break;
    case kNativeInventoryUpdate:
        translated = TranslateInventoryUpdate(socket, body, length, out);
        break;
    case kNativeSkillList:
        translated = TranslateSkillList(body, length, out);
        break;
    case kNativeMagicSkillUse:
        translated = TranslateMagicSkillUse(body, length, out);
        break;
    case kNativeMagicSkillLaunched:
        translated = TranslateMagicSkillLaunched(body, length, out);
        break;
    case kNativeShortcutInit:
        translated = TranslateShortcutInit(body, length, out);
        break;
    case kNativeShortcutRegisterReply:
        translated = TranslateShortcutRegister(body, length, out);
        break;
    case kNativeSetupGauge: // d objectId, d colour, d time, d total: the LU4 body
        translated = PassThrough(body, length, 17, out);
        break;
    case kNativeSkillCoolTime: // d count, count x (d skill, d level, d reuse, d remaining)
    case kNativeActionFail: // d 0; the official server sends it as is
        translated = length >= 5 && PassThrough(body, length, length, out);
        break;
    case kNativeExPacket:
        if (length >= 3) {
            const int extended = body[1] | body[2] << 8;
            if (extended == kNativeExAcquireSkillInfo) {
                translated = TranslateAcquireSkillInfo(body, length, out);
            } else if (extended == kNativeExEquipSlots) {
                translated = TranslateEquipSlots(socket, body, length, out);
            } else if (extended == kNativeExBasicActionList) { // LU4 numbers it 0x5f; same body
                translated = PassThrough(body, length, length, out);
                out[1] = 0x5f;
            } else if (extended == 0x2f) { // storage limits, d x12 (inventory, warehouse,
                // ..., private sell and buy slots): the client's reader (rva 0x4c0f450)
                // takes the same body; without it every store slot is over the limit
                translated = PassThrough(body, length, length, out);
            } else if (extended == 0xb8) {
                translated = TranslateBuySellList(body, length, out);
            } else if (extended == 0x13e) {
                translated = TranslateAdenaCount(socket, body, length, out);
            } else if (extended == 0x0c) { // auto shot: d item, d state, d slot; the
                // client's reader (rva 0x4c0b830) takes the same body
                translated = length >= 11 && PassThrough(body, length, length, out);
            } else if (extended == 0x81) { // the package sale's title
                translated = TranslateStoreTitle(body, length, out);
            }
        }
        break;
    default:
        break;
    }
    const int extended = body[0] == kNativeExPacket && length >= 3 ? body[1] | body[2] << 8 : -1;
    if (translated && out.empty()) { // consumed: the packet is part of a larger LU4 one
        return nullptr;
    }
    if (translated) {
        if (FirstTime(0x20000000 | body[0] << 16 | (extended & 0xffff))) {
            Dump("S2C-native", body, length);
            Dump("S2C-lu4", out.data(), static_cast<int>(out.size()));
        }
        return MakeBuffer(out);
    }
    if (FirstTime(0x10000000 | body[0] << 16 | (extended & 0xffff))) {
        L2_LOG_INFO(Log, "[lu4] No LU4 form for server packet 0x%02x%s%s (%d bytes); dropped. Head: %s", body[0],
                    extended >= 0 ? " ex 0x" : "", extended >= 0 ? Hex(body + 1, 2).c_str() : "", length,
                    Hex(body, std::min(length, 48)).c_str());
        Dump("S2C", body, length);
    }
    return nullptr;
}

} // namespace

bool IsVersionPacket(const std::uint8_t* packet, int length) {
    return length >= 13 && packet[0] == kClientVersion &&
        runtime::LoadLittleEndian32(packet + 1) == static_cast<std::uint32_t>(runtime::client_protocol::kLu4Revision);
}

bool TranslateIncoming(CUserSocket* socket, const std::uint8_t* packet, int length, std::vector<std::uint8_t>& out) {
    out.clear();
    bool translated = false;
    switch (packet[0]) {
    case kClientVersion:
        if (IsVersionPacket(packet, length)) {
            runtime::client_protocol::RecordNegotiatedRevision(socket, runtime::client_protocol::kLu4Revision);
            {
                const std::lock_guard guard(g_usersLock);
                g_users.erase(socket);
            }
            ForgetNpcs(socket);
            ForgetInventory(socket);
            ForgetParty(socket);
            ForgetTrade(socket);
            ForgetStores(socket);
            translated = TranslateVersion(out);
        }
        break;
    case kClientAuthLogin:
        translated = TranslateAuthLogin(packet, length, out);
        break;
    case kClientCharacterSelect: // d slot, then fields this server does not read
        if (length >= 5) {
            out.push_back(kNativeCharacterSelect);
            out.insert(out.end(), packet + 1, packet + 5);
            translated = true;
        }
        break;
    case kClientEnterWorld:
        out.push_back(kNativeEnterWorld);
        translated = true;
        break;
    case kClientMove: // c type (0 keyboard, 1 mouse), d origin xyz, d target xyz
        if (length >= 26) {
            out.push_back(kNativeMove);
            out.insert(out.end(), packet + 14, packet + 26);
            out.insert(out.end(), packet + 2, packet + 14);
            PutInt32(out, packet[1]);
            translated = true;
        }
        break;
    case kClientNetPing: // d ping id, d milliseconds, d: the native body (official capture)
        if (length >= 13) {
            out.push_back(kNativeNetPing);
            out.insert(out.end(), packet + 1, packet + 13);
            translated = true;
        }
        break;
    case kClientSay: // S text, d type, S target (whisper): the native body
        // The LU4 client keeps "//" to itself; "..command" is a builder command
        // (native c 0x74, S command without the prefix), as on the Lucera port.
        if (length >= 5 && packet[1] == '.' && packet[2] == 0 && packet[3] == '.' && packet[4] == 0) {
            out.push_back(kNativeBuilderCommand);
            Reader reader(packet, length, 5);
            const std::vector<std::uint8_t> command = reader.Text();
            out.insert(out.end(), command.begin(), command.end());
        } else {
            out.push_back(kNativeSayRequest);
            out.insert(out.end(), packet + 1, packet + length);
        }
        translated = true;
        break;
    case kClientNewCharacter:
        out.push_back(kNativeNewCharacter);
        translated = true;
        break;
    case kClientCharacterCreate: { // S name, c race, c sex, d class, d hair style, color, face
        Reader reader(packet, length, 1);
        const std::vector<std::uint8_t> name = reader.Text();
        const int at = reader.Position();
        if (reader.Ok() && at + 18 <= length) {
            // native S name, d race, d sex, d class, d INT STR CON MEN DEX WIT (the
            // server uses the class's recommended values), d hair style, color, face
            out.push_back(kNativeCharacterCreate);
            out.insert(out.end(), name.begin(), name.end());
            PutInt32(out, packet[at]);
            PutInt32(out, packet[at + 1]);
            out.insert(out.end(), packet + at + 2, packet + at + 6);
            PutZeros(out, 24);
            out.insert(out.end(), packet + at + 6, packet + at + 18);
            translated = true;
        }
        break;
    }
    case kClientCharacterList: // in the lobby: the native "back to the lobby" (c 0xd0, h 0x33) reloads it
        out = {kNativeClientEx, 0x33, 0x00};
        translated = true;
        break;
    case kClientAcquireSkillInfo: // d skill, d level, d type: the native packets
    case kClientAcquireSkill:
        out.assign(packet, packet + length);
        translated = true;
        break;
    case 0x42: // party invitation: S name, d routing
    case 0x43: // answer to an invitation: d answer
    case 0x44: // leave the party
    case 0x45: // dismiss a member: S name
    case 0x46: // dissolve the party
    case 0x55: // answer to a trade request: d answer
    case 0x1a: // trade request: d target
    case 0x1b: // add a trade item: d trade, d objectId, Q count
    case 0x1c: // confirm the trade: d
        // The client's senders (ClientParty*, ClientPlayerExchange*,
        // PlayerExchangeAdd) write these opcodes with the native bodies.
        out.assign(packet, packet + length);
        translated = true;
        break;
    case 0x40: // buy from a merchant: d list, d n, n x (d item, Q count)
    case 0x37: // sell to a merchant: d list, d n, n x (d objectId, d item, Q count)
    case 0x31: // set a sell store: d package sale, d n, n x (d objectId, Q count, Q price)
    case 0x32: // leave the sell store setup
    case 0x83: // buy from a store: d owner, d n, n x (d objectId, Q count, Q price)
    case 0x96: // close the sell store
    case 0x97: // sell store title: S
    case 0x9b: // leave the buy store setup
    case 0x9c: // close the buy store
    case 0x9d: // buy store title: S
        // The client's senders (ClientPlayerShop*) write the native bodies.
        out.assign(packet, packet + length);
        translated = true;
        break;
    case 0xc6: // confirm dialog answer: d message, d answer, d requester
        // (ClientConfirmDialogAnswer writes the native body)
        out.assign(packet, packet + length);
        translated = true;
        break;
    case kNativeClientEx:
        if (length >= 11 && packet[1] == 0x0d && packet[2] == 0) {
            // auto shot: h 0x0d, d item, d on (official capture). The native body
            // adds d slot; -1 lets the server pick it from the shot kind.
            out.assign(packet, packet + 11);
            PutInt32(out, 0xffffffffU);
            translated = true;
        }
        break;
    case 0x9a:
        translated = TranslateStoreBuySet(packet, length, out);
        break;
    case 0x9f:
        translated = TranslateStoreSell(socket, packet, length, out);
        break;
    case 0xb0: // multisell: d list, d entry, Q count, h enchant, d x2 variation,
        // h x8 attributes; the native packet ends with two ensoul counts
        out.assign(packet, packet + length);
        out.resize(out.size() + 2, 0);
        translated = true;
        break;
    case 0x30: // appeared after a teleport (LU4 sends 0x30 or 0x3a) -> native C_APPEARING 0x3a
    case 0x3a:
        out.push_back(0x3a);
        translated = true;
        break;
    case 0x02: // logout -> native C_LOGOUT 0x00
        out.push_back(0x00);
        translated = true;
        break;
    case 0x57: // restart to the character list: the native packet
        out.push_back(0x57);
        translated = true;
        break;
    case kClientItemList:
        out.push_back(kNativeItemListRequest);
        {
            const std::lock_guard guard(g_inventoriesLock);
            g_inventories[socket].openWindow = true;
        }
        translated = true;
        break;
    case kClientUseItem: // d objectId, c force -> d objectId, d force
        if (length >= 6) {
            out.push_back(kNativeUseItem);
            out.insert(out.end(), packet + 1, packet + 5);
            PutInt32(out, packet[5]);
            translated = true;
        }
        break;
    case kClientDropItem: // d objectId, Q count, d x, y, z: the native body
        if (length >= 25) {
            out.push_back(kNativeDropItem);
            out.insert(out.end(), packet + 1, packet + 25);
            translated = true;
        }
        break;
    case kClientMagicSkillUse: // d skill, d control, c shift: the native packet
        out.assign(packet, packet + length);
        translated = true;
        break;
    case kClientActionUse: // d action, c control, c shift -> d action, d control, c shift
        if (length >= 7) {
            out.push_back(kClientActionUse);
            out.insert(out.end(), packet + 1, packet + 5);
            PutInt32(out, packet[5]);
            out.push_back(packet[6]);
            translated = true;
        }
        break;
    case kClientShortcutRegister: // h type, h slot, d id -> d type, slot, id, level, category + 1, d, d
        if (length >= 9) {
            out.push_back(kNativeShortcutRegister);
            PutInt32(out, static_cast<std::uint32_t>(packet[1] | packet[2] << 8));
            PutInt32(out, static_cast<std::uint32_t>(packet[3] | packet[4] << 8));
            out.insert(out.end(), packet + 5, packet + 9);
            PutInt32(out, 1); // the server sets the learned level
            PutInt32(out, 1);
            translated = true;
        }
        break;
    case kClientShortcutDelete: // h slot -> d slot
        if (length >= 3) {
            out.push_back(kNativeShortcutDelete);
            PutInt32(out, static_cast<std::uint32_t>(packet[1] | packet[2] << 8));
            translated = true;
        }
        break;
    case kClientAttack: // d objectId, d x, y, z, c shift: the native body
        if (length >= 18) {
            out.push_back(kNativeAttackRequest);
            out.insert(out.end(), packet + 1, packet + 18);
            translated = true;
        }
        break;
    case kClientRestartPoint: // d point; the native packet adds d item, 0 here
        out.assign(packet, packet + length);
        translated = true;
        break;
    case kClientAction: // d objectId, d x, y, z, c shift: the same body
        if (length >= 18) {
            out.push_back(kNativeAction);
            out.insert(out.end(), packet + 1, packet + 18);
            translated = true;
        }
        break;
    case kClientTargetCancel: // c (native h)
        out = {kNativeTargetCancel, length >= 2 ? packet[1] : std::uint8_t{0}, 0};
        translated = true;
        break;
    case kClientLinkHtml: // S: the same packet
    case kClientBypass:
        out.assign(packet, packet + length);
        translated = true;
        break;
    case kClientValidatePosition: // d x, y, z, heading, vehicle; native adds c type
        if (length >= 21) {
            out.push_back(kNativeValidatePosition);
            out.insert(out.end(), packet + 1, packet + 21);
            out.push_back(0);
            translated = true;
        }
        break;
    default:
        break;
    }
    if (!translated) {
        if (FirstTime(packet[0])) {
            L2_LOG_INFO(Log, "[lu4] No translation for client packet 0x%02x (%d bytes); ignored. Head: %s", packet[0],
                        length, Hex(packet, std::min(length, 48)).c_str());
            Dump("C2S", packet, length);
        }
        return false;
    }
    if (FirstTime(0x40000000 | packet[0])) {
        Dump("C2S-lu4", packet, length);
        Dump("C2S-native", out.data(), static_cast<int>(out.size()));
    }
    out.resize(out.size() + kInputPadding, 0);
    return true;
}

void Install() { CSocket::s_outboundHook = &FilterOutbound; }

} // namespace lu4
