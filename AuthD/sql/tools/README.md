# AuthD server list

The bundled `config.txt` sets `ReadLocalServerList = true`, so AuthD reads the
world list from `etc/serverlist`. It registers world 1 on the same machine.
Edit that file for other addresses.

With `ReadLocalServerList = false`, AuthD reads the `authd.server` table
instead. Edit the values in `register_server.sql` and run it in any SQL
console or with `psql -X -f tools/register_server.sql`:

- `ip`: the address that game clients use. Use the LAN or public address for
  play from other machines.
- `inner_ip`: the address from which L2Server connects to AuthD, `127.0.0.1`
  when both run on the same machine.
- `port`: the L2Server `UserPort` (default 7777), not the AuthGateD port.
- `id`: the L2Server `AuthServerId`.

Running it again updates the network fields and keeps the name and the other
settings of an existing world. Restart AuthD afterwards. Game clients first
connect to AuthGateD (default port 2106). The world list then sends them to
the game server.
