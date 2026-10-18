#pragma once

#include "Shared/src/BaseStructure.h"

extern bool g_bGmNoResOn;
extern int g_GmNoRestartDistance;
extern FVector g_GmNoRestartPosition;
extern FVector g_GmRestartPosition;

bool IsGmNoRestartPosition(FVector position);
FVector GetGMRestartPoint();
