-- Current Cached.exe primary opcode 79 (RequestConfirmCastleDefence)
-- persistence.
--
-- The current handler and unnamed helper at 0x00484540 fix the four-input
-- ABI and affected-row result. Modified-287, Classic 3.0 and Ertheia provide
-- identical substantive bodies, including the world_id condition.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_updatecastlewar"(
    p_castle_id integer,
    p_pledge_id integer,
    p_status integer,
    p_world_id integer
) RETURNS TABLE (return_value integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    affected_rows bigint;
BEGIN
    UPDATE "world"."castle_war" AS castle_war
    SET "type" = p_status
    WHERE castle_war."castle_id" = p_castle_id
      AND castle_war."pledge_id" = p_pledge_id
      AND castle_war."world_id" = p_world_id;

    GET DIAGNOSTICS affected_rows = ROW_COUNT;
    RETURN QUERY SELECT affected_rows::integer;
END;
$function$;

COMMIT;
