-- Plain PostgreSQL for any SQL console. Edit the VALUES before execution.
-- ip: address advertised to game clients. Use the server's LAN IPv4 for LAN play.
-- inner_ip: source address of L2Server's connection to AuthD (loopback when
-- both applications run on the same machine and L2Server connects to 127.0.0.1).
-- port: L2Server [Setting]/UserPort (default 7777), not AuthGateD's login port.
-- id: L2Server [World]/AuthServerId. The example matches the default configuration.
-- Restart AuthD after registration so its in-memory server list is refreshed.
INSERT INTO authd.server (
    id, name, ip, inner_ip, age_limit, pk_flag, kind, port, region, domain_name
) VALUES (
    1, 'Bartz', '127.0.0.1', '127.0.0.1', 0, 0, 0, 7777, 0, ''
)
ON CONFLICT (id) DO UPDATE
SET ip = EXCLUDED.ip,
    inner_ip = EXCLUDED.inner_ip,
    port = EXCLUDED.port,
    domain_name = EXCLUDED.domain_name
RETURNING id, name, ip, inner_ip, port, domain_name;
