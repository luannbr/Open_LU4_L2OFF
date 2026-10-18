#pragma once

#include "Shared/src/MemorySub.h"

#include "IOC.h"
#include "Shared/src/Runtime/RawAtomic.h"

#include <cstdint>


// Recovered from l2npc/object.h and object.cpp.
class CObject : public CIOObject {
public:
    CObject();
    ~CObject() override;

    L2_DECLARE_OBJECT_TYPE(CObject)

    virtual bool Delete();
    std::uint32_t Id() const { return m_nId; }
    bool Deleted() const { return runtime::raw_atomic::LoadI32Relaxed(&mDeleteCount) != 0; }

    // Public in the target PDB and read directly by CSmartID.
    std::uint32_t m_nId;

protected:
    // Present as a protected declaration in the target PDB. No linked body
    // exists in either executable, so defining copy meaning here would be
    // an invention.
    CObject(const CObject&);

private:
    // Windows long is 32-bit. The PDB declares volatile long here, while
    // Delete applies InterlockedIncrement to this exact storage.
    volatile std::int32_t mDeleteCount;
};
