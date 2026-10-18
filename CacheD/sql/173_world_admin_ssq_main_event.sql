-- Current Cached.exe AQ opcodes 86 and 150.
--
-- Both handlers bind the same zero-input, nine-result procedure. The current
-- image fixes the callable/result ABI. Classic 3.0, modified 287 and Ertheia
-- keep the same substantive ordered SELECT.

BEGIN;

CREATE OR REPLACE FUNCTION "world"."lin_getssqmaineventrecord"()
RETURNS TABLE (
    ssq_round integer,
    room_number integer,
    record_type integer,
    point integer,
    record_time integer,
    elapsed_time integer,
    member_count integer,
    member_names text,
    epoch_seconds integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        record."ssq_round",
        record."room_no"::integer,
        record."record_type"::integer,
        record."point",
        record."record_time",
        record."elapsed_time",
        record."member_count",
        record."member_names"::text,
        floor(extract(epoch FROM statement_timestamp()))::integer
    FROM "world"."time_attack_record" AS record
    WHERE record."record_type" > 0
      AND record."ssq_round" = (
          SELECT max(data."round_number")
          FROM "world"."ssq_data" AS data
      )
    ORDER BY record."record_type" ASC,
             record."room_no" DESC;
$function$;

COMMIT;
