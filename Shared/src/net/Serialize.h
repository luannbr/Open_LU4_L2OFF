// serialize.h  -  the CacheD packet serialization engine (reconstructed from
// reconstruction/decompiled/cached_proto/{Assemble,Disassemble,VDisassemble}.c).
//
// Assemble(buf,cap,fmt,...) packs variadic VALUES into buf per fmt.
// Disassemble(p,fmt,...) / VDisassemble unpack p into out-POINTERS per fmt.
//
// Format chars recovered from VDisassemble @0x481c74 + the handler call sites
// (e.g. EtcManiUpdateUserEventTime uses "hddddddQdQdd"):
//   c  1-byte  int8            (pack: int    / unpack: char*)
//   h  2-byte  int16   LE      (pack: int    / unpack: short*)
//   d  4-byte  int32   LE      (pack: int    / unpack: int*)
//   Q  8-byte  int64   LE      (pack: int64  / unpack: int64*)
//   p  8-byte  (ptr/int64)     (pack: int64  / unpack: void*)  same width as Q
//   f  8-byte  double          (pack: double / unpack: double*)
//   s  narrow NUL-terminated   (pack: char*  / unpack: int cap, char*)
//   S  UTF-16LE NUL-terminated (pack: char16_t* /
//                              unpack: int capBytes, char16_t*)
//   b  fixed binary            (pack: int len, void* / unpack: int len, void*)
//   B  1-byte-len-prefixed bin (pack: int len, void* / unpack: char* lenOut, void*)
//   e  1-byte-len-prefixed bin (pack: int len, void* / unpack: void*)  (len consumed)
#pragma once
#include <cstdarg>
#include <cstdint>
#include <string>
#include <vector>

namespace protocol {

// Exact va_list entry used by CSocket::SendV. The current L2NPC target's
// unnamed implementation is at 0x00454fa8. Assemble is only its variadic
// wrapper.
int VAssemble(char* buf, int cap, const char* fmt, va_list args);

// pack  -  returns number of bytes written, or 0 on overflow (matches Send's
// "assemble too large" 0-return). buf/cap bound the output.
int Assemble(char* buf, int cap, const char* fmt, ...);

// unpack  -  returns the pointer just past the consumed input.
const unsigned char* Disassemble(const unsigned char* p, const char* fmt, ...);
const unsigned char* VDisassemble(const unsigned char* p, const char* fmt, va_list ap);

// round-trip self-test (pack then unpack a few field patterns). Returns true if OK.
bool SerializeSelfTest();

// ---- generic (format-driven) parse/emit  -  used to wire every handler's fmt ---
// A decoded field. `t` is the format char. Scalars in ival, doubles in dval,
// strings/blobs in sval (S decoded to UTF-8. B/B/e held as raw bytes).
struct Field {
    char t = 0;
    long long ival = 0;
    double dval = 0;
    std::string sval;
    static Field I(char t, long long v) {
        Field f;
        f.t = t;
        f.ival = v;
        return f;
    }
    static Field D(double v) {
        Field f;
        f.t = 'f';
        f.dval = v;
        return f;
    }
    static Field S(char t, const std::string& s) {
        Field f;
        f.t = t;
        f.sval = s;
        return f;
    }
};

// parse `len` bytes at `p` per `fmt` into a field vector (stops safely at end).
std::vector<Field> ParseByFmt(const unsigned char* p, int len, const char* fmt);
// emit `fmt` into buf (cap) drawing values from `vals` in order (missing -> 0/empty).
int EmitByFmt(char* buf, int cap, const char* fmt, const std::vector<Field>& vals);
// human-readable dump of parsed fields (for handler logging).
std::string FieldsToStr(const std::vector<Field>& v);

} // namespace protocol
