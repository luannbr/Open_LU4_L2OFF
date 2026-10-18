-- Extended requests 221-230. Current Cached.exe fixes the callable ABIs and
-- every Execute/Fetch branch. Classic 3.0 and the compatible later database
-- families keep the selected relational bodies. Request 230 reuses the
-- existing lin_brloaditemagathionenergy function from migration 029.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_brgeteventscore"(
    p_event_id integer,
    p_round_no integer,
    p_char_id integer
) RETURNS TABLE (score integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT event_score.score
    FROM "world"."br_event_score" AS event_score
    WHERE event_score.event_id = p_event_id
      AND event_score.round_no = p_round_no
      AND event_score.char_id = p_char_id
    LIMIT 1;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_brseteventscore"(
    p_event_id integer,
    p_round_no integer,
    p_char_id integer,
    p_score integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    -- The donor omits round_no from both the existence test and
    -- update condition. Preserve that cross-round overwrite behavior.
    IF EXISTS (
        SELECT 1
        FROM "world"."br_event_score" AS event_score
        WHERE event_score.event_id = p_event_id
          AND event_score.char_id = p_char_id
    ) THEN
        UPDATE "world"."br_event_score" AS event_score
        SET score = p_score,
            score_date = "world"."mssql_getdate"()
        WHERE event_score.event_id = p_event_id
          AND event_score.char_id = p_char_id;
    ELSE
        INSERT INTO "world"."br_event_score" (
            event_id,
            round_no,
            char_id,
            score,
            score_date
        ) VALUES (
            p_event_id,
            p_round_no,
            p_char_id,
            p_score,
            "world"."mssql_getdate"()
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_brloadminigamemyscore"(
    p_char_id integer
) RETURNS TABLE (
    my_rank integer,
    my_score integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_score integer;
    v_rank integer;
BEGIN
    SELECT minigame.score
    INTO v_score
    FROM "world"."br_minigame_score" AS minigame
    WHERE minigame.char_id = p_char_id
    LIMIT 1;

    IF NOT FOUND THEN
        RETURN QUERY SELECT 0, 0;
        RETURN;
    END IF;

    SELECT COUNT(*)::integer + 1
    INTO v_rank
    FROM "world"."br_minigame_score" AS minigame
    WHERE minigame.score > v_score;

    -- The shipping donor uses a strict comparison: rank 1000 is discarded.
    IF v_rank < 1000 THEN
        RETURN QUERY SELECT v_rank, v_score;
    ELSE
        DELETE FROM "world"."br_minigame_score" AS minigame
        WHERE minigame.char_id = p_char_id;
        RETURN QUERY SELECT 0, 0;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_brloadminigamebottomranker"()
RETURNS TABLE (
    bottom_rank integer,
    bottom_score integer
)
LANGUAGE plpgsql
STABLE
AS $function$
BEGIN
    IF EXISTS (SELECT 1 FROM "world"."br_minigame_score") THEN
        -- SQL Server's TOP(1000) variable assignment leaves the final row in
        -- the two output variables. The score ordering used by rank() makes
        -- that observable result the bottom member of the top 1000.
        RETURN QUERY
        WITH ranked AS MATERIALIZED (
            SELECT
                rank() OVER (
                    ORDER BY minigame.score DESC
                )::integer AS ranking,
                minigame.score
            FROM "world"."br_minigame_score" AS minigame
            ORDER BY minigame.score DESC
            LIMIT 1000
        )
        SELECT ranked.ranking,
               ranked.score
        FROM ranked
        ORDER BY ranked.ranking DESC
        LIMIT 1;
    ELSE
        RETURN QUERY SELECT 0, 0;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_brloadminigametoprankers"()
RETURNS TABLE (
    top_rank integer,
    char_name text,
    score integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        rank() OVER (
            ORDER BY minigame.score DESC
        )::integer,
        minigame.char_name::text,
        minigame.score
    FROM "world"."br_minigame_score" AS minigame
    ORDER BY minigame.score DESC
    LIMIT 100;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_brinsertminigamescore"(
    p_char_id integer,
    p_char_name text,
    p_score integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_my_rank integer;
BEGIN
    SELECT COUNT(*)::integer + 1
    INTO v_my_rank
    FROM "world"."br_minigame_score" AS minigame
    WHERE minigame.score > p_score;

    IF v_my_rank <= 1000 THEN
        IF EXISTS (
            SELECT 1
            FROM "world"."br_minigame_score" AS minigame
            WHERE minigame.char_id = p_char_id
              AND minigame.score < p_score
        ) THEN
            DELETE FROM "world"."br_minigame_score" AS minigame
            WHERE minigame.char_id = p_char_id
              AND minigame.score < p_score;
        END IF;

        IF NOT EXISTS (
            SELECT 1
            FROM "world"."br_minigame_score" AS minigame
            WHERE minigame.char_id = p_char_id
              AND minigame.score >= p_score
        ) THEN
            INSERT INTO "world"."br_minigame_score" (
                char_id,
                char_name,
                score
            ) VALUES (
                p_char_id,
                p_char_name,
                p_score
            );
        END IF;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_brgetlecturemark"(
    p_account_id integer
) RETURNS TABLE (
    lecture_mark integer,
    result_message integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_first_create_date timestamp without time zone;
    v_month_diff integer;
    v_lecture_mark integer;
    v_lecture_message integer;
    v_result_message integer;
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."user_data" AS users
        WHERE users.account_id = p_account_id
    ) THEN
        SELECT users.create_date
        INTO v_first_create_date
        FROM "world"."user_data" AS users
        WHERE users.account_id = p_account_id
        ORDER BY users.create_date
        LIMIT 1;

        -- MSSQL datediff(month, ...) counts crossed calendar-month
        -- boundaries instead of complete elapsed months.
        v_month_diff :=
            (EXTRACT(YEAR FROM "world"."mssql_getdate"())::integer * 12 +
             EXTRACT(MONTH FROM "world"."mssql_getdate"())::integer) -
            (EXTRACT(YEAR FROM v_first_create_date)::integer * 12 +
             EXTRACT(MONTH FROM v_first_create_date)::integer);

        IF NOT EXISTS (
            SELECT 1
            FROM "world"."br_account_data" AS account_data
            WHERE account_data.account_id = p_account_id
        ) THEN
            IF v_month_diff <= 6 THEN
                INSERT INTO "world"."br_account_data" VALUES (
                    p_account_id, 1, 0
                );
            ELSE
                INSERT INTO "world"."br_account_data" VALUES (
                    p_account_id, 3, 0
                );
            END IF;
        END IF;

        SELECT account_data.lecture_mark,
               account_data.lecture_message
        INTO v_lecture_mark,
             v_lecture_message
        FROM "world"."br_account_data" AS account_data
        WHERE account_data.account_id = p_account_id
        LIMIT 1;
        v_result_message := 0;

        IF v_lecture_message < 1 AND v_month_diff >= 3 THEN
            v_result_message := 1;
            UPDATE "world"."br_account_data" AS account_data
            SET lecture_message = 1
            WHERE account_data.account_id = p_account_id;
        END IF;

        IF v_lecture_message < 2 AND v_month_diff >= 6 THEN
            IF v_lecture_message = 0 THEN
                v_result_message := 3;
            ELSE
                v_result_message := 2;
            END IF;
            UPDATE "world"."br_account_data" AS account_data
            SET lecture_message = 2
            WHERE account_data.account_id = p_account_id;
        END IF;

        IF v_lecture_mark = 1 AND v_month_diff >= 6 THEN
            UPDATE "world"."br_account_data" AS account_data
            SET lecture_mark = 3
            WHERE account_data.account_id = p_account_id;
        END IF;

        SELECT account_data.lecture_mark,
               account_data.lecture_message
        INTO v_lecture_mark,
             v_lecture_message
        FROM "world"."br_account_data" AS account_data
        WHERE account_data.account_id = p_account_id
        LIMIT 1;
        RETURN QUERY SELECT v_lecture_mark, v_result_message;
    ELSE
        RETURN QUERY SELECT -1, NULL::integer;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_brsetlecturemark"(
    p_account_id integer,
    p_lecture_mark integer
) RETURNS TABLE (lecture_mark integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_first_create_date timestamp without time zone;
    v_month_diff integer;
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."user_data" AS users
        WHERE users.account_id = p_account_id
    ) THEN
        SELECT users.create_date
        INTO v_first_create_date
        FROM "world"."user_data" AS users
        WHERE users.account_id = p_account_id
        ORDER BY users.create_date
        LIMIT 1;

        v_month_diff :=
            (EXTRACT(YEAR FROM "world"."mssql_getdate"())::integer * 12 +
             EXTRACT(MONTH FROM "world"."mssql_getdate"())::integer) -
            (EXTRACT(YEAR FROM v_first_create_date)::integer * 12 +
             EXTRACT(MONTH FROM v_first_create_date)::integer);

        IF p_lecture_mark = 1 AND v_month_diff < 6 THEN
            UPDATE "world"."br_account_data" AS account_data
            SET lecture_mark = p_lecture_mark
            WHERE account_data.account_id = p_account_id;
        END IF;
        IF p_lecture_mark = 2 AND v_month_diff >= 3 THEN
            UPDATE "world"."br_account_data" AS account_data
            SET lecture_mark = p_lecture_mark
            WHERE account_data.account_id = p_account_id;
        END IF;
        IF p_lecture_mark = 3 THEN
            UPDATE "world"."br_account_data" AS account_data
            SET lecture_mark = p_lecture_mark
            WHERE account_data.account_id = p_account_id;
        END IF;

        RETURN QUERY
        SELECT account_data.lecture_mark::integer
        FROM "world"."br_account_data" AS account_data
        WHERE account_data.account_id = p_account_id;
    ELSE
        RETURN QUERY SELECT NULL::integer;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_brsetcreatedate"(
    p_char_id integer,
    p_year integer,
    p_month integer,
    p_day integer,
    p_hour integer,
    p_minute integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_data" AS users
    SET create_date = make_timestamp(
        p_year,
        p_month,
        p_day,
        p_hour,
        p_minute,
        0.0
    )
    WHERE users.char_id = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_brloaddropitemamountperserver"(
    p_item_class_id integer
) RETURNS TABLE (
    item_amount integer,
    drop_year integer,
    drop_month integer,
    drop_day integer,
    drop_hour integer,
    drop_minute integer,
    drop_second integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT drop_item.item_amount,
           EXTRACT(YEAR FROM drop_item.drop_time)::integer,
           EXTRACT(MONTH FROM drop_item.drop_time)::integer,
           EXTRACT(DAY FROM drop_item.drop_time)::integer,
           EXTRACT(HOUR FROM drop_item.drop_time)::integer,
           EXTRACT(MINUTE FROM drop_item.drop_time)::integer,
           EXTRACT(SECOND FROM drop_item.drop_time)::integer
    FROM "world"."br_drop_item_amount" AS drop_item
    WHERE drop_item.item_class_id = p_item_class_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_brsavedropitemamountperserver"(
    p_item_class_id integer,
    p_amount integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."br_drop_item_amount" AS drop_item
        WHERE drop_item.item_class_id = p_item_class_id
    ) THEN
        UPDATE "world"."br_drop_item_amount" AS drop_item
        SET item_amount = p_amount,
            drop_time = "world"."mssql_getdate"()
        WHERE drop_item.item_class_id = p_item_class_id;
    ELSE
        INSERT INTO "world"."br_drop_item_amount" (
            item_class_id,
            item_amount,
            drop_time
        ) VALUES (
            p_item_class_id,
            p_amount,
            "world"."mssql_getdate"()
        );
    END IF;
END;
$function$;

COMMIT;
