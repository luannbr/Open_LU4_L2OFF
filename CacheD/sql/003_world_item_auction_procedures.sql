-- The fourteen item auction procedures that Cached.exe calls. Five bodies
-- were rewritten from the observed behavior, because the original T-SQL is
-- not available.

BEGIN;

CREATE OR REPLACE FUNCTION "world"."lin_createitemauction"(
    p_auction_npc integer,
    p_start_time integer,
    p_end_time integer,
    p_item_index integer
) RETURNS TABLE (auction_id integer)
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."item_auction" (
        auction_npc, start_time, end_time, item_index,
        item_type, next_auction_display_start
    ) VALUES (
        p_auction_npc, p_start_time, p_end_time, p_item_index, 0, 0
    )
    RETURNING item_auction.auction_id;
$function$;

-- Behavioral reconstruction.  The ancestor procedure accepted only a
-- strictly increasing active price.  Current Cached adds a second result
-- and logs that rejected path as a DB transaction-order disorder.
CREATE OR REPLACE FUNCTION "world"."lin_createitemauctionbid"(
    p_auction_id integer,
    p_user_dbid integer,
    p_bidding_price bigint
) RETURNS TABLE (bid_id integer, db_transaction_disorder_flag integer)
LANGUAGE sql
VOLATILE
AS $function$
    WITH active_max AS (
        SELECT COALESCE(MAX(b.bidding_price), 0::bigint) AS bidding_price
        FROM "world"."item_auction_bid" AS b
        WHERE b.auction_id = p_auction_id AND b.withdraw = 0
    ),
    inserted AS (
        INSERT INTO "world"."item_auction_bid" (
            auction_id, char_id, bidding_price
        )
        SELECT p_auction_id, p_user_dbid, p_bidding_price
        FROM active_max
        WHERE active_max.bidding_price = 0
           OR active_max.bidding_price < p_bidding_price
        RETURNING item_auction_bid.bid_id
    )
    SELECT
        COALESCE((SELECT inserted.bid_id FROM inserted), 0),
        CASE WHEN EXISTS (SELECT 1 FROM inserted) THEN 0 ELSE 1 END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deleteitemauctionbid"(
    p_bid_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."item_auction_bid" WHERE bid_id = p_bid_id;
$function$;

-- The shipping CacheD handler has a confirmed one-bind/two-placeholder
-- defect, so this body is dormant on that path.  The paired L2Server
-- fixes the intended arguments as (auction_id, item_index).
CREATE OR REPLACE FUNCTION "world"."lin_setitemauctioninfo"(
    p_auction_id integer,
    p_item_index integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."item_auction"
    SET item_index = p_item_index
    WHERE auction_id = p_auction_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadallauction"(
    p_limit_time integer
) RETURNS TABLE (
    auction_id integer,
    auction_status integer,
    extend_status integer,
    auction_npc integer,
    start_time integer,
    end_time integer,
    item_index integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        a.auction_id,
        a.auction_status::integer,
        a.extend_status::integer,
        a.auction_npc,
        a.start_time,
        a.end_time,
        a.item_index
    FROM "world"."item_auction" AS a
    WHERE a.start_time > p_limit_time;
$function$;

-- Successful delivery consumes the winner's one active bid.  Matching
-- the amount is required because that exact amount was carried by the
-- paired L2Server warehouse manipulation.
CREATE OR REPLACE FUNCTION "world"."lin_successfulitemauctionbid"(
    p_auction_id integer,
    p_user_dbid integer,
    p_adena_amount bigint
) RETURNS TABLE (bid_id integer)
LANGUAGE sql
VOLATILE
AS $function$
    WITH candidate AS (
        SELECT b.bid_id
        FROM "world"."item_auction_bid" AS b
        WHERE b.auction_id = p_auction_id
          AND b.char_id = p_user_dbid
          AND b.bidding_price = p_adena_amount
          AND b.withdraw = 0
        ORDER BY b.bid_id DESC
        LIMIT 1
    ),
    consumed AS (
        UPDATE "world"."item_auction_bid" AS b
        SET withdraw = 1
        FROM candidate AS c
        WHERE b.bid_id = c.bid_id
        RETURNING b.bid_id
    )
    SELECT COALESCE((SELECT consumed.bid_id FROM consumed), 0);
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_successfulitemauctionbid_rollback"(
    p_bid_id integer,
    p_auction_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."item_auction_bid"
    SET withdraw = 0
    WHERE bid_id = p_bid_id AND auction_id = p_auction_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadallauctionbid"(
    p_auction_id integer
) RETURNS TABLE (user_dbid integer, bidding_price bigint)
LANGUAGE sql
STABLE
AS $function$
    SELECT b.char_id, b.bidding_price
    FROM "world"."item_auction_bid" AS b
    WHERE b.auction_id = p_auction_id AND b.withdraw = 0;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setitemauctionendtime"(
    p_auction_id integer,
    p_end_time integer,
    p_extend_status integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."item_auction"
    SET end_time = p_end_time, extend_status = p_extend_status::smallint
    WHERE auction_id = p_auction_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setitemauctionstatus"(
    p_auction_id integer,
    p_auction_status integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."item_auction"
    SET auction_status = p_auction_status::smallint
    WHERE auction_id = p_auction_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatecurrentitemauctioninfo"(
    p_auction_id integer,
    p_item_type integer,
    p_item_amount bigint,
    p_init_price bigint
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."item_auction"
    SET item_type = p_item_type,
        item_amount = p_item_amount,
        init_price = p_init_price
    WHERE auction_id = p_auction_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatenextauctiondisplaystarttime"(
    p_auction_id integer,
    p_next_auction_display_start integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."item_auction"
    SET next_auction_display_start = p_next_auction_display_start
    WHERE auction_id = p_auction_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatenextitemauctioninfo"(
    p_auction_id integer,
    p_next_item_type integer,
    p_next_item_amount bigint,
    p_next_init_price bigint
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."item_auction"
    SET next_item_type = p_next_item_type,
        next_item_amount = p_next_item_amount,
        next_init_price = p_next_init_price
    WHERE auction_id = p_auction_id;
$function$;

-- The leading bid_id is zero for the ordinary path and the exact row id
-- for compensation.  This removes the ancestor bug that reactivated all
-- historical bids for the same user during rollback.
CREATE OR REPLACE FUNCTION "world"."lin_withdrawitemauctionbid"(
    p_bid_id integer,
    p_auction_id integer,
    p_user_dbid integer,
    p_withdraw_flag integer
) RETURNS TABLE (bid_id integer, adena_amount bigint)
LANGUAGE sql
VOLATILE
AS $function$
    WITH candidate AS (
        SELECT b.bid_id, b.bidding_price
        FROM "world"."item_auction_bid" AS b
        WHERE b.auction_id = p_auction_id
          AND b.char_id = p_user_dbid
          AND (
                (p_withdraw_flag = 1 AND p_bid_id = 0 AND b.withdraw = 0)
             OR (p_withdraw_flag = 0 AND b.bid_id = p_bid_id)
          )
        ORDER BY b.bid_id DESC
        LIMIT 1
    ),
    changed AS (
        UPDATE "world"."item_auction_bid" AS b
        SET withdraw = p_withdraw_flag::smallint
        FROM candidate AS c
        WHERE b.bid_id = c.bid_id
        RETURNING b.bid_id, c.bidding_price
    )
    SELECT
        COALESCE((SELECT changed.bid_id FROM changed), 0),
        CASE
            WHEN p_withdraw_flag = 1
            THEN COALESCE((SELECT changed.bidding_price FROM changed), 0)
            ELSE 0::bigint
        END;
$function$;

COMMIT;
