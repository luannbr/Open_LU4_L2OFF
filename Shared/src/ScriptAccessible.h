#pragma once

#include <cstddef>

namespace l2npc {
class NpcMakerEx;
}


class GlobalObject;
class ScriptTypeInfo;
class CNPC;

// Exact 64-bit ScriptAccessibleEx vtable surface. The target stores no data
// after the vptr, so derived script-visible records start at offset 0x08.
class ScriptAccessibleEx {
public:
    virtual void CollectFunc(ScriptTypeInfo* typeInfo);
    virtual void CollectVar(ScriptTypeInfo* typeInfo);
    virtual ~ScriptAccessibleEx();

    virtual bool IsNPCInScript() const;
    virtual bool IsNPCMakerExInScript() const;
    virtual bool IsGlobalObjectInScript() const;
    virtual CNPC* CastNPCInScript();
    virtual l2npc::NpcMakerEx* CastNPCMakerExInScript();
    virtual GlobalObject* CastGlobalObjectInScript();
    virtual ScriptAccessibleEx* GetScriptAccessibleEx() const = 0;
};
