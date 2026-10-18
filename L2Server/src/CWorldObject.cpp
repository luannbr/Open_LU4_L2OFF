#include "CWorldObject.h"
#include "Shared/src/MemorySub.h"

#include "Creature.h"
#include "SerializedMessage.h"
#include "ThreadLocalWorldMap.h"


#include "Shared/src/Runtime/WrappingArithmetic.h"

#include <cstdint>

namespace {


double DistanceSquared(const FVector& left, const FVector& right) noexcept {
    return (right.x - left.x) * (right.x - left.x) + (right.y - left.y) * (right.y - left.y) +
        (right.z - left.z) * (right.z - left.z);
}

int CurrentYaw(const FAngle& direction) noexcept { return static_cast<int>(static_cast<std::uint16_t>(direction.Yaw)); }

} // namespace

CWorldObject::CWorldObject() {
    L2_GUARD_FUNCTION();
    m_nInstantZoneID = 0;
    m_nNextInstantZoneID = 0;
}

CWorldObject::CWorldObject(const CWorldObject& other) : CObject(other) {
    L2_GUARD_FUNCTION();
    m_nInstantZoneID = other.m_nInstantZoneID;
    m_nNextInstantZoneID = other.m_nNextInstantZoneID;
}

CWorldObject::~CWorldObject() = default;

L2_DEFINE_OBJECT_TYPED_RELEASE(CWorldObject)

bool CWorldObject::SetTargetable(bool, bool flag) {
    L2_GUARD_FUNCTION();
    if (!IsTargetable() && flag) {
        class TLMFilter final : public CTLMFilterDist {
        public:
            TLMFilter(CWorldObject* source, const FVector& position) :
                CTLMFilterDist(0, position, 2048.0), source_(source) {}

            bool CanDo(CObject* object) override {
                return source_ != nullptr && object != source_ && object != nullptr && CTLMFilterDist::CanDo(object);
            }

        private:
            CWorldObject* source_;
        };

        class TLMFunc final : public CTLMFunc {
        public:
            explicit TLMFunc(CWorldObject* target) : target_(target) {}

            bool CanDo(CObject* object, double) override {
                if (object->IsPC() && !object->IsDeleting() && object->m_Deleted == 0) {
                    CCreature* const actor = object->CastCreature();
                    if (actor->m_spTarget.GetWorldObject() == target_ && actor->IsPC()) {
                        actor->PushSerializedMessage(new CSerializedChangeTargetMessage(actor->CastCreature()));
                    }
                }
                return true;
            }

        private:
            CWorldObject* target_;
        };

        const FVector position = GetPos();
        TLMFilter filter(this, position);
        TLMFunc function(this);
        FOR_ALL_NEIGHBOR_USER_DIST(&filter, &function);
    }
    return true;
}

bool CWorldObject::IsTargetable() { return true; }

bool CWorldObject::IsShowNametag() { return true; }

double CWorldObject::GetCollisionRadius() const { return 1.0; }

bool CWorldObject::IsWorldObject() { return true; }

CWorldObject* CWorldObject::CastWorldObject() { return this; }

void CWorldObject::DecideDistance(CMoveToTargetShareData* data, CSmartID creatureId) {
    CCreature* const creature = creatureId.GetCreature();
    // Current CCreature has CObject/CWorldObject at offset zero. Its exact
    // slot-20 body reads CSharedCreatureData+0xa0. The current compiler
    // inlined that same accessor into CWorldObject::DecideDistance.
    CObject* const creatureObject = reinterpret_cast<CObject*>(creature);
    const int distance = static_cast<int>(creatureObject->GetCollisionRadius() + 100.0);
    data->m_nDistance = distance;
    data->m_nAcceptableDistance = distance + 10;
}

FVector CWorldObject::ConvRelToAbs(FVector position) const {
    L2_GUARD_FUNCTION();
    const FAngle direction = GetDir();
    const int yaw = CurrentYaw(direction);
    const FVector absolute = GetPos();
    const FVector axis{0, 0, 1};
    const FVector rotated = FVector::RotateAngleAxis(&position, yaw, &axis);
    return FVector{
        absolute.x + rotated.x,
        absolute.y + rotated.y,
        absolute.z + rotated.z,
    };
}

FVector CWorldObject::ConvAbsToRel(FVector position) const {
    L2_GUARD_FUNCTION();
    const FVector absolute = GetPos();
    FVector relative{
        position.x - absolute.x,
        position.y - absolute.y,
        position.z - absolute.z,
    };
    const FAngle direction = GetDir();
    const FVector axis{0, 0, 1};
    return FVector::RotateAngleAxis(&relative, -CurrentYaw(direction), &axis);
}

void CWorldObject::SetCollisionHeight(double) {}

double CWorldObject::GetCollisionHeight() const { return 1.0; }

void CWorldObject::SetCollisionRadius(double) {}

bool CWorldObject::IsActive() {
    // Current shared vtable slot +0x80 targets 0x004c0008:
    // mov al, 1, then ret.
    return true;
}

void CWorldObject::SetDir(const FAngle&) {}

FVector CWorldObject::GetPosAsFollowTarget() const { return GetPos(); }

bool CWorldObject::IsInRange(CWorldObject* object, double range) {
    const FVector currentPosition = GetPos();
    const FVector objectPosition = object->GetPos();
    if (DistanceSquared(currentPosition, objectPosition) < range * range) {
        const unsigned int objectZone = object->GetInZoneID();
        const unsigned int currentZone = GetInZoneID();
        return objectZone == currentZone;
    }
    return false;
}

bool CWorldObject::IsInRange(CWorldObject* object, int range, bool ignoreZone) {
    const FVector currentPosition = GetPos();
    const FVector objectPosition = object->GetPos();
    const double squaredRange = static_cast<double>(runtime::MultiplyInt32Wrapping(range, range));
    if (DistanceSquared(currentPosition, objectPosition) < squaredRange) {
        const unsigned int objectZone = object->GetInZoneID();
        const unsigned int currentZone = GetInZoneID();
        return objectZone == currentZone || ignoreZone;
    }
    return false;
}

FVector CWorldObject::GetRelPos() const { return m_RelPos; }

void CWorldObject::SetRelPos(const FVector& position) { m_RelPos = position; }

CAbstractCarrier::CAbstractCarrier() { L2_GUARD_FUNCTION(); }

CAbstractCarrier::CAbstractCarrier(const CAbstractCarrier& other) : CWorldObject(other) { L2_GUARD_FUNCTION(); }

L2_DEFINE_OBJECT_TYPED_RELEASE(CAbstractCarrier)

bool CAbstractCarrier::IsAbstractCarrier() { return true; }

CAbstractCarrier* CAbstractCarrier::CastAbstractCarrier() { return this; }
