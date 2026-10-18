-- Current Cached.exe CNewCharDB persistence ABI.
--
-- Current machine code 0x004f96a8 and 0x004f9860 fixes the two result
-- columns, the two input parameters and the one-row integer result.  Classic
-- 3.0, Ertheia and modified protocol-287 exports agree on the relation, key,
-- ordering and duplicate-account behavior.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadnewcharlist"()
RETURNS TABLE (
    account_id integer,
    char_id integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT entry.account_id, entry.char_id
    FROM "world"."user_newchar" AS entry
    ORDER BY entry.account_id, entry.char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savenewchar"(
    p_account_id integer,
    p_char_id integer
) RETURNS TABLE (result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."user_newchar" AS entry
        WHERE entry.account_id = p_account_id
    ) THEN
        RETURN QUERY SELECT -1;
        RETURN;
    END IF;

    INSERT INTO "world"."user_newchar" (account_id, char_id)
    VALUES (p_account_id, p_char_id);

    RETURN QUERY SELECT 1;
END;
$function$;

COMMIT;
