#pragma once

#include "AgitDeco.h"

#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <cstddef>
#include <cstdint>
#include <cwctype>

struct ResidenceDecoNameLess {
    bool operator()(const std::u16string& left, const std::u16string& right) const noexcept {
        const auto leftView = runtime::StringView(left);
        const auto rightView = runtime::StringView(right);
        const std::size_t common = leftView.size() < rightView.size() ? leftView.size() : rightView.size();
        for (std::size_t index = 0; index < common; ++index) {
            const std::wint_t leftUnit = std::towlower(static_cast<std::wint_t>(leftView[index]));
            const std::wint_t rightUnit = std::towlower(static_cast<std::wint_t>(rightView[index]));
            if (leftUnit != rightUnit) {
                return leftUnit < rightUnit;
            }
        }
        return leftView.size() < rightView.size();
    }
};

class CResidenceDecoDb {
public:
    CResidenceDecoDb();
    ~CResidenceDecoDb();

    static CResidenceDecoDb& Inst();

    void Load();
    void AddResidenceDeco(CResidenceDeco* residenceDeco);
    CResidenceDeco* FindResidenceDeco(std::uint32_t decoId);
    CResidenceDeco* FindResidenceDeco(int type, int level);

private:
    std::map<std::u16string, CResidenceDeco*, ResidenceDecoNameLess> m_mapNameToResidenceDeco; // +0x00
    std::map<std::uint32_t, CResidenceDeco*> m_mapIdToResidenceDeco; // +0x20
    std::map<std::CDualKey<int, int>, CResidenceDeco*> m_mapTypeLevelToResidenceDeco; // +0x40
};
