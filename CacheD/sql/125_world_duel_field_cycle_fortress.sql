-- Extended requests 91-100. Current Cached.exe and the paired current
-- L2Server.exe fix every callable ABI and packet order. Compatible modern
-- donors supply the unchanged relational bodies. Current machine code adds
-- the seventh FieldCycle value, mission_started_time.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

ALTER TABLE "world"."field_cycle"
    ADD COLUMN IF NOT EXISTS "mission_started_time"
        integer DEFAULT 0 NOT NULL;

CREATE OR REPLACE FUNCTION "world"."lin_updateitemduration"(
    p_item_id integer,
    p_duration integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."user_item_duration" (
        "item_id",
        "duration"
    ) VALUES (
        p_item_id,
        p_duration
    )
    ON CONFLICT ("item_id") DO UPDATE
    SET "duration" = EXCLUDED."duration";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_saveduelrecord"(
    p_user_id integer,
    p_individual_win integer,
    p_individual_lose integer,
    p_individual_draw integer,
    p_party_win integer,
    p_party_lose integer,
    p_party_draw integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."duel_record" (
        "user_id",
        "individual_win",
        "individual_lose",
        "individual_draw",
        "party_win",
        "party_lose",
        "party_draw"
    ) VALUES (
        p_user_id,
        p_individual_win,
        p_individual_lose,
        p_individual_draw,
        p_party_win,
        p_party_lose,
        p_party_draw
    )
    ON CONFLICT ("user_id") DO UPDATE
    SET "individual_win" = EXCLUDED."individual_win",
        "individual_lose" = EXCLUDED."individual_lose",
        "individual_draw" = EXCLUDED."individual_draw",
        "party_win" = EXCLUDED."party_win",
        "party_lose" = EXCLUDED."party_lose",
        "party_draw" = EXCLUDED."party_draw";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadduelrecord"(
    p_user_id integer
) RETURNS TABLE(
    individual_win integer,
    individual_lose integer,
    individual_draw integer,
    party_win integer,
    party_lose integer,
    party_draw integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT duel."individual_win",
           duel."individual_lose",
           duel."individual_draw",
           duel."party_win",
           duel."party_lose",
           duel."party_draw"
    FROM "world"."duel_record" AS duel
    WHERE duel."user_id" = p_user_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getolympiadmatchresult"(
    p_char_id integer,
    p_season integer
) RETURNS TABLE(
    match_time integer,
    char_name text,
    is_winner integer,
    elapsed_time integer,
    subjob0_class integer,
    game_rule integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT match."match_time",
           rival."char_name"::text,
           match."is_winner"::integer,
           match."elapsed_time",
           rival."subjob0_class",
           match."game_rule"
    FROM "world"."olympiad_match" AS match
    JOIN "world"."user_data" AS rival
      ON rival."char_id" = match."rival_id"
    WHERE match."char_id" = p_char_id
      AND match."season" = p_season
      AND match."is_winner" <> 2
    ORDER BY match."game_rule", match."match_time";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savefieldcycleinfo"(
    p_field_id integer,
    p_point integer,
    p_step integer,
    p_step_changed_time integer,
    p_point_changed_time integer,
    p_accumulated_point integer,
    p_mission_started_time integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."field_cycle" (
        "field_id",
        "point",
        "step",
        "step_changed_time",
        "point_changed_time",
        "accumulated_point",
        "mission_started_time"
    ) VALUES (
        p_field_id,
        p_point,
        p_step,
        p_step_changed_time,
        p_point_changed_time,
        p_accumulated_point,
        p_mission_started_time
    )
    ON CONFLICT ("field_id") DO UPDATE
    SET "point" = EXCLUDED."point",
        "step" = EXCLUDED."step",
        "step_changed_time" = EXCLUDED."step_changed_time",
        "point_changed_time" = EXCLUDED."point_changed_time",
        "accumulated_point" = EXCLUDED."accumulated_point",
        "mission_started_time" = EXCLUDED."mission_started_time";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadfieldcycleinfo"(
    p_field_id integer
) RETURNS TABLE(
    field_id integer,
    point integer,
    step integer,
    step_changed_time integer,
    point_changed_time integer,
    accumulated_point integer,
    mission_started_time integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT cycle."field_id",
           cycle."point",
           cycle."step",
           cycle."step_changed_time",
           cycle."point_changed_time",
           cycle."accumulated_point",
           cycle."mission_started_time"
    FROM "world"."field_cycle" AS cycle
    WHERE p_field_id = -1
       OR cycle."field_id" = p_field_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_createfortress"(
    p_id integer,
    p_name text
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."fortress" (
        "id",
        "name"
    ) VALUES (
        p_id,
        p_name
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadfortress"(
    p_id integer
) RETURNS TABLE(
    state integer,
    state_changed_time integer,
    state_elapsed_time integer,
    pledge_id integer,
    contract_status integer,
    parent_castle_id integer,
    last_owner_changed_time integer,
    last_reward_given_time integer,
    owner_reward_cycle_count integer,
    castle_treasure_level integer,
    owner_siege_defend_count integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT fortress."state",
           fortress."state_changed_time",
           fortress."state_elapsed_time",
           fortress."pledge_id",
           fortress."contract_status",
           fortress."parent_castle_id",
           fortress."last_owner_changed_time",
           fortress."last_reward_given_time",
           fortress."owner_reward_cycle_count",
           fortress."castle_treasure_level",
           fortress."owner_siege_defend_count"
    FROM "world"."fortress" AS fortress
    WHERE fortress."id" = p_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadfortressfacility"(
    p_fortress_id integer
) RETURNS TABLE(
    facility_type integer,
    facility_level integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT facility."facility_type",
           facility."facility_level"
    FROM "world"."fortress_facility" AS facility
    WHERE facility."fortress_id" = p_fortress_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadfortresssiege"(
    p_fortress_id integer
) RETURNS TABLE(
    siege_status integer,
    siege_start_time integer,
    siege_elapsed_time integer,
    is_valid_info integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT siege."siege_status",
           siege."siege_start_time",
           siege."siege_elapsed_time",
           siege."is_valid_info"
    FROM "world"."fortress_siege" AS siege
    WHERE siege."fortress_id" = p_fortress_id;
$function$;

COMMIT;
