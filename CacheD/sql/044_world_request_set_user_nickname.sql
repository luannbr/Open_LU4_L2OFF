-- Current Cached.exe primary opcode 37 (RequestSetUserNickname) database
-- contract. Current machine code fixes the two input binds and one fetched
-- integer result. Compatible SQL donors fix the update/return semantics.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_setusernickname"(
    p_user_id integer,
    p_nickname text
) RETURNS TABLE (return_code integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    updated_rows integer;
BEGIN
    UPDATE "world"."user_data" AS target
    SET "nickname" = p_nickname
    WHERE target."char_id" = p_user_id;

    GET DIAGNOSTICS updated_rows = ROW_COUNT;
    RETURN QUERY
    SELECT CASE
        WHEN updated_rows = 1 THEN p_user_id
        ELSE 0
    END;
END;
$function$;

COMMIT;
