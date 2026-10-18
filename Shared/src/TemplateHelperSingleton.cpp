#include "Shared/src/TemplateHelper.h"

// Exact Shared process global. Current L2Server Singleton<CParty::Stat>::inst
// at 0x008000cc serializes construction through this CIOCriticalSection.
// current L2NPC and its PDB expose the same owner. The portable
// build keeps the product-neutral singleton lock separate from L2NPC's
// process-owned missing-handler diagnostic in TemplateHelper.cpp.
#if defined(_MSC_VER) && !defined(__clang__)
// The factory registrars of other translation units lock this object during
// CRT startup, and a Windows CRITICAL_SECTION must be constructed first. The
// library initialization group runs before the user objects of every unit.
// It also destroys this lock after them.
#pragma warning(disable : 4073)
#pragma init_seg(lib)
#endif
#if defined(__clang__) && !defined(_WIN32)
// Mach-O static archives can schedule the process factory registrars before
// this translation unit and destroy this lock before other process globals.
// iocs.cpp makes the native backend safe for the early-Enter case. Retaining
// that one infrastructure lock through process exit keeps the target's
// effective MSVC CRT lifetime for late recovered destructors.
__attribute__((no_destroy))
#elif defined(__GNUC__) && !defined(_WIN32)
// GCC honors init_priority across ELF archive members. Constructing the lock
// first also makes its ordinary reverse-order destruction last.
__attribute__((init_priority(101)))
#endif
CIOCriticalSection g_SingletonLock;
