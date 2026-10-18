#pragma once

// PDB: l2core/fileutils.cpp. The Windows build receives a HANDLE. The
// portable boundary receives the corresponding open std::FILE as void*.
void hprintf(void* file, const wchar_t* format, ...);
