-- Current Cached.exe primary opcodes 80 through 83 and
-- CCastleDb::GetRelatedCastleSiege 0x0049fa98.
--
-- Every current production caller passes castle_id = 0, selecting this exact
-- two-input path. Modified-287, Classic 3.0 and Ertheia have identical
-- substantive bodies and the current decoder proves all three result columns.
-- The alternate current-image literal names lin_GetRelatedCastleSiege with
-- three placeholders but binds only two parameters. It is not
-- repaired or exposed as a fabricated three-input PostgreSQL function here.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_getallcastlesiege"(
    p_world_id integer,
    p_pledge_id integer
) RETURNS TABLE (
    castle_id integer,
    next_war_time integer,
    registration_type integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        castle_row."id"::integer,
        castle_row."next_war_time"::integer,
        castle_war."type"::integer
    FROM "world"."castle" AS castle_row
    JOIN "world"."castle_war" AS castle_war
      ON castle_row."id" = castle_war."castle_id"
    WHERE castle_row."next_war_time" <> 0
      AND castle_war."pledge_id" = p_pledge_id
      AND castle_war."world_id" = p_world_id;
$function$;

COMMIT;
