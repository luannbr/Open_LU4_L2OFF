// serialize.cpp  -  Assemble/Disassemble implementation.
#include "Serialize.h"
#include <cstring>
#include <string>
#include "Shared/src/Log.h"

namespace protocol {

static void putLE(char*& p, unsigned long long v, int n) {
    for (int i = 0; i < n; ++i) {
        p[i] = (char)((v >> (8 * i)) & 0xff);
    }
    p += n;
}

#if !defined(L2NPC_RECOVERED_IOC_ASSEMBLE)
int VAssemble(char* buf, int cap, const char* fmt, va_list args) {
    if (!buf || cap < 0 || !fmt) {
        return 0;
    }

    va_list ap;
    va_copy(ap, args);
    char* p = buf;
    char* end = buf + cap;
    bool ok = true;
    for (const char* f = fmt; *f && ok; ++f) {
        switch (*f) {
            case 'c': {
                int v = va_arg(ap, int);
                if (p + 1 > end) {
                    ok = false;
                    break;
                }
                putLE(p, (unsigned)v, 1);
                break;
            }
            // Current target 0x004550db advances by two bytes but writes only
            // the low byte. The second byte is left untouched.
            case 'C': {
                int v = va_arg(ap, int);
                if (p + 2 > end) {
                    ok = false;
                    break;
                }
                *p = static_cast<char>(v);
                p += 2;
                break;
            }
            case 'h': {
                int v = va_arg(ap, int);
                if (p + 2 > end) {
                    ok = false;
                    break;
                }
                putLE(p, (unsigned)v, 2);
                break;
            }
            case 'd': {
                int v = va_arg(ap, int);
                if (p + 4 > end) {
                    ok = false;
                    break;
                }
                putLE(p, (unsigned)v, 4);
                break;
            }
            case 'Q':
            case 'p': {
                long long v = va_arg(ap, long long);
                if (p + 8 > end) {
                    ok = false;
                    break;
                }
                putLE(p, (unsigned long long)v, 8);
                break;
            }
            case 'F': {
                double v = va_arg(ap, double);
                const float narrowed = static_cast<float>(v);
                if (p + 4 > end) {
                    ok = false;
                    break;
                }
                std::memcpy(p, &narrowed, 4);
                p += 4;
                break;
            }
            case 'f': {
                double v = va_arg(ap, double);
                if (p + 8 > end) {
                    ok = false;
                    break;
                }
                std::memcpy(p, &v, 8);
                p += 8;
                break;
            }
            case 's': {
                const char* s = va_arg(ap, const char*);
                size_t n = s ? std::strlen(s) : 0;
                if (p + (long)n + 1 > end) {
                    ok = false;
                    break;
                }
                if (n) {
                    std::memcpy(p, s, n);
                }
                p[n] = 0;
                p += n + 1;
                break;
            }
            // Shipping Windows wchar_t is a two-byte UTF-16 code unit.
            // Use char16_t so the wire layout stays identical on
            // platforms whose native wchar_t is four bytes.
            case 'S': {
                const char16_t* s = va_arg(ap, const char16_t*);
                size_t n = 0;
                while (s && s[n]) {
                    ++n;
                }
                if (p + (long)(n + 1) * 2 > end) {
                    ok = false;
                    break;
                }
                for (size_t i = 0; i < n; ++i) {
                    putLE(p, (unsigned)(s[i] & 0xffff), 2);
                }
                putLE(p, 0, 2);
                break;
            }
            case 'b': {
                int len = va_arg(ap, int);
                const void* d = va_arg(ap, const void*);
                if (p + len > end) {
                    ok = false;
                    break;
                }
                std::memcpy(p, d, len);
                p += len;
                break;
            }
            case 'B':
            case 'e': {
                int len = va_arg(ap, int);
                const void* d = va_arg(ap, const void*);
                if (p + 1 + len > end) {
                    ok = false;
                    break;
                }
                *p++ = (char)(len & 0xff);
                std::memcpy(p, d, len);
                p += len;
                break;
            }
            default:
                Log.Add(LOG_WARN, "Assemble: unknown fmt char '%c'", *f);
                break;
        }
    }
    va_end(ap);
    return ok ? (int)(p - buf) : 0;
}

int Assemble(char* buf, int cap, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int size = VAssemble(buf, cap, fmt, ap);
    va_end(ap);
    return size;
}
#endif

const unsigned char* VDisassemble(const unsigned char* p, const char* fmt, va_list ap) {
    for (const char* f = fmt; *f; ++f) {
        switch (*f) {
            case 'c': {
                char* o = va_arg(ap, char*);
                *o = (char)*p;
                p += 1;
                break;
            }
            case 'h': {
                void* o = va_arg(ap, void*);
                std::memcpy(o, p, 2);
                p += 2;
                break;
            }
            case 'd': {
                void* o = va_arg(ap, void*);
                std::memcpy(o, p, 4);
                p += 4;
                break;
            }
            case 'Q':
            case 'p':
            case 'f': {
                void* o = va_arg(ap, void*);
                std::memcpy(o, p, 8);
                p += 8;
                break;
            }
            case 's': {
                int cap = va_arg(ap, int);
                char* o = va_arg(ap, char*);
                int i = 0;
                for (; p[i] && i < cap - 1; ++i) {
                    o[i] = (char)p[i];
                }
                o[i] = 0;
                p += i + 1;
                break;
            } // advance past NUL
            case 'S': {
                int cap = va_arg(ap, int);
                char16_t* o = va_arg(ap, char16_t*);
                int maxw = cap / 2 - 1, i = 0;
                for (;; ++i) {
                    unsigned w = p[2 * i] | (p[2 * i + 1] << 8);
                    if (!w || i >= maxw) {
                        break;
                    }
                    o[i] = static_cast<char16_t>(w);
                }
                o[i] = 0;
                p += (i + 1) * 2;
                break;
            } // + NUL wchar
            case 'b': {
                int len = va_arg(ap, int);
                void* o = va_arg(ap, void*);
                std::memcpy(o, p, len);
                p += len;
                break;
            }
            case 'B': {
                char* lenOut = va_arg(ap, char*);
                void* o = va_arg(ap, void*);
                char L = (char)*p;
                *lenOut = L;
                std::memcpy(o, p + 1, (unsigned char)L);
                p += 1 + (unsigned char)L;
                break;
            }
            case 'e': {
                void* o = va_arg(ap, void*);
                unsigned char L = *p;
                std::memcpy(o, p + 1, L);
                p += 1 + L;
                break;
            }
            default:
                Log.Add(LOG_WARN, "Disassemble: unknown fmt char '%c'", *f);
                break;
        }
    }
    return p;
}

const unsigned char* Disassemble(const unsigned char* p, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const unsigned char* r = VDisassemble(p, fmt, ap);
    va_end(ap);
    return r;
}

// ---- generic format-driven parse/emit --------------------------------------
std::vector<Field> ParseByFmt(const unsigned char* p, int len, const char* fmt) {
    std::vector<Field> out;
    const unsigned char* end = p + len;
    for (const char* f = fmt; *f; ++f) {
        Field fd;
        fd.t = *f;
        switch (*f) {
            case 'c':
                if (p + 1 > end) {
                    return out;
                }
                fd.ival = (signed char)*p;
                p += 1;
                break;
            case 'h':
                if (p + 2 > end) {
                    return out;
                }
                {
                    short v;
                    std::memcpy(&v, p, 2);
                    fd.ival = v;
                }
                p += 2;
                break;
            case 'd':
                if (p + 4 > end) {
                    return out;
                }
                {
                    int v;
                    std::memcpy(&v, p, 4);
                    fd.ival = v;
                }
                p += 4;
                break;
            case 'Q':
            case 'p':
                if (p + 8 > end) {
                    return out;
                }
                {
                    long long v;
                    std::memcpy(&v, p, 8);
                    fd.ival = v;
                }
                p += 8;
                break;
            case 'f':
                if (p + 8 > end) {
                    return out;
                }
                {
                    double v;
                    std::memcpy(&v, p, 8);
                    fd.dval = v;
                }
                p += 8;
                break;
            case 's': {
                std::string s;
                while (p < end && *p) {
                    s.push_back((char)*p);
                    ++p;
                }
                if (p < end) {
                    ++p;
                }
                fd.sval = s;
                break;
            }
            case 'S': {
                std::string s;
                while (p + 1 < end) {
                    unsigned w = p[0] | (p[1] << 8);
                    p += 2;
                    if (!w) {
                        break;
                    }
                    s.push_back(w < 0x80 ? (char)w : '?');
                }
                fd.sval = s;
                break;
            }
            // 'b' = fixed 1-byte field (a byte flag). VDisassemble's 'b' takes a
            // caller-supplied length. The map-driven generic path has none, so we
            // consume exactly ONE byte  -  mid-payload 'b' no longer eats the rest,
            // and the fields AFTER it parse correctly (fixes e.g. used_sec).
            case 'b':
                if (p < end) {
                    fd.sval.assign((const char*)p, 1);
                    p += 1;
                }
                break;
            case 'B':
            case 'e': {
                if (p >= end) {
                    return out;
                }
                int L = *p;
                ++p;
                if (p + L > end) {
                    L = (int)(end - p);
                }
                fd.sval.assign((const char*)p, L);
                p += L;
                break;
            }
            default:
                break;
        }
        out.push_back(fd);
    }
    return out;
}

int EmitByFmt(char* buf, int cap, const char* fmt, const std::vector<Field>& vals) {
    char* w = buf;
    char* end = buf + cap;
    size_t vi = 0;
    for (const char* f = fmt; *f; ++f) {
        const Field* v = (vi < vals.size()) ? &vals[vi] : nullptr;
        ++vi;
        long long iv = v ? v->ival : 0;
        switch (*f) {
            case 'c':
                if (w + 1 > end) {
                    return 0;
                }
                *w++ = (char)iv;
                break;
            case 'h':
                if (w + 2 > end) {
                    return 0;
                }
                putLE(w, (unsigned)iv, 2);
                break;
            case 'd':
                if (w + 4 > end) {
                    return 0;
                }
                putLE(w, (unsigned)iv, 4);
                break;
            case 'Q':
            case 'p':
                if (w + 8 > end) {
                    return 0;
                }
                putLE(w, (unsigned long long)iv, 8);
                break;
            case 'f': {
                double dv = v ? v->dval : 0;
                if (w + 8 > end) {
                    return 0;
                }
                std::memcpy(w, &dv, 8);
                w += 8;
                break;
            }
            case 's': {
                const std::string s = v ? v->sval : std::string();
                if (w + (long)s.size() + 1 > end) {
                    return 0;
                }
                std::memcpy(w, s.data(), s.size());
                w[s.size()] = 0;
                w += s.size() + 1;
                break;
            }
            case 'S': {
                const std::string s = v ? v->sval : std::string();
                if (w + (long)(s.size() + 1) * 2 > end) {
                    return 0;
                }
                for (char c : s) {
                    putLE(w, (unsigned char)c, 2);
                }
                putLE(w, 0, 2);
                break;
            }
            // 'b' = fixed 1-byte field (symmetric with ParseByFmt): emit exactly one byte.
            case 'b': {
                if (w + 1 > end) {
                    return 0;
                }
                *w++ = (v && !v->sval.empty()) ? v->sval[0] : 0;
                break;
            }
            case 'B':
            case 'e': {
                const std::string s = v ? v->sval : std::string();
                if (w + 1 + (long)s.size() > end) {
                    return 0;
                }
                *w++ = (char)s.size();
                std::memcpy(w, s.data(), s.size());
                w += s.size();
                break;
            }
            default:
                break;
        }
    }
    return (int)(w - buf);
}

std::string FieldsToStr(const std::vector<Field>& v) {
    std::string r;
    char tmp[64];
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) {
            r += ",";
        }
        const Field& f = v[i];
        switch (f.t) {
            case 'f':
                std::snprintf(tmp, sizeof(tmp), "%g", f.dval);
                r += tmp;
                break;
            case 's':
            case 'S':
                r += "'" + f.sval + "'";
                break;
            case 'b':
            case 'B':
            case 'e':
                std::snprintf(tmp, sizeof(tmp), "<%zuB>", f.sval.size());
                r += tmp;
                break;
            default:
                std::snprintf(tmp, sizeof(tmp), "%lld", f.ival);
                r += tmp;
                break;
        }
    }
    return r;
}

#if !defined(L2NPC_RECOVERED_IOC_ASSEMBLE)
bool SerializeSelfTest() {
    char buf[256];
    // pack a mixed scalar + narrow-string pattern
    int n =
        Assemble(buf, sizeof(buf), "chdQfs", 0x41, 0x1234, (int)0x89abcdef, 0x1122334455667788LL, 3.5, "Hero");
    if (n <= 0) {
        Log.Add(LOG_ERROR, "SelfTest: pack failed");
        return false;
    }

    char c = 0;
    short h = 0;
    int d = 0;
    long long q = 0;
    double fv = 0;
    char s[16] = {0};
    Disassemble((const unsigned char*)buf, "chdQfs", &c, &h, &d, &q, &fv, (int)sizeof(s), s);

    bool ok = (c == 0x41) && ((unsigned short)h == 0x1234) && ((unsigned)d == 0x89abcdefu) && (q == 0x1122334455667788LL) &&
        (fv == 3.5) && (std::strcmp(s, "Hero") == 0);
    Log.Add(
        ok ? LOG_NORMAL : LOG_ERROR, "SerializeSelfTest %s: packed %d bytes; c=%02x h=%04x d=%08x Q=%016llx f=%.1f s='%s'",
        ok ? "PASS" : "FAIL", n, (unsigned char)c, (unsigned short)h, (unsigned)d, (unsigned long long)q, fv, s
    );

    // ---- generic ParseByFmt/EmitByFmt round-trip with a mid-payload 'b' ------
    // Layout "dddbddddQ" (== 0x16 ReqLogoutCharacter): the 'b' sits BETWEEN dwords.
    // the fields after it (incl. used_sec) must survive the round-trip byte-exact.
    std::vector<Field> in = {
        Field::I('d', 111),
        Field::I('d', 222),
        Field::I('d', 333),
        Field::S('b', std::string(1, '\x05')),
        Field::I('d', 7200 /*used_sec*/),
        Field::I('d', 444),
        Field::I('d', 555),
        Field::I('d', 666),
        Field::I('Q', 0x1122334455667788LL),
    };
    char gb[128];
    int gn = EmitByFmt(gb, sizeof(gb), "dddbddddQ", in);
    std::vector<Field> outv = ParseByFmt((const unsigned char*)gb, gn, "dddbddddQ");
    bool gok = outv.size() == 9 && outv[0].ival == 111 && outv[3].sval.size() == 1 &&
        (unsigned char)outv[3].sval[0] == 0x05 && outv[4].ival == 7200 && outv[8].ival == 0x1122334455667788LL;
    Log.Add(
        gok ? LOG_NORMAL : LOG_ERROR, "SerializeSelfTest[b-mid] %s: %d bytes; after-b used_sec=%lld (expect 7200), Q=%llx",
        gok ? "PASS" : "FAIL", gn, gok ? outv[4].ival : (outv.size() > 4 ? outv[4].ival : -1),
        (unsigned long long)(outv.size() > 8 ? outv[8].ival : 0)
    );

    return ok && gok;
}
#endif

} // namespace protocol
