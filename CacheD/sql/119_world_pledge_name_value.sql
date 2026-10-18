-- Current Cached.exe extended handlers 53 through 55.
--
-- Current machine code fixes the two callable name-value SQL ABIs. Pledge
-- skill loading is already supplied by migration 004. RequestLoadSubPledge
-- reads CPledge's in-memory map and does not call lin_LoadSubPledge.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_updatepledgenamevalue"(
    p_pledge_id integer,
    p_delta integer
) RETURNS TABLE (
    root_name_value integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    positive_delta bigint := CASE WHEN p_delta > 0 THEN p_delta ELSE 0 END;
BEGIN
    UPDATE "world"."pledge" AS pledge
    SET "root_name_value" = pledge."root_name_value" + p_delta,
        "accumulated_root_name_value" =
            COALESCE(pledge."accumulated_root_name_value", 0) + positive_delta
    WHERE pledge."pledge_id" = p_pledge_id;

    RETURN QUERY
    SELECT pledge."root_name_value"
    FROM "world"."pledge" AS pledge
    WHERE pledge."pledge_id" = p_pledge_id;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_addpledgenamevaluelog"(
    p_pledge_id integer,
    p_log_id integer,
    p_log_from integer,
    p_log_to integer,
    p_delta integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."pledge_namevalue_log" (
        "pledge_id",
        "log_id",
        "log_from",
        "log_to",
        "delta"
    ) VALUES (
        p_pledge_id,
        p_log_id::smallint,
        p_log_from,
        p_log_to,
        p_delta
    );
$function$;

COMMIT;
