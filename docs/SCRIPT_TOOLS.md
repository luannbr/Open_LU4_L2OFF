# NASC

`NASC` is a portable version of the original Windows compiler
`NASC64_2015.exe`. It compiles the NPC AI sources in `script/ai/src` into
`script/ai.obj` and builds on macOS, Linux and Windows. Its sources are in
`NASC/`.

## Build

NASC is part of the build presets:

```sh
cmake --preset native-release
cmake --build --preset native-release
```

The executable is `build/<preset>/NASC/NASC`. `L2_BUILD_NASC=OFF` removes it
from a build. NASC needs a 64-bit host. The MinGW build links the C++ runtime
statically, so `NASC.exe` needs no MinGW DLLs.

## AI build steps

1. The original `TotalChecker2015.exe` in `script/` writes the 13 PCH files,
   for example `npc_pch.txt` and `skill_pch2.txt` (`TotalChecker2015 pre`).
2. `TotalChecker2015 qcompile` merges `questdata.txt` into the AI files of
   `ai/` and writes the merged `NAME.ai` files into `script/`.
3. The macro step writes the expanded sources to `script/ai/src`. The original
   pipeline uses `MacroDataChecker.exe` for this step. You can also edit
   `script/ai/src` directly.
4. `NASC` compiles `script/ai/src` into `ai.obj`. L2NPC loads
   `script/ai.obj` at startup and on `.aireload`.

Steps 1 to 3 use original Windows tools. The PCH files and `script/ai/src` in
`script/` are already up to date, so NASC can be used without them.

## NASC

```sh
NASC --root script --output /tmp/nasc-output
cp /tmp/nasc-output/ai.obj script/ai.obj
```

NASC reads the module list from `ai/data/compile_list.dat`, the PCH files and
`ai/src/*.ai`. It never changes these files. The output directory must be new
or empty and receives `ai.obj`, `ai/obj/*.obj` and `ai/sym/*.sym`. Existing
files are never overwritten. Other options:

| Option | Effect |
| --- | --- |
| `--module NAME.ai` | build one module and its `#include` dependencies |
| `--no-merge` | write the module files, but no `ai.obj` |
| `--dry-run` | print the build plan |
| `--inventory` | list the AI modules and `compile_list.dat` |
| `-version PATH` | print the version header of an `ai.obj` |
| `-genhelp` | write `Help/index.txt` and the type skeletons |

The original switches `-qcompile`, `-rebuild`, `-qrebuild`, `-nousenotepad`,
`-skip` and `-c PATH` are accepted. NASC always builds into the separate
output directory. It does not update the timestamps in `compile_list.dat` and
does not start Notepad or the companion checkers. `-c` together with `-q` or
`-m` would start external Windows tools, so NASC rejects it.

### Field offsets

L2NPC finds script fields by the offsets in `ai.obj`. It uses its own tables
for this (`CNPC::CollectVar` in `L2NPC/src/NPC.cpp` and the maker and global
object types). `NASC64_2015.exe` was built with older headers, and 31 of its
field offsets are smaller than the offsets in the shipped `ai.obj` and in
L2NPC, for example `sm` (1816 instead of 1824) and `m_nAi` (6696 instead of
6864). The portable NASC uses the L2NPC values. These entries are marked in
`NASC/src/ScriptMetadata.cpp`. With the original values, L2NPC could not
resolve these fields.

Verification on 2026-10-13: the shipped 2018 script tree compiles to an
`ai.obj` that is byte-identical to the shipped file (SHA-256
`55fb894c214e110d975d4db16ffded6684572b206ee0743ebbc994eafa77a005`). The
included `script/` tree compiles without diagnostics. Its result has the same
instructions as `script/ai.obj`. Only the explanation comments that were added
to that file by hand are missing.

The Windows executable was cross-built with MinGW. It has not been run on
Windows.
