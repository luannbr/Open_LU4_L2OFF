-- Current Cached.exe primary opcode 22 (RequestCharacterLogout) play-time
-- persistence.
--
-- CAccount::SaveWeekPlayTime 0x0040aee4 binds account ID, used seconds and
-- the unpadded UTF-16 `year-month-day hour:minute:second` value to the exact
-- call lin_SaveTimeData(?,?,?). Eight substantive full-arity-compatible SQL
-- donors agree on the update-or-insert effect and negative-time rejection.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_savetimedata"(
    p_account_id integer,
    p_used_seconds integer,
    p_last_save_time text
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF p_used_seconds < 0 THEN
        RAISE EXCEPTION
            'Not valid parameter: account id[%] sec[%], dt[%]',
            p_account_id,
            p_used_seconds,
            p_last_save_time;
    END IF;

    INSERT INTO "world"."time_data" (
        "account_id",
        "last_logout",
        "used_sec"
    ) VALUES (
        p_account_id,
        p_last_save_time::timestamp without time zone,
        p_used_seconds
    )
    ON CONFLICT ("account_id") DO UPDATE
    SET "last_logout" = EXCLUDED."last_logout",
        "used_sec" = EXCLUDED."used_sec";
END;
$function$;

COMMIT;
