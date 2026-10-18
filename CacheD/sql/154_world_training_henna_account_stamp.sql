-- Current Cached.exe extended slots 382-390: Training Zone, periodic premium
-- Henna, limited-shop reset and account-stamp persistence.
--
-- The current image fixes every callable signature, bind type and result
-- column below. Classic 3.0, modified 287 and Ertheia keep identical
-- substantive bodies for every procedure except lin_TestTrainingZone and
-- lin_DeleteAccountUserStampLog. The latter is the direct inverse of the
-- exact load relation. The former is reached only by the paired current
-- builder `training settime` path: active training returns 100. Otherwise it
-- changes the account's remaining minutes and returns 200.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_brresetlimitedshopiteminfo"(
    p_account_id integer,
    p_char_id integer,
    p_product_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."br_user_shopitem_limit" AS shop
    WHERE shop."account_id" = p_account_id
      AND shop."product_id" = p_product_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_checktrainingzone"(
    p_account_id integer
) RETURNS TABLE(
    "start_time" integer,
    "remain_time" integer
)
LANGUAGE plpgsql
STABLE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."trainingzone_time" AS zone_time
        WHERE zone_time."account_id" = p_account_id
    ) THEN
        IF EXISTS (
            SELECT 1
            FROM "world"."trainingzone_state" AS zone_state
            JOIN "world"."user_data" AS character
              ON character."char_id" = zone_state."char_id"
            WHERE character."account_id" = p_account_id
              AND zone_state."states" = 1
        ) THEN
            RETURN QUERY SELECT -1, -1;
        ELSE
            RETURN QUERY
                SELECT zone_time."start_time", zone_time."remain_time"
                FROM "world"."trainingzone_time" AS zone_time
                WHERE zone_time."account_id" = p_account_id;
        END IF;
    ELSE
        RETURN QUERY SELECT 0, 0;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_starttrainingzone"(
    p_account_id integer,
    p_start_time integer,
    p_remain_time integer,
    p_char_id integer,
    p_subjob_id integer,
    p_start_level integer,
    p_x integer,
    p_y integer,
    p_z integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."trainingzone_time" AS zone_time
    SET
        "start_time" = p_start_time,
        "remain_time" = p_remain_time
    WHERE zone_time."account_id" = p_account_id;

    IF NOT FOUND THEN
        INSERT INTO "world"."trainingzone_time" (
            "account_id",
            "start_time",
            "remain_time"
        ) VALUES (
            p_account_id,
            p_start_time,
            p_remain_time
        );
    END IF;

    UPDATE "world"."trainingzone_state" AS zone_state
    SET
        "subjob_id" = p_subjob_id::smallint,
        "states" = 1,
        "training_time" = 0,
        "start_level" = p_start_level::smallint,
        "xloc" = p_x,
        "yloc" = p_y,
        "zloc" = p_z,
        "max_end_time" = p_start_time + p_remain_time * 60
    WHERE zone_state."char_id" = p_char_id;

    IF NOT FOUND THEN
        INSERT INTO "world"."trainingzone_state" (
            "char_id",
            "subjob_id",
            "states",
            "training_time",
            "start_level",
            "xloc",
            "yloc",
            "zloc",
            "max_end_time"
        ) VALUES (
            p_char_id,
            p_subjob_id::smallint,
            1,
            0,
            p_start_level::smallint,
            p_x,
            p_y,
            p_z,
            p_start_time + p_remain_time * 60
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_endtrainingzone"(
    p_account_id integer,
    p_end_time double precision,
    p_char_id integer
) RETURNS TABLE(
    "xloc" integer,
    "yloc" integer,
    "zloc" integer,
    "training_time" integer,
    "remain_time" integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_start_time integer;
    v_remain_time integer;
    v_training_time integer;
BEGIN
    SELECT zone_time."start_time", zone_time."remain_time"
    INTO v_start_time, v_remain_time
    FROM "world"."trainingzone_time" AS zone_time
    WHERE zone_time."account_id" = p_account_id;

    v_training_time =
        (trunc(p_end_time)::integer - v_start_time) / 60;
    IF v_training_time < 1 THEN
        UPDATE "world"."trainingzone_state" AS zone_state
        SET "states" = 0, "max_end_time" = 0
        WHERE zone_state."char_id" = p_char_id;
    ELSE
        IF v_training_time >= v_remain_time THEN
            v_training_time = v_remain_time;
        END IF;
        v_remain_time = v_remain_time - v_training_time;
        UPDATE "world"."trainingzone_time" AS zone_time
        SET "remain_time" = v_remain_time
        WHERE zone_time."account_id" = p_account_id;
        UPDATE "world"."trainingzone_state" AS zone_state
        SET
            "states" = 2,
            "training_time" = v_training_time,
            "max_end_time" = 0
        WHERE zone_state."char_id" = p_char_id;
    END IF;

    RETURN QUERY
        SELECT
            zone_state."xloc",
            zone_state."yloc",
            zone_state."zloc",
            v_training_time,
            v_remain_time
        FROM "world"."trainingzone_state" AS zone_state
        WHERE zone_state."char_id" = p_char_id;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_endtrainingzoneforlogin"(
    p_account_id integer,
    p_char_id integer,
    p_training_time integer
) RETURNS TABLE(
    "xloc" integer,
    "yloc" integer,
    "zloc" integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."trainingzone_time" AS zone_time
    SET "remain_time" = 0
    WHERE zone_time."account_id" = p_account_id;
    UPDATE "world"."trainingzone_state" AS zone_state
    SET
        "states" = 2,
        "training_time" = p_training_time,
        "max_end_time" = 0
    WHERE zone_state."char_id" = p_char_id;

    RETURN QUERY
        SELECT
            zone_state."xloc",
            zone_state."yloc",
            zone_state."zloc"
        FROM "world"."trainingzone_state" AS zone_state
        WHERE zone_state."char_id" = p_char_id;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadtrainingzonestate"(
    p_char_id integer
) RETURNS TABLE(
    "states" integer,
    "training_time" integer,
    "start_level" integer,
    "xloc" integer,
    "yloc" integer,
    "zloc" integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        zone_state."states"::integer,
        zone_state."training_time",
        zone_state."start_level"::integer,
        zone_state."xloc",
        zone_state."yloc",
        zone_state."zloc"
    FROM "world"."trainingzone_state" AS zone_state
    WHERE zone_state."char_id" = p_char_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadtrainingzonetime"(
    p_account_id integer
) RETURNS TABLE(
    "start_time" integer,
    "remain_time" integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT zone_time."start_time", zone_time."remain_time"
    FROM "world"."trainingzone_time" AS zone_time
    WHERE zone_time."account_id" = p_account_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_rewardtrainingzone"(
    p_char_id integer,
    p_subjob_id integer
) RETURNS TABLE("result" integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_result integer;
BEGIN
    UPDATE "world"."trainingzone_state" AS zone_state
    SET "states" = 0, "training_time" = 0
    WHERE zone_state."char_id" = p_char_id
      AND zone_state."subjob_id" = p_subjob_id;
    GET DIAGNOSTICS v_result = ROW_COUNT;
    RETURN QUERY SELECT v_result;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_testtrainingzone"(
    p_account_id integer,
    p_remain_time integer
) RETURNS TABLE("result" integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."trainingzone_state" AS zone_state
        JOIN "world"."user_data" AS character
          ON character."char_id" = zone_state."char_id"
        WHERE character."account_id" = p_account_id
          AND zone_state."states" = 1
    ) THEN
        RETURN QUERY SELECT 100;
        RETURN;
    END IF;

    UPDATE "world"."trainingzone_time" AS zone_time
    SET "remain_time" = p_remain_time
    WHERE zone_time."account_id" = p_account_id;
    RETURN QUERY SELECT 200;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadhennapremium"(
    p_char_id integer,
    p_subjob_id integer
) RETURNS TABLE(
    "henna_id" integer,
    "start_time" integer,
    "period" integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT premium."henna_id", premium."start_time", premium."period"
    FROM "world"."user_henna_premium" AS premium
    WHERE premium."char_id" = p_char_id
      AND premium."subjob_id" = p_subjob_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadaccountuserstamp"(
    p_account_id integer,
    p_type integer,
    p_stamp integer,
    p_use_stamp integer
) RETURNS TABLE(
    "stamp" integer,
    "stamp_time" integer
)
LANGUAGE plpgsql
STABLE
AS $function$
BEGIN
    IF p_use_stamp = 1 THEN
        RETURN QUERY
            SELECT account_stamp."stamp", account_stamp."stamp_time"
            FROM "world"."account_user_stamp" AS account_stamp
            WHERE account_stamp."account_id" = p_account_id
              AND account_stamp."type" = p_type
              AND account_stamp."stamp" = p_stamp;
    ELSE
        RETURN QUERY
            SELECT account_stamp."stamp", account_stamp."stamp_time"
            FROM "world"."account_user_stamp" AS account_stamp
            WHERE account_stamp."account_id" = p_account_id
              AND account_stamp."type" = p_type;
    END IF;

    IF NOT FOUND THEN
        RETURN QUERY SELECT 0, 0;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadaccountuserstamplog"(
    p_account_id integer,
    p_type integer
) RETURNS TABLE(
    "param1" integer,
    "msg1" varchar(256),
    "log_date" integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT stamp_log."param1", stamp_log."msg1", stamp_log."log_date"
    FROM "world"."account_user_stamp_log" AS stamp_log
    WHERE stamp_log."account_id" = p_account_id
      AND stamp_log."type" = p_type
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updateaccountuserstamp"(
    p_account_id integer,
    p_type integer,
    p_stamp integer,
    p_time integer,
    p_stamp_condition integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF p_stamp_condition = 1 THEN
        UPDATE "world"."account_user_stamp" AS account_stamp
        SET "stamp" = p_stamp, "stamp_time" = p_time
        WHERE account_stamp."account_id" = p_account_id
          AND account_stamp."type" = p_type
          AND account_stamp."stamp" = p_stamp;
    ELSE
        UPDATE "world"."account_user_stamp" AS account_stamp
        SET "stamp" = p_stamp, "stamp_time" = p_time
        WHERE account_stamp."account_id" = p_account_id
          AND account_stamp."type" = p_type;
    END IF;

    IF NOT FOUND THEN
        INSERT INTO "world"."account_user_stamp" (
            "account_id",
            "type",
            "stamp",
            "stamp_time"
        ) VALUES (
            p_account_id,
            p_type,
            p_stamp,
            p_time
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deleteaccountuserstamplog"(
    p_account_id integer,
    p_type integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."account_user_stamp_log" AS stamp_log
    WHERE stamp_log."account_id" = p_account_id
      AND stamp_log."type" = p_type
$function$;

COMMIT;
