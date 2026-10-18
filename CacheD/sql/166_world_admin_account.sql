-- Current Cached.exe AQ opcode 99 (RegisterAccountPacket).
--
-- The current machine image fixes the one-input lin_RegisterAccount ABI.
-- Classic 3.0, Essence 3.0, modified protocol-287 and Ertheia donors keep
-- the same idempotent insert into account_ch2.

BEGIN;

CREATE OR REPLACE FUNCTION "world"."lin_registeraccount"(
    p_account_name text
)
RETURNS void
LANGUAGE plpgsql
AS $function$
BEGIN
    INSERT INTO "world"."account_ch2" (
        "account"
    )
    SELECT p_account_name
    WHERE NOT EXISTS (
        SELECT 1
        FROM "world"."account_ch2" AS account
        WHERE "world"."mssql_text_eq"(account."account", p_account_name)
    );
END;
$function$;

COMMIT;
