-- Current Cached.exe extended requests 301 through 310.
--
-- The executable fixes all ten wire contracts and the exact SQL call ABIs.
-- Classic 3.0, modified 287 and Ertheia keep matching relational bodies.
-- RequestLoadPledgeGame and the MPCC bookmark path reuse functions recovered
-- in migrations 113 and 072 respectively.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_startpledgegame"(
    p_season integer,
    p_season_start_time integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."pledge_game" (
        "season",
        "step",
        "season_start_time"
    ) VALUES (
        p_season,
        1,
        p_season_start_time
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_endpledgegame"(
    p_season integer,
    p_season_end_time integer,
    p_winner_pledge_id integer DEFAULT 0,
    p_winner_char_id integer DEFAULT 0,
    p_start_next_season integer DEFAULT 1
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_top_ranker_count integer := 0;
BEGIN
    IF p_winner_char_id = 0 THEN
        SELECT result."char_id"
        INTO p_winner_char_id
        FROM "world"."pledge_game_result" AS result
        WHERE result."season" = p_season
        ORDER BY result."prize_count" DESC
        LIMIT 1;

        SELECT character."pledge_id"
        INTO p_winner_pledge_id
        FROM "world"."user_data" AS character
        WHERE character."char_id" = p_winner_char_id;
    END IF;

    -- The donor suppresses a winner whenever the highest prize
    -- is tied.  Its scalar lookup also makes a missing winner count as zero.
    SELECT count(*)::integer
    INTO v_top_ranker_count
    FROM "world"."pledge_game_result" AS result
    WHERE result."season" = p_season
      AND result."prize_count" = (
          SELECT selected."prize_count"
          FROM "world"."pledge_game_result" AS selected
          WHERE selected."char_id" = p_winner_char_id
            AND selected."season" = p_season
      );

    IF v_top_ranker_count <> 1 THEN
        p_winner_char_id := 0;
        p_winner_pledge_id := 0;
    END IF;

    UPDATE "world"."pledge_game" AS game
    SET "step" = 2,
        "season_end_time" = p_season_end_time,
        "winner_pledge_id" = p_winner_pledge_id,
        "winner_char_id" = p_winner_char_id
    WHERE game."season" = p_season
      AND game."step" = 1;

    IF p_start_next_season = 1 THEN
        INSERT INTO "world"."pledge_game" (
            "season",
            "step",
            "season_start_time"
        ) VALUES (
            p_season + 1,
            1,
            p_season_end_time + 1
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatepledgegameresult"(
    p_season integer,
    p_char_id integer,
    p_prize_count integer
) RETURNS TABLE (
    prize_count integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_prize_count integer := 0;
BEGIN
    -- Preserve the shipping donor's UNION (not UNION ALL): when the stored
    -- value equals the increment, the returned total contains it only once.
    SELECT SUM(data."prize_count")::integer
    INTO v_prize_count
    FROM (
        SELECT result."prize_count"
        FROM "world"."pledge_game_result" AS result
        WHERE result."season" = p_season
          AND result."char_id" = p_char_id
        UNION
        SELECT p_prize_count
    ) AS data;

    UPDATE "world"."pledge_game_result" AS result
    SET "prize_count" = result."prize_count" + p_prize_count
    WHERE result."season" = p_season
      AND result."char_id" = p_char_id;

    IF NOT FOUND THEN
        INSERT INTO "world"."pledge_game_result" (
            "season",
            "char_id",
            "prize_count"
        ) VALUES (
            p_season,
            p_char_id,
            p_prize_count
        );
    END IF;

    RETURN QUERY SELECT v_prize_count;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatepledgegamegotitem"(
    p_season integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."pledge_game" AS game
    SET "winner_got_item" = 1
    WHERE game."season" = p_season;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_increasepledgegamesurrendercount"(
    p_season integer,
    p_char_id integer,
    p_increase_count integer DEFAULT 1
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."pledge_game_result" AS result
    SET "surrender_count" =
        result."surrender_count" + p_increase_count
    WHERE result."season" = p_season
      AND result."char_id" = p_char_id;

    IF NOT FOUND THEN
        INSERT INTO "world"."pledge_game_result" (
            "season",
            "char_id",
            "prize_count",
            "surrender_count"
        ) VALUES (
            p_season,
            p_char_id,
            0,
            p_increase_count
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getpledgegamerecord"(
    p_season integer,
    p_char_id integer
) RETURNS TABLE (
    prize_count integer,
    surrender_count integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        SUM(data."prize_count")::integer,
        SUM(data."surrender_count")::integer
    FROM (
        SELECT
            result."prize_count",
            result."surrender_count"
        FROM "world"."pledge_game_result" AS result
        WHERE result."season" = p_season
          AND result."char_id" = p_char_id
        UNION ALL
        SELECT 0, 0
    ) AS data;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setcastletype"(
    p_castle_id integer,
    p_castle_type integer DEFAULT 0
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."castle" AS castle
    SET "castle_type" = p_castle_type
    WHERE castle."id" = p_castle_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletemacroall"(
    p_char_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    DELETE FROM "world"."user_macroinfo" AS info
    WHERE info."macro_id" IN (
        SELECT macro."macro_id"
        FROM "world"."user_macro" AS macro
        WHERE macro."char_id" = p_char_id
    );

    DELETE FROM "world"."user_macro" AS macro
    WHERE macro."char_id" = p_char_id;
END;
$function$;

COMMIT;
