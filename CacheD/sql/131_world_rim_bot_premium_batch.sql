-- Extended requests 151-160. Current Cached.exe fixes every callable,
-- parameter/result and packet ABI. Classic 3.0, modified 287 and Ertheia
-- keep the RIM, bot-report, color and premium-batch relational bodies.
-- The three captcha columns and lin_SaveBotReportConfirm effect are a
-- portable schema extension: the current four-result LoadBotReport ABI and
-- paired current L2Server methods prove their meanings, but no supplied SQL
-- archive keeps the added physical identifiers or procedure body.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

ALTER TABLE "world"."bot_report"
    ADD COLUMN IF NOT EXISTS "captcha_confirm_times" integer DEFAULT 0;
ALTER TABLE "world"."bot_report"
    ADD COLUMN IF NOT EXISTS "captcha_confirm_sec" integer DEFAULT 0;
ALTER TABLE "world"."bot_report"
    ADD COLUMN IF NOT EXISTS "captcha_delay_sec" integer DEFAULT 0;

UPDATE "world"."bot_report"
SET "captcha_confirm_times" = COALESCE("captcha_confirm_times", 0),
    "captcha_confirm_sec" = COALESCE("captcha_confirm_sec", 0),
    "captcha_delay_sec" = COALESCE("captcha_delay_sec", 0)
WHERE "captcha_confirm_times" IS NULL
   OR "captcha_confirm_sec" IS NULL
   OR "captcha_delay_sec" IS NULL;

ALTER TABLE "world"."bot_report"
    ALTER COLUMN "captcha_confirm_times" SET DEFAULT 0,
    ALTER COLUMN "captcha_confirm_times" SET NOT NULL,
    ALTER COLUMN "captcha_confirm_sec" SET DEFAULT 0,
    ALTER COLUMN "captcha_confirm_sec" SET NOT NULL,
    ALTER COLUMN "captcha_delay_sec" SET DEFAULT 0,
    ALTER COLUMN "captcha_delay_sec" SET NOT NULL;

CREATE OR REPLACE FUNCTION "world"."lin_getrimpointrank"(
    p_binding_time integer
) RETURNS TABLE (
    char_id integer,
    char_name text,
    class_id integer,
    level integer,
    maximum_point integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        rim."char_id",
        user_data."char_name"::text,
        user_data."class"::integer,
        user_data."lev"::integer,
        MAX(rim."point")::integer
    FROM "world"."user_rim_point" AS rim
    LEFT JOIN "world"."user_data" AS user_data
      ON user_data."char_id" = rim."char_id"
    WHERE rim."log_time" >= p_binding_time
    GROUP BY
        rim."char_id",
        user_data."char_name",
        user_data."class",
        user_data."lev"
    ORDER BY MAX(rim."point") DESC, user_data."lev"
    LIMIT 25;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadbotreport"(
    p_char_id integer
) RETURNS TABLE (
    reported integer,
    captcha_confirm_times integer,
    captcha_confirm_sec integer,
    captcha_delay_sec integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        report."reported",
        report."captcha_confirm_times",
        report."captcha_confirm_sec",
        report."captcha_delay_sec"
    FROM "world"."bot_report" AS report
    WHERE report."char_id" = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_resetbotreport"(
    p_char_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."bot_report" AS report (
        "char_id",
        "reported",
        "reported_date",
        "bot_admin"
    ) VALUES (
        p_char_id,
        0,
        0,
        0
    ) ON CONFLICT ("char_id") DO UPDATE
    SET "reported" = 0;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setbotreport"(
    p_char_id integer,
    p_reported integer,
    p_reported_date double precision
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."bot_report" (
        "char_id",
        "reported",
        "reported_date",
        "bot_admin"
    ) VALUES (
        p_char_id,
        p_reported,
        p_reported_date::integer,
        0
    ) ON CONFLICT ("char_id") DO UPDATE
    SET "reported" = EXCLUDED."reported",
        "reported_date" = EXCLUDED."reported_date";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savebotreportconfirm"(
    p_char_id integer,
    p_confirm_times integer,
    p_confirm_sec integer,
    p_delay_sec integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    -- The current executable proves the four-value effect but not whether
    -- the unavailable MSSQL body used UPDATE+INSERT or MERGE. This atomic
    -- upsert keeps the observable CacheD state for either row case.
    INSERT INTO "world"."bot_report" (
        "char_id",
        "reported",
        "reported_date",
        "bot_admin",
        "captcha_confirm_times",
        "captcha_confirm_sec",
        "captcha_delay_sec"
    ) VALUES (
        p_char_id,
        0,
        0,
        0,
        p_confirm_times,
        p_confirm_sec,
        p_delay_sec
    ) ON CONFLICT ("char_id") DO UPDATE
    SET "captcha_confirm_times" = EXCLUDED."captcha_confirm_times",
        "captcha_confirm_sec" = EXCLUDED."captcha_confirm_sec",
        "captcha_delay_sec" = EXCLUDED."captcha_delay_sec";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savebotreport"(
    p_char_id integer,
    p_reported_date double precision
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."bot_report" AS report (
        "char_id",
        "reported",
        "reported_date",
        "bot_admin"
    ) VALUES (
        p_char_id,
        1,
        p_reported_date::integer,
        0
    ) ON CONFLICT ("char_id") DO UPDATE
    SET "reported" = report."reported" + 1,
        "reported_date" = EXCLUDED."reported_date";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadbotreporttime"(
    p_char_id integer
) RETURNS TABLE (reported_date integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT report."reported_date"
    FROM "world"."bot_report" AS report
    WHERE report."char_id" = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_decaybotreport"(
    p_char_id integer,
    p_minus integer,
    p_reported_date double precision
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."bot_report" AS report
    SET "reported" = report."reported" - p_minus,
        "reported_date" = p_reported_date::integer
    WHERE report."char_id" = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadbotreporttopten"()
RETURNS TABLE (
    char_id integer,
    reported integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT report."char_id", report."reported"
    FROM "world"."bot_report" AS report
    JOIN "world"."user_data" AS user_data
      ON user_data."char_id" = report."char_id"
     AND user_data."account_id" > 0
    WHERE report."reported" > 0
    ORDER BY report."reported" DESC
    LIMIT 40;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setbotadmin"(
    p_char_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."bot_report" (
        "char_id",
        "reported",
        "reported_date",
        "bot_admin"
    ) VALUES (
        p_char_id,
        0,
        0,
        1
    ) ON CONFLICT ("char_id") DO UPDATE
    SET "bot_admin" = 1;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setcolor"(
    p_char_id integer,
    p_color integer,
    p_type integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF p_type = 0 THEN
        INSERT INTO "world"."user_name_color" (
            "char_id",
            "color_rgb"
        ) VALUES (
            p_char_id,
            p_color
        ) ON CONFLICT ("char_id") DO UPDATE
        SET "color_rgb" = EXCLUDED."color_rgb";
    ELSIF p_type = 1 THEN
        INSERT INTO "world"."user_name_color" (
            "char_id",
            "nickname_color_rgb"
        ) VALUES (
            p_char_id,
            p_color
        ) ON CONFLICT ("char_id") DO UPDATE
        SET "nickname_color_rgb" = EXCLUDED."nickname_color_rgb";
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadpremiumitemsforbatch"()
RETURNS TABLE (warehouse_no bigint)
LANGUAGE sql
STABLE
AS $function$
    SELECT premium."warehouse_no"
    FROM "world"."user_premium_item" AS premium
    WHERE premium."ibserver_delete_date" IS NULL
      AND premium."warehouse_no" > 0;
$function$;

COMMIT;
