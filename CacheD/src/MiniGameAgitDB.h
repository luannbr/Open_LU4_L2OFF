#pragma once

class CSocket;

// Current Cached.exe guard strings confirm the CMiniGameAgitDB owner and all
// six static signatures. The matching PDB tail is destroyed, so the physical
// MiniGameAgitDB filename is derived from that owner instead of claimed as a
// surviving source-path record.
class CMiniGameAgitDB {
public:
    static bool RequestLoadMiniGameAgit(CSocket* socket, const unsigned char* packet);
    static bool RequestSetMiniGameAgitStatus(CSocket* socket, const unsigned char* packet);
    static bool RequestAddMiniGameAgitPledge(CSocket* socket, const unsigned char* packet);
    static bool RequestDelMiniGameAgitPledge(CSocket* socket, const unsigned char* packet);
    static bool RequestAddMiniGameAgitMember(CSocket* socket, const unsigned char* packet);
    static bool RequestResetMiniGameAgit(CSocket* socket, const unsigned char* packet);
};
