-- Current Cached.exe primary opcode 102 persistence.
--
-- Current machine code fixes the two integer inputs and reverses wire order
-- when binding them as (hp, object_id). All substantive compatible donor
-- families agree on the single object_data update.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_saveobjecthp"(
    p_hp integer,
    p_object_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."object_data"
    SET "hp" = p_hp
    WHERE "id" = p_object_id;
$function$;

COMMIT;
