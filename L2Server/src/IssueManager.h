#pragma once

#include "Shared/src/StandardContainers.h"

#include <cstddef>

class Issue;

class IssueManager {
public:
    IssueManager();
    virtual ~IssueManager();

    static IssueManager* CreateInstance();

private:
    // Current 0x007d0bec stores the sentinel at +0x08 and the count at
    // +0x10 in the shipping list. Portable code owns a standard list
    // directly and does not emulate its internal lanes.
    std::list<Issue*> m_issueDBforUser;

    static IssueManager* ms_pInstance;
};
