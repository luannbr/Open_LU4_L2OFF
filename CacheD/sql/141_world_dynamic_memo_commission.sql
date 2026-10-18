-- Current Cached.exe RQ_EX protocol 251 and 254..260 database slice.
-- The three classic-family donors agree on each admitted body.  Current
-- machine-code bindings remain authoritative where the ABI changed: current
-- lin_RegisterCommission has 15 inputs (discount_type is stored as zero),
-- lin_GetBuyItemCommission returns 8 columns, and the two epoch inputs keep
-- the target SQL_DOUBLE binding.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadalldynamicquest"()
RETURNS TABLE (
    dynamic_quest_id integer,
    last_executed_time integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        dynamic_quest."dynamic_quest_id",
        dynamic_quest."last_executed_time"
    FROM "world"."dynamic_quest" AS dynamic_quest;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savedynamicquest"(
    p_dynamic_quest_id integer,
    p_last_executed_time integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."dynamic_quest" (
        "dynamic_quest_id",
        "last_executed_time"
    ) VALUES (
        p_dynamic_quest_id,
        p_last_executed_time
    )
    ON CONFLICT ("dynamic_quest_id") DO UPDATE
    SET "last_executed_time" = EXCLUDED."last_executed_time";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updateblockmemo"(
    p_char_id integer,
    p_block_char_id integer,
    p_block_memo text
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_blocklist"
    SET "block_memo" = p_block_memo
    WHERE "char_id" = p_char_id
      AND "block_char_id" = p_block_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatefriendmemo"(
    p_char_id integer,
    p_friend_char_id integer,
    p_friend_memo text
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_friend"
    SET "friend_memo" = p_friend_memo
    WHERE "char_id" = p_char_id
      AND "friend_char_id" = p_friend_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadinfocommission"()
RETURNS TABLE (total_count bigint)
LANGUAGE sql
STABLE
AS $function$
    SELECT COUNT(*)
    FROM "world"."commission" AS commission
    WHERE commission."buyer_id" = 0
      AND commission."item_delete_time" IS NULL
      AND commission."char_id" IN (
          SELECT user_data."char_id"
          FROM "world"."user_data" AS user_data
          WHERE user_data."account_id" > 0
      );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadallcommission"()
RETURNS TABLE (
    commission_id bigint,
    char_id integer,
    buyer_id integer,
    item_name text,
    item_price bigint,
    item_commission_type integer,
    item_commission_first_type integer,
    commission_fee bigint,
    item_commission_expired_type integer,
    item_expired_time integer,
    char_name text,
    item_id integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        commission."commission_id",
        commission."char_id",
        commission."buyer_id",
        commission."item_name"::text,
        commission."item_price",
        commission."item_commission_type",
        commission."item_commission_first_type",
        commission."commission_fee",
        commission."item_commission_expired_type",
        commission."item_expired_time",
        commission."char_name"::text,
        commission."item_id"
    FROM "world"."commission" AS commission
    WHERE commission."buyer_id" = 0
      AND commission."item_delete_time" IS NULL
      AND commission."char_id" IN (
          SELECT user_data."char_id"
          FROM "world"."user_data" AS user_data
          WHERE user_data."account_id" > 0
      );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_registercommission"(
    p_char_id integer,
    p_buyer_id integer,
    p_item_name text,
    p_item_price bigint,
    p_item_commission_type integer,
    p_item_commission_first_type integer,
    p_commission_fee bigint,
    p_item_commission_expired_type integer,
    p_item_expired_time double precision,
    p_char_name text,
    p_item_id integer,
    p_item_grade integer,
    p_item_general integer,
    p_item_register_time text,
    p_item_classid integer
) RETURNS TABLE (commission_id bigint)
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."commission" AS created (
        "char_id",
        "buyer_id",
        "item_name",
        "item_price",
        "item_commission_type",
        "item_commission_first_type",
        "commission_fee",
        "item_commission_expired_type",
        "item_expired_time",
        "char_name",
        "item_id",
        "item_grade",
        "item_general",
        "item_register_time",
        "item_classid",
        "discount_type"
    ) VALUES (
        p_char_id,
        p_buyer_id,
        p_item_name,
        p_item_price,
        p_item_commission_type,
        p_item_commission_first_type,
        p_commission_fee,
        p_item_commission_expired_type,
        p_item_expired_time::integer,
        p_char_name,
        p_item_id,
        p_item_grade,
        p_item_general,
        p_item_register_time::timestamp without time zone,
        p_item_classid,
        0
    )
    RETURNING created."commission_id";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getcommissiondata"(
    p_commission_id bigint,
    p_char_id integer
) RETURNS TABLE (
    item_commission_type integer,
    item_commission_expired_type integer,
    item_name text,
    item_commission_first_type integer,
    item_classid integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        commission."item_commission_type",
        commission."item_commission_expired_type",
        commission."item_name"::text,
        commission."item_commission_first_type",
        commission."item_classid"
    FROM "world"."commission" AS commission
    WHERE commission."commission_id" = p_commission_id
      AND commission."char_id" = p_char_id
      AND commission."buyer_id" = 0;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletecommission"(
    p_commission_id bigint,
    p_buyer_id integer,
    p_item_delete_time text
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."commission"
    SET
        "buyer_id" = p_buyer_id,
        "item_delete_time" =
            p_item_delete_time::timestamp without time zone
    WHERE "commission_id" = p_commission_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getbuyitemcommission"(
    p_commission_id bigint
) RETURNS TABLE (
    item_price bigint,
    char_id integer,
    item_id integer,
    item_commission_type integer,
    item_commission_expired_type integer,
    item_name text,
    item_commission_first_type integer,
    item_classid integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        commission."item_price",
        commission."char_id",
        commission."item_id",
        commission."item_commission_type",
        commission."item_commission_expired_type",
        commission."item_name"::text,
        commission."item_commission_first_type",
        commission."item_classid"
    FROM "world"."commission" AS commission
    WHERE commission."commission_id" = p_commission_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_registerpostcommissionitemdata"(
    p_post_id integer,
    p_item_classid integer,
    p_enchanted integer,
    p_attrt_fire integer,
    p_attrt_water integer,
    p_attrt_wind integer,
    p_attrt_earth integer,
    p_attrt_holy integer,
    p_attrt_unholy integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."post_commission_item_data" (
        "post_id",
        "item_classid",
        "enchanted",
        "attrt_fire",
        "attrt_water",
        "attrt_wind",
        "attrt_earth",
        "attrt_holy",
        "attrt_unholy"
    ) VALUES (
        p_post_id,
        p_item_classid,
        p_enchanted,
        p_attrt_fire,
        p_attrt_water,
        p_attrt_wind,
        p_attrt_earth,
        p_attrt_holy,
        p_attrt_unholy
    );
$function$;

-- Overload selected by current 0x0054cdcc's SQL_C_SLONG/SQL_DOUBLE epoch.
-- Migration 134 keeps the integer overload for already recovered callers
-- that still use the ordinary int helper.
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
    p_sent_time double precision,
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
        p_sent_time::integer,
        p_item_count,
        p_trade_money,
        p_post_fee,
        p_sender_type,
        p_recorded_id
    )
    RETURNING created."post_id";
$function$;

COMMIT;
