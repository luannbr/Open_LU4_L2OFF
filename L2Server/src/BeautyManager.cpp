#include "BeautyManager.h"
#include "DB.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Setting.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TemplateHelper.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

char kBeautyManagerSourceFile[] = "BeautyManager.cpp";

} // namespace

class CBeautyFaceList final : public CObjectMapFunc {
public:
    explicit CBeautyFaceList(const bool includePrice) : m_bPrice(includePrice) {}

    bool Do(MemoryObject* object) override;

private:
    bool m_bPrice;
};

class CBeautyHairList final : public CObjectMapFunc {
public:
    explicit CBeautyHairList(const bool includePrice) : m_bPrice(includePrice) {}

    bool Do(MemoryObject* object) override;

private:
    bool m_bPrice;
};

class CBeautyColorList final : public CObjectMapFunc {
public:
    explicit CBeautyColorList(const bool includePrice) : m_bPrice(includePrice) {}

    bool Do(MemoryObject* object) override;

private:
    bool m_bPrice;
};


bool CBeautyFaceList::Do(MemoryObject* const object) {
    CBeautyManager::Inst()->GetData(static_cast<BeautyFace*>(object)->m_Data, m_bPrice);
    return true;
}

bool CBeautyHairList::Do(MemoryObject* const object) {
    CBeautyManager::Inst()->GetData(static_cast<BeautyHair*>(object)->m_Data, m_bPrice);
    return true;
}

bool CBeautyColorList::Do(MemoryObject* const object) {
    CBeautyManager::Inst()->GetData(static_cast<BeautyColor*>(object)->m_Data, m_bPrice);
    return true;
}

namespace {


template <typename TObject, typename TObjectMap>
void InsertBeautyData(
    TObjectMap& objects, const CBeautyManager::BeautyKey key, const CBeautyData& data, const int insertLine
) {
    TObject* const object = new TObject(data);
    objects.Insert(key, object, kBeautyManagerSourceFile, insertLine);
    L2_RELEASE(object, ObjectRefType::CreateDelete);
}

} // namespace

BeautyFace::BeautyFace(const CBeautyData& data) : m_Data(data) {}

L2_DEFINE_OBJECT_TYPE_FULL_OUT_OF_LINE(BeautyFace)

BeautyHair::BeautyHair(const CBeautyData& data) : m_Data(data) {}

L2_DEFINE_OBJECT_TYPE_FULL_OUT_OF_LINE(BeautyHair)

BeautyColor::BeautyColor(const CBeautyData& data) : m_Data(data) {}

L2_DEFINE_OBJECT_TYPE_FULL_OUT_OF_LINE(BeautyColor)

CBeautyManager::CBeautyManager() :
    m_bInit(0), m_packetHelper(), m_lock() {}

CBeautyManager::~CBeautyManager() = default;

CBeautyManager* CBeautyManager::Inst() {
    static CBeautyManager instance;
    return &instance;
}

L2_DEFINE_OBJECT_TYPE_FULL_OUT_OF_LINE(CBeautyManager)

void CBeautyManager::Init() {
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::ExchangeI32(&m_bInit, 1);

    CPacketHelperEx<20480> packet;
    for (std::int32_t state = Beauty::HAIR; state < Beauty::STATEMAX; ++state) {
        CPacketHelperEx<20480> groupPacket;
        int count = 0;
        Singleton<CSetting>::inst().SendBeautyList(static_cast<Beauty::BeautyState>(state), groupPacket, count);
        packet.Assemble("ddb", state, count, groupPacket.GetLength(), groupPacket.GetPacket());
    }
    db.RequestBeautyLoad(Beauty::STATEMAX, packet);
}

void CBeautyManager::LoadData(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t groupCount = 0;
    packet = ::Disassemble(packet, "d", &groupCount);
    for (std::int32_t group = 0; group < groupCount; ++group) {
        std::int32_t stateValue = 0;
        std::int32_t count = 0;
        packet = ::Disassemble(packet, "dd", &stateValue, &count);
        const auto state = static_cast<Beauty::BeautyState>(stateValue);
        for (std::int32_t index = 0; index < count; ++index) {
            std::int32_t primary = 0;
            std::int32_t secondary = 0;
            CBeautyData data{};
            packet = ::Disassemble(
                packet, "ddQQQdd", &primary, &secondary, &data.m_nAdena, &data.m_nResetAdena, &data.m_nCoin, &data.m_nCount,
                &data.m_nEnable
            );
            data.m_nUpperID = state == Beauty::COLOR ? primary : 0;
            data.m_nID = secondary;
            const BeautyKey key(primary, secondary);
            if (state == Beauty::HAIR) {
                InsertBeautyData<BeautyHair>(m_vHairList, key, data, 0x4d);
            } else if (state == Beauty::FACE) {
                InsertBeautyData<BeautyFace>(m_vFaceList, key, data, 0x53);
            } else if (state == Beauty::COLOR) {
                InsertBeautyData<BeautyColor>(m_vColorList, key, data, 0x59);
            }
        }
    }
    runtime::raw_atomic::ExchangeI32(&m_bInit, 0);
}

void CBeautyManager::IncrementBeautyItem(const Beauty::BeautyState state, const int secondary, const int primary) {
    // Current L2Server 0x004b463c.
    L2_GUARD_FUNCTION();
    const BeautyKey key(primary, secondary);
    if (state == Beauty::HAIR) {
        CSPointer<BeautyHair> beauty = m_vHairList.FindObject(key);
        if (beauty) {
            beauty->m_Data.m_nCount = runtime::SubtractInt32Wrapping(beauty->m_Data.m_nCount, 1);
        }
    } else if (state == Beauty::FACE) {
        CSPointer<BeautyFace> beauty = m_vFaceList.FindObject(key);
        if (beauty) {
            beauty->m_Data.m_nCount = runtime::SubtractInt32Wrapping(beauty->m_Data.m_nCount, 1);
        }
    } else if (state == Beauty::COLOR) {
        CSPointer<BeautyColor> beauty = m_vColorList.FindObject(key);
        if (beauty) {
            beauty->m_Data.m_nCount = runtime::SubtractInt32Wrapping(beauty->m_Data.m_nCount, 1);
        }
    }
}

void CBeautyManager::GetData(const CBeautyData& data, const bool includePrice) {
    L2_GUARD_FUNCTION();
    if (data.m_nEnable == 0) {
        return;
    }
    if (includePrice) {
        m_packetHelper.Assemble(
            "dddddd", data.m_nUpperID, data.m_nID, static_cast<std::int32_t>(data.m_nAdena),
            static_cast<std::int32_t>(data.m_nResetAdena), static_cast<std::int32_t>(data.m_nCoin), data.m_nCount
        );
    } else {
        m_packetHelper.Assemble("dd", data.m_nID, data.m_nCount);
    }
}

void CBeautyManager::GetDataList(const int userSid, const Beauty::BeautyState state) {
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(m_lock[static_cast<std::size_t>(state)]);
        m_packetHelper.Reset();

        if (state == Beauty::HAIR && m_vHairList.GetSize() != 0) {
            CBeautyHairList list(false);
            m_vHairList.FOR_ALL(list);
        } else if (state == Beauty::FACE && m_vFaceList.GetSize() != 0) {
            CBeautyFaceList list(false);
            m_vFaceList.FOR_ALL(list);
        }

        if (m_packetHelper.GetLength() != 0) {
            CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
            User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
            if (user != nullptr) {
                const int count = m_packetHelper.GetAssembledCount();
                user->GatherBeautyPurchaseData(static_cast<int>(state), m_packetHelper);
                if (CUserSocket* const socket = user->GetSocket()) {
                    socket->Send(
                        "chQQddb", 0xfe, 0x135, user->m_inv.GetAdenaAmount(), user->m_inv.GetItemAmount(0x8dd4, 0, true),
                        static_cast<int>(state), count, m_packetHelper.GetLength(), m_packetHelper.GetPacket()
                    );
                }
            }
        }

        m_packetHelper.Reset();
    }
}

void CBeautyManager::GetItemData(const Beauty::BeautyState state) {
    L2_GUARD_FUNCTION();
    if (state == Beauty::HAIR) {
        m_packetHelper.Assemble("dd", static_cast<int>(state), m_vHairList.GetSize());
        CBeautyHairList list(true);
        m_vHairList.FOR_ALL(list);
    } else if (state == Beauty::FACE) {
        m_packetHelper.Assemble("dd", static_cast<int>(state), m_vFaceList.GetSize());
        CBeautyFaceList list(true);
        m_vFaceList.FOR_ALL(list);
    } else if (state == Beauty::COLOR) {
        m_packetHelper.Assemble("dd", static_cast<int>(state), m_vColorList.GetSize());
        CBeautyColorList list(true);
        m_vColorList.FOR_ALL(list);
    }
}

void CBeautyManager::SendAllDataList(User* const user) {
    // Current L2Server 0x004b4988.
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->GetSocket() == nullptr) {
        return;
    }

    CPacketHelperEx<20480> packet;
    for (int state = Beauty::HAIR; state <= Beauty::COLOR; ++state) {
        {
            L2_WRITE_GUARD(m_lock[static_cast<std::size_t>(state)]);
            m_packetHelper.Reset();
            GetItemData(static_cast<Beauty::BeautyState>(state));
            if (m_packetHelper.GetLength() != 0) {
                char* const itemPacket = m_packetHelper.GetPacket();
                const int itemPacketLength = m_packetHelper.GetLength();
                packet.Assemble("b", itemPacketLength, itemPacket);
            }
            m_packetHelper.Reset();
        }
    }

    if (packet.GetLength() != 0) {
        user->GetSocket()->Send("chb", 0xfe, 0x179, packet.GetLength(), packet.GetPacket());
    }
}

void CBeautyManager::RequestBeautyPurchaseList(User& user, const char requestFlag) {
    L2_GUARD_FUNCTION();
    db.RequestBeautyPurchaseList(user, requestFlag);
}

bool CBeautyManager::ResetBeautyItem(
    const Beauty::BeautyState state,
    const int primary,
    const int secondary,
    const int userDatabaseId,
    CPacketHelperEx<20480>& packet,
    std::int64_t& adenaPrice,
    std::int64_t&,
    const int builder
) {
    // Current L2Server 0x004b3888, original owner BeautyManager.cpp.
    L2_GUARD_FUNCTION();

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), userDatabaseId));
    if (!user) {
        return true;
    }

    std::int64_t resetAdena = 0;
    bool valid = true;
    const BeautyKey key(primary, secondary);
    if (state == Beauty::HAIR) {
        CSPointer<BeautyHair> beauty = m_vHairList.FindObject(key);
        if (beauty) {
            resetAdena = beauty->m_Data.m_nResetAdena;
            valid = secondary == user->m_data->m_nFaceEx;
        }
    } else if (state == Beauty::FACE) {
        CSPointer<BeautyFace> beauty = m_vFaceList.FindObject(key);
        if (beauty) {
            resetAdena = beauty->m_Data.m_nResetAdena;
            valid = secondary == user->m_data->m_nHairColorEx;
        }
    } else if (state == Beauty::COLOR) {
        CSPointer<BeautyColor> beauty = m_vColorList.FindObject(key);
        if (beauty) {
            resetAdena = beauty->m_Data.m_nResetAdena;
            valid = secondary == user->m_data->m_nHairShapeEx && primary == user->m_data->m_nFaceEx;
        } else {
            valid = primary == -1;
        }
        valid = valid && ((primary == -1 && secondary == -1) || (primary != -1 && secondary != -1));
    } else {
        valid = false;
    }

    if (!valid) {
        user->SendSystemMessage(SystemMessageId::PREVIOUS_STYLE_RESTORATION_FAILED);
        return false;
    }
    if (builder == 0 && runtime::SubtractInt64Wrapping(user->m_inv.GetAdenaAmount(), resetAdena) < 0) {
        user->SendSystemMessage(SystemMessageId::STYLE_RESTORATION_FAILED_NOT_ENOUGH_ADENA);
        return false;
    }

    packet.Assemble("ddd", static_cast<int>(state), secondary, primary);
    adenaPrice = runtime::AddInt64Wrapping(adenaPrice, resetAdena);
    return true;
}

bool CBeautyManager::BuyBeautyItem(
    const Beauty::BeautyState state,
    const int primary,
    const int secondary,
    const int userDatabaseId,
    CPacketHelperEx<20480>& packet,
    std::int64_t& adenaPrice,
    std::int64_t& beautyCoinPrice,
    const int builder
) {
    // Current L2Server 0x004b3f44, original owner BeautyManager.cpp.
    L2_GUARD_FUNCTION();

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), userDatabaseId));
    if (!user) {
        return true;
    }

    std::int64_t itemAdena = 0;
    std::int64_t itemCoin = 0;
    int itemCount = 1;
    bool valid = true;
    const BeautyKey key(primary, secondary);
    if (state == Beauty::HAIR) {
        CSPointer<BeautyHair> beauty = m_vHairList.FindObject(key);
        if (beauty && secondary != user->m_data->m_nFaceEx) {
            itemAdena = beauty->m_Data.m_nAdena;
            itemCoin = beauty->m_Data.m_nCoin;
            itemCount = beauty->m_Data.m_nCount;
        }
    } else if (state == Beauty::FACE) {
        CSPointer<BeautyFace> beauty = m_vFaceList.FindObject(key);
        if (beauty) {
            itemAdena = beauty->m_Data.m_nAdena;
            itemCoin = beauty->m_Data.m_nCoin;
            itemCount = beauty->m_Data.m_nCount;
        }
    } else if (state == Beauty::COLOR) {
        CSPointer<BeautyColor> beauty = m_vColorList.FindObject(key);
        if (beauty) {
            itemAdena = beauty->m_Data.m_nAdena;
            itemCoin = beauty->m_Data.m_nCoin;
            itemCount = beauty->m_Data.m_nCount;
        } else {
            valid = primary == -1 && secondary == -1;
        }
        valid = valid && ((primary == -1 && secondary == -1) || (primary != -1 && secondary != -1));
    } else {
        valid = false;
    }

    if (!valid) {
        user->SendSystemMessage(SystemMessageId::STYLE_CHANGE_FAILED);
        return false;
    }
    if (builder == 0) {
        if (runtime::SubtractInt64Wrapping(user->m_inv.GetAdenaAmount(), itemAdena) < 0) {
            user->SendSystemMessage(SystemMessageId::PURCHASE_FAILED_NOT_ENOUGH_ADENA);
            return false;
        }
        if (runtime::SubtractInt64Wrapping(user->m_inv.GetItemAmount(0x8dd4, 0, true), itemCoin) < 0) {
            user->SendSystemMessage(SystemMessageId::NOT_ENOUGH_REQUIRED_ITEMS);
            return false;
        }
        if (itemCount <= 0) {
            user->SendSystemMessage(SystemMessageId::ITEM_SOLD_OUT);
            return false;
        }
    }

    packet.Assemble("ddd", static_cast<int>(state), secondary, primary);
    adenaPrice = runtime::AddInt64Wrapping(adenaPrice, itemAdena);
    beautyCoinPrice = runtime::AddInt64Wrapping(beautyCoinPrice, itemCoin);
    return true;
}

bool CBeautyManager::SetBeautyItemLimit(const Beauty::BeautyState state, const int id, const int upperId, const int limit) {
    // Current L2Server 0x004b47ac, original owner BeautyManager.cpp.
    L2_GUARD_FUNCTION();

    const BeautyKey key(upperId, id);
    if (state == Beauty::HAIR) {
        CSPointer<BeautyHair> beauty = m_vHairList.FindObject(key);
        if (beauty) {
            beauty->m_Data.m_nCount = limit;
            return true;
        }
    } else if (state == Beauty::FACE) {
        CSPointer<BeautyFace> beauty = m_vFaceList.FindObject(key);
        if (beauty) {
            beauty->m_Data.m_nCount = limit;
            return true;
        }
    } else if (state == Beauty::COLOR) {
        CSPointer<BeautyColor> beauty = m_vColorList.FindObject(key);
        if (beauty) {
            beauty->m_Data.m_nCount = limit;
            return true;
        }
    }
    return false;
}
