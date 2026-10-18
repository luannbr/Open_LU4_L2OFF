-- Close three current CacheD callable-name gaps without conflating evidence
-- levels.
--
-- lin_InsertItemName:
--   * current Cached.exe fixes the five-input ABI and parsed values.
--   * the C1/Interlude procedure family fixes the direct row effect and the
--     id/name/desc/consumeType/IsQuest meaning.
--   * current ItemData is a different five-flag runtime relation, so the
--     physical carrier below is named cached_portable_*.
--
-- lin_PCCafeAccountCoupon*:
--   * current CacheD fixes route -3, six ordered inputs and one result.
--   * paired current L2Server fixes result 0 as an already-used serial,
--     result 2 as a user that already redeemed a coupon, and result 1 as the
--     only path that awards an item and records consumption.
--   * no supplied database contains the original shared relation or T-SQL.
--     These two bodies and their physical carrier are therefore a semantic
--     portable reconstruction, not an exact-source claim.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE TABLE IF NOT EXISTS "world"."cached_portable_item_name" (
    "id" integer NOT NULL,
    "name" varchar(50)
        COLLATE "world"."Korean_Wansung_CI_AS" NOT NULL,
    "description" varchar(50)
        COLLATE "world"."Korean_Wansung_CI_AS" NOT NULL,
    "consume_type" varchar(50)
        COLLATE "world"."Korean_Wansung_CI_AS",
    "is_quest" smallint DEFAULT 0 NOT NULL,
    CONSTRAINT "ck_cached_portable_item_name_is_quest"
        CHECK ("is_quest" BETWEEN 0 AND 255),
    CONSTRAINT "pk_cached_portable_item_name"
        PRIMARY KEY ("id")
);

COMMENT ON TABLE "world"."cached_portable_item_name" IS
    'Portable carrier for the legacy itemcache name import. Its row semantics '
    'come from the compatible C1/Interlude procedure; its physical name is not '
    'claimed to survive from the current database.';

-- Replace migration 195's isolated flush with the coherent legacy pair.
-- The current five-flag world.itemdata relation remains owned by
-- lin_InitItemData/lin_InsertItemData and is no longer erased by this stale
-- name-import command.
CREATE OR REPLACE FUNCTION "world"."lin_flushitemname"()
RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."cached_portable_item_name";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_insertitemname"(
    p_id integer,
    p_name text,
    p_description text,
    p_consume_type text,
    p_is_quest integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."cached_portable_item_name" (
        "id", "name", "description", "consume_type", "is_quest"
    ) VALUES (
        p_id,
        p_name,
        p_description,
        p_consume_type,
        p_is_quest::smallint
    );
$function$;

CREATE SCHEMA IF NOT EXISTS "shared";

CREATE TABLE IF NOT EXISTS "shared"."cached_portable_pccafe_account_coupon_use" (
    "coupon1" integer NOT NULL,
    "coupon2" integer NOT NULL,
    "coupon3" integer NOT NULL,
    "coupon4" integer NOT NULL,
    "user_db_id" integer NOT NULL,
    "world_id" integer NOT NULL,
    CONSTRAINT "pk_cached_portable_pccafe_account_coupon_use"
        PRIMARY KEY ("coupon1", "coupon2", "coupon3", "coupon4"),
    CONSTRAINT "uq_cached_portable_pccafe_account_coupon_use_user"
        UNIQUE ("world_id", "user_db_id")
);

COMMENT ON TABLE "shared"."cached_portable_pccafe_account_coupon_use" IS
    'Derived portable PCCafe coupon redemption ledger. Current executables '
    'fix its observable uniqueness semantics, but not the original table name '
    'or physical layout.';

CREATE OR REPLACE FUNCTION "shared"."lin_pccafeaccountcouponcheckstate"(
    p_coupon1 integer,
    p_coupon2 integer,
    p_coupon3 integer,
    p_coupon4 integer,
    p_user_db_id integer,
    p_world_id integer
) RETURNS TABLE (result integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT CASE
        WHEN EXISTS (
            SELECT 1
              FROM "shared"."cached_portable_pccafe_account_coupon_use" AS used
             WHERE used."coupon1" = p_coupon1
               AND used."coupon2" = p_coupon2
               AND used."coupon3" = p_coupon3
               AND used."coupon4" = p_coupon4
        ) THEN 0
        WHEN EXISTS (
            SELECT 1
              FROM "shared"."cached_portable_pccafe_account_coupon_use" AS used
             WHERE used."world_id" = p_world_id
               AND used."user_db_id" = p_user_db_id
        ) THEN 2
        ELSE 1
    END::integer;
$function$;

CREATE OR REPLACE FUNCTION "shared"."lin_pccafeaccountcouponsetused"(
    p_coupon1 integer,
    p_coupon2 integer,
    p_coupon3 integer,
    p_coupon4 integer,
    p_user_db_id integer,
    p_world_id integer
) RETURNS TABLE (result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    inserted_rows integer := 0;
BEGIN
    INSERT INTO "shared"."cached_portable_pccafe_account_coupon_use" (
        "coupon1", "coupon2", "coupon3", "coupon4",
        "user_db_id", "world_id"
    ) VALUES (
        p_coupon1, p_coupon2, p_coupon3, p_coupon4,
        p_user_db_id, p_world_id
    )
    ON CONFLICT DO NOTHING;

    GET DIAGNOSTICS inserted_rows = ROW_COUNT;
    IF inserted_rows = 1 THEN
        RETURN QUERY SELECT 1;
        RETURN;
    END IF;

    -- The current caller only distinguishes 1 from failure here. Retaining
    -- the check-state codes still makes sequential diagnostics deterministic.
    -- a concurrent uniqueness collision is conservatively reported as 0.
    RETURN QUERY
    SELECT CASE
        WHEN EXISTS (
            SELECT 1
              FROM "shared"."cached_portable_pccafe_account_coupon_use" AS used
             WHERE used."coupon1" = p_coupon1
               AND used."coupon2" = p_coupon2
               AND used."coupon3" = p_coupon3
               AND used."coupon4" = p_coupon4
        ) THEN 0
        WHEN EXISTS (
            SELECT 1
              FROM "shared"."cached_portable_pccafe_account_coupon_use" AS used
             WHERE used."world_id" = p_world_id
               AND used."user_db_id" = p_user_db_id
        ) THEN 2
        ELSE 0
    END::integer;
END;
$function$;

COMMIT;
