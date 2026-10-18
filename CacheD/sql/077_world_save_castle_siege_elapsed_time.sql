-- Current Cached.exe primary opcode 103 persistence.
--
-- Current machine code fixes the two integer inputs and reverses wire order
-- when binding them as (siege_elapsed_time, castle_id). All substantive
-- compatible donor families agree on the single castle update.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_savecastlesiegeelapsedtime"(
    p_siege_elapsed_time integer,
    p_castle_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."castle"
    SET "siege_elapsed_time" = p_siege_elapsed_time
    WHERE "id" = p_castle_id;
$function$;

COMMIT;
