#pragma once

#include "Agit.h"

#include <cstddef>

// Current CBaseCampAgit has no data beyond CWorldAgit. Its 57-slot vtable
// differs from the base only in class lifetime, TimerExpired and the
// base-camp query/cast pair.
class CBaseCampAgit final : public CWorldAgit {
public:
    CBaseCampAgit();
    ~CBaseCampAgit() override;

    L2_DECLARE_OBJECT_TYPE(CBaseCampAgit)

    void TimerExpired(int id) override;
    bool IsBaseCampAgit() const override;
    CBaseCampAgit* CastBaseCampAgit() const override;
};
