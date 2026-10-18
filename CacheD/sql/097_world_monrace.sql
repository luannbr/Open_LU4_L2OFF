-- Current Cached.exe primary opcode 188 (RequestMonrace) and
-- CWareHouse::DelMonRaceTicket persistence.
--
-- The current image fixes all callable names, input arities, input/result
-- binding widths and execution order. Classic 3.0, modified 287 and Ertheia
-- keep matching relational bodies for every procedure below. Current
-- Cached.exe remains authoritative where the later schema widened ticket
-- money fields to 64 bits.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_createmonrace"(
    p_lane_1 integer,
    p_lane_2 integer,
    p_lane_3 integer,
    p_lane_4 integer,
    p_lane_5 integer,
    p_lane_6 integer,
    p_lane_7 integer,
    p_lane_8 integer,
    p_tax_rate integer
) RETURNS TABLE (race_id integer)
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."monrace" (
        "lane1", "lane2", "lane3", "lane4",
        "lane5", "lane6", "lane7", "lane8", "tax_rate"
    ) VALUES (
        p_lane_1::smallint,
        p_lane_2::smallint,
        p_lane_3::smallint,
        p_lane_4::smallint,
        p_lane_5::smallint,
        p_lane_6::smallint,
        p_lane_7::smallint,
        p_lane_8::smallint,
        p_tax_rate
    )
    RETURNING "monrace"."race_id";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_createmonracemon"(
    p_runner_id integer,
    p_initial_win integer,
    p_run_count integer,
    p_win_count integer
) RETURNS TABLE (
    initial_win integer,
    run_count integer,
    win_count integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF NOT EXISTS (
        SELECT 1
        FROM "world"."monrace_mon" AS runner
        WHERE runner."runner_id" = p_runner_id
    ) THEN
        INSERT INTO "world"."monrace_mon" (
            "runner_id", "initial_win", "run_count", "win_count"
        ) VALUES (
            p_runner_id,
            p_initial_win::smallint,
            p_run_count,
            p_win_count
        );
    END IF;

    RETURN QUERY
    SELECT
        runner."initial_win"::integer,
        runner."run_count",
        runner."win_count"
    FROM "world"."monrace_mon" AS runner
    WHERE runner."runner_id" = p_runner_id;
END;
$function$;

-- MSSQL lin_GetMonRaceBet returns two columns for option 1 and three for
-- option 2. PostgreSQL has a fixed row type, so option 1 places its sum in
-- value_2 and leaves value_3 null. CacheD binds only the first two columns on
-- that path, exactly as it ignored any unbound ODBC result columns.
CREATE OR REPLACE FUNCTION "world"."lin_getmonracebet"(
    p_bet_type integer,
    p_race_id integer
) RETURNS TABLE (
    value_1 integer,
    value_2 integer,
    value_3 integer
)
LANGUAGE plpgsql
STABLE
AS $function$
BEGIN
    IF p_bet_type = 1 THEN
        RETURN QUERY
        SELECT
            ticket."bet_1"::integer,
            sum(ticket."bet_money")::integer,
            NULL::integer
        FROM "world"."monrace_ticket" AS ticket
        WHERE ticket."monraceid" = p_race_id
          AND ticket."bet_type" = 1
          AND ticket."deleted" = 0
        GROUP BY ticket."bet_1";
        RETURN;
    END IF;

    RETURN QUERY
    SELECT
        ticket."bet_1"::integer,
        ticket."bet_2"::integer,
        sum(ticket."bet_money")::integer
    FROM "world"."monrace_ticket" AS ticket
    WHERE ticket."monraceid" = p_race_id
      AND ticket."bet_type" = 2
      AND ticket."deleted" = 0
    GROUP BY ticket."bet_1", ticket."bet_2";
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getmonraceresult"(
    p_page integer,
    p_lines_per_page integer
) RETURNS TABLE (
    race_id integer,
    lane_1 integer,
    lane_2 integer,
    lane_3 integer,
    lane_4 integer,
    lane_5 integer,
    lane_6 integer,
    lane_7 integer,
    lane_8 integer,
    winner_1 integer,
    winner_2 integer,
    win_rate_1 double precision,
    win_rate_2 double precision,
    race_end integer
)
LANGUAGE plpgsql
STABLE
AS $function$
DECLARE
    v_page integer := p_page;
    v_max_race_id integer := 0;
BEGIN
    IF v_page <= 0 THEN
        v_page := 1;
    END IF;

    SELECT COALESCE(max(race."race_id"), 0)
    INTO v_max_race_id
    FROM "world"."monrace" AS race;

    RETURN QUERY
    SELECT
        race."race_id",
        race."lane1"::integer,
        race."lane2"::integer,
        race."lane3"::integer,
        race."lane4"::integer,
        race."lane5"::integer,
        race."lane6"::integer,
        race."lane7"::integer,
        race."lane8"::integer,
        race."win1"::integer,
        race."win2"::integer,
        race."winrate1",
        race."winrate2",
        race."race_end"::integer
    FROM "world"."monrace" AS race
    WHERE race."race_id" <=
              v_max_race_id - (v_page - 1) * p_lines_per_page
      AND race."race_id" >
              v_max_race_id - v_page * p_lines_per_page
    ORDER BY race."race_id" DESC;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getmonracetaxsum"(
    p_race_id integer
) RETURNS TABLE (tax_sum integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT COALESCE(sum(ticket."tax_money"), 0)::integer
    FROM "world"."monrace_ticket" AS ticket
    WHERE ticket."monraceid" = p_race_id
      AND ticket."deleted" = 0;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getmonraceticket"(
    p_item_id integer
) RETURNS TABLE (
    ticket_id integer,
    monrace_id integer,
    bet_type integer,
    bet_1 integer,
    bet_2 integer,
    bet_3 integer,
    bet_money bigint,
    win_rate_1 double precision,
    win_rate_2 double precision,
    winner_1 integer,
    winner_2 integer,
    race_end integer,
    tax_money bigint,
    remote_fee bigint
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        ticket."ticket_id",
        ticket."monraceid",
        ticket."bet_type"::integer,
        ticket."bet_1"::integer,
        ticket."bet_2"::integer,
        ticket."bet_3"::integer,
        ticket."bet_money",
        race."winrate1",
        race."winrate2",
        race."win1"::integer,
        race."win2"::integer,
        race."race_end"::integer,
        ticket."tax_money",
        ticket."remotefee"
    FROM (VALUES (1)) AS seed(value)
    LEFT JOIN LATERAL (
        SELECT source.*
        FROM "world"."monrace_ticket" AS source
        WHERE source."item_id" = p_item_id
          AND source."deleted" = 0
        LIMIT 1
    ) AS ticket ON true
    LEFT JOIN "world"."monrace" AS race
      ON race."race_id" = ticket."monraceid";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatemonrace"(
    p_race_id integer,
    p_win_rate_1 double precision,
    p_win_rate_2 double precision
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."monrace"
    SET
        "winrate1" = p_win_rate_1,
        "winrate2" = p_win_rate_2
    WHERE "race_id" = p_race_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatemonraceinfo"(
    p_race_id integer,
    p_run_1 double precision,
    p_run_2 double precision,
    p_run_3 double precision,
    p_run_4 double precision,
    p_run_5 double precision,
    p_run_6 double precision,
    p_run_7 double precision,
    p_run_8 double precision,
    p_winner_1 integer,
    p_winner_2 integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."monrace"
    SET
        "run1" = p_run_1,
        "run2" = p_run_2,
        "run3" = p_run_3,
        "run4" = p_run_4,
        "run5" = p_run_5,
        "run6" = p_run_6,
        "run7" = p_run_7,
        "run8" = p_run_8,
        "win1" = p_winner_1::smallint,
        "win2" = p_winner_2::smallint,
        "race_end" = 1
    WHERE "race_id" = p_race_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatemonracemon"(
    p_runner_id integer,
    p_win_rate integer,
    p_run_count integer,
    p_win_count integer
) RETURNS TABLE (
    initial_win integer,
    run_count integer,
    win_count integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."monrace_mon" AS runner
        WHERE runner."runner_id" = p_runner_id
    ) THEN
        UPDATE "world"."monrace_mon" AS runner
        SET
            "initial_win" = p_win_rate::smallint,
            "run_count" = p_run_count,
            "win_count" = p_win_count
        WHERE runner."runner_id" = p_runner_id;
    ELSE
        INSERT INTO "world"."monrace_mon" (
            "runner_id", "initial_win", "run_count", "win_count"
        ) VALUES (
            p_runner_id,
            p_win_rate::smallint,
            p_run_count,
            p_win_count
        );
    END IF;

    RETURN QUERY
    SELECT
        runner."initial_win"::integer,
        runner."run_count",
        runner."win_count"
    FROM "world"."monrace_mon" AS runner
    WHERE runner."runner_id" = p_runner_id;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_delmonraceticket"(
    p_item_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."monrace_ticket"
    SET "deleted" = 1
    WHERE "item_id" = p_item_id;
$function$;

COMMIT;
