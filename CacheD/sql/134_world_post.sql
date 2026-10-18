-- Extended requests 181-190. Current Cached.exe fixes every callable ABI and
-- result-column order below. Relational effects match the compatible Classic
-- 3.0 post procedures over the main world.post relations.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadblocked"(
    p_blocker_id integer,
    p_blockee_id integer
) RETURNS TABLE (blocker_id integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT block_entry."char_id"
    FROM "world"."user_blocklist" AS block_entry
    WHERE block_entry."char_id" = p_blocker_id
      AND block_entry."block_char_id" = p_blockee_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getdailycount"(
    p_char_id integer,
    p_event_id integer
) RETURNS TABLE (daily_count integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_compare_time timestamp without time zone;
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."user_daily_count" AS daily
        WHERE daily."char_id" = p_char_id
          AND daily."event_id" = p_event_id
    ) THEN
        v_compare_time :=
            date_trunc('day', "world"."mssql_getdate"()) +
            interval '6 hours 30 minutes';
        IF "world"."mssql_getdate"() < v_compare_time THEN
            v_compare_time := v_compare_time - interval '1 day';
        END IF;

        UPDATE "world"."user_daily_count" AS daily
        SET "count" = 0,
            "last_update_time" = "world"."mssql_getdate"()
        WHERE daily."char_id" = p_char_id
          AND daily."event_id" = p_event_id
          AND daily."last_update_time" < v_compare_time;

        RETURN QUERY
        SELECT daily."count"
        FROM "world"."user_daily_count" AS daily
        WHERE daily."char_id" = p_char_id
          AND daily."event_id" = p_event_id;
    ELSE
        RETURN QUERY SELECT 0::integer;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_createpost"(
    p_sender_id integer,
    p_receiver_id integer,
    p_trade integer,
    p_title text,
    p_contents text,
    p_process integer,
    p_returnable integer,
    p_sender_deleted integer,
    p_receiver_deleted integer,
    p_sent_time integer,
    p_item_count integer,
    p_trade_money bigint,
    p_post_fee bigint,
    p_sender_type integer,
    p_recorded_id integer
) RETURNS TABLE (post_id integer)
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."post" AS created (
        "sender_id",
        "receiver_id",
        "trade",
        "title",
        "contents",
        "process",
        "returnable",
        "returned",
        "sender_deleted",
        "receiver_deleted",
        "sent_time",
        "item_count",
        "trade_money",
        "post_fee",
        "sender_type",
        "recorded_id"
    ) VALUES (
        p_sender_id,
        p_receiver_id,
        p_trade,
        p_title,
        p_contents,
        p_process,
        p_returnable,
        0,
        p_sender_deleted,
        p_receiver_deleted,
        p_sent_time,
        p_item_count,
        p_trade_money,
        p_post_fee,
        p_sender_type,
        p_recorded_id
    )
    RETURNING created."post_id";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_createpostitemforadmin"(
    p_item_id integer,
    p_post_id integer,
    p_postkeeper_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."post_item_admin" (
        "item_id",
        "post_id",
        "postkeeper_id"
    ) VALUES (
        p_item_id,
        p_post_id,
        p_postkeeper_id
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getreceivedpostcount"(
    p_char_id integer
) RETURNS TABLE (received_count integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT COUNT(*)::integer
    FROM "world"."post" AS post
    WHERE post."receiver_id" = p_char_id
      AND post."receiver_deleted" = 0;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadpostbypostid"(
    p_post_id integer
) RETURNS TABLE (
    sender_id integer,
    receiver_id integer,
    trade integer,
    title text,
    contents text,
    process integer,
    returnable integer,
    returned integer,
    sender_deleted integer,
    receiver_deleted integer,
    sent_time integer,
    item_count integer,
    trade_money bigint,
    post_fee bigint,
    sender_type integer,
    recorded_id integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        post."sender_id",
        post."receiver_id",
        post."trade"::integer,
        post."title"::text,
        post."contents"::text,
        post."process"::integer,
        post."returnable"::integer,
        post."returned"::integer,
        post."sender_deleted"::integer,
        post."receiver_deleted"::integer,
        post."sent_time",
        post."item_count"::integer,
        COALESCE(post."trade_money", 0),
        COALESCE(post."post_fee", 0),
        COALESCE(post."sender_type", 0)::integer,
        COALESCE(post."recorded_id", 0)
    FROM "world"."post" AS post
    WHERE post."post_id" = p_post_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadpostbysenderid"(
    p_sender_id integer
) RETURNS TABLE (
    post_id integer,
    receiver_id integer,
    trade integer,
    title text,
    process integer,
    returnable integer,
    returned integer,
    sender_deleted integer,
    receiver_deleted integer,
    sent_time integer,
    item_count integer,
    sender_type integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        post."post_id",
        post."receiver_id",
        post."trade"::integer,
        post."title"::text,
        post."process"::integer,
        post."returnable"::integer,
        post."returned"::integer,
        post."sender_deleted"::integer,
        post."receiver_deleted"::integer,
        post."sent_time",
        post."item_count"::integer,
        COALESCE(post."sender_type", 0)::integer
    FROM "world"."post" AS post
    WHERE post."sender_id" = p_sender_id
      AND (post."sender_type" = 0 OR post."sender_type" = 7)
    ORDER BY post."post_id" DESC;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadpostbyreceiverid"(
    p_receiver_id integer
) RETURNS TABLE (
    post_id integer,
    sender_id integer,
    trade integer,
    title text,
    process integer,
    returnable integer,
    returned integer,
    sender_deleted integer,
    receiver_deleted integer,
    sent_time integer,
    item_count integer,
    sender_type integer,
    recorded_id integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        post."post_id",
        post."sender_id",
        post."trade"::integer,
        post."title"::text,
        post."process"::integer,
        post."returnable"::integer,
        post."returned"::integer,
        post."sender_deleted"::integer,
        post."receiver_deleted"::integer,
        post."sent_time",
        post."item_count"::integer,
        COALESCE(post."sender_type", 0)::integer,
        COALESCE(post."recorded_id", 0)
    FROM "world"."post" AS post
    WHERE post."receiver_id" = p_receiver_id
    ORDER BY post."post_id" DESC;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadpostcontent"(
    p_post_id integer
) RETURNS TABLE (contents text)
LANGUAGE sql
STABLE
AS $function$
    SELECT post."contents"::text
    FROM "world"."post" AS post
    WHERE post."post_id" = p_post_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadpostdeleted"(
    p_post_id integer
) RETURNS TABLE (
    sender_deleted integer,
    receiver_deleted integer,
    item_count integer,
    sender_type integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        post."sender_deleted"::integer,
        post."receiver_deleted"::integer,
        post."item_count"::integer,
        COALESCE(post."sender_type", 0)::integer
    FROM "world"."post" AS post
    WHERE post."post_id" = p_post_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadpostitem"(
    p_post_id integer
) RETURNS TABLE (
    item_id integer,
    amount bigint
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        item."item_id",
        item."amount"
    FROM "world"."user_item" AS item
    INNER JOIN "world"."post_item_admin" AS post_item
        ON post_item."item_id" = item."item_id"
    WHERE post_item."post_id" = p_post_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadpostcommissionitemdata"(
    p_post_id integer
) RETURNS TABLE (
    item_classid integer,
    enchanted integer,
    attrt_fire integer,
    attrt_water integer,
    attrt_wind integer,
    attrt_earth integer,
    attrt_holy integer,
    attrt_unholy integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        data."item_classid",
        data."enchanted",
        data."attrt_fire",
        data."attrt_water",
        data."attrt_wind",
        data."attrt_earth",
        data."attrt_holy",
        data."attrt_unholy"
    FROM "world"."post_commission_item_data" AS data
    WHERE data."post_id" = p_post_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getloadpostprocess"(
    p_post_id integer
) RETURNS TABLE (process integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT post."process"::integer
    FROM "world"."post" AS post
    WHERE post."post_id" = p_post_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_rejectpost"(
    p_post_id integer,
    p_sender_id integer,
    p_receiver_id integer,
    p_trade integer,
    p_title text,
    p_process integer,
    p_returnable integer,
    p_sent_time integer,
    p_recorded_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."post" AS post
    SET
        "sender_id" = p_sender_id,
        "receiver_id" = p_receiver_id,
        "trade" = p_trade,
        "title" = p_title,
        "process" = p_process,
        "returnable" = p_returnable,
        "returned" = 1,
        "sender_deleted" = 1,
        "sent_time" = p_sent_time,
        "recorded_id" = p_recorded_id
    WHERE post."post_id" = p_post_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatepostprocess"(
    p_post_id integer,
    p_process integer,
    p_delete_item integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."post" AS post
    SET "process" = p_process
    WHERE post."post_id" = p_post_id;

    IF p_delete_item = 1 THEN
        UPDATE "world"."post" AS post
        SET "item_count" = 0
        WHERE post."post_id" = p_post_id;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatepostdeleted"(
    p_post_id integer,
    p_sender_deleted integer,
    p_receiver_deleted integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."post" AS post
    SET
        "sender_deleted" = p_sender_deleted,
        "receiver_deleted" = p_receiver_deleted
    WHERE post."post_id" = p_post_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletepost"(
    p_post_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    DELETE FROM "world"."post"
    WHERE "post_id" = p_post_id;

    DELETE FROM "world"."post_item_admin"
    WHERE "post_id" = p_post_id;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletepostitemforadmin"(
    p_post_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."post_item_admin"
    WHERE "post_id" = p_post_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletepostcommissionitemdata"(
    p_post_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."post_commission_item_data"
    WHERE "post_id" = p_post_id;
$function$;

COMMIT;
