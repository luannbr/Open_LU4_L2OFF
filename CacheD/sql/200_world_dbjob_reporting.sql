-- Portability improvement, not original CacheD behavior: expose DBJob outcomes.
-- Result: 1 = executed, 0 = already executed, -1 = unsupported job type.
-- Executed means the cleanup completed, even when no rows needed removal.
-- SQL errors propagate and roll back both the claim and the cleanup.
-- The original void ABI remains available to all other callers.

BEGIN;

CREATE OR REPLACE FUNCTION "world"."cached_portable_execute_dbjob"(
    p_booking_time integer,
    p_job_type integer,
    p_job_value integer
) RETURNS integer
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    claimed_rows integer := 0;
    normalized_quest_id integer := p_job_value;
BEGIN
    IF p_job_type NOT IN (1, 2, 3, 4, 6) THEN
        RETURN -1;
    END IF;

    -- PostgreSQL concurrency replacement for donor IF NOT EXISTS + INSERT.
    -- Claim and effects share this function transaction, so an error rolls
    -- the marker back with the mutations.
    INSERT INTO "world"."regular_db_job" (
        booking_time, job_type, job_value, execute_time
    ) VALUES (
        p_booking_time, p_job_type, p_job_value, "world"."mssql_getdate"()
    ) ON CONFLICT (booking_time, job_type, job_value) DO NOTHING;
    GET DIAGNOSTICS claimed_rows = ROW_COUNT;
    IF claimed_rows = 0 THEN
        RETURN 0;
    END IF;

    IF p_job_type = 1 THEN
        PERFORM "world"."cached_portable_delete_item_by_type_for_dbjob"(p_job_value);
    ELSIF p_job_type = 2 THEN
        DELETE FROM "world"."user_newactiveskill"
        WHERE skill_id = p_job_value;
    ELSIF p_job_type = 3 THEN
        DELETE FROM "world"."user_quest"
        WHERE quest_no = p_job_value;

        IF normalized_quest_id > 10000 THEN
            normalized_quest_id := normalized_quest_id - 10000;
        END IF;
        IF (p_job_value <= 255 OR p_job_value > 10000)
           AND normalized_quest_id BETWEEN 0 AND 2047 THEN
            UPDATE "world"."user_data"
            SET quest_flag = set_byte(
                quest_flag,
                normalized_quest_id / 8,
                get_byte(quest_flag, normalized_quest_id / 8)
                    & ~(1 << (normalized_quest_id % 8))
            )
            WHERE quest_flag IS NOT NULL
              AND octet_length(quest_flag) = 256;
        END IF;
    ELSIF p_job_type = 4 THEN
        DELETE FROM "world"."ui_event_count"
        WHERE log_date < p_job_value;
    END IF;
    RETURN 1;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_executedbjob"(
    p_booking_time integer,
    p_job_type integer,
    p_job_value integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    PERFORM "world"."cached_portable_execute_dbjob"(
        p_booking_time, p_job_type, p_job_value);
END;
$function$;

COMMIT;
