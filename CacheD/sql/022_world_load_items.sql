-- Current Cached.exe primary opcode 4 (LoadItemsPacket) database slice.
--
-- The current machine code fixes the function ABIs and packet column order.
-- The one-day reward field names and their order are kept
-- by the matching L2Server PDB (`ICDailyMission`) and its DB request packet:
-- DailyMissionDBID, UpdateCheckTime, AccumulateCount, StateType, TimeRewarded.
-- No compatible body for these four procedures survives in the available SQL
-- donors, so the two physical PostgreSQL tables below are portable storage.
-- their externally visible function contracts follow the current binaries.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE TABLE IF NOT EXISTS "world"."user_one_day_reward" (
    char_id integer NOT NULL,
    daily_mission_db_id integer NOT NULL,
    update_check_time integer NOT NULL DEFAULT 0,
    accumulate_count integer NOT NULL DEFAULT 1,
    state_type integer NOT NULL DEFAULT 0,
    time_rewarded integer NOT NULL DEFAULT 0,
    CONSTRAINT "pk_user_one_day_reward"
        PRIMARY KEY (char_id, daily_mission_db_id)
);

CREATE TABLE IF NOT EXISTS "world"."user_item_reuse_delay" (
    char_id integer NOT NULL,
    item_class_id integer NOT NULL,
    reuse_end_time integer NOT NULL,
    CONSTRAINT "pk_user_item_reuse_delay"
        PRIMARY KEY (char_id, item_class_id)
);

CREATE OR REPLACE FUNCTION "world"."lin_loadonedayreward"(
    p_char_id integer
) RETURNS TABLE (
    daily_mission_db_id integer,
    update_check_time integer,
    accumulate_count integer,
    state_type integer,
    time_rewarded integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT reward.daily_mission_db_id,
           reward.update_check_time,
           reward.accumulate_count,
           reward.state_type,
           reward.time_rewarded
    FROM "world"."user_one_day_reward" AS reward
    WHERE reward.char_id = p_char_id
    ORDER BY reward.daily_mission_db_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updateonedayrewardinfo"(
    p_char_id integer,
    p_update_type integer,
    p_daily_mission_db_id integer,
    p_time_rewarded integer,
    p_update_check_time integer,
    p_accumulate_count integer,
    p_state_type integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    INSERT INTO "world"."user_one_day_reward" AS current_reward (
        char_id,
        daily_mission_db_id,
        update_check_time,
        accumulate_count,
        state_type,
        time_rewarded
    ) VALUES (
        p_char_id,
        p_daily_mission_db_id,
        p_update_check_time,
        p_accumulate_count,
        p_state_type,
        p_time_rewarded
    )
    ON CONFLICT (char_id, daily_mission_db_id) DO UPDATE
    SET update_check_time = EXCLUDED.update_check_time,
        accumulate_count = EXCLUDED.accumulate_count,
        state_type = EXCLUDED.state_type,
        -- Current RequestUpdateOneDayRewardInfo synthesizes TimeRewarded
        -- only for REWARDED (1), and reads an explicit value for TEST (3).
        -- CHECKTIME (2) passes zero in that slot and must keep the prior
        -- reward timestamp.
        time_rewarded = CASE
            WHEN p_update_type IN (1, 3)
                THEN EXCLUDED.time_rewarded
            ELSE current_reward.time_rewarded
        END;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getitemreusedelay"(
    p_char_id integer,
    p_current_time integer
) RETURNS TABLE (
    item_class_id integer,
    reuse_end_time integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT delay.item_class_id,
           delay.reuse_end_time
    FROM "world"."user_item_reuse_delay" AS delay
    WHERE delay.char_id = p_char_id
      AND p_current_time <= delay.reuse_end_time
    ORDER BY delay.item_class_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setitemreusedelay"(
    p_char_id integer,
    p_item_class_id integer,
    p_reuse_end_time integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."user_item_reuse_delay" (
        char_id, item_class_id, reuse_end_time
    ) VALUES (
        p_char_id, p_item_class_id, p_reuse_end_time
    )
    ON CONFLICT (char_id, item_class_id) DO UPDATE
    SET reuse_end_time = EXCLUDED.reuse_end_time;
$function$;

COMMIT;
