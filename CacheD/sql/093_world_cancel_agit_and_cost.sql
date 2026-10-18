-- Current Cached.exe primary opcodes 161 and 162.
--
-- The current machine fixes both callable ABIs. All substantive compatible
-- donor families agree on the complete relational effects.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_cancelagitauction"(
    p_agit_id integer,
    p_last_cancel integer
) RETURNS TABLE (
    auction_id integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_auction_id integer := 0;
BEGIN
    SELECT COALESCE((
        SELECT agit."auction_id"
        FROM "world"."agit" AS agit
        WHERE agit."id" = p_agit_id
    ), 0)
    INTO v_auction_id;

    UPDATE "world"."agit" AS agit
    SET
        "auction_id" = 0,
        "last_cancel" = p_last_cancel
    WHERE agit."id" = p_agit_id;

    RETURN QUERY SELECT v_auction_id;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setagitnextcost"(
    p_agit_id integer,
    p_next_cost integer,
    p_cost_fail integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."agit" AS agit
    SET
        "next_cost" = p_next_cost,
        "cost_fail" = p_cost_fail
    WHERE agit."id" = p_agit_id;
$function$;

COMMIT;
