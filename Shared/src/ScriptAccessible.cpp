#include "Shared/src/ScriptAccessible.h"


void ScriptAccessibleEx::CollectFunc(ScriptTypeInfo* typeInfo) {}

void ScriptAccessibleEx::CollectVar(ScriptTypeInfo* typeInfo) {}

ScriptAccessibleEx::~ScriptAccessibleEx() = default;

bool ScriptAccessibleEx::IsNPCInScript() const { return false; }

bool ScriptAccessibleEx::IsNPCMakerExInScript() const { return false; }

bool ScriptAccessibleEx::IsGlobalObjectInScript() const { return false; }

CNPC* ScriptAccessibleEx::CastNPCInScript() { return nullptr; }

l2npc::NpcMakerEx* ScriptAccessibleEx::CastNPCMakerExInScript() { return nullptr; }

GlobalObject* ScriptAccessibleEx::CastGlobalObjectInScript() { return nullptr; }
