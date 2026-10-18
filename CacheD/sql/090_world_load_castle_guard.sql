-- Current Cached.exe primary opcode 152 (RequestLoadCastleGuard).
--
-- The current machine fixes the one-integer input and eight-integer result
-- ABI. Classic 3.0, modified 287 and Ertheia have the same relational body
-- at that exact width and keep the database's natural row order.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadresidenceguard"(
    p_residence_id integer
) RETURNS TABLE (
    item_id integer,
    npc_id integer,
    guard_type integer,
    can_move integer,
    x integer,
    y integer,
    z integer,
    angle integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        rg."item_id",
        rg."npc_id",
        rg."guard_type",
        rg."can_move",
        rg."x",
        rg."y",
        rg."z",
        rg."angle"
    FROM "world"."residence_guard" AS rg
    WHERE rg."residence_id" = p_residence_id;
$function$;

COMMIT;
