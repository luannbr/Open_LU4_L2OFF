# Lineage II server sources

Portable C++20 sources of the five Lineage II server applications:

| Application | Role |
| --- | --- |
| `AuthD` | account authentication and the server list |
| `AuthGateD` | encrypted client gateway in front of AuthD |
| `CacheD` | database cache between the game servers and PostgreSQL |
| `L2Server` | game world server |
| `L2NPC` | NPC AI server |

The applications build on macOS, Linux and Windows and keep their data in
PostgreSQL. Build requirements for each platform are in
[REQUIREMENTS.md](REQUIREMENTS.md) (English and Russian).

## Contents

- `AuthD/`, `AuthGateD/`, `CacheD/`, `L2NPC/`, `L2Server/`: application
  sources (`src/`) and default configuration (`etc/`). AuthD and CacheD also
  keep their PostgreSQL migrations in `sql/`.
- `L2Core/`, `Shared/`, `Protocol/`: libraries used by every application.
- `cmake/`: CMake modules. `CMakePresets.json` has the build presets.
- `NASC/`: portable version of the original NASC compiler. It compiles the
  NPC AI sources into `script/ai.obj`. See
  [docs/SCRIPT_TOOLS.md](docs/SCRIPT_TOOLS.md).
- `script/`: server scripts and game data. It also keeps the original
  Windows data checkers and editors, including `TotalChecker2015.exe`.
- `html_USA/`, `html_KOREA/`: HTML dialogs for each client language.
- `GeoData/`: geodata.
- `docs/`: the configuration files, the AI compiler, the fixed defects of the
  original servers and the Builder (GM) command reference
  `docs/builder/index.html`.

## Build

Run CMake from this directory. The product subdirectories are not separate
projects. For a server, build the optimized release:

```sh
cmake --preset native-release
cmake --build --preset native-release
```

On Windows with Visual Studio 2022:

```sh
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release
```

| Preset | Use |
| --- | --- |
| `native-release` | optimized build for a server on macOS or Linux |
| `native-relwithdebinfo` | optimized build with debug information |
| `native-debug` | debug build for development |
| `windows-msvc` | Visual Studio 2022, with the build presets `windows-msvc-release` and `windows-msvc-debug` |
| `mingw-release` | MinGW cross build for Windows, only L2NPC, CacheD and NASC |

All presets also build the AI compiler `NASC`.

Each application and its default configuration go to
`build/<preset>/<Application>`, for example `build/native-release/L2Server`.
Visual Studio adds the configuration: `build/windows-msvc/Release/L2Server`.
`cmake --install build/native-release` creates a deployment copy in
`build/native-release/stage`.

L2NPC, CacheD and L2Server generate their script parsers with GNU Bison 3.5+
and re2c 3.0+ at build time. Set `PG_PREFIX` when CMake cannot find your
PostgreSQL installation. On Windows, CMake copies the PostgreSQL client DLLs
beside each executable.

Useful CMake options:

| Option | Default | Effect |
| --- | --- | --- |
| `L2_BUILD_AUTHD`, `L2_BUILD_AUTHGATED`, `L2_BUILD_CACHED`, `L2_BUILD_L2NPC`, `L2_BUILD_L2SERVER` | `ON` | select the applications |
| `L2_BUILD_NASC` | `ON` | builds the AI compiler |
| `L2_MEMORY_BACKEND` | `system` | `legacy` selects the original allocator |
| `L2_ENABLE_IPO` | `OFF` | IPO/LTO for Release and RelWithDebInfo |
| `L2_ENABLE_GUARD_PROFILING` | `OFF` | timing of `L2_GUARD_FUNCTION` scopes |
| `L2_ENABLE_STARTUP_DIAGNOSTICS` | `OFF` | detailed geodata startup probes (slow startup) |

## Configuration and data directories

Every application accepts `--config-dir PATH`. Keep your edited configuration
outside `build/`: CMake copies the defaults from `<Application>/etc` again
when it reconfigures.

```sh
./AuthD --config-dir /srv/lineage/config/AuthD
./AuthGateD --config-dir /srv/lineage/config/AuthGateD
./CacheD --config-dir /srv/lineage/config/CacheD
./L2Server --config-dir /srv/lineage/config/L2Server
./L2NPC --config-dir /srv/lineage/config/L2NPC
```

L2Server, L2NPC and CacheD read the game data. Select the directories on the
command line:

```sh
./CacheD --scripts-dir /srv/lineage/script
./L2Server --scripts-dir /srv/lineage/script --html-dir /srv/lineage \
    --geo-dir /srv/lineage/GeoData
./L2NPC --scripts-dir /srv/lineage/script --geo-dir /srv/lineage/GeoData
```

or in the `[Paths]` section of `l2cache.ini`, `l2server.ini` and `l2npc.ini`:

```ini
[Paths]
ScriptsDirectory=/srv/lineage/script
HtmlDirectory=/srv/lineage
GeoDirectory=/srv/lineage/GeoData
```

The command line has priority over the INI value. Relative INI values start
beside the INI file, and relative command-line values start at the working
directory. Without either value the original `../script`, `../GeoData` and
`../html_<LANG>` paths relative to the working directory are used.

`HtmlDirectory` is the HTML root. With `MultiLang=true` (the default in
`l2server.ini`) L2Server reads `html_USA`, `html_KOREA` and so on inside that
root, so in the example above it reads `/srv/lineage/html_USA`.

More details: [docs/CONFIGURATION.md](docs/CONFIGURATION.md).

## Database

The server needs PostgreSQL 18 or newer with ICU support. CacheD and AuthD
apply their SQL migrations automatically at startup:

- CacheD: `MigrationsDirectory` in `cached-postgresql.ini`. The default is
  `../sql`, relative to that file. Set the connection strings `connStr(...)`
  there too.
- AuthD: `migrationsDirectory` and the connection strings in `config.txt`.
  The default is `../sql`.

With `CreateDatabase=true` (CacheD) and `createDatabase = true` (AuthD), a
missing database is created first; the database user then needs `CREATEDB`.
The bundled configuration enables this and uses the local login
`postgres`/`root`. Change these values for your server.

Applied migrations are recorded in `public.server_migrations`. Do not change
an applied file. Add a new migration instead. See `AuthD/sql/README.md` and
`CacheD/sql/README.md` for the details.

## Start order

Start the applications in this order: AuthD, AuthGateD, CacheD, L2Server,
L2NPC. L2NPC uses the geodata shared memory that L2Server creates.

## Accounts and Builder rights

Both are set in the server consoles, no SQL is needed.

Create an account in the AuthD console:

```text
account create testuser ChangeMe123
```

AuthD also creates a missing account at the first password login. Set
`AutoCreateAccounts = false` in AuthD's `config.txt` to turn this off.

Give Builder (GM) rights in the CacheD console. Log the account out
completely, including the character selection screen, before the command:

```text
account builder testuser 1
account builder testuser 0
```

Levels `1` to `9` grant builder access, and `0` removes it. The level applies
to the existing and future characters of the account in this world.

AuthD and CacheD also accept `help`, `quit` and `exit`. L2NPC accepts
`.status`, `.netstat`, `.aireload <dev|live>`, `.help` and `.quit`.

AuthD reads the world list from `AuthD/etc/serverlist`. To keep it in the
database instead, see [AuthD/sql/tools/README.md](AuthD/sql/tools/README.md).

## Logs

The debug switches change diagnostic records only:

| Application | File | Option | Default |
| --- | --- | --- | --- |
| CacheD | `l2cache.ini` | `[Logging] Debug=0/1` | `0` |
| L2Server | `l2server.ini` | `[Logging] Debug=0/1` | `0` |
| L2NPC | `l2npc.ini` | `[Setting] ScriptDebugLog=false/true` | `false` |
| AuthD | `config.txt` | `DebugLogging = 0/1` | `1` |
| AuthGateD | `config.txt` | `DebugLogging = 0/1` | `1` |

## Host-bound SerialKey

CacheD and L2Server keep the original host-bound serial check, but it is off
by default (`EnableSerialKeyValidation=false` in `[Portable]` of `l2cache.ini`
and `l2server.ini`). `L2Server/etc/SerialKey.txt` is empty. To use the check,
generate a key on the server machine:

```sh
cmake --build --preset native-release --target l2server_serial_key
./build/native-release/L2Server/l2server_serial_key --output L2Server/etc/SerialKey.txt
```

Then reconfigure, so CMake copies the new file beside L2Server.
