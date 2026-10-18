-- Current Cached.exe AQ opcode 64 (AllEventPointPacket).
--
-- The current machine image fixes the zero-input/two-result ABI and orders
-- results by descending aggregate point. Classic Interlude, Classic 3.0,
-- modified 287 and Ertheia keep the same relational body.

BEGIN;

CREATE OR REPLACE FUNCTION "world"."lin_getallgrouppoint"()
RETURNS TABLE (
    group_id integer,
    group_point double precision
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        point."group_id",
        sum(point."group_point")::double precision AS "group_point"
    FROM "world"."event_point" AS point
    GROUP BY point."group_id"
    ORDER BY sum(point."group_point") DESC;
$function$;

COMMIT;
