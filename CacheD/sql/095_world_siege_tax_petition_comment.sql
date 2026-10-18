-- Current Cached.exe primary opcodes 171, 172 and 174-176.
--
-- The current image fixes every callable ABI below. Classic 3.0, modified
-- 287 and Ertheia provide full-arity-compatible relational bodies. The
-- two-key castle-war delete and three-date tax state are the modern forms
-- selected by current CacheD/L2Server machine code.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_deletemasterrelatedcastlewar"(
    p_pledge_id integer,
    p_world_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_deleted integer := 0;
BEGIN
    DELETE FROM "world"."castle_war" AS castle_war
    WHERE castle_war."pledge_id" = p_pledge_id
      AND castle_war."world_id" = p_world_id;
    GET DIAGNOSTICS v_deleted = ROW_COUNT;
    IF v_deleted = 0 THEN
        RAISE EXCEPTION
            'pledge id is not exist in castle_war.[%]',
            p_pledge_id;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getdbidbycharname"(
    p_character_name text
) RETURNS TABLE (
    char_id integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT character."char_id"
    FROM "world"."user_data" AS character
    WHERE "world"."mssql_text_eq"(character."char_name", p_character_name)
    LIMIT 1;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadlasttaxupdate"()
RETURNS TABLE (
    income_year integer,
    income_month integer,
    income_day integer,
    income_hour integer,
    income_minute integer,
    income_second integer,
    tax_year integer,
    tax_month integer,
    tax_day integer,
    tax_hour integer,
    tax_minute integer,
    tax_second integer,
    manor_year integer,
    manor_month integer,
    manor_day integer,
    manor_hour integer,
    manor_minute integer,
    manor_second integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        extract(year FROM tax."income_update")::integer,
        extract(month FROM tax."income_update")::integer,
        extract(day FROM tax."income_update")::integer,
        extract(hour FROM tax."income_update")::integer,
        extract(minute FROM tax."income_update")::integer,
        trunc(extract(second FROM tax."income_update"))::integer,
        extract(year FROM tax."tax_change")::integer,
        extract(month FROM tax."tax_change")::integer,
        extract(day FROM tax."tax_change")::integer,
        extract(hour FROM tax."tax_change")::integer,
        extract(minute FROM tax."tax_change")::integer,
        trunc(extract(second FROM tax."tax_change"))::integer,
        extract(year FROM tax."manor_reset")::integer,
        extract(month FROM tax."manor_reset")::integer,
        extract(day FROM tax."manor_reset")::integer,
        extract(hour FROM tax."manor_reset")::integer,
        extract(minute FROM tax."manor_reset")::integer,
        trunc(extract(second FROM tax."manor_reset"))::integer
    FROM "world"."castle_tax" AS tax
    LIMIT 1;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setlasttaxupdate"(
    p_income text,
    p_tax text,
    p_manor text
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (SELECT 1 FROM "world"."castle_tax") THEN
        UPDATE "world"."castle_tax"
        SET "income_update" = p_income::timestamp without time zone,
            "tax_change" = p_tax::timestamp without time zone,
            "manor_reset" = p_manor::timestamp without time zone;
    ELSE
        INSERT INTO "world"."castle_tax" (
            "income_update",
            "tax_change",
            "manor_reset"
        ) VALUES (
            p_income::timestamp without time zone,
            p_tax::timestamp without time zone,
            p_manor::timestamp without time zone
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_commentwrite"(
    p_character_name text,
    p_character_id integer,
    p_comment text,
    p_writer text
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."user_comment" (
        "char_name",
        "char_id",
        "comment",
        "writer"
    ) VALUES (
        p_character_name,
        p_character_id,
        p_comment,
        p_writer
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_commentsearch"(
    p_character_id integer,
    p_page integer
) RETURNS TABLE (
    comment_id integer,
    char_name text,
    char_id integer,
    comment text,
    writer text
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        user_comment."comment_id",
        user_comment."char_name"::text,
        user_comment."char_id",
        user_comment."comment"::text,
        user_comment."writer"::text
    FROM "world"."user_comment" AS user_comment
    WHERE user_comment."char_id" = p_character_id
      AND user_comment."deleted" = 0
    ORDER BY user_comment."comment_id" DESC;
$function$;

COMMENT ON FUNCTION "world"."lin_commentsearch"(integer, integer) IS
    'The compatible MSSQL body accepts page but does not use it.';

COMMIT;
