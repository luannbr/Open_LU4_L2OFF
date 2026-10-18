#include "Shared/src/ThreadSafeLibrary.h"

CCreatureSmartIdManager g_CreatureSmartIdManager;
CAirShipSmartIdManager g_AirShipSmartIdManager;

CItemSmartIdManager* ItemSmartIdManager() noexcept { return &g_ItemSmartIdManager; }
