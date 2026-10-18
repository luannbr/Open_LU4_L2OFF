-- Current Cached.exe primary opcodes 94 through 98 persistence.
--
-- The current handlers fix every callable ABI and bind order. The residence
-- income, contribution and agit effects are supplied by full-arity donor
-- definitions whose relational effects agree with the current tables. The
-- current RequestSaveCastleIncome path supplies zero for the
-- legacy crop-income argument and uses 64-bit shop-income values.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_savecastleincome"(
    p_crop_income integer,
    p_shop_income bigint,
    p_residence_id integer,
    p_shop_income_temp bigint,
    p_is_castle integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF p_is_castle = 1 THEN
        UPDATE "world"."castle"
        SET
            "crop_income" = p_crop_income,
            "shop_income" = p_shop_income,
            "shop_income_temp" = p_shop_income_temp
        WHERE "id" = p_residence_id;
    ELSE
        UPDATE "world"."agit"
        SET
            "shop_income" = p_shop_income,
            "shop_income_temp" = p_shop_income_temp
        WHERE "id" = p_residence_id;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updateagitstove"(
    p_stove integer,
    p_expire integer,
    p_agit_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."agit"
    SET
        "hp_stove" = p_stove,
        "hp_stove_expire" = p_expire
    WHERE "id" = p_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updateagitflame"(
    p_flame integer,
    p_expire integer,
    p_agit_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."agit"
    SET
        "mp_flame" = p_flame,
        "mp_flame_expire" = p_expire
    WHERE "id" = p_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updateagithatcher"(
    p_hatcher integer,
    p_agit_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."agit"
    SET "hatcher" = p_hatcher
    WHERE "id" = p_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updateagitteleport"(
    p_teleport_level integer,
    p_agit_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."agit"
    SET "teleport_level" = p_teleport_level
    WHERE "id" = p_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updateagitpledgeid"(
    p_pledge_id integer,
    p_agit_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."agit"
    SET "pledge_id" = p_pledge_id
    WHERE "id" = p_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updateagitauctionprice"(
    p_auction_price integer,
    p_agit_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."agit"
    SET "auction_price" = p_auction_price
    WHERE "id" = p_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updateagitauctiondata"(
    p_auction_date integer,
    p_agit_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."agit"
    SET "auction_date" = p_auction_date
    WHERE "id" = p_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updateagitstatus"(
    p_status integer,
    p_agit_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."agit"
    SET "status" = p_status
    WHERE "id" = p_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updateagitnextwartime"(
    p_next_war_time integer,
    p_agit_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."agit"
    SET "next_war_time" = p_next_war_time
    WHERE "id" = p_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getagitlist"()
RETURNS TABLE (
    agit_id integer,
    agit_name text,
    pledge_id integer,
    pledge_name text,
    ruler_id integer,
    ruler_name text,
    auction_time integer,
    next_war_time integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        agit."id"::integer,
        agit."name"::text,
        COALESCE(agit."pledge_id", 0)::integer,
        COALESCE(pledge."name", '')::text,
        COALESCE(pledge."ruler_id", 0)::integer,
        COALESCE(ruler."char_name", '')::text,
        COALESCE((
            SELECT MAX(auction."auction_time")
            FROM "world"."agit_auction" AS auction
            WHERE auction."agit_id" = agit."id"
        ), 0)::integer,
        COALESCE(agit."next_war_time", 0)::integer
    FROM "world"."agit" AS agit
    LEFT JOIN "world"."pledge" AS pledge
      ON pledge."pledge_id" = agit."pledge_id"
    LEFT JOIN "world"."user_data" AS ruler
      ON ruler."char_id" = pledge."ruler_id";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getcastlelist"()
RETURNS TABLE (
    castle_id integer,
    pledge_id integer,
    pledge_name text,
    ruler_id integer,
    ruler_name text,
    alliance_id integer,
    alliance_name text,
    tax_rate integer,
    next_tax_rate integer,
    next_war_time integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        castle."id"::integer,
        COALESCE(castle."pledge_id", 0)::integer,
        COALESCE(pledge."name", '')::text,
        COALESCE(pledge."ruler_id", 0)::integer,
        COALESCE(ruler."char_name", '')::text,
        COALESCE(pledge."alliance_id", 0)::integer,
        COALESCE(alliance."name", '')::text,
        castle."tax_rate"::integer,
        CASE
            WHEN castle."tax_rate_to_change" = -1 THEN 0
            ELSE castle."tax_rate_to_change"::integer
        END,
        castle."next_war_time"::integer
    FROM "world"."castle" AS castle
    LEFT JOIN "world"."pledge" AS pledge
      ON pledge."pledge_id" = castle."pledge_id"
    LEFT JOIN "world"."user_data" AS ruler
      ON ruler."char_id" = pledge."ruler_id"
    LEFT JOIN "world"."alliance" AS alliance
      ON alliance."id" = pledge."alliance_id"
    ORDER BY castle."id";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savepledgecontribution"(
    p_residence_id integer,
    p_pledge_id integer,
    p_contribution integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."pledge_contribution"
    SET "contribution" = "contribution" + p_contribution
    WHERE "residence_id" = p_residence_id
      AND "pledge_id" = p_pledge_id;

    IF NOT FOUND THEN
        INSERT INTO "world"."pledge_contribution" (
            "residence_id",
            "pledge_id",
            "contribution"
        ) VALUES (
            p_residence_id,
            p_pledge_id,
            p_contribution
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getcontributionwinnerpledge"(
    p_agit_id integer,
    p_register_only integer
) RETURNS TABLE (
    pledge_id integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT pledge."pledge_id"::integer
    FROM "world"."pledge" AS pledge
    JOIN "world"."pledge_contribution" AS contribution
      ON contribution."pledge_id" = pledge."pledge_id"
    WHERE (pledge."agit_id" = p_agit_id OR pledge."agit_id" = 0)
      AND pledge."skill_level" >= 4
      AND contribution."residence_id" = p_agit_id
      AND (
          p_register_only = 0
          OR EXISTS (
              SELECT 1
              FROM "world"."siege_agit_pledge" AS registration
              WHERE registration."agit_id" = p_agit_id
                AND registration."pledge_id" = pledge."pledge_id"
          )
      )
    ORDER BY contribution."contribution" DESC;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletepledgecontribution"(
    p_residence_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."pledge_contribution"
    WHERE "residence_id" = p_residence_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getcontributionrelatedpledge"(
    p_residence_id integer
) RETURNS TABLE (
    pledge_id integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT contribution."pledge_id"::integer
    FROM "world"."pledge_contribution" AS contribution
    WHERE contribution."residence_id" = p_residence_id;
$function$;

COMMIT;
