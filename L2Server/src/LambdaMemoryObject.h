#pragma once

#include "Shared/src/MemorySub.h"

// Current L2Server keeps only this physical owner's class-id initializer
// and static class-name carrier. The complete TotalChecker PDB declaration
// confirms an otherwise empty MemoryObject derivative with the ordinary
// class-specific allocation/AddRef/Release surface and exact 0x18 extent.
class LambdaMemoryObject : public MemoryObject {
public:
    L2_DECLARE_OBJECT_TYPE(LambdaMemoryObject)
};
