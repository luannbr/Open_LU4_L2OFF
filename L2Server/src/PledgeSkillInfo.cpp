#include "PledgeSkillInfo.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TextRuntime.h"
#include "Skill.h"
#include "XmlLoader.h"

#include <cstdint>
#include <string>

PledgeSkillInfo::PledgeSkillInfo() :
    skill_id(0),
    skill_level(0),
    add_type(PledgeAddType::MANUAL),
    social_class(SocialClass::WANDERER),
    expiry_time(0) {}

bool PledgeSkillInfo::LoadFromXML(const XMLElement* element) {
    L2_GUARD_FUNCTION();

    if (!element) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x17,
        //     L"(pElement != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x17,
        //     L"(pElement != NULL)")
        L2_LOG_ERROR(Log, "PledgeSkillInfo.xml contains a null skill element; the record cannot be loaded.");
        return false;
    }

    name_id = XMLUtil::GetAttributeValue(element, std::u16string(u"name_id"), std::u16string(u"undefined"));

    const std::wstring hostName = Utf16ToWide(name_id);
    int subLevel = 0;
    if (!g_SkillDB.GetSkillIdLevelByName(hostName.c_str(), &skill_id, &skill_level, &subLevel)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Undefined skill: %s",
        //     kSourceFile,
        //     0x21,
        //     hostName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]Undefined skill: %s",
        //     kSourceFile,
        //     0x21,
        //     hostName.c_str())
        L2_LOG_ERROR(
            Log, "PledgeSkillInfo.xml skill name '%s' cannot be resolved; the record is rejected.",
            WideToUtf8(hostName).c_str()
        );
        return false;
    }

    add_type = PledgeStuff::GetAttributeValueAsAddType(element, std::u16string(u"add_type"), PledgeAddType::MANUAL);
    social_class =
        PledgeStuff::GetAttributeValueAsSocialClassType(element, std::u16string(u"social_class"), SocialClass::WANDERER);
    expiry_time =
        runtime::MultiplyInt32Wrapping(XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"expiry_time"), 0), 3600);

    if (!Cost.LoadFromXML(XMLUtil::FindChild(element, std::u16string(u"Cost")))) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]No Cost : %d, %d",
        //     kSourceFile,
        //     0x31,
        //     skill_id,
        //     skill_level)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]No Cost : %d, %d",
        //     kSourceFile,
        //     0x31,
        //     skill_id,
        //     skill_level)
        L2_LOG_ERROR(
            Log, "PledgeSkillInfo.xml cost loading failed for skill %d level %d; the record is rejected.", skill_id,
            skill_level
        );
        return false;
    }

    if (!Condition.LoadFromXML(XMLUtil::FindChild(element, std::u16string(u"Condition")))) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]No condition : %d, %d",
        //     kSourceFile,
        //     0x3a,
        //     skill_id,
        //     skill_level)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]No condition : %d, %d",
        //     kSourceFile,
        //     0x3a,
        //     skill_id,
        //     skill_level)
        L2_LOG_ERROR(
            Log, "PledgeSkillInfo.xml condition loading failed for skill %d level %d; the record is rejected.", skill_id,
            skill_level
        );
        return false;
    }
    return true;
}

const PledgeSkillInfo* PledgeSkillIndexer::Find(int skillId, int skillLevel) const {
    L2_GUARD_FUNCTION();

    const SkillKey key(skillId, skillLevel);
    const auto found = m_RewardSkill.find(key);
    if (found != m_RewardSkill.end()) {
        return found->second;
    }

    if (!g_SkillDB.GetSkillInfo(CSkillKey(skillId, skillLevel), nullptr)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]no reward skill exist: %d, %d",
        //     kSourceFile,
        //     0x6c,
        //     skillId,
        //     skillLevel)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]no reward skill exist: %d, %d",
        //     kSourceFile,
        //     0x6c,
        //     skillId,
        //     skillLevel)
        L2_LOG_ERROR(
            Log,
            "Clan-skill lookup failed for skill %d level %d: neither the clan reward index nor the general skill database "
            "has a definition.",
            skillId, skillLevel
        );
    }
    return nullptr;
}

void PledgeSkillIndexer::Setup(const std::vector<PledgeSkillInfo*>& skills) {
    L2_GUARD_FUNCTION();

    m_RewardSkill.clear();
    for (PledgeSkillInfo* info : skills) {
        m_RewardSkill[SkillKey(info->skill_id, info->skill_level)] = info;
    }
}

PledgeSkillDB::PledgeSkillDB() : m_RewardSkills() {}

PledgeSkillDB::~PledgeSkillDB() {
    for (PledgeSkillInfo* info : Skills) {
        delete info;
    }
    Skills.clear();
}

const std::vector<PledgeSkillInfo*>& PledgeSkillDB::GetInfos() const {
    L2_GUARD_FUNCTION();
    return Skills;
}

const PledgeSkillInfo* PledgeSkillDB::FindSkill(int skillId, int skillLevel) const {
    return m_RewardSkills.Find(skillId, skillLevel);
}

void PledgeSkillDB::MakeRewardSkillIndex() {
    L2_GUARD_FUNCTION();
    m_RewardSkills.Setup(GetInfos());
}

void PledgeSkillDB::Load() {
    L2_GUARD_FUNCTION();

    constexpr const wchar_t* kFileName = L"..\\Script\\PledgeSkillInfo.xml";
    XMLElement* root = CXmlLoader::inst().Load(kFileName);
    if (!root) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Failed to load : %s",
        //     kSourceFile,
        //     0xac,
        //     kFileName)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]Failed to load : %s",
        //     kSourceFile,
        //     0xac,
        //     kFileName)
        L2_LOG_ERROR(
            Log,
            "Cannot load PledgeSkillInfo.xml: %llu skill definitions are currently held; this load adds none and leaves the "
            "reward index unchanged.",
            static_cast<unsigned long long>(Skills.size())
        );
        return;
    }

    for (const XMLElement* child : root->m_vChild) {
        PledgeSkillInfo* info = new PledgeSkillInfo;
        if (!info->LoadFromXML(child)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]Skill failed: %d",
            //     kSourceFile,
            //     0xb7,
            //     static_cast<int>(Skills.size()))
            // Noise: LoadFromXML already reports the rejected record and its cause. Skills.size() is not an XML record
            // position. L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]Skill failed: %d",
            //     kSourceFile,
            //     0xb7,
            //     static_cast<int>(Skills.size()))
            delete info;
            continue;
        }
        Skills.push_back(info);
    }

    root->ReleaseChild();
    delete root;
    MakeRewardSkillIndex();
}

void PledgeSkillDB::Builder::Reload(PledgeSkillDB* database) {
    L2_GUARD_FUNCTION();

    database->Skills.clear();
    database->Load();
}
