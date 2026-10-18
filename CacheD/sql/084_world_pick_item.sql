-- Current Cached.exe primary opcode 140 (RequestPickItem) database slice.
-- The current owner binds x, y and z in that order.  Classic 3.0, 287 and
-- Ertheia donors unanimously keep the same coordinate-keyed delete body.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_deleteresidenceguard"(
    p_x integer,
    p_y integer,
    p_z integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."residence_guard"
    WHERE x = p_x
      AND y = p_y
      AND z = p_z;
$function$;

COMMIT;
