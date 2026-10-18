-- Replace the earlier invented user_faction_point physical carrier with the
-- exact user_faction table kept in both directly parsed report MDFs and
-- the modified-287 world DDL. Current Cached.exe fixes the
-- int/int/bigint load projection, the five update inputs and bigint result.
--
-- An existing portable carrier is migrated only when its exact old shape and
-- key match. Conflicting target rows abort the transaction. No value is
-- discarded or selected by preference.

BEGIN;

CREATE TABLE IF NOT EXISTS "world"."user_faction" (
    "char_id" integer NOT NULL,
    "faction_id" integer NOT NULL,
    "faction_point" bigint NOT NULL,
    CONSTRAINT "pk_user_faction"
        PRIMARY KEY ("char_id", "faction_id")
);

DO $migration$
DECLARE
    v_shape text[];
    v_key text[];
    v_conflict boolean;
BEGIN
    SELECT array_agg(
               format(
                   '%s:%s:%s',
                   attribute.attname,
                   format_type(attribute.atttypid, attribute.atttypmod),
                   attribute.attnotnull
               )
               ORDER BY attribute.attnum
           )
    INTO v_shape
    FROM pg_attribute AS attribute
    JOIN pg_class AS relation
      ON relation.oid = attribute.attrelid
    JOIN pg_namespace AS namespace
      ON namespace.oid = relation.relnamespace
    WHERE namespace.nspname = 'world'
      AND relation.relname = 'user_faction'
      AND attribute.attnum > 0
      AND NOT attribute.attisdropped;

    IF v_shape <> ARRAY[
        'char_id:integer:t',
        'faction_id:integer:t',
        'faction_point:bigint:t'
    ]::text[] THEN
        RAISE EXCEPTION 'world.user_faction has contradictory shape: %', v_shape;
    END IF;

    SELECT array_agg(attribute.attname ORDER BY key_column.ordinality)
    INTO v_key
    FROM pg_constraint AS constraint_row
    JOIN pg_class AS relation
      ON relation.oid = constraint_row.conrelid
    JOIN pg_namespace AS namespace
      ON namespace.oid = relation.relnamespace
    CROSS JOIN LATERAL unnest(constraint_row.conkey)
        WITH ORDINALITY AS key_column(attnum, ordinality)
    JOIN pg_attribute AS attribute
      ON attribute.attrelid = relation.oid
     AND attribute.attnum = key_column.attnum
    WHERE namespace.nspname = 'world'
      AND relation.relname = 'user_faction'
      AND constraint_row.contype = 'p'
    GROUP BY constraint_row.oid;

    IF v_key IS NULL THEN
        ALTER TABLE "world"."user_faction"
            ADD CONSTRAINT "pk_user_faction"
            PRIMARY KEY ("char_id", "faction_id");
    ELSIF v_key <> ARRAY['char_id', 'faction_id']::text[] THEN
        RAISE EXCEPTION
            'world.user_faction has contradictory primary key: %', v_key;
    END IF;

    IF to_regclass('world.user_faction_point') IS NOT NULL THEN
        SELECT array_agg(
                   format(
                       '%s:%s:%s',
                       attribute.attname,
                       format_type(attribute.atttypid, attribute.atttypmod),
                       attribute.attnotnull
                   )
                   ORDER BY attribute.attnum
               )
        INTO v_shape
        FROM pg_attribute AS attribute
        JOIN pg_class AS relation
          ON relation.oid = attribute.attrelid
        JOIN pg_namespace AS namespace
          ON namespace.oid = relation.relnamespace
        WHERE namespace.nspname = 'world'
          AND relation.relname = 'user_faction_point'
          AND attribute.attnum > 0
          AND NOT attribute.attisdropped;

        IF v_shape <> ARRAY[
            'char_id:integer:t',
            'faction_type:integer:t',
            'point:bigint:t'
        ]::text[] THEN
            RAISE EXCEPTION
                'world.user_faction_point is not the reviewed portable carrier: %',
                v_shape;
        END IF;

        SELECT array_agg(attribute.attname ORDER BY key_column.ordinality)
        INTO v_key
        FROM pg_constraint AS constraint_row
        JOIN pg_class AS relation
          ON relation.oid = constraint_row.conrelid
        JOIN pg_namespace AS namespace
          ON namespace.oid = relation.relnamespace
        CROSS JOIN LATERAL unnest(constraint_row.conkey)
            WITH ORDINALITY AS key_column(attnum, ordinality)
        JOIN pg_attribute AS attribute
          ON attribute.attrelid = relation.oid
         AND attribute.attnum = key_column.attnum
        WHERE namespace.nspname = 'world'
          AND relation.relname = 'user_faction_point'
          AND constraint_row.contype = 'p'
        GROUP BY constraint_row.oid;

        IF v_key <> ARRAY['char_id', 'faction_type']::text[] THEN
            RAISE EXCEPTION
                'world.user_faction_point has contradictory primary key: %',
                v_key;
        END IF;

        EXECUTE $query$
            SELECT EXISTS (
                SELECT 1
                FROM "world"."user_faction_point" AS legacy
                JOIN "world"."user_faction" AS recovered
                  ON recovered."char_id" = legacy."char_id"
                 AND recovered."faction_id" = legacy."faction_type"
                WHERE recovered."faction_point" IS DISTINCT FROM legacy."point"
            )
        $query$
        INTO v_conflict;

        IF v_conflict THEN
            RAISE EXCEPTION
                'world.user_faction and the reviewed portable carrier conflict';
        END IF;

        EXECUTE $query$
            INSERT INTO "world"."user_faction" (
                "char_id",
                "faction_id",
                "faction_point"
            )
            SELECT legacy."char_id",
                   legacy."faction_type",
                   legacy."point"
            FROM "world"."user_faction_point" AS legacy
            ON CONFLICT ("char_id", "faction_id") DO NOTHING
        $query$;
    END IF;
END;
$migration$;

-- Recreate both callables before removing the reviewed legacy carrier from
-- an upgraded catalog. Dropping the exact old table then fails if any unknown
-- external dependency still refers to it.
DROP FUNCTION IF EXISTS "world"."lin_loadfactionpoint"(integer);
DROP FUNCTION IF EXISTS "world"."lin_updatefactionpoint"(
    integer,
    integer,
    integer,
    bigint,
    bigint
);

DROP TABLE IF EXISTS "world"."user_faction_point";

CREATE OR REPLACE FUNCTION "world"."lin_loadfactionpoint"(
    p_character_id integer
) RETURNS TABLE (
    faction_type integer,
    point bigint
)
LANGUAGE sql
STABLE
AS $function$
    SELECT faction."faction_id",
           faction."faction_point"
    FROM "world"."user_faction" AS faction
    WHERE faction."char_id" = p_character_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatefactionpoint"(
    p_command integer,
    p_character_id integer,
    p_faction_type integer,
    p_point bigint,
    p_maximum_point bigint
) RETURNS TABLE (point bigint)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_point bigint;
BEGIN
    INSERT INTO "world"."user_faction" (
        "char_id",
        "faction_id",
        "faction_point"
    ) VALUES (
        p_character_id,
        p_faction_type,
        LEAST(p_point, p_maximum_point)
    )
    ON CONFLICT ("char_id", "faction_id") DO UPDATE
    SET "faction_point" = LEAST(
        CASE p_command
            WHEN 0 THEN p_point
            WHEN 1 THEN "world"."user_faction"."faction_point" + p_point
            ELSE "world"."user_faction"."faction_point"
        END,
        p_maximum_point
    )
    RETURNING "user_faction"."faction_point"
    INTO v_point;

    RETURN QUERY SELECT v_point;
END;
$function$;

COMMIT;
