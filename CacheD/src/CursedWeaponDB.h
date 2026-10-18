#pragma once

class CSocket;

// Current Cached.exe guard literals keep this owner and all six static
// packet-handler signatures. RequestPickCursedWeapon and
// RequestDelCursedWeaponOnlyInDB also keep direct CursedWeaponDB.cpp source
// evidence. The remaining four methods are adjacent members of the same
// machine-confirmed owner.
class CCursedWeaponDB {
public:
    static bool RequestLoadCursedWeaponList(CSocket* socket, const unsigned char* packet);
    static bool RequestPickCursedWeapon(CSocket* socket, const unsigned char* packet);
    static bool RequestAddCursedWeapon(CSocket* socket, const unsigned char* packet);
    static bool RequestUpdateCursedWeapon(CSocket* socket, const unsigned char* packet);
    static bool RequestDelCursedWeapon(CSocket* socket, const unsigned char* packet);
    static bool RequestDelCursedWeaponOnlyInDB(CSocket* socket, const unsigned char* packet);
};
