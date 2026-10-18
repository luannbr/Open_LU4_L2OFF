# Configuration files

The default configuration of each application is in its `etc/` directory.
CMake copies these files beside the executable. Keep your own copy outside
`build/` and select it with `--config-dir`, because CMake overwrites the
copied defaults when it configures the project again.

A comment above each option explains it. `Shipping:` comments show the value
of the original deployment. They are for reference and are not defaults.

| Application | Files |
| --- | --- |
| AuthD | `etc/config.txt`, `etc/serverlist`, `etc/fcmsdk.ini` |
| AuthGateD | `etc/config.txt`, optional `BlockIPs.txt` |
| CacheD | `l2cache.ini`, `cached-postgresql.ini`, `secure.ini`, `perfmon.ini`, `exception.ini` |
| L2Server | `l2server.ini`, `secure.ini`, `perfmon.ini`, `NCGuard/penalty_pattern.ini`, `NCGuard/ncgsvrcfg.ini`, `SerialKey.txt`, `serverdata.dat` |
| L2NPC | `l2npc.ini`, `secure.ini`, `perfmon.ini` |

## AuthD and AuthGateD

`config.txt` has one `key = value` option per line. Keys are case-sensitive,
and strings are quoted. Some optional AuthD strings stay commented out: an
empty string is not the same as a missing key and can turn on another code
path, for example the old SMTP mail.

`createDatabase = true` creates a missing database before the migrations run.
`ReadLocalServerList = true` reads the world list from `etc/serverlist`, so a
new database needs no `authd.server` row.

## CacheD

`cached-postgresql.ini` holds the PostgreSQL connections. Each ID in
`SQL/DBList` needs `connStr(id)` and `schema(id)`. `cached-postgresql.ini.example`
shows all route families. `MigrationsDirectory` applies to every route. An
optional `MigrationsDirectory(id)` selects another directory for one route,
and an empty value turns migrations off for it. `CreateDatabase=true` creates
a missing database of a route that has migrations.

`Setting/IOBufferCount` is the one option that is not taken from
`--config-dir`: the I/O layer reads it from `./l2cache.ini` in the working
directory.

## L2Server

- `AuthServerId` defaults to `WorldId`.
- `LogD/FileDuration` must be positive.
- `AcceptEscapeCommand` is read, but the value is not used.
- `MultiLang=true` selects the HTML catalog by the player's language, see the
  [README](../README.md).
- On Windows the NCGuard loader also reads `update/ncgsdllver` from
  `NCGuard/ncgsvrcfg.ini`. The other NCGuard settings belong to that external
  provider, and the server has no defaults for them.

## L2NPC

L2NPC reads geodata from the shared memory that L2Server creates, so the
shared memory names in `l2npc.ini` must match L2Server. `GeoDataCellOffest` is
the spelling that the server reads. The `map/directory`, `LogD/directory`,
`GeoData/directory`, `AiObj` and `SharedMemory/UserMapDensity` entries are
kept from the original file, but the current server does not use them.

## Paths

Script, HTML and geodata directories are taken from the command line first,
then from `[Paths]` in the main INI file, and then from the original
`../script`, `../html_<LANG>` and `../GeoData` paths. Relative `[Paths]`
values start beside the INI file. See the [README](../README.md) for examples.

## Numbered options

Some options form numbered lists. Add entries when you raise the count:

- `PAIP_N`/`PAPort_N` and `GPProxyIP_N`/`GPProxyPort_N` follow the configured
  connection count.
- `PerfNObjectName`, `PerfNInstanceName` and `PerfNCounterName` follow
  `PerfMon/Count`, starting at 1.
- NCGuard `PATTERNN` follows `HEADER/COUNT`, starting at 0.

Unused entries do not turn on a feature.

## Reference files

`CacheD/etc/exception.ini`, `L2NPC/etc/perfmon.ini` and `AuthD/etc/fcmsdk.ini`
are kept from the original deployment. The current applications do not read
them. `SerialKey.txt` holds the host key (see the README) and `serverdata.dat`
is the binary NewInspector data. Neither file accepts comments.
