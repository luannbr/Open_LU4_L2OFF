-- Extended requests 211-220. Current Cached.exe fixes every callable ABI,
-- binding/result order and packet page boundary in this slice. The relational
-- bodies are kept by the compatible Classic 3.0 database donor. Paired
-- current L2Server.exe fixes the BR request/reply field order.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

-- olympiad_match is owned by migration 113, before every function that uses
-- it. Do not redeclare the table here: this migration only adds the later
-- request-211..220 callable surfaces.

CREATE OR REPLACE FUNCTION "world"."lin_loadolympiadmatchcount"()
RETURNS TABLE (
    char_id integer,
    season integer,
    step integer,
    game_rule integer,
    match_count integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT match_count.char_id,
           match_count.season,
           match_count.step,
           match_count.game_rule,
           match_count.match_count
    FROM "world"."olympiad_match_count" AS match_count
    ORDER BY match_count.char_id,
             match_count.season,
             match_count.step,
             match_count.game_rule;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_saveolympiadmatchcount"(
    p_char_id integer,
    p_season integer,
    p_step integer,
    p_game_rule integer,
    p_count integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_updated integer;
BEGIN
    -- The donor identifies an existing counter by character and
    -- game rule only, then replaces season and step as part of the update.
    UPDATE "world"."olympiad_match_count" AS match_count
    SET season = p_season,
        step = p_step,
        match_count = p_count
    WHERE match_count.char_id = p_char_id
      AND match_count.game_rule = p_game_rule;

    GET DIAGNOSTICS v_updated = ROW_COUNT;
    IF v_updated = 0 THEN
        INSERT INTO "world"."olympiad_match_count" (
            char_id, season, step, game_rule, match_count
        ) VALUES (
            p_char_id, p_season, p_step, p_game_rule, p_count
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getolympiadplayercount"(
    p_season integer
) RETURNS TABLE (player_count integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT COUNT(DISTINCT match.char_id)::integer
    FROM "world"."olympiad_match" AS match
    WHERE match.season = p_season;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadpremiumitemreceive"(
    p_warehouse_no bigint
) RETURNS TABLE (item_id integer)
LANGUAGE plpgsql
STABLE
AS $function$
DECLARE
    v_item_dbid integer;
BEGIN
    -- Keep the donor's scalar-subquery behavior: more than one matching
    -- receipt is an execution error, which the current CacheD caller treats
    -- exactly like any other failed ExecutePrepared call.
    v_item_dbid := (
        SELECT receive.item_dbid
        FROM "world"."user_premium_item_receive" AS receive
        WHERE receive.warehouse_no = p_warehouse_no
          AND receive.product_id > 0
    );

    IF v_item_dbid IS NULL THEN
        RETURN QUERY SELECT -1;
    ELSE
        RETURN QUERY
        SELECT item.item_id
        FROM "world"."user_item" AS item
        WHERE item.item_id = v_item_dbid;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_createpremiumitemreceive"(
    p_new_item_id integer,
    p_char_id integer,
    p_item_amount bigint,
    p_warehouse_no bigint,
    p_product_id integer,
    p_product_amount integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_updated integer;
BEGIN
    -- product_amount is carried by the shipping ABI but the donor body does
    -- not consume it after recording the individual received item amount.
    PERFORM p_product_amount;

    INSERT INTO "world"."user_premium_item_receive" (
        warehouse_no,
        real_recipient_char_id,
        item_dbid,
        item_amount,
        product_id,
        receive_date
    ) VALUES (
        p_warehouse_no,
        p_char_id,
        p_new_item_id,
        p_item_amount,
        p_product_id,
        "world"."mssql_getdate"()
    );

    UPDATE "world"."br_recent_product" AS recent
    SET buy_date = "world"."mssql_getdate"()
    WHERE recent.char_id = p_char_id
      AND recent.product_id = p_product_id;
    GET DIAGNOSTICS v_updated = ROW_COUNT;
    IF v_updated = 0 THEN
        INSERT INTO "world"."br_recent_product" (
            char_id, product_id, buy_date
        ) VALUES (
            p_char_id, p_product_id, "world"."mssql_getdate"()
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_brgetlimitedshopiteminfo"(
    p_account_id integer,
    p_char_id integer,
    p_product_id integer
) RETURNS TABLE (
    buy_count integer,
    buytime integer
)
LANGUAGE plpgsql
STABLE
AS $function$
BEGIN
    -- The donor accepts char_id but keys the lookup by account/product only.
    PERFORM p_char_id;
    RETURN QUERY
    SELECT shop_limit.buy_count,
           shop_limit.buytime
    FROM "world"."br_user_shopitem_limit" AS shop_limit
    WHERE shop_limit.account_id = p_account_id
      AND shop_limit.product_id = p_product_id;

    IF NOT FOUND THEN
        RETURN QUERY SELECT 0, -1;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_brsetlimitedshopiteminfo"(
    p_account_id integer,
    p_char_id integer,
    p_product_id integer,
    p_count integer,
    p_buytime integer,
    p_max_count integer,
    p_limit_day integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_updated integer;
BEGIN
    IF p_buytime <> 0 THEN
        UPDATE "world"."br_user_shopitem_limit" AS shop_limit
        SET buy_count = shop_limit.buy_count + p_count,
            buytime = p_buytime
        WHERE shop_limit.account_id = p_account_id
          AND shop_limit.product_id = p_product_id;
    ELSE
        UPDATE "world"."br_user_shopitem_limit" AS shop_limit
        SET buy_count = shop_limit.buy_count + p_count
        WHERE shop_limit.account_id = p_account_id
          AND shop_limit.product_id = p_product_id;
    END IF;

    GET DIAGNOSTICS v_updated = ROW_COUNT;
    IF v_updated = 0 THEN
        INSERT INTO "world"."br_user_shopitem_limit" (
            account_id,
            char_id,
            product_id,
            buy_count,
            buy_date_log,
            buytime,
            max_count,
            limit_day
        ) VALUES (
            p_account_id,
            p_char_id,
            p_product_id,
            p_count,
            "world"."mssql_getdate"(),
            p_buytime,
            p_max_count,
            p_limit_day
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_brsaveproductsoldcount"(
    p_product_id integer,
    p_product_amount integer
) RETURNS TABLE (sold_count integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    INSERT INTO "world"."br_product_sold_count" AS product (
        product_id, sold_count
    ) VALUES (
        p_product_id, p_product_amount
    )
    ON CONFLICT (product_id) DO UPDATE
    SET sold_count = product.sold_count + EXCLUDED.sold_count;

    RETURN QUERY
    SELECT product.sold_count
    FROM "world"."br_product_sold_count" AS product
    WHERE product.product_id = p_product_id;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_brloadproductsoldcount"(
    p_product_id integer
) RETURNS TABLE (sold_count integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT CASE
        WHEN product.sold_count > 0 THEN product.sold_count
        ELSE 0
    END
    FROM (VALUES (
        COALESCE((
            SELECT current_product.sold_count
            FROM "world"."br_product_sold_count" AS current_product
            WHERE current_product.product_id = p_product_id
        ), -1)
    )) AS product(sold_count);
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_brloadrecentproduct"(
    p_char_id integer,
    p_product_count integer
) RETURNS TABLE (product_id integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT recent.product_id
    FROM "world"."br_recent_product" AS recent
    WHERE recent.char_id = p_char_id
    ORDER BY recent.buy_date DESC
    LIMIT p_product_count;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_brupdateplayinguser"(
    p_playing_user integer
) RETURNS TABLE (playing_user integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_old_playing_count integer := -1;
BEGIN
    SELECT setting.playing_count
    INTO v_old_playing_count
    FROM "world"."branch_setting" AS setting
    LIMIT 1
    FOR UPDATE;

    IF NOT FOUND THEN
        v_old_playing_count := -1;
    END IF;

    IF v_old_playing_count = -1 THEN
        INSERT INTO "world"."branch_setting" (
            playing_user, playing_count
        ) VALUES (
            p_playing_user, 1
        );
        v_old_playing_count := 0;
    END IF;

    IF v_old_playing_count >= 72 THEN
        v_old_playing_count := 71;
    END IF;

    UPDATE "world"."branch_setting"
    SET playing_count = v_old_playing_count + 1;

    IF v_old_playing_count >= 0 THEN
        UPDATE "world"."branch_setting" AS setting
        SET playing_user =
            setting.playing_user + p_playing_user -
            trunc(
                setting.playing_user::double precision /
                setting.playing_count::double precision + 0.5
            )::integer;
    ELSE
        UPDATE "world"."branch_setting"
        SET playing_user = p_playing_user;
    END IF;

    IF v_old_playing_count < 0 THEN
        RETURN QUERY SELECT 4000;
    ELSE
        RETURN QUERY
        SELECT setting.playing_user / setting.playing_count
        FROM "world"."branch_setting" AS setting
        LIMIT 1;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_brinserteventranking"(
    p_event_id integer,
    p_round_no integer,
    p_char_id integer,
    p_score integer,
    p_char_name text
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_score integer;
    v_my_ranking integer;
BEGIN
    SELECT event_score.score
    INTO v_score
    FROM "world"."br_event_score" AS event_score
    WHERE event_score.event_id = p_event_id
      AND event_score.round_no = p_round_no
      AND event_score.char_id = p_char_id
    LIMIT 1;

    IF v_score IS NULL THEN
        INSERT INTO "world"."br_event_score" (
            event_id, round_no, char_id, score
        ) VALUES (
            p_event_id, p_round_no, p_char_id, p_score
        );
    ELSIF v_score < p_score THEN
        UPDATE "world"."br_event_score" AS event_score
        SET score = p_score,
            score_date = "world"."mssql_getdate"()
        WHERE event_score.event_id = p_event_id
          AND event_score.round_no = p_round_no
          AND event_score.char_id = p_char_id;
    ELSE
        RETURN;
    END IF;

    -- The donor omits event_id from this ranking count.
    SELECT COUNT(*)::integer
    INTO v_my_ranking
    FROM "world"."br_event_score" AS event_score
    WHERE event_score.score > p_score
      AND event_score.round_no = p_round_no;

    IF v_my_ranking < 1 THEN
        DELETE FROM "world"."br_event_top_players" AS top_player
        WHERE top_player.event_id = p_event_id
          AND top_player.round_no = p_round_no
          AND top_player.score < p_score;
        DELETE FROM "world"."br_event_top_players" AS top_player
        WHERE top_player.event_id = p_event_id
          AND top_player.round_no = p_round_no
          AND top_player.char_id = p_char_id;
        INSERT INTO "world"."br_event_top_players" (
            event_id,
            round_no,
            char_id,
            score,
            char_name,
            reward
        ) VALUES (
            p_event_id,
            p_round_no,
            p_char_id,
            p_score,
            p_char_name,
            0
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_brloadeventtoprankers"(
    p_event_id integer,
    p_round_no integer,
    p_char_id integer
) RETURNS TABLE (
    top_player_count integer,
    top_player_score integer,
    my_score integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        COUNT(top_player.score)::integer,
        COALESCE(MAX(top_player.score), -1)::integer,
        COALESCE((
            SELECT event_score.score
            FROM "world"."br_event_score" AS event_score
            WHERE event_score.event_id = p_event_id
              AND event_score.char_id = p_char_id
              AND event_score.round_no = p_round_no
            LIMIT 1
        ), -1)::integer
    FROM "world"."br_event_top_players" AS top_player
    WHERE top_player.event_id = p_event_id
      AND top_player.round_no = p_round_no;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_brupdateeventranking"(
    p_event_id integer,
    p_round_no integer
) RETURNS TABLE (char_name text)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_top_player_count integer;
    v_top_player_score integer := 0;
BEGIN
    SELECT COUNT(top_player.char_id)::integer
    INTO v_top_player_count
    FROM "world"."br_event_top_players" AS top_player
    WHERE top_player.event_id = p_event_id
      AND top_player.round_no = p_round_no;

    IF v_top_player_count > 0 THEN
        SELECT MAX(top_player.score)::integer
        INTO v_top_player_score
        FROM "world"."br_event_top_players" AS top_player
        WHERE top_player.event_id = p_event_id
          AND top_player.round_no = p_round_no;

        RETURN QUERY
        SELECT top_player.char_name::text
        FROM "world"."br_event_top_players" AS top_player
        WHERE top_player.reward = 0
          AND top_player.event_id = p_event_id
          AND top_player.round_no = p_round_no;

        UPDATE "world"."br_event_top_players" AS top_player
        SET reward = 1,
            adena_bonus = 400000000 / v_top_player_count
        WHERE top_player.reward = 0
          AND top_player.event_id = p_event_id
          AND top_player.round_no = p_round_no;
    ELSE
        RETURN QUERY SELECT NULL::text;
    END IF;

    INSERT INTO "world"."br_event_round" (
        event_id,
        round_no,
        top_player_count,
        top_player_score
    ) VALUES (
        p_event_id,
        p_round_no,
        v_top_player_count,
        v_top_player_score
    );
END;
$function$;

COMMIT;
