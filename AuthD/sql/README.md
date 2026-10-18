# AuthD database migrations

These files create the AuthD database in PostgreSQL. The original AuthD used
three MSSQL databases. AuthD still sends the same ODBC `{CALL ...}` requests,
and `Platform/PostgreSqlOdbc.cpp` passes them to PostgreSQL through libpq.
Accounts are created in the AuthD console. `tools/register_server.sql` adds a
world to the database server list, see [tools/README.md](tools/README.md).

## Automatic application

Set `migrationsDirectory` in AuthD's `config.txt`. The bundled file uses
`../sql`, relative to the configuration file. An empty value turns automatic
migrations off.

```text
migrationsDirectory = "../sql"
```

AuthD applies `001_schema.sql`, `002_routines.sql` and
`003_local_account_creation.sql` to each database it uses. Databases that are
the same physical database share one history in `public.server_migrations`
(application `authd`). AuthGateD has no database of its own.

## Manual application

```sh
psql "$AUTHD_DATABASE_URL" -v ON_ERROR_STOP=1 -f 001_schema.sql
psql "$AUTHD_DATABASE_URL" -v ON_ERROR_STOP=1 -f 002_routines.sql
psql "$AUTHD_DATABASE_URL" -v ON_ERROR_STOP=1 -f 003_local_account_creation.sql
```

Migration `003` adds account creation for the first password login and for
the AuthD console command `account create <username> <password>`.
`AutoCreateAccounts = true` in `config.txt` is the default. Set it to `false`
to require manual account creation. This applies only to local password
logins. Existing accounts and their passwords are never replaced.

## Connections

`config.txt` has three libpq connection strings:

- `gameAccountDBconnStr`: accounts and game data
- `secureServiceDBconnStr`: security service
- `GloActNcsConn`: global activity and schedules

They can point to one database or to separate databases that each have the
migrations. Example:

```text
host=127.0.0.1 port=5432 dbname=lineage2 user=lineage2 password=...
```

## Comments in the SQL files

- `DONOR`: translated from the original `lin2db.sql`.
- `EXE`: required by a call, bind or result that the AuthD executable uses.
- `SEMANTIC` or `PORTABLE`: a PostgreSQL body written for this port, because
  the original MSSQL body was not available.
