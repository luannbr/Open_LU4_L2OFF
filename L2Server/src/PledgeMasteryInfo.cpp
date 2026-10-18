#include "PledgeMasteryInfo.h"

#include "Shared/src/Log.h"
#include "Shared/src/TextRuntime.h"
#include "XmlLoader.h"

#include <string>

namespace {

const PledgeCostInfo NullCost;


} // namespace

bool PledgeMasteryPointInfo::LoadFromXML(
    const XMLElement* element, PledgeMasteryID::Type masteryID [[maybe_unused]], int pointStep [[maybe_unused]]
) {
    L2_GUARD_FUNCTION();

    if (!element) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMasteryInfo.cpp",
        //     0x35,
        //     L"(pElement != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMasteryInfo.cpp",
        //     0x35,
        //     L"(pElement != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot load clan mastery %d point step %d: the XML element is missing; the point record is rejected.",
            static_cast<int>(masteryID), pointStep
        );
        return false;
    }

    const XMLElement* costElement = XMLUtil::FindChild(element, std::u16string(u"Cost"));
    if (!Cost.LoadFromXML(costElement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeMasteryInfo.cpp",
        //     0x3b)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeMasteryInfo.cpp",
        //     0x3b)
        L2_LOG_ERROR(
            Log, "Cannot load costs for clan mastery %d point step %d; the point record is rejected.",
            static_cast<int>(masteryID), pointStep
        );
        return false;
    }
    return true;
}

bool PledgeMasteryRequirementInfo::LoadFromXML(const XMLElement* element) {
    L2_GUARD_FUNCTION();

    if (!element) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMasteryInfo.cpp",
        //     0x16,
        //     L"(pElement != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMasteryInfo.cpp",
        //     0x16,
        //     L"(pElement != NULL)")
        L2_LOG_ERROR(Log, "Cannot load a clan-mastery requirement: its XML element is missing.");
        return false;
    }

    min_point = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"min_point"), 0);
    if (min_point < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeMasteryInfo.cpp",
        //     0x1c)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeMasteryInfo.cpp",
        //     0x1c)
        L2_LOG_ERROR(Log, "Invalid clan-mastery requirement: min_point is %d; expected a nonnegative value.", min_point);
        return false;
    }

    const std::vector<XMLElement*> elements = XMLUtil::Select(element, std::u16string(u"MasteryID"));
    for (const XMLElement* masteryElement : elements) {
        MasteryID.push_back(
            PledgeStuff::GetAttributeValueAsMasterID(masteryElement, std::u16string(u"id"), PledgeMasteryID::NONE)
        );
    }
    return true;
}

bool PledgeMasteryResetInfo::LoadFromXML(const XMLElement* element) {
    L2_GUARD_FUNCTION();

    if (!element) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMasteryInfo.cpp",
        //     0xd5,
        //     L"(pElement != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMasteryInfo.cpp",
        //     0xd5,
        //     L"(pElement != NULL)")
        L2_LOG_ERROR(
            Log,
            "Cannot load clan-mastery reset settings: the Reset XML element is missing; existing reset settings are "
            "retained."
        );
        return false;
    }

    penalty_day = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"penalty_day"), 0) * 86400;
    const XMLElement* costElement = XMLUtil::FindChild(element, std::u16string(u"Cost"));
    return Cost.LoadFromXML(costElement);
}

PledgeMasteryInfo::PledgeMasteryInfo() : id(PledgeMasteryID::NONE) { Points.emplace_back(); }

PledgeMasteryInfo::~PledgeMasteryInfo() = default;

int PledgeMasteryInfo::GetMaxPointStep() const {
    L2_GUARD_FUNCTION();
    return static_cast<int>(Points.size()) - 1;
}

bool PledgeMasteryInfo::IsValidPointStep(int pointStep) const {
    L2_GUARD_FUNCTION();
    return pointStep > 0 && pointStep <= GetMaxPointStep();
}

const PledgeCostInfo& PledgeMasteryInfo::GetCost(int pointStep) const {
    L2_GUARD_FUNCTION();
    if (!IsValidPointStep(pointStep)) {
        return NullCost;
    }
    return runtime::CheckedVectorAt(Points, static_cast<std::size_t>(pointStep)).Cost;
}

void PledgeMasteryInfo::LoadRequirements(const XMLElement* element) {
    L2_GUARD_FUNCTION();

    const std::vector<XMLElement*> requirements = XMLUtil::Select(element, std::u16string(u"Require"));
    for (const XMLElement* requirementElement : requirements) {
        PledgeMasteryRequirementInfo requirement;
        if (!requirement.LoadFromXML(requirementElement)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]Failed to load Require: %d, %d",
            //     L"PledgeMasteryInfo.cpp",
            //     0xa1,
            //     static_cast<int>(id),
            //     static_cast<int>(Require.size()))
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]Failed to load Require: %d, %d",
            //     L"PledgeMasteryInfo.cpp",
            //     0xa1,
            //     static_cast<int>(id),
            //     static_cast<int>(Require.size()))
            L2_LOG_ERROR(
                Log,
                "Clan mastery %d: requirement entry %llu failed validation but is still appended to the requirements list.",
                static_cast<int>(id), static_cast<unsigned long long>(Require.size())
            );
        }
        Require.push_back(requirement);
    }
}

bool PledgeMasteryInfo::LoadFromXML(const XMLElement* element) {
    L2_GUARD_FUNCTION();

    if (!element) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMasteryInfo.cpp",
        //     0x5a,
        //     L"(pElement != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMasteryInfo.cpp",
        //     0x5a,
        //     L"(pElement != NULL)")
        L2_LOG_ERROR(Log, "Cannot load a clan-mastery definition: the XML element is missing; the definition is rejected.");
        return false;
    }

    id = PledgeStuff::GetAttributeValueAsMasterID(element, std::u16string(u"id"), PledgeMasteryID::NONE);
    if (id < PledgeMasteryID::PLEDGE_LV_1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Invalid ID: %d",
        //     L"PledgeMasteryInfo.cpp",
        //     0x60,
        //     static_cast<int>(id))
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]Invalid ID: %d",
        //     L"PledgeMasteryInfo.cpp",
        //     0x60,
        //     static_cast<int>(id))
        L2_LOG_ERROR(
            Log, "Clan-mastery definition rejected: ID %d is below the first supported ID %d.", static_cast<int>(id),
            static_cast<int>(PledgeMasteryID::PLEDGE_LV_1)
        );
        return false;
    }

    LoadRequirements(element);

    const XMLElement* pointsElement = XMLUtil::FindChild(element, std::u16string(u"Points"));
    if (!pointsElement) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]No Points: %d",
        //     L"PledgeMasteryInfo.cpp",
        //     0x6c,
        //     static_cast<int>(id))
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]No Points: %d",
        //     L"PledgeMasteryInfo.cpp",
        //     0x6c,
        //     static_cast<int>(id))
        L2_LOG_ERROR(Log, "Clan mastery %d has no Points XML element; the definition is rejected.", static_cast<int>(id));
        return false;
    }

    static const std::u16string pointElementName(u"Point");
    for (const XMLElement* pointElement : pointsElement->m_vChild) {
        if (runtime::StringView(pointElement->m_sName) != runtime::StringView(pointElementName)) {
            const std::string elementName = Utf16ToUtf8(pointElement->m_sName);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]Invalid element: %s",
            //     L"PledgeMasteryInfo.cpp",
            //     0x78,
            //     elementName.c_str())
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]Invalid element: %s",
            //     L"PledgeMasteryInfo.cpp",
            //     0x78,
            //     elementName.c_str())
            L2_LOG_ERROR(
                Log, "Clan mastery %d: unexpected element '%s' in Points; expected Point. The element is skipped.",
                static_cast<int>(id), elementName.c_str()
            );
            continue;
        }

        PledgeMasteryPointInfo point;
        if (!point.LoadFromXML(pointElement, id, static_cast<int>(Points.size()))) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]Failed to load Reward: %d",
            //     L"PledgeMasteryInfo.cpp",
            //     0x80,
            //     static_cast<int>(id))
            // Noise: The point loader already reports the rejected record with mastery ID and point step. Loading continues
            // with the next element. L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]Failed to load Reward: %d",
            //     L"PledgeMasteryInfo.cpp",
            //     0x80,
            //     static_cast<int>(id))
            continue;
        }
        Points.push_back(point);
    }

    if (Points.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeMasteryInfo.cpp",
        //     0x89)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeMasteryInfo.cpp",
        //     0x89)
        L2_LOG_ERROR(Log, "Clan mastery %d has empty point storage; the definition is rejected.", static_cast<int>(id));
        return false;
    }
    return true;
}

PledgeMasteryDB::PledgeMasteryDB() : Reset() {}

PledgeMasteryDB::~PledgeMasteryDB() {
    for (const auto& mastery : Masteries) {
        delete mastery.second;
    }
    Masteries.clear();
}

const PledgeMasteryInfo* PledgeMasteryDB::FindInfo(PledgeMasteryID::Type masteryID) const {
    L2_GUARD_FUNCTION();

    const auto found = Masteries.find(masteryID);
    if (found == Masteries.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]No mastery data: %d",
        //     L"PledgeMasteryInfo.cpp",
        //     0x143,
        //     static_cast<int>(masteryID))
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]No mastery data: %d",
        //     L"PledgeMasteryInfo.cpp",
        //     0x143,
        //     static_cast<int>(masteryID))
        L2_LOG_ERROR(
            Log, "No definition is registered for clan mastery %d; lookup returns no result.", static_cast<int>(masteryID)
        );
        return nullptr;
    }
    return found->second;
}

bool PledgeMasteryDB::HasPointStep(PledgeMasteryID::Type masteryID, int pointStep) const {
    L2_GUARD_FUNCTION();

    const PledgeMasteryInfo* mastery = FindInfo(masteryID);
    if (!mastery) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]No mastery data: %d",
        //     L"PledgeMasteryInfo.cpp",
        //     0x12c,
        //     static_cast<int>(masteryID))
        // Noise: FindInfo already identifies the missing mastery definition. The point-step check returns false.
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]No mastery data: %d",
        //     L"PledgeMasteryInfo.cpp",
        //     0x12c,
        //     static_cast<int>(masteryID))
        return false;
    }
    if (!mastery->IsValidPointStep(pointStep)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Invalid point step: %d, %d",
        //     L"PledgeMasteryInfo.cpp",
        //     0x132,
        //     static_cast<int>(masteryID),
        //     pointStep)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]Invalid point step: %d, %d",
        //     L"PledgeMasteryInfo.cpp",
        //     0x132,
        //     static_cast<int>(masteryID),
        //     pointStep)
        L2_LOG_WARN(
            Log, "Clan mastery %d has no point step %d; valid steps are 1 through %d.", static_cast<int>(masteryID),
            pointStep, mastery->GetMaxPointStep()
        );
        return false;
    }
    return true;
}

const PledgeCostInfo& PledgeMasteryDB::GetPointCost(PledgeMasteryID::Type masteryID, int pointStep) const {
    L2_GUARD_FUNCTION();

    const PledgeMasteryInfo* mastery = PledgeMasteryDB::inst().FindInfo(masteryID);
    if (!mastery) {
        return NullCost;
    }
    return mastery->GetCost(pointStep);
}

void PledgeMasteryDB::Load() {
    L2_GUARD_FUNCTION();

    constexpr const wchar_t* kFileName = L"..\\Script\\PledgeMasteryInfo.xml";
    XMLElement* root = CXmlLoader::inst().Load(kFileName);
    if (!root) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Failed to load : %s",
        //     L"PledgeMasteryInfo.cpp",
        //     0xf9,
        //     kFileName)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]Failed to load : %s",
        //     L"PledgeMasteryInfo.cpp",
        //     0xf9,
        //     kFileName)
        L2_LOG_ERROR(
            Log,
            "Cannot load PledgeMasteryInfo.xml: the XML loader returned no root; %llu definitions are currently held and "
            "reset settings are unchanged.",
            static_cast<unsigned long long>(Masteries.size())
        );
        return;
    }

    const XMLElement* resetElement = XMLUtil::FindChild(root, std::u16string(u"Reset"));
    Reset.LoadFromXML(resetElement);

    const std::vector<XMLElement*> masteries = XMLUtil::Select(root, std::u16string(u"Mastery"));
    for (const XMLElement* masteryElement : masteries) {
        PledgeMasteryInfo* mastery = new PledgeMasteryInfo;
        if (!mastery->LoadFromXML(masteryElement)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]Failed to load: %d",
            //     L"PledgeMasteryInfo.cpp",
            //     0x10d,
            //     static_cast<int>(Masteries.size()))
            // Noise: The definition loader already explains rejection. The registry size is not the XML record index. This
            // rejected definition is deleted and loading continues. L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]Failed to load: %d",
            //     L"PledgeMasteryInfo.cpp",
            //     0x10d,
            //     static_cast<int>(Masteries.size()))
            delete mastery;
            continue;
        }

        if (Masteries.find(mastery->id) != Masteries.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]Same mastery exists: %d",
            //     L"PledgeMasteryInfo.cpp",
            //     0x114,
            //     static_cast<int>(mastery->id))
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]Same mastery exists: %d",
            //     L"PledgeMasteryInfo.cpp",
            //     0x114,
            //     static_cast<int>(mastery->id))
            L2_LOG_ERROR(
                Log,
                "Duplicate clan mastery %d in PledgeMasteryInfo.xml; the existing definition is kept and the incoming "
                "definition is discarded.",
                static_cast<int>(mastery->id)
            );
            delete mastery;
            continue;
        }
        Masteries[mastery->id] = mastery;
    }

    root->ReleaseChild();
    delete root;
}

void PledgeMasteryDB::Builder::Reload(PledgeMasteryDB* database) {
    L2_GUARD_FUNCTION();

    database->Masteries.clear();
    database->Load();
}
