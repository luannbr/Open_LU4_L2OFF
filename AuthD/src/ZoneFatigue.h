#pragma once

// Original owner: Src/zonefatigue.h. L2Authd.pdb type 0x56BA, field list
// 0x56B9, sizeof 0x318. This is a standalone AuthD product class. It is not
// one of the cross-product Shared owners.
class ZoneFatigue {
public:
    ZoneFatigue();

    static int ResetCapsuleInfo(unsigned char zone, const char* pCapsuleInfo);
    static int ResetCapsuleInfo(unsigned char capsuleCount,
                                unsigned char zone,
                                unsigned char* capsuleId,
                                unsigned char capsuleIdSize,
                                unsigned char* capsuleTimeAmount,
                                unsigned char capsuleTimeAmountSize);

    int UpdateRemainTime(int gameAccountNo, unsigned char zone, int remainTime);
    int UseCapsule([[maybe_unused]] int gameAccountNo,
                   [[maybe_unused]] unsigned char zone,
                   unsigned char capsuleId,
                   int totalRemainTime);
    int GetZoneFatigue(int gameAccountNo, unsigned char zone);

    int GetRemainTime() { return mRemainTime; }

    int GetCapsuleCount() { return mCapsuleCount; }

    unsigned char* GetCapsuleUsedFlag() { return mCapsuleUsedFlag; }

    unsigned char* GetCapsuleId() { return mCapsuleId; }

    int* GetCapsuleTimeAmount() { return mCapsuleTimeAmount; }

    int GetCapsuleLastUsedTime() { return mCapsuleLastUsedTime; }

    // The selected image does not emit or call these header-only accessors.
    // mLastDbIO is initialized to zero and written only after a successful
    // GetZoneFatigue result, making this the unique state projection matching
    // the PDB method name and bool return type.
    bool SuccessfullyLoaded() { return mLastDbIO != 0; }

private:
    int mGameAccountNo; // target +0x000
    unsigned char mZone; // target +0x004
    int mRemainTime; // target +0x008
    unsigned char mCapsuleCount; // target +0x00C
    unsigned char mCapsuleUsedFlag[128]; // target +0x00D
    unsigned char mCapsuleId[128]; // target +0x08D
    int mCapsuleTimeAmount[128]; // target +0x110
    int mCapsuleLastUsedTime; // target +0x310
    int mLastDbIO; // target +0x314
};
