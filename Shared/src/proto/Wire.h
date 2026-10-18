// wire.h  -  the shared L2Server<->CacheD wire-DTO contract.
//
// The `EtcMani*` "maniform" packets are DB-mutation messages L2Server sends to
// CacheD. Both binaries (de)serialize them with the SHARED serialization engine
// (net/serialize.h) using the format strings below, so the bytes on the wire are
// identical. These 8 classes are identity-verified as shared in
// the recovered shared protocol manifest.
//
// IMPORTANT (see design note): each EtcMani body's DoAtCacheInternal()/Rollback()
// binds to its OWN binary's entity classes (CUser/CAccount/CUserDb), which differ
// between CacheD and L2Server. So the *bodies* (proto/reference/EtcMani*.cpp) are
// compiled per-binary against that binary's engine. Only the WIRE CONTRACT  -  the
// base interface and the format strings here  -  is shared. Keeping AssembleAttribute/
// DisassembleAttribute consistent with these strings is what guarantees the two
// binaries exchange byte-identical packets.
//
// Format chars are the shared serialize.h set (c/h/d/Q/p/f/s/S/b/B/e). The Assemble
// form carries a leading `h` = command/subtype tag (value 1). Disassemble omits it
// (it was already consumed as the packet opcode/subtype by the framer).
#pragma once
#include <cstdint>
#include "Serialize.h" // protocol::Assemble / protocol::Disassemble

namespace protocol {

// Main wire format for each maniform (assembleFmt includes the leading `h`
// tag. DisassembleFmt is the payload after the opcode). fieldFmt == disassembleFmt.
struct EtcManiFormat {
    const char* name;
    const char* assembleFmt;
    const char* disassembleFmt;
};

static const EtcManiFormat kEtcManiFormats[] = {
    {"EtcManiCreateItemAuctionBid", "hddddQ", "ddddQ"},
    {"EtcManiGiveItemByCastleSiegeCount", "hddddd", "ddddd"},
    {"EtcManiRollbackUserItemBaseAttribute", "hddddd", "ddddd"},
    {"EtcManiUpdatePledgeNameValue", "hdddd", "dddd"},
    {"EtcManiUpdateUserEventTime", "hddddddQdQdd", "ddddddQdQdd"},
    {"EtcManiUpdateUserItemBaseAttribute", "hdddddhhhhhhhh", "dddddhhhhhhhh"},
    {"EtcManiWithdrawItemAuctionBid", "hddQd", "ddQd"},
};
static const int kEtcManiFormatCount = 7;

// ---------------------------------------------------------------------------
//  IEtcMani  -  the shared maniform interface (the base `EtcMani` vtable surface).
//  A DB mutation that is (dis)assembled on the wire, applied at the cache
//  (DoAtCacheInternal), optionally at the server (DoAtServer), and reversible
//  (Rollback). Bodies live per-binary in proto/reference/EtcMani*.cpp.
// ---------------------------------------------------------------------------
class IEtcMani {
public:
    virtual ~IEtcMani() = default;
    virtual int Assemble(char* buf, int cap) = 0; // pack -> wire bytes
    virtual const unsigned char* Disassemble(const unsigned char* p) = 0; // wire -> fields
    virtual bool DoAtCacheInternal() = 0; // apply against the cache/DB
    virtual bool DoAtServer() { return false; } // usually a no-op at CacheD
    virtual bool Rollback() { return false; } // undo on failure
    virtual void DumpError() {}
};

// lookup the main format for a maniform by class name (nullptr if unknown).
inline const EtcManiFormat* FindEtcManiFormat(const char* name) {
    for (int i = 0; i < kEtcManiFormatCount; ++i) {
        const char* a = kEtcManiFormats[i].name;
        const char* b = name;
        bool eq = true;
        while (*a || *b) {
            if (*a != *b) {
                eq = false;
                break;
            }
            ++a;
            ++b;
        }
        if (eq) {
            return &kEtcManiFormats[i];
        }
    }
    return nullptr;
}

} // namespace protocol
