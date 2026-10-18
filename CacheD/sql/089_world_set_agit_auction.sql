-- Current Cached.exe primary opcode 151 (RequestSetAgitAuction).
--
-- The current machine fixes the three-input/four-result auction close ABI,
-- the three-input/two-result adena lookup and the one-input/four-result bid
-- enumeration. Classic 3.0, modified 287 and Ertheia have identical modern
-- relational bodies at those exact widths.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_setagitauction"(
    p_agit_id integer,
    p_auction_id integer,
    p_next_cost integer
) RETURNS TABLE (
    attend_id integer,
    max_price bigint,
    pledge_id integer,
    tax integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_attend_id integer := 0;
    v_max_price bigint := 0;
    v_pledge_id integer := 0;
    v_tax integer := 0;
BEGIN
    SELECT
        COALESCE(ab."attend_id", 0),
        COALESCE(ab."attend_price", 0::bigint),
        COALESCE(ab."attend_pledge_id", 0)
    INTO
        v_attend_id,
        v_max_price,
        v_pledge_id
    FROM "world"."agit_bid" AS ab
    WHERE ab."auction_id" = p_auction_id
    ORDER BY
        ab."attend_price" DESC,
        ab."attend_id" ASC
    LIMIT 1;

    IF NOT FOUND THEN
        v_attend_id := 0;
        v_max_price := 0;
        v_pledge_id := 0;
    END IF;

    IF v_max_price > 0 THEN
        UPDATE "world"."agit_auction"
        SET "accepted_bid" = v_attend_id
        WHERE "agit_auction"."auction_id" = p_auction_id;

        UPDATE "world"."agit"
        SET
            "auction_id" = NULL,
            "last_price" = v_max_price,
            "next_cost" = p_next_cost,
            "cost_fail" = NULL
        WHERE "agit"."id" = p_agit_id;

        UPDATE "world"."pledge"
        SET "agit_id" = p_agit_id
        WHERE "pledge"."pledge_id" = v_pledge_id;

        SELECT COALESCE((
            SELECT aa."auction_tax"
            FROM "world"."agit_auction" AS aa
            WHERE aa."auction_id" = p_auction_id
        ), 0)
        INTO v_tax;
    ELSE
        UPDATE "world"."agit_auction"
        SET "accepted_bid" = 0
        WHERE "agit_auction"."auction_id" = p_auction_id;

        UPDATE "world"."agit"
        SET
            "auction_id" = NULL,
            "next_cost" = 0,
            "cost_fail" = NULL
        WHERE "agit"."id" = p_agit_id;
    END IF;

    RETURN QUERY SELECT
        v_attend_id,
        v_max_price,
        v_pledge_id,
        v_tax;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getagitadena"(
    p_agit_id integer,
    p_auction_id integer,
    p_reason integer
) RETURNS TABLE (
    adena bigint,
    pledge_id integer
)
LANGUAGE plpgsql
STABLE
AS $function$
BEGIN
    IF p_reason = 1 THEN
        RETURN QUERY
        SELECT
            COALESCE(aa."adena", 0::bigint),
            COALESCE(aa."pledge_id", 0)
        FROM "world"."agit_adena" AS aa
        WHERE aa."agit_id" = p_agit_id
          AND aa."auction_id" = p_auction_id
          AND aa."reason" = p_reason
        LIMIT 1;
    ELSE
        RETURN QUERY
        SELECT
            COALESCE(aa."adena", 0::bigint),
            COALESCE(aa."pledge_id", 0)
        FROM "world"."agit_adena" AS aa
        WHERE aa."agit_id" = p_agit_id
          AND aa."auction_id" = p_auction_id
          AND aa."reason" = p_reason;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getagitbid"(
    p_auction_id integer
) RETURNS TABLE (
    attend_id integer,
    attend_price bigint,
    attend_pledge_id integer,
    attend_time integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        ab."attend_id",
        ab."attend_price",
        ab."attend_pledge_id",
        ab."attend_time"
    FROM "world"."agit_bid" AS ab
    WHERE ab."auction_id" = p_auction_id;
$function$;

COMMIT;
