-- Current Cached.exe primary opcodes 77/78
-- (RequestViewSiegeList/RequestApproveBattle) query surfaces.
--
-- Current CCastleDb fixes both two-input ABIs and their two-/three-column
-- results. C1, modified-287, Classic 3.0 and Ertheia provide compatible
-- substantive bodies with the same joins, filters and projections.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_viewsiegelist"(
    p_castle_id integer,
    p_type integer
) RETURNS TABLE (
    pledge_id integer,
    pledge_name text
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        pledge_row."pledge_id"::integer,
        pledge_row."name"::text
    FROM "world"."pledge" AS pledge_row
    JOIN "world"."castle_war" AS castle_war
      ON pledge_row."pledge_id" = castle_war."pledge_id"
    WHERE castle_war."castle_id" = p_castle_id
      AND castle_war."type" = p_type;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_approvebattle"(
    p_castle_id integer,
    p_type integer
) RETURNS TABLE (
    pledge_id integer,
    pledge_name text,
    registration_type integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        pledge_row."pledge_id"::integer,
        pledge_row."name"::text,
        castle_war."type"::integer
    FROM "world"."pledge" AS pledge_row
    JOIN "world"."castle_war" AS castle_war
      ON pledge_row."pledge_id" = castle_war."pledge_id"
    WHERE castle_war."castle_id" = p_castle_id
      AND castle_war."type" <> p_type;
$function$;

COMMIT;
