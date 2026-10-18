# Fixed defects of the original servers

This list describes defects of the original server executables that these
sources fix on purpose. Each fix keeps the original code next to it as a
comment or under `#if 0`, so the original behavior stays visible. The
addresses refer to the original executables.

Each entry names where the defect was found, what was wrong, how it is fixed,
the changed source files and the effect on compatible behavior.

## AuthD

### `AUTH-SELECT-001`: a repeated server selection replaces the login ticket

- **Found in:** `L2Authd.exe` (SHA-256
  `d532c60ba8ad29608f68154dc48e6006ce3a7ca4a32bda649124d7cd737f4278`),
  `AboutToPlay` at `0x40ee60` and the selection handler at `0x41E7E0`, together
  with `L2Server.exe` `AuthPlayOk` at `0x4a7654`.
- **Defect:** AuthD keeps no state for a selection in progress. A second
  selection sends another AboutToPlay, and L2Server replaces the pending key
  of the account before it checks the ready-account map. The first ticket then
  fails the comparison.
- **Fix:** AuthD reserves the server selection for each client session under
  a mutex. Requests while a selection is pending, or after PlayOk, are ignored.
  PlayFail releases the reservation. AuthD stores the selected world and
  session before it sends AboutToPlay.
- **Source:** `AuthD/src/ClientSession.cpp`, `AuthD/src/ClientSession.h`.
- **Effect:** client opcodes, encryption, ticket contents and the strict
  ticket check of L2Server are unchanged.

### `AUTH-SPRINTF-001`: unbounded formatting into fixed buffers

- **Found in:** `CDBConn::Execute`, `ExecuteIndirect`, `ExecuteInsert`,
  `ExecuteInsertIndirect`, `ExecuteDelete`, the connection-string loading of
  `DBEnv`, `sendMail` and the `SendMail` SMTP packets.
- **Defect:** `sprintf` and `vsprintf` write SQL statements, the connection
  string, mail addresses, the mail title and the mail body into fixed buffers
  without a size limit.
- **Fix:** `runtime::BoundedFormat` writes at most the buffer size and reports
  text that does not fit. Such a SQL statement is not sent and takes the
  existing failure path. A connection string that does not fit is logged and
  the connection is not opened. An SMTP line that does not fit ends the send
  like a socket error. The mail body is sent directly, with the same bytes.
- **Source:** `AuthD/src/DBConn.cpp`, `AuthD/src/Exception.cpp`,
  `AuthD/src/SendMail.cpp`, `Shared/src/Runtime/BoundedFormat.h`.
- **Effect:** text that fits is formatted and sent unchanged.

### `AUTH-FATIGUE-FMT-001`: a configuration path used as a format string

- **Found in:** `L2Authd.exe` `IFatigue::InitializeDll` at `0x00446F70`.
- **Defect:** `config.logDirectory` is passed to `snprintf` as the format. A
  `%` in the path, or a missing `logDirectory` key (`nullptr`), is undefined
  behavior.
- **Fix:** format with `"%s"` and use an empty path when the key is missing.
- **Source:** `AuthD/src/IFatigue.cpp`.
- **Effect:** ordinary paths are copied unchanged.

### `AUTH-CONFIG-DELETE-001`: mismatched release of the configuration buffer

- **Found in:** `L2Authd.exe` `Config::Load` at `0x00420FB0`, release call at
  `0x004213F3`.
- **Defect:** `LoadBinary` allocates the file buffer with `operator new[]`,
  but `Config::Load` frees it with the scalar `operator delete`. This is
  undefined behavior.
- **Fix:** free the buffer with `operator delete[]`.
- **Source:** `AuthD/src/Config.cpp`.
- **Effect:** none for configuration parsing.

## AuthGateD

### `GATE-DECRYPT-001`: joined client frames are decrypted at the wrong offset

- **Found in:** `AuthGateD.exe` (SHA-256
  `d02bf1dc9fb6e99cecce804a26f956a53078934a721ffdb8c09696a0208e7677`),
  `CClientSocket::OnRead` at `0x4077f0`.
- **Defect:** the code at `0x407891` decrypts at `buffer + 2`, while frame
  dispatch uses the offset of the current frame. A second frame in the same
  read, or a body that continues after the buffer was compacted, is decrypted
  in the wrong place.
- **Fix:** decrypt each complete body at its real offset in the receive
  buffer.
- **Source:** `AuthGateD/src/ClientSocket.cpp`.
- **Effect:** reads with one frame behave as before. Split and joined frames
  now decode correctly.

## L2Server

### `BUILDER-SETCLASS-001`: `//setclass` saves the class of the Builder

- **Found in:** `L2Server.exe` `BuilderCmd_setclass` at `0x004e8e40`.
- **Defect:** after `ChangeClass` on the selected target, the handler calls
  `UpdateMentorByLevelUp` on the Builder and builds the CacheD `DxSetClass`
  request from the DB ID, sub job and class of the Builder. The new class of
  the target is not saved, and the current class of the Builder is saved
  again.
- **Fix:** the mentor update and the `DxSetClass` request use the target.
- **Source:** `L2Server/src/BuilderCmdHandler.cpp`.
- **Effect:** the new class of the target survives a relog. The state of the
  Builder no longer changes.

### `BUILDER-RESETSKILL-001`: `//reset_skill` rebuilds the skills of the Builder

- **Found in:** `L2Server.exe` `BuilderCmd_reset_skill` at `0x0054a020`.
- **Defect:** the handler deletes the acquired skills of the target, then
  calls `AcquireSkills` with the race skills of the Builder and
  `AcquireAllHonorsSkill` on the Builder. The target is left without its race
  and honor skills.
- **Fix:** the target gets its own race and honor skills again, as
  `//reset_skill_me` does for the Builder.
- **Source:** `L2Server/src/BuilderCmdHandler.cpp`.
- **Effect:** the skills of the Builder no longer change.

### `BUILDER-ALLIANCE-MEMBER-001`: `//delete_alliance_member` refuses every member

- **Found in:** `L2Server.exe` `BuilderCmd_delete_alliance_member` at
  `0x005597a4`, membership check at `0x00559b23`..`0x00559b3b`.
- **Defect:** the check compares the alliance DB ID of the pledge with the
  inherited `CObject` DB ID of `CAlliance`, which the constructor at
  `0x004796f0` leaves at zero. A real member never matches, so the command
  always reports `pledge is not alliance member`.
- **Fix:** compare with the alliance DB ID stored in the alliance data.
- **Source:** `L2Server/src/BuilderCmdHandler.cpp`.
- **Effect:** the command now removes the named member pledge.

### `BUILDER-ITEMRESTRICT-001`: `//set_itemrestrict off` cannot be reached

- **Found in:** `L2Server.exe` `SetItemRestrict` at `0x005259ac`.
- **Defect:** the `off` branch compares a separate zero-filled buffer that the
  parser never fills. So `off` prints the usage line, and the restriction
  stays on until a restart. The text `itemrestrict off success` exists in the
  executable but is never sent.
- **Fix:** compare the parsed token with `off`.
- **Source:** `L2Server/src/BuilderCmdHandler.cpp`.
- **Effect:** `off` now turns the restriction off. `on` is unchanged.

### `SETITEM-SLOT-001`: uninitialized set item slot

- **Found in:** the original parser action for set items.
- **Defect:** `ParseSetItemSlot` returns an uninitialized `SlotType` for a
  slot name other than chest, head, gloves, legs or feet.
- **Fix:** an unknown name returns `ST_NONE`.
- **Source:** `L2Server/src/ParserSetItem.y`.
- **Effect:** none for the included `setitemgrp` data, which uses only the
  five known names.

### `GG-PATH-001`: unbounded copy of the GameGuard protocol directory

- **Found in:** the `GameGuardServerLibCompat` initializer.
- **Defect:** the configured protocol directory is copied into a 0x104-byte
  buffer with `strcpy` or `sprintf`.
- **Fix:** copy with `snprintf` and build each protocol path as a
  `std::string`.
- **Source:** `L2Server/src/External/GameGuardServerLibCompat.cpp`.
- **Effect:** paths that fit are unchanged.

### `GRADE-SKILL-POSTERASE-001`: vector access after erase

- **Found in:** `L2Server.exe` `CUserSocket::PopGradeSkillFromUser` at
  `0x00c165e4`.
- **Defect:** after `_DeleteAbnormalStatus` erases the matched
  `m_InfectedSkills` element, the function reads the same vector position
  again for the audit log. It sees the next entry, which has moved down, or
  stale bytes of the erased one.
- **Fix:** the function uses the moved entry when one exists, and the saved
  erased entry when the erased one was the last.
- **Source:** `L2Server/src/UserSocket.cpp`.
- **Effect:** the log fields stay the same, and the cleanup no longer reads an
  erased element.

## L2Server and NASC

### `STATS-OOB-001`: out-of-bounds `CStats` comparison

- **Found in:** `L2Server.exe`, `CStats::IsSameValue` inlined into
  `CSharedCreatureData::SetStats` at `0x00a52a48`. The same defect is in
  `NASC64_2015.exe` at `0x007b84f0`.
- **Defect:** the loop uses the 32-byte `sizeof(m_N)` as an element count, so
  it compares 32 `int32` values although `m_N` has only eight. It reads 96
  bytes past both `CStats` objects.
- **Fix:** compare exactly `std::size(m_N)` elements.
- **Source:** both `CStats::IsSameValue` bodies in
  `Shared/src/SharedFactory.cpp`.
- **Effect:** the result depends only on the eight stat values. It can no
  longer differ because of nearby memory or fail at a page boundary.

## L2NPC

### `SP-HANG-001`: endless random route selection

- **Found in:** `L2NPC.exe` `CNPC::dMoveSuperPoint` at `0x004a79ac` and
  `CNPC::dMoveFormation` at `0x0049897c`.
- **Defect:** both methods pick random destination nodes forever when the
  source node has no route object.
- **Fix:** check the map first. If no destination can satisfy the original
  loop, report the invalid graph and fail the desire. Otherwise run the
  original random loop unchanged.
- **Source:** `L2NPC/src/NPC.cpp`.
- **Effect:** valid graphs keep the same random calls and distribution. Only a
  graph on which the original loop could never end now fails. Empty but
  existing route lists still count as selectable, like in the original.

### `SP-PARSE-001`: unchecked `SuperPoint.bin` parser

- **Found in:** `L2NPC.exe` `CSuperPointManager::Load` at `0x0053d204`.
- **Defect:** counts from the file move an unchecked read position. The name
  length can overflow a stack buffer of 9999 UTF-16 code units plus the
  terminator, and failed allocations of graphs, paths and nodes are used
  afterwards.
- **Fix:** read through a position that knows the end of the data, check all
  counts against the remaining bytes, enforce the name size, check each
  allocation, reject extra bytes at the end and publish only complete graphs.
- **Source:** `L2NPC/src/SuperPoint.cpp`.
- **Effect:** the included valid `SuperPoint.bin` loads as before. A damaged
  or short file now fails cleanly and publishes no partial graph.

### `SCRIPT-NULL-001`: `TelPos` null dereference

- **Found in:** `L2NPC.exe` constructors at `0x0054b24c` and `0x0054b304`.
  The original parser actions `0x1b` and `0x1e` pass a null last argument.
- **Defect:** both constructors always read the optional vector of residence
  IDs.
- **Fix:** copy the residence IDs only when the vector exists. A null vector
  means the grammar form without a residence restriction.
- **Source:** `L2NPC/src/ScriptTypeInfo.h`.
- **Effect:** lists keep the original filtering of zero IDs. The two valid
  forms without a list no longer crash.

### `LEAK-INT-001`: leaked copy of a spawn name

- **Found in:** `L2NPC.exe` `RequestNPCSpawn` at `0x0052f320`, copy near
  `0x0052f5be`.
- **Defect:** a copy made with `calloc` is passed to the copying
  `CNpcSpawnDefine::SetName` and never freed.
- **Fix:** pass the existing name buffer directly to the copying setter.
- **Source:** `L2NPC/src/ServerSocket.cpp`.
- **Effect:** the stored name is the same, and spawn requests no longer leak
  memory.

## Shared code

### `GEO-NULL-001`: unchecked first floating cell

- **Found in:** `L2NPC.exe` `CGeoData::MoveStraightFloating` at `0x00433fa8`.
- **Defect:** a failed first `GetBaseCell` is carried into the first sight
  check across cells and dereferenced.
- **Fix:** return a movement failure at once when the first cell is missing.
- **Source:** `Shared/src/GeoData.cpp`.
- **Effect:** movement over loaded geodata is unchanged. A floating or flying
  movement outside the loaded geodata now fails instead of crashing.

### `GEO-SILENT-001`: ignored cell index failure

- **Found in:** `L2NPC.exe` `CGeoData::GetBaseCellFromSector` at
  `0x0043329c`.
- **Defect:** the function ignores `GetCellIndexes == false` and passes
  unwritten values to `GetBaseCell`.
- **Fix:** require a successful index lookup, report it once and return
  `nullptr` on failure.
- **Source:** `Shared/src/GeoData.cpp`.
- **Effect:** valid zone lookups are unchanged. A broken zone index can no
  longer return unrelated geometry.

### `UTIL-INIT-001`: unchecked file status

- **Found in:** `L2NPC.exe` `LoadBinaryAbsolute` at `0x00545684`.
- **Defect:** `_wstat64i32` can fail, and the function still uses the
  uninitialized `st_size` for the allocation, the read and the reported size.
- **Fix:** initialize the status, require a successful status query and a
  valid size, close the open file and return failure otherwise.
- **Source:** `Shared/src/Utils.cpp`.
- **Effect:** successful loads are unchanged. A failed status query now fails
  cleanly.

### `RWLOCK-INIT-001`: unchecked semaphore release

- **Found in:** `L2NPC.exe` `RWLock::WriteUnlock` at `0x0050cc54` and
  `Cached.exe` at `0x005292d0`.
- **Defect:** the result of `ReleaseSemaphore` is ignored, and
  `previousCount` is read although a failed call does not write it.
- **Fix:** initialize the value, check the result before reading it and
  report a failed release.
- **Source:** `Shared/src/RWLock.cpp` and the product `ProcessRuntime`
  adapters.
- **Effect:** successful unlocks are unchanged. A failed unlock is reported
  and no longer reads an unset value.

## CacheD database

### `CACHED-DAILY-COUNT-001`: `lin_ClearDailyCount` clears every row

- **Found in:** the original `lin_ClearDailyCount` procedure, kept in
  migration `144_world_attribute_dual_br_mentee.sql`.
- **Defect:** the filter is
  `where @char_id = @char_id and @event_id = @event_id`, which is true for
  every row. One `//setfreeparam post` deletes the daily counts of all
  characters.
- **Fix:** migration `201_world_clear_daily_count_filter.sql` filters on the
  `char_id` and `event_id` columns. The signature and the result column are
  unchanged, and migration 144 is not edited.
- **Source:** `CacheD/sql/201_world_clear_daily_count_filter.sql`.
- **Effect:** only the row of the requested character and event is deleted.
