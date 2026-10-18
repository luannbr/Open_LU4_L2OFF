-- Current Cached.exe CManor/CManorDb and primary opcode 208.
--
-- The current machine image fixes all six procedure ABIs and result-column
-- order.  Classic 3.0, modified 287 and Ertheia keep identical table
-- layouts and relational effects for the compatible full-arity bodies.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadmanorinfo"(
    p_manor_id integer
) RETURNS TABLE (
    residence_id integer,
    adena_vault bigint,
    crop_buy_vault bigint,
    change_state integer,
    last_changed text
)
LANGUAGE plpgsql
STABLE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."manor_info"
        WHERE "manor_info"."manor_id" = p_manor_id
    ) THEN
        RETURN QUERY
        SELECT
            info."residence_id",
            info."adena_vault",
            info."crop_buy_vault",
            info."change_state"::integer,
            to_char(info."last_changed", 'YYYY-MM-DD HH24:MI:SS')
        FROM "world"."manor_info" AS info
        WHERE info."manor_id" = p_manor_id;
    ELSE
        RETURN QUERY
        SELECT
            0::integer,
            0::bigint,
            0::bigint,
            0::integer,
            '0000-00-00 00:00:00'::text;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadmanorseed"(
    p_manor_id integer
) RETURNS TABLE (
    data_index integer,
    seed_id integer,
    seed_price bigint,
    seed_sell_count bigint,
    seed_remain_count bigint,
    crop_id integer,
    crop_price bigint,
    crop_buy_count bigint,
    crop_remain_count bigint,
    crop_type integer,
    crop_deposit bigint
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        data."data_index",
        data."seed_id",
        data."seed_price",
        data."seed_sell_count",
        data."seed_remain_count",
        data."crop_id",
        data."crop_price",
        data."crop_buy_count",
        data."crop_remain_count",
        data."crop_type",
        data."crop_deposit"
    FROM "world"."manor_data" AS data
    WHERE data."manor_id" = p_manor_id
    ORDER BY data."data_index";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadmanorseed_n"(
    p_manor_id integer
) RETURNS TABLE (
    data_index integer,
    seed_id_n integer,
    seed_price_n bigint,
    seed_sell_count_n bigint,
    crop_id_n integer,
    crop_buy_count_n bigint,
    crop_price_n bigint,
    crop_type_n integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        data."data_index",
        data."seed_id_n",
        data."seed_price_n",
        data."seed_sell_count_n",
        data."crop_id_n",
        data."crop_buy_count_n",
        data."crop_price_n",
        data."crop_type_n"
    FROM "world"."manor_data_n" AS data
    WHERE data."manor_id" = p_manor_id
    ORDER BY data."data_index";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savemanorinfo"(
    p_manor_id integer,
    p_residence_id integer,
    p_adena_seed_sell bigint,
    p_adena_crop_buy bigint,
    p_change_state smallint,
    p_last_changed text
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."manor_info"
        WHERE "manor_info"."manor_id" = p_manor_id
    ) THEN
        UPDATE "world"."manor_info"
        SET "residence_id" = p_residence_id,
            "adena_vault" = p_adena_seed_sell,
            "crop_buy_vault" = p_adena_crop_buy,
            "change_state" = p_change_state,
            "last_changed" =
                to_timestamp(
                    p_last_changed,
                    'YYYY-MM-DD HH24:MI:SS'
                )::timestamp without time zone
        WHERE "manor_info"."manor_id" = p_manor_id;
    ELSE
        INSERT INTO "world"."manor_info" (
            "manor_id",
            "residence_id",
            "adena_vault",
            "crop_buy_vault",
            "change_state",
            "last_changed"
        ) VALUES (
            p_manor_id,
            p_residence_id,
            p_adena_seed_sell,
            p_adena_crop_buy,
            p_change_state,
            to_timestamp(
                p_last_changed,
                'YYYY-MM-DD HH24:MI:SS'
            )::timestamp without time zone
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savemanorseed"(
    p_manor_id integer,
    p_data_index integer,
    p_seed_id bigint,
    p_seed_price bigint,
    p_seed_sell_count bigint,
    p_seed_remain_count bigint,
    p_crop_id bigint,
    p_crop_buy_count bigint,
    p_crop_remain_count bigint,
    p_crop_price bigint,
    p_crop_type bigint,
    p_crop_deposit bigint
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."manor_data"
        WHERE "manor_data"."manor_id" = p_manor_id
          AND "manor_data"."data_index" = p_data_index
    ) THEN
        UPDATE "world"."manor_data"
        SET "seed_id" = p_seed_id::integer,
            "seed_price" = p_seed_price,
            "seed_sell_count" = p_seed_sell_count,
            "seed_remain_count" = p_seed_remain_count,
            "crop_id" = p_crop_id::integer,
            "crop_buy_count" = p_crop_buy_count,
            "crop_remain_count" = p_crop_remain_count,
            "crop_price" = p_crop_price,
            "crop_type" = p_crop_type::integer,
            "crop_deposit" = p_crop_deposit
        WHERE "manor_data"."manor_id" = p_manor_id
          AND "manor_data"."data_index" = p_data_index;
    ELSE
        INSERT INTO "world"."manor_data" (
            "manor_id",
            "data_index",
            "seed_id",
            "seed_price",
            "seed_sell_count",
            "seed_remain_count",
            "crop_id",
            "crop_buy_count",
            "crop_remain_count",
            "crop_price",
            "crop_type",
            "crop_deposit"
        ) VALUES (
            p_manor_id,
            p_data_index,
            p_seed_id::integer,
            p_seed_price,
            p_seed_sell_count,
            p_seed_remain_count,
            p_crop_id::integer,
            p_crop_buy_count,
            p_crop_remain_count,
            p_crop_price,
            p_crop_type::integer,
            p_crop_deposit
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savemanorseed_n"(
    p_manor_id integer,
    p_data_index integer,
    p_seed_id_n bigint,
    p_seed_price_n bigint,
    p_seed_sell_count_n bigint,
    p_crop_id_n bigint,
    p_crop_buy_count_n bigint,
    p_crop_price_n bigint,
    p_crop_type_n bigint
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."manor_data_n"
        WHERE "manor_data_n"."manor_id" = p_manor_id
          AND "manor_data_n"."data_index" = p_data_index
    ) THEN
        UPDATE "world"."manor_data_n"
        SET "seed_id_n" = p_seed_id_n::integer,
            "seed_price_n" = p_seed_price_n,
            "seed_sell_count_n" = p_seed_sell_count_n,
            "crop_id_n" = p_crop_id_n::integer,
            "crop_buy_count_n" = p_crop_buy_count_n,
            "crop_price_n" = p_crop_price_n,
            "crop_type_n" = p_crop_type_n::integer
        WHERE "manor_data_n"."manor_id" = p_manor_id
          AND "manor_data_n"."data_index" = p_data_index;
    ELSE
        INSERT INTO "world"."manor_data_n" (
            "manor_id",
            "data_index",
            "seed_id_n",
            "seed_price_n",
            "seed_sell_count_n",
            "crop_id_n",
            "crop_buy_count_n",
            "crop_price_n",
            "crop_type_n"
        ) VALUES (
            p_manor_id,
            p_data_index,
            p_seed_id_n::integer,
            p_seed_price_n,
            p_seed_sell_count_n,
            p_crop_id_n::integer,
            p_crop_buy_count_n,
            p_crop_price_n,
            p_crop_type_n::integer
        );
    END IF;
END;
$function$;

COMMIT;
