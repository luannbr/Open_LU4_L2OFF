# CacheD database migrations

These files create and update the CacheD database. Builder rights are set in
the CacheD console, see the [README](../../README.md).

## Automatic application

Set `MigrationsDirectory` in the `[PostgreSQL]` section of
`cached-postgresql.ini` to this directory. The bundled file uses `../sql`,
relative to the configuration file. CacheD then applies all new migrations at
startup, before it opens its database pools. `MigrationsDirectory(<DBList id>)`
selects another directory for one database route, and an empty value turns
migrations off for that route.

Applied files are recorded in `public.server_migrations` (application
`cached`), once per physical database. Do not change an applied file. Add a new
migration instead. A database that was filled by hand, without this history, is
not detected automatically.

## Order

The number prefixes do not give the order. Use `migrations.order`, which the
startup runner reads. On an empty database the order starts with:

1. `000_postgresql_mssql_temporal_primitives.sql`
2. `000_postgresql_mssql_text_primitives.sql`
3. `001_world_stable_tables.sql`
4. `002_world_selected_divergent_tables.sql`
5. `005_world_startup_core_tables.sql`
6. `003_world_item_auction_procedures.sql`
7. `004_world_startup_procedures.sql`
8. `006_account_startup.sql` to `201_world_clear_daily_count_filter.sql`, in
   number order

`004` uses tables that `005` creates, so a plain alphabetical order fails on
an empty database. Add the name of every new migration to `migrations.order`.
`cmake --install` copies the order file with the SQL files.

## Notes

- `200_world_dbjob_reporting.sql` returns the result of a DBJob cleanup as a
  number: `1` executed, `0` already executed, `-1` unsupported type. Errors
  still roll back the operation. An executed job may remove zero rows.
- The comments in some files name the original database versions (for
  example Classic 3.0, Ertheia or protocol 287) that a table or procedure body
  comes from.
