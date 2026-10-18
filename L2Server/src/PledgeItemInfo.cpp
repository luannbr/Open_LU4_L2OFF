#include "PledgeItemInfo.h"

#include "PledgeStuff.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/StandardContainers.h"
#include "XmlLoader.h"

#include <algorithm>
#include <cstdint>

PledgeItemInfo::PledgeItemInfo() :
    class_id(-1), add_type(PledgeAddType::MANUAL), expiry_time(0) {}

bool PledgeItemRefill::LoadFromXML(const XMLElement* element) {
    L2_GUARD_FUNCTION();

    interval = std::max(
        runtime::MultiplyInt32Wrapping(XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"interval"), 0), 60), 0
    );
    amount = std::max(XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"amount"), 0), 0);
    return true;
}

bool PledgeItemInfo::LoadFromXML(const XMLElement* element) {
    L2_GUARD_FUNCTION();

    if (!element) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x2b,
        //     L"(pElement != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x2b,
        //     L"(pElement != NULL)")
        L2_LOG_ERROR(Log, "PledgeItemInfo.xml: missing item element; the record cannot be loaded.");
        return false;
    }

    class_id = PledgeStuff::GetAttributeAsClassID(element, std::u16string(u"class_id"));
    add_type = PledgeStuff::GetAttributeValueAsAddType(element, std::u16string(u"add_type"), PledgeAddType::MANUAL);
    expiry_time =
        runtime::MultiplyInt32Wrapping(XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"expiry_time"), 0), 3600);

    if (!Condition.LoadFromXML(XMLUtil::FindChild(element, std::u16string(u"Condition")))) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]No condition : %d",
        //     kSourceFile,
        //     0x35,
        //     class_id)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]No condition : %d",
        //     kSourceFile,
        //     0x35,
        //     class_id)
        L2_LOG_ERROR(
            Log, "PledgeItemInfo.xml: Condition loader rejected item class %d; this record is discarded.", class_id
        );
        return false;
    }
    if (!ActiveCost.LoadFromXML(XMLUtil::FindChild(element, std::u16string(u"ActiveCost")))) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]No ActiveCost : %d",
        //     kSourceFile,
        //     0x3e,
        //     class_id)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]No ActiveCost : %d",
        //     kSourceFile,
        //     0x3e,
        //     class_id)
        L2_LOG_ERROR(
            Log, "PledgeItemInfo.xml: ActiveCost loader rejected item class %d; this record is discarded.", class_id
        );
        return false;
    }
    if (!SellCost.LoadFromXML(XMLUtil::FindChild(element, std::u16string(u"SellCost")))) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]No SellCost : %d",
        //     kSourceFile,
        //     0x47,
        //     class_id)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]No SellCost : %d",
        //     kSourceFile,
        //     0x47,
        //     class_id)
        L2_LOG_ERROR(Log, "PledgeItemInfo.xml: SellCost loader rejected item class %d; this record is discarded.", class_id);
        return false;
    }
    if (!Refill.LoadFromXML(XMLUtil::FindChild(element, std::u16string(u"Refill")))) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]No Refill : %d",
        //     kSourceFile,
        //     0x50,
        //     class_id)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]No Refill : %d",
        //     kSourceFile,
        //     0x50,
        //     class_id)
        L2_LOG_ERROR(Log, "PledgeItemInfo.xml: Refill loader rejected item class %d; this record is discarded.", class_id);
        return false;
    }
    return true;
}

const PledgeItemInfo* PledgeItemIndexer::Find(int classId) const {
    L2_GUARD_FUNCTION();

    const auto position = m_items.find(classId);
    return position == m_items.end() ? nullptr : position->second;
}

void PledgeItemIndexer::Setup(const std::vector<PledgeItemInfo*>& items) {
    L2_GUARD_FUNCTION();

    m_items.clear();
    for (PledgeItemInfo* info : items) {
        m_items[info->class_id] = info;
    }
}

PledgeItemDB::PledgeItemDB() : m_Indexer() {}

PledgeItemDB::~PledgeItemDB() {
    for (PledgeItemInfo* info : Items) {
        delete info;
    }
    Items.clear();
}

const std::vector<PledgeItemInfo*>& PledgeItemDB::GetInfos() const {
    L2_GUARD_FUNCTION();
    return Items;
}

const PledgeItemInfo* PledgeItemDB::Find(int classId) const { return m_Indexer.Find(classId); }

void PledgeItemDB::MakeIndexer() {
    L2_GUARD_FUNCTION();
    m_Indexer.Setup(GetInfos());
}

void PledgeItemDB::Load() {
    L2_GUARD_FUNCTION();

    constexpr const wchar_t* kFileName = L"..\\Script\\PledgeItemInfo.xml";
    XMLElement* root = CXmlLoader::inst().Load(kFileName);
    if (!root) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Failed to load : %s",
        //     kSourceFile,
        //     0xbc,
        //     kFileName)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]Failed to load : %s",
        //     kSourceFile,
        //     0xbc,
        //     kFileName)
        L2_LOG_ERROR(Log, "Cannot load PledgeItemInfo.xml; the item index is not rebuilt.");
        return;
    }

    for (const XMLElement* child : root->m_vChild) {
        PledgeItemInfo* info = new PledgeItemInfo;
        if (!info->LoadFromXML(child)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]Item failed: %d",
            //     kSourceFile,
            //     0xc7,
            //     static_cast<int>(Items.size()))
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]Item failed: %d",
            //     kSourceFile,
            //     0xc7,
            //     static_cast<int>(Items.size()))
            L2_LOG_ERROR(
                Log,
                "PledgeItemInfo.xml: invalid item entry is discarded; %llu definitions are currently held and loading "
                "continues.",
                static_cast<unsigned long long>(Items.size())
            );
            delete info;
            continue;
        }
        Items.push_back(info);
    }

    root->ReleaseChild();
    delete root;
    MakeIndexer();
}

void PledgeItemDB::Builder::Reload(PledgeItemDB* database) {
    L2_GUARD_FUNCTION();

    // Current Reload clears the pointer range without deleting the old
    // process-owned records, then Load rebuilds the index from the new range.
    database->Items.clear();
    database->Load();
}
