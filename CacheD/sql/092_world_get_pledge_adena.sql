-- Current Cached.exe primary opcode 160 (RequestAgitPledgeAdena).
--
-- The current machine fixes one integer input and one signed-64-bit result.
-- Classic 3.0, modified 287 and Ertheia contain the same substantive body.
-- their bracketed SQL Server procedure identifier has a trailing space that
-- the current call literal omits, so the portable callable uses the exact
-- current spelling without that storage artifact.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_getpledgeadena"(
    p_pledge_id integer
) RETURNS TABLE (
    amount bigint
)
LANGUAGE sql
STABLE
AS $function$
    SELECT COALESCE(SUM(item."amount"), 0)::bigint
    FROM "world"."user_item" AS item
    WHERE item."warehouse" = 2
      AND item."char_id" = p_pledge_id
      AND item."item_type" = 57;
$function$;

COMMIT;
