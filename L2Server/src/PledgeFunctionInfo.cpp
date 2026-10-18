#include "PledgeFunctionInfo.h"

#include "Shared/src/Log.h"
#include "XmlLoader.h"

bool PledgeMemberCountInfo::LoadFromXML(const XMLElement* element) {
    L2_GUARD_FUNCTION();

    total_count = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"total_count"), 0);
    elite_count = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"elite_count"), 0);
    return true;
}

PledgeFunctionInfo::PledgeFunctionInfo() : type(PledgeFunctionType::NONE), MemberCount{} {}

bool PledgeFunctionInfo::LoadFromXML(const XMLElement* element) {
    L2_GUARD_FUNCTION();

    if (!element) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeFunctionInfo.cpp",
        //     0x26,
        //     L"(pElement != NULL)")
        // Log.Add(LOG_ERROR,
        //     L"[clan functions] Cannot load an entry from PledgeFunctionInfo.xml: the XML element is missing.")
        L2_LOG_ERROR(Log, u"[clan functions] Cannot load an entry from PledgeFunctionInfo.xml: the XML element is missing.");
        return false;
    }

    type = PledgeStuff::GetAttributeValueAsFunctionType(element, std::u16string(u"type"), PledgeFunctionType::NONE);

    const XMLElement* memberCount = XMLUtil::FindChild(element, std::u16string(u"MemberCount"));
    if (!MemberCount.LoadFromXML(memberCount)) {
        // Noise: PledgeMemberCountInfo::LoadFromXML currently always returns
        // true. This failure message is unreachable. Keep the recovered branch.
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]No condition : %d",
        //     L"PledgeFunctionInfo.cpp",
        //     0x2e,
        //     static_cast<int>(type))
        return false;
    }

    const XMLElement* condition = XMLUtil::FindChild(element, std::u16string(u"Condition"));
    if (!Condition.LoadFromXML(condition)) {
        // Noise: PledgeConditionInfo::LoadFromXML currently always returns
        // true. This failure message is unreachable. Keep the recovered branch.
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]No condition : %d",
        //     L"PledgeFunctionInfo.cpp",
        //     0x37,
        //     static_cast<int>(type))
        return false;
    }
    return true;
}

PledgeFunctionDB::PledgeFunctionDB() {}

PledgeFunctionDB::~PledgeFunctionDB() {
    for (PledgeFunctionInfo* function : Functions) {
        delete function;
    }
    Functions.clear();
}

const std::vector<PledgeFunctionInfo*>& PledgeFunctionDB::GetInfos() const {
    L2_GUARD_FUNCTION();
    return Functions;
}

void PledgeFunctionDB::Load() {
    L2_GUARD_FUNCTION();

    constexpr const wchar_t* kFileName = L"..\\Script\\PledgeFunctionInfo.xml";
    XMLElement* root = CXmlLoader::inst().Load(kFileName);
    if (!root) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Failed to load : %s",
        //     L"PledgeFunctionInfo.cpp",
        //     0x59,
        //     kFileName)
        // Log.Add(LOG_ERROR,
        //     L"[clan functions] Cannot load PledgeFunctionInfo.xml. Clan-function loading stopped.")
        L2_LOG_ERROR(Log, u"[clan functions] Cannot load PledgeFunctionInfo.xml. Clan-function loading stopped.");
        return;
    }

    for (XMLElement* element : root->m_vChild) {
        PledgeFunctionInfo* function = new PledgeFunctionInfo;
        if (!function->LoadFromXML(element)) {
            // Noise: LoadFromXML already reports the rejected entry's cause.
            // the stored-function count is not its XML row number.
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]Function failed: %d",
            //     L"PledgeFunctionInfo.cpp",
            //     100,
            //     static_cast<int>(Functions.size()))
            delete function;
        } else {
            Functions.push_back(function);
        }
    }

    root->ReleaseChild();
    delete root;
}

void PledgeFunctionDB::Builder::Reload(PledgeFunctionDB* database) {
    L2_GUARD_FUNCTION();

    // The current body discards only the pointer range and therefore leaks
    // the formerly referenced records. Preserve that reload behavior.
    database->Functions.clear();
    database->Load();
}
