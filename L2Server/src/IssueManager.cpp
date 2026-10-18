#include "IssueManager.h"



IssueManager* IssueManager::ms_pInstance = nullptr;

IssueManager::IssueManager() {}

IssueManager::~IssueManager() = default;

IssueManager* IssueManager::CreateInstance() {
    // Current L2Server 0x007d0c80 toggles: an absent instance
    // is allocated and constructed, while an existing one is deleted and
    // the singleton is cleared.
    if (ms_pInstance == nullptr) {
        ms_pInstance = new IssueManager;
    } else {
        delete ms_pInstance;
        ms_pInstance = nullptr;
    }
    return ms_pInstance;
}
