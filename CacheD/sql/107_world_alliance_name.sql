-- Current Cached.exe primary opcode 218 persistence.
--
-- CAllianceDb::ModifyAllianceName 0x0044ed44 fixes the two-input callable
-- ABI and updates its cached object only after successful execution. Classic
-- 3.0, modified 287 and Ertheia keep the same compatible single-row update.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_modifyalliancename"(
    p_alliance_name text,
    p_alliance_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."alliance" AS alliance_row
    SET "name" = p_alliance_name
    WHERE alliance_row."id" = p_alliance_id;
$function$;

COMMIT;
