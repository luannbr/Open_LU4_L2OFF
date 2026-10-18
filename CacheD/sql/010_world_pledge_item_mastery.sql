-- Current Cached.exe persistence ABI for PledgeItemInCache and
-- PledgeMasteryInCache.
--
-- All five callable shapes, bind/result order and update-after-success
-- behavior come from current Cached.exe 0x0051fed8..0x00520778.
-- pledge_mastery's relation and key are pinned by the modified protocol-287
-- database export.  The pledge_item relation is a PostgreSQL carrier for the
-- current ABI: no matching relation/procedure body survived in the
-- available donor exports, while current L2Server and its packet type prove
-- that the second value is an absolute expiry time instead of an amount.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE TABLE "world"."pledge_mastery" (
    "pledge_id" integer NOT NULL,
    "mastery_id" integer NOT NULL,
    "point" integer NOT NULL,
    CONSTRAINT "pk_pledge_mastery" PRIMARY KEY ("pledge_id", "mastery_id"),
    CONSTRAINT "ck_pledge_mastery_id" CHECK ("mastery_id" BETWEEN 1 AND 16)
);

CREATE TABLE "world"."pledge_item" (
    "pledge_id" integer NOT NULL,
    "item_class_id" integer NOT NULL,
    "expiry_time" integer NOT NULL,
    CONSTRAINT "pk_pledge_item" PRIMARY KEY ("pledge_id", "item_class_id")
);

CREATE OR REPLACE FUNCTION "world"."lin_pledgemasteryload"(
    p_pledge_id integer
) RETURNS TABLE (
    pledge_id integer,
    mastery_id integer,
    point integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT mastery.pledge_id, mastery.mastery_id, mastery.point
    FROM "world"."pledge_mastery" AS mastery
    WHERE mastery.pledge_id = p_pledge_id
    ORDER BY mastery.mastery_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_pledgemasterysave"(
    p_pledge_id integer,
    p_mastery_id integer,
    p_point integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."pledge_mastery" (
        pledge_id, mastery_id, point
    ) VALUES (
        p_pledge_id, p_mastery_id, p_point
    )
    ON CONFLICT (pledge_id, mastery_id) DO UPDATE
    SET point = EXCLUDED.point;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_pledgemasteryreset"(
    p_pledge_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."pledge_mastery" AS mastery
    WHERE mastery.pledge_id = p_pledge_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_pledgeitemload"(
    p_pledge_id integer
) RETURNS TABLE (
    item_class_id integer,
    expiry_time integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT item.item_class_id, item.expiry_time
    FROM "world"."pledge_item" AS item
    WHERE item.pledge_id = p_pledge_id
    ORDER BY item.item_class_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_pledgeitemsave"(
    p_pledge_id integer,
    p_item_class_id integer,
    p_expiry_time integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."pledge_item" (
        pledge_id, item_class_id, expiry_time
    ) VALUES (
        p_pledge_id, p_item_class_id, p_expiry_time
    )
    ON CONFLICT (pledge_id, item_class_id) DO UPDATE
    SET expiry_time = EXCLUDED.expiry_time;
$function$;

COMMIT;
