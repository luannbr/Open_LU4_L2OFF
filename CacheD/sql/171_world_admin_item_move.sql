-- Current Cached.exe AQ opcodes 54, 117, 118, 141-143 and 152-154.
--
-- The callable contracts are fixed by the current image. Classic 3.0 keeps
-- the matching SQL Server bodies: lin_GetItemType returns the five item
-- columns bound by CWareHouse::SeizeItem, and lin_SeizeItem applies the
-- commission warehouse for option 1 or the supplied warehouse for option 2.

BEGIN;

CREATE OR REPLACE FUNCTION "world"."lin_getitemtype"(
    p_item_id integer
) RETURNS TABLE (
    item_id integer,
    char_id integer,
    item_type integer,
    amount bigint,
    warehouse integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        item."item_id",
        item."char_id",
        item."item_type",
        item."amount",
        item."warehouse"
    FROM "world"."user_item" AS item
    WHERE item."item_id" = p_item_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_seizeitem"(
    p_option integer,
    p_warehouse integer,
    p_item_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF p_option = 1 THEN
        UPDATE "world"."user_item" AS item
        SET "warehouse" = 1001
        WHERE item."item_id" = p_item_id;
    ELSIF p_option = 2 THEN
        UPDATE "world"."user_item" AS item
        SET "warehouse" = p_warehouse
        WHERE item."item_id" = p_item_id;
    END IF;
END;
$function$;

COMMIT;
