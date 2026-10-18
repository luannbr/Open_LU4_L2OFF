#pragma once

#include "Shared/src/ScriptAccessible.h"
#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace l2npc {
class DBNpcInfo;
class NpcMakerEx;
} // namespace l2npc


class AtomicValue;
class AtomicValueMaker;
class CFHTML;
struct CNPCEvent;
struct MakerEvent;
class CNPC;
class CodeInfo;
class CodeInfoList;
class CSharedCreatureData;
class CSharedGeneralObjectData;
class CSharedItemData;
class CSharedPartyData;
class CSharedPledgeData;
class CSharedStaticObjectData;
class GlobalObject;
class HateInfo;
class IntList;
class ItemIndexList;
class NpcSpawnDefineEx;
class Position;
class PositionList;
class RoomInfo;
class RoomInfoList;

// scripttypeinfo.h lines 14-46 in the matching PDB. The current L2NPC.exe
// confirms a 0x20-byte BuySell row and a 0x40-byte TelPos row (the newer
// four-word xvector accounts for the latter's growth from the donor's 0x38).
struct BuySell {
    int id = 0;
    std::int64_t price = 0;
    float rate = 0.0F;
    std::int64_t count = 0;
};

struct TelPos {
    TelPos(char16_t* name, int xValue, int yValue, int zValue, int priceValue, std::vector<int>* residenceIds) :
        nameIndex(0), pszName(name), x(xValue), y(yValue), z(zValue), price(priceValue) {
        // SHIPPING BUG SCRIPT-NULL-001: parser actions 0x1b and 0x1e pass a
        // null residenceIds pointer, while both shipping TelPos constructors
        // dereference it unconditionally. Keep the recovered loop disabled.
        /*
        for (int index = 0.
             index < residenceIds->size().
             ++index) {
            if ((*residenceIds)[index] != 0) {
                vResidenceIdList.push_back(
                    (*residenceIds)[index]).
            }
        }
        */

        if (residenceIds) {
            for (std::size_t index = 0; index < residenceIds->size(); ++index) {
                if (runtime::CheckedVectorAt((*residenceIds), index) != 0) {
                    vResidenceIdList.push_back(runtime::CheckedVectorAt((*residenceIds), index));
                }
            }
        }
    }

    TelPos(int nameIndexValue, int xValue, int yValue, int zValue, int priceValue, std::vector<int>* residenceIds) :
        nameIndex(nameIndexValue), pszName(nullptr), x(xValue), y(yValue), z(zValue), price(priceValue) {
        // SHIPPING BUG SCRIPT-NULL-001: this overload has the same unchecked
        // dereference as L2NPC.exe 0x0054b304. Null means that the grammar
        // form supplied no residence restriction. Retain the old loop here.
        /*
        for (int index = 0.
             index < residenceIds->size().
             ++index) {
            if ((*residenceIds)[index] != 0) {
                vResidenceIdList.push_back(
                    (*residenceIds)[index]).
            }
        }
        */

        if (residenceIds) {
            for (std::size_t index = 0; index < residenceIds->size(); ++index) {
                if (runtime::CheckedVectorAt((*residenceIds), index) != 0) {
                    vResidenceIdList.push_back(runtime::CheckedVectorAt((*residenceIds), index));
                }
            }
        }
    }

    int nameIndex = 0;
    char16_t* pszName = nullptr;
    int x = 0;
    int y = 0;
    int z = 0;
    int price = 0;
    std::vector<int> vResidenceIdList;
};

using BuySellList = std::vector<BuySell>;
using TelPosList = std::vector<TelPos>;


// Numeric ids are the ids embedded in ai.obj and used by the shipping
// ScriptTypeInfo table. Only the types needed by the recovered class
// registration surfaces are named here. The registry can still hold any id.
enum class ScriptTypeId : int {
    Void = 1,
    Int = 2,
    Bool = Int,
    Float = 3,
    String = 4,
    Double = 5,
    Int64 = 6,
    Pointer = 7,
    CreatureData = 8,
    Creature = CreatureData,
    ItemData = 9,
    PledgeData = 10,
    PartyData = 11,
    StaticObjectData = 12,
    NpcMaker = 13,
    NpcMakerEx = NpcMaker,
    Npc = 14,
    HateInfo = 15,
    GlobalObject = 16,
    NpcSpawnDefineEx = 17,
    CodeInfo = 18,
    CodeInfoList = 19,
    DBNpcInfo = 20,
    AtomicValue = 21,
    RoomInfo = 22,
    RoomInfoList = 23,
    Position = 24,
    PositionList = 25,
    FHtml = 26,
    WayPoints = 27,
    WayPointDelays = 28,
    TelPosList = 29,
    BuySellList = 30,
    ItemIndexList = 31,
    IntList = 32,
    MpccData = 33,
    AtomicValueMaker = 34,
};

// scripttypeinfo.h:203-205 in the matching source image. The shipping
// DeclareScriptVariable<T> template resolves its descriptor through this
// compile-time C++-type to script-type mapping.
template <typename T>
struct Type2Info;

template <>
struct Type2Info<int> {
    static int Id() noexcept { return static_cast<int>(ScriptTypeId::Int); }
};

template <>
struct Type2Info<AtomicValue*> {
    static int Id() noexcept { return static_cast<int>(ScriptTypeId::AtomicValue); }
};

class ScriptTypeInfo;
class ScriptEventTypeInfo;

// PDB type ScriptVarInfo. The 2018 image still allocates 0x38 bytes and
// accesses mOffset at +0x28 and mType at +0x30.
class ScriptVarInfo {
public:
    ScriptVarInfo() = default;
    ScriptVarInfo(const ScriptVarInfo&) = default;
    ~ScriptVarInfo() = default;

    const wchar_t* GetScriptVarName() const noexcept;
    void SetScriptVarName(const wchar_t* name);

    void Offset(int offset) noexcept;
    int Offset() const noexcept;

    // NASC field operands are independent of the host C++ object layout.
    int CompiledOffset() const noexcept { return mCompiledOffset < 0 ? mOffset : mCompiledOffset; }
    void CompiledOffset(int offset) noexcept { mCompiledOffset = offset; }

    void Type(ScriptTypeInfo* type) noexcept;
    ScriptTypeInfo* Type() const noexcept;

private:
    std::wstring m_ScriptVarName;
    int mOffset = -1;
    int mCompiledOffset = -1;
    ScriptTypeInfo* mType = nullptr;
};

// PDB and current-image type ScriptFuncInfo (0x60 bytes). Function
// descriptors remain callable: Invoke receives the same eight-byte argument
// slots used by the target engine after the object/self slot.
class ScriptFuncInfo {
public:
    ScriptFuncInfo() = default;
    ScriptFuncInfo(const ScriptFuncInfo&) = default;
    virtual ~ScriptFuncInfo();

    virtual ScriptAccessibleEx* GetScriptAccessibleEx(std::int64_t self) const = 0;
    virtual std::int64_t Invoke(std::int64_t self, void* arguments) = 0;

    void AddArgType(ScriptTypeInfo* type);
    void SetRetType(ScriptTypeInfo* type) noexcept;
    ScriptTypeInfo* RetType() const noexcept;
    std::size_t ArgN() const noexcept;
    ScriptTypeInfo* ArgType(int index) const noexcept;

    void Index(int index) noexcept;
    int Index() const noexcept;
    void Name(const wchar_t* name);
    const wchar_t* Name() const noexcept;

private:
    int mIndex = 0;
    std::wstring mName;
    ScriptTypeInfo* mRetType = nullptr;
    std::vector<ScriptTypeInfo*> mArgTypeList;
};

namespace script_type_detail {

template <typename T>
struct IsScriptClass : std::false_type {};

#define L2NPC_SCRIPT_CLASS_TYPE(TYPE)                                                                                       \
    template <>                                                                                                             \
    struct IsScriptClass<TYPE*> : std::true_type {}

L2NPC_SCRIPT_CLASS_TYPE(AtomicValue);
L2NPC_SCRIPT_CLASS_TYPE(AtomicValueMaker);
L2NPC_SCRIPT_CLASS_TYPE(CFHTML);
L2NPC_SCRIPT_CLASS_TYPE(CNPC);
L2NPC_SCRIPT_CLASS_TYPE(CodeInfo);
L2NPC_SCRIPT_CLASS_TYPE(CodeInfoList);
L2NPC_SCRIPT_CLASS_TYPE(CSharedCreatureData);
L2NPC_SCRIPT_CLASS_TYPE(CSharedGeneralObjectData);
L2NPC_SCRIPT_CLASS_TYPE(CSharedItemData);
L2NPC_SCRIPT_CLASS_TYPE(CSharedPartyData);
L2NPC_SCRIPT_CLASS_TYPE(CSharedPledgeData);
L2NPC_SCRIPT_CLASS_TYPE(CSharedStaticObjectData);
L2NPC_SCRIPT_CLASS_TYPE(GlobalObject);
L2NPC_SCRIPT_CLASS_TYPE(HateInfo);
L2NPC_SCRIPT_CLASS_TYPE(IntList);
L2NPC_SCRIPT_CLASS_TYPE(ItemIndexList);
L2NPC_SCRIPT_CLASS_TYPE(Position);
L2NPC_SCRIPT_CLASS_TYPE(PositionList);
L2NPC_SCRIPT_CLASS_TYPE(RoomInfo);
L2NPC_SCRIPT_CLASS_TYPE(RoomInfoList);
L2NPC_SCRIPT_CLASS_TYPE(l2npc::DBNpcInfo);
L2NPC_SCRIPT_CLASS_TYPE(l2npc::NpcMakerEx);
L2NPC_SCRIPT_CLASS_TYPE(NpcSpawnDefineEx);

#undef L2NPC_SCRIPT_CLASS_TYPE

void InitializeRegisteredScriptClass(ScriptTypeInfo& typeInfo);
void InitializeRegisteredScriptEvent(ScriptEventTypeInfo& typeInfo);

template <typename T>
using RemoveCvRef = typename std::remove_cv<typename std::remove_reference<T>::type>::type;

template <typename MemberPointer>
struct MemberFunctionTraits;

#define L2NPC_SCRIPT_MEMBER_TRAITS(QUALIFIERS)                                                                              \
    template <typename Return, typename Owner, typename... Arguments>                                                       \
    struct MemberFunctionTraits<Return (Owner::*)(Arguments...) QUALIFIERS> {                                               \
        using ReturnType = Return;                                                                                          \
        using OwnerType = Owner;                                                                                            \
        using ArgumentTuple = std::tuple<Arguments...>;                                                                     \
    }

L2NPC_SCRIPT_MEMBER_TRAITS();
L2NPC_SCRIPT_MEMBER_TRAITS(const);
L2NPC_SCRIPT_MEMBER_TRAITS(noexcept);
L2NPC_SCRIPT_MEMBER_TRAITS(const noexcept);

#undef L2NPC_SCRIPT_MEMBER_TRAITS

// MSVC stores a single-inheritance member-function pointer in one eight-byte
// word. The three script owners below use multiple inheritance in the target,
// and their registration helpers copy a four-dword member pointer into
// ScriptFuncInfo_T at +0x60..+0x6f instead. The matching allocations are
// 0x70 bytes in L2NPC.exe (CNPC 0x004b9858, NpcMakerEx 0x004fcae4 and
// NpcSpawnDefineEx 0x005059cc). Ordinary owners allocate 0x68 bytes.
template <typename Owner>
struct TargetMemberFunctionPointerWords : std::integral_constant<std::size_t, 1> {};

template <>
struct TargetMemberFunctionPointerWords<CNPC> : std::integral_constant<std::size_t, 2> {};

template <>
struct TargetMemberFunctionPointerWords<l2npc::NpcMakerEx> : std::integral_constant<std::size_t, 2> {};

template <>
struct TargetMemberFunctionPointerWords<NpcSpawnDefineEx> : std::integral_constant<std::size_t, 2> {};

// A host member pointer cannot be placed directly in the target carrier:
// Itanium uses two words even for the target's one-word cases. Keep that
// callable value out of line while preserving the target descriptor's exact
// one- or two-word extent. The second target word is the MSVC this/vbase
// adjustment pair and is zero for the registered methods recovered here.
template <typename MemberPointer, std::size_t TargetWords>
struct PortableMemberFunctionCarrier;

template <typename MemberPointer>
struct PortableMemberFunctionCarrier<MemberPointer, 1> {
    MemberPointer* portableValue = nullptr;
};

template <typename MemberPointer>
struct PortableMemberFunctionCarrier<MemberPointer, 2> {
    MemberPointer* portableValue = nullptr;
    std::int32_t targetThisAdjustment = 0;
    std::int32_t targetVBaseDisplacement = 0;
};


template <typename Argument>
using StoredArgument = RemoveCvRef<Argument>;

template <typename Argument>
StoredArgument<Argument> DecodeArgument(const std::byte* slot) {
    using Value = StoredArgument<Argument>;
    static_assert(
        std::is_trivially_copyable<Value>::value,
        "shipping ScriptFuncInfo_T reads arguments directly from "
        "eight-byte VM slots"
    );

    // Each target specialization reads the declared C++ width from the low
    // bytes of its eight-byte slot (byte for bool, dword for int/float and
    // qword for pointers/int64). Reading the slot as int64 first would, for
    // example, turn the raw integer 0x100 into true for a bool argument even
    // though L2NPC.exe reads only its zero low byte.
    Value value;
    std::memcpy(&value, slot, sizeof(value));
    return value;
}

template <typename Value>
std::int64_t EncodeReturn(Value value) {
    using Result = RemoveCvRef<Value>;
    if constexpr (std::is_pointer<Result>::value) {
        return static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(value));
    } else if constexpr (std::is_same<Result, float>::value) {
        std::int64_t raw = 0;
        std::memcpy(&raw, &value, sizeof(value));
        return raw;
    } else if constexpr (std::is_same<Result, double>::value) {
        std::int64_t raw = 0;
        std::memcpy(&raw, &value, sizeof(value));
        return raw;
    } else {
        return static_cast<std::int64_t>(value);
    }
}

} // namespace script_type_detail

// The target emits one ScriptFuncInfo_T specialization for each member
// signature. The portable specialization keeps the actual member pointer
// and can call it from the target's eight-byte value slots.
template <typename MemberPointer>
class ScriptFuncInfo_T final : public ScriptFuncInfo {
public:
    explicit ScriptFuncInfo_T(MemberPointer function) : mMemFunc{} { mMemFunc.portableValue = new MemberPointer(function); }

    ScriptFuncInfo_T(const ScriptFuncInfo_T& other) : ScriptFuncInfo(other), mMemFunc{} {
        mMemFunc.portableValue = other.mMemFunc.portableValue ? new MemberPointer(*other.mMemFunc.portableValue) : nullptr;
    }

    ScriptFuncInfo_T& operator=(const ScriptFuncInfo_T& other) {
        if (this != &other) {
            auto* const replacement =
                other.mMemFunc.portableValue ? new MemberPointer(*other.mMemFunc.portableValue) : nullptr;
            ScriptFuncInfo::operator=(other);
            delete mMemFunc.portableValue;
            mMemFunc.portableValue = replacement;
        }
        return *this;
    }

    ~ScriptFuncInfo_T() override { delete mMemFunc.portableValue; }

    ScriptAccessibleEx* GetScriptAccessibleEx(std::int64_t self) const override {
        using Owner = typename script_type_detail::MemberFunctionTraits<MemberPointer>::OwnerType;
        Owner* const owner = reinterpret_cast<Owner*>(static_cast<std::intptr_t>(self));
        return static_cast<ScriptAccessibleEx*>(owner)->GetScriptAccessibleEx();
    }

    std::int64_t Invoke(std::int64_t self, void* arguments) override {
        using Traits = script_type_detail::MemberFunctionTraits<MemberPointer>;
        using Owner = typename Traits::OwnerType;
        using ArgumentTuple = typename Traits::ArgumentTuple;
        Owner* const owner = reinterpret_cast<Owner*>(static_cast<std::intptr_t>(self));
        const auto* const slots = static_cast<const std::byte*>(arguments);
        return InvokeImpl(owner, slots, std::make_index_sequence<std::tuple_size<ArgumentTuple>::value>{});
    }

private:
    template <std::size_t... Indices>
    std::int64_t InvokeImpl(
        typename script_type_detail::MemberFunctionTraits<MemberPointer>::OwnerType* owner,
        const std::byte* slots,
        std::index_sequence<Indices...>
    ) {
        using Traits = script_type_detail::MemberFunctionTraits<MemberPointer>;
        using Return = typename Traits::ReturnType;
        using ArgumentTuple = typename Traits::ArgumentTuple;

        std::tuple<script_type_detail::StoredArgument<typename std::tuple_element<Indices, ArgumentTuple>::type>...> decoded{
            script_type_detail::DecodeArgument<typename std::tuple_element<Indices, ArgumentTuple>::type>(
                slots + Indices * sizeof(std::int64_t)
            )...};

        if constexpr (std::is_void<Return>::value) {
            std::apply([&](auto&... values) { std::invoke(*mMemFunc.portableValue, owner, values...); }, decoded);
            return 0;
        } else {
            Return result = std::apply(
                [&](auto&... values) -> Return { return std::invoke(*mMemFunc.portableValue, owner, values...); }, decoded
            );
            return script_type_detail::EncodeReturn(result);
        }
    }

    using Owner = typename script_type_detail::MemberFunctionTraits<MemberPointer>::OwnerType;
    static constexpr std::size_t kTargetMemberFunctionPointerWords =
        script_type_detail::TargetMemberFunctionPointerWords<Owner>::value;
    using MemberFunctionCarrier =
        script_type_detail::PortableMemberFunctionCarrier<MemberPointer, kTargetMemberFunctionPointerWords>;

    // Begins at target offset +0x60. It has the same physical extent as the
    // matching MSVC member pointer while owning the host representation.
    MemberFunctionCarrier mMemFunc;
};


// The donor PDB records ScriptTypeInfo as 0x88 bytes. The shipping image
// keeps the same fields and offsets through mVarTable, but its two current
// std::map objects are 0x20 bytes each: mFuncTable moved from +0x70 to +0x78
// and the complete current object is 0x98 bytes.
class ScriptTypeInfo {
public:
    using var_table_type = std::map<std::wstring, ScriptVarInfo*>;
    using func_table_type = std::map<std::wstring, ScriptFuncInfo*>;

    ScriptTypeInfo() = default;
    ScriptTypeInfo(const ScriptTypeInfo&) = default;
    ScriptTypeInfo& operator=(const ScriptTypeInfo&) = default;
    virtual ~ScriptTypeInfo() = default;

    virtual void Initialize();
    virtual bool IsClass() const;

    int Id() const noexcept;
    void Id(int id) noexcept;
    void SetScriptTypeName(const wchar_t* name);
    const wchar_t* GetScriptTypeName() const noexcept;

    void AddFuncInfo(ScriptFuncInfo* function);
    void AddVarInfo(ScriptVarInfo* variable);
    ScriptVarInfo* VarNameToInfo(const wchar_t* name) const;
    void SetCompiledOffset(const wchar_t* name, int offset);
    void IndexCompiledFields();
    ScriptVarInfo* CompiledField(int offset) const;
    ScriptFuncInfo* FuncNameToInfo(const wchar_t* name) const;
    ScriptFuncInfo* IndexToSFNP(int index) const;
    var_table_type& GetVarTable() noexcept;
    func_table_type& GetFuncTable() noexcept;

    template <typename MemberPointer>
    void AddFunction(
        const wchar_t* name,
        MemberPointer function,
        ScriptTypeInfo* returnType,
        std::initializer_list<ScriptTypeInfo*> argumentTypes
    ) {
        auto* const descriptor = new ScriptFuncInfo_T<MemberPointer>(function);
        descriptor->Name(name);
        descriptor->SetRetType(returnType);
        for (ScriptTypeInfo* type : argumentTypes) {
            descriptor->AddArgType(type);
        }
        AddFuncInfo(descriptor);
    }

private:
    int mId = 0;
    std::wstring m_ScriptTypeName;
    std::vector<ScriptFuncInfo*> mFuncList;
    var_table_type mVarTable;
    std::map<int, ScriptVarInfo*> mCompiledFields;
    func_table_type mFuncTable;
};

// Original scripttypeinfo.h template. It adds no data to ScriptTypeInfo:
// the specialization only selects Initialize/IsClass vtable entries.
template <typename T>
class ScriptTypeInfo_T final : public ScriptTypeInfo {
public:
    using TYPE = T;

    void Initialize() override {
        if constexpr (script_type_detail::IsScriptClass<T>::value) {
            script_type_detail::InitializeRegisteredScriptClass(*this);
        }
    }

    bool IsClass() const override { return script_type_detail::IsScriptClass<T>::value; }
};

// Current ScriptEventTypeInfo extends ScriptTypeInfo with the event-name
// map at +0x98 and the event-name xvector at +0xb8. Its complete extent is
// 0xd8 bytes in L2NPC.exe.
class ScriptEventTypeInfo : public ScriptTypeInfo {
public:
    using event_name_table_type = std::map<std::wstring, int>;

    ScriptEventTypeInfo() = default;
    ~ScriptEventTypeInfo() override = default;

    void AddEventName(const wchar_t* name);
    int GetEventId(const wchar_t* name) const;

private:
    event_name_table_type mEventNameTable;
    std::vector<std::wstring> mEventNameList;
};

template <typename T>
class ScriptEventTypeInfo_T final : public ScriptEventTypeInfo {
public:
    using TYPE = T;

    void Initialize() override { script_type_detail::InitializeRegisteredScriptEvent(*this); }
};

// scripttypeinfo.h lines 759-793. The singleton is exactly two fixed pointer
// arrays: 35 script types followed by two event types (0x128 bytes total).
class ScriptTypeTable {
public:
    static ScriptTypeTable* inst();

    void Initialize(bool compileMode);

    void RegType(int id, ScriptTypeInfo* typeInfo);
    ScriptTypeInfo* GetType(int id) const;

    void RegEventType(int id, ScriptEventTypeInfo* typeInfo);
    ScriptEventTypeInfo* GetEventType(int id) const;

private:
    std::array<ScriptTypeInfo*, 35> m_ScriptTypeTable{};
    std::array<ScriptEventTypeInfo*, 2> m_ScriptEventTypeTable{};
};

// scripttypeinfo.h:818-827. The fourth argument exists only so template
// deduction selects Type2Info<T>. The target stores the variable's name,
// byte offset and registered ScriptTypeInfo pointer in a new descriptor.
template <typename T>
void DeclareScriptVariable(ScriptTypeInfo* typeInfo, const wchar_t* name, int offset, T value) {
    static_cast<void>(value);
    auto* const variable = new ScriptVarInfo;
    variable->SetScriptVarName(name);
    variable->Offset(offset);
    variable->Type(ScriptTypeTable::inst()->GetType(Type2Info<T>::Id()));
    typeInfo->AddVarInfo(variable);
}


ScriptTypeInfo& GetBuiltinScriptType(ScriptTypeId id);
