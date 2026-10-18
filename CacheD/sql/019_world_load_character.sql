-- Current Cached.exe primary opcode 1 (LoadCharacterPacket) database slice.
--
-- Procedure names, input counts, result counts and result scalar families are
-- pinned to the current image's DBConn bind sites.  Relational conditions and
-- ordering come from the compatible modified-287 / Classic 3.0 procedures.
-- Where those donors predate the current ABI (skill sublevels, split vitality
-- reset dates, hlocked/use_count), the additional columns are the values bound
-- by the current machine code instead of guessed compatibility placeholders.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

-- These three relations were excluded from the earlier stable baseline
-- because not every donor export agreed byte-for-byte on their DDL. Each
-- current-observable shape is selected below.
CREATE TABLE "world"."user_subjob" (
    "char_id" integer NOT NULL,
    "hp" double precision,
    "mp" double precision,
    "sp" bigint,
    "exp" bigint,
    "level" smallint,
    "henna_1" integer,
    "henna_2" integer,
    "henna_3" integer,
    "subjob_id" integer NOT NULL,
    "create_date" timestamp without time zone,
    "max_level" integer NOT NULL,
    "pp" integer NOT NULL,
    CONSTRAINT "ck_user_subjob_level_tinyint"
        CHECK ("level" BETWEEN 0 AND 255),
    CONSTRAINT "pk_user_subjob" PRIMARY KEY ("char_id", "subjob_id")
);

CREATE TABLE "world"."user_henna" (
    "char_id" integer NOT NULL,
    "henna_1" integer NOT NULL,
    "henna_2" integer NOT NULL,
    "henna_3" integer NOT NULL,
    "subjob_id" integer,
    CONSTRAINT "pk_user_henna" PRIMARY KEY ("char_id")
);

CREATE TABLE "world"."user_skill" (
    "char_id" integer NOT NULL,
    "skill_id" integer NOT NULL,
    "skill_lev" smallint,
    "to_end_time" integer NOT NULL,
    "subjob_id" integer NOT NULL,
    "is_lock" smallint NOT NULL,
    "skill_delay" integer NOT NULL,
    "skill_sub_level" smallint NOT NULL,
    CONSTRAINT "ck_user_skill_is_lock_tinyint"
        CHECK ("is_lock" BETWEEN 0 AND 255),
    CONSTRAINT "pk_user_skill"
        PRIMARY KEY ("char_id", "skill_id", "subjob_id")
);

-- Current Cached.exe stores and loads one timestamp for each of activities
-- 0..2.  No compatible SQL body survived, but all three request handlers and
-- CUser::LoadUserPledgeBonus establish this complete key/value contract.
CREATE TABLE "world"."user_pledge_bonus" (
    "char_id" integer NOT NULL,
    "activity" integer NOT NULL,
    "received_time" integer NOT NULL,
    CONSTRAINT "ck_user_pledge_bonus_activity"
        CHECK ("activity" BETWEEN 0 AND 2),
    CONSTRAINT "pk_user_pledge_bonus" PRIMARY KEY ("char_id", "activity")
);

-- The donor active-skill and vitality tables are one revision behind the
-- current five- and four-result load ABIs respectively.  Retain the donor-
-- confirmed latest_reset_date column as the first reset clock. The current
-- ABI proves a second clock, but not a physical rename of the first column.
ALTER TABLE "world"."user_newactiveskill"
    ADD COLUMN "skill_sub_level" smallint DEFAULT 0 NOT NULL;

ALTER TABLE "world"."user_vitality"
    ADD COLUMN "latest_reset_date_rc" integer DEFAULT 0 NOT NULL;

CREATE OR REPLACE FUNCTION "world"."lin_loadaccount"(
    p_account_id integer
) RETURNS TABLE (
    char_id integer,
    account_name varchar(50)
)
LANGUAGE sql
STABLE
AS $function$
    SELECT character.char_id, character.account_name
    FROM "world"."user_data" AS character
    WHERE p_account_id > 0
      AND character.account_id = p_account_id
    LIMIT 10;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loaddayusedtime"(
    p_account_id integer
) RETURNS TABLE (
    used_seconds integer,
    last_logout varchar(19)
)
LANGUAGE sql
STABLE
AS $function$
    SELECT time_data.used_sec,
           to_char(time_data.last_logout, 'YYYY-MM-DD HH24:MI:SS')::varchar(19)
    FROM "world"."time_data" AS time_data
    WHERE time_data.account_id = p_account_id
    LIMIT 1;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadeventtime"(
    p_account_id integer
) RETURNS TABLE (
    event_time integer,
    event_type integer,
    event_variable integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT event.event_time, event.event_type, event.event_variable
    FROM "world"."user_event_time" AS event
    WHERE event.account_id = p_account_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadfriends"(
    p_char_id integer
) RETURNS TABLE (
    friend_char_id integer,
    friend_memo varchar(50)
)
LANGUAGE sql
STABLE
AS $function$
    SELECT friend.friend_char_id, friend.friend_memo
    FROM "world"."user_friend" AS friend
    WHERE friend.char_id = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletefriends"(
    p_char_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."user_friend" AS friend
    WHERE friend.char_id = p_char_id OR friend.friend_char_id = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_cleardeletedcharblocklist"(
    p_blocked_char_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."user_blocklist" AS block_entry
    WHERE block_entry.block_char_id = p_blocked_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deleteacademymember"(
    p_char_id integer,
    p_pledge_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."academy_member" AS member
    WHERE member.user_id = p_char_id AND member.pledge_id = p_pledge_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_resetacademymaster"(
    p_master_id integer,
    p_pledge_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."academy_member" AS member
    SET master_id = 0
    WHERE member.master_id = p_master_id AND member.pledge_id = p_pledge_id;
$function$;

-- Expired-character deletion can traverse premium items before copying them
-- to user_item_deleted.  These are the unchanged compatible donor effects.
-- their seven-argument surface is also present in the current image.
CREATE OR REPLACE FUNCTION "world"."lin_logforpremiumitemcreate"(
    p_account_id integer,
    p_char_id integer,
    p_item_dbid integer,
    p_item_amount_before integer,
    p_item_amount_after integer,
    p_reason integer,
    p_item_type integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."user_premium_item_log" (
        account_id, char_id, item_dbid, create_date, create_reason,
        item_amount_before, item_amount_after, item_type
    ) VALUES (
        p_account_id, p_char_id, p_item_dbid, "world"."mssql_getdate"(), p_reason,
        p_item_amount_before, p_item_amount_after, p_item_type
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_logforpremiumitemdelete"(
    p_account_id integer,
    p_char_id integer,
    p_item_dbid integer,
    p_item_amount_before integer,
    p_item_amount_after integer,
    p_reason integer,
    p_item_type integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."user_premium_item_log" AS item_log
    SET delete_date = "world"."mssql_getdate"(),
        delete_reason = p_reason
    WHERE item_log.item_dbid = p_item_dbid;

    IF NOT FOUND THEN
        INSERT INTO "world"."user_premium_item_log" (
            account_id, char_id, item_dbid, delete_date, delete_reason,
            item_amount_before, item_amount_after, item_type
        ) VALUES (
            p_account_id, p_char_id, p_item_dbid,
            "world"."mssql_getdate"(), p_reason,
            p_item_amount_before, p_item_amount_after, p_item_type
        );
    END IF;
END;
$function$;

-- Exact current CUserDbHelper::BindUserProperty result order: 128 columns.
-- The slot projection follows current procedure ABI order, not
-- the physical user_slot order (agathion/artifact precede deco/waist/jewels).
CREATE OR REPLACE FUNCTION "world"."lin_getuserdatabycharid"(
    p_char_id integer
) RETURNS TABLE (
    character_name varchar(50),
    char_id integer,
    account_name varchar(50),
    account_id integer,
    pledge_id integer,
    builder integer,
    gender integer,
    race integer,
    character_class integer,
    world_id integer,
    xloc integer,
    yloc integer,
    zloc integer,
    is_in_vehicle integer,
    hp double precision,
    mp double precision,
    max_hp double precision,
    max_mp double precision,
    cp double precision,
    max_cp double precision,
    sp bigint,
    experience bigint,
    character_level integer,
    highest_experience bigint,
    highest_level integer,
    alignment integer,
    pk_count integer,
    duel_count integer,
    pk_pardon integer,
    st_underwear integer,
    st_right_ear integer,
    st_left_ear integer,
    st_neck integer,
    st_right_finger integer,
    st_left_finger integer,
    st_head integer,
    st_right_hand integer,
    st_left_hand integer,
    st_gloves integer,
    st_chest integer,
    st_legs integer,
    st_feet integer,
    st_back integer,
    st_both_hand integer,
    st_hair integer,
    st_hair2 integer,
    st_right_bracelet integer,
    st_left_bracelet integer,
    st_agathion_main integer,
    st_agathion_sub1 integer,
    st_agathion_sub2 integer,
    st_agathion_sub3 integer,
    st_agathion_sub4 integer,
    st_artifact_book integer,
    st_artifact_a1 integer,
    st_artifact_a2 integer,
    st_artifact_a3 integer,
    st_artifact_a4 integer,
    st_artifact_a5 integer,
    st_artifact_a6 integer,
    st_artifact_a7 integer,
    st_artifact_a8 integer,
    st_artifact_a9 integer,
    st_artifact_a10 integer,
    st_artifact_a11 integer,
    st_artifact_a12 integer,
    st_artifact_b1 integer,
    st_artifact_b2 integer,
    st_artifact_b3 integer,
    st_artifact_c1 integer,
    st_artifact_c2 integer,
    st_artifact_c3 integer,
    st_artifact_d1 integer,
    st_artifact_d2 integer,
    st_artifact_d3 integer,
    st_deco1 integer,
    st_deco2 integer,
    st_deco3 integer,
    st_deco4 integer,
    st_deco5 integer,
    st_deco6 integer,
    st_waist integer,
    st_brooch integer,
    st_jewel1 integer,
    st_jewel2 integer,
    st_jewel3 integer,
    st_jewel4 integer,
    st_jewel5 integer,
    st_jewel6 integer,
    delete_year integer,
    delete_month integer,
    delete_day integer,
    delete_hour integer,
    delete_minute integer,
    delete_second integer,
    quest_flag bytea,
    face_index integer,
    hair_shape_index integer,
    hair_color_index integer,
    nickname varchar(50),
    power_flag bytea,
    pledge_dismiss_time integer,
    pledge_ousted_time integer,
    pledge_withdraw_time integer,
    surrender_war_id integer,
    use_time integer,
    drop_experience bigint,
    ban_status integer,
    ban_end integer,
    subjob_id integer,
    subjob0_class integer,
    subjob1_class integer,
    subjob2_class integer,
    subjob3_class integer,
    dual_class_id integer,
    ssq_dawn_round integer,
    character_name_color integer,
    nickname_color integer,
    pledge_type integer,
    grade_id integer,
    academy_pledge_id integer,
    service_flag integer,
    tutorial_flag bytea,
    associated_inzone integer,
    bookmark_slot integer,
    associated_inzone_uid integer,
    job_group integer,
    ability_points integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        rtrim(character.char_name)::varchar(50),
        character.char_id,
        rtrim(character.account_name)::varchar(50),
        character.account_id,
        character.pledge_id,
        character.builder::integer,
        character.gender::integer,
        character.race::integer,
        character.class::integer,
        character.world::integer,
        character.xloc,
        character.yloc,
        character.zloc,
        character.isinvehicle::integer,
        character.hp,
        character.mp,
        character.max_hp::double precision,
        character.max_mp::double precision,
        character.cp,
        character.max_cp,
        character.sp,
        character.exp,
        character.lev::integer,
        CASE
            WHEN character.subjob_id = COALESCE(highest.subjob_id, -1)
                THEN character.exp
            WHEN character.exp >= COALESCE(highest.exp, 0)
                THEN character.exp
            ELSE COALESCE(highest.exp, 0)
        END,
        CASE
            WHEN character.subjob_id = COALESCE(highest.subjob_id, -1)
                THEN character.lev::integer
            WHEN character.exp >= COALESCE(highest.exp, 0)
                THEN character.lev::integer
            ELSE COALESCE(highest.level, 0)
        END,
        character.align,
        character.pk,
        character.duel,
        character.pkpardon,
        COALESCE(slot.st_underwear, 0),
        COALESCE(slot.st_right_ear, 0),
        COALESCE(slot.st_left_ear, 0),
        COALESCE(slot.st_neck, 0),
        COALESCE(slot.st_right_finger, 0),
        COALESCE(slot.st_left_finger, 0),
        COALESCE(slot.st_head, 0),
        COALESCE(slot.st_right_hand, 0),
        COALESCE(slot.st_left_hand, 0),
        COALESCE(slot.st_gloves, 0),
        COALESCE(slot.st_chest, 0),
        COALESCE(slot.st_legs, 0),
        COALESCE(slot.st_feet, 0),
        COALESCE(slot.st_back, 0),
        COALESCE(slot.st_both_hand, 0),
        COALESCE(slot.st_hair, 0),
        COALESCE(slot.st_hair2, 0),
        COALESCE(slot.st_right_bracelet, 0),
        COALESCE(slot.st_left_bracelet, 0),
        COALESCE(slot.st_agathion_main, 0),
        COALESCE(slot.st_agathion_sub1, 0),
        COALESCE(slot.st_agathion_sub2, 0),
        COALESCE(slot.st_agathion_sub3, 0),
        COALESCE(slot.st_agathion_sub4, 0),
        COALESCE(slot.st_artifactbook, 0),
        COALESCE(slot.st_artifact_a1, 0),
        COALESCE(slot.st_artifact_a2, 0),
        COALESCE(slot.st_artifact_a3, 0),
        COALESCE(slot.st_artifact_a4, 0),
        COALESCE(slot.st_artifact_a5, 0),
        COALESCE(slot.st_artifact_a6, 0),
        COALESCE(slot.st_artifact_a7, 0),
        COALESCE(slot.st_artifact_a8, 0),
        COALESCE(slot.st_artifact_a9, 0),
        COALESCE(slot.st_artifact_a10, 0),
        COALESCE(slot.st_artifact_a11, 0),
        COALESCE(slot.st_artifact_a12, 0),
        COALESCE(slot.st_artifact_b1, 0),
        COALESCE(slot.st_artifact_b2, 0),
        COALESCE(slot.st_artifact_b3, 0),
        COALESCE(slot.st_artifact_c1, 0),
        COALESCE(slot.st_artifact_c2, 0),
        COALESCE(slot.st_artifact_c3, 0),
        COALESCE(slot.st_artifact_d1, 0),
        COALESCE(slot.st_artifact_d2, 0),
        COALESCE(slot.st_artifact_d3, 0),
        COALESCE(slot.st_deco1, 0),
        COALESCE(slot.st_deco2, 0),
        COALESCE(slot.st_deco3, 0),
        COALESCE(slot.st_deco4, 0),
        COALESCE(slot.st_deco5, 0),
        COALESCE(slot.st_deco6, 0),
        COALESCE(slot.st_waist, 0),
        COALESCE(slot.st_brooch, 0),
        COALESCE(slot.st_jewel1, 0),
        COALESCE(slot.st_jewel2, 0),
        COALESCE(slot.st_jewel3, 0),
        COALESCE(slot.st_jewel4, 0),
        COALESCE(slot.st_jewel5, 0),
        COALESCE(slot.st_jewel6, 0),
        COALESCE(extract(year FROM character.temp_delete_date), 0)::integer,
        COALESCE(extract(month FROM character.temp_delete_date), 0)::integer,
        COALESCE(extract(day FROM character.temp_delete_date), 0)::integer,
        COALESCE(extract(hour FROM character.temp_delete_date), 0)::integer,
        COALESCE(extract(minute FROM character.temp_delete_date), 0)::integer,
        COALESCE(extract(second FROM character.temp_delete_date), 0)::integer,
        character.quest_flag,
        character.face_index,
        character.hair_shape_index,
        character.hair_color_index,
        character.nickname,
        character.power_flag,
        character.pledge_dismiss_time,
        character.pledge_ousted_time,
        character.pledge_withdraw_time,
        character.surrender_war_id,
        character.use_time,
        character.drop_exp,
        COALESCE(ban.status, 0),
        COALESCE(ban.ban_end, 0),
        COALESCE(character.subjob_id, 0),
        character.subjob0_class,
        character.subjob1_class,
        character.subjob2_class,
        character.subjob3_class,
        COALESCE(character.dual_class_id, 0),
        COALESCE(character.ssq_dawn_round, 0),
        COALESCE(name_color.color_rgb, 16777215),
        COALESCE(name_color.nickname_color_rgb, 15530402),
        character.pledge_type,
        character.grade_id,
        COALESCE(character.academy_pledge_id, 0),
        COALESCE(service.service_flag, 0),
        COALESCE(
            character.tutorial_flag,
            decode(repeat('00', 128), 'hex')
        ),
        COALESCE(character.associated_inzone, 0),
        COALESCE(character.bookmark_slot, 0),
        COALESCE(character.associated_inzone_uid, 0)::integer,
        COALESCE(job.job_group, -1),
        COALESCE(character.pp, 0)
    FROM "world"."user_data" AS character
    LEFT JOIN "world"."user_ban" AS ban
      ON ban.char_id = character.char_id
    LEFT JOIN "world"."user_name_color" AS name_color
      ON name_color.char_id = character.char_id
    LEFT JOIN "world"."user_service" AS service
      ON service.char_id = character.char_id
    LEFT JOIN "world"."user_slot" AS slot
      ON slot.char_id = character.char_id
    LEFT JOIN "world"."user_job_group" AS job
      ON job.char_id = character.char_id
    LEFT JOIN LATERAL (
        SELECT subjob.subjob_id,
               subjob.level::integer AS level,
               subjob.exp
        FROM "world"."user_subjob" AS subjob
        WHERE subjob.char_id = p_char_id
        ORDER BY subjob.exp DESC
        LIMIT 1
    ) AS highest ON true
    WHERE p_char_id > 0
      AND character.char_id = p_char_id
      AND character.account_id > 0;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getuserdatacountbycharid"(
    p_char_id integer
) RETURNS TABLE (user_count integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT count(*)::integer
    FROM "world"."user_data" AS character
    WHERE character.char_id = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getaquireskill"(
    p_char_id integer,
    p_subjob_id integer
) RETURNS TABLE (
    skill_id integer,
    skill_level integer,
    skill_sub_level integer,
    is_lock integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT skill.skill_id,
           skill.skill_lev::integer,
           skill.skill_sub_level::integer,
           skill.is_lock::integer
    FROM "world"."user_skill" AS skill
    WHERE skill.char_id = p_char_id
      AND COALESCE(skill.subjob_id, 0) = p_subjob_id
    ORDER BY skill.skill_id, skill.skill_lev;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getacquirepotentialskill"(
    p_char_id integer,
    p_subjob_id integer
) RETURNS TABLE (
    skill_id integer,
    skill_level integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT skill.skill_id, skill.skill_lev
    FROM "world"."user_potential_skill" AS skill
    WHERE skill.user_charid = p_char_id
      AND COALESCE(skill.subjob, 0) = p_subjob_id
    ORDER BY skill.skill_id, skill.skill_lev;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getskillreusedelay"(
    p_char_id integer,
    p_current_time integer
) RETURNS TABLE (
    skill_id integer,
    subjob_id integer,
    to_end_time integer,
    skill_delay integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT delay.skill_id, delay.subjob_id,
           delay.to_end_time, delay.skill_delay
    FROM "world"."user_skill_reuse_delay" AS delay
    WHERE delay.char_id = p_char_id
      AND p_current_time <= delay.to_end_time
    ORDER BY delay.skill_id, delay.subjob_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loaduseractiveskill"(
    p_char_id integer
) RETURNS TABLE (
    skill_id integer,
    skill_level integer,
    skill_sub_level integer,
    skill_duration integer,
    skill_selfcast integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT active.skill_id,
           active.skill_level::integer,
           active.skill_sub_level::integer,
           active.skill_duration,
           active.skill_selfcast::integer
    FROM "world"."user_newactiveskill" AS active
    WHERE active.char_id = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadvotesystem"(
    p_char_id integer
) RETURNS TABLE (
    bonus_time_limit integer,
    play_time integer,
    vote_count integer,
    bonus_count integer,
    play_time_count integer,
    last_init_time varchar(19)
)
LANGUAGE sql
STABLE
AS $function$
    SELECT vote.bonustimelimit,
           vote.playtime,
           vote.votecount,
           vote.bonuscount,
           vote.playtimecount,
           to_char(vote.lastinittime, 'YYYY-MM-DD HH24:MI:SS')::varchar(19)
    FROM "world"."user_vote_system" AS vote
    WHERE vote.char_id = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadtrainingzoneendtime"(
    p_char_id integer
) RETURNS TABLE (max_end_time integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT training.max_end_time
    FROM "world"."trainingzone_state" AS training
    WHERE training.char_id = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadlastlogout"(
    p_char_id integer
) RETURNS TABLE (
    logout_year integer,
    logout_month integer,
    logout_day integer,
    logout_hour integer,
    logout_minute integer,
    logout_second integer,
    login_year integer,
    login_month integer,
    login_day integer,
    login_hour integer,
    login_minute integer,
    login_second integer,
    create_year integer,
    create_month integer,
    create_day integer,
    create_hour integer,
    create_minute integer,
    create_second integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        extract(year FROM character.logout)::integer,
        extract(month FROM character.logout)::integer,
        extract(day FROM character.logout)::integer,
        extract(hour FROM character.logout)::integer,
        extract(minute FROM character.logout)::integer,
        extract(second FROM character.logout)::integer,
        extract(year FROM character.login)::integer,
        extract(month FROM character.login)::integer,
        extract(day FROM character.login)::integer,
        extract(hour FROM character.login)::integer,
        extract(minute FROM character.login)::integer,
        extract(second FROM character.login)::integer,
        extract(year FROM character.create_date)::integer,
        extract(month FROM character.create_date)::integer,
        extract(day FROM character.create_date)::integer,
        extract(hour FROM character.create_date)::integer,
        extract(minute FROM character.create_date)::integer,
        extract(second FROM character.create_date)::integer
    FROM "world"."user_data" AS character
    WHERE character.char_id = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadtimetogivekarmapoint"(
    p_char_id integer
) RETURNS TABLE (time_to_give_karma_point integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT COALESCE(character.time_to_give_karma_point, 0)
    FROM "world"."user_data" AS character
    WHERE character.char_id = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_selectuserquest"(
    p_char_id integer,
    p_type integer DEFAULT 0,
    p_quest_no integer DEFAULT 0
) RETURNS TABLE (
    quest_no integer,
    journal integer,
    state1 integer,
    state2 integer,
    state3 integer,
    state4 integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT quest.quest_no, quest.journal,
           quest.state1, quest.state2, quest.state3, quest.state4
    FROM "world"."user_quest" AS quest
    WHERE quest.char_id = p_char_id
      AND (
          (p_quest_no = 0 AND quest.type::integer = p_type)
          OR (p_quest_no <> 0 AND quest.quest_no = p_quest_no)
      )
    ORDER BY quest.quest_no;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_selectusernpclog"(
    p_char_id integer,
    p_quest_no integer DEFAULT -1,
    p_npc_id integer DEFAULT -1
) RETURNS TABLE (
    quest_no integer,
    npc_id integer,
    log_type integer,
    log_count integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT log.quest_no, log.npc_id, log.type::integer, log.count
    FROM "world"."user_npc_log" AS log
    WHERE log.char_id = p_char_id
      AND (p_quest_no = -1 OR log.quest_no = p_quest_no)
      AND (p_npc_id = -1 OR log.npc_id = p_npc_id);
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadrecipeinfo"(
    p_char_id integer
) RETURNS TABLE (recipe_id integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT recipe.recipe_id
    FROM "world"."user_recipe" AS recipe
    WHERE recipe.char_id = p_char_id
    ORDER BY recipe.recipe_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadhenna"(
    p_char_id integer
) RETURNS TABLE (
    henna_1 integer,
    henna_2 integer,
    henna_3 integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT henna.henna_1, henna.henna_2, henna.henna_3
    FROM "world"."user_henna" AS henna
    WHERE henna.char_id = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadssquserinfo"(
    p_char_id integer
) RETURNS TABLE (
    round_number integer,
    join_time integer,
    part integer,
    ssq_position integer,
    seal_selection_number integer,
    point bigint,
    twilight_a bigint,
    twilight_b bigint,
    twilight_c bigint,
    dawn_a bigint,
    dawn_b bigint,
    dawn_c bigint,
    ticket_buy_count integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT ssq.round_number,
           ssq.ssq_join_time,
           ssq.ssq_part::integer,
           ssq.ssq_position::integer,
           ssq.seal_selection_no::integer,
           ssq.ssq_point,
           ssq.twilight_a_item_num,
           ssq.twilight_b_item_num,
           ssq.twilight_c_item_num,
           ssq.dawn_a_item_num,
           ssq.dawn_b_item_num,
           ssq.dawn_c_item_num,
           ssq.ticket_buy_count::integer
    FROM "world"."ssq_user_data" AS ssq
    WHERE ssq.char_id = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadmergedinfo"(
    p_id integer,
    p_type integer
) RETURNS TABLE (
    previous_server integer,
    name_origin varchar(50)
)
LANGUAGE sql
STABLE
AS $function$
    SELECT merged.previous_server, merged.name_origin
    FROM "world"."changed_name_by_merge" AS merged
    WHERE merged.id = p_id
      AND merged.type = p_type
      AND merged.change_flag = 0;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loaduserbeauty"(
    p_char_id integer
) RETURNS TABLE (
    face_index_ex integer,
    hair_shape_index_ex integer,
    hair_color_index_ex integer,
    hair_accessory_flag integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT COALESCE(max(beauty.face_index_ex), 0),
           COALESCE(max(beauty.hairshape_index_ex), 0),
           COALESCE(max(beauty.haircolor_index_ex), 0),
           COALESCE(max(beauty.hair_accessory_flag), 1)
    FROM "world"."user_beauty" AS beauty
    WHERE beauty.char_id = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadmatchingpoint"(
    p_char_id integer
) RETURNS TABLE (
    pledge_game_point bigint,
    olympiad_point bigint
)
LANGUAGE sql
STABLE
AS $function$
    SELECT matching.pledgegame_point, matching.olympiad_point
    FROM "world"."user_matching_point" AS matching
    WHERE matching.char_id = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadcharvitality"(
    p_char_id integer,
    p_subjob_id integer
) RETURNS TABLE (
    vital_point integer,
    restore_count integer,
    latest_reset_date_vp integer,
    latest_reset_date_rc integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT vitality.vital_point,
           vitality.restore_count,
           vitality.latest_reset_date,
           vitality.latest_reset_date_rc
    FROM "world"."user_vitality" AS vitality
    WHERE vitality.char_id = p_char_id
      AND vitality.subjob_id = p_subjob_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savecharvitality"(
    p_char_id integer,
    p_subjob_id integer,
    p_vital_point integer,
    p_restore_count integer,
    p_latest_reset_date_vp integer,
    p_latest_reset_date_rc integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."user_vitality" (
        char_id, subjob_id, vital_point, restore_count,
        latest_reset_date, latest_reset_date_rc
    ) VALUES (
        p_char_id, p_subjob_id, p_vital_point, p_restore_count,
        p_latest_reset_date_vp, p_latest_reset_date_rc
    )
    ON CONFLICT (char_id, subjob_id) DO UPDATE
    SET vital_point = EXCLUDED.vital_point,
        restore_count = EXCLUDED.restore_count,
        latest_reset_date = EXCLUDED.latest_reset_date,
        latest_reset_date_rc = EXCLUDED.latest_reset_date_rc;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loaduserpledgebonus"(
    p_char_id integer,
    p_activity integer
) RETURNS TABLE (received_time integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT COALESCE(
        (
            SELECT bonus.received_time
            FROM "world"."user_pledge_bonus" AS bonus
            WHERE bonus.char_id = p_char_id
              AND bonus.activity = p_activity
        ),
        0
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updateuserpledgebonus"(
    p_char_id integer,
    p_activity integer,
    p_received_time integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."user_pledge_bonus" (
        char_id, activity, received_time
    ) VALUES (
        p_char_id, p_activity, p_received_time
    )
    ON CONFLICT (char_id, activity) DO UPDATE
    SET received_time = EXCLUDED.received_time;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getnoblessbyid"(
    p_char_id integer
) RETURNS TABLE (
    nobless_type integer,
    hero_type integer,
    win_count integer,
    previous_point integer,
    olympiad_point integer,
    match_count integer,
    olympiad_win_count integer,
    olympiad_lose_count integer,
    history_open integer,
    trade_point integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT nobless.nobless_type::integer,
           nobless.hero_type::integer,
           COALESCE(nobless.win_count, 0),
           COALESCE(nobless.previous_point, 0),
           COALESCE(nobless.olympiad_point, 0),
           COALESCE(nobless.match_count, 0),
           COALESCE(nobless.olympiad_win_count, 0),
           COALESCE(nobless.olympiad_lose_count, 0),
           COALESCE(nobless.history_open, 0)::integer,
           nobless.trade_point
    FROM "world"."user_nobless" AS nobless
    WHERE nobless.char_id = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setuserban"(
    p_char_id integer,
    p_status integer,
    p_ban_hour integer,
    p_ban_end integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF p_ban_hour = 0 THEN
        DELETE FROM "world"."user_ban" AS ban
        WHERE ban.char_id = p_char_id;
        RETURN;
    END IF;

    INSERT INTO "world"."user_ban" (
        char_id, status, ban_date, ban_hour, ban_end
    ) VALUES (
        p_char_id, p_status, "world"."mssql_getdate"(),
        p_ban_hour::smallint, p_ban_end
    )
    ON CONFLICT (char_id) DO UPDATE
    SET status = EXCLUDED.status,
        ban_date = EXCLUDED.ban_date,
        ban_hour = EXCLUDED.ban_hour,
        ban_end = EXCLUDED.ban_end;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_reloadaccounthighestexpandlevel"(
    p_account_id integer
) RETURNS TABLE (
    char_id integer,
    level integer
)
LANGUAGE sql
STABLE
AS $function$
    WITH top_subjob AS (
        SELECT subjob.char_id, subjob.subjob_id,
               subjob.level::integer AS level, subjob.exp
        FROM "world"."user_subjob" AS subjob
        WHERE subjob.char_id IN (
            SELECT character.char_id
            FROM "world"."user_data" AS character
            WHERE character.account_id = p_account_id
        )
        ORDER BY subjob.exp DESC, subjob.char_id
        LIMIT 1
    ),
    top_character AS (
        SELECT character.char_id,
               character.subjob_id,
               character.lev::integer AS level,
               character.exp
        FROM "world"."user_data" AS character
        WHERE p_account_id > 0
          AND character.account_id = p_account_id
        ORDER BY character.exp DESC, character.char_id
        LIMIT 1
    )
    SELECT
        CASE
            WHEN subjob.exp IS NULL THEN character.char_id
            WHEN character.char_id = subjob.char_id
             AND character.subjob_id = subjob.subjob_id
                THEN character.char_id
            WHEN character.exp >= subjob.exp THEN character.char_id
            ELSE subjob.char_id
        END,
        CASE
            WHEN subjob.exp IS NULL THEN character.level
            WHEN character.char_id = subjob.char_id
             AND character.subjob_id = subjob.subjob_id
                THEN character.level
            WHEN character.exp >= subjob.exp THEN character.level
            ELSE subjob.level
        END
    FROM top_character AS character
    LEFT JOIN top_subjob AS subjob ON true;
$function$;

-- Current CWareHouse::LoadItems binds 23 values.  The modified-287 body
-- provides the first 21. Current CItem fields +0x50/+0x100 are the final two.
CREATE OR REPLACE FUNCTION "world"."lin_loaditems"(
    p_char_id integer,
    p_warehouse integer
) RETURNS TABLE (
    item_id integer,
    item_type integer,
    amount bigint,
    enchant integer,
    eroded integer,
    bless integer,
    ident integer,
    wished integer,
    variation_opt1 integer,
    variation_opt2 integer,
    intensive_item_type integer,
    inventory_slot_index integer,
    appearance_type integer,
    attack_attribute_type smallint,
    attack_attribute_value smallint,
    defend_attribute_0 smallint,
    defend_attribute_1 smallint,
    defend_attribute_2 smallint,
    defend_attribute_3 smallint,
    defend_attribute_4 smallint,
    defend_attribute_5 smallint,
    hlocked smallint,
    use_count smallint
)
LANGUAGE sql
STABLE
AS $function$
    SELECT item.item_id,
           item.item_type,
           item.amount,
           item.enchant,
           item.eroded,
           item.bless::integer,
           item.ident,
           item.wished::integer,
           COALESCE(item.variation_opt1, 0),
           COALESCE(item.variation_opt2, 0),
           COALESCE(item.intensive_item_type, 0),
           COALESCE(item.inventory_slot_index, -1),
           COALESCE(appearance.apperance_type, 0),
           COALESCE(attribute.attack_attribute_type, -2)::smallint,
           COALESCE(attribute.attack_attribute_value, 0)::smallint,
           COALESCE(attribute.defend_attribute_0, 0)::smallint,
           COALESCE(attribute.defend_attribute_1, 0)::smallint,
           COALESCE(attribute.defend_attribute_2, 0)::smallint,
           COALESCE(attribute.defend_attribute_3, 0)::smallint,
           COALESCE(attribute.defend_attribute_4, 0)::smallint,
           COALESCE(attribute.defend_attribute_5, 0)::smallint,
           item.hlocked,
           item.use_count
    FROM "world"."user_item" AS item
    LEFT JOIN "world"."user_item_attribute" AS attribute
      ON attribute.item_id = item.item_id
    LEFT JOIN "world"."user_item_apperance" AS appearance
      ON appearance.item_id = item.item_id
    WHERE item.char_id = p_char_id
      AND item.warehouse = p_warehouse
      AND item.item_type > 0;
$function$;

COMMIT;
