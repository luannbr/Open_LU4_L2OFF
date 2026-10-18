-- Current Cached.exe primary opcodes 166 (RequestDeleteCastleGuard) and 168
-- (RequestAgitBidCancel).
--
-- The current image fixes the one-input delete-all call and the two-input,
-- four-result bid deletion ABI. Classic 3.0, modified 287 and Ertheia keep
-- the same relational effects. Their current-width bid price is bigint.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_deleteallresidenceguard"(
    p_residence_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."residence_guard"
    WHERE "residence_guard"."residence_id" = p_residence_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deleteagitbid"(
    p_agit_id integer,
    p_pledge_id integer
) RETURNS TABLE (
    agit_id integer,
    pledge_id integer,
    auction_id integer,
    price bigint
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_auction_id integer := 0;
    v_price bigint := 0;
BEGIN
    SELECT COALESCE((
        SELECT a."auction_id"
        FROM "world"."agit" AS a
        WHERE a."id" = p_agit_id
    ), 0)
    INTO v_auction_id;

    IF v_auction_id > 0 THEN
        SELECT COALESCE((
            SELECT ab."attend_price"
            FROM "world"."agit_bid" AS ab
            WHERE ab."auction_id" = v_auction_id
              AND ab."attend_pledge_id" = p_pledge_id
        ), 0::bigint)
        INTO v_price;

        DELETE FROM "world"."agit_bid"
        WHERE "agit_bid"."auction_id" = v_auction_id
          AND "agit_bid"."attend_pledge_id" = p_pledge_id;
    END IF;

    RETURN QUERY
    SELECT p_agit_id, p_pledge_id, v_auction_id, v_price;
END;
$function$;

COMMIT;
