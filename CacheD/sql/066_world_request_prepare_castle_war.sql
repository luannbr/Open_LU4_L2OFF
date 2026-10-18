-- Current Cached.exe primary opcode 76 (RequestPrepareCastleWar)
-- persistence.
--
-- The current handler and unnamed helper at 0x004844b0 fix the five- and
-- three-input ABIs. Modified-287, Classic 3.0 and Ertheia provide unanimous
-- compatible substantive bodies, including lin_DeleteCastleWar's affected
-- row-count result.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_setcastlewar"(
    p_world_id integer,
    p_pledge_id integer,
    p_castle_id integer,
    p_type integer,
    p_propose_time integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."castle_war" (
        "world_id",
        "pledge_id",
        "castle_id",
        "type",
        "propose_time"
    ) VALUES (
        p_world_id,
        p_pledge_id,
        p_castle_id,
        p_type,
        p_propose_time
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletecastlewar"(
    p_world_id integer,
    p_pledge_id integer,
    p_castle_id integer
) RETURNS TABLE (return_value integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    affected_rows bigint;
BEGIN
    DELETE FROM "world"."castle_war" AS castle_war
    WHERE castle_war."world_id" = p_world_id
      AND castle_war."pledge_id" = p_pledge_id
      AND castle_war."castle_id" = p_castle_id;

    GET DIAGNOSTICS affected_rows = ROW_COUNT;
    RETURN QUERY SELECT affected_rows::integer;
END;
$function$;

COMMIT;
