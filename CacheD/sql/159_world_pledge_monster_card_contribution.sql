-- Current Cached.exe extended slots 431-440: pledge-bonus delta update,
-- monster-book load/save, CardUpDown load/save and pledge-contribution member
-- info. User pledge-reward load/save already has its exact callable ABI in
-- migration 019. Upgrade-system and pledge-item manipulation reuse the
-- recovered item/EtcMani persistence owners and add no direct statement.
--
-- Current machine code fixes every function name, input type/order and result
-- type/order below. No supplied database dump keeps these newer bodies, so
-- the three added relations and their effects are ABI-constrained
-- PostgreSQL persistence models instead of claims about unavailable MSSQL
-- source. Existing pledge_bonus and contribution relations keep their
-- recovered shapes from migrations 158, 027 and 116.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_updatepledgebonus"(
    p_pledge_id integer,
    p_bonus_index integer,
    p_added_point integer,
    p_max_bonus_point integer,
    p_last_save_time integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."pledge_bonus" AS bonus
       SET "current_bonus_point" =
               bonus."current_bonus_point" + p_added_point,
           "max_bonus_point" = p_max_bonus_point,
           "last_save_time" = p_last_save_time
     WHERE bonus."pledge_id" = p_pledge_id
       AND bonus."bonus_index" = p_bonus_index
$function$;

CREATE TABLE IF NOT EXISTS "world"."user_monster_book" (
    "char_id" integer NOT NULL,
    "card_id" smallint NOT NULL,
    "trophy_level" smallint NOT NULL,
    "kill_count" integer NOT NULL,
    CONSTRAINT "pk_user_monster_book"
        PRIMARY KEY ("char_id", "card_id")
);

CREATE OR REPLACE FUNCTION "world"."lin_loadusermonsterbook"(
    p_char_id integer
) RETURNS TABLE (
    card_id integer,
    trophy_level integer,
    kill_count integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT book."card_id"::integer,
           book."trophy_level"::integer,
           book."kill_count"
      FROM "world"."user_monster_book" AS book
     WHERE book."char_id" = p_char_id
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updateusermonsterbook"(
    p_char_id integer,
    p_card_id smallint,
    p_trophy_level smallint,
    p_kill_count integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."user_monster_book" AS book (
        "char_id", "card_id", "trophy_level", "kill_count"
    ) VALUES (
        p_char_id, p_card_id, p_trophy_level, p_kill_count
    )
    ON CONFLICT ("char_id", "card_id") DO UPDATE
    SET "trophy_level" = EXCLUDED."trophy_level",
        "kill_count" = EXCLUDED."kill_count"
$function$;

CREATE TABLE IF NOT EXISTS "world"."card_updown_game" (
    "char_id" integer NOT NULL,
    "game_id" integer NOT NULL,
    "game_time" integer NOT NULL,
    "fixed_item_class_id" integer NOT NULL,
    "fixed_item_amount" bigint NOT NULL,
    "random_item_class_id" integer NOT NULL,
    "random_item_amount" bigint NOT NULL,
    CONSTRAINT "pk_card_updown_game" PRIMARY KEY ("char_id")
);

CREATE OR REPLACE FUNCTION "world"."lin_loadcardupdowngame"(
    p_char_id integer
) RETURNS TABLE (
    game_id integer,
    game_time integer,
    fixed_item_class_id integer,
    fixed_item_amount bigint,
    random_item_class_id integer,
    random_item_amount bigint
)
LANGUAGE sql
STABLE
AS $function$
    SELECT game."game_id",
           game."game_time",
           game."fixed_item_class_id",
           game."fixed_item_amount",
           game."random_item_class_id",
           game."random_item_amount"
      FROM "world"."card_updown_game" AS game
     WHERE game."char_id" = p_char_id

    UNION ALL

    -- Current Cached.exe RequestLoadCardUpdownGameRecord initializes all six
    -- output lanes to zero and requires one fetched row before replying to
    -- L2Server.  The original MSSQL procedure therefore supplies the empty
    -- game state for a character without a persisted record.  Preserve that
    -- contract at the PostgreSQL boundary. Otherwise UserGame
    -- remains permanently in GPS_LOADING and rejects every card-up/down
    -- command as game state 0.
    SELECT 0,
           0,
           0,
           0::bigint,
           0,
           0::bigint
     WHERE NOT EXISTS (
         SELECT 1
           FROM "world"."card_updown_game" AS game
          WHERE game."char_id" = p_char_id
     )
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatecardupdowngame"(
    p_char_id integer,
    p_game_id integer,
    p_game_time integer,
    p_fixed_item_class_id integer,
    p_fixed_item_amount bigint,
    p_random_item_class_id integer,
    p_random_item_amount bigint
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."card_updown_game" AS game (
        "char_id",
        "game_id",
        "game_time",
        "fixed_item_class_id",
        "fixed_item_amount",
        "random_item_class_id",
        "random_item_amount"
    ) VALUES (
        p_char_id,
        p_game_id,
        p_game_time,
        p_fixed_item_class_id,
        p_fixed_item_amount,
        p_random_item_class_id,
        p_random_item_amount
    )
    ON CONFLICT ("char_id") DO UPDATE
    SET "game_id" = EXCLUDED."game_id",
        "game_time" = EXCLUDED."game_time",
        "fixed_item_class_id" = EXCLUDED."fixed_item_class_id",
        "fixed_item_amount" = EXCLUDED."fixed_item_amount",
        "random_item_class_id" = EXCLUDED."random_item_class_id",
        "random_item_amount" = EXCLUDED."random_item_amount"
$function$;

CREATE OR REPLACE FUNCTION
"world"."lin_pledgecontributionmembergetinfo"(
    p_pledge_id integer,
    p_user_id integer
) RETURNS TABLE (
    total_point integer,
    period_point integer,
    rewarded_step integer,
    period_reward integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT contribution."total_point",
           contribution."period_point",
           COALESCE(reward."reward_step", 0),
           CASE
               WHEN COALESCE(reward."period_reward_taken", false)
               THEN 1
               ELSE 0
           END
      FROM "world"."pledge_contribution_member_history" AS contribution
      LEFT JOIN "world"."cached_portable_pledge_contribution_reward" AS reward
        ON reward."pledge_id" = contribution."pledge_id"
       AND reward."user_id" = contribution."user_id"
     WHERE contribution."pledge_id" = p_pledge_id
       AND contribution."user_id" = p_user_id
$function$;

COMMIT;
