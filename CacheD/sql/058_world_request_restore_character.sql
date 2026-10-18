-- Current Cached.exe primary opcode 57 (RequestRestoreCharacter) persistence.
--
-- The current machine fixes one unsigned character ID and no result columns.
-- All eight substantive compatible donor families have the same update.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_setcharacterdeleterestore"(
    p_char_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_data" AS character
    SET "temp_delete_date" = NULL
    WHERE character."char_id" = p_char_id;
$function$;

COMMIT;
