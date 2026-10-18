-- Current Cached.exe primary opcode 21 (RequestCharacterLogin) persistence.
--
-- CUser::WriteLogin 0x005a8288 executes exactly one int32 input through
-- lin_CharLogin before publishing the logged-in user, advancing its three
-- tick baselines and emitting enter-world log 0x323. Eight substantive,
-- full-arity-compatible SQL donor definitions share this one-row update.
-- the current machine code fixes the procedure name, arity,
-- input type and execute-before-publication branch.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_charlogin"(
    p_char_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_data"
    SET "login" = "world"."mssql_getdate"()
    WHERE "char_id" = p_char_id;
$function$;

COMMIT;
