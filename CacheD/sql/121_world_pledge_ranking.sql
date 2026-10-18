-- Current Cached.exe CPledgeRanking::Load at 0x0051f384 and extended ID 61.
--
-- The current machine fixes the no-input/two-integer-result ABI. High Five,
-- Classic 3.0, modified 287 and Ertheia all keep the same relational body.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadpledgeranking"()
RETURNS TABLE (
    pledge_id integer,
    root_name_value integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        pledge."pledge_id",
        pledge."root_name_value"
    FROM "world"."pledge" AS pledge
    WHERE pledge."skill_level" >= 5
    ORDER BY pledge."root_name_value" DESC;
$function$;

COMMIT;
