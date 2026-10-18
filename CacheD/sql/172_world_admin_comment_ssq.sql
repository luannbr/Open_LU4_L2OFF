-- Current Cached.exe AQ opcodes 33 and 85.
--
-- The current image fixes both callable ABIs, the two-input permutation for
-- lin_CommentDelete and the twelve-column lin_GetSSQStatus result order.
-- Compatible Classic 3.0, modified-287 and Ertheia bodies agree on the
-- relational effects and Seven Signs calculation.

BEGIN;

CREATE OR REPLACE FUNCTION "world"."lin_commentdelete"(
    p_delete integer,
    p_comment_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF p_delete = 1 THEN
        UPDATE "world"."user_comment" AS user_comment
        SET "deleted" = 1
        WHERE user_comment."comment_id" = p_comment_id;
    ELSIF p_delete = 2 THEN
        UPDATE "world"."user_comment" AS user_comment
        SET "deleted" = 0
        WHERE user_comment."comment_id" = p_comment_id;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getssqstatus"()
RETURNS TABLE (
    round_number integer,
    twilight_point integer,
    dawn_point integer,
    seal1 integer,
    seal2 integer,
    seal3 integer,
    seal4 integer,
    seal5 integer,
    seal6 integer,
    seal7 integer,
    epoch_seconds integer,
    status integer
)
LANGUAGE sql
STABLE
AS $function$
    WITH current_round AS (
        SELECT max(data."round_number") AS "round_number"
        FROM "world"."ssq_data" AS data
    ),
    collected_by_type AS (
        SELECT
            CASE
                WHEN joined."type" = 1
                    THEN sum(joined."collected_point")
                ELSE 0
            END AS "twilight_point",
            CASE
                WHEN joined."type" = 2
                    THEN sum(joined."collected_point")
                ELSE 0
            END AS "dawn_point"
        FROM "world"."ssq_join_data" AS joined
        WHERE joined."round_number" =
            (SELECT "round_number" FROM current_round)
        GROUP BY joined."type"
    ),
    collected_totals AS (
        SELECT
            sum(points."twilight_point") AS "twilight_point",
            sum(points."dawn_point") AS "dawn_point"
        FROM collected_by_type AS points
    ),
    collected AS (
        SELECT
            CASE
                WHEN totals."dawn_point" + totals."twilight_point" = 0
                    THEN 0
                ELSE trunc(
                    totals."twilight_point"::double precision /
                    (totals."dawn_point" + totals."twilight_point") *
                    500.0
                )::integer
            END AS "twilight_point",
            CASE
                WHEN totals."dawn_point" + totals."twilight_point" = 0
                    THEN 0
                ELSE trunc(
                    totals."dawn_point"::double precision /
                    (totals."dawn_point" + totals."twilight_point") *
                    500.0
                )::integer
            END AS "dawn_point"
        FROM collected_totals AS totals
    ),
    signed_room_points AS (
        SELECT
            record."room_no",
            sum(
                CASE
                    WHEN record."ssq_part" = 1 THEN record."point"
                    WHEN record."ssq_part" = 2 THEN -record."point"
                    ELSE 0
                END
            ) AS "point"
        FROM "world"."time_attack_record" AS record
        WHERE record."record_type" > 0
          AND record."ssq_round" =
              (SELECT "round_number" FROM current_round)
        GROUP BY record."room_no"
    ),
    room_winners AS (
        SELECT
            CASE
                WHEN room."point" > 0 THEN 1
                WHEN room."point" < 0 THEN 2
                ELSE 0
            END AS "ssq_part",
            CASE room."room_no"
                WHEN 1 THEN 60
                WHEN 2 THEN 70
                WHEN 3 THEN 100
                WHEN 4 THEN 120
                WHEN 5 THEN 150
                ELSE 0
            END AS "point"
        FROM signed_room_points AS room
    ),
    main_by_part AS (
        SELECT
            CASE
                WHEN winner."ssq_part" = 1 THEN sum(winner."point")
                ELSE 0
            END AS "twilight_point",
            CASE
                WHEN winner."ssq_part" = 2 THEN sum(winner."point")
                ELSE 0
            END AS "dawn_point"
        FROM room_winners AS winner
        GROUP BY winner."ssq_part"
    ),
    main_totals AS (
        SELECT
            sum(points."twilight_point") AS "twilight_point",
            sum(points."dawn_point") AS "dawn_point"
        FROM main_by_part AS points
    )
    SELECT
        data."round_number",
        (main."twilight_point" +
            collected."twilight_point")::integer,
        (main."dawn_point" +
            collected."dawn_point")::integer,
        data."seal1"::integer,
        data."seal2"::integer,
        data."seal3"::integer,
        data."seal4"::integer,
        data."seal5"::integer,
        data."seal6"::integer,
        data."seal7"::integer,
        floor(extract(epoch FROM statement_timestamp()))::integer,
        data."status"::integer
    FROM current_round AS round_state
    JOIN "world"."ssq_data" AS data
      ON data."round_number" = round_state."round_number"
    CROSS JOIN collected
    CROSS JOIN main_totals AS main
    LIMIT 1;
$function$;

COMMIT;
