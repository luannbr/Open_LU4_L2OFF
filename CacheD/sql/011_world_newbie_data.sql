-- Current Cached.exe CNewbieDb persistence ABI.
--
-- The four callable signatures and all bind/result positions come from the
-- current machine block 0x004f8694..0x004f93cd.  The relation, key and
-- effects are identical in the Classic 3.0, Ertheia, protocol-287 and High
-- Five database exports.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadnewbiedata"()
RETURNS TABLE (
    account_id integer,
    char_id integer,
    newbie_stat integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT data.account_id,
           data.char_id,
           data.newbie_stat::integer
    FROM "world"."user_newbie" AS data;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_createnewbiedata"(
    p_account_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."user_newbie" (
        account_id, char_id, newbie_stat
    ) VALUES (
        p_account_id, 0, 0
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatenewbiechardata"(
    p_account_id integer,
    p_char_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_newbie" AS data
    SET char_id = p_char_id
    WHERE data.account_id = p_account_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setnewbiestat"(
    p_account_id integer,
    p_newbie_stat integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_newbie" AS data
    SET newbie_stat = p_newbie_stat
    WHERE data.account_id = p_account_id;
$function$;

COMMIT;
