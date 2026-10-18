-- Current Cached.exe extended requests 281 through 290.
--
-- The current image fixes the callable ABIs and result-column ordering.
-- Classic 3.0, modified 287 and Ertheia supply matching relational bodies
-- for the dual-class, daily-count and mentor procedures.  Two historical
-- oddities are kept: lin_ClearDailyCount compares each
-- input parameter with itself and therefore clears the whole relation for
-- non-null inputs, while lin_LoadMentorPenalty does not constrain its outer
-- row to the requested mentor after finding that mentor's maximum end time.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_canceldualclass"(
    p_char_id integer
) RETURNS TABLE (result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_updated_rows integer;
BEGIN
    UPDATE "world"."user_data" AS character
    SET "dual_class_id" = -1
    WHERE character."char_id" = p_char_id;

    GET DIAGNOSTICS v_updated_rows = ROW_COUNT;
    RETURN QUERY
    SELECT CASE WHEN v_updated_rows = 1 THEN 1 ELSE 0 END;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setdailycount"(
    p_char_id integer,
    p_event_id integer
) RETURNS TABLE (daily_count integer)
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."user_daily_count" AS daily (
        "char_id",
        "event_id",
        "count",
        "optional",
        "last_update_time"
    ) VALUES (
        p_char_id,
        p_event_id,
        1,
        0,
        "world"."mssql_getdate"()
    )
    ON CONFLICT ("char_id", "event_id") DO UPDATE
    SET "count" = daily."count" + 1,
        "last_update_time" = "world"."mssql_getdate"()
    RETURNING "count";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_cleardailycount"(
    p_char_id integer,
    p_event_id integer
) RETURNS TABLE (cleared_count integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_deleted_rows integer;
BEGIN
    -- Shipping donor text is literally
    -- `where @char_id = @char_id and @event_id = @event_id`.
    DELETE FROM "world"."user_daily_count"
    WHERE p_char_id = p_char_id
      AND p_event_id = p_event_id;

    GET DIAGNOSTICS v_deleted_rows = ROW_COUNT;
    RETURN QUERY SELECT v_deleted_rows;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_addnewmentee"(
    p_mentor_id integer,
    p_mentee_id integer,
    p_add_time integer
) RETURNS TABLE (active_mentee_count integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_active_mentee_count integer;
BEGIN
    SELECT count(*)::integer
    INTO v_active_mentee_count
    FROM "world"."user_mentor" AS mentor
    INNER JOIN "world"."user_data" AS mentee
        ON mentee."char_id" = mentor."mentee_id"
    WHERE mentor."mentor_id" = p_mentor_id
      AND mentor."status" = 1
      AND mentee."account_id" > 0;

    IF v_active_mentee_count < 3 THEN
        IF EXISTS (
            SELECT 1
            FROM "world"."user_mentor" AS mentor
            WHERE mentor."mentor_id" = p_mentor_id
              AND mentor."mentee_id" = p_mentee_id
        ) THEN
            UPDATE "world"."user_mentor" AS mentor
            SET "status" = 1,
                "start_time" = p_add_time
            WHERE mentor."mentor_id" = p_mentor_id
              AND mentor."mentee_id" = p_mentee_id;
        ELSE
            INSERT INTO "world"."user_mentor" (
                "mentor_id",
                "mentee_id",
                "status",
                "start_time",
                "mentor_playtime",
                "mentee_playtime"
            ) VALUES (
                p_mentor_id,
                p_mentee_id,
                1,
                p_add_time,
                0,
                0
            );
        END IF;
    END IF;

    RETURN QUERY SELECT v_active_mentee_count;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadmenteelist"(
    p_char_id integer
) RETURNS TABLE (
    mentee_id integer,
    mentee_name text,
    mentee_class integer,
    mentee_level integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        mentor."mentee_id",
        mentee."char_name"::text,
        mentee."class",
        mentee."lev"::integer
    FROM "world"."user_mentor" AS mentor
    INNER JOIN "world"."user_data" AS mentee
        ON mentee."char_id" = mentor."mentee_id"
    WHERE mentor."mentor_id" = p_char_id
      AND mentor."status" = 1;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadmentorpenalty"(
    p_char_id integer
) RETURNS TABLE (
    penalty_status integer,
    penalty_end_time integer
)
LANGUAGE sql
STABLE
AS $function$
    WITH requested_maximum AS (
        SELECT max(COALESCE(mentor."end_time", 0)) AS end_time
        FROM "world"."user_mentor" AS mentor
        WHERE mentor."mentor_id" = p_char_id
          AND mentor."status" > 1
    )
    SELECT
        mentor."status",
        COALESCE(mentor."end_time", 0)
    FROM "world"."user_mentor" AS mentor
    CROSS JOIN requested_maximum
    WHERE requested_maximum.end_time = mentor."end_time";
$function$;

COMMIT;
