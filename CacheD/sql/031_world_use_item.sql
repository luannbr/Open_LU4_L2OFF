-- Current Cached.exe primary opcode 19 (UseItemPacket) database slice.
--
-- CWareHouse::UseItem and CItem::ChangeAmount fix the four input types and
-- single bigint result of lin_AmountChange. Classic 3.0, protocol-287 and
-- Ertheia keep the same modern BIGINT relational body. This
-- PostgreSQL function keeps that donor-consensus body under the current
-- machine-proved ABI. It is not inferred from the older C1 implementation.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_amountchange"(
    p_char_id integer,
    p_item_id integer,
    p_change bigint,
    p_zero_delete integer
) RETURNS TABLE (amount bigint)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_current_amount bigint;
    v_result_amount bigint := -1;
    v_rows integer := 0;
BEGIN
    SELECT item.amount
    INTO v_current_amount
    FROM "world"."user_item" AS item
    WHERE item.char_id = p_char_id
      AND item.item_id = p_item_id
    LIMIT 1;

    IF NOT FOUND OR v_current_amount + p_change < 0 THEN
        RETURN QUERY SELECT -1::bigint;
        RETURN;
    END IF;

    UPDATE "world"."user_item" AS item
    SET amount = item.amount + p_change
    WHERE item.char_id = p_char_id
      AND item.item_id = p_item_id
    RETURNING item.amount INTO v_result_amount;
    GET DIAGNOSTICS v_rows = ROW_COUNT;

    IF v_rows <> 1 THEN
        RETURN QUERY SELECT -1::bigint;
        RETURN;
    END IF;

    IF v_result_amount = 0 AND p_zero_delete = 1 THEN
        DELETE FROM "world"."user_item" AS item
        WHERE item.char_id = p_char_id
          AND item.item_id = p_item_id;
    END IF;

    RETURN QUERY SELECT v_result_amount;
END;
$function$;

COMMIT;
