-- Current Cached.exe primary opcode 24 (RequestSetAgitOwner) persistence.
--
-- The handler at 0x0048bcb8 fixes the two-input ordered ABI as
-- (pledge_id integer, agit_id integer). Eight substantive full-arity donor
-- definitions agree on the single relational effect below.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_setagitowner2"(
    p_pledge_id integer,
    p_agit_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."agit"
    SET "pledge_id" = p_pledge_id
    WHERE "id" = p_agit_id;
$function$;

COMMIT;
