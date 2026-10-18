-- Current Cached.exe extended handlers 56 through 60 and 84.
--
-- Cached.exe fixes all four call names, placeholder counts, result-column
-- order and the double-precision packet carrier. High Five, Classic 3.0,
-- modified 287 and Ertheia agree on the selected relational
-- effects after those current ABIs have been established.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadcursedweaponlist"()
RETURNS TABLE (
    item_id integer,
    create_date integer,
    last_kill_date integer,
    expired_date integer,
    kill_point double precision,
    char_id integer,
    item_class_id integer,
    original_pk integer,
    total_pk integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        cursed_weapon."item_id",
        cursed_weapon."create_date",
        cursed_weapon."last_kill_date",
        cursed_weapon."expired_date",
        cursed_weapon."kill_point",
        cursed_weapon."char_id",
        cursed_weapon."item_class_id",
        cursed_weapon."original_pk",
        cursed_weapon."total_pk"
    FROM "world"."cursed_weapon" AS cursed_weapon;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_addcursedweapon"(
    p_item_id integer,
    p_create_date integer,
    p_last_kill_date integer,
    p_expired_date integer,
    p_kill_point double precision,
    p_char_id integer,
    p_item_class_id integer,
    p_original_pk integer,
    p_total_pk integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    -- The selected MSSQL body performs a conditional delete followed by an
    -- insert instead of an in-place update. An unconditional delete has the
    -- same observed row effect and keeps the delete/insert transition.
    DELETE FROM "world"."cursed_weapon"
    WHERE "item_id" = p_item_id;

    INSERT INTO "world"."cursed_weapon" (
        "item_id",
        "create_date",
        "last_kill_date",
        "expired_date",
        "kill_point",
        "char_id",
        "item_class_id",
        "original_pk",
        "total_pk"
    ) VALUES (
        p_item_id,
        p_create_date,
        p_last_kill_date,
        p_expired_date,
        p_kill_point,
        p_char_id,
        p_item_class_id,
        p_original_pk,
        p_total_pk
    );
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatecursedweapon"(
    p_item_id integer,
    p_kill_point double precision,
    p_last_kill_date integer,
    p_expired_date integer,
    p_char_id integer,
    p_total_pk integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."cursed_weapon"
    SET "kill_point" = trunc(p_kill_point),
        "last_kill_date" = p_last_kill_date,
        "expired_date" = p_expired_date,
        "char_id" = p_char_id,
        "total_pk" = p_total_pk
    WHERE "item_id" = p_item_id;
$function$;

-- The current wire and DB binder carry kill_point as SQL_DOUBLE. Every
-- compatible MSSQL procedure declares the update parameter as int, so the
-- truncation above keeps that database-boundary conversion while keeping
-- PostgreSQL's callable signature compatible with the current machine call.

CREATE OR REPLACE FUNCTION "world"."lin_delcursedweapon"(
    p_item_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."cursed_weapon"
    WHERE "item_id" = p_item_id;
$function$;

COMMIT;
