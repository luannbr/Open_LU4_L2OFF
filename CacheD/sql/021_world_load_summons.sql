-- Current Cached.exe primary opcode 3 (RequestLoadSummons) database slice.
-- The current function fixes one int32 input and the exact eleven-column
-- result ABI. Modified-287, Classic 3.0 and Ertheia agree on this SELECT body.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadusersummon"(
    p_char_id integer
) RETURNS TABLE (
    summon_class_id integer,
    summon_db_id integer,
    summon_level integer,
    summon_org_remain_time integer,
    summon_remain_time integer,
    summon_exp_rate integer,
    summon_hp double precision,
    summon_mp double precision,
    summon_consume_item_id integer,
    summon_consume_item_count integer,
    summon_is_summon integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT summon.summon_class_id,
           summon.summon_db_id,
           summon.summon_level,
           summon.summon_org_remain_time,
           summon.summon_remain_time,
           summon.summon_exp_rate::integer,
           summon.summon_hp,
           summon.summon_mp,
           summon.summon_consume_item_id,
           summon.summon_consume_item_count,
           COALESCE(summon.summon_is_summon, 0)
    FROM "world"."user_summon_data" AS summon
    WHERE summon.char_id = p_char_id;
$function$;

COMMIT;
