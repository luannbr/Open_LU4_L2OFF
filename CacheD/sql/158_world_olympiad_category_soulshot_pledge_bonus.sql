-- Current Cached.exe extended slots 421-430: World Olympiad match-count and
-- match-result reads, class-category publication, AutoSoulShotUi persistence
-- and pledge-bonus initialization/loading. Slots 421 and 423 are shipping
-- dummy handlers. One-day reward persistence reuses migration 022 and World
-- Olympiad matching-point load reuses migration 115.
--
-- Current machine code fixes every callable name and parameter/result order.
-- TotalChecker2015.pdb additionally fixes AutoSoulShotUi's four item/state
-- pairs and PledgeBonusInfo's four int32 fields. No supplied database image
-- keeps the newer AutoSoulShotUi or pledge-bonus bodies, so their two
-- relations below are limited to that proved state. The
-- supplied Classic 3.0 donor keeps both category procedures as true no-ops.
-- the portable functions keep that observable behavior without adding an
-- invented category table.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_wolympiadloadmatchcount"()
RETURNS TABLE (
    world_id integer,
    user_db_id integer,
    season integer,
    step integer,
    game_rule integer,
    match_count integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT count_data."world_id",
           count_data."user_db_id",
           count_data."season",
           count_data."step",
           count_data."game_rule",
           count_data."match_count"
    FROM "world"."world_olympiad_match_count" AS count_data
    ORDER BY count_data."world_id",
             count_data."user_db_id",
             count_data."season",
             count_data."step",
             count_data."game_rule"
$function$;

-- The current result ABI is match time, rival name, winner flag, elapsed
-- time, rival class and game rule. Migration 115 already stores both sides of
-- each match and the complete participant identity needed to project it.
CREATE OR REPLACE FUNCTION "world"."lin_wolympiadgetmatchresult"(
    p_world_id integer,
    p_user_db_id integer,
    p_season integer
) RETURNS TABLE (
    match_time integer,
    rival_name text,
    is_winner integer,
    elapsed_time integer,
    rival_class_type integer,
    game_rule integer
)
LANGUAGE sql
STABLE
AS $function$
    WITH match_result AS (
        SELECT bout."match_id",
               bout."match_time",
               COALESCE(rival."char_name", '') AS rival_name,
               1 AS is_winner,
               bout."elapsed_time",
               COALESCE(rival."class", 0) AS rival_class_type,
               bout."game_rule"
        FROM "world"."world_olympiad_match" AS bout
        LEFT JOIN "world"."world_olympiad_user" AS rival
          ON rival."world_id" = bout."loser_world_id"
         AND rival."user_db_id" = bout."loser_db_id"
        WHERE bout."season" = p_season
          AND bout."winner_world_id" = p_world_id
          AND bout."winner_db_id" = p_user_db_id

        UNION ALL

        SELECT bout."match_id",
               bout."match_time",
               COALESCE(rival."char_name", '') AS rival_name,
               0 AS is_winner,
               bout."elapsed_time",
               COALESCE(rival."class", 0) AS rival_class_type,
               bout."game_rule"
        FROM "world"."world_olympiad_match" AS bout
        LEFT JOIN "world"."world_olympiad_user" AS rival
          ON rival."world_id" = bout."winner_world_id"
         AND rival."user_db_id" = bout."winner_db_id"
        WHERE bout."season" = p_season
          AND bout."loser_world_id" = p_world_id
          AND bout."loser_db_id" = p_user_db_id
    )
    SELECT result."match_time",
           result."rival_name",
           result."is_winner",
           result."elapsed_time",
           result."rival_class_type",
           result."game_rule"
    FROM match_result AS result
    ORDER BY result."match_time" DESC, result."match_id" DESC
$function$;

-- Both supplied Classic 3.0 procedures contain only SET NOCOUNT ON. Their
-- three-input/no-input ABIs and absence of persistent effects are kept.
CREATE OR REPLACE FUNCTION "world"."lin_addclasscategorydb"(
    p_category_type integer,
    p_category_id integer,
    p_object_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    NULL;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deleteclasscategorydb"()
RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    NULL;
END;
$function$;

CREATE TABLE IF NOT EXISTS "world"."auto_soul_shot_ui" (
    "char_id" integer NOT NULL,
    "item_class_id_0" integer NOT NULL,
    "is_active_state_0" integer NOT NULL,
    "item_class_id_1" integer NOT NULL,
    "is_active_state_1" integer NOT NULL,
    "item_class_id_2" integer NOT NULL,
    "is_active_state_2" integer NOT NULL,
    "item_class_id_3" integer NOT NULL,
    "is_active_state_3" integer NOT NULL,
    CONSTRAINT "pk_auto_soul_shot_ui" PRIMARY KEY ("char_id")
);

CREATE OR REPLACE FUNCTION "world"."lin_loadautosoulshotui"(
    p_char_id integer
) RETURNS TABLE (
    item_class_id_0 integer,
    is_active_state_0 integer,
    item_class_id_1 integer,
    is_active_state_1 integer,
    item_class_id_2 integer,
    is_active_state_2 integer,
    item_class_id_3 integer,
    is_active_state_3 integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT soulshot."item_class_id_0",
           soulshot."is_active_state_0",
           soulshot."item_class_id_1",
           soulshot."is_active_state_1",
           soulshot."item_class_id_2",
           soulshot."is_active_state_2",
           soulshot."item_class_id_3",
           soulshot."is_active_state_3"
    FROM "world"."auto_soul_shot_ui" AS soulshot
    WHERE soulshot."char_id" = p_char_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_saveautosoulshotui"(
    p_char_id integer,
    p_item_class_id_0 integer,
    p_is_active_state_0 integer,
    p_item_class_id_1 integer,
    p_is_active_state_1 integer,
    p_item_class_id_2 integer,
    p_is_active_state_2 integer,
    p_item_class_id_3 integer,
    p_is_active_state_3 integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."auto_soul_shot_ui" (
        "char_id",
        "item_class_id_0", "is_active_state_0",
        "item_class_id_1", "is_active_state_1",
        "item_class_id_2", "is_active_state_2",
        "item_class_id_3", "is_active_state_3"
    ) VALUES (
        p_char_id,
        p_item_class_id_0, p_is_active_state_0,
        p_item_class_id_1, p_is_active_state_1,
        p_item_class_id_2, p_is_active_state_2,
        p_item_class_id_3, p_is_active_state_3
    )
    ON CONFLICT ("char_id") DO UPDATE
    SET "item_class_id_0" = EXCLUDED."item_class_id_0",
        "is_active_state_0" = EXCLUDED."is_active_state_0",
        "item_class_id_1" = EXCLUDED."item_class_id_1",
        "is_active_state_1" = EXCLUDED."is_active_state_1",
        "item_class_id_2" = EXCLUDED."item_class_id_2",
        "is_active_state_2" = EXCLUDED."is_active_state_2",
        "item_class_id_3" = EXCLUDED."item_class_id_3",
        "is_active_state_3" = EXCLUDED."is_active_state_3"
$function$;

CREATE TABLE IF NOT EXISTS "world"."pledge_bonus" (
    "pledge_id" integer NOT NULL,
    "bonus_index" integer NOT NULL,
    "current_bonus_point" integer NOT NULL,
    "max_bonus_point" integer NOT NULL,
    "last_save_time" integer NOT NULL,
    "today_reward_level" integer NOT NULL,
    CONSTRAINT "ck_pledge_bonus_index"
        CHECK ("bonus_index" BETWEEN 0 AND 1),
    CONSTRAINT "pk_pledge_bonus"
        PRIMARY KEY ("pledge_id", "bonus_index")
);

CREATE OR REPLACE FUNCTION "world"."lin_initpledgebonus"(
    p_pledge_id integer,
    p_bonus_index integer,
    p_current_bonus_point integer,
    p_max_bonus_point integer,
    p_last_save_time integer,
    p_today_reward_level integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."pledge_bonus" (
        "pledge_id",
        "bonus_index",
        "current_bonus_point",
        "max_bonus_point",
        "last_save_time",
        "today_reward_level"
    ) VALUES (
        p_pledge_id,
        p_bonus_index,
        p_current_bonus_point,
        p_max_bonus_point,
        p_last_save_time,
        p_today_reward_level
    )
    ON CONFLICT ("pledge_id", "bonus_index") DO UPDATE
    SET "current_bonus_point" = EXCLUDED."current_bonus_point",
        "max_bonus_point" = EXCLUDED."max_bonus_point",
        "last_save_time" = EXCLUDED."last_save_time",
        "today_reward_level" = EXCLUDED."today_reward_level"
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadpledgebonus"(
    p_pledge_id integer,
    p_bonus_index integer
) RETURNS TABLE (
    current_bonus_point integer,
    max_bonus_point integer,
    last_save_time integer,
    today_reward_level integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT bonus."current_bonus_point",
           bonus."max_bonus_point",
           bonus."last_save_time",
           bonus."today_reward_level"
    FROM "world"."pledge_bonus" AS bonus
    WHERE bonus."pledge_id" = p_pledge_id
      AND bonus."bonus_index" = p_bonus_index
$function$;

COMMIT;
