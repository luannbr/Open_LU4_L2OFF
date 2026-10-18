-- Current Cached.exe primary opcodes 221-223 subjob transition persistence.
--
-- The executable fixes all three callable ABIs. Classic 3.0, modified 287
-- and Ertheia keep identical full lin_ChangeSubJob bodies and identical
-- modern lin_DeleteSubJob bodies. Lin_GetSubJobLevel has the same effective
-- two-column projection across the compatible modern donors.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_deletesubjob"(
    p_char_id integer,
    p_subjob_id integer
) RETURNS TABLE (
    result integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_current_subjob_id integer;
    v_dual_class_id integer;
    v_main_hp double precision;
    v_main_mp double precision;
    v_main_sp bigint;
    v_main_exp bigint;
    v_main_level integer;
    v_affected_rows integer;
BEGIN
    BEGIN
        SELECT user_row."subjob_id", user_row."dual_class_id"
        INTO v_current_subjob_id, v_dual_class_id
        FROM "world"."user_data" AS user_row
        WHERE user_row."char_id" = p_char_id;
        IF NOT FOUND OR p_subjob_id NOT BETWEEN 1 AND 3 THEN
            RAISE EXCEPTION USING ERRCODE = 'P0001';
        END IF;

        DELETE FROM "world"."shortcut_data"
        WHERE "char_id" = p_char_id
          AND "subjob_id" = p_subjob_id;

        DELETE FROM "world"."user_henna"
        WHERE "char_id" = p_char_id
          AND "subjob_id" = p_subjob_id;

        DELETE FROM "world"."user_skill"
        WHERE "char_id" = p_char_id
          AND "subjob_id" = p_subjob_id;

        DELETE FROM "world"."user_skill_reuse_delay"
        WHERE "char_id" = p_char_id
          AND "subjob_id" = p_subjob_id;

        DELETE FROM "world"."user_log"
        WHERE "char_id" = p_char_id
          AND "subjob_id" = p_subjob_id;

        UPDATE "world"."user_data" AS user_row
        SET "subjob1_class" = CASE
                WHEN p_subjob_id = 1 THEN -1
                ELSE user_row."subjob1_class"
            END,
            "subjob2_class" = CASE
                WHEN p_subjob_id = 2 THEN -1
                ELSE user_row."subjob2_class"
            END,
            "subjob3_class" = CASE
                WHEN p_subjob_id = 3 THEN -1
                ELSE user_row."subjob3_class"
            END
        WHERE user_row."char_id" = p_char_id;
        GET DIAGNOSTICS v_affected_rows = ROW_COUNT;
        IF v_affected_rows <> 1 THEN
            RAISE EXCEPTION USING ERRCODE = 'P0001';
        END IF;

        IF v_current_subjob_id = p_subjob_id THEN
            SELECT
                main_subjob."hp",
                main_subjob."mp",
                main_subjob."sp",
                main_subjob."exp",
                main_subjob."level"::integer
            INTO
                v_main_hp,
                v_main_mp,
                v_main_sp,
                v_main_exp,
                v_main_level
            FROM "world"."user_subjob" AS main_subjob
            WHERE main_subjob."char_id" = p_char_id
              AND main_subjob."subjob_id" = 0;
            IF NOT FOUND THEN
                RAISE EXCEPTION USING ERRCODE = 'P0001';
            END IF;

            UPDATE "world"."user_data" AS user_row
            SET "class" = user_row."subjob0_class",
                "subjob_id" = 0,
                "hp" = v_main_hp,
                "mp" = v_main_mp,
                "sp" = v_main_sp,
                "exp" = v_main_exp,
                "lev" = v_main_level,
                "dual_class_id" = CASE
                    WHEN v_dual_class_id = p_subjob_id THEN -1
                    ELSE v_dual_class_id
                END
            WHERE user_row."char_id" = p_char_id;
            GET DIAGNOSTICS v_affected_rows = ROW_COUNT;
            IF v_affected_rows <> 1 THEN
                RAISE EXCEPTION USING ERRCODE = 'P0001';
            END IF;
        END IF;

        DELETE FROM "world"."user_subjob"
        WHERE "char_id" = p_char_id
          AND "subjob_id" = p_subjob_id;
        GET DIAGNOSTICS v_affected_rows = ROW_COUNT;
        IF v_affected_rows <> 1 THEN
            RAISE EXCEPTION USING ERRCODE = 'P0001';
        END IF;
    EXCEPTION
        WHEN OTHERS THEN
            RETURN QUERY SELECT 0;
            RETURN;
    END;

    RETURN QUERY SELECT 1;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_changesubjob"(
    p_char_id integer,
    p_subjob_id integer,
    p_old_subjob_id integer,
    p_hp double precision,
    p_mp double precision,
    p_sp bigint,
    p_exp bigint,
    p_level integer,
    p_henna_1 integer,
    p_henna_2 integer,
    p_henna_3 integer,
    p_pp integer
) RETURNS TABLE (
    hp double precision,
    mp double precision,
    sp bigint,
    exp bigint,
    level integer,
    henna_1 integer,
    henna_2 integer,
    henna_3 integer,
    pp integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_hp double precision;
    v_mp double precision;
    v_sp bigint;
    v_exp bigint;
    v_level integer;
    v_henna_1 integer;
    v_henna_2 integer;
    v_henna_3 integer;
    v_pp integer;
BEGIN
    BEGIN
        PERFORM 1
        FROM "world"."user_data" AS user_row
        WHERE user_row."char_id" = p_char_id;

        UPDATE "world"."user_subjob" AS old_subjob
        SET "hp" = p_hp,
            "mp" = p_mp,
            "sp" = p_sp,
            "exp" = p_exp,
            "level" = p_level,
            "henna_1" = p_henna_1,
            "henna_2" = p_henna_2,
            "henna_3" = p_henna_3,
            "pp" = p_pp
        WHERE old_subjob."char_id" = p_char_id
          AND old_subjob."subjob_id" = p_old_subjob_id;

        SELECT
            new_subjob."hp",
            new_subjob."mp",
            new_subjob."sp",
            new_subjob."exp",
            new_subjob."level"::integer,
            new_subjob."henna_1",
            new_subjob."henna_2",
            new_subjob."henna_3",
            new_subjob."pp"
        INTO
            v_hp,
            v_mp,
            v_sp,
            v_exp,
            v_level,
            v_henna_1,
            v_henna_2,
            v_henna_3,
            v_pp
        FROM "world"."user_subjob" AS new_subjob
        WHERE new_subjob."char_id" = p_char_id
          AND new_subjob."subjob_id" = p_subjob_id;

        IF p_subjob_id NOT BETWEEN 0 AND 3 THEN
            RAISE EXCEPTION USING ERRCODE = 'P0001';
        END IF;

        UPDATE "world"."user_data" AS user_row
        SET "class" = CASE p_subjob_id
                WHEN 0 THEN user_row."subjob0_class"
                WHEN 1 THEN user_row."subjob1_class"
                WHEN 2 THEN user_row."subjob2_class"
                WHEN 3 THEN user_row."subjob3_class"
            END,
            "subjob_id" = p_subjob_id,
            "hp" = v_hp,
            "mp" = v_mp,
            "sp" = v_sp,
            "exp" = v_exp,
            "lev" = v_level,
            "pp" = v_pp
        WHERE user_row."char_id" = p_char_id;

        DELETE FROM "world"."user_newactiveskill"
        WHERE "char_id" = p_char_id;

        UPDATE "world"."user_henna"
        SET "henna_1" = v_henna_1,
            "henna_2" = v_henna_2,
            "henna_3" = v_henna_3
        WHERE "char_id" = p_char_id;
    EXCEPTION
        WHEN OTHERS THEN
            NULL;
    END;

    RETURN QUERY SELECT
        v_hp,
        v_mp,
        v_sp,
        v_exp,
        v_level,
        v_henna_1,
        v_henna_2,
        v_henna_3,
        v_pp;
END;
$function$;

-- Current CUser::ChangeSubJob(int) 0x005b1268 fixes the two-input ABI. The
-- statement order below is the matching Classic 3.0/modified-287 procedure:
-- save the active snapshot, load the selected one and copy its henna back.
CREATE OR REPLACE FUNCTION "world"."lin_changesubjobbysubjobid"(
    p_char_id integer,
    p_subjob_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_origin_subjob_id integer;
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."user_data" AS user_row
        WHERE user_row."char_id" = p_char_id
          AND user_row."subjob_id" = p_subjob_id
    ) THEN
        RETURN;
    END IF;

    IF p_subjob_id NOT BETWEEN 0 AND 3 THEN
        RAISE EXCEPTION USING ERRCODE = 'P0001';
    END IF;

    SELECT user_row."subjob_id"
    INTO v_origin_subjob_id
    FROM "world"."user_data" AS user_row
    WHERE user_row."char_id" = p_char_id;

    UPDATE "world"."user_subjob" AS old_subjob
    SET "hp" = user_row."hp",
        "mp" = user_row."mp",
        "sp" = user_row."sp",
        "exp" = user_row."exp",
        "level" = user_row."lev",
        "pp" = user_row."pp"
    FROM "world"."user_data" AS user_row
    WHERE old_subjob."char_id" = p_char_id
      AND old_subjob."subjob_id" = user_row."subjob_id"
      AND user_row."char_id" = p_char_id;

    UPDATE "world"."user_subjob" AS old_subjob
    SET "henna_1" = henna."henna_1",
        "henna_2" = henna."henna_2",
        "henna_3" = henna."henna_3"
    FROM "world"."user_henna" AS henna
    WHERE old_subjob."char_id" = p_char_id
      AND old_subjob."subjob_id" = v_origin_subjob_id
      AND henna."char_id" = p_char_id;

    UPDATE "world"."user_data" AS user_row
    SET "hp" = selected."hp",
        "mp" = selected."mp",
        "sp" = selected."sp",
        "exp" = selected."exp",
        "lev" = selected."level",
        "pp" = selected."pp",
        "subjob_id" = p_subjob_id
    FROM "world"."user_subjob" AS selected
    WHERE user_row."char_id" = p_char_id
      AND selected."char_id" = p_char_id
      AND selected."subjob_id" = p_subjob_id;

    UPDATE "world"."user_data" AS user_row
    SET "class" = CASE p_subjob_id
        WHEN 0 THEN user_row."subjob0_class"
        WHEN 1 THEN user_row."subjob1_class"
        WHEN 2 THEN user_row."subjob2_class"
        WHEN 3 THEN user_row."subjob3_class"
    END
    WHERE user_row."char_id" = p_char_id;

    UPDATE "world"."user_henna" AS henna
    SET "henna_1" = selected."henna_1",
        "henna_2" = selected."henna_2",
        "henna_3" = selected."henna_3"
    FROM "world"."user_subjob" AS selected
    WHERE henna."char_id" = p_char_id
      AND selected."char_id" = p_char_id
      AND selected."subjob_id" = p_subjob_id;
END;
$function$;

-- Current CUser::AddSubJob 0x005b1594 binds the character id followed by the
-- ten snapshot values below and consumes one integer result row. Compatible
-- SQL donors agree on the duplicate check and atomic class-slot update.
CREATE OR REPLACE FUNCTION "world"."lin_addsubjob"(
    p_char_id integer,
    p_new_class integer,
    p_hp double precision,
    p_mp double precision,
    p_sp bigint,
    p_exp bigint,
    p_level integer,
    p_subjob_id integer,
    p_henna_1 integer,
    p_henna_2 integer,
    p_henna_3 integer
) RETURNS TABLE (
    result integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_affected_rows integer;
BEGIN
    IF p_subjob_id NOT BETWEEN 1 AND 3 OR EXISTS (
        SELECT 1
        FROM "world"."user_subjob" AS subjob
        WHERE subjob."char_id" = p_char_id
          AND subjob."subjob_id" = p_subjob_id
    ) THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END IF;

    BEGIN
        INSERT INTO "world"."user_subjob" (
            "char_id",
            "hp",
            "mp",
            "sp",
            "exp",
            "level",
            "henna_1",
            "henna_2",
            "henna_3",
            "subjob_id",
            "create_date",
            "max_level",
            "pp"
        ) VALUES (
            p_char_id,
            p_hp,
            p_mp,
            p_sp,
            p_exp,
            p_level,
            p_henna_1,
            p_henna_2,
            p_henna_3,
            p_subjob_id,
            "world"."mssql_getdate"(),
            0,
            0
        );

        UPDATE "world"."user_data" AS user_row
        SET "subjob1_class" = CASE
                WHEN p_subjob_id = 1 THEN p_new_class
                ELSE user_row."subjob1_class"
            END,
            "subjob2_class" = CASE
                WHEN p_subjob_id = 2 THEN p_new_class
                ELSE user_row."subjob2_class"
            END,
            "subjob3_class" = CASE
                WHEN p_subjob_id = 3 THEN p_new_class
                ELSE user_row."subjob3_class"
            END
        WHERE user_row."char_id" = p_char_id;
        GET DIAGNOSTICS v_affected_rows = ROW_COUNT;
        IF v_affected_rows <> 1 THEN
            RAISE EXCEPTION USING ERRCODE = 'P0001';
        END IF;
    EXCEPTION
        WHEN integrity_constraint_violation OR data_exception OR
             raise_exception THEN
            RETURN QUERY SELECT 0;
            RETURN;
    END;

    RETURN QUERY SELECT 1;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getsubjoblevel"(
    p_char_id integer
) RETURNS TABLE (
    subjob_id integer,
    effective_level integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        subjob."subjob_id",
        CASE
            WHEN user_row."subjob_id" = subjob."subjob_id"
                THEN user_row."lev"::integer
            ELSE subjob."level"::integer
        END
    FROM "world"."user_subjob" AS subjob
    JOIN "world"."user_data" AS user_row
      ON user_row."char_id" = subjob."char_id"
    WHERE subjob."char_id" = p_char_id
$function$;

COMMIT;
