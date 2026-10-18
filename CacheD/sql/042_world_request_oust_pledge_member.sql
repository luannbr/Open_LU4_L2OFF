-- Current Cached.exe primary opcode 31 (RequestOustPledgeMember)
-- name-based user load and pledge removal persistence.
--
-- The machine code fixes the three callable ABIs and their ordered bindings.
-- Compatible modern donors keep identical bodies/effects. The by-name load
-- delegates to the already recovered 128-column by-ID function,
-- matching the donor procedure's TOP 1 + EXEC control flow.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_getuserdatacountbycharname"(
    p_character_name text
) RETURNS TABLE (user_count integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT count(*)::integer
    FROM "world"."user_data" AS character
    WHERE "world"."mssql_text_eq"(character.char_name, p_character_name);
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getuserdatabycharname"(
    p_character_name text
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
    WITH selected_character AS (
        SELECT character.char_id
        FROM "world"."user_data" AS character
        WHERE "world"."mssql_text_eq"(
            character.char_name,
            p_character_name
        )
        LIMIT 1
    )
    SELECT loaded.*
    FROM selected_character
    CROSS JOIN LATERAL
        "world"."lin_getuserdatabycharid"(
            selected_character.char_id
        ) AS loaded;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_oustpledgemember"(
    p_pledge_id integer,
    p_pledge_type integer,
    p_character_name text
) RETURNS TABLE (character_id integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_character_id integer := 0;
    v_match_count integer := 0;
    v_update_count integer := 0;
BEGIN
    SELECT count(*)::integer,
           COALESCE(min(character.char_id), 0)
    INTO v_match_count,
         v_character_id
    FROM "world"."user_data" AS character
    WHERE "world"."mssql_text_eq"(character.char_name, p_character_name)
      AND character.pledge_type = p_pledge_type;

    IF v_match_count <> 1 THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END IF;

    UPDATE "world"."user_data" AS character
    SET pledge_id = 0,
        pledge_type = 0,
        grade_id = 0
    WHERE character.char_id = v_character_id
      AND character.pledge_id = p_pledge_id
      AND character.pledge_type = p_pledge_type;

    GET DIAGNOSTICS v_update_count = ROW_COUNT;
    IF v_update_count = 1 THEN
        RETURN QUERY SELECT v_character_id;
    ELSE
        RETURN QUERY SELECT 0;
    END IF;
END;
$function$;

COMMIT;
