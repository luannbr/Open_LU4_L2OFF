-- Current Cached.exe primary opcode 149 (RequestCreateAgitAuction).
--
-- The current image fixes both five-input call ABIs and the one-integer
-- result of lin_CreateAgitAuction. Classic 3.0, modified 287 and Ertheia
-- keep the same full-arity relational bodies: create the auction, publish
-- its generated identifier on agit, then append the associated adena log.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_createagitauction"(
    p_agit_id integer,
    p_auction_desc text,
    p_min_price bigint,
    p_auction_time integer,
    p_auction_tax integer
) RETURNS TABLE (auction_id integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_auction_id integer;
BEGIN
    INSERT INTO "world"."agit_auction" (
        "agit_id",
        "auction_desc",
        "min_price",
        "auction_time",
        "auction_tax"
    ) VALUES (
        p_agit_id,
        p_auction_desc,
        p_min_price,
        p_auction_time,
        p_auction_tax
    )
    RETURNING "agit_auction"."auction_id"
    INTO v_auction_id;

    UPDATE "world"."agit"
    SET "auction_id" = v_auction_id
    WHERE "id" = p_agit_id;

    RETURN QUERY SELECT v_auction_id;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_insertagitadena"(
    p_agit_id integer,
    p_pledge_id integer,
    p_auction_id integer,
    p_reason integer,
    p_adena bigint
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."agit_adena" (
        "agit_id",
        "pledge_id",
        "auction_id",
        "reason",
        "adena"
    ) VALUES (
        p_agit_id,
        p_pledge_id,
        p_auction_id,
        p_reason,
        p_adena
    );
$function$;

COMMIT;
