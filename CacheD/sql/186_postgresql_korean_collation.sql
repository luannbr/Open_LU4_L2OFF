-- PostgreSQL replacement for the SQL Server world-database text collation.
--
-- The old C1 world DDL assigns Korean_Wansung_CI_AS to persistent character
-- columns.  Classic 3.0, modified protocol-287 and Ertheia use
-- that same named collation for the temporary user-macro carrier in
-- lin_MoveCharAddedService, while their persistent columns inherit the
-- database default.  Together these sources establish the intended flags:
-- case-insensitive, accent-sensitive, kana-insensitive and width-insensitive.
--
-- PostgreSQL has no Windows Wansung provider.  Its ICU Korean collation at
-- secondary strength is the cross-platform semantic replacement for those
-- four observable flags.  The early text-primitives migration creates that
-- collation before recovered function bodies can reference it. This migration
-- applies it to the finished catalog.  PostgreSQL 18 with ICU is required
-- instead of silently falling back to a host-dependent libc locale. SQL
-- Server's separate ANSI right-padding rule for equality is not a collation
-- property and remains recorded as a distinct compatibility edge.

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
      AND candidate.collencoding IN (-1, pg_catalog.pg_char_to_encoding('UTF8'));

    IF NOT FOUND THEN
        RAISE EXCEPTION
            'world.Korean_Wansung_CI_AS was not created by the early text-primitives migration';
    END IF;

    IF existing_provider <> 'i'
       OR existing_deterministic
       OR existing_locale <> 'ko-u-ks-level2' THEN
        RAISE EXCEPTION
            'world.Korean_Wansung_CI_AS has incompatible provider %, deterministic %, locale %',
            existing_provider,
            existing_deterministic,
            existing_locale;
    END IF;
END;
$collation$;

DO $columns$
DECLARE
    target_collation oid;
    text_column record;
BEGIN
    SELECT candidate.oid
    INTO STRICT target_collation
    FROM pg_catalog.pg_collation AS candidate
    JOIN pg_catalog.pg_namespace AS namespace
      ON namespace.oid = candidate.collnamespace
    WHERE namespace.nspname = 'world'
      AND candidate.collname = 'Korean_Wansung_CI_AS'
      AND candidate.collencoding IN (-1, pg_catalog.pg_char_to_encoding('UTF8'));

    FOR text_column IN
        SELECT namespace.nspname AS schema_name,
               relation.relname AS table_name,
               attribute.attname AS column_name,
               pg_catalog.format_type(
                   attribute.atttypid,
                   attribute.atttypmod
               ) AS data_type
        FROM pg_catalog.pg_attribute AS attribute
        JOIN pg_catalog.pg_class AS relation
          ON relation.oid = attribute.attrelid
        JOIN pg_catalog.pg_namespace AS namespace
          ON namespace.oid = relation.relnamespace
        WHERE namespace.nspname IN ('world', 'community', 'account')
          AND relation.relkind IN ('r', 'p')
          AND attribute.attnum > 0
          AND NOT attribute.attisdropped
          AND attribute.atttypid IN (
              'pg_catalog.text'::pg_catalog.regtype,
              'pg_catalog.varchar'::pg_catalog.regtype,
              'pg_catalog.bpchar'::pg_catalog.regtype
          )
          AND attribute.attcollation <> target_collation
        ORDER BY namespace.nspname,
                 relation.relname,
                 attribute.attnum
    LOOP
        EXECUTE pg_catalog.format(
            'ALTER TABLE %I.%I ALTER COLUMN %I TYPE %s '
            'COLLATE %I.%I USING %I::%s',
            text_column.schema_name,
            text_column.table_name,
            text_column.column_name,
            text_column.data_type,
            'world',
            'Korean_Wansung_CI_AS',
            text_column.column_name,
            text_column.data_type
        );
    END LOOP;

    IF EXISTS (
        SELECT 1
        FROM pg_catalog.pg_attribute AS attribute
        JOIN pg_catalog.pg_class AS relation
          ON relation.oid = attribute.attrelid
        JOIN pg_catalog.pg_namespace AS namespace
          ON namespace.oid = relation.relnamespace
        WHERE namespace.nspname IN ('world', 'community', 'account')
          AND relation.relkind IN ('r', 'p')
          AND attribute.attnum > 0
          AND NOT attribute.attisdropped
          AND attribute.atttypid IN (
              'pg_catalog.text'::pg_catalog.regtype,
              'pg_catalog.varchar'::pg_catalog.regtype,
              'pg_catalog.bpchar'::pg_catalog.regtype
          )
          AND attribute.attcollation <> target_collation
    ) THEN
        RAISE EXCEPTION
            'not every CacheD character column received world.Korean_Wansung_CI_AS';
    END IF;
END;
$columns$;

COMMIT;
