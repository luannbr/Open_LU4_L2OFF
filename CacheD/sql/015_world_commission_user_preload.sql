-- Current Cached.exe CUserDb::LoadCommissionUser persistence ABI.
--
-- The current caller has no inputs, binds one int32 output and keeps every
-- returned row. Classic 3.0, modified-287 and Ertheia contain byte-identical
-- donor procedure bodies. All select char_id where buyer_id=0.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadcommissionuser"()
RETURNS TABLE("char_id" integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT "c"."char_id"
    FROM "world"."commission" AS "c"
    WHERE "c"."buyer_id" = 0
$function$;

COMMIT;
