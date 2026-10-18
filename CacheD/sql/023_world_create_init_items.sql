-- Current Cached.exe primary opcode 5 (CreateInitItems) database slice.
--
-- The current image fixes all three callable ABIs below.  The compatible
-- modern SQL donors keep the relational bodies of
-- lin_SaveCharacter and lin_SetShortCut.  Current CWareHouse::MakeNewItem
-- extends the old lin_CreateItem ABI with hlocked/use_count as parameters
-- 13 and 14. Those positions come from the current machine-code bind helpers.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_createitem"(
    p_char_id integer,
    p_item_type integer,
    p_amount bigint,
    p_enchant integer,
    p_eroded integer,
    p_bless integer,
    p_ident integer,
    p_wished integer,
    p_warehouse integer,
    p_variation_opt1 integer,
    p_variation_opt2 integer,
    p_intensive_item_type integer,
    p_hlocked smallint,
    p_use_count smallint
) RETURNS TABLE (item_id integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_item_id integer;
BEGIN
    INSERT INTO "world"."user_item" (
        char_id,
        item_type,
        amount,
        enchant,
        eroded,
        bless,
        ident,
        wished,
        warehouse,
        variation_opt1,
        variation_opt2,
        intensive_item_type,
        hlocked,
        use_count
    ) VALUES (
        p_char_id,
        p_item_type,
        p_amount,
        p_enchant,
        p_eroded,
        p_bless::smallint,
        p_ident,
        p_wished::smallint,
        p_warehouse,
        p_variation_opt1,
        p_variation_opt2,
        p_intensive_item_type,
        p_hlocked,
        p_use_count
    )
    RETURNING "user_item"."item_id" INTO v_item_id;

    RETURN QUERY SELECT v_item_id;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savecharacter"(
    p_subjob_id integer,
    p_dual_class_id integer,
    p_pledge_id integer,
    p_class integer,
    p_xloc integer,
    p_yloc integer,
    p_zloc integer,
    p_is_in_vehicle integer,
    p_hp double precision,
    p_mp double precision,
    p_max_hp double precision,
    p_max_mp double precision,
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
    p_ssq_dawn_round integer,
    p_char_id integer,
    p_subjob0_class integer,
    p_subjob1_class integer,
    p_subjob2_class integer,
    p_subjob3_class integer,
    p_cp double precision,
    p_max_cp double precision
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_level_previous integer;
    v_class_previous integer;
    v_subjob_id_previous integer;
BEGIN
    -- The current 30-input ABI still carries pledge_id.  The compatible
    -- shipping procedure body does not update that column.
    PERFORM p_pledge_id;

    SELECT user_data.lev,
           user_data.class,
           user_data.subjob_id
    INTO v_level_previous,
         v_class_previous,
         v_subjob_id_previous
    FROM "world"."user_data" AS user_data
    WHERE user_data.char_id = p_char_id;

    UPDATE "world"."user_data"
    SET subjob_id = p_subjob_id,
        dual_class_id = p_dual_class_id,
        class = p_class,
        xloc = p_xloc,
        yloc = p_yloc,
        zloc = p_zloc,
        isinvehicle = p_is_in_vehicle,
        hp = p_hp,
        mp = p_mp,
        -- The matching MSSQL columns are integers even though the procedure
        -- receives float values. Truncation keeps that assignment.
        max_hp = trunc(p_max_hp)::integer,
        max_mp = trunc(p_max_mp)::integer,
        sp = p_sp,
        exp = p_exp,
        lev = p_level,
        align = p_alignment,
        pk = p_pk,
        duel = p_duel,
        pkpardon = p_pk_pardon,
        face_index = p_face_index,
        hair_shape_index = p_hair_shape_index,
        hair_color_index = p_hair_color_index,
        ssq_dawn_round = p_ssq_dawn_round,
        subjob0_class = p_subjob0_class,
        subjob1_class = p_subjob1_class,
        subjob2_class = p_subjob2_class,
        subjob3_class = p_subjob3_class,
        cp = p_cp,
        max_cp = p_max_cp
    WHERE char_id = p_char_id;

    IF p_subjob_id = v_subjob_id_previous AND
       (p_level <> v_level_previous OR p_class <> v_class_previous) THEN
        INSERT INTO "world"."user_data_extra" (char_id, modified_date)
        VALUES (p_char_id, "world"."mssql_getdate"())
        ON CONFLICT ON CONSTRAINT "pk_user_data_extra" DO UPDATE
        SET modified_date = EXCLUDED.modified_date;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setshortcut"(
    p_char_id integer,
    p_subjob_id integer,
    p_slotnum integer,
    p_type integer,
    p_id integer,
    p_macro text
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_char_id integer := p_char_id;
    v_subjob_id integer := p_subjob_id;
BEGIN
    IF v_char_id = 0 THEN
        v_char_id := v_subjob_id;
        v_subjob_id := 0;
    END IF;

    IF p_type = 0 THEN
        DELETE FROM "world"."shortcut_data"
        WHERE char_id = v_char_id
          AND subjob_id = v_subjob_id
          AND slotnum = p_slotnum;
    ELSE
        INSERT INTO "world"."shortcut_data" (
            char_id,
            slotnum,
            shortcut_type,
            shortcut_id,
            shortcut_macro,
            subjob_id
        ) VALUES (
            v_char_id,
            p_slotnum,
            p_type,
            p_id,
            p_macro::varchar(256),
            v_subjob_id
        )
        ON CONFLICT (char_id, slotnum, subjob_id) DO UPDATE
        SET shortcut_type = EXCLUDED.shortcut_type,
            shortcut_id = EXCLUDED.shortcut_id,
            shortcut_macro = EXCLUDED.shortcut_macro;
    END IF;
END;
$function$;

COMMIT;
