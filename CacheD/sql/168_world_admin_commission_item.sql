-- Current Cached.exe AQ opcodes 162 and 163.
--
-- The current image fixes both callable ABIs. Classic 3.0, modified 287 and
-- Ertheia contain identical bodies: lin_GetSeizeCommissionData returns the
-- four bound commission columns in this order, while lin_SeizeItem2 accepts
-- option for ABI compatibility but updates only the requested warehouse.

BEGIN;

CREATE OR REPLACE FUNCTION "world"."lin_getseizecommissiondata"(
    p_commission_id bigint
) RETURNS TABLE (
    item_commission_expired_type integer,
    item_commission_first_type integer,
    char_id integer,
    item_id integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        commission."item_commission_expired_type",
        commission."item_commission_first_type",
        commission."char_id",
        commission."item_id"
    FROM "world"."commission" AS commission
    WHERE commission."commission_id" = p_commission_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_seizeitem2"(
    p_option integer,
    p_warehouse integer,
    p_item_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    -- The shipping SQL Server body also accepts but does not read option.
    PERFORM p_option;
    UPDATE "world"."user_item" AS item
    SET "warehouse" = p_warehouse
    WHERE item."item_id" = p_item_id;
END;
$function$;

COMMIT;
