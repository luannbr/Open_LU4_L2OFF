#include "AgitDeco.h"
#include "Shared/src/MemorySub.h"

#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Exception.h"

#include <atomic>
#include <cstdint>
#include <cstring>


CResidenceDeco::CResidenceDeco() {
    L2_GUARD_FUNCTION();

    m_nDecoId = 0;
    m_sDecoName[0] = u'\0';
    m_nType = 0;
    m_nLevel = 0;
    m_nDepth = 0;
    m_nFunction = 0;
    m_dArgument = 0.0;
}

CResidenceDecoVisible::CResidenceDecoVisible(std::uint32_t residenceId, FVector position) :
    m_nResidenceId(residenceId), m_aDepthForClient{} {
    L2_GUARD_FUNCTION();
    SetPos(position);
    CStaticObject::OnCreate();
}

CResidenceDecoVisible::CResidenceDecoVisible(const CResidenceDecoVisible& other) :
    CStaticObject(other), m_nResidenceId(other.m_nResidenceId), m_aDepthForClient{} {
    L2_GUARD_FUNCTION();
    SetPos(other.GetPos());
    CStaticObject::OnCreate();
}

CResidenceDecoVisible::~CResidenceDecoVisible() {
    g_ServerContext.ResidenceDecoVisible.fetch_sub(1, std::memory_order_relaxed);
}

L2_DEFINE_OBJECT_CLASS_ID(CResidenceDecoVisible)

L2_DEFINE_OBJECT_CLASS_NAME(CResidenceDecoVisible, m_pClassName_CResidenceDecoVisible)

L2_DEFINE_OBJECT_ALLOCATOR(CResidenceDecoVisible, __GetClassName())

L2_DEFINE_OBJECT_NEW(CResidenceDecoVisible)

L2_DEFINE_OBJECT_TYPED_RELEASE(CResidenceDecoVisible)

CObject* CResidenceDecoVisible::Copy() { return new CResidenceDecoVisible(*this); }

bool CResidenceDecoVisible::IsResidenceDecoVisible() { return true; }

CResidenceDecoVisible* CResidenceDecoVisible::CastResidenceDecoVisible() { return this; }

bool CResidenceDecoVisible::IsEnemyTo(CCreature*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool CResidenceDecoVisible::IsAlive() { return true; }

bool CResidenceDecoVisible::IsActive() { return true; }

double CResidenceDecoVisible::AttackedBy(CCreature* attacker, double damage) {
    L2_GUARD_FUNCTION();
    return GotDamageBy(attacker, damage);
}

double CResidenceDecoVisible::GotDamageBy(CCreature*, double) {
    L2_GUARD_FUNCTION();
    return 0.0;
}

bool CResidenceDecoVisible::IsEnabled() { return true; }

void CResidenceDecoVisible::SendResidenceDecoVisibleInfo(CUserSocket* socket) {
    L2_GUARD_FUNCTION();
    class TLMFunc final : public CTLMFunc {
    public:
        TLMFunc(const std::uint32_t residenceId, const std::uint8_t* depth, const int size) noexcept :
            m_residenceId(residenceId), m_depth(depth), m_size(size) {}

        bool CanDo(CObject* object, double) override {
            if (object->IsPC()) {
                User* const user = object->CastUser();
                if (user != nullptr && !user->IsDeleting() && user->m_Deleted == 0) {
                    user->GetSocket()->Send("cdb", 0xfd, m_residenceId, m_size, m_depth);
                }
            }
            return true;
        }

    private:
        std::uint32_t m_residenceId;
        const std::uint8_t* m_depth;
        std::int32_t m_size;
    };

    if (socket != nullptr) {
        socket->Send("cdb", 0xfd, m_nResidenceId, 0x20, m_aDepthForClient);
        return;
    }

    CTLMFilterRect filter(0, GetPos(), 2048.0);
    TLMFunc sender(m_nResidenceId, m_aDepthForClient, 0x20);
    FOR_ALL_NEIGHBOR_USER_RECT(&filter, &sender);
}

void CResidenceDecoVisible::SetDecoDepth(int index, int depth) {
    m_aDepthForClient[index] = static_cast<std::uint8_t>(depth);
    SendResidenceDecoVisibleInfo(nullptr);
}
