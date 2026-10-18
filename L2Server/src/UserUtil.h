#pragma once

#include "Shared/src/ThreadSafeLibrary.h"

class CCreature;
class User;

namespace UserUtil {

void RegisterToRandomUser(int userId);
void UnregisterFromRandomUser(int userId);
CSPointer<User> FindRandomUser();
CSPointer<User> CastBuffToRandomUser(CCreature* caster, int skillId, int skillLevel, int skillSubLevel);

} // namespace UserUtil
