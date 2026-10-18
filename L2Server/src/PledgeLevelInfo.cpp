#include "PledgeLevelInfo.h"

#include "Shared/src/Log.h"
#include "XmlLoader.h"

bool PledgeLevelInfo::LoadFromXML(const XMLElement* element) {
    L2_GUARD_FUNCTION();

    if (!element) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeLevelInfo.cpp",
        //     0x15,
        //     L"(pElement != NULL)")
        // Log.Add(LOG_ERROR,
        //     L"[clan levels] Cannot load an entry from PledgeLevelInfo.xml: the XML element is missing.")
        L2_LOG_ERROR(Log, u"[clan levels] Cannot load an entry from PledgeLevelInfo.xml: the XML element is missing.");
        return false;
    }

    level = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"level"), 0);
    if (level == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Invalid ID",
        //     L"PledgeLevelInfo.cpp",
        //     0x1b)
        // Log.Add(LOG_ERROR,
        //     L"[clan levels] Rejecting an entry from PledgeLevelInfo.xml: the 'level' attribute resolved to zero.")
        L2_LOG_ERROR(
            Log, u"[clan levels] Rejecting an entry from PledgeLevelInfo.xml: the 'level' attribute resolved to zero."
        );
        return false;
    }

    const XMLElement* costElement = XMLUtil::FindChild(element, std::u16string(u"Cost"));
    if (!Cost.LoadFromXML(costElement)) {
        // Noise: PledgeCostInfo::LoadFromXML currently always returns true.
        // this failure message is unreachable. Keep the recovered branch.
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]No requirement : %d",
        //     L"PledgeLevelInfo.cpp",
        //     0x23,
        //     level)
        return false;
    }
    return true;
}

PledgeLevelDB::PledgeLevelDB() {}

PledgeLevelDB::~PledgeLevelDB() {
    for (const auto& level : Levels) {
        delete level.second;
    }
    Levels.clear();
}

void PledgeLevelDB::Load() {
    L2_GUARD_FUNCTION();

    constexpr const wchar_t* kFileName = L"..\\Script\\PledgeLevelInfo.xml";
    XMLElement* root = CXmlLoader::inst().Load(kFileName);
    if (!root) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Failed to load : %s",
        //     L"PledgeLevelInfo.cpp",
        //     0x46,
        //     kFileName)
        // Log.Add(LOG_ERROR,
        //     L"[clan levels] Cannot load PledgeLevelInfo.xml. Clan-level loading stopped.")
        L2_LOG_ERROR(Log, u"[clan levels] Cannot load PledgeLevelInfo.xml. Clan-level loading stopped.");
        return;
    }

    const std::vector<XMLElement*> levels = XMLUtil::Select(root, std::u16string(u"Level"));
    for (const XMLElement* element : levels) {
        PledgeLevelInfo* info = new PledgeLevelInfo;
        if (!info->LoadFromXML(element)) {
            // Noise: LoadFromXML already reports the rejected entry's cause.
            // the stored-level count is not its XML row number.
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]Failed to load: %d",
            //     L"PledgeLevelInfo.cpp",
            //     0x53,
            //     static_cast<int>(Levels.size()))
            delete info;
            continue;
        }
        Levels[info->level] = info;
    }

    root->ReleaseChild();
    delete root;
}

const PledgeLevelInfo* PledgeLevelDB::FindInfo(int level) const {
    L2_GUARD_FUNCTION();

    const auto found = Levels.find(level);
    if (found == Levels.end()) {
        return nullptr;
    }
    return found->second;
}

void PledgeLevelDB::Builder::Reload(PledgeLevelDB* database) {
    L2_GUARD_FUNCTION();

    database->Levels.clear();
    database->Load();
}
