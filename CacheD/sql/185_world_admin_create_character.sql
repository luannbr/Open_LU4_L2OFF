-- Current Cached.exe AQ 93 / CUserDb::CreateUser 0x005c3410 database slice.
--
-- The function name, 28-parameter ABI, bind order and fetched integer result
-- are fixed by the current machine code. The post-create field mutations are
-- kept by the matching classic_3.0 and modified-287 SQL.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_createcharwithsubjob"(
    p_char_name text,
    p_account_name text,
    p_account_id integer,
    p_pledge_id integer,
    p_builder integer,
    p_gender integer,
    p_race integer,
    p_class integer,
    p_world integer,
    p_xloc integer,
    p_yloc integer,
    p_zloc integer,
    p_hp double precision,
    p_mp double precision,
    p_sp bigint,
    p_exp bigint,
    p_level integer,
    p_alignment integer,
    p_pk integer,
    p_duel integer,
    p_pk_pardon integer,
    p_face_index integer,
    p_hair_shape_index integer,
    p_hair_color_index integer,
    p_subjob_id integer,
    p_main_class integer,
    p_dual_class integer,
    p_cp double precision
) RETURNS TABLE (char_id integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_char_id integer := 0;
BEGIN
    SELECT created.char_id
    INTO v_char_id
    FROM "world"."lin_createchar"(
        p_char_name,
        p_account_name,
        p_account_id,
        p_pledge_id,
        p_builder,
        p_gender,
        p_race,
        p_class,
        p_world,
        p_xloc,
        p_yloc,
        p_zloc,
        p_hp,
        p_mp,
        p_sp,
        p_exp,
        p_level,
        p_alignment,
        p_pk,
        p_duel,
        p_pk_pardon,
        p_face_index,
        p_hair_shape_index,
        p_hair_color_index
    ) AS created
    LIMIT 1;

    IF v_char_id > 0 THEN
        UPDATE "world"."user_data"
        SET subjob_id = p_subjob_id,
            subjob0_class = p_main_class,
            cp = p_cp,
            max_cp = p_cp,
            dual_class_id = p_dual_class
        WHERE user_data.char_id = v_char_id;

        INSERT INTO "world"."user_data_extra" (char_id, modified_date)
        VALUES (v_char_id, "world"."mssql_getdate"())
        ON CONFLICT ON CONSTRAINT "pk_user_data_extra" DO UPDATE
        SET modified_date = EXCLUDED.modified_date;
    END IF;

    RETURN QUERY SELECT v_char_id;
END;
$function$;

COMMIT;
