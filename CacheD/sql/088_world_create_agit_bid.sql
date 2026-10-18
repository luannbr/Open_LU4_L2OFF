-- Current Cached.exe primary opcode 150 (RequestCreateAgitBid).
--
-- The current machine fixes the two-input/three-result lookup and the
-- five-input/three-result create-or-update ABIs. Classic 3.0, modified 287
-- and Ertheia have byte-identical substantive bodies at those widths.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_getagitbidone"(
    p_auction_id integer,
    p_pledge_id integer
) RETURNS TABLE (
    auction_id integer,
    bid_id integer,
    last_bid bigint
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        p_auction_id,
        COALESCE(b."attend_id", 0),
        COALESCE(b."attend_price", 0::bigint)
    FROM (VALUES (1)) AS one_row(marker)
    LEFT JOIN LATERAL (
        SELECT
            ab."attend_id",
            ab."attend_price"
        FROM "world"."agit_bid" AS ab
        WHERE ab."auction_id" = p_auction_id
          AND ab."attend_pledge_id" = p_pledge_id
    ) AS b ON true;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_createagitbid"(
    p_agit_id integer,
    p_auction_id integer,
    p_price bigint,
    p_pledge_id integer,
    p_attend_time integer
) RETURNS TABLE (
    auction_id integer,
    bid_id integer,
    diff_adena bigint
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_last_bid bigint := 0;
    v_bid_id integer := 0;
    v_diff_adena bigint := 0;
BEGIN
    -- p_agit_id is part of the current callable ABI but, as in every
    -- compatible modern donor, is not consumed by the relational body.
    SELECT
        COALESCE(ab."attend_price", 0::bigint),
        COALESCE(ab."attend_id", 0)
    INTO
        v_last_bid,
        v_bid_id
    FROM "world"."agit_bid" AS ab
    WHERE ab."auction_id" = p_auction_id
      AND ab."attend_pledge_id" = p_pledge_id;

    IF FOUND THEN
        UPDATE "world"."agit_bid"
        SET
            "attend_price" = p_price,
            "attend_time" = p_attend_time
        WHERE "agit_bid"."auction_id" = p_auction_id
          AND "agit_bid"."attend_pledge_id" = p_pledge_id;
        v_diff_adena := v_last_bid - p_price;
    ELSE
        INSERT INTO "world"."agit_bid" (
            "auction_id",
            "attend_price",
            "attend_pledge_id",
            "attend_time"
        ) VALUES (
            p_auction_id,
            p_price,
            p_pledge_id,
            p_attend_time
        )
        RETURNING "agit_bid"."attend_id"
        INTO v_bid_id;
        v_diff_adena := p_price;
    END IF;

    RETURN QUERY SELECT p_auction_id, v_bid_id, v_diff_adena;
END;
$function$;

COMMIT;
