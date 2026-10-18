-- Current Cached.exe primary opcode 30 (RequestManipulateItem) special-item
-- persistence. The machine code fixes both callable ABIs, result bindings
-- and Fetch calls. All six compatible donor families keep identical
-- relational bodies and effect sets for both procedures.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_addlottoitem"(
    p_round integer,
    p_item_id integer,
    p_number_flag integer
) RETURNS TABLE (total_count integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_total_count integer;
BEGIN
    INSERT INTO "world"."lotto_items" (
        round_number,
        item_id,
        number_flag
    ) VALUES (
        p_round,
        p_item_id,
        p_number_flag
    );

    SELECT count(*)::integer
    INTO v_total_count
    FROM "world"."lotto_items" AS lotto_item
    WHERE lotto_item.round_number = p_round;

    UPDATE "world"."lotto_game" AS lotto_game
    SET total_count = v_total_count
    WHERE lotto_game.round_number = p_round;

    RETURN QUERY SELECT v_total_count;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_createmonraceticket"(
    p_monrace_id integer,
    p_bet_type integer,
    p_bet_1 integer,
    p_bet_2 integer,
    p_bet_3 integer,
    p_bet_money integer,
    p_tax_money integer,
    p_item_id integer,
    p_remote_fee integer
) RETURNS TABLE (ticket_id integer)
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."monrace_ticket" (
        monraceid,
        bet_type,
        bet_1,
        bet_2,
        bet_3,
        bet_money,
        tax_money,
        item_id,
        remotefee
    ) VALUES (
        p_monrace_id,
        p_bet_type::smallint,
        p_bet_1::smallint,
        p_bet_2::smallint,
        p_bet_3::smallint,
        p_bet_money,
        p_tax_money,
        p_item_id,
        p_remote_fee
    )
    RETURNING monrace_ticket.ticket_id;
$function$;

COMMIT;
