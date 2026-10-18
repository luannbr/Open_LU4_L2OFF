-- Current Cached.exe ND_PROTOCOL 0x05..0x12 persistence surface.
--
-- The current machine fixes every call name, input/result type and binding
-- order. Classic 3.0, modified 287 and Ertheia keep the same
-- relational bodies for all functions below. No inferred portable tables or
-- game rules are introduced here. Every relation is part of migration 001's
-- admitted world schema.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadlottogame"()
RETURNS TABLE (
    round_number integer,
    state integer,
    left_time integer,
    chosen_number_flag integer,
    rule_number integer,
    total_count integer,
    winner1_count integer,
    winner2_count integer,
    winner3_count integer,
    winner4_count integer,
    carried_adena bigint
)
LANGUAGE sql
STABLE
AS $function$
    SELECT game."round_number",
           game."state",
           game."left_time",
           game."chosen_number_flag",
           game."rule_number",
           game."total_count",
           game."winner1_count",
           game."winner2_count",
           game."winner3_count",
           game."winner4_count",
           game."carried_adena"
    FROM "world"."lotto_game" AS game
    ORDER BY game."round_number"
    LIMIT 65535;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadlottoitems"(
    p_round_number integer
) RETURNS TABLE (
    item_id bigint,
    number_flag integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT item."item_id"::bigint,
           item."number_flag"
    FROM "world"."lotto_items" AS item
    WHERE item."round_number" = p_round_number;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_createlottogame"(
    p_round_number integer,
    p_state integer,
    p_left_time integer,
    p_chosen_number_flag integer,
    p_rule_number integer,
    p_total_count integer,
    p_winner1_count integer,
    p_winner2_count integer,
    p_winner3_count integer,
    p_winner4_count integer,
    p_carried_adena bigint
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."lotto_game" (
        "round_number",
        "state",
        "left_time",
        "chosen_number_flag",
        "rule_number",
        "total_count",
        "winner1_count",
        "winner2_count",
        "winner3_count",
        "winner4_count",
        "carried_adena"
    ) VALUES (
        p_round_number,
        p_state,
        p_left_time,
        p_chosen_number_flag,
        p_rule_number,
        p_total_count,
        p_winner1_count,
        p_winner2_count,
        p_winner3_count,
        p_winner4_count,
        p_carried_adena
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatelottogame"(
    p_round_number integer,
    p_state integer,
    p_left_time integer,
    p_chosen_number_flag integer,
    p_rule_number integer,
    p_total_count integer,
    p_winner1_count integer,
    p_winner2_count integer,
    p_winner3_count integer,
    p_winner4_count integer,
    p_carried_adena bigint
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."lotto_game" AS game
    SET "state" = p_state,
        "left_time" = p_left_time,
        "chosen_number_flag" = p_chosen_number_flag,
        "rule_number" = p_rule_number,
        "total_count" = p_total_count,
        "winner1_count" = p_winner1_count,
        "winner2_count" = p_winner2_count,
        "winner3_count" = p_winner3_count,
        "winner4_count" = p_winner4_count,
        "carried_adena" = p_carried_adena
    WHERE game."round_number" = p_round_number;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savedbsavingmap"(
    p_map_key integer,
    p_map_value integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."dbsaving_map" (
        "map_key",
        "map_value"
    ) VALUES (
        p_map_key,
        p_map_value
    )
    ON CONFLICT ("map_key") DO UPDATE
    SET "map_value" = EXCLUDED."map_value";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loaddbsavingmap"(
    p_map_key integer
) RETURNS TABLE (map_value integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT saved."map_value"
    FROM "world"."dbsaving_map" AS saved
    WHERE saved."map_key" = p_map_key;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletedbsavingmap"(
    p_map_key integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."dbsaving_map" AS saved
    WHERE saved."map_key" = p_map_key;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_brgeteventrankerinfo"(
    p_event_id integer,
    p_character_id integer,
    p_score_limit integer
) RETURNS TABLE (
    adena_bonus integer,
    round_number integer,
    reward integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT COALESCE(top_player."adena_bonus", 0)::integer,
           COALESCE(top_player."round_no", 0)::integer,
           COALESCE(top_player."reward", 0)::integer
    FROM "world"."br_event_top_players" AS top_player
    WHERE top_player."event_id" = p_event_id
      AND top_player."char_id" = p_character_id
      AND p_score_limit <= top_player."score"
      AND top_player."reward" > 0
    ORDER BY top_player."reward"
    LIMIT 1;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_brdecreaseeventtoprankerreward"(
    p_event_id integer,
    p_round_number integer,
    p_character_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."br_event_top_players" AS top_player
    SET "reward" = 2
    WHERE top_player."event_id" = p_event_id
      AND top_player."round_no" = p_round_number
      AND top_player."char_id" = p_character_id
      AND top_player."reward" = 1;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_checkpremiumkamareward"(
    p_round_number integer,
    p_character_id integer
) RETURNS TABLE (
    ranking integer,
    check_flag integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT record."ranking"::integer,
           best."check_flag"::integer
    FROM "world"."premium_kamaloka_mybest" AS best
    JOIN "world"."premium_kamaloka_record" AS record
      ON best."party_no" = record."party_no"
    WHERE best."round_no" = p_round_number
      AND best."char_id" = p_character_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_receivepremiumkamareward"(
    p_round_number integer,
    p_character_id integer,
    p_reward_flag integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."premium_kamaloka_mybest" AS best
    SET "check_flag" = p_reward_flag::smallint
    WHERE best."round_no" = p_round_number
      AND best."char_id" = p_character_id;
$function$;

COMMIT;
