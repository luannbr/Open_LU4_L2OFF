#pragma once

#include "Lock.h"

#include <map>
#include <set>

// Original owner: Src/accountcontainer.h. L2Authd.pdb type 0x828F,
// target size 0x28. Native STL nodes use the host ABI in the portable build.
// the nested data graph and lock remain the members recorded by the PDB.
class AccountContainer {
public:
    using UidList = std::set<int>;

    struct CustomerTimeData {
        int fcid;
        UidList accountData;
    };

    using CustomerData = std::map<int, CustomerTimeData>;

    AccountContainer();
    ~AccountContainer();

    bool IsOverLimit(int fcid, int age, char authFlag);
    bool IsFatigueCustomer(int fcid, int age, char authFlag);
    void InsertFatigueAccount(int fcid, int uid);
    void RemoveFatigueAccount(int fcid, int uid);
    void SetAccountTime(int uid, int time);
    void GetAccountTime(
        int fcid,
        int* totalTime,
        int* lastLogoutTime);
    void GetLastLogoutTime(int uid, int* lastLogoutTime);

    static AccountContainer* Inst() {
        return sInstance;
    }

private:
    CustomerData mCustomerData;
    CLock mLock;

    static AccountContainer* sInstance;
};
