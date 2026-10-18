-- Store signed Builder levels, including the -2 and -1 access contexts.
-- BuilderCmd_setbuilder sends the level through CDB::SendSetBuilder opcode 0x36.
-- CacheD binds it as SQL_C_SLONG/SQL_INTEGER to lin_SetBuilderCharacter.
-- PostgreSQL stores the value as SMALLINT without an unsigned-only check.

BEGIN;

DO $migration$
DECLARE
    actual_type text;
BEGIN
    SELECT columns.data_type
      INTO actual_type
      FROM information_schema.columns AS columns
     WHERE columns.table_schema = 'world'
       AND columns.table_name = 'user_data'
       AND columns.column_name = 'builder';

    IF actual_type IS DISTINCT FROM 'smallint' THEN
        RAISE EXCEPTION
            'world.user_data.builder must be smallint before signed-domain migration; found %',
            COALESCE(actual_type, '<missing>');
    END IF;
END;
$migration$;

ALTER TABLE "world"."user_data"
    DROP CONSTRAINT IF EXISTS "ck_user_data_builder_tinyint";

COMMENT ON COLUMN "world"."user_data"."builder" IS
    'Signed Builder access level; current L2Server persists negative contexts -2 and -1.';

COMMIT;
