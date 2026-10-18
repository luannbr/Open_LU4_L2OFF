-- Current Cached.exe primary opcodes 87 and 89 through 91 persistence.
--
-- The current handler result bindings and input order fix every procedure
-- ABI. Modified-287, Classic 3.0 and Ertheia unanimously provide the same
-- substantive relational bodies.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_getbookmark"(
    p_char_id integer,
    p_bookmark_name text
) RETURNS TABLE (
    world_id integer,
    x integer,
    y integer,
    z integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        bookmark."world"::integer,
        bookmark."x"::integer,
        bookmark."y"::integer,
        bookmark."z"::integer
    FROM "world"."bookmark" AS bookmark
    WHERE bookmark."char_id" = p_char_id
      AND "world"."mssql_text_eq"(bookmark."name", p_bookmark_name);
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loaddoor"(
    p_name text
) RETURNS TABLE (
    hp integer,
    max_hp integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        door."hp"::integer,
        COALESCE(door."max_hp", door."hp")::integer
    FROM "world"."door" AS door
    WHERE "world"."mssql_text_eq"(door."name", p_name);
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_createdoordataifnotexist"(
    p_name text,
    p_default_hp integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."door" (
        "name",
        "hp",
        "max_hp"
    ) VALUES (
        p_name,
        p_default_hp,
        p_default_hp
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savecastlestatus"(
    p_status integer,
    p_castle_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."castle" AS castle
    SET "status" = p_status
    WHERE castle."id" = p_castle_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savedoorstatus"(
    p_hp integer,
    p_max_hp integer,
    p_name text
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."door" AS door
    SET
        "hp" = p_hp,
        "max_hp" = p_max_hp
    WHERE "world"."mssql_text_eq"(door."name", p_name);
$function$;

COMMIT;
