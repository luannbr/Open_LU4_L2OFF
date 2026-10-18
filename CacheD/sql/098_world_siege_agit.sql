-- Current Cached.exe primary opcodes 189-191:
-- RequestLoadSiegeAgit, RequestRegisterSiegeAgitPledge and
-- RequestUnregisterSiegeAgitPledge.
--
-- Current machine code fixes all three callable ABIs, ordered integer
-- bindings and result widths. Classic 3.0, modified 287, Ertheia and the
-- other full-arity compatible donors keep identical relational bodies and
-- effect sets over the already selected siege_agit_pledge table.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadsiegeagitpledge"(
    p_agit_id integer
) RETURNS TABLE (
    pledge_id integer,
    propose_time integer,
    status integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        registration."pledge_id",
        registration."propose_time",
        registration."status"
    FROM "world"."siege_agit_pledge" AS registration
    WHERE registration."agit_id" = p_agit_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_registersiegeagitpledge"(
    p_agit_id integer,
    p_pledge_id integer,
    p_propose_time integer
) RETURNS TABLE (result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    BEGIN
        INSERT INTO "world"."siege_agit_pledge" (
            "agit_id",
            "pledge_id",
            "propose_time"
        ) VALUES (
            p_agit_id,
            p_pledge_id,
            p_propose_time
        );
        result := 1;
    EXCEPTION WHEN OTHERS THEN
        result := 0;
    END;
    RETURN NEXT;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_unregistersiegeagitpledge"(
    p_agit_id integer,
    p_pledge_id integer
) RETURNS TABLE (result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    BEGIN
        DELETE FROM "world"."siege_agit_pledge"
        WHERE "siege_agit_pledge"."agit_id" = p_agit_id
          AND "siege_agit_pledge"."pledge_id" = p_pledge_id;
        -- The shipping T-SQL reports success even when no row matched.
        result := 1;
    EXCEPTION WHEN OTHERS THEN
        result := 0;
    END;
    RETURN NEXT;
END;
$function$;

COMMIT;
