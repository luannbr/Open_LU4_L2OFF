#pragma once

#include "Object.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <cstddef>
#include <cstdint>


class CGeneralObject : public CObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CGeneralObject)

    CGeneralObject();
    ~CGeneralObject() override;

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    int GetIndex() const override;
    CObject* Copy() override;

    virtual void OnCreate();

    void Delete();
    static CGeneralObject* GetObjectW(std::uint32_t smartId);


protected:
    CGeneralObject(const CGeneralObject& other);

public:
    ::CYieldLock* m_lock;
    CSharedGeneralObjectData* m_data;

private:
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CGeneralObject)
};

using CGeneralObjectSmartIdManager = CSmartIdManager<CGeneralObject, 1020000, 16>;

// Donor spelling. The existence, template parameters and one process-global
// instance come from current L2Server code/data references.
extern CGeneralObjectSmartIdManager g_GeneralObjectManager;

// Current Object.obj 0x008cba44: the L2Server world-sync owner reaches the
// process-global manager through this original physical wrapper.
void GeneralObjectGarbageCollect();
