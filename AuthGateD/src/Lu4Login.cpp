#include "Lu4Login.h"

#include "Shared/src/Runtime/LittleEndian.h"

#include <array>
#include <fstream>
#include <iterator>
#include <string_view>

namespace lu4 {
namespace {

std::vector<unsigned char> FromHex(std::string_view hex) {
    auto nibble = [](char c) {
        return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
    };
    std::vector<unsigned char> bytes(hex.size() / 2);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<unsigned char>(nibble(hex[index * 2]) << 4 | nibble(hex[index * 2 + 1]));
    }
    return bytes;
}

// Init montado para o DEBUG build (lu4.bin). O debug le a chave Blowfish de
// sessao na mensagem interna nos offsets 21..36 (mensagem de 37 bytes), nao nos
// 7..22 do Init oficial de 21 bytes do shipping -- por isso o Init oficial fazia
// o debug instalar lixo (opcode 255). Aqui a chave em 21..36 = bcaf6450..., que
// e exatamente a matriz de lu4_auth_key.bin, entao a decifragem com a matriz fixa
// funciona. Assinatura: marcador MAGIC na metade R (aceito pelo loader_cave) +
// metade S preservada do Init oficial (mantem S<L no range-check do Ed25519).
// Layout da mensagem (relativo ao opcode): [0]op=00 [1..16]prefix [17..20]sid
// (ecoa em login[1..4]) [21..36]chave. Gerado por scratchpad/gen_init.py.
// (Prova causal: relatorio da outra IA 2026-10-21, lu4_controlled_init_verified.json.)
constexpr std::string_view kInitHex =
    "7200CF8F0419EFA1D4E274513B970B62886B0676E99EAD83A9F9A704AF206959"
    "6F3C6E457843B96E4546873849FCB86CF6CDCF2FB7164C55354B9A313DAAC822"
    "DD554110A9D0B43B4A51A49800E842197CA08A28978B3C605BBA1D4C14203324"
    "698FC57C8F2A1395480CBB9BC88D54964F28";

// The 34 bytes the client reads after the MAC, one per reply kind, from the
// same capture. The client needs them present; the MAC marker is what it checks.
constexpr std::string_view kLoginOkBlock = "a1f475e1609897b3a84047393e6443cbf94f3a5d15de5d75d4e14e9f4f15e3020000";
constexpr std::string_view kPlayOkBlock = "6e3916863fc09d4f59bdf7d4734fdc9f67d87f7077920d6bbfed7285c166ac030000";
constexpr std::string_view kServerListBlock = "3fd64b9d41271e53f6bcaa61c58c3b17d5af2894c03fdb347ba1fa1da6cb80071d95";

// Accepted by lu4authhook.dll in place of the MAC.
constexpr std::string_view kMacMarker = "LU4-MAGIC-SERVERLIST-MAC-BYPASS!";

constexpr unsigned char kLoginOk = 0x03;
constexpr unsigned char kServerList = 0x04;
constexpr unsigned char kPlayOk = 0x07;

Blowfish_matrix g_keyTable{};

void Frame(const std::vector<unsigned char>& data, std::string_view block, std::vector<unsigned char>& out) {
    const std::vector<unsigned char> blockBytes = FromHex(block);
    out.clear();
    out.push_back(static_cast<unsigned char>(data.size()));
    out.push_back(static_cast<unsigned char>(data.size() >> 8U));
    out.insert(out.end(), data.begin(), data.end());
    out.insert(out.end(), kMacMarker.begin(), kMacMarker.end());
    out.insert(out.end(), blockBytes.begin(), blockBytes.end());
}

void PutLittleEndian32(std::vector<unsigned char>& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<unsigned char>(value >> shift));
    }
}

} // namespace

bool LoadKeyTable(const std::filesystem::path& file, std::string& error) {
    std::ifstream stream(file, std::ios::binary);
    const std::vector<unsigned char> raw{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    constexpr std::size_t kTableBytes = (18 + 4 * 256) * 4;
    if (raw.size() != kTableBytes) {
        error = "LU4 key table " + file.string() + " has " + std::to_string(raw.size()) + " bytes, expected " +
            std::to_string(kTableBytes);
        return false;
    }
    const unsigned char* cursor = raw.data();
    for (std::uint32_t& entry : g_keyTable.P) {
        entry = runtime::LoadLittleEndian32(cursor);
        cursor += 4;
    }
    for (auto& box : g_keyTable.S) {
        for (std::uint32_t& entry : box) {
            entry = runtime::LoadLittleEndian32(cursor);
            cursor += 4;
        }
    }
    return true;
}

const Blowfish_matrix& KeyTable() { return g_keyTable; }

const std::vector<unsigned char>& InitPacket() {
    static const std::vector<unsigned char> init = FromHex(kInitHex);
    return init;
}

int MapClientOpcode(unsigned char opcode) {
    switch (opcode) {
    case 0x00:
        return 0x00; // login
    case 0x01:
        return 0x01; // server list
    case 0x03:
        return 0x02; // server select
    default:
        return -1;
    }
}

bool ConvertReply(const unsigned char* packet, int length, std::vector<unsigned char>& out) {
    if (length < 1) {
        return false;
    }
    std::vector<unsigned char> data;
    switch (packet[0]) {
    case kLoginOk: // c d uid d oneTimeKey ...
        if (length < 9) {
            return false;
        }
        data = {0x01, 0x00};
        PutLittleEndian32(data, runtime::LoadLittleEndian32(packet + 1));
        PutLittleEndian32(data, runtime::LoadLittleEndian32(packet + 5));
        Frame(data, kLoginOkBlock, out);
        return true;
    case kPlayOk: // c d playKey d uid c server; the LU4 PlayOk carries uid first
        if (length < 9) {
            return false;
        }
        data = {0x04, 0x00};
        PutLittleEndian32(data, runtime::LoadLittleEndian32(packet + 5));
        PutLittleEndian32(data, runtime::LoadLittleEndian32(packet + 1));
        Frame(data, kPlayOkBlock, out);
        return true;
    case kServerList: // AuthD built the LU4 list body (lu4Mode there)
        data.assign(packet + 1, packet + length);
        Frame(data, kServerListBlock, out);
        return true;
    default:
        return false;
    }
}

} // namespace lu4
