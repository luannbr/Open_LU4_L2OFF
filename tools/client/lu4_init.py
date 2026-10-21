"""Builds the Init packet AuthGateD sends to the LU4 lu4.bin client
(kInitHex in AuthGateD/src/Lu4Login.cpp).

usage: python lu4_init.py

The official Init carries a 21-byte message; the lu4.bin build of
18-10-2026 reads the session Blowfish key at bytes 21..36 of a 37-byte
message instead, so with the official Init it installs garbage and the login
decrypts to opcode 255. This rebuilds the Init with the 37-byte message:

  [0] opcode 00, [1..16] prefix (the official message's first 16 bytes),
  [17..20] session id (echoed in login bytes 1..4), [21..36] session key.

The key is BC AF 64 50 71 3E 25 CF E1 72 C2 F4 BE BE 39 B2, the one whose
expanded table is AuthGateD/etc/lu4_auth_key.bin, so AuthGateD keeps
decrypting with that table. The 64-byte signature is the server-list marker
(LU4-MAGIC-SERVERLIST-MAC-BYPASS!, accepted by lu4loader) followed by the S
half of the official signature, which keeps S below the Ed25519 group order.

Wire codec (validated against the official Init, which it decodes and
re-encodes): u16 message length, message, signature, zero padding to 8
bytes; every 32-bit word after the first is XORed with a running sum seeded
with 0xFDC58595; then the final sum and 00 00 08 74; all encrypted with
standard Blowfish (initial key 6A ED 41 F2 39 BC 07 5D AF 14 8E 63 D1 90 B5
4C, little-endian block halves); framed with a u16 length.
"""
import decimal
import struct

MASK = 0xFFFFFFFF

OFFICIAL_INIT = bytes.fromhex(
    "620054CC4120F6D045B474513B970B62886B19956519808089F5F959E7C2AFFF"
    "85EC8B9186D26BEE5CA6A5D716293CD8926D0A43FDAAFE3FEC25A5D6935A807E"
    "17E4199841124094277BFC9C7E1B589AE77E552FE115FAC5919FD8402BABAFD7"
    "12AC"
)
INITIAL_KEY = bytes.fromhex("6AED41F239BC075DAF148E63D190B54C")
SESSION_KEY = bytes.fromhex("BCAF6450713E25CFE172C2F4BEBE39B2")
MARKER = b"LU4-MAGIC-SERVERLIST-MAC-BYPASS!"
XOR_SEED = 0xFDC58595
TRAILER = bytes.fromhex("00000874")


def pi_words(count):
    """The first count 32-bit words of pi's fractional hex digits (Blowfish's P and S)."""
    digits = count * 8
    decimal.getcontext().prec = int(digits * 1.21) + 50
    D = decimal.Decimal
    # Chudnovsky series
    terms = digits // 11 + 2
    total, k_term = D(0), D(1)
    for k in range(terms):
        if k:
            k_term *= D(-(6 * k - 5) * (2 * k - 1) * (6 * k - 1)) / D(k * k * k * 26680 * 640320 * 640320)
        total += k_term * (13591409 + 545140134 * k)
    pi = D(426880) * D(10005).sqrt() / total
    value = int((pi - 3) * (D(16) ** digits))
    text = "%0*x" % (digits, value)
    return [int(text[i:i + 8], 16) for i in range(0, digits, 8)]


PI = pi_words(18 + 4 * 256)
assert PI[0] == 0x243F6A88 and PI[17] == 0x8979FB1B and PI[18] == 0xD1310BA6, "pi digits"
PI_P = PI[:18]
PI_S = [PI[18 + box * 256:18 + (box + 1) * 256] for box in range(4)]


def feistel(x, S):
    return ((((S[0][x >> 24 & 0xFF] + S[1][x >> 16 & 0xFF]) & MASK) ^ S[2][x >> 8 & 0xFF]) + S[3][x & 0xFF]) & MASK


def encrypt_block(L, R, P, S):
    for i in range(16):
        L ^= P[i]
        R ^= feistel(L, S)
        L, R = R, L
    L, R = R, L
    return (L ^ P[17]) & MASK, (R ^ P[16]) & MASK


def decrypt_block(L, R, P, S):
    for i in range(17, 1, -1):
        L ^= P[i]
        R ^= feistel(L, S)
        L, R = R, L
    L, R = R, L
    return (L ^ P[0]) & MASK, (R ^ P[1]) & MASK


def key_schedule(key):
    P, S = list(PI_P), [list(box) for box in PI_S]
    position = 0
    for i in range(18):
        word = 0
        for _ in range(4):
            word = (word << 8 | key[position % len(key)]) & MASK
            position += 1
        P[i] ^= word
    L = R = 0
    for i in range(0, 18, 2):
        L, R = encrypt_block(L, R, P, S)
        P[i], P[i + 1] = L, R
    for box in S:
        for i in range(0, 256, 2):
            L, R = encrypt_block(L, R, P, S)
            box[i], box[i + 1] = L, R
    return P, S


P0, S0 = key_schedule(INITIAL_KEY)


def blocks(data, function):
    out = bytearray()
    for offset in range(0, len(data), 8):
        L, R = struct.unpack_from("<II", data, offset)
        out += struct.pack("<II", *function(L, R, P0, S0))
    return bytes(out)


def encode_init(message, signature):
    block = struct.pack("<H", len(message)) + message + signature
    block += b"\0" * (-len(block) % 8)
    words = list(struct.unpack("<%dI" % (len(block) // 4), block))
    running = XOR_SEED
    for i in range(1, len(words)):
        running = (running + words[i]) & MASK
        words[i] ^= running
    body = blocks(struct.pack("<%dI" % len(words), *words) + struct.pack("<I", running) + TRAILER, encrypt_block)
    return struct.pack("<H", len(body) + 2) + body


def decode_init(frame):
    raw = blocks(frame[2:], decrypt_block)
    words = list(struct.unpack("<%dI" % (len(raw) // 4), raw))
    running = words[-2]
    for i in range(len(words) - 3, 0, -1):
        words[i] ^= running
        running = (running - words[i]) & MASK
    decoded = struct.pack("<%dI" % len(words), *words)
    length = int.from_bytes(decoded[:2], "little")
    return decoded[2:2 + length], decoded[2 + length:2 + length + 64], running


official_message, official_signature, seed = decode_init(OFFICIAL_INIT)
assert official_message.hex() == "00d76f0700bcaf6450713e25cfe172c2f4bebe39b2", official_message.hex()
assert seed == XOR_SEED and encode_init(official_message, official_signature) == OFFICIAL_INIT

message = official_message + SESSION_KEY  # prefix, session id, key
assert len(message) == 37
init = encode_init(message, MARKER + official_signature[32:])
assert decode_init(init)[0] == message

text = init.hex().upper()
print("constexpr std::string_view kInitHex =")
lines = [text[i:i + 64] for i in range(0, len(text), 64)]
for index, line in enumerate(lines):
    print('    "%s"%s' % (line, ";" if index == len(lines) - 1 else ""))
