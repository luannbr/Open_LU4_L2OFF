-- Current Cached.exe extended requests 321 through 330.
--
-- The executable fixes every reached call ABI. Classic 3.0, modified 287
-- and Ertheia keep identical relational bodies for the seven functions
-- added here. lin_UpdateUserBeauty and the NPC quest-log functions already
-- belong to migrations 020 and 136 respectively.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_resetexdropcount"(
    p_char_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."ex_drop_info" AS info
    WHERE info."char_id" = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savepledgewarpointprev"(
    p_challenger integer,
    p_challengee integer,
    p_point_prev integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."war_declare" AS war
    SET "war_point_prev" = p_point_prev
    WHERE war."challenger" = p_challenger
      AND war."challengee" = p_challengee;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatepledgewarkillcount"(
    p_challenger integer,
    p_challengee integer,
    p_death_count integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."war_declare" AS war
    SET "death_count" = p_death_count
    WHERE war."challenger" = p_challenger
      AND war."challengee" = p_challengee;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setpreviousagitownerid"(
    p_agit_id integer,
    p_pledge_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."agit" AS agit
    SET "previous_owner_pledge" = p_pledge_id
    WHERE agit."id" = p_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadbeautydatalist"(
    p_beauty_type integer,
    p_beauty_upper_id integer,
    p_beauty_id integer
) RETURNS TABLE (
    buy_count integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."beauty_data_list" AS beauty
        WHERE beauty."beauty_type" = p_beauty_type
          AND beauty."beauty_id" = p_beauty_id
          AND beauty."beauty_upper_id" = p_beauty_upper_id
    ) THEN
        RETURN QUERY
        SELECT beauty."buy_count"
        FROM "world"."beauty_data_list" AS beauty
        WHERE beauty."beauty_type" = p_beauty_type
          AND beauty."beauty_id" = p_beauty_id
          AND beauty."beauty_upper_id" = p_beauty_upper_id;
    ELSE
        INSERT INTO "world"."beauty_data_list" (
            "beauty_type",
            "beauty_upper_id",
            "beauty_id",
            "buy_count"
        ) VALUES (
            p_beauty_type,
            p_beauty_upper_id,
            p_beauty_id,
            0
        );
        RETURN QUERY SELECT 0;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_incrementbeautydata"(
    p_beauty_type integer,
    p_beauty_id integer,
    p_beauty_upper_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."beauty_data_list" AS beauty
    SET "buy_count" = beauty."buy_count" + 1
    WHERE beauty."beauty_type" = p_beauty_type
      AND beauty."beauty_id" = p_beauty_id
      AND beauty."beauty_upper_id" = p_beauty_upper_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savebeautypurchaselist"(
    p_char_id integer,
    p_beauty_type integer,
    p_beauty_upper_id integer,
    p_beauty_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF NOT EXISTS (
        SELECT 1
        FROM "world"."user_beauty_purchase" AS purchase
        WHERE purchase."char_id" = p_char_id
          AND purchase."beauty_type" = p_beauty_type
          AND purchase."beauty_upper_id" = p_beauty_upper_id
          AND purchase."beauty_id" = p_beauty_id
    ) THEN
        INSERT INTO "world"."user_beauty_purchase" (
            "char_id",
            "beauty_type",
            "beauty_upper_id",
            "beauty_id"
        ) VALUES (
            p_char_id,
            p_beauty_type,
            p_beauty_upper_id,
            p_beauty_id
        );
    END IF;
END;
$function$;

COMMIT;
