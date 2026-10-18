-- Derived physical-key closure for three current-reachable world relations.
--
-- Current Cached.exe fixes the logical row identities but cannot expose MSSQL
-- catalog constraints. Direct read-only parsing of two populated report MDFs
-- establishes the same keys across distinct schema variants. Rep_L2World52
-- additionally has the exact current-observable 61/5/2-column table shapes.
-- Existing duplicates are not deleted or merged: ADD CONSTRAINT must fail so
-- a contradictory deployment is inspected instead of silently rewritten.

BEGIN;

DO $migration$
DECLARE
    v_key text[];
BEGIN
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
      AND relation.relname = 'user_pccafe_point'
      AND constraint_row.contype = 'p'
    GROUP BY constraint_row.oid;

    IF v_key IS NULL THEN
        ALTER TABLE "world"."user_pccafe_point"
            ADD CONSTRAINT "pk_user_pccafe_point" PRIMARY KEY ("char_id");
    ELSIF v_key <> ARRAY['char_id']::text[] THEN
        RAISE EXCEPTION
            'world.user_pccafe_point has contradictory primary key: %', v_key;
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
      AND relation.relname = 'user_slot'
      AND constraint_row.contype = 'p'
    GROUP BY constraint_row.oid;

    IF v_key IS NULL THEN
        ALTER TABLE "world"."user_slot"
            ADD CONSTRAINT "pk_user_slot" PRIMARY KEY ("char_id");
    ELSIF v_key <> ARRAY['char_id']::text[] THEN
        RAISE EXCEPTION
            'world.user_slot has contradictory primary key: %', v_key;
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
      AND relation.relname = 'castle_war'
      AND constraint_row.contype = 'p'
    GROUP BY constraint_row.oid;

    IF v_key IS NULL THEN
        ALTER TABLE "world"."castle_war"
            ADD CONSTRAINT "pk_castle_war"
            PRIMARY KEY ("castle_id", "pledge_id", "world_id");
    ELSIF v_key <> ARRAY['castle_id', 'pledge_id', 'world_id']::text[] THEN
        RAISE EXCEPTION
            'world.castle_war has contradictory primary key: %', v_key;
    END IF;
END;
$migration$;

COMMIT;
