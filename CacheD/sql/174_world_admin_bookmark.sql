-- Current Cached.exe administrative AQ 23/24 bookmark persistence.
--
-- The current image fixes the ordered ODBC signatures. C1, High Five,
-- modified 287, Classic 3.0 and Ertheia keep the same
-- relational effects for both procedures.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_addbookmark"(
    p_char_id integer,
    p_bookmark_name text,
    p_world integer,
    p_x integer,
    p_y integer,
    p_z integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."bookmark" AS bookmark
        WHERE bookmark."char_id" = p_char_id
          AND "world"."mssql_text_eq"(bookmark."name", p_bookmark_name)
    ) THEN
        UPDATE "world"."bookmark" AS bookmark
        SET
            "world" = p_world,
            "x" = p_x,
            "y" = p_y,
            "z" = p_z
        WHERE bookmark."char_id" = p_char_id
          AND "world"."mssql_text_eq"(bookmark."name", p_bookmark_name);
    ELSE
        INSERT INTO "world"."bookmark" (
            "char_id",
            "name",
            "world",
            "x",
            "y",
            "z"
        ) VALUES (
            p_char_id,
            p_bookmark_name,
            p_world,
            p_x,
            p_y,
            p_z
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_delbookmark"(
    p_char_id integer,
    p_bookmark_name text
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."bookmark" AS bookmark
    WHERE bookmark."char_id" = p_char_id
      AND "world"."mssql_text_eq"(bookmark."name", p_bookmark_name);
$function$;

COMMIT;
