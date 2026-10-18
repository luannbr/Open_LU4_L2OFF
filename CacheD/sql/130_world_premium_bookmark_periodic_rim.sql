-- Extended requests 141-150. Current Cached.exe fixes the packet, callable
-- and result-column ABIs. Classic 3.0 and the compatible 287/Ertheia SQL
-- families supply every relational body except lin_ChangeUserBookmarkSlot.
-- that one update is constrained by the current CacheD ABI and paired
-- L2Server bookmark-job sender.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_saveuserpremiumitem"(
    p_warehouse_no bigint,
    p_buyer_id integer,
    p_buyer_char_id integer,
    p_buyer_char_name text,
    p_recipient_id integer,
    p_recipient_char_id integer,
    p_recipient_char_name text,
    p_item_id integer,
    p_item_amount bigint
) RETURNS TABLE (result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_warehouse_no bigint := p_warehouse_no;
BEGIN
    IF v_warehouse_no = -10001 THEN
        -- The donor allocates a descending negative key from MIN()-1 and
        -- clamps the first value to -10001. Serialize only that allocator so
        -- PostgreSQL cannot hand the same key to two concurrent IB batches.
        LOCK TABLE "world"."user_premium_item"
            IN SHARE ROW EXCLUSIVE MODE;
        SELECT LEAST(
            COALESCE(MIN(premium."warehouse_no") - 1, -10001),
            -10001)
        INTO v_warehouse_no
        FROM "world"."user_premium_item" AS premium;
    END IF;

    INSERT INTO "world"."user_premium_item" (
        "warehouse_no",
        "buyer_id",
        "buyer_char_id",
        "buyer_char_name",
        "recipient_id",
        "recipient_char_id",
        "recipient_char_name",
        "item_id",
        "item_amount",
        "item_remain"
    ) VALUES (
        v_warehouse_no,
        p_buyer_id,
        p_buyer_char_id,
        p_buyer_char_name,
        p_recipient_id,
        p_recipient_char_id,
        p_recipient_char_name,
        p_item_id,
        p_item_amount,
        p_item_amount
    ) ON CONFLICT ("warehouse_no") DO NOTHING;

    -- Current Cached.exe binds only the first donor result column (@@ERROR),
    -- not the donor's echoed warehouse number.
    RETURN QUERY SELECT 0;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loaduserpremiumitem"(
    p_account_id integer
) RETURNS TABLE (
    warehouse_no bigint,
    item_id integer,
    item_remain bigint,
    buyer_id integer,
    buyer_char_name text
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        premium."warehouse_no",
        premium."item_id",
        premium."item_remain",
        premium."buyer_id",
        premium."buyer_char_name"::text
    FROM "world"."user_premium_item" AS premium
    WHERE premium."recipient_id" = p_account_id
      AND premium."item_remain" > 0;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setdeletedinibserver"(
    p_warehouse_no bigint
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_premium_item" AS premium
    SET "ibserver_delete_date" = "world"."mssql_getdate"()
    WHERE premium."warehouse_no" = p_warehouse_no;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletepremiumitem"(
    p_warehouse_no bigint
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."user_premium_item" AS premium
    WHERE premium."warehouse_no" = p_warehouse_no;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_withdrawpremiumitem"(
    p_item_dbid integer,
    p_char_id integer,
    p_warehouse_no bigint,
    p_amount bigint
) RETURNS TABLE (result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_item_remain bigint;
BEGIN
    SELECT premium."item_remain"
    INTO v_item_remain
    FROM "world"."user_premium_item" AS premium
    WHERE premium."warehouse_no" = p_warehouse_no;

    IF FOUND AND v_item_remain < p_amount THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END IF;

    UPDATE "world"."user_premium_item" AS premium
    SET "item_remain" = premium."item_remain" - p_amount
    WHERE premium."warehouse_no" = p_warehouse_no;

    INSERT INTO "world"."user_premium_item_receive" (
        "warehouse_no",
        "real_recipient_char_id",
        "item_dbid",
        "item_amount",
        "receive_date"
    ) VALUES (
        p_warehouse_no,
        p_char_id,
        p_item_dbid,
        p_amount,
        "world"."mssql_getdate"()
    );
    RETURN QUERY SELECT 1;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_withdrawpremiumitematnpc"(
    p_account_id integer,
    p_warehouse_no bigint
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    -- All three signature-compatible SQL donors ignore account_id and mark
    -- the selected IB warehouse entry completely withdrawn.
    UPDATE "world"."user_premium_item" AS premium
       SET "item_remain" = 0
     WHERE premium."warehouse_no" = p_warehouse_no;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_createpremiumitem"(
    p_char_id integer,
    p_item_type integer,
    p_amount bigint,
    p_enchant integer,
    p_eroded integer,
    p_bless integer,
    p_ident integer,
    p_wished integer,
    p_warehouse integer,
    p_variation_opt1 integer,
    p_variation_opt2 integer,
    p_intensive_item_type integer,
    p_hlocked smallint,
    p_use_count smallint,
    p_warehouse_no bigint
) RETURNS TABLE (item_id integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_item_remain bigint;
    v_item_id integer;
BEGIN
    SELECT premium."item_remain"
    INTO v_item_remain
    FROM "world"."user_premium_item" AS premium
    WHERE premium."warehouse_no" = p_warehouse_no
    FOR UPDATE;

    IF FOUND AND v_item_remain < p_amount THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END IF;

    INSERT INTO "world"."user_item" AS created_item (
        "char_id",
        "item_type",
        "amount",
        "enchant",
        "eroded",
        "bless",
        "ident",
        "wished",
        "warehouse",
        "variation_opt1",
        "variation_opt2",
        "intensive_item_type",
        "inventory_slot_index",
        "hlocked",
        "use_count"
    ) VALUES (
        p_char_id,
        p_item_type,
        p_amount,
        p_enchant,
        p_eroded,
        p_bless,
        p_ident,
        p_wished,
        p_warehouse,
        p_variation_opt1,
        p_variation_opt2,
        p_intensive_item_type,
        -1,
        p_hlocked,
        p_use_count
    ) RETURNING created_item."item_id" INTO v_item_id;

    UPDATE "world"."user_premium_item" AS premium
    SET "item_remain" = premium."item_remain" - p_amount
    WHERE premium."warehouse_no" = p_warehouse_no;

    INSERT INTO "world"."user_premium_item_receive" (
        "warehouse_no",
        "real_recipient_char_id",
        "item_dbid",
        "item_amount",
        "receive_date"
    ) VALUES (
        p_warehouse_no,
        p_char_id,
        v_item_id,
        p_amount,
        "world"."mssql_getdate"()
    );
    RETURN QUERY SELECT v_item_id;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loaduserbookmark"(
    p_char_id integer
) RETURNS TABLE (
    slot_id integer,
    pos_x integer,
    pos_y integer,
    pos_z integer,
    icon_id integer,
    slot_title text,
    icon_title text
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        bookmark."slot_id",
        bookmark."pos_x",
        bookmark."pos_y",
        bookmark."pos_z",
        bookmark."icon_id",
        bookmark."slot_title"::text,
        bookmark."icon_title"::text
    FROM "world"."user_bookmark" AS bookmark
    WHERE bookmark."char_id" = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_adduserbookmarkslot"(
    p_char_id integer,
    p_slot_count integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_data" AS user_data
    SET "bookmark_slot" = user_data."bookmark_slot" + p_slot_count
    WHERE user_data."char_id" = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_saveuserbookmarkslot"(
    p_char_id integer,
    p_slot_id integer,
    p_pos_x integer,
    p_pos_y integer,
    p_pos_z integer,
    p_icon_id integer,
    p_slot_title text,
    p_icon_title text
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."user_bookmark" (
        "char_id",
        "slot_id",
        "pos_x",
        "pos_y",
        "pos_z",
        "icon_id",
        "slot_title",
        "icon_title"
    ) VALUES (
        p_char_id,
        p_slot_id,
        p_pos_x,
        p_pos_y,
        p_pos_z,
        p_icon_id,
        p_slot_title,
        p_icon_title
    ) ON CONFLICT ("char_id", "slot_id") DO UPDATE
    SET "pos_x" = EXCLUDED."pos_x",
        "pos_y" = EXCLUDED."pos_y",
        "pos_z" = EXCLUDED."pos_z",
        "icon_id" = EXCLUDED."icon_id",
        "slot_title" = EXCLUDED."slot_title",
        "icon_title" = EXCLUDED."icon_title";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deleteuserbookmarkslot"(
    p_char_id integer,
    p_slot_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."user_bookmark" AS bookmark
    WHERE bookmark."char_id" = p_char_id
      AND bookmark."slot_id" = p_slot_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_changeuserbookmarkslot"(
    p_char_id integer,
    p_old_slot_id integer,
    p_new_slot_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    -- No supplied SQL archive keeps this body. The current callable ABI and
    -- paired sender fix the old/new roles. A primary-key collision therefore
    -- fails just as the matching SQL Server UPDATE would.
    UPDATE "world"."user_bookmark" AS bookmark
    SET "slot_id" = p_new_slot_id
    WHERE bookmark."char_id" = p_char_id
      AND bookmark."slot_id" = p_old_slot_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadperiodicitems"(
    p_char_id integer
) RETURNS TABLE (
    item_id integer,
    start_time integer,
    period integer,
    item_type integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        period_item."item_id",
        period_item."start_time",
        period_item."period",
        period_item."item_type"
    FROM "world"."user_item_period" AS period_item
    JOIN "world"."user_item" AS item
      ON item."item_id" = period_item."item_id"
    WHERE item."char_id" = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_registerperiodicitem"(
    p_item_id integer,
    p_start_time integer,
    p_period integer,
    p_item_type integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."user_item_period" (
        "item_id",
        "start_time",
        "period",
        "item_type"
    ) VALUES (
        p_item_id,
        p_start_time,
        p_period,
        p_item_type
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_removeexpiredperiodicitem"(
    p_item_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."user_item_period" AS period_item
    WHERE period_item."item_id" = p_item_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_additemperiod"(
    p_item_id integer,
    p_period_delta integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_item_period" AS period_item
    SET "period" = period_item."period" + p_period_delta
    WHERE period_item."item_id" = p_item_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_moditemperiod"(
    p_item_id integer,
    p_new_period integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_item_period" AS period_item
    SET "period" = p_new_period
    WHERE period_item."item_id" = p_item_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setrimpoint"(
    p_char_id integer,
    p_point integer,
    p_get_time integer,
    p_reset_time integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_last_time integer;
    v_last_point integer;
BEGIN
    SELECT rim."log_time", rim."point"
    INTO v_last_time, v_last_point
    FROM "world"."user_rim_point" AS rim
    WHERE rim."char_id" = p_char_id
    FOR UPDATE;

    IF FOUND THEN
        UPDATE "world"."user_rim_point" AS rim
        SET "point" = CASE
                WHEN v_last_time + p_reset_time < p_get_time
                    THEN p_point
                ELSE GREATEST(v_last_point, p_point)
            END,
            "log_time" = p_get_time
        WHERE rim."char_id" = p_char_id;
    ELSE
        INSERT INTO "world"."user_rim_point" (
            "char_id",
            "point",
            "log_time"
        ) VALUES (
            p_char_id,
            p_point,
            p_get_time
        );
    END IF;
END;
$function$;

COMMIT;
