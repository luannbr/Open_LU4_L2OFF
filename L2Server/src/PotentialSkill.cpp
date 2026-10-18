#include "PotentialSkill.h"

#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"
#include "XmlLoader.h"

#include <cstdint>
#include <string_view>

namespace {

constexpr const wchar_t* kPotentialSkillFile = L"..\\Script\\AbilityData.xml";

char16_t FoldAscii(char16_t value) noexcept {
    if (value >= u'A' && value <= u'Z') {
        return static_cast<char16_t>(value + (u'a' - u'A'));
    }
    return value;
}

bool EqualsAsciiCaseInsensitive(std::u16string_view left, std::u16string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (FoldAscii(left[index]) != FoldAscii(right[index])) {
            return false;
        }
    }
    return true;
}

} // namespace

CPotentialSkillDB* CPotentialSkillDB::m_PotentialSkillDB = nullptr;

CPotentialSkillDB& CPotentialSkillDB::Inst() {
    // Current L2Server.exe 0x009e87e8.
    L2_GUARD_FUNCTION();

    if (m_PotentialSkillDB == nullptr) {
        m_PotentialSkillDB = new CPotentialSkillDB;
    }
    return *m_PotentialSkillDB;
}

PotentialSkillAcquireInfo* CPotentialSkillDB::IsExistPotential(int skillId) {
    // Current L2Server.exe 0x009e88f0 compares against the process
    // singleton's end iterator, even though this is an instance method.
    L2_GUARD_FUNCTION();

    const auto found = m_mapPotentailSkillAcquireInfos.find(skillId);
    CPotentialSkillDB& singleton = Inst();
    if (found == singleton.m_mapPotentailSkillAcquireInfos.end()) {
        return nullptr;
    }
    return found->second;
}

void CPotentialSkillDB::Load() {
    // Current L2Server.exe 0x009e89d4.
    L2_GUARD_FUNCTION();

    XMLElement* root = !g_bScriptSecureEnabled ? CXmlLoader::inst().Load(kPotentialSkillFile)
                                               : CXmlLoader::inst().LoadSecure(kPotentialSkillFile);
    if (root == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Fail to load %s",
        //     L"PotentialSkill.cpp",
        //     0x28,
        //     kPotentialSkillFile)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Fail to load %s",
        //     L"PotentialSkill.cpp",
        //     0x28,
        //     kPotentialSkillFile)
        L2_LOG_ERROR(
            Log, "Cannot load AbilityData.xml through the %s XML loader; existing potential-skill data is retained.",
            g_bScriptSecureEnabled ? "secure" : "plain"
        );
        return;
    }

    // Current reload drops the tree nodes but does not delete the pointed-to
    // records. Preserve that process-lifetime ownership and overwrite leak.
    m_mapPotentailSkillAcquireInfos.clear();

    for (XMLElement* section : root->m_vChild) {
        if (section == nullptr) {
            continue;
        }

        if (EqualsAsciiCaseInsensitive(runtime::StringView(section->m_sName), u"Setting")) {
            for (XMLElement* setting : section->m_vChild) {
                if (setting != nullptr && EqualsAsciiCaseInsensitive(runtime::StringView(setting->m_sName), u"Set")) {
                    m_nResetSP = XMLUtil::GetAttributeValueAsInt64(setting, std::u16string(u"ResetSP"), 0);
                }
            }
            continue;
        }

        if (!EqualsAsciiCaseInsensitive(runtime::StringView(section->m_sName), u"Ability")) {
            continue;
        }

        for (XMLElement* element : section->m_vChild) {
            if (element == nullptr) {
                continue;
            }

            const int type = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"TYPE"), 0);
            const int skillId = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"SkillID"), 0);
            const int skillLevel = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"SkilllLev"), 0);

            // The current body evaluates Depth but stores literal zero.
            (void)XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"Depth"), 0);
            const int requireSkillId = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"RequireSkillID"), 0);
            const int requireLevel = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"RequireLev"), 0);
            const int requirePotentialPoints = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"RequirePP"), 0);
            const int requireSkillLevel = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"RequireSkillLev"), 0);
            const int requireCount = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"RequireCount"), 0);

            PotentialSkillAcquireInfo* const info = new PotentialSkillAcquireInfo;
            info->nSkillID = skillId;
            info->nSkillLevel = skillLevel;

            // Literal current producer quirk: the parsed RequireSkillID is
            // written into nRequirePP, while RequirePP is written into
            // nRequireSkillId. Consumers use the physical field names.
            info->nRequirePP = requireSkillId;
            info->nDepth = 0;
            info->nRequireLev = requireLevel;
            info->nRequireSkillId = requirePotentialPoints;
            info->nRequireiSkillLev = requireSkillLevel;
            info->nRequireCount = requireCount;
            info->etype = type;

            m_mapPotentailSkillAcquireInfos[skillId] = info;
        }
    }

    root->ReleaseChild();
    delete root;
}
