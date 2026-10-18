-- Current Cached.exe primary opcode 148 (RequestSetNextCastleSiege).
--
-- The current handler fixes the two unsigned 32-bit inputs and their order:
-- next-war time first, castle identifier second. Classic 3.0, modified 287,
-- Ertheia and High Five donors all keep the same single-row update effect.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_updatecastlenextwar"(
    p_next_war_time integer,
    p_castle_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."castle"
    SET "next_war_time" = p_next_war_time
    WHERE "id" = p_castle_id;
$function$;

COMMIT;
