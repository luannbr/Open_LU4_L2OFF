-- Current Cached.exe extended requests 271 through 280.
--
-- Cached.exe fixes every callable ABI and result order. Classic 3.0,
-- modified 287 and Ertheia keep identical substantive bodies for the
-- Agathion, name, job-group, PCCafe, daily-reset, event-time, karma-time and
-- character-extra procedures. The premium-goods procedure text is absent
-- from all supplied dumps. Its current five-input ABI, paired L2Server field
-- roles and the identical six-column table in all three compatible schemas
-- nevertheless fix the single-row append below without adding a guessed
-- field, key or branch.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE TABLE IF NOT EXISTS "world"."user_premium_goods_receive" (
    "goods_uid" bigint NOT NULL,
    "char_id" integer NOT NULL,
    "goods_id" integer NOT NULL,
    "item_type" integer NOT NULL,
    "item_amount" integer NOT NULL,
    "receive_date" timestamp without time zone NOT NULL
);

CREATE OR REPLACE FUNCTION "world"."lin_initagathionelapsedtime"(
    p_char_id integer,
    p_agathion_id integer,
    p_init_time integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."agathion" AS agathion
    SET "elapsed_time" = 0,
        "last_update_time" = p_init_time
    WHERE agathion."char_id" = p_char_id
      AND agathion."agathion_id" = p_agathion_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_checkcharname"(
    p_char_name text,
    p_account_id integer
) RETURNS TABLE (result integer)
LANGUAGE plpgsql
STABLE
AS $function$
DECLARE
    v_char_name text := rtrim(p_char_name);
BEGIN
    -- The source uses Unicode LIKE N' ' after RTRIM. Its pattern space is
    -- significant and is not SQL Server's ordinary padded equality rule.
    IF v_char_name LIKE ' ' THEN
        RETURN QUERY SELECT -1;
        RETURN;
    END IF;

    IF EXISTS (
        SELECT 1
        FROM "world"."user_prohibit" AS prohibited
        WHERE "world"."mssql_text_eq"(prohibited."char_name", v_char_name)
    ) THEN
        RETURN QUERY SELECT -2;
        RETURN;
    END IF;

    IF EXISTS (
        SELECT 1
        FROM "world"."user_prohibit_word" AS prohibited
        WHERE lower(v_char_name) LIKE
            '%' || lower(prohibited."words") || '%'
    ) THEN
        RETURN QUERY SELECT -3;
        RETURN;
    END IF;

    IF EXISTS (
        SELECT 1
        FROM "world"."user_name_reserved" AS reserved_name
        WHERE reserved_name."used" = 0
          AND "world"."mssql_text_eq"(
              reserved_name."char_name",
              v_char_name
          )
          AND reserved_name."account_id" <> p_account_id
    ) THEN
        RETURN QUERY SELECT -4;
        RETURN;
    END IF;

    IF EXISTS (
        SELECT 1
        FROM "world"."user_data" AS user_data
        WHERE "world"."mssql_text_eq"(user_data."char_name", v_char_name)
    ) THEN
        RETURN QUERY SELECT -5;
        RETURN;
    END IF;

    RETURN QUERY SELECT 1;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_manageusernamereserved"(
    p_option integer,
    p_char_name text,
    p_account_id integer,
    p_used integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF p_option = 0 THEN
        IF EXISTS (
            SELECT 1
            FROM "world"."user_name_reserved" AS reserved_name
            WHERE "world"."mssql_text_eq"(
                reserved_name."char_name",
                p_char_name
            )
        ) THEN
            RETURN;
        END IF;

        INSERT INTO "world"."user_name_reserved" (
            "char_name",
            "account_id",
            "used"
        ) VALUES (
            p_char_name,
            p_account_id,
            p_used
        );
    ELSIF p_option = 1 THEN
        DELETE FROM "world"."user_name_reserved"
        WHERE "world"."mssql_text_eq"("char_name", p_char_name);
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_createpremiumgoodsreceive"(
    p_goods_uid bigint,
    p_char_id integer,
    p_goods_id integer,
    p_item_type integer,
    p_item_amount integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."user_premium_goods_receive" (
        "goods_uid",
        "char_id",
        "goods_id",
        "item_type",
        "item_amount",
        "receive_date"
    ) VALUES (
        p_goods_uid,
        p_char_id,
        p_goods_id,
        p_item_type,
        p_item_amount,
        "world"."mssql_getdate"()
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_saveuserjobgroup"(
    p_char_id integer,
    p_job_group integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."user_job_group" AS job_group
    SET "job_group" = p_job_group
    WHERE job_group."char_id" = p_char_id;

    IF NOT FOUND THEN
        INSERT INTO "world"."user_job_group" (
            "char_id",
            "job_group"
        ) VALUES (
            p_char_id,
            p_job_group
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_saveaccountpccafedailypoint"(
    p_account_id integer,
    p_daily_point integer,
    p_check_level integer
) RETURNS TABLE (
    daily_point integer,
    check_level integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_base_time timestamp without time zone;
    v_reset_time text;
BEGIN
    IF p_daily_point < 0 THEN
        IF p_daily_point = -1 THEN
            DELETE FROM "world"."account_user_pccafe_daily_point"
            WHERE "account_id" = p_account_id;
            RETURN QUERY SELECT 0, 0;
            RETURN;
        END IF;
        RAISE EXCEPTION
            'Not valid parameter : account id[%] point[%]',
            p_account_id,
            p_daily_point;
    END IF;

    IF EXISTS (
        SELECT 1
        FROM "world"."account_user_pccafe_daily_point" AS pccafe
        WHERE pccafe."account_id" = p_account_id
    ) THEN
        SELECT reset_time."reset_time"
        INTO v_reset_time
        FROM "world"."daily_reset_time" AS reset_time
        WHERE "world"."mssql_text_eq"(
            reset_time."event_name",
            'daily_pccafe_point'
        );

        v_base_time := "world"."mssql_getdate"()::date +
            COALESCE(v_reset_time, '06:30')::time;
        IF "world"."mssql_getdate"() < v_base_time THEN
            v_base_time := v_base_time - INTERVAL '1 day';
        END IF;

        IF (
            SELECT pccafe."last_update_time"
            FROM "world"."account_user_pccafe_daily_point" AS pccafe
            WHERE pccafe."account_id" = p_account_id
        ) < v_base_time THEN
            UPDATE "world"."account_user_pccafe_daily_point" AS pccafe
            SET "daily_point" = 0,
                "check_level" = 0,
                "last_update_time" = "world"."mssql_getdate"()
            WHERE pccafe."account_id" = p_account_id;
        ELSIF p_daily_point <> 0 THEN
            UPDATE "world"."account_user_pccafe_daily_point" AS pccafe
            SET "daily_point" = pccafe."daily_point" + p_daily_point,
                "check_level" = p_check_level,
                "last_update_time" = "world"."mssql_getdate"()
            WHERE pccafe."account_id" = p_account_id;
        END IF;
    ELSIF p_daily_point <> 0 THEN
        INSERT INTO "world"."account_user_pccafe_daily_point" (
            "account_id",
            "daily_point",
            "check_level",
            "last_update_time"
        ) VALUES (
            p_account_id,
            p_daily_point,
            p_check_level,
            "world"."mssql_getdate"()
        );
    END IF;

    RETURN QUERY
    SELECT
        pccafe."daily_point",
        pccafe."check_level"::integer
    FROM "world"."account_user_pccafe_daily_point" AS pccafe
    WHERE pccafe."account_id" = p_account_id;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setdailyresettime"(
    p_event_name text,
    p_reset_time text
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."daily_reset_time" AS reset_time
    SET "reset_time" = p_reset_time
    WHERE "world"."mssql_text_eq"(reset_time."event_name", p_event_name);

    IF NOT FOUND THEN
        INSERT INTO "world"."daily_reset_time" (
            "event_name",
            "reset_time"
        ) VALUES (
            p_event_name,
            p_reset_time
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_clearoneeventtime"(
    p_event_type integer,
    p_event_variable integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."user_event_time" AS event_time
    WHERE event_time."event_type" = p_event_type
      AND event_time."event_variable" = p_event_variable;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatetimetogivekarmapoint"(
    p_char_id integer,
    p_time_to_give_karma_point integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_data" AS user_data
    SET "time_to_give_karma_point" = p_time_to_give_karma_point
    WHERE user_data."char_id" = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savecharextrainfo"(
    p_char_id integer,
    p_pattack integer,
    p_pdefend integer,
    p_phit integer,
    p_pavoid integer,
    p_pcritical integer,
    p_pattackspeed integer,
    p_mattack integer,
    p_mdefend integer,
    p_mhit integer,
    p_mavoid integer,
    p_mcritical integer,
    p_mattackspeed integer,
    p_speed integer,
    p_stat_str integer,
    p_stat_dex integer,
    p_stat_con integer,
    p_stat_int integer,
    p_stat_wit integer,
    p_stat_men integer,
    p_attack_attribute_type smallint,
    p_attack_attribute_value smallint,
    p_defend_attribute_0 smallint,
    p_defend_attribute_1 smallint,
    p_defend_attribute_2 smallint,
    p_defend_attribute_3 smallint,
    p_defend_attribute_4 smallint,
    p_defend_attribute_5 smallint,
    p_social_class integer,
    p_carry_weight double precision,
    p_subjob_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."user_extra_info" AS extra_info
    SET "pattack" = p_pattack,
        "pdefend" = p_pdefend,
        "phit" = p_phit,
        "pavoid" = p_pavoid,
        "pcritical" = p_pcritical,
        "pattackspeed" = p_pattackspeed,
        "mattack" = p_mattack,
        "mdefend" = p_mdefend,
        "mhit" = p_mhit,
        "mavoid" = p_mavoid,
        "mcritical" = p_mcritical,
        "mattackspeed" = p_mattackspeed,
        "speed" = p_speed,
        "stat_str" = p_stat_str,
        "stat_dex" = p_stat_dex,
        "stat_con" = p_stat_con,
        "stat_int" = p_stat_int,
        "stat_wit" = p_stat_wit,
        "stat_men" = p_stat_men,
        "attack_attribute_type" = p_attack_attribute_type,
        "attack_attribute_value" = p_attack_attribute_value,
        "defend_attribute_0" = p_defend_attribute_0,
        "defend_attribute_1" = p_defend_attribute_1,
        "defend_attribute_2" = p_defend_attribute_2,
        "defend_attribute_3" = p_defend_attribute_3,
        "defend_attribute_4" = p_defend_attribute_4,
        "defend_attribute_5" = p_defend_attribute_5,
        "social_class" = p_social_class,
        "carry_weight" = p_carry_weight
    WHERE extra_info."char_id" = p_char_id
      AND extra_info."subjob_id" = p_subjob_id;

    IF NOT FOUND THEN
        INSERT INTO "world"."user_extra_info" (
            "char_id",
            "pattack", "pdefend", "phit", "pavoid", "pcritical",
            "pattackspeed", "mattack", "mdefend", "mhit", "mavoid",
            "mcritical", "mattackspeed", "speed", "stat_str", "stat_dex",
            "stat_con", "stat_int", "stat_wit", "stat_men",
            "attack_attribute_type", "attack_attribute_value",
            "defend_attribute_0", "defend_attribute_1",
            "defend_attribute_2", "defend_attribute_3",
            "defend_attribute_4", "defend_attribute_5",
            "social_class", "carry_weight", "subjob_id"
        ) VALUES (
            p_char_id,
            p_pattack, p_pdefend, p_phit, p_pavoid, p_pcritical,
            p_pattackspeed, p_mattack, p_mdefend, p_mhit, p_mavoid,
            p_mcritical, p_mattackspeed, p_speed, p_stat_str, p_stat_dex,
            p_stat_con, p_stat_int, p_stat_wit, p_stat_men,
            p_attack_attribute_type, p_attack_attribute_value,
            p_defend_attribute_0, p_defend_attribute_1,
            p_defend_attribute_2, p_defend_attribute_3,
            p_defend_attribute_4, p_defend_attribute_5,
            p_social_class, p_carry_weight, p_subjob_id
        );
    END IF;
END;
$function$;

COMMIT;
