-- Current Cached.exe primary opcodes 215 and 216.
--
-- The current machine image fixes both procedure ABIs. Classic 3.0,
-- modified 287 and Ertheia keep the same compatible table
-- effects: aggregate all rows for a group and append one new point row.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_getgrouppoint"(
    p_group_id integer
) RETURNS TABLE (
    group_point double precision
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        sum(point."group_point")::double precision AS "group_point"
    FROM "world"."event_point" AS point
    WHERE point."group_id" = p_group_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_creategrouppoint"(
    p_group_id integer,
    p_group_point double precision
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."event_point" (
        "group_id",
        "group_point"
    ) VALUES (
        p_group_id,
        p_group_point
    );
$function$;

COMMIT;
