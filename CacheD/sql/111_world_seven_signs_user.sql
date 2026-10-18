-- Current Cached.exe Shared/DXPacket.cpp Seven Signs user persistence.
--
-- CUser::SaveSSQInfo 0x005b1fa0 and its current helper 0x005a67c0 fix the
-- 14-input order and physical bind types. Classic 3.0, modified 287 and
-- Ertheia keep the same update-or-insert relational body.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

-- The third current ODBC binding is SQL_C_SLONG/SQL_DOUBLE even though the
-- stored relation column and every compatible procedure donor use int32.
CREATE OR REPLACE FUNCTION "world"."lin_savessquserinfo"(
    p_char_id integer,
    p_round_number integer,
    p_ssq_join_time double precision,
    p_ssq_part integer,
    p_ssq_position integer,
    p_seal_selection_number integer,
    p_ssq_point bigint,
    p_twilight_a bigint,
    p_twilight_b bigint,
    p_twilight_c bigint,
    p_dawn_a bigint,
    p_dawn_b bigint,
    p_dawn_c bigint,
    p_ticket_buy_count integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."ssq_user_data" AS ssq
    SET "round_number" = p_round_number,
        "ssq_join_time" = p_ssq_join_time::integer,
        "ssq_part" = p_ssq_part::smallint,
        "ssq_position" = p_ssq_position::smallint,
        "seal_selection_no" = p_seal_selection_number::smallint,
        "ssq_point" = p_ssq_point,
        "twilight_a_item_num" = p_twilight_a,
        "twilight_b_item_num" = p_twilight_b,
        "twilight_c_item_num" = p_twilight_c,
        "dawn_a_item_num" = p_dawn_a,
        "dawn_b_item_num" = p_dawn_b,
        "dawn_c_item_num" = p_dawn_c,
        "ticket_buy_count" = p_ticket_buy_count::smallint
    WHERE ssq."char_id" = p_char_id;

    IF NOT FOUND THEN
        INSERT INTO "world"."ssq_user_data" (
            "char_id",
            "round_number",
            "ssq_join_time",
            "ssq_part",
            "ssq_position",
            "seal_selection_no",
            "ssq_point",
            "twilight_a_item_num",
            "twilight_b_item_num",
            "twilight_c_item_num",
            "dawn_a_item_num",
            "dawn_b_item_num",
            "dawn_c_item_num",
            "ticket_buy_count"
        ) VALUES (
            p_char_id,
            p_round_number,
            p_ssq_join_time::integer,
            p_ssq_part::smallint,
            p_ssq_position::smallint,
            p_seal_selection_number::smallint,
            p_ssq_point,
            p_twilight_a,
            p_twilight_b,
            p_twilight_c,
            p_dawn_a,
            p_dawn_b,
            p_dawn_c,
            p_ticket_buy_count::smallint
        );
    END IF;
END;
$function$;

COMMIT;
