#pragma once

#include <filesystem>


// Recovered empty singleton from Shared/LocalizationSystem.cpp. The two
// products compile different method bodies from this owner:
// L2NPC loads only npcpos, while L2Server loads all five localized datasets.
class LocalizationSystem {
public:
#if defined(L2_BUILD_L2SERVER)
    void LoadMultiSell(int countryCodeMaximum);
    bool LoadHairData(int countryCodeMaximum);
    bool LoadFaceData(int countryCodeMaximum);
    bool LoadColorData(int countryCodeMaximum);
#endif
    bool LoadNpcposData(int countryCodeMaximum);
    void LoadLocalizationData(int countryCodeMaximum);
};


extern LocalizationSystem g_Localization;

namespace portable_runtime {

// The Windows target uses ../Script/Localization/npcpos.txt relative to its
// executable. This process setting supplies the matching resolved path
// when the portable executable is launched from another working directory.
void ConfigureLocalizationNpcPos(std::filesystem::path baseNpcPosPath);
void ResetLocalizationNpcPos();

} // namespace portable_runtime
