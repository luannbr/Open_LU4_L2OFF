-- SQL Server text comparison support. Creates the Korean comparison
-- collation before any function uses it, and adds the internal comparison
-- helpers for conditions that are not LIKE.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

DO $collation$
DECLARE
    existing_provider text;
    existing_deterministic boolean;
    existing_locale text;
BEGIN
    SELECT candidate.collprovider::text,
           candidate.collisdeterministic,
           candidate.colllocale
    INTO existing_provider,
         existing_deterministic,
         existing_locale
    FROM pg_catalog.pg_collation AS candidate
    JOIN pg_catalog.pg_namespace AS namespace
      ON namespace.oid = candidate.collnamespace
    WHERE namespace.nspname = 'world'
      AND candidate.collname = 'Korean_Wansung_CI_AS'
      AND candidate.collencoding IN (
          -1,
          pg_catalog.pg_char_to_encoding('UTF8')
      );

    IF FOUND THEN
        IF existing_provider <> 'i'
           OR existing_deterministic
           OR existing_locale <> 'ko-u-ks-level2' THEN
            RAISE EXCEPTION
                'world.Korean_Wansung_CI_AS already exists with incompatible provider %, deterministic %, locale %',
                existing_provider,
                existing_deterministic,
                existing_locale;
        END IF;
    ELSE
        EXECUTE
            'CREATE COLLATION "world"."Korean_Wansung_CI_AS" '
            '(provider = icu, locale = ''ko-u-ks-level2'', '
            'deterministic = false)';
    END IF;
END;
$collation$;

CREATE OR REPLACE FUNCTION "world"."mssql_text_eq"(
    p_left text,
    p_right text
) RETURNS boolean
LANGUAGE sql
IMMUTABLE
STRICT
PARALLEL SAFE
AS $function$
    SELECT
        (rtrim(p_left, ' ') COLLATE "world"."Korean_Wansung_CI_AS") =
        (rtrim(p_right, ' ') COLLATE "world"."Korean_Wansung_CI_AS");
$function$;

CREATE OR REPLACE FUNCTION "world"."mssql_text_ne"(
    p_left text,
    p_right text
) RETURNS boolean
LANGUAGE sql
IMMUTABLE
STRICT
PARALLEL SAFE
AS $function$
    SELECT
        (rtrim(p_left, ' ') COLLATE "world"."Korean_Wansung_CI_AS") <>
        (rtrim(p_right, ' ') COLLATE "world"."Korean_Wansung_CI_AS");
$function$;

COMMIT;
