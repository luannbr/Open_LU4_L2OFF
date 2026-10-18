"""Lists the premium shop products the client knows (ProductName-*.dat).

usage: python client_products.py <client ProductName-eu.dat> <output client_products.txt>

The client shows a shop product only when its ID is in this file; the name and
icon come from the items the server sends, the description from the file. The
output has one line per product: ID, tab, description (empty for many).
convert_productdata.py gives the GF products IDs whose description is empty.

The file is "Lineage2Ver413": 128-byte RSA blocks under the public l2encdec key
(exponent 0x1d), then zlib. Each record: ID, item ID, description, two numbers,
second text, all little-endian with Unreal compact-index string lengths.
"""
import struct
import sys
import zlib

MODULUS = int(
    "75b4d6de5c016544068a1acf125869f43d2e09fc55b8b1e289556daf9b8757635593446288b3653da1ce91c87bb1a5c18f16323495c55d7d"
    "72c0890a83f69bfd1fd9434eb1c02f3e4679edfa43309319070129c267c85604d87bb65bae205de3707af1d2108881abb567c3b3d069ae67c3"
    "a4c6a3aa93d26413d4c66094ae2039", 16)
EXPONENT = 0x1D

def decode(path):
    data = open(path, "rb").read()
    if data[:28].decode("utf-16le", "replace") != "Lineage2Ver413":
        sys.exit(f"{path}: not a Lineage2Ver413 file")
    packed = bytearray()
    for offset in range(28, len(data) - 127, 128):
        block = pow(int.from_bytes(data[offset:offset + 128], "big"), EXPONENT, MODULUS).to_bytes(128, "big")
        size = int.from_bytes(block[:4], "big")
        if size > 124:
            break  # trailer
        pad = (124 - size) % 4
        packed += block[128 - size - pad:128 - pad]
    return zlib.decompress(bytes(packed[4:]))

def records(raw):
    position = 4
    def compact():
        nonlocal position
        byte = raw[position]; position += 1
        value, negative = byte & 0x3F, byte & 0x80
        if byte & 0x40:
            shift = 6
            while True:
                byte = raw[position]; position += 1
                value |= (byte & 0x7F) << shift
                shift += 7
                if not byte & 0x80:
                    break
        return -value if negative else value
    def text():
        nonlocal position
        length = compact()
        if length >= 0:
            value = raw[position:position + length].rstrip(b"\0").decode("latin-1")
            position += length
        else:
            value = raw[position:position - 2 * length].decode("utf-16le").rstrip("\0")
            position -= 2 * length
        return value
    def number():
        nonlocal position
        value = struct.unpack_from("<I", raw, position)[0]
        position += 4
        return value
    for _ in range(struct.unpack_from("<I", raw, 0)[0]):
        product, _item, description = number(), number(), text()
        number(); number()
        second = text()
        yield product, (description or second).replace("\t", " ")

if __name__ == "__main__":
    rows = sorted(records(decode(sys.argv[1])))
    open(sys.argv[2], "w", encoding="utf-8", newline="\n").write(
        "".join(f"{product}\t{description}\n" for product, description in rows))
    print(f"{len(rows)} products, {sum(1 for _, d in rows if not d)} without description")
