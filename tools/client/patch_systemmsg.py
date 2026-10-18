"""Fixes the login failure texts of the European client's SystemMsg-eu.dat.

usage: python patch_systemmsg.py <client system folder>

The client shows a login failure as two system messages. In SystemMsg-eu.dat
of this release, the second message of "wrong account or password" (450) and
"wrong password" (452) is the 303-character "account restricted for violating
the EULA" text, which overruns the client's message buffer: the client closes.
SystemMsg_Classic-eu.dat has the right text there ("Please check your account
information and try logging in again."); it is copied over.

The original is kept as SystemMsg-eu.dat.orig (not overwritten when it exists).
The file is re-encrypted as Lineage2Ver413 with the l2encdec key the client uses
(public exponent from acmi/L2crypt; the client decrypts with exponent 0x1d).
"""
import os
import shutil
import struct
import sys
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "gfport"))
from client_products import MODULUS, decode

ENCRYPT_EXPONENT = int(
    "30b4c2d798d47086145c75063c8e841e719776e400291d7838d3e6c4405b504c6a07f8fca27f32b86643d2649d1d5f12"
    "4cdd0bf272f0909dd7352fe10a77b34d831043d9ae541f8263c6fe3d1c14c2f04e43a7253a6dda9a8c1562cbd493c1b6"
    "31a1957618ad5dfe5ca28553f746e2fc6f2db816c7db223ec91e955081c1de65", 16)
HEADER = "Lineage2Ver413".encode("utf-16le")
FOOTER = bytes(19) + b"\x64"
MESSAGES = (450, 452)
BAD_PREFIX = b"Your account has been restricted for violating the EULA"

def compact(value):
    if value < 0x40:
        return bytes([value])
    out = [0x40 | (value & 0x3F)]
    value >>= 6
    while value:
        out.append((value & 0x7F) | (0x80 if value >> 7 else 0))
        value >>= 7
    return bytes(out)

def find_text(raw, message_id):
    """Offset and length of the compact-length text of a SystemMsg record."""
    key = struct.pack("<I", message_id)
    start = raw.find(key)
    while start != -1:
        position = start + 8
        first = raw[position]
        length, size = first & 0x3F, 1
        if first & 0x40:
            length |= raw[position + 1] << 6
            size = 2
        text = raw[position + size:position + size + length]
        if not first & 0x80 and 3 <= length < 4096 and text.endswith(b"\0") and \
                all(32 <= c < 127 or c in (9, 10) for c in text[:-1]):
            return position, size + length, text[:-1]
        start = raw.find(key, start + 1)
    sys.exit(f"SystemMsg {message_id} not found")

def encode(raw):
    packed = struct.pack("<I", len(raw)) + zlib.compress(raw, 9)
    blocks = bytearray()
    for offset in range(0, len(packed), 124):
        chunk = packed[offset:offset + 124]
        size = len(chunk)
        block = bytearray(128)
        block[3] = size
        start = 128 - size - (124 - size) % 4
        block[start:start + size] = chunk
        blocks += pow(int.from_bytes(block, "big"), ENCRYPT_EXPONENT, MODULUS).to_bytes(128, "big")
    return HEADER + bytes(blocks) + FOOTER

folder = sys.argv[1]
target = os.path.join(folder, "SystemMsg-eu.dat")
original = target + ".orig"
if not os.path.exists(original):
    shutil.copy2(target, original)
raw = bytearray(decode(original))
classic = decode(os.path.join(folder, "SystemMsg_Classic-eu.dat"))

for message_id in MESSAGES:
    position, size, text = find_text(raw, message_id)
    if not text.startswith(BAD_PREFIX):
        print(f"SystemMsg {message_id} already reads: {text[:60].decode()}")
        continue
    replacement = find_text(classic, message_id)[2]
    raw[position:position + size] = compact(len(replacement) + 1) + replacement + b"\0"
    print(f"SystemMsg {message_id}: {replacement.decode()}")

data = encode(bytes(raw))
open(target + ".new", "wb").write(data)
if decode(target + ".new") != bytes(raw):
    sys.exit("re-encoded file does not decode back; original left in place")
os.replace(target + ".new", target)
print(f"{target} written ({len(data)} bytes); original kept as {os.path.basename(original)}")
