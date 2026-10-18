-- Current Cached.exe account-event, PCCafe, stamp, SSQ and alliance runtime
-- persistence.
--
-- Current machine paths fix all five callable names and ordered bind/result
-- types.  Classic 3.0, modified 287 and Ertheia keep identical relational
-- effects. High Five agrees where that procedure survives.
-- SaveAccountUserStampLog receives target Win64 long through the shipping
-- SQL_DOUBLE binding, while the admitted column and donor procedure are
-- integer, so the portable signature keeps double precision
-- and converts the integral epoch value at the database boundary.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadaccountpccafepoint"(
    p_account_id integer
) RETURNS TABLE (point integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT pccafe."point"
      FROM "world"."account_user_pccafe_point" AS pccafe
     WHERE pccafe."account_id" = p_account_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_saveaccountuserstamplog"(
    p_account_id integer,
    p_char_id integer,
    p_type integer,
    p_param1 integer,
    p_message text,
    p_time double precision
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."account_user_stamp_log" (
        "account_id",
        "char_id",
        "type",
        "param1",
        "msg1",
        "log_date"
    ) VALUES (
        p_account_id,
        p_char_id,
        p_type,
        p_param1,
        p_message,
        trunc(p_time)::integer
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_saveeventtime"(
    p_account_id integer,
    p_event_time integer,
    p_event_type integer,
    p_event_variable integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."user_event_time" AS event_time
       SET "event_time" = p_event_time
     WHERE event_time."account_id" = p_account_id
       AND event_time."event_type" = p_event_type
       AND event_time."event_variable" = p_event_variable;

    IF NOT FOUND THEN
        INSERT INTO "world"."user_event_time" (
            "account_id",
            "event_time",
            "event_type",
            "event_variable"
        ) VALUES (
            p_account_id,
            p_event_time,
            p_event_type,
            p_event_variable
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setallianceinfo"(
    p_field_name text,
    p_field_data integer,
    p_alliance_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF "world"."mssql_text_eq"(p_field_name, 'oust_time') THEN
        UPDATE "world"."alliance" AS alliance
           SET "oust_time" = p_field_data
         WHERE alliance."id" = p_alliance_id;
    ELSIF "world"."mssql_text_eq"(p_field_name, 'crest_id') THEN
        UPDATE "world"."alliance" AS alliance
           SET "crest_id" = p_field_data
         WHERE alliance."id" = p_alliance_id;
    ELSE
        RAISE EXCEPTION
            'lin_SetAllianceInfo: invalid field [%]', p_field_name;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setuserssqdawnround"(
    p_char_id integer,
    p_ssq_dawn_round integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_data" AS character
       SET "ssq_dawn_round" = p_ssq_dawn_round
     WHERE character."char_id" = p_char_id;
$function$;

COMMIT;
