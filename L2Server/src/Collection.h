#pragma once

#include "NPC.h"

#include <cstddef>
#include <cstdint>


// Exact current Collection.obj owner. The current parser_npc materializer
// allocates 0x3d80 bytes and appends one interlocked LONG to the complete
// 0x3d78-byte CNPC envelope.
class CCollection : public CNPC {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CCollection)

    CCollection();
    CCollection(const CCollection& other);
    ~CCollection() override;

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()
    void ReportOrt(const char* callerFile, int callerLine) override;
    CObject* Copy() override;
    bool IsCollection() const override;
    CCollection* CastCollection() override;

    bool SetBeingCollected();
    void ResetBeingCollected();
    bool CanBeCollected() const;


private:
    volatile std::int32_t m_lBeingCollected;
    [[maybe_unused]] std::byte m_alignment3d7cTo3d80[0x04];

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CCollection)
};
