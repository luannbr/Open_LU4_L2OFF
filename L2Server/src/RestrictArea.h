#pragma once

#include "Shared/src/StandardContainers.h"
#include "Shared/src/Territory.h"

#include <cstddef>

class CRestrictArea : public MemoryObject {
public:
    CRestrictArea() = default;
    ~CRestrictArea() override = default;

    L2_DECLARE_OBJECT_TYPE(CRestrictArea)

    void Load();
    bool LoadData(char16_t* fileBuffer, int fileSize);
    bool LoadScript(const wchar_t* fileName, bool decrypt);
};


class CNpcPosDB {
public:
    using NpcPosMap = std::map<std::u16string, CTerritory*>;

    CNpcPosDB() = default;
    ~CNpcPosDB() = default;

    CTerritory* GetNpcPos(char16_t* name);
    void AddNpcPos(CTerritory* territory);

private:
    NpcPosMap m_NpcPosDB;
};


extern CNpcPosDB g_NpcPosDB;
extern CNpcPosDB g_NpcAnnounceDB;
extern CNpcPosDB g_RestrictedAreaDB;
extern CRestrictArea g_RestrictArea;
