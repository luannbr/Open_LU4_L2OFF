-- Current Cached.exe primary opcodes 84 and 85 persistence.
--
-- CPledge::DeleteCrest 0x00505f88 and CPledgeDb::ModifyPledgeName
-- 0x005193cc fix both callable ABIs. Modified-287, Classic 3.0 and Ertheia
-- provide identical substantive procedure bodies.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_initpledgecrest"(
    p_pledge_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."pledge" AS pledge
    SET "crest_id" = 0
    WHERE pledge."pledge_id" = p_pledge_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_modifypledgename"(
    p_pledge_name text,
    p_pledge_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."pledge" AS pledge
    SET "name" = p_pledge_name
    WHERE pledge."pledge_id" = p_pledge_id;

    UPDATE "world"."pledge_recruit_board" AS board
    SET "pledge_name" = p_pledge_name
    WHERE board."pledge_id" = p_pledge_id;
END;
$function$;

COMMIT;
