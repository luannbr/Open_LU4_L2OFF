-- Current Cached.exe CWareHouse::UpdateMarketPrice persistence.
--
-- The current machine fixes the three-parameter ABI and bind order.  The
-- rolling-average effect below is shared by the compatible full-arity SQL
-- donor families and targets the already recovered item_market_price table.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_updatemarketprice"(
    p_item_type integer,
    p_enchant integer,
    p_price bigint
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."item_market_price"
    SET
        "avg_price" = (
            ("avg_price" * "frequency") +
            p_price::double precision
        ) / ("frequency" + 1),
        "frequency" = "frequency" + 1
    WHERE "item_type" = p_item_type
      AND "enchant" = p_enchant;

    IF NOT FOUND THEN
        INSERT INTO "world"."item_market_price" (
            "item_type",
            "enchant",
            "avg_price",
            "frequency"
        ) VALUES (
            p_item_type,
            p_enchant,
            p_price::double precision,
            1
        );
    END IF;
END;
$function$;

COMMIT;
