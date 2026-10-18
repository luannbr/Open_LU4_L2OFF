-- Current Cached.exe Shared/DXPacket.cpp residence-decoration persistence.
--
-- DX slots 1 through 4 fix all six callable ABIs and their result widths.
-- Classic 3.0, modified 287 and Ertheia keep the same
-- relational bodies against the current agit/agit_deco columns.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_activateagitdeco"(
    p_is_active integer,
    p_agit_id integer,
    p_type smallint
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."agit_deco" AS deco
    SET "is_active" = p_is_active
    WHERE deco."agit_id" = p_agit_id
      AND deco."type" = p_type;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_renewagitdeco"(
    p_agit_id integer,
    p_type smallint,
    p_expire integer,
    p_is_active integer
) RETURNS TABLE (
    result smallint
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_rows integer := 0;
BEGIN
    UPDATE "world"."agit_deco" AS deco
    SET "expire" = p_expire,
        "is_active" = p_is_active
    WHERE deco."agit_id" = p_agit_id
      AND deco."type" = p_type;
    GET DIAGNOSTICS v_rows = ROW_COUNT;
    RETURN QUERY SELECT v_rows::smallint;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_resetagitdeco"(
    p_agit_id integer,
    p_type integer
) RETURNS TABLE (
    result integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_rows integer := 0;
BEGIN
    DELETE FROM "world"."agit_deco" AS deco
    WHERE deco."agit_id" = p_agit_id
      AND deco."type" = p_type;
    GET DIAGNOSTICS v_rows = ROW_COUNT;
    RETURN QUERY SELECT v_rows;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_resetallagitdeco"(
    p_agit_id integer
) RETURNS TABLE (
    result integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_rows integer := 0;
BEGIN
    DELETE FROM "world"."agit_deco" AS deco
    WHERE deco."agit_id" = p_agit_id;
    GET DIAGNOSTICS v_rows = ROW_COUNT;
    RETURN QUERY SELECT v_rows;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setagitdeco"(
    p_agit_id integer,
    p_type integer,
    p_id integer,
    p_name text,
    p_level integer,
    p_expire integer,
    p_is_active integer
) RETURNS TABLE (
    result integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_rows integer := 0;
BEGIN
    DELETE FROM "world"."agit_deco" AS deco
    WHERE deco."agit_id" = p_agit_id
      AND deco."type" = p_type;

    INSERT INTO "world"."agit_deco" (
        "agit_id",
        "type",
        "id",
        "name",
        "level",
        "expire",
        "is_active"
    ) VALUES (
        p_agit_id,
        p_type,
        p_id,
        p_name,
        p_level,
        p_expire,
        p_is_active
    );
    GET DIAGNOSTICS v_rows = ROW_COUNT;
    RETURN QUERY SELECT v_rows;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_uninstallagitdeco"(
    p_agit_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."agit" AS agit
    SET "hp_stove" = 0,
        "hp_stove_expire" = 0,
        "mp_flame" = 0,
        "mp_flame_expire" = 0,
        "teleport_level" = 0,
        "teleport_expire" = 0,
        "hatcher" = 0
    WHERE agit."id" = p_agit_id;
$function$;

COMMIT;
