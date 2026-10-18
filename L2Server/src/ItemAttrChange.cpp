#include "ItemAttrChange.h"
#include "Item.h"
#include "ItemAssembler.h"
#include "ItemAttrChangeParser.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/SharedFactory.h"
#include "User.h"
#include "UserSocket.h"

#include "Portable/ClientProtocolRuntime.h"
#include "Shared/src/Log.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/Utils.h"

CItemAttrChangeDB g_ItemAttrChangeDB;

CItemAttrChangeStruct::CItemAttrChangeStruct() {
    L2_GUARD_FUNCTION();

    m_nClassID = 0;
    InputAttrTypeList.clear();
    OutputAttrTypeList.clear();
}

CItemAttrChangeList::CItemAttrChangeList() {
    L2_GUARD_FUNCTION();

    m_nId = 0;
    m_mapItemAttrChangeInfo.clear();
}

void CItemAttrChangeList::AddList(CItemAttrChangeStruct* itemAttrChange) {
    L2_GUARD_FUNCTION();

    m_mapItemAttrChangeInfo.insert({itemAttrChange->m_nClassID, itemAttrChange});
}

bool CItemAttrChangeList::IsExist(int classId) {
    L2_GUARD_FUNCTION();

    return m_mapItemAttrChangeInfo.find(classId) != m_mapItemAttrChangeInfo.end();
}

bool CItemAttrChangeList::IsInputAttr(int classId, int attributeType) {
    L2_GUARD_FUNCTION();

    const auto found = m_mapItemAttrChangeInfo.find(classId);
    if (found == m_mapItemAttrChangeInfo.end()) {
        return false;
    }

    for (const AttributeTypeEnum value : found->second->InputAttrTypeList) {
        if (static_cast<int>(value) == attributeType) {
            return true;
        }
    }
    return false;
}

std::uint32_t CItemAttrChangeList::GetOutputAttr(int classId, int excludedAttributeType) {
    L2_GUARD_FUNCTION();

    const auto found = m_mapItemAttrChangeInfo.find(classId);
    if (found == m_mapItemAttrChangeInfo.end()) {
        return 0;
    }

    std::uint32_t result = 0;
    for (const AttributeTypeEnum value : found->second->OutputAttrTypeList) {
        if (static_cast<int>(value) == excludedAttributeType) {
            continue;
        }

        switch (value) {
            case ATTRT_FIRE:
                result |= 0x01;
                break;
            case ATTRT_WATER:
                result |= 0x02;
                break;
            case ATTRT_WIND:
                result |= 0x04;
                break;
            case ATTRT_EARTH:
                result |= 0x08;
                break;
            case ATTRT_HOLY:
                result |= 0x10;
                break;
            case ATTRT_UNHOLY:
                result |= 0x20;
                break;
            default:
                break;
        }
    }
    return result;
}

bool CItemAttrChangeList::IsOutputAttr(int classId, int attributeType) {
    L2_GUARD_FUNCTION();

    const auto found = m_mapItemAttrChangeInfo.find(classId);
    if (found == m_mapItemAttrChangeInfo.end()) {
        return false;
    }

    for (const AttributeTypeEnum value : found->second->OutputAttrTypeList) {
        if (static_cast<int>(value) == attributeType) {
            return true;
        }
    }
    return false;
}

CItemAttrChangeList* CItemAttrChangeDB::find(int id) {
    L2_GUARD_FUNCTION();

    const auto found = m_mapItemAttrChange.find(id);
    return found == m_mapItemAttrChange.end() ? nullptr : found->second;
}

void CItemAttrChangeDB::Add(CItemAttrChangeList* itemAttrChangeList) {
    m_mapItemAttrChange.insert({itemAttrChangeList->m_nId, itemAttrChangeList});
}

void CItemAttrChangeDB::Load() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\Itemattrchange.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/Itemattrchange.txt";
#endif

    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from ItemAttrChange file failed",
        //     L"ItemAttrChange.cpp",
        //     0xdf)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from ItemAttrChange file failed",
        //     L"ItemAttrChange.cpp",
        //     0xdf)
        L2_LOG_ERROR(Log, "Cannot read Itemattrchange.txt; attribute-change loading stops.");
        return;
    }

    CParserForItemAttrChange parser;
    if (!parser.create()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] objectParser.create failed",
        //     L"ItemAttrChange.cpp",
        //     0xe7)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] objectParser.create failed",
        //     L"ItemAttrChange.cpp",
        //     0xe7)
        L2_LOG_ERROR(Log, "Cannot initialize the Itemattrchange.txt parser; attribute-change loading stops.");
        return;
    }

    parser.m_lexer.yybegin(4);
    parser.m_lexer.yyin = input.GetStream();
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing ItemAttrChange file failed after line[%d]",
        //     L"ItemAttrChange.cpp",
        //     0xf3,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing ItemAttrChange file failed after line[%d]",
        //     L"ItemAttrChange.cpp",
        //     0xf3,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log,
            "Cannot parse Itemattrchange.txt at script line %lld; already inserted attribute-change entries are retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
}

void CItemAttrChangeDB::ItemAssemble(User* user, int id) {
    L2_GUARD_FUNCTION();

    if (user == nullptr) {
        return;
    }

    CItemAttrChangeList* const itemAttrChangeList = find(id);
    if (itemAttrChangeList == nullptr) {
        user->CheckAndResetPendingUseETCItem(PUIT_ATTRIBUTE_CHANGE);
        user->SendSystemMessage(SystemMessageId::ATTRIBUTE_CHANGE_FAILED);
        user->GetSocket()->Send("ch", 0xfe, 0x116);
        return;
    }

    CPackageItemPacketManager packetManager;
    {
        L2_READ_GUARD(user->m_inv);
        for (CSharedItemData* data = user->m_inv.GetFirst(); data != nullptr; data = data->Next()) {
            CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(data->index);
            if (!item || !item->IsWeapon()) {
                continue;
            }
            if (item->m_itemBaseAttribute.m_nAttackValue == 0) {
                continue;
            }
            if (!itemAttrChangeList->IsExist(item->m_nClassId)) {
                continue;
            }
            if (!itemAttrChangeList->IsInputAttr(item->m_nClassId, item->m_itemBaseAttribute.m_nAttackType)) {
                continue;
            }

            packetManager.CheckLimitPackageItemPacketDataSize();
            CItemAssembler itemAssembler(*user, *item.get(), false);
            const int assembled = packetManager.PackageAssembleItem(&itemAssembler);
            if (assembled < 1) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"Assembe Error at file[%s], line[%d], "
                //     L"assemble result = [%d], total length = [%d]",
                //     L"ItemAttrChange.cpp",
                //     0x134,
                //     assembled,
                //     packetManager.GetTotalPackageItemLength())
                // L2_LOG_ERROR(Log,
                //     L"Assembe Error at file[%s], line[%d], "
                //     L"assemble result = [%d], total length = [%d]",
                //     L"ItemAttrChange.cpp",
                //     0x134,
                //     assembled,
                //     packetManager.GetTotalPackageItemLength())
                L2_LOG_ERROR(
                    Log,
                    "Cannot assemble attribute-change list %d item class %d: result %d, accumulated packet length %d bytes; "
                    "this item is skipped.",
                    id, item->m_nClassId, assembled, packetManager.GetTotalPackageItemLength()
                );
                continue;
            }

            packetManager.SetAssembleItemLengthPerItem(assembled);
            packetManager.IncreasePackageItemCount();
        }
    }

    CUserSocket* const socket = user->GetSocket();
    socket->Send("chcdd", S_EX, S_EX_CHANGE_ATTRIBUTE_ITEM_LIST, 1, id, packetManager.GetTotalPackageItemCount());
    packetManager.SendPacket_PackageItemData(
        socket, S_EX, S_EX_CHANGE_ATTRIBUTE_ITEM_LIST, PackageItemPacket::PIPT_ITEM_INFO
    );
}
