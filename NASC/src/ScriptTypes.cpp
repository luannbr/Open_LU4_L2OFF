#include "ScriptTypes.h"

namespace nasc {

const std::array<std::optional<ScriptTypeDescriptor>, 35> scriptTypeTable{{
    std::nullopt,
    ScriptTypeDescriptor{1, u"void", u"void", false},
    ScriptTypeDescriptor{2, u"int", u"int", false},
    ScriptTypeDescriptor{3, u"float", u"float", false},
    ScriptTypeDescriptor{4, u"string", u"wchar_t*", false},
    ScriptTypeDescriptor{5, u"double", u"double", false},
    ScriptTypeDescriptor{6, u"int64", u"__int64", false},
    ScriptTypeDescriptor{7, u"pointer", u"void*", false},
    ScriptTypeDescriptor{8, u"CreatureData", u"CSharedCreatureData*", true},
    ScriptTypeDescriptor{9, u"ItemData", u"CSharedItemData*", true},
    ScriptTypeDescriptor{10, u"PledgeData", u"CSharedPledgeData*", true},
    ScriptTypeDescriptor{11, u"PartyData", u"CSharedPartyData*", true},
    ScriptTypeDescriptor{12, u"StaticObjectData", u"CSharedStaticObjectData*", true},
    ScriptTypeDescriptor{13, u"NpcMaker", u"NpcMakerEx*", true},
    ScriptTypeDescriptor{14, u"NPC", u"CNPC*", true},
    ScriptTypeDescriptor{15, u"HateInfo", u"HateInfo*", true},
    ScriptTypeDescriptor{16, u"GlobalObject", u"GlobalObject*", true},
    ScriptTypeDescriptor{17, u"SpawnDefine", u"NpcSpawnDefineEx*", true},
    ScriptTypeDescriptor{18, u"CodeInfo", u"CodeInfo*", true},
    ScriptTypeDescriptor{19, u"CodeInfoList", u"CodeInfoList*", true},
    ScriptTypeDescriptor{20, u"DBNpcInfo", u"DBNpcInfo*", true},
    ScriptTypeDescriptor{21, u"AtomicValue", u"AtomicValue*", true},
    ScriptTypeDescriptor{22, u"RoomInfo", u"RoomInfo*", true},
    ScriptTypeDescriptor{23, u"RoomInfoList", u"RoomInfoList*", true},
    ScriptTypeDescriptor{24, u"Position", u"Position*", true},
    ScriptTypeDescriptor{25, u"PositionList", u"PositionList*", true},
    ScriptTypeDescriptor{26, u"FHTML", u"CFHTML*", true},
    ScriptTypeDescriptor{27, u"WayPoints", u"std::vector<FVector>*", false},
    ScriptTypeDescriptor{28, u"WayPointDelays", u"std::vector<int>*", false},
    ScriptTypeDescriptor{29, u"TelPosList", u"std::vector<TelPos>*", false},
    ScriptTypeDescriptor{30, u"BuySellList", u"std::vector<BuySell>*", false},
    ScriptTypeDescriptor{31, u"ItemIndexList", u"ItemIndexList*", true},
    ScriptTypeDescriptor{32, u"IntList", u"IntList*", true},
    ScriptTypeDescriptor{33, u"MPCCData", u"CSharedGeneralObjectData*", true},
    ScriptTypeDescriptor{34, u"AtomicValueMaker", u"AtomicValueMaker*", true},
}};

const std::array<ScriptEventTypeDescriptor, 2> scriptEventTypeTable{{
    ScriptEventTypeDescriptor{0, u"NpcEvent", u"CNPCEvent*"},
    ScriptEventTypeDescriptor{1, u"MakerEvent", u"MakerEvent*"},
}};

const ScriptTypeDescriptor* FindScriptType(std::int32_t id) noexcept {
    if (id < 0 || static_cast<std::size_t>(id) >= scriptTypeTable.size()) {
        return nullptr;
    }
    const auto& descriptor = scriptTypeTable[static_cast<std::size_t>(id)];
    return descriptor ? &*descriptor : nullptr;
}

const ScriptTypeDescriptor* FindScriptType(std::u16string_view name) noexcept {
    for (const auto& descriptor : scriptTypeTable) {
        if (descriptor && descriptor->name == name) {
            return &*descriptor;
        }
    }
    return nullptr;
}

const ScriptEventTypeDescriptor* FindScriptEventType(std::int32_t id) noexcept {
    if (id < 0 || static_cast<std::size_t>(id) >= scriptEventTypeTable.size()) {
        return nullptr;
    }
    return &scriptEventTypeTable[static_cast<std::size_t>(id)];
}

bool AssignCompatible(const ScriptTypeDescriptor* destination, const ScriptTypeDescriptor* source) noexcept {
    const ScriptTypeDescriptor* const voidType = FindScriptType(1);
    const ScriptTypeDescriptor* const intType = FindScriptType(2);
    const ScriptTypeDescriptor* const floatType = FindScriptType(3);
    const ScriptTypeDescriptor* const doubleType = FindScriptType(5);
    const ScriptTypeDescriptor* const int64Type = FindScriptType(6);
    const ScriptTypeDescriptor* const pointerType = FindScriptType(7);

    if (destination == intType || destination == int64Type) {
        return source == intType || source == int64Type;
    }
    if (destination == floatType) {
        return source == intType || source == int64Type || source == floatType || source == doubleType;
    }
    if (destination == voidType || source == intType) {
        return false;
    }
    if (destination == pointerType) {
        return source->isClass;
    }
    return destination == source;
}

bool IsIntegralType(const ScriptTypeDescriptor* type) noexcept { return type->id == 2 || type->id == 6; }

bool OperatorCompatible(const ScriptTypeDescriptor* left, const ScriptTypeDescriptor* right) noexcept {
    const ScriptTypeDescriptor* const intType = FindScriptType(2);
    const ScriptTypeDescriptor* const floatType = FindScriptType(3);
    const ScriptTypeDescriptor* const doubleType = FindScriptType(5);
    const ScriptTypeDescriptor* const int64Type = FindScriptType(6);

    if (left == intType || left == int64Type) {
        return right == intType || right == int64Type;
    }
    if (left == floatType) {
        return right == floatType || right == doubleType;
    }
    return left == right;
}

BinaryOperationTypeResult BinaryOperationType(const ScriptTypeDescriptor* left, const ScriptTypeDescriptor* right) noexcept {
    const ScriptTypeDescriptor* const intType = FindScriptType(2);
    const ScriptTypeDescriptor* const floatType = FindScriptType(3);
    const ScriptTypeDescriptor* const doubleType = FindScriptType(5);
    const ScriptTypeDescriptor* const int64Type = FindScriptType(6);

    // The original reports an error and uses int when an operand is a class.
    if (left->isClass || right->isClass) {
        return {intType, false};
    }
    if (left == intType && right == intType) {
        return {intType, true};
    }
    if ((left == intType && right == int64Type) || (left == int64Type && right == intType)) {
        return {int64Type, true};
    }
    if (left == floatType || left == doubleType || right == floatType || right == doubleType) {
        return {floatType, true};
    }
    if (left == right) {
        return {left, true};
    }
    // For other mismatches the original reports an error but returns the left
    // type, not int.
    return {left, false};
}

} // namespace nasc
