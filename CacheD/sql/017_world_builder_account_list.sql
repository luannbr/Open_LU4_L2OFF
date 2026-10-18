-- Current Cached.exe 0x0045fbf4 calls the zero-input
-- lin_SelectTop200BuilderAccount procedure and binds one 20-byte char result.
-- The SELECT body is identical in the compatible High Five, modified-287,
-- Classic 3.0 and Ertheia donors.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_selecttop200builderaccount"()
RETURNS TABLE (account_name text)
LANGUAGE sql
STABLE
AS $function$
    -- DISTINCT ON uses the SQL Server comparison key while returning the
    -- original account spelling/padding from an otherwise unordered donor row.
    SELECT DISTINCT ON (
        rtrim(builder.account_name::text, ' ')
            COLLATE "world"."Korean_Wansung_CI_AS"
    ) builder.account_name::text
    FROM "world"."builder_account" AS builder
    LIMIT 200;
$function$;

COMMIT;
