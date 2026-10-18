#include "Shared/src/Log.h"
#include "Shared/src/MemorySub.h"

#include "SignBoard.h"

#include "CWorld.h"
#include "HTMLCache.h"
#include "Item.h"
#include "Protocol/src/L2Constants.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/SharedFactory.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Exception.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <limits>
#include <string>
#include <string_view>

namespace {

constexpr std::size_t kHtmlBufferCapacity = 0x2800;
constexpr std::size_t kHtmlRowCapacity = 1000;

constexpr const char16_t* kHtmlHeader = u"<html><body>&$650;<br><br>";
constexpr const char16_t* kHtmlFooter = u"<br></body></html>";
constexpr const char16_t* kObserverGroupRow = u"<button value = \"&$%d;\" action=\"bypass -h "
                                              u"observer_group_%d_%d\"back=\"l2ui_ct1.button."
                                              u"button_df_small_down\" width=140 height=25 "
                                              u"fore=\"l2ui_ct1.button.button_df_small\" /><br1>";
constexpr const char16_t* kObserverPositionRow = u"<button value = \"&$%d; - %d &$469;\" action=\"bypass -h "
                                                 u"observer_%d_%d\" back=\"l2ui_ct1.button."
                                                 u"button_df_small_down\" width=140 height=25 "
                                                 u"fore=\"l2ui_ct1.button.button_df_small\" /><br1>";

// Portable spelling of the current fixed 0x2800-unit wcscat_s calls. The
// target rows fit this buffer. The failure branch mirrors the secure CRT's
// cleared destination without inventing a larger dynamic carrier.
template <std::size_t N>
bool AppendText(std::array<char16_t, N>& destination, const char16_t* source) noexcept {
    using Traits = std::char_traits<char16_t>;
    const std::size_t destinationLength = Traits::length(destination.data());
    const std::size_t sourceLength = Traits::length(source);
    if (sourceLength >= N - destinationLength) {
        destination[0] = u'\0';
        return false;
    }
    Traits::copy(destination.data() + destinationLength, source, sourceLength + 1);
    return true;
}

std::int32_t LowSignedInt32(std::int64_t value) noexcept {
    const std::uint32_t low = static_cast<std::uint32_t>(value);
    std::int32_t result = 0;
    std::memcpy(&result, &low, sizeof(result));
    return result;
}

[[noreturn]] void PublishAddObserverPosCrash(const std::source_location location = std::source_location::current()) {
    runtime::process::FailCheckSizeInt(location);
}

} // namespace

CSignBoardDb g_SignBoardDb;

CSignBoard* CSignBoardDb::Find(const char16_t* const name) {
    const std::u16string key(name != nullptr ? name : u"");
    const auto found = m_SignBoardNameMap.find(key);
    return found != m_SignBoardNameMap.end() ? found->second : nullptr;
}

void CSignBoardDb::Add(const char16_t* const name, CSignBoard* const signBoard) {
    // Current L2Server 0x00c80fe0 only assigns the name map. Unlike the
    // older TotalChecker owner, it does not register a pending object here.
    m_SignBoardNameMap[std::u16string(name != nullptr ? name : u"")] = signBoard;
}

CObserverRemains::CObserverRemains() {
    for (ItemOptionKey& key : optionKey) {
        key.variationOption1 = 0;
        key.variationOption2 = 0;
    }
    CStaticObject::OnCreate();
}

CObserverRemains::~CObserverRemains() { g_ServerContext.ObserverRemains.fetch_sub(1, std::memory_order_relaxed); }

L2_DEFINE_OBJECT_CLASS_ID(CObserverRemains)

L2_DEFINE_OBJECT_CLASS_NAME(CObserverRemains, m_pClassName_CObserverRemains)

L2_DEFINE_OBJECT_ALLOCATOR(CObserverRemains, __GetClassName())

L2_DEFINE_OBJECT_NEW(CObserverRemains)

L2_DEFINE_OBJECT_TYPED_RELEASE(CObserverRemains)

bool CObserverRemains::IsObserverRemains() { return true; }

CObserverRemains* CObserverRemains::CastObserverRemains() { return this; }

void CObserverRemains::RemoveRemains() {
    L2_GUARD_FUNCTION();
    CWorld::Inst().StaticObjectDisappear(this);
}

void CObserverRemains::SendObserverRemainsInfo(CUserSocket* socket) {
    L2_GUARD_FUNCTION();
    User* const receiver = socket->GetUser();
    if (receiver == nullptr) {
        return;
    }
    const FVector positionZ = GetPos();
    const FVector positionY = GetPos();
    const FVector positionX = GetPos();
    socket->Send(
        "ccdddddShcdddddddddddddddddddcdddddddddcdddhhhhhhhhffffdddSddddccccccchbcchdddccdcccdddddchdcdddcdddddcdbccc", 0x31,
        0, runtime::Win64CvttDoubleToInt32(positionX.x), runtime::Win64CvttDoubleToInt32(positionY.y),
        runtime::Win64CvttDoubleToInt32(positionZ.z), 0, static_cast<std::int32_t>(m_nId), sName,
        static_cast<int>(static_cast<std::int16_t>(nRace)), static_cast<int>(static_cast<std::int8_t>(nSex)), nClass,
        nSlotItemClassID[0], nSlotItemClassID[6], nSlotItemClassID[7], nSlotItemClassID[8], nSlotItemClassID[9],
        nSlotItemClassID[10], nSlotItemClassID[11], nSlotItemClassID[12], nSlotItemClassID[13], nSlotItemClassID[14],
        nSlotItemClassID[16], nSlotItemClassID[18], optionKey[7].variationOption1, optionKey[7].variationOption2,
        optionKey[8].variationOption1, optionKey[8].variationOption2, optionKey[14].variationOption1,
        optionKey[14].variationOption2,
        static_cast<int>(static_cast<std::int8_t>(receiver->m_hMinNewSetItemEnchantedEffect & 0xffU)),
        nSlotItemShapeShiftClassID[7], nSlotItemShapeShiftClassID[8], nSlotItemShapeShiftClassID[14],
        nSlotItemShapeShiftClassID[9], nSlotItemShapeShiftClassID[10], nSlotItemShapeShiftClassID[11],
        nSlotItemShapeShiftClassID[12], nSlotItemShapeShiftClassID[16], nSlotItemShapeShiftClassID[18], 0, nCriminalRate, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 1.0, 1.0, dCollisionRadius, dCollisionHeight, nFace, nHairShape, nHairColor, u"",
        nPledgeSId, nPledgeCrestId, nAllianceId, nAllianceCrestId, 0, 0, 0, 0, 0, 0, 7, 0, 0, "", 0, 0, 0, 0, nNowClass, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0x00ffffff, 0, 0, 0, DEFAULT_NICKNAME_COLOR, 0, 0, 0, 0, 0,
        runtime::Win64CvttDoubleToInt32(receiver->m_data->m_dCP),
        runtime::Win64CvttDoubleToInt32(receiver->m_data->m_dHP),
        runtime::Win64CvttDoubleToInt32(receiver->m_data->m_dBaseHP),
        runtime::Win64CvttDoubleToInt32(receiver->m_data->m_dMP),
        runtime::Win64CvttDoubleToInt32(receiver->m_data->m_dBaseMP), 0, 0, 0, "", 0, receiver->m_bHairAccFlag != 0, 0
    );
}

void CObserverRemains::SetUser(User* user) {
    L2_GUARD_FUNCTION();

    const std::u16string targetName = ::std::u16string(user->GetName());
    const std::size_t copiedNameLength = std::min(targetName.size(), std::size(sName) - 1U);
    std::copy_n(targetName.data(), copiedNameLength, sName);
    sName[copiedNameLength] = u'\0';

    nRace = static_cast<std::int32_t>(user->m_data->m_Race);
    nSex = static_cast<std::int32_t>(user->m_data->m_Sex);
    nClass = user->m_data->m_nSubJob0Class;
    nNowClass = static_cast<std::int32_t>(user->m_data->m_nClass);
    nFace = user->m_data->m_nFaceEx != 0 ? user->m_data->m_nFaceEx : user->m_data->m_nFace;
    nHairShape = user->m_data->m_nHairShapeEx != 0 ? user->m_data->m_nHairShapeEx : user->m_data->m_nHairShape;
    nHairColor = user->m_data->m_nHairColorEx != 0 ? user->m_data->m_nHairColorEx : user->m_data->m_nHairColor;
    nCriminalRate = user->CalcCriminalRate();

    CItemFactory& itemFactory = CItemFactory::Inst();
    for (int slot = 0; slot < 63; ++slot) {
        if (slot >= 1 && slot <= 5) {
            nSlotItemClassID[slot] = 0;
            nSlotItemShapeShiftClassID[slot] = 0;
            continue;
        }

        const int itemIndex = user->m_data->m_nSlotIndex[slot];
        CSharedItemData* const itemData = itemFactory.GetData(itemIndex);
        if (itemData == nullptr) {
            nSlotItemClassID[slot] = 0;
            nSlotItemShapeShiftClassID[slot] = 0;
            continue;
        }

        nSlotItemClassID[slot] = itemData->m_nClassID;
        CItem* const item = itemFactory.GetObjectW(itemIndex);
        if (item == nullptr) {
            nSlotItemShapeShiftClassID[slot] = 0;
            continue;
        }

        optionKey[slot] = item->m_itemOption.itemOptionKey;
        nSlotItemShapeShiftClassID[slot] = item->m_data->GetShapeShiftedClassId();
    }

    nPledgeId = static_cast<std::int32_t>(user->m_data->m_nPledgeID);
    nPledgeSId = static_cast<std::int32_t>(user->m_nPledgeSid);
    nPledgeCrestId = static_cast<std::int32_t>(user->m_nPledgeCrestDbId);
    nAllianceId = static_cast<std::int32_t>(user->m_data->m_nAllianceId);
    nAllianceCrestId = static_cast<std::int32_t>(user->m_nAllianceCrestDbId);
    dCollisionRadius = user->m_data->m_dCollisionRadius;
    dCollisionHeight = user->m_data->m_dCollisionHeight;

    SetPos(user->GetPos());
    CWorld::Inst().StaticObjectAppear(this, GetPos());
}

CSignBoard::CSignBoard() : m_eAct(SBACT_NONE), m_nDisplayNPCID(0) {
    CStaticObject::OnCreate();
}

CSignBoard::~CSignBoard() {
    m_szHTML = nullptr;
    m_szTextureName = nullptr;
    g_ServerContext.SignBoard.fetch_sub(1, std::memory_order_relaxed);
}

L2_DEFINE_OBJECT_CLASS_ID(CSignBoard)

L2_DEFINE_OBJECT_CLASS_NAME(CSignBoard, m_pClassName_CSignBoard)

L2_DEFINE_OBJECT_ALLOCATOR(CSignBoard, __GetClassName())

L2_DEFINE_OBJECT_NEW(CSignBoard)

L2_DEFINE_OBJECT_TYPED_RELEASE(CSignBoard)

bool CSignBoard::IsSignBoard() { return true; }

CSignBoard* CSignBoard::CastSignBoard() { return this; }

bool CSignBoard::IsAlive() { return true; }

void CSignBoard::DecideDistance(CMoveToTargetShareData* data, CSmartID creatureId) {
    static_cast<void>(creatureId);
    data->m_nDistance = 200;
    data->m_nAcceptableDistance = 200;
}

void CSignBoard::SendSignBoardInfo(CUserSocket* socket) {
    if (!socket) {
        return;
    }

    if (m_nDisplayNPCID == 0) {
        socket->Send("cddddddddddd", 0x9f, m_data->m_nEditorID, static_cast<int>(m_nId), 0, 1, 0, 0, 0, 0, 0, 0, 0);
        return;
    }

    // Current evaluation order calls GetPos separately for z, y and x.
    const std::int32_t z = runtime::Win64CvttDoubleToInt32(GetPos().z);
    const std::int32_t y = runtime::Win64CvttDoubleToInt32(GetPos().y);
    const std::int32_t x = runtime::Win64CvttDoubleToInt32(GetPos().x);
    socket->Send(
        "cddSdddddffffdddd", 0x92, static_cast<int>(m_nId), m_nDisplayNPCID, u"", 0, x, y, z, 0, 1.0, 1.0, 30.0, 30.0, 0, 0,
        4, 0
    );
}

void CSignBoard::Interact(CCreature* creature) {
    CObject* const object = creature;
    if (m_eAct == SBACT_HTML) {
        if (object->IsPC()) {
            if (!m_szHTML) {
                // Portable narrow transport of the exact current wide log.
                // Log.Add(
                //     LOG_ERROR,
                //     "[%s][%d] html file name is null",
                //     "SignBoard.cpp",
                //     0x12a)
                // L2_LOG_ERROR(Log,
                //     "[%s][%d] html file name is null",
                //     "SignBoard.cpp",
                //     0x12a)
                L2_LOG_ERROR(
                    Log, "Signboard %p has no HTML filename; interaction still calls the HTML cache with a null filename.",
                    static_cast<void*>(this)
                );
            }
            const std::wstring htmlFileName = Utf16ToWide(m_szHTML);
            const wchar_t* const fileName = m_szHTML != nullptr ? htmlFileName.c_str() : nullptr;
            User* const cacheUser = object->CastUser();
            std::u16string html;
            if (g_HTMLCache.GetHTMLFile(WideToUtf16(fileName).c_str(), html, cacheUser->m_data->m_CountryCode)) {
                const std::u16string hostHtml(html);
                object->CastUser()->ShowHTML(WideToUtf16(fileName).c_str(), hostHtml.c_str(), 0, HTML::LAUNCH, true);
            }
        }
        return;
    }

    if (m_eAct == SBACT_TOWNMAP) {
        if (object->IsPC()) {
            User* const user = object->CastUser();
            user->GetSocket()->Send("cSdd", 0xea, m_szTextureName, m_nMapX, m_nMapY);
        }
        return;
    }

    if (m_eAct != SBACT_OBSERVER) {
        return;
    }

    std::array<char16_t, kHtmlBufferCapacity> html{};
    std::array<char16_t, kHtmlRowCapacity> row{};
    FormatUtf16To(html.data(), html.size(), u"%ls", kHtmlHeader);
    for (std::size_t index = 0; index < m_vObserverGroup.size(); ++index) {
        FormatUtf16To(
            row.data(), row.size(), kObserverGroupRow, runtime::CheckedVectorAt(m_vObserverGroup, index),
            static_cast<std::int32_t>(m_nId), static_cast<std::int32_t>(index)
        );
        AppendText(html, row.data());
    }
    AppendText(html, kHtmlFooter);

    User* const user = object->CastUser();
    TalkChannelCache::inst().mChannels[5].Cache(user->Id(), static_cast<std::uint32_t>(m_nId), 0);
    user->ShowHTML(nullptr, html.data(), 0, HTML::LAUNCH, true);
}

void CSignBoard::SendObserverListHTML(User* user, int group) {
    L2_GUARD_FUNCTION();

    std::array<char16_t, kHtmlBufferCapacity> html{};
    std::array<char16_t, kHtmlRowCapacity> row{};
    FormatUtf16To(html.data(), html.size(), u"%ls", kHtmlHeader);
    for (std::size_t index = 0; index < m_vObserverPosList.size(); ++index) {
        const ObserverPos& observer = runtime::CheckedVectorAt(m_vObserverPosList, index);
        if (observer.nGroup != group) {
            continue;
        }
        FormatUtf16To(
            row.data(), row.size(), kObserverPositionRow, observer.nNameID, LowSignedInt32(observer.price),
            static_cast<std::int32_t>(m_nId), static_cast<std::int32_t>(index)
        );
        AppendText(html, row.data());
    }
    AppendText(html, kHtmlFooter);

    TalkChannelCache::inst().mChannels[5].Cache(user->Id(), static_cast<std::uint32_t>(m_nId), 0);
    user->ShowHTML(nullptr, html.data(), 0, HTML::LAUNCH, true);
}

void CSignBoard::AddObserverPos(ObserverPos observerPos) {
    const std::size_t groupCount = m_vObserverGroup.size();
    if (groupCount == 0 || groupCount - 1 > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        PublishAddObserverPosCrash();
    }
    observerPos.nGroup = static_cast<std::int32_t>(groupCount - 1);
    m_vObserverPosList.push_back(observerPos);
}
