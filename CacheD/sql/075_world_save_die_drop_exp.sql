-- Current Cached.exe primary opcode 100 persistence.
--
-- The current handler fixes the bigint/integer ABI and bind order. All
-- substantive full-arity donor families agree on the single-row update.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_savedropexp"(
    p_drop_exp bigint,
    p_character_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_data"
    SET "drop_exp" = p_drop_exp
    WHERE "char_id" = p_character_id;
$function$;

COMMIT;
