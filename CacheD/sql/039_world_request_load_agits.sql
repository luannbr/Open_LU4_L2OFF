-- Current Cached.exe primary opcode 27 (CSocket::RequestLoadAgits)
-- persistence.
--
-- The current function at 0x0048c0bc fixes the input types and all 24 result
-- bindings.  Its projection is byte-for-byte compatible in the Classic 3.0,
-- Ertheia and 287 donor families. The paired current L2Server decoder at
-- 0x006a7458 fixes the same order on the wire.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadagit"(
    p_agit_id integer
) RETURNS TABLE (
    "pledge_id" integer,
    "hp_stove" integer,
    "hp_stove_expire" integer,
    "mp_flame" integer,
    "mp_flame_expire" integer,
    "hatcher" integer,
    "status" integer,
    "next_war_time" integer,
    "auction_id" integer,
    "auction_time" integer,
    "last_price" bigint,
    "last_cancel" integer,
    "min_price" bigint,
    "teleport_level" integer,
    "teleport_expire" integer,
    "auction_desc" text,
    "next_cost" integer,
    "cost_fail" integer,
    "tax_rate" integer,
    "tax_rate_to_change" integer,
    "tax_child_rate" integer,
    "tax_child_rate_to_change" integer,
    "shop_income" bigint,
    "shop_income_temp" bigint
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        a."pledge_id"::integer,
        a."hp_stove"::integer,
        a."hp_stove_expire"::integer,
        a."mp_flame"::integer,
        a."mp_flame_expire"::integer,
        a."hatcher"::integer,
        a."status"::integer,
        a."next_war_time"::integer,
        COALESCE(a."auction_id", 0)::integer,
        COALESCE(aa."auction_time", 0)::integer,
        COALESCE(a."last_price", 0)::bigint,
        COALESCE(a."last_cancel", 0)::integer,
        COALESCE(aa."min_price", 0)::bigint,
        COALESCE(a."teleport_level", 0)::integer,
        COALESCE(a."teleport_expire", 0)::integer,
        COALESCE(aa."auction_desc", '')::text,
        COALESCE(a."next_cost", 0)::integer,
        COALESCE(a."cost_fail", 0)::integer,
        COALESCE(a."tax_rate", 0)::integer,
        COALESCE(a."tax_rate_to_change", 0)::integer,
        COALESCE(a."tax_child_rate", 0)::integer,
        COALESCE(a."tax_child_rate_to_change", 0)::integer,
        COALESCE(a."shop_income", 0)::bigint,
        COALESCE(a."shop_income_temp", 0)::bigint
    FROM "world"."agit" AS a
    LEFT JOIN "world"."agit_auction" AS aa
      ON a."id" = aa."agit_id"
     AND a."auction_id" = aa."auction_id"
    WHERE a."id" = p_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_createagitifnotexist"(
    p_agit_id integer,
    p_agit_name text
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."agit" ("id", "name")
    VALUES (p_agit_id, p_agit_name);
$function$;

COMMIT;
