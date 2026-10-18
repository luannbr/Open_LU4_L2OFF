#pragma once

#include "Shared/src/StatusWnd.h"

// CachedStatusWnd.h, no fields beyond the current CacheD CStatusWnd base.
class CachedStatusWnd final : public CStatusWnd {
public:
    CachedStatusWnd();
    ~CachedStatusWnd() override;

    void OnRefresh() override;

    static CachedStatusWnd& Init();
};
