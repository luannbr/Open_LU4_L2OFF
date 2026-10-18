#pragma once

#include "Shared/src/Obj.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CInspector : public MemoryObject {
public:
    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    ~CInspector() override;

    int p1; // +0x18
    int p2; // +0x1c
    int p3; // +0x20
    int p4; // +0x24
    int r; // +0x28
    void* Bin; // +0x30
    int BinSize; // +0x38

private:
    L2_DECLARE_OBJECT_REGISTRATION(CInspector)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CInspector)
};

class CInspectorDB {
public:
    using InspectorList = std::vector<CInspector*>;
    using InspectorsDB = std::map<int, InspectorList>;

    void Load();
    void Unload();
    CInspector* GetInspector(int x, int y);

private:
    InspectorsDB m_vec; // +0x00
};

extern CInspectorDB g_InspectorDB;
