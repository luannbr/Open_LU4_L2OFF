#pragma once

#include "NPC.h"

class CZzoldagu : public CNPC {
public:

    CZzoldagu();
    CZzoldagu(const CZzoldagu& other);
    ~CZzoldagu() override;

    static void* operator new(std::size_t requestedSize);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void ReportOrt(const char* callerFile, int callerLine) override;
    CObject* Copy() override;
    bool IsZzoldagu() override;
    virtual CZzoldagu* CastZzoldagu();
};
