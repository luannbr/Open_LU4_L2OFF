-- Current Cached.exe primary opcode 70
-- (RequestSetUserPledgeInfo) persistence.
--
-- CPledgeDb::SetUserPledgeInfo 0x00514364 fixes the ordered ABI as
-- (UTF-16 field name, unsigned-compatible value, character ID). Compatible
-- substantive donors unanimously implement these same four field branches.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_updatepledgeinfo"(
    p_field_name text,
    p_field_value integer,
    p_char_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    CASE p_field_name
        WHEN 'pledge_dismiss_time' THEN
            UPDATE "world"."user_data"
            SET "pledge_dismiss_time" = p_field_value
            WHERE "char_id" = p_char_id;
        WHEN 'pledge_ousted_time' THEN
            UPDATE "world"."user_data"
            SET "pledge_ousted_time" = p_field_value
            WHERE "char_id" = p_char_id;
        WHEN 'pledge_withdraw_time' THEN
            UPDATE "world"."user_data"
            SET "pledge_withdraw_time" = p_field_value
            WHERE "char_id" = p_char_id;
        WHEN 'surrender_war_id' THEN
            UPDATE "world"."user_data"
            SET "surrender_war_id" = p_field_value
            WHERE "char_id" = p_char_id;
        ELSE
            RAISE EXCEPTION 'lin_UpdatePledgeInfo: invalid field [%]',
                p_field_name;
    END CASE;
END;
$function$;

COMMIT;
