#include "PledgeContributionInfo.h"

#include "PledgeStuff.h"
#include "Shared/src/Log.h"
#include "XmlLoader.h"

#include <algorithm>

bool PledgeRewardInfo::LoadFromXML(const XMLElement* element) {
    L2_GUARD_FUNCTION();

    if (!element) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x16,
        //     L"(pElement != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x16,
        //     L"(pElement != NULL)")
        L2_LOG_ERROR(Log, "PledgeContributionInfo.xml: missing Reward element; this record cannot be loaded.");
        return false;
    }

    item_class = PledgeStuff::GetAttributeAsClassID(element, std::u16string(u"item_class"));
    item_amount = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"item_amount"), 0);
    name_value = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"name_value"), 0);
    return true;
}

bool PledgeTotalGradeInfo::LoadFromXML(const XMLElement* element) {
    L2_GUARD_FUNCTION();

    if (!element) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x26,
        //     L"(pElement != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x26,
        //     L"(pElement != NULL)")
        L2_LOG_ERROR(
            Log, "PledgeContributionInfo.xml: missing TotalContribution/Grade element; this record cannot be loaded."
        );
        return false;
    }

    point = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"point"), 0);
    if (point <= 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Invalid point: %d",
        //     kSourceFile,
        //     0x2c,
        //     point)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]Invalid point: %d",
        //     kSourceFile,
        //     0x2c,
        //     point)
        L2_LOG_ERROR(
            Log,
            "PledgeContributionInfo.xml: total-contribution grade has point %d; a positive value is required and this "
            "record is rejected.",
            point
        );
        return false;
    }

    const XMLElement* reward = XMLUtil::FindChild(element, std::u16string(u"Reward"));
    if (!Reward.LoadFromXML(reward)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Failed to load: Reward",
        //     kSourceFile,
        //     0x33)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]Failed to load: Reward",
        //     kSourceFile,
        //     0x33)
        L2_LOG_ERROR(
            Log,
            "PledgeContributionInfo.xml: cannot load Reward for total-contribution grade (point %d); this record is "
            "rejected.",
            point
        );
        return false;
    }
    return true;
}

bool PledgePeriodRankInfo::LoadFromXML(const XMLElement* element) {
    L2_GUARD_FUNCTION();

    if (!element) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x41,
        //     L"(pElement != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x41,
        //     L"(pElement != NULL)")
        L2_LOG_ERROR(
            Log, "PledgeContributionInfo.xml: missing PeriodContribution/Ranks/Rank element; this record cannot be loaded."
        );
        return false;
    }

    rank = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"rank"), 0);
    if (rank <= 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Invalid rank: %d",
        //     kSourceFile,
        //     0x47,
        //     rank)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]Invalid rank: %d",
        //     kSourceFile,
        //     0x47,
        //     rank)
        L2_LOG_ERROR(
            Log,
            "PledgeContributionInfo.xml: period-contribution rank has rank %d; a positive value is required and this record "
            "is rejected.",
            rank
        );
        return false;
    }

    const XMLElement* reward = XMLUtil::FindChild(element, std::u16string(u"Reward"));
    if (!Reward.LoadFromXML(reward)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Failed to load: Reward",
        //     kSourceFile,
        //     0x4e)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]Failed to load: Reward",
        //     kSourceFile,
        //     0x4e)
        L2_LOG_ERROR(
            Log,
            "PledgeContributionInfo.xml: cannot load Reward for period-contribution rank (rank %d); this record is "
            "rejected.",
            rank
        );
        return false;
    }
    return true;
}

bool PledgePeriodGradeInfo::LoadFromXML(const XMLElement* element) {
    L2_GUARD_FUNCTION();

    if (!element) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x5c,
        //     L"(pElement != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x5c,
        //     L"(pElement != NULL)")
        L2_LOG_ERROR(
            Log, "PledgeContributionInfo.xml: missing PeriodContribution/Grades/Grade element; this record cannot be loaded."
        );
        return false;
    }

    ratio = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"ratio"), 0);
    if (ratio <= 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Invalid ratio: %d",
        //     kSourceFile,
        //     0x62,
        //     ratio)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]Invalid ratio: %d",
        //     kSourceFile,
        //     0x62,
        //     ratio)
        L2_LOG_ERROR(
            Log,
            "PledgeContributionInfo.xml: period-contribution grade has ratio %d; a positive value is required and this "
            "record is rejected.",
            ratio
        );
        return false;
    }

    const XMLElement* reward = XMLUtil::FindChild(element, std::u16string(u"Reward"));
    if (!Reward.LoadFromXML(reward)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Failed to load: Reward",
        //     kSourceFile,
        //     0x69)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]Failed to load: Reward",
        //     kSourceFile,
        //     0x69)
        L2_LOG_ERROR(
            Log,
            "PledgeContributionInfo.xml: cannot load Reward for period-contribution grade (ratio %d); this record is "
            "rejected.",
            ratio
        );
        return false;
    }
    return true;
}

bool PledgePeriodRewardWeightInfo::LoadFromXML(const XMLElement* element) {
    L2_GUARD_FUNCTION();

    if (!element) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x77,
        //     L"(pElement != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x77,
        //     L"(pElement != NULL)")
        L2_LOG_ERROR(
            Log, "PledgeContributionInfo.xml: missing PeriodContribution/Weight entry element; this record cannot be loaded."
        );
        return false;
    }

    member_count = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"member_count"), 0);
    item_factor = XMLUtil::GetAttributeValueAsFloat(element, std::u16string(u"item_factor"), 1.0F);
    namevalue_factor = XMLUtil::GetAttributeValueAsFloat(element, std::u16string(u"namevalue_factor"), 1.0F);
    return true;
}

PledgeContributionDB::PledgeContributionDB() :
    setting{{0, L2DateTime::NONE, 0}},
    min_member_count(0),
    min_point(0) {}

PledgeContributionDB::~PledgeContributionDB() {
    for (PledgeTotalGradeInfo* info : TotalGrades) {
        delete info;
    }
    TotalGrades.clear();

    for (PledgePeriodRankInfo* info : PeriodRanks) {
        delete info;
    }
    PeriodRanks.clear();

    for (PledgePeriodGradeInfo* info : PeriodGrades) {
        delete info;
    }
    PeriodGrades.clear();

    for (PledgePeriodRewardWeightInfo* info : PeriodRewardWeights) {
        delete info;
    }
    PeriodRewardWeights.clear();
}

const PledgeTotalGradeInfo* PledgeContributionDB::FindTotalGrade(int grade) const {
    L2_GUARD_FUNCTION();

    if (grade < 1 || grade > static_cast<int>(TotalGrades.size()) - 1) {
        return nullptr;
    }
    return runtime::CheckedVectorAt(TotalGrades, static_cast<std::size_t>(grade));
}

const PledgePeriodRankInfo* PledgeContributionDB::FindPeriodRank(int rank) const {
    L2_GUARD_FUNCTION();

    for (const PledgePeriodRankInfo* info : PeriodRanks) {
        if (info->rank == rank) {
            return info;
        }
    }
    return nullptr;
}

const PledgePeriodGradeInfo* PledgeContributionDB::FindPeriodGrade(int ratio) const {
    L2_GUARD_FUNCTION();

    if (PeriodGrades.empty()) {
        return nullptr;
    }
    for (const PledgePeriodGradeInfo* info : PeriodGrades) {
        if (ratio < info->ratio) {
            return info;
        }
    }
    return PeriodGrades.back();
}

const PledgePeriodRewardWeightInfo* PledgeContributionDB::FindPeriodWeight(int memberCount) const {
    L2_GUARD_FUNCTION();

    if (PeriodRewardWeights.empty()) {
        static const PledgePeriodRewardWeightInfo nullInfo;
        return &nullInfo;
    }
    for (const PledgePeriodRewardWeightInfo* info : PeriodRewardWeights) {
        if (memberCount <= info->member_count) {
            return info;
        }
    }
    return PeriodRewardWeights.back();
}

void PledgeContributionDB::LoadSetting(const XMLElement* element) {
    L2_GUARD_FUNCTION();

    if (!element) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0xb3,
        //     L"(pElement != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0xb3,
        //     L"(pElement != NULL)")
        L2_LOG_ERROR(Log, "PledgeContributionInfo.xml: missing Setting element; existing schedule settings are retained.");
        return;
    }

    const XMLElement* period = XMLUtil::FindChild(element, std::u16string(u"Period"));
    setting.period.hour = XMLUtil::GetAttributeValueAsInt(period, std::u16string(u"hour"), 0);
    setting.period.weekday =
        L2DateTime::ConvertDayOfWeek(XMLUtil::GetAttributeValue(period, std::u16string(u"weekday"), std::u16string(u"SUN")));
    setting.period.weeks = XMLUtil::GetAttributeValueAsInt(period, std::u16string(u"weeks"), -1);
}

void PledgeContributionDB::LoadTotalGrades(const XMLElement* element) {
    L2_GUARD_FUNCTION();

    if (!element) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0xc2,
        //     L"(pElement != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0xc2,
        //     L"(pElement != NULL)")
        L2_LOG_ERROR(Log, "PledgeContributionInfo.xml: missing TotalContribution element; no total grades are added.");
        return;
    }

    for (const XMLElement* child : element->m_vChild) {
        PledgeTotalGradeInfo* info = new PledgeTotalGradeInfo;
        if (!info->LoadFromXML(child)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]TotalContribution/Grade failed: %d",
            //     kSourceFile,
            //     0xcb,
            //     static_cast<int>(TotalGrades.size()))
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]TotalContribution/Grade failed: %d",
            //     kSourceFile,
            //     0xcb,
            //     static_cast<int>(TotalGrades.size()))
            L2_LOG_ERROR(
                Log,
                "PledgeContributionInfo.xml: invalid TotalContribution/Grade entry is discarded; %llu entries are currently "
                "held (including the grade-zero placeholder) and loading continues.",
                static_cast<unsigned long long>(TotalGrades.size())
            );
            delete info;
            continue;
        }
        TotalGrades.push_back(info);
    }
}

void PledgeContributionDB::LoadPeriodGrades(const XMLElement* element) {
    L2_GUARD_FUNCTION();

    if (!element) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0xea,
        //     L"(pElement != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0xea,
        //     L"(pElement != NULL)")
        L2_LOG_ERROR(
            Log, "PledgeContributionInfo.xml: missing PeriodContribution/Grades element; no period grades are added."
        );
        return;
    }

    for (const XMLElement* child : element->m_vChild) {
        PledgePeriodGradeInfo* info = new PledgePeriodGradeInfo;
        if (!info->LoadFromXML(child)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]PeriodContribution/Grade failed: %ul64d",
            //     kSourceFile,
            //     0xf3,
            //     static_cast<std::uint64_t>(PeriodGrades.size()))
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]PeriodContribution/Grade failed: %ul64d",
            //     kSourceFile,
            //     0xf3,
            //     static_cast<std::uint64_t>(PeriodGrades.size()))
            L2_LOG_ERROR(
                Log,
                "PledgeContributionInfo.xml: invalid PeriodContribution/Grades/Grade entry is discarded; %llu entries are "
                "currently held and loading continues.",
                static_cast<unsigned long long>(PeriodGrades.size())
            );
            delete info;
            continue;
        }
        PeriodGrades.push_back(info);
    }
}

void PledgeContributionDB::LoadPeriodRanks(const XMLElement* element) {
    L2_GUARD_FUNCTION();

    if (!element) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x102,
        //     L"(pElement != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x102,
        //     L"(pElement != NULL)")
        L2_LOG_ERROR(
            Log,
            "PledgeContributionInfo.xml: missing PeriodContribution/Ranks element; the member threshold is retained and no "
            "ranks are added."
        );
        return;
    }

    min_member_count = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"min_member_count"), 0);
    for (const XMLElement* child : element->m_vChild) {
        PledgePeriodRankInfo* info = new PledgePeriodRankInfo;
        if (!info->LoadFromXML(child)) {
            // The current source string says Grade even on the rank path.
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]PeriodContribution/Grade failed: %d",
            //     kSourceFile,
            //     0x10d,
            //     static_cast<int>(PeriodRanks.size()))
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]PeriodContribution/Grade failed: %d",
            //     kSourceFile,
            //     0x10d,
            //     static_cast<int>(PeriodRanks.size()))
            L2_LOG_ERROR(
                Log,
                "PledgeContributionInfo.xml: invalid PeriodContribution/Ranks/Rank entry is discarded; %llu entries are "
                "currently held and loading continues.",
                static_cast<unsigned long long>(PeriodRanks.size())
            );
            delete info;
            continue;
        }
        PeriodRanks.push_back(info);
    }
}

void PledgeContributionDB::LoadPeriodRewardWeights(const XMLElement* element) {
    L2_GUARD_FUNCTION();

    if (!element) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x11c,
        //     L"(pElement != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x11c,
        //     L"(pElement != NULL)")
        L2_LOG_ERROR(
            Log, "PledgeContributionInfo.xml: missing PeriodContribution/Weight element; no reward weights are added."
        );
        return;
    }

    for (const XMLElement* child : element->m_vChild) {
        PledgePeriodRewardWeightInfo* info = new PledgePeriodRewardWeightInfo;
        if (!info->LoadFromXML(child)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]PeriodContribution/Weight failed: %d",
            //     kSourceFile,
            //     0x125,
            //     static_cast<int>(PeriodRewardWeights.size()))
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]PeriodContribution/Weight failed: %d",
            //     kSourceFile,
            //     0x125,
            //     static_cast<int>(PeriodRewardWeights.size()))
            L2_LOG_ERROR(
                Log,
                "PledgeContributionInfo.xml: invalid PeriodContribution/Weight entry is discarded; %llu entries are "
                "currently held and loading continues.",
                static_cast<unsigned long long>(PeriodRewardWeights.size())
            );
            delete info;
            continue;
        }
        PeriodRewardWeights.push_back(info);
    }
}

void PledgeContributionDB::LoadPeriod(const XMLElement* element) {
    L2_GUARD_FUNCTION();

    if (!element) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0xda,
        //     L"(pElement != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0xda,
        //     L"(pElement != NULL)")
        L2_LOG_ERROR(
            Log,
            "PledgeContributionInfo.xml: missing PeriodContribution element; existing period settings and lists are "
            "retained."
        );
        return;
    }

    min_point = std::max(XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"min_point"), 0), 0);
    LoadPeriodRewardWeights(XMLUtil::FindChild(element, std::u16string(u"Weight")));
    LoadPeriodRanks(XMLUtil::FindChild(element, std::u16string(u"Ranks")));
    LoadPeriodGrades(XMLUtil::FindChild(element, std::u16string(u"Grades")));
}

void PledgeContributionDB::Load() {
    L2_GUARD_FUNCTION();

    constexpr const wchar_t* kFileName = L"..\\Script\\PledgeContributionInfo.xml";
    XMLElement* root = CXmlLoader::inst().Load(kFileName);
    if (!root) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Failed to load : %s",
        //     kSourceFile,
        //     0x9f,
        //     kFileName)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]Failed to load : %s",
        //     kSourceFile,
        //     0x9f,
        //     kFileName)
        L2_LOG_ERROR(Log, "Cannot load PledgeContributionInfo.xml; contribution definitions are not loaded by this call.");
        return;
    }

    // Grade zero is a real process-lifetime sentinel. The public lookup
    // accepts only indices 1..size-1.
    TotalGrades.push_back(new PledgeTotalGradeInfo);
    LoadSetting(XMLUtil::FindChild(root, std::u16string(u"Setting")));
    LoadTotalGrades(XMLUtil::FindChild(root, std::u16string(u"TotalContribution")));
    LoadPeriod(XMLUtil::FindChild(root, std::u16string(u"PeriodContribution")));

    root->ReleaseChild();
    delete root;
}

void PledgeContributionDB::Builder::Reload(PledgeContributionDB* database) {
    L2_GUARD_FUNCTION();

    // Current Reload resets only the four pointer ranges. It deliberately
    // does not delete their pointees before loading fresh XML records.
    database->TotalGrades.clear();
    database->PeriodRanks.clear();
    database->PeriodGrades.clear();
    database->PeriodRewardWeights.clear();
    database->Load();
}
