-- Current Cached.exe primary opcode 26 (RequestLoadCastles) persistence.
--
-- CCastleDb::SendReplyToLoadCastle 0x004a0024 fixes all five callable
-- procedure ABIs and ordered result bindings. The castle, castle-war and
-- agit-deco relational bodies below are identical across three compatible
-- modern donor families. No available database source contains the current
-- lin_AgitDecoNpcLoad body or its physical table name. The paired current
-- L2Server decoder nevertheless fixes that procedure's ordered result as
-- (slot, deco_npc_id, expire_time). Its named portable carrier
-- keeps that exact public procedure ABI without claiming an original
-- deployment-table identifier.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE TABLE "world"."cached_portable_residence_deco_npc" (
    "residence_id" integer NOT NULL,
    "slot" integer NOT NULL,
    "deco_npc_id" integer NOT NULL,
    "expire_time" integer NOT NULL,
    CONSTRAINT "pk_cached_portable_residence_deco_npc"
        PRIMARY KEY ("residence_id", "slot")
);

COMMENT ON TABLE "world"."cached_portable_residence_deco_npc" IS
    'Portable storage behind the current lin_AgitDecoNpcLoad ABI; the original physical table name is unavailable.';

CREATE OR REPLACE FUNCTION "world"."lin_loadcastle"(
    p_id integer,
    p_type integer
) RETURNS TABLE (
    "pledge_id" integer,
    "world_id" integer,
    "next_war_time" integer,
    "tax_rate" integer,
    "tax_child_rate" integer,
    "status" integer,
    "name" text,
    "crop_income" integer,
    "shop_income" bigint,
    "siege_elapsed_time" integer,
    "shop_income_temp" bigint,
    "tax_rate_to_change" integer,
    "tax_child_rate_to_change" integer,
    "prev_castle_owner_id" integer,
    "oldworld_id" integer,
    "castle_type" integer,
    "campaign_success" integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        c."pledge_id"::integer,
        c."world_id"::integer,
        c."next_war_time"::integer,
        c."tax_rate"::integer,
        c."tax_child_rate"::integer,
        c."status"::integer,
        c."name"::text,
        c."crop_income"::integer,
        c."shop_income"::bigint,
        c."siege_elapsed_time"::integer,
        c."shop_income_temp"::bigint,
        c."tax_rate_to_change"::integer,
        c."tax_child_rate_to_change"::integer,
        c."prev_castle_owner_id"::integer,
        c."oldworld_id"::integer,
        COALESCE(c."castle_type", 0)::integer,
        COALESCE(c."campaign_success", -1)::integer
    FROM "world"."castle" AS c
    WHERE c."id" = p_id
      AND c."type" = p_type;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_createcastle"(
    p_id integer,
    p_name text
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."castle" ("id", "name")
    VALUES (p_id, p_name);
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadcastlewarrelatedpledge"(
    p_castle_id integer
) RETURNS TABLE (
    "world_id" integer,
    "pledge_id" integer,
    "type" integer,
    "propose_time" integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        cw."world_id"::integer,
        cw."pledge_id"::integer,
        cw."type"::integer,
        cw."propose_time"::integer
    FROM "world"."castle_war" AS cw
    WHERE cw."castle_id" = p_castle_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadagitdeco"(
    p_agit_id integer
) RETURNS TABLE (
    "type" integer,
    "id" integer,
    "level" integer,
    "expire" integer,
    "is_active" integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        ad."type"::integer,
        ad."id"::integer,
        ad."level"::integer,
        ad."expire"::integer,
        ad."is_active"::integer
    FROM "world"."agit_deco" AS ad
    WHERE ad."agit_id" = p_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_agitdeconpcload"(
    p_residence_id integer
) RETURNS TABLE (
    "slot" integer,
    "deco_npc_id" integer,
    "expire_time" integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        npc."slot",
        npc."deco_npc_id",
        npc."expire_time"
    FROM "world"."cached_portable_residence_deco_npc" AS npc
    WHERE npc."residence_id" = p_residence_id;
$function$;

COMMIT;
