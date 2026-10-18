-- Current Cached.exe pledge-contribution, pledge-raid, pledge-mission and
-- BossInfo persistence used by DX slots 94..115 and the already recovered
-- Shared EtcMani owners.
--
-- The three npc_boss bodies below are donor-confirmed: Classic 3.0, modified
-- 287 and Ertheia keep the same procedure ABI and state transitions, and
-- the current image fixes every parameter/result position.
--
-- No supplied database dump contains the newer contribution/raid/mission
-- procedure bodies.  Their callable ABIs are exact current-machine facts.
-- the cached_portable_* tables and relational effects are an explicitly
-- derived PostgreSQL persistence model, not claimed original MSSQL schema.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadnpcboss"(
    p_npc_db_name text
) RETURNS TABLE (
    alive integer,
    hp integer,
    mp integer,
    pos_x integer,
    pos_y integer,
    pos_z integer,
    time_low integer,
    time_high integer,
    i0 integer,
    npc_id integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT boss."alive",
           boss."hp",
           boss."mp",
           boss."pos_x",
           boss."pos_y",
           boss."pos_z",
           boss."time_low",
           boss."time_high",
           boss."i0",
           boss."npc_id"
    FROM "world"."npc_boss" AS boss
    WHERE "world"."mssql_text_eq"(boss."npc_db_name", p_npc_db_name);
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savenpcboss"(
    p_npc_db_name text,
    p_alive integer,
    p_hp integer,
    p_mp integer,
    p_pos_x integer,
    p_pos_y integer,
    p_pos_z integer,
    p_time_low integer,
    p_time_high integer,
    p_npc_id integer,
    p_killer_mpcc_master_name text,
    p_killer_mpcc_member_count integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_previous_alive integer;
    v_killer_name text := COALESCE(p_killer_mpcc_master_name, '');
    v_killer_count integer := COALESCE(p_killer_mpcc_member_count, 0);
BEGIN
    SELECT boss."alive"
      INTO v_previous_alive
      FROM "world"."npc_boss" AS boss
     WHERE "world"."mssql_text_eq"(boss."npc_db_name", p_npc_db_name);

    IF p_alive = 1 THEN
        INSERT INTO "world"."npc_boss" (
            "npc_db_name", "alive", "hp", "mp", "pos_x", "pos_y",
            "pos_z", "time_low", "time_high", "i0", "npc_id",
            "mpcc_master", "mpcc_members", "generated_time", "died_time"
        ) VALUES (
            p_npc_db_name, p_alive, p_hp, p_mp, p_pos_x, p_pos_y,
            p_pos_z, p_time_low, p_time_high, 0, p_npc_id,
            '', 0, NULL, NULL
        )
        ON CONFLICT ("npc_db_name") DO UPDATE
        SET "alive" = EXCLUDED."alive",
            "hp" = EXCLUDED."hp",
            "mp" = EXCLUDED."mp",
            "pos_x" = EXCLUDED."pos_x",
            "pos_y" = EXCLUDED."pos_y",
            "pos_z" = EXCLUDED."pos_z",
            "time_low" = EXCLUDED."time_low",
            "time_high" = EXCLUDED."time_high",
            "npc_id" = EXCLUDED."npc_id";

        IF v_previous_alive = 0 THEN
            UPDATE "world"."npc_boss"
               SET "generated_time" = "world"."mssql_getdate"()
             WHERE "world"."mssql_text_eq"("npc_db_name", p_npc_db_name);
        END IF;
    ELSE
        -- The donor procedure keeps the previous MPCC identity when the
        -- death packet carries no complete command-channel identity.
        IF "world"."mssql_text_eq"(v_killer_name, '')
           OR v_killer_count = 0 THEN
            SELECT COALESCE(boss."mpcc_master", ''),
                   COALESCE(boss."mpcc_members", 0)
              INTO v_killer_name, v_killer_count
              FROM "world"."npc_boss" AS boss
             WHERE "world"."mssql_text_eq"(
                 boss."npc_db_name",
                 p_npc_db_name
             );
        END IF;

        INSERT INTO "world"."npc_boss" (
            "npc_db_name", "alive", "hp", "mp", "pos_x", "pos_y",
            "pos_z", "time_low", "time_high", "i0", "npc_id",
            "mpcc_master", "mpcc_members", "generated_time", "died_time"
        ) VALUES (
            p_npc_db_name, p_alive, p_hp, p_mp, p_pos_x, p_pos_y,
            p_pos_z, p_time_low, p_time_high, 0, p_npc_id,
            COALESCE(v_killer_name, ''), COALESCE(v_killer_count, 0),
            NULL, NULL
        )
        ON CONFLICT ("npc_db_name") DO UPDATE
        SET "alive" = EXCLUDED."alive",
            "hp" = EXCLUDED."hp",
            "mp" = EXCLUDED."mp",
            "pos_x" = EXCLUDED."pos_x",
            "pos_y" = EXCLUDED."pos_y",
            "pos_z" = EXCLUDED."pos_z",
            "time_low" = EXCLUDED."time_low",
            "time_high" = EXCLUDED."time_high",
            "mpcc_master" = EXCLUDED."mpcc_master",
            "mpcc_members" = EXCLUDED."mpcc_members";

        IF v_previous_alive = 1 THEN
            UPDATE "world"."npc_boss"
               SET "died_time" = "world"."mssql_getdate"()
             WHERE "world"."mssql_text_eq"("npc_db_name", p_npc_db_name);
        END IF;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatenpcbossvariable"(
    p_npc_db_name text,
    p_i0 integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."npc_boss" AS boss
       SET "i0" = p_i0
     WHERE "world"."mssql_text_eq"(boss."npc_db_name", p_npc_db_name);
$function$;

CREATE TABLE IF NOT EXISTS
"world"."cached_portable_pledge_contribution_reward" (
    "pledge_id" integer NOT NULL,
    "user_id" integer NOT NULL,
    "reward_step" integer DEFAULT 0 NOT NULL,
    "period_reward_taken" boolean DEFAULT false NOT NULL,
    CONSTRAINT "pk_cached_portable_pledge_contribution_reward"
        PRIMARY KEY ("pledge_id", "user_id")
);

CREATE TABLE IF NOT EXISTS
"world"."cached_portable_pledge_contribution_rank_state" (
    "state_id" smallint NOT NULL DEFAULT 1,
    "season" integer DEFAULT 0 NOT NULL,
    "start_time" integer DEFAULT 0 NOT NULL,
    "minimum_point" integer DEFAULT 0 NOT NULL,
    CONSTRAINT "pk_cached_portable_pledge_contribution_rank_state"
        PRIMARY KEY ("state_id"),
    CONSTRAINT "ck_cached_portable_pledge_contribution_rank_state_id"
        CHECK ("state_id" = 1)
);

CREATE OR REPLACE FUNCTION
"world"."lin_pledgecontributionmemberaddpoint"(
    p_pledge_id integer,
    p_user_id integer,
    p_point integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    INSERT INTO "world"."pledge_contribution_member_history" AS stored (
        "pledge_id", "user_id", "user_name", "pledge_type",
        "period_rank", "period_ratio", "period_point", "total_point"
    )
    SELECT p_pledge_id,
           p_user_id,
           character."char_name",
           character."pledge_type",
           0,
           0,
           p_point,
           p_point
      FROM "world"."user_data" AS character
     WHERE character."char_id" = p_user_id
    ON CONFLICT ("pledge_id", "user_id") DO UPDATE
    SET "user_name" = EXCLUDED."user_name",
        "pledge_type" = EXCLUDED."pledge_type",
        "period_point" = stored."period_point" + EXCLUDED."period_point",
        "total_point" = stored."total_point" + EXCLUDED."total_point";
END;
$function$;

CREATE OR REPLACE FUNCTION
"world"."lin_pledgecontributionmembersavereward"(
    p_pledge_id integer,
    p_user_id integer,
    p_reward_step integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."cached_portable_pledge_contribution_reward" (
        "pledge_id", "user_id", "reward_step", "period_reward_taken"
    ) VALUES (
        p_pledge_id, p_user_id, p_reward_step, false
    )
    ON CONFLICT ("pledge_id", "user_id") DO UPDATE
    SET "reward_step" = EXCLUDED."reward_step",
        "period_reward_taken" = false;
$function$;

CREATE OR REPLACE FUNCTION
"world"."lin_pledgecontributionmembertakereward"(
    p_pledge_id integer,
    p_user_id integer,
    p_reward_step integer,
    p_minimum_point integer
) RETURNS TABLE (result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_total_point integer;
    v_reward_step integer;
BEGIN
    SELECT contribution."total_point"
      INTO v_total_point
      FROM "world"."pledge_contribution_member_history" AS contribution
     WHERE contribution."pledge_id" = p_pledge_id
       AND contribution."user_id" = p_user_id
     FOR UPDATE;
    IF NOT FOUND OR v_total_point < p_minimum_point THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END IF;

    INSERT INTO "world"."cached_portable_pledge_contribution_reward" (
        "pledge_id", "user_id", "reward_step"
    ) VALUES (p_pledge_id, p_user_id, 0)
    ON CONFLICT ("pledge_id", "user_id") DO NOTHING;

    SELECT reward."reward_step"
      INTO v_reward_step
      FROM "world"."cached_portable_pledge_contribution_reward" AS reward
     WHERE reward."pledge_id" = p_pledge_id
       AND reward."user_id" = p_user_id
     FOR UPDATE;
    -- Current L2Server.exe 0x00996d2c computes the next grade from
    -- rewardedStep + 1, but passes the unmodified rewardedStep to the
    -- CETC_MANI_PLEDGE_CONTRIBUTION_REWARD constructor at 0x00997053.
    -- CacheD therefore receives the previously committed step: accept only
    -- that exact compare-and-set predecessor and persist its successor.
    IF v_reward_step IS DISTINCT FROM p_reward_step THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END IF;

    UPDATE "world"."cached_portable_pledge_contribution_reward" AS reward
       SET "reward_step" = p_reward_step + 1
     WHERE reward."pledge_id" = p_pledge_id
       AND reward."user_id" = p_user_id;
    RETURN QUERY SELECT 1;
END;
$function$;

CREATE OR REPLACE FUNCTION
"world"."lin_pledgecontributionmembertakeperiodreward"(
    p_pledge_id integer,
    p_user_id integer
) RETURNS TABLE (
    accepted integer,
    result integer,
    period_rank integer,
    period_ratio integer,
    active_member integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_rank integer;
    v_ratio integer;
    v_period_point integer;
    v_minimum_point integer := 0;
    v_taken boolean;
    v_active_member integer := 0;
BEGIN
    SELECT contribution."period_rank",
           contribution."period_ratio",
           contribution."period_point"
      INTO v_rank, v_ratio, v_period_point
      FROM "world"."pledge_contribution_member_history" AS contribution
     WHERE contribution."pledge_id" = p_pledge_id
       AND contribution."user_id" = p_user_id
     FOR UPDATE;
    IF NOT FOUND THEN
        RETURN QUERY SELECT 0, 0, 0, 0, 0;
        RETURN;
    END IF;

    SELECT state."minimum_point"
      INTO v_minimum_point
      FROM "world"."cached_portable_pledge_contribution_rank_state" AS state
     WHERE state."state_id" = 1;
    v_minimum_point := COALESCE(v_minimum_point, 0);

    INSERT INTO "world"."cached_portable_pledge_contribution_reward" (
        "pledge_id", "user_id"
    ) VALUES (p_pledge_id, p_user_id)
    ON CONFLICT ("pledge_id", "user_id") DO NOTHING;
    SELECT reward."period_reward_taken"
      INTO v_taken
      FROM "world"."cached_portable_pledge_contribution_reward" AS reward
     WHERE reward."pledge_id" = p_pledge_id
       AND reward."user_id" = p_user_id
     FOR UPDATE;

    SELECT COUNT(*)::integer
      INTO v_active_member
      FROM "world"."pledge_contribution_member_history" AS contribution
     WHERE contribution."pledge_id" = p_pledge_id
       AND contribution."period_point" >= v_minimum_point;

    IF v_taken THEN
        RETURN QUERY
        SELECT 0, 2, v_rank, v_ratio, v_active_member;
    ELSIF v_period_point < v_minimum_point THEN
        RETURN QUERY
        SELECT 0, 1, v_rank, v_ratio, v_active_member;
    ELSE
        UPDATE "world"."cached_portable_pledge_contribution_reward" AS reward
           SET "period_reward_taken" = true
         WHERE reward."pledge_id" = p_pledge_id
           AND reward."user_id" = p_user_id;
        RETURN QUERY
        SELECT 1, 0, v_rank, v_ratio, v_active_member;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_pledgecontributionrankload"()
RETURNS TABLE (season integer, start_time integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT COALESCE((
               SELECT state."season"
                 FROM "world".
                      "cached_portable_pledge_contribution_rank_state" AS state
                WHERE state."state_id" = 1
           ), 0),
           COALESCE((
               SELECT state."start_time"
                 FROM "world".
                      "cached_portable_pledge_contribution_rank_state" AS state
                WHERE state."state_id" = 1
           ), 0);
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_pledgecontributionranksave"(
    p_season integer,
    p_start_time double precision
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."cached_portable_pledge_contribution_rank_state" (
        "state_id", "season", "start_time"
    ) VALUES (1, p_season, trunc(p_start_time)::integer)
    ON CONFLICT ("state_id") DO UPDATE
    SET "season" = EXCLUDED."season",
        "start_time" = EXCLUDED."start_time";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_pledgecontributionrankdecide"(
    p_start_time double precision,
    p_minimum_point integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    INSERT INTO "world"."cached_portable_pledge_contribution_rank_state" (
        "state_id", "start_time", "minimum_point"
    ) VALUES (1, trunc(p_start_time)::integer, p_minimum_point)
    ON CONFLICT ("state_id") DO UPDATE
    SET "start_time" = EXCLUDED."start_time",
        "minimum_point" = EXCLUDED."minimum_point";

    WITH ranked AS (
        SELECT contribution."pledge_id",
               contribution."user_id",
               rank() OVER (
                   PARTITION BY contribution."pledge_id"
                   ORDER BY contribution."period_point" DESC,
                            contribution."user_id"
               )::integer AS decided_rank,
               CASE
                   WHEN SUM(contribution."period_point") OVER (
                            PARTITION BY contribution."pledge_id"
                        ) <= 0 THEN 0
                   ELSE trunc(
                       contribution."period_point"::numeric * 100 /
                       SUM(contribution."period_point") OVER (
                           PARTITION BY contribution."pledge_id"
                       )
                   )::integer
               END AS decided_ratio
          FROM "world"."pledge_contribution_member_history" AS contribution
         WHERE contribution."period_point" >= p_minimum_point
    )
    UPDATE "world"."pledge_contribution_member_history" AS contribution
       SET "period_rank" = ranked.decided_rank,
           "period_ratio" = ranked.decided_ratio
      FROM ranked
     WHERE contribution."pledge_id" = ranked."pledge_id"
       AND contribution."user_id" = ranked."user_id";
END;
$function$;

CREATE TABLE IF NOT EXISTS "world"."cached_portable_pledge_raid_season" (
    "season" integer NOT NULL,
    "start_time" integer NOT NULL,
    CONSTRAINT "pk_cached_portable_pledge_raid_season"
        PRIMARY KEY ("season"),
    CONSTRAINT "uq_cached_portable_pledge_raid_season_start"
        UNIQUE ("start_time")
);

CREATE TABLE IF NOT EXISTS "world"."cached_portable_pledge_raid_info" (
    "season" integer NOT NULL,
    "world_id" integer NOT NULL,
    "pledge_db_id" integer NOT NULL,
    "pledge_name" text NOT NULL,
    "save_time" integer NOT NULL,
    "point" bigint NOT NULL,
    "ranking" integer DEFAULT 0 NOT NULL,
    "reward_reason" integer DEFAULT 0 NOT NULL,
    CONSTRAINT "pk_cached_portable_pledge_raid_info"
        PRIMARY KEY ("season", "world_id", "pledge_db_id")
);

CREATE OR REPLACE FUNCTION "world"."lin_pledgeraidload"()
RETURNS TABLE (season integer, start_time integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT state."season", state."start_time"
      FROM "world"."cached_portable_pledge_raid_season" AS state
     ORDER BY state."season" DESC
     LIMIT 2;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_pledgeraidsave"(
    p_season integer,
    p_start_time double precision
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."cached_portable_pledge_raid_season" (
        "season", "start_time"
    ) VALUES (p_season, trunc(p_start_time)::integer)
    ON CONFLICT ("season") DO UPDATE
    SET "start_time" = EXCLUDED."start_time";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_pledgeraidnextseason"(
    p_start_time double precision
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_start_time integer := trunc(p_start_time)::integer;
BEGIN
    IF NOT EXISTS (
        SELECT 1
          FROM "world"."cached_portable_pledge_raid_season" AS state
         WHERE state."start_time" = v_start_time
    ) THEN
        INSERT INTO "world"."cached_portable_pledge_raid_season" (
            "season", "start_time"
        )
        SELECT COALESCE(MAX(state."season"), 0) + 1,
               v_start_time
          FROM "world"."cached_portable_pledge_raid_season" AS state;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_pledgeraidresetseason"()
RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    DELETE FROM "world"."cached_portable_pledge_raid_info";
    DELETE FROM "world"."cached_portable_pledge_raid_season";
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_pledgeraidgettotalcount"(
    p_season integer
) RETURNS TABLE (total_count integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT COUNT(*)::integer
      FROM "world"."cached_portable_pledge_raid_info" AS info
     WHERE info."season" = p_season;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_pledgeraidgetinfo"(
    p_season integer,
    p_world_id integer,
    p_pledge_db_id integer
) RETURNS TABLE (
    world_id integer,
    pledge_db_id integer,
    pledge_name text,
    ranking integer,
    point bigint,
    reward_reason integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT info."world_id",
           info."pledge_db_id",
           info."pledge_name",
           info."ranking",
           info."point",
           info."reward_reason"
      FROM "world"."cached_portable_pledge_raid_info" AS info
     WHERE info."season" = p_season
       AND info."world_id" = p_world_id
       AND info."pledge_db_id" = p_pledge_db_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_pledgeraidsetinfo"(
    p_season integer,
    p_world_id integer,
    p_pledge_db_id integer,
    p_pledge_name text,
    p_point bigint,
    p_save_time double precision
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    INSERT INTO "world"."cached_portable_pledge_raid_info" (
        "season", "world_id", "pledge_db_id", "pledge_name",
        "save_time", "point"
    ) VALUES (
        p_season, p_world_id, p_pledge_db_id, p_pledge_name,
        trunc(p_save_time)::integer, p_point
    )
    ON CONFLICT ("season", "world_id", "pledge_db_id") DO UPDATE
    SET "pledge_name" = EXCLUDED."pledge_name",
        "save_time" = EXCLUDED."save_time",
        "point" = EXCLUDED."point";

    WITH ranked AS (
        SELECT info."world_id",
               info."pledge_db_id",
               rank() OVER (
                   ORDER BY info."point" DESC,
                            info."save_time",
                            info."world_id",
                            info."pledge_db_id"
               )::integer AS decided_rank
          FROM "world"."cached_portable_pledge_raid_info" AS info
         WHERE info."season" = p_season
    )
    UPDATE "world"."cached_portable_pledge_raid_info" AS info
       SET "ranking" = ranked.decided_rank
      FROM ranked
     WHERE info."season" = p_season
       AND info."world_id" = ranked."world_id"
       AND info."pledge_db_id" = ranked."pledge_db_id";
END;
$function$;

CREATE TABLE IF NOT EXISTS "world"."cached_portable_pledge_raid_inzone" (
    "pledge_db_id" integer NOT NULL,
    "restrict_time" integer NOT NULL,
    CONSTRAINT "pk_cached_portable_pledge_raid_inzone"
        PRIMARY KEY ("pledge_db_id")
);

CREATE OR REPLACE FUNCTION "world"."lin_pledgeraidinzonesave"(
    p_pledge_db_id integer,
    p_restrict_time double precision
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."cached_portable_pledge_raid_inzone" (
        "pledge_db_id", "restrict_time"
    ) VALUES (p_pledge_db_id, trunc(p_restrict_time)::integer)
    ON CONFLICT ("pledge_db_id") DO UPDATE
    SET "restrict_time" = EXCLUDED."restrict_time";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_pledgeraidinzonedelete"(
    p_pledge_db_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."cached_portable_pledge_raid_inzone" AS inzone
     WHERE inzone."pledge_db_id" = p_pledge_db_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_pledgeraidinzonereset"()
RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."cached_portable_pledge_raid_inzone";
$function$;

CREATE TABLE IF NOT EXISTS "world"."cached_portable_pledge_mission" (
    "user_db_id" integer NOT NULL,
    "mission_id" integer NOT NULL,
    "count" integer DEFAULT 0 NOT NULL,
    "reward_time" integer DEFAULT 0 NOT NULL,
    CONSTRAINT "pk_cached_portable_pledge_mission"
        PRIMARY KEY ("user_db_id", "mission_id")
);

CREATE OR REPLACE FUNCTION "world"."lin_pledgemissionsetcount"(
    p_user_db_id integer,
    p_mission_id integer,
    p_count integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."cached_portable_pledge_mission" AS stored (
        "user_db_id", "mission_id", "count"
    ) VALUES (p_user_db_id, p_mission_id, p_count)
    ON CONFLICT ("user_db_id", "mission_id") DO UPDATE
    SET "count" = EXCLUDED."count";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_pledgemissionaddcount"(
    p_user_db_id integer,
    p_mission_id integer,
    p_add_count integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."cached_portable_pledge_mission" AS stored (
        "user_db_id", "mission_id", "count"
    ) VALUES (p_user_db_id, p_mission_id, p_add_count)
    ON CONFLICT ("user_db_id", "mission_id") DO UPDATE
    SET "count" = stored."count" + EXCLUDED."count";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_pledgemissionload"(
    p_user_db_id integer
) RETURNS TABLE (
    mission_id integer,
    count integer,
    reward_time integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT mission."mission_id",
           mission."count",
           mission."reward_time"
      FROM "world"."cached_portable_pledge_mission" AS mission
     WHERE mission."user_db_id" = p_user_db_id
     ORDER BY mission."mission_id";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_pledgemissionsave"(
    p_user_db_id integer,
    p_mission_id integer,
    p_count integer,
    p_reward_time double precision
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."cached_portable_pledge_mission" (
        "user_db_id", "mission_id", "count", "reward_time"
    ) VALUES (
        p_user_db_id, p_mission_id, p_count,
        trunc(p_reward_time)::integer
    )
    ON CONFLICT ("user_db_id", "mission_id") DO UPDATE
    SET "count" = EXCLUDED."count",
        "reward_time" = EXCLUDED."reward_time";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_pledgemissionreward"(
    p_user_db_id integer,
    p_mission_id integer,
    p_count integer,
    p_reward_time double precision
) RETURNS TABLE (result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_count integer;
BEGIN
    SELECT mission."count"
      INTO v_count
      FROM "world"."cached_portable_pledge_mission" AS mission
     WHERE mission."user_db_id" = p_user_db_id
       AND mission."mission_id" = p_mission_id
     FOR UPDATE;
    IF NOT FOUND OR v_count < p_count THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END IF;

    UPDATE "world"."cached_portable_pledge_mission" AS mission
       SET "reward_time" = trunc(p_reward_time)::integer
     WHERE mission."user_db_id" = p_user_db_id
       AND mission."mission_id" = p_mission_id;
    RETURN QUERY SELECT 1;
END;
$function$;

CREATE TABLE IF NOT EXISTS
"world"."cached_portable_pledge_mission_week_count" (
    "user_db_id" integer NOT NULL,
    "count" integer DEFAULT 0 NOT NULL,
    "week_start_time" integer DEFAULT 0 NOT NULL,
    CONSTRAINT "pk_cached_portable_pledge_mission_week_count"
        PRIMARY KEY ("user_db_id")
);

CREATE OR REPLACE FUNCTION
"world"."lin_pledgemissioncountofoneweekload"(
    p_user_db_id integer
) RETURNS TABLE (count integer, week_start_time integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT weekly."count", weekly."week_start_time"
      FROM "world"."cached_portable_pledge_mission_week_count" AS weekly
     WHERE weekly."user_db_id" = p_user_db_id;
$function$;

CREATE OR REPLACE FUNCTION
"world"."lin_pledgemissioncountofoneweekadd"(
    p_user_db_id integer,
    p_add_count integer,
    p_week_start_time double precision
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."cached_portable_pledge_mission_week_count" AS stored (
        "user_db_id", "count", "week_start_time"
    ) VALUES (
        p_user_db_id, p_add_count, trunc(p_week_start_time)::integer
    )
    ON CONFLICT ("user_db_id") DO UPDATE
    SET "count" = stored."count" + EXCLUDED."count",
        "week_start_time" = EXCLUDED."week_start_time";
$function$;

CREATE OR REPLACE FUNCTION
"world"."lin_pledgemissioncountofoneweekreset"(
    p_user_db_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."cached_portable_pledge_mission_week_count" AS weekly
     WHERE weekly."user_db_id" = p_user_db_id;
$function$;

COMMIT;
