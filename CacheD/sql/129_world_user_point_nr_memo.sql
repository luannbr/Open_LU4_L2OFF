-- Extended requests 131-140. Current Cached.exe fixes all packet, callable
-- and result-column ABIs. Classic 3.0, Ertheia and both compatible 287 SQL
-- families agree on each relational body represented below.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_getwaitingtransferpledgemaster"(
    p_pledge_id integer
) RETURNS TABLE (waiting_count integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT COUNT(*)::integer
    FROM "world"."pledge_master_transfer" AS transfer
    WHERE transfer."pledge_id" = p_pledge_id
      AND transfer."status" = 0;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deleteskilllist"(
    p_char_id integer,
    p_subjob_id integer,
    p_skill_list text
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    -- The donor constructs `skill_id IN (<comma-separated list>)` as dynamic
    -- T-SQL. Production sends decimal IDs only. Parse that valid wire domain
    -- instead of importing arbitrary executable SQL. Malformed/empty tokens
    -- still fail instead of being silently ignored.
    IF p_skill_list IS NULL OR btrim(p_skill_list) = '' THEN
        RAISE EXCEPTION 'empty skill list';
    END IF;

    DELETE FROM "world"."user_skill" AS skill
    WHERE skill."char_id" = p_char_id
      AND skill."subjob_id" = p_subjob_id
      AND skill."skill_id" IN (
          SELECT btrim(token)::integer
          FROM unnest(string_to_array(p_skill_list, ',')) AS token
      );
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savepvppointrestrain"(
    p_char_id integer,
    p_give_count integer,
    p_give_time integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."user_pvppoint_restrain" (
        "char_id",
        "give_count",
        "give_time"
    ) VALUES (
        p_char_id,
        p_give_count,
        p_give_time
    )
    ON CONFLICT ("char_id") DO UPDATE
    SET "give_count" = EXCLUDED."give_count",
        "give_time" = EXCLUDED."give_time";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadpvppointrestrain"(
    p_char_id integer
) RETURNS TABLE (
    give_count integer,
    give_time integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT restrain."give_count", restrain."give_time"
    FROM "world"."user_pvppoint_restrain" AS restrain
    WHERE restrain."char_id" = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savelastcolor"(
    p_char_id integer,
    p_color_index integer,
    p_saved_time integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."user_last_color" (
        "char_id",
        "color_index",
        "saved_time"
    ) VALUES (
        p_char_id,
        p_color_index,
        p_saved_time
    )
    ON CONFLICT ("char_id") DO UPDATE
    SET "color_index" = EXCLUDED."color_index",
        "saved_time" = EXCLUDED."saved_time";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadlastcolor"(
    p_char_id integer
) RETURNS TABLE (
    color_index integer,
    saved_time integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT color."color_index", color."saved_time"
    FROM "world"."user_last_color" AS color
    WHERE color."char_id" = p_char_id;
$function$;

COMMIT;
